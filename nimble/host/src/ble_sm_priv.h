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

#ifndef H_BLE_SM_
#define H_BLE_SM_

#include <inttypes.h>
#include "os/queue.h"
#include "nimble/nimble_opt.h"

struct ble_gap_sec_state;
struct hci_le_lt_key_req;
struct hci_encrypt_change;

#define BLE_SM_MTU            65

#define BLE_SM_HDR_SZ         1

/**
 * | Parameter                          | Size (octets)     |
 * +------------------------------------+-------------------+
 * | (Code=0x01/0x02 [req/rsp])         | 1                 |
 * | IO Capability                      | 1                 |
 * | OOB data flag                      | 1                 |
 * | AuthReq                            | 1                 |
 * | Maximum Encryption Key Size        | 1                 |
 * | Initiator Key Distribution         | 1                 |
 * | Responder Key Distribution         | 1                 |
 */
#define BLE_SM_PAIR_CMD_SZ        6
struct ble_sm_pair_cmd {
    uint8_t io_cap;
    uint8_t oob_data_flag;
    uint8_t authreq;
    uint8_t max_enc_key_size;
    uint8_t init_key_dist;
    uint8_t resp_key_dist;
};

/**
 * | Parameter                          | Size (octets)     |
 * +------------------------------------+-------------------+
 * | (Code=0x03)                        | 1                 |
 * | Confirm Value                      | 16                |
 */
#define BLE_SM_PAIR_CONFIRM_SZ    16
struct ble_sm_pair_confirm {
    uint8_t value[16];
};

/**
 * | Parameter                          | Size (octets)     |
 * +------------------------------------+-------------------+
 * | (Code=0x04)                        | 1                 |
 * | Random Value                       | 16                |
 */
#define BLE_SM_PAIR_RANDOM_SZ     16
struct ble_sm_pair_random {
    uint8_t value[16];
};

/**
 * | Parameter                          | Size (octets)     |
 * +------------------------------------+-------------------+
 * | (Code=0x05)                        | 1                 |
 * | Reason                             | 1                 |
 */
#define BLE_SM_PAIR_FAIL_SZ       1
struct ble_sm_pair_fail {
    uint8_t reason;
};

/**
 * | Parameter                          | Size (octets)     |
 * +------------------------------------+-------------------+
 * | (Code=0x06)                        | 1                 |
 * | ltk                                | 16                |
 */
#define BLE_SM_ENC_INFO_SZ        16
struct ble_sm_enc_info {
    uint8_t ltk_le[16];
};

/**
 * | Parameter                          | Size (octets)     |
 * +------------------------------------+-------------------+
 * | (Code=0x07)                        | 1                 |
 * | EDIV                               | 2                 |
 * | RAND                               | 8                 |
 */
#define BLE_SM_MASTER_IDEN_SZ     10
struct ble_sm_master_iden {
    uint16_t ediv;
    uint64_t rand_val;
};

/**
 * | Parameter                          | Size (octets)     |
 * +------------------------------------+-------------------+
 * | (Code=0x08)                        | 1                 |
 * | irk                                | 16                |
 */
#define BLE_SM_IDEN_INFO_SZ      16
struct ble_sm_iden_info {
    /* Stored in little-endian. */
    uint8_t irk_le[16];
};

/**
 * | Parameter                          | Size (octets)     |
 * +------------------------------------+-------------------+
 * | (Code=0x09)                        | 1                 |
 * | addr_type                          | 1                 |
 * | address                            | 6                 |
 */
#define BLE_SM_IDEN_ADDR_INFO_SZ  7
struct ble_sm_iden_addr_info {
    uint8_t addr_type;
    uint8_t bd_addr_le[6];
};

/**
 * | Parameter                          | Size (octets)     |
 * +------------------------------------+-------------------+
 * | (Code=0x0A)                        | 1                 |
 * | csrk                               | 16                |
 */
#define BLE_SM_SIGNING_INFO_SZ    16
struct ble_sm_signing_info {
    uint8_t sig_key_le[16];
};

/**
 * | Parameter                          | Size (octets)     |
 * +------------------------------------+-------------------+
 * | (Code=0x0B)                        | 1                 |
 * | authreq                            | 1                 |
 */
#define BLE_SM_SEC_REQ_SZ         1
struct ble_sm_sec_req {
    uint8_t authreq;
};

/**
 * | Parameter                          | Size (octets)     |
 * +------------------------------------+-------------------+
 * | (Code=0x0c)                        | 1                 |
 * | Public Key X                       | 32                |
 * | Public Key Y                       | 32                |
 */
