#include "engine.hpp"
#include <algorithm>
#include <cmath>
#include <numeric>
#include <set>
#include <chrono>

namespace minidb {
namespace {
// TRUE=1, FALSE=0, UNKNOWN=-1. WHERE keeps only TRUE.
int truthAnd(int a,int b) { if(a==0||b==0) return 0; return a==1&&b==1?1:-1; }
int truthOr(int a,int b) { if(a==1||b==1) return 1; return a==0&&b==0?0:-1; }
struct BoundExpr {
    Expr::Kind kind=Expr::Compare; size_t col=0; std::string op; Value v; bool negate=false;
    std::shared_ptr<BoundExpr> left,right;
};
std::shared_ptr<BoundExpr> bind(const Table& t,const std::shared_ptr<Expr>& e) {
    if(!e) return nullptr;
    auto b=std::make_shared<BoundExpr>(); b->kind=e->kind;
    if(e->kind==Expr::And||e->kind==Expr::Or) { b->left=bind(t,e->left); b->right=bind(t,e->right); }
    else { b->col=columnOffset(t,e->column); b->negate=e->negate; b->op=e->op;
        if(e->kind==Expr::Compare) {
            auto c=t.columns[b->col]; c.notNull=false; c.primary=false;
            b->v=coerce(e->literal,c);
        }
    }
    return b;
}
int evaluate(const Row& row,const std::shared_ptr<BoundExpr>& e) {
    if(!e) return 1;
    if(e->kind==Expr::And) return truthAnd(evaluate(row,e->left),evaluate(row,e->right));
    if(e->kind==Expr::Or) return truthOr(evaluate(row,e->left),evaluate(row,e->right));
    const auto& v=row[e->col];
    if(e->kind==Expr::IsNull) return ((v.index()==0)!=e->negate)?1:0;
    if(v.index()==0||e->v.index()==0) return -1;
    ValueLess less; bool lt=less(v,e->v),gt=less(e->v,v),eq=!lt&&!gt;
    const auto& op=e->op;
    if(op=="=") return eq;
    if(op=="!="||op=="<>") return !eq;
    if(op=="<") return lt;
    if(op=="<=") return !gt;
    if(op==">") return gt;
    return !lt;
}
struct Plan { std::string description="TableScan"; std::vector<size_t> candidates; bool indexed=false; };
// Safe extraction: an equality under AND is necessary for the whole predicate.
// OR falls back to a scan because either branch could satisfy the predicate.
void chooseIndex(const Table& t,const std::shared_ptr<BoundExpr>& e,Plan& p) {
    if(!e) return;
    if(e->kind==Expr::And) { chooseIndex(t,e->left,p); chooseIndex(t,e->right,p); return; }
    if(e->kind!=Expr::Compare||e->op!="="||e->v.index()==0) return;
    for(const auto& ix:t.indexes) if(ix.column==t.columns[e->col].name) {
        auto rows=ix.entries.find(e->v);
        if(!p.indexed||rows.size()<p.candidates.size()) { p.indexed=true; p.candidates=std::move(rows); p.description="IndexLookup("+ix.name+")"; }
    }
}
Plan plan(const Table& t,const std::shared_ptr<BoundExpr>& e) {
    Plan p; chooseIndex(t,e,p);
    if(!p.indexed) { p.candidates.resize(t.rows.size()); std::iota(p.candidates.begin(),p.candidates.end(),0); } return p;
}
bool mutation(Statement::Kind k) {
    return k==Statement::CreateTable||k==Statement::DropTable||k==Statement::AddColumn||k==Statement::DropColumn||
        k==Statement::RenameColumn||k==Statement::RenameTable||k==Statement::Insert||k==Statement::Update||
        k==Statement::Delete||k==Statement::CreateIndex||k==Statement::DropIndex;
}
Result message(const std::string& s) { Result r; r.message=s; return r; }
struct JoinBinding { bool fromLeft=false; size_t offset=0; };
bool integerEqualsReal(int64_t integer,double real) {
    // 2^63 is exactly representable as double. Check the half-open int64 range
    // before converting, and never round the integer to double (which loses
    // adjacent values above 2^53, including INT64_MAX).
    constexpr double int64Boundary=9223372036854775808.0;
    if(!std::isfinite(real)||real<-int64Boundary||real>=int64Boundary||std::trunc(real)!=real) return false;
    return integer==static_cast<int64_t>(real);
}
bool joinValuesEqual(const Value& first,const Value& second) {
    if(first.index()==second.index()) return first==second;
    if(auto integer=std::get_if<int64_t>(&first)) {
        if(auto real=std::get_if<double>(&second)) return integerEqualsReal(*integer,*real);
    }
    if(auto real=std::get_if<double>(&first)) {
        if(auto integer=std::get_if<int64_t>(&second)) return integerEqualsReal(*integer,*real);
    }
    return false;
}
JoinBinding resolveJoinColumn(const Table& left,const Table& right,const std::string& reference) {
    const auto dot=reference.find('.');
    const auto qualifier=dot==std::string::npos?std::string():reference.substr(0,dot);
    const auto column=dot==std::string::npos?reference:reference.substr(dot+1);
    if(column.empty()||column.find('.')!=std::string::npos)
        throw std::runtime_error("Invalid JOIN column reference: "+reference);
    auto findColumn=[](const Table& table,const std::string& name) -> std::optional<size_t> {
        for(size_t i=0;i<table.columns.size();++i) if(table.columns[i].name==name) return i;
        return std::nullopt;
    };
    if(!qualifier.empty()) {
        if(qualifier==left.name) {
            const auto offset=findColumn(left,column);
            if(!offset) throw std::runtime_error("Unknown column in JOIN condition: "+reference);
            return {true,*offset};
        }
        if(qualifier==right.name) {
            const auto offset=findColumn(right,column);
            if(!offset) throw std::runtime_error("Unknown column in JOIN condition: "+reference);
            return {false,*offset};
        }
        throw std::runtime_error("Unknown table in JOIN condition: "+qualifier);
    }
    const auto leftOffset=findColumn(left,column), rightOffset=findColumn(right,column);
    if(leftOffset&&rightOffset) throw std::runtime_error("Ambiguous column in JOIN condition: "+reference);
    if(leftOffset) return {true,*leftOffset};
    if(rightOffset) return {false,*rightOffset};
    throw std::runtime_error("Unknown column in JOIN condition: "+reference);
}
Table innerJoin(const Table& left,const Table& right,const Statement& s) {
    Table joined; joined.name=left.name+"_join_"+right.name;
    for(const auto& column:left.columns) { auto copy=column; copy.name=left.name+"."+column.name; joined.columns.push_back(std::move(copy)); }
    for(const auto& column:right.columns) { auto copy=column; copy.name=right.name+"."+column.name; joined.columns.push_back(std::move(copy)); }
    const auto first=resolveJoinColumn(left,right,s.joinLeft), second=resolveJoinColumn(left,right,s.joinRight);
    if(first.fromLeft==second.fromLeft) throw std::runtime_error("JOIN condition must reference both tables");
    const auto firstType=(first.fromLeft?left:right).columns[first.offset].type;
    const auto secondType=(second.fromLeft?left:right).columns[second.offset].type;
    const auto numeric=[](Type type) { return type==Type::Int||type==Type::Real; };
    if(firstType!=secondType&&!(numeric(firstType)&&numeric(secondType)))
        throw std::runtime_error("Incompatible JOIN column types: "+typeName(firstType)+" and "+typeName(secondType));
    for(const auto& leftRow:left.rows) for(const auto& rightRow:right.rows) {
        const auto& firstValue=first.fromLeft?leftRow[first.offset]:rightRow[first.offset];
        const auto& secondValue=second.fromLeft?leftRow[second.offset]:rightRow[second.offset];
        if(firstValue.index()==0||secondValue.index()==0) continue;
        if(!joinValuesEqual(firstValue,secondValue)) continue;
        Row row=leftRow; row.insert(row.end(),rightRow.begin(),rightRow.end()); joined.rows.push_back(std::move(row));
    }
    return joined;
}
}
namespace {
struct UnlockOnExit {
    Storage& storage; bool active;
    ~UnlockOnExit() { if(active) storage.release(); }
};
}
Engine::Engine(const std::string& file,uint32_t timeoutMs):storage(file),lockTimeoutMs(timeoutMs) {
    // Validate an available file immediately; a busy database can still open
    // an idle session. Its first data statement waits and reloads under lock.
    try { loadLocked(LockMode::Shared,0); storage.release(); }
    catch(const LockTimeout&) {}
}
void Engine::loadLocked(LockMode mode,uint32_t timeoutMs) {
    const auto deadline=std::chrono::steady_clock::now()+std::chrono::milliseconds(timeoutMs);
    auto remaining=[&]() {
        const auto ms=std::chrono::duration_cast<std::chrono::milliseconds>(deadline-std::chrono::steady_clock::now()).count();
        return static_cast<uint32_t>(std::max<int64_t>(0,ms));
    };
    while(true) {
        storage.acquire(mode,remaining());
        try { db=storage.load(); return; }
        catch(const RecoveryRequired&) {
            storage.release();
            storage.acquire(LockMode::Exclusive,remaining());
            try { db=storage.load(); }
            catch(...) { storage.release(); throw; }
            storage.release();
            // Reload under S after recovery; another writer could have committed.
        }
        catch(...) { storage.release(); throw; }
    }
}
Result Engine::execute(const Statement& s) {
    if(s.kind==Statement::Begin) {
        if(transaction) throw std::runtime_error("Nested transactions are not supported");
        loadLocked(s.readOnly?LockMode::Shared:LockMode::Exclusive,lockTimeoutMs);
        UnlockOnExit guard{storage,true};
        transaction=db; readOnly=s.readOnly; dirty=false; guard.active=false;
        return message(readOnly?"Read-only transaction started (shared lock)":"Transaction started (exclusive lock)");
    }
    if(s.kind==Statement::Commit||s.kind==Statement::Rollback) {
        if(!transaction) throw std::runtime_error("No active transaction");
        if(s.kind==Statement::Commit) { if(dirty) storage.save(db); }
        else db=std::move(*transaction);
        transaction.reset(); dirty=readOnly=false; storage.release();
        return message(s.kind==Statement::Commit?"Transaction committed":"Transaction rolled back");
    }
    if(s.kind==Statement::Help||s.kind==Statement::Quit) return executeImpl(s);
    if(transaction&&readOnly&&mutation(s.kind)) throw std::runtime_error("Cannot write in a read-only transaction; ROLLBACK or COMMIT first");
    const bool automatic=!transaction;
    if(automatic) loadLocked(mutation(s.kind)?LockMode::Exclusive:LockMode::Shared,lockTimeoutMs);
    UnlockOnExit guard{storage,automatic};
    if(!mutation(s.kind)) return executeImpl(s);
    // Each statement is atomic, even inside an explicit multi-statement transaction.
    auto before=db;
    try {
        auto result=executeImpl(s);
        for(auto& pair:db) { validateTable(pair.second); rebuildIndexes(pair.second); }
        if(automatic) storage.save(db); else dirty=true;
        return result;
    } catch(...) { db=std::move(before); throw; }
}
Result Engine::executeImpl(const Statement& s) {
    switch(s.kind) {
    case Statement::ShowTables:{
        Result r; r.headers={"table","rows","columns","indexes"};
        for(const auto& pair:db) { const auto& t=pair.second; r.rows.push_back({t.name,std::to_string(t.rows.size()),std::to_string(t.columns.size()),std::to_string(t.indexes.size())}); }
        r.message=std::to_string(r.rows.size())+" table(s)"; return r;
    }
    case Statement::Help:return message(
        "CREATE/DROP/ALTER TABLE; INSERT INTO ... VALUES; SELECT ... FROM ...\n"
        "UPDATE ... SET ...; DELETE FROM ...; CREATE/DROP INDEX; EXPLAIN SELECT ...\n"
        "WHERE supports comparisons, AND, OR, parentheses, IS [NOT] NULL\n"
        "SELECT supports *, column list, COUNT(*), INNER JOIN, GROUP BY, ORDER BY, LIMIT\n"
        "BEGIN [READ ONLY|READ WRITE]; COMMIT; ROLLBACK; SHOW TABLES; DESCRIBE table; QUIT;\n"
        "Read-only transactions share the database; writers hold an exclusive lock until transaction end.\n"
        "Types: INT, REAL, TEXT. End each interactive statement with a semicolon.");
    case Statement::Quit:return message("Bye");
    case Statement::CreateTable:{
        if(db.count(s.table)) throw std::runtime_error("Table already exists: "+s.table);
        Table t; t.name=s.table; t.columns=s.columns; validateTable(t); db.emplace(s.table,std::move(t)); return message("Table created: "+s.table);
    }
    case Statement::DropIndex:
        for(auto& pair:db) {
            auto& ix=pair.second.indexes;
            auto it=std::find_if(ix.begin(),ix.end(),[&](const Index& v){return v.name==s.name;});
            if(it!=ix.end()) { ix.erase(it); return message("Index dropped: "+s.name); }
        }
        throw std::runtime_error("Unknown index: "+s.name);
    default:break;
    }
    auto ti=db.find(s.table); if(ti==db.end()) throw std::runtime_error("Unknown table: "+s.table);
    auto& t=ti->second;
    switch(s.kind) {
    case Statement::DropTable:db.erase(ti); return message("Table dropped: "+s.table);
    case Statement::Describe:{
        Result r; r.headers={"column","type","not_null","primary_key","unique"};
        for(const auto& c:t.columns) r.rows.push_back({c.name,typeName(c.type),c.notNull?"YES":"NO",c.primary?"YES":"NO",(c.unique||c.primary)?"YES":"NO"});
        for(const auto& ix:t.indexes) r.message+="INDEX "+ix.name+" ON "+ix.column+"\n";
        if(r.message.empty()) r.message="No explicit indexes";
        return r;
    }
    case Statement::AddColumn:
        for(const auto& c:t.columns) if(c.name==s.columns[0].name) throw std::runtime_error("Column already exists");
        t.columns.push_back(s.columns[0]); for(auto& row:t.rows) row.push_back(std::monostate{});
        return message("Column added: "+s.columns[0].name);
    case Statement::DropColumn:{
        const auto c=columnOffset(t,s.name);
        if(t.columns.size()==1) throw std::runtime_error("Cannot drop the last column");
        for(const auto& ix:t.indexes) if(ix.column==s.name) throw std::runtime_error("Drop dependent index first: "+ix.name);
        t.columns.erase(t.columns.begin()+static_cast<std::ptrdiff_t>(c));
        for(auto& row:t.rows) row.erase(row.begin()+static_cast<std::ptrdiff_t>(c));
        return message("Column dropped: "+s.name);
    }
    case Statement::RenameColumn:{
        auto c=columnOffset(t,s.oldName);
        for(const auto& col:t.columns) if(col.name==s.name) throw std::runtime_error("Column already exists");
        t.columns[c].name=s.name; for(auto& ix:t.indexes) if(ix.column==s.oldName) ix.column=s.name;
        return message("Column renamed: "+s.name);
    }
    case Statement::RenameTable:{
        if(db.count(s.name)) throw std::runtime_error("Table already exists: "+s.name);
        auto copy=std::move(t); copy.name=s.name; db.erase(ti); db.emplace(s.name,std::move(copy)); return message("Table renamed: "+s.name);
    }
    case Statement::CreateIndex:{
        columnOffset(t,s.names[0]);
        for(const auto& pair:db) for(const auto& ix:pair.second.indexes) if(ix.name==s.name) throw std::runtime_error("Index already exists: "+s.name);
        Index ix; ix.name=s.name; ix.column=s.names[0]; t.indexes.push_back(std::move(ix)); return message("Index created: "+s.name);
    }
    case Statement::Insert:{
        std::vector<size_t> offsets;
        if(s.names.empty()) { for(size_t i=0;i<t.columns.size();++i) offsets.push_back(i); }
        else { std::set<std::string> names; for(const auto& n:s.names) { if(!names.insert(n).second) throw std::runtime_error("Duplicate insert column: "+n); offsets.push_back(columnOffset(t,n)); } }
        for(const auto& values:s.values) {
            if(values.size()!=offsets.size()) throw std::runtime_error("INSERT value count does not match column count");
            Row row(t.columns.size(),std::monostate{});
            for(size_t i=0;i<offsets.size();++i) row[offsets[i]]=coerce(values[i],t.columns[offsets[i]]);
            t.rows.push_back(std::move(row));
        }
        return message(std::to_string(s.values.size())+" row(s) inserted");
    }
    case Statement::Update:{
        auto e=bind(t,s.where); std::vector<std::pair<size_t,Value>> assignments; std::set<std::string> names;
        for(const auto& a:s.assignments) {
            if(!names.insert(a.first).second) throw std::runtime_error("Duplicate assignment: "+a.first);
            auto c=columnOffset(t,a.first); assignments.emplace_back(c,coerce(a.second,t.columns[c]));
        }
        size_t changed=0;
        for(auto& row:t.rows) if(evaluate(row,e)==1) { for(const auto& a:assignments) row[a.first]=a.second; ++changed; }
        return message(std::to_string(changed)+" row(s) updated");
    }
    case Statement::Delete:{
        auto e=bind(t,s.where); const auto before=t.rows.size();
        t.rows.erase(std::remove_if(t.rows.begin(),t.rows.end(),[&](const Row& row){return evaluate(row,e)==1;}),t.rows.end());
        return message(std::to_string(before-t.rows.size())+" row(s) deleted");
    }
    case Statement::Select:case Statement::Explain:{
        Table joined;
        Table* queryTable=&t;
        if(!s.joinTable.empty()) {
            auto other=db.find(s.joinTable); if(other==db.end()) throw std::runtime_error("Unknown table: "+s.joinTable);
            joined=innerJoin(t,other->second,s); queryTable=&joined;
        }
        auto& source=*queryTable;
        auto e=bind(source,s.where); std::vector<size_t> cols;
        Result r;
        const auto countItems=static_cast<size_t>(std::count_if(s.projections.begin(),s.projections.end(),[](const SelectItem& item){ return item.count; }));
        if(s.count&&countItems==0) throw std::runtime_error("Invalid aggregate projection");
        std::optional<size_t> group; if(!s.groupBy.empty()) group=columnOffset(source,s.groupBy);
        if(group&&!s.count) throw std::runtime_error("GROUP BY currently requires COUNT(*)");
        if(s.count&&group) {
            if(s.names.size()!=1||columnOffset(source,s.names[0])!=*group||countItems==0)
                throw std::runtime_error("GROUP BY query must select its group column and COUNT(*)");
            if(!s.order.empty()&&columnOffset(source,s.order)!=*group)
                throw std::runtime_error("GROUP BY ORDER BY must use the group column");
            for(const auto& item:s.projections) r.headers.push_back(item.count?"count(*)":source.columns[*group].name);
        } else if(s.count) {
            if(countItems!=s.projections.size()||!s.order.empty())
                throw std::runtime_error("COUNT(*) without GROUP BY cannot select columns or ORDER BY");
            for(size_t i=0;i<countItems;++i) r.headers.push_back("count(*)");
        }
        else if(s.names==std::vector<std::string>{"*"}) { for(size_t i=0;i<source.columns.size();++i) { cols.push_back(i); r.headers.push_back(source.columns[i].name); } }
        else for(const auto& name:s.names) { cols.push_back(columnOffset(source,name)); r.headers.push_back(name); }
        std::optional<size_t> order; if(!s.order.empty()) order=columnOffset(source,s.order);
        auto p=plan(source,e); if(!s.joinTable.empty()) { p.description="NestedLoopJoin"; p.indexed=false; }
        if(s.kind==Statement::Explain) {
            r.headers={"operator","candidate_rows","table_rows"}; r.rows={{p.description,std::to_string(p.candidates.size()),std::to_string(source.rows.size())}};
            r.message="Filter -> "+std::string(s.count?"Count":"Project")+(group?" -> Group":"")+(order?" -> Sort":"")+(s.limit?" -> Limit":""); return r;
        }
        std::vector<size_t> hits;
        for(auto i:p.candidates) if(evaluate(source.rows[i],e)==1) hits.push_back(i);
        if(s.count&&group) {
            std::map<Value,size_t,ValueLess> counts;
            for(auto i:hits) ++counts[source.rows[i][*group]];
            std::vector<std::pair<Value,size_t>> groups(counts.begin(),counts.end());
            if(order) std::stable_sort(groups.begin(),groups.end(),[&](const auto& a,const auto& b){ ValueLess less; return s.descending?less(b.first,a.first):less(a.first,b.first); });
            if(s.limit&&groups.size()>*s.limit) groups.resize(*s.limit);
            for(const auto& item:groups) {
                std::vector<std::string> row;
                for(const auto& projection:s.projections) row.push_back(projection.count?std::to_string(item.second):display(item.first));
                r.rows.push_back(std::move(row));
            }
        } else if(s.count) {
            if(!s.limit||*s.limit>0) {
                std::vector<std::string> row(countItems,std::to_string(hits.size()));
                r.rows.push_back(std::move(row));
            }
        }
        else {
            if(order) std::stable_sort(hits.begin(),hits.end(),[&](size_t a,size_t b){ ValueLess less; return s.descending?less(source.rows[b][*order],source.rows[a][*order]):less(source.rows[a][*order],source.rows[b][*order]); });
            if(s.limit&&hits.size()>*s.limit) hits.resize(*s.limit);
            for(auto i:hits) { std::vector<std::string> row; for(auto c:cols) row.push_back(display(source.rows[i][c])); r.rows.push_back(std::move(row)); }
        }
        r.message=std::to_string(r.rows.size())+" row(s), scanned "+std::to_string(p.candidates.size())+", plan "+p.description; return r;
    }
    default:throw std::runtime_error("Unsupported statement");
    }
}
}
