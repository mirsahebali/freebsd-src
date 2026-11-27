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

#include <dev/usb/usb.h>
#include <dev/usb/usbdi.h>

#include <dev/rtwn/if_rtwnreg.h>
#include <dev/rtwn/if_rtwnvar.h>
#include <dev/rtwn/usb/rtwn_usb_var.h>
#include <dev/rtwn/if_rtwn_debug.h>

#include <dev/rtwn/rtl8188e/r88e.h>
#include <dev/rtwn/rtl8188e/r88e_reg.h>
#include <dev/rtwn/rtl8188e/usb/r88eu.h>
#include <dev/rtwn/rtl8188e/usb/r88eu_reg.h>

#include <dev/rtwn/rtl8192c/r92c.h>
#include <dev/rtwn/rtl8192c/r92c_var.h>
#include <dev/rtwn/rtl8192c/usb/r92cu.h>
#include <dev/rtwn/rtl8192e/r92e_reg.h>

#include <dev/rtwn/rtl8188f/usb/r88fu.h>

#include <dev/rtwn/rtl8192f/r92f.h>
#include <dev/rtwn/rtl8192f/usb/r92fu.h>
#include <dev/rtwn/rtl8192f/r92f_reg.h>
#include <dev/rtwn/rtl8192f/usb/r92fu_reg.h>

static int
r92fu_disabled_to_emu(struct rtwn_softc *sc)
{
#define RTWN_CHK(res) do {	\
	if (res != 0)		\
		return (EIO);	\
} while(0)

	RTWN_CHK(rtwn_setbits_2(sc, R92C_APS_FSMCO,
	    R92C_APS_FSMCO_APDM_HPDN | R92C_APS_FSMCO_AFSM_HSUS, 0));

	RTWN_CHK(rtwn_bb_setbits(sc, R92C_GPIO_INTM, 0x10000, 0));

	RTWN_CHK(rtwn_setbits_2(sc, R92C_APS_FSMCO,
	    R92C_APS_FSMCO_AFSM_PCIE | R92C_APS_FSMCO_AFSM_HSUS, 0));

	return (0);
#undef RTWN_CHK
}

#define R92F_MAX_REG_POLL 500

