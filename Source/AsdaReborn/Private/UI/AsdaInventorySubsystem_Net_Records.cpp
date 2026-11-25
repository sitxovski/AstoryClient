// Copyright (c) 2026 @sitxovski (Discord)
// AsdaInventorySubsystem_Net_Records.cpp — ★ R3-c 12.11.2026 (разрез AsdaInventorySubsystem_Net.cpp, дословный перенос): ремонт, расширение сумки, выброс, записи лута/лавок/склада/ремонта/войны/переноса опций.
//   Общее для частей — в AsdaInventorySubsystem_Net_Private.h; шапка и реверс — в AsdaInventorySubsystem_Net.cpp.
#include "UI/AsdaInventorySubsystem.h"
#include "AsdaPacketWriter.h"   // ★ U6 (В1 дедупа №2): 15 байт слотов-приёмников 6096 через общий писатель
#include "UI/AsdaLocIds.h"   // Дд2/Дд3: нотисы кнопок ОБЩИХ окон (ONEBUT_MSGBOX 1500 — AsdaLoc::kBtnOk)
#include "UI/AsdaSowelSubsystem.h"         // ★ ветка САМОЦВЕТОВ правой панели (430…437 / 441…446 / 495 / 499)
// ★ обмен по таблице событий (двойной клик по предмету subType 202 → 6098)   ★ R3: комментарий снятого включения "Npc/AsdaNpcDialogSubsystem.h" (файл им не пользовался) — сохранён
// ★ С-Р1: общий офлайн-гейт отладочных команд (RefuseDebugWhenOnline)   ★ R3: комментарий снятого включения "AsdaOutgoingGate.h" (файл им не пользовался) — сохранён
#include "AsdaInventorySubsystem_Net_Private.h"   // ★ R3-c 12.11.2026: общее для частей AsdaInventorySubsystem_Net*.cpp

// ★ 5457 — ВОССТАНОВЛЕНИЕ ПРОЧНОСТИ (имя приёмника эталона `recvInventoryConfirmDye` врёт).
//   Эталон `inventoryConfirmDyeApply_5D80@0xD15D80` (item_inventory.c:41317): если прочность в
//   записи уже равна `ItemRecord->field68` (максимуму) — нотис 12830 «уже максимум», иначе
//   записать байт +13 и дать нотис 12831.
void UAsdaInventorySubsystem::ApplyRepairResult(const TArray<uint8>& Entry57)
{
	if (Entry57.Num() < FAsdaItemInstance::kRecordSize) { return; }
	const uint8* P = Entry57.GetData();
	const int32 Code = RdI32(P);
	const int32 Cont = P[4];
	const int32 Slot = RdI16(P + 5);
	const int32 Dur  = P[13];
	UGameInstance* GI = GetGameInstance();
	UAsdaItemTableSubsystem* IT = GI ? GI->GetSubsystem<UAsdaItemTableSubsystem>() : nullptr;
	const FAsdaItemRecord* Rec = IT ? IT->GetItem(Code) : nullptr;
	if (const FAsdaItemInstance* Cur = GetCell(Cont, Slot))
	{
		if (Rec && !Cur->IsEmpty() && Cur->Raw.Num() >= 14 && (int32)Cur->Raw[13] == Rec->DurabilityMax)
		{
			Notice(12830);
			return;
		}
	}
	PutRecord(P, /*ForcedContainer*/ 0);
	Notice(12831);
	if (bWindowOpen) { RefreshGrid(); }
	UE_LOG(LogAsdaInven, Log, TEXT("[премиум] 5457 починка: предмет %d (%d:%d), прочность=%d"), Code, Cont, Slot, Dur);
}

