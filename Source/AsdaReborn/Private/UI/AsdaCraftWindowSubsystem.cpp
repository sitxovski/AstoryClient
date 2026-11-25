// Copyright (c) 2026 @sitxovski (Discord)
// Реализация окна «Создание предмета» `ITEM_making_WIN` (JUI 76000) — список изученных рецептов.
//   Весь разбор эталона — в шапке `AsdaCraftWindowSubsystem.h` и в `Reports/craft/craft_re_report.md` §3.1/§4;
//   отчёт захода — `Reports/craft/wave0-2.md`. Структурный дамп ассета (каждый контрол) —
//   `Reports/craft/_craft_wbp_tree_main.txt` (зонд `Tools/_probe_itemupgrade_widgets.py`).

#include "UI/AsdaCraftWindowSubsystem.h"
#include "AsdaPerf.h"   // ★ R4-b (14.11.2026): области замера времени игрового потока
#include "World/AsdaAttackExit.h"   // ★ 01.11.2026: 4024/400 — выход своего из действия атаки (AsdaLeaveAttackAction)
#include "Quest/AsdaBoardQuestSubsystem.h"   // ★ 28.10.2026: гейт `sub_937640(g_pQuestCraftWnd)` — открыта доска объявлений
#include "AsdaDevConsole.h"   // ★ античит А2: ASDA_DEV_CVAR / ASDA_DEV_CONSOLE — дев-ключи и команды вне Shipping
#include "UI/AsdaUiHelpers.h"   // ★ U20: AsdaUi::BindButton
#include "UI/AsdaCraftSubsystem.h"         // модель ремесла (уровень/опыт/лимит/изученные) и протокол
#include "AsdaOutgoingGate.h"            // Р-2: RefuseDeferredStartWhenOnline — повторная проверка офлайна на отложенном старте
#include "Test/AsdaSelfTestKit.h"   // ★ 11.10.2026 (В3 S1): общий каркас самотестов (аргументы, отказ, перехват, итог, выход)
#include "AsdaCVarSave.h"                // ★ ИСПРАВЛЕНИЕ 11.10.2026 (В0, N1(д)/U8): ключи консоли самотеста — запомнить и вернуть (значение + SetBy)
#include "UI/AsdaCraftRecipeWindowSubsystem.h"   // окно состава 77000 (`CMakeItem` = `this+80` эталона), волна В3
#include "UI/AsdaJuiWindowHandle.h"        // каркас окна JUI (Ensure + зона рамки)
#include "UI/AsdaNotice.h"                 // Loc
#include "UI/AsdaIconCellGrid.h"           // иконки результата строк (общая точка)
#include "UI/AsdaItemTooltipSubsystem.h"   // тултип строки (`ShowPreviewSlotTooltip`) и подсказки 76005/76006
#include "UI/AsdaTipPanel.h"               // EAsdaTipOwner::Craft, FAsdaTipMetrics
#include "UI/AsdaWindowSubsystem.h"
#include "UI/AsdaInventorySubsystem.h"
#include "UI/AsdaExtraMenuSubsystem.h"     // самотест: вход «доп-меню команда 5»
#include "UI/AsdaTradeStateSubsystem.h"    // гейт 1: ОКНО обмена 2008 открыто (`g_bPartyInviteFlag` = `CExchange+0x2C`, ПД-2)
#include "UI/AsdaSystemOptionSubsystem.h"  // гейт 2: `objStateAllowsRender` = `IsLocalPlayerBusy`
#include "UI/AsdaSkillUseSubsystem.h"      // гейт 4: лопата в руке (`playerHasEquippedItemType10Sub2`)
#include "UI/AsdaPremiumGachaSubsystem.h"  // гейт 4: сцена гачи
#include "UI/AsdaPremiumShopSubsystem.h"   // гейт 4: элитный магазин (`helper_getField2075(g_pShopWnd)`)
#include "UI/AsdaPremiumPostSubsystem.h"   // гейт 4: элитный почтовый ящик (`g_pInventoryBagWindow`)
#include "UI/AsdaWarehouseSubsystem.h"     // гейт 4: склад (`CObject_GetField5581(g_pWarehouseWnd)`)
#include "Npc/AsdaNpcDialogSubsystem.h"    // гейт 1/шаг 5: разговор с NPC (`g_bInventoryLocked`)
#include "Login/AsdaButtonRelay.h"
#include "World/AsdaPlayerController.h"
#include "World/AsdaPlayerCharacter.h"
#include "AsdaNetSubsystem.h"
#include "AsdaSessionReset.h"
#include "AsdaBytes.h"
#include "Items/AsdaItemDbRows.h"
#include "Items/AsdaItemTableSubsystem.h"

#include "Blueprint/UserWidget.h"
#include "Blueprint/WidgetTree.h"
#include "Components/Button.h"
#include "Components/CanvasPanelSlot.h"
#include "Components/Image.h"
#include "Components/TextBlock.h"
#include "Engine/Engine.h"
#include "Engine/GameInstance.h"
#include "Engine/World.h"
#include "HAL/IConsoleManager.h"
#include "TimerManager.h"
#include "UnrealClient.h"             // FScreenshotRequest — именованный кадр самотеста
// ★ 08.11.2026 (пачка 19): полная остановка своего персонажа из клика UI (World/AsdaUiActionMoveHold.cpp, ключ `asda.Move.UiActionStop`).
void AsdaFullStopSelfForUiAction(class UGameInstance* GI, const TCHAR* Why);


DEFINE_LOG_CATEGORY_STATIC(LogAsdaCraftWin, Log, All);

// ═════════════════════════════ КЛЮЧИ ОТКАТА ИСПРАВЛЕНИЙ ═════════════════════════════════════════

// ★ К-2. Листание (`shopGridWndProc` 0x12932/0x12933, shop_trade.c:22145-22176) не снимает выбранную строку [5]
//   и не прячет рамку, а «Удалить инструкцию» шлёт 6600 с id записи СТРОКИ ТЕКУЩЕЙ страницы `[[5]+10]`
//   (:22180) — после листания удаляется ЧУЖОЙ рецепт (необратимо, без подтверждения), а на странице с меньшим
//   числом строк `[10+row] = 0` ⇒ `*(WORD*)0` ⇒ вылет клиента.
static ASDA_DEV_CVAR(int32, CVarCraftFixDeleteAfterPaging,
	TEXT("asda.Craft.FixDeleteAfterPaging"), 1,
	TEXT("Дефект оригинала К-2: 1 = листание снимает выбор (как смена вкладки), удаление шлёт рецепт, показанный "
	     "в окне состава; 0 = как оригинал — выбор переживает листание, удаляется строка текущей страницы "
	     "(пустая строка в оригинале = вылет; у нас — отказ с записью в лог)."),
	ECVF_Default);

// ★ Дефект оригинала, найденный волной В2 (адрес): кнопки ремесла 30009/30088 живут ВНУТРИ разговора с NPC, а первый
//   гейт `tileInteractGateAndOpen` отказывает по флагу «идёт разговор» (`g_bInventoryLocked` = `g_pNpcWnd`+0x400C)
//   с чатом 5591 — вход через NPC в оригинале не работает никогда.
//   ★★ ВЕТКА НЕДОСТИЖИМА В ТЕКУЩИХ ДАННЫХ (ревизия В0–В2 С-2, перепроверено В3): команд 30009/30010/30088 нет ни в
//   одном сценарии NPC порта (`NpcNsrButton.json`: 72 сценария, 158 кнопок; `NpcNsrPage/Header/QuestScript` — 0) и ни
//   в одном россыпном `.NSR` клиентов. Дефект латентный; ключ оставлен на случай появления кнопки в данных.
static ASDA_DEV_CVAR(int32, CVarCraftFixNpcEntry,
	TEXT("asda.Craft.FixNpcEntry"), 1,
	TEXT("Дефект оригинала (вход через NPC 30009/30088): 1 = разговор с NPC закрывается ДО гейтов, окно 76000 "
	     "открывается; 0 = как оригинал — гейт видит открытый разговор и отвечает чатом 5591."),
	ECVF_Default);

// ★ К-4 (данные). Строк 115300/115301 нет в `error list.bin` клиента; `StringTable_GetById@0x111F4C0` отдаёт для
//   отсутствующего id строку ИНДЕКСА 99 — id 1005 «Ты ранишь монстра.».
//   ★★ РЕШЕНИЕ ВЛАДЕЛЬЦА 26.09.2026: строки ВНЕСЕНЫ в порт под их родными номерами — источник клиент AsdaFableClassic
//   (`NEW/AsdaFableClassic/system/error list.bin`, формат «Ver2.0»: заголовок 256 б, u32 count = 6233, записи
//   {u32 id, u32 len, байты −100 cp1251}): 115300 «Внесенные инструкции/максимальное кол-во инструкций», 115301 «Опыт
//   ремесла». Место — `RawAssets/Text/GameText.csv` (конец файла) → `DT_GameText` (`Tools/import_gametext.py`); CSV —
//   единственный источник таблицы, перегенерации его никто не делает. Отчёт `Reports/craft/craft_owner_items.md` §1.
//   Своего ключа показ не требует: эталон зовёт `Loc(115300|115301)`, строка есть — показывается она, фолбэк индекса 99
//   ниже (`TickTooltip`) остаётся только на случай её отсутствия (как оригинал).
//   ★ Волна В3 (ревизия В0–В2 М-7): прежний ключ `asda.Craft.MissingTipBody` УБРАН — его значение 1 («пустое тело»)
//   не было исправлением, а только другой вид дефекта.

namespace
{
	const FName  kWindowId(UAsdaCraftSubsystem::kMainWindowId);
	const TCHAR* kWindowWbp = TEXT("/Game/AsdaAssets/UI/Player_UI/Craft/WBP_ITEM_making_WIN.WBP_ITEM_making_WIN_C");

	// Виды наведения (подсказки).
	enum : int32 { kTipRow = 0, kTipExp = 1, kTipLimit = 2 };

	// Рамка 76077: исходная точка `.JUI` (Origin 13 24) — код ставит `SetField74_75(13, 37*row+23)` в координатах
	//   ТОГО ЖЕ родителя; в ассете у группы своя исходная точка (L13 T29) ⇒ смещение = база ассета − Origin JUI.
	constexpr float kFrameJuiX = 13.f;
	constexpr float kFrameJuiY = 24.f;
	constexpr float kFrameCodeX = 13.f;          // `hiliteX = 13`
	constexpr float kFrameCodeY0 = 23.f;         // `hiliteY = 37 * row + 23`
	constexpr float kFrameStep = 37.f;

	// Рекурсивно «строка N» из имени вида `ITEM_making_BOX_L<N>_…` (1…6). −1 — не строка.
	int32 RowOfIdc(const FString& Idc)
	{
		static const FString P(TEXT("ITEM_making_BOX_L"));
		if (!Idc.StartsWith(P) || Idc.Len() <= P.Len()) { return -1; }
		const TCHAR C = Idc[P.Len()];
		if (C < TEXT('1') || C > TEXT('6')) { return -1; }
		if (Idc.Len() > P.Len() + 1 && Idc[P.Len() + 1] != TEXT('_')) { return -1; }
		return (int32)(C - TEXT('1'));
	}

	// ★ Ревизия В3 МЕЛК-11: помощники — ОБЩИЕ точки `AsdaUi::SetTextIf/SetShownDecorAll/AnyShown`, здесь только имена.
	void SetTxt(UTextBlock* T, const FString& S) { AsdaUi::SetTextIf(T, S); }
	void SetGroupShown(const TArray<TObjectPtr<UWidget>>& G, bool bShown) { AsdaUi::SetShownDecorAll(G, bShown); }
	bool GroupShown(const TArray<TObjectPtr<UWidget>>& G) { return AsdaUi::AnyShown(G); }
}

// ★ Ревизия В3 С-4 (волна В4). `uiCraftPanel_RefreshSummary@0xFDE4A0` (ui_core.c:100706-100714) — сводка 76000 после каждой
//   изготовленной штуки 77500 — перерисовывает затемнение ВЫБРАННОЙ строки условием «материалы есть ИЛИ уровень ВЫШЕ ⇒
//   спрятать, иначе показать», т. е. затемняет только строку «не хватает И уровень достаточен» (инверсия, как Д-В3-1).
//   Обычная перерисовка `uiList_RefreshItemRows6` (:100803) верна: «не хватает ИЛИ уровень выше».
static ASDA_DEV_CVAR(int32, CVarCraftFixSummaryRowDarken,
	TEXT("asda.Craft.FixSummaryRowDarken"), 1,
	TEXT("Дефект оригинала (сводка 76000 после изготовления): 1 = выбранная строка затемняется «материалов не хватает ИЛИ "
	     "уровень ремесла ниже нужного», как при обычной перерисовке; 0 = как оригинал — «не хватает И уровень достаточен»."),
	ECVF_Default);

// ═════════════════════════════ ЖИЗНЕННЫЙ ЦИКЛ ═════════════════════════════════════════════════

void UAsdaCraftWindowSubsystem::Initialize(FSubsystemCollectionBase& Collection)
{
	Super::Initialize(Collection);
	Collection.InitializeDependency(UAsdaWindowSubsystem::StaticClass());
	Collection.InitializeDependency(UAsdaCraftSubsystem::StaticClass());
	RowRecipe.Init(-1, kRows);
	UGameInstance* GI = GetGameInstance();
	if (UAsdaWindowSubsystem* WM = GI ? GI->GetSubsystem<UAsdaWindowSubsystem>() : nullptr)
	{
		// ★ `uiCloseAllWindows@0xD61960` (ui_core.c:54383-54384): `if (видно 76000) uiWindowHideAndCleanup`.
		//   ESC-закрывателя НЕТ: флага `CControl+0x228` в init окна нет (ESC окно не закрывает, как оригинал).
		WM->RegisterWindowCloser(kWindowId, FSimpleDelegate::CreateWeakLambda(this, [this]() { if (bOpen) { HideAndCleanup(); } }));
	}
	if (UAsdaCraftSubsystem* Craft = GI ? GI->GetSubsystem<UAsdaCraftSubsystem>() : nullptr)
	{
		HModel = Craft->OnModelChanged.AddUObject(this, &UAsdaCraftWindowSubsystem::OnModelChanged);
	}
	// ★ Волна В8 (ревизия В7 МЕЛК-4): своей подписки на смерть больше нет — строку entity_char.c:16908-16909 исполняет блок
	//   смерти диалога NPC (`UAsdaNpcDialogSubsystem::Tick` → `CloseAfterSelfDeath`); место относительно 5045 — ключ К-9.
	UE_LOG(LogAsdaCraftWin, Log, TEXT("[ремесло-окно] подсистема окна 76000 поднята"));
}

void UAsdaCraftWindowSubsystem::Deinitialize()
{
	UGameInstance* GI = GetGameInstance();
	if (UAsdaCraftSubsystem* Craft = GI ? GI->GetSubsystem<UAsdaCraftSubsystem>() : nullptr) { Craft->OnModelChanged.Remove(HModel); }
	ResetWidget();
	Super::Deinitialize();
}

