#ifndef H_BLE_HS_MBUF_PRIV_
#define H_BLE_HS_MBUF_PRIV_

struct os_mbuf;

struct os_mbuf *ble_hs_mbuf_bare_pkt(void);
struct os_mbuf *ble_hs_mbuf_acm_pkt(void);
struct os_mbuf *ble_hs_mbuf_l2cap_pkt(void);
int ble_hs_mbuf_pullup_base(struct os_mbuf **om, int base_len);

#endif
