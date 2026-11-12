#pragma once
#include <cstdint>
#include <map>
#include <memory>
#include <optional>
#include <stdexcept>
#include <string>
#include <vector>
#include "bptree.hpp"

namespace minidb {
enum class Type { Int, Real, Text };
struct Column { std::string name; Type type; bool notNull=false, primary=false, unique=false; };
using Row = std::vector<Value>;
struct Index {
    std::string name, column;
    BPlusTree entries;
};
struct Table { std::string name; std::vector<Column> columns; std::vector<Row> rows; std::vector<Index> indexes; };
using Database = std::map<std::string, Table>;
struct Expr {
    enum Kind { Compare, IsNull, And, Or } kind=Compare;
    std::string column, op;
    Value literal;
    bool negate=false;
    std::shared_ptr<Expr> left, right;
};
struct SelectItem { bool count=false; std::string name; };
struct Statement {
    enum Kind { CreateTable, DropTable, AddColumn, DropColumn, RenameColumn, RenameTable,
        Insert, Select, Update, Delete, CreateIndex, DropIndex, Begin, Commit, Rollback,
        ShowTables, Describe, Explain, Help, Quit } kind=Help;
    std::string table, name, oldName;
    std::vector<Column> columns;
    std::vector<std::string> names;
    std::vector<SelectItem> projections;
    std::vector<Row> values;
    std::vector<std::pair<std::string,Value>> assignments;
    std::shared_ptr<Expr> where;
    std::string joinTable, joinLeft, joinRight, groupBy;
    std::string order;
    bool descending=false, count=false;
    bool readOnly=false;
    std::optional<size_t> limit;
};
struct Result { std::vector<std::string> headers; std::vector<std::vector<std::string>> rows; std::string message; };
std::string display(const Value& v);
std::string typeName(Type t);
size_t columnOffset(const Table& t, const std::string& name);
Value coerce(const Value& v, const Column& c);
void validateTable(const Table& t);
void rebuildIndexes(Table& t);
}
