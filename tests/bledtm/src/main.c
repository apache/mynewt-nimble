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

#include "syscfg/syscfg.h"
#include "host/ble_hs.h"

extern int ble_hs_hci_cmd_tx(uint16_t, void *, uint8_t, void *, uint8_t, uint8_t *);

static void
bledtm_on_sync(void)
{
    uint8_t evtlen;
    uint8_t cmdlen;
    uint8_t cmd[3];
    uint8_t evt[2];
    int rc;

    evtlen = 0;
    cmdlen = 3;
    cmd[0] = MYNEWT_VAL(DTM_RF_CHANNEL);
    cmd[1] = MYNEWT_VAL(DTM_PAYLOAD_LENGTH);
    cmd[2] = MYNEWT_VAL(DTM_PAYLOAD_TYPE);

    rc = ble_hs_hci_cmd_tx(0x201e, cmd, cmdlen, evt, 2, &evtlen);
    assert(rc == 0);
}

int
main(void)
{
    sysinit();

    ble_hs_cfg.sync_cb = bledtm_on_sync;

    while (1) {
        os_eventq_run(os_eventq_dflt_get());
    }

    return 0;
}
