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

#include <stdint.h>
#include <controller/ble_ll_utils.h>
#include <testutil/testutil.h>
#include "ble_ll_cs_priv.h"

int ble_ll_cs_start_req_parameters_apply(struct ble_ll_cs_proc_params *ps, uint32_t conn_itvl_us,
                                         uint32_t ce_duration_us, uint16_t subrate_base_event,
                                         uint16_t subrate_factor, uint16_t event_cntr);

int ble_ll_cs_start_rsp_parameters_apply(struct ble_ll_cs_proc_params *ps,
                                         struct ble_ll_cs_proc_params *rx_ps, uint32_t conn_itvl_us,
                                         uint32_t ce_duration_us, uint16_t subrate_factor,
                                         uint16_t event_cntr);

int ble_ll_cs_start_ind_parameters_apply(struct ble_ll_cs_proc_params *ps,
                                         struct ble_ll_cs_proc_params *rx_ps, uint32_t conn_itvl_us,
                                         uint32_t ce_duration_us, uint16_t subrate_factor,
                                         uint16_t event_cntr);

static void
ble_ll_cs_config_offset_range_test(void)
{
    int rejected;
    struct ble_ll_cs_proc_params org_ps;
    struct ble_ll_cs_proc_params loc_ps;
    struct ble_ll_cs_proc_params rem_ps;
    uint32_t conn_itvl_us = 20000;
    /* The duration of a single CE slot in scheduler */
    uint32_t ce_duration_us = 1250;
    uint16_t event_cntr = 1000;
    uint16_t subrate_factor = 1;
    uint32_t loc_offset_min = MAX(BLE_LL_CS_EVENT_OFFSET_MIN_US, ce_duration_us +
                                  BLE_LL_CS_SUBEVENT_SAFE_SPACE_FROM_CE);
    uint32_t loc_offset_max = MIN(BLE_LL_CS_EVENT_OFFSET_MAX_US, conn_itvl_us -
                                  BLE_LL_CS_SUBEVENT_LEN_MIN - BLE_LL_CS_SUBEVENT_T_MES_US);
    uint32_t offsets[][2] = {
        /* Remote offset range spanning the entire available range of values.*/
        {BLE_LL_CS_EVENT_OFFSET_MIN_US, BLE_LL_CS_EVENT_OFFSET_MAX_US},
        /* Remote offset range spanning the entire local offset range */
        {loc_offset_min, loc_offset_max},
        /* Remote offset max is smaller than the local range */
        {loc_offset_min - 234, loc_offset_min - 123},
        /* Remote offset min is larger than the local range */
        {loc_offset_max + 123, loc_offset_max + 234},
        /* Offset range overlaps the local range from the min side */
        {loc_offset_min - 123, loc_offset_min + 123},
        /* Offset range overlaps the local range from the max side */
        {loc_offset_max - 123, loc_offset_max + 123},
        /* Offset range fits within the local range */
        {loc_offset_min + 123, loc_offset_max - 123},
        /* Offset narrowed to the one point, too small */
        {loc_offset_min - 123, loc_offset_min - 123},
        /* Offset narrowed to the one point, too large */
        {loc_offset_max + 123, loc_offset_max + 123},
        /* Offset narrowed to the one point, within the local range */
        {loc_offset_min + 123, loc_offset_min + 123},
    };
    uint8_t i;

    for (i = 0; i < ARRAY_SIZE(offsets); ++i) {
        memset(&org_ps, 0, sizeof(org_ps));
        org_ps.max_procedure_count = 1;
        org_ps.anchor_conn_event_cntr = 1008;
        org_ps.max_procedure_len = 30;
        org_ps.subevent_len = BLE_LL_CS_SUBEVENT_LEN_MIN;
        org_ps.event_interval = 1;
        org_ps.subevent_interval = 0;
        org_ps.subevents_per_event = 1;
        org_ps.preferred_peer_antenna = 1;
        org_ps.phy = 1;
        org_ps.offset_min = offsets[i][0];
        org_ps.offset_max = offsets[i][1];

        memcpy(&loc_ps, &org_ps, sizeof(loc_ps));
        rejected = ble_ll_cs_start_req_parameters_apply(&loc_ps, conn_itvl_us, ce_duration_us,
                                                        event_cntr, 1, event_cntr);

        assert(rejected == 0);
        assert(IN_RANGE(loc_ps.offset_min, loc_offset_min, loc_offset_max));
        assert(IN_RANGE(loc_ps.offset_max, loc_offset_min, loc_offset_max));

        memcpy(&rem_ps, &org_ps, sizeof(rem_ps));
        rejected = ble_ll_cs_start_rsp_parameters_apply(&loc_ps, &rem_ps, conn_itvl_us, ce_duration_us,
                                                        subrate_factor, event_cntr);
        assert(rejected == 0);
        assert(IN_RANGE(loc_ps.offset_min, loc_offset_min, loc_offset_max));
        assert(IN_RANGE(loc_ps.offset_max, loc_offset_min, loc_offset_max));
    }
}

