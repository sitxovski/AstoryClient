// Copyright (c) 2026 @sitxovski (Discord)
// ★★★ САМОТЕСТ «ОДИН GC-ЯКОРЬ — ОДНО ОКНО» (31.10.2026, `asda.SelfTest.RelayAnchors [quit]`). [ПОРТ]
//
// ★ ЗАЧЕМ. `UButton::OnClicked` держит реле `UAsdaButtonRelay` СЛАБО: живёт реле, только пока лежит в UPROPERTY-якоре владельца.
//   Полный обход 31.10 (96 якорей) нашёл якоря, общие у двух самостоятельных окон: сброс при показе/пересоздании одного окна
//   снимал реле живого другого, и после сборки мусора его кнопки молча не работали («Готово» попапа наград 540 в длинном пакете
//   самотестов). Исправления — ключи `asda.Npc.PopupRelayAnchor`, `asda.UI.RelayAnchorSplit`, `asda.UI.CellGridRebuildOnNewOwner`.
//
// ★ КАК ПРОВЕРЯЕТ. Сценарий «соседнее окно пересоздано/показано, пока это живо» — ТЕМ ЖЕ штатным путём, что в игре (показ окна,
//   `Ensure` окна, которое больше не живо), затем ПРИНУДИТЕЛЬНАЯ сборка мусора (`CollectGarbage`), затем у кнопки проверяется,
//   что в её `OnClicked` остался живой `UAsdaButtonRelay` (`GetAllObjects` — устаревшие слабые записи не возвращает), а у окон
//   подтверждения ещё и нажатие (`OnClicked.Broadcast` — то, что делает `SButton` на клик) доходит до колбэка вызывающего.
//   Каждый раздел — пара: ключ 1 (исправление, кнопка жива) и ключ 0 (прежнее поведение, кнопка мертва — дефект воспроизводится).
//     A. `TWOBUT_MSGBOX` (очередь окон-сообщений) и логин-диалог `MESSAGE2_WIN` (`ShowConfirm`) одновременно, в обоих порядках.
//     B. Окно разговора 570 пересоздано при живых попапах 540/750/2400/599 (`UAsdaNpcDialogSubsystem`).
//     C. Окно развития питомца 43500 пересоздано при живой сетке 43400 (`UAsdaPetEvolutionSubsystem`).
//     D. Окно заточки пересоздано при живом попапе 19100 (`UAsdaItemUpgradeSubsystem`).
//     E. Сетка ячеек (`UAsdaIconCellGrid`) переразмечена на НОВОМ окне — клик-зона на новом окне.
//     F. Парикмахерская: каждое открытие заново размечает карточки (`ApplyGridLayout` → `SetupCards`) — якорь реле не растёт,
//        у каждой привязанной кнопки окна после сборки мусора живое реле (ключ 0 — реле копятся).
//     G. Якорь сам выбрасывает реле уничтоженной кнопки при следующей привязке (`asda.UI.RelayAnchorPrune`; ключ 0 — копит).
//     H. Полоса и гейт «Да» своего окна (`asda.UI.ConfirmGaugeSplit`): TWOBUT с гейтом 1 с, поверх — логин-диалог; через 1,5 с
//        «Да» TWOBUT разблокирована (ключ 0 — общий гейт перехвачен логин-диалогом, «Да» TWOBUT заперта навсегда).
//     I. Ограничитель ввода (`UAsdaTextLimiter`) уничтоженного поля выбрасывается из якоря так же, как реле.
//     J. Перепривязка живой кнопки: `OnClicked.Clear()` и новое реле — прежнее реле той же кнопки выбрасывается из якоря.
//     K. Экземпляр полосы TWOBUT запоминается при показе: ключ `asda.UI.ConfirmGaugeSplit` переключён, пока окно на экране, —
//        «Да» всё равно разблокируется по заполнении.
//   Ключи возвращаются, созданные окна закрываются (их `Ensure` пересоздаст при следующем показе).
//
// Консоль: `asda.SelfTest.RelayAnchors [delay=N] [quit]`. Только офлайн. Строки — «ЯКОРЯ ok/FAIL», итог — «ЯКОРЯ ИТОГ: ok=… fail=…».
#include "CoreMinimal.h"
#include "AsdaDevConsole.h"

#if ASDA_DEV_CONSOLE && !UE_BUILD_SHIPPING

