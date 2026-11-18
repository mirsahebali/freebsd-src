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

#include <dev/rtwn/if_rtwnreg.h>
#include <dev/rtwn/if_rtwnvar.h>

#include <dev/rtwn/if_rtwn_ridx.h>

#include <dev/rtwn/rtl8188e/r88e.h>
#include <dev/rtwn/rtl8188e/r88e_priv.h>
#include <dev/rtwn/rtl8188e/r88e_reg.h>

#include <dev/rtwn/rtl8192c/r92c.h>
#include <dev/rtwn/rtl8192c/r92c_var.h>

#include <dev/rtwn/rtl8723b/r23b_reg.h>

#include <dev/rtwn/rtl8812a/r12a.h>
#include <dev/rtwn/rtl8812a/r12a_reg.h>

#include <dev/rtwn/rtl8188f/r88f.h>
#include <dev/rtwn/rtl8188f/r88f_reg.h>

#include <dev/rtwn/rtl8192f/r92f.h>
#include <dev/rtwn/rtl8192f/r92f_reg.h>
#include <dev/rtwn/rtl8192f/r92f_var.h>

static void
r92f_get_txpower(struct rtwn_softc *sc, int chain, struct ieee80211_channel *c,
    uint8_t power[RTWN_RIDX_COUNT])
{
	const struct ieee80211com *ic = &sc->sc_ic;
	struct r92f_softc *rs = sc->sc_priv;
	int i, ridx, group, cck_group, max_mcs, chan;

	/* Determine channel group. */
	if (IEEE80211_IS_CHAN_HT40(c)) {
		chan = c->ic_ieee;
	} else {
		chan = rtwn_chan2centieee(c);
	}
	group = rtwn_chan2group(chan);
	cck_group = rtwn_chan2cckgroup(chan);
	if (group == -1 || cck_group == -1) {	/* shouldn't happen */
		device_printf(sc->sc_dev, "%s: incorrect channel\n", __func__);
		return;
	}

	max_mcs = RTWN_RIDX_HT_MCS(sc->ntxchains * 8 - 1);

	for (ridx = RTWN_RIDX_CCK1; ridx <= RTWN_RIDX_CCK11; ridx++) {
		power[ridx] = rs->cck_tx_pwr[chain][cck_group];
		if (power[ridx] > ic->ic_txpowlimit)
			power[ridx] = ic->ic_txpowlimit;
	}
	for (ridx = RTWN_RIDX_OFDM6; ridx <= max_mcs; ridx++) {
		power[ridx] = rs->ht40_tx_pwr_2g[chain][group];
		if (power[ridx] > ic->ic_txpowlimit)
			power[ridx] = ic->ic_txpowlimit;
	}

	for (ridx = RTWN_RIDX_OFDM6; ridx <= RTWN_RIDX_OFDM54; ridx++) {
		/* Ensure we don't underflow if the power delta is -ve */
		int8_t pwr;

		pwr = power[ridx] + rs->ofdm_tx_pwr_diff_2g[chain][0];
		if (pwr < 0)
			pwr = 0;

		power[ridx] = pwr;
	}

	for (i = 0; i < sc->ntxchains; i++) {
		uint8_t min_mcs;
		int8_t pwr_diff, pwr;

		if (IEEE80211_IS_CHAN_HT40(c))
			pwr_diff = rs->bw40_tx_pwr_diff_2g[chain][i];
		else
			pwr_diff = rs->bw20_tx_pwr_diff_2g[chain][i];

		min_mcs = RTWN_RIDX_HT_MCS(i * 8);
		for (ridx = min_mcs; ridx < min_mcs + 8 && ridx <= max_mcs;
		    ridx++) {
			/* Ensure we don't underflow */
			pwr = power[ridx] + pwr_diff;
			if (pwr < 0)
				pwr = 0;
			power[ridx] = pwr;
		}
	}

	/* Apply max limit. */
	for (ridx = RTWN_RIDX_CCK1; ridx <= max_mcs; ridx++) {
		if (power[ridx] > R92C_MAX_TX_PWR)
			power[ridx] = R92C_MAX_TX_PWR;
	}

}

static void
r92f_set_txpower(struct rtwn_softc *sc, struct ieee80211_channel *c)
{
	uint8_t power[RTWN_RIDX_COUNT];
	int i;

	for (i = 0; i < sc->ntxchains; i++) {
		memset(power, 0, sizeof(power));
		/* Compute per-rate Tx power values. */
		r92f_get_txpower(sc, i, c, power);
		/* Optionally print out the power table */
		r92c_dump_txpower(sc, i, power);
		/* Write per-rate Tx power values to hardware. */
		r92c_write_txpower(sc, i, power);
	}
}

