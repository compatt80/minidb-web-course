#pragma once

#include <cstddef>
#include <cstdint>
#include <functional>
#include <unordered_map>
#include <vector>

namespace minidb {
class PageBufferPool {
public:
    using Loader=std::function<std::vector<uint8_t>(size_t)>;
    using Flusher=std::function<void(size_t,const std::vector<uint8_t>&)>;
private:
    struct Frame { size_t id; std::vector<uint8_t> bytes; bool dirty=false; uint64_t used=0; };
    size_t pageSize,capacity;
    uint64_t clock=0;
    std::unordered_map<size_t,Frame> frames;
public:
    PageBufferPool(size_t pageBytes,size_t maxPages);
    std::vector<uint8_t>& fetch(size_t pageId,const Loader& loader);
    void markDirty(size_t pageId);
    void flush(const Flusher& flusher);
    void evictOne();
    bool full() const { return frames.size()>=capacity; }
    size_t size() const { return frames.size(); }
    size_t bytesPerPage() const { return pageSize; }
};
}
