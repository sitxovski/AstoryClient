// Copyright (c) 2026 @sitxovski (Discord)
// ★★ САМОТЕСТ «ПРИСЕД САМ, CTRL НЕ НАЖАТ» (08.11.2026, пачка 15, баг 3). [ИСПРАВЛЕНИЕ] `asda.Crouch.StrayGuard`
//
// Разбор — у ключа в `AsdaPlayerCharacter_Move.cpp`. Режим «Нажатие» (по умолчанию): присед переключает отпускание ОДИНОЧНОГО Ctrl;
// Ctrl с чем-то ещё — сочетание. Проверка на пути игрока: настоящий ввод контроллера (`InputKey`, как от вьюпорта), настоящие
// события Slate (`ProcessMouseWheelOrGestureEvent` — проходят перехватчик и уходят окнам), тик персонажа, строка чата.
//   K. Контроль: одиночный Ctrl — ВКЛ, ещё раз — ВЫКЛ; W зажата до Ctrl — Ctrl переключает (ход вприсядку); Ctrl + клавиша,
//      зажатая на кадры, — сочетание.
//   B1. Ctrl + колесо (нажатие и отпускание в одном кадре, контроллер) — сочетание.
//   B2. Ctrl + быстрая клавиша (нажатие и отпускание в одном кадре) — сочетание.
//   B3. Shift зажат до Ctrl (смена раскладки Shift+Ctrl) — сочетание.
//   B4. Ctrl + колесо через Slate (над окном) — сочетание.
//   B5. Ctrl + событие, которое увидел только Slate (окно UMG съело клавишу) — сочетание.
//   B6. Ctrl зажат в строке чата, строка закрыта, Ctrl отпущен — сочетание.
//   H.  «Зажатие» не изменилось: Ctrl зажат — присед, колесо его не снимает, отпущен — встал.
// `guard=0` — прогон с ключом 0 (прежняя логика): B1–B6 ДОЛЖНЫ воспроизвести ложный присед (шаг 0 бага).
// Консоль: `asda.SelfTest.CrouchStray [guard=0] [delay=N] [quit]`. Только офлайн. Строки «САМОТЕСТ-ПРИСЕД-САМ», итог «… ИТОГ: ok=… fail=…».
#include "CoreMinimal.h"
#include "AsdaDevConsole.h"

#if ASDA_DEV_CONSOLE && !UE_BUILD_SHIPPING

#include "AsdaCVarSave.h"
#include "Test/AsdaSelfTestKit.h"
#include "Resources/AsdaGameUserSettings.h"
#include "World/AsdaPlayerCharacter.h"
#include "UI/AsdaChatWindowSubsystem.h"
#include "Engine/Engine.h"
#include "Engine/GameInstance.h"
#include "Engine/World.h"
#include "HAL/IConsoleManager.h"
#include "InputCoreTypes.h"
#include "GameFramework/PlayerController.h"
#include "Containers/Ticker.h"
#include "Framework/Application/SlateApplication.h"

DEFINE_LOG_CATEGORY_STATIC(LogAsdaCrouchStrayTest, Log, All);

void AsdaCrouchStrayDebugSlateEvent();   // AsdaPlayerCharacter_Move.cpp: событие, увиденное только перехватчиком Slate

namespace AsdaCrouchStrayTest
{
	static const TCHAR* const kCmd = TEXT("asda.SelfTest.CrouchStray");
	static const TCHAR* const kPrefix = TEXT("САМОТЕСТ-ПРИСЕД-САМ");

	// Шаг: пауза `Delay` с от начала шага, затем `Fn` (один раз).
	struct FStep
	{
		FString Name;            // имя шага (журнал)
		double Delay = 0.0;      // пауза до вызова, с
		TFunction<void()> Fn;    // тело
	};

	// Состояние прогона.
	struct FState
	{
		TWeakObjectPtr<UGameInstance> GI;   // экземпляр игры
		bool bQuit = false;                 // `quit` в конце
		bool bGuard = true;                 // ключ asda.Crouch.StrayGuard на прогон (false — воспроизведение прежнего)
		int32 Ok = 0, Fail = 0;             // счёт
		int32 OrigMode = 0;                 // режим профиля до прогона (вернуть)
		TArray<FStep> Steps;                // шаги
		int32 Next = 0;                     // текущий шаг
		double StepT = 0.0;                 // время входа в текущий шаг
		bool bReady = false;                // свой персонаж найден

		UWorld* W() const { return GI.IsValid() ? GI->GetWorld() : nullptr; }
		APlayerController* PC() const { UWorld* Wd = W(); return Wd ? Wd->GetFirstPlayerController() : nullptr; }
		AAsdaPlayerCharacter* Me() const { APlayerController* P = PC(); return P ? Cast<AAsdaPlayerCharacter>(P->GetPawn()) : nullptr; }
		UAsdaChatWindowSubsystem* Chat() const { return GI.IsValid() ? GI->GetSubsystem<UAsdaChatWindowSubsystem>() : nullptr; }

