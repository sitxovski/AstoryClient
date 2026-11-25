// Copyright (c) 2026 @sitxovski (Discord)
// AsdaPremiumShopSubsystem_Debug.cpp — ★ R2 10.11.2026 (разрез AsdaPremiumShopSubsystem.cpp, дословный перенос): дампы, SelfTest, дев-команды и самотесты магазина (GMall*).
//   Общее для частей — в AsdaPremiumShopSubsystemInternal.h; шапка и реверс — в AsdaPremiumShopSubsystem.cpp.
#include "UI/AsdaPremiumShopSubsystem.h"
#include "UI/AsdaPremiumBuyQueue.h"   // ★ R5-fix-b 13.11.2026 (заключительный заход): предикат «строки 12961 нет» (`AsdaPremium12961Missing`) для M3
#include "Resources/AsdaSoundSubsystem.h"   // ★ 06.11.2026: звуки окна 6000 (4/5/6)
#include "AsdaDevConsole.h"   // ★ античит А2: ASDA_DEV_CVAR / ASDA_DEV_CONSOLE — дев-ключи и команды вне Shipping
#include "UI/AsdaJuiWindowHandle.h"   // ★ волна 3 (15.09.2026): каркас окон JUI
#include "UI/AsdaNotice.h"   // ★ волна 1: фасад нотисов (Loc / Chat / MsgBox)
#include "UI/AsdaNoticeColors.h"   // ★ 02.11.2026: цвет −11514 чата 12919 (гейт `uiCanInteractWithTarget`)
#include "UI/AsdaHairShopSubsystem.h"        // ★ ревизия М-3: сессия парикмахерской (`CObject_GetField166`)
#include "UI/AsdaBoosterPanelSubsystem.h"   // ★ ревизия М-3: барабан гачи 10050 (`CObject_GetField254`); ★ 31.10.2026: окно «Вид» 15600
#include "UI/AsdaWarehouseSubsystem.h"      // ★ ревизия М-3: свиток склада (`CObject_GetField5581`)
#include "UI/AsdaTradeStateSubsystem.h"   // ★ 29.09.2026: гейты 6947 (открытие) и `uiCanInteractWithTarget` (покупка) — окно обмена 2008

#include "AsdaDataTableCache.h"
#include "Items/AsdaItemTableSubsystem.h"
#include "UI/AsdaEquipShopSubsystem.h"      // ★ ОБЩИЕ ТОЧКИ витрины `.she` (закон port-shared-points)
#include "UI/AsdaIconCellGrid.h"
#include "UI/AsdaItemTooltipSubsystem.h"
#include "UI/AsdaMessageSubsystem.h"
#include "UI/AsdaPackagePanelSubsystem.h"   // ★ панель состава набора 26500 — уже портирована
#include "UI/AsdaBoosterShopSubsystem.h"   // ★ самотест общий: печатает и магазин бустеров 15300
#include "UI/AsdaPremiumMallSubsystem.h"    // консольные команды дёргают и диспетчер 30000
#include "UI/AsdaPurchaseBoxSubsystem.h"    // ★ общая модалка подтверждения 1559
#include "UI/AsdaCashGiftSubsystem.h"       // ★ 01.11.2026 P101_CashGift: подарок 5072/5074 (кнопки 6085 + 9i, панели 6296/6333)
#include "UI/AsdaTipPanel.h"
#include "UI/AsdaUiHelpers.h"
#include "UI/AsdaWindowSubsystem.h"
// ★ волна В4 ремесла: хвост 5045 при закрытии (восстановление снимка окон)   ★ R3: комментарий снятого включения "Npc/AsdaNpcDialogSubsystem.h" (файл им не пользовался) — сохранён
#include "UI/AsdaNpcShopSubsystem.h"        // ★ волна В5: `uiInventoryOpenWindow` закрывает магазин NPC

#include "Blueprint/UserWidget.h"
#include "Blueprint/WidgetTree.h"
#include "Components/ComboBoxString.h"
#include "Components/Button.h"
#include "Components/CanvasPanelSlot.h"
#include "Components/CheckBox.h"
#include "Components/Image.h"
#include "Components/PanelWidget.h"
#include "Components/TextBlock.h"
#include "AsdaBytes.h"
#include "Engine/Engine.h"
#include "Engine/GameViewportClient.h"
#include "UnrealClient.h"
#include "Engine/GameInstance.h"
#include "Engine/World.h"
#include "GenericPlatform/GenericPlatformProcess.h"
#include "HAL/IConsoleManager.h"
#include "TimerManager.h"
#include "Internationalization/Regex.h"
#include "AsdaOutgoingGate.h"               // ★ С-Р1: общий офлайн-гейт отладочных команд (RefuseDebugWhenOnline)
#include "UI/AsdaDisassemblySubsystem.h"    // ★ волна 2-А: окно разборки 32000 видно ⇒ магазин 6000 молча не открывается
#include "Test/AsdaSelfTestKit.h"   // ★ 11.10.2026 (В3 S1): разбор аргументов самотеста (`quit` не путается с числом)
#include "Text/AsdaLang.h"          // ★ 26.10.2026: самотест имён бустеров — язык сессии (RU/EN)
#include "UObject/UObjectIterator.h" // ★ 26.10.2026: самотест имён бустеров — обход текстов открытых окон
#include "UI/AsdaInventorySubsystem.h"      // ★ 26.10.2026: самотест премиум-коробок — подбор ячеек вскрытия (ReservePackageSlots)
#include "Misc/FileHelper.h"                // ★ 26.10.2026: самотест премиум-коробок читает Tools/data/premium_box_sets.json
#include "Misc/Paths.h"                     // ★ 26.10.2026: путь спецификации от каталога проекта
#include "Dom/JsonObject.h"                 // ★ 26.10.2026: разбор спецификации наборов
#include "Serialization/JsonReader.h"       // ★ 26.10.2026: разбор спецификации наборов
#include "Serialization/JsonSerializer.h"   // ★ 26.10.2026: разбор спецификации наборов
// ★ 24.10.2026 (выход из сидения 1:1): `ApplyEtalonSceneStop` — стоп/«встать» при входе в сцену   ★ R3: комментарий снятого включения "World/AsdaPlayerCharacter.h" (файл им не пользовался) — сохранён
#include "UI/AsdaSystemOptionSubsystem.h"      // ★ 25.10.2026 (ревизия sit108, М-3): гейт «персонаж занят» (`objStateAllowsRender`) до стопа сцены
#include "GameFramework/PlayerController.h"    // ★ 24.10.2026: свой павн для `ApplyEtalonSceneStop`
#include "AsdaPremiumShopSubsystemInternal.h"   // ★ R2 10.11.2026: общее для частей AsdaPremiumShopSubsystem*.cpp

// ═══════════════════════════════ ДИАГНОСТИКА ═══════════════════════════════════════════════════
FString UAsdaPremiumShopSubsystem::DumpControls() const
{
	int32 CardsOk = 0, CartOk = 0, PkgOk = 0, MenuOk = 0, IdxOk = 0;
	for (const FAsdaPremiumCardUi& C : Cards)   { if (C.ItemIcon)  { ++CardsOk; } }
	for (const FAsdaPremiumCartUi& U : CartUi)  { if (U.Row)       { ++CartOk;  } }
	for (const FAsdaPackageCardUi& C : PkgUi)   { if (C.BuyButton) { ++PkgOk;   } }
	for (const TObjectPtr<UButton>& B : MenuButtons)  { if (B) { ++MenuOk; } }
	for (const TObjectPtr<UButton>& B : IndexButtons) { if (B) { ++IdxOk;  } }
	return FString::Printf(
		TEXT("=== SHELLITEM_STORE_WIN 6000 ===\n")
		TEXT("  карточки 6084+9i: %d/%d · корзина 6038+6j: %d/%d · наборы 58310+20i: %d/%d\n")
		TEXT("  полоса 6009: %d/%d · полоса 6002: %d/%d\n")
		TEXT("  крестик %s · сброс %s · купить %s · пополнить %s · баланс %s\n")
		TEXT("  стр. %s/%s · GXP %s · итого %s · погашено кодом: %d · вкладка 58300: %d узлов\n"),
		CardsOk, kCards, CartOk, kCartRows, PkgOk, kPkgCards,
		MenuOk, kMenuTabs, IdxOk, kIndexTabs,
		BtnClose ? TEXT("есть") : TEXT("НЕТ"), BtnReset ? TEXT("есть") : TEXT("НЕТ"),
		BtnBuySel ? TEXT("есть") : TEXT("НЕТ"), BtnCharge ? TEXT("есть") : TEXT("НЕТ"),
		BtnBalance ? TEXT("есть") : TEXT("НЕТ"),
		TxtPage ? TEXT("есть") : TEXT("НЕТ"), TxtPages ? TEXT("есть") : TEXT("НЕТ"),
		TxtCash ? TEXT("есть") : TEXT("НЕТ"), TxtSum ? TEXT("есть") : TEXT("НЕТ"),
		HiddenByCode.Num(), PackageTabTree.Num());
}

FString UAsdaPremiumShopSubsystem::DumpState() const
{
	FString S = FString::Printf(
		TEXT("6000: открыт=%s вкладка-наборов=%s | категория=%d инстр=%d класс=%d ур3=%d(%d) ")
		TEXT("| стр.%d/%d товаров %d\n")
		TEXT("      GXP=%d итого=%d в полёте=%s | покупок=%d ответов=%d\n"),
		bOpen ? TEXT("да") : TEXT("нет"), bPackageTab ? TEXT("да") : TEXT("нет"),
		CatTab, ToolSub, EqSub, EqSub2, bEqSub2Shown ? 1 : 0,
		Page + 1, MaxPage + 1, Entries.Num(), CashGxp, CartTotal(),
		bPending ? TEXT("да") : TEXT("нет"), CntBuy, CntResult);
	S += TEXT("      корзина:");
	for (int32 j = 0; j < kCartRows; ++j)
	{
		S += FString::Printf(TEXT(" [%d]%d%s"), j, CartRows[j].ItemCode, CartRows[j].bChecked ? TEXT("*") : TEXT(""));
	}
	S += TEXT("\n");
	return S;
}

// ★★★ САМОТЕСТ «ПУТЁМ ИГРОКА» (правило владельца №5): печатаем ФАКТИЧЕСКУЮ экранную геометрию
//   после отрисовки (`GetCachedGeometry`), видимость, цвет И ПОРЯДОК ДЕТЕЙ КАНВАСА — свежий урок:
//   узел может иметь верный цвет и координаты, но быть НЕ ВИДЕН, потому что лежит ниже по
//   порядку отрисовки.
FString UAsdaPremiumShopSubsystem::SelfTest() const
{
	FString S = TEXT("=== asda.PremiumMall.SelfTest ===\n");
	S += DumpState();
	S += DumpControls();
	if (!Widget) { S += TEXT("  ВИДЖЕТ НЕ СОЗДАН\n"); return S; }

	auto Row = [&S](const TCHAR* Tag, const UWidget* W)
	{
		if (!W) { S += FString::Printf(TEXT("  %-26s НЕТ УЗЛА\n"), Tag); return; }
		const FGeometry& G = W->GetCachedGeometry();
		const FVector2D Abs = G.GetAbsolutePosition();
		const FVector2D Sz  = G.GetLocalSize();
		int32 ZIdx = -1;
		if (const UPanelWidget* P = W->GetParent())
		{
			for (int32 i = 0; i < P->GetChildrenCount(); ++i) { if (P->GetChildAt(i) == W) { ZIdx = i; break; } }
		}
		// ★ Владелец 2026-08-28: «на шрифты и цвет текста не смотри» — печатаем ТЕКСТ.
		FString Txt = TEXT("—");
		if (const UTextBlock* Tb = Cast<UTextBlock>(W)) { Txt = Tb->GetText().ToString(); }
		S += FString::Printf(TEXT("  %-26s поз(%.0f,%.0f) разм(%.0f×%.0f) вид=%d порядок=%d «%s»\n"),
			Tag, Abs.X, Abs.Y, Sz.X, Sz.Y, (int32)W->GetVisibility(), ZIdx, *Txt);
	};
	Row(TEXT("6026 GXP"),        TxtCash);
	Row(TEXT("6025 «Сейчас GXP»"), TxtCashLbl);
	Row(TEXT("6081 «Итого»"),    TxtSum);
	Row(TEXT("6279 страница"),   TxtPage);
	Row(TEXT("6280 всего стр."), TxtPages);
	Row(TEXT("6275 купить"),     BtnBuySel);
	for (int32 i = 0; i < kMenuTabs; ++i)
	{
		Row(*FString::Printf(TEXT("6009 сегм.%d"), i), MenuLabels.IsValidIndex(i) ? MenuLabels[i].Get() : nullptr);
	}
	for (int32 i = 0; i < 3 && i < kCards; ++i)
	{
		Row(*FString::Printf(TEXT("карточка %d имя"), i), Cards[i].NameText);
		Row(*FString::Printf(TEXT("карточка %d цена"), i), Cards[i].PriceText);
	}
	for (int32 j = 0; j < 2 && j < kCartRows; ++j)
	{
		Row(*FString::Printf(TEXT("корзина %d имя"), j), CartUi[j].NameTxt);
	}
	// ★★★ КНОПКИ КАРТОЧКИ: видима должна быть РОВНО ОДНА (`_BUT0` 6089) с подписью loc 6944 «OK».
	for (int32 i = 0; i < 2 && i < kCards; ++i)
	{
		Row(*FString::Printf(TEXT("карточка %d КНОПКА"), i), Cards[i].BuyButton);
		Row(*FString::Printf(TEXT("карточка %d подпись"), i), Cards[i].BuyCaption);
		int32 Vis = 0;
		FString Caps;
		for (const TObjectPtr<UWidget>& W : Cards[i].AlwaysHidden)
		{
			if (!W) { continue; }
			if (W->GetVisibility() != ESlateVisibility::Collapsed) { ++Vis; }
			if (const UTextBlock* Tb = Cast<UTextBlock>(W.Get()))
			{
				Caps += FString::Printf(TEXT(" «%s»(вид=%d)"), *Tb->GetText().ToString(), (int32)W->GetVisibility());
			}
		}
		S += FString::Printf(TEXT("  карточка %d: скрытых узлов %d (не погашено %d), подписи:%s\n"),
			i, Cards[i].AlwaysHidden.Num(), Vis, *Caps);
	}
	// ★★★ ЗАДАЧА 3: белая кромка витрины 6083 — печатаем ФАКТИЧЕСКИЕ планы 9-slice.
	S += FString::Printf(TEXT("  --- рамка 6083: применённая высота %d px (по эталону 98*(рядов/3)+48) ---\n"),
		PanelHeightApplied);
	for (int32 Pl : { 0, 3, 6 })
	{
		Row(*FString::Printf(TEXT("6083 план %d"), Pl),
			PanelPlanes.IsValidIndex(Pl) ? PanelPlanes[Pl].Get() : nullptr);
	}
	// ★★★ ЖИВЫЕ ПОЛОСЫ 58200…58203: подписи, видимость и ПОРЯДОК ДЕТЕЙ канваса.
	S += FString::Printf(TEXT("  --- полосы: категория=%d инструмент=%d класс=%d уровень3=%d/%d ---\n"),
		CatTab, ToolSub, EqSub, EqSub2, bEqSub2Shown ? 1 : 0);
	for (int32 i = 0; i < kCatTabs; ++i)  { Row(*FString::Printf(TEXT("58200 кат.%d"), i),  CatLabels.IsValidIndex(i) ? CatLabels[i].Get() : nullptr); }
	for (int32 i = 0; i < kToolTabs; ++i) { Row(*FString::Printf(TEXT("58201 инстр.%d"), i), ToolLabels.IsValidIndex(i) ? ToolLabels[i].Get() : nullptr); }
	for (int32 i = 0; i < kEquipTabs; ++i){ Row(*FString::Printf(TEXT("58202 класс %d"), i), EqLabels.IsValidIndex(i) ? EqLabels[i].Get() : nullptr); }
	for (int32 i = 0; i < kSub2Tabs; ++i) { Row(*FString::Printf(TEXT("58203 ур3 %d"), i),   Sub2Labels.IsValidIndex(i) ? Sub2Labels[i].Get() : nullptr); }
	// Мёртвые: 6002, 6009, 58204, 58940 — обязаны быть вид=1.
	for (const TCHAR* N : { TEXT("SHELLITEM_STORE_INDEX_Text"), TEXT("SHELLITEM_STORE_MENU_Text"),
	                        TEXT("SHELLITEM_STORE_MENU_NEW1_2_Text"), TEXT("SHELLITEM_STORE_MENU_NEW1_3_Text") })
	{
		Row(N, AsdaUi::FindNodeExact(Widget, N));
	}
	S += FString::Printf(TEXT("  --- сетка поднята на %.0f px (записей BIN %d) ---\n"),
		ShiftApplied, BinRows.Num());
	return S;
}

namespace {
// ═══════════════════════════════ КОНСОЛЬНЫЕ КОМАНДЫ ═══════════════════════════════════════════
//   ★★★ ПРОВЕРКА — ПУТЁМ ИГРОКА (правило владельца №5): все команды повторяют РОВНО те же входы,
//   что и мышь/клавиатура, а `SelfTest` печатает ФАКТИЧЕСКУЮ экранную геометрию после отрисовки.
//   Запуск:
//     UnrealEditor.exe <uproject> /Game/Map/alpen/alpen -game
//       -ExecCmds="asda.PremiumMall.Toggle, asda.PremiumMall.SelfTest"
	UAsdaPremiumShopSubsystem* ShopSub(UWorld* W)
	{
		UGameInstance* GI = W ? W->GetGameInstance() : nullptr;
		return GI ? GI->GetSubsystem<UAsdaPremiumShopSubsystem>() : nullptr;
	}
	UAsdaPremiumMallSubsystem* MallSub(UWorld* W)
	{
		UGameInstance* GI = W ? W->GetGameInstance() : nullptr;
		return GI ? GI->GetSubsystem<UAsdaPremiumMallSubsystem>() : nullptr;
	}
}

#if !UE_BUILD_SHIPPING   // ★ античит А2 (12.10.2026): отладка самотестов/дев-команд — только вне Shipping
namespace {
// ★ ИСПРАВЛЕНИЕ 11.10.2026 (дедуп №2, В2-A, U13): было `AddOnScreenDebugMessage` (12 с поверх экрана; в Shipping оверлей пуст) — стало общая точка `AsdaNotice::ConsoleEcho`: каждая строка в журнал (Display), непустая — в ленту Tip.
	void ShopEcho(UWorld* W, const FString& S) { AsdaNotice::ConsoleEcho(W, S, LogAsdaPremShop); }
}
#endif // !UE_BUILD_SHIPPING

#if ASDA_DEV_CONSOLE   // ★ античит А2: дев-команда, в Shipping не регистрируется
static FAutoConsoleCommandWithWorldAndArgs GMallToggle(
	TEXT("asda.PremiumMall.Toggle"),
	TEXT("asda.PremiumMall.Toggle — тумблер молла ровно тем же путём, что клавиша-хоткей раздела (cmd 39)"),
	FConsoleCommandWithWorldAndArgsDelegate::CreateStatic([](const TArray<FString>&, UWorld* W)
	{
		if (UAsdaPremiumMallSubsystem* M = MallSub(W)) { M->Toggle(); ShopEcho(W, M->DumpState()); }
	}));
