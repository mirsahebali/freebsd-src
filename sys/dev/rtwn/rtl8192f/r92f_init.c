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

#include <dev/rtwn/rtl8192c/r92c.h>
#include <dev/rtwn/rtl8192c/r92c_reg.h>

#include <dev/rtwn/rtl8192e/r92e_reg.h>

#include <dev/rtwn/rtl8192f/r92f.h>
#include <dev/rtwn/rtl8192f/r92f_var.h>

#define R92F_XTAL1_M	0x0000007e
#define R92F_XTAL1_S	1
#define R92F_XTAL0_M	0x7e000000
#define R92F_XTAL0_S	25

static void
r92f_set_crystal_cap(struct rtwn_softc *sc)
{
	struct r92f_softc *rs = sc->sc_priv;
	uint8_t cap;
	uint32_t xtal0, xtal1;

	cap = rs->crystalcap & 0x3f;

	xtal0 = rtwn_read_4(sc, R92C_AFE_XTAL_CTRL);
	xtal1 = rtwn_read_4(sc, R92C_AFE_PLL_CTRL);

	RTWN_DPRINTF(sc, RTWN_DEBUG_USB,
		"%s: Adjusting crystal cap from 0x%x (actually 0x%x 0x%x) to 0x%x\n",
		__func__,
		rs->crystalcap,
		MS(xtal1, R92F_XTAL1),
		MS(xtal0, R92F_XTAL0),
		cap);

	xtal0 = RW(xtal0, R92F_XTAL0, cap);
	xtal1 = RW(xtal1, R92F_XTAL1, cap);
	rtwn_write_4(sc, R92C_AFE_PLL_CTRL, xtal1);
	rtwn_write_4(sc, R92C_AFE_XTAL_CTRL, xtal0);
}

static void
r92f_init_bb_common(struct rtwn_softc *sc)
{
	r92c_init_bb_common(sc);

	r92f_set_crystal_cap(sc);
}

void
r92f_init_bb(struct rtwn_softc *sc)
{
	/* Enable BB and RF */
	rtwn_bb_setbits(sc, R92C_SYS_FUNC_EN, 0,
	    R92C_SYS_FUNC_EN_BBRSTB | R92C_SYS_FUNC_EN_BB_GLB_RST);

	rtwn_write_1(sc, R92C_RF_CTRL, R92C_RF_CTRL_EN | R92C_RF_CTRL_RSTB |
	    R92C_RF_CTRL_SDMRSTB);

	/* To Fix MAC loopback mode fail. */
	rtwn_write_1(sc, R92C_LDOHCI12_CTRL, 0xf);
	rtwn_write_1(sc, R92E_LDOV12_CTRL + 1, 0xe9);

	rtwn_delay(sc, 10);

	r92f_init_bb_common(sc);
	/* Enable TX/RX for path A and B */
	rtwn_bb_setbits(sc, R92C_OFDM0_TRXPATHENA, 0xff, 0x33);
}

