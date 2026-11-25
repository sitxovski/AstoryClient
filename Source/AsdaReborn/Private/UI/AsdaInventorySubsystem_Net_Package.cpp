// Copyright (c) 2026 @sitxovski (Discord)
// AsdaInventorySubsystem_Net_Package.cpp — ★ R3-c 12.11.2026 (разрез AsdaInventorySubsystem_Net.cpp, дословный перенос): обёртки приёма 6096/гача/функции/ремонт, прочность 6570, коробки (резерв ячеек, 6096) и гача.
//   Общее для частей — в AsdaInventorySubsystem_Net_Private.h; шапка и реверс — в AsdaInventorySubsystem_Net.cpp.
#include "UI/AsdaInventorySubsystem.h"
#include "AsdaPacketWriter.h"   // ★ U6 (В1 дедупа №2): 15 байт слотов-приёмников 6096 через общий писатель
#include "UI/AsdaHudSubsystem.h"       // ★ анонс редкой добычи из КОРОБКИ (NotifyItemInfo → 6616), наше добавление
#include "UI/AsdaCraftSubsystem.h"         // ★ волна В3 ремесла: единый предикат «окно ремесла открыто» (`IsAnyCraftWindowOpen`)
#include "UI/AsdaSowelSubsystem.h"         // ★ ветка САМОЦВЕТОВ правой панели (430…437 / 441…446 / 495 / 499)
// ★ обмен по таблице событий (двойной клик по предмету subType 202 → 6098)   ★ R3: комментарий снятого включения "Npc/AsdaNpcDialogSubsystem.h" (файл им не пользовался) — сохранён
#include "UI/AsdaPackagePanelSubsystem.h"  // ★ панель состава пакета 26500 (двойной клик открывает ЕЁ)
#include "UI/AsdaBoosterPanelSubsystem.h"  // ★ окно барабана гачи 10050 (двойной клик открывает ЕГО)
#include "Items/AsdaItemLevelGate.h"       // ★ [ИСПРАВЛЕНИЕ] 29.10.2026: минимальный уровень коробки и бустера
#include "Items/AsdaChargeStack.h"         // ★ [ИСПРАВЛЕНИЕ] 01.11.2026 (P100_ChargeStack): подбор ячеек коробки со стопками зарядов
// ★ С-Р1: общий офлайн-гейт отладочных команд (RefuseDebugWhenOnline)   ★ R3: комментарий снятого включения "AsdaOutgoingGate.h" (файл им не пользовался) — сохранён
#include "World/AsdaZoneSubsystem.h"        // ★ 24.10.2026 (выход из сидения 1:1): бит города 0x80 клетки своего персонажа
#include "UI/AsdaSystemOptionSubsystem.h"      // ★ 25.10.2026 (ревизия sit108, М-3): гейт «персонаж занят» (`objStateAllowsRender`) до стопа сцены
#include "AsdaInventorySubsystem_Net_Private.h"   // ★ R3-c 12.11.2026: общее для частей AsdaInventorySubsystem_Net*.cpp

// ★ Д9 (просьба ЮЗЕРА). Эталон красит «+N%» в ячейке ФИКСИРОВАННЫМ цветом:
//   `CControl_SetField923(-3584)` = 0xFFFFF200 (item_inventory.c:35961 и 42406/42447).
//   Юзер просил «под цвет грейда» — это таблица `itemGetGradeColor@0xD29940`
//   (деф 0xFFFEF078, 5 → белый, 6 → 0xFFFFFF46, 7 → 0xFFFF96DC, 8 → 0xFF96FF32, 9 → 0xFFFF7800),
//   та же, которой эталон красит «+N%» в ЗАГОЛОВКЕ тултипа. Для заточек 0..4 цвета почти совпадают,
//   расходятся только на ≥5 — поэтому отступление мягкое и полностью откатываемое.
// ★★★ НАШЕ ОТСТУПЛЕНИЕ ОТ ОРИГИНАЛА (решение юзера 2026-08-13): АНОНС РЕДКОЙ ДОБЫЧИ ИЗ КОРОБКИ.
//   В ОРИГИНАЛЕ анонса у коробок НЕТ — доказано ДВУМЯ методами (`false-negatives-method`):
//     1) тело приёмника `netOnStorageResultPacket@0xDABFD0` (`etalon/network.c:24651-24783`)
//        прочитано целиком: закрывает панель, чистит ячейку коробки, раскладывает 5 записей,
//        обновляет вес, показывает нотис 99330 — `CItemManager_NotifyItemInfo` / 6616 / 6129
//        не вызываются ни разу;
//     2) полный скан `.text` клиента на `E8/E9 rel32` даёт РОВНО ЧЕТЫРЕ колл-сайта трамплина
//        `0x92B273 → CItemManager_NotifyItemInfo@0xD0C8F0`: `0xB70874` (премиум-гача),
//        `0xD11228` (подбор 5013), `0xD20B8E` (кнопка BOOSTER_OK гачи), `0xFE7538` (заточка).
//        Адреса `0xDABFD0` среди них нет; косвенных вызовов через vtable тоже нет (ссылок на
//        `0xD0C8F0`/`0x92B273` как на dword в `.rdata`/`.data` — ноль).
//   Юзер попросил анонс всё-таки сделать. Делаем ТЕМ ЖЕ ПУТЁМ, что у гачи: гоняем каждую
//   выданную запись через `UAsdaHudSubsystem::NotifyItemInfo` — критерий редкости ОДИН на все
//   источники (грейд ≥ 3 И (тип ≤ 9 ИЛИ 11/200); у самоцветов только грейд), своей «редкости»
//   не заводим.
//   ⚠ ПРОТИВ СПАМА: коробка выдаёт до ПЯТИ записей за раз. Поэтому контейнер/слот в
//   `NotifyItemInfo` НЕ передаём — третий пакет ветки (6129 «похвастаться предметом всем
//   вокруг») для коробок не шлём, иначе один клик = до пяти лишних широковещательных пакетов.
//   Сам 6616 уходит только по записям, прошедшим фильтр (обычно 0–1 из пяти).
//   Серверная защита от повтора этому не мешает: `SecretCheck@0x524390` требует СТРОГО
//   БОЛЬШЕГО счётчика (`if (tUser <= сохранённый) → Secret Error`), а клиент инкрементирует
//   `ObfCounter` на КАЖДЫЙ защищённый пакет (`FillAntiReplaySecret`), поэтому подряд идущие
//   6616 проходят все.
static ASDA_DEV_CVAR(int32, CVarAnnounceFromPackage,
	TEXT("asda.Hero.Announce.FromPackage"),
	1,
	TEXT("Анонсировать редкую добычу, выпавшую ИЗ КОРОБКИ/ПАКЕТА (6097): "
	     "1 = да, тем же путём NotifyItemInfo → 6616 (наше добавление, решение юзера 2026-08-13); "
	     "0 = как в оригинале — коробки не анонсируются вовсе."),
	ECVF_Default);