#endif // ASDA_DEV_CONSOLE

#if ASDA_DEV_CONSOLE   // ★ античит А2: дев-команда, в Shipping не регистрируется
static FAutoConsoleCommandWithWorldAndArgs GMallSeg(
	TEXT("asda.PremiumMall.Segment"),
	TEXT("asda.PremiumMall.Segment <0..3> — нажать сегмент переключателя 30002 (0 Элитный, "
	     "1 Бустер, 2 Причёски, 3 Набор)"),
	FConsoleCommandWithWorldAndArgsDelegate::CreateStatic([](const TArray<FString>& A, UWorld* W)
	{
		UAsdaPremiumMallSubsystem* M = MallSub(W);
		if (!M || A.Num() < 1) { ShopEcho(W, TEXT("использование: asda.PremiumMall.Segment <0..3>")); return; }
		M->SelectSegment(FCString::Atoi(*A[0]));
		ShopEcho(W, M->DumpState());
	}));
#endif // ASDA_DEV_CONSOLE

#if ASDA_DEV_CONSOLE   // ★ античит А2: дев-команда, в Shipping не регистрируется
static FAutoConsoleCommandWithWorldAndArgs GMallDump(
	TEXT("asda.PremiumMall.Dump"),
	TEXT("asda.PremiumMall.Dump — дамп контролов и состояния меню 30000, магазина 6000 и модалки 1559"),
	FConsoleCommandWithWorldAndArgsDelegate::CreateStatic([](const TArray<FString>&, UWorld* W)
	{
		FString S;
		if (UAsdaPremiumMallSubsystem* M = MallSub(W)) { S += M->DumpControls() + M->DumpState() + TEXT("\n"); }
		if (UAsdaPremiumShopSubsystem* P = ShopSub(W)) { S += P->DumpControls() + P->DumpState(); }
		UGameInstance* GI = W ? W->GetGameInstance() : nullptr;
		if (UAsdaPurchaseBoxSubsystem* B = GI ? GI->GetSubsystem<UAsdaPurchaseBoxSubsystem>() : nullptr)
		{
			S += B->DumpControls() + B->DumpState();
		}
		if (UAsdaBoosterShopSubsystem* Bs = GI ? GI->GetSubsystem<UAsdaBoosterShopSubsystem>() : nullptr)
		{
			S += Bs->DumpControls() + Bs->DumpState();
		}
		ShopEcho(W, S);
	}));
#endif // ASDA_DEV_CONSOLE

#if ASDA_DEV_CONSOLE   // ★ античит А2: дев-команда, в Shipping не регистрируется
// ★★★ ЗАДЕРЖКА ОБЯЗАТЕЛЬНА. `-ExecCmds` выполняет ВСЕ команды в ОДНОМ кадре, ДО первой
//   отрисовки, и `GetCachedGeometry` у только что показанных виджетов ещё нулевая — самотест
//   напечатал бы «поз(0,0) разм(0×0)» и наврал бы «всё на месте». Поэтому у команды есть
//   аргумент «через сколько секунд снять геометрию», а второй аргумент `quit` закрывает игру
//   сразу после снимка:
//     -ExecCmds="asda.PremiumMall.Toggle, asda.PremiumMall.SelfTest 4 quit"
static FAutoConsoleCommandWithWorldAndArgs GMallSelfTest(
	TEXT("asda.PremiumMall.SelfTest"),
	TEXT("asda.PremiumMall.SelfTest [задержкаСек] [quit] — ФАКТИЧЕСКАЯ экранная геометрия узлов "
	     "молла ПОСЛЕ отрисовки (позиция, размер, видимость, ПОРЯДОК ДЕТЕЙ канваса, цвет). "
	     "Без задержки снимок берётся в том же кадре и будет нулевым."),
	FConsoleCommandWithWorldAndArgsDelegate::CreateStatic([](const TArray<FString>& A, UWorld* W)
	{
		const float Delay = FAsdaSelfTestArgs::Parse(A).Float(0, 0.f);   // ★ 11.10.2026 (В3 S1): разбор каркаса — `quit` первым аргументом не читается как задержка
		const bool bQuit = A.ContainsByPredicate([](const FString& S) { return S.Equals(TEXT("quit"), ESearchCase::IgnoreCase); });
		auto Run = [W, bQuit]()
		{
			// ★ Разрешение меняется НЕ в том же кадре, что `-ExecCmds`, поэтому перед снятием
			//   геометрии пересобираем раскладку под фактический экран.
			if (UAsdaPremiumShopSubsystem* P = ShopSub(W)) { P->RelayoutForScreen(); ShopEcho(W, P->SelfTest()); }
			else { ShopEcho(W, TEXT("подсистема премиум-магазина не поднята")); }
			// ★ САМОТЕСТ ОБЩИЙ (требование владельца «переиспользуй»): второй команды не заводим —
			//   тот же `asda.PremiumMall.SelfTest` печатает и магазин бустеров 15300.
			if (UGameInstance* GI = W ? W->GetGameInstance() : nullptr)
			{
				if (UAsdaBoosterShopSubsystem* B = GI->GetSubsystem<UAsdaBoosterShopSubsystem>())
				{
					ShopEcho(W, B->SelfTest());
				}
			}
			if (bQuit && GEngine && W) { GEngine->DeferredCommands.Add(TEXT("quit")); }
		};
		if (Delay <= 0.f || !W) { Run(); return; }
		FTimerHandle H;
		W->GetTimerManager().SetTimer(H, FTimerDelegate::CreateLambda(Run), Delay, false);
		ShopEcho(W, FString::Printf(TEXT("самотест снимет геометрию через %.1f с%s"),
			Delay, bQuit ? TEXT(" и закроет игру") : TEXT("")));
	}));
#endif // ASDA_DEV_CONSOLE

#if ASDA_DEV_CONSOLE   // ★ античит А2: дев-команда, в Shipping не регистрируется
static FAutoConsoleCommandWithWorldAndArgs GMallTab(
	TEXT("asda.PremiumMall.Tab"),
	TEXT("asda.PremiumMall.Tab <0..9> — нажать сегмент полосы категорий 6009"),
	FConsoleCommandWithWorldAndArgsDelegate::CreateStatic([](const TArray<FString>& A, UWorld* W)
	{
		UAsdaPremiumShopSubsystem* P = ShopSub(W);
		if (!P || A.Num() < 1) { ShopEcho(W, TEXT("использование: asda.PremiumMall.Tab <0..9>")); return; }
		P->SetMenuTab(FCString::Atoi(*A[0]));
		ShopEcho(W, P->DumpState());
	}));
#endif // ASDA_DEV_CONSOLE

#if ASDA_DEV_CONSOLE   // ★ античит А2: дев-команда, в Shipping не регистрируется
static FAutoConsoleCommandWithWorldAndArgs GMallCat(
	TEXT("asda.PremiumMall.Cat"),
	TEXT("asda.PremiumMall.Cat <0..7> — нажать сегмент ЖИВОЙ полосы категорий 58200 "
	     "(0 Горячая новинка!, 1 Новинка!, 2 Инструмент, 3 Снаряжение, 4 Волосы, 5 Питомец, "
	     "6 Элитный набор, 7 Лавка мелочей)"),
	FConsoleCommandWithWorldAndArgsDelegate::CreateStatic([](const TArray<FString>& A, UWorld* W)
	{
		UAsdaPremiumShopSubsystem* P = ShopSub(W);
		if (!P || A.Num() < 1) { ShopEcho(W, TEXT("использование: asda.PremiumMall.Cat <0..7>")); return; }
		P->SetCategory(FCString::Atoi(*A[0]));
		ShopEcho(W, P->DumpState());
	}));
#endif // ASDA_DEV_CONSOLE

#if ASDA_DEV_CONSOLE   // ★ античит А2: дев-команда, в Shipping не регистрируется
static FAutoConsoleCommandWithWorldAndArgs GMallSub(
	TEXT("asda.PremiumMall.Sub"),
	TEXT("asda.PremiumMall.Sub <номер> — нажать сегмент ВТОРОЙ полосы: 58201 для категории "
	     "«Инструмент» (0..4) либо 58202 для «Снаряжения» (0..2 Воин/Лучник/Маг)"),
	FConsoleCommandWithWorldAndArgsDelegate::CreateStatic([](const TArray<FString>& A, UWorld* W)
	{
		UAsdaPremiumShopSubsystem* P = ShopSub(W);
		if (!P || A.Num() < 1) { ShopEcho(W, TEXT("использование: asda.PremiumMall.Sub <номер>")); return; }
		const int32 N = FCString::Atoi(*A[0]);
		if (P->GetCategory() == 3) { P->SetEquipSub(N); } else { P->SetToolSub(N); }
		ShopEcho(W, P->DumpState());
	}));
#endif // ASDA_DEV_CONSOLE

#if ASDA_DEV_CONSOLE   // ★ античит А2: дев-команда, в Shipping не регистрируется
static FAutoConsoleCommandWithWorldAndArgs GMallSub2(
	TEXT("asda.PremiumMall.Sub2"),
	TEXT("asda.PremiumMall.Sub2 <0..7> — нажать сегмент ТРЕТЬЕЙ полосы 58203 "
	     "(Общий вид, Оружие, Шлем, Доспех, Перчатки, Штаны, Ботинки, Бустер)"),
	FConsoleCommandWithWorldAndArgsDelegate::CreateStatic([](const TArray<FString>& A, UWorld* W)
	{
		UAsdaPremiumShopSubsystem* P = ShopSub(W);
		if (!P || A.Num() < 1) { ShopEcho(W, TEXT("использование: asda.PremiumMall.Sub2 <0..7>")); return; }
		P->SetEquipSub2(FCString::Atoi(*A[0]));
		ShopEcho(W, P->DumpState());
	}));
#endif // ASDA_DEV_CONSOLE

#if ASDA_DEV_CONSOLE   // ★ античит А2: дев-команда, в Shipping не регистрируется
static FAutoConsoleCommandWithWorldAndArgs GMallBuy(
	TEXT("asda.PremiumMall.Buy"),
	TEXT("asda.PremiumMall.Buy <номерКарточки 0..20> — нажать кнопку покупки карточки (откроет 1559)"),
	FConsoleCommandWithWorldAndArgsDelegate::CreateStatic([](const TArray<FString>& A, UWorld* W)
	{
		UAsdaPremiumShopSubsystem* P = ShopSub(W);
		if (!P || A.Num() < 1) { ShopEcho(W, TEXT("использование: asda.PremiumMall.Buy <0..20>")); return; }
		P->OnCardBuyClicked(FCString::Atoi(*A[0]));
		ShopEcho(W, P->DumpState());
	}));
#endif // ASDA_DEV_CONSOLE

#if ASDA_DEV_CONSOLE   // ★ античит А2: дев-команда, в Shipping не регистрируется
static FAutoConsoleCommandWithWorldAndArgs GMallGxp(
	TEXT("asda.PremiumMall.SetGxp"),
	TEXT("asda.PremiumMall.SetGxp <сумма> — подставить баланс GXP, который в бою приносит 5467 "
	     "(диагностика: позволяет пройти путь «открыть → купить → модалка 1559» без сервера)"),
	FConsoleCommandWithWorldAndArgsDelegate::CreateStatic([](const TArray<FString>& A, UWorld* W)
	{
		// ★ С-Р1 (26.09.2026), офлайн-гейт: поддельный баланс обходит клиентский гейт покупки (5070).
		if (FAsdaOutgoingGate::RefuseDebugWhenOnline(W, TEXT("asda.PremiumMall.SetGxp"))) { return; }
		UAsdaPremiumShopSubsystem* P = ShopSub(W);
		if (!P || A.Num() < 1) { ShopEcho(W, TEXT("использование: asda.PremiumMall.SetGxp <сумма>")); return; }
		P->DebugSetGxp(FCString::Atoi(*A[0]));
		ShopEcho(W, P->DumpState());
	}));
#endif // ASDA_DEV_CONSOLE

#if ASDA_DEV_CONSOLE   // ★ античит А2: дев-команда, в Shipping не регистрируется
static FAutoConsoleCommandWithWorldAndArgs GMallBoxBtn(
	TEXT("asda.PremiumMall.BoxClick"),
	TEXT("asda.PremiumMall.BoxClick <1561|1562> — нажать кнопку общей модалки покупки 1559"),
	FConsoleCommandWithWorldAndArgsDelegate::CreateStatic([](const TArray<FString>& A, UWorld* W)
	{
		UGameInstance* GI = W ? W->GetGameInstance() : nullptr;
		UAsdaPurchaseBoxSubsystem* B = GI ? GI->GetSubsystem<UAsdaPurchaseBoxSubsystem>() : nullptr;
		if (!B || A.Num() < 1) { ShopEcho(W, TEXT("использование: asda.PremiumMall.BoxClick <1561|1562>")); return; }
		const int32 Id = FCString::Atoi(*A[0]);
		if (Id == 1561) { B->OnConfirmClicked(); }
		else if (Id == 1562) { B->OnCancelClicked(); }
		else { ShopEcho(W, TEXT("известны только 1561 и 1562")); return; }
		ShopEcho(W, B->DumpState());
	}));
#endif // ASDA_DEV_CONSOLE

#if ASDA_DEV_CONSOLE   // ★ античит А2: дев-команда, в Shipping не регистрируется
// ★★ 24.10.2026 САМОТЕСТ ЗАЩЁЛКИ «ЗАПРОС В ПОЛЁТЕ» ОКНА 6000 ([ИСПРАВЛЕНИЕ] Д-18, ключ `asda.PremiumMall.ShopPendingTimeout`).
//   Без сети: защёлка взводится так же, как после отправки 5070 (`DebugArmPending`), снимает её настоящий `Tick` подсистемы,
//   ответ 5071 подаётся в настоящий обработчик `OnBuyResult` (код 0 «Не удалось получить предмет» — им же сервер теперь
//   отказывает в невыдаваемом предмете). Случаи:
//     A ответа нет, ключ 10 с: через 5 с защёлка ещё стоит, через 11 с снята;
//     B ответ пришёл: защёлку снимает приём 5071 сразу, счётчик ответов +1 (поведение оригинала, таймаут не участвует);
//     C ключ 0 (как оригинал): защёлка, взведённая 30 с назад, через 1,5 с всё ещё стоит.
//   Итог `САМОТЕСТ-6000-ЗАЩЁЛКА ИТОГ: ok=N fail=M` (ловит `asda.SelfTest.Batch`), ключ и защёлка возвращаются.
static FAutoConsoleCommandWithWorldAndArgs GMallPendingSelfTest(
	TEXT("asda.PremiumMall.PendingSelfTest"),
	TEXT("asda.PremiumMall.PendingSelfTest [quit] — защёлка «запрос в полёте» окна 6000: нет ответа → снята через "
	     "asda.PremiumMall.ShopPendingTimeout (10 с); ответ 5071 → снята сразу; ключ 0 → держится, как в оригинале"),
	FConsoleCommandWithWorldAndArgsDelegate::CreateStatic([](const TArray<FString>& A, UWorld* W)
	{
		static const TCHAR* kCmd = TEXT("asda.PremiumMall.PendingSelfTest");
		const FAsdaSelfTestArgs Args = FAsdaSelfTestArgs::Parse(A);
		if (AsdaSelfTestKit::RefuseStart(W, kCmd, Args, 60.f)) { return; }
		UAsdaPremiumShopSubsystem* P = ShopSub(W);
		if (!W || !P) { AsdaSelfTestKit::Refused(W, kCmd, Args.bQuit, TEXT("нет мира или подсистемы премиум-магазина")); return; }
		// Состояние прогона (общее для отложенных шагов).
		struct FRun { int32 Ok = 0; int32 Fail = 0; int32 Results0 = 0; int32 Queue0 = 0; };   // счёт проверок, ответов 5071 и окон-сообщений до шага B
		TSharedRef<FRun> R = MakeShared<FRun>();
		TWeakObjectPtr<UWorld> WW(W);
		TWeakObjectPtr<UAsdaPremiumShopSubsystem> WP(P);
		const bool bQuit = Args.bQuit;
		auto Check = [R](bool bOk, const FString& What)
		{
			if (bOk) { ++R->Ok; } else { ++R->Fail; }
			UE_LOG(LogAsdaPremShop, Display, TEXT("САМОТЕСТ-6000-ЗАЩЁЛКА %s %s"), bOk ? TEXT("OK  ") : TEXT("FAIL"), *What);
		};
		AsdaCVarSave::ArmWorldGuard(W, kCmd);
		AsdaCVarSave::SetForTest(TEXT("asda.PremiumMall.ShopPendingTimeout"), 10.f);
		// A: ответа нет.
		P->DebugArmPending(0.0);
		Check(P->DebugIsPending(), TEXT("A0 защёлка взведена (как после отправки 5070)"));
		auto After = [WW](float Sec, TFunction<void()> Fn)
		{
			if (UWorld* Wd = WW.Get())
			{
				FTimerHandle H;
				Wd->GetTimerManager().SetTimer(H, FTimerDelegate::CreateLambda(MoveTemp(Fn)), Sec, false);
			}
		};
		After(5.f, [WP, Check]()
		{
			if (UAsdaPremiumShopSubsystem* S = WP.Get()) { Check(S->DebugIsPending(), TEXT("A1 через 5 с (< 10 с) защёлка ещё стоит")); }
		});
		After(11.f, [WW, WP, R, Check, After, bQuit]()
		{
			UAsdaPremiumShopSubsystem* S = WP.Get();
			if (!S) { Check(false, TEXT("подсистема пропала")); return; }
			Check(!S->DebugIsPending(), TEXT("A2 через 11 с без ответа защёлка снята (Д-18), окно снова принимает покупки"));
			// B: ответ пришёл.
			S->DebugArmPending(0.0);
			R->Results0 = S->DebugResultCount();
			if (UWorld* Wd = WW.Get())
			{
				if (const UAsdaWindowSubsystem* WM = Wd->GetGameInstance() ? Wd->GetGameInstance()->GetSubsystem<UAsdaWindowSubsystem>() : nullptr)
				{
					R->Queue0 = WM->GetMsgBoxQueueNum();   // очередь окон-сообщений до ответа 5071
				}
			}
			TArray<uint8> Body;
			Body.SetNumZeroed(407);
			Body[1] = 1;   // валюта GXP; код результата 0 = «Не удалось получить предмет»
			S->DebugFeedBuyResult(Body);
			Check(!S->DebugIsPending() && S->DebugResultCount() == R->Results0 + 1,
				TEXT("B ответ 5071 снимает защёлку сразу (как оригинал), ответ учтён"));
			// Код 0 показал МОДАЛЬНОЕ окно 1500 («Не удалось получить предмет») — закрываем его, как кнопка «ОК»,
			//   иначе модальный гейт достался бы следующим самотестам пакета (магазин NPC, хотбар).
			if (UWorld* Wd = WW.Get())
			{
				if (UAsdaWindowSubsystem* WM = Wd->GetGameInstance() ? Wd->GetGameInstance()->GetSubsystem<UAsdaWindowSubsystem>() : nullptr)
				{
					const int32 QBefore = WM->GetMsgBoxQueueNum();
					if (QBefore > R->Queue0) { WM->AnswerMsgBox(/*bTwoBut*/ false, /*bYes*/ true); }   // «ОК» окна 1500
					Check(WM->GetMsgBoxQueueNum() == R->Queue0,
						FString::Printf(TEXT("B окно 1500 ответа закрыто «ОК» (очередь %d → %d, было %d)"),
							QBefore, WM->GetMsgBoxQueueNum(), R->Queue0));
				}
			}
			// C: ключ 0 — как оригинал.
			AsdaCVarSave::SetForTest(TEXT("asda.PremiumMall.ShopPendingTimeout"), 0.f);
			S->DebugArmPending(30.0);
			After(1.5f, [WW, WP, R, Check, bQuit]()
			{
				UAsdaPremiumShopSubsystem* S2 = WP.Get();
				Check(S2 && S2->DebugIsPending(), TEXT("C ключ 0: защёлка, взведённая 30 с назад, держится (оригинал)"));
				if (S2) { S2->DebugClearPending(); }
				AsdaSelfTestKit::Finish(   // возврат ключа (RestoreAll снимает и сторожа мира) + итог + quit
WW.Get(), kCmd, bQuit, LogAsdaPremShop, TEXT("САМОТЕСТ-6000-ЗАЩЁЛКА"), R->Ok, R->Fail);
			});
		});
	}));
