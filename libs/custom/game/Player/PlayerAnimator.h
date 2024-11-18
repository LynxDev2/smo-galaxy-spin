#pragma once

#include <prim/seadSafeString.h>

#include "Player/PlayerAnimFrameCtrl.h"

class PlayerAnimator {
public:
    void startAnim(const sead::SafeStringBase<char>& animName);
    void startSubAnim(const sead::SafeStringBase<char>& animName);

    bool isAnim(const sead::SafeStringBase<char>& animName) const;

    bool isAnimEnd() const;

    void endSubAnim();

    f32 getAnimFrame() const;
    f32 getAnimFrameMax() const;
    f32 getAnimFrameRate() const;

    unsigned char padding_18[0x18];
    PlayerAnimFrameCtrl* mAnimFrameCtrl;
    unsigned long padding_4;
    char* mCurrentAnim;
    unsigned char padding_58[0x50];
    char* mCurrentSubAnim;
};
