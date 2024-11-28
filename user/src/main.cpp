#include <exl/hook/base.hpp>
#include <mallow/config.hpp>
#include <mallow/init/initLogging.hpp>
#include <mallow/logging/logger.hpp>
#include <mallow/mallow.hpp>

#include "ModOptions.h"
#include "Player/PlayerActionGroundMoveControl.h"
#include "Player/PlayerActorHakoniwa.h"
#include "Player/PlayerSpinCapAttack.h"
#include "Player/PlayerColliderHakoniwa.h"
#include "Player/PlayerStateSpinCap.h"
#include "Player/PlayerStateSwim.h"
#include "Player/PlayerTrigger.h"
#include "Player/PlayerHackKeeper.h"
#include "Player/PlayerFunction.h"
#include "Library/LiveActor/ActorSensorMsgFunction.h"
#include "Library/LiveActor/ActorActionFunction.h"
#include "Library/Controller/InputFunction.h"
#include "Library/LiveActor/ActorMovementFunction.h"
#include "Library/LiveActor/ActorSensorFunction.h"
#include "Library/LiveActor/ActorPoseKeeper.h"
#include "Library/Math/MathAngleUtil.h"
#include "Library/Base/StringUtil.h"
#include "Project/HitSensor/HitSensor.h"
#include "Player/PlayerAnimator.h"
#include "Util/PlayerCollisionUtil.h"
#include "Library/Base/StringUtil.h"
#include "Library/Nerve/NerveUtil.h"
#include "Player/PlayerModelHolder.h"
#include "Library/Effect/EffectSystemInfo.h"
#include "Library/Nerve/NerveSetupUtil.h"

namespace rs {
    bool is2D(const IUseDimension*);
}
namespace PlayerEquipmentFunction {
    bool isEquipmentNoCapThrow(const PlayerEquipmentUser*);
}
class PlayerCarryKeeper {
public:
    bool isCarry() const;
};

using mallow::log::logLine;

//Mod code

const al::Nerve* getNerveAt(uintptr_t offset)
{
    return (const al::Nerve*)((((u64)malloc) - 0x00724b94) + offset);
}

bool isPadTriggerGalaxySpin(int port){
    switch (mallow::config::getConfg<ModOptions>()->spinButton) {
        case 'L':
            return al::isPadTriggerL(port);
        case 'R':
            return al::isPadTriggerR(port);
        case 'X':
            return al::isPadTriggerX(port);
        case 'Y':
        default:
            return al::isPadTriggerY(port);
    }
}

al::LiveActor* hitBuffer[0x40];
int hitBufferCount = 0;

const uintptr_t spinCapNrvOffset = 0x1d78940;
const uintptr_t nrvSpinCapFall = 0x1d7ff70;


bool isGalaxySpin = false;
bool canGalaxySpin = true;
bool canStandardSpin = true;
bool isGalaxyAfterStandardSpin = false;  // special case, as switching between spins resets isGalaxySpin and canStandardSpin
bool isStandardAfterGalaxySpin = false;
int galaxyFakethrowRemainder = -1;  // -1 = inactive, -2 = request to start, positive = remaining frames
bool triggerGalaxySpin = false;
bool prevIsCarry = false;

struct PlayerTryActionCapSpinAttack : public mallow::hook::Trampoline<PlayerTryActionCapSpinAttack>{
    static bool Callback(PlayerActorHakoniwa* player, bool a2) {
        // do not allow Y to trigger both pickup and spin on seeds (for picking up rocks, this function is not called)
        bool newIsCarry = player->mPlayerCarryKeeper->isCarry();
        if (newIsCarry && !prevIsCarry) {
            prevIsCarry = newIsCarry;
            return false;
        }
        prevIsCarry = newIsCarry;
        if (isPadTriggerGalaxySpin(-1) && !rs::is2D(player) && !PlayerEquipmentFunction::isEquipmentNoCapThrow(player->mPlayerEquipmentUser)) {
            if(player->mPlayerAnimator->isAnim("SpinSeparate"))
                return false;
            if (canGalaxySpin) {
                triggerGalaxySpin = true;
            }
            else {
                triggerGalaxySpin = true;
                galaxyFakethrowRemainder = -2;
            }
            return true;
        }

        if(Orig(player, a2)) {
            triggerGalaxySpin = false;
            return true;
        }
        return false;
    }
};