#endif // ASDA_DEV_CONSOLE

#if ASDA_DEV_CONSOLE   // ★ античит А2: дев-команда, в Shipping не регистрируется
// ★★ 26.10.2026 САМОТЕСТ «ОТКАЗ 5071 НЕ ПОРТИТ БАЛАНС» ([ИСПРАВЛЕНИЕ] ключи `asda.PremiumMall.FailKeepsBalance` и
//   `asda.PremiumMall.BoosterFailKeepsBalance`). Без сети: запрос в полёте взводится как после 5070 (окно 6000 —
//   `DebugArmPending`, магазин бустеров — перехваченная покупка `DebugConfirmBuy`), ответ 5071 подаётся в НАСТОЯЩИЕ
//   обработчики `OnBuyResult` телом, как его собирает сервер на отказ (`sub_4F9F00`: код 0, валюта 0, баланс −1). Случаи:
//     A окно 6000: GXP 999999 → отказ (код 0, −1) → GXP 999999; A2 успех (код 1, 777) → GXP 777;
//     B ключ 0 (прежний порт): отказ → GXP −1;
//     C магазин бустеров 15300: GXP 999999 → отказ → 999999; C2 ключ 0 → −1.
//   Окна-сообщения отказа закрываются «ОК». Итог `САМОТЕСТ-5071-БАЛАНС ИТОГ: ok=N fail=M`; ключи, перехват и признаки окон возвращаются.
static FAutoConsoleCommandWithWorldAndArgs GMallFailBalanceSelfTest(
	TEXT("asda.PremiumMall.FailBalanceSelfTest"),
	TEXT("asda.PremiumMall.FailBalanceSelfTest [quit] — отказ 5071 (код 0, баланс −1 от сервера) не меняет баланс окна 6000 "
	     "и магазина бустеров 15300, успех (код 1) меняет; ключи FailKeepsBalance 0 — прежнее поведение (−1)"),
	FConsoleCommandWithWorldAndArgsDelegate::CreateStatic([](const TArray<FString>& A, UWorld* W)
	{
		static const TCHAR* kCmd = TEXT("asda.PremiumMall.FailBalanceSelfTest");
		static const TCHAR* kKeyP = TEXT("asda.PremiumMall.FailKeepsBalance");
		static const TCHAR* kKeyB = TEXT("asda.PremiumMall.BoosterFailKeepsBalance");
		const FAsdaSelfTestArgs Args = FAsdaSelfTestArgs::Parse(A);
		if (AsdaSelfTestKit::RefuseStart(W, kCmd, Args, 30.f)) { return; }
		UAsdaPremiumShopSubsystem* P = ShopSub(W);
		UGameInstance* GI0 = W ? W->GetGameInstance() : nullptr;
		UAsdaBoosterShopSubsystem* Bs = GI0 ? GI0->GetSubsystem<UAsdaBoosterShopSubsystem>() : nullptr;
		UAsdaWindowSubsystem* WM = GI0 ? GI0->GetSubsystem<UAsdaWindowSubsystem>() : nullptr;
		if (!W || !P || !Bs || !WM)
		{
			AsdaSelfTestKit::Refused(W, kCmd, Args.bQuit, TEXT("нет мира или подсистем премиум-магазина / бустеров / окон"));
			return;
		}
		if (P->IsOpen() || Bs->IsOpen())
		{
			AsdaSelfTestKit::Refused(W, kCmd, Args.bQuit, TEXT("окно 6000 или 15300 открыто игроком — самотест подменил бы его баланс"));
			return;
		}
		int32 Ok = 0, Fail = 0;   // счёт проверок
		auto Check = [&Ok, &Fail](bool bOk, const FString& What)
		{
			if (bOk) { ++Ok; } else { ++Fail; }
			UE_LOG(LogAsdaPremShop, Display, TEXT("САМОТЕСТ-5071-БАЛАНС %s %s"), bOk ? TEXT("OK  ") : TEXT("FAIL"), *What);
		};
		// GXP из строки состояния подсистемы (`DumpState`: «GXP=<число>»).
		auto Gxp = [](const FString& Dump)
		{
			const int32 At = Dump.Find(TEXT("GXP="));
			return At >= 0 ? FCString::Atoi(*Dump.Mid(At + 4, 12)) : MIN_int32;
		};
		// Тело 5071 (407 б): код, валюта, баланс @+2, подрежим 1 = в инвентарь.
		auto Reply = [](uint8 Code, uint8 Gxp, int32 Balance)
		{
			TArray<uint8> B;
			B.SetNumZeroed(407);
			B[0] = Code;
			B[1] = Gxp;
			AsdaBytes::WrI32(&B[2], Balance);
			B[12] = 1;
			return B;
		};
		// Окна-сообщения, которые показал отказ, закрываем «ОК», чтобы модальный гейт не достался следующим самотестам.
		const int32 Queue0 = WM->GetMsgBoxQueueNum();
		auto CloseBoxes = [WM, Queue0]()
		{
			for (int32 Guard = 0; Guard < 8 && WM->GetMsgBoxQueueNum() > Queue0; ++Guard) { WM->AnswerMsgBox(/*bTwoBut*/ false, /*bYes*/ true); }
		};
		AsdaCVarSave::ArmWorldGuard(W, kCmd);
		P->DebugSetOpenFlag(true);
		Bs->DebugSetOpenFlag(true);

		// ── A: окно 6000, ключ 1 ──
		AsdaCVarSave::SetForTest(kKeyP, 1);
		P->DebugSetGxp(999999);
		P->DebugArmPending(0.0);
		P->DebugFeedBuyResult(Reply(0, 0, -1));
		CloseBoxes();
		Check(Gxp(P->DumpState()) == 999999 && !P->DebugIsPending(),
			FString::Printf(TEXT("A1 окно 6000: отказ (код 0, баланс −1) — баланс остался 999999 (GXP=%d), защёлка снята"), Gxp(P->DumpState())));
		P->DebugArmPending(0.0);
		P->DebugFeedBuyResult(Reply(1, 1, 777));
		CloseBoxes();
		Check(Gxp(P->DumpState()) == 777, FString::Printf(TEXT("A2 окно 6000: успех (код 1) — баланс из ответа 777 (GXP=%d)"), Gxp(P->DumpState())));
		// ── B: окно 6000, ключ 0 — прежний порт ──
		AsdaCVarSave::SetForTest(kKeyP, 0);
		P->DebugSetGxp(999999);
		P->DebugArmPending(0.0);
		P->DebugFeedBuyResult(Reply(0, 0, -1));
		CloseBoxes();
		Check(Gxp(P->DumpState()) == -1, FString::Printf(TEXT("B ключ 0: отказ кладёт −1, как прежний порт (GXP=%d)"), Gxp(P->DumpState())));

		// ── C: магазин бустеров 15300 ──
		AsdaCVarSave::SetForTest(kKeyB, 1);
		Bs->DebugSetGxp(999999);
		Bs->DebugSetCapture(true);
		Bs->DebugConfirmBuy(53672, 1);
		const bool bArmed = Bs->DebugIsPending();
		Bs->DebugFeedBuyResult(Reply(0, 0, -1));
		CloseBoxes();
		Check(bArmed && Gxp(Bs->DumpState()) == 999999 && !Bs->DebugIsPending(),
			FString::Printf(TEXT("C1 бустеры: запрос был в полёте (%d), отказ — баланс остался 999999 (GXP=%d)"), bArmed ? 1 : 0, Gxp(Bs->DumpState())));
		AsdaCVarSave::SetForTest(kKeyB, 0);
		Bs->DebugSetCapture(true);
		Bs->DebugConfirmBuy(53672, 1);
		Bs->DebugFeedBuyResult(Reply(0, 0, -1));
		CloseBoxes();
		Check(Gxp(Bs->DumpState()) == -1, FString::Printf(TEXT("C2 бустеры, ключ 0: отказ кладёт −1, как прежний порт (GXP=%d)"), Gxp(Bs->DumpState())));

		// Возврат: перехват, признаки окон, защёлки, баланс.
		P->DebugClearPending(); P->DebugSetGxp(0); P->DebugSetOpenFlag(false);
		Bs->DebugSetCapture(false); Bs->DebugSetGxp(0); Bs->DebugSetOpenFlag(false);
		AsdaSelfTestKit::Finish(   // возврат ключей (RestoreAll снимает и сторожа мира) + итог + quit
			W, kCmd, Args.bQuit, LogAsdaPremShop, TEXT("САМОТЕСТ-5071-БАЛАНС"), Ok, Fail);
	}));
#endif // ASDA_DEV_CONSOLE

#if ASDA_DEV_CONSOLE   // ★ античит А2: дев-команда, в Shipping не регистрируется
// ★★ ★ 13.11.2026 (строка 12961) САМОТЕСТ «КОД 0x0E ПОКУПКИ — СТРОКА 12961». Решение владельца: loc 12961 записана в обе таблицы текста
//   (RU «Не продаётся», EN «Not For Sale»); оригинал (shop_trade.c:3550) на код 0x0E показывает 12961, в русском эталоне
//   строки не было (дефект оригинала К11). Без сети, язык сессии (пакет `--lang ru` / `--lang en`):
//     D данные: `Loc(12961)` = ожидаемый текст языка сессии;
//     P окно 6000: запрос в полёте (`DebugArmPending`), ответ 5071 код 0x0E в НАСТОЯЩИЙ `OnBuyResult` — верхнее окно-сообщение
//       = строка 12961; при ключе `asda.PremiumMall.Notice12961Fallback` 1 и 0 (строка есть — ключ не подменяет её);
//     B магазин бустеров 15300: перехваченная покупка (`DebugConfirmBuy`), ответ 5071 код 0x0E — окно = строка 12961.
//   До правки 13.11 оба магазина подменяли 12961 строкой 9570 безусловно — P1 и B были бы красными.
//     K (R5-fix-b, второй заход): ключ `asda.PremiumMall.Show12961` 0 — оба магазина показывают 9570 (контроль «без правки»);
//     M (★ R5-fix-b 13.11.2026 (третий заход)): шов `asda.PremiumMall.Test12961Missing` 1 — строка «отсутствует», ключ Show12961 1:
//       окно 6000 (ключ запасной 1) и магазин бустеров показывают запасную 9570 — ветка «строки нет → 9570».
//   Окна-сообщения закрываются «ОК»; ключи, перехват, признаки окон и баланс возвращаются.
//   Итог `САМОТЕСТ-5071-КОД0E ИТОГ: ok=N fail=M`.
static FAutoConsoleCommandWithWorldAndArgs GMallCode0ESelfTest(
	TEXT("asda.PremiumMall.Code0ESelfTest"),
	TEXT("asda.PremiumMall.Code0ESelfTest [quit] — ответ покупки 5071 код 0x0E в окне 6000 и магазине бустеров 15300 показывает "
	     "строку 12961 («Не продаётся» / «Not For Sale» по языку сессии), а не запасную 9570"),
	FConsoleCommandWithWorldAndArgsDelegate::CreateStatic([](const TArray<FString>& A, UWorld* W)
	{
		static const TCHAR* kCmd = TEXT("asda.PremiumMall.Code0ESelfTest");
		static const TCHAR* kKeyFb = TEXT("asda.PremiumMall.Notice12961Fallback");   // запасная 9570 окна 6000 (только без строки)
		static const TCHAR* kKeyShow = TEXT("asda.PremiumMall.Show12961");            // ★ R5-fix-b 13.11.2026 (второй заход): 1 = правка, 0 = как было
		static const TCHAR* kKeyMiss = TEXT("asda.PremiumMall.Test12961Missing");     // ★ R5-fix-b 13.11.2026 (третий заход): шов «строки 12961 нет»
		const FAsdaSelfTestArgs Args = FAsdaSelfTestArgs::Parse(A);
		if (AsdaSelfTestKit::RefuseStart(W, kCmd, Args, 30.f)) { return; }
		UAsdaPremiumShopSubsystem* P = ShopSub(W);
		UGameInstance* GI0 = W ? W->GetGameInstance() : nullptr;
		UAsdaBoosterShopSubsystem* Bs = GI0 ? GI0->GetSubsystem<UAsdaBoosterShopSubsystem>() : nullptr;
		UAsdaWindowSubsystem* WM = GI0 ? GI0->GetSubsystem<UAsdaWindowSubsystem>() : nullptr;
		if (!W || !P || !Bs || !WM)
		{
			AsdaSelfTestKit::Refused(W, kCmd, Args.bQuit, TEXT("нет мира или подсистем премиум-магазина / бустеров / окон"));
			return;
		}
		if (P->IsOpen() || Bs->IsOpen())
		{
			AsdaSelfTestKit::Refused(W, kCmd, Args.bQuit, TEXT("окно 6000 или 15300 открыто игроком — самотест подменил бы его состояние"));
			return;
		}
		int32 Ok = 0, Fail = 0;   // счёт проверок
		auto Check = [&Ok, &Fail](bool bOk, const FString& What)
		{
			if (bOk) { ++Ok; } else { ++Fail; }
			UE_LOG(LogAsdaPremShop, Display, TEXT("САМОТЕСТ-5071-КОД0E %s %s"), bOk ? TEXT("OK  ") : TEXT("FAIL"), *What);
		};
		const bool bRu = FAsdaLang::IsRu();                                           // язык сессии
		const FString Want = bRu ? FString(TEXT("Не продаётся")) : FString(TEXT("Not For Sale"));   // решение владельца 13.11
		const FString Loc12961 = AsdaNotice::Loc(GI0, AsdaPremiumBuy::kResLoc5071[0x0E]);           // строка таблицы
		const FString Loc9570 = AsdaNotice::Loc(GI0, 9570);                                         // прежняя запасная
		// Тело 5071 (407 б): код, валюта, баланс @+2, подрежим 1 = в инвентарь.
		auto Reply = [](uint8 Code)
		{
			TArray<uint8> B;
			B.SetNumZeroed(407);
			B[0] = Code;
			AsdaBytes::WrI32(&B[2], -1);
			B[12] = 1;
			return B;
		};
		const int32 Queue0 = WM->GetMsgBoxQueueNum();   // очередь окон-сообщений до самотеста
		// Текст окна, которое показал ответ (верх очереди), и закрыть все новые окна «ОК».
		auto TakeBox = [WM, Queue0]()
		{
			const FString T = WM->GetMsgBoxQueueNum() > Queue0 ? WM->GetMsgBoxQueueTopText() : FString();
			for (int32 Guard = 0; Guard < 8 && WM->GetMsgBoxQueueNum() > Queue0; ++Guard) { WM->AnswerMsgBox(/*bTwoBut*/ false, /*bYes*/ true); }
			return T;
		};
		AsdaCVarSave::ArmWorldGuard(W, kCmd);
		P->DebugSetOpenFlag(true);
		Bs->DebugSetOpenFlag(true);

		// ── D: данные ──
		Check(AsdaPremiumBuy::kResLoc5071[0x0E] == 12961, FString::Printf(TEXT("D1 таблица 5071: код 0x0E → %d (эталон shop_trade.c:3550 — 12961)"),
			AsdaPremiumBuy::kResLoc5071[0x0E]));
		Check(Loc12961 == Want, FString::Printf(TEXT("D2 строка 12961 (%s) = «%s» (ждали «%s»)"), bRu ? TEXT("RU") : TEXT("EN"), *Loc12961, *Want));
		Check(!Loc9570.IsEmpty() && Loc9570 != Want, FString::Printf(TEXT("D3 контроль: запасная 9570 = «%s» — другая строка"), *Loc9570));

		// ── P: окно 6000, ключ запасной 1 и 0 ──
		AsdaCVarSave::SetForTest(kKeyShow, 1);   // ★ R5-fix-b 13.11.2026 (третий заход): P и B — при правке (ключ мог быть оставлен в 0 вручную)
		AsdaCVarSave::SetForTest(kKeyMiss, 0);
		for (const int32 Key : { 1, 0 })
		{
			AsdaCVarSave::SetForTest(kKeyFb, Key);
			P->DebugArmPending(0.0);
			P->DebugFeedBuyResult(Reply(0x0E));
			const FString T = TakeBox();
			Check(T == Want && !P->DebugIsPending(),
				FString::Printf(TEXT("P%d окно 6000, ключ запасной %d: код 0x0E → окно «%s» (ждали «%s»), защёлка снята"),
					Key ? 1 : 2, Key, *T, *Want));
		}
		AsdaCVarSave::SetForTest(kKeyFb, 1);

		// ── B: магазин бустеров 15300 ──
		Bs->DebugSetCapture(true);
		Bs->DebugConfirmBuy(53672, 1);
		const bool bArmed = Bs->DebugIsPending();
		Bs->DebugFeedBuyResult(Reply(0x0E));
		const FString TB = TakeBox();
		Check(bArmed && TB == Want && !Bs->DebugIsPending(),
			FString::Printf(TEXT("B магазин бустеров: запрос был в полёте (%d), код 0x0E → окно «%s» (ждали «%s»)"),
				bArmed ? 1 : 0, *TB, *Want));

		// ── K: ★ R5-fix-b 13.11.2026 (второй заход) — контроль «без правки»: ключ Show12961 = 0 возвращает поведение до 13.11.2026 ──
		//   (запасная 9570 безусловно). Проверки P1 и B при этом ключе были бы красными: окно показывает 9570, а не 12961.
		AsdaCVarSave::SetForTest(kKeyShow, 0);
		AsdaCVarSave::SetForTest(kKeyFb, 1);
		P->DebugArmPending(0.0);
		P->DebugFeedBuyResult(Reply(0x0E));
		const FString TK1 = TakeBox();
		Check(TK1 == Loc9570 && TK1 != Want, FString::Printf(TEXT("K1 ключ Show12961 0 (как было): окно 6000 → «%s» = запасная 9570, не «%s» (P1 был бы красным)"),
			*TK1, *Want));
		Bs->DebugSetCapture(true);
		Bs->DebugConfirmBuy(53672, 1);
		const bool bArmedK2 = Bs->DebugIsPending();   // ★ R5-fix-b 13.11.2026 (третий заход): запрос был в полёте (замечание ревизора)
		Bs->DebugFeedBuyResult(Reply(0x0E));
		const FString TK2 = TakeBox();
		Check(bArmedK2 && TK2 == Loc9570 && TK2 != Want, FString::Printf(TEXT("K2 ключ Show12961 0 (как было): бустеры (запрос в полёте %d) → «%s» = запасная 9570, не «%s» (B был бы красным)"),
			bArmedK2 ? 1 : 0, *TK2, *Want));
		AsdaCVarSave::SetForTest(kKeyShow, 1);

		// ── M: ★ R5-fix-b 13.11.2026 (третий заход) — ветка «строки 12961 нет в таблице → 9570» при ключе Show12961 = 1 ──
		//   Шов `asda.PremiumMall.Test12961Missing` = 1 (только дев): проверка видит строку отсутствующей, ассет не подменяется.
		AsdaCVarSave::SetForTest(kKeyMiss, 1);
		AsdaCVarSave::SetForTest(kKeyFb, 1);
		P->DebugArmPending(0.0);
		P->DebugFeedBuyResult(Reply(0x0E));
		const FString TM1 = TakeBox();
		Check(TM1 == Loc9570 && TM1 != Want && !P->DebugIsPending(),
			FString::Printf(TEXT("M1 строки 12961 «нет» (шов), ключ Show12961 1, запасной 1: окно 6000 → «%s» = запасная 9570"), *TM1));
		Bs->DebugSetCapture(true);
		Bs->DebugConfirmBuy(53672, 1);
		const bool bArmedM2 = Bs->DebugIsPending();   // ★ R5-fix-b 13.11.2026 (заключительный заход) (ревизор третьего захода, зам. 4.1): запрос был в полёте
		Bs->DebugFeedBuyResult(Reply(0x0E));
		const FString TM2 = TakeBox();
		Check(bArmedM2 && TM2 == Loc9570 && TM2 != Want && !Bs->DebugIsPending(),
			FString::Printf(TEXT("M2 строки 12961 «нет» (шов), ключ Show12961 1: бустеры (запрос в полёте %d) → «%s» = запасная 9570"), bArmedM2 ? 1 : 0, *TM2));
		AsdaCVarSave::SetForTest(kKeyMiss, 0);
		// ★ R5-fix-b 13.11.2026 (заключительный заход) (BRIEF4 п. 6): настоящую пустую строку 12961 без подмены ассета не получить — текст берётся из
		//   `DT_GameText` (RU) и языковой таблицы `FAsdaLang::ResolveId` (EN); вырезать строку можно только правкой загруженного
		//   ассета в памяти. Поэтому ветка идёт швом (M1/M2), а сам предикат ветки — здесь, на настоящей пустой строке при шве 0.
		Check(AsdaPremium12961Missing(FString()) && !AsdaPremium12961Missing(Want),
			FString::Printf(TEXT("M3 шов 0: пустой текст 12961 → «строки нет» (ветка 9570); записанная «%s» → строка есть"), *Want));

		// Возврат: перехват, признаки окон, защёлки, баланс.
		P->DebugClearPending(); P->DebugSetGxp(0); P->DebugSetOpenFlag(false);
		Bs->DebugSetCapture(false); Bs->DebugSetGxp(0); Bs->DebugSetOpenFlag(false);
		AsdaSelfTestKit::Finish(   // возврат ключей (RestoreAll снимает и сторожа мира) + итог + quit
			W, kCmd, Args.bQuit, LogAsdaPremShop, TEXT("САМОТЕСТ-5071-КОД0E"), Ok, Fail);
	}));
