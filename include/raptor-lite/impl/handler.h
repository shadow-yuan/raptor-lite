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

#ifndef __RAPTOR_IMPL_HANDLER__
#define __RAPTOR_IMPL_HANDLER__

#include <stddef.h>

#include "raptor-lite/utils/slice.h"

namespace raptor {

class Slice;
class Event;

class ProtocolHandler {
public:
    virtual ~ProtocolHandler() {}

    // return -1: error;  0: need more data; > 0 : pack_len
    virtual int CheckPackageLength(const void *data, size_t len) = 0;
};

class MessageHandler {
public:
    virtual ~MessageHandler() {}
    virtual int OnMessage(const Endpoint &ep, const Slice &slice) = 0;
};

class ConnectionHandler {
public:
    virtual ~ConnectionHandler() {}
    virtual int OnConnected(const Endpoint &ep) = 0;
    virtual int OnClosed(const Endpoint &ep) = 0;
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
    virtual void OnEvent(const Endpoint &ep, const Event &event) = 0;
};

}  // namespace raptor
#endif  // __RAPTOR_IMPL_HANDLER__
