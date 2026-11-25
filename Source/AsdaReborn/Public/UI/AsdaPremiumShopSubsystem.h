// Copyright (c) 2026 @sitxovski (Discord)
// UAsdaPremiumShopSubsystem — ★★★ КЭШ-МАГАЗИН ПРЕМИУМ-МОЛЛА, окно `SHELLITEM_STORE_WIN`
//   (JUI IDNum **6000**, 1148 узлов) + вкладка витрины наборов `SHELLITEM_STORE_Packageshop`
//   (**58300**, 12 карточек). Реверс — `Reports/premium_mall_core.md`,
//   `premium_mall_shops.md`; порт — `Reports/premium_mall_port.md`.
//
// ═══════════════ ★★★ ДВЕ ВИТРИНЫ — ОДИН КЛАСС. ЧТО ПЕРЕИСПОЛЬЗОВАНО ═════════════════════════════
//   Окна **6000** и **6800** в оригинале обслуживает ОДИН класс `CShellItemStore`
//   (`CShellItemStore_InitControls@0xC28820` привязывает оба: `ida.c:479296` и `479783`), и
//   `cShopDialogResetState@0xC32070` грузит ОБЕ витрины подряд (`Premium.she` и `FreeShop.she`).
//   Окно 6800 в порту уже есть — `UAsdaEquipShopSubsystem`. Поэтому ВТОРОГО МАГАЗИНА НЕТ:
//   отсюда зовутся его ОБЩИЕ ТОЧКИ (закон `port-shared-points`):
//     • `UAsdaEquipShopSubsystem::LoadSheListsFromTable` — разбор витрины `.she` из DataTable
//       (11 списков файла + 3 производных по `itemType == 9` / `itemSubType`);
//     • `UAsdaEquipShopSubsystem::ItemsPerPage` / `PanelRows` — лесенки `uiList_GetVisibleRows2`
//       и `uiList_GetVisibleRows`;
//     • `UAsdaEquipShopSubsystem::JobAllowsUsage` — фильтр по профессии.
//   ОТЛИЧИЯ 6000 от 6800 (только они и живут здесь):
//     • валюта — **GXP** (`CCharacter+0x44C`), витрина `Premium.she`; у 6800 — золото и `FreeShop.she`;
//     • сетка 21 карточка с базой **6084** и шагом **9** (у 6800 база 6857, шаг 6);
//     • корзина 7 строк с базой **6038** и шагом **6** (у 6800 база 6812, шаг 5);
//     • ДВЕ полосы вкладок: 6002 (3 сегм., живы 0 и 1) и 6009 (10 сегм., живы 0…8);
//     • собственная вкладка витрины наборов **58300**;
//     • сеть 5070 → 5071 (у 6800 — 5020 → 5021);
//     • примерки и налога войны здесь нет, зато есть панель GXP и кнопки «пополнить»/«баланс».
//
// ═══════════════ УСТРОЙСТВО ОКНА (все числа из `gamemain_jui.ini` и эталона) ═══════════════════
//   6001 крестик · 6010 картинка-заголовок «Премиум-магазин» (НЕ текст) ·
//   6002 `SHELLITEM_STORE_INDEX` TabBar_W108_3Btn: 0 = loc 12900 «Новые вещи», 1 = loc 6808
//        «Все вещи», ★ сегмент 2 кодом НЕ заполняется — мёртвый (дефект К8); при открытии
//        эталон ставит индекс **1** ·
//   6009 `SHELLITEM_STORE_MENU` BoxBar_W54W90_73Btn, 10 сегментов:
//        0 Воин 6915 · 1 Лучник 6916 · 2 Маг 6917 · 3 Доспех 6443 · 4 Перчатки 6844 ·
//        5 Штаны 6845 · 6 Щит 6846 · 7 Шлем 6847 · 8 Ботинки 6848 · 9 Инструмент 12907 ·
//   6004/6005 и 6007/6008 — два чекбокса-фильтра, ★ эталон ГАСИТ оба в Init
//        (`SetVisibleState(m_pCheckBtn,0)`) ⇒ витрина 6000 всегда работает без фильтров ·
//   6083 `SHELLITEM_ITEMLIST` — контейнер сетки, 21 ячейка `SHELLITEM_POP00…POP20`
//        (база IDNum **6084**, шаг **9**), 3 колонки × 7 строк; состав ячейки b+1…b+8 ·
//   6273 `SHELLITEM_POP00_SEL` — ОДНА рамка выделения на всю сетку (у 6800 рамка у каждой ячейки) ·
//   6278…6288 — пагинация «сетка»; 6011…6021 — пагинация «примерка» ·
//        ★ ИМЕНА ИНВЕРТИРОВАНЫ (дефект К15): `END*` стоят СЛЕВА (скин `Index_Prev` = «в начало»),
//        `FIRST*` — СПРАВА (`Index_Next` = «в конец»). Верим координатам и скину, не имени ·
//   6024 `SHELLITEM_STORE_DIS` — ★ имя врёт: это не «disabled», а КОНТЕЙНЕР КОРЗИНЫ ·
//   6030 `SHELLITEM_STORE_LISTM` — 7 строк `SHELLITEM_SELBASE1…7` = 6038 + 6j;
//        строка = `_TIT` имя + `_PRI` цена + `_MAC` монета + `_CHK`/`_DIM` две КАРТИНКИ состояния
//        (не чекбокс — клик ловит сама строка-PushButton) ·
//   6029 заголовок списка (loc 12906) · 6080 «Итого» (loc 12958) · 6081 значение ·
//   6037 «Сброс» (loc 6950) · 6275 «Купить выбранные вещи» (loc 6851) ·
//        6274 «Надеть набор» (loc 12905, скрыта кодом) · 6028/6036 скрыты кодом ·
//   6025 «Сейчас GXP» (loc 12904) · 6026 значение · 6276 «пополнить» · 6277 «проверить баланс» ·
//   40000/40001 «Деньги в подарок» (loc 12959) — ★ эталон гасит сразу после привязки (дефект К9) ·
//   58300 `SHELLITEM_STORE_Packageshop` — вкладка витрины наборов, 12 карточек `Package_1…12`
//        (IDNum 58310 + 20i), 2 колонки × 6 строк.
//
// ═══════════════ ★★★ ВКЛАДКА 6009 → НОМЕР СПИСКА ВИТРИНЫ (снято с кода, не угадано) ════════════
//   Отчёт `premium_mall_core.md` §12 п.1 оставил это соответствие ОТЛОЖЕННЫМ. Оно снято здесь по
//   `inventorySowelWin_SwitchTab@0xC3F070` (`ida.c:485945-486070`) — функции, куда приходит индекс
//   полосы 6009 (поле +2511). Каждая её ветка кладёт в `MaxPage` (+2514) `i16` по СВОЕМУ смещению;
//   дескрипторы списков лежат подряд с шагом 6 × i16 = 12 байт:
//       case 3 → i16 @4250   case 4 → @4256   case 5 → @4262
//       case 6 → @4268       case 7 → @4274   case 8 → @4280
//       case 0 → @4286       case 1 → @4292   case 2 → @4298
//   База дескриптора №0 = 4250 − 6·5 = **4220**, значит `дескриптор[k] = 4220 + 6k`, и
//       case 3…8 → списки **5…10**,  case 0/1/2 → списки **11/12/13** (три производных).
//   Это ТОЧНО ТА ЖЕ таблица, что уже выведена для полосы 6856 окна 6800
//   (`{11,12,13,5,6,7,8,9,10}`) — независимое подтверждение «один класс, одна раскладка».
//   ★★★ СЕГМЕНТ 9 «Инструмент» ВЕТКИ НЕ ИМЕЕТ: `default: break`, `MaxPage` остаётся от прошлой
//   вкладки, а список не меняется. Это ДЕФЕКТ ОРИГИНАЛА (К17), см. `asda.PremiumMall.FixTab9`.
//
// ═══════════════ СЕТЬ ═════════════════════════════════════════════════════════════════════════
//   C→S **5070** (195 б) → S→C **5071** (407 б). Паддинг ставится (`a6 = 1`).
//   ★ 01.11.2026 P101_CashGift: подарок **5072** (проверка имени) → 5073, **5074** (подарить) → 5075/5078 — кнопки карточек
//     «Подарок» 6085 + 9i и панели 6296/6333 в режиме подарка (`AsdaPremiumShopSubsystem_Gift.cpp`), общее состояние —
//     `UAsdaCashGiftSubsystem`. Пока на сервере P101 не выложен — ключ `asda.CashGift.Enable 0`: кнопок нет, ничего не уходит.
//   Баланс GXP — 5466 → 5467 (уже есть в `UAsdaNetSubsystem`).
//
// ═══════════════ НУМПАД 1410 ══════════════════════════════════════════════════════════════════
//   ★ НЕ ИСПОЛЬЗУЕТСЯ. Доказано двумя методами (`premium_mall_core.md` §9): (1) ни одна из 27
//   площадок вызова `uiOpenContextPopupWithHandlers@0xDA55A0` не принадлежит `CShellItemStore`;
//   (2) молл нумпад ЗАКРЫВАЕТ — `shopWin_Open@0xC37110` и `uiInventoryOpenWindow@0xC3DCB0`
//   зовут `uiDialogClose(g_pNumPadDialog)`. Сверено с `Docs/Port/systems/NUMPAD_FLOW.md`: ни один из 13
//   режимов за моллом не закреплён. Количество даёт КОМБОБОКС `SHELL_FUNC_NUMINPUT` 6358
//   (`sub_C42710@0xC42710`: индекс 0…6 → 1…7).
#pragma once

