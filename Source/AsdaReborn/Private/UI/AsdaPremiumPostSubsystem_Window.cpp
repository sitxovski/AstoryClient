// Copyright (c) 2026 @sitxovski (Discord)
// AsdaPremiumPostSubsystem_Window.cpp — ★ R3-c 12.11.2026 (разрез AsdaPremiumPostSubsystem.cpp, дословный перенос): окна и контролы, открыть/закрыть, строки, сетка, выделение.
//   Общее для частей — в AsdaPremiumPostSubsystem_Private.h; шапка и реверс — в AsdaPremiumPostSubsystem.cpp.
#include "UI/AsdaPremiumPostSubsystem.h"
#include "AsdaDevConsole.h"   // ★ античит А2: ASDA_DEV_CVAR / ASDA_DEV_CONSOLE — дев-ключи и команды вне Shipping
#include "UI/AsdaNotice.h"   // ★ U13 (дедуп №2, В2-A): ConsoleEcho — общее эхо консольных команд

#include "AsdaNetSubsystem.h"
#include "Blueprint/UserWidget.h"
#include "Components/Button.h"
#include "Framework/Application/SlateApplication.h"   // живой хит-тест: кого Slate реально видит под точкой
#include "Components/CanvasPanelSlot.h"
#include "Components/Image.h"
#include "Components/PanelWidget.h"
#include "Components/TextBlock.h"
#include "Engine/GameInstance.h"
#include "Login/AsdaButtonRelay.h"
#include "Text/AsdaTextSubsystem.h"
#include "UI/AsdaAnimatedPlane.h"
#include "UI/AsdaIconCellGrid.h"
#include "UI/AsdaInventorySubsystem.h"
#include "UI/AsdaItemTooltipSubsystem.h"
#include "UI/AsdaHotbarSubsystem.h"
#include "UI/AsdaPremiumGachaSubsystem.h"
#include "UI/AsdaUiHelpers.h"
#include "UI/AsdaWindowSubsystem.h"
#include "AsdaOutgoingGate.h"               // ★ С-Р1: общий офлайн-гейт отладочных команд (RefuseDebugWhenOnline)
#include "Test/AsdaSelfTestKit.h"            // ★ 25.10.2026: каркас самотеста (quit, итог)
#include "TimerManager.h"
#include "Engine/World.h"
#include "AsdaPremiumPostSubsystem_Private.h"   // ★ R3-c 12.11.2026: общее для частей AsdaPremiumPostSubsystem*.cpp

// ★ 08.11.2026 (пачка 19): полная остановка своего персонажа из клика UI (World/AsdaUiActionMoveHold.cpp, ключ `asda.Move.UiActionStop`).
void AsdaFullStopSelfForUiAction(class UGameInstance* GI, const TCHAR* Why);

namespace {
	const TCHAR* kWinPath = TEXT("/Game/AsdaAssets/UI/Player_UI/Premium/WBP_Primium_Item_post_WIN");
	// Выше окна инвентаря (`FAsdaInvCellUi::kInvenZ` = 720): по требованию владельца оба окна
	//   открыты одновременно, и ящик обязан лежать сверху (эталон делает `CClass_MapInsert` —
	//   «вывести наверх» — сразу после показа, `uiBagWindow_OpenAndSync` шаг 3).
	constexpr int32 kWinZ = 730;

	// ── строки таблицы текста (эталон `uiInitMixWindowGrid@0xEEFE60`) ────────────────────────────
	constexpr int32 kLocTitle      = 110666;   // 69001 «Элитный почтовый ящик»
	constexpr int32 kLocNote       = 110667;   // 69002 «Одновременно отображается 7 покупок»
	constexpr int32 kLocOk         = 110668;   // 69082 «Положить в рюкзак»
	constexpr int32 kLocClose      = 110669;   // 69083 «Закрыть»

	// ── UV радиокнопки `Smelting_V7Btn_2` из `JuiSkin.ini` (не подобраны — прочитаны из данных) ──
	//   SS 0/1 (обычная): 0.479 0.176 … 0.514 0.213 — это состояние стоит в импортированном WBP.
	//   SS 2/3 (выбранная): 0.959 0.449 … 0.994 0.486.
	const FBox2D kRadioUvNormal(FVector2D(0.479f, 0.176f), FVector2D(0.514f, 0.213f));
	const FBox2D kRadioUvChosen(FVector2D(0.959f, 0.449f), FVector2D(0.994f, 0.486f));

	// Имя группы иконок строки: `Primium_Item_post_MARK_R1_grp` … `_R7_grp`.
	FString RowIconGroupName(int32 Row) { return FString::Printf(TEXT("Primium_Item_post_MARK_R%d_grp"), Row + 1); }
	// Имя группы самой строки: `Primium_Item_post_L1_grp` … `_L7_grp`.
	FString RowGroupName(int32 Row)     { return FString::Printf(TEXT("Primium_Item_post_L%d_grp"), Row + 1); }

