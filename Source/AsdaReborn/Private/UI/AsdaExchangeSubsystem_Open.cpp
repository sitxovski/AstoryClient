// Copyright (c) 2026 @sitxovski (Discord)
// AsdaExchangeSubsystem_Open.cpp — ★ R2 10.11.2026 (разрез AsdaExchangeSubsystem.cpp, дословный перенос): время, гейты открытия, золото продажи, режимы/панели.
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
#include "Npc/AsdaNpcDialogSubsystem.h"    // GetCurrentNpcId — NPC-аукционщик в шапке 99xx (+10)
#include "UI/AsdaIconCellGrid.h"
#include "UI/AsdaInventorySubsystem.h"
#include "UI/AsdaJuiComboBox.h"
// имя предмета и подстановка аргументов нотиса   ★ R3: комментарий снятого включения "Text/AsdaTextSubsystem.h" (файл им не пользовался) — сохранён
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

// ★★★ ГЕЙТ УРОВНЯ НА РЕГИСТРАЦИЮ (режимы 1 и 2) — ЭТО РАСШИРЕНИЕ, А НЕ ЭТАЛОН.
//   ТРЕБОВАНИЕ ВЛАДЕЛЬЦА 2026-08-28: «если у игры ограничение на уровень, нужно такое же окно
//   выводить и на регистрацию обеих категорий».
//   ⚠ В ОРИГИНАЛЕ ТАКОГО ГЕЙТА НЕТ, и это проверено: `itemMediationOpenTab@0xD42E60` имеет ровно
//   пять гейтов (§1.2) — уровня среди них НЕ БЫЛО; порог 24 в аукционе встречается всего дважды —
//   в фильтре сетки выставления (`invFilterUsableItems@0xD45520`, ветка `categoryFlag == 3`) и в
//   модалке 97101 приёмника 9908 (`boosterShopWnd_OnItemListPacket`, `shop_trade.c:12866-12882`).
//   Поэтому — с ключом отката, по умолчанию ВКЛЮЧЕНО.
static ASDA_DEV_CVAR(int32, CVarExchangeRegLevelGate,
	TEXT("asda.Exchange.RegLevelGate"), 1,
	TEXT("Аукцион: 1 — модалка 97101 при уровне < 24 перед открытием режимов 1/2 (расширение, "
	     "требование владельца); 0 — как в оригинале, гейта нет."),
	ECVF_Default);

namespace {
	constexpr int32 kLocRegOpened     = 12687;
	constexpr int32 kLocRegClosed     = 12688;
	constexpr int32 kLocMyClosed      = 12690;
	constexpr int32 kLocBrowseOpenedA = 12691;
	constexpr int32 kLocBrowseOpenedB = 12692;
	constexpr int32 kLocBrowseClosed  = 12693;
}

// ★ ОСТАТОК ВРЕМЕНИ ТОРГОВ — точная копия `tagBoosterShop_FormatSlotRemainTime@0xD3E620`
//   (`shop_trade.c:13497-13546`). Вход — МИЛЛИСЕКУНДЫ.
//     `a3 == 0`                       → нотис **12697** (строка «срока нет»);
//     `a3 >= 3600000`                 → «<часы> <нотис 12535>»;
//     остаток `>= 60000`              → «… <минуты> <нотис 12536>»;
//     ни часов, ни минут и `a3 < 60000` → нотис **12698**.
//   Своих слов не добавляем: и «ч», и «мин» — из таблицы текста.
FString UAsdaExchangeSubsystem::FormatRemainTime(int32 RemainMs) const
{
	UGameInstance* GI = GetGameInstance();
	if (RemainMs == 0) { return AsdaUi::LocRaw(GI, 12697, true); }

	const int32 Hours   = RemainMs / 3600000;
	const int32 RestMs  = RemainMs % 3600000;
	FString HourStr, MinStr;
	if (RemainMs >= 3600000)
	{
		HourStr = FString::Printf(TEXT("%d %s"), Hours, *AsdaUi::LocRaw(GI, 12535, true));
	}
	if (RestMs >= 60000)
	{
		MinStr = FString::Printf(TEXT("%d %s"), RestMs / 60000, *AsdaUi::LocRaw(GI, 12536, true));
	}
	if (!HourStr.IsEmpty() && !MinStr.IsEmpty()) { return HourStr + TEXT(" ") + MinStr; }
	if (!HourStr.IsEmpty()) { return HourStr; }
	if (!MinStr.IsEmpty())  { return MinStr; }
	if (RemainMs < 60000)   { return AsdaUi::LocRaw(GI, 12698, true); }
	return FString();
}

