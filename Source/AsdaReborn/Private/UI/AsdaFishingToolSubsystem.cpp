// Copyright (c) 2026 @sitxovski (Discord)
// UAsdaFishingToolSubsystem — реализация окна «Взять рыболовную снасть» (JUI 15900).
//   Полное обоснование каждого числа — в шапке `AsdaFishingToolSubsystem.h` и в отчёте
//   `Reports/fishing_window_port.md`. Здесь комментируются только места реализации.
#include "UI/AsdaFishingToolSubsystem.h"
#include "AsdaPerf.h"   // ★ R4-c (14.11.2026): области замера времени игрового потока
#include "UI/AsdaEnterGuard.h"   // ★ 25.10.2026: вопросы этого файла Enter не подтверждает (только щелчок)
#include "AsdaDevConsole.h"   // ★ античит А2: ASDA_DEV_CVAR / ASDA_DEV_CONSOLE — дев-ключи и команды вне Shipping
#include "UI/AsdaNotice.h"   // ★ волна 1: фасад нотисов (Loc / Chat / MsgBox)
#include "UI/AsdaLocIds.h"   // Дд2/Дд3: нотисы кнопок ОБЩИХ окон (ONEBUT/TWOBUT/SHOUTING) — одно место
#include "AsdaBytes.h"   // D4: единые читалки сырой записи (little-endian, DataHex, ASCII-поле)

#include "UI/AsdaInventorySubsystem.h"
#include "UI/AsdaWindowSubsystem.h"
#include "UI/AsdaItemTooltipSubsystem.h"
#include "UI/AsdaTipPanel.h"
#include "UI/AsdaMessageSubsystem.h"
#include "UI/AsdaAnimatedPlane.h"
#include "UI/AsdaUiHelpers.h"
#include "Login/AsdaButtonRelay.h"
#include "Items/AsdaItemTableSubsystem.h"
#include "Resources/AsdaTextureSubsystem.h"
#include "Resources/AsdaSoundSubsystem.h"
#include "Text/AsdaTextSubsystem.h"
#include "AsdaNetSubsystem.h"

#include "Blueprint/UserWidget.h"
#include "Blueprint/WidgetTree.h"
#include "Components/CanvasPanelSlot.h"
#include "Components/PanelWidget.h"
#include "Components/Image.h"
#include "Components/Button.h"
#include "Components/TextBlock.h"
#include "Engine/Engine.h"
#include "Engine/Texture2D.h"
#include "Framework/Application/SlateApplication.h"
#include "HAL/IConsoleManager.h"
#include "AsdaOutgoingGate.h"               // ★ С-Р1: общий офлайн-гейт отладочных команд (RefuseDebugWhenOnline)

DEFINE_LOG_CATEGORY_STATIC(LogAsdaFishrod, Log, All);

// Имя зоны интерфейса и id окна для реестра (своего WBP у окна нет — оно внутри WBP_INVEN_WIN).
static const FName kFishrodZoneId(TEXT("INVEN_FISHROD_WIN"));
static const FName kInvenWindowId(TEXT("INVEN_WIN"));

// ── Нотисы эталона (все проверены в DT_GameText) ──
static constexpr int32 kLocTitle        = 111080;  // «Взять рыболовную снасть» (заголовок 15901)
static constexpr int32 kLocPickRod      = 111051;  // «Используется [%s]. Дважды щелкни нужную удочку…»
static constexpr int32 kLocDone         = 111027;  // «Запрос выполнен.»
static constexpr int32 kLocLineSimple   = 111026;  // леска, у удочки такой снасти ещё нет
static constexpr int32 kLocLineReplace  = 111028;  // леска, снасть уже стоит (будет заменена)
static constexpr int32 kLocHookSimple   = 111029;  // крючок, у удочки такой снасти ещё нет
static constexpr int32 kLocHookReplace  = 111030;  // крючок, снасть уже стоит (будет заменена)
// ★ Дд2 (аудит `code_dedup_audit_2.md`): переименованы ПО СМЫСЛУ. Раньше назывались
//   `kLocBtnOk`/`kLocBtnCancel` — ровно как одноимённые константы тултипа, у которых ДРУГИЕ
//   значения (2288/2378, кнопки 1532/1533 лотка самоцветов). Одно имя ⇒ два разных нотиса.
//   Эти два — подписи кнопок СОБСТВЕННОГО подтверждения окна снасти (`case 'N'` эталона).
static constexpr int32 kLocFishConfirmOk     = 108510;  // «OK»     (эталон `case 'N'`)
static constexpr int32 kLocFishConfirmCancel = 108511;  // «Отмена» (эталон `case 'N'`)

// ★ Ф1: ПКМ-отмена, обещанная нотисом 111051, но в бинаре не написанная. 1 = вернуть как в эталоне.
static ASDA_DEV_CVAR(int32, CVarLegacyNoRmbCancel,
	TEXT("asda.Fishrod.LegacyNoRmbCancel"), 0,
	TEXT("Окно снасти 15900: 1 = как в эталоне (правая кнопка НИЧЕГО не отменяет, хотя нотис 111051 ")
	TEXT("это обещает); 0 (по умолчанию) = ПКМ снимает взвод и закрывает окно."),
	ECVF_Default);

// ── Чтение полей 57-байтовой записи ──
// ★ D4 (аудит `code_dedup_audit_2.md`): тела переехали в `AsdaBytes` (Public/AsdaBytes.h).
static FORCEINLINE int32 FrRdI16(const uint8* P) { return AsdaBytes::RdI16(P); }
static FORCEINLINE int32 FrRdI32(const uint8* P) { return AsdaBytes::RdI32(P); }

// ★ D5 (аудит `code_dedup_audit_2.md`): своя `FrParsePlaneName` убрана — четыре копии формулы
//   «два числовых хвоста с конца» сведены в `AsdaUi::ParsePlaneName` (закон
//   `jui-importer-widget-naming` теперь живёт в одном месте). Прежний комментарий здесь честно
//   признавал дубль («формула та же, что у ParseInvenPlaneName») — дубля больше нет.

// ═══════════════════════════════ ЖИЗНЕННЫЙ ЦИКЛ ════════════════════════════════════════════════

void UAsdaFishingToolSubsystem::Initialize(FSubsystemCollectionBase& Collection)
{
	Super::Initialize(Collection);
	for (int32 i = 0; i < kCellCount; ++i) { ListToBagCell[i] = -1; }
	Cells.SetNum(kCellCount);
	UE_LOG(LogAsdaFishrod, Log, TEXT("[снасть] подсистема окна 15900 поднята"));
}

void UAsdaFishingToolSubsystem::Deinitialize()
{
	UnregisterZone();
	if (UGameInstance* GI = GetGameInstance())
	{
		if (UAsdaNetSubsystem* Net = GI->GetSubsystem<UAsdaNetSubsystem>())
		{
			Net->OnFishingToolResult.RemoveAll(this);
		}
	}
	ResetWidgets();
	Super::Deinitialize();
}

// ★★★ К-3: СБРОС СОСТОЯНИЯ СЕССИИ (шапка `AsdaSessionReset.h`). Подсистема живёт на
//   `GameInstance` и переживает разрыв связи, выход в выбор персонажа и травел; всё, что
//   ниже, — данные и защёлки ОДНОЙ сессии. Отправок здесь нет (закон `no-reentrant-net-callbacks`).
void UAsdaFishingToolSubsystem::ResetSessionState(EAsdaSessionResetReason Reason)
{
	ResetWidgets();
	bWindowOpen = false;
	bPendingArmed = false;            // «материал взведён»
	PendingCell = -1;
	RodCell = -1;
	bAwaitingResult = false;
	SentAtS = 0.0;
	PendingResults.Clear();
	PressedIndex = -1; HoverIndex = -1;
	bTipShown = false;
	bCloseHoverSoundPlayed = false;
	Mouse.Reset();
	(void)Reason;
}

void UAsdaFishingToolSubsystem::Tick(float /*DeltaTime*/)
{
	ASDA_PERF_SCOPE("Tick.FishingTool");   // ★ R4-c (14.11.2026): область замера (AsdaPerf.h; в Shipping пусто)
	UGameInstance* GI = GetGameInstance();
	if (!GI) { return; }

	// Подписка на 6166 — лениво (сеть поднимается позже подсистем UI).
	if (!bNetBound)
	{
		if (UAsdaNetSubsystem* Net = GI->GetSubsystem<UAsdaNetSubsystem>())
		{
			Net->OnFishingToolResult.AddUObject(this, &UAsdaFishingToolSubsystem::OnFishingToolResult);
			bNetBound = true;
			UE_LOG(LogAsdaFishrod, Log, TEXT("[снасть] подписка на 6166 установлена"));
		}
	}

	// Очередь ответов сервера — применяем ЗДЕСЬ (закон `no-reentrant-net-callbacks`).
	//   ★ Аудит №3 (B10): «под замком забрать всё разом и очистить» — общая точка.
	TArray<TArray<uint8>> Ready;
	PendingResults.Drain(Ready);
	for (const TArray<uint8>& B : Ready) { ApplyFishingToolResult(B); }

	// Предохранитель по таймауту (в эталоне флага под 6165 нет вовсе).
	if (bAwaitingResult && FPlatformTime::Seconds() - SentAtS > (double)ResultTimeoutS)
	{
		bAwaitingResult = false;
		UE_LOG(LogAsdaFishrod, Warning, TEXT("[снасть] ответ 6166 не пришёл за %.1f с — снимаем ожидание"), ResultTimeoutS);
	}

	if (!bWindowOpen) { return; }
	if (!EnsureWidgets()) { return; }
	TickPointer();
}

