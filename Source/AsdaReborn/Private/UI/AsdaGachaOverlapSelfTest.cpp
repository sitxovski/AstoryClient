// Copyright (c) 2026 @sitxovski (Discord)
// ★★★ САМОТЕСТ «ЩЕЛЧОК ПО СОСЕДНЕМУ АВТОМАТУ ГАЧИ ВО ВРЕМЯ РОЗЫГРЫША» (05.11.2026): `asda.SelfTest.GachaOverlap [delay=N] [quit]`.
//   Офлайн (-AsdaNoNet). Баг игроков: пока идёт розыгрыш на одном автомате премиум-гачи, щелчок по соседнему засчитывался
//   соседнему — прокрутка/шар/вскрытие уезжали на него («приз не с того автомата», «оба автомата заглохли»).
//   Эталон: смена автомата только за гейтом `GachaScene_OnControlMessage@0xB704C0` → `uiTradeConfirmMoney@0xB70420` /
//   `openItemGridIfReady@0xB70470` (ui_core.c:7517-7619): 6577 не в полёте (`+644`) и все автоматы в клипе < 2; витрина
//   прячет диалог оплаты (`uiOpenItemGridWindow@0xB6EDE0`). Ответ 6578 номера автомата не несёт.
//   Путь игрока: НАСТОЯЩИЕ кнопки полос (`PressSlotButton` = мультикаст `OnClicked`, как мышь), «Играть» 68108 диалога,
//   исходящий 6577 — перехватом отправок, входящие 5467/6578 — штатным `DebugFeedPacket` → `HandlePacket` (тела по раскладке
//   сервера: 5467 5 б, 6578 63 б — `sub_4F7D10@0x4F7D10`, GameServer.c:95644-95680).
//   A1  баланс 5467 → «Играть» автомата 1 → диалог 68100, выбор 1
//   A2  «Играть» 68108 → ровно один 6577 со слотом 0, запрос в полёте
//   A3  во время ожидания: «Играть» автомата 3 и витрина автомата 4 — выбор 1, диалога/витрины нет, второго 6577 нет
//   A4  повторный «Играть» 68108 во время ожидания — второго 6577 нет
//   B1  ответ 6578 С ЗАДЕРЖКОЙ 1.5 с (статус 1) → прокрутка на автомате 1 (клип 2), соседи в клипе 1, приз из ответа
//   B2  во время прокрутки щелчки по автоматам 3/5 (обе кнопки) — выбор 1, клипы соседей 1, 6577 нет
//   B3  выдача (клип 3) на автомате 1 → шар; щелчок по автомату 2 во время ожидания шара — выбор 1
//   B4  шар → вскрытие (клип 4) на автомате 1 → окно приза → «Нажми» → все в покое (клип 1)
//   C1  отказ: автомат 2 → 6577 слот 1 → щелчок по автомату 5 отклонён → 6578 статус 3 → окно-сообщение 110624 (язык сессии),
//       покой, запрос снят; после отказа автомат 5 снова выбирается
//   D1  диалог автомата 1 → витрина автомата 4 → диалог закрыт, выбор 4 (эталон `uiOpenItemGridWindow`)
//   K0  ключ `asda.Gacha.SlotSwitchGate 0` (прежнее поведение порта): щелчок по автомату 3 во время ожидания МЕНЯЕТ выбор,
//       и 6578 крутит автомат 3 — воспроизведение бага игроков
//   Итог: «ГАЧА-СОСЕД ИТОГ: ok=N fail=M».
#include "CoreMinimal.h"
#include "AsdaDevConsole.h"

#if ASDA_DEV_CONSOLE && !UE_BUILD_SHIPPING

#include "AsdaNetSubsystem.h"
#include "Test/AsdaSelfTestKit.h"
#include "Text/AsdaLang.h"
#include "UI/AsdaNotice.h"
#include "UI/AsdaPremiumGachaSubsystem.h"
#include "UI/AsdaWindowSubsystem.h"
#include "Blueprint/UserWidget.h"
#include "Components/Button.h"
#include "Engine/Engine.h"
#include "Engine/GameInstance.h"
#include "Engine/World.h"
#include "HAL/IConsoleManager.h"
#include "TimerManager.h"

DEFINE_LOG_CATEGORY_STATIC(LogAsdaGachaOverlapTest, Log, All);   // канал строк самотеста

