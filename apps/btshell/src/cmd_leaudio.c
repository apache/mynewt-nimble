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

#include "cmd_leaudio.h"
#include "btshell.h"
#include "console/console.h"
#include "shell/shell.h"
#include "bsp/bsp.h"
#include "errno.h"

#define STR_NULL        "null"

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

#define BROADCAST_SINK_PA_SYNC_TIMEOUT_DEFAULT  0x07D0

static int
broadcast_sink_pa_sync_params_get(struct ble_gap_periodic_sync_params *params)
{
    params->skip = 0;
    params->sync_timeout = BROADCAST_SINK_PA_SYNC_TIMEOUT_DEFAULT;
    params->reports_disabled = false;

    return 0;
}

static void
codec_specific_config_printf(const struct ble_audio_codec_id *unused, const uint8_t *data,
                             uint8_t len)
{
    console_printf("data=%p len=%u\n", data, len);
}

static void
base_bis_printf(const struct ble_audio_codec_id *codec_id, const struct ble_audio_base_bis *bis)
{
    console_printf("BISCodecConfig:\n\t");
    codec_specific_config_printf(codec_id,bis->codec_spec_config, bis->codec_spec_config_len);
}

static void
metadata_printf(const uint8_t *data, uint8_t len)
{
    console_printf("data=%p len=%u\n", data, len);
}

static void
base_subgroup_printf(uint8_t subgroup_index, const struct ble_audio_base_subgroup *subgroup)
{
    console_printf("subgroup_index=%u\n", subgroup_index);
    console_printf("Codec ID:\n\tformat=0x%02x company_id=0x%04x vendor_specific=0x%02x\n",
                   subgroup->codec_id.format, subgroup->codec_id.company_id,
                   subgroup->codec_id.vendor_specific);
    console_printf("SubgroupCodecConfig:\n\t");
    codec_specific_config_printf(&subgroup->codec_id,
                                 subgroup->codec_spec_config,
                                 subgroup->codec_spec_config_len);
    console_printf("Metadata:\n\t");
    metadata_printf(subgroup->metadata, subgroup->metadata_len);
}

static int
broadcast_sink_disc_start(const struct ble_gap_ext_disc_params *params)
{
    uint8_t own_addr_type;
    int rc;

    /* Figure out address to use while scanning. */
    rc = ble_hs_id_infer_auto(0, &own_addr_type);
    if (rc != 0) {
        console_printf("determining own address type failed (%d)", rc);
        assert(0);
    }

    rc = ble_gap_ext_disc(own_addr_type, 0, 0, 0, 0, 0, params, NULL, NULL, NULL);
    if (rc != 0) {
        console_printf("ext disc failed (%d)", rc);
    }

    return rc;
}

static int
broadcast_sink_disc_stop(void)
{
    int rc;

    rc = ble_gap_disc_cancel();
    if (rc != 0) {
        console_printf("disc cancel failed (%d)", rc);
    }

    return rc;
}

static int
broadcast_sink_action_fn(struct ble_audio_broadcast_sink_action *action, void *arg)
{
    switch (action->type) {
    case BLE_AUDIO_BROADCAST_SINK_ACTION_PA_SYNC:
        console_printf("PA Sync:\n");
        return broadcast_sink_pa_sync_params_get(action->pa_sync.out_params);
    case BLE_AUDIO_BROADCAST_SINK_ACTION_BIG_SYNC:
        console_printf("BIG Sync:\nsource_id=0x%02x iso_interval=0x%04x"
                       " presentation_delay=%u[us]\n",
                       action->big_sync.source_id, action->big_sync.iso_interval,
                       action->big_sync.presentation_delay);
        break;
    case BLE_AUDIO_BROADCAST_SINK_ACTION_BIS_SYNC:
        console_printf("BIS Sync:\n\tsource_id=0x%02x bis_index=0x%02x\n",
                       action->bis_sync.source_id, action->bis_sync.bis->index);
        base_subgroup_printf(action->bis_sync.subgroup_index, action->bis_sync.subgroup);
        base_bis_printf(&action->bis_sync.subgroup->codec_id, action->bis_sync.bis);
        return 0;
    case BLE_AUDIO_BROADCAST_SINK_ACTION_DISC_START:
        return broadcast_sink_disc_start(action->disc_start.params_preferred);
    case BLE_AUDIO_BROADCAST_SINK_ACTION_DISC_STOP:
        return broadcast_sink_disc_stop();
    default:
        assert(false);
        return ENOTSUP;
    }

    return 0;
}


