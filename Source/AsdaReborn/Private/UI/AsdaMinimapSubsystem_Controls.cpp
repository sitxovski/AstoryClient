// Copyright (c) 2026 @sitxovski (Discord)
// AsdaMinimapSubsystem_Controls.cpp — ★ R3-c 12.11.2026 (разрез AsdaMinimapSubsystem.cpp, дословный перенос): виджеты, кнопки, клики по карте, раскладка, битва армий, видимость/зум, большая карта.
//   Общее для частей — в AsdaMinimapSubsystem_Private.h; шапка и реверс — в AsdaMinimapSubsystem.cpp.
#include "UI/AsdaMinimapSubsystem.h"



#include "Components/CheckBox.h"
#include "AsdaMinimapSubsystem_Private.h"   // ★ R3-c 12.11.2026: общее для частей AsdaMinimapSubsystem*.cpp
#include "AsdaPerf.h"                         // ★ R4-b 14.11.2026: области замера времени игрового потока

// ★ R4-b 14.11.2026 [ПОРТ] (R4, доля b, B2): справка кнопок мини-карты каждый кадр искала до пяти узлов обходом дерева окна. Теперь узлы — кэш по
//   виджету (окно пересоздано — найти заново). Решение о наведении то же.
// ★ R4-b закрытие (15.11.2026): ключ `asda.Perf.MinimapTips` — старый путь и ключ удалены — только новое (решение владельца).

// ★ [ОРИГИНАЛ] 06.11.2026: клик по полю принимается, только если верхнее окно под мышью — сама карта (событие получает верхний
//   контрол, `MiniMap_BigMapWindowProc`, world_map.c:7456). Тело настоящего окна для Slate прозрачно, и клик по окну поверх карты
//   доходил до кнопки поля. 0 — прежнее (клик проходит сквозь окно).
static ASDA_DEV_CVAR(int32, CVarAsdaMinimapClickTopWindowOnly,
	TEXT("asda.Minimap.ClickTopWindowOnly"), 1,
	TEXT("1 — поле мини-карты принимает клик только как верхнее окно (как оригинал); 0 — прежнее (клик сквозь окно поверх карты)."),
	ECVF_Default);

static ASDA_DEV_CVAR(int32, CVarMinimapHelpTips,
	TEXT("asda.Minimap.HelpTips"),
	1,
	TEXT("1 = как в оригинале: при наведении на кнопки мини-карты (закрыть, приближение, вся карта, увеличение х2, индикатор) — справка; "
	     "0 = прежний порт (справки нет)."),
	ECVF_Default);

static const TCHAR* kMinimapWbp = TEXT("/Game/AsdaAssets/UI/Player_UI/Map/WBP_MINIMAP_WIN.WBP_MINIMAP_WIN_C");
static const TCHAR* kMinIconWbp = TEXT("/Game/AsdaAssets/UI/Player_UI/Map/WBP_MINIMAPMIN_WIN.WBP_MINIMAPMIN_WIN_C");
static const TCHAR* kBigMapWbp  = TEXT("/Game/AsdaAssets/UI/Player_UI/Map/WBP_MAXMAP_WIN.WBP_MAXMAP_WIN_C");
// Виджет занятости городов `Occupation_box` (79900) — рисуется под мини-картой, см. .h.
static const TCHAR* kOccupationWbp = TEXT("/Game/AsdaAssets/UI/Player_UI/CampWar/WBP_Occupation_box.WBP_Occupation_box_C");

static void SetRect(UWidget* W, float L, float T, float SizeX, float SizeY)
{
	if (UCanvasPanelSlot* S = SlotOf(W))
	{
		S->SetOffsets(FMargin(L, T, SizeX, SizeY));
	}
}

