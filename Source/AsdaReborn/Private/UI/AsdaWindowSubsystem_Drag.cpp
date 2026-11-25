// Copyright (c) 2026 @sitxovski (Discord)
// AsdaWindowSubsystem_Drag.cpp — ★ R2 10.11.2026 (разрез AsdaWindowSubsystem.cpp, дословный перенос): перетаскивание TickDrag, имитация клика/перетаскивания.
//   Общее для частей — в AsdaWindowSubsystemInternal.h; шапка и реверс — в AsdaWindowSubsystem.cpp.
#include "UI/AsdaWindowSubsystem.h"
#include "UI/AsdaUiVisibilitySubsystem.h"   // ★ 21.10.2026: Alt+Z — флаг «интерфейс скрыт» (владелец)
#include "AsdaDevConsole.h"   // ★ античит А2: ASDA_DEV_CVAR / ASDA_DEV_CONSOLE — дев-ключи и команды вне Shipping
#include "Text/AsdaLang.h"   // ★ N9 (дедуп №2, В2-A): FAsdaLang::MissingTag — единая заглушка «[#id]»
#include "UI/AsdaLocIds.h"   // Дд2/Дд3: нотисы кнопок ОБЩИХ окон (ONEBUT/TWOBUT/SHOUTING) — одно место
#include "Resources/AsdaAssetPaths.h"   // ★ волна 2 п. 7 (14.09.2026): реестр путей и правил имён ассетов (генерируемый)
#include "Engine/GameInstance.h"
#include "Engine/World.h"
#include "TimerManager.h"
#include "GameFramework/PlayerController.h"
#include "Blueprint/WidgetBlueprintLibrary.h"
#include "Components/TextBlock.h"
#include "Components/Button.h"
#include "Components/EditableText.h"   // поле ввода 1583 диалога SHOUTING_WIN
#include "Components/CheckBox.h"         // ★ приёмка: галки под ручкой (INVEN_NEW_WIN 83503)
#include "Components/EditableTextBox.h"  // ★ приёмка: поля ввода под ручкой
#include "Components/MultiLineEditableText.h"  // ★ приёмка: многострочный ввод
#include "Components/ComboBoxString.h"   // ★ приёмка: выпадающие списки
#include "Components/Slider.h"           // ★ приёмка: ползунки тянут мышь сами
#include "Components/ScrollBox.h"        // ★ приёмка: прокрутка тянет мышь сама
#include "Components/Border.h"
#include "Components/ProgressBar.h"
#include "Blueprint/WidgetTree.h"
#include "Blueprint/WidgetLayoutLibrary.h"   // DPI-скейл вьюпорта: абсолютная дельта курсора → юниты канвы
#include "Components/CanvasPanel.h"
#include "Components/CanvasPanelSlot.h"
#include "Components/PanelWidget.h"
#include "Components/Image.h"
#include "Engine/Engine.h"
#include "Engine/GameViewportClient.h"
#include "Framework/Application/SlateApplication.h"
#if !UE_BUILD_SHIPPING   // ★ 31.10.2026: журнал синтетического клика — дев-диагностика, в Shipping не входит
#include "Widgets/Input/SButton.h"         // ★ 31.10.2026: состояние кнопки под синтетическим кликом (журнал `[симклик]`)
#include "Types/ReflectionMetadata.h"   // ★ 31.10.2026: имя виджета под синтетическим кликом (журнал `[симклик]`)
#endif
#include "Fonts/FontMeasure.h"   // метрики строки для asda.UI.DumpTextMetrics   // фокус на игровой вьюпорт при базовом GameAndUI
#include "Misc/ConfigCacheIni.h"   // раскладка окон между сессиями: секция [AsdaWindowLayout] в ini игры
#include "Login/AsdaLoginData.h"   // FAsdaLoginData::Loc — подписи кнопок ТОЛЬКО из таблицы DT_GameText
// игровые loc-подписи (2288 «ОК» / 2289 «Отмена») для TWOBUT_MSGBOX   ★ R3: комментарий снятого включения "Text/AsdaTextSubsystem.h" (файл им не пользовался) — сохранён
#include "UI/AsdaUiHelpers.h"         // D1b: общая `AsdaUi::WidgetAbsRect` (хит-тест окна и рамки)
#include "UI/AsdaMessageSubsystem.h"  // D2: фолбэк однокнопочного msgbox, когда WBP не загрузился
#include "UI/AsdaHotbarSubsystem.h"      // шаг 13 `uiCloseAllWindows`: блокировка/разблокировка хотбара
// шаги 23-24: сворачивание трея и запрет его кнопок   ★ R3: комментарий снятого включения "UI/AsdaSystemMenuSubsystem.h" (файл им не пользовался) — сохранён
#include "Resources/AsdaSoundSubsystem.h" // звуки открытия/закрытия окна количества (5 / 6)
#include "UI/AsdaItemTooltipSubsystem.h"  // ★ 27.09.2026: панель 1530 для `DropTopMsgBoxBeforeNotice`
#include "UI/AsdaPurchaseBoxSubsystem.h"  // ★ 27.09.2026: панель 1559 для `DropTopMsgBoxBeforeNotice`
#include "Styling/SlateStyleRegistry.h"   // ★ гашение синей рамки фокуса: подмена кисти FocusRectangle
#include "Styling/SlateStyle.h"
#include "Styling/AppStyle.h"
#include "Brushes/SlateNoResource.h"
#include "AsdaOutgoingGate.h"               // ★ С-Р1: общий офлайн-гейт отладочных команд (RefuseDebugWhenOnline)
#include "Login/AsdaText.h"                 // FAsdaText::Loc — строки диалога `asda.UI.ConfirmPreview` (07.10.2026)
#include "Login/AsdaLoginGameMode.h"          // ★ 08.10.2026: подъём при показе не трогает экран логина
#include "World/AsdaWorldKind.h"               // ★ F7 (дедуп №2, В3): общий признак «сцена логина»
#include "GameFramework/GameModeBase.h"     // GetAuthGameMode — проверка экрана логина
#include "UnrealClient.h"                   // С-1 (07.10.2026): FViewport::ViewportResizedEvent — смена размера вьюпорта
#include "Blueprint/GameViewportSubsystem.h" // М-1 (07.10.2026): слот окна во вьюпорте — полноэкранный ли он
#include "Widgets/SOverlay.h"               // ★ 08.10.2026: подъём окна БЕЗ снятия с вьюпорта (слот оверлея вьюпорта)
#include "UI/AsdaNotice.h"                  // ★ 08.10.2026 (ревизия В3 рыбалки): нотис 6835 строгого нумпада — LocRaw / ChatColored
// ★ 08.10.2026: ColGold — цвет −11514 нотиса 6835   ★ R3: комментарий снятого включения "UI/AsdaChatSubsystem.h" (файл им не пользовался) — сохранён
#include "UI/AsdaMarkup.h"                  // ★ 09.10.2026: UAsdaRichText::ApplyFormatted — теги <FC=…> в теле окон-сообщений
#include "Components/RichTextBlock.h"      // ★ 09.10.2026: тело окна-сообщения после первой подмены — URichTextBlock
#include "AsdaWindowSubsystemInternal.h"   // ★ 23.10.2026 разнос: общее для частей AsdaWindowSubsystem*.cpp (LogAsdaWindow, чистые хелперы)
#include "AsdaWindowSubsystemInternal.h"   // ★ R2 10.11.2026: общее для частей AsdaWindowSubsystem*.cpp