#include "CoreMinimal.h"
#include "AsdaTickGate.h"   // ★ R4-b (14.11.2026): шаблон класса (CDO) не тикает
#include "Subsystems/GameInstanceSubsystem.h"
#include "Engine/DataTable.h"
#include "Tickable.h"
#include "AsdaSessionReset.h"   // IAsdaSessionResettable — сброс состояния сессии
#include "UI/AsdaShopDblClick.h"   // FAsdaShopDblClick — товар в корзину двойным щелчком (10.10.2026)
#include "UI/AsdaUiInputTypes.h"   // ★ R1: FAsdaMouseFrame / FAsdaDragArm / FAsdaDragGhost … — поля ввода (без всего AsdaUiHelpers.h)
#include "Layout/Margin.h"         // FMargin
#include "UI/AsdaPremiumBuyQueue.h" // FAsdaPremiumBuyQueue — корзина уходит поштучно (25.10.2026, «купил 3 — пришла 1»)
#include "AsdaPremiumShopSubsystem.generated.h"

class UUserWidget;
class UWidget;
class UImage;
class UButton;
class UCheckBox;
class UTextBlock;
class UEditableText;
class UAsdaJuiComboBox;
class UAsdaIconCellGrid;
class UAsdaButtonRelay;

// ═══ ★★★ ЗАПИСЬ `Data\NPC\Shop\PremiumShop.BIN` — НАСТОЯЩАЯ НАВИГАЦИЯ ВИТРИНЫ 6000 ═══════════
//   Загрузчик `premiumShopLoadBin@0xC18F30` (`ida.c:473149`): заголовок **20 б** (count @+4,
//   магия `PST` @+0xC), далее `count` записей по **68 б (0x44)**. В нашем клиенте 715 записей.
//   Раскладка снята с `ui_rebuildCategorizedItemList@0xC191F0` (`ida.c:473206-473345`) — функции,
//   которая раскладывает записи по восьми категориям полосы 58200:
//     +0  i32 ItemId
//     +4  i32 **Category**  1 Инструмент · 2 Снаряжение · 3 Волосы · 4 Питомец ·
//                           5 Элитный набор · 6 Лавка мелочей
//     +8  i32 **Sub1**      кат.1 -> подкатегория 58201 (1…4); кат.2 -> класс 58202 (1 Воин, 2 Лучник, 3 Маг)
//     +12 i32 **Sub2**      кат.2 -> третий уровень 58203 (9 Оружие, 0 Шлем, 1 Доспех, 4 Перчатки,
//                           2 Штаны, 3 Ботинки, 0xB Бустер)
//     +16 u8  Field16 · +20 i32 Field20
//     +24 u8  **NewFlag**   1 -> «Горячая новинка!» (кат.0), 2 -> «Новинка!» (кат.1)
//     +25 char[32] Note · +57 u8 Field57
//   Ключ сортировки эталона: `1000*Category + 100*Sub1 (если >0) + Sub2 (если >0)` — записи
//   вставляются в отсортированный контейнер (`sub_93828E`), и порядок задаёт раскладку по страницам.
USTRUCT(BlueprintType)
struct ASDAREBORN_API FAsdaPremiumShopBinRow : public FTableRowBase
{
	GENERATED_BODY()
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="PremiumShop") int32 ItemId = 0;
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="PremiumShop") int32 Category = 0;
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="PremiumShop") int32 Sub1 = 0;
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="PremiumShop") int32 Sub2 = 0;
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="PremiumShop") int32 Field16 = 0;
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="PremiumShop") int32 Field20 = 0;
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="PremiumShop") int32 NewFlag = 0;
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="PremiumShop") FString Note;
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="PremiumShop") int32 Field57 = 0;
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="PremiumShop") FString DataHex;
	// ★ 06.11.2026 (решение владельца): штук в коробке, которую выдаёт карточка; 0 — на подписи количества нет.
	//   Не поле PremiumShop.BIN: ведомость RawAssets/Data/NPC/PremiumShopBin_card_qty.json (Tools/asda_premium_card_labels.py).
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="PremiumShop") int32 CardQty = 0;
	// ★ 06.11.2026 (решение владельца «подарок = покупка себе»): код, который выдаёт покупка карточки (коробка карты шлюза gp7); 0 — сам код.
	//   Уходит в 5074 вместо кода карточки: GameServer дарит ровно присланный код. Та же ведомость.
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="PremiumShop") int32 GiftCode = 0;
};

// ═══ ОДНА ЗАПИСЬ ВИТРИНЫ: место карточки берётся из ПОЗИЦИИ кода в массиве (как у эталона). ═══
USTRUCT()
struct FAsdaPremiumEntry
{
	GENERATED_BODY()
	UPROPERTY(Transient) int32 ItemCode   = 0;
	UPROPERTY(Transient) int32 Page       = 0;
	UPROPERTY(Transient) int32 SlotInPage = 0;
};

// ═══ УЗЛЫ ОДНОГО СЕГМЕНТА ПОЛОСЫ ВКЛАДОК (кнопка, подпись и план одного и того же сегмента) ═══
USTRUCT()
struct FAsdaPremiumBarSeg
{
	GENERATED_BODY()
	UPROPERTY(Transient) TArray<TObjectPtr<UWidget>> Nodes;
};

