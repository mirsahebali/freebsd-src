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
#include <net80211/ieee80211_ratectl.h>

#include <dev/rtwn/if_rtwnreg.h>
#include <dev/rtwn/if_rtwnvar.h>

#include <dev/rtwn/if_rtwn_debug.h>
#include <dev/rtwn/if_rtwn_ridx.h>

#include <dev/rtwn/rtl8192c/r92c_rx_desc.h>

#include <dev/rtwn/rtl8192f/r92f.h>
#include <dev/rtwn/rtl8192f/r92f_reg.h>
#include <dev/rtwn/rtl8192f/r92f_rx_desc.h>
#include <dev/rtwn/rtl8192f/r92f_var.h>

#include <dev/rtwn/rtl8812a/r12a_rx_desc.h>

int8_t
r92f_get_rssi_cck(struct rtwn_softc *sc, void *physt)
{
	struct r92f_rx_phystat *phy = (struct r92f_rx_phystat *)physt;
	struct r92f_softc *rs = sc->sc_priv;
	uint32_t rxdw0, rxdw3;
	uint8_t vga_idx, lna_idx;
	int8_t rx_pwr_all;

	rxdw0 = le32toh(phy->rxdw0);
	rxdw3 = le32toh(phy->rxdw3);

	if (rs->cck_new_agc)
		return ((int8_t)(MS(rxdw0, R92F_RXDW0_PWDB0) - 110));

	lna_idx = (MS(rxdw3, R92F_RXDW3_LNA_H) << 3) | MS(rxdw3, R92F_RXDW3_LNA_L);
	vga_idx = MS(rxdw3, R92F_RXDW3_VGA);

	switch (lna_idx) {
	case 7:
		rx_pwr_all = -44 - (2 * vga_idx);
		break;
	case 5:
		rx_pwr_all = -28 - (2 * vga_idx);
		break;
	case 3:
		rx_pwr_all = -10 - (2 * vga_idx);
		break;
	case 0:
		rx_pwr_all = 14 - (2 * vga_idx);
		break;
	default:
		rx_pwr_all = 0;
		break;
	}

	return (rx_pwr_all);
}

int8_t
r92f_get_rssi_ofdm(struct rtwn_softc *sc, void *physt)
{
	struct r92f_rx_phystat *phy = (struct r92f_rx_phystat *)physt;
	uint32_t rxdw0, rxdw1;
	uint8_t pagenum, pwdb_max;
	int rx_path;
	uint8_t pwdb[4];

	rxdw0 = le32toh(phy->rxdw0);
	rxdw1 = le32toh(phy->rxdw1);

	pagenum = MS(rxdw0, R92F_RXDW0_PAGE_NUM);

	if (pagenum == 0)
		return (MS(rxdw0, R92F_RXDW0_PWDB0) - 110);

	if (pagenum > 2)
		return (0);

	pwdb[0] = MS(rxdw0, R92F_RXDW0_PWDB0);
	pwdb[1] = MS(rxdw0, R92F_RXDW0_PWDB1);
	pwdb[2] = MS(rxdw0, R92F_RXDW0_PWDB2);
	pwdb[3] = MS(rxdw1, R92F_RXDW1_PWDB3);

	pwdb_max = 0;

	for (rx_path = 0; rx_path < sc->nrxchains; rx_path++)
		pwdb_max = pwdb_max > pwdb[rx_path] ? pwdb_max : pwdb[rx_path];

	return ((uint8_t)(pwdb_max - 110));
}

void
r92f_get_rx_stats(struct rtwn_softc *sc, struct ieee80211_rx_stats *rxs,
    const void *desc, const void *physt_ptr)
{
	const struct r92c_rx_stat *stat = desc;
	uint32_t rxdw0, rxdw1, rxdw3, rxdw4;
	uint8_t rate;

	rxdw0 = le32toh(stat->rxdw0);
	rxdw1 = le32toh(stat->rxdw1);
	rxdw3 = le32toh(stat->rxdw3);
	rxdw4 = le32toh(stat->rxdw4);

	rate = MS(rxdw3, R92F_RXDW3_MCS);

	if (rxdw1 & R92C_RXDW1_AMPDU) {
		if (rxdw0 & R92C_RXDW0_PHYST)
			rxs->c_pktflags |= IEEE80211_RX_F_AMPDU;
		else
			rxs->c_pktflags |= IEEE80211_RX_F_AMPDU_MORE;
	}

	if ((rxdw4 & R92F_RXDW4_SPLCP) && RTWN_RATE_IS_HT(rate))
		rxs->c_pktflags |= IEEE80211_RX_F_SHORTGI;

	if (MS(rxdw4, R92F_RXDW4_BW) != 0)
		rxs->c_width = IEEE80211_RX_FW_40MHZ;
	else
		rxs->c_width = IEEE80211_RX_FW_20MHZ;

	if (RTWN_RATE_IS_CCK(rate))
		rxs->c_phytype = IEEE80211_RX_FP_11B;
	else if (RTWN_RATE_IS_OFDM(rate))
		rxs->c_phytype = IEEE80211_RX_FP_11G;
	else
		rxs->c_phytype = IEEE80211_RX_FP_11NG;

	/* Map HW rate index to 802.11 rate. */
	if (RTWN_RATE_IS_CCK(rate) || RTWN_RATE_IS_OFDM(rate)) {
		rxs->c_rate = ridx2rate[rate];
		if (RTWN_RATE_IS_CCK(rate))
			rxs->c_pktflags |= IEEE80211_RX_F_CCK;
		else
			rxs->c_pktflags |= IEEE80211_RX_F_OFDM;
	} else if (RTWN_RATE_IS_HT(rate)) {
		rxs->c_rate = IEEE80211_RATE_MCS | RTWN_RIDX_TO_MCS(rate);
		rxs->c_pktflags |= IEEE80211_RX_F_HT;
	} else {
		/* Reserved/unsupported rate. */
		rxs->c_rate = 0;
	}
}

