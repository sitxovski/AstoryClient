// Copyright (c) 2026 @sitxovski (Discord)
// AsdaExchangeSubsystem_Search.cpp — ★ R2 10.11.2026 (разрез AsdaExchangeSubsystem.cpp, дословный перенос): мои лоты, забор 9909, поиск/листинги, покупка 9905, фильтры, полосы прокрутки.
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
// имя предмета и подстановка аргументов нотиса   ★ R3: комментарий снятого включения "Text/AsdaTextSubsystem.h" (файл им не пользовался) — сохранён
#include "UI/AsdaItemTooltipSubsystem.h"   // общая точка подсказки предмета
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
// ★ С-Р1: общий офлайн-гейт отладочных команд (RefuseDebugWhenOnline)   ★ R3: комментарий снятого включения "AsdaOutgoingGate.h" (файл им не пользовался) — сохранён
#include "Test/AsdaSelfTestKit.h"   // ★ 11.10.2026 (В3 S1): общий каркас самотестов (аргументы, отказ, перехват, итог, выход)
#include "Login/AsdaLoginData.h"   // [ПОРТ] 08.10.2026: общий кодек провода (гибрид RU+EN+AR)
#include "AsdaExchangeSubsystemInternal.h"   // ★ R2 10.11.2026: общее для частей AsdaExchangeSubsystem*.cpp

namespace {
	// ── Геометрия трёх «колёсных окон»-списков (`gamemain_jui.ini`, абсолютные rect базы 1024×768;
	//    посчитано `asda_jui_parse.compute_anchors`). Узлов в WBP нет (NoSelect=1 + невидимый план),
	//    поэтому полоса ставится от ЖИВОЙ ячейки списка плюс эта дельта.
	//    13125 (16,174) 198×329 · первая ячейка `EXCHANGE_SELL0` (16,174) ⇒ дельта (0,0);
	//    13249 (16,222) 563×320 · первая иконка `EXCHMY_ITEM01`  (42,224) ⇒ дельта (−26,−2);
	//    13470 (16,223) 758×320 · первая иконка `EXCHANGER_ITEM01` (42,225) ⇒ дельта (−26,−2).
	constexpr float kSellListW = 198.f, kSellListH = 329.f;
	constexpr float kMyListW   = 563.f, kMyListH   = 320.f;
	constexpr float kLotListW  = 758.f, kLotListH  = 320.f;
	constexpr float kMyListDX  = -26.f, kMyListDY  = -2.f;
	constexpr float kLotListDX = -26.f, kLotListDY = -2.f;

	constexpr int32 kSellVisRows = 10;

	constexpr int32 kLocBuyBlocked    = 12545;  // клик «Купить» при занятых слотах — МОДАЛЬНОЕ ОК
}

// ═══════════════════════ панель «мои лоты» ════════════════════════════════════════════════════
void UAsdaExchangeSubsystem::RequestMyLots()
{
	UGameInstance* GI = GetGameInstance();
	UAsdaNetSubsystem* Net = GI ? GI->GetSubsystem<UAsdaNetSubsystem>() : nullptr;
	if (!Net) { return; }
	Net->SendExchangeMyLots9907(FMath::Max(0, MyCategory));
}

void UAsdaExchangeSubsystem::RefreshMyLots()
{
	// ★ Пачка 20, жалоба 5: снятая строка гасит «выбрать всё» (`uiShopSlotSetVisibleByRecord`).
	if (bMyAll) { for (const FAsdaExchangeMyLot& L : MyLots) { if (!L.IsEmpty() && !L.bChecked) { DropAllCheck(2); break; } } }
	UGameInstance* GI = GetGameInstance();
	const UAsdaItemTableSubsystem* IT = GI ? GI->GetSubsystem<UAsdaItemTableSubsystem>() : nullptr;

	int64 TotalGold = 0;
	int32 Count = 0;
	// ★ Итог считается по ВСЕМ записям (их до 10), а рисуются 8 строк начиная с `ScrollRowMy`.
	for (const FAsdaExchangeMyLot& All : MyLots)
	{
		if (!All.IsEmpty()) { ++Count; TotalGold += All.Price; }
	}
	ScrollRowMy = FMath::Clamp(ScrollRowMy, 0, GetMyMaxScrollRow());
	for (int32 R = 0; R < kMyRows; ++R)
	{
		static const FAsdaExchangeMyLot kEmptyLot;
		const int32 Idx = ScrollRowMy + R;
		const FAsdaExchangeMyLot& L = MyLots.IsValidIndex(Idx) ? MyLots[Idx] : kEmptyLot;
		const bool bHas = !L.IsEmpty();

		if (MyNames.IsValidIndex(R) && MyNames[R])
		{
			MyNames[R]->SetText(bHas && IT
				? FText::FromString(IT->GetItemName(L.ItemCode)) : FText::GetEmpty());
		}
		if (MyPrices.IsValidIndex(R) && MyPrices[R])
		{
			MyPrices[R]->SetText(bHas
				? FText::FromString(AsdaUi::FormatThousands((int32)L.Price)) : FText::GetEmpty());
		}
		if (MyTimes.IsValidIndex(R) && MyTimes[R])
		{
			MyTimes[R]->SetText(bHas ? FText::FromString(FormatRemainTime(L.RemainMs))
			                         : FText::GetEmpty());
		}
		if (MyChecks.IsValidIndex(R) && MyChecks[R])
		{
			MyChecks[R]->SetIsChecked(L.bChecked);
			MyChecks[R]->SetVisibility(bHas ? ESlateVisibility::Visible : ESlateVisibility::Hidden);
		}
	}
	// ★ ИСПРАВЛЕНИЕ 11.10.2026 (дедуп №2, В2-A, U10e): было `FText::AsNumber` (разделитель разрядов по культуре: EN «1,234», RU «1 234») — стало `FString::FromInt`, как `sprintf("%d")` эталона (`exchangeParseItemPacket@0xD3A3E0`, shop_trade.c:13209).
	if (TxtMyAmount)  { TxtMyAmount->SetText(FText::FromString(FString::FromInt(Count))); }
	if (TxtMyRegGold) { TxtMyRegGold->SetText(FText::FromString(AsdaUi::FormatThousands((int32)TotalGold))); }
	if (GridMy) { GridMy->Refresh(); }
}