namespace AsdaGachaOverlapTest
{
	static const TCHAR* kCmd      = TEXT("asda.SelfTest.GachaOverlap");   // имя команды
	static const TCHAR* kPrefix   = TEXT("ГАЧА-СОСЕД");                   // префикс строк и итога
	static const TCHAR* kSelfName = TEXT("ИгрокГачи");                    // имя своего персонажа офлайн-спавна
	static const TCHAR* kGateKey  = TEXT("asda.Gacha.SlotSwitchGate");    // проверяемый ключ (K0)
	static constexpr int32 kOpBalance = 5467;     // S→C премиум-баланс (5 б)
	static constexpr int32 kOpRoll    = 6577;     // C→S прокрутка (5 б: charId, слот)
	static constexpr int32 kOpResult  = 6578;     // S→C результат прокрутки (63 б)
	static constexpr int32 kPrizeA    = 68408;    // приз первого прокрута (строка 0 PrimiumGaChaTable, автомат 0)
	static constexpr int32 kPrizeK    = 68426;    // приз прокрута ветки K0
	static constexpr int32 kLocNoMoney = 110624;  // отказ 6578 статус 3

	// Контекст прогона.
	struct FCtx
	{
		int32 Ok = 0, Fail = 0;                       // пройдено / провалено
		TWeakObjectPtr<UWorld> W;                     // мир прогона
		bool bQuit = false;                           // выйти после итога
		uint32 CapGen = 0;                            // поколение перехвата отправок
		TArray<int32> Sent;                           // слоты перехваченных 6577 (по порядку)
		TUniquePtr<FAsdaNetSelfScope> Self;           // «свой персонаж» сети — вернуть в конце
		TUniquePtr<FAsdaCVarScope> Keys;              // ключи самотеста — вернуть в конце

		void Expect(bool b, const FString& What)
		{
			if (b) { ++Ok;   UE_LOG(LogAsdaGachaOverlapTest, Log,   TEXT("%s ok   %s"), kPrefix, *What); }
			else   { ++Fail; UE_LOG(LogAsdaGachaOverlapTest, Error, TEXT("%s FAIL %s"), kPrefix, *What); }
		}
		UGameInstance* GI() const { return W.IsValid() ? W->GetGameInstance() : nullptr; }
		template <class T> T* Sub() const { UGameInstance* G = GI(); return G ? G->GetSubsystem<T>() : nullptr; }
		UAsdaPremiumGachaSubsystem* G() const { return Sub<UAsdaPremiumGachaSubsystem>(); }
	};
	using FCtxRef = TSharedRef<FCtx>;
	using FStep = void (*)(const FCtxRef&);

	// Запустить шаг через `Sec` секунд (таймер мира; мира нет — шаг не идёт, итог печатает `Finish` из ветки отказа).
	static void After(const FCtxRef& C, float Sec, FStep Fn)
	{
		if (!C->W.IsValid()) { return; }
		FTimerHandle H;
		C->W->GetTimerManager().SetTimer(H, FTimerDelegate::CreateLambda([C, Fn]() { Fn(C); }), Sec, false);
	}

	// Тело 5467 по раскладке `netHandleAuthResult@0xECB480`: @0 u8 статус, @1 i32 баланс.
	static TArray<uint8> Body5467(uint8 Status, int32 Balance)
	{
		TArray<uint8> B; B.SetNumZeroed(5);
		B[0] = Status;
		FMemory::Memcpy(&B[1], &Balance, 4);
		return B;
	}

	// Тело 6578 (63 б) по раскладке сервера `sub_4F7D10`: @0 статус, @1 код приза (запись 56 б, остальное — конструктор),
	//   @57 хвост 0, @59 i32 новый баланс.
	static TArray<uint8> Body6578(uint8 Status, int32 Prize, int32 Balance)
	{
		TArray<uint8> B; B.SetNumZeroed(63);
		B[0] = Status;
		FMemory::Memcpy(&B[1], &Prize, 4);
		FMemory::Memcpy(&B[59], &Balance, 4);
		return B;
	}

	// Подать пакет штатным приёмом.
	static void Feed(const FCtxRef& C, int32 Op, const TArray<uint8>& Body)
	{
		if (UAsdaNetSubsystem* Net = C->Sub<UAsdaNetSubsystem>()) { Net->DebugFeedPacket(Op, Body); }
	}

