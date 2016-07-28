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

#include <assert.h>
#include <string.h>
#include <math.h>
#include "host/ble_hs.h"
#include "profiles/ans/ble_svc_ans.h"

/* Access function */
static int
ble_svc_ans_access(uint16_t conn_handle, uint16_t attr_handle,
                   struct ble_gatt_access_ctxt *ctxt, void *arg);

static const struct ble_gatt_svc_def ble_svc_ans_defs[] = {
    {
        /*** Alert Notification Service. */
        .type = BLE_GATT_SVC_TYPE_PRIMARY,
        .uuid128 = BLE_UUID16(BLE_SVC_ANS_UUID16),
        .characteristics = (struct ble_gatt_chr_def[]) { {
            /*** Supported New Alert Catagory 
             * 
             * This characteristic exposes what categories of new 
             * alert are supported in the server.
             * 
             * */
            .uuid128 = BLE_UUID16(BLE_SVC_ANS_CHR_UUID16_SUP_NEW_ALERT_CAT),
            .access_cb = ble_svc_ans_access,
            .flags = BLE_GATT_CHR_F_READ,
        }, {
            /*** New Alert 
             *
             * This characteristic exposes information about 
             * the count of new alerts (for a given category).
             *
             * */
            .uuid128 = BLE_UUID16(BLE_SVC_ANS_CHR_UUID16_NEW_ALERT),
            .access_cb = ble_svc_ans_access,
            .flags = BLE_GATT_CHR_F_NOTIFY,
        }, {
            /*** Supported Unread Alert Catagory 
             *
             * This characteristic exposes what categories of 
             * unread alert are supported in the server.
             *
             * */
            .uuid128 = BLE_UUID16(BLE_SVC_ANS_CHR_UUID16_SUP_UNR_ALERT_CAT),
            .access_cb = ble_svc_ans_access,
            .flags = BLE_GATT_CHR_F_READ,
        }, {
            /*** Unread Alert Status 
             *
             * This characteristic exposes the count of unread 
             * alert events existing in the server.
             *
             * */
            .uuid128 = BLE_UUID16(BLE_SVC_ANS_CHR_UUID16_UNR_ALERT_STAT),
            .access_cb = ble_svc_ans_access,
            .flags = BLE_GATT_CHR_F_NOTIFY,
        }, {
            /*** Alert Notification Control Point 
             *
             * This characteristic allows the peer device to 
             * enable/disable the alert notification of new alert 
             * and unread event more selectively than can be done 
             * by setting or clearing the notification bit in the 
             * Client Characteristic Configuration for each alert 
             * characteristic.
             *
             * */
            .uuid128 = BLE_UUID16(BLE_SVC_ANS_CHR_UUID16_ALERT_NOT_CTRL_PT),
            .access_cb = ble_svc_ans_access,
            .flags = BLE_GATT_CHR_F_WRITE,
        }, {
            0, /* No more characteristics in this service. */
        } },
    },

    {
        0, /* No more services. */
    },
};

static int
ble_svc_ans_chr_write(struct os_mbuf *om, uint16_t min_len,
                      uint16_t max_len, void *dst,
                      uint16_t *len)
{
    uint16_t om_len;
    int rc;

    om_len = OS_MBUF_PKTLEN(om);
    if (om_len < min_len || om_len > max_len) {
        return BLE_ATT_ERR_INVALID_ATTR_VALUE_LEN;
    }

    rc = ble_hs_mbuf_to_flat(om, dst, max_len, len);
    if (rc != 0) {
        return BLE_ATT_ERR_UNLIKELY;
    }

    return 0;
}

/* Because the max length of the info string is 18 octets,
 * and the category ID and count take up 2 octets each, we
 * require a maximum of 20 octets of space.
 */
#define GATT_SVR_NEW_ALERT_VAL_MAX_LEN   20 

/* Supported new alert categories bitmask */
static uint8_t ble_svc_ans_new_alert_cat;
/* New alert value */
static uint8_t ble_svc_ans_new_alert_val[GATT_SVR_NEW_ALERT_VAL_MAX_LEN];
static uint16_t ble_svc_ans_new_alert_val_len;
/* New alert count, one value for each category */
static uint8_t ble_svc_ans_new_alert_cnt[BLE_SVC_ANS_CAT_NUM];