class PlayerStateSpinCapNrvGalaxySpinGround : public al::Nerve {
public:
    void execute(al::NerveKeeper* keeper) const override {
        PlayerStateSpinCap* state = keeper->getParent<PlayerStateSpinCap>();

        if(al::isFirstStep(state)) {
            state->mAnimator->endSubAnim();
            state->mAnimator->startAnim("SpinSeparate");
            al::validateHitSensor(state->mActor, "GalaxySpin");
        }

        state->updateSpinGroundNerve();

        if(al::isGreaterStep(state, 21)) {
            al::invalidateHitSensor(state->mActor, "GalaxySpin");
        }

        if(state->mAnimator->isAnimEnd()) {
            state->kill();
        }
    }
};

class PlayerStateSpinCapNrvGalaxySpinAir : public al::Nerve {
public:
    void execute(al::NerveKeeper* keeper) const override {
        PlayerStateSpinCap* state = keeper->getParent<PlayerStateSpinCap>();

        if(al::isFirstStep(state)) {
            state->mAnimator->startAnim("SpinSeparate");
            al::validateHitSensor(state->mActor, "GalaxySpin");
        }

        state->updateSpinAirNerve();

        if(al::isGreaterStep(state, 21)) {
            al::invalidateHitSensor(state->mActor, "GalaxySpin");
            al::setNerve(state, getNerveAt(nrvSpinCapFall));
        }
    }
};

PlayerStateSpinCapNrvGalaxySpinAir GalaxySpinAir;
PlayerStateSpinCapNrvGalaxySpinGround GalaxySpinGround;

struct PlayerSpinCapAttackAppear : public mallow::hook::Trampoline<PlayerSpinCapAttackAppear>{
    static void Callback(PlayerStateSpinCap* state){
        if(isGalaxyAfterStandardSpin){
            isGalaxyAfterStandardSpin = false;
            canStandardSpin = false;
            triggerGalaxySpin = true;
        }
        if(isStandardAfterGalaxySpin) {
            isStandardAfterGalaxySpin = false;
            canGalaxySpin = false;
            triggerGalaxySpin = false;
        }

        if(!triggerGalaxySpin){
            canStandardSpin = false;
            isGalaxySpin = false;
            Orig(state);
            return;
        }
        hitBufferCount = 0;
        canGalaxySpin = false;
        isGalaxySpin = true;
        triggerGalaxySpin = false;

        // ----------------
        // MODIFIED FROM PlayerStateSpinCap::appear
        bool v2 = state->mTrigger->isOn(PlayerTrigger::EActionTrigger_val33);
        state->mIsDead = false;
        state->mIsInWater = false;
        state->_99 = 0;
        state->_80 = 0;
        state->_9C = {0.0f, 0.0f, 0.0f};
        state->_A8 = 0;
        // TODO set something on JudgeWaterSurfaceRun
        state->_A9 = state->mTrigger->isOn(PlayerTrigger::EActionTrigger_val0);
        bool v10 =
            rs::isOnGround(state->mActor, state->mCollider) && !state->mTrigger->isOn(PlayerTrigger::EActionTrigger_val2);

        if (v2 || v10) {
            /*if (rs::isOnGroundSkateCode(mActor, mCollider))
                mSpinCapAttack->clearAttackInfo();*/

            if (state->mTrigger->isOn(PlayerTrigger::EActionTrigger_val1)) {
                al::alongVectorNormalH(al::getVelocityPtr(state->mActor), al::getVelocity(state->mActor),
                                        -al::getGravity(state->mActor), rs::getCollidedGroundNormal(state->mCollider));
            }

            state->mActionGroundMoveControl->appear();
            /*mSpinCapAttack->setupAttackInfo();

            if (mSpinCapAttack->isSeparateSingleSpin())
                al::setNerve(this, &SpinGroundSeparate);
            else
                al::setNerve(this, &SpinGround);
            return;*/
            al::setNerve(state, &GalaxySpinGround); // <- new
        } else {
            state->_78 = 1;
            /*mSpinCapAttack->setupAttackInfo();
            if (mSpinCapAttack->isSeparateSingleSpin())
                al::setNerve(this, &SpinAirSeparate);
            else
                al::setNerve(this, &SpinAir);*/
            // ------- new:
            if(isGalaxySpin && galaxyFakethrowRemainder == -2)
                al::setNerve(state, getNerveAt(nrvSpinCapFall));
            else
                al::setNerve(state, &GalaxySpinAir);
            // -------
            return;
        }
        // ----------------------
        // END MODIFIED CODE
    }
};

