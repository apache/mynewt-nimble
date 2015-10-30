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
#include <stdint.h>
#include <stdio.h>
#include <assert.h>
#include <string.h>
#include "os/os.h"
#include "console/console.h"
#include "nimble/hci_common.h"
#include "nimble/hci_transport.h"

void
host_hci_dbg_le_event_disp(uint8_t subev, uint8_t len, uint8_t *evdata)
{
    int8_t rssi;
    uint8_t advlen;
    int i;
    int imax;
    uint8_t *dptr;
    char *adv_ptr;
    char adv_data_buf[32];

    switch (subev) {
    case 0x02:
        advlen = evdata[9];
        rssi = evdata[10 + advlen];
        console_printf("LE advertising report. len=%u num=%u evtype=%u "
                       "addrtype=%u addr=%x.%x.%x.%x.%x.%x advlen=%u "
                       "rssi=%d", len, evdata[0], evdata[1], evdata[2],
                       evdata[8], evdata[7], evdata[6], evdata[5],
                       evdata[4], evdata[3], advlen, rssi);
        if (advlen) {
            dptr = &evdata[10];
            while (advlen > 0) {
                memset(adv_data_buf, 0, 32);
                imax = advlen;
                if (imax > 8) {
                    imax = 8;
                }
                adv_ptr = &adv_data_buf[0];
                for (i = 0; i < imax; ++i) {
                    snprintf(adv_ptr, 4, "%02x ", *dptr);
                    adv_ptr += 3;
                    ++dptr;
                }
                advlen -= imax;
                console_printf("%s", adv_data_buf);
            }
        }
        break;
    default:
        console_printf("\tUnknown LE event");
        break;
    }
}

void
host_hci_dbg_event_disp(uint8_t *evbuf)
{
    uint8_t *evdata;
    uint8_t evcode;
    uint8_t len;
    uint16_t opcode;

    evcode = evbuf[0];
    len = evbuf[1];
    evdata = evbuf + 2;

    switch (evcode) {
    case BLE_HCI_EVCODE_COMMAND_COMPLETE:
        opcode = le16toh(evdata +1);
        console_printf("Command Complete: cmd_pkts=%u ocf=0x%x ogf=0x%x",
                       evdata[0], opcode & 0x3FF, opcode >> 10);
        break;
    case BLE_HCI_EVCODE_LE_META:
        host_hci_dbg_le_event_disp(evdata[0], len, evdata + 1);
        break;
    default:
        console_printf("Unknown event 0x%x len=%u", evcode, len);
        break;
    }
}