#endif // ASDA_DEV_CONSOLE

#if ASDA_DEV_CONSOLE   // ★ античит А2: дев-команда, в Shipping не регистрируется
// ★★ 26.10.2026 САМОТЕСТ «ИМЕНА БУСТЕРОВ ПО СОСТАВУ» (решение владельца, Reports/booster_bundles/AUDIT.md §9).
//   Путь игрока, язык сессии (пакет `--lang ru` / `--lang en`):
//     D данные: `GetItemName` (им пользуются витрины 6000/15300, премиум-почта, сумка) и заголовок/тело подсказки
//       `BuildTooltipByCode` (тот же сборщик, что у показа подсказки) у 33 кодов = новое имя/описание;
//     P окно 6000: открыто, обход категорий 58200 × подразделов × страниц — у каждой карточки с целевым кодом в окне виден
//       текст с новым именем, ни одного видимого текста со старым;
//     B окно 15300: обход вкладок × страниц — видны «копья заклинателя» и «меча и щита стража правосудия», старых нет.
//   Итог `САМОТЕСТ-ИМЕНА-БУСТЕРОВ ИТОГ: ok=N fail=M`; окна закрываются.
static FAutoConsoleCommandWithWorldAndArgs GMallBoosterNamesSelfTest(
	TEXT("asda.PremiumMall.BoosterNamesSelfTest"),
	TEXT("asda.PremiumMall.BoosterNamesSelfTest [quit] — новые имена бустеров (75816–75824, 75978–75980, 67859 и др.) в данных, "
	     "подсказке, витрине 6000 и магазине бустеров 15300 на языке сессии"),
	FConsoleCommandWithWorldAndArgsDelegate::CreateStatic([](const TArray<FString>& A, UWorld* W)
	{
		static const TCHAR* kCmd = TEXT("asda.PremiumMall.BoosterNamesSelfTest");
		const FAsdaSelfTestArgs Args = FAsdaSelfTestArgs::Parse(A);
		if (AsdaSelfTestKit::RefuseStart(W, kCmd, Args, 60.f)) { return; }
		UAsdaPremiumShopSubsystem* P = ShopSub(W);
		UGameInstance* GI0 = W ? W->GetGameInstance() : nullptr;
		UAsdaBoosterShopSubsystem* Bs = GI0 ? GI0->GetSubsystem<UAsdaBoosterShopSubsystem>() : nullptr;
		const UAsdaItemTableSubsystem* IT = GI0 ? GI0->GetSubsystem<UAsdaItemTableSubsystem>() : nullptr;
		const UAsdaItemTooltipSubsystem* Tip = GI0 ? GI0->GetSubsystem<UAsdaItemTooltipSubsystem>() : nullptr;
		if (!W || !P || !Bs || !IT || !Tip)
		{
			AsdaSelfTestKit::Refused(W, kCmd, Args.bQuit, TEXT("нет мира или подсистем магазинов / таблиц / подсказки"));
			return;
		}
		if (P->IsOpen() || Bs->IsOpen())
		{
			AsdaSelfTestKit::Refused(W, kCmd, Args.bQuit, TEXT("окно 6000 или 15300 открыто игроком"));
			return;
		}
		const bool bRu = FAsdaLang::IsRu();
		// Ожидание по коду: имя RU, имя EN, описание RU, описание EN (пусто — описание не проверяется).
		struct FExp { int32 Code; const TCHAR* RuName; const TCHAR* EnName; const TCHAR* RuDesc; const TCHAR* EnDesc; };
		TArray<FExp> Exp;
		// ★ §11 (26.10, вариант (а) владельца): призы — наборы и части брони; у 75821/75822 брони среди призов нет.
		for (int32 C = 75816; C <= 75824; ++C)
		{
			if (C == 75821 || C == 75822)
			{
				Exp.Add({ C, TEXT("Бустер наборов"), TEXT("Booster - Package Set"), TEXT("Бустер наборов"), TEXT("Booster - Package Set") });
			}
			else
			{
				Exp.Add({ C, TEXT("Бустер наборов и снаряжения"), TEXT("Booster - Packages and Gear"),
					TEXT("Бустер наборов и снаряжения"), TEXT("Booster - Packages and Gear") });
			}
		}
		for (int32 C : { 75978, 75979, 75980 })
		{
			Exp.Add({ C, TEXT("Бустер доспехов демонов"), TEXT("Booster - Demon Set"), TEXT("Бустер доспехов демонов"), TEXT("Booster - Demon Set") });
		}
		for (int32 C : { 65393, 65402, 65411, 65420, 65429, 67859, 67868, 67877 })
		{
			Exp.Add({ C, TEXT("Бустер копья заклинателя"), TEXT("Booster - Exorcist Spear"), TEXT(""), TEXT("") });
		}
		for (int32 C : { 62728, 62737, 62746 })
		{
			Exp.Add({ C, TEXT("Бустер меча и щита стража правосудия"), TEXT("Booster-Justice Guard Sword,Shield"),
				TEXT("Этот бустер меча и щита изготовили"), TEXT("booster sword and shield") });
		}
		Exp.Add({ 75503, TEXT("Бустер динозавруса"), TEXT("Hatchling Booster"), TEXT("Бустер динозавруса"), TEXT("Hatchling Booster") });
		// Старые имена (на текущем языке их не должно быть видно нигде в окнах).
		const TArray<FString> Old = bRu
			? TArray<FString>{ TEXT("демона"), TEXT("Бустер брони заклинателя"), TEXT("Бустер меча стража правосудия"),
			                   TEXT("Рога северного оленя"), TEXT("Серебряный Набор для улучшения") }
			: TArray<FString>{ TEXT("Booster-Demon") };
		int32 Ok = 0, Fail = 0;   // счёт проверок
		auto Check = [&Ok, &Fail](bool bOk, const FString& What)
		{
			if (bOk) { ++Ok; } else { ++Fail; }
			UE_LOG(LogAsdaPremShop, Display, TEXT("САМОТЕСТ-ИМЕНА-БУСТЕРОВ %s %s"), bOk ? TEXT("OK  ") : TEXT("FAIL"), *What);
		};
		// ── D: данные и подсказка ──
		for (const FExp& E : Exp)
		{
			const FString Want = bRu ? E.RuName : E.EnName;
			const FString Got = IT->GetItemName(E.Code);
			const FAsdaItemTooltipText T = Tip->BuildTooltipByCode(E.Code);
			const FString WantDesc = bRu ? E.RuDesc : E.EnDesc;
			const bool bDesc = WantDesc.IsEmpty() || T.Body.Contains(WantDesc);
			Check(Got == Want && T.Title.Contains(Want) && bDesc,
				FString::Printf(TEXT("D %d [%s]: имя «%s» (ждём «%s»), заголовок подсказки «%s», описание %s"),
					E.Code, bRu ? TEXT("ru") : TEXT("en"), *Got, *Want, *T.Title,
					WantDesc.IsEmpty() ? TEXT("не проверяется") : (bDesc ? TEXT("с новым текстом") : *FString::Printf(TEXT("БЕЗ «%s»: «%s»"), *WantDesc, *T.Body.Left(120)))));
		}
		// Видимые тексты окна по части имени класса виджета (WBP_SHELLITEM_STORE_WIN / WBP_BOOSTER_SHOP_WIN).
		auto Texts = [W](const TCHAR* ClassPart)
		{
			TArray<FString> Out;
			for (TObjectIterator<UUserWidget> It; It; ++It)
			{
				UUserWidget* U = *It;
				if (!U || U->GetWorld() != W || !U->IsInViewport() || !U->GetClass()->GetName().Contains(ClassPart) || !U->WidgetTree) { continue; }
				TArray<UWidget*> All;
				U->WidgetTree->GetAllWidgets(All);
				for (UWidget* Wd : All)
				{
					const UTextBlock* Tb = Cast<UTextBlock>(Wd);
					if (!Tb) { continue; }
					bool bVis = true;   // видим сам узел и вся цепочка родителей
					for (const UWidget* Up = Tb; Up; Up = Up->GetParent()) { if (!Up->IsVisible()) { bVis = false; break; } }
					if (bVis) { Out.Add(Tb->GetText().ToString()); }
				}
			}
			return Out;
		};
		auto HasOld = [&Old](const TArray<FString>& T, FString& Hit)
		{
			for (const FString& S : T) { for (const FString& O : Old) { if (S.Contains(O)) { Hit = S; return true; } } }
			return false;
		};
		// ── P: окно 6000 ──
		P->OpenWindow();
		TSet<int32> Seen, SeenOk;   // целевые коды на карточках / с новым именем в окне
		FString OldHit6000;
		const TSet<int32> Shop6000 = { 75816, 75817, 75818, 75819, 75820, 75821, 75822, 75823, 75824, 75978, 75979, 75980, 75503, 62728, 67859 };
		for (int32 Cat = 0; Cat < 8; ++Cat)
		{
			for (int32 Sub = 0; Sub < 5; ++Sub)
			{
				P->SetCategory(Cat);
				if (P->GetCategory() == 3) { if (Sub > 2) { continue; } P->SetEquipSub(Sub); } else { P->SetToolSub(Sub); }
				for (int32 Pg = 0; Pg < 20; ++Pg)
				{
					P->SetPage(Pg);
					const TArray<FString> T = Texts(TEXT("SHELLITEM_STORE"));
					FString Hit;
					if (OldHit6000.IsEmpty() && HasOld(T, Hit)) { OldHit6000 = Hit; }
					for (int32 Card = 0; Card < UAsdaPremiumShopSubsystem::kCards; ++Card)
					{
						const int32 Code = P->DebugCardCode(Card);
						if (!Shop6000.Contains(Code)) { continue; }
						Seen.Add(Code);
						const FExp* E = Exp.FindByPredicate([Code](const FExp& X) { return X.Code == Code; });
						if (E && T.Contains(bRu ? E->RuName : E->EnName)) { SeenOk.Add(Code); }
					}
				}
			}
		}
		P->CloseWindow();
		Check(Seen.Num() > 0 && SeenOk.Num() == Seen.Num(),
			FString::Printf(TEXT("P окно 6000: целевых карточек найдено %d, с новым именем в окне %d"), Seen.Num(), SeenOk.Num()));
		Check(OldHit6000.IsEmpty(), FString::Printf(TEXT("P окно 6000: старых имён на экране нет%s"),
			OldHit6000.IsEmpty() ? TEXT("") : *FString::Printf(TEXT(" — НАЙДЕНО «%s»"), *OldHit6000)));
		// ── B: окно 15300 ──
		Bs->OpenWindow();
		bool bSpear = false, bSword = false;
		FString OldHit15300;
		for (int32 Cat = 0; Cat < 7; ++Cat)
		{
			Bs->SetCategory(Cat);
			for (int32 Pg = 1; Pg <= 12; ++Pg)
			{
				Bs->SetPage(Pg);
				const TArray<FString> T = Texts(TEXT("BOOSTER_SHOP"));
				FString Hit;
				if (OldHit15300.IsEmpty() && HasOld(T, Hit)) { OldHit15300 = Hit; }
				bSpear |= T.Contains(bRu ? TEXT("Бустер копья заклинателя") : TEXT("Booster - Exorcist Spear"));
				bSword |= T.Contains(bRu ? TEXT("Бустер меча и щита стража правосудия") : TEXT("Booster-Justice Guard Sword,Shield"));
			}
		}
		Bs->CloseWindow();
		Check(bSpear && bSword, FString::Printf(TEXT("B окно 15300: «копья заклинателя» %s, «меча и щита стража правосудия» %s"),
			bSpear ? TEXT("видно") : TEXT("НЕТ"), bSword ? TEXT("видно") : TEXT("НЕТ")));
		Check(OldHit15300.IsEmpty(), FString::Printf(TEXT("B окно 15300: старых имён на экране нет%s"),
			OldHit15300.IsEmpty() ? TEXT("") : *FString::Printf(TEXT(" — НАЙДЕНО «%s»"), *OldHit15300)));
		AsdaSelfTestKit::Finish(W, kCmd, Args.bQuit, LogAsdaPremShop, TEXT("САМОТЕСТ-ИМЕНА-БУСТЕРОВ"), Ok, Fail);
	}));
#endif // ASDA_DEV_CONSOLE