void UAsdaExchangeSubsystem::SendTakeCheckedLots()
{
	UGameInstance* GI = GetGameInstance();
	UAsdaNetSubsystem* Net = GI ? GI->GetSubsystem<UAsdaNetSubsystem>() : nullptr;
	if (!Net) { return; }

	// ═══ ЗАПИСЬ ЗАБОРА 9909 — 45 БАЙТ, ПОЛНАЯ РАСКЛАДКА ════════════════════════════════════
	//   Эталон — `itemMediation_sendConfirm@0xD3BED0` (`item_inventory.c:46339-46348`):
	//   15 байт шапки + до десяти записей по 45 байт. Источник — строка «моих лотов» (76 байт,
	//   dword 1810 + 19·n), внутри которой с +0 лежит ЗАПИСЬ ПРЕДМЕТА (`item_inventory.c:46044-46056`).
	//
	//   | смещ. | что               | эталон 9909            | запись предмета | 9908  |
	//   |-------|-------------------|------------------------|-----------------|-------|
	//   | +0  4 | серийник лота     | `entryBuf[0]`   :46339 | —  (строка +72) | +0…3  |
	//   | +4  4 | код предмета      | `entryBuf[1]`   :46340 | +0              | +5    |
	//   | +8  4 | количество        | `entryBuf[2]`   :46341 | +9              | +9    |
	//   | +12 1 | прочность         | `LOBYTE([3])`   :46342 | +13             | +13   |
	//   | +13 1 | уровень заточки   | `BYTE1([3])`    :46343 | +24             | +14   |
	//   | +14 1 | (смысл не выведен)| `BYTE2([3])`    :46344 | +26             | +15   |
	//   | +15 1 | (смысл не выведен)| `HIBYTE([3])`   :46345 | +25             | +16   |
	//   |+16  2 | процент заточки   | `LOWORD([4])`   :46346 | +27…28          | +17…18|
	//   |+18…23 | НЕ ЗАПОЛНЯЮТСЯ    | эталон их не пишет     | —               | —     |
	//   |+24…43 | 5 пар заточки     | `[optWordIdx+6]`:46348 | +29…48          | +25…44|
	//   | +44 1 | НЕ ЗАПОЛНЯЕТСЯ    | эталон его не пишет    | —               | —     |
	//
	//   ★★★ ИСПРАВЛЕНО 2026-09-02: порт слал ТОЛЬКО +0/+4/+8, причём количество — как i16,
	//   тогда как эталон кладёт полный dword (`entryBuf[2]`). Остальные девятнадцать байт
	//   уходили нулями. Правка МЕНЯЕТ СОДЕРЖИМОЕ ПАКЕТА К СТАРОМУ СЕРВЕРУ и допущена потому,
	//   что ВОЗВРАЩАЕТ к эталону: `Recv_TransAction_DeleteItemReq@0x51E380` читает из записи
	//   ровно два поля — код (`GameServer.c:108944`) и количество (`:108980`), а всю запись
	//   копирует как есть (`:108922-108923`) и ретранслирует пакетом 9909 (`:109019`).
	TArray<TArray<uint8>> Rows;
	for (const FAsdaExchangeMyLot& L : MyLots)
	{
		if (L.IsEmpty() || !L.bChecked) { continue; }
		Rows.Add(BuildDeleteRow9909(L));   // ★ U6: запись — сборщиком ниже
	}
	if (Rows.Num() == 0) { return; }
	Net->SendExchangeDelete9909(Rows);
}

// ★ U6 (В1 дедупа №2): запись через `FAsdaPacketWriter` (байты те же, что у прежнего ручного буфера). Раскладка — таблица в `SendTakeCheckedLots` выше.
TArray<uint8> UAsdaExchangeSubsystem::BuildDeleteRow9909(const FAsdaExchangeMyLot& L)
{
	FAsdaPacketWriter W(45);
	W.I32(0, L.Serial);
	W.I32(4, L.ItemCode);
	W.I32(8, L.Amount);           // ★ dword, а не i16
	W.U8(12, L.Item.Durability);
	W.U8(13, L.Item.GemGrade);
	W.U8(14, L.ItemRec26);
	W.U8(15, L.ItemRec25);
	W.I16(16, L.Item.EnchantPercent);
	// +18…+23 эталон не заполняет — остаются нулями.
	for (int32 S = 0; S < 5; ++S)
	{
		const int32 V = L.EnchantOpts.IsValidIndex(S) ? L.EnchantOpts[S] : 0;
		W.I32(24 + S * 4, V);
	}
	// +44 эталон не заполняет — остаётся нулём.
	return W.ToArray();
}

// ═══════════════════════ панель поиска ════════════════════════════════════════════════════════
void UAsdaExchangeSubsystem::SendSearch()
{
	UGameInstance* GI = GetGameInstance();
	UAsdaNetSubsystem* Net = GI ? GI->GetSubsystem<UAsdaNetSubsystem>() : nullptr;
	if (!Net) { return; }

	// ★★★ P1: восемь байт критериев целиком по `mediationSendSearch@0xD55BB0`
	//   (`shop_trade.c:14663-14840`). Гейт режима 5 живёт ВНУТРИ `BuildSearchCriteria`.
	if (Mode != EAsdaExchangeMode::BrowseA && Mode != EAsdaExchangeMode::BrowseB) { return; }
	if (Mode == EAsdaExchangeMode::BrowseB && EquipSlotCodeOf(SearchParamGrade) <= -1)
	{
		// ★ ГЕЙТ ЭТАЛОНА: в режиме 5 при `charGetEquipSlotCode() <= -1` функция выходит ДО отправки
		//   (`shop_trade.c:14667-14668`) — пакет не уходит и нотиса нет. Поведение сохраняем.
		UE_LOG(LogAsdaExchange, Verbose,
			TEXT("аукцион: 9903 не отправлен — в режиме 5 не выбран пункт фильтра 13410 (эталонный гейт)"));
		return;
	}
	uint8 C[8] = { 0 };
	BuildSearchCriteria(C);
	Net->SendExchangeSearch9903(C);
}