	// ★ Волна 1 (14.09.2026, В3): правило видимости — общая точка `AsdaUi::SetShownDecor` (режим тот же).
	void SetVis(UWidget* W, bool bVisible)
	{
		AsdaUi::SetShownDecor(W, bVisible);
	}
}

// ═══════════════════════════════ ОКНО ══════════════════════════════════════════════════════════
bool UAsdaPremiumPostSubsystem::EnsureWindows()
{
	UAsdaWindowSubsystem* WM = GetGameInstance() ? GetGameInstance()->GetSubsystem<UAsdaWindowSubsystem>() : nullptr;
	if (!WM) { return false; }
	// ★★★ ГВАРД ИМЕННО `IsWindowLive`, А НЕ ПРОВЕРКА НА null. `UAsdaWindowSubsystem::CloseAll()`
	//   (вход в мир `UAsdaWorldSubsystem::RevealWorld`, разборка подсистемы)
	//   (порт `uiCloseAllWindows`) выкидывает окна из вьюпорта и чистит реестр, но наш
	//   `UPROPERTY`-указатель остаётся ненулевым: без этой проверки окно «открывалось» в никуда,
	//   а все кэши подконтролов указывали на снесённые виджеты. Этот дефект уже дважды ломал
	//   систему (сетка призов гачи и полоса прокрутки сумки).
	if (bReady && WM->IsWindowLive(Win)) { return true; }

	// Дерево пересоздаётся ⇒ ВСЕ кэши, живущие в нём, сбрасываются.
	TitleText = nullptr;
	NoteText = nullptr;
	SelFrame = nullptr;
	RowGroups.Reset();
	RowHighlights.Reset();
	RadioImages.Reset();
	IconImages.Reset();
	IconGrid = nullptr;      // сетка держит прозрачные кнопки-оверлеи внутри старого дерева
	Relays.Reset();          // GC-якоря обработчиков старых кнопок
	TipHoverCell = -1;
	Mouse.Reset();
	bReady = false;

	Win = WM->OpenWindowByPath(FName(kWinId), kWinPath, kWinZ, ESlateVisibility::SelfHitTestInvisible);
	if (!Win)
	{
		UE_LOG(LogAsdaPost, Error, TEXT("[элитный ящик] окно 69000 не загрузилось (%s)"), kWinPath);
		return false;
	}
	WM->SetWindowVisible(FName(kWinId), false);   // в `.JUI` у 69000 `Visible = "0"`
	SetupControls();
	bReady = true;
	return true;
}

void UAsdaPremiumPostSubsystem::CollectRowIconNames(int32 Row, TArray<FName>& Out) const
{
	Out.Reset();
	if (!Win) { return; }
	UPanelWidget* Grp = Cast<UPanelWidget>(Win->GetWidgetFromName(FName(*RowIconGroupName(Row))));
	if (!Grp)
	{
		UE_LOG(LogAsdaPost, Error, TEXT("[элитный ящик] группа иконок строки %d (%s) не найдена"),
			Row + 1, *RowIconGroupName(Row));
		return;
	}
	// Дети группы в порядке `.JUI`: подпись-`UTextBlock` контрола `MARK_R{n}`, хит-план `…_HIT`,
	//   затем 7 пар «план-иконка + её подпись». Берём только `UImage` и отбрасываем хит-план.
	for (int32 i = 0; i < Grp->GetChildrenCount(); ++i)
	{
		UWidget* C = Grp->GetChildAt(i);
		if (!C || !C->IsA(UImage::StaticClass())) { continue; }
		const FString N = C->GetName();
		if (N.Contains(TEXT("_HIT"))) { continue; }
		Out.Add(FName(*N));
	}
	if (Out.Num() != kCols)
	{
		UE_LOG(LogAsdaPost, Error,
			TEXT("[элитный ящик] строка %d: иконок найдено %d, а в `.JUI` их ровно %d (69%d11…69%d17) — ")
			TEXT("разметка окна разошлась с эталоном"), Row + 1, Out.Num(), kCols, Row, Row);
	}
}

