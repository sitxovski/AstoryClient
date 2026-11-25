// Copyright (c) 2026 @sitxovski (Discord)
// AsdaPossess — реализация гейта действий при одержимости. Шапка с законами, доказательствами и
//   ключами отката — в `Public/World/AsdaPossessGate.h`.
#include "World/AsdaPossessGate.h"
#include "AsdaDevConsole.h"   // ★ античит А2: ASDA_DEV_CVAR / ASDA_DEV_CONSOLE — дев-ключи и команды вне Shipping

#include "AsdaNetSubsystem.h"
#include "Text/AsdaTextSubsystem.h"
#include "UI/AsdaMessageSubsystem.h"
#include "UI/AsdaWindowSubsystem.h"

#include "Engine/GameInstance.h"
#include "HAL/IConsoleManager.h"

DEFINE_LOG_CATEGORY_STATIC(LogAsdaPossessGate, Log, All);

// ═════════════════════════════════ КЛЮЧИ ═══════════════════════════════════════════════════════

// ★ КЛЮЧ ОТКАТА ВСЕЙ ВЕТКИ. 1 — гейт работает (умолчание: без него порт позволяет одержимому
//   отправлять действия, которые сервер всё равно отобьёт, а игрок видит либо чужой код ошибки,
//   либо вообще ничего). 0 — гейта нет, поведение порта возвращается к состоянию до этой правки.
static ASDA_DEV_CVAR(int32, CVarPossessActionGate,
	TEXT("asda.Possess.ActionGate"), 1,
	TEXT("1 = не отправлять серверу действия, запрещённые при одержимости (SoulState==2); 0 = выключить гейт"),
	ECVF_Default);

// ★ КЛЮЧ ФОРМЫ ОТКАЗА. 0 — показать игроку строку оригинала (умолчание), 1 — отказать молча.
//   ПОЧЕМУ УМОЛЧАНИЕ 0, А НЕ 1: разбор утверждал, что оригинал блокирует МОЛЧА, но проверка по
//   `GameServer.c` это опровергла — сервер на КАЖДЫЙ из запрещённых опкодов отвечает пакетом
//   с кодом отказа, а клиент оригинала печатает по нему строку (8013 / 12165 / 12167 / 12168 / 8027).
//   Значит «с нотисом» — это и есть 1:1 оригинал, а «молча» — отступление от него. Ключ оставлен
//   на случай, если владелец захочет ровно ту тишину, которую описывал разбор.
static ASDA_DEV_CVAR(int32, CVarPossessSilentBlock,
	TEXT("asda.Possess.SilentBlock"), 0,
	TEXT("0 = при отказе показать нотис оригинала (8013/12165/12167/12168/8027); 1 = отказать молча, без строки"),
	ECVF_Default);

// ★ КЛЮЧ ДЛЯ СТРОК, ГДЕ СЕРВЕРНЫЙ ГЕЙТ УСЛОВЕН (флаг `bServerConditional`: 5018 и 5024).
//   ЗАЧЕМ ОТДЕЛЬНЫЙ КЛЮЧ. На этих двух опкодах сервер запрещает НЕ ВСЁ действие, а лишь часть
//   корзины/режимов, и условие считается по СЕРВЕРНОЙ таблице предметов, которой у клиента нет
//   (подробности — в комментариях самих строк таблицы ниже и в отчёте
//   `fix_possess_gate_review_fixes.md` §5). Пока условие не воспроизведено, мы режем ШИРЕ
//   оригинала, и это осознанное отступление. Ключ позволяет владельцу снять именно эти две
//   строки, не выключая весь гейт: 1 — резать (умолчание, безопасно: ни один запрещённый пакет
//   не уйдёт), 0 — пропускать их на провод, и тогда отказ приходит от сервера (5019 код 2 → 8013
//   в `AsdaNpcShopSubsystem::OnSellResult`, 5025 код 9 → 8013 в `UAsdaSowelSubsystem`), то есть
//   ровно как в оригинале, но с лишним кругом по проводу.
static ASDA_DEV_CVAR(int32, CVarPossessGateConditional,
	TEXT("asda.Possess.GateConditional"), 1,
	TEXT("1 = резать и опкоды с УСЛОВНЫМ серверным гейтом (5018/5024); 0 = пропускать их, отказ отдаст сервер"),
	ECVF_Default);

// ═════════════════════════════ ТАБЛИЦА ЗАПРЕЩЁННЫХ ОПКОДОВ ═════════════════════════════════════

namespace
{
	// ★ Куда оригинал кладёт строку отказа. Снято ПООТКРЫТИЮ каждого эталонного обработчика ответа
	//   (ссылка на файл:строку — в комментарии каждой строки таблицы). Это БИТОВАЯ МАСКА, а не
	//   одиночный выбор: у 5466/5468 эталон печатает строку СРАЗУ двумя вызовами подряд, и разными
	//   цветами, поэтому «один приёмник» его поведение не описывает.
	namespace ESink
	{
		enum : uint8
		{
			// Оригинал по этому коду отказа НЕ печатает НИЧЕГО (или печатает не loc-строку).
			//   Выдумывать сюда строку запрещено — закон `never-invent-only-original`.
			None   = 0,
			// `CChat_AddTokenizedLines(g_pChatWnd, txt, цвет, 10000, 0)` — системный чат (журнал 2572).
			Chat   = 1,
			// `CControl_SetTooltipText(g_pChatWnd, txt, цвет, 10000)` — плашка WARMESSAGE_WIN 221.
			Plank  = 2,
			// `CItem_BuildTooltipText(g_pItemTooltipUI, 0, -1, txt, 1)` — модалка ONEBUT_MSGBOX 1500.
			MsgBox = 4,
		};
	}

	// Одна строка таблицы = один исходящий опкод, на котором у сервера стоит `sub_52EE40`.
	struct FPossessGateRow
	{
		uint16              Opcode;      // что клиент шлёт (C->S)
		int32               LocId;       // строка DT_GameText из эталона; 0 = строки у оригинала НЕТ
		EAsdaMsgCategory    Category;    // канал маршрутизатора (DT_MessageRoutes)
		uint8               Sinks;       // маска ESink — приёмники, как в эталоне
		int32               ChatColor;   // ЗНАКОВЫЙ эталонный цвет чата  (0 = приёмник не задействован)
		int32               PlankColor;  // ЗНАКОВЫЙ эталонный цвет плашки (0 = приёмник не задействован)
		bool                bServerConditional; // серверный гейт условен ⇒ мы режем ШИРЕ оригинала
	};