bool UAsdaMinimapSubsystem::EnsureWidgets()
{
	UGameInstance* GI = GetGameInstance();
	UAsdaWindowSubsystem* WM = GI ? GI->GetSubsystem<UAsdaWindowSubsystem>() : nullptr;
	if (!WM) { return false; }
	// ★★★ ИСПРАВЛЕН ДЕФЕКТ (аудит №4, B4). Здесь стояла УСЕЧЁННАЯ версия гварда травела: только
	//   «указатель жив» и «мир тот же», БЕЗ `IsInViewport()`. Виджет, вылетевший из вьюпорта в том
	//   же мире, проходил проверку как годный кэш — и миникарта не переоткрывалась НИКОГДА, до
	//   перезахода в игру. Остальные 15 подсистем спрашивали все три условия; теперь спрашивают
	//   его все шестнадцать, и ровно одним способом — `UAsdaWindowSubsystem::IsWindowLive`.
	//   ⚠ Порядок тоже изменён: `WM` берётся ДО гварда (раньше `GetGameInstance()` разыменовывался
	//   в самом гварде без проверки на null).
	if (WM->IsWindowLive(MinimapWidget)) { return true; }

	// ★★★ SelfHitTestInvisible, НЕ Visible. Корень UUserWidget растянут на ВЕСЬ вьюпорт: при Visible он
	//   хит-тестится целиком и съедает КАЖДЫЙ клик по миру (регрессия 2026-08-01 — «не работает ни WASD,
	//   ни клик по земле»). Мышь должны получать только сами контролы (у них своя Visible).
	MinimapWidget = WM->OpenWindowByPath(FName(TEXT("MINIMAP_WIN")), kMinimapWbp, kMinimapZ, ESlateVisibility::SelfHitTestInvisible);
	MinIconWidget = WM->OpenWindowByPath(FName(TEXT("MINIMAPMIN_WIN")), kMinIconWbp, kMinimapZ, ESlateVisibility::SelfHitTestInvisible);
	if (!MinimapWidget) { return false; }
	if (MinIconWidget) { MinIconWidget->SetVisibility(ESlateVisibility::Collapsed); }   // JUI Visible=0

	// ── Панель занятости городов (79900). Своё окно: у него собственные анкоры (правый верх,
	//    смещение −84/300 в базе 1024×768) — двигать его вторично НЕЛЬЗЯ (закон о двух системах
	//    координат). Эталон показывает контейнер `CControl_SetVisible(79900, 1)`, а три иконки
	//    внутри оживляем по решению владельца (обоснование и ключ отката — в .h).
	if (CVarOccupationResetNodes.GetValueOnGameThread() != 0)   // ★ А2: окно пересоздаётся — прежние узлы мертвы
	{
		OccupationNodes.Reset();
		OccupationBasePos.Reset();
		bOccupationBaseCached = false;
	}
	OccupationWidget = WM->OpenWindowByPath(FName(TEXT("OCCUPATION_BOX")), kOccupationWbp,
		kMinimapZ, ESlateVisibility::SelfHitTestInvisible);
	OccupationIcons.Reset();
	if (OccupationWidget)
	{
		// ★ Порядок = ПОРЯДОК СЛОТОВ ПАКЕТА 6743, а не суффиксы имён (они у двух контролов врут):
		//   79901 → слот 0 Альпия, 79902 → слот 1 Силарис, 79903 → слот 2 Пламения.
		static const TCHAR* kIconIdc[3] = { TEXT("Occupation_apen"), TEXT("Occupation_flammio"), TEXT("Occupation_Silalis") };
		for (int32 i = 0; i < 3; ++i)
		{
			OccupationIcons.Add(Cast<UImage>(UAsdaIconCellGrid::FindCell(OccupationWidget, kIconIdc[i])));
		}
		LayoutOccupationBox();
		RefreshOccupationBox();
		// Пересчитываем видимость по каждому приходу 6743.
		if (UAsdaNetSubsystem* Net = GI->GetSubsystem<UAsdaNetSubsystem>())
		{
			// ★ ИСПРАВЛЕНИЕ (дедуп №2, В2-B): каждая пересборка окон (травел, сброс сессии) добавляла ЕЩЁ одну подписку —
			//   перерисовка по 6743 шла N раз. Прежняя подписка этого объекта снимается перед новой.
			Net->OnCityOwnerInfo.RemoveAll(this);
			Net->OnCityOwnerInfo.AddWeakLambda(this, [this]() { RefreshOccupationBox(); });
		}
	}

	MapView   = Cast<UCanvasPanel>(MinimapWidget->GetWidgetFromName(TEXT("MMap_View")));
	MapImage  = Cast<UImage>(MinimapWidget->GetWidgetFromName(TEXT("MMap_MapImage")));
	MarkLayer = Cast<UCanvasPanel>(MinimapWidget->GetWidgetFromName(TEXT("MMap_Markers")));
	if (!MapView || !MapImage || !MarkLayer)
	{
		UE_LOG(LogAsdaMinimap, Error,
			TEXT("[миникарта] в WBP_MINIMAP_WIN нет MMap_View/MMap_MapImage/MMap_Markers — прогони Tools/asda_setup_minimap_widget.py"));
		return false;
	}
	// ★★★ НОВОЕ ДЕРЕВО ВИДЖЕТОВ ⇒ ВСЕ КЭШИ «уже отрисовано» НЕДЕЙСТВИТЕЛЬНЫ.
	//   Порядок важен: сброс идёт ДО `BindChannelUI`/`ApplyLayout`, иначе они отработают на
	//   старых значениях и снова ничего не нарисуют. Разбор — в `ResetMinimapWidgetCaches`.
	ResetMinimapWidgetCaches();
	BindButtons();
	BindChannelUI();
	ApplyLayout();
	// ★ НОВОЕ ДЕРЕВО ПРИХОДИТ ИЗ .JUI: `MINIMAP_WIN` (1702) Visible="1", `MINIMAPMIN_WIN` (1700)
	//   Visible="0" — то есть развёрнутым. Это верное поведение ПРИ ПЕРВОМ создании (эталон при
	//   входе в мир мини-карту не сворачивает и между сессиями свёрнутость не сохраняет).
	//   Но окно 1702 у нас пересоздаётся ещё и на каждой смене карты (сносящий `CloseAll()` в
	//   `RevealWorld`), а в оригинале оно живёт всё время сессии. Поэтому выбор игрока —
	//   поле подсистемы `bVisible`, которое травел переживает, — накатываем на новое дерево:
	//   иначе свёрнутая мини-карта сама «разворачивалась» бы после каждого перехода.
	SetMinimapVisibleFrom(bVisible, TEXT("пересоздание окна 1702 (восстановление состояния)"));
	// ★★★ ШАГ 15 ЭТАЛОННОГО `uiCloseAllWindows@0xD61960` (ui_core.c:54375) — ЗАКРЫТЬ КАРТУ МИРА 1710,
	//     А НЕ СВОРАЧИВАТЬ МИНИ-КАРТУ. Разбор целиком — `Reports/minimap_collapse_fix.md`.
	//
	//   Эталон: `miniMapWndClose(g_pMiniMapWnd)` @0xD63FF0 (world_map.c:7189):
	//       if (CControl_IsVisible(this[10])) { CControl_SetVisible(this[10], 0);
	//                                           timerSetModeAndStart(g_pMiniMap, 0); звук 6; }
	//   ⚠ ИМЯ ФУНКЦИИ — МИСНОМЕР. `this[10]` — это `pBigMapWin` (окно карты мира 1710 MAXMAP_WIN),
	//     а вовсе не окно мини-карты 1702. Доказано двумя независимыми методами:
	//       1) `inventory_ToggleControlBarWindow@0xD64A30` (item_inventory.c:50238/50264/50274) —
	//          тумблер карты мира — содержит ДОСЛОВНО ту же пару над ИМЕНОВАННЫМ полем:
	//          `CControl_SetVisible(this->pBigMapWin, 0); timerSetModeAndStart(g_pMiniMap, 0); звук 6`
	//          (и зеркальную `SetVisible(...,1) + mode 1 + звук 5` на открытии);
	//       2) `MiniMap_Window_OnCommand@0xD64050` вешает `miniMapWndClose` на команду 0x6AF = 1711,
	//          а 1711 в `gamemain_jui.ini` — это `MAXMAP_CLOSE`, крестик окна 1710 `MAXMAP_WIN`
	//          (кнопка сворачивания мини-карты — другая: 1704 `MINIMAP_CLOSE`, case 0x6A8 →
	//          `uiDlgCollapseToCompact@0xD64280`, и её в списке `uiCloseAllWindows` НЕТ).
	//   Окно 1702 в эталонных списках закрытия не участвует вообще — ни в `uiCloseAllWindows`,
	//   ни в `uiCloseAllWindowsOnLeave@0xD62790`. Поэтому здесь расписываем ИМЕННО карту мира.
	//
	//   БЫЛО (баг): `SetMinimapVisible(false)` — мини-карта сама уходила в иконку 1700 при каждом
	//   разговоре с NPC, открытии сцены гачи и окна апгрейда, т.е. при любом `CloseAllPlayerWindows`.
	//
	//   Гвард `if (bBigMapVisible)` повторяет эталонное `if (CControl_IsVisible(...))`: без него
	//   закрыватель гонял бы звук/лог и сброс комбо-списков у и без того закрытой карты.
	WM->RegisterWindowCloser(FName(TEXT("MAXMAP_WIN")),
		FSimpleDelegate::CreateWeakLambda(this, [this]()
		{
			if (bBigMapVisible) { SetBigMapVisible(false); }
		}));
	UE_LOG(LogAsdaMinimap, Log, TEXT("[миникарта] окно создано (1702 + 1700), z=%d"), kMinimapZ);
	return true;
}

