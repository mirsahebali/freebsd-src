/*	$OpenBSD: if_urtwn.c,v 1.16 2011/02/10 17:26:40 jakemsr Exp $	*/

/*-
 * Copyright (c) 2010 Damien Bergamini <damien.bergamini@free.fr>
 * Copyright (c) 2014 Kevin Lo <kevlo@FreeBSD.org>
 * Copyright (c) 2015-2016 Andriy Voskoboinyk <avos@FreeBSD.org>
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
#include <dev/rtwn/usb/rtwn_usb_var.h>

#include <dev/rtwn/rtl8192c/r92c.h>
#include <dev/rtwn/rtl8192c/usb/r92cu.h>
#include <dev/rtwn/rtl8192c/r92c_var.h>

#include <dev/rtwn/rtl8192e/r92e_reg.h>

#include <dev/rtwn/rtl8188e/usb/r88eu.h>
#include <dev/rtwn/rtl8188e/usb/r88eu_reg.h>

#include <dev/rtwn/rtl8188f/usb/r88fu.h>


// TODO: move this .h and rename defs
#define R88F_DWBCN0_CTRL		0x208
#define REG_SYS_FUNC			0x0002
#define REG_NHM_TH9_TH10_8723B		0x0890
#define REG_NHM_TIMER_8723B		0x0894
#define REG_NHM_TH3_TO_TH0_8723B	0x0898
#define REG_NHM_TH7_TO_TH4_8723B	0x089c
#define SYS_FUNC_BBRSTB			0x00000001
#define SYS_FUNC_BB_GLB_RSTN		0x00000002
#define SYS_FUNC_USBA			0x00000004
#define SYS_FUNC_UPLL			0x00000008
#define SYS_FUNC_USBD			0x00000010
#define REG_OFDM0_FA_RSTC		0x0c0c
#define RF6052_REG_IQADJ_G1		0x01
#define REG_FPGA0_IQK			0x0e28
#define REG_RF_CTRL			0x001f
#define RF_ENABLE			0x00000001
#define RF_RSTB				0x00000002
#define RF_SDMRSTB			0x00000004
#define SYS_FUNC_DIO_RF			0x00002000
#define R92E_RXDMA_PRO			0x290
#define R92E_RXDMA_PRO_DMA_MODE		0x02
#define R92E_DWBCN1_CTRL		0x228
#define R92C_AFE_LDO_CTRL		0x027
#define REG_RXPKT_NUM			0x0284
#define RXPKT_NUM_RW_RELEASE_EN		0x00040000
#define REG_RQPN_NPQ			0x0214
#define REG_RQPN			0x0200
#define RXPKT_NUM_RXDMA_IDLE		0x00020000

void
r88fu_init_tx_agg(struct rtwn_softc *sc)
{
	uint8_t agg_ctrl, rxdma_mode, usb_tx_agg_desc_num = 6;
	uint32_t agg_rx;


	/* TX aggregation */
	rtwn_setbits_4(sc, R88F_DWBCN0_CTRL, 0xf << 4, usb_tx_agg_desc_num << 4);
	rtwn_write_1(sc, R92E_DWBCN1_CTRL, usb_tx_agg_desc_num << 1);

	/* RX aggegation */
	agg_ctrl = rtwn_read_1(sc, R92C_TRXDMA_CTRL);
	agg_ctrl &= ~/* BIT(2) = */  0x00000004;

	agg_rx = rtwn_read_1(sc, R92C_RXDMA_AGG_PG_TH);
	agg_rx &= /* ~BIT(31) = */ ~0x80000000;
	agg_rx &= ~0xff0f; /* reset agg size and timeout */

	rxdma_mode = rtwn_read_1(sc, R92E_RXDMA_PRO);
	rxdma_mode &= /* ~BIT(1) = */ ~0x00000002;

	rtwn_write_1(sc, R92C_TRXDMA_CTRL, agg_ctrl);
	rtwn_write_4(sc, R92C_RXDMA_AGG_PG_TH, agg_rx);
	rtwn_write_1(sc, R92E_RXDMA_PRO, rxdma_mode);
}

