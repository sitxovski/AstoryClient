// Copyright (c) 2026 @sitxovski (Discord)
// AsdaInventorySubsystem_Debug.cpp — ★ 23.10.2026 часть UAsdaInventorySubsystem, вынесенная из AsdaInventorySubsystem.cpp БАЙТ В БАЙТ
//   (разнос по смысловым группам; доказательство — Tools/asda_split_cpp.py verify, план Reports/split_2026-10-23/AsdaInventorySubsystem.plan.json).
//   Диагностика, консольные команды, офлайн-эмуляции волны 2 (только вне Shipping).
//   #include — те же, что у AsdaInventorySubsystem.cpp, в том же порядке; общее — AsdaInventorySubsystemInternal.h.
#include "UI/AsdaInventorySubsystem.h"
#include "AsdaDevConsole.h"   // ★ античит А2: ASDA_DEV_CVAR / ASDA_DEV_CONSOLE — дев-ключи и команды вне Shipping
#include "Text/AsdaLang.h"   // ★ N9 (дедуп №2, В2-A): FAsdaLang::MissingTag — единая заглушка «[#id]»
#include "UI/AsdaNoticeColors.h"   // ★ U12 (В1 дедупа №2): эталонные цвета нотисов одной точкой
#include "AsdaSelf.h"   // ★ волна 1: «это я?» / мой номер / мой уровень — общая точка ядра (E2, E4)
#include "UI/AsdaNotice.h"   // ★ волна 1: фасад нотисов (Loc / Chat / MsgBox)
#include "AsdaBytes.h"   // D4: единые читалки сырой записи (little-endian, DataHex, ASCII-поле)
#include "AsdaPacketWriter.h"   // ★ U6 (В1 дедупа №2): 15 байт слотов-приёмников 6096 через общий писатель
#include "UI/AsdaUiHelpers.h"
#include "UI/AsdaScrollBarNomal.h"   // D3: общая полоса прокрутки скина `Nomal_Scr`            // ★ D1/D2/D5: единые UI-хелперы (дедуп 2026-08-08)
#include "UI/AsdaShowOffSubsystem.h"   // ПКМ по ячейке → меню «похвастаться» (28000)
#include "UI/AsdaLocIds.h"   // Дд2/Дд3: нотисы кнопок ОБЩИХ окон (ONEBUT_MSGBOX 1500 — AsdaLoc::kBtnOk)
#include "UI/AsdaMiniStateSubsystem.h"   // общий тултип HUD (эталон держит один g_ItemTooltip)
#include "UI/AsdaHotbarSubsystem.h"
#include "UI/AsdaWarehouseSubsystem.h"   // свиток возврата склада (5451 подтип 112) → окно 5500
#include "UI/AsdaSelfStoreSubsystem.h"   // билет премиум-лавки (5451 подтип 65) → 6300 (волна Л1 торговли)
#include "UI/AsdaHudSubsystem.h"       // ★ анонс редкой добычи из КОРОБКИ (NotifyItemInfo → 6616), наше добавление
#include "UI/AsdaEffectsSubsystem.h"
#include "UI/AsdaAnimatedPlane.h"
#include "UI/AsdaItemTooltipSubsystem.h"
#include "UI/AsdaPetCageSubsystem.h"      // ★ кнопка 454 — окно питомца 43000 (волна 2)
#include "UI/AsdaItemUpgradeSubsystem.h"  // ★ кнопка 449 — окно улучшения снаряжения 20000 (25.09.2026)
#include "UI/AsdaCraftWindowSubsystem.h"   // ★ кнопка 452 — окно создания снаряжения 76000 (ремесло, волна В2)
#include "UI/AsdaDisassemblySubsystem.h"    // ★ кнопка 453 — окно разборки снаряжения 32000 (волна 2-А, 06.10.2026)
#include "UI/AsdaCraftSubsystem.h"         // ★ волна В3 ремесла: единый предикат «окно ремесла открыто» (`IsAnyCraftWindowOpen`)
#include "UI/AsdaTradeStateSubsystem.h"   // ★ 29.09.2026: гейты рюкзака и бустера — окно обмена 2008 открыто (`CExchange+0x2C`)
#include "Resources/AsdaSoundSubsystem.h"  // ★ кнопка 449 — звук id 5 ветки 66561 (волна 5 заточки)
#include "UI/AsdaFishingToolSubsystem.h"   // ★ окно «Взять рыболовную снасть» 15900 (точки открытия/закрытия)
#include "UI/AsdaFishBookSubsystem.h"      // ★ книга рыбака 74000: использование книги-предмета (подтип 210) → диалог 79
#include "UI/AsdaSowelSubsystem.h"         // ★ ветка САМОЦВЕТОВ правой панели (430…437 / 441…446 / 495 / 499)
#include "UI/AsdaPetHatcherySubsystem.h"   // ★ окно ИНКУБАТОРА 42000 (двойной клик по предмету subType 107)
// ★ обмен по таблице событий (двойной клик по предмету subType 202 → 6098)   ★ R3: комментарий снятого включения "Npc/AsdaNpcDialogSubsystem.h" (файл им не пользовался) — сохранён
#include "Pet/AsdaPetSubsystem.h"          // ★ волна 6: корм (subType 106 → 6108) и расширитель ячеек (104 → 6121)
#include "UI/AsdaSkillUseSubsystem.h"      // ★ волна 2: выбранная цель для поля @0 запроса 6096
#include "UI/AsdaPackagePanelSubsystem.h"  // ★ панель состава пакета 26500 (двойной клик открывает ЕЁ)
#include "UI/AsdaBoosterPanelSubsystem.h"  // ★ окно барабана гачи 10050 (двойной клик открывает ЕГО)
#include "UI/AsdaPremiumGachaSubsystem.h"  // ★ гейт `sub_922D5D@0xB6F370` — гейты №15/№15b применения лопат
// ★ 29.09.2026: `g_pPartyTargetObj` = элитный магазин (гейты №15/№15b лопат)   ★ R3: комментарий снятого включения "UI/AsdaPremiumShopSubsystem.h" (файл им не пользовался) — сохранён
#include "Items/AsdaItemTableSubsystem.h"
#include "Engine/DataTable.h"              // сырые поля шаблона предмета (DataHex) для гейтов панели
#include "Resources/AsdaTextureSubsystem.h"
#include "AsdaNetSubsystem.h"
#include "World/AsdaWorldSubsystem.h"
#include "World/AsdaPlayerController.h"
#include "Blueprint/UserWidget.h"
#include "Blueprint/WidgetTree.h"
#include "Components/CanvasPanel.h"
#include "Components/CanvasPanelSlot.h"
#include "Components/PanelWidget.h"
#include "Components/Image.h"
#include "Components/Button.h"
#include "Components/TextBlock.h"
#include "Engine/Engine.h"
#include "Engine/GameInstance.h"
#include "Engine/Texture2D.h"
#include "Framework/Application/SlateApplication.h"
#include "GameFramework/InputSettings.h"   // DoubleClickTime — интервал двойного клика Slate
#include "HAL/IConsoleManager.h"
#include "AsdaOutgoingGate.h"               // ★ С-Р1: общий офлайн-гейт отладочных команд (RefuseDebugWhenOnline)
#include "UI/AsdaScrollCallSubsystem.h"     // ★ волна 2-Г: свитки перемещения/призыва (окно 1552)
#include "AsdaInventorySubsystemInternal.h"   // ★ 23.10.2026 разнос: общее для частей AsdaInventorySubsystem*.cpp (LogAsdaInven, чистые хелперы)


// ══════════════════════════════ ДИАГНОСТИКА ════════════════════════════════════════════════════

// ★ 25.10.2026: единые часы реестра откатов (определены в AsdaHotbarSubsystem.cpp рядом с ключом asda.Cooldown.RealClock).
double AsdaCooldownNowS(const UObject* Ctx);

FString UAsdaInventorySubsystem::DumpCooldownCurtains() const
{
	const double Now = AsdaCooldownNowS(GetGameInstance());   // ★ 25.10.2026: часы реестра откатов
	FString R = FString::Printf(
		TEXT("=== ШТОРКИ: ИНВЕНТАРЬ (наше отступление 2026-08-14; в оригинале их тут нет) ===\n")
		TEXT("окно %s, контролы %s, вкладка %d (контейнер %d), планов создано: сумка %d, надетое %d\n"),
		bWindowOpen ? TEXT("открыто") : TEXT("закрыто"),
		bControlsReady ? TEXT("готовы") : TEXT("НЕ готовы"),
		CurrentTab, GetGridContainer(),
		[&]{ int32 N = 0; for (const FAsdaInvCellUi& C : GridUi)  { N += C.Cool.Plane ? 1 : 0; } return N; }(),
		[&]{ int32 N = 0; for (const FAsdaInvCellUi& C : EquipUi) { N += C.Cool.Plane ? 1 : 0; } return N; }());

	int32 Lit = 0;
	for (int32 C = 0; C < GridUi.Num(); ++C)
	{
		const FAsdaInvCellUi& U = GridUi[C];
		if (U.Cool.ShownState == 0) { continue; }
		++Lit;
		R += FString::Printf(TEXT("  сумка ячейка %-2d предмет %-6d состояние %d осталось %.2f с\n"),
			C + kGridCols * ScrollRow[FMath::Clamp(CurrentTab, 0, 1)], U.Cool.ShownKey,
			U.Cool.ShownState, FMath::Max(0.0, U.Cool.ShownEndS - Now));
	}
	for (int32 E = 0; E < EquipUi.Num(); ++E)
	{
		const FAsdaInvCellUi& U = EquipUi[E];
		if (U.Cool.ShownState == 0) { continue; }
		++Lit;
		R += FString::Printf(TEXT("  надетое слот %-2d предмет %-6d состояние %d осталось %.2f с\n"),
			E, U.Cool.ShownKey, U.Cool.ShownState, FMath::Max(0.0, U.Cool.ShownEndS - Now));
	}
	if (Lit == 0) { R += TEXT("  горящих шторок нет\n"); }
	return R;
}

