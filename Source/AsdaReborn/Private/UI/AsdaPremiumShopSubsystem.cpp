// Copyright (c) 2026 @sitxovski (Discord)
// AsdaPremiumShopSubsystem.cpp — КЭШ-МАГАЗИН ПРЕМИУМ-МОЛЛА, окно `SHELLITEM_STORE_WIN` (6000)
//   и вкладка витрины наборов `SHELLITEM_STORE_Packageshop` (58300).
//   Весь реверс с адресами, таблица «вкладка → список» и список переиспользованных общих точек —
//   в шапке `Public/UI/AsdaPremiumShopSubsystem.h`. Ни одно число здесь не выдумано.
//
// ═══════════ ТАБЛИЦА КОНТРОЛОВ (IDNum → узел WBP_SHELLITEM_STORE_WIN) ═════════════════════════
//   6001  SHELLITEM_STORE_CLOSE      SHELLITEM_STORE_CLOSE            крестик
//   6002  SHELLITEM_STORE_INDEX      SHELLITEM_STORE_INDEX_OPT0…2     полоса «Новые/Все вещи»
//   6009  SHELLITEM_STORE_MENU       SHELLITEM_STORE_MENU_OPT0…9      полоса категорий
//   6004/6007 фильтры                SHELLITEM_STORE_CHECK/_CHECK01   ★ эталон ГАСИТ оба
//   6084+9i ячейка                   SHELLITEM_POP{nn}_*              21 карточка
//   6038+6j строка корзины           SHELLITEM_SELBASE{n} + _TIT/_PRI/_MAC/_CHK/_DIM{n}
//   6029/6080/6081                   SHELLITEM_STORE_LIST_TITLE / _SUM_Text / _SUM_VAL
//   6037/6275                        SHELLITEM_STORE_BUT00 / _BUT07
//   6279/6280                        SHELLITEM_FUNC_NUM00 / _NUM01
//   6281…6288                        SHELLITEM_FUNC_NEXT(P)/_BACKS(P)/_FIRST1,2/_END1,2
//   6025/6026                        SHELLITEM_STORE_TXT_11 / _TXT09   «Сейчас GXP» / значение
//   6276/6277                        SHELLITEM_MAC_CHARGE / ShellItem_CashCheck_Btn
//   40000/40001                      ShellItem_PreCashTxt / ShellItem_PreCash  ★ мёртвые (К9)
//   58300                            SHELLITEM_STORE_Packageshop_grp   вкладка наборов
//   58310+20i                        Package_{n}_*                     12 карточек наборов
#include "UI/AsdaPremiumShopSubsystem.h"
#include "AsdaPerf.h"   // ★ R4-b (14.11.2026): области замера времени игрового потока
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
#include "Npc/AsdaNpcDialogSubsystem.h"     // ★ волна В4 ремесла: хвост 5045 при закрытии (восстановление снимка окон)
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

DEFINE_LOG_CATEGORY(LogAsdaPremShop);   // ★ R2: было DEFINE_LOG_CATEGORY_STATIC — категория общая для частей

namespace {
	const TCHAR* kListTablePath = TEXT("/Game/AsdaAssets/Data/NPC/DT_PremiumShopList.DT_PremiumShopList");

	constexpr int32 kLocConfirm1  = 20060;  // «[ss1] - купить?» — модалка (подтверждение одной вещи)

