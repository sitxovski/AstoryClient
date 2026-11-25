// Copyright (c) 2026 @sitxovski (Discord)
// Реализация `UAsdaPremiumPostSubsystem` — элитный почтовый ящик (окно 69000).
// Разбор фичи, список исправленных дефектов и осознанных отступлений — в шапке заголовка.
#include "UI/AsdaPremiumPostSubsystem.h"
#include "AsdaDevConsole.h"   // ★ античит А2: ASDA_DEV_CVAR / ASDA_DEV_CONSOLE — дев-ключи и команды вне Shipping
#include "UI/AsdaNotice.h"   // ★ U13 (дедуп №2, В2-A): ConsoleEcho — общее эхо консольных команд

#include "AsdaNetSubsystem.h"
#include "Blueprint/UserWidget.h"
#include "Framework/Application/SlateApplication.h"   // живой хит-тест: кого Slate реально видит под точкой
#include "Engine/GameInstance.h"
#include "UI/AsdaInventorySubsystem.h"
#include "UI/AsdaItemTooltipSubsystem.h"
#include "UI/AsdaHotbarSubsystem.h"
#include "UI/AsdaUiHelpers.h"
#include "UI/AsdaWindowSubsystem.h"
#include "AsdaOutgoingGate.h"               // ★ С-Р1: общий офлайн-гейт отладочных команд (RefuseDebugWhenOnline)
#include "Test/AsdaSelfTestKit.h"            // ★ 25.10.2026: каркас самотеста (quit, итог)
#include "TimerManager.h"
#include "Engine/World.h"
#include "AsdaPremiumPostSubsystem_Private.h"   // ★ R3-c 12.11.2026: общее для частей AsdaPremiumPostSubsystem*.cpp

DEFINE_LOG_CATEGORY(LogAsdaPost);   // ★ R2: было DEFINE_LOG_CATEGORY_STATIC — категория общая для частей

// ★ ТРЕБОВАНИЕ ВЛАДЕЛЬЦА (2026-08-25): «уведомление премиум-почты при входе висит бесконечно —
//   пусть показывается секунд 10, потом исчезает до следующего вызова (новая сессия или новый
//   предмет в почте)». ЭТО НАША ДОБАВКА ПОВЕРХ ОРИГИНАЛА: эталон снимает иконку типа 15 только
//   кликом по ней (`buffSlotCompact(…, 15)`), а его таймер 0x1E гасит лишь тип 11.
//   0 = вернуть эталонное «висит до клика».
static ASDA_DEV_CVAR(float, CVarPostNoticeSec,
	TEXT("asda.Post.NoticeSec"), 10.f,
	TEXT("Элитный почтовый ящик: сколько секунд горит иконка уведомления, прежде чем погаснуть ")
	TEXT("до нового заказа. 0 — держать до клика, как в оригинале."),
	ECVF_Default);

// ★★★ [ИСПРАВЛЕНИЕ] 25.10.2026 СТРОКА ЯЩИКА = ЗАКАЗ ЦЕЛИКОМ (слово владельца: «в премиум ящике каждый ряд имеет несколько
//   ячеек, а клался всегда 1 предмет… правильно всё парсить, размещать при получении и хранить в БД»). Оригинал: запись 11005 =
//   строка окна, в ней `Count` ОДИНАКОВЫХ иконок; корзина магазина ложилась одной записью (первая позиция). Теперь сервер
//   (GameServer P85 + шлюз + SQL premrows) кладёт заказ записями «строка БД на штуку» с одним `Order`, DBManager (DBPatch P85)
//   отдаёт до 49 записей, а клиент собирает записи одного `Order` в ОДНУ строку окна (до 7 ячеек — столько в строке JUI:
//   69011…69017, эталон `gridWindowPopulate7x7`) и забирает строку целиком: проверка места под ВСЮ строку, затем по 11006 на
//   запись (сервер так и устроен: одна запись = одна выдача), следующая — после 11007 код 1. Протокол не менялся.
//   0 = как оригинал (запись = строка, не больше 7 записей).
ASDA_DEV_CVAR(int32, CVarPostMultiItemRows,
	TEXT("asda.EliteMail.MultiItemRows"), 1,
	TEXT("Элитный почтовый ящик: 1 (по умолчанию) — записи 11005 одного заказа собираются в одну строку окна (до 7 ячеек), ")
	TEXT("забор строки целиком (по 11006 на запись, с проверкой места); 0 — как оригинал: запись = строка, не больше 7."),
	ECVF_Default);