#if ASDA_DEV_CONSOLE   // ★ античит А2: дев-команда, в Shipping не регистрируется
// ★★ 26.10.2026 САМОТЕСТ «ИМЕНА КОРОБОК ПО СОДЕРЖИМОМУ» (решение владельца, Reports/booster_bundles/AUDIT.md §10).
//   Язык сессии (пакет `--lang ru` / `--lang en`). Для 154 кодов волны:
//     N имя `GetItemName` (витрины, почта, сумка) и заголовок подсказки `BuildTooltipByCode` = ожидаемому;
//     V у 41 коробки вскрытие: `GetPackageContents` и панель состава 26500 (`Populate`, режим 0 «превью») показывают
//       ровно содержимое; у одиночной коробки имя начинается с имени содержимого и несёт «xN» (RU) / «*N» (EN),
//       у коробок оружия капитана все 5 предметов — оружие капитана.
//   Итог `САМОТЕСТ-ИМЕНА-КОРОБОК ИТОГ: ok=N fail=M`.
static FAutoConsoleCommandWithWorldAndArgs GMallBoxNamesSelfTest(
	TEXT("asda.PremiumMall.BoxNamesSelfTest"),
	TEXT("asda.PremiumMall.BoxNamesSelfTest [quit] — имена коробок 75701–75796 / 75966–75971 и EN-имена брони по содержимому: "
	     "имя, подсказка, вскрытие (панель 26500) на языке сессии"),
	FConsoleCommandWithWorldAndArgsDelegate::CreateStatic([](const TArray<FString>& A, UWorld* W)
	{
		static const TCHAR* kCmd = TEXT("asda.PremiumMall.BoxNamesSelfTest");
		const FAsdaSelfTestArgs Args = FAsdaSelfTestArgs::Parse(A);
		if (AsdaSelfTestKit::RefuseStart(W, kCmd, Args, 60.f)) { return; }
		UGameInstance* GI0 = W ? W->GetGameInstance() : nullptr;
		const UAsdaItemTableSubsystem* IT = GI0 ? GI0->GetSubsystem<UAsdaItemTableSubsystem>() : nullptr;
		const UAsdaItemTooltipSubsystem* Tip = GI0 ? GI0->GetSubsystem<UAsdaItemTooltipSubsystem>() : nullptr;
		UAsdaPackagePanelSubsystem* Pkg = GI0 ? GI0->GetSubsystem<UAsdaPackagePanelSubsystem>() : nullptr;
		if (!W || !IT || !Tip || !Pkg)
		{
			AsdaSelfTestKit::Refused(W, kCmd, Args.bQuit, TEXT("нет мира или подсистем таблиц / подсказки / панели состава"));
			return;
		}
		if (Pkg->IsPanelOpen())
		{
			AsdaSelfTestKit::Refused(W, kCmd, Args.bQuit, TEXT("панель состава 26500 открыта игроком"));
			return;
		}
		const bool bRu = FAsdaLang::IsRu();
		// Ожидание: код, имя RU, имя EN, 1 = коробка волны (проверяется вскрытие). Таблица снята с плана
		//   rename_boxes_20261026.py (Reports/booster_bundles) — не с самих таблиц, чтобы ловить незалитое.
		struct FExp { int32 Code; const TCHAR* Ru; const TCHAR* En; int32 bBox; };
		static const FExp Exp[] =
		{
			// ★ §11 (26.10): наборы 75656–75665 и коробки 76303–76308 по содержимому (близнецы 76651–76656, 76327).
			{ 75656, TEXT("Бонусный набор снеговика x5"), TEXT("Snowman Bonus Package*5"), 1 },
			{ 75657, TEXT("Бонусный набор оленя"), TEXT("Reindeer Bonus Package"), 1 },
			{ 75658, TEXT("Бонусный набор оленя"), TEXT("Reindeer Bonus Package"), 1 },
			{ 75659, TEXT("Бонусный набор оленя"), TEXT("Reindeer Bonus Package"), 1 },
			{ 75660, TEXT("Бонусный набор оленя"), TEXT("Reindeer Bonus Package"), 1 },
			{ 75661, TEXT("Бонусный набор оленя"), TEXT("Reindeer Bonus Package"), 1 },
			{ 75662, TEXT("Бонусный набор оленя"), TEXT("Reindeer Bonus Package"), 1 },
			{ 75663, TEXT("Бонусный набор оленя"), TEXT("Reindeer Bonus Package"), 1 },
			{ 75664, TEXT("Бонусный набор оленя"), TEXT("Reindeer Bonus Package"), 1 },
			{ 75665, TEXT("Бонусный набор оленя"), TEXT("Reindeer Bonus Package"), 1 },
			{ 76303, TEXT("Бонусный набор снеговика"), TEXT("Snowman Bonus Package"), 1 },
			{ 76304, TEXT("Бонусный набор снеговика"), TEXT("Snowman Bonus Package"), 1 },
			{ 76305, TEXT("Бонусный набор снеговика (маг, ур. 40)"), TEXT("Snowman Bonus Package (Mage, Lv. 40)"), 1 },
			{ 76306, TEXT("Бонусный набор снеговика (маг, ур. 40)"), TEXT("Snowman Bonus Package (Mage, Lv. 40)"), 1 },
			{ 76307, TEXT("Бонусный набор снеговика"), TEXT("Snowman Bonus Package"), 1 },
			{ 76308, TEXT("Бонусный набор снеговика (маг, ур. 40)"), TEXT("Snowman Bonus Package (Mage, Lv. 40)"), 1 },
			{ 23755, TEXT("Подарки друзей"), TEXT("Friends' Gifts"), 0 },
			{ 23756, TEXT("Подарки друзей (5 уровень)"), TEXT("Friends' Gifts (Level 5)"), 0 },
			{ 75477, TEXT("Драгоценный камень уровня D (10)"), TEXT("D-ranked Jewel(10)"), 1 },
			{ 75478, TEXT("Драгоценный камень уровня C (8)"), TEXT("C-ranked Jewel(8)"), 1 },   // ★ §12 Н6: внутри ×8
			{ 75479, TEXT("Драгоценный камень уровня B (5)"), TEXT("B-ranked Jewel(5)"), 1 },   // ★ §12 Н6: внутри ×5
			{ 75480, TEXT("Драгоценный камень уровня A (2)"), TEXT("A-ranked Jewel(2)"), 1 },   // ★ §12 Н6: внутри ×2
			{ 75644, TEXT("Костюм оленя-капитана"), TEXT("Captain Reindeer Set"), 1 },   // ★ §12 Н5: внутри наряд оленя-капитана
			{ 75671, TEXT("Шлем ловкача"), TEXT("Stalker's Helmet"), 0 },
			{ 75672, TEXT("Кираса ловкача"), TEXT("Stalker's Cuirass"), 0 },
			{ 75673, TEXT("Рукавицы ловкача"), TEXT("Stalker's Gauntlets"), 0 },
			{ 75674, TEXT("Набедренники ловкача"), TEXT("Stalker's Tasset"), 0 },
			{ 75675, TEXT("Ботинки ловкача"), TEXT("Stalker's Boots"), 0 },
			{ 75676, TEXT("Шлем ловкача"), TEXT("Stalker's Helmet"), 0 },
			{ 75677, TEXT("Кираса ловкача"), TEXT("Stalker's Cuirass"), 0 },
			{ 75678, TEXT("Рукавицы ловкача"), TEXT("Stalker's Gauntlets"), 0 },
			{ 75679, TEXT("Набедренники ловкача"), TEXT("Stalker's Tasset"), 0 },
			{ 75680, TEXT("Ботинки ловкача"), TEXT("Stalker's Boots"), 0 },
			{ 75681, TEXT("Шлем ловкача"), TEXT("Stalker's Helmet"), 0 },
			{ 75682, TEXT("Кираса ловкача"), TEXT("Stalker's Cuirass"), 0 },
			{ 75683, TEXT("Рукавицы ловкача"), TEXT("Stalker's Gauntlets"), 0 },
			{ 75684, TEXT("Набедренники ловкача"), TEXT("Stalker's Tasset"), 0 },
			{ 75685, TEXT("Ботинки ловкача"), TEXT("Stalker's Boots"), 0 },
			{ 75696, TEXT("Шлем ловкача"), TEXT("Stalker's Helmet"), 0 },
			{ 75697, TEXT("Кираса ловкача"), TEXT("Stalker's Cuirass"), 0 },
			{ 75698, TEXT("Рукавицы ловкача"), TEXT("Stalker's Gauntlets"), 0 },
			{ 75699, TEXT("Набедренники ловкача"), TEXT("Stalker's Tasset"), 0 },
			{ 75700, TEXT("Ботинки ловкача"), TEXT("Stalker's Boots"), 0 },
			{ 75701, TEXT("Зелье увеличения ОЗ на 20% (3) x5"), TEXT("HP 20% Increase potion(3)*5"), 1 },
			{ 75702, TEXT("Зелье увеличения ОМ на 20% (3) x5"), TEXT("MP 20% Increase potion(3)*5"), 1 },
			{ 75703, TEXT("Зелье лечения (100) x5"), TEXT("Reg. Potion(100)*5"), 1 },
			{ 75704, TEXT("Эликсир здоровья (100) x5"), TEXT("HP Elixir(100)*5"), 1 },
			{ 75705, TEXT("Зелье ОЗ (100) x5"), TEXT("HP Element Potion(100)*5"), 1 },
			{ 75706, TEXT("Тайное зелье ОЗ (100) x5"), TEXT("Mystic HP Element Potion(100)*5"), 1 },
			{ 75707, TEXT("Шлем Асуры"), TEXT("Asura's Helmet"), 0 },
			{ 75708, TEXT("Кираса Асуры"), TEXT("Asura's Armor"), 0 },
			{ 75709, TEXT("Рукавицы Асуры"), TEXT("Asura's Gauntlet"), 0 },
			{ 75710, TEXT("Набедренники Асуры"), TEXT("Asura's Toe set"), 0 },
			{ 75711, TEXT("Ботинки Асуры"), TEXT("Asura's Boots"), 0 },
			{ 75712, TEXT("Шлем Асуры"), TEXT("Asura's Helmet"), 0 },
			{ 75713, TEXT("Кираса Асуры"), TEXT("Asura's Armor"), 0 },
			{ 75714, TEXT("Рукавицы Асуры"), TEXT("Asura's Gauntlet"), 0 },
			{ 75715, TEXT("Набедренники Асуры"), TEXT("Asura's Toe set"), 0 },
			{ 75716, TEXT("Ботинки Асуры"), TEXT("Asura's Boots"), 0 },
			{ 75717, TEXT("Шлем Асуры"), TEXT("Asura's Helmet"), 0 },
			{ 75718, TEXT("Кираса Асуры"), TEXT("Asura's Armor"), 0 },
			{ 75719, TEXT("Рукавицы Асуры"), TEXT("Asura's Gauntlet"), 0 },
			{ 75720, TEXT("Набедренники Асуры"), TEXT("Asura's Toe set"), 0 },
			{ 75721, TEXT("Ботинки Асуры"), TEXT("Asura's Boots"), 0 },
			{ 75722, TEXT("Шлем Асуры"), TEXT("Asura's Helmet"), 0 },
			{ 75723, TEXT("Кираса Асуры"), TEXT("Asura's Armor"), 0 },
			{ 75724, TEXT("Рукавицы Асуры"), TEXT("Asura's Gauntlet"), 0 },
			{ 75725, TEXT("Набедренники Асуры"), TEXT("Asura's Toe set"), 0 },
			{ 75736, TEXT("Ботинки Асуры"), TEXT("Asura's Boots"), 0 },
			{ 75737, TEXT("Тайный эликсир ОЗ (100) x5"), TEXT("Mystic HP Elixir(100)*5"), 1 },
			{ 75738, TEXT("Тайное зелье восстан. (100) x5"), TEXT("Mystic Reg. Potion(100)*5"), 1 },
			{ 75739, TEXT("Тайное зелье ОЗ (500) x2"), TEXT("Mystic HP Element Potion(500*2)"), 1 },
			{ 75740, TEXT("Тайный эликсир ОЗ (500) x2"), TEXT("Mystic HP Elixir(500*2)"), 1 },
			{ 75741, TEXT("Тайное зелье лечения (500) x2"), TEXT("Mystic Reg. Potion(500*2)"), 1 },
			{ 75742, TEXT("Зелье защиты (30) x2"), TEXT("Defense Potion (30)*2"), 1 },
			{ 75743, TEXT("Шлем заклинателя адского пламени"), TEXT("Hell Flame Exorcist Helmet"), 0 },
			{ 75744, TEXT("Кираса заклинателя адского пламени"), TEXT("Hell Flame Exorcist Cuirass"), 0 },
			{ 75745, TEXT("Рукавицы заклинателя адского пламени"), TEXT("Hell Flame Exorcist Gauntlets"), 0 },
			{ 75746, TEXT("Набедренники заклинателя ада"), TEXT("Hell Flame Exorcist Tasset"), 0 },
			{ 75747, TEXT("Ботинки заклинателя адского пламени"), TEXT("Hell Flame Exorcist Boots"), 0 },
			{ 75748, TEXT("Шлем заклинателя адского пламени"), TEXT("Hell Flame Exorcist Helmet"), 0 },
			{ 75749, TEXT("Кираса заклинателя адского пламени"), TEXT("Hell Flame Exorcist Cuirass"), 0 },
			{ 75750, TEXT("Рукавицы заклинателя адского пламени"), TEXT("Hell Flame Exorcist Gauntlets"), 0 },
			{ 75751, TEXT("Набедренники заклинателя ада"), TEXT("Hell Flame Exorcist Tasset"), 0 },
			{ 75752, TEXT("Ботинки заклинателя адского пламени"), TEXT("Hell Flame Exorcist Boots"), 0 },
			{ 75753, TEXT("Шлем заклинателя адского пламени"), TEXT("Hell Flame Exorcist Helmet"), 0 },
			{ 75754, TEXT("Кираса заклинателя адского пламени"), TEXT("Hell Flame Exorcist Cuirass"), 0 },
			{ 75755, TEXT("Рукавицы заклинателя адского пламени"), TEXT("Hell Flame Exorcist Gauntlets"), 0 },
			{ 75756, TEXT("Набедренники заклинателя ада"), TEXT("Hell Flame Exorcist Tasset"), 0 },
			{ 75757, TEXT("Ботинки заклинателя адского пламени"), TEXT("Hell Flame Exorcist Boots"), 0 },
			{ 75758, TEXT("Шлем заклинателя адского пламени"), TEXT("Hell Flame Exorcist Helmet"), 0 },
			{ 75759, TEXT("Кираса заклинателя адского пламени"), TEXT("Hell Flame Exorcist Cuirass"), 0 },
			{ 75760, TEXT("Рукавицы заклинателя адского пламени"), TEXT("Hell Flame Exorcist Gauntlets"), 0 },
			{ 75761, TEXT("Набедренники заклинателя ада"), TEXT("Hell Flame Exorcist Tasset"), 0 },
			{ 75762, TEXT("Ботинки заклинателя адского пламени"), TEXT("Hell Flame Exorcist Boots"), 0 },
			{ 75763, TEXT("Шлем заклинателя адского пламени"), TEXT("Hell Flame Exorcist Helmet"), 0 },
			{ 75764, TEXT("Кираса заклинателя адского пламени"), TEXT("Hell Flame Exorcist Cuirass"), 0 },
			{ 75765, TEXT("Рукавицы заклинателя адского пламени"), TEXT("Hell Flame Exorcist Gauntlets"), 0 },
			{ 75773, TEXT("Зелье роста ОЗ на 20% (5+1) x3"), TEXT("HP 20% Increase potion(5+1)*3"), 1 },
			{ 75774, TEXT("Зелье роста ОМ на 20% (5+1) x3"), TEXT("MP 20% Increase potion(5+1)*3"), 1 },
			{ 75775, TEXT("Свиток призыва (100) x2"), TEXT("Recall Scroll(100)*2"), 1 },
			{ 75776, TEXT("Свиток перемещения (100) x2"), TEXT("Teleport Scroll(100)*2"), 1 },
			{ 75777, TEXT("Сокращение общего веса x4"), TEXT("Retention Weight Increase*4"), 1 },
			{ 75778, TEXT("Увеличение слотов хранения x4"), TEXT("Extend Storage Slot*4"), 1 },
			{ 75779, TEXT("Зелье опыта (30) x2"), TEXT("Piled Exp Potion(30)*2"), 1 },
			{ 75780, TEXT("Набор снятия ограничения питомцев (5) x2"), TEXT("Pet Limit-Breaking Charm Package(5)*2"), 1 },
			{ 75781, TEXT("Серебряный плюс-набор физ.атаки"), TEXT("Silver Physical Plus Package"), 1 },
			{ 75782, TEXT("Серебряный плюс-набор маг.атаки"), TEXT("Silver Magical Plus Package"), 1 },
			{ 75783, TEXT("Серебряный плюс-набор (обычный)"), TEXT("Silver Normal Plus Package"), 1 },
			{ 75784, TEXT("Случайникус (10) x3"), TEXT("Option Exchange Scroll(10)*3"), 1 },
			{ 75785, TEXT("Успешное улучшение (5) x3"), TEXT("Successful Upgrades(5)*3"), 1 },
			{ 75786, TEXT("Дополнительное улучшение (5) x3"), TEXT("Additional Upgrades(5)*3"), 1 },
			{ 75787, TEXT("Свиток ремонта брони (15) x3"), TEXT("Armor Restore Scroll(15)*3"), 1 },
			{ 75788, TEXT("Свиток ремонта оружия (15) x3"), TEXT("Weapon Restore Scroll(15)*3"), 1 },
			{ 75789, TEXT("Случайникус (10) x5"), TEXT("Option Exchange Scroll(10)*5"), 1 },
			{ 75791, TEXT("Свиток снятия лимита питомца x2"), TEXT("Pet Level Limit-Break Scroll*2"), 1 },
			{ 75792, TEXT("Свиток ремонта брони (30) x2"), TEXT("Armor Restore Scroll(30)*2"), 1 },
			{ 75793, TEXT("Свиток ремонта оружия (30) x2"), TEXT("Weapon Restore Scroll(30)*2"), 1 },
			{ 75794, TEXT("Серебряный набор для улучшения"), TEXT("Silver Upgrade Package"), 1 },
			{ 75795, TEXT("Золотой плюс-набор физ.атаки"), TEXT("Gold Physical Plus Package"), 1 },
			{ 75796, TEXT("Золотой плюс-набор маг.атаки"), TEXT("Gold Magical Plus Package"), 1 },
			{ 75798, TEXT("Шлем индейского наряда"), TEXT("Indian Helmet"), 0 },
			{ 75799, TEXT("Рубаха индейского наряда"), TEXT("Indian Shirt"), 0 },
			{ 75800, TEXT("Перчатки индейского наряда"), TEXT("Indian Gloves"), 0 },
			{ 75801, TEXT("Низ индейского наряда"), TEXT("Indian Bottoms"), 0 },
			{ 75802, TEXT("Тапочки индейского наряда"), TEXT("Indian Slippers"), 0 },
			{ 75803, TEXT("Шлем индейского наряда"), TEXT("Indian Helmet"), 0 },
			{ 75804, TEXT("Рубаха индейского наряда"), TEXT("Indian Shirt"), 0 },
			{ 75805, TEXT("Перчатки индейского наряда"), TEXT("Indian Gloves"), 0 },
			{ 75852, TEXT("Головной убор пиратского наряда"), TEXT("Pirate Bandana"), 0 },
			{ 75853, TEXT("Рубашка пиратского наряда"), TEXT("Pirate Shirt"), 0 },
			{ 75854, TEXT("Перчатки пиратского наряда"), TEXT("Pirate Gloves"), 0 },
			{ 75855, TEXT("Штаны пиратского наряда"), TEXT("Pirate Pants"), 0 },
			{ 75856, TEXT("Ботинки пиратского наряда"), TEXT("Pirate Boots"), 0 },
			{ 75857, TEXT("Головной убор пиратского наряда"), TEXT("Pirate Bandana"), 0 },
			{ 75858, TEXT("Рубашка пиратского наряда"), TEXT("Pirate Shirt"), 0 },
			{ 75859, TEXT("Перчатки пиратского наряда"), TEXT("Pirate Gloves"), 0 },
			{ 75860, TEXT("Штаны пиратского наряда"), TEXT("Pirate Pants"), 0 },
			{ 75861, TEXT("Ботинки пиратского наряда"), TEXT("Pirate Boots"), 0 },
			{ 75862, TEXT("Головной убор пиратского наряда"), TEXT("Pirate Bandana"), 0 },
			{ 75863, TEXT("Рубашка пиратского наряда"), TEXT("Pirate Shirt"), 0 },
			{ 75864, TEXT("Перчатки пиратского наряда"), TEXT("Pirate Gloves"), 0 },
			{ 75865, TEXT("Штаны пиратского наряда"), TEXT("Pirate Pants"), 0 },
			{ 75866, TEXT("Ботинки пиратского наряда"), TEXT("Pirate Boots"), 0 },
			{ 75966, TEXT("Набор оружия капитана (копье, меч, двуручный меч, щит)"), TEXT("Captain's Weapon Set (Spear, Sword, Blade, Shield)"), 1 },
			{ 75967, TEXT("Набор оружия капитана (баллиста, лук, арбалет)"), TEXT("Captain's Weapon Set (Ballista, Bow, Crossbow)"), 1 },
			{ 75968, TEXT("Набор оружия капитана (посох, трость, жезл)"), TEXT("Captain's Weapon Set (Staff, Stick, Rod)"), 1 },
			{ 75969, TEXT("Набор оружия капитана (копье, меч, двуручный меч, щит)"), TEXT("Captain's Weapon Set (Spear, Sword, Blade, Shield)"), 1 },
			{ 75970, TEXT("Набор оружия капитана (баллиста, лук, арбалет)"), TEXT("Captain's Weapon Set (Ballista, Bow, Crossbow)"), 1 },
			{ 75971, TEXT("Набор оружия капитана (посох, трость, жезл)"), TEXT("Captain's Weapon Set (Staff, Stick, Rod)"), 1 },
			{ 75972, TEXT("Набор оружия капитана (копье, меч, двуручный меч, щит)"), TEXT("Captain's Weapon Set (Spear, Sword, Blade, Shield)"), 1 },
			{ 75973, TEXT("Набор оружия капитана (баллиста, лук, арбалет)"), TEXT("Captain's Weapon Set (Ballista, Bow, Crossbow)"), 1 },
			{ 75974, TEXT("Набор оружия капитана (посох, трость, жезл)"), TEXT("Captain's Weapon Set (Staff, Stick, Rod)"), 1 },
			{ 75975, TEXT("Набор оружия капитана (копье, меч, двуручный меч, щит)"), TEXT("Captain's Weapon Set (Spear, Sword, Blade, Shield)"), 1 },
			{ 75976, TEXT("Набор оружия капитана (баллиста, лук, арбалет)"), TEXT("Captain's Weapon Set (Ballista, Bow, Crossbow)"), 1 },
			{ 75977, TEXT("Набор оружия капитана (посох, трость, жезл)"), TEXT("Captain's Weapon Set (Staff, Stick, Rod)"), 1 },
			{ 76127, TEXT("Шляпа испанского наряда"), TEXT("Spanish Hat"), 0 },
			{ 76128, TEXT("Верх испанского наряда"), TEXT("Spanish Top"), 0 },
			{ 76129, TEXT("Перчатки испанского наряда"), TEXT("Spanish Gloves"), 0 },
			{ 76130, TEXT("Низ испанского наряда"), TEXT("Spanish Bottoms"), 0 },
			{ 76131, TEXT("Ботинки испанского наряда"), TEXT("Spanish Shoes"), 0 },
			{ 76132, TEXT("Шляпа испанского наряда"), TEXT("Spanish Hat"), 0 },
			{ 76133, TEXT("Верх испанского наряда"), TEXT("Spanish Top"), 0 },
			{ 76134, TEXT("Перчатки испанского наряда"), TEXT("Spanish Gloves"), 0 },
			{ 76135, TEXT("Низ испанского наряда"), TEXT("Spanish Bottoms"), 0 },
			{ 76136, TEXT("Ботинки испанского наряда"), TEXT("Spanish Shoes"), 0 },
			{ 76137, TEXT("Шляпа испанского наряда"), TEXT("Spanish Hat"), 0 },
			{ 76138, TEXT("Верх испанского наряда"), TEXT("Spanish Top"), 0 },
			{ 76139, TEXT("Перчатки испанского наряда"), TEXT("Spanish Gloves"), 0 },
			{ 76140, TEXT("Низ испанского наряда"), TEXT("Spanish Bottoms"), 0 },
			{ 76141, TEXT("Ботинки испанского наряда"), TEXT("Spanish Shoes"), 0 },
			{ 76142, TEXT("Шляпа испанского наряда"), TEXT("Spanish Hat"), 0 },
			{ 76143, TEXT("Верх испанского наряда"), TEXT("Spanish Top"), 0 },
			{ 76144, TEXT("Перчатки испанского наряда"), TEXT("Spanish Gloves"), 0 },
			{ 76145, TEXT("Низ испанского наряда"), TEXT("Spanish Bottoms"), 0 },
			{ 76146, TEXT("Ботинки испанского наряда"), TEXT("Spanish Shoes"), 0 },
		};
		int32 Ok = 0, Fail = 0;   // счёт проверок
		auto Check = [&Ok, &Fail](bool bOk, const FString& What)
		{
			if (bOk) { ++Ok; } else { ++Fail; }
			UE_LOG(LogAsdaPremShop, Display, TEXT("САМОТЕСТ-ИМЕНА-КОРОБОК %s %s"), bOk ? TEXT("OK  ") : TEXT("FAIL"), *What);
		};
		// Часть имени до первой скобки (имя содержимого без количества: «Тайное зелье ОЗ (500)» → «Тайное зелье ОЗ »).
		auto Stem = [](const FString& S) { int32 P; return S.FindChar(TEXT('('), P) ? S.Left(P) : S; };
		for (const FExp& E : Exp)
		{
			const FString Want = bRu ? E.Ru : E.En;
			const FString Got = IT->GetItemName(E.Code);
			const FAsdaItemTooltipText T = Tip->BuildTooltipByCode(E.Code);
			Check(Got == Want && T.Title.Contains(Want),
				FString::Printf(TEXT("N %d [%s]: имя «%s» (ждём «%s»), заголовок подсказки «%s»"),
					E.Code, bRu ? TEXT("ru") : TEXT("en"), *Got, *Want, *T.Title));
			if (!E.bBox) { continue; }
			TArray<FAsdaPackageEntry> Entries;
			const bool bPk = IT->GetPackageContents(E.Code, Entries);
			// Панель состава 26500 — тот же путь, что «Вид»/вскрытие из сумки (режим 0 = превью, сеть не трогается).
			const bool bPanel = Pkg->Populate(E.Code, /*StampMode*/0, /*SrcContainer*/-1, /*SrcSlot*/-1);
			const FString Model = Pkg->DumpModel();
			Pkg->ClosePanel();
			bool bAllShown = bPk && bPanel && Entries.Num() > 0;
			TSet<int32> Codes;
			FString Inside;
			for (const FAsdaPackageEntry& En : Entries)
			{
				Codes.Add(En.ItemCode);
				const FString N = IT->GetItemName(En.ItemCode);
				Inside += FString::Printf(TEXT("%s%s"), Inside.IsEmpty() ? TEXT("") : TEXT(" + "), *N);
				bAllShown &= Model.Contains(FString::Printf(TEXT("%d «%s»"), En.ItemCode, *N));
			}
			bool bMatch = true;
			FString How;
			if (E.Code >= 75966 && E.Code <= 75977)
			{
				for (const FAsdaPackageEntry& En : Entries)
				{
					const FString N = IT->GetItemName(En.ItemCode);
					bMatch &= bRu ? N.Contains(TEXT("капитана")) : N.Contains(TEXT("Captain's"));
				}
				bMatch &= (Entries.Num() == 3 || Entries.Num() == 4);   // 07.11.2026: оружие своего класса — 3 у лучника и мага, 4 у воина
				How = TEXT("3–4 предмета оружия капитана своего класса");
			}
			else if (Codes.Num() == 1)
			{
				const FString N = IT->GetItemName(Entries[0].ItemCode);
				// ★ §12: число в имени = ВСЕГО штук (сумма количеств позиций): «xN»/«*N» у N позиций по 1 или «(N)» у одной стопки.
				int32 Total = 0;
				for (const FAsdaPackageEntry& En : Entries) { Total += En.Amount; }
				const FString Cnt = FString(bRu ? TEXT("x") : TEXT("*")) + FString::FromInt(Total);
				const FString Paren = FString::Printf(TEXT("(%d)"), Total);
				bMatch = Want.StartsWith(Stem(N).TrimEnd()) && (Want.Contains(Cnt) || (Entries.Num() == 1 && Want.EndsWith(Paren)));
				How = FString::Printf(TEXT("одиночная: «%s», всего %d шт."), *N, Total);
			}
			else
			{
				How = TEXT("смешанная (имя — у коробки-близнеца с тем же составом)");
			}
			Check(bAllShown && bMatch,
				FString::Printf(TEXT("V %d [%s] «%s»: вскрытие показывает %s; %s%s"), E.Code, bRu ? TEXT("ru") : TEXT("en"), *Want,
					*Inside, *How, bAllShown ? TEXT("") : *FString::Printf(TEXT(" — ПАНЕЛЬ: %s"), *Model.Left(300))));
		}
		AsdaSelfTestKit::Finish(W, kCmd, Args.bQuit, LogAsdaPremShop, TEXT("САМОТЕСТ-ИМЕНА-КОРОБОК"), Ok, Fail);
	}));
