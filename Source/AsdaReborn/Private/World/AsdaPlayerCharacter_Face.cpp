// Copyright (c) 2026 @sitxovski (Discord)
// ★★★ ВЫРАЖЕНИЕ ЛИЦА ПЕРСОНАЖА (07.11.2026, баг «эмоции не меняют лицо»). [ОРИГИНАЛ] Ключ `asda.Char.FaceExpression` (0 — лицо не меняется, прежний порт).
//   Эталон: текстура лица `ch1_<m|f>_00_face001_NN_00_0` — атлас 2×2, UV меша лежат в левой верхней четверти.
//     `meshSetPartBlendFactorsByMode@0x9E1DA0` сдвигает UV подсеток лица: режим 0 (0, 0) обычное, 1 (0.5, 0) глаза закрыты, 2 (0, 0.5) злое;
//     пишет режим и обнуляет таймер (`gap_A42[22668]`, `[22664]`).
//   Кто зовёт (`CCharacter_SetAction@0x9EE720`, `CChar_SetState@0xA13D50`): режим 2 — покой подтипа 20, атака 5, навык 24; все прочие действия,
//     все эмоции 33…47 тоже, — режим 0. Своего лица у эмоции в оригинале нет.
//   Боевой покой: конец прохода клипа → режим 0 (`CGameUI_UpdateFadeAnim@0xA17B00`). Моргание — только в покое 0 и сидя 8
//     (та же функция и `objAnimTickMode32@0xA16A80`): 3000 мс в режиме 0 → 1, 150 мс в режиме 1 → 0.
//   [ПОРТ] Сдвиг UV заменён сменой текстуры: копии `_e1` / `_e2` сдвинуты на полатласа (Tools/asda_import_face_expr_textures.py). Общий мастер
//     M_AsdaCharLit не меняется, смена работает на любом мастере слота («душа», появление).
#include "World/AsdaPlayerCharacter.h"
#include "World/AsdaSkillCastLock.h"
#include "UI/AsdaCastBarSubsystem.h"
#include "AsdaDevConsole.h"
#include "Resources/AsdaAssetPaths.h"
#include "Resources/AsdaMaterialNames.h"
#include "Resources/AsdaTextureSubsystem.h"
#include "Animation/AnimSingleNodeInstance.h"
#include "Components/SkeletalMeshComponent.h"
#include "Engine/Engine.h"
#include "Engine/GameInstance.h"
#include "Engine/Texture2D.h"
#include "Materials/MaterialInstanceDynamic.h"

DEFINE_LOG_CATEGORY_STATIC(LogAsdaFace, Log, All);

static ASDA_DEV_CVAR(int32, CVarAsdaCharFaceExpression, TEXT("asda.Char.FaceExpression"), 1,
	TEXT("[ОРИГИНАЛ] Выражение лица: 1 — злое в бою, моргание в покое и сидя, как эталон meshSetPartBlendFactorsByMode; 0 — лицо не меняется (прежний порт)."),
	ECVF_Default);

namespace
{
	constexpr float kFaceBlinkOpenMs = 3000.f;    // эталон: режим 0 дольше 3000 мс → глаза закрываются
	constexpr float kFaceBlinkClosedMs = 150.f;   // эталон: режим 1 дольше 150 мс → глаза открываются
	constexpr float kFaceClipFallbackS = 2.f;     // длина прохода, если клип покоя не прочитан

	// Слот лица базового тела: имя материала содержит «face» (то же правило, что у `UAsdaCharAssembly::ApplyFace`).
	int32 FindFaceSlot(const USkeletalMeshComponent* Skel)
	{
		if (!Skel) { return INDEX_NONE; }
		for (int32 i = 0; i < Skel->GetNumMaterials(); ++i)
		{
			const UMaterialInterface* M = Skel->GetMaterial(i);
			if (M && M->GetName().Contains(TEXT("face"))) { return i; }
		}
		return INDEX_NONE;
	}

	// Длина клипа, который сейчас играет тело, сек (запасное значение, если клип не прочитан).
	float CurrentClipLength(USkeletalMeshComponent* Skel)
	{
		UAnimSingleNodeInstance* Sni = Skel ? Skel->GetSingleNodeInstance() : nullptr;
		const float Len = Sni ? Sni->GetLength() : 0.f;
		return Len > 0.f ? Len : kFaceClipFallbackS;
	}
}