// ═════════════════ 5456 — РАСШИРЕНИЕ СКЛАДА (предмет `special_itemlist.SubType == 34`) ═════════
// ПОЛНЫЙ ПОТОК (реверс — `Reports/inventory_use_fix.md`, раздел 1):
//   предмет 30/467/541 (`SubType == 34`, `Flag102 == 1`) → C→S 5450 `Char_FunctionItem_Use`
//   → сервер `GameServer.c:91920` (`subType == 34 && ячеекСклада >= 270 ⇒ отказ 7`)
//   → S→C 5451 (списание предмета; `case 33` эталона окон НЕ показывает)
//   → S→C 5456 `recvWarehouseChangeBagPage@0xF34BC0` → `inventoryChangeBagPage@0xE8E440`.
// Ветку выбирает СЕТЕВОЙ слой (там же живёт `SelfStoreSlotsMax` = `CCharacter+0x456`), сюда
//   приходит уже готовый вердикт. Окно делает ровно две вещи эталона:
//     CChat_AddTokenizedLines(g_pChatWnd, нотис, -11514, 10000, 0);         // строка в чат
//     CItem_BuildTooltipText(g_pItemTooltipUI, 0, -1, нотис, 1);            // ONEBUT_MSGBOX 1500
//   Имя `g_pItemTooltipUI` врёт — это менеджер игровых MSGBOX; ветка `msgClass == 0 &&
//   noticeId == -1` ставит текст в `ONEBUT_TEXT` 1501, подпись кнопки `ONEBUT_BUT00` 1503 =
//   нотис 2288 и при `bForceShow = 1` поднимает окно МОДАЛЬНО. Общая точка порта —
//   `UAsdaWindowSubsystem::ShowGameMessage` (закон `port-shared-points`), СВОЕЙ копии не заводим.
// ⚠ Закон `no-reentrant-net-callbacks`: колбэк только пакует вердикт в очередь.
void UAsdaInventorySubsystem::OnBagExpand(int32 NewMaxSlots, bool bAccepted, int32 NoticeLocId, bool bAlsoChat)
{
	TArray<uint8> Packed;
	Packed.SetNumUninitialized(10);
	uint8* P = Packed.GetData();
	FMemory::Memcpy(P + 0, &NewMaxSlots, 4);
	FMemory::Memcpy(P + 4, &NoticeLocId, 4);
	P[8] = bAccepted ? 1 : 0;
	P[9] = bAlsoChat ? 1 : 0;
	PendingPackets.Push({ 5456, Packed });
}

void UAsdaInventorySubsystem::ApplyBagExpand(const TArray<uint8>& Packed8)
{
	if (Packed8.Num() < 10) { return; }
	const uint8* P = Packed8.GetData();
	int32 NewMaxSlots = 0, NoticeLocId = 0;
	FMemory::Memcpy(&NewMaxSlots, P + 0, 4);
	FMemory::Memcpy(&NoticeLocId, P + 4, 4);
	const bool bAccepted = (P[8] != 0);
	const bool bAlsoChat = (P[9] != 0);
	if (NoticeLocId <= 0) { return; }

	// Текст берём из таблиц игры (закон `no-invented-ui`). [ПОРТ] 09.10.2026 (мультиязычность, этап 3): RU-фолбэк убран → «[#id]».
	const FString Text = LocText(NoticeLocId);

	UGameInstance* GI = GetGameInstance();
	// (1) строка в чат — `CChat_AddTokenizedLines` эталона; в ветке отката её НЕТ.
	if (bAlsoChat) { Notice(NoticeLocId); }
	// (2) ОДНОКНОПОЧНАЯ МОДАЛКА — во ВСЕХ четырёх ветках эталона.
	//   Свою плашку подсказки гасим до модалки (закон `ui-shared-tip-ownership`: гасит ВЛАДЕЛЕЦ).
	HideCellTooltip();
	// ★ ИСПРАВЛЕНИЕ 11.10.2026 (дедуп №2, В2-A, U2): было «нет оконной подсистемы ⇒ только строка лога» — стало общая политика
	//   `AsdaNotice::MsgBox` (окна нет ⇒ текст в ленту категорией ItemRefused); лог оставлен.
	if (!AsdaNotice::MsgBox(GI, EAsdaMsgCategory::ItemRefused, Text, AsdaLoc::kBtnOk))
	{
		UE_LOG(LogAsdaInven, Warning,
			TEXT("[склад] 5456: нет UAsdaWindowSubsystem — модалку показать нечем, текст в ленте: %s"), *Text);
	}

	// (3) Эталон в ветке успеха дополнительно ОТКРЫВАЕТ окно склада на новой вкладке
	//   (`uiInventoryBagOpen(wnd, wnd->selSlotIndex)` + `uiCtrl_SetChildScrollbarPos(…, 0)`).
	//   Окно склада (`Warehouse_Win`, IDNum 5500) в порту ещё НЕ ПОРТИРОВАНО — кодом его никто
	//   не ведёт, есть только импортированный виджет. Отложено в `PORT_PENDING_ON_SYSTEMS.md`;
	//   ёмкость при этом уже принята сетевым слоем и доедет до окна, когда оно появится.
	if (bAccepted)
	{
		UE_LOG(LogAsdaInven, Log,
			TEXT("[склад] 5456: ёмкость принята — %d ячеек (%d вкладок). Эталон здесь открывает окно ")
			TEXT("склада на вкладке %d и сбрасывает полосу; система «окно склада» (5500) не портирована"),
			NewMaxSlots, NewMaxSlots / 30, FMath::Max(0, NewMaxSlots / 30 - 1));
	}
	UE_LOG(LogAsdaInven, Log,
		TEXT("[склад] 5456 показано: нотис %d «%s», модалка ONEBUT_MSGBOX, чат=%s, принято=%s"),
		NoticeLocId, *Text, bAlsoChat ? TEXT("да") : TEXT("нет"), bAccepted ? TEXT("да") : TEXT("нет"));
}