	// Эталонный цвет — знаковое 32-битное `0xAARRGGBB` (−11514 = 0xFFFFD306 золотой,
	//   −65536 = 0xFFFF0000 красный, −7930543 = 0xFF86FD51, −8847556 = 0xFF78FF3C).
	FColor EtalonColor(int32 SignedArgb)
	{
		const uint32 U = (uint32)SignedArgb;
		return FColor((uint8)((U >> 16) & 0xFF), (uint8)((U >> 8) & 0xFF),
		              (uint8)(U & 0xFF),         (uint8)((U >> 24) & 0xFF));
	}

	// TTL строки отказа. Эталон во ВСЕХ разобранных ветках кладёт 10000 мс — и в чат, и в плашку.
	constexpr float kEtalonTtlMs = 10000.f;

	// ★★★ ПОЛНАЯ ТАБЛИЦА. Каждая строка снята с call-site `sub_52EE40` в `GameServer.c`, сверена
	//   с ответом сервера И С ТЕЛОМ ЭТАЛОННОГО ОБРАБОТЧИКА ЭТОГО ОТВЕТА (файл:строка — в каждой
	//   строке). Порядок — по опкоду. Разбор «опкод → серверная функция → ответ/код» — в отчётах
	//   `fix_possess_action_gate.md` §2 и `fix_possess_gate_review_fixes.md`.
	//
	//   ★★★ ДВЕ РАЗНЫЕ СЕМЬИ СТРОК — НЕ ПУТАТЬ (ревизия 2026-09-03, `review_possess_action_gate.md`):
	//     • **8013** «Эта возможность доступна Друзьям.» — СТАРАЯ предметно-торговая семья
	//       (5003 / 5007 / 5011 / 5019 / 5023 / 5025 / 5071). Это и есть настоящая строка отказа
	//       по одержимости для предметов и торговли.
	//     • **12165** «Друзьям это недоступно» — ТОЛЬКО премиум-семья (5451 / 5467 / 5469 / 5471).
	//   Первая редакция таблицы ставила 12165 всем подряд — 8 строк из 19 были неверны. Прежде чем
	//   менять здесь loc-id, ОТКРОЙ обработчик ответа в `RE/etalon/` по ссылке из строки.
	//
	//   ⚠ Гейта НЕТ (и добавлять его нельзя, закон never-invent-only-original) на: покупке у NPC
	//   5020, укладке НА склад 5008, надевании/снятии экипировки, движении, бое, навыках, чате,
	//   расходниках и перекладывании внутри своего инвентаря — в этих обработчиках предиката нет.
	static const FPossessGateRow kGatedOpcodes[] =
	{
		// 4104 → `ClanCreateReq @0x4017B0` (GameServer.c:353) → 4105 код 10 → loc 12168, ONEBUT_MSGBOX
		//   (эталон `network.c:11959`, `Recv_AuthPassword@0xBC8CD0` case 0xA).
		//   ⚠ ЛОВУШКА: по смыслу сюда просится 12166 «…ты не можешь создать клан», но оригинал
		//   печатает ИМЕННО 12168. Не «чинить».
		{ 4104, 12168, EAsdaMsgCategory::Social,      ESink::MsgBox,             0,        0, false },
		// 4106 → `ClanDeleteReq @0x402090` (:587) → 4107 код 4 → loc 12167, MSGBOX
		//   (эталон `ui_core.c:17134`, `uiInvenNoticeByCode@0xBC94A0` case 4).
		{ 4106, 12167, EAsdaMsgCategory::Social,      ESink::MsgBox,             0,        0, false },
		// 4108 → `ClanMemberInvite_RequestUser @0x4025A0` (:742 сам, :752 цель) → 4109 код 8 → 12168,
		//   MSGBOX (эталон `network.c:12096`, `Recv_ClanMemberLeave_Notice@0xBC9940` case 8).
		{ 4108, 12168, EAsdaMsgCategory::Social,      ESink::MsgBox,             0,        0, false },
		// 5002 → `CharItemDrop @0x4D2690` (:83382 безусловно, :83399/:83497 по признаку +57) →
		//   5003 код 2 → **8013**, ТОЛЬКО чат, цвет −7930543 (эталон `network.c:17408-17411`,
		//   `Recv_ItemEquipStat@0xD0F350`, ветка `LOBYTE(packet[0])==2`). ⚠ Цвет здесь НЕ −11514.
		{ 5002,  8013, EAsdaMsgCategory::ItemRefused, ESink::Chat,       -7930543,        0, false },
		// 5006 → `CharItemDump @0x4D3410` (:83619) → 5007 код 2 → **8013**, чат −11514
		//   (эталон `network.c:18280-18283`, `netOnInventoryUseResult@0xD155A0`, `packet[0]==2`).
		{ 5006,  8013, EAsdaMsgCategory::ItemRefused, ESink::Chat,          -11514,        0, false },
		// 5010 → `CharItemRetake @0x50B3D0` (:102902) → 5011 код 4 → **8013**, ONEBUT_MSGBOX
		//   (эталон `network.c:30873-30875`, `Recv_InventorySwap2@0xE874F0` case 4 —
		//   `CItem_BuildTooltipText`, а НЕ чат).
		//   ⚠ ВЗЯТЬ со склада запрещено, а ПОЛОЖИТЬ (5008 `CharItemKeeping @0x5094A0`) — РАЗРЕШЕНО:
		//   предиката в нём нет. Это не опечатка разбора, а поведение сервера — 1:1.
		{ 5010,  8013, EAsdaMsgCategory::ItemRefused, ESink::MsgBox,             0,        0, false },
		// 5018 → `CharItemSell @0x50D240` → 5019 код 2 → **8013**, чат −11514
		//   (эталон `network.c:28567-28570`, `Recv_ShopBuy_Result@0xE31630`, `resultPkt[0]==2`).
		//   ★ УСЛОВНЫЙ ГЕЙТ СЕРВЕРА, МЫ РЕЖЕМ ШИРЕ (флаг ниже, ключ asda.Possess.GateConditional):
		//     :103441 — только для записей корзины с контейнером `ref+4 == 1` (там безусловно);
		//     :103508 — только если код предмета в диапазоне САМОЦВЕТОВ (`sub_532AE0`: 7001…предел);
		//     :103540 — только если предмет обычный (`sub_532A90`: 20551…предел) И у него взведён
		//               байт **+57** серверной 71-байтовой записи (`dword_5F5E98`) — «привязан
		//               к владельцу». Этого байта в клиентской таблице предметов НЕТ (её запись
		//               248 б, раскладка другая), а подменять его похожим клиентским полем нельзя:
		//               закон `client-server-table-desync` (таблицы клиента и сервера уже расходились).
		{ 5018,  8013, EAsdaMsgCategory::Trade,       ESink::Chat,          -11514,        0, true  },
		// 5022 → `CharCellobItemDetach @0x4DBC30` (:86968) → 5023 код 5 → **8013**, чат −11514
		//   (эталон `network.c:17494-17496`, `netRecvSowelActionResult@0xD0F7D0` case 5). Безусловен.
		{ 5022,  8013, EAsdaMsgCategory::ItemRefused, ESink::Chat,          -11514,        0, false },
		// 5024 → `CharCellobItemMove @0x4DC390` (:87161, :87241) → 5025 код 9 → **8013**, чат −11514
		//   (эталон `network.c:17565-17568`, `netRecvItemActionResult@0xD0FB10` case 9).
		//   ★ УСЛОВНЫЙ ГЕЙТ СЕРВЕРА, МЫ РЕЖЕМ ШИРЕ (см. ключ asda.Possess.GateConditional):
		//     оба call-site лежат внутри ветки `!sub_532A90(код цели) || серверный_item[+27] != 10
		//     || серверный_item[+33] != 1` (:87144) и срабатывают только для режимов `body[0]==1`
		//     и `body[0]==3`, да и то когда ОБА кода — самоцветы (`sub_532AE0`). Байты +27/+33
		//     серверной записи предмета клиенту недоступны ⇒ условие не воспроизвести.
		{ 5024,  8013, EAsdaMsgCategory::ItemRefused, ESink::Chat,          -11514,        0, true  },
		// 5070 → `CharChargeShopBuy @0x511DA0` (:104654) → 5071 код 11 → **8013**, MSGBOX
		//   (эталон `shop_trade.c:3528-3531`, `shop_OnRecvTransactionResult@0xC30BD0` case 0xB —
		//   `CItem_BuildTooltipText`, а НЕ чат).
		//   ⚠ Серверное условие `sub_52EE40(...) && v29[0] == 1` формально условно, но `v29[0]` —
		//   режим витрины, и порт (`BuildPremiumShopBody`), как и эталон, ВСЕГДА кладёт 1
		//   (`AsdaNetSubsystem.h:2005`) ⇒ условие тождественно истинно, расхождения нет.
		{ 5070,  8013, EAsdaMsgCategory::Trade,       ESink::MsgBox,             0,        0, false },
		// 5204 → `RequestUser_SoulMateDestory @0x4931D0` (:58738) → 5205 код 1 → loc 8027
		//   «Не удается отказаться от Друга», чат −8847556 (эталон `item_inventory.c:57160-57162`;
		//   порт уже печатает её в `UAsdaSoulmateSubsystem::OnBreakRefused`).
		{ 5204,  8027, EAsdaMsgCategory::Social,      ESink::Chat,        -8847556,        0, false },
		// 5282 → `CharExchangeReq @0x4DF830` (:88243 сам, :88248 цель) → 5283 код 10 (сам) / 11 (цель).
		//   ★★★ СТРОКИ У ОРИГИНАЛА НЕТ — и выдумывать её нельзя (`never-invent-only-original`).
		//   Эталонный обработчик `Recv_PartyMember@0xDF3480` (`network.c:27284`): `case 11`
		//   ОТСУТСТВУЕТ вовсе (уходит в `default: return`, то есть тишина), а `case 10`
		//   (`network.c:27394-27400`) печатает СЫРОЙ строковый литерал `aPc` (не `UI_FormatLocalizedNotice`,
		//   не loc-id) и добавляет иконку в панель оповещений. Ни то, ни другое нашим приёмником
		//   не воспроизводится, поэтому режем МОЛЧА — ровно столько, сколько говорит оригинал.
		{ 5282,     0, EAsdaMsgCategory::Trade,       ESink::None,               0,        0, false },
		// 5450 → `Char_FunctionItem_Use @0x4ED780` (:91748) → 5451 код 0x14 → loc 12165,
		//   ТОЛЬКО ПЛАШКА −11514 (эталон `network.c:36729-36734`: один `CControl_SetTooltipText`,
		//   дубля в чат НЕТ). Связка кода 0x14 → 12165 в порту уже есть (`AsdaInventorySubsystem:3075`).
		{ 5450, 12165, EAsdaMsgCategory::ItemRefused, ESink::Plank,              0,  -11514, false },
		// ★ 06.10.2026 (волна 2-Г): 5458 → `RecvClient_FunctionItem_Meet @0x4F33E0` (:93786) → 5459 код 7 → loc 12165,
		//   ТОЛЬКО ПЛАШКА −11514 (эталон `uiTooltip_ShowItemStatusNotice@0xF26880` case 7, ui_core.c:97978-97980 —
		//   `CControl_SetTooltipText(g_pChatWnd, …, −11514, 10000)`; ★ прежняя строка реестра «loc 109410» относилась к
		//   ДРУГОМУ обработчику `recvItemActionResult_ShowNotice` — открыт обработчик 0x1553, закон loc-per-handler).
		{ 5458, 12165, EAsdaMsgCategory::ItemRefused, ESink::Plank,              0,  -11514, false },
		// 5466 →`Recv_MacValue_Inquiry @0x4F4620` (:94187) → 5467 код 3 → loc 12165, чат −11514
		//   И плашка −11514 (эталон `network.c:32226-32229` — два вызова подряд).
		{ 5466, 12165, EAsdaMsgCategory::Trade,       (uint8)(ESink::Chat | ESink::Plank), -11514, -11514, false },
		// 5468 → `Recv_GachaItemUse @0x4F48C0` (:94307) → 5469 код 8 → loc 12165, чат −11514
		//   и плашка **−65536 (КРАСНЫЙ)** — цвета РАЗНЫЕ (эталон `network.c:44511-44515`).
		{ 5468, 12165, EAsdaMsgCategory::ItemRefused, (uint8)(ESink::Chat | ESink::Plank), -11514, -65536, false },
		// 5470 → `Recv_GachaItemUse_0 @0x4F59C0` (:94708) → 5471 код 5 → loc 12165, ТОЛЬКО чат
		//   −11514 (эталон `shop_trade.c:6272-6275`, плашки нет).
		{ 5470, 12165, EAsdaMsgCategory::Trade,       ESink::Chat,          -11514,        0, false },
		// ★ 06.10.2026 (волна 2-Г): 6134 → `RecvClient_FunctionItem_Summon @0x4F2900` (:93527) → 6135 код 3 → loc 104008
		//   «Ты входишь как Друг», ONEBUT_MSGBOX (эталон `netRecvInventoryItemUpdate@0xDCE180` case 3, network.c:26505-26509 —
		//   `CItem_BuildTooltipText(…, 0, −1, …)`).
		{ 6134, 104008, EAsdaMsgCategory::ItemRefused, ESink::MsgBox,            0,        0, false },
		// 9901 →`Recv_TransAction_RegistItemReq @0x517AB0` (:106906) → 9902 код 1.
		//   ★ ОСОЗНАННОЕ ОТСТУПЛЕНИЕ (`fix-original-defects`, откат — asda.Possess.SilentBlock 1):
		//   у биржи код 1 — ОБЩИЙ «не получилось». Эталон печатает по нему 12677 «Не удалось
		//   зарегистрировать предмет» (`network.c:20783-20791`, чат −11514 + тултип), то есть
		//   ту же строку, что и ещё в десятке ветвей отказа, — про одержимость игрок не узнаёт.
		//   Мы печатаем **8013** — строку ЭТОЙ ЖЕ семьи запретов (не выдуманную: её печатают
		//   5003/5007/5011/5019/5023/5025/5071 того же предиката `sub_52EE40`).
		{ 9901,  8013, EAsdaMsgCategory::Trade,       ESink::Chat,          -11514,        0, false },
		// 9905 → `Recv_TransAction_BuyItemReq @0x51AA10` (:107867) → 9906 код 1 (то же отступление;
		//   9906 уходит в `npcShop_DispatchResult(…, 4, 1)` → `itemMediationDispatchSlotResult`).
		{ 9905,  8013, EAsdaMsgCategory::Trade,       ESink::Chat,          -11514,        0, false },
		// 9909 → `Recv_TransAction_DeleteItemReq @0x51E380` (:108936) → 9910 код 1 (то же отступление;
		//   9910 уходит в `npcShop_DispatchResult(…, 3, 1)` → `boosterShopWnd_OnItemListPacket`).
		{ 9909,  8013, EAsdaMsgCategory::Trade,       ESink::Chat,          -11514,        0, false },
	};

