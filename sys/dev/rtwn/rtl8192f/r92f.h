#ifndef RTL8192F_H
#define RTL8192F_H

#include <dev/rtwn/if_rtwn_ridx.h>

/*
 * Global Definitions.
 */
#define R92F_HQ_NPAGES		8
#define R92F_LQ_NPAGES		8
#define R92F_NQ_NPAGES		8
#define R92F_TXPKTBUF_COUNT	177
#define R92F_TX_PAGE_COUNT	247
#define R92F_RX_DMA_BUFFER_SIZE	0x3f40
#define R92F_MAX_FW_SIZE	31136
#define R92F_RTWN_POWER_COUNT	28
#define R92F_FW_START_ADDR	0x4000
#define R92F_MACID_MAX	128

/*
 * Function declarations.
 */

/* r92f_calib.c */
void	r92f_lc_calib(struct rtwn_softc *);
void	r92f_iq_calib(struct rtwn_softc *);

/* r92f_chan.c */
int	r92f_set_tx_power(struct rtwn_softc *, struct ieee80211vap *);
void	r92f_set_chan(struct rtwn_softc *, struct ieee80211_channel *);

/* r92f_init.c */
void	r92f_init_bb(struct rtwn_softc *);

/* r92f_rom.c */
void	r92f_parse_rom(struct rtwn_softc *, uint8_t *);

/* r92f_rx.c */
int8_t	r92f_get_rssi_cck(struct rtwn_softc *, void *);
int8_t	r92f_get_rssi_ofdm(struct rtwn_softc *, void *);
void	r92f_get_rx_stats(struct rtwn_softc *, struct ieee80211_rx_stats *,
    const void *, const void *);

/* r92f_tx.c */
void	r92f_tx_setup_macid(void *, int);
void	r92f_fill_tx_desc(struct rtwn_softc *, struct ieee80211_node *,
    struct mbuf *, void *, uint8_t, bool, int);

/* r92f_fw.c */
void	r92f_fw_reset(struct rtwn_softc *, int);
void	r92f_fw_download_enable(struct rtwn_softc *, int);

/* r92f_led.c */
void	r92f_set_led(struct rtwn_softc *, int, int);

#endif /* RTL8192F_H */
