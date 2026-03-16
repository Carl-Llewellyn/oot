#include "usb_comm.h"

#include <string.h>

#include "actor.h"
#include "camera.h"
#include "libu64/pad.h"
#include "play_state.h"
#include "sm64_usb_protocol.h"
#include "z_lib.h"

/* how long (in frames) a remote player's last packet stays valid. */
#ifndef SM64_USB_STALE_FRAMES
#define SM64_USB_STALE_FRAMES 30
#endif

typedef struct {
    u8 valid;
    u16 buttons;
    s8 stick_x;
    s8 stick_y;
    s32 x;
    s32 y;
    s32 z;
    s16 pitch;
    s16 yaw;
    s16 roll;
    s16 cam_yaw;
    s16 level;
    u32 last_seen_frame;
} Sm64UsbRemoteState;

static Sm64UsbRemoteState sRemoteStates[SM64_USB_MAX_PLAYERS];
static u8 sParseBuf[SM64_USB_PACKET_SIZE];
static u32 sParseIndex = 0;

#ifndef SM64_USB_POS_SNAP_THRESHOLD
#define SM64_USB_POS_SNAP_THRESHOLD 60.0f
#endif

#ifndef SM64_USB_POS_MAX_ABS
#define SM64_USB_POS_MAX_ABS 30000.0f
#endif

#ifndef SM64_USB_ROT_SNAP_THRESHOLD
#define SM64_USB_ROT_SNAP_THRESHOLD 0x200
#endif

static f32 sm64usb_s32_to_f32(s32 v) {
    union {
        s32 i;
        f32 f;
    } u;

    u.i = v;
    return u.f;
}

static s32 sm64usb_abs_s16(s16 v) {
    return (v < 0) ? -(s32)v : (s32)v;
}

static f32 sm64usb_abs_f32(f32 v) {
    return (v < 0.0f) ? -v : v;
}

static s32 sm64usb_pos_valid(f32 x, f32 y, f32 z) {
    if (x != x || y != y || z != z) {
        return false;
    }
    if (x < -SM64_USB_POS_MAX_ABS || x > SM64_USB_POS_MAX_ABS) {
        return false;
    }
    if (y < -SM64_USB_POS_MAX_ABS || y > SM64_USB_POS_MAX_ABS) {
        return false;
    }
    if (z < -SM64_USB_POS_MAX_ABS || z > SM64_USB_POS_MAX_ABS) {
        return false;
    }

    return true;
}

static void sm64usb_memcpy(void* dst, const void* src, u32 n) {
    u8* d = (u8*)dst;
    const u8* s = (const u8*)src;
    u32 i;

    for (i = 0; i < n; i++) {
        d[i] = s[i];
    }
}

static s8 usb_comm_clamp_s8(s32 value) {
    if (value > 127) {
        return 127;
    }
    if (value < -128) {
        return -128;
    }
    return (s8)value;
}

static void usb_comm_adjust_stick_for_camera(PlayState* play, s16 remoteCamYaw, s8* inOutStickX, s8* inOutStickY) {
    Camera* activeCam;
    s16 localCamYaw;
    s16 yawDelta;
    f32 sinDelta;
    f32 cosDelta;
    f32 srcX;
    f32 srcY;
    f32 rotX;
    f32 rotY;
    s32 outX;
    s32 outY;

    if (play == NULL || inOutStickX == NULL || inOutStickY == NULL) {
        return;
    }

    activeCam = GET_ACTIVE_CAM(play);
    if (activeCam == NULL) {
        return;
    }

    localCamYaw = Camera_GetInputDirYaw(activeCam);
    yawDelta = (s16)(remoteCamYaw - localCamYaw);

    sinDelta = Math_SinS(yawDelta);
    cosDelta = Math_CosS(yawDelta);

    srcX = *inOutStickX;
    srcY = *inOutStickY;

    rotX = (srcX * cosDelta) - (srcY * sinDelta);
    rotY = (srcX * sinDelta) + (srcY * cosDelta);

    outX = (s32)((rotX >= 0.0f) ? (rotX + 0.5f) : (rotX - 0.5f));
    outY = (s32)((rotY >= 0.0f) ? (rotY + 0.5f) : (rotY - 0.5f));

    *inOutStickX = usb_comm_clamp_s8(outX);
    *inOutStickY = usb_comm_clamp_s8(outY);
}

static void usb_comm_update_input(Input* input, u16 buttons, s8 stickX, s8 stickY) {
    s32 buttonDiff;

    if (input == NULL) {
        return;
    }

    input->prev = input->cur;

    input->cur.button = buttons;
    input->cur.stick_x = stickX;
    input->cur.stick_y = stickY;
    input->cur.errno = 0;

    buttonDiff = input->prev.button ^ input->cur.button;
    input->press.button = (u16)(buttonDiff & input->cur.button);
    input->rel.button = (u16)(buttonDiff & input->prev.button);
    PadUtils_UpdateRelXY(input);

    input->press.stick_x = (s8)(input->cur.stick_x - input->prev.stick_x);
    input->press.stick_y = (s8)(input->cur.stick_y - input->prev.stick_y);
    input->press.errno = 0;
    input->rel.errno = 0;
}

