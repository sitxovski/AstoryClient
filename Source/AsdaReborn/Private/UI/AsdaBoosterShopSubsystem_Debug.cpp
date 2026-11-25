// Copyright (c) 2026 @sitxovski (Discord)
// AsdaBoosterShopSubsystem_Debug.cpp — ★ R3-c 12.11.2026 (разрез AsdaBoosterShopSubsystem.cpp, дословный перенос): дампы, SelfTest, дев-команды, отладка подарка.
//   Общее для частей — в AsdaBoosterShopSubsystem_Private.h; шапка и реверс — в AsdaBoosterShopSubsystem.cpp.
#include "UI/AsdaBoosterShopSubsystem.h"
#include "AsdaDevConsole.h"   // ★ античит А2: ASDA_DEV_CVAR / ASDA_DEV_CONSOLE — дев-ключи и команды вне Shipping
#include "UI/AsdaJuiWindowHandle.h"   // ★ волна 3 (15.09.2026): каркас окон JUI
#include "UI/AsdaNotice.h"   // ★ волна 1: фасад нотисов (Loc / Chat / MsgBox)

#include "AsdaDataTableCache.h"
#include "UI/AsdaBoosterPanelSubsystem.h"   // ★ окно предпросмотра 15600 — общая точка, уже есть
#include "UI/AsdaCommunityFriendSubsystem.h"   // AsdaCommunityUi::ShownVisibilityFor — общая точка видимости узла
#include "UI/AsdaIconCellGrid.h"
#include "UI/AsdaItemTooltipSubsystem.h"
#include "UI/AsdaJuiComboBox.h"
#include "UI/AsdaPurchaseBoxSubsystem.h"    // ★ общая модалка 1559, режим F
#include "UI/AsdaTipPanel.h"
#include "UI/AsdaUiHelpers.h"
#include "UI/AsdaWindowSubsystem.h"

#include "Blueprint/UserWidget.h"
#include "Blueprint/WidgetTree.h"
#include "Components/Button.h"
#include "Components/EditableText.h"
#include "Components/Image.h"
#include "Components/PanelWidget.h"
#include "Components/TextBlock.h"
#include "Engine/Engine.h"
#include "Engine/GameInstance.h"
#include "Engine/World.h"
#include "GenericPlatform/GenericPlatformProcess.h"
#include "HAL/IConsoleManager.h"
#include "TimerManager.h"
#include "AsdaOutgoingGate.h"               // ★ С-Р1: общий офлайн-гейт отладочных команд (RefuseDebugWhenOnline)
#include "AsdaBoosterShopSubsystem_Private.h"   // ★ R3-c 12.11.2026: общее для частей AsdaBoosterShopSubsystem*.cpp

// ═══════════════════════════════ ДИАГНОСТИКА ═══════════════════════════════════════════════════
FString UAsdaBoosterShopSubsystem::DumpControls() const
{
	int32 CellsOk = 0, NamesOk = 0, StampOk = 0, CatOk = 0, DigitsOk = 0;
	for (const FAsdaBoosterCellUi& C : Cells)
	{
		if (C.ItemIcon) { ++CellsOk; }
		if (C.NameText) { ++NamesOk; }
		if (C.Stamp)    { ++StampOk; }
		for (const TObjectPtr<UWidget>& D : C.Digits) { if (D) { ++DigitsOk; } }
	}
	for (const TObjectPtr<UButton>& B : CatButtons) { if (B) { ++CatOk; } }
	return FString::Printf(
		TEXT("=== BOOSTER_SHOP_WIN 15300 ===\n")
		TEXT("  ячейки: иконок %d/%d · подписей имени %d/%d · плашек скидки %d/%d · цифр %d/%d\n")
		TEXT("  категории 15545: %d/%d (панель %s) · крестик %s · пагинация %s/%s/%s/%s\n")
		TEXT("  GXP %s · пополнить %s · баланс %s · подложка 15599 %s · рамка 15496 %s\n")
		TEXT("  панель 15515: узлов %d · имя %s · вес %s · цена %s · комбо %s/%s/%s · OK %s · Вид %s\n")
		TEXT("  погашено кодом: %d\n"),
		CellsOk, kCells, NamesOk, kCells, StampOk, kCells, DigitsOk, kCells * kDigits,
		CatOk, kCats, CatPanel ? TEXT("есть") : TEXT("НЕТ"),
		BtnClose ? TEXT("есть") : TEXT("НЕТ"),
		BtnNext ? TEXT("+1") : TEXT("—"), BtnBack ? TEXT("−1") : TEXT("—"),
		BtnFirst ? TEXT("нач") : TEXT("—"), BtnLast ? TEXT("кон") : TEXT("—"),
		TxtCash ? TEXT("есть") : TEXT("НЕТ"), BtnCharge ? TEXT("есть") : TEXT("НЕТ"),
		BtnBalance ? TEXT("есть") : TEXT("НЕТ"), Part2 ? TEXT("есть") : TEXT("НЕТ"),
		SelFrame.Num() > 0 ? TEXT("есть") : TEXT("НЕТ"),
		PurTree.Num(), PurItemName ? TEXT("есть") : TEXT("НЕТ"), PurWeight ? TEXT("есть") : TEXT("НЕТ"),
		PurPrice ? TEXT("есть") : TEXT("НЕТ"),
		(ComboJob && ComboJob->IsReady()) ? TEXT("1") : TEXT("—"),
		(ComboLevel && ComboLevel->IsReady()) ? TEXT("2") : TEXT("—"),
		(ComboQty && ComboQty->IsReady()) ? TEXT("3") : TEXT("—"),
		PurOk ? TEXT("есть") : TEXT("НЕТ"), PurPreview ? TEXT("есть") : TEXT("НЕТ"),
		HiddenByCode.Num());
}

