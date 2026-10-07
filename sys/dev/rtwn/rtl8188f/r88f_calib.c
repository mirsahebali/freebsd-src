#include <sys/cdefs.h>

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

// TODO: rename move these defs to .h files
#define REG_OFDM1_LSTF			0x0d00
#define OFDM_LSTF_MASK			0x70000000
#define REG_TXPAUSE			0x0522
#define RF6052_REG_MODE_AG		0x18	/* RF channel and BW switch */

#define R92C_FPGA0_IQK			0xe28
#define R92C_TX_IQK_TONE_A		0xe30
#define R92C_RX_IQK_TONE_A		0xe34
#define R92C_TX_IQK_PI_A		0xe38
#define R92C_RX_IQK_PI_A		0xe3c
#define R92C_TX_IQK			0xe40
#define R92C_RX_IQK			0xe44
#define R92C_IQK_AGC_PTS		0xe48
#define R92C_IQK_AGC_RSP		0xe4c
#define R92C_TX_IQK_TONE_B		0xe50
#define R92C_RX_IQK_TONE_B		0xe54
#define R92C_TX_IQK_PI_B		0xe58
#define R92C_RX_IQK_PI_B		0xe5c
#define R92C_IQK_AGC_CONT		0xe60
#define R92C_CONFIG_ANT_A		0xb68
#define R92C_CONFIG_ANT_B		0xb6c
#define R92C_TXPAUSE_AC_VO		0x01
#define R92C_TXPAUSE_AC_VI		0x02
#define R92C_TXPAUSE_AC_BE		0x04
#define R92C_TXPAUSE_AC_BK		0x08
#define R92C_TXPAUSE_MGNT		0x10
#define R92C_TXPAUSE_HIGH		0x20
#define R92C_TXPAUSE_BCN		0x40
#define R92C_TXPAUSE_BCN_HIGH_MGNT	0x80
#define R92C_BCN_CTRL1			0x551
#define R92C_BCN_CTRL_EN_MBSSID		0x02
#define R92C_BCN_CTRL_TXBCN_RPT		0x04
#define R92C_BCN_CTRL_EN_BCN		0x08
#define R92C_BCN_CTRL_DIS_TSF_UDT0	0x10
#define R92C_TX_POWER_BEFORE_IQK_A	0xe94
#define R92C_TX_POWER_AFTER_IQK_A	0xe9c
#define R92C_RX_POWER_BEFORE_IQK_A	0xea0
#define R92C_RX_POWER_BEFORE_IQK_A_2	0xea4
#define R92C_RX_POWER_AFTER_IQK_A	0xea8
#define R92C_RX_POWER_AFTER_IQK_A_2	0xeac
#define R88F_ADDA_REGS			16
#define R88F_MAC_REGS			4
#define R88F_BB_REGS			9
#define REG_BEACON_CTRL			0x0550
#define REG_BEACON_CTRL_1		0x0551
#define REG_GPIO_MUXCFG			0x0040
#define REG_RX_OFDM			0x0ed0
#define REG_RX_WAIT_RIFS		0x0ed4
#define REG_RX_TO_RX			0x0ed8
#define REG_SLEEP			0x0ee0
#define REG_STANDBY			0x0edc
#define REG_PMPD_ANAEN			0x0eec
#define REG_RX_CCK			0x0e8c
#define REG_FPGA0_XA_RF_INT_OE		0x0860	/* RF Channel switch */
#define REG_FPGA0_XB_RF_INT_OE		0x0864
#define REG_OFDM0_XA_RX_IQ_IMBALANCE	0x0c14
#define REG_OFDM0_XB_RX_IQ_IMBALANCE	0x0c1c
#define REG_FPGA0_RF_MODE		0x0800
#define REG_OFDM0_XA_TX_IQ_IMBALANCE	0x0c80
#define REG_OFDM0_XB_TX_IQ_IMBALANCE	0x0c88
#define REG_OFDM0_XC_TX_IQ_IMBALANCE	0x0c90
#define REG_OFDM0_XD_TX_IQ_IMBALANCE	0x0c98
#define REG_FPGA0_XAB_RF_SW_CTRL	0x0870
#define REG_FPGA0_XA_RF_SW_CTRL		0x0870	/* 16 bit */
#define REG_FPGA0_XB_RF_SW_CTRL		0x0872	/* 16 bit */
#define REG_FPGA0_XCD_RF_SW_CTRL	0x0874
#define REG_FPGA0_XC_RF_SW_CTRL		0x0874	/* 16 bit */
#define REG_FPGA0_XD_RF_SW_CTRL		0x0876	/* 16 bit */
#define REG_OFDM0_ENERGY_CCA_THRES	0x0c4c
#define REG_OFDM0_RX_IQ_EXT_ANTA	0x0ca0
#define REG_OFDM0_XC_TX_AFE		0x0c94
#define REG_OFDM0_XD_TX_AFE		0x0c9c
#define RF6052_REG_WE_LUT		0xef
#define RF6052_REG_GAIN_CTRL		0xf5
#define REG_FPGA0_IQK			0x0e28
#define RF6052_REG_GAIN_P1		0x35
#define RF6052_REG_T_METER_8723B	0x42
#define RF6052_REG_UNKNOWN_43		0x43
#define RF6052_REG_UNKNOWN_55		0x55
#define RF6052_REG_PAD_TXG		0x56
#define RF6052_REG_TXMOD		0x58
#define RF6052_REG_RXG_MIX_SWBW		0x87
#define RF6052_REG_S0S1			0xb0
#define RF6052_REG_GAIN_CCA		0xdf
#define RF6052_REG_UNKNOWN_ED		0xed
#define REG_TX_IQK_TONE_A		0x0e30
#define REG_RX_IQK_TONE_A		0x0e34
#define REG_TX_IQK_PI_A			0x0e38
#define REG_RX_IQK_PI_A			0x0e3c
#define RF6052_REG_RCK_OS		0x30	/* RF TX PA control */
#define RF6052_REG_TXPA_G1		0x31	/* RF TX PA control */
#define RF6052_REG_TXPA_G2		0x32	/* RF TX PA control */
#define RF6052_REG_TXPA_G3		0x33	/* RF TX PA control */
#define REG_IQK_AGC_PTS			0x0e48
#define REG_IQK_AGC_RSP			0x0e4c
#define REG_TX_POWER_BEFORE_IQK_A	0x0e94
#define REG_IQK_RPT_TXA			0x0e98
#define REG_TX_POWER_AFTER_IQK_A	0x0e9c
#define REG_RX_IQK			0x0e44
#define REG_TX_IQK			0x0e40
#define REG_RX_POWER_AFTER_IQK_A_2	0x0eac
#define RF6052_REG_TXM_IDAC		0x08
#define REG_FPGA0_XA_HSSI_PARM1		0x0820	/* RF 3 wire register */
#define REG_OFDM0_XA_AGC_CORE1		0x0c50
#define REG_OFDM0_XA_AGC_CORE2		0x0c54
#define REG_OFDM0_XB_AGC_CORE1		0x0c58
#define REG_OFDM0_XB_AGC_CORE2		0x0c5c
#define REG_OFDM0_XC_AGC_CORE1		0x0c60
#define REG_OFDM0_XC_AGC_CORE2		0x0c64
#define REG_OFDM0_XD_AGC_CORE1		0x0c68
#define REG_OFDM0_XD_AGC_CORE2		0x0c6c
#define  OFDM0_X_AGC_CORE1_IGI_MASK	0x0000007F
#define  FPGA0_HSSI_PARM1_PI		0x00000100
#define REG_S0S1_PATH_SWITCH		0x0948	/* 8723BU */
#define REG_OFDM0_TRX_PATH_ENABLE	0x0c04
#define REG_TX_PTCL_CTRL		0x0520
#define REG_RX_POWER_BEFORE_IQK_A_2	0x0ea4
#define REG_FPGA0_XB_HSSI_PARM1		0x0828
#define REG_OFDM0_TR_MUX_PAR		0x0c08
#define REG_TX_OFDM_RFON		0x0e7c
#define REG_CONFIG_ANT_A		0x0b68
#define REG_CONFIG_ANT_B		0x0b6c
#define REG_TX_TO_TX			0x0e88
#define REG_FPGA0_XCD_SWITCH_CTRL	0x085c
#define REG_BLUETOOTH			0x0e6c
#define REG_TX_OFDM_BBON		0x0e80
#define REG_TX_CCK_RFON			0x0e74
#define REG_TX_TO_RX			0x0e84
#define REG_TX_CCK_BBON			0x0e78
#define REG_RX_WAIT_CCA			0x0e70