	// Поиск строки таблицы по опкоду. nullptr — гейта на этом опкоде нет.
	const FPossessGateRow* FindRow(uint16 Opcode)
	{
		for (const FPossessGateRow& R : kGatedOpcodes)
		{
			if (R.Opcode == Opcode) { return &R; }
		}
		return nullptr;
	}

	// Показ строки отказа ровно теми приёмниками и цветами, которыми её показывает оригинал.
	//   Чат и плашка отправляются ОТДЕЛЬНЫМИ сообщениями: у 5468 эталон красит их по-разному
	//   (чат золотой −11514, плашка красная −65536), а `FAsdaMsgParams` несёт один цвет на все цели.
	void ShowRefusal(UGameInstance* GI, const FPossessGateRow& Row)
	{
		// Оригинал по этому отказу ничего не печатает — и мы молчим (never-invent-only-original).
		if (Row.Sinks == ESink::None || Row.LocId == 0) { return; }
		if (!GI) { return; }
		UAsdaTextSubsystem* TS = GI->GetSubsystem<UAsdaTextSubsystem>();
		const FString Text = TS ? TS->GetTextById(Row.LocId) : FString();
		if (Text.IsEmpty())
		{
			// Строки нет в DT_GameText — молчать нельзя (молчание прячет причину отказа),
			//   поэтому уходим в маршрутизатор по id: он сам поставит диагностический плейсхолдер
			//   при `asda.Msg.ShowMissingLoc 1`.
			if (UAsdaMessageSubsystem* M = GI->GetSubsystem<UAsdaMessageSubsystem>())
			{
				M->PostLoc(Row.Category, Row.LocId);
			}
			return;
		}

		UAsdaMessageSubsystem* Msg = GI->GetSubsystem<UAsdaMessageSubsystem>();

		// Однокнопочная модалка 1500 — так эталон печатает клановые отказы, 5011 код 4 и 5071 код 11.
		if ((Row.Sinks & ESink::MsgBox) != 0)
		{
			if (UAsdaWindowSubsystem* WM = GI->GetSubsystem<UAsdaWindowSubsystem>())
			{
				WM->ShowGameMessage(Text, 2288 /*AsdaLoc::kBtnOk*/, Row.Category);
			}
			else if (Msg)
			{
				// Окон ещё нет (ранняя фаза) — не терять строку, отдать её маршрутизатору.
				Msg->PostText(Row.Category, Text);
			}
		}

		if (!Msg) { return; }

		// Одна цель — один Post: цвет у чата и у плашки в эталоне может отличаться.
		auto PostTo = [&](EAsdaMsgTarget Target, int32 SignedColor)
		{
			FAsdaMsgParams P;
			P.Category  = Row.Category;
			P.Text      = Text;
			P.SrcOpcode = (int32)Row.Opcode;
			P.Targets   = (int32)Target;
			P.bUseColor = true;
			P.Color     = EtalonColor(SignedColor);
			P.TtlMs     = kEtalonTtlMs;
			Msg->Post(P);
		};
		if ((Row.Sinks & ESink::Chat)  != 0) { PostTo(EAsdaMsgTarget::SystemChat, Row.ChatColor);  }
		if ((Row.Sinks & ESink::Plank) != 0) { PostTo(EAsdaMsgTarget::WarWindow,  Row.PlankColor); }
	}
}