void UAsdaPremiumPostSubsystem::SetupControls()
{
	if (!Win) { return; }

	// ── 69001 заголовок + 69002 примечание (эталон перетирает подписи `.JUI` строками таблицы) ──
	TitleText = Cast<UTextBlock>(Win->GetWidgetFromName(TEXT("Primium_Item_post_movetext")));
	NoteText  = Cast<UTextBlock>(Win->GetWidgetFromName(TEXT("Primium_Item_post_t2")));
	SetLocText(TEXT("Primium_Item_post_movetext"), kLocTitle);
	SetLocText(TEXT("Primium_Item_post_t2"),       kLocNote);
	// ★ `Autowrap` в Canvas ломает раскладку (закон `ui-autowrap-canvas-gotcha`) — выключаем явно
	//   у обеих подписей: примечание 290×32 длинное и импортёр вполне мог включить перенос.
	if (TitleText) { TitleText->SetAutoWrapText(false); }
	if (NoteText)  { NoteText->SetAutoWrapText(false); }

	// ── 69082 «Положить в рюкзак» / 69083 «Закрыть» / 69089 крестик ──
	SetLocText(TEXT("Primium_Item_post_OK_Text"), kLocOk);
	SetLocText(TEXT("Primium_Item_post_X_Text"),  kLocClose);
	auto BindBtn = [this](const TCHAR* Name, TFunction<void(int32)> Fn)
	{
		UButton* B = Cast<UButton>(Win->GetWidgetFromName(FName(Name)));
		if (!B)
		{
			UE_LOG(LogAsdaPost, Error, TEXT("[элитный ящик] кнопка %s не найдена в WBP"), Name);
			return;
		}
		UAsdaButtonRelay* R = NewObject<UAsdaButtonRelay>(this);
		R->Callback = MoveTemp(Fn);
		B->OnClicked.AddDynamic(R, &UAsdaButtonRelay::OnClicked);
		AsdaUi::AnchorRelay(Relays, R, B);   // ★ 31.10.2026: якорь чистит мёртвые/перепривязанные (`asda.UI.RelayAnchorPrune`)
	};
	// Эталон `uiInventoryHandleCommand`, `a3 == 66561`: 69082 → 11006, 69083 и 69089 → закрыть.
	BindBtn(TEXT("Primium_Item_post_OK"), [this](int32) { TakeSelected(); });
	BindBtn(TEXT("Primium_Item_post_X"),  [this](int32) { CloseWindow(); });
	BindBtn(TEXT("Primium_Item_post_XX"), [this](int32) { CloseWindow(); });

	// ── строки L1…L7 и их подсветка `Box_BlueOt20` ──
	RowGroups.SetNum(kRows);
	RowHighlights.SetNum(kRows);
	for (int32 R = 0; R < kRows; ++R)
	{
		UWidget* G = Win->GetWidgetFromName(FName(*RowGroupName(R)));
		RowGroups[R] = G;
		RowHighlights[R] = nullptr;
		if (UPanelWidget* P = Cast<UPanelWidget>(G))
		{
			for (int32 i = 0; i < P->GetChildrenCount(); ++i)
			{
				UImage* Img = Cast<UImage>(P->GetChildAt(i));
				if (Img && Img->GetName().StartsWith(TEXT("Box_BlueOt20"))) { RowHighlights[R] = Img; break; }
			}
		}
		if (!RowHighlights[R])
		{
			UE_LOG(LogAsdaPost, Warning, TEXT("[элитный ящик] строка %d: подсветки `Box_BlueOt20` нет"), R + 1);
		}
	}

	// ── 69081 радиогруппа: семь картинок `Smelting_V7Btn_2_…_{0..6}` в `Primium_Item_post_BOX_grp` ──
	//   ★ В `.JUI` это ОДИН контрол `Type = "RadioButton"` с семью элементами `ES 0..6`, но
	//     импортёр раскладывает элементы в отдельные планы (закон `jui-importer-widget-naming`).
	RadioImages.Reset();
	if (UPanelWidget* Box = Cast<UPanelWidget>(Win->GetWidgetFromName(TEXT("Primium_Item_post_BOX_grp"))))
	{
		for (int32 i = 0; i < Box->GetChildrenCount(); ++i)
		{
			UImage* Img = Cast<UImage>(Box->GetChildAt(i));
			if (Img && Img->GetName().StartsWith(TEXT("Smelting_V7Btn_2"))) { RadioImages.Add(Img); }
		}
	}
	if (RadioImages.Num() != kRows)
	{
		UE_LOG(LogAsdaPost, Error,
			TEXT("[элитный ящик] радиогруппа 69081: элементов %d вместо %d — выбор строки полосой слева не заработает"),
			RadioImages.Num(), kRows);
	}
	// ★★★ 2026-08-27. БЫЛО: кликабельного виджета у 69081 не было вовсе, и порт ловил выбор
	//   строки покадровым опросом живой геометрии семи планов (`TickRadioClick`).
	//   ПОЧЕМУ УБРАНО: контрол СТАТИЧЕН — семь элементов `ES 0..6` объявлены в `gamemain_jui.ini`
	//   с постоянной геометрией (полоса 18x259 в неподвижном окне), их число и класс
	//   (`RadioButton`) от рантайма не зависят. Опрос вдобавок не давал состояния наведения.
	//   ГДЕ ЖИВЁТ ТЕПЕРЬ: узлы `Primium_Item_post_BOX_SEL00_OPT{0..6}` в
	//   `Content/AsdaAssets/UI/Player_UI/Premium/WBP_Primium_Item_post_WIN` (шаг
	//   `Tools/asda_ui_missing_interactive.py`, сторожа — в `asda_ui_post_import.HANDMADE`).
	//   ★ Планы-картинки при этом ОСТАЮТСЯ видимыми: у `RadioButton` кадр покоя кнопки пуст
	//   (`UAsdaUmgBuilder::AddOptionButton`: Normal = NoDrawType), и «выбранность» по-прежнему
	//   рисует `RefreshSelectionVisual`, подменяя UV самого плана.
	int32 RadioBound = 0;
	for (int32 R = 0; R < kRows; ++R)
	{
		UButton* B = Cast<UButton>(Win->GetWidgetFromName(
			FName(*FString::Printf(TEXT("Primium_Item_post_BOX_SEL00_OPT%d"), R))));
		if (!B)
		{
			UE_LOG(LogAsdaPost, Error,
				TEXT("[элитный ящик] радиогруппа 69081: узла `Primium_Item_post_BOX_SEL00_OPT%d` нет в WBP"), R);
			continue;
		}
		AsdaUi::BindButton(this, B, R, [this, R](int32) { ToggleRow(R); }, Relays);   // ★ U20: реле клика — общая точка
		++RadioBound;
	}
	UE_LOG(LogAsdaPost, Log, TEXT("[элитный ящик] радиогруппа 69081: привязано %d/%d кнопок"),
		RadioBound, kRows);

	// ── 69079 анимированная рамка выбора (`Slot_AniSelect`, в `.JUI` `Visible = "0"`) ──
	SelFrame = Cast<UAsdaAnimatedPlane>(AsdaUi::FindNodeByPrefix(Win, TEXT("Primium_Item_post_BB")));
	if (SelFrame)
	{
		// ★ `Visible = "0"` импортёр протаскивает РЕКУРСИЕЙ на всё поддерево; сам узел — лист,
		//   поэтому достаточно снять свёртку с него, а показ/скрытие дальше ведёт код.
		SelFrame->bLoop = true;
		SetVis(SelFrame, false);
	}
	else
	{
		UE_LOG(LogAsdaPost, Warning, TEXT("[элитный ящик] рамки выбора 69079 (`Primium_Item_post_BB`) нет в WBP"));
	}

	// ── 49 иконок 69011…69077: ОБЩАЯ сетка `UAsdaIconCellGrid` ──
	//   Она даёт и прозрачные клик-кнопки поверх планов, и рендер иконки по коду предмета, и
	//   маршрутизацию клика. Ячейки 32×32 (`.JUI Size = "32 32"`, эталон
	//   `CControl_DrawTextureRect(ctrl, tex, 0, 0, 32, 32)`) — размер задаём явно.
	TArray<FName> AllCells;
	IconImages.Reset();
	for (int32 R = 0; R < kRows; ++R)
	{
		TArray<FName> RowNames;
		CollectRowIconNames(R, RowNames);
		for (int32 C = 0; C < kCols; ++C)
		{
			const FName N = RowNames.IsValidIndex(C) ? RowNames[C] : NAME_None;
			AllCells.Add(N);
			IconImages.Add(N.IsNone() ? nullptr : Cast<UImage>(Win->GetWidgetFromName(N)));
		}
	}
	IconGrid = NewObject<UAsdaIconCellGrid>(this);
	IconGrid->SetCellSizePx(32.f);
	IconGrid->SetupNamed(Win, AllCells, /*bMale*/ true,
		[this](int32 Cell) { return CellCodes.IsValidIndex(Cell) ? CellCodes[Cell] : 0; },
		// ★★★ ИСПРАВЛЕНИЕ ДЕФЕКТА A: в эталоне клик по иконке НЕ выбирал строку.
		[this](int32 Cell) { OnIconClicked(Cell); });

	UE_LOG(LogAsdaPost, Log,
		TEXT("[элитный ящик] контролы разобраны: строк %d, радиокнопок %d, иконок %d, рамка выбора %s"),
		RowGroups.Num(), RadioImages.Num(), IconImages.Num(), SelFrame ? TEXT("есть") : TEXT("НЕТ"));
}

