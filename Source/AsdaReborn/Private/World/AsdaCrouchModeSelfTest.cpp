// Copyright (c) 2026 @sitxovski (Discord)
// ★★★ САМОТЕСТ РЕЖИМА CTRL ХОДЬБЫ ВПРИСЯДКУ «НАЖАТИЕ / ЗАЖАТИЕ» (25.10.2026, запрос владельца). [ПОРТ]/[ИСПРАВЛЕНИЕ]
//
// ЧТО ПРОВЕРЯЕТ (настоящие входы: ввод контроллера `InputKey`, тик персонажа, `PollKeys`, `SendMove4006` под перехватом):
//   N. «Нажатие» (поле профиля `CrouchMode` = 0) — как с 24.10.2026: нажатие Ctrl само не переключает, отпускание — переключает.
//   Z. «Зажатие» (= 1), прямые вызовы `TickCrouchInput` в одном кадре: Ctrl вниз — присед сразу; сочетание его не снимает;
//      отпускание — не сразу (дребезг); запрет (ключ asda.Crouch.Enable 0) повторяется каждый кадр — снят, и присед встаёт.
//   R. «Зажатие», настоящий ввод: Ctrl вниз — присед без отпускания; вверх — встал после `asda.Crouch.HoldReleaseMs` (не раньше);
//      Ctrl+W (клавиша хода) — ход, строка чата НЕ открылась; ключ asda.Crouch.HoldNoMoveCombo 0 — Ctrl+W открывает шёпот
//      (прежнее); набор в чате — присед замирает, после закрытия строки идёт за Ctrl.
//   S. Сеть на ходу «в точку»: дребезг Ctrl короче окна дребезга — ни одного лишнего 4006; дребезг без окна (0 мс) при быстром
//      сервере — не чаще 4 пакетов в секунду, без повторов статуса подряд, последний отправленный = итоговый; ключ
//      asda.Crouch.NetCoalesce 0 — прежнее (пакетов больше).
//   I. Невидимость (подмена подсистемы): ход невидимого — присед в обоих режимах, без Ctrl и с ним.
//   M. Смена карты (`OpenLevel`) с зажатым Ctrl: «Зажатие» — новый персонаж стоит, пока новый контроллер не увидел Ctrl, затем
//      присед/встал как обычно; «Нажатие» — отпускание Ctrl, нажатого до портала, ничего не переключает, дальше — как прежде.
// Консоль: `asda.SelfTest.CrouchMode [delay=N] [quit]`. Только офлайн. Строки — «САМОТЕСТ-ПРИСЕД-РЕЖИМ», итог — «… ИТОГ: ok=… fail=…».
#include "CoreMinimal.h"
#include "AsdaDevConsole.h"

#if ASDA_DEV_CONSOLE && !UE_BUILD_SHIPPING

#include "AsdaBytes.h"
#include "AsdaNetSubsystem.h"
#include "AsdaCVarSave.h"
#include "Test/AsdaSelfTestKit.h"
#include "Resources/AsdaGameUserSettings.h"
#include "World/AsdaPlayerCharacter.h"
#include "World/AsdaStealthSubsystem.h"
#include "UI/AsdaChatWindowSubsystem.h"
#include "Engine/Engine.h"
#include "Engine/GameInstance.h"
#include "Engine/World.h"
#include "HAL/IConsoleManager.h"
#include "InputCoreTypes.h"
#include "GameFramework/PlayerController.h"
#include "Kismet/GameplayStatics.h"   // OpenLevel — настоящая смена карты
#include "Containers/Ticker.h"        // шаги переживают смену мира (таймеры мира гибнут)
#include "Framework/Application/SlateApplication.h"   // фокус клавиатуры после закрытия строки чата

DEFINE_LOG_CATEGORY_STATIC(LogAsdaCrouchModeTest, Log, All);

namespace AsdaCrouchModeTest
{
	static const TCHAR* const kCmd = TEXT("asda.SelfTest.CrouchMode");
	static const TCHAR* const kPrefix = TEXT("САМОТЕСТ-ПРИСЕД-РЕЖИМ");
	static constexpr int32 kToggle = UAsdaGameUserSettings::kCrouchToggle;   // «Нажатие»
	static constexpr int32 kHold = UAsdaGameUserSettings::kCrouchHold;       // «Зажатие»

	// Один шаг прогона: ждать `Delay` с от начала шага, затем звать `Fn` каждый тик, пока не вернёт true (не дольше `Timeout`).
	struct FStep
	{
		FString Name;                       // имя шага (журнал, таймаут)
		double Delay = 0.0;                 // пауза до первого вызова, с
		double Timeout = 5.0;               // потолок ожидания `Fn`, с
		TFunction<bool()> Fn;               // тело; true — шаг сделан
	};

