// Copyright (c) 2026 @sitxovski (Discord)
// AsdaPremiumMallSubsystem.cpp — ДИСПЕТЧЕР ПРЕМИУМ-МОЛЛА, окно `Premium_Menu` (30000).
//   Реверс, адреса, разрешение расхождения «26000 против 58300» и гейты — в шапке
//   `Public/UI/AsdaPremiumMallSubsystem.h`. Ни одно число не выдумано.
//
// ═══════════ ТАБЛИЦА КОНТРОЛОВ (gamemain_jui.ini `Premium_Menu`, 10 узлов) ════════════════════
//   30000 Premium_Menu         Premium_Menu_0_0                рамка 220×42, AlwaysTop, Visible=0
//   −1    Premium_Menu_L/_R    Premium_Menu_L_1_0 / _R_2_0     торцы рамки 53×42 / 52×42
//   30010 Premium_Menu_Txt     Premium_Menu_Txt_grp            «Select Shop» — МЁРТВ (К10)
//   30011 Premium_Menu_Select  Premium_Menu_Select             ComboBox — МЁРТВ (К10)
//   30002 PremiumMall_Menu     PremiumMall_Menu_grp + _OPT0…3  ★ ЖИВОЙ переключатель, 4 сегмента
//   −1    Premium_Menu_Line1/2 Premium_Menu_Line{1,2}_grp      разделители сегментов
#include "UI/AsdaPremiumMallSubsystem.h"
#include "AsdaPerf.h"   // ★ R4-b (14.11.2026): области замера времени игрового потока
#include "AsdaDevConsole.h"   // ★ античит А2: ASDA_DEV_CVAR / ASDA_DEV_CONSOLE — дев-ключи и команды вне Shipping
#include "UI/AsdaJuiWindowHandle.h"   // ★ волна 3 (15.09.2026): каркас окон JUI
#include "UI/AsdaNotice.h"   // ★ волна 1: фасад нотисов (Loc / Chat / MsgBox)
#include "UI/AsdaBoosterPanelSubsystem.h"  // ★ ревизия Л4/Л5 (МЕЛК-3): гейт 1 молла — окно бустера 10050 (`CObject_GetField254`)
#include "UI/AsdaWarehouseSubsystem.h"     // ★ ревизия Л4/Л5 (МЕЛК-3): гейт 1 молла — свиток возврата склада (`CObject_GetField5581`)
#include "UI/AsdaTradeStateSubsystem.h"   // ★ 30.09.2026 (волна Л5 торговли): гейт «лавка открыта» 22000/23000 → 107022

#include "Login/AsdaButtonRelay.h"
#include "UI/AsdaItemTooltipSubsystem.h"
#include "UI/AsdaMessageSubsystem.h"
#include "UI/AsdaBoosterShopSubsystem.h"   // ★ сегмент 1 «Бустер» (по ключу)
#include "UI/AsdaHairShopSubsystem.h"   // ★ сегмент 2 «Салон причёсок» (окно 15000)
#include "UI/AsdaPremiumShopSubsystem.h"
#include "UI/AsdaSystemMenuSubsystem.h"
#include "UI/AsdaUiHelpers.h"
#include "UI/AsdaWindowSubsystem.h"
#include "World/AsdaPlayerController.h"

#include "Blueprint/WidgetTree.h"
#include "Components/Button.h"
#include "Components/TextBlock.h"
#include "Engine/GameInstance.h"
#include "Engine/World.h"
#include "HAL/IConsoleManager.h"
#include "TimerManager.h"
#include "World/AsdaZoneSubsystem.h"   // ★ 30.10.2026: признак «город» (бит 0x80 клетки) для предупреждения 97100
#include "UI/AsdaCraftSubsystem.h"   // ★ волна В3 ремесла: единый предикат «окно ремесла открыто» (`IsAnyCraftWindowOpen`)

DEFINE_LOG_CATEGORY_STATIC(LogAsdaMall, Log, All);

namespace
{
	const FName  kWindowId(TEXT("Premium_Menu"));
	const TCHAR* kWbpPath = TEXT("/Game/AsdaAssets/UI/Player_UI/Premium/WBP_Premium_Menu");

	// Подписи сегментов — `CBoosterShop::Init` (`ida.c:420658-420671`), порядок 0…3.
	constexpr int32 kSegLoc[UAsdaPremiumMallSubsystem::kSegments] =
	{
		99401,   // 0 «Элитный»        → магазин 6000
		99400,   // 1 «Бустер»         → бустер-шоп 15300
		99402,   // 2 «Салон причесок» → парикмахерская 15000
		99403    // 3 «Набор»          → вкладка 58300 окна 6000
	};

	// Нотисы гейтов (§1.3 отчёта).
	constexpr int32 kLocOwnShop = 107022;   // «Ты в своей лавке» — чат, жёлтый
	constexpr int32 kLocInFight = 6946;     // «В бою хранилище недоступно» — чат, зелёный
	constexpr int32 kLocWarnMob = 97100;    // предупреждение — ОБЩАЯ ПЛАШКА, не чат

	// ★ Предупреждение 97100. В эталоне оно гасится битом 0x80 поля
	//   `controlBar_SlotIsActive(localPlayer, effectSlots[112].field_314)` (`ida.c:490000`).
	//   ★ 30.10.2026: соответствие НАЙДЕНО — `controlBar_SlotIsActive@0x95BE40` возвращает байт флага клетки
	//   .TER под игроком (`field_314` — индекс клетки), бит 0x80 = город/безопасная зона (тот же признак, что
	//   переключает BGM города, game_main.c:3821). Порт: `UAsdaZoneSubsystem::IsLocalPlayerInPeaceZoneNow`,
	//   гейт — ключ `asda.PremiumMall.WarnOutsideTownOnly` ниже. Этот ключ — общий выключатель предупреждения.
	static ASDA_DEV_CVAR(int32, CVarOpenWarning,
		TEXT("asda.PremiumMall.OpenWarning"), 1,
		TEXT("1 = показывать предупреждение 97100 при открытии молла (как оригинал, у которого "
		     "гасящий бит 0x80 в порту не воспроизводим); 0 = не показывать."),
		ECVF_Default);