// ★★★ 5007 — `netOnInventoryUseResult@0xD155A0` (network.c:18134). ПАКЕТ САМОДОСТАТОЧЕН:
//   никакого парного 5003 сервер НЕ шлёт (живой лог 2026-08-08 19:13:29: за 5007 не пришло
//   ни 5003, ни 4999 — именно поэтому «удаление не работало»: пакет уходил, сервер отвечал
//   result=1, а ячейка в UI оставалась). Раскладка тела (18 б), 1:1 из эталона:
//     +0  u8  result   (0 → нотис 6956, 1 → применить, 2 → нотис 8013 «занят»)
//     +1  i16 слот     (`*(__int16*)((char*)packet + 1)`)
//     +5  u8  контейнер(`BYTE1(packet[1])`: 1 = сумка, 2 = расходники, 4 = слот ковки/квеста)
//     +6  i32 ОСТАТОК  (`*(int*)((char*)&packet[1] + 2)`; ≤ 0 ⇒ слот очищается)
//     +12 i32 ДЕНЬГИ   (`uiSetHonorPointsDisplay(g_pGameClient, packet[3])`)
//     +16 i16 ВЕС      (`HUD_UpdateMiniWeightBar(g_pHUD, SLOWORD(packet[4]))`)
//   Проверка раскладки на живом пакете: [01|1A 00|00 00|02|00 00 00 00|00 00|62 37 76 3B|C6 2C]
//   → result 1, слот 26, контейнер 2, остаток 0, деньги 997 603 170, вес 11462 — числа сошлись
//   с суммой золота на экране юзера.
void UAsdaInventorySubsystem::ApplyDumpResult(const TArray<uint8>& Body18)
{
	// ★ 31.10.2026 [ИСПРАВЛЕНИЕ] (баг игроков «Нельзя удалить предмет» при отказе от задания): тот же 5007 сервер шлёт в ответ
	//   на ОТКАЗ ОТ ЗАДАНИЯ (5006 под-тип 4, `sendCharQuestDump@0x4DB510`). Эталон при типе 4 отдаёт пакет журналу
	//   (`uiCraftSlotConfirmRemove`), здесь — так же, до любых действий инвентаря; свои флаги ожидания инвентарь не трогает.
	//   Разбор и ключ отката `asda.QuestLog.AbandonAck` — `UAsdaQuestLogSubsystem::ConsumeItemDump5007`.
	if (UAsdaQuestLogSubsystem* QuestLog = GetGameInstance() ? GetGameInstance()->GetSubsystem<UAsdaQuestLogSubsystem>() : nullptr)
	{
		if (QuestLog->ConsumeItemDump5007(Body18, bDumpPending, ActionSentAtS)) { return; }
	}
	bDumpPending = false;
	bActionPending = false;
	if (Body18.Num() < 1) { return; }
	const uint8* P = Body18.GetData();
	const int32 Result = P[0];
	if (Result != 1)
	{
		// Эталон: 0 → нотис 6956, 2 → нотис 8013, прочее — молча.
		if (Result == 0)      { Notice(6956); }
		else if (Result == 2) { Notice(8013); }
		UE_LOG(LogAsdaInven, Log, TEXT("[инвентарь] 5007: уничтожение ОТКАЗАНО, result=%d"), Result);
		return;
	}
	if (Body18.Num() < 18)
	{
		UE_LOG(LogAsdaInven, Warning, TEXT("[инвентарь] 5007: тело %d б < 18 — слот не тронут"), Body18.Num());
		return;
	}
	const int32 SlotIndex = RdI16(P + 1);
	const int32 ContType  = P[5];
	const int32 Remain    = RdI32(P + 6);
	const int32 NewGold   = RdI32(P + 12);
	const int32 NewWeight = AsdaWeightFromWire(RdI16(P + 16));   // ★ 29.10.2026 (вес ×2): вес персонажа u16

	if (ContType == 4)
	{
		// `uiCraftSlotConfirmRemove(g_pQuestMgr, …)` — слот ковки/квеста, это НЕ инвентарь.
		UE_LOG(LogAsdaInven, Log, TEXT("[инвентарь] 5007: контейнер 4 (слот ковки/квеста) — окно инвентаря не трогаем"));
		return;
	}
	SetGoldDisplay(NewGold);
	if (UAsdaNetSubsystem* Net = GetGameInstance() ? GetGameInstance()->GetSubsystem<UAsdaNetSubsystem>() : nullptr)
	{
		Net->SetSelfWeight(NewWeight, -1, TEXT("5007 уничтожение"));
	}
	if (ContType == 1)
	{
		// Эталон: сумка — слот ЧИСТИТСЯ целиком (остаток не смотрится).
		ClearCell(1, SlotIndex);
	}
	else if (ContType == 2)
	{
		if (Remain < 1) { ClearCell(2, SlotIndex); }
		else if (Spend.IsValidIndex(SlotIndex))
		{
			Spend[SlotIndex].SetAmount(Remain);
			NotifyCell(2, SlotIndex);
		}
	}
	if (bWindowOpen) { RefreshGrid(); }
	UE_LOG(LogAsdaInven, Log,
		TEXT("[инвентарь] 5007: уничтожено — контейнер %d слот %d, остаток %d, деньги %d, вес %d"),
		ContType, SlotIndex, Remain, NewGold, NewWeight);
}