void UAsdaMinimapSubsystem::BindButtons()
{
	if (!MinimapWidget) { return; }
	// 1708 MINIMAP_UPDOWN — импортёр сделал CheckButton → UCheckBox (toggle «развернуть/свернуть»).
	if (UCheckBox* Up = Cast<UCheckBox>(MinimapWidget->GetWidgetFromName(TEXT("MINIMAP_UPDOWN"))))
	{
		Up->OnCheckStateChanged.Clear();
		Up->OnCheckStateChanged.AddDynamic(this, &UAsdaMinimapSubsystem::SetExpanded);
	}
	// 1706 MINIMAP_ZOOM — тоже toggle.
	if (UCheckBox* Zm = Cast<UCheckBox>(MinimapWidget->GetWidgetFromName(TEXT("MINIMAP_ZOOM"))))
	{
		Zm->OnCheckStateChanged.Clear();
		Zm->OnCheckStateChanged.AddDynamic(this, &UAsdaMinimapSubsystem::SetZoom);
	}
	// 1704 MINIMAP_CLOSE — свернуть в иконку (uiDlgCollapseToCompact).
	if (UButton* Cl = Cast<UButton>(MinimapWidget->GetWidgetFromName(TEXT("MINIMAP_CLOSE"))))
	{
		Cl->OnClicked.Clear();
		Cl->OnClicked.AddDynamic(this, &UAsdaMinimapSubsystem::OnCloseClicked);
	}
	// 1707 MINIMAP_MAXBUT — большая карта (inventory_ToggleControlBarWindow).
	if (UButton* Mx = Cast<UButton>(MinimapWidget->GetWidgetFromName(TEXT("MINIMAP_MAXBUT"))))
	{
		Mx->OnClicked.Clear();
		Mx->OnClicked.AddDynamic(this, &UAsdaMinimapSubsystem::ToggleBigMap);
	}
	// Клик по области карты = «идти сюда» (эталон: hover/push по MMap_BkBoder 1714 → g_bBigMapToggle).
	if (UButton* Ck = Cast<UButton>(MinimapWidget->GetWidgetFromName(TEXT("MMap_ClickArea"))))
	{
		Ck->OnClicked.Clear();
		Ck->OnClicked.AddDynamic(this, &UAsdaMinimapSubsystem::OnMapClicked);
	}
	// 17520 CAMPWAR_Out_Button — «выйти из боя» (эталон MiniMap_Window_OnCommand case 0x4470).
	if (UButton* Cw = Cast<UButton>(MinimapWidget->GetWidgetFromName(TEXT("CAMPWAR_Out_Button"))))
	{
		Cw->OnClicked.Clear();
		Cw->OnClicked.AddDynamic(this, &UAsdaMinimapSubsystem::OnCampWarOutClicked);
	}
	// 1701 MINIMAPMIN_ON — вернуть мини-карту (uiMinimapWindow_Open).
	if (MinIconWidget)
	{
		if (UButton* On = Cast<UButton>(MinIconWidget->GetWidgetFromName(TEXT("MINIMAPMIN_ON"))))
		{
			On->OnClicked.Clear();
			On->OnClicked.AddDynamic(this, &UAsdaMinimapSubsystem::OnRestoreClicked);
		}
	}
}

