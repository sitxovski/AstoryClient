// Copyright (c) 2026 @sitxovski (Discord)
// Флаг прогона офлайн-самотеста `FAsdaSelfTestRunGate`. Реализация `Pass` — где и была (`AsdaOutgoingGate.cpp`).
//   ★ R3 рефакторинга (11.11.2026): дословно вынесено из `AsdaOutgoingGate.h` (он включает этот файл — прежние потребители ничего
//   не теряют). Причина: 54 самотеста включали весь гейт исходящих `AsdaOutgoingGate.h` только ради этого флага.
//   Отражаемых типов здесь нет.
#pragma once
#include "CoreMinimal.h"

// ★★ СРЕДН-1 (ревизия shop_offline_fix, 10.10.2026): общий флаг прогона офлайн-самотеста, чьи шаги расписаны
//   НЕЗАВИСИМЫМИ таймерами (`At(T, …)`, пачка `SetTimer`). Держится в `TSharedRef` и захватывается каждым шагом; шаг
//   первой строкой зовёт `Pass`. Проверку делает первый сработавший шаг — повторная проверка офлайна на ОТЛОЖЕННОМ
//   старте (`RefuseDeferredStartWhenOnline`); при отказе все шаги прогона пропускаются, итог пишется один раз.
//   Соединение, появившееся ПОСЛЕ старта, ловит событие `FAsdaOutgoingGate::OnConnectionAppeared` (МЕЛК-2).
//   Образец — `asda.ExtraMenu.DragSelfTest`, `asda.Title.NoticeTest`, `asda.PartyFind.SelfTest`.
struct ASDAREBORN_API FAsdaSelfTestRunGate
{
	// Cmd — имя консольной команды для итога; bQuit — закрыть процесс при отказе (аргумент `quit` самотеста).
	FAsdaSelfTestRunGate(const TCHAR* InCmd, bool bInQuit) : Cmd(InCmd ? InCmd : TEXT("?")), bQuit(bInQuit) {}

	// true — шаг можно выполнять; false — прогон отменён (на отложенном старте не было офлайна), шаг должен сразу выйти.
	//   Ctx — мир, GameInstance или его подсистема (как у `RefuseDebugWhenOnline`).
	bool Pass(const UObject* Ctx);

	// Прогон отменён (был отказ на отложенном старте).
	bool IsRefused() const { return State < 0; }

	FString Cmd;      // имя самотеста (для итога отказа)
	bool bQuit;       // `quit` при отказе
	int8 State = 0;   // 0 — старт ещё не проверен, 1 — старт офлайн, −1 — отменён
};
