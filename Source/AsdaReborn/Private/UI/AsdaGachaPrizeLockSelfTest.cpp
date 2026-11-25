// Copyright (c) 2026 @sitxovski (Discord)
// ★★★ САМОТЕСТ «СЦЕНУ ГАЧИ НЕЛЬЗЯ ЗАКРЫТЬ, ПОКА ПРИЗ НЕ ЗАБРАН» (07.11.2026): `asda.SelfTest.GachaPrizeLock [delay=N] [quit] [shot]`.
//   Офлайн (-AsdaNoNet). Баг владельца: закрыли меню гачи после розыгрыша, не вскрыв шар, — при новом открытии шара нет.
//   Решение владельца: закрытие запрещено до забора приза, модальное окно на обоих языках (ключ `asda.Gacha.PrizeCloseLock`).
//   Путь игрока: настоящие кнопки 68076/68209/«Играть»/68108, ESC-обработчик сцены, кнопка меню (`ToggleScene`), окно ONEBUT_MSGBOX
//   и его кнопка ОК; 5467/6578 — штатным `DebugFeedPacket` → `HandlePacket`; 6577 — перехватом отправок.
//   R0  ключ 0: воспроизведение бага — сцена закрывается при шаре, новое открытие без шара
//   Автоматы 1…5, каждый:
//   L1  ожидание 6578: кнопка 68076 → сцена открыта, модальное окно 991202 на языке сессии; ОК закрывает окно
//   L2  прокрутка: крестик 68209, ESC, кнопка меню → сцена открыта
//   L3  шар: кнопка 68076 → отказ; вынужденное закрытие (`CloseScene`, смерть) → новое открытие: тот же автомат снова выдаёт шар, приз тот же
//   L4  шар → вскрытие → окно приза: кнопка 68076 → отказ; «Нажми» → покой, кнопка 68076 закрывает сцену; новое открытие — шара нет
//   M1  смена карты при шаре: шар возвращается при открытии
//   M2  разрыв связи при шаре: при открытии шара нет (приз ждёт в элитном ящике на сервере)
//   B1  бой при шаре сцену не закрывает; после забора приза закрывает (нотис 6813)
//   T1  сервер молчит 15 с: сцену можно закрыть; поздний 6578 → при открытии автомат выдаёт шар
//   Итог: «ГАЧА-ПРИЗ ИТОГ: ok=N fail=M».
#include "CoreMinimal.h"
#include "AsdaDevConsole.h"

#if ASDA_DEV_CONSOLE && !UE_BUILD_SHIPPING

#include "AsdaNetSubsystem.h"
#include "AsdaSessionReset.h"
#include "Test/AsdaSelfTestKit.h"
#include "Text/AsdaLang.h"
#include "UI/AsdaNotice.h"
#include "UI/AsdaPremiumGachaSubsystem.h"
#include "UI/AsdaWindowSubsystem.h"
#include "World/AsdaPlayerController.h"
#include "Blueprint/UserWidget.h"
#include "Components/Button.h"
#include "Engine/Engine.h"
#include "Engine/GameInstance.h"
#include "Engine/World.h"
#include "HAL/IConsoleManager.h"
#include "HighResScreenshot.h"   // кадр `shot` (FScreenshotRequest)
#include "Misc/Paths.h"          // путь кадра в Saved/Screenshots
#include "TimerManager.h"

DEFINE_LOG_CATEGORY_STATIC(LogAsdaGachaLockTest, Log, All);   // канал строк самотеста