void
r88f_lc_calib(struct rtwn_softc *sc)
{
	uint32_t reg;
	uint32_t rf_amode, lstf;
	int i;
	/* Check continuous TX and Packet TX */
	lstf = rtwn_bb_read(sc, REG_OFDM1_LSTF);

	if (lstf & OFDM_LSTF_MASK) {
		/* Disable all continuous TX */
		reg = lstf & ~OFDM_LSTF_MASK;
		rtwn_bb_write(sc, REG_OFDM1_LSTF, reg);
	} else {
		/* Deal with Packet TX case */
		/* block all queues */
		rtwn_write_1(sc, REG_TXPAUSE, 0xff);
	}

	/* Read original RF mode Path A */
	rf_amode = rtwn_rf_read(sc, 0, RF6052_REG_MODE_AG);

	/* Start LC calibration */
	rtwn_rf_write(sc, 0, RF6052_REG_MODE_AG, rf_amode | 0x08000);

	for (i = 0; i < 100; i++) {
		if ((rtwn_rf_read(sc, 0, RF6052_REG_MODE_AG) & 0x08000) == 0)
			break;
		rtwn_delay(sc, 10);
	}

	if (i == 100)
		device_printf(sc->sc_dev, "LC calibration timed out.\n");

	rtwn_rf_write(sc, 0, RF6052_REG_MODE_AG, rf_amode);

	/* Restore original parameters */
	if (lstf & OFDM_LSTF_MASK)
		rtwn_bb_write(sc, REG_OFDM1_LSTF, lstf);
	else /*  Deal with Packet TX case */
		rtwn_write_1(sc, REG_TXPAUSE, 0x00);
}

