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

#ifndef H_CMD_LEAUDIO_
#define H_CMD_LEAUDIO_

#include "cmd.h"

#define CMD_ADV_DATA_CODEC_SPEC_CFG_MAX_SZ                                  (9)
/**
 * Maximum Metadata size is maximum adv size minus minimum size of other
 * fields in BASE advertising
 */
#define CMD_ADV_DATA_METADATA_MAX_SZ    (MYNEWT_VAL(BLE_EXT_ADV_MAX_SIZE) - 27)
/**
 * Maximum size of extra data included in BASE advertising. Assumes minimum
 * size of other fields.
 */
#define CMD_ADV_DATA_EXTRA_MAX_SZ       (MYNEWT_VAL(BLE_EXT_ADV_MAX_SIZE) - 27)

int cmd_leaudio_base_add(int argc, char **argv);
int cmd_leaudio_big_sub_add(int argc, char **argv);
int cmd_leaudio_bis_add(int argc, char **argv);
int cmd_leaudio_broadcast_create(int argc, char **argv);
int cmd_leaudio_broadcast_destroy(int argc, char **argv);
int cmd_leaudio_broadcast_update(int argc, char **argv);
int cmd_leaudio_broadcast_start(int argc, char **argv);
int cmd_leaudio_broadcast_stop(int argc, char **argv);

#endif /* H_CMD_LEAUDIO_ */