FString UAsdaBoosterShopSubsystem::DumpState() const
{
	FString S = FString::Printf(
		TEXT("15300: открыт=%s | категория=%d стр.%d/%d ячеек на стр.=%d товаров B=%d F=%d\n")
		TEXT("       GXP=%d в полёте=%s | панель=%d код=%d (база %d) проф=%d ур=%d кол=%d склад=%s\n")
		TEXT("       покупок=%d ответов=%d выбранная ячейка=%d\n"),
		bOpen ? TEXT("да") : TEXT("нет"), Cat, Page, MaxPage, CellsPerPage(), RowsB.Num(), RowsF.Num(),
		CashGxp, bPending ? TEXT("да") : TEXT("нет"), PurMode, PurItemCode, PurBaseCode,
		PurJob, PurLevel, PurQty, bPurToWarehouse ? TEXT("да") : TEXT("нет"),
		CntBuy, CntResult, SelCell);
	S += FString::Printf(TEXT("       вариантов=%d профессий=%d уровней=%d\n"),
		Variants.Num(), JobList.Num(), LevelList.Num());
	return S;
}

// ★★★ САМОТЕСТ «ПУТЁМ ИГРОКА»: ФАКТИЧЕСКАЯ экранная геометрия ПОСЛЕ отрисовки, видимость, цвет
//   И ПОРЯДОК ДЕТЕЙ КАНВАСА (узел с верными координатами может лежать под плашкой).
FString UAsdaBoosterShopSubsystem::SelfTest() const
{
	FString S = TEXT("=== магазин бустеров 15300 ===\n");
	S += DumpState();
	S += DumpControls();
	if (!Widget) { S += TEXT("  ВИДЖЕТ НЕ СОЗДАН\n"); return S; }

	auto Row = [&S](const TCHAR* Tag, const UWidget* W)
	{
		if (!W) { S += FString::Printf(TEXT("  %-28s НЕТ УЗЛА\n"), Tag); return; }
		const FGeometry& G = W->GetCachedGeometry();
		const FVector2D Abs = G.GetAbsolutePosition();
		const FVector2D Sz  = G.GetLocalSize();
		int32 ZIdx = -1;
		if (const UPanelWidget* P = W->GetParent())
		{
			for (int32 i = 0; i < P->GetChildrenCount(); ++i) { if (P->GetChildAt(i) == W) { ZIdx = i; break; } }
		}
		FString Color = TEXT("—");
		if (const UTextBlock* Tb = Cast<UTextBlock>(W)) { Color = Tb->GetColorAndOpacity().GetSpecifiedColor().ToString(); }
		S += FString::Printf(TEXT("  %-28s поз(%.0f,%.0f) разм(%.0f×%.0f) вид=%d порядок=%d цвет=%s\n"),
			Tag, Abs.X, Abs.Y, Sz.X, Sz.Y, (int32)W->GetVisibility(), ZIdx, *Color);
	};
	// ★ СКОЛЬКО КАРТОЧЕК В КАЖДОЙ ВКЛАДКЕ — главная цифра проверки фильтра категорий.
	{
		static const TCHAR* Names[kCats] = { TEXT("Все"), TEXT("Популярные"), TEXT("Воин"),
			TEXT("Лучник"), TEXT("Маг"), TEXT("Доспех"), TEXT("Инструмент") };
		const int32 PerPage = CellsPerPage();
		S += TEXT("  карточек по вкладкам:\n");
		for (int32 i = 0; i < kCats; ++i)
		{
			const int32 N = CountForCategory(i);
			S += FString::Printf(TEXT("    %d %-12s %3d карточек, страниц %d\n"),
				i, Names[i], N, N > 0 ? (N + PerPage - 1) / PerPage : 1);
		}
	}
	Row(TEXT("15599 подложка"),   Part2);
	for (int32 i = 0; i < PanelPlanes.Num(); ++i)
	{
		Row(*FString::Printf(TEXT("15599 план рамки %d"), i), PanelPlanes[i].Get());
	}
	Row(TEXT("15511 GXP"),        TxtCash);
	Row(TEXT("15510 «Сейчас GXP»"), TxtCashLbl);
	Row(TEXT("15498 страница"),   TxtPage);
	Row(TEXT("15499 всего стр."), TxtPages);
	for (int32 i = 0; i < kCats; ++i)
	{
		Row(*FString::Printf(TEXT("15545 категория %d"), i), CatLabels.IsValidIndex(i) ? CatLabels[i].Get() : nullptr);
	}
	for (int32 i = 0; i < 3 && i < kCells; ++i)
	{
		Row(*FString::Printf(TEXT("ячейка %d имя"), i),   Cells[i].NameText);
		Row(*FString::Printf(TEXT("ячейка %d цена"), i),  Cells[i].PriceText);
		Row(*FString::Printf(TEXT("ячейка %d иконка"), i), Cells[i].ItemIcon);
	}
	// ★ Д-17: ряды панели покупки — подпись и её комбобокс обязаны стоять на ОДНОМ Y.
	// ★★★ КЛИК-ЗОНЫ ИКОНОК: живая геометрия зоны обязана совпасть с живой геометрией иконки.
	//   Если зона уехала (или её нет) — клик по иконке физически не может сработать.
	if (IconGrid)
	{
		S += IconGrid->DumpHitZonesLive(TEXT("иконки витрины"), 3);
		for (int32 i = 0; i < 3 && i < kCells; ++i)
		{
			// ★ R3-c чистка 12.11.2026 (C4701): без геометрии иконки в дамп шли неинициализированные значения — теперь нули.
			FVector2D IMn = FVector2D::ZeroVector, IMx = FVector2D::ZeroVector;
			const bool bIcon = Cells[i].ItemIcon && AsdaUi::WidgetAbsRect(Cells[i].ItemIcon.Get(), IMn, IMx);
			S += FString::Printf(TEXT("    иконка %02d живая: %s(%.0f,%.0f)-(%.0f,%.0f)\n"),
				i, bIcon ? TEXT("") : TEXT("НЕТ ГЕОМЕТРИИ "), IMn.X, IMn.Y, IMx.X, IMx.Y);
		}
	}
	// ★ ЧТО ВИДНО ПОД КНОПКОЙ «ОК» 15527: перечисляем ВСЕ видимые узлы панели, чей живой
	//   прямоугольник пересекается с прямоугольником кнопки — так «фантомная стрелка» называется
	//   сама, без догадок по именам.
	if (PurOk)
	{
		FVector2D BMn, BMx;
		if (AsdaUi::WidgetAbsRect(PurOk.Get(), BMn, BMx) && !(BMx - BMn).IsNearlyZero())
			{
			S += FString::Printf(TEXT("  узлы поверх кнопки 15527 (%.0f,%.0f)-(%.0f,%.0f):\n"),
				BMn.X, BMn.Y, BMx.X, BMx.Y);
			for (const TObjectPtr<UWidget>& W : PurTree)
			{
				if (!W || W->GetVisibility() == ESlateVisibility::Collapsed) { continue; }
				FVector2D Mn, Mx;
				if (!AsdaUi::WidgetAbsRect(W.Get(), Mn, Mx) || (Mx - Mn).IsNearlyZero()) { continue; }
				if (Mx.X < BMn.X || Mn.X > BMx.X || Mx.Y < BMn.Y - 12.f || Mn.Y > BMx.Y + 12.f) { continue; }
				S += FString::Printf(TEXT("    %-34s (%.0f,%.0f)-(%.0f,%.0f) вид=%d\n"),
					*W->GetName(), Mn.X, Mn.Y, Mx.X, Mx.Y, (int32)W->GetVisibility());
			}
		}
	}
	Row(TEXT("15533 иконка товара"), PurIcon);
	Row(TEXT("15527 значок «минус» (Д-19)"), PurOkIcon);
	Row(TEXT("15520 подпись «ОК» (Д-21)"), PurConfirmCap);
	S += FString::Printf(TEXT("  иконка 15533: кисть %s | значок минус: %s | подпись ОК подарка: %s\n"),
		(PurIcon && PurIcon->GetBrush().GetResourceObject()) ? TEXT("ЕСТЬ") : TEXT("НЕТ"),
		PurOkIcon ? (PurOkIcon->GetVisibility() == ESlateVisibility::Collapsed ? TEXT("скрыт") : TEXT("ВИДЕН")) : TEXT("узла нет"),
		PurConfirmCap ? (PurConfirmCap->GetVisibility() == ESlateVisibility::Collapsed ? TEXT("скрыта") : TEXT("ВИДНА")) : TEXT("узла нет"));
	Row(TEXT("15543 TXT4 (ряд 1)"), PurTxt4);
	Row(TEXT("15544 COMBO1 профессия"), AsdaUi::FindNodeByName(Widget, TEXT("BOOSTER_PUR_COMBO1_Text"), nullptr));
	Row(TEXT("15541 TXT5 (ряд 2)"), PurTxt5);
	Row(TEXT("15542 COMBO2 уровень"), AsdaUi::FindNodeByName(Widget, TEXT("BOOSTER_PUR_COMBO2_Text"), nullptr));
	Row(TEXT("15539 TXT6 (ряд 3)"), PurTxt6);
	Row(TEXT("15540 COMBO3 количество"), AsdaUi::FindNodeByName(Widget, TEXT("BOOSTER_PUR_COMBO3_Text"), nullptr));
	S += FString::Printf(TEXT("  тексты рядов: TXT4='%s' TXT5='%s' TXT6='%s'\n"),
		PurTxt4 ? *PurTxt4->GetText().ToString() : TEXT("—"),
		PurTxt5 ? *PurTxt5->GetText().ToString() : TEXT("—"),
		PurTxt6 ? *PurTxt6->GetText().ToString() : TEXT("—"));
	S += FString::Printf(TEXT("  списки: профессии(COMBO1)=%d уровни(COMBO2)=%d кол-во(COMBO3)=7 | выбрано проф=%d ур=%d код=%d\n"),
		JobList.Num(), LevelList.Num(), PurJob, PurLevel, PurItemCode);
	Row(TEXT("15538 имя в панели"), PurItemName);
	Row(TEXT("15522 цена в панели"), PurPrice);
	Row(TEXT("15525 GXP в панели"),  PurCash);
	Row(TEXT("15529 «В хранилище»"), PurWareGrp);
	Row(TEXT("15590 гайд-строка"),   PurGuide0);
	Row(TEXT("15591 гайд-строка"),   PurGuide1);
	return S;
}

