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

#ifndef __MCU_CMAC_SHARED_H_
#define __MCU_CMAC_SHARED_H_

#include <stdint.h>
#include "syscfg/syscfg.h"

#ifdef __cplusplus
extern "C" {
#endif

#define CMAC_SHARED_MAGIC_CMAC              (0x4C6C) /* "lL" */
#define CMAC_SHARED_MAGIC_SYS               (0x5368) /* "hS" */

#define CMAC_SHARED_F_SYS_LPCLK_AVAILABLE   (0x0001)

/*
 * Simple circular buffer for storing random numbers generated by M33
 *  Empty: cmr_in = cmr_out = 0;
 *  Full: cmr_in + 1 = cmr_out
 *
 *  cmr_in: used by the M33 to add random numbers to the circular buffer.
 *  cmr_out: used by CMAC to retrieve random numbers
 *
 *  NOTE: cmr_in and cmr_out are indices.
 */
#define CMAC_RAND_BUF_ELEMS                 (16)

struct cmac_rand {
    int cmr_active;
    int cmr_in;
    int cmr_out;
    uint32_t cmr_buf[CMAC_RAND_BUF_ELEMS];
};

struct cmac_mbox {
    uint16_t rd_off;
    uint16_t wr_off;
};

struct cmac_dcdc {
    uint8_t enabled;
    uint32_t v18;
    uint32_t v18p;
    uint32_t vdd;
    uint32_t v14;
    uint32_t ctrl1;
};

struct cmac_trim {
    uint8_t rfcu_len;
    uint8_t rfcu_mode1_len;
    uint8_t rfcu_mode2_len;
    uint8_t synth_len;
    uint32_t rfcu[ MYNEWT_VAL(CMAC_TRIM_SIZE_RFCU) ];
    uint32_t rfcu_mode1[2];
    uint32_t rfcu_mode2[2];
    uint32_t synth[ MYNEWT_VAL(CMAC_TRIM_SIZE_SYNTH) ];
};

#if MYNEWT_VAL(CMAC_DEBUG_DATA_ENABLE)
struct cmac_debug {
    int8_t last_rx_rssi;
    int8_t tx_power_override;

    uint32_t cal_res_1;
    uint32_t cal_res_2;
    uint32_t trim_val1_tx_1;
    uint32_t trim_val1_tx_2;
    uint32_t trim_val2_tx;
    uint32_t trim_val2_rx;
};
#endif

#if MYNEWT_VAL(CMAC_DEBUG_COREDUMP_ENABLE)
struct cmac_coredump {
    uint32_t lr;
    uint32_t pc;
    uint32_t assert;
    const char *assert_file;
    uint32_t assert_line;

    uint32_t CM_STAT_REG;
    uint32_t CM_LL_TIMER1_36_10_REG;
    uint32_t CM_LL_TIMER1_9_0_REG;
    uint32_t CM_ERROR_REG;
    uint32_t CM_EXC_STAT_REG;
};
#endif

#define CMAC_PENDING_OP_LP_CLK      0x0001
#define CMAC_PENDING_OP_RF_CAL      0x0002

struct cmac_shared_data {
    uint16_t magic_cmac;
    uint16_t magic_sys;
    uint16_t pending_ops;
    uint16_t lp_clock_freq;    /* LP clock frequency */
    uint32_t xtal32m_settle_us;/* XTAL32M settling time */
    struct cmac_mbox mbox_s2c; /* SYS2CMAC mailbox */
    struct cmac_mbox mbox_c2s; /* CMAC2SYS mailbox */
    struct cmac_dcdc dcdc;     /* DCDC settings */
    struct cmac_trim trim;     /* Trim data */
    struct cmac_rand rand;     /* Random numbers */
#if MYNEWT_VAL(CMAC_DEBUG_DATA_ENABLE)
    struct cmac_debug debug;   /* Extra debug data */
#endif
#if MYNEWT_VAL(CMAC_DEBUG_COREDUMP_ENABLE)
    struct cmac_coredump coredump;
#endif

    uint8_t mbox_s2c_buf[ MYNEWT_VAL(CMAC_MBOX_SIZE_S2C) ];
    uint8_t mbox_c2s_buf[ MYNEWT_VAL(CMAC_MBOX_SIZE_C2S) ];
};

#if MYNEWT_VAL(BLE_HOST) || MYNEWT_VAL(BLE_HCI_BRIDGE)
extern volatile struct cmac_shared_data *g_cmac_shared_data;
#elif MYNEWT_VAL(BLE_CONTROLLER)
extern volatile struct cmac_shared_data g_cmac_shared_data;
#endif

/* cmac_mbox */
typedef int (cmac_mbox_read_cb)(const void *data, uint16_t len);
typedef void (cmac_mbox_write_notif_cb)(void);
void cmac_mbox_set_read_cb(cmac_mbox_read_cb *cb);
void cmac_mbox_set_write_notif_cb(cmac_mbox_write_notif_cb *cb);
int cmac_mbox_has_data(void);
int cmac_mbox_read(void);
int cmac_mbox_write(const void *data, uint16_t len);

/* cmac_rand */
typedef void (*cmac_rand_isr_cb_t)(uint8_t rnum);
void cmac_rand_start(void);
void cmac_rand_stop(void);
void cmac_rand_read(void);
void cmac_rand_write(void);
void cmac_rand_chk_fill(void);
int cmac_rand_get_next(void);
int cmac_rand_is_active(void);
int cmac_rand_is_full(void);
void cmac_rand_fill(uint32_t *buf, int num_words);
void cmac_rand_set_isr_cb(cmac_rand_isr_cb_t cb);

void cmac_shared_init(void);
void cmac_shared_sync(void);

#if MYNEWT_VAL(BLE_HOST) || MYNEWT_VAL(BLE_HCI_BRIDGE)
#define CMAC_SHARED_LOCK_VAL    0x40000000
#elif MYNEWT_VAL(BLE_CONTROLLER)
#define CMAC_SHARED_LOCK_VAL    0xc0000000
#endif

static inline void
cmac_shared_lock(void)
{
    volatile uint32_t *bsr_set = (void *)0x50050074;
    volatile uint32_t *bsr_stat = (void *)0x5005007c;

    while ((*bsr_stat & 0xc0000000) != CMAC_SHARED_LOCK_VAL) {
        *bsr_set = CMAC_SHARED_LOCK_VAL;
    }
}

static inline void
cmac_shared_unlock(void)
{
    volatile uint32_t *bsr_reset = (void *)0x50050078;

    *bsr_reset = CMAC_SHARED_LOCK_VAL;
}

#ifdef __cplusplus
}
#endif

#endif /* __MCU_CMAC_SHARED_H_ */
