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

#ifndef __RAPTOR_LITE_HANDLER__
#define __RAPTOR_LITE_HANDLER__

#include <stddef.h>

namespace raptor {
class Event;
class Endpoint;
class Slice;

class ProtocolHandler {
public:
    virtual ~ProtocolHandler() {}

    // return -1: error;  0: need more data; > 0 : pack_len
    virtual int OnCheckPackageLength(const void *data, size_t len) = 0;
};

class MessageHandler {
public:
    virtual ~MessageHandler() {}
    virtual int OnMessage(Endpoint *ep, Slice *msg) = 0;
};

class HeartbeatHandler {
public:
    virtual ~HeartbeatHandler() {}

    // heart-beat event
    virtual void OnHeartbeat() = 0;

    // millseconds
    virtual size_t GetHeartbeatInterval() = 0;
};

class EventHandler {
public:
    virtual ~EventHandler() {}
    virtual void OnEvent(Endpoint *ep, Event *event) = 0;
};

}  // namespace raptor
#endif  // __RAPTOR_LITE_HANDLER__
