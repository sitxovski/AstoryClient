// Copyright (c) 2026 @sitxovski (Discord)
// ★★★ САМОТЕСТ СПИСКА «ESC НАЖИМАЕТ КРЕСТИК» — `asda.UI.SelfTestEscExtra [delay=N] [quit]` (07.11.2026, жалоба владельца на окно 56450).
//   Путь игрока: настоящая клавиша ESC (`ProcessKeyDownEvent`/`ProcessKeyUpEvent` на разных кадрах). Каждое окно открывается штатным входом своей подсистемы;
//   окно, которое офлайн не открылось, пишется как ПРОПУСК и провалом не считается. Проверки: окно закрыто, меню 69400 не открылось (нажатие потрачено окном),
//   ключ `asda.UI.EscExtraWindows 0` — окно 56450 остаётся открытым (поведение оригинала). Без сети.
#include "CoreMinimal.h"
#include "Test/AsdaSelfTestCtx.h"   // ★ R5-c (12.11.2026): общая точка самотестов вместо своих копий помощников

#if !UE_BUILD_SHIPPING

#include "UI/AsdaWindowSubsystem.h"
#include "UI/AsdaHuntReportSubsystem.h"
#include "UI/AsdaWarShopSubsystem.h"
#include "UI/AsdaConstellationSubsystem.h"
#include "UI/AsdaHighRefineSubsystem.h"
#include "UI/AsdaWarehouseSubsystem.h"
#include "UI/AsdaExchangeSubsystem.h"
#include "UI/AsdaCraftWindowSubsystem.h"
#include "UI/AsdaBoosterPanelSubsystem.h"
#include "Quest/AsdaBoardQuestSubsystem.h"
#include "Test/AsdaSelfTestKit.h"
#include "UI/AsdaSoulmateSubsystem.h"
#include "Blueprint/UserWidget.h"
#include "Blueprint/WidgetTree.h"
#include "Components/MultiLineEditableText.h"
#include "Components/EditableText.h"
#include "UnrealClient.h"
#include "World/AsdaWorldSubsystem.h"
#include "Engine/Engine.h"
#include "Engine/GameInstance.h"
#include "Engine/World.h"
#include "Framework/Application/SlateApplication.h"
#include "HAL/IConsoleManager.h"
#include "TimerManager.h"

DEFINE_LOG_CATEGORY_STATIC(LogAsdaEscExtra, Log, All);   // журнал самотеста

namespace AsdaEscExtraLocal
{
	static const TCHAR* kCmd = TEXT("asda.UI.SelfTestEscExtra");   // имя команды
	static const FName kMenu(TEXT("SYSTEM_MenuWIN_NEW"));          // ESC-меню 69400

	// Окно прогона: id и штатный вход.
	struct FRow
	{
		FName Id;                                // окно
		TFunction<void(UGameInstance*)> Open;    // открыть
	};

	// Состояние прогона.
	struct FCtx
	{
		TWeakObjectPtr<UWorld> W;      // мир
		bool bQuit = false;            // выйти после итога
		int32 Ok = 0, Fail = 0, Skip = 0;   // счётчики
		TArray<FRow> Rows;             // окна
		int32 Idx = 0;                 // текущее окно

		// Проверка: счётчик и строка журнала.
		void Expect(bool b, const FString& M)
		{
			b ? ++Ok : ++Fail;
			UE_LOG(LogAsdaEscExtra, Display, TEXT("[ESC-список] %s %s"), b ? TEXT("OK  ") : TEXT("FAIL"), *M);
		}
		UGameInstance* GI() const { return W.IsValid() ? W->GetGameInstance() : nullptr; }
		UAsdaWindowSubsystem* WM() const { UGameInstance* G = GI(); return G ? G->GetSubsystem<UAsdaWindowSubsystem>() : nullptr; }
	};
	using FCtxRef = TSharedRef<FCtx>;

	// Следующий этап через Sec секунд.
	using AsdaSelfTestCtx::ByStage::After;   // ★ R5-c (12.11.2026): общая точка (`Test/AsdaSelfTestCtx.h`) — копия отличалась только тем, что проверка мира записана `if (W) { … }` вместо раннего выхода.

