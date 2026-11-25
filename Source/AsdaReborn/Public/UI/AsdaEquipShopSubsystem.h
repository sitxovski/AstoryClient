// Copyright (c) 2026 @sitxovski (Discord)
// UAsdaEquipShopSubsystem — МАГАЗИН СНАРЯЖЕНИЯ С ПРИМЕРКОЙ: окно `SHELLITEM_STORE2_WIN`
//   (JUI IDNum **6800**). Открывается кнопкой диалога NPC **30020** («Купить вещь» / «Купить вещи»
//   у `140npc` Патон, `shuren` Сюрен, `yazark` Язарк — все трое «Торговец снаряжением»).
//   Полный разбор потока с адресами — `Reports/equip_shop_port.md`.
//   Структурный разбор контролов — `Docs/UI/Trade/SHELLITEM_STORE2_WIN.md`.
//
// ═══════════════ ★★★ ЧТО ЭТО ЗА ОКНО И ЗА ЧТО ТАМ ПЛАТЯТ ═════════════════════════════════════
//   Имя окна врёт: к «shell item» (системе улучшения оболочек) оно отношения не имеет. Это
//   магазин СНАРЯЖЕНИЯ, витрина которого лежит в `./Data/NPC/Shop/Freeshop.she`
//   (`cShopLoadFreeShop@0xCAB270`). Валюта — ЗОЛОТО, опкоды **5020 → 5021**, ровно те же, что
//   у магазина NPC (окно 1300): тело запроса 42 б = 7 записей `{i32 itemCode; i16 amount}`.
//   Оба варианта покупки этого окна кладут `amount = −1` (для НЕстековых вещей сервер это поле
//   не читает — `CharItemBuy@0x50F170` смотрит его только у предметов с флагом «стек» +35).
//
//   ЛОГИКА ОКНА — «ПРИМЕРЬ, ПОТОМ КУПИ» (нотис открытия **6815** «Доспехи и оружие можно купить
//   после примерки.»):
//     • слева — витрина из 21 карточки с листанием и фильтрами;
//     • клик по КАРТОЧКЕ (не по её кнопке) — ПРИМЕРКА: предмет надевается на своего персонажа
//       (`itemOnUseDispatch@0xC267E0`, ветка 0x1AC9…0x1B46) и попадает в правый «Список
//       снаряжения»;
//     • справа — 7 строк «Списка снаряжения» (`shopRefreshSellSlotList@0xC3ACB0`): это ПРЕВЬЮ
//       надетого (типы предмета 0,1,2,3,4,8,9), у каждой строки галочка;
//     • «Итого» (6847) — сумма цен превью с налогом войны (`charEquip_SumOptionStats@0xC36A60`);
//     • кнопка **6851** «Надеть куплен. вещи» покупает ВЕСЬ список (до 7 записей);
//     • кнопка карточки **6859+6k** покупает ОДНУ вещь.
//
// ═══════════════ УСТРОЙСТВО ОКНА ═════════════════════════════════════════════════════════════
//   6801 крестик · 6803/6802 чекбокс «Искать предметы одежды» (loc 12016) · 6808/6807 чекбокс
//   «Искать для своего пола» (loc 12015) · 6856 меню из 9 вкладок · 6805/6804/6806 «стр/всего» ·
//   6853 вперёд, 6855 назад (6852/6854 — МЁРТВЫЕ, обработчиков нет) · 21 карточка 6857+6k
//   {бокс, имя 6858, кнопка 6859, цена 6860 (СКРЫТА кодом), иконка 6861, рамка выбора 6862} и
//   параллельный блок цены 79500+10k…79506+10k · 6810 заголовок «Список снаряжения» (loc 12906) ·
//   7 строк 6812+5j {фон-кнопка, имя, цена, галка-вкл, галка-выкл} · 6811 «Итого» (loc 12958) ·
//   6847 сумма · 6850 золото игрока · 6848 «Снять выбранный предмет» (loc 6945) ·
//   6849 «Сброс» (loc 6950) · 6851 «Надеть куплен. вещи» (loc 6957).
//
// ═══════════════ ДАННЫЕ ВИТРИНЫ (`Freeshop.she`, `cShopLoadFreeShop@0xCAB270`) ════════════════
//   ★★★ ГДЕ ОНИ В ПОРТУ (2026-08-26): 11 списков файла разобраны импортёром
//   `Tools/asda_import_equip_shop.py` в таблицу `/Game/AsdaAssets/Data/NPC/DT_EquipShopList`
//   (11 строк, ключ = номер списка). Рантайм читает ТОЛЬКО её; прямого чтения `.she` с диска
//   больше нет, сам файл перенесён из `Content/` в `RawAssets/Data/NPC/Shop/`.
//   11 × u16 счётчиков, затем 11 массивов `int32 itemCode`. Из массива №4 (предметы `itemType==9`)
//   загрузчик строит ТРИ производных списка по подтипу: `<4` → №11, `8…11` → №12, `4…7` → №13.
//   Итого 14 списков. Раскладка по странице считается ОДИН раз: `page = i / ItemsPerPage`,
//   `slotInPage = i % ItemsPerPage`, где `ItemsPerPage = uiList_GetVisibleRows2(ширина)@0xCAB100`.
//
//   МЕНЮ 6856 → номер списка (`shopRefreshSlotList@0xC376B0` / `shopBuildSlotItemList@0xC382F0`):
//     0 «Воин» (6915) → 11 · 1 «Лучник» (6916) → 12 · 2 «Маг» (6917) → 13 ·
//     3 «Доспех» (6443) → 5 · 4 «Перчатки» (6844) → 6 · 5 «Штаны» (6845) → 7 ·
//     6 «Щит» (6846) → 8 · 7 «Шлем» (6847) → 9 · 8 «Ботинки» (6848) → 10.
//   Списки 0…4 в этом окне не показываются вовсе.
//
// ═══════════════ ТРИ НАБОРА СПИСКОВ (чекбоксы взаимоисключающие) ══════════════════════════════
//   Эталон держит витрину в ТРЁХ экземплярах и переключает их флагами 10328/10329:
//     • БЕЗ фильтра — сырые массивы (`10474+k`, дескрипторы `10348+3k`);
//     • чекбокс 6803 — набор `10544+4k` / дескрипторы `10390+3k`. Строится в ДВА прохода:
//         `dispatchByOpcode@0xCAE2B0` → одна из 40 функций по КОДУ ПРОФЕССИИ
//         (`CCharacter+0x438`, у нас `UAsdaNetSubsystem::SelfClassId`) — фильтр по
//         `ItemRecord->usageCategory`; затем `inventoryBag_RebuildSlotGrids@0xCAE6E0` —
//         фильтр по ПОЛУ и УРОВНЮ. Итог: «что я вообще могу надеть»;
//     • чекбокс 6808 — набор `10488+4k` / дескрипторы `10432+3k`, строит
//         `CInventoryDlg_RebuildItemGrid@0xCB5330`: только ПОЛ (`classRestriction == 0 ||
//         == мой пол`), и ТОЛЬКО для списков 5,6,7,9,10 — списки 4, 8, 11, 12, 13 копируются
//         без фильтра (так в эталоне: щит и оружие пола не имеют).
//   ⚠ Поле `ItemRecord::ClassRestriction` (+0x58) в этом фильтре — ПОЛ (0 любой / 1 / 2), а не
//     класс: `CInventoryDlg_RebuildItemGrid` сравнивает его с `CCharacter::gender`.
//
// ═══════════════ ПОКУПКА ══════════════════════════════════════════════════════════════════════
//   Кнопка карточки 6859+6k (`uiHandleCommand@0xC23AC0`, LABEL_33): запоминает индекс карточки,
//   кладёт код предмета в поле 10756 и открывает ДВУХКНОПОЧНУЮ модалку с текстом loc **20060**
//   («[ss1] - купить?», слот 0 = имя предмета), тип содержимого 34.
//   Кнопка 6851 (`case 0x1AC3`): гейт «список не пуст» (`entry_HasAnyActiveOf7@0xC3D900`),
//   затем модалка с текстом loc **6889** («Купить надетый предмет?»), тип 35.
//   «ОК» модалки (`OnSendItemUpgrade@0xD74180`, msgId 1523): тип 34 →
//   `Anticheat_sendSecuredReport_5020@0xC3C770` (ОДНА запись), тип 35 →
//   `net_sendSecuredReport_5020@0xC3CD90` (до 7 записей списка). Оба — опкод 5020, тело 42 б.
//
// ═══════════════ ОТВЕТ 5021 (`Recv_ShopSell_Result_0@0xC3D430`) ═══════════════════════════════
//   Код 1 — успех: записи в контейнеры, деньги и вес (общая точка инвентаря), по строке
//   loc **20065** в чат на каждую позицию, модели возвращаются к реальным, список и «Итого»
//   очищаются. Код 2 → модалка loc 2233, код 3 → 1012, код 4 → 2048.
//   ★ Коды 0 и 5 в ЭТОМ окне эталон НЕ обрабатывает (в отличие от магазина NPC, где 0 → 9570,
//     5 → 9569) — «молчащий отказ». Дефект оригинала, исправлен под ключом отката
//     `asda.EquipShop.LegacySilentCodes 1` (см. .cpp).
//
// ═══════════════ ЧЕГО В ОРИГИНАЛЕ НЕТ (проверено, не выдумываем) ═════════════════════════════
//   • Клиентских проверок золота / веса / свободных ячеек / уровня перед 5020 — нет вовсе,
//     всё гейтирует сервер `CharItemBuy@0x50F170`.
//   • Окна ввода количества (NUMPAD_WIN 1410) — нет: снаряжение нестековое, `amount` = −1
//     (сверено с таблицей режимов `Docs/Port/systems/NUMPAD_FLOW.md` — этого окна там нет).
//   • Блок 6990…6996 («базовая цена / налог / итого») — контейнер 6990 `Visible = "0"`,
//     кодом не заполняется. Мёртвый.
//   • Блок 57011…57133 («цена в медалях / требование звания») — ни одного обращения в коде.
//   • Контрол 7001 (loc 13241 «Золото:») получает текст в init, но в `.JUI` он `Visible = "0"`,
//     а видимость в оригинале НАСЛЕДУЕТСЯ ⇒ блок «Золото игрока» (7001 + 6850 + монетка) в
//     эталоне НЕ ВИДЕН вовсе, хотя код его заполняет. Дефект Д5, исправлен
//     (`asda.EquipShop.LegacyHiddenGold 1` — вернуть как было).
//   • ПОДСКАЗОК ПРЕДМЕТА по наведению нет: у всех контролов окна `ToopTip = ""`, обработчика
//     сообщений 8/9/0xA у `CShellItemStore` нет. Подключены НАШЕЙ ДОБАВКОЙ по требованию
//     владельца — `asda.EquipShop.Tooltips 0` выключает (владелец плашки `EAsdaTipOwner::EquipShop`).
//   • Признака «окно поверх всех» в `.JUI` нет (`AlwaysTop` у 6800 = 0). Окно переведено в полосу
//     приоритетных окон 8800…8899 НАШЕЙ ДОБАВКОЙ (`SetWindowPriority`) — иначе оно открывалось бы
//     позади окна разговора с NPC, из которого его вызывают.
//   • РУЧКИ ПЕРЕТАСКИВАНИЯ в данных нет: EVS{EVT=6,RET=0x20003} во всём поддереве окна НОЛЬ.
//     Заведена через `DA_JuiWindowMeta` (`Tools/asda_jui_missing_drag_handles.py`), позиция
//     сохраняется общим механизмом `AccumulateOffset` → ini → `ApplySavedLayout`.
#pragma once