void UAsdaCraftWindowSubsystem::ResetSessionState(EAsdaSessionResetReason Reason)
{
	// Окно — визуальный слой сессии: прячется при любом сбросе. Вкладка [4] — поле объекта окна, в оригинале
	//   живёт до перезапуска клиента (ctor = 4): при полном сбросе (другой персонаж) — к умолчанию.
	if (bOpen) { HideAndCleanup(); }
	if (AsdaIsFullSessionReset(Reason))
	{
		Tab = kTabAll;
		Page = 1;
		Pages = 1;
		SelRow = -1;
		Filtered.Reset();
		RowRecipe.Init(-1, kRows);
	}
	TipKind = TipIdx = -1;
	Mouse.Reset();
}

void UAsdaCraftWindowSubsystem::Tick(float /*DeltaTime*/)
{
	ASDA_PERF_SCOPE("Tick.CraftWindow");   // ★ R4-b (14.11.2026): область замера (AsdaPerf.h; в Shipping пусто)
	if (!bOpen || !bControlsReady) { return; }
	Mouse.Update();
	// Модальный гейт (П8-9): под модальным нотисом эталон мышь окнам ниже не отдаёт.
	if (AsdaUi::IsModalDispatchBlocked(GetGameInstance()))
	{
		TickTooltip(-1, -1);
		HideOver();
		Mouse.Reset();
		return;
	}
	TickMouse();
}

void UAsdaCraftWindowSubsystem::CloseAfterSelfDeath(bool bSnapshotTo5045)
{
	// ★ Смерть своего персонажа (`CCharacter_SetAction`, entity_char.c:16908-16909): `if (tileInteract_isControlVisible(
	//   g_pItemMainWnd)) uiWindowHideAndCleanup`. Зовёт блок смерти диалога NPC из своего `Tick` (не колбэк сети).
	if (!bOpen) { return; }
	// ★ W8-2 (исправление К-9): смерть при открытом 77000 без перехода в 77500. У эталона 77000 открыто только вместе с
	//   76000 (открывает его строка 76000 — shop_trade.c:22086; закрывает `uiWindowHideAndCleanup` 76000 — ui_core.c:100623),
	//   поэтому 5045 (16900) восстанавливает снимок при ВИДИМОМ 76000, и открыватели окон снимка молча отказывают по гейту
	//   «ремесло открыто» (сумка ui_core.c:46608, персонаж :74270, квест-гид :33571, навыки skill_buff.c:32749, сообщество
	//   social.c:3787), после чего флаги обнуляются (ui_core.c:60967) — окна потеряны. Исправление К-9 закрывает окна ремесла
	//   ДО 5045, но закрытие 77000 (`itemTooltipWndReset`) по К-7 сняло бы флаги владельца `Craft` — и 5045 снова ничего бы
	//   не вернул. На время этого закрытия снимок удерживается: его сразу отдаёт 5045. При ключе 0 — строго эталон.
	UAsdaCraftRecipeWindowSubsystem* Rw = bSnapshotTo5045 ? RecipeWin() : nullptr;
	if (Rw) { Rw->SetHoldSnapshotForDeathRestore(true); }
	HideAndCleanup();
	if (Rw) { Rw->SetHoldSnapshotForDeathRestore(false); }
	JournalAdd(bSnapshotTo5045 ? TEXT("смерть — окно закрыто до 5045, снимок 77000 отдан 5045 (К-9, W8-2)")
	                           : TEXT("смерть — окно закрыто"));
}

void UAsdaCraftWindowSubsystem::OnModelChanged(int32 Opcode)
{
	// 6599/6601: `craftPanelSelectItemSlot` / `craftRemoveSelectedItemSlot` пересобирают страницу и сводку (окно
	//   затем закрывается приёмником); 6605: `uiCraftPanelRefreshSummaryGated`. 6597/6603 окно 76000 не трогают.
	if (!bControlsReady) { return; }
	if (Opcode == 6599 || Opcode == 6601)
	{
		BuildFilteredPage(Tab);
		RefreshSummaryGated();
	}
	else if (Opcode == 6605)
	{
		RefreshSummaryGated();
	}
}

// ═════════════════════════════ ОКНО ═══════════════════════════════════════════════════════════

bool UAsdaCraftWindowSubsystem::EnsureWidget()
{
	const EAsdaJuiEnsure R = FAsdaJuiWindowHandle(kWindowId, kWindowWbp, kWinZ).Ensure(GetGameInstance(), Widget, bControlsReady,
		[this](UAsdaWindowSubsystem&) { ResetWidget(); },
		[this](UAsdaWindowSubsystem&) { SetupControls(); RegisterZones(); },
		[]() { UE_LOG(LogAsdaCraftWin, Error, TEXT("[ремесло-окно] WBP_ITEM_making_WIN не загрузился (%s)"), kWindowWbp); });
	return FAsdaJuiWindowHandle::Succeeded(R) && bControlsReady;
}

void UAsdaCraftWindowSubsystem::ResetWidget()
{
	UGameInstance* GI = GetGameInstance();
	if (UAsdaWindowSubsystem* WM = GI ? GI->GetSubsystem<UAsdaWindowSubsystem>() : nullptr)
	{
		WM->UnregisterUiZone(kWindowId);
		WM->CloseWindow(kWindowId);
	}
	Widget = nullptr;
	bControlsReady = false;
	bOpen = false;
	TxtTitle = TxtDelete = TxtClose = TxtCraftLbl = TxtLvlLbl = TxtLevel = TxtRecipeNum = TxtPage = TxtPages = nullptr;
	DragHandle = Frame = nullptr;
	BtnX = BtnDelete = BtnClose = BtnPrev = BtnNext = nullptr;
	Bar = FAsdaMiniBarFill();
	TabPlanes.Reset(); TabButtons.Reset(); TabCaptions.Reset();
	TabIdlePlanes.Reset(); TabIdleStyles.Reset(); TabIdleCaptions.Reset();
	ColHeads.Reset();
	Rows.Reset();
	MarkGrid = nullptr;
	Relays.Reset();
	FoundControls = 0;
	Mouse.Reset();
}

void UAsdaCraftWindowSubsystem::RegisterZones()
{
	UAsdaWindowSubsystem* WM = GetGameInstance() ? GetGameInstance()->GetSubsystem<UAsdaWindowSubsystem>() : nullptr;
	if (!WM) { return; }
	// Рамка окна — девять планов `ITEM_making_WIN_0_0…8` (скин `Black_BottomRound_White2Border`).
	FAsdaJuiWindowHandle::RegisterFrameZone(*WM, kWindowId, this, Widget, bOpen, TEXT("ITEM_making_WIN_0_%d"), 9);
}

void UAsdaCraftWindowSubsystem::SetupControls()
{
	bControlsReady = false;
	if (!Widget || !Widget->WidgetTree) { return; }
	Rows.SetNum(kRows);
	for (FAsdaCraftRowUi& R : Rows) { R.T.SetNum(4); R.Hit.Reset(); R.Black.Reset(); R.Over.Reset(); R.Mark = nullptr; R.MarkText = nullptr; }
	TabPlanes.SetNum(kTabs); TabButtons.SetNum(kTabs); TabCaptions.SetNum(kTabs); ColHeads.SetNum(5);
	TArray<FName> MarkNames; MarkNames.SetNum(kRows);

	TArray<UWidget*> All;
	Widget->WidgetTree->GetAllWidgets(All);
	for (UWidget* W : All)
	{
		if (!W) { continue; }
		const FString N = W->GetName();
		if (UButton* Btn = Cast<UButton>(W))
		{
			if (N == TEXT("ITEM_making_XX"))          { BtnX = Btn; continue; }        // 76099
			if (N == TEXT("ITEM_making_BOX_delbtn"))  { BtnDelete = Btn; continue; }   // 76096
			if (N == TEXT("ITEM_making_BOX_Xbtn"))    { BtnClose = Btn; continue; }    // 76097
			if (N == TEXT("ITEM_making_BOX_Alist_L")) { BtnPrev = Btn; continue; }     // 76082
			if (N == TEXT("ITEM_making_BOX_Alist_R")) { BtnNext = Btn; continue; }     // 76083
			for (int32 K = 0; K < kTabs; ++K)
			{
				if (N == FString::Printf(TEXT("ITEM_making_Radiobtn_OPT%d"), K)) { TabButtons[K] = Btn; }   // 76007
			}
			continue;
		}
		if (UTextBlock* Tb = Cast<UTextBlock>(W))
		{
			if (N == TEXT("ITEM_making_movetext"))       { TxtTitle = Tb; continue; }      // 76001
			if (N == TEXT("ITEM_making_BOX_delbtn_Text")){ TxtDelete = Tb; continue; }     // подпись 76096
			if (N == TEXT("ITEM_making_BOX_Xbtn_Text"))  { TxtClose = Tb; continue; }      // подпись 76097
			if (N == TEXT("ITEM_making_m_t1"))           { TxtCraftLbl = Tb; continue; }   // 76002
			if (N == TEXT("ITEM_making_m_t2"))           { TxtLvlLbl = Tb; continue; }     // 76003
			if (N == TEXT("ITEM_making_m_t3"))           { TxtLevel = Tb; continue; }      // 76004
			if (N == TEXT("ITEM_making_m_t4"))           { TxtRecipeNum = Tb; continue; }  // 76006
			if (N == TEXT("ITEM_making_BOX_tlist_L"))    { TxtPage = Tb; continue; }       // 76080
			if (N == TEXT("ITEM_making_BOX_tlist_R"))    { TxtPages = Tb; continue; }      // 76081
			if (N == TEXT("ITEM_making_Radiobtn_Text"))  { TabCaptions[0] = Tb; continue; }
			for (int32 K = 1; K < kTabs; ++K)
			{
				if (N == FString::Printf(TEXT("ITEM_making_Radiobtn_Text%d"), K + 1)) { TabCaptions[K] = Tb; }
			}
			for (int32 K = 0; K < 5; ++K)
			{
				if (N == FString::Printf(TEXT("ITEM_making_BOX_T%d_Text"), K + 1)) { ColHeads[K] = Tb; }   // 76011…76015
			}
			const int32 Row = RowOfIdc(N);
			if (Row >= 0)
			{
				for (int32 K = 0; K < 4; ++K)
				{
					if (N == FString::Printf(TEXT("ITEM_making_BOX_L%d_t%d"), Row + 1, K + 1)) { Rows[Row].T[K] = Tb; }
				}
				if (N == FString::Printf(TEXT("ITEM_making_BOX_L%d_mark_%d"), Row + 1, Row + 1)) { Rows[Row].MarkText = Tb; }
				if (N == FString::Printf(TEXT("ITEM_making_BOX_L%d_black"), Row + 1)) { Rows[Row].Black.Add(Tb); }
			}
			continue;
		}
		if (N == TEXT("ITEM_making_BOX_select_grp"))   // 76077 — группа рамки (8 планов + `Slot_AniSelect`)
		{
			Frame = W;
			FVector2D P, S;
			if (AsdaUi::SlotRect(W, P, S)) { FrameBase = P; }
			continue;
		}
		if (N.StartsWith(TEXT("ITEM_making_movetext")) && N.EndsWith(TEXT("_DRAG"))) { DragHandle = W; continue; }

		// Планы `<idc>_<juiIdx>_<plane>`.
		FString Idc; int32 JuiIdx = 0, Plane = 0;
		if (!AsdaUi::ParsePlaneName(N, Idc, JuiIdx, Plane)) { continue; }
		if (Idc == TEXT("ITEM_making_m_gbar"))   // 76005 `Yello_Bar`: ES0 подложка / ES1 заливка
		{
			if (Plane == 0) { Bar.Back = Cast<UImage>(W); }
			if (Plane == 1) { Bar.Fill = Cast<UImage>(W); }
			continue;
		}
		if (Idc == TEXT("ITEM_making_Radiobtn") && Plane >= 0 && Plane < kTabs) { TabPlanes[Plane] = Cast<UImage>(W); continue; }
		const int32 Row = RowOfIdc(Idc);
		if (Row < 0) { continue; }
		if (Idc == FString::Printf(TEXT("ITEM_making_BOX_L%d_mark_%d"), Row + 1, Row + 1) && Plane == 0)
		{
			Rows[Row].Mark = Cast<UImage>(W);
			MarkNames[Row] = FName(*N);
			Rows[Row].Hit.Add(W);
			continue;
		}
		if (Idc == FString::Printf(TEXT("ITEM_making_BOX_L%d_black"), Row + 1)) { Rows[Row].Black.Add(W); continue; }
		if (Idc == FString::Printf(TEXT("ITEM_making_BOX_L%d_over"), Row + 1))  { Rows[Row].Over.Add(W); continue; }
		for (int32 K = 0; K < 4; ++K)
		{
			if (Idc == FString::Printf(TEXT("ITEM_making_BOX_L%d_t%d"), Row + 1, K + 1)) { Rows[Row].Hit.Add(W); }
		}
	}

	// ── Полоса 76005: полная ширина = ширина подложки ES0 (86 = `Size` из `.JUI`). ★ Ширину берём у ПОДЛОЖКИ, а
	//    не у заливки: у плана ES1 в ассете ширина 57 — образец значения из импорта, не 100 %. ──
	{
		FVector2D P, S;
		if (Bar.Fill && UAsdaMiniStateSubsystem::GetSlotRect(Bar.Fill, P, S)) { Bar.BaseLeft = P.X; Bar.FullW = S.X; }
		if (Bar.Back && UAsdaMiniStateSubsystem::GetSlotRect(Bar.Back, P, S)) { Bar.FullW = S.X; }
	}

	// ── Иконки результата строк — ОБЩАЯ точка сеток иконок (пустая строка — пустая текстура, как эталон). ──
	MarkGrid = NewObject<UAsdaIconCellGrid>(this);
	MarkGrid->SetCellSizePx(32.f);
	MarkGrid->SetClickOverlayEnabled(false);
	MarkGrid->bClearEmptyCells = true;   // `CControl_DrawTextureRect(mark, пустая текстура, 0,0,32,32)`
	// ★ Волна В4: атлас пола персонажа — эталон рисует иконки строк через `g_ItemImgOptionVariant` (ui_core.c:100787).
	const UAsdaNetSubsystem* NetG = GetGameInstance() ? GetGameInstance()->GetSubsystem<UAsdaNetSubsystem>() : nullptr;
	MarkGrid->SetupNamed(Widget, MarkNames, /*bMale*/ !NetG || NetG->ResolveSelfGender() != 2, [this](int32 C) -> int32
	{
		const UAsdaCraftSubsystem* Craft = GetGameInstance() ? GetGameInstance()->GetSubsystem<UAsdaCraftSubsystem>() : nullptr;
		const FAsdaMakingRecipeRow* R = (Craft && RowRecipe.IsValidIndex(C) && RowRecipe[C] >= 0) ? Craft->FindRecipe(RowRecipe[C]) : nullptr;
		return R ? FMath::Max(0, R->ResultCode) : 0;
	}, [](int32) {});

	// ── Кнопки (ветка msgType 66561 `shopGridWndProc`; своих звуков у окна нет — §6.3 отчёта) ──
	auto Bind = [this](UButton* Btn, int32 IdNum, TFunction<void()>&& Fn)
	{
		if (!Btn) { return; }
		AsdaUi::BindButton(this, Btn, IdNum, [F = MoveTemp(Fn)](int32) { F(); }, Relays);   // ★ U20: реле клика — общая точка
	};
	Bind(BtnPrev,   76082, [this]() { OnPrevPage(); });
	Bind(BtnNext,   76083, [this]() { OnNextPage(); });
	Bind(BtnDelete, 76096, [this]() { OnDeleteClicked(); });
	Bind(BtnClose,  76097, [this]() { OnCloseClicked(); });
	Bind(BtnX,      76099, [this]() { OnCloseClicked(); });
	for (int32 K = 0; K < kTabs; ++K)
	{
		Bind(TabButtons[K], 76007, [this, K]() { OnTabSelected(K); });   // msg 66817 с `itemCategory = K`
	}

	// ── Статичные подписи и начальные значения — `CItemMainWnd_InitControls` (item_inventory.c:69332-69505) ──
	SetTxt(TxtTitle,    Loc(AsdaCraftLoc::kTitle));        // 76001 = 5500
	SetTxt(TxtDelete,   Loc(AsdaCraftLoc::kBtnDelete));    // 76096 = 5522 (подпись ассета «Выбрать» — образец)
	SetTxt(TxtClose,    Loc(AsdaCraftLoc::kBtnClose));     // 76097 = 5035
	AsdaUi::SetShownDecor(Frame, false);                    // 76077 `SetVisible(0)` (+ `helper_setByte295(…, 1)`)
	SetTxt(TxtCraftLbl, Loc(AsdaCraftLoc::kCraftLvl));      // 76002 = 5501
	SetTxt(TxtLvlLbl,   Loc(AsdaCraftLoc::kLvlShort));      // 76003 = 5502
	SetTxt(TxtLevel,    TEXT("0"));                         // 76004 = "0"
	if (Bar.IsValid()) { UAsdaMiniStateSubsystem::ClipFill(Bar, 0.f); BarFrac = 0.f; }   // 76005: [219] = [220] = 0 при init
	SetTxt(TxtRecipeNum, TEXT("0/0"));                      // 76006 = "0/0"
	for (int32 K = 0; K < 4; ++K) { SetTxt(TabCaptions[K], Loc(AsdaCraftLoc::kTabBase + K)); }   // 5540…5543
	SetTxt(TabCaptions[4], Loc(AsdaCraftLoc::kTabAll));     // 5328 «ВСЕ»
	for (int32 K = 0; K < 5; ++K) { SetTxt(ColHeads[K], Loc(AsdaCraftLoc::kColList + K)); }   // 5510…5514
	for (FAsdaCraftRowUi& R : Rows)
	{
		SetTxt(R.MarkText, FString());                        // `CControl_SetText(…, &Src)` пятью контролами строки
		for (UTextBlock* T : R.T) { SetTxt(T, FString()); }
		SetGroupShown(R.Over, false);
	}
	SetTxt(TxtPage,  TEXT("1"));                            // 76080 = "1"
	SetTxt(TxtPages, TEXT("1"));                            // 76081 = "1"

	// ── Итог разбора: все 63 узла группы 760 ──
	int32 Found = Widget ? 1 : 0;   // 76000 — сам корень
	auto Has = [&Found](const UObject* O) { if (O) { ++Found; } };
	Has(TxtTitle); Has(BtnX); Has(BtnDelete); Has(BtnClose); Has(Frame); Has(TxtCraftLbl); Has(TxtLvlLbl); Has(TxtLevel);
	Has(Bar.Fill); Has(TxtRecipeNum);
	{
		bool bTabsOk = true;
		for (int32 K = 0; K < kTabs; ++K) { bTabsOk &= (TabButtons[K] != nullptr && TabPlanes[K] != nullptr && TabCaptions[K] != nullptr); }
		if (bTabsOk) { ++Found; }   // 76007 — один контрол `RadioButton` на пять кнопок
	}
	for (UTextBlock* T : ColHeads) { Has(T); }
	for (const FAsdaCraftRowUi& R : Rows)
	{
		Has(R.Mark); for (UTextBlock* T : R.T) { Has(T); }
		if (R.Black.Num() > 0) { ++Found; }
		if (R.Over.Num() > 0) { ++Found; }
	}
	Has(TxtPage); Has(TxtPages); Has(BtnPrev); Has(BtnNext);
	FoundControls = Found;
	bControlsReady = (Rows[0].Mark != nullptr && TxtRecipeNum != nullptr && BtnDelete != nullptr && Frame != nullptr);
	UE_LOG(LogAsdaCraftWin, Log, TEXT("%s"), *DumpControls());
}