// ★ 30.10.2026 [ОРИГИНАЛ] Справка кнопок мини-карты (`asda.Minimap.HelpTips`). Эталон `MiniMap_BigMapWindowProc@0xD65210`
//   msg 8 → `uiBuildHelpTooltipByMsgId@0xD65ED0` (ui_core.c:54944): 1704 → тема 14, 1706 → 25 при включённом зуме / 13,
//   1707 → 15, 1708 → 26 при развёрнутом окне / 12, 1719 → 27; msg 9 → плашка за курсором, msg 0xA → гашение. Флаги эталона —
//   «отмечена ли кнопка-флажок» (`getFlagByte_p608` контролов +6/+7, world_map.c:7020/7026), у порта это `bZoom`/`bExpanded`.
//   Наведение у кнопок-флажков (`UCheckBox`) и кнопок (`UButton`) — по `IsHovered()` Slate: он отдаёт мышь только верхнему
//   виджету, как диспетчер эталона; индикатор 1719 — картинка, его проверяем геометрией.
void UAsdaMinimapSubsystem::TickHelpTips()
{
	ASDA_PERF_SCOPE("Minimap.HelpTips");   // ★ R4-b 14.11.2026: область замера (AsdaPerf.h; в Shipping пусто)
	UGameInstance* GI = GetGameInstance();
	if (CVarMinimapHelpTips.GetValueOnGameThread() == 0 || !MinimapWidget || !bVisible)
	{
		HelpTip.Hide(GI, EAsdaTipOwner::Minimap);
		return;
	}
	// ★ R4-b 14.11.2026 (B2): узлы справки — кэш по виджету (`TipNodes`, индексы как в списке ниже); ★ R4-b закрытие (15.11.2026): старый путь и ключ удалены — только новое (решение владельца).
	if (TipOwner.Get() != MinimapWidget)
	{
		static const TCHAR* const kTip[5] = { TEXT("MINIMAP_CLOSE"), TEXT("MINIMAP_ZOOM"), TEXT("MINIMAP_MAXBUT"), TEXT("MINIMAP_UPDOWN"), TEXT("Indicator_MiniMap") };
		TipOwner = MinimapWidget;
		for (int32 I = 0; I < 5; ++I) { TipNodes[I] = MinimapWidget->GetWidgetFromName(FName(kTip[I])); }
	}
	auto Node = [this](int32 I, const TCHAR* /*Name — для чтения списка ниже*/) -> UWidget* { return TipNodes[I].Get(); };
	// Контрол под курсором (живой, не свёрнутый).
	auto Hovered = [&Node](int32 I, const TCHAR* Name) -> bool
	{
		const UWidget* W = Node(I, Name);
		return W && W->IsHovered() && W->GetVisibility() != ESlateVisibility::Collapsed && W->GetVisibility() != ESlateVisibility::Hidden;
	};
	int32 Key = -1, Topic = -1;
	if (Hovered(0, TEXT("MINIMAP_CLOSE")))       { Key = 1704; Topic = 14; }
	else if (Hovered(1, TEXT("MINIMAP_ZOOM")))   { Key = 1706; Topic = bZoom ? 25 : 13; }
	else if (Hovered(2, TEXT("MINIMAP_MAXBUT"))) { Key = 1707; Topic = 15; }
	else if (Hovered(3, TEXT("MINIMAP_UPDOWN"))) { Key = 1708; Topic = bExpanded ? 26 : 12; }
	else if (AsdaUi::IsCursorOverWidget(Node(4, TEXT("Indicator_MiniMap")))) { Key = 1719; Topic = 27; }
	// Смена состояния флажка под курсором (щелчок) меняет тему — у эталона ту же: плашка перестраивается на следующем
	//   событии 8. Ключ включает тему, чтобы новый текст появился сразу после щелчка, а не только после ухода и возврата.
	HelpTip.UpdateTopic(GI, EAsdaTipOwner::Minimap, Key < 0 ? -1 : Key * 100 + Topic, Topic);
}

// 1704 MINIMAP_CLOSE — крестик самой мини-карты, эталонный `uiDlgCollapseToCompact@0xD64280`.
void UAsdaMinimapSubsystem::OnCloseClicked()   { SetMinimapVisibleFrom(false, TEXT("кнопка 1704 MINIMAP_CLOSE")); }
// 1701 — клик по иконке 1700, эталонный `miniIcon_OnOpenMinimap@0xD646F0` → `uiMinimapWindow_Open`.
void UAsdaMinimapSubsystem::OnRestoreClicked() { SetMinimapVisibleFrom(true,  TEXT("иконка 1700 MINIMAPMIN_WIN")); }

void UAsdaMinimapSubsystem::OnMapClicked()
{
	const UGameInstance* GI = GetGameInstance();
	const UAsdaWindowSubsystem* WM = GI ? GI->GetSubsystem<UAsdaWindowSubsystem>() : nullptr;
	if (CVarAsdaMinimapClickTopWindowOnly.GetValueOnGameThread() != 0 && WM && FSlateApplication::IsInitialized()
		&& WM->FindWindowCatchingAt(FSlateApplication::Get().GetCursorPos()) != FName(TEXT("MINIMAP_WIN")))
	{
		return;   // клик предназначен окну поверх карты
	}
	if (AsdaMinimapFishingGate(GetGameInstance(), TEXT("1714 клик по полю"))) { return; }   // ★ В4 рыбалки
	++MapClickCount;
	FVector2D L;
	if (ClickLocalPx(this, MapView, L)) { HandleMapClick(L, false); }
}

void UAsdaMinimapSubsystem::OnBigMapClicked()
{
	if (AsdaMinimapFishingGate(GetGameInstance(), TEXT("1729 «идти сюда»"))) { return; }   // ★ В4 рыбалки
	FVector2D L;
	if (ClickLocalPx(this, BigMapView, L)) { HandleMapClick(L, true); }
}

