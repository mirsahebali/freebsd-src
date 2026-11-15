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
#include <net80211/ieee80211_vht.h>

#include <dev/rtwn/if_rtwnreg.h>
#include <dev/rtwn/if_rtwnvar.h>

#include <dev/rtwn/if_rtwn_ridx.h>

#include <dev/rtwn/rtl8812a/r12a.h>
#include <dev/rtwn/rtl8812a/r12a_tx_desc.h>

#include <dev/rtwn/rtl8192f/r92f.h>

#define R92F_TXDW1_MACID_M	0x0000007f
#define R92F_TXDW1_MACID_S	0

void
r92f_tx_setup_macid(void *buf, int id)
{
	struct r12a_tx_desc *txd = buf;
	
	txd->txdw1 |= htole32(SM(R92F_TXDW1_MACID, id));
}

void
r92f_fill_tx_desc(struct rtwn_softc *sc, struct ieee80211_node *ni,
    struct mbuf *m, void *buf, uint8_t ridx, bool force_rate, int maxretry)
{
	struct r12a_tx_desc *txd = buf;
	struct rtwn_node *un = RTWN_NODE(ni);
	int macid;

	macid = un->id;

	if (macid == RTWN_MACID_UNDEFINED)
		macid = 0;

	r12a_fill_tx_desc(sc, ni, m, buf, ridx, force_rate, maxretry);

	txd->txdw1 &= htole32(~R92F_TXDW1_MACID_M);
	txd->txdw1 |= htole32(SM(R92F_TXDW1_MACID, macid));
}