bool UAsdaPremiumPostSubsystem::SetLocText(const TCHAR* WidgetName, int32 LocId)
{
	if (!Win || !WidgetName) { return false; }
	UTextBlock* Tb = Cast<UTextBlock>(Win->GetWidgetFromName(FName(WidgetName)));
	if (!Tb)
	{
		UE_LOG(LogAsdaPost, Warning, TEXT("[элитный ящик] loc: узла %s в WBP нет (строка %d)"), WidgetName, LocId);
		return false;
	}
	UAsdaTextSubsystem* Txt = GetGameInstance() ? GetGameInstance()->GetSubsystem<UAsdaTextSubsystem>() : nullptr;
	const FString S = Txt ? Txt->GetTextById(LocId) : FString();
	if (S.IsEmpty())
	{
		UE_LOG(LogAsdaPost, Warning, TEXT("[элитный ящик] loc: строка %d пуста — подпись %s осталась бы пустой"),
			LocId, WidgetName);
		return false;
	}
	Tb->SetText(FText::FromString(S));
	UE_LOG(LogAsdaPost, Log, TEXT("[элитный ящик] loc: %s ← %d «%s»"), WidgetName, LocId, *S);
	return true;
}

// ═══ Порт `uiBagWindow_OpenAndSync@0xEF1B30` (шаги 1…7 эталона, разбор — `elite_mailbox_re.md` §1.6) ═══
bool UAsdaPremiumPostSubsystem::OpenAndSync(bool bAlsoOpenInventory)
{
	UGameInstance* GI = GetGameInstance();
	UAsdaWindowSubsystem* WM = GI ? GI->GetSubsystem<UAsdaWindowSubsystem>() : nullptr;
	if (!WM) { return false; }

	// ★★★ НАШЕ НОВОВВЕДЕНИЕ (НЕ эталон), ТОЧКА ПРОСМОТРА №1: игрок открыл ящик — всё, что в нём
	//   сейчас лежит, он увидит. Метим ДО шага 2, потому что тот очищает модель, а метить надо
	//   ровно то содержимое, которое игроку показывали. Ответ сервера (11005) домётит остальное.
	MarkCurrentListSeen(TEXT("игрок открыл окно ящика"));

	// Шаг 1: `this[616] = 0` — снять «запрос уже в полёте».
	bRequestInFlight = false;
	// Шаг 2: очистить список позиций и 7 записей строк (`= -1`).
	for (FAsdaPremiumPostRow& R : Rows) { R = FAsdaPremiumPostRow(); }
	CellCodes.Init(-1, kRows * kCols);
	SelectedRow = 0;
	ResetTake(); CheckedOrders.Reset();   // [ИСПРАВЛЕНИЕ] 25.10.2026: незаконченный забор строки забыт

	if (!EnsureWindows()) { return false; }

	// Шаг 4 ЭТАЛОНА: `sowelWindowClose(g_pInventoryWnd, 1)` — закрыть инвентарь.
	//   ★★★ ОСОЗНАННОЕ ОТСТУПЛЕНИЕ ПО ТРЕБОВАНИЮ ВЛАДЕЛЬЦА: открываем ОБА окна.
	//   Эталонная ветка не потеряна — она под `bAlsoOpenInventory == false`.
	// ★★★ ПОРЯДОК ВАЖЕН, И ЭТО ЖИВОЙ ДЕФЕКТ 2026-08-19. Раньше ящик показывался и поднимался
	//   ПЕРВЫМ, а инвентарь открывался ПОСЛЕ — и `OpenWindow` инвентаря выводил его наверх
	//   (лог живого прогона: `[z-порядок] «INVEN_WIN»: z 720 → 731` при z ящика 730). Ящик
	//   оставался ПОД инвентарём, и первый же щелчок по ящику вызывал его подъём, а подъём
	//   перевешивает окно во вьюпорте и СТИРАЕТ кэш геометрии всех его виджетов
	//   (замер `asda.Post.ProbeRaise`: «радио 0 (1102,397)-(1120,416)» → «ПРЯМОУГОЛЬНИКА НЕТ»).
	//   Ровно отсюда «приходится нажимать по два раза». Инвентарь поднимаем ПЕРВЫМ, ящик — вторым.
	if (UAsdaInventorySubsystem* Inv = GI ? GI->GetSubsystem<UAsdaInventorySubsystem>() : nullptr)
	{
		if (bAlsoOpenInventory)
		{
			if (!Inv->IsWindowOpen()) { Inv->OpenWindow(); }
		}
		else if (Inv->IsWindowOpen())
		{
			Inv->CloseWindow();
		}
	}

	// Шаг 3: показать окно 69000 и вывести его наверх (`CClass_MapInsert(g_pWindowMgr, wnd)`).
	WM->SetWindowVisible(FName(kWinId), true);
	WM->BringWindowToFront(FName(kWinId));

	// ★ 08.11.2026 [ОРИГИНАЛ] (пачка 19): шаг 5 эталона (ui_core.c:92008-92019) — `CharFx_AutoSabzil_State(свой, …, 0)` + `Net_SendActionPacket`
	//   (4008): персонаж встаёт. В порту шага не было. Ключ `asda.Move.UiActionStop` (0 — прежнее: без остановки).
	AsdaFullStopSelfForUiAction(GI, TEXT("окно элитного ящика 69000"));

	// Шаг 6: перерисовать сетку (пока пусто).
	RefreshGrid();

	// Шаг 7: `netSecure_sendGuardedRequest(this)` → 11004. Гвард `this[616] != 1` уже проверен выше.
	if (UAsdaNetSubsystem* Net = GI ? GI->GetSubsystem<UAsdaNetSubsystem>() : nullptr)
	{
		if (Net->SendPremiumPostListReq11004()) { bRequestInFlight = true; }
	}
	UE_LOG(LogAsdaPost, Log, TEXT("[элитный ящик] окно открыто (инвентарь: %s), запрошен 11004"),
		bAlsoOpenInventory ? TEXT("оставляем открытым — отступление по требованию владельца")
		                   : TEXT("закрыт, как в эталоне"));
	return true;
}