int
r88fu_power_on(struct rtwn_softc *sc)
{
#define RTWN_CHK(res) do {	\
	if (res != 0)		\
		return (EIO);	\
} while(0)
	int ntries;

	/* Enable WL suspend. */
	RTWN_CHK(rtwn_setbits_2(sc, R92C_APS_FSMCO,
	    R92C_APS_FSMCO_AFSM_HSUS | R92C_APS_FSMCO_AFSM_PCIE, 0));

	/* Turn off USB APHY LDO under suspend mode. */
	RTWN_CHK(rtwn_setbits_1(sc, 0xc4, 0x10, 0));

	RTWN_CHK(rtwn_setbits_2(sc, R92C_APS_FSMCO, R92C_APS_FSMCO_APFM_RSM, 0));

	/* Wait for power ready bit. */
	for (ntries = 0; ntries < 5000; ntries++) {
		if (rtwn_read_4(sc, R92C_APS_FSMCO) & R92C_APS_FSMCO_SUS_HOST)
			break;
		rtwn_delay(sc, 10);
	}
	if (ntries == 5000) {
		device_printf(sc->sc_dev,
		    "timeout waiting for chip power up\n");
		return (ETIMEDOUT);
	}

	/* Disable HWPDN. */
	RTWN_CHK(rtwn_setbits_2(sc, R92C_APS_FSMCO, R92C_APS_FSMCO_APDM_HPDN, 0));

	/* Disable WL suspend. */
	RTWN_CHK(rtwn_setbits_2(sc, R92C_APS_FSMCO, R92C_APS_FSMCO_AFSM_HSUS, 0));

    	/* Auto enable WLAN. */
    	RTWN_CHK(rtwn_setbits_2(sc, R92C_APS_FSMCO, 0, R92C_APS_FSMCO_APFM_ONMAC));
	for (ntries = 0; ntries < 5000; ntries++) {
		if (!(rtwn_read_2(sc, R92C_APS_FSMCO) &
		    R92C_APS_FSMCO_APFM_ONMAC))
			break;
		rtwn_delay(sc, 10);
	}
	if (ntries == 5000)
		return (ETIMEDOUT);

	/* Reduce RF noise. */
	rtwn_write_1(sc, R92C_AFE_LDO_CTRL, 0x35);

	/* Enable MAC DMA/WMAC/SCHEDULE/SEC blocks. */
	RTWN_CHK(rtwn_write_2(sc, R92C_CR, 0));
	RTWN_CHK(rtwn_setbits_2(sc, R92C_CR, 0,
	    R92C_CR_HCI_TXDMA_EN | R92C_CR_TXDMA_EN |
	    R92C_CR_HCI_RXDMA_EN | R92C_CR_RXDMA_EN |
	    R92C_CR_PROTOCOL_EN | R92C_CR_SCHEDULE_EN |
	    R92C_CR_ENSEC | R92C_CR_CALTMR_EN));

	return (0);
#undef RTWN_CHK
}

static int
r88fu_flush_fifo(struct rtwn_softc *sc)
{
	uint32_t reg;
	int retry, retval;
	rtwn_write_1(sc, R92C_TXPAUSE, 0xff);

	rtwn_bb_setbits(sc, REG_RXPKT_NUM, 0, RXPKT_NUM_RW_RELEASE_EN);

	retry = 100;

	/* setting retval to (EBUSY = 16) */
	retval = -16;

	do {
		reg = rtwn_bb_read(sc, REG_RXPKT_NUM);
		if(reg & RXPKT_NUM_RXDMA_IDLE) {
			retval = 0;
			break;
		}
		
	} while (retry--);

	rtwn_write_2(sc, REG_RQPN_NPQ, 0);
	rtwn_bb_write(sc, REG_RQPN, 0x80000000);
	rtwn_delay(sc, 2);

	if (!retry)
		device_printf(sc->sc_dev, "%s: Failed to flush FIFO\n", __func__);

	return retval;

}