// ★ КЛЮЧ ОТКАТА (и он же — инструмент доказательства). 1 ⇒ окно поднимается наверх ПРЯМО НА
//   НАЖАТИИ, как было до 2026-08-19; в этом режиме первый клик по кнопке/галочке съедается
//   потерей захвата мыши (разбор — у `SetWindowZOrder`). 0 (по умолчанию) ⇒ подъём отложен до
//   отпускания. Держим ключ, чтобы разницу можно было показать A/B в одном прогоне.
static ASDA_DEV_CVAR(int32, CVarLegacyRaiseOnPress,
	TEXT("asda.UI.LegacyRaiseOnPress"), 0,
	TEXT("1 — поднимать окно наверх на НАЖАТИИ (старое поведение, съедает первый клик); 0 — на отпускании."),
	ECVF_Default);

// ★ [ОРИГИНАЛ] Пока показана общая подсказка (`g_ItemTooltip`: флаги менеджера +0x1B0/+0x1B1, ставят `uiTooltipShowAndLayout`
//   и `tooltipShowAtCursor`, снимает `CControl_KillFocus@0xEA2D30`), `sub_12AFF80` окна НЕ переставляет. 1 — как оригинал; 0 — подъём
//   и при показанной подсказке. Действует только при `asda.UI.RaiseOnBodyClickAll 1`.
static ASDA_DEV_CVAR(int32, CVarRaiseTooltipGate,
	TEXT("asda.UI.RaiseTooltipGate"), 1,
	TEXT("1 — пока показана общая подсказка предмета, нажатие окно не поднимает (как оригинал, флаги +0x1B0/+0x1B1); 0 — поднимает."),
	ECVF_Default);

// ★★★ КЛЮЧ ОТКАТА К ПРЕЖНЕМУ ПОВЕДЕНИЮ ПОРТА: «ТАЩИТЬ ОКНО ДАЖЕ ЗА КОНТРОЛ ПОД РУЧКОЙ».
//   ЗАЧЕМ ОН. Порт проверяет прямоугольник ручки СВОИМ тестом, параллельно хит-тесту Slate, и до
//   поправки финальной приёмки волны 8 срабатывали ОБА: нажатие ровно на крестике окна и закрывало
//   его (через Slate), и начинало перетаскивание (через наш тест). Живой прогон поймал это на
//   `Disassembly_ClsBtn` — окно уехало на −60 по X. У ОРИГИНАЛА так не бывает: `uiControlHitTest
//   @0x12DB470` (ui_core.c:107141-107200) возвращает РОВНО ОДИН контрол, а событие перетаскивания
//   поднимает только `CControl_OnLButtonDown` победителя (:107295-107296); крестик объявлен позже
//   ручки, значит он выше и забирает хит-тест.
//   0 (по умолчанию) — как оригинал: под курсором настоящий контрол ⇒ захват не берём.
//   1 — прежнее поведение порта (окно тащится и за свои кнопки).
static ASDA_DEV_CVAR(int32, CVarDragOverControls,
	TEXT("asda.UI.DragOverControls"), 0,
	TEXT("0 (по умолчанию) — перетаскивание НЕ начинается, если под курсором настоящий контрол ")
	TEXT("(кнопка, галка, поле ввода, список, ползунок): так делает оригинал, у него хит-тест ")
	TEXT("отдаёт один контрол. 1 — прежнее поведение порта: окно тащится и за собственный крестик."),
	ECVF_Default);

// ★ 26.10.2026 [ИСПРАВЛЕНИЕ] (волна quest_close_btn, доделка): ВИДИМОСТЬ РУЧКИ ПО ВСЕЙ ЦЕПОЧКЕ РОДИТЕЛЕЙ.
//   Эталон `uiControlHitTest@0x12DB470` (ui_core.c:107180-107193): контрол ловит нажатие, только если виден ОН И КАЖДЫЙ
//   его родитель (`for (i = parent(this); i; i = parent(i)) if (!CControl_IsVisible(i)) → не попал`); событие
//   перетаскивания поднимает только победитель хит-теста (`CControl_OnLButtonDown`, :107295-107296). Захват ручки порта
//   смотрел видимость ТОЛЬКО самой ручки, а её прямоугольник брал из кэша Slate — у ручки внутри свёрнутой группы он
//   устаревший (окно 700: ручка `Quest_Base2_111_DRAG` правой страницы 732 при свёрнутой странице держит прямоугольник
//   прежней раскладки — нажатие в этой полосе над окном брало захват и тащило окно).
//   1 (по умолчанию) — как эталон: ручка со скрытым предком не ловит; 0 — прежнее (только своя видимость).
static ASDA_DEV_CVAR(int32, CVarDragHandleChainVisible,
	TEXT("asda.UI.DragHandleChainVisible"), 1,
	TEXT("1 (по умолчанию) — ручка перетаскивания ловит нажатие, только если видна она и вся цепочка её родителей ")
	TEXT("(эталон uiControlHitTest); 0 — прежнее: проверялась только видимость самой ручки (скрытая ручка в свёрнутой ")
	TEXT("группе держала устаревший прямоугольник и могла начать перетаскивание)."),
	ECVF_Default);

// Ручка и все её родители в дереве окна не Collapsed/Hidden (порт цикла видимости `uiControlHitTest`).
static bool AsdaDragHandleChainVisible(const UWidget* HW)
{
	for (const UWidget* P = HW; P; P = P->GetParent())
	{
		const ESlateVisibility V = P->GetVisibility();
		if (V == ESlateVisibility::Collapsed || V == ESlateVisibility::Hidden) { return false; }
	}
	return true;
}

// ═════════════════ ★★★ ПЕРЕТАСКИВАНИЕ ОКНА ЗА РУЧКУ (эталон — см. шапку объявления) ═════════════
// ★★★ 09.11.2026 [ИСПРАВЛЕНИЕ] «Окно плывёт при быстром перетаскивании» (пачка 20, баг 5). В кадре игры отставания нет: `TickDrag` ставит ручку по курсору
//   этого же кадра, без сглаживания, как оригинал (`uiControl_RepositionByDelta@0x12F1C30`, самотест `asda.UI.SelfTestDragFollow`). Отстаёт ПОКАЗ кадра: у UE поток
//   рендера идёт на кадр позади игры (`r.OneFrameThreadLag 1`), поток RHI ещё на кадр, а аппаратный курсор Windows рисуется сразу. Оригинал однопоточный (D3D9), этой
//   очереди у него нет. На время перетаскивания очередь укорачивается: `r.OneFrameThreadLag 0` (рендер в том же кадре) и `r.GTSyncType 1` (игра ждёт RHI прошлого кадра).
//   После отпускания прежние значения возвращаются. Цена — кадры перетаскивания без параллельной работы потоков. 1 = так; 0 = как раньше.
static ASDA_DEV_CVAR(int32, CVarDragLowLatency,
	TEXT("asda.UI.DragLowLatency"), 1,
	TEXT("[ИСПРАВЛЕНИЕ] 1 (по умолчанию) = пока окно тащится, очередь кадров укорочена (r.OneFrameThreadLag 0, r.GTSyncType 1), окно идёт за курсором; 0 = как раньше."),
	ECVF_Default);

