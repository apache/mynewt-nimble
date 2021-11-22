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

#include <assert.h>
#include <os/mynewt.h>
#include <nimble/ble_hci_trans.h>

static int
forward_cmd_to_controller(uint8_t *cmdbuf, void *arg)
{
    (void)arg;

    return ble_hci_trans_hs_cmd_tx(cmdbuf);
}

int
forward_acl_to_controller(struct os_mbuf *om, void *arg)
{
    (void)arg;

    return ble_hci_trans_hs_acl_tx(om);
}

static int
forward_evt_to_host(uint8_t *hci_ev, void *arg)
{
    (void)arg;

    return ble_hci_trans_ll_evt_tx(hci_ev);
}

int
forward_acl_to_host(struct os_mbuf *om, void *arg)
{
    (void)arg;

    return ble_hci_trans_ll_acl_tx(om);
}

int
main(void)
{
    /* Initialize OS */
    sysinit();

    ble_hci_trans_cfg_hs(forward_evt_to_host, NULL, forward_acl_to_host, NULL);
    ble_hci_trans_cfg_ll(forward_cmd_to_controller, NULL, forward_acl_to_controller, NULL);

    while (1) {
        os_eventq_run(os_eventq_dflt_get());
    }
    return 0;
}