// ★★★ [ИСПРАВЛЕНИЕ] 07.11.2026 НЕСКОЛЬКО ЗАКАЗОВ ЗА ОДНО НАЖАТИЕ (пачка 14). Оригинал: 69081 — `RadioButton`, выбор один (`this[155]`,
//   `uiInventoryHandleCommand` a3 == 66817), а `net_sendSlotSyncOpcode11006@0xEF0CD0` шлёт одну запись. Протокол не меняем: сервер
//   (`Recv_PremiumItemToInven_Req@0x4F8340`) принимает ровно одну запись 10 б, поэтому заказы идут очередью: следующая запись — только
//   после 11007 код 1. Место проверяется сразу под все отмеченные заказы; отказ или окно выбора приза останавливают очередь, отметки
//   невзятых заказов остаются. Работает только при `asda.EliteMail.MultiItemRows 1`: иначе `Order` строки не уникален.
//   0 = как оригинал (один выбранный заказ).
ASDA_DEV_CVAR(int32, CVarPostMultiSelect,
	TEXT("asda.EliteMail.MultiSelect"), 1,
	TEXT("Элитный почтовый ящик: 1 (по умолчанию) — галочки отмечают несколько заказов, кнопка забирает их по очереди; ")
	TEXT("0 — как оригинал: выбран один заказ."),
	ECVF_Default);

// ═══════════════════════════════ ЖИЗНЕННЫЙ ЦИКЛ ════════════════════════════════════════════════
void UAsdaPremiumPostSubsystem::Initialize(FSubsystemCollectionBase& Collection)
{
	Super::Initialize(Collection);
	// ★★★ РОСПИСЬ «КАК ЗАКРЫТЬ СЕБЯ ПО ESC» — порт события 21 (`CControl_OnEvent21@0x12DCC30`).
	//   Окно `Primium_Item_post_WIN` (IDNum 69000) стоит в эталонной таблице `kEscCloseWindows`: признак
	//   `CControl+0x228` ему ставит `uiInitMixWindowGrid@0xEEFE60`. Проход
	//   `UAsdaWindowSubsystem::CloseTopmostOnEsc` закрывает верхнее окно ИМЕННО ЭТИМ вызовом,
	//   а не снятием виджета: снятие оставило бы подсистеме состояние «открыто», и окно
	//   больше не открылось бы. Закрыватель идемпотентен — повторный вызов ничего не делает.
	if (UAsdaWindowSubsystem* EscWM = GetGameInstance()
		? GetGameInstance()->GetSubsystem<UAsdaWindowSubsystem>() : nullptr)
	{
		EscWM->RegisterEscCloser(FName(kWinId), FSimpleDelegate::CreateUObject(this, &UAsdaPremiumPostSubsystem::CloseWindow));
	}
	// ★ Все четыре подсистемы, у которых мы забираем делегаты и общие точки, обязаны быть подняты
	//   ДО нас: `GetSubsystem` во время чужого `Initialize` может вернуть ещё не инициализированный
	//   экземпляр, и подписка на его делегат тихо потерялась бы.
	Collection.InitializeDependency(UAsdaNetSubsystem::StaticClass());
	Collection.InitializeDependency(UAsdaWindowSubsystem::StaticClass());
	Collection.InitializeDependency(UAsdaInventorySubsystem::StaticClass());
	Collection.InitializeDependency(UAsdaHotbarSubsystem::StaticClass());

	Rows.SetNum(kRows);
	CellCodes.Init(-1, kRows * kCols);

	UGameInstance* GI = GetGameInstance();
	if (UAsdaNetSubsystem* Net = GI ? GI->GetSubsystem<UAsdaNetSubsystem>() : nullptr)
	{
		HList  = Net->OnPremiumPostList.AddUObject(this, &UAsdaPremiumPostSubsystem::OnListPacket);
		HApply = Net->OnPremiumPostApply.AddUObject(this, &UAsdaPremiumPostSubsystem::OnApplyPacket);
		// ★★★ ВХОД №3 — «мы в мире» (замечание владельца №6).
		//   ⚠⚠ ПОПРАВКА К ПРОШЛОМУ ЗАХОДУ (доказана живой сессией владельца, разбор —
		//   `Reports/elite_mailbox_fix2.md` §2.1): подписки на ОДИН только `OnWorldReady`
		//   недостаточно. Он броадкастится на 4055/4056, а **эта стойка их не шлёт ВООБЩЕ** —
		//   сплошной скан 13 живых логов по сырому дампу `RECV op=` даёт 0 вхождений 4055 и 4056
		//   при 56 других опкодов. Вход в мир здесь подтверждается пакетом **4003 SpawnSelf**
		//   (`OnSelfSpawn`) — им же порт открывает мир (`UAsdaWorldSubsystem::OnSelfSpawn` →
		//   `RevealWorld`; в `OnWorldReady` мира-системы прямо написано «мир уже показан по 4003»).
		//   Поэтому подписываемся на ОБА делегата, но ведут они в ОДИН обработчик — второй точки
		//   правды не появляется, а дубль гасится гвардами `bWorldEntryQueryPending` /
		//   `bRequestInFlight` внутри `TickWorldEntry`.
		HSelfSpawn  = Net->OnSelfSpawn.AddUObject(this, &UAsdaPremiumPostSubsystem::OnWorldReady);
		HWorldReady = Net->OnWorldReady.AddUObject(this, &UAsdaPremiumPostSubsystem::OnWorldReady);
	}
	// ★ ВХОД №1 — кнопка 10082 `Inven_primium_item_btn` в окне инвентаря. Своей кнопки НЕ заводим:
	//   она уже есть, привязана и транслирует нажатие делегатом (`AsdaInventorySubsystem.cpp`,
	//   `BindSide(BtnPremiumItem, 10082, …)`).
	if (UAsdaInventorySubsystem* Inv = GI ? GI->GetSubsystem<UAsdaInventorySubsystem>() : nullptr)
	{
		HSideBtn = Inv->OnSideButtonClicked.AddUObject(this, &UAsdaPremiumPostSubsystem::OnInventorySideButton);
	}
	// ★ ВХОД №2 — иконка-уведомление слота 15 квик-бара (эталон `uiNoticeButtonDispatch` case 15).
	if (UAsdaHotbarSubsystem* Hot = GI ? GI->GetSubsystem<UAsdaHotbarSubsystem>() : nullptr)
	{
		HNotice = Hot->OnNoticeIconClicked.AddUObject(this, &UAsdaPremiumPostSubsystem::OnNoticeIconClicked);
	}
	UE_LOG(LogAsdaPost, Log,
		TEXT("[элитный ящик] подсистема поднята: окно 69000, опкоды 11004/11005/11006/11007, ")
		TEXT("входы — кнопка 10082, иконка уведомления типа 15 и ВХОД В МИР (4003 SpawnSelf + 4055/4056)"));
}

