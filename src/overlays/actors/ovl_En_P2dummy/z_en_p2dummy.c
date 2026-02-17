/*
 * File: z_en_p2dummy.c
 * Overlay: ovl_En_P2dummy
 * Description: Placeholder actor for future local co-op player 2 implementation
 */

#include "z_en_p2dummy.h"

#include "controller.h"
#include "camera.h"
#include "gfx.h"
#include "gfx_setupdl.h"
#include "play_state.h"
#include "sys_math.h"
#include "sys_matrix.h"
#include "z_lib.h"
#include "z_math.h"

#include "assets/objects/gameplay_keep/small_cube_model.h"

#define FLAGS (ACTOR_FLAG_UPDATE_CULLING_DISABLED | ACTOR_FLAG_DRAW_CULLING_DISABLED)

void EnP2dummy_Init(Actor* thisx, PlayState* play);
void EnP2dummy_Destroy(Actor* thisx, PlayState* play);
void EnP2dummy_Update(Actor* thisx, PlayState* play);
void EnP2dummy_Draw(Actor* thisx, PlayState* play);

ActorProfile En_P2dummy_Profile = {
    /**/ ACTOR_EN_P2DUMMY,
    /**/ ACTORCAT_NPC,
    /**/ FLAGS,
    /**/ OBJECT_GAMEPLAY_KEEP,
    /**/ sizeof(EnP2dummy),
    /**/ EnP2dummy_Init,
    /**/ EnP2dummy_Destroy,
    /**/ EnP2dummy_Update,
    /**/ EnP2dummy_Draw,
};

void EnP2dummy_Init(Actor* thisx, PlayState* play) {
    EnP2dummy* this = (EnP2dummy*)thisx;

    Actor_SetScale(&this->actor, 0.01f);
    this->actor.gravity = -3.0f;
    this->actor.minVelocityY = -20.0f;
    this->targetSpeed = 0.0f;
    this->targetYaw = this->actor.shape.rot.y;
    this->moveState = P2DUMMY_MOVE_IDLE;
    this->bobPhase = 0.0f;
}

void EnP2dummy_Destroy(Actor* thisx, PlayState* play) {
    if (play->p2DummyActor == thisx) {
        play->p2DummyActor = NULL;
    }
}

void EnP2dummy_Update(Actor* thisx, PlayState* play) {
    EnP2dummy* this = (EnP2dummy*)thisx;
    Input* p2Input = &play->p2Input;
    Camera* activeCam = GET_ACTIVE_CAM(play);
    s8 stickX = p2Input->cur.stick_x;
    s8 stickY = p2Input->cur.stick_y;
    f32 stickMag = sqrtf((f32)(stickX * stickX + stickY * stickY)) / 64.0f;
    f32 moveX;
    f32 moveZ;
    s16 camYaw;

    thisx->room = play->roomCtx.curRoom.num;

    if (stickMag > 1.0f) {
        stickMag = 1.0f;
    }

    if (stickMag > 0.15f) {
        this->targetSpeed = (stickMag > 0.65f) ? (6.0f * stickMag) : (3.0f * stickMag);

        if (CHECK_BTN_ANY(p2Input->cur.button, BTN_B)) {
            this->targetSpeed += 1.0f;
        }

        camYaw = Camera_GetInputDirYaw(activeCam);
        moveX = Math_CosS(camYaw) * stickX + Math_SinS(camYaw) * stickY;
        moveZ = -Math_SinS(camYaw) * stickX + Math_CosS(camYaw) * stickY;

        this->targetYaw = Math_Atan2S(moveX, moveZ);
        Math_SmoothStepToS(&thisx->shape.rot.y, this->targetYaw, 6, 0x1000, 0);
    } else {
        this->targetSpeed = 0.0f;
    }

    thisx->world.rot.y = thisx->shape.rot.y;
    Math_SmoothStepToF(&thisx->speed, this->targetSpeed, 0.35f, 0.8f, 0.01f);

    if (CHECK_BTN_ANY(p2Input->press.button, BTN_A) && (thisx->bgCheckFlags & BGCHECKFLAG_GROUND)) {
        thisx->velocity.y = 7.0f;
    }

    Actor_MoveXZGravity(thisx);
    Actor_UpdateBgCheckInfo(play, thisx, 26.0f, 18.0f, 40.0f,
                            UPDBGCHECKINFO_FLAG_0 | UPDBGCHECKINFO_FLAG_2 | UPDBGCHECKINFO_FLAG_3 |
                                UPDBGCHECKINFO_FLAG_4);

    if (!(thisx->bgCheckFlags & BGCHECKFLAG_GROUND)) {
        this->moveState = P2DUMMY_MOVE_AIR;
    } else if (thisx->speed < 0.2f) {
        this->moveState = P2DUMMY_MOVE_IDLE;
    } else if (thisx->speed < 4.0f) {
        this->moveState = P2DUMMY_MOVE_WALK;
    } else {
        this->moveState = P2DUMMY_MOVE_RUN;
    }

    if (this->moveState == P2DUMMY_MOVE_WALK || this->moveState == P2DUMMY_MOVE_RUN) {
        this->bobPhase += thisx->speed * 0.25f;
        thisx->shape.yOffset = sinf(this->bobPhase) * 100.0f;
    } else {
        thisx->shape.yOffset = 0.0f;
    }

    thisx->focus.pos = thisx->world.pos;
    thisx->focus.pos.y += 30.0f;
}

void EnP2dummy_Draw(Actor* thisx, PlayState* play) {
    EnP2dummy* this = (EnP2dummy*)thisx;
    u8 colorR;
    u8 colorG;
    u8 colorB;

    switch (this->moveState) {
        case P2DUMMY_MOVE_WALK:
            colorR = 80;
            colorG = 160;
            colorB = 255;
            break;

        case P2DUMMY_MOVE_RUN:
            colorR = 255;
            colorG = 120;
            colorB = 80;
            break;

        case P2DUMMY_MOVE_AIR:
            colorR = 255;
            colorG = 230;
            colorB = 80;
            break;

        default:
            colorR = 180;
            colorG = 180;
            colorB = 180;
            break;
    }

    OPEN_DISPS(play->state.gfxCtx, "../z_en_p2dummy.c", 66);

    Gfx_SetupDL_25Opa(play->state.gfxCtx);
    gDPSetPrimColor(POLY_OPA_DISP++, 0, 0, colorR, colorG, colorB, 255);
    gDPSetEnvColor(POLY_OPA_DISP++, 64, 0, 0, 255);

    MATRIX_FINALIZE_AND_LOAD(POLY_OPA_DISP++, play->state.gfxCtx, "../z_en_p2dummy.c", 72);
    gSPDisplayList(POLY_OPA_DISP++, gSmallCubeDL);

    CLOSE_DISPS(play->state.gfxCtx, "../z_en_p2dummy.c", 75);
}