// ═════════════════════════════════ ПУБЛИЧНЫЕ ФУНКЦИИ ═══════════════════════════════════════════

bool AsdaPossess::IsPossessed(const UAsdaNetSubsystem* Net)
{
	// С 2026-09-03 признак — производная единого `SelfSoulState` (== 2); поведение прежнее,
	//   зеркало `bSelfPossessed` держит в согласии сеттер `SetSelfSoulState`.
	return Net != nullptr && Net->IsSelfPossessed();
}

bool AsdaPossess::IsPossessed(const UGameInstance* GameInstance)
{
	if (!GameInstance) { return false; }
	// `GetSubsystem` неконстантен — снимаем константность только ради чтения признака.
	UGameInstance* GI = const_cast<UGameInstance*>(GameInstance);
	return IsPossessed(GI->GetSubsystem<UAsdaNetSubsystem>());
}

bool AsdaPossess::IsGatedOpcode(uint16 Opcode)
{
	return FindRow(Opcode) != nullptr;
}

bool AsdaPossess::BlockOutgoing(const UAsdaNetSubsystem* Net, uint16 Opcode)
{
	if (CVarPossessActionGate.GetValueOnGameThread() == 0) { return false; }
	if (!IsPossessed(Net)) { return false; }

	const FPossessGateRow* Row = FindRow(Opcode);
	if (!Row) { return false; }

	// Строки с УСЛОВНЫМ серверным гейтом (5018/5024) можно снять отдельным ключом: сервер там
	//   режет не всё, и при `asda.Possess.GateConditional 0` мы отдаём решение ему, как в оригинале.
	if (Row->bServerConditional && CVarPossessGateConditional.GetValueOnGameThread() == 0)
	{
		UE_LOG(LogAsdaPossessGate, Log,
			TEXT("[одержимость] опкод %u пропущен на провод: серверный гейт условен, ключ asda.Possess.GateConditional 0"),
			(uint32)Opcode);
		return false;
	}

	const bool bSilent = (CVarPossessSilentBlock.GetValueOnGameThread() != 0);
	if (!bSilent)
	{
		ShowRefusal(Net ? Net->GetGameInstance() : nullptr, *Row);
	}
	UE_LOG(LogAsdaPossessGate, Log,
		TEXT("[одержимость] опкод %u не отправлен: действие запрещено при SoulState==2 (нотис %d%s%s)"),
		(uint32)Opcode, Row->LocId,
		Row->LocId == 0 ? TEXT(" — у оригинала строки нет, режем молча") : TEXT(""),
		bSilent ? TEXT(", подавлен ключом asda.Possess.SilentBlock") : TEXT(""));
	return true;
}

