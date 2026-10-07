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
#include <dev/rtwn/if_rtwn_nop.h>

#include <dev/rtwn/usb/rtwn_usb_var.h>

#include <dev/rtwn/rtl8192c/r92c_reg.h>
#include <dev/rtwn/rtl8192c/r92c_var.h>

#include <dev/rtwn/rtl8192c/usb/r92cu.h>
#include <dev/rtwn/rtl8192c/usb/r92cu_tx_desc.h>

#include <dev/rtwn/rtl8812a/r12a.h>
#include <dev/rtwn/rtl8812a/usb/r12au.h>

#include <dev/rtwn/rtl8188e/r88e_priv.h>
#include <dev/rtwn/rtl8188e/r88e_rom_image.h>	/* for 'macaddr' field */

#include <dev/rtwn/rtl8188e/usb/r88eu.h>

#include <dev/rtwn/rtl8188f/r88f.h>
#include <dev/rtwn/rtl8188f/r88f_reg.h>
#include <dev/rtwn/rtl8188f/usb/r88fu.h>
#include <dev/rtwn/rtl8188f/r88f_priv.h>
#include <dev/rtwn/rtl8188f/r88f_rom_defs.h>
#include <dev/rtwn/rtl8188f/r88f_rom_image.h>

static struct rtwn_r88e_txpwr r88e_txpwr;

void	r88fu_attach(struct rtwn_usb_softc *);

static void
r88fu_set_macaddr(struct rtwn_softc *sc, uint8_t *buf)
{
	struct r88f_rom *rom = (struct r88f_rom *)buf;

	IEEE80211_ADDR_COPY(sc->sc_ic.ic_macaddr, rom->macaddr);
}

static void
r88f_read_chipid_vendor(struct rtwn_softc *sc, uint32_t reg_sys_cfg)
{
	struct r92c_softc *rs = sc->sc_priv;

	if (MS(reg_sys_cfg, R92C_SYS_CFG_CHIP_VER_RTL) == 1)
		rs->chip |= R88F_CHIP_B_CUT;
}

static void
r88f_postattach(struct rtwn_softc *sc)
{
	struct r92c_softc *rs = sc->sc_priv;

	if(rs->chip & R88F_CHIP_B_CUT)
		sc->rf_prog = &rtl8188ftv_cut_b_rf[0];

}

static void
r88fu_attach_private(struct rtwn_softc *sc)
{
	struct r92c_softc *rs;

	rs = malloc(sizeof(struct r92c_softc), M_RTWN_PRIV, M_WAITOK | M_ZERO);

	rs->rs_txpwr			= &r88e_txpwr;
	rs->rs_txagc			= NULL;

	rs->rs_set_bw20			= r88f_set_bw20;
	rs->rs_get_txpower		= r88f_get_txpower;
	rs->rs_set_gain			= r88e_set_gain;
	rs->rs_tx_enable_ampdu		= r92c_tx_enable_ampdu;
	rs->rs_tx_setup_hwseq		= r92c_tx_setup_hwseq;
	rs->rs_tx_setup_macid		= r92c_tx_setup_macid;
	rs->rs_set_rom_opts		= r88fu_set_macaddr;

	rs->rf_read_delay[0]		= 10;
	rs->rf_read_delay[1]		= 100;
	rs->rf_read_delay[2]		= 10;

	sc->sc_priv = rs;
}

static void
r88fu_adj_devcaps(struct rtwn_softc *sc)
{
	/* XXX TODO? */
}

