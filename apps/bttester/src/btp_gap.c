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

/* gap.c - Bluetooth GAP Tester */

/*
 * Copyright (c) 2015-2016 Intel Corporation
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include "host/ble_gap.h"
#include "host/util/util.h"
#include "console/console.h"

#include "../../../nimble/host/src/ble_hs_pvcy_priv.h"
#include "../../../nimble/host/src/ble_hs_hci_priv.h"
#include "../../../nimble/host/src/ble_sm_priv.h"

#include "btp/btp.h"

#include <errno.h>

#define CONTROLLER_INDEX 0
#define CONTROLLER_NAME "btp_tester"

#define BLE_AD_DISCOV_MASK (BLE_HS_ADV_F_DISC_LTD | BLE_HS_ADV_F_DISC_GEN)
#define ADV_BUF_LEN (sizeof(struct btp_gap_device_found_ev) + 2 * 31)

/* parameter values to reject in CPUP if all match the pattern */
#define REJECT_INTERVAL_MIN 0x0C80
#define REJECT_INTERVAL_MAX 0x0C80
#define REJECT_LATENCY 0x0000
#define REJECT_SUPERVISION_TIMEOUT 0x0C80

const uint8_t irk[16] = {
    0x11, 0x11, 0x11, 0x11, 0x11, 0x11, 0x11, 0x11,
    0x11, 0x11, 0x11, 0x11, 0x11, 0x11, 0x11, 0x11,
};

static uint8_t oob[16];
static struct ble_sm_sc_oob_data oob_data_local;
static struct ble_sm_sc_oob_data oob_data_remote;

static uint16_t current_settings;
uint8_t own_addr_type;
static ble_addr_t peer_id_addr;
static ble_addr_t peer_ota_addr;
static bool encrypted = false;

static struct os_callout                    update_params_co;
static struct btp_gap_conn_param_update_cmd update_params;

static struct os_callout                  connected_ev_co;
static struct btp_gap_device_connected_ev connected_ev;
#define CONNECTED_EV_DELAY_MS(itvl) 8 * BLE_HCI_CONN_ITVL * itvl / 1000
static int connection_attempts;
#if MYNEWT_VAL(BTTESTER_PRIVACY_MODE) && MYNEWT_VAL(BTTESTER_USE_NRPA)
static int64_t advertising_start;
static struct os_callout bttester_nrpa_rotate_timer;
#endif

static const struct ble_gap_conn_params dflt_conn_params = {
    .scan_itvl = 0x0010,
    .scan_window = 0x0010,
    .itvl_min = BLE_GAP_INITIAL_CONN_ITVL_MIN,
    .itvl_max = BLE_GAP_INITIAL_CONN_ITVL_MAX,
    .latency = 0,
    .supervision_timeout = 0x0100,
    .min_ce_len = 0x0010,
    .max_ce_len = 0x0300,
};

static int
gap_conn_find_by_addr(const ble_addr_t *dev_addr,
                      struct ble_gap_conn_desc *out_desc)
{
    ble_addr_t addr = *dev_addr;

    if (memcmp(BLE_ADDR_ANY, &peer_id_addr, 6) == 0) {
        return ble_gap_conn_find_by_addr(&addr, out_desc);
    }

    if (BLE_ADDR_IS_RPA(&addr)) {
        if (ble_addr_cmp(&peer_ota_addr, &addr) != 0) {
            return -1;
        }

        return ble_gap_conn_find_by_addr(&addr, out_desc);
    } else {
        if (ble_addr_cmp(&peer_id_addr, &addr) != 0) {
            return -1;
        }

        if (BLE_ADDR_IS_RPA(&peer_ota_addr)) {
            /* Change addr type to ID addr */
            addr.type |= 2;
        }

        return ble_gap_conn_find_by_addr(&addr, out_desc);
    }
}

static int
gap_event_cb(struct ble_gap_event *event, void *arg);

static uint8_t
supported_commands(const void *cmd, uint16_t cmd_len,
                   void *rsp, uint16_t *rsp_len)
{
    struct btp_gap_read_supported_commands_rp *rp = rsp;

    /* octet 0 */
    tester_set_bit(rp->data, BTP_GAP_READ_SUPPORTED_COMMANDS);
    tester_set_bit(rp->data, BTP_GAP_READ_CONTROLLER_INDEX_LIST);
    tester_set_bit(rp->data, BTP_GAP_READ_CONTROLLER_INFO);
    tester_set_bit(rp->data, BTP_GAP_SET_CONNECTABLE);

    /* octet 1 */
    tester_set_bit(rp->data, BTP_GAP_SET_DISCOVERABLE);
    tester_set_bit(rp->data, BTP_GAP_SET_BONDABLE);
    tester_set_bit(rp->data, BTP_GAP_START_ADVERTISING);
    tester_set_bit(rp->data, BTP_GAP_STOP_ADVERTISING);
    tester_set_bit(rp->data, BTP_GAP_START_DISCOVERY);
    tester_set_bit(rp->data, BTP_GAP_STOP_DISCOVERY);
    tester_set_bit(rp->data, BTP_GAP_CONNECT);
    tester_set_bit(rp->data, BTP_GAP_DISCONNECT);

    /* octet 2 */
    tester_set_bit(rp->data, BTP_GAP_SET_IO_CAP);
    tester_set_bit(rp->data, BTP_GAP_PAIR);
    tester_set_bit(rp->data, BTP_GAP_UNPAIR);
    tester_set_bit(rp->data, BTP_GAP_PASSKEY_ENTRY);
    tester_set_bit(rp->data, BTP_GAP_PASSKEY_CONFIRM);
    tester_set_bit(rp->data, BTP_GAP_START_DIRECT_ADV);
    tester_set_bit(rp->data, BTP_GAP_CONN_PARAM_UPDATE);

    /* octet 3 */
    tester_set_bit(rp->data, BTP_GAP_OOB_LEGACY_SET_DATA);
    tester_set_bit(rp->data, BTP_GAP_OOB_SC_GET_LOCAL_DATA);
    tester_set_bit(rp->data, BTP_GAP_OOB_SC_SET_REMOTE_DATA);
    tester_set_bit(rp->data, BTP_GAP_SET_MITM);

    *rsp_len = sizeof(*rp) + 4;

    return BTP_STATUS_SUCCESS;
}

static uint8_t
controller_index_list(const void *cmd, uint16_t cmd_len,
                      void *rsp, uint16_t *rsp_len)
{
    struct btp_gap_read_controller_index_list_rp *rp = rsp;

    SYS_LOG_DBG("");

    rp->num = 1;
    rp->index[0] = CONTROLLER_INDEX;

    *rsp_len = sizeof(*rp) + 1;

    return BTP_STATUS_SUCCESS;
}

static uint8_t
controller_info(const void *cmd, uint16_t cmd_len,
                void *rsp, uint16_t *rsp_len)
{
    struct btp_gap_read_controller_info_rp *rp = rsp;
    uint32_t supported_settings = 0;
    ble_addr_t addr;
    int rc;

    SYS_LOG_DBG("");

    rc = ble_hs_pvcy_set_our_irk(irk);
    assert(rc == 0);

    /* Make sure we have proper identity address set (public preferred) */
    rc = ble_hs_util_ensure_addr(1);
    assert(rc == 0);
    rc = ble_hs_id_copy_addr(BLE_ADDR_RANDOM, addr.val, NULL);
    assert(rc == 0);

    if (MYNEWT_VAL(BTTESTER_PRIVACY_MODE)) {
        if (MYNEWT_VAL(BTTESTER_USE_NRPA)) {
            own_addr_type = BLE_OWN_ADDR_RANDOM;
            rc = ble_hs_id_gen_rnd(1, &addr);
            assert(rc == 0);
            rc = ble_hs_id_set_rnd(addr.val);
            assert(rc == 0);
        } else {
            own_addr_type = BLE_OWN_ADDR_RPA_RANDOM_DEFAULT;
        }
        current_settings |= BIT(BTP_GAP_SETTINGS_PRIVACY);
        supported_settings |= BIT(BTP_GAP_SETTINGS_PRIVACY);
        memcpy(&rp->address, &addr, sizeof(rp->address));
    } else {
        rc = ble_hs_id_copy_addr(BLE_ADDR_PUBLIC, rp->address, NULL);
        if (rc) {
            own_addr_type = BLE_OWN_ADDR_RANDOM;
            memcpy(rp->address, addr.val, sizeof(rp->address));
            supported_settings |= BIT(BTP_GAP_SETTINGS_STATIC_ADDRESS);
            current_settings |= BIT(BTP_GAP_SETTINGS_STATIC_ADDRESS);
        } else {
            own_addr_type = BLE_OWN_ADDR_PUBLIC;
        }
    }

    supported_settings |= BIT(BTP_GAP_SETTINGS_POWERED);
    supported_settings |= BIT(BTP_GAP_SETTINGS_CONNECTABLE);
    supported_settings |= BIT(BTP_GAP_SETTINGS_BONDABLE);
    supported_settings |= BIT(BTP_GAP_SETTINGS_LE);
    supported_settings |= BIT(BTP_GAP_SETTINGS_ADVERTISING);
    supported_settings |= BIT(BTP_GAP_SETTINGS_SC);

    if (ble_hs_cfg.sm_bonding) {
        current_settings |= BIT(BTP_GAP_SETTINGS_BONDABLE);
    }
    if (ble_hs_cfg.sm_sc) {
        current_settings |= BIT(BTP_GAP_SETTINGS_SC);
    }

    rp->supported_settings = htole32(supported_settings);
    rp->current_settings = htole32(current_settings);

    memcpy(rp->name, CONTROLLER_NAME, sizeof(CONTROLLER_NAME));

    *rsp_len = sizeof(*rp);

    return BTP_STATUS_SUCCESS;
}