namespace AsdaGachaLockTest
{
	static const TCHAR* kCmd      = TEXT("asda.SelfTest.GachaPrizeLock");   // имя команды
	static const TCHAR* kPrefix   = TEXT("ГАЧА-ПРИЗ");                      // префикс строк и итога
	static const TCHAR* kSelfName = TEXT("ИгрокГачи");                      // имя своего персонажа офлайн-спавна
	static const TCHAR* kLockKey  = TEXT("asda.Gacha.PrizeCloseLock");      // проверяемый ключ (R0)
	static const TCHAR* kBox      = TEXT("ONEBUT_MSGBOX");                  // окно-сообщение
	static constexpr int32 kOpBalance = 5467;     // S→C премиум-баланс (5 б)
	static constexpr int32 kOpRoll    = 6577;     // C→S прокрутка (5 б)
	static constexpr int32 kOpResult  = 6578;     // S→C результат прокрутки (63 б)
	static constexpr int32 kPrize     = 68408;    // код приза в ответе
	static constexpr int32 kLocLock   = 991202;   // текст окна запрета
	static constexpr float kToBall    = 4.8f;     // клип 2 (2.000 с) + клип 3 (2.333 с) с запасом
	static constexpr float kToDispense = 2.7f;    // клип 3 или клип 4 (2.333 с) с запасом

	// Контекст прогона.
	struct FCtx
	{
		int32 Ok = 0, Fail = 0;                       // пройдено / провалено
		TWeakObjectPtr<UWorld> W;                     // мир прогона
		bool bQuit = false;                           // выйти после итога
		bool bShot = false;                           // снять кадр окна запрета
		bool bShotDone = false;                       // кадр уже снят
		uint32 CapGen = 0;                            // поколение перехвата отправок
		int32 Sent = 0;                               // сколько 6577 ушло
		int32 Slot = 0;                               // автомат текущего круга
		TUniquePtr<FAsdaNetSelfScope> Self;           // «свой персонаж» сети — вернуть в конце
		TUniquePtr<FAsdaCVarScope> Keys;              // ключи самотеста — вернуть в конце

		void Expect(bool b, const FString& What)
		{
			if (b) { ++Ok;   UE_LOG(LogAsdaGachaLockTest, Log,   TEXT("%s ok   %s"), kPrefix, *What); }
			else   { ++Fail; UE_LOG(LogAsdaGachaLockTest, Error, TEXT("%s FAIL %s"), kPrefix, *What); }
		}
		UGameInstance* GI() const { return W.IsValid() ? W->GetGameInstance() : nullptr; }
		template <class T> T* Sub() const { UGameInstance* G = GI(); return G ? G->GetSubsystem<T>() : nullptr; }
		UAsdaPremiumGachaSubsystem* G() const { return Sub<UAsdaPremiumGachaSubsystem>(); }
	};
	using FCtxRef = TSharedRef<FCtx>;
	using FStep = void (*)(const FCtxRef&);

	// Запустить шаг через `Sec` секунд.
	static void After(const FCtxRef& C, float Sec, FStep Fn)
	{
		if (!C->W.IsValid()) { return; }
		FTimerHandle H;
		C->W->GetTimerManager().SetTimer(H, FTimerDelegate::CreateLambda([C, Fn]() { Fn(C); }), Sec, false);
	}

	// Подать 6578 (63 б, раскладка `sub_4F7D10`): @0 статус 1, @1 код приза, @59 баланс.
	static void FeedPrize(const FCtxRef& C)
	{
		TArray<uint8> B; B.SetNumZeroed(63);
		B[0] = 1;
		const int32 Code = kPrize, Bal = 999999;
		FMemory::Memcpy(&B[1], &Code, 4);
		FMemory::Memcpy(&B[59], &Bal, 4);
		if (UAsdaNetSubsystem* Net = C->Sub<UAsdaNetSubsystem>()) { Net->DebugFeedPacket(kOpResult, B); }
	}

	// Подать 5467 (5 б): статус 1, баланс.
	static void FeedBalance(const FCtxRef& C)
	{
		TArray<uint8> B; B.SetNumZeroed(5);
		B[0] = 1;
		const int32 Bal = 999999;
		FMemory::Memcpy(&B[1], &Bal, 4);
		if (UAsdaNetSubsystem* Net = C->Sub<UAsdaNetSubsystem>()) { Net->DebugFeedPacket(kOpBalance, B); }
	}

	// Показано ли окно-сообщение.
	static bool BoxShown(const FCtxRef& C)
	{
		const UAsdaWindowSubsystem* WM = C->Sub<UAsdaWindowSubsystem>();
		return WM && WM->IsWindowVisible(FName(kBox));
	}