	// ── Адрес доната (эталон `UI_OpenDonatePage_ECACA0@0xECACA0`: `ShellExecuteA("open", …)`) ──
	const TCHAR* kDonateUrl = TEXT("");   // ★ 27.10.2026 (владелец): ссылка на area-game.ru снята — «Пополнить» пока ничего не открывает
}

	// ═══════════ КЛЮЧИ ОТКАТА ДЕФЕКТОВ ОРИГИНАЛА (закон `fix-original-defects`) ═══════════
	// К1/К2 — переполнение стека и пропуск восьмой позиции в `boosterShopSendBuyItem@0xC2F240`
	//   (`for (slotIdx = 0; slotIdx < count; ++slotIdx) if (slotIdx != 7) …` при буфере 172 б).
	//   Исправлено в СБОРЩИКЕ тела (`UAsdaNetSubsystem::BuildPremiumShopBody`, ровно 7 записей);
	//   здесь ключ ограничивает ЧИСЛО позиций, уходящих в один пакет.
	// ★★★ ЖАЛОБА ВЛАДЕЛЬЦА 2026-08-28: «белая кромка не подстраивается под содержимое».
	//   Рамка витрины 6083 в оригинале ЖИВАЯ (см. `ApplyPanelHeight` в .h); порт её не трогал.
	ASDA_DEV_CVAR(int32, CVarLegacyPanelHeight,
		TEXT("asda.PremiumMall.LegacyPanelHeight"), 0,
		TEXT("1 = НЕ подгонять высоту белой рамки витрины 6083 (как было до 2026-08-28: всегда 500 px "
		     "из .JUI); 0 = считать её по эталонной формуле 98*(рядов/3)+48 и по фактически "
		     "показанным карточкам."),
		ECVF_Default);

	// ★ 06.11.2026 [ОРИГИНАЛ]: звуки окна 6000 — открытие 5, закрытие 6, страницы и покупка 4. Порт окно вёл молча.
	ASDA_DEV_CVAR(int32, CVarPremiumUiSound,
		TEXT("asda.PremiumShop.UiSound"), 1,
		TEXT("Звуки окна премиум-магазина 6000, как в оригинале: 1 — играть, 0 — молча (прежнее поведение порта)."),
		ECVF_Default);

namespace {
	// ★★ [ИСПРАВЛЕНИЕ] Д-18 ДЛЯ ОКНА 6000 (24.10.2026) — ЗАЩЁЛКА «ЗАПРОС В ПОЛЁТЕ» НЕ СНИМАЕТСЯ, ЕСЛИ ОТВЕТА НЕТ.
	//   Эталон ставит `this[2071]` перед 5070 и снимает ТОЛЬКО в `shop_OnRecvTransactionResult@0xC30BD0` (5071).
	//   Старый GameServer на ответ шлюза с кодом вне item_table (`sub_4F6650` @0x4F69DC, `sub_532A90`) пишет
	//   «GP7's Response ItemCode is Invalid» и клиенту НЕ отвечает ничем — защёлка висела до закрытия окна, и КАЖДОЕ
	//   следующее нажатие отбивалось нотисом 1026 «Сервер не отвечает» (24.10.2026, покупка маунта 606). Снимаем защёлку
	//   по таймауту и пишем причину в журнал — тот же приём и то же значение, что у магазина бустеров (Д-18).
	static ASDA_DEV_CVAR(float, CVarShopPendingTimeout,
		TEXT("asda.PremiumMall.ShopPendingTimeout"), 10.f,
		TEXT("Секунд ожидания ответа 5071 окна 6000, после которых защёлка «запрос в полёте» снимается "
		     "(исправление Д-18 для премиум-магазина). 0 = как оригинал: защёлка держится до закрытия окна."),
		ECVF_Default);

	static ASDA_DEV_CVAR(int32, CVarLegacyShelves,
		TEXT("asda.PremiumMall.LegacyShelves"), 0,
		TEXT("0 = витрина строится по `PremiumShop.BIN` (живая навигация 58200…58203, как в оригинале); "
		     "1 = по спискам `Premium.SHE` мёртвой полосы 6009 (состояние порта до 2026-08-28)."),
		ECVF_Default);

	// К4 — off-by-one в `shopBuildSlotItemList@0xC382F0`: во всех девяти ветках `++slotIndex`
	//   стоит ДО проверки `> 21`, из-за чего пишется 22-й элемент в сетку на 21 ячейку.
	static ASDA_DEV_CVAR(int32, CVarFixSlotOffByOne,
		TEXT("asda.PremiumMall.FixSlotListOffByOne"), 1,
		TEXT("1 = страница вмещает ровно 21 карточку (исправление К4); 0 = эталонные 22."),
		ECVF_Default);
}

	// К8 — третий сегмент полосы 6002 живёт в скине, но кодом не заполняется.
	ASDA_DEV_CVAR(int32, CVarHideIndexSeg3,
		TEXT("asda.PremiumMall.HideIndexSeg3"), 1,
		TEXT("1 = гасить мёртвый третий сегмент полосы 6002 (К8); 0 = оставить с заглушкой скина."),
		ECVF_Default);

	// К17 (найден при порте) — сегмент 9 «Инструмент» полосы 6009 у эталона попадает в `default:`
	//   и НЕ МЕНЯЕТ список: витрина остаётся от прошлой вкладки, а подпись говорит «Инструмент».
	ASDA_DEV_CVAR(int32, CVarFixTab9,
		TEXT("asda.PremiumMall.FixTab9"), 1,
		TEXT("1 = сегмент 9 «Инструмент» показывает ПУСТУЮ витрину (у него нет своего списка) — "
		     "исправление К17; 0 = как оригинал: список остаётся от прошлой вкладки."),
		ECVF_Default);

