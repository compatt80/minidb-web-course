#include "engine.hpp"
#include "sql.hpp"
#include <fstream>
#include <iostream>
#include <iterator>
#include <charconv>
#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <io.h>
#else
#include <unistd.h>
#endif

using namespace minidb;
static void print(const Result& r) {
    auto line=[](const std::vector<std::string>& row) { for(size_t i=0;i<row.size();++i) { if(i) std::cout<<'\t'; std::cout<<row[i]; } std::cout<<'\n'; };
    if(!r.headers.empty()) { line(r.headers); for(const auto& row:r.rows) line(row); }
    if(!r.message.empty()) std::cout<<r.message<<'\n';
    std::cout.flush();
}
int main(int argc,char** argv) {
#ifdef _WIN32
    SetErrorMode(SEM_FAILCRITICALERRORS|SEM_NOGPFAULTERRORBOX|SEM_NOOPENFILEERRORBOX);
    SetConsoleOutputCP(CP_UTF8); SetConsoleCP(CP_UTF8);
    bool terminal=_isatty(_fileno(stdin))!=0;
#else
    bool terminal=isatty(STDIN_FILENO)!=0;
#endif
    std::string file="data/minidb.db",script; bool keepGoing=false;
    uint32_t lockTimeoutMs=5000;
    for(int i=1;i<argc;++i) {
        const std::string arg=argv[i];
        if((arg=="--db"||arg=="--script")&&i+1<argc) { if(arg=="--db") file=argv[++i]; else script=argv[++i]; }
        else if(arg=="--lock-timeout-ms"&&i+1<argc) {
            const std::string value=argv[++i];
            auto parsed=std::from_chars(value.data(),value.data()+value.size(),lockTimeoutMs);
            if(parsed.ec!=std::errc()||parsed.ptr!=value.data()+value.size()||lockTimeoutMs>600000) {
                std::cerr<<"--lock-timeout-ms requires an integer from 0 to 600000\n"; return 2;
            }
        }
        else if(arg=="--continue-on-error") keepGoing=true;
        else if(arg=="--help") { std::cout<<"MiniDB --db FILE [--script FILE] [--continue-on-error] [--lock-timeout-ms 0..600000]\n"; return 0; }
        else { std::cerr<<"Unknown or incomplete option: "<<arg<<'\n'; return 2; }
    }
    try {
        Engine engine(file,lockTimeoutMs); bool quit=false,failed=false; size_t statementNumber=0;
        auto run=[&](const std::string& sql) {
            try {
                ++statementNumber;
                if(tokenize(sql).size()==1) { --statementNumber; return; }
                auto s=Parser(sql).parse(); print(engine.execute(s)); quit=s.kind==Statement::Quit;
            } catch(const std::exception& e) {
                std::cerr<<"ERROR [statement "<<statementNumber<<"]: "<<e.what()<<'\n'; failed=true;
                if(!terminal&&!keepGoing) quit=true;
            }
        };
        if(!script.empty()) {
            terminal=false;
            std::ifstream in(script,std::ios::binary); if(!in) throw std::runtime_error("Cannot open script: "+script);
            std::string buffer((std::istreambuf_iterator<char>(in)),{});
            if(buffer.size()>=3&&buffer.substr(0,3)=="\xEF\xBB\xBF") buffer.erase(0,3);
            for(const auto& sql:splitStatements(buffer,true)) { run(sql); if(quit) break; }
        } else {
            if(terminal) std::cout<<"MiniDB 1.0 (C++17)\nType HELP; for commands. Database: "<<file<<"\n";
            std::string buffer,line;
            while(!quit) {
                if(terminal) std::cout<<(buffer.empty()?(engine.inTransaction()?"minidb[tx]> ":"minidb> "):"      ...> ")<<std::flush;
                if(!std::getline(std::cin,line)) break;
                buffer+=line+'\n';
                for(const auto& sql:splitStatements(buffer)) { run(sql); if(quit) break; }
            }
            if(!quit) for(const auto& sql:splitStatements(buffer,true)) { run(sql); if(quit) break; }
        }
        if(engine.inTransaction()) std::cerr<<"NOTICE: uncommitted transaction discarded on exit\n";
        return failed?1:0;
    } catch(const std::exception& e) { std::cerr<<"FATAL: "<<e.what()<<'\n'; return 2; }
}