void UAsdaPremiumPostSubsystem::Deinitialize()
{
	UGameInstance* GI = GetGameInstance();
	if (UAsdaNetSubsystem* Net = GI ? GI->GetSubsystem<UAsdaNetSubsystem>() : nullptr)
	{
		Net->OnPremiumPostList.Remove(HList);
		Net->OnPremiumPostApply.Remove(HApply);
		Net->OnWorldReady.Remove(HWorldReady);
		Net->OnSelfSpawn.Remove(HSelfSpawn);
	}
	if (UAsdaInventorySubsystem* Inv = GI ? GI->GetSubsystem<UAsdaInventorySubsystem>() : nullptr)
	{
		Inv->OnSideButtonClicked.Remove(HSideBtn);
	}
	if (UAsdaHotbarSubsystem* Hot = GI ? GI->GetSubsystem<UAsdaHotbarSubsystem>() : nullptr)
	{
		Hot->OnNoticeIconClicked.Remove(HNotice);
	}
	Super::Deinitialize();
}

// ★★★ К-3: СБРОС СОСТОЯНИЯ СЕССИИ (шапка `AsdaSessionReset.h`). Подсистема живёт на
//   `GameInstance` и переживает разрыв связи, выход в выбор персонажа и травел; всё, что
//   ниже, — данные и защёлки ОДНОЙ сессии. Отправок здесь нет (закон `no-reentrant-net-callbacks`).
void UAsdaPremiumPostSubsystem::ResetSessionState(EAsdaSessionResetReason Reason)
{
	CloseWindow();
	bRequestInFlight = false;
	PendingList.Reset();  bListPending  = false;
	PendingApply.Reset(); bApplyPending = false;
	DeferredRec.Reset();  bDeferActive  = false;
	ResetTake(); CheckedOrders.Reset();   // [ИСПРАВЛЕНИЕ] 25.10.2026: забор строки не переживает сессию
	bWorldEntryQueryPending = false;
	WorldEntryQueryDelay = 0.f;
	bNoticeWanted = false;
	NoticeShownAtS = 0.0;
	SelectedRow = 0;
	Mouse.Reset();
	if (AsdaIsFullSessionReset(Reason))
	{
		// Отметки «этот заказ уже видели» привязаны к charId владельца: у нового персонажа
		// они другие, и `SeenOwnerCharId` заставит перечитать их заново.
		SeenOrders.Reset();
		UnseenCount = 0;
		SeenOwnerCharId = 0;
	}
}

