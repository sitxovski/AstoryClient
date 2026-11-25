// Copyright (c) 2026 @sitxovski (Discord)
// AsdaPremiumShopSubsystem_Buy.cpp — ★ R2 10.11.2026 (разрез AsdaPremiumShopSubsystem.cpp, дословный перенос): покупка: клики, проверки, отправка, корзина-прогон, ответы, баланс.
//   Общее для частей — в AsdaPremiumShopSubsystemInternal.h; шапка и реверс — в AsdaPremiumShopSubsystem.cpp.
#include "UI/AsdaPremiumShopSubsystem.h"
#include "Resources/AsdaSoundSubsystem.h"   // ★ 06.11.2026: звуки окна 6000 (4/5/6)
#include "AsdaDevConsole.h"   // ★ античит А2: ASDA_DEV_CVAR / ASDA_DEV_CONSOLE — дев-ключи и команды вне Shipping
#include "UI/AsdaJuiWindowHandle.h"   // ★ волна 3 (15.09.2026): каркас окон JUI
#include "UI/AsdaNotice.h"   // ★ волна 1: фасад нотисов (Loc / Chat / MsgBox)
#include "UI/AsdaNoticeColors.h"   // ★ 02.11.2026: цвет −11514 чата 12919 (гейт `uiCanInteractWithTarget`)
#include "UI/AsdaHairShopSubsystem.h"        // ★ ревизия М-3: сессия парикмахерской (`CObject_GetField166`)
#include "UI/AsdaBoosterPanelSubsystem.h"   // ★ ревизия М-3: барабан гачи 10050 (`CObject_GetField254`); ★ 31.10.2026: окно «Вид» 15600
#include "UI/AsdaWarehouseSubsystem.h"      // ★ ревизия М-3: свиток склада (`CObject_GetField5581`)
#include "UI/AsdaTradeStateSubsystem.h"   // ★ 29.09.2026: гейты 6947 (открытие) и `uiCanInteractWithTarget` (покупка) — окно обмена 2008

#include "AsdaDataTableCache.h"
#include "AsdaNetSubsystem.h"
#include "Items/AsdaItemTableSubsystem.h"
#include "Text/AsdaTextSubsystem.h"
#include "UI/AsdaEquipShopSubsystem.h"      // ★ ОБЩИЕ ТОЧКИ витрины `.she` (закон port-shared-points)
#include "UI/AsdaIconCellGrid.h"
#include "UI/AsdaItemTooltipSubsystem.h"
#include "UI/AsdaMessageSubsystem.h"
#include "UI/AsdaPackagePanelSubsystem.h"   // ★ панель состава набора 26500 — уже портирована
#include "UI/AsdaBoosterShopSubsystem.h"   // ★ самотест общий: печатает и магазин бустеров 15300
#include "UI/AsdaPremiumMallSubsystem.h"    // консольные команды дёргают и диспетчер 30000
#include "UI/AsdaPurchaseBoxSubsystem.h"    // ★ общая модалка подтверждения 1559
#include "UI/AsdaCashGiftSubsystem.h"       // ★ 01.11.2026 P101_CashGift: подарок 5072/5074 (кнопки 6085 + 9i, панели 6296/6333)
#include "UI/AsdaTipPanel.h"
#include "UI/AsdaUiHelpers.h"
#include "UI/AsdaWindowSubsystem.h"
// ★ волна В4 ремесла: хвост 5045 при закрытии (восстановление снимка окон)   ★ R3: комментарий снятого включения "Npc/AsdaNpcDialogSubsystem.h" (файл им не пользовался) — сохранён
#include "UI/AsdaNpcShopSubsystem.h"        // ★ волна В5: `uiInventoryOpenWindow` закрывает магазин NPC

#include "Blueprint/UserWidget.h"
#include "Blueprint/WidgetTree.h"
#include "Components/ComboBoxString.h"
#include "Components/Button.h"
#include "Components/CanvasPanelSlot.h"
#include "Components/CheckBox.h"
#include "Components/Image.h"
#include "Components/PanelWidget.h"
#include "Components/TextBlock.h"
#include "AsdaBytes.h"
#include "Engine/Engine.h"
#include "Engine/GameViewportClient.h"
#include "UnrealClient.h"
#include "Engine/GameInstance.h"
#include "Engine/World.h"
#include "GenericPlatform/GenericPlatformProcess.h"
#include "HAL/IConsoleManager.h"
#include "TimerManager.h"
#include "Internationalization/Regex.h"
// ★ С-Р1: общий офлайн-гейт отладочных команд (RefuseDebugWhenOnline)   ★ R3: комментарий снятого включения "AsdaOutgoingGate.h" (файл им не пользовался) — сохранён
#include "UI/AsdaDisassemblySubsystem.h"    // ★ волна 2-А: окно разборки 32000 видно ⇒ магазин 6000 молча не открывается
#include "Test/AsdaSelfTestKit.h"   // ★ 11.10.2026 (В3 S1): разбор аргументов самотеста (`quit` не путается с числом)
#include "Text/AsdaLang.h"          // ★ 26.10.2026: самотест имён бустеров — язык сессии (RU/EN)
#include "UObject/UObjectIterator.h" // ★ 26.10.2026: самотест имён бустеров — обход текстов открытых окон
#include "UI/AsdaInventorySubsystem.h"      // ★ 26.10.2026: самотест премиум-коробок — подбор ячеек вскрытия (ReservePackageSlots)
#include "Misc/FileHelper.h"                // ★ 26.10.2026: самотест премиум-коробок читает Tools/data/premium_box_sets.json
#include "Misc/Paths.h"                     // ★ 26.10.2026: путь спецификации от каталога проекта
#include "Dom/JsonObject.h"                 // ★ 26.10.2026: разбор спецификации наборов
#include "Serialization/JsonReader.h"       // ★ 26.10.2026: разбор спецификации наборов
#include "Serialization/JsonSerializer.h"   // ★ 26.10.2026: разбор спецификации наборов
// ★ 24.10.2026 (выход из сидения 1:1): `ApplyEtalonSceneStop` — стоп/«встать» при входе в сцену   ★ R3: комментарий снятого включения "World/AsdaPlayerCharacter.h" (файл им не пользовался) — сохранён
#include "UI/AsdaSystemOptionSubsystem.h"      // ★ 25.10.2026 (ревизия sit108, М-3): гейт «персонаж занят» (`objStateAllowsRender`) до стопа сцены
#include "GameFramework/PlayerController.h"    // ★ 24.10.2026: свой павн для `ApplyEtalonSceneStop`
#include "AsdaPremiumShopSubsystemInternal.h"   // ★ R2 10.11.2026: общее для частей AsdaPremiumShopSubsystem*.cpp

namespace {
	// ── Нотисы ветки (таблица «событие → id → канал» — в отчёте порта) ──
	constexpr int32 kLocNoGxp     = 6778;   // «Не хватает GXP.» — чат
	constexpr int32 kLocBadState  = 12919;  // «…воспользоваться элитным магазином не удастся» — чат
	constexpr int32 kLocNoAnswer  = 1026;   // «Сервер не отвечает.» — чат (запрос уже в полёте)

