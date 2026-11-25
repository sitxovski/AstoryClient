// Copyright (c) 2026 @sitxovski (Discord)
// AsdaPlayerCharacterInternal.h — ★ 23.10.2026 (разнос AsdaPlayerCharacter.cpp): ЗАКРЫТЫЙ заголовок частей AsdaPlayerCharacter*.cpp.
//   Только нужное нескольким частям: категория журнала LogAsdaPlayer (один экземпляр — DEFINE_LOG_CATEGORY в
//   AsdaPlayerCharacter.cpp) и чистые static-константы/хелперы без состояния — текст байт в байт, у каждой части своя копия.
//   Подключать ПОСЛЕ #include пролога.
#pragma once

#include "CoreMinimal.h"
#include "World/AsdaPlayerCharacter.h"
#include "World/AsdaCombatTrace.h"

// Категория журнала персонажа: объявление; определение — AsdaPlayerCharacter.cpp (до разноса там был DEFINE_LOG_CATEGORY_STATIC).
DECLARE_LOG_CATEGORY_EXTERN(LogAsdaPlayer, Log, All);


#if ASDA_COMBAT_TRACE
// ★ Журнал боя (18.10.2026): «кто» для строки журнала — свой/сосед и имя с плашки. Только чистые геттеры.
static FString AsdaCtPcWho(const AAsdaPlayerCharacter* C)
{
	if (!C) { return TEXT("?"); }
	const FString Nm = (C->Nameplate && !C->Nameplate->DisplayName.IsEmpty()) ? C->Nameplate->DisplayName : C->GetName();
	return FString::Printf(TEXT("%s '%s'"), C->IsLocalPlayerCharacter() ? TEXT("свой") : TEXT("сосед"), *Nm);
}

// ★ Журнал боя (18.10.2026): цель разворота в замахе — моб (rid + имя), игрок (свой/сосед + имя) или «нет».
static FString AsdaCtPcTarget(const AActor* A)
{
	if (!A) { return TEXT("нет"); }
	if (const AAsdaMobActor* M = Cast<AAsdaMobActor>(A))
	{
		return FString::Printf(TEXT("моб rid=%d '%s' HP %d/%d"), M->NetRenderId, *M->MobName, M->CurHp, M->MaxHp);
	}
	if (const AAsdaPlayerCharacter* P = Cast<AAsdaPlayerCharacter>(A)) { return FString::Printf(TEXT("игрок %s"), *AsdaCtPcWho(P)); }
	return A->GetName();
}

// ★ Журнал боя (волна 2, 18.10.2026): «наш» ли персонаж — свой всегда (уровень 1); сосед — по его userIdx
//   (`FindUserIdxByActor`, своего поля номера у персонажа нет) через `IsUserRelevant` (цель PvP/участник нашего боя),
//   иначе уровень 2. Только чистые геттеры; зовётся лишь внутри аргумента ASDA_CTRACE_IF / IsOn.
static bool AsdaCtPcOurs(const AAsdaPlayerCharacter* C)
{
	if (!C) { return false; }
	if (C->IsLocalPlayerCharacter()) { return true; }
	const UGameInstance* GI = C->GetGameInstance();
	const UAsdaWorldSubsystem* WS = GI ? GI->GetSubsystem<UAsdaWorldSubsystem>() : nullptr;
	const int32 Uidx = WS ? WS->FindUserIdxByActor(C) : -1;
	return Uidx >= 0 && AsdaCombatTrace::IsUserRelevant(Uidx);
}
#endif

// База путей превью-скелета (клипы мира = previewanim_12..19; конверсия game↔UE: (x,y,z)→(-z,x,y)).
// ★ 02.09.2026: литерал больше НЕ выписан здесь — берётся из общей точки `AsdaCharPreviewBase`
//   (`World/AsdaPlayerCharacter.h`, закон `port-shared-points`). Обёртка оставлена, потому что
//   вызывающие ждут `FString`.
static FString CharBase(uint8 Sex)
{
	return FString(AsdaCharPreviewBase(Sex));
}