// Порт `sub_92548B`: команды 69083/69089 и `a3 == 0x15` (Esc) при `this[616] != 1`.
void UAsdaPremiumPostSubsystem::CloseWindow()
{
	UGameInstance* GI = GetGameInstance();
	if (UAsdaWindowSubsystem* WM = GI ? GI->GetSubsystem<UAsdaWindowSubsystem>() : nullptr)
	{
		WM->SetWindowVisible(FName(kWinId), false);
	}
	// ★★★ НАШЕ НОВОВВЕДЕНИЕ (НЕ эталон), ТОЧКА ПРОСМОТРА №3: игрок закрыл окно — значит, всё, что
	//   в нём было нарисовано, он видел. Нужна отдельно от точки №1 для случая «окно уже было
	//   открыто, список приехал позже, а потом ещё раз обновился» — метим фактическое содержимое
	//   на момент закрытия. Вызов идемпотентен.
	MarkCurrentListSeen(TEXT("игрок закрыл окно ящика"));
	// [ИСПРАВЛЕНИЕ] 25.10.2026: окно закрыто посреди забора строки — неотправленные записи остаются в ящике (ответ на уже
	//   отправленную запись придёт и будет применён как обычно).
	if (bTakeRun || bPreChoiceWait || bAwaitSwap)   // ★ 10.11.2026: и выбор до получения (одиночный режим тоже)
	{
		UE_LOG(LogAsdaPost, Log, TEXT("[элитный ящик] забор строки прерван закрытием окна: взято %d из %d, не отправлено %d"),
			TakeOk, TakeTotal, TakeLeft.Num());
		ResetTake();
	}
	// Гасим ТОЛЬКО свой показ подсказки (закон `ui-shared-tip-ownership`).
	if (UAsdaItemTooltipSubsystem* Tip = GI ? GI->GetSubsystem<UAsdaItemTooltipSubsystem>() : nullptr)
	{
		Tip->HideTooltipFor(EAsdaTipOwner::PremiumPost);
	}
	TipHoverCell = -1;
	Mouse.Reset();
	UE_LOG(LogAsdaPost, Log, TEXT("[элитный ящик] окно закрыто"));
}

