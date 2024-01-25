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

#include "audio/ble_audio.h"
#include "cmd_leaudio.h"
#include "btshell.h"
#include "console/console.h"
#include "shell/shell.h"
#include "errno.h"

#if (MYNEWT_VAL(BLE_ISO_BROADCAST_SOURCE))
#include "audio/ble_audio_broadcast_source.h"
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
#endif /* BLE_ISO_BROADCAST_SOURCE */

#if (MYNEWT_VAL(BLE_AUDIO_BROADCAST_SINK))
#include "audio/ble_audio_broadcast_sink.h"

static struct ble_audio_event_listener audio_event_listener;

static const char *
addr_type_str(uint8_t type)
{
    const struct parse_arg_kv_pair *kvs = cmd_addr_type;
    int i;

    for (i = 0; kvs[i].key != NULL; i++) {
        if (type == kvs[i].val) {
            return kvs[i].key;
        }
    }

    return "unknown";
}

static void
print_codec_id(struct ble_audio_codec_id *codec_id)
{
    console_printf(" codec_id={0x%02x, 0x%04x, 0x%04x}",
                   codec_id->format, codec_id->company_id, codec_id->vendor_specific);
}

static int
audio_event_handler(struct ble_audio_event *event, void *arg)
{
    int rc;

    switch (event->type) {
    case BLE_AUDIO_EVENT_BROADCAST_ANNOUNCEMENT: {
        struct ble_audio_event_broadcast_announcement *ev;

        ev = &event->broadcast_announcement;

        console_printf("BCST: broadcast_id=0x%6x sid=%d pa_interval=0x%04x"
                       " adv_addr_type=%s adv_addr=",
                       ev->broadcast_id, ev->ext_disc->sid,
                       ev->ext_disc->periodic_adv_itvl,
                       addr_type_str(ev->ext_disc->addr.type));
        print_addr(ev->ext_disc->addr.val);
        console_printf("\n");

        if (ev->svc_data_len > 0) {
            console_printf("    svc_data=");
            print_bytes(ev->svc_data, ev->svc_data_len);
            console_printf("\n");
        }

        if (ev->pub_announcement_data != NULL) {
            console_printf("    pba_features=0x%02x\n",
                           ev->pub_announcement_data->features);
            if (ev->pub_announcement_data->metadata_len > 0) {
                console_printf("    metadata=");
                print_bytes(ev->pub_announcement_data->metadata,
                            ev->pub_announcement_data->metadata_len);
                console_printf("\n");
            }
        }

        if (ev->name != NULL) {
            console_printf("    name=%.*s\n", ev->name->name_len, ev->name->name);
        }

        break;
    }

    case BLE_AUDIO_EVENT_BROADCAST_SINK_BASE_REPORT: {
        struct ble_audio_event_broadcast_sink_base_report *ev;
        struct ble_audio_base_iter subgroup_iter;
        struct ble_audio_base_group group;

        ev = &event->base_report;

        console_printf("BASE: tx_power=%d rssi=%d base_len=%d\n",
                       ev->tx_power, ev->rssi, ev->base_length);

        rc = ble_audio_base_parse(ev->base, ev->base_length, &group, &subgroup_iter);
        if (rc != 0){
            console_printf("Failed to parse BASE (%d)\n", rc);
            return rc;
        }

        console_printf(" presentation_delay=%d num_sub=%d\n",
                       group.presentation_delay, group.num_subgroups);

        for (uint8_t i = 0; i < group.num_subgroups; i++) {
            struct ble_audio_base_subgroup subgroup;
            struct ble_audio_base_iter bis_iter;

            rc = ble_audio_base_subgroup_iter(&subgroup_iter, &subgroup, &bis_iter);
            if (rc != 0){
                console_printf("Failed to parse BASE subgroup (%d)\n", rc);
                return rc;
            }

            print_codec_id(&subgroup.codec_id);
            console_printf("\n");

            if (subgroup.codec_spec_config_len > 0) {
                console_printf("    config=");
                print_bytes(subgroup.codec_spec_config, subgroup.codec_spec_config_len);
                console_printf("\n");
            }

            if (subgroup.metadata_len > 0) {
                console_printf("    metadata=");
                print_bytes(subgroup.metadata, subgroup.metadata_len);
                console_printf("\n");
            }

            console_printf(" num_bis=%d \n", subgroup.num_bis);

            for (uint8_t j = 0; j < subgroup.num_bis; j++) {
                struct ble_audio_base_bis bis;

                rc = ble_audio_base_bis_iter(&bis_iter, &bis);
                if (rc != 0){
                    console_printf("Failed to parse BASE subgroup (%d)\n", rc);
                    return rc;
                }

                console_printf(" bis_index=0x%02x \n", bis.index);

                if (bis.codec_spec_config_len > 0) {
                    console_printf("    config=");
                    print_bytes(bis.codec_spec_config, bis.codec_spec_config_len);
                    console_printf("\n");
                }
            }
        }

        break;
    }

    default:
        break;
    }

    return 0;
}

