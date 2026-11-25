// Copyright (c) 2026 @sitxovski (Discord)
// AsdaHighRefineSubsystem_Window.cpp — ★ R3-c 12.11.2026 (разрез AsdaHighRefineSubsystem.cpp, дословный перенос): рецепты, открыть/закрыть, деньги, панели, страницы, захват 6500, полоса прокрутки списка.
//   Общее для частей — в AsdaHighRefineSubsystem_Private.h; шапка и реверс — в AsdaHighRefineSubsystem.cpp.
#include "UI/AsdaHighRefineSubsystem.h"

#include "Net/NetOpcodes.h"          // ★ волна 13: опкод 6500 для перехвата самотеста (`DebugCapture6500`)

#include "AsdaHighRefineSubsystem_Private.h"   // ★ R3-c 12.11.2026: общее для частей AsdaHighRefineSubsystem*.cpp

// ★ Д7-6. «Отмена» при ожидании ответа снимает «идёт» и «занято», и «ОК» снова доступен — второй 6500 уходит
//   до ответа на первый. Сервер флаг «запрос к БД в полёте» (`gap_145[15]`) для 6500 ставит, но НЕ проверяет
//   (`wave6_server.md`) — две переплавки подряд списывают материалы дважды. Замок как у заточки (N-2).
static ASDA_DEV_CVAR(int32, CVarSingleInFlight,
	TEXT("asda.HighRefine.SingleInFlight"), 1,
	TEXT("Дефект оригинала Д7-6: 1 = пока 6500 без ответа 6501, новый не отправлять (снимают ответ, разрыв, "
	     "таймаут); 0 = как оригинал."),
	ECVF_Default);

namespace {
	// Цвета эталона (`CControl_SetField923`), ARGB.
	constexpr uint32 kColEnough = 0xFFFFFFFFu;   // −1 — материала хватает
	constexpr uint32 kColShort  = 0xFFFF0000u;   // −65536 — не хватает

	// ARGB → линейный цвет UMG (закон ui-srgb-gamma-rule: данные в sRGB).
	FSlateColor Argb(uint32 C)
	{
		return FSlateColor(AsdaUi::SrgbHexToLinear(FString::Printf(TEXT("%08X"), C)));
	}
}

void UAsdaHighRefineSubsystem::EnsureInit()
{
	// `CHighRefineUI_Init@0xCF3870`: +0 = 1, страница/выбор/ключ/флаги в ноль или −1, таблица рецептов.
	//   ★ [96] у эталона = время `Init`, а `Init` он зовёт при ЗАГРУЗКЕ интерфейсов — задолго до открытия
	//   окна. Порт инициализирует лениво при первом открытии, поэтому «недавняя активность» от `Init`
	//   запретила бы закрыть окно первую секунду — такого у оригинала не бывает. Отсчёт не ставим.
	if (bInited) { return; }
	bInited = true;
	bBusy = bDone = bInProgress = bMatsOk = false;
	Page = 0; SelCell = -1; RecipeKey = -1;
	LastReplyTime = -1.0;
	LoadRecipes();
}

void UAsdaHighRefineSubsystem::LoadRecipes()
{
	// `CHighRefine_LoadRefineHighBin@0xCF5FE0`: `./Data/NPC/RefineHigh.BIN`, 20 б заголовка + N × 68 б
	//   в список `this+84` в ПОРЯДКЕ ФАЙЛА. В порту — `DT_refinehigh` (994 записи, починен 25.09,
	//   `Reports/refinehigh_fix.md`); порядок файла = поле `Idx` (0…993 без дыр). Размер берём из
	//   таблицы, а не константой (там же, §5).
	Recipes.Reset();
	const UAsdaItemTableSubsystem* IT = GetGameInstance() ? GetGameInstance()->GetSubsystem<UAsdaItemTableSubsystem>() : nullptr;
	const UDataTable* DT = IT ? IT->GetDataTable(FName(TEXT("refinehigh"))) : nullptr;
	if (!DT)
	{
		UE_LOG(LogAsdaHighRefine, Error, TEXT("[переплавка] DT_refinehigh недоступна — окно 78200 без рецептов"));
		return;
	}
	TArray<FAsdaRefinehighRow*> Rows;
	DT->GetAllRows<FAsdaRefinehighRow>(TEXT("HighRefine"), Rows);
	for (const FAsdaRefinehighRow* R : Rows) { if (R) { Recipes.Add(*R); } }
	Recipes.Sort([](const FAsdaRefinehighRow& A, const FAsdaRefinehighRow& B) { return A.Idx < B.Idx; });
	int32 Holes = 0;
	for (int32 I = 0; I < Recipes.Num(); ++I) { if (Recipes[I].Idx != I) { ++Holes; } }
	UE_LOG(LogAsdaHighRefine, Log, TEXT("[переплавка] рецептов %d (позиция ≠ Idx: %d)"), Recipes.Num(), Holes);
}

