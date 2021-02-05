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

#ifndef __RAPTOR_LITE_ACCEPTOR__
#define __RAPTOR_LITE_ACCEPTOR__

#include <stddef.h>
#include <stdint.h>
#include <string>

#include "raptor-lite/utils/status.h"

namespace raptor {
class Endpoint;
class Property;
class AcceptorHandler {
public:
    virtual ~AcceptorHandler() {}

    /*
     * socket property:
     *   1. SocketNoSIGPIPE     (bool, default: true)
     *   2. SocketReuseAddress  (bool, default: true)
     *   3. SocketRecvTimeoutMs (int)
     *   4. SocketSendTimeoutMs (int)
     *   5. SocketLowLatency    (bool, default: true)
     */
    virtual void OnAccept(const Endpoint &ep, Property &settings) = 0;
};

class Acceptor {
public:
    virtual ~Acceptor() {}
    virtual raptor_error Start() = 0;
    virtual void Shutdown() = 0;
    virtual raptor_error AddListening(const std::string &addr) = 0;
};

/*
 * Property:
 *   1. AcceptorHandler (required)
 *   2. ListenThreadNum (optional, default: 1)
 */
raptor_error CreateAcceptor(const Property &p, Acceptor **out);
void DestoryAcceptor(Acceptor *);
}  // namespace raptor

#endif  // __RAPTOR_LITE_ACCEPTOR__
