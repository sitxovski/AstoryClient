// Copyright (c) 2026 @sitxovski (Discord)
// Реализация магазина снаряжения (окно SHELLITEM_STORE2_WIN 6800).
//   Весь реверс с адресами — в шапке UI/AsdaEquipShopSubsystem.h и в отчёте
//   `Reports/equip_shop_port.md`.
#include "UI/AsdaEquipShopSubsystem.h"
#include "AsdaPerf.h"   // ★ R4-b (14.11.2026): области замера времени игрового потока
#include "UI/AsdaEnterGuard.h"   // ★ 25.10.2026: вопросы этого файла Enter не подтверждает (только щелчок)
#include "AsdaDevConsole.h"   // ★ античит А2: ASDA_DEV_CVAR / ASDA_DEV_CONSOLE — дев-ключи и команды вне Shipping
#include "AsdaSelf.h"   // ★ В2-A дедупа №2: общая точка «это я?» (класс/уровень/charId/userNum)
#include "UI/AsdaJuiWindowHandle.h"   // ★ волна 3 (15.09.2026): каркас окон JUI
#include "UI/AsdaNotice.h"   // ★ волна 1: фасад нотисов (Loc / Chat / MsgBox)

#include "UI/AsdaWindowSubsystem.h"
#include "UI/AsdaTradeStateSubsystem.h"   // ★ 29.09.2026: гейт 1 открытия — окно обмена 2008 открыто (`CExchange+0x2C`, нотис 6947)
#include "UI/AsdaMessageSubsystem.h"
#include "UI/AsdaInventorySubsystem.h"
#include "UI/AsdaIconCellGrid.h"
#include "UI/AsdaItemTooltipSubsystem.h"   // ОБЩАЯ ТОЧКА плашки-подсказки (закон `port-shared-points`)
#include "UI/AsdaUiHelpers.h"
#include "UI/AsdaCharaInfoSubsystem.h"     // ★ волна В4 ремесла: мини-снимок окна персонажа
#include "Npc/AsdaNpcDialogSubsystem.h"    // ★ волна В4 ремесла: выход из разговора при закрытии магазина (5045)
#include "Resources/AsdaSoundSubsystem.h"  // ★ волна В4 ремесла: звуки 5/6 открытия/закрытия магазина
#include "Items/AsdaItemTableSubsystem.h"
#include "UI/AsdaNpcShopSubsystem.h"   // ★ 06.11.2026: общий звук сделки у NPC
#include "Text/AsdaTextSubsystem.h"
#include "World/AsdaWorldSubsystem.h"
#include "AsdaNetSubsystem.h"

#include "Blueprint/UserWidget.h"
#include "Blueprint/WidgetTree.h"
#include "Components/Image.h"
#include "Components/Button.h"
#include "Components/CheckBox.h"
#include "Components/TextBlock.h"
#include "Components/PanelWidget.h"
#include "Components/CanvasPanelSlot.h"
#include "Engine/GameInstance.h"
#include "Login/AsdaButtonRelay.h"
#include "AsdaDataTableCache.h"
#include "UI/AsdaSystemOptionSubsystem.h"      // ★ 25.10.2026 (ревизия sit108, М-3): гейт «персонаж занят» (`objStateAllowsRender`) до стопа сцены
#include "World/AsdaPlayerCharacter.h"          // ★ 24.10.2026 (выход из сидения 1:1): `ApplyEtalonSceneStop` — стоп/«встать» при входе в сцену
#include "GameFramework/PlayerController.h"    // ★ 24.10.2026: свой павн для `ApplyEtalonSceneStop`

DEFINE_LOG_CATEGORY_STATIC(LogAsdaEquipShop, Log, All);

// ★ 06.11.2026 [ОРИГИНАЛ] звук 4 кнопок окна 6800 (`uiHandleCommand@0xC23AC0`).
static ASDA_DEV_CVAR(int32, CVarEquipShopButtonSound,
	TEXT("asda.EquipShop.ButtonSound"), 1,
	TEXT("Звук 4 кнопок магазина снаряжения 6800, как в оригинале: 1 — играть, 0 — молча (прежнее поведение порта)."),
	ECVF_Default);

// ── Константы окна ──
static const FName  kWindowId(TEXT("SHELLITEM_STORE2_WIN"));
static const TCHAR* kWbpPath =
	TEXT("/Game/AsdaAssets/UI/Player_UI/Trade/WBP_SHELLITEM_STORE2_WIN.WBP_SHELLITEM_STORE2_WIN_C");
// ★★★ ВИТРИНА — DataTable, А НЕ СЫРОЙ `.she` ИЗ Content.
//   Было до 2026-08-26: `FFileHelper::LoadFileToArray` файла `Content/AsdaAssets/Data/NPC/Freeshop.she`
//   (копия оригинального `./Data/NPC/Shop/FreeShop.she`). Сырым файлам в Content не место —
//   исходник перенесён в `RawAssets/Data/NPC/Shop/`, данные разобраны импортёром
//   `Tools/asda_import_equip_shop.py` в 11 строк этой таблицы.
static const TCHAR* kListTablePath = TEXT("/Game/AsdaAssets/Data/NPC/DT_EquipShopList.DT_EquipShopList");

// ── Loc-идентификаторы. ВСЕ сняты с эталонных вызовов `UI_FormatLocalizedNotice`. ──
namespace EquipShopLoc
{
	// Подписи (`CShellItemStore_InitControls@0xC28820`, `tradeWinResetUI@0xC36B90`).
	constexpr int32 kWearListHdr  = 12906;   // 6810 «Список снаряжения»
	constexpr int32 kSumHdr       = 12958;   // 6811 «Итого»
	constexpr int32 kChkGenderTxt = 12015;   // 6807 «Искать для своего пола»
	constexpr int32 kChkWearTxt   = 12016;   // 6802 «Искать предметы одежды»
	constexpr int32 kBuyWornCap   = 6957;    // 6851 «Надеть куплен. вещи»
	constexpr int32 kRemoveCap    = 6945;    // 6848 «Снять выбранный предмет»
	constexpr int32 kResetCap     = 6950;    // 6849 «Сброс»
	constexpr int32 kCardBuyCap   = 6944;    // 6859+6k «OK»
	constexpr int32 kPriceLabel   = 111914;  // 79501+10k «Цена»
	constexpr int32 kNetPriceLabel= 2286;    // 79505+10k «Цена» (ставится один раз в init)
	constexpr int32 kWarDiscount  = 111915;  // 79503+10k «Скидка (%d%%)»
	constexpr int32 kGoldLbl      = 13241;   // 7001 «Золото:» / «My Gold» (★ 10.10.2026, блок Д5; см. SetupControls)
	constexpr int32 kWarSurcharge = 111916;  // 79503+10k «Наценка (%d%%)»
	// Девять пунктов меню 6856.
	constexpr int32 kTabWarrior   = 6915;    // «Воин»
	constexpr int32 kTabArcher    = 6916;    // «Лучник»
	constexpr int32 kTabMage      = 6917;    // «Маг»
	constexpr int32 kTabArmor     = 6443;    // «Доспех»
	constexpr int32 kTabGloves    = 6844;    // «Перчатки»
	constexpr int32 kTabPants     = 6845;    // «Штаны»
	constexpr int32 kTabShield    = 6846;    // «Щит»
	constexpr int32 kTabHelmet    = 6847;    // «Шлем»
	constexpr int32 kTabShoes     = 6848;    // «Ботинки»
	// Нотисы.
	constexpr int32 kOpenHint     = 6815;    // чат: «Доспехи и оружие можно купить после примерки.»
	constexpr int32 kBusyTrade    = 6947;    // чат: идёт обмен
	constexpr int32 kBusyState    = 6946;    // чат: состояние персонажа не позволяет
	constexpr int32 kAllRemoved   = 6890;    // чат: «Все вещи сняты»
	constexpr int32 kConfirmOne   = 20060;   // модалка: «[ss1] - купить?»
	constexpr int32 kConfirmWorn  = 6889;    // модалка: «Купить надетый предмет?»
	constexpr int32 kBoughtLine   = 20065;   // чат: «Ты покупаешь [ss1] за [ss2] золота.»
	// Коды ответа 5021.
	constexpr int32 kResNoItem    = 9570;    // 0 — эталон ЭТОГО окна молчит (см. CVar ниже)
	constexpr int32 kResBagFull   = 2233;    // 2
	constexpr int32 kResWeight    = 1012;    // 3
	constexpr int32 kResNoGold    = 2048;    // 4
	constexpr int32 kResNotSold   = 9569;    // 5 — эталон ЭТОГО окна молчит (см. CVar ниже)
}

// Меню 6856: индекс пункта → номер списка витрины (`shopBuildSlotItemList@0xC382F0`, switch 0…8).
static const int32 kTabToList[UAsdaEquipShopSubsystem::kMenuTabs] = { 11, 12, 13, 5, 6, 7, 8, 9, 10 };
static const int32 kTabLoc[UAsdaEquipShopSubsystem::kMenuTabs] =
{
	EquipShopLoc::kTabWarrior, EquipShopLoc::kTabArcher, EquipShopLoc::kTabMage,
	EquipShopLoc::kTabArmor,   EquipShopLoc::kTabGloves, EquipShopLoc::kTabPants,
	EquipShopLoc::kTabShield,  EquipShopLoc::kTabHelmet, EquipShopLoc::kTabShoes
};

// ★ Индексы контролов в `.JUI` (пре-ордер), они же суффиксы имён виджетов, которые расставил
//   импортёр. Сняты РАЗБОРОМ `Data/Decoded/jui/gamemain_jui.ini`, сверены с именами внутри
//   `WBP_SHELLITEM_STORE2_WIN.uasset` (напр. `SHELLITEM2_POP00_16`, `SHELLITEM2_POP20_457`).
//   На карточку: бокс, подложка имени, кнопка, цена, рамка выбора, иконка.
struct FEquipShopCardIdx { int32 Box, Name, Buy, Price, Sel, Icon; };
static const FEquipShopCardIdx kCardIdx[UAsdaEquipShopSubsystem::kCards] =
{
	{ 16, 17, 18, 20, 22, 23},   {  38,  39,  40,  42,  44,  45},   {  60,  61,  62,  64,  66,  67},
	{ 82, 83, 84, 86, 88, 89},   { 104, 105, 106, 108, 110, 111},   { 126, 127, 128, 130, 132, 133},
	{148,149,150,152,154,155},   { 170, 171, 172, 174, 176, 177},   { 193, 194, 195, 197, 199, 200},
	{215,216,217,219,221,222},   { 237, 238, 239, 241, 243, 244},   { 259, 260, 261, 263, 265, 266},
	{281,282,283,285,287,288},   { 303, 304, 305, 307, 309, 310},   { 325, 326, 327, 329, 331, 332},
	{347,348,349,351,353,354},   { 369, 370, 371, 373, 375, 376},   { 391, 392, 393, 395, 397, 398},
	{413,414,415,417,419,420},   { 435, 436, 437, 439, 441, 442},   { 457, 458, 459, 461, 463, 464}
};
// Блок цены 79500+10k…79506+10k — семь HIT-зон на карточку (у контролов ПУСТОЙ `ID` в `.JUI`).
static const int32 kPriceIdx[UAsdaEquipShopSubsystem::kCards][7] =
{
	{ 28, 29, 30, 32, 33, 35, 36}, { 50, 51, 52, 54, 55, 57, 58}, { 72, 73, 74, 76, 77, 79, 80},
	{ 94, 95, 96, 98, 99,101,102}, {116,117,118,120,121,123,124}, {138,139,140,142,143,145,146},
	{160,161,162,164,165,167,168}, {183,184,185,187,188,190,191}, {205,206,207,209,210,212,213},
	{227,228,229,231,232,234,235}, {249,250,251,253,254,256,257}, {271,272,273,275,276,278,279},
	{293,294,295,297,298,300,301}, {315,316,317,319,320,322,323}, {337,338,339,341,342,344,345},
	{359,360,361,363,364,366,367}, {381,382,383,385,386,388,389}, {403,404,405,407,408,410,411},
	{425,426,427,429,430,432,433}, {447,448,449,451,452,454,455}, {469,470,471,473,474,476,477}
};
// ★ Контейнер витрины 6983 `Shellitem_Store2_Win_Part2` — 15-й контрол в пре-ордере `.JUI`,
//   поэтому его девять планов рамки (скин `Win_Tab`, ResizeWindow 3×3) импортёр назвал
//   `Shellitem_Store2_Win_Part2_15_0` … `_15_8`.
static constexpr int32 kPanelJuiIdx = 15;

// Строки «Списка снаряжения» 6812+5j: {фон-кнопка, имя, цена, галка-вкл, галка-выкл}.
struct FEquipShopWearIdx { int32 Row, Name, Price, CheckOn, CheckOff, Gold; };
static const FEquipShopWearIdx kWearIdx[UAsdaEquipShopSubsystem::kWearRows] =
{
	{483,484,485,487,488,486}, {489,490,491,493,494,492}, {495,496,497,499,500,498},
	{501,502,503,505,506,504}, {507,508,509,511,512,510}, {513,514,515,517,518,516},
	{519,520,521,523,524,522}
};

// ★ ДЕФЕКТ ОРИГИНАЛА Д1. `Recv_ShopSell_Result_0@0xC3D430` (обработчик 5021 ИМЕННО этого окна)
//   разбирает только коды 1/2/3/4, а на 0 («предмет не найден») и 5 («в магазине такое не купишь»)
//   уходит в `default: return` — игрок не видит НИЧЕГО. В магазине NPC (окно 1300) те же коды
//   показываются нотисами 9570 и 9569 (`Recv_ShopSell_Result@0xE28FD0`). Показываем и здесь.
//   Откат: `asda.EquipShop.LegacySilentCodes 1` — молчать, как эталон.
static ASDA_DEV_CVAR(int32, CVarEquipShopSilentCodes,
	TEXT("asda.EquipShop.LegacySilentCodes"), 0,
	TEXT("Коды отказа 5021 0 и 5 в магазине снаряжения: 0 — показывать нотисы 9570/9569 (исправление Д1), ")
	TEXT("1 — молчать, как эталон."),
	ECVF_Default);

// ★ ДЕФЕКТ ОРИГИНАЛА Д2. В `uiItemListSlot_Populate@0xC3A200` на НЕвоенной карте (слот города −1)
//   поля 79503/79504 получают «-», а поле 79506 («Цена») НЕ обновляется вовсе — в нём остаётся
//   текст от ПРЕДЫДУЩЕЙ карточки/страницы. Мы пишем туда базовую цену (налога нет).
//   Откат: `asda.EquipShop.LegacyBlankNetPrice 1` — оставлять поле пустым, как эталон.
static ASDA_DEV_CVAR(int32, CVarEquipShopBlankNetPrice,
	TEXT("asda.EquipShop.LegacyBlankNetPrice"), 0,
	TEXT("Поле «Цена» карточки вне военной карты: 0 — базовая цена (исправление Д2), 1 — пусто."),
	ECVF_Default);

// ★ ДЕФЕКТ ОРИГИНАЛА Д3. Кнопка 6848 идёт через `uiSevenSlotPanelRefresh@0xC3B930`, и та печатает
//   цену строки БЕЗ налога войны, тогда как основная перерисовка `shopRefreshSellSlotList@0xC3ACB0`
//   печатает С налогом. После нажатия 6848 цены в списке «прыгали». Считаем всегда с налогом.
//   Откат: `asda.EquipShop.LegacyCompactPrice 1`.
static ASDA_DEV_CVAR(int32, CVarEquipShopCompactPrice,
	TEXT("asda.EquipShop.LegacyCompactPrice"), 0,
	TEXT("Цена строки после кнопки «Снять выбранный предмет»: 0 — с налогом войны (исправление Д3), ")
	TEXT("1 — без налога, как эталон."),
	ECVF_Default);

// ★ ПРИМЕРКА товара на своём персонаже. Эталон делает это ВСЕГДА (`charEquipApplyPartsModelByType`
//   @0xC280B0), ключ нужен на случай, если игроку мешает подмена внешности в мире.
static ASDA_DEV_CVAR(int32, CVarEquipShopPreview,
	TEXT("asda.EquipShop.Preview"), 1,
	TEXT("Примерка товара магазина снаряжения на своём персонаже: 1 — как в оригинале, 0 — выключить."),
	ECVF_Default);

// ★★★ НАША ДОБАВКА (требование владельца 2026-08-26 №2): ПОДСКАЗКА ПРЕДМЕТА ПО НАВЕДЕНИЮ.
//   ⚠ В ОРИГИНАЛЕ ЭТОГО НЕТ и не было: в `.JUI` у ВСЕХ контролов окна 6800 поле `ToopTip = ""`
//   (проверено разбором `Data/Decoded/jui/gamemain_jui.ini`, 567 узлов поддерева), а обработчика
//   сообщений 8 / 9 / 0xA (у соседей это `itemTooltip_setStateFromRecord@0xE285B0` →
//   `Tooltip_LayoutNearCursor` → `CControl_KillFocus(&g_ItemTooltip)`) у `CShellItemStore`
//   в эталоне нет вовсе. Подключено ОБЩЕЙ ТОЧКОЙ `UAsdaItemTooltipSubsystem` со СВОИМ
//   владельцем `EAsdaTipOwner::EquipShop` (закон `ui-shared-tip-ownership`: гасит только владелец).
//   Откат: `asda.EquipShop.Tooltips 0`.
static ASDA_DEV_CVAR(int32, CVarEquipShopTooltips,
	TEXT("asda.EquipShop.Tooltips"), 1,
	TEXT("Подсказка предмета по наведению в магазине снаряжения: 1 — показывать (НАША ДОБАВКА), ")
	TEXT("0 — как в оригинале (подсказок нет)."),
	ECVF_Default);

// ★ ДЕФЕКТ ОРИГИНАЛА Д5 — ЗОЛОТО ИГРОКА НЕ ВИДНО.
//   `CShellItemStore_InitControls@0xC28820` кладёт в контрол 7001 подпись loc 13241 («Золото:»),
//   а `shopWindowReset@0xC37540` (shop_trade.c:3812-3816) пишет в 6850 сумму денег игрока и даже
//   зовёт `CControl_SetVisible(6850, 1)`. Но 6850 — РЕБЁНОК 7001, а у 7001 в `.JUI`
//   `Visible = "0"`, и рендер оригинала в скрытого родителя не заходит вовсе
//   (`CControl_RenderRecursive`, ui_core.c:108944: `if (CControl_IsVisible(child)) …`).
//   ⇒ в эталоне весь блок «Золото: N» в этом окне НЕВИДИМ, хотя код его исправно заполняет.
//   Показываем (закон `fix-original-defects`). Откат: `asda.EquipShop.LegacyHiddenGold 1`.
static ASDA_DEV_CVAR(int32, CVarEquipShopHiddenGold,
	TEXT("asda.EquipShop.LegacyHiddenGold"), 0,
	TEXT("Блок «Золото игрока» (7001/6850) в магазине снаряжения: 0 — показывать (исправление Д5), ")
	TEXT("1 — прятать, как эталон (родитель 7001 скрыт в .JUI)."),
	ECVF_Default);

// ★★★ БЫЛО ОТСТУПЛЕНИЕ О2, СТАЛО 1:1 (замечание владельца 2026-08-26 №3 «чёрная подложка»).
//   `shopWin_Open@0xC37110` (shop_trade.c:3864-3872) подгоняет ВЫСОТУ контейнера 6983 под число
//   видимых рядов: `h = 98 * (uiList_GetVisibleRows(ширинаЭкрана) / 3) + 48`. Порт этого не делал
//   («рамка авторская»), и на 1024×768 разницы не было (9/3 · 98 + 48 = 440 = размер из `.JUI`).
//   НО высота САМОГО окна берётся из `.JUI` как `AlignYSize = "32 0 99"` = 99 % высоты экрана,
//   а нижняя панель «Списка снаряжения» (57121) прибита к НИЗУ окна (`Origin.y = -208`).
//   ⇒ на любом разрешении выше 768 окно растёт, панель списка уезжает вниз, а рамка витрины
//   остаётся 440 px — и между ними открывается широкая полоса ГОЛОГО ТЕЛА ОКНА. Тело окна — это
//   план 4 скина `Win_Mg` (центральный тайл 9-slice), и он в атласе `n_01` = сплошной
//   НЕПРОЗРАЧНЫЙ ЧЁРНЫЙ (пиксели 480…483 × 439…442 = RGBA 0,0,0,255) — та самая «чёрная подложка
//   вокруг списка». Лечение = сделать ровно то, что делает эталон.
//   Откат: `asda.EquipShop.LegacyPanelHeight 1` — оставить рамку авторской (440 из `.JUI`).
static ASDA_DEV_CVAR(int32, CVarEquipShopLegacyPanelHeight,
	TEXT("asda.EquipShop.LegacyPanelHeight"), 0,
	TEXT("Высота рамки витрины 6983: 0 — как эталон, 98*(rows/3)+48 (убирает чёрную полосу), ")
	TEXT("1 — оставить размер из .JUI (440)."),
	ECVF_Default);

