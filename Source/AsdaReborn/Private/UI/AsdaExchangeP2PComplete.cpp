// Copyright (c) 2026 @sitxovski (Discord)
// Прямой обмен между игроками — ИТОГ 5294 / 5295 (волна О4 торговли, 30.09.2026). [ПОРТ]
//   Назначение, границы волны и исправленные дефекты — в шапке `Public/UI/AsdaExchangeP2PSubsystem.h`; итог — `Reports/trade/wave_O4.md`.
//   Эталон: `Recv_Exchange_Complete@0xC85300` (network.c:14587-14864, тип 0), `Recv_Exchange_CompleteFull@0xC86150` (network.c:14878-15060,
//   тип 1), `uiInventoryShowFullNotice@0xC86EF0` (ui_core.c:35513-35527), `quickSlot_ApplyItemUpdate@0xD61610` (ui_core.c:54335),
//   `OnRecv_ItemUpdate@0xD0CC20` (item_inventory.c:39583), `OnRecv_RemoveSlot_byPtr@0xD0CE30` (item_inventory.c:39618),
//   `uiSetHonorPointsDisplay@0xB75D40`, `invGetFreeSlotByMarker@0xD1E910`, `getFieldIfPositive@0xBC58A0`.
//   Сервер: `recvDBCharNormalExchangeComplete@0x4EA750` / `recvDBCharShellExchangeComplelte@0x4E89D0` (успех 5, отказ БД 0 — обоим),
//   `CharNormal/ShellItemExchangeComplete` (0x4E7110 / 0x4E4910: 1/3 — нет места, 2/4 — перевес, 0 — «Same User»; 1/2 и 3/4
//   раздаются по роли стороны, а не по тому, у кого нехватка — см. `CompleteRefusal`).
//
// ★ ПРАВИЛО ПОРТА. Инвентарь не меняется до итога — списки окна были КОПИЯМИ ячеек (О2). Итог применяется ТОЛЬКО по данным сервера и
//   только через общие точки инвентаря (`ApplyExternalRecord`, `ClearCell`, `SetGoldDisplay`, `SetGoldCellAmount`) — второй модели
//   контейнеров здесь нет. Разбор — из `Tick` (очередь `Pending`), не из сетевого колбэка.
#include "UI/AsdaExchangeP2PSubsystem.h"
#include "AsdaWeightWire.h"   // ★ 29.10.2026 (вес ×2): AsdaWeightFromWire — вес персонажа по проводу u16
#include "UI/AsdaInventorySubsystem.h"      // общая точка записей: ApplyExternalRecord / ClearCell / золото
#include "UI/AsdaExchangeSubsystem.h"       // АУКЦИОН 13000 (`g_pItemShopWnd`): отложенное золото 9913 (`sub_92282B` / `sub_92104D`)
#include "UI/AsdaNotice.h"                  // плашка 221, окно-сообщение 1500
#include "Items/AsdaItemTableSubsystem.h"   // `CItemManager_GetItemRecord` (item_list)
#include "Items/AsdaItemDbRows.h"
#include "AsdaNetSubsystem.h"               // вес (`HUD_UpdateMiniWeightBar` → `SetSelfWeight`)
#include "AsdaBytes.h"
#include "Engine/GameInstance.h"

DEFINE_LOG_CATEGORY_STATIC(LogAsdaP2PDone, Log, All);

namespace
{
	// [ОРИГИНАЛ] Цвета строк по обработчику (`CChat_AddTokenizedLines(…, цвет, 10000, 0)`).
	const FColor kColDefault(0xFE, 0xD9, 0xF1, 0xFF);   // −75279 = 0xFFFED9F1 — 5025…5030
	const FColor kColFull(0xFF, 0xAD, 0x00, 0xFF);      // −21248 = 0xFFFFAD00 — 1012 (код 2)
	const FColor kColRed(0xFF, 0x00, 0x00, 0xFF);       // −65536 = 0xFFFF0000 — 9571 (`uiInventoryShowFullNotice`)

	constexpr int32 kGoldCode   = 20551;   // золото
	constexpr int32 kBody5295   = 70;      // `memcpy(payload, Src + 9, 0x46)`
	constexpr int32 kBody5294   = 275;     // `memcpy(payload, src + 9, 0x113)`
	constexpr int32 kEnt5295    = 13;      // запись 5295: код 4, тип 1, слот 2, кол-во 4, +14 2
	constexpr int32 kEnt5294    = 54;      // запись 5294 (полная запись предмета сервера)
	constexpr int32 kEntBase    = 5;       // записи с [5]; [0] код, [1..4] вес