	// Клипы пяти автоматов строкой «1,1,2,1,1».
	static FString Clips(const FCtxRef& C)
	{
		FString S;
		for (int32 i = 0; i < 5; ++i) { S += FString::Printf(TEXT("%s%d"), i ? TEXT(",") : TEXT(""), C->G() ? C->G()->GetMachineClip(i) : -9); }
		return S;
	}

	// Все автоматы, кроме `Except`, в клипе `Clip`.
	static bool OthersIn(const FCtxRef& C, int32 Except, int32 Clip)
	{
		for (int32 i = 0; i < 5; ++i) { if (i != Except && C->G()->GetMachineClip(i) != Clip) { return false; } }
		return true;
	}

	// Конец прогона: снять перехват, закрыть сцену, вернуть ключи и «своего», итог.
	static void Finish(const FCtxRef& C)
	{
		if (UAsdaNetSubsystem* Net = C->Sub<UAsdaNetSubsystem>()) { AsdaSelfTestKit::DisarmCapture(Net, C->CapGen); }
		// ★ 07.11.2026: полный сброс, а не `CloseScene`: незабранный шар не должен перейти в следующий самотест пакета.
		if (UAsdaPremiumGachaSubsystem* G = C->G()) { G->ResetSessionState(EAsdaSessionResetReason::Disconnected); }
		if (C->Keys) { C->Keys->Restore(); }
		if (C->Self) { C->Self->Restore(); }
		AsdaSelfTestKit::Finish(C->W.Get(), kCmd, C->bQuit, LogAsdaGachaOverlapTest, kPrefix, C->Ok, C->Fail);
	}

	static void StepB1(const FCtxRef& C);
	static void StepB3(const FCtxRef& C);
	static void StepB4(const FCtxRef& C);
	static void StepB5(const FCtxRef& C);
	static void StepC1(const FCtxRef& C);
	static void StepK0(const FCtxRef& C);
	static void StepK1(const FCtxRef& C);

	// A1–A4: заявка автомата 1 и щелчки по соседям, пока ждём ответа.
	static void StepA(const FCtxRef& C)
	{
		UAsdaPremiumGachaSubsystem* G = C->G();
		C->Expect(G && G->IsSceneOpen(), TEXT("сцена гачи открыта"));
		if (!G || !G->IsSceneOpen()) { Finish(C); return; }
		Feed(C, kOpBalance, Body5467(1, 999999));
		C->Expect(G->GetBalance() == 999999, FString::Printf(TEXT("A1 баланс 5467 = %d"), G->GetBalance()));
		C->Expect(G->PressSlotButton(0, /*bSpin*/ true), TEXT("A1 кнопка «Играть» автомата 1 есть"));
		C->Expect(G->IsConfirmDialogShown() && G->GetSelectedSlot() == 0,
			FString::Printf(TEXT("A1 диалог 68100 показан=%d, выбран автомат %d (ждём 1)"), G->IsConfirmDialogShown(), G->GetSelectedSlot() + 1));
		C->Expect(G->PressConfirmPlay(), TEXT("A2 кнопка «Играть» 68108 есть"));
		C->Expect(C->Sent.Num() == 1 && C->Sent[0] == 0 && G->IsRequestInFlight(),
			FString::Printf(TEXT("A2 ушёл один 6577 слот %d, в полёте=%d"), C->Sent.Num() ? C->Sent[0] : -1, G->IsRequestInFlight()));
		G->PressSlotButton(2, true);
		G->PressSlotButton(3, false);
		C->Expect(G->GetSelectedSlot() == 0 && !G->IsConfirmDialogShown() && !G->IsPrizeGridShown() && C->Sent.Num() == 1,
			FString::Printf(TEXT("A3 ожидание 6578: «Играть» автомата 3 и витрина автомата 4 → выбран %d, диалог %d, витрина %d, 6577 всего %d"),
				G->GetSelectedSlot() + 1, G->IsConfirmDialogShown(), G->IsPrizeGridShown(), C->Sent.Num()));
		G->PressConfirmPlay();
		C->Expect(C->Sent.Num() == 1, FString::Printf(TEXT("A4 повторный «Играть» 68108 во время ожидания → 6577 всего %d (ждём 1)"), C->Sent.Num()));
		C->Expect(G->GetPhase() == EAsdaGachaPhase::Idle && OthersIn(C, -1, 1),
			FString::Printf(TEXT("A4 до ответа автомат не крутится: фаза %d, клипы %s"), (int32)G->GetPhase(), *Clips(C)));
		After(C, 1.5f, &StepB1);   // ответ с задержкой
	}

