#ifndef UNFL_USB_H
#define UNFL_USB_H

#include "ultra64.h"

#define CART_DOM2_ADDR2_START 0x08000000
#define USB_COMM_OFFSET CART_DOM2_ADDR2_START
#define CART_SRAM_START CART_DOM2_ADDR2_START

#define USB_X_ADDR CART_SRAM_START
#define USB_Y_ADDR (USB_X_ADDR + 4)
#define USB_Z_ADDR (USB_Y_ADDR + 4)

#define READ_USB_X_ADDR (USB_Z_ADDR + 4)
#define READ_USB_Y_ADDR (READ_USB_X_ADDR + 4)
#define READ_USB_Z_ADDR (READ_USB_Y_ADDR + 4)

#define WAIT_ON_IO_BUSY(stat)                                                                          \
    stat = IO_READ(PI_STATUS_REG);                                                                     \
    while (stat & (PI_STATUS_IO_BUSY | PI_STATUS_DMA_BUSY))                                            \
        stat = IO_READ(PI_STATUS_REG);

// Align to 8-byte boundary for DMA 
#if !defined(ALIGNED8) && defined(__GNUC__)
#define ALIGNED8 __attribute__((aligned(8)))
#elif !defined(ALIGNED8)
#define ALIGNED8
#endif

#if !defined(UNUSED) && defined(__GNUC__)
#define UNUSED __attribute__((unused))
#elif !defined(UNUSED)
#define UNUSED
#endif

#ifndef STACKSIZE
#define STACKSIZE 0x2000
#endif


extern f32 read_usb_posX;
extern f32 read_usb_posY;
extern f32 read_usb_posZ;

typedef struct UsbIncomingDebug {
    u8 hasPacket;
    u8 passesHeaderChecks;
    u8 sync0;
    u8 sync1;
    u8 version;
    u8 playerId;
    u32 xRaw;
    u32 yRaw;
    u32 zRaw;
    s16 pitch;
    s16 yaw;
    s16 roll;
    s16 camYaw;
    u16 buttons;
    s8 stickX;
    s8 stickY;
    s16 level;
    u32 frameSeen;
} UsbIncomingDebug;

extern UsbIncomingDebug gUsbIncomingDebug;

extern ALIGNED8 u8 gThread7Stack[STACKSIZE];

extern f32 __osAtomicReadF32(f32 *src);
extern f32 __osAtomicWriteF32(f32 *src, f32 *dest);

extern void incoming_usb_pos(f32 *x, f32 *y, f32 *z) ;

struct PlayState;
extern void usb_update(struct PlayState* play);

#endif