namespace {
	// НАША ДОБАВКА (как у окна 6800): подсказка предмета по наведению. В оригинале её нет.
	static ASDA_DEV_CVAR(int32, CVarTooltips,
		TEXT("asda.PremiumMall.Tooltips"), 1,
		TEXT("1 = показывать подсказку товара по наведению (НАША ДОБАВКА, в оригинале её нет); 0 = выкл."),
		ECVF_Default);
}

// ═══════════════════════════════ ЖИЗНЕННЫЙ ЦИКЛ ════════════════════════════════════════════════
void UAsdaPremiumShopSubsystem::Initialize(FSubsystemCollectionBase& Collection)
{
	Super::Initialize(Collection);
	// ★★★ РОСПИСЬ «КАК ЗАКРЫТЬ СЕБЯ ПО ESC» — порт события 21 (`CControl_OnEvent21@0x12DCC30`).
	//   Окно `SHELLITEM_STORE_WIN` (IDNum 6000) стоит в эталонной таблице `kEscCloseWindows`: признак
	//   `CControl+0x228` ему ставит `CShellItemStore_InitControls@0xC28820`. Проход
	//   `UAsdaWindowSubsystem::CloseTopmostOnEsc` закрывает верхнее окно ИМЕННО ЭТИМ вызовом,
	//   а не снятием виджета: снятие оставило бы подсистеме состояние «открыто», и окно
	//   больше не открылось бы. Закрыватель идемпотентен — повторный вызов ничего не делает.
	Collection.InitializeDependency(UAsdaWindowSubsystem::StaticClass());
	if (UAsdaWindowSubsystem* EscWM = GetGameInstance()
		? GetGameInstance()->GetSubsystem<UAsdaWindowSubsystem>() : nullptr)
	{
		EscWM->RegisterEscCloser(kWindowId, FSimpleDelegate::CreateUObject(this, &UAsdaPremiumShopSubsystem::EscClose));   // ★ P101: панель 6296/6333 — первым слоем
	}
	Cards.SetNum(kCards);
	CartUi.SetNum(kCartRows);
	CartRows.SetNum(kCartRows);
	PkgUi.SetNum(kPkgCards);
	MenuButtons.SetNum(kMenuTabs);
	MenuLabels.SetNum(kMenuTabs);
	IndexButtons.SetNum(kIndexTabs);
	IndexLabels.SetNum(kIndexTabs);

	if (UGameInstance* GI = GetGameInstance())
	{
		if (UAsdaNetSubsystem* Net = GI->GetSubsystem<UAsdaNetSubsystem>())
		{
			HBuyResult = Net->OnPremiumShopResult.AddUObject(this, &UAsdaPremiumShopSubsystem::OnBuyResult);
			HBalance   = Net->OnPremiumBalance.AddUObject(this, &UAsdaPremiumShopSubsystem::OnBalance);
		}
	}
	UE_LOG(LogAsdaPremShop, Log,
		TEXT("[премиум-магазин] подсистема поднята: окно 6000, %d карточек, корзина %d, вкладок %d, "
		     "витрина наборов %d карточек"), kCards, kCartRows, kMenuTabs, kPkgCards);
}

void UAsdaPremiumShopSubsystem::Deinitialize()
{
	if (UGameInstance* GI = GetGameInstance())
	{
		if (UAsdaNetSubsystem* Net = GI->GetSubsystem<UAsdaNetSubsystem>())
		{
			Net->OnPremiumShopResult.Remove(HBuyResult);
			Net->OnPremiumBalance.Remove(HBalance);
		}
		if (UAsdaWindowSubsystem* WM = GI->GetSubsystem<UAsdaWindowSubsystem>())
		{
			WM->UnregisterUiZone(kWindowId);
			WM->UnregisterUiZone(FName(TEXT("SHELLITEM_STORE_WIN_GIFTPANEL")));   // ★ P101: зона панелей подарка 6296/6333
			WM->CloseWindow(kWindowId);
		}
	}
	Widget = nullptr;
	bOpen = false;
	bReady = false;
	Super::Deinitialize();
}