#endif // ASDA_DEV_CONSOLE

#if ASDA_DEV_CONSOLE   // ★ античит А2: дев-команда, в Shipping не регистрируется
// ★★ 26.10.2026 САМОТЕСТ «ПРЕМИУМ-КОРОБКИ ПРЕДМЕТ ×N» ([ПОРТ], решение владельца: песок 20931 ×100/×1000/×10000; рецепт
//   `Docs/Port/systems/PREMIUM_ITEM_ADD_FLOW.md`, генератор `Tools/asda_premium_box_add.py`). Ожидания берутся из
//   спецификации `Tools/data/premium_box_sets.json` (тот же источник, что у генератора) — следующий набор проверяется без
//   правки кода. Аргумент `group=<имя>` — только эта группа (по умолчанию все). Язык сессии (пакет `--lang ru|en`):
//     D данные: имя `GetItemName`, заголовок и тело подсказки `BuildTooltipByCode`, цена `item_list.Price`, состав
//       `GetPackageContents` = (предмет, кол-во), иконка = иконке предмета-источника;
//     V вскрытие: панель состава 26500 (режим 0 «превью») показывает предмет; `ReservePackageSlots` — одна позиция
//       в расходники (стопка), нехватки нет;
//     P витрина 6000: карточка найдена обходом вкладок/подразделов/страниц, на экране имя и цена (`FormatThousands`);
//     B покупка по одной: кнопка карточки → модалка 1559 → «ОК» → один 5070 (171 б) с этим кодом и таблицей 1 (item_list);
//       ответ 5071 код 1 снимает защёлку;
//     C корзина: все коробки группы (до 7) → кнопка 6275 → 1559 → один 5070 со всеми кодами (P85, ключ корзины 0).
//   Сеть не трогается: 5070 перехватывается (`DebugSetCapture`), ответы подаются в настоящий обработчик.
//   Итог `САМОТЕСТ-КОРОБКИ-НАБОРОВ ИТОГ: ok=N fail=M`.
static FAutoConsoleCommandWithWorldAndArgs GMallBoxAddSelfTest(
	TEXT("asda.PremiumMall.BoxAddSelfTest"),
	TEXT("asda.PremiumMall.BoxAddSelfTest [group=<имя>] [quit] — премиум-коробки «предмет ×N» из Tools/data/premium_box_sets.json: "
	     "имя/подсказка/цена/состав, вскрытие, витрина 6000, покупка по одной и корзиной (5070) на языке сессии"),
	FConsoleCommandWithWorldAndArgsDelegate::CreateStatic([](const TArray<FString>& A, UWorld* W)
	{
		static const TCHAR* kCmd = TEXT("asda.PremiumMall.BoxAddSelfTest");
		static const TCHAR* kCartKey = TEXT("asda.PremiumMall.CartOnePerPacket");
		const FAsdaSelfTestArgs Args = FAsdaSelfTestArgs::Parse(A);
		if (AsdaSelfTestKit::RefuseStart(W, kCmd, Args, 60.f)) { return; }
		UAsdaPremiumShopSubsystem* P = ShopSub(W);
		UGameInstance* GI0 = W ? W->GetGameInstance() : nullptr;
		const UAsdaItemTableSubsystem* IT = GI0 ? GI0->GetSubsystem<UAsdaItemTableSubsystem>() : nullptr;
		const UAsdaItemTooltipSubsystem* Tip = GI0 ? GI0->GetSubsystem<UAsdaItemTooltipSubsystem>() : nullptr;
		UAsdaPackagePanelSubsystem* Pkg = GI0 ? GI0->GetSubsystem<UAsdaPackagePanelSubsystem>() : nullptr;
		const UAsdaInventorySubsystem* Inv = GI0 ? GI0->GetSubsystem<UAsdaInventorySubsystem>() : nullptr;
		UAsdaPurchaseBoxSubsystem* Box = GI0 ? GI0->GetSubsystem<UAsdaPurchaseBoxSubsystem>() : nullptr;
		if (!W || !P || !IT || !Tip || !Pkg || !Inv || !Box)
		{
			AsdaSelfTestKit::Refused(W, kCmd, Args.bQuit, TEXT("нет мира или подсистем магазина / таблиц / подсказки / панели / инвентаря / 1559"));
			return;
		}
		if (P->IsOpen() || Pkg->IsPanelOpen())
		{
			AsdaSelfTestKit::Refused(W, kCmd, Args.bQuit, TEXT("окно 6000 или панель 26500 открыты игроком"));
			return;
		}
		// Ожидание одной коробки (поля спецификации).
		// ★ 02.11.2026 (Reports/levelup_potion): Catalog > 0 — коробка функционального предмета под ПРЕЖНЕЙ позицией
		//   витрины (558 «Зелье повышения уровня» → 79935 картой шлюза): карточка и 5070 — по коду позиции, таблица 0
		//   (special_itemlist), позиция состава — в сумку (контейнер 1); в корзину item_list такая позиция не кладётся.
		struct FBoxExp { bool bSplit = false; int32 Code = 0; int32 Item = 0; int32 Qty = 0; int32 Price = 0; int32 IconFrom = 0; int32 Cat = 0;
		                 int32 Catalog = 0; FString Name; FString Desc; FString Group;
		                 int32 Key() const { return Catalog > 0 ? Catalog : Code; } };
		FString Group;   // фильтр `group=`
		for (const FString& S : Args.Pos) { if (S.StartsWith(TEXT("group="))) { Group = S.Mid(6); } }
		const FString SpecPath = FPaths::Combine(FPaths::ProjectDir(), TEXT("Tools/data/premium_box_sets.json"));
		FString SpecText;
		TSharedPtr<FJsonObject> Root;
		const bool bRu = FAsdaLang::IsRu();
		TArray<FBoxExp> Exp;
		if (FFileHelper::LoadFileToString(SpecText, *SpecPath) &&
		    FJsonSerializer::Deserialize(TJsonReaderFactory<>::Create(SpecText), Root) && Root.IsValid())
		{
			const TArray<TSharedPtr<FJsonValue>>* Boxes = nullptr;
			if (Root->TryGetArrayField(TEXT("boxes"), Boxes) && Boxes)
			{
				for (const TSharedPtr<FJsonValue>& V : *Boxes)
				{
					const TSharedPtr<FJsonObject> O = V.IsValid() ? V->AsObject() : nullptr;
					if (!O.IsValid()) { continue; }
					FBoxExp E;
					E.Group = O->GetStringField(TEXT("group"));
					if (!Group.IsEmpty() && E.Group != Group) { continue; }
					E.Code = (int32)O->GetNumberField(TEXT("code"));
					E.Item = (int32)O->GetNumberField(TEXT("item"));
					E.Qty = (int32)O->GetNumberField(TEXT("qty"));
					O->TryGetBoolField(TEXT("split"), E.bSplit);   // ★ 07.11.2026: нестопочный предмет — по штуке в позиции
					E.Price = (int32)O->GetNumberField(TEXT("price"));
					E.IconFrom = (int32)O->GetNumberField(TEXT("icon_from"));
					E.Name = O->GetStringField(bRu ? TEXT("name_ru") : TEXT("name_en"));
					E.Desc = O->GetStringField(bRu ? TEXT("desc_ru") : TEXT("desc_en"));
					const TSharedPtr<FJsonObject>* Shop = nullptr;
					if (O->TryGetObjectField(TEXT("shop"), Shop) && Shop) { E.Cat = (int32)(*Shop)->GetNumberField(TEXT("Category")); }
					double Cat = 0.0;
					if (O->TryGetNumberField(TEXT("catalog"), Cat)) { E.Catalog = (int32)Cat; }
					Exp.Add(E);
				}
			}
		}
		if (Exp.Num() == 0)
		{
			AsdaSelfTestKit::Refused(W, kCmd, Args.bQuit, *FString::Printf(TEXT("спецификация %s не прочитана или пуста (группа «%s»)"), *SpecPath, *Group));
			return;
		}
		int32 Ok = 0, Fail = 0;   // счёт проверок
		auto Check = [&Ok, &Fail](bool bOk, const FString& What)
		{
			if (bOk) { ++Ok; } else { ++Fail; }
			UE_LOG(LogAsdaPremShop, Display, TEXT("САМОТЕСТ-КОРОБКИ-НАБОРОВ %s %s"), bOk ? TEXT("OK  ") : TEXT("FAIL"), *What);
		};
		const TCHAR* L = bRu ? TEXT("ru") : TEXT("en");
		// ── D и V: данные, подсказка, состав, иконка, вскрытие ──
		for (const FBoxExp& E : Exp)
		{
			const FAsdaItemRecord* Rec = IT->GetItem(E.Code);
			const FString Got = IT->GetItemName(E.Code);
			const FAsdaItemTooltipText T = Tip->BuildTooltipByCode(E.Code);
			Check(Rec && Got == E.Name && T.Title.Contains(E.Name) && T.Body.Contains(E.Desc),
				FString::Printf(TEXT("D %d [%s]: имя «%s» (ждём «%s»), заголовок «%s», описание %s"), E.Code, L, *Got, *E.Name, *T.Title,
					T.Body.Contains(E.Desc) ? TEXT("есть") : *FString::Printf(TEXT("НЕТ «%s» в «%s»"), *E.Desc, *T.Body.Left(120))));
			Check(Rec && Rec->Price == E.Price && IT->IsPackageItem(E.Code),
				FString::Printf(TEXT("D %d: цена item_list %d (ждём %d), коробка (packagetable) %s"), E.Code, Rec ? Rec->Price : -1, E.Price,
					IT->IsPackageItem(E.Code) ? TEXT("да") : TEXT("НЕТ")));
			TArray<FAsdaPackageEntry> Entries;
			const bool bPk = IT->GetPackageContents(E.Code, Entries);
			bool bSplitOk = E.bSplit && Entries.Num() == E.Qty;
			for (const FAsdaPackageEntry& Pe : Entries) { bSplitOk = bSplitOk && Pe.ItemCode == E.Item && Pe.Amount == 1; }
			Check(bPk && (bSplitOk || (!E.bSplit && Entries.Num() == 1 && Entries[0].ItemCode == E.Item && Entries[0].Amount == E.Qty)),
				FString::Printf(TEXT("D %d: состав %s (ждём %d «%s» ×%d)"), E.Code,
					Entries.Num() > 0 ? *FString::Printf(TEXT("%d ×%d, позиций %d"), Entries[0].ItemCode, Entries[0].Amount, Entries.Num()) : TEXT("пуст"),
					E.Item, *IT->GetItemName(E.Item), E.Qty));
			const FAsdaItemIcon Ia = IT->GetItemIcon(E.Code), Ib = IT->GetItemIcon(E.IconFrom);
			Check(Ia.IsValid() && Ia.Atlas == Ib.Atlas && Ia.CellX == Ib.CellX && Ia.CellY == Ib.CellY && Ia.CellSize == Ib.CellSize,
				FString::Printf(TEXT("D %d: иконка %s (%d,%d,%d) = иконке %d %s (%d,%d,%d)"), E.Code, *Ia.Atlas, Ia.CellX, Ia.CellY, Ia.CellSize,
					E.IconFrom, *Ib.Atlas, Ib.CellX, Ib.CellY, Ib.CellSize));
			const bool bPanel = Pkg->Populate(E.Code, /*StampMode*/0, /*SrcContainer*/-1, /*SrcSlot*/-1);
			const FString Model = Pkg->DumpModel();
			Pkg->ClosePanel();
			const FString Want = FString::Printf(TEXT("%d «%s»"), E.Item, *IT->GetItemName(E.Item));
			Check(bPanel && Model.Contains(Want),
				FString::Printf(TEXT("V %d: панель 26500 показывает %s%s"), E.Code, *Want, bPanel && Model.Contains(Want) ? TEXT("") : *FString::Printf(TEXT(" — ПАНЕЛЬ: %s"), *Model.Left(200))));
			TArray<uint8> Slots15;
			int32 Cnt = 0, ShortS = 0, ShortB = 0;
			const bool bRes = Inv->ReservePackageSlots(E.Code, Slots15, Cnt, ShortS, ShortB);
			const int32 WantCont = IT->IsSpecialItemCode(E.Item) ? 1 : 2;   // спецпредмет — сумка, стопка item_list — расходники
			const int32 WantCnt = E.bSplit ? E.Qty : 1;   // ★ 07.11.2026: нестопочный предмет — ячейка на штуку
			Check(bRes && Cnt == WantCnt && ShortS == 0 && ShortB == 0 && Slots15.Num() == 15 && Slots15[0] == WantCont,
				FString::Printf(TEXT("V %d: 6096 займёт позиций %d (ждём %d, контейнер %d), контейнер %d, нехватка %d/%d"), E.Code, Cnt, WantCnt,
					WantCont, Slots15.Num() > 0 ? (int32)Slots15[0] : -1, ShortS, ShortB));
		}
		// ── R: архивные коробки, приведённые к выдаче сервера (спецификация `relabeled`; 02.11.2026 — 76405 после
		//   PackageItem_Table 4572 = 558, Reports/levelup_potion): имя на языке сессии, состав, панель 26500, ячейка 6096 ──
		if (const TArray<TSharedPtr<FJsonValue>>* Rel = nullptr; Root.IsValid() && Root->TryGetArrayField(TEXT("relabeled"), Rel) && Rel && Group.IsEmpty())
		{
			for (const TSharedPtr<FJsonValue>& V : *Rel)
			{
				const TSharedPtr<FJsonObject> O = V.IsValid() ? V->AsObject() : nullptr;
				if (!O.IsValid()) { continue; }
				const int32 Code = (int32)O->GetNumberField(TEXT("code"));
				const int32 Item = (int32)O->GetNumberField(TEXT("item"));
				const FString Name = O->GetStringField(bRu ? TEXT("name_ru") : TEXT("name_en"));
				TArray<FAsdaPackageEntry> Entries;
				const bool bPk = IT->GetPackageContents(Code, Entries);
				Check(IT->GetItemName(Code) == Name && bPk && Entries.Num() == 1 && Entries[0].ItemCode == Item && Entries[0].Amount == 1,
					FString::Printf(TEXT("R %d [%s]: имя «%s» (ждём «%s»), состав %s (ждём %d ×1)"), Code, L, *IT->GetItemName(Code), *Name,
						Entries.Num() > 0 ? *FString::Printf(TEXT("%d ×%d, позиций %d"), Entries[0].ItemCode, Entries[0].Amount, Entries.Num()) : TEXT("пуст"), Item));
				const bool bPanel = Pkg->Populate(Code, /*StampMode*/0, /*SrcContainer*/-1, /*SrcSlot*/-1);
				const FString Model = Pkg->DumpModel();
				Pkg->ClosePanel();
				const FString Want = FString::Printf(TEXT("%d «%s»"), Item, *IT->GetItemName(Item));
				TArray<uint8> Slots15;
				int32 Cnt = 0, ShortS = 0, ShortB = 0;
				const bool bRes = Inv->ReservePackageSlots(Code, Slots15, Cnt, ShortS, ShortB);
				Check(bPanel && Model.Contains(Want) && bRes && Cnt == 1 && Slots15.Num() == 15 && Slots15[0] == 1,
					FString::Printf(TEXT("R %d: панель 26500 показывает %s %s; 6096 — позиций %d, контейнер %d (ждём 1 — сумка)"), Code, *Want,
						bPanel && Model.Contains(Want) ? TEXT("да") : TEXT("НЕТ"), Cnt, Slots15.Num() > 0 ? (int32)Slots15[0] : -1));
			}
		}
		// ── P: витрина 6000 ──
		auto Texts = [W]()
		{
			TArray<FString> Out;
			for (TObjectIterator<UUserWidget> It; It; ++It)
			{
				UUserWidget* U = *It;
				if (!U || U->GetWorld() != W || !U->IsInViewport() || !U->GetClass()->GetName().Contains(TEXT("SHELLITEM_STORE")) || !U->WidgetTree) { continue; }
				TArray<UWidget*> All;
				U->WidgetTree->GetAllWidgets(All);
				for (UWidget* Wd : All)
				{
					const UTextBlock* Tb = Cast<UTextBlock>(Wd);
					if (!Tb) { continue; }
					bool bVis = true;
					for (const UWidget* Up = Tb; Up; Up = Up->GetParent()) { if (!Up->IsVisible()) { bVis = false; break; } }
					if (bVis) { Out.Add(Tb->GetText().ToString()); }
				}
			}
			return Out;
		};
		struct FWhere { int32 Cat = -1; int32 Sub = -1; int32 Page = -1; int32 Card = -1; bool bName = false; bool bPrice = false; };
		TMap<int32, FWhere> Where;
		AsdaCVarSave::ArmWorldGuard(W, kCmd);
		P->OpenWindow();
		for (int32 Cat = 0; Cat < 8; ++Cat)
		{
			for (int32 Sub = 0; Sub < 5; ++Sub)
			{
				P->SetCategory(Cat);
				if (P->GetCategory() == 3) { if (Sub > 2) { continue; } P->SetEquipSub(Sub); } else { P->SetToolSub(Sub); }
				for (int32 Pg = 0; Pg < 20; ++Pg)
				{
					P->SetPage(Pg);
					const TArray<FString> T = Texts();
					for (int32 Card = 0; Card < UAsdaPremiumShopSubsystem::kCards; ++Card)
					{
						const int32 Code = P->DebugCardCode(Card);
						const FBoxExp* E = Exp.FindByPredicate([Code](const FBoxExp& X) { return X.Key() == Code; });
						if (!E || Where.Contains(Code)) { continue; }
						FWhere& Wh = Where.Add(Code);
						Wh.Cat = Cat; Wh.Sub = Sub; Wh.Page = Pg; Wh.Card = Card;
						Wh.bName = T.Contains(E->Name);
						Wh.bPrice = T.Contains(AsdaUi::FormatThousands(E->Price));
					}
				}
			}
		}
		for (const FBoxExp& E : Exp)
		{
			const FWhere* Wh = Where.Find(E.Key());
			Check(Wh && Wh->bName && Wh->bPrice,
				Wh ? FString::Printf(TEXT("P %d [%s]: карточка (вкладка %d, подраздел %d, стр. %d, № %d), имя «%s» %s, цена %s %s"), E.Key(), L,
				         Wh->Cat, Wh->Sub, Wh->Page, Wh->Card, *E.Name, Wh->bName ? TEXT("видно") : TEXT("НЕТ"),
				         *AsdaUi::FormatThousands(E.Price), Wh->bPrice ? TEXT("видно") : TEXT("НЕТ"))
				   : FString::Printf(TEXT("P %d: карточки на витрине 6000 НЕТ"), E.Key()));
			if (E.Catalog > 0)   // позиция витрины = сам спецпредмет: имя и цена карточки — его, коробка выдаётся картой шлюза
			{
				FAsdaSpecialItemlistRow Sp;
				const bool bSp = IT->GetSpecialItem(E.Catalog, Sp);
				Check(IT->GetItemName(E.Catalog) == E.Name && bSp && Sp.Price == E.Price,
					FString::Printf(TEXT("P %d [%s]: имя позиции «%s» = имени коробки %d «%s», цена special_itemlist %d (ждём %d)"), E.Catalog, L,
						*IT->GetItemName(E.Catalog), E.Code, *E.Name, bSp ? Sp.Price : -1, E.Price));
			}
		}
		// ── B и C: покупка (5070 перехватывается) ──
		auto Codes = [](const TArray<uint8>& B)
		{
			TArray<int32> Out;
			for (int32 i = 0; i < 7 && B.Num() >= 31; ++i) { const int32 C = AsdaBytes::RdI32(&B[3 + 4 * i]); if (C != -1) { Out.Add(C); } }
			return Out;
		};
		auto Reply = [](uint8 Code)
		{
			TArray<uint8> B;
			B.SetNumZeroed(407);
			B[0] = Code;
			B[1] = 1;
			AsdaBytes::WrI32(&B[2], 900000);
			B[12] = 1;
			return B;
		};
		AsdaCVarSave::SetForTest(kCartKey, 0);
		P->DebugSetGxp(999999);
		for (const FBoxExp& E : Exp)
		{
			const FWhere* Wh = Where.Find(E.Key());
			if (!Wh) { Check(false, FString::Printf(TEXT("B %d: покупка не проверена — карточки нет"), E.Key())); continue; }
			const int32 WantTable = E.Catalog > 0 ? 0 : 1;   // 0 — special_itemlist (sub_532B10), 1 — item_list (sub_532A90)
			P->SetCategory(Wh->Cat);
			if (P->GetCategory() == 3) { P->SetEquipSub(Wh->Sub); } else { P->SetToolSub(Wh->Sub); }
			P->SetPage(Wh->Page);
			P->DebugSetCapture(true);
			P->OnCardBuyClicked(Wh->Card);
			const bool bModal = Box->IsOpen();
			if (bModal) { Box->OnConfirmClicked(); }
			const TArray<TArray<uint8>>& Sent = P->DebugSentBodies();
			const TArray<uint8> B0 = Sent.Num() > 0 ? Sent.Last() : TArray<uint8>();
			Check(bModal && Sent.Num() == 1 && B0.Num() == 171 && B0[1] == WantTable && Codes(B0) == TArray<int32>({ E.Key() }) && P->DebugIsPending(),
				FString::Printf(TEXT("B %d: карточка → 1559 %s → 5070: пакетов %d, длина %d, таблица %d, коды %d шт. (первый %d)"), E.Key(),
					bModal ? TEXT("открыта") : TEXT("НЕ открыта"), Sent.Num(), B0.Num(), B0.Num() > 1 ? (int32)B0[1] : -1,
					Codes(B0).Num(), Codes(B0).Num() > 0 ? Codes(B0)[0] : 0));
			P->DebugFeedBuyResult(Reply(1));
			Check(!P->DebugIsPending(), FString::Printf(TEXT("B %d: ответ 5071 код 1 снял защёлку"), E.Code));
		}
		{
			TArray<int32> Cart;
			for (const FBoxExp& E : Exp) { if (Cart.Num() < 7 && E.Catalog == 0) { Cart.Add(E.Code); } }
			P->DebugSetCapture(true);
			P->DebugFillCart(Cart);
			P->OnBuySelectedClicked();
			const bool bModal = Box->IsOpen();
			if (bModal) { Box->OnConfirmClicked(); }
			const TArray<TArray<uint8>>& Sent = P->DebugSentBodies();
			const TArray<uint8> B0 = Sent.Num() > 0 ? Sent.Last() : TArray<uint8>();
			Check(bModal && Sent.Num() == 1 && B0.Num() == 171 && B0[1] == 1 && Codes(B0) == Cart,
				FString::Printf(TEXT("C корзина из %d: 1559 %s, пакетов %d, кодов в пакете %d, таблица %d"), Cart.Num(),
					bModal ? TEXT("открыта") : TEXT("НЕ открыта"), Sent.Num(), Codes(B0).Num(), B0.Num() > 1 ? (int32)B0[1] : -1));
			P->DebugFeedBuyResult(Reply(1));
			Check(!P->DebugIsPending() && P->DebugCartFilled() == 0,
				FString::Printf(TEXT("C ответ 5071 код 1: защёлка снята, корзина пуста (строк %d)"), P->DebugCartFilled()));
		}
		P->DebugSetCapture(false);
		P->DebugFillCart({});
		P->DebugClearPending();
		P->CloseWindow();
		AsdaSelfTestKit::Finish(W, kCmd, Args.bQuit, LogAsdaPremShop, TEXT("САМОТЕСТ-КОРОБКИ-НАБОРОВ"), Ok, Fail);
	}));