void UAsdaHighRefineSubsystem::OpenWindow()
{
	// `craftWnd_Open@0xCF3B00`: видимое окно не трогает; иначе Init (один раз), сброс денег и флагов,
	//   сброс выбора, окно наверх, показать, построить сетку.
	if (bOpen) { return; }
	if (!EnsureWidget()) { return; }
	EnsureInit();
	UAsdaWindowSubsystem* WM = GetGameInstance() ? GetGameInstance()->GetSubsystem<UAsdaWindowSubsystem>() : nullptr;
	if (!WM) { return; }
	ResetMoney();
	ResetTradeWindow();
	bOpen = true;
	Mouse.Reset();
	TipKind = TipIdx = -1;
	WM->SetWindowVisible(kWindowId, true);
	WM->BringWindowToFront(kWindowId);   // `CClass_MapInsert(g_pWindowMgr, m_pUI_Main)`
	RefreshBagPanel();
	JournalAdd(TEXT("open"));
	UE_LOG(LogAsdaHighRefine, Log, TEXT("[переплавка] окно 78200 открыто: в сетке %d предметов, страница %d"), List.Num(), Page);
}

void UAsdaHighRefineSubsystem::CloseWindow()
{
	// `craftWnd_Close@0xCF3BA0`: `if (uiCheckRecentActivity() != 1 && видимо)` — снять тултип, спрятать,
	//   `CloseHide` эффектов 78272/78273, «занято» = 0.
	//   ★ Волна 9: спрашиваем ОБЩУЮ точку (все условия эталонной функции, в т.ч. чужих окон), а не свою часть.
	if (AsdaUi::CheckRecentActivity(GetGameInstance()))
	{
		UE_LOG(LogAsdaHighRefine, Log, TEXT("[переплавка] закрытие отклонено: недавняя активность (идёт=%d, с ответа %.0f мс)"),
			bInProgress ? 1 : 0, LastReplyTime >= 0.0 ? (FPlatformTime::Seconds() - LastReplyTime) * 1000.0 : -1.0);
		return;
	}
	if (!bOpen) { return; }
	TickTooltip(-1, -1);
	bOpen = false;
	if (UAsdaWindowSubsystem* WM = GetGameInstance() ? GetGameInstance()->GetSubsystem<UAsdaWindowSubsystem>() : nullptr)
	{
		WM->SetWindowVisible(kWindowId, false);
	}
	ShowPlane(FxE1, false);
	ShowPlane(FxE2, false);
	ResetMoneyTexture();
	Mouse.Reset();
	JournalAdd(TEXT("close"));
}

bool UAsdaHighRefineSubsystem::IsRecentActivity() const
{
	// Порт СВОЕЙ части `uiCheckRecentActivity@0xD326A0` (ui_core.c:49880): `get_o4(g_pTooltipWnd) == 1`
	//   (+4 «идёт») и `now − get_o60(g_pTooltipWnd) < 1000` ([96] — момент последнего ответа 6501).
	//   Остальные условия функции — чужих окон: лавки заточки 17000 (`g_pRefineShop` +9 и [377]) и окна
	//   разборки 32000 (`g_pRecvUiHandler3` [223] и его поле [89] — «глобал» `g_bRefineInProgress`, миснейм).
	//   Все условия собраны в общей точке `AsdaUi::CheckRecentActivity` — её спрашивают места вызова.
	if (bInProgress) { return true; }
	return LastReplyTime >= 0.0 && (FPlatformTime::Seconds() - LastReplyTime) * 1000.0 < kRecentMs;
}

// ═════════════════════════════ ПОРТ ФУНКЦИЙ ОКНА ══════════════════════════════════════════════
void UAsdaHighRefineSubsystem::ResetMoney()
{
	// `craftWndResetMoney@0xCF3600`: текст золота, +4 = 0, +3 = 0, `CloseHide` полосы, значение 0, «занято» = 0.
	RefreshMoneyText();
	bInProgress = false;
	bDone = false;
	ProgressStart = -1.0;
	SetProgressVisible(false);
	SetProgressValue(0.f);
	ResetMoneyTexture();
}

void UAsdaHighRefineSubsystem::ResetMoneyTexture()
{
	// `craftWndResetMoneyTexture@0xCF3F10`: +2 = 0; `CChat_SetIconState(ОК, 1)`, `(X, 1)` — байт +293
	//   «принимает ввод» (миснейм: к чату отношения нет).
	bBusy = false;
	if (BtnOk) { BtnOk->SetIsEnabled(true); }
	if (BtnX)  { BtnX->SetIsEnabled(true); }
}

void UAsdaHighRefineSubsystem::SetBusy()
{
	// `sub_CF3EC0` (он же `sub_92F657`): +2 = 1; ОК и X перестают принимать ввод. Кадр кнопки эталон не
	//   меняет — у стиля JUI-кнопки кадр «выключено» = кадр 0 (`AsdaUmgBuilder`), вид прежний.
	bBusy = true;
	if (BtnOk) { BtnOk->SetIsEnabled(false); }
	if (BtnX)  { BtnX->SetIsEnabled(false); }
}