static const struct shell_param cmd_leaudio_broadcast_sink_start_params[] = {
    {"source_id", "usage: =<UINT8>"},
    {"broadcast_code", "usage: =[string], default: NULL"},
    {NULL, NULL}
};

#if MYNEWT_VAL(SHELL_CMD_HELP)
const struct shell_cmd_help cmd_leaudio_broadcast_sink_start_help = {
    .summary = "Start audio Broadcast Sink",
    .usage = NULL,
    .params = cmd_leaudio_broadcast_sink_start_params
};
#endif

int
cmd_leaudio_broadcast_sink_start(int argc, char **argv)
{
    struct ble_audio_broadcast_sink_add_params params = {0};
    char *broadcast_code;
    uint8_t source_id;
    int rc;

    rc = parse_arg_init(argc - 1, argv + 1);
    if (rc != 0) {
        return rc;
    }

    source_id = parse_arg_uint8("source_id", &rc);
    if (rc != 0) {
        console_printf("invalid 'source_id' parameter\n");
        return rc;
    }

    broadcast_code = parse_arg_extract("broadcast_code");
    if (broadcast_code != NULL) {
        strncpy((char *)params.broadcast_code, broadcast_code, BLE_AUDIO_BROADCAST_CODE_SIZE);
        params.broadcast_code_is_valid = true;
    }

    rc = ble_audio_broadcast_sink_start(source_id, &params);
    if (rc != 0) {
        console_printf("start failed (%d)\n", rc);
    }

    return rc;
}

static const struct shell_param cmd_leaudio_broadcast_sink_stop_params[] = {
    {"source_id", "usage: =<UINT8>"},
    {NULL, NULL}
};

#if MYNEWT_VAL(SHELL_CMD_HELP)
const struct shell_cmd_help cmd_leaudio_broadcast_sink_stop_help = {
    .summary = "Stop audio Broadcast Sink",
    .usage = NULL,
    .params = cmd_leaudio_broadcast_sink_stop_params
};
#endif

int
cmd_leaudio_broadcast_sink_stop(int argc, char **argv)
{
    uint8_t source_id;
    int rc;

    rc = parse_arg_init(argc - 1, argv + 1);
    if (rc != 0) {
        return rc;
    }

    source_id = parse_arg_uint8("source_id", &rc);
    if (rc != 0) {
        console_printf("invalid 'source_id' parameter\n");
        return rc;
    }

    rc = ble_audio_broadcast_sink_stop(source_id);
    if (rc != 0) {
        console_printf("stop failed (%d)\n", rc);
    }

    return rc;
}

static const struct shell_param cmd_leaudio_broadcast_sink_metadata_update_params[] = {
    {"source_id", "usage: =<UINT8>"},
    {"subgroup_index", "usage: =<UINT8>"},
    {"metadata", "usage: =[XX:XX...]"},
    {NULL, NULL}
};

#if MYNEWT_VAL(SHELL_CMD_HELP)
const struct shell_cmd_help cmd_leaudio_broadcast_sink_metadata_update_help = {
    .summary = "Update Broadcast Sink metadata",
    .usage = NULL,
    .params = cmd_leaudio_broadcast_sink_metadata_update_params
};
#endif

int
cmd_leaudio_broadcast_sink_metadata_update(int argc, char **argv)
{
    struct ble_audio_broadcast_sink_metadata_update_params params = {0};
    static bssnz_t uint8_t metadata[UINT8_MAX];
    unsigned int metadata_len;
    uint8_t source_id;
    int rc;

    rc = parse_arg_init(argc - 1, argv + 1);
    if (rc != 0) {
        return rc;
    }

    source_id = parse_arg_uint8("source_id", &rc);
    if (rc != 0) {
        console_printf("invalid 'source_id' parameter\n");
        return rc;
    }

    params.subgroup_index = parse_arg_uint8("subgroup_index", &rc);
    if (rc != 0) {
        console_printf("invalid 'subgroup_index' parameter\n");
        return rc;
    }

    rc = parse_arg_byte_stream("metadata", UINT8_MAX, metadata, &metadata_len);
    if (rc == 0) {
        params.metadata = metadata;
        params.metadata_length = metadata_len;
    } else if (rc != ENOENT) {
        console_printf("invalid 'metadata' parameter\n");
        return rc;
    }

    rc = ble_audio_broadcast_sink_metadata_update(source_id, &params);
    if (rc != 0) {
        console_printf("metadata update failed (%d)\n", rc);
    }

    return rc;
}