#include "CoreMinimal.h"
#include "AsdaTickGate.h"   // ★ R4-b (14.11.2026): шаблон класса (CDO) не тикает
#include "Subsystems/GameInstanceSubsystem.h"
#include "Tickable.h"
#include "UI/AsdaUiInputTypes.h"   // ★ R1: FAsdaMouseFrame / FAsdaDragArm / FAsdaDragGhost … — поля ввода (без всего AsdaUiHelpers.h)
#include "AsdaNetQueue.h"          // TAsdaNetQueue — очередь «колбэк копит, Tick применяет»
#include "UI/AsdaShopDblClick.h"   // FAsdaShopDblClick — примерка (= «Список снаряжения») двойным щелчком (10.10.2026)
#include "Engine/DataTable.h"   // FTableRowBase — витрина живёт в DataTable
#include "AsdaSessionReset.h"   // IAsdaSessionResettable — сброс состояния сессии
#include "AsdaEquipShopSubsystem.generated.h"

class UUserWidget;
class UWidget;
class UImage;
class UButton;
class UCheckBox;
class UTextBlock;
class UAsdaIconCellGrid;
class UAsdaButtonRelay;

// ═══ ОДИН СПИСОК ВИТРИНЫ КАК СТРОКА DataTable (`DT_EquipShopList`) ═══
//   В файле `Freeshop.she` списки идут ПОДРЯД и без имён — их отличает только порядковый номер
//   (0…10). Он же и ключ строки таблицы. ⚠ В таблице лежат ТОЛЬКО 11 списков ФАЙЛА; три
//   производных (11/12/13) строит рантайм по `itemType`/`itemSubType` из `item_list`, как эталон.
USTRUCT(BlueprintType)
struct ASDAREBORN_API FAsdaEquipShopListRow : public FTableRowBase
{
	GENERATED_BODY()