#include "Test/AsdaSelfTestKit.h"
#include "Login/AsdaButtonRelay.h"
#include "Npc/AsdaNpcDialogSubsystem.h"
#include "UI/AsdaHairShopSubsystem.h"
#include "UI/AsdaIconCellGrid.h"
#include "UI/AsdaItemUpgradeSubsystem.h"
#include "UI/AsdaPetEvolutionSubsystem.h"
#include "UI/AsdaUiHelpers.h"
#include "UI/AsdaWindowSubsystem.h"
#include "Blueprint/UserWidget.h"
#include "Blueprint/WidgetTree.h"
#include "Components/Button.h"
#include "Components/EditableText.h"
#include "Engine/GameInstance.h"
#include "Engine/World.h"
#include "HAL/IConsoleManager.h"
#include "TimerManager.h"
#include "UObject/UObjectGlobals.h"

DEFINE_LOG_CATEGORY_STATIC(LogAsdaRelayAnchorTest, Log, All);

namespace AsdaRelayAnchorTest
{
	static const TCHAR* kCmd = TEXT("asda.SelfTest.RelayAnchors");   // имя команды (итог, отказ, возврат ключей)
	static const FName kTwoBut(TEXT("TWOBUT_MSGBOX"));                // игровое окно-подтверждение 1520
	static const FName kMsg2(TEXT("MESSAGE2_WIN"));                   // логин-диалог `ShowConfirm`
	static const TCHAR* kOneButWbp = TEXT("/Game/AsdaAssets/UI/Player_UI/Common/WBP_ONEBUT_MSGBOX.WBP_ONEBUT_MSGBOX_C");   // носитель сетки (E)

	// Счётчики прогона.
	struct FRun
	{
		int32 Ok = 0, Fail = 0;   // пройдено / провалено
		// Одна проверка: печать и счёт.
		void Check(bool b, const FString& What)
		{
			if (b) { ++Ok; UE_LOG(LogAsdaRelayAnchorTest, Log, TEXT("ЯКОРЯ ok   %s"), *What); }
			else   { ++Fail; UE_LOG(LogAsdaRelayAnchorTest, Error, TEXT("ЯКОРЯ FAIL %s"), *What); }
		}
	};

	// Жив ли обработчик кнопки: в `OnClicked` есть неуничтоженное реле (устаревшие слабые записи `GetAllObjects` не отдаёт).
	static bool RelayAlive(const UButton* B)
	{
		if (!B) { return false; }
		for (UObject* O : B->OnClicked.GetAllObjects())
		{
			if (IsValid(O) && O->IsA<UAsdaButtonRelay>()) { return true; }
		}
		return false;
	}

	// Принудительная полная сборка мусора — то, что в игре случается «когда-нибудь».
	static void ForceGC() { CollectGarbage(GARBAGE_COLLECTION_KEEPFLAGS, /*bPerformFullPurge*/true); }

	// Кнопка окна `Win` по имени (nullptr — окна/кнопки нет).
	static UButton* WinButton(UAsdaWindowSubsystem* WM, FName Win, const TCHAR* Name)
	{
		UUserWidget* W = WM ? WM->FindWindow(Win) : nullptr;
		return W ? Cast<UButton>(W->GetWidgetFromName(FName(Name))) : nullptr;
	}

	static const TCHAR* Mark(bool b) { return b ? TEXT("жива") : TEXT("МЕРТВА"); }   // подпись состояния кнопки