struct r88f_iqk_calib_regs {
	uint32_t	adda_regs[R88F_ADDA_REGS];
	uint32_t	mac_regs[R88F_MAC_REGS];
	uint32_t	bb_regs[R88F_BB_REGS];
};

struct r88f_iqk_result {
    uint32_t before;
    uint32_t after;
};

static const uint32_t iqk_adda_regs[R88F_ADDA_REGS] = {
	REG_FPGA0_XCD_SWITCH_CTRL, REG_BLUETOOTH,
	REG_RX_WAIT_CCA, REG_TX_CCK_RFON,
	REG_TX_CCK_BBON, REG_TX_OFDM_RFON,
	REG_TX_OFDM_BBON, REG_TX_TO_RX,
	REG_TX_TO_TX, REG_RX_CCK,
	REG_RX_OFDM, REG_RX_WAIT_RIFS,
	REG_RX_TO_RX, REG_STANDBY,
	REG_SLEEP, REG_PMPD_ANAEN
};
static const uint32_t iqk_mac_regs[R88F_MAC_REGS] = {
	REG_TXPAUSE, REG_BEACON_CTRL,
	REG_BEACON_CTRL_1, REG_GPIO_MUXCFG
};
static const uint32_t iqk_bb_regs[R88F_BB_REGS] = {
	REG_OFDM0_TRX_PATH_ENABLE, REG_OFDM0_TR_MUX_PAR,
	REG_FPGA0_XCD_RF_SW_CTRL, REG_CONFIG_ANT_A, REG_CONFIG_ANT_B,
	REG_FPGA0_XAB_RF_SW_CTRL, REG_FPGA0_XA_RF_INT_OE,
	REG_FPGA0_XB_RF_INT_OE, REG_FPGA0_RF_MODE
};

static void
r88f_iqk_save_regs(struct rtwn_softc *sc, struct r88f_iqk_calib_regs *backup)
{
	int i;

	for (i = 0; i < R88F_ADDA_REGS; i++)
		backup->adda_regs[i] = rtwn_bb_read(sc, iqk_adda_regs[i]);

	for (i = 0; i < R88F_MAC_REGS - 1; i++)
		backup->mac_regs[i] = rtwn_read_1(sc, iqk_mac_regs[i]);

	backup->mac_regs[i] = rtwn_bb_read(sc, iqk_mac_regs[i]);

	for (i = 0; i < R88F_BB_REGS; i++)
		backup->bb_regs[i] = rtwn_bb_read(sc, iqk_bb_regs[i]);
}

static void
r88f_iqk_restore_regs(struct rtwn_softc *sc, const struct r88f_iqk_calib_regs *backup)
{
	int i;

	for (i = 0; i < R88F_ADDA_REGS; i++)
		rtwn_bb_write(sc, iqk_adda_regs[i], backup->adda_regs[i]);

	for (i = 0; i < R88F_MAC_REGS - 1; i++)
		rtwn_write_1(sc, iqk_mac_regs[i], backup->mac_regs[i]);

	rtwn_bb_write(sc, iqk_mac_regs[i], backup->mac_regs[i]);

	for (i = 0; i < R88F_BB_REGS; i++)
		rtwn_bb_write(sc, iqk_bb_regs[i], backup->bb_regs[i]);
}

static void 
r88f_path_adda_on(struct rtwn_softc *sc, const uint32_t *regs)
{
	int i;

	rtwn_bb_write(sc, regs[0], 0x03c00014);

	for (i = 1; i < R88F_ADDA_REGS ; i++)
		rtwn_bb_write(sc, regs[i], 0x03c00014);
}