// Включить/снять режим низкой задержки показа; значения до включения возвращаются.
static void AsdaApplyDragLowLatency(bool bWant)
{
	static bool bOn = false;            // режим сейчас включён
	static int32 SavedLag = 1;          // r.OneFrameThreadLag до включения
	static int32 SavedSync = 0;         // r.GTSyncType до включения
	bWant = bWant && CVarDragLowLatency.GetValueOnGameThread() != 0;
	if (bWant == bOn) { return; }
	IConsoleVariable* Lag = IConsoleManager::Get().FindConsoleVariable(TEXT("r.OneFrameThreadLag"));
	IConsoleVariable* Sync = IConsoleManager::Get().FindConsoleVariable(TEXT("r.GTSyncType"));
	if (!Lag || !Sync) { return; }
	if (bWant)
	{
		SavedLag = Lag->GetInt(); SavedSync = Sync->GetInt();
		Lag->Set(0, ECVF_SetByCode); Sync->Set(1, ECVF_SetByCode);
	}
	else
	{
		Lag->Set(SavedLag, ECVF_SetByCode); Sync->Set(SavedSync, ECVF_SetByCode);
	}
	bOn = bWant;
	UE_LOG(LogAsdaWindow, Verbose, TEXT("[перетаскивание] низкая задержка показа %s: r.OneFrameThreadLag=%d r.GTSyncType=%d"),
		bOn ? TEXT("включена") : TEXT("снята"), Lag->GetInt(), Sync->GetInt());
}