	// A. TWOBUT и MESSAGE2 одновременно. bGameFirst — сначала игровое (тогда показ логин-диалога сбрасывал якорь TWOBUT).
	static void CaseConfirm(FRun& R, UAsdaWindowSubsystem* WM, bool bSplit, bool bGameFirst)
	{
		AsdaCVarSave::SetForTest(TEXT("asda.UI.RelayAnchorSplit"), bSplit ? 1 : 0);
		WM->ClearMsgBoxQueue(kCmd);
		if (WM->IsOpen(kMsg2)) { WM->CloseConfirm(false); }
		int32 GameYes = 0, LoginYes = 0;
		auto ShowGame  = [&]() { WM->ShowGameConfirm(TEXT("RelayAnchors: игровое"), [&GameYes](bool b) { GameYes += b ? 1 : 0; }); };
		auto ShowLogin = [&]() { WM->ShowConfirm(TEXT("RelayAnchors: логин"), [&LoginYes](bool b) { LoginYes += b ? 1 : 0; }); };
		if (bGameFirst) { ShowGame(); ShowLogin(); } else { ShowLogin(); ShowGame(); }
		const FString Tag = FString::Printf(TEXT("A ключ %d, первым %s"), bSplit ? 1 : 0, bGameFirst ? TEXT("TWOBUT") : TEXT("MESSAGE2"));
		R.Check(WM->IsOpen(kTwoBut) && WM->IsOpen(kMsg2), Tag + TEXT(": оба окна на экране"));
		ForceGC();
		UButton* TwoYes = WinButton(WM, kTwoBut, TEXT("TWOBUT_BUT00"));
		UButton* LogYes = WinButton(WM, kMsg2, TEXT("MESSAGE2_BUT00"));
		const bool bTwo = RelayAlive(TwoYes), bLog = RelayAlive(LogYes);
		// Показанное ПЕРВЫМ — то, чей якорь снимал показ второго (при общем якоре).
		const bool bFirstAlive = bGameFirst ? bTwo : bLog;
		if (bSplit)
		{
			R.Check(bTwo && bLog, Tag + FString::Printf(TEXT(": после сборки мусора «Да» TWOBUT %s, «Да» MESSAGE2 %s (ждём обе живы)"), Mark(bTwo), Mark(bLog)));
		}
		else
		{
			R.Check(!bFirstAlive, Tag + FString::Printf(TEXT(": контроль — «Да» окна, показанного первым, %s (общий якорь, дефект воспроизводится)"), Mark(bFirstAlive)));
		}
		// Нажатие — то, что делает `SButton` на клик: колбэк вызывающего зовётся, только если реле живо.
		if (TwoYes) { TwoYes->OnClicked.Broadcast(); }
		if (LogYes) { LogYes->OnClicked.Broadcast(); }
		if (bSplit)
		{
			R.Check(GameYes == 1 && LoginYes == 1, Tag + FString::Printf(TEXT(": нажатие «Да» дошло — игровое ×%d, логин ×%d (ждём ×1, ×1)"), GameYes, LoginYes));
		}
		// Убрать хвосты: если реле было мертво, окна остались открыты.
		WM->ClearMsgBoxQueue(kCmd);
		if (WM->IsOpen(kMsg2)) { WM->CloseConfirm(false); }
	}

	// B/C/D. Пересоздание главного окна при живом зависимом. `Recreate` возвращает кнопки зависимых окон; `Close` закрывает
	//   зависимые окна до прогона, чтобы они привязались заново при текущем значении ключа.
	static void CaseRecreate(FRun& R, const TCHAR* What, const TCHAR* Key, bool bOn, const TArray<FName>& DepWindows,
		UAsdaWindowSubsystem* WM, TFunctionRef<TArray<UButton*>()> Recreate, const TArray<FString>& Names)
	{
		AsdaCVarSave::SetForTest(Key, bOn ? 1 : 0);
		for (const FName& Id : DepWindows) { WM->CloseWindow(Id); }
		const TArray<UButton*> Btns = Recreate();
		ForceGC();
		for (int32 i = 0; i < Names.Num(); ++i)
		{
			UButton* B = Btns.IsValidIndex(i) ? Btns[i] : nullptr;
			const FString Tag = FString::Printf(TEXT("%s ключ %d: %s"), What, bOn ? 1 : 0, *Names[i]);
			if (!B) { R.Check(false, Tag + TEXT(" — кнопки нет (окно не создано)")); continue; }
			const bool bAlive = RelayAlive(B);
			R.Check(bOn ? bAlive : !bAlive, Tag + FString::Printf(TEXT(" после пересоздания соседнего окна и сборки мусора — %s%s"), Mark(bAlive),
				bOn ? TEXT("") : TEXT(" (контроль: прежнее поведение, дефект воспроизводится)")));
		}
		for (const FName& Id : DepWindows) { WM->CloseWindow(Id); }
	}

	// Длина UPROPERTY-массива `Name` у объекта `O` (−1 — свойства нет): якоря приватны, читаем отражением.
	static int32 ArrayLen(const UObject* O, const TCHAR* Name)
	{
		const FArrayProperty* P = O ? FindFProperty<FArrayProperty>(O->GetClass(), FName(Name)) : nullptr;
		if (!P) { return -1; }
		FScriptArrayHelper H(P, P->ContainerPtrToValuePtr<void>(O));
		return H.Num();
	}

