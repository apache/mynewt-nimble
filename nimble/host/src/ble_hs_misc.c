#include <stdlib.h>
#include "os/os.h"
#include "ble_hs_priv.h"

int
ble_hs_misc_malloc_mempool(void **mem, struct os_mempool *pool,
                           int num_entries, int entry_size, char *name)
{
    int rc;

    *mem = malloc(OS_MEMPOOL_BYTES(num_entries, entry_size));
    if (*mem == NULL) {
        return BLE_HS_ENOMEM;
    }

    rc = os_mempool_init(pool, num_entries, entry_size, *mem, name);
    if (rc != 0) {
        free(*mem);
        return BLE_HS_EOS;
    }

    return 0;
}