UAsdaInventorySubsystem* UAsdaFishingToolSubsystem::Inv() const
{
	UGameInstance* GI = GetGameInstance();
	return GI ? GI->GetSubsystem<UAsdaInventorySubsystem>() : nullptr;
}

// ═══════════════════════════════ ВИДЖЕТЫ ════════════════════════════════════════════════════════

void UAsdaFishingToolSubsystem::ResetWidgets()
{
	HostWidget = nullptr;
	WinGroup = nullptr; BaseGroup = nullptr; TitleGroup = nullptr; TitleBarGroup = nullptr;
	TitleText = nullptr; CloseBtn = nullptr; SelCursor = nullptr;
	Cells.Reset(); Cells.SetNum(kCellCount);
	Relays.Reset();
	bControlsReady = false;
	HoverIndex = -1; PressedIndex = -1; bTipShown = false; bCloseHoverSoundPlayed = false;
}

bool UAsdaFishingToolSubsystem::EnsureWidgets()
{
	UAsdaInventorySubsystem* I = Inv();
	UUserWidget* Host = I ? I->GetWindowWidget() : nullptr;
	if (!Host) { if (HostWidget) { UnregisterZone(); ResetWidgets(); } return false; }
	if (Host == HostWidget && bControlsReady) { return true; }
	UnregisterZone();
	ResetWidgets();
	HostWidget = Host;
	ScanWidgets();
	if (!bControlsReady) { return false; }
	RegisterZone();
	ApplyVisibility();
	// Виджеты пересобрались (смена мира / переоткрытие инвентаря), а окно было открыто — восстановить список.
	if (bWindowOpen) { RefreshList(); }
	return true;
}

// ★★★ ПОЛНЫЙ СТРУКТУРНЫЙ РАЗБОР (закон `full-structural-analysis-always`): 71 узел поддерева
//   `Inven_Fishrod_Win` уже создан импортёром внутри `WBP_INVEN_WIN` — перегенерация НЕ нужна.
//   Имена: группа = `{idc}_grp`, план = `{idc}_{juiIdx}_{plane}`, интерактив/Text = ТОЧНОЕ idc.
//   Цвет/выравнивание/обводку/кадры кнопок импортёр уже взял из данных (`ui-text-color-exs`,
//   `ui-button-hover-click-rule`) — рантайм их НЕ переопределяет.
void UAsdaFishingToolSubsystem::ScanWidgets()
{
	if (!HostWidget || !HostWidget->WidgetTree) { return; }
	TArray<UWidget*> All;
	HostWidget->WidgetTree->GetAllWidgets(All);

	int32 IconsFound = 0, TextsFound = 0;
	for (UWidget* W : All)
	{
		if (!W) { continue; }
		const FString N = W->GetName();
		if (!N.StartsWith(TEXT("Inven_Fishrod_"))) { continue; }

		// ── интерактив и Text: точное имя idc ──
		if (UButton* Btn = Cast<UButton>(W))
		{
			if (N == TEXT("Inven_Fishrod_CloseBtn")) { CloseBtn = Btn; }
			continue;
		}
		if (UTextBlock* Tb = Cast<UTextBlock>(W))
		{
			Tb->SetAutoWrapText(false);   // закон `ui-autowrap-canvas-gotcha`
			if (N == TEXT("Inven_Fishrod_TitleTxt")) { TitleText = Tb; continue; }
			// Счётчик ячейки: `Inven_Fishrod_ItemNN` (в этом окне эталон пишет туда пустую строку).
			if (N.StartsWith(TEXT("Inven_Fishrod_Item")) && N.Len() == 20)
			{
				const FString Num = N.Mid(18, 2);
				if (FChar::IsDigit(Num[0]) && FChar::IsDigit(Num[1]))
				{
					const int32 Idx = FCString::Atoi(*Num);
					if (Cells.IsValidIndex(Idx)) { Cells[Idx].Text = Tb; Tb->SetText(FText::GetEmpty()); ++TextsFound; }
				}
			}
			continue;
		}

		// ── группы ──
		if (N == TEXT("Inven_Fishrod_Win_grp"))         { WinGroup = W;      continue; }
		if (N == TEXT("Inven_Fishrod_Base_grp"))        { BaseGroup = W;     continue; }
		if (N == TEXT("Inven_Fishrod_TitleTxt_grp"))    { TitleGroup = W;    continue; }
		if (N == TEXT("Inven_Fishrod_TtlBar_grp"))      { TitleBarGroup = W; continue; }

		// ── планы ──
		FString Idc; int32 JuiIdx = 0, PlaneIdx = 0;
		if (!AsdaUi::ParsePlaneName(N, Idc, JuiIdx, PlaneIdx)) { continue; }
		if (Idc == TEXT("Inven_Fishrod_Select"))
		{
			SelCursor = Cast<UAsdaAnimatedPlane>(W);   // Slot_AniSelect — 4 кадра ne_iconselected
			continue;
		}
		// Иконка ячейки: idc = `Inven_Fishrod_ItemNN` (у `Inven_Fishrod_ItemBase` цифр нет — не пройдёт).
		if (Idc.StartsWith(TEXT("Inven_Fishrod_Item")) && Idc.Len() == 20)
		{
			const FString Num = Idc.Mid(18, 2);
			if (FChar::IsDigit(Num[0]) && FChar::IsDigit(Num[1]))
			{
				const int32 Idx = FCString::Atoi(*Num);
				if (Cells.IsValidIndex(Idx)) { Cells[Idx].Icon = Cast<UImage>(W); ++IconsFound; }
			}
		}
	}

	// Заголовок 15901: текст = нотис 111080, стиль `CClass_SetField926(0x200000)` = ПСЕВДО-ЖИРНЫЙ
	//   (второй проход глифа ±1 px, НЕ Bold-гарнитура — ★ 2026-09-24, `AsdaUi::ApplyPseudoBold`).
	if (TitleText)
	{
		const FString T = LocRaw(kLocTitle);
		if (!T.IsEmpty()) { TitleText->SetText(FText::FromString(T)); }
		AsdaUi::ApplyPseudoBold(TitleText);
	}

	// Крестик 15902: клик = `sub_928A8C` (спрятать 15900) + звук №5 ветки 66561.
	if (CloseBtn)
	{
		UAsdaButtonRelay* R = NewObject<UAsdaButtonRelay>(this);
		R->Index = kCloseIdNum;
		R->Callback = [this](int32) { OnCloseClicked(); };
		R->HoverCallback = [this]() { OnCloseHovered(); };
		CloseBtn->OnClicked.Clear();
		CloseBtn->OnHovered.Clear();
		CloseBtn->OnClicked.AddDynamic(R, &UAsdaButtonRelay::OnClicked);
		CloseBtn->OnHovered.AddDynamic(R, &UAsdaButtonRelay::OnHovered);
		AsdaUi::AnchorRelay(Relays, R, CloseBtn);   // ★ 31.10.2026: якорь чистит мёртвые/перепривязанные (`asda.UI.RelayAnchorPrune`)
	}

	bControlsReady = (WinGroup != nullptr) && (IconsFound == kCellCount);
	UE_LOG(LogAsdaFishrod, Log,
		TEXT("[снасть] разбор окна 15900: группа=%s подложка=%s заголовок=%s крестик=%s курсор=%s иконок=%d текстов=%d → %s"),
		WinGroup ? TEXT("да") : TEXT("НЕТ"), BaseGroup ? TEXT("да") : TEXT("НЕТ"),
		TitleText ? TEXT("да") : TEXT("НЕТ"), CloseBtn ? TEXT("да") : TEXT("НЕТ"),
		SelCursor ? TEXT("да") : TEXT("НЕТ"), IconsFound, TextsFound,
		bControlsReady ? TEXT("ГОТОВО") : TEXT("НЕПОЛНО"));
}

void UAsdaFishingToolSubsystem::SetSubtreeVisible(UWidget* Root, bool bVisible)
{
	if (!Root) { return; }
	Root->SetVisibility(bVisible ? ESlateVisibility::SelfHitTestInvisible : ESlateVisibility::Collapsed);
	if (UPanelWidget* P = Cast<UPanelWidget>(Root))
	{
		const int32 N = P->GetChildrenCount();
		for (int32 i = 0; i < N; ++i) { SetSubtreeVisible(P->GetChildAt(i), bVisible); }
	}
}