// ═══════════════════════════════════════════════════════════════════════════════════════════════
// ★★★ ВОЛНА 2 — ТРИ КАНАЛА ПРИМЕНЕНИЯ, ИДУЩИЕ МИМО 5028/5029
//     A. КОРОБКИ/ПАКЕТЫ   6096 → 6097   (5 045 кодов — самый массовый применяемый класс)
//     B. ГАЧА             5468 → 5469 (+ 5474 → 5473 соседям)
//     C. ПРЕМИУМ/ФУНКЦ.   5450 / 5455 → 5451 / 5452 / 5453 / 5454 / 5456 / 5457 / 5459
//   Четвёртый канал волны 2 — «святые» предметы 5031 → 5032 — уже реализован в `UAsdaSowelSubsystem`
//   и переписки не потребовал: разбирательство показало, что 5031 это ОДИН поток (применение
//   расходника к САМОЦВЕТУ), а формулировка «свитки заточки на экипировку» в плане была неверной.
// ═══════════════════════════════════════════════════════════════════════════════════════════════
void UAsdaInventorySubsystem::OnPackageResult(const TArray<uint8>& Body300) { PendingPackets.Push({ 6097, Body300 }); }
void UAsdaInventorySubsystem::OnGachaResult  (const TArray<uint8>& Body68)  { PendingPackets.Push({ 5469, Body68  }); }
void UAsdaInventorySubsystem::OnFunctionResult(const TArray<uint8>& Body68) { PendingPackets.Push({ 5451, Body68  }); }
void UAsdaInventorySubsystem::OnItemRepaired (const TArray<uint8>& Entry57) { PendingPackets.Push({ 5457, Entry57 }); }
void UAsdaInventorySubsystem::OnSlotDurability6570(const TArray<uint8>& Body4) { PendingPackets.Push({ 6570, Body4 }); }

// ★ 08.10.2026 (рыбалка В1): `netApplySlotStateFlag@0xEDB150` (game.c:516053) — 1:1 по веткам эталона:
//   контейнер 2 и слот < 0x3C → `byte_290200B[57*слот]` (= +13 записи расходников);
//   контейнер 1 и слот <= 0x1D → `g_InvBag_FieldB + 57*слот` (= +13 записи сумки);
//   контейнер 3 и 0 ≤ слот ≤ 10 → `gap_636[57*слот + 5]` своего персонажа (= +13 записи снаряжения; та же
//   формула, что у ремонта 6572 — `ApplyEquipRepairResult6572`). Нотисов эталон не выводит.
//   Модель прочности — сырой байт `Raw[13]` + `SyncFromRaw` (второй копии поля в порту нет).
void UAsdaInventorySubsystem::ApplySlotDurability6570(const TArray<uint8>& Body4)
{
	if (Body4.Num() < 4) { return; }
	const int32 Cont = (int32)Body4[0];
	const int32 Slot = RdI16(Body4.GetData() + 1);
	const uint8 NewDur = Body4[3];
	const bool bInRange =
		(Cont == 2 && Slot >= 0 && Slot < 60) ||
		(Cont == 1 && Slot >= 0 && Slot < GetContainerSize(1)) ||   // ★ [ПОРТ] 29.10.2026 (инвентарь ×2): эталон `<= 0x1D` (сумка 30)
		(Cont == 3 && Slot >= 0 && Slot <= 10);
	if (!bInRange)
	{
		UE_LOG(LogAsdaInven, Log, TEXT("[износ] 6570: контейнер %d слот %d — вне веток эталона, пропуск"), Cont, Slot);
		return;
	}
	TArray<FAsdaItemInstance>* C = Container(Cont);
	if (!C || !C->IsValidIndex(Slot)) { return; }
	FAsdaItemInstance& It = (*C)[Slot];
	if (It.Raw.Num() < FAsdaItemInstance::kRecordSize) { return; }
	const int32 Was = (int32)It.Raw[13];
	It.Raw[13] = NewDur;                          // поле записи +13 — ТЕКУЩАЯ ПРОЧНОСТЬ
	It.SyncFromRaw();
	NotifyCell(Cont, Slot);
	if (bWindowOpen) { RefreshGrid(); RefreshEquip(); }
	UE_LOG(LogAsdaInven, Log, TEXT("[износ] 6570: контейнер %d слот %d код %d прочность %d → %d"),
		Cont, Slot, It.ItemCode, Was, (int32)NewDur);
}

// ───────────────────────────── A. КОРОБКИ И ПАКЕТЫ ────────────────────────────────────────────
//
// ★★★ ПОДБОР СЛОТОВ-ПРИЁМНИКОВ — ПОРТ `itemFindSowelSlots@0xDAACF0` (item_inventory.c:53642-53936).
//   Имя эталона ВРЁТ: к самоцветам функция отношения не имеет, она резервирует до пяти ячеек под
//   содержимое коробки. Правила (для каждой из ПЯТИ позиций состава, строго по порядку таблицы):
//     вид ячейки: запись `item_list` ⇒ `itemType <= 9 || categoryFlag == 3` ? СУМКА(1) : РАСХОД(2);
//                 запись `special_itemlist` ⇒ всегда СУМКА(1); запись `sowell_list` ⇒ РАСХОД(2);
//                 ни одной записи ⇒ позиция не используется (контейнер 0xFF).
//     СУМКА  — первый свободный из 30, ещё не занятый предыдущими позициями;
//     РАСХОД — сначала СТЕК с тем же кодом (только если `tradableFlag == 1 && itemType != 10`),
//              иначе первый свободный из 60, ещё не занятый предыдущими позициями.
//   Не нашли места ⇒ слот остаётся −100 — по этому признаку эталон и считает нехватку
//   (`itemHasNoEmptySowelSlots@0xDAAAB0`, имя тоже врёт: true = «мест ХВАТАЕТ»).
//   ★★★ [ИСПРАВЛЕНИЕ] 01.11.2026 (клиент 5.1.4, P100_ChargeStack; шапка — `Items/AsdaChargeStack.h`): тело вынесено в
//     `AsdaPackageReserve::Compute` (его же зовёт самотест `asda.SelfTest.ChargeStackReserve` с подставным составом);
//     ключ `asda.Item.ChargeStackReserve` = 1 — складываемый спецпредмет со стопкой того же ключа ячейку не требует.
// Ключ `asda.Package.EmptyRowShowsPanel` (определение и разбор — AsdaPackagePanelSubsystem.cpp; аксессор, а не поиск по имени).
bool AsdaPackageEmptyRowShowsPanelOn();