static struct ble_gap_adv_params adv_params = {
    .conn_mode = BLE_GAP_CONN_MODE_NON,
    .disc_mode = BLE_GAP_DISC_MODE_NON,
};

#if MYNEWT_VAL(BTTESTER_PRIVACY_MODE) && MYNEWT_VAL(BTTESTER_USE_NRPA)
static void rotate_nrpa_cb(struct os_event *ev)
{
    int rc;
    ble_addr_t addr;
    int32_t duration_ms = BLE_HS_FOREVER;
    int32_t remaining_time;
    os_time_t remaining_ticks;

    if (adv_params.disc_mode == BLE_GAP_DISC_MODE_LTD) {
        duration_ms = MYNEWT_VAL(BTTESTER_LTD_ADV_TIMEOUT);
    }

    ble_gap_adv_stop();
    rc = ble_hs_id_gen_rnd(1, &addr);
    assert(rc == 0);
    rc = ble_hs_id_set_rnd(addr.val);
    assert(rc == 0);

    ble_gap_adv_start(own_addr_type, NULL, duration_ms,
                      &adv_params, gap_event_cb, NULL);

    remaining_time = os_get_uptime_usec() - advertising_start;
    if (remaining_time > 0) {
        advertising_start = os_get_uptime_usec();
        os_time_ms_to_ticks(remaining_time, &remaining_ticks);
        os_callout_reset(&bttester_nrpa_rotate_timer,
                         remaining_ticks);
    }
}
#endif

static uint8_t
set_connectable(const void *cmd, uint16_t cmd_len,
                void *rsp, uint16_t *rsp_len)
{
    const struct btp_gap_set_connectable_cmd *cp = cmd;
    struct btp_gap_set_connectable_rp *rp = rsp;

    SYS_LOG_DBG("");

    if (cp->connectable) {
        current_settings |= BIT(BTP_GAP_SETTINGS_CONNECTABLE);
        adv_params.conn_mode = BLE_GAP_CONN_MODE_UND;
    } else {
        current_settings &= ~BIT(BTP_GAP_SETTINGS_CONNECTABLE);
        adv_params.conn_mode = BLE_GAP_CONN_MODE_NON;
    }

    rp->current_settings = htole32(current_settings);

    *rsp_len = sizeof(*rp);

    return BTP_STATUS_SUCCESS;
}

static uint8_t ad_flags = BLE_HS_ADV_F_BREDR_UNSUP;

static uint8_t
set_discoverable(const void *cmd, uint16_t cmd_len,
                 void *rsp, uint16_t *rsp_len)
{
    const struct btp_gap_set_discoverable_cmd *cp = cmd;
    struct btp_gap_set_discoverable_rp *rp = rsp;

    SYS_LOG_DBG("");

    switch (cp->discoverable) {
    case BTP_GAP_NON_DISCOVERABLE:
        ad_flags &= ~(BLE_HS_ADV_F_DISC_GEN | BLE_HS_ADV_F_DISC_LTD);
        adv_params.disc_mode = BLE_GAP_DISC_MODE_NON;
        current_settings &= ~BIT(BTP_GAP_SETTINGS_DISCOVERABLE);
        break;
    case BTP_GAP_GENERAL_DISCOVERABLE:
        ad_flags &= ~BLE_HS_ADV_F_DISC_LTD;
        ad_flags |= BLE_HS_ADV_F_DISC_GEN;
        adv_params.disc_mode = BLE_GAP_DISC_MODE_GEN;
        current_settings |= BIT(BTP_GAP_SETTINGS_DISCOVERABLE);
        break;
    case BTP_GAP_LIMITED_DISCOVERABLE:
        ad_flags &= ~BLE_HS_ADV_F_DISC_GEN;
        ad_flags |= BLE_HS_ADV_F_DISC_LTD;
        adv_params.disc_mode = BLE_GAP_DISC_MODE_LTD;
        current_settings |= BIT(BTP_GAP_SETTINGS_DISCOVERABLE);
        break;
    default:
        return BTP_STATUS_FAILED;
    }

    rp->current_settings = htole32(current_settings);

    *rsp_len = sizeof(*rp);

    return BTP_STATUS_SUCCESS;
}

static uint8_t
set_bondable(const void *cmd, uint16_t cmd_len,
             void *rsp, uint16_t *rsp_len)
{
    const struct btp_gap_set_bondable_cmd *cp = cmd;
    struct btp_gap_set_bondable_rp *rp = rsp;

    SYS_LOG_DBG("bondable: %d", cp->bondable);

    ble_hs_cfg.sm_bonding = cp->bondable;
    if (ble_hs_cfg.sm_bonding) {
        current_settings |= BIT(BTP_GAP_SETTINGS_BONDABLE);
    } else {
        current_settings &= ~BIT(BTP_GAP_SETTINGS_BONDABLE);
    }

    rp->current_settings = htole32(current_settings);
    *rsp_len = sizeof(*rp);
    return BTP_STATUS_SUCCESS;
}

static struct adv_data ad[10] = {
    ADV_DATA(BLE_HS_ADV_TYPE_FLAGS, &ad_flags, sizeof(ad_flags)),
};
static struct adv_data sd[10];

static int
set_ad(const struct adv_data *ad_data, size_t ad_len,
       uint8_t *buf, uint8_t *buf_len)
{
    int i;

    for (i = 0; i < ad_len; i++) {
        buf[(*buf_len)++] = ad_data[i].data_len + 1;
        buf[(*buf_len)++] = ad_data[i].type;

        memcpy(&buf[*buf_len], ad_data[i].data,
               ad_data[i].data_len);
        *buf_len += ad_data[i].data_len;
    }

    return 0;
}