	int32 RCode(const uint8* R)  { return R ? AsdaBytes::RdI32(R) : 0; }       // +0 i32 код
	int32 RType(const uint8* R)  { return R ? (int32)R[4] : 0; }               // +4 u8 контейнер
	int32 RSlot(const uint8* R)  { return R ? AsdaBytes::RdI16(R + 5) : -1; }  // +5 i16 ячейка
	int32 RCount(const uint8* R) { return R ? AsdaBytes::RdI32(R + 9) : 0; }   // +9 i32 кол-во

	// Тело фиксированной длины эталона: `memset(payload, 0, N); memcpy(payload, Src + 9, N)`. Короче — хвост нулями (у эталона там
	//   был бы мусор приёмного буфера) и Warning в лог.
	TArray<uint8> FixedBody(const TArray<uint8>& Body, int32 N, int32 Opcode)
	{
		TArray<uint8> P; P.SetNumZeroed(N);
		FMemory::Memcpy(P.GetData(), Body.GetData(), FMath::Min(N, Body.Num()));
		if (Body.Num() < N)
		{
			UE_LOG(LogAsdaP2PDone, Warning, TEXT("[обмен] %d: тело %d б < %d — хвост дополнен нулями"), Opcode, Body.Num(), N);
		}
		return P;
	}
}

// ═════════════════════════════ ОБЩЕЕ ДЛЯ 5294 / 5295 ════════════════════════════════════════════

bool UAsdaExchangeP2PSubsystem::CompleteRefusal(int32 Code)
{
	// Ветки 0…4 одинаковы у обоих приёмников (network.c:14619-14643 / 14903-14927). Окно закрывает только 0; при 1…4 окно, готовность
	//   и счётчик остаются, а +0x28 уже сброшен вызывающим — 2011 снова шлёт 5293 (сервер отказал ДО запроса в БД, замок P44 не стоит).
	switch (Code)
	{
	case 0:   // 5025 «Обмен не состоялся.» + сброс окна и 9571 (Same User / отказ БД)
		Notice(5025, kColDefault);
		ShowFullNotice();
		return true;
	// ★ Ревизия О4 (МЕЛК-1): сервер раздаёт коды ПО РОЛИ, а не по тому, у кого нехватка: 1/2 — стороне +3611, 3/4 — стороне +3609,
	//   при нехватке места/веса у ЛЮБОГО из двоих; 2 перекрывает 1 (GameServer.c:89867-89880). Клиент (как эталон) лишь печатает текст.
	case 1:   // «Нет места в рюкзаке.» — место (у кого-то из двоих), сторона +3611
		Notice(5026, kColDefault);
		return true;
	case 2:   // 1012 «…рюкзак будет переполнен.» цветом −21248 — вес (у кого-то из двоих), сторона +3611
		Notice(1012, kColFull);
		return true;
	case 3:   // «В рюкзаке игрока нет места.» — место, сторона +3609
		Notice(5028, kColDefault);
		return true;
	case 4:   // «Игрок не может нести такой вес.» — вес, сторона +3609
		Notice(5029, kColDefault);
		return true;
	default:
		return false;
	}
}

void UAsdaExchangeP2PSubsystem::ShowFullNotice()
{
	// `uiInventoryShowFullNotice@0xC86EF0`: `invItemListDialogInit` → `uiInventoryDialogReset` → 9571 красным: плашка 221
	//   (`CControl_SetTooltipText`), строка чата, ONEBUT 1500 (`CItem_BuildTooltipText(g_pItemTooltipUI, 0, −1, …)`, звук окна 25).
	UGameInstance* GI = GetGameInstance();
	InvItemListDialogInit(TEXT("итог: uiInventoryShowFullNotice"));
	InventoryDialogReset(TEXT("итог: uiInventoryShowFullNotice"));
	AsdaNotice::PlankLoc(GI, EAsdaMsgCategory::System, 9571, kColRed, 10000.f, 9571);
	Notice(9571, kColRed);
	AsdaNotice::MsgBoxLoc(GI, EAsdaMsgCategory::Trade, 9571);
}