namespace {
// ═══════════════════════════════ КОНСОЛЬНЫЕ КОМАНДЫ ═══════════════════════════════════════════
//   ★★★ ПРОВЕРКА — ПУТЁМ ИГРОКА: команды повторяют РОВНО те же входы, что мышь.
//   Самотест общий с моллом: `asda.PremiumMall.SelfTest [задержкаСек] [quit]` печатает и это окно.
	UAsdaBoosterShopSubsystem* Shop(UWorld* W)
	{
		UGameInstance* GI = W ? W->GetGameInstance() : nullptr;
		return GI ? GI->GetSubsystem<UAsdaBoosterShopSubsystem>() : nullptr;
	}
}

// ★ ИСПРАВЛЕНИЕ 11.10.2026 (дедуп №2, В2-A, U13): было `AddOnScreenDebugMessage` (12 с поверх экрана; в Shipping оверлей пуст) — стало общая точка `AsdaNotice::ConsoleEcho`: каждая строка в журнал (Display), непустая — в ленту Tip.
#if !UE_BUILD_SHIPPING   // ★ античит А2 (12.10.2026): отладка самотестов/дев-команд — только вне Shipping
namespace {
	void Echo(UWorld* W, const FString& S) { AsdaNotice::ConsoleEcho(W, S, LogAsdaBoosterShop); }
}
#endif // !UE_BUILD_SHIPPING