#endif // ASDA_DEV_CONSOLE

#if ASDA_DEV_CONSOLE   // ★ античит А2: дев-команда, в Shipping не регистрируется
// ★★★ 25.10.2026 САМОТЕСТ «КОРЗИНА ПОШТУЧНО» ([ИСПРАВЛЕНИЕ] `asda.PremiumMall.CartOnePerPacket`, шапка `AsdaPremiumBuyQueue.h`).
//   Без сети: 5070 перехватывается (`DebugSetCapture`) — тело 171 б кладётся в список вместо сокета, защёлка взводится как
//   после настоящей отправки; ответы 5071 подаются в НАСТОЯЩИЕ обработчики `OnBuyResult`. Путь игрока окна 6000 — кнопка
//   6275 `OnBuySelectedClicked` → модалка 1559 → «ОК» 1561 (`OnConfirmClicked`). Шаги идут по таймеру 0,3 с (следующую
//   позицию шлёт Tick подсистемы, а не сетевой колбэк). Случаи:
//     A корзина 3 × 75701 «Набор наряда преследователя» (случай игроков 19.10): три 5070 по ОДНОМУ коду, строки корзины
//       снимаются по одной, по третьему ответу корзина пуста и запуск окончен;
//     B смешанная 75701 + 493 (special_itemlist) + 63103: байт +1 «таблица» по позиции (1 / 0), отказ (код 0) на второй
//       позиции останавливает запуск — третья не уходит, в корзине остаются 493 и 63103;
//     C ключ 0 (как оригинал): один 5070 с тремя кодами;
//     D магазин бустеров 15300, 3 × 53672: три 5070 по одному коду; D2 ключ 0 — один пакет с тремя.
//   Итог `САМОТЕСТ-6000-КОРЗИНА ИТОГ: ok=N fail=M`; ключ, перехват, признаки окон и защёлки возвращаются.
static FAutoConsoleCommandWithWorldAndArgs GMallCartSelfTest(
	TEXT("asda.PremiumMall.CartSelfTest"),
	TEXT("asda.PremiumMall.CartSelfTest [quit] — корзина 6275 и «количество» магазина бустеров уходят поштучно "
	     "(по одному 5070, следующая после 5071 код 1; отказ останавливает); ключ 0 — одним пакетом, как оригинал"),
	FConsoleCommandWithWorldAndArgsDelegate::CreateStatic([](const TArray<FString>& A, UWorld* W)
	{
		static const TCHAR* kCmd = TEXT("asda.PremiumMall.CartSelfTest");
		static const TCHAR* kKey = TEXT("asda.PremiumMall.CartOnePerPacket");
		const FAsdaSelfTestArgs Args = FAsdaSelfTestArgs::Parse(A);
		if (AsdaSelfTestKit::RefuseStart(W, kCmd, Args, 60.f)) { return; }
		UAsdaPremiumShopSubsystem* P = ShopSub(W);
		UGameInstance* GI0 = W ? W->GetGameInstance() : nullptr;
		UAsdaBoosterShopSubsystem* Bs = GI0 ? GI0->GetSubsystem<UAsdaBoosterShopSubsystem>() : nullptr;
		UAsdaPurchaseBoxSubsystem* Box = GI0 ? GI0->GetSubsystem<UAsdaPurchaseBoxSubsystem>() : nullptr;
		UAsdaWindowSubsystem* WM0 = GI0 ? GI0->GetSubsystem<UAsdaWindowSubsystem>() : nullptr;
		if (!W || !P || !Bs || !Box || !WM0)
		{
			AsdaSelfTestKit::Refused(W, kCmd, Args.bQuit, TEXT("нет мира или подсистем премиум-магазина / бустеров / модалки 1559"));
			return;
		}
		if (P->IsOpen() || Bs->IsOpen())
		{
			AsdaSelfTestKit::Refused(W, kCmd, Args.bQuit, TEXT("окно 6000 или 15300 открыто игроком — самотест подменил бы его покупку"));
			return;
		}
		// Состояние прогона (общее для шагов таймера).
		struct FRun
		{
			int32 Ok = 0;                          // пройдено проверок
			int32 Fail = 0;                        // провалено проверок
			int32 Step = 0;                        // следующий шаг
			int32 Queue0 = 0;                      // очередь окон-сообщений до отказа (шаг B)
			bool  bDone = false;                   // итог подведён — таймер ждёт снятия на следующем кадре
			FTimerHandle H;                        // таймер шагов
			TArray<TFunction<void()>> Steps;       // шаги по порядку
		};
		TSharedRef<FRun> R = MakeShared<FRun>();
		TWeakObjectPtr<UWorld> WW(W);
		TWeakObjectPtr<UAsdaPremiumShopSubsystem> WP(P);
		TWeakObjectPtr<UAsdaBoosterShopSubsystem> WB(Bs);
		TWeakObjectPtr<UAsdaPurchaseBoxSubsystem> WX(Box);
		TWeakObjectPtr<UAsdaWindowSubsystem> WWM(WM0);
		const bool bQuit = Args.bQuit;
		auto Check = [R](bool bOk, const FString& What)
		{
			if (bOk) { ++R->Ok; } else { ++R->Fail; }
			UE_LOG(LogAsdaPremShop, Display, TEXT("САМОТЕСТ-6000-КОРЗИНА %s %s"), bOk ? TEXT("OK  ") : TEXT("FAIL"), *What);
		};
		// Коды тела 5070 (+3 + 4·i, «пусто» = −1) и байт +1 «таблица».
		auto Codes = [](const TArray<uint8>& B)
		{
			TArray<int32> Out;
			for (int32 i = 0; i < 7 && B.Num() >= 31; ++i)
			{
				const int32 C = AsdaBytes::RdI32(&B[3 + 4 * i]);
				if (C != -1) { Out.Add(C); }
			}
			return Out;
		};
		auto Fmt = [](const TArray<int32>& C)
		{
			FString S;
			for (int32 V : C) { S += FString::Printf(TEXT("%s%d"), S.IsEmpty() ? TEXT("") : TEXT(","), V); }
			return S.IsEmpty() ? FString(TEXT("—")) : S;
		};
		// Тело ответа 5071 (407 б): код, валюта GXP, баланс, подрежим 1 = в инвентарь.
		auto Reply = [](uint8 Code)
		{
			TArray<uint8> B;
			B.SetNumZeroed(407);
			B[0] = Code;
			B[1] = 1;
			AsdaBytes::WrI32(&B[2], 900000);
			B[12] = 1;
			return B;
		};
		// Последнее перехваченное тело окна 6000 / магазина бустеров.
		auto LastP = [WP]() { UAsdaPremiumShopSubsystem* S = WP.Get(); return (S && S->DebugSentBodies().Num() > 0) ? S->DebugSentBodies().Last() : TArray<uint8>(); };
		auto LastB = [WB]() { UAsdaBoosterShopSubsystem* S = WB.Get(); return (S && S->DebugSentBodies().Num() > 0) ? S->DebugSentBodies().Last() : TArray<uint8>(); };
		auto NumP = [WP]() { UAsdaPremiumShopSubsystem* S = WP.Get(); return S ? S->DebugSentBodies().Num() : -1; };
		auto NumB = [WB]() { UAsdaBoosterShopSubsystem* S = WB.Get(); return S ? S->DebugSentBodies().Num() : -1; };
		// Путь игрока: кнопка 6275 → модалка 1559 → «ОК».
		auto BuySelected = [WP, WX, Check](const TCHAR* Case)
		{
			UAsdaPremiumShopSubsystem* S = WP.Get();
			UAsdaPurchaseBoxSubsystem* X = WX.Get();
			if (!S || !X) { Check(false, FString::Printf(TEXT("%s подсистема пропала"), Case)); return; }
			S->OnBuySelectedClicked();
			const bool bBox = X->IsOpen();
			Check(bBox, FString::Printf(TEXT("%s кнопка 6275 открыла модалку 1559"), Case));
			if (bBox) { X->OnConfirmClicked(); }
		};
		AsdaCVarSave::ArmWorldGuard(W, kCmd);
		AsdaCVarSave::SetForTest(TEXT("asda.PremiumMall.CartVariants"), 0);   // здесь проверяются пакеты корзины; выбор уровня — `asda.SelfTest.PremiumBuyVariant`
		P->DebugSetOpenFlag(true);
		P->DebugSetGxp(999999);
		Bs->DebugSetOpenFlag(true);
		Bs->DebugSetGxp(999999);

		// ── A: 3 × 75701 поштучно ──
		R->Steps.Add([=]()
		{
			AsdaCVarSave::SetForTest(kKey, 1);
			UAsdaPremiumShopSubsystem* S = WP.Get(); if (!S) { return; }
			S->DebugSetCapture(true);
			S->DebugFillCart({ 75701, 75701, 75701 });
			BuySelected(TEXT("A"));
			const TArray<uint8> B = LastP();
			Check(NumP() == 1 && Codes(B) == TArray<int32>({ 75701 }) && B.Num() == 171 && B[1] == 1 && S->DebugIsPending(),
				FString::Printf(TEXT("A1 ушёл ОДИН 5070 с одним кодом (пакетов %d, коды %s, таблица %d)"), NumP(), *Fmt(Codes(B)), B.Num() > 1 ? B[1] : -1));
			Check(S->DebugCartRunning() && S->DebugCartLeft() == 2 && S->DebugCartFilled() == 3,
				FString::Printf(TEXT("A1 запуск идёт: осталось %d, строк корзины %d"), S->DebugCartLeft(), S->DebugCartFilled()));
			S->DebugFeedBuyResult(Reply(1));
		});
		R->Steps.Add([=]()
		{
			UAsdaPremiumShopSubsystem* S = WP.Get(); if (!S) { return; }
			Check(NumP() == 2 && Codes(LastP()) == TArray<int32>({ 75701 }) && S->DebugCartFilled() == 2 && S->DebugIsPending(),
				FString::Printf(TEXT("A2 после ответа код 1 Tick послал вторую позицию (пакетов %d, коды %s, строк %d)"), NumP(), *Fmt(Codes(LastP())), S->DebugCartFilled()));
			S->DebugFeedBuyResult(Reply(1));
		});
		R->Steps.Add([=]()
		{
			UAsdaPremiumShopSubsystem* S = WP.Get(); if (!S) { return; }
			Check(NumP() == 3 && Codes(LastP()) == TArray<int32>({ 75701 }) && S->DebugCartFilled() == 1,
				FString::Printf(TEXT("A3 третья позиция ушла (пакетов %d, строк %d)"), NumP(), S->DebugCartFilled()));
			S->DebugFeedBuyResult(Reply(1));
		});
		R->Steps.Add([=]()
		{
			UAsdaPremiumShopSubsystem* S = WP.Get(); if (!S) { return; }
			Check(NumP() == 3 && !S->DebugCartRunning() && S->DebugCartFilled() == 0 && !S->DebugIsPending(),
				FString::Printf(TEXT("A4 три ответа — запуск окончен, корзина пуста, лишних пакетов нет (пакетов %d, строк %d)"), NumP(), S->DebugCartFilled()));
		});

		// ── B: смешанная 75701 + 493 + 63103, отказ на второй ──
		R->Steps.Add([=]()
		{
			UAsdaPremiumShopSubsystem* S = WP.Get(); if (!S) { return; }
			S->DebugSetCapture(true);
			S->DebugFillCart({ 75701, 493, 63103 });
			BuySelected(TEXT("B"));
			const TArray<uint8> B = LastP();
			Check(NumP() == 1 && Codes(B) == TArray<int32>({ 75701 }) && B.Num() == 171 && B[1] == 1,
				FString::Printf(TEXT("B1 первая позиция 75701, таблица item_list (коды %s, таблица %d)"), *Fmt(Codes(B)), B.Num() > 1 ? B[1] : -1));
			S->DebugFeedBuyResult(Reply(1));
		});
		R->Steps.Add([=]()
		{
			UAsdaPremiumShopSubsystem* S = WP.Get(); if (!S) { return; }
			const TArray<uint8> B = LastP();
			Check(NumP() == 2 && Codes(B) == TArray<int32>({ 493 }) && B.Num() == 171 && B[1] == 0,
				FString::Printf(TEXT("B2 вторая позиция 493 со своей таблицей special_itemlist (коды %s, таблица %d)"), *Fmt(Codes(B)), B.Num() > 1 ? B[1] : -1));
			if (UAsdaWindowSubsystem* M = WWM.Get()) { R->Queue0 = M->GetMsgBoxQueueNum(); }
			S->DebugFeedBuyResult(Reply(0));   // код 0 «Не удалось получить предмет» — так отвечает и шлюз на отказ
		});
		R->Steps.Add([=]()
		{
			UAsdaPremiumShopSubsystem* S = WP.Get(); if (!S) { return; }
			Check(NumP() == 2 && !S->DebugCartRunning() && !S->DebugIsPending(),
				FString::Printf(TEXT("B3 отказ остановил запуск — третья позиция НЕ ушла (пакетов %d)"), NumP()));
			Check(S->DebugCartFilled() == 2 && S->DebugCartRowCode(0) <= 0 && S->DebugCartRowCode(1) == 493 && S->DebugCartRowCode(2) == 63103,
				FString::Printf(TEXT("B3 купленная 75701 снята, в корзине остались 493 и 63103 (строки %d/%d/%d)"),
					S->DebugCartRowCode(0), S->DebugCartRowCode(1), S->DebugCartRowCode(2)));
			// Код 0 показал модальное окно 1500 — закрываем «ОК», иначе модальный гейт достался бы следующим шагам и самотестам.
			if (UAsdaWindowSubsystem* M = WWM.Get())
			{
				if (M->GetMsgBoxQueueNum() > R->Queue0) { M->AnswerMsgBox(/*bTwoBut*/ false, /*bYes*/ true); }
				Check(M->GetMsgBoxQueueNum() == R->Queue0, TEXT("B3 окно 1500 отказа закрыто «ОК»"));
			}
		});

		// ── C: ключ 0 — одним пакетом, как оригинал ──
		R->Steps.Add([=]()
		{
			AsdaCVarSave::SetForTest(kKey, 0);
			UAsdaPremiumShopSubsystem* S = WP.Get(); if (!S) { return; }
			S->DebugSetCapture(true);
			S->DebugFillCart({ 75701, 75701, 75701 });
			BuySelected(TEXT("C"));
			Check(NumP() == 1 && Codes(LastP()) == TArray<int32>({ 75701, 75701, 75701 }) && !S->DebugCartRunning(),
				FString::Printf(TEXT("C ключ 0: один 5070 с тремя кодами, как оригинал (пакетов %d, коды %s)"), NumP(), *Fmt(Codes(LastP()))));
			S->DebugFeedBuyResult(Reply(1));
		});
		R->Steps.Add([=]()
		{
			UAsdaPremiumShopSubsystem* S = WP.Get(); if (!S) { return; }
			Check(NumP() == 1 && S->DebugCartFilled() == 0 && !S->DebugIsPending(),
				FString::Printf(TEXT("C ответ код 1 — корзина очищена, второго пакета нет (пакетов %d)"), NumP()));
		});

		// ── M: ключ 0; смешанная корзина при MixedCartTwoPackets 1 (как alpha-2.0.3) — два пакета по таблицам; В-1: корзину во
		//   время ожидания не меняют ──
		R->Steps.Add([=]()
		{
			AsdaCVarSave::SetForTest(kKey, 0);
			AsdaCVarSave::SetForTest(TEXT("asda.PremiumMall.MixedCartTwoPackets"), 1);
			UAsdaPremiumShopSubsystem* S = WP.Get(); if (!S) { return; }
			S->DebugSetCapture(true);
			S->DebugFillCart({ 75701, 493, 63103 });
			BuySelected(TEXT("M"));
			const TArray<uint8> B = LastP();
			Check(NumP() == 1 && Codes(B) == TArray<int32>({ 75701, 63103 }) && B.Num() == 171 && B[1] == 1 && S->DebugIsPending(),
				FString::Printf(TEXT("M1 смешанная: первый пакет — позиции item_list одним 5070 (коды %s, таблица %d)"), *Fmt(Codes(B)), B.Num() > 1 ? B[1] : -1));
			S->OnResetClicked();
			S->OnCartRowClicked(0);
			Check(S->DebugCartFilled() == 3 && S->DebugCartRowCode(0) == 75701,
				FString::Printf(TEXT("M1 В-1: «Сброс» и снятие галки во время ожидания отбиты (строк %d, строка 0 = %d)"),
					S->DebugCartFilled(), S->DebugCartRowCode(0)));
			S->DebugFeedBuyResult(Reply(1));
		});
		R->Steps.Add([=]()
		{
			UAsdaPremiumShopSubsystem* S = WP.Get(); if (!S) { return; }
			const TArray<uint8> B = LastP();
			Check(NumP() == 2 && Codes(B) == TArray<int32>({ 493 }) && B.Num() == 171 && B[1] == 0 && S->DebugIsPending(),
				FString::Printf(TEXT("M2 второй пакет — special_itemlist (коды %s, таблица %d), из Tick после ответа"), *Fmt(Codes(B)), B.Num() > 1 ? B[1] : -1));
			Check(S->DebugCartFilled() == 1 && S->DebugCartRowCode(1) == 493,
				FString::Printf(TEXT("M2 оплаченные строки item_list сняты, в корзине — позиция второго пакета (строк %d)"), S->DebugCartFilled()));
			S->DebugFeedBuyResult(Reply(1));
		});
		R->Steps.Add([=]()
		{
			UAsdaPremiumShopSubsystem* S = WP.Get(); if (!S) { return; }
			Check(NumP() == 2 && S->DebugCartFilled() == 0 && !S->DebugIsPending(),
				FString::Printf(TEXT("M3 оба ответа — корзина очищена, третьего пакета нет (пакетов %d)"), NumP()));
			// M4: 7 одинаковых — один пакет с семью кодами (строка ящика = 7 ячеек).
			S->DebugSetCapture(true);
			S->DebugFillCart({ 62728, 62728, 62728, 62728, 62728, 62728, 62728 });
			BuySelected(TEXT("M4"));
			Check(NumP() == 1 && Codes(LastP()).Num() == 7 && !S->DebugCartRunning(),
				FString::Printf(TEXT("M4 7 позиций — один 5070 с семью кодами (кодов %d)"), Codes(LastP()).Num()));
			S->DebugFeedBuyResult(Reply(1));
		});
		// ── R2 (25.10, баг «купил 7 — в строке 6»): MixedCartTwoPackets 0 (по умолчанию) — 7 РАЗНЫХ смешанных одним пакетом ──
		R->Steps.Add([=]()
		{
			AsdaCVarSave::SetForTest(TEXT("asda.PremiumMall.MixedCartTwoPackets"), 0);
			UAsdaPremiumShopSubsystem* S = WP.Get(); if (!S) { return; }
			S->DebugSetCapture(true);
			S->DebugFillCart({ 76392, 489, 202, 59, 499, 211, 503 });
			BuySelected(TEXT("R2"));
			const TArray<uint8> B = LastP();
			Check(NumP() == 1 && Codes(B) == TArray<int32>({ 76392, 489, 202, 59, 499, 211, 503 }) && B.Num() == 171 && B[1] == 1,
				FString::Printf(TEXT("R2 7 разных смешанных (покупка владельца 10:36) — ОДИН 5070, все 7 кодов, таблица 1 (коды %s, таблица %d)"),
					*Fmt(Codes(B)), B.Num() > 1 ? B[1] : -1));
			S->DebugFeedBuyResult(Reply(1));
		});
		R->Steps.Add([=]()
		{
			UAsdaPremiumShopSubsystem* S = WP.Get(); if (!S) { return; }
			Check(NumP() == 1 && S->DebugCartFilled() == 0 && !S->DebugIsPending(),
				FString::Printf(TEXT("R2 ответ код 1 — корзина очищена, второго пакета нет (пакетов %d)"), NumP()));
			// 7 одинаковых — тоже один пакет.
			S->DebugSetCapture(true);
			S->DebugFillCart({ 75701, 75701, 75701, 75701, 75701, 75701, 75701 });
			BuySelected(TEXT("R2b"));
			Check(NumP() == 1 && Codes(LastP()) == TArray<int32>({ 75701, 75701, 75701, 75701, 75701, 75701, 75701 }) && LastP()[1] == 1,
				FString::Printf(TEXT("R2 7 одинаковых 75701 — один 5070 с семью кодами (коды %s)"), *Fmt(Codes(LastP()))));
			S->DebugFeedBuyResult(Reply(1));
		});

		// ── D: магазин бустеров, 3 × 53672 ──
		R->Steps.Add([=]()
		{
			AsdaCVarSave::SetForTest(kKey, 1);
			UAsdaBoosterShopSubsystem* S = WB.Get(); if (!S) { return; }
			S->DebugSetCapture(true);
			S->DebugConfirmBuy(53672, 3);
			Check(NumB() == 1 && Codes(LastB()) == TArray<int32>({ 53672 }) && S->DebugBuyRunning() && S->DebugIsPending(),
				FString::Printf(TEXT("D1 бустеры ×3: ушёл ОДИН 5070 с одним кодом (пакетов %d, коды %s)"), NumB(), *Fmt(Codes(LastB()))));
			S->DebugFeedBuyResult(Reply(1));
		});
		R->Steps.Add([=]()
		{
			UAsdaBoosterShopSubsystem* S = WB.Get(); if (!S) { return; }
			Check(NumB() == 2 && Codes(LastB()) == TArray<int32>({ 53672 }), FString::Printf(TEXT("D2 вторая штука (пакетов %d)"), NumB()));
			S->DebugFeedBuyResult(Reply(1));
		});
		R->Steps.Add([=]()
		{
			UAsdaBoosterShopSubsystem* S = WB.Get(); if (!S) { return; }
			Check(NumB() == 3 && Codes(LastB()) == TArray<int32>({ 53672 }), FString::Printf(TEXT("D3 третья штука (пакетов %d)"), NumB()));
			S->DebugFeedBuyResult(Reply(1));
		});
		R->Steps.Add([=]()
		{
			UAsdaBoosterShopSubsystem* S = WB.Get(); if (!S) { return; }
			Check(NumB() == 3 && !S->DebugBuyRunning() && !S->DebugIsPending(),
				FString::Printf(TEXT("D4 три ответа — запуск окончен, лишних пакетов нет (пакетов %d)"), NumB()));
			// D2: ключ 0 — одним пакетом, как оригинал.
			AsdaCVarSave::SetForTest(kKey, 0);
			S->DebugSetCapture(true);
			S->DebugConfirmBuy(53672, 3);
			Check(NumB() == 1 && Codes(LastB()) == TArray<int32>({ 53672, 53672, 53672 }) && !S->DebugBuyRunning(),
				FString::Printf(TEXT("D5 ключ 0: один 5070 с тремя кодами, как оригинал (коды %s)"), *Fmt(Codes(LastB()))));
			S->DebugFeedBuyResult(Reply(1));
		});

		// Шаги по таймеру; в конце — возврат всего и итог.
		// ⚠ Таймер НЕ снимается изнутри своего же делегата (снятие уничтожило бы исполняемую лямбду и её захваты — AV):
		//   после итога он только помечается `bDone`, а снимает его отдельный вызов на следующем кадре.
		W->GetTimerManager().SetTimer(R->H, FTimerDelegate::CreateLambda([R, WW, WP, WB, bQuit]()
		{
			if (R->bDone) { return; }
			if (R->Step < R->Steps.Num()) { R->Steps[R->Step++](); return; }
			R->bDone = true;
			if (UAsdaPremiumShopSubsystem* S = WP.Get())
			{
				S->DebugSetCapture(false); S->DebugFillCart({}); S->DebugClearPending(); S->DebugSetOpenFlag(false);
			}
			if (UAsdaBoosterShopSubsystem* S = WB.Get()) { S->DebugSetCapture(false); S->DebugSetOpenFlag(false); }
			// Шаги держат `R` — разрываем кольцо: массив уходит в локальную и гибнет здесь, пока `R` жив захватом таймера.
			TArray<TFunction<void()>> Done = MoveTemp(R->Steps);
			Done.Reset();
			if (UWorld* Wd = WW.Get())
			{
				FTimerHandle H = R->H;
				TWeakObjectPtr<UWorld> W2 = WW;
				Wd->GetTimerManager().SetTimerForNextTick(FTimerDelegate::CreateLambda([W2, H]() mutable
				{
					if (UWorld* W3 = W2.Get()) { W3->GetTimerManager().ClearTimer(H); }
				}));
			}
			AsdaSelfTestKit::Finish(   // возврат ключа (RestoreAll снимает и сторожа мира) + итог + quit
				WW.Get(), kCmd, bQuit, LogAsdaPremShop, TEXT("САМОТЕСТ-6000-КОРЗИНА"), R->Ok, R->Fail);
		}), 0.3f, true);
	}));