void UAsdaPremiumPostSubsystem::ToggleWindow()
{
	// Порт `case 0xAu` диспетчера действий (ui_core.c:87186): видно → закрыть, иначе → открыть.
	if (IsWindowVisible()) { CloseWindow(); }
	else                   { OpenAndSync(!bLegacyCloseInventory); }
}

bool UAsdaPremiumPostSubsystem::IsWindowVisible() const
{
	UAsdaWindowSubsystem* WM = GetGameInstance() ? GetGameInstance()->GetSubsystem<UAsdaWindowSubsystem>() : nullptr;
	return WM && WM->IsWindowVisible(FName(kWinId));
}

// ═══════════════════════════════ МОДЕЛЬ И ОТРИСОВКА ════════════════════════════════════════════
const FAsdaPremiumPostRow& UAsdaPremiumPostSubsystem::GetRow(int32 Row) const
{
	static const FAsdaPremiumPostRow Empty;
	return Rows.IsValidIndex(Row) ? Rows[Row] : Empty;
}

int32 UAsdaPremiumPostSubsystem::GetFilledRowCount() const
{
	int32 N = 0;
	for (const FAsdaPremiumPostRow& R : Rows) { if (!R.IsEmpty()) { ++N; } }
	return N;
}

void UAsdaPremiumPostSubsystem::SelectRow(int32 Row)
{
	// Порт `uiInventoryHandleCommand`, `a3 == 66817 && a2 == 69081`: `this[155] = a5`.
	if (Row < 0 || Row >= kRows) { return; }
	// Прямой выбор оставляет отмеченной одну эту строку: так работают консоль и самотесты.
	if (PostMultiSelectOn())
	{
		CheckedOrders.Reset();
		if (Rows.IsValidIndex(Row) && !Rows[Row].IsEmpty()) { CheckedOrders.Add(Rows[Row].Order); }
		SelectedRow = Row;
		RefreshSelectionVisual();
		return;
	}
	if (SelectedRow == Row) { return; }
	SelectedRow = Row;
	RefreshSelectionVisual();
	UE_LOG(LogAsdaPost, Log, TEXT("[элитный ящик] выбрана строка %d (заказ %d, предмет %d, count %d, BuyType %d)"),
		Row, Rows[Row].Order, Rows[Row].ItemIndex, Rows[Row].Count, Rows[Row].BuyType);
}