void UAsdaExchangeSubsystem::RefreshListings()
{
	// ★ Пачка 20, жалоба 5: снятая строка гасит «выбрать всё» (`uiSowelSlotCommandHandler`).
	if (bLotAll) { for (const FAsdaExchangeListing& L : Listings) { if (!L.IsEmpty() && !L.bChecked) { DropAllCheck(3); break; } } }
	UGameInstance* GI = GetGameInstance();
	const UAsdaItemTableSubsystem* IT = GI ? GI->GetSubsystem<UAsdaItemTableSubsystem>() : nullptr;

	int64 BuySum = 0;
	// Сумма покупки — по ВСЕМ отмеченным записям страницы, а не по видимым восьми строкам.
	for (const FAsdaExchangeListing& All : Listings)
	{
		if (!All.IsEmpty() && All.bChecked) { BuySum += All.Price; }
	}
	ScrollRowLot = FMath::Clamp(ScrollRowLot, 0, GetLotMaxScrollRow());
	for (int32 R = 0; R < kLotRows; ++R)
	{
		static const FAsdaExchangeListing kEmptyLot;
		const int32 Idx = ScrollRowLot + R;
		const FAsdaExchangeListing& L = Listings.IsValidIndex(Idx) ? Listings[Idx] : kEmptyLot;
		const bool bHas = !L.IsEmpty();

		if (LotNames.IsValidIndex(R) && LotNames[R])
		{
			LotNames[R]->SetText(bHas && IT
				? FText::FromString(IT->GetItemName(L.ItemCode)) : FText::GetEmpty());
		}
		if (LotPrices.IsValidIndex(R) && LotPrices[R])
		{
			LotPrices[R]->SetText(bHas
				? FText::FromString(AsdaUi::FormatThousands((int32)L.Price)) : FText::GetEmpty());
		}
		if (LotSellers.IsValidIndex(R) && LotSellers[R])
		{
			LotSellers[R]->SetText(bHas ? FText::FromString(L.Seller) : FText::GetEmpty());
		}
		if (LotChecks.IsValidIndex(R) && LotChecks[R])
		{
			LotChecks[R]->SetIsChecked(L.bChecked);
			LotChecks[R]->SetVisibility(bHas ? ESlateVisibility::Visible : ESlateVisibility::Hidden);
		}
	}
	if (TxtBuyPrice) { TxtBuyPrice->SetText(FText::FromString(AsdaUi::FormatThousands((int32)BuySum))); }
	if (TxtExchGold)
	{
		const UAsdaInventorySubsystem* Inv = GI ? GI->GetSubsystem<UAsdaInventorySubsystem>() : nullptr;
		TxtExchGold->SetText(FText::FromString(AsdaUi::FormatThousands(Inv ? Inv->GetGold() : 0)));
	}
	if (GridLots) { GridLots->Refresh(); }
	RefreshSowelIcons();
	RefreshPager();
}

// ★★★ P2: гнёзда самоцветов строк выдачи (13511…13585).
//   ⚠ УТОЧНЕНО 2026-09-02: поля `+25…+44` записи 9904 (`FAsdaExchangeListing::EnchantOpts`) —
//   это НЕ коды самоцветов, а пять пар заточки (эталон `network.c:21575-21576` кладёт их
//   в запись предмета с +29). Настоящие четыре гнезда лежат в байтах `+72…+79`
//   (`network.c:21577-21578`) и порт их не читает — гнёзда всё равно погашены (см. ниже).
//   ⚠ ЧЕСТНО: САМ ОРИГИНАЛ ЭТИ ИКОНКИ НЕ РИСУЕТ. `CItemMediation_BuildUI` их не связывает, а
//   `CSatisfyWindow_RefreshItemSlots@0xD58810` (`ui_core.c:52512-52795`) трогает у строки только
//   контролы +201…+205 (иконка/имя/цена/продавец/галочка). Второй метод: во всём
//   `ida/export/AsdaStory_ida.c` (1 293 125 строк) IDNum 13510…13585 встречаются РОВНО ДВАЖДЫ —
//   обе в диапазонном `if` функции `uiAvatarMapItemToCostumeSlot@0xD423A0`. То есть в оригинале
//   это ПУСТЫЕ квадраты-ловушки клика — недоделка (заголовок колонки `EXCHANGER_SOWEL` 13502
//   «소웰» на месте, данные в пакете есть, а рисовать их забыли).
//   ⇒ ИСПРАВЛЕНИЕ ДЕФЕКТА ОРИГИНАЛА (закон `fix-original-defects`): гнёзда рисуем из тех же
//   данных пакета той же общей точкой, что и все прочие иконки предметов. Клик по гнезду
//   оставлен эталонным — выбирает СТРОКУ.
void UAsdaExchangeSubsystem::RefreshSowelIcons()
{
	if (GridSowel) { GridSowel->Refresh(); }
}

void UAsdaExchangeSubsystem::RefreshPager()
{
	// `skillwin_UpdatePageNumberUI@0xD59760` (★ миснейм M14): печатает номера +1 и ПРЯЧЕТ «вперёд»
	//   на последней странице.
	// ★ ИСПРАВЛЕНИЕ 11.10.2026 (дедуп №2, В2-A, U10e): было `FText::AsNumber` (разделитель разрядов по культуре: EN «1,234», RU «1 234») — стало `FString::FromInt`, как `sprintf("%d")` эталона (`skillwin_UpdatePageNumberUI@0xD59760`, skill_buff.c:29684-29718).
	if (TxtPage1) { TxtPage1->SetText(FText::FromString(FString::FromInt(PageCur  + 1))); }
	if (TxtPage2) { TxtPage2->SetText(FText::FromString(FString::FromInt(PageLast + 1))); }
	if (BtnPageNext)
	{
		BtnPageNext->SetVisibility(PageCur >= PageLast
			? ESlateVisibility::Hidden : ESlateVisibility::Visible);
	}
	if (BtnPagePrev)
	{
		BtnPagePrev->SetVisibility(PageCur <= 0
			? ESlateVisibility::Hidden : ESlateVisibility::Visible);
	}
}

void UAsdaExchangeSubsystem::PageNavigate(int32 Dir, int32 Jump)
{
	// Точный порт `uiTabNavigate` (`ui_core.c:53273-53320`).
	bool bSend = true;
	if (Jump != 0)
	{
		if (Jump == 1) { PageCur = 0; }
		else if (Jump == 2)
		{
			if (PageLast < 0) { PageLast = 0; }
			PageCur = PageLast;
		}
	}
	else if (Dir != 0)
	{
		// «вперёд»: на последней странице запрос НЕ шлётся — терминатора у аукциона нет.
		if (PageCur == PageLast) { bSend = false; } else { ++PageCur; }
	}
	else
	{
		if (PageCur > 0) { --PageCur; } else { bSend = false; }
	}

	if (bSend && (Mode == EAsdaExchangeMode::BrowseA || Mode == EAsdaExchangeMode::BrowseB))
	{
		SendSearch();
	}
}