static int
r92fu_emu_to_active(struct rtwn_softc *sc)
{
#define RTWN_CHK(res) do {	\
	if (res != 0)		\
		return (EIO);	\
} while(0)
	uint32_t reg;
	int count;

	/* enable LDOA12 MACRO block for all interface */
	RTWN_CHK(rtwn_setbits_1(sc, R92C_LDOA15_CTRL, 0, R92C_LDOA15_CTRL_EN));

	/* disable BT_GPS_SEL pins */
	RTWN_CHK(rtwn_bb_setbits(sc, R88E_BB_PAD_CTRL, 0x10000000, 0));

	rtwn_delay(sc, 1000);

	/* release analog Ips to digital */
	RTWN_CHK(rtwn_setbits_1(sc, R92C_SYS_ISO_CTRL,
		R92C_SYS_ISO_CTRL_IP2MAC, 0));

	RTWN_CHK(rtwn_setbits_2(sc, R92C_APS_FSMCO, R92C_APS_FSMCO_AFSM_PCIE |
	    R92C_APS_FSMCO_AFSM_HSUS | R92C_APS_FSMCO_APFM_RSM, 0));

	/* wait till 0x04[17] = 1 power ready */
	for (count = R92F_MAX_REG_POLL; count; count--) {
		reg = rtwn_bb_read(sc, R92C_APS_FSMCO);
		if (reg & 0x20000)
			break;

		rtwn_delay(sc, 10);
	}

	/* setting retval to (EBUSY = 16) */
	if (!count)
		return (-16);

	RTWN_CHK(rtwn_bb_setbits(sc, R92C_APS_FSMCO, 0,
	    R92C_APS_FSMCO_RDY_MACON));

	for (count = R92F_MAX_REG_POLL; count; count--) {
		reg = rtwn_bb_read(sc, R92C_APS_FSMCO);
		if ((reg & (R92C_APS_FSMCO_APFM_ONMAC | R92C_APS_FSMCO_APFM_OFF)) == 0)
			break;

		rtwn_delay(sc, 10);
	}

	if (!count)
		return (-16);

	/* SWR OCP enable */
	RTWN_CHK(rtwn_bb_setbits(sc, R92C_AFE_MISC, 0, 0x40000));

	RTWN_CHK(rtwn_setbits_2(sc, R92C_APS_FSMCO,
	    R92C_APS_FSMCO_APDM_HPDN, 0));

	RTWN_CHK(rtwn_setbits_2(sc, R92C_APS_FSMCO,
	    R92C_APS_FSMCO_AFSM_PCIE | R92C_APS_FSMCO_AFSM_HSUS, 0));

	/* 0x7c[31]=1, LDO has max output capability */
	RTWN_CHK(rtwn_bb_setbits(sc, R92C_LDO_SWR_CTRL, 0, 0x80000000));

	RTWN_CHK(rtwn_setbits_2(sc, R92C_APS_FSMCO, 0,
	    R92C_APS_FSMCO_APFM_ONMAC));

	for (count = R92F_MAX_REG_POLL; count; count--) {
		reg = rtwn_bb_read(sc, R92C_APS_FSMCO);
		if ((reg & R92C_APS_FSMCO_APFM_ONMAC) == 0)
			break;

		rtwn_delay(sc, 10);
	}

	if (!count)
		return (-16);

	/* Enable WL control XTAL setting */
	RTWN_CHK(rtwn_setbits_1(sc, R92C_AFE_MISC, 0,
	    R92F_AFE_MISC_WL_XTAL_CTRL));

	/* Enable falling edge triggering interrupt */
	RTWN_CHK(rtwn_setbits_2(sc, R92C_GPIO_INTM, 0,
	    R92F_GPIO_INTM_EDGE_TRIG_IRQ));


	/* Enable GPIO9 data mode */
	RTWN_CHK(rtwn_setbits_2(sc, R92F_GPIO_IO_SEL_2,
	    R92F_GPIO_IO_SEL_2_GPIO09_IRQ, 0));

	/* Enable GPIO9 input mode */
	RTWN_CHK(rtwn_setbits_2(sc, R92F_GPIO_IO_SEL_2,
	    R92F_GPIO_IO_SEL_2_GPIO09_INPUT, 0));

	/* Enable HSISR GPIO[C:0] interrupt */
	RTWN_CHK(rtwn_setbits_1(sc, R92C_HSIMR, 0, 0x01));
	

	/* RF HW ON/OFF Enable */
	RTWN_CHK(rtwn_setbits_1(sc, R92C_MULTI_FUNC_CTRL,
	    R92F_MULTI_WIFI_HW_ROF_EN, 0));

	/* Register Lock Disable */
	RTWN_CHK(rtwn_setbits_1(sc, R92C_RSV_CTRL, 0, 0x80));

	/* For GPIO9 internal pull high setting */
	RTWN_CHK(rtwn_setbits_2(sc, R92C_MULTI_FUNC_CTRL, 0, 0x4000));

	/* reset RF path S1 */
	RTWN_CHK(rtwn_write_1(sc, R92C_RF_CTRL, 0));

	/* reset RF path S0 */
	RTWN_CHK(rtwn_write_1(sc, R92C_AFE_XTAL_CTRL_EXT + 3, 0));

	/* enable RF path S1 */
	RTWN_CHK(rtwn_write_1(sc, R92C_RF_CTRL, R92C_RF_CTRL_SDMRSTB | R92C_RF_CTRL_RSTB | R92C_RF_CTRL_EN));

	/* enable RF path S0 */
	RTWN_CHK(rtwn_write_1(sc, R92C_AFE_XTAL_CTRL_EXT + 3, R92C_RF_CTRL_SDMRSTB | R92C_RF_CTRL_RSTB | R92C_RF_CTRL_EN));

	/* AFE_Ctrl */
	RTWN_CHK(rtwn_setbits_1(sc, R92F_RSVD_1, 0, 0x20));

	/* AFE_Ctrl */
	RTWN_CHK(rtwn_write_1(sc, R92F_RSVD_4, 0xcc));

	/* AFE_Ctrl 0x24[4:3]=00 for xtal gmn */
	RTWN_CHK(rtwn_setbits_1(sc, R92C_AFE_XTAL_CTRL, 0x18, 0));

	/* GPIO_A[31:0] Pull down software register */
	RTWN_CHK(rtwn_bb_write(sc, R92F_GPIO_A0, 0xffffffff));

	/* GPIO_B[7:0] Pull down software register */
	RTWN_CHK(rtwn_write_1(sc, R92F_GPIO_B0, 0xff));

	/* Register Lock Enable */
	RTWN_CHK(rtwn_setbits_1(sc, R92C_RSV_CTRL, 0x80, 0));

	return (0);
