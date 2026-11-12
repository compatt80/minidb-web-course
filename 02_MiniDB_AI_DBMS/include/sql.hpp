#pragma once
#include "model.hpp"

namespace minidb {
struct Token { enum Kind { Word, Number, String, Symbol, End } kind; std::string text; size_t pos; };
std::vector<Token> tokenize(const std::string& sql);
class Parser {
    std::vector<Token> tokens; size_t cursor=0;
    const Token& peek() const;
    bool accept(const std::string& s);
    void expect(const std::string& s);
    std::string identifier();
    std::string qualifiedIdentifier();
    Value literal();
    Column column();
    std::shared_ptr<Expr> expression();
    std::shared_ptr<Expr> conjunction();
    std::shared_ptr<Expr> atom();
    void query(Statement& s);
public:
    explicit Parser(const std::string& sql): tokens(tokenize(sql)) {}
    Statement parse();
};
// Splits only at semicolons outside SQL strings and -- comments.
std::vector<std::string> splitStatements(std::string& buffer, bool flush=false);
}