#if ASDA_DEV_CONSOLE   // ★ античит А2: дев-команда, в Shipping не регистрируется
static FAutoConsoleCommandWithWorldAndArgs GBoosterOpen(
	TEXT("asda.BoosterShop.Open"),
	TEXT("asda.BoosterShop.Open — открыть магазин бустеров 15300 ТЕМ ЖЕ входом, что кнопка NPC "
	     "(код диалога 30034)"),
	FConsoleCommandWithWorldAndArgsDelegate::CreateStatic([](const TArray<FString>&, UWorld* W)
	{
		if (UAsdaBoosterShopSubsystem* P = Shop(W)) { P->OpenWindow(); Echo(W, P->DumpState()); }
	}));
#endif // ASDA_DEV_CONSOLE

#if ASDA_DEV_CONSOLE   // ★ античит А2: дев-команда, в Shipping не регистрируется
static FAutoConsoleCommandWithWorldAndArgs GBoosterClose(
	TEXT("asda.BoosterShop.Close"),
	TEXT("asda.BoosterShop.Close — закрыть магазин бустеров (крестик 15301)"),
	FConsoleCommandWithWorldAndArgsDelegate::CreateStatic([](const TArray<FString>&, UWorld* W)
	{
		if (UAsdaBoosterShopSubsystem* P = Shop(W)) { P->CloseWindow(); Echo(W, P->DumpState()); }
	}));
