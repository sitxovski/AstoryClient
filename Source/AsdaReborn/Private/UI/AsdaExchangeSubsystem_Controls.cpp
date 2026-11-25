// Copyright (c) 2026 @sitxovski (Discord)
// AsdaExchangeSubsystem_Controls.cpp — ★ R2 10.11.2026 (разрез AsdaExchangeSubsystem.cpp, дословный перенос): SetupControls (39 КБ), подписи, категории.
//   Общее для частей — в AsdaExchangeSubsystemInternal.h; шапка и реверс — в AsdaExchangeSubsystem.cpp.
#include "UI/AsdaExchangeSubsystem.h"
#include "AsdaDevConsole.h"   // ★ античит А2: ASDA_DEV_CVAR / ASDA_DEV_CONSOLE — дев-ключи и команды вне Shipping
#include "AsdaSelf.h"   // ★ волна 1: «это я?» / мой номер / мой уровень — общая точка ядра (E2, E4)
#include "UI/AsdaNotice.h"   // ★ волна 1: фасад нотисов (Loc / Chat / MsgBox)
#include "UI/AsdaTradeStateSubsystem.h"   // ★ 29.09.2026: окно обмена 2008 открыто — гейт режимов и отложенное золото 9913

#include "AsdaNetSubsystem.h"
#include "AsdaBytes.h"
#include "AsdaPacketWriter.h"   // ★ U6 (В1 дедупа №2): записи 9901/9905/9909 через общий писатель
#include "Login/AsdaButtonRelay.h"
// GetCurrentNpcId — NPC-аукционщик в шапке 99xx (+10)   ★ R3: комментарий снятого включения "Npc/AsdaNpcDialogSubsystem.h" (файл им не пользовался) — сохранён
#include "UI/AsdaIconCellGrid.h"
#include "UI/AsdaJuiComboBox.h"
// имя предмета и подстановка аргументов нотиса   ★ R3: комментарий снятого включения "Text/AsdaTextSubsystem.h" (файл им не пользовался) — сохранён
#include "UI/AsdaItemTooltipSubsystem.h"   // общая точка подсказки предмета
#include "UI/AsdaMessageSubsystem.h"
#include "UI/AsdaUiHelpers.h"
#include "UI/AsdaCommunityFriendSubsystem.h"   // ★ общая точка AsdaCommunityUi::ShownVisibilityFor
#include "UI/AsdaWindowSubsystem.h"
#include "UI/AsdaWarehouseSubsystem.h"   // ★ пачка 20, жалоба 10: склад 4000 открыт — рынок не открывается
#include "World/AsdaWorldSubsystem.h"      // GetSelfCharName — имя игрока в записи 9901

#include "Blueprint/UserWidget.h"
#include "Blueprint/WidgetTree.h"
#include "Components/Button.h"
#include "Components/CanvasPanel.h"
#include "Components/CanvasPanelSlot.h"
#include "Components/CheckBox.h"
#include "Components/ComboBoxString.h"
#include "Components/EditableText.h"
#include "Components/Image.h"
#include "Components/TextBlock.h"
#include "Engine/GameInstance.h"
#include "TimerManager.h"
#include "Engine/World.h"
#include "HAL/IConsoleManager.h"
// ★ С-Р1: общий офлайн-гейт отладочных команд (RefuseDebugWhenOnline)   ★ R3: комментарий снятого включения "AsdaOutgoingGate.h" (файл им не пользовался) — сохранён
#include "Test/AsdaSelfTestKit.h"   // ★ 11.10.2026 (В3 S1): общий каркас самотестов (аргументы, отказ, перехват, итог, выход)
#include "Login/AsdaLoginData.h"   // [ПОРТ] 08.10.2026: общий кодек провода (гибрид RU+EN+AR)
#include "AsdaExchangeSubsystemInternal.h"   // ★ R2 10.11.2026: общее для частей AsdaExchangeSubsystem*.cpp

// [ОРИГИНАЛ] Жалоба 5: три большие галочки «выбрать всё» 13478/13479/13482 (в порте не были привязаны).
static ASDA_DEV_CVAR(int32, CVarExchangeSelectAll,
	TEXT("asda.Exchange.SelectAll"), 1,
	TEXT("Аукцион: 1 — большие галочки «выбрать всё» отмечают все строки панели; 0 — галочки не привязаны, как до 09.11."),
	ECVF_Default);

namespace {
	// Имена контролов строк — по IDNum из JUI (закон «по имени адресовать нельзя»: у сетки
	//   выставления имена сбиты на единицу с 13112, у гнёзд самоцветов строк 2…8 `ID` пустой).
	FString SellCellIdc(int32 I)
	{
		// 13064…13111 → SELL0…SELL47; 13112…13123 → SELL49…SELL60 (пропущен SELL48, дефект D2).
		return FString::Printf(TEXT("EXCHANGE_SELL%d"), (I <= 47) ? I : (I + 1));
	}
	FString RegRowIdc(int32 I)  { return FString::Printf(TEXT("EXCH_ITEM%02d"), I + 1); }
	FString MyRowIdc(int32 I)   { return FString::Printf(TEXT("EXCHMY_ITEM%02d"), I + 1); }
	FString LotRowIdc(int32 I)  { return FString::Printf(TEXT("EXCHANGER_ITEM%02d"), I + 1); }
}

