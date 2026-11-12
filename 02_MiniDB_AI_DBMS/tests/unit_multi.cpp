#include "bptree.hpp"
#include "page_buffer.hpp"
#include <cassert>
#include <iostream>
#include <map>
using namespace minidb;
int main() {
    BPlusTree students, courses, enrollments;
    for(int64_t i=0;i<1000;++i) {
        students.insert(i,i); courses.insert(999-i,999-i); enrollments.insert(i%4,i);
    }
    assert(students.find(int64_t(500)).front()==500);
    assert(courses.find(int64_t(500)).front()==500);
    assert(enrollments.find(int64_t(2)).size()==250);
    BPlusTree copy=students;students.clear();assert(copy.keyCount()==1000);
    std::cout<<"PASS 4.2 independent entity indexes splits duplicate keys copy\n";
    PageBufferPool pool(4096,8);std::map<size_t,int> loads;std::map<size_t,std::vector<uint8_t>> disk;
    auto loader=[&](size_t id){++loads[id];return std::vector<uint8_t>(4096,(uint8_t)id);};
    for(size_t i=0;i<8;++i){auto& bytes=pool.fetch(i,loader);bytes[0]=99;pool.markDirty(i);}
    assert(pool.size()==8);pool.fetch(0,loader);assert(loads[0]==1);
    bool blocked=false;try{pool.fetch(8,loader);}catch(const std::exception&){blocked=true;}assert(blocked);
    pool.flush([&](size_t id,const std::vector<uint8_t>& bytes){disk[id]=bytes;});
    assert(disk.size()==8);for(auto& entry:disk)assert(entry.second[0]==99);
    pool.evictOne();pool.fetch(8,loader);assert(pool.size()==8);
    std::cout<<"PASS 4.4 eight-page dirty flush hit LRU eviction refill\n";
}