static uint8_t
start_advertising(const void *cmd, uint16_t cmd_len,
                  void *rsp, uint16_t *rsp_len)
{
    const struct btp_gap_start_advertising_cmd *cp = cmd;
    struct btp_gap_start_advertising_rp *rp = rsp;
    int32_t duration_ms = BLE_HS_FOREVER;
    uint8_t buf[BLE_HS_ADV_MAX_SZ];
    uint8_t buf_len = 0;
    uint8_t adv_len, sd_len;
    uint8_t addr_type;
    uint32_t duration;

    int err;

    int i;

    SYS_LOG_DBG("");

    /* This command is very unfortunate since after variable data there is
     * additional 5 bytes (4 bytes for duration, 1 byte for own address
     * type.
     */
    if ((cmd_len < sizeof(*cp)) ||
        (cmd_len != sizeof(*cp) + cp->adv_data_len + cp->scan_rsp_len +
                    sizeof(duration) + sizeof(own_addr_type))) {
        return BTP_STATUS_FAILED;
    }

    /* currently ignored */
    duration = get_le32(cp->adv_data + cp->adv_data_len + cp->scan_rsp_len);
    (void)duration;
    addr_type = cp->adv_data[cp->adv_data_len +
                             cp->scan_rsp_len +
                             sizeof(duration)];

    for (i = 0, adv_len = 1U; i < cp->adv_data_len; adv_len++) {
        if (adv_len >= ARRAY_SIZE(ad)) {
            SYS_LOG_ERR("ad[] Out of memory");
            return BTP_STATUS_FAILED;
        }

        ad[adv_len].type = cp->scan_rsp[i++];
        ad[adv_len].data_len = cp->scan_rsp[i++];
        ad[adv_len].data = &cp->scan_rsp[i];
        i += ad[adv_len].data_len;
    }

    for (sd_len = 0U; i < cp->scan_rsp_len; sd_len++) {
        if (sd_len >= ARRAY_SIZE(sd)) {
            SYS_LOG_ERR("sd[] Out of memory");
            return BTP_STATUS_FAILED;
        }

        sd[sd_len].type = cp->scan_rsp[i++];
        sd[sd_len].data_len = cp->scan_rsp[i++];
        sd[sd_len].data = &cp->scan_rsp[i];
        i += sd[sd_len].data_len;
    }

    err = set_ad(ad, adv_len, buf, &buf_len);
    if (err) {
        return BTP_STATUS_FAILED;
    }

    err = ble_gap_adv_set_data(buf, buf_len);
    if (err != 0) {
        return BTP_STATUS_FAILED;
    }

    if (sd_len) {
        buf_len = 0;

        err = set_ad(sd, sd_len, buf, &buf_len);
        if (err) {
            SYS_LOG_ERR("Advertising failed: err %d", err);
            return BTP_STATUS_FAILED;
        }

        err = ble_gap_adv_rsp_set_data(buf, buf_len);
        if (err != 0) {
            SYS_LOG_ERR("Advertising failed: err %d", err);
            return BTP_STATUS_FAILED;
        }
    }

    if (adv_params.disc_mode == BLE_GAP_DISC_MODE_LTD) {
        duration_ms = MYNEWT_VAL(BTTESTER_LTD_ADV_TIMEOUT);
    }

    /* In NimBLE, own_addr_type is configured in `controller_info` function.
     * Let's just verify restrictions for Privacy options.
     */
    switch (addr_type) {
    case 0x00:
        break;
#if defined(CONFIG_BT_PRIVACY)
    case 0x01:
		/* RPA usage is is controlled via privacy settings */
		if (!atomic_test_bit(&current_settings, BTP_GAP_SETTINGS_PRIVACY)) {
			return BTP_STATUS_FAILED;
		}
		break;
	case 0x02:
		/* NRPA is used only for non-connectable advertising */
		if (atomic_test_bit(&current_settings, BTP_GAP_SETTINGS_CONNECTABLE)) {
			return BTP_STATUS_FAILED;
		}
		break;
#endif
    default:
        return BTP_STATUS_FAILED;
    }

#if MYNEWT_VAL(BTTESTER_PRIVACY_MODE) && MYNEWT_VAL(BTTESTER_USE_NRPA)
    if (MYNEWT_VAL(BTTESTER_NRPA_TIMEOUT) < duration_ms / 1000) {
        advertising_start = os_get_uptime_usec();
        os_callout_reset(&bttester_nrpa_rotate_timer,
                         OS_TICKS_PER_SEC * MYNEWT_VAL(BTTESTER_NRPA_TIMEOUT));
    }
#endif
    err = ble_gap_adv_start(own_addr_type, NULL, duration_ms,
                            &adv_params, gap_event_cb, NULL);
    if (err) {
        SYS_LOG_ERR("Advertising failed: err %d", err);
        return BTP_STATUS_FAILED;
    }

    current_settings |= BIT(BTP_GAP_SETTINGS_ADVERTISING);
    rp->current_settings = htole32(current_settings);

    *rsp_len = sizeof(*rp);

    return BTP_STATUS_SUCCESS;
}

static uint8_t
stop_advertising(const void *cmd, uint16_t cmd_len,
                 void *rsp, uint16_t *rsp_len)
{
    struct btp_gap_stop_advertising_rp *rp = rsp;
    int err;

    SYS_LOG_DBG("");

    err = ble_gap_adv_stop();
    if (err != 0) {
        return BTP_STATUS_FAILED;
    }

    current_settings &= ~BIT(BTP_GAP_SETTINGS_ADVERTISING);
    rp->current_settings = htole32(current_settings);

    *rsp_len = sizeof(*rp);

    return BTP_STATUS_SUCCESS;
}

static uint8_t
get_ad_flags(const uint8_t *data, uint8_t data_len)
{
    uint8_t len, i;

    /* Parse advertisement to get flags */
    for (i = 0; i < data_len; i += len - 1) {
        len = data[i++];
        if (!len) {
            break;
        }

        /* Check if field length is correct */
        if (len > (data_len - i) || (data_len - i) < 1) {
            break;
        }

        switch (data[i++]) {
        case BLE_HS_ADV_TYPE_FLAGS:
            return data[i];
        default:
            break;
        }
    }

    return 0;
}

static uint8_t discovery_flags;
static struct os_mbuf *adv_buf;

static void
store_adv(const ble_addr_t *addr, int8_t rssi,
          const uint8_t *data, uint8_t len)
{
    struct btp_gap_device_found_ev *ev;
    void *adv_data;
    /* cleanup */
    tester_mbuf_reset(adv_buf);

    ev = os_mbuf_extend(adv_buf, sizeof(*ev));
    if (!ev) {
        return;
    }

    memcpy(&ev->address, addr, sizeof(ev->address));
    ev->rssi = rssi;
    ev->flags = BTP_GAP_DEVICE_FOUND_FLAG_AD | BTP_GAP_DEVICE_FOUND_FLAG_RSSI;
    ev->eir_data_len = len;

    adv_data = os_mbuf_extend(adv_buf, len);
    if (!adv_data) {
        return;
    }

    memcpy(adv_data, data, len);
}

static void
device_found(ble_addr_t *addr, int8_t rssi, uint8_t evtype,
             const uint8_t *data, uint8_t len)
{
    struct btp_gap_device_found_ev *ev;
    void *adv_data;
    ble_addr_t a;

    /* if General/Limited Discovery - parse Advertising data to get flags */
    if (!(discovery_flags & BTP_GAP_DISCOVERY_FLAG_LE_OBSERVE) &&
        (evtype != BLE_HCI_ADV_RPT_EVTYPE_SCAN_RSP)) {
        uint8_t flags = get_ad_flags(data, len);

        /* ignore non-discoverable devices */
        if (!(flags & BLE_AD_DISCOV_MASK)) {
            SYS_LOG_DBG("Non discoverable, skipping");
            return;
        }

        /* if Limited Discovery - ignore general discoverable devices */
        if ((discovery_flags & BTP_GAP_DISCOVERY_FLAG_LIMITED) &&
            !(flags & BLE_HS_ADV_F_DISC_LTD)) {
            SYS_LOG_DBG("General discoverable, skipping");
            return;
        }
    }

    /* attach Scan Response data */
    if (evtype == BLE_HCI_ADV_RPT_EVTYPE_SCAN_RSP) {
        /* skip if there is no pending advertisement */
        if (!adv_buf->om_len) {
            SYS_LOG_INF("No pending advertisement, skipping");
            return;
        }

        ev = (void *) adv_buf->om_data;
        memcpy(&a, &ev->address, sizeof(a));

        /*
         * in general, the Scan Response comes right after the
         * Advertisement, but if not if send stored event and ignore
         * this one
         */
        if (ble_addr_cmp(addr, &a)) {
            SYS_LOG_INF("Address does not match, skipping");
            goto done;
        }

        ev->eir_data_len += len;
        ev->flags |= BTP_GAP_DEVICE_FOUND_FLAG_SD;

        adv_data = os_mbuf_extend(adv_buf, len);
        if (!adv_data) {
            return;
        }

        memcpy(adv_data, data, len);

        goto done;
    }

    /*
     * if there is another pending advertisement, send it and store the
     * current one
     */
    if (adv_buf->om_len) {
        tester_event(BTP_SERVICE_ID_GAP, BTP_GAP_EV_DEVICE_FOUND,
                     adv_buf->om_data, adv_buf->om_len);
    }

    store_adv(addr, rssi, data, len);

    /* if Active Scan and scannable event - wait for Scan Response */
    if ((discovery_flags & BTP_GAP_DISCOVERY_FLAG_LE_ACTIVE_SCAN) &&
        (evtype == BLE_HCI_ADV_RPT_EVTYPE_ADV_IND ||
         evtype == BLE_HCI_ADV_RPT_EVTYPE_SCAN_IND)) {
        SYS_LOG_DBG("Waiting for scan response");
        return;
    }
done:
    tester_event(BTP_SERVICE_ID_GAP, BTP_GAP_EV_DEVICE_FOUND,
                 adv_buf->om_data, adv_buf->om_len);
}

