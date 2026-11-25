// Copyright (c) 2026 @sitxovski (Discord)
// AsdaExchangeSubsystem_Recv.cpp — ★ R2 10.11.2026 (разрез AsdaExchangeSubsystem.cpp, дословный перенос): приём пакетов 99xx и нотисы.
//   Общее для частей — в AsdaExchangeSubsystemInternal.h; шапка и реверс — в AsdaExchangeSubsystem.cpp.
#include "UI/AsdaExchangeSubsystem.h"
#include "AsdaDevConsole.h"   // ★ античит А2: ASDA_DEV_CVAR / ASDA_DEV_CONSOLE — дев-ключи и команды вне Shipping
#include "AsdaSelf.h"   // ★ волна 1: «это я?» / мой номер / мой уровень — общая точка ядра (E2, E4)
#include "UI/AsdaNotice.h"   // ★ волна 1: фасад нотисов (Loc / Chat / MsgBox)
#include "UI/AsdaTradeStateSubsystem.h"   // ★ 29.09.2026: окно обмена 2008 открыто — гейт режимов и отложенное золото 9913

#include "AsdaNetSubsystem.h"
#include "AsdaBytes.h"
#include "AsdaPacketWriter.h"   // ★ U6 (В1 дедупа №2): записи 9901/9905/9909 через общий писатель
#include "Items/AsdaItemTableSubsystem.h"
// GetCurrentNpcId — NPC-аукционщик в шапке 99xx (+10)   ★ R3: комментарий снятого включения "Npc/AsdaNpcDialogSubsystem.h" (файл им не пользовался) — сохранён
#include "UI/AsdaIconCellGrid.h"
#include "UI/AsdaInventorySubsystem.h"
#include "UI/AsdaJuiComboBox.h"
#include "Text/AsdaTextSubsystem.h"   // имя предмета и подстановка аргументов нотиса
#include "UI/AsdaItemTooltipSubsystem.h"   // общая точка подсказки предмета
#include "UI/AsdaMessageSubsystem.h"
#include "UI/AsdaUiHelpers.h"
#include "UI/AsdaCommunityFriendSubsystem.h"   // ★ общая точка AsdaCommunityUi::ShownVisibilityFor
#include "UI/AsdaWindowSubsystem.h"
#include "UI/AsdaWarehouseSubsystem.h"   // ★ пачка 20, жалоба 10: склад 4000 открыт — рынок не открывается
#include "World/AsdaWorldSubsystem.h"      // GetSelfCharName — имя игрока в записи 9901

#include "Blueprint/UserWidget.h"
#include "Blueprint/WidgetTree.h"
#include "Components/Button.h"
#include "Components/CanvasPanel.h"
#include "Components/CanvasPanelSlot.h"
#include "Components/CheckBox.h"
#include "Components/ComboBoxString.h"
#include "Components/EditableText.h"
#include "Components/Image.h"
#include "Components/TextBlock.h"
#include "Engine/GameInstance.h"
#include "TimerManager.h"
#include "Engine/World.h"
#include "HAL/IConsoleManager.h"
#include "AsdaOutgoingGate.h"               // ★ С-Р1: общий офлайн-гейт отладочных команд (RefuseDebugWhenOnline)
#include "Test/AsdaSelfTestKit.h"   // ★ 11.10.2026 (В3 S1): общий каркас самотестов (аргументы, отказ, перехват, итог, выход)
#include "Login/AsdaLoginData.h"   // [ПОРТ] 08.10.2026: общий кодек провода (гибрид RU+EN+AR)
#include "AsdaExchangeSubsystemInternal.h"   // ★ R2 10.11.2026: общее для частей AsdaExchangeSubsystem*.cpp

// ═══════════════════ ★★★ ПАЧКА 20 (09.11.2026): ДЕСЯТЬ ЖАЛОБ ВЛАДЕЛЬЦА НА РЫНОК ═══════════════════════════════
//   Разбор: `Reports/npc_exchange_win_13000.md` §18. Пять ключей, у каждого 0 = поведение до пачки 20.
// [ПОРТ] Жалобы 1, 2, 3, 8: приёмники 9908/9910 и 9904/9906 различаются по `a4` эталонного диспетчера
//   (`npcShop_DispatchResult@0xD43270`, `npc_mob_pet.c:511`), а не по кратности длины. Порт по длине принимал ПУСТОЙ 9908
//   (ItemRelay шлёт 0 записей, когда в категории лотов нет) за ответ 9910 и снова слал 9907 — бесконечный шторм
//   (живой журнал 08.11: 3215 пар 9907/9908 за сессию), окно «мои лоты» пустым не открывалось, список при смене вкладки
//   13260 не очищался, ItemRelay и БД получали `EXEC UPCharExchangeItem` сотнями в минуту. Пустая страница 9904 точно так же
//   уходила в ветку покупки и оставляла на экране прежние лоты.
static ASDA_DEV_CVAR(int32, CVarExchangeRouteBySub,
	TEXT("asda.Exchange.RouteBySub"), 1,
	TEXT("Аукцион: 1 — 9908/9910 и 9904/9906 различаются по a4 диспетчера (пустой список открывает окно и не шлёт 9907 снова); 0 — по длине тела, как до 09.11."),
	ECVF_Default);

// [ОРИГИНАЛ] Жалобы 4, 7, 9: записи 9902 (выставлено), 9910 (забрано) и 9906 (куплено) меняют СУМКУ клиента
//   (`invSlotUpdateOrRemove@0xD43CB0` из `netOnNpcSellResult`, `exchangeParseItemPacket`, `netParseItemAcquireResult`) и золото
//   (`uiSetHonorPointsDisplay`). Порт этого не делал: выставленная вещь оставалась в сумке, забранная — не появлялась
//   в ячейке, куда её положил сервер, и следующее выставление/перенос шли по чужой ячейке.
static ASDA_DEV_CVAR(int32, CVarExchangeApplyInventory,
	TEXT("asda.Exchange.ApplyInventory"), 1,
	TEXT("Аукцион: 1 — записи 9902/9910/9906 применяются к сумке и золоту, как в оригинале; 0 — только нотисы, как до 09.11."),
	ECVF_Default);

namespace {
	// Размеры записей S→C (§7.3 отчёта).
	constexpr int32 kRec9902 = 67;

	constexpr int32 kRec9906 = 85;
	constexpr int32 kRec9908 = 62;
	constexpr int32 kRec9910 = 61;

