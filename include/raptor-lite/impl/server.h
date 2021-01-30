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

#ifndef __RAPTOR_LITE_SERVER__
#define __RAPTOR_LITE_SERVER__

#include <stddef.h>
#include <stdint.h>
#include <string>

namespace raptor {
class Server {
public:
    virtual ~Server() {}
    virtual bool AddListening(const std::string &addr) = 0;
    virtual bool Start() = 0;
    virtual void Shutdown() = 0;
    virtual bool SendRawMsg(uint64_t cid, const void *buff, size_t len) = 0;
    virtual bool Close(uint64_t cid) = 0;
};
}  // namespace raptor

#endif  // __RAPTOR_LITE_SERVER__