// ═════════════════════════════ ВХОДЫ И ГЕЙТЫ ══════════════════════════════════════════════════

bool UAsdaCraftWindowSubsystem::GateAndToggle(const TCHAR* Source)
{
	UGameInstance* GI = GetGameInstance();
	if (!GI) { return false; }
	// ── 1. `uiCheckPartyInviteInvLockGuard@0xFDD0F0` (ui_core.c:100482-100502) ──
	UAsdaCraftSubsystem* Craft = GI->GetSubsystem<UAsdaCraftSubsystem>();
	UAsdaNpcDialogSubsystem* Dlg = GI->GetSubsystem<UAsdaNpcDialogSubsystem>();
	// ★ ПД-2 (29.09.2026, `Reports/trade/trade_re_report.md` §1.1.8/§2.3). `g_bPartyInviteFlag` — МИСНЕЙМ: это
	//   `CExchange+0x2C`, «ОКНО обмена 2008 открыто» (писатель `mov [eax+0x2C],1` @0xC75A72 в `uiWindow_OpenWithGuards`).
	//   Прежний порт (ревизия В0–В2 М-5) читал здесь «заявка в полёте» (`IsExchangeRequestActive`) — игрок с неотвеченной
	//   заявкой (до 33 с) получал 5590 и не открывал ремесло; у эталона на стадии заявки гейт молчит. Теперь — общая
	//   точка `UAsdaTradeStateSubsystem::IsExchangeWindowOpen` (пока окна 2008 нет — false).
	if (UAsdaTradeStateSubsystem::IsExchangeWindowOpen(GI))
	{
		if (Craft) { Craft->NoticeGateChat(AsdaCraftLoc::kGateExchange); }   // 5590
		JournalAdd(FString::Printf(TEXT("гейт 5590 (обмен) — %s"), Source));
		return false;
	}
	if (Dlg && Dlg->IsDialogOpen())   // `g_bInventoryLocked` = разговор с NPC (`g_pNpcWnd`+0x400C)
	{
		if (Craft) { Craft->NoticeGateChat(AsdaCraftLoc::kGateDialog); }     // 5591
		JournalAdd(FString::Printf(TEXT("гейт 5591 (разговор с NPC) — %s"), Source));
		return false;
	}
	// ── 2. `objStateAllowsRender(g_LocalPlayer)` (миснейм: «персонаж занят») → молча ──
	if (const UAsdaSystemOptionSubsystem* SO = GI->GetSubsystem<UAsdaSystemOptionSubsystem>())
	{
		if (SO->IsLocalPlayerBusy()) { JournalAdd(FString::Printf(TEXT("гейт «персонаж занят» — %s"), Source)); return false; }
	}
	// ── 3. окно видно → закрыть (тумблер) ──
	if (bOpen)
	{
		HideAndCleanup();
		JournalAdd(FString::Printf(TEXT("закрыто тумблером — %s"), Source));
		return true;
	}
	// ── 4. молчаливые отказы (ui_core.c:100527-100566), строго по порядку эталона ──
	const UAsdaWindowSubsystem* WM = GI->GetSubsystem<UAsdaWindowSubsystem>();
	auto Silent = [this, Source](const TCHAR* Why) { JournalAdd(FString::Printf(TEXT("молча: %s — %s"), Why, Source)); return false; };
	//   `sub_FDD4D0(g_pRefineShop) == 1` — видна лавка заточки `Smelting2_Win` 17000 (в порту окна нет — ложно).
	//   ⚠ Ревизия В0–В2 М-6: эталон читает не видимость, а байт +9 объекта `0x2C37908` (`sub_93BED4`, см.
	//   `AsdaUiHelpers.h`); при переносе окна 17000 заменить проверку на это поле.
	if (WM && WM->IsWindowVisible(FName(TEXT("Smelting2_Win")))) { return Silent(TEXT("лавка заточки 17000")); }
	//   `g_bInventoryLocked` — повтор проверки разговора (выше уже отсеян).
	if (Dlg && Dlg->IsDialogOpen()) { return Silent(TEXT("разговор с NPC")); }
	//   `helper_getField2075(g_pShopWnd)` — открыт ЭЛИТНЫЙ МАГАЗИН (Shell Item Store, поле 2075; разбор NPC-порта).
	if (const UAsdaPremiumShopSubsystem* PS = GI->GetSubsystem<UAsdaPremiumShopSubsystem>()) { if (PS->IsOpen()) { return Silent(TEXT("элитный магазин 6000")); } }
	//   `playerHasEquippedItemType10Sub2` — в руке авто-бур (лопата).
	if (const UAsdaSkillUseSubsystem* SU = GI->GetSubsystem<UAsdaSkillUseSubsystem>()) { if (SU->IsAutoShovelEquipped()) { return Silent(TEXT("лопата в руке")); } }
	//   `sub_937640(g_pQuestCraftWnd)` — ★ 28.10.2026: открыта ДОСКА ОБЪЯВЛЕНИЙ 75000 (`NPC_SpecialAction_Type5`; миснейм IDA
	//   «квест-обмен/ремесло») → молча.
	if (UAsdaBoardQuestSubsystem::IsBoardOpen(GI)) { return Silent(TEXT("доска объявлений 75000")); }
	//   ★ Волна Л5 торговли (30.09.2026): `sub_934E86(g_pSellWindow)` — окно ПОДГОТОВКИ личной лавки 22000 (`CSellWindow+0x300`);
	//   `sub_929DC4(g_bSomeFeatureEnabled)` = `CShopDlg+0x168` — ВИТРИНА лавки 23000 (пишут `shopDlgOpenSellWindow` → 1, `shopDlgShow` → 0;
	//   прежнее «смысл не разобран» устарело). Клиентский гейт парный к серверному P35 С-13 (ремесло в лавке сервер отвергает).
	if (UAsdaTradeStateSubsystem::IsSellWindowOpen(GI)) { return Silent(TEXT("подготовка личной лавки 22000")); }
	if (UAsdaTradeStateSubsystem::IsShopDlgOpen(GI))    { return Silent(TEXT("витрина личной лавки 23000")); }
	//   `sub_922D5D(g_pGachaScene)` — сцена премиум-гачи.
	if (const UAsdaPremiumGachaSubsystem* PG = GI->GetSubsystem<UAsdaPremiumGachaSubsystem>()) { if (PG->IsSceneOpen()) { return Silent(TEXT("сцена гачи")); } }
	//   `sub_9384DC(g_pInventoryBagWindow) == 1` — элитный почтовый ящик 69000.
	if (const UAsdaPremiumPostSubsystem* PP = GI->GetSubsystem<UAsdaPremiumPostSubsystem>()) { if (PP->IsWindowVisible()) { return Silent(TEXT("элитный почтовый ящик 69000")); } }
	//   `CObject_GetField5581(g_pWarehouseWnd)` — свиток возврата склада (★ 05.11.2026: было «окно 4000 открыто», `asda.Warehouse.GateRecallFlag`).
	if (const UAsdaWarehouseSubsystem* WH = GI->GetSubsystem<UAsdaWarehouseSubsystem>()) { if (WH->IsGateField5581()) { return Silent(TEXT("свиток склада")); } }
	// ── 5. открыть ──
	OpenAfterGates(Source);
	return bOpen;
}

bool UAsdaCraftWindowSubsystem::GateAndToggleFromNpc(int32 NpcCommand)
{
	UAsdaNpcDialogSubsystem* Dlg = GetGameInstance() ? GetGameInstance()->GetSubsystem<UAsdaNpcDialogSubsystem>() : nullptr;
	if (CVarCraftFixNpcEntry.GetValueOnGameThread() != 0 && Dlg && Dlg->IsDialogOpen())
	{
		// ★ Исправление входа через NPC (см. ключ): разговор закрывается ДО гейта 5591. Точка — общий `Close()`
		//   (прецедент 30035); отступление от `dialogOnConfirmClose` описано у ветки в `AsdaNpcDialogSubsystem.cpp`.
		Dlg->Close();
		JournalAdd(FString::Printf(ASDA_DEV_TEXT(TEXT("NPC %d: разговор закрыт до гейтов (asda.Craft.FixNpcEntry 1)"), TEXT("NPC %d: разговор закрыт до гейтов")), NpcCommand));
	}
	return GateAndToggle(*FString::Printf(TEXT("диалог NPC %d"), NpcCommand));
}