	constexpr int32 kLocMyListOk      = 12689;
	constexpr int32 kLocRegDoneChat   = 20173;
	constexpr int32 kLocRegDoneModal  = 12686;
	constexpr int32 kLocBuyDoneChat   = 20065;
	constexpr int32 kLocBuyDoneModal  = 12776;
	constexpr int32 kLocTakeDoneChat  = 20170;
	constexpr int32 kLocTakeDoneModal = 12542;
	constexpr int32 kLocSoldPrice     = 20171;  // «твой лот куплен» — цена
	constexpr int32 kLocSoldTax       = 20172;  // «твой лот куплен» — налог

	// Код результата → нотис. Таблицы §7.5 отчёта, один в один с эталоном.
	int32 Loc9902(int32 Code)
	{
		switch (Code)
		{
			case 1:   return 12677; case 2:   return 12678; case 3:   return 12679;
			case 4:   return 12680; case 5:   return 12681; case 6:   return 12682;
			case 7:   return 12547; case 8:   return 12683; case 9:   return 12672;
			case 10:  return 12684; case 11:  return 12685; case 12:  return 12721;
			case 13:  return 12777; case 14:  return 97101; case 94:  return 12719;
			case 95:  return 12720; case 96:  return 12707; case 97:  return 12712;
			case 98:  return 12713; case 99:  return 12714; case 100: return 12711;
			default:  return 0;
		}
	}
	int32 Loc9904(int32 Code)
	{
		switch (Code)
		{
			case 1:  return 12671; case 2:  return 12672; case 94: return 12723;
			case 95: return 12724; case 96: return 12725; case 97: return 12712;
			// ★ ДЕФЕКТ D7 оригинала: код 98 не обработан — сохраняем поведение (нотиса нет).
			case 99: return 12713;
			default: return 0;
		}
	}
	int32 Loc9906(int32 Code)
	{
		switch (Code)
		{
			case 1:  return 12671; case 2:  return 12672; case 3:  return 12547;
			case 4:  return 12673; case 5:  return 12674; case 6:  return 12675;
			case 7:  return 12676; case 94: case 95: case 97: case 98: return 12726;
			case 96: return 12707; case 99: return 12710;
			default: return 0;
		}
	}
	int32 Loc9908(int32 Code)
	{
		// Только два обработанных кода — остальные эталон игнорирует.
		if (Code == 1)   { return 12695; }
		if (Code == 100) { return 12711; }
		return 0;
	}
	int32 Loc9910(int32 Code)
	{
		switch (Code)
		{
			case 1:  return 12695; case 2:  return 12672; case 3:  return 12674;
			case 4:  return 12675; case 5:  return 12549;
			case 94: case 95: case 97: case 98: case 99: return 12726;
			case 96: return 12707;
			default: return 0;
		}
	}
}

// ═══════════════════════ приём ════════════════════════════════════════════════════════════════
void UAsdaExchangeSubsystem::HandleDispatch(int32 Kind, int32 Sub, const TArray<uint8>& Body)
{
	// Порт `npcShop_DispatchResult@0xD43270`.
	switch (Kind)
	{
		case 1:
			// 9902 — результат выставления. При успехе эталон закрывает панель (тип 1).
			HandleRegisterResult(Body);
			break;

		case 3:
			// 9908 (Sub == 0) — мои лоты; 9910 (Sub == 1) — результат снятия.
			HandleListingPacket(Body, Sub);
			break;

		case 4:
			// ★ ДЕФЕКТ D6 эталона: вне режимов 4/5 пакет молча выбрасывается. Сохраняем поведение.
			if (Mode != EAsdaExchangeMode::BrowseA && Mode != EAsdaExchangeMode::BrowseB)
			{
				UE_LOG(LogAsdaExchange, Verbose,
					TEXT("аукцион: пакет поиска пришёл вне режимов 4/5 — отброшен (дефект оригинала D6)"));
				return;
			}
			HandleTradeResult(Body, Sub);
			break;

		case 6:
		{
			// 9913 — «твой лот куплен». Смещения ОТ НАЧАЛА ПАКЕТА, тело начинается с +9,
			//   поэтому внутри тела: цена = +8, золото = +39, налог = +44.
			const int32 Gold = I32(Body, 39);
			if (Gold <= 0) { return; }             // ★ гейт эталона
			const int32 Price = I32(Body, 8);
			const int32 Tax   = I32(Body, 44);
			// ★ 20171 «Товар [ss1] продан за [ss2] золота» и 20172 «Выплачен налог на торговлю:
			//   [ss1] золота» — аргументы кладёт эталон `ui_core.c:50765-50775`:
			//   у 20171 [ss1] = имя предмета (itemId в теле на +4), [ss2] = цена;
			//   у 20172 [ss1] = налог.
			NoticeChatBFmt(kLocSoldPrice,
				{ ItemNameByCode(I32(Body, 4)), AsdaUi::FormatThousands(Price) });
			NoticeChatBFmt(kLocSoldTax, { AsdaUi::FormatThousands(Tax) });
			UE_LOG(LogAsdaExchange, Log,
				TEXT("аукцион: лот продан — цена %d, налог %d, золото %d"), Price, Tax, Gold);
			// ★ 29.09.2026 (волна 0 торговли): [ОРИГИНАЛ] ui_core.c:50738-50746 — при ОТКРЫТОМ окне обмена 2008
			//   (`g_bPartyInviteFlag` = `CExchange+0x2C`) золото откладывается (`sub_92104D(win, v)`), иначе применяется и
			//   отложенное обнуляется (`sub_92104D(win, 0)`). Нотисы 20171/20172 эталон печатает в обоих случаях (ниже по
			//   функции) — порядок печати у порта выше, на исход не влияет. Общая точка `UAsdaTradeStateSubsystem`.
			if (UAsdaTradeStateSubsystem::IsExchangeWindowOpen(GetGameInstance()))
			{
				DeferredSaleGold = Gold;
				UE_LOG(LogAsdaExchange, Log, TEXT("аукцион: окно обмена 2008 открыто — золото %d отложено до закрытия обмена (ui_core.c:50740)"), Gold);
				break;
			}
			DeferredSaleGold = 0;
			// ★ 30.09.2026 (волна О4 торговли): `uiSetHonorPointsDisplay(g_pGameClient, золото)` (ui_core.c:50744) — поле денег инвентаря.
			if (UAsdaInventorySubsystem* Inv = GetGameInstance() ? GetGameInstance()->GetSubsystem<UAsdaInventorySubsystem>() : nullptr) { Inv->SetGoldDisplay(Gold); }
			if (TxtExchGold) { TxtExchGold->SetText(FText::FromString(AsdaUi::FormatThousands(Gold))); }
			break;
		}

		case 7:
		{
			// 9914 — «аукцион заблокирован». Сверяем userIdx: не совпал ⇒ пакет не наш.
			const int32 Uidx = I32(Body, 2);
			UGameInstance* GI = GetGameInstance();
			const UAsdaNetSubsystem* Net = GI ? GI->GetSubsystem<UAsdaNetSubsystem>() : nullptr;
			if (Net && Net->IsSelfUserNumKnown() && !AsdaSelf::IsSelfUserIdx(Net, Uidx)) { return; }
			bAuctionBlocked = true;               // после этого пускается ТОЛЬКО режим 3
			NoticeChat(kLocBusyOther);
			break;
		}

		default:
			break;
	}
	(void)Sub;
}

