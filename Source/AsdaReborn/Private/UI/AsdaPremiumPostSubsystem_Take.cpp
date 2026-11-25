// Copyright (c) 2026 @sitxovski (Discord)
// AsdaPremiumPostSubsystem_Take.cpp — ★ R3-c 12.11.2026 (разрез AsdaPremiumPostSubsystem.cpp, дословный перенос): пакеты списка и применения, получение: ячейки, свободное место, очередь выдачи, выбор варианта, результат, Tick.
//   Общее для частей — в AsdaPremiumPostSubsystem_Private.h; шапка и реверс — в AsdaPremiumPostSubsystem.cpp.
#include "UI/AsdaPremiumPostSubsystem.h"
#include "AsdaPerf.h"   // ★ R4-b (14.11.2026): области замера времени игрового потока
#include "AsdaWeightWire.h"   // ★ 29.10.2026 (вес ×2): AsdaWeightFromWire — вес персонажа по проводу u16
#include "Misc/ScopeExit.h"   // ★ 10.11.2026: ON_SCOPE_EXIT — снятие заряда варианта на любом выходе из ApplyResultBody
#include "AsdaDevConsole.h"   // ★ античит А2: ASDA_DEV_CVAR / ASDA_DEV_CONSOLE — дев-ключи и команды вне Shipping
#include "UI/AsdaNotice.h"   // ★ U13 (дедуп №2, В2-A): ConsoleEcho — общее эхо консольных команд

#include "AsdaNetSubsystem.h"
#include "AsdaBytes.h"
#include "AsdaPacketWriter.h"   // ★ U6 (В1 дедупа №2): запись 11006 через общий писатель
#include "Blueprint/UserWidget.h"
#include "Framework/Application/SlateApplication.h"   // живой хит-тест: кого Slate реально видит под точкой
#include "Engine/GameInstance.h"
#include "Items/AsdaItemTableSubsystem.h"
#include "Text/AsdaTextSubsystem.h"
#include "Login/AsdaText.h"   // [ПОРТ] 09.10.2026 (мультиязычность, этап 3): FAsdaText::Loc — отказ ящика по id
#include "UI/AsdaAnimatedPlane.h"
#include "UI/AsdaInventorySubsystem.h"
#include "UI/AsdaItemTooltipSubsystem.h"
#include "UI/AsdaMessageSubsystem.h"
#include "UI/AsdaPremiumGachaSubsystem.h"
#include "UI/AsdaUiHelpers.h"
#include "UI/AsdaWindowSubsystem.h"
#include "AsdaOutgoingGate.h"               // ★ С-Р1: общий офлайн-гейт отладочных команд (RefuseDebugWhenOnline)
#include "Test/AsdaSelfTestKit.h"            // ★ 25.10.2026: каркас самотеста (quit, итог)
#include "TimerManager.h"
#include "Engine/World.h"
#include "AsdaPremiumPostSubsystem_Private.h"   // ★ R3-c 12.11.2026: общее для частей AsdaPremiumPostSubsystem*.cpp

namespace {
	constexpr int32 kLocGranted    = 110671;   // нотис успеха 11007 (`inventorySyncApplyPacket`)
	constexpr int32 kLocNoRoom     = 1011;     // нотис статуса 3 («Нет места в рюкзаке.»)
}

// [ИСПРАВЛЕНИЕ] 10.11.2026 «выбор до получения»: коробка с выбором «профессия × уровень» спрашивает вариант ДО 11006.
//   В оригинале окно 68500 открывалось после 11007 — заказ уже в сумке, крестик ничего не отменял и оставлял вариант
//   «по умолчанию» (разбор — у `UAsdaPremiumGachaSubsystem::PostItemHasVariantChoice`). 0 = как оригинал.
static ASDA_DEV_CVAR(int32, CVarPostChooseBeforeTake,
	TEXT("asda.Post.ChooseBeforeTake"), 1,
	TEXT("Элитный почтовый ящик: 1 (по умолчанию) — вариант коробки выбирается до получения, крестик оставляет заказ в ящике; ")
	TEXT("0 — как оригинал: выбор после получения, крестик оставляет вариант по умолчанию."),
	ECVF_Default);

// ★ ОТКАТ К ЭТАЛОНУ ДЛЯ ТЕКСТА ОТКАЗА 11007 (жалоба владельца 2026-08-30 «клиент писал 0»).
//   1 = вернуть поведение оригинала: в системный чат уходит ГОЛЫЙ код статуса («0», «2», «5»),
//   как это делает `inventorySyncApplyPacket@0xEF1320`. 0 (по умолчанию) = наша расшифровка словами.
static ASDA_DEV_CVAR(int32, CVarPostRawStatusText,
	TEXT("asda.Post.RawStatusText"), 0,
	TEXT("Элитный почтовый ящик: 1 — печатать в чат голый код отказа 11007, как в оригинале; ")
	TEXT("0 — расшифровывать код словами (исправление дефекта оригинала)."),
	ECVF_Default);

// ═══════ ★★★ РАСШИРЕНИЕ (В ОРИГИНАЛЕ СОРТИРОВКИ НЕТ ВООБЩЕ) — «НОВЫЕ СВЕРХУ» ═══════
//   ЖАЛОБА ВЛАДЕЛЬЦА (2026-08-28): «предметы добавляются странным порядком, не по новизне».
//
//   ЧТО ПОКАЗАЛ РАЗБОР (доказательства — в `Reports/elite_mailbox_order.md`):
//   1. КЛИЕНТ-ЭТАЛОН НЕ СОРТИРУЕТ ВООБЩЕ. `netParseLocalSlotSync@0xEF0990`
//      (`RE/etalon/network.c:33763`) кладёт запись i пакета 11005 ровно в строку i
//      (`this + 636 + 10*i`), а `gridWindowPopulate7x7@0xEF1DE0` (`RE/etalon/world_map.c:8252`)
//      рисует строку по полю `rowIndex`, сохранённому в элементе. Контейнер иконок — `std::list`
//      с вставкой ПЕРЕД `end()` (`sub_93473D` → `sub_EF22D0@0xEF22D0` = push_back), компаратора
//      нет ни одного. ⇒ порядок строк = ровно тот, в котором их прислал сервер.
//   2. ПОРЯДОК ЗАДАЁТ БД — хранимая процедура `UPCashitemList` (DBManager, DB 2602→2603).
//      По живым логам (`GameServer\Log\Log_MGamePremium\List_*.txt`) она отдаёт СТРОГО ПО
//      ВОЗРАСТАНИЮ поля `Order` и НЕ БОЛЬШЕ 7 записей (при 12 покупках в БД пришли 7 наименьших).
//   3. ★★★ ПОЛЯ ВРЕМЕНИ В ЗАПИСИ 11005 НЕТ. Запись 10 б = `Order(4) + ItemIndex(4) + Count(1) +
//      Type(1)`. Сортировать «по новизне» физически не по чему.
//   4. ★★★ И `Order` НОВИЗНЫ НЕ НЕСЁТ. Имени не верим (закон `false-negatives-method`): по живым
//      логам `Insert Item Info … OrderIndex 150 … Cash 150` — для покупки в шопе `OrderIndex`
//      РАВЕН ЦЕНЕ предмета, а для приза гачи это ОСТАТОК БАЛАНСА (599121→598431→597741→…,
//      шаг −690 = цена коробки). Источник — наш эмулятор платёжного шлюза `gp7-server\server.js`
//      (стр. 418/424: `payload.writeUInt32LE(authoritativePrice|purchase.balance, 174)`), а
//      `GameServer` читает `tOrderIndex` ровно с байта 174 ответа GP7 (`SendNewPremiumPacket`,
//      `RE/ida/server_work/GameServer.c:95167`). `Order` даже НЕ УНИКАЛЕН: в живом ящике
//      одновременно висели две строки `Order 150` и три `Order 10000`.
//
//   ⇒ «Странный порядок» = ящик показывает 7 САМЫХ ДЕШЁВЫХ покупок по возрастанию цены, а свежие
//     дорогие в него вообще не попадают. Это ДЕФЕКТ ДАННЫХ НА СЕРВЕРЕ, и починить его клиентом
//     нельзя: недостающие записи до клиента не доезжают. Что править на сервере — описано в
//     `Reports/elite_mailbox_order.md` (`gp7-server` и `UPCashitemList`).
//
//   ЭТОТ КЛЮЧ — переворот полученных 7 строк по `Order` УБЫВАНИЮ (стабильный, пустые строки
//   остаются внизу). ПО УМОЛЧАНИЮ ВЫКЛЮЧЕН И ЭТО ОСОЗНАННО: пока сервер кладёт в `Order` цену,
//   включение даёт «сначала самые дорогие», а не «сначала самые новые» — то есть меняет одну
//   неверную раскладку на другую. Включать имеет смысл ПОСЛЕ серверной правки, когда `Order`
//   станет настоящим возрастающим номером заказа; тогда убывание = новизна.
static ASDA_DEV_CVAR(int32, CVarPostSortNewestFirst,
	TEXT("asda.EliteMail.SortNewestFirst"), 0,
	TEXT("Элитный почтовый ящик, РАСШИРЕНИЕ (в оригинале сортировки нет): 1 — переставить строки ")
	TEXT("по убыванию поля Order, 0 (по умолчанию) — эталон, порядок ровно как прислал сервер. ")
	TEXT("⚠ Пока сервер кладёт в Order цену покупки, 1 даст «дорогие сверху», а НЕ «новые сверху»."),
	ECVF_Default);

