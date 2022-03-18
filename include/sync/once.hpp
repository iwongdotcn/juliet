/**
 * sync.Once提供3层保证：
 *   1. 一个Once对象只会执行一次任务，多次调用sync.Once.Call，只有第一个会执行；
 *   2. 多线程同时调用sync.Once.Call，只会有一个线程真正执行任务；
 *   3. 多线程并发调用时，其他线程会等待第一个执行的线程调用结束，以便同步地检查执行结果。
 */
#ifndef _JULIET_SYNC_ONCE_H_
#define _JULIET_SYNC_ONCE_H_

#include <cassert>
#include <atomic>
#include <mutex>
#include <functional>
#include "Defer.hpp"

namespace juliet::sync {

/**
 * 封装成Once类，方便管理done标识和锁
 */
class Once {
public:
    Once() : done_(false) {

    }

    template<typename Callable, typename... Args>
    void Call(Callable&& _f, Args&&... _args) {
        if (!done_.load()) {
            std::function<void()> func = std::bind(std::forward<Callable>(_f), std::forward<Args>(_args)...);
            CallSlow(func);
        }
    }

private:
    void CallSlow(const std::function<void()>& _func) {
        assert(_func);
        std::lock_guard<std::mutex> guard(mu_);
        if (!done_) {
            DEFER([this]() { done_.store(true); });
            _func();
        }
    }

    std::atomic_bool done_;
    std::mutex mu_;
};

}

#endif // !_JULIET_SYNC_ONCE_H_