	// ★★ [ОРИГИНАЛ] 30.10.2026 (баг игроков «Caution Message кеш-магазина в городе»): эталон
	//   (`uiInventoryConfirmAction@0xC4AD60`, ui_core.c:30542) показывает 97100 ТОЛЬКО когда
	//   `(controlBar_SlotIsActive(клетка игрока) & 0x80) == 0` — то есть вне города. Порт показывал всегда:
	//   признак зоны не был сопоставлен. 1 = как оригинал (в городе/безопасной зоне не показывать),
	//   0 = прежнее поведение порта (показывать везде).
	static ASDA_DEV_CVAR(int32, CVarWarnOutsideTownOnly,
		TEXT("asda.PremiumMall.WarnOutsideTownOnly"), 1,
		TEXT("1 = предупреждение 97100 при открытии молла только вне города (бит 0x80 клетки, как оригинал); "
		     "0 = показывать и в городе (прежнее поведение порта)."),
		ECVF_Default);

	// ★ Сегменты 1 (бустер-шоп 15300) и 2 (парикмахерская 15000) в ЭТОТ этап не входят.
	//   1 = сообщать игроку строкой в чат «раздел ещё не реализован» (нужен для проверки путём
	//   игрока), 0 = молча оставаться на прежнем сегменте, как сделал бы оригинал без окна.
	static ASDA_DEV_CVAR(int32, CVarStubNotice,
		TEXT("asda.PremiumMall.StubNotice"), 1,
		TEXT("1 = писать в чат, что раздел молла ещё не портирован (сегменты 1 и 2); 0 = молча."),
		ECVF_Default);

	// ★★★ СЕГМЕНТ 1 «Бустер» В ОРИГИНАЛЕ НЕНАЖИМАЕМ — И ЭТО ДОКАЗАНО, А НЕ ПРЕДПОЛОЖЕНО.
	//   `BoosterShop_InitWindow@0xB7E4A0` кладёт индексы **1** и **3** в контейнер контрола 30002
	//   по смещению +0x268 (`sub_931C4A(pPremiumMenuBtn, 1)` и `(…, 3)`, `ida.c:420670-420671`
	//   → `sub_B807F0`: `containerInsertOrGrow(this + 154, &a2)`), а `uiList_OnItemClickSelect
	//   @0x12F6440` — ЕДИНСТВЕННАЯ во всём бинаре функция, поднимающая событие 0x10501 (66817), —
	//   для индексов из этого контейнера МОЛЧА возвращается ДО `RaiseEvent` и до смены выбора.
	//   Подтверждено вторым методом — дизассемблированием `0x12F6440` прямо из exe.
	//   ⇒ по умолчанию порт ведёт себя как оригинал: сегмент 1 окна НЕ открывает. Живой вход в
	//   магазин бустеров — кнопка NPC (код диалога 30034), она зовёт
	//   `UAsdaBoosterShopSubsystem::OpenWindow()`. Ключ ниже включает сегмент как ФИЧУ.
	// ═══════════ ★★★ РЕШЕНИЕ ВЛАДЕЛЬЦА 2026-08-28: ПОЛОСЫ-ПЕРЕКЛЮЧАТЕЛЯ В ИГРЕ НЕТ ═══════════
	//   ЭТО НЕ ПОВЕДЕНИЕ ОРИГИНАЛА, А ОСОЗНАННОЕ ОТСТУПЛЕНИЕ. Формулировка владельца по скриншоту
	//   полосы «Элитный | Бустер | Салон причёсок | Набор»: «нужно это меню убрать с переключениями».
	//
	//   ЧЕМ ОНО ОБОСНОВАНО И В ДАННЫХ. Половина полосы в оригинале НЕНАЖИМАЕМА:
	//   `BoosterShop_InitWindow@0xB7E4A0` кладёт индексы **1** и **3** в контейнер контрола 30002 по
	//   смещению +0x268 (`ida.c:420670-420671`), а `uiList_OnItemClickSelect@0x12F6440` — ЕДИНСТВЕННАЯ
	//   во всём бинаре функция, поднимающая событие 0x10501, — для индексов из этого контейнера
	//   МОЛЧА возвращается ДО `RaiseEvent` и до смены выбора. То есть «Бустер» и «Набор» не нажимались
	//   и в оригинале, а живыми оставались лишь сегменты 0 и 2.
	//
	//   ЧТО СТАЛО СО ВХОДАМИ (ни один не потерян):
	//     сегм. 0 «Элитный»        → магазин 6000 открывается НАПРЯМУЮ: хоткей `\` (cmd39) и кнопка
	//                                главного меню 73407 теперь тумблерят его без рамки-меню;
	//     сегм. 1 «Бустер»         → окно 15300 имеет СВОЙ вход — кнопка NPC 146 «Робот Пре-Пре 19»;
	//     сегм. 2 «Салон причёсок» → окно 15000 выводится КНОПКОЙ NPC 132 «Парикмахер Юки»
	//                                (`UAsdaHairShopSubsystem::OpenWindow`);
	//     сегм. 3 «Набор»          → витрина наборов живёт ВКЛАДКОЙ 58300 ВНУТРИ окна 6000, туда и
	//                                попадаем изнутри магазина. ⚠ Маршрутизация сегмента 3 уходит
	//                                вместе с меню — в оригинале он и так был ненажимаем.
	static ASDA_DEV_CVAR(int32, CVarLegacyMenu,
		TEXT("asda.PremiumMall.LegacyMenu"), 0,
		TEXT("0 = полосы-переключателя 30002 в игре НЕТ (решение владельца 2026-08-28): хоткей и "
		     "кнопка 73407 тумблерят магазин 6000 напрямую, парикмахерская открывается кнопкой NPC 132. "
		     "1 = вернуть эталонную рамку `Premium_Menu` 30000 с четырьмя сегментами."),
		ECVF_Default);

