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

#include <string.h>
#include <errno.h>
#include "testutil/testutil.h"
#include "nimble/ble.h"
#include "host/ble_uuid.h"
#include "host/ble_hs_test.h"
#include "ble_hs_test_util.h"

#define BLE_GATTS_REG_TEST_MAX_ENTRIES  256

struct ble_gatts_reg_test_entry {
    uint8_t op;
    uint8_t uuid128[16];
};

static struct ble_gatts_reg_test_entry
ble_gatts_reg_test_entries[BLE_GATTS_REG_TEST_MAX_ENTRIES];

static int ble_gatts_reg_test_num_entries;

static void
ble_gatts_reg_test_init(void)
{
    ble_hs_test_util_init();
    ble_gatts_reg_test_num_entries = 0;
}

static void
ble_gatts_reg_test_misc_reg_cb(uint8_t op, union ble_gatt_register_ctxt *ctxt,
                               void *arg)
{
    struct ble_gatts_reg_test_entry *entry;

    TEST_ASSERT_FATAL(ble_gatts_reg_test_num_entries <
                      BLE_GATTS_REG_TEST_MAX_ENTRIES);

    entry = ble_gatts_reg_test_entries + ble_gatts_reg_test_num_entries++;

    entry->op = op;
    switch (op) {
    case BLE_GATT_REGISTER_OP_SVC:
        memcpy(entry->uuid128, ctxt->svc.svc_def->uuid128, 16);
        break;

    case BLE_GATT_REGISTER_OP_CHR:
        memcpy(entry->uuid128, ctxt->chr.chr_def->uuid128, 16);
        break;

    case BLE_GATT_REGISTER_OP_DSC:
        memcpy(entry->uuid128, ctxt->dsc.dsc_def->uuid128, 16);
        break;

    default:
        TEST_ASSERT(0);
        break;
    }
}

static void
ble_gatts_reg_test_misc_verify_entry(uint8_t op, const uint8_t *uuid128)
{
    struct ble_gatts_reg_test_entry *entry;
    int i;

    for (i = 0; i < ble_gatts_reg_test_num_entries; i++) {
        entry = ble_gatts_reg_test_entries + i;
        if (entry->op == op && memcmp(entry->uuid128, uuid128, 16) == 0) {
            return;
        }
    }

    TEST_ASSERT(0);
}

static int
ble_gatts_reg_test_misc_dummy_access(uint16_t conn_handle,
                                     uint16_t attr_handle, uint8_t op,
                                     union ble_gatt_access_ctxt *ctxt,
                                     void *arg)
{
    return 0;
}

TEST_CASE(ble_gatts_reg_test_svc_return)
{
    int rc;

    ble_gatts_reg_test_init();

    /*** Missing UUID. */
    struct ble_gatt_svc_def svcs_no_uuid[] = { {
        .type = BLE_GATT_SVC_TYPE_PRIMARY,
    }, {
        0
    } };

    rc = ble_gatts_register_svcs(svcs_no_uuid, NULL, NULL);
    TEST_ASSERT(rc == BLE_HS_EINVAL);

    /*** Circular dependency. */
    struct ble_gatt_svc_def svcs_circ[] = { {
        .type = BLE_GATT_SVC_TYPE_PRIMARY,
        .uuid128 = BLE_UUID16(0x1234),
        .includes = (const struct ble_gatt_svc_def*[]) { svcs_circ + 1, NULL },
    }, {
        .type = BLE_GATT_SVC_TYPE_SECONDARY,
        .uuid128 = BLE_UUID16(0x1234),
        .includes = (const struct ble_gatt_svc_def*[]) { svcs_circ + 0, NULL },
    }, {
        0
    } };

    rc = ble_gatts_register_svcs(svcs_circ, NULL, NULL);
    TEST_ASSERT(rc == BLE_HS_EINVAL);

    /*** Success. */
    struct ble_gatt_svc_def svcs_good[] = { {
        .type = BLE_GATT_SVC_TYPE_PRIMARY,
        .uuid128 = BLE_UUID16(0x1234),
        .includes = (const struct ble_gatt_svc_def*[]) { svcs_good + 1, NULL },
    }, {
        .type = BLE_GATT_SVC_TYPE_SECONDARY,
        .uuid128 = BLE_UUID16(0x1234),
    }, {
        0
    } };

    rc = ble_gatts_register_svcs(svcs_good, NULL, NULL);
    TEST_ASSERT(rc == 0);
}

