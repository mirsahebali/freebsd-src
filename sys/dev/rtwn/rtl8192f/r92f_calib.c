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

#include <dev/rtwn/if_rtwn_debug.h>

#include <dev/rtwn/rtl8188e/r88e.h>
#include <dev/rtwn/rtl8188e/r88e_reg.h>

#include <dev/rtwn/rtl8188f/r88f.h>
#include <dev/rtwn/rtl8188f/r88f_reg.h>

#include <dev/rtwn/rtl8192c/r92c_reg.h>

#include <dev/rtwn/rtl8192f/r92f.h>
#include <dev/rtwn/rtl8192f/r92f_reg.h>
#include <dev/rtwn/rtl8192f/r92f_var.h>

#define LC_BACKUP_M	0xc0000000
#define LC_BACKUP_S	30

void
r92f_lc_calib(struct rtwn_softc *sc)
{
	uint32_t backup;
	uint32_t reg;

	/*
	 * Init GPIO settings for 8192f
	 */
	rtwn_setbits_1(sc, R92C_GPIO_MUXCFG, R92C_GPIO_MUXCFG_ENBT, 0);

	/* Aries's NarrowBand */
	reg = rtwn_bb_read(sc, R92C_OFDM0_TXPSEUDONOISEWGT);
	backup = MS(reg, LC_BACKUP);

	reg = RW(reg, LC_BACKUP, 0);
	rtwn_bb_write(sc, R92C_OFDM0_TXPSEUDONOISEWGT, reg);

	r88f_lc_calib(sc);

	/* Aries's NarrowBand */
	reg = rtwn_bb_read(sc, R92C_OFDM0_TXPSEUDONOISEWGT);
	reg = RW(reg, LC_BACKUP, backup);
	rtwn_bb_write(sc, R92C_OFDM0_TXPSEUDONOISEWGT, reg);

	/* reset OFDM state */
	rtwn_bb_setbits(sc, R92C_FPGA0_RFMOD, R92C_RFMOD_OFDM_EN, 0);
	rtwn_bb_setbits(sc, R92C_FPGA0_RFMOD, 0, R92C_RFMOD_OFDM_EN);
}

#define PATH_A 0
#define PATH_B 1

#define R92F_IQK_MAX_PATHS 2
#define R92F_IQK_MAX_TOLERANCE 5
#define R92F_IQK_POLL_MAX 21
#define R92F_IQK_POLL_STEP 5
#define R92F_IQK_MAX_RETRIES 2
#define R92F_RXIQB_EXT 0xca8

struct r92f_iqk_result {
	int32_t before;
	int32_t after;
};

struct r92f_iqk_run_result {
	int32_t before[R92F_IQK_MAX_PATHS];
	int32_t after[R92F_IQK_MAX_PATHS];
};

#define R92F_ADDA_REGS 2
#define R92F_MAC_REGS 4
#define R92F_BB_REGS 9

struct r92f_iqk_regs_vals {
	uint32_t adda_regs[R92F_ADDA_REGS];
	uint32_t mac_regs[R92F_MAC_REGS];
	uint32_t bb_regs[R92F_BB_REGS];
	uint32_t hssi_param1;
};

static const uint32_t iqk_adda_regs[R92F_ADDA_REGS] = {
	R92F_ANAPWR1, R88F_RX_WAIT_CCA
};
static const uint32_t iqk_mac_regs[R92F_MAC_REGS] = {
	R92C_TXPAUSE, R92C_BCN_CTRL(0),
	R92C_BCN_CTRL(1), R92C_GPIO_MUXCFG
};
static const uint32_t iqk_bb_regs[R92F_BB_REGS] = {
	R92C_OFDM0_TRXPATHENA, R92C_OFDM0_TRMUXPAR,
	R88F_FPGA0_XCD_RF_SW_CTRL, R92C_CONFIG_ANT(0), R92C_CONFIG_ANT(1),
	R92F_DPDT_CTRL, R92F_RFE_CTRL_ANT_SRC(0),
	R92F_RFE_CTRL_ANT_SRC(2), R92C_CCK0_AFESETTING
};

static void
r92f_iqk_save_regs(struct rtwn_softc *sc, struct r92f_iqk_regs_vals *backup)
{
	int i;

	for (i = 0; i < R92F_ADDA_REGS; i++)
		backup->adda_regs[i] = rtwn_bb_read(sc, iqk_adda_regs[i]);

	for (i = 0; i < R92F_MAC_REGS - 1; i++)
		backup->mac_regs[i] = rtwn_read_1(sc, iqk_mac_regs[i]);

	backup->mac_regs[i] = rtwn_bb_read(sc, iqk_mac_regs[i]);

	for (i = 0; i < R92F_BB_REGS; i++)
		backup->bb_regs[i] = rtwn_bb_read(sc, iqk_bb_regs[i]);
}

static void
r92f_iqk_restore_regs(struct rtwn_softc *sc, struct r92f_iqk_regs_vals *backup)
{
	int i;

	for (i = 0; i < R92F_ADDA_REGS; i++)
		rtwn_bb_write(sc, iqk_adda_regs[i], backup->adda_regs[i]);

	for (i = 0; i < R92F_MAC_REGS - 1; i++)
		rtwn_write_1(sc, iqk_mac_regs[i], backup->mac_regs[i]);

	rtwn_bb_write(sc, iqk_mac_regs[i], backup->mac_regs[i]);

	for (i = 0; i < R92F_BB_REGS; i++)
		rtwn_bb_write(sc, iqk_bb_regs[i], backup->bb_regs[i]);
}

static void
r92f_iqk_setup_bb(struct rtwn_softc *sc, uint32_t hssi_param1)
{
	rtwn_bb_write(sc, R88F_FPGA0_ANALOG4, 0xccf000c0);
	rtwn_bb_write(sc, R92F_ANAPWR1, 0x44ffbb44);
	rtwn_bb_write(sc, R88F_RX_WAIT_CCA, 0x00400040);
	rtwn_bb_write(sc, R92C_OFDM0_TRXPATHENA, 0x6f005403);
	rtwn_bb_write(sc, R92C_OFDM0_TRMUXPAR, 0x000804e4);
	rtwn_bb_write(sc, R88F_FPGA0_XCD_RF_SW_CTRL, 0x04203400);
	rtwn_bb_write(sc, R92C_HSSI_PARAM1(0), hssi_param1);
}

/* One shot, path A LOK & IQK */
static void
r92f_iqk_one_shot(struct rtwn_softc *sc)
{
	rtwn_bb_write(sc, R92C_IQK_AGC_PTS, 0xfa005800);
	rtwn_bb_write(sc, R92C_IQK_AGC_PTS, 0xf8005800);
}

