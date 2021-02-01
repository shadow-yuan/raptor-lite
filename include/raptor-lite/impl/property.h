/*
 *
 * Copyright (v) 2020 The Raptor Authors. All rights reserved.
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *     http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 *
 */

#ifndef __RAPTOR_LITE_PROPERTY__
#define __RAPTOR_LITE_PROPERTY__

#include <stddef.h>
#include <stdint.h>
#include <initializer_list>
#include <string>
#include <utility>
#include <unordered_map>

namespace raptor {
class PropertyEntry final {
public:
    PropertyEntry()
        : PropertyEntry(std::string(), uintptr_t(0)) {}

    ~PropertyEntry() {}

    PropertyEntry(const std::string &k, bool v)
        : PropertyEntry(std::string(k), uintptr_t(v ? 1 : 0)) {}

    PropertyEntry(const std::string &k, const void *v)
        : PropertyEntry(std::string(k), uintptr_t(reinterpret_cast<uintptr_t>(v))) {}

    PropertyEntry(const std::string &k, intptr_t v)
        : PropertyEntry(std::string(k), uintptr_t(v)) {}

    PropertyEntry(const std::string &k, uintptr_t v) {
        _kv = std::make_pair(k, v);
    }

    PropertyEntry(const std::string &k, int8_t v)
        : PropertyEntry(std::string(k), uintptr_t(v)) {}

    PropertyEntry(const std::string &k, int16_t v)
        : PropertyEntry(std::string(k), uintptr_t(v)) {}

    PropertyEntry(const std::string &k, int32_t v)
        : PropertyEntry(std::string(k), uintptr_t(v)) {}

    PropertyEntry(const std::string &k, int64_t v)
        : PropertyEntry(std::string(k), uintptr_t(v)) {}

    PropertyEntry(const std::string &k, uint8_t v)
        : PropertyEntry(std::string(k), uintptr_t(v)) {}

    PropertyEntry(const std::string &k, uint16_t v)
        : PropertyEntry(std::string(k), uintptr_t(v)) {}

    PropertyEntry(const std::string &k, uint32_t v)
        : PropertyEntry(std::string(k), uintptr_t(v)) {}

    PropertyEntry(const std::string &k, uint64_t v)
        : PropertyEntry(std::string(k), uintptr_t(v)) {}

    PropertyEntry(const PropertyEntry &o)
        : PropertyEntry(o._kv.first, o._kv.second) {}

    PropertyEntry &operator=(const PropertyEntry &o) {
        if (&o != this) {
            _kv.first = o._kv.first;
            _kv.second = o._kv.second;
        }
        return *this;
    }

    const std::string &Key() const {
        return _kv.first;
    }

    template <typename T>
    T Value() const {
        return static_cast<T>(_kv.second);
    }

    const std::pair<std::string, uintptr_t> &KeyValuePair() const {
        return _kv;
    }

private:
    std::pair<std::string, uintptr_t> _kv;
};

class Property final {
public:
    Property() = default;
    ~Property() = default;

    Property(std::initializer_list<PropertyEntry> init_list) {
        for (auto it = init_list.begin(); it != init_list.end(); ++it) {
            _tbl.insert(it->KeyValuePair());
        }
    }

    template <typename T>
    T GetValue(const std::string &key, T default_value = T()) {
        auto it = _tbl.find(key);
        if (it == _tbl.end()) {
            return default_value;
        }
        return static_cast<T>(it->second);
    }

private:
    std::unordered_map<std::string, uintptr_t> _tbl;
};
}  // namespace raptor
#endif  // __RAPTOR_LITE_PROPERTY__