void UAsdaExchangeSubsystem::HandleRegisterResult(const TArray<uint8>& Body)
{
	// До 5 записей по 67 байт. Код результата — байт +0 записи.
	bool bAllOk = Body.Num() >= kRec9902;
	int32 Gold = 0;
	int32 OkCount = 0;     // `soldCount` эталона — идёт в [sd1] нотиса 20173
	int32 RegFee  = 0;     // `totalGoldDelta` эталона — идёт в [ss2]
	for (int32 Off = 0; Off + kRec9902 <= Body.Num(); Off += kRec9902)
	{
		const int32 Code = U8(Body, Off + 0);
		if (Code != 0)
		{
			bAllOk = false;
			if (const int32 Loc = Loc9902(Code)) { NoticeChat(Loc); NoticeModal(Loc); }
			continue;
		}
		Gold = I32(Body, Off + 55);   // золото игрока после списания комиссии
		++OkCount;                    // `++soldCount` эталона (`network.c:20780`)
		RegFee += I32(Body, Off + 51);// `totalGoldDelta += *(_DWORD *)&v81[67*i + 51]` (`:20781`)

		// ★★★ ПАЧКА 20, жалоба 7 — [ОРИГИНАЛ] `netOnNpcSellResult@0xD50660` (`network.c:20640-20730`): успешная запись
		//   ЗАБИРАЕТ вещь из сумки клиента (`invSlotUpdateOrRemove`, контейнер +5, слот +6). Остаток = (+24) − (+20), не меньше 0:
		//   у стопки контейнера 2 с `tradableFlag` и остатком > 0 ячейке ставится остаток, иначе ячейка очищается.
		//   Раскладка записи предмета (57 б) из записи 9902: код +1, контейнер +5, слот +6, прочность +8 → 13,
		//   +9 → 24, +11 → 25, +10 → 26, +12 i16 → 27, вес +28 → 14, пять опций +30… → 29…, срок +59…+66 → 16…23.
		if (CVarExchangeApplyInventory.GetValueOnGameThread() != 0)
		{
			TArray<uint8> Rec; Rec.SetNumZeroed(57);
			AsdaBytes::WrI32(Rec.GetData() + 0, I32(Body, Off + 1));
			Rec[4] = (uint8)U8(Body, Off + 5);
			AsdaBytes::WrI16(Rec.GetData() + 5, (int16)AsdaBytes::SafeI16(Body, Off + 6));
			const int32 Remain = FMath::Max(0, I32(Body, Off + 24) - I32(Body, Off + 20));
			AsdaBytes::WrI32(Rec.GetData() + 9, Remain);
			ApplyLotToInventory(0, Rec, Remain);
		}
	}

	if (bAllOk)
	{
		// ★ Аргументы 20173 «Всего выставлено предметов: [sd1]. Общая цена: [ss1]. Стоимость
		//   регистрации: [ss2].» — ровно как их кладёт эталон (`network.c:21071-21077`):
		//     [sd1] = soldCount (число успешных записей);
		//     [ss1] = String_FormatThousands(totalSellValue) — сумма ЦЕН строк лота
		//             (`pInvWnd[20*row + 330]`, то есть наши `RegRows[i].Price`);
		//     [ss2] = String_FormatThousands(totalGoldDelta) — сумма поля +51 записей 9902
		//             («стоимость регистрации», §7.4).
		int64 SellSum = 0;
		for (const FAsdaExchangeRegRow& R : RegRows) { if (!R.IsEmpty()) { SellSum += R.Price; } }
		NoticeChatFmt(kLocRegDoneChat,
			{ AsdaUi::FormatThousands((int32)FMath::Clamp<int64>(SellSum, 0, MAX_int32)),
			  AsdaUi::FormatThousands(RegFee) },
			{ OkCount });
		NoticeModal(kLocRegDoneModal);
		// Успех ⇒ эталон закрывает панель выставления (`itemMediationCancelByType(this, 1)`).
		for (FAsdaExchangeRegRow& R : RegRows) { R.Clear(); }
		CancelByType(1);
	}
	if (Gold > 0 && TxtMoney)
	{
		TxtMoney->SetText(FText::FromString(AsdaUi::FormatThousands(Gold)));
	}
	// ★ ПАЧКА 20 — [ОРИГИНАЛ] `network.c:20730`: `if (+55 > 0) uiSetHonorPointsDisplay(g_pGameClient, +55)` — это поле денег
	//   ИНВЕНТАРЯ (общая точка `SetGoldDisplay`), а не только надпись окна: комиссия списана, рюкзак обязан это показать.
	if (Gold > 0 && CVarExchangeApplyInventory.GetValueOnGameThread() != 0)
	{
		if (UAsdaInventorySubsystem* Inv = GetGameInstance() ? GetGameInstance()->GetSubsystem<UAsdaInventorySubsystem>() : nullptr)
		{
			Inv->SetGoldDisplay(Gold);
		}
	}
}