int32 AAsdaPlayerCharacter::GetFaceActionState() const
{
	if (AnimState == EAsdaPlayerState::Dead) { return 6; }
	if (const int32 Sit = GetEtalonSitState()) { return Sit; }
	// Свой навык: фаза заявки и полоса каста, как в `AAsdaPlayerController::KbEtalonActionState`.
	if (IsLocallyControlled())
	{
		UGameInstance* GI = GetGameInstance();
		if (const int32 Phase = AsdaSkillCastLock::PhaseForActionState(GI)) { return Phase; }
		if (const UAsdaCastBarSubsystem* Bar = GI ? GI->GetSubsystem<UAsdaCastBarSubsystem>() : nullptr)
		{
			const int32 Cs = Bar->GetState();
			if (Bar->IsActive() && (Cs == 13 || Cs == 14 || Cs == 24 || Cs == 27)) { return Cs; }
		}
	}
	if (IsAttackAnimActive()) { return 5; }
	if (IsSkillAnimActive()) { return 24; }
	if (const int32 Busy = GetBusyActionCode()) { return Busy; }
	return AnimState == EAsdaPlayerState::Move ? 1 : 0;
}

void AAsdaPlayerCharacter::SetFaceMode(int32 Mode)
{
	FaceMode = Mode;
	FaceTimerMs = 0.f;
}

void AAsdaPlayerCharacter::TickFaceExpression(float DeltaSeconds)
{
	if (CVarAsdaCharFaceExpression.GetValueOnGameThread() == 0)
	{
		SetFaceMode(0);
		FaceLastState = -1;
		FaceClipEndS = 0.0;
		if (FaceShownMode != 0) { ApplyFaceTexture(); }
		return;
	}
	UWorld* W = GetWorld();
	if (!W) { return; }
	const double Now = W->GetTimeSeconds();
	const int32 St = GetFaceActionState();
	const bool bStance = (St == 0 && bCombatStance);
	// Вход в действие; покой подтипа 20 эталон взводит и повторно, пока стоит.
	if (St != FaceLastState || (bStance && CombatStanceSinceS != FaceStanceSinceS))
	{
		FaceClipEndS = 0.0;
		if (St == 5 || St == 24) { SetFaceMode(2); }
		else if (bStance)
		{
			SetFaceMode(2);
			FaceClipEndS = Now + CurrentClipLength(BaseSkel);
		}
		else { SetFaceMode(0); }
	}
	FaceLastState = St;
	FaceStanceSinceS = CombatStanceSinceS;
	// Конец прохода клипа боевого покоя: эталон ставит режим 0 на каждом проходе.
	if (FaceClipEndS > 0.0 && Now >= FaceClipEndS)
	{
		FaceClipEndS = bStance ? Now + CurrentClipLength(BaseSkel) : 0.0;
		SetFaceMode(0);
	}
	// Моргание: только покой и сидение; злое лицо таймер не закрывает.
	if (St == 0 || St == 8)
	{
		FaceTimerMs += DeltaSeconds * 1000.f;
		if (FaceMode == 0 && FaceTimerMs > kFaceBlinkOpenMs) { SetFaceMode(1); }
		else if (FaceMode == 1 && FaceTimerMs > kFaceBlinkClosedMs) { SetFaceMode(0); }
	}
	if (FaceShownMode != FaceMode) { ApplyFaceTexture(); }
}

void AAsdaPlayerCharacter::ApplyFaceTexture()
{
	FaceShownMode = FaceMode;
	if (!bHasLastAppliedParts || !BaseSkel) { return; }
	const int32 Slot = FindFaceSlot(BaseSkel);
	if (Slot == INDEX_NONE) { return; }
	// Индекс не задан — `ApplyFace` оставляет лицо 00 меша, режим берём у него же.
	static const TCHAR* const kSuffix[3] = { TEXT(""), TEXT("_e1"), TEXT("_e2") };
	const FString Name = AsdaAssetPaths::FaceTextureName(Sex != 2, FMath::Max(LastAppliedParts.FaceIndex, 0)) + kSuffix[FMath::Clamp(FaceMode, 0, 2)];
	UAsdaTextureSubsystem* TexMgr = GEngine ? GEngine->GetEngineSubsystem<UAsdaTextureSubsystem>() : nullptr;
	UTexture2D* Tex = TexMgr ? TexMgr->LoadTextureCached(AsdaAssetPaths::CharTextureObject(Name)) : nullptr;
	if (!Tex)
	{
		UE_LOG(LogAsdaFace, Warning, TEXT("Лицо: текстура не найдена %s"), *Name);
		return;
	}
	UMaterialInstanceDynamic* Mid = Cast<UMaterialInstanceDynamic>(BaseSkel->GetMaterial(Slot));
	if (!Mid) { Mid = BaseSkel->CreateDynamicMaterialInstance(Slot, BaseSkel->GetMaterial(Slot)); }
	if (Mid) { Mid->SetTextureParameterValue(AsdaMat::kParamTex, Tex); }
}

FString AAsdaPlayerCharacter::GetFaceTextureShown() const
{
	const int32 Slot = FindFaceSlot(BaseSkel);
	UTexture* Tex = nullptr;
	if (Slot != INDEX_NONE && BaseSkel->GetMaterial(Slot))
	{
		BaseSkel->GetMaterial(Slot)->GetTextureParameterValue(FName(AsdaMat::kParamTex), Tex);
	}
	return Tex ? Tex->GetName() : FString();
}