	// Настоящая клавиша ESC: нажатие.
	static void EscDownKeepFocus() { FSlateApplication::Get().ProcessKeyDownEvent(FKeyEvent(EKeys::Escape, FModifierKeysState(), 0, false, 27, 27)); }
	// Настоящая клавиша ESC с фокусом на игре.
	static void EscDown() { FSlateApplication::Get().SetAllUserFocusToGameViewport(); FSlateApplication::Get().ProcessKeyDownEvent(FKeyEvent(EKeys::Escape, FModifierKeysState(), 0, false, 27, 27)); }
	// Настоящая клавиша ESC: отпускание.
	static void EscUp() { FSlateApplication::Get().ProcessKeyUpEvent(FKeyEvent(EKeys::Escape, FModifierKeysState(), 0, false, 27, 27)); }

	// Значение ключа списка.
	static void SetKey(int32 V)
	{
		if (IConsoleVariable* K = IConsoleManager::Get().FindConsoleVariable(TEXT("asda.UI.EscExtraWindows"))) { K->Set(V, ECVF_SetByConsole); }
	}

	// Итог.
	static void Finish(FCtxRef C)
	{
		SetKey(1);
		UE_LOG(LogAsdaEscExtra, Display, TEXT("[ESC-список] пропущено окон (офлайн не открылись): %d"), C->Skip);
		AsdaSelfTestKit::Finish(C->W.Get(), kCmd, C->bQuit, LogAsdaEscExtra, TEXT("SelfTestEscExtra"), C->Ok, C->Fail);
	}

	// Контроль ключа 0: окно осталось открытым.
	static void StageKeyOffCheck(FCtxRef C)
	{
		EscUp();
		UAsdaHuntReportSubsystem* H = C->GI() ? C->GI()->GetSubsystem<UAsdaHuntReportSubsystem>() : nullptr;
		const bool bMenu = C->WM() && C->WM()->IsWindowVisible(kMenu);
		C->Expect(H && H->IsOpen() && bMenu, FString::Printf(TEXT("ключ 0: окно 56450 по ESC не закрылось (как оригинал), клавиша дошла: меню 69400 открыто=%d"), bMenu ? 1 : 0));
		if (H) { H->CloseWindow(); }
		if (C->WM() && C->WM()->IsWindowVisible(kMenu)) { C->WM()->CloseTopmostOnEsc(); }
		Finish(C);
	}
	// Контроль ключа 0: ESC.
	static void StageKeyOffEsc(FCtxRef C)
	{
		EscDown();
		After(C, 0.2f, &StageKeyOffCheck);
	}

	static void StageKeyOffStart(FCtxRef C);

