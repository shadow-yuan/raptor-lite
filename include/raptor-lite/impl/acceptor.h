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

namespace raptor {
class Endpoint;
class Property;
class AcceptorHandler {
public:
    virtual ~AcceptorHandler() {}

    /*
     * socket property:
     *   1. SocketNoSIGPIPE     (bool)
     *   2. SocketReuseAddress  (bool)
     *   3. SocketRecvTimeout   (int)
     *   4. SocketSendTimeout   (int)
     */
    virtual void OnAccept(Endpoint *ep, Property *settings);
};

class Acceptor {
public:
    virtual ~Acceptor() {}
    virtual bool Start() = 0;
    virtual void Shutdown() = 0;
    virtual bool AddListening(const std::string &addr) = 0;
};

/*
 * Property:
 *   1. AcceptorHandler (required)
 */
Acceptor *CreateAcceptor(const Property &p);
void DestoryAcceptor(Acceptor *);
}  // namespace raptor

#endif  // __RAPTOR_LITE_ACCEPTOR__