/* Supported unread alert catagories bitmask */ 
static uint8_t ble_svc_ans_unr_alert_cat; 
/* Unread alert status, contains supported catagories and count */
static uint8_t ble_svc_ans_unr_alert_stat[2];
/* Count of unread alerts. One value for each category */
static uint8_t ble_svc_ans_unr_alert_cnt[BLE_SVC_ANS_CAT_NUM];

/* Alert notification control point value */
static uint8_t ble_svc_ans_alert_not_ctrl_pt[2];

static int
ble_svc_ans_access(uint16_t conn_handle, uint16_t attr_handle,
                          struct ble_gatt_access_ctxt *ctxt,
                          void *arg)
{
    uint16_t uuid16;
    int rc;

    uuid16 = ble_uuid_128_to_16(ctxt->chr->uuid128);
    assert(uuid16 != 0);

    switch (uuid16) {
    case BLE_SVC_ANS_CHR_UUID16_SUP_NEW_ALERT_CAT:
        assert(ctxt->op == BLE_GATT_ACCESS_OP_READ_CHR);
        rc = os_mbuf_append(ctxt->om, &ble_svc_ans_new_alert_cat,
                            sizeof ble_svc_ans_new_alert_cat);
        return rc == 0 ? 0 : BLE_ATT_ERR_INSUFFICIENT_RES;

    case BLE_SVC_ANS_CHR_UUID16_NEW_ALERT:
        if (ctxt->op == BLE_GATT_ACCESS_OP_WRITE_CHR) {
            rc = ble_svc_ans_chr_write(ctxt->om, 0,
                                       sizeof ble_svc_ans_new_alert_val,
                                       ble_svc_ans_new_alert_val,
                                       &ble_svc_ans_new_alert_val_len);
            return rc;

        } else if (ctxt->op == BLE_GATT_ACCESS_OP_READ_CHR) {
            rc = os_mbuf_append(ctxt->om, &ble_svc_ans_new_alert_val,
                                sizeof ble_svc_ans_new_alert_val);
            return rc == 0 ? 0 : BLE_ATT_ERR_INSUFFICIENT_RES;
        }

    case BLE_SVC_ANS_CHR_UUID16_SUP_UNR_ALERT_CAT:
        assert(ctxt->op == BLE_GATT_ACCESS_OP_READ_CHR);
        rc = os_mbuf_append(ctxt->om, &ble_svc_ans_unr_alert_cat,
                            sizeof ble_svc_ans_unr_alert_cat);
        return rc == 0 ? 0 : BLE_ATT_ERR_INSUFFICIENT_RES;

    case BLE_SVC_ANS_CHR_UUID16_UNR_ALERT_STAT:
        if (ctxt->op == BLE_GATT_ACCESS_OP_WRITE_CHR) {
            rc = ble_svc_ans_chr_write(ctxt->om, 
                                       sizeof ble_svc_ans_unr_alert_stat, 
                                       sizeof ble_svc_ans_unr_alert_stat,
                                       &ble_svc_ans_unr_alert_stat,
                                       NULL);
            return rc; 
        } else {
            rc = os_mbuf_append(ctxt->om, &ble_svc_ans_unr_alert_stat,
                                sizeof ble_svc_ans_unr_alert_stat);
            return rc == 0 ? 0 : BLE_ATT_ERR_INSUFFICIENT_RES;
        }

    case BLE_SVC_ANS_CHR_UUID16_ALERT_NOT_CTRL_PT:
        if (ctxt->op == BLE_GATT_ACCESS_OP_WRITE_CHR) {
            rc = ble_svc_ans_chr_write(ctxt->om, 
                                       sizeof ble_svc_ans_alert_not_ctrl_pt, 
                                       sizeof ble_svc_ans_alert_not_ctrl_pt,
                                       &ble_svc_ans_alert_not_ctrl_pt,
                                       NULL);

            /* Get command ID and category ID */
            uint8_t cmd_id = ble_svc_ans_alert_not_ctrl_pt[0];
            uint8_t cat_id = ble_svc_ans_alert_not_ctrl_pt[1];
            uint8_t cat; 

            /* Set cat to the appropriate bitmask based on cat_id or
             * return error if the cat_id in an invalid range.
             */
            if (cat_id < BLE_SVC_ANS_CAT_NUM) {
                cat = (1 << cat_id); 
            } else if (cat_id == 0xff) {
                cat = cat_id;
            } else {
                return BLE_SVC_ANS_ERR_CMD_NOT_SUPPORTED;
            }
            switch(cmd_id) {
            case BLE_SVC_ANS_CMD_EN_NEW_ALERT_CAT:
                ble_svc_ans_new_alert_cat |= cat; 
                break;
            case BLE_SVC_ANS_CMD_EN_UNR_ALERT_CAT:
                ble_svc_ans_unr_alert_cat |= cat;
                break;
            case BLE_SVC_ANS_CMD_DIS_NEW_ALERT_CAT:
                ble_svc_ans_new_alert_cat &= ~cat;
                break;
            case BLE_SVC_ANS_CMD_DIS_UNR_ALERT_CAT:
                ble_svc_ans_unr_alert_cat &= ~cat;
                break;
            case BLE_SVC_ANS_CMD_NOT_NEW_ALERT_IMMEDIATE:
                /* TODO */
                break;
            case BLE_SVC_ANS_CMD_NOT_UNR_ALERT_IMMEDIATE:
                /* TODO */
                break;
            default:
                return BLE_SVC_ANS_ERR_CMD_NOT_SUPPORTED;
            }
            return 0;
        } else {
            rc = BLE_ATT_ERR_UNLIKELY;
        }
        return rc;

    default:
        assert(0);
        return BLE_ATT_ERR_UNLIKELY;
    }
}