	// Нажать ОК окна-сообщения (тот же мультикаст, что щелчок).
	static void BoxOk(const FCtxRef& C)
	{
		const UAsdaWindowSubsystem* WM = C->Sub<UAsdaWindowSubsystem>();
		UUserWidget* Box = WM ? WM->FindWindow(FName(kBox)) : nullptr;
		if (UButton* B = Box ? Cast<UButton>(Box->GetWidgetFromName(TEXT("ONEBUT_BUT00"))) : nullptr) { B->OnClicked.Broadcast(); }
	}

	// Проверка отказа: сцена открыта, окно показано и модально; ОК его закрывает.
	static void ExpectRefused(const FCtxRef& C, const FString& Tag)
	{
		UAsdaPremiumGachaSubsystem* G = C->G();
		const UAsdaWindowSubsystem* WM = C->Sub<UAsdaWindowSubsystem>();
		C->Expect(G && G->IsSceneOpen() && BoxShown(C) && WM && WM->IsAnyModalOpen(),
			FString::Printf(TEXT("%s: сцена открыта %d, окно %d, модально %d"), *Tag, G ? G->IsSceneOpen() : -1, BoxShown(C), WM ? WM->IsAnyModalOpen() : -1));
		BoxOk(C);
		C->Expect(!BoxShown(C), FString::Printf(TEXT("%s: ОК закрывает окно"), *Tag));
	}

	// Заявка на автомат `Slot`: «Играть» полосы → «Играть» 68108.
	static void Spin(const FCtxRef& C, int32 Slot)
	{
		UAsdaPremiumGachaSubsystem* G = C->G();
		const int32 Before = C->Sent;
		G->PressSlotButton(Slot, /*bSpin*/ true);
		G->PressConfirmPlay();
		C->Expect(C->Sent == Before + 1 && G->IsRequestInFlight() && G->GetSelectedSlot() == Slot,
			FString::Printf(TEXT("автомат %d: 6577 ушёл, запрос в полёте"), Slot + 1));
	}

	static void Finish(const FCtxRef& C);
	static void RoundStart(const FCtxRef& C);
	static void RoundBall(const FCtxRef& C);
	static void RoundAgain(const FCtxRef& C);
	static void RoundPrize(const FCtxRef& C);
	static void MapBall(const FCtxRef& C);
	static void DiscBall(const FCtxRef& C);
	static void FightBall(const FCtxRef& C);
	static void FightHold(const FCtxRef& C);
	static void FightPrize(const FCtxRef& C);
	static void FightClosed(const FCtxRef& C);
	static void SilenceStart(const FCtxRef& C);
	static void SilenceEnd(const FCtxRef& C);
	static void SilenceBall(const FCtxRef& C);
	static void AfterShot(const FCtxRef& C);

	// R0: ключ 0 — воспроизведение бага.
	static void R0Ball(const FCtxRef& C)
	{
		UAsdaPremiumGachaSubsystem* G = C->G();
		C->Expect(G->GetPhase() == EAsdaGachaPhase::AwaitClick, FString::Printf(TEXT("R0 шар выдан: фаза %d"), (int32)G->GetPhase()));
		G->PressCloseButton(/*bExitBar*/ true);
		C->Expect(!G->IsSceneOpen() && !BoxShown(C), TEXT("R0 ключ 0: кнопка 68076 закрыла сцену при шаре, окна нет"));
		G->OpenScene();
		C->Expect(G->IsSceneOpen() && G->GetPhase() == EAsdaGachaPhase::Idle,
			FString::Printf(TEXT("R0 ключ 0: новое открытие — шара нет (баг владельца), фаза %d"), (int32)G->GetPhase()));
		C->Keys->Set(kLockKey, 1);
		After(C, 0.3f, &RoundStart);
	}

	// R0: старт.
	static void R0(const FCtxRef& C)
	{
		UAsdaPremiumGachaSubsystem* G = C->G();
		C->Expect(G && G->IsSceneOpen(), TEXT("сцена гачи открыта"));
		if (!G || !G->IsSceneOpen()) { Finish(C); return; }
		FeedBalance(C);
		C->Keys->Set(kLockKey, 0);
		Spin(C, 0);
		FeedPrize(C);
		After(C, kToBall, &R0Ball);
	}