static void
r92f_iqk_poll(struct rtwn_softc *sc, uint32_t reg)
{
	int ktime;

	ktime = 0;

	while (rtwn_bb_read(sc, reg) == 0 && ktime < R92F_IQK_POLL_MAX) {
		rtwn_delay(sc, R92F_IQK_POLL_STEP);
		ktime += R92F_IQK_POLL_STEP;
	}
}

static void
r92f_iqk_pack_run(struct r92f_iqk_run_result *tx_result,
    struct r92f_iqk_run_result *rx_result, int32_t val[8])
{
	val[0] = tx_result->before[PATH_A];
	val[1] = tx_result->after[PATH_A];
	val[2] = rx_result->before[PATH_A];
	val[3] = rx_result->after[PATH_A];
	val[4] = tx_result->before[PATH_B];
	val[5] = tx_result->after[PATH_B];
	val[6] = rx_result->before[PATH_B];
	val[7] = rx_result->after[PATH_B];
}

static void
r92f_iqk_unpack_run(struct r92f_iqk_run_result *tx_result,
    struct r92f_iqk_run_result *rx_result, int32_t val[8])
{
	tx_result->before[PATH_A] = val[0];
	tx_result->after[PATH_A]  = val[1];
	rx_result->before[PATH_A] = val[2];
	rx_result->after[PATH_A]  = val[3];
	tx_result->before[PATH_B] = val[4];
	tx_result->after[PATH_B]  = val[5];
	rx_result->before[PATH_B] = val[6];
	rx_result->after[PATH_B]  = val[7];
}

static bool
r92f_iq_calib_compare_results(struct r92f_iqk_run_result *tx_result1,
    struct r92f_iqk_run_result *rx_result1,
    struct r92f_iqk_run_result *tx_result2,
    struct r92f_iqk_run_result *rx_result2,
    struct r92f_iqk_run_result *tx_merged,
    struct r92f_iqk_run_result *rx_merged)
{
	int32_t v1[8], v2[8], m[8];
	int32_t *candidate[2] = { 0, 0 };
	int32_t tmp1, tmp2;
	uint32_t diff, simubitmap;
	bool retval = true;
	int i, j;

	r92f_iqk_pack_run(tx_result1, rx_result1, v1);
	r92f_iqk_pack_run(tx_result2, rx_result2, v2);
	r92f_iqk_pack_run(tx_merged, rx_merged, m);

	simubitmap = 0;

	for (i = 0; i < 8; i++) {
		if (i & 1) {
			tmp1 = v1[i];
			if (tmp1 & 0x200)
				tmp1 |= ~0x3ff;
			tmp2 = v2[i];
			if (tmp2 & 0x200)
				tmp2 |= ~0x3ff;
		} else {
			tmp1 = v1[i];
			tmp2 = v2[i];
		}

		diff = (tmp1 > tmp2) ? (tmp1 - tmp2) : (tmp2 - tmp1);

		if (diff > R92F_IQK_MAX_TOLERANCE) {
			if ((i == 2 || i == 6) && !simubitmap) {
				if (v1[i] + v1[i + 1] == 0)
					candidate[i / 4] = v2;
				else if (v2[i] + v2[i + 1] == 0)
					candidate[i / 4] = v1;
				else
					simubitmap |= (1U << i);
			} else {
				simubitmap |= (1U << i);
			}
		}
	}

	if (simubitmap == 0) {
		for (i = 0; i < 2; i++) {
			if (candidate[i] != NULL) {
				for (j = i * 4; j < (i + 1) * 4 - 2; j++)
					m[j] = candidate[i][j];
				retval = false;
			}
		}
	} else {
		retval = false;

		if (!(simubitmap & 0x03)) {
			/* path A TX OK */
			for (i = 0; i < 2; i++)
				m[i] = v1[i];
		}

		if (!(simubitmap & 0x0c)) {
			/* path A RX OK */
			for (i = 2; i < 4; i++)
				m[i] = v1[i];
		}

		if (!(simubitmap & 0x30)) {
			/* path B TX OK */
			for (i = 4; i < 6; i++)
				m[i] = v1[i];
		}

		if (!(simubitmap & 0xc0)) {
			/* path B RX OK */
			for (i = 6; i < 8; i++)
				m[i] = v1[i];
		}
	}

	r92f_iqk_unpack_run(tx_merged, rx_merged, m);

	return (retval);
}

static void
r92f_fill_iqk_matrix_a(struct rtwn_softc *sc,
    bool path_ok, const struct r92f_iqk_run_result *tx_result,
    const struct r92f_iqk_run_result *rx_result, bool tx_only)
{
	uint32_t oldval, x, tx0_a, result_reg;
	int y, tx0_c;
	uint32_t reg;

	if (!path_ok)
		return;

	reg = rtwn_bb_read(sc, R92C_OFDM0_TXIQIMBALANCE(0));
	oldval = reg >> 22;

	x = tx_result->before[PATH_A];
	if ((x & 0x00000200) != 0)
		x = x | 0xfffffc00;
	tx0_a = (x * oldval) >> 8;

	reg = rtwn_bb_read(sc, R92C_OFDM0_TXIQIMBALANCE(0));
	reg &= ~0x3ff;
	reg |= tx0_a;
	rtwn_bb_write(sc, R92C_OFDM0_TXIQIMBALANCE(0), reg);

	reg = rtwn_bb_read(sc, R92C_OFDM0_ECCATHRESHOLD);
	reg &= ~0x80000000;
	if ((x * oldval >> 7) & 0x1)
		reg |= 0x80000000;
	rtwn_bb_write(sc, R92C_OFDM0_ECCATHRESHOLD, reg);

	y = tx_result->after[PATH_A];
	if ((y & 0x00000200) != 0)
		y |= 0xfffffc00;
	tx0_c = (y * oldval) >> 8;

	rtwn_bb_setbits(sc, R92C_OFDM0_TXAFE(0), 0xf0000000,
		 ((tx0_c & 0x3c0) >> 6) << 28);

	reg = rtwn_bb_read(sc, R92C_OFDM0_TXIQIMBALANCE(0));
	reg &= ~0x003f0000;
	reg |= ((tx0_a & 0x3f) << 16);
	rtwn_bb_write(sc, R92C_OFDM0_TXIQIMBALANCE(0), reg);

	reg = rtwn_bb_read(sc, R92C_OFDM0_ECCATHRESHOLD);
	reg &= ~0x20000000;
	if ((y * oldval >> 7) & 0x1)
		reg |= 0x20000000;
	rtwn_bb_write(sc, R92C_OFDM0_ECCATHRESHOLD, reg);

	if (tx_only) {
		RTWN_DPRINTF(sc, RTWN_DEBUG_CALIB, "%s: only TX\n", __func__);
		return;
	}

	result_reg = rx_result->before[PATH_A];
	rtwn_bb_setbits(sc, R92C_OFDM0_RXIQIMBALANCE(0), 0x3ff,
		 result_reg & 0x3ff);

	result_reg = rx_result->after[PATH_A] & 0x3f;
	rtwn_bb_setbits(sc, R92C_OFDM0_RXIQIMBALANCE(0), 0xfc00,
		 (result_reg << 10) & 0xfc00);

	result_reg = (rx_result->after[PATH_A] >> 6) & 0xf;
	rtwn_bb_setbits(sc, R92C_OFDM0_RXIQEXTANTA, 0xf0000000,
		 result_reg << 28);
}