void UAsdaExchangeSubsystem::SendBuyChecked()
{
	UGameInstance* GI = GetGameInstance();
	UAsdaNetSubsystem* Net = GI ? GI->GetSubsystem<UAsdaNetSubsystem>() : nullptr;
	if (!Net) { return; }

	// ═══ ЗАПИСЬ ПОКУПКИ 9905 — 45 БАЙТ, ПОЛНАЯ РАСКЛАДКА ═══════════════════════════════════
	//   Эталон — `mediationSendBuy@0xD56990` (`shop_trade.c:15080-15087`): пакет = 15 байт шапки
	//   + до восьми записей по 45 байт (`memcpy(&v26[Size], itemRecordBuf, 0x2Du)`).
	//   Источник каждого поля — СОХРАНЁННАЯ СТРОКА ЛОТА (116 байт, база объекта 0x2090,
	//   `network.c:21547` `memcpy(&wnd[29*i + 2084], slotTemplate, 0x74u)`), внутри которой
	//   с +24 лежит обычная ЗАПИСЬ ПРЕДМЕТА (57 б) — это доказывает
	//   `uiItemGridShowTooltip@0xD54E90` (`ida/export/AsdaStory_game.c:368350`), передающий
	//   `&строка[+24]` в `ItemTooltip_Build` как указатель на запись предмета.
	//
	//   | смещ. | что            | эталон                        | 9904 | сервер                  |
	//   |-------|----------------|-------------------------------|------|-------------------------|
	//   | +0  4 | серийник лота  | `itemRecordBuf[0]`   :15080   | +0   | не читает, ретранслирует|
	//   | +4  4 | код предмета   | `itemRecordBuf[1]`   :15081   | +5   | ЧИТАЕТ, `GameServer.c:107875` |
	//   | +8  4 | количество     | `itemRecordBuf[2]`   :15082   | +9   | ЧИТАЕТ, `GameServer.c:107926` |
	//   | +12 1 | ПРОЧНОСТЬ      | `LOBYTE([3]) = v41`  :15083   | +13  | не читает, ретранслирует|
	//   | +13 1 | УРОВЕНЬ ЗАТОЧКИ| `BYTE1([3]) = v40`   :15084   | +14  | не читает, ретранслирует|
	//   |+14…23 | НЕ ЗАПОЛНЯЮТСЯ | эталон их не пишет            | —    | не читает               |
	//   |+24…43 | 5 пар заточки  | `[fieldIdx+6]`  :15085-15086  |+25…44| не читает, ретранслирует|
	//   | +44 1 | НЕ ЗАПОЛНЯЕТСЯ | эталон его не пишет           | —    | не читает               |
	//
	//   Арифметика смещений в строке лота (база 8336 = 0x2090, запись предмета с +24):
	//     `itemField1 = self->gap_20AC[116*slot + 5]`  (:15054) → байт 8369 → запись +9
	//                   ← `network.c:21568` от байта **+9** пакета 9904 (количество);
	//     `v41 = self->gap_20AC[116*slot + 9]`         (:15055) → байт 8373 → запись +13
	//                   ← `network.c:21570` от байта **+13** пакета 9904 (прочность);
	//     `v40 = self[1].gap_2[116*slot + 2]`          (:15056) → байт 8384 → запись +24
	//                   ← `network.c:21571` от байта **+14** пакета 9904 (уровень заточки);
	//     `itemSerial = self[1].aRegItemRowsCont[116*slot + 12]` (:15057) → байт 8448
	//                   ← `network.c:21579` от байтов **+0…3** пакета 9904 (серийник);
	//     `itemExtraFields[i] = &self[1].pExchGaer + 116*slot + 4*i + 1` (:15059) → байт 8389+4i
	//                   ← `network.c:21576` от байтов **+25+4i** пакета 9904.
	//   Смысл записи предмета +13 = прочность и +24 = уровень заточки — законы проекта
	//   `item-record-plus13-durability` и `FAsdaItemInstance::GemGrade` (оба доказаны отдельно).
	//   ⚠ НЕ ПУТАТЬ: «+13» здесь встречается ДВАЖДЫ и в РАЗНЫХ записях — байт +13 ЗАПИСИ
	//   ПРЕДМЕТА (прочность) уезжает в байт +12 ЗАПИСИ ПАКЕТА, а в байт +13 ЗАПИСИ ПАКЕТА
	//   едет байт +24 записи предмета (заточка).
	//
	//   Сервер: `Recv_TransAction_BuyItemReq@0x51AA10` читает из записи РОВНО ДВА поля — код
	//   (`GameServer.c:107875` `v22 = *(_DWORD *)&Src[45*j + 4]`) и количество
	//   (`:107926` `v30 += *(_WORD *)&Src[45*j + 8] * вес_единицы`). Всё остальное он копирует
	//   как есть (`:107853-107854`) и ретранслирует пакетом 9905 дальше
	//   (`:107965 sub_5646C0(..., 9905, ...)`) — то есть перепутанные байты уезжали в
	//   ретранслятор и в БД, а не отсекались проверками.
	TArray<TArray<uint8>> Rows;
	for (const FAsdaExchangeListing& L : Listings)
	{
		if (L.IsEmpty() || !L.bChecked) { continue; }
		Rows.Add(BuildBuyRow9905(L));   // ★ U6: запись — сборщиком ниже
	}
	if (Rows.Num() == 0)
	{
		// Ничего не отмечено — эталон только показывает МОДАЛЬНОЕ ОК с нотисом 12545.
		NoticeModal(kLocBuyBlocked);
		return;
	}
	Net->SendExchangeBuy9905(Rows);
}

// ★ U6 (В1 дедупа №2): запись через `FAsdaPacketWriter` (байты те же, что у прежнего ручного буфера). Раскладка — таблица в `SendBuyChecked` выше.
TArray<uint8> UAsdaExchangeSubsystem::BuildBuyRow9905(const FAsdaExchangeListing& L)
{
	FAsdaPacketWriter W(45);
	W.I32(0, L.Serial);       // серийный номер лота
	W.I32(4, L.ItemCode);
	// ★★★ +8 i32 — КОЛИЧЕСТВО В ЛОТЕ. Раньше порт слал сюда НОЛЬ (аудит 2026-09-02, К-7б).
	//   ЭТАЛОН: `itemRecordBuf[2] = itemField1` (`shop_trade.c:15082`), где
	//   `itemField1 = *(_DWORD *)&self->gap_20AC[116*slot + 5]` (`:15054`) — поле **+9**
	//   сохранённой записи выдачи (0x20AC + 5 = база 0x20A8 + 9). А туда приёмник 9904
	//   `netOnItemSlotListResult@0xD5BAB0` (`network.c:21568`) кладёт
	//   `*(_DWORD *)((char *)&recordBuf[20*i + 2] + 1)` = байт **+9** записи пакета, то есть
	//   ровно `L.Amount` (порт читает его там же, `HandleTradeResult`).
	//   СЕРВЕР ЭТО ПОЛЕ ЧИТАЕТ: `Recv_TransAction_BuyItemReq@0x51AA10`
	//   (`GameServer.c:107926`) —
	//     `v30 += *(_WORD *)&Src[45*j + 8] * *(_WORD *)(dword_5F5E98 + 71*(code−20551) + 5);`
	//   это накопитель ВЕСА покупки, который дальше сверяется с пределом персонажа
	//   (`:107943-107946`). С нулём вес стопочных лотов считался как 0, а сама запись
	//   уходила ретранслятору с нулевым количеством.
	//   ⚠ ПРАВКА МЕНЯЕТ СОДЕРЖИМОЕ ПАКЕТА К СТАРОМУ СЕРВЕРУ — допущена только потому, что
	//   доказана по серверному коду. Требует проверки в игре.
	W.I32(8, L.Amount);
	// ★★★ ИСПРАВЛЕНО 2026-09-02: байты +12 и +13 были ПЕРЕПУТАНЫ. Порт слал
	//   `+12 = количество (обрезанное до 255)` и `+13 = прочность`; эталон
	//   (`shop_trade.c:15083-15084`) шлёт `+12 = прочность`, `+13 = уровень заточки`.
	//   Количество эталон отправляет ТОЛЬКО в +8 (см. выше) — дублировать его в +12 неоткуда:
	//   в строке лота +12 берётся из `gap_20AC[116*slot + 9]`, а количество лежит в
	//   `gap_20AC[116*slot + 5]`. Смещения и цепочку «9904 → строка лота → 9905» см. в
	//   таблице в шапке функции.
	//   ⚠ ПРАВКА МЕНЯЕТ СОДЕРЖИМОЕ ПАКЕТА К СТАРОМУ СЕРВЕРУ. Допущена потому, что ВОЗВРАЩАЕТ
	//   к эталону, а сервер оба байта не проверяет вовсе и лишь ретранслирует запись
	//   (`GameServer.c:107853-107854`, `:107965`) — то есть принять её он обязан.
	W.U8(12, L.Item.Durability);                        // ★ +12 — ПРОЧНОСТЬ (9904 +13)
	W.U8(13, L.Item.GemGrade);                          // ★ +13 — УРОВЕНЬ ЗАТОЧКИ (9904 +14)
	// +14…+23 эталон не заполняет — оставляем нули (тело писателя обнулено).
	// +24…+43 — пять пар заточки, байт-в-байт из полей +25…+44 записи 9904.
	for (int32 S = 0; S < 5; ++S)
	{
		const int32 V = L.EnchantOpts.IsValidIndex(S) ? L.EnchantOpts[S] : 0;
		W.I32(24 + S * 4, V);
	}
	// +44 эталон не заполняет — остаётся нулём.
	return W.ToArray();
}

