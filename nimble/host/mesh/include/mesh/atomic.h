/* atomic operations */

/*
 * Copyright (c) 1997-2015, Wind River Systems, Inc.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#ifndef __ATOMIC_H__
#define __ATOMIC_H__

#include "nimble/nimble_npl.h"

#ifdef __cplusplus
extern "C"
{
#endif

typedef ble_npl_atomic_t bt_mesh_atomic_t;
typedef ble_npl_atomic_val_t bt_mesh_atomic_val_t;

/**
 * @defgroup atomic_apis Atomic Services APIs
 * @{
 */

/**
 *
 * @brief Atomic addition.
 *
 * This routine performs an atomic addition on @a target.
 *
 * @param target Address of atomic variable.
 * @param value Value to add.
 *
 * @return Previous value of @a target.
 */
static inline bt_mesh_atomic_val_t
bt_mesh_atomic_add(bt_mesh_atomic_t *target, bt_mesh_atomic_val_t value)
{
    return ble_npl_atomic_add(target, value);
}

/**
 *
 * @brief Atomic subtraction.
 *
 * This routine performs an atomic subtraction on @a target.
 *
 * @param target Address of atomic variable.
 * @param value Value to subtract.
 *
 * @return Previous value of @a target.
 */

static inline bt_mesh_atomic_val_t
bt_mesh_atomic_sub(bt_mesh_atomic_t *target, bt_mesh_atomic_val_t value)
{
    return ble_npl_atomic_sub(target, value);
}

/**
 *
 * @brief Atomic increment.
 *
 * This routine performs an atomic increment by 1 on @a target.
 *
 * @param target Address of atomic variable.
 *
 * @return Previous value of @a target.
 */

static inline bt_mesh_atomic_val_t
bt_mesh_atomic_inc(bt_mesh_atomic_t *target)
{
    return bt_mesh_atomic_add(target, 1);
}

/**
 *
 * @brief Atomic decrement.
 *
 * This routine performs an atomic decrement by 1 on @a target.
 *
 * @param target Address of atomic variable.
 *
 * @return Previous value of @a target.
 */

static inline bt_mesh_atomic_val_t
bt_mesh_atomic_dec(bt_mesh_atomic_t *target)
{
    return bt_mesh_atomic_sub(target, 1);
}

/**
 *
 * @brief Atomic get.
 *
 * This routine performs an atomic read on @a target.
 *
 * @param target Address of atomic variable.
 *
 * @return Value of @a target.
 */

static inline bt_mesh_atomic_val_t
bt_mesh_atomic_get(const bt_mesh_atomic_t *target)
{
    return ble_npl_atomic_get(target);
}

/**
 *
 * @brief Atomic get-and-set.
 *
 * This routine atomically sets @a target to @a value and returns
 * the previous value of @a target.
 *
 * @param target Address of atomic variable.
 * @param value Value to write to @a target.
 *
 * @return Previous value of @a target.
 */

static inline bt_mesh_atomic_val_t
bt_mesh_atomic_set(bt_mesh_atomic_t *target, bt_mesh_atomic_val_t value)
{
    return ble_npl_atomic_set(target, value);
}

/**
 *
 * @brief Atomic clear.
 *
 * This routine atomically sets @a target to zero and returns its previous
 * value. (Hence, it is equivalent to bt_mesh_atomic_set(target, 0).)
 *
 * @param target Address of atomic variable.
 *
 * @return Previous value of @a target.
 */

static inline bt_mesh_atomic_val_t
bt_mesh_atomic_clear(bt_mesh_atomic_t *target)
{
    return bt_mesh_atomic_set(target, 0);
}

/**
 *
 * @brief Atomic bitwise inclusive OR.
 *
 * This routine atomically sets @a target to the bitwise inclusive OR of
 * @a target and @a value.
 *
 * @param target Address of atomic variable.
 * @param value Value to OR.
 *
 * @return Previous value of @a target.
 */

static inline bt_mesh_atomic_val_t
bt_mesh_atomic_or(bt_mesh_atomic_t *target, bt_mesh_atomic_val_t value)
{
    return ble_npl_atomic_or(target, value);
}

/**
 *
 * @brief Atomic bitwise AND.
 *
 * This routine atomically sets @a target to the bitwise AND of @a target
 * and @a value.
 *
 * @param target Address of atomic variable.
 * @param value Value to AND.
 *
 * @return Previous value of @a target.
 */

static inline bt_mesh_atomic_val_t
bt_mesh_atomic_and(bt_mesh_atomic_t *target, bt_mesh_atomic_val_t value)
{
    return ble_npl_atomic_and(target, value);
}

/**
 * @brief Initialize an atomic variable.
 *
 * This macro can be used to initialize an atomic variable. For example,
 * @code bt_mesh_atomic_t my_var = BT_MESH_ATOMIC_INIT(75); @endcode
 *
 * @param i Value to assign to atomic variable.
 */
#define BT_MESH_ATOMIC_INIT(i)   (i)

/**
 * @cond INTERNAL_HIDDEN
 */