#undef RTWN_CHK
}

static int
r92fu_active_to_emu(struct rtwn_softc *sc)
{
#define RTWN_CHK(res) do {	\
	if (res != 0)		\
		return (EIO);	\
} while(0)
	uint32_t reg;
	int count;

	/* Reset BB, RF enter Power Down mode */
	RTWN_CHK(rtwn_setbits_1(sc, R92C_SYS_FUNC_EN,
	    R92C_SYS_FUNC_EN_BBRSTB, 0));

	/* Enable rising edge triggering interrupt */
	RTWN_CHK(rtwn_setbits_2(sc, R92C_GPIO_INTM, 0x200, 0));

	/* release WLON reset */
	RTWN_CHK(rtwn_setbits_4(sc, R92C_APS_FSMCO, 0,
	    R92C_APS_FSMCO_RDY_MACON));

	/* turn off MAC by HW state machine */
	RTWN_CHK(rtwn_setbits_2(sc, R92C_APS_FSMCO, 0, R92C_APS_FSMCO_APFM_OFF));

	for (count = R92F_MAX_REG_POLL; count; count--) {
		reg = rtwn_bb_read(sc, R92C_APS_FSMCO);
		if ((reg & R92C_APS_FSMCO_APFM_OFF) == 0)
			break;

		rtwn_delay(sc, 10);
	}

	if (!count)
		return (-16);

	/* analog Ips to digital, 1:isolation */
	RTWN_CHK(rtwn_setbits_1(sc, R92C_SYS_ISO_CTRL, 0,
	    R92C_SYS_ISO_CTRL_IP2MAC));

	/* disable LDOA12 MACRO block */
	RTWN_CHK(rtwn_setbits_1(sc, R92C_LDOA15_CTRL, R92C_LDOA15_CTRL_EN, 0));

	return (0);
#undef RTWN_CHK
}

static int
r92fu_active_to_lps(struct rtwn_softc *sc)
{
#define RTWN_CHK(res) do {	\
	if (res != 0)		\
		return (EIO);	\
} while(0)
	
	uint16_t reg16;
	uint32_t reg;
	int retry;

	/* Tx Pause */
	RTWN_CHK(rtwn_write_1(sc, R92C_TXPAUSE, 0xff));

	retry = 100;

	/* Poll 32 bit wide R92C_SCH_TX_CMD for 0 to ensure no TX is pending. */
	do {
		reg = rtwn_bb_read(sc, R88E_SCH_TXCMD);
		if (!reg)
			break;

		rtwn_delay(sc, 10);
	} while (retry--);

	if (!retry) {
		device_printf(sc->sc_dev,
		    "%s: Failed to flush TX queue\n", __func__);
		return (-16);
	}

	/* Disable CCK and OFDM, clock gated */
	RTWN_CHK(rtwn_setbits_1(sc, R92C_SYS_FUNC_EN,
	    R92C_SYS_FUNC_EN_BBRSTB, 0));

	rtwn_delay(sc, 2);

	/* Whole BB is reset */
	RTWN_CHK(rtwn_setbits_1(sc, R92C_SYS_FUNC_EN,
	    R92C_SYS_FUNC_EN_BB_GLB_RST, 0));

	/* Reset MAC TRX */
	reg16 = rtwn_read_2(sc, R92C_CR);
	reg16 &= 0xff00;
	reg16 |= R92C_CR_HCI_RXDMA_EN | R92C_CR_HCI_TXDMA_EN;
	reg16 &= ~R92C_CR_ENSEC;
	RTWN_CHK(rtwn_write_2(sc, R92C_CR, reg16));

	/* Respond TxOK to scheduler */
	RTWN_CHK(rtwn_setbits_1(sc, R92C_DUAL_TSF_RST, 0,
	    R92C_DUAL_TSF_RST_TXOK));

	return (0);

#undef RTWN_CHK
}

static int
r92fu_emu_to_disabled(struct rtwn_softc *sc)
{
#define RTWN_CHK(res) do {	\
	if (res != 0)		\
		return (EIO);	\
} while(0)
	/* SOP option to disable BG/MB */
	RTWN_CHK(rtwn_write_1(sc, R92C_APS_FSMCO + 3, 0x20));

	/* 0x04[12:11] = 2b'01 enable WL suspend */
	RTWN_CHK(rtwn_setbits_2(sc, R92C_APS_FSMCO, R92C_APS_FSMCO_AFSM_PCIE,
	    R92C_APS_FSMCO_AFSM_HSUS));

	/* enable GPIO9 as EXT WAKEUP */
	RTWN_CHK(rtwn_bb_setbits(sc, R92C_GPIO_INTM, 0, 0x10000));

	return (0);