bool UAsdaInventorySubsystem::ReservePackageSlots(int32 ItemCode, TArray<uint8>& OutSlots15,
                                                  int32& OutCount, int32& OutShortSpend, int32& OutShortBag) const
{
	OutSlots15.Reset();
	OutSlots15.SetNumZeroed(15);
	OutCount = 0; OutShortSpend = 0; OutShortBag = 0;

	UGameInstance* GI = GetGameInstance();
	UAsdaItemTableSubsystem* IT = GI ? GI->GetSubsystem<UAsdaItemTableSubsystem>() : nullptr;
	if (!IT) { return false; }
	TArray<FAsdaPackageEntry> Contents;
	// ★ 11.11.2026 (А1) [ОРИГИНАЛ] найденная, но ПУСТАЯ строка packagetable: эталон `itemHasNoEmptySowelSlots`
	//   (item_inventory.c:53615-53628) зовёт `itemFindSowelSlots`, та возвращает 1 (:53936) — «место есть», и 6096
	//   уходит с числом позиций 0 (сервер `Recv_OpenPackageItem` отказывает 6097 кодом 6). Порт отказывал здесь и
	//   показывал ложное «не хватает ячеек 0/0». Ключ — `asda.Package.EmptyRowShowsPanel` (AsdaPackagePanelSubsystem.cpp).
	const bool bEmptyRowOk = AsdaPackageEmptyRowShowsPanelOn() && IT->IsPackageItem(ItemCode);   // пустая строка допустима
	if (!IT->GetPackageContents(ItemCode, Contents) && !bEmptyRowOk) { return false; }
	int32 Merged = 0;   // позиций, слитых в стопку (правило P100)
	const bool bRet = AsdaPackageReserve::Compute(IT, Contents, Container(1), GetContainerSize(1), Container(2),
		AsdaChargeStack::IsOn(), OutSlots15, OutCount, OutShortSpend, OutShortBag, &Merged)
		|| (bEmptyRowOk && Contents.Num() == 0);   // байты слотов — исходное состояние эталона (0xFF/−1), позиций 0
	if (Merged > 0)
	{
		UE_LOG(LogAsdaInven, Log, TEXT("[коробки] коробка %d: позиций %d, слито в стопки зарядов %d (P100), нехватка сумки %d / расходников %d"),
			ItemCode, OutCount, Merged, OutShortBag, OutShortSpend);
	}
	return bRet;
}

bool AsdaPackageReserve::Compute(const UAsdaItemTableSubsystem* IT, const TArray<FAsdaPackageEntry>& Contents,
	const TArray<FAsdaItemInstance>* Bag, int32 BagSize, const TArray<FAsdaItemInstance>* Spend2, bool bChargeMerge,
	TArray<uint8>& OutSlots15, int32& OutCount, int32& OutShortSpend, int32& OutShortBag, int32* OutMerged)
{
	OutSlots15.Reset();
	OutSlots15.SetNumZeroed(15);
	OutCount = 0; OutShortSpend = 0; OutShortBag = 0;
	if (OutMerged) { *OutMerged = 0; }
	if (!IT) { return false; }

	// ★ U6 (В1 дедупа №2): 15 байт собираются `FAsdaPacketWriter` (границы проверяет писатель) и в конце
	//   копируются в `OutSlots15`; байты те же, что у прежних записей прямо в массив.
	FAsdaPacketWriter W(15);
	// Начальное состояние эталона: контейнер 0xFF, слот −1; в цикле слот сразу становится −100.
	for (int32 i = 0; i < 5; ++i)
	{
		W.U8(3 * i, 0xFF);
		W.I16(3 * i + 1, -1);
	}

	// Уже зарезервированные позиции (эталон держит их вектором `pChar + 816`).
	TSet<int32> TakenBag, TakenSpend;
	const int32 BagN = Bag ? FMath::Min(Bag->Num(), BagSize) : 0;   // ★ 29.10.2026 (инвентарь ×2): доступная ёмкость

	// ★ [ИСПРАВЛЕНИЕ] P100: виртуальная сумка этой выдачи — какие ячейки уже несут ключ и сколько зарядов в них
	//   ДОБАВЛЕНО позициями этого же состава (сервер держит то же «накопителем ожидания»).
	//   AddedBySlot — добавка к ячейке s от предыдущих позиций; NewKeyBySlot — ключ НОВОЙ ячейки, занятой первой копией.
	TMap<int32, int64> AddedBySlot;
	TMap<int32, int32> NewKeyBySlot;

	for (int32 i = 0; i < 5; ++i)
	{
		W.I16(3 * i + 1, -100);   // эталон: `*(WORD*)(out + 3*m + 1) = -100`
		if (!Contents.IsValidIndex(i)) { continue; }
		const int32 Code = Contents[i].ItemCode;

		const FAsdaItemRecord* Rec = IT->GetItem(Code);
		int32 Kind = -1;
		if (Rec)                        { Kind = (Rec->ItemType <= 9 || Rec->CategoryFlag == 3) ? 1 : 0; }
		if (IT->IsSpecialItemCode(Code)) { Kind = 1; }
		if (IT->IsSowelCode(Code))       { Kind = 0; }
		if (Kind < 0)
		{
			UE_LOG(LogAsdaInven, Log,
				TEXT("[коробки] позиция %d: код %d не найден ни в одной таблице — эталон её пропускает"), i, Code);
			continue;
		}

		if (Kind == 1)
		{
			W.U8(3 * i, 1);
			// ★ [ИСПРАВЛЕНИЕ] P100 — складываемый спецпредмет (ключ > 0 по `DT_ChargeGroup`): добавка n = заряды кода (сервер
			//   P95/P100 количество позиции для спецпредмета не учитывает — одна ячейка на позицию, ревизия 02.11 п.8); первая по
			//   номеру ячейка того же ключа И того же Owner записи (+50) (лежащая или занятая раньше в этой же выдаче), у которой
			//   Amount + уже добавленное + n ≤ 30000, принимает заряды — новая ячейка не нужна. Иначе — как эталон.
			//   Owner входящей записи коробки у сервера — по привязке кода (FT+43 == 2 → 2, иначе 0); привязка = ключ % 10.
			int32 Key = 0, Charges = 0;
			if (bChargeMerge && IT->IsSpecialItemCode(Code) && AsdaChargeStack::Lookup(IT, Code, Key, Charges))
			{
				const int64 N = (int64)Charges;
				const uint8 WantOwner = (Key % 10 == 2) ? 2 : 0;   // Owner, который сервер поставит записи этой позиции
				int32 Stack = -1;
				for (int32 s = 0; s < BagN && Stack < 0; ++s)
				{
					int64 Have = -1;   // заряды ячейки s с ключом Key (−1 — ячейка не этого ключа)
					if (const int32* NK = NewKeyBySlot.Find(s))
					{
						if (*NK == Key) { Have = 0; }
					}
					else if (!(*Bag)[s].IsEmpty())
					{
						int32 CellKey = 0, CellCharges = 0;
						const uint8 CellOwner = (*Bag)[s].Raw.Num() > 50 ? (*Bag)[s].Raw[50] : 0;   // Owner записи ячейки (+50)
						if (AsdaChargeStack::Lookup(IT, (*Bag)[s].ItemCode, CellKey, CellCharges) && CellKey == Key && CellOwner == WantOwner)
						{
							Have = (int64)(*Bag)[s].Amount;
						}
					}
					if (Have < 0) { continue; }
					const int64* Added = AddedBySlot.Find(s);
					if (Have + (Added ? *Added : 0) + N <= (int64)AsdaChargeStack::kCap) { Stack = s; }
				}
				if (Stack >= 0)
				{
					AddedBySlot.FindOrAdd(Stack) += N;
					W.I16(3 * i + 1, Stack);   // ячейка, в которую сольётся (сервер вернёт её в 6097 полем +5)
					if (OutMerged) { ++*OutMerged; }
					UE_LOG(LogAsdaInven, Verbose, TEXT("[коробки] позиция %d: код %d (ключ %d, +%lld зарядов) → стопка %d"),
						i, Code, Key, N, Stack);
					++OutCount;
					continue;
				}
				// Стопки нет / все упёрлись в потолок — новая свободная ячейка (как сток); она становится ячейкой ключа.
				int32 Free = -1;
				for (int32 s = 0; s < BagN; ++s)
				{
					if ((*Bag)[s].IsEmpty() && !TakenBag.Contains(s)) { Free = s; break; }
				}
				if (Free >= 0)
				{
					TakenBag.Add(Free);
					NewKeyBySlot.Add(Free, Key);
					AddedBySlot.Add(Free, N);
					W.I16(3 * i + 1, Free);
				}
				else { ++OutShortBag; }
				++OutCount;
				continue;
			}
			int32 Found = -1;
			for (int32 s = 0; s < BagN; ++s)
			{
				if ((*Bag)[s].IsEmpty() && !TakenBag.Contains(s)) { Found = s; break; }
			}
			if (Found >= 0) { TakenBag.Add(Found); W.I16(3 * i + 1, Found); }
			else            { ++OutShortBag; }
		}
		else
		{
			W.U8(3 * i, 2);
			int32 Found = -1;
			// Доложить в существующий стек — только у «торгуемых» и не-itemType-10 (эталон).
			if (Rec && Rec->TradableFlag == 1 && Rec->ItemType != 10 && Spend2)
			{
				for (int32 s = 0; s < Spend2->Num(); ++s)
				{
					if ((*Spend2)[s].ItemCode == Code) { Found = s; break; }
				}
			}
			if (Found < 0 && Spend2)
			{
				for (int32 s = 0; s < Spend2->Num(); ++s)
				{
					if ((*Spend2)[s].IsEmpty() && !TakenSpend.Contains(s)) { Found = s; break; }
				}
			}
			if (Found >= 0) { TakenSpend.Add(Found); W.I16(3 * i + 1, Found); }
			else            { ++OutShortSpend; }
		}
		++OutCount;   // эталонный `usedSlotCount`: считаются позиции с контейнером != 0xFF
	}
	OutSlots15 = W.ToArray();
	return OutCount > 0;
}