// ═══════════════════════════════ СЕТЬ ══════════════════════════════════════════════════════════
void UAsdaPremiumPostSubsystem::OnListPacket(const TArray<uint8>& Body)
{
	// Закон `no-reentrant-net-callbacks`: из сетевого колбэка не трогаем ни окна, ни хотбар,
	//   ни модель инвентаря — откладываем на `Tick`.
	PendingList = Body;
	bListPending = true;
}

void UAsdaPremiumPostSubsystem::OnApplyPacket(const TArray<uint8>& Body409)
{
	PendingApply = Body409;
	bApplyPending = true;
}

// ═══ Порт `netParseLocalSlotSync@0xEF0990` (реверс §2.2) ═══
void UAsdaPremiumPostSubsystem::ApplyListBody(const TArray<uint8>& Body)
{
	UGameInstance* GI = GetGameInstance();
	UAsdaNetSubsystem* Net = GI ? GI->GetSubsystem<UAsdaNetSubsystem>() : nullptr;

	// Шаг 1 эталона: `this[616] = 0`.
	bRequestInFlight = false;
	if (Body.Num() < 4) { return; }

	// Шаг 2: сверка `CharIndex` пакета с локальным персонажем (`LocalPlayer->userIdx`).
	//   ⚠ Это именно charId (i32), а НЕ сессионный userNum (i16) — закон `charid-vs-usernum`.
	const int32 PktCharId = AsdaBytes::RdI32(&Body[0]);
	const int32 SelfCharId = Net ? Net->SelectedCharId : 0;
	if (SelfCharId != 0 && PktCharId != SelfCharId)
	{
		UE_LOG(LogAsdaPost, Warning, TEXT("[элитный ящик] 11005 не наш: CharIndex=%d, у нас %d — пакет отброшен"),
			PktCharId, SelfCharId);
		return;
	}

	// Шаг 3: чистка списка и 7 записей строк.
	for (FAsdaPremiumPostRow& R : Rows) { R = FAsdaPremiumPostRow(); }
	CellCodes.Init(-1, kRows * kCols);

	// Шаг 4: `slotCount = (len - 4) / 10`.
	const int32 RawCount = (Body.Num() - 4) / 10;
	// ★★★ ИСПРАВЛЕНИЕ ДЕФЕКТА C (закон `fix-original-defects`).
	//   Сервер `RecvDB_PremiumItemList_Res@0x4F8050` число записей НЕ ограничивает (буфер 649 б =
	//   до 64 записей), а приёмник эталонного клиента — `_WORD slotArray[36]` (72 б), то есть
	//   ровно 7 записей: `memcpy(slotArray, src, 10 * N)` при N > 7 переполняет стек.
	//   Читаем не больше 7 и громко пишем, сколько отбросили.
	const bool bMulti = CVarPostMultiItemRows.GetValueOnGameThread() != 0;
	if (bMulti)
	{
		// ★★★ [ИСПРАВЛЕНИЕ] 25.10.2026 строка = заказ: записи одного `Order` идут подряд (UPCashitemList premrows сортирует по
		//   Orderidx) — собираем их в одну строку окна. Потолок: 49 записей (DBPatch P85), 7 строк, 7 ячеек строки.
		const int32 RecN = FMath::Clamp(RawCount, 0, kMaxRecs);
		if (RawCount > kMaxRecs)
		{
			UE_LOG(LogAsdaPost, Error, TEXT("[элитный ящик] ★ 11005 принёс %d записей при потолке %d — лишние %d отброшены"),
				RawCount, kMaxRecs, RawCount - kMaxRecs);
		}
		int32 RowN = 0, Dropped = 0;
		for (int32 i = 0; i < RecN; ++i)
		{
			const uint8* P = &Body[4 + 10 * i];
			const int32 Order = AsdaBytes::RdI32(&P[0]);
			FAsdaPremiumPostRec Rec;
			Rec.ItemIndex = AsdaBytes::RdI32(&P[4]);
			Rec.Count     = FMath::Clamp((int32)P[8], 0, kCols);   // дефект B: Count уходит обратно в 11006 — потолок 7
			Rec.BuyType   = (int32)P[9];
			int32 Row = -1;
			for (int32 k = 0; k < RowN; ++k) { if (Rows[k].Order == Order) { Row = k; break; } }
			if (Row < 0)
			{
				if (RowN >= kRows) { ++Dropped; continue; }
				Row = RowN++;
				Rows[Row].Order = Order;
			}
			Rows[Row].Recs.Add(Rec);
		}
		for (int32 r = 0; r < RowN; ++r)
		{
			FAsdaPremiumPostRow& R = Rows[r];
			R.ItemIndex = R.Recs[0].ItemIndex;
			R.BuyType   = R.Recs[0].BuyType;
			int32 Cells = 0;
			for (const FAsdaPremiumPostRec& Rc : R.Recs) { Cells += Rc.Count; }
			R.Count = FMath::Clamp(Cells, 0, kCols);
			if (Cells > kCols)
			{
				UE_LOG(LogAsdaPost, Warning, TEXT("[элитный ящик] заказ %d: %d единиц при %d ячейках строки — показаны первые %d, забор берёт все"),
					R.Order, Cells, kCols, kCols);
			}
			FillRowCells(r);
		}
		if (Dropped > 0)
		{
			UE_LOG(LogAsdaPost, Warning, TEXT("[элитный ящик] 11005: записей восьмого и дальше заказов %d — не показаны (в окне 7 строк)"), Dropped);
		}
		UE_LOG(LogAsdaPost, Log, TEXT("[элитный ящик] ★ строка = заказ: записей %d → строк %d"), RecN, RowN);
	}
	const int32 Count = bMulti ? 0 : FMath::Clamp(RawCount, 0, kRows);
	if (!bMulti && RawCount > kRows)
	{
		UE_LOG(LogAsdaPost, Error,
			TEXT("[элитный ящик] ★ 11005 принёс %d записей при потолке %d. В ОРИГИНАЛЕ это переполнение ")
			TEXT("стека клиента (`_WORD slotArray[36]`, 72 б). Лишние %d отброшены — исправление дефекта C."),
			RawCount, kRows, RawCount - kRows);
	}

	for (int32 i = 0; i < Count; ++i)
	{
		const uint8* P = &Body[4 + 10 * i];
		FAsdaPremiumPostRow& R = Rows[i];
		R.Order     = AsdaBytes::RdI32(&P[0]);
		R.ItemIndex = AsdaBytes::RdI32(&P[4]);
		R.BuyType   = (int32)P[9];
		const int32 RawCnt = (int32)P[8];
		// ★★★ ИСПРАВЛЕНИЕ ДЕФЕКТА B, половина «приём».
		//   `Count` едет обратно на сервер внутри 11006, а там (`Recv_PremiumItemToInven_Req
		//   @0x4F8340`, цикл `0x4F87F3`) массив слотов — ровно 7 слов на стеке: `i == 7` затирает
		//   поле `BuyType`, `i >= 8` пишет за пределы 87-байтовой структуры. Клампим здесь, чтобы
		//   некорректное значение вообще не попало в модель.
		R.Count = FMath::Clamp(RawCnt, 0, kCols);
		if (RawCnt != R.Count)
		{
			UE_LOG(LogAsdaPost, Error,
				TEXT("[элитный ящик] ★ строка %d: сервер прислал Count=%d (потолок %d). Ограничено до %d — ")
				TEXT("исправление дефекта B (иначе такой Count ломает кадр серверного обработчика 11006)."),
				i, RawCnt, kCols, R.Count);
		}
		// Шаг 5: в строке рисуется `Count` ОДИНАКОВЫХ иконок предмета (эталонный внутренний цикл
		//   `for (iconRepeat = 0; iconRepeat < Count; ++iconRepeat)`).
		for (int32 c = 0; c < R.Count && c < kCols; ++c)
		{
			CellCodes[i * kCols + c] = R.ItemIndex;
		}
	}

	// ★ 25.10.2026: дальше по коду `Count` = число строк (у режима «строка = заказ» — собранных выше).
	const int32 RowsFilled = bMulti ? GetFilledRowCount() : Count;
	// ═══ ★★★ САМОДИАГНОСТИКА «ДВА ЗАКАЗА С ОДНИМ Orderidx» (нашей добавки, в эталоне её нет) ═══
	//   ЗАЧЕМ. Забор строки (11006) уходит на сервер ТОЛЬКО значением `Order`; хранимые процедуры
	//   `UPCashitemToinvenGear` / `UPCashitemToinvenFunctional` ищут строку так:
	//       SELECT @pCode=Code FROM dbo.ItemPostbox
	//        WHERE Userindex=@Userindex AND Charidx=@Charidx AND Orderidx=@Orderidx
	//   Ключ НЕ уникален (в `Order` лежит цена/остаток, а не номер заказа — см. `FAsdaPremiumPostRow`).
	//   При двух строках с одним `Order` скалярный SELECT оставляет в `@pCode` код ПРОИЗВОЛЬНОЙ
	//   (последней просмотренной) строки, и дальше `IF @pCode<>@Code` → `Flag=0` → 11007 статус 0:
	//   забрать нельзя НИ ОДНУ из строк, кроме той единственной, что «выиграла» у SELECT.
	//   А когда она всё-таки выигрывает — финальный `DELETE … WHERE Orderidx=@Orderidx` сносит ВСЕ
	//   строки с этим `Order`, то есть предметы-близнецы пропадают, не попав в рюкзак.
	//   Доказано на живом сервере 30.08.2026 (лог `DBProcError_20260830.txt`, журнал
	//   `JoyLog1.dbo.CashBuyGear` и текст самих процедур) — разбор в `Reports/elite_mailbox_dup_order.md`.
	{
		int32 DupPairs = 0;                       // сколько строк оказались не единственными со своим Order
		FString DupTrace;                         // человекочитаемый список «Order → коды предметов»
		for (int32 i = 0; i < RowsFilled; ++i)
		{
			if (Rows[i].IsEmpty()) { continue; }
			// Первое вхождение этого `Order` — собираем всю его группу; повторные пропускаем.
			bool bFirst = true;
			for (int32 k = 0; k < i; ++k) { if (!Rows[k].IsEmpty() && Rows[k].Order == Rows[i].Order) { bFirst = false; break; } }
			if (!bFirst) { continue; }
			FString Codes = FString::Printf(TEXT("%d"), Rows[i].ItemIndex);
			int32 InGroup = 1;
			for (int32 k = i + 1; k < RowsFilled; ++k)
			{
				if (!Rows[k].IsEmpty() && Rows[k].Order == Rows[i].Order)
				{
					Codes += FString::Printf(TEXT(", %d"), Rows[k].ItemIndex);
					++InGroup;
				}
			}
			if (InGroup > 1)
			{
				DupPairs += InGroup;
				DupTrace += FString::Printf(TEXT("%sOrder %d → коды [%s]"), DupTrace.IsEmpty() ? TEXT("") : TEXT("; "),
					Rows[i].Order, *Codes);
			}
		}
		if (DupPairs > 0)
		{
			UE_LOG(LogAsdaPost, Error,
				TEXT("[элитный ящик] ★★★ ДЕФЕКТ ДАННЫХ СЕРВЕРА: %d строк(и) делят один Orderidx (%s). ")
				TEXT("Хранимая процедура ищет строку по (Userindex, Charidx, Orderidx) — ключ не уникален, ")
				TEXT("поэтому забор части этих строк ВЕРНЁТ 11007 статус 0, а удачный забор УНИЧТОЖИТ ")
				TEXT("близнецов вместе с забранной строкой. Лечится миграцией ")
				TEXT("AstoryServer/Docs/SQL/elite_mailbox_dup_order_01_migrate.sql"),
				DupPairs, *DupTrace);
		}
	}

	// ★ ДИАГНОСТИКА ПОРЯДКА (нашей добавки, в эталоне такого лога нет). Печатаем `Order` строк
	//   РОВНО в том порядке, в котором их прислал сервер — это и есть доказательство того, по
	//   какому полю (и в какую сторону) сортирует хранимая процедура `UPCashitemList`.
	if (RowsFilled > 0)
	{
		FString OrderTrace;
		for (int32 i = 0; i < RowsFilled; ++i)
		{
			OrderTrace += FString::Printf(TEXT("%s%d(код %d, тип %d)"), i ? TEXT(" → ") : TEXT(""),
				Rows[i].Order, Rows[i].ItemIndex, Rows[i].BuyType);
		}
		UE_LOG(LogAsdaPost, Log, TEXT("[элитный ящик] порядок ОТ СЕРВЕРА (эталон его не меняет): %s"), *OrderTrace);
	}

	// ═══ ★★★ РАСШИРЕНИЕ `asda.EliteMail.SortNewestFirst` (разбор — у объявления ключа) ═══
	//   Эталон строки НЕ переставляет: `netParseLocalSlotSync@0xEF0990` кладёт запись i в строку i.
	//   Здесь — необязательный переворот по убыванию `Order`. Сортировка СТАБИЛЬНАЯ, поэтому
	//   строки с равным `Order` (а он не уникален — в живом ящике было три `Order 10000`)
	//   сохраняют серверный порядок между собой, и пустые строки всегда остаются внизу.
	//   ★ Нумерация строк при этом не разъезжается: `CellCodes` перестраивается здесь же, выбор
	//     строки заново ставит `RefreshGrid` эталонным перебором `k = 6…0`, а в 11006 уходит не
	//     номер строки, а сама запись (`Order`/`ItemIndex`/`Count`/`BuyType`) — см. `TakeSelected`.
	if (RowsFilled > 1 && CVarPostSortNewestFirst.GetValueOnGameThread() != 0)
	{
		TArray<FAsdaPremiumPostRow> Sorted;
		Sorted.Reserve(RowsFilled);
		for (int32 i = 0; i < RowsFilled; ++i) { Sorted.Add(Rows[i]); }
		Sorted.StableSort([](const FAsdaPremiumPostRow& A, const FAsdaPremiumPostRow& B)
		{
			if (A.IsEmpty() != B.IsEmpty()) { return B.IsEmpty(); }   // пустые — вниз
			return A.Order > B.Order;                                 // больший Order — выше
		});

		CellCodes.Init(-1, kRows * kCols);
		for (int32 i = 0; i < RowsFilled; ++i)
		{
			Rows[i] = Sorted[i];
			FillRowCells(i);   // ★ 25.10.2026: ячейки — из записей строки (у строки-заказа они разные)
		}
		UE_LOG(LogAsdaPost, Warning,
			TEXT("[элитный ящик] ★ РАСШИРЕНИЕ asda.EliteMail.SortNewestFirst=1: строки переставлены ")
			TEXT("по убыванию Order. Это НЕ поведение оригинала; и пока сервер кладёт в Order цену ")
			TEXT("покупки, наверху окажутся самые ДОРОГИЕ, а не самые новые."));
	}

	// Шаг 6: если окно ящика НЕ видно и список непустой — иконка уведомления слота 15 квик-бара.
	//   Эталон вешает её на ПЕРВОЙ же записи (`firstWarn`), повторно не вешает (`statusBarAddIcon`
	//   сам отсекает дубль типа 15).
	//   ★ Замечание владельца №6: сам факт «иконка должна висеть» запоминается ЗДЕСЬ, а вешает её
	//     (и перевешивает после смены карты) `TickWorldEntry` — панель квик-бара живёт своей
	//     жизнью и на момент прихода 11005 может быть ещё не поднята либо только что пересоздана.
	//   ★★★ НАШЕ НОВОВВЕДЕНИЕ (требование владельца 2026-08-20, НЕ эталон): иконку зажигает не сам
	//     факт «список непустой», а число ЕЩЁ НЕ ПРОСМОТРЕННЫХ заказов. Разбор — у
	//     `ReconcileSeenOrders` выше и в `Reports/elite_mailbox_seen.md`.

	// ★ Смена персонажа БЕЗ перезапуска клиента (выход в выбор персонажа и вход другим): ящик
	//   у каждого персонажа свой, чужой набор просмотренного применять нельзя. `PktCharId` —
	//   `CharIndex` из самого пакета, то есть владелец этого списка (это charId i32, а не
	//   сессионный userNum i16 — закон `charid-vs-usernum`).
	if (SeenOwnerCharId != PktCharId)
	{
		if (SeenOwnerCharId != 0 && SeenOrders.Num() > 0)
		{
			ResetSeenOrders(TEXT("сменился персонаж — набор прошлого персонажа не наш"));
		}
		SeenOrders.Reset();
		SeenOwnerCharId = PktCharId;
	}

	if (bSeenGateEnabled)
	{
		ReconcileSeenOrders();                 // сжали набор до подмножества списка + посчитали новые
		// Список пришёл, пока окно открыто ⇒ игрок смотрит прямо на него ⇒ всё просмотрено.
		if (IsWindowVisible()) { MarkCurrentListSeen(TEXT("список пришёл при открытом окне ящика")); }
		bNoticeWanted = (UnseenCount > 0);
	}
	else
	{
		// Откат к эталону (`asda.Post.SeenGate 0`): иконка на каждый непустой список.
		SeenOrders.Reset();
		UnseenCount = RowsFilled;
		bNoticeWanted = (RowsFilled > 0);
	}

	// ★ ИСПРАВЛЕНИЕ ДЕФЕКТА ОРИГИНАЛА (реверс §1.4), сохранено: в эталоне иконка ящика НЕ гаснет ни
	//   по таймеру, ни по опустошению — `statusBarAddIcon` взводит для типа 15 таймер `0x1E` на
	//   30 с, а обработчик `WM_TIMER case 0x1E` снимает ТОЛЬКО тип 11 (и попутно сносит чужую
	//   иконку «шёпот»). Здесь `ApplyNoticeIcon` снимает её и при пустом ящике (`Count == 0` ⇒
	//   `UnseenCount == 0`), и — по нововведению — когда всё содержимое уже просмотрено.
	ApplyNoticeIcon(RowsFilled == 0
		? TEXT("ящик пуст")
		: (bNoticeWanted ? TEXT("в ящике есть непросмотренные заказы")
		                 : TEXT("★ нововведение: весь ящик уже просмотрен в этой сессии")));

	// Шаг 7: перерисовка 7×7 + эталонный авто-выбор строки.
	RefreshGrid();

	UE_LOG(LogAsdaPost, Log,
		TEXT("[элитный ящик] 11005 разобран: строк %d (из %d), выбрана строка %d; ★ непросмотренных ")
		TEXT("заказов %d, просмотренных %d, иконка нужна: %s"),
		RowsFilled, RawCount, SelectedRow, UnseenCount, SeenOrders.Num(), bNoticeWanted ? TEXT("ДА") : TEXT("нет"));
}

