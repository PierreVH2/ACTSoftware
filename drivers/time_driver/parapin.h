

/*
 * parapin -- parallel port pin control library
 *
 */

#ifndef _PARAPIN_H_
#define _PARAPIN_H_

/* Define our ioctl commands.  We choose the magic number 0x96 */
#define PPDRV_IOC_MAGIC (0x96)

#define PPDRV_IOC_PINMODE_OUT       _IOW(PPDRV_IOC_MAGIC, 0, int)
#define PPDRV_IOC_PINMODE_IN        _IOW(PPDRV_IOC_MAGIC, 1, int)
#define PPDRV_IOC_PINSET            _IOW(PPDRV_IOC_MAGIC, 2, int)
#define PPDRV_IOC_PINCLEAR          _IOW(PPDRV_IOC_MAGIC, 3, int)
#define PPDRV_IOC_PINGET            _IOWR(PPDRV_IOC_MAGIC, 4, int)

/* functions common to both userspace and the kernel versions */

#define LP_CLEAR 0 /* for change_pin */
#define LP_SET   1 /* for change_pin */
#define LP_INPUT  0 /* for pin_mode */
#define LP_OUTPUT 1 /* for pin_mode */


/* This maps parallel port pin numbers to register bit numbers.
 * - shifts of 0..7   represent bits 0..7 of LPBASE + 0 (data register)
 * - shifts of 8..15  represent bits 0..7 of LPBASE + 1 (status register)
 * - shifts of 16..23 represent bits 0..7 of LPBASE + 2 (control register)
 */

#define LP_PIN01 (1 << 16)
#define LP_PIN02 (1 << 0)
#define LP_PIN03 (1 << 1)
#define LP_PIN04 (1 << 2)
#define LP_PIN05 (1 << 3)
#define LP_PIN06 (1 << 4)
#define LP_PIN07 (1 << 5)
#define LP_PIN08 (1 << 6)
#define LP_PIN09 (1 << 7)
#define LP_PIN10 (1 << 14)
#define LP_PIN11 (1 << 15)
#define LP_PIN12 (1 << 13)
#define LP_PIN13 (1 << 12)
#define LP_PIN14 (1 << 17)
#define LP_PIN15 (1 << 11)
#define LP_PIN16 (1 << 18)
#define LP_PIN17 (1 << 19)
#undef  LP_PIN18
#undef  LP_PIN19 /* pins 18..25 are commoned to signal ground */
#undef  LP_PIN20 /* (not controllable) */
#undef  LP_PIN21
#undef  LP_PIN22
#undef  LP_PIN23
#undef  LP_PIN24
#undef  LP_PIN25

#define LP_IRQ_MODE   (1 << 20) /* controls if pin 10 causes interrupts */
#define LP_INPUT_MODE (1 << 21) /* controls if data pins are input or output */

/*
 * Pin-to-register assignments in array form, so that they can be
 * accessed using LP_PIN[pin-number]
 */
static const int LP_PIN[] = {
  0,        /* "pin 0" - not controllable */
  LP_PIN01,
  LP_PIN02,
  LP_PIN03,
  LP_PIN04,
  LP_PIN05,
  LP_PIN06,
  LP_PIN07,
  LP_PIN08,
  LP_PIN09,
  LP_PIN10,
  LP_PIN11,
  LP_PIN12,
  LP_PIN13,
  LP_PIN14,
  LP_PIN15,
  LP_PIN16,
  LP_PIN17,
  0,        /* pin 18 - not controllable */
  0,        /* pin 19 - not controllable */
  0,        /* pin 20 - not controllable */
  0,        /* pin 21 - not controllable */
  0,        /* pin 22 - not controllable */
  0,        /* pin 23 - not controllable */
  0,        /* pin 24 - not controllable */
  0         /* pin 25 - not controllable */
};


/*************************************************************************/

/* Private Definitions are Below */

/* The state of some pins is inverted relative to the state of their
 * corresponding register bits.  These masks correct the bits that
 * need to be corrected. */
static const int lp_invert_masks[3] = {
  0,
  0x80, /* 10000000 */
  0x0b  /* 00001011 */
};

/* Pins controlled by LP_BASE + 0 (data register) */
#define LPBASE0_MASK  0x0000ff
/* Pins controlled by LP_BASE + 1 (status register) */
#define LPBASE1_MASK  0x00ff00
/* Pins controlled by LP_BASE + 2 (control register) */
#define LPBASE2_MASK  0xff0000

/* pins that are always inputs */
#define LP_ALWAYS_INPUT_PINS (LP_PIN10 | LP_PIN11 | LP_PIN12|LP_PIN13|LP_PIN15)

/* pins that can act as either inputs or outputs depending on LP_INPUT_MODE */
#define LP_DATA_PINS (LP_PIN02 | LP_PIN03 | LP_PIN04 | LP_PIN05 | LP_PIN06 | \
                   LP_PIN07 | LP_PIN08 | LP_PIN09)

/* pins that can act as either input or output */
#define LP_SWITCHABLE_PINS (LP_PIN01 | LP_PIN14 | LP_PIN16 | LP_PIN17)



#endif /* _PARAPIN_H_ */
