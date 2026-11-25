// Copyright (c) 2026 @sitxovski (Discord)
// ★★★ 09.11.2026 «Пропавшие» NPC — префикс [MISSING] и модель-замена (разбор и правило — в Public/Npc/AsdaNpcMissing.h).
#include "Npc/AsdaNpcMissing.h"
#include "Npc/AsdaNpcActor.h"
#include "AsdaDevConsole.h"
#include "Animation/AnimSequence.h"
#include "Components/SkeletalMeshComponent.h"
#include "Engine/SkeletalMesh.h"
#include "Npc/AsdaNpcGmOnly.h"   // текущая карта
#include "Engine/GameInstance.h"
#include "Engine/World.h"
#include "AsdaNpcLists.inl"   // ★ встроенные списки (не ini — владелец 09.11)

// Ключи только выключают оформление (в Shipping — константы ASDA_DEV_CVAR); показ скрытых NPC ими не включается.
static ASDA_DEV_CVAR(int32, CVarAsdaNpcMissingPrefix, TEXT("asda.Npc.MissingPrefix"), 1,
	TEXT("[PORT, owner 09.11] Returned NPC (built-in list AsdaNpcLists::kMissingPrefix) get a red [MISSING] prefix before the name ")
	TEXT("(overhead plate and talk window title; the name keeps its colour). 0 - no prefix."),
	ECVF_Default);

static ASDA_DEV_CVAR(int32, CVarAsdaNpcModelFallback, TEXT("asda.Npc.ModelFallback"), 1,
	TEXT("[PORT, owner 09.11] NPC of the built-in list AsdaNpcLists::kModelFallback and any NPC whose mesh has no model get the guard ")
	TEXT("model npc_m_h_soldier-a_00. 0 - as before (empty mesh)."),
	ECVF_Default);

namespace AsdaNpcMissingLocal
{
	static const TCHAR* const kPrefixText = TEXT("[MISSING]");   // текст префикса (не переводится)
	static const FColor kPrefixColor(255, 32, 32, 255);           // красный префикса

	template <int32 N> static bool InList(const int32 (&L)[N], int32 Id) { for (const int32 X : L) { if (X == Id) { return true; } } return false; }
	static FString Hex(const FColor& C) { return FString::Printf(TEXT("%02X%02X%02X%02X"), C.A, C.R, C.G, C.B); }
}

bool AsdaNpcMissing::HasPrefix(const UObject* Ctx, int32 NpcId)
{
	if (CVarAsdaNpcMissingPrefix.GetValueOnGameThread() == 0 || !Ctx) { return false; }
	const UWorld* W = Ctx->GetWorld();
	const UGameInstance* GI = W ? W->GetGameInstance() : nullptr;
	return AsdaNpcLists::Has(AsdaNpcLists::kMissingPrefix, NpcId, AsdaNpcGmOnly::CurrentMap(GI));
}

FString AsdaNpcMissing::PrefixMarkup(const FString& Name, const FLinearColor& NameColor)
{
	using namespace AsdaNpcMissingLocal;
	return FString::Printf(TEXT("<FC=%s>%s</F> <FC=%s>%s</F>"), *Hex(kPrefixColor), kPrefixText, *Hex(NameColor.ToFColor(true)), *Name);
}

bool AsdaNpcMissing::IsModelFallbackId(int32 NpcId)
{
	return AsdaNpcMissingLocal::InList(AsdaNpcLists::kModelFallback, NpcId);
}

bool AsdaNpcMissing::ApplyModelFallback(AAsdaNpcActor* A)
{
	if (!A || !A->Mesh || CVarAsdaNpcModelFallback.GetValueOnGameThread() == 0) { return false; }
	const bool bListed = IsModelFallbackId(A->NpcId);
	const bool bNoModel = A->Mesh->GetSkeletalMeshAsset() == nullptr;
	if (!bListed && !bNoModel) { return false; }
	USkeletalMesh* SK = LoadObject<USkeletalMesh>(nullptr, AsdaNpcLists::kFallbackMesh);
	if (!SK) { UE_LOG(LogTemp, Warning, TEXT("[NPC] модель-замена не загрузилась: %s"), AsdaNpcLists::kFallbackMesh); return false; }
	A->Mesh->SetSkeletalMeshAsset(SK);
	if (UAnimSequence* Idle = LoadObject<UAnimSequence>(nullptr, AsdaNpcLists::kFallbackAnim)) { A->IdleAnim = Idle; }
	UE_LOG(LogTemp, Log, TEXT("[NPC] NPC %d: модель стражника вместо %s (%s)"), A->NpcId,
		bNoModel ? TEXT("пустой модели") : TEXT("модели из списка"), *SK->GetName());
	return true;
}