// ★ U6 (В1 дедупа №2): запись через `FAsdaPacketWriter` (байты те же, что у прежнего ручного буфера). Запись 10 б пакета 11006: +0 Order, +4 ItemIndex, +8 количество (кламп 0…kCols), +9 BuyType.
TArray<uint8> UAsdaPremiumPostSubsystem::BuildToInvenRec11006(int32 Order, int32 ItemIndex, int32 Count, int32 BuyType)
{
	FAsdaPacketWriter W(10);
	W.I32(0, Order);
	W.I32(4, ItemIndex);
	W.U8(8, FMath::Clamp(Count, 0, kCols));
	W.U8(9, BuyType);
	return W.ToArray();
}

// ═══ ★★★ [ИСПРАВЛЕНИЕ] 25.10.2026 СТРОКА = ЗАКАЗ: ячейки, место, отправка одной записи ═══
void UAsdaPremiumPostSubsystem::FillRowCells(int32 Row)
{
	if (!Rows.IsValidIndex(Row)) { return; }
	const FAsdaPremiumPostRow& R = Rows[Row];
	for (int32 c = 0; c < kCols; ++c) { CellCodes[Row * kCols + c] = -1; }
	int32 Col = 0;
	if (R.Recs.Num() == 0)   // строка старого разбора: Count одинаковых ячеек
	{
		for (; Col < R.Count && Col < kCols; ++Col) { CellCodes[Row * kCols + Col] = R.ItemIndex; }
		return;
	}
	for (const FAsdaPremiumPostRec& Rc : R.Recs)
	{
		for (int32 k = 0; k < Rc.Count && Col < kCols; ++k) { CellCodes[Row * kCols + Col++] = Rc.ItemIndex; }
	}
}