#endif // ASDA_DEV_CONSOLE

#if ASDA_DEV_CONSOLE   // ★ античит А2: дев-команда, в Shipping не регистрируется
static FAutoConsoleCommandWithWorldAndArgs GBoosterCat(
	TEXT("asda.BoosterShop.Cat"),
	TEXT("asda.BoosterShop.Cat <0..6> — нажать вкладку категорий 15545 (0 Все, 1 Популярные, "
	     "2 Воин, 3 Лучник, 4 Маг, 5 Доспех, 6 Инструмент)"),
	FConsoleCommandWithWorldAndArgsDelegate::CreateStatic([](const TArray<FString>& A, UWorld* W)
	{
		UAsdaBoosterShopSubsystem* P = Shop(W);
		if (!P || A.Num() < 1) { Echo(W, TEXT("использование: asda.BoosterShop.Cat <0..6>")); return; }
		P->SetCategory(FCString::Atoi(*A[0]));
		Echo(W, P->DumpState());
	}));
#endif // ASDA_DEV_CONSOLE

#if ASDA_DEV_CONSOLE   // ★ античит А2: дев-команда, в Shipping не регистрируется
static FAutoConsoleCommandWithWorldAndArgs GBoosterPage(
	TEXT("asda.BoosterShop.Page"),
	TEXT("asda.BoosterShop.Page <номер> — перейти на страницу (1-based, как счётчик 15498)"),
	FConsoleCommandWithWorldAndArgsDelegate::CreateStatic([](const TArray<FString>& A, UWorld* W)
	{
		UAsdaBoosterShopSubsystem* P = Shop(W);
		if (!P || A.Num() < 1) { Echo(W, TEXT("использование: asda.BoosterShop.Page <номер>")); return; }
		P->SetPage(FCString::Atoi(*A[0]));
		Echo(W, P->DumpState());
	}));
#endif // ASDA_DEV_CONSOLE

#if ASDA_DEV_CONSOLE   // ★ античит А2: дев-команда, в Shipping не регистрируется
static FAutoConsoleCommandWithWorldAndArgs GBoosterBuy(
	TEXT("asda.BoosterShop.Buy"),
	TEXT("asda.BoosterShop.Buy <ячейка 0..20> — нажать «Купить» (15311) у ячейки: откроется "
	     "панель покупки 15515"),
	FConsoleCommandWithWorldAndArgsDelegate::CreateStatic([](const TArray<FString>& A, UWorld* W)
	{
		UAsdaBoosterShopSubsystem* P = Shop(W);
		if (!P || A.Num() < 1) { Echo(W, TEXT("использование: asda.BoosterShop.Buy <0..20>")); return; }
		P->OnCellBuyClicked(FCString::Atoi(*A[0]));
		Echo(W, P->DumpState());
	}));