// ★★★ ПАЧКА 20 (жалобы 4, 7, 9): ЗАПИСЬ ЛОТА → ЯЧЕЙКА СУМКИ. Порт `invSlotUpdateOrRemove@0xD43CB0` (`item_inventory.c:46880`)
//   в тех вариантах, какими его зовут три приёмника аукциона (ячейку ищет по коду + контейнеру + слоту записи):
//     Kind 0 — выставление 9902 (`netOnNpcSellResult`): контейнер 2, `item_list`, `tradableFlag` и остаток > 0 ⇒ ячейке
//              ставится остаток (режим 1); иначе ячейка очищается (режим 0);
//     Kind 1 — забор 9910 (`exchangeParseItemPacket`) и Kind 2/3 — покупка 9906 (`netParseItemAcquireResult`, режимы окна 4/5):
//              контейнер 2 и в ТОЙ ЖЕ ячейке уже лежит стопка того же кода (`invFindEquippedSlotItemCode` > 0) ⇒ количество
//              складывается (`tradableFlag <= 0` ⇒ 1, `flagWord176` ⇒ количество записи), режим 1; иначе запись кладётся
//              в слот целиком (режим 2). ★ Покупка режима 5 (Kind 3) стек не ищет — сразу режим 2 (`updateMode == 3`).
//   Запись меняет модель инвентаря ЧЕРЕЗ его общую точку `ApplyExternalRecord` (писать в контейнеры может только он).
void UAsdaExchangeSubsystem::ApplyLotToInventory(int32 Kind, const TArray<uint8>& Rec57, int32 RemainAmount)
{
	UGameInstance* GI = GetGameInstance();
	UAsdaInventorySubsystem* Inv = GI ? GI->GetSubsystem<UAsdaInventorySubsystem>() : nullptr;
	const UAsdaItemTableSubsystem* IT = GI ? GI->GetSubsystem<UAsdaItemTableSubsystem>() : nullptr;
	if (!Inv || Rec57.Num() < 57) { return; }
	const int32 Code = AsdaBytes::RdI32(Rec57.GetData() + 0);
	const int32 Cont = (int32)Rec57[4];
	const int32 Slot = AsdaBytes::RdI16(Rec57.GetData() + 5);
	if (Code <= 0 || (Cont != 1 && Cont != 2 && Cont != 3)) { return; }
	const FAsdaItemRecord* IR = IT ? IT->GetItem(Code) : nullptr;
	const FAsdaItemInstance* Cur = Inv->GetCell(Cont, Slot);
	const bool bSame = Cur && !Cur->IsEmpty() && Cur->ItemCode == Code;

	if (Kind == 0)
	{
		// Выставление: в ячейке должна лежать ЭТА вещь (эталон ищет по коду+контейнеру+слоту и молча пропускает иначе).
		if (!bSame)
		{
			UE_LOG(LogAsdaExchange, Warning, TEXT("аукцион 9902: в ячейке %d/%d нет предмета %d — сумка не тронута"), Cont, Slot, Code);
			return;
		}
		const bool bKeepRest = (Cont == 2) && IR && IR->TradableFlag != 0 && RemainAmount > 0;
		TArray<uint8> Upd = Rec57;
		AsdaBytes::WrI32(Upd.GetData() + 9, bKeepRest ? RemainAmount : 0);
		Inv->ApplyExternalRecord(Upd, UAsdaInventorySubsystem::EExternalRecordMode::CountOrClear);
		UE_LOG(LogAsdaExchange, Log, TEXT("аукцион 9902: ячейка %d/%d (предмет %d) — %s"), Cont, Slot, Code,
			bKeepRest ? *FString::Printf(TEXT("остаток %d"), RemainAmount) : TEXT("очищена (вещь ушла на рынок)"));
		return;
	}

	TArray<uint8> Put = Rec57;
	if (Kind != 3 && Cont == 2 && bSame && IR && IR->TradableFlag == 1)
	{
		// `invFindEquippedSlotItemCode` > 0: стопка того же кода в той же ячейке — количество складывается.
		int32 Amount = AsdaBytes::RdI32(Rec57.GetData() + 9);
		if (IR->FlagWord176 == 0) { Amount += FMath::Max(0, Cur->Amount); }
		AsdaBytes::WrI32(Put.GetData() + 9, Amount);
	}
	else if (Kind != 3 && Cont == 2 && bSame && IR && IR->TradableFlag <= 0)
	{
		AsdaBytes::WrI32(Put.GetData() + 9, 1);
	}
	Inv->ApplyExternalRecord(Put, UAsdaInventorySubsystem::EExternalRecordMode::AsIs);
	UE_LOG(LogAsdaExchange, Log, TEXT("аукцион %d: предмет %d ×%d положен в ячейку %d/%d"), Kind == 1 ? 9910 : 9906, Code,
		AsdaBytes::RdI32(Put.GetData() + 9), Cont, Slot);
}

// ★★★ ПАЧКА 20 (09.11.2026): ЗАПИСЬ ПРЕДМЕТА 57 Б ИЗ ЗАПИСИ 9910/9906. Раскладка у обоих приёмников одна
//   (`exchangeParseItemPacket` / `netParseItemAcquireResult`, заполнение `CSlot`): код +7 → 0, количество +11 → 9,
//   контейнер +15 → 4, слот +16 → 5, вес +18 → 14, прочность +20 → 13, +21 → 24, +23 → 25, +22 → 26, i16 +24 → 27,
//   пять опций +32… → 29…, восемь байт гнёзд → 16…23 (у 9910 с +53, у 9906 с +77). Прочее — как `CSlot_Init` порта.
static TArray<uint8> MakeLotRec57(const TArray<uint8>& Body, int32 Off, int32 SocketOff)
{
	FAsdaItemInstance Blank;
	Blank.Clear();
	TArray<uint8> R = Blank.Raw;
	R.SetNumZeroed(FAsdaItemInstance::kRecordSize);
	auto B = [&Body, Off](int32 O) { return (uint8)(Body.IsValidIndex(Off + O) ? Body[Off + O] : 0); };
	AsdaBytes::WrI32(R.GetData() + 0, AsdaBytes::SafeI32(Body, Off + 7));
	R[4] = B(15);
	AsdaBytes::WrI16(R.GetData() + 5, AsdaBytes::SafeI16(Body, Off + 16));
	AsdaBytes::WrI32(R.GetData() + 9, AsdaBytes::SafeI32(Body, Off + 11));
	R[13] = B(20);
	AsdaBytes::WrI16(R.GetData() + 14, AsdaBytes::SafeI16(Body, Off + 18));
	for (int32 K = 0; K < 8; ++K) { R[16 + K] = B(SocketOff + K); }
	R[24] = B(21);
	R[25] = B(23);
	R[26] = B(22);
	AsdaBytes::WrI16(R.GetData() + 27, AsdaBytes::SafeI16(Body, Off + 24));
	for (int32 K = 0; K < 20; ++K) { R[29 + K] = B(32 + K); }
	return R;
}

