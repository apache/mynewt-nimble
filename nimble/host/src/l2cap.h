#ifndef H_L2CAP_
#define H_L2CAP_

struct ble_host_connection;
struct hci_data_hdr;

#define BLE_L2CAP_CID_SIG   1
#define BLE_L2CAP_CID_ATT   4

#define BLE_L2CAP_HDR_SZ    4

struct ble_l2cap_hdr
{
    uint16_t blh_len;
    uint16_t blh_cid;
};

int ble_l2cap_parse_hdr(void *pkt, uint16_t len,
                        struct ble_l2cap_hdr *l2cap_hdr);
int ble_l2cap_rx(struct ble_host_connection *connection,
                 struct hci_data_hdr *hci_hdr,
                 struct ble_l2cap_hdr *l2cap_hdr,
                 void *pkt);

#endif