void UAsdaCraftWindowSubsystem::OpenAfterGates(const TCHAR* Source)
{
	UGameInstance* GI = GetGameInstance();
	if (!EnsureWidget()) { return; }
	UAsdaWindowSubsystem* WM = GI->GetSubsystem<UAsdaWindowSubsystem>();
	UAsdaCraftSubsystem* Craft = GI->GetSubsystem<UAsdaCraftSubsystem>();
	if (!WM || !Craft) { return; }
	// `dialogOnConfirmClose(g_pNpcWnd, 1)` — окно разговора прячется. У нас разговор к этому моменту закрыт гейтом
	//   или исправлением входа NPC; если он всё же открыт (гонка), кончаем его той же общей точкой.
	if (UAsdaNpcDialogSubsystem* Dlg = GI->GetSubsystem<UAsdaNpcDialogSubsystem>()) { if (Dlg->IsDialogOpen()) { Dlg->Close(); } }
	// `followTargetCancelReset` + `CharFx_AutoSabzil_State(…, 0)` + `Net_SendActionPacket` (4008) + тайл игрока —
	//   та же последовательность, что у входа в сцену гачи (`premiumGacha_BeginRoll`, порт в
	//   `AsdaPremiumGachaSubsystem.cpp`): маршрут снять, авто-атаку снять, покой, стоп 4008 из клика UI (не из сети).
	// ★ 08.11.2026 [ОРИГИНАЛ] (пачка 19): `followTargetCancelReset(&g_bMapTransferActive)` снимает и автопуть, а `CharFx_AutoSabzil_State(…, 0)`
	//   выводит из хода 1 — персонаж встаёт. Порт ставил только клип покоя: павн ехал дальше. Ключ `asda.Move.UiActionStop`.
	AsdaFullStopSelfForUiAction(GI, TEXT("вход в ремесло 76000"));
	UWorld* World = GetWorld();
	AAsdaPlayerController* PC = World ? Cast<AAsdaPlayerController>(World->GetFirstPlayerController()) : nullptr;
	const AActor* StopAt = nullptr;   // чья позиция уйдёт в 4008 (свой персонаж)
	if (PC)
	{
		PC->CancelRoute();
		AsdaLeaveAttackAction(PC, TEXT("вход в ремесло"));   // ★ 01.11.2026: `CharFx_AutoSabzil_State(…, 0)` из атаки 5 — 4024/400 (entity_char.c:16545)
		PC->bAutoAttack = false;
		if (AAsdaPlayerCharacter* Pawn = Cast<AAsdaPlayerCharacter>(PC->GetPawn()))
		{
			if (!Pawn->IsIncapacitated())
			{
				// ★ 24.10.2026 (выход из сидения 1:1): `CharFx_AutoSabzil_State(…, 0)` переписывает 7/8/9 сразу — без «встаёт» и без 4024
				//   (прежде порт ставил хвост «встаёт» и по его концу код 0). Ключ `asda.Char.SitExitEtalon 0` — прежнее.
				Pawn->DropSitForEtalonState(/*bIdleClip*/ true, TEXT("ремесло: CharFx 0"));
				Pawn->SetAnimState(EAsdaPlayerState::Idle);
			}
			StopAt = Pawn;
		}
	}
	// ★ 26.09.2026: раньше сюда шли сырые `GetActorLocation().X/.Y` движка; тело 4008 требует ИГРОВЫЕ
	//   (gameX, gameZ) — перевод внутри `SendStop4008AtActor` через `AsdaCoord::UeToGame2D`.
	if (UAsdaNetSubsystem* Net = GI->GetSubsystem<UAsdaNetSubsystem>()) { Net->SendStop4008AtActor(StopAt); }
	// `[218] = level * (10*[221]/5) / 10 + 20` (+ надбавка К-8), `[2] = 1`, пересборка страницы и сводки.
	Craft->RecalcLimit(TEXT("открытие окна"));
	Page = 1;
	BuildFilteredPage(Tab);
	RefreshSummaryGated();
	RefreshTabs();
	TipKind = TipIdx = -1;
	Mouse.Reset();
	bOpen = true;
	WM->SetWindowVisible(kWindowId, true);
	WM->BringWindowToFront(kWindowId);   // `CClass_MapInsert(g_pWindowMgr, [17])`
	JournalAdd(FString::Printf(TEXT("открыто — %s"), Source));
	UE_LOG(LogAsdaCraftWin, Log, TEXT("[ремесло-окно] 76000 открыто (%s): вкладка %d, страница %d/%d, строк %d, ур. %d, «%d / %d»"),
		Source, Tab, Page, Pages, Filtered.Num(), Craft->GetCraftLevel(), Craft->GetLearnedCount(), Craft->GetRecipeLimit());
}

void UAsdaCraftWindowSubsystem::HideAndCleanup()
{
	// `uiWindowHideAndCleanup@0xFDD590`: `uiPanelHideAndReset(this)` → `itemTooltipWndReset(this+80)` (окно состава
	//   77000: оба под-окна, [137] = 0, снова `uiPanelHideAndReset`) → `SetVisible([17], 0)`.
	if (bControlsReady) { PanelHideAndReset(); }
	// `itemTooltipWndReset(this + 80)` — окно состава 77000 (оба под-окна, `[137] = 0`, снова `uiPanelHideAndReset`).
	if (UAsdaCraftRecipeWindowSubsystem* Rw = RecipeWin()) { Rw->ResetWindow(TEXT("uiWindowHideAndCleanup 76000")); }
	const bool bWas = bOpen;
	bOpen = false;
	TickTooltip(-1, -1);
	if (UAsdaWindowSubsystem* WM = GetGameInstance() ? GetGameInstance()->GetSubsystem<UAsdaWindowSubsystem>() : nullptr)
	{
		WM->SetWindowVisible(kWindowId, false);
	}
	Mouse.Reset();
	if (bWas) { UE_LOG(LogAsdaCraftWin, Log, TEXT("[ремесло-окно] 76000 скрыто (uiWindowHideAndCleanup)")); }
}

void UAsdaCraftWindowSubsystem::PanelHideAndReset()
{
	// `uiPanelHideAndReset@0xFDD020` (ui_core.c:100460-100470).
	HideOver();                                  // `[16]` → SetVisible(0), [16] = 0
	AsdaUi::SetShownDecor(Frame, false);         // рамка 76077
	SelRow = -1;                                 // [5] = −1
	BuildFilteredPage(Tab);
	RefreshSummaryGated();
}

// ═════════════════════════════ СПИСОК И СВОДКА ════════════════════════════════════════════════

void UAsdaCraftWindowSubsystem::BuildFilteredPage(int32 Category)
{
	// `uiBuildFilteredItemPage@0xFE0A20` (ui_core.c:100812-100847).
	if (Category < 0) { return; }
	const UAsdaCraftSubsystem* Craft = GetGameInstance() ? GetGameInstance()->GetSubsystem<UAsdaCraftSubsystem>() : nullptr;
	Filtered.Reset();
	if (Craft)
	{
		const TArray<int32>& L = Craft->GetLearnedList();
		const int32 N = FMath::Min(Craft->GetLearnedCount(), L.Num());   // эталон: `i < [222]`
		for (int32 I = 0; I < N; ++I)
		{
			if (Category == kTabAll) { Filtered.Add(L[I]); continue; }
			const FAsdaMakingRecipeRow* R = Craft->FindRecipe(L[I]);
			if (R && R->Category == Category) { Filtered.Add(L[I]); }
		}
	}
	// `[1] = n/6 (+1 при остатке)`, и «не меньше текущей страницы» (`if ([1] < [2]) [1] = [2]`).
	Pages = Filtered.Num() / kRows + ((Filtered.Num() % kRows) ? 1 : 0);
	if (Pages < Page) { Pages = Page; }
}

void UAsdaCraftWindowSubsystem::RefreshRows()
{
	// `uiList_RefreshItemRows6@0xFDE700` (ui_core.c:100723-100809).
	UGameInstance* GI = GetGameInstance();
	const UAsdaCraftSubsystem* Craft = GI ? GI->GetSubsystem<UAsdaCraftSubsystem>() : nullptr;
	const UAsdaItemTableSubsystem* IT = GI ? GI->GetSubsystem<UAsdaItemTableSubsystem>() : nullptr;
	const int32 Base = kRows * (Page - 1);
	RowRecipe.Init(-1, kRows);
	for (int32 I = 0; I < kRows && I < Rows.Num(); ++I)
	{
		FAsdaCraftRowUi& Ui = Rows[I];
		const int32 Id = Filtered.IsValidIndex(Base + I) ? Filtered[Base + I] : -1;   // хвост списка эталона — memset(−1)
		const FAsdaMakingRecipeRow* R = (Craft && Id >= 0) ? Craft->FindRecipe(Id) : nullptr;
		const FAsdaItemRecord* Item = (R && IT) ? IT->GetItem(R->ResultCode) : nullptr;
		if (!R || !Item)
		{
			// Пустая строка (нет записи рецепта или предмета-результата): [10+i] = 0, пустая текстура, пять
			//   пустых текстов, затемнение ПОКАЗАНО (метка LABEL_9 эталона).
			SetTxt(Ui.MarkText, FString());
			for (UTextBlock* T : Ui.T) { SetTxt(T, FString()); }
			SetGroupShown(Ui.Black, true);
			continue;
		}
		RowRecipe[I] = Id;
		SetTxt(Ui.T[0], FString::FromInt(Item->RequiredLevel));                         // t1 `ItemRecord->requiredLevel`
		SetTxt(Ui.T[1], Loc(AsdaCraftLoc::kTabBase + R->Category));                     // t2 `Loc(5540 + rec[66])`
		SetTxt(Ui.T[2], FString::FromInt((int32)(int8)R->CraftLevel));                  // t3 `(char)rec[67]`
		SetTxt(Ui.T[3], Craft->RecipeName(R));                                           // t4 `rec + 2` ([ПОРТ] 09.10.2026 (мультиязычность): язык сессии)
		// Затемнение: материалов не хватает ИЛИ уровень ремесла мал (ui_core.c:100803).
		const bool bDark = !Craft->CheckRequiredItems(*R) || (int32)(int8)R->CraftLevel > Craft->GetCraftLevel();
		SetGroupShown(Ui.Black, bDark);
	}
	if (MarkGrid) { MarkGrid->Refresh(); }
}

void UAsdaCraftWindowSubsystem::RefreshSummaryGated()
{
	// `uiCraftPanelRefreshSummaryGated@0xFDE300` (ui_core.c:100651-100679). Гейт эталона — контрол уровня есть.
	if (!TxtLevel) { return; }
	const UAsdaCraftSubsystem* Craft = GetGameInstance() ? GetGameInstance()->GetSubsystem<UAsdaCraftSubsystem>() : nullptr;
	if (!Craft) { return; }
	SetTxt(TxtLevel, FString::FromInt(Craft->GetCraftLevel()));                          // 76004 = [221]
	// 76005: `CProgressBar_SetRange(0, 100.0f)`, значение `(float)[219]` — процент опыта ремесла.
	BarFrac = FMath::Clamp((float)Craft->GetCraftPercent() / 100.f, 0.f, 1.f);
	if (Bar.IsValid()) { UAsdaMiniStateSubsystem::ClipFill(Bar, BarFrac); }
	SetTxt(TxtRecipeNum, FString::Printf(TEXT("%d / %d"), Craft->GetLearnedCount(), Craft->GetRecipeLimit()));   // 76006
	RefreshRows();
	SetPageTexts();
}

void UAsdaCraftWindowSubsystem::RefreshSummary()
{
	// `uiCraftPanel_RefreshSummary@0xFDE4A0` (ui_core.c:100681-100717) — зовёт окно изготовления 77500 после каждой штуки
	//   (`upgradeAnimDlg_tick`, item_enhance.c:10723). Без гейта «контрол уровня есть» (он у `Gated`): уровень, полоса,
	//   «N / M», страницы; затем затемнение ВЫБРАННОЙ строки (С-4, ключ выше) и `uiCraftPanelRefreshAll` окна 77000.
	if (!bControlsReady) { return; }
	const UAsdaCraftSubsystem* Craft = GetGameInstance() ? GetGameInstance()->GetSubsystem<UAsdaCraftSubsystem>() : nullptr;
	if (!Craft) { return; }
	SetTxt(TxtLevel, FString::FromInt(Craft->GetCraftLevel()));                          // 76004 = [221]
	BarFrac = FMath::Clamp((float)Craft->GetCraftPercent() / 100.f, 0.f, 1.f);            // 76005: 0..100, [219]
	if (Bar.IsValid()) { UAsdaMiniStateSubsystem::ClipFill(Bar, BarFrac); }
	SetTxt(TxtRecipeNum, FString::Printf(TEXT("%d / %d"), Craft->GetLearnedCount(), Craft->GetRecipeLimit()));   // 76006
	SetPageTexts();                                                                        // 76080 / 76081
	if (SelRow >= 0 && Rows.IsValidIndex(SelRow))
	{
		const FAsdaMakingRecipeRow* R = Craft->FindRecipe(GetRowRecipe(SelRow));
		if (R)
		{
			const bool bHave = Craft->CheckRequiredItems(*R);
			const bool bLevelHigh = UAsdaCraftSubsystem::RecipeReqLevel(*R) > Craft->GetCraftLevel();
			const bool bDark = CVarCraftFixSummaryRowDarken.GetValueOnGameThread() != 0
				? (!bHave || bLevelHigh)          // исправлено — как `uiList_RefreshItemRows6`
				: !(bHave || bLevelHigh);         // эталон: `if (check || lvl > cur) SetVisible(black, 0) else (black, 1)`
			SetGroupShown(Rows[SelRow].Black, bDark);
		}
	}
	if (UAsdaCraftRecipeWindowSubsystem* Rw = RecipeWin()) { Rw->RefreshAll(); }   // `uiCraftPanelRefreshAll(this+80)`
}

void UAsdaCraftWindowSubsystem::SetPageTexts()
{
	SetTxt(TxtPage,  FString::FromInt(Page));    // 76080 = [2]
	SetTxt(TxtPages, FString::FromInt(Pages));   // 76081 = [1]
}

void UAsdaCraftWindowSubsystem::RefreshTabs()
{
	// Выбранная вкладка — `CControl_SetSelectedIndex(76007, [4])`; вид — общая точка вкладок порта.
	AsdaUi::ApplyTabState(TabPlanes, TabCaptions, TabButtons, Tab, TabIdlePlanes, TabIdleStyles, TabIdleCaptions);
}

void UAsdaCraftWindowSubsystem::PlaceFrame(int32 Row)
{
	// `CControl_SetField74_75(рамка, 13, 37*row + 23)` — координаты родителя в `.JUI`; в ассете группа стоит со
	//   смещением «база ассета − Origin JUI» (закон «оффсеты узлов в группах относительны», §3.4 отчёта: +5 по Y).
	if (!Frame) { return; }
	const FVector2D Pos(FrameBase.X + (kFrameCodeX - kFrameJuiX), FrameBase.Y + (kFrameCodeY0 + kFrameStep * Row - kFrameJuiY));
	AsdaUi::SetSlotPos(Frame, Pos);
}

void UAsdaCraftWindowSubsystem::HideOver()
{
	if (Rows.IsValidIndex(OverRow)) { SetGroupShown(Rows[OverRow].Over, false); }
	OverRow = -1;
}

// ═════════════════════════════ ДЕЙСТВИЯ ═══════════════════════════════════════════════════════

void UAsdaCraftWindowSubsystem::OnRowClicked(int32 Row)
{
	// msg 0xB по 76020…76076 (shop_trade.c:22070-22085).
	if (Row < 0 || Row >= kRows) { return; }
	const int32 Index = Row + kRows * (Page - 1);
	if (Index >= Filtered.Num()) { return; }
	SelRow = Row;                                            // [5]
	PlaceFrame(Row);
	AsdaUi::SetShownDecor(Frame, true);                      // рамка 76077
	JournalAdd(FString::Printf(TEXT("клик 0xB строка %d (рецепт %d)"), Row, RowRecipe.IsValidIndex(Row) ? RowRecipe[Row] : -1));
	// `uiCraftWnd_OpenForItem(this+80, [[5]+10])` — окно состава 77000 (волна В3): NULL-запись или тот же рецепт →
	//   ничего; иначе состав, под-окна очищены, показ, верхний слой, снимок открытых окон.
	const int32 Id = RowRecipe.IsValidIndex(Row) ? RowRecipe[Row] : -1;
	UAsdaCraftRecipeWindowSubsystem* Rw = RecipeWin();
	const bool bOpened = Rw && Rw->OpenForRecipe(Id, *FString::Printf(TEXT("строка %d окна 76000"), Row));
	JournalAdd(FString::Printf(TEXT("77000 %s — рецепт %d"), bOpened ? TEXT("открыто") : TEXT("не открыто (тот же рецепт / нет записи)"), Id));
}

UAsdaCraftRecipeWindowSubsystem* UAsdaCraftWindowSubsystem::RecipeWin() const
{
	return GetGameInstance() ? GetGameInstance()->GetSubsystem<UAsdaCraftRecipeWindowSubsystem>() : nullptr;
}

int32 UAsdaCraftWindowSubsystem::GetSelectedRecipe() const
{
	const UAsdaCraftRecipeWindowSubsystem* Rw = RecipeWin();
	return Rw ? Rw->GetRecipeId() : -1;
}

