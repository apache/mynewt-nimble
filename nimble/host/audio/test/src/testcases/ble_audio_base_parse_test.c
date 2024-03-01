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

#include "testutil/testutil.h"

#include "host/ble_hs.h"
#include "audio/ble_audio.h"

/**
 * BAP_v1.0.1 Table 3.16
 * BASE structure for the logical BASE structure example
 */
static const uint8_t example_base[] = {
    0x1e, 0x00, 0x00, /* Presentation_Delay: 40 ms */
    0x02, /* Num_Subgroups: 2 Subgroups */
    0x02, /* Num_BIS[0]: 2 BIS in Subgroup[0] */
    0x06, 0x00, 0x00, 0x00, 0x00, /* Codec_ID[0]: LC3 */
    0x0a, /* Codec_Specific_Configuration_Length[0] */
    0x02, 0x01, 0x08, /* LTV 1: Sampling_Frequency: 48000 Hz */
    0x02, 0x02, 0x02, /* LTV 2: Frame_Duration: 10 ms */
    0x03, 0x04, 0x64, 0x00, /* LTV 3: Octets_Per_Codec_Frame: 100 octets */
    0x09, /* Metadata_Length[0] */
    0x03, 0x02, 0x04, 0x00, /* LTV 1: Streaming_Audio_Contexts: Media */
    0x04, 0x04, 0x73, 0x70, 0x61, /* LTV 2: Language: Spanish */
    0x01, /* BIS_index[0[0]] */
    0x06, /* Codec_Specific_Configuration_Length[0[0]] */
    0x05, 0x03, 0x01, 0x00, 0x00, 0x00, /* LTV 1 = Audio_Channel_Allocation: FL */
    0x02, /* BIS_index[0[1]] */
    0x06, /* Codec_Specific_Configuration_Length[0[1]] */
    0x05, 0x03, 0x02, 0x00, 0x00, 0x00, /* LTV 1 = Audio_Channel_Allocation: FR */
    0x02, /* Num_BIS[1]: 2 BIS in Subgroup[0] */
    0x06, 0x00, 0x00, 0x00, 0x00, /* Codec_ID[1]: LC3 */
    0x0a, /* Codec_Specific_Configuration_Length[1] */
    0x02, 0x01, 0x08, /* LTV 1: Sampling_Frequency: 48000 Hz */
    0x02, 0x02, 0x02, /* LTV 2: Frame_Duration: 10 ms */
    0x03, 0x04, 0x64, 0x00, /* LTV 3: Octets_Per_Codec_Frame: 100 octets */
    0x09, /* Metadata_Length[1] */
    0x03, 0x02, 0x04, 0x00, /* LTV 1: Streaming_Audio_Contexts: Media */
    0x04, 0x04, 0x65, 0x6e, 0x67, /* LTV 2: Language: English */
    0x03, /* BIS_index[1[0]] */
    0x06, /* Codec_Specific_Configuration_Length[1[0]] */
    0x05, 0x03, 0x01, 0x00, 0x00, 0x00, /* LTV 1 = Audio_Channel_Allocation: FL */
    0x04, /* BIS_index[1[1]] */
    0x06, /* Codec_Specific_Configuration_Length[1[1]] */
    0x05, 0x03, 0x02, 0x00, 0x00, 0x00, /* LTV 1 = Audio_Channel_Allocation: FR */
};