// Контейнер выдачи — повтор `sub_5340B0@0x5340B0` (GameServer.c, ветка sub_532A90): item_list с классом +36 == 3 или типом
//   +26 ∈ {0…4, 8, 9, 12…15} → 1; прочий item_list → 2; спец-таблица (код ≤ 558) → 1; самоцветы (7001…) → 2.
//   У клиента поле +36 — `CategoryFlag`, +26 — `ItemType` (те же колонки 24 и 16 item_table.txt).
int32 UAsdaPremiumPostSubsystem::ContainerForCode(int32 ItemCode) const
{
	const UGameInstance* GI = GetGameInstance();
	const UAsdaItemTableSubsystem* IT = GI ? GI->GetSubsystem<UAsdaItemTableSubsystem>() : nullptr;
	if (const FAsdaItemRecord* Rec = IT ? IT->GetItem(ItemCode) : nullptr)
	{
		const int32 Ty = Rec->ItemType;
		const bool bGear = Rec->CategoryFlag == 3 || (Ty >= 0 && Ty <= 4) || Ty == 8 || Ty == 9 || (Ty >= 12 && Ty <= 15);
		return bGear ? 1 : 2;
	}
	return (ItemCode > 7000) ? 2 : 1;
}

int32 UAsdaPremiumPostSubsystem::FreeCells(int32 Container) const
{
	if (Container == 1 && DebugFree1 >= 0) { return DebugFree1; }
	if (Container == 2 && DebugFree2 >= 0) { return DebugFree2; }
	const UGameInstance* GI = GetGameInstance();
	const UAsdaInventorySubsystem* Inv = GI ? GI->GetSubsystem<UAsdaInventorySubsystem>() : nullptr;
	if (!Inv) { return 0; }
	int32 N = 0;
	const int32 Size = Inv->GetContainerSize(Container);
	for (int32 i = 0; i < Size; ++i)
	{
		const FAsdaItemInstance* C = Inv->GetCell(Container, i);
		if (!C || C->IsEmpty()) { ++N; }
	}
	return N;
}

// Место под ВСЕ строки разом: по ячейке на единицу в своём контейнере. Стопки не учитываются (у товаров премиум-витрины
//   их нет: CharChargeShopBuy пускает только класс 0/3), значит оценка не мягче серверной.
bool UAsdaPremiumPostSubsystem::CheckRoomForRows(const TArray<int32>& RowIdx) const
{
	int32 Need[3] = { 0, 0, 0 };
	for (const int32 Idx : RowIdx)
	{
		const FAsdaPremiumPostRow& R = Rows[Idx];
		if (R.Recs.Num() == 0) { Need[ContainerForCode(R.ItemIndex)] += R.Count; }
		for (const FAsdaPremiumPostRec& Rc : R.Recs) { Need[ContainerForCode(Rc.ItemIndex)] += Rc.Count; }
	}
	const int32 Free1 = FreeCells(1), Free2 = FreeCells(2);
	const bool bOk = Need[1] <= Free1 && Need[2] <= Free2;
	if (!bOk)
	{
		UE_LOG(LogAsdaPost, Log, TEXT("[элитный ящик] заказов %d: мест не хватает — сумка 1 нужно %d (свободно %d), сумка 2 нужно %d (свободно %d)"),
			RowIdx.Num(), Need[1], Free1, Need[2], Free2);
	}
	return bOk;
}

TArray<int32> UAsdaPremiumPostSubsystem::PickedRows() const
{
	TArray<int32> Out;
	for (int32 k = 0; k < Rows.Num(); ++k)
	{
		if (IsRowChecked(k)) { Out.Add(k); }
	}
	return Out;
}

bool UAsdaPremiumPostSubsystem::StartNextTakeRow()
{
	if (TakeQueue.Num() == 0) { return false; }
	const FAsdaPremiumPostRow R = TakeQueue[0];
	TakeQueue.RemoveAt(0);
	TakeLeft = R.Recs;
	if (TakeLeft.Num() == 0)   // строка из старого разбора (без записей) — одна запись из полей строки
	{
		FAsdaPremiumPostRec One;
		One.ItemIndex = R.ItemIndex; One.Count = R.Count; One.BuyType = R.BuyType;
		TakeLeft.Add(One);
	}
	TakeOrder = R.Order;
	TakeTotal = TakeLeft.Num();
	TakeOk = 0;
	const FAsdaPremiumPostRec First = TakeLeft[0];
	TakeLeft.RemoveAt(0);
	if (!SendTakeRec(TakeOrder, First)) { return false; }
	UE_LOG(LogAsdaPost, Log, TEXT("[элитный ящик] забор заказа %d: записей %d, ячеек %d, в очереди заказов %d"),
		R.Order, TakeTotal, R.Count, TakeQueue.Num());
	return true;
}

void UAsdaPremiumPostSubsystem::ResetTake()
{
	TakeLeft.Reset();
	TakeQueue.Reset();
	TakeRowsOk = 0;
	bTakeRun = false;
	bTakeSendNext = false;
	bAwaitSwap = false;
	if (bPreChoiceWait)   // окно выбора до получения закрываем молча: 11006 по нему не уходил
	{
		bPreChoiceWait = false;
		if (UAsdaPremiumGachaSubsystem* Gacha = GetGameInstance() ? GetGameInstance()->GetSubsystem<UAsdaPremiumGachaSubsystem>() : nullptr)
		{
			Gacha->CancelPreTakeChoice();
		}
	}
}

// Те же условия, что ветка гачи в `ApplyResultBody` (`inventorySyncApplyPacket`, запись j == 0), но ДО отправки:
//   контейнер берём из таблицы предметов (`ContainerForCode`), BuyType — из записи 11005.
bool UAsdaPremiumPostSubsystem::RecNeedsChoice(const FAsdaPremiumPostRec& Rec) const
{
	const UGameInstance* GI = GetGameInstance();
	const UAsdaItemTableSubsystem* Items = GI ? GI->GetSubsystem<UAsdaItemTableSubsystem>() : nullptr;
	const UAsdaPremiumGachaSubsystem* Gacha = GI ? GI->GetSubsystem<UAsdaPremiumGachaSubsystem>() : nullptr;
	const FAsdaItemRecord* Rc = Items ? Items->GetItem(Rec.ItemIndex) : nullptr;
	if (!Rc || !Gacha || Rec.BuyType != 3) { return false; }
	const bool bLinkOk = (Rc->LinkWordCode == 0) || ((uint16)Rc->LinkWordCode > 0x63);
	const bool bOuter = (ContainerForCode(Rec.ItemIndex) == 1 || Rc->ItemSubType == 200 || Rc->ItemSubType == 201 || Rc->ItemSubType == 66) && bLinkOk;
	const bool bInner = Rc->ItemType <= 4 || (Rc->ItemType >= 8 && Rc->ItemType <= 9)
		|| Rc->ItemSubType == 200 || Rc->ItemSubType == 201 || Rc->ItemSubType == 66;
	return bOuter && bInner && Gacha->PostItemHasVariantChoice(Rec.ItemIndex);
}

