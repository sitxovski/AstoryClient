// Copyright (c) 2026 @sitxovski (Discord)
// AsdaInventorySubsystem_Net.cpp — ★ 23.10.2026 часть UAsdaInventorySubsystem, вынесенная из AsdaInventorySubsystem.cpp БАЙТ В БАЙТ
//   (разнос по смысловым группам; доказательство — Tools/asda_split_cpp.py verify, план Reports/split_2026-10-23/AsdaInventorySubsystem.plan.json).
//   Сеть: приём (5001/5003/… результаты использования), коробки, гача, премиум/функциональные предметы, расширение склада,
//   магазин NPC, склад 5009/5011, ремонт 6572, военный магазин 6163.
//   #include — те же, что у AsdaInventorySubsystem.cpp, в том же порядке; общее — AsdaInventorySubsystemInternal.h.
#include "UI/AsdaInventorySubsystem.h"
#include "AsdaPacketWriter.h"   // ★ U6 (В1 дедупа №2): 15 байт слотов-приёмников 6096 через общий писатель
#include "UI/AsdaSowelSubsystem.h"         // ★ ветка САМОЦВЕТОВ правой панели (430…437 / 441…446 / 495 / 499)
// ★ обмен по таблице событий (двойной клик по предмету subType 202 → 6098)   ★ R3: комментарий снятого включения "Npc/AsdaNpcDialogSubsystem.h" (файл им не пользовался) — сохранён
#include "Components/CanvasPanel.h"
#include "Components/Button.h"
// ★ С-Р1: общий офлайн-гейт отладочных команд (RefuseDebugWhenOnline)   ★ R3: комментарий снятого включения "AsdaOutgoingGate.h" (файл им не пользовался) — сохранён
#include "AsdaInventorySubsystem_Net_Private.h"   // ★ R3-c 12.11.2026: общее для частей AsdaInventorySubsystem_Net*.cpp

// ★★ [ИСПРАВЛЕНИЕ] 29.10.2026: отказ надевания 5001 код 15 (не та профессия) и цвет кода 14 (см. ApplyMoveResult).
//   1 = как эталон: 20136 «…только для игрока <профессия предмета>» цветом −21248, код 14 тоже −21248; 0 = прежнее «самоцвет».
static ASDA_DEV_CVAR(int32, CVarEquipJobNotice,
	TEXT("asda.Inven.EquipJobNotice"),
	1,
	TEXT("Отказ 5001 код 15 (профессия не та): 1 = как оригинал — «Ты не можешь использовать этот предмет. Он предназначен "
	     "только для игрока <профессия>» (loc 20136 + 8438+профессия), цвет #FFAD00; код 14 (уровень) тем же цветом; "
	     "0 = прежнее поведение порта (строка 990468 «Предмет с самоцветом переместить нельзя.», код 14 золотым)."),
	ECVF_Default);

// ★★ [ИСПРАВЛЕНИЕ] 29.10.2026: предупреждение о пустых гнёздах при надевании (см. OnSelfEquipSlotChanged).
//   1 = как эталон: предметы с flagWord176 ≠ 0 (колчаны, наживка, заряды бура) молчат, цвета 7002/2033 — обработчика;
//   0 = прежнее поведение порта (2033 и на наживку/колчан, цвета маршрута).
static ASDA_DEV_CVAR(int32, CVarEquipSowelGate,
	TEXT("asda.Inven.EquipSowelGate"),
	1,
	TEXT("Предупреждение «нет живых камней» при надевании: 1 = как оригинал — только для предметов с flagWord176 == 0 "
	     "(колчан, наживка, заряд бура молчат), 7002 красным, 2033 цветом #FFAD00; 0 = прежнее поведение порта."),
	ECVF_Default);

// ══════════════════════════════ СЕТЬ: ПРИЁМ ════════════════════════════════════════════════════
void UAsdaInventorySubsystem::OnBatchReceived(int32 ContainerId, const TArray<uint8>& Raw)
{
	// ⚠ Закон `no-reentrant-net-callbacks`: из колбэка пакета — ТОЛЬКО в очередь.
	//   ★ Аудит №3 (B10): замок внутри общей очереди.
	FPendingBatch B;
	B.ContainerId = ContainerId;
	B.Raw = Raw;
	PendingBatches.Push(MoveTemp(B));
}

void UAsdaInventorySubsystem::OnBatchEnd(int32 ContainerId)
{
	FPendingBatch B;
	B.ContainerId = ContainerId;
	B.bEnd = true;
	PendingBatches.Push(MoveTemp(B));
}

void UAsdaInventorySubsystem::OnEquipFullReceived(const TArray<uint8>& Raw11x57)
{
	FPendingBatch B;
	B.ContainerId = 3;
	B.Raw = Raw11x57;
	PendingBatches.Push(MoveTemp(B));
}