TEST_CASE(ble_gatts_reg_test_chr_return)
{
    int rc;

    ble_gatts_reg_test_init();

    /*** Missing callback. */
    struct ble_gatt_svc_def svcs_no_chr_cb[] = { {
        .type = BLE_GATT_SVC_TYPE_PRIMARY,
        .uuid128 = BLE_UUID16(0x1234),
        .characteristics = (struct ble_gatt_chr_def[]) { {
            .uuid128 = BLE_UUID16(0x1111),
            .flags = BLE_GATT_CHR_F_READ,
        }, {
            0
        } },
    }, {
        0
    } };

    rc = ble_gatts_register_svcs(svcs_no_chr_cb, NULL, NULL);
    TEST_ASSERT(rc == BLE_HS_EINVAL);

    /*** Success. */
    struct ble_gatt_svc_def svcs_good[] = { {
        .type = BLE_GATT_SVC_TYPE_PRIMARY,
        .uuid128 = BLE_UUID16(0x1234),
        .characteristics = (struct ble_gatt_chr_def[]) { {
            .uuid128 = BLE_UUID16(0x1111),
            .access_cb = ble_gatts_reg_test_misc_dummy_access,
            .flags = BLE_GATT_CHR_F_READ,
        }, {
            0
        } },
    }, {
        0
    } };

    rc = ble_gatts_register_svcs(svcs_good, NULL, NULL);
    TEST_ASSERT(rc == 0);
}

TEST_CASE(ble_gatts_reg_test_dsc_return)
{
    int rc;

    ble_gatts_reg_test_init();

    /*** Missing callback. */
    struct ble_gatt_svc_def svcs_no_dsc_cb[] = { {
        .type = BLE_GATT_SVC_TYPE_PRIMARY,
        .uuid128 = BLE_UUID16(0x1234),
        .characteristics = (struct ble_gatt_chr_def[]) { {
            .uuid128 = BLE_UUID16(0x1111),
            .access_cb = ble_gatts_reg_test_misc_dummy_access,
            .flags = BLE_GATT_CHR_F_READ,
            .descriptors = (struct ble_gatt_dsc_def[]) { {
                .uuid128 = BLE_UUID16(0x8888),
                .att_flags = 5,
            }, {
                0
            } },
        }, {
            0
        } },
    }, {
        0
    } };

    rc = ble_gatts_register_svcs(svcs_no_dsc_cb, NULL, NULL);
    TEST_ASSERT(rc == BLE_HS_EINVAL);

    /*** Success. */
    struct ble_gatt_svc_def svcs_good[] = { {
        .type = BLE_GATT_SVC_TYPE_PRIMARY,
        .uuid128 = BLE_UUID16(0x1234),
        .characteristics = (struct ble_gatt_chr_def[]) { {
            .uuid128 = BLE_UUID16(0x1111),
            .access_cb = ble_gatts_reg_test_misc_dummy_access,
            .flags = BLE_GATT_CHR_F_READ,
            .descriptors = (struct ble_gatt_dsc_def[]) { {
                .uuid128 = BLE_UUID16(0x8888),
                .access_cb = ble_gatts_reg_test_misc_dummy_access,
                .att_flags = 5,
            }, {
                0
            } },
        }, {
            0
        } },
    }, {
        0
    } };

    rc = ble_gatts_register_svcs(svcs_good, NULL, NULL);
    TEST_ASSERT(rc == 0);
}

