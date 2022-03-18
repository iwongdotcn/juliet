/**
 * @file map.hpp
 * @brief C++ implementation from golang/sync.Map.
 * @author WangJun
 * @version 0.1
 * @date 2021/8/21 15:58
 */
#ifndef _JULIET_SYNC_MAP_H_
#define _JULIET_SYNC_MAP_H_

#if (defined __GNUC__ &&                                          \
     ((__GNUC__ == 3 && __GNUC_MINOR__ >= 4) || __GNUC__ > 3)) || \
    defined _MSC_VER
#pragma once
#endif /* __GNUC__ >= 3.4 || _MSC_VER */

#include <atomic>
#include <functional>
#include <memory>
#include <shared_mutex>
#include <unordered_map>
#include <utility>

namespace juliet::sync {
namespace map {

template <typename Value>
class Entry {
 public:
  using Ptr = std::shared_ptr<Value>;
  enum EState { kNull, kValue, kExpunged };

  explicit Entry(Ptr ptr) : ptr_(std::move(ptr)) {
    state_ = (ptr_ != nullptr) ? kValue : kNull;
  }

  static std::shared_ptr<Entry> NewEntry(const Value &val) {
    return std::make_shared<Entry>(std::make_shared<Value>(val));
  }

  struct LoadResult {
    Ptr value;
    bool loaded = false;
  };

  LoadResult Load() const {
    auto state = state_.load();
    if (state == kNull || state == kExpunged) {
      return {nullptr, false};
    }
    std::lock_guard<std::mutex> guard(mu_);
    return {ptr_, true};
  }

  bool TryStore(const Value &val) {
    auto cur_state = state_.load();
    for (;;) {
      if (cur_state == kExpunged) {
        return false;
      }
      if (state_.compare_exchange_strong(cur_state, kValue)) {
        std::lock_guard<std::mutex> guard(mu_);
        ptr_ = std::make_shared<Value>(val);
        return true;
      }
    }
  }

  void StoreLocked(const Value &val) {
    auto cur_state = state_.load();
    for (;;) {
      if (state_.compare_exchange_strong(cur_state, kValue)) {
        std::lock_guard<std::mutex> guard(mu_);
        ptr_ = std::make_shared<Value>(val);
        break;
      }
    }
  }

  struct TryLoadOrStoreResult {
    Ptr actual;
    bool loaded = false;
    bool ok = false;
  };

  /**
   * TryLoadOrStore
   * @param val
   * @return Value, loaded, Ok
   */
  TryLoadOrStoreResult TryLoadOrStore(const Value &val) {
    auto cur_state = state_.load();
    if (cur_state == kExpunged) {
      return {nullptr, false, false};
    }
    if (cur_state != kNull) {
      std::lock_guard<std::mutex> guard(mu_);
      return {ptr_, true, true};
    }

    for (;;) {
      assert(cur_state == kNull);
      if (state_.compare_exchange_strong(cur_state, kValue)) {
        std::lock_guard<std::mutex> guard(mu_);
        ptr_ = std::make_shared<Value>(val);
        return {ptr_, false, true};
      }
      if (cur_state == kExpunged) {
        return {nullptr, false, false};
      }
      if (cur_state != kNull) {
        std::lock_guard<std::mutex> guard(mu_);
        return {ptr_, true, true};
      }
    }
  }

  bool Delete(Value *val) {
    auto cur_state = state_.load();
    for (;;) {
      if (cur_state == kNull || cur_state == kExpunged) {
        return false;
      }
      if (state_.compare_exchange_strong(cur_state, kNull)) {
        Ptr ptr;
        {
          std::lock_guard<std::mutex> guard(mu_);
          ptr.swap(ptr_);
        }
        assert(ptr != nullptr);
        if (val != nullptr) {
          *val = std::move(*ptr);
        }
        return true;
      }
    }
  }

  bool TryExpungeLocked() {
    auto cur_state = state_.load();
    while (cur_state == kNull) {
      if (state_.compare_exchange_strong(cur_state, kExpunged)) {
        return true;
      }
    }
    return cur_state == kExpunged;
  }

  bool UnexpungeLocked() {
    auto expunged = kExpunged;
    if (state_.compare_exchange_strong(expunged, kNull)) {
      std::lock_guard<std::mutex> guard(mu_);
      ptr_ = nullptr;
      return true;
    }
    return false;
  }