// ═══════════════════════ режимы и гейты ═══════════════════════════════════════════════════════
bool UAsdaExchangeSubsystem::PassesOpenGates(int32 NewMode) const
{
	// 4. `if (a2 <= 0) return 0;`
	if (NewMode <= 0 || NewMode > 5) { return false; }

	// 2. ★ 29.09.2026 (волна 0 торговли): `g_bPartyInviteFlag` — МИСНЕЙМ, это не «приглашение в группу», а
	//    `CExchange+0x2C` = ОКНО обмена 2008 открыто (item_inventory.c:46764) ⇒ МОЛЧАЛИВЫЙ отказ. Общая точка
	//    `UAsdaTradeStateSubsystem::IsExchangeWindowOpen` (пока окна 2008 нет — false).
	//    3. Инвентарь занят (`get_o84_7520`, dword +0x210) ⇒ МОЛЧАЛИВЫЙ отказ.
	//    [ПОРТ] роль гейта 3 играет «открыт модальный диалог»: пока он на экране, окно не всплывает.
	UGameInstance* GI = GetGameInstance();
	if (UAsdaTradeStateSubsystem::IsExchangeWindowOpen(GI))
	{
		UE_LOG(LogAsdaExchange, Log, TEXT("аукцион: режим %d не открыт молча — открыто окно обмена 2008 (item_inventory.c:46764)"), NewMode);
		return false;
	}
	if (const UAsdaWindowSubsystem* WM = GI ? GI->GetSubsystem<UAsdaWindowSubsystem>() : nullptr)
	{
		if (WM->IsAnyModalOpen()) { return false; }
	}
	// ★★★ ПАЧКА 20, жалоба 10 — [ИСПРАВЛЕНИЕ], обратная сторона взаимоисключения: рынок не открывается поверх склада 4000
	//   и личной лавки (22000/23000). Молча, как гейт окна обмена выше (своей строки у оригинала нет). Ключ
	//   `asda.Exchange.ExclusiveWindows` 0 — гейта нет, как в оригинале.
	if (ExclusiveWindowsOn())
	{
		const UAsdaWarehouseSubsystem* Ware = GI ? GI->GetSubsystem<UAsdaWarehouseSubsystem>() : nullptr;
		if (Ware && Ware->IsOpen())
		{
			UE_LOG(LogAsdaExchange, Log, TEXT("аукцион: режим %d не открыт молча — открыт склад 4000 (пачка 20, жалоба 10)"), NewMode);
			return false;
		}
		if (UAsdaTradeStateSubsystem::IsSelfStoreOpen(GI))
		{
			UE_LOG(LogAsdaExchange, Log, TEXT("аукцион: режим %d не открыт молча — открыта личная лавка (пачка 20, жалоба 10)"), NewMode);
			return false;
		}
	}

	// 5. ★★★ ГЛАВНЫЙ ГЕЙТ — ЭТО «АУКЦИОН ЗАБЛОКИРОВАН», А НЕ «ЗАНЯТ ДРУГОЙ РЕЖИМ».
	//    Эталон: `if (!*(_DWORD *)&this->curCategory || a2 == 3)` (`item_inventory.c:46768`).
	//    ★ `curCategory` — это поле **+0x4C4** (`RE/ida/structs_ru.h:2112`), а ТЕКУЩИЙ РЕЖИМ
	//    лежит в ДРУГОМ поле — `gap_4C5[3]` = **+0x4C8** (`item_inventory.c:46770`). Это два
	//    разных байта, и порт их спутал: сюда попал гейт «уже активен другой режим», которого
	//    в оригинале НЕТ. +0x4C4 взводит ровно один приёмник — 9914 «аукцион заблокирован»
	//    (§7.5, `this[305] = 1`), и его же смысл несёт нотис 12555: «убедись, что в рюкзаке есть
	//    свободное место и вещи не валяются на земле». Отсюда и жалоба владельца: при попытке
	//    открыть второй режим летел ЧУЖОЙ нотис, а окно не переключалось.
	//    ⇒ Смена режима при открытом окне РАЗРЕШЕНА, и эталон её прямо обслуживает:
	//    `if (gap_4C5[3] && a2 != gap_4C5[3]) consignRegWndOpenByMode(this, gap_4C5[3]);`
	//    (`item_inventory.c:46770`) — старая панель переоткрывается, затем `uiHideThreeControls`.
	if (bAuctionBlocked && NewMode != 3)
	{
		NoticeChatAndTip(kLocBusyOther);
		return false;
	}
	return true;
}