	// B1–B2: ответ 6578 с задержкой → прокрутка на автомате 1; щелчки по соседям во время прокрутки.
	static void StepB1(const FCtxRef& C)
	{
		UAsdaPremiumGachaSubsystem* G = C->G();
		if (!G) { Finish(C); return; }
		Feed(C, kOpResult, Body6578(1, kPrizeA, 999999));
		C->Expect(G->GetPhase() == EAsdaGachaPhase::Rolling && !G->IsRequestInFlight() && G->GetMachineClip(0) == 2 && OthersIn(C, 0, 1)
			&& G->GetPrizeItemCode() == kPrizeA,
			FString::Printf(TEXT("B1 6578 через 1.5 с: фаза %d, клипы %s, приз %d"), (int32)G->GetPhase(), *Clips(C), G->GetPrizeItemCode()));
		G->PressSlotButton(2, true);
		G->PressSlotButton(4, true);
		G->PressSlotButton(2, false);
		C->Expect(G->GetSelectedSlot() == 0 && G->GetMachineClip(0) == 2 && OthersIn(C, 0, 1) && !G->IsConfirmDialogShown()
			&& !G->IsPrizeGridShown() && C->Sent.Num() == 1,
			FString::Printf(TEXT("B2 прокрутка: щелчки по автоматам 3/5 → выбран %d, клипы %s, диалог %d, витрина %d, 6577 всего %d"),
				G->GetSelectedSlot() + 1, *Clips(C), G->IsConfirmDialogShown(), G->IsPrizeGridShown(), C->Sent.Num()));
		After(C, 4.8f, &StepB3);   // клип 2 (2.000 с) + клип 3 (2.333 с) с запасом
	}

	// B3: выдача отыграна — шар ждёт щелчка; щелчок по соседу в это время отклонён.
	static void StepB3(const FCtxRef& C)
	{
		UAsdaPremiumGachaSubsystem* G = C->G();
		if (!G) { Finish(C); return; }
		C->Expect(G->GetPhase() == EAsdaGachaPhase::AwaitClick && G->GetMachineClip(0) == 3 && OthersIn(C, 0, 1),
			FString::Printf(TEXT("B3 выдача на автомате 1: фаза %d, клипы %s"), (int32)G->GetPhase(), *Clips(C)));
		G->PressSlotButton(1, true);
		C->Expect(G->GetSelectedSlot() == 0 && !G->IsConfirmDialogShown(),
			FString::Printf(TEXT("B3 щелчок по автомату 2 во время ожидания шара → выбран %d, диалог %d"), G->GetSelectedSlot() + 1, G->IsConfirmDialogShown()));
		G->ClickBall();
		C->Expect(G->GetPhase() == EAsdaGachaPhase::Opening && G->GetMachineClip(0) == 4 && OthersIn(C, 0, 1),
			FString::Printf(TEXT("B4 шар → вскрытие на автомате 1: фаза %d, клипы %s"), (int32)G->GetPhase(), *Clips(C)));
		After(C, 2.8f, &StepB4);   // клип 4 (2.333 с) с запасом
	}

	// B4: окно приза → «Нажми» → покой.
	static void StepB4(const FCtxRef& C)
	{
		UAsdaPremiumGachaSubsystem* G = C->G();
		if (!G) { Finish(C); return; }
		C->Expect(G->GetPhase() == EAsdaGachaPhase::Prize, FString::Printf(TEXT("B4 окно приза: фаза %d"), (int32)G->GetPhase()));
		G->TakePrize();
		C->Expect(G->GetPhase() == EAsdaGachaPhase::Idle && OthersIn(C, -1, 1),
			FString::Printf(TEXT("B4 «Нажми» → покой: фаза %d, клипы %s"), (int32)G->GetPhase(), *Clips(C)));
		After(C, 0.3f, &StepC1);
	}

