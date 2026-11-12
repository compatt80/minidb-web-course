#pragma once

#include <cstdint>
#include <memory>
#include <string>
#include <utility>
#include <variant>
#include <vector>

namespace minidb {
using Value = std::variant<std::monostate, int64_t, double, std::string>;

struct ValueLess {
    bool operator()(const Value& a, const Value& b) const;
};

// A small in-memory B+ tree used by secondary indexes. Leaves store row offsets;
// internal nodes only store separator keys and child pointers.
class BPlusTree {
    struct Node {
        bool leaf;
        std::vector<Value> keys;
        std::vector<std::vector<size_t>> values;
        std::vector<std::shared_ptr<Node>> children;
        Node* next=nullptr;
        explicit Node(bool isLeaf):leaf(isLeaf) {}
    };
    static constexpr size_t maxKeys=15;
    std::shared_ptr<Node> root;
    static const Value& firstKey(const Node& node);
    static void rebuildSeparators(Node& node);
    static std::shared_ptr<Node> insertNode(const std::shared_ptr<Node>& node,
        const Value& key, size_t row, Value& separator);
    static void collect(const std::shared_ptr<Node>& node,
        std::vector<std::pair<Value,std::vector<size_t>>>& entries);
public:
    BPlusTree();
    BPlusTree(const BPlusTree& other);
    BPlusTree& operator=(const BPlusTree& other);
    void clear();
    void insert(const Value& key, size_t row);
    std::vector<size_t> find(const Value& key) const;
    size_t keyCount() const;
};
}