	// Порядковый номер списка в файле, 0…10 (он же ключ строки).
	UPROPERTY(BlueprintReadOnly, Category="Asda|EquipShop") int32 ListIndex = 0;
	// Коды предметов списка ровно в том порядке, в каком они лежат в файле (порядок значим:
	//   по нему считаются номер страницы и место в ней).
	UPROPERTY(BlueprintReadOnly, Category="Asda|EquipShop") TArray<int32> ItemCodes;
};

// ═══ ОДНА ЗАПИСЬ ВИТРИНЫ. 1:1 запись эталона (16 б): {u16 page; u16 slotInPage; u32 itemCode}. ═══
USTRUCT()
struct FAsdaEquipShopEntry
{
	GENERATED_BODY()
	UPROPERTY(Transient) int32 ItemCode = 0;
	UPROPERTY(Transient) int32 Page     = 0;   // номер СТРАНИЦЫ (не строки): i / ItemsPerPage
	UPROPERTY(Transient) int32 SlotInPage = 0; // i % ItemsPerPage
};

// ═══ ОДИН ИЗ 14 СПИСКОВ (эталонный «дескриптор вкладки» + его массив) ═══
USTRUCT()
struct FAsdaEquipShopList
{
	GENERATED_BODY()
	UPROPERTY(Transient) int32 TabCode = 0;    // 10,20,…,140 — пишется загрузчиком, кодом не читается
	UPROPERTY(Transient) int32 MaxPage = 0;    // максимальный номер страницы
	UPROPERTY(Transient) TArray<FAsdaEquipShopEntry> Items;
};

