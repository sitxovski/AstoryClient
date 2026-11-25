// Copyright (c) 2026 @sitxovski (Discord)
// AsdaItemUpgradeSubsystem_Window.cpp — ★ R3-c 12.11.2026 (разрез AsdaItemUpgradeSubsystem.cpp, дословный перенос): контролы всплывашки, открыть/скрыть, кнопки, панели помощников/сумки/свитков, карточки, проверка выбора.
//   Общее для частей — в AsdaItemUpgradeSubsystem_Private.h; шапка и реверс — в AsdaItemUpgradeSubsystem.cpp.
#include "UI/AsdaItemUpgradeSubsystem.h"
#include "Misc/Paths.h"               // ★ Д-18: путь к Config проекта


#include "AsdaItemUpgradeSubsystem_Private.h"   // ★ R3-c 12.11.2026: общее для частей AsdaItemUpgradeSubsystem*.cpp

// ═════════════════════════════ КЛЮЧИ ОТКАТА ИСПРАВЛЕНИЙ ═════════════════════════════════════════
//   Каждое видимое игроку отступление от оригинала — с ключом; значение по умолчанию = «исправлено».
// ★ Д-12 (а). Эталон `itemEnhanceUI_OnControlMessage@0xD2A1C0` (item_inventory.c:44319-44329) выводит
//   текст успеха 50004 (чат + окно) ТОЛЬКО при ступени `+24 <= 5`, а у провала такого условия нет.
//   Вместе с узким попапом (б) успех на ступенях 11…19 проходил совершенно молча.
// ★★ [ОРИГИНАЛ] 30.10.2026 (волна bugs8, «нет цвета в текстах»): подтверждение 110110/110111 эталон кладёт в окно
//   С ТЕГАМИ (`sprintf(v167, "%s", UI_FormatLocalizedNotice(…))` → `CControl_SetText(msgbox, v167)`, item_inventory.c:51128-51137) —
//   предупреждение подсвечено `<FC=FFF26C4F>`. Порт брал `Loc` (теги срезаны). 1 = с тегами (`LocRaw`), 0 = прежний срез.
bool AsdaRelayAnchorSplitOn();   // ключ `asda.UI.RelayAnchorSplit` (UI/AsdaUiHelpers.cpp) — свой GC-якорь реле у каждого окна

// ★ Д-13. Затемнители 20100…20129 в `.JUI` есть, но `CItemUpgradeWin_InitControls` их не привязывает
//   (цикла с базой 20100 нет) — игрок узнаёт «нельзя точить» только нотисом 50011 после клика.
//   Клиентский признак «не точится» — `запись +26 == 0` (ровно гейт `ui_core.c:48320`).
static ASDA_DEV_CVAR(int32, CVarDimNonUpgradeable,
	TEXT("asda.Upgrade.DimNonUpgradeable"), 1,
	TEXT("Дефект оригинала Д-13: 1 = затемнять в сетке вещи с пределом попыток 0 (не точатся); "
	     "0 = как оригинал — затемнители не используются."),
	ECVF_Default);

// Тип снаряжения, который сервер пускает в заточку (шлем, доспех, штаны, ботинки, перчатки, щит, оружие).
static bool IsUpgradeGearType(int32 ItemType)
{
	return (ItemType >= 0 && ItemType <= 4) || ItemType == 8 || ItemType == 9;
}

// ★ Д-18 (продолжение): коды, которые клиент считает снаряжением, а серверная Item_Table.txt — нет (заготовки «-», удочки
//   69630/69631, тыквенные головы 73346…73349): сервер их не точит. Набор — ДАННЫЕ `Config/DefaultAsdaUpgrade.ini`
//   (`[Upgrade.ServerNonGear]`, генератор `Tools/asda_export_upgrade_nongear_codes.py`), ленивая загрузка при первом вызове.
static bool IsServerNonGearCode(int32 ItemCode)
{
	static TSet<int32> Codes;        // коды секции ini
	static bool bLoaded = false;     // загрузка выполнена (даже если секции нет)
	if (!bLoaded)
	{
		bLoaded = true;
		// Файл читается напрямую: `FConfigCacheIni::LoadLocalIniFile(…, "AsdaUpgrade", true)` в самотесте 30.10 вернул 0 строк.
		const FString Path = FPaths::Combine(FPaths::ProjectConfigDir(), TEXT("DefaultAsdaUpgrade.ini"));
		FString Text;
		const bool bRead = FFileHelper::LoadFileToString(Text, *Path);
		TArray<FString> Lines;
		Text.ParseIntoArrayLines(Lines, /*bCullEmpty*/ true);
		bool bInSection = false;
		for (FString L : Lines)
		{
			L.TrimStartAndEndInline();
			if (L.StartsWith(TEXT("["))) { bInSection = (L == TEXT("[Upgrade.ServerNonGear]")); continue; }
			FString Value;
			if (bInSection && L.Split(TEXT("+Code="), nullptr, &Value) && Value.IsNumeric()) { Codes.Add(FCString::Atoi(*Value)); }
		}
		UE_LOG(LogAsdaUpgrade, Log, TEXT("[улучшение] коды «сервер — не снаряжение» (%s, прочитан=%d, строк %d): %d"),
			*Path, bRead ? 1 : 0, Lines.Num(), Codes.Num());
	}
	return Codes.Contains(ItemCode);
}

// ★★ Д-19 [ИСПРАВЛЕНИЕ, дефект оригинала] 04.11.2026 (жалоба «редкие свитки не работают»). Эталон (`uiCombineRefreshSlots@0xD28A80`,
//   ui_core.c:48549-48580) сверяет грейд свитка с КЛИЕНТСКИМ грейдом цели `pricetable.dw04`, класс — по КЛИЕНТСКОМУ типу. Сервер
//   (`CharShellItemUpgrade_0@0x4FEE50`, GameServer.c:98674…) — со СВОИМ грейдом SowelCombine.txt (слово 46, +150; ветка «(2)» →
//   6505 код 3 «Недостаточно купонов») и СВОИМ типом Item_Table.txt (слово 16; ветка «(22)» → код 7). У 11 146 из 47 479 вещей
//   грейды расходятся (у сток-сервера так же): окно предлагало свитки, которые сервер отвергает (01.11 четыре отказа «(2)» на
//   41798), и прятало годные; серверного грейда 4 нет ни у одной вещи — свитки ранга S сервер не принимает. Ключ 1 — фильтр
//   по серверу; 0 — как оригинал.
static ASDA_DEV_CVAR(int32, CVarServerGradeFilter,
	TEXT("asda.Upgrade.ServerGradeFilter"), 1,
	TEXT("Дефект оригинала Д-19: 1 = свитки окна улучшения отбираются по СЕРВЕРНОМУ грейду и типу цели (Config/DefaultAsdaUpgradeGrade.ini — "
	     "ровно то, что примет сервер); 0 = как оригинал — по клиентским pricetable.dw04 и itemType."),
	ECVF_Default);

