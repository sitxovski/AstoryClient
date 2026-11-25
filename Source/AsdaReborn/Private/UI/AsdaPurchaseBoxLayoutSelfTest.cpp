// Copyright (c) 2026 @sitxovski (Discord)
// ★ САМОТЕСТ «КНОПКИ ОКНА ПОКУПКИ 1559 У РАМКИ» (25.10.2026): `asda.SelfTest.PurchaseBoxButtons [shot] [quit]`.
//   Баг игроков «Purchase and create character buttons are misplaced» (владелец: «съехали кнопки, это к окну покупки
//   причесок»): окно подтверждения 1559 внизу, а кнопки 1561/1562 «Купить/Отмена» — отдельно у верха экрана.
//   [ОРИГИНАЛ] кнопки — дети окна 1559 (.JUI `gamemain_jui.ini`: PURCHASE_BTN01 Origin 38,−38; PURCHASE_BTN02 149,−38;
//   AlignYPos 8 = от низа родителя), эталон ставит только окно (ui_core.c:55961) — кнопки всегда у его нижнего края.
//   Причина и исправление — у ключа `asda.PremiumMall.BoxBtnFollowFrame` (`AsdaPurchaseBoxSubsystem.cpp`).
//
//   ПУТЬ: тот же `UAsdaPurchaseBoxSubsystem::Open`, что зовут парикмахерская (режим E), премиум-магазин (A/B/D) и бустеры;
//   геометрия — ЖИВАЯ (`AsdaUi::WidgetAbsRect` = кэш Slate после отрисовки, закон `ui-check-game-path-geometry`).
//   Последовательность режимов повторяет игрока: E (первая причёска) → B (покупка в кэш-магазине) → E → D → A → E.
//   Между шагами — НАСТОЯЩЕЕ перетаскивание окна игроком за ручку (`SimulateDrag`), после него снова смена режима.
//   В каждом шаге: узел-кнопка `PURCHASE_BTNxx` (UButton = дочерний .JUI-узел (−1,3) 100×23 контрола (38|149, −38) 98×28)
//   стоит на рамка + 37 / + 148 по X, его верх = низ рамки − 35 (в единицах разметки: делим на масштаб рамки = её
//   ширина / 290); обе кнопки на экране.
//   `shot` — снимок кадра на шаге 3 (E после B — сценарий игрока) в Reports/hair_buttons/1559_<язык>_<экран>_key<k>.png.
//   `legacy` — прогон с ключом `asda.PremiumMall.BoxBtnFollowFrame 0` (прежнее поведение, сверка «до»); ключ возвращается.
//   Итог: «КНОПКИ-1559 ИТОГ: ok=N fail=M».
#include "CoreMinimal.h"
#include "AsdaDevConsole.h"

#if ASDA_DEV_CONSOLE && !UE_BUILD_SHIPPING

#include "UI/AsdaPurchaseBoxSubsystem.h"
#include "Login/AsdaLoginGameMode.h"   // второй самотест — экран создания персонажа (`RunCreateNameSelfTest`)
#include "UI/AsdaWindowSubsystem.h"
#include "Test/AsdaSelfTestNet.h"   // ★ R1: тонкий доступ самотестов к сети (вместо AsdaNetSubsystem.h)
#include "UI/AsdaUiHelpers.h"
#include "Text/AsdaLang.h"
#include "Test/AsdaSelfTestKit.h"
#include "Blueprint/UserWidget.h"
#include "Blueprint/WidgetTree.h"
#include "Components/CanvasPanel.h"
#include "Components/CanvasPanelSlot.h"
#include "Components/Widget.h"
#include "Containers/Ticker.h"
#include "Engine/GameInstance.h"
#include "Engine/GameViewportClient.h"
#include "Engine/World.h"
#include "HAL/IConsoleManager.h"
#include "HAL/PlatformTime.h"
#include "Misc/Paths.h"
#include "UnrealClient.h"   // FScreenshotRequest — снимок кадра (`shot`)

DEFINE_LOG_CATEGORY_STATIC(LogAsdaBoxBtnTest, Log, All);

