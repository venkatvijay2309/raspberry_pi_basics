#ifndef R8101_H
#define R8101_H

#ifdef __KERNEL__

#include <asm/io.h> /* io* */

#define REG_R8(reg) (ioread8(reg_base + reg))
#define REG_R16(reg) (ioread16(reg_base + reg))
#define REG_R32(reg) (ioread32(reg_base + reg))
#define REG_W8(val, reg) (iowrite8(val, reg_base + reg))
#define REG_W16(val, reg) (iowrite16(val, reg_base + reg))
#define REG_W32(val, reg) (iowrite32(val, reg_base + reg))

#define R8101_REGS_SIZE 256

/* Symbolic offsets to registers. */
enum RTL8101Registers {
	MAC0 = 0,		/* Ethernet hardware address. */
	MAC4 = 0x04,
	MAR0 = 8,		/* Multicast filter. */
	CounterAddrLow = 0x10,
	CounterAddrHigh = 0x14,
	TxDescStartAddrLow = 0x20,
	TxDescStartAddrHigh = 0x24,
	TxHDescStartAddrLow = 0x28,
	TxHDescStartAddrHigh = 0x2c,
	ERSR = 0x36,
	ChipCmd = 0x37,
	TxPoll = 0x38,
	IntrMask = 0x3C,
	IntrStatus = 0x3E,
	TxConfig = 0x40,
	RxConfig = 0x44,
//	RxMissed = 0x4C,
	TCTR = 0x48,
	Cfg9346 = 0x50,
	Config0 = 0x51,
	Config1 = 0x52,
	Config2 = 0x53,
	Config3 = 0x54,
	Config4 = 0x55,
	Config5 = 0x56,
	TDFNR	= 0x57,
	TimeIntr = 0x58,
	PHYAR = 0x60,
	CSIDR = 0x64,
	CSIAR = 0x68,
	PHYstatus = 0x6C,
	MACDBG = 0x6D,
	GPIO = 0x6E,
	PMCH = 0x6F,
	ERIDR = 0x70,
	ERIAR = 0x74,
	EPHYAR = 0x80,
	OCPDR = 0xB0,
	MACOCP = 0xB0,
	OCPAR = 0xB4,
	PHYOCP = 0xB8,
	DBG_reg = 0xD1,
	MCUCmd_reg = 0xD3,
	RxMaxSize = 0xDA,
	CPlusCmd = 0xE0,
	IntrMitigate = 0xE2,
	RxDescAddrLow = 0xE4,
	RxDescAddrHigh = 0xE8,
	MTPS = 0xEC,
	PHYIO = 0xF8,
};

enum DescStatusBits {
	DescOwn		= (1 << 31), /* Descriptor is owned by NIC */
	RingEnd		= (1 << 30), /* End of descriptor ring */
	FirstFrag	= (1 << 29), /* First segment of a packet */
	LastFrag	= (1 << 28), /* Final segment of a packet */

	/* Tx private */
	/*------ offset 0 of tx descriptor ------*/
	LargeSend	= (1 << 27), /* TCP Large Send Offload (TSO) */
	MSSShift	= 16,		 /* MSS value position */
	MSSMask		= 0x7FFU,	 /* MSS value 11 bits */
	TxIPCS		= (1 << 18), /* Calculate IP checksum */
	TxUDPCS		= (1 << 17), /* Calculate UDP/IP checksum */
	TxTCPCS		= (1 << 16), /* Calculate TCP/IP checksum */
	TxVlanTag	= (1 << 17), /* Add VLAN tag */

	/* RxStatusDesc */
	RxRWT		= (1 << 22),
	RxRES		= (1 << 21),
	RxRUNT		= (1 << 20),
	RxCRC		= (1 << 19),

	/* Rx private */
	/*------ offset 0 of rx descriptor ------*/
	PID1		= (1 << 18), /* Protocol ID bit 1/2 */
	PID0		= (1 << 17), /* Protocol ID bit 2/2 */

#define RxProtoUDP	(PID1)
#define RxProtoTCP	(PID0)
#define RxProtoIP	(PID1 | PID0)
#define RxProtoMask	RxProtoIP

	RxIPF		= (1 << 16), /* IP checksum failed */
	RxUDPF		= (1 << 15), /* UDP/IP checksum failed */
	RxTCPF		= (1 << 14), /* TCP/IP checksum failed */
	RxVlanTag	= (1 << 16), /* VLAN tag available */
};

