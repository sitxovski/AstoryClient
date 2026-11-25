// Copyright (c) 2026 @sitxovski (Discord)
// ★★★ 09.11.2026 [ПОРТ, РЕШЕНИЕ ВЛАДЕЛЬЦА] «ПРОПАВШИЕ» NPC — красный префикс [MISSING] и модель-замена (пачка 20, баг 7).
//   Владелец 09.11: «For any npc that missing their models — use any guard model and for missing npcs put in it's name "[MISSING]"
//   red prefix in name»; по Agnes (NPC 7) — «Без префикса».
//   1) ПРЕФИКС: NPC из списка `AsdaNpcLists::kMissingPrefix` — перед именем красный «[MISSING]» (цвет только у префикса, имя своим
//      цветом) в подписи над головой (`UAsdaNameplateComponent::RefreshNameplate`) и в заголовке окна разговора
//      (`UAsdaNpcDialogSubsystem`). RU и EN одинаково, не переводится. Ключ `asda.Npc.MissingPrefix` 1; 0 — без префикса.
//      Двухцветная строка — через общую точку разметки порта `UAsdaRichText::ApplyFormatted` (`<FC=AARRGGBB>…</F>`), как титул.
//   2) МОДЕЛЬ-ЗАМЕНА: NPC из списка `AsdaNpcLists::kModelFallback` и любой NPC, чья модель не загрузилась (у меша нет ассета),
//      получает модель стражника npc_m_h_soldier-a_00 (солдаты Альпии; путь — в той же таблице).
//      Ключ `asda.Npc.ModelFallback` 1; 0 — как было (пустой меш). Зовётся из `AAsdaNpcActor::BeginPlay`.
//   Оба списка — данные, встроенные в бинарь (Private/Npc/AsdaNpcLists.inl), не ini (владелец 09.11: «Internal mechanics needed»).
#pragma once

#include "CoreMinimal.h"

class AAsdaNpcActor;

namespace AsdaNpcMissing
{
	// Красный префикс «[MISSING]» у этого NPC на текущей карте (ключ включён и пара (NPC, карта) в `AsdaNpcLists::kMissingPrefix`).
	//   Ctx — любой объект мира (компонент, подсистема GI).
	ASDAREBORN_API bool HasPrefix(const UObject* Ctx, int32 NpcId);
	// Разметка «<красный>[MISSING]</> <цвет имени>Name</>» для `UAsdaRichText::ApplyFormatted`.
	ASDAREBORN_API FString PrefixMarkup(const FString& Name, const FLinearColor& NameColor);
	// Модель-замена: NPC в списке `AsdaNpcLists::kModelFallback` или у меша нет модели. true — модель поставлена.
	ASDAREBORN_API bool ApplyModelFallback(AAsdaNpcActor* A);
	// NPC в списке модели-замены (для самотеста и отчёта).
	ASDAREBORN_API bool IsModelFallbackId(int32 NpcId);
}