int
r92f_set_tx_power(struct rtwn_softc *sc, struct ieee80211vap *vap)
{

	if (vap->iv_bss == NULL)
		return (EINVAL);
	if (vap->iv_bss->ni_chan == IEEE80211_CHAN_ANYC)
		return (EINVAL);

	r92f_set_txpower(sc, vap->iv_bss->ni_chan);
	return (0);
}


static void
r92f_config_kfree(struct rtwn_softc *sc, uint8_t chan)
{
	uint8_t bb_gain[3] = { 0xff, 0xff, 0xff }; /* EFUSE_UNDEFINED = 0xff */
	uint8_t bb_gain_path_mask[2] = { 0x0f, 0xf0 };
	uint8_t bb_gain_path_shift[2] = { 0, 4 };
	int rfpath; /* RF_A or RF_B */
	uint8_t bb_gain_for_path;
	uint8_t channel_idx = 0;

	if (chan >= 1 && chan <= 3)
		channel_idx = 0;
	if (chan >= 4 && chan <= 9)
		channel_idx = 1;
	if (chan >= 10 && chan <= 14)
		channel_idx = 2;

	r88f_read_efuse_1(sc, 0x1ee, &bb_gain[1]);
	r88f_read_efuse_1(sc, 0x1ec, &bb_gain[0]);
	r88f_read_efuse_1(sc, 0x1ea, &bb_gain[2]);

	if (bb_gain[1] == 0xff)
		return;

	if (bb_gain[0] == 0xff)
		bb_gain[0] = bb_gain[1];

	if (bb_gain[2] == 0xff)
		bb_gain[2] = bb_gain[1];

	for (rfpath = 0; rfpath < 2; rfpath++) {
		/* power_trim based on 55[19:14] */
		rtwn_rf_setbits(sc, rfpath, R88F_RF_UNKNOWN_55, 0x20, 0x20);

		/* enable 55[14] for 0.5db step */
		rtwn_rf_setbits(sc, rfpath, R92F_RF_GAIN_CTRL, 0x40000,
		    0x40000);

		/* enter power_trim debug mode */
		rtwn_rf_setbits(sc, rfpath, R88F_RF_GAIN_CCA, 0x80, 0x80);

		/* write enable */
		rtwn_rf_setbits(sc, rfpath, R88E_RF_WE_LUT, 0x80, 0x80);


		bb_gain_for_path = (bb_gain[channel_idx] &
		    bb_gain_path_mask[rfpath]) >> bb_gain_path_shift[rfpath];

		rtwn_rf_setbits(sc, rfpath, R92C_RF_TXPA_G(2), 0x70000,
		    (channel_idx * 2) << 16);
		rtwn_rf_setbits(sc, rfpath, R92C_RF_TXPA_G(2), 0x3f,
		    bb_gain_for_path);

		rtwn_rf_setbits(sc, rfpath, R92C_RF_TXPA_G(2), 0x70000,
		    (channel_idx * 2 + 1) << 16);
			
		rtwn_rf_setbits(sc, rfpath, R92C_RF_TXPA_G(2), 0x3f,
		    bb_gain_for_path);

		/* leave power_trim debug mode */
		rtwn_rf_setbits(sc, rfpath, R88F_RF_GAIN_CCA, 0x80, 0);

		/* write disable */
		rtwn_rf_setbits(sc, rfpath, R88E_RF_WE_LUT, 0x80, 0);
	}
}

static void
r92f_revise_cck_tx_psf(struct rtwn_softc *sc, uint8_t channel)
{
	if (channel == 13) {
		/* Special value for channel 13 */
		rtwn_bb_write(sc, R92F_CCK0_TX_FILTER1, 0xf8fe0001);
		/* Normal values */
		rtwn_bb_write(sc, R92F_CCK0_TX_FILTER2, 0x64b80c1c);
		rtwn_write_2(sc, R92F_CCK0_DEBUG_PORT, 0x8810);
		rtwn_bb_write(sc, R92F_CCK0_TX_FILTER3, 0x01235667);
	} else if (channel == 14) {
		/* Normal value */
		rtwn_bb_write(sc, R92F_CCK0_TX_FILTER1, 0xe82c0001);
		/* Special values for channel 14 */
		rtwn_bb_write(sc, R92F_CCK0_TX_FILTER2, 0x0000b81c);
		rtwn_write_2(sc, R92F_CCK0_DEBUG_PORT, 0x0000);
		rtwn_bb_write(sc, R92F_CCK0_TX_FILTER3, 0x00003667);
	} else {
		/* Restore normal values from the phy init table */
		rtwn_bb_write(sc, R92F_CCK0_TX_FILTER1, 0xe82c0001);
		rtwn_bb_write(sc, R92F_CCK0_TX_FILTER2, 0x64b80c1c);
		rtwn_write_2(sc, R92F_CCK0_DEBUG_PORT, 0x8810);
		rtwn_bb_write(sc, R92F_CCK0_TX_FILTER3, 0x01235667);
	}
}

