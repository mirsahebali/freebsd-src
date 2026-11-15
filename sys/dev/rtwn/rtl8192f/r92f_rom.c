/*	$OpenBSD: if_urtwn.c,v 1.16 2011/02/10 17:26:40 jakemsr Exp $	*/

/*-
 * Copyright (c) 2010 Damien Bergamini <damien.bergamini@free.fr>
 * Copyright (c) 2014 Kevin Lo <kevlo@FreeBSD.org>
 * Copyright (c) 2015-2016 Andriy Voskoboinyk <avos@FreeBSD.org>
 * Copyright (c) 2026 Mir Saheb Ali <mirsahebali@pm.me>
 *
 * Permission to use, copy, modify, and distribute this software for any
 * purpose with or without fee is hereby granted, provided that the above
 * copyright notice and this permission notice appear in all copies.
 *
 * THE SOFTWARE IS PROVIDED "AS IS" AND THE AUTHOR DISCLAIMS ALL WARRANTIES
 * WITH REGARD TO THIS SOFTWARE INCLUDING ALL IMPLIED WARRANTIES OF
 * MERCHANTABILITY AND FITNESS. IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR
 * ANY SPECIAL, DIRECT, INDIRECT, OR CONSEQUENTIAL DAMAGES OR ANY DAMAGES
 * WHATSOEVER RESULTING FROM LOSS OF USE, DATA OR PROFITS, WHETHER IN AN
 * ACTION OF CONTRACT, NEGLIGENCE OR OTHER TORTIOUS ACTION, ARISING OUT OF
 * OR IN CONNECTION WITH THE USE OR PERFORMANCE OF THIS SOFTWARE.
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

#include <dev/rtwn/if_rtwnreg.h>
#include <dev/rtwn/if_rtwnvar.h>

#include <dev/rtwn/if_rtwn_debug.h>

#include <dev/rtwn/rtl8192c/r92c_var.h>
#include <dev/rtwn/rtl8192c/r92c_reg.h>

#include <dev/rtwn/rtl8192e/r92e_rom_image.h>

#include <dev/rtwn/rtl8192f/r92f.h>
#include <dev/rtwn/rtl8192f/r92f_priv.h>
#include <dev/rtwn/rtl8192f/r92f_var.h>
#include <dev/rtwn/rtl8192f/r92f_rom_image.h>

void
r92f_parse_rom(struct rtwn_softc *sc, uint8_t *buf)
{
	struct r92f_softc *rs = sc->sc_priv;
	struct r92f_rom *rom = (struct r92f_rom *)buf;
	uint8_t pwr_diff;
	int i, j, k;

	sc->thermal_meter = rom->thermal_meter;

	rs->crystalcap = RTWN_GET_ROM_VAR(rom->crystalcap,
	    R92E_ROM_CRYSTALCAP_DEF);

	for (i = 0; i < sc->ntxchains; i++) {
		struct r92e_tx_pwr_2g *pwr_2g = &rom->tx_pwr[i].pwr_2g;
		struct r92e_tx_pwr_diff_2g *pwr_diff_2g =
		    &rom->tx_pwr[i].pwr_diff_2g;

		for (j = 0; j < R92E_GROUP_2G - 1; j++) {
			rs->cck_tx_pwr[i][j] =
			    RTWN_GET_ROM_VAR(pwr_2g->cck[j],
				R92E_DEF_TX_PWR_2G);
			rs->ht40_tx_pwr_2g[i][j] =
			    RTWN_GET_ROM_VAR(pwr_2g->ht40[j],
				R92E_DEF_TX_PWR_2G);
		}
		rs->cck_tx_pwr[i][j] = RTWN_GET_ROM_VAR(pwr_2g->cck[j],
		    R92E_DEF_TX_PWR_2G);

		rs->cck_tx_pwr_diff_2g[i][0] = 0;
		rs->ofdm_tx_pwr_diff_2g[i][0] = RTWN_SIGN4TO8(
		    MS(pwr_diff_2g->ht20_ofdm, LOW_PART));
		rs->bw20_tx_pwr_diff_2g[i][0] = RTWN_SIGN4TO8(
		    MS(pwr_diff_2g->ht20_ofdm, HIGH_PART));
		rs->bw40_tx_pwr_diff_2g[i][0] = 0;
		pwr_diff = RTWN_GET_ROM_VAR(pwr_diff_2g->ht20_ofdm,
		    R92E_DEF_TX_PWR_HT20_DIFF);
		if (pwr_diff != R92E_DEF_TX_PWR_HT20_DIFF) {
			rs->ofdm_tx_pwr_diff_2g[i][0] = RTWN_SIGN4TO8(
			    MS(pwr_diff_2g->ht20_ofdm, LOW_PART));
			rs->bw20_tx_pwr_diff_2g[i][0] = RTWN_SIGN4TO8(
			    MS(pwr_diff_2g->ht20_ofdm, HIGH_PART));
		} else {
			rs->ofdm_tx_pwr_diff_2g[i][0] =
			rs->bw20_tx_pwr_diff_2g[i][0] = pwr_diff;
		}

		for (j = 1, k = 0; k < nitems(pwr_diff_2g->diff123); j++, k++) {
			pwr_diff = RTWN_GET_ROM_VAR(
			    pwr_diff_2g->diff123[k].ofdm_cck,
			    R92E_DEF_TX_PWR_DIFF);
			if (pwr_diff != R92E_DEF_TX_PWR_DIFF) {
				rs->cck_tx_pwr_diff_2g[i][j] = RTWN_SIGN4TO8(
				    MS(pwr_diff_2g->diff123[k].ofdm_cck,
				    LOW_PART));
				rs->ofdm_tx_pwr_diff_2g[i][j] = RTWN_SIGN4TO8(
				    MS(pwr_diff_2g->diff123[k].ofdm_cck,
				    HIGH_PART));
			} else {
				rs->cck_tx_pwr_diff_2g[i][j] =
				rs->ofdm_tx_pwr_diff_2g[i][j] = pwr_diff;
			}
			pwr_diff = RTWN_GET_ROM_VAR(
			    pwr_diff_2g->diff123[k].ht40_ht20,
			    R92E_DEF_TX_PWR_DIFF);
			if (pwr_diff != R92E_DEF_TX_PWR_DIFF) {
				rs->bw20_tx_pwr_diff_2g[i][j] = RTWN_SIGN4TO8(
				    MS(pwr_diff_2g->diff123[k].ht40_ht20,
				    LOW_PART));
				rs->bw40_tx_pwr_diff_2g[i][j] = RTWN_SIGN4TO8(
				    MS(pwr_diff_2g->diff123[k].ht40_ht20,
				    HIGH_PART));
			} else {
				rs->bw20_tx_pwr_diff_2g[i][j] =
				rs->bw40_tx_pwr_diff_2g[i][j] = pwr_diff;
			}
		}
	}

	rs->regulatory = MS(rom->rf_board_opt, R92C_ROM_RF1_REGULATORY);
	rs->crystalcap = rom->crystalcap & 0x3f;
	rs->rfe = rom->rfe_option;

	/* Read MAC address. */
	IEEE80211_ADDR_COPY(sc->sc_ic.ic_macaddr, rom->macaddr);
}