		void Expect(bool b, const FString& What)
		{
			if (b) { ++Ok; UE_LOG(LogAsdaCrouchStrayTest, Display, TEXT("%s ok   %s"), kPrefix, *What); }
			else   { ++Fail; UE_LOG(LogAsdaCrouchStrayTest, Error, TEXT("%s FAIL %s"), kPrefix, *What); }
		}
		void Do(const TCHAR* Name, double Delay, TFunction<void()> Fn)
		{
			FStep St; St.Name = Name; St.Delay = Delay; St.Fn = MoveTemp(Fn);
			Steps.Add(MoveTemp(St));
		}
	};

	// Нажать/отпустить клавишу штатным вводом контроллера (как событие вьюпорта).
	static void KeyEv(const FState& S, const FKey& K, bool bDown)
	{
		if (APlayerController* P = S.PC()) { P->InputKey(FInputKeyEventArgs::CreateSimulated(K, bDown ? IE_Pressed : IE_Released, bDown ? 1.f : 0.f)); }
	}
	// Нажатие и отпускание в ОДНОМ кадре (колесо, быстрая клавиша при низком FPS).
	static void TapSameFrame(const FState& S, const FKey& K)
	{
		KeyEv(S, K, true);
		KeyEv(S, K, false);
	}
	// Колесо через Slate: перехватчики ввода, затем окна/вьюпорт — настоящий путь события ОС.
	static void SlateWheel()
	{
		if (!FSlateApplication::IsInitialized()) { return; }
		FSlateApplication& App = FSlateApplication::Get();
		const FVector2D Pos = App.GetCursorPos();
		FPointerEvent Ev((uint32)0, Pos, Pos, TSet<FKey>(), EKeys::MouseWheelAxis, 1.f, FModifierKeysState());
		App.ProcessMouseWheelOrGestureEvent(Ev, nullptr);
	}

	static void SetMode(int32 M) { if (UAsdaGameUserSettings* P = UAsdaGameUserSettings::Get()) { P->CrouchMode = M; } }

	static void Finish(TSharedRef<FState> S)
	{
		if (AAsdaPlayerCharacter* Me = S->Me()) { Me->SetCrouch(false, TEXT("итог самотеста")); }
		if (UAsdaChatWindowSubsystem* Cw = S->Chat()) { if (Cw->IsInputActive()) { Cw->CloseInput(); } }
		SetMode(S->OrigMode);
		AsdaSelfTestKit::Finish(S->W(), kCmd, S->bQuit, LogAsdaCrouchStrayTest, kPrefix, S->Ok, S->Fail,
			*FString::Printf(TEXT(" (ключ StrayGuard=%d)"), S->bGuard ? 1 : 0));
	}

	// Сценарий «сочетание»: Pre — до Ctrl, Mid — пока Ctrl зажат, затем отпускание; ожидание: присед не переключился.
	//   При guard=0 сценарии бага ждут ОБРАТНОГО (прежняя логика переключала) — это и есть воспроизведение.
	static void AddCombo(TSharedRef<FState> S, const TCHAR* Id, const TCHAR* What, bool bBugCase,
		TFunction<void()> Pre, TFunction<void()> Mid, TFunction<void()> Post = nullptr)
	{
		const FString IdS = Id, WhatS = What;
		S->Do(Id, 0.15, [S, Pre]() { if (AAsdaPlayerCharacter* Me = S->Me()) { Me->SetCrouch(false, TEXT("самотест")); } if (Pre) { Pre(); } });
		S->Do(Id, 0.15, [S]() { KeyEv(*S, EKeys::LeftControl, true); });
		S->Do(Id, 0.15, [S, Mid]() { if (Mid) { Mid(); } });
		S->Do(Id, 0.15, [S]() { KeyEv(*S, EKeys::LeftControl, false); });
		S->Do(Id, 0.25, [S, IdS, WhatS, bBugCase, Post]()
		{
			AAsdaPlayerCharacter* Me = S->Me();
			const bool bOn = Me && Me->IsCrouchOn();
			const bool bWantOn = bBugCase && !S->bGuard;   // ключ 0 — прежняя логика переключала (воспроизведение бага)
			S->Expect(Me && bOn == bWantOn, FString::Printf(TEXT("%s %s: присед %d (ждали %d%s)"), *IdS, *WhatS, bOn ? 1 : 0, bWantOn ? 1 : 0,
				bWantOn ? TEXT(" — ключ 0, баг ВОСПРОИЗВЕДЁН") : TEXT("")));
			if (Post) { Post(); }
			if (Me) { Me->SetCrouch(false, TEXT("самотест")); }
		});
	}