void UAsdaPremiumPostSubsystem::ToggleRow(int32 Row)
{
	if (!PostMultiSelectOn()) { SelectRow(Row); return; }
	if (!Rows.IsValidIndex(Row) || Rows[Row].IsEmpty()) { return; }
	const int32 Order = Rows[Row].Order;
	if (CheckedOrders.Remove(Order) == 0)
	{
		CheckedOrders.Add(Order);
		SelectedRow = Row;
	}
	else if (SelectedRow == Row)
	{
		// Рамка переходит на нижнюю отмеченную строку. Если отметок нет, рамку скроет `RefreshSelectionVisual`.
		for (int32 k = kRows - 1; k >= 0; --k)
		{
			if (IsRowChecked(k)) { SelectedRow = k; break; }
		}
	}
	RefreshSelectionVisual();
	UE_LOG(LogAsdaPost, Log, TEXT("[элитный ящик] строка %d (заказ %d): отметка %s, отмечено заказов %d"),
		Row, Order, IsRowChecked(Row) ? TEXT("поставлена") : TEXT("снята"), CheckedOrders.Num());
}

bool UAsdaPremiumPostSubsystem::IsRowChecked(int32 Row) const
{
	if (!Rows.IsValidIndex(Row) || Rows[Row].IsEmpty()) { return false; }
	return PostMultiSelectOn() ? CheckedOrders.Contains(Rows[Row].Order) : Row == SelectedRow;
}

// ★★★ ИСПРАВЛЕНИЕ ДЕФЕКТА A (закон `fix-original-defects`).
//   ЭТАЛОН: `uiInventoryHandleCommand@0xEF1CC0` на клик (`a3 == 66561`) реагирует ТОЛЬКО для
//   69089/69083/69082. Иконки 69011…69077 шлют лишь события 8/9/0xA (подсказка), а выбор строки
//   меняет одна команда 66817 от радиогруппы 69081 — вертикальной полосы 18×19 px слева.
//   ⇒ игрок кликал по нужной иконке, выбор не менялся, и «Положить в рюкзак» забирало строку,
//   которую поставил `gridWindowPopulate7x7` (верхнюю непустую). ЧИНИМ: клик по любой иконке
//   строки выбирает ЭТУ строку. Радиокнопка продолжает работать как в оригинале.
void UAsdaPremiumPostSubsystem::OnIconClicked(int32 Cell)
{
	if (Cell < 0 || Cell >= kRows * kCols) { return; }
	const int32 Row = Cell / kCols;
	if (Rows.IsValidIndex(Row) && Rows[Row].IsEmpty())
	{
		// Пустая строка выбора не получает — так же, как её не получает пустая радиокнопка
		//   в `gridWindowPopulate7x7` (он выбирает только строку с `ItemIndex != -1`).
		return;
	}
	UE_LOG(LogAsdaPost, Log,
		TEXT("[элитный ящик] ★ клик по иконке ячейки %d (строка %d, колонка %d) → выбор строки ")
		TEXT("(исправление дефекта A: в оригинале клик по иконке выбор НЕ менял)"),
		Cell, Row, Cell % kCols);
	ToggleRow(Row);   // при `asda.EliteMail.MultiSelect 0` это прежний SelectRow
}