	// Коды результата 5071 → строка (§3.3 отчёта).
	//   ★ R5-b 12.11.2026: таблица одна — `AsdaPremiumBuy::kResLoc5071` (AsdaPremiumBuyQueue.h; построчные подписи перенесены туда).
	constexpr const int32 (&kResLoc)[15] = AsdaPremiumBuy::kResLoc5071;
	// Строки успеха (код 1): выбор по «валюта × куда положили».
	constexpr int32 kOkGxpStore   = 20062;  // GXP,     в хранилище
	constexpr int32 kOkCharmStore = 20061;  // обаяние, в хранилище
	constexpr int32 kOkGxpBag     = 20064;  // GXP,     в инвентарь
	constexpr int32 kOkCharmBag   = 20063;  // обаяние, в инвентарь
	// Фолбэк для отсутствующей 12961 (дефект К11): показываем 9570, а не чужую запасную строку оригинала.   // ★ R5-fix-b 13.11.2026 (третий заход): текст уточнён
	constexpr int32 kResFallback  = 9570;

	// ★ 06.11.2026: карточка с вариантами класса и уровня покупается через панель 6296 с комбо, как в оригинале (ui_core.c LABEL_48 → `tradeSelectSlot`).
	static ASDA_DEV_CVAR(int32, CVarBuyVariants,
		TEXT("asda.PremiumMall.BuyVariants"), 1,
		TEXT("1 = кнопка покупки карточки с вариантами открывает панель выбора класса и уровня; 0 = сразу модалка 1559, код карточки."),
		ECVF_Default);

	// ★ 06.11.2026: кнопка 6275 по эталону открывает панель 6296 для отмеченной строки (ui_core.c:25943-25953, `itemBuildTooltipAndCategoryList@0xC45340`).
	static ASDA_DEV_CVAR(int32, CVarCartVariants,
		TEXT("asda.PremiumMall.CartVariants"), 1,
		TEXT("1 = «Купить выбранное» спрашивает класс и уровень у строк корзины с вариантами; 0 = покупает коды строк как есть."),
		ECVF_Default);

	// ★★★ [ИСПРАВЛЕНИЕ] 26.10.2026 баг игроков «после отказа баланс −1, дальше "не хватает кредитов" до перезапуска».
	//   Сервер на отказ шлюза (`sub_4F6650`, ветка «GP7's Response is failure», GameServer.c:95228-95233) шлёт 5071 с
	//   конструктором `sub_4F9F00` (GameServer.c:96572): код 0, валюта 0, БАЛАНС −1 (@+2). Эталон
	//   `shop_OnRecvTransactionResult@0xC30BD0` (etalon/shop_trade.c:3255) пишет баланс (`uiInventoryUpdateCashText`)
	//   ТОЛЬКО в ветке кода 1, на отказ (case 0, 2…0xE) — лишь нотис. Порт клал баланс из ЛЮБОГО ответа ⇒ −1 в надписи и
	//   гейт `CashGxp < Price` отбивал каждую следующую покупку нотисом «не хватает» до нового 5466/5467.
	//   Случай игрока 24.10 19:18:44 (alpha-2.0.0, корзина 5×30, шлюз premium-price-mismatch). Ключ 0 = прежнее поведение порта.
	static ASDA_DEV_CVAR(int32, CVarFailKeepsBalance,
		TEXT("asda.PremiumMall.FailKeepsBalance"), 1,
		TEXT("1 = как эталон: баланс окна 6000 берётся из ответа 5071 только при коде 1 (на отказ сервер шлёт −1); "
		     "0 = прежний порт: баланс из любого ответа 5071."),
		ECVF_Default);

	static ASDA_DEV_CVAR(int32, CVarFixMultiBuy,
		TEXT("asda.PremiumMall.FixMultiBuyBound"), 1,
		TEXT("1 = не отправлять больше 7 позиций за пакет 5070 (исправление К1/К2 оригинала: "
		     "цикл `slotIdx != 7` при буфере 172 б писал за границу стека и терял 8-ю позицию); "
		     "0 = слать сколько дали (эталонное поведение, НЕБЕЗОПАСНО)."),
		ECVF_Default);

	// ★★★ [ИСПРАВЛЕНИЕ] 25.10.2026 «купил 3 — пришла 1» (разбор и доказательства — шапка `AsdaPremiumBuyQueue.h`):
	//   старый GameServer с шлюзом GP7 из пакета 5070 выдаёт только позицию 0, а списывает за все. Корзина 6275 и
	//   «количество» магазина бустеров уходят по одной позиции на пакет; следующая — после ответа 5071 с кодом 1.
	//   ★★ 25.10.2026 (позднее, слово владельца «корзина из нескольких позиций должна проходить»): сервер исправлен — GameServer
	//   P85 отдаёт шлюзу все коды, шлюз списывает один раз, SQL premrows кладёт заказ строкой ящика до 7 ячеек. Поэтому по
	//   умолчанию корзина снова уходит ОДНИМ пакетом, как у оригинала; поштучный режим оставлен ключом (стойка без P85).
	static ASDA_DEV_CVAR(int32, CVarCartOnePerPacket,
		TEXT("asda.PremiumMall.CartOnePerPacket"), 0,
		TEXT("0 (по умолчанию) = корзина премиум-магазина (6275) и N штук магазина бустеров уходят ОДНИМ пакетом 5070, как у "
		     "оригинала (сервер с P85 кладёт заказ одной строкой ящика); 1 = поштучно: один 5070 на позицию, следующая — после "
		     "ответа 5071 с кодом 1 (для стойки без P85: там сервер выдаёт из пакета одну вещь)."),
		ECVF_Default);

	// ★ r2 25.10.2026 (баг владельца «купил 7 — в строке 6»): смешанная корзина item_list + special_itemlist — ОДНИМ пакетом
	//   (GameServer P85 r2 передаёт шлюзу коды самого пакета); 1 — двумя пакетами, как alpha-2.0.3 (две строки ящика).
	static ASDA_DEV_CVAR(int32, CVarMixedCartTwoPackets,
		TEXT("asda.PremiumMall.MixedCartTwoPackets"), 0,
		TEXT("0 (по умолчанию) = смешанная корзина (item_list + special_itemlist) уходит ОДНИМ 5070 — одна строка ящика (нужен "
		     "GameServer P85 r2); 1 = двумя пакетами по таблицам, как alpha-2.0.3 (два заказа — две строки)."),
		ECVF_Default);

	// К11 — строки 12961 нет в таблице текста: при коде результата 0x0E оригинал показывал чужую запасную строку
	//   (`ppStringArray[99]`, в русской сборке — 1005).
	//   ★ 13.11.2026 (строка 12961): строка записана (RU «Не продаётся», EN «Not For Sale»). При ключе 1 код 0x0E
	//   показывает 9570, если строки 12961 нет в таблице (`Loc(12961)` пуст) ИЛИ `asda.PremiumMall.Show12961` = 0 (как до
	//   13.11.2026); иначе — саму 12961. При ключе 0 — `kResLoc` как есть (12961 или пусто).   // ★ R5-fix-b 13.11.2026 (заключительный заход, третья запись), ревизия REVIEW6 М-7   // ★ R5-fix-b 13.11.2026 (заключительный заход): уточнено (ревизор, зам. 5.1)
	//   (★ R5-fix-b 13.11.2026 (третий заход): прежний текст «пустое окно» был неточен.)
	static ASDA_DEV_CVAR(int32, CVarNotice12961,
		TEXT("asda.PremiumMall.Notice12961Fallback"), 1,
		TEXT("1 = при коде результата 0x0E и ОТСУТСТВИИ строки 12961 в таблице текста показывать 9570 (исправление К11: "
		     "в русском эталоне строки нет); 0 = как оригинал. Строка есть (с 13.11.2026) — показывается она, "
		     "если asda.PremiumMall.Show12961 = 1 (при 0 — 9570 и при строке)."),   // ★ R5-fix-b 13.11.2026 (третий заход): справка уточнена
		ECVF_Default);