#define BLE_SM_PUBLIC_KEY_SZ      64
struct ble_sm_public_key {
    uint8_t x[32];
    uint8_t y[32];
};

/**
 * | Parameter                          | Size (octets)     |
 * +------------------------------------+-------------------+
 * | (Code=0x0d)                        | 1                 |
 * | DHKey Check                        | 16                |
 */
#define BLE_SM_DHKEY_CHECK_SZ     16
struct ble_sm_dhkey_check {
    uint8_t value[16];
};

#if NIMBLE_OPT(SM)

#define BLE_SM_PROC_STATE_NONE              ((uint8_t)-1)
    
#define BLE_SM_PROC_STATE_PAIR              0
#define BLE_SM_PROC_STATE_CONFIRM           1
#define BLE_SM_PROC_STATE_RANDOM            2
#define BLE_SM_PROC_STATE_LTK_START         3
#define BLE_SM_PROC_STATE_LTK_RESTORE       4
#define BLE_SM_PROC_STATE_ENC_START         5
#define BLE_SM_PROC_STATE_ENC_RESTORE       6
#define BLE_SM_PROC_STATE_KEY_EXCH          7
#define BLE_SM_PROC_STATE_SEC_REQ           8
#define BLE_SM_PROC_STATE_PUBLIC_KEY        9
#define BLE_SM_PROC_STATE_DHKEY_CHECK       10
#define BLE_SM_PROC_STATE_CNT               11

#define BLE_SM_PROC_F_INITIATOR             0x01
#define BLE_SM_PROC_F_TK_VALID              0x02
#define BLE_SM_PROC_F_RX_CONFIRM            0x04
#define BLE_SM_PROC_F_AUTHENTICATED         0x08
#define BLE_SM_PROC_F_KEY_EXCHANGE          0x10
#define BLE_SM_PROC_F_BONDED                0x20
#define BLE_SM_PROC_F_SC                    0x40

#define BLE_SM_KE_F_ENC_INFO                0x01
#define BLE_SM_KE_F_MASTER_IDEN             0x02
#define BLE_SM_KE_F_IDEN_INFO               0x04
#define BLE_SM_KE_F_ADDR_INFO               0x08
#define BLE_SM_KE_F_SIGN_INFO               0x10

typedef uint8_t ble_sm_proc_flags;

struct ble_sm_keys {
    unsigned ltk_valid:1;
    unsigned ediv_rand_valid:1;
    unsigned irk_valid:1;
    unsigned csrk_valid:1;
    unsigned addr_valid:1;
    uint16_t ediv;
    uint64_t rand_val;
    uint8_t addr_type;
    uint8_t ltk[16];
    uint8_t irk[16];
    uint8_t csrk[16];
    uint8_t addr[6];
};

struct ble_sm_proc {
    STAILQ_ENTRY(ble_sm_proc) next;

    uint32_t exp_os_ticks;
    ble_sm_proc_flags flags;
    uint16_t conn_handle;
    uint8_t pair_alg;
    uint8_t state;
    uint8_t rx_key_flags;
    /* XXX: Minimum security requirements. */

    struct ble_sm_pair_cmd pair_req;
    struct ble_sm_pair_cmd pair_rsp;
    struct ble_sm_public_key pub_key_our;
    struct ble_sm_public_key pub_key_their;
    uint8_t priv_key_our[32];
    uint8_t tk[16];
    uint8_t confirm_their[16];
    uint8_t randm[16];
    uint8_t rands[16];
    uint8_t ltk[16];
    uint8_t mackey[16];

    uint16_t ediv;
    uint64_t rand_num;

    struct ble_sm_keys our_keys;
    struct ble_sm_keys peer_keys;
};

struct ble_sm_result {
    int app_status;
    uint8_t sm_err;
    uint8_t passkey_action;
    void *state_arg;
    unsigned do_state:1;
    unsigned enc_cb:1;
    unsigned persist_keys:1;
};

#ifdef BLE_HS_DEBUG
void ble_sm_dbg_set_next_pair_rand(uint8_t *next_pair_rand);
void ble_sm_dbg_set_next_ediv(uint16_t next_ediv);
void ble_sm_dbg_set_next_start_rand(uint64_t next_start_rand);
void ble_sm_dbg_set_next_ltk(uint8_t *next_ltk);
void ble_sm_dbg_set_next_irk(uint8_t *next_irk);
void ble_sm_dbg_set_next_csrk(uint8_t *next_csrk);
int ble_sm_dbg_num_procs(void);
#endif

uint8_t ble_sm_build_authreq(void);

struct ble_l2cap_chan *ble_sm_create_chan(void);