// ═══════════════════════════════ ВХОДЫ ИЗ СМЕЖНЫХ СИСТЕМ ═══════════════════════════════════════
void UAsdaPremiumPostSubsystem::OnInventorySideButton(int32 IdNum)
{
	// Эталон: `uiInventoryHandleCommand` окна инвентаря, `cmdId == 10082` →
	//   `uiBagWindow_OpenAndSync(g_pInventoryBagWindow, …)`.
	if (IdNum != 10082) { return; }
	// ★ Вход ИЗНУТРИ инвентаря. Эталон и здесь закрывает инвентарь (`uiBagWindow_OpenAndSync` зовёт
	//   `sowelWindowClose(g_pInventoryWnd, 1)` безусловно, откуда бы его ни позвали). По требованию
	//   владельца оба окна остаются открытыми; эталонная ветка — `asda.Post.LegacyCloseInven 1`.
	OpenAndSync(/*bAlsoOpenInventory*/ !bLegacyCloseInventory);
}

void UAsdaPremiumPostSubsystem::OnNoticeIconClicked(int32 NoticeType)
{
	if (NoticeType != kNoticeType) { return; }
	// ★★★ ОСОЗНАННОЕ ОТСТУПЛЕНИЕ ОТ ЭТАЛОНА (требование владельца; разбор — в шапке заголовка).
	//   Оригинал: `uiNoticeButtonDispatch@0xDF4F10` case 15 → `uiBagWindow_OpenAndSync`, а тот
	//   внутри зовёт `sowelWindowClose(g_pInventoryWnd, 1)` и ЗАКРЫВАЕТ инвентарь.
	//   Здесь по требованию владельца открываются ОБА окна. Эталонная ветка сохранена и
	//   включается ключом `asda.Post.LegacyCloseInven 1`.
	OpenAndSync(/*bAlsoOpenInventory*/ !bLegacyCloseInventory);
}