// ═════════════════════════════ ЖИЗНЕННЫЙ ЦИКЛ ═════════════════════════════════════════════════

void UAsdaEquipShopSubsystem::Initialize(FSubsystemCollectionBase& Collection)
{
	Super::Initialize(Collection);
	// ★★★ РОСПИСЬ «КАК ЗАКРЫТЬ СЕБЯ ПО ESC» — порт события 21 (`CControl_OnEvent21@0x12DCC30`).
	//   Окно `SHELLITEM_STORE2_WIN` (IDNum 6800) стоит в эталонной таблице `kEscCloseWindows`: признак
	//   `CControl+0x228` ему ставит `CShellItemStore_InitControls@0xC28820`. Проход
	//   `UAsdaWindowSubsystem::CloseTopmostOnEsc` закрывает верхнее окно ИМЕННО ЭТИМ вызовом,
	//   а не снятием виджета: снятие оставило бы подсистеме состояние «открыто», и окно
	//   больше не открылось бы. Закрыватель идемпотентен — повторный вызов ничего не делает.
	Collection.InitializeDependency(UAsdaWindowSubsystem::StaticClass());
	if (UAsdaWindowSubsystem* EscWM = GetGameInstance()
		? GetGameInstance()->GetSubsystem<UAsdaWindowSubsystem>() : nullptr)
	{
		EscWM->RegisterEscCloser(kWindowId, FSimpleDelegate::CreateUObject(this, &UAsdaEquipShopSubsystem::CloseWindow));
	}
	Cards.SetNum(kCards);
	WearUi.SetNum(kWearRows);
	WearRows.SetNum(kWearRows);
	RealEquip.Init(-1, kEquipSlots);
	PreviewEquip.Init(-1, kEquipSlots);
	PreviewApplied.Init(-1, kEquipSlots);
	Sets.SetNum(3 * kLists);

	if (UAsdaNetSubsystem* Net = GetGameInstance() ? GetGameInstance()->GetSubsystem<UAsdaNetSubsystem>() : nullptr)
	{
		// Закон `no-reentrant-net-callbacks`: колбэк только КОПИТ, разбор — в Tick.
		HBuyResult = Net->OnShopBuyResult.AddLambda([this](const TArray<uint8>& B)
		{
			// ★ 1:1 диспетчер эталона (network.c, `case 0x139D`): пока ОТКРЫТО окно 6800, ответ 5021
			//   разбирает ЕГО обработчик (`Recv_ShopSell_Result_0`), а не обработчик магазина NPC.
			if (bOpen) { PendingBuy.Push(B); }
		});
		HJobChanged = Net->OnSelfJobChanged.AddLambda([this](int32, int32, int32, int32) { bClassRebuildPending = true; });   // ★ 18.10.2026
	}
	UE_LOG(LogAsdaEquipShop, Log, TEXT("[магазин снаряжения] подсистема поднята"));
}

void UAsdaEquipShopSubsystem::Deinitialize()
{
	if (UAsdaNetSubsystem* Net = GetGameInstance() ? GetGameInstance()->GetSubsystem<UAsdaNetSubsystem>() : nullptr)
	{
		Net->OnShopBuyResult.Remove(HBuyResult);
		Net->OnSelfJobChanged.Remove(HJobChanged);   // ★ 18.10.2026
	}
	ResetWidget();
	Super::Deinitialize();
}

// ★★★ К-3: СБРОС СОСТОЯНИЯ СЕССИИ (шапка `AsdaSessionReset.h`). Подсистема живёт на
//   `GameInstance` и переживает разрыв связи, выход в выбор персонажа и травел; всё, что
//   ниже, — данные и защёлки ОДНОЙ сессии. Отправок здесь нет (закон `no-reentrant-net-callbacks`).
void UAsdaEquipShopSubsystem::ResetSessionState(EAsdaSessionResetReason Reason)
{
	ResetWidget();
	bOpen = false;
	bPending = false;                 // ждали 5021 — ответа уже не будет
	MenuTab = 0; MenuTabPrev = -2;
	Page = 0; MaxPage = 0; SelCard = -1;
	PendingItemCode = 0;
	BuiltForPixelWidth = 0;
	TipCard = -1;
	RealEquip.Reset(); PreviewEquip.Reset(); PreviewApplied.Reset();
	PendingBuyCodes.Reset(); PendingBuy.Clear();
	RawLists.Reset();                 // витрина приходит от сервера пакетом 5021
	// ★ ИСПРАВЛЕНИЕ 11.10.2026 (дедуп №2, В3 S1 — найдено пакетным прогоном самотестов): вместе с витриной снимается и признак
	//   «витрина прочитана». Прежде признак оставался взведённым при пустой витрине — `LoadShe` после ЛЮБОГО сброса сессии
	//   отвечал «не прочитана» навсегда, и окно 6800 больше не открывалось до перезапуска (самотест «снимок» — 3 провала).
	//   Теперь следующее открытие читает витрину заново, как в начале сессии.
	bSheLoaded = false;
	Mouse.Reset();
}

void UAsdaEquipShopSubsystem::Tick(float /*DeltaTime*/)
{
	ASDA_PERF_SCOPE("Tick.EquipShop");   // ★ R4-b (14.11.2026): область замера (AsdaPerf.h; в Shipping пусто)
	DrainPendingPackets();
	// ★ 18.10.2026 — смена профессии 5042: эталон пересобирает наборы витрины (фильтр «одежда» 6803 — по коду профессии
	//   `CCharacter+0x438`). Закрытое окно соберёт их при открытии само (`OpenWindow` → `BuildAllSets`).
	//   [ПОРТ] у открытого окна ещё и перерисовываем страницу: эталон менял только векторы, и витрина до перелистывания
	//   показывала товары прежнего фильтра.
	if (bClassRebuildPending)
	{
		bClassRebuildPending = false;
		if (bOpen && bControlsReady)
		{
			BuildAllSets();
			BuildPage();
			UE_LOG(LogAsdaEquipShop, Log, TEXT("[магазин снаряжения] смена профессии: наборы витрины пересобраны, страница перерисована"));
		}
	}
	if (!bOpen || !bControlsReady) { return; }
	Mouse.Update();
	// ★★★ МОДАЛЬНЫЙ ГЕЙТ (П8-9, волна 9): под открытым модальным нотисом эталон не отдаёт мышь
	//   НИКОМУ ниже (`ui_core.c:104149-104153`). Обоснование, порядок и ключ отката
	//   `asda.UI.ModalGatePolling` — в шапке `AsdaUi::IsModalDispatchBlocked` (`AsdaUiHelpers.h`).
	//   ⚠ Защёлки сбрасываем: иначе нажатие ДО нотиса и отпускание ПОСЛЕ его закрытия сошлись бы
	//   в клик, которого игрок не делал.
	if (AsdaUi::IsModalDispatchBlocked(GetGameInstance()))
	{
		TickTooltip(-1, -1);    // уже висящую подсказку карточки/строки ГАСИМ, а не просто не показываем
		Mouse.Reset();
		CardDbl.Reset();        // [ПОРТ] ревизия 10.10 МЕЛК-1: модальный нотис между щелчками рвёт пару (как щелчок по другому окну)
		return;
	}
	TickCards();
}

// ═════════════════════════════ ДАННЫЕ ═════════════════════════════════════════════════════════

// `uiList_GetVisibleRows2@0xCAB100` — сколько ПРЕДМЕТОВ помещается на страницу витрины.
//   Таблица эталона перечисляет РОВНО те режимы, что были в лаунчере 2008 года.
//   ★ ОТСТУПЛЕНИЕ (осознанное): на разрешении, которого в таблице НЕТ, эталон возвращает 6 —
//   и витрина из 21 карточки показывала бы 6 товаров. В порту окно живёт на любом разрешении,
//   поэтому неизвестную ширину мы кладём на ту же лесенку по ближайшему меньшему режиму.
//   Перечисленные в таблице режимы отдаются 1:1. Откат — `asda.EquipShop.LegacyPageSize 1`.
// ★ Чёрная обводка текста в окне 6800 (свойство скина оригинала). 0 = снять (по умолчанию,
//   требование владельца: с ней чёрные названия товаров нечитаемы), 1 = оставить как в оригинале.
static ASDA_DEV_CVAR(int32, CVarEquipShopTextOutline,
	TEXT("asda.EquipShop.TextOutline"), 0,
	TEXT("Окно 6800: 0 — снять чёрную обводку текста (читаемость), 1 — оставить как в оригинале."),
	ECVF_Default);

static ASDA_DEV_CVAR(int32, CVarEquipShopLegacyPageSize,
	TEXT("asda.EquipShop.LegacyPageSize"), 0,
	TEXT("Размер страницы витрины на НЕперечисленном разрешении: 0 — продолжить лесенку эталона, ")
	TEXT("1 — 6 товаров, как возвращает `uiList_GetVisibleRows2` в ветке default."),
	ECVF_Default);

int32 UAsdaEquipShopSubsystem::ItemsPerPage() const
{
	FVector2D V(1024.f, 768.f);
	if (const UWorld* W = GetGameInstance() ? GetGameInstance()->GetWorld() : nullptr)
	{
		const FVector2D S = AsdaUi::ViewportSizeUnits(W);
		if (S.X > 1.f && S.Y > 1.f) { V = S; }
	}
	const int32 Wd = FMath::RoundToInt(V.X);
	const int32 Ht = FMath::RoundToInt(V.Y);
	switch (Wd)   // ветки 1:1 по эталону
	{
	case 1024: return (Ht == 768) ? 12 : 6;
	case 1280: return (Ht == 1024) ? 18 : 12;
	case 1400: return (Ht == 1050) ? 18 : 6;
	case 1600: return (Ht == 1200) ? 21 : 18;
	case 1680: return 18;
	default: break;
	}
	if (CVarEquipShopLegacyPageSize.GetValueOnGameThread() != 0) { return 6; }
	if (Wd >= 1600) { return (Ht >= 1200) ? 21 : 18; }
	if (Wd >= 1280) { return (Ht >= 1024) ? 18 : 12; }
	if (Wd >= 1024) { return (Ht >= 768) ? 12 : 6; }
	return 6;
}

// `uiList_GetVisibleRows@0xBD6270` — по нему `shopWin_Open@0xC37110` считает высоту контейнера
//   6983: `h = 98 * (rows / 3) + 48`.
int32 UAsdaEquipShopSubsystem::PanelRows() const
{
	FVector2D V(1024.f, 768.f);
	if (const UWorld* W = GetGameInstance() ? GetGameInstance()->GetWorld() : nullptr)
	{
		const FVector2D S = AsdaUi::ViewportSizeUnits(W);
		if (S.X > 1.f && S.Y > 1.f) { V = S; }
	}
	const int32 Wd = FMath::RoundToInt(V.X);
	const int32 Ht = FMath::RoundToInt(V.Y);
	switch (Wd)
	{
	case 1024: return (Ht == 768) ? 9 : 6;
	case 1280: return (Ht == 1024) ? 15 : ((Ht == 800 || Ht == 760) ? 9 : 6);
	case 1400: return (Ht == 1050) ? 12 : 6;
	case 1600: return (Ht == 1000) ? 12 : ((Ht == 1200) ? 21 : 6);
	case 1680: return 18;
	default:   return 6;
	}
}

// ★★★ ОБЩАЯ ТОЧКА ДВУХ ВИТРИН. Раскладку файла `.she` (11 × u16 счётчиков, затем 11 массивов
//   `int32` кодов) разбирает ИМПОРТЁР; сюда приходят готовые списки. ТРИ ПРОИЗВОДНЫХ списка
//   (11/12/13) по-прежнему строит рантайм — они зависят не от файла витрины, а от
//   `itemType`/`itemSubType` в `item_list`, ровно как у эталона (иначе таблица «замёрзла» бы
//   относительно правок item-БД).
//   ⚠ Порядок кодов внутри списка ЗНАЧИМ (по нему считаются страница и место в ней), поэтому
//     строки таблицы раскладываются по полю `ListIndex`, а не по порядку обхода `GetRowMap`.
//   Вызывающие: `UAsdaEquipShopSubsystem::LoadShe` (6800, `Freeshop.she`) и
//   `UAsdaPremiumShopSubsystem::LoadShe` (6000, `Premium.she`).
bool UAsdaEquipShopSubsystem::LoadSheListsFromTable(const TCHAR* TablePath,
	const UAsdaItemTableSubsystem* IT, TArray<TArray<int32>>& OutRaw, const TCHAR* Tag)
{
	OutRaw.Empty();
	OutRaw.SetNum(kLists);

	const UDataTable* Dt = AsdaDt::Cached(TablePath);
	if (!Dt)
	{
		UE_LOG(LogAsdaEquipShop, Error, TEXT("[%s] нет таблицы %s — витрина пуста"), Tag, TablePath);
		OutRaw.Empty();      // чтобы повторный вызов не решил, что витрина уже загружена
		return false;
	}
	for (const TPair<FName, uint8*>& It : Dt->GetRowMap())
	{
		const FAsdaEquipShopListRow* R = reinterpret_cast<const FAsdaEquipShopListRow*>(It.Value);
		if (!R) { continue; }
		// В таблице только 11 списков ФАЙЛА; номер вне 0…10 — это порча данных, а не пустой список.
		if (R->ListIndex < 0 || R->ListIndex >= 11)
		{
			UE_LOG(LogAsdaEquipShop, Error,
				TEXT("[%s] %s: строка '%s' с ListIndex=%d вне 0…10 — пропущена"),
				Tag, TablePath, *It.Key.ToString(), R->ListIndex);
			continue;
		}
		OutRaw[R->ListIndex] = R->ItemCodes;
	}

	// Три производных списка из массива №4: `itemSubType < 4` → 11 («Воин»), `8…11` → 12
	//   («Лучник»), `4…7` → 13 («Маг»). Порядок именно такой (`cShopLoadFreeShop`, tabCode 120/130/140).
	for (int32 Code : OutRaw[4])
	{
		const FAsdaItemRecord* R = IT ? IT->GetItem(Code) : nullptr;
		if (!R || R->ItemType != 9) { continue; }
		const int32 SubT = R->ItemSubType;
		if (SubT < 4)                    { OutRaw[11].Add(Code); }
		else if (SubT >= 8 && SubT < 12) { OutRaw[12].Add(Code); }
		else if (SubT >= 4 && SubT < 8)  { OutRaw[13].Add(Code); }
	}
	UE_LOG(LogAsdaEquipShop, Log,
		TEXT("[%s] %s: категории %d/%d/%d/%d/%d/%d/%d/%d/%d/%d/%d, производные %d/%d/%d"),
		Tag, TablePath,
		OutRaw[0].Num(), OutRaw[1].Num(), OutRaw[2].Num(), OutRaw[3].Num(), OutRaw[4].Num(),
		OutRaw[5].Num(), OutRaw[6].Num(), OutRaw[7].Num(), OutRaw[8].Num(), OutRaw[9].Num(),
		OutRaw[10].Num(), OutRaw[11].Num(), OutRaw[12].Num(), OutRaw[13].Num());
	return true;
}

bool UAsdaEquipShopSubsystem::LoadShe()
{
	if (bSheLoaded) { return RawLists.Num() == kLists; }
	const UAsdaItemTableSubsystem* IT = GetGameInstance() ? GetGameInstance()->GetSubsystem<UAsdaItemTableSubsystem>() : nullptr;
	if (!LoadSheListsFromTable(kListTablePath, IT, RawLists, TEXT("магазин снаряжения"))) { return false; }
	bSheLoaded = true;
	return true;
}

// ★ Фильтр по ПРОФЕССИИ (чекбокс 6803). Таблица снята с сорока функций, которые выбирает
//   `dispatchByOpcode@0xCAE2B0` по коду профессии `CCharacter+0x438`. Условие каждой — набор
//   допустимых `ItemRecord->usageCategory`.
bool UAsdaEquipShopSubsystem::JobAllowsUsage(int32 JobCode, int32 Usage)
{
	// Значения 40…44 — «свободные ступени», их пропускают почти все профессии; конкретный набор
	//   у каждой свой, поэтому таблица перечисляет ВСЁ явно.
	static const TArray<int32> kSets[40] =
	{
		{0},                                        // 0  `uiItemGrid_RebuildCategoryLayout@0xCB60C0`
		{0,1,40},                                   // 1  `itemPanel_RebuildSlots_LowTier_T0_1@0xCB70B0`
		{0,1,2,40,41},                              // 2  `itemPanel_RebuildSlots_LowTier_T0_2@0xCB8140`
		{0,1,2,3,40,41,42},                         // 3  `uiItemGridRebuild_catLt4@0xCB92E0`
		{0,1,2,3,4,40,41,42,43},                    // 4  `uiItemGridRebuild_catLe4@0xCBA4F0`
		{0,1,2,3,4,5,40,41,42,43,44},               // 5  `uiItemGridRebuild_catLt6@0xCBB770`
		{0,1,2,3,6,40,41,42,43},                    // 6  `uiItemGridRebuild_catLt4or6@0xCBCA60`
		{0,1,2,3,6,7,40,41,42,43,44},               // 7  `uiItemGridRebuild_catLt4or6or7@0xCBDD50`
		{0,1,2,3,8,40,41,42,43},                    // 8  `uiItemGridRebuild_catLt4or8@0xCBF120`
		{0,1,2,3,8,9,40,41,42,43,44},               // 9  `inventory_buildCategoryGrids@0xCC0410`
		{0,1,2,3,10,40,41,42,43},                   // 10 `uiItemGridRebuild_catLt4or10@0xCC17E0`
		{0,1,2,3,10,11,40,41,42,43,44},             // 11 `uiItemGrid_RebuildFiltered_A@0xCC2AD0`
		{0,12,40},                                  // 12 `uiItemGrid_RebuildFiltered_B@0xCC3EF0`
		{0,12,13,40,41},                            // 13 `uiItemGrid_RebuildFiltered_C@0xCC5070`
		{0,12,13,14,40,41,42},                      // 14 `uiItemGrid_RebuildFiltered_D@0xCC62D0`
		{0,12,13,14,15,40,41,42,43},                // 15 `uiItemGrid_RebuildFiltered_E@0xCC7610`
		{0,12,13,14,15,16,40,41,42,43,44},          // 16 `uiItemGrid_RebuildFiltered_F@0xCC8950`
		{0,12,13,14,17,40,41,42,43},                // 17 `uiItemGrid_RebuildFiltered_G@0xCC9D00`
		{0,12,13,14,17,18,40,41,42,43,44},          // 18 `uiItemGrid_RebuildTabsB@0xCCB0B0`
		{0,12,13,14,19,40,41,42,43},                // 19 `itemWnd_RebuildTabLayout_Type19@0xCCC550`
		{0,12,13,14,19,20,40,41,42,43,44},          // 20 `..._Type19_20@0xCCD900`
		{0,12,13,14,21,40,41,42,43},                // 21 `..._Type21@0xCCED90`
		{0,12,13,14,21,22,40,41,42,43,44},          // 22 `..._Type21_22@0xCD0140`
		{0,23,40},                                  // 23 `..._Type23@0xCD15D0`
		{0,23,24,40,41},                            // 24 `..._Type23_24@0xCD2750`
		{0,23,24,25,40,41,42},                      // 25 `..._Type23_25@0xCD39B0`
		{0,23,24,25,26,40,41,42,43},                // 26 `uiItemGrid_RebuildLayout@0xCD4C80`
		{0,23,24,25,26,27,40,41,42,43,44},          // 27 `uiItemGrid_RebuildLayout_2@0xCD5FC0`
		{0,23,24,25,28,40,41,42,43},                // 28 `uiItemGrid_RebuildLayout_3@0xCD7370`
		{0,23,24,25,28,29,40,41,42,43,44},          // 29 `uiItemGrid_RebuildLayout_4@0xCD8720`
		{0,23,24,25,30,40,41,42,43},                // 30 `uiItemGrid_RebuildLayout_5@0xCD9BB0`
		{0,23,24,25,30,31,40,41,42,43,44},          // 31 `uiItemGrid_RebuildLayout_6@0xCDAF60`
		{0,23,24,25,32,40,41,42,43},                // 32 `uiItemGrid_RebuildLayout_7@0xCDC3F0`
		{0,23,24,25,32,33,40,41,42,43,44},          // 33 `InvUI_RebuildItemGridTab2_D7A0@0xCDD7A0`
		{0,1,2,3,12,13,14,23,24,25,34,40,41,42,43}, // 34 `InvUI_RebuildItemGridTab3@0xCDEC30`
		{0,1,2,3,12,13,14,23,24,25,34,35,40,41,42,43,44}, // 35 `InvUI_RebuildItemGridTab1@0xCE0130`
		{0,1,2,3,12,13,14,23,24,25,36,40,41,42,43}, // 36 `InvUI_RebuildItemGridTab4@0xCE1710`
		{0,1,2,3,12,13,14,23,24,25,36,37,40,41,42,43,44}, // 37 `InvUI_RebuildItemGridTab2@0xCE2C10`
		{0,1,2,3,12,13,14,23,24,25,38,40,41,42,43}, // 38 `InvUI_RebuildItemGridTab5@0xCE41F0`
		{0,1,2,3,12,13,14,23,24,25,38,39,40,41,42,43,44}  // 39 `uiItemGrid_RebuildTabsA@0xCE56F0`
	};
	// Код профессии вне 0…39 — у эталона `default:` свитча пуст: не строится НИЧЕГО.
	if (JobCode < 0 || JobCode > 39) { return false; }
	return kSets[JobCode].Contains(Usage);
}