	// ★ R5-fix-b 13.11.2026 (второй заход): показ записанной строки 12961 — правка поведения под ключом (бриф R5-fix: 1 = исправлено, 0 = как было).
	//   1 — код 0x0E показывает 12961 («Не продаётся» / «Not For Sale»), запасная 9570 — только если строки в таблице нет;
	//   0 — как до 13.11.2026: 9570 (окно 6000 — при `Notice12961Fallback` = 1, магазин бустеров — всегда). Общий для обоих магазинов.
	static ASDA_DEV_CVAR(int32, CVarShow12961,
		TEXT("asda.PremiumMall.Show12961"), 1,
		TEXT("1 = код 0x0E ответа покупки 5071 показывает строку 12961 («Не продаётся»), если она есть в таблице текста "
		     "(решение владельца 13.11.2026; в русском эталоне строки не было — дефект оригинала); 0 = как было: запасная 9570."),
		ECVF_Default);

	// ★ R5-fix-b 13.11.2026 (третий заход): шов самотеста — считать строку 12961 отсутствующей в таблице текста, чтобы проверить ветку
	//   «строки нет → 9570» без подмены ассета. Только дев-сборки: в Shipping ключа нет, значение постоянно 0 (поведение то же).
	static ASDA_DEV_CVAR(int32, CVarTest12961Missing,
		TEXT("asda.PremiumMall.Test12961Missing"), 0,
		TEXT("Только для самотеста: 1 = считать строку 12961 отсутствующей в таблице текста (проверка запасной 9570); 0 = как есть."),
		ECVF_Default);
}

// Аксессор ключа `asda.PremiumMall.Show12961` для магазина бустеров (имя ключа вне этого .cpp не пишем — Shipping).
bool AsdaPremiumShow12961On()
{
	return CVarShow12961.GetValueOnGameThread() != 0;
}

// ★ R5-fix-b 13.11.2026 (третий заход): «строки 12961 нет в таблице текста» — текст пуст или (только дев) включён шов самотеста.
bool AsdaPremium12961Missing(const FString& Text)
{
	return Text.IsEmpty() || CVarTest12961Missing.GetValueOnGameThread() != 0;
}

// Аксессор ключа `asda.PremiumMall.CartOnePerPacket` для магазина бустеров (имя ключа вне этого .cpp не пишем — Shipping).
bool AsdaPremiumCartOnePerPacketOn()
{
	return CVarCartOnePerPacket.GetValueOnGameThread() != 0;
}

// ═══════════════════════════════ ДЕЙСТВИЯ ══════════════════════════════════════════════════════
void UAsdaPremiumShopSubsystem::OnCardClicked(int32 Card)
{
	if (!bOpen || !Cards.IsValidIndex(Card) || Cards[Card].ItemCode <= 0) { return; }
	// [ИСПРАВЛЕНИЕ] 25.10.2026 (ревизия wave_1025 В-1): пока покупка ждёт ответа 5071, корзину не меняем — как гейт оригинала
	//   «запрос в полёте» (`this[2071]` → нотис 1026): иначе ответ снял бы не ту строку / очистил добавленную после отправки.
	if (bPending || CartRun.bRun || CartNextBatch.Num() > 0) { NoticeChat(kLocNoAnswer); return; }
	SelCard = Card;
	MoveSelectionFrame(Card);   // ★ 08.11.2026 (пачка 16, баг 6): рамка 6273 на карточке, положенной в корзину
	const int32 Slot = FindFreeCartSlot();
	if (Slot < 0)
	{
		// В эталоне корзина ровно на 7 строк; лишнее просто не помещается.
		UE_LOG(LogAsdaPremShop, Log, TEXT("[премиум-магазин] корзина заполнена (7 строк) — товар не добавлен"));
		return;
	}
	CartRows[Slot].ItemCode = Cards[Card].ItemCode;
	CartRows[Slot].bChecked = true;
	RefreshCart();
}

void UAsdaPremiumShopSubsystem::OnCartRowClicked(int32 Row)
{
	if (!bOpen || !CartRows.IsValidIndex(Row) || CartRows[Row].ItemCode <= 0) { return; }
	if (bPending || CartRun.bRun || CartNextBatch.Num() > 0) { NoticeChat(kLocNoAnswer); return; }   // [ИСПРАВЛЕНИЕ] 25.10.2026 (В-1): галку во время покупки не трогаем
	CartRows[Row].bChecked = !CartRows[Row].bChecked;
	RefreshCart();
}

void UAsdaPremiumShopSubsystem::OnResetClicked()
{
	// [ИСПРАВЛЕНИЕ] 25.10.2026 (В-1): «Сброс» 6037 во время покупки отбивается нотисом 1026 (внутренние вызовы после ответа
	//   идут, когда защёлка уже снята и очередей нет).
	if (bPending || CartRun.bRun || CartNextBatch.Num() > 0) { NoticeChat(kLocNoAnswer); return; }
	for (FAsdaPremiumCartRow& R : CartRows) { R = FAsdaPremiumCartRow(); }
	// ★ 08.11.2026 (пачка 16, баг 6) [ОРИГИНАЛ]: кнопка 6037 (`case 0x1795`, ui_core.c `uiOnCommandDispatch`) рамку выбора 6273 и
	//   индекс выбранной карточки (`gap_2734[680]`) не трогает — выбор остаётся (было `SelCard = -1`, рамки тогда не было вовсе).
	RefreshCart();
}

// Кнопка карточки `_BUT0` — покупка ОДНОЙ вещи через ОБЩУЮ МОДАЛКУ 1559 (режим A/B).
// ★★★ «НАЖИМАЮ ОК И НИЧЕГО НЕ ПРОИСХОДИТ» (жалоба владельца 2026-08-28).
//   ПРАВИЛО ВЛАДЕЛЬЦА: молчаливых отказов быть не должно — КАЖДЫЙ ранний выход пишет причину
//   в журнал. Нотисы в чат остаются ровно эталонные (1026 / 12919 / 6778), новых не выдумываем.
// ★ 29.09.2026 (волна 0 торговли): часть `uiCanInteractWithTarget@0xC43310` (ui_core.c:28973-28982), которую зовут все
//   четыре отправителя покупки окна 6000 (`net_sendItemActionRequest`, `netSendSecureChallengeMd5`,
//   `shopSendBoosterPurchaseRequest`, `boosterShopSendBuyItem` — network.c:13105/13533, shop_trade.c:2879/3082) ДО гейта
//   1026: открыто ОКНО обмена 2008 (`g_bPartyInviteFlag` = `CExchange+0x2C`) → отправитель молча возвращает 0.
//   [ПОРТ] Остальные условия той же функции (`this[10327]`, `objStateAllowsRender`, `sub_93248D`) в порту не перенесены и до
//   правки. Общая точка `UAsdaTradeStateSubsystem` (пока окна 2008 нет — false). true = покупка заблокирована (молча).
static bool PremiumBuyBlockedByExchange(const UObject* Ctx, const TCHAR* Where)
{
	// ★ 02.11.2026 (P101, ревизия С-3) [ОРИГИНАЛ]: гейт `uiCanInteractWithTarget@0xC43310` перенесён ЦЕЛИКОМ общей точкой
	//   `AsdaShopCanInteract` (магазин 6800, окно обмена 2008, персонаж занят, рюкзак) — прежде порт проверял только окно обмена.
	//   Отказ — чат 12919 (−11514, 10 с), как у отправителей 5070 эталона (shop_trade.c:3041-3042 и аналог в `boosterShopSendBuyItem`); прежде порт молчал.
	if (AsdaShopCanInteract(Ctx)) { return false; }
	const UWorld* W = Ctx ? Ctx->GetWorld() : nullptr;
	AsdaNotice::ChatColored(W ? W->GetGameInstance() : nullptr, EAsdaMsgCategory::Trade,
		AsdaNotice::LocRaw(W ? W->GetGameInstance() : nullptr, 12919), AsdaNoticeColor::Gold, 10000.f);
	UE_LOG(LogAsdaPremShop, Log, TEXT("[премиум-магазин] %s: uiCanInteractWithTarget — нельзя (6800/обмен/занят/рюкзак), чат 12919"), Where);
	return true;
}