struct PlayerStateSpinCapKill : public mallow::hook::Trampoline<PlayerStateSpinCapKill>{
    static void Callback(PlayerStateSpinCap* state){
        Orig(state);
        isGalaxySpin = false;
        canStandardSpin = true;
        canGalaxySpin = true;
        galaxyFakethrowRemainder = -1;
        al::invalidateHitSensor(state->mActor, "GalaxySpin");
    }
};

struct PlayerStateSpinCapFall : public mallow::hook::Trampoline<PlayerStateSpinCapFall>{
    static void Callback(PlayerStateSpinCap* state){
        Orig(state);

        if(galaxyFakethrowRemainder == -2) {
            galaxyFakethrowRemainder = 21;
            al::validateHitSensor(state->mActor, "GalaxySpin");
            state->mAnimator->startAnim("SpinSeparate");
        }
        else if(galaxyFakethrowRemainder > 0) {
            galaxyFakethrowRemainder--;
        }
        else if(galaxyFakethrowRemainder == 0) {
            galaxyFakethrowRemainder = -1;
            al::invalidateHitSensor(state->mActor, "GalaxySpin");
        }
    }
};

struct PlayerConstGetSpinAirSpeedMax : public mallow::hook::Trampoline<PlayerConstGetSpinAirSpeedMax> {
    static float Callback(PlayerConst* playerConst) {
        if(isGalaxySpin)
            return playerConst->getNormalMaxSpeed();
        return Orig(playerConst);
    }
};

struct PlayerConstGetSpinBrakeFrame : public mallow::hook::Trampoline<PlayerConstGetSpinBrakeFrame> {
    static s32 Callback(PlayerConst* playerConst) {
        if(isGalaxySpin)
            return 0;
        return Orig(playerConst);
    }
};

// used in swimming, which also calls tryActionCapSpinAttack before, so just assume isGalaxySpin is properly set up
struct PlayerSpinCapAttackIsSeparateSingleSpin : public mallow::hook::Trampoline<PlayerSpinCapAttackIsSeparateSingleSpin>{
    static bool Callback(PlayerStateSwim* thisPtr){
        if(triggerGalaxySpin) {
            return true;
        }
        return Orig(thisPtr);
    }
};

struct PlayerStateSwimExeSwimSpinCap : public mallow::hook::Trampoline<PlayerStateSwimExeSwimSpinCap>{
    static void Callback(PlayerStateSwim* thisPtr){
        Orig(thisPtr);
        if(triggerGalaxySpin && al::isFirstStep(thisPtr)) {
            al::validateHitSensor(thisPtr->mActor, "GalaxySpin");
            hitBufferCount = 0;
            isGalaxySpin = true;
            triggerGalaxySpin = false;
        }
        if(isGalaxySpin && (al::isGreaterStep(thisPtr, 62) || al::isStep(thisPtr, -1))) {
            al::invalidateHitSensor(thisPtr->mActor, "GalaxySpin");
            isGalaxySpin = false;
        }
    }
};

