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

#if NIMBLE_OPT_SM

#include <inttypes.h>
#include <string.h>
#include "mbedtls/aes.h"
#include "nimble/ble.h"
#include "ble_hs_priv.h"

static mbedtls_aes_context ble_l2cap_sm_alg_ctxt;

static void
ble_l2cap_sm_alg_xor_128(uint8_t *p, uint8_t *q, uint8_t *r)
{
    int i;

    for (i = 0; i < 16; i++) {
        r[i] = p[i] ^ q[i];
    }
}

static int
ble_l2cap_sm_alg_encrypt(uint8_t *key, uint8_t *plaintext, uint8_t *enc_data)
{
    mbedtls_aes_init(&ble_l2cap_sm_alg_ctxt);
    uint8_t tmp[16];
    int rc;

    BLE_HS_LOG(DEBUG, "ble_l2cap_sm_alg_encrypt; key=");
    ble_hs_misc_log_flat_buf(key, 16);
    BLE_HS_LOG(DEBUG, " plaintext=");
    ble_hs_misc_log_flat_buf(plaintext, 16);
    BLE_HS_LOG(DEBUG, "\n");

    swap_buf(tmp, key, 16);

    rc = mbedtls_aes_setkey_enc(&ble_l2cap_sm_alg_ctxt, tmp, 128);
    if (rc != 0) {
        return BLE_HS_EUNKNOWN;
    }

    swap_buf(tmp, plaintext, 16);

    rc = mbedtls_aes_crypt_ecb(&ble_l2cap_sm_alg_ctxt, MBEDTLS_AES_ENCRYPT,
                               tmp, enc_data);
    if (rc != 0) {
        return BLE_HS_EUNKNOWN;
    }

    swap_in_place(enc_data, 16);

    BLE_HS_LOG(DEBUG, "ble_l2cap_sm_alg_encrypt; enc_data=");
    ble_hs_misc_log_flat_buf(enc_data, 16);
    BLE_HS_LOG(DEBUG, "\n");

    return 0;
}

int
ble_l2cap_sm_alg_s1(uint8_t *k, uint8_t *r1, uint8_t *r2, uint8_t *out)
{
    /* The most significant 64-bits of r1 are discarded to generate
     * r1' and the most significant 64-bits of r2 are discarded to
     * generate r2'.
     * r1' is concatenated with r2' to generate r' which is used as
     * the 128-bit input parameter plaintextData to security function e:
     *
     *    r' = r1' || r2'
     */
    memcpy(out, r2, 8);
    memcpy(out + 8, r1, 8);

    /* s1(k, r1 , r2) = e(k, r') */
    return ble_l2cap_sm_alg_encrypt(k, out, out);
}

int
ble_l2cap_sm_alg_c1(uint8_t *k, uint8_t *r,
                    uint8_t *preq, uint8_t *pres,
                    uint8_t iat, uint8_t rat,
                    uint8_t *ia, uint8_t *ra,
                    uint8_t *out_enc_data)
{
    uint8_t p1[16], p2[16];
    int rc;

    BLE_HS_LOG(DEBUG, "ble_l2cap_sm_alg_c1; k=");
    ble_hs_misc_log_flat_buf(k, 16);
    BLE_HS_LOG(DEBUG, " r=");
    ble_hs_misc_log_flat_buf(r, 16);
    BLE_HS_LOG(DEBUG, " iat=%d rat=%d", iat, rat);
    BLE_HS_LOG(DEBUG, " ia=");
    ble_hs_misc_log_flat_buf(ia, 6);
    BLE_HS_LOG(DEBUG, " ra=");
    ble_hs_misc_log_flat_buf(ra, 6);
    BLE_HS_LOG(DEBUG, " preq=");
    ble_hs_misc_log_flat_buf(preq, 7);
    BLE_HS_LOG(DEBUG, " pres=");
    ble_hs_misc_log_flat_buf(pres, 7);
    BLE_HS_LOG(DEBUG, "\n");

    /* pres, preq, rat and iat are concatenated to generate p1 */
    p1[0] = iat;
    p1[1] = rat;
    memcpy(p1 + 2, preq, 7);
    memcpy(p1 + 9, pres, 7);

    BLE_HS_LOG(DEBUG, "ble_l2cap_sm_alg_c1; p1=");
    ble_hs_misc_log_flat_buf(p1, sizeof p1);
    BLE_HS_LOG(DEBUG, "\n");

    /* c1 = e(k, e(k, r XOR p1) XOR p2) */

    /* Using out_enc_data as temporary output buffer */
    ble_l2cap_sm_alg_xor_128(r, p1, out_enc_data);

    rc = ble_l2cap_sm_alg_encrypt(k, out_enc_data, out_enc_data);
    if (rc != 0) {
        return rc;
    }

    /* ra is concatenated with ia and padding to generate p2 */
    memcpy(p2, ra, 6);
    memcpy(p2 + 6, ia, 6);
    memset(p2 + 12, 0, 4);

    BLE_HS_LOG(DEBUG, "ble_l2cap_sm_alg_c1; p2=");
    ble_hs_misc_log_flat_buf(p2, sizeof p2);
    BLE_HS_LOG(DEBUG, "\n");

    ble_l2cap_sm_alg_xor_128(out_enc_data, p2, out_enc_data);

    rc = ble_l2cap_sm_alg_encrypt(k, out_enc_data, out_enc_data);
    if (rc != 0) {
        return rc;
    }

    return 0;
}

#endif