void UAsdaFishingToolSubsystem::ApplyVisibility()
{
	if (!WinGroup) { return; }
	if (bWindowOpen)
	{
		// Разворачиваем ВСЁ поддерево (см. комментарий у SetSubtreeVisible), затем `RefreshList`
		//   вернёт `Collapsed` курсору 15990 и пустым ячейкам — как в эталоне.
		SetSubtreeVisible(WinGroup, true);
	}
	else
	{
		// Свернуть достаточно корневой группы: детей мы всё равно развернём заново при показе.
		WinGroup->SetVisibility(ESlateVisibility::Collapsed);
	}
}

// Прямоугольник окна для реестра зон: ТОЛЬКО узлы корпуса (закон: «объединение всех узлов»
//   раздувает зону — уже ловили на чате).
bool UAsdaFishingToolSubsystem::GetWindowRect(FVector2D& OutMin, FVector2D& OutMax) const
{
	if (!bWindowOpen || !WinGroup) { return false; }
	bool bAny = false;
	FVector2D Mn(FLT_MAX, FLT_MAX), Mx(-FLT_MAX, -FLT_MAX);
	auto Acc = [&](const UWidget* W)
	{
		if (!W || W->GetVisibility() == ESlateVisibility::Collapsed) { return; }
		// ★ Аудит №4 (A9/B1): живой абсолютный прямоугольник — общая точка `AsdaUi::WidgetAbsRect`
		//   (она же отсеивает нулевую геометрию). Накопитель min/max остаётся здесь.
		FVector2D P, PMax;
		if (!AsdaUi::WidgetAbsRect(W, P, PMax)) { return; }
		Mn = FVector2D(FMath::Min(Mn.X, P.X), FMath::Min(Mn.Y, P.Y));
		Mx = FVector2D(FMath::Max(Mx.X, PMax.X), FMath::Max(Mx.Y, PMax.Y));
		bAny = true;
	};
	Acc(BaseGroup);       // Box_Black 199×165 — тело окна
	Acc(TitleBarGroup);   // WinSm_CBar/LBar/RBar 203×24 — шапка
	Acc(TitleGroup);      // ручка перетаскивания + крестик
	if (!bAny) { return false; }
	OutMin = Mn; OutMax = Mx;
	return true;
}

void UAsdaFishingToolSubsystem::RegisterZone()
{
	UGameInstance* GI = GetGameInstance();
	UAsdaWindowSubsystem* WM = GI ? GI->GetSubsystem<UAsdaWindowSubsystem>() : nullptr;
	if (!WM) { return; }
	// Зона ОТДЕЛЬНАЯ от инвентарной: окно 15900 стоит по .JUI в (352,296) и в свёрнутом инвентаре
	//   (ширина 228) целиком выходит за его корпус — общая зона инвентаря его не накрывает.
	WM->RegisterUiZone(kFishrodZoneId, FAsdaUiZoneRect::CreateWeakLambda(this,
		[this](FVector2D& Mn, FVector2D& Mx) { return GetWindowRect(Mn, Mx); }));
}

void UAsdaFishingToolSubsystem::UnregisterZone()
{
	UGameInstance* GI = GetGameInstance();
	if (UAsdaWindowSubsystem* WM = GI ? GI->GetSubsystem<UAsdaWindowSubsystem>() : nullptr)
	{
		WM->UnregisterUiZone(kFishrodZoneId);
	}
}

// ═══════════════════════════════ ОТКРЫТИЕ / ЗАКРЫТИЕ ═══════════════════════════════════════════

// Эталон `uiInventoryClearSelection@0xD34B10`: сброс выбора, показ 15900, перерисовка списка.
void UAsdaFishingToolSubsystem::ShowWindow()
{
	SelIndex = -1;
	bWindowOpen = true;
	if (!EnsureWidgets())
	{
		UE_LOG(LogAsdaFishrod, Warning, TEXT("[снасть] окно 15900: виджеты не разобраны (инвентарь закрыт?)"));
		return;
	}
	ApplyVisibility();
	RefreshList();
	// Дочернее окно всегда над владельцем (эталон `uiShowChildWindow`: `CClass_MapInsert`).
	if (UGameInstance* GI = GetGameInstance())
	{
		if (UAsdaWindowSubsystem* WM = GI->GetSubsystem<UAsdaWindowSubsystem>()) { WM->BringWindowToFront(kInvenWindowId); }
	}
	UE_LOG(LogAsdaFishrod, Log, TEXT("[снасть] окно 15900 открыто"));
}

void UAsdaFishingToolSubsystem::CloseWindow()
{
	if (!bWindowOpen) { return; }
	bWindowOpen = false;
	HideCellTip();
	if (SelCursor) { SelCursor->SetVisibility(ESlateVisibility::Collapsed); }
	ApplyVisibility();
	UE_LOG(LogAsdaFishrod, Log, TEXT("[снасть] окно 15900 закрыто (крестик 15902 / sub_D34B60)"));
}

// Эталон `sub_FC9DF0`: `*(u8*)(this+16) = 0; this[3] = 0;` (+ по нашему Ф1 — гасим окно).
void UAsdaFishingToolSubsystem::CancelPending(bool bAlsoHide)
{
	if (bPendingArmed)
	{
		UE_LOG(LogAsdaFishrod, Log, TEXT("[снасть] взвод снят (материал контейнер=%d ячейка=%d)"),
			PendingContainer, PendingCell);
	}
	bPendingArmed = false;
	PendingContainer = 0; PendingCell = -1;
	if (bAlsoHide) { CloseWindow(); }
}

void UAsdaFishingToolSubsystem::OnInventoryOpened()
{
	// Эталон `uiOpenInventoryWindow`: сначала `sub_928A8C` (скрыть 15900), потом `sub_9241EE` (снять взвод).
	CloseWindow();
	CancelPending(/*bAlsoHide*/ false);
}

void UAsdaFishingToolSubsystem::OnCloseClicked()
{
	// ★ Волна 9 заточки: звук id 5 кнопок процедуры инвентаря — ОБЩАЯ точка `AsdaUi::PlayInvenButtonSfx`.
	//   Параметр редактора `CloseSoundId` сохранён: 5 (умолчание) → общая точка, иное → как было.
	if (CloseSoundId == AsdaUi::kInvenButtonSoundId) { AsdaUi::PlayInvenButtonSfx(GetGameInstance()); }
	else { PlayUiSound(CloseSoundId); }
	CloseWindow();
}

void UAsdaFishingToolSubsystem::OnCloseHovered()
{
	// Кадр наведения кнопки ставит сам импортёр (скин `CloseBtn_WinSm`); здесь только защита от
	//   повторного лога. Отдельного звука наведения у эталонных кнопок НЕТ.
	bCloseHoverSoundPlayed = true;
}

// Эталон `sub_FC9C80`: запомнить материал и взвести флаг.
void UAsdaFishingToolSubsystem::ArmPending(int32 ContainerId, int32 CellIndex)
{
	bPendingArmed = true;
	PendingContainer = ContainerId;
	PendingCell = CellIndex;
	RodContainer = 0; RodCell = -1;
}

// ═══════════════════════════════ ТОЧКА ВХОДА ИЗ ИНВЕНТАРЯ ══════════════════════════════════════

bool UAsdaFishingToolSubsystem::OnInventoryDoubleClick(int32 ContainerId, int32 CellIndex)
{
	UAsdaInventorySubsystem* I = Inv();
	UGameInstance* GI = GetGameInstance();
	UAsdaItemTableSubsystem* IT = GI ? GI->GetSubsystem<UAsdaItemTableSubsystem>() : nullptr;
	if (!I || !IT) { return false; }
	const FAsdaItemInstance* Cell = I->GetCell(ContainerId, CellIndex);
	if (!Cell || Cell->IsEmpty()) { return false; }
	const FAsdaItemRecord* Rec = IT->GetItem(Cell->ItemCode);
	if (!Rec) { return false; }

	// ── ВЕТКА 1 (эталон item_inventory.c:37864-37872): материал 208/209 в контейнере 2 ──
	if (Rec->ItemSubType == kLineSubType || Rec->ItemSubType == kHookSubType)
	{
		ArmPending(ContainerId, CellIndex);
		// Нотис 111051 с именем материала → однокнопочный msgbox (`CItem_BuildTooltipText(…, 0, −1, …, 1)`).
		const FString Name = IT->GetItemName(Cell->ItemCode);
		Msg1Button(FormatPrintf(LocRaw(kLocPickRod), { Name }));
		ShowWindow();
		UE_LOG(LogAsdaFishrod, Log, TEXT("[снасть] взвод: %s (код %d, подтип %d) контейнер=%d ячейка=%d"),
			*Name, Cell->ItemCode, Rec->ItemSubType, ContainerId, CellIndex);
		return true;
	}

	if (!bPendingArmed) { return false; }

	// ── ВЕТКА 2 (эталон item_inventory.c:38118): удочка при взведённом ожидании — ПОДТВЕРДИТЬ ──
	if (Rec->ItemSubType == kRodSubType && Rec->ItemVariant == kRodVariant)
	{
		// Эталон зовёт `itemConfirmPendingAction(g_InventoryUI, v4, pSlotItem)` и ВОЗВРАЩАЕТ 0 —
		//   т.е. обычное «использовать/надеть» для этого клика уже не выполняется.
		RodContainer = ContainerId; RodCell = CellIndex;
		ConfirmSelectedPair();
		return true;
	}

	// ── ВЕТКА 3 (эталон item_inventory.c:38138 `sub_9241EE`): любой другой предмет снимает взвод,
	//    но обычное действие по нему ВЫПОЛНЯЕТСЯ (эталон идёт дальше по функции) ──
	CancelPending(/*bAlsoHide*/ true);
	return false;
}