static int
discovery_cb(struct ble_gap_event *event, void *arg)
{
    if (event->type == BLE_GAP_EVENT_DISC) {
        device_found(&event->disc.addr, event->disc.rssi,
                     event->disc.event_type, event->disc.data,
                     event->disc.length_data);
    }

    return 0;
}

static uint8_t
start_discovery(const void *cmd, uint16_t cmd_len,
                void *rsp, uint16_t *rsp_len)
{
    const struct btp_gap_start_discovery_cmd *cp = cmd;
    struct ble_gap_disc_params params = {0};

    SYS_LOG_DBG("");

    /* only LE scan is supported */
    if (cp->flags & BTP_GAP_DISCOVERY_FLAG_BREDR) {
        return BTP_STATUS_FAILED;
    }

    params.passive = (cp->flags & BTP_GAP_DISCOVERY_FLAG_LE_ACTIVE_SCAN) == 0;
    params.limited = (cp->flags & BTP_GAP_DISCOVERY_FLAG_LIMITED) > 0;
    params.filter_duplicates = 1;

    if (ble_gap_disc(own_addr_type, BLE_HS_FOREVER,
                     &params, discovery_cb, NULL) != 0) {
        return BTP_STATUS_FAILED;
    }

    tester_mbuf_reset(adv_buf);
    discovery_flags = cp->flags;

    return BTP_STATUS_SUCCESS;
}

static uint8_t
stop_discovery(const void *cmd, uint16_t cmd_len,
               void *rsp, uint16_t *rsp_len)
{
    SYS_LOG_DBG("");

    if (ble_gap_disc_cancel() != 0) {
        return BTP_STATUS_FAILED;
    }

    return BTP_STATUS_SUCCESS;
}

/* Bluetooth Core Spec v5.1 | Section 10.7.1
 * If a privacy-enabled Peripheral, that has a stored bond,
 * receives a resolvable private address, the Host may resolve
 * the resolvable private address [...]
 * If the resolution is successful, the Host may accept the connection.
 * If the resolution procedure fails, then the Host shall disconnect
 * with the error code "Authentication failure" [...]
 */
static void
periph_privacy(struct ble_gap_conn_desc desc)
{
#if !MYNEWT_VAL(BTTESTER_PRIVACY_MODE)
    return;
#endif
    int count;

    SYS_LOG_DBG("");

    ble_store_util_count(BLE_STORE_OBJ_TYPE_PEER_SEC, &count);
    if (count > 0 && BLE_ADDR_IS_RPA(&desc.peer_id_addr)) {
        SYS_LOG_DBG("Authentication failure, disconnecting");
        ble_gap_terminate(desc.conn_handle, BLE_ERR_AUTH_FAIL);
    }
}

static void
device_connected_ev_send(struct os_event *ev)
{
    struct ble_gap_conn_desc desc;
    int rc;

    SYS_LOG_DBG("");

    rc = gap_conn_find_by_addr((ble_addr_t *) &connected_ev, &desc);
    if (rc) {
        tester_rsp(BTP_SERVICE_ID_GAP, BTP_GAP_EV_DEVICE_CONNECTED,
                   BTP_STATUS_FAILED);
        return;
    }

    tester_event(BTP_SERVICE_ID_GAP, BTP_GAP_EV_DEVICE_CONNECTED,
                 (uint8_t *) &connected_ev, sizeof(connected_ev));

    periph_privacy(desc);
}

static void
le_connected(uint16_t conn_handle, int status)
{
    struct ble_gap_conn_desc desc;
    ble_addr_t *addr;
    int rc;

    SYS_LOG_DBG("");

    if (status != 0) {
        return;
    }

    rc = ble_gap_conn_find(conn_handle, &desc);
    if (rc) {
        return;
    }

    peer_id_addr = desc.peer_id_addr;
    peer_ota_addr = desc.peer_ota_addr;

    addr = &desc.peer_id_addr;

    memcpy(&connected_ev.address, addr, sizeof(connected_ev.address));
    connected_ev.conn_itvl = desc.conn_itvl;
    connected_ev.conn_latency = desc.conn_latency;
    connected_ev.supervision_timeout = desc.supervision_timeout;

#if MYNEWT_VAL(BTTESTER_CONN_RETRY)
    os_callout_reset(&connected_ev_co,
             os_time_ms_to_ticks32(
                 CONNECTED_EV_DELAY_MS(desc.conn_itvl)));
#else
    tester_event(BTP_SERVICE_ID_GAP, BTP_GAP_EV_DEVICE_CONNECTED,
                 (uint8_t *) &connected_ev,
                 sizeof(connected_ev));
#endif
}

static void
le_disconnected(struct ble_gap_conn_desc *conn, int reason)
{
    struct btp_gap_device_disconnected_ev ev;
    ble_addr_t *addr = &conn->peer_ota_addr;

    SYS_LOG_DBG("");

#if MYNEWT_VAL(BTTESTER_CONN_RETRY)
    int rc;

    if ((reason == BLE_HS_HCI_ERR(BLE_ERR_CONN_ESTABLISHMENT)) &&
        os_callout_queued(&connected_ev_co)) {
        if (connection_attempts < MYNEWT_VAL(BTTESTER_CONN_RETRY)) {
            os_callout_stop(&connected_ev_co);

            /* try connecting again */
            rc = ble_gap_connect(own_addr_type, addr, 0,
                         &dflt_conn_params, gap_event_cb,
                         NULL);

            if (rc == 0) {
                connection_attempts++;
                return;
            }
        }
    } else if (os_callout_queued(&connected_ev_co)) {
        os_callout_stop(&connected_ev_co);
        return;
    }
#endif

    connection_attempts = 0;
    memset(&connected_ev, 0, sizeof(connected_ev));

    memcpy(&ev.address, addr, sizeof(ev.address));

    tester_event(BTP_SERVICE_ID_GAP, BTP_GAP_EV_DEVICE_DISCONNECTED,
                 (uint8_t *) &ev, sizeof(ev));
}

static void
auth_passkey_oob(uint16_t conn_handle)
{
    struct ble_gap_conn_desc desc;
    struct ble_sm_io pk;
    int rc;

    SYS_LOG_DBG("");

    rc = ble_gap_conn_find(conn_handle, &desc);
    if (rc) {
        return;
    }

    memcpy(pk.oob, oob, sizeof(oob));
    pk.action = BLE_SM_IOACT_OOB;

    rc = ble_sm_inject_io(conn_handle, &pk);
    assert(rc == 0);
}

static void
auth_passkey_display(uint16_t conn_handle, unsigned int passkey)
{
    struct ble_gap_conn_desc desc;
    struct btp_gap_passkey_display_ev ev;
    ble_addr_t *addr;
    struct ble_sm_io pk;
    int rc;

    SYS_LOG_DBG("");

    rc = ble_gap_conn_find(conn_handle, &desc);
    if (rc) {
        return;
    }

    rc = ble_hs_hci_rand(&pk.passkey, sizeof(pk.passkey));
    assert(rc == 0);
    /* Max value is 999999 */
    pk.passkey %= 1000000;
    pk.action = BLE_SM_IOACT_DISP;

    rc = ble_sm_inject_io(conn_handle, &pk);
    assert(rc == 0);

    addr = &desc.peer_ota_addr;

    memcpy(&ev.address, addr, sizeof(ev.address));
    ev.passkey = htole32(pk.passkey);

    tester_event(BTP_SERVICE_ID_GAP, BTP_GAP_EV_PASSKEY_DISPLAY,
                 (uint8_t *) &ev, sizeof(ev));
}

static void
auth_passkey_entry(uint16_t conn_handle)
{
    struct ble_gap_conn_desc desc;
    struct btp_gap_passkey_entry_req_ev ev;
    ble_addr_t *addr;
    int rc;

    SYS_LOG_DBG("");

    rc = ble_gap_conn_find(conn_handle, &desc);
    if (rc) {
        return;
    }

    addr = &desc.peer_ota_addr;

    memcpy(&ev.address, addr, sizeof(ev.address));

    tester_event(BTP_SERVICE_ID_GAP, BTP_GAP_EV_PASSKEY_ENTRY_REQ,
                 (uint8_t *) &ev, sizeof(ev));
}