// ── Раскладка: разворот/сворот (MiniMap_SetMaxMin@0xD64E70 + uiPanel9SliceLayout) ────────────────
void UAsdaMinimapSubsystem::ApplyLayout()
{
	if (!MinimapWidget) { return; }

	const float W = WinW();                                   // 133 / 260
	const float H = bExpanded ? 260.f : 134.f;                 // JUI 134 → SetMaxMin 260
	const float L = -W;                                        // окно прижато к правому краю (anchor 1,0)
	const float T = kWinTop;

	// ── Рамка окна (Box_White3Stroke, 8 кусков, угол 3 px) ──
	auto Frame = [&](const TCHAR* Name, float X, float Y, float SX, float SY)
	{
		SetRect(MinimapWidget->GetWidgetFromName(Name), X, Y, SX, SY);
	};
	const float C = 3.f;
	Frame(TEXT("MINIMAP_WIN_0_0"), L,         T,         C,       C);
	Frame(TEXT("MINIMAP_WIN_0_1"), L + C,     T,         W - 2*C, C);
	Frame(TEXT("MINIMAP_WIN_0_2"), L + W - C, T,         C,       C);
	Frame(TEXT("MINIMAP_WIN_0_3"), L,         T + C,     C,       H - 2*C);
	Frame(TEXT("MINIMAP_WIN_0_4"), L + W - C, T + C,     C,       H - 2*C);
	Frame(TEXT("MINIMAP_WIN_0_5"), L,         T + H - C, C,       C);
	Frame(TEXT("MINIMAP_WIN_0_6"), L + C,     T + H - C, W - 2*C, C);
	Frame(TEXT("MINIMAP_WIN_0_7"), L + W - C, T + H - C, C,       C);

	// ── Чёрная рамка области карты MMap_BkBoder (1 px, вложена в окно с отступом 2) ──
	const float BW = W - 4.f, BH = H - 4.f;
	SetRect(MinimapWidget->GetWidgetFromName(TEXT("MMap_BkBoder_grp")), L + 2.f, kMapTop, BW, BH);
	auto Stroke = [&](const TCHAR* Name, float X, float Y, float SX, float SY)
	{
		SetRect(MinimapWidget->GetWidgetFromName(Name), X, Y, SX, SY);
	};
	Stroke(TEXT("MMap_BkBoder_20_0"), 0,        0,        1,        1);
	Stroke(TEXT("MMap_BkBoder_20_1"), 1,        0,        BW - 2.f, 1);
	Stroke(TEXT("MMap_BkBoder_20_2"), BW - 1.f, 0,        1,        1);
	Stroke(TEXT("MMap_BkBoder_20_3"), 0,        1,        1,        BH - 2.f);
	Stroke(TEXT("MMap_BkBoder_20_4"), BW - 1.f, 1,        1,        BH - 2.f);
	Stroke(TEXT("MMap_BkBoder_20_5"), 0,        BH - 1.f, 1,        1);
	Stroke(TEXT("MMap_BkBoder_20_6"), 1,        BH - 1.f, BW - 2.f, 1);
	Stroke(TEXT("MMap_BkBoder_20_7"), BW - 1.f, BH - 1.f, 1,        1);
	// Компас: JUI AlignY mode 8 = от НИЗА рамки (компакт 130−39 = 91 — совпадает с импортом).
	SetRect(MinimapWidget->GetWidgetFromName(TEXT("MMap_CompassIcon_21_0")), 5.f, BH - 39.f, 11.f, 34.f);

	// ── Окно карты: компакт (L, 36) 130×130, развёрнуто (L+1, 36) 256×256 (uiMinimapBuildQuads) ──
	const float MP = MapPx();
	SetRect(MapView,   L + (bExpanded ? 1.f : 0.f), kMapTop, MP, MP);
	SetRect(MarkLayer, L + (bExpanded ? 1.f : 0.f), kMapTop, MP, MP);
	SetRect(MapImage, 0.f, 0.f, MP, MP);
	SetRect(MinimapWidget->GetWidgetFromName(TEXT("MMap_ClickArea")),
		L + (bExpanded ? 1.f : 0.f), kMapTop, MP, MP);

	// ── Контролы, привязанные к ЛЕВОМУ краю окна (JUI AlignXPos mode 1/5) — едут вместе с ним ──
	//    Контролы с mode 2 (от правого края) остаются на месте: экран-правый анкор их уже держит.
	static const TCHAR* kLeftBound[] = {
		TEXT("MMap_CBar_grp"), TEXT("MMap_CBar2_grp"), TEXT("MINIMAP_UPDOWN"),
		TEXT("Indicator_MiniMap_grp"), TEXT("Change_Channel_Btn_grp"),
		TEXT("CAMPWAR_Out_Button_grp"), TEXT("Gear_bookX_btn"),
	};
	// ── Контролы, привязанные к НИЗУ окна (JUI AlignYPos mode 8) ──
	static const TCHAR* kBottomBound[] = {
		TEXT("MMap_XYBg_grp"), TEXT("MINIMAP_MAXBUT"), TEXT("MINIMAP_UPDOWN"), TEXT("MINIMAP_ZOOM"),
	};

	if (!bLayoutCached)
	{
		BasePos.Reset();
		auto Cache = [&](const TCHAR* N)
		{
			if (UCanvasPanelSlot* S = SlotOf(MinimapWidget->GetWidgetFromName(N)))
			{
				const FMargin M = S->GetOffsets();
				BasePos.Add(FName(N), FVector2D(M.Left, M.Top));
			}
		};
		for (const TCHAR* N : kLeftBound)   { Cache(N); }
		for (const TCHAR* N : kBottomBound) { Cache(N); }
		bLayoutCached = true;
	}

	const float DX = bExpanded ? -(kWinExpandW - kWinCompactW) : 0.f;   // −127
	const float DY = bExpanded ? (260.f - 134.f) : 0.f;                 // +126
	auto Place = [&](const TCHAR* N, float dx, float dy)
	{
		const FVector2D* B = BasePos.Find(FName(N));
		if (!B) { return; }
		if (UCanvasPanelSlot* S = SlotOf(MinimapWidget->GetWidgetFromName(N)))
		{
			FMargin M = S->GetOffsets();
			M.Left = B->X + dx; M.Top = B->Y + dy;
			S->SetOffsets(M);
		}
	};
	for (const TCHAR* N : kLeftBound)   { Place(N, DX, 0.f); }
	for (const TCHAR* N : kBottomBound) { Place(N, DX, DY); }
	// MINIMAP_UPDOWN попал в оба списка — доставим ему обе поправки разом.
	Place(TEXT("MINIMAP_UPDOWN"), DX, DY);
	// MINIMAP_ZOOM/MAXBUT привязаны к ПРАВОМУ краю по X (mode 2) — только вертикальный сдвиг.
	Place(TEXT("MINIMAP_ZOOM"),   0.f, DY);
	Place(TEXT("MINIMAP_MAXBUT"), 0.f, DY);
	Place(TEXT("MMap_XYBg_grp"),  0.f, DY);

	// ── Планка заголовка развёрнутого режима: контрол 1715 MMap_CBar2 виден ТОЛЬКО в развёрнутом
	//    (эталон MiniMap_SetMaxMin@0xD64E70 / uiDlgExpandPanelByCtrlId: SetVisible(pCBar2, 1|0)).
	//    В JUI у него Visible="0", поэтому импортёр скрыл и группу, и её собственный план — показываем оба
	//    (SetVisible контрола в оригинале показывает и его картинку).
	if (UWidget* CB2 = MinimapWidget->GetWidgetFromName(TEXT("MMap_CBar2_grp")))
	{
		AsdaUi::SetShownDecor(CB2, bExpanded);
	}
	if (UWidget* CB2P = MinimapWidget->GetWidgetFromName(TEXT("MMap_CBar2_12_0")))
	{
		AsdaUi::SetShownNoHit(CB2P, bExpanded);
	}
	UpdateCampWarButton();
	// Низ мини-карты меняется при разворачивании/сворачивании — панель занятости едет за ним.
	LayoutOccupationBox();

}