// Включено ли одевание своей модели из контейнера 3 (определение — AsdaWorldSubsystem.cpp, ключ `asda.Char.SelfWornFromContainer`).
bool AsdaSelfWornFromContainerOn();

void UAsdaInventorySubsystem::ApplyPendingBatches()
{
	TArray<FPendingBatch> Batches;
	// ★ Аудит №3 (B10): прежний блок «под замком забрать всё разом и очистить», очередь пуста ⇒ выход.
	if (!PendingBatches.Drain(Batches)) { return; }
	// ⚠ На время разбора батча гасим ребилд хотбара: иначе он пересобирает 78 слотов на КАЖДУЮ
	//   из 60 записей. Один ребилд делаем в конце.
	TGuardValue<bool> Guard(bSuppressHotbarRefresh, true);
	bool bWornBatch = false;   // в этой пачке разобрано надетое (1027)

	for (const FPendingBatch& B : Batches)
	{
		if (B.bEnd)
		{
			// Нулевой пакет-маркер конца (4046 / 4049 / 4052).
			// ★★★ ЗДЕСЬ И ТОЛЬКО ЗДЕСЬ контейнер становится «полученным»: до маркера пустая ячейка
			//   означает «ещё не пришло», а не «предмета нет» (см. `IsContainerReceived`).
			ContainerReceivedMask |= (1 << B.ContainerId);
			int32 Used = 0;
			if (const TArray<FAsdaItemInstance>* C = Container(B.ContainerId))
			{
				for (const FAsdaItemInstance& I : *C) { if (!I.IsEmpty()) { ++Used; } }
			}
			UE_LOG(LogAsdaInven, Log, TEXT("[инвентарь] контейнер %d получен целиком: занято %d из %d"),
				B.ContainerId, Used, GetContainerSize(B.ContainerId));
			OnCellChanged.Broadcast(-1, -1);   // «перестроить всё»
			continue;
		}
		if (B.ContainerId == 3)
		{
			// ★ Надетое: 11 записей ПОДРЯД, ИНДЕКС = ЭКИП-СЛОТ (а не поле +5 записи!).
			//   `game_main.c:2807` копирует массив в `CCharacter+1588` как есть.
			const int32 Count = FMath::Min(kEquipSlots, B.Raw.Num() / FAsdaItemInstance::kRecordSize);
			for (int32 S = 0; S < Count; ++S)
			{
				const uint8* R = B.Raw.GetData() + FAsdaItemInstance::kRecordSize * S;
				Equip[S].SetRaw(R, FAsdaItemInstance::kRecordSize);
				Equip[S].SetContainerType(3);
				Equip[S].SetSlotIndex(S);
				NotifyCell(3, S);
			}
			// ★ Надетое приходит ОДНИМ пакетом 1027 — маркера конца у него нет, значит «получено»
			//   ровно в этот момент.
			ContainerReceivedMask |= (1 << 3);
			bWornBatch = true;
			// ★ Печатаем и ПОРЯДКОВЫЙ номер, и слот ИЗ САМОЙ ЗАПИСИ: ремонт (6571) шлёт серверу
			//   пару «контейнер, слот», и если эти числа расходятся, сервер отвечает «предмет
			//   не найден» (код 10). Без сверки причину не отличить от ошибки формата.
			{
				FString Dump;
				for (int32 S = 0; S < Count; ++S)
				{
					const uint8* R = B.Raw.GetData() + FAsdaItemInstance::kRecordSize * S;
					const int32 RawSlot = (int32)(int16)(R[5] | ((uint16)R[6] << 8));
					if (Equip[S].IsEmpty()) { continue; }
					Dump += FString::Printf(TEXT("%s[%d] код=%d слот-в-записи=%d"),
						Dump.IsEmpty() ? TEXT("") : TEXT(", "), S, Equip[S].ItemCode, RawSlot);
				}
				UE_LOG(LogAsdaInven, Log, TEXT("[инвентарь] надетое (1027) слоты: %s"), *Dump);
			}
			UE_LOG(LogAsdaInven, Log, TEXT("[инвентарь] надетое (1027): разобрано %d слотов из 11"), Count);
			continue;
		}
		int32 Applied = 0;
		for (int32 Off = 0; Off + FAsdaItemInstance::kRecordSize <= B.Raw.Num(); Off += FAsdaItemInstance::kRecordSize)
		{
			PutRecord(B.Raw.GetData() + Off, B.ContainerId);
			++Applied;
		}
		UE_LOG(LogAsdaInven, Log, TEXT("[инвентарь] батч контейнера %d: %d б → %d записей"),
			B.ContainerId, B.Raw.Num(), Applied);
	}
	if (bWindowOpen) { RefreshGrid(); RefreshEquip(); }
	RefreshMoney();
	// ★ 06.11.2026 [ОРИГИНАЛ] (`asda.Char.SelfWornFromContainer`): 1027 разобран, когда свой персонаж уже стоит (спавн мог
	//   одеть его только по снимку char-list) — сверить модель с авторитетом. Совпало — ничего не пересобирается.
	if (bWornBatch && AsdaSelfWornFromContainerOn())
	{
		UAsdaWorldSubsystem* WS = GetGameInstance() ? GetGameInstance()->GetSubsystem<UAsdaWorldSubsystem>() : nullptr;
		if (WS && IsValid(WS->LocalPlayerActor))
		{
			const FAsdaWornParts& P = WS->SelfParts;
			const int32 Have[7][2] = { { 0, P.Helmet }, { 1, P.Chest }, { 2, P.Pants }, { 3, P.Shoes }, { 4, P.Gloves }, { 8, P.Shield }, { 9, P.Weapon } };
			TArray<FIntPoint> Diff;   // слот → код из контейнера 3, расходящиеся со снимком модели
			for (const auto& H : Have)
			{
				const int32 Code = Equip[H[0]].IsEmpty() ? 0 : Equip[H[0]].ItemCode;
				if (Code != H[1]) { Diff.Add(FIntPoint(H[0], Code)); }
			}
			if (Diff.Num() > 0)
			{
				UE_LOG(LogAsdaInven, Log, TEXT("[инвентарь] надетое (1027): модель расходится в %d слотах — сверка"), Diff.Num());
				WS->ApplySelfEquipParts(Diff);
			}
		}
	}
	RefreshHotbarCache();   // ОДИН ребилд хотбара на весь батч (см. TGuardValue выше)
}

