/*
 * Licensed to the Apache Software Foundation (ASF) under one
 * or more contributor license agreements.  See the NOTICE file
 * distributed with this work for additional information
 * regarding copyright ownership.  The ASF licenses this file
 * to you under the Apache License, Version 2.0 (the
 * "License"); you may not use this file except in compliance
 * with the License.  You may obtain a copy of the License at
 * 
 *  http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing,
 * software distributed under the License is distributed on an
 * "AS IS" BASIS, WITHOUT WARRANTIES OR CONDITIONS OF ANY
 * KIND, either express or implied.  See the License for the
 * specific language governing permissions and limitations
 * under the License.
 */

#ifndef _OS_MUTEX_H_
#define _OS_MUTEX_H_

#include "os/os.h"
#include "FreeRTOS.h"
#include "semphr.h"

#ifdef __cplusplus
extern "C" {
#endif

struct os_mutex
{
    SemaphoreHandle_t handle;
};

os_error_t os_mutex_init(struct os_mutex *mu);

os_error_t os_mutex_release(struct os_mutex *mu);

os_error_t os_mutex_pend(struct os_mutex *mu, uint32_t timeout);

#ifdef __cplusplus
}
#endif

#endif  /* _OS_MUTEX_H_ */