// ★★★ К-3: СБРОС СОСТОЯНИЯ СЕССИИ (шапка `AsdaSessionReset.h`). Подсистема живёт на
//   `GameInstance` и переживает разрыв связи, выход в выбор персонажа и травел; всё, что
//   ниже, — данные и защёлки ОДНОЙ сессии. Отправок здесь нет (закон `no-reentrant-net-callbacks`).
void UAsdaPremiumShopSubsystem::ResetSessionState(EAsdaSessionResetReason Reason)
{
	// ★ Волна В4: при сбросе сессии окна снимка не возвращаем (травел/разрыв) — снимок снимет `UAsdaWindowSubsystem`.
	bSnapshotTaken = false;
	CloseWindow();
	Entries.Reset();                  // витрина строится из ответов сервера
	PendingBuyCodes.Reset();
	CartRun.Stop();                   // [ИСПРАВЛЕНИЕ] 25.10.2026: поштучная покупка корзины не переживает сессию
	CartNextBatch.Reset(); bCartNextSend = false;
	Page = 0; MaxPage = 0; PkgPage = 0;
	SelCard = -1; TipCard = -1;
	CashGxp = 0;                      // баланс `CCharacter+0x44C` — принадлежность персонажа
	BuiltForPerPage = 0;
	bPackageTab = false;
	bEqSub2Shown = false;
	ShiftApplied = -1.f;
	(void)Reason;
}