FString UAsdaInventorySubsystem::DumpControls() const
{
	FString S = TEXT("=== INVEN_WIN (400): разобранные контролы ===\n");
	S += FString::Printf(TEXT("виджет: %s, контролы готовы: %s, окно %s, панель %s, вкладка %d, ряд %d/%d\n"),
		InvenWidget ? TEXT("есть") : TEXT("НЕТ"), bControlsReady ? TEXT("да") : TEXT("нет"),
		bWindowOpen ? TEXT("открыто") : TEXT("закрыто"),
		bPanelExpanded ? TEXT("раскрыта") : TEXT("свёрнута"),
		CurrentTab, ScrollRow[FMath::Clamp(CurrentTab, 0, 1)], GetMaxScrollRow());
	for (int32 C = 0; C < GridUi.Num(); ++C)
	{
		S += FString::Printf(TEXT("  INVEN_SET%-2d idnum=%d икона=%s текст=%s поз=(%.0f,%.0f) размер=(%.0f,%.0f)\n"),
			// ★ [ПОРТ] 29.10.2026 (инвентарь ×2): idnum `.JUI` есть только у ячеек эталона 0..29 (10010…10039); клоны 30..59
			//   созданы шагом пост-импорта и idnum не имеют (10040…10043 в `.JUI` заняты другими контролами) — печатаем 0.
			C, (C < kShellSlotsLegacy) ? 10010 + C : 0,
			GridUi[C].Icon ? *GridUi[C].Icon->GetName() : TEXT("НЕТ"),
			GridUi[C].StackText ? *GridUi[C].StackText->GetName() : TEXT("НЕТ"),
			GridUi[C].IconPos.X, GridUi[C].IconPos.Y, GridUi[C].IconSize.X, GridUi[C].IconSize.Y);
	}
	for (int32 E = 0; E < EquipUi.Num(); ++E)
	{
		S += FString::Printf(TEXT("  EQUIP_SET%-2d idnum=%d икона=%s поз=(%.0f,%.0f)\n"),
			E, 600 + E,
			EquipUi[E].Icon ? *EquipUi[E].Icon->GetName() : TEXT("НЕТ"),
			EquipUi[E].IconPos.X, EquipUi[E].IconPos.Y);
	}
	S += FString::Printf(TEXT("  402 вкладки: OPT0=%s OPT1=%s текст=%s\n"),
		TabOpt0 ? TEXT("да") : TEXT("НЕТ"), TabOpt1 ? TEXT("да") : TEXT("НЕТ"), TabText ? TEXT("да") : TEXT("НЕТ"));
	S += FString::Printf(TEXT("  403 закрыть=%s · 408 раскрыть=%s · 412 свернуть=%s · 405 корзина=%s\n"),
		BtnClose ? TEXT("да") : TEXT("НЕТ"), BtnExpand ? TEXT("да") : TEXT("НЕТ"),
		BtnCollapse ? TEXT("да") : TEXT("НЕТ"), BtnTrash ? TEXT("да") : TEXT("НЕТ"));
	S += FString::Printf(TEXT("  407 деньги=%s · 409 курсор=%s · 612 курсор экипа=%s · 413 панель=%s\n"),
		MoneyText ? TEXT("да") : TEXT("НЕТ"), GridSel ? TEXT("да") : TEXT("НЕТ"),
		EquipSel ? TEXT("да") : TEXT("НЕТ"), RightPanel ? TEXT("да") : TEXT("НЕТ"));
	S += FString::Printf(TEXT("  80400 автосортировка=%s (пунктов %d) · 450/451 шестерни=%s/%s\n"),
		BtnAutoArrange ? TEXT("да") : TEXT("НЕТ"), AutoArrangeItems.Num(),
		Gear ? TEXT("да") : TEXT("НЕТ"), AniGear ? TEXT("да") : TEXT("НЕТ"));
	S += DumpScrollBar();
	return S;
}

// ★ Выбор ячейки кодом — 1:1 та же ветка, что и клик мышью (событие 0xB эталона,
//   `CInventoryUI_OnSlotClick`): курсор выбора + наполнение правой инфо-панели + список самоцветов.
void UAsdaInventorySubsystem::DebugSelectCell(int32 ContainerId, int32 CellIndex)
{
	if (ContainerId == 3) { SelEquipSlot = CellIndex; RefreshEquip(); }
	else                  { SelCell      = CellIndex; RefreshGrid();  }
	if (Tooltip.FillInfoPanel) { Tooltip.FillInfoPanel(ContainerId, CellIndex); }
	SowelNotifySelect(GetGameInstance(), ContainerId, CellIndex);
	AutoExpandOnSelect(ContainerId, CellIndex);
	UE_LOG(LogAsdaInven, Log, TEXT("[инвентарь] выбор ячейки %d:%d (команда)"), ContainerId, CellIndex);
}

// ★ Дамп выделения — доказательство по дефекту 2 («выделение не сбрасывается»).
FString UAsdaInventorySubsystem::DumpSelection() const
{
	auto Cell = [this](int32 Cont, int32 Idx) -> FString
	{
		if (Idx < 0) { return TEXT("нет"); }
		const FAsdaItemInstance* I = GetCell(Cont, Idx);
		if (!I)            { return FString::Printf(TEXT("%d:%d (вне модели)"), Cont, Idx); }
		if (I->IsEmpty())  { return FString::Printf(TEXT("%d:%d ПУСТА"), Cont, Idx); }
		return FString::Printf(TEXT("%d:%d код=%d кол-во=%d"), Cont, Idx, I->ItemCode, I->Amount);
	};
	return FString::Printf(
		TEXT("═══ ВЫДЕЛЕНИЕ ИНВЕНТАРЯ ═══\n")
		TEXT("  вкладка %d (контейнер %d), окно %s, панель %s\n")
		TEXT("  ячейка сетки  : %s   (курсор 409 %s)\n")
		TEXT("  надетый слот  : %s   (курсор экипировки %s)\n"),
		CurrentTab, GetGridContainer(),
		bWindowOpen ? TEXT("открыто") : TEXT("закрыто"),
		bPanelExpanded ? TEXT("раскрыта") : TEXT("свёрнута"),
		*Cell(GetGridContainer(), SelCell),
		(GridSel && GridSel->GetVisibility() != ESlateVisibility::Collapsed) ? TEXT("виден") : TEXT("скрыт"),
		*Cell(3, SelEquipSlot),
		(EquipSel && EquipSel->GetVisibility() != ESlateVisibility::Collapsed) ? TEXT("виден") : TEXT("скрыт"));
}

FString UAsdaInventorySubsystem::DumpModel() const
{
	FString S = FString::Printf(
		TEXT("=== Инвентарь: модель ===\nзолото=%d · ожидание ответа=%s · очередь автосортировки=%d\n"),
		GetGold(), bActionPending ? TEXT("ДА") : TEXT("нет"), ArrangeQueue.Num());
	auto DumpOne = [&S, this](const TCHAR* Name, int32 ContId)
	{
		const TArray<FAsdaItemInstance>* C = Container(ContId);
		if (!C) { return; }
		int32 Used = 0;
		for (const FAsdaItemInstance& I : *C) { if (!I.IsEmpty()) { ++Used; } }
		S += FString::Printf(TEXT("-- контейнер %d (%s): занято %d из %d\n"), ContId, Name, Used, GetContainerSize(ContId));
		for (int32 i = 0; i < C->Num(); ++i)
		{
			if ((*C)[i].IsEmpty()) { continue; }
			S += FString::Printf(TEXT("   [%3d] %s\n"), i, *(*C)[i].ToLine());
		}
	};
	DumpOne(TEXT("снаряжение/квестовые"), 1);
	DumpOne(TEXT("расходники/золото"), 2);
	DumpOne(TEXT("надетое"), 3);
	DumpOne(TEXT("склад"), 5);
	return S;
}

void UAsdaInventorySubsystem::SimulateBatch(int32 ContainerId, const TArray<uint8>& RawRecords)
{
	OnBatchReceived(ContainerId, RawRecords);
	OnBatchEnd(ContainerId);
	UE_LOG(LogAsdaInven, Log, TEXT("[инвентарь] эмуляция батча контейнера %d: %d б поставлено в очередь"),
		ContainerId, RawRecords.Num());
}