// Применение записи подобранного предмета (тело 5013 @3), уже вырезанной колбэком.
void UAsdaInventorySubsystem::ApplyLootRecord(const TArray<uint8>& Rec57)
{
	if (Rec57.Num() < FAsdaItemInstance::kRecordSize) { return; }
	// ★★★ Эталон `uiInventoryOnRecvItemAcquired@0xD10E80` (ui_core.c:47012) → `OnRecv_ItemUpdate`:
	//   запись КЛАДЁТСЯ в свой контейнер (поле +4) по своему слоту (+5) — и для золота тоже,
	//   а ПЕРЕД этим для кода 20551 вызывается `uiSetHonorPointsDisplay(g_pGameClient, Src+9)`,
	//   т.е. НОВАЯ ПОЛНАЯ СУММА приезжает прямо в записи.
	//   ⚠ Прежний порт для золота делал `RefreshMoney(); return;` — перечитывал СТАРУЮ запись
	//   модели и потому «золото не менялось при подборе с земли» (п.6 теста).
	const int32 Code = RdI32(Rec57.GetData());
	if (Code == kGoldItemCode) { SetGoldDisplay(RdI32(Rec57.GetData() + 9)); }
	PutRecord(Rec57.GetData(), /*ForcedContainer*/ 0);
	if (bWindowOpen) { RefreshGrid(); }
}

// ══════════════════ МАГАЗИН NPC: ПРИМЕНЕНИЕ РЕЗУЛЬТАТА В МОДЕЛЬ (окно 1300) ════════════════════
// ★ ДЕФЕКТ ОРИГИНАЛА Д1 (магазин). `Recv_ShopSell_Result@0xE28FD0` (network.c:28164-28200), ветка
//   успеха, для КАЖДОЙ из пяти записей делает так:
//        if (запись.контейнер == 2) { положить в g_InventoryArray2[слот]; перерисовать }
//        if (запись.контейнер == 1) { положить в g_InventoryBag[слот];            }
//        if (запись[+9] /*количество*/ >= 0) {
//            if (контейнер == 2) { положить ЕЩЁ РАЗ, перерисовать }
//            if (контейнер == 1) { memset(g_InventoryBag[слот], 0, 57); UI_OnInventoryRemove(...);
//                                  CInventoryUI_UpdateBagSlotIcon(код, слот); }
//        }
//   Условие `количество >= 0` истинно у любой купленной вещи (у снаряжения там 0 или 1), поэтому
//   ветка контейнера 1 ЗАТИРАЕТ ячейку, в которую строкой выше сама же положила запись: купленное
//   СНАРЯЖЕНИЕ в сумке не появляется до следующего полного батча 4045. Для контейнера 2 тот же
//   блок безвреден — там запись просто кладётся повторно.
//   ⇒ ИСПРАВЛЯЕМ (закон fix-original-defects): кладём запись в её контейнер и НЕ затираем.
static ASDA_DEV_CVAR(int32, CVarShopLegacyBuyClearsShell,
	TEXT("asda.Shop.LegacyBuyClearsShell"),
	0,
	TEXT("Ответ магазина 5021: 0 = купленная вещь остаётся в контейнере 1 (исправление Д1); "
	     "1 = как эталон — ячейка контейнера 1 сразу затирается нулями."),
	ECVF_Default);