struct PlayerStateSwimExeSwimSpinCapSurface : public mallow::hook::Trampoline<PlayerStateSwimExeSwimSpinCapSurface>{
    static void Callback(PlayerStateSwim* thisPtr){
        Orig(thisPtr);
        if(triggerGalaxySpin && al::isFirstStep(thisPtr)) {
            al::validateHitSensor(thisPtr->mActor, "GalaxySpin");
            hitBufferCount = 0;
            isGalaxySpin = true;
            triggerGalaxySpin = false;
        }
        if(isGalaxySpin && (al::isGreaterStep(thisPtr, 62) || al::isStep(thisPtr, -1))) {
            al::invalidateHitSensor(thisPtr->mActor, "GalaxySpin");
            isGalaxySpin = false;
        }
    }
};

struct PlayerStateSwimExeSwimHipDropHeadSliding : public mallow::hook::Trampoline<PlayerStateSwimExeSwimHipDropHeadSliding>{
    static void Callback(PlayerStateSwim* thisPtr){
        Orig(thisPtr);
        if(isPadTriggerGalaxySpin(-1))
            if(((PlayerActorHakoniwa*)thisPtr->mActor)->tryActionCapSpinAttackImpl(true))
                thisPtr->startCapThrow();
    }
};

struct PlayerStateSwimKill : public mallow::hook::Trampoline<PlayerStateSwimKill>{
    static void Callback(PlayerStateSwim* state){
        Orig(state);
        isGalaxySpin = false;
        al::invalidateHitSensor(state->mActor, "GalaxySpin");
    }
};

struct PlayerSpinCapAttackStartSpinSeparateSwimSurface : public mallow::hook::Trampoline<PlayerSpinCapAttackStartSpinSeparateSwimSurface>{
    static void Callback(PlayerSpinCapAttack* thisPtr, PlayerAnimator* animator){
        if(!isGalaxySpin && !triggerGalaxySpin) {
            Orig(thisPtr, animator);
            return;
        }

        animator->startAnim("SpinSeparateSwim");
        animator->startSubAnim("SpinSeparateSwim");
    }
};

namespace al {
    bool sendMsgKickStoneAttackReflect(al::HitSensor* receiver, al::HitSensor* sender);
}

namespace rs {
    bool sendMsgHackAttack(al::HitSensor* receiver, al::HitSensor* sender);
    bool sendMsgCapTrampolineAttack(al::HitSensor* receiver, al::HitSensor* sender);
    bool sendMsgHammerBrosHammerEnemyAttack(al::HitSensor* receiver, al::HitSensor* sender);
    bool sendMsgCapReflect(al::HitSensor* receiver, al::HitSensor* sender);
    bool sendMsgCapAttack(al::HitSensor* receiver, al::HitSensor* sender);
    al::HitSensor* tryGetCollidedWallSensor(IUsePlayerCollision const* collider);
}

struct PlayerAttackSensorHook : public mallow::hook::Trampoline<PlayerAttackSensorHook>{
    static void Callback(PlayerActorHakoniwa* thisPtr, al::HitSensor* target, al::HitSensor* source){
        if(al::isSensorName(target, "GalaxySpin") && thisPtr->mPlayerAnimator && (al::isEqualString(thisPtr->mPlayerAnimator->mCurrentAnim, "SpinSeparate") || isGalaxySpin)){
            bool isInHitBuffer = false;
            for(int i = 0; i < hitBufferCount; i++){
                if(hitBuffer[i] == al::getSensorHost(source)){
                    isInHitBuffer = true;
                    break;
                }
            }
            isInHitBuffer |= al::isEqualSubString(typeid(al::getSensorHost(source)).name(),"Seed");  // Seed Check
            if(!isInHitBuffer){
                if(
                    rs::sendMsgCapTrampolineAttack(source, target) ||
                    // disallow fire attack on sheep
                    (!al::isEqualString(al::getSensorHost(source)->mActorName, "コレクトアニマル") && al::sendMsgEnemyAttackFire(source, target, nullptr)) ||
                    al::sendMsgExplosion(source, target, nullptr) ||
                    rs::sendMsgHackAttack(source, target) ||
                    rs::sendMsgHammerBrosHammerEnemyAttack(source, target) ||
                    //allow hammer attack for MarchingCubes
                    (rs::tryGetCollidedWallSensor(thisPtr->mPlayerColliderHakoniwa) && rs::sendMsgHammerBrosHammerEnemyAttack(rs::tryGetCollidedWallSensor(thisPtr->mPlayerColliderHakoniwa), target)) ||
                    rs::sendMsgCapReflect(source, target) ||
                    rs::sendMsgCapAttack(source, target) ||
                    al::sendMsgKickStoneAttackReflect(source, target) ||
                    al::sendMsgPlayerSpinAttack(source, target, nullptr)
                ) {
                    /*logLine("hit: %s => %s", al::getSensorHost(source)->mActorName, source->mName);
                    const char* name = al::getSensorHost(source)->mActorName;
                    while(*name != 0)
                        mallow::log::log("%d ", *name++);*/
                    hitBuffer[hitBufferCount++] = al::getSensorHost(source);
                    al::LiveActor* playerModel = thisPtr->mPlayerModelHolder->findModelActor("Normal");
                    if(playerModel){
                        sead::Vector3 sourceOffsetFromPlayer = al::getTrans(al::getSensorHost(source));
                        al::tryEmitEffect(playerModel, "Hit", &sourceOffsetFromPlayer);
                    }
                    return;
                }
            }
        }
        Orig(thisPtr, target, source);
    }
};