// ★ Применить одну 57-байтовую запись из ЧУЖОГО пакета (6166 «снасть на удочку»).
//   Эталон `invHandleItemSlotSyncPacket@0xFCA590`: маршрут по полям записи (+4 контейнер, +5 слот),
//   `amount(+9) == −1` ⇒ слот очищается (`memset(…, 0, 0x39)`), иначе запись кладётся целиком
//   (`Inventory_SetBagSlot@0xD61650`: `memcpy(g_InventoryBag + 57*slot, rec, 57)` + перерисовка).
bool UAsdaInventorySubsystem::ApplyExternalRecord(const TArray<uint8>& Rec57, EExternalRecordMode Mode)
{
	if (Rec57.Num() < FAsdaItemInstance::kRecordSize) { return false; }
	const int32 Cont = (int32)Rec57[4];
	const int32 Slot = RdI16(Rec57.GetData() + 5);
	const int32 Amount = RdI32(Rec57.GetData() + 9);
	// ★ Режим ремесла (объявление в .h): эталон трогает ТОЛЬКО контейнеры 1/2 и ищет ячейку по слоту.
	if (Mode == EExternalRecordMode::CountOrClear)
	{
		if (Cont != 1 && Cont != 2)
		{
			// `slotTableSetFieldById` / `OnRecv_RemoveSlot_byPtr` для других контейнеров ничего не делают.
			UE_LOG(LogAsdaInven, Log, TEXT("[инвентарь] запись ремесла: контейнер %d слот %d — эталон такие не применяет, пропуск"), Cont, Slot);
			return false;
		}
		FAsdaItemInstance* Cell = GetCellMutable(Cont, Slot);
		if (!Cell)
		{
			UE_LOG(LogAsdaInven, Warning, TEXT("[инвентарь] запись ремесла: контейнер %d слот %d вне модели — пропуск"), Cont, Slot);
			return false;
		}
		if (Amount > 0)
		{
			// `slotTableSetFieldById@0x99A460`: `*(DWORD*)(ячейка + 9) = count` у ячейки с этим слотом — и только.
			Cell->SetAmount(Amount);
			NotifyCell(Cont, Slot);
		}
		else
		{
			ClearCell(Cont, Slot);   // `OnRecv_RemoveSlot_byPtr@0xD0CE30` → memset 57 б
		}
		if (bWindowOpen) { RefreshGrid(); RefreshEquip(); }
		UE_LOG(LogAsdaInven, Log, TEXT("[инвентарь] запись ремесла применена: контейнер=%d слот=%d код=%d кол-во=%d → %s"),
			Cont, Slot, RdI32(Rec57.GetData() + 0), Amount, Amount > 0 ? TEXT("количество") : TEXT("ячейка очищена"));
		return true;
	}
	TArray<FAsdaItemInstance>* C = Container(Cont);
	if (!C || !C->IsValidIndex(Slot))
	{
		UE_LOG(LogAsdaInven, Warning, TEXT("[инвентарь] внешняя запись: контейнер %d слот %d вне модели — пропуск"), Cont, Slot);
		return false;
	}
	// ★ Режим AsIs (ревизия О4 МЕЛК-3): `OnRecv_ItemUpdate@0xD0CC20` пишет запись `memcpy` без проверки количества; «−1 ⇒ очистить» —
	//   контракт 6166/6582 (`invHandleItemSlotSyncPacket`), его остальные вызывающие сохраняют (Default).
	if (Amount == -1 && Mode != EExternalRecordMode::AsIs) { ClearCell(Cont, Slot); }
	else { PutRecord(Rec57.GetData(), Cont); }
	if (bWindowOpen) { RefreshGrid(); RefreshEquip(); }
	UE_LOG(LogAsdaInven, Log, TEXT("[инвентарь] внешняя запись применена: контейнер=%d слот=%d код=%d кол-во=%d"),
		Cont, Slot, RdI32(Rec57.GetData() + 0), Amount);
	return true;
}

// ★ Волна 11 заточки: иконка надетого слота — цель симклика самотеста (объявление и смысл — в заголовке).
UWidget* UAsdaInventorySubsystem::DebugGetEquipSlotIcon(int32 Slot) const
{
	return EquipUi.IsValidIndex(Slot) ? EquipUi[Slot].Icon.Get() : nullptr;
}

// Иконка ячейки сетки видимой страницы (самотест замка ввода модалкой — настоящий ПКМ по ячейке).
UWidget* UAsdaInventorySubsystem::DebugGetGridIcon(int32 C) const
{
	return GridUi.IsValidIndex(C) ? GridUi[C].Icon.Get() : nullptr;
}

UWidget* UAsdaInventorySubsystem::DebugGetTrashButton() const
{
	return BtnTrash.Get();
}

bool UAsdaInventorySubsystem::DebugPutItem(int32 ContainerId, int32 CellIndex, int32 ItemCode, int32 Amount)
{
	TArray<FAsdaItemInstance>* C = Container(ContainerId);
	if (!C || !C->IsValidIndex(CellIndex)) { return false; }
	FAsdaItemInstance& I = (*C)[CellIndex];
	I.Clear();
	if (ItemCode > 0)
	{
		WrI32(I.Raw.GetData() + 0, ItemCode);
		I.Raw[4] = (uint8)ContainerId;
		WrI16(I.Raw.GetData() + 5, CellIndex);
		WrI32(I.Raw.GetData() + 9, FMath::Max(1, Amount));
		I.SyncFromRaw();
	}
	else
	{
		I.SetContainerType((uint8)ContainerId);
		I.SetSlotIndex(CellIndex);
	}
	NotifyCell(ContainerId, CellIndex);
	// ★ 06.09.2026 (переименование корейских MIC): контейнер 3 = НАДЕТОЕ, и его ячейка = слот части
	//   модели (`ItemRecord->itemType`, см. `ApplyEquipReply`). Локальная подкладка в 3-й контейнер
	//   раньше меняла только сетку окна, а модель героя — нет; для офлайн-проверки «часть экипа не
	//   серая» (-game без сервера) толкаем её в ту же ОБЩУЮ точку, что и ответ 5001. Только 0..4/8/9 —
	//   остальные слоты модель не трогают (`ApplySelfEquipPart` их сам отсекает).
	if (ContainerId == 3)
	{
		UGameInstance* GI = GetGameInstance();
		if (UAsdaWorldSubsystem* WS = GI ? GI->GetSubsystem<UAsdaWorldSubsystem>() : nullptr)
		{
			WS->ApplySelfEquipPart(CellIndex, ItemCode > 0 ? ItemCode : 0, 0);
		}
	}
	if (bWindowOpen) { RefreshGrid(); RefreshEquip(); }
	RefreshMoney();
	return true;
}

// ══════════════════════════════ КОНСОЛЬНЫЕ КОМАНДЫ ═════════════════════════════════════════════
//   Юзер запускает их В ИГРЕ и лог не читает — каждая строка уходит и в лог, и в системный журнал.

static UAsdaInventorySubsystem* InvOf(UWorld* W)
{
	return (W && W->GetGameInstance()) ? W->GetGameInstance()->GetSubsystem<UAsdaInventorySubsystem>() : nullptr;
}

// ★ U13 (В1 дедупа №2): тело — общая точка `AsdaNotice::ConsoleEcho` (прежняя копия была дословной, менялась только категория лога).
#if !UE_BUILD_SHIPPING   // ★ античит А2 (12.10.2026): отладка самотестов/дев-команд — только вне Shipping
static void InvEcho(UWorld* W, const FString& Text)
{
	AsdaNotice::ConsoleEcho(W, Text, LogAsdaInven);
}
#endif // !UE_BUILD_SHIPPING

#if ASDA_DEV_CONSOLE   // ★ античит А2: дев-команда, в Shipping не регистрируется
static FAutoConsoleCommandWithWorldAndArgs GInvOpen(
	TEXT("asda.Inven.Open"),
	TEXT("asda.Inven.Open [0|1] — открыть/закрыть окно инвентаря (без аргумента — переключить). "
	     "Тот же путь, что кнопка меню 73402."),
	FConsoleCommandWithWorldAndArgsDelegate::CreateLambda([](const TArray<FString>& A, UWorld* W)
	{
		UAsdaInventorySubsystem* Inv = InvOf(W);
		if (!Inv) { return; }
		if (A.Num() < 1) { Inv->ToggleWindow(); }
		else if (FCString::Atoi(*A[0]) != 0) { Inv->OpenWindow(); }
		else { Inv->CloseWindow(); }
		InvEcho(W, FString::Printf(TEXT("инвентарь: %s"), Inv->IsWindowOpen() ? TEXT("открыт") : TEXT("закрыт")));
	}));
#endif // ASDA_DEV_CONSOLE

#if ASDA_DEV_CONSOLE   // ★ античит А2: дев-команда, в Shipping не регистрируется
static FAutoConsoleCommandWithWorldAndArgs GInvDump(
	TEXT("asda.Inven.Dump"),
	TEXT("Инвентарь: дамп КАЖДОГО разобранного контрола окна INVEN_WIN"),
	FConsoleCommandWithWorldAndArgsDelegate::CreateLambda([](const TArray<FString>&, UWorld* W)
	{
		if (UAsdaInventorySubsystem* Inv = InvOf(W)) { InvEcho(W, Inv->DumpControls()); }
	}));
#endif // ASDA_DEV_CONSOLE