// ═══ ВИДЖЕТЫ ОДНОЙ КАРТОЧКИ СЕТКИ `SHELLITEM_POP{nn}` (IDNum 6084 + 9i) ═══
USTRUCT()
struct FAsdaPremiumCardUi
{
	GENERATED_BODY()
	// ВСЁ поддерево карточки, собранное ПО ПРЕФИКСУ имени. Именно по нему гасится пустая карточка:
	//   раскладка ячеек в `.JUI` НЕОДНОРОДНА (закон `jui-uneven-node-layout`), и точечные ссылки
	//   покрывают не все планы — непогашенные оставляли бы «плюсы» и обводки на пустом месте.
	UPROPERTY(Transient) TArray<TObjectPtr<UWidget>> Parts;
	// Узлы, которые не показываются НИКОГДА (авторская заглушка `CaptionText` из `.JUI`).
	//   Гасятся ПОСЛЕ общего показа поддерева.
	UPROPERTY(Transient) TArray<TObjectPtr<UWidget>> AlwaysHidden;
	UPROPERTY(Transient) TObjectPtr<UButton>    BuyButton = nullptr;   // b+5 `_BUT0` (loc 6944 «OK»)
	UPROPERTY(Transient) TObjectPtr<UTextBlock> BuyCaption= nullptr;
	UPROPERTY(Transient) TObjectPtr<UTextBlock> NameText  = nullptr;   // b+2 `_TXT00` — подпись имени
	UPROPERTY(Transient) TObjectPtr<UTextBlock> PriceText = nullptr;   // b+3 `_TXT01` — цена в GXP
	UPROPERTY(Transient) TObjectPtr<UImage>     ItemIcon  = nullptr;   // b+4 `_ICON` 64×64
	// b+7 `SHOP_NEW` и b+8 `SHOP_HOT` — бейджи, эталон держит их скрытыми.
	UPROPERTY(Transient) TArray<TObjectPtr<UWidget>> Badges;
	// b+1 `_BUT2` **6085** «Подарок» (loc 6821) — эталон кладёт в неё ТЕКСТ (`ida.c:481568-481569`),
	//   но `SetVisible` для неё не зовёт НИ РАЗУ ⇒ на экране её нет (путь подарка идёт опкодом 5074,
	//   которого сервер НЕ ПРИНИМАЕТ).
	// ★★★ КРОМЕ ЭТИХ ДВУХ у карточки есть ЕЩЁ ДВА МЁРТВЫХ УЗЛА ДАННЫХ — `_BUT0_b` и `_BUT2_b`
	//   (скин `BKBtn_W122`, **IDNum = −1**, `Visible = "0"`, подписи «Купить» и «Подарить» лежат
	//   прямо в `.JUI`): код о них не знает вовсе. Видимой обязана остаться РОВНО ОДНА кнопка —
	//   `_BUT0` **6089** с подписью loc **6944** «OK»; её IDNum и есть код команды покупки
	//   (`uiMapCommandIdToSlotIndex@0xC24E10`: 6089 + 9i → индекс карточки).
	// ⚠ У ячейки **00** имена настоящих кнопок несут мусорный префикс `__` (в `.JUI` их `ID`
	//   начинается с `[ `), поэтому поиск ролей идёт по регулярке `^(?:__)?…$`.
	UPROPERTY(Transient) TObjectPtr<UWidget> GiftButton = nullptr;
	// ★ 01.11.2026 P101_CashGift: поддерево настоящей кнопки «Подарок» `_BUT2` (группа, кнопка, подпись, значки; БЕЗ мёртвой `_BUT2_b`) —
	//   показывается только при включённом подарке (`AsdaCashGiftOn`); подпись — loc 6821 (эталон `ida.c:481568-481569`).
	UPROPERTY(Transient) TArray<TObjectPtr<UWidget>> GiftParts;
	UPROPERTY(Transient) TObjectPtr<UTextBlock> GiftCaption = nullptr;   // `_BUT2_Text`
	UPROPERTY(Transient) int32 ItemCode = 0;
};

// ═══ ★ 01.11.2026 P101_CashGift: ПАНЕЛЬ 6296 `SHELLITEM_EQUIP_WIN` / 6333 `SHELLITEM_FUNC_WIN` (режим подарка) ═══
//   Узлы — в ассете окна (импорт .JUI; полный дамп — `Reports/cash_gift/p101/dump_cashgift_controls.txt`).
//   Заполнение — `ItemTooltip_PopulateWindow@0xC40A10` (item_inventory.c:21141) при `panelMode = 1`.
USTRUCT()
struct FAsdaShellGiftPanelUi
{
	GENERATED_BODY()
	UPROPERTY(Transient) TObjectPtr<UWidget>       Root = nullptr;       // `SHELLITEM_EQUIP_WIN_grp` / `SHELLITEM_FUNC_WIN_grp`
	UPROPERTY(Transient) TObjectPtr<UTextBlock>    Title = nullptr;      // 6301 / 6338 `_TITLE`
	UPROPERTY(Transient) TObjectPtr<UTextBlock>    Substance = nullptr;  // 6302 / 6339 `_SUBSTANCE`
	UPROPERTY(Transient) TObjectPtr<UTextBlock>    Txt01 = nullptr;      // 6303 / 6340
	UPROPERTY(Transient) TObjectPtr<UTextBlock>    Txt02 = nullptr;      // 6304 / 6341
	UPROPERTY(Transient) TObjectPtr<UTextBlock>    Txt03 = nullptr;      // 6305 (только 6296)
	UPROPERTY(Transient) TObjectPtr<UTextBlock>    Txt04 = nullptr;      // 6306 (только 6296)
	UPROPERTY(Transient) TObjectPtr<UImage>        Icon = nullptr;       // 6298 / 6335 `_ICON` 64×64
	UPROPERTY(Transient) TObjectPtr<UWidget>       Hot = nullptr;        // 6299 / 6336
	UPROPERTY(Transient) TObjectPtr<UWidget>       New = nullptr;        // 6300 / 6337
	UPROPERTY(Transient) TObjectPtr<UTextBlock>    Price1 = nullptr;     // 6314 / 6348 цена
	UPROPERTY(Transient) TObjectPtr<UTextBlock>    Price2 = nullptr;     // 6317 / 6351 кэш на счету
	UPROPERTY(Transient) TObjectPtr<UWidget>       NameRow = nullptr;    // 6310 / 6344 `_NAME_grp` (подпись + «Проверить»)
	UPROPERTY(Transient) TObjectPtr<UEditableText> NameEdit = nullptr;   // 6311 / 6345 `_NINPUT` (LimitText 20)
	UPROPERTY(Transient) TArray<TObjectPtr<UWidget>> NameEditParts;      // поле и его подложка `_NINPUT_Base*`
	UPROPERTY(Transient) TObjectPtr<UButton>       Check = nullptr;      // 6312 / 6346 `_NCONFIRM` «Проверить»
	UPROPERTY(Transient) TObjectPtr<UTextBlock>    JobTxt = nullptr;     // 6325 (только 6296)
	UPROPERTY(Transient) TObjectPtr<UTextBlock>    LvTxt = nullptr;      // 6323 (только 6296)
	UPROPERTY(Transient) TObjectPtr<UAsdaJuiComboBox> Job = nullptr;     // 6326 (только 6296)
	UPROPERTY(Transient) TObjectPtr<UAsdaJuiComboBox> Lv = nullptr;      // 6324 (только 6296)
	UPROPERTY(Transient) TArray<TObjectPtr<UWidget>> NumParts;           // 6357 + 6358 (только 6333): в подарке скрыты
	UPROPERTY(Transient) TArray<TObjectPtr<UWidget>> WareParts;          // 6320/6354 чекбокс + 6321/6355 подпись: в подарке скрыты
	UPROPERTY(Transient) TObjectPtr<UButton>       PreBtn = nullptr;     // 6319 / 6353 `_PRE_BTN` (loc 6874 «Подарок»)
	UPROPERTY(Transient) TObjectPtr<UTextBlock>    PreBtnText = nullptr; // подпись `_PRE_BTN_Text`
	UPROPERTY(Transient) TObjectPtr<UButton>       CanBtn = nullptr;     // 6322 / 6356 `_CAN_BTN` «Отмена»
};

// ═══ ВИДЖЕТЫ ОДНОЙ СТРОКИ КОРЗИНЫ `SHELLITEM_SELBASE{n}` (IDNum 6038 + 6j) ═══
USTRUCT()
struct FAsdaPremiumCartUi
{
	GENERATED_BODY()
	UPROPERTY(Transient) TObjectPtr<UButton>    Row      = nullptr;   // сама строка-PushButton
	UPROPERTY(Transient) TObjectPtr<UTextBlock> NameTxt  = nullptr;   // `_TIT{n}`
	UPROPERTY(Transient) TObjectPtr<UTextBlock> PriceTxt = nullptr;   // `_PRI{n}`
	UPROPERTY(Transient) TObjectPtr<UWidget>    CheckOn  = nullptr;   // `_CHK{n}` — картинка «выбрано»
	UPROPERTY(Transient) TObjectPtr<UWidget>    CheckOff = nullptr;   // `_DIM{n}` — картинка «не выбрано»
	UPROPERTY(Transient) TArray<TObjectPtr<UWidget>> Parts;
};