// ★ 29.09.2026 (волна 0 торговли): вывод золота 9913, отложенного при открытом окне обмена — объявление и эталон в .h.
bool UAsdaExchangeSubsystem::FlushDeferredSaleGold(const TCHAR* Why)
{
	if (DeferredSaleGold <= 0) { return false; }
	const int32 Gold = DeferredSaleGold;
	DeferredSaleGold = 0;   // `sub_92104D(g_pItemShopWnd, 0)`
	// ★ 30.09.2026 (волна О4 торговли): эталон выводит сумму `uiSetHonorPointsDisplay(g_pGameClient, v)` (ui_core.c:35244) — это поле
	//   денег ИНВЕНТАРЯ (общая точка `SetGoldDisplay`), а не только надпись аукциона; раньше менялась лишь надпись 13475.
	if (UAsdaInventorySubsystem* Inv = GetGameInstance() ? GetGameInstance()->GetSubsystem<UAsdaInventorySubsystem>() : nullptr) { Inv->SetGoldDisplay(Gold); }
	if (TxtExchGold) { TxtExchGold->SetText(FText::FromString(AsdaUi::FormatThousands(Gold))); }
	UE_LOG(LogAsdaExchange, Log, TEXT("аукцион: отложенное золото %d выведено (%s)"), Gold, Why ? Why : TEXT("-"));
	return true;
}

// ★ 30.09.2026 (волна О4 торговли): обнуление отложенного золота без вывода — объявление и эталон в .h.
bool UAsdaExchangeSubsystem::ResetDeferredSaleGold(const TCHAR* Why)
{
	if (DeferredSaleGold <= 0) { return false; }
	UE_LOG(LogAsdaExchange, Log, TEXT("аукцион: отложенное золото %d обнулено без вывода (%s)"), DeferredSaleGold, Why ? Why : TEXT("-"));
	DeferredSaleGold = 0;   // `sub_92104D(g_pItemShopWnd, 0)`
	return true;
}

// ★★★ ЕДИНСТВЕННАЯ ТОЧКА ГЕЙТА УРОВНЯ «24» — ОДНО УСЛОВИЕ НА РЕЖИМЫ 1, 2 И 3.
//   Что здесь эталон, а что наше (важно не спутать при следующем заходе):
//     • режим 3 — ЭТАЛОННОЕ поведение по СМЫСЛУ (модалка 97101 при `level < 0x18`,
//       `boosterShopWnd_OnItemListPacket`, `shop_trade.c:12866-12882`), но эталон показывает её
//       ПОСЛЕ открытия окна и ТОЛЬКО получив 9908. Наше отличие: показываем ДО запроса и окна
//       не открываем — это исправление дефекта **D10**, откат `asda.Exchange.LegacyLevelGate 1`;
//     • режимы 1 и 2 — РАСШИРЕНИЕ, В ОРИГИНАЛЕ ТАКОГО ГЕЙТА НЕТ (`itemMediationOpenTab@0xD42E60`
//       имеет ровно пять гейтов, уровня среди них нет). Требование владельца, откат
//       `asda.Exchange.RegLevelGate 0`.
//   ★ Ноль от `SelfLevel()` — это «уровень НЕИЗВЕСТЕН», а не «нулевой уровень»: блокировать по
//     нему нельзя, иначе окно закроется всем подряд на ранних кадрах (закон `helper_initInvalidIds`).
bool UAsdaExchangeSubsystem::LevelGateBlocks(int32 NewMode) const
{
	if (NewMode != 1 && NewMode != 2 && NewMode != 3) { return false; }

	// Ключи отката: у режимов 1/2 — свой (расширение), у режима 3 — ключ дефекта D10.
	const bool bEnabled = (NewMode == 3)
		? (CVarExchangeLegacyLevelGate.GetValueOnGameThread() == 0)
		: (CVarExchangeRegLevelGate.GetValueOnGameThread() != 0);
	if (!bEnabled) { return false; }

	const int32 Lv = SelfLevel();
	return Lv > 0 && Lv < 24;
}