// Серверные грейд и тип цели для Д-19: ДАННЫЕ `Config/DefaultAsdaUpgradeGrade.ini` (`[Upgrade.ServerGrade]`, строки
//   `+Item=<код>,<грейд>,<тип>` — только коды, где клиент расходится с сервером; генератор `Tools/asda_export_upgrade_server_grade.py`),
//   ленивая загрузка при первом вызове. false — кода в наборе нет (клиентские поля равны серверным).
static bool ServerGradeOf(int32 ItemCode, int32& OutGrade, int32& OutType)
{
	static TMap<int32, TPair<int32, int32>> Rows;   // код → (серверный грейд, серверный тип)
	static bool bLoaded = false;                    // загрузка выполнена (даже если файла нет)
	if (!bLoaded)
	{
		bLoaded = true;
		const FString Path = FPaths::Combine(FPaths::ProjectConfigDir(), TEXT("DefaultAsdaUpgradeGrade.ini"));
		FString Text;
		const bool bRead = FFileHelper::LoadFileToString(Text, *Path);
		TArray<FString> Lines;
		Text.ParseIntoArrayLines(Lines, /*bCullEmpty*/ true);
		bool bInSection = false;
		for (FString L : Lines)
		{
			L.TrimStartAndEndInline();
			if (L.StartsWith(TEXT("["))) { bInSection = (L == TEXT("[Upgrade.ServerGrade]")); continue; }
			FString Value;
			if (!bInSection || !L.Split(TEXT("+Item="), nullptr, &Value)) { continue; }
			TArray<FString> F;
			Value.ParseIntoArray(F, TEXT(","));
			if (F.Num() == 3) { Rows.Add(FCString::Atoi(*F[0]), TPair<int32, int32>(FCString::Atoi(*F[1]), FCString::Atoi(*F[2]))); }
		}
		UE_LOG(LogAsdaUpgrade, Log, TEXT("[улучшение] серверный грейд/тип целей (%s, прочитан=%d): %d кодов"), *Path, bRead ? 1 : 0, Rows.Num());
	}
	const TPair<int32, int32>* P = Rows.Find(ItemCode);
	if (!P) { return false; }
	OutGrade = P->Key;
	OutType = P->Value;
	return true;
}

namespace {
	// Цвета эталона (int32 из `CControl_SetField923`, приводим к ARGB).
	constexpr uint32 kColEnchantLegacy = (uint32)-3584;      // 0xFFFFF200 — «+N%» под иконкой сетки
	constexpr uint32 kColLevelLimit    = (uint32)-65536;     // 0xFFFF0000 — «после» при пределе
	constexpr uint32 kColLevelNext     = (uint32)-2763520;   // 0xFFD5D500 — «после» обычный
	constexpr uint32 kColEffectAfter   = (uint32)-6250336;   // 0xFFA0A0A0 — «Эффект после»
	constexpr uint32 kColHelperCount   = (uint32)-1;         // 0xFFFFFFFF — число помощников

	// Порог переноса улучшения (Д-11): живёт на сервере, `Recv_User_Disassemble` `if (v22 >= 88)`.
	constexpr int32 kTransferThreshold = 88;

	// Подтипы вида свитка/купона/помощника (`Table2Record->subType`).
	constexpr int32 kSubTypeCoupon90 = 90;   // купон заточки в сумке (режим «сверх предела»)
	constexpr int32 kSubTypeHelper97 = 97;   // помощник: +% к эффекту (resultModel)
	constexpr int32 kSubTypeHelper98 = 98;   // помощник: +к шансу (сервер)
	constexpr int32 kLinkScroll      = 70;   // `ItemRecord->linkCode == 70` — свиток заточки

	// ARGB → линейный цвет UMG (закон ui-srgb-gamma-rule: данные в sRGB).
	FSlateColor Argb(uint32 C)
	{
		return FSlateColor(AsdaUi::SrgbHexToLinear(FString::Printf(TEXT("%08X"), C)));
	}

	// Цвет «+N%» под иконкой: пользовательский ключ инвентаря `asda.Inven.EnchantTextGradeColor`
	//   (Д9, просьба юзера) — читаем ЧУЖОЙ ключ по имени, второго такого же не заводим.
	uint32 EnchantLabelArgb(int32 Level)
	{
		static IConsoleVariable* V = ASDA_FIND_DEV_CVAR(TEXT("asda.Inven.EnchantTextGradeColor"));
		// ★ античит А2 (12.10.2026): в Shipping ключа нет (константа = умолчание 1), поэтому «ключа нет» = цвет по ступени —
		//   в Development ветка недостижима (ключ зарегистрирован всегда), поведение не меняется.
		const bool bGrade = V ? (V->GetInt() != 0) : true;
		return bGrade ? UAsdaItemTooltipSubsystem::EnhanceColorArgb(Level) : kColEnchantLegacy;
	}
}

