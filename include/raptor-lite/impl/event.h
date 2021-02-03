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

#ifndef __RAPTOR_LITE_EVENT__
#define __RAPTOR_LITE_EVENT__

#include <stddef.h>
#include <string>

namespace raptor {

typedef enum {
    kNoneError,
    kSocketError,
    kHeartbeatTimeout,
    kConnectFailed,
    kConnectionClosed
} EventType;

class Event final {
public:
    Event(EventType t)
        : _type(t)
        , _error_code(0) {}

    Event(EventType t, const std::string &desc, int err)
        : _type(t)
        , _desc(desc)
        , _error_code(err) {}

    ~Event() {}

    EventType Type() const {
        return _type;
    }

    const std::string &What() const {
        return _desc;
    }

    int ErrorCode() const {
        return _error_code;
    }

    void SetErrorCode(int e) {
        _error_code = e;
    }

    void SetErrorDesc(const std::string &desc) {
        _desc = desc;
    }

private:
    EventType _type;
    std::string _desc;
    int _error_code;
};
}  // namespace raptor
#endif  // __RAPTOR_LITE_EVENT__
