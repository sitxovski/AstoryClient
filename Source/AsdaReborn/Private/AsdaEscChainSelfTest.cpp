// Copyright (c) 2026 @sitxovski (Discord)
// ★ САМОТЕСТ «ESC СНИМАЕТ ЦЕЛЬ» (25.10.2026): `asda.SelfTest.EscChain [quit]`. Разбор эталона — `Public/UI/AsdaEscChain.h`.
//   Путь игрока: ESC — настоящие события Slate (`ProcessKeyDownEvent`/`ProcessKeyUpEvent` на разных кадрах) — препроцессоры,
//   фокус, вьюпорт, `UPlayerInput`, опрос `UAsdaSystemOptionSubsystem::PollHotkeys`, как от клавиатуры. Цели — штатный
//   `SetTarget` (тот же, что у клика); моб — пакетом 4013 через `DebugFeedPacket`, сосед — `DebugSpawnFakePeer`, NPC — актор
//   уровня. Без сети (-AsdaNoNet + перехват отправок). Мир (`--map world`). Итог: «ESC-ЦЕЛЬ ИТОГ: ok=N fail=M».
//   Разделы:
//     K. Ключ 0 (поведение до правки = жалоба игроков): цель-моб, окон нет — ESC открывает меню 69400, цель и плашка на месте.
//     A. Моб: ESC снимает цель и плашку PVP_TARGET, меню НЕ открыто; следующий ESC — меню; ещё один — меню закрыто.
//     B. Чужой игрок, C. сам игрок, D. NPC (без разговора) — ESC снимает цель, меню не открыто.
//     E0. Ключ закрытия разговора 0 (оригинал): ESC снимает выделение, разговор ОСТАЁТСЯ; второй ESC — меню поверх разговора.
//     E. [ИСПРАВЛЕНИЕ] Разговор + цель-NPC: ESC закрывает разговор (кнопка 575), второй — цель, третий — меню.
//     F. [ИСПРАВЛЕНИЕ] Разговор + магазин 1300 + цель: ESC — магазин (0x215), ESC — разговор, ESC — цель, ESC — меню.
//     S. Ветки (в): бег без заявки — меню нет, остановки нет; бег с заявкой удара — 4008 и стоп; боевой покой — меню нет;
//        моб-обидчик в мире — меню нет, бег с маской цели-моба — 4008; вход в покой стирает обидчика — меню открывается.
//     M. Меню 69400 открыто + цель: один ESC закрывает меню И снимает цель (процедура 69400 флага 0x215 не ставит).
//     Q. Модалка: TWOBUT 1520 + цель — ESC = «Отмена», цель на месте (0x215); ONEBUT 1500 + цель — окно на месте (съедено),
//        цель снята (флаг 0x215 у 1500 не ставится).
//     T. Строка чата открыта + цель: ESC снимает цель, строка остаётся; второй ESC — строка закрыта, меню не открыто.
//     T2. Строка чата + меню 69400, цели нет: один ESC закрывает меню (без флага 0x215) и строку чата.
#include "CoreMinimal.h"
#include "AsdaDevConsole.h"

#if ASDA_DEV_CONSOLE && !UE_BUILD_SHIPPING

#include "UI/AsdaEscChain.h"
#include "UI/AsdaWindowSubsystem.h"
#include "UI/AsdaChatWindowSubsystem.h"
#include "UI/AsdaSystemOptionSubsystem.h"
#include "UI/AsdaNpcShopSubsystem.h"
#include "Npc/AsdaNpcDialogSubsystem.h"
#include "Npc/AsdaNpcActor.h"
#include "Npc/AsdaMobActor.h"
#include "AsdaCVarSave.h"
#include "AsdaNetSubsystem.h"
#include "AsdaTestPackets.h"
#include "Test/AsdaSelfTestKit.h"
#include "World/AsdaWorldSubsystem.h"
#include "World/AsdaPlayerController.h"
#include "World/AsdaPlayerCharacter.h"
#include "World/AsdaCoord.h"
#include "Blueprint/UserWidget.h"
#include "Components/Button.h"
#include "Containers/Ticker.h"
#include "Engine/Engine.h"
#include "Engine/GameInstance.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "Framework/Application/SlateApplication.h"
#include "HAL/IConsoleManager.h"
#include "HAL/PlatformTime.h"
#include "InputCoreTypes.h"

DEFINE_LOG_CATEGORY_STATIC(LogAsdaEscChainTest, Log, All);

bool AsdaAutoPathExitsCombatOn();   // ключ `asda.Move.AutoPathExitsCombat` (World/AsdaPlayerController_Move.cpp) — аксессор

namespace AsdaEscChainSelfTest
{
	static const TCHAR* kCmd = TEXT("asda.SelfTest.EscChain");   // имя команды (итог, отказ, возврат ключей)
	static const FName kOneBut(TEXT("ONEBUT_MSGBOX"));           // окно-сообщение 1500
	static const FName kTwoBut(TEXT("TWOBUT_MSGBOX"));           // окно-подтверждение 1520
	static const FName kFrame(TEXT("PVP_TARGET"));               // плашка выбранной цели (окно 40)
	static const FName kShop(TEXT("NPCSHOP_WIN"));               // магазин NPC 1300
	static constexpr int32 kMobRid = 7311;                       // renderId тестового моба
	static constexpr int32 kMobType = 1;                         // тип моба (волк alpen) — как у других самотестов
	static constexpr int32 kPeer = 41;                           // userNum тестового соседа

