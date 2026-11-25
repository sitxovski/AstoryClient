// Copyright (c) 2026 @sitxovski (Discord)
// AsdaItemUpgradeSubsystem_Attempt.cpp — ★ R3-c 12.11.2026 (разрез AsdaItemUpgradeSubsystem.cpp, дословный перенос): клики, подтверждение, попытка и отправка, звуки/эффекты, ответ и записи, итог, мышь/подсказка, тексты.
//   Общее для частей — в AsdaItemUpgradeSubsystem_Private.h; шапка и реверс — в AsdaItemUpgradeSubsystem.cpp.
#include "UI/AsdaItemUpgradeSubsystem.h"
#include "UI/AsdaEnterGuard.h"   // ★ 25.10.2026: вопросы этого файла Enter не подтверждает (только щелчок)

#include "Text/AsdaTextSubsystem.h"

#include "AsdaItemUpgradeSubsystem_Private.h"   // ★ R3-c 12.11.2026: общее для частей AsdaItemUpgradeSubsystem*.cpp

static ASDA_DEV_CVAR(int32, CVarConfirmKeepTags,
	TEXT("asda.Upgrade.ConfirmKeepTags"), 1,
	TEXT("1 = подтверждение улучшения 110110/110111 с тегами цвета, как оригинал; 0 = теги срезаются (прежний порт)."),
	ECVF_Default);

static ASDA_DEV_CVAR(int32, CVarSuccessTextAllLevels,
	TEXT("asda.Upgrade.SuccessTextAllLevels"), 1,
	TEXT("Дефект оригинала Д-12а: 1 = текст успеха 50004 (чат + окно) на ЛЮБОЙ ступени; "
	     "0 = как оригинал — только при ступени <= 5."),
	ECVF_Default);

// ★ Д-12 (б) / П-5. Попап 19100 эталона показывается только при ступени 6…10
//   (`CItemEnhanceUI_ShowResultPopup@0xD2A470`, item_inventory.c:44375-44378), а сервер выдаёт и
//   усиливает доп-опцию на ступенях 6…20 (GameServer.c:99002-99035).
static ASDA_DEV_CVAR(int32, CVarOptionPopupMaxLevel,
	TEXT("asda.Upgrade.OptionPopupMaxLevel"), 20,
	TEXT("Дефект оригинала Д-12б: верхняя ступень, до которой показывается попап 19100 «получена опция». "
	     "20 = исправлено (сервер усиливает опцию до +20); 10 = как оригинал."),
	ECVF_Default);

// ★ Наше исправление (найдено при переносе): в ветке провала эталон показывает 110119 «предмет
//   уничтожен» по ЛЮБОЙ из четырёх записей ответа, у которой код 0 и контейнер 1 (network.c:19773-19790).
//   Помощники 97/98 и купоны subType 90 лежат в сумке (контейнер 1) — кончившаяся стопка помощника
//   даёт ЛОЖНОЕ «предмет уничтожен» при целом предмете.
static ASDA_DEV_CVAR(int32, CVarDestroyNoticeTargetOnly,
	TEXT("asda.Upgrade.DestroyNoticeTargetOnly"), 1,
	TEXT("1 = нотис 110119 «предмет уничтожен» только по записи УЛУЧШАЕМОГО предмета (первая запись 6505); "
	     "0 = как оригинал — по любой опустевшей записи сумки."),
	ECVF_Default);

// ★ Наше исправление (найдено при переносе, описание поправлено ревизией R-4): при отсутствии записи
//   опции (`+41 < 0`, дефект сервера Д-9) эталон `CItemEnhanceUI_ShowResultPopup` кладёт в 19105 ПОСЛЕДНЕЕ
//   содержимое `Buffer` — а последней в него писала строка подписи иконки 19103 `sprintf(Buffer, "+%d%%", +27)`.
//   То есть в строке опции дублируется «+N%» подписи иконки.
static ASDA_DEV_CVAR(int32, CVarPopupOptionNoDup,
	TEXT("asda.Upgrade.PopupOptionNoDup"), 1,
	TEXT("1 = если опции нет, строка 19105 попапа пуста; 0 = как оригинал (в 19105 повтор «+N%» подписи иконки 19103)."),
	ECVF_Default);

namespace {
	constexpr uint32 kColPopupOption   = (uint32)-103;       // 0xFFFFFF99 — строка опции попапа
	const FColor     kColResultChat(0x75, 0xDA, 0xFF, 0xFF); // −9053441 = 0xFF75DAFF — строка результата в чат

	// Длительности одноразовых эффектов из `JuiSkin.ini` (FrameInterval = полный прогон, мс) —
	//   запас, если план не отдал свою (нет данных анимации).
	float FxFallbackMs(EAsdaUpgradeFx Fx)
	{
		switch (Fx)
		{
		case EAsdaUpgradeFx::Effect1: return 1200.f;
		case EAsdaUpgradeFx::Success: return 900.f;
		case EAsdaUpgradeFx::FailAni: return 600.f;
		default:                      return 0.f;
		}
	}

	// IDNum контрола эффекта (для журнала волны 5 и логов).
	int32 FxIdNum(EAsdaUpgradeFx Fx)
	{
		switch (Fx)
		{
		case EAsdaUpgradeFx::Effect1: return 20070;
		case EAsdaUpgradeFx::Effect2: return 20071;
		case EAsdaUpgradeFx::Click:   return 20072;
		case EAsdaUpgradeFx::Success: return 20004;
		case EAsdaUpgradeFx::FailAni: return 20005;
		case EAsdaUpgradeFx::Fail:    return 20073;
		default:                      return 0;
		}
	}
}

// ═════════════════════════════ ДЕЙСТВИЯ ═══════════════════════════════════════════════════════
void UAsdaItemUpgradeSubsystem::OnGridCellClicked(int32 Cell)
{
	// `itemCombine_SelectSlot@0xD27930`: только при «свободен» (+3394).
	// ★ 29.10.2026 (инвентарь ×2): `Cell` — позиция СПИСКА `GridList` (видимая ячейка + окно прокрутки), до 60.
	if (!bIdle || Cell < 0 || Cell >= kShellCells) { return; }
	ShowFx(EAsdaUpgradeFx::Fail, false);   // 20073 «трещина» снимается выбором нового предмета
	PanelResetAndHide();
	if (GridList.IsValidIndex(Cell) && !GridList[Cell].IsEmpty())
	{
		Target = GridList[Cell];
		TargetIdx = Cell;
		RefreshScrolls();
		UpdateItemInfo();
		const int32 VisCell = BagScroll.ToVisible(Cell);   // ★ 29.10.2026: рамка — над видимой ячейкой этой позиции
		if (FrameItemSel && GridIcons.IsValidIndex(VisCell))
		{
			PlaceFrameAt(FrameItemSel, BaseItemSel, GridIcons[0], GridIcons[VisCell]);
			FrameItemSel->SetVisibility(ESlateVisibility::HitTestInvisible);
			if (UAsdaAnimatedPlane* P = Cast<UAsdaAnimatedPlane>(FrameItemSel)) { P->RestartAnim(); }
		}
	}
	// Выбор свитка и помощников сбрасывается ВСЕГДА (индексы 925/927/929 эталон здесь не трогает).
	if (FrameScrollSel) { FrameScrollSel->SetVisibility(ESlateVisibility::Collapsed); }
	if (FrameSubSel0)   { FrameSubSel0->SetVisibility(ESlateVisibility::Collapsed); }
	if (FrameSubSel1)   { FrameSubSel1->SetVisibility(ESlateVisibility::Collapsed); }
	Scroll.Clear(); Helper97.Clear(); Helper98.Clear();
	RedrawCards();
}