void
r92f_set_chan(struct rtwn_softc *sc, struct ieee80211_channel *c)
{
	bool ht40 = IEEE80211_IS_CHAN_HT40(c);
	uint8_t chan, subchan = 0;
	bool sec_ch_above = 0;
	uint32_t reg;

	/* unit: 256us. 256ms */
	rtwn_write_2(sc, R92F_PKT_VO_VI_LIFE_TIME, 0x0400);
	/* unit: 256us. 256ms */
	rtwn_write_2(sc, R92F_PKT_BE_BK_LIFE_TIME, 0x0400);

	r92f_set_txpower(sc, c);

	chan = rtwn_chan2centieee(c);	

	if(IEEE80211_IS_CHAN_HT40U(c)) {
		sec_ch_above = 1;
		subchan = 2;
	} else if(IEEE80211_IS_CHAN_HT40D(c)) {
		sec_ch_above = 0;
		subchan = 1;
	}

	r92f_revise_cck_tx_psf(sc, chan);

	/* Set channel */
	reg = rtwn_rf_read(sc, 0, R92C_RF_CHNLBW);
	reg &= ~(0x60000); /* select the 2.4G band(?) */
	reg = (reg & ~0xff) | (chan & 0xff);
	rtwn_rf_write(sc, 0, R92C_RF_CHNLBW, reg);
	rtwn_rf_write(sc, 1, R92C_RF_CHNLBW, reg);

	r92f_config_kfree(sc, chan);

	rtwn_write_1(sc, R92F_DATA_SUBCHANNEL, subchan);

	/* small BW */
	rtwn_bb_setbits(sc, R92C_OFDM0_TXPSEUDONOISEWGT, 0xC0000000, 0);

	rtwn_bb_setbits(sc, R92C_FPGA0_RFMOD, R92C_RFMOD_40MHZ, ht40);

	rtwn_bb_setbits(sc, R92C_FPGA1_RFMOD, R92C_RFMOD_40MHZ, ht40);

	/* ADC clock = 160M */
	rtwn_bb_setbits(sc, R92C_FPGA0_RFMOD, 0x700, 0x400);

	/* DAC clock = 80M */
	rtwn_bb_setbits(sc, R92C_FPGA0_RFMOD, 0x3000, 0x2000); 

	/* ADC buffer clk */
	rtwn_bb_setbits(sc, R92F_ANTDIV_PARA1, 0xc000000, 0x8000000); 

	if (ht40)
		/* Set Control channel to upper or lower. */
		rtwn_bb_setbits(sc, R92C_CCK0_SYSTEM, R92C_CCK0_SYSTEM_CCK_SIDEBAND,
		    (!sec_ch_above) << 4);

	/* Enable CCK */
	rtwn_bb_setbits(sc, R92C_FPGA0_RFMOD, 0, R92F_FPGA0_RFMOD_CCK);

	/* RF TRX_BW */
	reg = rtwn_rf_read(sc, 0, R92C_RF_CHNLBW);
	reg &= ~R92F_RF_CHNLBW_BW_MASK;
	if (ht40)
		reg |= R88F_RF_CHNLBW_BW40;
	else
		reg |= R88F_RF_CHNLBW_BW20;
	rtwn_rf_write(sc, 0, R92C_RF_CHNLBW, reg);
	rtwn_rf_write(sc, 1, R92C_RF_CHNLBW, reg);

	/* Modify RX DFIR parameters */
	rtwn_bb_setbits(sc, R92F_TAP_UPD_97F, 0x300000, 0x200000);

	rtwn_bb_setbits(sc, R92F_DOWNSAM_FACTOR, 0x30000000, 0x20000000);

	if (ht40)
		reg = 0x3;
	else
		reg = 0x1a3;
	rtwn_bb_setbits(sc, R92F_RX_DFIR_MOD_97F, 0x1ff, reg);
}
