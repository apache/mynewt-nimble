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

#include <errno.h>
#include <string.h>
#include "os/os.h"
#include "nimble/ble.h"
#include "ble_hs_priv.h"
#include "host/ble_att.h"
#include "host/ble_uuid.h"
#include "ble_hs_priv.h"

static void *
ble_att_init_parse(uint8_t op, void *payload, int min_len, int actual_len)
{
    uint8_t *u8ptr;

    BLE_HS_DBG_ASSERT(actual_len >= min_len);

    u8ptr = payload;
    BLE_HS_DBG_ASSERT(u8ptr[0] == op);

    return u8ptr + 1;
}

static void *
ble_att_init_parse_2op(uint8_t op1, uint8_t op2, void *payload,
                       int min_len, int actual_len)
{
    uint8_t *u8ptr;

    BLE_HS_DBG_ASSERT(actual_len >= min_len);

    u8ptr = payload;
    BLE_HS_DBG_ASSERT(u8ptr[0] == op1 || u8ptr[0] == op2);

    return u8ptr + 1;
}

static void *
ble_att_init_write(uint8_t op, void *payload, int min_len, int actual_len)
{
    uint8_t *u8ptr;

    BLE_HS_DBG_ASSERT(actual_len >= min_len);

    u8ptr = payload;
    u8ptr[0] = op;

    return u8ptr + 1;
}

static void
ble_att_error_rsp_swap(struct ble_att_error_rsp *dst,
                       struct ble_att_error_rsp *src)
{
    dst->baep_req_op = src->baep_req_op;
    dst->baep_handle = TOFROMLE16(src->baep_handle);
    dst->baep_error_code = src->baep_error_code;
}

void
ble_att_error_rsp_parse(void *payload, int len, struct ble_att_error_rsp *dst)
{
    struct ble_att_error_rsp *src;

    src = ble_att_init_parse(BLE_ATT_OP_ERROR_RSP, payload,
                             BLE_ATT_ERROR_RSP_SZ, len);
    ble_att_error_rsp_swap(dst, src);
}

void
ble_att_error_rsp_write(void *payload, int len, struct ble_att_error_rsp *src)
{
    struct ble_att_error_rsp *dst;

    dst = ble_att_init_write(BLE_ATT_OP_ERROR_RSP, payload,
                             BLE_ATT_ERROR_RSP_SZ, len);
    ble_att_error_rsp_swap(dst, src);
}

void
ble_att_error_rsp_log(struct ble_att_error_rsp *cmd)
{
    BLE_HS_LOG(DEBUG, "req_op=%d handle=0x%04x error_code=%d",
               cmd->baep_req_op, cmd->baep_handle, cmd->baep_error_code);
}

static void
ble_att_mtu_cmd_swap(struct ble_att_mtu_cmd *dst, struct ble_att_mtu_cmd *src)
{
    dst->bamc_mtu = TOFROMLE16(src->bamc_mtu);
}

void
ble_att_mtu_cmd_parse(void *payload, int len, struct ble_att_mtu_cmd *dst)
{
    struct ble_att_mtu_cmd *src;

    src = ble_att_init_parse_2op(BLE_ATT_OP_MTU_REQ, BLE_ATT_OP_MTU_RSP,
                                 payload, BLE_ATT_MTU_CMD_SZ, len);
    ble_att_mtu_cmd_swap(dst, src);
}

void
ble_att_mtu_req_write(void *payload, int len, struct ble_att_mtu_cmd *src)
{
    struct ble_att_mtu_cmd *dst;

    dst = ble_att_init_write(BLE_ATT_OP_MTU_REQ, payload,
                             BLE_ATT_MTU_CMD_SZ, len);
    ble_att_mtu_cmd_swap(dst, src);
}

void
ble_att_mtu_rsp_write(void *payload, int len, struct ble_att_mtu_cmd *src)
{
    struct ble_att_mtu_cmd *dst;

    dst = ble_att_init_write(BLE_ATT_OP_MTU_RSP, payload,
                             BLE_ATT_MTU_CMD_SZ, len);
    ble_att_mtu_cmd_swap(dst, src);
}

void
ble_att_mtu_cmd_log(struct ble_att_mtu_cmd *cmd)
{
    BLE_HS_LOG(DEBUG, "mtu=%d", cmd->bamc_mtu);
}