// ═══════════════════════════════ СПИСОК УДОЧЕК ═════════════════════════════════════════════════

// Эталон `uiInventoryRefreshFilteredGrid@0xD34D30`.
void UAsdaFishingToolSubsystem::RefreshList()
{
	UAsdaInventorySubsystem* I = Inv();
	UGameInstance* GI = GetGameInstance();
	UAsdaItemTableSubsystem* IT = GI ? GI->GetSubsystem<UAsdaItemTableSubsystem>() : nullptr;
	UAsdaTextureSubsystem* TexMgr = GEngine ? GEngine->GetEngineSubsystem<UAsdaTextureSubsystem>() : nullptr;
	if (!bControlsReady) { return; }

	// 1) Полная очистка 30 ячеек (эталон: пустая иконка слота 32×32 + `SetText("")` + `self[..669] = 0`).
	for (int32 i = 0; i < kCellCount; ++i)
	{
		ListToBagCell[i] = -1;
		if (UImage* Img = Cells[i].Icon)
		{
			FSlateBrush B = Img->GetBrush();
			B.SetResourceObject(nullptr);
			B.DrawAs = ESlateBrushDrawType::NoDrawType;
			Img->SetBrush(B);
			Img->SetVisibility(ESlateVisibility::Collapsed);
		}
		if (Cells[i].Text) { Cells[i].Text->SetText(FText::GetEmpty()); Cells[i].Text->SetVisibility(ESlateVisibility::Collapsed); }
	}
	// 2) Курсор выбора прячется всегда, показывается только если выбранная позиция ещё существует.
	if (SelCursor) { SelCursor->SetVisibility(ESlateVisibility::Collapsed); }
	if (!I || !IT) { return; }

	// 3) Перебор ВСЕГО контейнера 1 подряд; в список попадают только удочки (подтип 10, вариант 1).
	int32 Visible = 0;
	const int32 BagSize = I->GetContainerSize(kRodContainer);
	for (int32 Bag = 0; Bag < BagSize && Visible < kCellCount; ++Bag)
	{
		const FAsdaItemInstance* It = I->GetCell(kRodContainer, Bag);
		if (!It || It->IsEmpty()) { continue; }
		const FAsdaItemRecord* Rec = IT->GetItem(It->ItemCode);
		if (!Rec || Rec->ItemSubType != kRodSubType || Rec->ItemVariant != kRodVariant) { continue; }

		ListToBagCell[Visible] = Bag;
		// Иконка предмета из атласа (тот же путь, что у ячеек инвентаря).
		if (UImage* Img = Cells[Visible].Icon)
		{
			// ★ Волна 1 (14.09.2026, В1): клетка атласа — общая точка (заворот, кисть 32×32, как было).
			//   Нет данных/атласа → кисть не трогается, узел всё равно показывается — как было.
			if (TexMgr) { AsdaUi::ApplyItemIcon(GetGameInstance(), Img, It->ItemCode, FVector2D(32.f, 32.f)); }
			Img->SetVisibility(ESlateVisibility::SelfHitTestInvisible);
		}
		// 4) Курсор 15990 встаёт на выбранную позицию со сдвигом +1 по X (эталон: `slotPosX + 1`).
		if (SelIndex == Visible && SelCursor && Cells[Visible].Icon)
		{
			if (UCanvasPanelSlot* SS = AsdaUi::SlotOf(SelCursor))
			{
				const UCanvasPanelSlot* IS = AsdaUi::SlotOf(Cells[Visible].Icon);
				if (IS)
				{
					const FMargin Ic2 = IS->GetOffsets();
					FMargin O = SS->GetOffsets();
					O.Left = Ic2.Left + 1.f;
					O.Top  = Ic2.Top;
					SS->SetOffsets(O);
				}
			}
			SelCursor->SetVisibility(ESlateVisibility::SelfHitTestInvisible);
			SelCursor->RestartAnim();
		}
		++Visible;
	}
	UE_LOG(LogAsdaFishrod, Verbose, TEXT("[снасть] список перестроен: удочек %d, выбрана позиция %d"), Visible, SelIndex);
}

// ═══════════════════════════════ ВЫБОР И ПОДТВЕРЖДЕНИЕ ═════════════════════════════════════════

// Эталон `uiInventorySelectFilter@0xD34B90` (событие 0xB): только сдвинуть курсор.
void UAsdaFishingToolSubsystem::SelectListIndex(int32 ListIndex)
{
	if (ListIndex < 0 || ListIndex >= kCellCount) { return; }
	if (ListToBagCell[ListIndex] < 0) { return; }   // эталон: пустая позиция не выбирается
	SelIndex = ListIndex;
	if (SelectSoundId > 0) { PlayUiSound(SelectSoundId); }
	RefreshList();
}

// Эталон `uiInventorySelectEquipSlot@0xD34C10` (событие 0xD): выбрать + подтвердить.
bool UAsdaFishingToolSubsystem::ConfirmListIndex(int32 ListIndex)
{
	if (ListIndex < 0 || ListIndex >= kCellCount) { return false; }
	const int32 Bag = ListToBagCell[ListIndex];
	if (Bag < 0) { return false; }
	SelIndex = ListIndex;
	UAsdaInventorySubsystem* I = Inv();
	UGameInstance* GI = GetGameInstance();
	UAsdaItemTableSubsystem* IT = GI ? GI->GetSubsystem<UAsdaItemTableSubsystem>() : nullptr;
	const FAsdaItemInstance* Rod = I ? I->GetCell(kRodContainer, Bag) : nullptr;
	const FAsdaItemRecord* RodRec = (IT && Rod && !Rod->IsEmpty()) ? IT->GetItem(Rod->ItemCode) : nullptr;
	// Эталон: перед `itemConfirmPendingAction` проверяет `sub_9306BF` (взвод) и тип удочки.
	if (bPendingArmed && RodRec && RodRec->ItemSubType == kRodSubType && RodRec->ItemVariant == kRodVariant)
	{
		RodContainer = kRodContainer; RodCell = Bag;
		const bool bOk = ConfirmSelectedPair();
		RefreshList();
		return bOk;
	}
	RefreshList();
	return false;
}

// Эталон `itemConfirmPendingAction@0xFC9D70` + текст диалога `case 'N'` (=78).
bool UAsdaFishingToolSubsystem::ConfirmSelectedPair()
{
	if (!bPendingArmed) { return false; }
	bPendingArmed = false;   // эталон снимает взвод ДО проверки совместимости

	UAsdaInventorySubsystem* I = Inv();
	UGameInstance* GI = GetGameInstance();
	UAsdaItemTableSubsystem* IT = GI ? GI->GetSubsystem<UAsdaItemTableSubsystem>() : nullptr;
	UAsdaWindowSubsystem* WM = GI ? GI->GetSubsystem<UAsdaWindowSubsystem>() : nullptr;
	if (!I || !IT || !WM) { return false; }

	const FAsdaItemInstance* Rod = I->GetCell(RodContainer, RodCell);
	const FAsdaItemInstance* Mat = I->GetCell(PendingContainer, PendingCell);
	if (!Rod || Rod->IsEmpty() || !Mat || Mat->IsEmpty()) { return false; }
	const FAsdaItemRecord* RodRec = IT->GetItem(Rod->ItemCode);
	const FAsdaItemRecord* MatRec = IT->GetItem(Mat->ItemCode);
	// `itemCheckUpgradeCompat@0xFC9CB0` — удочка 10/1 И материал 208|209.
	if (!RodRec || !MatRec) { return false; }
	if (RodRec->ItemSubType != kRodSubType || RodRec->ItemVariant != kRodVariant) { return false; }
	if (MatRec->ItemSubType != kLineSubType && MatRec->ItemSubType != kHookSubType) { return false; }

	const FString Text = BuildConfirmText(*Rod, *Mat, *MatRec);
	UE_LOG(LogAsdaFishrod, Log,
		TEXT("[снасть] подтверждение: удочка «%s» (ячейка %d) ← «%s» (подтип %d, LinkCode %d, ячейка %d); текст='%s'"),
		*IT->GetItemName(Rod->ItemCode), RodCell, *IT->GetItemName(Mat->ItemCode),
		MatRec->ItemSubType, MatRec->LinkCode, PendingCell, *Text);

	// Двухкнопочный msgbox эталона: TWOBUT_MSGBOX, кнопки 108510 «OK» / 108511 «Отмена».
	AsdaEnterGuard::NoEnter(WM)->ShowGameConfirm(Text, [this](bool bYes)
	{
		if (bYes) { SendAttachRequest(); }
		else      { UE_LOG(LogAsdaFishrod, Log, TEXT("[снасть] подтверждение отменено пользователем")); }
	}, /*GaugeGateSec*/ 0.f, kLocFishConfirmOk, kLocFishConfirmCancel, /*очередь: эталон (1, 78) item_inventory.c:67942*/78, 1);
	return true;
}

