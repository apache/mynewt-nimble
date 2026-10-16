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

/**
  Unit tests for the Mutex api (ble_npl_mutex):

  ble_npl_error_t ble_npl_mutex_init(struct ble_npl_mutex *mu);
  ble_npl_error_t ble_npl_mutex_pend(struct ble_npl_mutex *mu, uint32_t timeout);
  ble_npl_error_t ble_npl_mutex_release(struct ble_npl_mutex *mu);
  int ble_npl_mutex_locked_by_cur_task(struct ble_npl_mutex *mu);
*/

#include <pthread.h>

#include "test_util.h"
#include "nimble/nimble_npl.h"

static struct ble_npl_mutex mutex;

static int other_thread_locked;
static int other_thread_pend_rc;

static void *
other_thread_handler(void *arg)
{
    other_thread_locked = ble_npl_mutex_locked_by_cur_task(&mutex);

    /* Mutex is held by main thread so this shall time out */
    other_thread_pend_rc = ble_npl_mutex_pend(&mutex, 10);

    return NULL;
}

static void *
other_thread_lock_handler(void *arg)
{
    SuccessOrQuit(ble_npl_mutex_pend(&mutex, BLE_NPL_TIME_FOREVER),
                  "ble_npl_mutex_pend: error locking mutex.");
    other_thread_locked = ble_npl_mutex_locked_by_cur_task(&mutex);
    SuccessOrQuit(ble_npl_mutex_release(&mutex),
                  "ble_npl_mutex_release: error releasing mutex.");

    return NULL;
}

int
main(int argc, char **argv)
{
    pthread_t thread;

    SuccessOrQuit(ble_npl_mutex_init(&mutex),
                  "ble_npl_mutex_init: error initializing mutex.");
    VerifyOrQuit(!ble_npl_mutex_locked_by_cur_task(&mutex),
                 "mutex reported as locked after init.");

    SuccessOrQuit(ble_npl_mutex_pend(&mutex, BLE_NPL_TIME_FOREVER),
                  "ble_npl_mutex_pend: error locking mutex.");
    VerifyOrQuit(ble_npl_mutex_locked_by_cur_task(&mutex),
                 "mutex not reported as locked by owner.");

    /* Nested lock */
    SuccessOrQuit(ble_npl_mutex_pend(&mutex, BLE_NPL_TIME_FOREVER),
                  "ble_npl_mutex_pend: error locking mutex (nested).");
    SuccessOrQuit(ble_npl_mutex_release(&mutex),
                  "ble_npl_mutex_release: error releasing mutex (nested).");
    VerifyOrQuit(ble_npl_mutex_locked_by_cur_task(&mutex),
                 "mutex not reported as locked after nested release.");

    /* Other thread shall not see mutex as locked by itself */
    pthread_create(&thread, NULL, other_thread_handler, NULL);
    pthread_join(thread, NULL);
    VerifyOrQuit(!other_thread_locked,
                 "mutex reported as locked by non-owner thread.");
    VerifyOrQuit(other_thread_pend_rc == BLE_NPL_TIMEOUT,
                 "non-owner thread locked mutex held by owner.");
    VerifyOrQuit(ble_npl_mutex_locked_by_cur_task(&mutex),
                 "mutex not reported as locked after non-owner pend.");

    SuccessOrQuit(ble_npl_mutex_release(&mutex),
                  "ble_npl_mutex_release: error releasing mutex.");
    VerifyOrQuit(!ble_npl_mutex_locked_by_cur_task(&mutex),
                 "mutex reported as locked after release.");

    /* Mutex locked and released by other thread */
    pthread_create(&thread, NULL, other_thread_lock_handler, NULL);
    pthread_join(thread, NULL);
    VerifyOrQuit(other_thread_locked,
                 "mutex not reported as locked by other thread.");
    VerifyOrQuit(!ble_npl_mutex_locked_by_cur_task(&mutex),
                 "mutex reported as locked after other thread released it.");

    /* Timed lock */
    SuccessOrQuit(ble_npl_mutex_pend(&mutex, 100),
                  "ble_npl_mutex_pend: error locking mutex with timeout.");
    VerifyOrQuit(ble_npl_mutex_locked_by_cur_task(&mutex),
                 "mutex not reported as locked after timed lock.");
    SuccessOrQuit(ble_npl_mutex_release(&mutex),
                  "ble_npl_mutex_release: error releasing mutex.");

    printf("All tests passed\n");

    return PASS;
}