bool UAsdaWindowSubsystem::TickDrag(float /*DeltaTime*/)
{
	if (!FSlateApplication::IsInitialized()) { return true; }
	// ★★★ СИНТЕТИЧЕСКИЙ КЛИК (`asda.UI.SimClick`) — РОВНО ЗДЕСЬ, в начале кадрового разбора.
	//   Настоящее нажатие Slate получает в `PumpMessages` ДО общего тикера, то есть до этой функции.
	//   Инъекция в этой же точке даёт тот же порядок «нажатие → разбор кадра → … → отпускание»,
	//   иначе проверка не воспроизводила бы съеденный клик (разбор — у `SetWindowZOrder`).
	TickSimulatedClick();
	// ★ Аудит №3 (B3): опрос ЛКМ, фронт и курсор — общая точка. Запись латча (`Mouse.Update()`)
	//   стоит ровно там же, где стояла прежняя строка `bPrevLmbDown = bDown` — в самом начале
	//   разбора, ДО всех веток и ранних выходов.
	Mouse.Update();
	const bool bDown = Mouse.bLmb;
	const FVector2D Cur = Mouse.Cursor;
	const bool bPressEdge = Mouse.bLmbDown;
	AsdaApplyDragLowLatency(bDown && !DragWindowId.IsNone());   // ★ 09.11.2026: пока окно тащится — короткая очередь кадров
	// ★ Подъём по телу (06.10.2026): ключ, фронт СКМ (у общего опроса фронта СКМ нет) и снимок фокуса ДО нажатия.
	const bool bRaiseAll = CVarRaiseOnBodyClickAll.GetValueOnGameThread() != 0;      // правила эталона включены
	const bool bMmbDown = Mouse.bMmb && !bPrevMmbRaise;                               // фронт нажатия СКМ
	bPrevMmbRaise = Mouse.bMmb;
	if (!(bPressEdge || Mouse.bRmbDown || bMmbDown))
	{
		FocusBeforePress = FSlateApplication::Get().GetKeyboardFocusedWidget();       // фокус прошлого кадра = до нажатия
	}
	// [ОРИГИНАЛ] Подсказка показана (+0x1B0/+0x1B1) ⇒ `sub_12AFF80` не переставляет (ключ `asda.UI.RaiseTooltipGate`).
	auto TooltipBlocksRaise = [this]() -> bool
	{
		if (CVarRaiseTooltipGate.GetValueOnGameThread() == 0) { return false; }
		const UAsdaItemTooltipSubsystem* Tip = GetGameInstance() ? GetGameInstance()->GetSubsystem<UAsdaItemTooltipSubsystem>() : nullptr;
		// ★ Ревизия (МЕЛК-2): флаги эталона ставит ОБЩИЙ `g_ItemTooltip` — и карточка, и однострочная подсказка у курсора
		//   (`tooltipShowAtCursor`, тот же 5000), и вторая плашка (+0x1B1, 5050). В порту это общие плашки ITEM_TIPWIN /
		//   ItemCompareTip_Win: однострочные подсказки (эмоции, ссылки, награды) идут через `ShowTextTip` той же плашки.
		return (Tip && Tip->IsTooltipShown()) || IsWindowVisible(FName(TEXT("ITEM_TIPWIN"))) || IsWindowVisible(FName(TEXT("ItemCompareTip_Win")));
	};
	// [ОРИГИНАЛ] Нажатие ПКМ/СКМ тоже поднимает окно (события 14/17 `uiContainerHandleMouse`). Подъём — на отпускании ВСЕХ кнопок.
	if (bRaiseAll && !bDown && (Mouse.bRmbDown || bMmbDown) && DragWindowId.IsNone())
	{
		const FName Catcher = FindWindowCatchingAt(Cur);
		ApplyPressFocusRule(Catcher);
		if (!Catcher.IsNone() && IsBodyRaiseEligible(Catcher))
		{
			if (TooltipBlocksRaise())
			{
				UE_LOG(LogAsdaWindow, Verbose, TEXT("[z-порядок] «%s»: показана подсказка — подъёма нет (эталон +0x1B0)"), *Catcher.ToString());
			}
			else { PendingRaiseWindowId = Catcher; }
		}
	}

	// ★ Кламп восстановленных из ini позиций — здесь же: нужен покадровый опрос, пока Slate не
	//   посчитает геометрию окна (в кадре создания её ещё нет). Пустое множество стоит ноль.
	TickLayoutRestore();

	if (!bDown)
	{
		if (!DragWindowId.IsNone())
		{
			// ОТПУСКАНИЕ ПОСЛЕ ПЕРЕТАСКИВАНИЯ = момент записи позиции в ini (а не каждый кадр:
			//   писать файл во время движения мыши нельзя). ★ Пакет 4: сперва вернуть в экран, если отпустили за краем.
			ClampDragOnRelease();
			FlushWindowLayout();
		}
		DragWindowId = NAME_None;
		DragHandleIndex = INDEX_NONE;
		// ★★★ ОТЛОЖЕННЫЙ ПОДЪЁМ ОКНА — см. обоснование у `PendingRaiseWindowId` в заголовке.
		//   Подъём делает `SetWindowZOrder`, а тот перевешивает окно во вьюпорте; сделанный МЕЖДУ
		//   нажатием и отпусканием, он рвёт путь захвата мыши и Slate съедает клик целиком.
		//   Поэтому щелчок по телу окна поднимает его НА ОТПУСКАНИИ, когда клик уже доставлен.
		// ★ 06.10.2026: при правилах эталона ждём отпускания и ПКМ/СКМ (перевешивание при зажатой кнопке рвёт её захват).
		// ★ Ревизия (СРЕДН-2): отложенный возврат фокуса строке ввода — на отпускании ВСЕХ кнопок (клик уже доставлен Slate).
		if (PendingFocusRestore.IsValid() && !Mouse.bRmb && !Mouse.bMmb)
		{
			const TSharedPtr<SWidget> Was = PendingFocusRestore.Pin();
			PendingFocusRestore.Reset();
			const TSharedPtr<SWidget> Now = FSlateApplication::Get().GetKeyboardFocusedWidget();
			const bool bOtherEdit = Now.IsValid() && Now != Was && Now->GetType().ToString().Contains(TEXT("Editable"));
			if (Was.IsValid() && Now != Was && !bOtherEdit && !FindWindowOwningSlate(Was).IsNone())
			{
				FSlateApplication::Get().SetKeyboardFocus(Was, EFocusCause::SetDirectly);
				UE_LOG(LogAsdaWindow, Log, TEXT("[фокус] отпускание — фокус возвращён полю окна «%s»"), *FindWindowOwningSlate(Was).ToString());
			}
		}
		if (!PendingRaiseWindowId.IsNone() && (!bRaiseAll || (!Mouse.bRmb && !Mouse.bMmb)))
		{
			const FName Raise = PendingRaiseWindowId;
			PendingRaiseWindowId = NAME_None;
			const TSharedPtr<SWidget> Foc = bRaiseAll ? FSlateApplication::Get().GetKeyboardFocusedWidget() : nullptr;
			// ★★★ 08.10.2026: [ОРИГИНАЛ] тело поднимается на НАЖАТИИ, окна, показанные/поднятые щелчком, — после (отложенный список
			//   `CClass_MapInsert`), то есть выше. Порт поднимает тело на отпускании — поэтому тело встаёт ПОД самое нижнее из окон,
			//   поднятых этим щелчком (их не перевешиваем: у них может быть фокус в строке ввода). Закрытое щелчком тело не трогаем.
			int32 MinZ = MAX_int32;
			if (CVarOpenOnTop.GetValueOnGameThread() != 0)
			{
				for (const FName& Id : RaisedDuringPress)
				{
					const FAsdaWindowEntry* R = Windows.Find(Id);
					if (R && AsdaWindowIsEntryVisible(*R) && R->ZOrder < kAlwaysTopZ && !R->bModal) { MinZ = FMath::Min(MinZ, R->ZOrder); }
				}
			}
			RaisedDuringPress.Reset();
			const FAsdaWindowEntry* B = Windows.Find(Raise);
			if (B && !AsdaWindowIsEntryVisible(*B)) {}   // тело закрыто щелчком — поднимать нечего
			else if (MinZ != MAX_int32 && B && !B->bAlwaysTopRuntime && B->ZOrder < kAlwaysTopZ)
			{
				const int32 Want = FMath::Max(MinZ - 1, kRaiseBandFloor);
				if (B->ZOrder < Want)
				{
					UE_LOG(LogAsdaWindow, Log, TEXT("[открытие поверх] тело «%s» по щелчку — под открытым им окном: z %d → %d"), *Raise.ToString(), B->ZOrder, Want);
					SetWindowZOrder(Raise, Want);
				}
				RaiseOwnedAbove(Raise);
			}
			else { BringWindowToFront(Raise); }
			if (PendingShowRaise.Num() > 0) { FlushShowRaise(TEXT("после подъёма тела по щелчку")); }
			if (bRaiseAll) { RestoreFocusAfterRaise(Foc); }
		}
		else if (PendingShowRaise.Num() > 0 && PendingRaiseWindowId.IsNone()) { FlushShowRaise(TEXT("отпускание кнопки мыши")); }
		if (PendingRaiseWindowId.IsNone()) { RaisedDuringPress.Reset(); }
		return true;
	}

	// ★★★ МОДАЛЬНЫЙ ГЕЙТ (эталон `uiIsAnyModalWindowOpen@0xB79520` + §B.4: захваченный контрол
	//   `pManager+104` забирает ВЕСЬ диспетч мыши). Пока открыто модальное окно, тянуть можно только
	//   его само (`SHOUTING_WIN` 1580 ручку в .JUI имеет) — нижние окна мышь не получают вовсе.
	const bool bModalUp = IsAnyModalOpen();
	if (bModalUp && !DragWindowId.IsNone())
	{
		const FAsdaWindowEntry* DragE = Windows.Find(DragWindowId);
		// ★ 10.10.2026: общая точка гейта мыши модалки. Без точки (modal_lock2): начатое до модалки перетаскивание корня 74000
		//   невозможно — 74200 открывается двойным щелчком по ячейке, а не во время проводки; захват под модалкой — `…At` ниже.
		if (!DragE || !PassesModalMouseGate(DragWindowId, *DragE))
		{
			// ★★★ П8-9, ВОЛНА 9: перетаскивание надо снять КОРРЕКТНО, а не бросить в подвешенном
			//   состоянии. Нотис может открыться ПОСРЕДИ проводки окна (ответ сервера приходит
			//   своим кадром), и накопленный в `AccumulateOffset` сдвиг к этому моменту уже есть,
			//   а ветка записи (`!bDown` выше) его больше не увидит — `DragWindowId` сейчас
			//   обнулится. Поэтому позицию пишем здесь, тем же вызовом, что и штатное отпускание (пакет 4: с возвратом в экран).
			ClampDragOnRelease();
			FlushWindowLayout();
			DragWindowId = NAME_None;
			DragHandleIndex = INDEX_NONE;
			return true;
		}
	}

	// ── Ведём начатое перетаскивание: сдвиг на ДЕЛЬТУ курсора (не «курсор − точка захвата»). ──
	if (!DragWindowId.IsNone())
	{
		FAsdaWindowEntry* E = Windows.Find(DragWindowId);
		if (!E || !E->Widget || !E->DragHandles.IsValidIndex(DragHandleIndex))
		{
			DragWindowId = NAME_None; DragHandleIndex = INDEX_NONE;
			return true;
		}
		const FVector2D Delta = Cur - DragPrevMouse;
		DragPrevMouse = Cur;
		if (!Delta.IsNearlyZero())
		{
			// Абсолютные слейт-юниты → локальные юниты канвы окна (DPI-скейл проекта; кривая = 1.0).
			float Scale = 1.f;
			if (const UWorld* W = GetGameInstance() ? GetGameInstance()->GetWorld() : nullptr)
			{
				Scale = UWidgetLayoutLibrary::GetViewportScale(W);
			}
			if (Scale <= KINDA_SMALL_NUMBER) { Scale = 1.f; }
			const FVector2D Local = Delta / Scale;
			const FAsdaJuiDragHandle& H = E->DragHandles[DragHandleIndex];
			ShiftDragTarget(H, *E, Local);
			E->DragOffset += Local;
			bDragMoved = true;   // пакет 4: на отпускании — проверка «не за краем ли»
			// ★★★ УЧЁТ ДЛЯ СОХРАНЕНИЯ. Сдвиг записывается ОКНУ-ЦЕЛИ (у ручки бывает чужая цель),
			//   ключ — контрол-цель. На диск попадёт на отпускании ЛКМ (см. ветку !bDown выше).
			AccumulateOffset(H.TargetWindowId.IsNone() ? DragWindowId : H.TargetWindowId,
				H.TargetControl, Local);
		}
		return true;
	}

	// ── Захват ТОЛЬКО по фронту нажатия (у эталона ветка `plane+1972 & 0x20` тоже требует
	//    предварительного отпускания; иначе окно «прилипало» бы к курсору при проводке мыши). ──
	if (!bPressEdge) { return true; }

	// Порядок разбора — СВЕРХУ ВНИЗ по z: первое окно, поймавшее мышь, «съедает» событие (эталон
	//   `uiWindowMgrDispatchMouse @0x12AF540`), а заодно поднимается наверх, если не AlwaysTop.
	TArray<TPair<int32, FName>> Order;
	Order.Reserve(Windows.Num());
	for (const TPair<FName, FAsdaWindowEntry>& Kv : Windows)
	{
		if (Kv.Value.Widget) { Order.Emplace(Kv.Value.ZOrder, Kv.Key); }
	}
	Order.Sort([](const TPair<int32, FName>& A, const TPair<int32, FName>& B) { return A.Key > B.Key; });

	for (const TPair<int32, FName>& It : Order)
	{
		FAsdaWindowEntry* E = Windows.Find(It.Value);
		if (!E || !E->Widget) { continue; }
		// ★ Модальный гейт: пока модалка открыта, окна ниже полосы модалок мышь не разбирают.
		// ★ 10.10.2026: общая точка гейта мыши модалки; modal_lock2 (МЕЛК-2): с точкой нажатия — книгу 74000 при открытом 74200
		//   нельзя схватить за заголовок `WinMd_CBar` (эталон отдаёт мышь только поддереву 74200).
		if (bModalUp && !PassesModalMouseGateAt(It.Value, *E, Cur)) { continue; }
		const ESlateVisibility WV = E->Widget->GetVisibility();
		if (WV == ESlateVisibility::Collapsed || WV == ESlateVisibility::Hidden) { continue; }
		FName Hit;
		if (!HitTestWidgetTree(E->Widget->GetRootWidget(), E->NoHitWidgets, Cur, Hit, AsdaDrawnBodyFor(It.Value, *E))) { continue; }
		// ★ Подъём наверх — только у ПЕРЕТАСКИВАЕМЫХ окон. Эталон поднимает любое окно на любое
		//   взаимодействие, но у нас z HUD-окон и чата ведут их собственные подсистемы
		//   (PushTopmost, сцепка журнала с чатом) — трогать их порядок мы не имеем права.
		//
		// ★★★ ЖИВОЙ ДЕФЕКТ 2026-08-19 («иногда приходится нажимать по два раза»). Подъём здесь
		//   зовёт `SetWindowZOrder`, а тот СНИМАЕТ окно с вьюпорта и вешает заново. Сделанное
		//   между нажатием и отпусканием, это рвёт путь захвата мыши: `FSlateUser::GetCaptorPath`
		//   получает `Truncated`, зовёт `ReleaseCapture` → `SButton::OnMouseCaptureLost` →
		//   `Release()`, и `OnMouseButtonUp` при методе `DownAndUp` уже не выполняет `ExecuteOnClick`
		//   (полный разбор с адресами — у `SetWindowZOrder`). Клик пропадал БЕЗ СЛЕДА в логе, а
		//   второй проходил, потому что окно уже наверху и перевешивать нечего.
		//   ЛЕЧЕНИЕ: нажатие по ТЕЛУ окна только ЗАПОМИНАЕТ, кого поднять, — подъём выполняется на
		//   отпускании (ветка `!bDown` выше), когда клик уже доставлен.
		//   ★★★ ПОПРАВКА ФИНАЛЬНОЙ ПРИЁМКИ: ЗДЕСЬ СТОЯЛО ЛОЖНОЕ ДОПУЩЕНИЕ
		//   «нажатие по РУЧКЕ поднимает немедленно: под ручкой кликабельных контролов нет и
		//   терять нечего». Контролы под ручками ЕСТЬ, и их МНОГО: скан данных по 126 окнам
		//   и 144 ручкам дал 87 ручек с интерактивным контролом под ними, из них 58 — классические
		//   полосы заголовка с КРЕСТИКОМ (склад 5500/5509, Item_Union_Win 50600/50689,
		//   Disassembly_Win 32000/32001, Free_post_box_WIN 81000, Gear_book_WIN 80300,
		//   SYSTEM_MenuWIN_NEW 69400, чат 2500/2570/2590/2670/2700/2800/2900 и др.), ещё 27 — ручка
		//   на всё окно. То есть дефект, найденный на питомцах, был потенциально у 58 окон.
		//   Что с этим сделано — в блоке ниже (два пункта: подъём и захват).
		// ★★★ КТО ПОД КУРСОРОМ — СЧИТАЕТСЯ ОДИН РАЗ, ДО всего остального: ответ нужен и
		//   захвату перетаскивания (ниже), и решению о подъёме (ещё ниже).
		const bool bCapturingCtrlUnderCursor =
			FindSlateCapturingWidgetUnderCursor(E->Widget->GetRootWidget(), Cur);
		bool bPressedOnHandle = false;
		const bool bAddedOn = CVarAddedDragHandles.GetValueOnGameThread() != 0;
		for (int32 i = 0; i < E->DragHandles.Num(); ++i)
		{
			// Ручка-ДОБАВКА (в оригинале её не было) выключается ключом отката.
			if (E->DragHandles[i].bAddedByPort && !bAddedOn) { continue; }
			UWidget* HW = E->Widget->GetWidgetFromName(E->DragHandles[i].HandleWidget);
			if (!HW) { continue; }
			const ESlateVisibility HV = HW->GetVisibility();
			if (HV == ESlateVisibility::Collapsed || HV == ESlateVisibility::Hidden) { continue; }
			// ★ 26.10.2026 [ИСПРАВЛЕНИЕ] (`asda.UI.DragHandleChainVisible`): скрытый предок — ручка не ловит (эталон
			//   `uiControlHitTest`, ui_core.c:107180-107193); иначе устаревший прямоугольник Slate брал захват.
			if (CVarDragHandleChainVisible.GetValueOnGameThread() != 0 && !AsdaDragHandleChainVisible(HW)) { continue; }
			FVector2D Mn, Mx;
			if (!WidgetAbsRect(HW, Mn, Mx)) { continue; }
			if (Cur.X < Mn.X || Cur.X > Mx.X || Cur.Y < Mn.Y || Cur.Y > Mx.Y) { continue; }
			bPressedOnHandle = true;
			// ★★★ ПОПРАВКА ФИНАЛЬНОЙ ПРИЁМКИ (СРЕДН-3): ОКНО БОЛЬШЕ НЕ ТАЩИТСЯ
			//   ЗА СОБСТВЕННЫЙ КРЕСТИК. ЧТО БЫЛО: `DragWindowId` брался сразу по попаданию в
			//   прямоугольник ручки, и перетаскивание, начатое ровно на крестике, двигало окно.
			//   В ОРИГИНАЛЕ ЭТО НЕВОЗМОЖНО, и вот почему: `uiControlHitTest@0x12DB470`
			//   (ui_core.c:107141-107200) возвращает РОВНО ОДИН контрол и сразу выходит, а событие
			//   перетаскивания поднимается внутри `CControl_OnLButtonDown` (:107295-107296) ТОЛЬКО у
			//   победителя хит-теста. Крестик объявлен ПОЗЖЕ ручки в списке детей ⇒ он выше,
			//   он забирает хит-тест, и перетаскивание НЕ НАЧИНАЕТСЯ вовсе. Конфликта «ручка И
			//   кнопка» у оригинала не бывает физически.
			//   ★★★ ПОЧЕМУ «ПОЗЖЕ ⇒ ВЫШЕ» — ЭТО ДОКАЗАНО, А НЕ ПРЕДПОЛОЖЕНО (поправка ревизии;
			//   прежде здесь стояло голое утверждение). Цепочка замкнута:
			//     1) список детей заполняется `push_back`-ом В ПОРЯДКЕ ОБЪЯВЛЕНИЯ: загрузчик `.JUI`
			//        идёт по XML вперёд (`ui_core.c:105067`), `CControl::AddChild` кладёт в конец
			//        (`ui_core.c:130347-130353`);
			//     2) РЕНДЕР идёт по этому списку ВПЕРЁД (`ui_core.c:108946-108953`) ⇒ объявленный
			//        позже рисуется позже, то есть ПОВЕРХ;
			//     3) ХИТ-ТЕСТ идёт по ТОМУ ЖЕ списку НАЗАД — `rbegin/rend` (`ui_core.c:107155-107156`;
			//        реверсный итератор `stl_runtime.c:92591-92608`, шаг через `_Prev`
			//        `stl_runtime.c:93086-93091`) — и ВОЗВРАЩАЕТСЯ НА ПЕРВОМ ПОПАДАНИИ (`:107161`).
			//   Рендер вперёд + хит-тест назад по одному списку — это и есть z-порядок.
			//   КОНТРОЛЬНАЯ ПАРА НА ДАННЫХ: в `SYSTEM_MenuWIN_NEW` ручка `SYSTEM_MENU_TMOVE`
			//   объявлена ПЕРВОЙ (`Data/Decoded/jui/gamemain_jui.ini:325835`), крестик `SYSTEM_MENU_Xbtn` —
			//   ВТОРЫМ (`:325865`), оба прямые братья с `AlwaysTop=0`. Живой прогон это и показал:
			//   перетаскивание ровно за крестик окно НЕ двигает.
			//   У нас же свой прямоугольный тест ручки идёт ПАРАЛЛЕЛЬНО хит-тесту Slate — срабатывают
			//   ОБА. Приводим к 1:1: если под курсором НАСТОЯЩИЙ контрол, а не одна лишь ручка,
			//   захват НЕ берём. КЛЮЧ ОТКАТА: `asda.UI.DragOverControls 1` — прежнее поведение порта.
			if (bCapturingCtrlUnderCursor && CVarDragOverControls.GetValueOnGameThread() == 0)
			{
				UE_LOG(LogAsdaWindow, Verbose,
					TEXT("[перетаскивание] «%s»: под курсором контрол, а не ручка — захват не берём "
					     "(1:1 с оригиналом; откат asda.UI.DragOverControls 1)"), *It.Value.ToString());
				break;
			}
			DragWindowId = It.Value;
			DragHandleIndex = i;
			DragPrevMouse = Cur;
			bDragMoved = false;   // новый захват — сдвигов ещё не было
			UE_LOG(LogAsdaWindow, Verbose, TEXT("[перетаскивание] захват «%s» ручкой «%s» → цель «%s»"),
				*It.Value.ToString(), *E->DragHandles[i].HandleWidget.ToString(),
				*E->DragHandles[i].TargetControl.ToString());
			break;
		}
		// ★ Волна 1-А: `bRaiseOnBodyClick` — окно без ручек, поднимаемое по телу (ручек нет ⇒ ветка «по ручке» не сработает,
		//   подъём уйдёт в отложенный `PendingRaiseWindowId` на отпускании — клик по его кнопкам не съедается).
		// ★★★ 06.10.2026 (решение владельца): при `asda.UI.RaiseOnBodyClickAll 1` кого поднимать решают правила эталона
		//   (`IsBodyRaiseEligible`), а фокус поля — `ApplyPressFocusRule`; 0 — прежнее условие строкой ниже.
		// Ключ 0: AlwaysTop из меты не поднимался и раньше (`BringWindowToFront` выходил) — с полосой AlwaysTop это держим явно.
		bool bRaiseThis = (E->DragHandles.Num() > 0 || E->bRaiseOnBodyClick) && !E->bAlwaysTop;
		if (bRaiseAll)
		{
			ApplyPressFocusRule(It.Value);
			bRaiseThis = IsBodyRaiseEligible(It.Value);
			if (bRaiseThis && TooltipBlocksRaise())
			{
				UE_LOG(LogAsdaWindow, Verbose, TEXT("[z-порядок] «%s»: показана подсказка — подъёма нет (эталон +0x1B0)"), *It.Value.ToString());
				bRaiseThis = false;
			}
		}
		if (bRaiseThis)
		{
			const bool bLegacy = CVarLegacyRaiseOnPress.GetValueOnGameThread() != 0;
			// ★★★ ПОПРАВКА ФИНАЛЬНОЙ ПРИЁМКИ ВОЛНЫ 8: «ПОД РУЧКОЙ КЛИКАБЕЛЬНЫХ КОНТРОЛОВ НЕТ» —
			//   ЭТО НЕВЕРНО, И ЭТО СТОИЛО ЖИВОГО ДЕФЕКТА. У окна 43400 «крестик» 43439
			//   (`Pet_Inven_Win_X`, живой дамп `43400_Pet_Inven_Win.md:82`, прям (603,274)-(616,287))
			//   ЛЕЖИТ ВНУТРИ ручки `Pet_Inven_Top_Text_68_DRAG` (:81, прям (421,267)-(626,291)).
			//   Нажатие по нему попадало в ветку «по ручке» и поднимало окно НЕМЕДЛЕННО, пока ЛКМ
			//   зажата, — а это ровно тот разрыв захвата мыши, который описан абзацем выше.
			//   ДОКАЗАНО ЖИВЫМ ПРОГОНОМ (`Reports/pet/v8/game_acc_final3.log`):
			//     «[симклик] НАЖАТИЕ в (610,280)» → в ТОМ ЖЕ кадре 17
			//     «[z-порядок] «Pet_Inven_Win»: z 752 → 755 (перевешено в вьюпорте) ★ ЛКМ ЗАЖАТА»
			//     → «ОТПУСКАНИЕ … клик доставлен Slate» → сетка 43400 ОСТАЛАСЬ показанной.
			//   ⚠ ПОЧЕМУ ЭТО НЕ ЛЕЧИТСЯ ЗАЩЁЛКОЙ `FAsdaPressLatch`: 43439 — настоящая `UButton`,
			//   её клик идёт НАПРЯМУЮ через Slate, мимо нашего покадрового опроса. Наша защёлка
			//   такие кнопки не видит вовсе.
			//   ЛЕЧЕНИЕ: немедленный подъём по ручке остаётся (тащить окно из-под чужих надо
			//   сразу), НО только если под курсором НЕТ живой кнопки. Есть кнопка ⇒ подъём
			//   откладывается на отпускание — тем же полем `PendingRaiseWindowId`, что и для тела
			//   окна. Перетаскивание при этом не страдает: `DragWindowId` уже взят выше, и первый
			//   же сдвиг мыши потащит окно; наверх оно встанет на отпускании.
			// ★★★ ЭТО ОТСТУПЛЕНИЕ ОТ ОРИГИНАЛА, ПОМЕЧЕНО (закон `fix-original-defects`).
			//   Оригинал поднимает окно НЕМЕДЛЕННО (ui_core.c:104163-104170), и это ему ничего не
			//   стоит: у него подъём — ПЕРЕСТАНОВКА В СПИСКЕ менеджера, а не снятие и повторное
			//   навешивание виджета. Мы же вынуждены звать `RemoveFromParent`/`AddToViewport`, а это рвёт
			//   `FSlateUser::GetCaptorPath` — потолок движка, не выбор вкуса. Поэтому подъём
			//   ОТКЛАДЫВАЕТСЯ на отпускание — игрок видит разницу в ОДИН кадр.
			//   КЛЮЧ ОТКАТА к дословному поведению: `asda.UI.LegacyRaiseOnPress 1`.
			const bool bButtonUnderCursor = bPressedOnHandle && bCapturingCtrlUnderCursor;
			if ((bPressedOnHandle && !bButtonUnderCursor) || bLegacy)
			{
				const TSharedPtr<SWidget> Foc = bRaiseAll ? FSlateApplication::Get().GetKeyboardFocusedWidget() : nullptr;
				BringWindowToFront(It.Value);
				if (bRaiseAll) { RestoreFocusAfterRaise(Foc); }
			}
			else
			{
				if (bButtonUnderCursor)
				{
					UE_LOG(LogAsdaWindow, Verbose,
						TEXT("[z-порядок] «%s»: нажатие по ручке пришлось на КНОПКУ — подъём отложен ")
						TEXT("до отпускания, иначе перевешивание во вьюпорте съело бы клик"),
						*It.Value.ToString());
				}
				PendingRaiseWindowId = It.Value;
			}
		}
		break;   // окно поймало мышь — ниже не идём
	}
	return true;
}