static void
r88f_fill_iqk_matrix_a(struct rtwn_softc *sc, const bool iqk_ok,
		       const struct r88f_iqk_result *tx_result,
		       const struct r88f_iqk_result *rx_result,
		       bool tx_only)
{
	uint32_t oldval, x, tx0_a, result_reg;
	int y, tx0_c;
	uint32_t reg;

	if (!iqk_ok)
		return;

	reg = rtwn_bb_read(sc, REG_OFDM0_XA_TX_IQ_IMBALANCE);
	oldval = reg >> 22;

	x = tx_result->before;
	if((x & 0x00000200) != 0)
		x = x | 0xfffffc00;
	tx0_a = (x * oldval) >> 8;

	rtwn_bb_setbits(sc, REG_OFDM0_XA_TX_IQ_IMBALANCE, 0x3ff, tx0_a);

	reg = rtwn_bb_read(sc, REG_OFDM0_ENERGY_CCA_THRES);
	reg &= ~0x80000000;
	if ((x * oldval >> 7) & 0x1)
		reg |= 0x80000000;
	rtwn_bb_write(sc, REG_OFDM0_ENERGY_CCA_THRES, reg);

	y = tx_result->after;
	if ((y & 0x00000200) != 0)
		y |= 0xfffffc00;
	tx0_c = (y * oldval) >> 8;

	rtwn_bb_setbits(sc, REG_OFDM0_XC_TX_AFE, 0xf0000000, ((tx0_c & 0x3c0) >> 6) << 28);

	rtwn_bb_setbits(sc, REG_OFDM0_XA_TX_IQ_IMBALANCE, 0x003f0000, ((tx0_c & 0x3f) << 16));

	reg = rtwn_bb_read(sc, REG_OFDM0_ENERGY_CCA_THRES);
	reg &= ~0x20000000;
	if ((y * oldval >> 7) & 0x1)
		reg |= 0x20000000;
	rtwn_bb_write(sc, REG_OFDM0_ENERGY_CCA_THRES, reg);

	if (tx_only) {
		device_printf(sc->sc_dev, "%s: only TX\n", __func__);
		return;
	}

	result_reg = rx_result->before;
	rtwn_bb_setbits(sc, REG_OFDM0_XA_RX_IQ_IMBALANCE, 0x3ff, result_reg & 0x3ff);

	result_reg = rx_result->after & 0x3f;
	rtwn_bb_setbits(sc, REG_OFDM0_XA_RX_IQ_IMBALANCE, 0xfc00, (result_reg << 10) & 0xfc00);

	result_reg = (rx_result->after >> 6) & 0xf;
	rtwn_bb_setbits(sc, REG_OFDM0_RX_IQ_EXT_ANTA, 0xf0000000, result_reg << 28);
}

static bool
r88f_iq_calib_get_tx_result(struct rtwn_softc *sc, struct r88f_iqk_result *tx_result)
{
	uint32_t reg_eac, reg_e94, reg_e9c;

	reg_eac = rtwn_bb_read(sc, REG_RX_POWER_AFTER_IQK_A_2);

	reg_e94 = rtwn_bb_read(sc, REG_TX_POWER_BEFORE_IQK_A);
	tx_result->before = (reg_e94 >> 16) & 0x3ff;

	reg_e9c = rtwn_bb_read(sc, REG_TX_POWER_AFTER_IQK_A);
	tx_result->after = (reg_e9c >> 16) & 0x3ff;

	return (!(reg_eac & 0x10000000) &&
		(tx_result->before != 0x142) && 
		(tx_result->after != 0x042));
}

static bool
r88f_iq_calib_get_rx_result(struct rtwn_softc *sc, uint32_t lok_result,
			    struct r88f_iqk_result *rx_result)
{

	uint32_t reg_eac, reg_ea4;

	/* Reload LOK value from TX calibration */
	rtwn_rf_write(sc, 0, RF6052_REG_TXM_IDAC, lok_result);

	reg_eac = rtwn_bb_read(sc, REG_RX_POWER_AFTER_IQK_A_2);
	reg_ea4 = rtwn_bb_read(sc, REG_RX_POWER_BEFORE_IQK_A_2);
	rx_result->before = (reg_ea4 >> 16) & 0x3ff;
	rx_result->after = (reg_eac >> 16) & 0x3ff;

	return (!(reg_eac & 0x08000000) &&
		(rx_result->before == 0x132) &&
		(rx_result->after == 0x036));
}