void UAsdaItemUpgradeSubsystem::OnScrollCellClicked(int32 Cell)
{
	// `itemWindow_SelectCompareSlotA@0xD27B70` — без гейта «свободен»; рамка сперва гаснет.
	if (Cell < 0 || Cell >= kScrollCells) { return; }
	if (FrameScrollSel) { FrameScrollSel->SetVisibility(ESlateVisibility::Collapsed); }
	if (ScrollList.IsValidIndex(Cell))
	{
		Scroll = ScrollList[Cell];
		if (FrameScrollSel && ScrollIcons.IsValidIndex(Cell))
		{
			PlaceFrameAt(FrameScrollSel, BaseScrollSel, ScrollIcons[0], ScrollIcons[Cell]);
			FrameScrollSel->SetVisibility(ESlateVisibility::HitTestInvisible);
			if (UAsdaAnimatedPlane* P = Cast<UAsdaAnimatedPlane>(FrameScrollSel)) { P->RestartAnim(); }
		}
		ScrollIdx = Cell;
	}
	RedrawCards();
}

void UAsdaItemUpgradeSubsystem::OnHelper97CellClicked(int32 Cell)
{
	// `itemWindow_SelectCompareSlotB@0xD27CE0` (20081…20083 → this+846, рамка 20140).
	if (Cell < 0 || Cell >= kHelperCells) { return; }
	if (FrameSubSel0) { FrameSubSel0->SetVisibility(ESlateVisibility::Collapsed); }
	if (H97List.IsValidIndex(Cell))
	{
		Helper97 = H97List[Cell];
		if (FrameSubSel0 && H97Icons.IsValidIndex(Cell))
		{
			PlaceFrameAt(FrameSubSel0, BaseSubSel0, H97Icons[0], H97Icons[Cell]);
			FrameSubSel0->SetVisibility(ESlateVisibility::HitTestInvisible);
			if (UAsdaAnimatedPlane* P = Cast<UAsdaAnimatedPlane>(FrameSubSel0)) { P->RestartAnim(); }
		}
		H97Idx = Cell;
	}
	RedrawCards();
}

void UAsdaItemUpgradeSubsystem::OnHelper98CellClicked(int32 Cell)
{
	// `itemWindow_SelectCompareSlotC@0xD27E50` (20090…20092 → this+847, рамка 20141).
	if (Cell < 0 || Cell >= kHelperCells) { return; }
	if (FrameSubSel1) { FrameSubSel1->SetVisibility(ESlateVisibility::Collapsed); }
	if (H98List.IsValidIndex(Cell))
	{
		Helper98 = H98List[Cell];
		if (FrameSubSel1 && H98Icons.IsValidIndex(Cell))
		{
			PlaceFrameAt(FrameSubSel1, BaseSubSel1, H98Icons[0], H98Icons[Cell]);
			FrameSubSel1->SetVisibility(ESlateVisibility::HitTestInvisible);
			if (UAsdaAnimatedPlane* P = Cast<UAsdaAnimatedPlane>(FrameSubSel1)) { P->RestartAnim(); }
		}
		H98Idx = Cell;
	}
	RedrawCards();
}

void UAsdaItemUpgradeSubsystem::OnCardIconClicked(int32 Kind)
{
	// msgType 0xB по 20150/20153/20156 (ui_core.c:47943-47957): сбросить выбор, рамку НЕ гасить (1:1).
	if (Kind == 0) { Scroll.Clear(); }
	else if (Kind == 1) { Helper97.Clear(); }
	else if (Kind == 2) { Helper98.Clear(); }
	else { return; }
	RedrawCards();
}

void UAsdaItemUpgradeSubsystem::OnCardClearClicked(int32 Kind)
{
	// Крестики 20152/20155/20158 (ui_core.c:47666-47683): сбросить выбор и спрятать рамку.
	if (Kind == 0) { Scroll.Clear();   if (FrameScrollSel) { FrameScrollSel->SetVisibility(ESlateVisibility::Collapsed); } }
	else if (Kind == 1) { Helper97.Clear(); if (FrameSubSel0) { FrameSubSel0->SetVisibility(ESlateVisibility::Collapsed); } }
	else if (Kind == 2) { Helper98.Clear(); if (FrameSubSel1) { FrameSubSel1->SetVisibility(ESlateVisibility::Collapsed); } }
	else { return; }
	RedrawCards();
}

void UAsdaItemUpgradeSubsystem::OnUpgradeClicked()
{
	// `uiItemUpgradeValidate@0xD27FC0` (ui_core.c:48261-48336). ГЕЙТА ОТКРЫТИЯ окна нет, гейты — здесь.
	if (!bIdle) { return; }   // `if (*(this + 3394))`
	if (ReplyLatch.IsArmed() && CVarSingleInFlight.GetValueOnGameThread() != 0)
	{
		UE_LOG(LogAsdaUpgrade, Log, TEXT("[заточка] 20190: предыдущий 6504 ещё без ответа — новая попытка не начата (N-2)"));
		return;
	}
	const FAsdaItemInstance* S = LiveOf(Scroll);
	const int32 ScrollSlot = (S && !S->IsEmpty()) ? (int32)S->SlotIndex : -1;
	const FAsdaItemInstance* T = LiveOf(Target);
	if (!T || T->IsEmpty()) { MsgBox(AsdaItemUpgradeLoc::kNeedTarget); return; }   // 110114
	if (!S || S->IsEmpty()) { MsgBox(AsdaItemUpgradeLoc::kNeedScroll); return; }   // 110115
	const int32 Lvl = (int32)T->GemGrade, Used = RecByte(*T, 25), Max = RecByte(*T, 26);
	if (Max == Used && Max > Lvl) { MsgBox(AsdaItemUpgradeLoc::kTriesSpent); return; }   // 110116
	const int32 TargetSlot = (int32)T->SlotIndex;
	if (ScrollSlot == -1 || TargetSlot == -1 || !bIdle) { return; }
	// ★★★ N-14: `+26 == 0` → 50011. На сервере проверки «предел > 0» НЕТ (с пределом 0 предмет сразу
	//   в «сверх предела», где провал свитком УНИЧТОЖАЕТ его) — этот гейт единственная защита.
	if (Max == 0) { MsgBox(AsdaItemUpgradeLoc::kNotUpgradable); return; }
	if (Lvl == Max) { AskConfirm72(); return; }   // предмет на пределе — модальное подтверждение 72
	StartAttempt();
}

