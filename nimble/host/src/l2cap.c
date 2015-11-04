#include <errno.h>
#include "nimble/ble.h"
#include "nimble/hci_common.h"
#include "l2cap.h"

int
ble_l2cap_parse_hdr(void *pkt, uint16_t len, struct ble_l2cap_hdr *l2cap_hdr)
{
    uint8_t *u8ptr;
    uint16_t off;

    if (len < BLE_L2CAP_HDR_SZ) {
        return EMSGSIZE;
    }

    off = 0;
    u8ptr = pkt;

    l2cap_hdr->blh_len = le16toh(u8ptr + off);
    off += 2;

    l2cap_hdr->blh_cid = le16toh(u8ptr + off);
    off += 2;

    if (len < BLE_L2CAP_HDR_SZ + l2cap_hdr->blh_len) {
        return EMSGSIZE;
    }

    return 0;
}

int
ble_l2cap_rx(struct ble_host_connection *connection,
             struct hci_data_hdr *hci_hdr,
             struct ble_l2cap_hdr *l2cap_hdr,
             void *pkt)
{
    /* XXX: Look up L2CAP channel by connection-cid pair. */
    /* XXX: Append incoming data to channel buffer. */
    /* XXX: Call channel-specific rx callback. */

    return 0;
}