void UAsdaExchangeP2PSubsystem::ApplyItemRec(const uint8* Rec57, const TCHAR* Why)
{
	// `quickSlot_ApplyItemUpdate` = `CClass_BuildItemRecord(хотбар, 2, rec)` + `OnRecv_ItemUpdate(rec)`: запись целиком в контейнер
	//   по полю +4 — ветки только 1/2/3 (`switch (Src[4])`), прочие эталон молча не применяет. Хотбар — общая точка инвентаря.
	if (!Rec57) { return; }
	const int32 Cont = RType(Rec57);
	if (Cont < 1 || Cont > 3)
	{
		UE_LOG(LogAsdaP2PDone, Verbose, TEXT("[обмен] %s: запись код %d контейнер %d — у `OnRecv_ItemUpdate` ветки нет, пропуск"), Why, RCode(Rec57), Cont);
		return;
	}
	UAsdaInventorySubsystem* Inv = GetGameInstance() ? GetGameInstance()->GetSubsystem<UAsdaInventorySubsystem>() : nullptr;
	if (!Inv) { return; }
	TArray<uint8> R; R.Append(Rec57, kRec);
	// ★ Ревизия О4 (МЕЛК-3): режим AsIs — как `OnRecv_ItemUpdate`, запись пишется как есть (у Default кол-во −1 чистило бы ячейку —
	//   это контракт 6166/6582, у остальных вызывающих он сохранён).
	Inv->ApplyExternalRecord(R, UAsdaInventorySubsystem::EExternalRecordMode::AsIs);
	UE_LOG(LogAsdaP2PDone, Log, TEXT("[обмен] %s: запись → контейнер %d ячейка %d код %d кол-во %d"), Why, Cont, RSlot(Rec57), RCode(Rec57), RCount(Rec57));
}

void UAsdaExchangeP2PSubsystem::RemoveInvSlot(const uint8* Rec57, const TCHAR* Why)
{
	// `OnRecv_RemoveSlot_byPtr(g_pInventoryWnd, {слот +5, …, контейнер +4})`: контейнер 1 / 2 — `BuildItemRecord(1)` + memset 57 б +
	//   перерисовка; прочие — ничего.
	if (!Rec57) { return; }
	const int32 Cont = RType(Rec57);
	if (Cont != 1 && Cont != 2) { return; }
	if (UAsdaInventorySubsystem* Inv = GetGameInstance() ? GetGameInstance()->GetSubsystem<UAsdaInventorySubsystem>() : nullptr)
	{
		Inv->ClearCell(Cont, RSlot(Rec57));
		UE_LOG(LogAsdaP2PDone, Log, TEXT("[обмен] %s: отдано — контейнер %d ячейка %d (код %d) очищена"), Why, Cont, RSlot(Rec57), RCode(Rec57));
	}
}

void UAsdaExchangeP2PSubsystem::FinishComplete(int32 Weight, const TCHAR* Why)
{
	// Хвост ветки 5: `HUD_UpdateMiniWeightBar(g_pHUD, [1..4])`, `invItemListDialogInit`, `uiInventoryDialogReset` (окно скрыто, +0x2C = 0,
	//   вывод отложенного золота, если ещё есть), чат 5030 «Обмен совершен успешно.».
	if (UAsdaNetSubsystem* Net = GetGameInstance() ? GetGameInstance()->GetSubsystem<UAsdaNetSubsystem>() : nullptr)
	{
		Net->SetSelfWeight(AsdaWeightFromWire(Weight), -1, Why);   // ★ 29.10.2026 (вес ×2): i32 5294/5295 +1 — младшие 16 бит u16
	}
	InvItemListDialogInit(Why);
	InventoryDialogReset(Why);
	Notice(5030, kColDefault);
}

// ═════════════════════════════ 5295 — ОБМЕН ОБЫЧНЫМИ ПРЕДМЕТАМИ (ТИП 0) ════════════════════════════

