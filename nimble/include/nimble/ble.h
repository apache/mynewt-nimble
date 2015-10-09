/**
 * Copyright (c) 2015 Stack Inc.
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

#ifndef H_BLE_
#define H_BLE_

/* XXX: some or all of these should not be here */
#include "os/os_mbuf.h"
extern struct os_mbuf_pool g_mbuf_pool; 

#define BLE_DEV_ADDR_LEN        (6)
extern uint8_t g_dev_addr[BLE_DEV_ADDR_LEN];

void htole16(uint8_t *buf, uint16_t x);
void htole32(uint8_t *buf, uint32_t x);
void htole64(uint8_t *buf, uint64_t x);
uint16_t le16toh(uint8_t *buf);
uint32_t le32toh(uint8_t *buf);
uint64_t le64toh(uint8_t *buf);
/* XXX */

enum ble_error_codes
{
    /* An "error" code of 0 means success */
    BLE_ERR_SUCCESS             = 0,
    BLE_ERR_UNK_HCI_CMD         = 1,
    BLE_ERR_UNK_CONN_ID         = 2,
    BLE_ERR_HW_FAIL             = 3,
    BLE_ERR_PAGE_TMO            = 4,
    BLE_ERR_AUTH_FAIL           = 5,
    BLE_ERR_PINKEY_MISSING      = 6,
    BLE_ERR_MEM_CAPACITY        = 7,
    BLE_ERR_CONN_TMO            = 8,
    BLE_ERR_CONN_LIMIT          = 9,
    BLE_ERR_SYNCH_CONN_LIMIT    = 10,
    BLE_ERR_ACL_CONN_EXISTS     = 11,
    BLE_ERR_CMD_DISALLOWED      = 12,
    BLE_ERR_CONN_REJ_RESOURCES  = 13,
    BLE_ERR_CONN_REJ_SECURITY   = 14,
    BLE_ERR_CONN_REJ_BD_ADDR    = 15,
    BLE_ERR_CONN_ACCEPT_TMO     = 16,
    BLE_ERR_UNSUPPORTED         = 17,
    BLE_ERR_INV_HCI_CMD_PARMS   = 18,
    BLE_ERR_REM_USER_CONN_TERM  = 19,
    BLE_ERR_RD_CONN_TERM_RESRCS = 20,
    BLE_ERR_RD_CONN_TERM_PWROFF = 21,
    BLE_ERR_CONN_TERM_LOCAL     = 22,
    BLE_ERR_REPEATED_ATTEMPTS   = 23,
    BLE_ERR_NO_PAIRING          = 24,
    BLE_ERR_UNK_LMP             = 25,
    BLE_ERR_UNSUPP_FEATURE      = 26,
    BLE_ERR_SCO_OFFSET          = 27,
    BLE_ERR_SCO_ITVL            = 28,
    BLE_ERR_SCO_AIR_MODE        = 29,
    BLE_ERR_INV_LMP_LL_PARM     = 30,
    BLE_ERR_UNSPECIFIED         = 31,
    BLE_ERR_UNSUPP_LMP_LL_PARM  = 32,
    BLE_ERR_NO_ROLE_CHANGE      = 33,
    BLE_ERR_LMP_LL_RSP_TMO      = 34,
    BLE_ERR_LMP_COLLISION       = 35,
    BLE_ERR_LMP_PDU             = 36,
    BLE_ERR_ENCRYPTION_MODE     = 37,
    BLE_ERR_LINK_KEY_CHANGE     = 38,
    BLE_ERR_UNSUPP_QOS          = 39,
    BLE_ERR_INSTANT_PASSED      = 40,
    BLE_ERR_UNIT_KEY_PAIRING    = 41,
    BLE_ERR_DIFF_TRANS_COLL     = 42,
    /* BLE_ERR_RESERVED         = 43 */
    BLE_ERR_QOS_PARM            = 44,
    BLE_ERR_QOS_REJECTED        = 45,
    BLE_ERR_CHAN_CLASS          = 46,
    BLE_ERR_INSUFFICIENT_SEC    = 47,
    BLE_ERR_PARM_OUT_OF_RANGE   = 48,
    /* BLE_ERR_RESERVED         = 49 */
    BLE_ERR_PENDING_ROLE_SW     = 50,
    /* BLE_ERR_RESERVED         = 51 */
    BLE_ERR_RESERVED_SLOT       = 52,
    BLE_ERR_ROLE_SW_FAIL        = 53,
    BLE_ERR_INQ_RSP_TOO_BIG     = 54,
    BLE_ERR_SEC_SIMPLE_PAIR     = 55,
    BLE_ERR_HOST_BUSY_PAIR      = 56,
    BLE_ERR_CONN_REJ_CHANNEL    = 57,
    BLE_ERR_CTRLR_BUSY          = 58,
    BLE_ERR_CONN_PARMS          = 59,
    BLE_ERR_DIR_ADV_TMO         = 60,
    BLE_ERR_CONN_TERM_MIC       = 61,
    BLE_ERR_CONN_ESTABLISHMENT  = 62,
    BLE_ERR_MAC_CONN_FAIL       = 63,
    BLE_ERR_COARSE_CLK_ADJ      = 64
};

#endif /* H_BLE_ */
