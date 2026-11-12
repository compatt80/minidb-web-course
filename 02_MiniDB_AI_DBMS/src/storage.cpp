#include "storage.hpp"
#include "page_buffer.hpp"
#include <algorithm>
#include <cerrno>
#include <cstdio>
#include <cstring>
#include <cstdlib>
#include <fstream>
#include <optional>
#include <set>
#include <sys/stat.h>
#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <io.h>
#include <direct.h>
#else
#include <fcntl.h>
#include <sys/file.h>
#include <unistd.h>
#endif

namespace minidb {
namespace {
constexpr size_t maxFile=64*1024*1024;
std::string parentPath(const std::string& p) {
#ifdef _WIN32
    auto i=p.find_last_of("/\\");
#else
    auto i=p.find_last_of('/');
#endif
    if(i==std::string::npos) return {};
    return p.substr(0,i==0?1:i);
}
void createDirectory(const std::string& p) {
    if(p.empty()||p=="."||p=="/"||p=="\\"||p.back()==':') return;
    struct stat info{};
    if(stat(p.c_str(),&info)==0) {
        if(!(info.st_mode&S_IFDIR)) throw std::runtime_error("Database parent is not a directory");
        return;
    }
    auto parent=parentPath(p); if(parent!=p) createDirectory(parent);
#ifdef _WIN32
    int result=_mkdir(p.c_str());
#else
    int result=mkdir(p.c_str(),0700);
#endif
    if(result!=0&&errno!=EEXIST) throw std::runtime_error("Cannot create database directory: "+p);
}
struct Writer {
    std::string b;
    void integer(uint64_t v,int n) { for(int i=0;i<n;++i) b.push_back(static_cast<char>((v>>(8*i))&255)); }
    void text(const std::string& s) { if(s.size()>1024*1024) throw std::runtime_error("String exceeds 1 MiB"); integer(s.size(),4); b+=s; }
    void value(const Value& v) {
        integer(v.index(),1);
        if(auto p=std::get_if<int64_t>(&v)) { uint64_t bits; std::memcpy(&bits,p,8); integer(bits,8); }
        else if(auto p=std::get_if<double>(&v)) { uint64_t bits; std::memcpy(&bits,p,8); integer(bits,8); }
        else if(auto p=std::get_if<std::string>(&v)) text(*p);
    }
};
struct Reader {
    const std::string& b; size_t pos=0;
    uint64_t integer(int n) {
        if(static_cast<size_t>(n)>b.size()-pos) throw std::runtime_error("Truncated database");
        uint64_t v=0; for(int i=0;i<n;++i) v|=static_cast<uint64_t>(static_cast<unsigned char>(b[pos++]))<<(8*i); return v;
    }
    size_t count(size_t bound) { auto n=integer(4); if(n>bound) throw std::runtime_error("Database count exceeds format limit"); return static_cast<size_t>(n); }
    std::string text() { auto n=count(1024*1024); if(n>b.size()-pos) throw std::runtime_error("Truncated database string"); auto s=b.substr(pos,n); pos+=n; return s; }
    Value value() {
        switch(integer(1)) {
            case 0:return std::monostate{};
            case 1:{ auto bits=integer(8); int64_t v; std::memcpy(&v,&bits,8); return v; }
            case 2:{ auto bits=integer(8); double v; std::memcpy(&v,&bits,8); return v; }
            case 3:return text();
            default:throw std::runtime_error("Invalid value tag");
        }
    }
};
uint64_t checksum(const std::string& b) { uint64_t h=14695981039346656037ULL; for(unsigned char c:b) { h^=c; h*=1099511628211ULL; } return h; }
bool validName(const std::string& s) {
    if(s.empty() || !((s[0]>='a'&&s[0]<='z')||s[0]=='_')) return false;
    for(char c:s) if(!((c>='a'&&c<='z')||(c>='0'&&c<='9')||c=='_')) return false;
    return true;
}
std::optional<std::string> readFile(const std::string& file, bool allowMissing) {
    errno=0;
    FILE* input=std::fopen(file.c_str(),"rb");
    if(!input) {
        if(allowMissing&&errno==ENOENT) return std::nullopt;
        throw std::runtime_error("Cannot read database file: "+file);
    }
    if(std::fseek(input,0,SEEK_END)!=0) { std::fclose(input); throw std::runtime_error("Cannot seek database file"); }
    auto size=std::ftell(input);
    if(size<0) { std::fclose(input); throw std::runtime_error("Cannot determine database file size"); }
    std::rewind(input);
    if(static_cast<size_t>(size)>maxFile) { std::fclose(input); throw std::runtime_error("Database file exceeds 64 MiB"); }
    std::string bytes(static_cast<size_t>(size),'\0');
    bool readOk=bytes.empty()||std::fread(&bytes[0],1,bytes.size(),input)==bytes.size();
    std::fclose(input);
    if(!readOk) throw std::runtime_error("Database read failed");
    return bytes;
}
void syncDirectory(const std::string& path) {
#ifndef _WIN32
    auto parent=parentPath(path); if(parent.empty()) parent=".";
    int fd=::open(parent.c_str(),O_RDONLY); if(fd>=0) { fsync(fd); ::close(fd); }
#else
    (void)path;
#endif
}
void writeAtomic(const std::string& path, const std::string& bytes) {
    const auto temp=path+".tmp"; FILE* f=std::fopen(temp.c_str(),"wb");
    if(!f) throw std::runtime_error("Cannot write database temporary file");
    bool ok=false;
    try {
        constexpr size_t pageSize=4096;
        PageBufferPool pool(pageSize,8);
        const auto pageCount=(bytes.size()+pageSize-1)/pageSize;
        const auto loader=[&](size_t pageId) {
            std::vector<uint8_t> page(pageSize,0); const auto offset=pageId*pageSize;
            const auto count=std::min(pageSize,bytes.size()-offset);
            std::copy_n(reinterpret_cast<const uint8_t*>(bytes.data()+offset),count,page.begin()); return page;
        };
        const auto flusher=[&](size_t pageId,const std::vector<uint8_t>& page) {
            const auto offset=pageId*pageSize; const auto count=std::min(pageSize,bytes.size()-offset);
            if(std::fseek(f,static_cast<long>(offset),SEEK_SET)!=0||std::fwrite(page.data(),1,count,f)!=count) throw std::runtime_error("Database page write failed");
        };
        for(size_t pageId=0;pageId<pageCount;++pageId) {
            if(pool.full()) { pool.flush(flusher); pool.evictOne(); }
            pool.fetch(pageId,loader); pool.markDirty(pageId);
        }
        pool.flush(flusher); ok=std::fflush(f)==0;
    } catch(...) { std::fclose(f); std::remove(temp.c_str()); throw; }
#ifdef _WIN32
    if(ok) ok=_commit(_fileno(f))==0;
#else
    if(ok) ok=fsync(fileno(f))==0;
#endif
    if(std::fclose(f)!=0) ok=false;
    if(!ok) { std::remove(temp.c_str()); throw std::runtime_error("Database write failed; previous snapshot retained"); }
#ifdef _WIN32
    if(!MoveFileExA(temp.c_str(),path.c_str(),MOVEFILE_REPLACE_EXISTING|MOVEFILE_WRITE_THROUGH) ) {
        std::remove(temp.c_str()); throw std::runtime_error("Database replacement failed; previous snapshot retained");
    }
#else
    if(std::rename(temp.c_str(),path.c_str())!=0) { std::remove(temp.c_str()); throw std::runtime_error("Database replacement failed"); }
#endif
    syncDirectory(path);
}
std::string encodeSnapshot(const Database& db) {
    if(db.size()>10000) throw std::runtime_error("Too many tables");
    Writer w; w.b="MINIDB01"; w.integer(1,4); w.integer(db.size(),4);
    for(const auto& pair:db) {
        const auto& t=pair.second;
        if(t.columns.size()>1024||t.indexes.size()>10000||t.rows.size()>1000000) throw std::runtime_error("Table exceeds format limits");
        w.text(t.name); w.integer(t.columns.size(),4);
        for(const auto& c:t.columns) { w.text(c.name); w.integer(static_cast<uint64_t>(c.type),1); w.integer((c.notNull?1:0)|(c.primary?2:0)|(c.unique?4:0),1); }
        w.integer(t.indexes.size(),4); for(const auto& ix:t.indexes) { w.text(ix.name); w.text(ix.column); }
        w.integer(t.rows.size(),4); for(const auto& row:t.rows) for(const auto& v:row) w.value(v);
        if(w.b.size()>maxFile-8) throw std::runtime_error("Database exceeds 64 MiB");
    }
    w.integer(checksum(w.b),8); return w.b;
}
Database decodeSnapshot(const std::string& bytes) {
    if(bytes.size()<24||bytes.size()>maxFile) throw std::runtime_error("Invalid database size (24 bytes to 64 MiB required)");
    const auto payload=bytes.substr(0,bytes.size()-8); Reader tail{bytes}; tail.pos=payload.size();
    if(checksum(payload)!=tail.integer(8)) throw std::runtime_error("Database checksum mismatch; file left unchanged");
    Reader r{payload};
    if(payload.substr(0,8)!="MINIDB01") throw std::runtime_error("Unknown database signature");
    r.pos=8; if(r.integer(4)!=1) throw std::runtime_error("Unsupported database version");
    Database db; std::set<std::string> globalIndexes;
    auto tables=r.count(10000);
    for(size_t i=0;i<tables;++i) {
        Table t; t.name=r.text(); if(!validName(t.name)||db.count(t.name)) throw std::runtime_error("Invalid or duplicate table name");
        auto columns=r.count(1024);
        for(size_t j=0;j<columns;++j) {
            Column c; c.name=r.text(); if(!validName(c.name)) throw std::runtime_error("Invalid column name");
            auto ty=r.integer(1),flags=r.integer(1); if(ty>2||flags>7) throw std::runtime_error("Invalid column metadata");
            c.type=static_cast<Type>(ty); c.notNull=flags&1; c.primary=flags&2; c.unique=flags&4; t.columns.push_back(c);
        }
        auto indexes=r.count(10000);
        for(size_t j=0;j<indexes;++j) {
            Index ix; ix.name=r.text(); ix.column=r.text();
            if(!validName(ix.name)||!globalIndexes.insert(ix.name).second) throw std::runtime_error("Invalid or duplicate index name");
            t.indexes.push_back(std::move(ix));
        }
        auto rows=r.count(1000000);
        for(size_t j=0;j<rows;++j) { Row row; for(size_t k=0;k<columns;++k) row.push_back(r.value()); t.rows.push_back(std::move(row)); }
        validateTable(t); rebuildIndexes(t); db.emplace(t.name,std::move(t));
    }
    if(r.pos!=payload.size()) throw std::runtime_error("Trailing bytes in database");
    return db;
}
}
Storage::Storage(std::string file):path(std::move(file)) {
    createDirectory(parentPath(path));
    locks=std::make_unique<DatabaseLock>(path+".lock");
}
Storage::~Storage()=default;
Database Storage::load() const {
    if(lockMode()==LockMode::None) throw std::logic_error("Database read requires a lock");
    const auto walPath=path+".wal";
    const auto wal=readFile(walPath,true);
    if(wal) {
        if(lockMode()!=LockMode::Exclusive) throw RecoveryRequired();
        auto recovered=decodeSnapshot(*wal);
        writeAtomic(path,*wal);
        std::remove(walPath.c_str());
        return recovered;
    }
    const auto bytes=readFile(path,true);
    if(!bytes) return {};
    if(bytes->empty()) throw std::runtime_error("Invalid database size (empty file)");
    return decodeSnapshot(*bytes);
}
void Storage::save(const Database& db) const {
    if(lockMode()!=LockMode::Exclusive) throw std::logic_error("Database write requires an exclusive lock");
    const auto snapshot=encodeSnapshot(db);
    const auto walPath=path+".wal";
    try {
        writeAtomic(walPath,snapshot);
        const char* crash=getenv("MINIDB_TEST_CRASH_AFTER_WAL");
        if(crash&&std::string(crash)=="1") std::abort();
        writeAtomic(path,snapshot);
        std::remove(walPath.c_str());
    } catch(...) {
        std::remove(walPath.c_str());
        throw;
    }
}
}