	// ★ 25.10.2026 (ui_toggles, `Reports/ui_toggles/TOGGLE_AUDIT.md`) [ОРИГИНАЛ]. Эталон: латч молла `this+4`
	//   (`get_o4_3B40@0xB83B40`, его читает тумблер `uiInventoryConfirmAction@0xC4AD60` через `sub_939152`) снимает
	//   `uiStorageWindow_Close@0xB83AE0`, и её зовут ВСЕ пути закрытия магазина 6000: крестик/ESC (`uiHandleCommand@0xC23AC0`,
	//   ui_core.c:25986; `uiOnCommandDispatch@0xC235C0`, :25455), загрузка карты (game_main.c:12998). В порту магазин
	//   закрывается своим `CloseWindow` мимо молла — латч оставался «открыт», и следующее `\`/73407 «закрывало» уже закрытый
	//   магазин (открывался со второго нажатия, жалоба владельца 25.10). 1 = латч сверяется с видимостью адресата.
	static ASDA_DEV_CVAR(int32, CVarToggleFromFact,
		TEXT("asda.PremiumMall.ToggleFromFact"), 1,
		TEXT("1 (по умолчанию) = «молл открыт» только пока его адресат (магазин 6000 / рамка 30000 / парикмахерская) на экране ")
		TEXT("— как эталон, у которого каждое закрытие магазина снимает латч; 0 = прежнее (голый латч: после ESC/крестика ")
		TEXT("кнопка 73407 и `\\` открывали магазин только со второго раза)."),
		ECVF_Default);

	// ★ 25.10.2026 (ui_toggles) [ИСПРАВЛЕНИЕ]: переподписка хоткея `\` на новый контроллер после травела (разбор — у `BoundPc`).
	static ASDA_DEV_CVAR(int32, CVarRebindHotkey,
		TEXT("asda.PremiumMall.RebindHotkey"), 1,
		TEXT("1 (по умолчанию) = хоткей премиум-магазина переподписывается на новый контроллер после смены карты; ")
		TEXT("0 = прежнее (подписка только на первый контроллер — после смены карты клавиша не работала)."),
		ECVF_Default);

	// ★ 25.10.2026 (ревизия `Reports/wave_1025b_review/REVIEW.md`, 4.1) [ИСПРАВЛЕНИЕ]: кнопка меню 73407 (команда и предикат
	//   «молл открыт») подключается к системному меню, поднятому ЗАРАНЕЕ (`InitializeDependency`), а не «если уже поднято»
	//   (`GetSubsystem`). При другом порядке поднятия подсистем кнопка тихо оставалась без факта и без команды.
	static ASDA_DEV_CVAR(int32, CVarMenuDependency,
		TEXT("asda.PremiumMall.MenuDependency"), 1,
		TEXT("1 (по умолчанию) = системное меню поднимается до молла (зависимость подсистем), кнопка 73407 подключается всегда; ")
		TEXT("0 = прежнее (подключение только если меню уже поднято)."),
		ECVF_Default);

	static ASDA_DEV_CVAR(int32, CVarEnableBoosterSegment,
		TEXT("asda.PremiumMall.EnableBoosterSegment"), 0,
		TEXT("0 = сегмент 1 «Бустер» переключателя 30002 ничего не открывает, как в оригинале "
		     "(клиент помечает пункты 1 и 3 ненажимаемыми через контейнер +0x268); "
		     "1 = сегмент открывает магазин бустеров 15300."),
		ECVF_Default);
}

// ═══════════════════════════════ ЖИЗНЕННЫЙ ЦИКЛ ════════════════════════════════════════════════
void UAsdaPremiumMallSubsystem::Initialize(FSubsystemCollectionBase& Collection)
{
	Super::Initialize(Collection);
	SegButtons.SetNum(kSegments);
	SegLabels.SetNum(kSegments);

	// Кнопка главного меню 73407 — подписка сразу: системное меню поднимается тем же коллектором.
	// ★ Ревизия 4.1 (ключ `asda.PremiumMall.MenuDependency`): меню — зависимостью, порядок поднятия подсистем не важен.
	UAsdaSystemMenuSubsystem* MenuDep = (CVarMenuDependency.GetValueOnGameThread() != 0)
		? Collection.InitializeDependency<UAsdaSystemMenuSubsystem>() : nullptr;
	if (UGameInstance* GI = GetGameInstance())
	{
		if (UAsdaSystemMenuSubsystem* Menu = MenuDep ? MenuDep : GI->GetSubsystem<UAsdaSystemMenuSubsystem>())
		{
			Menu->OnMenuCommand.AddDynamic(this, &UAsdaPremiumMallSubsystem::OnMenuCommand);
			// ★ 25.10.2026 (ui_toggles) [ОРИГИНАЛ]: подсветка кнопки 73407 = «молл открыт» (эталон `uiPanel_SetTabVisible(…, 3, 1)`
			//   в `entity_char.c:45093` и `(…, 3, 0)` в `uiStorageWindow_Close` / `uiInventoryResetAndLeave`). Окна в таблице
			//   меню у 73407 нет, поэтому — предикат (как у гачи 73410); с ним же единый механизм меню ведёт и направление щелчка.
			Menu->RegisterWindowPredicate(kMenuBtn, [this]() { return IsOpen(); });
		}
	}
	UE_LOG(LogAsdaMall, Log,
		TEXT("[премиум-молл] диспетчер 30000 поднят: 4 сегмента, хоткей cmd%d, кнопка меню %d"),
		kCmdHotkey, kMenuBtn);
}

void UAsdaPremiumMallSubsystem::Deinitialize()
{
	if (UGameInstance* GI = GetGameInstance())
	{
		if (UAsdaSystemMenuSubsystem* Menu = GI->GetSubsystem<UAsdaSystemMenuSubsystem>())
		{
			Menu->OnMenuCommand.RemoveDynamic(this, &UAsdaPremiumMallSubsystem::OnMenuCommand);
		}
		if (UAsdaWindowSubsystem* WM = GI->GetSubsystem<UAsdaWindowSubsystem>()) { WM->CloseWindow(kWindowId); }
		if (UWorld* W = GI->GetWorld())
		{
			if (AAsdaPlayerController* Pc = Cast<AAsdaPlayerController>(W->GetFirstPlayerController()))
			{
				Pc->OnGameCommand.RemoveDynamic(this, &UAsdaPremiumMallSubsystem::OnGameCommand);
			}
		}
	}
	Widget = nullptr;
	bOpen = false;
	bReady = false;
	bPcBound = false;
	Super::Deinitialize();
}

