#include <string.h>

#include "usb.h"

#include "ultra64.h"
#include "ultra64/os_pi.h"
#include "ultra64/rcp.h"

#include "camera.h"
#include "play_state.h"
#include "player.h"
#include "sm64_usb_protocol.h"
#include "usb_comm.h"

void __osPiGetAccess(void);
void __osPiRelAccess(void);

UsbIncomingDebug gUsbIncomingDebug;

static u32 float_to_u32(f32 f) {
    union {
        f32 f;
        u32 u;
    } u;

    u.f = f;
    return u.u;
}

static void usb_debug_capture_incoming(const u8* data, u32 frameSeen) {
    if (data == NULL) {
        return;
    }

    gUsbIncomingDebug.hasPacket = true;
    gUsbIncomingDebug.sync0 = data[SM64_USB_O_SYNC0];
    gUsbIncomingDebug.sync1 = data[SM64_USB_O_SYNC1];
    gUsbIncomingDebug.version = data[SM64_USB_O_VERSION];
    gUsbIncomingDebug.playerId = data[SM64_USB_O_PLAYER_ID];
    gUsbIncomingDebug.xRaw = (u32)sm64usb_read_be32(&data[SM64_USB_O_X]);
    gUsbIncomingDebug.yRaw = (u32)sm64usb_read_be32(&data[SM64_USB_O_Y]);
    gUsbIncomingDebug.zRaw = (u32)sm64usb_read_be32(&data[SM64_USB_O_Z]);
    gUsbIncomingDebug.pitch = (s16)sm64usb_read_be16(&data[SM64_USB_O_PITCH]);
    gUsbIncomingDebug.yaw = (s16)sm64usb_read_be16(&data[SM64_USB_O_YAW]);
    gUsbIncomingDebug.roll = (s16)sm64usb_read_be16(&data[SM64_USB_O_ROLL]);
    gUsbIncomingDebug.camYaw = (s16)sm64usb_read_be16(&data[SM64_USB_O_CAM_YAW]);
    gUsbIncomingDebug.buttons = sm64usb_read_be16(&data[SM64_USB_O_BUTTONS]);
    gUsbIncomingDebug.stickX = (s8)data[SM64_USB_O_STICK_X];
    gUsbIncomingDebug.stickY = (s8)data[SM64_USB_O_STICK_Y];
    gUsbIncomingDebug.level = (s16)sm64usb_read_be16(&data[SM64_USB_O_LEVEL]);
    gUsbIncomingDebug.passesHeaderChecks = (gUsbIncomingDebug.sync0 == SM64_USB_SYNC0) &&
                                           (gUsbIncomingDebug.sync1 == SM64_USB_SYNC1) &&
                                           (gUsbIncomingDebug.version == SM64_USB_VERSION);
    gUsbIncomingDebug.frameSeen = frameSeen;
}

static void send_32_bit(s32 len, const u8* data) {
    u32* currWriteAddr = (u32*)CART_SRAM_START;
    s32 i;
    s32 n;
    u32 word;
    u32 status;

    if (len != SM64_USB_PACKET_SIZE) {
        return;
    }

    for (i = 0; i < len; i += 4) {
        word = 0;
        n = (len - i < 4) ? (len - i) : 4;
        memcpy(&word, data + i, n);

        WAIT_ON_IO_BUSY(status);
        IO_WRITE((u32)currWriteAddr, word);
        currWriteAddr++;
    }
}

static void read_incoming(u8* data) {
    s32 len = SM64_USB_PACKET_SIZE;
    u32* currReadAddr = (u32*)CART_SRAM_START;
    s32 i;
    s32 n;
    u32 word;
    u32 status;

    if (data == NULL) {
        return;
    }

    for (i = 0; i < len; i += 4) {
        n = (len - i < 4) ? (len - i) : 4;
        WAIT_ON_IO_BUSY(status);
        word = IO_READ((u32)currReadAddr);
        currReadAddr++;
        memcpy(data + i, &word, n);
    }
}

static void usb_send_state(PlayState* play) {
    u8 raw[SM64_USB_PACKET_SIZE] = { 0 };
    Input* input;
    Player* player;
    Camera* activeCam;
    s16 camYaw = 0;

    if (play == NULL) {
        return;
    }

    player = GET_PLAYER(play);
    if (player == NULL) {
        return;
    }

    input = &play->state.input[0];
    activeCam = GET_ACTIVE_CAM(play);

    if (activeCam != NULL) {
        camYaw = Camera_GetInputDirYaw(activeCam);
    }

    raw[SM64_USB_O_SYNC0] = SM64_USB_SYNC0;
    raw[SM64_USB_O_SYNC1] = SM64_USB_SYNC1;
    raw[SM64_USB_O_VERSION] = SM64_USB_VERSION;
    raw[SM64_USB_O_PLAYER_ID] = 0xFF;

    sm64usb_write_be32(&raw[SM64_USB_O_X], float_to_u32(player->actor.world.pos.x));
    sm64usb_write_be32(&raw[SM64_USB_O_Y], float_to_u32(player->actor.world.pos.y));
    sm64usb_write_be32(&raw[SM64_USB_O_Z], float_to_u32(player->actor.world.pos.z));

    sm64usb_write_be16(&raw[SM64_USB_O_PITCH], (u16)player->actor.shape.rot.x);
    sm64usb_write_be16(&raw[SM64_USB_O_YAW], (u16)player->actor.shape.rot.y);
    sm64usb_write_be16(&raw[SM64_USB_O_ROLL], (u16)player->actor.shape.rot.z);
    sm64usb_write_be16(&raw[SM64_USB_O_CAM_YAW], (u16)camYaw);

    sm64usb_write_be16(&raw[SM64_USB_O_BUTTONS], input->cur.button);
    raw[SM64_USB_O_STICK_X] = (u8)input->cur.stick_x;
    raw[SM64_USB_O_STICK_Y] = (u8)input->cur.stick_y;
    sm64usb_write_be16(&raw[SM64_USB_O_LEVEL], (u16)play->sceneId);

    send_32_bit(SM64_USB_PACKET_SIZE, raw);
}

void usb_update(PlayState* play) {
    u8 incomingState[SM64_USB_PACKET_SIZE] = { 0 };

    if (play == NULL || GET_PLAYER(play) == NULL) {
        return;
    }

    __osPiGetAccess();
    usb_send_state(play);
    read_incoming(incomingState);
    __osPiRelAccess();

    usb_debug_capture_incoming(incomingState, play->gameplayFrames);

    usb_comm_consume_bytes(play, incomingState, SM64_USB_PACKET_SIZE);
    usb_comm_apply_remote_inputs(play);
}