// these are not supposed to be able to switch to capthrow mode, so check Y and current state manually
struct PlayerActorHakoniwaExeRolling : public mallow::hook::Trampoline<PlayerActorHakoniwaExeRolling>{
    static void Callback(PlayerActorHakoniwa* thisPtr){
        if(isPadTriggerGalaxySpin(-1) && !thisPtr->mPlayerAnimator->isAnim("SpinSeparate") && canGalaxySpin) {
            triggerGalaxySpin = true;
            al::setNerve(thisPtr, getNerveAt(spinCapNrvOffset));
            return;
        }
        Orig(thisPtr);
    }
};
struct PlayerActorHakoniwaExeSquat : public mallow::hook::Trampoline<PlayerActorHakoniwaExeSquat>{
    static void Callback(PlayerActorHakoniwa* thisPtr){
        if(isPadTriggerGalaxySpin(-1) && !thisPtr->mPlayerAnimator->isAnim("SpinSeparate") && canGalaxySpin) {
            triggerGalaxySpin = true;
            al::setNerve(thisPtr, getNerveAt(spinCapNrvOffset));
            return;
        }
        Orig(thisPtr);
    }
};

struct PlayerCarryKeeperIsCarryDuringSpin : public mallow::hook::Inline<PlayerCarryKeeperIsCarryDuringSpin>{
    static void Callback(exl::hook::InlineCtx* ctx) {
        if(ctx->X[0] && isGalaxySpin)
            ctx->X[0] = false;
    }
};

struct PadTriggerYHook : public mallow::hook::Trampoline<PadTriggerYHook>{
    static bool Callback(int port){
        if(port == 100)
            return Orig(-1);
        return false;
    };
};

struct PlayerMovementHook : public mallow::hook::Trampoline<PlayerMovementHook>{
    static void Callback(PlayerActorHakoniwa* thisPtr){
        Orig(thisPtr);
    }
};