// ★★★ ПЕРЕСЧЁТ ФЛАГА «АУКЦИОН ЗАБЛОКИРОВАН» — исправление дефекта порта (аудит 2026-09-02, К-7а).
//   ЧТО ДЕЛАЛ ПОРТ: приём 9908 «поднимал» `bAuctionBlocked = true` и не снимал его НИКОГДА
//     (подсистема — `UGameInstanceSubsystem`, то есть до перезахода в игру).
//   ЧТО ДЕЛАЕТ ОРИГИНАЛ: флаг ПЕРЕСЧИТЫВАЕТСЯ при каждом входе в режим 3 и при его закрытии —
//     `itemMediation_onConfirmEvent@0xD3A0A0` (`item_inventory.c:45904-45907`):
//         `if ( playerInv_CountSowelEquipped(this) ) *(this + 305) = 1; else *(this + 305) = 0;`
//     Само `playerInv_CountSowelEquipped@0xD3F1D0` (`entity_char.c:45742-45766`) — цикл по ДЕСЯТИ
//     сохранённым записям «моих лотов»: лот «висит», если код предмета нашёлся ХОТЯ БЫ В ОДНОЙ
//     из трёх таблиц (item_list / sowell_list / special_itemlist) И поле **+46** записи равно 0.
//   ⚠ Совет аудитора «просто удалить строку 3180» был бы ошибкой: он выбросил бы воспроизведённое
//     поведение оригинала (висящий лот действительно должен запирать режимы 1/2/4/5).
//   Ключа отката нет намеренно: это возврат к поведению эталона, а не наша надстройка.
void UAsdaExchangeSubsystem::RecomputeAuctionBlocked()
{
	UGameInstance* GI = GetGameInstance();
	const UAsdaItemTableSubsystem* IT = GI ? GI->GetSubsystem<UAsdaItemTableSubsystem>() : nullptr;

	// `v5` эталона — «есть хоть один висящий лот»; `*(this + 8280)` — их счётчик. Счётчик в
	//   эталонном срезе никто не читает, поэтому держим его локально: он идёт только в лог.
	int32 Hanging = 0;
	if (IT)
	{
		for (int32 i = 0; i < kMyMaxRecs && MyLots.IsValidIndex(i); ++i)
		{
			const FAsdaExchangeMyLot& L = MyLots[i];
			// ★ Проверка «код известен таблицам» — ровно три вызова `CItemManager_Get*Record`
			//   эталона. В порте её не было вовсе: флаг взводился и по записи с неизвестным кодом.
			FAsdaSowellRow Sw;
			FAsdaSpecialItemlistRow Sp;
			const bool bKnown = (IT->GetItem(L.ItemCode) != nullptr)
				|| IT->GetSowell(L.ItemCode, Sw)
				|| IT->GetSpecialItem(L.ItemCode, Sp);
			if (bKnown && L.Field46 == 0) { ++Hanging; }
		}
	}

	const bool bNew = (Hanging > 0);
	if (bNew != bAuctionBlocked)
	{
		UE_LOG(LogAsdaExchange, Log,
			TEXT("аукцион: флаг блокировки %s → %s (висящих лотов: %d)"),
			bAuctionBlocked ? TEXT("ДА") : TEXT("нет"), bNew ? TEXT("ДА") : TEXT("нет"), Hanging);
	}
	bAuctionBlocked = bNew;
}

