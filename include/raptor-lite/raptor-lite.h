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

#ifndef __RAPTOR_LITE_H__
#define __RAPTOR_LITE_H__

#include <raptor-lite/impl/acceptor.h>
#include <raptor-lite/impl/connector.h>
#include <raptor-lite/impl/container.h>
#include <raptor-lite/impl/endpoint.h>
#include <raptor-lite/impl/event.h>
#include <raptor-lite/impl/handler.h>
#include <raptor-lite/impl/property.h>

#include <raptor-lite/utils/atomic.h>
#include <raptor-lite/utils/cpu.h>
#include <raptor-lite/utils/list_entry.h>
#include <raptor-lite/utils/log.h>
#include <raptor-lite/utils/mpscq.h>
#include <raptor-lite/utils/ref_counted_ptr.h>
#include <raptor-lite/utils/ref_counted.h>
#include <raptor-lite/utils/slice_buffer.h>
#include <raptor-lite/utils/slice.h>
#include <raptor-lite/utils/status.h>
#include <raptor-lite/utils/sync.h>
#include <raptor-lite/utils/thread.h>
#include <raptor-lite/utils/time.h>
#include <raptor-lite/utils/useful.h>

int RaptorGlobalStartup();
int RaptorGlobalCleanup();

#endif  // __RAPTOR_LITE_H__
