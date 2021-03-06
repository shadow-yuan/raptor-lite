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

#include "src/common/sockaddr.h"
#include "raptor-lite/utils/log.h"

int RaptorGlobalStartup() {
    raptor::LogStartup();
#ifdef _WIN32
    WSADATA wsaData;
    int status = WSAStartup(MAKEWORD(2, 0), &wsaData);
    if (status != 0) {
        log_error("Failed to WSAStartup, status = %d, error = %d", status, WSAGetLastError());
    }
    return status;
#else
    return 0;
#endif
}

int RaptorGlobalCleanup() {
    raptor::LogCleanup();
#ifdef _WIN32
    return WSACleanup();
#else
    return 0;
#endif
}