// Маршрут ответа «мои лоты» / «снятие». ★ ПАЧКА 20: по `a4` эталонного диспетчера (`Sub`), а не по длине тела — разбор у
//   ключа `asda.Exchange.RouteBySub`. Ключ 0 — прежний разбор по кратности длины (пустой 9908 уходил в ветку 9910).
void UAsdaExchangeSubsystem::HandleListingPacket(const TArray<uint8>& Body, int32 Sub)
{
	if (CVarExchangeRouteBySub.GetValueOnGameThread() != 0)
	{
		if (Sub == 1) { HandleTakeResult(Body); } else { HandleMyLotsPacket(Body); }
		return;
	}
	const bool bMyLots = (Body.Num() % kRec9908) == 0 && Body.Num() >= kRec9908;
	if (bMyLots) { HandleMyLotsPacket(Body); return; }
	HandleTakeResult(Body);
	RequestMyLots();   // прежнее поведение порта (ключ 0): после снятия список запрашивался заново
}

// 9908 — «мои лоты». Эталон: `boosterShopWnd_OnItemListPacket@0xD3A220` ветка `a3 == 0` (`shop_trade.c:12863-12884`) →
//   `inventory_ParseItemListPacket@0xD3B5C0`, затем ВСЕГДА показ панели 13200 и корня 13000 — ДАЖЕ ПРИ НУЛЕ ЗАПИСЕЙ
//   (жалоба 1: «окно не открывается, если ничего не выставлено»). Порт держит исправление D10 (уровень < 24 и пусто — без окна).
void UAsdaExchangeSubsystem::HandleMyLotsPacket(const TArray<uint8>& Body)
{
	// ★ Сервер шлёт ДО 10 записей (§9.4), а строк в панели ВОСЕМЬ — держим все, лишние
	//   доступны прокруткой 13249. Раньше записи 9 и 10 молча терялись.
	MyLots.Reset();
	// ★ Пачка 20, жалоба 5: свежий список приходит без отметок — большая галочка «всё» гаснет вместе с ними.
	DropAllCheck(2);
	MyLots.SetNum(kMyMaxRecs);
	ScrollRowMy = 0;
	int32 Out = 0;
	for (int32 Off = 0; Off + kRec9908 <= Body.Num() && Out < kMyMaxRecs; Off += kRec9908)
	{
		const int32 Status = U8(Body, Off + 4);
		if (Status != 0)
		{
			// ★ Эталон обрабатывает только 1 и 100, остальное игнорирует молча. Поведение
			//   сохраняем, но ПИШЕМ В ЛОГ: именно немой код (напр. 2 — «NPC-аукционщик не
			//   найден») и не давал открыться окну «моих лотов».
			UE_LOG(LogAsdaExchange, Warning,
				TEXT("аукцион 9908: запись %d, статус %d%s"), Off / kRec9908, Status,
				Loc9908(Status) ? TEXT("") : TEXT(" — эталон этот код не обрабатывает"));
			if (const int32 Loc = Loc9908(Status)) { NoticeChat(Loc); }
			continue;
		}
		FAsdaExchangeMyLot& L = MyLots[Out++];
		L.Serial   = I32(Body, Off + 0);
		L.ItemCode = I32(Body, Off + 5);
		L.Amount   = I32(Body, Off + 9);
		L.Price    = I32(Body, Off + 50);
		L.Item.ItemCode   = L.ItemCode;
		L.Item.Amount     = L.Amount;
		// ★ +13 → прочность (`item_inventory.c:46046`: байт +13 пакета ложится в байт +13
		//   записи предмета — `BYTE1(storageWnd[19*n + 1813])`).
		L.Item.Durability = (uint8)U8(Body, Off + 13);
		// ★★★ ДОБАВЛЕНО 2026-09-02. Порт читал из записи 9908 только код/количество/цену/
		//   прочность/остаток/поле +46, а эталонный ЗАБОР ЛОТА (`itemMediation_sendConfirm`,
		//   опкод 9909) кладёт в запись ещё шесть полей — они брались из тех же строк
		//   «моих лотов» и без них уходили нулями. Раскладка — в шапке `SendTakeCheckedLots`.
		//     +14 → запись предмета +24 — УРОВЕНЬ ЗАТОЧКИ  (`item_inventory.c:46047`);
		//     +15 → запись предмета +26                    (`item_inventory.c:46049`);
		//     +16 → запись предмета +25                    (`item_inventory.c:46048`);
		//     +17…18 → запись предмета +27…28 — ПРОЦЕНТ ЗАТОЧКИ (`item_inventory.c:46050`);
		//     +25…44 → запись предмета +29…48 — пять пар заточки (`item_inventory.c:46051-46052`).
		L.Item.GemGrade       = (uint8)U8(Body, Off + 14);
		L.ItemRec26           = U8(Body, Off + 15);
		L.ItemRec25           = U8(Body, Off + 16);
		L.Item.EnchantPercent = AsdaBytes::SafeI16(Body, Off + 17);
		L.EnchantOpts.SetNumZeroed(5);
		for (int32 S = 0; S < 5; ++S) { L.EnchantOpts[S] = I32(Body, Off + 25 + S * 4); }
		// ★★★ ПАЧКА 20 — [ПОРТ] ОСТАТОК ТОРГОВ ЛЕЖИТ В +46, А НЕ В +54. Формат `tagBoosterShop_FormatSlotRemainTime` зовётся с
		//   `pDigit[19*i + 4]` (`shop_trade.c:13395`), а `pDigit[19*i + 6]` той же строки — серийник (dword 1828, сверка в
		//   `exchangeParseItemPacket`), значит `pDigit[+4]` = dword **1826** = байт **+46** записи. ItemRelay кладёт туда
		//   `1000 × остаток` (`Process_MyItemInquiryReq`, `ItemRelayServer.c:2868-2871`), ноль — срок истёк (нотис 12697). Байты
		//   +54…+61 — четыре i16 записи предмета +16…+23 (гнёзда), их порт показывал как время. Ключ 0 — прежнее +54.
		L.RemainMs = I32(Body, Off + ((CVarExchangeRouteBySub.GetValueOnGameThread() != 0) ? 46 : 54));
		// ★★★ ИСПРАВЛЕН ДЕФЕКТ ПОРТА (аудит 2026-09-02, К-7а): раньше здесь стояло
		//   `if (I32(Body, Off+46) == 0) bAuctionBlocked = true;` — флаг «поднимался» прямо
		//   в разборе пакета и НИКОГДА не снимался, поэтому после первого же просмотра
		//   «моих лотов» с висящим лотом аукцион закрывался до перезахода.
		//   ЭТАЛОН ЗДЕСЬ ФЛАГ НЕ ТРОГАЕТ ВООБЩЕ: `inventory_ParseItemListPacket@0xD3B5C0`
		//   (`item_inventory.c:46056`) только КЛАДЁТ поле +46 в запись
		//   (`storageWnd[19*n + 1826]`), а решение принимает отдельный ПЕРЕСЧЁТ
		//   `playerInv_CountSowelEquipped` + `if/else` в `itemMediation_onConfirmEvent`,
		//   который умеет флаг и снимать (см. `RecomputeAuctionBlocked`).
		L.Field46 = I32(Body, Off + 46);
	}
	NoticeChat(kLocMyListOk);

	// ★★★ ДЕФЕКТ ОРИГИНАЛА **D10** — «модалка про 24-й уровень поверх ПУСТОГО окна».
	//   Эталон (`boosterShopWnd_OnItemListPacket`, `shop_trade.c:12866-12882`) делает так:
	//   показать панель 13200 → показать корень 13000 → и ТОЛЬКО ПОТОМ, если
	//   `CharManager_GetLocalPlayer()->level < 0x18` (24), вывести модалку **97101**.
	//   То есть окно открывается ВСЕГДА, даже когда показывать в нём нечего и торговать нельзя.
	//   Владелец назвал это дефектом (проверка в игре 2026-08-27), правим по закону
	//   `fix-original-defects`: если уровень < 24 И СПИСОК ПУСТ — окно не открываем, остаётся
	//   одна модалка. Если лоты ЕСТЬ — окно открываем всегда: иначе их нечем забрать.
	//   Откат к эталонному поведению: `asda.Exchange.LegacyLevelGate 1`.
	const bool bLowLevel = (SelfLevel() < 24);
	const bool bEmpty    = (Out == 0);
	// ★ ВТОРУЮ модалку не плодим: с 2026-08-28 ту же 97101 показывает `LevelGateBlocks` ещё
	//   ДО отправки 9907, и она к моменту ответа обычно ещё на экране. Проверка
	//   БЕЗ ЗАЩЁЛКИ — по факту наличия модалки, а не по запомненному флагу. Если игрок её
	//   уже закрыл (или гейт был откачен `LegacyLevelGate 1`), модалка выводится как раньше.
	if (bLowLevel)
	{
		UGameInstance* GI = GetGameInstance();
		const UAsdaWindowSubsystem* WM = GI ? GI->GetSubsystem<UAsdaWindowSubsystem>() : nullptr;
		if (!WM || !WM->IsAnyModalOpen()) { NoticeModal(kLocLevel24); }
	}
	if (bLowLevel && bEmpty && CVarExchangeLegacyLevelGate.GetValueOnGameThread() == 0)
	{
		UE_LOG(LogAsdaExchange, Log,
			TEXT("аукцион: уровень %d < 24 и «моих лотов» нет — окно не открываем (дефект D10 исправлен)"),
			SelfLevel());
		Mode = EAsdaExchangeMode::None;
		return;
	}

	// Панель «моих лотов» показывает именно ПРИЁМНИК — окно по режиму 3 не открывалось.
	EnsureWidget();
	if (Widget && bControlsReady)
	{
		Mode = EAsdaExchangeMode::MyLots;
		ShowPanel(EAsdaExchangeMode::MyLots);
	}
	RefreshMyLots();
}