#undef RTWN_CHK
}

int
r92fu_power_on(struct rtwn_softc *sc)
{
#define RTWN_CHK(res) do {	\
	if (res != 0)		\
		return (EIO);	\
} while(0)

	RTWN_CHK(rtwn_write_1(sc, R92F_USB_ACCESS_TIMEOUT, 0x80));

	RTWN_CHK(r92fu_disabled_to_emu(sc));

	RTWN_CHK(r92fu_emu_to_active(sc));

	RTWN_CHK(rtwn_write_2(sc, R92C_CR, 0));
	
	RTWN_CHK(rtwn_setbits_2(sc, R92C_CR, 0,
	    R92C_CR_HCI_TXDMA_EN | R92C_CR_HCI_RXDMA_EN |
	    R92C_CR_TXDMA_EN | R92C_CR_RXDMA_EN |
	    R92C_CR_PROTOCOL_EN | R92C_CR_SCHEDULE_EN |
	    R92C_CR_ENSEC | R92C_CR_CALTMR_EN));

	return (0);
#undef RTWN_CHK
}

void
r92fu_power_off(struct rtwn_softc *sc)
{
	int error;
	r88fu_flush_fifo(sc);

	/* Stop Tx Report Timer. 0x4EC[Bit1]=b'0 */
	error = rtwn_setbits_1(sc, R88E_TX_RPT_CTRL, 0x2, 0);
	if (error == ENXIO) /* hardware gone */
		return;
	
	/* stop rx */
	rtwn_write_1(sc, R92C_CR, 0x00);

	r92fu_active_to_lps(sc);

	
#ifndef RTWN_WITHOUT_UCODE
	/* Reset Firmware if running in RAM */
	if (rtwn_read_1(sc, R92C_MCUFWDL) & R92C_MCUFWDL_RAM_DL_SEL)
		r92c_fw_reset(sc, RTWN_FW_RESET_SHUTDOWN);
#endif


	/* Reset MCU */
	rtwn_setbits_2(sc, R92C_SYS_FUNC_EN, R92C_SYS_FUNC_EN_CPUEN, 0);

	/* Reset MCU ready status */
	rtwn_write_1(sc, R92C_MCUFWDL, 0x00);

	if (r92fu_active_to_emu(sc) != 0)
		RTWN_DPRINTF(sc, RTWN_DEBUG_RESET,
		    "%s: failed to enter EMU state\n", __func__);
	r92fu_emu_to_disabled(sc);
}

int
r92fu_set_qmap(struct rtwn_softc *sc)
{
	struct rtwn_usb_softc *uc = RTWN_USB_SOFTC(sc);
	uint32_t qmap, reg;

	switch (uc->ntx) {
		case 1:
			qmap = R92F_USB_TRXDMA_CTRL_QMAP_HQ;
			break;
		case 2:
			qmap = R92F_USB_TRXDMA_CTRL_QMAP_HQ_NQ;
			break;
		default:
			qmap = R92F_USB_TRXDMA_CTRL_QMAP_3EP;
			break;
	}

	reg = rtwn_read_4(sc, R92C_TRXDMA_CTRL);
	reg &= 0x7;
	reg |= qmap;
	return (rtwn_write_4(sc, R92C_TRXDMA_CTRL, reg));
}

