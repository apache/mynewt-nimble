/*
 * Copyright (c) 2016 Intel Corporation
 *
 * SPDX-License-Identifier: Apache-2.0
 */

/**
 * @file
 *
 * @brief Single-linked list implementation
 *
 * Single-linked list implementation using inline macros/functions.
 * This API is not thread safe, and thus if a list is used across threads,
 * calls to functions must be protected with synchronization primitives.
 */

#ifndef __SLIST_H__
#define __SLIST_H__

#include <stddef.h>
#include <stdbool.h>
#include "os/queue.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct os_mbuf sys_snode_t;

STAILQ_HEAD(_slist, os_mbuf_pkthdr);

typedef struct _slist sys_slist_t;

/**
 * @brief Provide the primitive to iterate on a list
 * Note: the loop is unsafe and thus __sn should not be removed
 *
 * User _MUST_ add the loop statement curly braces enclosing its own code:
 *
 *     SYS_SLIST_FOR_EACH_NODE(l, n) {
 *         <user code>
 *     }
 *
 * This and other SYS_SLIST_*() macros are not thread safe.
 *
 * @param __sl A pointer on a sys_slist_t to iterate on
 * @param __sn A sys_snode_t pointer to peek each node of the list
 */
#define SYS_SLIST_FOR_EACH_NODE(__sl, __sn)				\
	for (__sn = sys_slist_peek_head(__sl); __sn;			\
	     __sn = sys_slist_peek_next(__sn))

/**
 * @brief Provide the primitive to iterate on a list under a container
 * Note: the loop is unsafe and thus __cn should not be detached
 *
 * User _MUST_ add the loop statement curly braces enclosing its own code:
 *
 *     SYS_SLIST_FOR_EACH_CONTAINER(l, c, n) {
 *         <user code>
 *     }
 *
 * @param __sl A pointer on a sys_slist_t to iterate on
 * @param __cn A pointer to peek each entry of the list
 * @param __n The field name of sys_node_t within the container struct
 */
#define SYS_SLIST_FOR_EACH_CONTAINER(__sl, __cn, __n)			\
	STAILQ_FOREACH(__cn, __sl, omp_next)

/**
 * @brief Initialize a list
 *
 * @param list A pointer on the list to initialize
 */
void sys_slist_init(sys_slist_t *list);

#define SYS_SLIST_STATIC_INIT(ptr_to_list) STAILQ_HEAD_INITIALIZER(ptr_to_list)

/**
 * @brief Test if the given list is empty
 *
 * @param list A pointer on the list to test
 *
 * @return a boolean, true if it's empty, false otherwise
 */
bool sys_slist_is_empty(sys_slist_t *list);

/**
 * @brief Peek the first node from the list
 *
 * @param list A point on the list to peek the first node from
 *
 * @return A pointer on the first node of the list (or NULL if none)
 */
sys_snode_t *sys_slist_peek_head(sys_slist_t *list);

/**
 * @brief Peek the last node from the list
 *
 * @param list A point on the list to peek the last node from
 *
 * @return A pointer on the last node of the list (or NULL if none)
 */
sys_snode_t *sys_slist_peek_tail(sys_slist_t *list);

/**
 * @brief Peek the next node from current node, node is not NULL
 *
 * Faster then sys_slist_peek_next() if node is known not to be NULL.
 *
 * @param node A pointer on the node where to peek the next node
 *
 * @return a pointer on the next node (or NULL if none)
 */
sys_snode_t *sys_slist_peek_next_no_check(sys_snode_t *node);

/**
 * @brief Peek the next node from current node
 *
 * @param node A pointer on the node where to peek the next node
 *
 * @return a pointer on the next node (or NULL if none)
 */
sys_snode_t *sys_slist_peek_next(sys_snode_t *node);

/**
 * @brief Prepend a node to the given list
 *
 * This and other sys_slist_*() functions are not thread safe.
 *
 * @param list A pointer on the list to affect
 * @param node A pointer on the node to prepend
 */
void sys_slist_prepend(sys_slist_t *list, sys_snode_t *node);

/**
 * @brief Append a node to the given list
 *
 * This and other sys_slist_*() functions are not thread safe.
 *
 * @param list A pointer on the list to affect
 * @param node A pointer on the node to append
 */
void sys_slist_append(sys_slist_t *list, sys_snode_t *node);

/**
 * @brief Append a list to the given list
 *
 * Append a singly-linked, NULL-terminated list consisting of nodes containing
 * the pointer to the next node as the first element of a node, to @a list.
 * This and other sys_slist_*() functions are not thread safe.
 *
 * @param list A pointer on the list to affect
 * @param head A pointer to the first element of the list to append
 * @param tail A pointer to the last element of the list to append
 */
void sys_slist_append_list(sys_slist_t *list, sys_slist_t *list_append);

/**
 * @brief merge two slists, appending the second one to the first
 *
 * When the operation is completed, the appending list is empty.
 * This and other sys_slist_*() functions are not thread safe.
 *
 * @param list A pointer on the list to affect
 * @param list_to_append A pointer to the list to append.
 */
void sys_slist_merge_slist(sys_slist_t *list, sys_slist_t *list_to_append);

/**
 * @brief Insert a node to the given list
 *
 * This and other sys_slist_*() functions are not thread safe.
 *
 * @param list A pointer on the list to affect
 * @param prev A pointer on the previous node
 * @param node A pointer on the node to insert
 */
void sys_slist_insert(sys_slist_t *list,
				    sys_snode_t *prev,
				    sys_snode_t *node);

/**
 * @brief Fetch and remove the first node of the given list
 *
 * List must be known to be non-empty.
 * This and other sys_slist_*() functions are not thread safe.
 *
 * @param list A pointer on the list to affect
 *
 * @return A pointer to the first node of the list
 */
sys_snode_t *sys_slist_get_not_empty(sys_slist_t *list);
/**
 * @brief Fetch and remove the first node of the given list
 *
 * This and other sys_slist_*() functions are not thread safe.
 *
 * @param list A pointer on the list to affect
 *
 * @return A pointer to the first node of the list (or NULL if empty)
 */
sys_snode_t *sys_slist_get(sys_slist_t *list);

/**
 * @brief Remove a node
 *
 * This and other sys_slist_*() functions are not thread safe.
 *
 * @param list A pointer on the list to affect
 * @param prev_node A pointer on the previous node
 *        (can be NULL, which means the node is the list's head)
 * @param node A pointer on the node to remove
 */
void sys_slist_remove(sys_slist_t *list,
				    sys_snode_t *prev_node,
				    sys_snode_t *node);

/**
 * @brief Find and remove a node from a list
 *
 * This and other sys_slist_*() functions are not thread safe.
 *
 * @param list A pointer on the list to affect
 * @param node A pointer on the node to remove from the list
 *
 * @return true if node was removed
 */
bool sys_slist_find_and_remove(sys_slist_t *list,
					     sys_snode_t *node);

void net_buf_slist_put(sys_slist_t *list, struct os_mbuf *buf);

struct os_mbuf *net_buf_slist_get(sys_slist_t *list);

#ifdef __cplusplus
}
#endif

#endif /* __SLIST_H__ */
