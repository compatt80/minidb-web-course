#include "sql.hpp"
#include <cctype>
#include <cmath>
#include <limits>

namespace minidb {
static std::string lower(std::string s) { for(char& c:s) c=static_cast<char>(std::tolower(static_cast<unsigned char>(c))); return s; }
std::vector<Token> tokenize(const std::string& sql) {
    std::vector<Token> out; size_t i=0;
    while(i<sql.size()) {
        auto ch=static_cast<unsigned char>(sql[i]);
        if(std::isspace(ch)) { ++i; continue; }
        if(sql[i]=='-' && i+1<sql.size() && sql[i+1]=='-') { while(i<sql.size()&&sql[i]!='\n') ++i; continue; }
        const size_t start=i;
        if(std::isalpha(ch)||ch=='_') {
            ++i; while(i<sql.size()&&(std::isalnum(static_cast<unsigned char>(sql[i]))||sql[i]=='_')) ++i;
            out.push_back({Token::Word,lower(sql.substr(start,i-start)),start});
        } else if(std::isdigit(ch)||(ch=='.'&&i+1<sql.size()&&std::isdigit(static_cast<unsigned char>(sql[i+1])))) {
            bool dot=false;
            while(i<sql.size() && (std::isdigit(static_cast<unsigned char>(sql[i]))||(!dot&&sql[i]=='.'))) { if(sql[i]=='.') dot=true; ++i; }
            out.push_back({Token::Number,sql.substr(start,i-start),start});
        } else if(ch=='\'') {
            ++i; std::string value; bool closed=false;
            while(i<sql.size()) {
                if(sql[i]=='\'') { ++i; if(i<sql.size()&&sql[i]=='\'') { value+='\''; ++i; } else { closed=true; break; } }
                else value+=sql[i++];
            }
            if(!closed) throw std::runtime_error("Unterminated string at byte "+std::to_string(start));
            out.push_back({Token::String,value,start});
        } else {
            std::string sym(1,sql[i++]);
            if(i<sql.size() && (sym=="<"||sym==">"||sym=="!") && (sql[i]=='='||(sym=="<"&&sql[i]=='>'))) sym+=sql[i++];
            if(std::string("(),;*=<>!+-.").find(static_cast<char>(ch))==std::string::npos) throw std::runtime_error("Unexpected character at byte "+std::to_string(start));
            out.push_back({Token::Symbol,sym,start});
        }
    }
    out.push_back({Token::End,"",sql.size()}); return out;
}
const Token& Parser::peek() const { return tokens[cursor]; }
bool Parser::accept(const std::string& s) {
    if((peek().kind==Token::Word||peek().kind==Token::Symbol)&&peek().text==s) { ++cursor; return true; } return false;
}
void Parser::expect(const std::string& s) { if(!accept(s)) throw std::runtime_error("Expected '"+s+"' at byte "+std::to_string(peek().pos)); }
std::string Parser::identifier() {
    if(peek().kind!=Token::Word) throw std::runtime_error("Expected identifier at byte "+std::to_string(peek().pos));
    return tokens[cursor++].text;
}
std::string Parser::qualifiedIdentifier() {
    auto name=identifier();
    if(accept(".")) name+="."+identifier();
    return name;
}
Value Parser::literal() {
    if(accept("null")) return std::monostate{};
    if(peek().kind==Token::String) return tokens[cursor++].text;
    bool neg=accept("-"); if(!neg) accept("+");
    if(peek().kind!=Token::Number) throw std::runtime_error("Expected literal at byte "+std::to_string(peek().pos));
    std::string text=(neg?"-":"")+tokens[cursor++].text;
    try {
        if(text.find('.')!=std::string::npos) { double d=std::stod(text); if(!std::isfinite(d)) throw std::runtime_error("REAL overflow"); return d; }
        return static_cast<int64_t>(std::stoll(text));
    } catch(const std::exception&) { throw std::runtime_error("Numeric literal out of range: "+text); }
}
Column Parser::column() {
    Column c; c.name=identifier(); const auto type=identifier();
    if(type=="int"||type=="integer") c.type=Type::Int;
    else if(type=="real"||type=="double"||type=="float") c.type=Type::Real;
    else if(type=="text") c.type=Type::Text;
    else throw std::runtime_error("Unsupported type: "+type);
    while(true) {
        if(accept("primary")) { expect("key"); c.primary=true; c.notNull=true; c.unique=true; }
        else if(accept("not")) { expect("null"); c.notNull=true; }
        else if(accept("unique")) c.unique=true;
        else break;
    }
    return c;
}
std::shared_ptr<Expr> Parser::atom() {
    if(accept("(")) { auto e=expression(); expect(")"); return e; }
    auto e=std::make_shared<Expr>(); e->column=qualifiedIdentifier();
    if(accept("is")) { e->kind=Expr::IsNull; e->negate=accept("not"); expect("null"); }
    else {
        const auto op=peek().text;
        if(peek().kind!=Token::Symbol||(op!="="&&op!="!="&&op!="<>"&&op!="<"&&op!="<="&&op!=">"&&op!=">=")) throw std::runtime_error("Expected comparison operator");
        ++cursor; e->op=op; e->literal=literal();
    }
    return e;
}
std::shared_ptr<Expr> Parser::conjunction() {
    auto e=atom(); while(accept("and")) { auto n=std::make_shared<Expr>(); n->kind=Expr::And; n->left=e; n->right=atom(); e=n; } return e;
}
std::shared_ptr<Expr> Parser::expression() {
    auto e=conjunction(); while(accept("or")) { auto n=std::make_shared<Expr>(); n->kind=Expr::Or; n->left=e; n->right=conjunction(); e=n; } return e;
}
void Parser::query(Statement& s) {
    if(accept("*")) s.names={"*"};
    else {
        do {
            if(accept("count")) { expect("("); expect("*"); expect(")"); s.count=true; s.projections.push_back({true,{}}); }
            else { auto name=qualifiedIdentifier(); s.names.push_back(name); s.projections.push_back({false,name}); }
        } while(accept(","));
    }
    expect("from"); s.table=identifier();
    const bool innerJoin=accept("inner");
    if(innerJoin) expect("join");
    if(innerJoin||accept("join")) { s.joinTable=identifier(); expect("on"); s.joinLeft=qualifiedIdentifier(); expect("="); s.joinRight=qualifiedIdentifier(); }
    if(accept("where")) s.where=expression();
    if(accept("group")) { expect("by"); s.groupBy=qualifiedIdentifier(); }
    if(accept("order")) { expect("by"); s.order=qualifiedIdentifier(); if(accept("desc")) s.descending=true; else accept("asc"); }
    if(accept("limit")) {
        auto v=literal(); auto n=std::get_if<int64_t>(&v);
        if(!n||*n<0) throw std::runtime_error("LIMIT requires a nonnegative integer");
        s.limit=static_cast<size_t>(*n);
    }
}
Statement Parser::parse() {
    Statement s;
    if(accept("create")) {
        if(accept("table")) { s.kind=Statement::CreateTable; s.table=identifier(); expect("("); do { s.columns.push_back(column()); } while(accept(",")); expect(")"); }
        else { expect("index"); s.kind=Statement::CreateIndex; s.name=identifier(); expect("on"); s.table=identifier(); expect("("); s.names.push_back(identifier()); expect(")"); }
    } else if(accept("drop")) {
        if(accept("table")) { s.kind=Statement::DropTable; s.table=identifier(); }
        else { expect("index"); s.kind=Statement::DropIndex; s.name=identifier(); }
    } else if(accept("alter")) {
        expect("table"); s.table=identifier();
        if(accept("add")) { accept("column"); s.kind=Statement::AddColumn; s.columns.push_back(column()); }
        else if(accept("drop")) { expect("column"); s.kind=Statement::DropColumn; s.name=identifier(); }
        else { expect("rename"); if(accept("column")) { s.kind=Statement::RenameColumn; s.oldName=identifier(); expect("to"); s.name=identifier(); } else { expect("to"); s.kind=Statement::RenameTable; s.name=identifier(); } }
    } else if(accept("insert")) {
        s.kind=Statement::Insert; expect("into"); s.table=identifier();
        if(accept("(")) { do { s.names.push_back(identifier()); } while(accept(",")); expect(")"); }
        expect("values"); do { expect("("); Row r; do { r.push_back(literal()); } while(accept(",")); expect(")"); s.values.push_back(r); } while(accept(","));
    } else if(accept("select")) { s.kind=Statement::Select; query(s); }
    else if(accept("explain")) { s.kind=Statement::Explain; expect("select"); query(s); }
    else if(accept("update")) {
        s.kind=Statement::Update; s.table=identifier(); expect("set");
        do { auto name=identifier(); expect("="); auto v=literal(); s.assignments.emplace_back(name,v); } while(accept(","));
        if(accept("where")) s.where=expression();
    } else if(accept("delete")) { s.kind=Statement::Delete; expect("from"); s.table=identifier(); if(accept("where")) s.where=expression(); }
    else if(accept("begin")) {
        s.kind=Statement::Begin; accept("transaction");
        if(accept("read")) { if(accept("only")) s.readOnly=true; else expect("write"); }
    }
    else if(accept("commit")) s.kind=Statement::Commit;
    else if(accept("rollback")) s.kind=Statement::Rollback;
    else if(accept("show")) { expect("tables"); s.kind=Statement::ShowTables; }
    else if(accept("describe")||accept("desc")) { s.kind=Statement::Describe; s.table=identifier(); }
    else if(accept("help")) s.kind=Statement::Help;
    else if(accept("quit")||accept("exit")) s.kind=Statement::Quit;
    else throw std::runtime_error("Unknown SQL command at byte "+std::to_string(peek().pos));
    accept(";"); if(peek().kind!=Token::End) throw std::runtime_error("Unexpected token '"+peek().text+"' at byte "+std::to_string(peek().pos));
    return s;
}
std::vector<std::string> splitStatements(std::string& buffer,bool flush) {
    bool quoted=false, comment=false; size_t start=0; std::vector<std::string> out;
    for(size_t i=0;i<buffer.size();++i) {
        char ch=buffer[i];
        if(comment) { if(ch=='\n') comment=false; continue; }
        if(quoted) { if(ch=='\'') { if(i+1<buffer.size()&&buffer[i+1]=='\'') ++i; else quoted=false; } continue; }
        if(ch=='\'') quoted=true;
        else if(ch=='-'&&i+1<buffer.size()&&buffer[i+1]=='-') { comment=true; ++i; }
        else if(ch==';') { out.push_back(buffer.substr(start,i-start+1)); start=i+1; }
    }
    buffer.erase(0,start);
    if(flush && !buffer.empty()) { out.push_back(buffer); buffer.clear(); }
    return out;
}
}