 private:
  Ptr ptr_;
  std::atomic<EState> state_{kNull};
  mutable std::mutex mu_;
};

template <typename Key, typename Value>
struct ReadOnly {
  using InnerMap = std::unordered_map<Key, std::shared_ptr<Entry<Value>>>;
  std::shared_ptr<InnerMap> m;
  bool amended;

  ReadOnly() : m(std::make_shared<InnerMap>()), amended(false) {}

  explicit ReadOnly(std::shared_ptr<InnerMap> _m)
      : m(std::move(_m)), amended(false) {}
};

template <typename Key, typename Value>
struct Read {
  // There's no feature for std::atomic<struct T> in C++11,
  // so only can implement it with rwlock.
  // The performance will slightly inferior to golang/sync.Map.
  mutable std::shared_timed_mutex mu;
  ReadOnly<Key, Value> readOnly;

  ReadOnly<Key, Value> Load() const {
    std::shared_lock<std::shared_timed_mutex> lock(mu);
    return readOnly;
  }

  void Store(ReadOnly<Key, Value> ro) {
    std::lock_guard<std::shared_timed_mutex> guard(mu);
    readOnly = std::move(ro);
  }
};

/**
 * Read cached map.
 * Modify cache table only on cache missed or read cache oversize.
 * Other times, use read-only map.
 * @tparam Key
 * @tparam Value
 */
template <typename Key, typename Value>
class Map {
 public:
  using RawMap = std::unordered_map<Key, Value>;
  using ValueEntry = Entry<Value>;
  using EntryPtr = std::shared_ptr<ValueEntry>;
  using InnerMap = std::unordered_map<Key, EntryPtr>;

  void Store(const Key &key, const Value &value) {
    auto read = read_.Load();
    auto it = read.m->find(key);
    if (it != read.m->end()) {
      auto entry = it->second;
      assert(entry != nullptr);
      if (entry->TryStore(value)) {
        return;
      }
    }

    std::lock_guard<std::mutex> guard(mu_);
    read = read_.Load();
    it = read.m->find(key);
    if (it != read.m->end()) {
      auto entry = it->second;
      assert(entry != nullptr);
      if (entry->UnexpungeLocked()) {
        assert(dirty_ != nullptr);
        (*dirty_)[key] = entry;
      }
      entry->StoreLocked(value);
    } else if (dirty_ != nullptr && (it = dirty_->find(key)) != dirty_->end()) {
      auto entry = it->second;
      assert(entry != nullptr);
      entry->StoreLocked(value);
    } else {
      if (!read.amended) {
        DirtyLocked();
        read_.readOnly.amended = true;
      }
      assert(dirty_ != nullptr);
      (*dirty_)[key] = ValueEntry::NewEntry(value);
    }
  }

  Value Load(const Key &key) {
    Value result{};
    Get(key, &result);
    return result;
  }

  bool Load(const Key &key, Value *value) {
    EntryPtr entry;
    auto read = read_.Load();
    auto it = read.m->find(key);
    if (it != read.m->end()) {
      entry = it->second;
    } else if (read.amended) {
      std::lock_guard<std::mutex> guard(mu_);
      read = read_.Load();
      it = read.m->find(key);
      if (it != read.m->end()) {
        entry = it->second;
      } else if (read.amended) {
        it = dirty_->find(key);
        if (it != dirty_->end()) {
          entry = it->second;
        }
        MissLocked();
      }
    }
    if (entry) {
      auto load_result = entry->Load();
      if (load_result.loaded) {
        assert(load_result.value != nullptr);
        assert(value != nullptr);
        *value = *load_result.value;
        return true;
      }
    }
    return false;
  }