	static void Build(TSharedRef<FState> S)
	{
		S->Do(TEXT("подготовка"), 0.0, [S]()
		{
			AsdaCVarSave::SetForTest(TEXT("asda.Crouch.Enable"), 1);
			AsdaCVarSave::SetForTest(TEXT("asda.Crouch.HoldMode"), 1);
			AsdaCVarSave::SetForTest(TEXT("asda.Crouch.HoldReleaseMs"), 50);
			AsdaCVarSave::SetForTest(TEXT("asda.Crouch.StrayGuard"), S->bGuard ? 1 : 0);
			SetMode(UAsdaGameUserSettings::kCrouchToggle);
			if (AAsdaPlayerCharacter* Me = S->Me()) { Me->StopMove(); Me->SetAnimState(EAsdaPlayerState::Idle); Me->SetCrouch(false, TEXT("самотест")); }
		});

		// ── K: контроль (прежнее поведение сохранено) ──
		S->Do(TEXT("K1"), 0.15, [S]() { KeyEv(*S, EKeys::LeftControl, true); });
		S->Do(TEXT("K1"), 0.15, [S]() { KeyEv(*S, EKeys::LeftControl, false); });
		S->Do(TEXT("K1"), 0.25, [S]()
		{
			AAsdaPlayerCharacter* Me = S->Me();
			S->Expect(Me && Me->IsCrouchOn(), TEXT("K1 одиночный Ctrl — присед ВКЛ"));
			KeyEv(*S, EKeys::LeftControl, true);
		});
		S->Do(TEXT("K2"), 0.15, [S]() { KeyEv(*S, EKeys::LeftControl, false); });
		S->Do(TEXT("K2"), 0.25, [S]()
		{
			AAsdaPlayerCharacter* Me = S->Me();
			S->Expect(Me && !Me->IsCrouchOn(), TEXT("K2 одиночный Ctrl ещё раз — присед ВЫКЛ"));
			KeyEv(*S, EKeys::W, true);   // W зажата ДО Ctrl — ход; Ctrl должен переключить
		});
		S->Do(TEXT("K3"), 0.15, [S]() { KeyEv(*S, EKeys::LeftControl, true); });
		S->Do(TEXT("K3"), 0.15, [S]() { KeyEv(*S, EKeys::LeftControl, false); });
		S->Do(TEXT("K3"), 0.25, [S]()
		{
			AAsdaPlayerCharacter* Me = S->Me();
			S->Expect(Me && Me->IsCrouchOn(), TEXT("K3 W зажата до Ctrl, Ctrl нажат и отпущен — присед ВКЛ (ход вприсядку)"));
			KeyEv(*S, EKeys::W, false);
			if (Me) { Me->StopMove(); Me->SetCrouch(false, TEXT("самотест")); }
		});
		AddCombo(S, TEXT("K4"), TEXT("Ctrl + клавиша, зажатая на кадры (сочетание)"), false,
			nullptr, [S]() { KeyEv(*S, EKeys::Semicolon, true); }, [S]() { KeyEv(*S, EKeys::Semicolon, false); });

		// ── B: случаи бага ──
		AddCombo(S, TEXT("B1"), TEXT("Ctrl + колесо (контроллер, нажатие и отпускание в одном кадре)"), true,
			nullptr, [S]() { TapSameFrame(*S, EKeys::MouseScrollUp); });
		AddCombo(S, TEXT("B2"), TEXT("Ctrl + быстрая клавиша (в одном кадре)"), true,
			nullptr, [S]() { TapSameFrame(*S, EKeys::Semicolon); });
		AddCombo(S, TEXT("B3"), TEXT("Shift зажат до Ctrl (смена раскладки Shift+Ctrl)"), true,
			[S]() { KeyEv(*S, EKeys::LeftShift, true); }, nullptr, [S]() { KeyEv(*S, EKeys::LeftShift, false); });
		AddCombo(S, TEXT("B4"), TEXT("Ctrl + колесо через Slate (над окном)"), true,
			nullptr, []() { SlateWheel(); });
		AddCombo(S, TEXT("B5"), TEXT("Ctrl + нажатие, съеденное окном UMG (видел только Slate)"), true,
			nullptr, []() { AsdaCrouchStrayDebugSlateEvent(); });
		AddCombo(S, TEXT("B6"), TEXT("Ctrl зажат в строке чата, строка закрыта до отпускания Ctrl"), true,
			[S]() { if (UAsdaChatWindowSubsystem* Cw = S->Chat()) { Cw->OpenInput(67); } },
			[S]()
			{
				UAsdaChatWindowSubsystem* Cw = S->Chat();
				S->Expect(Cw && Cw->IsInputActive(), TEXT("B6 строка чата открыта, пока Ctrl зажат"));
				if (Cw) { Cw->CloseInput(); }
				if (FSlateApplication::IsInitialized()) { FSlateApplication::Get().SetAllUserFocusToGameViewport(); }
			});

		// ── H: «Зажатие» не изменилось ──
		S->Do(TEXT("H"), 0.15, [S]() { SetMode(UAsdaGameUserSettings::kCrouchHold); KeyEv(*S, EKeys::LeftControl, true); });
		S->Do(TEXT("H"), 0.15, [S]()
		{
			AAsdaPlayerCharacter* Me = S->Me();
			S->Expect(Me && Me->IsCrouchOn(), TEXT("H1 «Зажатие»: Ctrl зажат — присед ВКЛ"));
			TapSameFrame(*S, EKeys::MouseScrollUp);
		});
		S->Do(TEXT("H"), 0.15, [S]()
		{
			AAsdaPlayerCharacter* Me = S->Me();
			S->Expect(Me && Me->IsCrouchOn(), TEXT("H2 «Зажатие»: колесо при зажатом Ctrl — присед держится"));
			KeyEv(*S, EKeys::LeftControl, false);
		});
		S->Do(TEXT("H"), 0.3, [S]()
		{
			AAsdaPlayerCharacter* Me = S->Me();
			S->Expect(Me && !Me->IsCrouchOn(), TEXT("H3 «Зажатие»: Ctrl отпущен — встал"));
			SetMode(UAsdaGameUserSettings::kCrouchToggle);
		});
	}