void UAsdaPremiumShopSubsystem::OnCardBuyClicked(int32 Card)
{
	if (!bOpen)
	{
		UE_LOG(LogAsdaPremShop, Warning, TEXT("[премиум-магазин] покупка: окно не открыто"));
		return;
	}
	if (!Cards.IsValidIndex(Card))
	{
		UE_LOG(LogAsdaPremShop, Warning, TEXT("[премиум-магазин] покупка: карточки %d нет (всего %d)"),
			Card, Cards.Num());
		return;
	}
	const int32 Code = Cards[Card].ItemCode;
	if (Code <= 0)
	{
		UE_LOG(LogAsdaPremShop, Warning,
			TEXT("[премиум-магазин] покупка: карточка %d ПУСТА (кода товара нет) — страница %d/%d, товаров %d"),
			Card, Page + 1, MaxPage + 1, Entries.Num());
		return;
	}

	if (PremiumBuyBlockedByExchange(this, TEXT("покупка карточки"))) { return; }   // ★ 29.09.2026: `uiCanInteractWithTarget`
	// ★ 01.11.2026 P101_CashGift: открыта панель подарка 6296/6333 → `uiCheckTradePanelsClosed@0xC43390` — чат 12929 (ui_core.c:28995).
	if (IsGiftPanelShown()) { NoticeChat(12929); return; }
	// ★ 08.11.2026 (пачка 16, баг 6) [ОРИГИНАЛ]: `tradeSelectSlot@0xC406D0` первым делом ставит рамку 6273 на карточку кнопки
	//   (shop_trade.c:5471-5472: `uiMapCommandIdToSlotIndex` → `uiTabPanel_MoveSelectionHighlight(…, 1)`), до гейта «запрос в полёте».
	SelCard = Card;
	MoveSelectionFrame(Card);
	// Гейт «запрос уже в полёте» (`this[2071] == 1`) → нотис 1026 (`ida.c:480629`).
	//   [ИСПРАВЛЕНИЕ] 25.10.2026: идущая поштучная покупка корзины — тоже «в полёте» (между ответом и следующей позицией).
	if (bPending || CartRun.bRun || CartNextBatch.Num() > 0)
	{
		UE_LOG(LogAsdaPremShop, Warning,
			TEXT("[премиум-магазин] покупка карточки %d (код %d) отклонена: предыдущий запрос 5070 ещё в полёте (нотис %d)"),
			Card, Code, kLocNoAnswer);
		NoticeChat(kLocNoAnswer);
		return;
	}

	if (IsGiftPanelShown()) { NoticeChat(12929); return; }   // ★ 06.11.2026: `uiCheckTradePanelsClosed@0xC43390` — иначе карточка подменит панель строки корзины
	if (TryOpenBuyVariants(Code)) { return; }   // ветка вариантов играет звук 4 сама
	AsdaPremiumUiSound(GetGameInstance(), 4);   // ★ 06.11.2026 [ОРИГИНАЛ]: `tradeSelectSlot@0xC406D0` (shop_trade.c:5473)
	BuyCodeChecked(Card, Code);
}

bool UAsdaPremiumShopSubsystem::TryOpenBuyVariants(int32 Code)
{
	// Оригинал: `tradeSelectSlot` → `tradeBuildCategoryItemList(код, setCode, 1)` + `ItemTooltip_PopulateWindow(код, 0, 0)` (shop_trade.c:5505-5520), панель 6296, panelMode 0.
	//   [ОТСТУПЛЕНИЕ] Панель только у карточек с вариантами; остальные идут сразу в 1559, как раньше.
	if (CVarBuyVariants.GetValueOnGameThread() == 0 || !GiftEquip.Root) { return false; }
	const UGameInstance* GI = GetGameInstance();
	const UAsdaItemTableSubsystem* IT = GI ? GI->GetSubsystem<UAsdaItemTableSubsystem>() : nullptr;
	if (!IT || !IT->GetItem(Code)) { return false; }
	GiftBuildSetList(Code, /*bQuiet*/true);
	if (!bGiftVariants) { return false; }
	if (UAsdaSoundSubsystem* Snd = const_cast<UGameInstance*>(GI)->GetSubsystem<UAsdaSoundSubsystem>()) { Snd->PlaySound2DById(4); }
	PopulateGiftPanel(Code, /*bFunc*/false, /*bBuy*/true);
	UE_LOG(LogAsdaPremShop, Log, TEXT("[премиум-магазин] карточка %d: вариантов %d, панель 6296 в режиме покупки"), Code, GiftSetCodes.Num());
	return IsGiftPanelShown();
}

bool UAsdaPremiumShopSubsystem::CartPickNext()
{
	while (CartPick.Num() > 0)
	{
		const int32 Row = CartPick[0];
		CartPick.RemoveAt(0);
		if (CartRows.IsValidIndex(Row) && TryOpenBuyVariants(CartRows[Row].ItemCode))
		{
			CartPickRow = Row;
			return true;
		}
	}
	return false;
}

void UAsdaPremiumShopSubsystem::CartPickConfirm()
{
	if (CartRows.IsValidIndex(CartPickRow)) { CartRows[CartPickRow].ItemCode = GiftItemCode; }
	RefreshCart();
	const TArray<int32> Left = CartPick;
	CloseGiftPanels();
	CartPick = Left;
	if (CartPickNext()) { return; }
	bCartPicked = true;
	OnBuySelectedClicked();
}