static void
ble_ll_cs_config_offset_rejection_test(void)
{
    struct ble_ll_cs_proc_params loc_ps;
    struct ble_ll_cs_proc_params rem_ps;
    uint32_t conn_itvl_us = 20000;
    /* The duration of a single CE slot in scheduler */
    uint32_t ce_duration_us = 1250;
    uint16_t event_cntr = 1000;
    uint32_t loc_offset_min = MAX(BLE_LL_CS_EVENT_OFFSET_MIN_US, ce_duration_us +
                                  BLE_LL_CS_SUBEVENT_SAFE_SPACE_FROM_CE);
    uint32_t loc_offset_max = MIN(BLE_LL_CS_EVENT_OFFSET_MAX_US, conn_itvl_us -
                                  BLE_LL_CS_SUBEVENT_LEN_MIN - BLE_LL_CS_SUBEVENT_T_MES_US);
    uint32_t offsets[] = {
        loc_offset_min - 123, /* Offset too small */
        loc_offset_min + 123, /* Offset within the allowed range */
        loc_offset_max + 123, /* Offset too large */
    };
    int rejects[] = {1, 0, 1};
    int rejected;
    int should_reject;
    uint8_t i;

    for (i = 0; i < ARRAY_SIZE(offsets); ++i) {
        memset(&loc_ps, 0, sizeof(loc_ps));
        loc_ps.max_procedure_count = 1;
        loc_ps.anchor_conn_event_cntr = 1010;
        loc_ps.max_procedure_len = 30;
        loc_ps.subevent_len = BLE_LL_CS_SUBEVENT_LEN_MIN;
        loc_ps.event_interval = 1;
        loc_ps.subevent_interval = 0;
        loc_ps.subevents_per_event = 1;
        loc_ps.preferred_peer_antenna = 1;
        loc_ps.phy = 1;
        loc_ps.offset_min = loc_offset_min;
        loc_ps.offset_max = loc_offset_max;

        memcpy(&rem_ps, &loc_ps, sizeof(rem_ps));
        rem_ps.event_offset = offsets[i];
        rem_ps.offset_min = rem_ps.event_offset;
        rem_ps.offset_max = rem_ps.event_offset;
        should_reject = rejects[i];

        rejected = ble_ll_cs_start_ind_parameters_apply(&loc_ps, &rem_ps, conn_itvl_us,
                                                        ce_duration_us, 1, event_cntr);
        assert(should_reject == rejected);
    }
}

static void
ble_ll_cs_config_subevent_len_test(void)
{
    int rejected;
    struct ble_ll_cs_proc_params org_ps;
    struct ble_ll_cs_proc_params loc_ps;
    struct ble_ll_cs_proc_params rem_ps;
    uint32_t conn_itvl_us = 20000;
    uint32_t ce_duration_us = 1250;
    uint16_t event_cntr = 1000;
    uint32_t loc_offset_min = MAX(BLE_LL_CS_EVENT_OFFSET_MIN_US, ce_duration_us +
                                  BLE_LL_CS_SUBEVENT_SAFE_SPACE_FROM_CE);
    uint32_t loc_offset_max = MIN(BLE_LL_CS_EVENT_OFFSET_MAX_US, conn_itvl_us -
                                  BLE_LL_CS_SUBEVENT_LEN_MIN - BLE_LL_CS_SUBEVENT_T_MES_US);
    uint32_t subevent_lens[] = {
        /* subvent_len larger that the connection interval */
        conn_itvl_us + 123,
        /* subvent_len smaller that the connection interval, but does not fit */
        conn_itvl_us - 10,
        /* subvent_len fits only if min offset selected */
        conn_itvl_us - loc_offset_min - BLE_LL_CS_SUBEVENT_T_MES_US,
        /* subvent_len fits even if max offset selected */
        conn_itvl_us - loc_offset_max - BLE_LL_CS_SUBEVENT_T_MES_US,
        /* valid subvent_len other than edge value */
        conn_itvl_us - loc_offset_max - BLE_LL_CS_SUBEVENT_T_MES_US - 123,
    };
    uint8_t i;

    for (i = 0; i < ARRAY_SIZE(subevent_lens); ++i) {
        memset(&org_ps, 0, sizeof(org_ps));
        org_ps.max_procedure_count = 1;
        org_ps.anchor_conn_event_cntr = 1000;
        org_ps.max_procedure_len = 30;
        org_ps.offset_min = loc_offset_min;
        org_ps.offset_max = loc_offset_max;
        org_ps.event_interval = 1;
        org_ps.subevent_interval = 0;
        org_ps.subevents_per_event = 1;
        org_ps.preferred_peer_antenna = 1;
        org_ps.phy = 1;
        org_ps.subevent_len = subevent_lens[i];

        memcpy(&loc_ps, &org_ps, sizeof(loc_ps));
        rejected = ble_ll_cs_start_req_parameters_apply(&loc_ps, conn_itvl_us, ce_duration_us,
                                                        event_cntr, 1, event_cntr);
        assert(rejected == 0);
        assert(loc_ps.subevent_len <= (conn_itvl_us - loc_offset_min - BLE_LL_CS_SUBEVENT_T_MES_US));

        memcpy(&rem_ps, &org_ps, sizeof(rem_ps));
        rejected = ble_ll_cs_start_rsp_parameters_apply(&loc_ps, &rem_ps, conn_itvl_us, ce_duration_us,
                                                        1, event_cntr);
        assert(rejected == 0);
        assert(loc_ps.subevent_len <= (conn_itvl_us - loc_offset_min - BLE_LL_CS_SUBEVENT_T_MES_US));
    }
}