	// F. Парикмахерская: три открытия подряд; якоря `Relays` + `CardRelays` не растут, привязанные кнопки живы.
	static void CaseHair(FRun& R, UAsdaWindowSubsystem* WM, UAsdaHairShopSubsystem* H, bool bOn)
	{
		AsdaCVarSave::SetForTest(TEXT("asda.UI.RelayAnchorSplit"), bOn ? 1 : 0);
		AsdaCVarSave::SetForTest(TEXT("asda.UI.RelayAnchorPrune"), bOn ? 1 : 0);   // контроль — оба исправления выключены (прочистка тоже снимает рост)
		auto Total = [H]() { return FMath::Max(0, ArrayLen(H, TEXT("Relays"))) + FMath::Max(0, ArrayLen(H, TEXT("CardRelays"))); };
		H->CloseWindow();
		const bool bOpen1 = H->OpenWindow();
		const int32 N1 = Total();
		H->CloseWindow(); H->OpenWindow(); H->CloseWindow();
		const bool bOpen3 = H->OpenWindow();
		const int32 N3 = Total();
		const FString Tag = FString::Printf(TEXT("F ключ %d"), bOn ? 1 : 0);
		R.Check(bOpen1 && bOpen3 && N1 > 0, Tag + FString::Printf(TEXT(": окно 15000 открыто (реле в якорях %d)"), N1));
		R.Check(bOn ? N3 == N1 : N3 > N1, Tag + FString::Printf(TEXT(": после трёх открытий реле в якорях %d → %d%s"), N1, N3,
			bOn ? TEXT(" (ждём без роста)") : TEXT(" (контроль: прежнее поведение — растёт)")));
		if (bOn)
		{
			ForceGC();
			int32 Bound = 0, Dead = 0;
			UUserWidget* Win = WM->FindWindow(FName(TEXT("HAIRSHOP_WIN")));
			if (Win && Win->WidgetTree)
			{
				TArray<UWidget*> All; Win->WidgetTree->GetAllWidgets(All);
				for (UWidget* X : All)
				{
					const UButton* B = Cast<UButton>(X);
					if (!B || !B->OnClicked.IsBound()) { continue; }
					++Bound;
					Dead += RelayAlive(B) ? 0 : 1;
				}
			}
			R.Check(Bound > 0 && Dead == 0, Tag + FString::Printf(TEXT(": после сборки мусора привязанных кнопок %d, без живого реле %d"), Bound, Dead));
		}
		H->CloseWindow();
	}

	// G. Две привязки в один якорь; кнопка первой уничтожена сборкой мусора между ними.
	static void CasePrune(FRun& R, UGameInstance* GI, bool bOn)
	{
		AsdaCVarSave::SetForTest(TEXT("asda.UI.RelayAnchorPrune"), bOn ? 1 : 0);
		TArray<TObjectPtr<UAsdaButtonRelay>> Anchor;   // якорь теста: реле держим корнем (массив не UPROPERTY)
		UButton* Gone = NewObject<UButton>(GetTransientPackage());
		UButton* Kept = NewObject<UButton>(GetTransientPackage());
		Kept->AddToRoot();
		UAsdaButtonRelay* R1 = AsdaUi::BindButton(GI, Gone, 1, [](int32) {}, Anchor);
		if (R1) { R1->AddToRoot(); }
		ForceGC();   // `Gone` никем не держится — уничтожен, его реле мертво
		UAsdaButtonRelay* R2 = AsdaUi::BindButton(GI, Kept, 2, [](int32) {}, Anchor);
		if (R2) { R2->AddToRoot(); }
		const bool bDead = R1 && R1->HasDeadBinding();
		R.Check(bDead, FString::Printf(TEXT("G ключ %d: кнопка первой привязки уничтожена — реле помечено мёртвым"), bOn ? 1 : 0));
		R.Check(bOn ? Anchor.Num() == 1 : Anchor.Num() == 2, FString::Printf(TEXT("G ключ %d: в якоре после второй привязки реле %d (%s)"),
			bOn ? 1 : 0, Anchor.Num(), bOn ? TEXT("ждём 1 — мёртвое выброшено") : TEXT("контроль: прежнее — копится, ждём 2")));
		if (R1) { R1->RemoveFromRoot(); }
		if (R2) { R2->RemoveFromRoot(); }
		Kept->RemoveFromRoot();
	}