void UAsdaItemUpgradeSubsystem::AskConfirm72()
{
	// `CItem_BuildTooltipText(g_pItemTooltipUI, 1, 72, &Src, 1)` → TWOBUT_MSGBOX, текст по парному тернару
	//   `categoryFlag == 3 ? 110111 : 110110`, кнопки 108510/108511 (item_inventory.c:51118-51140).
	if (bConfirmOpen) { return; }
	UGameInstance* GI = GetGameInstance();
	UAsdaWindowSubsystem* WM = GI ? GI->GetSubsystem<UAsdaWindowSubsystem>() : nullptr;
	const UAsdaItemTableSubsystem* IT = GI ? GI->GetSubsystem<UAsdaItemTableSubsystem>() : nullptr;
	const FAsdaItemInstance* T = LiveOf(Target);
	const FAsdaItemRecord* R = (T && IT) ? IT->GetItem(T->ItemCode) : nullptr;
	if (!WM || !R) { return; }
	const int32 ConfirmId = (R->CategoryFlag == 3) ? AsdaItemUpgradeLoc::kConfirmArmor : AsdaItemUpgradeLoc::kConfirmWeapon;
	const FString Text = (CVarConfirmKeepTags.GetValueOnGameThread() != 0) ? LocRaw(ConfirmId) : Loc(ConfirmId);   // ★ 30.10.2026: теги цвета
	if (Text.IsEmpty()) { return; }
	bConfirmOpen = true;
	AsdaEnterGuard::NoEnter(WM)->ShowGameConfirm(Text, [this](bool bYes)
	{
		bConfirmOpen = false;
		if (!bYes) { return; }
		// «Да» → msgId 1523, case 0x48 (ui_core.c:57108-57117) — та же стартовая последовательность.
		//   Эталон НЕ перепроверяет выбор (при пустом свитке он разыменовал бы ноль) — мы проверяем.
		const FAsdaItemInstance* S = LiveOf(Scroll);
		const FAsdaItemInstance* T = LiveOf(Target);
		if (!S || S->IsEmpty() || !T || T->IsEmpty() || !bIdle) { return; }
		if (ReplyLatch.IsArmed() && CVarSingleInFlight.GetValueOnGameThread() != 0) { return; }
		StartAttempt();
	}, /*GaugeGateSec*/0.f, AsdaItemUpgradeLoc::kConfirmOk, AsdaItemUpgradeLoc::kConfirmCancel, /*очередь: эталон (1, 72) ui_core.c:48298*/72, 1);
}

void UAsdaItemUpgradeSubsystem::StartAttempt()
{
	// Ветка «проверки пройдены» (ui_core.c:48301-48312) и «Да» диалога 72: +3394 = 0; показать 20070;
	//   спрятать 20005/20073; запомнить слоты свитка и цели. ★ ПАКЕТ ЗДЕСЬ ЕЩЁ НЕ ШЛЁТСЯ.
	//   Звук «S_e_Itemupgrade1.wav» — сразу после показа 20070 и скрытия 20005/20073 (волна 5).
	const FAsdaItemInstance* S = LiveOf(Scroll);
	const FAsdaItemInstance* T = LiveOf(Target);
	if (!S || !T) { return; }
	bIdle = false;
	ShowFx(EAsdaUpgradeFx::Effect1, true);
	ShowFx(EAsdaUpgradeFx::FailAni, false);
	ShowFx(EAsdaUpgradeFx::Fail, false);
	PlaySfx(TEXT("S_e_Itemupgrade1"));                   // ui_core.c:48310 / 57114 — после показа 20070
	SavedScrollSlot = (int32)S->SlotIndex;               // +3396
	SavedTargetSlot = (int32)T->SlotIndex;               // +3400
	// ★ Наше: контейнер свитка запоминаем ВМЕСТЕ со слотом. Эталон берёт контейнер при ОТПРАВКЕ из
	//   текущего выбора (`*(this[845] + 4)`), а слот — запомненный здесь; свиток можно перевыбрать во
	//   время анимации 20070 (у `SelectCompareSlotA` гейта нет) — и пара «контейнер/слот» разъедется.
	SavedScrollContainer = (int32)S->ContainerType > 0 ? (int32)S->ContainerType : Scroll.Container;
	UE_LOG(LogAsdaUpgrade, Log, TEXT("[заточка] старт попытки: цель код=%d слот=%d, свиток код=%d к=%d слот=%d"),
		T->ItemCode, SavedTargetSlot, S->ItemCode, SavedScrollContainer, SavedScrollSlot);
}

void UAsdaItemUpgradeSubsystem::OnIconClicked()
{
	// Клик по иконке 20003 при `+3394 == 0 && +3404 == 1` — msgType 0xB = ЛКМ (ui_core.c:47958-47986, под
	//   блокировкой) И 0xE = ПКМ (:48006-48034, без блокировки); тела одинаковы, обе ветки зовут эту
	//   функцию (`TickMouse`, волна 10). +3394 = 1, +3404 = 0, погасить
	//   20071/20072, звук «S_e_button10.wav», `sub_9243E7` (всегда 1) → отправка.
	if (bIdle || !bArmed) { return; }
	bIdle = true;
	bArmed = false;
	ShowFx(EAsdaUpgradeFx::Effect2, false);
	ShowFx(EAsdaUpgradeFx::Click, false);
	PlaySfx(TEXT("S_e_button10"));   // ui_core.c:47966 / 48014 — перед `netSendBoneMaskRequest`
	SendAttempt();
}

void UAsdaItemUpgradeSubsystem::SendAttempt()
{
	// `netSendBoneMaskRequest@0xD2AAB0`: только если выбран свиток (`if (this[845])`).
	UAsdaNetSubsystem* Net = GetGameInstance() ? GetGameInstance()->GetSubsystem<UAsdaNetSubsystem>() : nullptr;
	const FAsdaItemInstance* S = LiveOf(Scroll);
	if (!Net || !S || S->IsEmpty())
	{
		UE_LOG(LogAsdaUpgrade, Warning, TEXT("[заточка] 6504 не отправлен: %s"), !Net ? TEXT("нет сети") : TEXT("свиток не выбран"));
		return;
	}
	// ★ N-14 (клиентская часть, наше): перед самой отправкой ещё раз «+26 == 0 ⇒ 50011». Запись цели
	//   могла смениться между кнопкой и кликом по иконке (ответ на чужой пакет, перекладка сумки).
	const FAsdaItemInstance* T = LiveOf(Target);
	if (!T || T->IsEmpty() || RecByte(*T, 26) == 0)
	{
		HideAllFx();
		MsgBox(AsdaItemUpgradeLoc::kNotUpgradable);
		UE_LOG(LogAsdaUpgrade, Warning, TEXT("[заточка] 6504 не отправлен: у цели нет предела попыток (+26 == 0) — защита N-14"));
		return;
	}
	auto PairOf = [this](const FAsdaItemUpgradeEntry& E, uint8& OutCont, int32& OutSlot)
	{
		OutCont = 0; OutSlot = -1;   // сентинел «пары нет» — `sub_D2B170`
		if (const FAsdaItemInstance* It = LiveOf(E))
		{
			if (!It->IsEmpty())
			{
				OutCont = (uint8)((int32)It->ContainerType > 0 ? (int32)It->ContainerType : E.Container);
				OutSlot = (int32)It->SlotIndex;
			}
		}
	};
	uint8 C97 = 0, C98 = 0; int32 S97 = -1, S98 = -1;
	PairOf(Helper97, C97, S97);   // this+846 → @6/@7
	PairOf(Helper98, C98, S98);   // this+847 → @9/@10
	const uint8 ScrollCont = (uint8)SavedScrollContainer;
	const bool bOk = Net->SendItemUpgrade6504(/*контейнер цели*/1, SavedTargetSlot, ScrollCont, SavedScrollSlot, C97, S97, C98, S98);
	if (!bOk)
	{
		// ★ Закон «не защёлкивать состояние заранее»: пакет не ушёл — замок НЕ ставим, окно свободно.
		UE_LOG(LogAsdaUpgrade, Warning, TEXT("[заточка] 6504 не ушёл (нет соединения/гейт) — окно остаётся свободным"));
		return;
	}
	// Эталон после `CNetwork_Send` ставит +3394 = 0 (асм 0xD2B005 `mov byte [eax+0xD42], 0`).
	bIdle = false;
	ReplyLatch.Arm(6504);   // ★ S2 (В2-A дедупа №2, 11.10.2026)
}

// ═════════════════════════════ ЗВУКИ (волна 5) ═════════════════════════════════════════════════
void UAsdaItemUpgradeSubsystem::PlaySfx(const TCHAR* Name)
{
	// `csoundPlayName2D(g_pSoundMgr, 18, "имя.wav")`: 2D по имени, аргумент 18 эталоном игнорируется.
	//   ★ Решение координатора: звук ДОИГРЫВАЕТ до конца — одноразовый `PlaySound2D`, анимацию не ждёт.
	JournalAdd(FString::Printf(TEXT("snd %s"), Name));
	AsdaUi::PlaySound2DByName(GetGameInstance(), Name);   // ★ R5-b 12.11.2026: общая точка (то же тело)
}