// Текст подтверждения 1:1 по `case 'N'` (`CItem_BuildTooltipText@0xD6E790`).
FString UAsdaFishingToolSubsystem::BuildConfirmText(const FAsdaItemInstance& Rod, const FAsdaItemInstance& Mat,
                                                    const FAsdaItemRecord& MatRec) const
{
	UGameInstance* GI = GetGameInstance();
	UAsdaItemTableSubsystem* IT = GI ? GI->GetSubsystem<UAsdaItemTableSubsystem>() : nullptr;
	if (!IT || Rod.Raw.Num() < FAsdaItemInstance::kRecordSize) { return FString(); }
	const FString RodName = IT->GetItemName(Rod.ItemCode);
	const FString MatName = IT->GetItemName(Mat.ItemCode);

	// Ключ 96/97 задаёт LinkCode материала: 1 = «органика» (0x60), 2 = «синтетика» (0x61).
	//   При LinkCode вне {1,2} эталон оставляет текст ПУСТЫМ — воспроизводим.
	int32 WantCode = 0;
	if (MatRec.LinkCode == 1) { WantCode = kToolCodeOrganic; }
	else if (MatRec.LinkCode == 2) { WantCode = kToolCodeSynthetic; }
	if (WantCode == 0) { return FString(); }

	const bool bHook = (MatRec.ItemSubType == kHookSubType);
	const int32 CodeOff = bHook ? kRodHookCodeOff : kRodLineCodeOff;
	const int32 ValOff  = bHook ? kRodHookValOff  : kRodLineValOff;
	const int32 CurCode = FrRdI16(Rod.Raw.GetData() + CodeOff);
	const int32 CurVal  = FrRdI16(Rod.Raw.GetData() + ValOff);

	// ★★★ 09.10.2026 (аудит fmtArg, `Reports/owner_1009/fmtarg_audit.md`): порядок аргументов — ПО БАЙТАМ.
	//   Декомпил пишет `fmtArg0, fmtArg1, fmtArg2` в порядке PUSH, то есть ОБРАТНО порядку `%s` (fmtArg0 = последний).
	//   Порт читал их как прямой порядок и ставил удочку на место материала. Дизасм сборки IDB (13 344 768 б):
	//     111028 0xD6FFD4…0xD70051 / 111030 0xD701DB…0xD70258: push «мат +N» ([ebp-0x16F0]/[-0x17F0]), push имя удочки
	//       ([ebp-0x1670] = g_pSelectedItemRecord), push имя материала ([ebp-0x166C], у него проверяется LinkCode +0x60),
	//       `add esp,0x14` ⇒ sprintf(buf, fmt, МАТЕРИАЛ, УДОЧКА, «МАТЕРИАЛ +N»);
	//     111026 0xD7005C…0xD7009F / 111029 0xD70263…0xD702A6 (и вторые ветки LinkCode 2): push удочка, push материал,
	//       `add esp,0x10` ⇒ sprintf(buf, fmt, МАТЕРИАЛ, УДОЧКА).
	if (CurCode == WantCode)
	{
		// Снасть того же вида уже стоит: аргументы имя материала, имя удочки, «имя материала +N».
		const int32 LocId = bHook ? kLocHookReplace : kLocLineReplace;
		return FormatPrintf(LocRaw(LocId), { MatName, RodName, FString::Printf(TEXT("%s +%d"), *MatName, CurVal) });
	}
	// Снасти нет (или другого вида): аргументы имя материала, имя удочки.
	const int32 LocId = bHook ? kLocHookSimple : kLocLineSimple;
	return FormatPrintf(LocRaw(LocId), { MatName, RodName });
}

// ═══════════════════════════════ СЕТЬ ══════════════════════════════════════════════════════════

bool UAsdaFishingToolSubsystem::SendAttachRequest()
{
	UAsdaInventorySubsystem* I = Inv();
	UGameInstance* GI = GetGameInstance();
	UAsdaNetSubsystem* Net = GI ? GI->GetSubsystem<UAsdaNetSubsystem>() : nullptr;
	UAsdaItemTableSubsystem* IT = GI ? GI->GetSubsystem<UAsdaItemTableSubsystem>() : nullptr;
	if (!I || !Net || !IT) { return false; }
	if (bAwaitingResult)
	{
		UE_LOG(LogAsdaFishrod, Log, TEXT("[снасть] ждём 6166 на предыдущий запрос — пропуск"));
		return false;
	}
	const FAsdaItemInstance* Rod = I->GetCell(RodContainer, RodCell);
	const FAsdaItemInstance* Mat = I->GetCell(PendingContainer, PendingCell);
	if (!Rod || Rod->IsEmpty() || !Mat || Mat->IsEmpty()) { return false; }
	const FAsdaItemRecord* MatRec = IT->GetItem(Mat->ItemCode);
	if (!MatRec) { return false; }
	// actionType: 0 при подтипе 208 (леска), 1 при 209 (крючок) — так в `itemNet_SendItemActionPacket`.
	const uint8 ActionType = (MatRec->ItemSubType == kHookSubType) ? 1 : 0;
	const bool bOk = Net->SendFishingToolUse6165(Net->SelectedCharId, ActionType, Rod->Raw, Mat->Raw);
	if (bOk) { bAwaitingResult = true; SentAtS = FPlatformTime::Seconds(); }
	return bOk;
}

void UAsdaFishingToolSubsystem::OnFishingToolResult(const TArray<uint8>& Body117)
{
	// применяем в Tick (закон `no-reentrant-net-callbacks`); ★ Аудит №3 (B10): замок — в очереди
	PendingResults.Push(Body117);
}

// Эталон `invHandleItemSlotSyncPacket@0xFCA590`.
void UAsdaFishingToolSubsystem::ApplyFishingToolResult(const TArray<uint8>& B)
{
	bAwaitingResult = false;
	if (B.Num() < 117) { return; }
	const uint8 Result = B[0];
	if (Result == 0)
	{
		// Эталон: `if (payload[0])` — НУЛЕВОЙ код в оригинале не показывается вообще (окно молчит).
		UE_LOG(LogAsdaFishrod, Warning,
			TEXT("[снасть] 6166 result=0: сервер отклонил (IsCheck_CharFlag / PortalLogout / рассинхрон userIdx)"));
		return;
	}
	if (Result != 1)
	{
		// Ф2: эталон печатает голое число (`sprintf("%d", payload[0])`) — оставляем 1:1, расшифровка в лог.
		const TCHAR* Why =
			Result == 2 ? TEXT("материал не найден в контейнере") :
			Result == 3 ? TEXT("удочка не найдена в контейнере") :
			Result == 4 ? TEXT("материал не 208/209") :
			Result == 5 ? TEXT("предмет не удочка (тип 9 / подтип 10 / вариант 1)") : TEXT("код сервером не документирован");
		UE_LOG(LogAsdaFishrod, Warning, TEXT("[снасть] 6166 отказ, код %d — %s"), (int32)Result, Why);
		Msg1Button(FString::Printf(TEXT("%d"), (int32)Result));
		return;
	}

	// Успех: нотис 111027 + две записи по 57 б (@3 обновлённая удочка, @60 израсходованный материал).
	Msg1Button(LocRaw(kLocDone));
	UAsdaInventorySubsystem* I = Inv();
	for (int32 Rec = 0; Rec < 2; ++Rec)
	{
		const int32 Off = 3 + Rec * FAsdaItemInstance::kRecordSize;
		TArray<uint8> R;
		R.Append(B.GetData() + Off, FAsdaItemInstance::kRecordSize);
		const int32 Code = FrRdI32(R.GetData() + 0);
		const int32 Cont = (int32)R[4];
		const int32 Slot = FrRdI16(R.GetData() + 5);
		const int32 Amount = FrRdI32(R.GetData() + 9);
		UE_LOG(LogAsdaFishrod, Log, TEXT("[снасть] 6166 запись %d: код=%d контейнер=%d слот=%d кол-во=%d%s"),
			Rec, Code, Cont, Slot, Amount, Amount == -1 ? TEXT(" (слот очищается)") : TEXT(""));
		if (Rec == 0 && Amount >= 0 && R.Num() >= 48)
		{
			UE_LOG(LogAsdaFishrod, Log, TEXT("[снасть]   снасть удочки после ответа: леска код=%d запас=%d, крючок код=%d запас=%d"),
				FrRdI16(R.GetData() + kRodLineCodeOff), FrRdI16(R.GetData() + kRodLineValOff),
				FrRdI16(R.GetData() + kRodHookCodeOff), FrRdI16(R.GetData() + kRodHookValOff));
		}
		if (I) { I->ApplyExternalRecord(R); }
	}
	// Взвод уже снят подтверждением; окно эталон НЕ закрывает — можно поставить снасть на другую удочку.
	RefreshList();
}