#endif // ASDA_DEV_CONSOLE

// Клик по ИКОНКЕ карточки — тот же вход, что мышь (сетка `UAsdaIconCellGrid`).
#if ASDA_DEV_CONSOLE   // ★ античит А2: дев-команда, в Shipping не регистрируется
static FAutoConsoleCommandWithWorldAndArgs GBoosterIcon(
	TEXT("asda.BoosterShop.Icon"),
	TEXT("asda.BoosterShop.Icon <ячейка 0..20> — нажать ИКОНКУ карточки (15310 + 9i): рамка выбора "
	     "+ окно предпросмотра 15600 «Вид»"),
	FConsoleCommandWithWorldAndArgsDelegate::CreateStatic([](const TArray<FString>& A, UWorld* W)
	{
		UAsdaBoosterShopSubsystem* P = Shop(W);
		if (!P || A.Num() < 1) { Echo(W, TEXT("использование: asda.BoosterShop.Icon <0..20>")); return; }
		P->OnCellIconClicked(FCString::Atoi(*A[0]));
		Echo(W, P->DumpState());
	}));
#endif // ASDA_DEV_CONSOLE

#if ASDA_DEV_CONSOLE   // ★ античит А2: дев-команда, в Shipping не регистрируется
static FAutoConsoleCommandWithWorldAndArgs GBoosterQty(
	TEXT("asda.BoosterShop.Qty"),
	TEXT("asda.BoosterShop.Qty <1..7> — выбрать количество комбобоксом 15540"),
	FConsoleCommandWithWorldAndArgsDelegate::CreateStatic([](const TArray<FString>& A, UWorld* W)
	{
		UAsdaBoosterShopSubsystem* P = Shop(W);
		if (!P || A.Num() < 1) { Echo(W, TEXT("использование: asda.BoosterShop.Qty <1..7>")); return; }
		P->SetQuantity(FCString::Atoi(*A[0]));
		Echo(W, P->DumpState());
	}));
#endif // ASDA_DEV_CONSOLE

#if ASDA_DEV_CONSOLE   // ★ античит А2: дев-команда, в Shipping не регистрируется
static FAutoConsoleCommandWithWorldAndArgs GBoosterWare(
	TEXT("asda.BoosterShop.Ware"),
	TEXT("asda.BoosterShop.Ware <0|1> — взвести чекбокс «В хранилище» 15528 (байт 2 тела 5070)"),
	FConsoleCommandWithWorldAndArgsDelegate::CreateStatic([](const TArray<FString>& A, UWorld* W)
	{
		UAsdaBoosterShopSubsystem* P = Shop(W);
		if (!P || A.Num() < 1) { Echo(W, TEXT("использование: asda.BoosterShop.Ware <0|1>")); return; }
		P->SetToWarehouse(FCString::Atoi(*A[0]) != 0);
		Echo(W, P->DumpState());
	}));
#endif // ASDA_DEV_CONSOLE

#if ASDA_DEV_CONSOLE   // ★ античит А2: дев-команда, в Shipping не регистрируется
static FAutoConsoleCommandWithWorldAndArgs GBoosterPurOk(
	TEXT("asda.BoosterShop.PurOk"),
	TEXT("asda.BoosterShop.PurOk — нажать «OK» 15527 панели покупки: откроется ОБЩАЯ модалка 1559 "
	     "в режиме F"),
	FConsoleCommandWithWorldAndArgsDelegate::CreateStatic([](const TArray<FString>&, UWorld* W)
	{
		if (UAsdaBoosterShopSubsystem* P = Shop(W)) { P->OnPurOkClicked(); Echo(W, P->DumpState()); }
	}));
#endif // ASDA_DEV_CONSOLE

#if ASDA_DEV_CONSOLE   // ★ античит А2: дев-команда, в Shipping не регистрируется
static FAutoConsoleCommandWithWorldAndArgs GBoosterPrev(
	TEXT("asda.BoosterShop.Preview"),
	TEXT("asda.BoosterShop.Preview — нажать «Вид» 15547 (окно предпросмотра 15600)"),
	FConsoleCommandWithWorldAndArgsDelegate::CreateStatic([](const TArray<FString>&, UWorld* W)
	{
		if (UAsdaBoosterShopSubsystem* P = Shop(W)) { P->OnPurPreviewClicked(); Echo(W, P->DumpState()); }
	}));
