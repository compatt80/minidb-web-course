#pragma once
#include "model.hpp"
#include "lock_manager.hpp"

namespace minidb {
class Storage {
    std::string path;
    std::unique_ptr<DatabaseLock> locks;
public:
    explicit Storage(std::string file);
    ~Storage();
    Storage(const Storage&)=delete;
    Storage& operator=(const Storage&)=delete;
    void acquire(LockMode mode,uint32_t timeoutMs) { locks->acquire(mode,timeoutMs); }
    void release() noexcept { locks->release(); }
    LockMode lockMode() const { return locks->mode(); }
    Database load() const;
    void save(const Database& db) const;
};
}
