/*
 * Copyright (c) 2026 Mir Saheb Ali <mirsahebali@pm.me>
 *
 * SPDX-License-Identifier: BSD-2-Clause
 */

#ifndef R88F_REG_H
#define R88F_REG_H

/* System Configuration. */
#define R88F_HQ_NPAGES		12
#define R88F_LQ_NPAGES		2
#define R88F_NQ_NPAGES		2
#define R88F_TXPKTBUF_COUNT	177
#define R88F_TX_PAGE_COUNT	247
#define R88F_RX_DMA_BUFFER_SIZE	0x3f80

#define R88F_OFDM0_FA_RSTC		0xc0c
#define R88F_NHM_TH9_TH10_8723B		0x890
#define R88F_NHM_TIMER_8723B		0x894
#define R88F_NHM_TH3_TO_TH0_8723B	0x898
#define R88F_NHM_TH7_TO_TH4_8723B	0x89c
#define R88F_OFDM0_RX_D_SYNC_PATH	0xc40
#define R88F_FPGA0_XCD_RF_SW_CTRL	0x0874
#define R88F_CCK_PD_THRESH		0x0a0a

#define R88F_ADDA_REGS			16
#define R88F_MAC_REGS			4
#define R88F_BB_REGS			9

/* Baseband registers. */
#define R88F_RX_DFIR		0x954

/* RF registers. */
#define R88F_RF_RXG_GAIN 0x87

/*
 * RF (6052) registers
 */
#define R88F_RF6052_REG_GAIN_CCA	0xdf
#define R88F_RF6052_REG_UNKNOWN_55	0x55
#define R88F_RF6052_REG_PAD_TXG		0x56

#define R88F_EFUSE_CTRL 0x0030

/* Bits for R88F_RF_RXG_GAIN. */
#define R88F_RF_TXA_PREPAD 0x65

/* Bits for R88F_RF6052_REG_GAIN_CCA. */
#define R88F_REG_PKTBUF_DBG_CTRL 0x140

/* Bits for R88F_HT_SINGLE_AMPDU. */
#define R88F_HT_SINGLE_AMPDU_EN	0x80

/* Bits for R88F_CCK_PD_THRESH. */
#define R88F_CCK_PD_TYPE1_LV1_TH	0x83

#define R88F_MAX_FW_SIZE 21020

#endif // !R88F_REG_H
