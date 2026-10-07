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
#include <dev/rtwn/if_rtwn_debug.h>

#include <dev/rtwn/usb/rtwn_usb_var.h>

#include <dev/rtwn/rtl8192c/r92c.h>
#include <dev/rtwn/rtl8192c/r92c_priv.h>
#include <dev/rtwn/rtl8192c/r92c_reg.h>
#include <dev/rtwn/rtl8192c/r92c_var.h>

#include <dev/rtwn/rtl8192e/r92e_reg.h>

#include <dev/rtwn/rtl8188f/r88f.h>
#include <dev/rtwn/rtl8188f/r88f_reg.h>

#define R92E_AUTO_LLT			0x224
#define R92E_AUTO_LLT_EN	0x00010000
#define R92E_RXDMA_PRO_DMA_MODE	0x02
#define R92C_RESP_SIFS_CCK		0x63c
#define R92C_RESP_SIFS_OFDM		0x63e
#define R92E_RXDMA_PRO			0x290

int
r88f_llt_init(struct rtwn_softc *sc)
{
	int ntries;

	rtwn_setbits_4(sc, R92E_AUTO_LLT, 0, R92E_AUTO_LLT_EN);
	for (ntries = 0; ntries < 1000; ntries++) {
		if (!(rtwn_read_4(sc, R92E_AUTO_LLT) & R92E_AUTO_LLT_EN))
			return (0);
		rtwn_delay(sc, 2);
	}

	/* R88F usb quirks */
	rtwn_setbits_2(sc, R92C_CR, 0, 0x00000040 | 0x00000080);
	rtwn_setbits_4(sc, R92C_TXDMA_OFFSET_CHK, 0, 0x00000200);

	return (ETIMEDOUT);
}


int
r88f_set_page_size(struct rtwn_softc *sc)
{
	return (rtwn_write_1(sc, R92C_PBP,
	    SM(R92C_PBP_PSRX, R92C_PBP_256) |
	    SM(R92C_PBP_PSTX, R92C_PBP_256)));
}
void
r88f_init_ampdu(struct rtwn_softc *sc)
{
	struct rtwn_usb_softc *uc = RTWN_USB_SOFTC(sc);
	uint8_t reg;

	reg = rtwn_read_1(sc, R92E_RXDMA_PRO);
	reg &= ~0x30;
	if(usbd_get_speed(uc->uc_udev) == USB_SPEED_HIGH)
	{
		rtwn_write_1(sc, R92E_RXDMA_PRO, reg | 0x1e);
	} else {
		rtwn_write_1(sc, R92E_RXDMA_PRO, reg | 0x2e);
	}

	/* Setup AMPDU aggregation. */
	rtwn_setbits_1(sc, R88F_HT_SINGLE_AMPDU, 0, R88F_HT_SINGLE_AMPDU_EN);
	rtwn_write_2(sc, R92C_MAX_AGGR_NUM, 0x0c14);
	rtwn_write_1(sc, R88F_AMPDU_MAX_TIME, 0x70);
	rtwn_write_4(sc, R92C_AGGLEN_LMT, 0xffffffff);

	/* For VHT packet length 11K */
	rtwn_write_1(sc, R88F_RX_PKT_LIMIT, 0x18);

	rtwn_write_1(sc, R92C_PIFS, 0);
	rtwn_write_1(sc, R92C_FWHW_TXQ_CTRL, 0x80);
	rtwn_write_4(sc, R92C_FAST_EDCA_CTRL, 0x03086666);
	rtwn_write_1(sc, R92C_USTIME_TSF, 0x28);
	rtwn_write_1(sc, R88F_USTIME_EDCA, 0x28);

	/* To prevent bus resetting the mac. */
	rtwn_setbits_1(sc, R92C_RSV_CTRL, 0, R92C_RSV_CTRL_R_DIS_PRST_0 | R92C_RSV_CTRL_R_DIS_PRST_1);
}

void
r88f_init_edca(struct rtwn_softc *sc)
{
	/* SIFS */
	rtwn_write_2(sc, R92C_SPEC_SIFS, 0x100a);
	rtwn_write_2(sc, R92C_MAC_SPEC_SIFS, 0x100a);
	rtwn_write_2(sc, R92C_SIFS_CCK, 0x100a);
	rtwn_write_2(sc, R92C_SIFS_OFDM, 0x100a);
	rtwn_write_2(sc, R92C_RESP_SIFS_CCK, 0x0808);
	rtwn_write_2(sc, R92C_RESP_SIFS_OFDM, 0x0a0a);

	/* TXOP */
	rtwn_write_4(sc, R92C_EDCA_BE_PARAM, 0x005ea42b);
	rtwn_write_4(sc, R92C_EDCA_BK_PARAM, 0x0000a44f);
	rtwn_write_4(sc, R92C_EDCA_VI_PARAM, 0x005ea324);
	rtwn_write_4(sc, R92C_EDCA_VO_PARAM, 0x002fa226);
}