// ★★★ ДВОЙНОЙ КЛИК ПО КОРОБКЕ — ОТКРЫТЬ ПАНЕЛЬ СОСТАВА, А НЕ ШЛЯТЬ ПАКЕТ.
//   Эталон `CInventory_OnSlotUse@0xD07460` (`item_inventory.c:37873-37878`):
//       stampWnd_PopulateItemSlots(g_pPackageStampWindow, *(DWORD*)pSlotItem, /*stampMode*/ 1, 0);
//       dword_2C2C3C4 = pSlotItem;      // = g_pPackageStampWindow + 0x34 — ЗАПОМНИТЬ ВЫБОР
//   Пакет 6096 уходит только по кнопке 26502 и только СЛЕДУЮЩИМ КАДРОМ
//   (`itemPackAgeToggleSendAction@0xDAB6E0` из `game_main.c:1480`) — см. `SendOpenPackage6096`.
//
//   ★ СНЯТО ВРЕМЕННОЕ ОТСТУПЛЕНИЕ ВОЛНЫ 2. Раньше здесь выполнялось действие кнопки подтверждения
//     (сразу проверка места + отправка), потому что у панели не было логики. Теперь панель
//     реализована (`UAsdaPackagePanelSubsystem`, окно 26500), и поведение эталонное 1:1.
bool UAsdaInventorySubsystem::OpenPackageItem(int32 ContainerId, int32 CellIndex)
{
	const FAsdaItemInstance* Src = GetCell(ContainerId, CellIndex);
	if (!Src || Src->IsEmpty()) { return false; }
	UGameInstance* GI = GetGameInstance();
	UAsdaItemTableSubsystem* IT = GI ? GI->GetSubsystem<UAsdaItemTableSubsystem>() : nullptr;
	UAsdaPackagePanelSubsystem* Panel = GI ? GI->GetSubsystem<UAsdaPackagePanelSubsystem>() : nullptr;
	if (!IT || !Panel) { return false; }
	const int32 Code = Src->ItemCode;
	if (!IT->IsPackageItem(Code))
	{
		UE_LOG(LogAsdaInven, Log, TEXT("[коробки] предмет %d не коробка (нет строки packagetable) — панель не открываем"), Code);
		return false;
	}
	// ★★★ [ИСПРАВЛЕНИЕ] 29.10.2026: минимальный уровень коробки — эталон панель открывает при любом уровне, сток сервера
	//   6096 уровень не читает (сервер — P95). Ниже уровня → плашка 104012, панель не открываем. Ключ `asda.Item.UseLevelGate`.
	if (AsdaItemLevelGate::RefuseBelowLevel(GI, Code, AsdaItemLevelGate::EUse::Package))
	{
		return false;
	}
	// stampMode = 1 (вскрытие): подпись кнопки 26502 = нотис 99305 «Open Package».
	const bool bOk = Panel->Populate(Code, /*StampMode*/ 1, ContainerId, CellIndex, /*bShowTooltip*/ false);
	UE_LOG(LogAsdaInven, Log,
		TEXT("[коробки] двойной клик по коробке %d (%d:%d) → панель 26500 %s (6096 уйдёт по кнопке 26502)"),
		Code, ContainerId, CellIndex, bOk ? TEXT("открыта") : TEXT("НЕ открыта"));
	return bOk;
}