namespace AsdaPurchaseBoxBtnSelfTest
{
	// Имя команды (строка итога, отказ, возврат ключей).
	static const TCHAR* kCmd = TEXT("asda.SelfTest.PurchaseBoxButtons");
	// Идентификатор окна в реестре окон.
	static const FName kWinId(TEXT("SHELLITEM_PURCHASE_BOX"));
	// Оракул из .JUI (не из боевого кода): ширина окна 1559 и места кнопок от левого/нижнего края рамки.
	static constexpr float kJuiWinW = 290.f;
	//   Контрол 1561: Origin (38, −38) от низа окна; его кнопка-узел — (−1, 3) ⇒ X 37, верх = низ − 35. 1562: X 149 − 1 = 148.
	static constexpr float kJuiOkX = 37.f, kJuiCancelX = 148.f, kJuiBtnFromBottom = 35.f;
	// Допуск, единиц разметки (округление пикселей при масштабе экрана).
	static constexpr float kTol = 2.f;
	// Пауза после показа до замера (кэш геометрии Slate обновляется после отрисовки), с.
	static constexpr double kSettleSec = 0.4;
	// Шаг с снимком (E после B — сценарий игрока).
	static constexpr int32 kShotStep = 2;

	// Шаг прогона: режим окна (None — перетащить открытое окно за ручку) и подпись для журнала.
	struct FStep { EAsdaPurchaseMode Mode; const TCHAR* Label; };
	static const FStep kSteps[] = {
		{ EAsdaPurchaseMode::HairShop,   TEXT("1 E парикмахерская (первый показ)") },
		{ EAsdaPurchaseMode::CashFunc,   TEXT("2 B кэш-магазин") },
		{ EAsdaPurchaseMode::HairShop,   TEXT("3 E парикмахерская после кэш-магазина") },
		{ EAsdaPurchaseMode::HairInShop, TEXT("4 D причёска в кэш-магазине") },
		{ EAsdaPurchaseMode::CashEquip,  TEXT("5 A кэш-магазин, снаряжение") },
		{ EAsdaPurchaseMode::HairShop,   TEXT("6 E парикмахерская ещё раз") },
		{ EAsdaPurchaseMode::None,       TEXT("7 игрок перетащил окно вниз-влево") },
		{ EAsdaPurchaseMode::CashFunc,   TEXT("8 B кэш-магазин после перетаскивания") },
		{ EAsdaPurchaseMode::HairShop,   TEXT("9 E парикмахерская после перетаскивания") },
	};

	// Состояние прогона (живёт до итога; шаги — на тиках).
	struct FRun
	{
		TWeakObjectPtr<UWorld> World;          // мир самотеста
		TWeakObjectPtr<UGameInstance> GI;      // экземпляр игры
		bool bQuit = false;                    // выйти после итога
		bool bShot = false;                    // снимок кадра на шаге `kShotStep`
		int32 Ok = 0, Fail = 0;                // счёт проверок
		int32 Step = 0;                        // текущий шаг
		int32 Phase = 0;                       // 0 — открыть, 1 — ждать и мерить, 2 — ждать запись снимка
		double PhaseStart = 0.0;               // начало фазы
		FString Res;                           // метка экрана «ШxВ»
		FVector2D LastFrameMn = FVector2D(-1.0, -1.0);   // левый-верх рамки на прошлом замере (проверка, что перетаскивание сдвинуло окно)
		bool bCaptureBusyAtStart = false;      // перехват отправок взведён до прогона (не наш)
		int32 SavedKey = -1;                   // `legacy`: значение ключа до прогона (вернуть в `Done`), −1 — не меняли
		FTSTicker::FDelegateHandle Tick;       // тик шагов
	};
	static TSharedPtr<FRun> GRun;

	// Проверка с записью в журнал.
	static void Expect(FRun& R, bool bOk, const FString& What, const FString& Detail = FString())
	{
		bOk ? ++R.Ok : ++R.Fail;
		if (bOk) { UE_LOG(LogAsdaBoxBtnTest, Log, TEXT("КНОПКИ-1559 ok   [%s] %s %s"), *R.Res, *What, *Detail); }
		else     { UE_LOG(LogAsdaBoxBtnTest, Warning, TEXT("КНОПКИ-1559 FAIL [%s] %s %s"), *R.Res, *What, *Detail); }
	}