/**
 * Adds a new alert to the given category then sets the new 
 * alert val to the category, the new alert count for the given 
 * category, and the given info string. If set up for notification, 
 * setting this value will notify the client.
 * 
 * @param cat_flag              The flag for the category which should
 *                                  should be incremented and notified
 * @param info_str              The info string to be sent to the client
 *                                  with the notification.
 * 
 * @return 0 if success, BLE_HS_EINVAL if the category is not enabled 
 *         or if the flag is not valid.
 */
int
ble_svc_ans_new_alert_add(uint8_t cat_id, const char * info_str)
{
    uint8_t cat_bit_mask; 
    if (cat_id < BLE_SVC_ANS_CAT_NUM) {
        cat_bit_mask = (1 << cat_id); 
    } else {
        return BLE_HS_EINVAL;
    }

    if ((cat_bit_mask & ble_svc_ans_new_alert_cat) == 0) {
        return BLE_HS_EINVAL;
    }

    ble_svc_ans_new_alert_cnt[cat_id] += 1;
    ble_svc_ans_new_alert_val[0] = cat_id;
    ble_svc_ans_new_alert_val[1] = ble_svc_ans_new_alert_cnt[cat_id];
    int n = sizeof info_str;
    if (n > BLE_SVC_ANS_INFO_STR_MAX_LEN) {
        memcpy(&ble_svc_ans_new_alert_val[2], info_str, 
                BLE_SVC_ANS_INFO_STR_MAX_LEN);
    } else {
        memcpy(&ble_svc_ans_new_alert_val[2], info_str, n);
    }
    return 0;
}

/**
 * Adds an unread alert to the given category then sets the unread
 * alert stat to the category and the new unread alert value for
 * the given category. If set up for notification, setting this value
 * will notify the client.
 * 
 * @param cat_flag              The flag for the category which should
 *                                  should be incremented and notified
 * 
 * @return 0 if success, BLE_HS_EINVAL if the category is not enabled 
 *         or if the flag is not valid.
 */
int
ble_svc_ans_unr_alert_add(uint8_t cat_id)
{
    uint8_t cat_bit_mask; 
    if (cat_id < BLE_SVC_ANS_CAT_NUM) {
        cat_bit_mask = pow(2, cat_id);
    } else {
        return BLE_HS_EINVAL;
    }

    if ((cat_bit_mask & ble_svc_ans_unr_alert_cat) == 0) {
        return BLE_HS_EINVAL;
    }

    ble_svc_ans_unr_alert_cnt[cat_id] += 1;
    ble_svc_ans_unr_alert_stat[0] = cat_id;
    ble_svc_ans_unr_alert_stat[1] = ble_svc_ans_unr_alert_cnt[cat_id];
    return 0;
}

/**
 * Initialize the ANS 
 * 
 * XXX: We should technically be able to change the new alert and
 *      unread alert catagories when we have no active connections.
 */
int
ble_svc_ans_init(struct ble_hs_cfg *cfg)
{
    int rc;
    rc = ble_gatts_count_cfg(ble_svc_ans_defs, cfg);
    if (rc != 0) {
        return rc;
    }

    rc = ble_gatts_add_svcs(ble_svc_ans_defs);
    if (rc != 0) {
        return rc;
    }

    return 0;
}