void UAsdaPremiumShopSubsystem::Tick(float /*DeltaTime*/)
{
	ASDA_PERF_SCOPE("Tick.PremiumShop");   // ★ R4-b (14.11.2026): область замера (AsdaPerf.h; в Shipping пусто)
	// [ИСПРАВЛЕНИЕ] Д-18 (окно 6000): снять зависшую защёлку «запрос в полёте», иначе покупки молча отбиваются 1026.
	//   Проверка стоит ДО выхода по `bOpen`: открытие/закрытие окна защёлку и так сбрасывают, а самотест проверяет её без окна.
	if (bPending)
	{
		const float Limit = CVarShopPendingTimeout.GetValueOnGameThread();
		if (Limit > 0.f && (FPlatformTime::Seconds() - PendingSince) > (double)Limit)
		{
			bPending = false;
			UE_LOG(LogAsdaPremShop, Warning,
				TEXT("[премиум-магазин] ответ 5071 не пришёл за %.0f с — защёлка снята (Д-18), покупки снова доступны. ")
				TEXT("Прошлый запрос: кодов %d, первый %d"),
				Limit, PendingBuyCodes.Num(), PendingBuyCodes.Num() > 0 ? PendingBuyCodes[0] : 0);
			if (CartRun.bRun) { StopCartRun(TEXT("ответ 5071 не пришёл (таймаут Д-18)")); }
			CartNextBatch.Reset();   // второй пакет смешанной корзины после таймаута не шлём
			bCartNextSend = false;
		}
	}
	// [ИСПРАВЛЕНИЕ] 25.10.2026 смешанная корзина: второй пакет — отсюда (ответ 5071 пришёл в сетевом колбэке).
	if (bCartNextSend)
	{
		bCartNextSend = false;
		TArray<int32> Next = MoveTemp(CartNextBatch);
		CartNextBatch.Reset();
		if (bOpen && !bPending && Next.Num() > 0) { SendBuy(Next, bCartNextTable1, /*bToInventory*/true); }
		else { UE_LOG(LogAsdaPremShop, Log, TEXT("[премиум-магазин] смешанная корзина: второй пакет не отправлен (окно закрыто или запрос в полёте)")); }
	}
	// [ИСПРАВЛЕНИЕ] 25.10.2026 корзина поштучно: следующую позицию шлём отсюда — ответ 5071 пришёл в сетевом колбэке.
	if (CartRun.bSendNext)
	{
		CartRun.bSendNext = false;
		if (!bOpen) { StopCartRun(TEXT("окно 6000 закрыто")); }
		else if (bPending) { StopCartRun(TEXT("защёлка уже взведена чужим запросом")); }
		else { SendNextCartItem(); }
	}
	if (!bOpen || !bReady) { Mouse.Reset(); return; }
	Mouse.Update();
	// ★ ИСПРАВЛЕНИЕ 11.10.2026 (дедуп №2, В2-A, U9): было — без модального гейта (только «окно выше под курсором»), и
	//   карточки ловили нажатие под модальным нотисом в стороне от него; стало как у магазина снаряжения 6800.
	//   Эталон: нажатие 0xB карточек 6000 приходит в `uiOnCommandDispatch@0xC235C0` (ui_core.c:25399, тот же обработчик,
	//   что у 6800 — ветки `a3 == 6000` / `a3 == 6800`), а события ему рождает диспетч мыши менеджера окон, который при
	//   захвате модалки окна не обходит вовсе (`uiWindowMgrDispatchMouse@0x12AF540`, ui_core.c:104149-104153).
	//   Разбор, порядок и ключ отката `asda.UI.ModalGatePolling 0` — в шапке `AsdaUi::IsModalDispatchBlocked`.
	// ★ 08.11.2026 (пачка 18, баг 2): 6296 теперь сама модальный контрол окна (`RegisterOriginalModal`) — её комбо запирает только ЧУЖАЯ
	//   модалка (`IsModalDispatchBlockedFor`: собственный модальный контрол окна опрос не запирает), карточки ниже — по прежнему гейту.
	if (!AsdaUi::IsModalDispatchBlockedFor(GetGameInstance(), kWindowId)) { TickGiftPanels(); }   // ★ P101: комбо панели 6296 (тот же кадр мыши)
	if (AsdaUi::IsModalDispatchBlocked(GetGameInstance()))
	{
		if (CVarTooltips.GetValueOnGameThread() != 0) { TickTooltip(-1); }   // висящую подсказку карточки гасим
		Mouse.Reset();
		CardDbl.Reset();   // модальный нотис между щелчками рвёт пару (как у 6800)
		return;
	}

	// Какая карточка под курсором (по ФАКТИЧЕСКОЙ экранной геометрии — закон «проверка путём игрока»).
	int32 Over = -1;
	if (!bPackageTab)
	{
		for (int32 i = 0; i < kCards; ++i)
		{
			if (Cards[i].ItemCode <= 0 || !Cards[i].ItemIcon) { continue; }
			// Общая точка хит-теста по ЖИВОЙ геометрии Slate (закон `ui-window-drag-hittest`).
			if (AsdaUi::IsCursorOverWidget(Cards[i].ItemIcon.Get())) { Over = i; break; }
		}
	}

	// ★★★ КЛИК ПО ИКОНКЕ КАРТОЧКИ — ЗДЕСЬ, ПО ЖИВОЙ ГЕОМЕТРИИ (10.10.2026). ДЕФЕКТ ПОРТА: прежде клик ловили
	//   прозрачные кнопки-оверлеи `UAsdaIconCellGrid`, поставленные по позиции иконок В МОМЕНТ СБОРКИ; раскладка
	//   под экран (`ApplyContentShift`, «сетка вверх на N px») потом двигает сетку, а зоны оставались на месте —
	//   путём игрока (самотест `asda.Shop.SelfTestDblClick`, экран 1024×768) щелчок по иконке не доходил ВООБЩЕ.
	//   Тот же приём, что у магазина NPC (`UAsdaNpcShopSubsystem::TickDrag`) и магазина снаряжения (`TickCards`).
	//   Эталон ловит 0xB = нажатие ЛКМ (`uiOnCommandDispatch@0xC235C0`, ui_core.c:25477) — берём фронт нажатия.
	//   Окно выше (модалка покупки, нумпад) перехватывает нажатие — как у оверлея, который оно закрывало собой.
	if (Mouse.bLmbDown)
	{
		UAsdaWindowSubsystem* WM = GetGameInstance() ? GetGameInstance()->GetSubsystem<UAsdaWindowSubsystem>() : nullptr;
		const bool bBlocked = WM && (WM->IsPointerBlockedByTopmost(kWindowId) || WM->IsCursorOverHigherWindow(kWindowId));
		// Кнопка покупки карточки перехватывает нажатие целиком (как в магазине снаряжения), иначе щелчок по ней
		//   засчитался бы ещё и первым щелчком пары по карточке.
		const bool bOverBuy = (Over >= 0) && Cards[Over].BuyButton && AsdaUi::IsCursorOverWidget(Cards[Over].BuyButton.Get());
		if (!bBlocked) { OnCardIconPressed(bOverBuy ? -1 : Over); }
		else           { CardDbl.Reset(); }   // [ПОРТ] ревизия 10.10 МЕЛК-1: нажатие по окну выше — «мимо карточки», пара рвётся
	}

	if (CVarTooltips.GetValueOnGameThread() == 0) { return; }
	// ★ 31.10.2026 [ИСПРАВЛЕНИЕ] (`asda.Booster.PrevPaths`): окно выше витрины под курсором (окно «Вид» 15600 поверх карточек)
	//   — подсказки карточки нет: эталонное наведение (событие 8) получает только верхнее окно под курсором. Прежде карточка
	//   под «Видом» перебивала подсказку его ячейки.
	if (Over >= 0 && UAsdaBoosterPanelSubsystem::PrevPathsOn())
	{
		const UAsdaWindowSubsystem* WMt = GetGameInstance() ? GetGameInstance()->GetSubsystem<UAsdaWindowSubsystem>() : nullptr;
		if (WMt && WMt->IsCursorOverHigherWindow(kWindowId)) { Over = -1; }
	}
	TickTooltip(Over);
}