static void
r92f_fill_iqk_matrix_b(struct rtwn_softc *sc,
    bool path_ok, const struct r92f_iqk_run_result *tx_result,
    const struct r92f_iqk_run_result *rx_result, bool tx_only)
{
	uint32_t oldval, x, tx1_a, result_reg;
	int y, tx1_c;
	uint32_t reg;

	if (!path_ok)
		return;

	reg = rtwn_bb_read(sc, R92C_OFDM0_TXIQIMBALANCE(1));
	oldval = reg >> 22;

	x = tx_result->before[PATH_B];
	if ((x & 0x00000200) != 0)
		x = x | 0xfffffc00;
	tx1_a = (x * oldval) >> 8;

	reg = rtwn_bb_read(sc, R92C_OFDM0_TXIQIMBALANCE(1));
	reg &= ~0x3ff;
	reg |= tx1_a;
	rtwn_bb_write(sc, R92C_OFDM0_TXIQIMBALANCE(1), reg);

	reg = rtwn_bb_read(sc, R92C_OFDM0_ECCATHRESHOLD);
	reg &= ~0x80000000;
	if ((x * oldval >> 7) & 0x1)
		reg |= 0x80000000;
	rtwn_bb_write(sc, R92C_OFDM0_ECCATHRESHOLD, reg);

	y = tx_result->after[PATH_B];
	if ((y & 0x00000200) != 0)
		y |= 0xfffffc00;
	tx1_c = (y * oldval) >> 8;

	rtwn_bb_setbits(sc, R92C_OFDM0_TXAFE(1), 0xf0000000,
		 ((tx1_c & 0x3c0) >> 6) << 28);

	reg = rtwn_bb_read(sc, R92C_OFDM0_TXIQIMBALANCE(1));
	reg &= ~0x003f0000;
	reg |= ((tx1_a & 0x3f) << 16);
	rtwn_bb_write(sc, R92C_OFDM0_TXIQIMBALANCE(1), reg);

	reg = rtwn_bb_read(sc, R92C_OFDM0_ECCATHRESHOLD);
	reg &= ~0x20000000;
	if ((y * oldval >> 7) & 0x1)
		reg |= 0x20000000;
	rtwn_bb_write(sc, R92C_OFDM0_ECCATHRESHOLD, reg);

	if (tx_only) {
		RTWN_DPRINTF(sc, RTWN_DEBUG_CALIB, "%s: only TX\n", __func__);
		return;
	}

	result_reg = rx_result->before[PATH_B];
	rtwn_bb_setbits(sc, R92C_OFDM0_RXIQIMBALANCE(1), 0x3ff,
		 result_reg & 0x3ff);

	result_reg = rx_result->after[PATH_B] & 0x3f;
	rtwn_bb_setbits(sc, R92C_OFDM0_RXIQIMBALANCE(1), 0xfc00,
		 (result_reg << 10) & 0xfc00);

	result_reg = (rx_result->after[PATH_B] >> 6) & 0xf;
	rtwn_bb_setbits(sc, R92F_RXIQB_EXT, 0x000000f0, result_reg << 4);
}

static int
r92f_iqk_tx_path_a(struct rtwn_softc *sc)
{
	struct r92f_softc *rs = sc->sc_priv;
	struct r92f_iqk_result tx_result;
	struct r92f_iqk_result rx_result;
	uint32_t rf_0x58_i, rf_0x58_q, reg;
	uint8_t rfe = rs->rfe;
	int result = 0;
	int i;

	/* Leave IQK mode */
	rtwn_bb_setbits(sc, R92C_FPGA0_IQK, 0xffffff00, 0);

	r92f_iqk_setup_bb(sc, 0x01000100);

	rtwn_rf_setbits(sc, 0, R88F_RF_GAIN_CCA, 0x10, 0x10);
	rtwn_rf_setbits(sc, 0, R88F_RF_GAIN_CCA, 0x800, 0x800);

	if (rfe == 7 || rfe == 8 || rfe == 9 || rfe == 12)
		reg = 0x30;
	else
		reg = 0xe9;

	rtwn_rf_setbits(sc, 0, R88F_RF_PAD_TXG, 0x003ff, reg);

	rtwn_bb_setbits(sc, R92C_FPGA0_IQK, 0xffffff00, 0x80800000);

	/* path-A IQK setting */
	rtwn_bb_write(sc, R92C_TX_IQK_TONE(0), 0x18008c1c);
	rtwn_bb_write(sc, R92C_RX_IQK_TONE(0), 0x38008c1c);
	rtwn_bb_write(sc, R92C_TX_IQK_TONE(1), 0x38008c1c);
	rtwn_bb_write(sc, R92C_RX_IQK_TONE(1), 0x38008c1c);

	rtwn_bb_write(sc, R92C_TX_IQK_PI(0), 0x8214000f);
	rtwn_bb_write(sc, R92C_RX_IQK_PI(0), 0x28140000);

	rtwn_bb_write(sc, R92C_TX_IQK, 0x01007c00);
	rtwn_bb_write(sc, R92C_RX_IQK, 0x01004800);

	/* LO calibration setting */
	rtwn_bb_write(sc, R92C_IQK_AGC_RSP, 0x00e62911);

	r92f_iqk_one_shot(sc);

	rtwn_delay(sc, 15);

	r92f_iqk_poll(sc, R92F_IQK_RPT_TXA);

	/* Check failed */
	rx_result.after = rtwn_bb_read(sc, R92C_RX_POWER_IQK_AFTER(0));
	tx_result.before = rtwn_bb_read(sc, R92C_TX_POWER_IQK_BEFORE(0));
	tx_result.after = rtwn_bb_read(sc, R92C_TX_POWER_IQK_AFTER(0));

	/* reload 0xdf and CCK_IND off */
	rtwn_bb_setbits(sc, R92C_FPGA0_IQK, 0xffffff00, 0);

	rtwn_rf_setbits(sc, 0, R88E_RF_WE_LUT, 0x10, 0x10);

	reg = rtwn_rf_read(sc, 0, R92F_RF_TXMOD);
	rf_0x58_i = (reg & 0x000fc000) >> 14;
	rf_0x58_q = (reg & 0x003f0) >> 4;

	for (i = 0; i < 8; i++) {
		rtwn_rf_setbits(sc, 0, R92C_RF_TXPA_G(2), 0x1c000, i << 14);
		rtwn_rf_setbits(sc, 0, R92C_RF_TXPA_G(2), 0x00fc0, rf_0x58_i << 6);
		rtwn_rf_setbits(sc, 0, R92C_RF_TXPA_G(2), 0x0003f, rf_0x58_q);
	}

	rtwn_rf_setbits(sc, 0, R92C_RF_AC, 0x4000, 0);
	rtwn_rf_setbits(sc, 0, R88E_RF_WE_LUT, 0x10, 0);
	rtwn_rf_setbits(sc, 0, R88F_RF_GAIN_CCA, 0x00810, 0);

	if (!(rx_result.after & 0x10000000) &&
	    ((tx_result.before & 0x03ff0000) != 0x01420000) &&
	    ((tx_result.after & 0x03ff0000) != 0x00420000))
		result |= 0x01;

	return (result);
}