TEST_CASE_SELF(ble_audio_base_parse_test)
{
    struct ble_audio_base_subgroup subgroup;
    struct ble_audio_base_group group;
    struct ble_audio_base_bis bis;
    struct ble_audio_base_iter subgroup_iter;
    struct ble_audio_base_iter bis_iter;
    int rc;

    rc = ble_audio_base_parse(example_base, (uint8_t)sizeof(example_base),
                              &group, &subgroup_iter);
    TEST_ASSERT(rc == 0);

    TEST_ASSERT(group.presentation_delay == 30);
    TEST_ASSERT(group.num_subgroups == 2);

    rc = ble_audio_base_subgroup_iter(&subgroup_iter, &subgroup, &bis_iter);
    TEST_ASSERT(rc == 0);

    TEST_ASSERT(subgroup.codec_id.format == 0x06);
    TEST_ASSERT(subgroup.codec_id.company_id == 0x0000);
    TEST_ASSERT(subgroup.codec_id.vendor_specific == 0x0000);
    TEST_ASSERT(subgroup.codec_spec_config_len == 10);
    TEST_ASSERT(subgroup.codec_spec_config != NULL);
    TEST_ASSERT(subgroup.metadata_len == 9);
    TEST_ASSERT(subgroup.num_bis == 2);

    rc = ble_audio_base_bis_iter(&bis_iter, &bis);
    TEST_ASSERT(rc == 0);

    TEST_ASSERT(bis.index == 0x01);
    TEST_ASSERT(bis.codec_spec_config_len == 6);
    TEST_ASSERT(bis.codec_spec_config != NULL);

    rc = ble_audio_base_bis_iter(&bis_iter, &bis);
    TEST_ASSERT(rc == 0);

    TEST_ASSERT(bis.index == 0x02);
    TEST_ASSERT(bis.codec_spec_config_len == 6);
    TEST_ASSERT(bis.codec_spec_config != NULL);

    rc = ble_audio_base_bis_iter(&bis_iter, &bis);
    TEST_ASSERT(rc == BLE_HS_ENOENT);

    rc = ble_audio_base_subgroup_iter(&subgroup_iter, &subgroup, &bis_iter);
    TEST_ASSERT(rc == 0);

    TEST_ASSERT(subgroup.codec_id.format == 0x06);
    TEST_ASSERT(subgroup.codec_id.company_id == 0x0000);
    TEST_ASSERT(subgroup.codec_id.vendor_specific == 0x0000);
    TEST_ASSERT(subgroup.codec_spec_config_len == 10);
    TEST_ASSERT(subgroup.codec_spec_config != NULL);
    TEST_ASSERT(subgroup.metadata_len == 9);
    TEST_ASSERT(subgroup.num_bis == 2);

    rc = ble_audio_base_bis_iter(&bis_iter, &bis);
    TEST_ASSERT(rc == 0);

    TEST_ASSERT(bis.index == 0x03);
    TEST_ASSERT(bis.codec_spec_config_len == 6);
    TEST_ASSERT(bis.codec_spec_config != NULL);

    rc = ble_audio_base_bis_iter(&bis_iter, &bis);
    TEST_ASSERT(rc == 0);

    TEST_ASSERT(bis.index == 0x04);
    TEST_ASSERT(bis.codec_spec_config_len == 6);
    TEST_ASSERT(bis.codec_spec_config != NULL);

    rc = ble_audio_base_bis_iter(&bis_iter, &bis);
    TEST_ASSERT(rc == BLE_HS_ENOENT);

    rc = ble_audio_base_subgroup_iter(&subgroup_iter, &subgroup, &bis_iter);
    TEST_ASSERT(rc == BLE_HS_ENOENT);
}

TEST_CASE_SELF(ble_audio_base_parse_test_params)
{
    struct ble_audio_base_subgroup subgroup;
    struct ble_audio_base_group group;
    struct ble_audio_base_bis bis;
    struct ble_audio_base_iter subgroup_iter;
    struct ble_audio_base_iter bis_iter;
    int rc;

    rc = ble_audio_base_parse(NULL, (uint8_t)sizeof(example_base), &group, &subgroup_iter);
    TEST_ASSERT(rc == BLE_HS_EINVAL);

    rc = ble_audio_base_parse(NULL, (uint8_t)sizeof(example_base), NULL, &subgroup_iter);
    TEST_ASSERT(rc == BLE_HS_EINVAL);

    rc = ble_audio_base_parse(example_base, (uint8_t)sizeof(example_base), &group, NULL);
    TEST_ASSERT(rc == 0);

    rc = ble_audio_base_parse(example_base, (uint8_t)sizeof(example_base), &group, &subgroup_iter);
    TEST_ASSERT(rc == 0);

    rc = ble_audio_base_subgroup_iter(NULL, &subgroup, &bis_iter);
    TEST_ASSERT(rc == BLE_HS_EINVAL);

    rc = ble_audio_base_subgroup_iter(&subgroup_iter, NULL, &bis_iter);
    TEST_ASSERT(rc == BLE_HS_EINVAL);

    rc = ble_audio_base_subgroup_iter(&subgroup_iter, &subgroup, NULL);
    TEST_ASSERT(rc == 0);

    rc = ble_audio_base_bis_iter(NULL, &bis);
    TEST_ASSERT(rc == BLE_HS_EINVAL);

    rc = ble_audio_base_bis_iter(&bis_iter, NULL);
    TEST_ASSERT(rc == BLE_HS_EINVAL);
}