	// L1: ожидание 6578.
	static void RoundStart(const FCtxRef& C)
	{
		UAsdaPremiumGachaSubsystem* G = C->G();
		const FString T = AsdaNotice::Loc(C->GI(), kLocLock);
		bool bCyr = false;
		for (const TCHAR Ch : T) { if (Ch >= 0x0400 && Ch <= 0x04FF) { bCyr = true; break; } }
		const bool bEn = FAsdaLang::Current() == EAsdaLang::En;
		if (C->Slot == 0) { C->Expect(!T.IsEmpty() && bCyr == !bEn, FString::Printf(TEXT("строка 991202 (%s): «%s»"), bEn ? TEXT("EN") : TEXT("RU"), *T)); }
		Spin(C, C->Slot);
		G->PressCloseButton(/*bExitBar*/ true);
		if (C->bShot && !C->bShotDone)
		{
			C->bShotDone = true;
			const FString Png = FPaths::ConvertRelativePathToFull(FPaths::Combine(FPaths::ProjectDir(), TEXT("Saved"), TEXT("Screenshots"),
				FString::Printf(TEXT("asda_gacha_lock_%s.png"), bEn ? TEXT("en") : TEXT("ru"))));
			UE_LOG(LogAsdaGachaLockTest, Warning, TEXT("%s КАДР → %s"), kPrefix, *Png);
			FScreenshotRequest::RequestScreenshot(Png, /*bShowUI=*/true, /*bAddFilenameSuffix=*/false);
			After(C, 1.5f, &AfterShot);
			return;
		}
		AfterShot(C);
	}

	// L1 (конец) и L2: окно запрета, ответ сервера, прокрутка.
	static void AfterShot(const FCtxRef& C)
	{
		UAsdaPremiumGachaSubsystem* G = C->G();
		ExpectRefused(C, FString::Printf(TEXT("L1 автомат %d, ожидание 6578, кнопка 68076"), C->Slot + 1));
		FeedPrize(C);
		C->Expect(G->GetPhase() == EAsdaGachaPhase::Rolling && G->GetMachineClip(C->Slot) == 2,
			FString::Printf(TEXT("L2 автомат %d: прокрутка, клип %d"), C->Slot + 1, G->GetMachineClip(C->Slot)));
		G->PressCloseButton(/*bExitBar*/ false);
		ExpectRefused(C, FString::Printf(TEXT("L2 автомат %d, прокрутка, крестик 68209"), C->Slot + 1));
		G->EscClose();
		ExpectRefused(C, FString::Printf(TEXT("L2 автомат %d, прокрутка, ESC"), C->Slot + 1));
		G->ToggleScene();
		ExpectRefused(C, FString::Printf(TEXT("L2 автомат %d, прокрутка, кнопка меню"), C->Slot + 1));
		After(C, kToBall, &RoundBall);
	}

	// L3: шар, отказ и вынужденное закрытие.
	static void RoundBall(const FCtxRef& C)
	{
		UAsdaPremiumGachaSubsystem* G = C->G();
		C->Expect(G->GetPhase() == EAsdaGachaPhase::AwaitClick, FString::Printf(TEXT("L3 автомат %d: шар, фаза %d"), C->Slot + 1, (int32)G->GetPhase()));
		G->PressCloseButton(/*bExitBar*/ true);
		ExpectRefused(C, FString::Printf(TEXT("L3 автомат %d, шар, кнопка 68076"), C->Slot + 1));
		G->CloseScene();   // тот путь, что смерть
		G->OpenScene();
		C->Expect(G->GetPhase() == EAsdaGachaPhase::Dispensing && G->GetSelectedSlot() == C->Slot && G->GetPrizeItemCode() == kPrize
			&& G->GetMachineClip(C->Slot) == 3,
			FString::Printf(TEXT("L3 автомат %d: после вынужденного закрытия снова выдача: фаза %d, выбран %d, приз %d, клип %d"),
				C->Slot + 1, (int32)G->GetPhase(), G->GetSelectedSlot() + 1, G->GetPrizeItemCode(), G->GetMachineClip(C->Slot)));
		After(C, kToDispense, &RoundAgain);
	}