// ── ★ 17520 CAMPWAR_Out_Button — «Выйти из боя» (подпись = нотис 10046) ──────────────────────────
//  ВИДИМОСТЬ. В JUI Visible="0". Показывает её эталон только через uiDlgSetDetailVisible@0xD66E60:
//      *(this+268) = a2;  CControl_SetVisible(this[51] /*17520*/, a2);  CControl_SetVisible(this[17] /*панель
//      каналов 17501*/, 0);  uiDlgExpandShowDetail(this);
//  а его единственный вызывающий — campWarWnd_ToggleVisible@0xCA1690 (окно лагерной войны), который
//  включается при входе на карту лагерной войны: netHandleCampWarMapEntry (network.c:43101-43125) считает
//  bCampWarVisible = (*(g_aMapEntityTable[mapId] + 69) == 1) — тот самый байт +69 записи реестра карт
//  («campwar/PK»). В порту это поле FAsdaMapRow::CampwarPkFlag (AsdaMapTable.h, «+69 campwar/PK»).
//  ⚠ Колонка CampwarPkFlag в DT_MapTable сейчас НЕ ЗАПОЛНЕНА (экспортёра для неё нет) → кнопка скрыта на
//  всех картах, что и требуется на обычных картах; как только колонка появится, условие заработает само.
//  КЛИК. Эталон MiniMap_Window_OnCommand@0xD64050 case 0x4470 (=17520): если тип текущей карты != 6 и != 29,
//  показывается нотис 10047. НИКАКОГО ПАКЕТА клиент в этом обработчике НЕ ШЛЁТ (весь срез проверен).
void UAsdaMinimapSubsystem::UpdateCampWarButton()
{
	if (!MinimapWidget) { return; }
	UGameInstance* GI = GetGameInstance();
	UAsdaLoadManagerSubsystem* LM = GI ? GI->GetSubsystem<UAsdaLoadManagerSubsystem>() : nullptr;
	const FAsdaMapRow* Row = LM ? LM->GetMapRow(MapId) : nullptr;
	// ★ 30.10.2026 [ОРИГИНАЛ] (`asda.CampWar.Battle 1`): видимость 17520 = a2 `campWarWnd_ToggleVisible` (HUD боя +0x2388) —
	//   `uiDlgSetDetailVisible@0xD66E60`: +0x10C = a2, 17520 = a2, панель каналов 17501 прячется. Ключ 0 — прежний порт (флаг карты +69).
	const UAsdaCampWarBattleSubsystem* CwB = GI ? GI->GetSubsystem<UAsdaCampWarBattleSubsystem>() : nullptr;
	const bool bByHud = CwB && UAsdaCampWarBattleSubsystem::IsBattleOn();
	const bool bShow = bByHud ? CwB->IsHudOn() : (Row && Row->CampwarPkFlag == 1);
	if (bShow == bCampWarShown && MapId == ShownCampWarMapId) { return; }
	const bool bChanged = (bShow != bCampWarShown);
	bCampWarShown = bShow; ShownCampWarMapId = MapId;
	if (bByHud && bChanged && bChannelPanelOpen) { SetChannelPanelOpen(false); }   // `CControl_SetVisible(17501, 0)`
	auto Vis = [&](const TCHAR* N, ESlateVisibility V)
	{
		if (UWidget* W = MinimapWidget->GetWidgetFromName(N)) { W->SetVisibility(V); }
	};
	Vis(TEXT("CAMPWAR_Out_Button_grp"),  bShow ? ESlateVisibility::SelfHitTestInvisible : ESlateVisibility::Collapsed);
	Vis(TEXT("CAMPWAR_Out_Button"),      bShow ? ESlateVisibility::Visible             : ESlateVisibility::Collapsed);
	Vis(TEXT("CAMPWAR_Out_Button_Text"), bShow ? ESlateVisibility::SelfHitTestInvisible : ESlateVisibility::Collapsed);
	if (bShow)
	{
		// Подпись кнопки — нотис 10046 (эталон MiniMap_BuildUI: Collection_AddString(self[51], loc 10046)).
		if (UTextBlock* T = Cast<UTextBlock>(MinimapWidget->GetWidgetFromName(TEXT("CAMPWAR_Out_Button_Text"))))
		{
			if (UAsdaTextSubsystem* TS = GI ? GI->GetSubsystem<UAsdaTextSubsystem>() : nullptr)
			{
				T->SetText(FText::FromString(TS->GetTextById(10046)));
			}
		}
	}
	UE_LOG(LogAsdaMinimap, Log, TEXT("[миникарта] кнопка «выйти из боя» (17520)=%s (карта=%d, campwar-флаг=%d)"),
		bShow ? TEXT("показана") : TEXT("скрыта"), MapId, Row ? Row->CampwarPkFlag : -1);
}

