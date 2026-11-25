// Copyright (c) 2026 @sitxovski (Discord)
// AsdaItemTooltipSubsystem.h — ПОРТ ГЛОБАЛЬНОГО ОБЪЕКТА `g_ItemTooltip` (класс `CItemTooltipWnd`)
//   и правой инфо-панели окна инвентаря.
//
// ════════════════ ЧТО ЭТО В ОРИГИНАЛЕ ════════════════
// Один глобальный объект, инициализируемый `CItemTipWin_InitUI@0xEA15E0` (item_inventory.c:59437),
// держит ДВА окна JUI (кэш контролов — по индексам `this[N]`):
//   this[1]=5000 `ITEM_TIPWIN`        this[3]=5001 `ITEM_TIPTITLE`   this[4]=5002 `ITEM_TIPCONTENS`
//   this[5]=5003 `ITEM_TIP_2`         this[6]=5004 (нотис 109831)    this[7]=5007 (значок премиума)
//   this[8]=5005 `ITEM_TIP_3`         this[9]=5006 (нотис 109832)
//   this[2]=5050 `ItemCompareTip_Win` this[10]=5051 Title            this[11]=5052 Contents
//   this[12]=5053  this[13]=5054 (нотис 109830)  this[14]=5057  this[15]=5055  this[16]=5056
//   this[66]=высота глифа, this[67]=межстрочный интервал, this[43]/this[44]=w/h, this[64]/[65]=x/y
//
// ★★★ СРАВНЕНИЕ С НАДЕТЫМ — ЕСТЬ (прошлый реверс утверждал обратное, это ИСПРАВЛЕНО).
//   Доказательства:
//     1) `ItemTooltip_Build@0xEA5260` имеет параметры `suppressCompare` и `useSecondaryWindow`;
//        при `!suppressCompare && !useSecondaryWindow` считает `canCompareEquipped` и в конце
//        (item_inventory.c:60703) РЕКУРСИВНО зовёт себя же с `useSecondaryWindow = 1`;
//     2) второе окно — `ItemCompareTip_Win` (5050), его подпись 5054 в .JUI = «착용 아이템»
//        («НАДЕТЫЙ ПРЕДМЕТ»), подпись основного 5004 = «비교 아이템» («предмет для сравнения»);
//     3) `Tooltip_LayoutNearCursor@0xEA26F0` ставит 5050 ВПЛОТНУЮ СПРАВА от 5000
//        (`x = width(5000) + x0`) и складывает ширины перед клампом к экрану.
//   Правила `canCompareEquipped` (item_inventory.c:60450-60469):
//     • слот экипировки = `ItemRecord->itemType`, требуется `itemType <= 9` и надетое `> 0`;
//     • `itemType` наведённого != `itemType` надетого → 0;
//     • `itemType == 9` и `itemSubType` не совпали → 0;
//     • код ЛЮБОГО из двух == 22818 или 22819 → принудительно 1.
//   Для НАДЕТОГО предмета `equipSlotUpdateTooltip@0xC6AFB0` зовёт билдер с `suppressCompare = 1`
//   (сравнивать надетое с самим собой не нужно).
//
// ════════════════ ИЗ ЧЕГО СТРОИТСЯ ТЕЛО ТУЛТИПА ════════════════
// `Item_BuildTooltipText@0xEC2890` (item_inventory.c:62253) собирает 18 «секций» в буфер
// `textBuf[260*i]` и склеивает их в ЖЁСТКОМ порядке
//   11, 17, 5, 0, 1, 2, 3, 4, 7, 8, 9, 10, 6, 12, 13, 14, 15, 16
// (секция 10 в срезе НИКЕМ не пишется — «ссылок в срезе нет», оставлена пустой).
// Состав секций и id строк — в комментариях к `EAsdaTipSection` в .cpp.
//
// ════════════════ ЦВЕТА (ARGB, sRGB → в UE конвертируем AsdaUi::SrgbU32ToLinear) ════════════════
//   0xFFF45151  «нельзя по классу» / «мало уровня» / цена   (colorTitle/colorReqLevel/priceColor)
//   0xFFFAD508  «не передаётся» (bound/sealed state)         (colorBound/stateColor)
//   0xFFF4D9A4  дефолтный цвет заголовка ДО грейда          (titleDefColor)
//   0xFF3D9AF8  примечание «запечатан» (нотис 2044)         (colorSealed)
//   0xFF00FE00  зарезервирован эталоном, не используется     (colorGreen)
//   0xFFFDBC49  строки опций самоцветов                      (colorOption)
//   0xFF01DF0B  строки статов предмета                       (item_buildStatTooltip)
//   0xFFFFFF99 / 0xFFFFCC66  парные статы (тип 96 / 97, нотисы 111054 / 111055)
//   0xFF00BFF3  опции спец-предмета / сет-бонус              (Item_BuildOptionTooltip)
//   0xFFFF0000  прочность == 0
//   ГРЕЙД имени — ТА ЖЕ таблица, что у наземного лута, `CItemManager_GetDropColor@0x9C45C0`:
//     0 0xFFFFFFFF · 1 0xFFFFFF46 · 2 0xFFFF96DC · 3 0xFF96FF32 · 4 0xFFFF7800 · 5 0xFF4141FF
//     6 0xFFBE50FF · 7 0xFFFA1E46 · 8 0xFF9BEBEB · >8 → элемент 0.
//   ЗАТОЧКА (`itemGetGradeColor@0xD29940`): деф 0xFFFEF078; 5 0xFFFFFFFF; 6 0xFFFFFF46;
//     7 0xFFFF96DC; 8 0xFF96FF32; 9 0xFFFF7800.
#pragma once
#include "CoreMinimal.h"
#include "AsdaTickGate.h"   // ★ R4-b (14.11.2026): шаблон класса (CDO) не тикает
#include "Subsystems/GameInstanceSubsystem.h"
#include "Tickable.h"
#include "UI/AsdaTipPanel.h"
// FAsdaRichApplyOpts / FTextBlockStyle / ETextJustify   ★ R3: комментарий снятого включения "UI/AsdaMarkup.h" (файл им не пользовался) — сохранён
#include "UI/AsdaUiInputTypes.h"   // ★ R1: FAsdaMouseFrame / FAsdaDragArm / FAsdaDragGhost … — поля ввода (без всего AsdaUiHelpers.h)
#include "Framework/Text/TextLayout.h"   // ETextJustify
#include "Components/SlateWrapperTypes.h"   // ESlateVisibility
#include "AsdaSessionReset.h"   // IAsdaSessionResettable — сброс состояния сессии
#include "AsdaItemTooltipSubsystem.generated.h"

struct FAsdaItemInstance;
struct FAsdaItemRecord;
class UUserWidget;
class UWidget;
class UTextBlock;
class UImage;
class UProgressBar;
class UAsdaItemTableSubsystem;
class UAsdaTextSubsystem;
class UAsdaInventorySubsystem;   // владелец правой инфо-панели (Д1 круга 6: сторожим её раскрытие)
class IConsoleObject;