#if ASDA_DEV_CONSOLE   // ★ античит А2: дев-команда, в Shipping не регистрируется
static FAutoConsoleCommandWithWorldAndArgs GInvModel(
	TEXT("asda.Inven.Model"),
	TEXT("Инвентарь: дамп модели (контейнеры 1/2/3/5, золото, флаг ожидания)"),
	FConsoleCommandWithWorldAndArgsDelegate::CreateLambda([](const TArray<FString>&, UWorld* W)
	{
		if (UAsdaInventorySubsystem* Inv = InvOf(W)) { InvEcho(W, Inv->DumpModel()); }
	}));
#endif // ASDA_DEV_CONSOLE

// ★ Диагностика иконок: показывает, ИЗ КАКОЙ таблицы взялась иконка кода и какая это ячейка атласа.
//   Нужна была, чтобы поймать «прозрачные предметы на вкладке Снаряжение» (записи second-таблицы).
#if ASDA_DEV_CONSOLE   // ★ античит А2: дев-команда, в Shipping не регистрируется
static FAutoConsoleCommandWithWorldAndArgs GInvIcon(
	TEXT("asda.Inven.IconInfo"),
	TEXT("asda.Inven.IconInfo <itemCode> — из какой таблицы иконка (item_list / sowell_list / "
	     "special_itemlist), имя атласа, ячейка и размер"),
	FConsoleCommandWithWorldAndArgsDelegate::CreateLambda([](const TArray<FString>& A, UWorld* W)
	{
		UGameInstance* GI = W ? W->GetGameInstance() : nullptr;
		UAsdaItemTableSubsystem* IT = GI ? GI->GetSubsystem<UAsdaItemTableSubsystem>() : nullptr;
		if (!IT || A.Num() < 1) { InvEcho(W, TEXT("использование: asda.Inven.IconInfo <itemCode>")); return; }
		const int32 Code = FCString::Atoi(*A[0]);
		const FAsdaItemIcon Ic = IT->GetItemIcon(Code, true);
		const TCHAR* Src = IT->IsSowelCode(Code) ? TEXT("sowell_list")
			: (IT->IsSpecialItemCode(Code) ? TEXT("special_itemlist (Table2)") : TEXT("item_list"));
		InvEcho(W, FString::Printf(TEXT("код %d: таблица=%s, категория=%d, имя='%s', атлас='%s' ячейка=(%d,%d) размер=%d"),
			Code, Src, IT->GetItemCategory(Code), *IT->GetItemName(Code),
			Ic.IsValid() ? *Ic.Atlas : TEXT("—"), Ic.CellX, Ic.CellY, Ic.CellSize));
	}));
#endif // ASDA_DEV_CONSOLE

#if ASDA_DEV_CONSOLE   // ★ античит А2: дев-команда, в Shipping не регистрируется
static FAutoConsoleCommandWithWorldAndArgs GInvPut(
	TEXT("asda.Inven.Put"),
	TEXT("asda.Inven.Put <контейнер 1|2|3|5> <ячейка> <itemCode> [кол-во] — положить предмет в ячейку "
	     "ЛОКАЛЬНО (без сервера): проверка отрисовки, моста хотбара и формул сетки"),
	FConsoleCommandWithWorldAndArgsDelegate::CreateLambda([](const TArray<FString>& A, UWorld* W)
	{
		// ★ С-Р1 (26.09.2026), офлайн-гейт: поддельная запись ячейки — следующее действие с ней ушло бы с поддельной записью (5000/5028/5006).
		if (FAsdaOutgoingGate::RefuseDebugWhenOnline(W, TEXT("asda.Inven.Put"))) { return; }
		UAsdaInventorySubsystem* Inv = InvOf(W);
		if (!Inv || A.Num() < 3)
		{
			InvEcho(W, TEXT("использование: asda.Inven.Put <контейнер> <ячейка> <itemCode> [кол-во]"));
			return;
		}
		const bool bOk = Inv->DebugPutItem(FCString::Atoi(*A[0]), FCString::Atoi(*A[1]),
			FCString::Atoi(*A[2]), A.Num() > 3 ? FCString::Atoi(*A[3]) : 1);
		InvEcho(W, bOk ? TEXT("положено") : TEXT("отказ: контейнер/ячейка вне диапазона"));
	}));
#endif // ASDA_DEV_CONSOLE

#if ASDA_DEV_CONSOLE   // ★ античит А2: дев-команда, в Shipping не регистрируется
static FAutoConsoleCommandWithWorldAndArgs GInvTab(
	TEXT("asda.Inven.Tab"),
	TEXT("asda.Inven.Tab <0|1> — переключатель 402: 0 = «Расходники» (контейнер 2, 60), "
	     "1 = «Снаряжение» (контейнер 1, 60; при asda.Inven.ShellX2 0 — 30)"),
	FConsoleCommandWithWorldAndArgsDelegate::CreateLambda([](const TArray<FString>& A, UWorld* W)
	{
		UAsdaInventorySubsystem* Inv = InvOf(W);
		if (!Inv || A.Num() < 1) { InvEcho(W, TEXT("использование: asda.Inven.Tab <0|1>")); return; }
		Inv->SetTab(FCString::Atoi(*A[0]));
		InvEcho(W, FString::Printf(TEXT("вкладка %d, контейнер %d"), Inv->GetTab(), Inv->GetGridContainer()));
	}));
#endif // ASDA_DEV_CONSOLE

#if ASDA_DEV_CONSOLE   // ★ античит А2: дев-команда, в Shipping не регистрируется
static FAutoConsoleCommandWithWorldAndArgs GInvScroll(
	TEXT("asda.Inven.Scroll"),
	TEXT("asda.Inven.Scroll <дельта в РЯДАХ> — прокрутка сетки (эталонная «страница» = сдвиг на 6 ячеек)"),
	FConsoleCommandWithWorldAndArgsDelegate::CreateLambda([](const TArray<FString>& A, UWorld* W)
	{
		UAsdaInventorySubsystem* Inv = InvOf(W);
		if (!Inv || A.Num() < 1) { InvEcho(W, TEXT("использование: asda.Inven.Scroll <дельта>")); return; }
		Inv->ScrollBy(FCString::Atoi(*A[0]));
		InvEcho(W, FString::Printf(TEXT("ряд %d из %d"), Inv->GetScrollRow(), Inv->GetMaxScrollRow()));
	}));
#endif // ASDA_DEV_CONSOLE

#if ASDA_DEV_CONSOLE   // ★ античит А2: дев-команда, в Shipping не регистрируется
static FAutoConsoleCommandWithWorldAndArgs GInvScrollBar(
	TEXT("asda.Inven.ScrollBar"),
	TEXT("asda.Inven.ScrollBar [ряд] — состояние ПОЛОСЫ прокрутки (планы скина `Nomal_Scr`, диапазон, "
	     "бегунок, геометрия). С аргументом — поставить ряд напрямую (эталонный uiScrollbarSetPos)."),
	FConsoleCommandWithWorldAndArgsDelegate::CreateLambda([](const TArray<FString>& A, UWorld* W)
	{
		UAsdaInventorySubsystem* Inv = InvOf(W);
		if (!Inv) { return; }
		if (A.Num() >= 1) { Inv->SetScrollRow(FCString::Atoi(*A[0])); }
		InvEcho(W, Inv->DumpScrollBar());
	}));
#endif // ASDA_DEV_CONSOLE

#if ASDA_DEV_CONSOLE   // ★ античит А2: дев-команда, в Shipping не регистрируется
static FAutoConsoleCommandWithWorldAndArgs GInvEquip(
	TEXT("asda.Inven.Equip"),
	TEXT("asda.Inven.Equip <контейнер 1|2> <ячейка> — надеть предмет (5000, dstType = 3, "
	     "dstSlot = ItemRecord->itemType)"),
	FConsoleCommandWithWorldAndArgsDelegate::CreateLambda([](const TArray<FString>& A, UWorld* W)
	{
		UAsdaInventorySubsystem* Inv = InvOf(W);
		if (!Inv || A.Num() < 2) { InvEcho(W, TEXT("использование: asda.Inven.Equip <контейнер> <ячейка>")); return; }
		const bool bOk = Inv->EquipItem(FCString::Atoi(*A[0]), FCString::Atoi(*A[1]));
		InvEcho(W, bOk ? TEXT("5000 отправлен (ждём 5001)") : TEXT("отказ"));
	}));
#endif // ASDA_DEV_CONSOLE

#if ASDA_DEV_CONSOLE   // ★ античит А2: дев-команда, в Shipping не регистрируется
static FAutoConsoleCommandWithWorldAndArgs GInvUnequip(
	TEXT("asda.Inven.Unequip"),
	TEXT("asda.Inven.Unequip <слот 0..10> — снять надетое (5000, srcType = 3; dstType = 2 для слотов "
	     "5/6/7 или flagWord176, иначе 1)"),
	FConsoleCommandWithWorldAndArgsDelegate::CreateLambda([](const TArray<FString>& A, UWorld* W)
	{
		UAsdaInventorySubsystem* Inv = InvOf(W);
		if (!Inv || A.Num() < 1) { InvEcho(W, TEXT("использование: asda.Inven.Unequip <слот>")); return; }
		const bool bOk = Inv->UnequipItem(FCString::Atoi(*A[0]));
		InvEcho(W, bOk ? TEXT("5000 отправлен (ждём 5001)") : TEXT("отказ"));
	}));