	// Контекст прогона.
	struct FRun
	{
		TWeakObjectPtr<UWorld> W;                 // мир прогона
		bool bQuit = false;                       // выйти после итога
		int32 Ok = 0, Fail = 0;                   // счёт проверок
		FString Phase;                            // раздел (в строках проверок)
		TArray<TFunction<int32(FRun&)>> Steps;    // шаги: тело возвращает, сколько кадров ждать
		int32 Step = 0;                           // текущий шаг
		uint64 WaitUntilFrame = 0;                // ждать до кадра
		double WaitUntilTime = 0.0;               // и до времени
		uint32 CapGen = 0;                        // перехват отправок
		int32 Sent = 0;                           // перехвачено отправок
		TArray<uint16> EscOps;                    // опкоды, ушедшие между нажатием ESC и проверкой после отпускания (ждём ни одного)
		bool bInEsc = false;                      // идёт окно «нажат ESC → проверка» (отправки считаются в EscOps)
		int32 Clears0 = 0;                        // счётчик снятий цели шагом ESC на начало проверки
		TWeakObjectPtr<AAsdaNpcActor> Npc;        // NPC уровня для разделов D/E/F
		int32 ConfirmYes = 0, ConfirmNo = 0;      // ответы TWOBUT
		int32 MsgOk = 0;                          // «ОК» ONEBUT
		int32 Stops0 = 0;                         // счётчик остановок веток (в) на начало проверки
		bool bWasInGame = false;                  // `bInGameConnected` до раздела S (на время раздела — «в мире», 4008 перехватывается)
		bool Moving() const { const AAsdaPlayerController* P = PC(); return P && P->IsMovingNow(); }

		// Проверка: строка журнала и счёт.
		void Expect(bool b, const FString& What)
		{
			if (b) { ++Ok; UE_LOG(LogAsdaEscChainTest, Log, TEXT("ESC-ЦЕЛЬ ok   [%s] %s"), *Phase, *What); }
			else   { ++Fail; UE_LOG(LogAsdaEscChainTest, Error, TEXT("ESC-ЦЕЛЬ FAIL [%s] %s"), *Phase, *What); }
		}
		UGameInstance* GI() const { UWorld* X = W.Get(); return X ? X->GetGameInstance() : nullptr; }
		template<typename T> T* Sub() const { UGameInstance* G = GI(); return G ? G->GetSubsystem<T>() : nullptr; }
		UAsdaWindowSubsystem* WM() const { return Sub<UAsdaWindowSubsystem>(); }
		AAsdaPlayerController* PC() const { UWorld* X = W.Get(); return X ? Cast<AAsdaPlayerController>(X->GetFirstPlayerController()) : nullptr; }
		AActor* Target() const { const AAsdaPlayerController* P = PC(); return P ? P->TargetActor.Get() : nullptr; }
		bool Visible(FName Id) const { const UAsdaWindowSubsystem* M = WM(); return M && M->IsWindowVisible(Id); }
		bool MenuOn() const { const UAsdaSystemOptionSubsystem* S = Sub<UAsdaSystemOptionSubsystem>(); return S && S->IsMenuShown(); }
		bool ChatOn() const { const UAsdaChatWindowSubsystem* C = Sub<UAsdaChatWindowSubsystem>(); return C && C->IsInputActive(); }
		bool DlgOn() const { const UAsdaNpcDialogSubsystem* D = Sub<UAsdaNpcDialogSubsystem>(); return D && D->IsDialogOpen(); }
		bool ShopOn() const { const UAsdaNpcShopSubsystem* S = Sub<UAsdaNpcShopSubsystem>(); return S && S->IsOpen(); }
		AAsdaMobActor* Mob() const { const UAsdaWorldSubsystem* S = Sub<UAsdaWorldSubsystem>(); return S ? S->FindNetMobByRid(kMobRid) : nullptr; }
		AActor* Peer() const { const UAsdaWorldSubsystem* S = Sub<UAsdaWorldSubsystem>(); return S ? S->ActorByUserIdx(kPeer) : nullptr; }
		AActor* Self() const { const UAsdaWorldSubsystem* S = Sub<UAsdaWorldSubsystem>(); return (S && IsValid(S->LocalPlayerActor)) ? S->LocalPlayerActor.Get() : nullptr; }
		int32 Clears() const { return AsdaEscChain::GetClearCount(); }
		FString TgtName() const { const AActor* A = Target(); return A ? A->GetName() : FString(TEXT("нет")); }
	};
	static TSharedPtr<FRun> GRun;

	// Фокус клавиатуры — игровому вьюпорту (игрок щёлкнул по миру).
	static void FocusGame() { if (FSlateApplication::IsInitialized()) { FSlateApplication::Get().SetAllUserFocusToGameViewport(); } }