static int
broadcast_sink_audio_event_handler(struct ble_audio_event *event, void *arg)
{
    switch (event->type) {
    case BLE_AUDIO_EVENT_BROADCAST_SINK_PA_SYNC_STATE:
        console_printf("source_id=0x%02x PA sync: %s\n",
                       event->broadcast_sink_pa_sync_state.source_id,
                       ble_audio_broadcast_sink_sync_state_str(
                               event->broadcast_sink_pa_sync_state.state));
        break;
    case BLE_AUDIO_EVENT_BROADCAST_SINK_BIS_SYNC_STATE:
        console_printf("source_id=0x%02x bis_index=0x%02x BIS sync: %s\n",
                       event->broadcast_sink_bis_sync_state.source_id,
                       event->broadcast_sink_bis_sync_state.bis_index,
                       ble_audio_broadcast_sink_sync_state_str(
                               event->broadcast_sink_bis_sync_state.state));
        if (event->broadcast_sink_bis_sync_state.state ==
            BLE_AUDIO_BROADCAST_SINK_SYNC_STATE_ESTABLISHED) {
            console_printf("conn_handle=0x%04x\n",
                           event->broadcast_sink_bis_sync_state.conn_handle);
        }
        break;
    default:
        break;
    }

    return 0;
}
#endif /* BLE_AUDIO_BROADCAST_SINK */

#if MYNEWT_VAL(BLE_AUDIO_SCAN_DELEGATOR)
#include "audio/ble_audio_scan_delegator.h"

static void
scan_delegator_source_desc_printf(const struct ble_audio_scan_delegator_source_desc *source_desc)
{
    console_printf("broadcast_id=0x%6x adv_sid=%d adv_addr_type=%s adv_addr=",
                   source_desc->broadcast_id, source_desc->adv_sid,
                   cmd_addr_type_str(source_desc->addr.type));
    print_addr(source_desc->addr.val);
    console_printf("\n");
}

static void
scan_delegator_sync_opt_printf(const struct ble_audio_scan_delegator_sync_opt *sync_opt)
{
    console_printf("pa_sync=%d pa_interval=0x%04x num_subgroups=%d",
                   sync_opt->pa_sync, sync_opt->pa_interval, sync_opt->num_subgroups);
    for (uint8_t i = 0; i < sync_opt->num_subgroups; i++) {
        console_printf("\n\tbis_sync=0x%04x metadata_length=%d metadata=",
                       sync_opt->subgroups[i].bis_sync, sync_opt->subgroups[i].metadata_length);
        print_bytes(sync_opt->subgroups[i].metadata, sync_opt->subgroups[i].metadata_length);
    }
    console_printf("\n");
}

static int
scan_delegator_pick_source_id_to_swap(uint8_t *out_source_id_to_swap)
{
    /* TODO: Add some logic here */
    *out_source_id_to_swap = 0;

    return 0;
}

static int
scan_delegator_action_fn(struct ble_audio_scan_delegator_action *action, void *arg)
{
    switch (action->type) {
    case BLE_AUDIO_SCAN_DELEGATOR_ACTION_SOURCE_ADD:
        console_printf("Source Add:\nsource_id=%u\n", action->source_add.source_id);
        scan_delegator_source_desc_printf(&action->source_add.source_desc);
        scan_delegator_sync_opt_printf(&action->source_add.sync_opt);
        if (action->source_add.out_source_id_to_swap == NULL) {
            return 0;
        } else {
            return scan_delegator_pick_source_id_to_swap(action->source_add.out_source_id_to_swap);
        }
    case BLE_AUDIO_SCAN_DELEGATOR_ACTION_SOURCE_MODIFY:
        console_printf("Source Modify:\nsource_id=%u\n", action->source_modify.source_id);
        scan_delegator_sync_opt_printf(&action->source_modify.sync_opt);
        break;
    case BLE_AUDIO_SCAN_DELEGATOR_ACTION_SOURCE_REMOVE:
        console_printf("Source Remove:\nsource_id=%u\n", action->source_remove.source_id);
        break;
    default:
        assert(false);
        return ENOTSUP;
    }

    return 0;
}

