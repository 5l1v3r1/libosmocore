/*! \file signal.c
 * Generic signalling/notification infrastructure. */
/*
 * (C) 2009 by Holger Hans Peter Freyther <zecke@selfish.org>
 * All Rights Reserved
 *
 * SPDX-License-Identifier: GPL-2.0+
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License along
 * with this program; if not, write to the Free Software Foundation, Inc.,
 * 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA.
 *
 */

#include <osmocom/core/signal.h>
#include <osmocom/core/talloc.h>
#include <osmocom/core/linuxlist.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>

/*! \addtogroup signal
 *  @{
 *  Generic signalling/notification infrastructure.
 *
 * \file signal.c */


/* legacy export for old openbsc.git versions that insist on setting this directly */
void *tall_sigh_ctx;
/* actual talloc context used these days */
static void *tall_sigh_ctx2;

static LLIST_HEAD(signal_handler_list);

struct signal_handler {
	struct llist_head entry;
	unsigned int subsys;
	osmo_signal_cbfn *cbfn;
	void *data;
};

/* DEPRECATED: Never really worked and we now have OTC_GLOBAL */
void *osmo_signal_talloc_ctx_init(void *root_ctx) {
	return tall_sigh_ctx2;
}

/*! Register a new signal handler
 *  \param[in] subsys Subsystem number
 *  \param[in] cbfn Callback function
 *  \param[in] data Data passed through to callback
 *  \returns 0 on success; negative in case of error
 */
int osmo_signal_register_handler(unsigned int subsys,
				 osmo_signal_cbfn *cbfn, void *data)
{
	struct signal_handler *sig_data;

	if (!tall_sigh_ctx2)
		tall_sigh_ctx2 = talloc_named_const(OTC_GLOBAL, 0, "osmo_signal");
	if (!tall_sigh_ctx2)
		return -ENOMEM;

	sig_data = talloc(tall_sigh_ctx2, struct signal_handler);
	if (!sig_data)
		return -ENOMEM;

	sig_data->subsys = subsys;
	sig_data->data = data;
	sig_data->cbfn = cbfn;

	/* FIXME: check if we already have a handler for this subsys/cbfn/data */

	llist_add_tail(&sig_data->entry, &signal_handler_list);

	return 0;
}

/*! Unregister signal handler
 *  \param[in] subsys Subsystem number
 *  \param[in] cbfn Callback function
 *  \param[in] data Data passed through to callback
 */
void osmo_signal_unregister_handler(unsigned int subsys,
				    osmo_signal_cbfn *cbfn, void *data)
{
	struct signal_handler *handler;

	llist_for_each_entry(handler, &signal_handler_list, entry) {
		if (handler->cbfn == cbfn && handler->data == data 
		    && subsys == handler->subsys) {
			llist_del(&handler->entry);
			talloc_free(handler);
			break;
		}
	}
}

/*! dispatch (deliver) a new signal to all registered handlers
 *  \param[in] subsys Subsystem number
 *  \param[in] signal Signal number,
 *  \param[in] signal_data Data to be passed along to handlers
 */
void osmo_signal_dispatch(unsigned int subsys, unsigned int signal,
			  void *signal_data)
{
	struct signal_handler *handler;

	llist_for_each_entry(handler, &signal_handler_list, entry) {
		if (handler->subsys != subsys)
			continue;
		(*handler->cbfn)(subsys, signal, handler->data, signal_data);
	}
}

/*! @} */