// ═══ СТРОКА КОРЗИНЫ — МОДЕЛЬ ═══
USTRUCT()
struct FAsdaPremiumCartRow
{
	GENERATED_BODY()
	UPROPERTY(Transient) int32 ItemCode = -1;
	UPROPERTY(Transient) bool  bChecked = false;
};

// ═══ ВИДЖЕТЫ ОДНОЙ КАРТОЧКИ ВИТРИНЫ НАБОРОВ `Package_{n}` (IDNum 58310 + 20i) ═══
USTRUCT()
struct FAsdaPackageCardUi
{
	GENERATED_BODY()
	UPROPERTY(Transient) TArray<TObjectPtr<UWidget>> Parts;
	UPROPERTY(Transient) TObjectPtr<UTextBlock> NameText  = nullptr;   // b+1  `_ItemName_Text`
	UPROPERTY(Transient) TObjectPtr<UImage>     ItemIcon  = nullptr;   // b+2  `_ItemIcon` 128×128
	UPROPERTY(Transient) TObjectPtr<UButton>    BuyButton = nullptr;   // b+5  `_PurBtn`
	UPROPERTY(Transient) TObjectPtr<UTextBlock> BuyCaption= nullptr;
	UPROPERTY(Transient) TObjectPtr<UTextBlock> NormalLbl = nullptr;   // b+6  `_NTxt00`  (loc 99302)
	UPROPERTY(Transient) TObjectPtr<UTextBlock> NormalVal = nullptr;   // b+7  `_NorPrice00`
	UPROPERTY(Transient) TObjectPtr<UTextBlock> RateLbl   = nullptr;   // b+8  `_RTxt00`  (loc 99304)
	UPROPERTY(Transient) TObjectPtr<UTextBlock> RateVal   = nullptr;   // b+9  `_DiscRate00`
	UPROPERTY(Transient) TObjectPtr<UTextBlock> PriceLbl  = nullptr;   // b+10 `_PTxt00`  (loc 99303)
	UPROPERTY(Transient) TObjectPtr<UTextBlock> PriceVal  = nullptr;   // b+11 `_Price00`
	UPROPERTY(Transient) int32 ItemCode = 0;
};