	// Слот узла строкой: анкор, выравнивание, смещения (диагностика расхождения).
	static FString SlotStr(const UWidget* W)
	{
		const UCanvasPanelSlot* S = W ? AsdaUi::SlotOf(W) : nullptr;
		if (!S) { return TEXT("(нет слота)"); }
		const FAnchors A = S->GetAnchors();
		const FMargin O = S->GetOffsets();
		return FString::Printf(TEXT("анкор (%.3f,%.3f)-(%.3f,%.3f) смещ (%.1f,%.1f,%.1f,%.1f)"),
			A.Minimum.X, A.Minimum.Y, A.Maximum.X, A.Maximum.Y, O.Left, O.Top, O.Right, O.Bottom);
	}

	// Замер шага: рамка (9 планов корня) и обе кнопки по живой геометрии.
	static void Measure(FRun& R, UUserWidget* Win, const TCHAR* Label, bool bAfterDrag)
	{
		if (!Win || !Win->WidgetTree) { Expect(R, false, Label, TEXT("окна 1559 нет в реестре")); return; }
		UCanvasPanel* Root = Cast<UCanvasPanel>(Win->WidgetTree->RootWidget);
		FVector2D FrMn(FLT_MAX, FLT_MAX), FrMx(-FLT_MAX, -FLT_MAX);
		int32 Planes = 0;
		for (int32 i = 0; Root && i < Root->GetChildrenCount(); ++i)
		{
			const UWidget* Ch = Root->GetChildAt(i);
			if (!Ch || !Ch->GetName().StartsWith(TEXT("SHELLITEM_PURCHASE_BOX_0_")) || Ch->GetName().EndsWith(TEXT("_DRAG"))) { continue; }
			if (!Ch->IsVisible()) { continue; }
			FVector2D Mn, Mx;
			if (!AsdaUi::WidgetAbsRect(Ch, Mn, Mx)) { continue; }
			FrMn = FVector2D(FMath::Min(FrMn.X, Mn.X), FMath::Min(FrMn.Y, Mn.Y));
			FrMx = FVector2D(FMath::Max(FrMx.X, Mx.X), FMath::Max(FrMx.Y, Mx.Y));
			++Planes;
		}
		const UWidget* Ok = AsdaUi::FindNodeExact(Win, TEXT("PURCHASE_BTN01"));
		const UWidget* Cn = AsdaUi::FindNodeExact(Win, TEXT("PURCHASE_BTN02"));
		FVector2D OkMn(0.0, 0.0), OkMx(0.0, 0.0), CnMn(0.0, 0.0), CnMx(0.0, 0.0);
		const bool bOkG = Ok && AsdaUi::WidgetAbsRect(Ok, OkMn, OkMx);
		const bool bCnG = Cn && AsdaUi::WidgetAbsRect(Cn, CnMn, CnMx);
		if (Planes == 0 || !bOkG || !bCnG)
		{
			Expect(R, false, Label, FString::Printf(TEXT("нет геометрии: планов рамки %d, «Купить» %s, «Отмена» %s"),
				Planes, bOkG ? TEXT("есть") : TEXT("НЕТ"), bCnG ? TEXT("есть") : TEXT("НЕТ")));
			return;
		}
		if (bAfterDrag)
		{
			const FVector2D D = FrMn - R.LastFrameMn;
			Expect(R, D.Size() >= 50.0, FString::Printf(TEXT("%s: рамка сдвинулась перетаскиванием"), Label),
				FString::Printf(TEXT("(сдвиг %.0f,%.0f px)"), D.X, D.Y));
		}
		R.LastFrameMn = FrMn;
		const float S = (FrMx.X - FrMn.X) / kJuiWinW;   // масштаб: px Slate на единицу разметки
		const float OkX = (OkMn.X - FrMn.X) / S, CnX = (CnMn.X - FrMn.X) / S;
		const float OkB = (FrMx.Y - OkMn.Y) / S, CnB = (FrMx.Y - CnMn.Y) / S;
		const FString Geo = FString::Printf(TEXT("(рамка (%.0f,%.0f)-(%.0f,%.0f) масштаб %.3f; «Купить» x=%.1f от низа %.1f; «Отмена» x=%.1f от низа %.1f)"),
			FrMn.X, FrMn.Y, FrMx.X, FrMx.Y, S, OkX, OkB, CnX, CnB);
		UE_LOG(LogAsdaBoxBtnTest, Log, TEXT("КНОПКИ-1559 слот «Купить» %s; группа %s; рамка[0] %s"), *SlotStr(Ok),
			*SlotStr(AsdaUi::FindNodeExact(Win, TEXT("PURCHASE_BTN01_Base_grp"))),
			*SlotStr(AsdaUi::FindNodeByPrefix(Win, TEXT("SHELLITEM_PURCHASE_BOX_0_0"))));
		Expect(R, FMath::Abs(OkX - kJuiOkX) <= kTol && FMath::Abs(CnX - kJuiCancelX) <= kTol,
			FString::Printf(TEXT("%s: кнопки по X от левого края рамки = 37 / 148 (.JUI)"), Label), Geo);
		Expect(R, FMath::Abs(OkB - kJuiBtnFromBottom) <= kTol && FMath::Abs(CnB - kJuiBtnFromBottom) <= kTol,
			FString::Printf(TEXT("%s: верх кнопок = низ рамки − 35 (.JUI AlignYPos 8)"), Label), Geo);
		// На экране целиком (вьюпорт в абсолютных координатах — по геометрии корня окна).
		const FGeometry& RootG = Win->GetCachedGeometry();
		const FVector2D VMn = RootG.GetAbsolutePosition(), VMx = VMn + RootG.GetAbsoluteSize();
		const bool bIn = OkMn.X >= VMn.X - 1 && CnMx.X <= VMx.X + 1 && OkMn.Y >= VMn.Y - 1 && OkMx.Y <= VMx.Y + 1;
		Expect(R, bIn, FString::Printf(TEXT("%s: кнопки на экране"), Label),
			FString::Printf(TEXT("(экран (%.0f,%.0f)-(%.0f,%.0f))"), VMn.X, VMn.Y, VMx.X, VMx.Y));
	}