void UAsdaItemUpgradeSubsystem::PlayButtonSfx(int32 IdNum)
{
	// `csoundPlayName2DStore(g_pSoundMgr, 5, 0)` — id 5 таблицы SFT.BIN = `S_e_button06.wav`.
	//   ★ Волна 9: сам звук — общая точка кнопок процедуры инвентаря `AsdaUi::PlayInvenButtonSfx`; здесь журнал.
	JournalAdd(FString::Printf(TEXT("snd id5 (кнопка %d)"), IdNum));
	AsdaUi::PlayInvenButtonSfx(GetGameInstance());
}

void UAsdaItemUpgradeSubsystem::OnIconHoverEnter()
{
	// ui_core.c:47842-47843: `if (cmdId == 20003 && !+3394 && +3404 == 1) csoundPlayName2D(…, "S_e_button09.wav")`.
	if (!bIdle && bArmed) { PlaySfx(TEXT("S_e_button09")); }
}

void UAsdaItemUpgradeSubsystem::JournalAdd(const FString& Event)
{
	// Журнал диагностический — держим хвост (256 строк). ★ R5-b 12.11.2026: общая точка (то же тело).
	AsdaUi::JournalAppend(FxJournal, FxJournalStart, Event);
}

// ═════════════════════════════ ЭФФЕКТЫ ════════════════════════════════════════════════════════
void UAsdaItemUpgradeSubsystem::ShowFx(EAsdaUpgradeFx Fx, bool bShow)
{
	const int32 F = (int32)Fx;
	if (!FxGroups.IsValidIndex(F)) { return; }
	// `CControl_SetVisible(c, 1) + CControl_Show(c)` — показ с перезапуском анимации;
	//   `CControl_SetVisible(c, 0) + CControl_CloseHide(c)` — скрыть и остановить БЕЗ события «доиграла».
	SetGroupVisible(FxGroups[F], bShow);
	UAsdaAnimatedPlane* P = FxPlanes.IsValidIndex(F) ? FxPlanes[F].Get() : nullptr;
	if (P) { if (bShow) { P->RestartAnim(); } else { P->StopAnim(); } }
	if (bShow) { JournalAdd(FString::Printf(TEXT("fx+ %d"), FxIdNum(Fx))); }
	// Крайний срок ОДНОРАЗОВОЙ анимации (ResultNeverLost): длительность прогона + 1.5 с. У 20071/20072/20073
	//   события конца нет (статичные/цикл) — срока у них не бывает.
	const bool bOneShot = (Fx == EAsdaUpgradeFx::Effect1 || Fx == EAsdaUpgradeFx::Success || Fx == EAsdaUpgradeFx::FailAni);
	const float Ms = (P && P->FrameIntervalMs > 0.f) ? P->FrameIntervalMs : FxFallbackMs(Fx);
	FxDeadline[F] = (bShow && bOneShot && Ms > 0.f) ? FPlatformTime::Seconds() + (double)Ms / 1000.0 + 1.5 : 0.0;
	if (bShow && Fx == EAsdaUpgradeFx::Effect1 && !P)
	{
		// Плана анимации нет в ассете — «взвести» сразу, иначе иконка никогда не станет кликабельной.
		OnEffectFinished(Fx);
	}
}

void UAsdaItemUpgradeSubsystem::HideAllFx()
{
	for (int32 F = 0; F < (int32)EAsdaUpgradeFx::Count; ++F) { ShowFx((EAsdaUpgradeFx)F, false); }
}

void UAsdaItemUpgradeSubsystem::FlushPendingResultIfAny()
{
	// ★ ResultNeverLost (наше): эффекты гасят ДО конца анимации результата (тик проверки выбора, открытие
	//   окна) — в оригинале буфер +3405 при этом так и не выводится. Отдаём его сейчас.
	if (!bResultPending || CVarResultNeverLost.GetValueOnGameThread() == 0) { return; }
	const bool bSuccess = (PendingFx == EAsdaUpgradeFx::Success);
	UE_LOG(LogAsdaUpgrade, Log, TEXT("[заточка] анимацию результата срезали — текст выводим сразу (%s)"),
		bSuccess ? TEXT("успех") : TEXT("провал"));
	JournalAdd(TEXT("срез анимации тиком → вывод буфера (ResultNeverLost)"));
	OnEffectFinished(PendingFx);
}

void UAsdaItemUpgradeSubsystem::HandleEffect1Finished() { OnEffectFinished(EAsdaUpgradeFx::Effect1); }
void UAsdaItemUpgradeSubsystem::HandleSuccessFinished() { OnEffectFinished(EAsdaUpgradeFx::Success); }
void UAsdaItemUpgradeSubsystem::HandleFailAniFinished() { OnEffectFinished(EAsdaUpgradeFx::FailAni); }

void UAsdaItemUpgradeSubsystem::OnEffectFinished(EAsdaUpgradeFx Fx)
{
	// `itemEnhanceUI_OnControlMessage@0xD2A1C0` (item_inventory.c:44296-44338).
	FxDeadline[(int32)Fx] = 0.0;
	JournalAdd(FString::Printf(TEXT("fx0x18 %d"), FxIdNum(Fx)));
	switch (Fx)
	{
	case EAsdaUpgradeFx::Effect1:
	{
		// Конец 20070: спрятать его, показать 20071 + 20072, «взведён» (+3404 = 1).
		SetGroupVisible(FxGroups[(int32)EAsdaUpgradeFx::Effect1], false);
		if (bIdle) { return; }   // попытку уже отменил тик/сброс — «взводить» нечего
		ShowFx(EAsdaUpgradeFx::Effect2, true);
		ShowFx(EAsdaUpgradeFx::Click, true);
		bArmed = true;
		break;
	}
	case EAsdaUpgradeFx::Success:
	{
		// Конец 20004: спрятать; при ступени `+24 <= 5` — буфер в чат и в окно (Д-12а — на любой);
		//   затем попап 19100.
		SetGroupVisible(FxGroups[(int32)EAsdaUpgradeFx::Success], false);
		if (!bResultPending || PendingFx != EAsdaUpgradeFx::Success) { return; }
		bResultPending = false;
		// ★ R-1: ступень — из записи ответа (эталон читает ту же ячейку `self[844]+24`).
		const int32 Lvl = (ResultRecord.Num() >= 57) ? (int32)ResultRecord[24] : 0;
		if (CVarSuccessTextAllLevels.GetValueOnGameThread() != 0 || Lvl <= 5)
		{
			DeliverResultText();
		}
		ShowResultPopup();
		break;
	}
	case EAsdaUpgradeFx::FailAni:
	{
		// Конец 20005: спрятать, показать 20073 («трещина»), буфер в чат и в окно — БЕЗУСЛОВНО.
		SetGroupVisible(FxGroups[(int32)EAsdaUpgradeFx::FailAni], false);
		if (!bResultPending || PendingFx != EAsdaUpgradeFx::FailAni) { return; }
		bResultPending = false;
		SetGroupVisible(FxGroups[(int32)EAsdaUpgradeFx::Fail], true);   // эталон: SetVisible без Show
		DeliverResultText();
		break;
	}
	default:
		break;   // 20071/20072 — ветки конца мёртвые в эталоне
	}
}