  /**
   * Get value if key exists, or put a new key value.
   * @param key
   * @param value
   * @param actual returns the value if key exists.
   * @return true if the value was loaded, false if store.
   */
  bool LoadOrStore(const Key &key, const Value &value, Value *actual) {
    auto read = read_.Load();
    auto it = read.m->find(key);
    if (it != read.m->end()) {
      auto entry = it->second;
      assert(entry != nullptr);
      auto try_result = entry->TryLoadOrStore(value);
      if (try_result.ok) {
        assert(actual != nullptr);
        *actual = *try_result.actual;
        return try_result.loaded;
      }
    }

    std::lock_guard<std::mutex> guard(mu_);
    read = read_.Load();
    it = read.m->find(key);
    if (it != read.m->end()) {
      auto entry = it->second;
      assert(entry != nullptr);
      if (entry->UnexpungeLocked()) {
        assert(dirty_ != nullptr);
        (*dirty_)[key] = entry;
      }
      auto try_result = entry->TryLoadOrStore(value);
      assert(actual != nullptr);
      *actual = *try_result.actual;
      return try_result.loaded;
    } else if (dirty_ != nullptr && (it = dirty_->find(key)) != dirty_->end()) {
      auto entry = it->second;
      assert(entry != nullptr);
      auto try_result = entry->TryLoadOrStore(value);
      MissLocked();
      assert(actual != nullptr);
      *actual = *try_result.actual;
      return try_result.loaded;
    }

    if (!read.amended) {
      DirtyLocked();
      read_.readOnly.amended = true;
    }
    assert(dirty_ != nullptr);
    (*dirty_)[key] = ValueEntry::NewEntry(value);
    assert(actual != nullptr);
    *actual = value;
    return false;
  }

  void Delete(const Key &key) { Delete(key, nullptr); }

  bool Delete(const Key &key, Value *value) {
    EntryPtr entry;
    auto read = read_.Load();
    auto it = read.m->find(key);
    if (it != read.m->end()) {
      entry = it->second;
    } else if (read.amended) {
      std::lock_guard<std::mutex> guard(mu_);
      read = read_.Load();
      it = read.m->find(key);
      if (it != read.m->end()) {
        entry = it->second;
      } else if (read.amended) {
        it = dirty_->find(key);
        if (it != dirty_->end()) {
          entry = it->second;
          dirty_->erase(it);
        }
        MissLocked();
      }
    }
    return entry != nullptr && entry->Delete(value);
  }

  void Reset() { Reset(nullptr); }

  void Reset(RawMap *raw) {
    ReadOnly<Key, Value> read;
    {
      std::lock_guard<std::mutex> guard(mu_);
      read = read_.Load();
      if (read.amended) {
        read = ReadOnly<Key, Value>{std::move(dirty_)};
      }
      read_.Store(ReadOnly<Key, Value>{});
      dirty_ = nullptr;
      misses_ = 0;
    }

    if (raw != nullptr) {
      assert(read.m != nullptr);
      for (const auto &kv : *read.m) {
        auto entry = kv.second;
        assert(entry != nullptr);
        auto loaded = entry->Load();
        if (loaded.loaded) {
          raw->emplace(kv.first, *(loaded.value));
        }
      }
    }
  }

  using Enumerator = std::function<bool(const Key &key, const Value &value)>;

  void Range(const Enumerator &enumerator) {
    if (!enumerator) {
      return;
    }

    auto read = read_.Load();
    if (read.amended) {
      std::lock_guard<std::mutex> guard(mu_);
      read = read_.Load();
      if (read.amended) {
        read = ReadOnly<Key, Value>{std::move(dirty_)};
        read_.Store(read);
        dirty_ = nullptr;
        misses_ = 0;
      }
    }

    assert(read.m != nullptr);
    for (const auto &kv : *read.m) {
      auto entry = kv.second;
      assert(entry != nullptr);
      auto load = entry->Load();
      if (load.loaded && !enumerator(kv.first, *(load.value))) {
        break;
      }
    }
  }

  //  using RemovePredicator = std::function<bool(const Key &key, const Value
  //  &value)>;
  //
  //  int RemoveIf(const RemovePredicator &predicator) {
  //    return 0;
  //  }

 private:
  void MissLocked() {
    ++misses_;
    assert(dirty_ != nullptr);
    if (misses_ < static_cast<int>(dirty_->size())) {
      return;
    }

    read_.Store(ReadOnly<Key, Value>{std::move(dirty_)});
    dirty_ = nullptr;
    misses_ = 0;
  }

  void DirtyLocked() {
    if (dirty_) {
      return;
    }
    dirty_ = std::make_shared<InnerMap>();

    auto read = read_.Load();
    for (const auto &kv : *read.m) {
      auto entry = kv.second;
      assert(entry != nullptr);
      if (!entry->TryExpungeLocked()) {
        dirty_->emplace(kv);
      }
    }
  }

 private:
  mutable std::mutex mu_;
  Read<Key, Value> read_;
  std::shared_ptr<InnerMap> dirty_;
  int misses_ = 0;
};

}  // namespace map

using map::Map;

}  // namespace juliet::sync

#endif  // !_JULIET_SYNC_MAP_H_