void UAsdaInventorySubsystem::ApplyShopBuyRecords(const TArray<uint8>& Body406)
{
	if (Body406.Num() < 406) { return; }
	const uint8* P = Body406.GetData();
	// Деньги приезжают полной суммой (`uiSetHonorPointsDisplay(g_pGameClient, pkt[100])`).
	SetGoldDisplay(RdI32(P + 400));
	const bool bLegacy = CVarShopLegacyBuyClearsShell.GetValueOnGameThread() != 0;
	int32 Applied = 0;
	for (int32 i = 0; i < 5; ++i)
	{
		const uint8* Rec = P + 1 + 57 * i;
		const int32 Code = RdI32(Rec);
		if (Code <= 0) { continue; }              // эталон: `*(int*)(pkt + 57*i + 1) > 0`
		const int32 Cont = (int32)Rec[4];
		const int32 Slot = RdI16(Rec + 5);
		if (Cont != 1 && Cont != 2)
		{
			UE_LOG(LogAsdaInven, Warning,
				TEXT("[магазин] 5021 запись %d: контейнер %d не моделируется (код %d, слот %d)"), i, Cont, Code, Slot);
			continue;
		}
		PutRecord(Rec, /*ForcedContainer*/ 0);
		++Applied;
		if (bLegacy && Cont == 1)
		{
			// Точное поведение эталона (откат Д1): ячейка сумки сразу затирается.
			ClearCell(1, Slot);
			UE_LOG(LogAsdaInven, Log,
				TEXT("[магазин] 5021 запись %d: откат Д1 — ячейка 1/%d затёрта как в эталоне"), i, Slot);
		}
	}
	if (bWindowOpen) { RefreshGrid(); RefreshMoney(); }
	UE_LOG(LogAsdaInven, Log, TEXT("[магазин] 5021: применено записей %d, деньги %d"), Applied, GetGold());
}

void UAsdaInventorySubsystem::ApplyShopSellRefs(const TArray<uint8>& Body62)
{
	if (Body62.Num() < 62) { return; }
	const uint8* P = Body62.GetData();
	SetGoldDisplay(RdI32(P + 56));
	int32 Applied = 0;
	for (int32 i = 0; i < 5; ++i)
	{
		const uint8* Ref = P + 1 + 11 * i;
		const int32 Slot = RdI16(Ref + 0);
		if (Slot < 0) { continue; }               // эталон: `*(i16*)(pkt + 11*i + 1) >= 0`
		const int32 Cont = (int32)Ref[4];
		const int32 NewAmount = RdI32(Ref + 5);
		if (Cont == 1)
		{
			// Эталон: снаряжение продаётся ЦЕЛИКОМ — слот контейнера 1 чистится без взгляда на остаток.
			ClearCell(1, Slot);
		}
		else if (Cont == 2)
		{
			if (NewAmount <= 0) { ClearCell(2, Slot); }
			else if (Spend.IsValidIndex(Slot))
			{
				Spend[Slot].SetAmount(NewAmount);
				NotifyCell(2, Slot);
			}
		}
		else
		{
			UE_LOG(LogAsdaInven, Warning, TEXT("[магазин] 5019 запись %d: контейнер %d не моделируется"), i, Cont);
			continue;
		}
		++Applied;
	}
	if (bWindowOpen) { RefreshGrid(); RefreshMoney(); }
	UE_LOG(LogAsdaInven, Log, TEXT("[магазин] 5019: применено записей %d, деньги %d"), Applied, GetGold());
}

namespace {
// ═══════════════ СКЛАД (5009 / 5011) — раскладка и правила описаны у объявлений в .h ══════════
//   Одна запись «item-ref» обоих пакетов — 11 байт: `+0 i16` слот, `+2 i16` второй индекс,
//   `+4 u8` контейнер, `+5 i32` количество, `+9 i16` вес стека.
	// Перебить у 57-байтовой записи поля +5/+7/+4/+9/+14 значениями из item-ref — ровно те пять
	//   присваиваний, которые делают `inventorySwapSlots@0xE8A820` (item_inventory.c:58490-58494)
	//   и `inventorySyncSlotsFromPacket@0xE8ADE0` (item_inventory.c:58582-58585).
	//   `bWithSecond` = переносить ли поле +7 (у 5009 оно переносится, у 5011 — нет).
	void ApplyRefFieldsToRecord(FAsdaItemInstance& Rec, const uint8* Ref, int32 ForcedSlot, bool bWithSecond)
	{
		if (Rec.Raw.Num() < FAsdaItemInstance::kRecordSize) { return; }
		uint8* R = Rec.Raw.GetData();
		WrI16(R + 5, ForcedSlot);
		if (bWithSecond) { WrI16(R + 7, RdI16(Ref + 2)); }
		R[4] = Ref[4];
		WrI32(R + 9,  RdI32(Ref + 5));
		WrI16(R + 14, RdI16(Ref + 9));
		Rec.SyncFromRaw();
	}
}

