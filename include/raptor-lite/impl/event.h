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

#include "raptor-lite/utils/status.h"

namespace raptor {

typedef enum { kManualShutdown, kSocketError, kConnectionTimeout } EventType;

class Event final {
public:
    Event()
        : _type(kManualShutdown)
        , _desc(nullptr) {}

    Event(EventType t, raptor_error err)
        : _type(t)
        , _desc(err) {}

    ~Event() {}

    EventType Type() const {
        return _type;
    }

    std::string What() const {
        if (_desc == RAPTOR_ERROR_NONE) {
            return std::string();
        }
        return _desc->ToString();
    }

    int ErrorCode() const {
        if (_desc == RAPTOR_ERROR_NONE) {
            return 0;
        }
        return _desc->ErrorCode();
    }

private:
    EventType _type;
    raptor_error _desc;
};
}  // namespace raptor
#endif  // __RAPTOR_LITE_EVENT__
