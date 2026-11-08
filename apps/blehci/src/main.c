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

#include "os/mynewt.h"
#if MYNEWT_VAL(BLE_CONTROLLER)
#include "controller/ble_ll.h"
#endif

#if MYNEWT_VAL(OS_ASSERT_CB)
void
os_assert_cb(const char *file, int line, const char *func, const char *e)
{
    ble_ll_assert(file, line);
}
#endif

int
mynewt_main(int argc, char **argv)
{
    /* Initialize OS */
    sysinit();

    while (1) {
        os_eventq_run(os_eventq_dflt_get());
    }
    return 0;
}