	// Ключ исправления (самотест дев-only: прямой поиск по имени допустим).
	static IConsoleVariable* KeyVar() { return IConsoleManager::Get().FindConsoleVariable(TEXT("asda.PremiumMall.BoxBtnFollowFrame")); }

	// Конец прогона: закрыть окно, итог, вернуть ключ, выход.
	static bool Done()
	{
		FRun& R = *GRun;
		if (UGameInstance* GI = R.GI.Get())
		{
			if (UAsdaPurchaseBoxSubsystem* Box = GI->GetSubsystem<UAsdaPurchaseBoxSubsystem>()) { Box->Close(); }
			// Окно `legacy`-прогона осталось с «чужими» смещениями — снести, следующий показ создаст его заново.
			if (UAsdaWindowSubsystem* WM = GI->GetSubsystem<UAsdaWindowSubsystem>()) { if (R.SavedKey >= 0) { WM->CloseWindow(kWinId); } }
		}
		// Самотест перехват не ставит; если он оказался взведён за прогон — это чужой код на пути окна (утечка пакета).
		if (UGameInstance* GI = R.GI.Get())
		{
			const UAsdaNetSubsystem* Net = AsdaTestNet::Get(GI);
			if (Net && !R.bCaptureBusyAtStart)
			{
				Expect(R, !AsdaSelfTestKit::IsCaptureBusy(Net), TEXT("перехват отправок не взведён за прогон"));
			}
		}
		const int32 KeyNow = KeyVar() ? KeyVar()->GetInt() : -1;
		if (R.SavedKey >= 0 && KeyVar()) { KeyVar()->Set(R.SavedKey, ECVF_SetByConsole); }
		AsdaSelfTestKit::Finish(R.World.Get(), kCmd, R.bQuit, LogAsdaBoxBtnTest, TEXT("КНОПКИ-1559"), R.Ok, R.Fail,
			*FString::Printf(TEXT(" [%s, ключ BoxBtnFollowFrame=%d]"), *R.Res, KeyNow));
		GRun.Reset();
		return false;
	}