void UAsdaHighRefineSubsystem::ResetTradeWindow()
{
	// `uiResetTradeWindow@0xCF5130`: [94] = −1, [95] = 0, иконка результата пустая, гайд 15301, три
	//   материала пустые (цвет «хватает»), цена и опыт «0».
	RecipeKey = -1;
	bMatsOk = false;
	ResultCode = 0;
	if (ResultIconGrid) { ResultIconGrid->Refresh(); }
	if (TxtGuide) { TxtGuide->SetText(FText::FromString(Loc(AsdaHighRefineLoc::kGuideSelect))); }
	for (int32 M = 0; M < kMats; ++M)
	{
		MatCodes[M] = 0;
		if (MatNames.IsValidIndex(M) && MatNames[M]) { MatNames[M]->SetText(FText::GetEmpty()); }
		if (MatCounts.IsValidIndex(M) && MatCounts[M])
		{
			MatCounts[M]->SetText(FText::GetEmpty());
			MatCounts[M]->SetColorAndOpacity(Argb(kColEnough));
		}
	}
	if (MatIconGrid) { MatIconGrid->Refresh(); }
	if (TxtUseGold) { TxtUseGold->SetText(FText::FromString(TEXT("0"))); }
	if (TxtExp)     { TxtExp->SetText(FText::FromString(TEXT("0"))); }
}

void UAsdaHighRefineSubsystem::RefreshBagPanel()
{
	// `uiInventoryBagPanelRefresh@0xCF4400`: только при видимом окне. Список = сумка 0…29, у которой есть
	//   запись `item_list`, категория 3 (`itemGetCategoryById`: itemType 0…4, 8, 9, 13) и строка иконки.
	if (!bOpen) { return; }
	List.Reset();
	if (CheckFrame) { CheckFrame->SetVisibility(ESlateVisibility::Collapsed); }
	SelCell = -1;
	for (UTextBlock* Tb : GridLabels) { if (Tb) { Tb->SetText(FText::GetEmpty()); } }
	const UAsdaInventorySubsystem* Inv = GetGameInstance() ? GetGameInstance()->GetSubsystem<UAsdaInventorySubsystem>() : nullptr;
	const UAsdaItemTableSubsystem* IT = GetGameInstance() ? GetGameInstance()->GetSubsystem<UAsdaItemTableSubsystem>() : nullptr;
	if (Inv && IT)
	{
		for (int32 J = 0; J < kShellCells; ++J)
		{
			const FAsdaItemInstance* It = Inv->GetCell(1, J);
			if (!It || It->IsEmpty()) { continue; }
			// ★ Волна 9: категория — общей точкой `GetItemCategory` (порт `itemGetCategoryById`), без своей копии.
			if (IT->GetItemCategory(It->ItemCode) != 3) { continue; }
			const FAsdaItemRecord* R = IT->GetItem(It->ItemCode);
			if (!R) { continue; }
			// `CItemManager_GetImgOptionPtr(imgIndex, вариант)` — строка иконки; пустая ⇒ позиция не попадает.
			const FAsdaImgRecord* Img = IT->GetImgRecord(R->ImgIndex);
			if (!Img || Img->IconAtlasMale.IsEmpty()) { continue; }
			FAsdaHighRefineEntry E;
			E.CellIndex = J;
			E.IconCode = It->ItemCode;
			List.Add(E);
		}
	}
	if (GridIconGrid) { GridIconGrid->Refresh(); }
	LayoutListScroll();   // ★ волна 12: диапазон полосы 78226 = число страниц нового списка
}

