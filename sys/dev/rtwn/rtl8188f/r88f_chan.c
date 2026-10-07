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
#include <dev/rtwn/if_rtwn_ridx.h>

#include <dev/rtwn/rtl8192c/r92c.h>
#include <dev/rtwn/rtl8192c/r92c_var.h>

#include <dev/rtwn/rtl8188e/r88e.h>
#include <dev/rtwn/rtl8188e/r88e_priv.h>
#include <dev/rtwn/rtl8188e/r88e_reg.h>

#include <dev/rtwn/rtl8188f/r88f.h>
#include <dev/rtwn/rtl8188f/r88f_reg.h>

#define RTWN_POWER_COUNT 28
#define RTWN_RIDX_MCS0	12
#define RTWN_RIDX_MCS8	(RTWN_RIDX_MCS0 + 8)

// TODO: defs from linux, rename and move to thier appropriate header files
#define REG_TX_AGC_A_CCK1_MCS32	0xe08
#define REG_OFDM0_RX_D_SYNC_PATH	0x0c40
#define REG_FPGA0_XB_RF_INT_OE		0x0864
#define R88F_OFDM0_RX_D_SYNC_PATH 0xc40
#define R88F_SOS1_PATH_SWITCH 0x948
#define REG_FPGA0_ANALOG4	0x088c
#define REG_OFDM0_XA_AGC_CORE1	0x0c50
#define FPGA_RF_MODE_CCK	0x01000000
#define REG_FPGA0_PSD_FUNC	0x0808
#define REG_FPGA0_PSD_REPORT	0x08b4
#define REG_FPGA0_RF_MODE	0x0800
#define REG_OFDM1_CSI_FIX_MASK1		0x0d40
#define REG_OFDM1_CSI_FIX_MASK2		0x0d44
#define REG_OFDM1_CFO_TRACKING		0x0d2c
#define REG_RESPONSE_RATE_SET		0x0440
#define  RSR_RSC_LOWER_SUB_CHANNEL	0x200000
#define  RSR_RSC_UPPER_SUB_CHANNEL	0x400000
#define  RSR_RSC_BANDWIDTH_40M		(RSR_RSC_UPPER_SUB_CHANNEL | \
					 RSR_RSC_LOWER_SUB_CHANNEL)
#define  MODE_AG_BW_20MHZ_8723B		(0x00000400 | 0x00000800)
#define  MODE_AG_BW_40MHZ_8723B		0x00000400
#define  MODE_AG_BW_80MHZ_8723B		0
/* 8723bu */ #define REG_DATA_SUBCHANNEL	0x0483
#define REG_CCK0_SYSTEM			0x0a00
#define  CCK0_SIDEBAND	 0x00000010
#define R92C_OFDM0_TX_PSDO_NOISE_WEIGHT 0x0ce4
#define R92C_OFDM0_XA_RX_AFE		0x0c10
#define MODE_AG_CHANNEL_MASK 0x3ff


static int
rtwn_chan2group(int chan)
{
	int group;

	if (chan <= 2)
		group = 0;
	else if (chan <= 5)
		group = 1;
	else if (chan <= 8)
		group = 2;
	else if (chan <= 11)
		group = 3;
	else
		group = 4;

	return (group);
}


void
r88f_get_txpower(struct rtwn_softc *sc, int chain,
    struct ieee80211_channel *c, uint8_t power[RTWN_RIDX_COUNT])
{
	struct r92c_softc *rs = (struct r92c_softc *)&sc->sc_priv;
	const struct rtwn_r88e_txpwr *rt = (const struct rtwn_r88e_txpwr *)&rs->rs_txpwr;
	uint32_t reg;
	uint8_t cckpow, htpow, ofdmpow;
	int8_t diff;
	int ridx, chan, group;

	/* Determine channel group. */
	chan = rtwn_chan2centieee(c);	/* XXX center freq! */
	group = rtwn_chan2group(chan);

	memset(power, 0, RTWN_POWER_COUNT * sizeof(power[0]));

	/* Compute per-CCK rate Tx power. */
	cckpow = rt->cck_tx_pwr[group];
	for (ridx = RTWN_RIDX_CCK1; ridx <= RTWN_RIDX_CCK11; ridx++) {
		power[ridx] = cckpow;
		if (power[ridx] > R92C_MAX_TX_PWR)
			power[ridx] = R92C_MAX_TX_PWR;
	}

	reg = rtwn_read_4(sc, R92C_TXAGC_A_CCK1_MCS32);
	reg &= 0xffff00ff;
	reg |= (cckpow << 8);
	rtwn_write_4(sc, R92C_TXAGC_A_CCK1_MCS32, reg);

	reg = rtwn_read_4(sc, R92C_TXAGC_B_CCK11_A_CCK2_11);
	reg &= 0xff;
	reg |= ((cckpow << 8) | (cckpow << 16) | (cckpow << 24));
	rtwn_write_4(sc, R92C_TXAGC_B_CCK11_A_CCK2_11, reg);