	// I. Ограничитель ввода: поле уничтожено сборкой мусора между двумя привязками.
	static void CaseLimiter(FRun& R, bool bOn)
	{
		AsdaCVarSave::SetForTest(TEXT("asda.UI.RelayAnchorPrune"), bOn ? 1 : 0);
		TArray<TObjectPtr<UObject>> Anchor;
		UEditableText* Gone = NewObject<UEditableText>(GetTransientPackage());
		UEditableText* Kept = NewObject<UEditableText>(GetTransientPackage());
		Kept->AddToRoot();
		UAsdaTextLimiter* L1 = NewObject<UAsdaTextLimiter>(GetTransientPackage());
		L1->AddToRoot();
		Gone->OnTextChanged.AddDynamic(L1, &UAsdaTextLimiter::OnTextChanged);
		AsdaUi::AnchorRelay(Anchor, L1, Gone);
		ForceGC();
		UAsdaTextLimiter* L2 = NewObject<UAsdaTextLimiter>(GetTransientPackage());
		L2->AddToRoot();
		Kept->OnTextChanged.AddDynamic(L2, &UAsdaTextLimiter::OnTextChanged);
		AsdaUi::AnchorRelay(Anchor, L2, Kept);
		R.Check(bOn ? Anchor.Num() == 1 : Anchor.Num() == 2, FString::Printf(TEXT("I ключ %d: ограничитель уничтоженного поля — в якоре %d (%s)"),
			bOn ? 1 : 0, Anchor.Num(), bOn ? TEXT("ждём 1 — выброшен") : TEXT("контроль: прежнее — копится, ждём 2")));
		L1->RemoveFromRoot(); L2->RemoveFromRoot(); Kept->RemoveFromRoot();
	}

	// J. Перепривязка живой кнопки после `OnClicked.Clear()`.
	static void CaseRebind(FRun& R, UGameInstance* GI, bool bOn)
	{
		AsdaCVarSave::SetForTest(TEXT("asda.UI.RelayAnchorPrune"), bOn ? 1 : 0);
		TArray<TObjectPtr<UAsdaButtonRelay>> Anchor;
		UButton* B = NewObject<UButton>(GetTransientPackage());
		B->AddToRoot();
		int32 Calls1 = 0, Calls2 = 0;
		UAsdaButtonRelay* R1 = AsdaUi::BindButton(GI, B, 1, [&Calls1](int32) { ++Calls1; }, Anchor);
		if (R1) { R1->AddToRoot(); }
		B->OnClicked.Clear();
		UAsdaButtonRelay* R2 = AsdaUi::BindButton(GI, B, 2, [&Calls2](int32) { ++Calls2; }, Anchor);
		if (R2) { R2->AddToRoot(); }
		B->OnClicked.Broadcast();
		R.Check(Calls1 == 0 && Calls2 == 1, FString::Printf(TEXT("J ключ %d: клик дошёл только до нового реле (старое ×%d, новое ×%d)"), bOn ? 1 : 0, Calls1, Calls2));
		R.Check(bOn ? Anchor.Num() == 1 : Anchor.Num() == 2, FString::Printf(TEXT("J ключ %d: после перепривязки той же кнопки в якоре %d (%s)"),
			bOn ? 1 : 0, Anchor.Num(), bOn ? TEXT("ждём 1 — прежнее выброшено") : TEXT("контроль: прежнее — копится, ждём 2")));
		if (R1) { R1->RemoveFromRoot(); }
		if (R2) { R2->RemoveFromRoot(); }
		B->RemoveFromRoot();
	}