void UAsdaItemUpgradeSubsystem::SetupPopupControls()
{
	bPopupReady = false;
	if (!PopupWidget || !PopupWidget->WidgetTree) { return; }
	FName IconName;
	TArray<UWidget*> All;
	PopupWidget->WidgetTree->GetAllWidgets(All);
	for (UWidget* W : All)
	{
		if (!W) { continue; }
		const FString N = W->GetName();
		if (UButton* B = Cast<UButton>(W)) { if (N == TEXT("Get_Upgrade_OKBtn")) { PopOkBtn = B; } continue; }   // 19106
		if (UTextBlock* Tb = Cast<UTextBlock>(W))
		{
			if (N == TEXT("Gain_Upgrade_Title"))      { PopTitle     = Tb; continue; }   // 19101
			if (N == TEXT("Get_Upgrade_Text"))        { PopText      = Tb; continue; }   // 19102
			if (N == TEXT("Icon_noimage"))            { PopIconLabel = Tb; continue; }   // 19103 (текст)
			if (N == TEXT("Get_Upgrade_OptionTitle")) { PopOptTitle  = Tb; continue; }   // 19104
			if (N == TEXT("Get_Upgrade_Option"))      { CaptureFmt(FmtPopOpt, Tb); continue; }   // 19105
			if (N == TEXT("Get_Upgrade_OKBtn_Text"))  { PopOkText    = Tb; continue; }
			continue;
		}
		if (UImage* Img = Cast<UImage>(W))
		{
			if (N == TEXT("Icon_noimage_10_0")) { PopIcon = Img; IconName = FName(*N); }   // 19103 (план)
		}
	}
	if (!IconName.IsNone())
	{
		PopIconGrid = NewObject<UAsdaIconCellGrid>(this);
		PopIconGrid->SetCellSizePx(64.f);
		PopIconGrid->SetClickOverlayEnabled(false);
		PopIconGrid->bClearEmptyCells = true;
		TArray<FName> One; One.Add(IconName);
		PopIconGrid->SetupNamed(PopupWidget, One, /*bMale*/true, [this](int32)
		{
			// ★ R-1: иконка — предмет из записи ответа 6505 (тот же источник, что текст попапа).
			return (ResultRecord.Num() >= 57) ? FMath::Max(0, AsdaBytes::RdI32(ResultRecord.GetData())) : 0;
		}, [](int32) {});
	}
	if (PopOkBtn)
	{
		UAsdaButtonRelay* R = NewObject<UAsdaButtonRelay>(this);
		R->Index = 19106;
		// ui_core.c:47647 `SetVisible(self[657], 0)`; звук id 5 — та же ветка 66561 (ui_core.c:47649).
		R->Callback = [this](int32) { PlayButtonSfx(19106); OnPopupOkClicked(); };
		PopOkBtn->OnClicked.AddDynamic(R, &UAsdaButtonRelay::OnClicked);
		// ★ 31.10.2026: якорь — попапа, а не окна заточки (его `ResetWidget` попап не закрывает; `asda.UI.RelayAnchorSplit`).
		AsdaUi::AnchorRelay(AsdaRelayAnchorSplitOn() ? PopupRelays : Relays, R, PopOkBtn);   // ★ 31.10.2026: прочистка (`asda.UI.RelayAnchorPrune`)
	}
	bPopupReady = (PopTitle && PopText && PopOkBtn);
	UE_LOG(LogAsdaUpgrade, Log,
		TEXT("[заточка] попап 19100 разобран: 19101=%s 19102=%s 19103=%s/%s 19104=%s 19105=%s 19106=%s (найдено %d/7)"),
		PopTitle ? TEXT("есть") : TEXT("НЕТ"), PopText ? TEXT("есть") : TEXT("НЕТ"),
		PopIcon ? TEXT("есть") : TEXT("НЕТ"), PopIconLabel ? TEXT("есть") : TEXT("НЕТ"),
		PopOptTitle ? TEXT("есть") : TEXT("НЕТ"), FmtPopOpt.Node ? TEXT("есть") : TEXT("НЕТ"),
		PopOkBtn ? TEXT("есть") : TEXT("НЕТ"),
		(PopTitle ? 1 : 0) + (PopText ? 1 : 0) + (PopIcon ? 1 : 0) + (PopIconLabel ? 1 : 0)
			+ (PopOptTitle ? 1 : 0) + (FmtPopOpt.Node ? 1 : 0) + (PopOkBtn ? 1 : 0));
}

// ═════════════════════════════ ОТКРЫТИЕ / ЗАКРЫТИЕ ════════════════════════════════════════════
void UAsdaItemUpgradeSubsystem::ToggleWindow()
{
	// `uiCombineWindow_Close@0xD28340`: окно видимо (`+292 == 1`) ⇒ только спрятать.
	if (bOpen) { HideWindow(); return; }
	OpenWindow();
}

void UAsdaItemUpgradeSubsystem::OpenWindow()
{
	if (!EnsureWidget()) { return; }
	UGameInstance* GI = GetGameInstance();
	UAsdaWindowSubsystem* WM = GI ? GI->GetSubsystem<UAsdaWindowSubsystem>() : nullptr;
	if (!WM) { return; }
	// Ветка «невидимо»: спрятать 20073, полный сброс, цель = 0, погасить плашку, поднять окно наверх.
	ShowFx(EAsdaUpgradeFx::Fail, false);
	ResetOpen();
	Target.Clear();
	if (UAsdaItemTooltipSubsystem* Tip = GI->GetSubsystem<UAsdaItemTooltipSubsystem>()) { Tip->HideTooltipFor(EAsdaTipOwner::ItemUpgrade); }
	bOpen = true;
	Mouse.Reset();
	TipKind = TipIdx = -1;
	BagScroll.Reset();   // ★ 29.10.2026 (инвентарь ×2): окно открывается с первого ряда сетки
	WM->SetWindowVisible(kWindowId, true);
	WM->BringWindowToFront(kWindowId);   // `CClass_MapInsert(g_pWindowMgr, окно)`
	LastValidateTime = -1.0;             // гайд 20013 пишет тик — первый проход сразу
	UE_LOG(LogAsdaUpgrade, Log, TEXT("[заточка] окно 20000 открыто (замок полёта=%d)"), ReplyLatch.IsArmed() ? 1 : 0);
}

void UAsdaItemUpgradeSubsystem::HideWindow()
{
	if (!bOpen) { return; }
	bOpen = false;
	TipKind = TipIdx = -1;
	if (UAsdaItemTooltipSubsystem* Tip = GetGameInstance() ? GetGameInstance()->GetSubsystem<UAsdaItemTooltipSubsystem>() : nullptr)
	{
		Tip->HideTooltipFor(EAsdaTipOwner::ItemUpgrade);
	}
	if (UAsdaWindowSubsystem* WM = GetGameInstance() ? GetGameInstance()->GetSubsystem<UAsdaWindowSubsystem>() : nullptr)
	{
		WM->SetWindowVisible(kWindowId, false);
	}
	Mouse.Reset();
}

void UAsdaItemUpgradeSubsystem::OnCancelClicked()
{
	// `uiHidePopups@0xD2A400`: +3392 = 0, окно спрятать, курсор по умолчанию, цель = 0, спрятать 20060.
	bLive = false;
	HideWindow();
	Target.Clear();
	if (FrameItemSel) { FrameItemSel->SetVisibility(ESlateVisibility::Collapsed); }
}

void UAsdaItemUpgradeSubsystem::HidePopup()
{
	if (!bPopupOpen) { return; }
	bPopupOpen = false;
	if (UAsdaWindowSubsystem* WM = GetGameInstance() ? GetGameInstance()->GetSubsystem<UAsdaWindowSubsystem>() : nullptr)
	{
		WM->SetWindowVisible(kPopupId, false);
	}
}

void UAsdaItemUpgradeSubsystem::OnPopupOkClicked()
{
	HidePopup();
}

