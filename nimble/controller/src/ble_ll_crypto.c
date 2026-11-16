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
#include <string.h>
#include <nimble/ble.h>
#include <controller/ble_ll_crypto.h>
#include <controller/ble_hw.h>

static void
ble_ll_crypto_encrypt_block(struct ble_encryption_block *ecb)
{
    /* Hardware AES may be preempted by radio (e.g. on-the-fly CCM or AAR),
     * retry until it succeeds.
     */
    while (ble_hw_encrypt_block(ecb)) {
    }
}

static void
cmac_subkey(const uint8_t *k_in, uint8_t *k_out)
{
    uint8_t msb;
    int i;

    msb = k_in[0] & 0x80;

    for (i = 0; i < BLE_ENC_BLOCK_SIZE - 1; i++) {
        k_out[i] = (k_in[i] << 1) | (k_in[i + 1] >> 7);
    }
    k_out[BLE_ENC_BLOCK_SIZE - 1] = k_in[BLE_ENC_BLOCK_SIZE - 1] << 1;

    if (msb) {
        k_out[BLE_ENC_BLOCK_SIZE - 1] ^= 0x87;
    }
}

int
ble_ll_crypto_cmac(const uint8_t *key, const uint8_t *in, int len,
                   uint8_t *out)
{
    struct ble_encryption_block ecb;
    uint8_t subkey[BLE_ENC_BLOCK_SIZE];
    uint8_t m;
    int i;

    memcpy(ecb.key, key, BLE_ENC_BLOCK_SIZE);

    /* Generate_Subkey */
    memset(ecb.plain_text, 0, BLE_ENC_BLOCK_SIZE);
    ble_ll_crypto_encrypt_block(&ecb);

    /* Generate K1 */
    cmac_subkey(ecb.cipher_text, subkey);

    /* Generate K2 or skip if last block should be xor-ed with K1 */
    if (len == 0 || len % BLE_ENC_BLOCK_SIZE) {
        cmac_subkey(subkey, subkey);
    }

    /* Blocks 1 .. n-1 */
    memset(ecb.cipher_text, 0, BLE_ENC_BLOCK_SIZE);
    while (len > BLE_ENC_BLOCK_SIZE) {
        for (i = 0; i < BLE_ENC_BLOCK_SIZE; i++) {
            ecb.plain_text[i] = ecb.cipher_text[i] ^ in[i];
        }
        ble_ll_crypto_encrypt_block(&ecb);

        in += BLE_ENC_BLOCK_SIZE;
        len -= BLE_ENC_BLOCK_SIZE;
    }

    /* Last block */
    for (i = 0; i < BLE_ENC_BLOCK_SIZE; i++) {
        if (i < len) {
            m = in[i];
        } else if (i == len) {
            m = 0x80;
        } else {
            m = 0;
        }
        ecb.plain_text[i] = ecb.cipher_text[i] ^ m ^ subkey[i];
    }
    ble_ll_crypto_encrypt_block(&ecb);

    memcpy(out, ecb.cipher_text, BLE_ENC_BLOCK_SIZE);

    return 0;
}

int
ble_ll_crypto_h8(const uint8_t *k, const uint8_t *s, const uint8_t *key_id,
                 uint8_t *out)
{
    uint8_t ik[16];
    int rc;

    rc = ble_ll_crypto_cmac(s, k, 16, ik);
    if (rc) {
        return rc;
    }

    return ble_ll_crypto_cmac(ik, key_id, 4, out);
}