void UAsdaItemUpgradeSubsystem::DeliverResultText()
{
	// `CChat_AddTokenizedLines(g_pChatWnd, this+3405, −9053441, 10000, 0)` (системный чат, голубой, 10 с)
	//   И `CItem_BuildTooltipText(g_pItemTooltipUI, 0, −1, this+3405, 1)` (ONEBUT_MSGBOX 1500).
	if (ResultText.IsEmpty()) { return; }
	UGameInstance* GI = GetGameInstance();
	AsdaNotice::ChatColored(GI, EAsdaMsgCategory::System, ResultText, kColResultChat, 10000.f);
	AsdaNotice::MsgBox(GI, EAsdaMsgCategory::System, FAsdaMarkup::StripTags(ResultText));
}

// ═════════════════════════════ ПРИЁМ 6505 ═════════════════════════════════════════════════════
void UAsdaItemUpgradeSubsystem::DrainPendingPackets()
{
	// ★ U7 (В1 дедупа №2): общая очередь `TAsdaNetQueue` (замок + Drain) вместо своего массива.
	TArray<TArray<uint8>> Local;
	if (!PendingResults.Drain(Local)) { return; }
	for (const TArray<uint8>& B : Local) { OnUpgradeResult(B); }
}

void UAsdaItemUpgradeSubsystem::ReleaseInFlight(const TCHAR* Why)
{
	if (!ReplyLatch.IsArmed()) { return; }
	ReplyLatch.Disarm();
	UE_LOG(LogAsdaUpgrade, Log, TEXT("[заточка] замок «6504 в полёте» снят: %s (%.1f с)"),
		Why, FPlatformTime::Seconds() - ReplyLatch.ArmedAt);
}

void UAsdaItemUpgradeSubsystem::OnUpgradeResult(const TArray<uint8>& Body)
{
	// `Recv_ItemUpgradeBatch@0xD2B1E0` (network.c:19609-19828).
	if (Body.Num() < 231) { return; }
	// `*(this + 3394) = 1` — первым делом.
	// ★ Ревизия R-7 (наше): ответ, пришедший ПОСЛЕ таймаута замка (`!ReplyLatch.IsArmed()`), когда игрок уже начал
	//   НОВУЮ попытку (идёт 20070 / иконка взведена, `!bIdle`), не должен её отменять — иначе конец 20070
	//   выйдет по `if (bIdle) return` и попытка молча пропадёт. Сам результат старой попытки выводится.
	const bool bLateForNewAttempt = !ReplyLatch.IsArmed() && !bIdle;
	if (bLateForNewAttempt)
	{
		UE_LOG(LogAsdaUpgrade, Warning, TEXT("[заточка] 6505 пришёл после таймаута, идёт новая попытка — её флаги не трогаем (R-7)"));
	}
	else
	{
		bIdle = true;
	}
	ReleaseInFlight(TEXT("ответ 6505"));
	const int32 Code = (int32)Body[0];
	UE_LOG(LogAsdaUpgrade, Log, TEXT("[заточка] 6505 код=%d вес=%d предмет: код=%d ступень=%d попыток=%d/%d процент=%d"),
		Code, AsdaBytes::RdI16(&Body[1]), AsdaBytes::RdI32(&Body[3]), (int32)Body[3 + 24], (int32)Body[3 + 25],
		(int32)Body[3 + 26], AsdaBytes::RdI16(&Body[3 + 27]));
	const bool bWindowUsable = bControlsReady;
	switch (Code)
	{
	case 1:
	{
		// Успех: показать 20004 + звук «S_e_Itemupgrade2.wav» (network.c:19643-19645), вес двинул сетевой
		//   слой, четыре записи в модель, текст 50004 «Создано: [[ss1] + [sd1]].» (имя + новая ступень) в буфер.
		// ★ Ревизия R-1: буфер сбрасывается ПЕРВЫМ, имя и ступень — из записи пакета @3 (это запись цели
		//   после операции), а не из текущей цели окна — иначе в чат ушёл бы текст ПРОШЛОЙ попытки.
		ResultText.Reset();
		ResultRecord.Reset();
		ResultRecord.Append(Body.GetData() + 3, 57);
		ApplyResultRecords(Body.GetData(), /*bFailBranch*/false);
		{
			const int32 RCode = AsdaBytes::RdI32(Body.GetData() + 3);
			const int32 RLvl  = (int32)Body[3 + 24];
			UAsdaTextSubsystem* Txt = GetGameInstance()->GetSubsystem<UAsdaTextSubsystem>();
			const UAsdaItemTableSubsystem* IT = GetGameInstance()->GetSubsystem<UAsdaItemTableSubsystem>();
			if (RCode > 0 && Txt && IT && IT->GetItem(RCode))
			{
				ResultText = Txt->FormatById(AsdaItemUpgradeLoc::kResultOk, { IT->GetItemName(RCode) }, { RLvl });
			}
		}
		if (bWindowUsable) { ShowFx(EAsdaUpgradeFx::Success, true); }
		PlaySfx(TEXT("S_e_Itemupgrade2"));
		// Пустой текст и запись без предмета — буфер не взводим: выводить нечего (R-1).
		bResultPending = !ResultText.IsEmpty() || AsdaBytes::RdI32(Body.GetData() + 3) > 0;
		PendingFx = EAsdaUpgradeFx::Success;
		if (!bWindowUsable) { OnEffectFinished(EAsdaUpgradeFx::Success); }
		break;
	}
	case 2: MsgBox(AsdaItemUpgradeLoc::kResultCode2); break;
	case 3: MsgBox(AsdaItemUpgradeLoc::kResultCode3); break;
	case 4: MsgBox(AsdaItemUpgradeLoc::kResultCode4); break;
	case 5: MsgBox(AsdaItemUpgradeLoc::kResultCode5); break;   // сервер этот код не шлёт (Д-3)
	case 6: MsgBox(AsdaItemUpgradeLoc::kResultCode6); break;   // сервер этот код не шлёт (Д-3)
	case 7: MsgBox(AsdaItemUpgradeLoc::kResultCode7); break;
	case 8:
	{
		// Провал: показать 20005 + звук «S_e_Itemupgrade3.wav» (network.c:19752-19754), текст 50005 в буфер,
		//   записи в модель; опустевшая запись сумки → 110119 «предмет уничтожен» (см. `DestroyNoticeTargetOnly`).
		ResultText = Loc(AsdaItemUpgradeLoc::kResultFail);
		ResultRecord.Reset();
		ApplyResultRecords(Body.GetData(), /*bFailBranch*/true);
		bResultPending = true;
		PendingFx = EAsdaUpgradeFx::FailAni;
		if (bWindowUsable) { ShowFx(EAsdaUpgradeFx::FailAni, true); }
		PlaySfx(TEXT("S_e_Itemupgrade3"));
		if (!bWindowUsable) { OnEffectFinished(EAsdaUpgradeFx::FailAni); }
		break;
	}
	default:
		// ★ Код 0 — 13 веток сервера (дефект Д-2, лечится серверным патчем). Эталон: `default: break` — ничего.
		UE_LOG(LogAsdaUpgrade, Warning, TEXT("[заточка] 6505 код %d — отказ сервера БЕЗ текста (эталон молчит, Д-2)"), Code);
		break;
	}
	if (!bWindowUsable) { return; }
	// Всегда: перерисовать сетку/свитки/инфо; свиток/помощники, чья ячейка опустела, — сбросить.
	RefreshInventorySlots();
	RefreshScrolls();
	UpdateItemInfo();
	for (FAsdaItemUpgradeEntry* E : { &Scroll, &Helper97, &Helper98 })
	{
		const FAsdaItemInstance* It = LiveOf(*E);
		if (!E->IsEmpty() && (!It || It->IsEmpty())) { E->Clear(); }
	}
	RedrawCards();
}