void UAsdaItemUpgradeSubsystem::OnSpecialClicked()
{
	// Эталон `ui_core.c:47684`: `if (cmdId == 20197) craftWnd_Open(g_pTooltipWnd)` — окно высокой
	//   переплавки `Smelting3_Win` 78200 (`CHighRefineUI_InitControls`, опкоды 6500/6501). Это СОСЕДНЯЯ
	//   система со своим объектом (`g_pTooltipWnd` — миснейм), порт — `UAsdaHighRefineSubsystem` (волна 7).
	//   `craftWnd_Open` — НЕ тумблер: видимое окно он не трогает.
	UAsdaHighRefineSubsystem* HR = GetGameInstance() ? GetGameInstance()->GetSubsystem<UAsdaHighRefineSubsystem>() : nullptr;
	if (!HR)
	{
		UE_LOG(LogAsdaUpgrade, Warning, TEXT("[заточка] 20197 «Особый»: подсистема окна 78200 недоступна"));
		return;
	}
	HR->OpenWindow();
	UE_LOG(LogAsdaUpgrade, Log, TEXT("[заточка] 20197 «Особый» → окно 78200 %s"), HR->IsOpen() ? TEXT("открыто") : TEXT("не открылось"));
}

// ═════════════════════════════ ПОРТ ФУНКЦИЙ ОКНА ══════════════════════════════════════════════
void UAsdaItemUpgradeSubsystem::ResetOpen()
{
	// `uiCombineWindowResetOpen@0xD29EF0`: +3392 = 1, +3394 = 1, +3404 = 0.
	bLive = true;
	// ★ N-2: эталон здесь «отпускает» замок полёта (+3394 = 1) — отсюда второй 6504 до ответа.
	const bool bKeepLock = ReplyLatch.IsArmed() && CVarSingleInFlight.GetValueOnGameThread() != 0;
	bIdle = !bKeepLock;
	bArmed = false;
	// Шесть ячеек свитков — пустые, рамки 20058/20140/20141 — скрыть.
	for (UTextBlock* Tb : ScrollLabels) { if (Tb) { Tb->SetText(FText::GetEmpty()); } }
	if (FrameScrollSel) { FrameScrollSel->SetVisibility(ESlateVisibility::Collapsed); }
	if (FrameSubSel0)   { FrameSubSel0->SetVisibility(ESlateVisibility::Collapsed); }
	if (FrameSubSel1)   { FrameSubSel1->SetVisibility(ESlateVisibility::Collapsed); }
	// Все шесть эффектов гасятся — при открытии эффектов нет (ui_core.c:48890-48901). Буфер результата,
	//   не дождавшийся конца анимации, отдаём ДО сброса цели (ему нужна ступень предмета и попап).
	FlushPendingResultIfAny();
	HideAllFx();
	Target.Clear(); Scroll.Clear(); Helper97.Clear(); Helper98.Clear();
	GridList.Reset();
	ScrollList.Reset();
	if (ScrollGrid) { ScrollGrid->Refresh(); }
	PanelResetAndHide();
	RefreshInventorySlots();
	RefreshScrolls();
	RedrawCards();
	if (TxtPanelValue) { TxtPanelValue->SetText(FText::GetEmpty()); }   // `CWidget_SetTextAll4(self[832], "")`
}

void UAsdaItemUpgradeSubsystem::PanelResetAndHide()
{
	// `uiPanel_ResetAndHide@0xD29830`: иконка цели пустая, семь полей текста пустые, цель = 0, 20060 скрыть.
	Target.Clear();
	if (TargetGrid) { TargetGrid->Refresh(); }
	SetFmt(FmtName, FString());
	for (UTextBlock* Tb : { TxtLevelBef.Get(), TxtLevelAft.Get(), TxtEffectBef.Get(), TxtEffectAft.Get(),
	                        TxtTriesBef.Get(), TxtTriesAft.Get() })
	{
		if (Tb) { Tb->SetText(FText::GetEmpty()); }
	}
	if (FrameItemSel) { FrameItemSel->SetVisibility(ESlateVisibility::Collapsed); }
	// Наше (Д-11/N-6): панель «+N%» принадлежит выбранному предмету — без цели её нет.
	UpdateEnhancePanel(0, false);
}

void UAsdaItemUpgradeSubsystem::RefreshHelpers97()
{
	// `invGridRefreshTable2Type97@0xD28420`: сумка 0…29, `Table2Record->subType == 97`; рисуются первые три.
	H97List.Reset();
	const UAsdaInventorySubsystem* Inv = GetGameInstance() ? GetGameInstance()->GetSubsystem<UAsdaInventorySubsystem>() : nullptr;
	if (Inv)
	{
		for (int32 C = 0; C < kShellCells; ++C)
		{
			const FAsdaItemInstance* It = Inv->GetCell(kBag, C);
			if (!It || It->IsEmpty() || SpecialSubTypeOf(It->ItemCode) != kSubTypeHelper97) { continue; }
			FAsdaItemUpgradeEntry E; E.ItemCode = It->ItemCode; E.Container = kBag; E.CellIndex = C;
			H97List.Add(E);
		}
	}
	for (int32 C = 0; C < kHelperCells; ++C)
	{
		if (!H97Labels.IsValidIndex(C) || !H97Labels[C]) { continue; }
		const FAsdaItemInstance* It = H97List.IsValidIndex(C) ? LiveOf(H97List[C]) : nullptr;
		if (It && It->Amount > 0)
		{
			H97Labels[C]->SetText(FText::FromString(FString::FromInt(It->Amount)));   // U10e: `sprintf("%d")`, item_inventory.c:44145 — разбор у H98
			H97Labels[C]->SetColorAndOpacity(Argb(kColHelperCount));   // `CControl_SetField923(…, −1)`
		}
		else
		{
			H97Labels[C]->SetText(FText::GetEmpty());
			if (H97LabelBase.IsValidIndex(C)) { H97Labels[C]->SetColorAndOpacity(H97LabelBase[C]); }
		}
	}
	if (H97Grid) { H97Grid->Refresh(); }
}