	// C1: отказ сервера при щелчке по соседу в ожидании.
	static void StepC1(const FCtxRef& C)
	{
		UAsdaPremiumGachaSubsystem* G = C->G();
		if (!G) { Finish(C); return; }
		G->PressSlotButton(1, true);
		G->PressConfirmPlay();
		C->Expect(C->Sent.Num() == 2 && C->Sent[1] == 1 && G->IsRequestInFlight(),
			FString::Printf(TEXT("C1 автомат 2: 6577 всего %d, слот %d"), C->Sent.Num(), C->Sent.Num() > 1 ? C->Sent[1] : -1));
		G->PressSlotButton(4, true);
		C->Expect(G->GetSelectedSlot() == 1 && !G->IsConfirmDialogShown(),
			FString::Printf(TEXT("C1 щелчок по автомату 5 в ожидании → выбран %d, диалог %d"), G->GetSelectedSlot() + 1, G->IsConfirmDialogShown()));
		const FString T = AsdaNotice::Loc(C->GI(), kLocNoMoney);
		Feed(C, kOpResult, Body6578(3, 0, 999999));
		const bool bEn = FAsdaLang::Current() == EAsdaLang::En;
		bool bCyr = false;
		for (const TCHAR Ch : T) { if (Ch >= 0x0400 && Ch <= 0x04FF) { bCyr = true; break; } }
		// ★ 07.11.2026 [ОРИГИНАЛ]: отказ — окно-сообщение ONEBUT_MSGBOX (`CItem_BuildTooltipText(…, 0, -1, …)`), а не строка чата.
		UAsdaWindowSubsystem* WM = C->Sub<UAsdaWindowSubsystem>();
		UUserWidget* Box = WM ? WM->FindWindow(FName(TEXT("ONEBUT_MSGBOX"))) : nullptr;
		C->Expect(!T.IsEmpty() && WM && WM->IsWindowVisible(FName(TEXT("ONEBUT_MSGBOX"))) && bCyr == !bEn,
			FString::Printf(TEXT("C1 6578 статус 3 → окно-сообщение с нотисом 110624 (%s) «%s»"), bEn ? TEXT("EN") : TEXT("RU"), *T));
		if (UButton* Ok = Box ? Cast<UButton>(Box->GetWidgetFromName(TEXT("ONEBUT_BUT00"))) : nullptr) { Ok->OnClicked.Broadcast(); }
		C->Expect(G->GetPhase() == EAsdaGachaPhase::Idle && !G->IsRequestInFlight() && OthersIn(C, -1, 1),
			FString::Printf(TEXT("C1 после отказа покой: фаза %d, в полёте %d, клипы %s"), (int32)G->GetPhase(), G->IsRequestInFlight(), *Clips(C)));
		G->PressSlotButton(4, true);
		C->Expect(G->GetSelectedSlot() == 4 && G->IsConfirmDialogShown(),
			FString::Printf(TEXT("C1 после отказа автомат 5 выбирается: выбран %d, диалог %d"), G->GetSelectedSlot() + 1, G->IsConfirmDialogShown()));
		G->CancelRoll();

		// D1: диалог автомата 1 → витрина автомата 4 (эталон прячет диалог).
		G->PressSlotButton(0, true);
		G->PressSlotButton(3, false);
		C->Expect(!G->IsConfirmDialogShown() && G->IsPrizeGridShown() && G->GetSelectedSlot() == 3,
			FString::Printf(TEXT("D1 диалог автомата 1 → витрина автомата 4: диалог %d, витрина %d, выбран %d"),
				G->IsConfirmDialogShown(), G->IsPrizeGridShown(), G->GetSelectedSlot() + 1));
		G->CloseGridWindow();
		After(C, 0.3f, &StepK0);
	}

	// K0: прежнее поведение порта под ключом 0 — воспроизведение бага.
	static void StepK0(const FCtxRef& C)
	{
		UAsdaPremiumGachaSubsystem* G = C->G();
		if (!G) { Finish(C); return; }
		C->Keys->Set(kGateKey, 0);
		G->PressSlotButton(0, true);
		G->PressConfirmPlay();
		G->PressSlotButton(2, true);
		C->Expect(G->GetSelectedSlot() == 2 && C->Sent.Num() == 3 && C->Sent[2] == 0,
			FString::Printf(TEXT("K0 ключ 0: 6577 слот %d, щелчок по автомату 3 в ожидании МЕНЯЕТ выбор → %d"),
				C->Sent.Num() > 2 ? C->Sent[2] : -1, G->GetSelectedSlot() + 1));
		Feed(C, kOpResult, Body6578(1, kPrizeK, 999999));
		C->Expect(G->GetMachineClip(2) == 2 && G->GetMachineClip(0) == 1,
			FString::Printf(TEXT("K0 ключ 0: ответ 6578 автомата 1 крутит автомат 3 (баг игроков): клипы %s"), *Clips(C)));
		C->Keys->Restore();
		After(C, 0.2f, &StepK1);
	}