#endif // ASDA_DEV_CONSOLE

// ── 10.10.2026: доступ самотеста двойного щелчка (`asda.Shop.SelfTestDblClick`) ──
int32 UAsdaPremiumShopSubsystem::DebugCartFilled() const
{
	int32 N = 0;
	for (const FAsdaPremiumCartRow& R : CartRows) { if (R.ItemCode > 0) { ++N; } }
	return N;
}

// ── 25.10.2026: самотест корзины — строки корзины как после двойных щелчков по карточкам (отмечены) ──
void UAsdaPremiumShopSubsystem::DebugFillCart(const TArray<int32>& Codes)
{
	for (int32 j = 0; j < CartRows.Num(); ++j)
	{
		CartRows[j] = FAsdaPremiumCartRow();
		if (Codes.IsValidIndex(j) && Codes[j] > 0) { CartRows[j].ItemCode = Codes[j]; CartRows[j].bChecked = true; }
	}
	RefreshCart();
}

int32 UAsdaPremiumShopSubsystem::DebugCardCode(int32 Card) const
{
	return Cards.IsValidIndex(Card) ? Cards[Card].ItemCode : 0;
}

UWidget* UAsdaPremiumShopSubsystem::DebugCardIcon(int32 Card) const
{
	return Cards.IsValidIndex(Card) ? Cards[Card].ItemIcon.Get() : nullptr;
}