// ═══════════════ ★★★ P1: ТРИ СПИСКА КРИТЕРИЕВ 13410 / 13411 / 13412 ══════════════════════════
//
// ЧТО ЭТО ЗА СПИСКИ. Их состав ЗАВИСИТ ОТ КАТЕГОРИИ, выбранной в наборе кнопок
//   (`EXCHANGER_RADIOBTN2` — 7 пунктов в режиме 4, `EXCHANGER_RADIOBTN1` — 8 в режиме 5), а сами
//   подписи фильтров 13487/13488/13489 при этом ПЕРЕПИСЫВАЮТСЯ. Статические подписи `.JUI`
//   (`종류` вид / `직업` класс / `레벨` уровень) — только заготовка для конструктора экрана.
//   Всё, что ниже, снято с `uiItemDlg_fillAttributeLists@0xD59C80` (`ui_core.c:52834-53225`):
//   каждый пункт — ЛОКАЛИЗОВАННАЯ строка по id нотиса, ничего не придумано.
//
//   РЕЖИМ 4 (`searchCritSel2`, набор RADIOBTN2, категории 0…6)
//     0, 1 → 13410: 11 пунктов = 12775 «Все» + 12729…12738 («1 ~ 10» … «91 ~ 100»); подпись 2330;
//     2    → 13410: 2 = 8584 «Живой камень», 8585 «Составной ЖК» (★ БЕЗ пункта «Все»);
//                   13411: 6 = «Все» + 12756…12760 (Отвага/Концентрация/Мудрость/Спокойствие/Защита);
//                   13412: 11 уровней; подписи 12728 / 9564 / 2330;
//     3, 4, 5 → списков НЕТ (все три контрола гасятся);
//     6    → 13410: 10 = «Все» + 12747…12755 (Камень/Дерево/Металл/Кожа/Ткань/Кованое изделие/
//                   Запчасть/Тип карточки/Разное); подпись 12727.
//   РЕЖИМ 5 (`searchCritSel1`, набор RADIOBTN1, категории 0…7)
//     0    → 13410: 9 = 12739…12746 (Короткий меч/Меч/Копье/Двуручный меч/Арбалет/Лук/Баллиста/
//                   Посох) + ★ 8560 «Короткий лук» — ЗДЕСЬ ПУНКТА «Все» НЕТ;
//                   13411: 11 уровней; подписи 9564 / 2330;
//     1    → 13410: 1 = 6846 «Щит»; 13411: 11 уровней; подписи 9564 / 2330;
//     2…6  → 13410: 10 = 12620…12629 (Новичок/Солдат/Рыцарь/Элитный боец/Лучник/Стрелок/Опытный
//                   лучник/Маг/Волшебник/Архимаг) — БЕЗ «Все»;
//                   13411: 3 = «Все» + 12602 «М» + 12603 «Ж»; 13412: 11 уровней;
//                   подписи 2297 «Профессия» / 11016 «Пол» / 2330 «Уровень»;
//     7    → 13410: 11 уровней (★ эталон печатает их в буфер третьего списка, но рисует в ПЕРВЫЙ
//                   контрол и считает `list1RowCount` — переиспользование буфера, не ошибка);
//                   подпись 2330.
//   ★ ДЕФЕКТ ОРИГИНАЛА (мёртвая ветка): в режиме 5, категориях 2…6 после десятого пункта
//     `noticeId` переустанавливается в 12596 («1-я работа»), но цикл к этому моменту уже
//     закончился — значение не используется никогда.
void UAsdaExchangeSubsystem::FillAttributeLists(int32 InMode, int32 Cat)
{
	if (InMode <= 0) { return; }                    // `if (itemMajorCategory <= 0) return;`
	UGameInstance* GI = GetGameInstance();
	auto L = [GI](int32 Id) { return AsdaUi::LocRaw(GI, Id, /*bDebugFallback*/ true); };

	TArray<FString> L1, L2, L3;
	int32 Cap1 = 0, Cap2 = 0, Cap3 = 0;             // нотисы подписей 13487 / 13488 / 13489

	// 11 пунктов «Все» + десять диапазонов уровня — повторяются в шести ветках эталона.
	auto AddLevels = [&L](TArray<FString>& Dst)
	{
		Dst.Add(L(12775));
		for (int32 I = 0; I < 10; ++I) { Dst.Add(L(12729 + I)); }
	};

	if (InMode == 4)
	{
		switch (Cat)
		{
			case 0:
			case 1: AddLevels(L1); Cap1 = 2330; break;
			case 2:
				L1.Add(L(8584)); L1.Add(L(8585));
				L2.Add(L(12775));
				for (int32 I = 0; I < 5; ++I) { L2.Add(L(12756 + I)); }
				AddLevels(L3);
				Cap1 = 12728; Cap2 = 9564; Cap3 = 2330;
				break;
			case 3: case 4: case 5: break;          // `list1RowCount = list2RowCount = list3RowCount = 0`
			case 6:
				L1.Add(L(12775));
				for (int32 I = 0; I < 9; ++I) { L1.Add(L(12747 + I)); }
				Cap1 = 12727;
				break;
			default: break;
		}
	}
	else if (InMode == 5)
	{
		switch (Cat)
		{
			case 0:
				for (int32 I = 0; I < 8; ++I) { L1.Add(L(12739 + I)); }
				L1.Add(L(8560));                    // ★ девятый пункт — 8560, а не 12747
				AddLevels(L2);
				Cap1 = 9564; Cap2 = 2330;
				break;
			case 1:
				L1.Add(L(6846));
				AddLevels(L2);
				Cap1 = 9564; Cap2 = 2330;
				break;
			case 2: case 3: case 4: case 5: case 6:
				for (int32 I = 0; I < 10; ++I) { L1.Add(L(12620 + I)); }
				L2.Add(L(12775)); L2.Add(L(12602)); L2.Add(L(12603));
				AddLevels(L3);
				Cap1 = 2297; Cap2 = 11016; Cap3 = 2330;
				break;
			case 7: AddLevels(L1); Cap1 = 2330; break;
			default: break;
		}
	}

	// Заполнение и хвост эталона: пустой список ⇒ контрол гасится.
	auto Apply = [this, &L](UAsdaJuiComboBox* C, const TArray<TObjectPtr<UWidget>>& Grp,
		UTextBlock* Caption, const TArray<FString>& Items, int32 CapLoc)
	{
		if (C)
		{
			C->SetOpen(false);
			C->SetItems(Items);
		}
		SetGroupVisible(Grp, Items.Num() > 0);
		if (Caption)
		{
			Caption->SetText(CapLoc ? FText::FromString(L(CapLoc)) : FText::GetEmpty());
		}
	};
	Apply(CmbKind,  GrpFilter1, TxtFilter1, L1, Cap1);
	Apply(CmbClass, GrpFilter2, TxtFilter2, L2, Cap2);
	Apply(CmbLevel, GrpFilter3, TxtFilter3, L3, Cap3);

	// ★ Выбор по умолчанию задаёт ОТДЕЛЬНАЯ функция (миснейм `render_SetTexCoordSignFlags`).
	ApplyDefaultFilterSelection(InMode, Cat);
	if (CmbKind)  { CmbKind->SetSelectedIndex(SearchParamGrade); }
	if (CmbClass) { CmbClass->SetSelectedIndex(SearchParamMinLevel); }
	if (CmbLevel) { CmbLevel->SetSelectedIndex(SearchParamAlt); }

	UE_LOG(LogAsdaExchange, Verbose,
		TEXT("аукцион: фильтры режима %d, категория %d → пунктов %d/%d/%d, подписи %d/%d/%d"),
		InMode, Cat, L1.Num(), L2.Num(), L3.Num(), Cap1, Cap2, Cap3);
}