bool UAsdaExchangeSubsystem::OpenMode(int32 NewMode)
{
	// Гейт `sub_922402(g_pItemShopWnd) != N` из `game_main.c` — режим уже активен, ничего не делаем.
	//   ★ КРОМЕ РЕЖИМА 3: он не «режим окна», а ДЕЙСТВИЕ «запросить мои лоты» (§1), и повторный
	//   запрос обязан уходить. Иначе один неотвеченный 9907 запирает пункт меню навсегда.
	if ((int32)Mode == NewMode && NewMode != 3) { return false; }
	if (!PassesOpenGates(NewMode)) { return false; }

	// ★★★ ЕДИНСТВЕННАЯ ТОЧКА ГЕЙТА УРОВНЯ «24» — РЕЖИМЫ 1, 2 И 3 СРАЗУ (жалобы владельца
	//   2026-08-28 пп. 1 и 2). Стоит ДО `EnsureWidget()`, ДО `ShowPanel` и ДО отправки 9907,
	//   поэтому окно не открывается ВОВСЕ, а на экране остаётся одна модалка 97101.
	//   Почему до отправки: у режима 3 модалку раньше показывал ТОЛЬКО приёмник 9908
	//   (`HandleListingPacket`, дефект D10). Если сервер молчит или отвечает кодом, которого
	//   эталонная таблица не знает (у 9907 сервер сперва ищет NPC-аукционщика — `sub_4375E0`,
	//   `GameServer.c:107645`; не нашёл ⇒ статус 2, а `Loc9908` знает только 1 и 100), то
	//   приёмник не срабатывает НИКОГДА — владелец и видел «нажимаю, ничего не происходит».
	//   Гейт уровня от ответа сервера не зависит: порог 24 — чисто клиентский (в
	//   `Recv_TransAction_RegistItemReq@0x517AB0` проверки уровня НЕТ), значит клиент вправе
	//   решить сам, не спрашивая сервер.
	if (LevelGateBlocks(NewMode))
	{
		UE_LOG(LogAsdaExchange, Log,
			TEXT("аукцион: уровень %d < 24 — модалка 97101, окно режима %d НЕ открываем "
			     "(откат: asda.Exchange.RegLevelGate 0 для режимов 1/2, "
			     "asda.Exchange.LegacyLevelGate 1 для режима 3)"), SelfLevel(), NewMode);
		NoticeModal(kLocLevel24);
		// ★ ЕДИНСТВЕННОЕ ОТЛИЧИЕ РЕЖИМА 3 — НЕ В ОКНЕ, А В ЗАПРОСЕ. Окно не открывается у всех
		//   трёх режимов одинаково; но у «моих лотов» запрос 9907 всё же уходит, потому что
		//   уровень 23 с УЖЕ ВЫСТАВЛЕННЫМИ лотами возможен (сервер уровень при регистрации не
		//   проверяет — `Recv_TransAction_RegistItemReq@0x517AB0`), и такому игроку лоты надо
		//   чем-то забирать. Решение «показывать окно, только если лоты ЕСТЬ» уже принято при
		//   исправлении дефекта D10 (`HandleListingPacket`) — здесь оно сохранено, а не
		//   отменено: при пустом списке приёмник окна не покажет.
		//   ★ Пересчёт флага блокировки идёт вместе с запросом: у эталона он привязан к ВХОДУ
		//     в режим 3 (`itemMediationOpenTab` → `itemMediation_onConfirmEvent(this, 3, 1)`),
		//     а гейт уровня — наша надстройка, из-за которой сюда сделан ранний выход.
		if (NewMode == 3) { RequestMyLots(); RecomputeAuctionBlocked(); }
		return false;
	}

	EnsureWidget();
	if (!Widget || !bControlsReady) { return false; }

	// ★★★ ЗАПОМНИТЬ NPC-АУКЦИОНЩИКА (жалоба владельца №1, 2026-08-28). Шапка ВСЕХ аукционных
	//   пакетов несёт на +10 ТИП NPC, у которого открыт аукцион; сервер ищет его на карте
	//   (`sub_4375E0`, `GameServer.c:23192`) и, не найдя, отвечает кодом 2 у 9903 (нотис 12672
	//   «Такого персонажа нет на этой карте») либо 9 у 9901. Порт слал туда ноль — отсюда чужой
	//   нотис вместо 12725 «Нет данных по разыскиваемому предмету». Разбор — в объявлении
	//   `UAsdaNetSubsystem::BuildExchangeHeader15`.
	//   Значение «липкое»: диалог NPC к моменту приёма ответа может быть уже закрыт.
	{
		UGameInstance* GI = GetGameInstance();
		UAsdaNetSubsystem* Net = GI ? GI->GetSubsystem<UAsdaNetSubsystem>() : nullptr;
		const UAsdaNpcDialogSubsystem* Dlg = GI ? GI->GetSubsystem<UAsdaNpcDialogSubsystem>() : nullptr;
		if (Net && Dlg) { Net->SetExchangeNpcId(Dlg->GetCurrentNpcId()); }
	}

	// При смене режима 1↔2 старая панель переоткрывается (`consignRegWndOpenByMode@0xD41DA0`).
	const EAsdaExchangeMode Prev = Mode;
	// ★★★ РЕЖИМ 3 БОЛЬШЕ НЕ ЗАЩЁЛКИВАЕТ `Mode` (найдено 2026-08-31: «„Мои предметы“ вообще не
	//   открывается, и модалки нет»). Цепочка тупика: режим 3 окна НЕ ОТКРЫВАЕТ (гейт эталона
	//   `if (a2 != 3)`), его показывает только приёмник 9908. Порт при этом сразу ставил
	//   `Mode = MyLots`. Если ответа 9908 нет или он приходит с кодом, который эталон игнорирует
	//   (у 9907 сервер, как и у 9903, сперва ищет NPC-аукционщика — `GameServer.c:107645`,
	//   `sub_4375E0`; не нашёл ⇒ статус 2, а `Loc9908` знает только 1 и 100), то на экране не
	//   появляется НИЧЕГО, закрывать нечего, `Mode` навсегда остаётся `MyLots` — и следующий
	//   клик гасит ранний выход `if ((int32)Mode == NewMode) return false;`. Молча и навсегда.
	//   ⇒ Защёлку делает ПРИЁМНИК, когда реально показывает панель (он это и так делает).
	//   В эталоне такой ловушки нет, потому что там окно «моих лотов» открывается ВСЕГДА;
	//   тупик породила наша же правка дефекта D10 (§14.4) плюс молчание сервера.
	if (NewMode != 3) { Mode = (EAsdaExchangeMode)NewMode; }

	// `uiHideThreeControls@0xD435D0` — гасим все три панели, затем показываем нужную.
	//   ★ РЕЖИМ 3 ОКНА НЕ ПОКАЗЫВАЕТ: гейт `if (a2 != 3)` (`item_inventory.c:46788`). Он только
	//   шлёт 9907, а панель «мои лоты» показывает ПРИЁМНИК (`shop_trade.c:12869-12871`).
	// ★★★ ГАСИМ ТРИ ПАНЕЛИ ВСЕГДА, ВКЛЮЧАЯ РЕЖИМ 3 (найдено 2026-08-28: «„Мои предметы“ открывает
	//   НЕ ТО окно»). В эталоне `uiHideThreeControls(this)` стоит ДО свитча по `a2`
	//   (`item_inventory.c:46771`), то есть выполняется и для режима 3; окно не показывается
	//   отдельным гейтом `if (a2 != 3)` НИЖЕ. Порт же для режима 3 не звал `ShowPanel` вовсе,
	//   поэтому на экране оставалась ПРЕДЫДУЩАЯ панель (выставления или поиска), и щелчок по
	//   «Мои предметы» выглядел как «открылось не то окно».
	ShowPanel(NewMode == 3 ? EAsdaExchangeMode::None : Mode);

	switch (NewMode)
	{
		case 1:
		case 2:
			// ★ Гейт уровня «24» сюда БОЛЬШЕ НЕ КОПИРУЕТСЯ: он выше, в единственной точке
			//   `LevelGateBlocks` (требование владельца 2026-08-28 — одно условие на три режима).
			//   Раньше он стоял здесь, показывал модалку и… ПРОПУСКАЛ выполнение дальше, поэтому
			//   поверх модалки всё равно открывалось окно регистрации.
			// `consignItemRegisterToggle(this, N, 1)`: открыть панель, отфильтровать сетку, нотис 12687.
			RebuildSellGrid();
			RefreshRegRows();
			RefreshRegTotals();
			NoticeChat(kLocRegOpened);
			break;

		case 3:
			// ★ НЕ ВКЛАДКА: сразу шлём 9907; панель 13200 покажет ПРИЁМНИК 9908.
			RequestMyLots();
			// ★★★ ПОРЯДОК 1:1 С ЭТАЛОНОМ. `itemMediationOpenTab(…, 3)` зовёт
			//   `itemMediation_onConfirmEvent(this, 3, 1)` (`item_inventory.c:46780`), а тот
			//   СНАЧАЛА отправляет запрос (`itemMediationDispatch(this, 3, 0)`), и только ПОТОМ
			//   пересчитывает флаг — то есть по ЕЩЁ СТАРОМУ составу «моих лотов». Свежий 9908
			//   к этому моменту не пришёл, и это не ошибка: закрытие панели пересчитает флаг
			//   заново, уже по новому составу.
			RecomputeAuctionBlocked();
			break;

		case 4:
		case 5:
			NoticeChat(kLocBrowseOpenedA);
			NoticeChat(kLocBrowseOpenedB);
			// ★★★ P1: `uiSatisfyWindowSwitchTab@0xD53D10` (`ui_core.c:51934-51941`) при показе набора
			//   категорий СРАЗУ заполняет три списка критериев ТЕКУЩЕЙ категорией режима:
			//   `uiItemDlg_fillAttributeLists(this, 4, *(this+2076))` против `(this, 5, *(this+2075))`.
			FillAttributeLists(NewMode, (NewMode == 4) ? SearchCritSel2 : SearchCritSel1);
			// ★ Эталон сам имитирует клик 13424: сброс страницы и немедленный поиск.
			PageCur = 0; PageLast = 0;
			ScrollRowLot = 0;
			SendSearch();
			break;

		default:
			break;
	}

	UE_LOG(LogAsdaExchange, Log, TEXT("аукцион: режим %d → %d"), (int32)Prev, NewMode);
	return true;
}