TEST_CASE_SELF(ble_audio_base_parse_test_data_length)
{
    struct ble_audio_base_subgroup subgroup;
    struct ble_audio_base_group group;
    struct ble_audio_base_bis bis;
    struct ble_audio_base_iter subgroup_iter;
    struct ble_audio_base_iter bis_iter;
    int rc;

    /* Incomplete: empty */
    rc = ble_audio_base_parse(example_base, 0, &group, &subgroup_iter);
    TEST_ASSERT(rc == BLE_HS_EMSGSIZE);

    /* Truncated: Presentation_Delay Parameter */
    rc = ble_audio_base_parse(example_base, 2, &group, &subgroup_iter);
    TEST_ASSERT(rc == BLE_HS_EMSGSIZE);

    /* Incomplete: no Num_Subgroups[0] Parameter */
    rc = ble_audio_base_parse(example_base, 3, &group, &subgroup_iter);
    TEST_ASSERT(rc == BLE_HS_EMSGSIZE);

    /* Incomplete: no Num_BIS[0] Parameter */
    rc = ble_audio_base_parse(example_base, 4, &group, &subgroup_iter);
    TEST_ASSERT(rc == 0);
    rc = ble_audio_base_subgroup_iter(&subgroup_iter, &subgroup, &bis_iter);
    TEST_ASSERT(rc == BLE_HS_EMSGSIZE);

    /* Truncated: Codec_ID[0] Parameter */
    rc = ble_audio_base_parse(example_base, 9, &group, &subgroup_iter);
    rc = ble_audio_base_subgroup_iter(&subgroup_iter, &subgroup, &bis_iter);
    TEST_ASSERT(rc == BLE_HS_EMSGSIZE);

    /* Incomplete: no Codec_Specific_Configuration_Length[0] Parameter */
    rc = ble_audio_base_parse(example_base, 13, &group, &subgroup_iter);
    rc = ble_audio_base_subgroup_iter(&subgroup_iter, &subgroup, &bis_iter);
    TEST_ASSERT(rc == BLE_HS_EMSGSIZE);

    /* Truncated: Codec_Specific_Configuration[0] Parameter */
    rc = ble_audio_base_parse(example_base, 14, &group, &subgroup_iter);
    rc = ble_audio_base_subgroup_iter(&subgroup_iter, &subgroup, &bis_iter);
    TEST_ASSERT(rc == BLE_HS_EMSGSIZE);

    /* Incomplete: no Metadata_Length[0] Parameter */
    rc = ble_audio_base_parse(example_base, 21, &group, &subgroup_iter);
    rc = ble_audio_base_subgroup_iter(&subgroup_iter, &subgroup, &bis_iter);
    TEST_ASSERT(rc == BLE_HS_EMSGSIZE);

    /* Truncated: Metadata[0] Parameter */
    rc = ble_audio_base_parse(example_base, 30, &group, &subgroup_iter);
    rc = ble_audio_base_subgroup_iter(&subgroup_iter, &subgroup, &bis_iter);
    TEST_ASSERT(rc == BLE_HS_EMSGSIZE);

    /* Incomplete: no BIS_index[0[0]] Parameter */
    rc = ble_audio_base_parse(example_base, 31, &group, &subgroup_iter);
    rc = ble_audio_base_subgroup_iter(&subgroup_iter, &subgroup, &bis_iter);
    TEST_ASSERT(rc == 0);
    rc = ble_audio_base_bis_iter(&bis_iter, &bis);
    TEST_ASSERT(rc == BLE_HS_EMSGSIZE);

    /* Incomplete: no Codec_Specific_Configuration_Length[0[0]] Parameter */
    rc = ble_audio_base_parse(example_base, 32, &group, &subgroup_iter);
    rc = ble_audio_base_subgroup_iter(&subgroup_iter, &subgroup, &bis_iter);
    rc = ble_audio_base_bis_iter(&bis_iter, &bis);
    TEST_ASSERT(rc == BLE_HS_EMSGSIZE);

    /* Truncated: Codec_Specific_Configuration_Length[0[0]] Parameter */
    rc = ble_audio_base_parse(example_base, 38, &group, &subgroup_iter);
    rc = ble_audio_base_subgroup_iter(&subgroup_iter, &subgroup, &bis_iter);
    rc = ble_audio_base_bis_iter(&bis_iter, &bis);
    TEST_ASSERT(rc == BLE_HS_EMSGSIZE);

    /* Incomplete: no BIS_index[0[1]] Parameter */
    rc = ble_audio_base_parse(example_base, 39, &group, &subgroup_iter);
    rc = ble_audio_base_subgroup_iter(&subgroup_iter, &subgroup, &bis_iter);
    rc = ble_audio_base_bis_iter(&bis_iter, &bis);
    TEST_ASSERT(rc == 0);
    rc = ble_audio_base_bis_iter(&bis_iter, &bis);
    TEST_ASSERT(rc == BLE_HS_EMSGSIZE);

    /* Incomplete: no Codec_Specific_Configuration_Length[0[1]] Parameter */
    rc = ble_audio_base_parse(example_base, 40, &group, &subgroup_iter);
    rc = ble_audio_base_subgroup_iter(&subgroup_iter, &subgroup, &bis_iter);
    rc = ble_audio_base_bis_iter(&bis_iter, &bis);
    rc = ble_audio_base_bis_iter(&bis_iter, &bis);
    TEST_ASSERT(rc == BLE_HS_EMSGSIZE);

    /* Truncated: Codec_Specific_Configuration_Length[0[1]] Parameter */
    rc = ble_audio_base_parse(example_base, 46, &group, &subgroup_iter);
    rc = ble_audio_base_subgroup_iter(&subgroup_iter, &subgroup, &bis_iter);
    rc = ble_audio_base_bis_iter(&bis_iter, &bis);
    TEST_ASSERT(rc == 0);
    rc = ble_audio_base_bis_iter(&bis_iter, &bis);
    TEST_ASSERT(rc == BLE_HS_EMSGSIZE);

    /* Incomplete: no Num_BIS[1] Parameter */
    rc = ble_audio_base_parse(example_base, 47, &group, &subgroup_iter);
    rc = ble_audio_base_subgroup_iter(&subgroup_iter, &subgroup, &bis_iter);
    TEST_ASSERT(rc == 0);
    rc = ble_audio_base_subgroup_iter(&subgroup_iter, &subgroup, &bis_iter);
    TEST_ASSERT(rc == BLE_HS_EMSGSIZE);

    /* Truncated: Codec_ID[1] Parameter */
    rc = ble_audio_base_parse(example_base, 52, &group, &subgroup_iter);
    rc = ble_audio_base_subgroup_iter(&subgroup_iter, &subgroup, &bis_iter);
    rc = ble_audio_base_subgroup_iter(&subgroup_iter, &subgroup, &bis_iter);
    TEST_ASSERT(rc == BLE_HS_EMSGSIZE);

    /* Incomplete: no Codec_Specific_Configuration_Length[1] Parameter */
    rc = ble_audio_base_parse(example_base, 53, &group, &subgroup_iter);
    rc = ble_audio_base_subgroup_iter(&subgroup_iter, &subgroup, &bis_iter);
    rc = ble_audio_base_subgroup_iter(&subgroup_iter, &subgroup, &bis_iter);
    TEST_ASSERT(rc == BLE_HS_EMSGSIZE);

    /* Truncated: Codec_Specific_Configuration[1] Parameter */
    rc = ble_audio_base_parse(example_base, 63, &group, &subgroup_iter);
    rc = ble_audio_base_subgroup_iter(&subgroup_iter, &subgroup, &bis_iter);
    rc = ble_audio_base_subgroup_iter(&subgroup_iter, &subgroup, &bis_iter);
    TEST_ASSERT(rc == BLE_HS_EMSGSIZE);

    /* Incomplete: no Metadata_Length[1] Parameter */
    rc = ble_audio_base_parse(example_base, 64, &group, &subgroup_iter);
    rc = ble_audio_base_subgroup_iter(&subgroup_iter, &subgroup, &bis_iter);
    rc = ble_audio_base_subgroup_iter(&subgroup_iter, &subgroup, &bis_iter);
    TEST_ASSERT(rc == BLE_HS_EMSGSIZE);

    /* Truncated: Metadata[1] Parameter */
    rc = ble_audio_base_parse(example_base, 73, &group, &subgroup_iter);
    rc = ble_audio_base_subgroup_iter(&subgroup_iter, &subgroup, &bis_iter);
    rc = ble_audio_base_subgroup_iter(&subgroup_iter, &subgroup, &bis_iter);
    TEST_ASSERT(rc == BLE_HS_EMSGSIZE);

    /* Incomplete: no BIS_index[1[0]] Parameter */
    rc = ble_audio_base_parse(example_base, 74, &group, &subgroup_iter);
    rc = ble_audio_base_subgroup_iter(&subgroup_iter, &subgroup, &bis_iter);
    rc = ble_audio_base_subgroup_iter(&subgroup_iter, &subgroup, &bis_iter);
    TEST_ASSERT(rc == 0);
    rc = ble_audio_base_bis_iter(&bis_iter, &bis);
    TEST_ASSERT(rc == BLE_HS_EMSGSIZE);

    /* Incomplete: no Codec_Specific_Configuration_Length[1[0]] Parameter */
    rc = ble_audio_base_parse(example_base, 75, &group, &subgroup_iter);
    rc = ble_audio_base_subgroup_iter(&subgroup_iter, &subgroup, &bis_iter);
    rc = ble_audio_base_subgroup_iter(&subgroup_iter, &subgroup, &bis_iter);
    rc = ble_audio_base_bis_iter(&bis_iter, &bis);
    TEST_ASSERT(rc == BLE_HS_EMSGSIZE);

    /* Truncated: Codec_Specific_Configuration_Length[1[0]] Parameter */
    rc = ble_audio_base_parse(example_base, 81, &group, &subgroup_iter);
    rc = ble_audio_base_subgroup_iter(&subgroup_iter, &subgroup, &bis_iter);
    rc = ble_audio_base_subgroup_iter(&subgroup_iter, &subgroup, &bis_iter);
    rc = ble_audio_base_bis_iter(&bis_iter, &bis);
    TEST_ASSERT(rc == BLE_HS_EMSGSIZE);

    /* Incomplete: no BIS_index[1[1]] Parameter */
    rc = ble_audio_base_parse(example_base, 82, &group, &subgroup_iter);
    rc = ble_audio_base_subgroup_iter(&subgroup_iter, &subgroup, &bis_iter);
    rc = ble_audio_base_subgroup_iter(&subgroup_iter, &subgroup, &bis_iter);
    rc = ble_audio_base_bis_iter(&bis_iter, &bis);
    TEST_ASSERT(rc == 0);
    rc = ble_audio_base_bis_iter(&bis_iter, &bis);
    TEST_ASSERT(rc == BLE_HS_EMSGSIZE);

    /* Incomplete: no Codec_Specific_Configuration_Length[1[1]] Parameter */
    rc = ble_audio_base_parse(example_base, 83, &group, &subgroup_iter);
    rc = ble_audio_base_subgroup_iter(&subgroup_iter, &subgroup, &bis_iter);
    rc = ble_audio_base_subgroup_iter(&subgroup_iter, &subgroup, &bis_iter);
    rc = ble_audio_base_bis_iter(&bis_iter, &bis);
    rc = ble_audio_base_bis_iter(&bis_iter, &bis);
    TEST_ASSERT(rc == BLE_HS_EMSGSIZE);

    /* Truncated: Codec_Specific_Configuration_Length[0[1]] Parameter */
    rc = ble_audio_base_parse(example_base, 89, &group, &subgroup_iter);
    rc = ble_audio_base_subgroup_iter(&subgroup_iter, &subgroup, &bis_iter);
    rc = ble_audio_base_subgroup_iter(&subgroup_iter, &subgroup, &bis_iter);
    rc = ble_audio_base_bis_iter(&bis_iter, &bis);
    rc = ble_audio_base_bis_iter(&bis_iter, &bis);
    TEST_ASSERT(rc == BLE_HS_EMSGSIZE);
}

TEST_SUITE(ble_audio_base_parse_test_suite)
{
    ble_audio_base_parse_test();
    ble_audio_base_parse_test_params();
    ble_audio_base_parse_test_data_length();
}