void
r88fu_power_off(struct rtwn_softc *sc)
{
	uint8_t reg;
	int error, ntries;

	r88fu_flush_fifo(sc);

	/* Disable any kind of TX reports. */
	error = rtwn_setbits_1(sc, R88E_TX_RPT_CTRL,
	    R88E_TX_RPT1_ENA | R88E_TX_RPT2_ENA, 0);
	if (error == ENXIO)	/* hardware gone */
		return;

	/* Stop Rx. */
	rtwn_write_1(sc, R92C_CR, 0);

	/* Move card to Low Power State. */
	/* Block all Tx queues. */
	rtwn_write_1(sc, R92C_TXPAUSE, R92C_TX_QUEUE_ALL);

	for (ntries = 0; ntries < 10; ntries++) {
		/* Should be zero if no packet is transmitting. */
		if (rtwn_read_4(sc, R88E_SCH_TXCMD) == 0)
			break;

		rtwn_delay(sc, 5000);
	}
	if (ntries == 10) {
		device_printf(sc->sc_dev, "%s: failed to block Tx queues\n",
		    __func__);
		return;
	}

	/* CCK and OFDM are disabled, and clock are gated. */
	rtwn_setbits_1(sc, R92C_SYS_FUNC_EN, R92C_SYS_FUNC_EN_BBRSTB, 0);

	rtwn_delay(sc, 1);

	/* Reset MAC TRX */
	rtwn_write_1(sc, R92C_CR,
	    R92C_CR_HCI_TXDMA_EN | R92C_CR_HCI_RXDMA_EN |
	    R92C_CR_TXDMA_EN | R92C_CR_RXDMA_EN |
	    R92C_CR_PROTOCOL_EN | R92C_CR_SCHEDULE_EN);

	/* check if removed later */
	rtwn_setbits_1_shift(sc, R92C_CR, R92C_CR_ENSEC, 0, 1);

	/* Respond TxOK to scheduler */
	rtwn_setbits_1(sc, R92C_DUAL_TSF_RST, 0, 0x20);

	/* If firmware in ram code, do reset. */
#ifndef RTWN_WITHOUT_UCODE
	if (rtwn_read_1(sc, R92C_MCUFWDL) & R92C_MCUFWDL_RDY)
		r88f_fw_reset(sc, RTWN_FW_RESET_SHUTDOWN);
#endif

	/* Reset MCU ready status. */
	rtwn_write_1(sc, R92C_MCUFWDL, 0);

	/* Disable 32k. */
	rtwn_setbits_1(sc, R88E_32K_CTRL, 0x01, 0);

	/* Move card to Disabled state. */
	/* Turn off RF. */
	rtwn_write_1(sc, R92C_RF_CTRL, 0);

	/* LDO Sleep mode. */
	rtwn_setbits_1(sc, R92C_LPLDO_CTRL, 0, R92C_LPLDO_CTRL_SLEEP);

	/* Turn off MAC by HW state machine */
	rtwn_setbits_1_shift(sc, R92C_APS_FSMCO, 0,
	    R92C_APS_FSMCO_APFM_OFF, 1);

	for (ntries = 0; ntries < 10; ntries++) {
		/* Wait until it will be disabled. */
		if ((rtwn_read_2(sc, R92C_APS_FSMCO) &
		    R92C_APS_FSMCO_APFM_OFF) == 0)
			break;

		rtwn_delay(sc, 5000);
	}
	if (ntries == 10) {
		device_printf(sc->sc_dev, "%s: could not turn off MAC\n",
		    __func__);
		return;
	}

	/* schmit trigger */
	rtwn_setbits_1(sc, R92C_AFE_XTAL_CTRL + 2, 0, 0x80);

	/* Enable WL suspend. */
	rtwn_setbits_1_shift(sc, R92C_APS_FSMCO,
	    R92C_APS_FSMCO_AFSM_PCIE, R92C_APS_FSMCO_AFSM_HSUS, 1);

	/* Enable bandgap mbias in suspend. */
	rtwn_write_1(sc, R92C_APS_FSMCO + 3, 0);

	/* Clear SIC_EN register. */
	rtwn_setbits_1(sc, R92C_GPIO_MUXCFG + 1, 0x10, 0);

	/* Set USB suspend enable local register */
	rtwn_setbits_1(sc, R92C_USB_SUSPEND, 0, 0x10);

	/* Reset MCU IO Wrapper. */
	reg = rtwn_read_1(sc, R92C_RSV_CTRL + 1);
	rtwn_write_1(sc, R92C_RSV_CTRL + 1, reg & ~0x08);
	rtwn_write_1(sc, R92C_RSV_CTRL + 1, reg | 0x08);

	/* marked as 'For Power Consumption' code. */
	rtwn_write_1(sc, R92C_GPIO_OUT, rtwn_read_1(sc, R92C_GPIO_IN));
	rtwn_write_1(sc, R92C_GPIO_IOSEL, 0xff);

	rtwn_write_1(sc, R92C_GPIO_IO_SEL,
	    rtwn_read_1(sc, R92C_GPIO_IO_SEL) << 4);
	rtwn_setbits_1(sc, R92C_GPIO_MOD, 0, 0x0f);

	/* Set LNA, TRSW, EX_PA Pin to output mode. */
	rtwn_write_4(sc, R88E_BB_PAD_CTRL, 0x00080808);
}