	// K1: вернуть сцену в покой (закрыть) и подвести итог.
	static void StepK1(const FCtxRef& C)
	{
		IConsoleVariable* V = IConsoleManager::Get().FindConsoleVariable(kGateKey);
		C->Expect(V && V->GetInt() == 1, FString::Printf(TEXT("K1 ключ %s вернулся в %d"), kGateKey, V ? V->GetInt() : -1));
		Finish(C);
	}

	// Старт: перехват 6577, «свой» charId, открытие сцены тем же путём, что кнопка 73410.
	static void Run(const FCtxRef& C)
	{
		UAsdaNetSubsystem* Net = C->Sub<UAsdaNetSubsystem>();
		UAsdaPremiumGachaSubsystem* G = C->G();
		if (!Net || !G) { AsdaSelfTestKit::Refused(C->W.Get(), kCmd, C->bQuit, TEXT("нет подсистемы сети или гачи")); return; }
		C->Self = MakeUnique<FAsdaNetSelfScope>(Net, kCmd);
		C->Keys = MakeUnique<FAsdaCVarScope>(C->W.Get(), kCmd);
		C->Keys->Set(kGateKey, 1);
		Net->SelectedCharId = 75214;   // charId тела 6577 — любой ненулевой, сеть офлайн
		TWeakPtr<FCtx> WC = C;
		C->CapGen = AsdaSelfTestKit::ArmCapture(Net, [WC](uint16 Op, const uint8* P, int32 Len, bool, TCHAR)
		{
			if (TSharedPtr<FCtx> S = WC.Pin()) { if (Op == kOpRoll && P && Len >= 5) { S->Sent.Add((int32)P[4]); } }
		}, (uint16)kOpRoll, kCmd);
		if (C->CapGen == 0) { AsdaSelfTestKit::Refused(C->W.Get(), kCmd, C->bQuit, TEXT("перехват отправок занят")); return; }
		if (G->IsSceneOpen()) { G->CloseScene(); }
		G->OpenScene();
		After(C, 1.5f, &StepA);
	}
}

static FAutoConsoleCommandWithWorldAndArgs GAsdaSelfTestGachaOverlap(
	TEXT("asda.SelfTest.GachaOverlap"),
	TEXT("05.11.2026: премиум-гача — щелчки по соседним автоматам во время ожидания 6578, прокрутки, выдачи и после отказа "
	     "не меняют выбранный автомат и не шлют второй 6577 (гейт эталона uiTradeConfirmMoney/openItemGridIfReady); ответ с задержкой, "
	     "отказ 3 → нотис 110624; ключ asda.Gacha.SlotSwitchGate 0 воспроизводит баг. Офлайн. `delay=N` `quit`. Язык — `-AsdaLang=`."),
	FConsoleCommandWithWorldAndArgsDelegate::CreateLambda([](const TArray<FString>& Args, UWorld* W)
	{
		using namespace AsdaGachaOverlapTest;
		const FAsdaSelfTestArgs KA = FAsdaSelfTestArgs::Parse(Args);
		// Сцена гачи и системный журнал живут при своём персонаже в мире — офлайн его нет: штатный фейковый спавн и повтор через 2 с.
		if (!KA.Has(TEXT("selfready")) && W && GEngine && AsdaSelfTestKit::NeedSelfSpawn(W, kSelfName))
		{
			GEngine->Exec(W, TEXT("Asda.Load.CurrentMap 3"));
			GEngine->Exec(W, *FString::Printf(TEXT("Asda.World.FakeSelfSpawn 256 256 %s"), kSelfName));
			const FString Again = FString::Printf(TEXT("%s selfready%s"), kCmd, KA.bQuit ? TEXT(" quit") : TEXT(""));
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