#define BT_MESH_ATOMIC_BITS      (sizeof(bt_mesh_atomic_val_t) * 8)
#define BT_MESH_ATOMIC_MASK(bit) (1 << ((bit) & (BT_MESH_ATOMIC_BITS - 1)))
#define BT_MESH_ATOMIC_ELEM(addr, bit) ((addr) + ((bit) / BT_MESH_ATOMIC_BITS))

/**
 * INTERNAL_HIDDEN @endcond
 */

/**
 * @brief Define an array of atomic variables.
 *
 * This macro defines an array of atomic variables containing at least
 * @a num_bits bits.
 *
 * @note
 * If used from file scope, the bits of the array are initialized to zero;
 * if used from within a function, the bits are left uninitialized.
 *
 * @param name Name of array of atomic variables.
 * @param num_bits Number of bits needed.
 */
#define BT_MESH_ATOMIC_DEFINE(name, num_bits)                                 \
    bt_mesh_atomic_t name[1 + ((num_bits) - 1) / BT_MESH_ATOMIC_BITS]

/**
 * @brief Atomically test a bit.
 *
 * This routine tests whether bit number @a bit of @a target is set or not.
 * The target may be a single atomic variable or an array of them.
 *
 * @param target Address of atomic variable or array.
 * @param bit Bit number (starting from 0).
 *
 * @return 1 if the bit was set, 0 if it wasn't.
 */
static inline int
bt_mesh_atomic_test_bit(const bt_mesh_atomic_t *target, int bit)
{
    bt_mesh_atomic_val_t val = bt_mesh_atomic_get(BT_MESH_ATOMIC_ELEM(target, bit));

    return (1 & (val >> (bit & (BT_MESH_ATOMIC_BITS - 1))));
}

    /**
     * @brief Atomically test and clear a bit.
     *
     * Atomically clear bit number @a bit of @a target and return its old value.
     * The target may be a single atomic variable or an array of them.
     *
     * @param target Address of atomic variable or array.
     * @param bit Bit number (starting from 0).
     *
     * @return 1 if the bit was set, 0 if it wasn't.
     */
static inline int
bt_mesh_atomic_test_and_clear_bit(bt_mesh_atomic_t *target, int bit)
{
    bt_mesh_atomic_val_t mask = BT_MESH_ATOMIC_MASK(bit);
    bt_mesh_atomic_val_t old;

    old = bt_mesh_atomic_and(BT_MESH_ATOMIC_ELEM(target, bit), ~mask);

    return (old & mask) != 0;
}

    /**
     * @brief Atomically set a bit.
     *
     * Atomically set bit number @a bit of @a target and return its old value.
     * The target may be a single atomic variable or an array of them.
     *
     * @param target Address of atomic variable or array.
     * @param bit Bit number (starting from 0).
     *
     * @return 1 if the bit was set, 0 if it wasn't.
     */
static inline int
bt_mesh_atomic_test_and_set_bit(bt_mesh_atomic_t *target, int bit)
{
    bt_mesh_atomic_val_t mask = BT_MESH_ATOMIC_MASK(bit);
    bt_mesh_atomic_val_t old;

    old = bt_mesh_atomic_or(BT_MESH_ATOMIC_ELEM(target, bit), mask);

    return (old & mask) != 0;
}

    /**
     * @brief Atomically clear a bit.
     *
     * Atomically clear bit number @a bit of @a target.
     * The target may be a single atomic variable or an array of them.
     *
     * @param target Address of atomic variable or array.
     * @param bit Bit number (starting from 0).
     *
     * @return N/A
     */
static inline void
bt_mesh_atomic_clear_bit(bt_mesh_atomic_t *target, int bit)
{
    bt_mesh_atomic_val_t mask = BT_MESH_ATOMIC_MASK(bit);

    bt_mesh_atomic_and(BT_MESH_ATOMIC_ELEM(target, bit), ~mask);
}

    /**
     * @brief Atomically set a bit.
     *
     * Atomically set bit number @a bit of @a target.
     * The target may be a single atomic variable or an array of them.
     *
     * @param target Address of atomic variable or array.
     * @param bit Bit number (starting from 0).
     *
     * @return N/A
     */
static inline void
bt_mesh_atomic_set_bit(bt_mesh_atomic_t *target, int bit)
{
    bt_mesh_atomic_val_t mask = BT_MESH_ATOMIC_MASK(bit);

    bt_mesh_atomic_or(BT_MESH_ATOMIC_ELEM(target, bit), mask);
}

/**
* @brief Atomically set a bit to a given value.
*
* Atomically set bit number @a bit of @a target to value @a val.
* The target may be a single atomic variable or an array of them.
*
* @param target Address of atomic variable or array.
* @param bit Bit number (starting from 0).
* @param val true for 1, false for 0.
*
* @return N/A
*/
static inline void
bt_mesh_atomic_set_bit_to(bt_mesh_atomic_t *target, int bit, bool val)
{
    bt_mesh_atomic_val_t mask = BT_MESH_ATOMIC_MASK(bit);

    if (val) {
        (void)bt_mesh_atomic_or(BT_MESH_ATOMIC_ELEM(target, bit), mask);
    } else {
        (void)bt_mesh_atomic_and(BT_MESH_ATOMIC_ELEM(target, bit), ~mask);
    }
}

/**
 * @}
 */

#ifdef __cplusplus
}
#endif

#endif /* __ATOMIC_H__ */