// ═══ ВИДЖЕТЫ ОДНОЙ КАРТОЧКИ (6857+6k и параллельный блок 79500+10k) ═══
USTRUCT()
struct FAsdaEquipShopCardUi
{
	GENERATED_BODY()
	/** ★ Узлы карточки, которые не показываются НИКОГДА (авторские заглушки `CaptionText` из `.JUI`,
	 *  например «Ёнгари»). Гасятся ПОСЛЕ общего показа поддерева — см. `ShowCard`. */
	UPROPERTY(Transient) TArray<TObjectPtr<UWidget>> AlwaysHidden;
	UPROPERTY(Transient) TArray<TObjectPtr<UWidget>> BoxPlanes;   // 6857+6k — 8 планов ResizeWindow
	UPROPERTY(Transient) TArray<TObjectPtr<UWidget>> SelPlanes;   // 6862+6k — 8 планов рамки выбора
	UPROPERTY(Transient) TArray<TObjectPtr<UWidget>> Parts;       // всё, что гасится вместе с карточкой
	UPROPERTY(Transient) TObjectPtr<UWidget>    NamePlane = nullptr;  // 6858+6k — подложка имени
	UPROPERTY(Transient) TObjectPtr<UTextBlock> NameText  = nullptr;  // рантайм-подпись имени
	UPROPERTY(Transient) TObjectPtr<UButton>    BuyButton = nullptr;  // 6859+6k
	UPROPERTY(Transient) TObjectPtr<UTextBlock> BuyCaption= nullptr;  // подпись кнопки (loc 6944)
	UPROPERTY(Transient) TObjectPtr<UTextBlock> PriceText = nullptr;  // 6860+6k — эталон её ГАСИТ
	UPROPERTY(Transient) TObjectPtr<UImage>     ItemIcon  = nullptr;  // 6861+6k (план 64×64)
	// Блок цены 79501…79506: три пары «подпись/значение», подписи и значения — рантайм-`TextBlock`
	//   над HIT-зонами (в `.JUI` у этих контролов ПУСТОЙ `ID`, импортёр текста для них не создал).
	UPROPERTY(Transient) TArray<TObjectPtr<UWidget>>    PriceZones;   // 7 HIT-зон (79500…79506)
	UPROPERTY(Transient) TArray<TObjectPtr<UTextBlock>> PriceTexts;   // 6 подписей (79501…79506)
	// ★★★ ВЕСЬ БЛОК ЦЕНЫ ЦЕЛИКОМ: контейнер-группа контрола 79500 и ВСЁ его поддерево (вложенные
	//   группы 79501…79506, их HIT-зоны, монетки `BoxIcon_Gold` и наши рантайм-подписи).
	//   Нужен потому, что в `.JUI` контрол 79500 помечен `Visible = "0"`, импортёр честно свернул
	//   всю его ветку (`asda_jui_generic_umg.py`, `eff_hidden` → `COLLAPSED`), а видимость в
	//   оригинале НАСЛЕДУЕТСЯ (`CControl_RenderRecursive`, ui_core.c:108944 — в ребёнка заходят
	//   только `if (CControl_IsVisible(child))`). Эталон включает блок КОДОМ:
	//   `CControl_SetVisible(pThis[13*slot + 665], 1)` в `uiItemListSlot_Populate@0xC3A200`,
	//   где 665 = 79500 (привязка `CShellItemStore_InitControls@0xC28820`, k-цикл 79500+10k).
	//   [0] — корень блока, остальное — поддерево.
	UPROPERTY(Transient) TArray<TObjectPtr<UWidget>>    PriceBlock;
	UPROPERTY(Transient) int32 ItemCode = 0;    // что показано (0 = карточка пуста)
};