static void
auth_passkey_numcmp(uint16_t conn_handle, unsigned int passkey)
{
    struct ble_gap_conn_desc desc;
    struct btp_gap_passkey_confirm_req_ev ev;
    ble_addr_t *addr;
    int rc;

    SYS_LOG_DBG("");

    rc = ble_gap_conn_find(conn_handle, &desc);
    if (rc) {
        return;
    }

    addr = &desc.peer_ota_addr;

    memcpy(&ev.address, addr, sizeof(ev.address));
    ev.passkey = htole32(passkey);

    tester_event(BTP_SERVICE_ID_GAP, BTP_GAP_EV_PASSKEY_CONFIRM_REQ,
                 (uint8_t *) &ev, sizeof(ev));
}

static void
auth_passkey_oob_sc(uint16_t conn_handle)
{
    int rc;
    struct ble_sm_io pk;

    SYS_LOG_DBG("");

    memset(&pk, 0, sizeof(pk));

    pk.oob_sc_data.local = &oob_data_local;

    if (ble_hs_cfg.sm_oob_data_flag) {
        pk.oob_sc_data.remote = &oob_data_remote;
    }

    pk.action = BLE_SM_IOACT_OOB_SC;
    rc = ble_sm_inject_io(conn_handle, &pk);
    if (rc != 0) {
        console_printf("error providing oob; rc=%d\n", rc);
    }
}

static void
le_passkey_action(uint16_t conn_handle,
                  struct ble_gap_passkey_params *params)
{
    SYS_LOG_DBG("");

    switch (params->action) {
    case BLE_SM_IOACT_NONE:
        break;
    case BLE_SM_IOACT_OOB:
        auth_passkey_oob(conn_handle);
        break;
    case BLE_SM_IOACT_INPUT:
        auth_passkey_entry(conn_handle);
        break;
    case BLE_SM_IOACT_DISP:
        auth_passkey_display(conn_handle,
                             params->numcmp);
        break;
    case BLE_SM_IOACT_NUMCMP:
        auth_passkey_numcmp(conn_handle,
                            params->numcmp);
        break;
    case BLE_SM_IOACT_OOB_SC:
        auth_passkey_oob_sc(conn_handle);
        break;
    default:
        assert(0);
    }
}

static void
le_identity_resolved(uint16_t conn_handle)
{
    struct ble_gap_conn_desc desc;
    struct btp_gap_identity_resolved_ev ev;
    int rc;

    SYS_LOG_DBG("");

    rc = ble_gap_conn_find(conn_handle, &desc);
    if (rc) {
        return;
    }

    peer_id_addr = desc.peer_id_addr;
    peer_ota_addr = desc.peer_ota_addr;

    memcpy(&ev.address, &desc.peer_ota_addr, sizeof(ev.address));

    memcpy(&ev.identity_address, &desc.peer_id_addr,
           sizeof(ev.identity_address));

    tester_event(BTP_SERVICE_ID_GAP, BTP_GAP_EV_IDENTITY_RESOLVED,
                 (uint8_t *) &ev, sizeof(ev));
}

static void
le_pairing_failed(uint16_t conn_handle, int reason)
{
    struct ble_gap_conn_desc desc;
    struct btp_gap_sec_pairing_failed_ev ev;
    int rc;

    SYS_LOG_DBG("");

    rc = ble_gap_conn_find(conn_handle, &desc);
    if (rc) {
        return;
    }

    peer_id_addr = desc.peer_id_addr;
    peer_ota_addr = desc.peer_ota_addr;

    memcpy(&ev.address, &desc.peer_ota_addr, sizeof(ev.address));

    ev.reason = reason;

    tester_event(BTP_SERVICE_ID_GAP, BTP_GAP_EV_SEC_PAIRING_FAILED,
                 (uint8_t *) &ev, sizeof(ev));
}

static void
le_conn_param_update(struct ble_gap_conn_desc *desc)
{
    struct btp_gap_conn_param_update_ev ev;

    SYS_LOG_DBG("");

    memcpy(&ev.address, &desc->peer_ota_addr, sizeof(ev.address));

    ev.conn_itvl = desc->conn_itvl;
    ev.conn_latency = desc->conn_latency;
    ev.supervision_timeout = desc->supervision_timeout;

    tester_event(BTP_SERVICE_ID_GAP, BTP_GAP_EV_CONN_PARAM_UPDATE,
                 (uint8_t *) &ev, sizeof(ev));
}

static void
le_encryption_changed(struct ble_gap_conn_desc *desc)
{
    struct btp_gap_sec_level_changed_ev ev;

    SYS_LOG_DBG("");

    encrypted = (bool) desc->sec_state.encrypted;

    memcpy(&ev.address, &desc->peer_ota_addr, sizeof(ev.address));
    ev.level = 0;

    if (desc->sec_state.encrypted) {
        if (desc->sec_state.authenticated) {
            if (desc->sec_state.key_size == 16) {
                ev.level = 3;
            } else {
                ev.level = 2;
            }
        } else {
            ev.level = 1;
        }
    }

    tester_event(BTP_SERVICE_ID_GAP, BTP_GAP_EV_SEC_LEVEL_CHANGED,
                 (uint8_t *) &ev, sizeof(ev));
}

static void
bond_lost(uint16_t conn_handle)
{
    struct btp_gap_bond_lost_ev ev;
    struct ble_gap_conn_desc desc;
    int rc;

    rc = ble_gap_conn_find(conn_handle, &desc);
    assert(rc == 0);

    memcpy(&ev.address, &desc.peer_id_addr, sizeof(ev.address));
    tester_event(BTP_SERVICE_ID_GAP, BTP_GAP_EV_BOND_LOST,
                 (uint8_t *) &ev, sizeof(ev));
}

static void
print_bytes(const uint8_t *bytes, int len)
{
    int i;

    for (i = 0; i < len; i++) {
        console_printf("%s0x%02x", i != 0 ? ":" : "", bytes[i]);
    }
}

static void
print_mbuf(const struct os_mbuf *om)
{
    int colon;

    colon = 0;
    while (om != NULL) {
        if (colon) {
            console_printf(":");
        } else {
            colon = 1;
        }
        print_bytes(om->om_data, om->om_len);
        om = SLIST_NEXT(om, om_next);
    }
}

static void
print_addr(const void *addr)
{
    const uint8_t *u8p;

    u8p = addr;
    console_printf("%02x:%02x:%02x:%02x:%02x:%02x",
                   u8p[5], u8p[4], u8p[3], u8p[2], u8p[1], u8p[0]);
}

static void
print_conn_desc(const struct ble_gap_conn_desc *desc)
{
    console_printf("handle=%d our_ota_addr_type=%d our_ota_addr=",
                   desc->conn_handle, desc->our_ota_addr.type);
    print_addr(desc->our_ota_addr.val);
    console_printf(" our_id_addr_type=%d our_id_addr=",
                   desc->our_id_addr.type);
    print_addr(desc->our_id_addr.val);
    console_printf(" peer_ota_addr_type=%d peer_ota_addr=",
                   desc->peer_ota_addr.type);
    print_addr(desc->peer_ota_addr.val);
    console_printf(" peer_id_addr_type=%d peer_id_addr=",
                   desc->peer_id_addr.type);
    print_addr(desc->peer_id_addr.val);
    console_printf(" conn_itvl=%d conn_latency=%d supervision_timeout=%d "
                   "key_sz=%d encrypted=%d authenticated=%d bonded=%d\n",
                   desc->conn_itvl, desc->conn_latency,
                   desc->supervision_timeout,
                   desc->sec_state.key_size,
                   desc->sec_state.encrypted,
                   desc->sec_state.authenticated,
                   desc->sec_state.bonded);
}

static void
adv_complete(void)
{
    struct btp_gap_new_settings_ev ev;

    current_settings &= ~BIT(BTP_GAP_SETTINGS_ADVERTISING);
    ev.current_settings = htole32(current_settings);

    tester_event(BTP_SERVICE_ID_GAP, BTP_GAP_EV_NEW_SETTINGS,
                 (uint8_t *) &ev, sizeof(ev));
}