static const struct shell_param cmd_leaudio_scan_delegator_receive_state_add_params[] = {
    {"addr_type", "usage: =[public|random], default: public"},
    {"addr", "usage: =[XX:XX:XX:XX:XX:XX]"},
    {"broadcast_id", "usage: =[0-0xFFFFFF]"},
    {"adv_sid", "usage: =[UINT8], default: 0"},
    {NULL, NULL}
};

#if MYNEWT_VAL(SHELL_CMD_HELP)
const struct shell_cmd_help cmd_leaudio_scan_delegator_receive_state_add_help = {
    .summary = "Add receive state",
    .usage = NULL,
    .params = cmd_leaudio_scan_delegator_receive_state_add_params
};
#endif /* SHELL_CMD_HELP */

int
cmd_leaudio_scan_delegator_receive_state_add(int argc, char **argv)
{
    struct ble_audio_scan_delegator_receive_state_add_params params = {0};
    uint8_t source_id;
    int rc;

    rc = parse_arg_init(argc - 1, argv + 1);
    if (rc != 0) {
        return rc;
    }

    rc = cmd_parse_addr(NULL, &params.source_desc.addr);
    if (rc != 0) {
        console_printf("invalid 'adv_addr' parameter\n");
        return rc;
    }

    params.source_desc.broadcast_id = parse_arg_uint32("broadcast_id", &rc);
    if (rc != 0) {
        console_printf("invalid 'broadcast_id' parameter\n");
        return rc;
    }

    params.source_desc.adv_sid = parse_arg_uint8_dflt("adv_sid", 0, &rc);
    if (rc != 0) {
        console_printf("invalid 'adv_sid' parameter\n");
        return rc;
    }

    rc = ble_audio_scan_delegator_receive_state_add(&params, &source_id);
    if (rc != 0) {
        console_printf("Failed to add receive state (%d)\n", rc);
    } else {
        console_printf("New source_id=%u created\n", source_id);
    }

    return rc;
}

static const struct shell_param cmd_leaudio_scan_delegator_receive_state_remove_params[] = {
    {"source_id", "usage: =<UINT8>"},
    {NULL, NULL}
};

#if MYNEWT_VAL(SHELL_CMD_HELP)
const struct shell_cmd_help cmd_leaudio_scan_delegator_receive_state_remove_help = {
    .summary = "Remove receive state",
    .usage = NULL,
    .params = cmd_leaudio_scan_delegator_receive_state_remove_params
};
#endif /* SHELL_CMD_HELP */

int
cmd_leaudio_scan_delegator_receive_state_remove(int argc, char **argv)
{
    uint8_t source_id;
    int rc;

    rc = parse_arg_init(argc - 1, argv + 1);
    if (rc != 0) {
        return rc;
    }

    source_id = parse_arg_uint8("source_id", &rc);
    if (rc != 0) {
        console_printf("invalid 'source_id' parameter\n");
        return rc;
    }

    rc = ble_audio_scan_delegator_receive_state_remove(source_id);
    if (rc != 0) {
        console_printf("remove failed (%d)\n", rc);
    }

    return rc;
}

const struct parse_arg_kv_pair cmd_pa_sync_type[] = {
    { "not_synced",    BLE_AUDIO_SCAN_DELEGATOR_PA_SYNC_STATE_NOT_SYNCED },
    { "sync_info_req", BLE_AUDIO_SCAN_DELEGATOR_PA_SYNC_STATE_SYNC_INFO_REQ },
    { "synced",        BLE_AUDIO_SCAN_DELEGATOR_PA_SYNC_STATE_SYNCED },
    { "failed",        BLE_AUDIO_SCAN_DELEGATOR_PA_SYNC_STATE_ERROR },
    { "no_past",       BLE_AUDIO_SCAN_DELEGATOR_PA_SYNC_STATE_NO_PAST },
    { NULL }
};

const struct parse_arg_kv_pair cmd_big_enc_type[] = {
    { "not_encrypted", BLE_AUDIO_SCAN_DELEGATOR_BIG_ENC_NONE },
    { "code_req",      BLE_AUDIO_SCAN_DELEGATOR_BIG_ENC_BROADCAST_CODE_MISSING },
    { "decrypting",    BLE_AUDIO_SCAN_DELEGATOR_BIG_ENC_DECRYPTING },
    { "bad_code",      BLE_AUDIO_SCAN_DELEGATOR_BIG_ENC_BROADCAST_CODE_INVALID },
    { NULL }
};