	// Тик шагов: открыть → выждать → замер (→ снимок) → следующий.
	static bool OnTick(float)
	{
		if (!GRun.IsValid()) { return false; }
		FRun& R = *GRun;
		UGameInstance* GI = R.GI.Get();
		UAsdaPurchaseBoxSubsystem* Box = GI ? GI->GetSubsystem<UAsdaPurchaseBoxSubsystem>() : nullptr;
		UAsdaWindowSubsystem* WM = GI ? GI->GetSubsystem<UAsdaWindowSubsystem>() : nullptr;
		if (!Box || !WM) { ++R.Fail; return Done(); }
		const double Now = FPlatformTime::Seconds();
		if (R.Step >= (int32)UE_ARRAY_COUNT(kSteps)) { return Done(); }
		const FStep& St = kSteps[R.Step];
		switch (R.Phase)
		{
			case 0:
			{
				if (St.Mode == EAsdaPurchaseMode::None)
				{
					// Перетаскивание путём игрока: нажать на ручке окна, повести, отпустить (`SimulateDrag`).
					FVector2D HMn, HMx;
					if (!WM->DebugGetDragHandleRect(kWinId, 0, HMn, HMx))
					{
						Expect(R, false, FString::Printf(TEXT("%s: ручка перетаскивания есть"), St.Label));
						++R.Step; return true;
					}
					const FVector2D From((HMn.X + HMx.X) * 0.5, HMn.Y + 8.0);
					WM->SimulateDrag(From, From + FVector2D(-120.0, 150.0), /*HoldFrames*/ 2);
					R.Phase = 3; R.PhaseStart = Now;
					return true;
				}
				Box->Close();   // как игрок: прежняя покупка закрыта, следующая открывает окно заново
				FAsdaPurchaseBoxRequest Q;
				Q.Mode = St.Mode;
				Q.NameOverride = (St.Mode == EAsdaPurchaseMode::HairShop || St.Mode == EAsdaPurchaseMode::HairInShop)
					? TEXT("SelfTest Hair") : TEXT("SelfTest Item");
				Q.PriceOverride = 19;
				const bool bOpen = Box->Open(Q, [](bool) {});
				Expect(R, bOpen && Box->IsOpen(), FString::Printf(TEXT("%s: окно открыто"), St.Label));
				R.Phase = 1; R.PhaseStart = Now;
				return true;
			}
			case 1:
				if (Now - R.PhaseStart < kSettleSec) { return true; }
				Measure(R, WM->FindWindow(kWinId), St.Label, St.Mode == EAsdaPurchaseMode::None);
				if (R.bShot && R.Step == kShotStep)
				{
					const int32 Key = KeyVar() ? KeyVar()->GetInt() : -1;
					const FString Path = FPaths::ConvertRelativePathToFull(FPaths::ProjectDir() / FString::Printf(
						TEXT("../Reports/hair_buttons/1559_%s_%s_key%d.png"), FAsdaLang::Code(FAsdaLang::Current()), *R.Res, Key));
					FScreenshotRequest::RequestScreenshot(Path, /*bShowUI*/true, /*bAddFilenameSuffix*/false);
					UE_LOG(LogAsdaBoxBtnTest, Log, TEXT("КНОПКИ-1559 снимок: %s"), *Path);
					R.Phase = 2; R.PhaseStart = Now;
					return true;
				}
				++R.Step; R.Phase = 0;
				return true;
			case 2:
				if (Now - R.PhaseStart < 1.5) { return true; }   // дать кадру записаться
				++R.Step; R.Phase = 0;
				return true;
			case 3:
				// Перетаскивание доставлено (отпускание прошло) → выждать отрисовку и мерить, как обычный шаг.
				if (WM->IsSimClickPending()) { return true; }
				R.Phase = 1; R.PhaseStart = Now;
				return true;
			default:
				return Done();
		}
	}

	// Закрыть окно покупки и снести его виджет из реестра окон.
	static void Box_CloseAndDrop(UGameInstance* GI)
	{
		if (UAsdaPurchaseBoxSubsystem* Box = GI ? GI->GetSubsystem<UAsdaPurchaseBoxSubsystem>() : nullptr) { Box->Close(); }
		if (UAsdaWindowSubsystem* WM = GI ? GI->GetSubsystem<UAsdaWindowSubsystem>() : nullptr) { WM->CloseWindow(kWinId); }
	}