void UAsdaWindowSubsystem::ShiftDragTarget(const FAsdaJuiDragHandle& Handle, const FAsdaWindowEntry& Owner,
	FVector2D Delta)
{
	// Цель может лежать в ДРУГОМ корневом окне — это данные оригинала (Gear_book_movetext → INVEN_WIN).
	//   Окно-владелец само лежит в реестре под тем же id, под которым нашлась его мета, поэтому
	//   «не найдено» = цель в ДРУГОМ, сейчас закрытом окне ⇒ двигать нечего (эталон: контрол скрыт).
	const FAsdaWindowEntry* TgtEntry = &Owner;
	if (!Handle.TargetWindowId.IsNone())
	{
		const FAsdaWindowEntry* Other = Windows.Find(Handle.TargetWindowId);
		if (!Other) { return; }
		TgtEntry = Other;
	}
	UUserWidget* TW = TgtEntry->Widget;
	if (!TW) { return; }
	if (Handle.bTargetIsWindowRoot)
	{
		// Цель = корневой контрол окна ⇒ едет ВСЁ окно: сдвигаем слоты всех прямых детей RootCanvas.
		if (UPanelWidget* RootCv = Cast<UPanelWidget>(TW->GetRootWidget()))
		{
			for (int32 i = 0; i < RootCv->GetChildrenCount(); ++i)
			{
				ShiftCanvasSlot(RootCv->GetChildAt(i), Delta);
			}
		}
		return;
	}
	// Цель — ВЛОЖЕННОЕ под-окно: едет только её контейнер-группа (со всем поддеревом) или её виджеты.
	for (const FName& N : Handle.TargetWidgets)
	{
		ShiftCanvasSlot(TW->GetWidgetFromName(N), Delta);
	}
}