static const struct shell_param cmd_leaudio_scan_delegator_receive_state_set_params[] = {
    {"source_id", "usage: =<UINT8>"},
    {"pa_sync_state", "usage: =[not_synced|sync_info_req|synced|failed|no_past],"
     " default: not_synced"},
    {"big_enc", "usage: =[not_encrypted|code_req|decrypting|bad_code],"
     " default: not_encrypted"},
    {"bad_code", "usage: =[string], default: NULL"},
    {NULL, NULL}
};

#if MYNEWT_VAL(SHELL_CMD_HELP)
const struct shell_cmd_help cmd_leaudio_scan_delegator_receive_state_set_help = {
    .summary = "Set receive state",
    .usage = NULL,
    .params = cmd_leaudio_scan_delegator_receive_state_set_params
};
#endif /* SHELL_CMD_HELP */

int
cmd_leaudio_scan_delegator_receive_state_set(int argc, char **argv)
{
    struct ble_audio_scan_delegator_receive_state state = {0};
    char *bad_code;
    uint8_t source_id;
    int rc;

    rc = parse_arg_init(argc - 1, argv + 1);
    if (rc != 0) {
        return rc;
    }

    source_id = parse_arg_uint8("source_id", &rc);
    if (rc != 0) {
        console_printf("invalid 'source_id' parameter\n");
        return rc;
    }

    state.pa_sync_state = parse_arg_kv_dflt("pa_sync_state", cmd_pa_sync_type,
                                            BLE_AUDIO_SCAN_DELEGATOR_PA_SYNC_STATE_NOT_SYNCED, &rc);
    if (rc != 0) {
        console_printf("invalid 'pa_sync_state' parameter\n");
        return rc;
    }

    state.big_enc = parse_arg_kv_dflt("big_enc", cmd_big_enc_type,
                                      BLE_AUDIO_SCAN_DELEGATOR_BIG_ENC_NONE, &rc);
    if (rc != 0) {
        console_printf("invalid 'big_enc' parameter\n");
        return rc;
    }

    bad_code = parse_arg_extract("bad_code");
    if (bad_code != NULL) {
        strncpy((char *)state.bad_code, bad_code, BLE_AUDIO_BROADCAST_CODE_SIZE);
    }

    /* TODO: initialize state.subgroups  */
    state.num_subgroups = 0;

    rc = ble_audio_scan_delegator_receive_state_set(source_id, &state);
    if (rc != 0) {
        console_printf("set failed (%d)\n", rc);
    }

    return rc;
}

static const char *
pa_sync_type_str(enum ble_audio_scan_delegator_pa_sync_state pa_sync_state)
{
    for (size_t i = 0; i < ARRAY_SIZE(cmd_pa_sync_type); i++) {
        if (cmd_pa_sync_type[i].val == pa_sync_state) {
            return cmd_pa_sync_type[i].key;
        }
    }

    return STR_NULL;
}

static const char *
big_enc_type_str(enum ble_audio_scan_delegator_big_enc big_enc)
{
    for (size_t i = 0; i < ARRAY_SIZE(cmd_big_enc_type); i++) {
        if (cmd_big_enc_type[i].val == big_enc) {
            return cmd_big_enc_type[i].key;
        }
    }

    return STR_NULL;
}

static void
scan_delegator_receive_state_printf(const struct ble_audio_scan_delegator_receive_state *state)
{
    console_printf("pa_sync_state=%s big_enc=%s num_subgroups=%d",
                   pa_sync_type_str(state->pa_sync_state), big_enc_type_str(state->big_enc),
                   state->num_subgroups);

    if (state->big_enc == BLE_AUDIO_SCAN_DELEGATOR_BIG_ENC_BROADCAST_CODE_INVALID) {
        console_printf("bad_code=");
        print_bytes(state->bad_code, sizeof(state->bad_code));
        console_printf("\n");
    }

    for (uint8_t i = 0; i < state->num_subgroups; i++) {
        console_printf("\n\tbis_sync=0x%04x metadata_length=%d metadata=",
                       state->subgroups[i].bis_sync, state->subgroups[i].metadata_length);
        print_bytes(state->subgroups[i].metadata, state->subgroups[i].metadata_length);
        console_printf("\n");
    }
}

