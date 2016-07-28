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

#ifndef H_BLE_SVC_ANS_
#define H_BLE_SVC_ANS_

struct ble_hs_cfg;

/* 16 Bit Alert Notification Service UUID */
#define BLE_SVC_ANS_UUID16                                  0x1811

/* 16 Bit Alert Notification Servivce Characteristic UUIDs */
#define BLE_SVC_ANS_CHR_UUID16_SUP_NEW_ALERT_CAT            0x2a47
#define BLE_SVC_ANS_CHR_UUID16_NEW_ALERT                    0x2a46
#define BLE_SVC_ANS_CHR_UUID16_SUP_UNR_ALERT_CAT            0x2a48
#define BLE_SVC_ANS_CHR_UUID16_UNR_ALERT_STAT               0x2a45
#define BLE_SVC_ANS_CHR_UUID16_ALERT_NOT_CTRL_PT            0x2a44

/* Alert Notification Service Category ID Bit Masks 
 *
 * TODO: Need to add remaining 2 categories */
#define BLE_SVC_ANS_CAT_F_SIMPLE_ALERT                      0x01
#define BLE_SVC_ANS_CAT_F_EMAIL                             0x02
#define BLE_SVC_ANS_CAT_F_NEWS                              0x04
#define BLE_SVC_ANS_CAT_F_CALL                              0x08
#define BLE_SVC_ANS_CAT_F_MISSED_CALL                       0x10
#define BLE_SVC_ANS_CAT_F_SMS                               0x20
#define BLE_SVC_ANS_CAT_F_VOICE_MAIL                        0x40
#define BLE_SVC_ANS_CAT_F_SCHEDULE                          0x80    

/* Alert Notification Service Category IDs
 *
 * TODO: Need to add remaining 2 categories */
#define BLE_SVC_ANS_CAT_SIMPLE_ALERT                        0x1
#define BLE_SVC_ANS_CAT_EMAIL                               0x2
#define BLE_SVC_ANS_CAT_NEWS                                0x3
#define BLE_SVC_ANS_CAT_CALL                                0x4
#define BLE_SVC_ANS_CAT_MISSED_CALL                         0x5
#define BLE_SVC_ANS_CAT_SMS                                 0x6
#define BLE_SVC_ANS_CAT_VOICE_MAIL                          0x7
#define BLE_SVC_ANS_CAT_SCHEDULE                            0x8

/* Number of valid ANS categories 
 *
 * TODO: Need to add remaining 2 categories */
#define BLE_SVC_ANS_CAT_NUM                                 8

/* Alert Notification Control Point Command IDs */
#define BLE_SVC_ANS_CMD_EN_NEW_ALERT_CAT                    0
#define BLE_SVC_ANS_CMD_EN_UNR_ALERT_CAT                    1
#define BLE_SVC_ANS_CMD_DIS_NEW_ALERT_CAT                   2
#define BLE_SVC_ANS_CMD_DIS_UNR_ALERT_CAT                   3
#define BLE_SVC_ANS_CMD_NOT_NEW_ALERT_IMMEADIATE            4
#define BLE_SVC_ANS_CMD_NOT_UNR_ALERT_IMMEADIATE            5

int ble_svc_ans_new_alert_add(uint8_t category_flag, 
                              char * info_str);
int ble_svc_ans_unr_alert_add(uint8_t category_flag);

int ble_svc_ans_init(struct ble_hs_cfg *cfg, 
                     uint8_t new_alert_cat,
                     uint8_t urn_alert_cat);

#endif

