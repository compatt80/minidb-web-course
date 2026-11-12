#include "model.hpp"
#include <cmath>
#include <iomanip>
#include <set>
#include <sstream>

namespace minidb {
std::string display(const Value& v) {
    if(std::holds_alternative<std::monostate>(v)) return "NULL";
    if(auto p=std::get_if<int64_t>(&v)) return std::to_string(*p);
    if(auto p=std::get_if<double>(&v)) { std::ostringstream out; out<<std::setprecision(15)<<*p; return out.str(); }
    std::string out;
    for(char ch:std::get<std::string>(v)) {
        if(ch=='\n') out+="\\n"; else if(ch=='\r') out+="\\r";
        else if(ch=='\t') out+="\\t"; else out+=ch;
    }
    return out;
}
std::string typeName(Type t) { return t==Type::Int?"INT":t==Type::Real?"REAL":"TEXT"; }
size_t columnOffset(const Table& t,const std::string& name) {
    for(size_t i=0;i<t.columns.size();++i) if(t.columns[i].name==name) return i;
    size_t match=0; size_t found=0;
    for(size_t i=0;i<t.columns.size();++i) {
        const auto dot=t.columns[i].name.find_last_of('.');
        if(dot!=std::string::npos&&t.columns[i].name.substr(dot+1)==name) { match=i; ++found; }
    }
    if(found==1) return match;
    if(found>1) throw std::runtime_error("Ambiguous column: "+name);
    throw std::runtime_error("Unknown column: "+name);
}
Value coerce(const Value& v,const Column& c) {
    if(v.index()==0) { if(c.notNull||c.primary) throw std::runtime_error("NOT NULL constraint: "+c.name); return v; }
    if(c.type==Type::Int && std::holds_alternative<int64_t>(v)) return v;
    if(c.type==Type::Real) {
        if(auto p=std::get_if<int64_t>(&v)) return static_cast<double>(*p);
        if(auto p=std::get_if<double>(&v)) { if(!std::isfinite(*p)) throw std::runtime_error("Non-finite REAL"); return v; }
    }
    if(c.type==Type::Text && std::holds_alternative<std::string>(v)) return v;
    throw std::runtime_error("Type mismatch for "+c.name+" (expected "+typeName(c.type)+")");
}
void validateTable(const Table& t) {
    std::set<std::string> names; int primaries=0;
    if(t.columns.empty()) throw std::runtime_error("A table needs at least one column");
    for(const auto& c:t.columns) {
        if(!names.insert(c.name).second) throw std::runtime_error("Duplicate column: "+c.name);
        primaries+=c.primary;
    }
    if(primaries>1) throw std::runtime_error("Only one single-column PRIMARY KEY is supported");
    for(const auto& r:t.rows) {
        if(r.size()!=t.columns.size()) throw std::runtime_error("Invalid row width");
        for(size_t i=0;i<r.size();++i) {
            const auto normalized=coerce(r[i],t.columns[i]);
            if(normalized.index()!=r[i].index()) throw std::runtime_error("Invalid stored value type");
        }
    }
    for(size_t i=0;i<t.columns.size();++i) if(t.columns[i].primary||t.columns[i].unique) {
        std::set<Value,ValueLess> seen;
        for(const auto& r:t.rows) if(r[i].index()!=0 && !seen.insert(r[i]).second)
            throw std::runtime_error("UNIQUE constraint: "+t.columns[i].name);
    }
    names.clear();
    for(const auto& ix:t.indexes) {
        columnOffset(t,ix.column);
        if(!names.insert(ix.name).second) throw std::runtime_error("Duplicate index: "+ix.name);
    }
}
void rebuildIndexes(Table& t) {
    for(auto& ix:t.indexes) {
        ix.entries.clear(); const auto c=columnOffset(t,ix.column);
        for(size_t i=0;i<t.rows.size();++i) if(t.rows[i][c].index()!=0) ix.entries.insert(t.rows[i][c],i);
    }
}
}