// ★★★ К-3: СБРОС СОСТОЯНИЯ СЕССИИ (шапка `AsdaSessionReset.h`). Подсистема живёт на
//   `GameInstance` и переживает разрыв связи, выход в выбор персонажа и травел; всё, что
//   ниже, — данные и защёлки ОДНОЙ сессии. Отправок здесь нет (закон `no-reentrant-net-callbacks`).
void UAsdaPremiumMallSubsystem::ResetSessionState(EAsdaSessionResetReason Reason)
{
	if (bOpen) { CloseMall(); }
	bOpen = false;
	Segment = 0;
	(void)Reason;
}

void UAsdaPremiumMallSubsystem::Tick(float /*DeltaTime*/)
{
	ASDA_PERF_SCOPE("Tick.PremiumMall");   // ★ R4-b (14.11.2026): область замера (AsdaPerf.h; в Shipping пусто)
	// Единственная задача тика: дождаться контроллера игрока и подписаться на хоткей.
	//   ★ 25.10.2026 (ui_toggles): при `asda.PremiumMall.RebindHotkey 1` сверяем и сам контроллер — травел меняет его.
	if (!bPcBound || CVarRebindHotkey.GetValueOnGameThread() != 0) { BindPlayerController(); }
}

FString UAsdaPremiumMallSubsystem::Loc(int32 Id) const
{
	// ★ Волна 1 (14.09.2026, В2): общая точка `AsdaNotice::Loc` — теги срезаны, маркеры как в таблице (как было).
	return AsdaNotice::Loc(GetGameInstance(), Id);
}

// Чат — канал `Trade` (магазин/транзакции): маршрут задан в `DT_MessageRoutes`, окно кода не знает.
void UAsdaPremiumMallSubsystem::NoticeChat(int32 LocId) const
{
	// ★ Волна 1 (14.09.2026, В2): маршрут — фасад `AsdaNotice` (категория та же, поведение то же).
	AsdaNotice::Chat(GetGameInstance(), EAsdaMsgCategory::Trade, LocId);
}

// Общая ПЛАШКА подсказки (эталон: `CItem_BuildTooltipText(g_pItemTooltipUI, 0, −1, текст, 1)`).
void UAsdaPremiumMallSubsystem::NoticeTip(int32 LocId) const
{
	// ★ МИСНЕЙМ (аудит 01_cpp_ui б-4): это МОДАЛЬНОЕ окно 1500, а не подсказка.
	// ★ Волна 1 (14.09.2026, В2): единый маршрут `AsdaNotice::MsgBox` — окно 1500, без оконной подсистемы —
	//   строка в ленту той же категорией. Категория/подпись кнопки — прежние.
	AsdaNotice::MsgBox(GetGameInstance(), EAsdaMsgCategory::ItemRefused, Loc(LocId));
}

// ═══════════════════════════════ ВИДЖЕТ ════════════════════════════════════════════════════════
bool UAsdaPremiumMallSubsystem::EnsureWidget()
{
	// ★ Каркас окна — `FAsdaJuiWindowHandle::Ensure` (волна 3 аудита дублей): гвард `IsWindowLive`,
	//   сброс протухшего окна, открытие WBP, привязка, скрытие — в одной точке, в прежнем порядке.
	const EAsdaJuiEnsure R = FAsdaJuiWindowHandle(kWindowId, kWbpPath, kMenuZ).Ensure(GetGameInstance(), Widget, bReady,
		[this](UAsdaWindowSubsystem& WM)
		{
			WM.CloseWindow(kWindowId);
			Widget = nullptr; bReady = false; bOpen = false;
		},
		[this](UAsdaWindowSubsystem& WM)
		{
			SetupControls();
			// `.JUI`: у 30000 `AlwaysTop = "1"` — рамка молла живёт в полосе приоритетных окон.
			WM.SetWindowPriority(kWindowId, true);
		},
		[]()
		{
			UE_LOG(LogAsdaMall, Error, TEXT("[премиум-молл] WBP_Premium_Menu не загрузился (%s)"), kWbpPath);
		},
		[this](UAsdaWindowSubsystem&)
		{
			bOpen = false;
		});
	return FAsdaJuiWindowHandle::Succeeded(R);
}

void UAsdaPremiumMallSubsystem::SetupControls()
{
	bReady = false;
	Relays.Reset();
	DeadSelector.Reset();
	TabIdleColors.Reset();
	if (!Widget || !Widget->WidgetTree) { return; }

	auto F = [this](const FString& N) -> UWidget* { return AsdaUi::FindNodeByName(Widget, *N, nullptr); };

	int32 Found = 0;
	for (int32 S = 0; S < kSegments; ++S)
	{
		SegButtons[S] = Cast<UButton>(F(FString::Printf(TEXT("PremiumMall_Menu_OPT%d"), S)));
		// Подписи: первая зовётся `_Text`, остальные `_Text2`, `_Text3`, `_Text4` — так их
		//   именует общий шаг подписей вкладок (`Tools/asda_ui_tabbar_captions.py`), тем же
		//   правилом их кладёт `Tools/asda_ui_premium_menu_tabs.py`.
		SegLabels[S] = Cast<UTextBlock>(F(S == 0 ? FString(TEXT("PremiumMall_Menu_Text"))
		                                         : FString::Printf(TEXT("PremiumMall_Menu_Text%d"), S + 1)));
		if (SegButtons[S]) { ++Found; }
		if (SegLabels[S])
		{
			SegLabels[S]->SetAutoWrapText(false);   // закон `ui-autowrap-canvas-gotcha`
			const FString Cap = Loc(kSegLoc[S]);
			if (!Cap.IsEmpty()) { SegLabels[S]->SetText(FText::FromString(Cap)); }
		}
	}
	if (Found != kSegments)
	{
		UE_LOG(LogAsdaMall, Error,
			TEXT("[премиум-молл] переключатель 30002: найдено %d кнопок из %d. Узлы `PremiumMall_Menu_OPT0…3` "
			     "кладёт шаг Tools/asda_ui_premium_menu_tabs.py — прогони его (импортёр их не делает: "
			     "у всех четырёх планов скина PlaneAlwaysInvisible=1)."), Found, kSegments);
	}

	// ★ Мёртвая пара 30010 + 30011 (дефект К10): в `.JUI` оба `Visible="0"`, и код оригинала их
	//   не показывает. Держим погашенными явно — чтобы случайная правка ассета их не «оживила».
	for (const TCHAR* N : { TEXT("Premium_Menu_Txt_grp"), TEXT("Premium_Menu_Select"),
	                        TEXT("Premium_Menu_Select_Text"), TEXT("Premium_Menu_Select_5_0"),
	                        TEXT("Premium_Menu_Select_5_1") })
	{
		if (UWidget* W = AsdaUi::FindNodeByName(Widget, N, nullptr))
		{
			W->SetVisibility(ESlateVisibility::Collapsed);
			DeadSelector.Add(W);
		}
	}

	// Кнопки-сегменты → мостики.
	for (int32 S = 0; S < kSegments; ++S)
	{
		if (!SegButtons[S]) { continue; }
		UAsdaButtonRelay* R = NewObject<UAsdaButtonRelay>(this);
		R->Index = S;
		R->Callback = [this](int32 Idx) { SelectSegment(Idx); };
		SegButtons[S]->OnClicked.AddDynamic(R, &UAsdaButtonRelay::OnClicked);
		AsdaUi::AnchorRelay(Relays, R, SegButtons[S]);   // ★ 31.10.2026: якорь чистит мёртвые/перепривязанные (`asda.UI.RelayAnchorPrune`)
	}

	bReady = (Found > 0);
	RefreshTabState();
}

