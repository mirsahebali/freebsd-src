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
#define R92F_RX_DMA_BUFFER_SIZE	0x3f3f
#define R92F_MAX_FW_SIZE	31136
#define R92F_RTWN_POWER_COUNT	28
#define R92F_FW_START_ADDR	0x4000

/*
 * Function declarations.
 */

/* r92f_calib.c */
void	r92f_lc_calib(struct rtwn_softc *);

/* r92f_chan.c */

/* r92f_rom.c */
void	r92f_parse_rom(struct rtwn_softc *, uint8_t *);

/* r92f_fw.c */
void	r92f_fw_reset(struct rtwn_softc *, int);
void	r92f_fw_download_enable(struct rtwn_softc *, int);

/* r92f_led.c */
void	r92f_set_led(struct rtwn_softc *, int, int);

#endif /* RTL8192F_H */