static void
ble_att_find_info_req_swap(struct ble_att_find_info_req *dst,
                           struct ble_att_find_info_req *src)
{
    dst->bafq_start_handle = TOFROMLE16(src->bafq_start_handle);
    dst->bafq_end_handle = TOFROMLE16(src->bafq_end_handle);
}

void
ble_att_find_info_req_parse(void *payload, int len,
                            struct ble_att_find_info_req *dst)
{
    struct ble_att_find_info_req *src;

    src = ble_att_init_parse(BLE_ATT_OP_FIND_INFO_REQ, payload,
                             BLE_ATT_FIND_INFO_REQ_SZ, len);
    ble_att_find_info_req_swap(dst, src);
}

void
ble_att_find_info_req_write(void *payload, int len,
                            struct ble_att_find_info_req *src)
{
    struct ble_att_find_info_req *dst;

    dst = ble_att_init_write(BLE_ATT_OP_FIND_INFO_REQ, payload,
                             BLE_ATT_FIND_INFO_REQ_SZ, len);
    ble_att_find_info_req_swap(dst, src);
}

void
ble_att_find_info_req_log(struct ble_att_find_info_req *cmd)
{
    BLE_HS_LOG(DEBUG, "start_handle=0x%04x end_handle=0x%04x",
               cmd->bafq_start_handle, cmd->bafq_end_handle);
}

static void
ble_att_find_info_rsp_swap(struct ble_att_find_info_rsp *dst,
                           struct ble_att_find_info_rsp *src)
{
    dst->bafp_format = src->bafp_format;
}

void
ble_att_find_info_rsp_parse(void *payload, int len,
                            struct ble_att_find_info_rsp *dst)
{
    struct ble_att_find_info_rsp *src;

    src = ble_att_init_parse(BLE_ATT_OP_FIND_INFO_RSP, payload,
                             BLE_ATT_FIND_INFO_RSP_BASE_SZ, len);
    ble_att_find_info_rsp_swap(dst, src);
}

void
ble_att_find_info_rsp_write(void *payload, int len,
                            struct ble_att_find_info_rsp *src)
{
    struct ble_att_find_info_rsp *dst;

    dst = ble_att_init_write(BLE_ATT_OP_FIND_INFO_RSP, payload,
                             BLE_ATT_FIND_INFO_RSP_BASE_SZ, len);
    ble_att_find_info_rsp_swap(dst, src);
}

void
ble_att_find_info_rsp_log(struct ble_att_find_info_rsp *cmd)
{
    BLE_HS_LOG(DEBUG, "format=%d", cmd->bafp_format);
}

static void
ble_att_find_type_value_req_swap(struct ble_att_find_type_value_req *dst,
                                 struct ble_att_find_type_value_req *src)
{
    dst->bavq_start_handle = TOFROMLE16(src->bavq_start_handle);
    dst->bavq_end_handle = TOFROMLE16(src->bavq_end_handle);
    dst->bavq_attr_type = TOFROMLE16(src->bavq_attr_type);
}

void
ble_att_find_type_value_req_parse(void *payload, int len,
                                  struct ble_att_find_type_value_req *dst)
{
    struct ble_att_find_type_value_req *src;

    src = ble_att_init_parse(BLE_ATT_OP_FIND_TYPE_VALUE_REQ, payload,
                             BLE_ATT_FIND_TYPE_VALUE_REQ_BASE_SZ, len);
    ble_att_find_type_value_req_swap(dst, src);
}

void
ble_att_find_type_value_req_write(void *payload, int len,
                                  struct ble_att_find_type_value_req *src)
{
    struct ble_att_find_type_value_req *dst;

    dst = ble_att_init_write(BLE_ATT_OP_FIND_TYPE_VALUE_REQ, payload,
                             BLE_ATT_FIND_TYPE_VALUE_REQ_BASE_SZ, len);
    ble_att_find_type_value_req_swap(dst, src);
}

void
ble_att_find_type_value_req_log(struct ble_att_find_type_value_req *cmd)
{
    BLE_HS_LOG(DEBUG, "start_handle=0x%04x end_handle=0x%04x attr_type=%d",
               cmd->bavq_start_handle, cmd->bavq_end_handle,
               cmd->bavq_attr_type);
}

