#ifndef H_ITF_
#define H_ITF_

struct ble_att_chan;
struct os_eventq;

int ble_att_chan_open_default(struct ble_att_chan *ac);
int ble_att_chan_poll(struct ble_att_chan *c, struct os_eventq *evq);
int ble_host_sim_send_data_connectionless(uint16_t con_handle, uint16_t cid,
                                          uint8_t *data, uint16_t len);
int ble_sim_listen(uint16_t con_handle);
int ble_host_sim_poll(void);

#endif
