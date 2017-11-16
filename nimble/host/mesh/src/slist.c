
/*
 * Copyright (c) 2016 Intel Corporation
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <stddef.h>
#include <stdbool.h>
#include "mesh/slist.h"
#include "os/queue.h"
#include "os/os.h"
#include "os/os_mbuf.h"


/**
 * @brief Initialize a list
 *
 * @param list A pointer on the list to initialize
 */
void sys_slist_init(sys_slist_t *list)
{
	STAILQ_INIT(list);
}

/**
 * @brief Test if the given list is empty
 *
 * @param list A pointer on the list to test
 *
 * @return a boolean, true if it's empty, false otherwise
 */
bool sys_slist_is_empty(sys_slist_t *list)
{
	return STAILQ_EMPTY(list);
}

/**
 * @brief Peek the first node from the list
 *
 * @param list A point on the list to peek the first node from
 *
 * @return A pointer on the first node of the list (or NULL if none)
 */
sys_snode_t *sys_slist_peek_head(sys_slist_t *list)
{
	struct os_mbuf_pkthdr *mp;
	struct os_mbuf *m;

	mp = STAILQ_FIRST(list);

	if (mp) {
		m = OS_MBUF_PKTHDR_TO_MBUF(mp);
	} else {
		m = NULL;
	}

	return m;
}

/**
 * @brief Peek the last node from the list
 *
 * @param list A point on the list to peek the last node from
 *
 * @return A pointer on the last node of the list (or NULL if none)
 */
sys_snode_t *sys_slist_peek_tail(sys_slist_t *list)
{
	struct os_mbuf_pkthdr *mp;
	struct os_mbuf *m;

	mp = STAILQ_LAST(list, os_mbuf_pkthdr, omp_next);

	if (mp) {
		m = OS_MBUF_PKTHDR_TO_MBUF(mp);
	} else {
		m = NULL;
	}

	return m;
}

/**
 * @brief Peek the next node from current node, node is not NULL
 *
 * Faster then sys_slist_peek_next() if node is known not to be NULL.
 *
 * @param node A pointer on the node where to peek the next node
 *
 * @return a pointer on the next node (or NULL if none)
 */
sys_snode_t *sys_slist_peek_next_no_check(sys_snode_t *node)
{
	struct os_mbuf_pkthdr *mp;
	struct os_mbuf *m;

	mp = OS_MBUF_PKTHDR(node);
	if (!mp) {
		return NULL;
	}

	mp = STAILQ_NEXT(mp, omp_next);

	if (mp) {
		m = OS_MBUF_PKTHDR_TO_MBUF(mp);
	} else {
		m = NULL;
	}

	return m;
}

/**
 * @brief Peek the next node from current node
 *
 * @param node A pointer on the node where to peek the next node
 *
 * @return a pointer on the next node (or NULL if none)
 */
sys_snode_t *sys_slist_peek_next(sys_snode_t *node)
{
	return node ? sys_slist_peek_next_no_check(node) : NULL;
}

/**
 * @brief Prepend a node to the given list
 *
 * This and other sys_slist_*() functions are not thread safe.
 *
 * @param list A pointer on the list to affect
 * @param node A pointer on the node to prepend
 */
void sys_slist_prepend(sys_slist_t *list,
				     sys_snode_t *node)
{
	struct os_mbuf_pkthdr *mp;

	mp = OS_MBUF_PKTHDR(node);
	if (!mp) {
		return;
	}

	STAILQ_INSERT_HEAD(list, mp, omp_next);
}

/**
 * @brief Append a node to the given list
 *
 * This and other sys_slist_*() functions are not thread safe.
 *
 * @param list A pointer on the list to affect
 * @param node A pointer on the node to append
 */
void sys_slist_append(sys_slist_t *list,
				    sys_snode_t *node)
{
	struct os_mbuf_pkthdr *mp;

	mp = OS_MBUF_PKTHDR(node);
	if (!mp) {
		return;
	}

	STAILQ_INSERT_TAIL(list, mp, omp_next);
}

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
void sys_slist_append_list(sys_slist_t *list,
					 sys_slist_t *list_append)
{
	struct os_mbuf_pkthdr *mp;

	STAILQ_FOREACH(mp, list_append, omp_next) {
		STAILQ_INSERT_TAIL(list, mp, omp_next);
	}
}

/**
 * @brief merge two slists, appending the second one to the first
 *
 * When the operation is completed, the appending list is empty.
 * This and other sys_slist_*() functions are not thread safe.
 *
 * @param list A pointer on the list to affect
 * @param list_to_append A pointer to the list to append.
 */
void sys_slist_merge_slist(sys_slist_t *list,
					 sys_slist_t *list_to_append)
{
	sys_slist_append_list(list, list_to_append);
	sys_slist_init(list_to_append);
}

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
				    sys_snode_t *node)
{
	struct os_mbuf_pkthdr *mp, *mp_prev;

	if (!prev) {
		sys_slist_prepend(list, node);
	} else {
		mp_prev = OS_MBUF_PKTHDR(prev);
		mp = OS_MBUF_PKTHDR(node);
		STAILQ_INSERT_AFTER(list, mp_prev, mp, omp_next);
	}
}

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
sys_snode_t *sys_slist_get_not_empty(sys_slist_t *list)
{
	struct os_mbuf_pkthdr *mp;
	struct os_mbuf *m;

	mp = STAILQ_FIRST(list);
	m = OS_MBUF_PKTHDR_TO_MBUF(mp);

	STAILQ_REMOVE_HEAD(list, omp_next);

	return m;
}

/**
 * @brief Fetch and remove the first node of the given list
 *
 * This and other sys_slist_*() functions are not thread safe.
 *
 * @param list A pointer on the list to affect
 *
 * @return A pointer to the first node of the list (or NULL if empty)
 */
sys_snode_t *sys_slist_get(sys_slist_t *list)
{
	return sys_slist_is_empty(list) ? NULL : sys_slist_get_not_empty(list);
}

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
				    sys_snode_t *node)
{
	struct os_mbuf_pkthdr *mp;

	mp = OS_MBUF_PKTHDR(node);
	STAILQ_REMOVE(list, mp, os_mbuf_pkthdr, omp_next);
}

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
					     sys_snode_t *node)
{
	sys_snode_t *prev = NULL;
	sys_snode_t *test;

	SYS_SLIST_FOR_EACH_NODE(list, test) {
		if (test == node) {
			sys_slist_remove(list, prev, node);
			return true;
		}

		prev = test;
	}

	return false;

}

void net_buf_slist_put(sys_slist_t *list, struct os_mbuf *buf)
{
	sys_slist_append(list, buf);
}

struct os_mbuf *net_buf_slist_get(sys_slist_t *list)
{
	return sys_slist_get(list);
}