void UAsdaHighRefineSubsystem::RefreshRecipeInfo(int32 SelectedCell)
{
	// `craftWnd_RefreshRecipeInfo@0xCF48D0` (item_enhance.c:1592-1757).
	const int32 Slot = (SelectedCell == -1) ? SelCell : SelectedCell;
	const int32 Abs = Slot + kGridCells * Page;
	if ((uint32)Abs >= (uint32)List.Num())   // эталон сравнивает БЕЗЗНАКОВО
	{
		if (CheckFrame) { CheckFrame->SetVisibility(ESlateVisibility::Collapsed); }
		SelCell = -1;
		ResetTradeWindow();
		return;
	}
	// Рамка выбора — на ячейку `Slot` (`CControl_SetField74_75(this[59], x+10, y+7)` — сдвиг ячейки от
	//   ячейки 0 внутри её группы; рамка лежит над ячейкой 0 в `.JUI`).
	if (CheckFrame && GridIcons.IsValidIndex(Slot) && GridIcons[Slot] && GridIcons[0])
	{
		if (UCanvasPanelSlot* S = Cast<UCanvasPanelSlot>(CheckFrame->Slot))
		{
			S->SetPosition(CheckBase + (SlotPos(GridIcons[Slot]) - SlotPos(GridIcons[0])));
		}
		CheckFrame->SetVisibility(ESlateVisibility::HitTestInvisible);
		if (UAsdaAnimatedPlane* P = Cast<UAsdaAnimatedPlane>(CheckFrame)) { P->RestartAnim(); }
	}
	ResetTradeWindow();
	const FAsdaItemInstance* Live = LiveOf(List[Abs]);
	const int32 Code = (Live && !Live->IsEmpty()) ? Live->ItemCode : 0;   // `**(vector[Abs] + 260)` — живая запись
	const UAsdaItemTableSubsystem* IT = GetGameInstance() ? GetGameInstance()->GetSubsystem<UAsdaItemTableSubsystem>() : nullptr;
	const FAsdaItemRecord* ResRec = (IT && Code > 0) ? IT->GetItem(Code) : nullptr;
	const UAsdaInventorySubsystem* Inv = GetGameInstance() ? GetGameInstance()->GetSubsystem<UAsdaInventorySubsystem>() : nullptr;
	for (int32 I = 0; I < Recipes.Num() && ResRec; ++I)
	{
		const FAsdaRefinehighRow& R = Recipes[I];
		// Первая запись, у которой +4 == requiredLevel, +12 == itemType, +16 == itemGrade.
		if (R.ResultRequiredLevel != ResRec->RequiredLevel || R.ResultItemType != ResRec->ItemType || R.ResultItemGrade != ResRec->ItemGrade)
		{
			continue;
		}
		const int32 MatCode[kMats] = { R.Mat1Code, R.Mat2Code, R.Mat3Code };
		const int32 MatNeed[kMats] = { R.Mat1Qty, R.Mat2Qty, R.Mat3Qty };
		bMatsOk = true;   // [95] = 1
		for (int32 M = 0; M < kMats; ++M)
		{
			const FAsdaItemRecord* MR = IT->GetItem(MatCode[M]);
			if (!MR) { continue; }   // эталон: `if (ItemRecord)` — материал вне item_list не рисуется и не проверяется
			MatCodes[M] = MatCode[M];
			if (MatNames.IsValidIndex(M) && MatNames[M]) { MatNames[M]->SetText(FText::FromString(IT->GetItemName(MatCode[M]))); }
			const int32 Have = Inv ? Inv->CountItemById(MatCode[M]) : 0;   // `CInventory_CountItemById` — общая точка
			if (MatCounts.IsValidIndex(M) && MatCounts[M])
			{
				MatCounts[M]->SetText(FText::FromString(FString::Printf(TEXT("%d / %d"), Have, MatNeed[M])));
				MatCounts[M]->SetColorAndOpacity(Argb(Have >= MatNeed[M] ? kColEnough : kColShort));
			}
			if (Have < MatNeed[M]) { bMatsOk = false; }
		}
		if (MatIconGrid) { MatIconGrid->Refresh(); }
		ResultCode = Code;                                        // иконка результата = выбранный предмет
		if (ResultIconGrid) { ResultIconGrid->Refresh(); }
		if (TxtUseGold) { TxtUseGold->SetText(FText::FromString(AsdaUi::FormatThousands(R.GoldCost))); }   // +64
		if (TxtGuide) { TxtGuide->SetText(FText::FromString(Loc(AsdaHighRefineLoc::kGuideReady))); }      // 15302
		RecipeKey = R.Idx;                                        // [94] = *запись (поле +0)
		SelCell = Slot;                                           // [93]
		if (TxtExp) { TxtExp->SetText(FText::FromString(AsdaUi::FormatThousands(R.RefineLevel))); }       // +8 «опыт за успех»
		JournalAdd(FString::Printf(TEXT("recipe %d (предмет %d, материалов хватает=%d)"), R.Idx, Code, bMatsOk ? 1 : 0));
		return;
	}
	// Рецепта нет: рамка стоит на ячейке, гайд 15301 (из `uiResetTradeWindow`), [93] НЕ меняется (1:1).
	JournalAdd(FString::Printf(TEXT("recipe нет (предмет %d)"), Code));
}

void UAsdaHighRefineSubsystem::RefreshMoneyText()
{
	// `String_FormatThousands(getField770234(g_pGameClient))` — поле золота клиента.
	const UAsdaInventorySubsystem* Inv = GetGameInstance() ? GetGameInstance()->GetSubsystem<UAsdaInventorySubsystem>() : nullptr;
	if (TxtHaveGold) { TxtHaveGold->SetText(FText::FromString(AsdaUi::FormatThousands(Inv ? Inv->GetGold() : 0))); }
}

// ═════════════════════════════ ДЕЙСТВИЯ ═══════════════════════════════════════════════════════
void UAsdaHighRefineSubsystem::OnGridCellClicked(int32 Cell)
{
	// msg 0xB/0xF → `craftWndRefreshMoneyAndRecipe@0xCF3F60`: при «занято» — ничего; по ячейке сетки —
	//   иконка результата пустая и рецепт; текст золота — на любой клик.
	if (bBusy) { return; }
	if (Cell >= 0 && Cell < kGridCells)
	{
		ResultCode = 0;
		if (ResultIconGrid) { ResultIconGrid->Refresh(); }
		RefreshRecipeInfo(Cell);
	}
	RefreshMoneyText();
}