	// Поле 8302 окна 8300.
	static FString IntroText(FCtxRef C)
	{
		UUserWidget* Win = C->WM() ? C->WM()->FindWindow(FName(TEXT("My_Introduction_Win"))) : nullptr;
		UWidget* F = Win ? Win->GetWidgetFromName(TEXT("My_Intro_EditBox")) : nullptr;
		if (const UMultiLineEditableText* M = Cast<UMultiLineEditableText>(F)) { return M->GetText().ToString(); }
		if (const UEditableText* E = Cast<UEditableText>(F)) { return E->GetText().ToString(); }
		return F ? FString::Printf(TEXT("(тип поля %s)"), *F->GetClass()->GetName()) : FString(TEXT("(нет поля)"));
	}
	// Шаги пары 8200/8300 настоящей клавишей.
	static TArray<TFunction<void(FCtxRef)>> GPair;
	static int32 GPairIdx = 0;
	static void PairStep(FCtxRef C)
	{
		if (GPairIdx >= GPair.Num()) { GPair.Reset(); StageKeyOffStart(C); return; }
		GPair[GPairIdx++](C);
		After(C, 0.35f, &PairStep);
	}
	// Открыть 8200 и 8300, ввести знак настоящей клавишей.
	static void AddPairOpen()
	{
		GPair.Add([](FCtxRef C) { C->GI()->GetSubsystem<UAsdaSoulmateSubsystem>()->OpenSearchWindow(); });
		GPair.Add([](FCtxRef C) { C->GI()->GetSubsystem<UAsdaSoulmateSubsystem>()->OpenIntroWindow(); });
		GPair.Add([](FCtxRef) {});
		GPair.Add([](FCtxRef C)
		{
			FSlateApplication::Get().ProcessKeyCharEvent(FCharacterEvent(TEXT('Q'), FModifierKeysState(), 0, false));
		});
		GPair.Add([](FCtxRef C)
		{
			const FString Txt = IntroText(C);
			C->Expect(Txt.Contains(TEXT("Q")) && C->GI()->GetSubsystem<UAsdaSoulmateSubsystem>()->IsIntroEditFocused(),
				FString::Printf(TEXT("поле 8302: настоящий ввод, текст «%s»"), *Txt));
			EscDownKeepFocus();
		});
		GPair.Add([](FCtxRef) { EscUp(); });
	}
	// Состояние пары после ESC.
	static void AddPairCheck(bool bIntro, bool bSearch, bool bFocus, const TCHAR* Tag)
	{
		const FString T(Tag);
		GPair.Add([bIntro, bSearch, bFocus, T](FCtxRef C)
		{
			UAsdaWindowSubsystem* WM = C->WM();
			UAsdaSoulmateSubsystem* So = C->GI()->GetSubsystem<UAsdaSoulmateSubsystem>();
			const bool bI = WM->IsWindowVisible(FName(TEXT("My_Introduction_Win"))), bS = So->IsSearchWindowShown(), bF = So->IsIntroEditFocused(), bM = WM->IsWindowVisible(kMenu);
			const bool bText = !bI || IntroText(C).Contains(TEXT("Q"));
			C->Expect(bI == bIntro && bS == bSearch && bF == bFocus && !bM && bText,
				FString::Printf(TEXT("%s: 8300=%d 8200=%d фокус=%d меню=%d текст цел=%d"), *T, bI ? 1 : 0, bS ? 1 : 0, bF ? 1 : 0, bM ? 1 : 0, bText ? 1 : 0));
		});
	}
	// Пара настоящей клавишей: ключ 1, затем ключ 0.
	static void StagePair(FCtxRef C)
	{
		GPair.Reset(); GPairIdx = 0;
		AddPairOpen();
		AddPairCheck(true, true, false, TEXT("ключ 1, клавиша ESC с фокусом в 8302: фокус снят, окна на месте"));
		GPair.Add([](FCtxRef) { EscDown(); });
		GPair.Add([](FCtxRef) { EscUp(); });
		AddPairCheck(false, true, false, TEXT("ключ 1, клавиша ESC: закрыто верхнее 8300"));
		GPair.Add([](FCtxRef) { EscDown(); });
		GPair.Add([](FCtxRef) { EscUp(); });
		AddPairCheck(false, false, false, TEXT("ключ 1, клавиша ESC: закрыто 8200"));
		GPair.Add([](FCtxRef) { SetKey(0); });
		AddPairOpen();
		AddPairCheck(true, false, false, TEXT("ключ 0 (оригинал), клавиша ESC с фокусом в 8302: 8200 закрыто, 8300 с текстом осталось"));
		GPair.Add([](FCtxRef C) { C->GI()->GetSubsystem<UAsdaSoulmateSubsystem>()->CloseIntroWindow(); SetKey(1); });
		PairStep(C);
	}

	static void StageNext(FCtxRef C);
	// Этап: после ESC окно закрыто, меню нет.
	static void StageCheck(FCtxRef C)
	{
		EscUp();
		UAsdaWindowSubsystem* WM = C->WM();
		const FRow& R = C->Rows[C->Idx];
		const bool bMenu = WM && WM->IsWindowVisible(kMenu);
		C->Expect(WM && !WM->IsWindowVisible(R.Id) && !bMenu, FString::Printf(TEXT("%s: ESC закрыл окно, меню 69400 открыто=%d"), *R.Id.ToString(), bMenu ? 1 : 0));
		if (bMenu) { WM->CloseTopmostOnEsc(); }
		++C->Idx;
		After(C, 0.3f, &StageNext);
	}
	// Этап: окно открыто? ESC.
	static void StagePress(FCtxRef C)
	{
		UAsdaWindowSubsystem* WM = C->WM();
		const FRow& R = C->Rows[C->Idx];
		if (!WM || !WM->IsWindowVisible(R.Id))
		{
			++C->Skip;
			UE_LOG(LogAsdaEscExtra, Display, TEXT("[ESC-список] ПРОПУСК %s: офлайн не открылось"), *R.Id.ToString());
			++C->Idx;
			After(C, 0.1f, &StageNext);
			return;
		}
		EscDown();
		After(C, 0.2f, &StageCheck);
	}
	// Этап: открыть очередное окно.
	static void StageNext(FCtxRef C)
	{
		if (C->Idx >= C->Rows.Num()) { StagePair(C); return; }
		C->Rows[C->Idx].Open(C->GI());
		After(C, 0.7f, &StagePress);
	}