	// K. TWOBUT с гейтом 1 с при ключе 1; пока окно на экране, ключ переключается в 0; через 1,5 с «Да» разблокирована.
	static void CaseGaugeLatch(TSharedRef<FRun> R, UWorld* W, TFunction<void()> Next)
	{
		UAsdaWindowSubsystem* WM = W && W->GetGameInstance() ? W->GetGameInstance()->GetSubsystem<UAsdaWindowSubsystem>() : nullptr;
		if (!WM) { R->Check(false, TEXT("K нет оконной подсистемы")); Next(); return; }
		AsdaCVarSave::SetForTest(TEXT("asda.UI.ConfirmGaugeSplit"), 1);
		WM->ClearMsgBoxQueue(kCmd);
		WM->ShowGameConfirm(TEXT("RelayAnchors: запоминание полосы"), [](bool) {}, /*GaugeGateSec*/1.0f);
		AsdaCVarSave::SetForTest(TEXT("asda.UI.ConfirmGaugeSplit"), 0);   // переключили, пока окно на экране
		TWeakObjectPtr<UButton> WeakYes(WinButton(WM, kTwoBut, TEXT("TWOBUT_BUT00")));
		TWeakObjectPtr<UAsdaWindowSubsystem> WeakWM(WM);
		FTimerHandle H;
		W->GetTimerManager().SetTimer(H, FTimerDelegate::CreateLambda([R, WeakYes, WeakWM, Next]()
		{
			const bool bEnabled = WeakYes.IsValid() && WeakYes->GetIsEnabled();
			R->Check(bEnabled, FString::Printf(TEXT("K ключ переключён при открытом TWOBUT: через 1,5 с «Да» %s (полоса запомнена при показе)"),
				bEnabled ? TEXT("разблокирована") : TEXT("ЗАПЕРТА")));
			if (UAsdaWindowSubsystem* M = WeakWM.Get()) { M->ClearMsgBoxQueue(kCmd); }
			Next();
		}), 1.5f, false);
	}

	// H. TWOBUT с гейтом «Да» на 1 с, поверх — логин-диалог; проверка через 1,5 с (таймер мира), затем `Next`.
	static void CaseGauge(TSharedRef<FRun> R, UWorld* W, bool bOn, TFunction<void()> Next)
	{
		UAsdaWindowSubsystem* WM = W && W->GetGameInstance() ? W->GetGameInstance()->GetSubsystem<UAsdaWindowSubsystem>() : nullptr;
		if (!WM) { R->Check(false, TEXT("H нет оконной подсистемы")); Next(); return; }
		AsdaCVarSave::SetForTest(TEXT("asda.UI.ConfirmGaugeSplit"), bOn ? 1 : 0);
		WM->ClearMsgBoxQueue(kCmd);
		if (WM->IsOpen(kMsg2)) { WM->CloseConfirm(false); }
		WM->ShowGameConfirm(TEXT("RelayAnchors: гейт TWOBUT"), [](bool) {}, /*GaugeGateSec*/1.0f);
		UButton* TwoYes = WinButton(WM, kTwoBut, TEXT("TWOBUT_BUT00"));
		R->Check(TwoYes && !TwoYes->GetIsEnabled(), FString::Printf(TEXT("H ключ %d: «Да» TWOBUT заперта до заполнения полосы"), bOn ? 1 : 0));
		WM->ShowConfirm(TEXT("RelayAnchors: логин поверх"), [](bool) {});
		TWeakObjectPtr<UButton> WeakYes(TwoYes);
		TWeakObjectPtr<UAsdaWindowSubsystem> WeakWM(WM);
		FTimerHandle H;
		W->GetTimerManager().SetTimer(H, FTimerDelegate::CreateLambda([R, bOn, WeakYes, WeakWM, Next]()
		{
			const bool bEnabled = WeakYes.IsValid() && WeakYes->GetIsEnabled();
			R->Check(bOn ? bEnabled : !bEnabled, FString::Printf(TEXT("H ключ %d: через 1,5 с при логин-диалоге поверх «Да» TWOBUT %s%s"), bOn ? 1 : 0,
				bEnabled ? TEXT("разблокирована") : TEXT("ЗАПЕРТА"), bOn ? TEXT("") : TEXT(" (контроль: общий гейт перехвачен — дефект воспроизводится)")));
			if (UAsdaWindowSubsystem* M = WeakWM.Get())
			{
				M->ClearMsgBoxQueue(kCmd);
				if (M->IsOpen(kMsg2)) { M->CloseConfirm(false); }
			}
			Next();
		}), 1.5f, false);
	}