static bool
r88f_iqk_path_a_tx(struct rtwn_softc *sc, uint32_t *lok_result, struct r88f_iqk_result *tx_result)
{
	uint32_t reg;

	/* Leave IQK mode */
	reg = rtwn_bb_read(sc, REG_FPGA0_IQK);
	reg &= 0x000000ff;
	rtwn_bb_write(sc, REG_FPGA0_IQK, reg);

	/* Enable path A PA in TX IQK mode */
	rtwn_rf_setbits(sc, 0, RF6052_REG_WE_LUT, 0, 0x80000);
	rtwn_rf_write(sc, 0, RF6052_REG_RCK_OS, 0x20000);
	rtwn_rf_write(sc, 0, RF6052_REG_TXPA_G1, 0x0000f);
	rtwn_rf_write(sc, 0, RF6052_REG_TXPA_G2, 0x07ff7);

	/* PA, PAD gain adjust */
	rtwn_rf_write(sc, 0, RF6052_REG_GAIN_CCA, 0x980);
	rtwn_rf_write(sc, 0, RF6052_REG_PAD_TXG, 0x5102a);

	/* Enter IQK mode */
	reg = rtwn_bb_read(sc, REG_FPGA0_IQK);
	reg &= 0x000000ff;
	reg |= 0x80800000;
	rtwn_bb_write(sc, REG_FPGA0_IQK, reg);

	/* path-A IQK settings */
	rtwn_bb_write(sc, REG_TX_IQK_TONE_A, 0x18008c1c);
	rtwn_bb_write(sc, REG_RX_IQK_TONE_A, 0x38008c1c);

	rtwn_bb_write(sc, REG_TX_IQK_PI_A, 0x821403ff);
	rtwn_bb_write(sc, REG_RX_IQK_PI_A, 0x28160000);

	/* LO calibration setting */
	rtwn_bb_write(sc, REG_IQK_AGC_RSP, 0x00462911);

	/* One shot, path A LOK & IQK */
	rtwn_bb_write(sc, REG_IQK_AGC_PTS, 0xf9000000);
	rtwn_bb_write(sc, REG_IQK_AGC_PTS, 0xf8000000);

	rtwn_delay(sc, 25);
	
	/* Leave IQK mode */
	reg = rtwn_bb_read(sc, REG_FPGA0_IQK);
	reg &= 0x000000ff;
	rtwn_bb_write(sc, REG_FPGA0_IQK, reg);

	rtwn_rf_write(sc, 0, RF6052_REG_GAIN_CCA, 0x180);

	*lok_result = rtwn_rf_read(sc, 0, RF6052_REG_TXM_IDAC);

	return r88f_iq_calib_get_tx_result(sc, tx_result);
}