void tryCapSpinAndRethrow(PlayerActorHakoniwa* player, bool a2) {
    if(isGalaxySpin) {  // currently in GalaxySpin
        bool trySpin = player->tryActionCapSpinAttackImpl(a2);  // try to start another spin, can only succeed for standard throw
        if(!trySpin)
            return;

        if(!isPadTriggerGalaxySpin(-1)) {  // standard throw or fakethrow
            if(canStandardSpin) {
                // tries a standard spin, is allowed to do so
                al::setNerve(player, getNerveAt(spinCapNrvOffset));
                isStandardAfterGalaxySpin = true;
                return;
            }
            else {
                // tries a standard spin, not allowed to do so
                //player->mPlayerSpinCapAttack->tryStartCapSpinAirMiss(player->mPlayerAnimator);
                // fakespins on standard spins should not happen in this mod
                return;
            }
        } else {  // Y pressed => GalaxySpin or fake-GalaxySpin
            if(galaxyFakethrowRemainder != -1 || player->mPlayerAnimator->isAnim("SpinSeparate"))
                return;  // already in fakethrow or GalaxySpin

            if(canGalaxySpin) {
                // tries a GalaxySpin, is allowed to do so => should never happen, but better safe than sorry
                al::setNerve(player, getNerveAt(spinCapNrvOffset));
                return;
            }
            else {
                // tries a GalaxySpin, not allowed to do so
                galaxyFakethrowRemainder = -2;
                return;
            }
        }

        // not attempting or allowed to initiate a spin, so check if should be fakethrow
        if(isPadTriggerGalaxySpin(-1) && galaxyFakethrowRemainder == -1 && !player->mPlayerAnimator->isAnim("SpinSeparate")) {
            // Y button pressed, start a galaxy fakethrow
            galaxyFakethrowRemainder = -2;
            return;
        }
    }
    else {  // currently in standard spin
        bool trySpin = player->tryActionCapSpinAttackImpl(a2);  // try to start another spin, can succeed for GalaxySpin and fakethrow
        if(!trySpin)
            return;

        if(!isPadTriggerGalaxySpin(-1)) {  // standard throw or fakethrow
            if(canStandardSpin) {
                // tries a standard spin, is allowed to do so => should never happen, but better safe than sorry
                al::setNerve(player, getNerveAt(spinCapNrvOffset));
                return;
            }
            else {
                // tries a standard spin, not allowed to do so
                //player->mPlayerSpinCapAttack->tryStartCapSpinAirMiss(player->mPlayerAnimator);
                // fakespins on standard spins should not happen in this mod
                return;
            }
        } else {  // Y pressed => GalaxySpin or fake-GalaxySpin
            if(galaxyFakethrowRemainder != -1 || player->mPlayerAnimator->isAnim("SpinSeparate"))
                return;  // already in fakethrow or GalaxySpin

            if(canGalaxySpin) {
                // tries a GalaxySpin, is allowed to do so
                al::setNerve(player, getNerveAt(spinCapNrvOffset));
                isGalaxyAfterStandardSpin = true;
                return;
            }
            else {
                // tries a GalaxySpin, not allowed to do so
                galaxyFakethrowRemainder = -2;
                return;
            }
        }
    }
}

struct InputIsTriggerActionXexclusivelyHook : public mallow::hook::Trampoline<InputIsTriggerActionXexclusivelyHook>{
    static bool Callback(const al::LiveActor* actor, int port){
        if(port == 100)
            return Orig(actor, PlayerFunction::getPlayerInputPort(actor));
        bool canCapThrow = true;
        switch (mallow::config::getConfg<ModOptions>()->spinButton) {
            case 'Y':
                canCapThrow = al::isPadTriggerX(port);
                break;
            case 'X':
                canCapThrow = al::isPadTriggerY(port);
                break;
        }
        return Orig(actor, port) && canCapThrow;
    }
};

struct InputIsTriggerActionCameraResetHook : public mallow::hook::Trampoline<InputIsTriggerActionCameraResetHook>{
    static bool Callback(const al::LiveActor* actor, int port){
        switch (mallow::config::getConfg<ModOptions>()->spinButton) {
            case 'L':
                return al::isPadTriggerR(port);
            case 'R':
                return al::isPadTriggerL(port);
        }
        return Orig(actor, port);
    }
};

