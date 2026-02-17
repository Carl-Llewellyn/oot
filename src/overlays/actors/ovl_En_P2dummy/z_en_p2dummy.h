#ifndef Z_EN_P2DUMMY_H
#define Z_EN_P2DUMMY_H

#include "ultra64.h"
#include "actor.h"

typedef enum EnP2dummyMoveState {
    P2DUMMY_MOVE_IDLE = 0,
    P2DUMMY_MOVE_WALK = 1,
    P2DUMMY_MOVE_RUN = 2,
    P2DUMMY_MOVE_AIR = 3
} EnP2dummyMoveState;

typedef struct EnP2dummy {
    /* 0x0000 */ Actor actor;
    /* 0x014C */ f32 targetSpeed;
    /* 0x0150 */ s16 targetYaw;
    /* 0x0152 */ u8 moveState;
    /* 0x0153 */ u8 pad153;
    /* 0x0154 */ f32 bobPhase;
} EnP2dummy; // size = 0x0158

#endif