// Раскладка одного набора: сколько предметов на странице, номера страниц и максимум.
void UAsdaEquipShopSubsystem::BuildSet(int32 SetIndex)
{
	const UAsdaItemTableSubsystem* IT = GetGameInstance() ? GetGameInstance()->GetSubsystem<UAsdaItemTableSubsystem>() : nullptr;
	const UAsdaNetSubsystem*       Net= GetGameInstance() ? GetGameInstance()->GetSubsystem<UAsdaNetSubsystem>() : nullptr;
	const int32 PerPage  = FMath::Max(1, ItemsPerPage());
	// ★ ИСПРАВЛЕНИЕ 11.10.2026 (дедуп №2, В2-A, U5d): класс — общая точка `AsdaSelf::GetClassId` (байт +0x438, фолбэк — запись char-select rec+24);
	//   ★ ИСПРАВЛЕНИЕ 11.10.2026 (дедуп №2, В2-A, U5c): уровень — общая точка `AsdaSelf::GetLevel` (до 4005/4036 — уровень выбранного персонажа, а не 0). Было: сырые `SelfClassId` (−1 до 4005) и `SelfLevel` (0).
	const int32 JobCode  = AsdaSelf::GetClassId(Net);
	const int32 Gender   = Net ? (int32)Net->ResolveSelfGender() : 0;
	const int32 Level    = AsdaSelf::GetLevel(Net);

	for (int32 k = 0; k < kLists; ++k)
	{
		FAsdaEquipShopList& L = Sets[SetIndex * kLists + k];
		L.TabCode = 10 * (k + 1);          // 10…140 — так их нумерует `cShopLoadFreeShop`
		L.MaxPage = 0;
		L.Items.Reset();
		int32 Placed = 0;
		for (int32 Code : RawLists[k])
		{
			const FAsdaItemRecord* R = IT ? IT->GetItem(Code) : nullptr;
			if (!R) { continue; }            // эталон везде отсекает записи без `ItemRecord`
			bool bKeep = true;
			if (SetIndex == 1)
			{
				// Чекбокс 6803 «Искать предметы одежды» = профессия ∧ пол ∧ уровень.
				//   Списки 0…3 эталон копирует БЕЗ фильтра (`dispatchByOpcode` и
				//   `inventoryBag_RebuildSlotGrids@0xCAE6E0` начинают фильтровать с четвёртого).
				if (k >= 4)
				{
					bKeep = JobAllowsUsage(JobCode, R->UsageCategory);
					// Список №4 (`itemType == 9`) вдобавок сужается до `itemSubType < 4`.
					if (bKeep && k == 4) { bKeep = (R->ItemType == 9 && R->ItemSubType < 4); }
					// Второй проход: пол и уровень (`inventoryBag_RebuildSlotGrids`).
					if (bKeep)
					{
						const bool bSexOk = (Gender == 1) ? (R->ClassRestriction <= 1)
						                                  : (R->ClassRestriction == 0 || R->ClassRestriction == 2);
						bKeep = bSexOk && (Level >= R->RequiredLevel);
					}
				}
			}
			else if (SetIndex == 2)
			{
				// Чекбокс 6808 «Искать для своего пола» — ТОЛЬКО пол и ТОЛЬКО для списков
				//   5, 6, 7, 9, 10 (`CInventoryDlg_RebuildItemGrid@0xCB5330`: списки 4, 8, 11…13
				//   он копирует без фильтра — щит и оружие пола не имеют).
				if (k == 5 || k == 6 || k == 7 || k == 9 || k == 10)
				{
					bKeep = (R->ClassRestriction == 0) || (R->ClassRestriction == Gender);
				}
			}
			if (!bKeep) { continue; }
			FAsdaEquipShopEntry E;
			E.ItemCode   = Code;
			E.Page       = Placed / PerPage;
			E.SlotInPage = Placed % PerPage;
			L.MaxPage    = FMath::Max(L.MaxPage, E.Page);
			L.Items.Add(E);
			++Placed;
		}
	}
}

void UAsdaEquipShopSubsystem::BuildAllSets()
{
	if (!LoadShe()) { return; }
	for (int32 S = 0; S < 3; ++S) { BuildSet(S); }
	BuiltForPixelWidth = ItemsPerPage();
	UE_LOG(LogAsdaEquipShop, Log, TEXT("[магазин снаряжения] наборы собраны: на страницу %d предметов"), BuiltForPixelWidth);
}

const FAsdaEquipShopList& UAsdaEquipShopSubsystem::ActiveList() const
{
	static const FAsdaEquipShopList Empty;
	const int32 K = kTabToList[FMath::Clamp(MenuTab, 0, kMenuTabs - 1)];
	const int32 I = ActiveSet() * kLists + K;
	return Sets.IsValidIndex(I) ? Sets[I] : Empty;
}

// ═════════════════════════════ ОКНО ═══════════════════════════════════════════════════════════

bool UAsdaEquipShopSubsystem::EnsureWidget()
{
	// ★ Каркас окна — `FAsdaJuiWindowHandle::Ensure` (волна 3 аудита дублей): гвард `IsWindowLive`,
	//   сброс протухшего окна, открытие WBP, привязка, скрытие — в одной точке, в прежнем порядке.
	const EAsdaJuiEnsure R = FAsdaJuiWindowHandle(kWindowId, kWbpPath, kShopZ).Ensure(GetGameInstance(), Widget, bControlsReady,
		[this](UAsdaWindowSubsystem&) { ResetWidget(); },
		[this](UAsdaWindowSubsystem&)
		{
			SetupControls();
			RegisterZone();
		},
		[]()
		{
			UE_LOG(LogAsdaEquipShop, Error, TEXT("[магазин снаряжения] WBP не загрузился (%s)"), kWbpPath);
		});
	return FAsdaJuiWindowHandle::Succeeded(R) && bControlsReady;
}

void UAsdaEquipShopSubsystem::ResetWidget()
{
	if (UAsdaWindowSubsystem* WM = GetGameInstance() ? GetGameInstance()->GetSubsystem<UAsdaWindowSubsystem>() : nullptr)
	{
		WM->UnregisterUiZone(kWindowId);
		WM->CloseWindow(kWindowId);
	}
	Widget = nullptr;
	IconGrid = nullptr;
	Relays.Reset();
	MenuButtons.Reset();
	MenuLabels.Reset();
	for (FAsdaEquipShopCardUi& C : Cards)  { C = FAsdaEquipShopCardUi(); }
	for (FAsdaEquipShopWearUi& W : WearUi) { W = FAsdaEquipShopWearUi(); }
	PanelPlanes.Reset();
	GoldBlock.Reset();
	PanelHeightApplied = 0;
	TipCard = -1;
	BtnClose = BtnNext = BtnBack = BtnRemove = BtnReset = BtnBuyWorn = nullptr;
	ChkWear = ChkGender = nullptr;
	TxtChkWear = TxtChkGender = TxtPage = TxtPages = TxtTotal = TxtGold = nullptr;
	TxtWearHdr = TxtSumHdr = CapRemove = CapReset = CapBuyWorn = nullptr;
	bControlsReady = false;
	Mouse.Reset();
}

UTextBlock* UAsdaEquipShopSubsystem::MakeLabelLike(UWidget* Ref, const TCHAR* NewName,
	const UTextBlock* FontDonor, bool bRightAlign)
{
	// ★ Волна 3: общая точка `AsdaShopUi::MakeLabelLike` (магазин снаряжения ≡ военный магазин, тело без изменений).
	return AsdaShopUi::MakeLabelLike(Widget, Ref, NewName, FontDonor, bRightAlign);
}