// ── Колбэки одиночных пакетов: ТОЛЬКО в очередь (закон `no-reentrant-net-callbacks`) ──
//    ★ Аудит №3 (B10): замок — внутри общей `TAsdaNetQueue`.
void UAsdaInventorySubsystem::OnMoveResult(const TArray<uint8>& Body24)
{
	PendingPackets.Push({ 5001, Body24 });
}

void UAsdaInventorySubsystem::OnSlotUpdate(const TArray<uint8>& Body14)
{
	PendingPackets.Push({ 5003, Body14 });
}

void UAsdaInventorySubsystem::OnSlotRemoved(const TArray<uint8>& Body59)
{
	PendingPackets.Push({ 4999, Body59 });
}

void UAsdaInventorySubsystem::OnUseResult(const TArray<uint8>& Body24)
{
	PendingPackets.Push({ 5029, Body24 });
}

void UAsdaInventorySubsystem::OnDumpResult(const TArray<uint8>& Body18)
{
	PendingPackets.Push({ 5007, Body18 });
}

void UAsdaInventorySubsystem::OnLootPicked(uint8 Result, int32 PickerUserNum, const TArray<uint8>& Body62, int32 /*NewWeight*/)
{
	// ★ 5013 несёт ПОЛНУЮ 57-байтовую запись подобранного предмета @3 (эталон
	//   `Net_HandleItemPickupResult@0xF45420` → `uiInventoryOnRecvItemAcquired(inv, a1 + 3)`).
	if (Result != 1 || Body62.Num() < 3 + FAsdaItemInstance::kRecordSize) { return; }
	// ★★★ 05.11.2026 — ПОДБОР ДРУГИМ ЧЛЕНОМ ОТРЯДА. При дележе добычи сервер (`CharItemPicking`, GameServer.c:84334-84341)
	//   шлёт 5013 ВСЕМ членам отряда: picker = userIdx подобравшего, запись = ЕГО ячейка. Эталон кладёт запись в сумку
	//   ТОЛЬКО в ветке «picker < 0 или это я» (network.c:42889-42892); у прочих — лишь строка 20250 в чат. Прежний порт
	//   клал чужую запись в свою модель → в сумке появлялся чужой предмет (призрак до следующего 4045/4048).
	if (PickerUserNum >= 0)
	{
		const UGameInstance* GI = GetGameInstance();
		const UAsdaNetSubsystem* Net = GI ? GI->GetSubsystem<UAsdaNetSubsystem>() : nullptr;
		if (!AsdaSelf::IsSelfUserIdx(Net, PickerUserNum)) { return; }
	}
	TArray<uint8> Rec;
	Rec.SetNumUninitialized(FAsdaItemInstance::kRecordSize);
	FMemory::Memcpy(Rec.GetData(), Body62.GetData() + 3, FAsdaItemInstance::kRecordSize);
	PendingPackets.Push({ 1, MoveTemp(Rec) });
}