	// Шаг-тело (и сколько кадров ждать после него).
	static void Add(FRun& R, TFunction<void(FRun&)> Fn, int32 Frames = 3)
	{
		R.Steps.Add([Fn, Frames](FRun& X) -> int32 { Fn(X); return Frames; });
	}
	// ESC реальной клавишей: нажатие и отпускание на разных кадрах (коды VK_ESCAPE 27/27).
	static void AddEsc(FRun& R)
	{
		Add(R, [](FRun& X) { X.bInEsc = true; if (FSlateApplication::IsInitialized()) { FSlateApplication::Get().ProcessKeyDownEvent(FKeyEvent(EKeys::Escape, FModifierKeysState(), 0, false, 27, 27)); } }, 2);
		Add(R, [](FRun&) { if (FSlateApplication::IsInitialized()) { FSlateApplication::Get().ProcessKeyUpEvent(FKeyEvent(EKeys::Escape, FModifierKeysState(), 0, false, 27, 27)); } }, 4);
		Add(R, [](FRun& X) { X.bInEsc = false; }, 1);
	}
	// Уборка между разделами: окна-сообщения, строка чата, разговор, магазин, меню, цель; фокус — миру.
	static void Clean(FRun& X)
	{
		UAsdaWindowSubsystem* M = X.WM();
		for (int32 i = 0; M && i < 8 && M->GetMsgBoxQueueNum() > 0; ++i)
		{
			const FName Id = M->IsWindowVisible(kTwoBut) ? kTwoBut : kOneBut;
			UUserWidget* Root = M->FindWindow(Id);
			UButton* B = Root ? Cast<UButton>(Root->GetWidgetFromName(Id == kTwoBut ? TEXT("TWOBUT_BUT01") : TEXT("ONEBUT_BUT00"))) : nullptr;
			if (!B) { break; }
			B->OnClicked.Broadcast();
		}
		if (UAsdaChatWindowSubsystem* C = X.Sub<UAsdaChatWindowSubsystem>()) { if (C->IsInputActive()) { C->CloseInput(); } }
		if (UAsdaNpcShopSubsystem* S = X.Sub<UAsdaNpcShopSubsystem>()) { if (S->IsOpen()) { S->CloseShop(); } }
		if (UAsdaNpcDialogSubsystem* D = X.Sub<UAsdaNpcDialogSubsystem>()) { if (D->IsDialogOpen()) { D->Close(); } }
		if (UAsdaSystemOptionSubsystem* S = X.Sub<UAsdaSystemOptionSubsystem>()) { if (S->IsMenuShown()) { S->ToggleMenu(); } }
		if (AAsdaPlayerController* P = X.PC()) { P->ClearTarget(); }
		FocusGame();
	}
	// Выбрать цель штатным путём клика (`SetTarget`) и запомнить счётчик снятий.
	static void Pick(FRun& X, AActor* A)
	{
		X.Clears0 = X.Clears();
		if (AAsdaPlayerController* P = X.PC()) { P->SetTarget(A); }
		FocusGame();
	}
	// Пробежка путём игрока: приказ хода к точке в 6 клетках (тот же вход, что щелчок по карте).
	static void StartRun(FRun& X)
	{
		AAsdaPlayerController* P = X.PC();
		const AAsdaPlayerCharacter* C = Cast<AAsdaPlayerCharacter>(X.Self());
		// Офлайн ответа 4007 нет — эталонный бит 2 «ход ждёт ответа» закрыл бы следующий 4006 (как у самотеста позиции моба).
		if (UAsdaNetSubsystem* Net = X.Sub<UAsdaNetSubsystem>()) { Net->ClearMoveRequestInFlight(TEXT("самотест ESC: новая пробежка")); }
		if (P && C) { P->MoveToGamePoint(C->GetGamePosXZ() + FVector2D(600.f, 0.f)); }
		FocusGame();
	}
	// Типовая проверка «ESC снял цель, меню не открыто».
	static void ExpectCleared(FRun& X, const TCHAR* What)
	{
		X.Expect(X.Target() == nullptr && !X.MenuOn() && X.Clears() == X.Clears0 + 1,
			FString::Printf(TEXT("%s: ESC снял цель (цель «%s», снятий +%d), меню 69400 %s, плашка PVP_TARGET %s"), What, *X.TgtName(),
				X.Clears() - X.Clears0, X.MenuOn() ? TEXT("ОТКРЫТО") : TEXT("не открыто"), X.Visible(kFrame) ? TEXT("ВИДНА") : TEXT("скрыта")));
	}

	// Тикер ядра: шаги по кадрам.
	static bool OnTick(float)
	{
		if (!GRun.IsValid()) { return false; }
		FRun& R = *GRun;
		UWorld* W = R.W.Get();
		if (!W)
		{
			AsdaCVarSave::RestoreAll(kCmd);
			GRun.Reset();
			return false;
		}
		if (GFrameCounter < R.WaitUntilFrame || FPlatformTime::Seconds() < R.WaitUntilTime) { return true; }
		if (!R.Steps.IsValidIndex(R.Step))
		{
			AsdaSelfTestKit::DisarmCapture(AsdaSelfTestKit::NetOf(W), R.CapGen);
			const FString Tail = FString::Printf(TEXT(" (перехвачено отправок: %d)"), R.Sent);
			AsdaSelfTestKit::Finish(W, kCmd, R.bQuit, LogAsdaEscChainTest, TEXT("ESC-ЦЕЛЬ"), R.Ok, R.Fail, *Tail);
			GRun.Reset();
			return false;
		}
		const int32 Frames = FMath::Max(1, R.Steps[R.Step](R));
		++R.Step;
		R.WaitUntilFrame = GFrameCounter + (uint64)Frames;
		R.WaitUntilTime = FPlatformTime::Seconds() + Frames / 30.0;
		return true;
	}

	static constexpr int32 kSettle = 16;   // кадров «окно отстоялось» (≥ 0,5 с)

