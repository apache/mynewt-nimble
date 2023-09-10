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

#include <stdint.h>
#define __USE_GNU
#include <pthread.h>

#include "nimble/nimble_npl.h"

static struct ble_npl_mutex s_mutex;
static uint8_t s_mutex_inited = 0;

uint32_t ble_npl_hw_enter_critical(void)
{
    if( !s_mutex_inited ) {
        ble_npl_mutex_init(&s_mutex);
        s_mutex_inited = 1;
    }

    pthread_mutex_lock(&s_mutex.lock);
    return 0;
}

void ble_npl_hw_exit_critical(uint32_t ctx)
{
    pthread_mutex_unlock(&s_mutex.lock);
}
