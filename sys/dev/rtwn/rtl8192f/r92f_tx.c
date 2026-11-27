/*
 * Copyright (c) 2026 Mir Saheb Ali <mirsahebali@pm.me>
 *
 * SPDX-License-Identifier: BSD-2-Clause
 */

#include <sys/cdefs.h>
#include "opt_wlan.h"

#include <sys/param.h>
#include <sys/lock.h>
#include <sys/mutex.h>
#include <sys/mbuf.h>
#include <sys/kernel.h>
#include <sys/socket.h>
#include <sys/systm.h>
#include <sys/malloc.h>
#include <sys/queue.h>
#include <sys/taskqueue.h>
#include <sys/bus.h>
#include <sys/endian.h>
#include <sys/linker.h>

#include <net/if.h>
#include <net/ethernet.h>
#include <net/if_media.h>

#include <net80211/ieee80211_var.h>
#include <net80211/ieee80211_radiotap.h>
#include <net80211/ieee80211_vht.h>

#include <dev/rtwn/if_rtwnreg.h>
#include <dev/rtwn/if_rtwnvar.h>

#include <dev/rtwn/if_rtwn_ridx.h>

#include <dev/rtwn/rtl8812a/r12a.h>
#include <dev/rtwn/rtl8812a/r12a_tx_desc.h>

#include <dev/rtwn/rtl8192f/r92f.h>

#define R92F_TXDW1_MACID_M	0x0000007f
#define R92F_TXDW1_MACID_S	0

void
r92f_tx_setup_macid(void *buf, int id)
{
	struct r12a_tx_desc *txd = buf;
	
	txd->txdw1 &= htole32(~R92F_TXDW1_MACID_M);
	txd->txdw1 |= htole32(SM(R92F_TXDW1_MACID, id));
}
#define R92F_TXDW5_PATH_A_EN 0x01000000
#define R92F_TXDW5_PATH_B_EN 0x02000000

void
r92f_fill_tx_desc(struct rtwn_softc *sc, struct ieee80211_node *ni,
    struct mbuf *m, void *buf, uint8_t ridx, bool force_rate, int maxretry)
{
	struct r12a_tx_desc *txd = buf;
	struct rtwn_node *un = RTWN_NODE(ni);
	int macid;

	macid = un->id;

	if (IEEE80211_IS_MULTICAST(mtod(m, struct ieee80211_frame *)->i_addr1))
		macid = RTWN_MACID_BC;

	if (macid == RTWN_MACID_UNDEFINED)
		macid = 0;

	r12a_fill_tx_desc(sc, ni, m, buf, ridx, force_rate, maxretry);
	
	/* Vendor does this for some reason */
	txd->txdw5 |= htole32(R92F_TXDW5_PATH_A_EN
	    | ((sc->ntxchains > 1) ? R92F_TXDW5_PATH_B_EN : 0));

	if (IEEE80211_IS_MULTICAST(mtod(m, struct ieee80211_frame *)->i_addr1))
		macid = RTWN_MACID_BC;

	txd->flags0 &= ~(R12A_FLAGS0_LSG | R12A_FLAGS0_FSG);
	txd->txdw1 &= htole32(~R92F_TXDW1_MACID_M);
	txd->txdw1 |= htole32(SM(R92F_TXDW1_MACID, macid));
}

void
r92f_fill_tx_desc_raw(struct rtwn_softc *sc, struct ieee80211_node *ni,
    struct mbuf *m, void *buf, const struct ieee80211_bpf_params *params)
{
	struct r12a_tx_desc *txd;

	txd = (struct r12a_tx_desc *)buf;

	r12a_fill_tx_desc_raw(sc, ni, m, buf, params);

	txd->flags0 &= ~(R12A_FLAGS0_LSG | R12A_FLAGS0_FSG);
}

void
r92f_fill_tx_desc_null(struct rtwn_softc *sc, void *buf, int is11b, int qos,
    int id)
{
	struct r12a_tx_desc *txd;

	txd = (struct r12a_tx_desc *)buf;

	r12a_fill_tx_desc_null(sc, buf, is11b, qos, id);

	txd->flags0 &= ~(R12A_FLAGS0_LSG | R12A_FLAGS0_FSG);
}