static void usb_comm_clear_input(Input* input) {
    usb_comm_update_input(input, 0, 0, 0);
}

static void usb_comm_apply_remote_position(PlayState* play, const Sm64UsbRemoteState* state) {
    Actor* p2Actor;
    f32 x;
    f32 y;
    f32 z;
    f32 dx;
    f32 dy;
    f32 dz;
    f32 dist2;
    f32 thresh;
    s32 rotThresh;

    if (play == NULL || state == NULL) {
        return;
    }

    p2Actor = play->p2DummyActor;
    if (p2Actor == NULL) {
        return;
    }

    x = sm64usb_s32_to_f32(state->x);
    y = sm64usb_s32_to_f32(state->y);
    z = sm64usb_s32_to_f32(state->z);
    dx = x - p2Actor->world.pos.x;
    dy = y - p2Actor->world.pos.y;
    dz = z - p2Actor->world.pos.z;
    dist2 = dx * dx + dy * dy + dz * dz;
    thresh = SM64_USB_POS_SNAP_THRESHOLD;
    rotThresh = SM64_USB_ROT_SNAP_THRESHOLD;

   if (!sm64usb_pos_valid(x, y, z)) {
        return;
    }

    if (dist2 > (thresh * thresh)) {
        p2Actor->world.pos.x = x;
        p2Actor->world.pos.y = y;
        p2Actor->world.pos.z = z;

        p2Actor->prevPos = p2Actor->world.pos;
        p2Actor->focus.pos = p2Actor->world.pos;
        p2Actor->focus.pos.y += 30.0f;
    }

    if (sm64usb_abs_s16((s16)(state->pitch - p2Actor->shape.rot.x)) > rotThresh) {
        p2Actor->shape.rot.x = state->pitch;
        p2Actor->world.rot.x = state->pitch;
    }
    if (sm64usb_abs_s16((s16)(state->yaw - p2Actor->shape.rot.y)) > rotThresh) {
        p2Actor->shape.rot.y = state->yaw;
        p2Actor->world.rot.y = state->yaw;
    }
    if (sm64usb_abs_s16((s16)(state->roll - p2Actor->shape.rot.z)) > rotThresh) {
        p2Actor->shape.rot.z = state->roll;
        p2Actor->world.rot.z = state->roll;
    }
}

int usb_comm_get_remote_cam_yaw(u8 slot, s16* outYaw, PlayState* play) {
    u8 playerId;

    if (outYaw == NULL || play == NULL) {
        return false;
    }
    if (slot == 0) {
        return false;
    }

    playerId = (u8)(slot - 1);
    if (playerId >= SM64_USB_MAX_PLAYERS) {
        return false;
    }
    if (!sRemoteStates[playerId].valid) {
        return false;
    }

    *outYaw = sRemoteStates[playerId].cam_yaw;
    return true;
}

/* Store already-decoded fields (host-endian), so the rest of the game never worries about byte order. */
static void usb_comm_store_decoded(u8 playerId, u16 buttons, s8 stickX, s8 stickY, s32 x, s32 y, s32 z, s16 pitch,
                                   s16 yaw, s16 roll, s16 camYaw, s16 level, u32 nowFrame) {
//    if (playerId >= SM64_USB_MAX_PLAYERS) {
       // return;
  //  }

  playerId = 0;
    sRemoteStates[playerId].valid = true;
    sRemoteStates[playerId].buttons = buttons;
    sRemoteStates[playerId].stick_x = stickX;
    sRemoteStates[playerId].stick_y = stickY;
    sRemoteStates[playerId].x = x;
    sRemoteStates[playerId].y = y;
    sRemoteStates[playerId].z = z;
    sRemoteStates[playerId].pitch = pitch;
    sRemoteStates[playerId].yaw = yaw;
    sRemoteStates[playerId].roll = roll;
    sRemoteStates[playerId].cam_yaw = camYaw;
    sRemoteStates[playerId].level = level;
    sRemoteStates[playerId].last_seen_frame = nowFrame;
}

void usb_comm_init(void) {
    usb_comm_reset();
}

void usb_comm_reset(void) {
    bzero(sRemoteStates, sizeof(sRemoteStates));
    bzero(sParseBuf, sizeof(sParseBuf));
    sParseIndex = 0;
}