	static void Run(UWorld* W, bool bQuit)
	{
		TSharedRef<FRun> RunPtr = MakeShared<FRun>();
		FRun& R = *RunPtr;
		UGameInstance* GI = W ? W->GetGameInstance() : nullptr;
		UAsdaWindowSubsystem* WM = GI ? GI->GetSubsystem<UAsdaWindowSubsystem>() : nullptr;
		UAsdaNpcDialogSubsystem* Dlg = GI ? GI->GetSubsystem<UAsdaNpcDialogSubsystem>() : nullptr;
		UAsdaPetEvolutionSubsystem* Evo = GI ? GI->GetSubsystem<UAsdaPetEvolutionSubsystem>() : nullptr;
		UAsdaItemUpgradeSubsystem* Upg = GI ? GI->GetSubsystem<UAsdaItemUpgradeSubsystem>() : nullptr;
		UAsdaHairShopSubsystem* Hair = GI ? GI->GetSubsystem<UAsdaHairShopSubsystem>() : nullptr;
		if (!WM || !Dlg || !Evo || !Upg || !Hair) { AsdaSelfTestKit::Refused(W, kCmd, bQuit, TEXT("нет подсистем окон")); return; }
		FAsdaCVarScope Keys(W, kCmd);   // ключи вернутся на выходе

		// ── A ──
		for (const bool bSplit : { true, false })
		{
			CaseConfirm(R, WM, bSplit, /*bGameFirst*/true);
			CaseConfirm(R, WM, bSplit, /*bGameFirst*/false);
		}
		AsdaCVarSave::SetForTest(TEXT("asda.UI.RelayAnchorSplit"), 1);

		// ── B: окно разговора 570 при живых попапах ──
		const TArray<FName> NpcPopups = { FName(TEXT("EPISODEPOP_WIN")), FName(TEXT("QUEST_Confirm_Win")), FName(TEXT("MULTYQUEST_WIN")), FName(TEXT("QUEST_INFO_WIN")) };
		const TArray<FString> NpcNames = { TEXT("«ОК» 567 попапа наград 540"), TEXT("«ОК» 780 окна приёма 750"), TEXT("крестик 2411 облачка 2400"), TEXT("крестик 586 подсказки 599") };
		for (const bool bOn : { true, false })
		{
			CaseRecreate(R, TEXT("B"), TEXT("asda.Npc.PopupRelayAnchor"), bOn, NpcPopups, WM,
				[Dlg]() { return Dlg->DebugRecreateTalkKeepPopups(); }, NpcNames);
		}
		AsdaCVarSave::SetForTest(TEXT("asda.Npc.PopupRelayAnchor"), 1);

		// ── C: окно развития 43500 при живой сетке 43400 ──
		for (const bool bOn : { true, false })
		{
			CaseRecreate(R, TEXT("C"), TEXT("asda.UI.RelayAnchorSplit"), bOn, { FName(TEXT("Pet_Inven_Win")) }, WM,
				[Evo]() { return TArray<UButton*>{ Evo->DebugRecreateEvoKeepGrid() }; }, { TEXT("крестик 43439 сетки 43400") });
		}
		WM->CloseWindow(FName(TEXT("Pet_Evolution_Win")));

		// ── D: окно заточки при живом попапе 19100 ──
		for (const bool bOn : { true, false })
		{
			CaseRecreate(R, TEXT("D"), TEXT("asda.UI.RelayAnchorSplit"), bOn, { FName(TEXT("Get_UpgradeOtion")) }, WM,
				[Upg]() { return TArray<UButton*>{ Upg->DebugRecreateWindowKeepPopup() }; }, { TEXT("«ОК» 19106 попапа 19100") });
		}
		WM->CloseWindow(FName(TEXT("Item_Upgrade_Win")));
		AsdaCVarSave::SetForTest(TEXT("asda.UI.RelayAnchorSplit"), 1);

		// ── E: сетка ячеек переразмечена на новом окне ──
		if (UClass* Cls = LoadClass<UUserWidget>(nullptr, kOneButWbp))
		{
			for (const bool bOn : { true, false })
			{
				AsdaCVarSave::SetForTest(TEXT("asda.UI.CellGridRebuildOnNewOwner"), bOn ? 1 : 0);
				UUserWidget* A = CreateWidget<UUserWidget>(W, Cls);
				UUserWidget* B = CreateWidget<UUserWidget>(W, Cls);
				UAsdaIconCellGrid* G = NewObject<UAsdaIconCellGrid>(GI);
				const TArray<FName> Cells = { FName(TEXT("ONEBUT_TEXT")) };
				G->SetupNamed(A, Cells, true, [](int32) { return 0; }, [](int32) {});
				const UButton* HitA = G->GetHitButton(0);
				G->SetupNamed(B, Cells, true, [](int32) { return 0; }, [](int32) {});
				const UButton* HitB = G->GetHitButton(0);
				const bool bOnNew = HitB && HitB->GetTypedOuter<UUserWidget>() == B;
				R.Check(HitA && HitA->GetTypedOuter<UUserWidget>() == A, FString::Printf(TEXT("E ключ %d: клик-зона на первом окне построена"), bOn ? 1 : 0));
				R.Check(bOn ? bOnNew : !bOnNew, FString::Printf(TEXT("E ключ %d: после Setup с новым окном клик-зона %s%s"), bOn ? 1 : 0,
					bOnNew ? TEXT("на НОВОМ окне") : TEXT("осталась на старом"), bOn ? TEXT("") : TEXT(" (контроль: прежнее поведение)")));
			}
		}
		else { R.Check(false, TEXT("E нет класса окна-носителя WBP_ONEBUT_MSGBOX")); }

		// ── F: парикмахерская ──
		CaseHair(R, WM, Hair, /*bOn*/true);
		CaseHair(R, WM, Hair, /*bOn*/false);
		AsdaCVarSave::SetForTest(TEXT("asda.UI.RelayAnchorPrune"), 1);

		// ── G: якорь выбрасывает мёртвые реле ──
		CasePrune(R, GI, /*bOn*/true);
		CasePrune(R, GI, /*bOn*/false);
		// ── I, J ──
		CaseLimiter(R, true); CaseLimiter(R, false);
		CaseRebind(R, GI, true); CaseRebind(R, GI, false);
		AsdaCVarSave::SetForTest(TEXT("asda.UI.RelayAnchorPrune"), 1);
		Keys.Restore();

		// ── H (асинхронно): полоса и гейт своего окна; ключи этого раздела — свой сторож ──
		TSharedRef<FAsdaCVarScope> GaugeKeys = MakeShared<FAsdaCVarScope>(W, kCmd);
		TWeakObjectPtr<UWorld> WW = W;
		CaseGauge(RunPtr, W, /*bOn*/true, [RunPtr, WW, bQuit, GaugeKeys]()
		{
			UWorld* W2 = WW.Get();
			if (!W2) { return; }
			CaseGauge(RunPtr, W2, /*bOn*/false, [RunPtr, WW, bQuit, GaugeKeys]()
			{
				UWorld* W3 = WW.Get();
				if (!W3) { return; }
				CaseGaugeLatch(RunPtr, W3, [RunPtr, WW, bQuit, GaugeKeys]()
				{
					GaugeKeys->Restore();
					AsdaSelfTestKit::Finish(WW.Get(), kCmd, bQuit, LogAsdaRelayAnchorTest, TEXT("ЯКОРЯ"), RunPtr->Ok, RunPtr->Fail);
				});
			});
		});
	}
}