// ★★★ ОТПРАВКА 6096. Порт `net_SendItemActionReq_5slot@0xDAB7E0` (`network.c:24479-24643`).
//   Зовётся ТОЛЬКО из отложенной защёлки панели 26500 — никогда напрямую из двойного клика.
//   ⚠ Эталон здесь ПОВТОРЯЕТ подбор слотов и при нехватке МОЛЧА ничего не шлёт: нотис 99306
//     показывается раньше, в обработчике кнопки, и в этой точке его уже нет.
bool UAsdaInventorySubsystem::SendOpenPackage6096(int32 ItemCode, int32 ContainerId, int32 CellIndex)
{
	UGameInstance* GI = GetGameInstance();
	UAsdaNetSubsystem* Net = GI ? GI->GetSubsystem<UAsdaNetSubsystem>() : nullptr;
	UAsdaItemTableSubsystem* IT = GI ? GI->GetSubsystem<UAsdaItemTableSubsystem>() : nullptr;
	if (!Net || !IT) { return false; }

	TArray<FAsdaPackageEntry> Contents;
	IT->GetPackageContents(ItemCode, Contents);
	for (int32 i = 0; i < Contents.Num(); ++i)
	{
		UE_LOG(LogAsdaInven, Verbose, TEXT("[коробки] состав %d/%d: предмет %d («%s») × %d"),
			i + 1, Contents.Num(), Contents[i].ItemCode, *IT->GetItemName(Contents[i].ItemCode), Contents[i].Amount);
	}

	TArray<uint8> Slots15;
	int32 Count = 0, ShortSpend = 0, ShortBag = 0;
	if (!ReservePackageSlots(ItemCode, Slots15, Count, ShortSpend, ShortBag))
	{
		UE_LOG(LogAsdaInven, Warning, TEXT("[коробки] предмет %d: состав пуст — вскрывать нечего"), ItemCode);
		return false;
	}
	if (ShortSpend > 0 || ShortBag > 0)
	{
		// Эталон в ЭТОЙ точке молчит (нотис уже показала кнопка) — только лог.
		UE_LOG(LogAsdaInven, Log,
			TEXT("[коробки] предмет %d: не хватает ячеек (расходники %d, сумка %d) — эталон здесь шлёт молча ничего"),
			ItemCode, ShortSpend, ShortBag);
		return false;
	}

	// Поле @0 запроса — СВОЙ userNum. Сервер его НЕ читает, но собираем как эталон.
	//   ★ 27.09.2026 [ОРИГИНАЛ] (аудит `Reports/owner_0927/charid_senders_audit.md`): эталон
	//   `reqBody[0] = Target_GetSelectedUserIdx(CharManager_GetLocalPlayer())` (`net_SendItemActionReq_5slot@0xDAB7E0`) — миснейм:
	//   геттер (`Char_GetHoveredUserIdx@0x966490`, `*(u16*)(this+190188)`) возвращает номер ПЕРЕДАННОГО, т.е. свой.
	//   Прежде здесь стоял номер выбранной цели.
	// ★ 27.09.2026 [ОРИГИНАЛ] без своего персонажа эталон МОЛЧИТ: `if (pLocalPlayer)` вокруг сборки и отправки
	//   (network.c:24527-24529) — ни пакета, ни нотиса. Прежде порт слал @0 = −1. [ПОРТ] «свой персонаж есть» =
	//   номер сессии пришёл (userNum выдаётся вместе с персонажем; так же гейтятся 6059/6091/6146).
	if (!Net->IsSelfUserNumKnown())
	{
		UE_LOG(LogAsdaInven, Log, TEXT("[коробки] предмет %d: своего персонажа нет — 6096 не шлём (эталон молчит)"), ItemCode);
		return false;
	}
	const int32 SelfUserNum = (int32)Net->SelfUserNum;   // @0 — свой userNum
	// ⚠ @2 — именно charId ([[charid-vs-usernum]]): сервер сверяет его с `sub_5324F0(user)` и
	//   при расхождении отвечает кодом 2.
	const bool bOk = Net->SendOpenPackageItem6096(Net->SelectedCharId, SelfUserNum, (uint8)Count,
		(uint8)ContainerId, CellIndex, Slots15);
	if (bOk) { bActionPending = true; ActionSentAtS = FPlatformTime::Seconds(); }
	return bOk;
}