// Порт `gridWindowPopulate7x7@0xEF1DE0`.
void UAsdaPremiumPostSubsystem::RefreshGrid()
{
	// 1) Очистка всех 49 ячеек (эталон затирает их пустой текстурой 32×32 и ставит код −1).
	//    Модель уже подготовлена вызывающим; здесь только перерисовка.
	if (IconGrid) { IconGrid->Refresh(); }

	// 2) ★★★ ЭТАЛОННЫЙ АВТО-ВЫБОР — ПРОЧИТАН ПО КОДУ, А НЕ ПО ОПИСАНИЮ.
	//    `gridWindowPopulate7x7@0xEF1DE0` (world_map.c:8296-8306):
	//        for ( k = 6; k >= 0; --k )
	//            if ( *(int *)((char *)v13 + 10 * k + 640) != -1 )
	//            { CControl_SetSelectedIndex((CControl *)v13[4], k); v13[155] = k; return result; }
	//    Обход идёт СВЕРХУ ПО ИНДЕКСУ ВНИЗ (k = 6 → 0) и ВЫХОДИТ на первой найденной ⇒ выбирается
	//    строка с НАИБОЛЬШИМ индексом, то есть **НИЖНЯЯ (последняя) непустая**, а не верхняя.
	//    Строки заполняются подряд от 0, значит это последний пришедший заказ.
	//    ⚠ Именно поэтому дефект A так больно бил: игрок кликал по иконке НУЖНОГО заказа, выбор
	//      оставался на ПОСЛЕДНЕЙ строке, и забирался совсем другой заказ целиком.
	//    ★ При включённом расширении `asda.EliteMail.SortNewestFirst` правило остаётся ТЕМ ЖЕ
	//      (нижняя непустая строка), но означает уже не «последний пришедший», а «последний в
	//      выбранном порядке» — номера строк и радиовыбор от перестановки не разъезжаются.
	// ★ 07.11.2026 `asda.EliteMail.MultiSelect`: отметки взятых заказов уходят, отметки оставшихся живут. Если отметок нет,
	//   действует то же эталонное правило: отмечается нижняя непустая строка.
	const bool bMultiSel = PostMultiSelectOn();
	if (bMultiSel)
	{
		TSet<int32> Alive;
		for (const FAsdaPremiumPostRow& R : Rows)
		{
			if (!R.IsEmpty() && CheckedOrders.Contains(R.Order)) { Alive.Add(R.Order); }
		}
		CheckedOrders = MoveTemp(Alive);
	}
	for (int32 k = kRows - 1; k >= 0; --k)
	{
		if (Rows[k].IsEmpty()) { continue; }
		if (bMultiSel && CheckedOrders.Num() > 0 && !CheckedOrders.Contains(Rows[k].Order)) { continue; }
		SelectedRow = k;
		if (bMultiSel) { CheckedOrders.Add(Rows[k].Order); }
		break;
	}
	RefreshSelectionVisual();
}

void UAsdaPremiumPostSubsystem::RefreshSelectionVisual()
{
	const bool bAnyFilled = GetFilledRowCount() > 0;
	for (int32 R = 0; R < kRows; ++R)
	{
		const bool bSel = bAnyFilled && IsRowChecked(R);
		// Подложка выбора строки — скрытый в `.JUI` план `Box_BlueOt20` (`Visible = "0"`).
		SetVis(RowHighlights.IsValidIndex(R) ? RowHighlights[R].Get() : nullptr, bSel);
		// Радиокнопка 69081: состояние `SS 2` (выбранная) против `SS 0` — UV из `JuiSkin.ini`.
		if (RadioImages.IsValidIndex(R))
		{
			if (UImage* Img = RadioImages[R].Get())
			{
				FSlateBrush B = Img->GetBrush();
				B.SetUVRegion(bSel ? kRadioUvChosen : kRadioUvNormal);
				Img->SetBrush(B);
			}
		}
	}
	// 69079 — анимированная рамка `Slot_AniSelect` над ПЕРВОЙ иконкой выбранной строки.
	//   В `.JUI` она лежит в `NoImage_grp` на (34, 2) — ровно над иконкой строки 1. Строки лежат в
	//   ТОЙ ЖЕ канве `NoImage_grp`, поэтому Y рамки = Y группы строки + её собственные 2 px.
	//   ★ Шаг берём ИЗ ЖИВОЙ РАЗМЕТКИ (позиция группы строки), а не из формулы «40·i»: в `.JUI`
	//   строки стоят на 0/41/82/122/162/202/242, то есть шаг НЕ ровно 40, и формула промахнулась бы
	//   на 1…2 px у нижних строк.
	if (SelFrame)
	{
		const bool bShow = bAnyFilled && IsRowChecked(SelectedRow);
		if (bShow)
		{
			UCanvasPanelSlot* CS = AsdaUi::SlotOf(SelFrame);
			UCanvasPanelSlot* RS = RowGroups.IsValidIndex(SelectedRow) ? AsdaUi::SlotOf(RowGroups[SelectedRow].Get()) : nullptr;
			if (CS && RS)
			{
				FVector2D P = CS->GetPosition();
				P.Y = RS->GetPosition().Y + 2.f;
				CS->SetPosition(P);
			}
			SetVis(SelFrame, true);
			SelFrame->RestartAnim();
		}
		else
		{
			SetVis(SelFrame, false);
			SelFrame->StopAnim();
		}
	}
}