void UAsdaCraftWindowSubsystem::OnRecipeWindowReset()
{
	// Хвост `itemTooltipWndReset@0xFE2200` → `uiPanelHideAndReset(g_pItemMainWnd)` (item_inventory.c:69553).
	if (bControlsReady) { PanelHideAndReset(); }
}

void UAsdaCraftWindowSubsystem::OnPrevPage()
{
	// 0x12932 (shop_trade.c:22143-22160).
	if (--Page < 1) { Page = 1; return; }
	if (CVarCraftFixDeleteAfterPaging.GetValueOnGameThread() != 0)
	{
		// ★ К-2: выбор снимается, как при смене вкладки (shop_trade.c:22196, :22210-22216).
		SelRow = -1; HideOver(); AsdaUi::SetShownDecor(Frame, false);
	}
	SetTxt(TxtPage, FString::FromInt(Page));
	RefreshRows();
	JournalAdd(FString::Printf(TEXT("страница %d/%d"), Page, Pages));
}

void UAsdaCraftWindowSubsystem::OnNextPage()
{
	// 0x12933 (shop_trade.c:22161-22176).
	if (++Page > Pages) { Page = Pages; return; }
	if (CVarCraftFixDeleteAfterPaging.GetValueOnGameThread() != 0)
	{
		SelRow = -1; HideOver(); AsdaUi::SetShownDecor(Frame, false);
	}
	SetTxt(TxtPage, FString::FromInt(Page));
	RefreshRows();
	JournalAdd(FString::Printf(TEXT("страница %d/%d"), Page, Pages));
}

void UAsdaCraftWindowSubsystem::OnDeleteClicked()
{
	// 0x12940 (shop_trade.c:22177-22183): `if ([5] >= 0) { 6600(*(WORD*)[[5]+10]); uiPanelHideAndReset(); }`.
	//   Подтверждения у оригинала НЕТ (особенность, переносим 1:1).
	int32 Id = -1;
	if (CVarCraftFixDeleteAfterPaging.GetValueOnGameThread() != 0)
	{
		// ★ К-2 (+ ревизия В0–В2 М-8, решено в В3): удаляется рецепт, ПОКАЗАННЫЙ в окне состава 77000 (`[80+137]`) —
		//   даже когда листание сняло выбор строки (иначе окно показывает рецепт, а «Удалить» молчит); окна состава
		//   нет — строка текущей страницы. Гейт эталона `[5] >= 0` у исправленного пути заменён этим правилом.
		const int32 Shown = GetSelectedRecipe();
		Id = (Shown >= 0) ? Shown : (RowRecipe.IsValidIndex(SelRow) ? RowRecipe[SelRow] : -1);
		if (Id < 0) { JournalAdd(TEXT("удаление: ни рецепта окна состава, ни строки не выбрано")); return; }
	}
	else
	{
		if (SelRow < 0) { JournalAdd(TEXT("удаление: строка не выбрана")); return; }
		Id = RowRecipe.IsValidIndex(SelRow) ? RowRecipe[SelRow] : -1;
		if (Id < 0)
		{
			// Эталон: `*(WORD*)0` ⇒ AV — клиент падает. Падать не будем: запись в лог, пакета нет.
			UE_LOG(LogAsdaCraftWin, Warning, TEXT("[ремесло-окно] удаление (как оригинал): строка %d этой страницы пуста — в оригинале здесь вылет (AV), пакет не отправлен"), SelRow);
			JournalAdd(FString::Printf(TEXT("удаление: строка %d пуста — в оригинале вылет"), SelRow));
			return;
		}
	}
	if (Id < 0) { return; }
	UAsdaCraftSubsystem* Craft = GetGameInstance() ? GetGameInstance()->GetSubsystem<UAsdaCraftSubsystem>() : nullptr;
	const bool bSent = Craft && Craft->SendForgetRecipe(Id);
	JournalAdd(FString::Printf(TEXT("удаление рецепта %d → 6600 %s"), Id, bSent ? TEXT("ушёл") : TEXT("не ушёл")));
	if (!bSent)
	{
		// ★ Ревизия В0–В2 М-4: пакет не ушёл (замок «операция в полёте» `asda.Craft.SingleInFlight` или нет сети).
		//   У эталона замка нет и отказа не бывает, нотиса для этого случая в данных нет ⇒ выбор НЕ снимаем:
		//   строка остаётся выделенной, игрок видит, что удаление не прошло, и может нажать ещё раз после ответа.
		UE_LOG(LogAsdaCraftWin, Log, TEXT("[ремесло-окно] удаление рецепта %d не отправлено — выбор строки сохранён (замок/нет сети)"), Id);
		return;
	}
	PanelHideAndReset();
}

void UAsdaCraftWindowSubsystem::OnCloseClicked()
{
	// 0x12941 / 0x12943 → `uiWindowHideAndCleanup`.
	HideAndCleanup();
	JournalAdd(TEXT("закрыто кнопкой"));
}

void UAsdaCraftWindowSubsystem::OnTabSelected(int32 NewTab)
{
	// msg 66817 (shop_trade.c:22195-22216): [4] = кат., [5] = −1, [2] = 1, пересборка, подписи страниц, over и рамка прячутся.
	if (NewTab < 0 || NewTab >= kTabs) { return; }
	Tab = NewTab;
	SelRow = -1;
	Page = 1;
	BuildFilteredPage(Tab);
	RefreshRows();
	SetPageTexts();
	HideOver();
	AsdaUi::SetShownDecor(Frame, false);
	RefreshTabs();
	JournalAdd(FString::Printf(TEXT("вкладка %d: строк %d, страниц %d"), Tab, Filtered.Num(), Pages));
}

// ═════════════════════════════ МЫШЬ И ПОДСКАЗКИ ═══════════════════════════════════════════════

void UAsdaCraftWindowSubsystem::TickMouse()
{
	UAsdaWindowSubsystem* WM = GetGameInstance() ? GetGameInstance()->GetSubsystem<UAsdaWindowSubsystem>() : nullptr;
	if (WM && (WM->IsPointerBlockedByTopmost(kWindowId) || WM->IsCursorOverHigherWindow(kWindowId)))
	{
		TickTooltip(-1, -1);
		HideOver();
		Mouse.Reset();
		return;
	}
	int32 Kind = -1, Idx = -1;
	for (int32 R = 0; R < Rows.Num() && Kind < 0; ++R)
	{
		for (const TObjectPtr<UWidget>& W : Rows[R].Hit)
		{
			if (W && AsdaUi::IsCursorOverWidget(W)) { Kind = kTipRow; Idx = R; break; }
		}
	}
	if (Kind < 0 && (Bar.Back || Bar.Fill) && AsdaUi::IsCursorOverWidget(Bar.Back ? Bar.Back.Get() : Bar.Fill.Get())) { Kind = kTipExp; Idx = 0; }
	if (Kind < 0 && TxtRecipeNum && AsdaUi::IsCursorOverWidget(TxtRecipeNum)) { Kind = kTipLimit; Idx = 0; }

	// msg 9 / 0xA по строке — «over» (shop_trade.c:22101-22129): показывается, только если индекс < числа строк.
	const int32 WantOver = (Kind == kTipRow && Idx + kRows * (Page - 1) < Filtered.Num()) ? Idx : -1;
	if (WantOver != OverRow)
	{
		HideOver();
		if (Rows.IsValidIndex(WantOver)) { SetGroupShown(Rows[WantOver].Over, true); OverRow = WantOver; }
	}
	TickTooltip(Kind, Idx);

	if (Mouse.bLmbDown && Kind == kTipRow) { OnRowClicked(Idx); }
}

void UAsdaCraftWindowSubsystem::TickTooltip(int32 Kind, int32 Idx)
{
	// ★ Дедуп №2, В1-B (U18, 11.10.2026): шапка трекера (ключ «что показано», гашение при уходе курсора) — общая точка
	//   `UAsdaItemTooltipSubsystem::BeginHoverTip`; тело показа (эталонные билдеры окна) — ниже, как было.
	UAsdaItemTooltipSubsystem* Tip = UAsdaItemTooltipSubsystem::BeginHoverTip(GetGameInstance(), EAsdaTipOwner::Craft, TipKind, TipIdx, Kind, Idx);
	if (!Tip) { return; }
	if (Kind == kTipExp || Kind == kTipLimit)
	{
		// msg 8 по 76005/76006 → `tutorialPopupLayoutSmall(&g_ItemTooltip, Loc(12077), Loc(115301|115300), −1, −1)`.
		const int32 BodyId = (Kind == kTipExp) ? AsdaCraftLoc::kTipExp : AsdaCraftLoc::kTipLimit;
		FString Body = AsdaNotice::LocRaw(GetGameInstance(), BodyId);
		if (Body.IsEmpty())
		{
			Body = AsdaNotice::LocRaw(GetGameInstance(), AsdaCraftLoc::kTipMissingIdx99);   // как оригинал (К-4)
		}
		FAsdaTipMetrics M;
		M.WidthAdd = 27.f;    // «маленькая» всплывашка эталона (как у подсказки полосы окна 20000)
		M.ExtraLines = 0;
		Tip->ShowTextTip(EAsdaTipOwner::Craft, Loc(AsdaCraftLoc::kTipTitle), Body, M);
		JournalAdd(FString::Printf(TEXT("подсказка %d: «%s» / «%s»"), Kind == kTipExp ? 76005 : 76006, *Loc(AsdaCraftLoc::kTipTitle), *Body));
		return;
	}
	// msg 8 по строке → `uiCraft_ShowItemTooltip(this, [10+row])`: NULL — ничего; иначе синтетическая запись
	//   предмета-результата (+0 код, +13 `field68`, +14 `field72`, +50 `tooltipFlag200`) и полный `ItemTooltip_Build`
	//   (или спец-предмет / самоцвет) — ровно то, что делает общая точка `ShowPreviewSlotTooltip`.
	const UAsdaCraftSubsystem* Craft = GetGameInstance()->GetSubsystem<UAsdaCraftSubsystem>();
	const int32 Id = RowRecipe.IsValidIndex(Idx) ? RowRecipe[Idx] : -1;
	const FAsdaMakingRecipeRow* R = (Craft && Id >= 0) ? Craft->FindRecipe(Id) : nullptr;
	if (!R) { Tip->HideTooltipFor(EAsdaTipOwner::Craft); return; }
	Tip->ShowPreviewSlotTooltip(R->ResultCode, EAsdaTipOwner::Craft);
	JournalAdd(FString::Printf(TEXT("тултип строки %d: предмет %d"), Idx, R->ResultCode));
}

// ═════════════════════════════ ДИАГНОСТИКА ════════════════════════════════════════════════════

FString UAsdaCraftWindowSubsystem::Loc(int32 Id) const
{
	return AsdaNotice::Loc(GetGameInstance(), Id);
}

void UAsdaCraftWindowSubsystem::JournalAdd(const FString& Line)
{
	Journal.Add(Line);
	UE_LOG(LogAsdaCraftWin, Log, TEXT("[ремесло-окно] %s"), *Line);
}

FString UAsdaCraftWindowSubsystem::DumpControls() const
{
	FString S = TEXT("=== окно создания предмета 76000: разобранные контролы ===\n");
	auto Line = [&S](const FString& Id, bool bOk) { S += FString::Printf(TEXT("  %-44s %s\n"), *Id, bOk ? TEXT("есть") : TEXT("НЕТ")); };
	Line(TEXT("76000 ITEM_making_WIN (корень)"), Widget != nullptr);
	Line(TEXT("76001 movetext «Создание предмета»"), TxtTitle != nullptr);
	Line(TEXT("76001 ручка перетаскивания _DRAG"), DragHandle != nullptr);
	Line(TEXT("76099 XX (закрыть)"), BtnX != nullptr);
	Line(TEXT("76096 BOX_delbtn «Удалить инструкцию»"), BtnDelete != nullptr && TxtDelete != nullptr);
	Line(TEXT("76097 BOX_Xbtn «Закрыть»"), BtnClose != nullptr && TxtClose != nullptr);
	Line(TEXT("76077 BOX_select (рамка, группа)"), Frame != nullptr);
	Line(TEXT("76002 m_t1 «Ур. ремесла»"), TxtCraftLbl != nullptr);
	Line(TEXT("76003 m_t2 «Ур.»"), TxtLvlLbl != nullptr);
	Line(TEXT("76004 m_t3 (уровень)"), TxtLevel != nullptr);
	Line(FString::Printf(TEXT("76005 m_gbar (ES0/ES1, ширина %.0f)"), Bar.FullW), Bar.Fill != nullptr && Bar.Back != nullptr);
	Line(TEXT("76006 m_t4 «N / M»"), TxtRecipeNum != nullptr);
	for (int32 K = 0; K < TabButtons.Num(); ++K)
	{
		Line(FString::Printf(TEXT("76007 вкладка %d (план/кнопка/подпись)"), K),
			TabPlanes.IsValidIndex(K) && TabPlanes[K] && TabButtons[K] && TabCaptions.IsValidIndex(K) && TabCaptions[K]);
	}
	for (int32 K = 0; K < ColHeads.Num(); ++K) { Line(FString::Printf(TEXT("%d BOX_T%d_Text"), 76011 + K, K + 1), ColHeads[K] != nullptr); }
	for (int32 R = 0; R < Rows.Num(); ++R)
	{
		const FAsdaCraftRowUi& U = Rows[R];
		Line(FString::Printf(TEXT("%d L%d_mark (план + подпись)"), 76020 + 10 * R, R + 1), U.Mark != nullptr && U.MarkText != nullptr);
		for (int32 K = 0; K < 4; ++K) { Line(FString::Printf(TEXT("%d L%d_t%d"), 76021 + 10 * R + K, R + 1, K + 1), U.T.IsValidIndex(K) && U.T[K] != nullptr); }
		Line(FString::Printf(TEXT("%d L%d_black (узлов %d)"), 76025 + 10 * R, R + 1, U.Black.Num()), U.Black.Num() > 0);
		Line(FString::Printf(TEXT("%d L%d_over (узлов %d)"), 76026 + 10 * R, R + 1, U.Over.Num()), U.Over.Num() > 0);
		Line(FString::Printf(TEXT("       зона строки %d (узлов %d)"), R + 1, U.Hit.Num()), U.Hit.Num() == 5);
	}
	Line(TEXT("76080 tlist_L (страница)"), TxtPage != nullptr);
	Line(TEXT("76081 tlist_R (страниц)"), TxtPages != nullptr);
	Line(TEXT("76082 Alist_L (назад)"), BtnPrev != nullptr);
	Line(TEXT("76083 Alist_R (вперёд)"), BtnNext != nullptr);
	S += FString::Printf(TEXT("  ИТОГ: найдено %d/%d узлов группы 760\n"), FoundControls, kControlsTotal);
	return S;
}