// ★★★ 6097 — РЕЗУЛЬТАТ ВСКРЫТИЯ. Порт `netOnStorageResultPacket@0xDABFD0` (network.c:24651-24783).
void UAsdaInventorySubsystem::ApplyPackageResult(const TArray<uint8>& Body300)
{
	if (Body300.Num() < 300) { return; }
	const uint8* P = Body300.GetData();
	const int32 Granted   = P[6];
	const int32 Result    = P[7];
	const int32 BoxCont   = P[8];
	const int32 BoxSlot   = RdI16(P + 9);
	const int32 NewWeight = AsdaWeightFromWire(RdI32(P + 296));   // ★ 29.10.2026 (вес ×2): вес персонажа u16
	bActionPending = false;

	// ★★★ ПЕРВЫМ ДЕЛОМ — закрыть панель состава. Эталон `netOnStorageResultPacket@0xDABFD0` делает
	//   это самой первой строкой (`network.c:24677`: `uiPanelCloseAndKillFocus(self, a2)`) — ДО
	//   разбора кода результата, то есть панель гаснет и на успехе, и на любой ошибке.
	if (UGameInstance* GIp = GetGameInstance())
	{
		if (UAsdaPackagePanelSubsystem* Panel = GIp->GetSubsystem<UAsdaPackagePanelSubsystem>())
		{
			Panel->NotifyPackageResult();
		}
	}

	if (Result != 1)
	{
		// Эталон: коды 2…11 → строки 99312…99321 (loc = 99310 + код); код 0 («ошибка БД», строка
		//   99310) в switch НЕ ОБРАБОТАН — `default: return`, то есть молча. Оставляем как есть,
		//   но фиксируем в логе, чтобы код не терялся.
		if (Result >= 2 && Result <= 11)
		{
			Notice(99310 + Result);   // [ПОРТ] 09.10.2026 (мультиязычность, этап 3): без RU-фолбэков (нет строки → «[#id]»)
		}
		else
		{
			UE_LOG(LogAsdaInven, Log,
				TEXT("[коробки] 6097 result=%d — эталон такой код игнорирует молча (строка 99310 «Database failed» не выводится)"),
				Result);
		}
		return;
	}

	// 1. Ячейка САМОЙ КОРОБКИ чистится целиком (эталон делает `memset` записи и обновляет иконку).
	if (BoxCont == 1 || BoxCont == 2) { ClearCell(BoxCont, BoxSlot); }

	// 2. Пять записей выданных предметов: каждая кладётся ПО СВОИМ полям (+4 контейнер, +5 слот) —
	//    ровно то, что делает эталон двумя ветками (`g_InventoryArray2` / `g_InventoryBag`).
	int32 Applied = 0;
	for (int32 i = 0; i < 5; ++i)
	{
		const uint8* Rec = P + 11 + 57 * i;
		if (RdI32(Rec) <= 0) { continue; }
		PutRecord(Rec, /*ForcedContainer*/ 0);
		++Applied;
	}

	// 2а. ★ НАШЕ ДОБАВЛЕНИЕ (решение юзера 2026-08-13) — АНОНС РЕДКОЙ ДОБЫЧИ ИЗ КОРОБКИ.
	//     Оригинал здесь анонса не делает (доказательство — в шапке `CVarAnnounceFromPackage`).
	//     Путь ТОТ ЖЕ, что у приза гачи (`AsdaBoosterPanelSubsystem::OnOkButton` →
	//     `NotifyItemInfo`): критерий редкости живёт ВНУТРИ `NotifyItemInfo` (ветка предметов +
	//     ветка самоцветов), отдельной логики не заводим.
	//     Зовём ПОСЛЕ раскладки записей: к этому моменту предмет уже лежит в слоте, состояние
	//     сумки согласовано. Контейнер/слот не передаём — см. «против спама» в шапке cvar.
	if (Applied > 0 && CVarAnnounceFromPackage.GetValueOnAnyThread() != 0)
	{
		if (UAsdaHudSubsystem* Hud = GetGameInstance() ? GetGameInstance()->GetSubsystem<UAsdaHudSubsystem>() : nullptr)
		{
			for (int32 i = 0; i < 5; ++i)
			{
				const uint8* Rec = P + 11 + 57 * i;
				const int32  Code = RdI32(Rec);
				if (Code <= 0) { continue; }
				Hud->NotifyItemInfo(Code, /*bForce=*/false, /*ContainerType=*/-1, /*SlotIndex=*/-1);
			}
		}
	}

	// 3. Вес персонажа — i32 @296 (`HUD_UpdateMiniWeightBar(g_pHUD, packet[74])`).
	if (UAsdaNetSubsystem* Net = GetGameInstance() ? GetGameInstance()->GetSubsystem<UAsdaNetSubsystem>() : nullptr)
	{
		Net->SetSelfWeight(NewWeight, -1, TEXT("6097 вскрытие коробки"));
	}

	// 4. Нотис 99330 «Ты получил [sd1] предметов из пакета». Число — байт @6 (эталон кладёт его
	//    в числовой слот 0 через `CClass_SetSlotPair(g_pLocNoticeCtx, 0, BYTE2(packet[1]), 0)`).
	if (UAsdaMessageSubsystem* Msg = GetGameInstance() ? GetGameInstance()->GetSubsystem<UAsdaMessageSubsystem>() : nullptr)
	{
		Msg->PostLocFmt(EAsdaMsgCategory::Loot, 99330, {}, { Granted });
	}

	// 5. Иконки панели быстрого доступа (эталон: `CClass_BuildItemRecord(g_pQuickSlotBar, 2, …)`
	//    для каждой выданной записи) и перерисовка окна.
	if (UAsdaHotbarSubsystem* Hb = GetGameInstance() ? GetGameInstance()->GetSubsystem<UAsdaHotbarSubsystem>() : nullptr)
	{
		Hb->RebuildSlots();
		Hb->RefreshPage();
	}
	if (bWindowOpen) { RefreshGrid(); }
	UE_LOG(LogAsdaInven, Log,
		TEXT("[коробки] 6097 успех: коробка %d:%d израсходована, выдано записей %d (сервер насчитал %d), вес %d"),
		BoxCont, BoxSlot, Applied, Granted, NewWeight);
}