	// Запуск: офлайн-гейт, подсистемы, метка экрана, тик. `legacy` — с ключом 0 (сверка «до»).
	static void Run(const TArray<FString>& A, UWorld* W)
	{
		const FAsdaSelfTestArgs Args = FAsdaSelfTestArgs::Parse(A);
		if (AsdaSelfTestKit::RefuseStart(W, kCmd, Args)) { return; }
		UGameInstance* GI = W ? W->GetGameInstance() : nullptr;
		if (!GI || !GI->GetSubsystem<UAsdaPurchaseBoxSubsystem>() || !GI->GetSubsystem<UAsdaWindowSubsystem>())
		{
			AsdaSelfTestKit::Refused(W, kCmd, Args.bQuit, TEXT("нет подсистемы окна покупки или окон"));
			return;
		}
		if (GRun.IsValid()) { AsdaSelfTestKit::Refused(W, kCmd, Args.bQuit, TEXT("самотест уже идёт")); return; }
		GRun = MakeShared<FRun>();
		FRun& R = *GRun;
		R.World = W; R.GI = GI; R.bQuit = Args.bQuit; R.bShot = Args.Has(TEXT("shot"));
		if (const UAsdaNetSubsystem* Net = AsdaTestNet::Get(GI)) { R.bCaptureBusyAtStart = AsdaSelfTestKit::IsCaptureBusy(Net); }
		// Свежее окно: снести прежнее (следующий `Open` создаст его заново — как первый показ в сессии).
		Box_CloseAndDrop(GI);
		// `legacy` — прогон с ключом 0 (прежнее поведение) для сверки «до»; ключ возвращается в `Done`.
		if (Args.Has(TEXT("legacy")) && KeyVar()) { R.SavedKey = KeyVar()->GetInt(); KeyVar()->Set(0, ECVF_SetByConsole); }
		FVector2D V(0.0, 0.0);
		if (UGameViewportClient* VC = W->GetGameViewport()) { VC->GetViewportSize(V); }
		R.Res = FString::Printf(TEXT("%.0fx%.0f"), V.X, V.Y);
		UE_LOG(LogAsdaBoxBtnTest, Log, TEXT("КНОПКИ-1559 старт: экран %s, язык %s, ключ BoxBtnFollowFrame=%d"), *R.Res,
			FAsdaLang::Code(FAsdaLang::Current()), KeyVar() ? KeyVar()->GetInt() : -1);
		R.PhaseStart = FPlatformTime::Seconds();
		R.Tick = FTSTicker::GetCoreTicker().AddTicker(FTickerDelegate::CreateStatic(&OnTick), 0.05f);
	}
}

// ★ ВТОРАЯ ЧАСТЬ ЖАЛОБЫ — «create character buttons»: `asda.SelfTest.CharCreateButtons [shot] [quit]` (карта логина, офлайн).
//   Открывает экран создания штатным путём (`AAsdaLoginGameMode::RunCreateNameSelfTest` с недопустимым именем — пакет не уходит),
//   ждёт отрисовку и сверяет ЖИВУЮ геометрию кнопок с .JUI `login_jui.ini` (все четыре — дети CARAMAKE_WIN с AlignPos 5,
//   то есть их ВЗАИМНОЕ положение в единицах разметки фиксировано): OKBUT (7,520) 123×25, CANCELBUT (7,550), BACKBUT (−11,355)
//   14×27, NEXTBUT (351,355). Плюс: все на экране. Только чтение — ассеты и Login.umap не трогаются.
//   `shot` — снимок в Reports/hair_buttons/caramake_<язык>_<экран>.png. Итог: «КНОПКИ-СОЗДАНИЕ ИТОГ: ok=N fail=M».

namespace AsdaCharCreateBtnSelfTest
{
	// Имя команды.
	static const TCHAR* kCmd = TEXT("asda.SelfTest.CharCreateButtons");
	// Пауза после открытия экрана до замера, с.
	static constexpr double kSettleSec = 2.0;

	// Состояние прогона.
	struct FRun
	{
		TWeakObjectPtr<UWorld> World;          // мир (карта логина)
		TWeakObjectPtr<UGameInstance> GI;      // экземпляр игры
		bool bQuit = false, bShot = false;     // выход после итога; снимок
		int32 Ok = 0, Fail = 0;                // счёт
		int32 Phase = 0;                       // 0 — ждать и мерить, 1 — ждать запись снимка
		double PhaseStart = 0.0;               // начало фазы
		FString Res;                           // метка экрана
	};
	static TSharedPtr<FRun> GRun;

