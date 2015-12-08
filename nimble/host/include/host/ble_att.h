#ifndef H_BLE_ATT_
#define H_BLE_ATT_

#define BLE_ATT_OP_ERROR_RSP             0x01
#define BLE_ATT_OP_MTU_REQ               0x02
#define BLE_ATT_OP_MTU_RSP               0x03
#define BLE_ATT_OP_FIND_INFO_REQ         0x04
#define BLE_ATT_OP_FIND_INFO_RSP         0x05
#define BLE_ATT_OP_FIND_TYPE_VALUE_REQ   0x06
#define BLE_ATT_OP_FIND_TYPE_VALUE_RSP   0x07
#define BLE_ATT_OP_READ_TYPE_REQ         0x08
#define BLE_ATT_OP_READ_TYPE_RSP         0x09
#define BLE_ATT_OP_READ_REQ              0x0a
#define BLE_ATT_OP_READ_RSP              0x0b
#define BLE_ATT_OP_READ_GROUP_TYPE_REQ   0x10
#define BLE_ATT_OP_READ_GROUP_TYPE_RSP   0x11
#define BLE_ATT_OP_WRITE_REQ             0x12
#define BLE_ATT_OP_WRITE_RSP             0x13

union ble_att_svr_handle_arg {
    struct {
        void *attr_data;
        int attr_len;
    } aha_read;

    struct {
        struct os_mbuf *om;
        int attr_len;
    } aha_write;
};

#define HA_FLAG_PERM_READ            (1 << 0)
#define HA_FLAG_PERM_WRITE           (1 << 1) 
#define HA_FLAG_PERM_RW              (1 << 2)
#define HA_FLAG_ENC_REQ              (1 << 3)
#define HA_FLAG_AUTHENTICATION_REQ   (1 << 4)
#define HA_FLAG_AUTHORIZATION_REQ    (1 << 5)

struct ble_att_svr_entry;

/**
 * Handles a host attribute request.
 *
 * @param entry                 The host attribute being requested.
 * @param op                    The operation being performed on the attribute.
 * @param arg                   The request data associated with that host
 *                                  attribute.
 *
 * @return                      0 on success;
 *                              One of the BLE_ATT_ERR_[...] codes on
 *                                  failure.
 */
typedef int ble_att_svr_handle_func(struct ble_att_svr_entry *entry,
                                    uint8_t op,
                                    union ble_att_svr_handle_arg *arg);

struct ble_att_svr_entry {
    STAILQ_ENTRY(ble_att_svr_entry) ha_next;

    uint8_t ha_uuid[16];
    uint8_t ha_flags;
    uint8_t ha_pad1;
    uint16_t ha_handle_id;
    ble_att_svr_handle_func *ha_fn;
};

int ble_att_svr_register(uint8_t *uuid, uint8_t flags, uint16_t *handle_id,
                         ble_att_svr_handle_func *fn);

#endif