int32 UAsdaInventorySubsystem::ApplyWarehouseKeepResult5009(const TArray<uint8>& Body113)
{
	if (Body113.Num() < 113) { return 0; }
	const uint8* P = Body113.GetData();
	int32 Applied = 0;

	// ── Проход 1: СКЛАД получает КОПИЮ записи инвентаря (эталон читает её ДО прохода 2) ──
	for (int32 i = 0; i < 5; ++i)
	{
		const uint8* InvRef = P + 1  + 11 * i;
		const uint8* StRef  = P + 56 + 11 * i;
		const int32 StSlot = RdI16(StRef + 0);
		if (StSlot < 0 || !Store.IsValidIndex(StSlot)) { continue; }
		const int32 InvSlot = RdI16(InvRef + 0);
		const int32 InvCont = (int32)InvRef[4];
		const FAsdaItemInstance* Src = GetCell(InvCont, InvSlot);
		if (!Src || Src->Raw.Num() < FAsdaItemInstance::kRecordSize)
		{
			UE_LOG(LogAsdaInven, Warning,
				TEXT("[склад] 5009 запись %d: источник контейнер %d слот %d не найден — пропуск"),
				i, InvCont, InvSlot);
			continue;
		}
		Store[StSlot].SetRaw(Src->Raw.GetData(), FAsdaItemInstance::kRecordSize);
		ApplyRefFieldsToRecord(Store[StSlot], StRef, StSlot, /*bWithSecond*/true);
		NotifyCell(5, StSlot);
		++Applied;
	}

	// ── Проход 2: ИНВЕНТАРЬ — новое количество либо очистка ячейки ──
	for (int32 j = 0; j < 5; ++j)
	{
		const uint8* InvRef = P + 1 + 11 * j;
		const int32 Slot = RdI16(InvRef + 0);
		if (Slot < 0) { continue; }
		const int32 Cont = (int32)InvRef[4];
		TArray<FAsdaItemInstance>* C = Container(Cont);
		if (!C || !C->IsValidIndex(Slot)) { continue; }
		const int32 NewAmount = RdI32(InvRef + 5);
		if (NewAmount >= 0)
		{
			(*C)[Slot].SetAmount(NewAmount);
			(*C)[Slot].SetStackWeight(RdI16(InvRef + 9));
			if ((*C)[Slot].ItemCode == kGoldItemCode) { SetGoldDisplay(NewAmount); }
			NotifyCell(Cont, Slot);
		}
		else
		{
			// ★ Количество < 0 ⇒ предмет ушёл на склад ЦЕЛИКОМ, ячейка чистится
			//   (эталон: `sub_93C9FB(Src)` + `sub_93C9FB(v11)`).
			ClearCell(Cont, Slot);
		}
		++Applied;
	}

	if (bWindowOpen) { RefreshGrid(); }
	RefreshMoney();
	UE_LOG(LogAsdaInven, Log, TEXT("[склад] 5009: применено записей %d, вес %d"), Applied, RdI16(P + 111));
	return Applied;
}

int32 UAsdaInventorySubsystem::ApplyWarehouseTakeResult5011(const TArray<uint8>& Body117)
{
	if (Body117.Num() < 117) { return 0; }
	const uint8* P = Body117.GetData();
	// Деньги приезжают ПОЛНОЙ суммой (комиссия уже списана сервером); эталон ставит их ВСЕГДА.
	SetGoldDisplay(RdI32(P + 111));
	int32 Applied = 0;

	// ── Проход 1: ИНВЕНТАРЬ получает КОПИЮ записи склада ──
	for (int32 i = 0; i < 5; ++i)
	{
		const uint8* InvRef = P + 1  + 11 * i;
		const uint8* StRef  = P + 56 + 11 * i;
		const int32 InvSlot = RdI16(InvRef + 0);
		if (InvSlot < 0) { continue; }
		const int32 Cont = (int32)InvRef[4];
		// Эталон разбирает ТОЛЬКО контейнеры 1 и 2 (две явные ветки).
		if (Cont != 1 && Cont != 2) { continue; }
		const int32 StSlot = RdI16(StRef + 0);
		TArray<FAsdaItemInstance>* C = Container(Cont);
		if (!C || !C->IsValidIndex(InvSlot) || !Store.IsValidIndex(StSlot)) { continue; }
		if (Store[StSlot].Raw.Num() < FAsdaItemInstance::kRecordSize)
		{
			UE_LOG(LogAsdaInven, Warning,
				TEXT("[склад] 5011 запись %d: ячейка склада %d пуста — копировать нечего"), i, StSlot);
			continue;
		}
		(*C)[InvSlot].SetRaw(Store[StSlot].Raw.GetData(), FAsdaItemInstance::kRecordSize);
		ApplyRefFieldsToRecord((*C)[InvSlot], InvRef, InvSlot, /*bWithSecond*/false);
		if ((*C)[InvSlot].ItemCode == kGoldItemCode) { SetGoldDisplay((*C)[InvSlot].Amount); }
		NotifyCell(Cont, InvSlot);
		++Applied;
	}

	// ── Проход 2: СКЛАД — остаток либо очистка (гейт эталона — по ссылке ИНВЕНТАРЯ) ──
	for (int32 j = 0; j < 5; ++j)
	{
		const uint8* InvRef = P + 1  + 11 * j;
		const uint8* StRef  = P + 56 + 11 * j;
		if (RdI16(InvRef + 0) < 0) { continue; }
		const int32 StSlot = RdI16(StRef + 0);
		if (!Store.IsValidIndex(StSlot)) { continue; }
		const int32 NewAmount = RdI32(StRef + 5);
		if (NewAmount == -1)
		{
			ClearCell(5, StSlot);
		}
		else
		{
			Store[StSlot].SetAmount(NewAmount);
			Store[StSlot].SetStackWeight(RdI16(StRef + 9));
			NotifyCell(5, StSlot);
		}
		++Applied;
	}

	if (bWindowOpen) { RefreshGrid(); }
	RefreshMoney();
	UE_LOG(LogAsdaInven, Log, TEXT("[склад] 5011: применено записей %d, деньги %d, вес %d"),
		Applied, GetGold(), RdI16(P + 115));
	return Applied;
}