// ═════════════ ★★★ СИНТЕТИЧЕСКИЙ КЛИК ДЛЯ ЖИВОЙ ПРОВЕРКИ (`asda.UI.SimClick`) ══════════════════
//
// ЗАЧЕМ. Жалобы вида «приходится нажимать по два раза» до сих пор проверялись ТОЛЬКО руками:
//   в headless-PIE мышью не кликнешь, а звать колбэк кнопки напрямую бессмысленно — теряется
//   ровно то, что и ломается (доставка события Slate между нажатием и отпусканием). Здесь
//   инжектируются НАСТОЯЩИЕ события Slate `ProcessMouseButtonDownEvent` / `...UpEvent`, поэтому
//   через них проходит весь путь: захват мыши, `SButton::bIsPressed`, потеря захвата при
//   перевешивании окна, `ExecuteOnClick`.
//
// ★ ЗАДЕРЖКА В КАДРАХ ОБЯЗАТЕЛЬНА. Настоящий человек держит кнопку 5…15 кадров, и всё, что
//   успевает случиться за это время (в т. ч. смена z-порядка), клик и убивает. Клик «нажал и
//   отпустил в одном кадре» этого не ловит.
void UAsdaWindowSubsystem::SimulateClickAt(FVector2D ScreenPos, int32 HoldFrames, bool bRightButton)
{
	bSimClickRight = bRightButton;   // ★ 24.09: ПКМ теперь тоже можно послать (см. шапку объявления)
	SimClickPos = ScreenPos;
	SimClickEndPos = ScreenPos;      // обычный клик = перетаскивание нулевой длины
	bSimClickIsDrag = false;
	SimClickHold = FMath::Clamp(HoldFrames, 0, 600);
	SimClickFramesLeft = SimClickHold;
	bSimClickDown = false;
	bSimClickArmed = true;
	UE_LOG(LogAsdaWindow, Log, TEXT("[симклик] заказан клик в (%.0f,%.0f), удержание %d кадров, кнопка=%s"),
		ScreenPos.X, ScreenPos.Y, SimClickHold, bSimClickRight ? TEXT("ПКМ") : TEXT("ЛКМ"));
}