	// L4: вскрытие.
	static void RoundAgain(const FCtxRef& C)
	{
		UAsdaPremiumGachaSubsystem* G = C->G();
		C->Expect(G->GetPhase() == EAsdaGachaPhase::AwaitClick, FString::Printf(TEXT("L3 автомат %d: шар снова на месте, фаза %d"), C->Slot + 1, (int32)G->GetPhase()));
		G->ClickBall();
		After(C, kToDispense, &RoundPrize);
	}

	// L4: окно приза, отказ, забор, свободное закрытие.
	static void RoundPrize(const FCtxRef& C)
	{
		UAsdaPremiumGachaSubsystem* G = C->G();
		C->Expect(G->GetPhase() == EAsdaGachaPhase::Prize, FString::Printf(TEXT("L4 автомат %d: окно приза, фаза %d"), C->Slot + 1, (int32)G->GetPhase()));
		G->PressCloseButton(/*bExitBar*/ true);
		ExpectRefused(C, FString::Printf(TEXT("L4 автомат %d, окно приза, кнопка 68076"), C->Slot + 1));
		G->TakePrize();
		C->Expect(G->GetPhase() == EAsdaGachaPhase::Idle && !G->IsPrizePending(), FString::Printf(TEXT("L4 автомат %d: приз забран, покой"), C->Slot + 1));
		G->PressCloseButton(/*bExitBar*/ true);
		C->Expect(!G->IsSceneOpen() && !BoxShown(C), FString::Printf(TEXT("L4 автомат %d: кнопка 68076 закрывает сцену"), C->Slot + 1));
		G->OpenScene();
		C->Expect(G->IsSceneOpen() && G->GetPhase() == EAsdaGachaPhase::Idle, FString::Printf(TEXT("L4 автомат %d: новое открытие без шара"), C->Slot + 1));
		FeedBalance(C);
		if (++C->Slot < 5) { After(C, 0.3f, &RoundStart); return; }
		// M1: смена карты при шаре.
		C->Slot = 2;
		Spin(C, C->Slot);
		FeedPrize(C);
		After(C, kToBall, &MapBall);
	}

	// M1 и старт M2.
	static void MapBall(const FCtxRef& C)
	{
		UAsdaPremiumGachaSubsystem* G = C->G();
		G->ResetSessionState(EAsdaSessionResetReason::MapChanged);
		C->Expect(!G->IsSceneOpen(), TEXT("M1 смена карты закрыла сцену"));
		G->OpenScene();
		C->Expect(G->GetPhase() == EAsdaGachaPhase::Dispensing && G->GetSelectedSlot() == 2 && G->GetPrizeItemCode() == kPrize,
			FString::Printf(TEXT("M1 шар вернулся: фаза %d, выбран %d, приз %d"), (int32)G->GetPhase(), G->GetSelectedSlot() + 1, G->GetPrizeItemCode()));
		After(C, kToDispense, &DiscBall);
	}

	// M2: разрыв связи при шаре.
	static void DiscBall(const FCtxRef& C)
	{
		UAsdaPremiumGachaSubsystem* G = C->G();
		G->ResetSessionState(EAsdaSessionResetReason::Disconnected);
		G->OpenScene();
		C->Expect(G->GetPhase() == EAsdaGachaPhase::Idle, FString::Printf(TEXT("M2 разрыв: при открытии шара нет (приз в элитном ящике), фаза %d"), (int32)G->GetPhase()));
		FeedBalance(C);
		Spin(C, 1);
		FeedPrize(C);
		After(C, kToBall, &FightBall);
	}

	// B1: бой при шаре.
	static void FightBall(const FCtxRef& C)
	{
		if (const UWorld* W = C->W.Get())
		{
			if (AAsdaPlayerController* PC = Cast<AAsdaPlayerController>(W->GetFirstPlayerController())) { PC->bInCombatStance = true; }
		}
		After(C, 0.5f, &FightHold);
	}