	// Состояние прогона (живёт между тиками и через смену мира).
	struct FState
	{
		TWeakObjectPtr<UGameInstance> GI;   // экземпляр игры (переживает OpenLevel)
		bool bQuit = false;                 // `quit` в конце
		int32 Ok = 0, Fail = 0;             // счёт
		uint32 CapGen = 0;                  // поколение перехвата отправок
		TArray<int32> Status;               // статус @+3 перехваченных 4006 по порядку
		bool bWasInGame = false;            // `bInGameConnected` до прогона (вернуть)
		int32 OrigMode = 0;                 // режим профиля до прогона (вернуть)
		UPTRINT OldWorld = 0;               // адрес мира до портала (дождаться нового)
		int32 Mark = 0;                     // отметка числа 4006 (начало замера)
		int32 Count = 0;                    // счётчик шагов дребезга
		int32 NewRun = 0, OldRun = 0;       // пакетов за дребезг: с ключом NetCoalesce 1 / 0
		TArray<FStep> Steps;                // шаги
		int32 Next = 0;                     // текущий шаг
		double StepT = 0.0;                 // реальное время входа в текущий шаг

		UWorld* W() const { return GI.IsValid() ? GI->GetWorld() : nullptr; }
		APlayerController* PC() const { UWorld* Wd = W(); return Wd ? Wd->GetFirstPlayerController() : nullptr; }
		AAsdaPlayerCharacter* Me() const { APlayerController* P = PC(); return P ? Cast<AAsdaPlayerCharacter>(P->GetPawn()) : nullptr; }
		UAsdaNetSubsystem* Net() const { return GI.IsValid() ? GI->GetSubsystem<UAsdaNetSubsystem>() : nullptr; }
		UAsdaStealthSubsystem* Stealth() const { return GI.IsValid() ? GI->GetSubsystem<UAsdaStealthSubsystem>() : nullptr; }
		UAsdaChatWindowSubsystem* Chat() const { return GI.IsValid() ? GI->GetSubsystem<UAsdaChatWindowSubsystem>() : nullptr; }

		void Expect(bool b, const FString& What)
		{
			if (b) { ++Ok; UE_LOG(LogAsdaCrouchModeTest, Display, TEXT("%s ok   %s"), kPrefix, *What); }
			else   { ++Fail; UE_LOG(LogAsdaCrouchModeTest, Error, TEXT("%s FAIL %s"), kPrefix, *What); }
		}
		// Шаг: пауза Delay, затем Fn (true — сделано).
		void Add(const TCHAR* Name, double Delay, TFunction<bool()> Fn, double Timeout = 5.0)
		{
			FStep St; St.Name = Name; St.Delay = Delay; St.Timeout = Timeout; St.Fn = MoveTemp(Fn);
			Steps.Add(MoveTemp(St));
		}
		// Шаг из одного действия (всегда «сделано»).
		void Do(const TCHAR* Name, double Delay, TFunction<void()> Fn)
		{
			Add(Name, Delay, [F = MoveTemp(Fn)]() { F(); return true; });
		}
	};

	// Нажать/отпустить клавишу штатным вводом текущего контроллера.
	static void KeyEv(const FState& S, const FKey& K, bool bDown)
	{
		if (APlayerController* P = S.PC()) { P->InputKey(FInputKeyEventArgs::CreateSimulated(K, bDown ? IE_Pressed : IE_Released, bDown ? 1.f : 0.f)); }
	}

	// Режим профиля (в памяти; как после «ОК» окна — персонаж читает поле каждый кадр).
	static void SetMode(int32 M) { if (UAsdaGameUserSettings* P = UAsdaGameUserSettings::Get()) { P->CrouchMode = M; } }

	// Играет ли клип anim_<Idx>.
	static bool ClipIs(const AAsdaPlayerCharacter* C, int32 Idx)
	{
		return C && C->GetPlayingClipName().EndsWith(FString::Printf(TEXT("anim_%d"), Idx));
	}

	// Подряд одинаковых статусов в перехваченных 4006 начиная с `From` (повтор = лишний пакет).
	static int32 DupRuns(const FState& S, int32 From)
	{
		int32 N = 0;
		for (int32 i = FMath::Max(From + 1, 1); i < S.Status.Num(); ++i) { if (S.Status[i] == S.Status[i - 1] && i - 1 >= From) { ++N; } }
		return N;
	}

	// Пуск хода «в точку» далеко вперёд + «клик»-4006 со статусом без приседа (как настоящий клик), ответ сервера — сразу.
	static void StartPointMove(FState& S)
	{
		AAsdaPlayerCharacter* Me = S.Me(); UAsdaNetSubsystem* Net = S.Net();
		if (!Me || !Net) { return; }
		const FVector2D From = Me->GetGamePosXZ();
		Me->NetMove(From, From + FVector2D(4000.f, 0.f), 0.35f);
		Net->SendMove4006((float)From.X, (float)From.Y, (float)From.X + 4000.f, (float)From.Y);
		Net->ClearMoveRequestInFlight(TEXT("самотест присед — ответ сервера"));
	}