void UAsdaPremiumShopSubsystem::BuyCodeChecked(int32 Card, int32 Code)
{
	const UGameInstance* GI = GetGameInstance();
	const UAsdaItemTableSubsystem* IT = GI ? GI->GetSubsystem<UAsdaItemTableSubsystem>() : nullptr;
	const FAsdaItemRecord* Rec = IT ? IT->GetItem(Code) : nullptr;
	FAsdaSpecialItemlistRow Sp;
	const bool bSpecial = (!Rec && IT) ? IT->GetSpecialItem(Code, Sp) : false;
	if (!Rec && !bSpecial)
	{
		UE_LOG(LogAsdaPremShop, Warning,
			TEXT("[премиум-магазин] покупка карточки %d отклонена: кода %d нет ни в `item_list`, ни в ")
			TEXT("`special_itemlist` (нотис %d)"), Card, Code, kLocBadState);
		NoticeChat(kLocBadState);
		return;
	}

	// Локальная проверка платёжеспособности (`ida.c:480656-480658`): GXP игрока против цены.
	//   ★ К16 оригинала: она ВСЕГДА сверяется с GXP, даже когда сервер посчитал бы обаянием.
	//   Оставляем 1:1 — отдельного отправителя покупки за обаяние в эталоне нет вовсе.
	const int32 Price = Rec ? Rec->Price : Sp.Price;
	if (CashGxp < Price)
	{
		// ★ Самая частая причина «ничего не происходит»: баланс GXP ещё НЕ ПРИШЁЛ (пакет 5467)
		//   или его действительно не хватает. Раньше отказ уходил только нотисом в чат.
		UE_LOG(LogAsdaPremShop, Warning,
			TEXT("[премиум-магазин] покупка карточки %d (код %d) отклонена: цена %d, а GXP на счету %d ")
			TEXT("(нотис %d). Если баланс 0 — ответ 5467 ещё не пришёл; для проверки без сервера: ")
			TEXT("asda.PremiumMall.SetGxp <сумма>"),
			Card, Code, Price, CashGxp, kLocNoGxp);
		NoticeChat(kLocNoGxp);
		return;
	}
	UE_LOG(LogAsdaPremShop, Log,
		TEXT("[премиум-магазин] покупка карточки %d: код %d, цена %d, GXP %d — открываю модалку 1559"),
		Card, Code, Price, CashGxp);

	FAsdaPurchaseBoxRequest Req;
	Req.Mode      = Rec ? EAsdaPurchaseMode::CashEquip : EAsdaPurchaseMode::CashFunc;
	Req.ItemCode  = Code;
	Req.PanelMode = 0;                 // покупка (не подарок: путь подарка = 5074, сервер его не берёт)
	Req.Quantity  = 1;
	Req.TradeMode = 1;                 // «Рюкзак» (loc 12925) — доставка в инвентарь
	if (CardQtyByCode.Contains(Code) && IT) { Req.NameOverride = CardLabel(Code, IT->GetItemName(Code)); }   // ★ 06.11.2026: модалка 1559 называет товар как карточка
	if (UAsdaPurchaseBoxSubsystem* Box = GI ? const_cast<UGameInstance*>(GI)->GetSubsystem<UAsdaPurchaseBoxSubsystem>() : nullptr)
	{
		const bool bTable1 = (Rec != nullptr);
		Box->Open(Req, [this, Code, bTable1](bool bOk)
		{
			if (!bOk) { return; }                       // ★ отказ доходит до владельца (исправление D6)
			SendBuy({ Code }, bTable1, /*bToInventory*/true);
			if (bPanelBuy) { CloseGiftPanels(); }       // ★ 06.11.2026: покупка из панели 6296 — панель закрывается
		});
	}
	else
	{
		// Модалки нет — путь не защёлкиваем, иначе получим вечное ожидание (правило владельца).
		UE_LOG(LogAsdaPremShop, Error, TEXT("[премиум-магазин] нет подсистемы модалки 1559 — покупка отменена"));
	}
}

// Кнопка 6275 «Купить выбранные вещи» — все отмеченные строки корзины: у оригинала одним пакетом, у порта с 25.10.2026
//   поштучно ([ИСПРАВЛЕНИЕ] `asda.PremiumMall.CartOnePerPacket`, шапка `AsdaPremiumBuyQueue.h`).
void UAsdaPremiumShopSubsystem::OnBuySelectedClicked()
{
	if (!bOpen) { return; }
	if (PremiumBuyBlockedByExchange(this, TEXT("покупка выбранных"))) { return; }   // ★ 29.09.2026: `uiCanInteractWithTarget`
	if (bPending || CartRun.bRun || CartNextBatch.Num() > 0) { NoticeChat(kLocNoAnswer); return; }   // идущая поштучная покупка / второй пакет — тоже «в полёте»

	// ★ 06.11.2026: эталон покупает одну отмеченную строку через панель 6296. [ОТСТУПЛЕНИЕ] Корзина порта покупает несколько строк, поэтому панель открывается по очереди
	//   для каждой строки с вариантами, выбранный код заменяет код строки, затем обычная 1559 и 5070. Отмена панели прерывает очередь, корзина остаётся.
	const bool bPicked = bCartPicked;
	bCartPicked = false;
	if (!bPicked && CVarCartVariants.GetValueOnGameThread() != 0)
	{
		if (IsGiftPanelShown()) { NoticeChat(12929); return; }   // `uiCheckTradePanelsClosed@0xC43390`
		CartPick.Reset();
		for (int32 j = 0; j < CartRows.Num(); ++j)
		{
			if (CartRows[j].ItemCode > 0 && CartRows[j].bChecked) { CartPick.Add(j); }
		}
		if (CartPickNext()) { return; }
	}

	TArray<int32> Codes;
	for (const FAsdaPremiumCartRow& R : CartRows)
	{
		if (R.ItemCode > 0 && R.bChecked) { Codes.Add(R.ItemCode); }
	}
	if (Codes.Num() == 0) { return; }
	// ★★★ ИСПРАВЛЕНИЕ К1/К2: не больше 7 позиций в пакет. У эталона цикл шёл до `count` с
	//   условием `slotIdx != 7` — при 8 позициях терялась восьмая, при 43+ шла запись за границу
	//   стекового буфера 172 б.
	if (CVarFixMultiBuy.GetValueOnGameThread() != 0 && Codes.Num() > 7) { Codes.SetNum(7); }

	const UGameInstance* GI = GetGameInstance();
	const UAsdaItemTableSubsystem* IT = GI ? GI->GetSubsystem<UAsdaItemTableSubsystem>() : nullptr;
	int32 Total = 0;
	bool bAllTable1 = true;
	for (int32 Code : Codes)
	{
		if (const FAsdaItemRecord* Rec = IT ? IT->GetItem(Code) : nullptr) { Total += Rec->Price; continue; }
		FAsdaSpecialItemlistRow Sp;
		if (IT && IT->GetSpecialItem(Code, Sp)) { Total += Sp.Price; bAllTable1 = false; }
	}
	if (CashGxp < Total) { NoticeChat(kLocNoGxp); return; }

	// Подтверждение — общая модалка 1559 (эталон здесь показывает msgbox loc 20060 «[ss1] - купить?»
	//   у ОДНОЙ вещи; для списка эталон подтверждения не спрашивал вовсе — мы показываем ту же
	//   модалку с первой позицией и общей суммой, чтобы игрок видел, за что платит).
	FAsdaPurchaseBoxRequest Req;
	Req.Mode          = EAsdaPurchaseMode::CashEquip;
	Req.ItemCode      = Codes[0];
	Req.PanelMode     = 0;
	Req.Quantity      = Codes.Num();
	Req.TradeMode     = 1;
	Req.PriceOverride = Total;
	if (UAsdaPurchaseBoxSubsystem* Box = GI ? const_cast<UGameInstance*>(GI)->GetSubsystem<UAsdaPurchaseBoxSubsystem>() : nullptr)
	{
		Box->Open(Req, [this, Codes, bAllTable1](bool bOk)
		{
			if (!bOk) { return; }
			// [ИСПРАВЛЕНИЕ] 25.10.2026: ключ CartOnePerPacket 1 — поштучно (стойка без P85 выдаёт из пакета только позицию 0).
			if (Codes.Num() > 1 && AsdaPremiumCartOnePerPacketOn()) { StartCartRun(Codes); return; }
			// [ИСПРАВЛЕНИЕ] 25.10.2026: смешанная корзина item_list + special_itemlist — двумя пакетами. Байт +1 «таблица» у
			//   пакета один, а CharChargeShopBuy@0x511DA0 в ветке своей таблицы молча пропускает коды другой (не считает и не
			//   выдаёт): у прежнего порта (общий флаг = 0 при любой спец-позиции) товары item_list смешанной корзины пропадали.
			const UGameInstance* GI2 = GetGameInstance();
			const UAsdaItemTableSubsystem* IT2 = GI2 ? GI2->GetSubsystem<UAsdaItemTableSubsystem>() : nullptr;
			TArray<int32> T1, T0;
			for (int32 C : Codes) { if (IT2 && IT2->GetItem(C)) { T1.Add(C); } else { T0.Add(C); } }
			// ★ r2 (25.10.2026, баг владельца «купил 7 — в строке 6»): два пакета давали ДВА заказа = две строки ящика (6 спец +
			//   1 item_list). С GameServer P85 r2 (коды самого пакета в запрос шлюзу) смешанная корзина уходит ОДНИМ пакетом:
			//   байт «таблица» = 1, если есть хоть одна позиция item_list (сервер посчитает их), прочие шлюз разрешит сам.
			//   Ключ asda.PremiumMall.MixedCartTwoPackets 1 — как alpha-2.0.3 (два пакета; для стойки с P85 r1).
			if (T1.Num() > 0 && T0.Num() > 0 && CVarMixedCartTwoPackets.GetValueOnGameThread() == 0)
			{
				UE_LOG(LogAsdaPremShop, Log, TEXT("[премиум-магазин] смешанная корзина одним 5070: %d позиций item_list + %d special_itemlist (таблица 1)"),
					T1.Num(), T0.Num());
				SendBuy(Codes, /*bTable1*/true, /*bToInventory*/true);
				return;
			}
			if (T1.Num() > 0 && T0.Num() > 0)
			{
				CartNextBatch = T0;
				bCartNextTable1 = false;
				UE_LOG(LogAsdaPremShop, Log, TEXT("[премиум-магазин] смешанная корзина: %d позиций item_list сейчас, %d special_itemlist — ")
					TEXT("вторым пакетом после ответа 5071"), T1.Num(), T0.Num());
				SendBuy(T1, /*bTable1*/true, /*bToInventory*/true);
				if (!bPending) { CartNextBatch.Reset(); }   // первый пакет не ушёл — второй не шлём
				return;
			}
			SendBuy(Codes, bAllTable1, /*bToInventory*/true);
		});
	}
}