void UAsdaHighRefineSubsystem::OnOkClicked()
{
	// `craftWnd_OnButtonMsg@0xCF3D00`, ветка 78291: при «занято» — ничего; `g_bRefineInProgress != 1`
	//   (★ миснейм: это поле [89] «идёт разборка» окна 32000, 0x2C38194 = 0x2C38030 + 0x164; ★ волна 2-А 06.10.2026 —
	//   окно портировано, условие читается у `UAsdaDisassemblySubsystem`); материалов хватает ⇒ полоса + звук + «идёт»
	//   + «занято», иначе гайд 15303.
	if (bBusy) { return; }
	if (const UAsdaDisassemblySubsystem* Ds = GetGameInstance() ? GetGameInstance()->GetSubsystem<UAsdaDisassemblySubsystem>() : nullptr)
	{
		if (Ds->IsDisassembling())   // `else if (g_bRefineInProgress != 1 && a2 == 78291)` (item_enhance.c:1504) — молча
		{
			JournalAdd(TEXT("ОК — идёт разборка 32000, молча"));
			return;
		}
	}
	if (ReplyLatch.IsArmed() && CVarSingleInFlight.GetValueOnGameThread() != 0)
	{
		UE_LOG(LogAsdaHighRefine, Log, TEXT("[переплавка] ОК: предыдущий 6500 ещё без ответа — не начинаем (Д7-6)"));
		return;
	}
	if (bMatsOk)
	{
		SetProgressValue(0.f);
		SetProgressVisible(true);
		ProgressStart = FPlatformTime::Seconds();   // `uiProgressBar_AnimStart(this[80], 1000)`
		AttemptStartTime = ProgressStart;           // ★ Д7-7: попытка начата сейчас (сверяется с [96])
		JournalAdd(TEXT("bar start"));
		PlaySfx(TEXT("S_e_refine_process"));
		bInProgress = true;
		SetBusy();
	}
	else if (TxtGuide)
	{
		TxtGuide->SetText(FText::FromString(Loc(AsdaHighRefineLoc::kGuideNoMats)));
		JournalAdd(TEXT("guide 15303"));
	}
}

void UAsdaHighRefineSubsystem::OnCancelClicked()
{
	// `craftWnd_OnButtonMsg`, ветка 78292: не идёт ⇒ `craftWnd_Close`; идёт ⇒ снять «идёт», `CloseHide`
	//   полосы, значение 0, «занято» = 0 (окно остаётся).
	if (!bInProgress)
	{
		CloseWindow();
		return;
	}
	bInProgress = false;
	ProgressStart = -1.0;
	SetProgressVisible(false);
	SetProgressValue(0.f);
	ResetMoneyTexture();
	JournalAdd(TEXT("cancel"));
}

void UAsdaHighRefineSubsystem::OnCloseClicked()
{
	// `craftWnd_OnButtonMsg`, ветка 78299 — только при «свободно».
	if (bBusy) { return; }
	CloseWindow();
}

bool UAsdaHighRefineSubsystem::OnWheel(int32 Notches)
{
	// ★ Д7-3: событие 0x17 `uiTooltipWndProc` — `if (!+2) sub_CF3E80(page)` (страница + сетка), затем
	//   БЕЗУСЛОВНО `uiResetTradeWindow` и снятие тултипа. Колесо вверх — предыдущая страница.
	if (!bOpen || Notches == 0 || CVarWheelPages.GetValueOnGameThread() == 0) { return false; }
	UAsdaWindowSubsystem* WM = GetGameInstance() ? GetGameInstance()->GetSubsystem<UAsdaWindowSubsystem>() : nullptr;
	if (!WM || !WM->IsCursorOverWindow(kWindowId)) { return false; }
	const int32 NewPage = FMath::Clamp(Page + (Notches > 0 ? -1 : 1), 0, GetMaxPage());
	if (NewPage == Page) { return true; }   // у края позиция полосы не меняется — события нет, колесо съедено
	// ★★★ Волна 9 (МЕЛК-2 ревизии волны 8): при «занято» (+2) колесо СЪЕДАЕТСЯ БЕЗ ДЕЙСТВИЙ.
	//   Обоснование по эталону: ветка 0x17 сама охраняет смену страницы условием `if (!+2)` — замысел
	//   «в занятости не листать»; безусловный `uiResetTradeWindow` за ней у оригинала недостижим (полоса
	//   78226 скрыта), а у нас (путь открыт Д7-3) он срывал бы попытку молча: [94] = −1, и на конце полосы
	//   `itemComposeValidateSelection` отказывает без текста. Та же логика, что у общей блокировки
	//   `uiCheckRecentActivity` окон инвентаря: пока идёт переплавка, ввод, меняющий выбор, не принимается.
	if (bBusy)
	{
		JournalAdd(TEXT("page — занято, колесо съедено"));
		return true;
	}
	SetPage(NewPage, TEXT("колесо"));
	return true;
}