// ★★★ МИСНЕЙМ: `render_SetTexCoordSignFlags@0xD5C560` (`render_core.c:5532-5606`) к рендеру
//   отношения не имеет — это ВЫБОР ПО УМОЛЧАНИЮ трёх фильтров сразу после заполнения списков:
//   существующему списку ставится пункт **0**, отсутствующему — **−1**. Именно отсюда берётся
//   значение, которое эталонный `CControl_SetSelectedItem` кладёт в контрол (в декомпиле оно
//   выглядит «неинициализированным локалом» — на деле его пишет эта функция).
void UAsdaExchangeSubsystem::ApplyDefaultFilterSelection(int32 InMode, int32 Cat)
{
	if (InMode <= 0) { return; }
	int32 A = -1, B = -1, C = -1;
	if (InMode == 4)
	{
		switch (Cat)
		{
			case 0: case 1: A = 0;              break;
			case 2:         A = 0; B = 0; C = 0; break;
			case 3: case 4: case 5:             break;   // все три −1
			case 6:         A = 0;              break;
			default: return;                             // прочие категории состояние НЕ трогают
		}
	}
	else if (InMode == 5)
	{
		switch (Cat)
		{
			case 0: case 1: A = 0; B = 0;        break;
			case 2: case 3: case 4: case 5: case 6: A = 0; B = 0; C = 0; break;
			case 7:         A = 0;               break;
			default: return;
		}
	}
	else { return; }
	SearchParamGrade    = A;
	SearchParamMinLevel = B;
	SearchParamAlt      = C;
}

// `uiItemDlgSelChangeNeedsRefresh@0xD5CB10` (`ui_core.c:53318-53350`): в режиме 5 переход внутри
//   одной ГРУППЫ категорий (0 ↔ 0, 1 ↔ 1, 2…6 ↔ 2…6) списков не меняет, поэтому их не перезаливают.
bool UAsdaExchangeSubsystem::SelChangeNeedsRefresh(int32 InMode, int32 Prev, int32 Now)
{
	if (InMode == 0) { return false; }
	if (Now < 0)     { return false; }
	if (InMode != 5) { return true; }
	switch (Now)
	{
		case 0: return Prev != 0;
		case 1: return Prev != 1;
		case 2: case 3: case 4: case 5: case 6: return (Prev < 2 || Prev > 6);
		default: return true;
	}
}

// `satisfyWindowResetSlots@0xD5ADA0` (`ui_core.c:53232-53266`): сброс восьми строк выдачи и
//   пагинации; при `bResetFilters` дополнительно сбрасываются ТРИ выбора фильтров в −1.
//   ★ Особенность эталона: ветка `a2 == 4` обнуляет поле 2075 (категорию режима 5) и наоборот —
//   воспроизводим как есть.
void UAsdaExchangeSubsystem::ResetSearchSlots(int32 InMode, bool bResetFilters)
{
	if (InMode == 4)      { SearchCritSel1 = 0; }
	else if (InMode == 5) { SearchCritSel2 = 0; }
	if (bResetFilters)
	{
		SearchParamGrade = SearchParamMinLevel = SearchParamAlt = -1;
	}
	for (FAsdaExchangeListing& L : Listings) { L = FAsdaExchangeListing(); }
	PageCur = 0; PageLast = 0; ScrollRowLot = 0;
}

// `uiItemDlgOnSelChange@0xD59B20` (`ui_core.c:52800-52832`) — клик по пункту набора категорий.
void UAsdaExchangeSubsystem::OnCategoryPicked(int32 InMode, int32 Cat)
{
	// Смена фильтра работает только в режимах 4/5 (`entity_char.c:45947-45951`).
	if (InMode == 4 && Mode != EAsdaExchangeMode::BrowseA) { return; }
	if (InMode == 5 && Mode != EAsdaExchangeMode::BrowseB) { return; }

	if (InMode == 4)
	{
		SearchCritSel2 = Cat;
		// ★ Подсветку активной вкладки обновляем ДО раннего выхода: игрок должен видеть, что
		//   кнопка нажалась, даже если категория та же и перезаполнять списки не надо.
		RefreshCategoryCaptions();
		if (SearchCritSel2 == PrevCritSel2) { return; }
		PrevCritSel2 = SearchCritSel2;
		ResetSearchSlots(4, /*bResetFilters*/ true);
		FillAttributeLists(4, Cat);
	}
	else if (InMode == 5)
	{
		SearchCritSel1 = Cat;
		RefreshCategoryCaptions();
		if (SearchCritSel1 == PrevCritSel1) { return; }
		const bool bRefresh = SelChangeNeedsRefresh(5, PrevCritSel1, Cat);
		PrevCritSel1 = SearchCritSel1;
		ResetSearchSlots(5, bRefresh);
		if (bRefresh) { FillAttributeLists(5, Cat); }
	}
	else { return; }
	RefreshListings();
}