static int
r92f_iqk_tx_rx_path_a(struct rtwn_softc *sc)
{
	struct r92f_iqk_result rx_result;
	struct r92f_iqk_result tx_result;
	int result = 0;
	uint32_t reg;

	/* Leave IQK mode */
	rtwn_bb_setbits(sc, R92C_FPGA0_IQK, 0xffffff00, 0);

	/* PA/PAD control by 0x56, and set = 0x0 */
	rtwn_rf_setbits(sc, 0, R88F_RF_GAIN_CCA, 0x2, 0x2);
	rtwn_rf_write(sc, 0, R92F_RF_GAIN_P1, 0);
	rtwn_rf_setbits(sc, 0, R88F_RF_GAIN_CCA, 0x800, 0x800);
	rtwn_rf_setbits(sc, 0, R88F_RF_PAD_TXG, 0x003ff, 0x27);

	/* Enter IQK mode */
	rtwn_bb_setbits(sc, R92C_FPGA0_IQK, 0xffffff00, 0x80800000);

	/* path-A IQK setting */
	rtwn_bb_write(sc, R92C_TX_IQK_TONE(0), 0x18008c1c);
	rtwn_bb_write(sc, R92C_RX_IQK_TONE(0), 0x38008c1c);
	rtwn_bb_write(sc, R92C_TX_IQK_TONE(1), 0x38008c1c);
	rtwn_bb_write(sc, R92C_RX_IQK_TONE(1), 0x38008c1c);

	rtwn_bb_write(sc, R92C_TX_IQK_PI(0), 0x82160027);
	rtwn_bb_write(sc, R92C_RX_IQK_PI(0), 0x28160000);

	/* Tx IQK setting */
	rtwn_bb_write(sc, R92C_TX_IQK, 0x01007c00);
	rtwn_bb_write(sc, R92C_RX_IQK, 0x01004800);

	/* LO calibration setting */
	rtwn_bb_write(sc, R92C_IQK_AGC_RSP, 0x0086a911);

	r92f_iqk_one_shot(sc);

	rtwn_delay(sc, 15);

	r92f_iqk_poll(sc, R92F_IQK_RPT_TXA);

	/* Check failed */
	rx_result.after = rtwn_bb_read(sc, R92C_RX_POWER_IQK_AFTER(0));
	tx_result.before = rtwn_bb_read(sc, R92C_TX_POWER_IQK_BEFORE(0));
	tx_result.after = rtwn_bb_read(sc, R92C_TX_POWER_IQK_AFTER(0));

	if (!(rx_result.after & 0x10000000) &&
	    ((tx_result.before & 0x03ff0000) != 0x01420000) &&
	    ((tx_result.after & 0x03ff0000) != 0x00420000)) {
		result |= 0x01;
	} else { /* If TX not OK, ignore RX */
		/* PA/PAD controlled by 0x0 */
		rtwn_bb_setbits(sc, R92C_FPGA0_IQK, 0xffffff00, 0);
		rtwn_rf_setbits(sc, 0, R88F_RF_GAIN_CCA, 0x800, 0);

		return (result);
	}

	reg = 0x80007c00 | (tx_result.before & 0x3ff0000) | ((tx_result.after & 0x3ff0000) >> 16);
	rtwn_bb_write(sc, R92C_TX_IQK, reg);

	/* Modify RX IQK mode table */
	rtwn_bb_setbits(sc, R92C_FPGA0_IQK, 0xffffff00, 0);

	/* PA/PAD control by 0x56, and set = 0x0 */
	rtwn_rf_setbits(sc, 0, R88F_RF_GAIN_CCA, 0x2, 0x2);
	rtwn_rf_write(sc, 0, R92F_RF_GAIN_P1, 0);
	rtwn_rf_setbits(sc, 0, R88F_RF_GAIN_CCA, 0x800, 0x800);
	rtwn_rf_setbits(sc, 0, R88F_RF_PAD_TXG, 0x003ff, 0x1e0);

	r92f_iqk_setup_bb(sc, 0x01000100);

	/* Enter IQK mode */
	rtwn_bb_setbits(sc, R92C_FPGA0_IQK, 0xffffff00, 0x80800000);

	/* path-A IQK setting */
	rtwn_bb_write(sc, R92C_TX_IQK_TONE(0), 0x38008c1c);
	rtwn_bb_write(sc, R92C_RX_IQK_TONE(0), 0x18008c1c);
	rtwn_bb_write(sc, R92C_TX_IQK_TONE(1), 0x38008c1c);
	rtwn_bb_write(sc, R92C_RX_IQK_TONE(1), 0x38008c1c);

	rtwn_bb_write(sc, R92C_TX_IQK_PI(0), 0x82170000);
	rtwn_bb_write(sc, R92C_RX_IQK_PI(0), 0x28170000);

	/* RX IQK setting */
	rtwn_bb_write(sc, R92C_RX_IQK, 0x01004800);

	/* LO calibration setting */
	rtwn_bb_write(sc, R92C_IQK_AGC_RSP, 0x0046a8d1);

	r92f_iqk_one_shot(sc);

	rtwn_delay(sc, 15);

	r92f_iqk_poll(sc, R92F_IQK_RPT_RXA);

	/* Check failed */
	rx_result.after = rtwn_bb_read(sc, R92C_RX_POWER_IQK_AFTER(0));
	rx_result.before = rtwn_bb_read(sc, R92C_RX_POWER_IQK_BEFORE(0));

	/* Leave IQK mode */
	rtwn_bb_setbits(sc, R92C_FPGA0_IQK, 0xffffff00, 0);

	rtwn_rf_setbits(sc, 0, R88F_RF_GAIN_CCA, 0x800, 0);
	rtwn_rf_write(sc, 0, R92F_RF_GAIN_P1, 0x02000);

	if (!(rx_result.after & 0x8000000) &&
	    ((rx_result.before & 0x03ff0000) != 0x01320000) &&
	    ((rx_result.after & 0x03ff0000) != 0x00360000))
		result |= 0x02;

	return (result);
}

