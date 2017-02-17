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

#ifndef H_BLE_
#define H_BLE_

#include <inttypes.h>
#include <string.h>

#ifdef __cplusplus
extern "C" {
#endif

/* XXX: some or all of these should not be here */
#include "os/os.h"
#include "syscfg/syscfg.h"

/* BLE encryption block definitions */
#define BLE_ENC_BLOCK_SIZE       (16)

struct ble_encryption_block
{
    uint8_t     key[BLE_ENC_BLOCK_SIZE];
    uint8_t     plain_text[BLE_ENC_BLOCK_SIZE];
    uint8_t     cipher_text[BLE_ENC_BLOCK_SIZE];
};

/*
 * BLE MBUF structure:
 *
 * The BLE mbuf structure is as follows. Note that this structure applies to
 * the packet header mbuf (not mbufs that are part of a "packet chain"):
 *      struct os_mbuf          (16)
 *      struct os_mbuf_pkthdr   (8)
 *      struct ble_mbuf_hdr     (8)
 *      Data buffer             (payload size, in bytes)
 *
 * The BLE mbuf header contains the following:
 *  flags: bitfield with the following values
 *      0x01:   Set if there was a match on the whitelist
 *      0x02:   Set if a connect request was transmitted upon receiving pdu
 *      0x04:   Set the first time we transmit the PDU (used to detect retry).
 *  channel: The logical BLE channel PHY channel # (0 - 39)
 *  crcok: flag denoting CRC check passed (1) or failed (0).
 *  rssi: RSSI, in dBm.
 */
struct ble_mbuf_hdr_rxinfo
{
    uint8_t flags;
    uint8_t channel;
    uint8_t handle;
    int8_t  rssi;
#if MYNEWT_VAL(BLE_MULTI_ADV_SUPPORT)
    void *advsm;   /* advertising state machine */
#endif
};

/* Flag definitions for rxinfo  */
#define BLE_MBUF_HDR_F_CRC_OK           (0x80)
#define BLE_MBUF_HDR_F_DEVMATCH         (0x40)
#define BLE_MBUF_HDR_F_MIC_FAILURE      (0x20)
#define BLE_MBUF_HDR_F_SCAN_RSP_TXD     (0x10)
#define BLE_MBUF_HDR_F_SCAN_RSP_CHK     (0x08)
#define BLE_MBUF_HDR_F_RESOLVED         (0x04)
#define BLE_MBUF_HDR_F_RXSTATE_MASK     (0x03)

/* Transmit info. NOTE: no flags defined */
struct ble_mbuf_hdr_txinfo
{
    uint8_t flags;
    uint8_t offset;
    uint8_t pyld_len;
    uint8_t hdr_byte;
};

struct ble_mbuf_hdr
{
    union {
        struct ble_mbuf_hdr_rxinfo rxinfo;
        struct ble_mbuf_hdr_txinfo txinfo;
    };
    uint32_t beg_cputime;
#if (MYNEWT_VAL(OS_CPUTIME_FREQ) == 32768)
    uint32_t rem_usecs;
#endif
};

#define BLE_MBUF_HDR_CRC_OK(hdr)        \
    ((hdr)->rxinfo.flags & BLE_MBUF_HDR_F_CRC_OK)

#define BLE_MBUF_HDR_MIC_FAILURE(hdr)   \
    ((hdr)->rxinfo.flags & BLE_MBUF_HDR_F_MIC_FAILURE)

#define BLE_MBUF_HDR_RESOLVED(hdr)      \
    ((hdr)->rxinfo.flags & BLE_MBUF_HDR_F_RESOLVED)

#define BLE_MBUF_HDR_RX_STATE(hdr)      \
    ((hdr)->rxinfo.flags & BLE_MBUF_HDR_F_RXSTATE_MASK)

#define BLE_MBUF_HDR_PTR(om)            \
    (struct ble_mbuf_hdr *)((uint8_t *)om + sizeof(struct os_mbuf) + \
                            sizeof(struct os_mbuf_pkthdr))

/* BLE mbuf overhead per packet header mbuf */
#define BLE_MBUF_PKTHDR_OVERHEAD        \
    (sizeof(struct os_mbuf_pkthdr) + sizeof(struct ble_mbuf_hdr))

#define BLE_MBUF_MEMBLOCK_OVERHEAD      \
    (sizeof(struct os_mbuf) + BLE_MBUF_PKTHDR_OVERHEAD)

#define BLE_DEV_ADDR_LEN        (6)
extern uint8_t g_dev_addr[BLE_DEV_ADDR_LEN];
extern uint8_t g_random_addr[BLE_DEV_ADDR_LEN];

/* BLE Error Codes (Core v4.2 Vol 2 part D) */
enum ble_error_codes
{
    /* An "error" code of 0 means success */
    BLE_ERR_SUCCESS             = 0,
    BLE_ERR_UNKNOWN_HCI_CMD     = 1,
    BLE_ERR_UNK_CONN_ID         = 2,
    BLE_ERR_HW_FAIL             = 3,
    BLE_ERR_PAGE_TMO            = 4,
    BLE_ERR_AUTH_FAIL           = 5,
    BLE_ERR_PINKEY_MISSING      = 6,
    BLE_ERR_MEM_CAPACITY        = 7,
    BLE_ERR_CONN_SPVN_TMO       = 8,
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
    BLE_ERR_UNSUPP_REM_FEATURE  = 26,
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
    BLE_ERR_CTLR_BUSY           = 58,
    BLE_ERR_CONN_PARMS          = 59,
    BLE_ERR_DIR_ADV_TMO         = 60,
    BLE_ERR_CONN_TERM_MIC       = 61,
    BLE_ERR_CONN_ESTABLISHMENT  = 62,
    BLE_ERR_MAC_CONN_FAIL       = 63,
    BLE_ERR_COARSE_CLK_ADJ      = 64,
    BLE_ERR_MAX                 = 255
};

int ble_err_from_os(int os_err);

/* HW error codes */
#define BLE_HW_ERR_DO_NOT_USE           (0) /* XXX: reserve this one for now */
#define BLE_HW_ERR_HCI_SYNC_LOSS        (1)

/* Own Bluetooth Device address type */
#define BLE_OWN_ADDR_PUBLIC                  (0x00)
#define BLE_OWN_ADDR_RANDOM                  (0x01)
#define BLE_OWN_ADDR_RPA_PUBLIC_DEFAULT      (0x02)
#define BLE_OWN_ADDR_RPA_RANDOM_DEFAULT      (0x03)

/* Bluetooth Device address type */
#define BLE_ADDR_PUBLIC      (0x00)
#define BLE_ADDR_RANDOM      (0x01)
#define BLE_ADDR_PUBLIC_ID   (0x02)
#define BLE_ADDR_RANDOM_ID   (0x03)

#define BLE_ADDR_ANY (&(ble_addr_t) { 0, {0, 0, 0, 0, 0, 0} })

#define BLE_ADDR_IS_RPA(addr)     (((addr)->type == BLE_ADDR_RANDOM) && \
                                   ((addr)->val[5] & 0xc0) == 0x40)
#define BLE_ADDR_IS_NRPA(addr)    (((addr)->type == BLE_ADDR_RANDOM) && \
                                   (((addr)->val[5] & 0xc0) == 0x00)
#define BLE_ADDR_IS_STATIC(addr)  (((addr)->type == BLE_ADDR_RANDOM) && \
                                   (((addr)->val[5] & 0xc0) == 0xc0)

typedef struct {
    uint8_t type;
    uint8_t val[6];
} ble_addr_t;


static inline int ble_addr_cmp(const ble_addr_t *a, const ble_addr_t *b)
{
    return memcmp(a, b, sizeof(*a));
}

#ifdef __cplusplus
}
#endif

#endif /* H_BLE_ */