// `charAppearanceSetPartIndex@0xD5AF20` (★ миснейм; `entity_char.c:45956-46135`) — запись выбора
//   пункта в одно из трёх полей С КЛАМПОМ по длине списка ЭТОЙ категории. Вне длины пишется −1
//   («не выбрано»), и это НЕ ноль: ноль здесь валидный пункт (закон `helper_initInvalidIds`).
void UAsdaExchangeSubsystem::OnFilterPicked(int32 Which, int32 Idx)
{
	const int32 InMode = (Mode == EAsdaExchangeMode::BrowseA) ? 4
		: (Mode == EAsdaExchangeMode::BrowseB) ? 5 : 0;
	if (InMode <= 0 || Idx < 0) { return; }
	const int32 Cat = (InMode == 4) ? SearchCritSel2 : SearchCritSel1;

	// Длина списка `Which` при категории `Cat` — ровно та, что построил `FillAttributeLists`.
	auto Limit = [InMode, Cat](int32 W) -> int32
	{
		if (InMode == 4)
		{
			if (W == 0)
			{
				if (Cat <= 1)           { return 11; }
				if (Cat == 2)           { return 2;  }
				if (Cat == 6)           { return 10; }
				return 0;
			}
			if (W == 1) { return (Cat == 2) ? 6  : 0; }
			return          (Cat == 2) ? 11 : 0;
		}
		// режим 5
		if (W == 0)
		{
			if (Cat == 0)               { return 9;  }
			if (Cat == 1)               { return 1;  }
			if (Cat >= 2 && Cat <= 6)   { return 10; }
			if (Cat == 7)               { return 11; }
			return 0;
		}
		if (W == 1)
		{
			if (Cat <= 1)               { return 11; }
			if (Cat >= 2 && Cat <= 6)   { return 3;  }
			return 0;
		}
		return (Cat >= 2 && Cat <= 6) ? 11 : 0;
	};

	const int32 Max = Limit(Which);
	const int32 Val = (Max > 0 && Idx < Max) ? Idx : -1;
	if (Which == 0)      { SearchParamGrade    = Val; }
	else if (Which == 1) { SearchParamMinLevel = Val; }
	else                 { SearchParamAlt      = Val; }

	// Эталонный поток: смена фильтра сбрасывает страницу и повторяет поиск.
	PageCur = 0; PageLast = 0; ScrollRowLot = 0;
	SendSearch();
}

// `charGetEquipSlotCode@0xD5B8F0` (`entity_char.c:46138-46180`): пункт комбобокса 13410 → КОД
//   СЛОТА ЭКИПИРОВКИ для `criteria[2]`. Таблица снята один в один; вне её — −1.
int32 UAsdaExchangeSubsystem::EquipSlotCodeOf(int32 Sel)
{
	static const int32 kCode[13] = { 0, 1, 2, 3, 12, 13, 14, 23, 24, 25, 40, 41, 42 };
	return (Sel >= 0 && Sel < 13) ? kCode[Sel] : -1;
}

// `charGetVehicleStateCode@0xD5BA30` (тунк `sub_92DA0A@0x92DA0A`): пункт 13411 → `criteria[4]`.
//   В режиме 4 всегда 255; в режиме 5 значения 0/1/2 проходят как есть, прочие дают 255.
int32 UAsdaExchangeSubsystem::VehicleStateCode() const
{
	if (Mode == EAsdaExchangeMode::BrowseA) { return 255; }
	switch (SearchParamMinLevel)
	{
		case 0: return 0;
		case 1: return 1;
		case 2: return 2;
		default: return 255;
	}
}

// ★★★ ВОСЕМЬ БАЙТ КРИТЕРИЕВ 9903 — `mediationSendSearch@0xD55BB0` (`shop_trade.c:14663-14840`).
//   Буфер инициализируется НУЛЯМИ (`sub_D568D0@0xD568D0` зануляет байты 0…6; байт 7 всегда
//   перезаписывается номером страницы), поэтому здесь ноль — законное «поле не задано»,
//   а «фильтр не выбран» кодируется значением **100**.
void UAsdaExchangeSubsystem::BuildSearchCriteria(uint8 Out[8]) const
{
	for (int32 I = 0; I < 7; ++I) { Out[I] = 0; }

	auto Or100 = [](int32 V) -> uint8 { return (uint8)((V > 0) ? V : 100); };

	if (Mode == EAsdaExchangeMode::BrowseA)                 // ветка `a2 == 0`, режим 4
	{
		switch (SearchCritSel2)
		{
			case 0: Out[0] = 5;  Out[1] = 0; Out[5] = 0; Out[3] = Or100(SearchParamGrade); break;
			case 1: Out[0] = 7;  Out[1] = 0; Out[5] = 0; Out[3] = Or100(SearchParamGrade); break;
			case 2:
				Out[0] = 11; Out[1] = 6;
				Out[6] = (uint8)FMath::Max(0, SearchParamGrade);
				if (SearchParamGrade == 1)
				{
					// ★ Единственное место, где к уровню прибавляется 5 (`shop_trade.c:14793-14800`).
					Out[5] = (uint8)((SearchParamMinLevel > 0) ? (SearchParamMinLevel + 5) : 100);
				}
				else if (SearchParamGrade == 0)
				{
					Out[5] = Or100(SearchParamMinLevel);
				}
				Out[3] = Or100(SearchParamAlt);
				break;
			case 3: Out[0] = 11; Out[1] = 2; break;
			case 4: Out[0] = 11; Out[1] = 4; break;
			case 5: Out[0] = 11; Out[1] = 1; break;
			case 6: Out[0] = 11; Out[1] = 3; Out[5] = 0; Out[3] = Or100(SearchParamGrade); break;
			default: break;
		}
	}
	else if (Mode == EAsdaExchangeMode::BrowseB)            // ветка `a2 == 1`, режим 5
	{
		const int32 SlotIdx  = EquipSlotCodeOf(SearchParamGrade);
		const int32 GradeVal = VehicleStateCode();
		switch (SearchCritSel1)
		{
			// `strcpy(criteria, "\t")` — это байт 9 и завершающий ноль, а не строка.
			case 0: Out[0] = 9; Out[1] = 0; Out[5] = (uint8)FMath::Max(0, SearchParamGrade);
					Out[3] = Or100(SearchParamMinLevel); break;
			case 1: Out[0] = 8; Out[1] = 0; Out[5] = (uint8)FMath::Max(0, SearchParamGrade);
					Out[3] = Or100(SearchParamMinLevel); break;
			case 2: case 3: case 4: case 5: case 6:
			{
				static const uint8 kKind[5] = { 0, 1, 2, 4, 3 };   // категории 2,3,4,5,6
				Out[0] = kKind[SearchCritSel1 - 2];
				Out[1] = 0;
				Out[2] = (uint8)FMath::Max(0, SlotIdx);
				Out[3] = Or100(SearchParamAlt);
				Out[4] = (uint8)((GradeVal != 0) ? GradeVal : 100);
				break;
			}
			case 7: Out[0] = 12; Out[1] = 0; Out[3] = Or100(SearchParamGrade); break;
			default: break;
		}
	}
	// ★ `criteria[7] = self[1].aListingRowsCont[84]` — то же поле `+2318`, что и текущая страница
	//   (арифметика `0x20BC + 0x328 + 84 = 9272 = 2318 × 4`).
	Out[7] = (uint8)FMath::Clamp(PageCur, 0, 255);
}