// 9910 — результат снятия лота. Эталон `exchangeParseItemPacket@0xD3A3E0` (`shop_trade.c:12890-13230`):
//   код 0 — вещь кладётся в сумку по контейнеру/слоту записи (жалобы 4 и 9), лот с тем же кодом и серийником убирается из
//   списка «моих лотов» на месте (без нового 9907), чат 20170 с именем; коды 1…5, 94…99 — чат + модалка и разбор СТОП
//   (`return` эталона). После разбора — уплотнение списка, счётчик 13481 и, если что-то забрано, модалка 12542 и
//   большая галочка «всё» гаснет.
void UAsdaExchangeSubsystem::HandleTakeResult(const TArray<uint8>& Body)
{
	UGameInstance* GI = GetGameInstance();
	const UAsdaItemTableSubsystem* IT = GI ? GI->GetSubsystem<UAsdaItemTableSubsystem>() : nullptr;
	const bool bNew = CVarExchangeRouteBySub.GetValueOnGameThread() != 0;
	bool bNeedRefresh = false;
	bool bAllOk = Body.Num() >= kRec9910;
	int32 N = 0;
	for (int32 Off = 0; Off + kRec9910 <= Body.Num() && N < kMyMaxRecs; Off += kRec9910, ++N)
	{
		const int32 Code = U8(Body, Off + 6);
		if (Code != 0)
		{
			bAllOk = false;
			if (const int32 Loc = Loc9910(Code)) { NoticeChat(Loc); NoticeModal(Loc); }
			if (bNew) { break; }   // эталон: `return` на первом же отказе
			continue;
		}
		const int32 ItemCode = I32(Body, Off + 7);
		FAsdaSowellRow Sw;
		FAsdaSpecialItemlistRow Sp;
		if (bNew && IT && !IT->GetItem(ItemCode) && !IT->GetSowell(ItemCode, Sw) && !IT->GetSpecialItem(ItemCode, Sp)) { continue; }
		if (CVarExchangeApplyInventory.GetValueOnGameThread() != 0) { ApplyLotToInventory(1, MakeLotRec57(Body, Off, 53), 0); }
		if (bNew)
		{
			const int32 Serial = I32(Body, Off + 0);
			for (FAsdaExchangeMyLot& L : MyLots)
			{
				if (!L.IsEmpty() && L.ItemCode == ItemCode && L.Serial == Serial) { L = FAsdaExchangeMyLot(); }
			}
		}
		// ★ 20170 «[ss1] удаляется из списка продаж» — [ss1] = ИМЯ ПРЕДМЕТА (`shop_trade.c:13049-13066`).
		NoticeChatFmt(kLocTakeDoneChat, { ItemNameByCode(ItemCode) }, {});
		bNeedRefresh = true;
	}
	if (!bNew)
	{
		if (bAllOk) { NoticeModal(kLocTakeDoneModal); }
		return;
	}
	// Уплотнение «моих лотов» (`compactedSlots`, `shop_trade.c:13176-13182`) — длина массива фиксированная.
	TArray<FAsdaExchangeMyLot> Kept;
	for (const FAsdaExchangeMyLot& L : MyLots) { if (!L.IsEmpty()) { Kept.Add(L); } }
	MyLots.Reset(); MyLots.SetNum(kMyMaxRecs);
	for (int32 I = 0; I < Kept.Num() && I < kMyMaxRecs; ++I) { MyLots[I] = Kept[I]; }
	if (bNeedRefresh)
	{
		DropAllCheck(2);
		NoticeModal(kLocTakeDoneModal);
	}
	RefreshMyLots();
}