static bool
r88f_iqk_path_a_tx_rx(struct rtwn_softc *sc, uint32_t lok_result, struct r88f_iqk_result *rx_result)
{
	uint32_t reg;

	/*
	 * Leave IQK mode
	 */
	reg = rtwn_bb_read(sc, REG_FPGA0_IQK);
	reg &= 0x000000ff;
	rtwn_bb_write(sc, REG_FPGA0_IQK, reg);

	/*
	 * Enable path A PA in TX IQK mode
	 */
	rtwn_rf_setbits(sc, 0, RF6052_REG_WE_LUT, 0, 0x80000);
	rtwn_rf_write(sc, 0, RF6052_REG_RCK_OS, 0x30000);
	rtwn_rf_write(sc, 0, RF6052_REG_TXPA_G1, 0x0000f);
	rtwn_rf_write(sc, 0, RF6052_REG_TXPA_G2, 0xf1173);

	/* PA,PAD gain adjust */
	rtwn_rf_write(sc, 0, RF6052_REG_GAIN_CCA, 0x980);
	rtwn_rf_write(sc, 0, RF6052_REG_PAD_TXG, 0x5102a);


	/*
	 * Enter IQK mode
	 */
	reg = rtwn_bb_read(sc, REG_FPGA0_IQK);
	reg &= 0x000000ff;
	reg |= 0x80800000;
	rtwn_bb_write(sc, REG_FPGA0_IQK, reg);

	/*
	 * TX IQK settings
	 */
	rtwn_bb_write(sc, REG_TX_IQK, 0x01007c00);
	rtwn_bb_write(sc, REG_RX_IQK, 0x01004800);

	/* path-A IQK setting */
	rtwn_bb_write(sc, REG_TX_IQK_TONE_A, 0x10008c1c);
	rtwn_bb_write(sc, REG_RX_IQK_TONE_A, 0x30008c1c);

	rtwn_bb_write(sc, REG_TX_IQK_PI_A, 0x82160fff);
	rtwn_bb_write(sc, REG_RX_IQK_PI_A, 0x28160000);

	/* LO calibration setting */
	rtwn_bb_write(sc, REG_IQK_AGC_RSP, 0x00462911);

	/* One shot, path A LOK & IQK */
	rtwn_bb_write(sc, REG_IQK_AGC_PTS, 0xf9000000);
	rtwn_bb_write(sc, REG_IQK_AGC_PTS, 0xf8000000);

	rtwn_delay(sc, 25);

	/*
	 * Leave IQK mode
	 */
	reg = rtwn_bb_read(sc, REG_FPGA0_IQK);
	reg &= 0x000000ff;
	rtwn_bb_write(sc, REG_FPGA0_IQK, reg);

	rtwn_rf_write(sc, 0, RF6052_REG_GAIN_CCA, 0x180);

	if(!r88f_iq_calib_get_tx_result(sc, rx_result))
		return false;
	
	rtwn_bb_write(sc, REG_TX_IQK, 
	       0x80007c00 | (rx_result->before & 0x3ff0000) |
	       ((rx_result->after & 0x3ff0000) >> 16));

	/*
	 * Modify RX IQK mode table
	 */
	reg = rtwn_bb_read(sc, REG_FPGA0_IQK);
	reg &= 0x000000ff;
	rtwn_bb_write(sc, REG_FPGA0_IQK, reg);

	rtwn_rf_setbits(sc, 0, RF6052_REG_WE_LUT, 0, 0x80000);
	rtwn_rf_write(sc, 0, RF6052_REG_RCK_OS, 0x30000);
	rtwn_rf_write(sc, 0, RF6052_REG_TXPA_G1, 0x0000f);
	rtwn_rf_write(sc, 0, RF6052_REG_TXPA_G2, 0xf7ff2);

	/*
	 * PA, PAD setting
	 */
	rtwn_rf_write(sc, 0, RF6052_REG_GAIN_CCA, 0x980);
	rtwn_rf_write(sc, 0, RF6052_REG_PAD_TXG, 0x51000);
	/*
	 * Enter IQK mode
	 */
	reg = rtwn_bb_read(sc, REG_FPGA0_IQK);
	reg &= 0x000000ff;
	reg |= 0x80800000;
	rtwn_bb_write(sc, REG_FPGA0_IQK, reg);

	/*
	 * RX IQK setting
	 */
	rtwn_bb_write(sc, REG_RX_IQK, 0x01004800);

	/* path-A IQK setting */
	rtwn_bb_write(sc, REG_TX_IQK_TONE_A, 0x30008c1c);
	rtwn_bb_write(sc, REG_RX_IQK_TONE_A, 0x10008c1c);

	rtwn_bb_write(sc, REG_TX_IQK_PI_A, 0x82160000);
	rtwn_bb_write(sc, REG_RX_IQK_PI_A, 0x281613ff);

	/* LO calibration setting */
	rtwn_bb_write(sc, REG_IQK_AGC_RSP, 0x0046a911);

	/* One shot, path A LOK & IQK */
	rtwn_bb_write(sc, REG_IQK_AGC_PTS, 0xf9000000);
	rtwn_bb_write(sc, REG_IQK_AGC_PTS, 0xf8000000);

	rtwn_delay(sc, 25);

	/*
	 * Leave IQK Mode
	 */
	reg = rtwn_bb_read(sc, REG_FPGA0_IQK);
	reg &= 0x000000ff;
	rtwn_bb_write(sc, REG_FPGA0_IQK, reg);

	rtwn_rf_write(sc, 0, RF6052_REG_GAIN_CCA, 0x180);

	return r88f_iq_calib_get_rx_result(sc, lok_result, rx_result);

}

#define MAX_TOLERANCE 5

static bool
r88f_iq_calib_compare_results(struct r88f_iqk_result *tx_result1,
				struct r88f_iqk_result *rx_result1,
				struct r88f_iqk_result *tx_result2,
				struct r88f_iqk_result *rx_result2,
				struct r88f_iqk_result **tx_candidate,
				struct r88f_iqk_result **rx_candidate)
{
	uint32_t diff;
	bool tx_good = false;
	int32_t tmp1, tmp2;

	diff = abs((int32_t)tx_result1->before - (int32_t)tx_result2->before);
	if (diff <= MAX_TOLERANCE) {
		tmp1 = tx_result1->after & 0x00000200 ? (tx_result1->after | 0xfffffc00) : tx_result1->after;
		tmp2 = tx_result2->after & 0x00000200 ? (tx_result2->after | 0xfffffc00) : tx_result2->after;
		diff = abs(tmp1 - tmp2);

		if(diff <= MAX_TOLERANCE) {
			*tx_candidate = tx_result1;
			tx_good = true;
		}
	}