bool UAsdaPremiumPostSubsystem::SendTakeRec(int32 Order, const FAsdaPremiumPostRec& Rec)
{
	if (CVarPostChooseBeforeTake.GetValueOnGameThread() != 0 && RecNeedsChoice(Rec))
	{
		UAsdaPremiumGachaSubsystem* Gacha = GetGameInstance() ? GetGameInstance()->GetSubsystem<UAsdaPremiumGachaSubsystem>() : nullptr;
		if (Gacha && Gacha->OpenPreTakeChoice(Rec.ItemIndex))
		{
			bPreChoiceWait = true;
			PreChoiceOrder = Order;
			PreChoiceRec = Rec;
			UE_LOG(LogAsdaPost, Log, TEXT("[элитный ящик] заказ %d, предмет %d: сначала выбор варианта, 11006 пока не шлём"), Order, Rec.ItemIndex);
			return true;
		}
		// Без выбора не забираем: иначе выдался бы вариант по умолчанию — тот самый дефект. Заказ остаётся в ящике.
		UE_LOG(LogAsdaPost, Warning, TEXT("[элитный ящик] заказ %d, предмет %d: окно выбора не открылось (занято или не поднялось) — 11006 не шлём"), Order, Rec.ItemIndex);
		return false;
	}
	return SendTakeRecNow(Order, Rec);
}

void UAsdaPremiumPostSubsystem::OnPreTakeChoiceDone(bool bChosen, int32 BaseCode, int32 Variant)
{
	if (!bPreChoiceWait) { return; }
	bPreChoiceWait = false;
	UAsdaPremiumGachaSubsystem* Gacha = GetGameInstance() ? GetGameInstance()->GetSubsystem<UAsdaPremiumGachaSubsystem>() : nullptr;
	if (!bChosen)
	{
		UE_LOG(LogAsdaPost, Log, TEXT("[элитный ящик] выбор варианта отменён: заказ %d (предмет %d) остаётся в ящике, 11006 не уходил"),
			PreChoiceOrder, BaseCode);
		SkipPreChoiceOrder();
		return;
	}
	if (Gacha) { Gacha->ArmPostVariant(BaseCode, Variant); }
	if (!SendTakeRecNow(PreChoiceOrder, PreChoiceRec))
	{
		if (Gacha) { Gacha->DisarmPostVariant(); }
		ResetTake();
	}
}

void UAsdaPremiumPostSubsystem::SkipPreChoiceOrder()
{
	CheckedOrders.Remove(PreChoiceOrder);   // отказ от заказа = снять с него отметку, сам заказ в ящике
	RefreshSelectionVisual();
	if (!bTakeRun) { return; }               // одиночный забор (`MultiItemRows 0`): больше ничего не ждём
	const bool bAnyTaken = TakeRowsOk > 0 || TakeOk > 0;
	TakeLeft.Reset();                        // остаток записей этого заказа тоже не шлём
	if (TakeQueue.Num() > 0)
	{
		bTakeSendNext = true;                // следующие отмеченные заказы — из Tick
		return;
	}
	ResetTake();
	if (bAnyTaken)                           // что-то уже взято: нотис и перечитать ящик, как в конце очереди
	{
		UGameInstance* GI = GetGameInstance();
		if (UAsdaMessageSubsystem* Msg = GI ? GI->GetSubsystem<UAsdaMessageSubsystem>() : nullptr) { Msg->PostLoc(EAsdaMsgCategory::Trade, kLocGranted); }
		UAsdaNetSubsystem* Net = GI ? GI->GetSubsystem<UAsdaNetSubsystem>() : nullptr;
		if (Net && !bRequestInFlight && Net->SendPremiumPostListReq11004()) { bRequestInFlight = true; }
	}
}

bool UAsdaPremiumPostSubsystem::SendTakeRecNow(int32 Order, const FAsdaPremiumPostRec& Rec)
{
	const TArray<uint8> Rec10 = BuildToInvenRec11006(Order, Rec.ItemIndex, Rec.Count, Rec.BuyType);
#if ASDA_DEV_CONSOLE
	if (bDebugTakeCapture)   // самотест: в сокет не уходит
	{
		DebugTakeSentRecs.Add(Rec10);
		bRequestInFlight = true;
		return true;
	}
#endif
	UAsdaNetSubsystem* Net = GetGameInstance() ? GetGameInstance()->GetSubsystem<UAsdaNetSubsystem>() : nullptr;
	if (Net && Net->SendPremiumPostToInven11006(Rec10.GetData()))
	{
		bRequestInFlight = true;
		UE_LOG(LogAsdaPost, Log, TEXT("[элитный ящик] 11006 отправлен: заказ %d, предмет %d ×%d, BuyType %d"),
			Order, Rec.ItemIndex, Rec.Count, Rec.BuyType);
		return true;
	}
	UE_LOG(LogAsdaPost, Warning, TEXT("[элитный ящик] 11006 не ушёл (нет связи): заказ %d, предмет %d"), Order, Rec.ItemIndex);
	return false;
}

int32 UAsdaPremiumPostSubsystem::DebugRowCellCount(int32 Row) const
{
	int32 N = 0;
	for (int32 c = 0; c < kCols; ++c) { if (CellCodes.IsValidIndex(Row * kCols + c) && CellCodes[Row * kCols + c] > 0) { ++N; } }
	return N;
}

// ═══ Порт `net_sendSlotSyncOpcode11006@0xEF0CD0` (реверс §2.3) ═══
void UAsdaPremiumPostSubsystem::TakeSelected()
{
	if (!Rows.IsValidIndex(SelectedRow)) { return; }
	const FAsdaPremiumPostRow& R = Rows[SelectedRow];

	// Гвард 1 эталона: `this[636 + 10*sel + 4] != -1` — это поле ItemIndex, а НЕ Order
	//   (адрес в декомпиле `+640`, то есть запись+4). Пустую строку не отправляем.
	if (R.IsEmpty())
	{
		UE_LOG(LogAsdaPost, Log, TEXT("[элитный ящик] строка %d пуста — 11006 не отправляем (гвард эталона)"),
			SelectedRow);
		return;
	}
	// Гвард 2 эталона: `this[616] != 1` — предыдущий запрос ещё не отвечен.
	if (bRequestInFlight || bTakeRun || bPreChoiceWait || bAwaitSwap)
	{
		UE_LOG(LogAsdaPost, Log, TEXT("[элитный ящик] предыдущий запрос ещё в полёте — 11006 не отправляем"));
		return;
	}

	// ★★★ [ИСПРАВЛЕНИЕ] 25.10.2026 строка = заказ (`asda.EliteMail.MultiItemRows`): забор ВСЕЙ строки. Сначала место под все
	//   её ячейки (иначе часть заказа легла бы в сумку, а часть — отказом 3/5 осталась), затем 11006 первой записи; остальные —
	//   из Tick после 11007 код 1. Запись 11006 — ровно запись 11005 (дефект B: Count не редактируется).
	if (CVarPostMultiItemRows.GetValueOnGameThread() != 0)
	{
		const TArray<int32> Picked = PickedRows();
		if (Picked.Num() == 0)
		{
			UE_LOG(LogAsdaPost, Log, TEXT("[элитный ящик] ни один заказ не отмечен — 11006 не отправляем"));
			return;
		}
		if (!CheckRoomForRows(Picked))
		{
			if (UAsdaMessageSubsystem* Msg = GetGameInstance() ? GetGameInstance()->GetSubsystem<UAsdaMessageSubsystem>() : nullptr)
			{
				Msg->PostLoc(EAsdaMsgCategory::ItemRefused, kLocNoRoom);   // «Нет места в рюкзаке.» (строка статуса 3 оригинала)
			}
			return;
		}
		ResetTake();
		for (const int32 Idx : Picked) { TakeQueue.Add(Rows[Idx]); }
		bTakeRun = true;
		if (!StartNextTakeRow()) { ResetTake(); }
		return;
	}

	// ★★★ ДЕФЕКТ B, половина «отправка»: в пакет уходит РОВНО то, что пришло в 11005.
	//   UI `Count` не редактирует и не может; значение уже проклампировано семью на приёме,
	//   а `SendPremiumPostToInven11006` проверит его ещё раз перед сокетом.
	const TArray<uint8> Rec10 = BuildToInvenRec11006(R.Order, R.ItemIndex, R.Count, R.BuyType);   // ★ U6: сборщик ниже

	// ★★★ ПРЕДУПРЕЖДЕНИЕ ЗАРАНЕЕ (наша добавка). Если у выбранной строки есть близнец с тем же
	//   `Order`, ответ 11007 почти наверняка придёт со статусом 0 — и это НЕ ошибка разбора пакета,
	//   а неуникальный ключ в `dbo.ItemPostbox` (полный разбор — у самодиагностики в `ApplyListBody`).
	//   Пишем это ДО отправки, чтобы в логе причина стояла раньше следствия.
	{
		int32 Twins = 0;                                   // сколько ещё строк делят `Order` с выбранной
		for (int32 k = 0; k < Rows.Num(); ++k)
		{
			if (k != SelectedRow && !Rows[k].IsEmpty() && Rows[k].Order == R.Order) { ++Twins; }
		}
		if (Twins > 0)
		{
			UE_LOG(LogAsdaPost, Error,
				TEXT("[элитный ящик] ★ у строки %d (Order %d, код %d) есть ещё %d строк(а) с тем же Order. ")
				TEXT("Ожидаем 11007 статус 0 (процедура сверит Code произвольного близнеца), а при удаче — ")
				TEXT("потерю близнецов: DELETE в процедуре идёт по одному лишь Orderidx."),
				SelectedRow, R.Order, R.ItemIndex, Twins);
		}
	}

	// [ИСПРАВЛЕНИЕ] 10.11.2026 «выбор до получения»: коробку с выбором варианта сначала спрашиваем, 11006 — после ОК.
	{
		FAsdaPremiumPostRec One;
		One.ItemIndex = R.ItemIndex; One.Count = R.Count; One.BuyType = R.BuyType;
		if (CVarPostChooseBeforeTake.GetValueOnGameThread() != 0 && RecNeedsChoice(One))
		{
			SendTakeRec(R.Order, One);
			return;
		}
	}

	UAsdaNetSubsystem* Net = GetGameInstance() ? GetGameInstance()->GetSubsystem<UAsdaNetSubsystem>() : nullptr;
	if (Net && Net->SendPremiumPostToInven11006(Rec10.GetData()))
	{
		bRequestInFlight = true;
		UE_LOG(LogAsdaPost, Log,
			TEXT("[элитный ящик] 11006 отправлен: строка %d, заказ %d, предмет %d ×%d, BuyType %d"),
			SelectedRow, R.Order, R.ItemIndex, R.Count, R.BuyType);
	}
}