static void
ble_ll_cs_config_subevent_interval_test(void)
{
    int rejected;
    struct ble_ll_cs_proc_params org_ps;
    struct ble_ll_cs_proc_params loc_ps;
    struct ble_ll_cs_proc_params rem_ps;
    uint32_t conn_itvl_us = 20000;
    uint32_t ce_duration_us = 1250;
    uint32_t subevent_interval_us;
    uint16_t event_cntr = 1000;
    uint32_t loc_offset_min = MAX(BLE_LL_CS_EVENT_OFFSET_MIN_US, ce_duration_us +
                                  BLE_LL_CS_SUBEVENT_SAFE_SPACE_FROM_CE);
    uint32_t loc_offset_max = MIN(BLE_LL_CS_EVENT_OFFSET_MAX_US, conn_itvl_us -
                                  BLE_LL_CS_SUBEVENT_LEN_MIN - BLE_LL_CS_SUBEVENT_T_MES_US);
    uint32_t subevent_intervals[] = {
        7, /* subevent_interval smaller than subvent_len */
        8, /* subevent_interval equal to subvent_len */
        10, /* subevent_interval within allowed max range */
        40, /* subevent_interval larger than allowed max range */
    };
    uint8_t i;

    memset(&org_ps, 0, sizeof(org_ps));
    org_ps.max_procedure_count = 1;
    org_ps.anchor_conn_event_cntr = 1000;
    org_ps.offset_min = loc_offset_min;
    org_ps.offset_max = loc_offset_max;
    org_ps.preferred_peer_antenna = 1;
    org_ps.phy = 1;
    org_ps.event_interval = 1;
    org_ps.max_procedure_len = 30; /* 30 * 625us = 18750us */
    org_ps.subevent_len = 5000; /* usecs */
    org_ps.subevents_per_event = 1;
    org_ps.subevent_interval = 0;

    /* subevent_interval corrected to 0 */
    memcpy(&loc_ps, &org_ps, sizeof(loc_ps));
    loc_ps.subevents_per_event = 1;
    loc_ps.subevent_interval = 10; /* 10 * 625us = 6250us */
    rejected = ble_ll_cs_start_req_parameters_apply(&loc_ps, conn_itvl_us, ce_duration_us,
                                                    event_cntr, 1, event_cntr);
    assert(loc_ps.subevent_interval == 0);

    for (i = 0; i < ARRAY_SIZE(subevent_intervals); ++i) {
        memcpy(&loc_ps, &org_ps, sizeof(loc_ps));
        loc_ps.subevents_per_event = 3;
        loc_ps.subevent_interval = subevent_intervals[i];
        rejected = ble_ll_cs_start_req_parameters_apply(&loc_ps, conn_itvl_us, ce_duration_us,
                                                        event_cntr, 1, event_cntr);
        assert(rejected == 0);
        subevent_interval_us = loc_ps.subevent_interval * BLE_LL_CS_SUBEVENTS_INTERVAL_UNIT_US;
        assert(subevent_interval_us > loc_ps.subevent_len || loc_ps.subevent_interval == 0);
        assert(loc_ps.subevent_interval < loc_ps.max_procedure_len);

        memcpy(&rem_ps, &org_ps, sizeof(rem_ps));
        rem_ps.subevents_per_event = 3;
        rem_ps.subevent_interval = subevent_intervals[i];
        rejected = ble_ll_cs_start_rsp_parameters_apply(&loc_ps, &rem_ps, conn_itvl_us, ce_duration_us,
                                                        1, event_cntr);
        assert(rejected == 0);
        subevent_interval_us = loc_ps.subevent_interval * BLE_LL_CS_SUBEVENTS_INTERVAL_UNIT_US;
        assert(subevent_interval_us > loc_ps.subevent_len || loc_ps.subevent_interval == 0);
        assert(loc_ps.subevent_interval < loc_ps.max_procedure_len);
    }
}

TEST_SUITE(ble_ll_cs_config_test_suite)
{
    ble_ll_cs_config_offset_range_test();
    ble_ll_cs_config_offset_rejection_test();
    ble_ll_cs_config_subevent_len_test();
    ble_ll_cs_config_subevent_interval_test();
}
