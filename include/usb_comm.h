#ifndef USB_COMM_H
#define USB_COMM_H

#include "ultra64/ultratypes.h"

struct PlayState;

#include "sm64_usb_protocol.h"

void usb_comm_init(void);
void usb_comm_reset(void);
void usb_comm_consume_bytes(struct PlayState* play, const u8* data, u32 len);
void usb_comm_apply_remote_inputs(struct PlayState* play);
int usb_comm_get_remote_cam_yaw(u8 slot, s16* outYaw, struct PlayState* play);

#endif
