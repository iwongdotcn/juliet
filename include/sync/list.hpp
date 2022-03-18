/**
 * @file list.hpp
 * @author WangJun
 * @brief 简单的并发链表
 * 目前只支持添加值的同时遍历链表，添加操作不会因为遍历耗时太长而阻塞，遍历操作也不会因为大量添加操作而饿死。
 * @version 0.1
 * @date 2021/8/16 18:51
 */
#ifndef _SYNC_LIST_HPP_
#define _SYNC_LIST_HPP_
#if (defined __GNUC__ && ((__GNUC__ == 3 && __GNUC_MINOR__ >= 4) || __GNUC__ > 3)) || defined _MSC_VER
#pragma once
#endif /* __GNUC__ >= 3.4 || _MSC_VER */

#include <list>
#include <atomic>
#include <mutex>
#include <shared_mutex>
#include <functional>

namespace juliet::sync {

template<typename Type>
class List {
public:
    template<typename Container>
    explicit List(const Container& container) : list_(container.begin(), container.end()) {

    }

    explicit List(std::list<Type>&& list) : list_(std::move(list)) {

    }

    List(List<Type>&& rr) noexcept : listMut_(std::move(rr.listMut_)), list_(std::move(rr.list_))
    , buffer_(std::move(rr.buffer_)), bufferMut_(std::move(rr.bufferMut_)) {

    }

    void Add(const Type& value) {
        std::lock_guard<std::mutex> guard(bufferMut_);
        buffer_.emplace_back(value);
    }

    void ForEach(const std::function<void (const Type&)>& func) {
        {
            std::lock_guard<std::shared_timed_mutex> guard(listMut_);

            bufferMut_.lock();
            auto buffer = std::move(buffer_);
            bufferMut_.unlock();

            for (const auto& v : buffer) {
                list_.emplace_back(v);
            }
        }

        std::shared_lock<std::shared_timed_mutex> lock(listMut_);
        for (const auto& v : list_) {
            func(v);
        }
    }

    int ForEachRemove(const std::function<bool (const Type&)>& func) {
        int count = 0;
        std::lock_guard<std::shared_timed_mutex> guard(listMut_);

        for (auto it = list_.begin(); it != list_.end(); ) {
            if (func(*it))
                ++it;
            else {
                it = list_.erase(it);
                ++count;
            }
        }

        bufferMut_.lock();
        auto buffer = std::move(buffer_);
        bufferMut_.unlock();

        for (const auto& v : buffer) {
            if (func(v))
                list_.emplace_back(v);
            else
                ++count;
        }
        return count;
    }

private:
    std::shared_timed_mutex listMut_;
    std::list<Type> list_;
    std::mutex bufferMut_;
    std::list<Type> buffer_;
};

}

#endif // !_SYNC_LIST_HPP_