static void
ble_gatts_reg_test_misc_svcs(struct ble_gatt_svc_def *svcs)
{
    const struct ble_gatt_svc_def *svc;
    const struct ble_gatt_chr_def *chr;
    const struct ble_gatt_dsc_def *dsc;
    int rc;

    ble_gatts_reg_test_init();

    /* Register all the attributes. */
    rc = ble_gatts_register_svcs(svcs, ble_gatts_reg_test_misc_reg_cb,
                                 NULL);
    TEST_ASSERT_FATAL(rc == 0);

    /* Verify that the appropriate callbacks were executed. */
    for (svc = svcs; svc->type != BLE_GATT_SVC_TYPE_END; svc++) {
        ble_gatts_reg_test_misc_verify_entry(BLE_GATT_REGISTER_OP_SVC,
                                             svc->uuid128);

        if (svc->characteristics != NULL) {
            for (chr = svc->characteristics; chr->uuid128 != NULL; chr++) {
                ble_gatts_reg_test_misc_verify_entry(BLE_GATT_REGISTER_OP_CHR,
                                                     chr->uuid128);

                if (chr->descriptors != NULL) {
                    for (dsc = chr->descriptors; dsc->uuid128 != NULL; dsc++) {
                        ble_gatts_reg_test_misc_verify_entry(
                            BLE_GATT_REGISTER_OP_DSC, dsc->uuid128);
                    }
                }
            }
        }
    }
}

TEST_CASE(ble_gatts_reg_test_svc_cb)
{
    /*** 1 primary. */
    ble_gatts_reg_test_misc_svcs((struct ble_gatt_svc_def[]) { {
        .type = BLE_GATT_SVC_TYPE_PRIMARY,
        .uuid128 = BLE_UUID16(0x1234),
    }, {
        0
    } });

    /*** 3 primary. */
    ble_gatts_reg_test_misc_svcs((struct ble_gatt_svc_def[]) { {
        .type = BLE_GATT_SVC_TYPE_PRIMARY,
        .uuid128 = BLE_UUID16(0x1234),
    }, {
        .type = BLE_GATT_SVC_TYPE_PRIMARY,
        .uuid128 = BLE_UUID16(0x2234),
    }, {
        .type = BLE_GATT_SVC_TYPE_PRIMARY,
        .uuid128 = BLE_UUID16(0x3234),
    }, {
        0
    } });


    /*** 1 primary, 1 secondary. */
    ble_gatts_reg_test_misc_svcs((struct ble_gatt_svc_def[]) { {
        .type = BLE_GATT_SVC_TYPE_PRIMARY,
        .uuid128 = BLE_UUID16(0x1234),
    }, {
        .type = BLE_GATT_SVC_TYPE_SECONDARY,
        .uuid128 = BLE_UUID16(0x2222),
    }, {
        0
    } });

    /*** 1 primary, 1 secondary, 1 include. */
    struct ble_gatt_svc_def svcs[] = {
        [0] = {
            .type = BLE_GATT_SVC_TYPE_PRIMARY,
            .uuid128 = BLE_UUID16(0x1234),
            .includes = (const struct ble_gatt_svc_def*[]) { svcs + 1, NULL, },
        },
        [1] = {
            .type = BLE_GATT_SVC_TYPE_SECONDARY,
            .uuid128 = BLE_UUID16(0x2222),
        }, {
            0
        }
    };
    ble_gatts_reg_test_misc_svcs(svcs);
}

TEST_CASE(ble_gatts_reg_test_chr_cb)
{
    /*** 1 characteristic. */
    ble_gatts_reg_test_misc_svcs((struct ble_gatt_svc_def[]) { {
        .type = BLE_GATT_SVC_TYPE_PRIMARY,
        .uuid128 = BLE_UUID16(0x1234),
        .characteristics = (struct ble_gatt_chr_def[]) { {
            .uuid128 = BLE_UUID16(0x1111),
            .access_cb = ble_gatts_reg_test_misc_dummy_access,
            .flags = BLE_GATT_CHR_F_READ,
        }, {
            0
        } },
    }, {
        0
    } });

    /*** 3 characteristics. */
    ble_gatts_reg_test_misc_svcs((struct ble_gatt_svc_def[]) { {
        .type = BLE_GATT_SVC_TYPE_PRIMARY,
        .uuid128 = BLE_UUID16(0x1234),
        .characteristics = (struct ble_gatt_chr_def[]) { {
            .uuid128 = BLE_UUID16(0x1111),
            .access_cb = ble_gatts_reg_test_misc_dummy_access,
            .flags = BLE_GATT_CHR_F_READ,
        }, {
            .uuid128 = BLE_UUID16(0x2222),
            .access_cb = ble_gatts_reg_test_misc_dummy_access,
            .flags = BLE_GATT_CHR_F_WRITE,
        }, {
            0
        } },
    }, {
        .type = BLE_GATT_SVC_TYPE_SECONDARY,
        .uuid128 = BLE_UUID16(0x5678),
        .characteristics = (struct ble_gatt_chr_def[]) { {
            .uuid128 = BLE_UUID16(0x3333),
            .access_cb = ble_gatts_reg_test_misc_dummy_access,
            .flags = BLE_GATT_CHR_F_READ,
        }, {
            0
        } },
    }, {
        0
    } });
}