// ★★★ 07.11.2026 [ИСПРАВЛЕНИЕ] (решение владельца; оригинал так не делает) СНЯТИЕ ВЕЩИ ВЕРХОМ ССАЖИВАЕТ ПО-НАСТОЯЩЕМУ.
//   Сервер и эталон при снятии транспорт не трогают (`CharPutOffEquip@0x4CD910` ни 5454, ни 6071 не шлёт). Новое правило: снятие,
//   подтверждённое 5001 итогом 1, клиент завершает командой «слезть» 5455; сервер отвечает 5454 + 6071 (`PopFunctionItemState_Toggle@0x4F1F60`
//   не отказывает никогда), дальше работает обычная высадка, пересчёт хода и рассылка соседям.
//   • только своя заявка `UnequipItem` (двойной клик, перетаскивание с куклы): серверные перекладки не считаются;
//   • слот должен остаться пустым: замена вещи другой (обмен 5001 подвид 1, надевание поверх) не ссаживает — сервер там транспорт тоже держит;
//   • отказ 5001 и молчание — 5455 нет, игрок верхом, как и на сервере; заявка снимается следующим ответом;
//   • запись (15, 2) заранее НЕ снимаем (закон `no-state-latch-before-shown`): пропавший 5454 оставит клиент верхом, как сервер, и следующее
//     снятие пошлёт команду снова; повтор внутри 2 с не шлётся — первый ответ ещё идёт;
//   • переход на другую карту: соединение прежней карты уже закрывается, команда не шлётся.
//   0 — как оригинал: снятие вещи транспорт не трогает.
static ASDA_DEV_CVAR(int32, CVarMountDismountOnUnequip,
	TEXT("asda.Mount.DismountOnUnequip"), 1,
	TEXT("[ИСПРАВЛЕНИЕ 07.11.2026] 1 = подтверждённое снятие вещи верхом шлёт «слезть» 5455; 0 = как оригинал (игрок остаётся верхом)."),
	ECVF_Default);

void UAsdaInventorySubsystem::DismountAfterUnequip(int32 EquipSlot)
{
	if (CVarMountDismountOnUnequip.GetValueOnGameThread() == 0) { return; }
	const FAsdaItemInstance* Now = GetCell(3, EquipSlot);
	if (Now && !Now->IsEmpty())
	{
		UE_LOG(LogAsdaInven, Log, TEXT("[транспорт] слот %d после ответа занят (замена вещи) — не ссаживаем"), EquipSlot);
		return;
	}
	UGameInstance* GI = GetGameInstance();
	UAsdaNetSubsystem* Net = GI ? GI->GetSubsystem<UAsdaNetSubsystem>() : nullptr;
	UAsdaEffectsSubsystem* Eff = GI ? GI->GetSubsystem<UAsdaEffectsSubsystem>() : nullptr;
	const int32 SelfNum = AsdaSelf::GetUserIdxOr(GI, INDEX_NONE);
	const FAsdaEffectEntry* Mount = (Eff && SelfNum != INDEX_NONE) ? Eff->FindByTypeSub(SelfNum, 15, 2) : nullptr;
	if (!Net || !Mount || Mount->ItemCode <= 0) { return; }   // не верхом (или уже нажато «слезть»)
	if (Net->bTransferInProgress)
	{
		UE_LOG(LogAsdaInven, Log, TEXT("[транспорт] снятие вещи во время перехода — 5455 не шлём, игрок верхом"));
		return;
	}
	const double NowS = FPlatformTime::Seconds();
	if (NowS - UnequipDismountSentS < 2.0)
	{
		UE_LOG(LogAsdaInven, Log, TEXT("[транспорт] снятие вещи: 5455 уже послан, ждём 5454"));
		return;
	}
	const bool bOk = Net->SendFunctionItemToggle5455(Mount->ItemCode);
	if (bOk) { UnequipDismountSentS = NowS; }
	UE_LOG(LogAsdaInven, Log, TEXT("[транспорт] снята вещь слота %d верхом → 5455 кодом %d %s"), EquipSlot, Mount->ItemCode, bOk ? TEXT("отправлен") : TEXT("НЕ отправлен"));
}