// ═══════════════════════ связывание контролов ═════════════════════════════════════════════════
void UAsdaExchangeSubsystem::SetupControls()
{
	bControlsReady = false;
	if (!Widget || !Widget->WidgetTree) { return; }

	SellIcons.SetNum(kSellCells);   SellLabels.SetNum(kSellCells);
	RegIcons.SetNum(kRegRows);      RegFeeTexts.SetNum(kRegRows);
	RegChecks.SetNum(kRegRows);     RegPriceEdits.SetNum(kRegRows);
	RegDueCombos.SetNum(kRegRows);  RegArrUp.SetNum(kRegRows); RegArrDn.SetNum(kRegRows);
	MyIcons.SetNum(kMyRows);        MyNames.SetNum(kMyRows);
	MyPrices.SetNum(kMyRows);       MyTimes.SetNum(kMyRows);  MyChecks.SetNum(kMyRows);
	LotIcons.SetNum(kLotRows);      LotNames.SetNum(kLotRows);
	LotPrices.SetNum(kLotRows);     LotSellers.SetNum(kLotRows); LotChecks.SetNum(kLotRows);
	LotSowelIcons.SetNum(kLotRows * kLotSowels);

	TArray<UWidget*> All;
	Widget->WidgetTree->GetAllWidgets(All);

	for (UWidget* W : All)
	{
		if (!W) { continue; }
		const FString N = W->GetName();

		if (UButton* Btn = Cast<UButton>(W))
		{
			if (N == TEXT("EXCHANGE_TITLE_DEL")) { BtnTitleDel   = Btn; continue; }
			if (N == TEXT("EXCH_SELDEL"))        { BtnSelDel     = Btn; continue; }
			if (N == TEXT("EXCH_OK"))            { BtnRegOk      = Btn; continue; }
			if (N == TEXT("EXCH_CANCEL"))        { BtnRegCancel  = Btn; continue; }
			if (N == TEXT("EXCHMY_DEL"))         { BtnMyDel      = Btn; continue; }
			if (N == TEXT("EXCHMY_CLOSE"))       { BtnMyClose    = Btn; continue; }
			if (N == TEXT("EXCHANGER_BUY"))      { BtnBuy        = Btn; continue; }
			if (N == TEXT("EXCHANGER_CANCEL"))   { BtnExchCancel = Btn; continue; }
			if (N == TEXT("EXCHANGER_RESCAN"))   { BtnRescan     = Btn; continue; }
			// ★ Кнопки пагинации — клик ловит ВЛОЖЕННЫЙ контрол (`…BTN1`), у него и скин.
			if (N == TEXT("EXCHANGER_PREBTN1"))  { BtnPagePrev   = Btn; continue; }
			if (N == TEXT("EXCHANGER_NEXTBTN1")) { BtnPageNext   = Btn; continue; }
			if (N == TEXT("EXCHANGER_STATBTN1")) { BtnPageFirst  = Btn; continue; }
			if (N == TEXT("EXCHANGER_ENDBTN1"))  { BtnPageLast   = Btn; continue; }
			// ★ Наборы КАТЕГОРИЙ поиска — в данных это `RadioButton`, поэтому импортёр делает по
			//   кнопке на пункт (`{idc}_OPT{i}`): 13408 — ВОСЕМЬ (режим 5), 13409 — СЕМЬ (режим 4).
			//   Выбранный пункт и есть `searchCritSel1` / `searchCritSel2` эталона.
			if (N.StartsWith(TEXT("EXCHANGER_RADIOBTN1_OPT")))
			{
				const int32 I = FCString::Atoi(*N.RightChop(23));
				if (I >= 0 && I < 32) { if (CatBtns1.Num() <= I) { CatBtns1.SetNum(I + 1); } CatBtns1[I] = Btn; }
				continue;
			}
			if (N.StartsWith(TEXT("EXCHANGER_RADIOBTN2_OPT")))
			{
				const int32 I = FCString::Atoi(*N.RightChop(23));
				if (I >= 0 && I < 32) { if (CatBtns2.Num() <= I) { CatBtns2.SetNum(I + 1); } CatBtns2[I] = Btn; }
				continue;
			}
			// ★ Переключатель категорий «моих лотов» 13260 — ТОЖЕ RadioButton (`TabBar_W109_2Btn`),
			//   а не комбобокс: прежняя привязка через `UAsdaJuiComboBox` не находила контрол.
			if (N.StartsWith(TEXT("EXCHMY_CateSelBtn_OPT")))
			{
				const int32 I = FCString::Atoi(*N.RightChop(21));
				if (I >= 0 && I < 8) { if (MyCatBtns.Num() <= I) { MyCatBtns.SetNum(I + 1); } MyCatBtns[I] = Btn; }
				continue;
			}
			continue;
		}
		if (UCheckBox* Cb = Cast<UCheckBox>(W))
		{
			for (int32 R = 0; R < kRegRows; ++R)
			{
				if (N == FString::Printf(TEXT("EXCH_ITEMCHK%02d"), R + 1)) { RegChecks[R] = Cb; }
			}
			for (int32 R = 0; R < kMyRows; ++R)
			{
				if (N == FString::Printf(TEXT("EXCHMY_CHK%02d"), R + 1)) { MyChecks[R] = Cb; }
			}
			for (int32 R = 0; R < kLotRows; ++R)
			{
				if (N == FString::Printf(TEXT("EXCHANGER_ITEMCHK%02d"), R + 1)) { LotChecks[R] = Cb; }
			}
			// ★ Пачка 20, жалоба 5: большие галочки «выбрать всё» (`BigBox_Check`) — в ассете есть, порт их не брал.
			if (N == TEXT("EXCH_ALLCHK"))      { ChkRegAll = Cb; }
			if (N == TEXT("EXCHMY_ALLCHK"))    { ChkMyAll  = Cb; }
			if (N == TEXT("EXCHANGER_ALLCHK")) { ChkLotAll = Cb; }
			continue;
		}
		if (UEditableText* Ed = Cast<UEditableText>(W))
		{
			for (int32 R = 0; R < kRegRows; ++R)
			{
				if (N == FString::Printf(TEXT("EXCH_EDIT_PRICE%02d"), R + 1)) { RegPriceEdits[R] = Ed; }
			}
			continue;
		}
		if (UTextBlock* Tb = Cast<UTextBlock>(W))
		{
			if (N == TEXT("EXCH_SUBTITLE"))       { TxtSubTitle  = Tb; continue; }
			if (N == TEXT("EXCH_MONEY"))          { TxtMoney     = Tb; continue; }
			if (N == TEXT("EXCH_TOTALFEE"))       { TxtTotalFee  = Tb; continue; }
			if (N == TEXT("EXCHMY_AMOUNT"))       { TxtMyAmount  = Tb; continue; }
			if (N == TEXT("EXCHMY_REGGOLD"))      { TxtMyRegGold = Tb; continue; }
			if (N == TEXT("EXCHANGER_BUYPRICE2")) { TxtBuyPrice  = Tb; continue; }
			if (N == TEXT("EXCHANGER_GOLD1"))     { TxtExchGold  = Tb; continue; }
			if (N == TEXT("EXCHANGER_PAGE1"))     { TxtPage1     = Tb; continue; }
			if (N == TEXT("EXCHANGER_PAGE2"))     { TxtPage2     = Tb; continue; }
			// ★ Подписи трёх фильтров: их текст ДИНАМИЧЕСКИЙ (эталон пишет в +299/+300/+301
			//   при каждом заполнении списков), статический `CaptionText` из JUI — только заготовка.
			if (N == TEXT("EXCHANGER_TXT1"))      { TxtFilter1   = Tb; continue; }
			if (N == TEXT("EXCHANGER_TXT2"))      { TxtFilter2   = Tb; continue; }
			if (N == TEXT("EXCHANGER_TXT3"))      { TxtFilter3   = Tb; continue; }
			// Подписи двух пунктов переключателя «моих лотов» (нотисы 98200 / 98201).
			if (N == TEXT("EXCHMY_CateSelBtn_Text"))
			{
				if (MyCatTexts.Num() < 1) { MyCatTexts.SetNum(1); }
				MyCatTexts[0] = Tb; continue;
			}
			if (N == TEXT("EXCHMY_CateSelBtn_Text2"))
			{
				if (MyCatTexts.Num() < 2) { MyCatTexts.SetNum(2); }
				MyCatTexts[1] = Tb; continue;
			}
			// ★★★ ПОДПИСИ ВКЛАДОК НАБОРОВ КАТЕГОРИЙ. Импортёр даёт ОДНУ подпись на контрол
			//   (`{idc}_Text`), остальные кладёт шаг `asda_ui_tabbar_captions` ПУСТЫМИ — текст в
			//   оригинале ставит рантайм по id локализации (`uiOptionListboxInit@0xD544A0`).
			//   Индекс: `_Text` = вкладка 0, `_Text2` = вкладка 1, …
			{
				auto BindTab = [&N, Tb](const TCHAR* Idc, TArray<TObjectPtr<UTextBlock>>& Arr) -> bool
				{
					const FString Pre = FString(Idc) + TEXT("_Text");
					if (!N.StartsWith(Pre)) { return false; }
					const FString Tail = N.RightChop(Pre.Len());
					int32 I = 0;
					if (Tail.IsEmpty()) { I = 0; }
					else
					{
						for (TCHAR C : Tail) { if (!FChar::IsDigit(C)) { return false; } }
						I = FCString::Atoi(*Tail) - 1;
					}
					if (I < 0 || I >= 32) { return false; }
					if (Arr.Num() <= I) { Arr.SetNum(I + 1); }
					Arr[I] = Tb;
					return true;
				};
				if (BindTab(TEXT("EXCHANGER_RADIOBTN1"), CatTexts1)) { continue; }
				if (BindTab(TEXT("EXCHANGER_RADIOBTN2"), CatTexts2)) { continue; }
			}
			for (int32 R = 0; R < kRegRows; ++R)
			{
				if (N == FString::Printf(TEXT("EXCH_ITEMPRICE%02d"), R + 1)) { RegFeeTexts[R] = Tb; }
			}
			for (int32 R = 0; R < kMyRows; ++R)
			{
				if (N == FString::Printf(TEXT("EXCHMY_NAME%02d"), R + 1))  { MyNames[R]  = Tb; }
				if (N == FString::Printf(TEXT("EXCHMY_PRICE%02d"), R + 1)) { MyPrices[R] = Tb; }
				if (N == FString::Printf(TEXT("EXCHMY_TIME%02d"), R + 1))  { MyTimes[R]  = Tb; }
			}
			for (int32 R = 0; R < kLotRows; ++R)
			{
				if (N == FString::Printf(TEXT("EXCHANGER_ITEMNAME%02d"), R + 1)) { LotNames[R]   = Tb; }
				if (N == FString::Printf(TEXT("EXCHANGER_ITEMPRI%02d"),  R + 1)) { LotPrices[R]  = Tb; }
				if (N == FString::Printf(TEXT("EXCHANGER_ITEMSEL%02d"),  R + 1)) { LotSellers[R] = Tb; }
			}
			continue;
		}

		// Планы: `<idc>_<juiIdx>_<plane>`.
		FString Idc; int32 JuiIdx = 0, Plane = 0;
		if (AsdaUi::ParsePlaneName(N, Idc, JuiIdx, Plane))
		{
			if (Idc == TEXT("EXCHANGE_SELECTED1")) { SellSelFrame = W; continue; }
			if (UImage* Img = Cast<UImage>(W))
			{
				for (int32 C = 0; C < kSellCells; ++C)
				{
					if (Idc == SellCellIdc(C)) { SellIcons[C] = Img; }
				}
				for (int32 R = 0; R < kRegRows; ++R) { if (Idc == RegRowIdc(R)) { RegIcons[R] = Img; } }
				for (int32 R = 0; R < kMyRows;  ++R) { if (Idc == MyRowIdc(R))  { MyIcons[R]  = Img; } }
				for (int32 R = 0; R < kLotRows; ++R) { if (Idc == LotRowIdc(R)) { LotIcons[R] = Img; } }
				// ★★★ P2, СТРОКА 1 выдачи: её пять гнёзд в данных `ID` ИМЕЮТ
				//   (`EXCHANGER_BB1_1`…`_5`), поэтому импортёр назвал их планы штатно.
				for (int32 K = 0; K < kLotSowels; ++K)
				{
					if (Idc == FString::Printf(TEXT("EXCHANGER_BB1_%d"), K + 1))
					{
						LotSowelIcons[0 * kLotSowels + K] = Img;
					}
				}
			}
		}
	}

	// ★★★ P2, СТРОКИ 2…8 выдачи: у их гнёзд `ID` в данных ПУСТ, поэтому импортёр давал планам
	//   имя по СКИНУ с индексом обхода (`NoImage_icon_<idx>_0`) — адресовать нельзя, индекс едет
	//   от любой правки данных. Узлы с устойчивыми именами кладёт В АССЕТ шаг пост-импорта
	//   `Tools/asda_ui_missing_interactive.py` по ОБЩЕМУ правилу `asda_jui_parse.anchored_names`
	//   (`{ЯКОРЬ}_ICO{NN}`, якорь — `ID` контейнера `EXCHANGER_BB{n}_0`); таблица разрешённых
	//   контролов — `asda_jui_parse.ICON_HOLDER_NODES`, сторожа — `asda_ui_post_import.HANDMADE`.
	for (int32 R = 1; R < kLotRows; ++R)
	{
		for (int32 K = 0; K < kLotSowels; ++K)
		{
			const FName Nm(*FString::Printf(TEXT("EXCHANGER_BB%d_0_ICO%02d"), R + 1, K));
			LotSowelIcons[R * kLotSowels + K] = Cast<UImage>(Widget->GetWidgetFromName(Nm));
		}
	}

	// ── Поддеревья трёх панелей и двух наборов категорий ──
	CollectByIdc(TEXT("EXCH_REGISTRATION"),   GrpRegistration);
	CollectByIdc(TEXT("EXCH_MYITEM"),         GrpMyItem);
	CollectByIdc(TEXT("EXCHANGER_WIN"),       GrpExchanger);
	// ★ Наборы категорий собираем ОТДЕЛЬНЫМ сборщиком: он отбрасывает планы ВТОРОГО, дублирующего
	//   объявления `EXCHANGER_RADIOBTN2` в данных и включает в группу кнопки `_OPT<i>` и подписи.
	CollectRadioSet(TEXT("EXCHANGER_RADIOBTN1"), CatBtns1.Num(), GrpRadio1);
	CollectRadioSet(TEXT("EXCHANGER_RADIOBTN2"), CatBtns2.Num(), GrpRadio2);

	// ★★★ ПЛАНЫ-ПЛАШКИ ВКЛАДОК (жалоба владельца №2, 2026-08-28: «активная вкладка должна быть
	//   чёрной с белым текстом и белой рамкой»). Скин переключателя даёт КАДРАМ РАЗНЫЕ КАРТИНКИ,
	//   а не только цвет подписи: `TabBar_W72_8Btn` (`JuiSkin.ini:60823`) кадр 0 — UV
	//   (0.496,0.586)-(0.777,0.672), Diffuse FFC0C0C0 (СВЕТЛАЯ плашка), кадр 2 — UV
	//   (0.496,0.484)-(0.777,0.570), Diffuse FFFFFFFF. Замер атласа `ne_skillbar3.png` 256×256:
	//   регион кадра 2 = пиксели (126,123)-(198,145) — ЧЁРНАЯ середина в БЕЛОЙ РАМКЕ; кадр 0 —
	//   сплошная светлая плашка. То есть «чёрный фон + белая рамка» уже нарисованы в атласе,
	//   их надо лишь ПОКАЗАТЬ на активной вкладке.
	//   Кадр 2 УЖЕ ЛЕЖИТ В АССЕТЕ — `UAsdaUmgBuilder::AddOptionButton` кладёт его в
	//   `FButtonStyle::Pressed` кнопки `{idc}_OPT<i>` (`AsdaUmgBuilder.cpp`, `Style.SetPressed`).
	//   Рантайму остаётся перенести кисть на план (состояние «какая вкладка активна» в ассет не
	//   уносится — правило владельца). Планы берём из УЖЕ ОЧИЩЕННЫХ групп, поэтому дублирующий
	//   набор планов `EXCHANGER_RADIOBTN2` сюда не попадает.
	auto CollectPlanes = [](const TArray<TObjectPtr<UWidget>>& Grp, const TCHAR* Idc,
		TArray<TObjectPtr<UImage>>& Out)
	{
		Out.Reset();
		for (const TObjectPtr<UWidget>& W : Grp)
		{
			FString ParsedIdc; int32 JuiIdx = 0, Plane = 0;
			UImage* Img = Cast<UImage>(W.Get());
			if (!Img || !AsdaUi::ParsePlaneName(Img->GetName(), ParsedIdc, JuiIdx, Plane)) { continue; }
			if (ParsedIdc != Idc || Plane < 0 || Plane >= 32) { continue; }
			if (Out.Num() <= Plane) { Out.SetNum(Plane + 1); }
			Out[Plane] = Img;
		}
	};
	CollectPlanes(GrpRadio1, TEXT("EXCHANGER_RADIOBTN1"), CatPlanes1);
	CollectPlanes(GrpRadio2, TEXT("EXCHANGER_RADIOBTN2"), CatPlanes2);

	// ★★★ ДУБЛИРУЮЩЕЕСЯ ОБЪЯВЛЕНИЕ ДАЁТ ДВА ВИДЖЕТА С ОДНИМ ИМЕНЕМ (найдено 2026-08-29 пробой
	//   `Tools/_probe_exch_tabs.py`). У `EXCHANGER_RADIOBTN2` в дереве ассета РОВНО ПО ДВА узла
	//   `_OPT0…_OPT4` (пять от объявления `TabBar_W77_5Btn`, шаг 78 px, и пять от настоящего
	//   `TabBar_W72_7Btn`, шаг 73 px) и только по одному `_OPT5`/`_OPT6`. Имена совпадают, поэтому
	//   и привязка в C++ (`GetAllWidgets`, побеждает последний), и шаг подписей (обход стеком,
	//   побеждает первый) выбирают их НЕЗАВИСИМО ДРУГ ОТ ДРУГА — набор мог разъехаться: кадровые
	//   цвета скина ложились в одну кнопку, а рантайм читал их из другой. У индекса 0 это давало
	//   `bFromSkin = false` и эвристику вместо цвета кадра 2 — подпись «Кольцо» уходила в чёрный
	//   на чёрной активной вкладке, то есть ПРОПАДАЛА (жалоба владельца, скрин 1).
	//   ⇒ Привязываем кнопку по ГЕОМЕТРИИ: берём ту, чей слот совпадает со слотом ОСТАВЛЕННОГО
	//   плана этой же вкладки. Имена врать могут, числа — нет (закон «именам не верь»).
	auto FixupBtnsByPlaneGeometry = [this](const TCHAR* Idc, const TArray<TObjectPtr<UImage>>& Planes,
		TArray<TObjectPtr<UButton>>& Btns)
	{
		if (!Widget || !Widget->WidgetTree) { return; }
		TArray<UWidget*> All;
		Widget->WidgetTree->GetAllWidgets(All);
		int32 Fixed = 0;
		for (int32 I = 0; I < Planes.Num(); ++I)
		{
			const UCanvasPanelSlot* PS = Planes[I] ? Cast<UCanvasPanelSlot>(Planes[I]->Slot) : nullptr;
			if (!PS) { continue; }
			const FMargin PO = PS->GetOffsets();
			const FString Want = FString::Printf(TEXT("%s_OPT%d"), Idc, I);
			for (UWidget* W : All)
			{
				UButton* B = Cast<UButton>(W);
				if (!B || B->GetName() != Want) { continue; }
				const UCanvasPanelSlot* BS = Cast<UCanvasPanelSlot>(B->Slot);
				if (!BS) { continue; }
				const FMargin BO = BS->GetOffsets();
				if (FMath::Abs(BO.Left - PO.Left) > 1.f || FMath::Abs(BO.Right - PO.Right) > 1.f) { continue; }
				if (Btns.IsValidIndex(I) && Btns[I] != B) { Btns[I] = B; ++Fixed; }
				break;
			}
		}
		if (Fixed > 0)
		{
			UE_LOG(LogAsdaExchange, Log,
				TEXT("аукцион: набор %s — кнопок перепривязано по геометрии плана: %d (в дереве есть одноимённые дубли)"),
				Idc, Fixed);
		}
	};
	FixupBtnsByPlaneGeometry(TEXT("EXCHANGER_RADIOBTN1"), CatPlanes1, CatBtns1);
	FixupBtnsByPlaneGeometry(TEXT("EXCHANGER_RADIOBTN2"), CatPlanes2, CatBtns2);
	{
		TArray<TObjectPtr<UWidget>> Tmp;
		CollectByIdc(TEXT("EXCHMY_CateSelBtn"), Tmp);
		CollectPlanes(Tmp, TEXT("EXCHMY_CateSelBtn"), MyCatPlanes);
	}
	UE_LOG(LogAsdaExchange, Log, TEXT("аукцион: планов вкладок — 13408 %d, 13409 %d, 13260 %d"),
		CatPlanes1.Num(), CatPlanes2.Num(), MyCatPlanes.Num());

	// ★ Узлы, которые не показываются НИКОГДА. Помимо SEL-рамок сюда попадают недоделанный в
	//   оригинале фильтр самоцветов (13501 `EXCHANGER_TXT4` + 13503 `EXCHANGER_SEARCH4`) и
	//   несвязанный `EXCHANGER_RADIOBTN3` (13500) — дефект D1 отчёта.
	AlwaysHidden.Reset();
	{
		TArray<TObjectPtr<UWidget>> Tmp;
		// ★★★ ГНЁЗДА САМОЦВЕТОВ СТРОК ВЫДАЧИ (13502 и 13510…13585) — В ОРИГИНАЛЕ НЕ РИСУЮТСЯ.
		//   Доказано ДВУМЯ методами (жалоба владельца 2026-08-30 «в строке лота лишние иконки,
		//   похожие на сапоги»):
		//   1) РЕНДЕР строки — `CSatisfyWindow_RefreshItemSlots@0xD58810` (`ui_core.c:52512+`):
		//      у строки РОВНО ПЯТЬ элементов, поля `11*slot + 201…205`:
		//        +201 иконка (`CControl_DrawTextureRect` + подпись количества),
		//        +202 имя (цвет = `CItemManager_GetDropColor`), +203 цена, +204 продавец,
		//        +205 галочка (`SetVisibleState` по наличию предмета). Больше НИЧЕГО.
		//   2) ИНИЦИАЛИЗАЦИЯ — `uiInitDialogControls` (`ui_core.c:52044-52047`): на строку
		//      ровно ЧЕТЫРЕ текстовых поля `44*i + 804/808/812/816` (44 б = те же 11 dword).
		//   ⇒ Наполнение гнёзд было НАШЕЙ ВЫДУМКОЙ (заход P2, §13.2), да ещё и кодами из полей
		//   `+25…+44` записи 9904, а это **5×i32 ОПЦИИ ЗАТОЧКИ** (§7.4), а не коды самоцветов —
		//   значения опций попадали в `GetItemIcon` и давали случайные картинки. Гасим (закон
		//   `no-invented-ui`); сетка гнёзд больше не строится.
		for (int32 R = 1; R <= kLotRows; ++R)
		{
			CollectByIdc(FString::Printf(TEXT("EXCHANGER_BB%d_0"), R), Tmp);
			AlwaysHidden.Append(Tmp);
			for (int32 C = 1; C <= kLotSowels; ++C)
			{
				CollectByIdc(FString::Printf(TEXT("EXCHANGER_BB%d_%d"), R, C), Tmp);
				AlwaysHidden.Append(Tmp);
			}
		}
		CollectByIdc(TEXT("EXCHANGER_SOWEL"), Tmp);
		AlwaysHidden.Append(Tmp);
		for (const TCHAR* Idc : { TEXT("EXCHANGE_SELECTED1"), TEXT("EXCH_ITEMSELECTED"),
		                          TEXT("EXCHANGER_SELECTED"), TEXT("EXCHANGER_TXT4"),
		                          TEXT("EXCHANGER_SEARCH4"),  TEXT("EXCHANGER_RADIOBTN3"),
		                          TEXT("Exchange_AniGaer") })
		{
			CollectByIdc(Idc, Tmp);
			AlwaysHidden.Append(Tmp);
		}
	}

	// ── Комбобоксы ──
	auto MakeCombo = [this](const TCHAR* Idc) -> UAsdaJuiComboBox*
	{
		UAsdaJuiComboBox* C = NewObject<UAsdaJuiComboBox>(this);
		if (!C->Setup(Widget, Idc, kWindowId, /*bOpenUp*/ false))
		{
			UE_LOG(LogAsdaExchange, Warning, TEXT("аукцион: комбобокс %s не найден в дереве окна"), Idc);
		}
		return C;
	};
	CmbKind  = MakeCombo(TEXT("EXCHANGER_SEARCH1"));
	CmbClass = MakeCombo(TEXT("EXCHANGER_SEARCH2"));
	CmbLevel = MakeCombo(TEXT("EXCHANGER_SEARCH3"));
	CollectByIdc(TEXT("EXCHANGER_SEARCH1"), GrpFilter1);
	CollectByIdc(TEXT("EXCHANGER_SEARCH2"), GrpFilter2);
	CollectByIdc(TEXT("EXCHANGER_SEARCH3"), GrpFilter3);
	// ★★★ ПОЛЯ ЦЕНЫ — ЖИВАЯ ПРИВЯЗКА (жалоба владельца №6, 2026-08-28). Эталон
	//   `consignRegWnd_OnAmountEdit@0xD4E330` обрабатывает КАЖДОЕ событие поля: читает `atoi`,
	//   кладёт цену в строку лота и зовёт `ItemListWnd_RefreshRows` (пересчёт комиссии и итога).
	//   В порте этого не было вовсе: цена читалась только внутри `RefreshRegRows`, а её при наборе
	//   текста никто не звал ⇒ комиссия оставалась нулевой, а `SendRegister` видел цену 0 и
	//   печатал 12699. `OnTextCommitted` — эталонная ветка `a3 == 67587` (Enter): нормализуем текст.
	for (int32 R = 0; R < kRegRows; ++R)
	{
		if (!RegPriceEdits.IsValidIndex(R) || !RegPriceEdits[R]) { continue; }
		RegPriceEdits[R]->OnTextChanged.AddUniqueDynamic(this, &UAsdaExchangeSubsystem::OnPriceTextChanged);
		RegPriceEdits[R]->OnTextCommitted.AddUniqueDynamic(this, &UAsdaExchangeSubsystem::OnPriceTextCommitted);
	}

	for (int32 R = 0; R < kRegRows; ++R)
	{
		RegDueCombos[R] = MakeCombo(*FString::Printf(TEXT("EXCH_DUE%02d"), R + 1));
		// Сроки — ровно те, что понимает сервер: 1 / 3 / 5 / 7 дней (коды 24 / 72 / 120 / 168 ч).
		//   ★ Четвёртый пункт в оригинале отсутствовал (дефект D8) — добавлен, ставка 7 % серверная.
		RegDueCombos[R]->SetItems({ TEXT("1"), TEXT("3"), TEXT("5"), TEXT("7") });
		RegDueCombos[R]->SetSelectedIndex(0);
		const int32 Row = R;
		RegDueCombos[R]->OnPicked.BindWeakLambda(this, [this, Row](int32 Idx)
		{
			static const int32 kDays[4] = { 1, 3, 5, 7 };
			if (RegRows.IsValidIndex(Row) && Idx >= 0 && Idx < 4) { RegRows[Row].DueDays = kDays[Idx]; }
			RefreshRegRows();
			RefreshRegTotals();
		});
	}

	// ★★★ P1: три списка критериев наполняет `FillAttributeLists` по ТЕКУЩЕЙ КАТЕГОРИИ; здесь —
	//   только маршрутизация выбора пункта в поля `searchParam*` (порт `charAppearanceSetPartIndex`).
	if (CmbKind)  { CmbKind->OnPicked.BindWeakLambda(this,  [this](int32 I) { OnFilterPicked(0, I); }); }
	if (CmbClass) { CmbClass->OnPicked.BindWeakLambda(this, [this](int32 I) { OnFilterPicked(1, I); }); }
	if (CmbLevel) { CmbLevel->OnPicked.BindWeakLambda(this, [this](int32 I) { OnFilterPicked(2, I); }); }

	// ── Сетки иконок ──
	// ★★★ ДЕФЕКТ ПОРТА, НАЙДЕННЫЙ 2026-08-27 (жалоба «открывается окно без моих предметов, пустые
	//   ячейки»). Сеткам передавались имена `EXCHANGE_SELL0`, `EXCH_ITEM01`, `EXCHMY_ITEM01`,
	//   `EXCHANGER_ITEM01` — то есть `ID` КОНТРОЛА. Но в ассете под этим именем лежит `TextBlock`
	//   (подпись количества), а ИКОНКА — это ПЛАН `<idc>_<juiIdx>_0` типа `Image`
	//   (живой дамп `Tools/_probe_exchange_tree.py`: `EXCHANGE_SELL0` = TextBlock 10×14,
	//   `EXCHANGE_SELL0_84_0` = Image 32×32). `UAsdaIconCellGrid::Refresh` делает
	//   `Cast<UImage>(CellWidget(Cell))` и на `TextBlock` молча выходит ⇒ НИ ОДНА иконка НИКОГДА
	//   не рисовалась ни в одной из четырёх сеток. Клик-зона тоже строилась по слоту подписи —
	//   на 18 px НИЖЕ самой ячейки.
	//   ⇒ Имена берём у УЖЕ СВЯЗАННЫХ выше `Image`-планов (`SellIcons` / `RegIcons` / `MyIcons` /
	//   `LotIcons`), ровно как это делает сетка гнёзд самоцветов.
	auto MakeGridFromIcons = [this](const TCHAR* Tag, const TArray<TObjectPtr<UImage>>& Icons,
		float CellW, float CellH, TFunction<int32(int32)> Code, TFunction<void(int32)> Click) -> UAsdaIconCellGrid*
	{
		TArray<FName> Names; Names.Reserve(Icons.Num());
		int32 Bound = 0;
		for (const TObjectPtr<UImage>& Img : Icons)
		{
			Names.Add(Img ? Img->GetFName() : NAME_None);
			if (Img) { ++Bound; }
		}
		UAsdaIconCellGrid* G = NewObject<UAsdaIconCellGrid>(this);
		G->SetOverlayTag(Tag);            // ★ уникальные имена клик-зон: сеток в окне пять
		G->SetCellSizePx(CellW, CellH);
		// ★★★ ИКОНКА ВСЕГДА 32×32, КАК В ДАННЫХ (жалоба владельца 2026-08-28 «иконка растянута на
		//   всё окно»). Клик-зона у списков — ВСЯ СТРОКА (537 и 722 px, §14.3), и до этой правки
		//   её ширина уезжала в размер картинки: живой дамп показывал
		//   `EXCHANGER_ITEM01_334_0 … слот(21,0 722x722)`. Размер ячейки-иконки в `.JUI` у всех
		//   пяти сеток окна 13000 — `Size = "32 32"`.
		G->SetIconSizePx(32.f, 32.f);
		G->bClearEmptyCells = true;
		// ★★★ ТУЛТИП ПРЕДМЕТА (жалоба владельца 2026-08-28: «подсказки нет»). Подключаем ОБЩУЮ
		//   ТОЧКУ `UAsdaItemTooltipSubsystem` через новый hover-колбэк сетки — второй реализации
		//   тултипа не появляется. Владелец плашки — свой (`EAsdaTipOwner::Exchange`), иначе
		//   инвентарь погасил бы её тем же кадром (закон `ui-shared-tip-ownership`).
		//   Эталон показывает подсказку по msg 8 контрола ячейки, гасит по msg 0xA — это и есть
		//   пара «наведение / уход».
		{
			TFunction<int32(int32)> CodeForTip = Code;   // копия провайдера: `Code` уедет в MoveTemp
			G->SetHoverHandlers(
				[this, CodeForTip](int32 Cell)
				{
					const int32 ItemCode = CodeForTip ? CodeForTip(Cell) : 0;
					if (ItemCode <= 0) { return; }
					if (UAsdaItemTooltipSubsystem* Tip = TooltipSys())
					{
						// ★★★ НЕ `ShowItemTooltipByCode` (жалоба владельца 2026-08-30 «подсказка
						//   обрезана по ширине и высоте»). Тот построитель — порт
						//   `itemTooltipBuildByCode@0xEA6B80` («имя + описание») и работает с
						//   метриками `WidthAdd = 27 / ExtraLines = 1` — узкая рамка на две
						//   строки, в которую длинное описание не влезает.
						//   Ячейка строки лота — ЭТО ЯЧЕЙКА ПРЕДМЕТА, и у эталона для ячеек без
						//   57-байтовой записи слота есть свой путь: `uiInventoryShowSlotTooltip@0xB87200`
						//   собирает запись на стеке и зовёт ОБЫЧНЫЙ `ItemTooltip_Build` — то есть
						//   полный состав и штатный расчёт размера окна. В порте это
						//   `ShowPreviewSlotTooltip` (та же общая точка, своей копии не заводим).
						//   Правка ТОЧЕЧНАЯ: меняется только вызов аукциона, подсказки инвентаря,
						//   магазина и склада идут своими путями и не затронуты.
						Tip->ShowPreviewSlotTooltip(ItemCode, EAsdaTipOwner::Exchange);
					}
				},
				[this]()
				{
					if (UAsdaItemTooltipSubsystem* Tip = TooltipSys())
					{
						Tip->HideTooltipFor(EAsdaTipOwner::Exchange);
					}
				});
		}
		G->SetupNamed(Widget, Names, /*bMale*/ true, MoveTemp(Code), MoveTemp(Click));
		UE_LOG(LogAsdaExchange, Log, TEXT("аукцион: сетка %s — ячеек привязано %d из %d"),
			Tag, Bound, Icons.Num());
		return G;
	};
	// ★ Прокрутка сетки выставления сдвигает окно на ЦЕЛЫЕ РЯДЫ по 6 ячеек (полоса 13125).
	//   Размер ячейки — 32×32 из данных (`EXCHANGE_SELL0_B` `Size = "32 32"`, шаг 33).
	GridSell = MakeGridFromIcons(TEXT("EXCH_SELL"), SellIcons, 32.f, 32.f,
		[this](int32 C) { const int32 I = ScrollRowSell * kSellCols + C;
			return SellCells.IsValidIndex(I) ? SellCellCode(I) : 0; },
		[this](int32 C) { OnSellCellClicked(ScrollRowSell * kSellCols + C); });
	// ★ У строки лота зона ТОЛЬКО по иконке 32×32: правее в той же строке живут поле цены
	//   (`EXCH_EDIT_PRICE0N`, L61…214) и комбобокс срока — широкая зона перекрыла бы их.
	GridReg = MakeGridFromIcons(TEXT("EXCH_REG"), RegIcons, 32.f, 32.f,
		[this](int32 R) { return RegRows.IsValidIndex(R) ? RegRows[R].Item.ItemCode : 0; },
		[this](int32 R) { if (RegRows.IsValidIndex(R)) { RegRows[R].bChecked = !RegRows[R].bChecked; RefreshRegRows(); } });
	// ★ У списков строка панели ≠ запись данных: сервер шлёт до 10 «моих лотов» на 8 строк,
	//   поэтому индекс записи = верхний ряд прокрутки + номер строки.
	// ★ У СПИСКОВ зона — ВСЯ СТРОКА, как в эталоне: `uiAvatarMapItemToCostumeSlot@0xD423A0`
	//   (`ui_core.c:50409-50455`) отображает ШЕСТЬ IDNum строки (иконка/имя/цена/продавец/…) в её
	//   номер, то есть клик по имени или цене тоже выбирает лот. Ширина = ширина строки минус
	//   отступ иконки (в данных иконка стоит на L21, галочка — на L0 и остаётся своей):
	//   «мои лоты» 558 − 21 = 537, выдача 743 − 21 = 722. Высота 32 при шаге строк 37.
	GridMy = MakeGridFromIcons(TEXT("EXCH_MY"), MyIcons, 537.f, 32.f,
		[this](int32 R) { const int32 I = ScrollRowMy + R; return MyLots.IsValidIndex(I) ? MyLots[I].ItemCode : 0; },
		[this](int32 R) { const int32 I = ScrollRowMy + R;
			if (MyLots.IsValidIndex(I)) { MyLots[I].bChecked = !MyLots[I].bChecked; RefreshMyLots(); } });
	GridLots = MakeGridFromIcons(TEXT("EXCH_LOT"), LotIcons, 722.f, 32.f,
		[this](int32 R) { const int32 I = ScrollRowLot + R; return Listings.IsValidIndex(I) ? Listings[I].ItemCode : 0; },
		[this](int32 R) { const int32 I = ScrollRowLot + R;
			if (Listings.IsValidIndex(I)) { Listings[I].bChecked = !Listings[I].bChecked; RefreshListings(); } });

	// ★★★ P2: 8×5 гнёзд самоцветов строк выдачи — ТА ЖЕ общая точка `UAsdaIconCellGrid`
	//   (код самоцвета → иконка через `UAsdaItemTableSubsystem::GetItemIcon`, ветка `IsSowelCode`).
	//   Клик по гнезду в оригинале ВЫБИРАЕТ СТРОКУ, а не гнездо: `uiAvatarMapItemToCostumeSlot@0xD423A0`
	//   (`ui_core.c:50409-50455`) отображает шесть IDNum строки в её номер 0…7.
	{
		TArray<FName> SowelNames;
		SowelNames.Reserve(kLotRows * kLotSowels);
		int32 Bound = 0;
		for (int32 R = 0; R < kLotRows; ++R)
		{
			for (int32 K = 0; K < kLotSowels; ++K)
			{
				UImage* Img = LotSowelIcons[R * kLotSowels + K];
				SowelNames.Add(Img ? Img->GetFName() : NAME_None);
				if (Img) { ++Bound; }
			}
		}
		// ★★★ СЕТКА ГНЁЗД БОЛЬШЕ НЕ СТРОИТСЯ (2026-08-30). Строка выдачи в оригинале несёт РОВНО
		//   пять элементов (иконка / имя / цена / продавец / галочка — `CSatisfyWindow_RefreshItemSlots`,
		//   `ui_core.c:52512+`, поля `11*slot + 201…205`), гнёзд самоцветов среди них НЕТ.
		//   Подробное доказательство и почему в них ехали «сапоги» — у списка `AlwaysHidden`.
		//   Оставляем ветку целиком, но выключенной: узлы гасятся, клик-зоны не создаются.
		(void)Bound;
		if (false)
		{
		GridSowel = NewObject<UAsdaIconCellGrid>(this);
		GridSowel->SetOverlayTag(TEXT("EXCH_SOWEL"));   // ★ уникальное имя клик-зон
		GridSowel->SetCellSizePx(16.f);                      // Size = "16 16" из данных
		GridSowel->bClearEmptyCells = true;                  // страничный список ⇒ пустое гнездо гасим
		GridSowel->SetupNamed(Widget, SowelNames, /*bMale*/ true,
			[this](int32 Cell)
			{
				const int32 I = ScrollRowLot + Cell / kLotSowels;
				const int32 K = Cell % kLotSowels;
				if (!Listings.IsValidIndex(I) || !Listings[I].EnchantOpts.IsValidIndex(K)) { return 0; }
				return Listings[I].EnchantOpts[K];
			},
			[this](int32 Cell)
			{
				const int32 I = ScrollRowLot + Cell / kLotSowels;
				if (Listings.IsValidIndex(I)) { Listings[I].bChecked = !Listings[I].bChecked; RefreshListings(); }
			});
		UE_LOG(LogAsdaExchange, Log, TEXT("аукцион: гнёзд самоцветов привязано %d из %d"),
			Bound, kLotRows * kLotSowels);
		}
		UE_LOG(LogAsdaExchange, Log,
			TEXT("аукцион: гнёзда самоцветов строк выдачи ВЫКЛЮЧЕНЫ — в оригинале строка несёт "
			     "иконку/имя/цену/продавца/галочку и больше ничего"));
	}

	// ★★★ P3: три полосы прокрутки — общая точка, своей копии нет.
	BuildScrollBars();

	// ── Кнопки ──
	auto Bind = [this](UButton* Btn, int32 Index, TFunction<void(int32)> Fn)
	{
		if (!Btn) { return; }
		AsdaUi::BindButton(this, Btn, Index, MoveTemp(Fn), Relays);   // ★ U20: реле клика — общая точка
	};
	// 13004 — крестик. Эталон: `objClassifyType123` → `itemMediationCancelByType(тип)`.
	Bind(BtnTitleDel, 0, [this](int32) { CloseAll(); });
	// 13058 — убрать отмеченные строки лота.
	Bind(BtnSelDel,   0, [this](int32) { DropCheckedRegRows(); });
	// 13061 — выставить (9901).
	Bind(BtnRegOk,    0, [this](int32) { SendRegister(); });
	// 13060 — закрыть панель выставления (тип 1).
	Bind(BtnRegCancel,0, [this](int32) { CancelByType(1); });
	// 13251 — снять отмеченные лоты (9909).
	Bind(BtnMyDel,    0, [this](int32) { SendTakeCheckedLots(); });
	// 13250 — закрыть «мои лоты» (тип 2).
	Bind(BtnMyClose,  0, [this](int32) { CancelByType(2); });
	// 13423 — купить (9905).
	Bind(BtnBuy,      0, [this](int32) { SendBuyChecked(); });
	// 13422 — закрыть панель поиска (тип 3).
	Bind(BtnExchCancel,0,[this](int32) { CancelByType(3); });
	// 13424 — искать заново со сбросом страницы.
	Bind(BtnRescan,   0, [this](int32) { PageCur = 0; PageLast = 0; SendSearch(); });
	// Пагинация — порт `uiTabNavigate` (`ui_core.c:53273-53320`).
	Bind(BtnPagePrev,  0, [this](int32) { PageNavigate(0, 0); });
	Bind(BtnPageNext,  0, [this](int32) { PageNavigate(1, 0); });
	Bind(BtnPageFirst, 0, [this](int32) { PageNavigate(0, 1); });
	Bind(BtnPageLast,  0, [this](int32) { PageNavigate(0, 2); });

	// ★★★ P1: КАТЕГОРИИ ПОИСКА. Клик по пункту набора = `uiItemDlgOnSelChange@0xD59B20`:
	//   режим 4 пишет `searchCritSel2`, режим 5 — `searchCritSel1`, затем перезаполняет три списка.
	for (int32 I = 0; I < CatBtns2.Num(); ++I)
	{
		Bind(CatBtns2[I], I, [this](int32 Idx) { OnCategoryPicked(4, Idx); });
	}
	for (int32 I = 0; I < CatBtns1.Num(); ++I)
	{
		Bind(CatBtns1[I], I, [this](int32 Idx) { OnCategoryPicked(5, Idx); });
	}
	// ★ Переключатель категорий «моих лотов» 13260: смена категории повторно шлёт 9907
	//   (`itemWnd_dispatchUiMessage@0xD41E00`, `item_inventory.c:46585-46588`).
	for (int32 I = 0; I < MyCatBtns.Num(); ++I)
	{
		Bind(MyCatBtns[I], I, [this](int32 Idx)
		{ MyCategory = Idx; RefreshCategoryCaptions(); RequestMyLots(); });
	}

	// Галочки строк — через тот же мостик, различая строки по индексу.
	auto BindCheck = [this](UCheckBox* Cb, int32 Index, TFunction<void(int32, bool)> Fn)
	{
		if (!Cb) { return; }
		UAsdaButtonRelay* R = NewObject<UAsdaButtonRelay>(this);
		R->Index = Index;
		R->CheckCallback = MoveTemp(Fn);
		Cb->OnCheckStateChanged.AddDynamic(R, &UAsdaButtonRelay::OnCheckChanged);
		AsdaUi::AnchorRelay(Relays, R, Cb);   // ★ 31.10.2026: якорь чистит мёртвые/перепривязанные (`asda.UI.RelayAnchorPrune`)
	};
	for (int32 R = 0; R < kRegRows; ++R)
	{
		BindCheck(RegChecks[R], R, [this](int32 I, bool bOn)
		{ if (RegRows.IsValidIndex(I)) { RegRows[I].bChecked = bOn; if (!bOn) { DropAllCheck(1); } } });
	}
	for (int32 R = 0; R < kMyRows; ++R)
	{
		BindCheck(MyChecks[R], R, [this](int32 I, bool bOn)
		{
			// ★ Пачка 20: строка «моих лотов» = ВЕРХНИЙ РЯД ПРОКРУТКИ + номер строки (записей до 10 на 8 строк) — прежде
			//   галочка строки R писала запись R и после прокрутки отмечала не тот лот.
			const int32 Idx = ScrollRowMy + I;
			if (MyLots.IsValidIndex(Idx)) { MyLots[Idx].bChecked = bOn; if (!bOn) { DropAllCheck(2); } }
		});
	}
	for (int32 R = 0; R < kLotRows; ++R)
	{
		BindCheck(LotChecks[R], R, [this](int32 I, bool bOn)
		{
			const int32 Idx = ScrollRowLot + I;   // ★ пачка 20: та же поправка на прокрутку, что у «моих лотов»
			if (Listings.IsValidIndex(Idx)) { Listings[Idx].bChecked = bOn; if (!bOn) { DropAllCheck(3); } RefreshListings(); }
		});
	}
	// ★★★ ПАЧКА 20, жалоба 5: три большие галочки «выбрать всё» (порт `uiItemSlots_OnHover` 13478, `uiHandleBoosterMsg` 13479,
	//   `uiSowelSlotCommandHandler` 13482). Ключ `asda.Exchange.SelectAll` 0 — не привязаны, как до 09.11.
	if (CVarExchangeSelectAll.GetValueOnGameThread() != 0)
	{
		BindCheck(ChkRegAll, 1, [this](int32 P, bool bOn) { OnAllCheck(P, bOn); });
		BindCheck(ChkMyAll,  2, [this](int32 P, bool bOn) { OnAllCheck(P, bOn); });
		BindCheck(ChkLotAll, 3, [this](int32 P, bool bOn) { OnAllCheck(P, bOn); });
	}

	// ★★★ Статические подписи ТРЁХ панелей — из таблицы строк (порт `invForgeDialogInit` и её
	//   близнецов, разбор — в шапке `InitStaticCaptions`). Раньше их держал ассет, и текст был
	//   переводом корейской заготовки `.JUI`, а не строкой игры.
	InitStaticCaptions();
	// ★ Подписи вкладок ставим сразу: тексты берутся из локализации, а не из ассета (там пусто).
	RefreshCategoryCaptions();

	bControlsReady = true;
}