// ═══ ВИДЖЕТЫ ОДНОЙ СТРОКИ «Списка снаряжения» (6812+5j) ═══
USTRUCT()
struct FAsdaEquipShopWearUi
{
	GENERATED_BODY()
	UPROPERTY(Transient) TObjectPtr<UButton>    Row     = nullptr;   // 6812+5j — фон-кнопка строки
	UPROPERTY(Transient) TObjectPtr<UTextBlock> NameTxt = nullptr;   // 6813+5j
	UPROPERTY(Transient) TObjectPtr<UTextBlock> PriceTxt= nullptr;   // 6814+5j
	UPROPERTY(Transient) TObjectPtr<UWidget>    CheckOn = nullptr;   // 6815+5j — галка «выбрано»
	UPROPERTY(Transient) TObjectPtr<UWidget>    CheckOff= nullptr;   // 6816+5j — галка «не выбрано»
	UPROPERTY(Transient) TObjectPtr<UWidget>    GoldIcon= nullptr;   // декоративная монета строки
	/** ★ ВСЕ узлы строки (собраны по префиксу имени). Именно по ним гасится ПУСТАЯ строка:
	 *  раскладка строк в `.JUI` неоднородна, и точечные ссылки выше покрывают не все планы —
	 *  непогашенные давали светлые «обводки» на пустом месте (жалоба владельца 2026-08-26). */
	UPROPERTY(Transient) TArray<TObjectPtr<UWidget>> Parts;
};

// ═══ СТРОКА «Списка снаряжения» — модель (эталонные поля 10687+3j) ═══
USTRUCT()
struct FAsdaEquipShopWearRow
{
	GENERATED_BODY()
	UPROPERTY(Transient) int32 ItemCode = -1;
	UPROPERTY(Transient) int32 Category = -1;   // itemType→категория эталона: 0→8,1→2,2→4,3→5,4→3,8→7,9→6
	UPROPERTY(Transient) bool  bChecked = false;
};