	// Проверка с записью в журнал.
	static void Expect(FRun& R, bool bOk, const FString& What, const FString& Detail = FString())
	{
		bOk ? ++R.Ok : ++R.Fail;
		if (bOk) { UE_LOG(LogAsdaBoxBtnTest, Log, TEXT("КНОПКИ-СОЗДАНИЕ ok   [%s] %s %s"), *R.Res, *What, *Detail); }
		else     { UE_LOG(LogAsdaBoxBtnTest, Warning, TEXT("КНОПКИ-СОЗДАНИЕ FAIL [%s] %s %s"), *R.Res, *What, *Detail); }
	}

	// Тик: замер → (снимок) → итог.
	static bool OnTick(float)
	{
		if (!GRun.IsValid()) { return false; }
		FRun& R = *GRun;
		const double Now = FPlatformTime::Seconds();
		auto Finish = [&R]()
		{
			AsdaSelfTestKit::Finish(R.World.Get(), kCmd, R.bQuit, LogAsdaBoxBtnTest, TEXT("КНОПКИ-СОЗДАНИЕ"), R.Ok, R.Fail,
				*FString::Printf(TEXT(" [%s]"), *R.Res));
			GRun.Reset();
			return false;
		};
		if (R.Phase == 1) { return (Now - R.PhaseStart < 1.5) ? true : Finish(); }
		if (Now - R.PhaseStart < kSettleSec) { return true; }
		UGameInstance* GI = R.GI.Get();
		UAsdaWindowSubsystem* WM = GI ? GI->GetSubsystem<UAsdaWindowSubsystem>() : nullptr;
		UUserWidget* Win = WM ? WM->FindWindow(FName(TEXT("CARAMAKE_WIN"))) : nullptr;
		if (!Win) { Expect(R, false, TEXT("окно CARAMAKE_WIN открыто")); return Finish(); }
		struct FBtn { const TCHAR* Name; float X, Y, W; FVector2D Mn, Mx; bool bG; };
		FBtn B[] = { { TEXT("CARAMAKE_OKBUT"), 7.f, 520.f, 123.f }, { TEXT("CARAMAKE_CANCELBUT"), 7.f, 550.f, 123.f },
		             { TEXT("CARAMAKE_BACKBUT"), -11.f, 355.f, 14.f }, { TEXT("CARAMAKE_NEXTBUT"), 351.f, 355.f, 14.f } };
		const FGeometry& RootG = Win->GetCachedGeometry();
		const FVector2D VMn = RootG.GetAbsolutePosition(), VMx = VMn + RootG.GetAbsoluteSize();
		bool bAll = true;
		for (FBtn& X : B)
		{
			const UWidget* N = AsdaUi::FindNodeExact(Win, X.Name);
			X.bG = N && AsdaUi::WidgetAbsRect(N, X.Mn, X.Mx);
			bAll &= X.bG;
			if (X.bG)
			{
				const bool bIn = X.Mn.X >= VMn.X - 1 && X.Mx.X <= VMx.X + 1 && X.Mn.Y >= VMn.Y - 1 && X.Mx.Y <= VMx.Y + 1;
				Expect(R, bIn, FString::Printf(TEXT("%s на экране"), X.Name),
					FString::Printf(TEXT("((%.0f,%.0f)-(%.0f,%.0f), экран (%.0f,%.0f)-(%.0f,%.0f))"), X.Mn.X, X.Mn.Y, X.Mx.X, X.Mx.Y, VMn.X, VMn.Y, VMx.X, VMx.Y));
			}
			else { Expect(R, false, FString::Printf(TEXT("%s: есть геометрия"), X.Name)); }
		}
		if (bAll)
		{
			const float S = (B[0].Mx.X - B[0].Mn.X) / B[0].W;   // масштаб по ширине «Создать»
			int32 Bad = 0; FString Det;
			for (int32 i = 1; i < 4; ++i)
			{
				const float Dx = (B[i].Mn.X - B[0].Mn.X) / S, Dy = (B[i].Mn.Y - B[0].Mn.Y) / S;
				const float Ex = B[i].X - B[0].X, Ey = B[i].Y - B[0].Y;
				const bool bOk = FMath::Abs(Dx - Ex) <= 2.f && FMath::Abs(Dy - Ey) <= 2.f;
				Bad += bOk ? 0 : 1;
				Det += FString::Printf(TEXT(" %s: (%.1f,%.1f) ждём (%.0f,%.0f)%s;"), B[i].Name, Dx, Dy, Ex, Ey, bOk ? TEXT("") : TEXT(" ✗"));
			}
			Expect(R, Bad == 0, TEXT("взаимное положение кнопок = .JUI (от «Создать», единицы разметки)"),
				FString::Printf(TEXT("(масштаб %.3f;%s)"), S, *Det));
		}
		if (R.bShot)
		{
			const FString Path = FPaths::ConvertRelativePathToFull(FPaths::ProjectDir() / FString::Printf(
				TEXT("../Reports/hair_buttons/caramake_%s_%s.png"), FAsdaLang::Code(FAsdaLang::Current()), *R.Res));
			FScreenshotRequest::RequestScreenshot(Path, /*bShowUI*/true, /*bAddFilenameSuffix*/false);
			UE_LOG(LogAsdaBoxBtnTest, Log, TEXT("КНОПКИ-СОЗДАНИЕ снимок: %s"), *Path);
			R.Phase = 1; R.PhaseStart = Now;
			return true;
		}
		return Finish();
	}