void UAsdaExchangeSubsystem::ShowPanel(EAsdaExchangeMode NewMode)
{
	// Гасим все три поддерева, потом показываем нужное — порядок эталона
	//   (`uiHideThreeControls@0xD435D0`, `ui_core.c:50679-50684`).
	SetGroupVisible(GrpRegistration, false);
	SetGroupVisible(GrpMyItem,       false);
	SetGroupVisible(GrpExchanger,    false);

	// ★★★ КЛИК-ЗОНЫ СЕТОК ЖИВУТ В КОРНЕ ОКНА, А НЕ ВНУТРИ ПАНЕЛИ (`UAsdaIconCellGrid::BuildClickOverlay`),
	//   поэтому гашение панели их НЕ ЗАДЕВАЕТ. Пока их не гасили явно, невидимые ячейки одной панели
	//   ловили мышь поверх видимой другой — часть кнопок «не нажималась» (жалоба владельца).
	const bool bReg  = (NewMode == EAsdaExchangeMode::RegisterSpend || NewMode == EAsdaExchangeMode::RegisterShell);
	const bool bMy   = (NewMode == EAsdaExchangeMode::MyLots);
	const bool bBrow = (NewMode == EAsdaExchangeMode::BrowseA || NewMode == EAsdaExchangeMode::BrowseB);
	if (GridSell)  { GridSell->SetHitZonesVisible(bReg); }
	if (GridReg)   { GridReg->SetHitZonesVisible(bReg); }
	if (GridMy)    { GridMy->SetHitZonesVisible(bMy); }
	if (GridLots)  { GridLots->SetHitZonesVisible(bBrow); }
	if (GridSowel) { GridSowel->SetHitZonesVisible(bBrow); }

	// ★ Окно показывается во ВСЕХ режимах, кроме «нет режима». Режим 3 окна сам не открывает
	//   (`if (a2 != 3)`, `item_inventory.c:46788`) — панель «мои лоты» показывает ПРИЁМНИК 9908,
	//   и он приходит уже с `Mode == MyLots`.
	const bool bWantWindow = (NewMode != EAsdaExchangeMode::None);

	switch (NewMode)
	{
		case EAsdaExchangeMode::RegisterSpend:
		case EAsdaExchangeMode::RegisterShell:
			SetGroupVisible(GrpRegistration, true);
			break;
		case EAsdaExchangeMode::MyLots:
			SetGroupVisible(GrpMyItem, true);
			break;
		case EAsdaExchangeMode::BrowseA:
			SetGroupVisible(GrpExchanger, true);
			// ★ МИСНЕЙМ M1: режим 4 показывает RADIOBTN2 (7 кнопок), а не RADIOBTN1.
			SetGroupVisible(GrpRadio2, true);
			SetGroupVisible(GrpRadio1, false);
			break;
		case EAsdaExchangeMode::BrowseB:
			SetGroupVisible(GrpExchanger, true);
			// ★ Режим 5 показывает RADIOBTN1 (8 кнопок).
			SetGroupVisible(GrpRadio1, true);
			SetGroupVisible(GrpRadio2, false);
			break;
		default:
			// Режима нет — гасим оба набора категорий, иначе их кнопки останутся живыми.
			SetGroupVisible(GrpRadio1, false);
			SetGroupVisible(GrpRadio2, false);
			break;
	}

	// Узлы «вечно скрытые» гасим ПОСЛЕ общего показа (закон `jui-uneven-node-layout`).
	SetGroupVisible(AlwaysHidden, false);

	// Подписи вкладок и подсветка активной — после того, как набор показан.
	if (bBrow || bMy) { RefreshCategoryCaptions(); }

	if (bWantWindow && !bOpen)
	{
		bOpen = true;
		if (UGameInstance* GI = GetGameInstance())
		{
			if (UAsdaWindowSubsystem* WM = GI->GetSubsystem<UAsdaWindowSubsystem>())
			{
				WM->SetWindowZOrder(kWindowId, 0);
			}
		}
	}
	if (Widget)
	{
		AsdaUi::SetShownDecor(Widget, bOpen);
	}
}

