#ifndef SCOPE_HPP
#define SCOPE_HPP

// PThreads
#include <pthread.h>

// Intel intrinsics
//#include <immintrin.h>

// STL
#include <functional>
#include <vector>

// xsync
#include "lock.hpp"
#include "spinlock-rtm.hpp"

namespace xsync {

/*
 * Provides an RAII style wrapper for transactional execution.
 */
template <typename LockType>
class XScope {
public:

    XScope(LockType& fallback) : fallback_(fallback) { enter(); }
    ~XScope() { exit(); }

    // No copy
    XScope(const XScope& other) = delete;

    // The next few functions need to be specialized for each of the locktypes
    // passing a non-specialized lock will result in additional aborts
    inline bool isFallbackLocked() {
        return false;
    }

    // These need to be specialized if the LockType is not a BasicLockable
    inline void lockFallback() { fallback_.lock();  }
    inline void unlockFallback() { fallback_.unlock(); }

    void enter(int nretries = 3) {
        unsigned int xact_status;

        for (int attempt = 0; attempt <= nretries; ++attempt) {
            xact_status = _xbegin();

            if (xact_status == _XBEGIN_STARTED) {

                if (!isFallbackLocked()) {
                    // No other threads are executing the critical section non-transactionally,
                    // so we can execute the critical section
                    return;
                } else {
                    // explicit abort since the lock is held
                    _xabort(0xFF);
                }

            } else {
                // We have aborted
                if ((xact_status & _XABORT_EXPLICIT) && _XABORT_CODE(xact_status) == 0xFF) {
                    // We aborted because the lock was held
                    // wait until the lock is free and then retry
                    lockFallback();
                    unlockFallback();

                } else if (!(xact_status & _XABORT_RETRY)) {
                    // If the retry bit is not set, take fallback
                    break;
                }
            }
        }
        // Take fallback
        lockFallback();
    }

    void exit() {
        if (isFallbackLocked()) {
            unlockFallback();
        } else {
            _xend();
        }

        // Execute callbacks
        for (std::function<void()> &cb : cbs_) {
            cb();
        }
    }

    // Callback must not throw an exception since it is executed in the destructor
    void registerCommitCallback(const std::function<void()> &cb) {
        cbs_.push_back(cb);
    }


private:
    //friend class XCondVar;
    std::vector<std::function<void()>> cbs_;
    LockType &fallback_;
};

// These are lock-specific hacks to check the state without modifying memory.
// Usually this state is private (for good reason), but we need to access it

// template<>
// bool XScope<spinlock_t>::isFallbackLocked() {
//     return *(reinterpret_cast<int*>(&fallback_)) == 0;
//}

/*template<>
bool XScope<pthread_mutex_t>::isFallbackLocked() {
    return fallback_.__data.__lock != 0;
}*/

template<>
inline void XScope<pthread_mutex_t>::lockFallback() {
    pthread_mutex_lock(&fallback_);
}

template<>
inline void XScope<pthread_mutex_t>::unlockFallback() {
    pthread_mutex_unlock(&fallback_);
}


} // namespace xsync

#endif