// ══════════ ★★★ ВХОД В МИР И СМЕНА КАРТЫ (замечание владельца №6) ══════════
//
// ЧТО ГОВОРИТ ЭТАЛОН. Запрос 11004 в оригинале уходит ровно из четырёх мест, и НИ ОДНО из них
//   не связано со входом в мир (реверс `elite_mailbox_re.md` §2.2, §5):
//     1) `uiBagWindow_OpenAndSync@0xEF1B30` — открытие самого окна 69000;
//     2) `inventorySyncApplyPacket@0xEF1320` (хвост) — после применения 11007;
//     3) `uiGuildPanelClose@0xB6E920` — закрытие сцены гачи, под флагом `CGachaScene+gap_254[292]`;
//     4) `uiInventoryResetAndLeave@0xC3DE00` — закрытие премиум-шопа, под флагом `+43036`.
//   Сервер сам 11005 не пушит: `RecvDB_PremiumItemList_Res@0x4F8050` отвечает только на 2603,
//   а 2602 шлёт только обработчик 11004. Пуш-уведомление 6226 в стойке не отправляет никто
//   (сплошной скан шести серверов на immediate `0x1852` — 0 вхождений, §2.5).
//   ⇒ В ОРИГИНАЛЕ игрок, вошедший в игру с непустым ящиком, НЕ ВИДИТ НИЧЕГО, пока сам не откроет
//   окно или не закроет гачу/шоп. Это дефект, а не «точка вызова, которую мы не нашли»:
//   искали двумя методами — по вызовам `netSecure_sendGuardedRequest` во всём срезе эталона
//   (4 вызова, перечислены выше) и по отправителям 11005 на живых серверах.
//
// ЧТО СДЕЛАНО (закон `fix-original-defects`, прямое требование владельца). Запрос 11004 уходит
//   по факту входа в мир. Источников события ДВА, и оба ведут сюда: **4003 `SpawnSelf`**
//   (`OnSelfSpawn` — рабочий в этой стойке, им же порт открывает мир) и 4055/4056 (`OnWorldReady`
//   — на живом сервере владельца НЕ приходят ни разу, доказано сплошным сканом 13 логов; оставлен
//   на случай другой сборки сервера). Оба пакета сервер шлёт и при первом входе в игру, и после
//   КАЖДОЙ загрузки другой локации, поэтому оба случая владельца закрыты. Второй точки правды не
//   возникает: обработчик один, повтор гасят гварды `bWorldEntryQueryPending`/`bRequestInFlight`.
void UAsdaPremiumPostSubsystem::OnWorldReady()
{
	// Слать прямо отсюда нельзя: мы внутри разбора входящего пакета (закон `no-reentrant-net-callbacks`).
	if (bWorldEntryQueryPending) { return; }   // 4003 и 4055/4056 в одном входе — запрос всё равно один
	bWorldEntryQueryPending = true;
	// Небольшая задержка: пакет входа приходит РАНЬШЕ, чем поднимается панель квик-бара и павн
	//   игрока (хотбар поднимается лениво из своего тика по `CurrentMapId >= 0` + живой
	//   `LocalPlayerActor`). Ответ 11005 всё равно дождётся панели через `TickWorldEntry`,
	//   но так в логе не будет шквала «панель ещё не поднята».
	WorldEntryQueryDelay = 1.0f;
	UE_LOG(LogAsdaPost, Log,
		TEXT("[элитный ящик] вход в мир подтверждён (4003 SpawnSelf либо 4055/4056) — запросим ")
		TEXT("состояние ящика (11004) через %.1f с"), WorldEntryQueryDelay);
}

void UAsdaPremiumPostSubsystem::TickWorldEntry(float DeltaTime)
{
	UGameInstance* GI = GetGameInstance();
	UAsdaNetSubsystem* Net = GI ? GI->GetSubsystem<UAsdaNetSubsystem>() : nullptr;

	// ── 1. Отложенный запрос состояния ящика после входа в мир / смены карты ──
	if (bWorldEntryQueryPending)
	{
		WorldEntryQueryDelay -= DeltaTime;
		if (WorldEntryQueryDelay <= 0.f)
		{
			if (Net && Net->bInGameConnected && !bRequestInFlight)
			{
				if (Net->SendPremiumPostListReq11004())
				{
					bRequestInFlight = true;
					bWorldEntryQueryPending = false;
					UE_LOG(LogAsdaPost, Log,
						TEXT("[элитный ящик] вход в мир: отправлен 11004 (исправление дефекта ")
						TEXT("оригинала — он состояние ящика при входе не спрашивал вовсе)"));
				}
				else
				{
					WorldEntryQueryDelay = 2.0f;   // сокет не готов — повторим
				}
			}
			else
			{
				WorldEntryQueryDelay = 2.0f;
			}
		}
	}

	// ── 2. Удержание иконки уведомления ──
	//   ★ НАШЕ НОВОВВЕДЕНИЕ ЗДЕСЬ НЕ ДУБЛИРУЕТСЯ: «липкость» смотрит на `bNoticeWanted`, а его
	//   теперь считает `ReconcileSeenOrders` — по числу НЕПРОСМОТРЕННЫХ заказов. Поэтому просмотр
	//   ящика переживает и пересоздание панели `QUICK_NEW_WIN`, и смену карты: набор просмотренного
	//   живёт в подсистеме `GameInstance`, а не в виджете, который `ResetWidget` сносит.
	//   Панель `QUICK_NEW_WIN` поднимается лениво и ПЕРЕСОЗДАЁТСЯ на каждой смене карты
	//   (`UAsdaHotbarSubsystem::ResetWidget` чистит массив слотов уведомлений вместе с типами).
	//   Поэтому истина «иконка должна висеть» живёт здесь, а хотбару она навязывается, как только
	//   его панель снова жива. Без этого иконка терялась бы: (а) если 11005 пришёл, пока висит
	//   загрузочный экран, (б) после каждого перехода на другую карту.
	// ── 2а. ★ АВТОСНЯТИЕ ИКОНКИ ЧЕРЕЗ N СЕКУНД (требование владельца 2026-08-25) ──
	//   Оригинал держит иконку типа 15 до клика по ней; владелец попросил показывать её ~10 секунд
	//   и гасить «до следующего вызова». Гасим не просто иконку, а помечаем текущий список
	//   просмотренным — тогда «следующий вызов» получается ровно тот, что просили: новая сессия
	//   (набор пуст) или НОВЫЙ заказ в ящике (его в наборе нет, иконка зажжётся снова).
	//   `asda.Post.NoticeSec 0` — вернуть эталонное «висит до клика».
	{
		const float HoldSec = CVarPostNoticeSec.GetValueOnGameThread();
		if (HoldSec > 0.f && NoticeShownAtS > 0.0 && bNoticeWanted)
		{
			if (FPlatformTime::Seconds() - NoticeShownAtS >= (double)HoldSec)
			{
				MarkCurrentListSeen(TEXT("★ показ уведомления истёк — гасим до нового заказа"));
				return;
			}
		}
	}

	if (!bNoticeWanted || IsWindowVisible()) { return; }
	UAsdaWindowSubsystem* WM = GI ? GI->GetSubsystem<UAsdaWindowSubsystem>() : nullptr;
	if (!WM || !WM->IsOpen(FName(TEXT("QUICK_NEW_WIN")))) { return; }   // панели ещё нет — молча ждём
	UAsdaHotbarSubsystem* Hot = GI ? GI->GetSubsystem<UAsdaHotbarSubsystem>() : nullptr;
	if (!Hot || Hot->HasNoticeIcon(kNoticeType)) { return; }
	Hot->AddNoticeIcon(kNoticeType);
	UE_LOG(LogAsdaPost, Log,
		TEXT("[элитный ящик] иконка уведомления типа %d (пере)выставлена: в ящике есть непереданные ")
		TEXT("предметы, а панель квик-бара только что поднялась/пересоздалась"), kNoticeType);
}