FString UAsdaCraftWindowSubsystem::DumpState() const
{
	FString S = FString::Printf(TEXT("[ремесло-окно] открыто=%d вкладка=%d страница=%d/%d строк=%d выбрана=%d рецепт окна состава=%d over=%d полоса=%.2f\n"),
		bOpen ? 1 : 0, Tab, Page, Pages, Filtered.Num(), SelRow, GetSelectedRecipe(), OverRow, BarFrac);
	for (int32 R = 0; R < Rows.Num(); ++R)
	{
		const FAsdaCraftRowUi& U = Rows[R];
		S += FString::Printf(TEXT("  строка %d: рецепт %d · t1=«%s» t2=«%s» t3=«%s» t4=«%s» · затемнена=%d\n"), R,
			RowRecipe.IsValidIndex(R) ? RowRecipe[R] : -1,
			(U.T.IsValidIndex(0) && U.T[0]) ? *U.T[0]->GetText().ToString() : TEXT("-"),
			(U.T.IsValidIndex(1) && U.T[1]) ? *U.T[1]->GetText().ToString() : TEXT("-"),
			(U.T.IsValidIndex(2) && U.T[2]) ? *U.T[2]->GetText().ToString() : TEXT("-"),
			(U.T.IsValidIndex(3) && U.T[3]) ? *U.T[3]->GetText().ToString() : TEXT("-"),
			GroupShown(U.Black) ? 1 : 0);
	}
	return S;
}

FString UAsdaCraftWindowSubsystem::DebugText(int32 IdNum) const
{
	auto T = [](const UTextBlock* B) { return B ? B->GetText().ToString() : FString(TEXT("<нет>")); };
	switch (IdNum)
	{
	case 76001: return T(TxtTitle);
	case 76002: return T(TxtCraftLbl);
	case 76003: return T(TxtLvlLbl);
	case 76004: return T(TxtLevel);
	case 76006: return T(TxtRecipeNum);
	case 76080: return T(TxtPage);
	case 76081: return T(TxtPages);
	case 76096: return T(TxtDelete);
	case 76097: return T(TxtClose);
	default: break;
	}
	if (IdNum >= 76011 && IdNum <= 76015) { return ColHeads.IsValidIndex(IdNum - 76011) ? T(ColHeads[IdNum - 76011]) : FString(); }
	if (IdNum >= 76020 && IdNum <= 76076)
	{
		const int32 R = (IdNum - 76020) / 10, K = (IdNum - 76020) % 10;
		if (Rows.IsValidIndex(R) && K >= 1 && K <= 4 && Rows[R].T.IsValidIndex(K - 1)) { return T(Rows[R].T[K - 1]); }
	}
	if (IdNum >= 7600700 && IdNum <= 7600704) { const int32 K = IdNum - 7600700; return TabCaptions.IsValidIndex(K) ? T(TabCaptions[K]) : FString(); }
	return FString();
}

bool UAsdaCraftWindowSubsystem::DebugRowBlack(int32 Row) const
{
	return Rows.IsValidIndex(Row) && GroupShown(Rows[Row].Black);
}

bool UAsdaCraftWindowSubsystem::DebugFrameVisible() const
{
	return AsdaUi::IsShown(Frame);   // ★ волна В6 ремесла (МЕЛК-8): общая точка видимости узла
}

FVector2D UAsdaCraftWindowSubsystem::DebugFrameSlotPos() const
{
	FVector2D P = FVector2D::ZeroVector, S;
	if (Frame) { AsdaUi::SlotRect(Frame, P, S); }
	return P;
}

bool UAsdaCraftWindowSubsystem::DebugPressButton(int32 IdNum)
{
	UButton* Btn = nullptr;
	switch (IdNum)
	{
	case 76082: Btn = BtnPrev; break;
	case 76083: Btn = BtnNext; break;
	case 76096: Btn = BtnDelete; break;
	case 76097: Btn = BtnClose; break;
	case 76099: Btn = BtnX; break;
	default:
		if (IdNum >= 76007000 && IdNum < 76007000 + kTabs) { const int32 K = IdNum - 76007000; Btn = TabButtons.IsValidIndex(K) ? TabButtons[K].Get() : nullptr; }
		break;
	}
	if (!Btn) { return false; }
	Btn->OnClicked.Broadcast();   // тот же путь, что клик игрока: делегат кнопки → реле → обработчик
	return true;
}

bool UAsdaCraftWindowSubsystem::DebugRowCenter(int32 Row, FVector2D& Out) const
{
	if (!Rows.IsValidIndex(Row)) { return false; }
	// Центр подписи t4 (название) — самый широкий узел строки.
	for (const TObjectPtr<UWidget>& W : Rows[Row].Hit)
	{
		FVector2D Mn, Mx;
		if (W && W->GetName().Contains(TEXT("_t4_")) && AsdaUi::WidgetAbsRect(W, Mn, Mx)) { Out = (Mn + Mx) * 0.5f; return true; }
	}
	return false;
}

// ═════════════════════════════ КОНСОЛЬ И САМОТЕСТ ═════════════════════════════════════════════

namespace
{
	struct FCraftCtx
	{
		UAsdaCraftWindowSubsystem* W = nullptr;
		UAsdaCraftSubsystem* C = nullptr;
		UGameInstance* GI = nullptr;
	};
	bool CraftFromWorld(UWorld* World, FCraftCtx& Out)
	{
		Out.GI = World ? World->GetGameInstance() : nullptr;
		Out.W = Out.GI ? Out.GI->GetSubsystem<UAsdaCraftWindowSubsystem>() : nullptr;
		Out.C = Out.GI ? Out.GI->GetSubsystem<UAsdaCraftSubsystem>() : nullptr;
		return Out.W && Out.C;
	}

#if ASDA_DEV_CONSOLE   // ★ античит А2: дев-команда, в Shipping не регистрируется
	FAutoConsoleCommandWithWorldAndArgs GCmdCraftWindow(
		TEXT("asda.Craft.Window"),
		TEXT("asda.Craft.Window — тумблер окна 76000 путём клавиши Z (`tileInteractGateAndOpen` со всеми гейтами)."),
		FConsoleCommandWithWorldAndArgsDelegate::CreateLambda([](const TArray<FString>&, UWorld* W)
		{
			FCraftCtx X; if (CraftFromWorld(W, X)) { X.W->GateAndToggle(TEXT("консоль")); }
		}));
#endif // ASDA_DEV_CONSOLE

#if ASDA_DEV_CONSOLE   // ★ античит А2: дев-команда, в Shipping не регистрируется
	FAutoConsoleCommandWithWorldAndArgs GCmdCraftState(
		TEXT("asda.Craft.State"),
		TEXT("asda.Craft.State — модель ремесла, контролы и состояние окна 76000."),
		FConsoleCommandWithWorldAndArgsDelegate::CreateLambda([](const TArray<FString>&, UWorld* W)
		{
			FCraftCtx X; if (!CraftFromWorld(W, X)) { return; }
			UE_LOG(LogAsdaCraftWin, Log, TEXT("%s%s%s"), *X.C->DumpState(), *X.W->DumpControls(), *X.W->DumpState());
		}));
#endif // ASDA_DEV_CONSOLE

	// Синтез 6597 и прогон через БОЕВОЙ приёмник: `asda.Craft.Inject6597 <лимит> <ур> <опыт> <процент> <id…>`.
#if ASDA_DEV_CONSOLE   // ★ античит А2: дев-команда, в Shipping не регистрируется
	FAutoConsoleCommandWithWorldAndArgs GCmdCraftInject6597(
		TEXT("asda.Craft.Inject6597"),
		TEXT("asda.Craft.Inject6597 <лимит> <уровень> <опыт> <процент> <id рецептов…> — тело 6597 через боевой приёмник."),
		FConsoleCommandWithWorldAndArgsDelegate::CreateLambda([](const TArray<FString>& A, UWorld* W)
		{
			FCraftCtx X; if (!CraftFromWorld(W, X) || A.Num() < 4) { return; }
			// ★ Волна В8 (ревизия В7 С-3): вброс подменил бы модель живого персонажа поверх настоящего 6597 — только офлайн.
			if (UAsdaCraftSubsystem::RefuseDebugWhenOnline(W, TEXT("asda.Craft.Inject6597"), 0.f)) { return; }
			TArray<int32> Ids; for (int32 I = 4; I < A.Num(); ++I) { Ids.Add(FCString::Atoi(*A[I])); }
			X.C->DebugInject(6597, UAsdaCraftSubsystem::DebugBuild6597(Ids, FCString::Atoi(*A[0]), FCString::Atoi(*A[1]), FCString::Atoi(*A[2]), FCString::Atoi(*A[3])));
		}));
#endif // ASDA_DEV_CONSOLE