#endif // ASDA_DEV_CONSOLE

#if ASDA_DEV_CONSOLE   // ★ античит А2: дев-команда, в Shipping не регистрируется
static FAutoConsoleCommandWithWorldAndArgs GBoosterGxp(
	TEXT("asda.BoosterShop.SetGxp"),
	TEXT("asda.BoosterShop.SetGxp <сумма> — подставить баланс GXP, который в бою приносит 5467"),
	FConsoleCommandWithWorldAndArgsDelegate::CreateStatic([](const TArray<FString>& A, UWorld* W)
	{
		// ★ С-Р1 (26.09.2026), офлайн-гейт: поддельный баланс обходит клиентский гейт покупки (5070).
		if (FAsdaOutgoingGate::RefuseDebugWhenOnline(W, TEXT("asda.BoosterShop.SetGxp"))) { return; }
		UAsdaBoosterShopSubsystem* P = Shop(W);
		if (!P || A.Num() < 1) { Echo(W, TEXT("использование: asda.BoosterShop.SetGxp <сумма>")); return; }
		P->DebugSetGxp(FCString::Atoi(*A[0]));
		Echo(W, P->DumpState());
	}));
#endif // ASDA_DEV_CONSOLE

#if ASDA_DEV_CONSOLE   // ★ античит А2: дев-команда, в Shipping не регистрируется
static FAutoConsoleCommandWithWorldAndArgs GBoosterDump(
	TEXT("asda.BoosterShop.Dump"),
	TEXT("asda.BoosterShop.Dump — дамп контролов и состояния окна 15300"),
	FConsoleCommandWithWorldAndArgsDelegate::CreateStatic([](const TArray<FString>&, UWorld* W)
	{
		if (UAsdaBoosterShopSubsystem* P = Shop(W)) { Echo(W, P->DumpControls() + P->DumpState()); }
	}));
#endif // ASDA_DEV_CONSOLE

// ═══ ★ 01.11.2026 P101_CashGift: доступ самотеста `asda.SelfTest.CashGift` ═══
void UAsdaBoosterShopSubsystem::DebugSetGiftName(const FString& Name)
{
	// Как набор с клавиатуры: текст + событие изменения (на нём висит ограничитель LimitText 20).
	if (PurName) { PurName->SetText(FText::FromString(Name)); PurName->OnTextChanged.Broadcast(FText::FromString(Name)); }
}

FString UAsdaBoosterShopSubsystem::DebugGiftGeometry() const
{
	// Путь игрока: видимость и абсолютный прямоугольник ПОСЛЕ отрисовки (`GetCachedGeometry`).
	FString S;
	auto Line = [&S](const TCHAR* Tag, const UWidget* W)
	{
		if (!W) { S += FString::Printf(TEXT("  %s: НЕТ УЗЛА\n"), Tag); return; }
		const FGeometry G = W->GetCachedGeometry();
		const FVector2D P = G.GetAbsolutePosition();
		const FVector2D Z = G.GetAbsoluteSize();
		const UEditableText* Ed = Cast<UEditableText>(W);   // поле ввода — с текстом (проверка LimitText)
		S += FString::Printf(TEXT("  %s %s vis=%d экран=(%.0f,%.0f %.0fx%.0f)%s\n"), Tag, *W->GetName(), W->IsVisible() ? 1 : 0, P.X, P.Y, Z.X, Z.Y,
			Ed ? *FString::Printf(TEXT(" текст=«%s»"), *Ed->GetText().ToString()) : TEXT(""));
	};
	Line(TEXT("Подарок-ячейки-0"), Cells.IsValidIndex(0) ? Cells[0].GiftButton.Get() : nullptr);
	Line(TEXT("Подарок-подпись-0"), Cells.IsValidIndex(0) ? Cells[0].GiftCaption.Get() : nullptr);
	Line(TEXT("поле-15519"), PurName.Get());
	Line(TEXT("кнопка-15520"), PurConfirm.Get());
	Line(TEXT("подпись-15518"), PurTxt1.Get());
	S += FString::Printf(TEXT("  режим панели %d, код %d, кол-во %d\n"), PurMode, PurItemCode, PurQty);
	return S;
}