// ═══════════════ ★★★ P3: ТРИ ПОЛОСЫ ПРОКРУТКИ (общая точка `FAsdaScrollBarNomal`) ════════════
void UAsdaExchangeSubsystem::BuildScrollBars()
{
	if (!Widget || !Widget->WidgetTree) { return; }
	UCanvasPanel* Root = Cast<UCanvasPanel>(Widget->WidgetTree->RootWidget);
	if (!Root) { return; }
	ScrollSell.Build(Widget, Root, TEXT("EXCHANGE_SCROLL"),  /*ZTrack*/ 20);
	ScrollMy  .Build(Widget, Root, TEXT("EXCHMY_SCROLL"),    /*ZTrack*/ 20);
	ScrollLot .Build(Widget, Root, TEXT("EXCHANGER_SCROLL"), /*ZTrack*/ 20);
	UE_LOG(LogAsdaExchange, Log, TEXT("аукцион: полосы прокрутки 13125/13249/13470 = %s/%s/%s"),
		ScrollSell.IsBuilt() ? TEXT("да") : TEXT("НЕТ"),
		ScrollMy.IsBuilt()   ? TEXT("да") : TEXT("НЕТ"),
		ScrollLot.IsBuilt()  ? TEXT("да") : TEXT("НЕТ"));
}

int32 UAsdaExchangeSubsystem::GetSellMaxScrollRow() const
{
	const int32 Rows = FMath::DivideAndRoundUp(SellCells.Num(), kSellCols);
	return FMath::Max(0, Rows - kSellVisRows);
}

int32 UAsdaExchangeSubsystem::GetMyMaxScrollRow() const
{
	int32 N = 0;
	for (const FAsdaExchangeMyLot& L : MyLots) { if (!L.IsEmpty()) { ++N; } }
	return FMath::Max(0, N - kMyRows);
}

int32 UAsdaExchangeSubsystem::GetLotMaxScrollRow() const
{
	int32 N = 0;
	for (const FAsdaExchangeListing& L : Listings) { if (!L.IsEmpty()) { ++N; } }
	return FMath::Max(0, N - kLotRows);
}

void UAsdaExchangeSubsystem::LayoutScrollBars()
{
	// Полоса стоит у ПРАВОГО края своего «колёсного окна». Самих узлов 13125/13249/13470 в WBP
	//   нет (`NoSelect = "1"` + невидимый план скина), поэтому левый-верх считаем от ЖИВОЙ
	//   геометрии первой ячейки списка плюс дельта из данных `.JUI` (константы вверху файла).
	auto Place = [](FAsdaScrollBarNomal& Bar, const UWidget* Anchor, bool bWant,
		float DX, float DY, float W, float H, int32 VisRows, int32 TotalRows, int32 Row)
	{
		if (!Bar.IsBuilt()) { return; }
		if (!bWant || !Anchor) { Bar.Hide(); return; }
		const FVector2D Origin = AsdaUi::CanvasRootPos(Anchor) + FVector2D(DX + W, DY);
		const int32 MaxRow = FMath::Max(0, TotalRows - VisRows);
		// ★ Волна 14 (МЕЛК-1): доля — целыми аргументами эталона (видимые, всего), длину считает `EtalonThumbLen`.
		Bar.Layout(Origin, H, /*Scale*/ 1.f, VisRows, TotalRows, MaxRow, Row);
	};

	const bool bReg    = (Mode == EAsdaExchangeMode::RegisterSpend || Mode == EAsdaExchangeMode::RegisterShell);
	const bool bMy     = (Mode == EAsdaExchangeMode::MyLots);
	const bool bBrowse = (Mode == EAsdaExchangeMode::BrowseA || Mode == EAsdaExchangeMode::BrowseB);

	int32 MyN = 0;  for (const FAsdaExchangeMyLot& L : MyLots)     { if (!L.IsEmpty()) { ++MyN; } }
	int32 LotN = 0; for (const FAsdaExchangeListing& L : Listings) { if (!L.IsEmpty()) { ++LotN; } }

	Place(ScrollSell, SellIcons.IsValidIndex(0) ? SellIcons[0].Get() : nullptr, bReg,
		0.f, 0.f, kSellListW, kSellListH,
		kSellVisRows, FMath::DivideAndRoundUp(SellCells.Num(), kSellCols), ScrollRowSell);
	Place(ScrollMy, MyIcons.IsValidIndex(0) ? MyIcons[0].Get() : nullptr, bMy,
		kMyListDX, kMyListDY, kMyListW, kMyListH, kMyRows, MyN, ScrollRowMy);
	Place(ScrollLot, LotIcons.IsValidIndex(0) ? LotIcons[0].Get() : nullptr, bBrowse,
		kLotListDX, kLotListDY, kLotListW, kLotListH, kLotRows, LotN, ScrollRowLot);
}

void UAsdaExchangeSubsystem::TickScrollBars()
{
	auto Run = [this](FAsdaScrollBarNomal& Bar, int32 MaxRow, int32& Row, TFunction<void()> OnChanged)
	{
		if (!Bar.IsBuilt()) { return; }
		int32 Value = 0;
		const int32 Was = Row;
		switch (Bar.Tick(Mouse.bLmb, Mouse.bLmbDown, MaxRow, Value))
		{
			case EAsdaScrollAction::ScrollBy: Row = FMath::Clamp(Row + Value, 0, MaxRow); break;
			case EAsdaScrollAction::SetRow:   Row = FMath::Clamp(Value, 0, MaxRow);       break;
			default: break;
		}
		if (Row != Was) { OnChanged(); }
	};
	Run(ScrollSell, GetSellMaxScrollRow(), ScrollRowSell, [this] { RebuildSellGrid(); });
	Run(ScrollMy,   GetMyMaxScrollRow(),   ScrollRowMy,   [this] { RefreshMyLots(); });
	Run(ScrollLot,  GetLotMaxScrollRow(),  ScrollRowLot,  [this] { RefreshListings(); });
}