// ═══════════════ РЕМОНТ (6572) — раскладка и правила описаны у объявления в .h ════════════════
int32 UAsdaInventorySubsystem::ApplyEquipRepairResult6572(const TArray<uint8>& Body170)
{
	if (Body170.Num() < 170) { return 0; }
	const uint8* P = Body170.GetData();
	// Деньги приезжают ПОЛНОЙ суммой (эталон `uiSetHonorPointsDisplay(g_pGameClient, *(int*)&v9[83])`).
	SetGoldDisplay(RdI32(P + 166));
	int32 Applied = 0;
	for (int32 i = 0; i < 41; ++i)
	{
		const int32 Slot = RdI16(P + 43 + 2 * i);
		if (Slot == -1) { continue; }                 // эталон: `!= -1`
		const int32 Cont = (int32)P[2 + i];
		const uint8 NewDur = P[125 + i];
		// ★ Границы слота у эталона РАЗНЫЕ для каждого контейнера — воспроизводим 1:1, а не
		//   «как получится»: 2 → `< 0x3C`, 1 → `<= 0x1D`, 3 → `<= 0xA`.
		//   ★ [ПОРТ] 29.10.2026 (инвентарь ×2): сумка 60 ⇒ граница контейнера 1 — доступная ёмкость (`GetContainerSize(1)`:
		//   60, либо 30 при `asda.Inven.ShellX2 0` — тогда ровно эталонное `<= 0x1D`).
		const bool bInRange =
			(Cont == 2 && Slot >= 0 && Slot < 60) ||
			(Cont == 1 && Slot >= 0 && Slot < GetContainerSize(1)) ||
			(Cont == 3 && Slot >= 0 && Slot <= 10);
		if (!bInRange) { continue; }
		TArray<FAsdaItemInstance>* C = Container(Cont);
		if (!C || !C->IsValidIndex(Slot)) { continue; }
		FAsdaItemInstance& It = (*C)[Slot];
		if (It.Raw.Num() < FAsdaItemInstance::kRecordSize) { continue; }
		It.Raw[13] = NewDur;                          // поле записи +13 — ТЕКУЩАЯ ПРОЧНОСТЬ
		It.SyncFromRaw();
		NotifyCell(Cont, Slot);
		++Applied;
	}
	if (bWindowOpen) { RefreshGrid(); RefreshMoney(); }
	UE_LOG(LogAsdaInven, Log, TEXT("[ремонт] 6572: обновлено слотов %d, деньги %d"), Applied, GetGold());
	return Applied;
}

// ═══════════ ВОЕННЫЙ МАГАЗИН (6163) — раскладка описана у объявления в .h ═════════════════════
void UAsdaInventorySubsystem::ApplyWarShopBuyRecords(const TArray<uint8>& Body121)
{
	if (Body121.Num() < 121) { return; }
	const uint8* P = Body121.GetData();
	SetGoldDisplay(RdI32(P + 3));
	int32 Applied = 0;
	for (int32 i = 0; i < 2; ++i)
	{
		const uint8* Rec = P + 7 + 57 * i;
		const int32 Cont = (int32)Rec[4];
		const int32 Slot = RdI16(Rec + 5);
		const int32 Amount = RdI32(Rec + 9);
		if (Slot < 0) { continue; }
		if (Amount == -1)
		{
			// Эталон: `memset(контейнер + 57*слот, 0, 0x39)` — цена израсходована целиком.
			ClearCell(Cont, Slot);
			++Applied;
			continue;
		}
		PutRecord(Rec, /*ForcedContainer*/ 0);
		++Applied;
	}
	if (bWindowOpen) { RefreshGrid(); RefreshMoney(); }
	UE_LOG(LogAsdaInven, Log, TEXT("[военный магазин] 6163: применено записей %d, деньги %d"), Applied, GetGold());
}