// Кнопка 58315+20i — покупка набора (режим C общей модалки).
void UAsdaPremiumShopSubsystem::OnPackageBuyClicked(int32 Card)
{
	if (!bOpen || !PkgUi.IsValidIndex(Card)) { return; }
	const int32 Code = PkgUi[Card].ItemCode;
	if (Code <= 0) { return; }
	if (PremiumBuyBlockedByExchange(this, TEXT("покупка набора"))) { return; }   // ★ 29.09.2026: `uiCanInteractWithTarget`
	if (bPending || CartRun.bRun || CartNextBatch.Num() > 0) { NoticeChat(kLocNoAnswer); return; }   // [ИСПРАВЛЕНИЕ] 25.10.2026: и поштучная корзина / второй пакет

	UGameInstance* GI = GetGameInstance();
	AsdaPremiumUiSound(GI, 4);   // ★ 06.11.2026 [ОРИГИНАЛ]: `tradeSelectSlot@0xC406D0` (shop_trade.c:5473) — звук клика карточки
	// ★ Эталон по клику карточки набора СНАЧАЛА открывает панель состава 26500
	//   (`uiShowItemPackageStampWnd`), в режиме ПРЕВЬЮ — она уже портирована.
	if (UAsdaPackagePanelSubsystem* Pkg = GI ? GI->GetSubsystem<UAsdaPackagePanelSubsystem>() : nullptr)
	{
		Pkg->Populate(Code, /*StampMode*/0, /*SrcContainer*/-1, /*SrcSlot*/-1, /*bShowTooltip*/false);
	}

	const UAsdaItemTableSubsystem* IT = GI ? GI->GetSubsystem<UAsdaItemTableSubsystem>() : nullptr;
	const FAsdaItemRecord* Rec = IT ? IT->GetItem(Code) : nullptr;
	const int32 Price = Rec ? Rec->Price : 0;
	if (CashGxp < Price) { NoticeChat(kLocNoGxp); return; }

	FAsdaPurchaseBoxRequest Req;
	Req.Mode      = EAsdaPurchaseMode::Package;
	Req.ItemCode  = Code;
	Req.PanelMode = 0;
	Req.Quantity  = 1;
	Req.TradeMode = 1;
	if (UAsdaPurchaseBoxSubsystem* Box = GI ? GI->GetSubsystem<UAsdaPurchaseBoxSubsystem>() : nullptr)
	{
		Box->Open(Req, [this, Code, bTable1 = (Rec != nullptr)](bool bOk)
		{
			if (!bOk) { return; }
			SendBuy({ Code }, bTable1, /*bToInventory*/true);
		});
	}
}

// Кнопка 6276 «пополнить». ★★★ БРАУЗЕРНЫЕ ОКНА 18000/19000 НЕ ПОРТИРУЕМ (решение владельца):
//   наружу через `LaunchURL`. И ПАРОЛЬ В ССЫЛКУ НЕ КЛАДЁМ: пакет 11002 приносит клиенту `uid`
//   и `passwd`, и оригинал сшивал их в query string
//   (`http://gameshop.fantazium.ru/shop.php?uid=%d&passwd=%s&…`). Такая ссылка попадает в историю
//   браузера и в логи прокси — переносить 1:1 нельзя. Открываем страницу доната БЕЗ учётных
//   данных, ровно тем же способом, каким это уже делает сам оригинал в другой своей кнопке
//   (`UI_OpenDonatePage_ECACA0@0xECACA0`: `ShellExecuteA(…, "open", "https://area-game.ru/profile/donate")`).
void UAsdaPremiumShopSubsystem::OnChargeClicked()
{
	// ★ 27.10.2026 (владелец): внешний браузер не открываем — ссылка на area-game.ru снята до своего сайта.
	UE_LOG(LogAsdaPremShop, Log, TEXT("[премиум-магазин] «пополнить» (6276) → ссылка снята, ничего не открываем"));
}

// Кнопка 6277 «проверить баланс» → опкод 5466 (`net_SendVerifiedRequest5466@0xECADD0`).
void UAsdaPremiumShopSubsystem::OnBalanceClicked()
{
	AsdaShopUi::QueryPremiumBalance(GetGameInstance());   // ★ волна 3: пакет 5466 с выбранным персонажем — общая точка
}