void UAsdaInventorySubsystem::ApplyMoveResult(const TArray<uint8>& Body24)
{
	if (Body24.Num() < 24) { return; }
	const uint8* P = Body24.GetData();
	const int32 Result  = P[0];
	const int32 SubKind = P[1];               // 1 = обмен местами, 2 = переезд в пустой слот
	const int32 SrcSlot = RdI16(P + 2);
	const int32 SrcType = P[6];
	const int32 DstSlot = RdI16(P + 13);
	const int32 DstType = P[17];

	// ★ Флаг ожидания снимается на ЛЮБОЙ результат (эталон `uiHandleResultCodeNotice@0xD603D0`).
	bActionPending = false;
	const int32 UnequipWas = PendingUnequipSlot;   // ★ 07.11.2026: заявка «снять» живёт ровно до этого ответа
	PendingUnequipSlot = -1;

	// ★ 06.11.2026 [ИСПРАВЛЕНИЕ] Тип 4 — ячейки журнала заданий (`itemMoveApply@0xD607C0` → `questSlotMoveSwap`), предметов здесь нет.
	//   Ключ `asda.QuestLog.SlotMove`, разбор — `UAsdaQuestLogSubsystem::ConsumeItemMove5001`.
	if (UAsdaQuestLogSubsystem* QuestLog = GetGameInstance() ? GetGameInstance()->GetSubsystem<UAsdaQuestLogSubsystem>() : nullptr)
	{
		if (QuestLog->ConsumeItemMove5001(Body24)) { return; }
	}

	if (Result != 1)
	{
		// Таблица нотисов эталона (отчёт B §2.2). Не перечисленные коды (18/100/…) — молча.
		static const TMap<int32, int32> kNotice = {
			{ 0, 2014 }, { 2, 2025 }, { 3, 2024 }, { 10, 2026 }, { 11, 2027 }, { 12, 2028 },
			{ 13, 2029 }, { 14, 2030 }, { 16, 2032 }, { 19, 2040 }, { 20, 110321 },
		};
		const bool bJobNotice = CVarEquipJobNotice.GetValueOnGameThread() != 0;
		const FColor kOrange(0xFF, 0xAD, 0x00, 0xFF);   // −21248 = 0xFFFFAD00 — цвет эталона для кодов 14 и 15
		if (Result == 14 && bJobNotice)
		{
			// ★ [ИСПРАВЛЕНИЕ] 29.10.2026: 2030 «уровень» эталон выводит цветом −21248, не золотым −11514 (ui_core.c:54235).
			AsdaNotice::ChatColored(GetGameInstance(), EAsdaMsgCategory::ItemRefused, LocText(2030), kOrange);
		}
		else if (const int32* Loc = kNotice.Find(Result))
		{
			Notice(*Loc);
		}
		else if (Result == 15 && bJobNotice)
		{
			// ★ [ИСПРАВЛЕНИЕ] 29.10.2026 (баг владельца: «Chess's Sword» 79722 на чужую профессию → «самоцвет»).
			//   Код 15 сервера = НЕ ТА ПРОФЕССИЯ: `sub_4CDAA0@0x4CDAA0` проверяет профессию (`sub_4CDDA0`, поле +10)
			//   раньше уровня (14), GameServer.c:81966-82001. Эталон `chatNoticeSowelItemBlocked@0xD62280` (chat.c:5888):
			//   код поднятого предмета (`this+8` — копия записи, `sub_D11820`); вне диапазона самоцветов →
			//   20136 «…предназначен только для игрока [ss1]», [ss1] = loc (8438 + usageCategory)
			//   (`uiSetLocalizedNoticeText`, ida.c:349200), цвет −21248. Самоцвет или нет записи — молча.
			//   Поднятый предмет у нас = исходная ячейка: сервер в отказе эхом кладёт запрос (GameServer.c:82446),
			//   а при отказе предмет остаётся на месте. Прежде порт выводил свою строку 990468 «самоцвет».
			UGameInstance* GI = GetGameInstance();
			const UAsdaItemTableSubsystem* IT = GI ? GI->GetSubsystem<UAsdaItemTableSubsystem>() : nullptr;
			const TArray<FAsdaItemInstance>* SrcCells = Container(SrcType);
			const int32 Code = (SrcCells && SrcCells->IsValidIndex(SrcSlot)) ? (*SrcCells)[SrcSlot].ItemCode : 0;
			const FAsdaItemRecord* Rec = (IT && Code > 0 && !IT->IsSowelCode(Code)) ? IT->GetItem(Code) : nullptr;
			if (Rec)
			{
				const FString Job = LocText(8438 + Rec->UsageCategory, TEXT(""));
				AsdaNotice::ChatColored(GI, EAsdaMsgCategory::ItemRefused, AsdaNotice::LocSlot(GI, 20136, Job), kOrange);
			}
			UE_LOG(LogAsdaInven, Log, TEXT("[инвентарь] 5001 result=15 (профессия): предмет %d, профессия предмета %d%s"),
				Code, Rec ? Rec->UsageCategory : -1, Rec ? TEXT("") : TEXT(" — молча (самоцвет/нет записи)"));
		}
		else if (Result == 15)
		{
			// asda.Inven.EquipJobNotice 0 — прежнее поведение порта.
			if (UAsdaMessageSubsystem* Msg = GetGameInstance() ? GetGameInstance()->GetSubsystem<UAsdaMessageSubsystem>() : nullptr)
			{
				Msg->PostLoc(EAsdaMsgCategory::ItemRefused, 990468);   // [ПОРТ] 09.10.2026 (мультиязычность, этап 3): «Предмет с самоцветом переместить нельзя.»
			}
		}
		else
		{
			UE_LOG(LogAsdaInven, Log, TEXT("[инвентарь] 5001 result=%d — эталон обрабатывает молча"), Result);
		}
		// ★ Ничего локально НЕ применяем: клиент оригинала ждёт ровно result == 1.
		// ★★★ ОЧЕРЕДЬ АВТОСОРТИРОВКИ НЕ СБРАСЫВАЕМ. Шаговый драйвер эталона
		//   `inventoryAutoArrangeStep@0xD35E60` обработки отказа НЕ ИМЕЕТ ВООБЩЕ: он лишь ждёт
		//   снятия флага ожидания (`this+0x1AC4`, ставится в `netSendInventoryMoveRequest@0xD35FB0`),
		//   двигает курсор `this+0x1ABC` и шлёт следующую пару. Прежний `ArrangeQueue.Reset()` был
		//   нашей отсебятиной и убивал сортировку на первом же отклонённом предмете.
		if (bArrangeRunning)
		{
			++ArrangeRefused;
			UE_LOG(LogAsdaInven, Warning,
				TEXT("[инвентарь] автосортировка: сервер отклонил пару (result=%d), пропускаем — "
				     "осталось %d пар (эталон делает так же)"), Result, ArrangeQueue.Num());
		}
		return;
	}

	// ── result == 1 → `itemMoveApply@0xD607C0`: применить перемещение локально ──
	TArray<FAsdaItemInstance>* SrcC = Container(SrcType);
	TArray<FAsdaItemInstance>* DstC = Container(DstType);
	if (!SrcC || !DstC || !SrcC->IsValidIndex(SrcSlot))
	{
		UE_LOG(LogAsdaInven, Warning, TEXT("[инвентарь] 5001: контейнеры/слоты вне диапазона (src %d:%d, dst %d:%d)"),
			SrcType, SrcSlot, DstType, DstSlot);
		return;
	}
	// Снятие (3→1 / 3→2): сервер сам выбрал место, `dstSlot` в ответе — уже КОНЕЧНЫЙ слот.
	int32 RealDst = DstSlot;
	if (!DstC->IsValidIndex(RealDst))
	{
		UE_LOG(LogAsdaInven, Warning, TEXT("[инвентарь] 5001: dst-слот %d вне контейнера %d"), RealDst, DstType);
		return;
	}

	// ★ Что стояло в этих ячейках ДО применения — нужно нотисам «надел/снял» и смене модели.
	const int32 OldSrcCode = (*SrcC)[SrcSlot].ItemCode;   // предмет, который уезжает

	// ★ [ИСПРАВЛЕНИЕ 07.11.2026] Слоты панели следуют за предметом — ДО перестановки, как в `itemMoveApply@0xD607C0`: пересборка из
	//   `NotifyCell` ниже читает уже новые ячейки. Контейнер 1/2/3 и подтип записи слота совпадают (`HotbarSubTypeToContainer`).
	if (UAsdaHotbarSubsystem* Hb = GetGameInstance() ? GetGameInstance()->GetSubsystem<UAsdaHotbarSubsystem>() : nullptr)
	{
		Hb->FollowItemMove(SrcType, SrcSlot, DstType, RealDst, /*bSwap*/ SubKind == 1 && !(*DstC)[RealDst].IsEmpty());
	}

	FAsdaItemInstance Tmp = (*DstC)[RealDst];
	(*DstC)[RealDst] = (*SrcC)[SrcSlot];
	(*DstC)[RealDst].SetContainerType((uint8)DstType);
	(*DstC)[RealDst].SetSlotIndex(RealDst);
	if (SubKind == 1 && !Tmp.IsEmpty())
	{
		// Обмен местами: содержимое цели переезжает в источник.
		(*SrcC)[SrcSlot] = Tmp;
		(*SrcC)[SrcSlot].SetContainerType((uint8)SrcType);
		(*SrcC)[SrcSlot].SetSlotIndex(SrcSlot);
	}
	else
	{
		(*SrcC)[SrcSlot].Clear();
		(*SrcC)[SrcSlot].SetContainerType((uint8)SrcType);
		(*SrcC)[SrcSlot].SetSlotIndex(SrcSlot);
	}
	NotifyCell(SrcType, SrcSlot);
	NotifyCell(DstType, RealDst);
	if (bWindowOpen) { RefreshGrid(); RefreshEquip(); }
	UE_LOG(LogAsdaInven, Log, TEXT("[инвентарь] 5001 применено: %s (%d:%d → %d:%d)"),
		SubKind == 1 ? TEXT("обмен местами") : TEXT("переезд"), SrcType, SrcSlot, DstType, RealDst);

	// ══════ ★★★ КОНТЕЙНЕР 3 ЗАТРОНУТ ⇒ МЕНЯЕМ МОДЕЛЬ И ПИШЕМ НОТИСЫ (equip_change_fix.md) ══════
	//   Эталон делает это ровно здесь же, внутри применения 5001:
	//     • приёмник 3 (НАДЕЛ)  — `inventoryApplySlotItem@0xD60BA0` case 3: чат-нотис **20134**
	//       цветом −11514 (#FFD306) + `itemUI_ApplyEquipWithColor@0xC6B5B0` (модель + предупреждение
	//       о пустых гнёздах самоцветов);
	//     • источник 3 (СНЯЛ)   — `slotItemDrop@0xD60EB0` case 3: чат-нотис **20135** тем же цветом
	//       + `cAvatar_PreviewPartModel@0xC6B8A0` (снятие части модели).
	//   Порядок эталона: сначала приёмник, потом источник — держим тот же.
	//   ⚠ Сервер СЕБЕ 4021/4023 НЕ ШЛЁТ (`SendPacket_ClippingSectorInOtherUser@0x41EA80` пропускает
	//   отправителя: `if (v14 != a3)`), поэтому иного источника смены своей внешности нет.
	if (DstType == 3) { OnSelfEquipSlotChanged(RealDst, OldSrcCode, /*bPutOn*/true); }
	if (SrcType == 3) { OnSelfEquipSlotChanged(SrcSlot, OldSrcCode, /*bPutOn*/false); }
	// ★ 07.11.2026 (`asda.Mount.DismountOnUnequip`): подтверждённая своя заявка «снять» — после модели, команда идёт за снятием.
	if (SrcType == 3 && DstType != 3 && SrcSlot == UnequipWas) { DismountAfterUnequip(SrcSlot); }
}

