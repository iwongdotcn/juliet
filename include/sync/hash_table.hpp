/**
 * @file hash_table.hpp
 * 线程安全的散列表，基于std::unordered_map和读写锁的简单封装。
 * @author WangJun
 * @brief
 * @version 0.1
 * @date 2021/8/16 21:30
 */
#ifndef _SYNC_HASH_TABLE_HPP_
#define _SYNC_HASH_TABLE_HPP_

#if (defined __GNUC__ && ((__GNUC__ == 3 && __GNUC_MINOR__ >= 4) || __GNUC__ > 3)) || defined _MSC_VER
#pragma once
#endif /* __GNUC__ >= 3.4 || _MSC_VER */

#include <cassert>
#include <shared_mutex>
#include <functional>
#include <unordered_map>

namespace juliet::sync {

template<typename Key, typename Value>
class HashTable {
public:
    using Map = std::unordered_map<Key, Value>;

    HashTable() = default;

    ~HashTable() = default;

    enum EPutStatus {
        PUT_SKIPPED,
        PUT_NEW,
        PUT_OVERWRITE
    };

    /**
     * 写入一个数据
     * @param key
     * @param value
     * @return 是否新写入
     * @retval true 新写入
     * @retval false 改写已有值
     */
    EPutStatus Put(const Key& key, const Value& value) {
        std::lock_guard<std::shared_timed_mutex> guard(mu_);
        auto status = map_.emplace(key, value);
        if (status.second)
            return PUT_NEW;
        status.first->second = value;
        return PUT_OVERWRITE;
    }

    EPutStatus TryPut(const Key& key, const Value& value) {
        std::lock_guard<std::shared_timed_mutex> guard(mu_);
        return map_.emplace(key, value).second ? PUT_NEW : PUT_SKIPPED;
    }

    Value Get(const Key& key) const {
        Value result{};
        Get(key, result);
        return result;
    }

    bool Get(const Key& key, Value& value) const {
        std::shared_lock<std::shared_timed_mutex> lock(mu_);
        auto it = map_.find(key);
        if (it != map_.end()) {
            value = it->second;
            return true;
        }
        return false;
    }

    void Remove(const Key& key) {
        Value v;
        Remove(key, v);
    }

    /**
     *
     * @param key
     * @param value
     * @return 是否返回value
     */
    bool Remove(const Key& key, Value& value) {
        std::lock_guard<std::shared_timed_mutex> guard(mu_);
        auto it = map_.find(key);
        if (it != map_.end()) {
            value = std::move(it->second);
            map_.erase(it);
            return true;
        }
        return false;
    }

    void Clear() {
        Map m;
        Clear(m);
    }

    void Clear(Map& m) {
        m.clear();
        std::lock_guard<std::shared_timed_mutex> guard(mu_);
        map_.swap(m);
    }

    // 枚举器
    using Enumerator = std::function<void (const Key& key, const Value& value)>;

    void ForEach(const Enumerator& enumerator) {
        if (!enumerator)
            return;

        std::shared_lock<std::shared_timed_mutex> lock(mu_);
        for (const auto& kv : map_) {
            enumerator(kv.first, kv.second);
        }
    }

    // 移除谓词
    using RemovePredicator = std::function<bool (const Key& key, const Value& value)>;

    int RemoveIf(const RemovePredicator& predicator) {
        int count = 0;
        if (!predicator)
            return count;

        std::lock_guard<std::shared_timed_mutex> guard(mu_);
        for (auto it = map_.begin(); it != map_.end(); ++it) {
            if (predicator(it->first, it->second))
                ++it;
            else {
                ++count;
                map_.erase(it);
            }
        }
        return count;
    }

private:
    mutable std::shared_timed_mutex mu_;
    Map map_;
};

}

#endif // !_SYNC_HASH_TABLE_HPP_
