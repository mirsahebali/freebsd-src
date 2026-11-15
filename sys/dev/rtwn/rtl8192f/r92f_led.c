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

#include <dev/rtwn/if_rtwnvar.h>
#include <dev/rtwn/if_rtwnreg.h>

#include <dev/rtwn/rtl8192c/r92c_reg.h>
#include <dev/rtwn/rtl8192c/r92c_reg.h>

#include <dev/rtwn/rtl8192f/r92f.h>
#include <dev/rtwn/rtl8192f/r92f_reg.h>

void
r92f_set_led(struct rtwn_softc *sc, int led, int on)
{
	uint32_t ledcfg;

	/* Values obtained by observing the USB traffic from the Windows driver. */
	rtwn_bb_write(sc, R92F_SW_GPIO_SHARE_CTRL(0), 0x20080);
	rtwn_bb_write(sc, R92F_SW_GPIO_SHARE_CTRL(1), 0x1b0000);

	ledcfg = rtwn_bb_read(sc, R92C_LEDCFG2);

	/* Comfast CF-826F uses LED1. Asus USB-N13 C1 uses LED0. Set both. */
	ledcfg = RW(ledcfg, R92F_LEDCFG0_LED2EN, R92F_LED_GPIO_ENABLE);
	ledcfg = RW(ledcfg, R92F_LEDCFG0_LED0_IO_MODE, R92F_LED_IO_MODE_OUTPUT);
	ledcfg = RW(ledcfg, R92F_LEDCFG0_LED1_IO_MODE, R92F_LED_IO_MODE_OUTPUT);

	if (led == RTWN_LED_LINK) {
		if (!on) {
			ledcfg = RW(ledcfg, R92F_LEDCFG0_LED0CM, R92F_LED_MODE_SW_CTRL);
			ledcfg = RW(ledcfg, R92F_LEDCFG0_LED0SV, R92F_LED_SW_OFF);
			ledcfg = RW(ledcfg, R92F_LEDCFG0_LED1CM, R92F_LED_MODE_SW_CTRL);
			ledcfg = RW(ledcfg, R92F_LEDCFG0_LED1SV, R92F_LED_SW_OFF);
		} else {
			ledcfg = RW(ledcfg, R92F_LEDCFG0_LED0CM, R92F_LED_MODE_SW_CTRL);
			ledcfg = RW(ledcfg, R92F_LEDCFG0_LED0SV, R92F_LED_SW_ON);
			ledcfg = RW(ledcfg, R92F_LEDCFG0_LED1CM, R92F_LED_MODE_SW_CTRL);
			ledcfg = RW(ledcfg, R92F_LEDCFG0_LED1SV, R92F_LED_SW_ON);
		}
		rtwn_write_1(sc, R92C_LEDCFG2, ledcfg);
		sc->ledlink = on;	/* Save LED state. */
	}

	/* XXX: hw traffic controlled led blinking? */
	/*
	*	ledcfg = RW(ledcfg, LEDCFG0_LED0CM, R92F_LED_MODE_TX_OR_RX_EVENTS);
	*	ledcfg = RW(ledcfg, LEDCFG0_LED0SV, R92F_LED_SW_OFF);
	*	ledcfg = RW(ledcfg, LEDCFG0_LED1CM, R92F_LED_MODE_TX_OR_RX_EVENTS);
	*	ledcfg = RW(ledcfg, LEDCFG0_LED1SV, R92F_LED_SW_OFF);
	*/
}
