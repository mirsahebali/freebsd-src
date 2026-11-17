#ifndef RTL8192FU_H
#define RTL8192FU_H

#include <dev/rtwn/rtl8192f/r92f.h>

/*
 * Function declarations
 */

/* r92fu_init.c */
int	r92fu_power_on(struct rtwn_softc *);
void	r92fu_power_off(struct rtwn_softc *);
int	r92fu_set_qmap(struct rtwn_softc *);
void	r92fu_init_rx_agg(struct rtwn_softc *);
void	r92fu_init_intr(struct rtwn_softc *);
void	r92fu_init_ampdu(struct rtwn_softc *);

#endif /* RTL8192FU_H */