extern "C" void userMain() {
    exl::hook::Initialize();
    PlayerMovementHook::InstallAtSymbol("_ZN19PlayerActorHakoniwa8movementEv");
    mallow::init::installHooks();
    // trigger spin instead of cap throw
    PlayerTryActionCapSpinAttack::InstallAtSymbol("_ZN19PlayerActorHakoniwa26tryActionCapSpinAttackImplEb");
    PlayerSpinCapAttackAppear::InstallAtSymbol("_ZN18PlayerStateSpinCap6appearEv");
    PlayerStateSpinCapKill::InstallAtSymbol("_ZN18PlayerStateSpinCap4killEv");
    PlayerStateSpinCapFall::InstallAtSymbol("_ZN18PlayerStateSpinCap7exeFallEv");
    PlayerSpinCapAttackIsSeparateSingleSpin::InstallAtSymbol("_ZNK19PlayerSpinCapAttack20isSeparateSingleSpinEv");
    PlayerStateSwimExeSwimSpinCap::InstallAtSymbol("_ZN15PlayerStateSwim14exeSwimSpinCapEv");
    PlayerStateSwimExeSwimSpinCapSurface::InstallAtSymbol("_ZN15PlayerStateSwim21exeSwimSpinCapSurfaceEv");
    // UPDATE: do not interrupt underwater dive with GalaxySpin
    //PlayerStateSwimExeSwimHipDropHeadSliding::InstallAtSymbol("_ZN15PlayerStateSwim25exeSwimHipDropHeadSlidingEv");
    PlayerStateSwimKill::InstallAtSymbol("_ZN15PlayerStateSwim4killEv");
    PlayerSpinCapAttackStartSpinSeparateSwimSurface::InstallAtSymbol("_ZN19PlayerSpinCapAttack28startSpinSeparateSwimSurfaceEP14PlayerAnimator");

    // allow carrying an object during a GalaxySpin
    PlayerCarryKeeperIsCarryDuringSpin::InstallAtOffset(0x423A24);

    // allow triggering spin on roll and squat
    PlayerActorHakoniwaExeRolling::InstallAtSymbol("_ZN19PlayerActorHakoniwa10exeRollingEv");
    PlayerActorHakoniwaExeSquat::InstallAtSymbol("_ZN19PlayerActorHakoniwa8exeSquatEv");

    // allow triggering another spin while falling from a spin
    exl::patch::CodePatcher fakethrowPatcher(0x423B80);
    fakethrowPatcher.WriteInst(0x1F2003D5);  // NOP
    fakethrowPatcher.WriteInst(0x1F2003D5);  // NOP
    fakethrowPatcher.WriteInst(0x1F2003D5);  // NOP
    fakethrowPatcher.WriteInst(0x1F2003D5);  // NOP
    fakethrowPatcher.Seek(0x423B9C);
    fakethrowPatcher.BranchInst(reinterpret_cast<void*>(&tryCapSpinAndRethrow));

    // do not cancel momentum on spin
    PlayerConstGetSpinAirSpeedMax::InstallAtSymbol("_ZNK11PlayerConst18getSpinAirSpeedMaxEv");
    PlayerConstGetSpinBrakeFrame::InstallAtSymbol("_ZNK11PlayerConst17getSpinBrakeFrameEv");

    // send out attack messages during spins
    PlayerAttackSensorHook::InstallAtSymbol("_ZN19PlayerActorHakoniwa12attackSensorEPN2al9HitSensorES2_");

    // disable Y button for everything else
    //PadTriggerYHook::InstallAtSymbol("_ZN2al13isPadTriggerYEi");
    InputIsTriggerActionXexclusivelyHook::InstallAtSymbol("_ZN19PlayerInputFunction15isTriggerActionEPKN2al9LiveActorEi");
    InputIsTriggerActionCameraResetHook::InstallAtSymbol("_ZN19PlayerInputFunction20isTriggerCameraResetEPKN2al9LiveActorEi");
    // manually allow hacks and "special things" to use Y button
    exl::patch::CodePatcher yButtonPatcher(0x44C9FC);
    yButtonPatcher.WriteInst(exl::armv8::inst::Movk(exl::armv8::reg::W1, 100));  // isTriggerHackAction
    yButtonPatcher.Seek(0x44C718);
    yButtonPatcher.WriteInst(exl::armv8::inst::Movk(exl::armv8::reg::W1, 100));  // isTriggerAction
    yButtonPatcher.Seek(0x44C5F0);
    yButtonPatcher.WriteInst(exl::armv8::inst::Movk(exl::armv8::reg::W1, 100));  // isTriggerCarryStart
}