static void
ble_att_read_type_req_swap(struct ble_att_read_type_req *dst,
                           struct ble_att_read_type_req *src)
{
    dst->batq_start_handle = TOFROMLE16(src->batq_start_handle);
    dst->batq_end_handle = TOFROMLE16(src->batq_end_handle);
}

void
ble_att_read_type_req_parse(void *payload, int len,
                            struct ble_att_read_type_req *dst)
{
    struct ble_att_read_type_req *src;

    src = ble_att_init_parse(BLE_ATT_OP_READ_TYPE_REQ, payload,
                             BLE_ATT_READ_TYPE_REQ_BASE_SZ, len);
    ble_att_read_type_req_swap(dst, src);
}

void
ble_att_read_type_req_write(void *payload, int len,
                            struct ble_att_read_type_req *src)
{
    struct ble_att_read_type_req *dst;

    dst = ble_att_init_write(BLE_ATT_OP_READ_TYPE_REQ, payload,
                             BLE_ATT_READ_TYPE_REQ_BASE_SZ, len);
    ble_att_read_type_req_swap(dst, src);
}

void
ble_att_read_type_req_log(struct ble_att_read_type_req *cmd)
{
    BLE_HS_LOG(DEBUG, "start_handle=0x%04x end_handle=0x%04x",
               cmd->batq_start_handle, cmd->batq_end_handle);
}

static void
ble_att_read_type_rsp_swap(struct ble_att_read_type_rsp *dst,
                           struct ble_att_read_type_rsp *src)
{
    dst->batp_length = src->batp_length;
}

void
ble_att_read_type_rsp_parse(void *payload, int len,
                            struct ble_att_read_type_rsp *dst)
{
    struct ble_att_read_type_rsp *src;

    src = ble_att_init_parse(BLE_ATT_OP_READ_TYPE_RSP, payload,
                             BLE_ATT_READ_TYPE_RSP_BASE_SZ, len);
    ble_att_read_type_rsp_swap(dst, src);
}

void
ble_att_read_type_rsp_write(void *payload, int len,
                            struct ble_att_read_type_rsp *src)
{
    struct ble_att_read_type_rsp *dst;

    dst = ble_att_init_write(BLE_ATT_OP_READ_TYPE_RSP, payload,
                             BLE_ATT_READ_TYPE_RSP_BASE_SZ, len);
    ble_att_read_type_rsp_swap(dst, src);
}

void
ble_att_read_type_rsp_log(struct ble_att_read_type_rsp *cmd)
{
    BLE_HS_LOG(DEBUG, "length=%d", cmd->batp_length);
}

static void
ble_att_read_req_swap(struct ble_att_read_req *dst,
                      struct ble_att_read_req *src)
{
    dst->barq_handle = TOFROMLE16(src->barq_handle);
}

void
ble_att_read_req_parse(void *payload, int len, struct ble_att_read_req *dst)
{
    struct ble_att_read_req *src;

    src = ble_att_init_parse(BLE_ATT_OP_READ_REQ, payload,
                             BLE_ATT_READ_REQ_SZ, len);
    ble_att_read_req_swap(dst, src);
}

void
ble_att_read_req_write(void *payload, int len, struct ble_att_read_req *src)
{
    struct ble_att_read_req *dst;

    dst = ble_att_init_write(BLE_ATT_OP_READ_REQ, payload,
                             BLE_ATT_READ_REQ_SZ, len);
    ble_att_read_req_swap(dst, src);
}

void
ble_att_read_req_log(struct ble_att_read_req *cmd)
{
    BLE_HS_LOG(DEBUG, "handle=0x%04x", cmd->barq_handle);
}

static void
ble_att_read_blob_req_swap(struct ble_att_read_blob_req *dst,
                           struct ble_att_read_blob_req *src)
{
    dst->babq_handle = TOFROMLE16(src->babq_handle);
    dst->babq_offset = TOFROMLE16(src->babq_offset);
}

void
ble_att_read_blob_req_parse(void *payload, int len,
                            struct ble_att_read_blob_req *dst)
{
    struct ble_att_read_blob_req *src;

    src = ble_att_init_parse(BLE_ATT_OP_READ_BLOB_REQ, payload,
                             BLE_ATT_READ_BLOB_REQ_SZ, len);
    ble_att_read_blob_req_swap(dst, src);
}

void
ble_att_read_blob_req_write(void *payload, int len,
                            struct ble_att_read_blob_req *src)
{
    struct ble_att_read_blob_req *dst;