	// Контроль ключа 0 на окне 56450.
	static void StageKeyOffStart(FCtxRef C)
	{
		SetKey(0);
		if (UAsdaHuntReportSubsystem* H = C->GI() ? C->GI()->GetSubsystem<UAsdaHuntReportSubsystem>() : nullptr) { H->OpenWindow(); }
		After(C, 0.5f, &StageKeyOffEsc);
	}

	// Окна, которые открываются штатным входом подсистемы.
	static void Run(FCtxRef C)
	{
		C->Rows = {
			{ FName(TEXT("Hunt_report_Win")),     [](UGameInstance* G) { G->GetSubsystem<UAsdaHuntReportSubsystem>()->OpenWindow(); } },
			{ FName(TEXT("HONOR_itemmall_Win")),  [](UGameInstance* G) { G->GetSubsystem<UAsdaWarShopSubsystem>()->OpenWindow(); } },
			{ FName(TEXT("Constellation_S_WIN")), [](UGameInstance* G) { G->GetSubsystem<UAsdaConstellationSubsystem>()->OpenWindow(); } },
			{ FName(TEXT("Smelting3_Win")),       [](UGameInstance* G) { G->GetSubsystem<UAsdaHighRefineSubsystem>()->OpenWindow(); } },
			{ FName(TEXT("WARE_WIN")),            [](UGameInstance* G) { G->GetSubsystem<UAsdaWarehouseSubsystem>()->OpenWindow(UAsdaWarehouseSubsystem::kModeKeep); } },
			{ FName(TEXT("EXCHANGE_WIN")),        [](UGameInstance* G) { G->GetSubsystem<UAsdaExchangeSubsystem>()->OpenMode(1); } },
			{ FName(TEXT("ITEM_making_WIN")),     [](UGameInstance* G) { G->GetSubsystem<UAsdaCraftWindowSubsystem>()->GateAndToggle(TEXT("самотест ESC")); } },
			{ FName(TEXT("BOOSTER_PREV_WIN")),    [](UGameInstance* G) { G->GetSubsystem<UAsdaBoosterPanelSubsystem>()->OpenPrevWindowFrom(75978, EAsdaPrevSource::BoosterShop, true); } },
			{ FName(TEXT("BOOSTING_WIN")),        [](UGameInstance* G) { G->GetSubsystem<UAsdaBoosterPanelSubsystem>()->OpenWindow(75978, 0, 0); } },
			{ FName(TEXT("Subjugate_WIN")),       [](UGameInstance* G)
				{
					UAsdaBoardQuestSubsystem* B = G->GetSubsystem<UAsdaBoardQuestSubsystem>();
					for (int32 Npc = 1; Npc < 1000; ++Npc) { if (B->IsBoardNpc(Npc)) { B->OpenForNpc(Npc); break; } }
				} },
		};
		C->Idx = 0;
		// Свой персонаж офлайн: клавиши опрашиваются только в мире с персонажем, как в `asda.SelfTest.EscChain`.
		UWorld* W = C->W.Get();
		if (W && AsdaSelfTestKit::NeedSelfSpawn(W) && GEngine)
		{
			if (UAsdaWorldSubsystem* WS = W->GetGameInstance()->GetSubsystem<UAsdaWorldSubsystem>())
			{
				GEngine->Exec(W, TEXT("Asda.Load.CurrentMap 3"));
				WS->DebugSpawnSelfAt(256, 256, TEXT("СамТест"));
			}
		}
		After(C, 2.5f, &StageNext);
	}
}

