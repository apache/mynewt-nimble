/**
 * Copyright (c) 2015 Runtime Inc.
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *   http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

#include <string.h>
#include <errno.h>
#include <assert.h>
#include "nimble/ble.h"
#include "ble_l2cap.h"
#include "ble_l2cap_sig.h"

static int
ble_l2cap_sig_rx(struct ble_hs_conn *conn, struct ble_l2cap_chan *chan,
                 struct os_mbuf *om)
{
    return BLE_ERR_UNSUPPORTED;
}

struct ble_l2cap_chan *
ble_l2cap_sig_create_chan(void)
{
    struct ble_l2cap_chan *chan;

    chan = ble_l2cap_chan_alloc();
    if (chan == NULL) {
        return NULL;
    }

    chan->blc_cid = BLE_L2CAP_CID_SIG;
    chan->blc_my_mtu = BLE_L2CAP_SIG_MTU;
    chan->blc_default_mtu = BLE_L2CAP_SIG_MTU;
    chan->blc_rx_fn = ble_l2cap_sig_rx;

    return chan;
}