// Разбор ВСЕХ контролов окна (закон `full-structural-analysis-always`).
void UAsdaEquipShopSubsystem::SetupControls()
{
	bControlsReady = false;
	if (!Widget || !Widget->WidgetTree) { return; }

	TArray<UWidget*> All;
	Widget->WidgetTree->GetAllWidgets(All);
	TMap<FString, UWidget*> ByName;
	ByName.Reserve(All.Num());
	for (UWidget* W : All) { if (W) { ByName.Add(W->GetName(), W); } }
	auto Find = [&ByName](const FString& N) -> UWidget* { UWidget** P = ByName.Find(N); return P ? *P : nullptr; };

	// ── Именованные контролы ──
	BtnClose   = Cast<UButton>(Find(TEXT("SHELLITEM_STORE2_CLOSE")));      // 6801
	BtnNext    = Cast<UButton>(Find(TEXT("SHELLITEM_STORE2_NEXTP")));      // 6853
	BtnBack    = Cast<UButton>(Find(TEXT("SHELLITEM_STORE2_BACKP")));      // 6855
	BtnRemove  = Cast<UButton>(Find(TEXT("SHELLITEM_STORE2_BUT00")));      // 6848
	BtnReset   = Cast<UButton>(Find(TEXT("SHELLITEM_STORE2_BUT01")));      // 6849
	BtnBuyWorn = Cast<UButton>(Find(TEXT("SHELLITEM_STORE2_BUT02")));      // 6851
	ChkWear    = Cast<UCheckBox>(Find(TEXT("SHELLITEM_STORE2_CHECK")));    // 6803
	ChkGender  = Cast<UCheckBox>(Find(TEXT("SHELLITEM_STORE2_CHECK01")));  // 6808
	TxtChkWear   = Cast<UTextBlock>(Find(TEXT("SHELLITEM_STORE2_TXT00")));  // 6802
	TxtChkGender = Cast<UTextBlock>(Find(TEXT("SHELLITEM_STORE2_TXT01")));  // 6807
	TxtPage      = Cast<UTextBlock>(Find(TEXT("SHELLITEM_STORE2_NUM00")));  // 6805
	TxtPages     = Cast<UTextBlock>(Find(TEXT("SHELLITEM_STORE2_NUM01")));  // 6806
	TxtTotal     = Cast<UTextBlock>(Find(TEXT("SHELLITEM_STORE2_TXT09")));  // 6847
	TxtGold      = Cast<UTextBlock>(Find(TEXT("SHELLITEM_STORE2_TXT11")));  // 6850
	TxtWearHdr   = Cast<UTextBlock>(Find(TEXT("SHELLITEM_STORE2_WEARLIST_Text")));
	TxtSumHdr    = Cast<UTextBlock>(Find(TEXT("SHELLITEM_STORE2_SUM_Text")));
	CapRemove    = Cast<UTextBlock>(Find(TEXT("SHELLITEM_STORE2_BUT00_Text")));
	CapReset     = Cast<UTextBlock>(Find(TEXT("SHELLITEM_STORE2_BUT01_Text")));
	CapBuyWorn   = Cast<UTextBlock>(Find(TEXT("SHELLITEM_STORE2_BUT02_Text")));

	// Статические подписи — ровно те loc-id, что ставит `CShellItemStore_InitControls@0xC28820`.
	auto SetLoc = [this](UTextBlock* T, int32 Id) { if (T) { const FString S = Loc(Id); if (!S.IsEmpty()) { T->SetText(FText::FromString(S)); } } };
	SetLoc(TxtChkWear,   EquipShopLoc::kChkWearTxt);
	SetLoc(TxtChkGender, EquipShopLoc::kChkGenderTxt);
	SetLoc(TxtWearHdr,   EquipShopLoc::kWearListHdr);
	SetLoc(TxtSumHdr,    EquipShopLoc::kSumHdr);
	SetLoc(CapRemove,    EquipShopLoc::kRemoveCap);
	SetLoc(CapReset,     EquipShopLoc::kResetCap);
	SetLoc(CapBuyWorn,   EquipShopLoc::kBuyWornCap);
	// ★ 10.10.2026 (`owner_1010/overlaps_fixes.md`): подпись блока «Золото игрока» 7001 (`SHELLITEM_STORE2_TXT10`) кодом не
	//   ставилась — в EN оставался образец ассета «Голд». Эталон блок не показывает (Д5, контрол скрыт), поэтому строки кода
	//   у него нет; id — по IDNum контрола из плана подписей этапа 4 мультиязычности
	//   (`Tools/data/asda_ui_caption_gametext_plan.tsv`: 7001 → GameText 13241 «Золото:» / «My Gold»).
	SetLoc(Cast<UTextBlock>(Find(TEXT("SHELLITEM_STORE2_TXT10"))), EquipShopLoc::kGoldLbl);

	// ── Меню вкладок 6856: RadioButton импортирован девятью кнопками `_OPT0…_OPT8` ──
	MenuButtons.SetNum(kMenuTabs);
	MenuLabels.SetNum(kMenuTabs);
	for (int32 T = 0; T < kMenuTabs; ++T)
	{
		MenuButtons[T] = Cast<UButton>(Find(FString::Printf(TEXT("SHELLITEM_STORE2_MENU_OPT%d"), T)));
	}
	// Собственная подпись переключателя — корейская заглушка «전사» из `.JUI` (эталон её
	//   перетирает через `CListBox_SetItemString`). У нас подписи — девять рантайм-`TextBlock`,
	//   поэтому заглушку гасим.
	if (UWidget* W = Find(TEXT("SHELLITEM_STORE2_MENU_Text"))) { W->SetVisibility(ESlateVisibility::Collapsed); }

	// ── 21 карточка ──
	TArray<FName> IconNames; IconNames.SetNum(kCards);
	// ★ 10.10.2026 (`Reports/owner_1010/overlaps_fixes.md`, СРЕДН-1): строки ассета блока цены карточки 0
	//   (`SHELLITEM2_POP00_TXT02…07` = контролы 79501…79506) — образец оформления для подписей остальных карточек.
	//   В `.JUI` (gamemain_jui.ini:294436-294700 и 294946-295200) у 79501+10k…79506+10k ВСЕХ карточек одинаковые
	//   `TextColor` (C0C0C0 / FFFFFF / FFFFFFAA по строкам) и `TextAlign` (4 подпись влево, 6 число вправо), а
	//   строки импортёр создал только у карточки 0 (у неё одной есть образец текста).
	TArray<UTextBlock*> PriceStyle; PriceStyle.Init(nullptr, 6);
	int32 Resolved = 0;
	for (int32 i = 0; i < kCards; ++i)
	{
		FAsdaEquipShopCardUi& C = Cards[i];
		const FEquipShopCardIdx& X = kCardIdx[i];
		const FString P = FString::Printf(TEXT("SHELLITEM2_POP%02d"), i);

		// ★★★ ВСЕ УЗЛЫ КАРТОЧКИ — ПО ПРЕФИКСУ, А НЕ ПО ВЫЧИСЛЕННЫМ ИМЕНАМ.
		//   ДЕФЕКТ (жалоба владельца 2026-08-26: «что это за значок +»): структура карточек
		//   НЕОДНОРОДНА — например, у `SHELLITEM2_POP01` нет узла `_BUT0_Icon`, который есть
		//   у 00/02/03/04. Точечный поиск `Find("%s_BUT0_Icon_%d_0")` у таких карточек не находил
		//   виджет, тот не попадал в `Parts` — и оставался ВИДИМЫМ на пустой карточке: на экране
		//   висел одинокий «+» (иконка кнопки покупки) там, где товара нет.
		//   Ниже собираем ВСЁ поддерево карточки по префиксу имени. Точечные ссылки (иконка,
		//   подпись, кнопка) остаются — они нужны для заполнения, но за ВИДИМОСТЬ теперь отвечает
		//   этот полный список.
		{
			const FString Pref = P + TEXT("_");
			for (UWidget* W : All)
			{
				if (!W) { continue; }
				const FString N = W->GetName();
				if (N == P || N.StartsWith(Pref)) { C.Parts.AddUnique(W); }
			}
		}

		for (int32 Pl = 0; Pl < 8; ++Pl)
		{
			if (UWidget* W = Find(FString::Printf(TEXT("%s_%d_%d"), *P, X.Box, Pl))) { C.BoxPlanes.Add(W); C.Parts.Add(W); }
			if (UWidget* W = Find(FString::Printf(TEXT("%s_SEL_%d_%d"), *P, X.Sel, Pl))) { C.SelPlanes.Add(W); }
		}
		if (UWidget* W = Find(FString::Printf(TEXT("%s_TXT00_%d_0"), *P, X.Name))) { C.NamePlane = W; C.Parts.Add(W); }
		// ★★★ ЗАГЛУШКА-ПОДПИСЬ ИЗ `CaptionText` — В СПИСОК «ВСЕГДА СКРЫТЫХ».
		//   Импортёр перенёс из `.JUI` авторский `CaptionText` карточки (например, «Ёнгари» —
		//   имя товара, стоявшее в редакторе окна), и он спорит с нашей рантайм-подписью.
		//   ⚠ РЕГРЕССИЯ 2026-08-26: раньше его гасили ЗДЕСЬ, один раз при разборе, но после того
		//   как `ShowCard` стал поднимать ВСЁ поддерево карточки (иначе не гасились «плюсы»
		//   пустых карточек), заглушка возвращалась на экран. Теперь она в отдельном списке,
		//   который гасится ПОСЛЕ общего показа — и при разборе, и при каждом показе карточки.
		if (UWidget* W = Find(FString::Printf(TEXT("%s_TXT00_Text"), *P)))
		{
			W->SetVisibility(ESlateVisibility::Collapsed);
			C.AlwaysHidden.Add(W);
		}
		// ★ 10.10.2026 (СРЕДН-1): блоки 57011 «Мл. лейтенант и выше» (+ значок 57013) и 57012 «500~» (медали) есть
		//   у карточек 0 и 1 (у карточки 1 — 57015 + значок 57017 и 57016, без текста), в `.JUI` все объявлены
		//   `Visible = "0"`; импортёр поднимал только поддерево карточки 0 (ревизия overlaps_fixes МЕЛК-3, уточнено
		//   10.10.2026). Эталон их не включает: ни одного обращения к
		//   57011…57013 нет ни в декомпиле (`RE/etalon/*.c`), ни непосредственным операндом в `AsdaStory.exe`
		//   13 344 768 б (скан dword 56990…57140 — эти id не встречаются). Карточка 0 поднимала их вместе со
		//   всем поддеревом по префиксу имени — поэтому гасим их после общего показа, как заглушку имени.
		for (const TCHAR* Grp : { TEXT("_TXT_M_01_grp"), TEXT("_TXT_M_02_grp") })
		{
			if (UWidget* W = Find(P + Grp))
			{
				W->SetVisibility(ESlateVisibility::Collapsed);
				C.AlwaysHidden.Add(W);
			}
		}
		C.BuyButton  = Cast<UButton>(Find(FString::Printf(TEXT("%s_BUT0"), *P)));
		C.BuyCaption = Cast<UTextBlock>(Find(FString::Printf(TEXT("%s_BUT0_Text"), *P)));
		if (C.BuyButton)  { C.Parts.Add(C.BuyButton); }
		if (C.BuyCaption) { C.Parts.Add(C.BuyCaption); }
		if (UWidget* W = Find(FString::Printf(TEXT("%s_BUT0_Icon_%d_0"), *P, X.Buy + 1))) { C.Parts.Add(W); }
		C.PriceText = Cast<UTextBlock>(Find(FString::Printf(TEXT("%s_TXT01"), *P)));
		if (UWidget* W = Find(FString::Printf(TEXT("%s_TXT01_Icon_%d_0"), *P, X.Price + 1))) { C.Parts.Add(W); }
		if (UImage* Img = Cast<UImage>(Find(FString::Printf(TEXT("%s_ICON_%d_0"), *P, X.Icon))))
		{
			C.ItemIcon = Img; C.Parts.Add(Img); IconNames[i] = FName(*Img->GetName());
		}
		// Блок цены: 7 HIT-зон, над шестью из них — рантайм-подписи.
		C.PriceZones.SetNum(7);
		C.PriceTexts.SetNum(6);
		for (int32 Z = 0; Z < 7; ++Z)
		{
			C.PriceZones[Z] = Find(FString::Printf(TEXT("NoImage_%d_HIT"), kPriceIdx[i][Z]));
			if (C.PriceZones[Z]) { C.Parts.Add(C.PriceZones[Z].Get()); }
		}
		for (int32 Z = 1; Z < 7; ++Z)
		{
			// ★ 10.10.2026 (СРЕДН-1): у карточки 0 строка контрола уже есть в ассете (той же геометрии, что хит-зона) —
			//   берём её, а не строим вторую поверх (было: «Цена/Себестоимость», «999,999», «Скидка лагеря» внахлёст;
			//   тот же дефект, что в окне ремонта). Остальным карточкам строим подпись и даём ей оформление строки
			//   карточки 0 (цвет, выравнивание, шрифт из `.JUI`).
			if (UTextBlock* Own = AsdaUi::FindOwnLabelLike(C.PriceZones[Z].Get()))
			{
				C.PriceTexts[Z - 1] = Own;
				if (!PriceStyle[Z - 1]) { PriceStyle[Z - 1] = Own; }
			}
			else
			{
				C.PriceTexts[Z - 1] = MakeLabelLike(C.PriceZones[Z].Get(),
					*FString::Printf(TEXT("EquipShop_Price_%d_%d"), i, Z), TxtGold, false);
				if (C.PriceTexts[Z - 1] && PriceStyle[Z - 1])
				{
					const UTextBlock* St = PriceStyle[Z - 1];   // строка карточки 0 того же ряда
					C.PriceTexts[Z - 1]->SetFont(St->GetFont());
					C.PriceTexts[Z - 1]->SetColorAndOpacity(St->GetColorAndOpacity());
					C.PriceTexts[Z - 1]->SetJustification(AsdaUi::ReadJustification(St));
				}
			}
			if (C.PriceTexts[Z - 1]) { C.Parts.Add(C.PriceTexts[Z - 1].Get()); }
		}
		// ★★★ ЗДЕСЬ ЖИЛ ДЕФЕКТ «НЕ ВИДНО ЦЕН». Контрол 79500 в `.JUI` помечен `Visible = "0"`,
		//   импортёр честно свернул ВСЮ его ветку (группа `NoImage_grp` + всё поддерево →
		//   `COLLAPSED`), а порт включал только ЛИСТЬЯ (`C.Parts`). Свёрнутая группа-родитель в
		//   UMG гасит поддерево так же, как скрытый контрол в оригинале
		//   (`CControl_RenderRecursive`: в ребёнка заходят только `if (CControl_IsVisible(child))`),
		//   поэтому цены не рисовались вообще. Эталон включает блок КОДОМ, одной строкой
		//   `CControl_SetVisible(pThis[13*slot + 665], 1)` — делаем то же (`ShowPriceBlock`).
		//   Собираем корень блока и ВСЁ поддерево — собирать надо ПОСЛЕ создания подписей,
		//   иначе они в набор не попадут.
		C.PriceBlock.Reset();
		if (C.PriceZones[0])
		{
			if (UPanelWidget* Root = C.PriceZones[0]->GetParent())
			{
				C.PriceBlock.Add(Root);
				TArray<UWidget*> Stack; Stack.Add(Root);
				while (Stack.Num() > 0)
				{
					UWidget* Cur = Stack.Pop(EAllowShrinking::No);
					if (UPanelWidget* Pan = Cast<UPanelWidget>(Cur))
					{
						for (int32 Ci = 0; Ci < Pan->GetChildrenCount(); ++Ci)
						{
							if (UWidget* Kid = Pan->GetChildAt(Ci)) { C.PriceBlock.Add(Kid); Stack.Add(Kid); }
						}
					}
				}
			}
		}
		if (C.PriceBlock.Num() == 0)
		{
			UE_LOG(LogAsdaEquipShop, Warning,
				TEXT("[магазин снаряжения] карточка %d: блок цены 79500+%d не найден — цены не будет"),
				i, 10 * i);
		}
		// Подпись имени — своя, поверх подложки 6858+6k.
		C.NameText = MakeLabelLike(C.NamePlane.Get(), *FString::Printf(TEXT("EquipShop_Name_%d"), i), TxtGold, false);
		if (C.NameText) { C.Parts.Add(C.NameText.Get()); }

		if (C.ItemIcon && C.BuyButton) { ++Resolved; }
		else
		{
			UE_LOG(LogAsdaEquipShop, Warning,
				TEXT("[магазин снаряжения] карточка %d (JUI-индекс %d): иконка %s, кнопка %s — не разобрана"),
				i, X.Box, C.ItemIcon ? TEXT("есть") : TEXT("НЕТ"), C.BuyButton ? TEXT("есть") : TEXT("НЕТ"));
		}
	}

	// ── 7 строк «Списка снаряжения» ──
	for (int32 j = 0; j < kWearRows; ++j)
	{
		FAsdaEquipShopWearUi& W = WearUi[j];
		const FEquipShopWearIdx& X = kWearIdx[j];
		const FString P = FString::Printf(TEXT("SHELLITEM_STORE2_TXT%02d"), 2 + j);
		W.Row      = Cast<UButton>(Find(P));
		W.NameTxt  = Cast<UTextBlock>(Find(P + TEXT("_00")));
		W.PriceTxt = Cast<UTextBlock>(Find(P + TEXT("_01")));
		W.CheckOn  = Find(FString::Printf(TEXT("%sSEL_%d_0"), *P, X.CheckOn));
		W.CheckOff = Find(FString::Printf(TEXT("%s_02_%d_0"), *P, X.CheckOff));
		W.GoldIcon = Find(FString::Printf(TEXT("%s_04_%d_0"), *P, X.Gold));

		// ★★★ ВСЁ ПОДДЕРЕВО СТРОКИ — ПО ПРЕФИКСУ (жалоба владельца 2026-08-26: «обводки в корзине»).
		//   Строки списка, как и карточки витрины, собраны в `.JUI` НЕОДНОРОДНО: у части есть планы
		//   рамки/подложки, которых нет у соседей, и точечный поиск по вычисленным именам их
		//   не находил. Непогашенные планы пустых строк и давали светлые обводки на пустом месте.
		//   Точечные ссылки выше остаются (по ним заполняем), а за ВИДИМОСТЬ отвечает этот список.
		W.Parts.Reset();
		{
			const FString Pref = P + TEXT("_");
			for (UWidget* Wd : All)
			{
				if (!Wd) { continue; }
				const FString N = Wd->GetName();
				// ⚠ `StartsWith(P)` без разделителя схватил бы TXT02 в TXT020 — сравниваем точно
				//   с именем строки либо с её префиксом «имя_», плюс отдельная форма «имяSEL_».
				if (N == P || N.StartsWith(Pref) || N.StartsWith(P + TEXT("SEL_"))) { W.Parts.AddUnique(Wd); }
			}
		}
	}

	// ── Девять планов рамки витрины 6983 (для `ApplyPanelHeight`) ──
	PanelPlanes.Reset();
	PanelPlanes.SetNum(9);
	for (int32 Pl = 0; Pl < 9; ++Pl)
	{
		PanelPlanes[Pl] = Find(FString::Printf(TEXT("Shellitem_Store2_Win_Part2_%d_%d"), kPanelJuiIdx, Pl));
	}
	PanelHeightApplied = 0;

	// ── ★ Д5: блок «Золото игрока». В `.JUI` контрол 7001 (`SHELLITEM_STORE2_TXT10`) помечен
	//   `Visible = "0"`, поэтому импортёр свернул его группу вместе с 6850 и монеткой. Собираем
	//   ветку целиком, включать/выключать её будет `RefreshGoldText` по CVar (см. шапку CVar Д5).
	GoldBlock.Reset();
	if (TxtGold)
	{
		// Поднимаемся до группы контрола 7001: она содержит и подпись 7001, и 6850, и буллет.
		UWidget* Node = TxtGold.Get();
		UPanelWidget* Root = nullptr;
		for (int32 Up = 0; Up < 4 && Node; ++Up)
		{
			UPanelWidget* Par = Node->GetParent();
			if (!Par) { break; }
			Root = Par;
			if (Par->GetName().StartsWith(TEXT("SHELLITEM_STORE2_TXT10"))) { break; }
			Node = Par;
		}
		if (Root)
		{
			GoldBlock.Add(Root);
			TArray<UWidget*> Stack; Stack.Add(Root);
			while (Stack.Num() > 0)
			{
				UWidget* Cur = Stack.Pop(EAllowShrinking::No);
				if (UPanelWidget* Pan = Cast<UPanelWidget>(Cur))
				{
					for (int32 Ci = 0; Ci < Pan->GetChildrenCount(); ++Ci)
					{
						if (UWidget* Kid = Pan->GetChildAt(Ci)) { GoldBlock.Add(Kid); Stack.Add(Kid); }
					}
				}
			}
		}
	}

	// ── Иконки товара через ОБЩУЮ точку (закон `port-shared-points`) ──
	//   Клик ловим САМИ по живой геометрии (`TickCards`), оверлей выключен.
	IconGrid = NewObject<UAsdaIconCellGrid>(this);
	IconGrid->SetCellSizePx(64.f);            // `.JUI`: Size = "64 64" у 6861+6k
	IconGrid->SetClickOverlayEnabled(false);
	IconGrid->bClearEmptyCells = true;
	IconGrid->SetupNamed(Widget, IconNames, /*bMale*/true,
		[this](int32 Card) -> int32 { return Cards.IsValidIndex(Card) ? Cards[Card].ItemCode : 0; },
		[this](int32 Card) { OnCardPreviewClicked(Card); });

	// ── Кнопки ──
	// ★ 06.11.2026 [ОРИГИНАЛ]: `uiHandleCommand@0xC23AC0` (ui_core.c:25824-25923) первым делом играет звук 4 на 6801, 6848, 6849, 6851,
	//   6853, 6855, строках примерки 6812+5j и карточках 6859+6k; вкладки 6856 в этой функции нет — они без звука.
	auto Bind = [this](UButton* Btn, int32 IdNum, TFunction<void(int32)>&& Fn)
	{
		if (!Btn) { return; }
		TFunction<void(int32)> Sounded = [this, IdNum, Inner = MoveTemp(Fn)](int32 Arg)
		{
			if (IdNum != 6856 && CVarEquipShopButtonSound.GetValueOnGameThread() != 0)
			{
				UGameInstance* GI = GetGameInstance();
				if (UAsdaSoundSubsystem* Snd = GI ? GI->GetSubsystem<UAsdaSoundSubsystem>() : nullptr) { Snd->PlaySound2DById(4); }
			}
			Inner(Arg);
		};
		AsdaUi::BindButton(this, Btn, IdNum, MoveTemp(Sounded), Relays);   // ★ U20: реле клика — общая точка
	};
	Bind(BtnClose,   6801, [this](int32) { CloseWindow(); });
	Bind(BtnNext,    6853, [this](int32) { SetPage(Page + 1); });
	Bind(BtnBack,    6855, [this](int32) { SetPage(Page - 1); });
	Bind(BtnRemove,  6848, [this](int32) { OnRemoveCheckedClicked(); });
	Bind(BtnReset,   6849, [this](int32) { OnResetClicked(); });
	Bind(BtnBuyWorn, 6851, [this](int32) { OnBuyWornClicked(); });
	for (int32 T = 0; T < kMenuTabs; ++T)
	{
		Bind(MenuButtons[T], 6856, [this, T](int32) { SetMenuTab(T); });
		MenuLabels[T] = MakeLabelLike(MenuButtons[T].Get(), *FString::Printf(TEXT("EquipShop_Tab_%d"), T), TxtGold, false);
		if (MenuLabels[T])
		{
			MenuLabels[T]->SetJustification(ETextJustify::Center);
			const FString S = Loc(kTabLoc[T]);
			if (!S.IsEmpty()) { MenuLabels[T]->SetText(FText::FromString(S)); }
		}
	}
	// Начальное состояние вкладок (та же общая точка, что и при переключении).
	MenuIdleColors.Reset();
	AsdaUi::ApplyTabCaptionState(MenuLabels, MenuTab, MenuIdleColors);
	for (int32 i = 0; i < kCards; ++i)
	{
		const int32 Card = i;
		Bind(Cards[i].BuyButton.Get(), 6859 + 6 * i, [this, Card](int32) { OnCardBuyClicked(Card); });
	}
	for (int32 j = 0; j < kWearRows; ++j)
	{
		const int32 Row = j;
		Bind(WearUi[j].Row.Get(), 6812 + 5 * j, [this, Row](int32) { OnWearRowClicked(Row); });
	}

	// ── Чекбоксы: взаимоисключающие (`uiHandleToggleCommand@0xC250D0`) ──
	if (ChkWear)
	{
		UAsdaButtonRelay* R = NewObject<UAsdaButtonRelay>(this);
		R->Index = 6803;
		ChkWear->OnCheckStateChanged.AddDynamic(R, &UAsdaButtonRelay::OnCheckChanged);
		AsdaUi::AnchorRelay(Relays, R, ChkWear);   // ★ 31.10.2026: якорь чистит мёртвые/перепривязанные (`asda.UI.RelayAnchorPrune`)
		R->CheckCallback = [this](int32, bool bOn) { SetFilter(0, bOn); };
	}
	if (ChkGender)
	{
		UAsdaButtonRelay* R = NewObject<UAsdaButtonRelay>(this);
		R->Index = 6808;
		ChkGender->OnCheckStateChanged.AddDynamic(R, &UAsdaButtonRelay::OnCheckChanged);
		AsdaUi::AnchorRelay(Relays, R, ChkGender);   // ★ 31.10.2026: якорь чистит мёртвые/перепривязанные (`asda.UI.RelayAnchorPrune`)
		R->CheckCallback = [this](int32, bool bOn) { SetFilter(1, bOn); };
	}

	bControlsReady = (Resolved == kCards) && (BtnClose != nullptr);
	UE_LOG(LogAsdaEquipShop, Log,
		TEXT("[магазин снаряжения] окно 6800 разобрано: карточек %d/%d, строк списка %d, вкладок %d, ")
		TEXT("крестик %s, страницы %s/%s, золото %s, итого %s, чекбоксы %s/%s"),
		Resolved, kCards, kWearRows,
		(MenuButtons[0] ? 1 : 0) + (MenuButtons[1] ? 1 : 0) + (MenuButtons[2] ? 1 : 0) + (MenuButtons[3] ? 1 : 0)
		+ (MenuButtons[4] ? 1 : 0) + (MenuButtons[5] ? 1 : 0) + (MenuButtons[6] ? 1 : 0) + (MenuButtons[7] ? 1 : 0)
		+ (MenuButtons[8] ? 1 : 0),
		BtnClose ? TEXT("да") : TEXT("НЕТ"), TxtPage ? TEXT("да") : TEXT("НЕТ"), TxtPages ? TEXT("да") : TEXT("НЕТ"),
		TxtGold ? TEXT("да") : TEXT("НЕТ"), TxtTotal ? TEXT("да") : TEXT("НЕТ"),
		ChkWear ? TEXT("да") : TEXT("НЕТ"), ChkGender ? TEXT("да") : TEXT("НЕТ"));
}

void UAsdaEquipShopSubsystem::RegisterZone()
{
	UAsdaWindowSubsystem* WM = GetGameInstance() ? GetGameInstance()->GetSubsystem<UAsdaWindowSubsystem>() : nullptr;
	if (!WM) { return; }
	// ★ Волна 3: зона по планам рамки — общая точка `FAsdaJuiWindowHandle::RegisterFrameZone` (`!bOpen || !Widget` → нет зоны).
	FAsdaJuiWindowHandle::RegisterFrameZone(*WM, kWindowId, this, Widget, bOpen, TEXT("SHELLITEM_STORE2_WIN_0_%d"), 9);
}

// ═════════════════════════════ ОТКРЫТИЕ / ЗАКРЫТИЕ ════════════════════════════════════════════