#if MYNEWT_VAL(SHELL_CMD_HELP)
static const struct shell_param cmd_leaudio_broadcast_sink_create_params[] = {
        {"adv_addr_type", "usage: =[public|random], default: public"},
        {"adv_addr", "usage: =[XX:XX:XX:XX:XX:XX]"},
        {"broadcast_id", "usage: =[0-0xFFFFFF]"},
        {"sid", "usage: =[UINT8], default: 0"},
        { NULL, NULL}
};

const struct shell_cmd_help cmd_leaudio_broadcast_sink_create_help = {
        .summary = "Create Broadcast Sink",
        .usage = NULL,
        .params = cmd_leaudio_broadcast_sink_create_params
};
#endif

int
cmd_leaudio_broadcast_sink_create(int argc, char **argv)
{
    struct ble_audio_broadcast_sink_create_params params = { 0 };
    ble_addr_t adv_addr;
    uint8_t instance_id;
    int rc;

    rc = parse_arg_init(argc - 1, argv + 1);
    if (rc != 0) {
        return rc;
    }

    rc = parse_dev_addr("adv_", cmd_addr_type, &adv_addr);
    if (rc != 0) {
        console_printf("invalid 'adv_addr' parameter\n");
        return rc;
    }

    params.adv_addr = &adv_addr;

    params.broadcast_id = parse_arg_uint32("broadcast_id", &rc);
    if (rc != 0) {
        return rc;
    }

    params.adv_sid = parse_arg_uint8("sid", &rc);
    if (rc != 0) {
        console_printf("invalid sid parameter\n");
        return rc;
    }

    params.cb = audio_event_handler;

    rc = ble_audio_broadcast_sink_create(&params, &instance_id);
    if (rc != 0) {
        console_printf("Failed to create Audio Broadcast Sink instance (%d)\n", rc);
    } else {
        console_printf("Broadcast Sink instance_id %u created\n", instance_id);
    }

    return rc;
}

#if MYNEWT_VAL(SHELL_CMD_HELP)
static const struct shell_param cmd_leaudio_broadcast_sink_destroy_params[] = {
        {"instance_id", "usage: =<UINT8>"},
        { NULL, NULL}
};

const struct shell_cmd_help cmd_leaudio_broadcast_sink_destroy_help = {
        .summary = "Destroy Broadcast Sink",
        .usage = NULL,
        .params = cmd_leaudio_broadcast_sink_destroy_params
};
#endif

int
cmd_leaudio_broadcast_sink_destroy(int argc, char **argv)
{
    uint8_t instance_id;
    int rc;

    rc = parse_arg_init(argc - 1, argv + 1);
    if (rc != 0) {
        return rc;
    }

    instance_id = parse_arg_uint8("instance_id", &rc);
    if (rc != 0) {
        console_printf("invalid instance_id parameter\n");
        return rc;
    }

    rc = ble_audio_broadcast_sink_destroy(instance_id);
    if (rc != 0) {
        console_printf("Failed to destroy Audio Broadcast Sink instance (%d)\n", rc);
    } else {
        console_printf("Broadcast Sink instance_id %u destroyed\n", instance_id);
    }

    return rc;
}

#if MYNEWT_VAL(SHELL_CMD_HELP)
static const struct shell_param cmd_leaudio_broadcast_sink_pa_sync_params[] = {
        {"instance_id", "usage: =<UINT8>"},
        {"skip", "usage: =[0-0x01F3], default: 0x0000"},
        {"sync_timeout", "usage: =[0x000A-0x4000], default: 0x07D0"},
        {"reports_disabled", "disable reports, usage: =[0-1], default: 0"},
        {NULL, NULL}
};

const struct shell_cmd_help cmd_leaudio_broadcast_sink_pa_sync_help = {
        .summary = "Sync to Broadcast Source PA",
        .usage = NULL,
        .params = cmd_leaudio_broadcast_sink_pa_sync_params
};
#endif

int
cmd_leaudio_broadcast_sink_pa_sync(int argc, char **argv)
{
    struct ble_gap_periodic_sync_params params = { 0 };
    uint8_t instance_id;
    int rc;

    rc = parse_arg_init(argc - 1, argv + 1);
    if (rc != 0) {
        return rc;
    }

    instance_id = parse_arg_uint8("instance_id", &rc);
    if (rc != 0) {
        console_printf("invalid instance_id parameter\n");
        return rc;
    }

    params.skip = parse_arg_uint16_dflt("skip", 0, &rc);
    if (rc != 0) {
        console_printf("invalid 'skip' parameter\n");
        return rc;
    }

    params.sync_timeout = parse_arg_time_dflt("sync_timeout", 10000, 2000, &rc);
    if (rc != 0) {
        console_printf("invalid 'sync_timeout' parameter\n");
        return rc;
    }

    params.reports_disabled = parse_arg_bool_dflt("reports_disabled", false, &rc);
    if (rc != 0) {
        console_printf("invalid 'reports_disabled' parameter\n");
        return rc;
    }

    rc = ble_audio_broadcast_sink_pa_sync(instance_id, &params);
    if (rc != 0) {
        console_printf("Failed to initiate PA Sync (%d)\n", rc);
    }

    return rc;
}