// ★ [ПОРТ] ТРЕБОВАНИЕ ВЛАДЕЛЬЦА 10.10.2026: в корзину — ДВОЙНЫМ щелчком. Эталон кладёт по ОДИНОЧНОМУ 0xB
//   (`uiOnCommandDispatch@0xC235C0` ui_core.c:25477 → `itemOnUseDispatch@0xC267E0` item_inventory.c:19073 →
//   `uiRefreshItemSlotList@0xC36010`). Первый щелчок теперь только выбирает карточку (`SelCard`; рамки выбора
//   у этого окна в порту нет, подсказка — по наведению), второй по той же карточке в пределах `GetDoubleClickTime`
//   — `OnCardClicked` ровно один раз. Откат: `asda.Shop.DblClickToCart 0`.
// ★ 31.10.2026: категория записи `PremiumShop.BIN` карточки (поле +4) — эталон хранит её в ячейке витрины ключом
//   `1000·Category + 100·Sub1 + Sub2` (`gap_EC4[56·idx + 3256]`); ветки 3000 (волосы) и 5000 (набор) клика по карточке
//   разбираются раньше «Вида». Вкладки 0/1 («новинки») собраны по флагу +24 — категорию берём из самой записи.
//   −1 = категория неизвестна (запасной путь витрины `.she`): у такой записи ветки волос/наборов нет.
static int32 PremiumCardBinCategory(const TArray<FAsdaPremiumShopBinRow>& Rows, int32 CatTab, int32 Code)
{
	if (CatTab >= 2 && CatTab < UAsdaPremiumShopSubsystem::kCatTabs) { return kCatToField[CatTab]; }
	const int32 WantFlag = (CatTab == 0) ? 1 : 2;
	for (const FAsdaPremiumShopBinRow& R : Rows)
	{
		if (R.ItemId == Code && R.NewFlag == WantFlag) { return R.Category; }
	}
	return -1;
}

// ★ 31.10.2026 порт `cItem_GetSlotCategoryByType@0xC23350` (`AsdaStory_game.c`): слот экипировки по типу предмета;
//   −1 = «не надевается» (расходники, бустеры — `ItemType` 11/13). Плюс правка вызывающего `itemOnUseDispatch`:
//   запись второй таблицы с подтипом ≠ 15 → 100.
static int32 PremiumCardSlotCategory(const UAsdaItemTableSubsystem* IT, int32 Code)
{
	FAsdaSpecialItemlistRow Sp;
	const bool bSp = IT && IT->GetSpecialItem(Code, Sp);
	if (bSp && Sp.SubType == 15) { return 10; }
	const FAsdaItemRecord* R = IT ? IT->GetItem(Code) : nullptr;
	int32 Cat = -1;
	if (R)
	{
		switch (R->ItemType)
		{
		case 0: Cat = 8; break;
		case 1: Cat = 2; break;
		case 2: Cat = 4; break;
		case 3: Cat = 5; break;
		case 4: Cat = 3; break;
		case 8: Cat = 7; break;
		case 9: Cat = 6; break;
		default: break;
		}
	}
	if (bSp) { Cat = 100; }   // `Table2Record && subType != 15` (подтип 15 вышел выше)
	return Cat;
}