	static void Finish(TSharedRef<FState> S)
	{
		UWorld* W = S->W();
		if (AAsdaPlayerCharacter* Me = S->Me()) { Me->StopMove(); Me->SetCrouch(false, TEXT("итог самотеста")); }
		if (UAsdaStealthSubsystem* St = S->Stealth()) { if (S->Me()) { St->DebugForceInvisible(S->Me(), -1); } }
		if (UAsdaChatWindowSubsystem* Cw = S->Chat()) { if (Cw->IsInputActive()) { Cw->CloseInput(); } }
		if (UAsdaNetSubsystem* Net = S->Net())
		{
			if (S->CapGen) { AsdaSelfTestKit::DisarmCapture(Net, S->CapGen); S->CapGen = 0; }
			Net->bInGameConnected = S->bWasInGame;
			Net->ClearMoveRequestInFlight(TEXT("итог самотеста присед"));
		}
		SetMode(S->OrigMode);
		AsdaSelfTestKit::Finish(W, kCmd, S->bQuit, LogAsdaCrouchModeTest, kPrefix, S->Ok, S->Fail,
			*FString::Printf(TEXT(" (4006: %d)"), S->Status.Num()));
	}

	// Взвести перехват 4006 (после портала — заново, если сняли).
	static bool ArmCap(TSharedRef<FState> S)
	{
		UAsdaNetSubsystem* Net = S->Net();
		if (!Net) { return false; }
		if (S->CapGen && AsdaSelfTestKit::IsCaptureBusy(Net)) { return true; }
		TWeakPtr<FState> WS = S;
		S->CapGen = AsdaSelfTestKit::ArmCapture(Net, [WS](uint16 Op, const uint8* P, int32 Len, bool, TCHAR)
		{
			if (TSharedPtr<FState> SP = WS.Pin()) { if (Op == 4006 && P && Len >= 43) { SP->Status.Add((int32)AsdaBytes::RdU16(P + 16 + 3)); } }
		}, 0, kCmd);
		return S->CapGen != 0;
	}

	// Шаги «портал»: запомнить мир, OpenLevel; дождаться нового мира и своего персонажа.
	static void AddPortal(TSharedRef<FState> S, const TCHAR* Tag)
	{
		S->Do(Tag, 0.1, [S]()
		{
			UWorld* W = S->W();
			S->OldWorld = (UPTRINT)W;
			UE_LOG(LogAsdaCrouchModeTest, Display, TEXT("%s портал: OpenLevel alpen (Ctrl зажат)"), kPrefix);
			if (W) { UGameplayStatics::OpenLevel(W, FName(TEXT("alpen")), true, TEXT("game=/Script/AsdaReborn.AsdaWorldGameMode")); }
		});
		S->Add(TEXT("новый мир"), 0.5, [S]()
		{
			UWorld* W = S->W();
			if (!W || (UPTRINT)W == S->OldWorld || !W->HasBegunPlay()) { return false; }
			if (GEngine)
			{
				GEngine->Exec(W, TEXT("Asda.Load.CurrentMap 3"));
				GEngine->Exec(W, TEXT("Asda.World.FakeSelfSpawn 256 256"));
			}
			return true;
		}, 120.0);
		S->Add(TEXT("свой персонаж после портала"), 1.0, [S]()
		{
			AAsdaPlayerCharacter* Me = S->Me();
			if (!Me) { return false; }
			if (UAsdaNetSubsystem* Net = S->Net()) { Net->bInGameConnected = true; }   // сброс сессии мог снять — прогон офлайн под перехватом
			S->Expect(ArmCap(S), TEXT("M0 перехват отправок жив после портала"));
			Me->SetAnimState(EAsdaPlayerState::Idle);
			return true;
		}, 30.0);
	}

