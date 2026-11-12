#pragma once
#include <cstdint>
#include <stdexcept>
#include <string>

namespace minidb {
enum class LockMode { None, Shared, Exclusive };
class LockTimeout : public std::runtime_error {
public:
    using std::runtime_error::runtime_error;
};
class RecoveryRequired : public std::runtime_error {
public:
    RecoveryRequired():std::runtime_error("WAL recovery requires an exclusive lock") {}
};

// One database resource, held until transaction end. No lock upgrades.
class DatabaseLock {
#ifdef _WIN32
    void* handle=nullptr;
#else
    int handle=-1;
#endif
    LockMode held=LockMode::None;
    bool tryAcquire(LockMode mode);
public:
    explicit DatabaseLock(const std::string& lockFile);
    ~DatabaseLock();
    DatabaseLock(const DatabaseLock&)=delete;
    DatabaseLock& operator=(const DatabaseLock&)=delete;
    void acquire(LockMode mode,uint32_t timeoutMs);
    void release() noexcept;
    LockMode mode() const { return held; }
};
}