    dst = ble_att_init_write(BLE_ATT_OP_READ_BLOB_REQ, payload,
                             BLE_ATT_READ_BLOB_REQ_SZ, len);
    ble_att_read_blob_req_swap(dst, src);
}

void
ble_att_read_blob_req_log(struct ble_att_read_blob_req *cmd)
{
    BLE_HS_LOG(DEBUG, "handle=0x%04x offset=%d", cmd->babq_handle,
               cmd->babq_offset);
}

void
ble_att_read_mult_req_parse(void *payload, int len)
{
    ble_att_init_parse(BLE_ATT_OP_READ_MULT_REQ, payload,
                       BLE_ATT_READ_MULT_REQ_BASE_SZ, len);
}

void
ble_att_read_mult_req_write(void *payload, int len)
{
    ble_att_init_write(BLE_ATT_OP_READ_MULT_REQ, payload,
                       BLE_ATT_READ_MULT_REQ_BASE_SZ, len);
}

void
ble_att_read_mult_rsp_parse(void *payload, int len)
{
    ble_att_init_parse(BLE_ATT_OP_READ_MULT_RSP, payload,
                       BLE_ATT_READ_MULT_RSP_BASE_SZ, len);
}

void
ble_att_read_mult_rsp_write(void *payload, int len)
{
    ble_att_init_write(BLE_ATT_OP_READ_MULT_RSP, payload,
                       BLE_ATT_READ_MULT_RSP_BASE_SZ, len);
}

static void
ble_att_read_group_type_req_swap(struct ble_att_read_group_type_req *dst,
                                 struct ble_att_read_group_type_req *src)
{
    dst->bagq_start_handle = TOFROMLE16(src->bagq_start_handle);
    dst->bagq_end_handle = TOFROMLE16(src->bagq_end_handle);
}

void
ble_att_read_group_type_req_parse(void *payload, int len,
                                  struct ble_att_read_group_type_req *dst)
{
    struct ble_att_read_group_type_req *src;

    src = ble_att_init_parse(BLE_ATT_OP_READ_GROUP_TYPE_REQ, payload,
                             BLE_ATT_READ_GROUP_TYPE_REQ_BASE_SZ, len);
    ble_att_read_group_type_req_swap(dst, src);
}

void
ble_att_read_group_type_req_write(void *payload, int len,
                                  struct ble_att_read_group_type_req *src)
{
    struct ble_att_read_group_type_req *dst;

    dst = ble_att_init_write(BLE_ATT_OP_READ_GROUP_TYPE_REQ, payload,
                             BLE_ATT_READ_GROUP_TYPE_REQ_BASE_SZ, len);
    ble_att_read_group_type_req_swap(dst, src);
}

void
ble_att_read_group_type_req_log(struct ble_att_read_group_type_req *cmd)
{
    BLE_HS_LOG(DEBUG, "start_handle=0x%04x end_handle=0x%04x",
               cmd->bagq_start_handle, cmd->bagq_end_handle);
}

static void
ble_att_read_group_type_rsp_swap(struct ble_att_read_group_type_rsp *dst,
                                 struct ble_att_read_group_type_rsp *src)
{
    dst->bagp_length = src->bagp_length;
}

void
ble_att_read_group_type_rsp_parse(void *payload, int len,
                                  struct ble_att_read_group_type_rsp *dst)
{
    struct ble_att_read_group_type_rsp *src;

    src = ble_att_init_parse(BLE_ATT_OP_READ_GROUP_TYPE_RSP, payload,
                             BLE_ATT_READ_GROUP_TYPE_RSP_BASE_SZ, len);
    ble_att_read_group_type_rsp_swap(dst, src);
}

void
ble_att_read_group_type_rsp_write(void *payload, int len,
                                  struct ble_att_read_group_type_rsp *src)
{
    struct ble_att_read_group_type_rsp *dst;

    dst = ble_att_init_write(BLE_ATT_OP_READ_GROUP_TYPE_RSP, payload,
                             BLE_ATT_READ_GROUP_TYPE_RSP_BASE_SZ, len);
    ble_att_read_group_type_rsp_swap(dst, src);
}

void
ble_att_read_group_type_rsp_log(struct ble_att_read_group_type_rsp *cmd)
{
    BLE_HS_LOG(DEBUG, "length=%d", cmd->bagp_length);
}