// ═══ Порт `inventorySyncApplyPacket@0xEF1320` (реверс §2.4) ═══
void UAsdaPremiumPostSubsystem::ApplyResultBody(const TArray<uint8>& B)
{
	UGameInstance* GI = GetGameInstance();
	UAsdaNetSubsystem* Net = GI ? GI->GetSubsystem<UAsdaNetSubsystem>() : nullptr;
	UAsdaInventorySubsystem* Inv = GI ? GI->GetSubsystem<UAsdaInventorySubsystem>() : nullptr;
	UAsdaMessageSubsystem* Msg = GI ? GI->GetSubsystem<UAsdaMessageSubsystem>() : nullptr;
	UAsdaItemTableSubsystem* Items = GI ? GI->GetSubsystem<UAsdaItemTableSubsystem>() : nullptr;

	bRequestInFlight = false;   // эталон: `a1[616] = 0` первой строкой
	// [ИСПРАВЛЕНИЕ] 10.11.2026 «выбор до получения»: заряд варианта живёт ровно один 11007 — на любом выходе ниже
	//   (отказ, чужой персонаж, короткое тело) он снимается, иначе устаревший вариант ушёл бы в 6581 позже.
	UAsdaPremiumGachaSubsystem* GachaArm = GI ? GI->GetSubsystem<UAsdaPremiumGachaSubsystem>() : nullptr;
	ON_SCOPE_EXIT { if (GachaArm) { GachaArm->DisarmPostVariant(); } };
	if (B.Num() < 409) { return; }

	// Сверка `CharIndex` (@1) с локальным персонажем.
	const int32 PktCharId = AsdaBytes::RdI32(&B[1]);
	const int32 SelfCharId = Net ? Net->SelectedCharId : 0;
	if (SelfCharId != 0 && PktCharId != SelfCharId)
	{
		UE_LOG(LogAsdaPost, Warning, TEXT("[элитный ящик] 11007 не наш: CharIndex=%d, у нас %d"), PktCharId, SelfCharId);
		return;
	}

	const int32 Status = (int32)B[0];
	if (Status != 1)
	{
		// ═══ ★★★ ИСПРАВЛЕНИЕ ДЕФЕКТА ОРИГИНАЛА (закон `fix-original-defects`) ═══
		//   ЖАЛОБА ВЛАДЕЛЬЦА (2026-08-30): «клиент писал 0 при попытке получить предмет из
		//   премиум-ящика». Найдено в логе дословно: `LogAsdaChat: [сисчат A] 0`.
		//   ЭТО ПОВЕДЕНИЕ ЭТАЛОНА: `inventorySyncApplyPacket@0xEF1320` для статуса 3 берёт строку
		//   1011, а для ВСЕХ остальных кодов печатает в чат сам код через «%d» — игрок видит голое
		//   число без единого слова. Строки под коды 0/2/5 в таблице текста нет вообще.
		//   Ниже — наша расшифровка. Тексты не выдуманы: каждый взят из ветки, которая этот код
		//   ставит (`Recv_PremiumItemToInven_Req@0x4F8340`, `sub_4F89D0@0x4F89D0` в GameServer и
		//   хранимые процедуры `UPCashitemToinven{Gear|Functional}`, снятые с живой БД 30.08.2026).
		//   Код всё равно остаётся в скобках — по нему ищут в логах поддержки.
		//   Откат к эталонному голому числу: `asda.Post.RawStatusText 1`.
		if (Status == 3)
		{
			// Единственный код, у которого в оригинале ЕСТЬ своя строка: «Нет места в рюкзаке.»
			if (Msg) { Msg->PostLoc(EAsdaMsgCategory::ItemRefused, kLocNoRoom); }
		}
		else if (Msg)
		{
			// Расшифровка кода: что именно отказало на сервере.
			// [ПОРТ] 09.10.2026 (мультиязычность, этап 3): расшифровки — строки 990475…990478, сам нотис — 990479 (язык сессии).
			int32 WhyLoc = 990475;   // «сервер отказал без пояснения»
			switch (Status)
			{
			case 0:
				// `Flag=0` хранимой процедуры: `IF @pCode<>@Code OR @pCode is null`. То есть строки
				//   с таким Orderidx у персонажа нет ЛИБО её Code не совпал с запрошенным. На живом
				//   сервере это давали строки-близнецы с одинаковым Orderidx (см. `ApplyListBody`).
				WhyLoc = 990476;   // «заказ не найден в ящике или код предмета в нём не совпал»
				break;
			case 2:
				// `Recv_PremiumItemToInven_Req`: ItemIndex не опознан ни одной из трёх таблиц
				//   предметов сервера (`sub_532A90` / `sub_532AE0` / `sub_532B10`).
				WhyLoc = 990477;   // «сервер не знает такого предмета»
				break;
			case 5:
				// `sub_4F89D0`: при раскладке ответа кончились свободные ячейки (`sub_4D6740` = −1).
				WhyLoc = 990478;   // «в рюкзаке не хватило свободных ячеек»
				break;
			default:
				break;
			}
			const bool bRaw = CVarPostRawStatusText.GetValueOnGameThread() != 0;
			Msg->PostText(EAsdaMsgCategory::ItemRefused, bRaw
				? FString::Printf(TEXT("%d"), Status)
				: UAsdaTextSubsystem::FormatPrintf(FAsdaText::Loc(990479), { FAsdaText::Loc(WhyLoc), FString::FromInt(Status) }));
		}
		UE_LOG(LogAsdaPost, Warning,
			TEXT("[элитный ящик] 11007 отказ: статус=%d. Статус 0 = хранимая процедура ")
			TEXT("UPCashitemToinven{Gear|Functional} вернула Flag=0 (строки с таким Orderidx нет либо ")
			TEXT("её Code не совпал), 2 = предмет неизвестен серверу, 3 = нет места, 5 = нет ячейки."),
			Status);
		// [ИСПРАВЛЕНИЕ] 25.10.2026 строка = заказ: отказ останавливает забор строки; если часть уже взята — перезапросить список.
		if (bTakeRun)
		{
			UE_LOG(LogAsdaPost, Warning, TEXT("[элитный ящик] забор заказа %d остановлен отказом %d: взято %d из %d"),
				TakeOrder, Status, TakeOk, TakeTotal);
			const bool bAny = TakeOk > 0 || TakeRowsOk > 0;
			ResetTake();
			if (bAny && Net && !bRequestInFlight && Net->SendPremiumPostListReq11004()) { bRequestInFlight = true; }
		}
		return;
	}

	// Вес персонажа (@404). Эталон: `HUD_UpdateMiniWeightBar(g_pHUD, v19[101])`; у нас полосу
	//   рисует `UAsdaMiniStateSubsystem`, а единая точка записи веса — `Net->SetSelfWeight`.
	const int32 NewWeight = AsdaWeightFromWire(AsdaBytes::RdI32(&B[404]));   // ★ 29.10.2026: вес u16
	if (Net) { Net->SetSelfWeight(NewWeight, -1, TEXT("11007 элитный ящик")); }

	const int32 BuyType = (int32)B[408];
	bool bGachaShown = false;
	int32 Applied = 0, Cleared = 0;

	// 7 записей предмета по 57 б начиная с @5.
	//   ★ ДЕФЕКТ B, третья половина: приёмник эталона тоже ровно 7 записей — читаем не больше,
	//     сколько бы сервер ни насчитал по `Count`.
	for (int32 j = 0; j < kRows; ++j)
	{
		TArray<uint8> Rec;
		Rec.SetNumUninitialized(57);
		FMemory::Memcpy(Rec.GetData(), &B[5 + 57 * j], 57);
		const int32 Code   = AsdaBytes::RdI32(&Rec[0]);
		const int32 Cont   = (int32)Rec[4];
		const int32 Slot   = (int32)(*reinterpret_cast<const int16*>(Rec.GetData() + 5));
		const int32 Amount = AsdaBytes::RdI32(&Rec[9]);

		// Эталон различает записи ПО ПОЛЮ `amount` (`*(int*)(&v14[2] + 57*j + 1)` = запись+9):
		//   −1 ⇒ ячейку ОЧИСТИТЬ, ≥ 0 ⇒ ПОЛОЖИТЬ. Это ровно контракт `ApplyExternalRecord`
		//   (маршрут по полям самой записи: +4 контейнер, +5 слот, amount == −1 = очистка).
		if (Amount == -1)
		{
			if (Inv && Slot >= 0 && (Cont == 1 || Cont == 2)) { Inv->ApplyExternalRecord(Rec); ++Cleared; }
			continue;
		}
		if (Amount < 0) { continue; }
		// Пустой хвост ответа (сервер обнулил записи с индексом ≥ `Count`) — это НЕ ячейка:
		//   код 0 и контейнер 0 не адресуют ничего, эталон на такой записи тоже не пишет никуда.
		if (Code == 0 && Cont == 0) { continue; }
		// ★ Считаем ТОЛЬКО реально положенные записи. Сервер (`RecvDB_PremiumItemToInven_Res
		//   @0x4F89D0`) заполняет ровно `Count` записей из семи, а хвост оставляет нулевым —
		//   у нулевой записи контейнер 0 и слот 0, и эталон её тоже пропускает (пишет только
		//   при контейнере 1 или 2, item_inventory.c:66359-66369). Прежний счётчик инкрементился
		//   безусловно и печатал «положено 7» при одной положенной записи, а `ApplyExternalRecord`
		//   на каждую нулевую честно писал в лог предупреждение «вне модели» — шесть штук подряд.
		// ★★★ ОТСТУПЛЕНИЕ «не показывать предмет до выбора» (замечание владельца №2; по умолчанию
		//   ВЫКЛЮЧЕНО, разбор — `Reports/elite_mailbox_fix3.md` §2). Первая запись кладётся в
		//   модель НЕ здесь, а ниже — когда станет известно, откроется ли окно выбора варианта:
		//   если откроется, запись уходит в ожидание и попадёт в сумку только по итогу выбора.
		const bool bDeferCandidate = bDeferItemUntilChoice && (j == 0);
		bool bPut = false;
		if (!bDeferCandidate)
		{
			bPut = Inv && Inv->ApplyExternalRecord(Rec);
			if (bPut) { ++Applied; }
		}

		// ★ Ветка гачи — ТОЛЬКО для j == 0 (эталон `if (!j)`), со всеми его условиями.
		if (j != 0) { continue; }
		const FAsdaItemRecord* Rc = Items ? Items->GetItem(Code) : nullptr;
		if (!Rc)
		{
			// Окна выбора не будет ⇒ отложенное держать незачем, кладём немедленно.
			if (bDeferCandidate && Inv && Inv->ApplyExternalRecord(Rec)) { ++Applied; }
			continue;
		}
		// `canShowGachaPopup = linkWordCode ? (linkWordCode > 0x63) : 1`
		const bool bLinkOk = (Rc->LinkWordCode == 0) || ((uint16)Rc->LinkWordCode > 0x63);
		// `(container == 1 || subType == 200 || 201 || 66) && canShowGachaPopup`
		const bool bOuter = (Cont == 1 || Rc->ItemSubType == 200 || Rc->ItemSubType == 201 || Rc->ItemSubType == 66) && bLinkOk;
		if (!bOuter)
		{
			if (bDeferCandidate && Inv && Inv->ApplyExternalRecord(Rec)) { ++Applied; }
			continue;
		}
		// `(itemType <= 4 || 8…9 || subType 200/201/66) && BuyType == 3`
		const bool bInner = (Rc->ItemType <= 4 || (Rc->ItemType >= 8 && Rc->ItemType <= 9)
			|| Rc->ItemSubType == 200 || Rc->ItemSubType == 201 || Rc->ItemSubType == 66) && (BuyType == 3);
		if (!bInner)
		{
			if (bDeferCandidate && Inv && Inv->ApplyExternalRecord(Rec)) { ++Applied; }
			continue;
		}

		// ★★★ МОСТ «11007 → ГАЧА». Эталон здесь зовёт `uiItemFilterListPopulate(g_pGachaScene, code)`
		//   + `uiItemInfoPopupOpen(g_pGachaScene)`, но КОНТЕЙНЕР И СЛОТ приза в состояние сцены НЕ
		//   кладёт — из-за чего кнопка обмена варианта из этого окна уходила на сервер с
		//   контейнером 0 и слотом −1 (реверс §7.3). Мост делает и то, и другое.
		if (UAsdaPremiumGachaSubsystem* Gacha = GI ? GI->GetSubsystem<UAsdaPremiumGachaSubsystem>() : nullptr)
		{
			bGachaShown = Gacha->OnPrizeTakenFromPost(Rec);
		}
		// Итог по отложенной записи: окно выбора открылось — ждём выбора; не открылось — кладём.
		if (bDeferCandidate)
		{
			if (bGachaShown)
			{
				// Прошлое ожидание, если оно почему-то ещё висит, отдаём в сумку: терять предмет
				//   из МОДЕЛИ нельзя — на сервере он уже лежит.
				FlushDeferredRecord(true, TEXT("пришёл новый приз, а прошлый выбор так и не закрыли"));
				DeferredRec = Rec;
				bDeferActive = true;
				UE_LOG(LogAsdaPost, Log,
					TEXT("[элитный ящик] ★ предмет %d пока НЕ показан в инвентаре: ждём выбора варианта ")
					TEXT("(отступление от эталона, ключ asda.Post.HideUntilChoice)"), Code);
			}
			else if (Inv && Inv->ApplyExternalRecord(Rec))
			{
				++Applied;
			}
		}
	}

	// [ИСПРАВЛЕНИЕ] 10.11.2026 «выбор до получения»: заряд варианта живёт ровно до этого 11007 — мост его уже взял либо не звался.
	UAsdaPremiumGachaSubsystem* GachaSub = GI ? GI->GetSubsystem<UAsdaPremiumGachaSubsystem>() : nullptr;
	const bool bSwapWait = bGachaShown && GachaSub && GachaSub->IsPostSwapPending();   // 6581 по выбранному варианту в полёте
	if (GachaSub) { GachaSub->DisarmPostVariant(); }

	// [ИСПРАВЛЕНИЕ] 25.10.2026 строка = заказ: запись взята — следующая уйдёт из Tick; нотис и перезапрос списка — по последней.
	if (bTakeRun)
	{
		++TakeOk;
		// ★ 07.11.2026 `asda.EliteMail.MultiSelect`: окно выбора приза держит одну запись (`PrizeRecord`), и следующий приз затёр бы её.
		//   Поэтому очередь останавливается: невзятое остаётся отмеченным, игрок выбирает вариант и нажимает кнопку ещё раз.
		//   ★ 10.11.2026: вариант, выбранный ДО получения, окна не открывает — очередь ждёт только ответа 6582 (`bAwaitSwap`).
		const bool bStopForChoice = bGachaShown && !bSwapWait && PostMultiSelectOn();
		const bool bRowDone = TakeLeft.Num() == 0;
		if (bRowDone)
		{
			UE_LOG(LogAsdaPost, Log, TEXT("[элитный ящик] заказ %d взят целиком: записей %d"), TakeOrder, TakeOk);
			CheckedOrders.Remove(TakeOrder);
			++TakeRowsOk;
		}
		const bool bMore = TakeLeft.Num() > 0 || TakeQueue.Num() > 0;
		if (bMore && bSwapWait)
		{
			bAwaitSwap = true;
			UE_LOG(LogAsdaPost, Log, TEXT("[элитный ящик] заказ %d: ждём 6582 по выбранному варианту, затем очередь дальше"), TakeOrder);
			return;
		}
		if (bMore && !bStopForChoice)
		{
			bTakeSendNext = true;
			if (!bRowDone) { UE_LOG(LogAsdaPost, Log, TEXT("[элитный ящик] заказ %d: взята запись %d из %d"), TakeOrder, TakeOk, TakeTotal); }
			return;
		}
		if (bStopForChoice && bMore)
		{
			UE_LOG(LogAsdaPost, Log, TEXT("[элитный ящик] забор остановлен окном выбора приза: не взято записей заказа %d, заказов в очереди %d"),
				TakeLeft.Num(), TakeQueue.Num());
		}
		ResetTake();
	}
	// Одиночный забор и последняя запись очереди: пока 6581 в полёте, новое нажатие не начинаем (окно 68500 одно).
	if (bSwapWait && !bTakeRun) { bAwaitSwap = true; }

	// Эталон: если окно гачи не показали — нотис 110671 «Покупка положена в рюкзак».
	if (!bGachaShown && Msg) { Msg->PostLoc(EAsdaMsgCategory::Trade, kLocGranted); }

	// Эталон в конце: `CInventoryUI_ResetAndRefresh` (её делает сам инвентарь по `OnCellChanged`)
	//   и ПОВТОРНЫЙ 11004 — обновить содержимое ящика.
	if (Net && !bRequestInFlight)
	{
		if (Net->SendPremiumPostListReq11004()) { bRequestInFlight = true; }
	}

	UE_LOG(LogAsdaPost, Log,
		TEXT("[элитный ящик] 11007 применён: записей положено %d, очищено %d, вес %d, BuyType %d, окно гачи %s"),
		Applied, Cleared, NewWeight, BuyType, bGachaShown ? TEXT("показано") : TEXT("не показано"));
}