bool UAsdaExchangeSubsystem::CancelByType(int32 CancelType)
{
	// ★ Нумерация закрытия НЕ совпадает с нумерацией открытия
	//   (`itemMediationCancelByType@0xD43100`): 1 = выставление, 2 = мои лоты, 3 = поиск.
	if (CancelType <= 0 || CancelType > 3) { return false; }
	switch (CancelType)
	{
		case 1: NoticeChat(kLocRegClosed);    break;
		case 2:
			// ★ Закрытие «моих лотов» = `itemMediationCancelByType(this, 2)`
			//   (`item_inventory.c:46824`) → `itemMediation_onConfirmEvent(this, 3, 0)`:
			//   СНАЧАЛА нотис 12690, ПОТОМ пересчёт флага блокировки — и именно здесь оригинал
			//   его СНИМАЕТ, если висящих лотов в свежем списке 9908 больше нет.
			NoticeChat(kLocMyClosed);
			RecomputeAuctionBlocked();
			break;
		case 3: NoticeChat(kLocBrowseClosed); break;
		default: break;
	}
	Mode = EAsdaExchangeMode::None;
	bOpen = false;
	bRegAll = false; bMyAll = false; bLotAll = false;   // ★ пачка 20: окно закрыто — галочки «всё» сняты (виджет пересоздаётся)
	ShowPanel(EAsdaExchangeMode::None);
	if (UGameInstance* GI = GetGameInstance())
	{
		if (UAsdaWindowSubsystem* WM = GI->GetSubsystem<UAsdaWindowSubsystem>())
		{
			WM->CloseWindow(kWindowId);
		}
	}
	Widget = nullptr;
	ResetBindings();
	return true;
}