// ═══════════════════════ РЕЖИМ «ДУША» (SoulState == 1) — ВТОРАЯ ТАБЛИЦА ═══════════════════════
//   Шапка с законами, доказательствами и ключами — в `Public/World/AsdaPossessGate.h`
//   (раздел `namespace AsdaSoul`). Ground truth: `Reports/soul_state_1_system.md` §3 +
//   ревизия `review_soul_state_1_system.md` §3; каждая строка сверена с `case` диспетчера
//   `GameServer.c` САМОСТОЯТЕЛЬНО (номер строки — в комментарии строки таблицы).

DEFINE_LOG_CATEGORY_STATIC(LogAsdaSoulGate, Log, All);

// ★ КЛЮЧ ВХОДА. 1 — слать 1016 режимом 1 (умолчание — решение владельца 2026-09-03, отступление
//   от оригинала: `entity_char.c:51785-51790` при dealMode==1 показывает подсказку 33 и выходит).
//   0 — поведение оригинала: `SendSelectChar(Mode=1)` возвращает false, пакет не уходит.
static ASDA_DEV_CVAR(int32, CVarSoulAllowEnter,
	TEXT("asda.Soul.AllowEnter"), 1,
	TEXT("1 = отправлять 1016 режимом 1 («душа», решение владельца); 0 = как оригинал — не отправлять"),
	ECVF_Default);

// ★ КЛЮЧ ОТКАТА ВСЕЙ ВЕТКИ ГЕЙТА. 1 — при SoulState==1 запрещённые опкоды не уходят на провод
//   (умолчание); 0 — гейта нет, пакеты уходят, и сервер молча их проглатывает (`sub_52EEA0`).
static ASDA_DEV_CVAR(int32, CVarSoulActionGate,
	TEXT("asda.Soul.ActionGate"), 1,
	TEXT("1 = не отправлять серверу действия, запрещённые в режиме «душа» (SoulState==1); 0 = выключить гейт"),
	ECVF_Default);

// ★ КЛЮЧ ФОРМЫ ОТКАЗА. 1 — молча (умолчание). ПОЧЕМУ УМОЛЧАНИЕ 1, А НЕ 0 (у одержимости наоборот):
//   у одержимости сервер отвечает кодом, и клиент оригинала печатает строку, — там «с нотисом» и
//   есть 1:1. Здесь же все 71 call-site в диспетчере — `if(!sub_52EEA0) handler;` без else и без
//   ответа (ревизия §2 прочитала ветки боя, воскрешения, склада, лавки): оригинал МОЛЧИТ, значит
//   молчание — 1:1, а нотис — наше отступление, включаемое ключом.
static ASDA_DEV_CVAR(int32, CVarSoulSilentBlock,
	TEXT("asda.Soul.SilentBlock"), 1,
	TEXT("1 = отказывать молча, как оригинал (сервер пакет проглатывает без ответа); 0 = показать нотис asda.Soul.NoticeLoc"),
	ECVF_Default);

// ★ LOC-ID СТРОКИ НОТИСА при `SilentBlock 0`. Умолчание 0 = молчать даже при SilentBlock 0:
//   в `DT_GameText` подходящей строки НЕ БЫЛО (поиск «призрак» / «наблюдател» / «в облике» — 0
//   совпадений; строки «в таком состоянии…» 1019/1101/7007 принадлежат гейтам обездвиживания и
//   на душу их переносить — выдумка). ★ Владелец завёл строку 2026-09-03: **990131**
//   «Запрещено в безопасном режиме» (`RawAssets/Text/GameText.csv`, наш диапазон 9901xx, рядом с
//   990130 «Делиться»). Текст — формулировка владельца, не эталонная (эталон молчит).
static ASDA_DEV_CVAR(int32, CVarSoulNoticeLoc,
	TEXT("asda.Soul.NoticeLoc"), 990131,
	TEXT("loc-id строки нотиса при asda.Soul.SilentBlock 0; 0 = строки нет, молчать"),
	ECVF_Default);

namespace
{
	// Одна строка = один исходящий опкод, на `case` которого в диспетчере стоит `!sub_52EEA0`.
	struct FSoulGateRow
	{
		uint16            Opcode;     // что клиент шлёт (C->S)
		EAsdaMsgCategory  Category;   // канал маршрутизатора для НАШЕГО нотиса (у оригинала строки нет)
		const TCHAR*      What;       // человекочитаемое имя действия — только для лога
	};