// ═══════════════════════════════ СЕТЬ ══════════════════════════════════════════════════════════
void UAsdaPremiumShopSubsystem::SendBuy(const TArray<int32>& Codes, bool bTable1, bool bToInventory)
{
	if (Codes.Num() == 0) { return; }
	// ★ 29.09.2026: [ПОРТ] повтор гейта `uiCanInteractWithTarget` в момент отправки — между кликом и отправкой у порта стоит
	//   модалка 1559, за время которой окно обмена могло открыться (у эталона проверка стоит в самом отправителе).
	if (PremiumBuyBlockedByExchange(this, TEXT("отправка 5070"))) { return; }
	UGameInstance* GI = GetGameInstance();
	UAsdaNetSubsystem* Net = GI ? GI->GetSubsystem<UAsdaNetSubsystem>() : nullptr;
	if (!Net) { return; }

	uint8 Body[171];
	// `+0` режим витрины: в `cShopDialogResetState` это поле = 1 у обоих отправителей окна 6000.
	UAsdaNetSubsystem::BuildPremiumShopBody(Codes, /*Mode*/1, bTable1, bToInventory, Body);
#if ASDA_DEV_CONSOLE   // [ИСПРАВЛЕНИЕ] ревизия wave_1025 М-6: перехват самотеста — только в дев-сборке
	if (bDebugCapture)
	{
		DebugSent.Add(TArray<uint8>(Body, 171));   // самотест: тело перехвачено, в сокет не уходит
	}
	else
#endif
	if (!Net->SendPremiumShopBuy5070(Body)) { return; }

	// ★ Защёлку «запрос в полёте» ставим ТОЛЬКО после успешной отправки (`this[2071] = 1` эталона).
	bPending = true;
	PendingSince = FPlatformTime::Seconds();   // [ИСПРАВЛЕНИЕ] Д-18: отсчёт таймаута защёлки
	PendingBuyCodes = Codes;
	++CntBuy;
}

// ═══ [ИСПРАВЛЕНИЕ] 25.10.2026 КОРЗИНА ПОШТУЧНО (`asda.PremiumMall.CartOnePerPacket`; разбор — шапка `AsdaPremiumBuyQueue.h`) ═══
void UAsdaPremiumShopSubsystem::StartCartRun(const TArray<int32>& Codes)
{
	CartRun.Start(Codes);
	UE_LOG(LogAsdaPremShop, Log,
		TEXT("[премиум-магазин] корзина поштучно: %d позиций, по одному 5070 (сервер с GP7 выдаёт из пакета только позицию 0)"),
		CartRun.Total);
	SendNextCartItem();
}

void UAsdaPremiumShopSubsystem::SendNextCartItem()
{
	const int32 Code = CartRun.PopNext();
	if (Code <= 0) { StopCartRun(TEXT("позиций не осталось")); return; }
	// Байт +1 «таблица» — по САМОЙ позиции: 1 = item_list, 0 = special_itemlist (у пакета на всю корзину он был общим).
	const UGameInstance* GI = GetGameInstance();
	const UAsdaItemTableSubsystem* IT = GI ? GI->GetSubsystem<UAsdaItemTableSubsystem>() : nullptr;
	const bool bTable1 = (IT && IT->GetItem(Code) != nullptr);
	SendBuy({ Code }, bTable1, /*bToInventory*/true);
	if (!bPending)
	{
		// SendBuy отказал молча (окно обмена, нет сети) — причину он уже записал; остаток не шлём.
		StopCartRun(TEXT("позиция не ушла"));
		return;
	}
	UE_LOG(LogAsdaPremShop, Log, TEXT("[премиум-магазин] корзина поштучно: позиция %d из %d — код %d (таблица %d)"),
		CartRun.Sent, CartRun.Total, Code, bTable1 ? 1 : 0);
}

void UAsdaPremiumShopSubsystem::StopCartRun(const TCHAR* Why)
{
	if (!CartRun.bRun) { return; }
	UE_LOG(LogAsdaPremShop, Log,
		TEXT("[премиум-магазин] корзина поштучно остановлена: %s — куплено %d из %d, не отправлено %d"),
		Why, CartRun.Ok, CartRun.Total, CartRun.Left.Num());
	CartRun.Stop();
}

void UAsdaPremiumShopSubsystem::RemoveBoughtCartRow(int32 ItemCode)
{
	for (FAsdaPremiumCartRow& R : CartRows)
	{
		if (R.ItemCode == ItemCode && R.bChecked) { R = FAsdaPremiumCartRow(); RefreshCart(); return; }
	}
}