// Маршрут ответа «страница поиска» / «покупка». ★ ПАЧКА 20: по `a4` (`Sub`), ключ `asda.Exchange.RouteBySub`.
void UAsdaExchangeSubsystem::HandleTradeResult(const TArray<uint8>& Body, int32 Sub)
{
	if (CVarExchangeRouteBySub.GetValueOnGameThread() != 0)
	{
		if (Sub == 1) { HandleBuyResult(Body); } else { HandleSearchPage(Body); }
		return;
	}
	const bool bSearch = (Body.Num() % kRec9904) == 0 && Body.Num() >= kRec9904;
	if (bSearch) { HandleSearchPage(Body); } else { HandleBuyResult(Body); }
}

// 9904 — страница поиска. Эталон `netOnItemSlotListResult@0xD5BAB0` (`network.c:21540-21640`): ВСЕ 8 строк выдачи сначала
//   очищаются (`slotHolderInit`), затем заполняются записями — пустая страница даёт пустой список, а не «старые лоты».
void UAsdaExchangeSubsystem::HandleSearchPage(const TArray<uint8>& Body)
{
	Listings.Reset();
	// ★ Пачка 20, жалоба 5: эталон на каждой записи страницы гасит большую галочку (`wnd[294]`, `wnd[2074] = 0`).
	DropAllCheck(3);
	Listings.SetNum(kLotMaxRecs);
	ScrollRowLot = 0;
	int32 Out = 0;
	for (int32 Off = 0; Off + kRec9904 <= Body.Num() && Out < kLotMaxRecs; Off += kRec9904)
	{
		const int32 Code = U8(Body, Off + 4);
		if (Code != 0)
		{
			if (const int32 Loc = Loc9904(Code)) { NoticeChat(Loc); }
			continue;
		}
		FAsdaExchangeListing& L = Listings[Out++];
		L.Serial   = I32(Body, Off + 0);
		L.ItemCode = I32(Body, Off + 5);
		L.Amount   = I32(Body, Off + 9);
		L.Price    = I32(Body, Off + 48);
		L.Item.ItemCode   = L.ItemCode;
		L.Item.Amount     = L.Amount;
		// ★ +13 → ПРОЧНОСТЬ. Эталон `netOnItemSlotListResult@0xD5BAB0` (`network.c:21570`):
		//   `BYTE1(wnd[29*i + 2093]) = BYTE1(recordBuf[20*i + 3])` — байт **+13** пакета
		//   ложится в байт **+13** записи предмета (dword 2093 = байт 8372, +1 = 8373;
		//   строка лота начинается с байта 8336, запись предмета внутри неё — с +24,
		//   значит 8373 − 8336 − 24 = **13**). Байт +13 записи предмета — прочность
		//   (закон `item-record-plus13-durability`).
		L.Item.Durability = (uint8)U8(Body, Off + 13);
		// ★★★ ДОБАВЛЕНО 2026-09-02: +14 → УРОВЕНЬ ЗАТОЧКИ. Эталон `network.c:21571`:
		//   `LOBYTE(wnd[29*i + 2096]) = BYTE2(recordBuf[20*i + 3])` — байт **+14** пакета
		//   ложится в dword 2096 = байт 8384, то есть в байт **+24** записи предмета
		//   (8384 − 8336 − 24 = 24). Байт +24 записи предмета — уровень заточки
		//   (`FAsdaItemInstance::GemGrade`). Порт это поле не читал, из-за чего запись
		//   покупки 9905 уходила без заточки — см. `SendBuyChecked`.
		L.Item.GemGrade   = (uint8)U8(Body, Off + 14);
		L.EnchantOpts.SetNumZeroed(kLotSowels);
		for (int32 S = 0; S < kLotSowels; ++S) { L.EnchantOpts[S] = I32(Body, Off + 25 + S * 4); }
		// Имя продавца — 20 байт cp1251 по +52.
		{
			char Buf[21] = { 0 };
			for (int32 B = 0; B < 20; ++B) { Buf[B] = (char)U8(Body, Off + 52 + B); }
			// ★ [ПОРТ] 08.10.2026: было `ANSI_TO_TCHAR` (кодовая страница ОС — на не-русской Windows кириллица билась).
			L.Seller = FAsdaLoginData::DecodeCp1251(reinterpret_cast<const uint8*>(Buf), 20);
		}
		// ★ Номера страниц лежат В КАЖДОЙ ЗАПИСИ; побеждает последняя, как в эталоне.
		PageCur  = U8(Body, Off + 46);
		PageLast = U8(Body, Off + 47);
	}
	RefreshListings();
}

// 9906 — результат покупки (`netParseItemAcquireResult@0xD57620`).
void UAsdaExchangeSubsystem::HandleBuyResult(const TArray<uint8>& Body)
{
	// 9906 — результат покупки. Код результата — ЗНАКОВЫЙ байт +6.
	bool bAnyOk = false;
	for (int32 Off = 0; Off + kRec9906 <= Body.Num(); Off += kRec9906)
	{
		const int32 Code = (int32)(int8)U8(Body, Off + 6);
		if (Code != 0)
		{
			if (const int32 Loc = Loc9906(Code)) { NoticeChat(Loc); NoticeModal(Loc); }
			continue;
		}
		// ★ 20065 «Ты покупаешь [ss1] за [ss2] золота»: [ss1] = имя предмета (+7 записи 9906),
		//   [ss2] = уплаченное золото (+53, §7.4) с разделителями разрядов.
		bAnyOk = true;
		// ★★★ ПАЧКА 20 — [ОРИГИНАЛ] `netParseItemAcquireResult@0xD57620` (`network.c:21170-21225`): купленная вещь кладётся в сумку
		//   (`invSlotUpdateOrRemove`, режим окна 4 — со стеком, 5 — в слот), золото = текущее − уплаченное (+53).
		if (CVarExchangeApplyInventory.GetValueOnGameThread() != 0)
		{
			ApplyLotToInventory(Mode == EAsdaExchangeMode::BrowseB ? 3 : 2, MakeLotRec57(Body, Off, 77), 0);
			const int32 Paid = I32(Body, Off + 53);
			if (UAsdaInventorySubsystem* Inv = GetGameInstance() ? GetGameInstance()->GetSubsystem<UAsdaInventorySubsystem>() : nullptr)
			{
				const int32 Left = Inv->GetGold() - Paid;
				if (Paid > 0 && Left > 0) { Inv->SetGoldDisplay(Left); }
			}
		}
		NoticeChatFmt(kLocBuyDoneChat,
			{ ItemNameByCode(I32(Body, Off + 7)),
			  AsdaUi::FormatThousands(I32(Body, Off + 53)) }, {});
		// Купленный лот убираем из выдачи, как эталон.
		const int32 Serial = I32(Body, Off + 0);
		for (FAsdaExchangeListing& L : Listings)
		{
			if (L.Serial == Serial) { L = FAsdaExchangeListing(); }
		}
	}
	if (bAnyOk) { NoticeModal(kLocBuyDoneModal); }
	RefreshListings();
}

