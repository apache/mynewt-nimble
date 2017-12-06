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

#include <stddef.h>
#include "os/os.h"
#include "sysinit/sysinit.h"
#include "host/ble_hs.h"
#include "services/gap/ble_svc_gap.h"
#include "services/gatt/ble_svc_gatt.h"
#include "services/ans/ble_svc_ans.h"
#include "services/ias/ble_svc_ias.h"
#include "services/lls/ble_svc_lls.h"
#include "services/tps/ble_svc_tps.h"
#include "controller/ble_ll.h"

void
nimble_init(void)
{
    void os_msys_init(void);
    void ble_hci_ram_pkg_init(void);
    void ble_store_ram_init(void);

    sysinit_start();
    os_msys_init();
    ble_hci_ram_pkg_init();
    ble_hs_init();
    ble_ll_init();
    ble_svc_gap_init();
    ble_svc_gatt_init();
    ble_svc_ans_init();
    ble_svc_ias_init();
    ble_svc_lls_init();
    ble_svc_tps_init();
    ble_store_ram_init();
    sysinit_end();
}

void
nimble_run(void)
{
    while (1) {
        struct os_event *ev;

        ev = os_eventq_get(os_eventq_dflt_get());
        assert(ev->ev_cb != NULL);

        ev->ev_cb(ev);
    }
}
