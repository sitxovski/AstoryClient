// Copyright (c) 2026 @sitxovski (Discord)
// AsdaSkillFxSubsystem_Private.h — ★ R3-c 12.11.2026 (разрез AsdaSkillFxSubsystem.cpp): ЗАКРЫТЫЙ заголовок частей AsdaSkillFxSubsystem*.cpp.
//   Только нужное нескольким частям: категория журнала (определение — AsdaSkillFxSubsystem.cpp), static-помощники и константы
//   без состояния (текст байт в байт, у каждой части своя копия), extern-объявления ключей/состояния
//   (одно определение — в домашней части). Подключать ПОСЛЕ включений пролога.
#pragma once

#include "Skill/AsdaSkillFxSubsystem.h"
#include "AsdaDevConsole.h"   // ★ античит А2: ASDA_DEV_CVAR / ASDA_DEV_CONSOLE — дев-ключи и команды вне Shipping

#include "AsdaEffectActor.h"
#include "Engine/World.h"
#include "HAL/IConsoleManager.h"
#include "UI/AsdaEffectsSubsystem.h"

// Категория журнала частей AsdaSkillFxSubsystem*.cpp: определение — AsdaSkillFxSubsystem.cpp.
DECLARE_LOG_CATEGORY_EXTERN( LogAsdaSkillFx, Log, All );

// Тег эффект-актора показа навыка Дружбы: по нему GM-«скрыться» восстанавливает собственный множитель эффекта.
static const FName kAsdaSoulSkillGlowTag(TEXT("AsdaSoulSkillGlow"));

// GAsdaSkillHitFrameAlways: определение — в домашней части (R2).
extern int32 GAsdaSkillHitFrameAlways;

// GAsdaSkillHitFxTargetSplit: определение — в домашней части (R2).
extern int32 GAsdaSkillHitFxTargetSplit;

// GAsdaSkillPrimaryMissNoHitFx: определение — в домашней части (R2).
extern int32 GAsdaSkillPrimaryMissNoHitFx;

// GAsdaSkillPrimaryMissGate: определение — в домашней части (R2).
extern bool GAsdaSkillPrimaryMissGate;
// GAsdaSkillRecordReleaseKind: определение — в домашней части (R2).
extern int32 GAsdaSkillRecordReleaseKind;
// GAsdaSkillMissNoDamageText: определение — в домашней части (R2).
extern int32 GAsdaSkillMissNoDamageText;

// GAsdaSkillMobHitSoundAtTarget: определение — в домашней части (R2).
extern int32 GAsdaSkillMobHitSoundAtTarget;

// Ключ CVarAsdaSkillGrenadeArc: определение — в домашней части (R2, без static).
//   ★ R3-c чистка: в Shipping ключ — константа-умолчание, своя копия в каждой части (как до разреза: значение
//   сворачивается компилятором, внешнего объекта нет). Умолчание — одно число на обе ветки.
static constexpr int32 kAsdaSkillGrenadeArcDefault = 1;
#if ASDA_DEV_CONSOLE
extern TAutoConsoleVariable<int32> CVarAsdaSkillGrenadeArc;
#else
static TAsdaConstCVar<int32> CVarAsdaSkillGrenadeArc(kAsdaSkillGrenadeArcDefault);
#endif

// GAsdaShowFromCastIdle: определение — в домашней части (R2).
extern bool GAsdaShowFromCastIdle;

// Ожидаемое появление снаряда у кости одного кастера (слот показа у кастера один — эталон держит то же одним слотом).
struct FAsdaChargePre
{
	int32 SkillId = 0;                 // навык показа
	float AtMs = 0.f;                  // порог, клип-мс (заряд − порог cb4)
	bool bSpawned = false;             // модель уже рождена
	TWeakObjectPtr<AActor> Model;      // модель у кости — снимается в момент вылета
};

// GAsdaChargePreLastS: определение — в домашней части (R2).
extern double GAsdaChargePreLastS;
// GAsdaChargeLaunchLastS: определение — в домашней части (R2).
extern double GAsdaChargeLaunchLastS;

// GAsdaSkillHitRecordPerPhase: определение — в домашней части (R2).
extern int32 GAsdaSkillHitRecordPerPhase;

// AsdaSkillIsPhaseIndexedTick: определение — в домашней части (R2).
bool AsdaSkillIsPhaseIndexedTick(const FString& TickFn);

// GAsdaSkillHexaFourHits: определение — в домашней части (R2).
extern int32 GAsdaSkillHexaFourHits;

// Тик с четырьмя фазами-ударами (эталон: `field_3A0` доходит до 3 только здесь; второй метод — у `CCharacter_UpdateSkillEffectTick`
//   значение 3 означает конец показа, а не удар; скан `Reports/batch17/starlight/scan_multihit.py`: 4 порога в combo time.bin только у 513).
static bool IsHexaFourHitTick(const FString& TickFn)
{
	return GAsdaSkillHexaFourHits != 0 && TickFn.Equals(TEXT("CSkillEffect_Update"), ESearchCase::CaseSensitive);
}

// Эффекты удара фазы (0..3).
static const TArray<FAsdaSkillFxSpawn>& PhaseHitList(const FAsdaSkillFxActive& A, int32 Phase)
{
	switch (Phase)
	{
	case 0: return A.Hit0;
	case 1: return A.Hit1;
	case 3: return A.Hit3;
	default: return A.Hit2;
	}
}

namespace {
	// ★ Кость спавна эффектов навыка — «кончик оружия». Эталон SkillEffect_UpdateScaledStrikeHit@0xA72610:
	//   List_FindIndexByName(model, "8_a_End") → Effect_SpawnByName(..., boneIdx, /*attach*/7, ...).
	const TCHAR* kFxBone = TEXT("8_a_End");
	// ★★★ …НО НЕ ВСЕГДА. `Char_UpdateSkillAnim_MultiPhase@0xA76E70` (`entity_char.c:29620-29623`):
	//     if (pChar[52091] < 8u || pChar[52091] > 0xBu) bone = "8_a_End"; else bone = "8_b1";
	//   `pChar[52091]` — это `equippedWeaponType` (доказательство: `entity_char.c:50683`
	//   `dstChar->equippedWeaponType = *((_DWORD *)srcNet + 52091)`). Подтипы 8..0xB — лук/арбалет/
	//   баллиста: у них точка эффекта не «кончик клинка», а «тетива» — та же кость «8_b1», которую
	//   `AsdaWorldSubsystem` уже использует стартом стрелы (там же, эталон `pChar[52082]`).
	const TCHAR* kFxBoneRanged = TEXT("8_b1");
	// Кость привязки эффектов навыка по подтипу оружия — точный порт развилки выше.
	FORCEINLINE const TCHAR* FxBoneFor(int32 WeaponSubType)
	{
		return (WeaponSubType >= 8 && WeaponSubType <= 0xB) ? kFxBoneRanged : kFxBone;
	}
}

// GAsdaSkillFxScale: определение — в домашней части (R2).
extern float GAsdaSkillFxScale;

// GAsdaSkillFxStrictQuad: определение — в домашней части (R2).
extern bool GAsdaSkillFxStrictQuad;