static int
r92f_iqk_tx_path_b(struct rtwn_softc *sc)
{
	struct r92f_softc *rs = sc->sc_priv;
	struct r92f_iqk_result tx_result;
	struct r92f_iqk_result rx_result;
	uint32_t rf_0x58_i, rf_0x58_q;
	uint32_t reg;
	uint8_t rfe = rs->rfe;
	int result = 0;
	int i;

	/* PA/PAD controlled by 0x0 */
	rtwn_bb_setbits(sc, R92C_FPGA0_IQK, 0xffffff00, 0);

	r92f_iqk_setup_bb(sc, 0x01000000);

	rtwn_rf_setbits(sc, 1, R88F_RF_GAIN_CCA, 0x10, 0x10);
	rtwn_rf_setbits(sc, 1, R88F_RF_GAIN_CCA, 0x800, 0x800);

	if (rfe == 7 || rfe == 8 || rfe == 9 || rfe == 12)
		rtwn_rf_setbits(sc, 1, R88F_RF_PAD_TXG, 0x003ff, 0x30);

	else
		rtwn_rf_setbits(sc, 1, R88F_RF_PAD_TXG, 0x00fff, 0xe9);

	rtwn_bb_setbits(sc, R92C_FPGA0_IQK, 0xffffff00, 0x80800000);

	/* Path B IQK setting */
	rtwn_bb_write(sc, R92C_TX_IQK_TONE(0), 0x38008c1c);
	rtwn_bb_write(sc, R92C_RX_IQK_TONE(0), 0x38008c1c);
	rtwn_bb_write(sc, R92C_TX_IQK_TONE(1), 0x18008c1c);
	rtwn_bb_write(sc, R92C_RX_IQK_TONE(1), 0x38008c1c);

	rtwn_bb_write(sc, R92C_TX_IQK_PI(1), 0x8214000F);
	rtwn_bb_write(sc, R92C_RX_IQK_PI(1), 0x28140000);

	rtwn_bb_write(sc, R92C_TX_IQK, 0x01007c00);
	rtwn_bb_write(sc, R92C_RX_IQK, 0x01004800);

	/* LO calibration setting */
	rtwn_bb_write(sc, R92C_IQK_AGC_RSP, 0x00e62911);

	r92f_iqk_one_shot(sc);

	rtwn_delay(sc, 15);

	r92f_iqk_poll(sc, R92F_IQK_RPT_TXB);

	/* Check failed */
	rx_result.after = rtwn_bb_read(sc, R92C_RX_POWER_IQK_AFTER(0));
	tx_result.before = rtwn_bb_read(sc, R92C_TX_POWER_IQK_BEFORE(1));
	tx_result.after = rtwn_bb_read(sc, R92C_TX_POWER_IQK_AFTER(1));

	/* reload 0xdf and CCK_IND off */
	rtwn_bb_setbits(sc, R92C_FPGA0_IQK, 0xffffff00, 0);

	rtwn_rf_setbits(sc, 1, R88E_RF_WE_LUT, 0x10, 0x10);

	reg = rtwn_rf_read(sc, 1, R92F_RF_TXMOD);
	rf_0x58_i = (reg & 0xfc000) >> 14;
	rf_0x58_q = (reg & 0x003f0) >> 4;

	for (i = 0; i < 8; i++) {
		rtwn_rf_setbits(sc, 1, R92C_RF_TXPA_G(2), 0x1c000, i << 14);
		rtwn_rf_setbits(sc, 1, R92C_RF_TXPA_G(2), 0x00fc0, rf_0x58_i << 6);
		rtwn_rf_setbits(sc, 1, R92C_RF_TXPA_G(2), 0x0003f, rf_0x58_q);
	}

	rtwn_rf_setbits(sc, 1, R92C_RF_AC, 0x4000, 0);
	rtwn_rf_setbits(sc, 1, R88E_RF_WE_LUT, 0x10, 0);
	rtwn_rf_setbits(sc, 1, R88F_RF_GAIN_CCA, 0x00810, 0);

	if (!(rx_result.after & 0x80000000) &&
	    ((tx_result.before & 0x03ff0000) != 0x01420000) &&
	    ((tx_result.after & 0x03ff0000) != 0x00420000))
		result |= 0x01;
	else
		RTWN_DPRINTF(sc, RTWN_DEBUG_CALIB, "%s: Path B IQK failed!\n",
			 __func__);

	return (result);
}

