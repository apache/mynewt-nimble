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

#include <string.h>
#include "host/ble_eddystone.h"
#include "host/ble_hs_adv.h"
#include "ble_hs_priv.h"

#define BLE_EDDYSTONE_MAX_SVC_DATA_LEN  22
#define BLE_EDDYSTONE_SVC_DATA_BASE_SZ  3

#define BLE_EDDYSTONE_SERVICE_UUID      0xfeaa

#define BLE_EDDYSTONE_FRAME_TYPE_UID    0x00
#define BLE_EDDYSTONE_FRAME_TYPE_URL    0x10

static ble_uuid16_t ble_eddystone_uuids16[BLE_EDDYSTONE_MAX_UUIDS16 + 1];
static uint8_t ble_eddystone_svc_data[BLE_EDDYSTONE_MAX_SVC_DATA_LEN];

/**
 * Writes an eddystone header to the global service data buffer.
 *
 * @param frame_type                The eddystone frame type; one of the
 *                                      BLE_EDDYSTONE_FRAME_TYPE_[...] values.
 *
 * @return                          A pointer to where the service data payload
 *                                      should be written.
 */
static void *
ble_eddystone_set_svc_data_base(uint8_t frame_type)
{
    put_le16(ble_eddystone_svc_data, BLE_EDDYSTONE_SERVICE_UUID);
    ble_eddystone_svc_data[2] = frame_type;

    return ble_eddystone_svc_data + BLE_EDDYSTONE_SVC_DATA_BASE_SZ;
}

/**
 * Populates the supplied advertisement fields struct to represent an eddystone
 * advertisement.  Prior to calling this function, you must write the service
 * data header and payload using the ble_eddystone_set_svc_data_base()
 * function.
 *
 * @param adv_fields            The base advertisement fields to transform into
 *                                  an eddystone beacon.  All configured fields
 *                                  are preserved; you probably want to clear
 *                                  this struct before calling this function.
 * @param svc_data_len          The amount of data written to the global
 *                                  service data buffer.
 *
 * @return                      0 on success; BLE_HS_E... on failure.
 */
static int
ble_eddystone_set_adv_data_gen(struct ble_hs_adv_fields *adv_fields,
                               uint8_t svc_data_len)
{
    int rc;

    if (adv_fields->num_uuids16 > BLE_EDDYSTONE_MAX_UUIDS16) {
        return BLE_HS_EINVAL;
    }
    if (svc_data_len > BLE_EDDYSTONE_MAX_SVC_DATA_LEN) {
        return BLE_HS_EINVAL;
    }
    if (adv_fields->num_uuids16 > 0 && !adv_fields->uuids16_is_complete) {
        return BLE_HS_EINVAL;
    }
    if (adv_fields->svc_data_uuid16_len != 0) {
        return BLE_HS_EINVAL;
    }

    ble_eddystone_uuids16[0] =
        (ble_uuid16_t) BLE_UUID16_INIT(BLE_EDDYSTONE_SERVICE_UUID);
    memcpy(ble_eddystone_uuids16 + 1, adv_fields->uuids16,
           adv_fields->num_uuids16 * sizeof(ble_uuid16_t));
    adv_fields->uuids16 = ble_eddystone_uuids16;
    adv_fields->num_uuids16++;
    adv_fields->uuids16_is_complete = 1;

    adv_fields->svc_data_uuid16 = ble_eddystone_svc_data;
    adv_fields->svc_data_uuid16_len = svc_data_len +
                                      BLE_EDDYSTONE_SVC_DATA_BASE_SZ;

    rc = ble_gap_adv_set_fields(adv_fields);
    if (rc != 0) {
        return rc;
    }

    return 0;
}

/**
 * Configures the device to advertise eddystone UID beacons.
 *
 * @param adv_fields            The base advertisement fields to transform into
 *                                  an eddystone beacon.  All configured fields
 *                                  are preserved; you probably want to clear
 *                                  this struct before calling this function.
 * @param uid                   The 16-byte UID to advertise.
 *
 * @return                      0 on success;
 *                              BLE_HS_EBUSY if advertising is in progress;
 *                              BLE_HS_EMSGSIZE if the specified data is too
 *                                  large to fit in an advertisement;
 *                              Other nonzero on failure.
 */
int
ble_eddystone_set_adv_data_uid(struct ble_hs_adv_fields *adv_fields, void *uid)
{
    uint8_t *svc_data;
    int8_t tx_pwr;
    int rc;

    /* Eddystone UUID and frame type (0). */
    svc_data = ble_eddystone_set_svc_data_base(BLE_EDDYSTONE_FRAME_TYPE_UID);

    /* Ranging data (Calibrated tx power at 0 meters). */
    rc = ble_hs_hci_util_read_adv_tx_pwr(&tx_pwr);
    if (rc != 0) {
        return rc;
    }
    svc_data[0] = tx_pwr;

    /* UID. */
    memcpy(svc_data + 1, uid, 16);

    /* Reserved. */
    svc_data[17] = 0x00;
    svc_data[18] = 0x00;

    rc = ble_eddystone_set_adv_data_gen(adv_fields, 19);
    if (rc != 0) {
        return rc;
    }

    return 0;
}

/**
 * Configures the device to advertise eddystone URL beacons.
 *
 * @param adv_fields            The base advertisement fields to transform into
 *                                  an eddystone beacon.  All configured fields
 *                                  are preserved; you probably want to clear
 *                                  this struct before calling this function.
 * @param url_scheme            The prefix of the URL; one of the
 *                                  BLE_EDDYSTONE_URL_SCHEME values.
 * @param url_body              The middle of the URL.  Don't include the
 *                                  suffix if there is a suitable suffix code.
 * @param url_body_len          The string length of the url_body argument.
 * @param url_suffix            The suffix of the URL; one of the
 *                                  BLE_EDDYSTONE_URL_SUFFIX values; use
 *                                  BLE_EDDYSTONE_URL_SUFFIX_NONE if the suffix
 *                                  is embedded in the body argument.
 *
 * @return                      0 on success;
 *                              BLE_HS_EBUSY if advertising is in progress;
 *                              BLE_HS_EMSGSIZE if the specified data is too
 *                                  large to fit in an advertisement;
 *                              Other nonzero on failure.
 */
int
ble_eddystone_set_adv_data_url(struct ble_hs_adv_fields *adv_fields,
                               uint8_t url_scheme, char *url_body,
                               uint8_t url_body_len, uint8_t url_suffix)
{
    uint8_t *svc_data;
    int8_t tx_pwr;
    int url_len;
    int rc;

    url_len = url_body_len;
    if (url_suffix != BLE_EDDYSTONE_URL_SUFFIX_NONE) {
        url_len++;
    }
    if (url_len > BLE_EDDYSTONE_URL_MAX_LEN) {
        return BLE_HS_EINVAL;
    }

    svc_data = ble_eddystone_set_svc_data_base(BLE_EDDYSTONE_FRAME_TYPE_URL);
    
    rc = ble_hs_hci_util_read_adv_tx_pwr(&tx_pwr);
    if (rc != 0) {
        return rc;
    }
    svc_data[0] = tx_pwr;
    svc_data[1] = url_scheme;
    memcpy(svc_data + 2, url_body, url_body_len);
    if (url_suffix != BLE_EDDYSTONE_URL_SUFFIX_NONE) {
        svc_data[2 + url_body_len] = url_suffix;
    }

    rc = ble_eddystone_set_adv_data_gen(adv_fields, url_len + 2);
    if (rc != 0) {
        return rc;
    }

    return 0;
}