int32 UAsdaHighRefineSubsystem::GetMaxPage() const
{
	// Последняя страница, на которой есть предмет (14 ячеек на страницу). У эталона диапазон полосы
	//   ЖЁСТКО 2 страницы (`uiControlSetScrollRange(78226, 1, 2)`), а сумка — 30 ячеек: 29-й и 30-й
	//   предмет снаряжения недостижимы. [ПОРТ] (Д7-3): диапазон — по списку, как у колеса.
	return FMath::Max(0, (List.Num() - 1) / kGridCells);
}

void UAsdaHighRefineSubsystem::SetPage(int32 NewPage, const TCHAR* Source)
{
	// Ветка 0x17 `uiTooltipWndProc`: `sub_CF3E80` = страница [92] + `uiInventoryBagPanelRefresh`, затем
	//   `uiResetTradeWindow`, `CControl_KillFocus(&g_ItemTooltip)`. Общая точка колеса и полосы 78226.
	Page = NewPage;
	{
		// `sub_CF3E80` — страница и перерисовка сетки. Список строится заново ТЕМ ЖЕ проходом по сумке.
		const int32 Keep = Page;
		RefreshBagPanel();
		Page = Keep;
		if (GridIconGrid) { GridIconGrid->Refresh(); }
	}
	ResetTradeWindow();
	TickTooltip(-1, -1);
	LayoutListScroll();
	JournalAdd(FString::Printf(TEXT("page %d (%s)"), Page, Source));
}

bool UAsdaHighRefineSubsystem::OnListScroll(EAsdaScrollAction Action, int32 Value)
{
	// Полоса 78226: `uiScrollbarOnButtonEvent` / `uiScrollbarOnMouseDrag` → событие 69633…69637 →
	//   колёсное окно `sub_12F3540` → `CControl_RaiseEvent(0x17, позиция)` → ветка 0x17 окна. Позиция
	//   полосы = страница (`uiControlSetScrollRange(1, 2)`: максимум 2 − 1 = 1, т.е. позиции 0…1 = страницы).
	//   Шаг стрелки и «страницы» дорожки = 1 (`uiControlSetScrollPage(78226, 1, 1)`).
	if (!bOpen || Action == EAsdaScrollAction::None || CVarWheelPages.GetValueOnGameThread() == 0) { return false; }
	const int32 MaxPage = GetMaxPage();
	const int32 NewPage = (Action == EAsdaScrollAction::SetRow)
		? FMath::Clamp(Value, 0, MaxPage)
		: FMath::Clamp(Page + Value, 0, MaxPage);
	// [ПОРТ] как у колеса (Д7-3): без смены страницы события нет. У эталона `sub_12F3540` поднимает 0x17 и на
	//   краю, и на каждом кадре перетаскивания — `uiResetTradeWindow` сбрасывал бы выбор от простого касания полосы.
	if (NewPage == Page) { return true; }
	// ★ Та же блокировка, что у колеса (волна 9): при «занято» (+2) ввод, меняющий выбор, съедается —
	//   ветка 0x17 эталона сама охраняет смену страницы `if (!+2)`.
	//   [ИСПРАВЛЕНИЕ ДЕФЕКТА ОРИГИНАЛА] (М-9 ревизии волны 12): эталон при «занято» бегунок СДВИГАЕТ
	//   (`uiScrollbarOnButtonEvent`: `+154 = pos` после `RaiseEvent`), хотя страница не меняется, — полоса
	//   показывает не ту страницу, что сетка. Порт держит бегунок на текущей странице.
	//   ★ Волна 13 (М-6): при перетаскивании бегунка сюда приходят КАЖДЫЙ кадр — журнал и раскладку делаем
	//   один раз за нажатие (латч снимается отпусканием ЛКМ в `TickListScroll`), иначе кольцо журнала
	//   в 256 строк вытеснялось бы за секунды.
	if (bBusy)
	{
		if (!bBusyScrollLogged)
		{
			bBusyScrollLogged = true;
			JournalAdd(TEXT("page — занято, полоса съедена"));
			LayoutListScroll();
		}
		return true;
	}
	SetPage(NewPage, Action == EAsdaScrollAction::SetRow ? TEXT("бегунок") : TEXT("полоса"));
	return true;
}