	diff = abs((int32_t)rx_result1->before - (int32_t)rx_result2->before);
	if(diff > MAX_TOLERANCE) {
		if (tx_good && (rx_result1->before + rx_result1->after) == 0) 
			*tx_candidate = tx_result2;
		
		return false;
	}

	tmp1 = (rx_result1->after & 0x200) ? (0xfffffc00 | rx_result1->after) : rx_result1->after;
	tmp2 = (rx_result2->after & 0x200) ? (0xfffffc00 | rx_result2->after) : rx_result2->after;
	diff = abs(tmp1 - tmp2);

	if (diff > MAX_TOLERANCE)
		return false;

	*rx_candidate = rx_result1;

	return tx_good;
}

static void
r88f_iq_calib_run(struct rtwn_softc *sc, struct r88f_iqk_calib_regs *regs,
                  struct r88f_iqk_result *tx_result,
		  struct r88f_iqk_result *rx_result, int t)
{
	uint32_t rx_initial_gain, lok_result, hssi_param1;
	uint32_t path_sel_rf, path_sel_bb;
	uint32_t reg;
	int retry = 2;
	int i;
	bool path_a_ok;

	/*
	 * Note: IQ calibration must be performed after loading
	 *       PHY_REG.txt , and radio_a, radio_b.txt
	 */

	rx_initial_gain = rtwn_bb_read(sc, REG_OFDM0_XA_AGC_CORE1);

	if (t == 0) {
		r88f_iqk_save_regs(sc, regs);
	}

	r88f_path_adda_on(sc, regs->adda_regs);

	if (t == 0) {
		/* Save ADDA parameters, turn Path A ADDA on */
		reg = rtwn_bb_read(sc, REG_FPGA0_XA_HSSI_PARM1);
		hssi_param1 = rtwn_bb_read(sc, R92C_HSSI_PARAM1(0));
	}

	/* save RF path */
	path_sel_bb = rtwn_bb_read(sc, REG_S0S1_PATH_SWITCH);
	path_sel_rf = rtwn_rf_read(sc, 0, RF6052_REG_S0S1);

	/* BB setting */
	rtwn_bb_write(sc, REG_OFDM0_TRX_PATH_ENABLE, 0x03a05600);
	rtwn_bb_write(sc, REG_OFDM0_TR_MUX_PAR, 0x000800e4);
	rtwn_bb_write(sc, REG_FPGA0_XCD_RF_SW_CTRL, 0x25204000);

	rtwn_bb_setbits(sc, REG_TX_PTCL_CTRL, 0, 0x00ff0000);

	reg = rtwn_bb_read(sc, REG_FPGA0_IQK);
	reg &= 0xff;
	reg |= 0x80800000;
	rtwn_bb_write(sc, REG_FPGA0_IQK, reg);
	rtwn_bb_write(sc, REG_TX_IQK, 0x01007c00);
	rtwn_bb_write(sc, REG_RX_IQK, 0x01004800);

	for (i = 0; i < retry; i++) {
		path_a_ok = r88f_iqk_path_a_tx(sc, &lok_result, tx_result);
		if(path_a_ok) {
			rtwn_bb_write(sc, REG_FPGA0_IQK, 0x00);
			break;
		}
	}

	for (i = 0; i < retry; i++) {
		path_a_ok = r88f_iqk_path_a_tx_rx(sc, lok_result, tx_result);
		if(path_a_ok) {
			rtwn_bb_write(sc, REG_FPGA0_IQK, 0x00);
			break;
		}
	}

	if (path_a_ok)
		device_printf(sc->sc_dev, "%s: Path A IQK failed!\n", __func__);

	/* Back to BB mode, load original value */
	reg = rtwn_bb_read(sc, REG_FPGA0_IQK);
	reg &= 0xff;
	rtwn_bb_write(sc, REG_FPGA0_IQK, reg);

	if (t == 0)
		return;

	if (!(hssi_param1 & R92C_HSSI_PARAM1_PI)) {
		/*
		 * Switch back BB to SI mode after finishing
		 * IQ Calibration
		 */
		rtwn_bb_write(sc, REG_FPGA0_XA_HSSI_PARM1, 0x01000000);
		rtwn_bb_write(sc, REG_FPGA0_XB_HSSI_PARM1, 0x01000000);
	}

	/* Reload ADDA power saving, MAC and BB paramters */
	r88f_iqk_restore_regs(sc, regs);