	htpow = rt->ht40_tx_pwr[group];

	/* Compute per-OFDM rate Tx power. */
	diff = rt->ofdm_tx_pwr_diff;
	ofdmpow = htpow + diff;
	for (ridx = RTWN_RIDX_OFDM6; ridx <= RTWN_RIDX_OFDM54; ridx++) {
		power[ridx] = ofdmpow;
		if (power[ridx] > R92C_MAX_TX_PWR)
			power[ridx] = R92C_MAX_TX_PWR;
	}

	/* Compute per-MCS Tx power. */
	diff = rt->bw20_tx_pwr_diff;
	htpow += diff;
	for (ridx = RTWN_RIDX_MCS0; ridx < RTWN_RIDX_MCS8; ridx++) {
		power[ridx] = htpow;
		if (power[ridx] > R92C_MAX_TX_PWR)
			power[ridx] = R92C_MAX_TX_PWR;
	}
}

static void
r88f_set_txpower(struct rtwn_softc *sc, struct ieee80211_channel *c)
{
	uint8_t power[RTWN_RIDX_COUNT];

	memset(power, 0, sizeof(power));
	/* Compute per-rate Tx power values. */
	r88f_get_txpower(sc, 0, c, power);
	/* Write per-rate Tx power values to hardware. */
	r92c_write_txpower(sc, 0, power);
}

int
r88f_set_tx_power(struct rtwn_softc *sc, struct ieee80211vap *vap)
{
	if (vap->iv_bss == NULL)
		return (EINVAL);
	if (vap->iv_bss->ni_chan == IEEE80211_CHAN_ANYC)
		return (EINVAL);

	/* Set it for the current channel */
	r88f_set_txpower(sc, vap->iv_bss->ni_chan);

	return (0);
}


static void
r88f_spur_calibration(struct rtwn_softc* sc, struct ieee80211_channel *c, int chan)
{
	static const uint32_t frequencies[14 + 1] = {
		[5] = 0xFCCD,
		[6] = 0xFC4D,
		[7] = 0xFFCD,
		[8] = 0xFF4D,
		[11] = 0xFDCD,
		[13] = 0xFCCD,
		[14] = 0xFF9A
	};

	static const uint32_t reg_d40[14 + 1] = {
		[5] = 0x06000000,
		[6] = 0x00000600,
		[13] = 0x06000000
	};

	static const uint32_t reg_d44[14 + 1] = {
		[11] = 0x04000000
	};

	static const uint32_t reg_d4c[14 + 1] = {
		[7] = 0x06000000,
		[8] = 0x00000380,
		[14] = 0x00180000
	};

	const uint8_t threshold = 0x16;
	bool do_notch, hw_ctrl, sw_ctrl, hw_ctrl_s1 = 0, sw_ctrl_s1 = 0;
	uint32_t reg, initial_gain, reg948;

	rtwn_bb_setbits(sc, R88F_OFDM0_RX_D_SYNC_PATH, 0,/* GENMASK(28, 24) = */ 0x1f000000);

	/* enable notch filter */
	rtwn_bb_setbits(sc, REG_OFDM0_RX_D_SYNC_PATH, 0, /* BIT(9) = */ 0x00000200);

	if (chan <= 14 && frequencies[chan] > 0)
	{
		reg948 = rtwn_read_4(sc, R88F_SOS1_PATH_SWITCH);
		hw_ctrl = reg948 & /* BIT(6) = */ 0x00000040;
		sw_ctrl = !hw_ctrl;
		if(hw_ctrl)
		{
			reg = rtwn_read_4(sc, REG_FPGA0_XB_RF_INT_OE);
			reg &= /* GENMASK(5, 3) = */ 0x00000038;
			hw_ctrl_s1 = reg == /* BIT(3) = */ 0x00000008;
		} else if(sw_ctrl) {
			sw_ctrl_s1 = !(reg948 & /* BIT(9) = */ 0x00000200);
		}

		if (hw_ctrl_s1 || sw_ctrl_s1) 
		{
			initial_gain = rtwn_read_4(sc, REG_OFDM0_XA_AGC_CORE1);

			/* Disable CCK block */
			rtwn_bb_setbits(sc, R92C_FPGA0_RFMOD, FPGA_RF_MODE_CCK, 0);

			rtwn_bb_setbits(sc, R92C_OFDM0_AGCCORE1(0), R92C_OFDM0_AGCCORE1_GAIN_M, 0x30);

			/* disable 3-wire */
			rtwn_bb_write(sc, REG_FPGA0_ANALOG4, 0xccf000c0);

			
			/* Setup PSD */
			rtwn_bb_write(sc, REG_FPGA0_PSD_FUNC, frequencies[chan]);

			/* Start PSD */
			rtwn_bb_write(sc, REG_FPGA0_PSD_FUNC, 0x400000 | frequencies[chan]);

			rtwn_delay(sc, 30);

			do_notch = rtwn_bb_read(sc, REG_FPGA0_PSD_REPORT) >= threshold;

			/* turn off PSD */
			rtwn_bb_write(sc, REG_FPGA0_PSD_FUNC, frequencies[chan]);

			/* enable 3-wire */
			rtwn_bb_write(sc, REG_FPGA0_ANALOG4, 0xccc000c0);

			/* Enable CCK block */
			rtwn_bb_setbits(sc, REG_FPGA0_RF_MODE, 0, FPGA_RF_MODE_CCK);

			rtwn_bb_write(sc, R92C_OFDM0_AGCCORE1(0), initial_gain);
			
			if(do_notch)
			{
				rtwn_bb_write(sc, REG_OFDM1_CSI_FIX_MASK1, reg_d40[chan]);
				rtwn_bb_write(sc, REG_OFDM1_CSI_FIX_MASK2, reg_d44[chan]);

				rtwn_bb_write(sc, 0xd48, 0x0);
				rtwn_bb_write(sc, 0xd4c, reg_d4c[chan]);

				/* enable CSI mask */
				rtwn_bb_setbits(sc, REG_OFDM1_CFO_TRACKING, 0, /* BIT(28) = */ 0x10000000);

				return;
			}
		}
	}

	/* disable CSI mask function */
	rtwn_bb_setbits(sc, REG_OFDM1_CFO_TRACKING, /* BIT(28) = */ 0x10000000, 0);
}
	