	// ★ САМОТЕСТ ПУТЁМ ИГРОКА (-game). Сервера нет: пакеты синтезируются и идут БОЕВЫМИ приёмниками, исходящие
	//   ловит узкий перехват (только свой опкод, снимается сам). `delay=N` — сдвиг шагов, `shot` — кадры, `quit` — выход.
#if ASDA_DEV_CONSOLE   // ★ античит А2: дев-команда, в Shipping не регистрируется
	FAutoConsoleCommandWithWorldAndArgs GCmdCraftSelfTest(
		TEXT("asda.Craft.SelfTest"),
		TEXT("Окно 76000 и модель ремесла: разбор контролов, 6597 (и после «портала»), все входы, вкладки, листание, полоса, "
		     "К-2, приёмники 6599/6601/6603/6605, отправители 6598/6600/6602/6604 с перехватом тела. `delay=N` `shot` `quit`."),
		FConsoleCommandWithWorldAndArgsDelegate::CreateLambda([](const TArray<FString>& Args, UWorld* W)
		{
			FCraftCtx X; if (!CraftFromWorld(W, X) || !W) { return; }
			UAsdaCraftWindowSubsystem* Win = X.W;
			UAsdaCraftSubsystem* Craft = X.C;
			UGameInstance* GI = X.GI;
			UAsdaInventorySubsystem* Inv = GI->GetSubsystem<UAsdaInventorySubsystem>();
			UAsdaWindowSubsystem* WM = GI->GetSubsystem<UAsdaWindowSubsystem>();
			if (!Inv || !WM) { return; }
			bool bQuit = false, bShot = false;
			float Delay = 0.f;
			FString ShotPrefix(TEXT("asda_craft_w012_"));
			for (const FString& A : Args)
			{
				if (A.Equals(TEXT("quit"), ESearchCase::IgnoreCase)) { bQuit = true; }
				if (A.Equals(TEXT("shot"), ESearchCase::IgnoreCase)) { bShot = true; }
				if (A.StartsWith(TEXT("prefix="))) { ShotPrefix = A.Mid(7); }
			}
			Delay = AsdaSelfTestKit::DelayArgOr(Args, Delay);   // ★ R5-fix-b 13.11.2026 (третий заход): `delay=N` — общая точка (была ветка цикла аргументов)
			// ★ Волна В8 (ревизия В7 С-3): только офлайн — при живом соединении отказ, иначе окно защиты отправителей.
			const FAsdaSelfTestArgs KitArgs = FAsdaSelfTestArgs::Parse(Args);   // ★ 11.10.2026 (В3 S1): каркас — отказ с ИТОГ и quit, второй барьер на все опкоды
			if (AsdaSelfTestKit::RefuseStart(W, TEXT("asda.Craft.SelfTest"), KitArgs)) { return; }
			struct FStat { int32 Ok = 0; int32 Fail = 0; };
			TSharedRef<FStat> Stat = MakeShared<FStat>();
			// ★ Р-2 (ревизия shopdbl2, 10.10.2026): офлайн проверяется повторно в момент ОТЛОЖЕННОГО старта — первым сработавшим
			//   шагом; при отказе все шаги прогона пропускаются (итог отказа пишет `RefuseDeferredStartWhenOnline`).
			// ★ 11.10.2026 (В3 S1): своя копия флага (FGate) заменена общим `FAsdaSelfTestRunGate` — поведение то же.
			TSharedRef<FAsdaSelfTestRunGate> Gate = MakeShared<FAsdaSelfTestRunGate>(TEXT("asda.Craft.SelfTest"), bQuit);   // общий флаг отложенного старта
			auto At = [W, Win, Delay, Gate, bQuit](float T, TFunction<void()> Fn)
			{
				FTimerHandle H;
				TWeakObjectPtr<UWorld> WW(W);   // мир прогона (для отказа)
				W->GetTimerManager().SetTimer(H, FTimerDelegate::CreateWeakLambda(Win, [Gate, WW, bQuit, Fn = MoveTemp(Fn)]()
				{
					if (!Gate->Pass(WW.Get())) { return; }   // Р-2: офлайн повторно — на отложенном старте
					Fn();
				}), T + Delay, false);
			};
			// ★ Ревизия В3 (мелкое): префикс имён кадров — параметр `prefix=` (умолчание `asda_craft_w012_`).
			auto Shot = [bShot, ShotPrefix](const TCHAR* Name)
			{
				if (!bShot) { return; }
				FString N(Name);
				static const FString kDef(TEXT("asda_craft_w012_"));
				if (N.StartsWith(kDef)) { N = ShotPrefix + N.Mid(kDef.Len()); }
				UE_LOG(LogAsdaCraftWin, Warning, TEXT("САМОТЕСТ-76000: КАДР → %s.png"), *N);
				FScreenshotRequest::RequestScreenshot(N, /*bShowUI=*/true, /*bAddFilenameSuffix=*/false);
			};
			auto Check = [Stat](bool bOk, const FString& What)
			{
				if (bOk) { ++Stat->Ok; } else { ++Stat->Fail; }
				UE_LOG(LogAsdaCraftWin, Warning, TEXT("САМОТЕСТ-76000 %s %s"), bOk ? TEXT("✔") : TEXT("✘ ПРОВАЛ"), *What);
			};
			auto Dump = [Win, Craft](const TCHAR* Step)
			{
				UE_LOG(LogAsdaCraftWin, Warning, TEXT("САМОТЕСТ-76000: %s\n%s%s"), Step, *Craft->DumpState(), *Win->DumpState());
				for (const FString& L : Win->GetJournal()) { UE_LOG(LogAsdaCraftWin, Warning, TEXT("  журнал окна: %s"), *L); }
				for (const FString& L : Craft->GetNoticeJournal()) { UE_LOG(LogAsdaCraftWin, Warning, TEXT("  журнал нотисов: %s"), *L); }
				Win->ClearJournal(); Craft->ClearNoticeJournal();
			};
			auto JournalHas = [Win](const TCHAR* What) { for (const FString& L : Win->GetJournal()) { if (L.Contains(What)) { return true; } } return false; };
			auto NoticeHas = [Craft](const TCHAR* What) { for (const FString& L : Craft->GetNoticeJournal()) { if (L.Contains(What)) { return true; } } return false; };
			auto PC = [W]() { return W ? Cast<AAsdaPlayerController>(W->GetFirstPlayerController()) : nullptr; };
			auto SetCVar = [](const TCHAR* Name, int32 V) { AsdaCVarSave::SetForTest(Name, V); };   // ★ ИСПРАВЛЕНИЕ 11.10.2026 (В0, N1(д)/U8): исходное запоминается
			// Изученные рецепты теста: 17 шт. (3 страницы на «ВСЕ»), все четыре категории.
			static const TArray<int32> kLearned = { 1, 2, 3, 4, 5, 6, 7, 8, 59, 82, 105, 214, 215, 216, 222, 233, 234 };

			// ── Шаг 1: разбор контролов, 6597 боевым приёмником, материалы рецепта 1 в сумку ──
			At(1.0f, [Win, Craft, Inv, WM, Check]()
			{
				WM->CloseWindow(FName(TEXT("ONEBUT_MSGBOX")));   // нотисы прошлых самотестов не закрывают кадры
				Craft->DebugInject(6597, UAsdaCraftSubsystem::DebugBuild6597(kLearned, 35, 3, 120, 40));
				// Рецепт 1 «абразив» = 20931 ×3 — кладём ровно 3 (строка НЕ затемнена); рецепт 2 — материалов нет.
				for (int32 C = 48; C < 60; ++C) { Inv->DebugPutItem(2, C, 0, 0); }
				Inv->DebugPutItem(2, 50, 20931, 3);
				Check(Craft->GetRecipeCount() == 249 && Craft->GetRateCount() == 249,
					FString::Printf(TEXT("таблицы: рецептов %d, rate %d (ждали 249/249)"), Craft->GetRecipeCount(), Craft->GetRateCount()));
				const FAsdaMakingRateRow* R1 = Craft->FindRate(1);
				Check(R1 && R1->OutItem0 == 20962, TEXT("making_rate запись 1: OutItem0 = 20962"));
				const FAsdaMakingRecipeRow* R222 = Craft->FindRecipe(222);
				Check(R222 && R222->ResultCode == 72750, TEXT("рецепт 222 → благословенный 72750 (ведомость Д-Д1)"));
			});
			At(1.3f, [Craft, Check]()
			{
				Check(Craft->HasReceivedList() && Craft->GetLearnedCount() == kLearned.Num() && Craft->GetCraftLevel() == 3
					&& Craft->GetCraftPercent() == 40 && Craft->GetRecipeLimit() == 35,
					FString::Printf(TEXT("6597 принят: изучено %d, ур. %d, процент %d, лимит %d"), Craft->GetLearnedCount(),
						Craft->GetCraftLevel(), Craft->GetCraftPercent(), Craft->GetRecipeLimit()));
				Check(Craft->GetLimitBonus() == 35 - Craft->ClientLimitFormula(),
					FString::Printf(TEXT("К-8: надбавка %d = 35 − формула клиента %d"), Craft->GetLimitBonus(), Craft->ClientLimitFormula()));
			});
			// ── Шаг 2: вход клавишей Z (действие 41) — боевой диспетчер команд контроллера ──
			At(1.5f, [Win, PC, Check]()
			{
				if (AAsdaPlayerController* P = PC()) { P->RunCommandForCheck(41); }
				else { Win->GateAndToggle(TEXT("самотест: Z без контроллера")); }
				Check(Win->IsOpen(), TEXT("Z (действие 41) открыл окно 76000"));
				UE_LOG(LogAsdaCraftWin, Warning, TEXT("%s"), *Win->DumpControls());
				Check(Win->GetFoundControls() == UAsdaCraftWindowSubsystem::kControlsTotal,
					FString::Printf(TEXT("разбор контролов: %d/%d"), Win->GetFoundControls(), UAsdaCraftWindowSubsystem::kControlsTotal));
			});
			At(1.8f, [W]() { GEngine->Exec(W, TEXT("asda.UI.TreeGeom craft_open WBP_ITEM_making_WIN")); });
			At(2.0f, [Shot]() { Shot(TEXT("asda_craft_w012_open")); });
			At(2.2f, [Win, Craft, Check, Dump]()
			{
				Check(Win->DebugText(76001) == TEXT("Создание предмета"), FString::Printf(TEXT("76001 = «%s»"), *Win->DebugText(76001)));
				Check(Win->DebugText(76096) == TEXT("Удалить инструкцию"), FString::Printf(TEXT("76096 = «%s»"), *Win->DebugText(76096)));
				Check(Win->DebugText(76004) == TEXT("3"), FString::Printf(TEXT("76004 уровень = «%s»"), *Win->DebugText(76004)));
				Check(Win->DebugText(76006) == TEXT("17 / 35"), FString::Printf(TEXT("76006 «N / M» = «%s»"), *Win->DebugText(76006)));
				Check(Win->DebugText(76080) == TEXT("1") && Win->DebugText(76081) == TEXT("3"),
					FString::Printf(TEXT("страницы «%s»/«%s» (ждали 1/3)"), *Win->DebugText(76080), *Win->DebugText(76081)));
				Check(FMath::IsNearlyEqual(Win->DebugBarFrac(), 0.4f, 0.005f), /* ★ 11.11.2026 (D4b): допуск полпроцента — доля полосы считается делением, в Dev-сборке младшие биты иные */ FString::Printf(TEXT("полоса 76005 = %.2f (ждали 0.40)"), Win->DebugBarFrac()));
				Check(Win->DebugText(7600704) == TEXT("ВСЕ") && Win->DebugText(7600700) == TEXT("Оружие"), TEXT("подписи вкладок 5328/5540"));
				Check(Win->GetRowRecipe(0) == 1 && !Win->DebugRowBlack(0), TEXT("строка 0 = рецепт 1, материалы есть — не затемнена"));
				Check(Win->GetRowRecipe(1) == 2 && Win->DebugRowBlack(1), TEXT("строка 1 = рецепт 2, материалов нет — затемнена"));
				Check(Win->DebugText(76024) == TEXT("абразив") || !Win->DebugText(76024).IsEmpty(), FString::Printf(TEXT("t4 строки 0 = «%s»"), *Win->DebugText(76024)));
				Dump(TEXT("после открытия"));
			});
			// ── Шаг 3: все входы — тумблер Z, доп-меню 5, X, NPC 30088, «Закрыть», кнопка инвентаря 452, NPC 30010 ──
			At(2.6f, [Win, PC, GI, Check]()
			{
				if (AAsdaPlayerController* P = PC()) { P->RunCommandForCheck(41); }
				Check(!Win->IsOpen(), TEXT("Z повторно — окно закрыто (тумблер)"));
				if (UAsdaExtraMenuSubsystem* EM = GI->GetSubsystem<UAsdaExtraMenuSubsystem>()) { EM->DispatchCommand(5, TEXT("самотест")); }
				Check(Win->IsOpen(), TEXT("доп-меню команда 5 открыла окно"));
				Win->DebugPressButton(76099);
				Check(!Win->IsOpen(), TEXT("76099 XX закрыл окно"));
				if (UAsdaNpcDialogSubsystem* Dlg = GI->GetSubsystem<UAsdaNpcDialogSubsystem>()) { Dlg->RunCommandForCheck(30088, 0); }
				Check(Win->IsOpen(), TEXT("NPC 30088 открыл окно"));
				Win->DebugPressButton(76097);
				Check(!Win->IsOpen(), TEXT("76097 «Закрыть» закрыл окно"));
				if (UAsdaNpcDialogSubsystem* Dlg = GI->GetSubsystem<UAsdaNpcDialogSubsystem>()) { Dlg->RunCommandForCheck(30009, 0); }
				Check(Win->IsOpen(), TEXT("NPC 30009 открыл окно"));
				if (UAsdaNpcDialogSubsystem* Dlg = GI->GetSubsystem<UAsdaNpcDialogSubsystem>()) { Dlg->RunCommandForCheck(30009, 0); }
				Check(Win->IsOpen(), TEXT("NPC 30009 при открытом окне: закрыть и открыть заново — окно открыто"));
				if (UAsdaNpcDialogSubsystem* Dlg = GI->GetSubsystem<UAsdaNpcDialogSubsystem>()) { Dlg->RunCommandForCheck(30010, 0); }
				Check(!Win->IsOpen(), TEXT("NPC 30010 закрыл окно"));
				// ESC окно НЕ закрывает (как оригинал): проверка с выключенным списком порта `asda.UI.EscExtraWindows`.
				Win->GateAndToggle(TEXT("самотест ESC"));
				IConsoleVariable* EscExtra = IConsoleManager::Get().FindConsoleVariable(TEXT("asda.UI.EscExtraWindows"));
				const int32 EscExtraWas = EscExtra ? EscExtra->GetInt() : 0;
				if (EscExtra) { EscExtra->Set(0, ECVF_SetByConsole); }
				if (UAsdaWindowSubsystem* WMx = GI->GetSubsystem<UAsdaWindowSubsystem>()) { WMx->CloseTopmostOnEsc(); }
				if (EscExtra) { EscExtra->Set(EscExtraWas, ECVF_SetByConsole); }
				Check(Win->IsOpen(), TEXT("ESC не закрыл окно 76000"));
				Check(UAsdaCraftSubsystem::IsAnyCraftWindowOpen(GI), TEXT("единый предикат «окно ремесла открыто» (гейт клика по миру, питомец, гача)"));
				Win->OnCloseClicked();
				// Кнопка инвентаря 452 — настоящим кликом Slate по кнопке окна сумки.
				UAsdaInventorySubsystem* InvX = GI->GetSubsystem<UAsdaInventorySubsystem>();
				if (InvX && !InvX->IsWindowOpen()) { InvX->OpenWindow(); }
			});
			At(3.0f, [GI, Check]()
			{
				UAsdaWindowSubsystem* WMx = GI->GetSubsystem<UAsdaWindowSubsystem>();
				UUserWidget* InvW = WMx ? WMx->FindWindow(FName(TEXT("INVEN_WIN"))) : nullptr;
				UWidget* B = InvW ? InvW->GetWidgetFromName(FName(TEXT("Inven_Smelting_Btn"))) : nullptr;
				FVector2D Mn, Mx;
				const bool bGeo = B && AsdaUi::WidgetAbsRect(B, Mn, Mx);
				Check(bGeo, TEXT("кнопка 452 `Inven_Smelting_Btn` найдена в окне сумки (живая геометрия)"));
				if (bGeo) { WMx->SimulateClickAt((Mn + Mx) * 0.5f, /*HoldFrames*/ 3); }
			});
			At(3.6f, [Win, GI, Check]()
			{
				Check(Win->IsOpen(), TEXT("кнопка инвентаря 452 (клик Slate) открыла окно 76000"));
				if (UAsdaInventorySubsystem* InvX = GI->GetSubsystem<UAsdaInventorySubsystem>()) { if (InvX->IsWindowOpen()) { InvX->CloseWindow(); } }
				if (!Win->IsOpen()) { Win->GateAndToggle(TEXT("самотест: повтор открытия")); }
			});
			// ── Шаг 4: вкладки (радио-бар 76007) ──
			At(4.0f, [Win, Craft, Check]()
			{
				Win->DebugPressButton(76007000);   // «Оружие»
				int32 Want = 0;
				for (int32 Id : kLearned) { const FAsdaMakingRecipeRow* R = Craft->FindRecipe(Id); if (R && R->Category == 0) { ++Want; } }
				Check(Win->GetTab() == 0 && Win->GetFilteredCount() == Want, FString::Printf(TEXT("вкладка «Оружие»: строк %d (ждали %d)"), Win->GetFilteredCount(), Want));
			});
			At(4.2f, [Shot]() { Shot(TEXT("asda_craft_w012_tab0")); });
			At(4.4f, [Win, Check]()
			{
				Win->DebugPressButton(76007004);   // «ВСЕ»
				Check(Win->GetTab() == 4 && Win->GetFilteredCount() == kLearned.Num() && Win->GetPage() == 1, TEXT("вкладка «ВСЕ»: 17 строк, страница 1"));
			});
			// ── Шаг 5: выбор строки НАСТОЯЩИМ кликом, рамка 76077 (смещение +5 от JUI), листание, К-2 ──
			At(4.6f, [Win, GI, Check]()
			{
				FVector2D C;
				const bool bOk = Win->DebugRowCenter(2, C);
				Check(bOk, TEXT("центр строки 2 по живой геометрии"));
				if (bOk) { GI->GetSubsystem<UAsdaWindowSubsystem>()->SimulateClickAt(C, 3); }
			});
			At(5.0f, [Win, Check, W]()
			{
				Check(Win->GetSelRow() == 2 && Win->DebugFrameVisible(), FString::Printf(TEXT("клик Slate по строке 2: выбрана %d, рамка видна %d"), Win->GetSelRow(), Win->DebugFrameVisible() ? 1 : 0));
				const FVector2D P = Win->DebugFrameSlotPos();
				Check(FMath::IsNearlyEqual((float)P.X, 13.f) && FMath::IsNearlyEqual((float)P.Y, 29.f + (23.f + 37.f * 2.f - 24.f)),
					FString::Printf(TEXT("рамка 76077 в канвасе (%.0f, %.0f) = база ассета (13,29) + код (13, 37·2+23) − JUI (13,24)"), P.X, P.Y));
				Check(Win->GetSelectedRecipe() == 3, FString::Printf(TEXT("рецепт окна состава 77000 = %d (строка 2 = рецепт 3; окно открыто — волна В3)"), Win->GetSelectedRecipe()));
				GEngine->Exec(W, TEXT("asda.UI.TreeGeom craft_sel WBP_ITEM_making_WIN"));
			});
			At(5.2f, [Shot]() { Shot(TEXT("asda_craft_w012_select")); });
			At(5.4f, [Win, Craft, SetCVar, Check]()
			{
				// К-2 исправлено: листание снимает выбор строки, а «Удалить» шлёт рецепт, ПОКАЗАННЫЙ в окне состава 77000
				//   (ревизия В0–В2 М-8, решено в В3), — не строку чужой страницы.
				SetCVar(TEXT("asda.Craft.FixDeleteAfterPaging"), 1);
				Craft->DebugCaptured.Reset();
				const bool bArm = Craft->DebugCaptureOnce(6600);
				Win->DebugPressButton(76083);
				Check(Win->GetPage() == 2 && Win->GetSelRow() == -1 && !Win->DebugFrameVisible(), TEXT("К-2 (1): «вперёд» → страница 2, выбор снят, рамка спрятана"));
				Win->DebugPressButton(76096);
				const int32 SentK2 = (Craft->DebugCaptured.Num() == 1 && Craft->DebugCaptured[0].Value.Num() == 2) ? AsdaBytes::RdI16(Craft->DebugCaptured[0].Value.GetData()) : -1;
				Check(bArm && SentK2 == 3, FString::Printf(TEXT("К-2 (1): «Удалить» после листания шлёт рецепт окна состава (3), ушло %d"), SentK2));
				Craft->DebugDisarmCapture(TEXT("шаг К-2 (1)"));
				Craft->DebugReleaseInFlight();   // перехваченный пакет ответа не получит
			});
			At(5.6f, [Shot]() { Shot(TEXT("asda_craft_w012_page2")); });
			At(5.8f, [Win, Craft, Check]()
			{
				// Удаление без листания (исправленный путь): выбрана строка 1 страницы 2 = рецепт kLearned[7].
				Win->OnRowClicked(1);
				Craft->DebugCaptured.Reset();
				Craft->DebugCaptureOnce(6600);
				Win->DebugPressButton(76096);
				const bool bGot = Craft->DebugCaptured.Num() == 1 && Craft->DebugCaptured[0].Value.Num() == 2;
				Check(bGot && AsdaBytes::RdI16(Craft->DebugCaptured[0].Value.GetData()) == kLearned[7],
					FString::Printf(TEXT("6600 перехвачен: тело 2 б, id = %d (ждали %d)"), bGot ? AsdaBytes::RdI16(Craft->DebugCaptured[0].Value.GetData()) : -1, kLearned[7]));
				Check(!Craft->DebugIsCaptureArmed(), TEXT("перехват 6600 снялся сам после первого пакета"));
				Check(Win->GetSelRow() == -1, TEXT("после «Удалить» — `uiPanelHideAndReset`: выбор снят"));
				// ★ Замок «операция ремесла в полёте» (пара P35): 6600 без ответа — следующий запрос не уходит.
				Check(Craft->IsRequestInFlight() && Craft->GetInFlightOpcode() == 6600, TEXT("замок: 6600 в полёте"));
				Check(!Craft->SendForgetRecipe(kLearned[8]), TEXT("замок: второй запрос ремесла до ответа не ушёл"));
				// Ответ 6601 (любой код, здесь 99) снимает замок.
				TArray<uint8> D99; D99.SetNumZeroed(3); D99[0] = 99; Craft->DebugInject(6601, D99);
			});
			At(5.9f, [Craft, Check, NoticeHas]()
			{
				Check(!Craft->IsRequestInFlight() && NoticeHas(TEXT("msgbox 5563")), TEXT("замок снят ответом 6601 (код 99 → ONEBUT 5563)"));
			});
			At(6.0f, [Win, Craft, SetCVar, Check]()
			{
				// К-2 как оригинал: выбор переживает листание; на стр. 3 строки 5 нет → «вылет» (у нас отказ, пакета нет).
				SetCVar(TEXT("asda.Craft.FixDeleteAfterPaging"), 0);
				Win->OnTabSelected(4);
				Win->DebugPressButton(76083);   // стр. 2
				Win->OnRowClicked(5);
				Win->DebugPressButton(76083);   // стр. 3 (5 строк)
				Craft->DebugCaptured.Reset();
				Craft->DebugCaptureOnce(6600);
				Win->DebugPressButton(76096);
				Check(Craft->DebugCaptured.Num() == 0 && Win->GetPage() == 3, TEXT("К-2 (0): пустая строка после листания — отказ вместо вылета, 6600 не ушёл"));
				Craft->DebugDisarmCapture(TEXT("шаг К-2 (0) а"));
				// Строка 4 стр. 2, листание → стр. 3: оригинал удаляет ЧУЖОЙ рецепт (строки 4 новой страницы).
				Win->OnTabSelected(4);
				Win->DebugPressButton(76083);
				Win->OnRowClicked(4);
				const int32 Chosen = Win->GetSelectedRecipe();
				Win->DebugPressButton(76083);
				Craft->DebugCaptured.Reset();
				Craft->DebugCaptureOnce(6600);
				Win->DebugPressButton(76096);
				const int32 Sent = Craft->DebugCaptured.Num() == 1 ? AsdaBytes::RdI16(Craft->DebugCaptured[0].Value.GetData()) : -1;
				Check(Sent == kLearned[16] && Sent != Chosen, FString::Printf(TEXT("К-2 (0): выбран %d, но ушёл 6600 с %d — дефект воспроизведён"), Chosen, Sent));
				Craft->DebugDisarmCapture(TEXT("шаг К-2 (0) б"));
				SetCVar(TEXT("asda.Craft.FixDeleteAfterPaging"), 1);
				Win->OnTabSelected(4);
			});
			// ── Шаг 6: приёмники 6599/6601/6605/6603 (боевые, через очередь) ──
			At(6.4f, [Craft, Inv, Check]()
			{
				// 6599 код 1: свиток в расходниках (кол-во 1 → 0 ⇒ ячейка удаляется), рецепт 9 изучен.
				Inv->DebugPutItem(2, 55, 71712, 1);
				TArray<uint8> B; B.SetNumZeroed(64);
				B[0] = 1; AsdaBytes::WrI32(&B[1], 0);
				AsdaBytes::WrI32(&B[5], 71712); B[5 + 4] = 2; AsdaBytes::WrI16(&B[5 + 5], 55); AsdaBytes::WrI32(&B[5 + 9], 0);
				AsdaBytes::WrI16(&B[62], 9);
				Craft->DebugInject(6599, B);
				TArray<uint8> B12; B12.SetNumZeroed(64); B12[0] = 12; Craft->DebugInject(6599, B12);
			});
			At(6.7f, [Win, Craft, Inv, Check, NoticeHas]()
			{
				Check(Craft->IsLearned(9) && Craft->GetLearnedCount() == kLearned.Num() + 1, TEXT("6599 код 1: рецепт 9 изучен, счётчик +1"));
				const FAsdaItemInstance* C55 = Inv->GetCell(2, 55);
				Check(C55 && C55->IsEmpty(), TEXT("6599: запись свитка count 0 ⇒ ячейка удалена (режим CountOrClear)"));
				Check(!Win->IsOpen(), TEXT("6599 код 1 закрыл окно 76000 (`uiWindowHideAndCleanup`)"));
				Check(NoticeHas(TEXT("chat+plank 5572")) && NoticeHas(TEXT("msgbox 5562")), TEXT("нотисы: 5572 чат+плашка, код 12 → ONEBUT 5562"));
				TArray<uint8> D; D.SetNumZeroed(3); D[0] = 1; AsdaBytes::WrI16(&D[1], 9); Craft->DebugInject(6601, D);
				TArray<uint8> D11; D11.SetNumZeroed(3); D11[0] = 11; Craft->DebugInject(6601, D11);
				TArray<uint8> L; L.SetNumZeroed(61); L[0] = 1; L[1] = 4; Craft->DebugInject(6605, L);
				TArray<uint8> L2; L2.SetNumZeroed(61); L2[0] = 2; Craft->DebugInject(6605, L2);
			});
			At(7.0f, [Craft, Check, NoticeHas, Dump]()
			{
				Check(!Craft->IsLearned(9) && Craft->GetLearnedCount() == kLearned.Num(), TEXT("6601 код 1: рецепт 9 удалён, счётчик −1"));
				Check(Craft->GetCraftLevel() == 4, FString::Printf(TEXT("6605 код 1: уровень ремесла %d"), Craft->GetCraftLevel()));
				Check(NoticeHas(TEXT("msgbox 5564")) && NoticeHas(TEXT("chat+plank 5583")) && NoticeHas(TEXT("msgbox 5581")),
					TEXT("нотисы: 6601 код 11 → 5564, 6605 код 1 → 5583 чат+плашка, код 2 → 5581"));
				Dump(TEXT("после 6599/6601/6605"));
			});
			At(7.2f, [Craft, Inv, Check]()
			{
				// 6603 код 1: материал 20931 ×3 израсходован (count 0 ⇒ удалить), продукт 20962 в расходники 52 (К-1: сразу).
				Inv->DebugPutItem(2, 52, 0, 0);
				TArray<uint8> B; B.SetNumZeroed(525);
				B[0] = 1; AsdaBytes::WrI16(&B[1], 0); B[3] = 5; AsdaBytes::WrI32(&B[4], 300); AsdaBytes::WrI16(&B[8], 55);
				B[10] = 1; B[11] = 0;
				AsdaBytes::WrI32(&B[12], 20962); B[12 + 4] = 2; AsdaBytes::WrI16(&B[12 + 5], 52); AsdaBytes::WrI32(&B[12 + 9], 1);
				AsdaBytes::WrI32(&B[69], 20931); B[69 + 4] = 2; AsdaBytes::WrI16(&B[69 + 5], 50); AsdaBytes::WrI32(&B[69 + 9], 0);
				Craft->DebugInject(6603, B);
				TArray<uint8> B10; B10.SetNumZeroed(525); B10[0] = 10; Craft->DebugInject(6603, B10);
			});
			At(7.5f, [Craft, Inv, Check, NoticeHas]()
			{
				Check(Craft->GetCraftLevel() == 5 && Craft->GetCraftExp() == 300 && Craft->GetCraftPercent() == 55,
					TEXT("6603 код 1: ур. 5, опыт 300, процент 55"));
				Check(Craft->GetRecipeLimit() == Craft->ClientLimitFormula() + Craft->GetLimitBonus(), TEXT("6603: лимит пересчитан с надбавкой (К-8)"));
				const FAsdaItemInstance* M = Inv->GetCell(2, 50);
				const FAsdaItemInstance* P = Inv->GetCell(2, 52);
				Check(M && M->IsEmpty(), TEXT("6603: материал count 0 ⇒ ячейка удалена"));
				Check(P && P->ItemCode == 20962, TEXT("6603: продукт 20962 положен сразу (К-1, окна 77500 нет)"));
				Check(NoticeHas(TEXT("msgbox 5566")), TEXT("6603 код 10 → ONEBUT 5566 (К-5)"));
			});
			// ── Шаг 7: сброс сессии — портал сохраняет модель (Р-1), вход в мир чистит ──
			At(7.8f, [Craft, GI, Check]()
			{
				UAsdaSessionResetSubsystem* SR = GI->GetSubsystem<UAsdaSessionResetSubsystem>();
				const int32 Before = Craft->GetLearnedCount();
				if (SR) { SR->Broadcast(EAsdaSessionResetReason::MapChanged); }
				Check(Craft->GetLearnedCount() == Before && Craft->HasReceivedList(), TEXT("«портал» (MapChanged): модель ремесла сохранена (ревизия Р-1)"));
				// Новый 6597 после портала перезаписывает всё целиком.
				Craft->DebugInject(6597, UAsdaCraftSubsystem::DebugBuild6597({ 1, 2, 59 }, 36, 6, 999, 12));
			});
			At(8.1f, [Craft, Win, Check]()
			{
				Check(Craft->GetLearnedCount() == 3 && Craft->GetCraftLevel() == 6 && Craft->GetRecipeLimit() == 36,
					TEXT("6597 после «портала» принят: 3 рецепта, ур. 6, лимит 36"));
				Win->GateAndToggle(TEXT("самотест после портала"));
				Check(Win->IsOpen() && Win->DebugText(76006) == FString::Printf(TEXT("3 / %d"), Craft->GetRecipeLimit()),
					FString::Printf(TEXT("окно после портала: «%s»"), *Win->DebugText(76006)));
			});
			At(8.3f, [Shot]() { Shot(TEXT("asda_craft_w012_after_portal")); });
			// ── Шаг 8: отправители 6598/6602/6604 — перехват тела (узкий, свой опкод, снимается сам) ──
			At(8.6f, [Craft, Check]()
			{
				auto One = [Craft](int32 Op, TFunction<void()> Send) -> TArray<uint8>
				{
					Craft->DebugCaptured.Reset();
					if (!Craft->DebugCaptureOnce(Op)) { return TArray<uint8>(); }
					Send();
					Craft->DebugDisarmCapture(TEXT("шаг отправителей"));
					Craft->DebugReleaseInFlight();   // перехваченный пакет ответа не получит — замок снимаем вручную
					return Craft->DebugCaptured.Num() == 1 ? Craft->DebugCaptured[0].Value : TArray<uint8>();
				};
				const TArray<uint8> B98 = One(6598, [Craft]() { Craft->SendLearnRecipe(7, 2); });
				Check(B98.Num() == 3 && AsdaBytes::RdI16(B98.GetData()) == 7 && B98[2] == 2, TEXT("6598 тело 3 б: слот 7, контейнер 2"));
				const TArray<uint8> B04 = One(6604, [Craft]() { Craft->SendLevelUpScroll(11, 2); });
				Check(B04.Num() == 3 && AsdaBytes::RdI16(B04.GetData()) == 11 && B04[2] == 2, TEXT("6604 тело 3 б: слот 11, контейнер 2"));
				const TArray<int32> Slots = { 50, 51, -1 };
				const TArray<uint8> Conts = { 2, 1, 0 };
				const TArray<uint8> B02 = One(6602, [Craft, Slots, Conts]() { Craft->SendCraftEnter(59, Slots, Conts); });
				const TArray<uint8> Ref = UAsdaNetSubsystem::BuildCraftEnter6602Body(59, Slots, Conts);
				FString Hex; for (uint8 X : B02) { Hex += FString::Printf(TEXT("%02X "), X); }
				Check(B02.Num() == 26 && B02 == Ref && AsdaBytes::RdI16(B02.GetData()) == 59 && AsdaBytes::RdI16(B02.GetData() + 20) == -1
					&& AsdaBytes::RdI16(B02.GetData() + 22) == -1 && B02[24] == 0 && B02[25] == 0,
					FString::Printf(TEXT("6602 тело 26 б (катализаторы −1/0): %s"), *Hex));
				Check(!Craft->DebugIsCaptureArmed(), TEXT("перехватчик снят после всех отправителей"));
				// Таймаут замка: ответа нет — через `asda.Craft.ReplyTimeoutSec` ожидание снимается само.
				AsdaCVarSave::SetForTest(TEXT("asda.Craft.ReplyTimeoutSec"), 0.2f);
				Craft->DebugCaptured.Reset();
				Craft->DebugCaptureOnce(6604);
				Craft->SendLevelUpScroll(11, 2);
				Check(Craft->IsRequestInFlight(), TEXT("таймаут: 6604 в полёте"));
			});
			At(8.9f, [Craft, Check]()
			{
				Check(!Craft->IsRequestInFlight(), TEXT("таймаут: без ответа замок снят сам (молчание сервера не даёт тупика)"));
				AsdaCVarSave::SetForTest(TEXT("asda.Craft.ReplyTimeoutSec"), 20.f);   // значение на время остатка прогона; исходное вернёт итог
			});
			// ── Шаг 9: смерть закрывает окно; вход в мир чистит модель ──
			At(9.0f, [Win, Craft, GI, Check, Dump]()
			{
				UAsdaSessionResetSubsystem* SR = GI->GetSubsystem<UAsdaSessionResetSubsystem>();
				if (SR) { SR->Broadcast(EAsdaSessionResetReason::EnteringWorld); }
				Check(!Craft->HasReceivedList() && Craft->GetLearnedCount() == 0 && !Win->IsOpen(), TEXT("вход в мир (EnteringWorld): модель очищена, окно скрыто"));
				Dump(TEXT("финал"));
			});
			At(9.5f, [Stat, bQuit, W]()
			{
				// ★ ИСПРАВЛЕНИЕ 11.10.2026 (В0, N1(д)/U8): ключи прогона (Craft.FixDeleteAfterPaging, Craft.ReplyTimeoutSec) — к исходному значению
				//   И приоритету SetBy (прежде — константы 1 / 20 с SetByCode).
				AsdaCVarSave::RestoreAll(TEXT("asda.Craft.SelfTest"));
				AsdaSelfTestKit::Summary(LogAsdaCraftWin, TEXT("САМОТЕСТ-76000"), Stat->Ok, Stat->Fail, TEXT(""), true);   // ★ 11.10.2026 (В3 S1): единый итог
				AsdaSelfTestKit::Quit(W, bQuit);
			});
		}));
#endif // ASDA_DEV_CONSOLE
}
