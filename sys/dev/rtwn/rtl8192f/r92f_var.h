#ifndef R92F_VAR_H
#define R92F_VAR_H

#include <dev/rtwn/rtl8192c/r92c_var.h>
#include <dev/rtwn/rtl8192f/r92f_rom_defs.h>

struct r92f_softc {
	struct r92c_softc super;
	int8_t cck_tx_pwr[R92F_MAX_RF_PATH][R92E_GROUP_2G];
	int8_t ht40_tx_pwr_2g[R92F_MAX_RF_PATH][R92E_GROUP_2G];

	int8_t cck_tx_pwr_diff_2g[R92F_MAX_RF_PATH] [R92E_MAX_TX_COUNT];
	int8_t ofdm_tx_pwr_diff_2g[R92F_MAX_RF_PATH][R92E_MAX_TX_COUNT];
	int8_t bw20_tx_pwr_diff_2g[R92F_MAX_RF_PATH][R92E_MAX_TX_COUNT];
	int8_t bw40_tx_pwr_diff_2g[R92F_MAX_RF_PATH][R92E_MAX_TX_COUNT];

	uint8_t regulatory;
	uint8_t crystalcap;
	uint8_t rfe;


	void	(*rs_tx_setup_macid)(void *, int);
};

#endif /* R92F_VAR_H */