// ───────────────────────────── B. ГАЧА («бустер») ──────────────────────────────────────────────
//
// ★★★ ПРИМЕНИТЬ ГАЧА-ПРЕДМЕТ. Эталон: двойной клик открывает окно `BOOSTING_WIN` (10050,
//   `itemBoosterWnd_Open@0xD1F4A0`), кнопка `BOOSTER_START` (10069) → `boosterShopUseOrClose@0xD22250`
//   → `Send_GachaItemUse@0xA0EAD0`. Барабан (`boosterShopUpdate@0xD202A0`, 36 иконок, замедление,
//   таймауты 30 000 / 5 000 мс) — часть окна; разметка окна в порту импортирована
//   (`WBP_BOOSTING_WIN`), логики у неё нет, поэтому здесь выполняется действие кнопки «Старт».
//   Превью барабана доступно через `GetBoosterPreview` — данные для окна готовы.
// ★★★ ДВОЙНОЙ КЛИК ПО ГАЧА-ПРЕДМЕТУ — ОТКРЫТЬ ОКНО БАРАБАНА, А НЕ ШЛЯТЬ ПАКЕТ.
//   Эталон `CInventory_OnSlotUse@0xD07460` → `skillAction_TriggerSabzil@0xD222C0` (имя врёт: это
//   гейт «можно ли сейчас применить») собирает 7-байтовый дескриптор `{i32 код; u8 контейнер;
//   i16 слот}` и зовёт `itemBoosterWnd_Open@0xD1F4A0` (`item_inventory.c:38076` и `:38368`).
//   Пакет 5468 уходит только по кнопке START (10069) окна 10050.
//   ★ СНЯТО ВРЕМЕННОЕ ОТСТУПЛЕНИЕ ВОЛНЫ 2 (двойной клик выполнял действие кнопки подтверждения).
bool UAsdaInventorySubsystem::UseGachaItem(int32 ContainerId, int32 CellIndex)
{
	const FAsdaItemInstance* Src = GetCell(ContainerId, CellIndex);
	if (!Src || Src->IsEmpty()) { return false; }
	// ★ 31.10.2026 [ИСПРАВЛЕНИЕ] (`asda.Duel.LoserLock`): пока свой персонаж доигрывает жест поражения дуэли (51) — предметы не
	//   применяются, МОЛЧА (у оригинала гейта нет — жест обрывался; разбор у `AsdaDuelLoserLocked`).
	{
		const UAsdaWorldSubsystem* DlW = GetGameInstance() ? GetGameInstance()->GetSubsystem<UAsdaWorldSubsystem>() : nullptr;
		if (DlW && AsdaDuelLoserLocked(Cast<AAsdaPlayerCharacter>(DlW->LocalPlayerActor.Get())))
		{
			UE_LOG(LogAsdaInven, Log, TEXT("[инвентарь] %s: жест поражения дуэли — предмет не применён"), ANSI_TO_TCHAR(__FUNCTION__));
			return false;
		}
	}
	UGameInstance* GI = GetGameInstance();
	UAsdaBoosterPanelSubsystem* Win = GI ? GI->GetSubsystem<UAsdaBoosterPanelSubsystem>() : nullptr;
	if (!Win) { return false; }
	// ★ Волна В3 ремесла (ревизия В0–В2 С-1): гейт `skillAction_TriggerSabzil@0xD222C0` (skill_buff.c:29624/29626) —
	//   при открытом окне ремесла 76000 / изготовления 77500 `return 0`: бустер не открывается, МОЛЧА.
	if (UAsdaCraftSubsystem::IsAnyCraftWindowOpen(GI))
	{
		UE_LOG(LogAsdaInven, Log, TEXT("[гача] бустер %d не открыт молча: открыто окно ремесла (skill_buff.c:29624)"), Src->ItemCode);
		return false;
	}
	// ★ 29.09.2026 (волна 0 торговли): тот же `skillAction_TriggerSabzil@0xD222C0` (skill_buff.c:29619) — при ОТКРЫТОМ окне
	//   обмена 2008 (`g_bPartyInviteFlag` = `CExchange+0x2C`) `return 0` МОЛЧА. [ОРИГИНАЛ] порядок: 2075 → `sub_93B448` → обмен →
	//   `objStateAllowsRender` → 76000 → 77500; [ПОРТ] у порта ремесло проверяется первым — на исход не влияет (все молча).
	if (UAsdaTradeStateSubsystem::IsExchangeWindowOpen(GI))
	{
		UE_LOG(LogAsdaInven, Log, TEXT("[гача] бустер %d не открыт молча: открыто окно обмена 2008 (skill_buff.c:29619)"), Src->ItemCode);
		return false;
	}
	// ★ 25.10.2026 (ревизия sit108, М-3) [ОРИГИНАЛ]: тот же `skillAction_TriggerSabzil` — `objStateAllowsRender` →
	//   `return 0` МОЛЧА: бустер не открывается, стопа нет. Общая точка `IsLocalPlayerBusy`; ключ `asda.Char.SitExitEtalon 0` — прежнее.
	if (AAsdaPlayerCharacter::IsSitExitEtalon())
	{
		const UAsdaSystemOptionSubsystem* SOb = GI->GetSubsystem<UAsdaSystemOptionSubsystem>();
		if (SOb && SOb->IsLocalPlayerBusy())
		{
			UE_LOG(LogAsdaInven, Log, TEXT("[гача] бустер %d не открыт молча: персонаж занят (skillAction_TriggerSabzil)"), Src->ItemCode);
			return false;
		}
	}
	// ★ 24.10.2026 (выход из сидения 1:1) [ОРИГИНАЛ]: `skillAction_TriggerSabzil@0xD222C0` — ТОЛЬКО в городе (`controlBar_SlotIsActive(клетка)
	//   & 0x80`): на ходу 4008, затем `charActionStateIs7to9 ? CharFx(9) : CharFx(0)` (сидящий встаёт без 109). Ключ `asda.Char.SitExitEtalon`.
	if (const UAsdaZoneSubsystem* Zn = GI->GetSubsystem<UAsdaZoneSubsystem>(); Zn && Zn->IsInPeaceZone())
	{
		if (APlayerController* StopPc = GetWorld() ? GetWorld()->GetFirstPlayerController() : nullptr)
		{
			if (AAsdaPlayerCharacter* StopCh = Cast<AAsdaPlayerCharacter>(StopPc->GetPawn())) { StopCh->ApplyEtalonSceneStop(TEXT("бустер 10050 в городе (skillAction_TriggerSabzil)")); }
		}
	}
	// ★★★ [ИСПРАВЛЕНИЕ] 29.10.2026 (баг «Pig Pang … shall not be open till level 10»): минимальный уровень бустера —
	//   эталон окно 10050 открывает при любом уровне, сток сервера 5468 уровень не читает (сервер — P95). Ниже уровня →
	//   плашка 104012, окно не открываем. Стоит ПОСЛЕ молчаливых гейтов эталона: при открытом обмене/ремесле — по-прежнему молча.
	if (AsdaItemLevelGate::RefuseBelowLevel(GI, Src->ItemCode, AsdaItemLevelGate::EUse::Booster))
	{
		return false;
	}
	// Гейты «есть запись item_list» и «есть строка booster_list» проверяет само окно (эталон
	//   `itemBoosterWnd_Open` возвращает 0, и вызывающий уходит в обычное применение предмета).
	const bool bOk = Win->OpenWindow(Src->ItemCode, ContainerId, CellIndex);
	UE_LOG(LogAsdaInven, Log,
		TEXT("[гача] двойной клик по бустеру %d (%d:%d) → окно 10050 %s (5468 уйдёт по кнопке START)"),
		Src->ItemCode, ContainerId, CellIndex, bOk ? TEXT("открыто") : TEXT("НЕ открыто"));
	return bOk;
}