// ══════════ ★★★ НАШЕ НОВОВВЕДЕНИЕ: «ПРОСМОТРЕННЫЕ ЗАКАЗЫ» (требование владельца 2026-08-20) ══════════
//
// ⚠⚠ ЭТО НЕ ЭТАЛОННОЕ ПОВЕДЕНИЕ, а наша добавка. Что делает ЭТАЛОН (реверс §2.2 п. 6 и §1.4):
//   `netParseLocalSlotSync@0xEF0990` вешает `statusBarAddIcon(g_pQuickSlotBar, 15)` на КАЖДЫЙ
//   непустой 11005 при закрытом окне ящика — без всякой памяти о том, смотрел ли игрок ящик.
//   Снимается иконка только кликом по ней (`buffSlotCompact(…, 15)`) либо `uiNoticeIconPanelReset`;
//   таймер 0x1E, который эталон взводит, снимает ТОЛЬКО тип 11. Никакого признака «просмотрено»
//   в оригинале нет ни в клиенте, ни в протоколе (в 11005 нет ни флага «новое», ни времени).
//
// ТРЕБОВАНИЕ ВЛАДЕЛЬЦА (дословно): «Когда человек просмотрел элитный почтовый ящик (в текущей
//   игровой сессии) — не нужно больше ему оповещение отправлять иконкой моргающей! Выводить
//   заново только если новый предмет там появляется.»
//
// ЧТО СЧИТАЕТСЯ «НОВЫМ ЗАКАЗОМ». Заказ = строка 11005 = запись 10 б (`Order`, `ItemIndex`,
//   `Count`, `BuyType`). Ключ идентичности — ВСЯ запись целиком; развёрнутое обоснование
//   у `FAsdaPremiumPostSeenKey` в заголовке. Коротко: `Order` — идентификатор покупки в БД
//   (сервер кладёт его в DB-запрос `Recv_PremiumItemToInven_Req@0x4F8340`, поле `+9`), но тела
//   процедуры `UPCashitemList` у нас нет и гарантии «`Order` не переиспользуется» тоже (§6.5),
//   поэтому ключ расширен остальными тремя полями, а переиспользование закрыто сжатием набора.
//
// ПОЧЕМУ НАБОР ЗАКАЗОВ, А НЕ ОДИН ФЛАГ «ЯЩИК СМОТРЕЛИ». Владелец потребовал прямо: посмотрели два
//   заказа, пришёл третий — иконка обязана загореться. Общий флаг это не умеет в принципе.
// Пересечь набор просмотренного с текущим списком и посчитать непросмотренные заказы.
//   ★ Сравнение МУЛЬТИМНОЖЕСТВОМ: найденная запись набора «съедается», поэтому два полностью
//     одинаковых заказа в списке останутся двумя, а не схлопнутся в один.
//   ★ Порядок строк не важен — сравниваем содержимое, а не позицию. Это и есть ответ на крайний
//     случай «список пришёл в другом порядке»: сервер волен вернуть те же заказы иначе
//     отсортированными, иконка от этого не загорится.
//   ★ После вызова `SeenOrders` — строго подмножество текущего содержимого: заказ, который забрали
//     (или который исчез по любой другой причине), из набора ВЫБЫВАЕТ. Отсюда два следствия:
//     память не растёт (потолок 7 записей), и переиспользованный сервером `Order` считается новым.
int32 UAsdaPremiumPostSubsystem::ReconcileSeenOrders()
{
	TArray<FAsdaPremiumPostSeenKey> Remaining = MoveTemp(SeenOrders);   // прошлый набор — расходуем
	SeenOrders.Reset();
	SeenOrders.Reserve(kRows);

	int32 Unseen = 0;
	for (const FAsdaPremiumPostRow& R : Rows)
	{
		if (R.IsEmpty()) { continue; }                    // пустая строка заказом не является
		const FAsdaPremiumPostSeenKey Key(R);
		const int32 Idx = Remaining.IndexOfByKey(Key);
		if (Idx != INDEX_NONE)
		{
			Remaining.RemoveAtSwap(Idx);                           // «съели» совпадение
			SeenOrders.Add(Key);                                   // заказ остаётся просмотренным
		}
		else
		{
			++Unseen;                                              // такого заказа игрок ещё не видел
		}
	}
	if (Remaining.Num() > 0)
	{
		UE_LOG(LogAsdaPost, Log,
			TEXT("[элитный ящик] ★ нововведение «просмотрено»: %d просмотренных заказов ушли из ящика ")
			TEXT("(забраны либо сняты сервером) — выброшены из набора, память не растёт"), Remaining.Num());
	}
	UnseenCount = Unseen;
	return Unseen;
}