	static void Build(FRun& R)
	{
		// ── Подготовка: свой персонаж, моб 4013 рядом, сосед, NPC уровня ──
		Add(R, [](FRun& X)
		{
			X.Phase = TEXT("подготовка");
			Clean(X);
			// ★ 31.10.2026: предусловие разделов K/A — персонаж В ПОКОЕ. Сразу после боевого самотеста (в пакете — `Combat.SelfTestRhythm`)
			//   ещё идёт хвост боевой стойки (`CombatStanceUntil`, 10 с — как у оригинала), и ESC верно уходит в ветку «в бою»: меню не
			//   открывается, а тест ждал меню (пакет bug-skills-stop: 50/2). Заявка удара и стойка снимаются здесь тем же штатным путём.
			if (AAsdaPlayerController* P = X.PC())
			{
				const bool bWasFight = P->bInCombatStance || P->CombatStanceUntil > FPlatformTime::Seconds() || P->bCombatStarted || P->bAutoAttack;
				P->bAutoAttack = false; P->bCombatStarted = false;
				P->ExitCombatStance();
				P->CombatStanceUntil = 0.0;
				P->bInCombatStance = false;   // `ExitCombatStance` снимает позу и хвост, флаг стойки — нет
				if (bWasFight) { UE_LOG(LogAsdaEscChainTest, Log, TEXT("ESC-ЦЕЛЬ [подготовка] боевая стойка/заявка от прошлого прогона сняты — персонаж в покое")); }
			}
			UAsdaWorldSubsystem* WS = X.Sub<UAsdaWorldSubsystem>();
			const AAsdaPlayerCharacter* C = Cast<AAsdaPlayerCharacter>(X.Self());
			X.Expect(WS && C && X.PC(), TEXT("свой персонаж и контроллер на месте"));
			if (!WS || !C) { return; }
			const FIntPoint Home = AsdaCoord::GameToTile(C->GetGamePosXZ());
			if (UAsdaNetSubsystem* Net = X.Sub<UAsdaNetSubsystem>())
			{
				Net->DebugFeedPacket(4013, AsdaTestPackets::MobSpawn4013(kMobRid, kMobType, Home + FIntPoint(4, 0), /*State*/1, 100));
			}
			WS->DebugSpawnFakePeer(kPeer, TEXT("EscPeer"), AsdaCoord::TileCenterGame(Home + FIntPoint(0, 3)));
			for (TActorIterator<AAsdaNpcActor> It(X.W.Get()); It; ++It)
			{
				if (It->NpcId > 0) { X.Npc = *It; break; }
			}
		}, 10);
		Add(R, [](FRun& X)
		{
			X.Expect(X.Mob() != nullptr, FString::Printf(TEXT("моб rid %d появился (4013)"), kMobRid));
			X.Expect(X.Peer() != nullptr, FString::Printf(TEXT("сосед userNum %d появился"), kPeer));
			X.Expect(X.Npc.IsValid(), FString::Printf(TEXT("NPC уровня найден: %s (npcId %d)"),
				X.Npc.IsValid() ? *X.Npc->GetName() : TEXT("—"), X.Npc.IsValid() ? X.Npc->NpcId : 0));
		});

		// ── K. Ключ 0 — поведение до правки (жалоба игроков) ──
		Add(R, [](FRun& X)
		{
			X.Phase = TEXT("K ключ 0 (до правки)");
			Clean(X);
			AsdaCVarSave::SetForTest(TEXT("asda.UI.EscClearTarget"), 0);
			Pick(X, X.Mob());
		}, 6);
		Add(R, [](FRun& X) { X.Expect(X.Target() == X.Mob() && X.Mob() && X.Visible(kFrame), TEXT("цель-моб выбрана, плашка PVP_TARGET видна")); });
		AddEsc(R);
		Add(R, [](FRun& X)
		{
			X.Expect(X.Target() == X.Mob() && X.MenuOn() && X.Visible(kFrame),
				FString::Printf(TEXT("ключ 0: ESC открыл меню 69400 (%s), цель и плашка остались (%s) — так было у игроков"),
					X.MenuOn() ? TEXT("да") : TEXT("НЕТ"), *X.TgtName()));
			AsdaCVarSave::SetForTest(TEXT("asda.UI.EscClearTarget"), 1);
			Clean(X);
		});

		// ── A. Моб: ESC → цель снята, меню не открыто; ESC → меню; ESC → меню закрыто ──
		Add(R, [](FRun& X) { X.Phase = TEXT("A моб"); Pick(X, X.Mob()); }, 6);
		Add(R, [](FRun& X) { X.Expect(X.Target() == X.Mob() && X.Mob() && X.Visible(kFrame), TEXT("цель-моб выбрана, плашка видна")); });
		AddEsc(R);
		Add(R, [](FRun& X) { ExpectCleared(X, TEXT("моб")); }, 4);
		Add(R, [](FRun& X) { X.Expect(!X.Visible(kFrame), TEXT("плашка PVP_TARGET скрыта после отрисовки")); });
		AddEsc(R);
		Add(R, [](FRun& X) { X.Expect(X.MenuOn(), TEXT("второй ESC (цели нет): меню 69400 открыто")); });
		AddEsc(R);
		Add(R, [](FRun& X) { X.Expect(!X.MenuOn(), TEXT("третий ESC: меню 69400 закрыто (окно меню — верхнее с признаком ESC)")); Clean(X); });

		// ── B. Чужой игрок ──
		Add(R, [](FRun& X) { X.Phase = TEXT("B игрок"); Pick(X, X.Peer()); }, 6);
		Add(R, [](FRun& X) { X.Expect(X.Target() == X.Peer() && X.Peer(), TEXT("цель-игрок выбрана")); });
		AddEsc(R);
		Add(R, [](FRun& X) { ExpectCleared(X, TEXT("игрок")); Clean(X); });

		// ── C. Сам игрок ──
		Add(R, [](FRun& X) { X.Phase = TEXT("C сам игрок"); Pick(X, X.Self()); }, 6);
		Add(R, [](FRun& X) { X.Expect(X.Target() == X.Self() && X.Self(), TEXT("цель — свой персонаж")); });
		AddEsc(R);
		Add(R, [](FRun& X) { ExpectCleared(X, TEXT("сам игрок")); Clean(X); });

		// ── D. NPC без разговора ──
		Add(R, [](FRun& X) { X.Phase = TEXT("D NPC"); Pick(X, X.Npc.Get()); }, 6);
		Add(R, [](FRun& X) { X.Expect(X.Npc.IsValid() && X.Target() == X.Npc.Get(), TEXT("цель-NPC выбрана")); });
		AddEsc(R);
		Add(R, [](FRun& X) { ExpectCleared(X, TEXT("NPC")); Clean(X); });

		// ── E0. Ключ закрытия разговора 0 (поведение оригинала): разговор остаётся, ESC снимает выделение NPC ──
		Add(R, [](FRun& X)
		{
			X.Phase = TEXT("E0 разговор, ключ 0 (оригинал)");
			AsdaCVarSave::SetForTest(TEXT("asda.UI.EscCloseNpcDialog"), 0);
			Pick(X, X.Npc.Get());
			UAsdaNpcDialogSubsystem* D = X.Sub<UAsdaNpcDialogSubsystem>();
			X.Expect(D && X.Npc.IsValid() && D->OpenForNpc(X.Npc.Get()), TEXT("разговор с NPC открыт"));
			FocusGame();
		}, kSettle);
		AddEsc(R);
		Add(R, [](FRun& X)
		{
			ExpectCleared(X, TEXT("ключ 0: первый ESC"));
			X.Expect(X.DlgOn(), FString::Printf(TEXT("ключ 0: разговор %s (эталон: у окна 570 признака ESC нет)"), X.DlgOn() ? TEXT("остался") : TEXT("ЗАКРЫТ")));
		});
		AddEsc(R);
		Add(R, [](FRun& X)
		{
			X.Expect(X.MenuOn() && X.DlgOn(), FString::Printf(TEXT("ключ 0: второй ESC — меню 69400 %s, разговор %s"),
				X.MenuOn() ? TEXT("открыто") : TEXT("НЕ открыто"), X.DlgOn() ? TEXT("на месте") : TEXT("ЗАКРЫТ")));
			AsdaCVarSave::SetForTest(TEXT("asda.UI.EscCloseNpcDialog"), 1);
			Clean(X);
		});

		// ── E. [ИСПРАВЛЕНИЕ] Разговор NPC + цель-NPC: ESC — разговор, ESC — цель, ESC — меню ──
		Add(R, [](FRun& X)
		{
			X.Phase = TEXT("E разговор NPC");
			Pick(X, X.Npc.Get());
			UAsdaNpcDialogSubsystem* D = X.Sub<UAsdaNpcDialogSubsystem>();
			X.Expect(D && X.Npc.IsValid() && D->OpenForNpc(X.Npc.Get()), TEXT("разговор с NPC открыт, цель — этот NPC"));
			FocusGame();
		}, kSettle);
		AddEsc(R);
		Add(R, [](FRun& X)
		{
			X.Expect(!X.DlgOn() && X.Target() == X.Npc.Get() && !X.MenuOn(),
				FString::Printf(TEXT("первый ESC: разговор %s (кнопка 575), цель «%s», меню %s"), X.DlgOn() ? TEXT("НЕ закрыт") : TEXT("закрыт"),
					*X.TgtName(), X.MenuOn() ? TEXT("ОТКРЫТО") : TEXT("не открыто")));
			X.Clears0 = X.Clears();
		});
		AddEsc(R);
		Add(R, [](FRun& X) { ExpectCleared(X, TEXT("второй ESC")); });
		AddEsc(R);
		Add(R, [](FRun& X) { X.Expect(X.MenuOn(), TEXT("третий ESC: меню 69400 открыто")); Clean(X); });

		// ── F. [ИСПРАВЛЕНИЕ] Разговор + магазин NPC 1300 + цель: ESC×3 — магазин, разговор, цель; четвёртый — меню ──
		Add(R, [](FRun& X)
		{
			X.Phase = TEXT("F разговор + магазин");
			Pick(X, X.Npc.Get());
			UAsdaNpcDialogSubsystem* D = X.Sub<UAsdaNpcDialogSubsystem>();
			if (D && X.Npc.IsValid()) { D->OpenForNpc(X.Npc.Get()); }
			if (UAsdaNpcShopSubsystem* S = X.Sub<UAsdaNpcShopSubsystem>()) { if (X.Npc.IsValid()) { S->OpenShop(5, X.Npc->NpcId); } }
			FocusGame();
		}, kSettle);
		Add(R, [](FRun& X)
		{
			X.Expect(X.DlgOn() && X.ShopOn() && X.Visible(kShop) && X.Target() == X.Npc.Get(), TEXT("разговор, магазин 1300 и цель-NPC на месте"));
			X.Clears0 = X.Clears();
		});
		AddEsc(R);
		Add(R, [](FRun& X)
		{
			X.Expect(!X.ShopOn() && X.Target() == X.Npc.Get() && X.DlgOn() && !X.MenuOn(),
				FString::Printf(TEXT("ESC 1: магазин %s, цель «%s», разговор %s (окно 1300 ставит CWindowMgr+0x215 — ESC потрачен целиком)"),
					X.ShopOn() ? TEXT("НЕ закрыт") : TEXT("закрыт"), *X.TgtName(), X.DlgOn() ? TEXT("на месте") : TEXT("ЗАКРЫТ")));
		});
		AddEsc(R);
		Add(R, [](FRun& X)
		{
			X.Expect(!X.DlgOn() && X.Target() == X.Npc.Get() && !X.MenuOn(),
				FString::Printf(TEXT("ESC 2: разговор %s, цель «%s»"), X.DlgOn() ? TEXT("НЕ закрыт") : TEXT("закрыт"), *X.TgtName()));
			X.Clears0 = X.Clears();
		});
		AddEsc(R);
		Add(R, [](FRun& X) { ExpectCleared(X, TEXT("ESC 3")); });
		AddEsc(R);
		Add(R, [](FRun& X) { X.Expect(X.MenuOn(), TEXT("ESC 4: меню 69400 открыто")); Clean(X); });

		// ── S. Ветки (в): состояние своего персонажа (бег, заявка удара, боевой покой, обидчик-моб) ──
		Add(R, [](FRun& X)
		{
			X.Phase = TEXT("S бег без заявки");
			Clean(X);
			if (UAsdaNetSubsystem* Net = X.Sub<UAsdaNetSubsystem>()) { X.bWasInGame = Net->bInGameConnected; Net->bInGameConnected = true; }
			X.Stops0 = AsdaEscChain::GetStateStopCount();
			StartRun(X);
		}, 6);
		Add(R, [](FRun& X) { X.Expect(X.Moving(), FString::Printf(TEXT("персонаж бежит (%s)"), X.Moving() ? TEXT("да") : TEXT("НЕТ"))); });
		AddEsc(R);
		Add(R, [](FRun& X)
		{
			X.Expect(!X.MenuOn() && AsdaEscChain::GetStateStopCount() == X.Stops0,
				FString::Printf(TEXT("бег без заявки: меню %s, остановок %d (эталон: «не в бою», состояние 1 → только выход)"),
					X.MenuOn() ? TEXT("ОТКРЫТО") : TEXT("не открыто"), AsdaEscChain::GetStateStopCount() - X.Stops0));
		});
		Add(R, [](FRun& X)
		{
			X.Phase = TEXT("S бег с заявкой удара");
			if (AAsdaPlayerController* P = X.PC()) { P->bCombatStarted = true; }   // заявка удара ДО приказа хода
			X.Stops0 = AsdaEscChain::GetStateStopCount();
			X.EscOps.Reset();
			StartRun(X);
		}, 2);
		// ★ 31.10.2026: приказ хода `MoveToGamePoint` с 26.10 (navattack, ключ `asda.Move.AutoPathExitsCombat`) выводит из атаки
		//   (`AsdaExitCombatForAutoPath` — как клик по земле, эталон case 0x10 entity_char.c:24085), поэтому заявка, поставленная
		//   ДО пробежки, снята. Состояние «бег + [1].field118 = 1» в игре — заявка удара, взведённая уже на бегу (подход к цели):
		//   её и ставим, ветка ESC (game_main.c:9342) проверяется прежней сверкой ниже.
		Add(R, [](FRun& X)
		{
			AAsdaPlayerController* P = X.PC();
			const bool bOn = AsdaAutoPathExitsCombatOn();
			X.Expect(P && X.Moving() && P->bCombatStarted == !bOn,
				FString::Printf(TEXT("приказ хода при заявке удара: персонаж %s, заявка %s (ключ AutoPathExitsCombat=%d — %s)"),
					X.Moving() ? TEXT("бежит") : TEXT("СТОИТ"), (P && P->bCombatStarted) ? TEXT("осталась") : TEXT("снята"), bOn ? 1 : 0,
					bOn ? TEXT("ждём «снята»") : TEXT("ждём «осталась»")));
			if (P) { P->bCombatStarted = true; }   // [1].field118 = 1 (4026 ушла) — уже на бегу
		}, 4);
		AddEsc(R);
		Add(R, [](FRun& X)
		{
			const bool b4008 = X.EscOps.Contains(4008);
			X.Expect(AsdaEscChain::GetStateStopCount() == X.Stops0 + 1 && !X.Moving() && b4008 && !X.MenuOn(),
				FString::Printf(TEXT("бег с заявкой: остановок +%d, 4008 %s, персонаж %s, меню %s (эталон game_main.c:9342)"),
					AsdaEscChain::GetStateStopCount() - X.Stops0, b4008 ? TEXT("ушёл") : TEXT("НЕ ушёл"), X.Moving() ? TEXT("БЕЖИТ") : TEXT("стоит"),
					X.MenuOn() ? TEXT("ОТКРЫТО") : TEXT("не открыто")));
			if (AAsdaPlayerController* P = X.PC()) { P->bCombatStarted = false; }
			X.EscOps.Reset();
		}, 6);
		Add(R, [](FRun& X)
		{
			X.Phase = TEXT("S боевой покой");
			if (AAsdaPlayerController* P = X.PC()) { P->CombatStanceUntil = FPlatformTime::Seconds() + 5.0; }   // подрежим 20
		}, 4);
		AddEsc(R);
		Add(R, [](FRun& X)
		{
			X.Expect(!X.MenuOn(), FString::Printf(TEXT("боевой покой (подрежим 20): меню %s"), X.MenuOn() ? TEXT("ОТКРЫТО") : TEXT("не открыто")));
			if (AAsdaPlayerController* P = X.PC()) { P->CombatStanceUntil = 0.0; }
		}, 4);
		Add(R, [](FRun& X)
		{
			X.Phase = TEXT("S обидчик-моб");
			AsdaEscChain::NoteSelfHitByMob(kMobRid, 5);   // кадр удара моба по мне (порт `Combat_ApplyDamage`)
			X.Expect(AsdaEscChain::GetSelfAttackerRid() == kMobRid, TEXT("урон 5 от моба записан (netWord54)"));
			AsdaEscChain::NoteSelfHitByMob(-1, 5);
			AsdaEscChain::NoteSelfHitByMob(kMobRid + 1, -1);   // уклонение — поле не трогается
			X.Expect(AsdaEscChain::GetSelfAttackerRid() == kMobRid, TEXT("уклонение (−1) обидчика не меняет"));
		}, 4);
		AddEsc(R);
		Add(R, [](FRun& X)
		{
			X.Expect(!X.MenuOn(), FString::Printf(TEXT("покой, моб-обидчик в мире: меню %s («в бою»)"), X.MenuOn() ? TEXT("ОТКРЫТО") : TEXT("не открыто")));
			if (AAsdaPlayerCharacter* C = Cast<AAsdaPlayerCharacter>(X.Self())) { C->TargetModeMask = 1; }   // маска цели-моба (бит 0)
			X.Stops0 = AsdaEscChain::GetStateStopCount();
			X.EscOps.Reset();
			StartRun(X);
		}, 6);
		AddEsc(R);
		Add(R, [](FRun& X)
		{
			const bool b4008 = X.EscOps.Contains(4008);
			X.Expect(AsdaEscChain::GetStateStopCount() == X.Stops0 + 1 && b4008 && !X.MenuOn(),
				FString::Printf(TEXT("бег в бою с маской цели-моба: остановок +%d, 4008 %s, меню %s (эталон game_main.c:9324)"),
					AsdaEscChain::GetStateStopCount() - X.Stops0, b4008 ? TEXT("ушёл") : TEXT("НЕ ушёл"), X.MenuOn() ? TEXT("ОТКРЫТО") : TEXT("не открыто")));
			if (AAsdaPlayerCharacter* C = Cast<AAsdaPlayerCharacter>(X.Self())) { C->TargetModeMask = 0; }
			X.EscOps.Reset();
		}, 6);
		Add(R, [](FRun& X)
		{
			X.Expect(AsdaEscChain::GetSelfAttackerRid() < 0, FString::Printf(TEXT("вход в покой стёр обидчика (netWord54 = %d)"), AsdaEscChain::GetSelfAttackerRid()));
		});
		AddEsc(R);
		Add(R, [](FRun& X)
		{
			X.Expect(X.MenuOn(), TEXT("покой без обидчика и стойки: ESC открыл меню 69400"));
			if (UAsdaNetSubsystem* Net = X.Sub<UAsdaNetSubsystem>()) { Net->bInGameConnected = X.bWasInGame; }
			Clean(X);
		});

		// ── M. Меню 69400 + цель: один ESC — и меню, и цель ──
		Add(R, [](FRun& X)
		{
			X.Phase = TEXT("M меню + цель");
			Pick(X, X.Mob());
			if (UAsdaSystemOptionSubsystem* S = X.Sub<UAsdaSystemOptionSubsystem>()) { if (!S->IsMenuShown()) { S->ToggleMenu(); } }
			FocusGame();
		}, kSettle);
		Add(R, [](FRun& X) { X.Expect(X.MenuOn() && X.Target() == X.Mob(), TEXT("меню 69400 открыто, цель-моб выбрана")); });
		AddEsc(R);
		Add(R, [](FRun& X)
		{
			X.Expect(!X.MenuOn() && X.Target() == nullptr && X.Clears() == X.Clears0 + 1,
				FString::Printf(TEXT("один ESC: меню %s и цель «%s» (процедура 69400 флага 0x215 не ставит — нажатие идёт дальше к цели)"),
					X.MenuOn() ? TEXT("НЕ закрыто") : TEXT("закрыто"), *X.TgtName()));
			Clean(X);
		});

		// ── Q. Модалки ──
		Add(R, [](FRun& X)
		{
			X.Phase = TEXT("Q модалка TWOBUT");
			Pick(X, X.Mob());
			X.ConfirmYes = X.ConfirmNo = 0;
			TWeakPtr<FRun> Wk = GRun;
			if (UAsdaWindowSubsystem* M = X.WM())
			{
				M->ShowGameConfirm(TEXT("Проверка ESC: цель"), [Wk](bool b) { if (TSharedPtr<FRun> P = Wk.Pin()) { ++(b ? P->ConfirmYes : P->ConfirmNo); } },
					0.f, 2288, 2289, AsdaMsgBox::kNoKey, 1);
			}
		}, kSettle);
		AddEsc(R);
		Add(R, [](FRun& X)
		{
			X.Expect(X.ConfirmNo == 1 && !X.Visible(kTwoBut) && X.Target() == X.Mob(),
				FString::Printf(TEXT("TWOBUT: ESC = «Отмена» (нет %d), окно %s, цель «%s» осталась (1520 ставит 0x215)"),
					X.ConfirmNo, X.Visible(kTwoBut) ? TEXT("НЕ закрыто") : TEXT("закрыто"), *X.TgtName()));
			X.Phase = TEXT("Q модалка ONEBUT");
			X.MsgOk = 0;
			X.Clears0 = X.Clears();
			TWeakPtr<FRun> Wk = GRun;
			if (UAsdaWindowSubsystem* M = X.WM())
			{
				M->ShowGameMessage(TEXT("Проверка ESC: сообщение"), 2288, EAsdaMsgCategory::ItemRefused,
					[Wk]() { if (TSharedPtr<FRun> P = Wk.Pin()) { ++P->MsgOk; } }, 0, AsdaMsgBox::kNoKey);
			}
		}, kSettle);
		AddEsc(R);
		Add(R, [](FRun& X)
		{
			X.Expect(X.Visible(kOneBut) && X.MsgOk == 0 && X.Target() == nullptr && X.Clears() == X.Clears0 + 1,
				FString::Printf(TEXT("ONEBUT: окно %s (съедено, ОК %d), цель «%s» (1500 флага 0x215 не ставит — эталон снимает цель)"),
					X.Visible(kOneBut) ? TEXT("на месте") : TEXT("ЗАКРЫТО"), X.MsgOk, *X.TgtName()));
			Clean(X);
		});

		// ── T. Строка чата + цель ──
		Add(R, [](FRun& X)
		{
			X.Phase = TEXT("T строка чата");
			Pick(X, X.Mob());
		}, 4);
		// Строка чата — путём игрока: Enter (окон нет — `uiChatInputToggle` эталона), фокус уходит в поле строки.
		Add(R, [](FRun&) { if (FSlateApplication::IsInitialized()) { FSlateApplication::Get().ProcessKeyDownEvent(FKeyEvent(EKeys::Enter, FModifierKeysState(), 0, false, 13, 13));
			FSlateApplication::Get().ProcessKeyCharEvent(FCharacterEvent(TEXT('\r'), FModifierKeysState(), 0, false)); } }, 2);
		Add(R, [](FRun&) { if (FSlateApplication::IsInitialized()) { FSlateApplication::Get().ProcessKeyUpEvent(FKeyEvent(EKeys::Enter, FModifierKeysState(), 0, false, 13, 13)); } }, kSettle);
		Add(R, [](FRun& X)
		{
			const TSharedPtr<SWidget> F = FSlateApplication::IsInitialized() ? FSlateApplication::Get().GetKeyboardFocusedWidget() : nullptr;
			X.Expect(X.ChatOn() && X.Target() == X.Mob(), FString::Printf(TEXT("строка чата открыта (фокус «%s»), цель-моб выбрана"),
				F.IsValid() ? *F->GetTypeAsString() : TEXT("нет")));
		});
		AddEsc(R);
		Add(R, [](FRun& X)
		{
			ExpectCleared(X, TEXT("строка чата: первый ESC"));
			X.Expect(X.ChatOn(), FString::Printf(TEXT("строка чата %s (эталон: шаг цели раньше закрытия строки)"), X.ChatOn() ? TEXT("осталась") : TEXT("ЗАКРЫТА")));
		});
		AddEsc(R);
		Add(R, [](FRun& X)
		{
			X.Expect(!X.ChatOn() && !X.MenuOn(), FString::Printf(TEXT("второй ESC: строка чата %s, меню %s"),
				X.ChatOn() ? TEXT("НЕ закрыта") : TEXT("закрыта"), X.MenuOn() ? TEXT("ОТКРЫТО") : TEXT("не открыто")));
			Clean(X);
		});

		// ── T2. Строка чата + меню 69400 (окно без флага 0x215), цели нет: один ESC закрывает и меню, и строку ──
		Add(R, [](FRun& X)
		{
			X.Phase = TEXT("T2 строка чата + меню");
			if (UAsdaSystemOptionSubsystem* S = X.Sub<UAsdaSystemOptionSubsystem>()) { if (!S->IsMenuShown()) { S->ToggleMenu(); } }
			FocusGame();
		}, kSettle);
		Add(R, [](FRun&) { if (FSlateApplication::IsInitialized()) { FSlateApplication::Get().ProcessKeyDownEvent(FKeyEvent(EKeys::Enter, FModifierKeysState(), 0, false, 13, 13));
			FSlateApplication::Get().ProcessKeyCharEvent(FCharacterEvent(TEXT('\r'), FModifierKeysState(), 0, false)); } }, 2);
		Add(R, [](FRun&) { if (FSlateApplication::IsInitialized()) { FSlateApplication::Get().ProcessKeyUpEvent(FKeyEvent(EKeys::Enter, FModifierKeysState(), 0, false, 13, 13)); } }, kSettle);
		Add(R, [](FRun& X) { X.Expect(X.ChatOn() && X.MenuOn() && X.Target() == nullptr, TEXT("меню 69400 открыто, строка чата открыта, цели нет")); });
		AddEsc(R);
		Add(R, [](FRun& X)
		{
			X.Expect(!X.MenuOn() && !X.ChatOn(), FString::Printf(TEXT("один ESC: меню %s, строка чата %s (эталон: окно без 0x215 → цели нет → строка чата)"),
				X.MenuOn() ? TEXT("НЕ закрыто") : TEXT("закрыто"), X.ChatOn() ? TEXT("НЕ закрыта") : TEXT("закрыта")));
			Clean(X);
		});

		// ── Уборка: моб и сосед ──
		Add(R, [](FRun& X)
		{
			X.Phase = TEXT("уборка");
			Clean(X);
			if (AAsdaMobActor* M = X.Mob()) { M->Destroy(); }
			FString Ops;
			int32 Other = 0;
			for (const uint16 Op : X.EscOps) { Ops += FString::Printf(TEXT(" %u"), Op); Other += (Op != 5045); }
			X.Expect(Other == 0, FString::Printf(TEXT("ESC в сеть ничего не шлёт: за нажатиями ESC перехвачено %d (опкоды:%s); всего за прогон %d ")
				TEXT("(5045 — «первая встреча» при закрытии разговора, как у кнопки 575; выбор цели-игрока 6538 и уборка — вне ESC)"), X.EscOps.Num(), Ops.IsEmpty() ? TEXT(" нет") : *Ops, X.Sent));
		});
	}