// ★★★ ОДНА ТОЧКА «слот экипировки изменился»: нотис + модель. `bPutOn` = надели (иначе сняли).
//   `ItemCode` — предмет, который НАДЕЛИ (при bPutOn) либо который СНЯЛИ (иначе): эталону нужен
//   именно он, а не текущее содержимое ячейки (при обмене там уже лежит другой).
void UAsdaInventorySubsystem::OnSelfEquipSlotChanged(int32 EquipSlot, int32 ItemCode, bool bPutOn)
{
	UGameInstance* GI = GetGameInstance();
	UAsdaItemTableSubsystem* IT = GI ? GI->GetSubsystem<UAsdaItemTableSubsystem>() : nullptr;
	UAsdaMessageSubsystem* Msg = GI ? GI->GetSubsystem<UAsdaMessageSubsystem>() : nullptr;
	const FString Name = IT ? IT->GetItemName(ItemCode) : FString();

	// ── Нотис «[ss1]: предмет помещен в ячейку.» / «[ss1]: предмет вынут.» ──
	//   Эталон: `CObject_SetSlotString(g_pLocNoticeCtx, 0, имя, 0)` + `UI_FormatLocalizedNotice(…,
	//   20134|20135, …)` → `CChat_AddTokenizedLines(g_pChatWnd, …, −11514, 10000, 0)`, то есть
	//   СИСТЕМНЫЙ ЧАТ, цвет #FFD306, TTL 10 с. У нас это категория `Loot` (тот же маршрут).
	if (Msg && ItemCode > 0)
	{
		Msg->PostLocFmt(EAsdaMsgCategory::Loot, bPutOn ? 20134 : 20135, { Name }, {});
	}

	// ── Предупреждение о пустых гнёздах самоцветов (ТОЛЬКО при надевании) ──
	//   Эталон `itemUI_ApplyEquipWithColor@0xC6B5B0` (item_inventory.c:22913-22949): если у ТОЛЬКО ЧТО
	//   надетой вещи все четыре гнезда (`запись+16/18/20/22`) пусты и её `itemType` вне 5…7, то
	//     • слот 9 (оружие) и НЕ (itemSubType == 10 && itemVariant ∈ {1,2}) — удочка/бур молчат —
	//       нотис **7002** цветом −65536 (#FF0000);
	//     • любой другой слот — нотис **2033** цветом −21248 (#FFAD00).
	//   ★ [ИСПРАВЛЕНИЕ] 29.10.2026 (баг владельца: «Без живых камней…» при надетой удочке): эталон зовёт
	//     `itemUI_ApplyEquipWithColor` ТОЛЬКО при `flagWord176 == 0` (`inventoryApplySlotItem` case 3, item_inventory.c:50070).
	//     Ненулевое поле — у всех настоящих предметов типа 10 слота 10 (колчаны, наживка 204, заряды бура 36): их надевание
	//     предупреждения не даёт. Порт этого условия не проверял, и наживка к удочке (слот 10 ≠ 9) выводила 2033.
	//     Цвета — обработчика эталона: 7002 −65536 (#FF0000), 2033 −21248 (#FFAD00). Ключ `asda.Inven.EquipSowelGate` (0 = прежнее).
	if (bPutOn && ItemCode > 0)
	{
		const FAsdaItemInstance* Now = GetCell(3, EquipSlot);
		const FAsdaItemRecord* Rec = IT ? IT->GetItem(ItemCode) : nullptr;
		const bool bGate = CVarEquipSowelGate.GetValueOnGameThread() != 0;
		const bool bAccessory = (Rec && Rec->ItemType >= 5 && Rec->ItemType <= 7);
		const bool bNoSowels = Now && Now->Sowel0 <= 0 && Now->Sowel1 <= 0 && Now->Sowel2 <= 0 && Now->Sowel3 <= 0;
		const bool bSkipFlag = bGate && (!Rec || Rec->FlagWord176 != 0);   // эталон: нет записи или флаг ≠ 0 — проверки нет
		if (Msg && Now && !bAccessory && bNoSowels && !bSkipFlag)
		{
			const bool bTool = Rec && Rec->ItemSubType == 10 && (Rec->ItemVariant == 1 || Rec->ItemVariant == 2);
			if (EquipSlot == 9)
			{
				if (!bTool)
				{
					if (bGate) { AsdaNotice::ChatColored(GI, EAsdaMsgCategory::ItemRefused, LocText(7002), FColor(0xFF, 0x00, 0x00, 0xFF)); }
					else       { Msg->PostLoc(EAsdaMsgCategory::ItemRefused, 7002); }
				}
			}
			else
			{
				if (bGate) { AsdaNotice::ChatColored(GI, EAsdaMsgCategory::ItemRefused, LocText(2033), FColor(0xFF, 0xAD, 0x00, 0xFF)); }
				else       { Msg->PostLoc(EAsdaMsgCategory::ItemRefused, 2033); }
			}
		}
	}

	// ── МОДЕЛЬ. Слот контейнера 3 == `ItemRecord->itemType` (сервер сверяет их в `CharPutOnEquip`),
	//   поэтому номер слота и есть номер части в `tagCHARACTER_SetPartsModelByType@0x9E18D0`.
	//   Берём АКТУАЛЬНОЕ содержимое ячейки: при обмене «надел вместо надетого» там уже новый предмет,
	//   при снятии — пусто (0 = «убрать часть»).
	if (UAsdaWorldSubsystem* WS = GI ? GI->GetSubsystem<UAsdaWorldSubsystem>() : nullptr)
	{
		const FAsdaItemInstance* Now = GetCell(3, EquipSlot);
		const int32 NowCode  = (Now && !Now->IsEmpty()) ? Now->ItemCode : 0;
		const int32 NowGrade = (Now && !Now->IsEmpty()) ? (int32)Now->GemGrade : 0;
		WS->ApplySelfEquipPart(EquipSlot, NowCode, NowGrade);
	}
}