// ═══════════════════════ нотисы ═══════════════════════════════════════════════════════════════
void UAsdaExchangeSubsystem::NoticeChat(int32 LocId) const
{
	// ★ Волна 1 (14.09.2026, В2): маршрут — фасад `AsdaNotice` (категория та же, поведение то же).
	AsdaNotice::Chat(GetGameInstance(), EAsdaMsgCategory::Trade, LocId);
}

// ★★★ НОТИС С АРГУМЕНТАМИ (жалоба владельца 2026-08-28: в чат уходил сырой шаблон
//   «Всего выставлено предметов: [sd1]. Общая цена: [ss1]. Стоимость регистрации: [ss2].»).
//   Эталон перед КАЖДЫМ таким нотисом заполняет слоты контекста `g_pLocNoticeCtx`
//   (`CObject_SetSlotString` / `CClass_SetSlotPair`) и только потом зовёт
//   `UI_FormatLocalizedNotice`. В порте слоты передаются в сообщение (`PostLocFmt`), а
//   подстановку делает общая точка `UAsdaTextSubsystem`.
UAsdaItemTooltipSubsystem* UAsdaExchangeSubsystem::TooltipSys() const
{
	UGameInstance* GI = GetGameInstance();
	return GI ? GI->GetSubsystem<UAsdaItemTooltipSubsystem>() : nullptr;
}

// Имя предмета по коду — через общую точку таблиц предметов (второй копии не заводим).
FString UAsdaExchangeSubsystem::ItemNameByCode(int32 ItemCode) const
{
	UGameInstance* GI = GetGameInstance();
	const UAsdaTextSubsystem* T = GI ? GI->GetSubsystem<UAsdaTextSubsystem>() : nullptr;
	return (T && ItemCode > 0) ? T->GetItemName(ItemCode) : FString();
}

void UAsdaExchangeSubsystem::NoticeChatFmt(int32 LocId, const TArray<FString>& StrArgs,
	const TArray<int32>& NumArgs) const
{
	// ★ Волна 1 (14.09.2026, В2): маршрут — фасад `AsdaNotice` (категория та же, поведение то же).
	AsdaNotice::ChatFmt(GetGameInstance(), EAsdaMsgCategory::Trade, LocId, StrArgs, NumArgs);
}

void UAsdaExchangeSubsystem::NoticeChatBFmt(int32 LocId, const TArray<FString>& StrArgs) const
{
	UGameInstance* GI = GetGameInstance();
	UAsdaMessageSubsystem* MS = GI ? GI->GetSubsystem<UAsdaMessageSubsystem>() : nullptr;
	if (!MS) { return; }
	FAsdaMsgParams P;
	P.Category   = EAsdaMsgCategory::Trade;
	P.LocId      = LocId;
	P.StringArgs = StrArgs;
	P.Targets    = (int32)EAsdaMsgTarget::SystemChat | (int32)EAsdaMsgTarget::TrackB;
	MS->Post(P);
}

void UAsdaExchangeSubsystem::NoticeChatB(int32 LocId) const
{
	// ★★★ БУФЕР B — ОТДЕЛЬНЫЙ КАНАЛ, а не «тот же чат». Эталон отличает его аргументами
	//   `CChat_AddTokenizedLines(txt, −11526, 0, /*flag*/ 1)`: при `flag == 1` строка идёт во
	//   всплывающий буфер B на 10 с (`chat.c:3285`), при 0 — в буфер A на `dur` мс. В порте это
	//   бит `EAsdaMsgTarget::TrackB`. РАНЬШЕ ЗДЕСЬ СТОЯЛ ОБЫЧНЫЙ `PostLoc`, и оба нотиса 9913
	//   («твой лот куплен» 20171 и налог 20172) уходили не в тот буфер.
	UGameInstance* GI = GetGameInstance();
	UAsdaMessageSubsystem* MS = GI ? GI->GetSubsystem<UAsdaMessageSubsystem>() : nullptr;
	if (!MS) { return; }
	FAsdaMsgParams P;
	P.Category = EAsdaMsgCategory::Trade;
	P.LocId    = LocId;
	P.Targets  = (int32)EAsdaMsgTarget::SystemChat | (int32)EAsdaMsgTarget::TrackB;
	MS->Post(P);
}

void UAsdaExchangeSubsystem::NoticeTip(int32 LocId) const
{
	// Всплывающая подсказка (`CControl_SetTooltipText`) в порте — приёмник `WARMESSAGE_WIN` 221.
	//   ★ ОСОЗНАННОЕ ОТСТУПЛЕНИЕ (решение владельца, шапка `AsdaMessageSubsystem.h`): канал 221
	//   оставлен ТОЛЬКО за военными событиями, остальной feedback идёт в системный чат. Поэтому
	//   отдельного вывода здесь нет — см. `NoticeChatAndTip`.
	NoticeChat(LocId);
}

void UAsdaExchangeSubsystem::NoticeChatAndTip(int32 LocId) const
{
	// Эталон выводит 12555 ОДНОВРЕМЕННО в чат и во всплывающую подсказку (`item_inventory.c:46803-46807`).
	//   ★ В порте оба приёмника сведены в системный чат (решение владельца о канале 221), поэтому
	//   ПОСТИМ ОДИН РАЗ: два вызова давали бы ДВЕ ОДИНАКОВЫЕ строки подряд.
	NoticeChat(LocId);
}

void UAsdaExchangeSubsystem::NoticeModal(int32 LocId) const
{
	// ★ МИСНЕЙМ M6: `CItem_BuildTooltipText` — это МОДАЛЬНОЕ окно-сообщение (`CMsgBoxMgr`),
	//   а не тултип. Кнопка подписана нотисом 2288.
	// ★ Волна 1 (14.09.2026, В2): единый маршрут `AsdaNotice::MsgBox` — окно 1500, без оконной подсистемы —
	//   строка в ленту той же категорией. Категория/подпись кнопки — прежние.
	UGameInstance* GI = GetGameInstance();
	AsdaNotice::MsgBox(GI, EAsdaMsgCategory::ItemRefused, AsdaNotice::LocRaw(GI, LocId, /*bDebugFallback*/ true), 2288);
}