// ★★★ 6554 — две записи «свиток / снаряжение» (разбор веток см. у объявления в заголовке).
int32 UAsdaInventorySubsystem::ApplyItemOptionTransRecords(const TArray<uint8>& Body119, const TCHAR* Tag)
{
	if (Body119.Num() < 119) { return 0; }
	const uint8* P = Body119.GetData();
	int32 Applied = 0;
	for (int32 i = 0; i < 2; ++i)
	{
		const uint8* Rec  = P + 1 + 57 * i;
		const int32  Cont = (int32)Rec[4];
		const int32  Slot = RdI16(Rec + 5);
		const int32  Code = RdI32(Rec);
		if (Slot < 0) { continue; }        // эталон: `if (*(i16*)(pkt+6+57i) != -1)`
		if (Cont == 2)
		{
			PutRecord(Rec, /*ForcedContainer*/ 0);
			++Applied;
		}
		else if (Cont == 1)
		{
			ClearCell(1, Slot);
			if (Code > 0) { PutRecord(Rec, /*ForcedContainer*/ 0); }   // `Inventory_SetBagSlot`: только при коде > 0
			++Applied;
		}
		else
		{
			UE_LOG(LogAsdaInven, Warning,
				TEXT("%s: запись %d пришла с контейнером %d — эталон обрабатывает только 1 и 2"),
				Tag ? Tag : TEXT(""), i, Cont);
		}
	}
	// ★ 31.10.2026 (ревизия камней): эталон для КАЖДОЙ записи со слотом зовёт `CClass_BuildItemRecord(g_pQuickSlotBar, 2, запись)`
	//   (6554 network.c:8760, 6552 item_inventory.c:4880, 6550 network.c:9087) — иконки и счётчики панели быстрого доступа.
	//   Прежде общая точка этого не делала: свиток/камень с панели оставался на ней после списания.
	if (Applied > 0)
	{
		if (UAsdaHotbarSubsystem* Hb = GetGameInstance() ? GetGameInstance()->GetSubsystem<UAsdaHotbarSubsystem>() : nullptr)
		{
			Hb->RebuildSlots();
			Hb->RefreshPage();
		}
	}
	if (bWindowOpen) { RefreshGrid(); RefreshEquip(); }
	UE_LOG(LogAsdaInven, Log, TEXT("%s: применено записей %d"), Tag ? Tag : TEXT(""), Applied);
	return Applied;
}

int32 UAsdaInventorySubsystem::ApplyExtractShopRecords(const TArray<uint8>& Body184, bool bSuccess)
{
	if (Body184.Num() < 184) { return 0; }
	const uint8* P = Body184.GetData();
	int32 Applied = 0;

	// Одна запись ответа: контейнер (+4), слот (+5), код (+0).
	auto SlotOfRec = [](const uint8* R) { return RdI16(R + 5); };
	auto ContOfRec = [](const uint8* R) { return (int32)R[4]; };

	const uint8* RecSrc    = P + 1;      // источник (сервер его удалил)
	const uint8* RecDst    = P + 58;     // приёмник с новыми параметрами
	const uint8* RecScroll = P + 115;    // свиток после списания

	// 1) Источник «сгорел» — чистим его слот. Эталон: `memset(&g_InventoryBag + 57*body[3], 0, 0x39)`.
	{
		const int32 Slot = SlotOfRec(RecSrc);
		if (Slot >= 0) { ClearCell(1, Slot); ++Applied; }
	}
	// 2) Свиток. Эталон: `memcpy(&g_InventoryBag + 57*body[60], (char*)&body[57] + 1, 0x39)` —
	//    кладёт КАК ЕСТЬ, без гейта по коду: нулевая запись и означает «стопка кончилась».
	{
		const int32 Slot = SlotOfRec(RecScroll);
		const int32 Cont = ContOfRec(RecScroll);
		if (Slot >= 0 && Cont == 1) { PutRecord(RecScroll, /*ForcedContainer*/ 0); ++Applied; }
	}
	// 3) Приёмник — только в ветке успеха (в ветке 10 эталон эту запись вообще не трогает).
	if (bSuccess)
	{
		const int32 Slot = SlotOfRec(RecDst);
		const int32 Cont = ContOfRec(RecDst);
		if (Slot >= 0 && Cont == 1)
		{
			ClearCell(1, Slot);
			PutRecord(RecDst, /*ForcedContainer*/ 0);
			++Applied;
		}
	}
	// Новое золото эталон ставит в обеих ветках (`uiSetHonorPointsDisplay(g_pGameClient, &body[87])`).
	SetGoldDisplay(RdI32(P + 174));
	if (bWindowOpen) { RefreshGrid(); RefreshEquip(); }
	UE_LOG(LogAsdaInven, Log, TEXT("[передача параметров] 5434: применено записей %d (успех=%s)"),
		Applied, bSuccess ? TEXT("да") : TEXT("нет"));
	return Applied;
}