void UAsdaInventorySubsystem::ApplySlotUpdate(const TArray<uint8>& Body14)
{
	if (Body14.Num() < 14) { return; }
	const uint8* P = Body14.GetData();
	const int32 Result    = P[0];
	const int32 SlotIndex = RdI16(P + 1);
	const int32 ContType  = P[5];
	const int32 NewAmount = RdI32(P + 6);
	const int32 NewStackW = RdI16(P + 10);

	if (Result != 1)
	{
		Notice(Result == 2 ? 8013 : 2041);
		return;
	}
	TArray<FAsdaItemInstance>* C = Container(ContType);
	if (!C || !C->IsValidIndex(SlotIndex)) { return; }
	FAsdaItemInstance& I = (*C)[SlotIndex];

	if (ContType == 1)
	{
		// Эталон: контейнер 1 → слот ОЧИЩАЕТСЯ полностью.
		ClearCell(1, SlotIndex);
	}
	else
	{
		const bool bGold = (I.ItemCode == kGoldItemCode);
		if (NewAmount > 0)
		{
			I.SetAmount(NewAmount);
			I.SetStackWeight(NewStackW);
			NotifyCell(ContType, SlotIndex);
		}
		else
		{
			ClearCell(ContType, SlotIndex);
		}
		// ★ Эталон `Recv_ItemEquipStat@0xD0F350`: у слота 20551 новая сумма уходит в поле денег
		//   даже когда она стала нулём (слот при этом очищается).
		if (bGold) { SetGoldDisplay(FMath::Max(0, NewAmount)); }
	}
	if (bWindowOpen) { RefreshGrid(); RefreshEquip(); }
	RefreshMoney();
}

void UAsdaInventorySubsystem::ApplySlotRemoved(const TArray<uint8>& Body59)
{
	if (Body59.Num() < 59) { return; }
	const uint8* P = Body59.GetData();
	const int32 ContType  = P[4];
	const int32 SlotIndex = RdI16(P + 5);
	ClearCell(ContType, SlotIndex);
	if (bWindowOpen) { RefreshGrid(); RefreshEquip(); }
	RefreshMoney();
}