// `shopWin_Open@0xC37110` → `tradeWinConfirmSubmit@0xC37CB0`: два гейта, сброс окна, снимок
//   надетого, применение моделей и нотис 6815 в чат.
void UAsdaEquipShopSubsystem::OpenWindow()
{
	if (bOpen) { return; }
	UGameInstance* GI = GetGameInstance();

	// Гейт 1: ОКНО обмена 2008 открыто (`g_bPartyInviteFlag` = `CExchange+0x2C`) — нотис 6947 в чат (−6881324).
	//   ★ 29.09.2026 (волна 0 торговли, реестр №41а гейт 1): флаг ЖИВОЙ (писатель @0xC75A72), подключена общая точка
	//   `UAsdaTradeStateSubsystem::IsExchangeWindowOpen` (пока окна 2008 нет — false). [ОРИГИНАЛ] shop_trade.c:4176-4180.
	if (UAsdaTradeStateSubsystem::IsExchangeWindowOpen(GI))
	{
		// ★ Ревизия С-1: канал обработчика эталона — чат цветом −6881324 (0xFF96FFD4), 10 с (не категория Trade).
		AsdaNotice::ChatColored(GI, EAsdaMsgCategory::System, AsdaNotice::LocRaw(GI, EquipShopLoc::kBusyTrade),
			FColor(0x96, 0xFF, 0xD4, 0xFF), 10000.f);   // 6947
		UE_LOG(LogAsdaEquipShop, Log, TEXT("[магазин снаряжения] окно обмена 2008 открыто — не открываем, нотис 6947 (tradeWinConfirmSubmit)"));
		return;
	}
	// Гейт 2: состояние персонажа (`objStateAllowsRender`) — нотис 6946 в чат.
	//   ★ 25.10.2026 (ревизия sit108, М-3) [ОРИГИНАЛ] (`tradeWinConfirmSubmit@0xC37CB0`): гейт подключён к общей точке «персонаж занят»
	//   (`UAsdaSystemOptionSubsystem::IsLocalPlayerBusy`) — окно не открывается, стопа сцены нет. Ключ `asda.Char.SitExitEtalon 0` — прежнее.
	if (AAsdaPlayerCharacter::IsSitExitEtalon() && GetGameInstance() && GetGameInstance()->GetSubsystem<UAsdaSystemOptionSubsystem>() && GetGameInstance()->GetSubsystem<UAsdaSystemOptionSubsystem>()->IsLocalPlayerBusy())
	{
		AsdaNotice::ChatColored(GI, EAsdaMsgCategory::System, AsdaNotice::LocRaw(GI, EquipShopLoc::kBusyState),
			FColor(0x96, 0xFF, 0xD4, 0xFF), 10000.f);   // 6946, канал как у гейта 1
		UE_LOG(LogAsdaEquipShop, Log, TEXT("[магазин снаряжения] персонаж занят — не открываем, нотис 6946 (tradeWinConfirmSubmit)"));
		return;
	}

	if (!LoadShe())
	{
		UE_LOG(LogAsdaEquipShop, Error, TEXT("[магазин снаряжения] витрина не прочитана — окно не открываем"));
		return;
	}
	BuildAllSets();
	if (!EnsureWidget()) { return; }
	// ★ 24.10.2026 (выход из сидения 1:1) [ОРИГИНАЛ]: `tradeWinConfirmSubmit@0xC37CB0` — на ходу 4008, затем `charActionStateIs7to9 ? CharFx(9) : CharFx(0)`
	//   (сидящий встаёт БЕЗ 109, по концу «встаёт» — 4024 0; иначе покой). [ПОРТ] прежде шаг не был перенесён. Ключ `asda.Char.SitExitEtalon`.
	if (APlayerController* StopPc = GetWorld() ? GetWorld()->GetFirstPlayerController() : nullptr)
	{
		if (AAsdaPlayerCharacter* StopCh = Cast<AAsdaPlayerCharacter>(StopPc->GetPawn())) { StopCh->ApplyEtalonSceneStop(TEXT("магазин снаряжения (tradeWinConfirmSubmit)")); }
	}

	bOpen    = true;
	bPending = false;
	// `shopWindowReset@0xC37540`: страница 0, вкладка 0, оба фильтра сняты, «Итого» = 0.
	MenuTab  = 0;
	MenuTabPrev = 0;
	Page     = 0;
	SelCard  = -1;
	bFilterWear = false;
	bFilterGender = false;
	if (ChkWear)   { ChkWear->SetIsChecked(false); }
	if (ChkGender) { ChkGender->SetIsChecked(false); }
	PendingItemCode = 0;
	PendingBuyCodes.Reset();

	// Иконки товара берутся из атласа СВОЕГО пола: эталон рисует их через `g_ItemImgOptionVariant`
	//   (`CItemManager_GetImgOptionPtr` в `uiItemListSlot_Populate@0xC3A200`).
	if (IconGrid)
	{
		const UAsdaNetSubsystem* Net = GI ? GI->GetSubsystem<UAsdaNetSubsystem>() : nullptr;
		IconGrid->SetMale(!Net || Net->ResolveSelfGender() != 2);
	}

	// ★ Волна В4 ремесла (ревизия В3 С-1): `tradeWinConfirmSubmit@0xC37CB0` (shop_trade.c:4193-4220) — мини-снимок
	//   `uiMarkPendingWindowOpen` (сумка [2089], окно персонажа [2090]), закрыть окно персонажа (`uiWindowClose`),
	//   звук id 5, закрыть сумку (`sowelWindowClose(g_pInventoryWnd, 1)`). Вернёт их закрытие магазина.
	if (GI)
	{
		UAsdaInventorySubsystem* InvS = GI->GetSubsystem<UAsdaInventorySubsystem>();
		UAsdaCharaInfoSubsystem* Chara = GI->GetSubsystem<UAsdaCharaInfoSubsystem>();
		bReopenBagOnClose   = InvS && InvS->IsWindowOpen();
		bReopenCharaOnClose = Chara && Chara->IsWindowOpen();
		if (bReopenCharaOnClose) { Chara->CloseWindow(); }
		if (UAsdaSoundSubsystem* Snd = GI->GetSubsystem<UAsdaSoundSubsystem>()) { Snd->PlaySound2DById(5); }
		if (bReopenBagOnClose) { InvS->CloseWindow(); }
		UE_LOG(LogAsdaEquipShop, Log, TEXT("[магазин снаряжения] мини-снимок окон: сумка %d, персонаж %d (закрыты до выхода из магазина)"),
			bReopenBagOnClose ? 1 : 0, bReopenCharaOnClose ? 1 : 0);
	}

	SnapshotEquip();
	// Эталон при открытии СНИМАЕТ с модели оружие и щит (`charApplyEquipPartsModels(..., 1)`):
	//   в примерочной персонаж стоит без них, пока их не примерят.
	ApplyPreviewModels(/*bClosing*/false);
	// ★★★ ЧЁРНАЯ ОБВОДКА ТЕКСТА — СНЯТА (требование владельца 2026-08-26: «надписи всё ещё чёрные…
	//   я требовал убрать обводку»).
	//   ПРИЧИНА ВИДА: импортёр ставит контур 1 px чёрным всем текстам, у чьего скина `TextOutLine="1"`
	//   (свойство скина, 7059 из 7862 контролов) — это поведение оригинала. Но названия товаров в
	//   «Списке снаряжения» сами ЧЁРНЫЕ (`EXS TextColor = FF000000` у `SHELLITEM_STORE2_TXT0N_00`),
	//   и при кегле 8 контур той же черноты «съедает» букву: надпись читается как грязное пятно.
	//   Снимаем обводку по ВСЕМУ окну общей точкой `ApplyTextOutlineTree` (закон `port-shared-points`).
	//   ⚠ Это ОСОЗНАННОЕ отступление от оригинала, ключ отката — `asda.EquipShop.TextOutline 1`.
	// ★★★ УБРАНО 2026-08-27. Это было ОСОЗНАННОЕ отступление от оригинала — и оно больше не нужно:
	//   выяснилось, что обводки тут не должно быть ПО ДАННЫМ ОРИГИНАЛА (у `EquipShop_Win` она не
	//   положена ни одному контролу), а появлялась она из-за ошибки импортёра — он читал поле 925
	//   («флаги переноса строк») вместо поля 926 («стиль текста») и ставил обводку 7059 контролам
	//   из 7862. Теперь обводка расставлена по данным в самих ассетах шагом
	//   `Tools/asda_ui_text_style926.py`, а правило в импортёре отключено.
	//   ⇒ Отступления нет, ключ отката `asda.EquipShop.TextOutline` больше ничего не переключает.

	ApplyPanelHeight();      // `shopWin_Open@0xC37110`: подгонка высоты контейнера 6983
	RefreshWearList();
	BuildPage();
	RefreshGoldText();

	if (UAsdaWindowSubsystem* WM = GI ? GI->GetSubsystem<UAsdaWindowSubsystem>() : nullptr)
	{
		WM->SetWindowVisible(kWindowId, true);
		// ★★★ НАША ДОБАВКА (требование владельца 2026-08-26 №4): окно магазина снаряжения живёт
		//   в ПОЛОСЕ ПРИОРИТЕТНЫХ ОКОН [8800…8899] — иначе оно открывается ПОЗАДИ окна разговора
		//   с NPC (570), из которого его и открыли. Полоса заведена для коробок/бустеров, см.
		//   `UAsdaWindowSubsystem::kPriorityZ`. Тултипы (9099) и модалки (20000) НАМЕРЕННО выше:
		//   подсказка предмета и подтверждение покупки обязаны быть видны поверх окна.
		//   ⚠ В `.JUI` признака «приоритетное окно» нет (`AlwaysTop` — это про порядок подъёма,
		//   и у 6800 он равен 0), так что это отступление от оригинала, а не воспроизведение.
		WM->SetWindowPriority(kWindowId, true);
	}
	NoticeChat(EquipShopLoc::kOpenHint);
	UE_LOG(LogAsdaEquipShop, Log, TEXT("[магазин снаряжения] окно открыто: вкладка %d, предметов %d, страниц %d"),
		MenuTab, ActiveList().Items.Num(), MaxPage + 1);
}

// `shopWin_Close@0xC372C0`: сброс состояния, ПОЛНОЕ восстановление реальных моделей.
void UAsdaEquipShopSubsystem::CloseWindow()
{
	if (!bOpen) { return; }
	bOpen = false;
	// ★ Волна В4 ремесла (ревизия В3 С-1): `shopWin_Close@0xC372C0` начинает со звука id 6 (shop_trade.c:3887).
	if (UAsdaSoundSubsystem* Snd = GetGameInstance() ? GetGameInstance()->GetSubsystem<UAsdaSoundSubsystem>() : nullptr)
	{
		Snd->PlaySound2DById(6);
	}
	SelCard = -1;
	PendingItemCode = 0;
	// Плашку-подсказку гасит ТОЛЬКО её владелец (закон `ui-shared-tip-ownership`).
	if (UAsdaItemTooltipSubsystem* Tip = GetGameInstance() ? GetGameInstance()->GetSubsystem<UAsdaItemTooltipSubsystem>() : nullptr)
	{
		Tip->HideTooltipFor(EAsdaTipOwner::EquipShop);
	}
	TipCard = -1;
	for (FAsdaEquipShopWearRow& R : WearRows) { R = FAsdaEquipShopWearRow(); }
	// ★ Волна В4 ремесла (ревизия В3 С-1): `uiShellStore_RefreshOpenWindows@0xC27550` (shop_trade.c:3899) — вернуть окна
	//   мини-снимка: сумку `uiOpenInventoryWindow(…, 1)` (звук id 5; `OpenWindow` порта молчит — звук здесь, как в снимке
	//   окон) и окно персонажа `uiWindow_OpenIfAllowed` (звук играет само окно); флаги в 0.
	if (UGameInstance* GI = GetGameInstance())
	{
		if (bReopenBagOnClose)
		{
			if (UAsdaInventorySubsystem* InvS = GI->GetSubsystem<UAsdaInventorySubsystem>())
			{
				if (!InvS->IsWindowOpen()) { InvS->OpenWindow(); }
				if (InvS->IsWindowOpen()) { if (UAsdaSoundSubsystem* Snd = GI->GetSubsystem<UAsdaSoundSubsystem>()) { Snd->PlaySound2DById(5); } }
			}
		}
		if (bReopenCharaOnClose)
		{
			if (UAsdaCharaInfoSubsystem* Chara = GI->GetSubsystem<UAsdaCharaInfoSubsystem>()) { if (!Chara->IsWindowOpen()) { Chara->OpenWindow(); } }
		}
	}
	bReopenBagOnClose = bReopenCharaOnClose = false;
	ApplyPreviewModels(/*bClosing*/true);
	if (UAsdaWindowSubsystem* WM = GetGameInstance() ? GetGameInstance()->GetSubsystem<UAsdaWindowSubsystem>() : nullptr)
	{
		WM->SetWindowVisible(kWindowId, false);
	}
	UE_LOG(LogAsdaEquipShop, Log, TEXT("[магазин снаряжения] окно закрыто"));
	// ★ Волна В4 ремесла (ревизия В3 С-1): хвост `shopWin_Close` — `UI_resetAllWindowsAndLeave_5045(g_pNpcWnd)`
	//   (shop_trade.c:3918): выход из разговора, спрятанного при открытии магазина, и восстановление снимка окон.
	if (UAsdaNpcDialogSubsystem* Dlg = GetGameInstance() ? GetGameInstance()->GetSubsystem<UAsdaNpcDialogSubsystem>() : nullptr)
	{
		Dlg->ResetAllWindowsAndLeave(TEXT("закрытие магазина снаряжения 6800 (shopWin_Close)"));
	}
}

// ═════════════════════════════ СТРАНИЦА ═══════════════════════════════════════════════════════

void UAsdaEquipShopSubsystem::ShowCard(int32 Card, bool bVisible)
{
	if (!Cards.IsValidIndex(Card)) { return; }
	const ESlateVisibility V = bVisible ? ESlateVisibility::SelfHitTestInvisible : ESlateVisibility::Collapsed;
	for (const TObjectPtr<UWidget>& W : Cards[Card].Parts) { if (W) { W->SetVisibility(V); } }
	// ★ Узлы, которые не должны показываться НИКОГДА (авторские заглушки `CaptionText` из `.JUI`),
	//   гасим ПОСЛЕ общего показа — иначе они всплывают вместе с остальным поддеревом.
	for (const TObjectPtr<UWidget>& W : Cards[Card].AlwaysHidden)
	{
		if (W) { W->SetVisibility(ESlateVisibility::Collapsed); }
	}
	// ★★★ КНОПКЕ ПОКУПКИ нужен ЖИВОЙ хит-тест: она настоящая `UButton` со своим `OnClicked`,
	//   а `SelfHitTestInvisible` снял бы у неё приём мыши (закон `selfhittest-vs-hittest-invisible`).
	if (Cards[Card].BuyButton)
	{
		AsdaUi::SetShownHit(Cards[Card].BuyButton, bVisible);
	}
	// ★ 1:1 `uiItemListSlot_Populate`: собственный ценник карточки (6860+6k) эталон ГАСИТ сразу
	//   после записи — цену показывает блок 79502/79506.
	if (Cards[Card].PriceText) { Cards[Card].PriceText->SetVisibility(ESlateVisibility::Collapsed); }
	// ★★★ Блок цены 79500+10k — 1:1 `CControl_SetVisible(pThis[13*slot + 665], bVisible)`.
	ShowPriceBlock(Card, bVisible);
	if (!bVisible) { ShowCardSelection(Card, false); }
}

// ★★★ ВКЛЮЧЕНИЕ БЛОКА ЦЕНЫ КАРТОЧКИ (79500+10k со всем поддеревом 79501…79506).
//   Эталон: `uiItemListSlot_Populate@0xC3A200` внутри `if (ItemRecord)` зовёт
//   `CControl_SetVisible(pThis[13*slotIndex + 665], 1u)`; поле +665 привязано в
//   `CShellItemStore_InitControls@0xC28820` как `FindControlByID(10*k + 79500)`.
//   ⚠ В `.JUI` контрол 79500 объявлен `Visible = "0"` — блок ВСЕГДА включается кодом, и без
//   этого вызова цены не видно ни в оригинале, ни у нас (наследование видимости доказано
//   `CControl_RenderRecursive`, ui_core.c:108944).
//   ХИТ-ТЕСТ: корню блока даём `HitTestInvisible`, а не `SelfHitTestInvisible` — блок цены
//   геометрически НАЛЕЗАЕТ на кнопку покупки 6859+6k (блок x 72…154, кнопка x 88…210 внутри
//   карточки 212×90), и его дети съедали бы её клики (закон `selfhittest-vs-hittest-invisible`).
void UAsdaEquipShopSubsystem::ShowPriceBlock(int32 Card, bool bVisible)
{
	if (!Cards.IsValidIndex(Card)) { return; }
	const FAsdaEquipShopCardUi& C = Cards[Card];
	for (int32 i = 0; i < C.PriceBlock.Num(); ++i)
	{
		UWidget* W = C.PriceBlock[i].Get();
		if (!W) { continue; }
		if (!bVisible) { W->SetVisibility(ESlateVisibility::Collapsed); continue; }
		W->SetVisibility(i == 0 ? ESlateVisibility::HitTestInvisible : ESlateVisibility::SelfHitTestInvisible);
	}
}

// ★★★ ВЫСОТА РАМКИ ВИТРИНЫ 6983 — 1:1 `shopWin_Open@0xC37110` (shop_trade.c:3864-3872):
//     `h = 98 * (uiList_GetVisibleRows(ширинаЭкрана) / 3) + 48`
//   (эталон читает ширину через `CConfig_GetWidth(&g_hMainWnd)`, у нас — `PanelRows()`).
//   ЗАЧЕМ: высота САМОГО окна в `.JUI` задана как `AlignYSize = "32 0 99"` = 99 % высоты экрана,
//   а нижняя панель списка (57121) прибита к низу окна (`Origin.y = -208`). Без подгонки рамки
//   на разрешениях выше 768 между витриной и списком открывается полоса ГОЛОГО ТЕЛА ОКНА —
//   центрального плана скина `Win_Mg`, а он в атласе `n_01` сплошной непрозрачный ЧЁРНЫЙ
//   (пиксели 480…483 × 439…442 = RGBA 0,0,0,255). Это и есть «чёрная подложка вокруг списка».
//   КАК: иерархия `.JUI` у нас расплющена, рамка живёт девятью плоскими планами 3×3
//   (`Shellitem_Store2_Win_Part2_15_0…8`). Меняем ровно то, что меняет ресайз 9-slice:
//   средний ряд (планы 3,4,5) тянем по высоте, нижний ряд (6,7,8) опускаем. Верхний ряд и все
//   колонки не трогаем — ширина окна не меняется.
void UAsdaEquipShopSubsystem::ApplyPanelHeight(int32 VisibleCards)
{
	if (CVarEquipShopLegacyPanelHeight.GetValueOnGameThread() != 0) { return; }

	// ★★★ РАМКА СЧИТАЕТСЯ ПО ФАКТИЧЕСКИ ПОКАЗАННЫМ КАРТОЧКАМ, а не только по таблице разрешений.
	//   ДЕФЕКТ (жалоба владельца 2026-08-26: «не растягивается белая рамка после второго ряда»):
	//   `PanelRows()` берёт число рядов из ТАБЛИЦЫ РАЗРЕШЕНИЙ эталона, а карточки раскладываются
	//   по страницам, посчитанным ИМПОРТЁРОМ. На разрешении, которого нет в таблице, `PanelRows()`
	//   отдаёт 6 (две строки), а на экране рисуется 9 карточек — третий ряд оказывался ВНЕ рамки.
	//   Берём максимум из двух величин: эталонную раскладку не ломаем, но рамка гарантированно
	//   накрывает всё, что реально показано.
	// ★ Волна 3: проверка 9 планов, расчёт рядов и перекладка рамки 3×3 — общая точка `AsdaShopUi::ApplyShowcaseHeight`
	//   (порядок прежний: планы → ряды по разрешению → «высота не изменилась» → слоты → перекладка).
	AsdaShopUi::FShowcaseHeight H;
	if (!AsdaShopUi::ApplyShowcaseHeight(PanelPlanes, [this]() { return PanelRows(); }, VisibleCards, PanelHeightApplied, H)) { return; }
	UE_LOG(LogAsdaEquipShop, Log,
		TEXT("[магазин снаряжения] рамка витрины 6983: по разрешению %d, показано карточек %d ⇒ ")
		TEXT("рядов %d, высота %d px"),
		H.ByRes, H.Shown, H.Rows, H.NewH);
}

void UAsdaEquipShopSubsystem::ShowCardSelection(int32 Card, bool bVisible)
{
	if (!Cards.IsValidIndex(Card)) { return; }
	AsdaShopUi::SetSelectionVisible(Cards[Card].SelPlanes, bVisible);   // ★ волна 3: общая точка рамки выбора
}

// `shopRefreshSlotList@0xC376B0`: взять максимум страниц из дескриптора активного набора,
//   отобрать записи текущей страницы, спрятать все 21 карточку и заполнить занятые.
void UAsdaEquipShopSubsystem::BuildPage()
{
	CardDbl.Reset();   // [ПОРТ] 10.10: страница перестроена — под тем же номером карточки другой товар
	const FAsdaEquipShopList& L = ActiveList();
	MaxPage = L.MaxPage;
	Page    = FMath::Clamp(Page, 0, MaxPage);

	for (int32 i = 0; i < kCards; ++i) { Cards[i].ItemCode = 0; ShowCard(i, false); }

	int32 Slot = 0;
	for (const FAsdaEquipShopEntry& E : L.Items)
	{
		if (E.Page != Page) { continue; }
		if (Slot >= kCards) { break; }        // эталон обрывает набор на 21-й записи
		PopulateCard(Slot, E.ItemCode);
		++Slot;
	}
	// Подсветка выбранной карточки живёт отдельно от заполнения (`uiTabPanel_SelectTab@0xC270E0`).
	for (int32 i = 0; i < kCards; ++i) { ShowCardSelection(i, SelCard == i && Cards[i].ItemCode > 0); }
	if (IconGrid) { IconGrid->Refresh(); }
	// ★ Рамку витрины подгоняем ЗДЕСЬ: только после заполнения известно, сколько карточек реально
	//   показано на текущей странице (см. `ApplyPanelHeight`).
	ApplyPanelHeight(Slot);
	RefreshPageLabels();
}