void UAsdaExchangeP2PSubsystem::Recv5295(const TArray<uint8>& Body)
{
	const TArray<uint8> P = FixedBody(Body, kBody5295, 5295);
	bCompleteSent = 0;   // `*(_DWORD *)self->gap_28 = 0` — до switch, при ЛЮБОМ коде
	const int32 Code = P[0];
	UE_LOG(LogAsdaP2PDone, Log, TEXT("[обмен] 5295: код %d, вес %d (окно #%d, тип %d)"), Code, AsdaBytes::RdI32(P.GetData() + 1), WindowIndex, ExchangeType);
	if (Code != 5)
	{
		if (!CompleteRefusal(Code)) { UE_LOG(LogAsdaP2PDone, Log, TEXT("[обмен] 5295: код %d — ветки у эталона нет (default: return)"), Code); }
		return;
	}
	UGameInstance* GI = GetGameInstance();
	UAsdaInventorySubsystem* Inv = GI ? GI->GetSubsystem<UAsdaInventorySubsystem>() : nullptr;
	UAsdaExchangeSubsystem* Au = GI ? GI->GetSubsystem<UAsdaExchangeSubsystem>() : nullptr;
	const UAsdaItemTableSubsystem* IT = GI ? GI->GetSubsystem<UAsdaItemTableSubsystem>() : nullptr;
	if (!Inv) { UE_LOG(LogAsdaP2PDone, Error, TEXT("[обмен] 5295: инвентаря нет — итог не применён")); return; }
	// Поля записи i пакета (с [5], шаг 13): код @0, тип @4, слот @5, кол-во @7, «+14» @11.
	auto ECode = [&P](int32 i) { return AsdaBytes::RdI32(P.GetData() + kEntBase + kEnt5295 * i + 0); };
	auto EType = [&P](int32 i) { return (int32)P[kEntBase + kEnt5295 * i + 4]; };
	auto ESlot = [&P](int32 i) { return (int32)AsdaBytes::RdI16(P.GetData() + kEntBase + kEnt5295 * i + 5); };
	auto ECount = [&P](int32 i) { return AsdaBytes::RdI32(P.GetData() + kEntBase + kEnt5295 * i + 7); };
	auto EW14 = [&P](int32 i) { return (int32)AsdaBytes::RdI16(P.GetData() + kEntBase + kEnt5295 * i + 11); };
	Inv->BeginCellBatch();   // хотбар (`CClass_BuildItemRecord` 1/2 по каждой записи) — одна пересборка в конце

	// ── 1. Отданное, которого нет среди полученного и которое не осталось стопкой в списке, — снять (network.c:14647-14677) ──
	for (int32 i = 0; i < kTray; ++i)
	{
		const uint8* T = TrayRec(i);
		const uint8* Cp = CopyRec(i);
		const int32 SrcIdx = Cp ? AsdaBytes::RdI32(Cp + 60) : -1;   // `gap_46[64·i + 350]` — индекс-источник копии лотка
		// [ПОРТ] индекс вне 0…59 (у эталона — чтение за границей списка) даёт «записи нет».
		const uint8* L = ListRec(0, SrcIdx);
		bool bNew = true;
		for (int32 j = 0; j < kTray; ++j)
		{
			if (RType(T) == EType(j) && RCode(T) == ECode(j) && RSlot(T) == ESlot(j)) { bNew = false; }
			if (L && IT && IT->GetItem(RCode(L)) && RType(T) == RType(L) && RCode(T) == RCode(L) && RSlot(T) == RSlot(L) && RCount(L) > 0) { bNew = false; }
		}
		if (bNew) { RemoveInvSlot(T, TEXT("5295")); }
	}

	// ── 2. Список-источник (остатки после выкладки) — в инвентарь; золото — через отложенную сумму аукциона (network.c:14678-14748) ──
	for (int32 i = 0; i < kList0Max; ++i)
	{
		uint8* L = ListRec(0, i);
		if (RCode(L) <= 0) { continue; }
		if (RCode(L) != kGoldCode) { ApplyItemRec(L, TEXT("5295 список")); continue; }
		const int32 Deferred = Au ? Au->GetDeferredSaleGold() : 0;   // `sub_92282B(g_pItemShopWnd)`
		if (Deferred <= 0)
		{
			Inv->SetGoldDisplay(RCount(L));   // `uiSetHonorPointsDisplay(g_pGameClient, список.кол-во)`
			ApplyItemRec(L, TEXT("5295 список (золото)"));
			continue;
		}
		int32 GoldValue = Deferred;
		if (GoldValue == RCount(L))
		{
			Inv->SetGoldDisplay(GoldValue);
			if (Au) { Au->ResetDeferredSaleGold(TEXT("итог обмена 5295: отложенное = запас списка")); }
			continue;
		}
		// Отложенная сумма (новый запас после продажи на аукционе) минус золото, отданное в лотке; запись в инвентарь эталон тут
		//   НЕ кладёт — только сумму денег и кол-во записи списка (к нему прибавится полученное золото в шаге 3).
		int32 GoldGiven = 0; bool bGoldInTray = false;
		for (int32 k = 0; k < kTray; ++k)
		{
			if (RCode(TrayRec(k)) == kGoldCode) { bGoldInTray = true; GoldGiven = RCount(TrayRec(k)); }
		}
		if (bGoldInTray) { GoldValue -= GoldGiven; }
		Inv->SetGoldDisplay(GoldValue);
		if (Au) { Au->ResetDeferredSaleGold(TEXT("итог обмена 5295: отложенное − отданное")); }
		AsdaBytes::WrI32(L + 9, GoldValue);
		UE_LOG(LogAsdaP2PDone, Log, TEXT("[обмен] 5295: золото — отложено %d, отдано %d → %d"), Deferred, GoldGiven, GoldValue);
	}

	// ── 3. Полученное: стопка того же кода/контейнера/ячейки в списке — кол-во складывается, иначе запись как пришла (:14749-14820) ──
	MyTray.Init(0, kTray * kRec);   // `memset(aLocalItemTemplate, 0, …)`
	for (int32 i = 0; i < kTray; ++i)
	{
		uint8* T = TrayRec(i);
		bool bMatched = false;
		auto FillTray = [&](int32 Qty)
		{
			T[4] = (uint8)EType(i);
			AsdaBytes::WrI16(T + 5, ESlot(i));
			AsdaBytes::WrI32(T + 0, ECode(i));
			AsdaBytes::WrI32(T + 9, Qty);
			AsdaBytes::WrI16(T + 14, EW14(i));
			ApplyItemRec(T, TEXT("5295 получено"));
			if (RCode(T) == kGoldCode) { Inv->SetGoldDisplay(RCount(T)); }
		};
		for (int32 j = 0; j < kList0Max; ++j)
		{
			const uint8* L = ListRec(0, j);
			if (RCode(L) == ECode(i) && RType(L) == EType(i) && RSlot(L) == ESlot(i))
			{
				FillTray(ECount(i) + RCount(L));
				bMatched = true;
			}
		}
		if (!bMatched) { FillTray(ECount(i)); }
	}
	// ── 4. Копия контейнера 2 (+0x1958) и `CClass_BuildItemRecord(1/2)` по копиям лотка (:14821-14851) — это пересборка хотбара по
	//   ссылкам «контейнер + ячейка»; у порта её делает общая точка `EndCellBatch` → `RefreshHotbarCache`. Саму копию эталон стирает
	//   `invItemListDialogInit` строкой ниже — вне обработчика она не живёт, заводить её в модели незачем.
	Inv->EndCellBatch();
	FinishComplete(AsdaBytes::RdI32(P.GetData() + 1), TEXT("итог обмена 5295"));
}