static int
r92f_iqk_tx_rx_path_b(struct rtwn_softc *sc)
{
	struct r92f_iqk_result rx_result_a;
	struct r92f_iqk_result tx_result_b;
	uint32_t reg_eac, reg_ec4, reg_ecc;
	uint32_t reg;
	int result = 0;

	/* Leave IQK mode */
	rtwn_bb_setbits(sc, R92C_FPGA0_IQK, 0xffffff00, 0);

	rtwn_rf_setbits(sc, 1, R88F_RF_GAIN_CCA, 0x2, 0x2);
	rtwn_rf_write(sc, 1, R92F_RF_GAIN_P1, 0);
	rtwn_rf_setbits(sc, 1, R88F_RF_GAIN_CCA, 0x800, 0x800);
	rtwn_rf_setbits(sc, 1, R88F_RF_PAD_TXG, 0x003ff, 0x67);

	r92f_iqk_setup_bb(sc, 0x01000000);

	rtwn_bb_setbits(sc, R92C_FPGA0_IQK, 0xffffff00, 0x80800000);

	/* path-B IQK setting */
	rtwn_bb_write(sc, R92C_TX_IQK_TONE(0), 0x38008c1c);
	rtwn_bb_write(sc, R92C_RX_IQK_TONE(0), 0x38008c1c);
	rtwn_bb_write(sc, R92C_TX_IQK_TONE(1), 0x18008c1c);
	rtwn_bb_write(sc, R92C_RX_IQK_TONE(1), 0x38008c1c);

	rtwn_bb_write(sc, R92C_TX_IQK_PI(1), 0x82160027);
	rtwn_bb_write(sc, R92C_RX_IQK_PI(1), 0x28160000);

	/* LO calibration setting */
	rtwn_bb_write(sc, R92C_IQK_AGC_RSP, 0x0086a911);

	r92f_iqk_one_shot(sc);

	rtwn_delay(sc, 15);

	r92f_iqk_poll(sc, R92F_IQK_RPT_TXB);

	/* Check failed */
	rx_result_a.after = rtwn_bb_read(sc, R92C_RX_POWER_IQK_AFTER(0));
	tx_result_b.before = rtwn_bb_read(sc, R92C_TX_POWER_IQK_BEFORE(1));
	tx_result_b.after = rtwn_bb_read(sc, R92C_TX_POWER_IQK_AFTER(1));

	if (!(rx_result_a.after & 0x80000000) &&
	    ((tx_result_b.before & 0x03ff0000) != 0x01420000) &&
	    ((tx_result_b.after & 0x03ff0000) != 0x00420000)) {
		result |= 0x01;
	} else {
		/* PA/PAD controlled by 0x0 */
		rtwn_bb_setbits(sc, R92C_FPGA0_IQK, 0xffffff00, 0);

		rtwn_rf_setbits(sc, 1, R88F_RF_GAIN_CCA, 0x800, 0);
		return (result);
	}

	reg = 0x80007c00 | (tx_result_b.before & 0x03ff0000) | ((tx_result_b.after >> 16) & 0x03ff);
	rtwn_bb_write(sc, R92C_TX_IQK, reg);

	/* Modify RX IQK mode table */
	rtwn_bb_setbits(sc, R92C_FPGA0_IQK, 0xffffff00, 0);

	rtwn_rf_setbits(sc, 1, R88F_RF_GAIN_CCA, 0x2, 0x2);
	rtwn_rf_write(sc, 1, R92F_RF_GAIN_P1, 0);
	rtwn_rf_setbits(sc, 1, R88F_RF_GAIN_CCA, 0x800, 0x800);
	rtwn_rf_setbits(sc, 1, R88F_RF_PAD_TXG, 0x003ff, 0x1e0);

	r92f_iqk_setup_bb(sc, 0x01000000);

	rtwn_bb_setbits(sc, R92C_FPGA0_IQK, 0xffffff00, 0x80800000);

	/* Path B IQK setting */
	rtwn_bb_write(sc, R92C_TX_IQK_TONE(0), 0x38008c1c);
	rtwn_bb_write(sc, R92C_RX_IQK_TONE(0), 0x38008c1c);
	rtwn_bb_write(sc, R92C_TX_IQK_TONE(1), 0x38008c1c);
	rtwn_bb_write(sc, R92C_RX_IQK_TONE(1), 0x18008c1c);

	rtwn_bb_write(sc, R92C_TX_IQK_PI(1), 0x82170000);
	rtwn_bb_write(sc, R92C_RX_IQK_PI(1), 0x28170000);

	/* IQK setting */
	rtwn_bb_write(sc, R92C_RX_IQK, 0x01004800);

	/* LO calibration setting */
	rtwn_bb_write(sc, R92C_IQK_AGC_RSP, 0x0046a911);

	r92f_iqk_one_shot(sc);

	rtwn_delay(sc, 15);

	r92f_iqk_poll(sc, R92F_IQK_RPT_RXB);

	reg_eac = rtwn_bb_read(sc, R92C_RX_POWER_IQK_AFTER(0));
	reg_ec4 = rtwn_bb_read(sc, R92C_RX_POWER_IQK_BEFORE(1));
	reg_ecc = rtwn_bb_read(sc, R92C_RX_POWER_IQK_AFTER(1));

	rtwn_bb_setbits(sc, R92C_FPGA0_IQK, 0xffffff00, 0);
	rtwn_bb_write(sc, R92C_HSSI_PARAM1(0), 0x01000100);

	rtwn_rf_setbits(sc, 1, R88F_RF_GAIN_CCA, 0x800, 0);
	rtwn_rf_setbits(sc, 1, R88F_RF_GAIN_CCA, 0x2, 0);
	rtwn_rf_write(sc, 1, R92F_RF_GAIN_P1, 0x02000);

	if (!(reg_eac & 0x40000000) &&
	    ((reg_ec4 & 0x03ff0000) != 0x01320000) &&
	    ((reg_ecc & 0x03ff0000) != 0x00360000))
		result |= 0x02;
	else
		RTWN_DPRINTF(sc, RTWN_DEBUG_CALIB, "%s: Path B RX IQK failed!\n", __func__);

	return (result);
}

typedef int (*r92f_iqk_func)(struct rtwn_softc *);

static int
r92f_iqk_run_retry(struct rtwn_softc *sc, r92f_iqk_func func, int expected,
    uint32_t before_reg, uint32_t after_reg, int32_t *before, int32_t *after,
    int32_t fail_before, int32_t fail_after)
{
	int i, result;
	uint32_t reg;

	result = 0;
	for (i = 0; i < R92F_IQK_MAX_RETRIES; i++) {
		result = func(sc);

		if (result == expected) {
			reg = rtwn_bb_read(sc, before_reg);
			*before = MS(reg, R92C_POWER_IQK_RESULT);

			reg = rtwn_bb_read(sc, after_reg);
			*after = MS(reg, R92C_POWER_IQK_RESULT);
			break;
		}

		*before = fail_before;
		*after = fail_after;
	}

	return (result);
}

static void
r92f_iq_calib_run(struct rtwn_softc *sc,
    struct r92f_iqk_regs_vals *iqk_backup_regs,
    struct r92f_iqk_run_result *tx_result,
    struct r92f_iqk_run_result *rx_result, int t)
{
	struct r92f_softc *rs = sc->sc_priv;
	uint32_t rx_initial_gain_a, rx_initial_gain_b;
	int path_a_ok, path_a_tx_ok, path_b_ok, path_b_tx_ok;
	uint8_t rfe = rs->rfe;

	/*
	 * Note: IQ calibration must be performed after loading
	 *       PHY_REG.txt , and radio_a, radio_b.txt
	 */

	rtwn_bb_setbits(sc, R92C_FPGA0_IQK, 0xffffff00, 0);

	rx_initial_gain_a = rtwn_bb_read(sc, R92C_OFDM0_AGCCORE1(0));
	rx_initial_gain_b = rtwn_bb_read(sc, R92C_OFDM0_AGCCORE1(1));
	
	if (t == 0) {
		/* Save ADDA parameters, turn Path A ADDA on */
		r92f_iqk_save_regs(sc, iqk_backup_regs);
	}

	rtwn_bb_setbits(sc, R92C_FPGA0_RFPARAM(1), 0, 0x80000000);

	/* MAC settings */
	rtwn_write_1(sc, R92C_TXPAUSE, 0xff);
	rtwn_setbits_1(sc, R92C_GPIO_MUXCFG, R92C_GPIO_MUXCFG_ENBT, 0);