	// B1: сцена держится.
	static void FightHold(const FCtxRef& C)
	{
		UAsdaPremiumGachaSubsystem* G = C->G();
		C->Expect(G->IsSceneOpen() && G->GetPhase() == EAsdaGachaPhase::AwaitClick, TEXT("B1 бой при шаре сцену не закрыл"));
		G->ClickBall();
		After(C, kToDispense, &FightPrize);
	}

	// B1: забор приза в бою.
	static void FightPrize(const FCtxRef& C)
	{
		C->G()->TakePrize();
		After(C, 0.5f, &FightClosed);
	}

	// B1: после забора бой закрывает сцену.
	static void FightClosed(const FCtxRef& C)
	{
		UAsdaPremiumGachaSubsystem* G = C->G();
		C->Expect(!G->IsSceneOpen(), TEXT("B1 после забора приза бой закрыл сцену (как эталон, нотис 6813)"));
		if (const UWorld* W = C->W.Get())
		{
			if (AAsdaPlayerController* PC = Cast<AAsdaPlayerController>(W->GetFirstPlayerController())) { PC->bInCombatStance = false; }
		}
		G->OpenScene();
		After(C, 0.5f, &SilenceStart);
	}

	// T1: сервер молчит.
	static void SilenceStart(const FCtxRef& C)
	{
		FeedBalance(C);
		Spin(C, 4);
		After(C, 15.5f, &SilenceEnd);
	}

	// T1: через 15 с закрыть можно; поздний ответ запоминается.
	static void SilenceEnd(const FCtxRef& C)
	{
		UAsdaPremiumGachaSubsystem* G = C->G();
		G->PressCloseButton(/*bExitBar*/ true);
		C->Expect(!G->IsSceneOpen() && !BoxShown(C), TEXT("T1 сервер молчит 15 с: кнопка 68076 закрывает сцену (нет тупика)"));
		FeedPrize(C);
		C->Expect(!G->IsSceneOpen(), TEXT("T1 поздний 6578 сцену не открывает"));
		G->OpenScene();
		C->Expect(G->GetPhase() == EAsdaGachaPhase::Dispensing && G->GetSelectedSlot() == 4 && G->GetPrizeItemCode() == kPrize,
			FString::Printf(TEXT("T1 после позднего 6578 автомат 5 выдаёт шар: фаза %d, выбран %d, приз %d"), (int32)G->GetPhase(), G->GetSelectedSlot() + 1, G->GetPrizeItemCode()));
		After(C, kToDispense, &SilenceBall);
	}

	// T1: завершить приз.
	static void SilenceBall(const FCtxRef& C)
	{
		UAsdaPremiumGachaSubsystem* G = C->G();
		G->ClickBall();
		After(C, kToDispense, [](const FCtxRef& X) { X->G()->TakePrize(); Finish(X); });
	}

	// Конец прогона: снять перехват, закрыть сцену, вернуть ключи и «своего», итог.
	static void Finish(const FCtxRef& C)
	{
		if (UAsdaNetSubsystem* Net = C->Sub<UAsdaNetSubsystem>()) { AsdaSelfTestKit::DisarmCapture(Net, C->CapGen); }
		// ★ 07.11.2026: полный сброс, а не `CloseScene`: незабранный шар не должен перейти в следующий самотест пакета.
		if (UAsdaPremiumGachaSubsystem* G = C->G()) { G->ResetSessionState(EAsdaSessionResetReason::Disconnected); }
		if (C->Keys) { C->Keys->Restore(); }
		if (C->Self) { C->Self->Restore(); }
		AsdaSelfTestKit::Finish(C->W.Get(), kCmd, C->bQuit, LogAsdaGachaLockTest, kPrefix, C->Ok, C->Fail);
	}