static int
gap_event_cb(struct ble_gap_event *event, void *arg)
{
    struct ble_gap_conn_desc desc;
    int rc;

    switch (event->type) {
    case BLE_GAP_EVENT_ADV_COMPLETE:
        console_printf("advertising complete; reason=%d\n",
                       event->adv_complete.reason);
        break;
    case BLE_GAP_EVENT_CONNECT:
        console_printf("connection %s; status=%d ",
                       event->connect.status == 0 ? "established"
                                                  : "failed",
                       event->connect.status);
        if (event->connect.status == 0) {
            rc = ble_gap_conn_find(event->connect.conn_handle, &desc);
            assert(rc == 0);
            print_conn_desc(&desc);
        }

        if (desc.role == BLE_GAP_ROLE_SLAVE) {
            adv_complete();
        }

        le_connected(event->connect.conn_handle,
                     event->connect.status);
        break;
    case BLE_GAP_EVENT_DISCONNECT:
        console_printf("disconnect; reason=%d ",
                       event->disconnect.reason);
        print_conn_desc(&event->disconnect.conn);
        le_disconnected(&event->disconnect.conn,
                        event->disconnect.reason);
        break;
    case BLE_GAP_EVENT_ENC_CHANGE:
        console_printf("encryption change event; status=%d ",
                       event->enc_change.status);
        rc = ble_gap_conn_find(event->enc_change.conn_handle, &desc);
        assert(rc == 0);
        print_conn_desc(&desc);
        le_encryption_changed(&desc);
        if (event->enc_change.status
            == BLE_HS_HCI_ERR(BLE_ERR_PINKEY_MISSING)) {
            bond_lost(event->enc_change.conn_handle);
        }
        break;
    case BLE_GAP_EVENT_PASSKEY_ACTION:
        console_printf("passkey action event; action=%d",
                       event->passkey.params.action);
        if (event->passkey.params.action == BLE_SM_IOACT_NUMCMP) {
            console_printf(" numcmp=%lu",
                           (unsigned long) event->passkey.params.numcmp);
        }
        console_printf("\n");
        le_passkey_action(event->passkey.conn_handle,
                          &event->passkey.params);
        break;
    case BLE_GAP_EVENT_IDENTITY_RESOLVED:
        console_printf("identity resolved ");
        rc = ble_gap_conn_find(event->identity_resolved.conn_handle, &desc);
        assert(rc == 0);
        print_conn_desc(&desc);
        le_identity_resolved(event->identity_resolved.conn_handle);
        break;
    case BLE_GAP_EVENT_NOTIFY_RX:
        console_printf(
            "notification rx event; attr_handle=%d indication=%d "
            "len=%d data=",
            event->notify_rx.attr_handle,
            event->notify_rx.indication,
            OS_MBUF_PKTLEN(event->notify_rx.om));

        print_mbuf(event->notify_rx.om);
        console_printf("\n");
        tester_gattc_notify_rx_ev(event->notify_rx.conn_handle,
                                  event->notify_rx.attr_handle,
                                  event->notify_rx.indication,
                                  event->notify_rx.om);
        break;
    case BLE_GAP_EVENT_SUBSCRIBE:
        console_printf("subscribe event; conn_handle=%d attr_handle=%d "
                       "reason=%d prevn=%d curn=%d previ=%d curi=%d\n",
                       event->subscribe.conn_handle,
                       event->subscribe.attr_handle,
                       event->subscribe.reason,
                       event->subscribe.prev_notify,
                       event->subscribe.cur_notify,
                       event->subscribe.prev_indicate,
                       event->subscribe.cur_indicate);
        tester_gatt_subscribe_ev(event->subscribe.conn_handle,
                                 event->subscribe.attr_handle,
                                 event->subscribe.reason,
                                 event->subscribe.prev_notify,
                                 event->subscribe.cur_notify,
                                 event->subscribe.prev_indicate,
                                 event->subscribe.cur_indicate);
        break;
    case BLE_GAP_EVENT_REPEAT_PAIRING:
        console_printf("repeat pairing event; conn_handle=%d "
                       "cur_key_sz=%d cur_auth=%d cur_sc=%d "
                       "new_key_sz=%d new_auth=%d new_sc=%d "
                       "new_bonding=%d\n",
                       event->repeat_pairing.conn_handle,
                       event->repeat_pairing.cur_key_size,
                       event->repeat_pairing.cur_authenticated,
                       event->repeat_pairing.cur_sc,
                       event->repeat_pairing.new_key_size,
                       event->repeat_pairing.new_authenticated,
                       event->repeat_pairing.new_sc,
                       event->repeat_pairing.new_bonding);
        rc = ble_gap_conn_find(event->repeat_pairing.conn_handle, &desc);
        assert(rc == 0);
        rc = ble_store_util_delete_peer(&desc.peer_id_addr);
        assert(rc == 0);
        bond_lost(event->repeat_pairing.conn_handle);
        return BLE_GAP_REPEAT_PAIRING_RETRY;
    case BLE_GAP_EVENT_CONN_UPDATE:
        console_printf("connection update event; status=%d ",
                       event->conn_update.status);
        rc = ble_gap_conn_find(event->conn_update.conn_handle, &desc);
        assert(rc == 0);
        print_conn_desc(&desc);
        le_conn_param_update(&desc);
        break;
    case BLE_GAP_EVENT_CONN_UPDATE_REQ:
        console_printf("connection update request event; "
                       "conn_handle=%d itvl_min=%d itvl_max=%d "
                       "latency=%d supervision_timoeut=%d "
                       "min_ce_len=%d max_ce_len=%d\n",
                       event->conn_update_req.conn_handle,
                       event->conn_update_req.peer_params->itvl_min,
                       event->conn_update_req.peer_params->itvl_max,
                       event->conn_update_req.peer_params->latency,
                       event->conn_update_req.peer_params->supervision_timeout,
                       event->conn_update_req.peer_params->min_ce_len,
                       event->conn_update_req.peer_params->max_ce_len);

        *event->conn_update_req.self_params = *event->conn_update_req.peer_params;
        break;
    case BLE_GAP_EVENT_L2CAP_UPDATE_REQ:
        console_printf("connection update request event; "
                       "conn_handle=%d itvl_min=%d itvl_max=%d "
                       "latency=%d supervision_timoeut=%d "
                       "min_ce_len=%d max_ce_len=%d\n",
                       event->conn_update_req.conn_handle,
                       event->conn_update_req.peer_params->itvl_min,
                       event->conn_update_req.peer_params->itvl_max,
                       event->conn_update_req.peer_params->latency,
                       event->conn_update_req.peer_params->supervision_timeout,
                       event->conn_update_req.peer_params->min_ce_len,
                       event->conn_update_req.peer_params->max_ce_len);
        if (event->conn_update_req.peer_params->itvl_min
            == REJECT_INTERVAL_MIN &&
            event->conn_update_req.peer_params->itvl_max
            == REJECT_INTERVAL_MAX &&
            event->conn_update_req.peer_params->latency == REJECT_LATENCY
            &&
            event->conn_update_req.peer_params->supervision_timeout
            == REJECT_SUPERVISION_TIMEOUT) {
            return EINVAL;
        }
    case BLE_GAP_EVENT_PARING_COMPLETE:
        console_printf("received pairing complete: "
                       "conn_handle=%d status=%d\n",
                       event->pairing_complete.conn_handle,
                       event->pairing_complete.status);
        if (event->pairing_complete.status != BLE_SM_ERR_SUCCESS) {
            le_pairing_failed(event->pairing_complete.conn_handle,
                              event->pairing_complete.status);
        }
        break;
    default:
        break;
    }

    return 0;
}

static uint8_t
connect(const void *cmd, uint16_t cmd_len,
        void *rsp, uint16_t *rsp_len)
{
    const struct btp_gap_connect_cmd *cp = cmd;

    ble_addr_t *addr =  (ble_addr_t *)&cp->address;

    SYS_LOG_DBG("");

    if (ble_addr_cmp(BLE_ADDR_ANY, addr) == 0) {
        addr = NULL;
    }

    if (ble_gap_connect(own_addr_type, addr, 0,
                        &dflt_conn_params, gap_event_cb, NULL)) {
        return BTP_STATUS_FAILED;
    }

    return BTP_STATUS_SUCCESS;
}

static uint8_t
disconnect(const void *cmd, uint16_t cmd_len,
           void *rsp, uint16_t *rsp_len)
{
    const struct btp_gap_disconnect_cmd *cp = cmd;
    struct ble_gap_conn_desc desc;
    int rc;

    SYS_LOG_DBG("");

    rc = gap_conn_find_by_addr(&cp->address, &desc);
    if (rc) {
        return BTP_STATUS_FAILED;
    }

    if (ble_gap_terminate(desc.conn_handle, BLE_ERR_REM_USER_CONN_TERM)) {
        return BTP_STATUS_FAILED;
    }

    return BTP_STATUS_SUCCESS;
}