UCLASS()
class ASDAREBORN_API UAsdaPremiumShopSubsystem : public UGameInstanceSubsystem, public FTickableGameObject,
	public IAsdaSessionResettable
{
	GENERATED_BODY()

public:
	virtual void Initialize(FSubsystemCollectionBase& Collection) override;
	virtual void Deinitialize() override;

	// ── IAsdaSessionResettable (К-3) ──
	// Окно, его защёлка «запрос в полёте» и полученные от сервера данные
	// принадлежат СЕССИИ и переживали разрыв связи вместе с GameInstance.
	virtual void ResetSessionState(EAsdaSessionResetReason Reason) override;

	virtual void Tick(float DeltaTime) override;
	virtual TStatId GetStatId() const override { RETURN_QUICK_DECLARE_CYCLE_STAT(UAsdaPremiumShopSubsystem, STATGROUP_Tickables); }
	// ★ R4-b (14.11.2026): шаблон класса (CDO) не тикает — раньше `Tick` звался дважды за кадр, у рабочего объекта и у шаблона
	//   (ключ `asda.Perf.NoCdoTick`, разбор — `AsdaTickGate.h`). Рабочий объект тикает как раньше.
	virtual bool IsTickable() const override { return AsdaTickGate::NotCdo(this); }
	virtual bool IsTickableInEditor() const override { return false; }
	virtual bool IsTickableWhenPaused() const override { return false; }

	// ── Числа окна (все из `.JUI` и эталона) ──
	static constexpr int32 kWinIdNum   = 6000;
	static constexpr int32 kCards      = 21;    // SHELLITEM_POP00…POP20, база 6084, шаг 9
	static constexpr int32 kCartRows   = 7;     // SHELLITEM_SELBASE1…7,  база 6038, шаг 6
	static constexpr int32 kMenuTabs   = 10;    // 6009 — МЁРТВАЯ полоса (эталон гасит, ida.c:479949)
	// ★★★ ЖИВАЯ НАВИГАЦИЯ ВИТРИНЫ — ЧЕТЫРЕ ПОЛОСЫ `SHELLITEM_STORE_MENU_NEW*`.
	//   Доказательство — `itemBagDlgInit@0xC2C040` (`ida.c:480014-480051`): привязывает 58200…58203,
	//   ПОКАЗЫВАЕТ 58200 (`SetVisible(…, 1u)`) и заполняет подписи циклами
	//   `loc 10401+k` (8), `loc 10421+m` (5), `loc 10426+n` (3), `loc 10441+ii` (8).
	//   Показ/скрытие под-полос — `hairShop_UpdatePanelVisibility@0xC4CFB0` (событие 0x10502).
	static constexpr int32 kCatTabs    = 8;     // 58200 категории
	static constexpr int32 kToolTabs   = 5;     // 58201 подкатегории «Инструмент»
	static constexpr int32 kEquipTabs  = 3;     // 58202 класс «Снаряжение» (Воин/Лучник/Маг)
	static constexpr int32 kSub2Tabs   = 8;     // 58203 третий уровень «Снаряжения»
	static constexpr int32 kIndexTabs  = 3;     // 6002 (живы 0 и 1)
	static constexpr int32 kLists      = 14;    // 11 из `Premium.she` + 3 производных
	static constexpr int32 kPkgCards   = 12;    // Package_1…12, база 58310, шаг 20
	// Магазин молла лежит НАД инвентарём (720) и магазином снаряжения (732).
	static constexpr int32 kShopZ = 733;

	// ═══ ОТКРЫТИЕ / ЗАКРЫТИЕ ═══
	//   `bPackageTab` = true — открыть сразу на витрине наборов (сегмент 3 меню 30002,
	//   эталонный `uiPackageStampWndOpenPage`, который показывает `this[951]` = контрол 58300).
	UFUNCTION(BlueprintCallable, Category="Asda|ПремиумМагазин")
	void OpenWindow(bool bPackageTab = false);

	UFUNCTION(BlueprintCallable, Category="Asda|ПремиумМагазин")
	void CloseWindow();

	UFUNCTION(BlueprintPure, Category="Asda|ПремиумМагазин")
	bool IsOpen() const { return bOpen; }

	UFUNCTION(BlueprintPure, Category="Asda|ПремиумМагазин")
	bool IsPackageTab() const { return bPackageTab; }

	// ═══ ДЕЙСТВИЯ ═══
	void OnCardClicked(int32 Card);        // клик по карточке — положить в корзину
	void OnCardBuyClicked(int32 Card);     // 6084+9i+5 `_BUT0` — купить одну вещь (через 1559)
	void OnCartRowClicked(int32 Row);      // 6038+6j — переключить галочку строки
	void OnResetClicked();                 // 6037 «Сброс»
	void OnBuySelectedClicked();           // 6275 «Купить выбранные вещи»
	void OnChargeClicked();                // 6276 «пополнить» — внешний браузер
	void OnBalanceClicked();               // 6277 «проверить баланс» — 5466
	void SetMenuTab(int32 Tab);            // 6009 — мёртвая полоса, оставлена ради диагностики
	void SetIndexTab(int32 Tab);           // 6002 — мёртвая полоса, оставлена ради диагностики
	// ═══ ★★★ ЖИВЫЕ ПОЛОСЫ ═══
	UFUNCTION(BlueprintCallable, Category="Asda|ПремиумМагазин") void SetCategory(int32 Cat);   // 58200, 0…7
	UFUNCTION(BlueprintCallable, Category="Asda|ПремиумМагазин") void SetToolSub(int32 Sub);    // 58201, 0…4
	UFUNCTION(BlueprintCallable, Category="Asda|ПремиумМагазин") void SetEquipSub(int32 Sub);   // 58202, 0…2
	UFUNCTION(BlueprintCallable, Category="Asda|ПремиумМагазин") void SetEquipSub2(int32 Sub);  // 58203, 0…7
	UFUNCTION(BlueprintPure, Category="Asda|ПремиумМагазин") int32 GetCategory() const { return CatTab; }
	// ★ R4-b хвосты-2 (15.11.2026): только чтение — снимок состояния витрины самотестом (`asda.SelfTest.BoxClass` возвращает его после прогона).
	int32 GetToolSub() const { return ToolSub; }        // 58201
	int32 GetEquipSub() const { return EqSub; }         // 58202
	int32 GetEquipSub2() const { return EqSub2; }       // 58203
	bool IsEquipSub2Shown() const { return bEqSub2Shown; }   // третий уровень 58203 показан
	int32 GetPage() const { return Page; }              // страница витрины (6279…6288)
	// ★ Пересобрать раскладку под ТЕКУЩЕЕ разрешение (полосы, схлопывание, число карточек).
	//   Нужен и самотесту (разрешение меняется не в том же кадре), и на будущее — смене режима экрана.
	UFUNCTION(BlueprintCallable, Category="Asda|ПремиумМагазин") void RelayoutForScreen();
	void SetPage(int32 NewPage);           // 6279…6288
	void ShowPackageTab(bool bOn);         // 58300
	void OnPackageBuyClicked(int32 Card);  // 58315+20i

	// ★ ДИАГНОСТИКА БЕЗ СЕРВЕРА: подставить баланс GXP, который в бою приходит пакетом 5467.
	//   Нужна для проверки ПУТЁМ ИГРОКА (открыть → купить → модалка) на карте без соединения.
	void DebugSetGxp(int32 Value) { CashGxp = Value; RefreshCash(); }
	// ★ 09.10.2026 (самотест цикла автолопаты, `asda.Fish.AnimSelfTest` раздел F): только ПРИЗНАК «магазин открыт» (`IsOpen`,
	//   эталонный `g_pPartyTargetObj`) — для гейта 9001 повтора, без окна и без сети. Тик окна ждёт `bReady` и признак не трогает.
	//   Вызывающий обязан вернуть false в том же шаге.
	void DebugSetOpenFlag(bool bOn) { bOpen = bOn; }
	// ── 10.10.2026: доступ самотеста `asda.Shop.SelfTestDblClick` (только чтение; клик — настоящим событием Slate) ──
	int32 DebugCartFilled() const;                 // сколько строк корзины занято
	int32 DebugSelCard() const { return SelCard; } // выбранная карточка (−1 = нет)
	int32 DebugCardCode(int32 Card) const;         // код товара карточки (0 = пусто)
	FString DebugCardText(int32 Card) const;       // подпись карточки, как на экране (самотест `asda.SelfTest.PremiumCardQty`)
	FString DebugCartRowText(int32 Row) const;     // подпись строки корзины, как на экране (тот же самотест)
	// Подпись товара витрины: имя + количество коробки (RU « x3», EN «*3»). Имя предмета в сумке не меняется. Ключ `asda.PremiumMall.CardQtyLabel`.
	FString CardLabel(int32 Code, const FString& Name) const;
	// Код позиции 5074 для карточки: то, что выдаёт покупка себе (столбец GiftCode), иначе сам код. Ключ `asda.PremiumMall.GiftAsBuy`.
	int32 GiftWireCode(int32 Code) const;
	// ★ 06.11.2026: покупка карточки с вариантами класса и уровня идёт через панель 6296, как в оригинале (`tradeSelectSlot`). Ключ `asda.PremiumMall.BuyVariants`.
	bool IsPanelBuy() const { return bPanelBuy; }
	UWidget* DebugBuySelWidget() const;   // кнопка 6275 (настоящие щелчки самотеста `asda.SelfTest.PremiumBuyVariant`)
	UWidget* DebugNode(const FString& Name) const;   // узел окна по имени (настоящие щелчки самотеста `asda.SelfTest.PremiumBuyVariant`)
	UWidget* DebugCardBuyWidget(int32 Card) const;   // кнопка покупки карточки
	UWidget* DebugCardGiftWidget(int32 Card) const;  // кнопка подарка карточки
	UWidget* DebugGiftGiveWidget() const;            // 6319 «Купить» / «Подарок» панели 6296
	FString DebugEquipPrice() const;                 // цена на панели 6296, как на экране
	FString DebugGiftTitle(bool bFunc) const;      // заголовок окна подарка, как на экране (самотест `asda.SelfTest.PremiumCardQty`)
	UWidget* DebugCardIcon(int32 Card) const;      // иконка карточки — точка симклика по живой геометрии
	// ── 24.10.2026: доступ самотеста защёлки «запрос в полёте» (`asda.PremiumMall.PendingSelfTest`, Д-18 окна 6000) ──
	// Взвести защёлку так, как её ставит `SendBuy` после отправки 5070, но «AgoSec секунд назад» (без сети).
	void DebugArmPending(double AgoSec) { bPending = true; PendingSince = FPlatformTime::Seconds() - AgoSec; }
	bool DebugIsPending() const { return bPending; }        // защёлка `this[2071]` взведена
	void DebugClearPending() { bPending = false; }          // вернуть защёлку в исходное (конец самотеста)
	void DebugFeedBuyResult(const TArray<uint8>& Body407) { OnBuyResult(Body407); }   // приём 5071 тем же обработчиком
	int32 DebugResultCount() const { return CntResult; }    // сколько ответов 5071 принято
	// ── 25.10.2026: доступ самотеста корзины `asda.PremiumMall.CartSelfTest` ([ИСПРАВЛЕНИЕ] корзина поштучно) ──
	void DebugFillCart(const TArray<int32>& Codes);         // строки корзины = эти коды (отмечены), остальные пусты
	// Перехват 5070: тело (171 б) кладётся в `DebugSent` вместо сокета, защёлка ставится как после настоящей отправки.
	void DebugSetCapture(bool bOn) { bDebugCapture = bOn; DebugSent.Reset(); }
	const TArray<TArray<uint8>>& DebugSentBodies() const { return DebugSent; }   // перехваченные тела 5070 по порядку
	bool  DebugCartRunning() const { return CartRun.bRun; }  // идёт поштучная покупка корзины
	int32 DebugCartLeft() const { return CartRun.Left.Num(); }   // позиций ещё не отправлено
	int32 DebugCartRowCode(int32 Row) const { return CartRows.IsValidIndex(Row) ? CartRows[Row].ItemCode : 0; }   // код строки корзины

	// ═══ ★ 01.11.2026 P101_CashGift: ПОДАРОК ИЗ ОКНА 6000 (`AsdaPremiumShopSubsystem_Gift.cpp`) ═══
	void OnCardGiftClicked(int32 Card);    // 6085 + 9i → LABEL_53 (ui_core.c:25705) → `tradeBuyTabSelect@0xC42760` → панель, panelMode 1
	void OnGiftCheckClicked();             // 6312 / 6346 → `uiConfirmPasswordMatch@0xC47330` → 5072
	void OnGiftGiveClicked(bool bFunc);    // 6319 / 6353 → `upgradeUiHandleInfoMsg@0xC422B0` → 1559 → 5074
	void CloseGiftPanels();                // 6322 / 6356 и сброс → `uiDialogCloseReset@0xC42600`
	// ESC окна 6000 (событие 0x15, ui_core.c:25443-25455): открыта панель 6296/6333 → закрыть только её, иначе — окно целиком.
	void EscClose();
	bool IsGiftPanelShown() const;         // `uiCheckTradePanelsClosed@0xC43390` == false (видна 6296 или 6333)
	int32 GetGiftItemCode() const { return GiftItemCode; }   // +10728
	int32 GetCashGxp() const { return CashGxp; }             // `CCharacter+0x44C` (локальная копия окна)
	// Самотест: ввести имя в поле открытой панели (как набор с клавиатуры — через `SetText` поля).
	void DebugSetGiftName(const FString& Name);
	// Самотест: выбрать пункт комбо профессии/уровня панели 6296 (тот же вход, что клик по строке списка).
	void DebugPickGiftJob(int32 Idx) { GiftOnJobPicked(Idx); }
	void DebugPickGiftLv(int32 Idx) { GiftOnLvPicked(Idx); }
	int32 DebugGiftJobs() const { return GiftJobs.Num(); }    // различных профессий в наборе
	int32 DebugGiftLvs() const { return GiftLvCodes.Num(); }  // уровней выбранной профессии
	// Путь игрока: ФАКТИЧЕСКАЯ экранная геометрия узлов панели подарка после отрисовки (`GetCachedGeometry`).
	FString DumpGiftPanels() const;

	// ═══ ДИАГНОСТИКА (закон `full-structural-analysis-always`) ═══
	FString DumpControls() const;
	FString DumpState() const;
	// Самотест «путём игрока»: `asda.PremiumMall.SelfTest` — печатает ФАКТИЧЕСКУЮ экранную
	//   геометрию (`GetCachedGeometry`), видимость и цвет узлов ПОСЛЕ отрисовки.
	FString SelfTest() const;

private:
	// ── Данные ──
	bool  LoadShe();                       // порт `shopPremiumMall_LoadShe@0xBD6460` через общую точку
	void  BuildPages();                    // раскладка страниц активного списка
	int32 ActiveListIndex() const;         // вкладка 6009 → номер списка (см. шапку)
	const TArray<FAsdaPremiumEntry>& ActiveEntries() const;

	// ═══ ★★★ ЖИВАЯ НАВИГАЦИЯ: `PremiumShop.BIN` → 8 категорий и их под-полосы ═══
	bool  LoadBin();                       // порт `premiumShopLoadBin@0xC18F30` (через DataTable)
	// Порт `ui_rebuildCategorizedItemList@0xC191F0`: отбор кодов текущей категории/подкатегории
	//   в эталонном порядке (ключ `1000*Cat + 100*Sub1 + Sub2`).
	void  BuildActiveCodes(TArray<int32>& Out) const;
	int32 PerPageForTab() const;           // `uiRowCountByScreenHeight` / `uiList_GetVisibleRows` / 6
	// ★★ Ограничение числа карточек тем, что РЕАЛЬНО влезает при текущем разрешении
	//   (ключ `asda.PremiumMall.FitScreen`). Считается по слотам, а не константами.
	int32 FitToScreen(int32 ByRes) const;
	void  SetupBars();                     // привязка и подписи четырёх полос
	void  RefreshBarVisibility();          // порт `hairShop_UpdatePanelVisibility@0xC4CFB0`
	// ★ Уплотнение полосы категорий 58200: видимые сегменты сдвигаются влево, чтобы на месте
	//   скрытых не оставалось дыр. Считается ОТ ИСХОДНЫХ смещений (`AsdaUi::ShiftSlotXFromBase`).
	void  CompactCatBar();
	bool  IsCatHidden(int32 Cat) const;    // категория скрыта ключом владельца?
	void  RefreshBarState();               // подсветка выбранного сегмента во всех полосах
	// ★ Схлопывание пустой области под полосами — тот же приём, что у склада
	//   (`UAsdaWarehouseSubsystem::ApplyTabAreaCollapse`, дефект D19): сдвиг считается ОТ ИСХОДНЫХ
	//   смещений (`AsdaUi::ShiftSlotYFromBase`), иначе повторный вызов даёт нулевой сдвиг.
	void  ApplyContentShift();

	// ── Жизненный цикл виджета ──
	bool EnsureWidget();
	void SetupControls();
	void SetupCards();
	void SetupCart();
	void SetupPackageCards();
	void RegisterZone();
	UTextBlock* MakeLabelLike(UWidget* Ref, const TCHAR* NewName, const UTextBlock* FontDonor);

	// ── Рендер ──
	void BuildPage();
	void PopulateCard(int32 Card, int32 ItemCode);
	void ShowCard(int32 Card, bool bVisible);
	// ★ 08.11.2026 (пачка 16, баг 6) [ОРИГИНАЛ]: порт `uiTabPanel_MoveSelectionHighlight@0xC27030` — одна рамка выбора 6273
	//   `SHELLITEM_POP00_SEL` на всю сетку встаёт на карточку `Card` и показывается; −1 / пустая или скрытая карточка — гасится.
	void MoveSelectionFrame(int32 Card);
	// Сбор восьми планов рамки 6273 и корней 21 карточки; домашние позиции снимаются с ТЕКУЩЕГО виджета (после смены карты — заново).
	void SetupSelFrame();
	void RefreshPageLabels();
	void RefreshCash();                    // 6026 — «Сейчас GXP»
	void RefreshCart();                    // порт `uiRefreshItemSlotList@0xC36010`
	void RefreshTabState();
	void BuildPackagePage();
	// ★★★ ВЫСОТА БЕЛОЙ РАМКИ ВИТРИНЫ 6083 `SHELLITEM_ITEMLIST` — жалоба владельца 2026-08-28
	//   «также как у бустеров — белая кромка не подстраивается под содержимое».
	//   ЭТАЛОН: `inventorySowelWin_SwitchTab@0xC3F070` (`ida.c:486078-486100`) на КАЖДОМ
	//   переключении вкладки берёт контрол **6083**, запоминает его позицию
	//   (`Class_GetVec2_74`), читает ширину экрана и ставит РАЗМЕР
	//       h = 98 * (uiList_GetVisibleRows(ширина) / 3) + 48
	//   (для вкладки 9 «Инструмент» — та же формула, но по `uiRowCountByScreenHeight@0xBD63E0`),
	//   после чего ВОЗВРАЩАЕТ позицию (`CControl_SetField74_75`) — ресайз 9-slice её сдвигает.
	//   ⇒ рамка в оригинале ЖИВАЯ и меняется вместе с числом рядов; порт её не трогал вовсе,
	//   поэтому она навсегда оставалась 500 px из `.JUI`.
	//   КАК: иерархия `.JUI` расплющена, рамка живёт девятью планами 3×3
	//   (`SHELLITEM_ITEMLIST_{idx}_0…8`). Тянем средний ряд (3,4,5) по высоте и опускаем нижний
	//   (6,7,8) — ровно то, что делает ресайз 9-slice. Ширина окна не меняется, колонки не трогаем.
	void ApplyPanelHeight(int32 VisibleCards);
	int32 PanelRowsForTab() const;   // 6009 сегм.9 → `uiRowCountByScreenHeight`, иначе `PanelRows`

	// ── Корзина ──
	int32 FindFreeCartSlot() const;
	int32 CartTotal() const;

	// ── Сеть ──
	void SendBuy(const TArray<int32>& Codes, bool bTable1, bool bToInventory);
	// [ИСПРАВЛЕНИЕ] 25.10.2026 корзина поштучно (`asda.PremiumMall.CartOnePerPacket`, шапка `AsdaPremiumBuyQueue.h`).
	void StartCartRun(const TArray<int32>& Codes);   // начать: первая позиция уходит сразу
	void SendNextCartItem();                         // отправить следующую позицию (из Tick, не из колбэка 5071)
	void StopCartRun(const TCHAR* Why);              // прервать запуск (неотправленное не покупается)
	void RemoveBoughtCartRow(int32 ItemCode);        // снять из корзины строку подтверждённой покупки
	void OnBuyResult(const TArray<uint8>& Body407);
	void OnBalance(uint8 Status, int32 Balance);

	// ── Подсказка предмета ──
	void TickTooltip(int32 OverCard);

	// ── Нотисы ──
	FString Loc(int32 Id) const;
	void NoticeChat(int32 LocId) const;
	void NoticeTip(int32 LocId) const;

	// ═══ СОСТОЯНИЕ ═══
	UPROPERTY(Transient) TObjectPtr<UUserWidget> Widget = nullptr;
	UPROPERTY(Transient) TArray<FAsdaPremiumCardUi> Cards;
	UPROPERTY(Transient) TArray<FAsdaPremiumCartUi> CartUi;
	UPROPERTY(Transient) TArray<FAsdaPackageCardUi> PkgUi;
	UPROPERTY(Transient) TObjectPtr<UButton>    BtnClose   = nullptr;   // 6001
	UPROPERTY(Transient) TObjectPtr<UButton>    BtnReset   = nullptr;   // 6037
	UPROPERTY(Transient) TObjectPtr<UButton>    BtnBuySel  = nullptr;   // 6275
	UPROPERTY(Transient) TObjectPtr<UButton>    BtnCharge  = nullptr;   // 6276
	UPROPERTY(Transient) TObjectPtr<UButton>    BtnBalance = nullptr;   // 6277
	// ★★★ ЖИВАЯ пагинация окна 6000 — группа EQUIP 6011…6021 (у группы FUNC 6278…6288 эталон
	//   ГАСИТ корень 6278, и её счётчики-дети гаснут вместе с ним). Разбор — в .cpp.
	UPROPERTY(Transient) TObjectPtr<UButton>    BtnNext    = nullptr;   // 6015 EQUIP_NEXTP
	UPROPERTY(Transient) TObjectPtr<UButton>    BtnBack    = nullptr;   // 6017 EQUIP_BACKP
	UPROPERTY(Transient) TObjectPtr<UButton>    BtnFirst   = nullptr;   // ★ 6021 `END2` — СЛЕВА
	UPROPERTY(Transient) TObjectPtr<UButton>    BtnLast    = nullptr;   // ★ 6019 `FIRST2` — СПРАВА
	UPROPERTY(Transient) TObjectPtr<UTextBlock> TxtPage    = nullptr;   // 6012 EQUIP_NUM00
	UPROPERTY(Transient) TObjectPtr<UTextBlock> TxtPages   = nullptr;   // 6013 EQUIP_NUM01
	UPROPERTY(Transient) TObjectPtr<UTextBlock> TxtCash    = nullptr;   // 6026
	UPROPERTY(Transient) TObjectPtr<UTextBlock> TxtCashLbl = nullptr;   // 6025 (loc 12904)
	UPROPERTY(Transient) TObjectPtr<UTextBlock> TxtSum     = nullptr;   // 6081
	UPROPERTY(Transient) TObjectPtr<UTextBlock> TxtSumHdr  = nullptr;   // 6080 (loc 12958)
	UPROPERTY(Transient) TObjectPtr<UTextBlock> TxtCartHdr = nullptr;   // 6029 (loc 12906)
	UPROPERTY(Transient) TObjectPtr<UTextBlock> CapReset   = nullptr;   // 6037 (loc 6950)
	UPROPERTY(Transient) TObjectPtr<UTextBlock> CapBuySel  = nullptr;   // 6275 (loc 6851)
	UPROPERTY(Transient) TArray<TObjectPtr<UButton>>    MenuButtons;    // 6009 OPT0…OPT9
	UPROPERTY(Transient) TArray<TObjectPtr<UTextBlock>> MenuLabels;
	UPROPERTY(Transient) TArray<TObjectPtr<UButton>>    IndexButtons;   // 6002 OPT0…OPT2
	UPROPERTY(Transient) TArray<TObjectPtr<UTextBlock>> IndexLabels;
	// Узлы, которые эталон ГАСИТ в Init: фильтры 6004/6005/6007/6008, кнопки 6028/6036/6274,
	//   мёртвая пара «подарочный GXP» 40000/40001, рамка выделения 6273, слэш 6278.
	UPROPERTY(Transient) TArray<TObjectPtr<UWidget>> HiddenByCode;
	// Всё поддерево вкладки 58300 — показывается/гасится целиком.
	UPROPERTY(Transient) TArray<TObjectPtr<UWidget>> PackageTabTree;
	// Всё поддерево сетки 6083 — гасится, пока открыта вкладка наборов.
	UPROPERTY(Transient) TArray<TObjectPtr<UWidget>> GridTree;
	// Девять планов рамки витрины 6083 (`SHELLITEM_ITEMLIST_{idx}_0…8`), порядок 3×3.
	UPROPERTY(Transient) TArray<TObjectPtr<UWidget>> PanelPlanes;
	int32 PanelHeightApplied = 0;
	// ★★★ ПОЛОСЫ `SHELLITEM_STORE_MENU_NEW1/1_2/1_3/2/3/4` (IDNum 58200/58204/58940/58201/58202/
	//   58203) — у ВСЕХ ШЕСТИ в `.JUI` `Visible = "0"`, и эталонный обработчик
	//   `hairShop_UpdatePanelVisibility@0xC4CFB0` первым делом гасит их снова. Импортёр свернул
	//   их планы и кнопки, но ПОДПИСИ (`*_Text*`, их кладёт общий шаг подписей вкладок) остались
	//   видимыми и лежат в КОРНЕВОМ канвасе — отсюда «Экипировка», «Показать всё», «Воин», «HOT~»,
	//   «Все» на скриншоте владельца. Гасим ВСЁ поддерево по префиксу (закон `jui-uneven-node-layout`).
	// ★★★ ЧЕТЫРЕ ЖИВЫЕ ПОЛОСЫ. 58204 `NEW1_2` и 58940 `NEW1_3` кодом НЕ привязываются
	//   (`itemBagDlgInit` знает только 58200…58203) ⇒ они мёртвые и гасятся.
	UPROPERTY(Transient) TArray<TObjectPtr<UButton>>    CatButtons;    // 58200 ×8
	UPROPERTY(Transient) TArray<TObjectPtr<UTextBlock>> CatLabels;
	UPROPERTY(Transient) TArray<TObjectPtr<UButton>>    ToolButtons;   // 58201 ×5
	UPROPERTY(Transient) TArray<TObjectPtr<UTextBlock>> ToolLabels;
	UPROPERTY(Transient) TArray<TObjectPtr<UButton>>    EqButtons;     // 58202 ×3
	UPROPERTY(Transient) TArray<TObjectPtr<UTextBlock>> EqLabels;
	UPROPERTY(Transient) TArray<TObjectPtr<UButton>>    Sub2Buttons;   // 58203 ×8
	UPROPERTY(Transient) TArray<TObjectPtr<UTextBlock>> Sub2Labels;
	// Поддеревья полос целиком (для показа/скрытия по правилам эталона).
	// Узлы КАЖДОГО сегмента полосы 58200 по отдельности (кнопка + подпись + план) — нужны
	//   уплотнению: двигать надо посегментно, а не всю полосу.
	UPROPERTY(Transient) TArray<FAsdaPremiumBarSeg> CatSegNodes;
	UPROPERTY(Transient) TArray<TObjectPtr<UWidget>> BarCatTree;    // 58200
	UPROPERTY(Transient) TArray<TObjectPtr<UWidget>> BarToolTree;   // 58201
	UPROPERTY(Transient) TArray<TObjectPtr<UWidget>> BarEqTree;     // 58202
	UPROPERTY(Transient) TArray<TObjectPtr<UWidget>> BarSub2Tree;   // 58203
	// Узлы, которые двигает `ApplyContentShift` (сетка 6083 и вкладка наборов 58300).
	UPROPERTY(Transient) TArray<TObjectPtr<UWidget>> ShiftTargets;
	// Исходные смещения слотов — ЛОВУШКА склада D19: сдвиг считаем от них, а не от текущих.
	TMap<TWeakObjectPtr<UWidget>, FMargin> BaseOffsets;
	TArray<FLinearColor> CatIdle, ToolIdle, EqIdle, Sub2Idle;
	TArray<FAsdaPremiumShopBinRow> BinRows;
	TMap<int32, int32> CardQtyByCode;   // код товара → штук в коробке (столбец CardQty строк витрины)
	TMap<int32, int32> GiftCodeByCode;  // код товара → код выдачи для подарка (столбец GiftCode)
	bool bPanelBuy = false;             // панель 6296 открыта покупкой (panelMode 0), а не подарком
	TArray<int32> CartPick;             // строки корзины, которым ещё не выбран вариант (кнопка 6275)
	int32 CartPickRow = -1;             // строка корзины, для которой сейчас открыта панель 6296
	bool bCartPicked = false;           // варианты выбраны: следующий проход 6275 идёт сразу в 1559
	bool CartPickNext();                // открыть панель следующей строки с вариантами; false — таких строк нет
	void CartPickConfirm();             // «Купить» панели: код варианта в строку, дальше следующая строка или 1559
	bool TryOpenBuyVariants(int32 Code);             // есть варианты → панель 6296 в режиме покупки
	void BuyCodeChecked(int32 Card, int32 Code);     // проверки цены и модалка 1559 для выбранного кода
	bool  bBinLoaded = false;
	int32 CatTab  = 0;    // 58200 (эталонное поле `pItemDlg[2057]`)
	int32 ToolSub = 0;    // 58201 (`pItemDlg[2058]`)
	int32 EqSub   = 0;    // 58202 (`pItemDlg[2060]`)
	int32 EqSub2  = 0;    // 58203 (`pItemDlg[2059]`)
	// ★ Третий уровень 58203 появляется ТОЛЬКО после клика по классу в 58202 (эталон:
	//   ветка `a2 == 58202` показывает обе полосы, `ida.c:490829-490832` + `478223-478232`).
	bool  bEqSub2Shown = false;
	float ShiftApplied = -1.f;
	UPROPERTY(Transient) TArray<TObjectPtr<UWidget>> DeadNewBars;
	UPROPERTY(Transient) TObjectPtr<UAsdaIconCellGrid> IconGrid = nullptr;
	UPROPERTY(Transient) TObjectPtr<UAsdaIconCellGrid> PkgIconGrid = nullptr;
	UPROPERTY(Transient) TArray<TObjectPtr<UAsdaButtonRelay>> Relays;

	// ── Модель ──
	TArray<TArray<int32>>            RawLists;     // 14 списков (11 файла + 3 производных)
	TArray<FAsdaPremiumEntry>        Entries;      // разложенный по страницам активный список
	UPROPERTY(Transient) TArray<FAsdaPremiumCartRow> CartRows;

	TArray<FLinearColor> MenuIdleColors;
	TArray<FLinearColor> IndexIdleColors;

	bool  bOpen = false;
	bool  bSnapshotTaken = false;    // ★ волна В4: при открытии снят снимок окон и сделано «закрыть всё» — вернуть при закрытии
	bool  bReady = false;
	bool  bSheLoaded = false;
	bool  bPending = false;          // ждём 5071 (эталонное поле `this[2071]`) → нотис 1026
	double PendingSince = 0.0;       // [ИСПРАВЛЕНИЕ] Д-18 окна 6000: время отправки 5070 (FPlatformTime::Seconds) — отсчёт таймаута защёлки
	bool  bPackageTab = false;
	int32 MenuTab  = 0;              // 6009 (эталонное поле +2511)
	int32 IndexTab = 1;              // 6002 (эталонное поле +2509; при открытии эталон ставит 1)
	int32 Page     = 0;
	int32 MaxPage  = 0;
	int32 PkgPage  = 0;
	int32 SelCard  = -1;
	// ★ 08.11.2026 (пачка 16, баг 6): рамка выбора 6273 — 8 планов `SHELLITEM_POP00_SEL_*` (группы нет, родитель `SHELLITEM_ITEMLIST_grp`),
	//   корни карточек `SHELLITEM_POPnn_grp` и домашние позиции первого плана рамки и карточки 00 (`.JUI`: рамка (11,6) 216×94,
	//   карточка (13,9) 212×90). Дамп: `Reports/batch16/shop_outline/dump_selframe_before.txt`.
	UPROPERTY(Transient) TArray<TObjectPtr<UWidget>> SelFrame;
	UPROPERTY(Transient) TArray<TObjectPtr<UWidget>> CardRoots;
	FVector2D SelFrameHome = FVector2D::ZeroVector;   // позиция плана `_SEL_*_0` в ассете
	FVector2D CardHome0    = FVector2D::ZeroVector;   // позиция `SHELLITEM_POP00_grp` в ассете
	// [ПОРТ] 10.10.2026: распознаватель двойного щелчка по иконке карточки (ключ — номер карточки 0…20).
	FAsdaShopDblClick CardDbl;
	// 10.10.2026: латч мыши окна — клик по иконке карточки (эталонный 0xB = нажатие ЛКМ) по живой геометрии.
	FAsdaMouseFrame Mouse;
	// Нажатие ЛКМ над иконкой карточки `Card` (−1 = мимо карточек): первый щелчок — выбор, второй — в корзину.
	void OnCardIconPressed(int32 Card);
	int32 CashGxp  = 0;              // локальная копия `CCharacter+0x44C`
	int32 TipCard  = -1;
	int32 BuiltForPerPage = 0;
	// Что улетело в 5070 — из этого строятся строки успеха 20062/20064 по ответу.
	TArray<int32> PendingBuyCodes;
	FAsdaPremiumBuyQueue CartRun;            // [ИСПРАВЛЕНИЕ] 25.10.2026: очередь поштучной покупки корзины (ключ CartOnePerPacket 1)
	// [ИСПРАВЛЕНИЕ] 25.10.2026 (корзина целиком): смешанная корзина item_list + special_itemlist — ДВА пакета 5070 (байт +1
	//   «таблица» у пакета один: CharChargeShopBuy молча пропускает коды не той таблицы). Второй уходит после 5071 код 1.
	TArray<int32> CartNextBatch;             // коды второго пакета
	bool bCartNextTable1 = false;            // таблица второго пакета (1 = item_list)
	bool bCartNextSend = false;              // первый пакет подтверждён — второй шлёт Tick
	bool bDebugCapture = false;              // самотест: 5070 не уходит в сокет, тело пишется в DebugSent
	TArray<TArray<uint8>> DebugSent;         // самотест: перехваченные тела 5070
	FDelegateHandle HBuyResult;
	FDelegateHandle HBalance;
	int32 CntBuy = 0;
	int32 CntResult = 0;

	// ── ★ 01.11.2026 P101_CashGift: панели подарка 6296/6333 ──
	void SetupGiftPanels();                // привязка узлов, реле, ограничители поля, зона ввода
	void TickGiftPanels();                 // комбо профессии/уровня 6296 (тот же кадр мыши `Mouse`)
	void PopulateGiftPanel(int32 ItemCode, bool bFunc, bool bBuy = false);   // `ItemTooltip_PopulateWindow` при panelMode = 1
	void ResetGiftPanels();                // `upgradeUiResetPanels@0xC41F50`
	void GiftBuildSetList(int32 BaseCode, bool bQuiet = false); // `tradeBuildCategoryItemList@0xC43560`
	bool GiftBuildMatched();               // `tradeItem_BuildMatchedList@0xC41C60` + сортировка + списки комбо
	void GiftRefreshCombos();              // `tradeRefreshHistoryList@0xC43EA0`
	void GiftFillInfo();                   // `UI_FillItemInfoPanel@0xC44EA0`
	void GiftOnJobPicked(int32 Idx);       // `uiItemFilterListPopulate_49F0@0xC449F0`
	void GiftOnLvPicked(int32 Idx);        // `uiInventorySelectItemSlot@0xC45170`
	void OnGiftClearFields(bool bBoth, int32 FieldIdc);   // `uiPanelClearTextAndState` / очистка поля при «себе»
	void ShowGiftPanel(FAsdaShellGiftPanelUi& P, bool bShow);
	UPROPERTY(Transient) FAsdaShellGiftPanelUi GiftEquip;   // 6296
	UPROPERTY(Transient) FAsdaShellGiftPanelUi GiftFunc;    // 6333
	UPROPERTY(Transient) TArray<TObjectPtr<UObject>> GiftAnchors;   // реле кнопок и ограничители полей (GC-якорь)
	int32 GiftItemCode = -1;               // +10728 [692] — код товара панели
	bool  bGiftVariants = false;           // [2080] — в наборе есть варианты (комбо профессии/уровня)
	TArray<int32> GiftSetCodes;            // +2690… коды `item_list` того же `setCode` (`categoryFlag == 3`), по профессиям
	TArray<int32> GiftJobs;                // +6782… различные `usageCategory` по возрастанию
	TArray<int32> GiftLvCodes;             // +6692… коды выбранной профессии по возрастанию уровня
	FDelegateHandle HGiftClear;            // `UAsdaCashGiftSubsystem::OnClearNameFields`
	FDelegateHandle HGiftReset;            // `UAsdaCashGiftSubsystem::OnWindowReset`
};