void
r88f_set_chan(struct rtwn_softc *sc, struct ieee80211_channel *c)
{
	struct r92c_softc *rs = sc->sc_priv;
	u_int chan;
	uint32_t reg;

	chan = rtwn_chan2centieee(c);	/* XXX center freq! */

	/* Set Tx power for this new channel. */
	r88f_set_txpower(sc, c);

	r88f_spur_calibration(sc, c, chan);


	rtwn_bb_setbits(sc, R92C_FPGA0_RFMOD, R92C_RFMOD_40MHZ, 0);
	rtwn_bb_setbits(sc, R92C_FPGA1_RFMOD, R92C_RFMOD_40MHZ, 0);


	rtwn_bb_setbits(sc, R92C_FPGA0_RFMOD, 0x00000700, 0x7 << 8);
	rtwn_bb_setbits(sc, R92C_FPGA0_RFMOD, 0x00007000, 0x5 << 12);

	reg = rtwn_bb_read(sc, R92C_OFDM0_TX_PSDO_NOISE_WEIGHT);
	reg &= ~0xc0000000;
	rtwn_bb_write(sc, R92C_OFDM0_TX_PSDO_NOISE_WEIGHT, reg);

	/* Small bandwidth */
	rtwn_bb_setbits(sc, R92C_OFDM0_TX_PSDO_NOISE_WEIGHT, 0, 0x30000000);
	/* ADC buffer clk */
	rtwn_bb_setbits(sc, R92C_OFDM0_XA_RX_AFE, 0, 0x30000000);

	/* OFDM Rx DFIR */
	rtwn_bb_setbits(sc, R88F_RX_DFIR, 0x00080000, 0);
	rtwn_bb_setbits(sc, R88F_RX_DFIR, 0x00f00000, 0x3 << 15);

	/* Select 20MHz bandwidth. */
	rtwn_rf_write(sc, 0, R92C_RF_CHNLBW,
	    (rs->rf_chnlbw[0] & ~0xfff) | chan | R88E_RF_CHNLBW_BW20);

	rtwn_rf_write(sc, 0, 0x87, 0x65);
	rtwn_rf_write(sc, 0, 0x1c, 0);
	rtwn_rf_write(sc, 0, 0xdf, 0x0140);
	rtwn_rf_write(sc, 0, 0x1b, 0x1c6c);
}

void
r88f_set_bw20(struct rtwn_softc *sc, uint8_t chan)
{
	struct r92c_softc *rs = sc->sc_priv;

	rtwn_bb_setbits(sc, R92C_FPGA0_RFMOD, R92C_RFMOD_40MHZ, 0);
	rtwn_bb_setbits(sc, R92C_FPGA1_RFMOD, R92C_RFMOD_40MHZ, 0);

	/* Select 20MHz bandwidth. */
	rtwn_rf_write(sc, 0, R92C_RF_CHNLBW,
	    (rs->rf_chnlbw[0] & ~0xfff) | chan | R88E_RF_CHNLBW_BW20);
	rtwn_rf_write(sc, 0, R88F_RF_RXG_GAIN, R88F_RF_TXA_PREPAD);
	rtwn_rf_write(sc, 0, R92C_RF_RX_BB2, 0);
	rtwn_rf_write(sc, 0, R88F_RF6052_REG_GAIN_CCA, R88F_REG_PKTBUF_DBG_CTRL);
	rtwn_rf_write(sc, 0, R92C_RF_RX_G2, 0x1c6c);
}