void UAsdaItemUpgradeSubsystem::RefreshHelpers98()
{
	// `uiPanelRefreshItemsCat98@0xD28750`: то же для `subType == 98`.
	H98List.Reset();
	const UAsdaInventorySubsystem* Inv = GetGameInstance() ? GetGameInstance()->GetSubsystem<UAsdaInventorySubsystem>() : nullptr;
	if (Inv)
	{
		for (int32 C = 0; C < kShellCells; ++C)
		{
			const FAsdaItemInstance* It = Inv->GetCell(kBag, C);
			if (!It || It->IsEmpty() || SpecialSubTypeOf(It->ItemCode) != kSubTypeHelper98) { continue; }
			FAsdaItemUpgradeEntry E; E.ItemCode = It->ItemCode; E.Container = kBag; E.CellIndex = C;
			H98List.Add(E);
		}
	}
	for (int32 C = 0; C < kHelperCells; ++C)
	{
		if (!H98Labels.IsValidIndex(C) || !H98Labels[C]) { continue; }
		const FAsdaItemInstance* It = H98List.IsValidIndex(C) ? LiveOf(H98List[C]) : nullptr;
		if (It && It->Amount > 0)
		{
			// ★ ИСПРАВЛЕНИЕ 11.10.2026 (дедуп №2, В2-A, U10e): было `FText::AsNumber` (разделитель разрядов по культуре: EN «1,234», RU «1 234») — стало `FString::FromInt`, как `sprintf("%d")` эталона (`invGridRefreshTable2Type97@0xD28420` item_inventory.c:44145,
			//   `uiPanelRefreshItemsCat98@0xD28750` ui_core.c:48436).
			H98Labels[C]->SetText(FText::FromString(FString::FromInt(It->Amount)));
			H98Labels[C]->SetColorAndOpacity(Argb(kColHelperCount));
		}
		else
		{
			H98Labels[C]->SetText(FText::GetEmpty());
			if (H98LabelBase.IsValidIndex(C)) { H98Labels[C]->SetColorAndOpacity(H98LabelBase[C]); }
		}
	}
	if (H98Grid) { H98Grid->Refresh(); }
}

void UAsdaItemUpgradeSubsystem::RefreshInventorySlots()
{
	// `uiRefreshInventorySlots@0xD29A20`.
	RefreshHelpers98();
	RefreshHelpers97();
	// Цель пропала из ячейки (запись с кодом 0) или цели нет — сброс панели.
	{
		const FAsdaItemInstance* T = LiveOf(Target);
		if (!T || T->IsEmpty()) { PanelResetAndHide(); }
	}
	GridList.Reset();
	const UGameInstance* GI = GetGameInstance();
	const UAsdaInventorySubsystem* Inv = GI ? GI->GetSubsystem<UAsdaInventorySubsystem>() : nullptr;
	const UAsdaItemTableSubsystem* IT = GI ? GI->GetSubsystem<UAsdaItemTableSubsystem>() : nullptr;
	if (Inv && IT)
	{
		for (int32 C = 0; C < kShellCells; ++C)
		{
			const FAsdaItemInstance* It = Inv->GetCell(kBag, C);
			if (!It || It->IsEmpty()) { continue; }
			const FAsdaItemRecord* R = IT->GetItem(It->ItemCode);   // только записи `item_list`
			if (!R || R->ItemSubType == 66 || R->ItemSubType == 200 || R->ItemSubType == 201) { continue; }
			// ★ [ИСПРАВЛЕНИЕ] Д-18: только снаряжение — типы, которые сервер пускает в заточку (маска `Recv_User_ShellItemUpgrade(22)`).
			if (CVarGearOnlyGrid.GetValueOnGameThread() != 0 && (!IsUpgradeGearType(R->ItemType) || IsServerNonGearCode(R->ItemCode))) { continue; }
			FAsdaItemUpgradeEntry E; E.ItemCode = It->ItemCode; E.Container = kBag; E.CellIndex = C;
			GridList.Add(E);
		}
	}
	BagScroll.SetTotal(GridList.Num());   // ★ 29.10.2026 (инвентарь ×2): ряд клампится, если список укоротился
	RefreshGridView();
}

// ★ [ПОРТ] 29.10.2026 (инвентарь ×2): видимое окно сетки 20020…20049 над `GridList` — подписи «+N%», затемнители,
//   иконки и рамка выбранной цели 20060 (видна, только если цель в окне). Зовётся после пересборки списка и по колесу.
void UAsdaItemUpgradeSubsystem::RefreshGridView()
{
	const bool bDim = CVarDimNonUpgradeable.GetValueOnGameThread() != 0;
	for (int32 C = 0; C < kGridCells; ++C)
	{
		const int32 L = BagScroll.ToList(C);
		const FAsdaItemInstance* It = GridList.IsValidIndex(L) ? LiveOf(GridList[L]) : nullptr;
		if (GridLabels.IsValidIndex(C) && GridLabels[C])
		{
			// «+N%» (i16 запись +27) при ступени +24 > 0, цвет −3584 (или по ступени — ключ инвентаря Д9).
			if (It && It->GemGrade > 0)
			{
				GridLabels[C]->SetText(FText::FromString(FString::Printf(TEXT("+%d%%"), It->EnchantPercent)));
				GridLabels[C]->SetColorAndOpacity(Argb(EnchantLabelArgb((int32)It->GemGrade)));
			}
			else
			{
				GridLabels[C]->SetText(FText::GetEmpty());
				if (GridLabelBase.IsValidIndex(C)) { GridLabels[C]->SetColorAndOpacity(GridLabelBase[C]); }
			}
		}
		// ★ Д-13 (наше): затемнитель 20100+C у вещи с пределом попыток 0 (`+26 == 0` — не точится).
		if (DisableGroups.IsValidIndex(C))
		{
			SetGroupVisible(DisableGroups[C], bDim && It && RecByte(*It, 26) == 0);
		}
	}
	if (GridIconsGrid) { GridIconsGrid->Refresh(); }
	// Рамка выбранной цели — за позицией списка: в окне прокрутки — над её ячейкой, вне окна — спрятана.
	//   ★ Ревизия 30.10: только пока цель ЖИВА и позиция списка всё ещё она (`SameCell`). После `PanelResetAndHide`
	//   (цель снята, ушла после 6505, ячейка опустела) `TargetIdx` устаревает до проверки в тике — рамку не показываем.
	if (FrameItemSel)
	{
		const bool bTargetLive = !Target.IsEmpty() && GridList.IsValidIndex(TargetIdx) && GridList[TargetIdx].SameCell(Target);
		const int32 V = bTargetLive ? BagScroll.ToVisible(TargetIdx) : -1;
		if (V >= 0 && GridIcons.IsValidIndex(V) && GridIcons[V] && GridIcons.Num() > 0 && GridIcons[0])
		{
			PlaceFrameAt(FrameItemSel, BaseItemSel, GridIcons[0], GridIcons[V]);
			FrameItemSel->SetVisibility(ESlateVisibility::HitTestInvisible);
		}
		else
		{
			FrameItemSel->SetVisibility(ESlateVisibility::Collapsed);
		}
	}
}