	// Запуск: офлайн-гейт, режим логина, открыть создание, тик.
	static void Run(const TArray<FString>& A, UWorld* W)
	{
		const FAsdaSelfTestArgs Args = FAsdaSelfTestArgs::Parse(A);
		if (AsdaSelfTestKit::RefuseStart(W, kCmd, Args)) { return; }
		AAsdaLoginGameMode* GM = W ? Cast<AAsdaLoginGameMode>(W->GetAuthGameMode()) : nullptr;
		if (!GM) { AsdaSelfTestKit::Refused(W, kCmd, Args.bQuit, TEXT("нет AAsdaLoginGameMode (нужна карта логина)")); return; }
		if (GRun.IsValid()) { AsdaSelfTestKit::Refused(W, kCmd, Args.bQuit, TEXT("самотест уже идёт")); return; }
		GRun = MakeShared<FRun>();
		FRun& R = *GRun;
		R.World = W; R.GI = W->GetGameInstance(); R.bQuit = Args.bQuit; R.bShot = Args.Has(TEXT("shot"));
		FVector2D V(0.0, 0.0);
		if (UGameViewportClient* VC = W->GetGameViewport()) { VC->GetViewportSize(V); }
		R.Res = FString::Printf(TEXT("%.0fx%.0f"), V.X, V.Y);
		GM->RunCreateNameSelfTest(TEXT("a'b"));   // недопустимое имя: экран создания открыт, пакет 1014 не уходит
		R.PhaseStart = FPlatformTime::Seconds();
		FTSTicker::GetCoreTicker().AddTicker(FTickerDelegate::CreateStatic(&OnTick), 0.1f);
	}
}

static FAutoConsoleCommandWithWorldAndArgs GAsdaSelfTestCharCreateButtons(
	TEXT("asda.SelfTest.CharCreateButtons"),
	TEXT("Самотест (карта логина, офлайн): экран создания персонажа — кнопки Создать/Отмена/стрелки на экране и во взаимном "
	     "положении .JUI, по живой геометрии. `shot` — снимок в Reports/hair_buttons. `quit`."),
	FConsoleCommandWithWorldAndArgsDelegate::CreateLambda([](const TArray<FString>& A, UWorld* W)
	{
		AsdaCharCreateBtnSelfTest::Run(A, W);
	}));

static FAutoConsoleCommandWithWorldAndArgs GAsdaSelfTestPurchaseBoxButtons(
	TEXT("asda.SelfTest.PurchaseBoxButtons"),
	TEXT("Самотест: кнопки «Купить/Отмена» окна покупки 1559 стоят у рамки (38/149 от левого края, низ − 38, как .JUI) при смене "
	     "режимов E→B→E→D→A→E (парикмахерская ↔ кэш-магазин), по живой геометрии. `shot` — снимок шага 3 в Reports/hair_buttons. `legacy` — с ключом 0 (сверка «до»). `quit`."),
	FConsoleCommandWithWorldAndArgsDelegate::CreateLambda([](const TArray<FString>& A, UWorld* W)
	{
		AsdaPurchaseBoxBtnSelfTest::Run(A, W);
	}));

#endif // ASDA_DEV_CONSOLE && !UE_BUILD_SHIPPING
