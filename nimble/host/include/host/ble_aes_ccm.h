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

#ifndef _BLE_AES_CCM_
#define _BLE_AES_CCM_

#include "syscfg/syscfg.h"
#include "os/queue.h"
#include "host/ble_hs.h"

#if MYNEWT_VAL(BLE_CRYPTO_STACK_MBEDTLS)
#include "mbedtls/aes.h"
#else
#include "tinycrypt/aes.h"
#endif

#ifdef __cplusplus
extern "C" {
#endif

#if MYNEWT_VAL(BLE_ENC_ADV_DATA)

const char *ble_aes_ccm_hex(const void *buf, size_t len);
int ble_aes_ccm_encrypt_be(const uint8_t *key, const uint8_t *plaintext, uint8_t *enc_data);
int ble_aes_ccm_decrypt(const uint8_t key[16], uint8_t nonce[13], const uint8_t *enc_data,
                        size_t len, const uint8_t *aad, size_t aad_len,
                        uint8_t *plaintext, size_t mic_size);
int ble_aes_ccm_encrypt(const uint8_t key[16], uint8_t nonce[13], const uint8_t *enc_data,
                        size_t len, const uint8_t *aad, size_t aad_len,
                        uint8_t *plaintext, size_t mic_size);

#endif /* ENC_ADV_DATA */

#ifdef __cplusplus
}
#endif

#endif /* _BLE_AES_CCM_ */