static void
ble_att_write_req_swap(struct ble_att_write_req *dst,
                       struct ble_att_write_req *src)
{
    dst->bawq_handle = TOFROMLE16(src->bawq_handle);
}

void
ble_att_write_req_parse(void *payload, int len, struct ble_att_write_req *dst)
{
    struct ble_att_write_req *src;

    src = ble_att_init_parse(BLE_ATT_OP_WRITE_REQ, payload,
                             BLE_ATT_WRITE_REQ_BASE_SZ, len);
    ble_att_write_req_swap(dst, src);
}

void
ble_att_write_cmd_parse(void *payload, int len, struct ble_att_write_req *dst)
{
    struct ble_att_write_req *src;

    src = ble_att_init_parse(BLE_ATT_OP_WRITE_CMD, payload,
                             BLE_ATT_WRITE_REQ_BASE_SZ, len);
    ble_att_write_req_swap(dst, src);
}

void
ble_att_write_req_write(void *payload, int len, struct ble_att_write_req *src)
{
    struct ble_att_write_req *dst;

    dst = ble_att_init_write(BLE_ATT_OP_WRITE_REQ, payload,
                             BLE_ATT_WRITE_REQ_BASE_SZ, len);
    ble_att_write_req_swap(dst, src);
}

void
ble_att_write_cmd_write(void *payload, int len, struct ble_att_write_req *src)
{
    struct ble_att_write_req *dst;

    dst = ble_att_init_write(BLE_ATT_OP_WRITE_CMD, payload,
                             BLE_ATT_WRITE_REQ_BASE_SZ, len);
    ble_att_write_req_swap(dst, src);
}

void
ble_att_write_cmd_log(struct ble_att_write_req *cmd)
{
    BLE_HS_LOG(DEBUG, "handle=0x%04x", cmd->bawq_handle);
}

static void
ble_att_prep_write_cmd_swap(struct ble_att_prep_write_cmd *dst,
                            struct ble_att_prep_write_cmd *src)
{
    dst->bapc_handle = TOFROMLE16(src->bapc_handle);
    dst->bapc_offset = TOFROMLE16(src->bapc_offset);
}

void
ble_att_prep_write_req_parse(void *payload, int len,
                             struct ble_att_prep_write_cmd *dst)
{
    struct ble_att_prep_write_cmd *src;

    src = ble_att_init_parse(BLE_ATT_OP_PREP_WRITE_REQ, payload,
                             BLE_ATT_PREP_WRITE_CMD_BASE_SZ, len);
    ble_att_prep_write_cmd_swap(dst, src);
}

void
ble_att_prep_write_req_write(void *payload, int len,
                             struct ble_att_prep_write_cmd *src)
{
    struct ble_att_prep_write_cmd *dst;

    dst = ble_att_init_write(BLE_ATT_OP_PREP_WRITE_REQ, payload,
                             BLE_ATT_PREP_WRITE_CMD_BASE_SZ, len);
    ble_att_prep_write_cmd_swap(dst, src);
}

void
ble_att_prep_write_rsp_parse(void *payload, int len,
                             struct ble_att_prep_write_cmd *dst)
{
    struct ble_att_prep_write_cmd *src;

    src = ble_att_init_parse(BLE_ATT_OP_PREP_WRITE_RSP, payload,
                             BLE_ATT_PREP_WRITE_CMD_BASE_SZ, len);
    ble_att_prep_write_cmd_swap(dst, src);
}

void
ble_att_prep_write_rsp_write(void *payload, int len,
                             struct ble_att_prep_write_cmd *src)
{
    struct ble_att_prep_write_cmd *dst;

    dst = ble_att_init_write(BLE_ATT_OP_PREP_WRITE_RSP, payload,
                             BLE_ATT_PREP_WRITE_CMD_BASE_SZ, len);
    ble_att_prep_write_cmd_swap(dst, src);
}

void
ble_att_prep_write_cmd_log(struct ble_att_prep_write_cmd *cmd)
{
    BLE_HS_LOG(DEBUG, "handle=0x%04x offset=%d", cmd->bapc_handle,
               cmd->bapc_offset);
}

static void
ble_att_exec_write_req_swap(struct ble_att_exec_write_req *dst,
                            struct ble_att_exec_write_req *src)
{
    dst->baeq_flags = src->baeq_flags;
}