	static bool Tick(TSharedRef<FState> S)
	{
		if (!S->GI.IsValid()) { AsdaCVarSave::RestoreAll(kCmd); return false; }
		if (!S->bReady)
		{
			if (!S->Me()) { return true; }   // ждём своего персонажа (FakeSelfSpawn)
			S->bReady = true; S->StepT = FPlatformTime::Seconds();
		}
		if (S->Next >= S->Steps.Num()) { Finish(S); return false; }
		FStep& St = S->Steps[S->Next];
		if (FPlatformTime::Seconds() - S->StepT < St.Delay) { return true; }
		St.Fn();
		++S->Next; S->StepT = FPlatformTime::Seconds();
		return true;
	}
}

static FAutoConsoleCommandWithWorldAndArgs GAsdaSelfTestCrouchStray(
	TEXT("asda.SelfTest.CrouchStray"),
	TEXT("[ИСПРАВЛЕНИЕ] Самотест ложного приседа в «Нажатии» (пачка 15, баг 3): Ctrl+колесо, быстрые нажатия, Shift до Ctrl, события Slate, ")
	TEXT("Ctrl из строки чата; контроль одиночного Ctrl и «Зажатия». Офлайн. [guard=0 — воспроизвести прежнее] [delay=N] [quit]"),
	FConsoleCommandWithWorldAndArgsDelegate::CreateLambda([](const TArray<FString>& Args, UWorld* W)
	{
		using namespace AsdaCrouchStrayTest;
		const FAsdaSelfTestArgs KA = FAsdaSelfTestArgs::Parse(Args);
		if (AsdaSelfTestKit::RefuseStart(W, kCmd, KA)) { return; }
		UGameInstance* GI = W ? W->GetGameInstance() : nullptr;
		if (!GI) { AsdaSelfTestKit::Refused(W, kCmd, KA.bQuit, TEXT("нет GameInstance")); return; }
		if (AsdaSelfTestKit::NeedSelfSpawn(W) && GEngine)
		{
			GEngine->Exec(W, TEXT("Asda.Load.CurrentMap 3"));
			GEngine->Exec(W, TEXT("Asda.World.FakeSelfSpawn 256 256"));
		}
		TSharedRef<FState> S = MakeShared<FState>();
		S->GI = GI;
		S->bQuit = KA.bQuit;
		S->OrigMode = UAsdaGameUserSettings::GetCrouchMode();
		for (const FString& A : Args) { if (A.Equals(TEXT("guard=0"), ESearchCase::IgnoreCase)) { S->bGuard = false; } }
		Build(S);
		S->StepT = FPlatformTime::Seconds() + KA.Delay;
		FTSTicker::GetCoreTicker().AddTicker(FTickerDelegate::CreateLambda([S](float) -> bool { return AsdaCrouchStrayTest::Tick(S); }), 0.0f);
		UE_LOG(LogAsdaCrouchStrayTest, Display, TEXT("%s запуск: шагов %d, ключ StrayGuard=%d"), kPrefix, S->Steps.Num(), S->bGuard ? 1 : 0);
	}));

#endif // ASDA_DEV_CONSOLE && !UE_BUILD_SHIPPING
