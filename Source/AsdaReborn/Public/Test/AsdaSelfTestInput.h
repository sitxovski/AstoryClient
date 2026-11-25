// Copyright (c) 2026 @sitxovski (Discord)
// ★★★ САМОТЕСТЫ: ОБЩИЕ ПОМОЩНИКИ ВВОДА И ГЕОМЕТРИИ `AsdaSelfTestInput` (R5-c, волна дублей, 12.11.2026). [ПОРТ]
//   Аудит и отчёт: `Reports/refactor_2026-11/r5/c/AUDIT.md`, `REPORT.md`.
//
//   ЗАЧЕМ. Самотесты окон наводят мышь, ищут центр живого прямоугольника узла и нажимают клавиши своими копиями одних
//   и тех же функций: `Hover` — 17 дословных копий, `Move` — 5, `Center` — 20 (два написания с одним результатом),
//   `CenterOf` — 3, `KeyEv` — 3, плюс `HasCyrillic` — 5 и `DumpLine` — 3. Здесь — по одной функции на каждую.
//
//   КАК ПОДКЛЮЧАЕТСЯ. В самотесте на месте прежней копии стоит её комментарий и `using AsdaSelfTestInput::Hover;` —
//   вызовы не меняются. Функции разные по смыслу и НЕ взаимозаменяемы:
//     • `Hover` проверяет, что Slate поднят; `Move` — нет (так было у копий, каждая оставлена как была);
//     • `Center` требует, чтобы узел был показан (`AsdaUi::IsShown`) и имел размер ≥ 2 px; `CenterOf` — только ненулевой размер.
//   Только для самотестов; состояния игры не трогает.
#pragma once
#include "CoreMinimal.h"
#include "Components/Widget.h"
#include "Engine/World.h"
#include "Framework/Application/SlateApplication.h"
#include "GameFramework/PlayerController.h"

#include "UI/AsdaUiHelpers.h"

namespace AsdaSelfTestInput
{
	// Наведение без щелчка: курсор в точку P и событие движения мыши Slate. Slate не поднят — ничего.
	inline void Hover(const FVector2D& P)
	{
		if (!FSlateApplication::IsInitialized()) { return; }
		FSlateApplication& App = FSlateApplication::Get();
		App.SetCursorPos(P);
		App.ProcessMouseMoveEvent(FPointerEvent(0, P, P, TSet<FKey>(), EKeys::Invalid, 0.f, FModifierKeysState()));
	}

	// Настоящее движение мыши: курсор в точку P и событие движения Slate (без проверки, что Slate поднят, — как у копий).
	inline void Move(const FVector2D& P)
	{
		FSlateApplication& App = FSlateApplication::Get();
		App.SetCursorPos(P);
		App.ProcessMouseMoveEvent(FPointerEvent(0, P, P, TSet<FKey>(), EKeys::Invalid, 0.f, FModifierKeysState()));
	}

	// Центр живого прямоугольника узла (false — не отрисован / скрыт / размер меньше 2 px).
	//   У десяти копий `Mn`/`Mx` объявлялись без начального значения — на результат не влияло: `AsdaUi::WidgetAbsRect` при
	//   `true` пишет оба выхода, а при `false` функция выходит, не читая их.
	inline bool Center(const UWidget* Wd, FVector2D& Out)
	{
		FVector2D Mn = FVector2D::ZeroVector, Mx = FVector2D::ZeroVector;
		if (!Wd || !AsdaUi::IsShown(Wd) || !AsdaUi::WidgetAbsRect(Wd, Mn, Mx) || (Mx - Mn).X < 2.0 || (Mx - Mn).Y < 2.0) { return false; }
		Out = (Mn + Mx) * 0.5f;
		return true;
	}

	// Центр живой геометрии (false — узла нет / не отрисован / нулевой размер). Показан ли узел — НЕ проверяется.
	inline bool CenterOf(const UWidget* X, FVector2D& Out)
	{
		FVector2D Mn, Mx;
		if (!X || !AsdaUi::WidgetAbsRect(X, Mn, Mx) || (Mx - Mn).IsNearlyZero()) { return false; }
		Out = (Mn + Mx) * 0.5f;
		return true;
	}

	// Нажать/отпустить клавишу штатным путём `UPlayerInput` первого контроллера мира (как `asda.Keybinds.HoldSim`).
	inline void KeyEv(UWorld* W, const FKey& K, bool bDown)
	{
		APlayerController* PC = W ? W->GetFirstPlayerController() : nullptr;
		if (PC) { PC->InputKey(FInputKeyEventArgs::CreateSimulated(K, bDown ? IE_Pressed : IE_Released, bDown ? 1.f : 0.f)); }
	}

	// Есть ли в строке кириллица (признак RU-текста).
	inline bool HasCyrillic(const FString& S)
	{
		for (const TCHAR C : S) { if (C >= 0x0400 && C <= 0x04FF) { return true; } }
		return false;
	}

	// Строка разбора окна по тегу (`DumpWindow`: первая строка, которая после пробелов начинается с Tag), пусто — нет строки.
	inline FString DumpLine(const FString& Dump, const FString& Tag)
	{
		TArray<FString> Lines;
		Dump.ParseIntoArrayLines(Lines);
		for (const FString& L : Lines) { if (L.TrimStart().StartsWith(Tag)) { return L; } }
		return FString();
	}
}
