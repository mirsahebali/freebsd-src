#ifndef R92F_ROM_IMAGE_H
#define R92F_ROM_IMAGE_H

#include <dev/rtwn/rtl8192f/r92f_rom_defs.h>

struct r92f_rom {
	uint8_t reserved1[18];
	struct r92e_tx_pwr tx_pwr[R92F_MAX_RF_PATH];
	uint8_t reserved2[84];
	uint8_t channel_plan;
	uint8_t xtal_k;	
	uint8_t thermal_meter;
	uint8_t iqk_lck;
	uint8_t pa_type;
	uint8_t lna_type_2g;
	uint8_t reserved3[1];
	uint8_t lna_type_5g;
	uint8_t reserved4[1];
	uint8_t rf_board_option;
	uint8_t rf_feature_option;
	uint8_t rf_bt_setting;
	uint8_t eeprom_version;
	uint8_t eeprom_customer_id;
	uint8_t reserved5[3];
	uint8_t rf_antenna_option;
	uint8_t rfe_option;
	uint8_t country_code;
	uint8_t reserved6[52];
	uint8_t vid[2];
	uint8_t pid[2];
	uint8_t usb_optional_function;
	uint8_t reserved7[2];
	uint8_t mac_addr[ETH_ALEN];
	uint8_t device_info[80];
	uint8_t reserved8[163];
} __packed;

_Static_assert(sizeof(struct r92f_rom) == R92F_EFUSE_MAP_LEN,
    "R92F_EFUSE_MAP_LEN must be equal to sizeof(struct r92f_rom)!");

#endif /* R92F_ROM_IMAGE_H */