UCLASS()
class ASDAREBORN_API UAsdaEquipShopSubsystem : public UGameInstanceSubsystem, public FTickableGameObject,
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
	virtual TStatId GetStatId() const override { RETURN_QUICK_DECLARE_CYCLE_STAT(UAsdaEquipShopSubsystem, STATGROUP_Tickables); }
	// ★ R4-b (14.11.2026): шаблон класса (CDO) не тикает — раньше `Tick` звался дважды за кадр, у рабочего объекта и у шаблона
	//   (ключ `asda.Perf.NoCdoTick`, разбор — `AsdaTickGate.h`). Рабочий объект тикает как раньше.
	virtual bool IsTickable() const override { return AsdaTickGate::NotCdo(this); }
	virtual bool IsTickableInEditor() const override { return false; }
	virtual bool IsTickableWhenPaused() const override { return false; }

	// ── Числа окна (все из `.JUI` и эталона, ни одно не выдумано) ──
	static constexpr int32 kCards      = 21;   // 6857…6982 шагом 6
	static constexpr int32 kWearRows   = 7;    // 6812…6846 шагом 5
	static constexpr int32 kMenuTabs   = 9;    // 6856: OPT0…OPT8
	static constexpr int32 kLists      = 14;   // 11 из `.she` + 3 производных
	static constexpr int32 kEquipSlots = 11;   // снимок надетого (индекс = `ItemRecord->itemType`)
	static constexpr int32 kShopZ      = 732;  // выше инвентаря (720), магазина (730) и ремонта (731)

	// ═══ ОТКРЫТИЕ / ЗАКРЫТИЕ ═══
	UFUNCTION(BlueprintCallable, Category="Asda|EquipShop")
	void OpenWindow();                     // `shopWin_Open@0xC37110` + `tradeWinConfirmSubmit@0xC37CB0`
	UFUNCTION(BlueprintCallable, Category="Asda|EquipShop")
	void CloseWindow();                    // `shopWin_Close@0xC372C0`
	UFUNCTION(BlueprintPure, Category="Asda|EquipShop")
	bool IsOpen() const { return bOpen; }

	// ═══ ДЕЙСТВИЯ (публичны — их дёргают и мышь, и консольные команды) ═══
	void OnCardPreviewClicked(int32 Card);   // клик по карточке — ПРИМЕРКА
	void OnCardSelectOnly(int32 Card);       // [ПОРТ] 10.10: одиночный щелчок по карточке — только рамка выбора, без примерки
	// ── 10.10.2026: доступ самотеста `asda.Shop.SelfTestDblClick` (только чтение) ──
	void DebugArmBuy(int32 ItemCode) { PendingBuyCodes.Reset(); PendingBuyCodes.Add(ItemCode); }   // ★ 06.11.2026: самотест звуков — позиция ответа 5021
	int32 DebugWearFilled() const;                 // сколько строк «Списка снаряжения» занято
	int32 DebugSelCard() const { return SelCard; } // подсвеченная карточка (−1 = нет)
	int32 DebugCardCode(int32 Card) const;         // код товара карточки (0 = пусто)
	UWidget* DebugCardIcon(int32 Card) const;      // иконка карточки — точка симклика по живой геометрии
	void OnCardBuyClicked(int32 Card);       // 6859+6k — модалка покупки одной вещи
	void OnBuyWornClicked();                 // 6851
	void OnRemoveCheckedClicked();           // 6848
	void OnResetClicked();                   // 6849
	void OnWearRowClicked(int32 Row);        // 6812+5j — переключить галочку
	void SetMenuTab(int32 Tab);              // 6856
	void SetPage(int32 NewPage);             // 6853 / 6855
	void SetFilter(int32 Which, bool bOn);   // Which: 0 = чекбокс 6803, 1 = чекбокс 6808

	// ══════════════════════════════════════════════════════════════════════════════════════════
	// ★★★ ОБЩАЯ ТОЧКА ДВУХ ВИТРИН ОДНОГО КЛАССА `CShellItemStore` (закон `port-shared-points`).
	//   Окна **6800** (эта подсистема, за золото, `Freeshop.she`) и **6000** (премиум-молл, за GXP,
	//   `Premium.she`) обслуживает ОДИН эталонный класс и ОДИН загрузчик витрины: разница между
	//   `cShopLoadFreeShop@0xCAB270` и `shopPremiumMall_LoadShe@0xBD6460` — только имя файла.
	//   Поэтому разбор витрины и фильтр по профессии живут ЗДЕСЬ и переиспользуются моллом
	//   (`UAsdaPremiumShopSubsystem`), а не копируются во второй магазин.
	//
	//   `TablePath` — DataTable со строками `FAsdaEquipShopListRow` (11 списков ФАЙЛА);
	//   `OutRaw` получает 14 списков: 11 из файла + три ПРОИЗВОДНЫХ (11 «Воин», 12 «Лучник»,
	//   13 «Маг»), которые строятся из списка №4 по `itemType == 9` и `itemSubType`
	//   (`<4` → 11, `8…11` → 12, `4…7` → 13) — ровно как эталонный загрузчик.
	//   `Tag` — префикс сообщений в журнале («магазин снаряжения» / «премиум-магазин»).
	static bool LoadSheListsFromTable(const TCHAR* TablePath, const class UAsdaItemTableSubsystem* IT,
		TArray<TArray<int32>>& OutRaw, const TCHAR* Tag);

	// Сколько ПРЕДМЕТОВ помещается на страницу витрины (`uiList_GetVisibleRows2@0xCAB100`) и
	//   сколько РЯДОВ у панели сетки (`uiList_GetVisibleRows@0xBD6270`). Обе лесенки эталон
	//   применяет к ОБОИМ окнам одного класса, поэтому они тоже общие точки, а не частность 6800.
	int32 ItemsPerPage() const;           // `uiList_GetVisibleRows2@0xCAB100`
	int32 PanelRows() const;              // `uiList_GetVisibleRows@0xBD6270`

	// Фильтр «что вообще может надеть эта профессия» — таблица из сорока эталонных функций,
	//   которые выбирает `dispatchByOpcode@0xCAE2B0` по коду профессии `CCharacter+0x438`.
	//   Публична по той же причине: она не про окно 6800, а про данные.
	static bool JobAllowsUsage(int32 JobCode, int32 UsageCategory);

	// ═══ ДИАГНОСТИКА ═══
	FString DumpControls() const;
	FString DumpState() const;