	static void Build(TSharedRef<FState> S)
	{
		// ── Подготовка ──
		S->Add(TEXT("подготовка"), 0.0, [S]()
		{
			AAsdaPlayerCharacter* Me = S->Me(); UAsdaNetSubsystem* Net = S->Net();
			if (!Me || !Net || !S->Stealth() || !S->Chat()) { return false; }
			if (!ArmCap(S)) { S->Expect(false, TEXT("перехват отправок занят")); S->Next = S->Steps.Num(); return true; }
			for (const TCHAR* K : { TEXT("asda.Crouch.Enable"), TEXT("asda.Crouch.HideAnim"), TEXT("asda.Crouch.NetStatus"), TEXT("asda.Crouch.HoldMode"),
			                        TEXT("asda.Crouch.NetCoalesce"), TEXT("asda.Crouch.HoldNoMoveCombo"), TEXT("asda.Stealth.Visual") })
			{
				AsdaCVarSave::SetForTest(K, 1);
			}
			AsdaCVarSave::SetForTest(TEXT("asda.Crouch.HoldReleaseMs"), 50);
			S->bWasInGame = Net->bInGameConnected;
			Net->bInGameConnected = true;
			Me->StopMove(); Me->SetAnimState(EAsdaPlayerState::Idle); Me->SetCrouch(false, TEXT("самотест"));
			return true;
		}, 30.0);

		// ── N: «Нажатие» — как прежде ──
		S->Do(TEXT("N"), 0.1, [S]()
		{
			AAsdaPlayerCharacter* Me = S->Me(); if (!Me) { return; }
			SetMode(kToggle);
			Me->TickCrouchInput(true, false, true);
			S->Expect(!Me->IsCrouchOn(), TEXT("N1 «Нажатие»: Ctrl вниз — присед не включён (переключает отпускание)"));
			Me->TickCrouchInput(false, false, true);
			S->Expect(Me->IsCrouchOn() && Me->GetMoveStatusFor4006() == AAsdaPlayerCharacter::kCrouchMoveStatus,
				FString::Printf(TEXT("N2 «Нажатие»: отпускание — присед ВКЛ, статус 4006 = %d (ждём 4)"), Me->GetMoveStatusFor4006()));
			Me->TickCrouchInput(true, false, true); Me->TickCrouchInput(false, false, true);
			S->Expect(!Me->IsCrouchOn(), TEXT("N3 «Нажатие»: ещё нажатие — ВЫКЛ"));
			KeyEv(*S, EKeys::LeftControl, true);
		});
		S->Do(TEXT("N4"), 0.2, [S]()
		{
			AAsdaPlayerCharacter* Me = S->Me(); if (!Me) { return; }
			S->Expect(!Me->IsCrouchOn(), TEXT("N4 «Нажатие», настоящий ввод: Ctrl зажат 0.2 с — присед не включён"));
			KeyEv(*S, EKeys::LeftControl, false);
		});
		S->Do(TEXT("N5"), 0.2, [S]()
		{
			AAsdaPlayerCharacter* Me = S->Me(); if (!Me) { return; }
			S->Expect(Me->IsCrouchOn(), TEXT("N5 «Нажатие», настоящий ввод: Ctrl отпущен — присед ВКЛ"));
			KeyEv(*S, EKeys::LeftControl, true);
		});
		S->Do(TEXT("N6"), 0.15, [S]() { KeyEv(*S, EKeys::LeftControl, false); });
		S->Do(TEXT("N7"), 0.2, [S]()
		{
			AAsdaPlayerCharacter* Me = S->Me(); if (!Me) { return; }
			S->Expect(!Me->IsCrouchOn(), TEXT("N6 «Нажатие», настоящий ввод: второе нажатие — ВЫКЛ"));
		});

		// ── Z: «Зажатие», прямые вызовы в одном кадре ──
		S->Do(TEXT("Z"), 0.1, [S]()
		{
			AAsdaPlayerCharacter* Me = S->Me(); if (!Me) { return; }
			SetMode(kHold);
			Me->TickCrouchInput(true, false, true);
			S->Expect(Me->IsCrouchOn() && Me->GetMoveStatusFor4006() == AAsdaPlayerCharacter::kCrouchMoveStatus,
				FString::Printf(TEXT("Z1 «Зажатие»: Ctrl вниз — присед сразу, статус 4006 = %d (ждём 4)"), Me->GetMoveStatusFor4006()));
			Me->TickCrouchInput(true, true, true);
			S->Expect(Me->IsCrouchOn(), TEXT("Z2 «Зажатие»: Ctrl + новая клавиша (сочетание) — присед держится"));
			Me->TickCrouchInput(false, false, true);
			S->Expect(Me->IsCrouchOn(), TEXT("Z3 «Зажатие»: отпускание — не сразу (окно дребезга 50 мс)"));
			Me->SetCrouch(false, TEXT("самотест Z"));
			AsdaCVarSave::SetForTest(TEXT("asda.Crouch.Enable"), 0);
			Me->TickCrouchInput(true, false, true);
			const bool bRefused = !Me->IsCrouchOn() && !Me->GetLastCrouchRefusal().IsEmpty();
			AsdaCVarSave::SetForTest(TEXT("asda.Crouch.Enable"), 1);
			Me->TickCrouchInput(true, false, true);
			S->Expect(bRefused && Me->IsCrouchOn(), TEXT("Z4 «Зажатие»: запрет (ключ Enable 0) — отказ; запрет снят при зажатом Ctrl — присед в следующем кадре"));
			Me->SetCrouch(false, TEXT("самотест Z"));
		});

		// ── R: «Зажатие», настоящий ввод ──
		S->Do(TEXT("R1"), 0.15, [S]() { AsdaCVarSave::SetForTest(TEXT("asda.Crouch.HoldReleaseMs"), 300); KeyEv(*S, EKeys::LeftControl, true); });
		S->Do(TEXT("R2"), 0.2, [S]()
		{
			AAsdaPlayerCharacter* Me = S->Me(); if (!Me) { return; }
			S->Expect(Me->IsCrouchOn(), TEXT("R1 «Зажатие», настоящий ввод: Ctrl зажат — присед ВКЛ без отпускания"));
			KeyEv(*S, EKeys::LeftControl, false);
		});
		S->Do(TEXT("R3"), 0.12, [S]()
		{
			AAsdaPlayerCharacter* Me = S->Me(); if (!Me) { return; }
			S->Expect(Me->IsCrouchOn(), TEXT("R2 окно дребезга 300 мс: через 0.12 с после отпускания ещё вприсядку"));
		});
		S->Do(TEXT("R4"), 0.4, [S]()
		{
			AAsdaPlayerCharacter* Me = S->Me(); if (!Me) { return; }
			S->Expect(!Me->IsCrouchOn(), TEXT("R3 через 0.5 с после отпускания — встал"));
			AsdaCVarSave::SetForTest(TEXT("asda.Crouch.HoldReleaseMs"), 50);
			KeyEv(*S, EKeys::LeftControl, true);
		});
		// Ctrl+W: клавиша хода при зажатом Ctrl — ход, строки чата нет.
		S->Do(TEXT("R5"), 0.15, [S]() { KeyEv(*S, EKeys::W, true); });
		S->Do(TEXT("R6"), 0.3, [S]()
		{
			AAsdaPlayerCharacter* Me = S->Me(); if (!Me) { return; }
			S->Expect(Me->IsCrouchOn(), FString::Printf(TEXT("R4 Ctrl+W зажаты: присед %d (ждём 1), идёт %d"), Me->IsCrouchOn() ? 1 : 0, Me->IsMoving() ? 1 : 0));
			KeyEv(*S, EKeys::W, false);
		});
		S->Do(TEXT("R7"), 0.2, [S]()
		{
			AAsdaPlayerCharacter* Me = S->Me(); UAsdaChatWindowSubsystem* Cw = S->Chat(); if (!Me || !Cw) { return; }
			S->Expect(!Cw->IsInputActive() && Me->IsCrouchOn(), FString::Printf(TEXT("R5 W отпущена при зажатом Ctrl: строка чата %s, присед %d (ждём «нет», 1)"),
				Cw->IsInputActive() ? TEXT("ОТКРЫТА") : TEXT("нет"), Me->IsCrouchOn() ? 1 : 0));
			KeyEv(*S, EKeys::LeftControl, false);
		});
		S->Do(TEXT("R8"), 0.25, [S]()
		{
			AAsdaPlayerCharacter* Me = S->Me(); if (!Me) { return; }
			S->Expect(!Me->IsCrouchOn(), TEXT("R6 Ctrl отпущен — встал"));
			Me->StopMove();
			AsdaCVarSave::SetForTest(TEXT("asda.Crouch.HoldNoMoveCombo"), 0);
			KeyEv(*S, EKeys::LeftControl, true);
		});
		S->Do(TEXT("R9"), 0.15, [S]() { KeyEv(*S, EKeys::W, true); });
		S->Do(TEXT("R10"), 0.2, [S]() { KeyEv(*S, EKeys::W, false); });
		S->Do(TEXT("R11"), 0.2, [S]()
		{
			AAsdaPlayerCharacter* Me = S->Me(); UAsdaChatWindowSubsystem* Cw = S->Chat(); if (!Me || !Cw) { return; }
			S->Expect(Cw->IsInputActive(), TEXT("R7 ключ HoldNoMoveCombo 0: Ctrl+W открыл строку чата (шёпот) — прежнее поведение"));
			// Набор в чате: Ctrl отпускаем, пока строка открыта — присед замирает.
			KeyEv(*S, EKeys::LeftControl, false);
			AsdaCVarSave::SetForTest(TEXT("asda.Crouch.HoldNoMoveCombo"), 1);
		});
		S->Do(TEXT("R12"), 0.3, [S]()
		{
			AAsdaPlayerCharacter* Me = S->Me(); UAsdaChatWindowSubsystem* Cw = S->Chat(); if (!Me || !Cw) { return; }
			S->Expect(Me->IsCrouchOn() && Cw->IsInputActive(), FString::Printf(TEXT("R8 строка чата открыта, Ctrl отпущен 0.3 с — присед замер (%d, ждём 1)"),
				Me->IsCrouchOn() ? 1 : 0));
			Cw->CloseInput();
		});
		S->Do(TEXT("R13"), 0.3, [S]()
		{
			AAsdaPlayerCharacter* Me = S->Me(); if (!Me) { return; }
			const TSharedPtr<SWidget> Fw = FSlateApplication::IsInitialized() ? FSlateApplication::Get().GetKeyboardFocusedWidget() : nullptr;
			UE_LOG(LogAsdaCrouchModeTest, Display, TEXT("%s после закрытия строки: фокус клавиатуры «%s», игровой ввод разрешён %d"), kPrefix,
				Fw.IsValid() ? *Fw->GetTypeAsString() : TEXT("нет"), Me->IsJumpInputAllowed() ? 1 : 0);
			S->Expect(!Me->IsCrouchOn(), TEXT("R9 строка закрыта, Ctrl не зажат — встал"));
			Me->StopMove();
			if (FSlateApplication::IsInitialized()) { FSlateApplication::Get().SetAllUserFocusToGameViewport(); }   // дальше — с фокусом у мира
		});

		// ── S: сеть на ходу «в точку» ──
		S->Do(TEXT("S0"), 0.3, [S]() { StartPointMove(*S); });
		// S1: дребезг 30 мс (короче окна) — присед не моргает, лишних пакетов нет. Окно на замер — 150 мс: кадр офлайн-прогона
		//   бывает 30–60 мс, а событие отпускания доходит до опроса только на кадре контроллера.
		S->Do(TEXT("S1"), 0.3, [S]()
		{
			AAsdaPlayerCharacter* Me = S->Me();
			S->Expect(Me && Me->IsMovingToPoint() && !Me->IsCrouchOn(), TEXT("S0 идёт «в точку» стоя, «клик»-4006 отвечен"));
			AsdaCVarSave::SetForTest(TEXT("asda.Crouch.HoldReleaseMs"), 150);
			S->Mark = S->Status.Num(); S->Count = 0;
			KeyEv(*S, EKeys::LeftControl, true);
		});
		S->Add(TEXT("S1 дребезг"), 0.03, [S]()
		{
			if (UAsdaNetSubsystem* Net = S->Net()) { Net->ClearMoveRequestInFlight(TEXT("самотест — быстрый сервер")); }
			++S->Count;
			KeyEv(*S, EKeys::LeftControl, (S->Count % 2) == 0);   // 1 — вверх, 2 — вниз, …
			S->StepT = FPlatformTime::Seconds();                  // следующий вызов — через 30 мс
			return S->Count >= 24;
		}, 10.0);
		S->Do(TEXT("S1 итог"), 0.03, [S]()
		{
			AAsdaPlayerCharacter* Me = S->Me();
			const int32 N = S->Status.Num() - S->Mark;
			S->Expect(Me && Me->IsCrouchOn() && N == 1 && S->Status.Last() == AAsdaPlayerCharacter::kCrouchMoveStatus,
				FString::Printf(TEXT("S1 дребезг Ctrl 30 мс × 24 (окно 150 мс): присед держится %d, 4006 за дребезг %d (ждём 1 — включение), последний статус %d"),
					Me && Me->IsCrouchOn() ? 1 : 0, N, S->Status.Num() ? S->Status.Last() : -1));
			KeyEv(*S, EKeys::LeftControl, false);
		});
		// S2/S3: дребезг без окна (0 мс), быстрый сервер — с ключом NetCoalesce 1 и 0.
		for (const int32 Coal : { 1, 0 })
		{
			S->Do(TEXT("S2 подготовка"), 0.4, [S, Coal]()
			{
				AsdaCVarSave::SetForTest(TEXT("asda.Crouch.HoldReleaseMs"), 0);
				AsdaCVarSave::SetForTest(TEXT("asda.Crouch.NetCoalesce"), Coal);
				if (UAsdaNetSubsystem* Net = S->Net()) { Net->ClearMoveRequestInFlight(TEXT("самотест — ответ")); }
				S->Mark = S->Status.Num(); S->Count = 0;
			});
			S->Add(TEXT("S2 дребезг"), 0.03, [S]()
			{
				if (UAsdaNetSubsystem* Net = S->Net()) { Net->ClearMoveRequestInFlight(TEXT("самотест — быстрый сервер")); }
				++S->Count;
				KeyEv(*S, EKeys::LeftControl, (S->Count % 2) == 1);   // 1 — вниз, 2 — вверх, …
				S->StepT = FPlatformTime::Seconds();
				return S->Count >= 30;
			}, 10.0);
			S->Do(TEXT("S2 отпустить"), 0.03, [S]() { KeyEv(*S, EKeys::LeftControl, false); });
			S->Do(TEXT("S2 итог"), 0.6, [S, Coal]()
			{
				AAsdaPlayerCharacter* Me = S->Me(); UAsdaNetSubsystem* Net = S->Net();
				if (Net) { Net->ClearMoveRequestInFlight(TEXT("самотест — ответ")); }
				const int32 N = S->Status.Num() - S->Mark;
				const int32 Dups = DupRuns(*S, S->Mark);
				if (Coal == 1)
				{
					S->NewRun = N;
					S->Expect(Me && !Me->IsCrouchOn() && N >= 1 && N <= 6 && Dups == 0,
						FString::Printf(TEXT("S2 дребезг 0 мс × 30 по 30 мс (≈0.9 с), NetCoalesce 1: 4006 %d (ждём ≤ 6 — не чаще 4/с), повторов статуса подряд %d (ждём 0), стоит %d"),
							N, Dups, Me && !Me->IsCrouchOn() ? 1 : 0));
				}
				else
				{
					S->OldRun = N;
					S->Expect(N >= S->NewRun, FString::Printf(TEXT("S3 тот же дребезг, NetCoalesce 0 (прежнее): 4006 %d ≥ %d — сравнение"), N, S->NewRun));
				}
			});
			S->Add(TEXT("S2 досылка итога"), 0.0, [S, Coal]()
			{
				// Итог досылки: последний отправленный статус = текущий (досылка дошла после паузы 250 мс).
				AAsdaPlayerCharacter* Me = S->Me();
				if (UAsdaNetSubsystem* Net = S->Net()) { Net->ClearMoveRequestInFlight(TEXT("самотест — ответ")); }
				if (Coal == 0) { return true; }
				const bool bSame = Me && S->Status.Num() && S->Status.Last() == Me->GetMoveStatusFor4006();
				if (!bSame && FPlatformTime::Seconds() - S->StepT < 1.5) { return false; }
				S->Expect(bSame, FString::Printf(TEXT("S4 после дребезга последний 4006 со статусом %d = текущий %d (сервер знает итог)"),
					S->Status.Num() ? S->Status.Last() : -1, Me ? Me->GetMoveStatusFor4006() : -1));
				return true;
			}, 3.0);
		}
		S->Do(TEXT("S конец"), 0.1, [S]()
		{
			AsdaCVarSave::SetForTest(TEXT("asda.Crouch.HoldReleaseMs"), 50);
			AsdaCVarSave::SetForTest(TEXT("asda.Crouch.NetCoalesce"), 1);
			if (AAsdaPlayerCharacter* Me = S->Me()) { Me->StopMove(); Me->SetCrouch(false, TEXT("самотест S")); }
		});

		// ── I: невидимость — присед в обоих режимах ──
		for (const int32 Mode : { kHold, kToggle })
		{
			S->Do(TEXT("I1"), 0.2, [S, Mode]()
			{
				AAsdaPlayerCharacter* Me = S->Me(); UAsdaStealthSubsystem* St = S->Stealth(); if (!Me || !St) { return; }
				SetMode(Mode);
				St->DebugForceInvisible(Me, 1);
				const FVector2D From = Me->GetGamePosXZ();
				Me->NetMove(From, From + FVector2D(0.f, 2000.f), 0.35f);
			});
			S->Do(TEXT("I2"), 0.2, [S, Mode]()
			{
				AAsdaPlayerCharacter* Me = S->Me(); if (!Me) { return; }
				S->Expect(Me->IsSkillStealth() && !Me->IsCrouchOn() && ClipIs(Me, AAsdaPlayerCharacter::kClipCrouchWalk),
					FString::Printf(TEXT("I1 %s: невидимый идёт без Ctrl — клип «%s» (ждём anim_239), свой присед %d"), Mode == kHold ? TEXT("«Зажатие»") : TEXT("«Нажатие»"),
						*Me->GetPlayingClipName(), Me->IsCrouchOn() ? 1 : 0));
				KeyEv(*S, EKeys::LeftControl, true);
			});
			S->Do(TEXT("I3"), 0.2, [S]() { KeyEv(*S, EKeys::LeftControl, false); });
			S->Do(TEXT("I4"), 0.3, [S, Mode]()
			{
				AAsdaPlayerCharacter* Me = S->Me(); UAsdaStealthSubsystem* St = S->Stealth(); if (!Me || !St) { return; }
				S->Expect(Me->IsSkillStealth() && ClipIs(Me, AAsdaPlayerCharacter::kClipCrouchWalk),
					FString::Printf(TEXT("I2 %s: после нажатия/отпускания Ctrl невидимый — клип «%s» (ждём anim_239), свой присед %d"),
						Mode == kHold ? TEXT("«Зажатие»") : TEXT("«Нажатие»"), *Me->GetPlayingClipName(), Me->IsCrouchOn() ? 1 : 0));
				Me->SetCrouch(false, TEXT("самотест I"));
				St->DebugForceInvisible(Me, -1);
				Me->StopMove();
			});
		}

		// ── M: смена карты с зажатым Ctrl — «Зажатие» ──
		S->Do(TEXT("M1"), 0.2, [S]() { SetMode(kHold); KeyEv(*S, EKeys::LeftControl, true); });
		S->Do(TEXT("M2"), 0.2, [S]()
		{
			AAsdaPlayerCharacter* Me = S->Me();
			S->Expect(Me && Me->IsCrouchOn(), TEXT("M1 «Зажатие»: Ctrl зажат перед порталом — вприсядку"));
		});
		AddPortal(S, TEXT("M портал 1"));
		S->Do(TEXT("M3"), 0.5, [S]()
		{
			AAsdaPlayerCharacter* Me = S->Me(); APlayerController* PC = S->PC();
			const bool bCtrlNew = PC && PC->IsInputKeyDown(EKeys::LeftControl);
			S->Expect(Me && !Me->IsCrouchOn() && !bCtrlNew, FString::Printf(TEXT("M2 «Зажатие» после портала: новый контроллер Ctrl не видит (%d) — стоит (%d), не застрял"),
				bCtrlNew ? 1 : 0, Me && Me->IsCrouchOn() ? 1 : 0));
			KeyEv(*S, EKeys::LeftControl, true);   // нажатие на новой карте
		});
		S->Do(TEXT("M4"), 0.2, [S]()
		{
			AAsdaPlayerCharacter* Me = S->Me();
			S->Expect(Me && Me->IsCrouchOn(), TEXT("M3 «Зажатие» на новой карте: Ctrl зажат — вприсядку"));
			KeyEv(*S, EKeys::LeftControl, false);
		});
		S->Do(TEXT("M5"), 0.3, [S]()
		{
			AAsdaPlayerCharacter* Me = S->Me();
			S->Expect(Me && !Me->IsCrouchOn(), TEXT("M4 «Зажатие» на новой карте: Ctrl отпущен — встал"));
			// «Нажатие»: Ctrl зажат до портала, отпущен после.
			SetMode(kToggle);
			KeyEv(*S, EKeys::LeftControl, true);
		});
		AddPortal(S, TEXT("M портал 2"));
		S->Do(TEXT("M6"), 0.5, [S]() { KeyEv(*S, EKeys::LeftControl, false); });
		S->Do(TEXT("M7"), 0.3, [S]()
		{
			AAsdaPlayerCharacter* Me = S->Me();
			S->Expect(Me && !Me->IsCrouchOn(), TEXT("M5 «Нажатие»: Ctrl нажат до портала, отпущен после — ничего не переключилось"));
			KeyEv(*S, EKeys::LeftControl, true);
		});
		S->Do(TEXT("M8"), 0.15, [S]() { KeyEv(*S, EKeys::LeftControl, false); });
		S->Do(TEXT("M9"), 0.3, [S]()
		{
			AAsdaPlayerCharacter* Me = S->Me();
			S->Expect(Me && Me->IsCrouchOn(), TEXT("M6 «Нажатие» на новой карте: нажатие и отпускание Ctrl — присед ВКЛ"));
			if (Me) { Me->SetCrouch(false, TEXT("самотест M")); }
		});
	}