// Состояние «какая вкладка активна» — ЕДИНСТВЕННОЕ, что не уносится в ассет (закон владельца).
//   Цвета кадров скина (покой FFC8C8C8 / наведение FFFFFFFF / выбрано FF94FF28) кладёт в стиль
//   кнопок шаг `asda_ui_premium_menu_tabs.py`, поэтому берём перегрузку С КНОПКАМИ.
void UAsdaPremiumMallSubsystem::RefreshTabState()
{
	AsdaUi::ApplyTabCaptionState(SegLabels, SegButtons, Segment, TabIdleColors);
}

// ═══════════════════════════════ ГЕЙТЫ ═════════════════════════════════════════════════════════
bool UAsdaPremiumMallSubsystem::CanOpen() const
{
	// Гейт 3 «личная лавка» → нотис 107022 — ★ волна Л5 торговли (30.09.2026): стоит в `Toggle`/`OpenMall` (`RefuseOwnShop`), ДО
	//   тумблера закрытия, как у эталона (ui_core.c:30498).
	// Гейт 5 «в бою» → нотис 6946 + скрыть вкладку 3 системного меню.
	const UGameInstance* GI = GetGameInstance();
	const UWorld* W = GI ? GI->GetWorld() : nullptr;
	const AAsdaPlayerController* Pc = W ? Cast<AAsdaPlayerController>(W->GetFirstPlayerController()) : nullptr;
	if (Pc && Pc->bInCombatStance) { return false; }
	return true;
}

// ═══════════════════════════════ ТУМБЛЕР ═══════════════════════════════════════════════════════
// Порт `uiInventoryConfirmAction@0xC4AD60` (`ida.c:489938-490014`).
void UAsdaPremiumMallSubsystem::Toggle()
{
	// ★ Волна Л5 торговли: гейт «личная лавка» эталона стоит ДО ветки «молл открыт → закрыть» (ui_core.c:30498 → 30510), после
	//   молчаливого гейта 1: ★ ревизия Л4/Л5 (МЕЛК-3) — гейт 1 целиком (`MallGate1Silent`): где эталон молчит, 107022 нет.
	//   Тумблер при гейте 1 ведёт себя как прежде (закрытие открытого молла гейт 1 не трогал).
	if (!MallGate1Silent() && RefuseOwnShop()) { return; }
	ReconcileLatch(TEXT("тумблер"));   // ★ 25.10.2026 (ui_toggles): латч при закрытом адресате — снять (разбор у ключа)
	if (bOpen) { CloseMall(); }
	else       { OpenMall(0); }
}

// ★ 25.10.2026 (ui_toggles) [ОРИГИНАЛ]: факт «молл открыт» (разбор — у объявления и у ключа `asda.PremiumMall.ToggleFromFact`).
bool UAsdaPremiumMallSubsystem::IsOpen() const
{
	if (CVarToggleFromFact.GetValueOnGameThread() == 0) { return bOpen; }
	return bOpen && IsAddresseeShown();
}

bool UAsdaPremiumMallSubsystem::IsAddresseeShown() const
{
	const UGameInstance* GI = GetGameInstance();
	const UAsdaWindowSubsystem* WM = GI ? GI->GetSubsystem<UAsdaWindowSubsystem>() : nullptr;
	if (!WM) { return false; }
	// С рамкой 30000 (`LegacyMenu 1`) молл открыт, пока видна сама рамка (эталонный `CControl_SetVisible(this+2)`).
	if (CVarLegacyMenu.GetValueOnGameThread() != 0) { return WM->IsWindowVisible(kWindowId); }
	switch (Segment)
	{
	case 2:   // парикмахерская 15000 — у неё сессия, а не одно окно (`IsSessionOpen` — общая точка её владельца)
		if (const UAsdaHairShopSubsystem* Hair = GI->GetSubsystem<UAsdaHairShopSubsystem>()) { return Hair->IsSessionOpen(); }
		return false;
	case 1:   // бустер-шоп: адресата у сегмента нет (по умолчанию ненажимаем) — наблюдать нечего, латч не трогаем
		return true;
	default:  // 0 «Элитный» и 3 «Набор» — окно 6000 `SHELLITEM_STORE_WIN` (вкладка 58300 живёт внутри него)
		return WM->IsWindowVisible(FName(TEXT("SHELLITEM_STORE_WIN")));
	}
}

void UAsdaPremiumMallSubsystem::ReconcileLatch(const TCHAR* Why)
{
	if (CVarToggleFromFact.GetValueOnGameThread() == 0 || !bOpen || IsAddresseeShown()) { return; }
	// Адресат закрыт мимо молла (ESC/крестик магазина, «закрыть всё», загрузка карты): эталонный `uiStorageWindow_Close`
	//   у оригинала снял бы латч в том же закрытии — снимаем здесь, ничего не закрывая повторно.
	bOpen = false;
	UE_LOG(LogAsdaMall, Log, TEXT("[премиум-молл] %s: адресат сегмента %d уже закрыт мимо молла — латч «открыт» снят (как uiStorageWindow_Close)"),
		Why, Segment);
}

