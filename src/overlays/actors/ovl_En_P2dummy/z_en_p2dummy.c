/*
 * File: z_en_p2dummy.c
 * Overlay: ovl_En_P2dummy
 * Description: Placeholder actor for future local co-op player 2 implementation
 */

#include "z_en_p2dummy.h"

#include "play_state.h"

#define FLAGS ACTOR_FLAG_UPDATE_CULLING_DISABLED

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
}

void EnP2dummy_Update(Actor* thisx, PlayState* play) {
}

void EnP2dummy_Draw(Actor* thisx, PlayState* play) {
}
