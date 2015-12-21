#include "ble_hs_priv.h"

static struct ble_hs_cfg ble_hs_cfg_dflt = {
    .max_connections = 16,
    .max_outstanding_pkts_per_conn = 5,
};

struct ble_hs_cfg ble_hs_cfg;

void
ble_hs_cfg_init(void)
{
    ble_hs_cfg = ble_hs_cfg_dflt;
}
