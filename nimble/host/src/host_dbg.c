/**
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
#include <stdio.h>
#include <assert.h>
#include <string.h>
#include "os/os.h"
#include "console/console.h"
#include "nimble/hci_common.h"
#include "nimble/hci_transport.h"
#include "ble_hs_priv.h"

void
host_hci_dbg_le_event_disp(uint8_t subev, uint8_t len, uint8_t *evdata)
{
    int8_t rssi;
    uint8_t advlen;
    uint8_t status;
    int i;
    int imax;
    uint8_t *dptr;
    char *adv_ptr;
    char adv_data_buf[32];

    switch (subev) {
    case BLE_HCI_LE_SUBEV_CONN_COMPLETE:
        status = evdata[0];
        if (status == BLE_ERR_SUCCESS) {
            BLE_HS_LOG(DEBUG, "LE connection complete. handle=%u role=%u "
                              "paddrtype=%u addr=%x.%x.%x.%x.%x.%x itvl=%u "
                              "latency=%u spvn_tmo=%u mca=%u\n", 
                       le16toh(evdata + 1), evdata[3], evdata[4], 
                       evdata[10], evdata[9], evdata[8], evdata[7],
                       evdata[6], evdata[5], le16toh(evdata + 11), 
                       le16toh(evdata + 13), le16toh(evdata + 15), 
                       evdata[17]);
        } else {
            BLE_HS_LOG(DEBUG, "LE connection complete. FAIL (status=%u)\n",
                       status);
        }
        break;
    case BLE_HCI_LE_SUBEV_ADV_RPT:
        advlen = evdata[9];
        rssi = evdata[10 + advlen];
        BLE_HS_LOG(DEBUG, "LE advertising report. len=%u num=%u evtype=%u "
                          "addrtype=%u addr=%x.%x.%x.%x.%x.%x advlen=%u "
                          "rssi=%d\n", len, evdata[0], evdata[1], evdata[2],
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
                BLE_HS_LOG(DEBUG, "%s\n", adv_data_buf);
            }
        }
        break;
    case BLE_HCI_LE_SUBEV_CONN_UPD_COMPLETE:
        status = evdata[0];
        if (status == BLE_ERR_SUCCESS) {
            BLE_HS_LOG(DEBUG, "LE Connection Update Complete. handle=%u "
                              "itvl=%u latency=%u timeout=%u\n",
                       le16toh(evdata + 1), le16toh(evdata + 3), 
                       le16toh(evdata + 5), le16toh(evdata + 7));
        } else {
            BLE_HS_LOG(DEBUG, "LE Connection Update Complete. FAIL "
                              "(status=%u)\n", status);
        }
        break;

    case BLE_HCI_LE_SUBEV_DATA_LEN_CHG:
        BLE_HS_LOG(DEBUG, "LE Data Length Change. handle=%u max_tx_bytes=%u "
                          "max_tx_time=%u max_rx_bytes=%u max_rx_time=%u\n",
                   le16toh(evdata), le16toh(evdata + 2), 
                   le16toh(evdata + 4), le16toh(evdata + 6),
                   le16toh(evdata + 8));
        break;
    case BLE_HCI_LE_SUBEV_REM_CONN_PARM_REQ:
        BLE_HS_LOG(DEBUG, "LE Remote Connection Parameter Request. handle=%u "
                          "min_itvl=%u max_itvl=%u latency=%u timeout=%u\n",
                   le16toh(evdata), le16toh(evdata + 2), 
                   le16toh(evdata + 4), le16toh(evdata + 6),
                   le16toh(evdata + 8));
        break;

    case BLE_HCI_LE_SUBEV_RD_REM_USED_FEAT:
        status = evdata[0];
        if (status == BLE_ERR_SUCCESS) {
            BLE_HS_LOG(DEBUG, "LE Remote Used Features. handle=%u feat=",
                       le16toh(evdata + 1));
            for (i = 0; i < BLE_HCI_RD_LOC_SUPP_FEAT_RSPLEN; ++i) {
                BLE_HS_LOG(DEBUG, "%02x ", evdata[3 + i]);
            }
            BLE_HS_LOG(DEBUG, "\n");
        } else {
            BLE_HS_LOG(DEBUG, "LE Remote Used Features. FAIL (status=%u)\n",
                       status);
        }
        break;

    default:
        BLE_HS_LOG(DEBUG, "\tUnknown LE event\n");
        break;
    }
}

/**
 * Display a disconnection complete command.
 * 
 * 
 * @param evdata 
 * @param len 
 */
void
host_hci_dbg_disconn_comp_disp(uint8_t *evdata, uint8_t len)
{
    uint8_t status;
    uint8_t reason;
    uint16_t handle;

    status = evdata[0];
    handle = le16toh(evdata + 1);
    /* Ignore reason if status is not success */
    if (status != BLE_ERR_SUCCESS) {
        reason = 0;
    } else {
        reason = evdata[3];
    }
    BLE_HS_LOG(DEBUG, "Disconnection Complete: status=%u handle=%u "
                      "reason=%u\n", status, handle, reason);
}