bool UAsdaPremiumMallSubsystem::MallGate1Silent() const
{
	// [ОРИГИНАЛ] Гейт 1 `uiInventoryConfirmAction@0xC4AD60` (ui_core.c:30492): `!tileInteract_isControlVisible(g_pItemMainWnd) &&
	//   !isBoundControlVisible(g_pItemQtyMakeDlg) && !CObject_GetField254(g_pInventoryWnd) && !CObject_GetField5581(g_pWarehouseWnd)`,
	//   иначе МОЛЧА. Окна ремесла 76000/77500 — `IsAnyCraftWindowOpen`; поле 254 — окно бустера 10050 (`UAsdaBoosterPanelSubsystem`,
	//   та же точка, что у гейта хода клавишами); поле 5581 — свиток возврата склада (`UAsdaWarehouseSubsystem::IsRecallActive`).
	//   Следующее условие эталона `!SlotByIndex || !*(slot+68)` (запись карты `getSlotByIndex(g_pLocalPlayer, getField3036)`,
	//   байт +68) в данных порта не разобрано — реестр (ревизия Л4/Л5 МЕЛК-3).
	const UGameInstance* GI = GetGameInstance();
	if (UAsdaCraftSubsystem::IsAnyCraftWindowOpen(GI)) { return true; }
	if (const UAsdaBoosterPanelSubsystem* Bp = GI ? GI->GetSubsystem<UAsdaBoosterPanelSubsystem>() : nullptr) { if (Bp->IsWindowOpen()) { return true; } }
	if (const UAsdaWarehouseSubsystem* Wh = GI ? GI->GetSubsystem<UAsdaWarehouseSubsystem>() : nullptr) { if (Wh->IsRecallActive()) { return true; } }
	return false;
}

bool UAsdaPremiumMallSubsystem::RefuseOwnShop() const
{
	// [ОРИГИНАЛ] `uiInventoryConfirmAction@0xC4AD60` (ui_core.c:30498-30503; МИСНЕЙМ = открыть премиум-молл): после молчаливого гейта 1
	//   `if (sub_929DC4(g_bSomeFeatureEnabled))` → 107022 в чат (−11514, 10 с) и выход. Проверяется ТОЛЬКО витрина 23000 (у хозяина и у
	//   гостя), окно подготовки 22000 молл не блокирует — К-11 [ОРИГИНАЛ], оставлено 1:1.
	if (!UAsdaTradeStateSubsystem::IsShopDlgOpen(GetGameInstance())) { return false; }
	UAsdaTradeStateSubsystem::ChatStoreGate(GetGameInstance(), kLocOwnShop, TEXT("uiInventoryConfirmAction"));
	return true;
}

void UAsdaPremiumMallSubsystem::OpenMall(int32 InSegment)
{
	// Гейт 1 эталона (`uiInventoryConfirmAction@0xC4AD60`, ui_core.c:30492): `!tileInteract_isControlVisible(
	//   g_pItemMainWnd) && !isBoundControlVisible(g_pItemQtyMakeDlg) && !CObject_GetField254(g_pInventoryWnd) &&
	//   !CObject_GetField5581(g_pWarehouseWnd)` — МОЛЧА ничего.
	//   ★ Поправка волны В3 ремесла (ревизия В0–В2 С-1): «окно предмета / окно количества» — это окна РЕМЕСЛА 76000 и
	//   77500 (`g_pItemMainWnd` / `g_pItemQtyMakeDlg`), а не нумпад 1410, как читал прежний порт. Поля 254 (барабан
	//   гачи/бустера) и 5581 (склад) — ★ подключены ревизией Л4/Л5 (МЕЛК-3), см. `MallGate1Silent`.
	if (UAsdaCraftSubsystem::IsAnyCraftWindowOpen(GetGameInstance()))
	{
		UE_LOG(LogAsdaMall, Log, TEXT("[премиум-молл] открытие отклонено молча: открыто окно ремесла 76000/77500 (гейт 1, ui_core.c:30492)"));
		return;
	}
	// ★ Ревизия Л4/Л5 (МЕЛК-3): остальные условия гейта 1 — окно бустера (поле 254) и свиток возврата склада (поле 5581) — МОЛЧА.
	if (MallGate1Silent())
	{
		UE_LOG(LogAsdaMall, Log, TEXT("[премиум-молл] открытие отклонено молча: окно бустера или свиток возврата склада (гейт 1, ui_core.c:30492)"));
		return;
	}
	if (RefuseOwnShop()) { return; }   // ★ волна Л5: гейт 3 «личная лавка» (витрина 23000) → 107022

	if (!CanOpen())
	{
		// Нотис 6946 в чат + эталонное «скрыть вкладку 3 системного меню».
		NoticeChat(kLocInFight);
		UE_LOG(LogAsdaMall, Log, TEXT("[премиум-молл] открытие отклонено: бой (нотис %d)"), kLocInFight);
		return;
	}

	// ★★★ РЕШЕНИЕ ВЛАДЕЛЬЦА 2026-08-28: полосы-переключателя 30002 в игре нет (см. `CVarLegacyMenu`).
	//   Тогда рамку `Premium_Menu` 30000 НЕ СОЗДАЁМ ВООБЩЕ — иначе она осталась бы висеть пустым
	//   прямоугольником поверх магазина. Хоткей и кнопка 73407 в этом режиме ведут прямо в сегмент 0.
	const bool bLegacyMenu = (CVarLegacyMenu.GetValueOnGameThread() != 0);
	if (bLegacyMenu)
	{
		if (!EnsureWidget()) { return; }
	}

	// Предупреждение 97100 — ПЛАШКА, а не чат (эталон `CItem_BuildTooltipText`).
	//   ★ [ОРИГИНАЛ] 30.10.2026: только вне города — `(controlBar_SlotIsActive(клетка) & 0x80) == 0` (ui_core.c:30542).
	if (CVarOpenWarning.GetValueOnGameThread() != 0)
	{
		const UAsdaZoneSubsystem* Zone = GetGameInstance() ? GetGameInstance()->GetSubsystem<UAsdaZoneSubsystem>() : nullptr;
		const bool bPeace = Zone && Zone->IsLocalPlayerInPeaceZoneNow();
		if (bPeace && CVarWarnOutsideTownOnly.GetValueOnGameThread() != 0)
		{
			UE_LOG(LogAsdaMall, Log, TEXT("[премиум-молл] предупреждение %d не показано: игрок в городе (бит 0x80 клетки)"), kLocWarnMob);
		}
		else
		{
			NoticeTip(kLocWarnMob);
		}
	}

	if (bLegacyMenu)
	{
		if (UGameInstance* GI = GetGameInstance())
		{
			if (UAsdaWindowSubsystem* WM = GI->GetSubsystem<UAsdaWindowSubsystem>())
			{
				WM->SetWindowVisible(kWindowId, true);
				WM->BringWindowToFront(kWindowId);
			}
		}
	}
	// ★ Правило владельца «не защёлкивать состояние до того, как экран реально показан»:
	//   флаг ставим ПОСЛЕ показа рамки, иначе тумблер уйдёт в «открыто», а окна не будет.
	bOpen = true;
	++CntOpen;
	Segment = -1;                  // чтобы `SelectSegment` не счёл переход «в тот же сегмент»
	// ★ Без полосы живой адресат один — сегмент 0 (магазин 6000). Остальные входы у систем свои.
	SelectSegment(bLegacyMenu ? FMath::Clamp(InSegment, 0, kSegments - 1) : 0);
	UE_LOG(LogAsdaMall, Log, TEXT("[премиум-молл] открыт, сегмент %d"), Segment);
}

