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

#include "os/os.h"
#include "nimble/ble.h"
#include "ble_l2cap.h"
#include "ble_l2cap_util.h"

/**
 * Reads a uint16 from the specified L2CAP channel's buffer and converts it to
 * little endian.
 *
 * @return                      0 on success; nonzero if the channel buffer
 *                                  did not contain the requested data.
 */
int
ble_l2cap_read_uint16(const struct ble_l2cap_chan *chan, int off,
                      uint16_t *u16)
{
    int rc;

    rc = os_mbuf_copydata(chan->blc_rx_buf, off, sizeof *u16, u16);
    if (rc != 0) {
        return -1;
    }

    *u16 = le16toh((void *)u16);

    return 0;
}

/**
 * Removes the specified number of bytes from the front of an L2CAP channel's
 * buffer.  If the buffer is empty as a result, it is freed.
 */
void
ble_l2cap_strip(struct ble_l2cap_chan *chan, int delta)
{
    os_mbuf_adj(&ble_l2cap_mbuf_pool, chan->blc_rx_buf, delta);

    if (OS_MBUF_PKTHDR(chan->blc_rx_buf)->omp_len == 0) {
        os_mbuf_free_chain(&ble_l2cap_mbuf_pool, chan->blc_rx_buf);
        chan->blc_rx_buf = NULL;
    }
}