	// ★★★ ПОЛНАЯ ТАБЛИЦА (62 опкода = все из 71 диспетчерских, которые порт умеет отправлять; ★ 27.09.2026 +4111; ★ 29.09.2026 +5284;
	//   ★ 29.09.2026 +5288 +5290 — волна О2; ★ 30.09.2026 +5291 +5293 — волна О3; ★ 06.10.2026 +4070 — волна 1-Б; ★ 06.10.2026 +5458 — волна 2-Г).
	//   У каждой строки — строка `GameServer.c` с `case` и серверный обработчик. Порядок — как в
	//   диспетчере (по группам), чтобы сверять с ним построчно. Остальные 9 (6316,
	//   5475, 5267, 5016, 5026, 5033, 5048, 4032, 4114; ★ 07.10.2026: 5045 — в таблице с волны 1-А)
	//   порт не отправляет — `PORT_PENDING_ON_SYSTEMS.md`, раздел «Режим „душа“».
	//   ★ ИСПРАВЛЕНО 06.10.2026 (волна 1-Б): прежняя запись «4070 порт не шлёт» устарела — окно настроек
	//     69500 шлёт 4070 (`UAsdaSystemOptionSubsystem::SendSettingsPacket4070` → `UAsdaNetSubsystem::
	//     SendOptionFlags4070` → `SendMd5Signed`, а там общий шлюз с битом `EAsdaGate::Soul`), а сервер
	//     гейтит его так же, как остальные: `GameServer.c:20156-20161` `case 0xFE6: if (!sub_52EEA0(…))
	//     sub_52A0C0(…)`. Без строки таблицы «душа» слала бы 4070, который сервер МОЛЧА глотает.
	//
	//   ⚠ ГЕЙТА НЕТ (и ставить нельзя, `never-invent-only-original`) на: движении 4006/4008
	//   (`:20102/20107`), самолавке 0x189E…0x18A9, рефайне/разборе 0x1964…0x196A, кланадмине
	//   0x196C…0x1988, HP/MP цели 0x198A, EventDungeon, гаче-рулетке 0x19AF…0x19B5, крафте
	//   0x19C6…0x19CC, сквад-блоке 0x1A2C…0x1A4E, блоке 0x1777…0x182D, портале/логоне/логауте.
	static const FSoulGateRow kSoulGatedOpcodes[] =
	{
		// ── Биржа (сравнение ДЕСЯТИЧНОЕ `v4 == …` и `case 0x26B…`) ──
		{ 9915, EAsdaMsgCategory::Trade,         TEXT("биржа: открыть")        }, // :18820 v4==9915 → sub_521770
		{ 9901, EAsdaMsgCategory::Trade,         TEXT("биржа: выставить лот")  }, // :18864 v4==9901 → Recv_TransAction_RegistItemReq
		{ 9905, EAsdaMsgCategory::Trade,         TEXT("биржа: купить лот")     }, // :18835 case 0x26B1 → Recv_TransAction_BuyItemReq
		{ 9909, EAsdaMsgCategory::Trade,         TEXT("биржа: снять лот")      }, // :18849 case 0x26B5 → Recv_TransAction_DeleteItemReq
		// ── Обмен игрок↔игрок (0x14A2…0x14AD; порт шлёт 5282, 5284, 5286, 5288, 5290, 5291 и 5293 — все клиентские опкоды блока) ──
		{ 5282, EAsdaMsgCategory::Trade,         TEXT("обмен: запрос")         }, // :19644 case 0x14A2 → CharExchangeReq
		// ★ 29.09.2026 (волна О1 торговли): порт шлёт 5284 (ответ на заявку: 1 принять / 0 отказ / 3 «занят»).
		{ 5284, EAsdaMsgCategory::Trade,         TEXT("обмен: ответ на заявку") }, // :19650 case 0x14A4 → CharExchangeAcceptReq
		{ 5286, EAsdaMsgCategory::Trade,         TEXT("обмен: отмена")         }, // :19656 case 0x14A6 → CharExchanageCancelReq
		// ★ 29.09.2026 (волна О2 торговли): порт шлёт 5288 (в лоток) и 5290 (с лотка) окна 2008.
		{ 5288, EAsdaMsgCategory::Trade,         TEXT("обмен: в лоток")        }, // :19662 case 0x14A8 → CharExchangeDragDropItem
		{ 5290, EAsdaMsgCategory::Trade,         TEXT("обмен: с лотка")        }, // :19668 case 0x14AA → CharExchangeDragDropDeleteItem
		// ★ 30.09.2026 (волна О3 торговли): порт шлёт 5291 (готовность 2048) и 5293 (итог 2011).
		{ 5291, EAsdaMsgCategory::Trade,         TEXT("обмен: готовность")     }, // :19674 case 0x14AB → CharExchangeFixReq
		{ 5293, EAsdaMsgCategory::Trade,         TEXT("обмен: итог")           }, // :19680 case 0x14AD → CharExchangeCompleteReq
		// ── Функциональные предметы и гача (0x154A…0x1563) ──
		{ 5450, EAsdaMsgCategory::ItemRefused,   TEXT("функц. предмет")        }, // :19722 case 0x154A → Char_FunctionItem_Use
		{ 5455, EAsdaMsgCategory::ItemRefused,   TEXT("функц. предмет: тумблер") }, // :19728 case 0x154F → PopFunctionItemState_Toggle
		// ★ 06.10.2026 (волна 2-Г): порт шлёт 5458 (свиток перемещения к игроку, окно 1552 вид 60).
		{ 5458, EAsdaMsgCategory::ItemRefused,   TEXT("свиток перемещения")    }, // :19732 case 0x1552 → RecvClient_FunctionItem_Meet
		{ 5468, EAsdaMsgCategory::ItemRefused,   TEXT("гача: запуск")          }, // :19746 case 0x155C → Recv_GachaItemUse
		{ 5470, EAsdaMsgCategory::Trade,         TEXT("гача: причёска")        }, // :19752 case 0x155E → Recv_GachaItemUse_0
		{ 5474, EAsdaMsgCategory::ItemRefused,   TEXT("гача: эффект")          }, // :19758 case 0x1562 → Recv_GachaItemEffect
		// ── Навыки (5267 порт не шлёт; 0x1485/0x1488) ──
		// ── ★ Волна 1-А (06.10.2026): «первая встреча с NPC» 5045 — строка 13 списка ожидающих (порт его теперь шлёт) ──
		{ 5045, EAsdaMsgCategory::Quest,         TEXT("NPC: первая встреча")   }, // :19899 case 0x13B5 → sub_45C260
		{ 5253, EAsdaMsgCategory::SkillRefused,  TEXT("навык: изучить")        }, // :20045 case 0x1485 → Recv_SkillPoint_SkillActive
		{ 5256, EAsdaMsgCategory::SkillRefused,  TEXT("навык: применить")      }, // :20051 case 0x1488 → RecvChar_SkillUse
		// ── Предметы, склад, магазины (v4==5000 и 0x138A…0x13A9) ──
		//   ★ У души запрещены даже ПОДБОР 5012 и ЗЕЛЬЯ 5028 — шире, чем у одержимости.
		{ 5000, EAsdaMsgCategory::ItemRefused,   TEXT("предмет: переложить")   }, // :20063 v4==5000 → CharItemMove
		{ 5002, EAsdaMsgCategory::ItemRefused,   TEXT("предмет: выбросить")    }, // :19805 case 0x138A → CharItemDrop
		{ 5006, EAsdaMsgCategory::ItemRefused,   TEXT("предмет: уничтожить")   }, // :19811 case 0x138E → CharItemDump
		{ 5008, EAsdaMsgCategory::ItemRefused,   TEXT("склад: положить")       }, // :19817 case 0x1390 → CharItemKeeping
		{ 5010, EAsdaMsgCategory::ItemRefused,   TEXT("склад: взять")          }, // :19823 case 0x1392 → CharItemRetake
		{ 5012, EAsdaMsgCategory::Loot,          TEXT("подбор с земли")        }, // :19829 case 0x1394 → CharItemPicking
		{ 5018, EAsdaMsgCategory::Trade,         TEXT("продать NPC")           }, // :19841 case 0x139A → CharItemSell
		{ 5020, EAsdaMsgCategory::Trade,         TEXT("купить у NPC")          }, // :19847 case 0x139C → CharItemBuy
		{ 5022, EAsdaMsgCategory::ItemRefused,   TEXT("самоцвет: вынуть")      }, // :19853 case 0x139E → CharCellobItemDetach
		{ 5024, EAsdaMsgCategory::ItemRefused,   TEXT("самоцвет: вставить")    }, // :19859 case 0x13A0 → CharCellobItemMove
		{ 5028, EAsdaMsgCategory::ItemRefused,   TEXT("расходник/зелье")       }, // :19871 case 0x13A4 → CharSpendItemUse
		{ 5031, EAsdaMsgCategory::ItemRefused,   TEXT("святой предмет")        }, // :19877 case 0x13A7 → CharSpendHolyItemUse
		// ── Квесты (0x13B1…0x13B8; 5048 порт не шлёт; 5045 шлёт с волны 1-А) ──
		{ 5041, EAsdaMsgCategory::Quest,         TEXT("квест: сдать")          }, // :19889 case 0x13B1 → Send_RewardQuestProcess
		{ 5043, EAsdaMsgCategory::Quest,         TEXT("квест: взять")          }, // :19895 case 0x13B3 → Send_StartQuestBookProcess
		// ── Кэш-шоп ──
		{ 5070, EAsdaMsgCategory::Trade,         TEXT("кэш-шоп: покупка")      }, // :19933 case 0x13CE → CharChargeShopBuy
		// ── Чат (0x13DC…0x13E0, 0x13F1) — душа НЕ пишет вообще ──
		{ 5084, EAsdaMsgCategory::Social,        TEXT("чат: крик")             }, // :19939 case 0x13DC → Recv_NormalChatClient
		{ 5086, EAsdaMsgCategory::Social,        TEXT("чат: общий")            }, // :19945 case 0x13DE → recvGlobalChatClient
		{ 5088, EAsdaMsgCategory::Social,        TEXT("чат: шёпот")            }, // :19951 case 0x13E0 → sub_547BD0
		{ 5105, EAsdaMsgCategory::Social,        TEXT("чат: отряд")            }, // :19993 case 0x13F1 → sub_450FB0
		// ── Отряд (0x13E2…0x13EC) ──
		{ 5090, EAsdaMsgCategory::Social,        TEXT("отряд: создать")        }, // :19957 case 0x13E2 → sub_44F4C0
		{ 5092, EAsdaMsgCategory::Social,        TEXT("отряд: ответ на создание") }, // :19963 case 0x13E4 → ResponseUser_NewPartyCreate
		{ 5094, EAsdaMsgCategory::Social,        TEXT("отряд: вступить")       }, // :19969 case 0x13E6 → sub_44FDE0
		{ 5096, EAsdaMsgCategory::Social,        TEXT("отряд: принять приглашение") }, // :19975 case 0x13E8 → sub_450210
		{ 5098, EAsdaMsgCategory::Social,        TEXT("отряд: выйти")          }, // :19981 case 0x13EA → sub_450620
		{ 5100, EAsdaMsgCategory::Social,        TEXT("отряд: исключить")      }, // :19987 case 0x13EC → sub_4508E0
		// ── Соулмейт (0x1450…0x146F) ──
		{ 5200, EAsdaMsgCategory::Social,        TEXT("соулмейт: предложение") }, // :20004 case 0x1450 → RequestUser_SoulMateCreate
		{ 5202, EAsdaMsgCategory::Social,        TEXT("соулмейт: ответ")       }, // :20010 case 0x1452 → ResponseUser_SoulMateCreate
		{ 5204, EAsdaMsgCategory::Social,        TEXT("соулмейт: разрыв")      }, // :20016 case 0x1454 → RequestUser_SoulMateDestory
		{ 5206, EAsdaMsgCategory::Social,        TEXT("соулмейт: ответ на разрыв") }, // :20022 case 0x1456 → ResponseUser_SoulMateDestory
		{ 5221, EAsdaMsgCategory::Social,        TEXT("соулмейт: режимы баффов") }, // :20028 case 0x1465 → sub_52A230
		{ 5231, EAsdaMsgCategory::SkillRefused,  TEXT("соулмейт: навык")       }, // :20034 case 0x146F → RecvChar_SoulMateSkillUse
		// ── Бой (0xFB8…0xFC0; 4032 порт не шлёт) ──
		{ 4024, EAsdaMsgCategory::CombatRefused, TEXT("действие/эмоция 4024")  }, // :20114 case 0xFB8 → sub_546730
		{ 4026, EAsdaMsgCategory::CombatRefused, TEXT("атака")                 }, // :20120 case 0xFBA → Send_CharNormalMonsterAttack
		{ 4028, EAsdaMsgCategory::CombatRefused, TEXT("атака: следующий удар") }, // :20126 case 0xFBC → Send_CharNormalMonsterAttackNext
		// ── Склад-инфо, воскрешение, флаги окна настроек (0xFD2, 0xFD6, 0xFE6) ──
		{ 4050, EAsdaMsgCategory::ItemRefused,   TEXT("склад: страница")       }, // :20146 case 0xFD2 → Send_ClientStoreHouseItemInfo
		{ 4054, EAsdaMsgCategory::System,        TEXT("воскрешение/реген")     }, // :20152 case 0xFD6 → RecvCharRegen
		// ★ 06.10.2026 (волна 1-Б): порт шлёт 4070 из окна настроек 69500 (`SendOptionFlags4070`).
		{ 4070, EAsdaMsgCategory::System,        TEXT("флаги окна настроек")   }, // :20156 case 0xFE6 → sub_52A0C0
		// ── Клан (0x1008…0x1014; 4114 порт не шлёт; ★ 27.09.2026: 4111 — ответ на приглашение, `friend_gate_12435.md`) ──
		{ 4104, EAsdaMsgCategory::Social,        TEXT("клан: создать")         }, // :20164 case 0x1008 → ClanCreateReq
		{ 4106, EAsdaMsgCategory::Social,        TEXT("клан: распустить")      }, // :20170 case 0x100A → ClanDeleteReq
		{ 4108, EAsdaMsgCategory::Social,        TEXT("клан: пригласить")      }, // :20176 case 0x100C → ClanMemberInvite_RequestUser
		{ 4111, EAsdaMsgCategory::Social,        TEXT("клан: ответ на приглашение") }, // :20182 case 0x100F → ClanMemberInvite_Recv_ResponseUser
		{ 4116, EAsdaMsgCategory::Social,        TEXT("клан: выйти")           }, // :20194 case 0x1014 → ClanMemberLeaveReq_0
	};