void UAsdaPremiumMallSubsystem::CloseMall()
{
	if (!bOpen) { return; }
	// Эталон закрывает адресата ТЕКУЩЕГО сегмента (`ida.c:489971-489987`).
	CloseSegment(Segment);
	if (CVarLegacyMenu.GetValueOnGameThread() != 0)
	{
		if (UGameInstance* GI = GetGameInstance())
		{
			if (UAsdaWindowSubsystem* WM = GI->GetSubsystem<UAsdaWindowSubsystem>()) { WM->SetWindowVisible(kWindowId, false); }
		}
	}
	bOpen = false;
	UE_LOG(LogAsdaMall, Log, TEXT("[премиум-молл] закрыт (сегмент %d)"), Segment);
}

// ═══════════════════════════════ СЕГМЕНТЫ ══════════════════════════════════════════════════════
// Порт `case 30002` (`ida.c:423296-423345`): каждый сегмент ЗАКРЫВАЕТ чужих адресатов и открывает
//   своего. Мы делаем то же, но без эталонного дефекта К5 (сегмент 3 у оригинала открывал
//   парикмахерскую и тут же её закрывал — лишняя перерисовка/мигание).
void UAsdaPremiumMallSubsystem::SelectSegment(int32 InSeg)
{
	const int32 Seg = FMath::Clamp(InSeg, 0, kSegments - 1);
	if (!bOpen) { OpenMall(Seg); return; }
	if (Seg == Segment) { return; }

	for (int32 S = 0; S < kSegments; ++S) { if (S != Seg) { CloseSegment(S); } }
	Segment = Seg;
	++CntSeg[Seg];
	RefreshTabState();
	OpenSegment(Seg);
}

void UAsdaPremiumMallSubsystem::OpenSegment(int32 Seg)
{
	UGameInstance* GI = GetGameInstance();
	UAsdaPremiumShopSubsystem* Shop = GI ? GI->GetSubsystem<UAsdaPremiumShopSubsystem>() : nullptr;
	switch (Seg)
	{
	case 0:   // «Элитный» → `uiInventoryOpenWindow(g_pShopWnd)` = окно 6000
		if (Shop) { Shop->OpenWindow(/*bPackageTab*/false); }
		return;
	case 3:   // «Набор» → `uiPackageStampWndOpenPage(…, 1)` = вкладка 58300 ВНУТРИ окна 6000
		// ★★★ В ОРИГИНАЛЕ СЕГМЕНТ 3 НЕНАЖИМАЕМ (индекс 3 лежит в контейнере +0x268 контрола 30002,
		//   `ida.c:420671`), и ядро порта маршрутизировало его ошибочно. Вместе с полосой уходит и
		//   эта маршрутизация: витрина наборов достижима вкладкой 58300 ИЗНУТРИ окна 6000.
		//   Ключ `asda.PremiumMall.LegacyMenu 1` возвращает и полосу, и прежнее поведение.
		if (CVarLegacyMenu.GetValueOnGameThread() == 0)
		{
			UE_LOG(LogAsdaMall, Log,
				TEXT("[премиум-молл] сегмент 3 («%s») не маршрутизируется: полоса 30002 убрана "
				     "(решение владельца 2026-08-28), а в оригинале пункт и так ненажимаем. "
				     "Витрина наборов — вкладка 58300 внутри окна 6000."), *Loc(kSegLoc[3]));
			return;
		}
		if (Shop) { Shop->OpenWindow(/*bPackageTab*/true); }
		return;
	case 1:   // «Бустер» → `uiStorageWindow_Open` = окно 15300
		// ★★★ В ОРИГИНАЛЕ ЭТОТ СЕГМЕНТ НЕНАЖИМАЕМ (доказательство — у ключа выше), поэтому по
		//   умолчанию мы честно НЕ открываем окно: живой вход у магазина бустеров другой —
		//   кнопка NPC. Ключ `asda.PremiumMall.EnableBoosterSegment 1` включает сегмент.
		if (CVarEnableBoosterSegment.GetValueOnGameThread() != 0)
		{
			if (UAsdaBoosterShopSubsystem* Booster = GI ? GI->GetSubsystem<UAsdaBoosterShopSubsystem>() : nullptr)
			{
				Booster->OpenWindow();
				return;
			}
		}
		if (CVarStubNotice.GetValueOnGameThread() != 0)
		{
			UE_LOG(LogAsdaMall, Warning,
				TEXT("[премиум-молл] сегмент 1 («%s») в оригинале ПОМЕЧЕН НЕНАЖИМАЕМЫМ (контейнер "
				     "+0x268 контрола 30002) — окно не открывается. Живой вход магазина бустеров — "
				     "кнопка NPC; включить сегмент: asda.PremiumMall.EnableBoosterSegment 1"),
				*Loc(kSegLoc[1]));
		}
		return;
	case 2:   // «Салон причесок» → парикмахерская 15000 (`uiHairShopWindowOnShow@0xCEEE90`)
		// ★ Это ЕДИНСТВЕННЫЙ живой вход парикмахерской в оригинале (кода NPC 30029 в данных нет —
		//   0 вхождений в `.nsr` семи клиентов). В нашей сборке полоса убрана, и наружу тот же
		//   вход выведен кнопкой NPC 132 «Парикмахер Юки»; здесь ветка остаётся для режима
		//   `asda.PremiumMall.LegacyMenu 1` — второго механизма не заводим.
		if (UAsdaHairShopSubsystem* Hair = GI ? GI->GetSubsystem<UAsdaHairShopSubsystem>() : nullptr)
		{
			if (!Hair->IsSessionOpen()) { Hair->OpenWindow(); }
			return;
		}
		return;
	default:
		if (CVarStubNotice.GetValueOnGameThread() != 0)
		{
			// ★ Текст берём из САМОЙ ЖЕ таблицы игры (подпись сегмента), ничего не выдумывая.
			UE_LOG(LogAsdaMall, Warning,
				TEXT("[премиум-молл] сегмент %d («%s») адресата не имеет"), Seg, *Loc(kSegLoc[Seg]));
		}
		return;
	}
}