// ★★★ 5469 — РЕЗУЛЬТАТ ГАЧИ. Порт `Recv_BoosterShopResult@0xF4C220` (network.c:44421) и ветки
//   применения `Recv_BoosterShopResult_0@0xCFF9F0` (network.c:16985-17215).
//   ⚠ КОД 0 — ЭТО УСПЕХ (а не отказ, как в 5029): у этого канала нумерация своя.
void UAsdaInventorySubsystem::ApplyGachaResult(const TArray<uint8>& Body68)
{
	if (Body68.Num() < 68) { return; }
	const uint8* P = Body68.GetData();
	const int32 Result    = P[0];
	const int32 SpentCode = RdI32(P + 2);
	bActionPending = false;

	UGameInstance* GIr = GetGameInstance();
	UAsdaBoosterPanelSubsystem* Win = GIr ? GIr->GetSubsystem<UAsdaBoosterPanelSubsystem>() : nullptr;

	if (Result != 0)
	{
		// Таблица нотисов эталона (network.c:44440-44520): 1→13107 … 7→13113, 8→12165.
		//   ★ И на любом отказе эталон закрывает окно барабана (`tagBoosterShop_CloseWindow`).
		static const int32 kLoc[9] = { 0, 13107, 13108, 13109, 13110, 13111, 13112, 13113, 12165 };
		if (Result >= 1 && Result <= 8)
		{
			Notice(kLoc[Result]);
		}
		if (Win) { Win->OnCloseButton(); }
		UE_LOG(LogAsdaInven, Log, TEXT("[гача] 5469 отказ: result=%d (предмет %d)"), Result, SpentCode);
		return;
	}

	// ★★★ РАЗВИЛКА ЭТАЛОНА `Recv_BoosterShopResult_0@0xCFF9F0` (network.c:16985-17215):
	//   ветки А/Б — окно бустера ЖИВО: пакет кладётся в его буфер, запускается вращение барабана,
	//   прогресс-бар на 9200 мс, и приз применяется ПОЗЖЕ, по кнопке OK (10070);
	//   ветка В (`network.c:17060+`) — окно закрыто: результат применяется НЕМЕДЛЕННО, со звуком,
	//   и сразу же уходит 5474.
	if (Win && Win->NotifyGachaResult(Body68))
	{
		UE_LOG(LogAsdaInven, Log,
			TEXT("[гача] 5469 успех: окно 10050 забрало пакет (барабан пущен) — приз применится по кнопке OK"));
		return;
	}
	ApplyGachaPrize(Body68, /*bPlayFxSound*/ true);
}

// ★★★ ВЕТКА УСПЕХА 5469. Вызывается из ДВУХ мест эталона: кнопка OK окна 10050
//   (`OnRecv_ItemEnchantResult@0xD20B30`, имя врёт) и ветка В приёма (окно закрыто).
void UAsdaInventorySubsystem::ApplyGachaPrize(const TArray<uint8>& Body68, bool bPlayFxSound,
                                              bool bSpawnFx, bool bSend5474, bool bRareNotice)
{
	if (Body68.Num() < 68) { return; }
	const uint8* P = Body68.GetData();
	const bool  bRare     = (P[1] == 1);
	const int32 SpentCode = RdI32(P + 2);
	const int32 SpentCont = P[6];
	const int32 SpentSlot = RdI16(P + 7);
	const int32 NewWeight = AsdaWeightFromWire(RdI16(P + 66));   // ★ 29.10.2026 (вес ×2): вес персонажа u16

	// 1. Списание потраченной гачи и выдача приза (запись 57 б ложится по своим полям).
	if (SpentCont == 1 || SpentCont == 2) { ClearCell(SpentCont, SpentSlot); }
	const uint8* Prize = P + 9;
	const int32 PrizeCode = RdI32(Prize);
	if (PrizeCode > 0) { PutRecord(Prize, /*ForcedContainer*/ 0); }

	UGameInstance* GI = GetGameInstance();
	UAsdaNetSubsystem* Net = GI ? GI->GetSubsystem<UAsdaNetSubsystem>() : nullptr;
	if (Net) { Net->SetSelfWeight(NewWeight, -1, TEXT("5469 гача")); }

	// 2. Эффект удачи НА СЕБЕ (эталон играет его локально, сервер соседям сам не шлёт).
	//    ★ ЗВУК — ТОЛЬКО В ВЕТКЕ «ОКНО ЗАКРЫТО». В ветке кнопки OK (`OnRecv_ItemEnchantResult`,
	//      shop_trade.c:12417-12452) эталон спавнит эффект на кости `M_DUMMY`/`G_DUMMY` БЕЗ звука —
	//      там уже отыграл `s_item_boosting02.wav` в момент приёма пакета.
	//    ★ `bSpawnFx == false` — путь ЗАКРЫТИЯ окна (`boosterApplyResult@0xD1F830`): в его теле
	//      `Effect_SpawnByName` не вызывается ни разу, только `sub_9349C7(g_pSoundMgr,
	//      "s_item_boosting01.wav")` — остановка зациклённого звука барабана.
	if (bSpawnFx)
	{
		if (UAsdaEffectsSubsystem* Eff = GI ? GI->GetSubsystem<UAsdaEffectsSubsystem>() : nullptr)
		{
			const int32 SelfIdx = Net ? AsdaSelf::GetUserIdxOr(GI, INDEX_NONE) : -1;   // ★ волна 1 (E2): −1 до пакета 4003
			if (bPlayFxSound) { Eff->SpawnBoostFx(SelfIdx, bRare); }
			else { Eff->SpawnDummyBoneFx(SelfIdx, bRare ? TEXT("eff_lucky_a_01") : TEXT("eff_lucky_a_00"), FString()); }
		}
	}

	// 3. Чат-нотис с именем приза: редкий → 20067, обычный → 20066 (эталон красит имя цветом грейда;
	//    в порту цвет строки задаёт маршрутизатор по категории `Loot` — та же зелёная палитра дропа).
	if (UAsdaMessageSubsystem* Msg = GI ? GI->GetSubsystem<UAsdaMessageSubsystem>() : nullptr)
	{
		UAsdaItemTableSubsystem* IT = GI->GetSubsystem<UAsdaItemTableSubsystem>();
		// ★ Путь ЗАКРЫТИЯ окна (`bRareNotice == false`) даёт нотис ВСЕГДА 20066:
		//   `boosterApplyResult@0xD1F830` (`shop_trade.c:12089`) зовёт
		//   `UI_FormatLocalizedNotice(g_pLocNoticeCtx, 20066, 1, 1)` без ветки по редкости.
		Msg->PostLocFmt(EAsdaMsgCategory::Loot, (bRareNotice && bRare) ? 20067 : 20066,
			{ IT ? IT->GetItemName(PrizeCode) : FString::FromInt(PrizeCode) }, {});
	}

	// 4. ★ Клиент САМ просит сервер показать эффект соседям — 5474 (сервер рассылает 5473 по
	//    9 секторам, исключая отправителя). Без этого шага соседи ничего не увидят.
	//    ★ На пути закрытия окна эталон 5474 НЕ шлёт (в `boosterApplyResult` отправки нет).
	if (Net && bSend5474) { Net->SendGachaItemEffect5474(bRare ? 1 : 0); }

	if (UAsdaHotbarSubsystem* Hb = GI ? GI->GetSubsystem<UAsdaHotbarSubsystem>() : nullptr)
	{
		Hb->RebuildSlots();
		Hb->RefreshPage();
	}
	if (bWindowOpen) { RefreshGrid(); }
	UE_LOG(LogAsdaInven, Log, TEXT("[гача] 5469 успех: потрачен %d (%d:%d), выигран %d, редкий=%d, вес=%d"),
		SpentCode, SpentCont, SpentSlot, PrizeCode, (int32)bRare, NewWeight);
}