void UAsdaHighRefineSubsystem::EnforceSinglePageWhenKeyOff()
{
	// ★ Волна 13 (СРЕДН-1 ревизии волны 12). У оригинала страница 2 недостижима — `Page ≡ 0`. Ключ Д7-3
	//   переключается и на ходу (самотест, консоль): при переходе в 0 со 2-й страницы сетка и рецепт
	//   оставались на странице 2. Приводим к одной странице тем же путём 0x17 (`SetPage`: сетка, сброс
	//   выбора, тултип, полоса). При «занято» (+2) ждём его снятия: смена страницы сбросила бы выбор
	//   посреди попытки (та же блокировка, что у колеса), а отправка всё равно берёт ту страницу, что
	//   видна в сетке (`SelCell + 14*Page`), — рассинхрона нет.
	if (Page == 0 || CVarWheelPages.GetValueOnGameThread() != 0 || bBusy) { return; }
	if (!bOpen)
	{
		// Окно закрыто: сетку перестроит `OpenWindow` (`RefreshBagPanel` сбрасывает выбор).
		Page = 0;
		SelCell = -1;
		JournalAdd(TEXT("page 0 (ключ Д7-3 = 0, окно закрыто)"));
		return;
	}
	SetPage(0, TEXT("ключ Д7-3 = 0"));
}

bool UAsdaHighRefineSubsystem::DebugCapture6500(bool bOn)
{
	// Перехватчик стенда пакетов (`UAsdaNetSubsystem::SendCaptureForStand`, friend): БОЕВОЙ путь отправки
	//   до `SendSecured` включительно. ★ Волна 14 (МЕЛК-3): узкий — только 6500 (`SendCaptureOnlyOpcode`).
	if (!bOn) { DisarmCapture6500(TEXT("снят самотестом")); return true; }
	UAsdaNetSubsystem* Net = GetGameInstance() ? GetGameInstance()->GetSubsystem<UAsdaNetSubsystem>() : nullptr;
	if (!Net) { return false; }
	if (Net->IsConnected())
	{
		UE_LOG(LogAsdaHighRefine, Warning, TEXT("[переплавка] перехват 6500 НЕ поставлен: есть живое соединение с сервером"));
		return false;
	}
	if (Net->SendCaptureForStand)
	{
		UE_LOG(LogAsdaHighRefine, Warning, TEXT("[переплавка] перехват 6500 НЕ поставлен: перехватчик уже занят (стенд/питомцы)"));
		return false;
	}
	DebugSent6500.Reset();
	TWeakObjectPtr<UAsdaHighRefineSubsystem> WeakThis(this);
	// ★ Волна 15 (МЕЛК-2 ревизии волны 14): ставим через `ArmSendCapture` и запоминаем поколение — снимаем потом
	//   только СВОЙ перехватчик, даже если чужой (питомцы) тоже узкий или стоит на тот же опкод.
	Capture6500Gen = Net->ArmSendCapture([WeakThis](uint16 Op, const uint8* P, int32 Len, bool /*bPad*/, TCHAR /*Env*/)
	{
		UAsdaHighRefineSubsystem* Self = WeakThis.Get();
		if (!Self || Op != (uint16)OP_RECV_USER_UPPER_REFINE) { return; }
		TArray<uint8>& B = Self->DebugSent6500.AddDefaulted_GetRef();
		if (P && Len > 0) { B.Append(P, Len); }
		// Первый 6500 пойман — перехватчик снимается СРАЗУ (отправитель зовёт копию, снимать изнутри безопасно).
		Self->DisarmCapture6500(TEXT("пойман первый 6500"));
	}, (uint16)OP_RECV_USER_UPPER_REFINE);
	bCapture6500Armed = true;
	Capture6500ArmedAt = FPlatformTime::Seconds();
	return true;
}

bool UAsdaHighRefineSubsystem::DebugNetHasSendCapture() const
{
	const UGameInstance* GI = GetGameInstance();
	const UAsdaNetSubsystem* Net = GI ? GI->GetSubsystem<UAsdaNetSubsystem>() : nullptr;
	return Net && (static_cast<bool>(Net->SendCaptureForStand) || Net->SendCaptureOnlyOpcode != 0);
}

bool UAsdaHighRefineSubsystem::DebugNetLinkFakedByCapture() const
{
	const UGameInstance* GI = GetGameInstance();
	const UAsdaNetSubsystem* Net = GI ? GI->GetSubsystem<UAsdaNetSubsystem>() : nullptr;
	// «Связь есть» при отсутствии сокета — значит, перехватчик выдаёт себя за соединение (так делает только стенд).
	return Net && Net->IsSendLinkUp() && !Net->IsConnected();
}

void UAsdaHighRefineSubsystem::DisarmCapture6500(const TCHAR* Why)
{
	if (!bCapture6500Armed) { return; }
	bCapture6500Armed = false;
	UGameInstance* GI = GetGameInstance();
	UAsdaNetSubsystem* Net = GI ? GI->GetSubsystem<UAsdaNetSubsystem>() : nullptr;
	// Снимаем только СВОЙ перехватчик. ★ Волна 15 (МЕЛК-2): «свой» — по поколению `ArmSendCapture`, а не по
	//   опкоду: если за это время перехватчик сменил кто-то другой (питомцы), его не трогаем.
	const bool bMine = Net && Net->DisarmSendCapture(Capture6500Gen);
	Capture6500Gen = 0;
	UE_LOG(LogAsdaHighRefine, Log, TEXT("[переплавка] перехват 6500 снят: %s%s"), Why,
		bMine ? TEXT("") : TEXT(" (перехватчик уже сменён другим — чужой не трогаю)"));
}