void UAsdaItemUpgradeSubsystem::RefreshScrolls()
{
	// `uiCombineRefreshSlots@0xD28A80(this, this[844])`.
	const FAsdaItemInstance* T = LiveOf(Target);
	if (!T || T->IsEmpty())
	{
		// Эталон при пустой цели зовёт `uiPanel_ResetAndHide` и СПИСОК СВИТКОВ НЕ ТРОГАЕТ (он чистится
		//   только в ветке «есть запись предмета»). 1:1 — прежние иконки остаются до сброса окна.
		PanelResetAndHide();
		return;
	}
	const UGameInstance* GI = GetGameInstance();
	const UAsdaInventorySubsystem* Inv = GI ? GI->GetSubsystem<UAsdaInventorySubsystem>() : nullptr;
	const UAsdaItemTableSubsystem* IT = GI ? GI->GetSubsystem<UAsdaItemTableSubsystem>() : nullptr;
	if (!Inv || !IT || !IT->GetItem(T->ItemCode)) { return; }

	// Режим «сверх предела»: `isUpgradeMode = +26 <= +24`.
	const bool bUpgradeMode = RecByte(*T, 26) <= (int32)T->GemGrade;
	ScrollList.Reset();
	// Расходники 0…59: `ItemRecord->linkCode == 70` и совместимость (`EventRecord` по `itemSubType` свитка).
	//   ★ Обе ветки эталона (обычная и «сверх предела») сканируют расходники ОДИНАКОВО (§7.5 отчёта).
	for (int32 C = 0; C < kSpendCells; ++C)
	{
		const FAsdaItemInstance* It = Inv->GetCell(kSpend, C);
		if (!It || It->IsEmpty()) { continue; }
		const FAsdaItemRecord* R = IT->GetItem(It->ItemCode);
		if (!R || R->LinkCode != kLinkScroll) { continue; }
		if (!IsScrollCompatible(R->ItemSubType, *T)) { continue; }
		FAsdaItemUpgradeEntry E; E.ItemCode = It->ItemCode; E.Container = kSpend; E.CellIndex = C;
		ScrollList.Add(E);
	}
	// «Сверх предела»: плюс купоны сумки `Table2Record->subType == 90` — БЕЗ проверки совместимости.
	if (bUpgradeMode)
	{
		for (int32 C = 0; C < kShellCells; ++C)
		{
			const FAsdaItemInstance* It = Inv->GetCell(kBag, C);
			if (!It || It->IsEmpty() || SpecialSubTypeOf(It->ItemCode) != kSubTypeCoupon90) { continue; }
			FAsdaItemUpgradeEntry E; E.ItemCode = It->ItemCode; E.Container = kBag; E.CellIndex = C;
			ScrollList.Add(E);
		}
	}
	// Шесть видимых ячеек: иконка и «%d» — количество (+9), без ограничения самого списка.
	for (int32 C = 0; C < kScrollCells; ++C)
	{
		if (!ScrollLabels.IsValidIndex(C) || !ScrollLabels[C]) { continue; }
		const FAsdaItemInstance* It = ScrollList.IsValidIndex(C) ? LiveOf(ScrollList[C]) : nullptr;
		ScrollLabels[C]->SetText(It ? FText::FromString(FString::Printf(TEXT("%d"), It->Amount)) : FText::GetEmpty());
	}
	if (ScrollGrid) { ScrollGrid->Refresh(); }
}

bool UAsdaItemUpgradeSubsystem::IsScrollCompatible(int32 ScrollEventId, const FAsdaItemInstance& Tgt) const
{
	// `uiCombineRefreshSlots`, ui_core.c:48485-48510: `EventRecord[24] == 5 || == Table2Record_0[4]`
	//   (pricetable dw04 цели), затем класс по `EventRecord[20]`: 3 — всему; оружие (`itemType == 9`) — 0;
	//   доспех (`itemType <= 4 || == 8`) — 1; остальным (5/6/7…) — ничего.
	const UAsdaItemTableSubsystem* IT = GetGameInstance() ? GetGameInstance()->GetSubsystem<UAsdaItemTableSubsystem>() : nullptr;
	if (!IT) { return false; }
	FAsdaUpgradetableRow Ev;
	if (!IT->GetUpgradeTable(ScrollEventId, Ev)) { return false; }
	FAsdaPriceRow Price;
	if (!IT->GetPrice(Tgt.ItemCode, Price)) { return false; }
	const FAsdaItemRecord* TR = IT->GetItem(Tgt.ItemCode);
	if (!TR) { return false; }
	int32 Grade = Price.dw04;               // эталон: клиентский грейд цели
	int32 TypeOf = (int32)TR->ItemType;     // эталон: клиентский тип цели
	// ★ Д-19: грейд и тип — серверные (там, где клиент с сервером расходится). Пропуск сверки грейда сервером для видов
	//   74/84/91 здесь выражен тем же `ReqTargetGrade == 5`: у этих строк UpgradeTable (и у купона 90) грейд 5.
	if (CVarServerGradeFilter.GetValueOnGameThread() != 0) { ServerGradeOf(Tgt.ItemCode, Grade, TypeOf); }
	if (!(Ev.ReqTargetGrade == 5 || Ev.ReqTargetGrade == Grade)) { return false; }
	const uint32 Type = (uint32)TypeOf;
	if (Ev.ApplyItemTypeGroup == 3) { return true; }
	if (Type == 9) { return Ev.ApplyItemTypeGroup == 0; }
	if (Type <= 4 || Type == 8) { return Ev.ApplyItemTypeGroup == 1; }
	return false;
}

void UAsdaItemUpgradeSubsystem::RedrawCards()
{
	// `itemWindow_RedrawCompareSlots@0xD293C0`: три карточки 20150/20153/20156, затем инфо-панель.
	if (CardGrid) { CardGrid->Refresh(); }
	UpdateItemInfo();
}

