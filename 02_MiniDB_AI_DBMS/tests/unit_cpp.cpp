#include "bptree.hpp"
#include "page_buffer.hpp"
#include <algorithm>
#include <cassert>
#include <cstdint>
#include <map>
#include <numeric>
#include <random>
#include <vector>

using namespace minidb;

int main() {
    BPlusTree tree;
    for(int64_t i=0;i<500;++i) tree.insert(i,i*10);
    tree.insert(int64_t(42),9999);
    auto hits=tree.find(int64_t(42));
    assert(hits.size()==2&&hits[0]==420&&hits[1]==9999);
    assert(tree.find(int64_t(501)).empty());
    BPlusTree copy=tree;
    assert(copy.keyCount()==500&&copy.find(int64_t(499)).front()==4990);

    for(int mode=0;mode<3;++mode) {
        BPlusTree large;
        std::vector<int64_t> keys(1000);
        std::iota(keys.begin(),keys.end(),int64_t(0));
        if(mode==1) std::reverse(keys.begin(),keys.end());
        if(mode==2) { std::mt19937 rng(42); std::shuffle(keys.begin(),keys.end(),rng); }
        for(auto key:keys) large.insert(key,static_cast<size_t>(key));
        assert(large.keyCount()==1000);
        for(int64_t key=0;key<1000;++key) {
            const auto found=large.find(key);
            assert(found.size()==1&&found.front()==static_cast<size_t>(key));
        }
    }

    PageBufferPool pool(16,2);
    auto loader=[](size_t pageId) { return std::vector<uint8_t>(16,static_cast<uint8_t>(pageId)); };
    auto& first=pool.fetch(1,loader); first[0]=7; pool.markDirty(1);
    pool.fetch(2,loader);
    bool rejected=false;
    try { pool.fetch(3,loader); } catch(const std::exception&) { rejected=true; }
    assert(rejected);
    std::map<size_t,uint8_t> flushed;
    pool.flush([&](size_t id,const std::vector<uint8_t>& bytes){ flushed[id]=bytes[0]; });
    pool.evictOne();
    pool.fetch(3,loader);
    assert(flushed[1]==7);
    return 0;
}