void
r88fu_init_rx_agg(struct rtwn_softc *sc)
{
	rtwn_setbits_1(sc, R92C_TRXDMA_CTRL, 0, R92C_TRXDMA_CTRL_RXDMA_AGG_EN);
	/* set 5 is the dmasize */
	rtwn_write_1(sc, R92C_RXDMA_AGG_PG_TH, 5);
	/* set 32 is the dmatiming */
	rtwn_write_1(sc, R92C_RXDMA_AGG_PG_TH + 1, 32);
	rtwn_setbits_1(sc, R92E_RXDMA_PRO, 0, R92E_RXDMA_PRO_DMA_MODE);
}

void 
r88fu_init_bb(struct rtwn_softc *sc)
{

	/* Enable BB and RF */
	rtwn_setbits_2(sc, REG_SYS_FUNC, 0, SYS_FUNC_BB_GLB_RSTN | SYS_FUNC_BBRSTB | SYS_FUNC_DIO_RF);

	/*
	 * Per vendor driver, run power sequence before init of RF
	 */
	rtwn_write_1(sc, REG_RF_CTRL, RF_ENABLE | RF_RSTB | RF_SDMRSTB);

	rtwn_delay(sc, 10);

	rtwn_rf_write(sc, 0, RF6052_REG_IQADJ_G1, 0x780);

	rtwn_write_1(sc, REG_SYS_FUNC, SYS_FUNC_BB_GLB_RSTN | SYS_FUNC_BBRSTB | SYS_FUNC_USBA | SYS_FUNC_USBD);
}

void
r88fu_init_stats(struct rtwn_softc *sc)
{
	uint32_t clr;

	rtwn_write_2(sc, REG_NHM_TIMER_8723B + 2, 0xc350);
	rtwn_write_2(sc, REG_NHM_TH9_TH10_8723B + 2, 0xffff);
	rtwn_write_4(sc, REG_NHM_TH3_TO_TH0_8723B, 0xffffff50);
	rtwn_write_4(sc, REG_NHM_TH7_TO_TH4_8723B, 0xffffffff);

	rtwn_setbits_4(sc, REG_FPGA0_IQK, 0, 0xff);
	
	clr = /* (BIT(8) | BIT(9) | BIT(10)) */ 0x00000100 | 0x00000200 | 0x00000400;
	rtwn_setbits_4(sc, REG_NHM_TH9_TH10_8723B, clr, 0x00000100);


	rtwn_setbits_4(sc, REG_OFDM0_FA_RSTC, 0, 0x00000080);
}

void
r88fu_post_init(struct rtwn_softc *sc)
{
	/* Enable per-packet TX report (RPT1) */
	rtwn_setbits_1(sc, R88E_TX_RPT_CTRL, 0, R88E_TX_RPT1_ENA);

#ifndef RTWN_WITHOUT_UCODE
	/* Enable timer report (RPT2) if requested */
	if (sc->macid_rpt2_max_num > 0) {
		rtwn_setbits_1(sc, R88E_TX_RPT_CTRL, 0,
		    R88E_TX_RPT2_ENA);

		/* Configure how many TX RPT2 entries to populate */
		rtwn_write_1(sc, R88E_TX_RPT_MACID_MAX,
		    sc->macid_rpt2_max_num);
		/* Enable periodic TX report; 32uS units */
		rtwn_write_2(sc, R88E_TX_RPT_TIME, 0xcdf0);
	}
#endif

	/* Perform LO and IQ calibrations. */
	rtwn_iq_calib(sc);
	/* Perform LC calibration. */
	rtwn_lc_calib(sc);

	rtwn_write_1(sc, R92C_USB_HRPWM, 0);

	if (sc->sc_ratectl_sysctl == RTWN_RATECTL_FW) {
		/* No support (yet?) for f/w rate adaptation. */
		sc->sc_ratectl = RTWN_RATECTL_NET80211;
	} else
		sc->sc_ratectl = sc->sc_ratectl_sysctl;
}

