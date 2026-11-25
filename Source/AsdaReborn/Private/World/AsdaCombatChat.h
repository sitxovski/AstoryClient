// Copyright (c) 2026 @sitxovski (Discord)
// ★ R5 (12.11.2026, дубли): СТРОКА БОЯ В СИСТЕМНЫЙ ЧАТ — одна копия связки эталона
//   `CObject_SetSlotString`×N + `CClass_SetSlotPair` → `UI_FormatLocalizedNotice(id)` → `CChat_AddTokenizedLines(g_pChatWnd, текст, цвет, 10000, 0)`
//   (дорожка A, TTL 10 000 мс). До R5 её повторяли шесть мест: урон навыка (`AsdaSkillDamageChat`), строка 4102, тик ДоТа,
//   отражение, навык моба (`UAsdaMobSkillSubsystem::ChatNotice`), промах/неуязвимость рывка.
//   ⚠ Это НЕ `AsdaNotice::ChatColored`: тот идёт через маршрутизатор категорий (звук, приёмники), а бой пишет в чат напрямую —
//   так было во всех шести копиях, и здесь оставлено так же. `LogCombatNotice` и `PostPvpChatLine` мира не тронуты: у них свои
//   счётчики самотеста и предупреждения.
#pragma once

#include "CoreMinimal.h"

class UGameInstance;

// Исход `AsdaCombatChat::Line`: вызывающему иногда важно отличить «нет подсистем» от «нотис пуст в таблице».
enum class EAsdaCombatChat : uint8
{
	NoSubsystems,   // нет GI, подсистемы текста или чата — ничего не сделано, текст не форматировался
	EmptyLine,      // нотис пуст в таблице текста — строки нет (как эталон)
	Posted,         // строка ушла в системный чат
};

namespace AsdaCombatChat
{
	// Формат нотиса NoticeId со строковыми слотами Strs и числовыми Nums; непустая строка — в системный чат цветом Col
	//   (дорожка A, 10 000 мс). OutLine (если задан) получает отформатированный текст (пустой при EmptyLine; не трогается
	//   при NoSubsystems).
	EAsdaCombatChat Line(UGameInstance* GI, int32 NoticeId, const TArray<FString>& Strs, const TArray<int32>& Nums,
		const FColor& Col, FString* OutLine = nullptr);
}