// Готовый разобранный тултип: заголовок (с цветом грейда) + тело с inline-разметкой.
USTRUCT(BlueprintType)
struct ASDAREBORN_API FAsdaItemTooltipText
{
	GENERATED_BODY()
	UPROPERTY(BlueprintReadOnly, Category="Asda|Tooltip") FString Title;        // 5001 (с тегами заточки)
	UPROPERTY(BlueprintReadOnly, Category="Asda|Tooltip") FLinearColor TitleColor = FLinearColor::White;
	UPROPERTY(BlueprintReadOnly, Category="Asda|Tooltip") FString Body;         // 5002 (18 секций, с тегами)
	UPROPERTY(BlueprintReadOnly, Category="Asda|Tooltip") bool bValid = false;
	// Тот же цвет заголовка, но в ИСХОДНОМ sRGB-ARGB (плашка ждёт именно его; обратная конверсия
	//   из `TitleColor` теряет точность). Без UPROPERTY: uint32 рефлексия UE не поддерживает.
	uint32 TitleArgb = 0xFFFFFFFF;
};

UCLASS()
class ASDAREBORN_API UAsdaItemTooltipSubsystem : public UGameInstanceSubsystem, public FTickableGameObject,
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
	virtual TStatId GetStatId() const override { RETURN_QUICK_DECLARE_CYCLE_STAT(UAsdaItemTooltipSubsystem, STATGROUP_Tickables); }
	// ★ R4-b (14.11.2026): шаблон класса (CDO) не тикает — раньше `Tick` звался дважды за кадр, у рабочего объекта и у шаблона
	//   (ключ `asda.Perf.NoCdoTick`, разбор — `AsdaTickGate.h`). Рабочий объект тикает как раньше.
	virtual bool IsTickable() const override { return AsdaTickGate::NotCdo(this); }
	virtual bool IsTickableInEditor() const override { return false; }
	virtual bool IsTickableWhenPaused() const override { return false; }

	// ══ 1. СБОРКА ТЕКСТА (чистая функция над данными — её же гоняют консольные проверки) ══
	//   bNoTitleLine  — эталонный аргумент `noTitle` (секция 0 «Профессия» не пишется);
	//   bNotBound     — эталонный аргумент `notBound` (0 ⇒ принудительная строка «не передаётся»).
	FAsdaItemTooltipText BuildTooltip(const FAsdaItemInstance& It, bool bNoTitleLine = false,
	                                  bool bNotBound = true) const;

	// Упрощённый путь эталона `itemTooltipBuildByCode@0xEA6B80`: заголовок = имя предмета,
	//   тело = описание из `Item_tooltip.BIN`. Используется, когда 57-байтовой записи нет.
	FAsdaItemTooltipText BuildTooltipByCode(int32 ItemCode) const;

	// ★★★ ВЕТКА ЗАПИСЕЙ ВТОРОЙ ТАБЛИЦЫ в `ItemTooltip_Build` (item_inventory.c:60430-60697).
	//   Эталон в САМОМ НАЧАЛЕ билдера проверяет `код ∈ [g_Table2CodeFirst; g_Table2CodeLast]`
	//   (`special_itemlist`, коды 1…558) и в этом случае НЕ ЗОВЁТ `Item_BuildTooltipText` вовсе:
	//   у спец-предмета нет ни статов, ни гнёзд, ни прочности — только «Вес», «Тип», «Уровень»,
	//   строка состояния и описание. Порт этой ветки не знал, поэтому у любого премиум/бустер-
	//   предмета `GetItem()` отдавал nullptr и тултип не показывался ВООБЩЕ.
	FAsdaItemTooltipText BuildTooltipTable2(const FAsdaItemInstance& It, FAsdaTipMetrics& OutM) const;

	// ★★★ ПРОСТОЙ ТУЛТИП ПО ТИПУ ПРЕДМЕТА — порт `CItem_BuildSowelTooltipText@0xEA2EC0`
	//   (item_inventory.c:59497). ВТОРОЙ, полностью отдельный пайплайн эталона: в него ведёт
	//   `CControl_SetState@0xEA1D30` → `tooltipBuildByTypeAndShow@0xEA2590`, и состав тултипа
	//   там ЗАВИСИТ ОТ `ItemRecord->itemType` (см. таблицу в .cpp). Через него идут:
	//     • вкладка «Расходники» окна инвентаря (`invSlotBuildTooltip@0xD06670`, ветка
	//       `gap_1B0[84] == 0`, item_inventory.c:37425-37465);
	//     • надетые слоты с `itemType` 5..7 и 10..11 (`equipSlotUpdateTooltip@0xC6AFB0`,
	//       item_inventory.c:22790-22808).
	//   Quantity — эталонный `a3` (количество из записи слота, +9), BindArg — эталонный `a4`.
	FAsdaItemTooltipText BuildTooltipSimple(int32 ItemCode, int32 Quantity, int32 BindArg,
	                                        FAsdaTipMetrics& OutM) const;

	// ★ [ИСПРАВЛЕНИЕ] баг 11 (01.11.2026, ключ `asda.ItemTip.BagNonGearSimple`): премиум-предмет (categoryFlag 3) — не снаряжение
	//   (itemType 10/11: бустеры, наборы, прочее), показанный «полным путём» (SourceKind 1: вкладка «Снаряжение/Премиум» и окна,
	//   что зовут полный билдер), получает подсказку простого билдера вместо полей снаряжения. true — этот путь включён для `R`.
	bool UsesBagNonGearTip(const FAsdaItemRecord* R) const;
	// Подсказка такого предмета: состав простого билдера типа 11 (имя, «Уровень» по `asda.ItemTip.LevelLine`, «Вес», описание)
	//   плюс строки полного билдера, без которых предметы не различить: «Профессия» (UsageCategory не 0/40, красная при чужом
	//   классе) и привязка (8401 при байте +50, иначе 8503 при TooltipFlag200), премиум-подвал 109832. Общий для игры
	//   (`ShowItemTooltip`) и редакторского просмотрщика (`AsdaItemTipStandalone`).
	FAsdaItemTooltipText BuildTooltipBagNonGear(const FAsdaItemInstance& It, FAsdaTipMetrics& OutM) const;

	// ★★★ ТУЛТИП САМОЦВЕТА — ПЕРВАЯ половина того же `CItem_BuildSowelTooltipText@0xEA2EC0`
	//   (item_inventory.c:59782-59995). Отдельный состав: «Профессия», «Доступное оружие»
	//   (19-битная маска +0x44), «Тип», «Уровень», «Вид», «Место в снаряжении», «Вес», строка
	//   владельца и блок «следующий грейд». Ширина 220, строк = перенос(206) + 2.
	//   ⚠ Полю «Вес» (`sowelRecord->tooltipParam`) в декодере НЕ сопоставлена колонка — читаем
	//   байт +38 напрямую из `DataHex` (`UAsdaItemTableSubsystem::SowelRawByte`), смещение снято
	//   с бинаря, а не угадано (см. комментарий у `SowelRawByte`).
	FAsdaItemTooltipText BuildTooltipSowel(int32 ItemCode, FAsdaTipMetrics& OutM) const;

	// ★ Эталонное правило маршрутизации НАДЕТОГО слота (`equipSlotUpdateTooltip`):
	//   `itemType` 5,6,7 и 10,11 → простой тултип; всё остальное → полный `ItemTooltip_Build`.
	static bool ItemTypeUsesSimpleTooltip(int32 ItemType)
	{ return (ItemType >= 5 && ItemType <= 7) || ItemType == 10 || ItemType == 11; }

	// ══ 2. ПОКАЗ ══
	// Тултип предмета у курсора. bSuppressCompare = эталонный `suppressCompare`
	//   (у НАДЕТОГО предмета = true, `equipSlotUpdateTooltip`).
	//   Owner — кто просит (см. EAsdaTipOwner): скрыть плашку сможет только он.
	//   SourceKind — ОТКУДА предмет (эталонный `slotSource`): 1 = вкладка «Снаряжение»
	//   (`g_InventoryBag`), 2 = вкладка «Расходники» (`g_InventoryArray2`), 3 = надетое.
	//   ★ От него зависит ПАЙПЛАЙН тултипа, а не только оформление (см. `BuildTooltipSimple`).
	void ShowItemTooltip(const FAsdaItemInstance& It, bool bSuppressCompare,
	                     EAsdaTipOwner Owner = EAsdaTipOwner::Item, int32 SourceKind = 1);
	// То же по коду предмета (нет записи слота).
	void ShowItemTooltipByCode(int32 ItemCode, EAsdaTipOwner Owner = EAsdaTipOwner::Item);
	// ★★★ ТУЛТИП ЯЧЕЙКИ ПРЕДПРОСМОТРА (окно «Вид» бустера 15600, панель состава пака 26500) —
	//   порт `uiInventoryShowSlotTooltip@0xB87200` (ui_core.c:10433-10513). У этих ячеек НЕТ
	//   57-байтовой записи слота, поэтому эталон СОБИРАЕТ ЕЁ НА СТЕКЕ (`CSlot_Init(v10)` = нули)
	//   и кладёт туда поля ШАБЛОНА, после чего зовёт ОБЫЧНЫЙ построитель
	//   `ItemTooltip_Build(&g_ItemTooltip, slot, −1, 0, 1, 1, 0, 0)` — тот же, что у вещей сумки,
	//   то есть с полным составом характеристик. Смещения синтетической записи сняты с бинаря
	//   (0xB87200, база кадра `ebp−0x40`), а не с порядка локалов Hex-Rays:
	//     обычный предмет   +0 = itemCode · +13 = ItemRecord[+0x44] (прочность-максимум)
	//                       +14 i16 = ItemRecord[+0x48] (вес единицы) · +50 = ItemRecord[+0xC8]
	//       asm: `8945c0` · `8a4844/884dcd` · `668b4848/66894dce` · `8a88c8000000/884df2`
	//     спец-предмет      +0 = code · +9 i32 = Table2[+0x58] (число применений)
	//                       +14 i16 = Table2[+0x44] · +50 = Table2[+0x7C]
	//       asm: `0fb74858/894dc9` · `668b4844/66894dce` · `8a487c/884df2`
	//     самоцвет          `CItem_BuildSowelTooltipText` + `uiTooltipShowAndLayout`
	//   ⚠ Это НЕ `ShowItemTooltipByCode`: тот повторяет `itemTooltipBuildByCode@0xEA6B80`
	//   («имя + описание», без характеристик), и подключение его к предпросмотру и было дефектом.
	void ShowPreviewSlotTooltip(int32 ItemCode, EAsdaTipOwner Owner = EAsdaTipOwner::Item);
	// ★ U17 (В1 дедупа №2): синтетическая запись `ShowPreviewSlotTooltip` отдельно от показа — ради побайтного самотеста
	//   `asda.Dedup.W1ASelfTest` (код → 57 байт: `CSlot_Init` + прочность/вес/флаг из item_list либо special_itemlist).
	TArray<uint8> BuildPreviewSlotRecord(int32 ItemCode) const;
	// ★★★ ТУЛТИП ЯЧЕЙКИ ПАНЕЛИ СОСТАВА ПАКА/КОРОБКИ (окно 26500) — порт
	//   `stampWnd_ShowItemTooltip@0xDB29B0` (item_enhance.c:3680-3741). ЭТО ОТДЕЛЬНАЯ ФУНКЦИЯ
	//   ЭТАЛОНА, а не копия `uiInventoryShowSlotTooltip`, и правила у неё ДРУГИЕ:
	//     • запись `item_list`, `itemType > 9`  → `CControl_SetState(&g_ItemTooltip, 0, code, 0, 1, −1, 0)`
	//       = ПРОСТОЙ билдер `CItem_BuildSowelTooltipText` (у нас `BuildTooltipSimple`);
	//     • запись `item_list`, `itemType <= 9` → полный `ItemTooltip_Build(slot, −1, 0, 1, 1, 0, 0)`;
	//     • запись `special_itemlist`           → тот же полный билдер (ветка ВТОРОЙ таблицы);
	//     • запись `sowell_list`                → `CControl_SetState(…, 1, code, …)` = билдер самоцвета.
	//   ⚠ И синтетическая запись здесь БЕДНЕЕ: эталон пишет в неё ТОЛЬКО `slot[0] = code`
	//   (`CSlot_Init(v6); v6[0] = ItemRecord->itemCode;`) — ни прочности, ни веса, ни флага +50.
	void ShowStampSlotTooltip(int32 ItemCode, EAsdaTipOwner Owner = EAsdaTipOwner::Item);
	// ★★★ ТУЛТИП ЯЧЕЙКИ ВИТРИНЫ ПРИЗОВ ПРЕМИУМ-ГАЧИ (68311…68335) и ИКОНКИ ПРИЗА (68453) — порт
	//   `cItemBar_BuildSlotTooltip@0xB6F3C0` (item_inventory.c:5422-5474). ТРЕТЬЯ отдельная функция
	//   эталона, и разветвление у неё СВОЁ (ни `uiInventoryShowSlotTooltip`, ни `stampWnd_…`):
	//     • запись `item_list` и (`itemType <= 4` ИЛИ `8…9` ИЛИ `categoryFlag == 3`)
	//         → `ItemTooltip_Build(&g_ItemTooltip, slot, v8=0, 0, 1, 1, 1, 0)` = ПОЛНЫЙ билдер;
	//     • запись `item_list`, остальные типы
	//         → `CControl_SetState(&g_ItemTooltip, 0, code, …)` = ПРОСТОЙ билдер;
	//     • запись `sowell_list`   → `CControl_SetState(…, 1, code, …)` = билдер самоцвета;
	//     • запись `special_itemlist` → полный билдер с `−1` (ветка ВТОРОЙ таблицы).
	//   Синтетическая запись — БЕДНАЯ, как у панели состава: `CSlot_Init(v6); v6[0] = code`.
	void ShowGachaSlotTooltip(int32 ItemCode, EAsdaTipOwner Owner = EAsdaTipOwner::PremiumGacha);
	// ★★★ ТУЛТИП ЯЧЕЙКИ ВИТРИНЫ МАГАЗИНА NPC (1320…1355) — порт `Item_BuildTooltipText_EAC520
	//   @0xEAC520` (item_inventory.c:60836). ПЯТАЯ отдельная развилка эталона, и приходят в неё
	//   через `itemTooltip_setStateFromRecord@0xE285B0` (слот vtbl+40 стороны ПОКУПКИ) →
	//   `itemTooltipUpdateOnHover@0xEAC430`. Аргументы, с которыми зовёт магазин:
	//     `itemCount` = пятое поле записи витрины (ПОТОЛОК СТОПКИ), `isBuyMode` = 0, `showCount` = 1.
	//   Что делает билдер (проверено по веткам `case 0xA` и `case 0xB`, item_inventory.c:61198-61350):
	//     • ЗАГОЛОВОК: при `isBuyMode || showCount` — «имя (N)», иначе просто имя; цвет — по грейду
	//       (`CItemManager_GetDropColor(itemRecord->itemGrade)`);
	//     • МЕТКА ЦЕНЫ: `isBuyMode == 0` ⇒ нотис **2303**, цена = `itemCount * ItemRecord->price`
	//       (при `itemType == 10` без умножения); `isBuyMode == 1` ⇒ нотис 2302 и `subCategory`;
	//     • ТЕЛО: `itemType == 10` → строки «{2295 Вес} : W», «{2305 Количество} : N», пустая,
	//       «{метка цены} : {цена} {2304 Золото}»; остальные типы → «{2295} : W», пустая,
	//       «{описание}», пустая, «{метка цены} : {цена} {2304}»;
	//     • если денег МЕНЬШЕ цены — цена красится `<FC=…>` (эталонный `colorPriceWarn`).
	//   Самоцветы у эталона идут ВТОРОЙ половиной той же функции — у нас это уже готовый
	//   `BuildTooltipSowel`, второй копии не заводим.
	//   ★ `bShowCount` (ревизия R-8, 25.09.2026) — эталонный аргумент `showCount` билдера. Магазин зовёт с 1
	//   (умолчание — поведение прежних вызовов не меняется); список свитков окна заточки 20000 — с 0
	//   (`Item_BuildTooltipText_EAC520(…, showCount = 0)`), и тогда заголовок «имя (N)» у него только при
	//   `isBuyMode` (эталонное условие `isBuyMode || showCount`).
	void ShowShopSlotTooltip(int32 ItemCode, int32 Count, bool bSellMode,
	                         EAsdaTipOwner Owner = EAsdaTipOwner::NpcShop, bool bShowCount = true);
	// ★★★ ТУЛТИП ИКОНКИ НАГРАДЫ ПОПАПА ЗАДАНИЯ `EPISODEPOP_WIN` (540) — порт
	//   `uiShowEquipSlotTooltip2@0xD925A0` (наведение, msg 8) и `uiShowEquipSlotTooltip@0xD92B30`
	//   (клик, msg 0xB), ui_core.c:59846 и 59974. ЧЕТВЁРТАЯ отдельная развилка эталона: она
	//   отличается и от витрины гачи, и от панели состава пака, и разная у ДВУХ панелей окна.
	//     • подсказка есть ТОЛЬКО у слотов вида 8…12 (предмет/самоцвет); «деньги/опыт/харизма/
	//       эпизод» (виды 0…3) её не имеют — гейт делает ВЫЗЫВАЮЩИЙ, у него модель слотов;
	//     • `bSelectPanel = true` (панель ВЫБОРА 553, иконки 557…2404) — ЭТАЛОН НАВЕДЕНИЯ:
	//         самоцвет  → `CControl_SetState(…, 1, code, 0, a5=1, …)` = ПОЛНАЯ карточка самоцвета;
	//         `item_list` → `ItemTooltip_Build(slot, −1, 0, 1, 1, 0, 0)` = ПОЛНАЯ карточка предмета;
	//         `special_itemlist` → `SetState(…, 0, code, count, a5=1, …)` = ПРОСТОЙ билдер;
	//     • `bSelectPanel = false` (панель ГАРАНТИРОВАННОЙ награды 541, иконки 544…2401) —
	//       ★ ОТСТУПЛЕНИЕ ПО ПРОСЬБЕ ВЛАДЕЛЬЦА, помечено сознательно. Эталон на НАВЕДЕНИИ отдаёт
	//       сюда `CControl_SetState(…, a5=0, …)` → `uiResolveLinkTooltip@0xEA2140`, то есть ОДНУ
	//       СТРОКУ с именем предмета (`tooltipShowAtCursor`: высота 27, ширина текст+42), а полную
	//       карточку открывает по КЛИКУ (`uiShowEquipSlotTooltip`). Владелец попросил полную и на
	//       наведении, поэтому мы переносим на наведение ИМЕННО ЭТАЛОННОЕ ПРАВИЛО КЛИКА, а не
	//       выдумываем своё: `itemType > 4 && (itemType < 8 || itemType > 9)` → простой билдер,
	//       иначе полный; самоцвет → полная карточка самоцвета.
	//   `Count` — эталонный `a3` (количество из слота награды), уходит в `Quantity` простого билдера.
	void ShowQuestRewardSlotTooltip(int32 ItemCode, int32 Count, bool bSelectPanel,
	                                EAsdaTipOwner Owner = EAsdaTipOwner::QuestReward);
	// ★★★ Скрыть, ТОЛЬКО если тултипом владеет `Owner`. Возвращает true, если реально скрыли.
	//   Все покадровые опросчики курсора обязаны звать ИМЕННО ЭТО (см. шапку EAsdaTipOwner:
	//   безусловный Hide из чужого тика съедал тултип предмета через кадр после показа).
	bool HideTooltipFor(EAsdaTipOwner Owner);
	// ★ ШАПКА ТРЕКЕРА ПОДСКАЗКИ ОКНА (дедуп №2, В1-B, U18, 11.10.2026) — одна точка вместо построчных копий начала
	//   `TickTooltip` ремесла 76000, высокой переплавки 78200, заточки 20000, лавки (`TickTip`) и разборки 32000:
	//   нет подсистемы тултипа → nullptr (ключ окна не трогается); ключ (вид, индекс) тот же, что показан → nullptr;
	//   иначе ключ запоминается, и при `Kind < 0` (курсор ушёл, эталонное событие 0xA → `CControl_KillFocus(&g_ItemTooltip)`)
	//   тултип гасится `HideTooltipFor(Owner)` → nullptr. Ненулевой ответ — «ключ сменился, показывай»: тело показа (свои
	//   эталонные билдеры) остаётся в окне.
	static UAsdaItemTooltipSubsystem* BeginHoverTip(UGameInstance* GI, EAsdaTipOwner Owner,
		int32& CurKind, int32& CurIdx, int32 Kind, int32 Idx);
	// То же для окна с ОДНИМ ключом (IDNum контрола под курсором; разборка 32000).
	static UAsdaItemTooltipSubsystem* BeginHoverTip(UGameInstance* GI, EAsdaTipOwner Owner, int32& CurId, int32 Id);
	// Скрыть БЕЗУСЛОВНО (закрытие HUD, травел, консоль) — эталон `CControl_KillFocus(&g_ItemTooltip)`.
	void HideTooltip();
	bool IsTooltipShown() const;
	// Кто сейчас держит плашку.
	EAsdaTipOwner GetTipOwner() const;

	// ══ 3. ОБЩАЯ ПЛАШКА (её берут другие подсистемы вместо своей копии) ══
	//   Эталон использует ТОТ ЖЕ `g_ItemTooltip` для подсказки Plus Package, подсказки эффекта,
	//   подсказки слота хотбара и т.д. — поэтому окно ОДНО на всю игру.
	UAsdaTipPanel* GetMainPanel();
	// Показать произвольный текст в общей плашке (заголовок + тело с тегами) от имени `Owner`.
	void ShowTextTip(EAsdaTipOwner Owner, const FString& Title, const FString& Body, const FAsdaTipMetrics& Metrics);
	// Диагностика: состояние обеих плашек одной строкой (команда `asda.ItemTip.State`).
	FString DebugTipState() const;

	// ══ 4. ПРАВАЯ ИНФО-ПАНЕЛЬ ОКНА ИНВЕНТАРЯ (эталон `ItemTooltip_PopulateInfoPanel@0xD02C00`) ══
	// Привязка контролов окна `INVEN_WIN`. Зовёт инвентарь, когда окно готово.
	void BindInfoPanel(UUserWidget* InvenWindow);
	// SourceKind: 1 — предмет из сумки/расходников, 3 — надетое (эталонный аргумент `slotSource`).
	void FillInfoPanel(const FAsdaItemInstance& It, int32 SourceKind);
	// ★ Ветка записей ВТОРОЙ таблицы (`special_itemlist`, коды 1…558) — премиум/бустер-предметы.
	//   Эталон: item_inventory.c:36668-36790, СТАРШАЯ половина развилки `PopulateInfoPanel`.
	void FillInfoPanelSpecial(const FAsdaItemInstance& It, const struct FAsdaSpecialItemlistRow& Sp);
	// Эталон `uiInventoryDetail_Reset@0xD17300` (в части ТЕКСТОВ панели: сам сброс вкладки/страницы
	//   делает инвентарь — это его состояние).
	void ClearInfoPanel();
	// ★ 08.11.2026 (пачка 17, баг 5): стрелка 408 раскрыла панель руками — отложенная свёртка кадра (её ставит `ClearInfoPanel`,
	//   в том числе при смене вкладки той же стрелкой) не должна её схлопнуть: ручная стрелка под правило Д1 не подпадает.
	void CancelPendingPanelCollapse() { bPendingPanelCollapse = false; }
	bool IsInfoPanelBound() const { return bInfoBound; }
	// ★ 21.10.2026: окно, к которому СЕЙЧАС привязана инфо-панель (самотест `asda.SelfTest.Sowel` сверяет его с живым
	//   окном инвентаря — пересоздание WBP после смены мира раньше оставляло панель привязанной к старому окну).
	UUserWidget* GetInfoPanelWindow() const { return InvenWindow.Get(); }
	// ★ 21.10.2026: окно инвентаря пересоздано (другой экземпляр WBP) — перепривязать панель к живому. true — перепривязали.
	//   Зовут тик подсистемы и мост инвентаря перед наполнением/очисткой (наполнение в кадре пересоздания не теряется).
	bool RebindInfoPanelIfWindowChanged();

	// ══ 5. ОКНО ВЫБОРА КАМНЯ `SOWELL_SELBOX` (idnum 1530) ══════════════════════════════════════
	// ★★★ ЧТО ЭТО. Всплывашка «в какое гнездо вставить камень, если свободных нет».
	//   Эталон: `sowelSlot_DrawStones@0xD6CC50` (item_inventory.c:50283) рисует в неё 2 или 3
	//   иконки камней и показывает окно; выбор камня — `uiCtrlOnDragCommand@0xD78490`
	//   (ui_core.c:58121, сообщения 1540/1541 для лотка на 2 и 1542/1543/1544 для лотка на 3);
	//   «OK» 1532 / «Отмена» 1533 — `ui_core.c:57886-57895`.
	//   ⚠ ИМЯ WBP В ЗАДАНИИ БЫЛО ДРУГИМ: `sowelSlot_DrawStones` работает с контролами
	//   **1535/1536/1537/1538/1540…1544** — это дети окна **`SOWELL_SELBOX` (1530)**, то есть
	//   `WBP_SOWELL_SELBOX`. Окно `SOWEL_Tooltip` (13700) — ДРУГОЕ (5 строк «имя/стат/грейд»,
	//   класс `CSowelWindow`, привязка `uiWindow_BindControls@0xEA19C0`); в срезе оно только
	//   привязывается и скрывается, ни одна функция его строк не заполняет.
	//   ⚠ Владелец в оригинале — `CItemTooltipUI` (менеджер msgbox’ов 1520/1530/1545), отдельного
	//   класса в порту нет, поэтому окно живёт здесь, рядом с `g_ItemTooltip`.
	//   Подписка на источник: `UAsdaSowelSubsystem::OnNeedSowelStonesWindow` (см. Initialize).
	void ShowSowelStonesWindow(const TArray<uint8>& Rec57, int32 SlotMode);
	void HideSowelStonesWindow();
	bool IsSowelStonesWindowShown() const;
	// Выбрать камень в лотке (эталон: сообщения 1540/1541 либо 1542/1543/1544).
	//   TrayIndex 1..2 для лотка на два камня, 1..3 для лотка на три (нумерация эталона:
	//   `gap_32D8[40]` = 1/2/3, а код камня берётся из записи по `+16 + 2*index`).
	void SelectSowelStone(int32 TrayIndex);
	// Номер выбранного гнезда (−1 — не выбрано).
	int32 GetSowelStoneSelection() const { return SowelWinSel; }

	// ★ «OK» (1532). Эталон шлёт `net_sendItemUseRequest(g_pInventoryWnd, запись, выбранное гнездо)`
	//   = пакет **5024** с полями: контейнер (1/3), слот носителя (`запись+5`), НОМЕР ГНЕЗДА,
	//   код камня, СТОЯЩЕГО в этом гнезде (`запись + 16 + 2*гнездо`), и код+слот камня, ВЫБРАННОГО
	//   в списке доступных. Последние два — состояние `UAsdaSowelSubsystem` (`SelStoneIndex`,
	//   `TargetContainer`), приватное и в чужом файле, который этим заходом трогать нельзя.
	//   Поэтому окно не шлёт пакет само, а отдаёт наружу номер гнезда — подписчик (владелец
	//   состояния камней) вызывает свой `SendSowelSocket5024`. «Отмена» (1533) в эталоне НЕ шлёт
	//   ничего (`net_sendItemUseRequest(…, 0, -1)` уходит в ветку `if (itemSlotPtr)` = пусто).
	DECLARE_MULTICAST_DELEGATE_OneParam(FOnAsdaSowelSelBoxAccepted, int32 /*SocketIndex*/);
	FOnAsdaSowelSelBoxAccepted OnSowelSelBoxAccepted;

	// ══ 6. ХЕЛПЕРЫ ДАННЫХ (публичны — ими пользуются консольные команды и инфо-панель) ══
	// Цвет грейда предмета (`CItemManager_GetDropColor`, индекс > 9 → элемент 0).
	static uint32 GradeColorArgb(int32 Grade);
	// Цвет уровня заточки (`itemGetGradeColor@0xD29940`).
	static uint32 EnhanceColorArgb(int32 Enhance);
	// Достижим ли класс предмета текущим персонажем (`upgradeAnyGradeReachable@0xEC8F70`).
	bool IsUsageCategoryReachable(int32 UsageCategory) const;
	// Проверка одной ветки классов (`upgradeCheckGradeReachable@0xEC9010`), 1:1.
	static bool CheckGradeReachable(int32 PlayerClass, int32 UsageCategory);

	// Дамп текста тултипа в строку (для консоли/логов).
	FString DumpTooltip(const FAsdaItemInstance& It) const;

	// ★ Формат строки опции по `ItemOptionTable.BIN` (+4 = printf-шаблон); значение делится на 100
	//   при id >= 300 (эталон `CItemManager_FindRecordById@0xB62200` + `sprintf`). Пусто — записи нет.
	//   ПУБЛИЧЕН с 2026-08-26: тем же форматом печатает свои пять строк параметров окно передачи
	//   параметров `EXTRACT_WIN` 16000 (`itemDlgFillSlotDetail@0xC8EF10` зовёт ровно эту пару
	//   «найти запись → sprintf») — заводить второй такой же форматтер запрещено
	//   (закон `port-shared-points`).
	FString FormatOptionValue(int32 OptionId, int32 RawValue) const;