#endif // ASDA_DEV_CONSOLE

#if ASDA_DEV_CONSOLE   // ★ античит А2: дев-команда, в Shipping не регистрируется
static FAutoConsoleCommandWithWorldAndArgs GInvMove(
	TEXT("asda.Inven.Move"),
	TEXT("asda.Inven.Move <контейнер> <откуда> <куда> — перестановка внутри контейнера (5000). "
	     "Между контейнерами 1↔2 сервер перенос ЗАПРЕЩАЕТ."),
	FConsoleCommandWithWorldAndArgsDelegate::CreateLambda([](const TArray<FString>& A, UWorld* W)
	{
		UAsdaInventorySubsystem* Inv = InvOf(W);
		if (!Inv || A.Num() < 3) { InvEcho(W, TEXT("использование: asda.Inven.Move <контейнер> <откуда> <куда>")); return; }
		const int32 C = FCString::Atoi(*A[0]);
		const bool bOk = Inv->MoveItem(C, FCString::Atoi(*A[1]), C, FCString::Atoi(*A[2]));
		InvEcho(W, bOk ? TEXT("5000 отправлен (ждём 5001)") : TEXT("отказ"));
	}));
#endif // ASDA_DEV_CONSOLE

#if ASDA_DEV_CONSOLE   // ★ античит А2: дев-команда, в Shipping не регистрируется
static FAutoConsoleCommandWithWorldAndArgs GInvUse(
	TEXT("asda.Inven.Use"),
	TEXT("asda.Inven.Use <контейнер> <ячейка> — двойной клик: расходник → 5028, иначе 5000 dstType=3"),
	FConsoleCommandWithWorldAndArgsDelegate::CreateLambda([](const TArray<FString>& A, UWorld* W)
	{
		UAsdaInventorySubsystem* Inv = InvOf(W);
		if (!Inv || A.Num() < 2) { InvEcho(W, TEXT("использование: asda.Inven.Use <контейнер> <ячейка>")); return; }
		const bool bOk = Inv->UseByDoubleClick(FCString::Atoi(*A[0]), FCString::Atoi(*A[1]));
		InvEcho(W, bOk ? TEXT("пакет отправлен") : TEXT("отказ"));
	}));
#endif // ASDA_DEV_CONSOLE

// ★★★ ОФЛАЙН-ЭМУЛЯЦИЯ ОТВЕТА 5029 — проверка всей волны 1 БЕЗ СЕРВЕРА. Собирает ровно те 24 байта,
//   которые кладёт `CharSpendItemUse@0x4D8C40` (GameServer.c:86176-86186), и отдаёт их штатным путём
//   (колбэк → очередь → `ApplyUseResult` в Tick), то есть проверяется и закон no-reentrant-net-callbacks.
//   Чужое применение проверяется подстановкой ЧУЖОГО userNum: должен появиться только эффект и звук.
#if ASDA_DEV_CONSOLE   // ★ античит А2: дев-команда, в Shipping не регистрируется
static FAutoConsoleCommandWithWorldAndArgs GInvRecv5029(
	TEXT("asda.Inven.Recv5029"),
	TEXT("asda.Inven.Recv5029 <результат> <itemCode> <ячейка> [контейнер=2] [остаток=1] [весСтопки=0] "
	     "[весПерсонажа=0] [userNum=свой] — эмулировать приход 5029 (сервер не нужен). "
	     "Примеры: 1 20571 0 2 4 40 1200 (зелье здоровья, linkCode 200, откат 5000 мс); "
	     "3 20571 0 (отказ «жди отката», нотис 9565)"),
	FConsoleCommandWithWorldAndArgsDelegate::CreateLambda([](const TArray<FString>& A, UWorld* W)
	{
		// ★ С-Р1 (26.09.2026), офлайн-гейт: поддельный 5029: при result=11 боевой приёмник шлёт 4008.
		static const uint16 kGuardOps[] = { 4008 };
		if (FAsdaOutgoingGate::RefuseDebugWhenOnline(W, TEXT("asda.Inven.Recv5029"), 5.f, MakeArrayView(kGuardOps))) { return; }
		UAsdaInventorySubsystem* Inv = InvOf(W);
		if (!Inv || A.Num() < 3)
		{
			InvEcho(W, TEXT("использование: asda.Inven.Recv5029 <результат> <itemCode> <ячейка> [контейнер] [остаток] [весСтопки] [весПерсонажа] [userNum]"));
			return;
		}
		UGameInstance* GI = W ? W->GetGameInstance() : nullptr;
		UAsdaNetSubsystem* Net = GI ? GI->GetSubsystem<UAsdaNetSubsystem>() : nullptr;
		const int32 SelfNum = Net ? (int32)Net->SelfUserNum : 0;

		const int32 Res   = FCString::Atoi(*A[0]);
		const int32 Code  = FCString::Atoi(*A[1]);
		const int32 Slot  = FCString::Atoi(*A[2]);
		const int32 Cont  = (A.Num() > 3) ? FCString::Atoi(*A[3]) : 2;
		const int32 Cnt   = (A.Num() > 4) ? FCString::Atoi(*A[4]) : 1;
		const int32 StkW  = (A.Num() > 5) ? FCString::Atoi(*A[5]) : 0;
		const int32 ChrW  = (A.Num() > 6) ? FCString::Atoi(*A[6]) : 0;
		const int32 UNum  = (A.Num() > 7) ? FCString::Atoi(*A[7]) : SelfNum;

		TArray<uint8> B;
		B.SetNumZeroed(24);
		auto PutI16 = [&B](int32 Off, int32 V) { B[Off] = (uint8)(V & 0xFF); B[Off + 1] = (uint8)((V >> 8) & 0xFF); };
		auto PutI32 = [&B](int32 Off, int32 V)
		{
			B[Off]     = (uint8)(V & 0xFF);        B[Off + 1] = (uint8)((V >> 8)  & 0xFF);
			B[Off + 2] = (uint8)((V >> 16) & 0xFF); B[Off + 3] = (uint8)((V >> 24) & 0xFF);
		};
		B[0] = (uint8)Res;          // +0  результат
		PutI16(1,  UNum);           // +1  userNum ЦЕЛИ (НЕ charId)
		PutI32(3,  0);              // +3  charId — клиент его не читает
		PutI32(7,  Code);           // +7  itemCode
		PutI16(11, Slot);           // +11 ячейка
		PutI16(13, 0);              // +13 резерв
		B[15] = (uint8)Cont;        // +15 контейнер
		PutI32(16, Cnt);            // +16 новое количество в стопке
		PutI16(20, StkW);           // +20 вес стопки
		PutI16(22, ChrW);           // +22 вес персонажа
		Inv->SimulateUseResult(B);
		InvEcho(W, FString::Printf(
			TEXT("5029 подан: результат=%d код=%d %d:%d остаток=%d весСтопки=%d весПерсонажа=%d userNum=%d%s"),
			Res, Code, Cont, Slot, Cnt, StkW, ChrW, UNum,
			(UNum == SelfNum) ? TEXT(" (СВОЙ)") : TEXT(" (СОСЕДА — ждём только эффект и звук)")));
	}));
#endif // ASDA_DEV_CONSOLE

// ═══════════ ★★★ ВОЛНА 2 — ОФЛАЙН-ЭМУЛЯЦИИ И ОТЛАДКА (сервер не нужен) ══════════════════════════

// Состав коробки по коду: проверка связки `DurationOrEnchant → packagetable` без сервера.
#if ASDA_DEV_CONSOLE   // ★ античит А2: дев-команда, в Shipping не регистрируется
static FAutoConsoleCommandWithWorldAndArgs GInvPackDump(
	TEXT("asda.Inven.Package"),
	TEXT("asda.Inven.Package <itemCode> — показать состав коробки/пакета (строка packagetable по "
	     "DurationOrEnchant) и подобранные слоты-приёмники. Пример: asda.Inven.Package 20816"),
	FConsoleCommandWithWorldAndArgsDelegate::CreateLambda([](const TArray<FString>& A, UWorld* W)
	{
		UAsdaInventorySubsystem* Inv = InvOf(W);
		UGameInstance* GI = W ? W->GetGameInstance() : nullptr;
		UAsdaItemTableSubsystem* IT = GI ? GI->GetSubsystem<UAsdaItemTableSubsystem>() : nullptr;
		if (!Inv || !IT || A.Num() < 1) { InvEcho(W, TEXT("использование: asda.Inven.Package <itemCode>")); return; }
		const int32 Code = FCString::Atoi(*A[0]);
		if (!IT->IsPackageItem(Code)) { InvEcho(W, FString::Printf(TEXT("предмет %d не коробка"), Code)); return; }
		TArray<FAsdaPackageEntry> C;
		IT->GetPackageContents(Code, C);
		for (int32 i = 0; i < C.Num(); ++i)
		{
			InvEcho(W, FString::Printf(TEXT("  %d) %d «%s» ×%d"),
				i + 1, C[i].ItemCode, *IT->GetItemName(C[i].ItemCode), C[i].Amount));
		}
		TArray<uint8> Slots; int32 Cnt = 0, ShortS = 0, ShortB = 0;
		Inv->ReservePackageSlots(Code, Slots, Cnt, ShortS, ShortB);
		FString S;
		for (int32 i = 0; i < 5; ++i)
		{
			S += FString::Printf(TEXT("[%d:%d] "), (int32)Slots[3 * i],
				(int32)(int16)(Slots[3 * i + 1] | (Slots[3 * i + 2] << 8)));
		}
		InvEcho(W, FString::Printf(TEXT("слоты-приёмники: %s(позиций %d, не хватает: расходники %d, сумка %d)"),
			*S, Cnt, ShortS, ShortB));
	}));
