#include "bptree.hpp"
#include <algorithm>

namespace minidb {
bool ValueLess::operator()(const Value& a,const Value& b) const {
    if(a.index()!=b.index()) return a.index()<b.index();
    return a<b;
}

BPlusTree::BPlusTree():root(std::make_shared<Node>(true)) {}
BPlusTree::BPlusTree(const BPlusTree& other):BPlusTree() {
    std::vector<std::pair<Value,std::vector<size_t>>> entries;
    collect(other.root,entries);
    for(const auto& entry:entries) for(auto row:entry.second) insert(entry.first,row);
}
BPlusTree& BPlusTree::operator=(const BPlusTree& other) {
    if(this==&other) return *this;
    BPlusTree copy(other); root.swap(copy.root); return *this;
}
void BPlusTree::clear() { root=std::make_shared<Node>(true); }
const Value& BPlusTree::firstKey(const Node& node) {
    if(node.leaf) return node.keys.front();
    return firstKey(*node.children.front());
}
void BPlusTree::rebuildSeparators(Node& node) {
    node.keys.clear();
    for(size_t i=1;i<node.children.size();++i) node.keys.push_back(firstKey(*node.children[i]));
}
std::shared_ptr<BPlusTree::Node> BPlusTree::insertNode(const std::shared_ptr<Node>& node,
    const Value& key, size_t row, Value& separator) {
    ValueLess less;
    if(node->leaf) {
        auto it=std::lower_bound(node->keys.begin(),node->keys.end(),key,less);
        const auto pos=static_cast<size_t>(it-node->keys.begin());
        if(it!=node->keys.end()&&!less(key,*it)&&!less(*it,key)) node->values[pos].push_back(row);
        else { node->keys.insert(it,key); node->values.insert(node->values.begin()+static_cast<std::ptrdiff_t>(pos),{row}); }
        if(node->keys.size()<=maxKeys) return nullptr;
        const auto mid=node->keys.size()/2; auto right=std::make_shared<Node>(true);
        right->keys.assign(node->keys.begin()+static_cast<std::ptrdiff_t>(mid),node->keys.end());
        right->values.assign(node->values.begin()+static_cast<std::ptrdiff_t>(mid),node->values.end());
        node->keys.erase(node->keys.begin()+static_cast<std::ptrdiff_t>(mid),node->keys.end());
        node->values.erase(node->values.begin()+static_cast<std::ptrdiff_t>(mid),node->values.end());
        right->next=node->next; node->next=right.get(); separator=right->keys.front(); return right;
    }
    const auto child=static_cast<size_t>(std::upper_bound(node->keys.begin(),node->keys.end(),key,
        [&](const Value& value,const Value& element){ return less(value,element); })-node->keys.begin());
    auto right=insertNode(node->children[child],key,row,separator);
    if(!right) return nullptr;
    node->children.insert(node->children.begin()+static_cast<std::ptrdiff_t>(child+1),right);
    rebuildSeparators(*node);
    if(node->children.size()<=maxKeys+1) return nullptr;
    const auto mid=node->children.size()/2; auto split=std::make_shared<Node>(false);
    split->children.assign(node->children.begin()+static_cast<std::ptrdiff_t>(mid),node->children.end());
    node->children.erase(node->children.begin()+static_cast<std::ptrdiff_t>(mid),node->children.end());
    rebuildSeparators(*node); rebuildSeparators(*split); separator=firstKey(*split->children.front()); return split;
}
void BPlusTree::insert(const Value& key, size_t row) {
    Value separator; auto right=insertNode(root,key,row,separator);
    if(right) { auto newRoot=std::make_shared<Node>(false); newRoot->children={root,right}; rebuildSeparators(*newRoot); root=std::move(newRoot); }
}
std::vector<size_t> BPlusTree::find(const Value& key) const {
    ValueLess less; auto node=root;
    while(!node->leaf) {
        const auto child=static_cast<size_t>(std::upper_bound(node->keys.begin(),node->keys.end(),key,
            [&](const Value& value,const Value& element){ return less(value,element); })-node->keys.begin());
        node=node->children[child];
    }
    auto it=std::lower_bound(node->keys.begin(),node->keys.end(),key,less);
    if(it==node->keys.end()||less(key,*it)||less(*it,key)) return {};
    return node->values[static_cast<size_t>(it-node->keys.begin())];
}
size_t BPlusTree::keyCount() const {
    std::vector<std::pair<Value,std::vector<size_t>>> entries; collect(root,entries); return entries.size();
}
void BPlusTree::collect(const std::shared_ptr<Node>& node,
    std::vector<std::pair<Value,std::vector<size_t>>>& entries) {
    if(node->leaf) { for(size_t i=0;i<node->keys.size();++i) entries.emplace_back(node->keys[i],node->values[i]); return; }
    for(const auto& child:node->children) collect(child,entries);
}
}