#if MYNEWT_VAL(SHELL_CMD_HELP)
static const struct shell_param cmd_leaudio_broadcast_sink_pa_sync_term_params[] = {
        {"instance_id", "usage: =<UINT8>"},
        { NULL, NULL}
};

const struct shell_cmd_help cmd_leaudio_broadcast_sink_pa_sync_term_help = {
        .summary = "Terminate PA Sync",
        .usage = NULL,
        .params = cmd_leaudio_broadcast_sink_pa_sync_term_params
};
#endif

int
cmd_leaudio_broadcast_sink_pa_sync_term(int argc, char **argv)
{
    uint8_t instance_id;
    int rc;

    rc = parse_arg_init(argc - 1, argv + 1);
    if (rc != 0) {
        return rc;
    }

    instance_id = parse_arg_uint8("instance_id", &rc);
    if (rc != 0) {
        console_printf("invalid instance_id parameter\n");
        return rc;
    }

    rc = ble_audio_broadcast_sink_pa_sync_term(instance_id);
    if (rc != 0) {
        console_printf("Failed to terminate PA Sync (%d)\n", rc);
    }

    return rc;
}

static int
iso_event_handler(struct ble_iso_event *event, void *arg)
{
    switch (event->type) {
    case BLE_ISO_EVENT_ISO_RX:
        break;

    default:
        break;
    }

    return 0;
}


#if MYNEWT_VAL(SHELL_CMD_HELP)
static const struct shell_param cmd_leaudio_broadcast_sink_big_sync_params[] = {
        {"instance_id", "usage: =<UINT8>"},
        {"bis_sync_mask", "mask of BIS indexes to sync, usage: =<UINT32>"},
        {NULL, NULL}
};

const struct shell_cmd_help cmd_leaudio_broadcast_sink_big_sync_help = {
        .summary = "Sync to Audio Broadcast",
        .usage = NULL,
        .params = cmd_leaudio_broadcast_sink_big_sync_params
};
#endif

int
cmd_leaudio_broadcast_sink_sync(int argc, char **argv)
{
    struct ble_audio_broadcast_sink_bis_params bis_params[MYNEWT_VAL(BLE_ISO_MAX_BISES)] = { 0 };
    struct ble_audio_broadcast_sink_big_sync_params big_params = {
            .bis_params = bis_params,
    };
    uint8_t bis_index = 0x01;
    uint16_t bis_sync_mask;
    uint8_t instance_id;
    int rc;

    rc = parse_arg_init(argc - 1, argv + 1);
    if (rc != 0) {
        return rc;
    }

    instance_id = parse_arg_uint8("instance_id", &rc);
    if (rc != 0) {
        console_printf("invalid instance_id parameter\n");
        return rc;
    }

    bis_sync_mask = parse_arg_uint16("bis_sync_mask", &rc);
    if (rc != 0) {
        console_printf("invalid 'bis_sync_mask' parameter\n");
        return rc;
    }

    big_params.num_bis = __builtin_popcount(bis_sync_mask);

    for (uint8_t i = 0; i < big_params.num_bis; i++) {
        while (bis_params[i].bis_index == 0) {
            if (bis_sync_mask & (1 << (bis_index - 1))) {
                bis_params[i].bis_index = bis_index;
                bis_params[i].cb = iso_event_handler;
                bis_params[i].cb_arg = NULL;
            }

            bis_index++;
        }
    }

    rc = ble_audio_broadcast_sink_big_sync(instance_id, &big_params);
    if (rc != 0) {
        console_printf("Failed to synchronize sink (%d)\n", rc);
    }

    return rc;
}

#if MYNEWT_VAL(SHELL_CMD_HELP)
static const struct shell_param cmd_leaudio_broadcast_sink_big_sync_term_params[] = {
        {"instance_id", "usage: =<UINT8>"},
        {NULL, NULL}
};

const struct shell_cmd_help cmd_leaudio_broadcast_sink_big_sync_term_help = {
        .summary = "Terminate Audio Broadcast Sync",
        .usage = NULL,
        .params = cmd_leaudio_broadcast_sink_big_sync_term_params
};
#endif

int
cmd_leaudio_broadcast_sink_big_sync_term(int argc, char **argv)
{
    uint8_t instance_id;
    int rc;

    rc = parse_arg_init(argc - 1, argv + 1);
    if (rc != 0) {
        return rc;
    }

    instance_id = parse_arg_uint8("instance_id", &rc);
    if (rc != 0) {
        console_printf("invalid instance_id parameter\n");
        return rc;
    }

    rc = ble_audio_broadcast_sink_big_sync_term(instance_id);
    if (rc != 0) {
        console_printf("Failed to terminate sink (%d)\n", rc);
    }

    return rc;
}

#endif /* BLE_AUDIO_BROADCAST_SINK */

void
btshell_audio_broadcast_sink_init(void)
{
#if (MYNEWT_VAL(BLE_AUDIO_BROADCAST_SINK))
    int rc;

    rc = ble_audio_event_listener_register(&audio_event_listener,
                                           audio_event_handler, NULL);
    assert(rc == 0);
#endif /* BLE_AUDIO_BROADCAST_SINK */
}
