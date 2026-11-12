#include "lock_manager.hpp"
#include <chrono>
#include <cerrno>
#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#else
#include <fcntl.h>
#include <sys/file.h>
#include <unistd.h>
#endif

namespace minidb {
DatabaseLock::DatabaseLock(const std::string& lockFile) {
#ifdef _WIN32
    auto h=CreateFileA(lockFile.c_str(),GENERIC_READ|GENERIC_WRITE,
                      FILE_SHARE_READ|FILE_SHARE_WRITE,nullptr,OPEN_ALWAYS,FILE_ATTRIBUTE_NORMAL,nullptr);
    if(h==INVALID_HANDLE_VALUE) throw std::runtime_error("Cannot open database lock file");
    handle=h;
#else
    handle=::open(lockFile.c_str(),O_CREAT|O_RDWR|O_CLOEXEC,0600);
    if(handle<0) throw std::runtime_error("Cannot open database lock file");
#endif
}
DatabaseLock::~DatabaseLock() {
    release();
#ifdef _WIN32
    if(handle) CloseHandle(static_cast<HANDLE>(handle));
#else
    if(handle>=0) ::close(handle);
#endif
}
bool DatabaseLock::tryAcquire(LockMode requested) {
#ifdef _WIN32
    OVERLAPPED range{};
    const DWORD flags=LOCKFILE_FAIL_IMMEDIATELY|(requested==LockMode::Exclusive?LOCKFILE_EXCLUSIVE_LOCK:0);
    if(LockFileEx(static_cast<HANDLE>(handle),flags,0,1,0,&range)) return true;
    if(GetLastError()==ERROR_LOCK_VIOLATION) return false;
#else
    if(flock(handle,(requested==LockMode::Shared?LOCK_SH:LOCK_EX)|LOCK_NB)==0) return true;
    if(errno==EWOULDBLOCK||errno==EAGAIN||errno==EINTR) return false;
#endif
    throw std::runtime_error("Cannot acquire database transaction lock");
}
void DatabaseLock::acquire(LockMode requested,uint32_t timeoutMs) {
    if(requested==LockMode::None) throw std::logic_error("Invalid lock mode");
    if(held==requested) return;
    if(held!=LockMode::None) throw std::logic_error("Lock upgrades are not supported");
    const auto deadline=std::chrono::steady_clock::now()+std::chrono::milliseconds(timeoutMs);
    while(true) {
        if(tryAcquire(requested)) { held=requested; return; }
        if(std::chrono::steady_clock::now()>=deadline)
            throw LockTimeout("Database lock timeout after "+std::to_string(timeoutMs)+
                              " ms waiting for "+(requested==LockMode::Shared?"shared":"exclusive")+" lock");
#ifdef _WIN32
        Sleep(10);
#else
        usleep(10000);
#endif
    }
}
void DatabaseLock::release() noexcept {
    if(held==LockMode::None) return;
#ifdef _WIN32
    OVERLAPPED range{};
    // CloseHandle in the destructor is also a final release on abnormal exit.
    if(!UnlockFileEx(static_cast<HANDLE>(handle),0,1,0,&range)) std::terminate();
#else
    if(flock(handle,LOCK_UN)!=0) std::terminate();
#endif
    held=LockMode::None;
}
}