void UAsdaPremiumShopSubsystem::OnCardIconPressed(int32 Card)
{
	if (Card < 0 || !Cards.IsValidIndex(Card) || Cards[Card].ItemCode <= 0) { CardDbl.Reset(); return; }
	// ★★★ 31.10.2026 [ОРИГИНАЛ] (`asda.Booster.PrevPaths`): нажатие 0xB по карточке БУСТЕРА открывает окно «Вид» 15600 —
	//   `itemOnUseDispatch@0xC267E0` (`AsdaStory_game.c:266861-266900`): не волосы (3000) и не набор (5000), слот
	//   `cItem_GetSlotCategoryByType` ≤ −1, запись `item_list` с `DurationOrEnchant ≥ 1` ⇒
	//   `uiStorageWindow_ResetState` + `boosterWindowPopulate(…, 1, код)` + `set_o2B9(…, 1)` (щелчок по ячейкам «Вида» жив),
	//   выбор карточки (`gap_2734[680]`) и рамка выбора. Окно эталон НЕ двигает. В корзину такая карточка у эталона щелчком
	//   не попадает вовсе; у порта двойной щелчок — правило владельца 10.10.2026 — оставлен как был.
	if (UAsdaBoosterPanelSubsystem::PrevPathsOn())
	{
		const int32 Code = Cards[Card].ItemCode;
		const int32 BinCat = PremiumCardBinCategory(BinRows, CatTab, Code);
		UGameInstance* GI = GetGameInstance();
		const UAsdaItemTableSubsystem* IT = GI ? GI->GetSubsystem<UAsdaItemTableSubsystem>() : nullptr;
		const FAsdaItemRecord* Rec = IT ? IT->GetItem(Code) : nullptr;
		if (BinCat != 3 && BinCat != 5 && Rec && Rec->DurationOrEnchant >= 1 && PremiumCardSlotCategory(IT, Code) <= -1)
		{
			if (UAsdaBoosterPanelSubsystem* Bp = GI->GetSubsystem<UAsdaBoosterPanelSubsystem>())
			{
				const bool bOk = Bp->OpenPrevWindowFrom(Code, EAsdaPrevSource::PremiumShop, /*bCellsActive*/ true);
				UE_LOG(LogAsdaPremShop, Log, TEXT("[премиум-магазин] карточка %d (код %d): бустер → окно «Вид» 15600 %s"),
					Card, Code, bOk ? TEXT("открыто") : TEXT("НЕ открыто (нет строки booster_list)"));
			}
		}
	}
	// ★ 08.11.2026 (пачка 16, баг 6): выбор карточки — сразу рамка 6273 (оригинал: `itemOnUseDispatch` → `uiTabPanel_MoveSelectionHighlight`,
	//   item_inventory.c:19116-19191). [ПОРТ] У расходника без срока (`durationOrEnchant < 1`) оригинал на щелчок не делает НИЧЕГО (ни рамки,
	//   ни корзины), а у порта первый щелчок по правилу владельца 10.10.2026 выбирает ЛЮБУЮ карточку — рамка встаёт на каждую выбранную.
	SelCard = Card;
	MoveSelectionFrame(Card);
	if (CardDbl.Click(Card)) { OnCardClicked(Card); }
}

FString UAsdaPremiumShopSubsystem::Loc(int32 Id) const
{
	// ★ Волна 1 (14.09.2026, В2): общая точка `AsdaNotice::Loc` — теги срезаны, маркеры как в таблице (как было).
	return AsdaNotice::Loc(GetGameInstance(), Id);
}

void UAsdaPremiumShopSubsystem::NoticeChat(int32 LocId) const
{
	// ★ Волна 1 (14.09.2026, В2): маршрут — фасад `AsdaNotice` (категория та же, поведение то же).
	AsdaNotice::Chat(GetGameInstance(), EAsdaMsgCategory::Trade, LocId);
}

void UAsdaPremiumShopSubsystem::NoticeTip(int32 LocId) const
{
	// ★ МИСНЕЙМ (аудит 01_cpp_ui б-4): это МОДАЛЬНОЕ окно 1500, а не подсказка.
	// ★ Волна 1 (14.09.2026, В2): единый маршрут `AsdaNotice::MsgBox` — окно 1500, без оконной подсистемы —
	//   строка в ленту той же категорией. Категория/подпись кнопки — прежние.
	AsdaNotice::MsgBox(GetGameInstance(), EAsdaMsgCategory::ItemRefused, Loc(LocId));
}