// ★★★ Итог истории с выбором варианта — зовёт подсистема гачи (объявление и разбор в заголовке).
void UAsdaPremiumPostSubsystem::OnVariantChoiceFinished(bool bSwapApplied)
{
	// Обмен состоялся ⇒ 6582 уже положил в ячейку ВЫБРАННЫЙ вариант, и отложенная запись (старый,
	//   «умолчательный» код) в модель попасть не должна. Обмена не было ⇒ у игрока остаётся
	//   вариант по умолчанию — вот теперь он и обязан появиться в сумке.
	FlushDeferredRecord(!bSwapApplied,
		bSwapApplied ? TEXT("обмен варианта состоялся — запись кладёт ответ 6582")
		             : TEXT("выбор отменён или отклонён сервером"));
	// [ИСПРАВЛЕНИЕ] 10.11.2026 «выбор до получения»: 6582 пришёл — очередь отмеченных заказов идёт дальше.
	if (bAwaitSwap)
	{
		bAwaitSwap = false;
		if (bTakeRun) { bTakeSendNext = true; }
	}
}

void UAsdaPremiumPostSubsystem::FlushDeferredRecord(bool bApply, const TCHAR* Reason)
{
	if (!bDeferActive) { return; }
	bDeferActive = false;
	const TArray<uint8> Rec = MoveTemp(DeferredRec);
	DeferredRec.Reset();
	if (!bApply)
	{
		UE_LOG(LogAsdaPost, Log, TEXT("[элитный ящик] отложенная запись приза снята: %s"), Reason);
		return;
	}
	UAsdaInventorySubsystem* Inv = GetGameInstance() ? GetGameInstance()->GetSubsystem<UAsdaInventorySubsystem>() : nullptr;
	const bool bOk = Inv && Inv->ApplyExternalRecord(Rec);
	UE_LOG(LogAsdaPost, Log, TEXT("[элитный ящик] отложенная запись приза %sположена в инвентарь: %s"),
		bOk ? TEXT("") : TEXT("НЕ "), Reason);
}