// ═══════════════════════════════ ВВОД ══════════════════════════════════════════════════════════

// ★ D1 (аудит `code_dedup_audit_2.md`): тело переехало в `AsdaUi::IsCursorOverWidget`.
bool UAsdaFishingToolSubsystem::IsCursorOver(const UWidget* W) const
{
	return AsdaUi::IsCursorOverWidget(W);
}

int32 UAsdaFishingToolSubsystem::HitTestCell() const
{
	for (int32 i = 0; i < kCellCount; ++i)
	{
		if (ListToBagCell[i] >= 0 && IsCursorOver(Cells[i].Icon)) { return i; }
	}
	return -1;
}

void UAsdaFishingToolSubsystem::TickPointer()
{
	if (!FSlateApplication::IsInitialized()) { return; }
	UGameInstance* GI = GetGameInstance();
	UAsdaWindowSubsystem* WM = GI ? GI->GetSubsystem<UAsdaWindowSubsystem>() : nullptr;
	if (!WM) { return; }
	// Чужой всплывающий слой поверх нас забирает ввод целиком. Латч здесь НЕ трогается сознательно.
	// ⚠ МОДАЛКА ИЗ ЭТОГО УСЛОВИЯ УБРАНА и проверяется НИЖЕ, под `Mouse.Update()` — см. гейт там.
	if (WM->IsPointerBlockedByTopmost(kFishrodZoneId)) { return; }

	// ★ Аудит №3 (B3): опрос обеих кнопок и фронты — общая точка, ровно на месте прежнего опроса
	//   (ранний выход выше — чужой слой поверх — латч не трогает, как и раньше).
	Mouse.Update();
	// ★★★ МОДАЛЬНЫЙ ГЕЙТ (П8-9, волна 9): под открытым модальным нотисом эталон не отдаёт мышь
	//   НИКОМУ ниже (`ui_core.c:104149-104153`). Обоснование, порядок и ключ отката
	//   `asda.UI.ModalGatePolling` — в шапке `AsdaUi::IsModalDispatchBlocked` (`AsdaUiHelpers.h`).
	//   ★ ПОЧЕМУ ПРОВЕРКА МОДАЛКИ ПЕРЕЕХАЛА СЮДА, ПОД ОПРОС. Раньше она стояла одним условием с
	//     `IsPointerBlockedByTopmost` ВЫШЕ `Mouse.Update()`, и латч замирал в доносном состоянии:
	//     на возврате из-под нотиса выстреливал ЛОЖНЫЙ фронт ОТПУСКАНИЯ, то есть «применить
	//     позицию», которого игрок не заказывал. `Reset()` кладёт латч в «кнопка отпущена»,
	//     поэтому удержанная кнопка после нотиса даст ЧЕСТНЫЙ фронт нажатия.
	//   ⚠ Своя защёлка `PressedIndex` сбрасывается здесь же, а висящую плашку ячейки — ГАСИМ.
	if (AsdaUi::IsModalDispatchBlocked(GI))
	{
		PressedIndex = -1;
		if (HoverIndex >= 0) { HoverIndex = -1; HideCellTip(); }
		Mouse.Reset();
		return;
	}
	const int32 Under = HitTestCell();

	// ── событие 8/0xA: наведение → тултип предмета (эталон `uiSlot_BuildHoverTooltip@0xD35070`) ──
	if (Under != HoverIndex)
	{
		HoverIndex = Under;
		HideCellTip();
		if (Under >= 0) { ShowCellTip(Under); }
	}

	// ★★★ Волна 9 заточки (С-1): окно 15900 — процедура инвентаря (`CGameWindow_OnCommand`, группа 159);
	//   общая блокировка `uiCheckRecentActivity` глушит у неё 0xB (`uiInventorySelectFilter`, ui_core.c:47932)
	//   и 0xD (`uiInventorySelectEquipSlot`, :47991). Наведение выше — не глушится. Защёлку снимаем.
	const bool bRecentLock = AsdaUi::CheckRecentActivity(GI);
	if (bRecentLock && (Mouse.bLmbDown || Mouse.bLmbUp)) { PressedIndex = -1; }
	// ── событие 0xB: нажатие ЛКМ = выбрать позицию ──
	else if (Mouse.bLmbDown)
	{
		PressedIndex = Under;
		if (Under >= 0) { SelectListIndex(Under); }
	}
	// ── событие 0xD: отпускание над той же ячейкой = «применить» (эталонный двойной клик) ──
	else if (Mouse.bLmbUp)
	{
		if (Under >= 0 && Under == PressedIndex) { ConfirmListIndex(Under); }
		PressedIndex = -1;
	}

	// ── Ф1: ПКМ над окном = отмена (обещано нотисом 111051; в бинаре обработчика нет) ──
	if (Mouse.bRmbDown && CVarLegacyNoRmbCancel.GetValueOnGameThread() == 0)
	{
		FVector2D Mn, Mx;
		const FVector2D C = Mouse.Cursor;   // тот же `GetCursorPos()` того же кадра
		if (GetWindowRect(Mn, Mx) && C.X >= Mn.X && C.X <= Mx.X && C.Y >= Mn.Y && C.Y <= Mx.Y && !AsdaUi::IsCoveredByHigherWindow(WinGroup, C))
		{
			UE_LOG(LogAsdaFishrod, Log, TEXT("[снасть] ПКМ по окну — отмена (наше исправление Ф1)"));
			CancelPending(/*bAlsoHide*/ true);
		}
	}
	// ★ Аудит №3 (B3): запись обоих латчей делает `Mouse.Update()` в начале функции.
}

void UAsdaFishingToolSubsystem::ShowCellTip(int32 ListIndex)
{
	if (ListIndex < 0 || ListIndex >= kCellCount) { return; }
	const int32 Bag = ListToBagCell[ListIndex];
	if (Bag < 0) { return; }
	UAsdaInventorySubsystem* I = Inv();
	UGameInstance* GI = GetGameInstance();
	UAsdaItemTooltipSubsystem* Tip = GI ? GI->GetSubsystem<UAsdaItemTooltipSubsystem>() : nullptr;
	const FAsdaItemInstance* It = I ? I->GetCell(kRodContainer, Bag) : nullptr;
	if (!Tip || !It || It->IsEmpty()) { return; }
	// Эталон строит тултип по 57-байтовой КОПИИ записи (`qmemcpy(v6, …, 0x39)`) — общая плашка игры.
	Tip->ShowItemTooltip(*It, /*bSuppressCompare*/ false, EAsdaTipOwner::Item);
	bTipShown = true;
}

void UAsdaFishingToolSubsystem::HideCellTip()
{
	if (!bTipShown) { return; }
	bTipShown = false;
	UGameInstance* GI = GetGameInstance();
	if (UAsdaItemTooltipSubsystem* Tip = GI ? GI->GetSubsystem<UAsdaItemTooltipSubsystem>() : nullptr)
	{
		Tip->HideTooltipFor(EAsdaTipOwner::Item);   // закон `ui-shared-tip-ownership`: гасит только владелец
	}
}

// ═══════════════════════════════ ПОМОЩНИКИ ═════════════════════════════════════════════════════

// ★ D10 (аудит `code_dedup_audit_2.md`): тело переехало в `AsdaUi::LocRaw`. Фолбэк ЭТОГО окна —
//   ПУСТАЯ строка (bDebugFallback = false): нотисы 111026/111028/111029/111030/111051 идут в msgbox,
//   и вместо отсутствующего текста окно показывать «[#N]» (до 11.10.2026 — «[loc N]», N9) не должно. Поведение сохранено 1:1.
FString UAsdaFishingToolSubsystem::LocRaw(int32 Id) const
{
	// ★ Волна 1 (14.09.2026, В2): общая точка `AsdaNotice::LocRaw` (поведение то же).
	return AsdaNotice::LocRaw(GetGameInstance(), Id, /*bDebugFallback*/false);
}

// ★ D8 (аудит `code_dedup_audit_2.md`): подстановка `%s`/`%d` подряд переехала в
//   `AsdaUi::FormatPrintfArgs`.
//   ★ ИСПРАВЛЕН ДЕФЕКТ ПОРТА (закон `fix-original-defects`): ЭТА копия НЕ обрабатывала «%%», и
//   лишний «%» уезжал в текст. Эталон гонит loc-строку через C-`sprintf`, а он «%%» схлопывает —
//   верной была копия окна самоцветов, она и стала общей точкой.
FString UAsdaFishingToolSubsystem::FormatPrintf(const FString& Fmt, const TArray<FString>& Args)
{
	return AsdaUi::FormatPrintfArgs(Fmt, Args);
}