static uint8_t
set_io_cap(const void *cmd, uint16_t cmd_len,
           void *rsp, uint16_t *rsp_len)
{
    const struct btp_gap_set_io_cap_cmd *cp = cmd;

    SYS_LOG_DBG("");

    switch (cp->io_cap) {
    case BTP_GAP_IO_CAP_DISPLAY_ONLY:
        ble_hs_cfg.sm_io_cap = BLE_SM_IO_CAP_DISP_ONLY;
        ble_hs_cfg.sm_mitm = 1;
        break;
    case BTP_GAP_IO_CAP_KEYBOARD_DISPLAY:
        ble_hs_cfg.sm_io_cap = BLE_SM_IO_CAP_KEYBOARD_DISP;
        ble_hs_cfg.sm_mitm = 1;
        break;
    case BTP_GAP_IO_CAP_NO_INPUT_OUTPUT:
        ble_hs_cfg.sm_io_cap = BLE_SM_IO_CAP_NO_IO;
        ble_hs_cfg.sm_mitm = 0;
        break;
    case BTP_GAP_IO_CAP_KEYBOARD_ONLY:
        ble_hs_cfg.sm_io_cap = BLE_SM_IO_CAP_KEYBOARD_ONLY;
        ble_hs_cfg.sm_mitm = 1;
        break;
    case BTP_GAP_IO_CAP_DISPLAY_YESNO:
        ble_hs_cfg.sm_io_cap = BLE_SM_IO_CAP_DISP_YES_NO;
        ble_hs_cfg.sm_mitm = 1;
        break;
    default:
        return BTP_STATUS_FAILED;
    }

    return BTP_STATUS_SUCCESS;
}

static uint8_t
pair(const void *cmd, uint16_t cmd_len,
     void *rsp, uint16_t *rsp_len)
{
    const struct btp_gap_pair_cmd *cp = cmd;
    struct ble_gap_conn_desc desc;
    int rc;

    SYS_LOG_DBG("");

    rc = gap_conn_find_by_addr(&cp->address, &desc);
    if (rc) {
        return BTP_STATUS_FAILED;
    }

    rc = ble_gap_security_initiate(desc.conn_handle);
    if (rc != 0 && rc != BLE_HS_EALREADY) {
        return BTP_STATUS_FAILED;
    }

    return BTP_STATUS_SUCCESS;
}

static uint8_t
unpair(const void *cmd, uint16_t cmd_len,
       void *rsp, uint16_t *rsp_len)
{
    const struct btp_gap_unpair_cmd *cp = cmd;
    int err;

    SYS_LOG_DBG("");

    err = ble_gap_unpair(&cp->address);
    return err != 0 ? BTP_STATUS_FAILED : BTP_STATUS_SUCCESS;
}

static uint8_t
passkey_entry(const void *cmd, uint16_t cmd_len,
              void *rsp, uint16_t *rsp_len)
{
    const struct btp_gap_passkey_entry_cmd *cp = cmd;
    struct ble_gap_conn_desc desc;
    struct ble_sm_io pk;
    int rc;

    SYS_LOG_DBG("");

    rc = gap_conn_find_by_addr(&cp->address, &desc);
    if (rc) {
        return BTP_STATUS_FAILED;
    }

    pk.action = BLE_SM_IOACT_INPUT;
    pk.passkey = le32toh(cp->passkey);

    rc = ble_sm_inject_io(desc.conn_handle, &pk);
    if (rc) {
        return BTP_STATUS_FAILED;
    }

    return BTP_STATUS_SUCCESS;
}

static uint8_t
passkey_confirm(const void *cmd, uint16_t cmd_len,
                void *rsp, uint16_t *rsp_len)
{
    const struct btp_gap_passkey_confirm_cmd *cp = cmd;
    struct ble_gap_conn_desc desc;
    struct ble_sm_io pk;
    int rc;

    SYS_LOG_DBG("");

    rc = gap_conn_find_by_addr(&cp->address, &desc);
    if (rc) {
        return BTP_STATUS_FAILED;
    }

    pk.action = BLE_SM_IOACT_NUMCMP;
    pk.numcmp_accept = cp->match;

    rc = ble_sm_inject_io(desc.conn_handle, &pk);
    if (rc) {
        console_printf("sm inject io failed");
        return BTP_STATUS_FAILED;
    }

    return BTP_STATUS_SUCCESS;
}

static uint8_t
start_direct_adv(const void *cmd, uint16_t cmd_len,
                 void *rsp, uint16_t *rsp_len)
{
    const struct btp_gap_start_direct_adv_cmd *cp = cmd;
    struct btp_gap_start_advertising_rp *rp = rsp;
    static struct ble_gap_adv_params adv_params = {
        .conn_mode = BLE_GAP_CONN_MODE_DIR,
    };
    int err;

    SYS_LOG_DBG("");

    adv_params.high_duty_cycle = cp->high_duty;

    err = ble_gap_adv_start(own_addr_type, &cp->address,
                            BLE_HS_FOREVER, &adv_params,
                            gap_event_cb, NULL);
    if (err) {
        SYS_LOG_ERR("Advertising failed: err %d", err);
        return BTP_STATUS_FAILED;
    }

    current_settings |= BIT(BTP_GAP_SETTINGS_ADVERTISING);
    rp->current_settings = htole32(current_settings);

    *rsp_len = sizeof(*rp);

    return BTP_STATUS_SUCCESS;
}

static void
conn_param_update_cb(uint16_t conn_handle, int status, void *arg)
{
    console_printf("conn param update complete; conn_handle=%d status=%d\n",
                   conn_handle, status);
}

static int
conn_param_update_slave(uint16_t conn_handle,
                        const struct btp_gap_conn_param_update_cmd *cmd)
{
    int rc;
    struct ble_l2cap_sig_update_params params;

    params.itvl_min = cmd->conn_itvl_min;
    params.itvl_max = cmd->conn_itvl_max;
    params.slave_latency = cmd->conn_latency;
    params.timeout_multiplier = cmd->supervision_timeout;

    rc = ble_l2cap_sig_update(conn_handle, &params,
                              conn_param_update_cb, NULL);
    if (rc) {
        SYS_LOG_ERR("Failed to send update params: rc=%d", rc);
    }

    return 0;
}

static int
conn_param_update_master(uint16_t conn_handle,
                         const struct btp_gap_conn_param_update_cmd *cmd)
{
    int rc;
    struct ble_gap_upd_params params;

    params.itvl_min = cmd->conn_itvl_min;
    params.itvl_max = cmd->conn_itvl_max;
    params.latency = cmd->conn_latency;
    params.supervision_timeout = cmd->supervision_timeout;
    params.min_ce_len = 0;
    params.max_ce_len = 0;
    rc = ble_gap_update_params(conn_handle, &params);
    if (rc) {
        SYS_LOG_ERR("Failed to send update params: rc=%d", rc);
    }

    return rc;
}

static void
conn_param_update(struct os_event *ev)
{
    struct ble_gap_conn_desc desc;
    int rc;

    SYS_LOG_DBG("");

    rc = gap_conn_find_by_addr((ble_addr_t *) &update_params, &desc);
    if (rc) {
        goto rsp;
    }

    if ((desc.conn_itvl >= update_params.conn_itvl_min) &&
        (desc.conn_itvl <= update_params.conn_itvl_max) &&
        (desc.conn_latency == update_params.conn_latency) &&
        (desc.supervision_timeout == update_params.supervision_timeout)) {
        goto rsp;
    }

    if (desc.role == BLE_GAP_ROLE_MASTER) {
        rc = conn_param_update_master(desc.conn_handle, &update_params);
    } else {
        rc = conn_param_update_slave(desc.conn_handle, &update_params);
    }

    if (rc == 0) {
        return;
    }

rsp:
    SYS_LOG_ERR("Conn param update fail; rc=%d", rc);
}

static uint8_t
conn_param_update_async(const void *cmd, uint16_t cmd_len,
                        void *rsp, uint16_t *rsp_len)
{
    const struct btp_gap_conn_param_update_cmd *cp = cmd;
    update_params = *cp;

    os_callout_reset(&update_params_co, 0);

    return BTP_STATUS_SUCCESS;
}

