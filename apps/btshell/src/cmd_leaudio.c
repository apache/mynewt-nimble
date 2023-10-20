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

#include "host/ble_audio_broadcast.h"
#include "cmd_leaudio.h"
#include "btshell.h"
#include "console/console.h"
#include "errno.h"

#if (MYNEWT_VAL(BLE_ISO_BROADCASTER))
int
cmd_leaudio_base_add(int argc, char **argv)
{
    uint32_t presentation_delay;
    uint8_t adv_instance;
    int rc;

    rc = parse_arg_init(argc - 1, argv + 1);
    if (rc != 0) {
        return rc;
    }

    adv_instance = parse_arg_uint8("adv_instance", &rc);
    if (rc != 0 || adv_instance >= BLE_ADV_INSTANCES) {
        console_printf("invalid advertising instance\n");
        return rc;
    }

    presentation_delay = parse_arg_uint32("presentation_delay", &rc);
    if (rc != 0) {
        return rc;
    }

    return btshell_broadcast_base_add(adv_instance, presentation_delay);
}

int
cmd_leaudio_big_sub_add(int argc, char **argv)
{
    uint8_t adv_instance;
    uint8_t codec_fmt;
    uint16_t company_id;
    uint16_t vendor_spec;
    static uint8_t metadata[CMD_ADV_DATA_METADATA_MAX_SZ];
    unsigned int metadata_len;
    static uint8_t codec_spec_cfg[CMD_ADV_DATA_CODEC_SPEC_CFG_MAX_SZ];
    unsigned int codec_spec_cfg_len;
    int rc;

    rc = parse_arg_init(argc - 1, argv + 1);
    if (rc != 0) {
        return rc;
    }

    adv_instance = parse_arg_uint8("adv_instance", &rc);
    if (rc != 0 || adv_instance >= BLE_ADV_INSTANCES) {
        console_printf("invalid advertising instance\n");
        return rc;
    }

    codec_fmt = parse_arg_uint8("codec_fmt", &rc);
    if (rc != 0) {
        return rc;
    }

    company_id = parse_arg_uint16("company_id", &rc);
    if (rc != 0) {
        return rc;
    }

    vendor_spec = parse_arg_uint16("vendor_spec", &rc);
    if (rc != 0) {
        return rc;
    }

    rc = parse_arg_byte_stream("codec_spec_config",
                               CMD_ADV_DATA_CODEC_SPEC_CFG_MAX_SZ,
                               codec_spec_cfg, &codec_spec_cfg_len);
    if (rc != 0 && rc != ENOENT) {
        return rc;
    }

    rc = parse_arg_byte_stream("metadata", CMD_ADV_DATA_METADATA_MAX_SZ,
                               metadata, &metadata_len);
    if (rc != 0 && rc != ENOENT) {
        return rc;
    }

    return btshell_broadcast_big_sub_add(adv_instance,
                                         codec_fmt, company_id,
                                         vendor_spec,
                                         metadata, metadata_len,
                                         codec_spec_cfg, codec_spec_cfg_len);
}

int
cmd_leaudio_bis_add(int argc, char **argv)
{
    uint8_t adv_instance;
    static uint8_t codec_spec_cfg[CMD_ADV_DATA_CODEC_SPEC_CFG_MAX_SZ];
    unsigned int codec_spec_cfg_len;
    int rc;

    rc = parse_arg_init(argc - 1, argv + 1);
    if (rc != 0) {
        return rc;
    }

    adv_instance = parse_arg_uint8("adv_instance", &rc);
    if (rc != 0 || adv_instance >= BLE_ADV_INSTANCES) {
        console_printf("invalid advertising instance\n");
        return rc;
    }

    rc = parse_arg_byte_stream("codec_spec_config",
                               CMD_ADV_DATA_CODEC_SPEC_CFG_MAX_SZ,
                               codec_spec_cfg, &codec_spec_cfg_len);
    if (rc != 0) {
        return rc;
    }

    return btshell_broadcast_bis_add(adv_instance, codec_spec_cfg,
                                     codec_spec_cfg_len);
}