void UAsdaExchangeSubsystem::CloseAll()
{
	// Крестик 13004: эталон определяет активный тип и закрывает именно его.
	switch (Mode)
	{
		case EAsdaExchangeMode::RegisterSpend:
		case EAsdaExchangeMode::RegisterShell: CancelByType(1); break;
		case EAsdaExchangeMode::MyLots:        CancelByType(2); break;
		case EAsdaExchangeMode::BrowseA:
		case EAsdaExchangeMode::BrowseB:       CancelByType(3); break;
		default:
			// `objClassifyType123` вернул −1 ⇒ клик игнорируется.
			break;
	}
}

// ═══════════════════════ панель выставления ═══════════════════════════════════════════════════
int32 UAsdaExchangeSubsystem::SourceContainerForMode(EAsdaExchangeMode M) const
{
	// ★ ЗЕРКАЛЬНО: режим 1 → контейнер 2 (`SpendBag`, 60 ячеек), режим 2 → контейнер 1
	//   (`ShellBag`, 30 ячеек). Доказано `invFilterUsableItems@0xD45520`.
	if (M == EAsdaExchangeMode::RegisterSpend) { return (int32)EAsdaItemContainer::SpendBag; }
	if (M == EAsdaExchangeMode::RegisterShell) { return (int32)EAsdaItemContainer::ShellBag; }
	return 0;
}
