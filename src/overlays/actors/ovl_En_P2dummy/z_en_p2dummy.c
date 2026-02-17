/*
 * File: z_en_p2dummy.c
 * Overlay: ovl_En_P2dummy
 * Description: Placeholder actor for future local co-op player 2 implementation
 */

#include "z_en_p2dummy.h"

#include "controller.h"
#include "gfx.h"
#include "gfx_setupdl.h"
#include "play_state.h"
#include "sys_matrix.h"

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
}

void EnP2dummy_Destroy(Actor* thisx, PlayState* play) {
    if (play->p2DummyActor == thisx) {
        play->p2DummyActor = NULL;
    }
}

void EnP2dummy_Update(Actor* thisx, PlayState* play) {
    f32 moveScale = 0.1f;

    thisx->room = play->roomCtx.curRoom.num;
    thisx->focus.pos = thisx->world.pos;
    thisx->shape.rot.y += play->p2Input.cur.stick_x * 0x40;
    thisx->world.pos.x += play->p2Input.cur.stick_x * moveScale;
    thisx->world.pos.z -= play->p2Input.cur.stick_y * moveScale;

    if (CHECK_BTN_ANY(play->p2Input.cur.button, BTN_A)) {
        thisx->shape.rot.x += 0x200;
    }
}

void EnP2dummy_Draw(Actor* thisx, PlayState* play) {
    OPEN_DISPS(play->state.gfxCtx, "../z_en_p2dummy.c", 66);

    Gfx_SetupDL_25Opa(play->state.gfxCtx);
    gDPSetPrimColor(POLY_OPA_DISP++, 0, 0, 255, 48, 48, 255);
    gDPSetEnvColor(POLY_OPA_DISP++, 64, 0, 0, 255);

    MATRIX_FINALIZE_AND_LOAD(POLY_OPA_DISP++, play->state.gfxCtx, "../z_en_p2dummy.c", 72);
    gSPDisplayList(POLY_OPA_DISP++, gSmallCubeDL);

    CLOSE_DISPS(play->state.gfxCtx, "../z_en_p2dummy.c", 75);
}