int32 UAsdaItemUpgradeSubsystem::ApplyResultRecords(const uint8* Body, bool bFailBranch)
{
	// Четыре записи по 57 б с +3: предмет / свиток / помощник 97 / помощник 98. Правило эталона:
	//   код == 0 → контейнер 2: копия записи (пустая), контейнер 1: очистить; код > 0 → положить запись
	//   (контейнер 1 — сперва очистить). Прочие контейнеры эталон не трогает.
	UAsdaInventorySubsystem* Inv = GetGameInstance() ? GetGameInstance()->GetSubsystem<UAsdaInventorySubsystem>() : nullptr;
	if (!Inv || !Body) { return 0; }
	const bool bTargetOnly = CVarDestroyNoticeTargetOnly.GetValueOnGameThread() != 0;
	int32 Applied = 0;
	int32 DestroyNotices = 0;
	Inv->BeginCellBatch();
	for (int32 I = 0; I < 4; ++I)
	{
		const uint8* Rec = Body + 3 + 57 * I;
		const int32 Code = AsdaBytes::RdI32(Rec);
		const int32 Cont = (int32)Rec[4];
		const int32 Slot = AsdaBytes::RdI16(Rec + 5);
		if (Cont != 1 && Cont != 2) { continue; }
		// ★ N-3 (наше): эталон пишет по контейнеру и слоту ИЗ ПАКЕТА без проверки границ
		//   (`memset(&g_InventoryBag + 57 * слот, …)`) — у нас кламп: сумка 0…59 (★ 29.10.2026, инвентарь ×2; было 0…29), расходники 0…59
		//   и не дальше модели. Запись вне границ отбрасывается целиком.
		const int32 Limit = FMath::Min(Cont == 1 ? kShellCells : kSpendCells, Inv->GetContainerSize(Cont));
		if (Slot < 0 || Slot >= Limit)
		{
			UE_LOG(LogAsdaUpgrade, Warning, TEXT("[заточка] 6505 запись %d: контейнер %d слот %d вне границ (0…%d) — отброшена (N-3)"),
				I, Cont, Slot, Limit - 1);
			continue;
		}
		if (Code == 0)
		{
			Inv->ClearCell(Cont, Slot);
			++Applied;
			if (Cont == 1 && bFailBranch && (!bTargetOnly || I == 0)) { ++DestroyNotices; }
		}
		else if (Code > 0)
		{
			if (Cont == 1) { Inv->ClearCell(Cont, Slot); }
			TArray<uint8> R57; R57.Append(Rec, 57);
			if (Inv->ApplyExternalRecord(R57)) { ++Applied; }
		}
	}
	Inv->EndCellBatch();
	for (int32 K = 0; K < DestroyNotices; ++K) { MsgBox(AsdaItemUpgradeLoc::kDestroyed); }   // 110119
	UE_LOG(LogAsdaUpgrade, Log, TEXT("[заточка] 6505: записей применено %d, нотисов 110119 %d"), Applied, DestroyNotices);
	return Applied;
}

void UAsdaItemUpgradeSubsystem::ShowResultPopup()
{
	// `CItemEnhanceUI_ShowResultPopup@0xD2A470`: только при `6 <= +24 <= 10` (Д-12б — до 20).
	// ★ Ревизия R-1: источник — запись предмета ИЗ ПАКЕТА 6505 (эталон читает `self[844]`, указатель на ту же
	//   ячейку, куда приём только что положил эту запись; у порта цель к этому моменту может быть сброшена).
	if (ResultRecord.Num() < 57) { return; }
	const uint8* R = ResultRecord.GetData();
	const int32 Code = AsdaBytes::RdI32(R);
	if (Code <= 0) { return; }
	const int32 Lvl = (int32)R[24];
	const int32 EnchantPct = AsdaBytes::RdI16(R + 27);
	const int32 MaxLvl = FMath::Max(6, CVarOptionPopupMaxLevel.GetValueOnGameThread());
	if (Lvl < 6 || Lvl > MaxLvl) { return; }
	UGameInstance* GI = GetGameInstance();
	const UAsdaItemTableSubsystem* IT = GI ? GI->GetSubsystem<UAsdaItemTableSubsystem>() : nullptr;
	UAsdaTextSubsystem* Txt = GI ? GI->GetSubsystem<UAsdaTextSubsystem>() : nullptr;
	if (!IT || !Txt || !IT->GetItem(Code)) { return; }
	if (!EnsurePopup()) { return; }
	const FString Name = IT->GetItemName(Code);
	// 19101: 99110 при ступени ровно 6, 99111 при ≥ 7.
	if (PopTitle) { PopTitle->SetText(FText::FromString(Loc(Lvl == 6 ? AsdaItemUpgradeLoc::kPopTitle6 : AsdaItemUpgradeLoc::kPopTitle7))); }
	// 19102: 99100 / 99101 со слотами [ss1] = имя, [sd1] = ступень.
	const FString Body = Txt->FormatById(Lvl == 6 ? AsdaItemUpgradeLoc::kPopText6 : AsdaItemUpgradeLoc::kPopText7, { Name }, { Lvl });
	if (PopText) { PopText->SetText(FText::FromString(FAsdaMarkup::StripTags(Body))); }
	// 19103: иконка предмета и «+N%» (запись +27).
	if (PopIconGrid) { PopIconGrid->Refresh(); }
	if (PopIconLabel) { PopIconLabel->SetText(FText::FromString(FString::Printf(TEXT("+%d%%"), EnchantPct))); }
	// 19104: «Дополнительное улучшение».
	if (PopOptTitle) { PopOptTitle->SetText(FText::FromString(Loc(AsdaItemUpgradeLoc::kPopOptTitle))); }
	// 19105: строка опции (i16 +41 id, i16 +43 значение) по `ItemOptionTable` цветом −103.
	FString OptLine;
	if (UAsdaItemTooltipSubsystem* Tip = GI->GetSubsystem<UAsdaItemTooltipSubsystem>())
	{
		const int32 OptId  = AsdaBytes::RdI16(R + 41);
		const int32 OptVal = AsdaBytes::RdI16(R + 43);
		const FString V = Tip->FormatOptionValue(OptId, OptVal);
		if (!V.IsEmpty()) { OptLine = FString::Printf(TEXT("<FC=%08x>%s</F> \n"), kColPopupOption, *V); }
	}
	if (OptLine.IsEmpty() && CVarPopupOptionNoDup.GetValueOnGameThread() == 0)
	{
		// 1:1 эталон (ревизия R-4): в `Buffer` осталась подпись иконки 19103 `sprintf(Buffer, "+%d%%", +27)`.
		OptLine = FString::Printf(TEXT("+%d%%"), EnchantPct);
	}
	SetFmt(FmtPopOpt, OptLine);
	if (PopOkText) { PopOkText->SetText(FText::FromString(Loc(AsdaItemUpgradeLoc::kPopOk))); }
	if (UAsdaWindowSubsystem* WM = GI->GetSubsystem<UAsdaWindowSubsystem>())
	{
		bPopupOpen = true;
		WM->SetWindowVisible(kPopupId, true);
		WM->BringWindowToFront(kPopupId);   // `CClass_MapInsert(g_pWindowMgr, self[657])`
	}
	UE_LOG(LogAsdaUpgrade, Log, TEXT("[заточка] попап 19100: ступень %d, опция «%s»"), Lvl, *OptLine);
}