private:
	// ── источники данных ──
	UAsdaItemTableSubsystem* Items() const;
	UAsdaTextSubsystem*      Text()  const;
	// Строка таблицы текста БЕЗ среза тегов (★ грабля `ministate-win-port`).
	FString LocRaw(int32 Id) const;   // ★ Дедуп №2, В1-B (U10a): было `Loc` — по смыслу это `AsdaNotice::LocRaw` (теги сохранены, маркеры подставлены, фолбэк пусто)
	int32   SelfLevel() const;
	int32   SelfClass() const;

	// ── сборка секций ──
	// Порядок склейки — эталонный массив `sectionOrder`.
	void  BuildSections(const FAsdaItemInstance& It, bool bNoTitleLine, bool bNotBound,
	                    TArray<FString>& OutSec, FString& OutTitle, FLinearColor& OutTitleColor) const;
	// `item_buildStatTooltip@0xEC5300` — 5 пар (тип, значение) из записи +29, шаг 4.
	FString BuildStatLines(const FAsdaItemInstance& It) const;
	// `CItemManager_BuildSowelTooltip@0xEC4F80` — агрегация гнёзд 1..3 по типу бонуса.
	FString BuildSowelLines(const FAsdaItemInstance& It) const;
	// `Item_BuildOptionTooltip@0xEC5980` — 2 пары опций спец-предмета + хвостовой нотис 110310.
	FString BuildSpecialOptionLines(const FAsdaItemInstance& It) const;
	// `itemTooltip_FormatStatRange*@0xEC4230/4570/48B0/4BE0` — «%d ~ %d» бонуса самоцвета гнезда 0.
	FString FormatSowelStatRange(const FAsdaItemInstance& It, int32 BonusType) const;
	// ── окно сравнения ──
	// `canCompareEquipped` эталона; при true возвращает надетый предмет в OutEquipped.
	bool ResolveCompareTarget(const FAsdaItemInstance& It, FAsdaItemInstance& OutEquipped) const;
	// Поставить окно сравнения вплотную справа от основного (по УЖЕ выставленной позиции основного).
	void PlaceComparePanel();

	// ── инфо-панель ──
	UWidget* Info(const TCHAR* Name) const;
	// ★★★ План (image-контрол) инфо-панели ищется по ПРЕФИКСУ имени: импортёр JUI даёт точное имя
	//   только интерактив-виджетам и Text, а планам — `{idc}_{ctrlIdx}_{planeJ}`
	//   (закон `jui-importer-widget-naming`). PlaneIdx = J (0 = ES0 подложка, 1 = ES1 заливка).
	UImage* InfoPlane(const TCHAR* Idc, int32 PlaneIdx) const;
	void SetInfoText(const TCHAR* Name, const FString& Markup, bool bShow = true);
	void SetInfoVis(const TCHAR* Name, bool bShow);
	// ★ 21.10.2026: у части контролов панели в .JUI есть СВОИ планы (плашка `Sowel_M` 419/420/421/479, полоса
	//   «Премиум-опция» 477, заголовки 426/427/428) — импортёр сложил их с текстом в группу `<idc>_grp`. Эталонный
	//   `CControl_SetVisible(контрол, 0)` гасит контрол ВМЕСТЕ с планами, поэтому видимость текста дублируется на его
	//   группу (иначе на экране оставались пустые плашки/полосы — «то, чего у предмета нет, — скрывать»).
	void SetInfoGroupVis(const TCHAR* Name, ESlateVisibility Vis);
	// Привязка ПЛАНОВ панели (иконка 423 + две полосы 10008/424) — они не Text, точного имени нет.
	void BindInfoPlanes();
	// ★ Иконка 423 (`DrawTextureRect(pTooltip[52], …)`): атлас-ячейка предмета в план 64×64.
	//   Возвращает false, если ячейку/атлас не разрешили (тогда план гасим).
	bool ApplyInfoIcon(int32 ItemCode);
	void ClearInfoIcon();
	// Клип заливки ES1 по доле (закон `ui-progressbar-animate-existing`: анимируем сам план,
	//   а не подменяем браш). Frac клампится в 0..1.
	static void ClipBarFill(UImage* Fill, float BaseLeft, float FullW, float Frac);
	// ★★★ Снять оформление ВСЕХ текст-контролов окна ДО первой подмены на URichTextBlock.
	//   `ApplyFormatted` создаёт новый контрол со стилем строки `Default` таблицы DT_AsdaRichStyle
	//   (AsdaUIFontRT 10 px, БЕЛЫЙ, влево, autowrap) и безвозвратно теряет то, что импортёр взял из
	//   JUI: кегль `FONT_PX = 8`, цвет из `EXS TextColor`, выравнивание из `EXS TextAlign`, обводку
	//   скина и признак однострочности. Отсюда жалоба «нечитаемо, не те цвета/размер/выравнивание».
	void CaptureInfoStyles();
	// ★ Эталонные правки раскладки, которые оригинал делает В ИНИЦИАЛИЗАЦИИ окна (`invInitControls`,
	//   item_inventory.c:35294+) и которых нет в .JUI — импортёр их знать не мог.
	void ApplyEtalonInfoLayout();

	// ── окно выбора камня SOWELL_SELBOX ──
	bool BindSowelSelBox();                                  // создать/найти окно и его контролы
	bool ApplyStoneIcon(UImage* Plane, int32 SowelCode);      // `DrawTextureRect(icon, 0,0,32,32)`
	void PlaceStoneCursor();                                  // курсор 1537/1538 на выбранную иконку
	UFUNCTION() void OnSowelSelBoxOk();                       // кнопка 1532
	UFUNCTION() void OnSowelSelBoxCancel();                   // кнопка 1533
	void PlaySowelSelBoxSfx(const TCHAR* Why);                // звук 4 кнопок 1532/1533 — общая точка окон-сообщений (волна В6)
	// Опрос курсора по иконкам лотка. Кнопок у них нет (импортёр делает UButton только для
	//   PushButton-скинов), поэтому клик ловим тем же способом, что и остальной порт, —
	//   покадровым хит-тестом по кэш-геометрии (см. `UAsdaInventorySubsystem::IsCursorOver`).
	void TickSowelSelBox();
	bool IsCursorOverWidget(const UWidget* W) const;

	// ── мост в инвентарь ──
	void TryBindInventoryBridge();

	// ★★★ Д1 (КРУГ 5). «Экипировочный» ли тип предмета — то есть тот, ради которого правая
	//   инфо-панель вообще существует: БРОНЯ (`itemType` 0…4: шлем/верх/низ/обувь/перчатки),
	//   ЩИТ (8) и ОРУЖИЕ (9). Ровно этот предикат эталон использует, когда выбирает ПОЛНЫЙ
	//   тултип вместо простого — `itemType <= 4 || (itemType >= 8 && itemType <= 9)`
	//   (item_inventory.c:5465 и его отрицание в 4649). Соответствие типов проверено по
	//   `invBagApplyEquipModelByItemType@0xC3D9E0` (item_inventory.c:20942): 9 → слот частей 6
	//   (оружие), 8 → слот частей 7 (щит).
	static bool IsEquipInfoPanelType(int32 ItemType)
	{
		return (ItemType >= 0 && ItemType <= 4) || ItemType == 8 || ItemType == 9;
	}
	// ★ Д4 (КРУГ 5). Наведение на ГНЁЗДА САМОЦВЕТОВ правой панели (441…444). Эталон
	//   `invSlotBuildTooltip@0xD06670`, последняя ветка (item_inventory.c:37484-37492):
	//   `if (cmdId < 0x1B9 || cmdId > 0x1BC) return 0;` — 0x1B9 = 441, 0x1BC = 444.
	void TickInfoPanelSockets();
	// ★★★ Д5 (КРУГ 5, доработка). НЕЗАВИСИМЫЙ хит-тест сетки инвентаря — контрольная проверка
	//   попадания: перебирает планы `INVEN_SET{N}_{idx}_0` окна и возвращает ВИДИМЫЙ индекс ячейки
	//   (0…29) под курсором, −1 если курсора над сеткой нет. Считается ОТДЕЛЬНО от
	//   `UAsdaInventorySubsystem::HitTest`, чтобы расхождение «подсказка не для той ячейки»
	//   ловилось само и печаталось в лог, а не искалось глазами по скриншоту.
	int32 HitTestGridCellVisual() const;
	// Д1: свернуть правую панель окна инвентаря. Свёртку умеет только `UAsdaInventorySubsystem`
	//   (`SetPanelExpanded` — публичный API, файл инвентаря не правим), но её клик зовёт
	//   `AutoExpandOnSelect` ПОСЛЕ моста `FillInfoPanel`, поэтому свернуть надо дважды: сразу
	//   и на ближайшем `Tick` (флаг `bPendingPanelCollapse`).
	void CollapseInfoPanelWindow();
	// ★★★ Д1 (КРУГ 6): «панель без выбранного предмета — свёрнута» (требование пользователя;
	//   эталон `inventoryItemDetailSetVisible@0xD173A0` раскрывает её на вкладке «Снаряжение» сам).
	//   `MarkInfoPanelFilled`   — панель наполнена, отложенную свёртку кадра отменить;
	//   `NoteInfoPanelSource`   — запомнить контейнер/ячейку/ряд прокрутки показанного предмета;
	//   `OnInventoryCellChanged`— подписка на `UAsdaInventorySubsystem::OnCellChanged` (пакет);
	//   `EnforceInfoPanelSelectionRule` — покадровая доводка инварианта (зовётся из `Tick`).
	//   Откат к эталону: `asda.ItemTip.LegacyInfoPanelNoSelection 1`.
	void MarkInfoPanelFilled();
	void NoteInfoPanelSource(int32 Container, int32 Cell);
	void OnInventoryCellChanged(int32 Container, int32 Cell);
	void EnforceInfoPanelSelectionRule();
	// ★ Д3 (КРУГ 5). Ужать текст-контролы инфо-панели по ПРАВОЙ ГРАНИЦЕ деревянной рамки
	//   `Inven_RightBase` и запомнить их предельную ширину для авто-ужатия кегля.
	void ClampInfoTextToFrame();
	// Кегль, при котором строка целиком влезает в MaxWidth контрола (эталон обрезал бы хвост).
	static int32 FitFontSizeToWidth(const FString& Plain, const FSlateFontInfo& Font, float MaxWidth);

	// ── диагностика показа (одна строка: длины текста + класс/размер/видимость/позиция виджета) ──
	void LogShow(const TCHAR* Kind, int32 ItemCode, const FString& Title, const FString& Body,
	             const UAsdaTipPanel* P) const;

	UPROPERTY(Transient) TObjectPtr<UAsdaTipPanel> MainPanel = nullptr;      // ITEM_TIPWIN 5000
	UPROPERTY(Transient) TObjectPtr<UAsdaTipPanel> ComparePanel = nullptr;   // ItemCompareTip_Win 5050
	UPROPERTY(Transient) TObjectPtr<UUserWidget>   InvenWindow = nullptr;    // окно INVEN_WIN (инфо-панель)

	// Снимок оформления одного текст-контрола инфо-панели (взят из WBP, где его положил импортёр JUI).
	struct FAsdaInfoTextStyle
	{
		FTextBlockStyle    Style;                          // шрифт+кегль+обводка, цвет, тень
		ETextJustify::Type Justify = ETextJustify::Left;   // из `EXS TextAlign`
		bool               bWrap   = false;                // многострочный? (импортёр: h >= 28)
		bool               bValid  = false;
		// ★ Д3 (КРУГ 5): фактическая ширина/высота слота ПОСЛЕ ужатия по правой границе рамки.
		//   0 ⇒ ещё не измеряли. `bMultiLine` = высота слота вмещает 2+ строки (единственный такой
		//   контрол панели — 422 `INVEN_SHELLCONTENS`, 346×86): только он переносится по словам,
		//   остальным перенос лишь ПРЯЧЕТ хвост под рамкой высотой 14 px.
		float              MaxWidth   = 0.f;
		bool               bMultiLine = false;
	};
	TMap<FName, FAsdaInfoTextStyle> InfoStyles;

	// ── ПЛАНЫ инфо-панели (у импортёра это UImage с idx-суффиксом, а не именованные контролы) ──
	//   423  `INVEN_EQUIICON_108_0`     — иконка предмета 64×64 (`pTooltip[52]`);
	//   10008 `INVEN_EQUIICON_BAR_110_*` — полоса ПРОЧНОСТИ (`pTooltip[53]`), ES0 64 px + ES1 43 px;
	//   424  `INVEN_INVASIONGAUGE_106_*` — полоса «состояния» (`pTooltip[56]`), ES0 60 px + ES1 40 px.
	//   ★ У обеих полос ширина плана-заливки в ассете — статичный ДИЗАЙН-процент, поэтому «100 %»
	//     берём у ПОДЛОЖКИ ES0 (та же грабля разобрана в мини-статусе).
	UPROPERTY(Transient) TObjectPtr<UImage> IconPlane = nullptr;   // 423
	UPROPERTY(Transient) TObjectPtr<UImage> DuriBack  = nullptr;   // 10008 ES0
	UPROPERTY(Transient) TObjectPtr<UImage> DuriFill  = nullptr;   // 10008 ES1
	UPROPERTY(Transient) TObjectPtr<UImage> CondBack  = nullptr;   // 424  ES0
	UPROPERTY(Transient) TObjectPtr<UImage> CondFill  = nullptr;   // 424  ES1
	float DuriFullW = 0.f, DuriLeft = 0.f;    // полная ширина/левый оффсет заливки прочности
	float CondFullW = 0.f, CondLeft = 0.f;    // то же для полосы состояния

	// ── окно SOWELL_SELBOX (1530): контролы 1:1 по дампу WBP (Tools/_dump_sowell_selbox.py) ──
	UPROPERTY(Transient) TObjectPtr<UUserWidget> SowelSelBox = nullptr;   // 1530
	UPROPERTY(Transient) TObjectPtr<UWidget> SowelTrBack2 = nullptr;      // 1535 группа лотка на 2 камня
	UPROPERTY(Transient) TObjectPtr<UWidget> SowelTrBack3 = nullptr;      // 1536 группа лотка на 3 камня
	UPROPERTY(Transient) TArray<TObjectPtr<UImage>> SowelTray2;           // 1540/1541 (планы иконок)
	UPROPERTY(Transient) TArray<TObjectPtr<UImage>> SowelTray3;           // 1542/1543/1544
	UPROPERTY(Transient) TArray<TObjectPtr<UWidget>> SowelTrayBase2;      // подложки Sowel_Btray*2_Base
	UPROPERTY(Transient) TArray<TObjectPtr<UWidget>> SowelTrayBase3;      // подложки Sowel_Btray0*_Base
	UPROPERTY(Transient) TObjectPtr<UWidget> SowelCursor2 = nullptr;      // 1537 (лоток на 2)
	UPROPERTY(Transient) TObjectPtr<UWidget> SowelCursor3 = nullptr;      // 1538 (лоток на 3)
	// Копия 57-байтовой записи носителя (эталон `memcpy(&gap_32D8[44], pSowelData, 0x39)`).
	TArray<uint8> SowelWinRec;
	int32 SowelWinSel  = -1;      // `gap_32D8[40]` — выбранное ГНЕЗДО (1..3), −1 = нет
	bool  bSowelWinTwo = false;   // `gap_32D8[0]`  — 1 = показан лоток на два камня
	bool  bSowelWinBound = false;
	// ★ Аудит №3 (B3): опрос мыши и латч фронта — общей структурой (был свой `bSowelPrevMouseDown`).
	FAsdaMouseFrame SowelMouse;          // фронт нажатия ЛКМ для хит-теста иконок лотка

	// ── ★ КРУГ 5: состояние правой инфо-панели между кадрами ──
	//   `PanelSowelCode[i]` — код камня в гнезде i ПОКАЗАННОГО в панели предмета (запись +16/+18/
	//   +20/+22). Эталон читает их ровно так же: `*(__int16*)((char*)&word_2901960 + 57*m_nSelBagSlot
	//   + 2*statSlotIdx)`, где `word_2901960` — символ IDA на `g_InventoryBag + 16`
	//   (item_inventory.c:37487). 0 = гнездо пустое.
	int32 PanelSowelCode[4] = { 0, 0, 0, 0 };
	int32 PanelSocketHover  = -1;    // гнездо под курсором (0…3), −1 = нет; чтобы не пересобирать текст
	// Д1: панель наполнена НЕ оружием/бронёй/щитом ⇒ окно надо свернуть. Свёртку выполняет
	//   `UAsdaInventorySubsystem::SetPanelExpanded`, но клик инвентаря зовёт `AutoExpandOnSelect`
	//   ПОСЛЕ моста `FillInfoPanel` — поэтому свернуть надо и сразу, и на ближайшем `Tick`.
	bool bPendingPanelCollapse = false;

	// ── ★★★ Д1 (КРУГ 6): «нет выбранного предмета ⇒ правая панель свёрнута» ──
	//   `bInfoPanelFilled` — панель СЕЙЧАС показывает предмет (ставится в `FillInfoPanel`, снимается
	//   в `ClearInfoPanel` — единственной точке «панель пуста»).
	//   `InfoPanelSrc*` — откуда взят показанный предмет: контейнер (1 сумка / 3 надетое), ячейка и
	//   ряд прокрутки на момент наполнения. Ряд нужен потому, что `UAsdaInventorySubsystem::SetScrollRow`
	//   сбрасывает выбор (`SelCell = −1`), но панель не чистит и наружу об этом не сообщает.
	bool  bInfoPanelFilled = false;
	int32 InfoPanelSrcCont = -1;
	int32 InfoPanelSrcCell = -1;
	int32 InfoPanelSrcRow  = -1;
	// Подписка на `OnCellChanged` инвентаря (предмет из показанной ячейки может исчезнуть пакетом).
	//   Тип хранения — базовый `UGameInstanceSubsystem`: в этом заголовке инвентарь только объявлен
	//   вперёд, а `Cast<>` до него делается в .cpp, где заголовок инвентаря подключён целиком.
	FDelegateHandle CellWatchHandle;
	TWeakObjectPtr<UGameInstanceSubsystem> WatchedInv;

	bool bInfoBound = false;
	bool bInfoLayoutFixed = false;   // эталонные сдвиги контролов применены (делать РОВНО один раз)
	// ★ ИСПРАВЛЕНИЕ 11.10.2026 (ревизия В2 дедупа №2): окно, к которому применены эталонные сдвиги. Слабая ссылка
	//   НЕ сбрасывается сбросом сессии: при разрыве связи без смены мира окно INVEN_WIN живёт дальше, а `InvenWindow`
	//   обнуляется — прежнее сравнение «новое окно?» по `InvenWindow` давало «да» и сдвигало 488/489 ещё на +14.
	TWeakObjectPtr<UUserWidget> InfoLayoutWindow;
	bool bBridgeBound = false;
	bool bCompareShown = false;

	TArray<IConsoleObject*> ConsoleCmds;
};