// «Игрок это увидел». Точек просмотра ровно три, и все три ведут сюда:
//   1) `OpenAndSync` — игрок открыл окно (метим ДО очистки модели, пока строки ещё в ней);
//   2) `ApplyListBody` при видимом окне — список пришёл прямо игроку на глаза;
//   3) `CloseWindow` — игрок закрыл окно, значит содержимое он видел.
void UAsdaPremiumPostSubsystem::MarkCurrentListSeen(const TCHAR* Reason)
{
	if (!bSeenGateEnabled) { return; }

	// ★★★ ИМЕННО ОБЪЕДИНЕНИЕ, А НЕ ПЕРЕЗАПИСЬ НАБОРА. Это не стилистика, а живой дефект:
	//   `OpenAndSync` ОЧИЩАЕТ модель (шаг 2 эталона, `= -1`) сразу после метки, и если 11004 не
	//   ушёл (сокет не готов) или ответ 11005 потерялся, то закрытие окна пришло бы сюда с ПУСТЫМИ
	//   строками — перезапись стёрла бы всё просмотренное, и иконка вернулась бы сама собой.
	//   Лишние записи набора не копятся: их выбрасывает `ReconcileSeenOrders` на первом же 11005.
	//   Добавление — тоже мультимножеством, чтобы два одинаковых заказа не схлопнулись в один.
	TArray<FAsdaPremiumPostSeenKey> Pool = SeenOrders;
	int32 Added = 0;
	for (const FAsdaPremiumPostRow& R : Rows)
	{
		if (R.IsEmpty()) { continue; }
		const FAsdaPremiumPostSeenKey Key(R);
		const int32 Idx = Pool.IndexOfByKey(Key);
		if (Idx != INDEX_NONE) { Pool.RemoveAtSwap(Idx); }   // уже был просмотрен
		else { SeenOrders.Add(Key); ++Added; }
	}
	UnseenCount = 0;
	bNoticeWanted = false;
	ApplyNoticeIcon(Reason);
	UE_LOG(LogAsdaPost, Log,
		TEXT("[элитный ящик] ★ нововведение «просмотрено»: %s — добавлено %d, в наборе %d заказ(ов); ")
		TEXT("иконка типа %d больше не загорится, пока не придёт НОВЫЙ заказ"),
		Reason, Added, SeenOrders.Num(), kNoticeType);
}

