#include <errno.h>
#include "os/os.h"
#include "nimble/ble.h"
#include "ble_l2cap.h"
#include "ble_l2cap_util.h"
#include "ble_hs_att_cmd.h"

int
ble_hs_att_read_req_parse(void *payload, int len,
                          struct ble_hs_att_read_req *req)
{
    uint8_t *u8ptr;

    if (len < BLE_HS_ATT_READ_REQ_SZ) {
        return EMSGSIZE;
    }

    u8ptr = payload;

    req->bharq_op = u8ptr[0];
    req->bharq_handle = le16toh(u8ptr + 1);

    if (req->bharq_op != BLE_HS_ATT_OP_READ_REQ) {
        return EINVAL;
    }

    return 0;
}

int
ble_hs_att_read_req_write(void *payload, int len,
                          struct ble_hs_att_read_req *req)
{
    uint8_t *u8ptr;

    if (len < BLE_HS_ATT_READ_REQ_SZ) {
        return EMSGSIZE;
    }

    u8ptr = payload;

    u8ptr[0] = req->bharq_op;
    htole16(u8ptr + 1, req->bharq_handle);

    return 0;
}

int
ble_hs_att_error_rsp_parse(void *payload, int len,
                           struct ble_hs_att_error_rsp *rsp)
{
    uint8_t *u8ptr;

    if (len < BLE_HS_ATT_ERROR_RSP_SZ) {
        return EMSGSIZE;
    }

    u8ptr = payload;

    rsp->bhaep_op = u8ptr[0];
    rsp->bhaep_req_op = u8ptr[1];
    rsp->bhaep_handle = le16toh(u8ptr + 2);
    rsp->bhaep_error_code = u8ptr[4];

    if (rsp->bhaep_op != BLE_HS_ATT_OP_ERROR_RSP) {
        return EINVAL;
    }

    return 0;
}

int
ble_hs_att_error_rsp_write(void *payload, int len,
                           struct ble_hs_att_error_rsp *rsp)
{
    uint8_t *u8ptr;

    if (len < BLE_HS_ATT_ERROR_RSP_SZ) {
        return EMSGSIZE;
    }

    u8ptr = payload;

    u8ptr[0] = rsp->bhaep_op;
    u8ptr[1] = rsp->bhaep_req_op;
    htole16(u8ptr + 2, rsp->bhaep_handle);
    u8ptr[4] = rsp->bhaep_error_code;

    return 0;
}