static FAutoConsoleCommandWithWorldAndArgs GAsdaSelfTestRelayAnchors(
	TEXT("asda.SelfTest.RelayAnchors"),
	TEXT("Самотест: у кнопок каждого окна свой GC-якорь реле — TWOBUT+MESSAGE2 одновременно, окно разговора 570 при попапах, ")
	TEXT("развитие питомца 43500 при сетке, заточка при попапе, сетка ячеек на новом окне; принудительная сборка мусора; ключ 0 — контроль. ")
	TEXT("Офлайн. `delay=N` `quit`."),
	FConsoleCommandWithWorldAndArgsDelegate::CreateLambda([](const TArray<FString>& Args, UWorld* W)
	{
		const FAsdaSelfTestArgs A = FAsdaSelfTestArgs::Parse(Args);
		if (AsdaSelfTestKit::RefuseStart(W, AsdaRelayAnchorTest::kCmd, A)) { return; }
		if (!W) { return; }
		if (A.Delay <= 0.f) { AsdaRelayAnchorTest::Run(W, A.bQuit); return; }
		TWeakObjectPtr<UWorld> WW = W;
		const bool bQuit = A.bQuit;
		FTimerHandle H;
		W->GetTimerManager().SetTimer(H, FTimerDelegate::CreateLambda([WW, bQuit]()
		{
			if (WW.Get() && AsdaSelfTestKit::RefuseDeferred(WW.Get(), AsdaRelayAnchorTest::kCmd, bQuit)) { return; }
			if (UWorld* Wd = WW.Get()) { AsdaRelayAnchorTest::Run(Wd, bQuit); }
		}), A.Delay, false);
	}));

#endif // ASDA_DEV_CONSOLE && !UE_BUILD_SHIPPING
