#pragma once
#include "storage.hpp"

namespace minidb {
class Engine {
    Storage storage;
    Database db;
    std::optional<Database> transaction;
    bool readOnly=false,dirty=false;
    uint32_t lockTimeoutMs;
    void loadLocked(LockMode mode,uint32_t timeoutMs);
    Result executeImpl(const Statement& s);
public:
    explicit Engine(const std::string& file,uint32_t timeoutMs=5000);
    Result execute(const Statement& s);
    bool inTransaction() const { return transaction.has_value(); }
};
}