void ble_sm_pair_cmd_parse(void *payload, int len,
                           struct ble_sm_pair_cmd *cmd);
int ble_sm_pair_cmd_is_valid(struct ble_sm_pair_cmd *cmd);
void ble_sm_pair_cmd_write(void *payload, int len, int is_req,
                           struct ble_sm_pair_cmd *cmd);
int ble_sm_pair_cmd_tx(uint16_t conn_handle, int is_req,
                       struct ble_sm_pair_cmd *cmd);
void ble_sm_pair_confirm_parse(void *payload, int len,
                               struct ble_sm_pair_confirm *cmd);
void ble_sm_pair_confirm_write(void *payload, int len,
                               struct ble_sm_pair_confirm *cmd);
int ble_sm_pair_confirm_tx(uint16_t conn_handle,
                           struct ble_sm_pair_confirm *cmd);
void ble_sm_pair_random_parse(void *payload, int len,
                              struct ble_sm_pair_random *cmd);
void ble_sm_pair_random_write(void *payload, int len,
                              struct ble_sm_pair_random *cmd);
int ble_sm_pair_random_tx(uint16_t conn_handle,
                          struct ble_sm_pair_random *cmd);
void ble_sm_pair_fail_parse(void *payload, int len,
                            struct ble_sm_pair_fail *cmd);
void ble_sm_pair_fail_write(void *payload, int len,
                            struct ble_sm_pair_fail *cmd);
int ble_sm_pair_fail_tx(uint16_t conn_handle, uint8_t reason);

int ble_sm_alg_s1(uint8_t *k, uint8_t *r1, uint8_t *r2, uint8_t *out);
int ble_sm_alg_c1(uint8_t *k, uint8_t *r,
                  uint8_t *preq, uint8_t *pres,
                  uint8_t iat, uint8_t rat,
                  uint8_t *ia, uint8_t *ra,
                  uint8_t *out_enc_data);
int ble_sm_alg_f4(uint8_t *u, uint8_t *v, uint8_t *x, uint8_t z,
                  uint8_t *out_enc_data);
int ble_sm_alg_g2(uint8_t *u, uint8_t *v, uint8_t *x, uint8_t *y,
                  uint32_t *passkey);
int ble_sm_alg_f5(uint8_t *w, uint8_t *n1, uint8_t *n2, uint8_t a1t,
                  uint8_t *a1, uint8_t a2t, uint8_t *a2,
                  uint8_t *mackey, uint8_t *ltk);
int ble_sm_alg_f6(uint8_t *w, uint8_t *n1, uint8_t *n2, uint8_t *r,
                  uint8_t *iocap, uint8_t a1t, uint8_t *a1,
                  uint8_t a2t, uint8_t *a2, uint8_t *check);
int ble_sm_alg_gen_key_pair(uint8_t *pub, uint8_t *priv);

void ble_sm_enc_info_parse(void *payload, int len,
                           struct ble_sm_enc_info *cmd);
int ble_sm_enc_info_tx(uint16_t conn_handle,
                       struct ble_sm_enc_info *cmd);
void ble_sm_master_iden_parse(void *payload, int len,
                              struct ble_sm_master_iden *cmd);
int ble_sm_master_iden_tx(uint16_t conn_handle,
                          struct ble_sm_master_iden *cmd);
void ble_sm_iden_info_parse(void *payload, int len,
                            struct ble_sm_iden_info *cmd);
int ble_sm_iden_info_tx(uint16_t conn_handle,
                        struct ble_sm_iden_info *cmd);
void ble_sm_iden_addr_parse(void *payload, int len,
                            struct ble_sm_iden_addr_info *cmd);
int ble_sm_iden_addr_tx(uint16_t conn_handle,
                        struct ble_sm_iden_addr_info *cmd);
void ble_sm_signing_info_parse(void *payload, int len,
                               struct ble_sm_signing_info *cmd);
int ble_sm_signing_info_tx(uint16_t conn_handle,
                           struct ble_sm_signing_info *cmd);
void ble_sm_sec_req_parse(void *payload, int len,
                          struct ble_sm_sec_req *cmd);
void ble_sm_sec_req_write(void *payload, int len,
                          struct ble_sm_sec_req *cmd);
int ble_sm_sec_req_tx(uint16_t conn_handle,
                      struct ble_sm_sec_req *cmd);
int ble_sm_public_key_parse(void *payload, int len,
                            struct ble_sm_public_key *cmd);
int ble_sm_public_key_write(void *payload, int len,
                            struct ble_sm_public_key *cmd);
