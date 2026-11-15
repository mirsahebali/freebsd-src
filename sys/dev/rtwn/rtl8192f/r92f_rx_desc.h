#ifndef R92F_RX_DESC_H
#define R92F_RX_DESC_H

#define R92F_RXDW0_PAGE_NUM_M	0x000000ff
#define R92F_RXDW0_PAGE_NUM_S	0

#define R92F_RXDW0_PWDB0_M	0x0000ff00
#define R92F_RXDW0_PWDB0_S	8

#define R92F_RXDW0_PWDB1_M	0x00ff0000
#define R92F_RXDW0_PWDB1_S	16

#define R92F_RXDW0_PWDB2_M	0xff000000
#define R92F_RXDW0_PWDB2_S	24

#define R92F_RXDW1_PWDB3_M	0x000000ff
#define R92F_RXDW1_PWDB3_S	0

#define R92F_RXDW3_VGA_M	0x00001f00
#define R92F_RXDW3_VGA_S	8

#define R92F_RXDW3_LNA_L_M	0x0000e000
#define R92F_RXDW3_LNA_L_S	13
#define R92F_RXDW3_LNA_H_M	0x00800000
#define R92F_RXDW3_LNA_H_S	23
#define R92F_RXDW4_SPLCP	0x00000001
#define R92F_RXDW4_BW_M		0x00000030
#define R92F_RXDW4_BW_S		4
#define R92F_RXDW3_MCS_M	0x0000007f
#define R92F_RXDW3_MCS_S	0

struct r92f_rx_phystat {
	uint32_t rxdw0;
	uint32_t rxdw1;
	uint32_t rxdw2;
	uint32_t rxdw3;
	uint32_t rxdw4;
	uint32_t rxdw5;
	uint32_t rxdw6;
} __packed;

#endif // !R92F_RX_DESC_H
