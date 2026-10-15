#pragma once
#include<Windows.h>

namespace ycnet::detail {

class srw_lock {
    SRWLOCK srwlock;
public:
    srw_lock() {
        InitializeSRWLock(&srwlock);
    }
    ~srw_lock() {
    }
    void lock_exclusive() {
        AcquireSRWLockExclusive(&srwlock);
    }
    void unlock_exclusive() {
        ReleaseSRWLockExclusive(&srwlock);
    }
    void lock_shared() {
        AcquireSRWLockShared(&srwlock);
    }
    void unlock_shared() {
        ReleaseSRWLockShared(&srwlock);
    }
};

class shared_lock_guard {
    srw_lock& lock;
public:
    shared_lock_guard(srw_lock& lock) :lock(lock) {
        lock.lock_shared();
    }
    ~shared_lock_guard() {
        lock.unlock_shared();
    }
};

class exclusive_lock_guard {
    srw_lock& lock;
public:
    exclusive_lock_guard(srw_lock& lock) :lock(lock) {
        lock.lock_exclusive();
    }
    ~exclusive_lock_guard() {
        lock.unlock_exclusive();
    }
};
} // namespace ycnet::detail