	// Старт: перехват 6577, charId, открытие сцены тем же путём, что кнопка 73410.
	static void Run(const FCtxRef& C)
	{
		UAsdaNetSubsystem* Net = C->Sub<UAsdaNetSubsystem>();
		UAsdaPremiumGachaSubsystem* G = C->G();
		if (!Net || !G) { AsdaSelfTestKit::Refused(C->W.Get(), kCmd, C->bQuit, TEXT("нет подсистемы сети или гачи")); return; }
		C->Self = MakeUnique<FAsdaNetSelfScope>(Net, kCmd);
		C->Keys = MakeUnique<FAsdaCVarScope>(C->W.Get(), kCmd);
		Net->SelectedCharId = 75214;   // charId тела 6577 — любой ненулевой, сеть офлайн
		TWeakPtr<FCtx> WC = C;
		C->CapGen = AsdaSelfTestKit::ArmCapture(Net, [WC](uint16 Op, const uint8* P, int32 Len, bool, TCHAR)
		{
			if (TSharedPtr<FCtx> S = WC.Pin()) { if (Op == kOpRoll && P && Len >= 5) { ++S->Sent; } }
		}, (uint16)kOpRoll, kCmd);
		if (C->CapGen == 0) { AsdaSelfTestKit::Refused(C->W.Get(), kCmd, C->bQuit, TEXT("перехват отправок занят")); return; }
		if (G->IsSceneOpen()) { G->CloseScene(); }
		G->OpenScene();
		After(C, 1.5f, &R0);
	}
}

static FAutoConsoleCommandWithWorldAndArgs GAsdaSelfTestGachaPrizeLock(
	TEXT("asda.SelfTest.GachaPrizeLock"),
	TEXT("07.11.2026: сцену премиум-гачи нельзя закрыть, пока приз не забран (5 автоматов; кнопки 68076/68209, ESC, меню, бой), окно 991202 на языке "
	     "сессии; вынужденное закрытие и смена карты возвращают шар при открытии; разрыв — приз в элитном ящике; 15 с молчания сервера — не тупик; "
	     "ключ asda.Gacha.PrizeCloseLock 0 воспроизводит баг. Офлайн. `delay=N` `quit` `shot`. Язык — `-AsdaLang=`."),
	FConsoleCommandWithWorldAndArgsDelegate::CreateLambda([](const TArray<FString>& Args, UWorld* W)
	{
		using namespace AsdaGachaLockTest;
		const FAsdaSelfTestArgs KA = FAsdaSelfTestArgs::Parse(Args);
		// Сцена гачи живёт при своём персонаже в мире — офлайн его нет: штатный фейковый спавн и повтор через 2 с.
		if (!KA.Has(TEXT("selfready")) && W && GEngine && AsdaSelfTestKit::NeedSelfSpawn(W, kSelfName))
		{
			GEngine->Exec(W, TEXT("Asda.Load.CurrentMap 3"));
			GEngine->Exec(W, *FString::Printf(TEXT("Asda.World.FakeSelfSpawn 256 256 %s"), kSelfName));
			const FString Again = FString::Printf(TEXT("%s selfready%s%s"), kCmd, KA.bQuit ? TEXT(" quit") : TEXT(""), KA.Has(TEXT("shot")) ? TEXT(" shot") : TEXT(""));
			TWeakObjectPtr<UWorld> WW(W);
			FTimerHandle H;
			W->GetTimerManager().SetTimer(H, FTimerDelegate::CreateLambda([WW, Again]() { if (WW.IsValid() && GEngine) { GEngine->Exec(WW.Get(), *Again); } }), 2.f, false);
			return;
		}
		if (AsdaSelfTestKit::RefuseStart(W, kCmd, KA)) { return; }
		if (!W) { return; }
		FCtxRef C = MakeShared<FCtx>();
		C->W = W;
		C->bQuit = KA.bQuit;
		C->bShot = KA.Has(TEXT("shot"));
		if (KA.Delay > 0.f)
		{
			FTimerHandle H;
			W->GetTimerManager().SetTimer(H, FTimerDelegate::CreateLambda([C]() { Run(C); }), KA.Delay, false);
		}
		else
		{
			Run(C);
		}
	}));

#endif // ASDA_DEV_CONSOLE && !UE_BUILD_SHIPPING