static uint8_t
set_oob_legacy_data(const void *cmd, uint16_t cmd_len,
                    void *rsp, uint16_t *rsp_len)
{
    const struct btp_gap_oob_legacy_set_data_cmd *cp = cmd;

    ble_hs_cfg.sm_oob_data_flag = 1;
    memcpy(oob, cp->oob_data, sizeof(oob));

    return BTP_STATUS_SUCCESS;
}

static uint8_t
get_oob_sc_local_data(const void *cmd, uint16_t cmd_len,
                      void *rsp, uint16_t *rsp_len)
{
    struct btp_gap_oob_sc_get_local_data_rp *rp = rsp;

    memcpy(rp->r, oob_data_local.r, 16);
    memcpy(rp->c, oob_data_local.c, 16);

    *rsp_len = sizeof(*rp);

    return BTP_STATUS_SUCCESS;
}

static uint8_t
set_oob_sc_remote_data(const void *cmd, uint16_t cmd_len,
                       void *rsp, uint16_t *rsp_len)
{
    const struct btp_gap_oob_sc_set_remote_data_cmd *cp = cmd;

    ble_hs_cfg.sm_oob_data_flag = 1;
    memcpy(oob_data_remote.r, cp->r, 16);
    memcpy(oob_data_remote.c, cp->c, 16);

    return BTP_STATUS_SUCCESS;
}

static uint8_t
set_mitm(const void *cmd, uint16_t cmd_len,
         void *rsp, uint16_t *rsp_len)
{
    const struct btp_gap_set_mitm_cmd *cp = cmd;

    ble_hs_cfg.sm_mitm = cp->mitm;

    return BTP_STATUS_SUCCESS;
}

static uint8_t
set_filter_accept_list(const void *cmd, uint16_t cmd_len,
                       void *rsp, uint16_t *rsp_len)
{
    const struct btp_gap_set_filter_accept_list_cmd *cp = cmd;
    int err;

    SYS_LOG_DBG("");

    /*
     * Check if the nb of bytes received matches the len of addrs list.
     * Then set the filter accept list.
     */
    if ((cmd_len < sizeof(*cp)) ||
        (cmd_len != sizeof(*cp) + (cp->list_len * sizeof(cp->addrs[0])))) {
        return BTP_STATUS_FAILED;
    }

    err = ble_gap_wl_set(cp->addrs, cp->list_len);
    if (err != 0) {
        return BTP_STATUS_FAILED;
    }

    return BTP_STATUS_SUCCESS;
}

static const struct btp_handler handlers[] = {
    {
        .opcode = BTP_GAP_READ_SUPPORTED_COMMANDS,
        .index = BTP_INDEX_NONE,
        .expect_len = 0,
        .func = supported_commands,
    },
    {
        .opcode = BTP_GAP_READ_CONTROLLER_INDEX_LIST,
        .index = BTP_INDEX_NONE,
        .expect_len = 0,
        .func = controller_index_list,
    },
    {
        .opcode = BTP_GAP_READ_CONTROLLER_INFO,
        .expect_len = 0,
        .func = controller_info,
    },
    {
        .opcode = BTP_GAP_SET_CONNECTABLE,
        .expect_len = sizeof(struct btp_gap_set_connectable_cmd),
        .func = set_connectable,
    },
    {
        .opcode = BTP_GAP_SET_DISCOVERABLE,
        .expect_len = sizeof(struct btp_gap_set_discoverable_cmd),
        .func = set_discoverable,
    },
    {
        .opcode = BTP_GAP_SET_BONDABLE,
        .expect_len = sizeof(struct btp_gap_set_bondable_cmd),
        .func = set_bondable,
    },
    {
        .opcode = BTP_GAP_START_ADVERTISING,
        .expect_len = BTP_HANDLER_LENGTH_VARIABLE,
        .func = start_advertising,
    },
    {
        .opcode = BTP_GAP_START_DIRECT_ADV,
        .expect_len = sizeof(struct btp_gap_start_direct_adv_cmd),
        .func = start_direct_adv,
    },
    {
        .opcode = BTP_GAP_STOP_ADVERTISING,
        .expect_len = 0,
        .func = stop_advertising,
    },
    {
        .opcode = BTP_GAP_START_DISCOVERY,
        .expect_len = sizeof(struct btp_gap_start_discovery_cmd),
        .func = start_discovery,
    },
    {
        .opcode = BTP_GAP_STOP_DISCOVERY,
        .expect_len = 0,
        .func = stop_discovery,
    },
    {
        .opcode = BTP_GAP_CONNECT,
        .expect_len = sizeof(struct btp_gap_connect_cmd),
        .func = connect,
    },
    {
        .opcode = BTP_GAP_DISCONNECT,
        .expect_len = sizeof(struct btp_gap_disconnect_cmd),
        .func = disconnect,
    },
    {
        .opcode = BTP_GAP_SET_IO_CAP,
        .expect_len = sizeof(struct btp_gap_set_io_cap_cmd),
        .func = set_io_cap,
    },
    {
        .opcode = BTP_GAP_PAIR,
        .expect_len = sizeof(struct btp_gap_pair_cmd),
        .func = pair,
    },
    {
        .opcode = BTP_GAP_UNPAIR,
        .expect_len = sizeof(struct btp_gap_unpair_cmd),
        .func = unpair,
    },
    {
        .opcode = BTP_GAP_PASSKEY_ENTRY,
        .expect_len = sizeof(struct btp_gap_passkey_entry_cmd),
        .func = passkey_entry,
    },
    {
        .opcode = BTP_GAP_PASSKEY_CONFIRM,
        .expect_len = sizeof(struct btp_gap_passkey_confirm_cmd),
        .func = passkey_confirm,
    },
    {
        .opcode = BTP_GAP_CONN_PARAM_UPDATE,
        .expect_len = sizeof(struct btp_gap_conn_param_update_cmd),
        .func = conn_param_update_async,
    },
    {
        .opcode = BTP_GAP_OOB_LEGACY_SET_DATA,
        .expect_len = sizeof(struct btp_gap_oob_legacy_set_data_cmd),
        .func = set_oob_legacy_data,
    },
    {
        .opcode = BTP_GAP_OOB_SC_GET_LOCAL_DATA,
        .expect_len = 0,
        .func = get_oob_sc_local_data,
    },
    {
        .opcode = BTP_GAP_OOB_SC_SET_REMOTE_DATA,
        .expect_len = sizeof(struct btp_gap_oob_sc_set_remote_data_cmd),
        .func = set_oob_sc_remote_data,
    },
    {
        .opcode = BTP_GAP_SET_MITM,
        .expect_len = sizeof(struct btp_gap_set_mitm_cmd),
        .func = set_mitm,
    },
    {
        .opcode = BTP_GAP_SET_FILTER_ACCEPT_LIST,
        .expect_len = BTP_HANDLER_LENGTH_VARIABLE,
        .func = set_filter_accept_list,
    },
};

static void
tester_init_gap_cb()
{
    current_settings = 0;
    current_settings |= BIT(BTP_GAP_SETTINGS_POWERED);
    current_settings |= BIT(BTP_GAP_SETTINGS_LE);

    os_callout_init(&update_params_co, os_eventq_dflt_get(),
                    conn_param_update, NULL);

    os_callout_init(&connected_ev_co, os_eventq_dflt_get(),
                    device_connected_ev_send, NULL);
}

uint8_t
tester_init_gap(void)
{
#if MYNEWT_VAL(BLE_SM_SC)
    int rc;

    rc = ble_sm_sc_oob_generate_data(&oob_data_local);
    if (rc) {
        console_printf("Error: generating oob data; reason=%d\n", rc);
        return BTP_STATUS_FAILED;
    }
#endif
#if MYNEWT_VAL(BTTESTER_PRIVACY_MODE) && MYNEWT_VAL(BTTESTER_USE_NRPA)
    os_callout_init(&bttester_nrpa_rotate_timer, os_eventq_dflt_get(),
                    rotate_nrpa_cb, NULL);
#endif
    adv_buf = os_msys_get(ADV_BUF_LEN, 0);
    assert(adv_buf);

    tester_init_gap_cb();

    tester_register_command_handlers(BTP_SERVICE_ID_GAP, handlers,
                                     ARRAY_SIZE(handlers));

    return BTP_STATUS_SUCCESS;
}

uint8_t
tester_unregister_gap(void)
{
    return BTP_STATUS_SUCCESS;
}
