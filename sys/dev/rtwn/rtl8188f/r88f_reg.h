#ifndef R88F_REG_H
#define R88F_REG_H

/* System Configuration. */
#define R88F_HT_SINGLE_AMPDU	0x4c7
#define R88F_AMPDU_MAX_TIME	0x456
#define R88F_RX_PKT_LIMIT	0x60c
#define R88F_USTIME_EDCA	0x638

#define R88F_HQ_NPAGES		12
#define R88F_LQ_NPAGES		2
#define R88F_NQ_NPAGES		2
#define R88F_TXPKTBUF_COUNT	177
#define R88F_TX_PAGE_COUNT	247
#define R88F_RX_DMA_BUFFER_SIZE	0x3f80

/* Baseband registers. */
#define R88F_RX_DFIR		0x954

/* RF registers. */
#define R88F_RF_RXG_GAIN 0x87

/* NextGen regs. */
#define R88F_RF6052_REG_GAIN_CCA 0xdf

/* Bits for R88F_RF_RXG_GAIN. */
#define R88F_RF_TXA_PREPAD 0x65

/* Bits for R88F_RF6052_REG_GAIN_CCA. */
#define R88F_REG_PKTBUF_DBG_CTRL 0x0140

/* Bits for R88F_HT_SINGLE_AMPDU. */

#define R88F_HT_SINGLE_AMPDU_EN	0x80

#endif // !R88F_REG_H
