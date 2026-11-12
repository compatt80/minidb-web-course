#include "page_buffer.hpp"
#include <algorithm>
#include <stdexcept>

namespace minidb {
PageBufferPool::PageBufferPool(size_t pageBytes,size_t maxPages):pageSize(pageBytes),capacity(maxPages) {
    if(pageSize==0||capacity==0) throw std::invalid_argument("Page buffer dimensions must be positive");
}
std::vector<uint8_t>& PageBufferPool::fetch(size_t pageId,const Loader& loader) {
    ++clock; auto it=frames.find(pageId);
    if(it!=frames.end()) { it->second.used=clock; return it->second.bytes; }
    if(full()) throw std::runtime_error("Page buffer is full; flush dirty pages before fetching");
    auto bytes=loader(pageId);
    if(bytes.size()!=pageSize) throw std::runtime_error("Page loader returned an invalid page size");
    auto result=frames.emplace(pageId,Frame{pageId,std::move(bytes),false,clock});
    return result.first->second.bytes;
}
void PageBufferPool::markDirty(size_t pageId) {
    auto it=frames.find(pageId); if(it==frames.end()) throw std::runtime_error("Cannot dirty a page not in the buffer");
    it->second.dirty=true;
}
void PageBufferPool::flush(const Flusher& flusher) {
    for(auto& pair:frames) if(pair.second.dirty) { flusher(pair.second.id,pair.second.bytes); pair.second.dirty=false; }
}
void PageBufferPool::evictOne() {
    if(frames.empty()) return;
    auto victim=std::min_element(frames.begin(),frames.end(),[](const auto& a,const auto& b){ return a.second.used<b.second.used; });
    if(victim->second.dirty) throw std::runtime_error("Cannot evict a dirty page");
    frames.erase(victim);
}
}
