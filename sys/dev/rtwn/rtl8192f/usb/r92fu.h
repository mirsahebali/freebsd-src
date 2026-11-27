/*
 * Copyright (c) 2026 Mir Saheb Ali <mirsahebali@pm.me>
 *
 * SPDX-License-Identifier: BSD-2-Clause
 */

#ifndef RTL8192FU_H
#define RTL8192FU_H

#define R92F_EFUSE_MAP_LEN	512

#include <dev/rtwn/rtl8192f/r92f.h>

/*
 * Function declarations
 */

/* r92fu_init.c */
int	r92fu_power_on(struct rtwn_softc *);
void	r92fu_power_off(struct rtwn_softc *);
int	r92fu_set_qmap(struct rtwn_softc *);
void	r92fu_init_rx_agg(struct rtwn_softc *);
void	r92fu_init_tx_agg(struct rtwn_softc *);
void	r92fu_init_ampdu(struct rtwn_softc *);
void	r92fu_post_init(struct rtwn_softc *);
void	r92fu_init_antsel(struct rtwn_softc *);

#endif /* RTL8192FU_H */