int ble_sm_public_key_tx(uint16_t conn_handle,
                         struct ble_sm_public_key *cmd);
int ble_sm_dhkey_check_parse(void *payload, int len,
                             struct ble_sm_dhkey_check *cmd);
int ble_sm_dhkey_check_write(void *payload, int len,
                             struct ble_sm_dhkey_check *cmd);
int ble_sm_dhkey_check_tx(uint16_t conn_handle,
                          struct ble_sm_dhkey_check *cmd);

void ble_sm_rx_encryption_change(struct hci_encrypt_change *evt);
int ble_sm_rx_lt_key_req(struct hci_le_lt_key_req *evt);

int ble_sm_lgcy_next_state(struct ble_sm_proc *proc);
int ble_sm_lgcy_passkey_action(struct ble_sm_proc *proc);
void ble_sm_lgcy_confirm_go(struct ble_sm_proc *proc,
                            struct ble_sm_result *res);
void ble_sm_lgcy_random_go(struct ble_sm_proc *proc,
                           struct ble_sm_result *res);
void ble_sm_lgcy_random_handle(struct ble_sm_proc *proc,
                               struct ble_sm_result *res);

int ble_sm_sc_next_state(struct ble_sm_proc *proc);
int ble_sm_sc_passkey_action(struct ble_sm_proc *proc);
void ble_sm_sc_confirm_go(struct ble_sm_proc *proc,
                          struct ble_sm_result *res);
void ble_sm_sc_random_go(struct ble_sm_proc *proc,
                         struct ble_sm_result *res);
void ble_sm_sc_random_handle(struct ble_sm_proc *proc,
                             struct ble_sm_result *res);
void ble_sm_sc_public_key_go(struct ble_sm_proc *proc,
                             struct ble_sm_result *res,
                             void *arg);
void ble_sm_sc_public_key_handle(struct ble_sm_proc *proc,
                                 struct ble_sm_public_key *cmd,
                                 struct ble_sm_result *res);
void ble_sm_sc_rx_public_key(uint16_t conn_handle, uint8_t op,
                             struct os_mbuf **om, struct ble_sm_result *res);
void ble_sm_sc_dhkey_check_go(struct ble_sm_proc *proc,
                                 struct ble_sm_result *res, void *arg);
void ble_sm_sc_rx_dhkey_check(uint16_t conn_handle, uint8_t op,
                              struct os_mbuf **om, struct ble_sm_result *res);

struct ble_sm_proc *ble_sm_proc_find(uint16_t conn_handle, uint8_t state,
                                     int is_initiator,
                                     struct ble_sm_proc **out_prev);
int ble_sm_gen_pub_priv(struct ble_sm_proc *proc,
                        uint8_t *pub, uint8_t *priv);
uint8_t *ble_sm_our_pair_rand(struct ble_sm_proc *proc);
uint8_t *ble_sm_their_pair_rand(struct ble_sm_proc *proc);
void ble_sm_go(struct ble_sm_proc *proc, struct ble_sm_result *res, void *arg);
void ble_sm_process_result(uint16_t conn_handle, struct ble_sm_result *res);


void ble_sm_heartbeat(void);
int ble_sm_pair_initiate(uint16_t conn_handle);
int ble_sm_slave_initiate(uint16_t conn_handle);
int ble_sm_enc_initiate(uint16_t conn_handle, uint8_t *ltk,
                        uint16_t ediv, uint64_t rand_val, int auth);
int ble_sm_init(void);

#else

#ifdef BLE_HS_DEBUG
#define ble_sm_dbg_set_next_rand(next_rand)
#define ble_sm_dbg_num_procs() 0
#endif

#define ble_sm_create_chan() NULL

#define ble_sm_pair_cmd_tx(conn_handle, is_req, cmd) BLE_HS_ENOTSUP
#define ble_sm_pair_confirm_tx(conn_handle, cmd) BLE_HS_ENOTSUP
#define ble_sm_pair_random_tx(conn_handle, cmd) BLE_HS_ENOTSUP
#define ble_sm_pair_fail_tx(conn_handle, cmd) BLE_HS_ENOTSUP

#define ble_sm_alg_s1(k, r1, r2, out) BLE_HS_ENOTSUP
#define ble_sm_alg_c1(k, r, preq, pres, iat, rat, ia, ra, out_enc_data) \
    BLE_HS_ENOTSUP

#define ble_sm_rx_encryption_change(evt) ((void)(evt))
#define ble_sm_rx_lt_key_req(evt) ((void)(evt))

#define ble_sm_heartbeat()
#define ble_sm_init() 0

#endif

#endif