// ═════════════════════════════ ОПРОС МЫШИ ═════════════════════════════════════════════════════
void UAsdaItemUpgradeSubsystem::TickMouse()
{
	// ★ Ревизия R-6: закон шапки `AsdaWindowSubsystem.h` — опросчик мыши ПЕРВОЙ строкой спрашивает, не лежит
	//   ли под курсором чужой всплывающий слой; плюс собственный попап 19100 (не модальный, якорь по центру
	//   экрана) — клик по его телу над карточками сбрасывал выбор свитка/помощника.
	if (UAsdaWindowSubsystem* WM = GetGameInstance() ? GetGameInstance()->GetSubsystem<UAsdaWindowSubsystem>() : nullptr)
	{
		// ★ Волна 9 (МЕЛК-7 ревизии волны 8): и ОБЫЧНОЕ окно поверх — окна 20000 и 78200 независимы и
		//   перекрываются полосой 56 px; эталон отдаёт мышь только верхнему (`uiWindowMgrDispatchMouse`).
		if (WM->IsPointerBlockedByTopmost(kWindowId) || (bPopupOpen && WM->IsCursorOverWindow(kPopupId))
			|| WM->IsCursorOverHigherWindow(kWindowId))
		{
			TickTooltip(-1, -1);
			bIconHover = false;
			Mouse.Reset();
			return;
		}
	}
	// Наведение/клик по ЖИВОЙ геометрии: ячейки лежат внутри групп, посчитанные зоны уезжают.
	int32 Kind = -1, Idx = -1;
	for (int32 C = 0; C < kGridCells && Kind < 0; ++C)
	{
		// ★ 29.10.2026 (инвентарь ×2): индекс сетки — позиция СПИСКА `GridList` (видимая ячейка + окно прокрутки).
		if (GridIcons.IsValidIndex(C) && GridIcons[C] && AsdaUi::IsCursorOverWidget(GridIcons[C])) { Kind = kTipGrid; Idx = BagScroll.ToList(C); }
	}
	for (int32 C = 0; C < kScrollCells && Kind < 0; ++C)
	{
		if (ScrollIcons.IsValidIndex(C) && ScrollIcons[C] && AsdaUi::IsCursorOverWidget(ScrollIcons[C])) { Kind = kTipScroll; Idx = C; }
	}
	for (int32 C = 0; C < kHelperCells && Kind < 0; ++C)
	{
		if (H97Icons.IsValidIndex(C) && H97Icons[C] && AsdaUi::IsCursorOverWidget(H97Icons[C])) { Kind = kTipH97; Idx = C; }
		else if (H98Icons.IsValidIndex(C) && H98Icons[C] && AsdaUi::IsCursorOverWidget(H98Icons[C])) { Kind = kTipH98; Idx = C; }
	}
	for (int32 K = 0; K < 3 && Kind < 0; ++K)
	{
		if (CardIcons.IsValidIndex(K) && CardIcons[K] && AsdaUi::IsCursorOverWidget(CardIcons[K])) { Kind = kTipCard; Idx = K; }
	}
	if (Kind < 0 && TargetIcon && AsdaUi::IsCursorOverWidget(TargetIcon)) { Kind = kTipTarget; Idx = 0; }
	// Подсказка 20195 — только над открытой частью полосы (кнопка «Особый» лежит поверх неё).
	if (Kind < 0 && Panel20195 && Panel20195->GetVisibility() != ESlateVisibility::Collapsed
		&& AsdaUi::IsCursorOverWidget(Panel20195) && !(BtnSpecial && AsdaUi::IsCursorOverWidget(BtnSpecial)))
	{
		Kind = kTipHint; Idx = 0;
	}

	TickTooltip(Kind, Idx);

	// ★ Волна 5: msg 8 (наведение) по 20003 — фронт «курсор вошёл» (у эталона 8 — вход, 0xA — уход).
	const bool bOverIcon = (Kind == kTipTarget);
	if (bOverIcon && !bIconHover) { OnIconHoverEnter(); }
	bIconHover = bOverIcon;

	// ★★★ Волна 10: событие 0xE — ФРОНТ НАЖАТИЯ ПКМ по взведённой иконке 20003 → то же тело, что у 0xB
	//   (`CGameWindow_OnCommand`, ui_core.c:48005-48034). Что такое 0xE, доказано двумя методами:
	//   (1) генератор: 0xE поднимает ТОЛЬКО `CControl_OnRButtonDblClick@0x12DC410` (миснейм; ui_core.c:107522,
	//       асм `6A 01 … 83 F8 01 75 … 6A 00 … 6A 02 … 6A 0E`) при байте кнопки [1] == 1 и [0], [2] != 1; байт [1]
	//       = 1 кладёт насос `input_ProcessMoveCommand@0x12F0290` (сообщение 3, кнопка 1), а его ставит WndProc
	//       на WM_RBUTTONDOWN 0x204 (`CWindowMgr_QueueLButton(…, 1, …)`, game_main.c:5822 — тоже миснейм);
	//       зеркально 0xB — байт [0] (левая); (2) потребители: окно продажи шлёт на 0xB `sellWindowOnSlotClick(…, 0)`,
	//       на 0xE — `(…, 1)` (ui_core.c:65043-65051), инвентарь на 0xE открывает меню «похвастаться» (ПКМ, :48036).
	//   ★ БЕЗ `uiCheckRecentActivity`: в ветке 0xE эталона её НЕТ (в отличие от 0xB, :47932) — блокировку не ставим.
	//   Хвост ветки (`CInventoryUI_OnSlotClick(20003)` → 0 вне диапазонов 10010…10039/430…435/441…444,
	//   item_inventory.c:36229; меню 28000 под `self[202]` — не откроется) для кода 20003 ничего не делает.
	//   Прочие контролы окна 20000 на 0xE у эталона не отвечают — только иконка 20003.
	if (Mouse.bRmbDown && Kind == kTipTarget)
	{
		JournalAdd(TEXT("клик 0xE (ПКМ) по 20003"));
		OnIconClicked();
	}

	if (!Mouse.bLmbDown || Kind < 0) { return; }
	// ★★★ Волна 9 (С-1): 0xB окна 20000 идёт через `CGameWindow_OnCommand`, а там весь разбор клика стоит под
	//   `if (!uiCheckRecentActivity())` (ui_core.c:47932) — пока окно 78200 ведёт переплавку (и секунду после
	//   ответа 6501), выбор ячеек, карточек и клик по взведённой 20003 не принимаются. Наведение — принимается.
	if (AsdaUi::CheckRecentActivity(GetGameInstance()))
	{
		JournalAdd(TEXT("клик отклонён: uiCheckRecentActivity"));
		return;
	}
	JournalAdd(FString::Printf(TEXT("клик 0xB вид %d ячейка %d"), Kind, Idx));   // журнал самотеста (волна 9)
	// msgType 0xB (ЛКМ). ПКМ (0xE) по 20003 — выше, отдельной веткой без блокировки (волна 10).
	switch (Kind)
	{
	case kTipGrid:   OnGridCellClicked(Idx);     break;
	case kTipScroll: OnScrollCellClicked(Idx);   break;
	case kTipH97:    OnHelper97CellClicked(Idx); break;
	case kTipH98:    OnHelper98CellClicked(Idx); break;
	case kTipCard:   OnCardIconClicked(Idx);     break;
	case kTipTarget: OnIconClicked();            break;
	default: break;
	}
}