	// Поиск строки таблицы души по опкоду. nullptr — гейта на этом опкоде нет.
	const FSoulGateRow* FindSoulRow(uint16 Opcode)
	{
		for (const FSoulGateRow& R : kSoulGatedOpcodes)
		{
			if (R.Opcode == Opcode) { return &R; }
		}
		return nullptr;
	}

	// Нотис (только при `SilentBlock 0` и заданном `NoticeLoc`) — не чаще раза в 2 с на опкод:
	//   автоатака (4028) и эмоции (4024) шлются циклически, без дросселя строка сыпалась бы в чат
	//   каждый тик. Оригинал здесь молчит, так что и дроссель — часть нашего отступления.
	void ShowSoulRefusal(UGameInstance* GI, const FSoulGateRow& Row, int32 LocId)
	{
		if (!GI || LocId <= 0) { return; }
		static TMap<uint16, double> LastShownAt;
		const double Now = FPlatformTime::Seconds();
		if (const double* Prev = LastShownAt.Find(Row.Opcode))
		{
			if (Now - *Prev < 2.0) { return; }
		}
		LastShownAt.Add(Row.Opcode, Now);
		if (UAsdaMessageSubsystem* Msg = GI->GetSubsystem<UAsdaMessageSubsystem>())
		{
			Msg->PostLoc(Row.Category, LocId);
		}
	}
}

