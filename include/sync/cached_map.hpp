/**
 * @file cached_map.hpp
 * @brief 引入读缓存，分担写加锁压力。主要针对读多写少场景
 * @author WangJun
 * @version 0.1
 * @date 2021/8/20 22:49
 */
#ifndef _JULIET_SYNC_CACHED_MAP_HPP_
#define _JULIET_SYNC_CACHED_MAP_HPP_

#if (defined __GNUC__ &&                                          \
     ((__GNUC__ == 3 && __GNUC_MINOR__ >= 4) || __GNUC__ > 3)) || \
    defined _MSC_VER
#pragma once
#endif /* __GNUC__ >= 3.4 || _MSC_VER */

#include <memory>
#include <shared_mutex>
#include <unordered_map>

#include "hash_table.hpp"

namespace juliet::sync {

namespace cached {

template <typename Value>
struct Entry {
  mutable std::shared_timed_mutex mu_;
  // Null means not exists. Maybe been deleted or never stored.
  Value val_;

  void Store(const Value& v);

  Value Load() const;
};

template <typename Key, typename Value>
class Read {
 public:
  using EntryPtr = std::shared_ptr<Entry<Value>>;

  std::pair<EntryPtr, bool> Get(const Key& key) const;

  void TryStore(const Key& key, const Value& val);

  void Update(const Key& key, const Value& val);

  void Clear();

 private:
  mutable std::shared_timed_mutex mu_;
  std::unordered_map<Key, EntryPtr> map_;
};

template <typename Value>
inline void Entry<Value>::Store(const Value& v) {
  std::lock_guard<std::shared_timed_mutex> guard(mu_);
  val_ = v;
}

template <typename Value>
inline Value Entry<Value>::Load() const {
  std::shared_lock<std::shared_timed_mutex> lock(mu_);
  return val_;
}

template <typename Key, typename Value>
inline std::pair<typename Read<Key, Value>::EntryPtr, bool>
Read<Key, Value>::Get(const Key& key) const {
  std::shared_lock<std::shared_timed_mutex> lock(mu_);
  auto it = map_.find(key);
  if (it == map_.end()) return {nullptr, false};
  assert(it->second);
  return {it->second, true};
}

template <typename Key, typename Value>
inline void Read<Key, Value>::TryStore(const Key& key, const Value& val) {
  auto get = Get(key);
  if (!get.second) return;
  const auto& entry = get.first;
  assert(entry);
  entry->Store(val);
}

template <typename Key, typename Value>
inline void Read<Key, Value>::Update(const Key& key, const Value& val) {
  auto entry = std::make_shared<Entry<Value>>();
  entry->val_ = val;

  {
    std::lock_guard<std::shared_timed_mutex> lock(mu_);
    auto emplace = map_.emplace(key, entry);
    if (emplace.second) return;

    auto it = emplace.first;
    assert(it != map_.end());
    entry = it->second;
  }

  entry->Store(val);
}

template <typename Key, typename Value>
void Read<Key, Value>::Clear() {
  mu_.lock();
  auto m = std::move(map_);
  mu_.unlock();
}

}  // namespace cached

/**
 * 读缓存Map
 * 只在Cache missed时，以及读缓存过大时，修改读缓存表（使用互斥锁）
 * 其他时候都只读（使用共享锁），提升并发读性能
 * @tparam Key
 * @tparam Value
 */
template <typename Key, typename Value>
class CachedMap {
 public:
  using ValuePtr = std::shared_ptr<Value>;

  using EPutStatus = typename HashTable<Key, ValuePtr>::EPutStatus;

  using Map = std::unordered_map<Key, Value>;

  CachedMap();

  ~CachedMap() = default;

  // Put
  EPutStatus Put(const Key& key, const Value& value);

  EPutStatus TryPut(const Key& key, const Value& value);

  Value Get(const Key& key) const {
    Value result{};
    Get(key, result);
    return result;
  }

  bool Get(const Key& key, Value& value) const;

  void Remove(const Key& key) {
    Value v;
    Remove(key, v);
  }

  bool Remove(const Key& key, Value& value);

  void Clear() {
    std::unordered_map<Key, Value> m;
    Clear(m);
  }

  void Clear(Map& m);

 private:
  std::unique_ptr<cached::Read<Key, ValuePtr>> read_;

  HashTable<Key, ValuePtr> write_;
};

template <typename Key, typename Value>
inline CachedMap<Key, Value>::CachedMap()
    : read_(std::make_unique<cached::Read<Key, ValuePtr>>()) {}

template <typename Key, typename Value>
inline typename CachedMap<Key, Value>::EPutStatus CachedMap<Key, Value>::Put(
    const Key& key, const Value& value) {
  // 插入新的Value指针，永不改写Value
  // 查询得到的Value指针可安全地只读访问
  auto val = std::make_shared<Value>(value);
  auto status = write_.Put(key, val);
  read_->TryStore(key, val);
  return status;
}

template <typename Key, typename Value>
inline typename CachedMap<Key, Value>::EPutStatus CachedMap<Key, Value>::TryPut(
    const Key& key, const Value& value) {
  auto val = std::make_shared<Value>(value);
  auto status = write_.TryPut(key, val);
  // 只在改写成功后更新缓存
  if (status == EPutStatus::PUT_NEW) read_->TryStore(key, val);
  return status;
}

template <typename Key, typename Value>
inline bool CachedMap<Key, Value>::Get(const Key& key, Value& value) const {
  auto get = read_->Get(key);
  ValuePtr val;
  if (get.second) {
    const auto& entry = get.first;
    assert(entry);
    val = entry->Load();
  } else {
    val = write_.Get(key);
    read_->Update(key, val);
  }

  if (val) value = *val;
  return val != nullptr;
}

template <typename Key, typename Value>
inline bool CachedMap<Key, Value>::Remove(const Key& key, Value& value) {
  ValuePtr val;
  if (!write_.Remove(key, val)) return false;

  value = *val;
  read_->Update(key, nullptr);
  return true;
}

template <typename Key, typename Value>
inline void CachedMap<Key, Value>::Clear(Map& m) {
  std::unordered_map<Key, ValuePtr> w;
  write_.Clear(w);
  read_->Clear();

  for (const auto& kv : w) {
    m.emplace(kv.first, *kv.second);
  }
}

}  // namespace juliet::sync

#endif  // !_JULIET_SYNC_CACHED_MAP_HPP_