void UAsdaMinimapSubsystem::OnCampWarOutClicked()
{
	// ★ 30.10.2026 [ОРИГИНАЛ] (`asda.CampWar.Battle 1`, BATTLE_SPEC §3.9): состояние ≠ 6/29 → TWOBUT 10047 → «Да» → 6731 (`Clan_Send_Leave`).
	//   Прежняя запись «пакета нет» была неверна: «Да» общего обработчика окон (`OnSendItemUpgrade@0xD74180` case 0x273F) шлёт 6731.
	if (UAsdaCampWarBattleSubsystem* CwB = GetGameInstance() ? GetGameInstance()->GetSubsystem<UAsdaCampWarBattleSubsystem>() : nullptr)
	{
		if (UAsdaCampWarBattleSubsystem::IsBattleOn()) { CwB->OnOutButton(); return; }
	}
	// Эталон case 0x4470: тип карты != 6 && != 29 → нотис 10047. Тип текущей карты приходит клиенту в
	//   структуре персонажа (Net_GetCurrentMapType); в порту этого поля пока нет, поэтому гейт по типу
	//   не проверяется — нотис показывается всегда. Пакета в клиентском срезе НЕТ.
	UGameInstance* GI = GetGameInstance();
	UAsdaTextSubsystem* TS = GI ? GI->GetSubsystem<UAsdaTextSubsystem>() : nullptr;
	// ★ ИСПРАВЛЕНИЕ 11.10.2026 (дедуп №2, В2-A, U10d): заглушка строки лога была «нотис 10047» — стала единая «[#10047]».
	UE_LOG(LogAsdaMinimap, Log, TEXT("[миникарта] 17520 «выйти из боя»: %s"),
		TS ? *TS->GetTextById(10047) : *FAsdaLang::MissingTag(10047));
}

// ── Кнопки ───────────────────────────────────────────────────────────────────────────────────────
void UAsdaMinimapSubsystem::SetExpanded(bool bOn)
{
	if (bExpanded == bOn) { return; }
	bExpanded = bOn;
	ApplyLayout();
	UE_LOG(LogAsdaMinimap, Log, TEXT("[миникарта] режим=%s зум=%s"),
		bExpanded ? TEXT("развёрнутая 256") : TEXT("компактная 130"), bZoom ? TEXT("вкл") : TEXT("выкл"));
}

void UAsdaMinimapSubsystem::SetZoom(bool bOn)
{
	if (bZoom == bOn) { return; }
	bZoom = bOn;
	UE_LOG(LogAsdaMinimap, Log, TEXT("[миникарта] зум=%s (видно текселов=%.0f)"),
		bZoom ? TEXT("вкл") : TEXT("выкл"), ViewTexels());
}

void UAsdaMinimapSubsystem::SetMinimapVisible(bool bOn)
{
	SetMinimapVisibleFrom(bOn, TEXT("внешний вызов"));
}

// Эталон: скрыть → `uiDlgCollapseToCompact@0xD64280` (1702 прячем, 1700 показываем),
//         показать → `uiMinimapWindow_Open@0xD64330` (зеркально). Обе — ТОЛЬКО по действию игрока:
//         ни `uiCloseAllWindows`, ни `uiCloseAllWindowsOnLeave` окна 1702 не касаются.
void UAsdaMinimapSubsystem::SetMinimapVisibleFrom(bool bOn, const TCHAR* Reason)
{
	const bool bWas = bVisible;
	bVisible = bOn;
	if (!bOn) { HelpTip.Hide(GetGameInstance(), EAsdaTipOwner::Minimap); }
	if (!bOn) { TargetTip.Hide(GetGameInstance(), EAsdaTipOwner::Minimap); TargetTip.HoverKey = -1; }   // ★ 31.10.2026: окно карты ушло   // ★ 30.10.2026: кнопки ушли из-под курсора
	// SelfHitTestInvisible — см. EnsureWidgets: корень окна не должен ловить мышь на весь экран.
	AsdaUi::SetShownDecor(MinimapWidget, bOn);
	AsdaUi::SetShownDecor(MinIconWidget, !bOn);
	UE_LOG(LogAsdaMinimap, Log, TEXT("[миникарта] видимость=%s (было %s), инициатор: %s"),
		bOn  ? TEXT("окно 1702") : TEXT("иконка 1700"),
		bWas ? TEXT("окно 1702") : TEXT("иконка 1700"),
		Reason ? Reason : TEXT("?"));
}

// ── ★ КАРТА МИРА: открыть/закрыть (inventory_ToggleControlBarWindow@0xD64A30, 1:1) ───────────────
//   Один и тот же метод зовут: клавиша M (cmd 37), кнопка мини-карты 1707 и — в будущем — главный
//   интерфейс. Порядок проверок и нотисов повторён построчно.
void UAsdaMinimapSubsystem::ToggleWorldMap()
{
	// ★ 10.11.2026 [ОРИГИНАЛ] обучение: первая строка `inventory_ToggleControlBarWindow@0xD64A30` — байт 78 (до гейтов).
	AsdaTutorial::Mark(this, EAsdaTutorialFlag::MapToggle);
	UGameInstance* GI = GetGameInstance();
	UAsdaTextSubsystem*   TS = GI ? GI->GetSubsystem<UAsdaTextSubsystem>()   : nullptr;
	UAsdaWindowSubsystem* WM = GI ? GI->GetSubsystem<UAsdaWindowSubsystem>() : nullptr;
	auto Notice = [&](int32 Id)
	{
		// ★ ИСПРАВЛЕНИЕ 11.10.2026 (дедуп №2, В2-A, U10d): заглушка строки лога была «?» — стала единая «[#id]».
		UE_LOG(LogAsdaMinimap, Log, TEXT("[карта мира] открытие запрещено: %s (нотис %d)"),
			TS ? *TS->GetTextById(Id) : *FAsdaLang::MissingTag(Id), Id);
	};
	// Эталон: парикмахерская 99000 / элитный магазин 99001 / военный магазин 67200 `HONOR_itemmall_Win` 110611 / модальный нотис 99002.
	//   ★ 27.09.2026 (ревизия WASD): 110611 — военный магазин 67200 (`g_pItemTooltipWindow` — миснейм, байт +776 пишет
	//   `UI_HonorShopDlg_Init`), а не магазин бустеров, как было подписано здесь.
	//   В порту из этих окон существует только модальный диалог подтверждения — его и проверяем;
	//   остальные три подсистемы ещё не портированы (их гейты добавятся вместе с ними).
	if (WM && WM->IsAnyModalOpen()) { Notice(99002); return; }
	// ★ Волна В3 ремесла (ревизия В0–В2 С-1): `inventory_ToggleControlBarWindow@0xD64A30` (item_inventory.c:50217) —
	//   ВСЁ тело (и открыть, и закрыть) под `!tileInteract_isControlVisible(g_pItemMainWnd) && !isBoundControlVisible(
	//   g_pItemQtyMakeDlg)`: при открытом окне ремесла клавиша M / кнопка 1707 молча ничего не делают.
	if (UAsdaCraftSubsystem::IsAnyCraftWindowOpen(GI))
	{
		UE_LOG(LogAsdaMinimap, Log, TEXT("[карта мира] тумблер отклонён молча: открыто окно ремесла 76000/77500 (item_inventory.c:50217)"));
		return;
	}

	SetBigMapVisible(!bBigMapVisible);
}