	static void Run(const TArray<FString>& A, UWorld* W)
	{
		const FAsdaSelfTestArgs Args = FAsdaSelfTestArgs::Parse(A);
		if (AsdaSelfTestKit::RefuseStart(W, kCmd, Args)) { return; }
		if (!W || !W->GetGameInstance()) { AsdaSelfTestKit::Refused(W, kCmd, Args.bQuit, TEXT("нет мира")); return; }
		if (GRun.IsValid()) { AsdaSelfTestKit::Refused(W, kCmd, Args.bQuit, TEXT("самотест уже идёт")); return; }
		AsdaCVarSave::ArmWorldGuard(W, kCmd);
		AsdaCVarSave::SetForTest(TEXT("asda.UI.EscClearTarget"), 1);
		AsdaCVarSave::SetForTest(TEXT("asda.UI.EscFromTextField"), 1);
		AsdaCVarSave::SetForTest(TEXT("asda.UI.EscCloseNpcDialog"), 1);
		AsdaCVarSave::SetForTest(TEXT("asda.UI.EscStateBranches"), 1);
		GRun = MakeShared<FRun>();
		FRun& R = *GRun;
		R.W = W;
		R.bQuit = Args.bQuit;
		TWeakPtr<FRun> Weak = GRun;
		R.CapGen = AsdaSelfTestKit::ArmCapture(AsdaSelfTestKit::NetOf(W),
			[Weak](uint16 Op, const uint8*, int32, bool, TCHAR) { if (TSharedPtr<FRun> P = Weak.Pin()) { ++P->Sent; if (P->bInEsc) { P->EscOps.Add(Op); } } }, 0, kCmd);
		// Свой персонаж офлайн — HUD, плашка цели и окна мира строятся только в мире с персонажем.
		if (AsdaSelfTestKit::NeedSelfSpawn(W) && GEngine)
		{
			if (UAsdaWorldSubsystem* WS = W->GetGameInstance()->GetSubsystem<UAsdaWorldSubsystem>())
			{
				GEngine->Exec(W, TEXT("Asda.Load.CurrentMap 3"));
				WS->DebugSpawnSelfAt(256, 256, TEXT("СамТест"));
			}
		}
		Build(R);
		R.WaitUntilTime = FPlatformTime::Seconds() + 2.5;   // первый шаг — после построения HUD и окон
		FTSTicker::GetCoreTicker().AddTicker(FTickerDelegate::CreateStatic(&OnTick), 0.f);
	}
}

static FAutoConsoleCommandWithWorldAndArgs GAsdaSelfTestEscChain(
	TEXT("asda.SelfTest.EscChain"),
	TEXT("Самотест: ESC снимает выбранную цель (моб, игрок, сам игрок, NPC) по цепочке эталона GameInput_DispatchEvent case 2: окно с флагом "
	     "0x215, разговор NPC, магазин NPC, меню 69400, модалки, строка чата, ключ отката. Мир, только офлайн. `quit`."),
	FConsoleCommandWithWorldAndArgsDelegate::CreateLambda([](const TArray<FString>& A, UWorld* W)
	{
		AsdaEscChainSelfTest::Run(A, W);
	}));


#endif // ASDA_DEV_CONSOLE && !UE_BUILD_SHIPPING