// ═══════════════════════════════ TICK ══════════════════════════════════════════════════════════
void UAsdaPremiumPostSubsystem::Tick(float DeltaTime)
{
	ASDA_PERF_SCOPE("Tick.PremiumPost");   // ★ R4-b (14.11.2026): область замера (AsdaPerf.h; в Shipping пусто)
	// ★ Вход в мир / смена карты и удержание иконки уведомления — ДО раннего выхода «окно не видно»
	//   ниже: как раз в этом состоянии (окно закрыто) иконка и должна работать.
	TickWorldEntry(DeltaTime);

	// Отложенные пакеты (закон `no-reentrant-net-callbacks`).
	if (bListPending)
	{
		bListPending = false;
		const TArray<uint8> Body = MoveTemp(PendingList);
		PendingList.Reset();
		ApplyListBody(Body);
	}
	if (bApplyPending)
	{
		bApplyPending = false;
		const TArray<uint8> Body = MoveTemp(PendingApply);
		PendingApply.Reset();
		ApplyResultBody(Body);
	}
	// [ИСПРАВЛЕНИЕ] 10.11.2026 «выбор до получения»: окно 68500 закрыли не крестиком (ESC, закрытие сцены гачи,
	//   общий сброс окон) — это та же отмена: заказ остаётся в ящике, очередь не висит.
	if (bPreChoiceWait)
	{
		UAsdaPremiumGachaSubsystem* Gacha = GetGameInstance() ? GetGameInstance()->GetSubsystem<UAsdaPremiumGachaSubsystem>() : nullptr;
		if (!Gacha || !Gacha->IsPreTakeChoiceOpen())
		{
			if (Gacha) { Gacha->CancelPreTakeChoice(); }
			OnPreTakeChoiceDone(false, PreChoiceRec.ItemIndex, -1);
		}
	}
	// [ИСПРАВЛЕНИЕ] 25.10.2026 строка = заказ: следующая запись заказа — отсюда (11007 пришёл в сетевом колбэке).
	if (bTakeSendNext && !bRequestInFlight)
	{
		bTakeSendNext = false;
		if (TakeLeft.Num() > 0)
		{
			const FAsdaPremiumPostRec Next = TakeLeft[0];
			TakeLeft.RemoveAt(0);
			if (!SendTakeRec(TakeOrder, Next)) { ResetTake(); }
		}
		else if (!StartNextTakeRow()) { ResetTake(); }
	}

	if (!IsWindowVisible()) { Mouse.Reset(); TipHoverCell = -1; return; }
	Mouse.Update();
	// ★★★ МОДАЛЬНЫЙ ГЕЙТ (П8-9, волна 9): под открытым модальным нотисом эталон не отдаёт мышь
	//   НИКОМУ ниже (`ui_core.c:104149-104153`). Обоснование, порядок и ключ отката
	//   `asda.UI.ModalGatePolling` — в шапке `AsdaUi::IsModalDispatchBlocked` (`AsdaUiHelpers.h`).
	//   ⚠ Защёлки сбрасываем: иначе нажатие ДО нотиса и отпускание ПОСЛЕ его закрытия сошлись бы
	//   в клик, которого игрок не делал.
	if (AsdaUi::IsModalDispatchBlocked(GetGameInstance()))
	{
		if (TipHoverCell >= 0)   // висящую плашку ячейки ГАСИМ, а не просто «не показываем»
		{
			TipHoverCell = -1;
			if (UAsdaItemTooltipSubsystem* Tip = GetGameInstance() ? GetGameInstance()->GetSubsystem<UAsdaItemTooltipSubsystem>() : nullptr)
			{
				Tip->HideTooltipFor(EAsdaTipOwner::PremiumPost);
			}
		}
		Mouse.Reset();
		return;
	}
	TickTooltips();
}

// Порт событий 8/9/0xA окна (эталон `uiItemGridFindTooltip@0xEF2040`).
//   ★ Разветвление построителя у ящика ТО ЖЕ, что у витрины премиум-гачи
//     (`cItemBar_BuildSlotTooltip@0xB6F3C0`): item_list + (itemType ≤ 4 | 8…9 | categoryFlag == 3)
//     → полный билдер, прочие item_list → простой, sowell_list → билдер самоцвета,
//     special_itemlist → полный с −1; синтетическая запись «бедная» (`CSlot_Init; slot[0] = code`).
//     Поэтому берём ГОТОВУЮ общую точку `ShowGachaSlotTooltip`, а не заводим восьмой построитель
//     (закон `port-shared-points`). Владелец плашки — свой (`PremiumPost`), иначе тик инвентаря
//     гасил бы её через кадр (закон `ui-shared-tip-ownership`).
void UAsdaPremiumPostSubsystem::TickTooltips()
{
	UAsdaItemTooltipSubsystem* Tip = GetGameInstance() ? GetGameInstance()->GetSubsystem<UAsdaItemTooltipSubsystem>() : nullptr;
	if (!Tip) { return; }

	int32 Hit = -1;
	for (int32 Cell = 0; Cell < IconImages.Num() && Hit < 0; ++Cell)
	{
		if (!CellCodes.IsValidIndex(Cell) || CellCodes[Cell] <= 0) { continue; }   // пустая ячейка подсказки не имеет
		if (AsdaUi::IsCursorOverWidget(IconImages[Cell].Get())) { Hit = Cell; }
	}
	if (Hit < 0)
	{
		if (TipHoverCell >= 0) { Tip->HideTooltipFor(EAsdaTipOwner::PremiumPost); TipHoverCell = -1; }
		return;
	}
	if (Hit == TipHoverCell) { return; }   // тот же контрол — эталон событие 8 повторно не шлёт
	TipHoverCell = Hit;
	Tip->ShowGachaSlotTooltip(CellCodes[Hit], EAsdaTipOwner::PremiumPost);
}