void
ble_att_exec_write_req_parse(void *payload, int len,
                             struct ble_att_exec_write_req *dst)
{
    struct ble_att_exec_write_req *src;

    src = ble_att_init_parse(BLE_ATT_OP_EXEC_WRITE_REQ, payload,
                             BLE_ATT_EXEC_WRITE_REQ_SZ, len);
    ble_att_exec_write_req_swap(dst, src);
}

void
ble_att_exec_write_req_write(void *payload, int len,
                             struct ble_att_exec_write_req *src)
{
    struct ble_att_exec_write_req *dst;

    dst = ble_att_init_write(BLE_ATT_OP_EXEC_WRITE_REQ, payload,
                             BLE_ATT_EXEC_WRITE_REQ_SZ, len);
    ble_att_exec_write_req_swap(dst, src);
}

void
ble_att_exec_write_req_log(struct ble_att_exec_write_req *cmd)
{
    BLE_HS_LOG(DEBUG, "flags=0x%02x", cmd->baeq_flags);
}

void
ble_att_exec_write_rsp_parse(void *payload, int len)
{
    ble_att_init_parse(BLE_ATT_OP_EXEC_WRITE_RSP, payload,
                       BLE_ATT_EXEC_WRITE_RSP_SZ, len);
}

void
ble_att_exec_write_rsp_write(void *payload, int len)
{
    ble_att_init_write(BLE_ATT_OP_EXEC_WRITE_RSP, payload,
                       BLE_ATT_EXEC_WRITE_RSP_SZ, len);
}

static void
ble_att_notify_req_swap(struct ble_att_notify_req *dst,
                        struct ble_att_notify_req *src)
{
    dst->banq_handle = TOFROMLE16(src->banq_handle);
}

void
ble_att_notify_req_parse(void *payload, int len,
                         struct ble_att_notify_req *dst)
{
    struct ble_att_notify_req *src;

    src = ble_att_init_parse(BLE_ATT_OP_NOTIFY_REQ, payload,
                             BLE_ATT_NOTIFY_REQ_BASE_SZ, len);
    ble_att_notify_req_swap(dst, src);
}

void
ble_att_notify_req_write(void *payload, int len,
                         struct ble_att_notify_req *src)
{
    struct ble_att_notify_req *dst;

    dst = ble_att_init_write(BLE_ATT_OP_NOTIFY_REQ, payload,
                             BLE_ATT_NOTIFY_REQ_BASE_SZ, len);
    ble_att_notify_req_swap(dst, src);
}

void
ble_att_notify_req_log(struct ble_att_notify_req *cmd)
{
    BLE_HS_LOG(DEBUG, "handle=0x%04x", cmd->banq_handle);
}

static void
ble_att_indicate_req_swap(struct ble_att_indicate_req *dst,
                          struct ble_att_indicate_req *src)
{
    dst->baiq_handle = TOFROMLE16(src->baiq_handle);
}

void
ble_att_indicate_req_parse(void *payload, int len,
                           struct ble_att_indicate_req *dst)
{
    struct ble_att_indicate_req *src;

    src = ble_att_init_parse(BLE_ATT_OP_INDICATE_REQ, payload,
                             BLE_ATT_INDICATE_REQ_BASE_SZ, len);
    ble_att_indicate_req_swap(dst, src);
}

void
ble_att_indicate_req_write(void *payload, int len,
                           struct ble_att_indicate_req *src)
{
    struct ble_att_indicate_req *dst;

    dst = ble_att_init_write(BLE_ATT_OP_INDICATE_REQ, payload,
                             BLE_ATT_INDICATE_REQ_BASE_SZ, len);
    ble_att_indicate_req_swap(dst, src);
}

void
ble_att_indicate_req_log(struct ble_att_indicate_req *cmd)
{
    BLE_HS_LOG(DEBUG, "handle=0x%04x", cmd->baiq_handle);
}

void
ble_att_indicate_rsp_parse(void *payload, int len)
{
    ble_att_init_parse(BLE_ATT_OP_INDICATE_RSP, payload,
                       BLE_ATT_INDICATE_RSP_SZ, len);
}

void
ble_att_indicate_rsp_write(void *payload, int len)
{
    ble_att_init_write(BLE_ATT_OP_INDICATE_RSP, payload,
                       BLE_ATT_INDICATE_RSP_SZ, len);
}