void UAsdaFishingToolSubsystem::PlayUiSound(int32 SoundId) const
{
	if (SoundId <= 0) { return; }
	UGameInstance* GI = GetGameInstance();
	if (UAsdaSoundSubsystem* Snd = GI ? GI->GetSubsystem<UAsdaSoundSubsystem>() : nullptr)
	{
		Snd->PlaySound2DById(SoundId);
	}
}

// Однокнопочный msgbox эталона (`CItem_BuildTooltipText(g_pItemTooltipUI, 0, −1, текст, 1)` →
//   ONEBUT_MSGBOX 1500: текст `ONEBUT_TEXT` 1501, кнопка `ONEBUT_BUT00` 1503 с подписью loc 2288,
//   полоса `ONEBUT_GAUGE` 1502 скрыта при init, ui_core.c:55951).
// ★ D2 (аудит `code_dedup_audit_2.md`): полотно сборки окна 1500 переехало в
//   `UAsdaWindowSubsystem::ShowGameMessage` (общая точка рядом с двухкнопочным `ShowGameConfirm`).
//   Здесь остался форвард; подпись кнопки — общий нотис `AsdaLoc::kBtnOk` (2288), что и был.
void UAsdaFishingToolSubsystem::Msg1Button(const FString& Text)
{
	// ★ ИСПРАВЛЕНИЕ 11.10.2026 (дедуп №2, В2-A, U2): было «нет оконной подсистемы ⇒ молча» — стало общая политика
	//   `AsdaNotice::MsgBox` (окна нет ⇒ строка в ленту той же категорией ItemRefused, что и прежний умолчательный канал).
	AsdaNotice::MsgBox(GetGameInstance(), EAsdaMsgCategory::ItemRefused, Text, AsdaLoc::kBtnOk);
	UE_LOG(LogAsdaFishrod, Log, TEXT("[снасть] msgbox: %s"), *Text);
}

// ═══════════════════════════════ ДИАГНОСТИКА ═══════════════════════════════════════════════════

FString UAsdaFishingToolSubsystem::DumpControls() const
{
	FString S = TEXT("═══ ОКНО «ВЗЯТЬ РЫБОЛОВНУЮ СНАСТЬ» (Inven_Fishrod_Win, IDNum 15900) ═══\n");
	auto Line = [&S](const TCHAR* Role, int32 IdNum, const UWidget* W)
	{
		if (!W) { S += FString::Printf(TEXT("  %-26s id=%-6d  НЕ НАЙДЕН\n"), Role, IdNum); return; }
		const UCanvasPanelSlot* CS = AsdaUi::SlotOf(W);
		const FMargin O = CS ? CS->GetOffsets() : FMargin();
		S += FString::Printf(TEXT("  %-26s id=%-6d  %-34s %-20s слот(%.0f,%.0f,%.0f×%.0f) vis=%d\n"),
			Role, IdNum, *W->GetName(), *W->GetClass()->GetName(),
			O.Left, O.Top, O.Right, O.Bottom, (int32)W->GetVisibility());
	};
	Line(TEXT("окно (группа)"),      kWinIdNum,   WinGroup);
	Line(TEXT("подложка Box_Black"), -1,          BaseGroup);
	Line(TEXT("шапка WinSm_CBar"),   -1,          TitleBarGroup);
	Line(TEXT("заголовок+ручка"),    kTitleIdNum, TitleGroup);
	Line(TEXT("заголовок (текст)"),  kTitleIdNum, TitleText);
	Line(TEXT("крестик"),            kCloseIdNum, CloseBtn);
	Line(TEXT("курсор выбора"),      kSelIdNum,   SelCursor);
	for (int32 i = 0; i < kCellCount; ++i)
	{
		Line(*FString::Printf(TEXT("ячейка %02d (иконка)"), i), kCellFirstId + i, Cells[i].Icon);
		Line(*FString::Printf(TEXT("ячейка %02d (текст)"), i),  kCellFirstId + i, Cells[i].Text);
	}
	S += FString::Printf(TEXT("  ИТОГО: контролы разобраны=%s, окно %s\n"),
		bControlsReady ? TEXT("да") : TEXT("НЕТ"), bWindowOpen ? TEXT("открыто") : TEXT("закрыто"));
	return S;
}

FString UAsdaFishingToolSubsystem::DumpState() const
{
	UAsdaInventorySubsystem* I = Inv();
	UGameInstance* GI = GetGameInstance();
	UAsdaItemTableSubsystem* IT = GI ? GI->GetSubsystem<UAsdaItemTableSubsystem>() : nullptr;
	FString S = FString::Printf(
		TEXT("═══ СНАСТЬ: окно=%s, взвод=%s (материал контейнер=%d ячейка=%d), удочка контейнер=%d ячейка=%d, ")
		TEXT("позиция списка=%d, ждём 6166=%s ═══\n"),
		bWindowOpen ? TEXT("открыто") : TEXT("закрыто"), bPendingArmed ? TEXT("ДА") : TEXT("нет"),
		PendingContainer, PendingCell, RodContainer, RodCell, SelIndex, bAwaitingResult ? TEXT("да") : TEXT("нет"));
	if (bPendingArmed && I && IT)
	{
		if (const FAsdaItemInstance* M = I->GetCell(PendingContainer, PendingCell))
		{
			const FAsdaItemRecord* R = IT->GetItem(M->ItemCode);
			S += FString::Printf(TEXT("  материал: код=%d «%s» подтип=%d LinkCode=%d\n"),
				M->ItemCode, *IT->GetItemName(M->ItemCode), R ? R->ItemSubType : -1, R ? R->LinkCode : -1);
		}
	}
	for (int32 i = 0; i < kCellCount; ++i)
	{
		if (ListToBagCell[i] < 0) { continue; }
		const FAsdaItemInstance* It = I ? I->GetCell(kRodContainer, ListToBagCell[i]) : nullptr;
		if (!It) { continue; }
		S += FString::Printf(TEXT("  список[%02d] ← контейнер 1 ячейка %02d: код=%d «%s» леска(код=%d запас=%d) крючок(код=%d запас=%d)%s\n"),
			i, ListToBagCell[i], It->ItemCode, IT ? *IT->GetItemName(It->ItemCode) : TEXT("?"),
			It->Raw.Num() >= 48 ? FrRdI16(It->Raw.GetData() + kRodLineCodeOff) : 0,
			It->Raw.Num() >= 48 ? FrRdI16(It->Raw.GetData() + kRodLineValOff) : 0,
			It->Raw.Num() >= 48 ? FrRdI16(It->Raw.GetData() + kRodHookCodeOff) : 0,
			It->Raw.Num() >= 48 ? FrRdI16(It->Raw.GetData() + kRodHookValOff) : 0,
			(i == SelIndex) ? TEXT("  ← ВЫБРАНА") : TEXT(""));
	}
	return S;
}

// Эмуляция ответа сервера: собираем 117 б ровно так, как их шлёт `sub_40E8F0` (RecvDB_FishingToolUse).
void UAsdaFishingToolSubsystem::SimulateResult(uint8 Result)
{
	TArray<uint8> B;
	B.SetNumZeroed(117);
	B[0] = Result;
	if (Result == 1)
	{
		UAsdaInventorySubsystem* I = Inv();
		const FAsdaItemInstance* Rod = I ? I->GetCell(RodContainer, RodCell) : nullptr;
		const FAsdaItemInstance* Mat = I ? I->GetCell(PendingContainer, PendingCell) : nullptr;
		if (Rod && Rod->Raw.Num() >= FAsdaItemInstance::kRecordSize)
		{
			FMemory::Memcpy(B.GetData() + 3, Rod->Raw.GetData(), FAsdaItemInstance::kRecordSize);
			// Сервер вписывает пару «код снасти / запас»; для проверки ставим органическую леску 96 и запас 4.
			B[3 + kRodLineCodeOff] = (uint8)(kToolCodeOrganic & 0xFF); B[3 + kRodLineCodeOff + 1] = 0;
			B[3 + kRodLineValOff]  = 4;                                B[3 + kRodLineValOff + 1] = 0;
		}
		if (Mat && Mat->Raw.Num() >= FAsdaItemInstance::kRecordSize)
		{
			FMemory::Memcpy(B.GetData() + 60, Mat->Raw.GetData(), FAsdaItemInstance::kRecordSize);
			// amount = −1 ⇒ слот материала очищается (так делает сервер перед отправкой).
			for (int32 k = 0; k < 4; ++k) { B[60 + 9 + k] = 0xFF; }
		}
	}
	// ★ Аудит №3 (B10): та же общая очередь, что и у живого 6166 (это консольный ввод «как с сервера»).
	PendingResults.Push(MoveTemp(B));
}

// ═══════════════════════════════ КОНСОЛЬНЫЕ КОМАНДЫ ════════════════════════════════════════════
//   Проверка без живого сервера. Все команды печатают и в лог, и в системный журнал.

static UAsdaFishingToolSubsystem* FrOf(UWorld* W)
{
	return (W && W->GetGameInstance()) ? W->GetGameInstance()->GetSubsystem<UAsdaFishingToolSubsystem>() : nullptr;
}