	static bool Tick(TSharedRef<FState> S)
	{
		if (!S->GI.IsValid()) { AsdaCVarSave::RestoreAll(kCmd); return false; }
		if (S->Next >= S->Steps.Num()) { Finish(S); return false; }
		const double Now = FPlatformTime::Seconds();
		FStep& St = S->Steps[S->Next];
		const double In = Now - S->StepT;
		if (In < St.Delay) { return true; }
		const int32 Was = S->Next;
		const bool bDone = St.Fn();
		if (S->Next != Was) { return true; }   // шаг сам перескочил (отказ) — дальше по Next
		if (bDone) { ++S->Next; S->StepT = FPlatformTime::Seconds(); return true; }
		if (In - St.Delay > St.Timeout)
		{
			S->Expect(false, FString::Printf(TEXT("шаг «%s» не дождался за %.0f с"), *St.Name, St.Timeout));
			Finish(S);
			return false;
		}
		return true;
	}
}

static FAutoConsoleCommandWithWorldAndArgs GAsdaSelfTestCrouchMode(
	TEXT("asda.SelfTest.CrouchMode"),
	TEXT("[ПОРТ] Самотест режима Ctrl приседа «Нажатие/Зажатие» (пункт «Присед» окна настроек): ввод контроллера, дребезг, Ctrl+W, ")
	TEXT("набор в чате, 4006 без флуда (NetCoalesce), невидимость, смена карты OpenLevel с зажатым Ctrl. Офлайн, на карте мира. [delay=N] [quit]"),
	FConsoleCommandWithWorldAndArgsDelegate::CreateLambda([](const TArray<FString>& Args, UWorld* W)
	{
		using namespace AsdaCrouchModeTest;
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
		Build(S);
		S->StepT = FPlatformTime::Seconds() + KA.Delay;
		FTSTicker::GetCoreTicker().AddTicker(FTickerDelegate::CreateLambda([S](float) -> bool { return Tick(S); }), 0.0f);
		UE_LOG(LogAsdaCrouchModeTest, Display, TEXT("%s запуск: шагов %d, режим профиля %d"), kPrefix, S->Steps.Num(), S->OrigMode);
	}));

#endif // ASDA_DEV_CONSOLE && !UE_BUILD_SHIPPING