// Одна точка на всю подсистему: привести иконку квик-бара к текущему `bNoticeWanted`.
//   Своей иконки не заводим и второго источника истины в хотбаре не создаём — пользуемся уже
//   существующими общими точками `AddNoticeIcon`/`RemoveNoticeIcon` (закон `port-shared-points`).
void UAsdaPremiumPostSubsystem::ApplyNoticeIcon(const TCHAR* Reason)
{
	UGameInstance* GI = GetGameInstance();
	UAsdaHotbarSubsystem* Hot = GI ? GI->GetSubsystem<UAsdaHotbarSubsystem>() : nullptr;
	if (!Hot) { return; }

	const bool bHas = Hot->HasNoticeIcon(kNoticeType);
	// Показывать только при закрытом окне — ЭТО ЭТАЛОН (`sub_9384DC(this) == 0`), не наша добавка.
	const bool bWant = bNoticeWanted && !IsWindowVisible();
	if (bWant && !bHas)
	{
		Hot->AddNoticeIcon(kNoticeType);
		// ★ Отсчёт показа начинаем с момента, когда иконка РЕАЛЬНО загорелась (а не когда мы этого
		//   захотели): панель квик-бара поднимается лениво и пересоздаётся на каждой смене карты,
		//   поэтому «хочу» и «горит» разнесены во времени.
		NoticeShownAtS = FPlatformTime::Seconds();
		UE_LOG(LogAsdaPost, Log, TEXT("[элитный ящик] иконка типа %d ВКЛЮЧЕНА: %s"), kNoticeType, Reason);
	}
	else if (!bNoticeWanted && bHas)
	{
		Hot->RemoveNoticeIcon(kNoticeType);
		NoticeShownAtS = 0.0;
		UE_LOG(LogAsdaPost, Log, TEXT("[элитный ящик] иконка типа %d СНЯТА: %s"), kNoticeType, Reason);
	}
}

void UAsdaPremiumPostSubsystem::ResetSeenOrders(const TCHAR* Reason)
{
	const int32 Was = SeenOrders.Num();
	SeenOrders.Reset();
	UnseenCount = GetFilledRowCount();
	bNoticeWanted = (UnseenCount > 0);
	ApplyNoticeIcon(TEXT("★ нововведение: набор просмотренного сброшен — всё содержимое снова новое"));
	UE_LOG(LogAsdaPost, Log,
		TEXT("[элитный ящик] ★ нововведение «просмотрено»: набор сброшен (%s), было %d записей; ")
		TEXT("непросмотренных заказов теперь %d"), Reason, Was, UnseenCount);
}

void UAsdaPremiumPostSubsystem::SetSeenGateEnabled(bool bOn)
{
	if (bSeenGateEnabled == bOn) { return; }
	bSeenGateEnabled = bOn;
	if (!bOn)
	{
		// Откат к эталону: памяти о просмотре нет, иконка обязана гореть на любом непустом ящике.
		SeenOrders.Reset();
		UnseenCount = GetFilledRowCount();
		bNoticeWanted = (UnseenCount > 0);
		ApplyNoticeIcon(TEXT("выключено нововведение «просмотрено» — поведение эталона восстановлено"));
	}
	else
	{
		UnseenCount = GetFilledRowCount();
		bNoticeWanted = (UnseenCount > 0);
		ApplyNoticeIcon(TEXT("включено нововведение «просмотрено» — набор пуст, весь ящик считается новым"));
	}
	UE_LOG(LogAsdaPost, Log,
		TEXT("[элитный ящик] признак «просмотрено» %s (эталон — ВЫКЛЮЧЕНО: там такого механизма нет)"),
		bSeenGateEnabled ? TEXT("ВКЛЮЧЁН") : TEXT("выключен"));
}