// ═══════════════════════════════ ДАННЫЕ ВИТРИНЫ ════════════════════════════════════════════════
// Порт `shopPremiumMall_LoadShe@0xBD6460` — ЧЕРЕЗ ОБЩУЮ ТОЧКУ магазина 6800: у обоих окон один
//   класс и один формат витрины, различается только файл (`Premium.she` против `FreeShop.she`).
bool UAsdaPremiumShopSubsystem::LoadShe()
{
	if (bSheLoaded) { return RawLists.Num() == kLists; }
	const UAsdaItemTableSubsystem* IT =
		GetGameInstance() ? GetGameInstance()->GetSubsystem<UAsdaItemTableSubsystem>() : nullptr;
	if (!UAsdaEquipShopSubsystem::LoadSheListsFromTable(kListTablePath, IT, RawLists, TEXT("премиум-магазин")))
	{
		return false;
	}
	bSheLoaded = true;
	return true;
}

int32 UAsdaPremiumShopSubsystem::ActiveListIndex() const
{
	if (MenuTab < 0 || MenuTab >= kMenuTabs) { return -1; }
	const int32 L = kTabToList[MenuTab];
	if (L < 0 && CVarFixTab9.GetValueOnGameThread() == 0)
	{
		// Эталон: `default: break` — список НЕ меняется, остаётся от прошлой вкладки.
		return -2;
	}
	return L;
}

const TArray<FAsdaPremiumEntry>& UAsdaPremiumShopSubsystem::ActiveEntries() const
{
	return Entries;
}

// Раскладка активного списка по страницам: `page = i / PerPage`, `slot = i % PerPage` (эталон).
// ★★★ ПЕРЕВЕДЕНО НА ЖИВУЮ НАВИГАЦИЮ 2026-08-28. Раньше страница строилась по спискам
//   `Premium.SHE` — это данные СТАРОЙ полосы 6009, которую эталон гасит навсегда
//   (`ida.c:479949`). Настоящий источник — `PremiumShop.BIN` (см. `LoadBin`/`BuildActiveCodes`).
//   Ключ `asda.PremiumMall.LegacyShelves 1` возвращает прежний путь через `.she` (для сверки и
//   как запасной, если таблица `DT_PremiumShopBin` не импортирована).
void UAsdaPremiumShopSubsystem::BuildPages()
{
	Entries.Reset();
	MaxPage = 0;

	UGameInstance* GI = GetGameInstance();
	TArray<int32> Codes;
	const bool bLegacy = (CVarLegacyShelves.GetValueOnGameThread() != 0);
	int32 PerPage = 0;
	if (!bLegacy && LoadBin())
	{
		BuildActiveCodes(Codes);
		PerPage = PerPageForTab();
	}
	else
	{
		// Запасной путь: списки `.she` по мёртвой полосе 6009 (как было до 2026-08-28).
		const int32 L = ActiveListIndex();
		if (L == -2) { return; }
		if (L < 0 || !RawLists.IsValidIndex(L)) { return; }
		Codes = RawLists[L];
		const UAsdaEquipShopSubsystem* Eq = GI ? GI->GetSubsystem<UAsdaEquipShopSubsystem>() : nullptr;
		PerPage = Eq ? Eq->ItemsPerPage() : 12;
	}
	const int32 Cap = (CVarFixSlotOffByOne.GetValueOnGameThread() != 0) ? kCards : (kCards + 1);
	PerPage = FMath::Clamp(PerPage, 1, Cap);
	BuiltForPerPage = PerPage;

	const UAsdaItemTableSubsystem* IT = GI ? GI->GetSubsystem<UAsdaItemTableSubsystem>() : nullptr;
	int32 Placed = 0;
	for (int32 Code : Codes)
	{
		// Эталон везде отсекает коды, для которых нет записи ни в одной из двух таблиц.
		FAsdaSpecialItemlistRow Sp;
		const bool bHas = IT && (IT->GetItem(Code) != nullptr || IT->GetSpecialItem(Code, Sp));
		if (!bHas) { continue; }
		FAsdaPremiumEntry E;
		E.ItemCode   = Code;
		E.Page       = Placed / PerPage;
		E.SlotInPage = Placed % PerPage;
		MaxPage      = FMath::Max(MaxPage, E.Page);
		Entries.Add(E);
		++Placed;
	}
	Page = FMath::Clamp(Page, 0, MaxPage);
	UE_LOG(LogAsdaPremShop, Log,
		TEXT("[премиум-магазин] категория %d (инструмент %d, класс %d, уровень3 %d/%d): ")
		TEXT("%d кодов, годных %d, на страницу %d, страниц %d"),
		CatTab, ToolSub, EqSub, EqSub2, bEqSub2Shown ? 1 : 0,
		Codes.Num(), Entries.Num(), PerPage, MaxPage + 1);
}
