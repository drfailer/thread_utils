#ifndef THREAD_UTILS_DATA_STRUCTURES_ARRAY_MAP
#define THREAD_UTILS_DATA_STRUCTURES_ARRAY_MAP
#include <vector>
#include <algorithm>

template <typename T>
struct TU_Maybe {
    T value;
    bool ok;
};

template <typename K, typename V>
struct TU_ArrayMap {
    struct Data {
        K key;
        V value;
        Data() = default;
        Data(K key, V value) : key(key), value(std::forward<V>(value)) {}
        Data(Data const &) = delete;
        Data &operator=(Data const &) = delete;
        Data(Data &&other) : key(std::move(other.key)), value(std::move(other.value)) {}
        Data &operator=(Data &&other) {
            this->key = std::move(other.key);
            this->value = std::move(other.value);
            return *this;
        }
        ~Data() = default;
        bool operator<(Data const &other) const { return this->key < other.key; }
        bool operator<(K const &key) const { return this->key < key; }
    };

    std::vector<Data> datas = {};

    void insert(K key, V value);
    bool contains(K key);
    TU_Maybe<V *> operator[](K key);
    TU_Maybe<V const *> operator[](K key) const;

    using iterator = std::vector<Data>::iterator;
    using const_iterator = std::vector<Data>::const_iterator;
    iterator begin() { return datas.begin(); }
    iterator end() { return datas.end(); }
    const_iterator begin() const { return datas.cbegin(); }
    const_iterator end() const{ return datas.cend(); }
    const_iterator cbegin() const { return datas.cbegin(); }
    const_iterator cend() const{ return datas.cend(); }

    TU_ArrayMap() = default;
    TU_ArrayMap(TU_ArrayMap<K, V> const &) = delete;
    TU_ArrayMap<K, V> &operator=(TU_ArrayMap<K, V> const &) = delete;
    TU_ArrayMap(TU_ArrayMap<K, V> &&other) : datas(std::move(other.datas)) {}
    TU_ArrayMap<K, V> &operator=(TU_ArrayMap<K, V> &&other) {
        this->datas = std::move(other.datas);
        return *this;
    }
    ~TU_ArrayMap() = default;
};

template <typename K, typename V>
void TU_ArrayMap<K, V>::insert(K key, V value) {
    Data data{key, std::move(value)};
    auto it = std::lower_bound(datas.begin(), datas.end(), data);
    datas.insert(it, std::move(data));
}

template <typename K, typename V>
bool TU_ArrayMap<K, V>::contains(K key) {
    auto it = std::lower_bound(datas.begin(), datas.end(), key);
    if (it == datas.end() || it->key != key) {
        return false;
    }
    return true;
}


template <typename K, typename V>
TU_Maybe<V *> TU_ArrayMap<K, V>::operator[](K key) {
    auto it = std::lower_bound(datas.begin(), datas.end(), key);
    if (it == datas.end() || it->key != key) {
        return {nullptr, false};
    }
    return {&it->value, true};
}

template <typename K, typename V>
TU_Maybe<V const *> TU_ArrayMap<K, V>::operator[](K key) const {
    auto it = std::lower_bound(datas.begin(), datas.end(), key);
    if (it == datas.end() || it->key != key) {
        return {nullptr, false};
    }
    return {&it->value, true};
}

#endif