// ★ U13 (В1 дедупа №2): тело — общая точка `AsdaNotice::ConsoleEcho` (прежняя копия была дословной, менялась только категория лога).
#if !UE_BUILD_SHIPPING   // ★ античит А2 (12.10.2026): отладка самотестов/дев-команд — только вне Shipping
static void FrEcho(UWorld* W, const FString& Text)
{
	AsdaNotice::ConsoleEcho(W, Text, LogAsdaFishrod);
}
#endif // !UE_BUILD_SHIPPING

#if ASDA_DEV_CONSOLE   // ★ античит А2: дев-команда, в Shipping не регистрируется
static FAutoConsoleCommandWithWorldAndArgs GFrDump(
	TEXT("asda.Fishrod.Dump"),
	TEXT("Окно снасти 15900: дамп КАЖДОГО разобранного контрола (закон full-structural-analysis)"),
	FConsoleCommandWithWorldAndArgsDelegate::CreateLambda([](const TArray<FString>&, UWorld* W)
	{
		if (UAsdaFishingToolSubsystem* F = FrOf(W)) { FrEcho(W, F->DumpControls()); }
	}));
#endif // ASDA_DEV_CONSOLE

#if ASDA_DEV_CONSOLE   // ★ античит А2: дев-команда, в Shipping не регистрируется
static FAutoConsoleCommandWithWorldAndArgs GFrState(
	TEXT("asda.Fishrod.State"),
	TEXT("Окно снасти 15900: взвод, выбранная удочка, список и коды снасти в записях удочек"),
	FConsoleCommandWithWorldAndArgsDelegate::CreateLambda([](const TArray<FString>&, UWorld* W)
	{
		if (UAsdaFishingToolSubsystem* F = FrOf(W)) { FrEcho(W, F->DumpState()); }
	}));
#endif // ASDA_DEV_CONSOLE

// Полный офлайн-сценарий: разложить удочки и леску/крючок по контейнерам и открыть окно.
#if ASDA_DEV_CONSOLE   // ★ античит А2: дев-команда, в Shipping не регистрируется
static FAutoConsoleCommandWithWorldAndArgs GFrSeed(
	TEXT("asda.Fishrod.Seed"),
	TEXT("asda.Fishrod.Seed [кодМатериала=69687] — ЛОКАЛЬНО положить 3 удочки в контейнер 1 и материал ")
	TEXT("(208 леска / 209 крючок) в контейнер 2, затем открыть окно 15900. Сервер не нужен."),
	FConsoleCommandWithWorldAndArgsDelegate::CreateLambda([](const TArray<FString>& A, UWorld* W)
	{
		// ★ С-Р1 (26.09.2026), офлайн-гейт: кладёт поддельные удочки/материал в сумку — следующий 6165 ушёл бы с их записями.
		if (FAsdaOutgoingGate::RefuseDebugWhenOnline(W, TEXT("asda.Fishrod.Seed"))) { return; }
		UGameInstance* GI = W ? W->GetGameInstance() : nullptr;
		UAsdaInventorySubsystem* I = GI ? GI->GetSubsystem<UAsdaInventorySubsystem>() : nullptr;
		UAsdaFishingToolSubsystem* F = FrOf(W);
		if (!I || !F) { FrEcho(W, TEXT("снасть: подсистемы не готовы")); return; }
		if (!I->IsWindowOpen()) { I->OpenWindow(); }
		// Удочки из item_list: 69657 «Старая удочка», 69658 «Крепкая удочка», 69630 «Гиперкарбон-1000».
		I->DebugPutItem(1, 0, 69657, 1);
		I->DebugPutItem(1, 3, 69658, 1);
		I->DebugPutItem(1, 7, 69630, 1);
		const int32 MatCode = (A.Num() >= 1) ? FCString::Atoi(*A[0]) : 69687;
		I->DebugPutItem(2, 0, MatCode, 1);
		F->OnInventoryDoubleClick(2, 0);
		FrEcho(W, FString::Printf(TEXT("снасть: удочки 69657/69658/69630 в контейнере 1, материал %d в контейнере 2 (ячейка 0); окно %s"),
			MatCode, F->IsWindowOpen() ? TEXT("открыто") : TEXT("НЕ открылось")));
	}));
#endif // ASDA_DEV_CONSOLE

#if ASDA_DEV_CONSOLE   // ★ античит А2: дев-команда, в Shipping не регистрируется
static FAutoConsoleCommandWithWorldAndArgs GFrSelect(
	TEXT("asda.Fishrod.Select"),
	TEXT("asda.Fishrod.Select <позиция 0..29> — выбрать удочку в списке (эталонное событие 0xB)"),
	FConsoleCommandWithWorldAndArgsDelegate::CreateLambda([](const TArray<FString>& A, UWorld* W)
	{
		UAsdaFishingToolSubsystem* F = FrOf(W);
		if (!F || A.Num() < 1) { FrEcho(W, TEXT("использование: asda.Fishrod.Select <позиция>")); return; }
		F->SelectListIndex(FCString::Atoi(*A[0]));
		FrEcho(W, F->DumpState());
	}));
#endif // ASDA_DEV_CONSOLE

#if ASDA_DEV_CONSOLE   // ★ античит А2: дев-команда, в Shipping не регистрируется
static FAutoConsoleCommandWithWorldAndArgs GFrConfirm(
	TEXT("asda.Fishrod.Confirm"),
	TEXT("asda.Fishrod.Confirm <позиция 0..29> — выбрать И подтвердить (эталонное событие 0xD): ")
	TEXT("откроется диалог 108510/108511, «OK» пошлёт 6165"),
	FConsoleCommandWithWorldAndArgsDelegate::CreateLambda([](const TArray<FString>& A, UWorld* W)
	{
		UAsdaFishingToolSubsystem* F = FrOf(W);
		if (!F || A.Num() < 1) { FrEcho(W, TEXT("использование: asda.Fishrod.Confirm <позиция>")); return; }
		const bool bOk = F->ConfirmListIndex(FCString::Atoi(*A[0]));
		FrEcho(W, FString::Printf(TEXT("снасть: подтверждение %s"), bOk ? TEXT("запущено (см. диалог)") : TEXT("отклонено (нет взвода / не удочка)")));
	}));
#endif // ASDA_DEV_CONSOLE

#if ASDA_DEV_CONSOLE   // ★ античит А2: дев-команда, в Shipping не регистрируется
static FAutoConsoleCommandWithWorldAndArgs GFrSend(
	TEXT("asda.Fishrod.Send"),
	TEXT("asda.Fishrod.Send — отправить 6165 по уже выбранной паре, минуя диалог"),
	FConsoleCommandWithWorldAndArgsDelegate::CreateLambda([](const TArray<FString>&, UWorld* W)
	{
		UAsdaFishingToolSubsystem* F = FrOf(W);
		if (!F) { return; }
		FrEcho(W, FString::Printf(TEXT("снасть: 6165 %s"), F->SendAttachRequest() ? TEXT("отправлен") : TEXT("НЕ отправлен")));
	}));
#endif // ASDA_DEV_CONSOLE

#if ASDA_DEV_CONSOLE   // ★ античит А2: дев-команда, в Shipping не регистрируется
static FAutoConsoleCommandWithWorldAndArgs GFrSim(
	TEXT("asda.Fishrod.SimResult"),
	TEXT("asda.Fishrod.SimResult [код=1] — эмулировать ответ 6166 (1 успех; 0/2/3/4/5 отказы сервера)"),
	FConsoleCommandWithWorldAndArgsDelegate::CreateLambda([](const TArray<FString>& A, UWorld* W)
	{
		// ★ С-Р1 (26.09.2026), офлайн-гейт: поддельный ответ снимает замок bAwaitingResult (повторный 6165) и подделывает сумку.
		if (FAsdaOutgoingGate::RefuseDebugWhenOnline(W, TEXT("asda.Fishrod.SimResult"))) { return; }
		UAsdaFishingToolSubsystem* F = FrOf(W);
		if (!F) { return; }
		const int32 Code = (A.Num() >= 1) ? FCString::Atoi(*A[0]) : 1;
		F->SimulateResult((uint8)FMath::Clamp(Code, 0, 255));
		FrEcho(W, FString::Printf(TEXT("снасть: эмулирован 6166 result=%d"), Code));
	}));
#endif // ASDA_DEV_CONSOLE

#if ASDA_DEV_CONSOLE   // ★ античит А2: дев-команда, в Shipping не регистрируется
static FAutoConsoleCommandWithWorldAndArgs GFrClose(
	TEXT("asda.Fishrod.Close"),
	TEXT("asda.Fishrod.Close — закрыть окно 15900 и снять взвод"),
	FConsoleCommandWithWorldAndArgsDelegate::CreateLambda([](const TArray<FString>&, UWorld* W)
	{
		if (UAsdaFishingToolSubsystem* F = FrOf(W)) { F->CancelPending(true); FrEcho(W, TEXT("снасть: окно закрыто, взвод снят")); }
	}));
#endif // ASDA_DEV_CONSOLE
