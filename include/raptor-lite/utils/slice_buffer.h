/*
 *
 * Copyright (c) 2020 The Raptor Authors. All rights reserved.
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

#ifndef __RAPTOR_UTILS_SLICE_BUFFER__
#define __RAPTOR_UTILS_SLICE_BUFFER__

#include <stddef.h>
#include <stdint.h>
#include <functional>
#include <vector>

#include "raptor-lite/utils/slice.h"

namespace raptor {
class SliceBuffer final {
public:
    SliceBuffer();
    ~SliceBuffer();

    Slice Merge() const;
    size_t SliceCount() const;
    size_t GetBufferLength() const;
    void AddSlice(const Slice &s);
    void AddSlice(Slice &&s);

    Slice GetHeader(size_t len);
    bool MoveHeader(size_t len);
    void ClearBuffer();
    bool Empty() const;

    // have assert if empty
    const Slice &Front() const;
    const Slice &Back() const;

    void PopFront();
    void PopBack();

    // callback returns false to interrupt the loop
    void ForEachSlice(std::function<bool(const Slice &s)> callback) const;
    const Slice &operator[](size_t n) const;
    inline const Slice &At(size_t n) const {
        return this->operator[](n);
    }

private:
    size_t CopyToBuffer(void *buff, size_t len);

    std::vector<Slice> _vs;
    size_t _length;
};

}  // namespace raptor

#endif  // __RAPTOR_UTILS_SLICE_BUFFER__