void usb_comm_consume_bytes(PlayState* play, const u8* data, u32 len) {
    u32 i;
    u32 nowFrame;

    if (play == NULL || data == NULL || len == 0) {
        return;
    }

    nowFrame = play->gameplayFrames;

    for (i = 0; i < len; i++) {
        u8 byte = data[i];

        if (sParseIndex == 0) {
            if (byte != SM64_USB_SYNC0) {
                continue;
            }

            sParseBuf[sParseIndex++] = byte;
            continue;
        }

        if (sParseIndex == 1) {
            if (byte != SM64_USB_SYNC1) {
                sParseIndex = 0;
                continue;
            }

            sParseBuf[sParseIndex++] = byte;
            continue;
        }

        sParseBuf[sParseIndex++] = byte;

        if (sParseIndex >= SM64_USB_PACKET_SIZE) {
            if (sParseBuf[SM64_USB_O_SYNC0] == SM64_USB_SYNC0 && sParseBuf[SM64_USB_O_SYNC1] == SM64_USB_SYNC1 &&
                sParseBuf[SM64_USB_O_VERSION] == SM64_USB_VERSION) {
                Sm64UsbPacket pkt;
                u8 pid;
                s32 x;
                s32 y;
                s32 z;
                u16 buttons;
                s16 pitch;
                s16 yaw;
                s16 roll;
                s16 camYaw;
                s8 stickX;
                s8 stickY;
                s16 level;

                sm64usb_memcpy(pkt.b, sParseBuf, (u32)SM64_USB_PACKET_SIZE);

                pid = pkt.b[SM64_USB_O_PLAYER_ID];
                x = sm64usb_read_be32(&pkt.b[SM64_USB_O_X]);
                y = sm64usb_read_be32(&pkt.b[SM64_USB_O_Y]);
                z = sm64usb_read_be32(&pkt.b[SM64_USB_O_Z]);
                pitch = (s16)sm64usb_read_be16(&pkt.b[SM64_USB_O_PITCH]);
                yaw = (s16)sm64usb_read_be16(&pkt.b[SM64_USB_O_YAW]);
                roll = (s16)sm64usb_read_be16(&pkt.b[SM64_USB_O_ROLL]);
                camYaw = (s16)sm64usb_read_be16(&pkt.b[SM64_USB_O_CAM_YAW]);
                buttons = sm64usb_read_be16(&pkt.b[SM64_USB_O_BUTTONS]);
                stickX = (s8)pkt.b[SM64_USB_O_STICK_X];
                stickY = (s8)pkt.b[SM64_USB_O_STICK_Y];
                level = (s16)sm64usb_read_be16(&pkt.b[SM64_USB_O_LEVEL]);

                usb_comm_store_decoded(pid, buttons, stickX, stickY, x, y, z, pitch, yaw, roll, camYaw, level,
                                       nowFrame);
            }

            sParseIndex = 0;
        }
    }
}

void usb_comm_apply_remote_inputs(PlayState* play) {
    u8 playerId;
    s32 appliedP2 = false;

    if (play == NULL) {
        return;
    }

    for (playerId = 0; playerId < SM64_USB_MAX_PLAYERS; playerId++) {
        s8 remappedStickX;
        s8 remappedStickY;

        if (!sRemoteStates[playerId].valid) {
            continue;
        }

        remappedStickX = sRemoteStates[playerId].stick_x;
        remappedStickY = sRemoteStates[playerId].stick_y;
        usb_comm_adjust_stick_for_camera(play, sRemoteStates[playerId].cam_yaw, &remappedStickX, &remappedStickY);

        /* Route the first valid remote state into P2 without scene/slot filtering. */
        usb_comm_update_input(&play->p2Input, sRemoteStates[playerId].buttons, remappedStickX, remappedStickY);
#if SM64_USB_APPLY_REMOTE_POS
        usb_comm_apply_remote_position(play, &sRemoteStates[playerId]);
#endif
        appliedP2 = true;
        break;
    }

    if (!appliedP2) {
        usb_comm_clear_input(&play->p2Input);
    }
}

void usb_comm_post_actor_update(PlayState* play) {
    u8 playerId;
    Actor* p2Actor;
    f32 x;
    f32 y;
    f32 z;
    f32 thresh;

    if (play == NULL) {
        return;
    }

    p2Actor = play->p2DummyActor;
    if (p2Actor == NULL) {
        return;
    }

    thresh = SM64_USB_POS_SNAP_THRESHOLD;

    for (playerId = 0; playerId < SM64_USB_MAX_PLAYERS; playerId++) {
        if (!sRemoteStates[playerId].valid) {
            continue;
        }

        x = sm64usb_s32_to_f32(sRemoteStates[playerId].x);
        y = sm64usb_s32_to_f32(sRemoteStates[playerId].y);
        z = sm64usb_s32_to_f32(sRemoteStates[playerId].z);

        if (!sm64usb_pos_valid(x, y, z)) {
            continue;
        }

        if ((sm64usb_abs_f32(x - p2Actor->world.pos.x) > thresh) || (sm64usb_abs_f32(y - p2Actor->world.pos.y) > thresh) ||
            (sm64usb_abs_f32(z - p2Actor->world.pos.z) > thresh)) {
            p2Actor->world.pos.x = x;
            p2Actor->world.pos.y = y;
            p2Actor->world.pos.z = z;
            p2Actor->prevPos = p2Actor->world.pos;
            p2Actor->focus.pos = p2Actor->world.pos;
            p2Actor->focus.pos.y += 30.0f;
        }

        break;
    }
}