	/* Reload RF path */
	rtwn_bb_write(sc, REG_S0S1_PATH_SWITCH, path_sel_bb);
	rtwn_rf_write(sc, 0, RF6052_REG_S0S1, path_sel_rf);


	/* Restore RX initial gain */
	reg = rtwn_bb_read(sc, REG_OFDM0_XA_AGC_CORE1);
	reg &= 0xffffff00;
	reg |= 0x50;
	rtwn_bb_write(sc, REG_OFDM0_XA_AGC_CORE1, reg);
	reg = rtwn_bb_read(sc, REG_OFDM0_XA_AGC_CORE1);
	reg &= 0xffffff00;
	reg |= rx_initial_gain & 0xff;
	rtwn_bb_write(sc, REG_OFDM0_XA_AGC_CORE1, reg);

	/* Load 0xe30 IQC default value */
	rtwn_bb_write(sc, REG_TX_IQK_TONE_A, 0x01008c00);
	rtwn_bb_write(sc, REG_RX_IQK_TONE_A, 0x01008c00);
}

void
r88f_iq_calib(struct rtwn_softc* sc)
{
	struct r88f_iqk_calib_regs regs;
	struct r88f_iqk_result tx_result[3] = { {0, 0} };
	struct r88f_iqk_result rx_result[3] = { {0, 0} };;
	/* Final results */
	struct r88f_iqk_result *tx_candidate = NULL;
	struct r88f_iqk_result *rx_candidate = NULL;
	uint32_t path_sel_bb, path_sel_rf;
	int i;

	/* Save RF path */
	path_sel_bb = rtwn_bb_read(sc, REG_S0S1_PATH_SWITCH);
	path_sel_rf = rtwn_rf_read(sc, 0, RF6052_REG_S0S1);

	for (i = 0; i < 3; i++) {
		r88f_iq_calib_run(sc, &regs, &tx_result[i], &rx_result[i], i);

		if (i == 1) {
			/* Compare 0 vs 1 */
			if(r88f_iq_calib_compare_results(&tx_result[0], &tx_result[1],
				    &rx_result[0], &rx_result[1], 
				    &tx_candidate, &rx_candidate)) {
				device_printf(sc->sc_dev, "[SELF DEBUG] %s: Run 0-1 passed\n", __func__);
				goto write;
			}
		}

		if (i == 2) {
			/* Compare 0 vs 2 */
			if(r88f_iq_calib_compare_results(&tx_result[0], &tx_result[2],
				    &rx_result[0], &rx_result[2], 
				    &tx_candidate, &rx_candidate)) {
				device_printf(sc->sc_dev, "[SELF DEBUG] %s: Run 0-2 passed\n", __func__);
				goto write;
			}

			/* Compare 1 vs 2 */
			if(r88f_iq_calib_compare_results(&tx_result[1], &tx_result[2],
				    &rx_result[1], &rx_result[2], 
				    &tx_candidate, &rx_candidate)) {
				device_printf(sc->sc_dev, "[SELF DEBUG] %s: Run 1-2 passed\n", __func__);
			} else {
				if(tx_candidate == NULL) 
					tx_candidate = &tx_result[0];
				if(rx_candidate == NULL) 
					rx_candidate = &rx_result[0];

				/* check complete failure */
				if(tx_candidate->before == 0 && tx_candidate->after == 0 && rx_candidate->before == 0 && rx_candidate->after == 0) {
					device_printf(sc->sc_dev, "[SELF DEBUG] %s: IQK failed completely\n", __func__);
					goto done;
				}

				device_printf(sc->sc_dev, "[SELF DEBUG] %s: using fallback candidate\n", __func__);
			}
		}
	}

write:
	if(tx_candidate == NULL || (tx_candidate->before == 0 && tx_candidate->after == 0)) {
		device_printf(sc->sc_dev, "[SELF DEBUG] %s: TX IQK failed completely\n", __func__);
		goto done;
	}

	device_printf(sc->sc_dev, "%s: IQK candidate - TX: before=%#x after=%#x RX: before=%#x after=%#x\n",
		__func__,
		tx_candidate->before, tx_candidate->after,
		rx_candidate ? rx_candidate->before : 0,
		rx_candidate ? rx_candidate->after : 0);

        r88f_fill_iqk_matrix_a(sc, true, tx_candidate, rx_candidate ? rx_candidate : NULL,
                              (rx_candidate == NULL || rx_candidate->before == 0));

done:
    r88f_iqk_save_regs(sc, &regs);

    /* Restore RF path */
    rtwn_bb_write(sc, REG_S0S1_PATH_SWITCH, path_sel_bb);
    rtwn_rf_write(sc, 0, RF6052_REG_S0S1, path_sel_rf);
}