#endif // ASDA_DEV_CONSOLE

// Эмуляция 6097: собирает те же 300 байт, что кладёт `sub_55B1C0@0x55B1C0`.
//   Записи выданных предметов делаются минимальными (код/контейнер/слот/количество/вес) — этого
//   достаточно, чтобы предмет реально лёг в ячейку тем же путём, что при живом сервере.
#if ASDA_DEV_CONSOLE   // ★ античит А2: дев-команда, в Shipping не регистрируется
static FAutoConsoleCommandWithWorldAndArgs GInvRecv6097(
	TEXT("asda.Inven.Recv6097"),
	TEXT("asda.Inven.Recv6097 <результат> <контейнерКоробки> <слотКоробки> [весПерсонажа=0] "
	     "[код1 контейнер1 слот1] … до пяти троек — эмулировать ответ на вскрытие коробки. "
	     "Примеры: 1 2 0 1200 46759 1 0 46760 1 1 ; 8 2 0 (отказ «ошибка индекса пакета», нотис 99318)"),
	FConsoleCommandWithWorldAndArgsDelegate::CreateLambda([](const TArray<FString>& A, UWorld* W)
	{
		// ★ С-Р1 (26.09.2026), офлайн-гейт: поддельный 6097: анонс редкой добычи 6616 ушёл бы ВСЕМ игрокам карты с произвольным кодом.
		static const uint16 kGuardOps[] = { 6616, 6129 };
		if (FAsdaOutgoingGate::RefuseDebugWhenOnline(W, TEXT("asda.Inven.Recv6097"), 5.f, MakeArrayView(kGuardOps))) { return; }
		UAsdaInventorySubsystem* Inv = InvOf(W);
		if (!Inv || A.Num() < 3) { InvEcho(W, TEXT("использование: asda.Inven.Recv6097 <результат> <контейнер> <слот> [вес] [код конт слот]…")); return; }
		UGameInstance* GI = W ? W->GetGameInstance() : nullptr;
		UAsdaNetSubsystem* Net = GI ? GI->GetSubsystem<UAsdaNetSubsystem>() : nullptr;
		TArray<uint8> B; B.SetNumZeroed(300);
		auto P16 = [&B](int32 O, int32 V) { AsdaBytes::WrI16(&B[O], V); };
		auto P32 = [&B](int32 O, int32 V) { AsdaBytes::WrI32(&B[O], V); };
		P16(0, Net ? (int32)Net->SelfUserNum : 0);
		P32(2, Net ? Net->SelectedCharId : 0);
		B[7] = (uint8)FCString::Atoi(*A[0]);
		B[8] = (uint8)FCString::Atoi(*A[1]);
		P16(9, FCString::Atoi(*A[2]));
		P32(296, (A.Num() > 3) ? FCString::Atoi(*A[3]) : 0);
		int32 Given = 0;
		for (int32 i = 0; i < 5; ++i)
		{
			const int32 Base = 4 + 3 * i;
			if (A.Num() < Base + 3) { break; }
			uint8* Rec = B.GetData() + 11 + 57 * i;
			AsdaBytes::WrI32(Rec + 0, FCString::Atoi(*A[Base]));
			Rec[4] = (uint8)FCString::Atoi(*A[Base + 1]);
			AsdaBytes::WrI16(Rec + 5, FCString::Atoi(*A[Base + 2]));
			AsdaBytes::WrI16(Rec + 7, -1);
			AsdaBytes::WrI32(Rec + 9, 1);     // количество
			++Given;
		}
		B[6] = (uint8)Given;
		Inv->SimulatePackageResult(B);
		InvEcho(W, FString::Printf(TEXT("6097 подан: результат=%s, выдано записей %d"), *A[0], Given));
	}));
#endif // ASDA_DEV_CONSOLE

// Эмуляция 5469 (гача). ⚠ Успех у этого канала — код 0, а не 1.
#if ASDA_DEV_CONSOLE   // ★ античит А2: дев-команда, в Shipping не регистрируется
static FAutoConsoleCommandWithWorldAndArgs GInvRecv5469(
	TEXT("asda.Inven.Recv5469"),
	TEXT("asda.Inven.Recv5469 <результат 0=успех> <редкий 0/1> <кодГачи> <контейнер> <слот> "
	     "<кодПриза> [слотПриза=0] [весПерсонажа=0] — эмулировать результат гачи. "
	     "Пример: asda.Inven.Recv5469 0 1 20932 2 3 46759 5 1200"),
	FConsoleCommandWithWorldAndArgsDelegate::CreateLambda([](const TArray<FString>& A, UWorld* W)
	{
		// ★ С-Р1 (26.09.2026), офлайн-гейт: поддельный 5469: приз и эффект удачи 5474 (+ анонс 6616) ушли бы на сервер.
		static const uint16 kGuardOps[] = { 5474, 6616, 6129 };
		if (FAsdaOutgoingGate::RefuseDebugWhenOnline(W, TEXT("asda.Inven.Recv5469"), 5.f, MakeArrayView(kGuardOps))) { return; }
		UAsdaInventorySubsystem* Inv = InvOf(W);
		if (!Inv || A.Num() < 6) { InvEcho(W, TEXT("использование: asda.Inven.Recv5469 <результат> <редкий> <кодГачи> <контейнер> <слот> <кодПриза> [слотПриза] [вес]")); return; }
		TArray<uint8> B; B.SetNumZeroed(68);
		B[0] = (uint8)FCString::Atoi(*A[0]);
		B[1] = (uint8)FCString::Atoi(*A[1]);
		AsdaBytes::WrI32(&B[2], FCString::Atoi(*A[2]));
		B[6] = (uint8)FCString::Atoi(*A[3]);
		AsdaBytes::WrI16(&B[7], FCString::Atoi(*A[4]));
		uint8* Prize = B.GetData() + 9;
		AsdaBytes::WrI32(Prize + 0, FCString::Atoi(*A[5]));
		Prize[4] = 1;                                                       // приз ложится в СУМКУ
		AsdaBytes::WrI16(Prize + 5, (A.Num() > 6) ? FCString::Atoi(*A[6]) : 0);
		AsdaBytes::WrI16(Prize + 7, -1);
		AsdaBytes::WrI32(Prize + 9, 1);
		AsdaBytes::WrI16(&B[66], (A.Num() > 7) ? FCString::Atoi(*A[7]) : 0);
		Inv->SimulateGachaResult(B);
		InvEcho(W, FString::Printf(TEXT("5469 подан: результат=%s редкий=%s приз=%s"), *A[0], *A[1], *A[5]));
	}));
#endif // ASDA_DEV_CONSOLE

// Эмуляция 5451 (премиум). Успех = 1; вес правится только при `special_itemlist.SubType == 51`.
#if ASDA_DEV_CONSOLE   // ★ античит А2: дев-команда, в Shipping не регистрируется
static FAutoConsoleCommandWithWorldAndArgs GInvRecv5451(
	TEXT("asda.Inven.Recv5451"),
	TEXT("asda.Inven.Recv5451 <результат> <кодПремиум> [контейнер=1] [слот=0] [вес=0] [максВес=0] — "
	     "эмулировать ответ на применение премиум-предмета. Примеры: 1 461 1 0 ; 15 461 (нотис 12848)"),
	FConsoleCommandWithWorldAndArgsDelegate::CreateLambda([](const TArray<FString>& A, UWorld* W)
	{
		// ★ С-Р1 (26.09.2026), офлайн-гейт: поддельный 5451: код 0x16/0x17 шлёт тумблер 5455 с кодом из аргументов.
		static const uint16 kGuardOps[] = { 5455 };
		if (FAsdaOutgoingGate::RefuseDebugWhenOnline(W, TEXT("asda.Inven.Recv5451"), 5.f, MakeArrayView(kGuardOps))) { return; }
		UAsdaInventorySubsystem* Inv = InvOf(W);
		if (!Inv || A.Num() < 2) { InvEcho(W, TEXT("использование: asda.Inven.Recv5451 <результат> <код> [контейнер] [слот] [вес] [максВес]")); return; }
		UGameInstance* GI = W ? W->GetGameInstance() : nullptr;
		UAsdaNetSubsystem* Net = GI ? GI->GetSubsystem<UAsdaNetSubsystem>() : nullptr;
		TArray<uint8> B; B.SetNumZeroed(68);
		B[0] = (uint8)FCString::Atoi(*A[0]);
		AsdaBytes::WrI16(&B[1], Net ? (int32)Net->SelfUserNum : 0);
		uint8* Rec = B.GetData() + 3;
		AsdaBytes::WrI32(Rec + 0, FCString::Atoi(*A[1]));
		Rec[4] = (uint8)((A.Num() > 2) ? FCString::Atoi(*A[2]) : 1);
		AsdaBytes::WrI16(Rec + 5, (A.Num() > 3) ? FCString::Atoi(*A[3]) : 0);
		AsdaBytes::WrI16(Rec + 7, -1);
		AsdaBytes::WrI32(Rec + 9, 1);
		AsdaBytes::WrI16(&B[60], (A.Num() > 4) ? FCString::Atoi(*A[4]) : 0);
		AsdaBytes::WrI16(&B[66], (A.Num() > 5) ? FCString::Atoi(*A[5]) : 0);
		Inv->SimulateFunctionResult(B);
		InvEcho(W, FString::Printf(TEXT("5451 подан: результат=%s код=%s"), *A[0], *A[1]));
	}));