void UAsdaItemUpgradeSubsystem::UpdateItemInfo()
{
	// `CItemWindow_UpdateItemInfo@0xD2C810` (item_inventory.c:44451-44720).
	const FAsdaItemInstance* T = LiveOf(Target);
	const UGameInstance* GI = GetGameInstance();
	const UAsdaItemTableSubsystem* IT = GI ? GI->GetSubsystem<UAsdaItemTableSubsystem>() : nullptr;
	const FAsdaItemRecord* TR = (T && !T->IsEmpty() && IT) ? IT->GetItem(T->ItemCode) : nullptr;
	if (!TR) { return; }

	if (TargetGrid) { TargetGrid->Refresh(); }
	const int32 Lvl  = (int32)T->GemGrade;          // +24
	const int32 Used = RecByte(*T, 25);             // +25
	const int32 Max  = RecByte(*T, 26);             // +26
	const int32 Pct  = T->EnchantPercent;           // +27 (i16)

	// Уровень: до «+N», после «+N+1» либо «+N+1(Предел)» красным при `+24 == +26`.
	if (TxtLevelBef) { TxtLevelBef->SetText(FText::FromString(FString::Printf(TEXT("+%d"), Lvl))); }
	if (TxtLevelAft)
	{
		if (Lvl == Max)
		{
			TxtLevelAft->SetText(FText::FromString(FString::Printf(TEXT("+%d(%s)"), Lvl + 1, *Loc(AsdaItemUpgradeLoc::kLimitSuffix))));
			TxtLevelAft->SetColorAndOpacity(Argb(kColLevelLimit));
		}
		else
		{
			TxtLevelAft->SetText(FText::FromString(FString::Printf(TEXT("+%d"), Lvl + 1)));
			TxtLevelAft->SetColorAndOpacity(Argb(kColLevelNext));
		}
	}
	// Эффект до «N%».
	if (TxtEffectBef) { TxtEffectBef->SetText(FText::FromString(FString::Printf(TEXT("%d%%"), Pct))); }
	// Панель «+N%» (Д-11/N-6).
	UpdateEnhancePanel(Pct, true);
	// Эффект после: пусто; при выбранном свитке — `EventRecord[+16] + +27 (+ resultModel помощника 97)`.
	if (TxtEffectAft)
	{
		TxtEffectAft->SetText(FText::GetEmpty());
		const FAsdaItemInstance* S = LiveOf(Scroll);
		if (S && !S->IsEmpty())
		{
			int32 Bonus97 = 0;
			if (const FAsdaItemInstance* H = LiveOf(Helper97)) { if (!H->IsEmpty()) { Bonus97 = SpecialResultModelOf(H->ItemCode); } }
			// Вид свитка по ОБЕИМ таблицам, как у эталона: `ItemRecord->itemSubType`, затем
			//   `Table2Record->subType` (купон 90) — второе, если есть, перекрывает первое.
			FAsdaUpgradetableRow Ev;
			if (const FAsdaItemRecord* SR = IT->GetItem(S->ItemCode))
			{
				if (IT->GetUpgradeTable(SR->ItemSubType, Ev))
				{
					TxtEffectAft->SetColorAndOpacity(Argb(kColEffectAfter));
					TxtEffectAft->SetText(FText::FromString(FString::Printf(TEXT("%d%%"), Ev.RateBasePercent + Pct + Bonus97)));
				}
			}
			const int32 Sub = SpecialSubTypeOf(S->ItemCode);
			if (Sub >= 0 && IT->GetUpgradeTable(Sub, Ev))
			{
				TxtEffectAft->SetColorAndOpacity(Argb(kColEffectAfter));
				TxtEffectAft->SetText(FText::FromString(FString::Printf(TEXT("%d%%"), Ev.RateBasePercent + Pct + Bonus97)));
			}
		}
	}
	// Попыток: до «(+26 − +25)/+26» (не ниже 0), после «(+26 − +25 − 1)/+26» (не ниже 0).
	if (TxtTriesBef) { TxtTriesBef->SetText(FText::FromString(FString::Printf(TEXT("%d/%d"), FMath::Max(0, Max - Used), Max))); }
	if (TxtTriesAft) { TxtTriesAft->SetText(FText::FromString(FString::Printf(TEXT("%d/%d"), FMath::Max(0, Max - Used - 1), Max))); }

	// Имя 20006: цвет контрола = цвет грейда (`CItemManager_GetDropColor(itemGrade)`), при ступени > 0
	//   префикс «+N%» (запись +27) цветом ступени (`itemGetGradeColor`), от ступени 5 — ещё класс
	//   `UI_FormatLocalizedNotice(50100 + ступень)` тем же цветом. Ветки `itemType == 9` и прочих у эталона
	//   дают ОДИН И ТОТ ЖЕ формат (item_inventory.c:44683-44712).
	const FString Name = IT->GetItemName(T->ItemCode);
	FString Raw;
	if (Lvl > 0)
	{
		const uint32 Gc = UAsdaItemTooltipSubsystem::EnhanceColorArgb(Lvl);
		if (Lvl < 5)
		{
			Raw = FString::Printf(TEXT("<FC=%08x>+%d%%</F> %s"), Gc, Pct, *Name);
		}
		else
		{
			Raw = FString::Printf(TEXT("<FC=%08x>+%d%%</F> <FC=%08x>%s</F> %s"), Gc, Pct, Gc,
				*LocRaw(AsdaItemUpgradeLoc::kGradeNameBase + Lvl), *Name);
		}
	}
	else
	{
		Raw = Name;
	}
	if (FmtName.bStyleOk)
	{
		FmtName.Style.SetColorAndOpacity(Argb(UAsdaItemTooltipSubsystem::GradeColorArgb(TR->ItemGrade)));
	}
	SetFmt(FmtName, Raw);
}

void UAsdaItemUpgradeSubsystem::UpdateEnhancePanel(int32 Percent, bool bHasTarget)
{
	// ★ Д-11 + N-6 (наше исправление, ключ `asda.Upgrade.EnhancePanel`). Эталон прячет 20193/20194 в ОБЕИХ
	//   ветках и пишет «+|% − 77|» в 20192 только при проценте ниже 77 (item_inventory.c:44570-44584).
	//   Исправлено: порог 88 (сервер), текст «+(88 − %)», 20193 показывается при проценте ниже порога.
	//   20194 не включаем НИКОГДА: его глифы в русском атласе — обрезки чужой надписи («Отменить привязк…»
	//   и «отмена»), смысл контрола кодом оригинала не задан (см. отчёт). 20195 — подложка полосы и носитель
	//   подсказки 110112 — видна, пока выбран предмет.
	const bool bFix = CVarEnhancePanel.GetValueOnGameThread() != 0;
	const bool bShowValue = bFix && bHasTarget && Percent < kTransferThreshold;
	if (bHasTarget && TxtPanelValue)
	{
		if (Percent < kTransferThreshold)
		{
			// Эталонный формат `g_pFmtLevel` = «+%d» (строка по адресу 0x13E8EF0 в бинаре).
			TxtPanelValue->SetText(FText::FromString(FString::Printf(TEXT("+%d"), FMath::Abs(Percent - kTransferThreshold))));
		}
	}
	AsdaUi::SetShownNoHit(Panel20193, bShowValue);
	// ★ Волна 12: `bDebugShow20194` — только кадр для владельца (`asda.Upgrade.PanelShots`, режим 3).
	AsdaUi::SetShownNoHit(Panel20194, bDebugShow20194);
	AsdaUi::SetShownNoHit(Panel20195, (bFix && bHasTarget));
}