	if (rfe == 7 || rfe == 8 || rfe == 9 || rfe == 12) {
		/* path A */
		rtwn_bb_setbits(sc, R92F_RFE_CTRL_ANT_SRC(0), 0xf, 0x7);
		rtwn_bb_setbits(sc, R92F_DPDT_CTRL, 0x1, 0x0);

		rtwn_bb_setbits(sc, R92F_RFE_CTRL_ANT_SRC(0), 0xf00, 0x700);
		rtwn_bb_setbits(sc, R92F_DPDT_CTRL, 0x4, 0x0);

		rtwn_bb_setbits(sc, R92F_RFE_CTRL_ANT_SRC(0), 0xF000, 0x7000);
		rtwn_bb_setbits(sc, R92F_DPDT_CTRL, 0x8, 0x0);

		/* path B */
		rtwn_bb_setbits(sc, R92F_RFE_CTRL_ANT_SRC(2), 0xf0, 0x70);
		rtwn_bb_setbits(sc, R92F_DPDT_CTRL, 0x20000, 0x0);

		rtwn_bb_setbits(sc, R92F_RFE_CTRL_ANT_SRC(2), 0xf0000, 0x70000);
		rtwn_bb_setbits(sc, R92F_DPDT_CTRL, 0x100000, 0x0);

		rtwn_bb_setbits(sc, R92F_RFE_CTRL_ANT_SRC(3), 0xf000, 0x7000);
		rtwn_bb_setbits(sc, R92F_DPDT_CTRL, 0x8000000, 0x0);
	}

	/* path B standby */
	rtwn_bb_setbits(sc, R92C_FPGA0_IQK, 0xffffff00, 0x000000);
	rtwn_rf_write(sc, 1, R92C_RF_AC, 0x10000);
	rtwn_bb_setbits(sc, R92C_FPGA0_IQK, 0xffffff00, 0x80800000);

	path_a_tx_ok = r92f_iqk_run_retry(sc, r92f_iqk_tx_path_a, 0x01,
	    R92C_TX_POWER_IQK_BEFORE(0), R92C_TX_POWER_IQK_AFTER(0),
	    &tx_result[t].before[PATH_A], &tx_result[t].after[PATH_A],
	    0x100, 0x0);

	path_a_ok = r92f_iqk_run_retry(sc, r92f_iqk_tx_rx_path_a, 0x03,
	    R92C_RX_POWER_IQK_BEFORE(0), R92C_RX_POWER_IQK_AFTER(0),
	    &rx_result[t].before[PATH_A], &rx_result[t].after[PATH_A],
	    0x100, 0x0);

	if (!path_a_tx_ok || !path_a_ok)
		RTWN_DPRINTF(sc, RTWN_DEBUG_CALIB, "%s: Path A IQK failed!\n",
		    __func__);

	path_b_tx_ok = r92f_iqk_run_retry(sc, r92f_iqk_tx_path_b, 0x01,
	    R92C_TX_POWER_IQK_BEFORE(1), R92C_TX_POWER_IQK_AFTER(1),
	    &tx_result[t].before[PATH_B], &tx_result[t].after[PATH_B],
	    0x100, 0x0);

	path_b_ok = r92f_iqk_run_retry(sc, r92f_iqk_tx_rx_path_b, 0x03,
	    R92C_RX_POWER_IQK_BEFORE(1), R92C_RX_POWER_IQK_AFTER(1),
	    &rx_result[t].before[PATH_B], &rx_result[t].after[PATH_B],
	    0x100, 0x0);

	if (!path_b_tx_ok || !path_b_ok )
		RTWN_DPRINTF(sc, RTWN_DEBUG_CALIB, "%s: Path B IQK failed!\n",
		    __func__);

	/* Back to BB mode, load original value */
	rtwn_bb_setbits(sc, R92C_FPGA0_IQK, 0xffffff00, 0);

	rtwn_bb_write(sc, R88F_FPGA0_ANALOG4, 0xcc0000c0);
	rtwn_bb_write(sc, R92F_ANAPWR1, 0x44bbbb44);
	rtwn_bb_write(sc, R88F_RX_WAIT_CCA, 0x80408040);
	rtwn_bb_write(sc, R92C_OFDM0_TRXPATHENA, 0x6f005433);
	rtwn_bb_write(sc, R92C_OFDM0_TRMUXPAR, 0x000004e4);
	rtwn_bb_write(sc, R88F_FPGA0_XCD_RF_SW_CTRL, 0x04003400);
	rtwn_bb_write(sc, R92C_HSSI_PARAM1(0), 0x01000100);

	/* Reload ADDA power saving parameters, MAC parameters and BB parameters */
	r92f_iqk_restore_regs(sc, iqk_backup_regs);

	rtwn_bb_setbits(sc, R92C_FPGA0_RFPARAM(1), 0x80000000, 0);

	/* Restore RX initial gain */
	rtwn_bb_setbits(sc, R92C_OFDM0_AGCCORE1(0), 0xff, 0x50);
	rtwn_bb_setbits(sc, R92C_OFDM0_AGCCORE1(0), 0xff,
	    rx_initial_gain_a & 0xff);
	rtwn_bb_setbits(sc, R92C_OFDM0_AGCCORE1(1), 0xff, 0x50);
	rtwn_bb_setbits(sc, R92C_OFDM0_AGCCORE1(1), 0xff,
	    rx_initial_gain_b & 0xff);
}