void
r92fu_init_ampdu(struct rtwn_softc *sc)
{
	struct rtwn_usb_softc *uc = RTWN_USB_SOFTC(sc);
	uint8_t val;

	val = rtwn_read_1(sc, R12A_RXDMA_PRO);
	val = RW(val, R12A_BURST_SZ,
	    usbd_get_speed(uc->uc_udev) == USB_SPEED_HIGH ?
	    R12A_BURST_SZ_USB2 : R12A_BURST_SZ_USB1);
	val |= R12A_DMA_MODE | SM(R12A_BURST_CNT, 3);
	rtwn_write_1(sc, R12A_RXDMA_PRO, val);

	/* Enable single packet AMPDU. */
	rtwn_setbits_1(sc, R12A_HT_SINGLE_AMPDU, 0,
	    R12A_HT_SINGLE_AMPDU_PKT_ENA);

	rtwn_write_2(sc, R92C_MAX_AGGR_NUM, 0x1f1f);
	rtwn_write_1(sc, R12A_AMPDU_MAX_TIME, 0x5e);
	rtwn_write_4(sc, R12A_AMPDU_MAX_LENGTH, UINT32_MAX);

	/*
	 * The vendor driver says this is for
	 * "VHT packet length 11K", but this chip
	 * doesn't support VHT rates...
	 */
	rtwn_write_1(sc, R92C_RX_PKT_LIMIT, 0x18);

	rtwn_write_1(sc, R92C_PIFS, 0);
	rtwn_write_1(sc, R92C_FWHW_TXQ_CTRL, R92C_FWHW_TXQ_CTRL_AMPDU_RTY_NEW);
	rtwn_write_4(sc, R92C_FAST_EDCA_CTRL, 0x03086666);

	rtwn_write_1(sc, R92C_USTIME_TSF, 0x50);
	rtwn_write_1(sc, R92C_USTIME_EDCA, 0x50);

	/* Do not reset MAC. */
	rtwn_setbits_1(sc, R92C_RSV_CTRL, 0, 0x60);
}

void
r92fu_init_tx_agg(struct rtwn_softc *sc)
{
	uint8_t usb_tx_agg_desc_num = 6;

	/* TX aggregation */
	rtwn_setbits_4(sc, R92C_TDECTRL, 0xf << 4, usb_tx_agg_desc_num << 4);
	rtwn_write_1(sc, R92E_DWBCN1_CTRL, usb_tx_agg_desc_num << 1);
}

void
r92fu_init_rx_agg(struct rtwn_softc *sc)
{
	uint32_t agg_rx;
	uint8_t agg_ctrl;

	/* RX aggregation */
	agg_ctrl = rtwn_read_1(sc, R92C_TRXDMA_CTRL);
	agg_ctrl &= ~R92C_TRXDMA_CTRL_RXDMA_AGG_EN;

	agg_rx = rtwn_read_4(sc, R92C_RXDMA_AGG_PG_TH);
	agg_rx &= ~R92F_RXDMA_USB_AGG_EN;
	agg_rx &= ~0xff0f; /* reset agg size and timeout */

	rtwn_write_1(sc, R92C_TRXDMA_CTRL, agg_ctrl);
	rtwn_write_4(sc, R92C_RXDMA_AGG_PG_TH, agg_rx);
}

void
r92fu_init_antsel(struct rtwn_softc *sc)
{
	rtwn_setbits_1(sc, R92C_LEDCFG2, 0, 0x80);
}

void
r92fu_post_init(struct rtwn_softc *sc)
{
	uint8_t reg8;

	rtwn_setbits_1(sc, R88E_TX_RPT_CTRL, 0, R88E_TX_RPT2_ENA);
	/* Set MAX RPT MACID */
	rtwn_write_1(sc, R88E_TX_RPT_CTRL + 1, R88E_TX_RPT2_ENA);
	/* TX report Timer. Unit: 32us */
	rtwn_write_2(sc, R88E_TX_RPT_TIME, 0xcdf0);
	/* tmp ps ? */
	reg8 = rtwn_read_1(sc, 0xa3);
	reg8 &= 0xf8;
	rtwn_write_1(sc, 0xa3, reg8);

	rtwn_write_1(sc, R12A_EARLY_MODE_CONTROL, 0);

	/* RFSW control: TRSW/TRSWB/ANTSW/ANTSWB/PAPE */
	rtwn_write_4(sc, R92C_FPGA0_RFIFACESW(0), 0x7000760);

	/* Perform LC calibration. */
	r92f_lc_calib(sc);
	/* Perform LO and IQ calibrations. */
	r92f_iq_calib(sc);

	/*
	 * This should enable thermal meter
	 */
	rtwn_rf_setbits(sc, 0, R88E_RF_T_METER, 0, 0x37cf8);

	rtwn_write_1(sc, R92C_USB_HRPWM, 0);

	if (sc->sc_ratectl_sysctl == RTWN_RATECTL_FW) {
		/* No support (yet?) for f/w rate adaptation. */
		sc->sc_ratectl = RTWN_RATECTL_NET80211;
	} else
		sc->sc_ratectl = sc->sc_ratectl_sysctl;

}