enum ClearBitMasks {
	MultiIntrClear	= 0xF000,
	ChipCmdClear	= 0xE2,
	Config1Clear	= (1<<7)|(1<<6)|(1<<3)|(1<<2)|(1<<1)
};

enum ChipCmdBits {
	StopReq		= 0x80,
	CmdReset	= 0x10,
	CmdRxEnb	= 0x08,
	CmdTxEnb	= 0x04,
	RxBufEmpty	= 0x01
};

enum Cfg9346Bits {
	Cfg9346_Lock	= 0x00,
	Cfg9346_Unlock	= 0xC0
};

enum IntrStatusBits {
	PCIErr		= 0x8000,
	PCSTimeout	= 0x4000,
	SWInt		= 0x0100,
	TxDescUnavail = 0x80,
	RxFIFOOver	= 0x40,
	LinkChg		= 0x20,
	RxDescUnavail = 0x10,
	TxErr		= 0x08,
	TxOK		= 0x04,
	RxErr		= 0x02,
	RxOK		= 0x01,

	IntrEnMask	= RxDescUnavail | TxOK | RxOK | SWInt
};

enum TxStatusBits {
	TxHostOwns	= 0x2000,
	TxUnderrun	= 0x4000,
	TxStatOK	= 0x8000,
	TxOutOfWindow	= 0x20000000,
	TxAborted	= 0x40000000,
	TxCarrierLost	= 0x80000000
};

enum RxStatusBits {
	RxMulticast	= 0x8000,
	RxPhysical	= 0x4000,
	RxBroadcast	= 0x2000,
	RxBadSymbol	= 0x0020,
	RxRunt		= 0x0010,
	RxTooLong	= 0x0008,
	RxCRCErr	= 0x0004,
	RxBadAlign	= 0x0002,
	RxStatusOK	= 0x0001
};

enum TxConfigBits {
	/* Interframe Gap Time. Only TxIFG96 doesn't violate IEEE 802.3 */
	TxIFGShift	= 24,
	TxIFG84		= (0 << TxIFGShift), /* 8.4us / 840ns (10 / 100Mbps) */
	TxIFG88		= (1 << TxIFGShift), /* 8.8us / 880ns (10 / 100Mbps) */
	TxIFG92		= (2 << TxIFGShift), /* 9.2us / 920ns (10 / 100Mbps) */
	TxIFG96		= (3 << TxIFGShift), /* 9.6us / 960ns (10 / 100Mbps) */

	TxLoopBack	= (1 << 18) | (1 << 17), /* Enable loopback test mode */
	TxCRC		= (1 << 16),	/* DISABLE Tx pkt CRC append */
	TxClearAbt	= (1 << 0),	/* Clear abort (WO) */

	/* Max DMA burst */
	TxDMAShift	= 8, /* DMA burst value (0-6) is shifted this many bits */
	TxDMAUnlimited = (6 << TxDMAShift),

	TxRetryShift	= 4, /* TXRR value (0-15) is shifted this many bits */

	TxVersionMask	= 0x7C800000 /* mask out version bits 30-26, 23 */
};

enum RxConfigBits {
	/* rx fifo threshold */
	RxFIFOShift	= 13,
	RxFIFONone	= (7 << RxFIFOShift),

	/* Max DMA burst */
	RxDMAShift	= 8, /* DMA burst value (0-6) is shifted this many bits */
	RxDMAUnlimited = (6 << RxDMAShift),

	/* rx ring buffer length */
	RxRcv8K	= 0,
	RxRcv16K	= (1 << 11),
	RxRcv32K	= (1 << 12),
	RxRcv64K	= (1 << 11) | (1 << 12),
	/* Disable packet wrap at end of Rx buffer. (not possible with 64k) */
	RxNoWrap	= (1 << 7),
	Rx9356SEL = (1 << 6),

	AcceptErr	= 0x20,
	AcceptRunt	= 0x10,
	AcceptBroadcast	= 0x08,
	AcceptMulticast	= 0x04,
	AcceptMyPhys	= 0x02,
	AcceptAllPhys	= 0x01,

	RxConfigMask	= 0xFF7E1880
};

enum TxPrioPoll {
	/* Transmit Priority Polling*/
	HPQ = 0x80,
	NPQ = 0x40,
	FSWInt = 0x01
};

#endif

#endif