void UAsdaItemUpgradeSubsystem::TickValidateSelection()
{
	// `uiItemPanelTickValidateSelection@0xD2BD40` (ui_core.c:48963-49200). Сравнение «запись под индексом
	//   списка == запомненный указатель» у нас — «та же ячейка того же контейнера».
	RefreshInventorySlots();
	RefreshScrolls();

	auto ResetFxAndFlags = [this]()
	{
		FlushPendingResultIfAny();
		HideAllFx();
		bIdle = true;       // +3394 = 1
		bArmed = false;     // +3404 = 0
	};

	// ── Свиток (925) ──
	if (ScrollIdx >= 0 && ScrollList.IsValidIndex(ScrollIdx) && !ScrollList[ScrollIdx].SameCell(Scroll))
	{
		if (FrameScrollSel) { FrameScrollSel->SetVisibility(ESlateVisibility::Collapsed); }
		Scroll.Clear(); ScrollIdx = -1;
		ResetFxAndFlags();
		RedrawCards();
	}
	if ((uint32)ScrollIdx >= (uint32)ScrollList.Num())   // эталон сравнивает БЕЗЗНАКОВО: −1 тоже «вне»
	{
		if (FrameScrollSel) { FrameScrollSel->SetVisibility(ESlateVisibility::Collapsed); }
		Scroll.Clear(); ScrollIdx = -1;
		RedrawCards();
	}
	// ── Цель (843) ──
	auto ClearAllSelection = [this]()
	{
		if (FrameItemSel) { FrameItemSel->SetVisibility(ESlateVisibility::Collapsed); }
		Target.Clear(); Scroll.Clear(); Helper97.Clear(); Helper98.Clear();
		TargetIdx = -1;
		if (FrameScrollSel) { FrameScrollSel->SetVisibility(ESlateVisibility::Collapsed); }
		if (FrameSubSel0)   { FrameSubSel0->SetVisibility(ESlateVisibility::Collapsed); }
		if (FrameSubSel1)   { FrameSubSel1->SetVisibility(ESlateVisibility::Collapsed); }
		ScrollIdx = H97Idx = H98Idx = -1;
	};
	if (TargetIdx >= 0 && GridList.IsValidIndex(TargetIdx) && !GridList[TargetIdx].SameCell(Target))
	{
		FlushPendingResultIfAny();   // до сброса цели — буферу нужна её ступень
		ClearAllSelection();
		ResetFxAndFlags();
		PanelResetAndHide();
		RedrawCards();
	}
	if ((uint32)TargetIdx >= (uint32)GridList.Num())
	{
		ClearAllSelection();
		PanelResetAndHide();
		RedrawCards();
	}
	// ── Помощник 97 (927) и 98 (929) ──
	if (H97Idx >= 0 && H97List.IsValidIndex(H97Idx) && !H97List[H97Idx].SameCell(Helper97))
	{
		if (FrameSubSel0) { FrameSubSel0->SetVisibility(ESlateVisibility::Collapsed); }
		Helper97.Clear(); H97Idx = -1;
		RedrawCards();
	}
	if ((uint32)H97Idx >= (uint32)H97List.Num())
	{
		if (FrameSubSel0) { FrameSubSel0->SetVisibility(ESlateVisibility::Collapsed); }
		Helper97.Clear(); H97Idx = -1;
		RedrawCards();
	}
	if (H98Idx >= 0 && H98List.IsValidIndex(H98Idx) && !H98List[H98Idx].SameCell(Helper98))
	{
		if (FrameSubSel1) { FrameSubSel1->SetVisibility(ESlateVisibility::Collapsed); }
		Helper98.Clear(); H98Idx = -1;
		RedrawCards();
	}
	if ((uint32)H98Idx >= (uint32)H98List.Num())
	{
		if (FrameSubSel1) { FrameSubSel1->SetVisibility(ESlateVisibility::Collapsed); }
		Helper98.Clear(); H98Idx = -1;
		RedrawCards();
	}

	// ── Гайд 20013 (тексты пишутся в КОНТРОЛ, не в ленту) ──
	int32 GuideId = 0;
	const FAsdaItemInstance* T = LiveOf(Target);
	if (!T || T->IsEmpty())
	{
		GuideId = AsdaItemUpgradeLoc::kNeedTarget;   // 110114
	}
	else
	{
		const int32 Lvl = (int32)T->GemGrade, Used = RecByte(*T, 25), Max = RecByte(*T, 26);
		const int32 Delta = Max - Used;
		if (Delta > 0) { GuideId = AsdaItemUpgradeLoc::kNeedScroll; }                          // 110115
		if (Delta == 0 && Max > Lvl) { GuideId = AsdaItemUpgradeLoc::kTriesSpent; }              // 110116
		if (Max == Lvl)
		{
			// ★ Парный тернар (закон notices-registry-lossy-on-ternary): доспех (`categoryFlag == 3`) — 110118.
			const UAsdaItemTableSubsystem* IT = GetGameInstance()->GetSubsystem<UAsdaItemTableSubsystem>();
			if (const FAsdaItemRecord* R = IT ? IT->GetItem(T->ItemCode) : nullptr)
			{
				GuideId = (R->CategoryFlag == 3) ? AsdaItemUpgradeLoc::kGuideMaxArmor : AsdaItemUpgradeLoc::kGuideMaxWeapon;
			}
		}
		// ★ [ИСПРАВЛЕНИЕ] Д-17: предел 0 (`+26 == 0`, вещь не точится) у эталона попадает в ветку `+26 == +24` —
		//   игрок читает «достиг предела… при провале сбросится/уничтожится», хотя попыток нет вовсе (баг игроков
		//   29.10, Military Gloves). Показываем ту же строку 50011, что даст клик по «Улучшить».
		if (Max == 0 && CVarHonestNoUpgradeGuide.GetValueOnGameThread() != 0) { GuideId = AsdaItemUpgradeLoc::kNotUpgradable; }
	}
	if (GuideId != 0 && FmtGuide.LastRaw != LocRaw(GuideId))
	{
		SetFmt(FmtGuide, LocRaw(GuideId));
	}
}