void
r92f_iq_calib(struct rtwn_softc *sc)
{
	int32_t reg_e94, reg_e9c, reg_ea4, reg_eac;
	int32_t reg_eb4, reg_ebc, reg_ec4, reg_ecc;
	struct r92f_softc *rs = sc->sc_priv;
	uint32_t path_a_0xdf, path_a_0x35;
	uint32_t path_b_0xdf, path_b_0x35;
	bool path_a_ok, path_b_ok;
	uint8_t rfe = rs->rfe;
	uint32_t rfe_path_select;
	struct r92f_iqk_regs_vals backup_regs;
	struct r92f_iqk_run_result tx_result[3] = { {{0}, {0}} };
	struct r92f_iqk_run_result rx_result[3] = { {{0}, {0}} };
	struct r92f_iqk_run_result tx_merged = { 0 };
	struct r92f_iqk_run_result rx_merged = { 0 };
	struct r92f_iqk_run_result *tx_selected = NULL;
	struct r92f_iqk_run_result *rx_selected = NULL;
	int i, candidate;
	int32_t reg_tmp = 0;
	bool match;

	rfe_path_select = rtwn_bb_read(sc, R92F_RFE_PATH_SELECT);

	path_a_0xdf = rtwn_rf_read(sc, 0, R88F_RF_GAIN_CCA);
	path_a_0x35 = rtwn_rf_read(sc, 0, R92F_RF_GAIN_P1);
	path_b_0xdf = rtwn_rf_read(sc, 1, R88F_RF_GAIN_CCA);
	path_b_0x35 = rtwn_rf_read(sc, 1, R92F_RF_GAIN_P1);

	candidate = -1;

	path_a_ok = false;
	path_b_ok = false;

	for (i = 0; i < 3; i++) {
		r92f_iq_calib_run(sc, &backup_regs, tx_result, rx_result, i);

		if (i == 1) {
			if (r92f_iq_calib_compare_results(&tx_result[0],
			    &rx_result[0], &tx_result[1], &rx_result[1],
			    &tx_merged, &rx_merged)) {
				candidate = 0;
				break;
			}
		}

		if (i == 2) {
			if (r92f_iq_calib_compare_results(&tx_result[0],
			    &rx_result[0], &tx_result[2], &rx_result[2],
			    &tx_merged, &rx_merged)) {
				candidate = 0;
				break;
			}

			match = r92f_iq_calib_compare_results(&tx_result[1],
			    &rx_result[1], &tx_result[2], &rx_result[2],
			    &tx_merged, &rx_merged);
			if (match) {
				candidate = 1;
			} else {
				reg_tmp += tx_merged.before[PATH_A];
				reg_tmp += tx_merged.after[PATH_A];
				reg_tmp += rx_merged.before[PATH_A];
				reg_tmp += rx_merged.after[PATH_A];
				reg_tmp += tx_merged.before[PATH_B];
				reg_tmp += tx_merged.after[PATH_B];
				reg_tmp += rx_merged.before[PATH_B];
				reg_tmp += rx_merged.after[PATH_B];

				if (!reg_tmp) {
					RTWN_DPRINTF(sc, RTWN_DEBUG_CALIB,
					    "%s: IQK failed completely\n",
					    __func__);
					candidate = -1;
				} else
					candidate = 3;
			}
		}
	}

	if (candidate >= 0) {
		if (candidate == 3) {
			tx_selected = &tx_merged;
			rx_selected = &rx_merged;
		} else {
			tx_selected = &tx_result[candidate];
			rx_selected = &rx_result[candidate];
		}
		reg_e94 = tx_selected->before[PATH_A];
		reg_e9c = tx_selected->after[PATH_A];
		reg_ea4 = rx_selected->before[PATH_A];
		reg_eac = rx_selected->after[PATH_A];
		reg_eb4 = tx_selected->before[PATH_B];
		reg_ebc = tx_selected->after[PATH_B];
		reg_ec4 = rx_selected->before[PATH_B];
		reg_ecc = rx_selected->after[PATH_B];

		RTWN_DPRINTF(sc, RTWN_DEBUG_CALIB,
		    "%s: candidate is %x\n", __func__, candidate);
		RTWN_DPRINTF(sc, RTWN_DEBUG_CALIB,
		    "%s: e94=%x e9c=%x ea4=%x eac=%x eb4=%x ebc=%x ec4=%x ecc=%x\n",
		    __func__, reg_e94, reg_e9c, reg_ea4, reg_eac,
		    reg_eb4, reg_ebc, reg_ec4, reg_ecc);

		path_a_ok = true;
		path_b_ok = true;
	}

	rtwn_bb_setbits(sc, R92C_TX_IQK_TONE(0), 0x3ff00000, 0x10000000);
	rtwn_bb_setbits(sc, R92F_NP_ANTA, 0x3ff, 0);
	rtwn_bb_setbits(sc, R92C_TX_IQK_TONE(1), 0x3ff00000, 0x10000000);
	rtwn_bb_setbits(sc, R92F_TAP_UPD_97F, 0x3ff, 0);

	if (candidate >= 0) {
		if (reg_e94)
			r92f_fill_iqk_matrix_a(sc, path_a_ok, tx_selected,
			    rx_selected, reg_ea4 == 0);

		if (reg_eb4)
			r92f_fill_iqk_matrix_b(sc, path_b_ok, tx_selected,
			    rx_selected, reg_ec4 == 0);
	}

	rtwn_rf_write(sc, 0, R88F_RF_GAIN_CCA, path_a_0xdf);
	rtwn_rf_write(sc, 0, R92F_RF_GAIN_P1, path_a_0x35);
	rtwn_rf_write(sc, 1, R88F_RF_GAIN_CCA, path_b_0xdf);
	rtwn_rf_write(sc, 1, R92F_RF_GAIN_P1, path_b_0x35);

	if (rfe == 7 || rfe == 8 || rfe == 9 || rfe == 12) {
		rtwn_bb_setbits(sc, R92F_SW_GPIO_SHARE_CTRL(1), 0, 0x70000);
		rtwn_bb_setbits(sc, R92C_LEDCFG0, 0x6c00000, 0);
		rtwn_bb_setbits(sc, R88E_BB_PAD_CTRL, 0, 0x30000000);
		rtwn_bb_setbits(sc, R92F_SW_GPIO_SHARE_CTRL(0), 0x600010, 0);

		/*
		 * Originally:
		 * odm_set_bb_reg(dm, R_0x944, BIT(11) | 0x1F, 0x3F);
		 *
		 * It clears bit 11 and sets bits 0..4. The mask doesn't cover
		 * bit 5 so it's not modified. Is that what it's supposed to
		 * accomplish?
		 */
		rtwn_bb_setbits(sc, R92F_RFE_BUFFER, 0x81f, 0x1f);

		if (rfe == 7) {
			rtwn_bb_setbits(sc, R92F_RFE_CTRL_ANT_SRC(0), 0xfffff, 0x23200);
			rtwn_bb_setbits(sc, R92F_RFE_CTRL_ANT_SRC(2), 0xfffff, 0x23200);
			rtwn_bb_setbits(sc, R92F_RFE_CTRL_ANT_SRC(1), 0xf000, 0x3000);
			rtwn_bb_setbits(sc, R92F_RFE_CTRL_ANT_SRC(3), 0xf000, 0x3000);

		} else {
			rtwn_bb_setbits(sc, R92F_RFE_CTRL_ANT_SRC(0), 0xfffff, 0x22200);
			rtwn_bb_setbits(sc, R92F_RFE_CTRL_ANT_SRC(2), 0xfffff, 0x22200);
			rtwn_bb_setbits(sc, R92F_RFE_CTRL_ANT_SRC(1), 0xf000, 0x2000);
			rtwn_bb_setbits(sc, R92F_RFE_CTRL_ANT_SRC(3), 0xf000, 0x2000);
		}

		rtwn_bb_setbits(sc, R92F_RFE_OPT62, 0x4, 0);

		if (rfe == 7)
			rtwn_bb_write(sc, R92F_RFE_OPT, 0x03000003);

		rtwn_bb_write(sc, R92F_RFE_PATH_SELECT, rfe_path_select);
	}
}