private:
	// ── Данные ──
	// Витрина из DT_EquipShopList + три производных списка (было: чтение `Freeshop.she` с диска).
	bool LoadShe();                       // `cShopLoadFreeShop@0xCAB270`
	void BuildAllSets();                  // раскладка страниц + три набора фильтров
	void BuildSet(int32 SetIndex);        // 0 базовый, 1 «одежда» (6803), 2 «свой пол» (6808)
	int32 ActiveSet() const { return bFilterGender ? 2 : (bFilterWear ? 1 : 0); }
	const FAsdaEquipShopList& ActiveList() const;

	// ── Жизненный цикл окна ──
	bool EnsureWidget();
	void ResetWidget();
	void SetupControls();
	void RegisterZone();
	UTextBlock* MakeLabelLike(UWidget* Ref, const TCHAR* NewName, const UTextBlock* FontDonor, bool bRightAlign);

	// ── Рендер ──
	void BuildPage();                     // `shopRefreshSlotList@0xC376B0` + `uiItemListSlot_Populate@0xC3A200`
	void PopulateCard(int32 Card, int32 ItemCode);
	void ShowCard(int32 Card, bool bVisible);
	void ShowCardSelection(int32 Card, bool bVisible);
	// Включить/выключить блок цены карточки целиком — 1:1 `SetVisible(79500, …)` эталона.
	void ShowPriceBlock(int32 Card, bool bVisible);
	// `shopWin_Open@0xC37110`: высота контейнера 6983 = `98 * (PanelRows() / 3) + 48`.
	void ApplyPanelHeight(int32 VisibleCards = 0);
	void RefreshPageLabels();             // `tradeWinUpdatePageLabels@0xC37BC0`
	void RefreshGoldText();               // 6850
	void RefreshWearList();               // `shopRefreshSellSlotList@0xC3ACB0`
	void CompactWearList();               // `uiSevenSlotPanelRefresh@0xC3B930` (кнопка 6848)
	int32 WearTotal() const;              // `charEquip_SumOptionStats@0xC36A60`

	// ── Примерка ──
	void SnapshotEquip();                 // `invCopyFieldsFromBagClass@0xC27E90`
	void PreviewItem(int32 ItemCode);     // `itemOnUseDispatch@0xC267E0` (ветка карточек)
	void ApplyPreviewModels(bool bClosing);   // `charApplyEquipPartsModels@0xC28440`
	void ClearPreview();                  // обнуление массива превью (`memset(this + 2496, 0, 0x2C)`)

	// ── Сеть ──
	void SendBuyOne();                    // `Anticheat_sendSecuredReport_5020@0xC3C770`
	void SendBuyWorn();                   // `net_sendSecuredReport_5020@0xC3CD90`
	void OnBuyResult(const TArray<uint8>& Body406);   // `Recv_ShopSell_Result_0@0xC3D430`
	void DrainPendingPackets();

	// ── Покадровые опросы ──
	void TickCards();
	// Подсказка предмета по наведению — НАША ДОБАВКА (см. `EAsdaTipOwner::EquipShop`).
	void TickTooltip(int32 OverCard, int32 OverRow);

	// ── Нотисы ──
	FString Loc(int32 LocId) const;
	void NoticeChat(int32 LocId) const;
	void NoticeMsgBox(int32 LocId) const;

	// ── Утилиты ──
	int32 ApplyWarPercent(int32 Value) const;   // `uniconWar_ApplyOptionPercent@0xBCEEC0` (a2 = 0)
	static int32 CategoryFromItemType(int32 ItemType);

	// ═══ СОСТОЯНИЕ ═══
	UPROPERTY(Transient) TObjectPtr<UUserWidget> Widget = nullptr;
	UPROPERTY(Transient) TArray<FAsdaEquipShopCardUi> Cards;
	UPROPERTY(Transient) TArray<FAsdaEquipShopWearUi> WearUi;
	UPROPERTY(Transient) TObjectPtr<UButton>    BtnClose = nullptr;   // 6801
	UPROPERTY(Transient) TObjectPtr<UButton>    BtnNext  = nullptr;   // 6853 NEXTP
	UPROPERTY(Transient) TObjectPtr<UButton>    BtnBack  = nullptr;   // 6855 BACKP
	UPROPERTY(Transient) TObjectPtr<UButton>    BtnRemove= nullptr;   // 6848 BUT00
	UPROPERTY(Transient) TObjectPtr<UButton>    BtnReset = nullptr;   // 6849 BUT01
	UPROPERTY(Transient) TObjectPtr<UButton>    BtnBuyWorn=nullptr;   // 6851 BUT02
	UPROPERTY(Transient) TObjectPtr<UCheckBox>  ChkWear  = nullptr;   // 6803
	UPROPERTY(Transient) TObjectPtr<UCheckBox>  ChkGender= nullptr;   // 6808
	UPROPERTY(Transient) TArray<TObjectPtr<UButton>>    MenuButtons;  // 6856 → OPT0…OPT8
	UPROPERTY(Transient) TArray<TObjectPtr<UTextBlock>> MenuLabels;   // рантайм-подписи вкладок
	// Базовые цвета подписей вкладок — снимает общая точка `AsdaUi::ApplyTabCaptionState`.
	TArray<FLinearColor> MenuIdleColors;
	UPROPERTY(Transient) TObjectPtr<UTextBlock> TxtChkWear  = nullptr;// 6802 (loc 12016)
	UPROPERTY(Transient) TObjectPtr<UTextBlock> TxtChkGender= nullptr;// 6807 (loc 12015)
	UPROPERTY(Transient) TObjectPtr<UTextBlock> TxtPage  = nullptr;   // 6805
	UPROPERTY(Transient) TObjectPtr<UTextBlock> TxtPages = nullptr;   // 6806
	UPROPERTY(Transient) TObjectPtr<UTextBlock> TxtTotal = nullptr;   // 6847
	UPROPERTY(Transient) TObjectPtr<UTextBlock> TxtGold  = nullptr;   // 6850
	UPROPERTY(Transient) TObjectPtr<UTextBlock> TxtWearHdr = nullptr; // 6810 (loc 12906)
	UPROPERTY(Transient) TObjectPtr<UTextBlock> TxtSumHdr  = nullptr; // 6811 (loc 12958)
	UPROPERTY(Transient) TObjectPtr<UTextBlock> CapRemove  = nullptr; // подпись 6848 (loc 6945)
	UPROPERTY(Transient) TObjectPtr<UTextBlock> CapReset   = nullptr; // подпись 6849 (loc 6950)
	UPROPERTY(Transient) TObjectPtr<UTextBlock> CapBuyWorn = nullptr; // подпись 6851 (loc 6957)
	UPROPERTY(Transient) TArray<TObjectPtr<UAsdaButtonRelay>> Relays;
	// 9 планов рамки контейнера 6983 (`Shellitem_Store2_Win_Part2_15_0…8`) — их высоту двигает
	//   `ApplyPanelHeight` ровно так, как эталонный `shopWin_Open`.
	UPROPERTY(Transient) TArray<TObjectPtr<UWidget>> PanelPlanes;
	// Виджеты блока «золото игрока» (контрол 7001 + его поддерево 6850/TXT12/буллет). В `.JUI`
	//   7001 помечен `Visible = "0"` ⇒ в ОРИГИНАЛЕ золото в этом окне не видно вовсе (дефект Д5).
	UPROPERTY(Transient) TArray<TObjectPtr<UWidget>> GoldBlock;
	UPROPERTY(Transient) TObjectPtr<UAsdaIconCellGrid> IconGrid = nullptr;   // 21 иконка товара

	// ── Модель ──
	// Три набора × 14 списков: [0] без фильтра, [1] чекбокс 6803, [2] чекбокс 6808.
	UPROPERTY(Transient) TArray<FAsdaEquipShopList> Sets;         // 3*14 подряд, индекс = Set*14 + k
	UPROPERTY(Transient) TArray<FAsdaEquipShopWearRow> WearRows;  // 7 строк «Списка снаряжения»
	// Сырые коды: 11 категорий из DT_EquipShopList + 3 производных (по подтипу `itemType==9`).
	TArray<TArray<int32>> RawLists;

	bool  bOpen = false;
	bool  bControlsReady = false;
	bool  bSheLoaded = false;
	bool  bPending = false;              // ждём 5021 — повторный запрос не уходит
	// ★ Волна В4 ремесла (ревизия В3 С-1): собственный мини-снимок окна (`uiMarkPendingWindowOpen@0xC274C0`, поля
	//   [2089]/[2090] `CShellItemStore`): открыта ли была сумка / окно персонажа при открытии магазина — их вернёт
	//   закрытие (`uiShellStore_RefreshOpenWindows@0xC27550` → `uiOpenShellStoreOrInventory`).
	bool  bReopenBagOnClose = false;     // [2089] — сумка
	bool  bReopenCharaOnClose = false;   // [2090] — окно персонажа
	bool  bFilterWear = false;           // флаг чекбокса 6803 (эталонное поле 10328)
	bool  bFilterGender = false;         // флаг чекбокса 6808 (эталонное поле 10329)
	int32 MenuTab = 0;                   // 10729
	int32 MenuTabPrev = -2;              // 10730
	int32 Page = 0;                      // 10731
	int32 MaxPage = 0;                   // 10732
	int32 SelCard = -1;                  // 10734 — подсвеченная карточка
	int32 PendingItemCode = 0;           // 10756 — что уходит в 5020 одной записью
	int32 BuiltForPixelWidth = 0;        // на какой ширине экрана посчитаны страницы
	int32 PanelHeightApplied = 0;        // высота рамки 6983, уже раскатанная на планы (0 = не трогали)
	int32 TipCard = -1;                  // что показано в плашке: 0…20 карточка, 100+j строка списка

	// Примерка: реальный снимок и текущее превью, индекс = `ItemRecord->itemType` (0…10).
	TArray<int32> RealEquip;
	TArray<int32> PreviewEquip;
	TArray<int32> PreviewApplied;        // что уже раскатано на модель

	// Коды позиций, ушедших в 5020 — из них строятся строки чата 20065 по успешному ответу.
	TArray<int32> PendingBuyCodes;

	FAsdaMouseFrame Mouse;
	// [ПОРТ] 10.10.2026: распознаватель двойного щелчка по карточке (ключ — номер карточки 0…20).
	FAsdaShopDblClick CardDbl;
	TAsdaNetQueue<TArray<uint8>> PendingBuy;
	FDelegateHandle HBuyResult;
	// ★ 18.10.2026: смена профессии 5042 (`OnSelfJobChanged`) — эталон `inventoryBag_DispatchRebuild(g_pShopWnd)` +
	//   `invBagRebuildItemGrids(g_pShopWnd)` (network.c:22019-22020): наборы витрины пересобираются под новый класс.
	//   Колбэк только взводит защёлку, пересборка — в Tick (закон `no-reentrant-net-callbacks` этого файла).
	FDelegateHandle HJobChanged;
	bool bClassRebuildPending = false;
};