// ═════════════════════════════ 5294 — ОБМЕН ЛИЧНЫМИ ВЕЩАМИ (ТИП 1) ═════════════════════════════════

void UAsdaExchangeP2PSubsystem::Recv5294(const TArray<uint8>& Body)
{
	const TArray<uint8> P = FixedBody(Body, kBody5294, 5294);
	bCompleteSent = 0;   // `*(_DWORD *)this_2->gap_28 = 0` — до switch
	const int32 Code = P[0];
	UE_LOG(LogAsdaP2PDone, Log, TEXT("[обмен] 5294: код %d, вес %d (окно #%d, тип %d)"), Code, AsdaBytes::RdI32(P.GetData() + 1), WindowIndex, ExchangeType);
	if (Code != 5)
	{
		if (!CompleteRefusal(Code)) { UE_LOG(LogAsdaP2PDone, Log, TEXT("[обмен] 5294: код %d — ветки у эталона нет (default: return)"), Code); }
		return;
	}
	UGameInstance* GI = GetGameInstance();
	UAsdaInventorySubsystem* Inv = GI ? GI->GetSubsystem<UAsdaInventorySubsystem>() : nullptr;
	UAsdaExchangeSubsystem* Au = GI ? GI->GetSubsystem<UAsdaExchangeSubsystem>() : nullptr;
	if (!Inv) { UE_LOG(LogAsdaP2PDone, Error, TEXT("[обмен] 5294: инвентаря нет — итог не применён")); return; }
	Inv->BeginCellBatch();

	// ── 1. Отданное (network.c:14931-14975): золото — пересчёт суммы (с отложенной суммой аукциона), вещь — снять ячейку ──
	for (int32 s = 0; s < kTray; ++s)
	{
		const uint8* T = TrayRec(s);
		if (RCode(T) <= 0) { continue; }
		if (RCode(T) != kGoldCode)
		{
			RemoveInvSlot(T, TEXT("5294"));   // + `CClass_BuildItemRecord(1)` — хотбар в `EndCellBatch`
			continue;
		}
		int32 GoldDelta = FMath::Max(0, Inv->GetGold());   // `getFieldIfPositive(g_pGameClient)`
		const int32 Deferred = Au ? Au->GetDeferredSaleGold() : 0;
		if (Deferred <= 0) { GoldDelta -= RCount(T); }
		else
		{
			GoldDelta = Deferred - RCount(T);
			if (Au) { Au->ResetDeferredSaleGold(TEXT("итог обмена 5294: отдано золото")); }
		}
		// `invGetFreeSlotByMarker(goldDelta ≤ 0 ? 1 : goldDelta)` (sprintf-строка эталоном не используется), затем
		//   `uiSetHonorPointsDisplay(goldDelta)`. [ПОРТ] отрицательную сумму общая точка не ставит (сервер отдачу всего золота не
		//   пропускает: клиент 5039, сервер `v55 < 0 → 1`).
		Inv->SetGoldCellAmount(GoldDelta <= 0 ? 1 : GoldDelta);
		Inv->SetGoldDisplay(GoldDelta);
		UE_LOG(LogAsdaP2PDone, Log, TEXT("[обмен] 5294: отдано золото %d (отложено %d) → сумма %d"), RCount(T), Deferred, GoldDelta);
	}

	// ── 2. Полученное (network.c:14976-15044): 54-байтная запись сервера → запись 57 б в лоток; золото — `quickSlot_ApplyItemUpdate`
	//   + сумма, вещь — `UI_OnInventoryRemove` (МИСНЕЙМ: `Inventory_SetBagSlot` кладёт запись в СУМКУ по слоту +5) ──
	MyTray.Init(0, kTray * kRec);
	for (int32 s = 0; s < kTray; ++s)
	{
		const uint8* E = P.GetData() + kEntBase + kEnt5294 * s;
		const int32 ECodeV = AsdaBytes::RdI32(E + 0);
		if (ECodeV <= 0) { continue; }
		uint8* T = TrayRec(s);
		T[4] = E[4];                                                   // тип
		AsdaBytes::WrI16(T + 5, AsdaBytes::RdI16(E + 5));              // слот
		AsdaBytes::WrI32(T + 0, ECodeV);                               // код
		AsdaBytes::WrI16(T + 14, AsdaBytes::RdI16(E + 8));             // +14 (вес стопки)
		T[13] = E[7];                                                  // +13 прочность
		T[24] = E[10];                                                 // +24 заточка
		T[25] = E[12];                                                 // +25
		T[26] = E[11];                                                 // +26
		AsdaBytes::WrI16(T + 27, AsdaBytes::RdI16(E + 13));            // +27 «+N%»
		for (int32 i = 0; i < 5; ++i) { AsdaBytes::WrI32(T + 29 + 4 * i, AsdaBytes::RdI32(E + 21 + 4 * i)); }   // +29 5 опций
		AsdaBytes::WrI32(T + 9, AsdaBytes::RdI32(E + 42));             // +9 кол-во
		FMemory::Memcpy(T + 16, E + 46, 8);                            // +16 4 самоцвета
		if (ECodeV == kGoldCode)
		{
			ApplyItemRec(T, TEXT("5294 получено (золото)"));
			Inv->SetGoldDisplay(RCount(T));   // сервер прислал уже итоговую сумму (своё + полученное)
			continue;
		}
		TArray<uint8> R; R.Append(T, kRec);
		if (R[4] != 1)
		{
			// [ПОРТ] модель порта ведёт запись по полю +4; `Inventory_SetBagSlot` эталона кладёт в сумку независимо от него.
			UE_LOG(LogAsdaP2PDone, Warning, TEXT("[обмен] 5294: запись код %d с контейнером %d — эталон кладёт в сумку (1), так и делаем"), ECodeV, (int32)R[4]);
			R[4] = 1;
		}
		ApplyItemRec(R.GetData(), TEXT("5294 получено"));
	}
	Inv->EndCellBatch();
	FinishComplete(AsdaBytes::RdI32(P.GetData() + 1), TEXT("итог обмена 5294"));
}
