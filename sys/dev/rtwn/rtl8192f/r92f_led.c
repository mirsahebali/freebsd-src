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
#include <sys/taskqueue.h> #include <sys/bus.h>
#include <sys/endian.h>
#include <sys/linker.h>

#include <net/if.h>
#include <net/ethernet.h>
#include <net/if_media.h>

#include <net80211/ieee80211_var.h>
#include <net80211/ieee80211_radiotap.h>

#include <dev/rtwn/if_rtwnvar.h>

#include <dev/rtwn/rtl8192c/r92c_reg.h>
#include <dev/rtwn/rtl8192c/r92c_reg.h>

// XXX: rename and move it to r92f_reg.h
#define REG_SW_GPIO_SHARE_CTRL_0	0x1038
#define REG_SW_GPIO_SHARE_CTRL_1	0x103c

#define LED_GPIO_ENABLE	0x1
#define LEDCFG0_LED2EN_M	0x00200000
#define LEDCFG0_LED2EN_S	21

#define LEDCFG0_LED0_IO_MODE_M	0x80
#define LEDCFG0_LED0_IO_MODE_S	7

#define LEDCFG0_LED1_IO_MODE_M	0x8000
#define LEDCFG0_LED1_IO_MODE_S	15

#define LEDCFG0_LED0CM_M	0x7
#define LEDCFG0_LED0CM_S	0

#define LEDCFG0_LED1CM_M	0x700
#define LEDCFG0_LED1CM_S	8

#define LEDCFG0_LED0SV_M	0x8
#define LEDCFG0_LED0SV_S	3

#define LEDCFG0_LED1SV_M	0x800
#define LEDCFG0_LED1SV_S	11

#define LED_MODE_SW_CTRL		0x0
#define LED_SW_OFF			0x0
#define LED_SW_ON			0x1

#define LED_MODE_TX_OR_RX_EVENTS	0x3


void
r92f_set_led(struct rtwn_softc *sc, int led, int on)
{
	uint32_t ledcfg;

	/* Values obtained by observing the USB traffic from the Windows driver. */
	rtwn_bb_write(sc, REG_SW_GPIO_SHARE_CTRL_0, 0x20080);
	rtwn_bb_write(sc, REG_SW_GPIO_SHARE_CTRL_1, 0x1b0000);

	ledcfg = rtwn_bb_read(sc, R92C_LEDCFG2);

	/* Comfast CF-826F uses LED1. Asus USB-N13 C1 uses LED0. Set both. */

	ledcfg = RW(ledcfg, LEDCFG0_LED2EN, LED_GPIO_ENABLE);
	ledcfg = RW(ledcfg, LEDCFG0_LED0_IO_MODE, LED_IO_MODE_OUTPUT);
	ledcfg = RW(ledcfg, LEDCFG0_LED1_IO_MODE, LED_IO_MODE_OUTPUT);

	if (led == RTWN_LED_LINK) {
		if (!on) {
			ledcfg = RW(ledcfg, LEDCFG0_LED0CM, LED_MODE_SW_CTRL);
			ledcfg = RW(ledcfg, LEDCFG0_LED0SV, LED_SW_OFF);
			ledcfg = RW(ledcfg, LEDCFG0_LED1CM, LED_MODE_SW_CTRL);
			ledcfg = RW(ledcfg, LEDCFG0_LED1SV, LED_SW_OFF);
		} else {
			ledcfg = RW(ledcfg, LEDCFG0_LED0CM, LED_MODE_SW_CTRL);
			ledcfg = RW(ledcfg, LEDCFG0_LED0SV, LED_SW_ON);
			ledcfg = RW(ledcfg, LEDCFG0_LED1CM, LED_MODE_SW_CTRL);
			ledcfg = RW(ledcfg, LEDCFG0_LED1SV, LED_SW_ON);
		}
		rtwn_write_1(sc, R92C_LEDCFG2, ledcfg);
		sc->ledlink = on;	/* Save LED state. */
	}

	/* XXX: hw controlled led blinking */
	/*
	*	ledcfg = RW(ledcfg, LEDCFG0_LED0CM, LED_MODE_TX_OR_RX_EVENTS);
	*	ledcfg = RW(ledcfg, LEDCFG0_LED0SV, LED_SW_OFF);
	*	ledcfg = RW(ledcfg, LEDCFG0_LED1CM, LED_MODE_TX_OR_RX_EVENTS);
	*	ledcfg = RW(ledcfg, LEDCFG0_LED1SV, LED_SW_OFF);
	*/
}
