// 바이트 예산 기반 LRU 캐시. 스레드 안전하지 않으므로 호출자가 잠근다.
#pragma once
#include <cstddef>
#include <list>
#include <unordered_map>
#include <utility>

namespace ft {

template <class K, class V>
class LruCache {
public:
    explicit LruCache(size_t budgetBytes) : budget_(budgetBytes) {}

    // 있으면 값을 돌려주고 가장 최근으로 올린다. 없으면 nullptr.
    V* Get(const K& key) {
        auto it = map_.find(key);
        if (it == map_.end()) return nullptr;
        lru_.splice(lru_.begin(), lru_, it->second);
        return &it->second->value;
    }
    bool Contains(const K& key) const { return map_.find(key) != map_.end(); }

    void Put(const K& key, V value, size_t bytes) {
        auto it = map_.find(key);
        if (it != map_.end()) {
            bytes_ -= it->second->bytes;
            lru_.erase(it->second);
            map_.erase(it);
        }
        lru_.push_front(Node{key, std::move(value), bytes});
        map_[key] = lru_.begin();
        bytes_ += bytes;
        while (bytes_ > budget_ && lru_.size() > 1) {
            const Node& last = lru_.back();
            bytes_ -= last.bytes;
            map_.erase(last.key);
            lru_.pop_back();
        }
    }

    void Clear() {
        lru_.clear();
        map_.clear();
        bytes_ = 0;
    }
    size_t Bytes() const { return bytes_; }
    size_t Count() const { return lru_.size(); }

private:
    struct Node {
        K key;
        V value;
        size_t bytes;
    };
    size_t budget_;
    size_t bytes_ = 0;
    std::list<Node> lru_;
    std::unordered_map<K, typename std::list<Node>::iterator> map_;
};

}  // namespace ft
