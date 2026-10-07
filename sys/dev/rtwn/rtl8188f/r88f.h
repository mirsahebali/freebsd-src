#ifndef RTL8188F_H
#define RTL8188F_H

#include <dev/rtwn/if_rtwn_ridx.h>

/*
 * Global definitions.
 */
#define R88F_CHIP_B_CUT 0x04
/* 
 * Function declarations.
 */

/* r88f_rom.c */
int	r88f_efuse_preread(struct rtwn_softc *);
void	r88f_parse_rom(struct rtwn_softc *, uint8_t *);

/* r88f_rx.c */
int8_t	r88f_get_rssi_cck(struct rtwn_softc *, void *);
int8_t	r88f_get_rssi_ofdm(struct rtwn_softc *, void *);

/* r88f_tx.c */
void	r88f_init_tx_agg(struct rtwn_softc *);

/* r88f_init.c */
int	r88f_set_page_size(struct rtwn_softc *);
void	r88f_init_ampdu(struct rtwn_softc *sc);
int	r88f_llt_init(struct rtwn_softc *sc);
void	r88f_init_edca(struct rtwn_softc *sc);

/* r88f_calib.c */
void	r88f_lc_calib(struct rtwn_softc *);
void	r88f_iq_calib(struct rtwn_softc *);

/* r88f_chan.c */
void	r88f_set_chan(struct rtwn_softc *, struct ieee80211_channel *);
void	r88f_set_bw20(struct rtwn_softc *, uint8_t);
void	r88f_get_txpower(struct rtwn_softc *, int, struct ieee80211_channel *, uint8_t power[RTWN_RIDX_COUNT]);
int	r88f_set_tx_power(struct rtwn_softc *, struct ieee80211vap *);

/* r88f_fw.c */
void r88f_fw_reset(struct rtwn_softc *, int);
void r88f_fw_download_enable(struct rtwn_softc *, int);

/* r88f_rf.c */
void	r88f_rf_write(struct rtwn_softc *, int, uint8_t, uint32_t);

#endif // !RTL8188F_H