// Порт `shop_OnRecvTransactionResult@0xC30BD0`.
void UAsdaPremiumShopSubsystem::OnBuyResult(const TArray<uint8>& B)
{
	if (B.Num() < 407) { return; }
	++CntResult;
	// [ИСПРАВЛЕНИЕ] 25.10.2026: очередь корзины двигаем только ответом на СВОЙ запрос (делегат 5071 общий с окном 15300).
	//   Пока идёт поштучная покупка, чужой ответ (своего запроса в полёте нет) не трогает ни корзину, ни очередь.
	if (CartRun.bRun && !bPending)
	{
		UE_LOG(LogAsdaPremShop, Log, TEXT("[премиум-магазин] 5071 код %d без своего запроса во время покупки корзины — пропущен"), (int32)B[0]);
		return;
	}
	const bool bOurRun = CartRun.bRun;
	const bool bWasPending = bPending;   // [ИСПРАВЛЕНИЕ] 25.10.2026 (М-5): ответ на СВОЙ запрос (а не поздний после Д-18 / чужой)
	bPending = false;

	const uint8 Code     = B[0];
	const bool  bGxp     = (B[1] != 0);                       // ≠0 → GXP, 0 → очки обаяния
	const int32 Balance  = AsdaBytes::RdI32(&B[2]);
	const int32 Weight   = AsdaBytes::RdI16(&B[6]);
	const int32 SubMode  = (int32)B[12];                      // байт +4 записи слота 0
	const bool  bToStore = (SubMode == 5);

	// [ИСПРАВЛЕНИЕ] 26.10.2026: баланс — только из успешного ответа (код 1), как эталон; разбор — у `CVarFailKeepsBalance`.
	if (Code == 1 || CVarFailKeepsBalance.GetValueOnGameThread() == 0)
	{
		CashGxp = Balance;
		RefreshCash();
	}
	else
	{
		UE_LOG(LogAsdaPremShop, Log,
			TEXT("[премиум-магазин] 5071 отказ код %d: баланс из пакета %d не применён (эталон пишет баланс только при коде 1), остаётся %d"),
			(int32)Code, Balance, CashGxp);
	}

	UGameInstance* GI = GetGameInstance();
	const UAsdaItemTableSubsystem* IT = GI ? GI->GetSubsystem<UAsdaItemTableSubsystem>() : nullptr;
	UAsdaTextSubsystem*  TS = GI ? GI->GetSubsystem<UAsdaTextSubsystem>() : nullptr;
	UAsdaMessageSubsystem* MS = GI ? GI->GetSubsystem<UAsdaMessageSubsystem>() : nullptr;

	if (Code == 1)
	{
		// ★ 01.11.2026 P101_CashGift: эталон на успехе 5071 зовёт `uiDialogCloseReset` (shop_trade.c:3470) — панели 6296/6333
		//   закрыты, [2078]/[2072] сброшены, поля и имя получателя очищены.
		CloseGiftPanels();
		// ★★★ УСПЕХ. Строки 20061…20064 несут ДВА маркера: `[ss1]` = имя предмета, `[ss2]` = цена
		//   С РАЗРЯДАМИ. Оба слота СТРОКОВЫЕ (эталон зовёт `CObject_SetSlotString`), поэтому и
		//   маркер `[ss…]`, а не `[sd…]`; разряды ставит ВЫЗЫВАЮЩИЙ (`String_FormatThousands`).
		//   Заполняем через ОБЩУЮ ТОЧКУ `UAsdaTextSubsystem::FormatById`.
		const int32 LocId = bGxp ? (bToStore ? kOkGxpStore : kOkGxpBag)
		                         : (bToStore ? kOkCharmStore : kOkCharmBag);
		for (int32 ItemCode : PendingBuyCodes)
		{
			FString Name; int32 Price = 0;
			if (const FAsdaItemRecord* Rec = IT ? IT->GetItem(ItemCode) : nullptr) { Name = IT->GetItemName(ItemCode); Price = Rec->Price; }   // ★ [ПОРТ] 09.10.2026 (мультиязычность): имя — геттером (язык сессии)
			else
			{
				FAsdaSpecialItemlistRow Sp;
				if (IT && IT->GetSpecialItem(ItemCode, Sp)) { Name = Sp.Name; Price = Sp.Price; }
			}
			// ⚠ Цена за ОЧКИ ОБАЯНИЯ живёт в поле записи +0xB8 (`ItemRecord[46]`), которого в
			//   порт-таблице `DT_item_list` НЕТ. Для этой ветки показываем GXP-цену и пишем
			//   предупреждение — выдумывать число нельзя (закон «не выдумывать»).
			if (!bGxp)
			{
				UE_LOG(LogAsdaPremShop, Warning,
					TEXT("[премиум-магазин] ответ 5071: оплата ОЧКАМИ ОБАЯНИЯ. Цена из поля +0xB8 в "
					     "DT_item_list не выгружена — в нотисе %d показана цена в GXP"), LocId);
			}
			if (MS && TS)
			{
				MS->PostLocFmt(EAsdaMsgCategory::Trade, LocId,
					{ CardLabel(ItemCode, Name), AsdaUi::FormatThousands(Price) }, {});
			}
		}
		if (bOurRun)
		{
			// [ИСПРАВЛЕНИЕ] 25.10.2026 корзина поштучно: позиция подтверждена — снять её строку; следующую шлёт Tick.
			++CartRun.Ok;
			if (PendingBuyCodes.Num() > 0) { RemoveBoughtCartRow(PendingBuyCodes[0]); }
			PendingBuyCodes.Reset();
			if (CartRun.HasMore()) { CartRun.bSendNext = true; }
			else
			{
				UE_LOG(LogAsdaPremShop, Log, TEXT("[премиум-магазин] корзина поштучно: куплено %d из %d"), CartRun.Ok, CartRun.Total);
				CartRun.Stop();
				OnResetClicked();      // корзина отработала целиком — очищаем, как эталон при успехе
			}
		}
		else if (bWasPending && CartNextBatch.Num() > 0)
		{
			// [ИСПРАВЛЕНИЕ] 25.10.2026: первый пакет смешанной корзины подтверждён — второй уйдёт из Tick; его строки в корзине.
			for (int32 C : PendingBuyCodes) { RemoveBoughtCartRow(C); }
			PendingBuyCodes.Reset();
			bCartNextSend = true;
		}
		else
		{
			PendingBuyCodes.Reset();
			// [ИСПРАВЛЕНИЕ] 25.10.2026 (М-5): поздний ответ после таймаута Д-18 (своего запроса в полёте нет) корзину не стирает.
			if (bWasPending) { OnResetClicked(); }   // корзина отработала — очищаем, как эталон при успехе
		}
	}
	else
	{
		// [ИСПРАВЛЕНИЕ] 25.10.2026 корзина поштучно: отказ останавливает очередь; купленное уже снято, остальное в корзине.
		if (bOurRun) { StopCartRun(TEXT("отказ сервера (код 5071 ≠ 1)")); }
		if (CartNextBatch.Num() > 0)   // второй пакет смешанной корзины после отказа не шлём (строки остаются в корзине)
		{
			UE_LOG(LogAsdaPremShop, Log, TEXT("[премиум-магазин] смешанная корзина: отказ — второй пакет (%d позиций) не отправлен"), CartNextBatch.Num());
			CartNextBatch.Reset();
			bCartNextSend = false;
		}
		int32 LocId = (Code < UE_ARRAY_COUNT(kResLoc)) ? kResLoc[Code] : 0;
		// ★ К11: в русском эталоне строки 12961 нет — оригинал показывал чужую запасную строку.
		//   ★ 13.11.2026 (строка 12961): строка записана; 9570 — только если её нет в таблице текста (и ключ 1).
		if (Code == 0x0E && (!AsdaPremiumShow12961On() || AsdaPremium12961Missing(Loc(LocId))) && CVarNotice12961.GetValueOnGameThread() != 0)   // ★ R5-fix-b 13.11.2026 (второй заход): ключ Show12961
		{
			LocId = kResFallback;
			UE_LOG(LogAsdaPremShop, Warning,
				TEXT("[премиум-магазин] код результата 0x0E: %s — показана %d"),   // ★ R5-fix-b 13.11.2026 (третий заход): причина по ключу
				AsdaPremiumShow12961On() ? TEXT("строки 12961 нет в таблице текста (К11)") : TEXT("ключ показа 12961 = 0 (как до 13.11.2026)"), kResFallback);
		}
		if (LocId > 0) { NoticeTip(LocId); }
		else
		{
			UE_LOG(LogAsdaPremShop, Warning, TEXT("[премиум-магазин] 5071: код %d без реакции (как эталон)"), (int32)Code);
		}
		PendingBuyCodes.Reset();
	}

	UE_LOG(LogAsdaPremShop, Log,
		TEXT("[премиум-магазин] 5071: код=%d валюта=%s баланс=%d вес=%d доставка=%s"),
		(int32)Code, bGxp ? TEXT("GXP") : TEXT("обаяние"), Balance, Weight,
		bToStore ? TEXT("хранилище") : TEXT("инвентарь"));
}

void UAsdaPremiumShopSubsystem::OnBalance(uint8 Status, int32 Balance)
{
	// Коды 5467 (`ida.c:721295-721325`): 0 → 12942, 1 → 12943, 2 → 12944.
	// ★★★ ЛОГ ОБЯЗАТЕЛЕН (разбор жалобы «нажимаю ОК и ничего не происходит», 2026-08-28):
	//   покупка отбивается гейтом «не хватает GXP», когда баланс так и остался нулём. Причина
	//   видна ТОЛЬКО здесь: пришёл ли ответ вообще и с каким кодом.
	UE_LOG(LogAsdaPremShop, Log,
		TEXT("[премиум-магазин] 5467 премиум-баланс: статус=%d (%s), баланс=%d (было %d)"),
		(int32)Status,
		Status == 1 ? TEXT("успех") : (Status == 0 ? TEXT("нет данных, нотис 12942")
			: (Status == 2 ? TEXT("charId не совпал, нотис 12944") : TEXT("прочее"))),
		Balance, CashGxp);
	if (Status == 1) { CashGxp = Balance; RefreshCash(); }
	else if (Status == 0) { NoticeChat(12942); }
	else if (Status == 2) { NoticeChat(12944); }
}

// ═══════════════════════════════ ПОДСКАЗКА ═════════════════════════════════════════════════════
// НАША ДОБАВКА (в оригинале у `CShellItemStore` обработчика 8/9/0xA нет). Гасим ТОЛЬКО СВОЮ
//   плашку — закон `ui-shared-tip-ownership`.
void UAsdaPremiumShopSubsystem::TickTooltip(int32 OverCard)
{
	// ★ Волна 3: общая точка `AsdaShopUi::TickCardTooltip` (бустер-шоп ≡ премиум-магазин; код предмета берётся только при OverCard >= 0).
	AsdaShopUi::TickCardTooltip(GetGameInstance(), TipCard, OverCard, EAsdaTipOwner::PremiumShop,
		[this](int32 I) { return Cards[I].ItemCode; });
}