#endif // ASDA_DEV_CONSOLE

// ★★★ 5456 — РАСШИРЕНИЕ СКЛАДА. Подаём НАСТОЯЩИЙ пакет через штатный приёмник сети, чтобы
//   проверялась и его ветка (12833 / 12834 / 32010), и модалка окна — а не только показ.
#if ASDA_DEV_CONSOLE   // ★ античит А2: дев-команда, в Shipping не регистрируется
static FAutoConsoleCommandWithWorldAndArgs GInvRecv5456(
	TEXT("asda.Inven.Recv5456"),
	TEXT("asda.Inven.Recv5456 <новаяЁмкостьВЯчейках> — эмулировать ответ сервера на предмет "
	     "«расширение хранилища» (special_itemlist.SubType == 34, коды 30/467/541). "
	     "Примеры при текущих 60: 90 → принято (нотис 12833), 30 → отказ (12834), "
	     "при 270 любое значение → 32010"),
	FConsoleCommandWithWorldAndArgsDelegate::CreateLambda([](const TArray<FString>& A, UWorld* W)
	{
		// ★ С-Р1 (26.09.2026), офлайн-гейт: поддельный 5456 помечает вкладку склада загруженной и меняет число ячеек — ломает живую сессию.
		if (FAsdaOutgoingGate::RefuseDebugWhenOnline(W, TEXT("asda.Inven.Recv5456"))) { return; }
		UGameInstance* GI = W ? W->GetGameInstance() : nullptr;
		UAsdaNetSubsystem* Net = GI ? GI->GetSubsystem<UAsdaNetSubsystem>() : nullptr;
		if (!Net || A.Num() < 1) { InvEcho(W, TEXT("использование: asda.Inven.Recv5456 <новаяЁмкость>")); return; }
		TArray<uint8> B; B.SetNumZeroed(2);
		AsdaBytes::WrI16(B.GetData(), FCString::Atoi(*A[0]));
		InvEcho(W, FString::Printf(TEXT("5456 подан: было %d ячеек, шлём %s"), Net->SelfStoreSlotsMax, *A[0]));
		Net->DebugFeedPacket(5456, B);
	}));
#endif // ASDA_DEV_CONSOLE

// ★ Прокрутить очередь пакетов вручную: в игре её крутит `Tick`, но у него гейт «мы в мире»,
//   а PIE без сервера в мир не входит. Поведение игры команда не меняет — это тестовый крючок.
#if ASDA_DEV_CONSOLE   // ★ античит А2: дев-команда, в Shipping не регистрируется
static FAutoConsoleCommandWithWorld GInvPump(
	TEXT("asda.Inven.Pump"),
	TEXT("asda.Inven.Pump — разобрать очередь пришедших пакетов сейчас (для PIE-проверок без сервера)"),
	FConsoleCommandWithWorldDelegate::CreateLambda([](UWorld* W)
	{
		// ★ С-Р1 (26.09.2026), офлайн-гейт: тестовый крюк разбирает очередь приёма в обход гейта «в мире» — вместе с Recv*-подделками шлёт 4008/5474/5455.
		if (FAsdaOutgoingGate::RefuseDebugWhenOnline(W, TEXT("asda.Inven.Pump"))) { return; }
		UAsdaInventorySubsystem* Inv = InvOf(W);
		if (!Inv) { return; }
		Inv->DebugPumpPendingPackets();
		InvEcho(W, TEXT("очередь разобрана"));
	}));
#endif // ASDA_DEV_CONSOLE

// ★ Выбрать ячейку кодом — та же ветка, что клик мышью (опросчик мыши в PIE без мира не тикает).
#if ASDA_DEV_CONSOLE   // ★ античит А2: дев-команда, в Shipping не регистрируется
static FAutoConsoleCommandWithWorldAndArgs GInvSelect(
	TEXT("asda.Inven.Select"),
	TEXT("asda.Inven.Select <контейнер 1|2|3> <ячейка> — выделить ячейку (эквивалент клика)"),
	FConsoleCommandWithWorldAndArgsDelegate::CreateLambda([](const TArray<FString>& A, UWorld* W)
	{
		UAsdaInventorySubsystem* Inv = InvOf(W);
		if (!Inv || A.Num() < 2) { InvEcho(W, TEXT("использование: asda.Inven.Select <контейнер> <ячейка>")); return; }
		Inv->DebugSelectCell(FCString::Atoi(*A[0]), FCString::Atoi(*A[1]));
		InvEcho(W, Inv->DumpSelection());
	}));
#endif // ASDA_DEV_CONSOLE

// ★★★ Диагностика выделения (дефект «выделение не сбрасывается при опустошении ячейки»).
#if ASDA_DEV_CONSOLE   // ★ античит А2: дев-команда, в Shipping не регистрируется
static FAutoConsoleCommandWithWorld GInvSelDump(
	TEXT("asda.Inven.SelDump"),
	TEXT("asda.Inven.SelDump — показать текущее выделение (ячейка сетки / надетый слот) и содержимое"),
	FConsoleCommandWithWorldDelegate::CreateLambda([](UWorld* W)
	{
		UAsdaInventorySubsystem* Inv = InvOf(W);
		if (!Inv) { return; }
		InvEcho(W, Inv->DumpSelection());
	}));
#endif // ASDA_DEV_CONSOLE

#if ASDA_DEV_CONSOLE   // ★ античит А2: дев-команда, в Shipping не регистрируется
static FAutoConsoleCommandWithWorldAndArgs GInvDestroy(
	TEXT("asda.Inven.Destroy"),
	TEXT("asda.Inven.Destroy <контейнер> <ячейка> — уничтожить предмет (5006), то же, что бросок в "
	     "корзину 405; по умолчанию спрашивает подтверждение (Д3)"),
	FConsoleCommandWithWorldAndArgsDelegate::CreateLambda([](const TArray<FString>& A, UWorld* W)
	{
		UAsdaInventorySubsystem* Inv = InvOf(W);
		if (!Inv || A.Num() < 2) { InvEcho(W, TEXT("использование: asda.Inven.Destroy <контейнер> <ячейка>")); return; }
		Inv->DestroyItem(FCString::Atoi(*A[0]), FCString::Atoi(*A[1]));
	}));
#endif // ASDA_DEV_CONSOLE

#if ASDA_DEV_CONSOLE   // ★ античит А2: дев-команда, в Shipping не регистрируется
static FAutoConsoleCommandWithWorldAndArgs GInvDrop(
	TEXT("asda.Inven.Drop"),
	TEXT("asda.Inven.Drop <контейнер> <ячейка> — выбросить на землю (5002). ⚠ В КЛИЕНТЕ ОРИГИНАЛА "
	     "отправителя этого пакета НЕТ (дефект §8.3), серверный обработчик CharItemDrop полный — "
	     "команда существует для проверки серверной ветки. На живом сервере — только с `online`: asda.Inven.Drop <к> <я> online"),
	FConsoleCommandWithWorldAndArgsDelegate::CreateLambda([](const TArray<FString>& A, UWorld* W)
	{
		// ★ С-Р1, редакция 1 (СРЕДН-4): отправителя 5002 в клиенте оригинала НЕТ — это отладка серверной ветки, а не действие
		//   игрока; предмет уходит на землю без подтверждения. Онлайн только с явным `online`.
		if (FAsdaOutgoingGate::RefuseRiskyWithoutOnlineOptIn(W, TEXT("asda.Inven.Drop"), A, TEXT("выброс предмета 5002 без подтверждения (в оригинале такого отправителя нет)"))) { return; }
		UAsdaInventorySubsystem* Inv = InvOf(W);
		if (!Inv || A.Num() < 2) { InvEcho(W, TEXT("использование: asda.Inven.Drop <контейнер> <ячейка> [online]")); return; }
		const bool bOk = Inv->DropItemToGround(FCString::Atoi(*A[0]), FCString::Atoi(*A[1]));
		InvEcho(W, bOk ? TEXT("5002 отправлен") : TEXT("отказ"));
	}));
#endif // ASDA_DEV_CONSOLE

#if ASDA_DEV_CONSOLE   // ★ античит А2: дев-команда, в Shipping не регистрируется
static FAutoConsoleCommandWithWorldAndArgs GInvWarehouse(
	TEXT("asda.Inven.Warehouse"),
	TEXT("asda.Inven.Warehouse <страница 1..N> — запросить страницу склада (4050 → 4051/4052); "
	     "окна склада ещё нет, содержимое ложится в модель контейнера 5 (см. asda.Inven.Model)"),
	FConsoleCommandWithWorldAndArgsDelegate::CreateLambda([](const TArray<FString>& A, UWorld* W)
	{
		UAsdaInventorySubsystem* Inv = InvOf(W);
		if (!Inv) { return; }
		Inv->RequestWarehousePage(A.Num() > 0 ? FCString::Atoi(*A[0]) : 1);
		InvEcho(W, TEXT("4050 отправлен"));
	}));