void UAsdaPremiumMallSubsystem::CloseSegment(int32 Seg)
{
	UGameInstance* GI = GetGameInstance();
	UAsdaPremiumShopSubsystem* Shop = GI ? GI->GetSubsystem<UAsdaPremiumShopSubsystem>() : nullptr;
	switch (Seg)
	{
	case 0:
	case 3:
		// У сегментов 0 и 3 адресат ОДИН — окно 6000 (вкладка 58300 живёт внутри него).
		//   Поэтому закрываем его только тогда, когда уходим с обоих (см. `SelectSegment`:
		//   переход 0↔3 закроет и снова откроет то же окно — 1:1 эталон, который на каждом
		//   переключении зовёт свою пару «закрыть/открыть»).
		if (Shop && Shop->IsOpen()) { Shop->CloseWindow(); }
		return;
	case 2:
		if (UAsdaHairShopSubsystem* Hair = GI ? GI->GetSubsystem<UAsdaHairShopSubsystem>() : nullptr)
		{
			if (Hair->IsSessionOpen()) { Hair->CloseWindow(); }
		}
		return;
	default:
		return;   // бустер-шоп: у него свой вход и своё закрытие
	}
}

// ═══════════════════════════════ ВХОДЫ ═════════════════════════════════════════════════════════
void UAsdaPremiumMallSubsystem::OnGameCommand(int32 Cmd)
{
	if (Cmd != kCmdHotkey) { return; }
	Toggle();
}

void UAsdaPremiumMallSubsystem::OnMenuCommand(int32 ButtonIdNum, int32 /*TargetWindowId*/, bool /*bOpenReq*/)
{
	if (ButtonIdNum != kMenuBtn) { return; }
	// ★ Эталон у кнопки 73407 ЗАКРЫТИЯ НЕ ИМЕЕТ (`AsdaSystemMenuSubsystem.h:30`), а хоткей —
	//   тумблер. Кнопка идёт по тому же пути, что и хоткей: так игрок может закрыть молл тем же,
	//   чем открыл (иначе кнопка «залипала» бы).
	Toggle();
}

void UAsdaPremiumMallSubsystem::BindPlayerController()
{
	UGameInstance* GI = GetGameInstance();
	UWorld* W = GI ? GI->GetWorld() : nullptr;
	AAsdaPlayerController* Pc = W ? Cast<AAsdaPlayerController>(W->GetFirstPlayerController()) : nullptr;
	if (!Pc) { return; }
	if (CVarRebindHotkey.GetValueOnGameThread() != 0)
	{
		if (Pc == BoundPc.Get()) { return; }   // уже подписаны на ЭТОТ контроллер
		if (AAsdaPlayerController* Old = BoundPc.Get()) { Old->OnGameCommand.RemoveDynamic(this, &UAsdaPremiumMallSubsystem::OnGameCommand); }
	}
	else if (bPcBound) { return; }            // прежнее: флаг от первого контроллера
	Pc->OnGameCommand.AddUniqueDynamic(this, &UAsdaPremiumMallSubsystem::OnGameCommand);
	BoundPc = Pc;
	bPcBound = true;
	UE_LOG(LogAsdaMall, Log, TEXT("[премиум-молл] подписка на хоткей cmd%d выполнена"), kCmdHotkey);
}

// ═══════════════════════════════ ДИАГНОСТИКА ═══════════════════════════════════════════════════
FString UAsdaPremiumMallSubsystem::DumpControls() const
{
	FString S = TEXT("=== Premium_Menu 30000 ===\n");
	for (int32 i = 0; i < kSegments; ++i)
	{
		S += FString::Printf(TEXT("  сегм.%d loc%-6d кнопка %-4s подпись %-4s «%s»\n"),
			i, kSegLoc[i],
			SegButtons.IsValidIndex(i) && SegButtons[i] ? TEXT("есть") : TEXT("НЕТ"),
			SegLabels.IsValidIndex(i) && SegLabels[i] ? TEXT("есть") : TEXT("НЕТ"),
			SegLabels.IsValidIndex(i) && SegLabels[i] ? *SegLabels[i]->GetText().ToString() : TEXT(""));
	}
	S += FString::Printf(TEXT("  мёртвый селектор 30010/30011: %d узлов погашено\n"), DeadSelector.Num());
	return S;
}

FString UAsdaPremiumMallSubsystem::DumpState() const
{
	return FString::Printf(
		TEXT("30000: открыт=%s сегмент=%d | открытий=%d, по сегментам %d/%d/%d/%d | хоткей привязан=%s"),
		bOpen ? TEXT("да") : TEXT("нет"), Segment, CntOpen,
		CntSeg[0], CntSeg[1], CntSeg[2], CntSeg[3], bPcBound ? TEXT("да") : TEXT("нет"));
}