// ★★★ Синтетическое ПЕРЕТАСКИВАНИЕ (обоснование — в заголовке). Тело общее с кликом: клик это
//   тот же путь с нулевым отрезком, второй копии инъекции событий не заводим.
void UAsdaWindowSubsystem::SimulateDrag(FVector2D FromPos, FVector2D ToPos, int32 HoldFrames, bool bRightButton)
{
	bSimClickRight = bRightButton;
	SimClickPos = FromPos;
	SimClickEndPos = ToPos;
	bSimClickIsDrag = true;
	// ⚠ Кадров удержания должно хватить, чтобы курсор дошёл шагами: при 0 кадров движения не будет
	//   вовсе и порог `FAsdaDragArm` не сработает — заказ выродился бы в обычный клик.
	SimClickHold = FMath::Clamp(HoldFrames, 2, 600);
	SimClickFramesLeft = SimClickHold;
	bSimClickDown = false;
	bSimClickArmed = true;
	UE_LOG(LogAsdaWindow, Log,
		TEXT("[симдраг] заказано перетаскивание (%.0f,%.0f) → (%.0f,%.0f), удержание %d кадров, кнопка=%s"),
		FromPos.X, FromPos.Y, ToPos.X, ToPos.Y, SimClickHold, bSimClickRight ? TEXT("ПКМ") : TEXT("ЛКМ"));
}