// Проба порядка 8200/8300: `asda.UI.EscOrderProbe [delay=N] [quit]` — z, полоса AlwaysTop и сетка верхних окон по экрану (шаг 8 px) до и после окна 8300, кадр esc_order. Только чтение.
namespace AsdaEscExtraLocal
{
	// Сколько точек экрана ловит каждое из двух окон.
	static void LogCatch(UAsdaWindowSubsystem* WM, const TCHAR* Tag)
	{
		const FName A(TEXT("Find_Soulmate_Win")), B(TEXT("My_Introduction_Win"));
		int32 Na = 0, Nb = 0;
		for (int32 Y = 0; Y < 768; Y += 8) { for (int32 X = 0; X < 1024; X += 8)
		{
			const FName T = WM->FindWindowCatchingAt(FVector2D(X, Y));
			Na += (T == A) ? 1 : 0; Nb += (T == B) ? 1 : 0;
		} }
		UE_LOG(LogAsdaEscExtra, Display, TEXT("[порядок] %s: 8200 видно=%d z=%d AlwaysTop=%d точек=%d; 8300 видно=%d z=%d AlwaysTop=%d точек=%d"), Tag,
			WM->IsWindowVisible(A) ? 1 : 0, WM->GetWindowZOrder(A), WM->IsWindowAlwaysTop(A) ? 1 : 0, Na, WM->IsWindowVisible(B) ? 1 : 0, WM->GetWindowZOrder(B), WM->IsWindowAlwaysTop(B) ? 1 : 0, Nb);
	}
	static void ProbeEnd(FCtxRef C) { AsdaSelfTestKit::Finish(C->W.Get(), TEXT("asda.UI.EscOrderProbe"), C->bQuit, LogAsdaEscExtra, TEXT("EscOrderProbe"), 1, 0); }
	static void ProbeShot(FCtxRef C)
	{
		UAsdaWindowSubsystem* WM = C->WM();
		WM->BringWindowToFront(FName(TEXT("Find_Soulmate_Win")));
		LogCatch(WM, TEXT("после подъёма 8200"));
		FScreenshotRequest::RequestScreenshot(TEXT("esc_order"), /*bShowUI=*/true, /*bAddFilenameSuffix=*/false);
		After(C, 1.5f, &ProbeEnd);
	}
	static void ProbeIntro(FCtxRef C)
	{
		LogCatch(C->WM(), TEXT("только 8200"));
		C->GI()->GetSubsystem<UAsdaSoulmateSubsystem>()->OpenIntroWindow();
		After(C, 0.7f, &ProbeShot);
	}
}

static FAutoConsoleCommandWithWorldAndArgs GAsdaUiEscOrderProbe(
	TEXT("asda.UI.EscOrderProbe"),
	TEXT("asda.UI.EscOrderProbe [delay=N] [quit] — проба порядка окон 8200/8300: z, полоса, точки экрана, кадр esc_order. Только чтение."),
	FConsoleCommandWithWorldAndArgsDelegate::CreateLambda([](const TArray<FString>& A, UWorld* W)
	{
		using namespace AsdaEscExtraLocal;
		const FAsdaSelfTestArgs KitArgs = FAsdaSelfTestArgs::Parse(A);
		if (AsdaSelfTestKit::RefuseStart(W, TEXT("asda.UI.EscOrderProbe"), KitArgs)) { return; }
		if (!W) { return; }
		FCtxRef C = MakeShared<FCtx>();
		C->W = W;
		C->bQuit = KitArgs.bQuit;
		auto Go = [C]()
		{
			C->GI()->GetSubsystem<UAsdaSoulmateSubsystem>()->OpenSearchWindow();
			After(C, 0.7f, &ProbeIntro);
		};
		if (KitArgs.Delay <= 0.f) { Go(); return; }
		FTimerHandle H;
		W->GetTimerManager().SetTimer(H, FTimerDelegate::CreateLambda(Go), KitArgs.Delay, false);
	}));

static FAutoConsoleCommandWithWorldAndArgs GAsdaUiSelfTestEscExtra(
	TEXT("asda.UI.SelfTestEscExtra"),
	TEXT("asda.UI.SelfTestEscExtra [delay=N] [quit] — ОФЛАЙН-самотест 07.11.2026: настоящая клавиша ESC закрывает окна списка порта, меню не открывается; ключ 0 — окно 56450 остаётся."),
	FConsoleCommandWithWorldAndArgsDelegate::CreateLambda([](const TArray<FString>& A, UWorld* W)
	{
		using namespace AsdaEscExtraLocal;
		AsdaCVarSave::ArmWorldGuard(W, kCmd);
		const FAsdaSelfTestArgs KitArgs = FAsdaSelfTestArgs::Parse(A);
		if (AsdaSelfTestKit::RefuseStart(W, kCmd, KitArgs)) { return; }
		if (!W) { return; }
		FCtxRef C = MakeShared<FCtx>();
		C->W = W;
		C->bQuit = KitArgs.bQuit;
		auto Go = [C]()
		{
			if (AsdaSelfTestKit::RefuseDeferred(C->W.Get(), kCmd, C->bQuit)) { return; }
			Run(C);
		};
		if (KitArgs.Delay <= 0.f) { Go(); return; }
		FTimerHandle H;
		W->GetTimerManager().SetTimer(H, FTimerDelegate::CreateLambda(Go), KitArgs.Delay, false);
	}));

#endif // !UE_BUILD_SHIPPING