// `uiItemListSlot_Populate@0xC3A200` — заполнение одной карточки.
void UAsdaEquipShopSubsystem::PopulateCard(int32 Card, int32 ItemCode)
{
	if (!Cards.IsValidIndex(Card)) { return; }
	FAsdaEquipShopCardUi& C = Cards[Card];
	const UAsdaItemTableSubsystem* IT = GetGameInstance() ? GetGameInstance()->GetSubsystem<UAsdaItemTableSubsystem>() : nullptr;
	const FAsdaItemRecord* R = IT ? IT->GetItem(ItemCode) : nullptr;
	if (!R) { return; }                      // нет записи ⇒ карточка остаётся скрытой (эталон)

	C.ItemCode = ItemCode;
	ShowCard(Card, true);

	FString Name = IT->GetItemName(ItemCode);
	if (Name.IsEmpty()) { Name = FString::FromInt(ItemCode); }
	if (C.NameText)   { C.NameText->SetText(FText::FromString(Name)); }
	if (C.PriceText)  { C.PriceText->SetText(FText::FromString(AsdaUi::FormatThousands(R->Price))); }
	if (C.BuyCaption) { const FString S = Loc(EquipShopLoc::kCardBuyCap); if (!S.IsEmpty()) { C.BuyCaption->SetText(FText::FromString(S)); } }

	auto Put = [&C](int32 Idx1Based, const FString& S)
	{
		const int32 I = Idx1Based - 1;
		if (C.PriceTexts.IsValidIndex(I) && C.PriceTexts[I]) { C.PriceTexts[I]->SetText(FText::FromString(S)); }
	};
	// 79501 «Цена», 79502 = базовая цена, 79505 «Цена» (loc 2286), 79506 = итог.
	Put(1, Loc(EquipShopLoc::kPriceLabel));
	Put(2, AsdaUi::FormatThousands(R->Price));
	Put(5, Loc(EquipShopLoc::kNetPriceLabel));

	// ── Блок налога войны (заинлайненный `uniconWar_ApplyOptionPercent@0xBCEEC0`) ──
	// ★ ЭТАЛОННЫЙ ОБРАЗЕЦ ПОВЕДЕНИЯ (доказано `fix_helpers_dedup.md`): ветку «Owner == 3» отдельной
	//   от «Self == -1» держит ИМЕННО ЭТА копия — на ничьём городе итог = `Price + Tax`, гасятся
	//   только подписи 79503/79504. Именованные `ApplyWarPercent` теперь считают цену ТАК ЖЕ.
	//   Инлайн НЕ сведён в `ApplyWarPercent`, т.к. дополнительно строит три подписи и «-»-маркеры
	//   и для этого читает сырой `Owner` (общая точка его не отдаёт) — осмысленное отличие.
	UGameInstance* GI = GetGameInstance();
	const int32 Slot = AsdaUi::WarCitySlot(GI);
	if (Slot < 0)
	{
		Put(3, TEXT("-"));
		Put(4, TEXT("-"));
		// ★ Д2: эталон поле 79506 здесь НЕ трогает (см. CVar в шапке файла).
		Put(6, CVarEquipShopBlankNetPrice.GetValueOnGameThread() != 0
			? FString() : AsdaUi::FormatThousands(R->Price));
		return;
	}
	const UAsdaNetSubsystem*   Net = GI ? GI->GetSubsystem<UAsdaNetSubsystem>()   : nullptr;
	const UAsdaWorldSubsystem* WS  = GI ? GI->GetSubsystem<UAsdaWorldSubsystem>() : nullptr;
	const int32 Owner = (Net && Net->HasCityOwnerInfo()) ? Net->GetCityOwnerFaction(Slot) : -1;
	const int32 Pct   = (Net && Net->HasCityOwnerInfo()) ? Net->GetCityTaxPercent(Slot)   : 0;
	const int32 Self  = WS ? WS->GetSelfCampId() : -1;
	if (Self == Owner)
	{
		const int32 Tax = (int32)((double)R->Price / 100.0 * (double)Pct);
		Put(4, Tax ? FString::Printf(TEXT("-%s"), *AsdaUi::FormatThousands(Tax)) : AsdaUi::FormatThousands(0));
		Put(6, AsdaUi::FormatThousands(R->Price - Tax));
		Put(3, AsdaUi::FormatPrintfArgs(Loc(EquipShopLoc::kWarDiscount), { FString::FromInt(Pct) }));
	}
	else
	{
		int32 Tax = (int32)((double)R->Price / 100.0 * (double)Pct);
		if (Self == -1) { Tax = 0; }
		Put(4, FString::Printf(TEXT("+%s"), *AsdaUi::FormatThousands(Tax)));
		Put(6, AsdaUi::FormatThousands(R->Price + Tax));
		Put(3, AsdaUi::FormatPrintfArgs(Loc(EquipShopLoc::kWarSurcharge), { FString::FromInt(Pct) }));
		if (Owner == 3 || Self == -1) { Put(3, TEXT("-")); Put(4, TEXT("-")); }
	}
}

// `tradeWinUpdatePageLabels@0xC37BC0`: «страница+1» и «максимум+1».
void UAsdaEquipShopSubsystem::RefreshPageLabels()
{
	// ★ ИСПРАВЛЕНИЕ 11.10.2026 (дедуп №2, В2-A, U10e): было `FText::AsNumber` (разделитель разрядов по культуре: EN «1,234», RU «1 234») — стало `FString::FromInt`, как `sprintf("%d")` эталона (shop_trade.c:4143-4144).
	if (TxtPage)  { TxtPage->SetText(FText::FromString(FString::FromInt(Page + 1))); }
	if (TxtPages) { TxtPages->SetText(FText::FromString(FString::FromInt(MaxPage + 1))); }
}

void UAsdaEquipShopSubsystem::RefreshGoldText()
{
	const UAsdaInventorySubsystem* Inv = GetGameInstance() ? GetGameInstance()->GetSubsystem<UAsdaInventorySubsystem>() : nullptr;
	if (TxtGold) { TxtGold->SetText(FText::FromString(AsdaUi::FormatThousands(Inv ? Inv->GetGold() : 0))); }
	// ★ Д5: эталон пишет сюда деньги и даже зовёт `SetVisible(6850, 1)`, но родитель 7001 в `.JUI`
	//   скрыт, поэтому в оригинале блок не видно вовсе. Показываем всю ветку (откат — CVar).
	const bool bLegacyHidden = CVarEquipShopHiddenGold.GetValueOnGameThread() != 0;
	for (const TObjectPtr<UWidget>& W : GoldBlock)
	{
		AsdaUi::SetShownDecor(W, !bLegacyHidden);
	}
}

// ═════════════════════════════ «СПИСОК СНАРЯЖЕНИЯ» (7 строк) ══════════════════════════════════

int32 UAsdaEquipShopSubsystem::CategoryFromItemType(int32 ItemType)
{
	switch (ItemType)
	{
	case 0: return 8;  case 1: return 2;  case 2: return 4;
	case 3: return 5;  case 4: return 3;  case 8: return 7;  case 9: return 6;
	default: return -1;
	}
}

// `charEquip_SumOptionStats@0xC36A60`: сумма цен превью по слотам 0,1,2,3,4,8,9 с налогом войны.
int32 UAsdaEquipShopSubsystem::WearTotal() const
{
	static const int32 kSlots[] = { 0, 1, 2, 3, 4, 8, 9 };
	const UAsdaItemTableSubsystem* IT = GetGameInstance() ? GetGameInstance()->GetSubsystem<UAsdaItemTableSubsystem>() : nullptr;
	int32 Sum = 0;
	for (int32 S : kSlots)
	{
		if (!PreviewEquip.IsValidIndex(S) || PreviewEquip[S] <= 0) { continue; }
		if (const FAsdaItemRecord* R = IT ? IT->GetItem(PreviewEquip[S]) : nullptr)
		{
			Sum += ApplyWarPercent(R->Price);
		}
	}
	return Sum;
}

// `shopRefreshSellSlotList@0xC3ACB0`: собрать 7 строк из превью надетого, сохранив галочки.
void UAsdaEquipShopSubsystem::RefreshWearList()
{
	const UAsdaItemTableSubsystem* IT = GetGameInstance() ? GetGameInstance()->GetSubsystem<UAsdaItemTableSubsystem>() : nullptr;

	// Прежние галочки — по коду предмета (эталон сопоставляет ровно так).
	TArray<FAsdaEquipShopWearRow> Saved = WearRows;
	for (FAsdaEquipShopWearRow& R : WearRows) { R = FAsdaEquipShopWearRow(); }

	int32 Out = 0;
	for (int32 S = 0; S < kEquipSlots && Out < kWearRows; ++S)
	{
		if (!PreviewEquip.IsValidIndex(S) || PreviewEquip[S] <= 0) { continue; }
		const FAsdaItemRecord* R = IT ? IT->GetItem(PreviewEquip[S]) : nullptr;
		WearRows[Out].ItemCode = PreviewEquip[S];
		WearRows[Out].Category = R ? CategoryFromItemType(R->ItemType) : -1;
		WearRows[Out].bChecked = false;
		for (const FAsdaEquipShopWearRow& Old : Saved)
		{
			if (Old.ItemCode == WearRows[Out].ItemCode) { WearRows[Out].bChecked = Old.bChecked; }
		}
		++Out;
	}

	// Отрисовка: гасим все, потом рисуем занятые подряд.
	for (int32 j = 0; j < kWearRows; ++j)
	{
		FAsdaEquipShopWearUi& W = WearUi[j];
		if (W.NameTxt)  { W.NameTxt->SetText(FText::GetEmpty()); }
		if (W.PriceTxt) { W.PriceTxt->SetText(FText::GetEmpty()); }
		// ★ Гасим ВСЁ поддерево строки, а не четыре точечные ссылки: иначе на пустых строках
		//   остаются планы рамки/подложки — те самые «обводки» (см. сбор `W.Parts`).
		for (const TObjectPtr<UWidget>& Wd : W.Parts) { if (Wd) { Wd->SetVisibility(ESlateVisibility::Collapsed); } }
		if (W.Row)      { W.Row->SetVisibility(ESlateVisibility::Collapsed); }
		if (W.GoldIcon) { W.GoldIcon->SetVisibility(ESlateVisibility::Collapsed); }
		if (W.CheckOn)  { W.CheckOn->SetVisibility(ESlateVisibility::Collapsed); }
		if (W.CheckOff) { W.CheckOff->SetVisibility(ESlateVisibility::Collapsed); }
	}
	for (int32 j = 0; j < kWearRows; ++j)
	{
		if (WearRows[j].ItemCode <= 0) { continue; }
		const FAsdaItemRecord* R = IT ? IT->GetItem(WearRows[j].ItemCode) : nullptr;
		if (!R) { continue; }
		FAsdaEquipShopWearUi& W = WearUi[j];
		FString Name = IT->GetItemName(WearRows[j].ItemCode);
		if (Name.IsEmpty()) { Name = FString::FromInt(WearRows[j].ItemCode); }
		if (W.NameTxt)  { W.NameTxt->SetText(FText::FromString(Name)); }
		if (W.PriceTxt) { W.PriceTxt->SetText(FText::FromString(AsdaUi::FormatThousands(ApplyWarPercent(R->Price)))); }
		// ★ Показ строки: сперва поднимаем ВСЁ её поддерево (мы его целиком гасили выше),
		//   затем точечно уточняем режимы у кнопки-фона и галок.
		for (const TObjectPtr<UWidget>& Wd : W.Parts)
		{
			if (Wd) { Wd->SetVisibility(ESlateVisibility::SelfHitTestInvisible); }
		}
		if (W.Row)      { W.Row->SetVisibility(ESlateVisibility::Visible); }
		if (W.GoldIcon) { W.GoldIcon->SetVisibility(ESlateVisibility::HitTestInvisible); }
		AsdaUi::SetShownNoHit(W.CheckOn, WearRows[j].bChecked);
		AsdaUi::SetShownNoHit(W.CheckOff, !WearRows[j].bChecked);
	}
	if (TxtTotal) { TxtTotal->SetText(FText::FromString(AsdaUi::FormatThousands(WearTotal()))); }
}

// `uiSevenSlotPanelRefresh@0xC3B930` — кнопка 6848: ОТМЕЧЕННЫЕ строки выбрасываются из списка,
//   оставшиеся ужимаются вверх, сумма пересчитывается.
void UAsdaEquipShopSubsystem::CompactWearList()
{
	const UAsdaItemTableSubsystem* IT = GetGameInstance() ? GetGameInstance()->GetSubsystem<UAsdaItemTableSubsystem>() : nullptr;
	TArray<FAsdaEquipShopWearRow> Kept;
	for (const FAsdaEquipShopWearRow& R : WearRows)
	{
		if (R.ItemCode > 0 && !R.bChecked) { Kept.Add(R); }
	}
	// ★ Отмеченные вещи «снимаются» и с ПРЕВЬЮ — иначе следующая перерисовка списка вернула бы их
	//   (эталон держит список и превью раздельно, и после 6848 они у него расходятся до ближайшего
	//   `shopRefreshSellSlotList`; у нас список строится ИЗ превью, поэтому снимаем сразу).
	for (const FAsdaEquipShopWearRow& R : WearRows)
	{
		if (R.ItemCode > 0 && R.bChecked)
		{
			for (int32 S = 0; S < kEquipSlots; ++S)
			{
				if (PreviewEquip.IsValidIndex(S) && PreviewEquip[S] == R.ItemCode) { PreviewEquip[S] = -1; }
			}
		}
	}
	for (int32 j = 0; j < kWearRows; ++j)
	{
		WearRows[j] = Kept.IsValidIndex(j) ? Kept[j] : FAsdaEquipShopWearRow();
	}
	ApplyPreviewModels(/*bClosing*/false);

	// Отрисовка строк (цена — с налогом; ★ Д3, см. CVar в шапке файла).
	const bool bLegacyPrice = CVarEquipShopCompactPrice.GetValueOnGameThread() != 0;
	for (int32 j = 0; j < kWearRows; ++j)
	{
		FAsdaEquipShopWearUi& W = WearUi[j];
		const FAsdaEquipShopWearRow& R = WearRows[j];
		const FAsdaItemRecord* Rec = (R.ItemCode > 0 && IT) ? IT->GetItem(R.ItemCode) : nullptr;
		if (!Rec)
		{
			if (W.NameTxt)  { W.NameTxt->SetText(FText::GetEmpty()); }
			if (W.PriceTxt) { W.PriceTxt->SetText(FText::GetEmpty()); }
			if (W.Row)      { W.Row->SetVisibility(ESlateVisibility::Collapsed); }
			if (W.GoldIcon) { W.GoldIcon->SetVisibility(ESlateVisibility::Collapsed); }
			if (W.CheckOn)  { W.CheckOn->SetVisibility(ESlateVisibility::Collapsed); }
			if (W.CheckOff) { W.CheckOff->SetVisibility(ESlateVisibility::Collapsed); }
			continue;
		}
		FString Name = IT->GetItemName(R.ItemCode);
		if (Name.IsEmpty()) { Name = FString::FromInt(R.ItemCode); }
		const int32 Shown = bLegacyPrice ? Rec->Price : ApplyWarPercent(Rec->Price);
		if (W.NameTxt)  { W.NameTxt->SetText(FText::FromString(Name)); }
		if (W.PriceTxt) { W.PriceTxt->SetText(FText::FromString(AsdaUi::FormatThousands(Shown))); }
		if (W.Row)      { W.Row->SetVisibility(ESlateVisibility::Visible); }
		if (W.GoldIcon) { W.GoldIcon->SetVisibility(ESlateVisibility::HitTestInvisible); }
		if (W.CheckOn)  { W.CheckOn->SetVisibility(ESlateVisibility::Collapsed); }
		if (W.CheckOff) { W.CheckOff->SetVisibility(ESlateVisibility::HitTestInvisible); }
	}
	if (TxtTotal) { TxtTotal->SetText(FText::FromString(AsdaUi::FormatThousands(WearTotal()))); }
	// Подсветка карточек снимается (`uiInventory_HideSlots@0xC3DC40`).
	//   ★ ДЕФЕКТ ОРИГИНАЛА: та функция гасит рамку выбора только у ПЕРВЫХ СЕМИ карточек из 21
	//   (цикл `i < 7` по массиву из 21 элемента). Гасим у всех — иначе рамка «залипала» бы.
	SelCard = -1;
	for (int32 i = 0; i < kCards; ++i) { ShowCardSelection(i, false); }
}

// ═════════════════════════════ ПРИМЕРКА ═══════════════════════════════════════════════════════

// `invCopyFieldsFromBagClass@0xC27E90` — снимок реально надетого (11 слотов контейнера 3).
//   ★★★ ВАЖНО: массив ПРЕВЬЮ (`this[2496+i]`) при этом ОБНУЛЯЕТСЯ (`invResetSlotArrays@0xC27E20`
//   ставит −1 обоим массивам, а заполняется только «реальный» `this[2485+i]`). Поэтому при
//   открытии «Список снаряжения» ПУСТ и «Итого» = 0 — вещи попадают в него только примеркой.
void UAsdaEquipShopSubsystem::SnapshotEquip()
{
	RealEquip.Init(-1, kEquipSlots);
	const UAsdaInventorySubsystem* Inv = GetGameInstance() ? GetGameInstance()->GetSubsystem<UAsdaInventorySubsystem>() : nullptr;
	if (Inv)
	{
		for (int32 S = 0; S < kEquipSlots; ++S)
		{
			const FAsdaItemInstance* C = Inv->GetCell(3, S);
			RealEquip[S] = (C && !C->IsEmpty()) ? C->ItemCode : -1;
		}
	}
	PreviewEquip.Init(-1, kEquipSlots);
	PreviewApplied.Init(-1, kEquipSlots);
}

// `itemOnUseDispatch@0xC267E0` (ветка карточек) + `charEquipApplyPartsModelByType@0xC280B0`
//   + `invItem_RemoveIfType9@0xC28130` (снятие конфликтующей пары «двуручное оружие ↔ щит»).
void UAsdaEquipShopSubsystem::PreviewItem(int32 ItemCode)
{
	if (ItemCode <= 0) { return; }
	const UAsdaItemTableSubsystem* IT = GetGameInstance() ? GetGameInstance()->GetSubsystem<UAsdaItemTableSubsystem>() : nullptr;
	const FAsdaItemRecord* R = IT ? IT->GetItem(ItemCode) : nullptr;
	if (!R) { return; }
	const int32 Type = R->ItemType;
	if (CategoryFromItemType(Type) < 0) { return; }   // `itemSlotKindFromType@0xC234B0` вернул −1
	if (!PreviewEquip.IsValidIndex(Type)) { return; }
	// `equipPart_MatchIdByType@0xC27F70`: этот же предмет уже в слоте ⇒ эталон НИЧЕГО не делает
	//   (повторный клик по карточке не снимает вещь).
	if (PreviewEquip[Type] == ItemCode) { return; }

	// Пара «двуручное оружие ↔ щит»: подтипы двуручного оружия — {2,3,5,8,9,10,11,12,13,14}.
	auto IsTwoHanded = [IT](int32 Code) -> bool
	{
		const FAsdaItemRecord* W = (Code > 0 && IT) ? IT->GetItem(Code) : nullptr;
		if (!W || W->ItemType != 9) { return false; }
		const int32 S = W->ItemSubType;
		return S == 2 || S == 3 || S == 5 || (S >= 8 && S <= 14);
	};
	if (Type == 9 && IsTwoHanded(ItemCode))          { PreviewEquip[8] = -1; }   // взяли двуручное — щит долой
	else if (Type == 8 && IsTwoHanded(PreviewEquip.IsValidIndex(9) ? PreviewEquip[9] : -1)) { PreviewEquip[9] = -1; }

	PreviewEquip[Type] = ItemCode;
	ApplyPreviewModels(/*bClosing*/false);
}