#endif // ASDA_DEV_CONSOLE

#if ASDA_DEV_CONSOLE   // ★ античит А2: дев-команда, в Shipping не регистрируется
static FAutoConsoleCommandWithWorldAndArgs GInvArrange(
	TEXT("asda.Inven.AutoArrange"),
	TEXT("asda.Inven.AutoArrange [0|1] — автосортировка ТЕКУЩЕЙ вкладки (эталон inventoryAutoArrange; "
	     "★ Д1: в оригинальной сборке эта кнопка была недостижима — включено решением юзера)"),
	FConsoleCommandWithWorldAndArgsDelegate::CreateLambda([](const TArray<FString>& A, UWorld* W)
	{
		UAsdaInventorySubsystem* Inv = InvOf(W);
		if (!Inv) { return; }
		Inv->AutoArrange(A.Num() > 0 ? FCString::Atoi(*A[0]) : 0);
	}));
#endif // ASDA_DEV_CONSOLE

// ★★★ Бросок предмета в панель быстрого доступа БЕЗ МЫШИ — та же точка, что и перетаскивание
//   (`AssignToHotbarSlot` = порт `hotbarAssignInventoryItem`), включая все гейты и отправку 5067.
#if ASDA_DEV_CONSOLE   // ★ античит А2: дев-команда, в Shipping не регистрируется
static FAutoConsoleCommandWithWorldAndArgs GInvToHotbar(
	TEXT("asda.Inven.ToHotbar"),
	TEXT("asda.Inven.ToHotbar <контейнер 1|2> <ячейка> <абс.слот панели 0..77> — положить предмет из "
	     "сумки в слот панели быстрого доступа (то же, что перетаскивание мышью): гейты эталона "
	     "(золото/самоцвет/слот оружия/флаг +0xB4), запись «контейнер+ячейка», сразу уходит 5067. "
	     "Абс.слот: основной ряд = колонка + 13×страница, T-ряд = колонка + 65"),
	FConsoleCommandWithWorldAndArgsDelegate::CreateLambda([](const TArray<FString>& A, UWorld* W)
	{
		UAsdaInventorySubsystem* Inv = InvOf(W);
		if (!Inv || A.Num() < 3)
		{
			InvEcho(W, TEXT("использование: asda.Inven.ToHotbar <контейнер> <ячейка> <абс.слот>"));
			return;
		}
		const bool bOk = Inv->AssignToHotbarSlot(FCString::Atoi(*A[0]), FCString::Atoi(*A[1]), FCString::Atoi(*A[2]));
		InvEcho(W, bOk ? TEXT("предмет положен в панель (5067 отправлен)")
		               : TEXT("отказ — причина в логе LogAsdaInven («[инвентарь→панель] отказ: …»)"));
	}));
#endif // ASDA_DEV_CONSOLE

#if ASDA_DEV_CONSOLE   // ★ античит А2: дев-команда, в Shipping не регистрируется
static FAutoConsoleCommandWithWorldAndArgs GInvRecv(
	TEXT("asda.Inven.RecvBatch"),
	TEXT("asda.Inven.RecvBatch <контейнер 1|2|5> [itemCode ячейка кол-во] ... — эмулировать приход "
	     "содержимого (4045/4048/4051 + маркер конца), сервер не нужен"),
	FConsoleCommandWithWorldAndArgsDelegate::CreateLambda([](const TArray<FString>& A, UWorld* W)
	{
		// ★ С-Р1 (26.09.2026), офлайн-гейт: поддельные записи сумки — изменённые слоты панели уехали бы со следующим 5067.
		if (FAsdaOutgoingGate::RefuseDebugWhenOnline(W, TEXT("asda.Inven.RecvBatch"))) { return; }
		UAsdaInventorySubsystem* Inv = InvOf(W);
		if (!Inv || A.Num() < 1)
		{
			InvEcho(W, TEXT("использование: asda.Inven.RecvBatch <контейнер> [itemCode ячейка кол-во] ..."));
			return;
		}
		const int32 Cont = FCString::Atoi(*A[0]);
		TArray<uint8> Raw;
		for (int32 i = 1; i + 2 < A.Num(); i += 3)
		{
			const int32 Base = Raw.Num();
			Raw.AddZeroed(57);
			uint8* R = Raw.GetData() + Base;
			const int32 Code = FCString::Atoi(*A[i]);
			const int32 Cell = FCString::Atoi(*A[i + 1]);
			const int32 Cnt  = FCString::Atoi(*A[i + 2]);
			R[0] = (uint8)(Code & 0xFF); R[1] = (uint8)((Code >> 8) & 0xFF);
			R[2] = (uint8)((Code >> 16) & 0xFF); R[3] = (uint8)((Code >> 24) & 0xFF);
			R[4] = (uint8)Cont;
			R[5] = (uint8)(Cell & 0xFF); R[6] = (uint8)((Cell >> 8) & 0xFF);
			R[9]  = (uint8)(Cnt & 0xFF); R[10] = (uint8)((Cnt >> 8) & 0xFF);
			R[11] = (uint8)((Cnt >> 16) & 0xFF); R[12] = (uint8)((Cnt >> 24) & 0xFF);
		}
		Inv->SimulateBatch(Cont, Raw);
		InvEcho(W, FString::Printf(TEXT("батч контейнера %d: %d записей поставлено в очередь (применится в Tick)"),
			Cont, Raw.Num() / 57));
	}));
#endif // ASDA_DEV_CONSOLE

// ★ 2026-09-25 (optgroup-sync, `Reports/gear_upgrade/optgroup_sync.md`). Как `asda.Inven.Put`, но с
//   ДОП-ОПЦИЯМИ гнёзд записи предмета: +29/+31 (гнездо 0: id параметра, значение) и +33/+35 (гнездо 1), i16.
//   Нужен, чтобы без сервера открыть окно 50300: оно показывает панель гнезда только при `+29+4j > 0`
//   (эталон `CItemTooltip_BuildOptionLines@0xB62C70`), а Put кладёт нули. Батч (RecvBatch) не годится:
//   его применяет Tick только «в мире» (есть LocalPlayerActor), а офлайн-карты этого не дают.
bool UAsdaInventorySubsystem::DebugPutItemWithOptions(int32 ContainerId, int32 CellIndex, int32 ItemCode,
	int32 Opt0, int32 Val0, int32 Opt1, int32 Val1)
{
	if (!DebugPutItem(ContainerId, CellIndex, ItemCode, 1) || ItemCode <= 0) { return false; }
	FAsdaItemInstance& I = (*Container(ContainerId))[CellIndex];
	WrI16(I.Raw.GetData() + 29, Opt0);   // гнездо 0: id параметра
	WrI16(I.Raw.GetData() + 31, Val0);   // гнездо 0: значение
	WrI16(I.Raw.GetData() + 33, Opt1);   // гнездо 1: id параметра
	WrI16(I.Raw.GetData() + 35, Val1);   // гнездо 1: значение
	I.SyncFromRaw();
	NotifyCell(ContainerId, CellIndex);
	if (bWindowOpen) { RefreshGrid(); RefreshEquip(); }
	return true;
}

#if ASDA_DEV_CONSOLE   // ★ античит А2: дев-команда, в Shipping не регистрируется
static FAutoConsoleCommandWithWorldAndArgs GInvPutOpt(
	TEXT("asda.Inven.PutOpt"),
	TEXT("asda.Inven.PutOpt <контейнер> <ячейка> <itemCode> <опция0> <знач0> <опция1> <знач1> — положить предмет "
	     "ЛОКАЛЬНО (без сервера) с доп-опциями гнёзд (+29/+31, +33/+35): проверка окна 50300"),
	FConsoleCommandWithWorldAndArgsDelegate::CreateLambda([](const TArray<FString>& A, UWorld* W)
	{
		// ★ С-Р1 (26.09.2026), офлайн-гейт: поддельная запись ячейки с опциями (см. asda.Inven.Put).
		if (FAsdaOutgoingGate::RefuseDebugWhenOnline(W, TEXT("asda.Inven.PutOpt"))) { return; }
		UAsdaInventorySubsystem* Inv = InvOf(W);
		if (!Inv || A.Num() < 7)
		{
			InvEcho(W, TEXT("использование: asda.Inven.PutOpt <контейнер> <ячейка> <itemCode> <опция0> <знач0> <опция1> <знач1>"));
			return;
		}
		const bool bOk = Inv->DebugPutItemWithOptions(FCString::Atoi(*A[0]), FCString::Atoi(*A[1]), FCString::Atoi(*A[2]),
			FCString::Atoi(*A[3]), FCString::Atoi(*A[4]), FCString::Atoi(*A[5]), FCString::Atoi(*A[6]));
		InvEcho(W, bOk ? TEXT("положено с опциями") : TEXT("отказ: контейнер/ячейка вне диапазона"));
	}));
#endif // ASDA_DEV_CONSOLE
