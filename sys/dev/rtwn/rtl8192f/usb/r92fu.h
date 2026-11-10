#ifndef RTL8192FU_H
#define RTL8192FU_H

#include <dev/rtwn/rtl8188f/r88f.h>

/*
 * Function declarations
 */

/* r88fu_init.c */
int	r92fu_power_on(struct rtwn_softc *);
void	r92fu_power_off(struct rtwn_softc *);

#endif /* RTL8192FU_H */