// `charApplyEquipPartsModels@0xC28440` + `charEquipApplyPartsModelByType@0xC280B0`.
//   Что стоит на модели для части T:
//     • пока окно ОТКРЫТО (`bClosing == false`, эталонный режим 1): примеренное
//       (`PreviewEquip[T]`), а если ничего не примерено — РЕАЛЬНОЕ надетое, причём ОРУЖИЕ и ЩИТ
//       эталон в этом режиме снимает (`SetPartsModelByType(…, 6, 0)` и `(…, 7, 0)`);
//     • при ЗАКРЫТИИ (`bClosing == true`, режим 0) — целиком реальное надетое, включая оружие и щит.
void UAsdaEquipShopSubsystem::ApplyPreviewModels(bool bClosing)
{
	if (CVarEquipShopPreview.GetValueOnGameThread() == 0) { return; }
	UAsdaWorldSubsystem* WS = GetGameInstance() ? GetGameInstance()->GetSubsystem<UAsdaWorldSubsystem>() : nullptr;
	if (!WS) { return; }
	// Эталон трогает части 0…4 (шлем/грудь/штаны/обувь/перчатки), 8 щит, 9 оружие.
	static const int32 kParts[] = { 0, 1, 2, 3, 4, 8, 9 };
	if (PreviewApplied.Num() != kEquipSlots) { PreviewApplied.Init(-1, kEquipSlots); }
	TArray<FIntPoint> Batch;
	for (int32 T : kParts)
	{
		int32 Base = (RealEquip.IsValidIndex(T) && RealEquip[T] > 0) ? RealEquip[T] : 0;
		if (!bClosing && (T == 8 || T == 9)) { Base = 0; }
		const int32 Want = (!bClosing && PreviewEquip.IsValidIndex(T) && PreviewEquip[T] > 0)
			? PreviewEquip[T] : Base;
		if (PreviewApplied[T] == Want) { continue; }
		PreviewApplied[T] = Want;
		Batch.Emplace(T, Want);
	}
	// ★ ОДНИМ вызовом (см. `UAsdaWorldSubsystem::ApplySelfEquipParts`): поштучные вызовы
	//   пересинхронизировали бы снимок с контейнером 3 и стирали бы уже примеренные части.
	if (Batch.Num() > 0) { WS->ApplySelfEquipParts(Batch); }
}

// `tradeResetAllSlots@0xC3C390`, `memset(this + 2496, 0, 0x2C)` — обнулить массив превью.
void UAsdaEquipShopSubsystem::ClearPreview()
{
	PreviewEquip.Init(-1, kEquipSlots);
}

// ═════════════════════════════ ДЕЙСТВИЯ ═══════════════════════════════════════════════════════

void UAsdaEquipShopSubsystem::SetMenuTab(int32 Tab)
{
	// `shopWinSetCategory@0xC256C0`: смена вкладки сбрасывает страницу и подсветку.
	if (Tab < 0 || Tab >= kMenuTabs) { return; }
	MenuTab = Tab;
	if (MenuTab == MenuTabPrev) { return; }
	MenuTabPrev = MenuTab;
	Page = 0;
	SelCard = -1;
	// ★ СОСТОЯНИЕ «какая вкладка активна» — ОБЩАЯ ТОЧКА (закон `port-shared-points`).
	//   ★ У ЭТОГО окна скин вкладок ТЁМНЫЙ, а подписи — рантайм-`TextBlock` (`MakeLabelLike`), то есть
	//     ЧИСТО БЕЛЫЕ. Общая точка отрабатывает ЗЕРКАЛЬНЫЙ случай (правка 2026-08-27): активная
	//     остаётся БЕЛОЙ, а неактивные ПРИГЛУШАЮТСЯ — иначе при белом базовом цвете активную
	//     было бы не отличить. См. `TabCaptionActiveColor`/`TabCaptionInactiveColor`.
	AsdaUi::ApplyTabCaptionState(MenuLabels, MenuTab, MenuIdleColors);
	BuildPage();
}

void UAsdaEquipShopSubsystem::SetPage(int32 NewPage)
{
	// Эталон 6853: `if (page < maxPage) ++page;`  6855: `if (page > 0) --page;`
	Page = FMath::Clamp(NewPage, 0, MaxPage);
	BuildPage();
}

// `uiHandleToggleCommand@0xC250D0`: чекбоксы взаимоисключающие, страница сбрасывается в 0,
//   подсветка снимается.
void UAsdaEquipShopSubsystem::SetFilter(int32 Which, bool bOn)
{
	if (Which == 0)
	{
		bFilterWear = bOn;
		if (bFilterWear) { bFilterGender = false; if (ChkGender) { ChkGender->SetIsChecked(false); } }
	}
	else
	{
		bFilterGender = bOn;
		if (bFilterGender) { bFilterWear = false; if (ChkWear) { ChkWear->SetIsChecked(false); } }
	}
	Page = 0;
	SelCard = -1;
	BuildPage();
	UE_LOG(LogAsdaEquipShop, Log, TEXT("[магазин снаряжения] фильтр: одежда=%s пол=%s, предметов %d"),
		bFilterWear ? TEXT("да") : TEXT("нет"), bFilterGender ? TEXT("да") : TEXT("нет"), ActiveList().Items.Num());
}

// Клик по карточке (не по её кнопке) — ПРИМЕРКА + подсветка + перестроение списка.
// [ПОРТ] 10.10.2026: первый щелчок пары — только подсветка карточки (рамка выбора эталона 10734), без примерки.
void UAsdaEquipShopSubsystem::OnCardSelectOnly(int32 Card)
{
	if (!Cards.IsValidIndex(Card) || Cards[Card].ItemCode <= 0) { return; }
	SelCard = Card;
	for (int32 i = 0; i < kCards; ++i) { ShowCardSelection(i, i == Card); }
}

void UAsdaEquipShopSubsystem::OnCardPreviewClicked(int32 Card)
{
	if (!Cards.IsValidIndex(Card) || Cards[Card].ItemCode <= 0) { return; }
	SelCard = Card;
	PreviewItem(Cards[Card].ItemCode);
	for (int32 i = 0; i < kCards; ++i) { ShowCardSelection(i, i == Card); }
	RefreshWearList();
}

// Кнопка карточки 6859+6k (`uiHandleCommand@0xC23AC0`, LABEL_33): запомнить предмет и показать
//   двухкнопочную модалку с текстом loc 20060.
void UAsdaEquipShopSubsystem::OnCardBuyClicked(int32 Card)
{
	if (!Cards.IsValidIndex(Card) || Cards[Card].ItemCode <= 0) { return; }
	SelCard = Card;
	PendingItemCode = Cards[Card].ItemCode;
	const UAsdaTextSubsystem* TS = GetGameInstance() ? GetGameInstance()->GetSubsystem<UAsdaTextSubsystem>() : nullptr;
	const UAsdaItemTableSubsystem* IT = GetGameInstance() ? GetGameInstance()->GetSubsystem<UAsdaItemTableSubsystem>() : nullptr;
	FString Name = IT ? IT->GetItemName(PendingItemCode) : FString();
	if (Name.IsEmpty()) { Name = FString::FromInt(PendingItemCode); }
	const FString Text = TS ? TS->FormatById(EquipShopLoc::kConfirmOne, { Name }, {}) : FString();
	UAsdaWindowSubsystem* WM = GetGameInstance() ? GetGameInstance()->GetSubsystem<UAsdaWindowSubsystem>() : nullptr;
	if (WM && !Text.IsEmpty())
	{
		AsdaEnterGuard::NoEnter(WM)->ShowGameConfirm(Text, [this](bool bYes) { if (bYes) { SendBuyOne(); } else { PendingItemCode = 0; } }, 0.f, 2288, 2289, /*очередь: эталон (1, 34) ui_core.c:25936*/34, 1);
	}
}

// Кнопка 6851 (`case 0x1AC3`): гейт «список не пуст» (`entry_HasAnyActiveOf7@0xC3D900`) + модалка 6889.
void UAsdaEquipShopSubsystem::OnBuyWornClicked()
{
	bool bAny = false;
	for (const FAsdaEquipShopWearRow& R : WearRows) { if (R.ItemCode > 0) { bAny = true; break; } }
	if (!bAny) { return; }                    // эталон просто ничего не делает, без нотиса
	const FString Text = Loc(EquipShopLoc::kConfirmWorn);
	UAsdaWindowSubsystem* WM = GetGameInstance() ? GetGameInstance()->GetSubsystem<UAsdaWindowSubsystem>() : nullptr;
	if (WM && !Text.IsEmpty())
	{
		AsdaEnterGuard::NoEnter(WM)->ShowGameConfirm(Text, [this](bool bYes) { if (bYes) { SendBuyWorn(); } }, 0.f, 2288, 2289, /*очередь: эталон (1, 35) ui_core.c:25839*/35, 1);
	}
}

// Кнопка 6848: гейт `uiInventory_HasActiveSlot@0xC3DBB0` («есть отмеченная галкой строка»).
void UAsdaEquipShopSubsystem::OnRemoveCheckedClicked()
{
	bool bAny = false;
	for (const FAsdaEquipShopWearRow& R : WearRows) { if (R.ItemCode > 0 && R.bChecked) { bAny = true; break; } }
	if (!bAny) { return; }
	CompactWearList();
}

// Кнопка 6849 (`tradeResetAllSlots@0xC3C390`): список очищается, модели возвращаются к реальным
//   БЕЗ оружия и щита, «Итого» = 0, нотис 6890 в чат.
void UAsdaEquipShopSubsystem::OnResetClicked()
{
	bool bAny = false;
	for (int32 S = 0; S < kEquipSlots; ++S)
	{
		// `invAreAllEquipSlotsEmpty@0xC33650` смотрит слоты 0,1,2,3,4,8,9 превью.
		if ((S <= 4 || S == 8 || S == 9) && PreviewEquip.IsValidIndex(S) && PreviewEquip[S] > 0) { bAny = true; break; }
	}
	if (!bAny) { return; }
	for (FAsdaEquipShopWearRow& R : WearRows) { R = FAsdaEquipShopWearRow(); }
	ClearPreview();
	ApplyPreviewModels(/*bClosing*/false);
	RefreshWearList();
	if (TxtTotal) { TxtTotal->SetText(FText::FromString(TEXT("0"))); }
	SelCard = -1;
	for (int32 i = 0; i < kCards; ++i) { ShowCardSelection(i, false); }
	NoticeChat(EquipShopLoc::kAllRemoved);
}

// Клик по строке списка 6812+5j (`uiSevenSlotPanel_ToggleSlot@0xC3B6E0`) — переключить галочку.
void UAsdaEquipShopSubsystem::OnWearRowClicked(int32 Row)
{
	if (!WearRows.IsValidIndex(Row) || WearRows[Row].ItemCode <= 0) { return; }
	const bool bNew = !WearRows[Row].bChecked;
	// Эталон переключает ВСЕ строки с тем же кодом предмета (цикл сравнения по `itemCode`).
	for (FAsdaEquipShopWearRow& R : WearRows)
	{
		if (R.ItemCode == WearRows[Row].ItemCode) { R.bChecked = bNew; }
	}
	for (int32 j = 0; j < kWearRows; ++j)
	{
		if (WearRows[j].ItemCode <= 0) { continue; }
		AsdaUi::SetShownNoHit(WearUi[j].CheckOn, WearRows[j].bChecked);
		AsdaUi::SetShownNoHit(WearUi[j].CheckOff, !WearRows[j].bChecked);
	}
}

// ═════════════════════════════ ПОКАДРОВЫЕ ОПРОСЫ ══════════════════════════════════════════════

void UAsdaEquipShopSubsystem::TickCards()
{
	// ★ Наведение и клик ловим ПО ЖИВОЙ ГЕОМЕТРИИ (`AsdaUi::IsCursorOverWidget`): карточки в `.JUI`
	//   заданы ЦЕПОЧКОЙ вложенности, и вычисленные «по данным» клик-зоны уезжают на чужие панели
	//   (закон `jui-cell-chain-nesting`). Тот же приём, что в `UAsdaNpcShopSubsystem::TickDrag`.
	int32 OverCard = -1;      // курсор над телом карточки (бокс/иконка) — это ПРИМЕРКА
	int32 OverBuy  = -1;      // курсор над кнопкой покупки 6859+6k — клик у неё свой (реле кнопки)
	for (int32 i = 0; i < kCards; ++i)
	{
		if (Cards[i].ItemCode <= 0) { continue; }
		if (OverBuy < 0 && Cards[i].BuyButton && AsdaUi::IsCursorOverWidget(Cards[i].BuyButton.Get())) { OverBuy = i; }
		if (OverCard >= 0) { continue; }
		if (Cards[i].ItemIcon && AsdaUi::IsCursorOverWidget(Cards[i].ItemIcon.Get())) { OverCard = i; continue; }
		for (const TObjectPtr<UWidget>& W : Cards[i].BoxPlanes)
		{
			if (W && AsdaUi::IsCursorOverWidget(W.Get())) { OverCard = i; break; }
		}
	}
	// Строки «Списка снаряжения» — только для подсказки; клик у них свой (реле фон-кнопки).
	int32 OverRow = -1;
	for (int32 j = 0; j < kWearRows; ++j)
	{
		if (WearRows[j].ItemCode <= 0 || !WearUi[j].Row) { continue; }
		if (WearUi[j].Row->GetVisibility() == ESlateVisibility::Collapsed) { continue; }
		if (AsdaUi::IsCursorOverWidget(WearUi[j].Row.Get())) { OverRow = j; break; }
	}

	// Подсказка предмета — НАША ДОБАВКА (в оригинале её у этого окна нет, см. CVar выше).
	//   Над кнопкой покупки показываем подсказку ЕЁ карточки: кнопка — часть карточки.
	TickTooltip((OverBuy >= 0) ? OverBuy : OverCard, OverRow);

	if (!Mouse.bLmbDown) { return; }
	// Кнопка покупки перехватывает нажатие целиком (иначе примерка сработала бы вместе с ней).
	if (OverBuy >= 0) { CardDbl.Reset(); return; }
	// ★ [ПОРТ] ТРЕБОВАНИЕ ВЛАДЕЛЬЦА 10.10.2026 «в корзину — двойным щелчком». У этого окна «корзина» —
	//   «Список снаряжения»: эталон перестраивает его из того, что ПРИМЕРЕНО (`shopRefreshSellSlotList`,
	//   shop_trade.c:5093, по 0xB `itemOnUseDispatch@0xC267E0`, item_inventory.c:19162-19203), а кнопка 6851
	//   покупает весь список. Эталон примеряет по ОДИНОЧНОМУ 0xB. Теперь одиночный — только рамка выбора,
	//   второй щелчок по той же карточке — примерка (= в список) ровно один раз.
	//   Откат: `asda.Shop.DblClickToCart 0` (каждый щелчок примеряет, как эталон).
	if (OverCard >= 0)
	{
		if (CardDbl.Click(OverCard)) { OnCardPreviewClicked(OverCard); }
		else                         { OnCardSelectOnly(OverCard); }
		return;
	}
	CardDbl.Reset();   // нажатие мимо карточек рвёт пару щелчков
}

// ★★★ НАША ДОБАВКА: подсказка предмета по наведению (владелец `EAsdaTipOwner::EquipShop`).
//   Приоритет — карточка витрины, затем строка «Списка снаряжения». Плашку строим ОБЩЕЙ ТОЧКОЙ
//   `UAsdaItemTooltipSubsystem::ShowItemTooltipByCode` (по коду предмета: у товара витрины
//   экземпляра в контейнере ещё нет), ровно как военный магазин
//   (`UAsdaWarShopSubsystem::TickCards`, эталонный аналог `uiEquip_OnSlotHoverTooltip@0xB778C0`).
//   Гасим ТОЛЬКО свою плашку — `HideTooltipFor(EquipShop)` (закон `ui-shared-tip-ownership`).
void UAsdaEquipShopSubsystem::TickTooltip(int32 OverCard, int32 OverRow)
{
	UAsdaItemTooltipSubsystem* Tip = GetGameInstance() ? GetGameInstance()->GetSubsystem<UAsdaItemTooltipSubsystem>() : nullptr;
	if (!Tip) { return; }
	if (CVarEquipShopTooltips.GetValueOnGameThread() == 0)
	{
		if (TipCard >= 0) { Tip->HideTooltipFor(EAsdaTipOwner::EquipShop); TipCard = -1; }
		return;
	}
	// Ключ «что показано»: 0…20 — карточка витрины, 100+j — строка списка, −1 — ничего.
	int32 Target = -1;
	int32 Code   = 0;
	if (Cards.IsValidIndex(OverCard) && Cards[OverCard].ItemCode > 0)
	{
		Target = OverCard;
		Code   = Cards[OverCard].ItemCode;
	}
	else if (WearRows.IsValidIndex(OverRow) && WearRows[OverRow].ItemCode > 0)
	{
		Target = 100 + OverRow;
		Code   = WearRows[OverRow].ItemCode;
	}
	if (Target == TipCard) { return; }          // плашка уже про этот предмет — не перестраиваем
	TipCard = Target;
	if (Target < 0) { Tip->HideTooltipFor(EAsdaTipOwner::EquipShop); return; }
	Tip->ShowItemTooltipByCode(Code, EAsdaTipOwner::EquipShop);
}

// ═════════════════════════════ СЕТЬ ═══════════════════════════════════════════════════════════

// `Anticheat_sendSecuredReport_5020@0xC3C770`: тело 42 б, ЗАПОЛНЕНА ТОЛЬКО ПЕРВАЯ запись
//   {выбранный предмет, amount = −1}; остальные шесть — нули.
void UAsdaEquipShopSubsystem::SendBuyOne()
{
	if (bPending || PendingItemCode <= 0) { return; }
	UAsdaNetSubsystem* Net = GetGameInstance() ? GetGameInstance()->GetSubsystem<UAsdaNetSubsystem>() : nullptr;
	if (!Net) { return; }
	TArray<int32> Codes; Codes.Add(PendingItemCode);
	TArray<int32> Qty;   Qty.Add(-1);
	PendingBuyCodes = Codes;
	if (Net->SendCharItemBuy5020(Codes, Qty))
	{
		bPending = true;
		UE_LOG(LogAsdaEquipShop, Log, TEXT("[магазин снаряжения] →5020 одна вещь: %d"), PendingItemCode);
	}
}

// `net_sendSecuredReport_5020@0xC3CD90`: до 7 записей из «Списка снаряжения», amount = −1.
void UAsdaEquipShopSubsystem::SendBuyWorn()
{
	if (bPending) { return; }
	UAsdaNetSubsystem* Net = GetGameInstance() ? GetGameInstance()->GetSubsystem<UAsdaNetSubsystem>() : nullptr;
	if (!Net) { return; }
	TArray<int32> Codes, Qty;
	for (const FAsdaEquipShopWearRow& R : WearRows)
	{
		if (R.ItemCode <= 0) { continue; }
		Codes.Add(R.ItemCode);
		Qty.Add(-1);
	}
	if (Codes.Num() == 0) { return; }
	PendingBuyCodes = Codes;
	if (Net->SendCharItemBuy5020(Codes, Qty))
	{
		bPending = true;
		UE_LOG(LogAsdaEquipShop, Log, TEXT("[магазин снаряжения] →5020 список: позиций %d, сумма %d"), Codes.Num(), WearTotal());
	}
}

void UAsdaEquipShopSubsystem::DrainPendingPackets()
{
	// ★ U7 (В1 дедупа №2): общая очередь `TAsdaNetQueue` (замок + Drain) вместо своего массива. Прежний цикл `while … [0]; RemoveAt(0)` давал тот же порядок: во время разбора новые тела в очередь
	//   не попадают (колбэк сети приходит из `UAsdaNetSubsystem::Tick`, не отсюда).
	if (TArray<TArray<uint8>> Local; PendingBuy.Drain(Local))
	{
		for (const TArray<uint8>& B : Local) { OnBuyResult(B); }
	}
}

