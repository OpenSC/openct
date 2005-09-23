#ifndef	_CARDMAN_H_
#define	_CARDMAN_H_

#define	MAX_ATR			33

#define	CM2020_MAX_DEV		16
#define	CM4000_MAX_DEV		4

typedef struct atreq {
	int atr_len;
	unsigned char atr[64];
	int power_act;
	unsigned char bIFSD;
	unsigned char bIFSC;
} atreq_t;

typedef struct ptsreq {
	unsigned long protocol;	/*T=0: 2^0, T=1:  2^1 */
	unsigned char flags;
	unsigned char pts1;
	unsigned char pts2;
	unsigned char pts3;
} ptsreq_t;

#define	CM_IOC_MAGIC		'c'
#define	CM_IOC_MAXNR	        255

#define	CM_IOCGSTATUS		_IOR (CM_IOC_MAGIC, 0, unsigned char *)
#define	CM_IOCGATR		_IOWR(CM_IOC_MAGIC, 1, atreq_t *)
#define	CM_IOCSPTS		_IOW (CM_IOC_MAGIC, 2, ptsreq_t *)
#define	CM_IOCSRDR		_IO  (CM_IOC_MAGIC, 3)
#define CM_IOCARDOFF            _IO  (CM_IOC_MAGIC, 4)

#define CM_IOSDBGLVL            _IOW(CM_IOC_MAGIC, 250, int*)

/* card and device states */
#define	CM_CARD_INSERTED		0x01
#define	CM_CARD_POWERED			0x02
#define	CM_ATR_PRESENT			0x04
#define	CM_ATR_VALID	 		0x08
#define	CM_STATE_VALID			0x0f
/* extra info only from CM4000 */
#define	CM_NO_READER			0x10
#define	CM_BAD_CARD			0x20

#ifdef __KERNEL__

/* USB can have 16 readers, while PCMCIA is allowed 4 slots */

#define	CM4000_MAX_DEV		        4

#ifdef	__CM2020__

#define	MODULE_NAME		"cardman_usb"

#define	CM2020_MAX_DEV		        16
#define	CM2020_MINOR		        224

#define	CM2020_REQT_WRITE		0x42
#define	CM2020_REQT_READ		0xc2

#define	CM2020_MODE_1			0x01
#define	CM2020_MODE_2			0x02
#define	CM2020_MODE_3			0x03
#define	CM2020_MODE_4			0x08
#define	CM2020_CARD_ON			0x10
#define	CM2020_CARD_OFF			0x11
#define	CM2020_GET_STATUS		0x20
#define	CM2020_STATUS_MASK		0xc0
#define	CM2020_STATUS_NO_CARD		0x00
#define	CM2020_STATUS_NOT_POWERD	0x40
#define	CM2020_STATUS_POWERD		0xc0
#define CM2020_SET_PARAMETER		0x30

#define CM2020_CARDON_COLD              0x00
#define CM2020_CARDON_WARM              0x01

#define CM2020_FREQUENCY_3_72MHZ        0x00
#define CM2020_FREQUENCY_5_12MHZ        0x10

#define CM2020_BAUDRATE_115200          0x0C
#define CM2020_BAUDRATE_76800           0x08
#define CM2020_BAUDRATE_57600           0x06
#define CM2020_BAUDRATE_38400           0x04
#define CM2020_BAUDRATE_28800           0x03
#define CM2020_BAUDRATE_19200           0x02
#define CM2020_BAUDRATE_9600            0x01

#define CM2020_ODD_PARITY               0x80

#define CM2020_CARD_ASYNC               0x00

enum {
	CB_NOP,
	CB_SET_PARAMETER,
	CB_READ_STATUS,
	CB_READ_ATR,
	CB_WRITE_PTS,
	CB_READ_PTS,
	CB_WRITE_T1,
	CB_PROG_T1,
	CB_READ_T1,
	CB_WRITE_T0,
	CB_WRITE_T0_SW1SW2,
	CB_READ_T0,
	CB_READ_T0_DATA,
	CB_CARD_OFF,
	CB_T1MODE2
};

#define	TIMEOUT_LEN		60000
#define	MAX_RBUF		512

typedef struct usb_cardman {

	struct usb_device *dev;
#if LINUX_VERSION_CODE >= KERNEL_VERSION(2,6,0)
	struct usb_interface *interface;	/* the interface for this device */
#endif
	struct task_struct *owner;
#if LINUX_VERSION_CODE >= KERNEL_VERSION(2,4,20)
	struct usb_ctrlrequest *dr;
#else
	devrequest *dr;
#endif
	struct urb *irq, *ctrl, *rctl;
	unsigned char *ibuf, *cbuf, *rcbuf;
	wait_queue_head_t waitq;

	unsigned char atr[MAX_ATR];
	unsigned char atr_csum;
	unsigned char atr_len;
	unsigned char bIFSD, bIFSC;
	unsigned char ta1;	/* TA(1) specifies Fi over b8 to b5, Di over b4 to b1 */
	unsigned char pts[4];

	unsigned char rbuf[MAX_RBUF];
	short rlen;

	int t1_reply_len;

	/* length of a T=0 packet, excl. the header length */
	unsigned char t0_data_len;

	/* relative data offset as we proceed through the packet */
	unsigned char t0_data_off;

	/* byte 2 of the T=0 header (INS from CLA INS ADR...) */
	unsigned char t0_ins;

	/* length of T=0 reply we expcet. 2 for a WriteT0, else
	 * ReadT0 length + 2 (Sw1 Sw2)
	 */
	unsigned short t0_expected_reply_len;

	int bInterval;
	unsigned char ctrlendp;
	unsigned char intendp;
	unsigned char card_state;
	int flags;
	int op;
	unsigned char proto;
	int ttl, ttl_hi,	/* CWT */
	 bwt,			/* BWT */
	 ptsttl;		/* PTS retry */
#if LINUX_VERSION_CODE >= KERNEL_VERSION(2,6,0)
	int open;
	int present;
	struct semaphore sem;
	int minor;
#endif
} usb_cardman_t;
#endif				/* __CM2020__ */

#ifdef	__CM4000__

#define	DEVICE_NAME		"cmm"
#define	MODULE_NAME		"cardman_cs"

/* unofficial CM4000 ioctl */
#define CM4000_IOCMONITOR      _IO (CM_IOC_MAGIC, 251)
#define CM4000_IOCDUMPATR      _IO (CM_IOC_MAGIC, 252)
#define CM4000_IOCDECUSECOUNT  _IO (CM_IOC_MAGIC, 253)
#define CM4000_IOCPOWERON      _IO (CM_IOC_MAGIC, 254)
#define CM4000_IOCGIOADDR      _IOW(CM_IOC_MAGIC, 255, int*)

#endif				/* __CM4000__ */

#endif				/* __KERNEL__ */
#endif				/* _CARDMAN_H_ */