// ★★★ ПОДПИСИ ВКЛАДОК И ПОДСВЕТКА АКТИВНОЙ.
//
//   ГДЕ ВЗЯТЫ ТЕКСТЫ (жалоба владельца 2026-08-27 «текста нету»). `uiOptionListboxInit@0xD544A0`
//   (`ui_core.c:52140-52170`) — инициализация ЭТОГО окна (`self[179]` = 13400, `self[180]` = 13408,
//   `self[181]` = 13409, `self[182…184]` = 13410…13412, `self[299…301]` = 13487…13489):
//     • набор **13408** (`EXCHANGER_RADIOBTN1`, 8 кнопок, РЕЖИМ 5) — нотисы
//       **12520, 12521, 12522, 12523, 12524, 12525, 12526, 12527** (цикл `for (i = 0; i < 8; ++i)`
//       по массиву `noticeIds`; ★ девятый элемент массива — 12775 — циклом НЕ БЕРЁТСЯ, мёртвый);
//     • набор **13409** (`EXCHANGER_RADIOBTN2`, 7 кнопок, РЕЖИМ 4) — нотисы **12528…12534**
//       (`noticeId = 12528; while (i < 7) { … ++noticeId; }`);
//     • переключатель «моих лотов» **13260** — нотисы **98200 / 98201**
//       (`CItemMediation_BuildUI`, `shop_trade.c:13924-13931`).
//   Ни одной выдуманной подписи: только `AsdaUi::LocRaw`.
//
//   ЦВЕТ. Подсветку ставит ОБЩАЯ ТОЧКА `AsdaUi::ApplyTabCaptionState` — перегрузка С КНОПКАМИ,
//   которая берёт цвета из КАДРОВ СКИНА, положенных в `FButtonStyle` шагом
//   `Tools/asda_ui_tabbar_captions.py` (`JuiSkin.ini`: кадр 0 покой `FF808080`, кадр 1 наведение
//   `FFAAAAAA`, кадр 2 ВЫБРАНО `FFFFFFFF`). Своей палитры здесь нет.
// ═══════════════════════════════════════════════════════════════════════════════════════════════
//  ★★★ СТАТИЧЕСКИЕ ПОДПИСИ ОКНА — ИЗ ТАБЛИЦЫ СТРОК, А НЕ ИЗ `.JUI` (найдено 2026-08-28)
//
//  Все заголовки колонок и подписи кнопок трёх панелей оригинал ставит В РАНТАЙМЕ по id
//  локализации, а корейский `EXS TextContent` в `.JUI` — только заготовка дизайнера:
//    • панель ВЫСТАВЛЕНИЯ — `invForgeDialogInit@0xD450B0` (`item_inventory.c:47295-47353`) и её
//      близнец `consignRegWnd_InitControls@0xD448A0` (`ui_core.c:50881-50931`);
//    • панель «МОИ ЛОТЫ» — `shop_trade.c:12809-12841`;
//    • панель ПОИСКА — `uiInitDialogControls` (`ui_core.c:52028-52056`).
//  Соответствие «поле объекта → IDNum» снято по порядку `CWindowMgr_FindControlByID` в
//  `CItemMediation_BuildUI@0xD40100` (`shop_trade.c:13730-13790`): индексы 1…12 = 13000, 13001,
//  13002, 13003, 13004, 13005, 13007, 13008, 13009, 13012, 13013, 13014; далее 5 строк по 9 полей
//  (13…57), 58 = 13055, 59 = 13056, 60 = 13058, 61 = 13061, 62 = 13060, 63…122 = 60 ячеек сетки
//  13064…13123, 123 = 13125, 124 = 13126. Якорь проверки — `pForgeDlg[slotIdx + 63]` в цикле по
//  ШЕСТИДЕСЯТИ ячейкам: он однозначно привязывает всю нумерацию.
//
//  ★ ЧТО ЭТО МЕНЯЕТ ПО СУЩЕСТВУ (жалоба владельца п.5): оригинал зовёт третью колонку
//  **12504 «Налог»**, итог — **12505 «Общий налог на продажу»**, а кнопку 13058 —
//  **12506 «Отменить продажу»**. То есть владелец был прав, и это НЕ переименование «по вкусу»:
//  наш прежний текст пришёл из словаря перевода корейской заготовки, а не из таблицы игры.
//  ⚠ При этом СУММА по-прежнему считается как 1 % (`sub_521920`) — «налог» здесь имя из таблицы.
//
//  ★ ДВЕ ПОДПИСИ ОРИГИНАЛ ГАСИТ, а не заполняет:
//    `CControl_SetText(pForgeDlg[9*slot + 21], "")` — это 13300…13304 `EXCH_ITEM_Msg0N`, то есть
//    строка «При сделке взимается комиссия []% …» НИКОГДА не показывается (отсюда и `[]` на
//    скриншоте владельца — это литерал заготовки, а не маркер подстановки);
//    `CControl_SetVisibleState(pForgeDlg[124], 0)` — 13126 `Exchange_RemoveinKorea`.
void UAsdaExchangeSubsystem::InitStaticCaptions()
{
	if (!Widget) { return; }
	UGameInstance* GI = GetGameInstance();

	auto SetTxt = [this](const TCHAR* Name, const FString& Text)
	{
		if (UTextBlock* T = Cast<UTextBlock>(Widget->GetWidgetFromName(FName(Name))))
		{
			T->SetText(FText::FromString(Text));
			return true;
		}
		return false;
	};
	auto SetLoc = [&](const TCHAR* Name, int32 LocId)
	{
		if (!SetTxt(Name, AsdaUi::LocRaw(GI, LocId, true)))
		{
			UE_LOG(LogAsdaExchange, Warning, TEXT("аукцион: подпись %s не найдена (loc %d)"), Name, LocId);
		}
	};

	// ── панель ВЫСТАВЛЕНИЯ ──
	SetLoc(TEXT("EXCH_SUBTITLE"),        12500);   // «- Предмет на продажу»
	SetLoc(TEXT("EXCH_CASHTEXT"),        12501);   // «Золото:»
	SetLoc(TEXT("EXCH_PRICETEXT"),       12502);   // «Цена продажи»
	SetLoc(TEXT("EXCH_TIMETEXT"),        12503);   // «Время »
	SetLoc(TEXT("EXCH_FEETEXT"),         12504);   // ★ «Налог»
	SetLoc(TEXT("EXCH_TOTALFEETEXT"),    12505);   // ★ «Общий налог на продажу»
	SetLoc(TEXT("EXCH_SELDEL_Text"),     12506);   // ★ «Отменить продажу»
	SetLoc(TEXT("EXCH_OK_Text"),         12507);   // «OK»
	SetLoc(TEXT("EXCH_CANCEL_Text"),      2289);   // «Отмена»
	// ★ Пять подписей-заготовок оригинал ЗАТИРАЕТ (см. шапку) — вместе с литералом «[]».
	for (int32 R = 0; R < kRegRows; ++R)
	{
		SetTxt(*FString::Printf(TEXT("EXCH_ITEM_Msg%02d"), R + 1), FString());
	}

	// ── панель «МОИ ЛОТЫ» ──
	SetLoc(TEXT("EXCH_REGTEXT"),         12511);   // «Стоимость»
	SetLoc(TEXT("EXCHMY_NAME"),          12508);   // «Предмет»
	SetLoc(TEXT("EXCHMY_PRICE"),         12509);   // «Цена продажи»
	SetLoc(TEXT("EXCHMY_REMAINTIME"),    12510);   // «Осталось времени»
	SetLoc(TEXT("EXCHMY_CLOSE_Text"),     5035);   // «Закрыть»
	SetLoc(TEXT("EXCHMY_DEL_Text"),      12512);   // «Снять»
	SetLoc(TEXT("EXCHMY_AMOUNTTXT"),     12722);   // «Предметов»

	// ── панель ПОИСКА И ПОКУПКИ ──
	SetLoc(TEXT("EXCHANGER_CANCEL_Text"), 2289);   // «Отмена»
	SetLoc(TEXT("EXCHANGER_BUY_Text"),   12519);   // «OK»
	SetLoc(TEXT("EXCHANGER_RESCAN_Text"),12513);   // «Поиск»
	SetLoc(TEXT("EXCHANGER_NAME"),       12514);   // «Предмет»
	SetLoc(TEXT("EXCHANGER_PRICE"),      12515);   // «Цена»
	SetLoc(TEXT("EXCHANGER_SELLER"),     12516);   // «Продавец»
	SetLoc(TEXT("EXCHANGER_BUYPRICE1"),  12517);   // «Цена:»
	SetLoc(TEXT("EXCHANGER_GOLD"),       12501);   // «Золото:»

	// ★ 13126 — оригинал прячет (`CControl_SetVisibleState(pForgeDlg[124], 0)`).
	if (UWidget* W = Widget->GetWidgetFromName(TEXT("Exchange_RemoveinKorea")))
	{
		W->SetVisibility(ESlateVisibility::Collapsed);
	}
}