static const struct shell_param cmd_leaudio_scan_delegator_receive_state_get_params[] = {
    {"source_id", "usage: =<UINT8>"},
    {NULL, NULL}
};

#if MYNEWT_VAL(SHELL_CMD_HELP)
const struct shell_cmd_help cmd_leaudio_scan_delegator_receive_state_get_help = {
    .summary = "Get receive state",
    .usage = NULL,
    .params = cmd_leaudio_scan_delegator_receive_state_get_params
};
#endif /* SHELL_CMD_HELP */

int
cmd_leaudio_scan_delegator_receive_state_get(int argc, char **argv)
{
    struct ble_audio_scan_delegator_receive_state state = {0};
    uint8_t source_id;
    int rc;

    rc = parse_arg_init(argc - 1, argv + 1);
    if (rc != 0) {
        return rc;
    }

    source_id = parse_arg_uint8("source_id", &rc);
    if (rc != 0) {
        console_printf("invalid 'source_id' parameter\n");
        return rc;
    }

    rc = ble_audio_scan_delegator_receive_state_get(source_id, &state);
    if (rc != 0) {
        console_printf("get failed (%d)\n", rc);
    } else {
        console_printf("source_id=%u\n", source_id);
        scan_delegator_receive_state_printf(&state);
    }

    return rc;
}

static int
scan_delegator_receive_state_foreach_fn(struct ble_audio_scan_delegator_receive_state_entry *entry,
                                        void *arg)
{
    console_printf("source_id=%u\n", entry->source_id);
    scan_delegator_source_desc_printf(&entry->source_desc);
    scan_delegator_receive_state_printf(&entry->state);

    return 0;
}

#if MYNEWT_VAL(SHELL_CMD_HELP)
const struct shell_cmd_help cmd_leaudio_scan_delegator_receive_state_show_help = {
    .summary = "List receive states",
    .usage = NULL,
    .params = NULL
};
#endif /* SHELL_CMD_HELP */

int
cmd_leaudio_scan_delegator_receive_state_show(int argc, char **argv)
{
    uint8_t num_entries = 0;

    ble_audio_scan_delegator_receive_state_foreach(scan_delegator_receive_state_foreach_fn,
                                                   &num_entries);
    if (num_entries == 0) {
        console_printf("No receive state\n");
    }

    return 0;
}

static int
scan_delegator_audio_event_handler(struct ble_audio_event *event, void *arg)
{
    switch (event->type) {
    case BLE_AUDIO_EVENT_BROADCAST_ANNOUNCEMENT:
        console_printf("Broadcast Announcement\n");
        console_printf("broadcast_id=0x%6x adv_sid=%d addr_type=%s addr=",
                       event->broadcast_announcement.broadcast_id,
                       event->broadcast_announcement.ext_disc->sid,
                       cmd_addr_type_str(event->broadcast_announcement.ext_disc->addr.type));
        print_addr(event->broadcast_announcement.ext_disc->addr.val);
        console_printf("\n");
        break;
    default:
        break;
    }

    return 0;
}
#endif /* BLE_AUDIO_SCAN_DELEGATOR */

void
btshell_leaudio_init(void)
{
    int rc = 0;

#if (MYNEWT_VAL(BLE_AUDIO_SCAN_DELEGATOR))
    static struct ble_audio_event_listener scan_delegator_listener;

    rc = ble_audio_scan_delegator_action_fn_set(scan_delegator_action_fn, NULL);
    assert(rc == 0);

    rc = ble_audio_event_listener_register(&scan_delegator_listener,
                                           scan_delegator_audio_event_handler, NULL);
    assert(rc == 0);
#endif /* BLE_AUDIO_SCAN_DELEGATOR */

#if (MYNEWT_VAL(BLE_AUDIO_BROADCAST_SINK))
    static struct ble_audio_event_listener broadcast_sink_listener;

    rc = ble_audio_broadcast_sink_cb_set(broadcast_sink_action_fn, NULL);
    assert(rc == 0);

    rc = ble_audio_event_listener_register(&broadcast_sink_listener,
                                           broadcast_sink_audio_event_handler, NULL);
#endif /* BLE_AUDIO_BROADCAST_SINK */
    assert(rc == 0);
}