bool AsdaSoul::IsSoul(const UAsdaNetSubsystem* Net)
{
	return Net != nullptr && Net->SelfSoulState == 1;
}

bool AsdaSoul::IsSoul(const UGameInstance* GameInstance)
{
	if (!GameInstance) { return false; }
	// `GetSubsystem` неконстантен — снимаем константность только ради чтения признака.
	UGameInstance* GI = const_cast<UGameInstance*>(GameInstance);
	return IsSoul(GI->GetSubsystem<UAsdaNetSubsystem>());
}

bool AsdaSoul::IsGatedOpcode(uint16 Opcode)
{
	return FindSoulRow(Opcode) != nullptr;
}

bool AsdaSoul::IsEnterAllowed()
{
	return CVarSoulAllowEnter.GetValueOnGameThread() != 0;
}

// ═══ ★★★ ГЕЙТ ДЕЙСТВИЙ ПОД GM-«СКРЫТЬСЯ» ═══════════════════════════════════════════════════════
//   Разбор, источник каждой строки таблицы и обоснование молчания — у объявления в заголовке.
static ASDA_DEV_CVAR(int32, CVarGmHideAttackGate,
	TEXT("asda.Gm.HideAttackGate"), 1,
	TEXT("1 = под GM-hide клиент не отыгрывает атаку и не шлёт боевые пакеты (сервер их и так молча "
	     "выбрасывает); 0 = как оригинал — замах играется, пакет уходит в пустоту."),
	ECVF_Default);

// Опкоды, которые сервер под `CUser+0x3BC3B == 300` глотает молча. Движение 4024 в список
//   СОЗНАТЕЛЬНО не внесено (см. заголовок): оператор должен ходить.
struct FGmHideGateRow { uint16 Opcode; const TCHAR* What; };
static const FGmHideGateRow kGmHideGatedOpcodes[] =
{
	{ 4026, TEXT("атака моба (Send_CharNormalMonsterAttack, case 0xFBA)")      },
	{ 4028, TEXT("следующий удар (Send_CharNormalMonsterAttackNext, case 0xFBC)") },
	{ 4201, TEXT("атака игрока (Recv_User_AttackToPlayer, case 0x1069)")       },
	{ 4203, TEXT("доклад об уроне (Recv_User_AttackedToPlayer, case 0x106B)")  },
	{ 5256, TEXT("применить навык (RecvChar_SkillUse, case 0x1488)")           },
	{ 6158, TEXT("навык хранителя души (RecvChar_SkillUse_0, case 0x180E)")    },
};

static const FGmHideGateRow* FindGmHideRow(uint16 Opcode)
{
	for (const FGmHideGateRow& R : kGmHideGatedOpcodes)
	{
		if (R.Opcode == Opcode) { return &R; }
	}
	return nullptr;
}

bool AsdaGmHide::IsHidden(const UAsdaNetSubsystem* Net)
{
	return Net != nullptr && Net->IsGmHidden();
}

bool AsdaGmHide::IsHidden(const UGameInstance* GameInstance)
{
	if (!GameInstance) { return false; }
	// `GetSubsystem` неконстантен — снимаем константность только ради чтения признака.
	UGameInstance* GI = const_cast<UGameInstance*>(GameInstance);
	return IsHidden(GI->GetSubsystem<UAsdaNetSubsystem>());
}

bool AsdaGmHide::IsGatedOpcode(uint16 Opcode)
{
	return FindGmHideRow(Opcode) != nullptr;
}

bool AsdaGmHide::BlockOutgoing(const UAsdaNetSubsystem* Net, uint16 Opcode)
{
	if (CVarGmHideAttackGate.GetValueOnGameThread() == 0) { return false; }
	if (!IsHidden(Net)) { return false; }
	const FGmHideGateRow* Row = FindGmHideRow(Opcode);
	if (!Row) { return false; }
	// Молча: нотиса у этой команды в эталоне нет ни одного (проверено телом обработчика 6006).
	UE_LOG(LogAsdaSoulGate, Log,
		TEXT("[GM-hide] опкод %u (%s) не отправлен: сервер под скрытием проглотил бы его молча"),
		(uint32)Opcode, Row->What);
	return true;
}

bool AsdaGmHide::BlockCombatAction(const UGameInstance* GameInstance)
{
	if (CVarGmHideAttackGate.GetValueOnGameThread() == 0) { return false; }
	return IsHidden(GameInstance);
}

bool AsdaSoul::BlockOutgoing(const UAsdaNetSubsystem* Net, uint16 Opcode)
{
	if (CVarSoulActionGate.GetValueOnGameThread() == 0) { return false; }
	if (!IsSoul(Net)) { return false; }

	const FSoulGateRow* Row = FindSoulRow(Opcode);
	if (!Row) { return false; }

	const bool  bSilent = (CVarSoulSilentBlock.GetValueOnGameThread() != 0);
	const int32 LocId   = CVarSoulNoticeLoc.GetValueOnGameThread();
	if (!bSilent)
	{
		ShowSoulRefusal(Net ? Net->GetGameInstance() : nullptr, *Row, LocId);
	}
	UE_LOG(LogAsdaSoulGate, Log,
		TEXT("[душа] опкод %u (%s) не отправлен: сервер при SoulState==1 проглотил бы его молча (sub_52EEA0)%s"),
		(uint32)Opcode, Row->What,
		bSilent ? TEXT("; молча, как оригинал")
		        : (LocId > 0 ? TEXT("; показан нотис asda.Soul.NoticeLoc")
		                     : TEXT("; asda.Soul.SilentBlock 0, но asda.Soul.NoticeLoc = 0 — строки нет, молчим")));
	return true;
}