void UAsdaExchangeSubsystem::RefreshCategoryCaptions()
{
	UGameInstance* GI = GetGameInstance();

	auto FillSet = [this, GI](TArray<TObjectPtr<UTextBlock>>& Texts,
		const TArray<TObjectPtr<UButton>>& Btns, int32 FirstLoc, int32 Active,
		TArray<FLinearColor>& Idle, const TArray<TObjectPtr<UImage>>& Planes,
		TArray<FSlateBrush>& PlaneIdle, TArray<FButtonStyle>& BtnIdle)
	{
		for (int32 I = 0; I < Texts.Num(); ++I)
		{
			if (!Texts[I]) { continue; }
			// Подпись есть только у тех вкладок, у которых есть кнопка (число кнопок = число планов).
			const FString Txt = Btns.IsValidIndex(I) ? AsdaUi::LocRaw(GI, FirstLoc + I, true) : FString();
			Texts[I]->SetText(FText::FromString(Txt));
		}
		// ★★★ ОДИН ПРОХОД: плашка кадра 2 + гашение кисти активной кнопки + цвет подписи.
		//   Раньше это были ДВА вызова, и между ними кнопка успевала перерисовать вкладку своей
		//   кистью — отсюда «сначала чёрная полоса, потом обводка» (разбор — в объявлении
		//   `AsdaUi::ApplyTabState`).
		AsdaUi::ApplyTabState(Planes, Texts, Btns, Active, PlaneIdle, BtnIdle, Idle);
	};

	FillSet(CatTexts1, CatBtns1, 12520, SearchCritSel1, CatIdle1,  CatPlanes1,  CatPlaneIdle1,  CatBtnIdle1);   // 13408, режим 5
	FillSet(CatTexts2, CatBtns2, 12528, SearchCritSel2, CatIdle2,  CatPlanes2,  CatPlaneIdle2,  CatBtnIdle2);   // 13409, режим 4
	FillSet(MyCatTexts, MyCatBtns, 98200, MyCategory,   MyCatIdle, MyCatPlanes, MyCatPlaneIdle, MyCatBtnIdle);  // 13260
}