void UAsdaItemUpgradeSubsystem::TickTooltip(int32 Kind, int32 Idx)
{
	// msg 9 → `itemInitWin_ShowSlotTooltip@0xD2D720`; msg 8 для 20195 → `tutorialPopupLayoutSmall`
	//   (заголовок 110113, тело 110112); msg 0xA → `CControl_KillFocus(&g_ItemTooltip)`.
	// ★ Дедуп №2, В1-B (U18, 11.10.2026): шапка трекера (ключ «что показано», гашение при уходе курсора) — общая точка
	//   `UAsdaItemTooltipSubsystem::BeginHoverTip`; тело показа (эталонные билдеры окна) — ниже, как было.
	UAsdaItemTooltipSubsystem* Tip = UAsdaItemTooltipSubsystem::BeginHoverTip(GetGameInstance(), EAsdaTipOwner::ItemUpgrade, TipKind, TipIdx, Kind, Idx);
	if (!Tip) { return; }
	if (Kind == kTipHint)
	{
		FAsdaTipMetrics M;
		M.WidthAdd = 27.f;    // «маленькая» всплывашка эталона (как у подсказки кнопки 449)
		M.ExtraLines = 0;
		Tip->ShowTextTip(EAsdaTipOwner::ItemUpgrade, Loc(AsdaItemUpgradeLoc::kHintTitle), LocRaw(AsdaItemUpgradeLoc::kHintBody), M);
		return;
	}
	const FAsdaItemUpgradeEntry* E = nullptr;
	bool bBuff = false;   // «buff» эталона — ячейка списка свитков: простой билдер `Item_BuildTooltipText_EAC520`
	switch (Kind)
	{
	case kTipGrid:   E = GridList.IsValidIndex(Idx) ? &GridList[Idx] : nullptr; break;
	case kTipScroll: E = ScrollList.IsValidIndex(Idx) ? &ScrollList[Idx] : nullptr; bBuff = true; break;
	case kTipH97:    E = H97List.IsValidIndex(Idx) ? &H97List[Idx] : nullptr; break;
	case kTipH98:    E = H98List.IsValidIndex(Idx) ? &H98List[Idx] : nullptr; break;
	case kTipCard:   E = (Idx == 0) ? &Scroll : (Idx == 1) ? &Helper97 : &Helper98; break;
	case kTipTarget: E = &Target; break;
	default: break;
	}
	const FAsdaItemInstance* It = E ? LiveOf(*E) : nullptr;
	if (!It || It->IsEmpty()) { Tip->HideTooltipFor(EAsdaTipOwner::ItemUpgrade); return; }
	const UAsdaItemTableSubsystem* IT = GetGameInstance()->GetSubsystem<UAsdaItemTableSubsystem>();
	if (IT && IT->GetItem(It->ItemCode))
	{
		// ★ R-8: эталон зовёт простой билдер с `showCount = 0` — «(N)» в заголовке нет.
		if (bBuff) { Tip->ShowShopSlotTooltip(It->ItemCode, It->Amount, /*bSellMode*/false, EAsdaTipOwner::ItemUpgrade, /*bShowCount*/false); }
		else { Tip->ShowItemTooltip(*It, /*bSuppressCompare*/false, EAsdaTipOwner::ItemUpgrade, E->Container == kSpend ? 2 : 1); }
	}
	else if (SpecialSubTypeOf(It->ItemCode) >= 0)
	{
		// `Table2Record` — синтетическая запись с одним кодом + полный билдер (как у панели состава пака).
		Tip->ShowStampSlotTooltip(It->ItemCode, EAsdaTipOwner::ItemUpgrade);
	}
}

// ═════════════════════════════ ДАННЫЕ И ОБЩИЕ ТОЧКИ ═══════════════════════════════════════════
const FAsdaItemInstance* UAsdaItemUpgradeSubsystem::LiveOf(const FAsdaItemUpgradeEntry& E) const
{
	if (E.IsEmpty()) { return nullptr; }
	const UAsdaInventorySubsystem* Inv = GetGameInstance() ? GetGameInstance()->GetSubsystem<UAsdaInventorySubsystem>() : nullptr;
	return Inv ? Inv->GetCell(E.Container, E.CellIndex) : nullptr;
}

int32 UAsdaItemUpgradeSubsystem::RecByte(const FAsdaItemInstance& It, int32 Off)
{
	return It.Raw.IsValidIndex(Off) ? (int32)It.Raw[Off] : 0;
}

int32 UAsdaItemUpgradeSubsystem::SpecialSubTypeOf(int32 ItemCode) const
{
	const UAsdaItemTableSubsystem* IT = GetGameInstance() ? GetGameInstance()->GetSubsystem<UAsdaItemTableSubsystem>() : nullptr;
	if (!IT || !IT->IsSpecialItemCode(ItemCode)) { return -1; }
	FAsdaSpecialItemlistRow Row;
	return IT->GetSpecialItem(ItemCode, Row) ? Row.SubType : -1;
}

int32 UAsdaItemUpgradeSubsystem::SpecialResultModelOf(int32 ItemCode) const
{
	// `*(unsigned __int16 *)table2Record->resultModel` — у помощника 97 это прибавка к эффекту (5).
	const UAsdaItemTableSubsystem* IT = GetGameInstance() ? GetGameInstance()->GetSubsystem<UAsdaItemTableSubsystem>() : nullptr;
	if (!IT || !IT->IsSpecialItemCode(ItemCode)) { return 0; }
	FAsdaSpecialItemlistRow Row;
	return IT->GetSpecialItem(ItemCode, Row) ? (Row.ResultModel & 0xFFFF) : 0;
}

void UAsdaItemUpgradeSubsystem::PlaceFrameAt(UWidget* Frame, const FVector2D& Base, UWidget* Cell0, UWidget* Cell) const
{
	// `CControl_SetField74_75(рамка, позиция ячейки)`: рамка и ячейки — в одной системе координат `.JUI`
	//   (у 20060 ещё +(10,7) — это смещение списка, уже учтённое в базовой позиции рамки). Двигаем рамку
	//   на сдвиг ячейки относительно ячейки 0 — это ДИНАМИКА (выбор), а не правка разметки.
	UCanvasPanelSlot* S = Frame ? Cast<UCanvasPanelSlot>(Frame->Slot) : nullptr;
	if (!S || !Cell0 || !Cell) { return; }
	const FVector2D P = Base + (SlotPos(Cell) - SlotPos(Cell0));
	S->SetPosition(P);
}

void UAsdaItemUpgradeSubsystem::CaptureFmt(FAsdaFmtText& F, UTextBlock* T)
{
	// Тот же приём, что у окна характеристик (`UAsdaCharaInfoSubsystem::CaptureFmt`): стиль JUI снимаем
	//   ДО первой подмены на rich, иначе rich-контрол возьмёт строку `Default` таблицы стилей.
	//   25.09.2026: тело переехало в общую точку разметки (окна 900/20000/50300), здесь — вызов.
	UAsdaRichText::CaptureFmt(F, T);
}

void UAsdaItemUpgradeSubsystem::SetFmt(FAsdaFmtText& F, const FString& Raw)
{
	UAsdaRichText::SetFmt(F, Raw);   // общая точка `UAsdaRichText::SetFmt` (`ApplyFormatted` + переприсвоение узла)
}

FString UAsdaItemUpgradeSubsystem::Loc(int32 LocId) const
{
	return AsdaNotice::Loc(GetGameInstance(), LocId);
}

FString UAsdaItemUpgradeSubsystem::LocRaw(int32 LocId) const
{
	return AsdaNotice::LocRaw(GetGameInstance(), LocId, /*bDebugFallback*/false);
}

void UAsdaItemUpgradeSubsystem::MsgBox(int32 LocId) const
{
	// `CItem_BuildTooltipText(g_pItemTooltipUI, 0, −1, текст, 1)` = ONEBUT_MSGBOX 1500 (не канал ленты).
	//   Без оконной подсистемы/строки фасад уводит текст в ленту категорией отказа предмета.
	AsdaNotice::MsgBoxLoc(GetGameInstance(), EAsdaMsgCategory::ItemRefused, LocId);
}
