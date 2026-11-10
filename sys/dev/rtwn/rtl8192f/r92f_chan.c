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
#include <dev/rtwn/rtl8812a/r12a_reg.h>

#include <dev/rtwn/rtl8188f/r88f.h>

static void
r92f_config_kfree(struct rtwn_softc, uint8_t chan)
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
		rtwn_rf_setbits(sc, rfpath, RF6052_REG_UNKNOWN_55, 0x20, 0x20);

		/* enable 55[14] for 0.5db step */
		rtwn_rf_setbits(sc, rfpath, RF6052_REG_GAIN_CTRL, 0x40000,
		    0x40000);

		/* enter power_trim debug mode */
		rtwn_rf_setbits(sc, rfpath, RF6052_REG_GAIN_CCA, 0x80, 0x80);

		/* write enable */
		rtwn_rf_setbits(sc, rfpath, RF6052_REG_WE_LUT, 0x80, 0x80);


		bb_gain_for_path = (bb_gain[channel_idx] &
		    bb_gain_path_mask[rfpath]) >> bb_gain_path_shift[rfpath];

		rtl8xxxu_write_rfreg_mask(priv, rfpath, RF6052_REG_TXPA_G3,
					  0x70000, channel_idx * 2);
		rtl8xxxu_write_rfreg_mask(priv, rfpath, RF6052_REG_TXPA_G3,
					  0x3f, bb_gain_for_path);

		rtl8xxxu_write_rfreg_mask(priv, rfpath, RF6052_REG_TXPA_G3,
					  0x70000, channel_idx * 2 + 1);
		rtl8xxxu_write_rfreg_mask(priv, rfpath, RF6052_REG_TXPA_G3,
					  0x3f, bb_gain_for_path);

		/* leave power_trim debug mode */
		rtl8xxxu_write_rfreg_mask(priv, rfpath, RF6052_REG_GAIN_CCA,
					  BIT(7), 0);

		/* write disable */
		rtl8xxxu_write_rfreg_mask(priv, rfpath, RF6052_REG_WE_LUT, BIT(7), 0);
	}
}

static void
r92f_revise_cck_tx_psf(struct rtwn_softc *sc, uint8_t channel)
{
	if (channel == 13) {
		/* Special value for channel 13 */
		rtwn_bb_write(sc, REG_CCK0_TX_FILTER1, 0xf8fe0001);
		/* Normal values */
		rtwn_bb_write(sc, REG_CCK0_TX_FILTER2, 0x64B80C1C);
		rtwn_write_2(sc, REG_CCK0_DEBUG_PORT, 0x8810);
		rtwn_bb_write(sc, REG_CCK0_TX_FILTER3, 0x01235667);
	} else if (channel == 14) {
		/* Normal value */
		rtwn_bb_write(sc, REG_CCK0_TX_FILTER1, 0xE82C0001);
		/* Special values for channel 14 */
		rtwn_bb_write(sc, REG_CCK0_TX_FILTER2, 0x0000B81C);
		rtwn_write_2(sc, REG_CCK0_DEBUG_PORT, 0x0000);
		rtwn_bb_write(sc, REG_CCK0_TX_FILTER3, 0x00003667);
	} else {
		/* Restore normal values from the phy init table */
		rtwn_bb_write(sc, REG_CCK0_TX_FILTER1, 0xE82C0001);
		rtwn_bb_write(sc, REG_CCK0_TX_FILTER2, 0x64B80C1C);
		rtwn_write_2(sc, REG_CCK0_DEBUG_PORT, 0x8810);
		rtwn_bb_write(sc, REG_CCK0_TX_FILTER3, 0x01235667);
	}
}

void
r88f_set_chan(struct rtwn_softc *sc, struct ieee80211_channel *c)
{
	bool ht40 = IEEE80211_IS_CHAN_HT40(c);
	uint8_t chan, subchan = 0;
	bool sec_ch_above = 0;
	uint32_t reg;

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
	reg = rtwn_rf_read(sc, 0, RF6052_REG_MODE_AG);
	reg &= ~(0x60000); /* select the 2.4G band(?) */
	reg = (reg & ~0xff) | (channel & 0xff);
	rtwn_rf_write(sc, 0, RF6052_REG_MODE_AG, reg);
	rtwn_r_write(sc, 1, RF6052_REG_MODE_AG, reg);

	r92f_config_kfree(sc, chan);

	rtl8xxxu_write8(priv, REG_DATA_SUBCHANNEL, subchannel);

	/* small BW */
	rtl8xxxu_write32_clear(priv, REG_OFDM0_TX_PSDO_NOISE_WEIGHT, GENMASK(31, 30));

	rtl8xxxu_write32_mask(priv, REG_FPGA0_RF_MODE, FPGA_RF_MODE, ht40);
	rtl8xxxu_write32_mask(priv, REG_FPGA1_RF_MODE, FPGA_RF_MODE, ht40);

	/* ADC clock = 160M */
	rtl8xxxu_write32_mask(priv, REG_FPGA0_RF_MODE, GENMASK(10, 8), 4);

	/* DAC clock = 80M */
	rtl8xxxu_write32_mask(priv, REG_FPGA0_RF_MODE, BIT(13) | BIT(12), 2);

	/* ADC buffer clk */
	rtl8xxxu_write32_mask(priv, REG_ANTDIV_PARA1, BIT(27) | BIT(26), 2);

	if (ht40)
		/* Set Control channel to upper or lower. */
		rtl8xxxu_write32_mask(priv, REG_CCK0_SYSTEM,
				      CCK0_SIDEBAND, !sec_ch_above);

	/* Enable CCK */
	rtl8xxxu_write32_set(priv, REG_FPGA0_RF_MODE, FPGA_RF_MODE_CCK);

	/* RF TRX_BW */
	val32 = rtl8xxxu_read_rfreg(priv, RF_A, RF6052_REG_MODE_AG);
	val32 &= ~MODE_AG_BW_MASK;
	if (ht40)
		val32 |= MODE_AG_BW_40MHZ_8723B;
	else
		val32 |= MODE_AG_BW_20MHZ_8723B;
	rtl8xxxu_write_rfreg(priv, RF_A, RF6052_REG_MODE_AG, val32);
	if (priv->rf_paths > 1)
		rtl8xxxu_write_rfreg(priv, RF_B, RF6052_REG_MODE_AG, val32);

	/* Modify RX DFIR parameters */
	rtl8xxxu_write32_mask(priv, REG_TAP_UPD_97F, BIT(21) | BIT(20), 2);

	rtl8xxxu_write32_mask(priv, REG_DOWNSAM_FACTOR, BIT(29) | BIT(28), 2);

	if (ht40)
		val32 = 0x3;
	else
		val32 = 0x1a3;
	rtl8xxxu_write32_mask(priv, REG_RX_DFIR_MOD_97F, 0x1ff, val32);
}