void UAsdaMinimapSubsystem::ToggleBigMap() { ToggleWorldMap(); }

void UAsdaMinimapSubsystem::SetBigMapVisible(bool bOn)
{
	UGameInstance* GI = GetGameInstance();
	UAsdaWindowSubsystem* WM = GI ? GI->GetSubsystem<UAsdaWindowSubsystem>() : nullptr;
	if (!WM) { return; }
	const bool bWas = bBigMapVisible;
	bBigMapVisible = bOn;
	// ★★★ ГВАРД, А НЕ ПРОВЕРКА НА NULL (регрессия 2026-08-18). Здесь стояло `if (bOn && !BigMapWidget)`.
	//   После сносящего `WM->CloseAll()` (вход в мир `UAsdaWorldSubsystem::RevealWorld`) указатель
	//   на окно 1710 ОСТАЁТСЯ живым объектом, но само окно снято с реестра и с вьюпорта. Проверка
	//   на null это пропускала, ветка переоткрытия не срабатывала, а `SetVisibility` ниже красила
	//   виджет, которого нет на экране — карта мира по клавише M переставала открываться навсегда.
	//   Спрашиваем ровно то же, что и мини-карта: `IsWindowLive` (указатель + тот же мир + во вьюпорте).
	if (bOn && !WM->IsWindowLive(BigMapWidget))
	{
		// Новое дерево ⇒ кэши прошлого недействительны (комбобоксы и пул маркеров).
		ResetBigMapWidgetCaches();
		// ★★★ SelfHitTestInvisible и bModal=false — окно карты НЕ модальное: в оригинале при открытой
		//   карте персонаж продолжает бегать. Модальность/Visible на корне = регрессия «пропал WASD».
		BigMapWidget = WM->OpenWindowByPath(FName(TEXT("MAXMAP_WIN")), kBigMapWbp, kBigMapZ, ESlateVisibility::SelfHitTestInvisible);
		if (BigMapWidget)
		{
			BigMapView   = Cast<UCanvasPanel>(BigMapWidget->GetWidgetFromName(TEXT("MaxMap_View")));
			BigMapImage  = Cast<UImage>(BigMapWidget->GetWidgetFromName(TEXT("MaxMap_MapImage")));
			BigMarkLayer = Cast<UCanvasPanel>(BigMapWidget->GetWidgetFromName(TEXT("MaxMap_Markers")));
			if (UButton* Cl = Cast<UButton>(BigMapWidget->GetWidgetFromName(TEXT("MAXMAP_CLOSE"))))
			{
				Cl->OnClicked.Clear();
				Cl->OnClicked.AddDynamic(this, &UAsdaMinimapSubsystem::OnBigMapCloseClicked);
			}
			if (UButton* Ck = Cast<UButton>(BigMapWidget->GetWidgetFromName(TEXT("MaxMap_ClickArea"))))
			{
				Ck->OnClicked.Clear();
				Ck->OnClicked.AddDynamic(this, &UAsdaMinimapSubsystem::OnBigMapClicked);
			}
			BuildWorldMapUI();
		}
	}
	if (BigMapWidget)
	{
		AsdaUi::SetShownDecor(BigMapWidget, bOn);
	}
	// Эталон inventory_ToggleControlBarWindow: при открытии городской флаг пересчитывается заново,
	//   причём на карте 1 (rein) он ПРИНУДИТЕЛЬНО гасится; затем this[64] = −1 + worldMapDlg_SelectCurrentMap.
	if (bOn)
	{
		AAsdaPlayerCharacter* P = nullptr;
		const UAsdaWalkGrid* G = GetGrid(P);
		if (P && G)
		{
			const FVector2D Pos = P->GetGamePosXZ();
			bool bTown = G->IsTilePeaceZone(FMath::FloorToInt(Pos.X / 100.f), FMath::FloorToInt(Pos.Y / 100.f));
			if (MapId == 1) { bTown = false; }
			bTownMode = bTown;
		}
		LoadMapTextures();
		SelectCurrentMapInCombos();
	}
	else
	{
		// Курсор, подменённый наведением на карту (18/20), обязан вернуться к обычному.
		if (bCursorOverridden)
		{
			if (AAsdaPlayerController* PC = Cast<AAsdaPlayerController>(GetGameInstance() ? GetGameInstance()->GetFirstLocalPlayerController() : nullptr))
			{
				PC->bUiCursorOverride = false;
				PC->SetGameCursor(AAsdaPlayerController::EAsdaCursor::Normal);
			}
			bCursorOverridden = false;
		}
		bHoverInMap = false;
		SetComboListOpen(true,  false);   // закрытое окно не должно «помнить» раскрытый список
		SetComboListOpen(false, false);
	}
	// Звук: открытие 5, закрытие 6 (csoundPlayName2DStore в inventory_ToggleControlBarWindow).
	if (bWas != bOn)
	{
		if (UAsdaSoundSubsystem* SS = GI ? GI->GetSubsystem<UAsdaSoundSubsystem>() : nullptr)
		{
			SS->PlaySound2DById(bOn ? 5 : 6);
		}
	}
	UE_LOG(LogAsdaMinimap, Log, TEXT("[карта мира] окно=%s карта=%d зона=%s"),
		bOn ? TEXT("открыто") : TEXT("закрыто"), MapId, bTownMode ? TEXT("город") : TEXT("поле"));
}

void UAsdaMinimapSubsystem::OnBigMapCloseClicked() { SetBigMapVisible(false); }