#if !UE_BUILD_SHIPPING   // ★ 31.10.2026: журнал синтетического клика — дев-диагностика, в Shipping не входит
// ★ 31.10.2026 (разбор потерянного «Готово» окна 540 в пакетном прогоне самотестов): что Slate видит под точкой синтетического
//   клика — три нижних звена пути хит-теста (UMG-имя, если есть). Без этой строки «клик доставлен» не говорит, КОМУ. Вместе с
//   состоянием кнопки (ниже) она и показала, что Slate клик выполнял, а мёртвым было реле `OnClicked` (GC-якорь, `PopupRelays`).
static FString AsdaSimClickWhatUnder(FSlateApplication& App, const FVector2D& Pos)
{
	const FWidgetPath Path = App.LocateWindowUnderMouse(Pos, App.GetInteractiveTopLevelWindows());
	if (!Path.IsValid()) { return TEXT("—"); }
	FString S;
	for (int32 i = Path.Widgets.Num() - 1, n = 0; i >= 0 && n < 3; --i, ++n)
	{
		S += (n ? TEXT(" < ") : TEXT("")) + FReflectionMetaData::GetWidgetDebugInfo(&Path.Widgets[i].Widget.Get());
	}
	return S;
}

// ★ 31.10.2026: состояние кнопки Slate под точкой (наведение/нажата/захват мыши) — те три признака, по которым `SButton` решает,
//   будет ли клик (SButton.cpp:424). Пусто — под точкой не кнопка.
static FString AsdaSimClickButtonState(FSlateApplication& App, const FVector2D& Pos)
{
	const FWidgetPath Path = App.LocateWindowUnderMouse(Pos, App.GetInteractiveTopLevelWindows());
	for (int32 i = Path.IsValid() ? Path.Widgets.Num() - 1 : -1; i >= 0; --i)
	{
		const TSharedRef<SWidget> W = Path.Widgets[i].Widget;
		if (W->GetType() != FName(TEXT("SButton"))) { continue; }
		const TSharedRef<SButton> B = StaticCastSharedRef<SButton>(W);
		return FString::Printf(TEXT("кнопка: наведение=%d нажата=%d захват=%d"), B->IsHovered() ? 1 : 0, B->IsPressed() ? 1 : 0,
			B->HasMouseCapture() ? 1 : 0);
	}
	return FString();
}
#else
// Shipping: диагностики под курсором нет — строка журнала клика без хвоста.
static FString AsdaSimClickWhatUnder(FSlateApplication&, const FVector2D&) { return TEXT("—"); }
static FString AsdaSimClickButtonState(FSlateApplication&, const FVector2D&) { return FString(); }
#endif // !UE_BUILD_SHIPPING

void UAsdaWindowSubsystem::TickSimulatedClick()
{
	if (!bSimClickArmed) { return; }
	FSlateApplication& App = FSlateApplication::Get();
	// ★ 24.09: одна и та же инъекция шлёт ту кнопку, которую заказали. Второй копии пути нет —
	//   иначе правый клик проверялся бы не тем кодом, что левый (закон общей точки).
	const FKey SimKey = bSimClickRight ? EKeys::RightMouseButton : EKeys::LeftMouseButton;
	if (!bSimClickDown)
	{
		// ★ Курсор двигаем ЯВНО: и `AsdaUi::IsCursorOverWidget`, и хит-тест окон читают
		//   `GetCursorPos()`, а не координату из события. Печатаем, что реально получилось —
		//   в headless платформенный курсор может не сдвинуться, и без этой строки прогон
		//   выглядел бы как «клик не сработал» по совсем другой причине.
		App.SetCursorPos(SimClickPos);
		const FVector2D Real = App.GetCursorPos();
		TSet<FKey> Pressed;
		Pressed.Add(SimKey);
		const FPointerEvent Ev(0, SimClickPos, SimClickPos, Pressed, SimKey, 0.f, FModifierKeysState());
		const FString Under = AsdaSimClickWhatUnder(App, SimClickPos);
		const bool bHandled = App.ProcessMouseButtonDownEvent(nullptr, Ev);
		bSimClickDown = true;
		UE_LOG(LogAsdaWindow, Log, TEXT("[симклик] НАЖАТИЕ %s в (%.0f,%.0f); курсор Slate встал в (%.0f,%.0f)%s; обработано=%d, под курсором: %s"),
			bSimClickRight ? TEXT("ПКМ") : TEXT("ЛКМ"), SimClickPos.X, SimClickPos.Y, Real.X, Real.Y,
			Real.Equals(SimClickPos, 1.f) ? TEXT("") : TEXT("  ★ КУРСОР НЕ ВСТАЛ — хит-тесты по курсору проверить нельзя"),
			bHandled ? 1 : 0, *Under);
		return;   // отпускание — не раньше следующего кадра
	}
	if (SimClickFramesLeft > 0)
	{
		--SimClickFramesLeft;
		// ★★★ ВЕДЁМ КУРСОР ПО КАДРАМ. Без настоящего движения не сработает ни порог `FAsdaDragArm`
		//   (он меряет пройденные пиксели), ни хит-тесты «что под курсором» — они читают
		//   `GetCursorPos()`. Для обычного клика отрезок нулевой, и ветка ничего не меняет.
		if (bSimClickIsDrag && SimClickHold > 0)
		{
			const float A = 1.f - (float)SimClickFramesLeft / (float)SimClickHold;   // 0 → 1
			const FVector2D P = FMath::Lerp(SimClickPos, SimClickEndPos, A);
			App.SetCursorPos(P);
			TSet<FKey> Pressed;
			Pressed.Add(SimKey);
			const FPointerEvent Mv(0, P, P, Pressed, EKeys::Invalid, 0.f, FModifierKeysState());
			App.ProcessMouseMoveEvent(Mv);
		}
		return;
	}
	const FVector2D UpPos = bSimClickIsDrag ? SimClickEndPos : SimClickPos;
	if (bSimClickIsDrag) { App.SetCursorPos(UpPos); }
	const TSet<FKey> None;
	const FPointerEvent Ev(0, UpPos, UpPos, None, SimKey, 0.f, FModifierKeysState());
	const FString UnderUp = AsdaSimClickWhatUnder(App, UpPos);
	const FString BtnBefore = AsdaSimClickButtonState(App, UpPos);
	const bool bUpHandled = App.ProcessMouseButtonUpEvent(Ev);
	const bool bWasRight = bSimClickRight;
	bSimClickDown = false;
	bSimClickArmed = false;
	bSimClickIsDrag = false;
	bSimClickRight = false;   // следующий заказ по умолчанию снова левый
	UE_LOG(LogAsdaWindow, Log, TEXT("[симклик] ОТПУСКАНИЕ %s в (%.0f,%.0f) — клик доставлен Slate; обработано=%d, под курсором: %s%s%s"),
		bWasRight ? TEXT("ПКМ") : TEXT("ЛКМ"), UpPos.X, UpPos.Y, bUpHandled ? 1 : 0, *UnderUp,
		BtnBefore.IsEmpty() ? TEXT("") : TEXT("; до отпускания "), *BtnBefore);
}