void
r88fu_attach(struct rtwn_usb_softc *uc)
{
	struct rtwn_softc *sc		= &uc->uc_sc;

	/* USB part. */
	uc->uc_align_rx			= r12au_align_rx;
	uc->tx_agg_desc_num		= 6;

	/* Common part. */
	sc->sc_flags			= RTWN_FLAG_EXT_HDR;

	sc->sc_set_chan			= r88f_set_chan;
	sc->sc_fill_tx_desc		= r92c_fill_tx_desc;
	sc->sc_fill_tx_desc_raw 	= r92c_fill_tx_desc_raw;
	sc->sc_fill_tx_desc_null	= r92c_fill_tx_desc_null;
	sc->sc_dump_tx_desc		= r92cu_dump_tx_desc;
	sc->sc_tx_radiotap_flags	= r92c_tx_radiotap_flags;
	sc->sc_rx_radiotap_flags	= r92c_rx_radiotap_flags;
	sc->sc_get_rx_stats		= r12a_get_rx_stats;
	sc->sc_get_rssi_cck		= r88f_get_rssi_cck;
	sc->sc_get_rssi_ofdm		= r88f_get_rssi_ofdm;
	sc->sc_classify_intr		= r88e_classify_intr;
	sc->sc_handle_tx_report		= r88e_ratectl_tx_complete;
	sc->sc_handle_tx_report2	= r88e_ratectl_tx_complete_periodic;
	sc->sc_handle_c2h_report	= r88e_handle_c2h_report;
	sc->sc_check_frame		= rtwn_nop_int_softc_mbuf;
	sc->sc_rf_read			= r92c_rf_read;
	sc->sc_rf_write			= r88f_rf_write;
	sc->sc_check_condition		= r92c_check_condition;
	sc->sc_efuse_postread		= rtwn_nop_softc;
	sc->sc_efuse_preread		= r88f_efuse_preread;
	sc->sc_parse_rom		= r88f_parse_rom;
	sc->sc_set_led			= r88e_set_led;
	sc->sc_power_on			= r88fu_power_on;
	sc->sc_power_off		= r88fu_power_off;
#ifndef RTWN_WITHOUT_UCODE
	sc->sc_fw_reset			= r88f_fw_reset;
	sc->sc_fw_download_enable	= r88f_fw_download_enable;
#endif
	sc->sc_llt_init			= r88f_llt_init;
	sc->sc_set_page_size		= r88f_set_page_size;
	sc->sc_lc_calib			= r88f_lc_calib;
	sc->sc_iq_calib			= r88f_iq_calib;
	sc->sc_read_chipid_vendor	= r88f_read_chipid_vendor;
	sc->sc_adj_devcaps		= r88fu_adj_devcaps;
	sc->sc_vap_preattach		= rtwn_nop_softc_vap;
	sc->sc_postattach		= r88f_postattach;
	sc->sc_detach_private		= r92c_detach_private;
	sc->sc_set_media_status		= r88e_set_media_status;
#ifndef RTWN_WITHOUT_UCODE
	sc->sc_set_rsvd_page		= r88e_set_rsvd_page;
	sc->sc_set_pwrmode		= r88e_set_pwrmode;
	sc->sc_set_rssi			= rtwn_nop_softc;	/* XXX TODO? */
#endif
	sc->sc_beacon_init		= r92c_beacon_init;
	sc->sc_beacon_enable		= r92c_beacon_enable;
	sc->sc_sta_beacon_enable	= r92c_sta_beacon_enable;
	sc->sc_beacon_set_rate		= rtwn_nop_void_int;
	sc->sc_beacon_select		= rtwn_nop_softc_int;
	sc->sc_temp_measure		= r92c_temp_measure;
	sc->sc_temp_read		= r92c_temp_read;
	sc->sc_init_tx_agg		= r88fu_init_tx_agg;
	sc->sc_init_rx_agg		= r88fu_init_rx_agg;
	sc->sc_init_ampdu		= r88f_init_ampdu;
	sc->sc_init_intr		= rtwn_nop_softc;
	sc->sc_init_edca		= r88f_init_edca;
	sc->sc_init_bb			= r88fu_init_bb;
	sc->sc_init_rf			= r92c_init_rf;
	sc->sc_init_antsel		= rtwn_nop_softc;
	sc->sc_post_init		= r88fu_post_init;
	sc->sc_init_bcnq1_boundary	= rtwn_nop_int_softc;
	sc->sc_set_tx_power		= r88f_set_tx_power;

	sc->mac_prog			= &rtl8188ftv_mac[0];
	sc->mac_size			= nitems(rtl8188ftv_mac);
	sc->bb_prog			= &rtl8188ftv_bb[0];
	sc->bb_size			= nitems(rtl8188ftv_bb);
	sc->agc_prog			= &rtl8188ftv_agc[0];
	sc->agc_size			= nitems(rtl8188ftv_agc);
	sc->rf_prog			= &rtl8188ftv_rf[0];

	sc->name			= "RTL8188FTV";
	sc->fwname			= "rtwn-rtl8188fufw";
	sc->fwsig			= 0x88f;

	sc->ackto			= 0x40;
	sc->npubqpages			= R88F_TX_PAGE_COUNT;
	sc->nhqpages			= R88F_HQ_NPAGES;
	sc->nlqpages			= R88F_LQ_NPAGES;
	sc->nnqpages			= R88F_NQ_NPAGES;
	sc->page_size			= R92C_TX_PAGE_SIZE;

	sc->page_count			= R88F_TX_PAGE_COUNT;
	sc->pktbuf_count		= R88F_TXPKTBUF_COUNT;

	sc->txdesc_len			= sizeof(struct r92cu_tx_desc);
	sc->efuse_maxlen		= R88E_EFUSE_MAX_LEN;
	sc->efuse_maplen		= R88E_EFUSE_MAP_LEN;
	sc->rx_dma_size			= R88F_RX_DMA_BUFFER_SIZE;

	sc->macid_limit			= R88E_MACID_MAX + 1;
	/* XXX this limit may be expanded to R88E_MACID_MAX */
	sc->macid_rpt2_max_num		= 2;
	sc->cam_entry_limit		= R92C_CAM_ENTRY_COUNT;
	sc->fwsize_limit		= R12A_MAX_FW_SIZE;
	sc->temp_delta			= R88E_CALIB_THRESHOLD;

	sc->bcn_status_reg[0]		= R92C_TDECTRL;
	sc->bcn_status_reg[1]		= R92C_TDECTRL;
	sc->rcr				= 0;

	sc->ntxchains			= 1;
	sc->nrxchains			= 1;

	r88fu_attach_private(sc);
}