void UAsdaHighRefineSubsystem::LayoutListScroll()
{
	// `uiScrollbarSetRange(1, P)`: максимум позиции P − 1, доля видимого 1/P (у эталона P = 2), бегунок ≥ 6 px;
	//   при одной странице бегунок схлопывается, стрелки и дорожка остаются (флаг +632). Ключ Д7-3 = 0 —
	//   полосы нет, как у оригинала (78226 `Visible = 0`).
	if (!ListScroll.IsBuilt()) { return; }
	if (!bOpen || CVarWheelPages.GetValueOnGameThread() == 0 || ListScrollH <= 0.f)
	{
		ListScroll.Hide();
		if (ListZone1) { ListZone1->SetVisibility(ESlateVisibility::Collapsed); }
		return;
	}
	if (ListZone1) { ListZone1->SetVisibility(ESlateVisibility::HitTestInvisible); }   // `NoSelect = 1`
	const int32 MaxPage = GetMaxPage();
	// ★ Волна 14 (МЕЛК-1): целые аргументы эталона `(1, P)`, P = MaxPage + 1.
	ListScroll.Layout(ListScrollOrigin, ListScrollH, /*Scale*/1.f, 1, MaxPage + 1, MaxPage,
		FMath::Clamp(Page, 0, MaxPage));
}

bool UAsdaHighRefineSubsystem::TickListScroll()
{
	if (!ListScroll.IsBuilt()) { return false; }
	// Ключ выключили на ходу — полосу убрать (и ничего не ловить); включили — вернуть.
	const bool bShown = ListScroll.Track && ListScroll.Track->GetVisibility() != ESlateVisibility::Collapsed;
	if (CVarWheelPages.GetValueOnGameThread() == 0)
	{
		if (bShown) { LayoutListScroll(); }
		ListScroll.bThumbGrab = false;
		return false;
	}
	if (!bShown) { LayoutListScroll(); }
	// (★ волна 14, МЕЛК-5: сброс латча `bBusyScrollLogged` перенесён в `Tick` — до всех ранних выходов.)
	const bool bOver = ListScroll.IsCursorOver();
	int32 Value = 0;
	const EAsdaScrollAction A = ListScroll.Tick(Mouse.bLmb, Mouse.bLmbDown, GetMaxPage(), Value);
	if (A != EAsdaScrollAction::None) { OnListScroll(A, Value); }
	return bOver || ListScroll.bThumbGrab;
}

UWidget* UAsdaHighRefineSubsystem::DebugGetScrollPlane(int32 Plane) const
{
	switch (Plane)
	{
	case 0: return ListScroll.Track;
	case 1: return ListScroll.Up;
	case 2: return ListScroll.Down;
	case 3: return ListScroll.Thumb;
	default: return nullptr;
	}
}

bool UAsdaHighRefineSubsystem::DebugIsScrollShown() const
{
	return ListScroll.Track && ListScroll.Track->GetVisibility() != ESlateVisibility::Collapsed;
}

FString UAsdaHighRefineSubsystem::DumpListScroll() const
{
	// Зона 78226, планы полосы (слот в канвасе группы и живая геометрия) и бегунок — для самотеста и консоли.
	FString S = FString::Printf(TEXT("=== полоса 78226: страница %d / макс %d, список %d, ключ Д7-3=%d, показана=%d ===\n"),
		Page, GetMaxPage(), List.Num(), CVarWheelPages.GetValueOnGameThread(), DebugIsScrollShown() ? 1 : 0);
	auto Row = [&S](const TCHAR* Id, const UWidget* W)
	{
		if (!W) { S += FString::Printf(TEXT("  %-10s НЕТ\n"), Id); return; }
		const UCanvasPanelSlot* Sl = AsdaUi::SlotOf(W);
		const FMargin O = Sl ? Sl->GetOffsets() : FMargin();
		const FGeometry& G = W->GetCachedGeometry();
		const FVector2D P = G.GetAbsolutePosition();
		const FVector2D Z = G.GetAbsoluteSize();
		S += FString::Printf(TEXT("  %-10s слот L%.0f T%.0f %.0f×%.0f · экран (%.0f,%.0f) %.0f×%.0f · вид=%d\n"), Id,
			O.Left, O.Top, O.Right, O.Bottom, P.X, P.Y, Z.X, Z.Y, (int32)W->GetVisibility());
	};
	Row(TEXT("зона78226"), ListZone1);
	Row(TEXT("зона78265"), ListZone2);
	Row(TEXT("дорожка"), ListScroll.Track);
	Row(TEXT("↑"), ListScroll.Up);
	Row(TEXT("↓"), ListScroll.Down);
	Row(TEXT("бегунок"), ListScroll.Thumb);
	S += TEXT("  ") + ListScroll.Dump();
	return S;
}
