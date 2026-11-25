// Copyright (c) 2026 @sitxovski (Discord)
// ★ 04.11.2026 [ИСПРАВЛЕНИЕ] (questmark-chmella): ОДНО правило «запись сценария .NSR без своей страницы» для ОБОИХ переборов
//   записей — метки над NPC / на картах (`UAsdaQuestMarkerSubsystem::EvalMarkForNsr`, эталон questFindMatchingActionMode1/2)
//   и разговора (`UAsdaNpcDialogSubsystem::RunScriptRecordScan`, эталон uiCraftSlotClickHandler). Разбор — в определении
//   (`Private/Quest/AsdaQuestMarkerSubsystem.cpp`, блок «ОБЩЕЕ ПРАВИЛО: запись без страницы»).
#pragma once

#include "CoreMinimal.h"

namespace AsdaNsrScript
{
	// true — запись надо ПРОПУСТИТЬ (перебор идёт дальше): страницы `PageId` в сценарии `NsrName` нет и ключ
	//   `asda.Npc.SkipMissingPage` включён (по умолчанию 1). Ключ 0 — прежнее поведение обоих переборов.
	ASDAREBORN_API bool ShouldSkipRecordWithoutPage(const FString& NsrName, int32 PageId);

	// true — выдача задания `QuestId` закрыта (06.11.2026, решение владельца): его цель не спавнится. Запись сценария и ячейка
	//   выдачи не показываются и не дают метку. Ключ `asda.Quest.ClosedStart 0` — как в оригинале.
	ASDAREBORN_API bool StartClosed(int32 QuestId);

	// Страница строки облачка заданий (ячейка SNQ «выдаёт/принимает», запись SNQICT `Key` → `TalkId`). Страница есть (или
	//   ключ выключен) — `TalkId` как есть. Страницы нет — ЕДИНСТВЕННАЯ страница этого сценария, у которой шаг несёт состояние
	//   `Key` (тот же ключ условия, что у ячейки; у ячеек выдачи шаг принимает ровно этот квест); такой нет — −1 (строку не
	//   показывать и не считать в метку). Ключ `asda.Npc.CellPageRedirect 0` — без перенаправления (только −1).
	ASDAREBORN_API int32 ResolveCellTalkId(const FString& NsrName, int32 TalkId, int32 Key);
}