TEST_CASE(ble_gatts_reg_test_dsc_cb)
{
    /*** 1 descriptor. */
    ble_gatts_reg_test_misc_svcs((struct ble_gatt_svc_def[]) { {
        .type = BLE_GATT_SVC_TYPE_PRIMARY,
        .uuid128 = BLE_UUID16(0x1234),
        .characteristics = (struct ble_gatt_chr_def[]) { {
            .uuid128 = BLE_UUID16(0x1111),
            .access_cb = ble_gatts_reg_test_misc_dummy_access,
            .flags = BLE_GATT_CHR_F_READ,
            .descriptors = (struct ble_gatt_dsc_def[]) { {
                .uuid128 = BLE_UUID16(0xaaaa),
                .att_flags = 5,
                .access_cb = ble_gatts_reg_test_misc_dummy_access,
            }, {
                0
            } },
        }, {
            0
        } },
    }, {
        0
    } });

    /*** 5 descriptors. */
    ble_gatts_reg_test_misc_svcs((struct ble_gatt_svc_def[]) { {
        .type = BLE_GATT_SVC_TYPE_PRIMARY,
        .uuid128 = BLE_UUID16(0x1234),
        .characteristics = (struct ble_gatt_chr_def[]) { {
            .uuid128 = BLE_UUID16(0x1111),
            .access_cb = ble_gatts_reg_test_misc_dummy_access,
            .flags = BLE_GATT_CHR_F_READ,
            .descriptors = (struct ble_gatt_dsc_def[]) { {
                .uuid128 = BLE_UUID16(0xaaaa),
                .att_flags = 5,
                .access_cb = ble_gatts_reg_test_misc_dummy_access,
            }, {
                0
            } },
        }, {
            .uuid128 = BLE_UUID16(0x2222),
            .access_cb = ble_gatts_reg_test_misc_dummy_access,
            .flags = BLE_GATT_CHR_F_WRITE,
        }, {
            0
        } },
    }, {
        .type = BLE_GATT_SVC_TYPE_SECONDARY,
        .uuid128 = BLE_UUID16(0x5678),
        .characteristics = (struct ble_gatt_chr_def[]) { {
            .uuid128 = BLE_UUID16(0x3333),
            .access_cb = ble_gatts_reg_test_misc_dummy_access,
            .flags = BLE_GATT_CHR_F_READ,
            .descriptors = (struct ble_gatt_dsc_def[]) { {
                .uuid128 = BLE_UUID16(0xaaab),
                .att_flags = 5,
                .access_cb = ble_gatts_reg_test_misc_dummy_access,
            }, {
                .uuid128 = BLE_UUID16(0xaaac),
                .att_flags = 5,
                .access_cb = ble_gatts_reg_test_misc_dummy_access,
            }, {
                .uuid128 = BLE_UUID16(0xaaad),
                .att_flags = 5,
                .access_cb = ble_gatts_reg_test_misc_dummy_access,
            }, {
                .uuid128 = BLE_UUID16(0xaaae),
                .att_flags = 5,
                .access_cb = ble_gatts_reg_test_misc_dummy_access,
            }, {
                0
            } },
        }, {
            0
        } },
    }, {
        0
    } });
}

TEST_SUITE(ble_gatts_reg_suite)
{
    ble_gatts_reg_test_svc_return();
    ble_gatts_reg_test_chr_return();
    ble_gatts_reg_test_dsc_return();

    ble_gatts_reg_test_svc_cb();
    ble_gatts_reg_test_chr_cb();
    ble_gatts_reg_test_dsc_cb();
}

int
ble_gatts_reg_test_all(void)
{
    ble_gatts_reg_suite();

    return tu_any_failed;
}