// `Recv_ShopSell_Result_0@0xC3D430` — приём 5021 ПРИ ОТКРЫТОМ окне 6800.
void UAsdaEquipShopSubsystem::OnBuyResult(const TArray<uint8>& Body406)
{
	bPending = false;
	if (Body406.Num() < 406) { return; }
	const int32 Code = (int32)Body406[0];
	UGameInstance* GI = GetGameInstance();

	if (Code != 1)
	{
		const bool bSilent = CVarEquipShopSilentCodes.GetValueOnGameThread() != 0;
		switch (Code)
		{
		case 0: if (!bSilent) { NoticeMsgBox(EquipShopLoc::kResNoItem); }  break;   // ★ Д1
		case 2: NoticeMsgBox(EquipShopLoc::kResBagFull); break;
		case 3: NoticeMsgBox(EquipShopLoc::kResWeight);  break;
		case 4: NoticeMsgBox(EquipShopLoc::kResNoGold);  break;
		case 5: if (!bSilent) { NoticeMsgBox(EquipShopLoc::kResNotSold); } break;   // ★ Д1
		default: break;
		}
		PendingItemCode = 0;
		PendingBuyCodes.Reset();
		UE_LOG(LogAsdaEquipShop, Log, TEXT("[магазин снаряжения] 5021 отказ, код %d"), Code);
		return;
	}

	// Успех: записи в контейнеры, деньги и вес — ОДНОЙ точкой инвентаря (та же, что у окна 1300).
	if (UAsdaInventorySubsystem* Inv = GI ? GI->GetSubsystem<UAsdaInventorySubsystem>() : nullptr)
	{
		Inv->ApplyShopBuyRecords(Body406);
	}
	// По строке 20065 на каждую купленную позицию: «Ты покупаешь [ss1] за [ss2] золота.»
	//   Цена — с налогом войны, ровно как в эталоне (`uniconWar_ApplyOptionPercent(price, 0)`).
	const UAsdaItemTableSubsystem* IT = GI ? GI->GetSubsystem<UAsdaItemTableSubsystem>() : nullptr;
	const UAsdaTextSubsystem*      TS = GI ? GI->GetSubsystem<UAsdaTextSubsystem>() : nullptr;
	if (UAsdaMessageSubsystem* Msg = GI ? GI->GetSubsystem<UAsdaMessageSubsystem>() : nullptr)
	{
		for (int32 C : PendingBuyCodes)
		{
			const FAsdaItemRecord* R = IT ? IT->GetItem(C) : nullptr;
			FString Name = IT ? IT->GetItemName(C) : FString();
			if (Name.IsEmpty()) { Name = FString::FromInt(C); }
			const FString PriceStr = AsdaUi::FormatThousands(R ? ApplyWarPercent(R->Price) : 0);
			const FString Line = TS ? TS->FormatById(EquipShopLoc::kBoughtLine, { Name, PriceStr }, {}) : FString();
			if (!Line.IsEmpty()) { Msg->PostText(EAsdaMsgCategory::Trade, Line); }
		}
	}
	// ★ 06.11.2026 [ПОРТ]: та же сделка с NPC за золото, что в окне 1300, — тот же звук (ключ `asda.Shop.TradeSound`).
	if (PendingBuyCodes.Num() > 0)
	{
		if (const UAsdaNpcShopSubsystem* Npc = GI ? GI->GetSubsystem<UAsdaNpcShopSubsystem>() : nullptr) { Npc->PlayDealSound(PendingBuyCodes[0]); }
	}
	PendingBuyCodes.Reset();
	PendingItemCode = 0;

	// Модели возвращаются к реальным БЕЗ оружия и щита, список и «Итого» очищаются.
	ClearPreview();
	ApplyPreviewModels(/*bClosing*/false);
	for (FAsdaEquipShopWearRow& R : WearRows) { R = FAsdaEquipShopWearRow(); }
	RefreshWearList();
	if (TxtTotal) { TxtTotal->SetText(FText::FromString(TEXT("0"))); }
	RefreshGoldText();
	SelCard = -1;
	for (int32 i = 0; i < kCards; ++i) { ShowCardSelection(i, false); }
	UE_LOG(LogAsdaEquipShop, Log, TEXT("[магазин снаряжения] 5021 успех"));
}

// ═════════════════════════════ НОТИСЫ ═════════════════════════════════════════════════════════

FString UAsdaEquipShopSubsystem::Loc(int32 LocId) const
{
	// ★ Волна 1 (14.09.2026, В2): общая точка `AsdaNotice::Loc` — теги срезаны, маркеры как в таблице (как было).
	return AsdaNotice::Loc(GetGameInstance(), LocId);
}

// Эталон: `CChat_AddTokenizedLines(g_pChatWnd, текст, цвет, 10000, 0)` — обычная строка чата.
void UAsdaEquipShopSubsystem::NoticeChat(int32 LocId) const
{
	// ★ Волна 1 (14.09.2026, В2): маршрут — фасад `AsdaNotice` (категория та же, поведение то же).
	AsdaNotice::ChatText(GetGameInstance(), EAsdaMsgCategory::Trade, Loc(LocId));   // пустая строка не отправляется — как было
}

// Эталон: `CItem_BuildTooltipText(g_pItemTooltipUI, 0, −1, текст, 1)` = `ONEBUT_MSGBOX` 1500.
void UAsdaEquipShopSubsystem::NoticeMsgBox(int32 LocId) const
{
	// ★ Волна 1 (14.09.2026, В2): единый маршрут `AsdaNotice::MsgBox` — окно 1500, без оконной подсистемы —
	//   строка в ленту той же категорией. Категория/подпись кнопки — прежние.
	AsdaNotice::MsgBox(GetGameInstance(), EAsdaMsgCategory::Trade, Loc(LocId), 2288);
}

// `uniconWar_ApplyOptionPercent@0xBCEEC0` с `a2 = 0` — та же формула, что у магазина NPC.
int32 UAsdaEquipShopSubsystem::ApplyWarPercent(int32 Value) const
{
	UGameInstance* GI = GetGameInstance();
	const int32 Slot = AsdaUi::WarCitySlot(GI);
	if (Slot < 0) { return Value; }
	int32 Percent = 0; bool bNeutral = true; bool bNoFaction = true;
	const bool bOwn = AsdaUi::WarCityOwnedBySelf(GI, Slot, Percent, bNeutral, &bNoFaction);
	// ★ ИСПРАВЛЕНИЕ денежного дефекта (`fix_helpers_dedup.md`): обнуляем долю ТОЛЬКО при отсутствии
	//   фракции у игрока (`bNoFaction`). На ничьём городе (Owner == 3) эталон наценку ПРИМЕНЯЕТ —
	//   это подтверждает и инлайн `PopulateCard` (Put(6) = `Price + Tax`). Прежний `if (bNeutral)`
	//   терял наценку. Совпадает с `uniconWar_ApplyOptionPercent@0xBCEEC0` (a2 = 0).
	if (Percent <= 0 || bNoFaction) { return Value; }
	const int32 Delta = (int32)((double)Value / 100.0 * (double)Percent);
	if (bOwn) { return Value - Delta; }
	return Value + Delta;
}

// ═════════════════════════════ ДИАГНОСТИКА ════════════════════════════════════════════════════

FString UAsdaEquipShopSubsystem::DumpControls() const
{
	FString S = TEXT("=== магазин снаряжения 6800: разобранные контролы ===\n");
	auto Line = [&S](const TCHAR* Id, const UObject* W) { S += FString::Printf(TEXT("  %-34s %s\n"), Id, W ? TEXT("есть") : TEXT("НЕТ")); };
	Line(TEXT("6801 CLOSE"),    BtnClose);
	Line(TEXT("6803 CHECK"),    ChkWear);
	Line(TEXT("6808 CHECK01"),  ChkGender);
	Line(TEXT("6805 NUM00"),    TxtPage);
	Line(TEXT("6806 NUM01"),    TxtPages);
	Line(TEXT("6847 TXT09 итого"), TxtTotal);
	Line(TEXT("6850 TXT11 золото"), TxtGold);
	Line(TEXT("6848 BUT00"),    BtnRemove);
	Line(TEXT("6849 BUT01"),    BtnReset);
	Line(TEXT("6851 BUT02"),    BtnBuyWorn);
	Line(TEXT("6853 NEXTP"),    BtnNext);
	Line(TEXT("6855 BACKP"),    BtnBack);
	for (int32 T = 0; T < MenuButtons.Num(); ++T)
	{
		S += FString::Printf(TEXT("  6856 OPT%d %s (подпись %s)\n"), T,
			MenuButtons[T] ? TEXT("есть") : TEXT("НЕТ"),
			MenuLabels.IsValidIndex(T) && MenuLabels[T] ? TEXT("есть") : TEXT("НЕТ"));
	}
	for (int32 i = 0; i < Cards.Num(); ++i)
	{
		const FAsdaEquipShopCardUi& C = Cards[i];
		S += FString::Printf(TEXT("  карточка %2d (JUI %3d): рамка %d, выбор %d, кнопка %s, иконка %s, подпись %s, блок цены %d/6\n"),
			i, kCardIdx[i].Box, C.BoxPlanes.Num(), C.SelPlanes.Num(),
			C.BuyButton ? TEXT("да") : TEXT("НЕТ"), C.ItemIcon ? TEXT("да") : TEXT("НЕТ"),
			C.NameText ? TEXT("да") : TEXT("НЕТ"),
			(C.PriceTexts.Num() == 6 ? (C.PriceTexts[0] ? 6 : 0) : 0));
		S += FString::Printf(TEXT("       блок цены 79500+%d: виджетов %d%s\n"), 10 * i, C.PriceBlock.Num(),
			C.PriceBlock.Num() == 0 ? TEXT("  <-- ЦЕН НЕ БУДЕТ") : TEXT(""));
	}
	for (int32 j = 0; j < WearUi.Num(); ++j)
	{
		S += FString::Printf(TEXT("  строка %d (JUI %3d): кнопка %s, имя %s, цена %s, галки %s/%s\n"),
			j, kWearIdx[j].Row,
			WearUi[j].Row ? TEXT("да") : TEXT("НЕТ"), WearUi[j].NameTxt ? TEXT("да") : TEXT("НЕТ"),
			WearUi[j].PriceTxt ? TEXT("да") : TEXT("НЕТ"),
			WearUi[j].CheckOn ? TEXT("да") : TEXT("НЕТ"), WearUi[j].CheckOff ? TEXT("да") : TEXT("НЕТ"));
	}
	return S;
}

FString UAsdaEquipShopSubsystem::DumpState() const
{
	const FAsdaEquipShopList& L = ActiveList();
	FString S = FString::Printf(
		TEXT("=== магазин снаряжения: состояние ===\n")
		TEXT("  открыто=%s ждём 5021=%s вкладка=%d (список %d) страница %d/%d предметов %d\n")
		TEXT("  фильтры: одежда=%s пол=%s; на страницу %d; выбрана карточка %d; в пакет уйдёт %d\n"),
		bOpen ? TEXT("да") : TEXT("нет"), bPending ? TEXT("да") : TEXT("нет"),
		MenuTab, kTabToList[FMath::Clamp(MenuTab, 0, kMenuTabs - 1)], Page + 1, MaxPage + 1, L.Items.Num(),
		bFilterWear ? TEXT("да") : TEXT("нет"), bFilterGender ? TEXT("да") : TEXT("нет"),
		BuiltForPixelWidth, SelCard, PendingItemCode);
	// ★★★ БЫЛО ОТСТУПЛЕНИЕ О2, СТАЛО 1:1 (2026-08-26): эталонный `shopWin_Open@0xC37110`
	//   подгоняет высоту контейнера 6983 под `98 * (uiList_GetVisibleRows(ширина) / 3) + 48`.
	//   Теперь это делает `ApplyPanelHeight` — без неё выше 768 между витриной и списком
	//   открывается чёрная полоса голого тела окна (CVar `asda.EquipShop.LegacyPanelHeight`).
	S += FString::Printf(TEXT("  рамка 6983: рядов %d => эталон %d px; раскатано %d px; планов %d/9\n"),
		PanelRows(), 98 * (PanelRows() / 3) + 48, PanelHeightApplied, PanelPlanes.Num());
	S += FString::Printf(TEXT("  блок «Золото игрока» (7001): виджетов %d; в подсказке %d\n"),
		GoldBlock.Num(), TipCard);
	for (int32 k = 0; k < kLists; ++k)
	{
		S += FString::Printf(TEXT("  список %2d (код %3d): без фильтра %3d, одежда %3d, пол %3d\n"),
			k, Sets.IsValidIndex(k) ? Sets[k].TabCode : 0,
			Sets.IsValidIndex(k) ? Sets[k].Items.Num() : 0,
			Sets.IsValidIndex(kLists + k) ? Sets[kLists + k].Items.Num() : 0,
			Sets.IsValidIndex(2 * kLists + k) ? Sets[2 * kLists + k].Items.Num() : 0);
	}
	for (int32 j = 0; j < WearRows.Num(); ++j)
	{
		if (WearRows[j].ItemCode <= 0) { continue; }
		S += FString::Printf(TEXT("  строка %d: предмет %d, категория %d, галка %s\n"),
			j, WearRows[j].ItemCode, WearRows[j].Category, WearRows[j].bChecked ? TEXT("да") : TEXT("нет"));
	}
	S += FString::Printf(TEXT("  «Итого» = %d\n"), WearTotal());
	return S;
}

// ═══════════════════════════ КОНСОЛЬНЫЕ КОМАНДЫ (проверка без NPC) ════════════════════════════
namespace
{
	UAsdaEquipShopSubsystem* EquipShopSub(UWorld* W)
	{
		return (W && W->GetGameInstance()) ? W->GetGameInstance()->GetSubsystem<UAsdaEquipShopSubsystem>() : nullptr;
	}
}

#if ASDA_DEV_CONSOLE   // ★ античит А2: дев-команда, в Shipping не регистрируется
static FAutoConsoleCommandWithWorldAndArgs GEquipShopOpen(
	TEXT("asda.EquipShop.Open"),
	TEXT("asda.EquipShop.Open — открыть магазин снаряжения 6800 так же, как код диалога NPC 30020"),
	FConsoleCommandWithWorldAndArgsDelegate::CreateLambda([](const TArray<FString>&, UWorld* W)
	{
		UAsdaEquipShopSubsystem* S = EquipShopSub(W);
		if (!S) { UE_LOG(LogAsdaEquipShop, Warning, TEXT("подсистема недоступна")); return; }
		S->OpenWindow();
	}));
#endif // ASDA_DEV_CONSOLE

#if ASDA_DEV_CONSOLE   // ★ античит А2: дев-команда, в Shipping не регистрируется
static FAutoConsoleCommandWithWorldAndArgs GEquipShopClose(
	TEXT("asda.EquipShop.Close"),
	TEXT("asda.EquipShop.Close — закрыть окно (эталон shopWin_Close@0xC372C0)"),
	FConsoleCommandWithWorldAndArgsDelegate::CreateLambda([](const TArray<FString>&, UWorld* W)
	{
		if (UAsdaEquipShopSubsystem* S = EquipShopSub(W)) { S->CloseWindow(); }
	}));
#endif // ASDA_DEV_CONSOLE

#if ASDA_DEV_CONSOLE   // ★ античит А2: дев-команда, в Shipping не регистрируется
static FAutoConsoleCommandWithWorldAndArgs GEquipShopDump(
	TEXT("asda.EquipShop.Dump"),
	TEXT("asda.EquipShop.Dump — дамп КАЖДОГО контрола окна 6800 + витрина, фильтры, страница, список"),
	FConsoleCommandWithWorldAndArgsDelegate::CreateLambda([](const TArray<FString>&, UWorld* W)
	{
		UAsdaEquipShopSubsystem* S = EquipShopSub(W);
		if (!S) { UE_LOG(LogAsdaEquipShop, Warning, TEXT("подсистема недоступна")); return; }
		UE_LOG(LogAsdaEquipShop, Log, TEXT("\n%s\n%s"), *S->DumpControls(), *S->DumpState());
	}));
#endif // ASDA_DEV_CONSOLE

#if ASDA_DEV_CONSOLE   // ★ античит А2: дев-команда, в Shipping не регистрируется
static FAutoConsoleCommandWithWorldAndArgs GEquipShopTab(
	TEXT("asda.EquipShop.Tab"),
	TEXT("asda.EquipShop.Tab <0..8> — меню 6856: 0 Воин, 1 Лучник, 2 Маг, 3 Доспех, 4 Перчатки, ")
	TEXT("5 Штаны, 6 Щит, 7 Шлем, 8 Ботинки"),
	FConsoleCommandWithWorldAndArgsDelegate::CreateLambda([](const TArray<FString>& A, UWorld* W)
	{
		if (UAsdaEquipShopSubsystem* S = EquipShopSub(W)) { S->SetMenuTab(A.Num() > 0 ? FCString::Atoi(*A[0]) : 0); }
	}));
#endif // ASDA_DEV_CONSOLE

#if ASDA_DEV_CONSOLE   // ★ античит А2: дев-команда, в Shipping не регистрируется
static FAutoConsoleCommandWithWorldAndArgs GEquipShopPage(
	TEXT("asda.EquipShop.Page"),
	TEXT("asda.EquipShop.Page <страница> — кнопки 6853 / 6855"),
	FConsoleCommandWithWorldAndArgsDelegate::CreateLambda([](const TArray<FString>& A, UWorld* W)
	{
		if (UAsdaEquipShopSubsystem* S = EquipShopSub(W)) { S->SetPage(A.Num() > 0 ? FCString::Atoi(*A[0]) - 1 : 0); }
	}));
#endif // ASDA_DEV_CONSOLE

#if ASDA_DEV_CONSOLE   // ★ античит А2: дев-команда, в Shipping не регистрируется
static FAutoConsoleCommandWithWorldAndArgs GEquipShopFilter(
	TEXT("asda.EquipShop.Filter"),
	TEXT("asda.EquipShop.Filter <0|1> <0|1> — чекбоксы: первый аргумент 6803 «одежда», второй 6808 «свой пол»"),
	FConsoleCommandWithWorldAndArgsDelegate::CreateLambda([](const TArray<FString>& A, UWorld* W)
	{
		UAsdaEquipShopSubsystem* S = EquipShopSub(W);
		if (!S) { return; }
		S->SetFilter(0, A.Num() > 0 && FCString::Atoi(*A[0]) != 0);
		S->SetFilter(1, A.Num() > 1 && FCString::Atoi(*A[1]) != 0);
	}));
#endif // ASDA_DEV_CONSOLE

#if ASDA_DEV_CONSOLE   // ★ античит А2: дев-команда, в Shipping не регистрируется
static FAutoConsoleCommandWithWorldAndArgs GEquipShopClick(
	TEXT("asda.EquipShop.Click"),
	TEXT("asda.EquipShop.Click <карточка 0..20> [buy] — прогнать клик ТЕМ ЖЕ путём, что и мышь: ")
	TEXT("без `buy` — примерка, с `buy` — окно подтверждения покупки"),
	FConsoleCommandWithWorldAndArgsDelegate::CreateLambda([](const TArray<FString>& A, UWorld* W)
	{
		UAsdaEquipShopSubsystem* S = EquipShopSub(W);
		if (!S) { return; }
		const int32 Card = A.Num() > 0 ? FCString::Atoi(*A[0]) : 0;
		if (A.Num() > 1 && A[1].Equals(TEXT("buy"), ESearchCase::IgnoreCase)) { S->OnCardBuyClicked(Card); }
		else { S->OnCardPreviewClicked(Card); }
	}));
#endif // ASDA_DEV_CONSOLE

#if ASDA_DEV_CONSOLE   // ★ античит А2: дев-команда, в Shipping не регистрируется
static FAutoConsoleCommandWithWorldAndArgs GEquipShopWear(
	TEXT("asda.EquipShop.Wear"),
	TEXT("asda.EquipShop.Wear <строка 0..6|buy|remove|reset> — строки «Списка снаряжения» и кнопки 6848/6849/6851"),
	FConsoleCommandWithWorldAndArgsDelegate::CreateLambda([](const TArray<FString>& A, UWorld* W)
	{
		UAsdaEquipShopSubsystem* S = EquipShopSub(W);
		if (!S || A.Num() == 0) { return; }
		if (A[0].Equals(TEXT("buy"),    ESearchCase::IgnoreCase)) { S->OnBuyWornClicked();      return; }
		if (A[0].Equals(TEXT("remove"), ESearchCase::IgnoreCase)) { S->OnRemoveCheckedClicked(); return; }
		if (A[0].Equals(TEXT("reset"),  ESearchCase::IgnoreCase)) { S->OnResetClicked();        return; }
		S->OnWearRowClicked(FCString::Atoi(*A[0]));
	}));
#endif // ASDA_DEV_CONSOLE

// ── 10.10.2026: доступ самотеста двойного щелчка (`asda.Shop.SelfTestDblClick`) ──
int32 UAsdaEquipShopSubsystem::DebugWearFilled() const
{
	int32 N = 0;
	for (const FAsdaEquipShopWearRow& R : WearRows) { if (R.ItemCode > 0) { ++N; } }
	return N;
}

int32 UAsdaEquipShopSubsystem::DebugCardCode(int32 Card) const
{
	return Cards.IsValidIndex(Card) ? Cards[Card].ItemCode : 0;
}

UWidget* UAsdaEquipShopSubsystem::DebugCardIcon(int32 Card) const
{
	return Cards.IsValidIndex(Card) ? Cards[Card].ItemIcon.Get() : nullptr;
}