int
cmd_leaudio_broadcast_create(int argc, char **argv)
{
    struct ble_iso_big_params big_params;
    uint8_t adv_instance;
    const char *name;
    uint8_t extra_data[CMD_ADV_DATA_EXTRA_MAX_SZ];
    static uint8_t own_addr_type;
    unsigned int extra_data_len;
    struct ble_gap_periodic_adv_params periodic_params;
    struct ble_gap_ext_adv_params extended_params = {
        .scannable = 0,
        .connectable = 0,
        .primary_phy = BLE_HCI_LE_PHY_1M,
    };
    int rc;

    rc = ble_hs_id_infer_auto(0, &own_addr_type);
    if (rc != 0) {
        return rc;
    }

    extended_params.own_addr_type = own_addr_type;

    rc = parse_arg_init(argc - 1, argv + 1);
    if (rc != 0) {
        return rc;
    }

    adv_instance = parse_arg_uint8("adv_instance", &rc);
    if (rc != 0 || adv_instance >= BLE_ADV_INSTANCES) {
        console_printf("invalid advertising instance\n");
        return rc;
    }

    extended_params.sid = adv_instance;
    extended_params.itvl_min = parse_arg_uint8_dflt("ext_interval_min",
                                                    0, &rc);
    if (rc != 0 && rc != ENOENT) {
        console_printf("invalid extended advertising interval (min)\n");
        return rc;
    }

    extended_params.itvl_max = parse_arg_uint8_dflt("ext_interval_max", 0,
                                                    &rc);
    if (rc != 0 && rc != ENOENT) {
        console_printf("invalid extended advertising interval (max)\n");
        return rc;
    }

    periodic_params.itvl_min = parse_arg_uint8_dflt("per_interval_min", 0,
                                                    &rc);
    if (rc != 0 && rc != ENOENT) {
        console_printf("invalid periodic advertising interval (min)\n");
        return rc;
    }

    periodic_params.itvl_max = parse_arg_uint8_dflt("per_interval_max", 0,
                                                    &rc);
    if (rc != 0 && rc != ENOENT) {
        console_printf("invalid periodic advertising interval (max)\n");
        return rc;
    }

    name = parse_arg_extract("name");

    big_params.sdu_interval = parse_arg_uint32_bounds("sdu_interval",
                                                      0x0000FF, 0x0FFFFF,
                                                      &rc);
    if (rc != 0) {
        console_printf("invalid SDU interval\n");
        return rc;
    }

    big_params.max_sdu = parse_arg_uint16_bounds("max_sdu", 0x0001, 0x0FFF,
                                           &rc);
    if (rc != 0) {
        console_printf("invalid max SDU size\n");
        return rc;
    }

    big_params.max_transport_latency = parse_arg_uint16_bounds("max_latency",
                                                               0x0005, 0x0FA0,
                                                               &rc);
    if (rc != 0) {
        console_printf("invalid max transport latency\n");
        return rc;
    }

    big_params.rtn = parse_arg_uint8_bounds("rtn", 0x00, 0x1E, &rc);
    if (rc != 0) {
        console_printf("invalid RTN\n");
        return rc;
    }

    big_params.phy = parse_arg_uint8_bounds("phy", 0, 2, &rc);
    if (rc != 0) {
        console_printf("invalid PHY\n");
        return rc;
    }

    extended_params.secondary_phy = big_params.phy;

    big_params.packing = parse_arg_uint8_bounds_dflt("packing", 0, 1, 1, &rc);
    if (rc != 0) {
        console_printf("invalid packing\n");
        return rc;
    }

    big_params.framing = parse_arg_uint8_bounds_dflt("framing", 0, 1, 0, &rc);
    if (rc != 0) {
        console_printf("invalid framing\n");
        return rc;
    }

    big_params.encryption = parse_arg_uint8_bounds_dflt("encryption", 0, 1, 0, &rc);
    if (rc != 0) {
        console_printf("invalid encryption\n");
        return rc;
    }

    if (big_params.encryption) {
        big_params.broadcast_code = parse_arg_extract("broadcast_code");
        if (big_params.broadcast_code == NULL) {
            console_printf("broadcast code missing\n");
            return ENOENT;
        }

        if (strlen(big_params.broadcast_code) > 16) {
            console_printf("broadcast code too long\n");
            return ENOENT;
        }
    }

    rc = parse_arg_byte_stream("extra_data",
                               CMD_ADV_DATA_EXTRA_MAX_SZ,
                               extra_data, &extra_data_len);
    if (rc == ENOENT) {
        extra_data_len = 0;
    } else if (rc != 0) {
        return rc;
    }

    return btshell_broadcast_create(adv_instance,
                                    &extended_params,
                                    &periodic_params,
                                    name,
                                    big_params,
                                    extra_data,
                                    extra_data_len);
}

int
cmd_leaudio_broadcast_destroy(int argc, char **argv)
{
    uint8_t adv_instance;
    int rc;

    rc = parse_arg_init(argc - 1, argv + 1);
    if (rc != 0) {
        return rc;
    }

    adv_instance = parse_arg_uint8("adv_instance", &rc);
    if (rc != 0 || adv_instance >= BLE_ADV_INSTANCES) {
        console_printf("invalid advertising instance\n");
        return rc;
    }

    return btshell_broadcast_destroy(adv_instance);
}

int
cmd_leaudio_broadcast_update(int argc, char **argv)
{
    uint8_t adv_instance;
    uint8_t extra_data[CMD_ADV_DATA_EXTRA_MAX_SZ];
    unsigned int extra_data_len;
    const char *name;
    int rc;

    rc = parse_arg_init(argc - 1, argv + 1);
    if (rc != 0) {
        return rc;
    }

    adv_instance = parse_arg_uint8("adv_instance", &rc);
    if (rc != 0 || adv_instance >= BLE_ADV_INSTANCES) {
        console_printf("invalid advertising instance\n");
        return rc;
    }

    rc = parse_arg_byte_stream("extra_data",
                               CMD_ADV_DATA_EXTRA_MAX_SZ,
                               extra_data, &extra_data_len);
    if (rc != 0 && rc != ENOENT) {
        return rc;
    }

    name = parse_arg_extract("name");

    return btshell_broadcast_update(adv_instance, name, extra_data, extra_data_len);
}

int
cmd_leaudio_broadcast_start(int argc, char **argv)
{
    uint8_t adv_instance;
    int rc;

    rc = parse_arg_init(argc - 1, argv + 1);
    if (rc != 0) {
        return rc;
    }

    adv_instance = parse_arg_uint8("adv_instance", &rc);
    if (rc != 0 || adv_instance >= BLE_ADV_INSTANCES) {
        console_printf("invalid advertising instance\n");
        return rc;
    }

    return btshell_broadcast_start(adv_instance);
}

int
cmd_leaudio_broadcast_stop(int argc, char **argv)
{
    uint8_t adv_instance;
    int rc;

    rc = parse_arg_init(argc - 1, argv + 1);
    if (rc != 0) {
        return rc;
    }

    adv_instance = parse_arg_uint8("adv_instance", &rc);
    if (rc != 0 || adv_instance >= BLE_ADV_INSTANCES) {
        console_printf("invalid advertising instance\n");
        return rc;
    }

    return btshell_broadcast_stop(adv_instance);
}
#endif