/**
 * Display a version information event 
 * 
 * @param evdata 
 * @param len 
 */
static void
host_hci_dbg_rd_rem_ver_disp(uint8_t *evdata, uint8_t len)
{
    BLE_HS_LOG(DEBUG, "Remote Version Info: status=%u handle=%u vers_nr=%u "
                      "compid=%u subver=%u\n",
               evdata[0], le16toh(evdata + 1), evdata[3],
               le16toh(evdata + 4), le16toh(evdata + 6));
}

/**
 * Display the number of completed packets event
 * 
 * @param evdata 
 * @param len 
 */
void
host_hci_dbg_num_comp_pkts_disp(uint8_t *evdata, uint8_t len)
{
    uint8_t handles;
    uint8_t *handle_ptr;
    uint8_t *pkt_ptr;
    uint16_t handle;
    uint16_t pkts;

    handles = evdata[0];
    if (len != ((handles * 4) + 1)) {
        BLE_HS_LOG(DEBUG, "ERR: Number of Completed Packets bad length: "
                          "num_handles=%u len=%u\n", handles, len);
        return;

    }

    BLE_HS_LOG(DEBUG, "Number of Completed Packets: num_handles=%u\n",
               handles);
    if (handles) {
        handle_ptr = evdata + 1;
        pkt_ptr = handle_ptr + (2 * handles);
        while (handles) {
            handle = le16toh(handle_ptr);
            handle_ptr += 2;
            pkts = le16toh(pkt_ptr);
            pkt_ptr += 2;
            BLE_HS_LOG(DEBUG, "handle:%u pkts:%u\n", handle, pkts);
            --handles;
        }
    }
}

void
host_hci_dbg_cmd_complete_disp(uint8_t *evdata, uint8_t len)
{
    uint8_t ogf;
    uint8_t ocf;
    uint16_t opcode;

    opcode = le16toh(evdata + 1);
    ogf = BLE_HCI_OGF(opcode);
    ocf = BLE_HCI_OCF(opcode);

    BLE_HS_LOG(DEBUG, "Command Complete: cmd_pkts=%u ocf=0x%x ogf=0x%x ",
               evdata[0], ocf, ogf);

    /* Display parameters based on command. */
    if (ogf == BLE_HCI_OGF_LE) {
        switch (ocf) {
        case BLE_HCI_OCF_LE_SET_ADV_DATA:
            BLE_HS_LOG(DEBUG, "status=%u", evdata[3]);
            break;
        default:
            break;
        }
    } else if (ogf == BLE_HCI_OGF_INFO_PARAMS) {
        switch (ocf) {
        case BLE_HCI_OCF_IP_RD_LOCAL_VER:
            BLE_HS_LOG(DEBUG, "status=%u ", evdata[3]);
            if (evdata[3] == BLE_ERR_SUCCESS) {
                BLE_HS_LOG(DEBUG, "hci_ver=%u hci_rev=%u lmp_ver=%u mfrg=%u "
                                  "lmp_subver=%u",
                           evdata[4], le16toh(evdata + 5), evdata[7],
                           le16toh(evdata + 8), le16toh(evdata + 10));
            }
            break;
        default:
            break;
        }
    }
    BLE_HS_LOG(DEBUG, "\n");

}

void
host_hci_dbg_cmd_status_disp(uint8_t *evdata, uint8_t len)
{
    uint8_t ogf;
    uint8_t ocf;
    uint16_t opcode;

    opcode = le16toh(evdata + 2);
    ogf = BLE_HCI_OGF(opcode);
    ocf = BLE_HCI_OCF(opcode);

    BLE_HS_LOG(DEBUG, "Command Status: status=%u cmd_pkts=%u ocf=0x%x "
                      "ogf=0x%x\n", evdata[0], evdata[1], ocf, ogf);
}

void
host_hci_dbg_event_disp(uint8_t *evbuf)
{
    uint8_t *evdata;
    uint8_t evcode;
    uint8_t len;
 
    evcode = evbuf[0];
    len = evbuf[1];
    evdata = evbuf + 2;

    switch (evcode) {
    case BLE_HCI_EVCODE_DISCONN_CMP:
        host_hci_dbg_disconn_comp_disp(evdata, len);
        break;
    case BLE_HCI_EVCODE_RD_REM_VER_INFO_CMP:
        host_hci_dbg_rd_rem_ver_disp(evdata, len);
        break;
    case BLE_HCI_EVCODE_COMMAND_COMPLETE:
        host_hci_dbg_cmd_complete_disp(evdata, len);
        break;
    case BLE_HCI_EVCODE_COMMAND_STATUS:
        host_hci_dbg_cmd_status_disp(evdata, len);
        break;
    case BLE_HCI_EVCODE_NUM_COMP_PKTS:
        host_hci_dbg_num_comp_pkts_disp(evdata, len);
        break;
    case BLE_HCI_EVCODE_LE_META:
        host_hci_dbg_le_event_disp(evdata[0], len, evdata + 1);
        break;
    default:
        BLE_HS_LOG(DEBUG, "Unknown event 0x%x len=%u\n", evcode, len);
        break;
    }
}
