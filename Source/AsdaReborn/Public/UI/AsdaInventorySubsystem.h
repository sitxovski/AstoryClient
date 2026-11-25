// Copyright (c) 2026 @sitxovski (Discord)
// UAsdaInventorySubsystem — ЯДРО ИНВЕНТАРЯ: модель пяти контейнеров оригинала, сеть предметов
//   и окно `INVEN_WIN` (JUI IDNum 400, 586 контролов).
//   Полный реверс: `Reports/inventory_re_A.md` (канон окна, структура, UI-механика) и
//   `Reports/inventory_re_B.md` (данные, 27 опкодов, контейнеры). Реализация и все отступления —
//   `Reports/inventory_port.md`.
//
// ═══════════════════════════ КАНОН: КАКОЕ ОКНО ЖИВО ════════════════════════════════════════════
//   **`INVEN_WIN` (400)**, а НЕ `INVEN_NEW_WIN` (83500). Доказано тремя способами (отчёт A §0):
//     (1) путь показа `ITEMBUT 73402 → uiToggleInventoryWindow@0xE8FA10 → uiOpenInventoryWindow@0xCFF300`
//         → `SetVisible(FindControlByID(g_pWindowMgr, 400))` — числа 83500 в цепочке НЕТ;
//     (2) побайтовый скан: dword 83500 = 0 вхождений в `AsdaStory.exe`/`AsdaStoryA.exe`/`Addon.dll`;
//     (3) живость idnum: у 400 — 142 из 289 живы, у 83500 — 8 из 106, и все восемь разбираются
//         ВНУТРИ обработчика СТАРОГО класса `CGameWindow_OnCommand@0xD1B140`.
//   Это ВТОРОЕ ИСКЛЮЧЕНИЕ из закона `jui-prefer-new-windows` (первое — `CHARAINFO_WIN` 900).
//   Ловушка: главное меню «ссылается» на 83500 — это ошибка разбора меню, а не признак живости.
//
// ═══════════════════════════ ПЯТЬ КОНТЕЙНЕРОВ (числа ПРОТОКОЛА) ════════════════════════════════
//   Роутер сервера `sub_4D61E0@0x4D61E0` (чтение) / `sub_4D63F0` (запись) / `sub_4D6740` (свободный слот).
//     1 `ShellInven`        30  × 57 б — снаряжение / квестовые. НЕ стекуется (вес = вес записи).
//     2 `SpendCellobInven`  60  × 57 б — расходники / самоцветы / **ЗОЛОТО (itemCode 20551)**. Стекуется.
//     3 надетое             11  × 57 б — (+9 «аватар» 11..19: код клиента их знает, контролов нет).
//     4 слоты КВЕСТОВ       12  × 44 б — ★ НЕ инвентарь предметов, здесь НЕ моделируется.
//     5 склад               ≤270 × 57 б — страницами по 30, ёмкость = `CUser+430`.
//   ★ ГРАБЛЯ ИМЕНОВАНИЯ: клиентский символ `g_InventoryBag` — это контейнер **1**, а НЕ «сумка
//     расходников»; расходники и золото живут в `g_InventoryArray2` = контейнер **2**.
//
// ═══════════════════════════ СЕТКА ОКНА (ключ к «индексу ячейки») ══════════════════════════════
//   `INVEN_SET0..29` = idnum 10010…10039, ячейка 32×32, шаг 33 px, **6 колонок × 5 рядов**,
//   левый-верх (126, 320) в базе 1024×768. Тридцать КОНТРОЛОВ обслуживают ДВА контейнера —
//   ★★★ [ПОРТ] 29.10.2026 (инвентарь ×2, решение владельца): сетка стала **12 колонок × 5 рядов = 60 контролов**
//   `INVEN_SET0..59` (клоны 30..59 — шаг пост-импорта `Tools/asda_ui_inventory_x2.py`), нумерация построчно:
//   `INVEN_SET{r·12+c}` в (126+33c, 320+33r); сумка (контейнер 1) 30 → 60 (`kShellSlots`), обе вкладки видны целиком,
//   прокрутки нет (`GetMaxScrollRow() == 0`, полоса спрятана). Ключ `asda.Inven.ShellX2 0` — прежняя ёмкость сумки 30
//   (старый сервер): ячейки 30..59 пустые и недоступные. Абзацы ниже описывают ЭТАЛОН (6×5, сумка 30).
//   какой именно, решает переключатель `INVEN_CHOICEBUT` (402), текущая вкладка = `CInventoryWindow+0x204`:
//     вкладка 0 «Расходники» (текст 13247) → контейнер **2**, 60 ячеек, колесо/полоса 494 ВИДНЫ,
//                                            `SetScrollRange(4, 9)` ⇒ range 5 ⇒ 6 позиций;
//     вкладка 1 «Снаряжение» (текст 13248) → контейнер **1**, 30 ячеек, полоса СКРЫТА (вместе с 494).
//   Устройство самой полосы — раздел «ПОЛОСА ПРОКРУТКИ» ниже.
//   ★ ФОРМУЛА (дизасм `0xD00990`/`0xD01730`, `imul eax, eax, 6`):
//        абсолютный индекс = (idnum − 10010) + 6 · страница        // страница = сдвиг В РЯДАХ
//        индекс ≥ размера контейнера → ячейка рисуется пустой
//
// ═══════════════════════════ ПОЛОСА ПРОКРУТКИ (реверс 2026-08-09) ══════════════════════════════
//   ★★★ ОТДЕЛЬНОГО КОНТРОЛА ПОЛОСЫ В `gamemain_jui.ini` НЕТ — и искать его бесполезно. Полосу
//   СОЗДАЁТ ДВИЖОК в рантайме как РЕБЁНКА «колёсного окна» `INVEN_WHEELWIN` (494):
//     `JuiSkin.ini` → `[Ctrl] Name = "WheelWin_Nomal"  Type = "WheelWindow"`
//                     **`ScrollBarFileNameVert = "Nomal_Scr"`**
//     `uiControl_AttachVertScrollbar@0x12F3160` читает этот ключ у СКИНА и зовёт
//     `uiControl_attachScrollbar@0x12F35B0`: `new CScrollBar` (`CScrollBar_ctor@0x12FE240`),
//     `ParseFrameBlock(скин Nomal_Scr)`, `SetVisible(1)`, позиция = (rect.right, rect.top)
//     родителя, высота = высота родителя. Сторона — из `EXS { VertScrollPosType = "1" }` у 494
//     (`uiControl_layoutScrollbar@0x12F3330`: 1 = СПРАВА, 0 = слева).
//   ⇒ Полоса = **(324, 320) 11 × 164** в базе 1024×768: ровно справа от сетки, во всю её высоту.
//
//   СКИН `Nomal_Scr` (`Type = "ScrollBar"`, `ScrollBarType = "1"` = вертикальный), 4 плана,
//   атлас `N_01.tga` (512×512), тинт всех планов `FFFFFFFF`:
//     ES0 дорожка   Rect "0 11 11 41"  UV (0.996,0.000)-(1.000,0.004) → ПРОЗРАЧНЫЙ пиксель,
//                   дорожка НИЧЕГО не рисует, но ловит клики «страница вверх/вниз»;
//     ES1 стрелка ↑ Rect "0  0 11 11"  UV (0.387,0.438)-(0.408,0.459)  (тексель 198,224 11×11);
//     ES2 стрелка ↓ Rect "0 41 11 52"  UV (0.414,0.438)-(0.436,0.459)  (тексель 212,224 11×11),
//                   у состояния SS2 (нажата) UV сдвинут на −1 тексель по обеим осям
//                   ⇒ картинка едет на +1 px вправо-вниз — это ЭТАЛОННАЯ обратная связь нажатия;
//     ES3 бегунок   Rect "0 20 11 31"  UV (0.359,0.438)-(0.381,0.459)  (тексель 184,224 11×11).
//   `uiScrollbarLayoutPlanes@0x12FE500` при высоте H растягивает дорожку до H−22, а стрелку ↓
//   сажает в самый низ ⇒ ↑(0,0,11,11), дорожка(0,11,11,H−11), ↓(0,H−11,11,H).
//
//   ДИАПАЗОН. `uiScrollbarSetRange@0x12FF5E0` (a2 = видимых рядов, a3 = всего рядов):
//     `m_nMax = a3 − a2`;   `m_nThumbLen = max(6, ДлинаДорожки · a2 / a3)`;
//     `m_nMax <= 0` ⇒ бегунок гасится (высота 0), стрелки и дорожка остаются.
//   `uiScrollbarLayoutThumb@0x12FEA40`: `смещение = (Дорожка − Бегунок) · m_nPos / m_nMax`.
//   Кто и с чем зовёт: `InventoryWindow_InitControls` / `uiStorageWndReset` / `CInventoryUI_ResetAndRefresh`
//   и `inventoryItemDetailSetVisible(0)` → `SetScrollRange(494, 4, 9)` (вкладка «Расходники»);
//   `uiInventoryDetail_Reset@0xD17300` → `SetScrollRange(494, 4, 5)` (вкладка «Снаряжение»).
//   `uiControlSetScrollPage(494, 1, 1)` ⇒ шаг стрелки = 1 ряд и шаг «страницы» = ТОЖЕ 1 ряд.
//
//   СОБЫТИЯ. `uiScrollbarOnButtonEvent@0x12FF2C0` по плану-кнопке:
//     план 1 → 69633 «строка вверх»   (pos − шаг, клампится ≥ 0)
//     план 2 → 69634 «строка вниз»    (pos + шаг, клампится ≤ max)
//     план 0 → 69635/69636 «страница вверх/вниз» (по курсору выше/ниже бегунка; ВНУТРИ бегунка — 0)
//     план 3 → латч +608 = 1, дальше `uiScrollbarOnMouseDrag@0x12FF020` шлёт 69637:
//              `pos = max · (курсорY − верхДорожки − Бегунок/2) / (Дорожка − Бегунок)` —
//              ★ бегунок ЦЕНТРИРУЕТСЯ под курсором, «точки захвата» в оригинале нет.
//     `uiScrollbarSetPos@0x12FF800` → 69638 (программная установка).
//   Хозяин-контрол ретранслирует ЛЮБОЕ из 69633…69638 как **msgType 23 (0x17)** с `cmdId 494`
//   (`sub_12F3540@0x12F3540`), а `CGameWindow_OnCommand@0xD1B140` (ui_core.c:47486) по нему пишет
//   страницу и перерисовывает сетку: вкладка 0 → `m_nEquipPage` + `CInventoryUI_RefreshStoragePage`,
//   вкладка 1 → `m_nBagPage` + `CInventoryUI_RefreshBagIconsPage`.
//   КОЛЕСО — `sub_12F3480@0x12F3480` у контрола-хозяина: `pos ∓ шаг` → `uiScrollbarSetPos` → то же 23.
//
//   ВИДИМОСТЬ. Полоса живёт и гаснет ВМЕСТЕ с 494: `inventoryItemDetailSetVisible(1)` и
//   `uiToggleSubWindowVisible@0xD1E770` прячут 494 на вкладке «Снаряжение» / при раскрытой правой
//   панели, `inventoryItemDetailSetVisible(0)` и `CInventoryUI_ResetAndRefresh` показывают обратно.
//
// ═══════════════════════════ СЕТЬ (реверс B §2) ════════════════════════════════════════════════
//   Приём при входе: 4045→4046 (контейнер 1), 4048→4049 (контейнер 2), 4050→4051/4052 (склад).
//     Нарезка: до 11 записей (627 б) в пакете, хвост отдельным пакетом, затем НУЛЕВОЙ пакет-маркер.
//     Надетое (контейнер 3) полными записями приходит ТОЛЬКО в 1027/0x403 (11 × 57).
//   Действия: **5000 → 5001** (переместить / надеть / снять). ★ Клиент НЕ применяет оптимистично —
//     ждёт `result == 1`; до ответа стоит флаг `g_bInventoryActionPending` (у нас `bActionPending`).
//   Расходник: **5028 → 5029**. Уничтожить: 5006 → 5007 (+5003). Очистка слота: 4999.
//   Количество / деньги / вес: 5003 (@6 newAmount, @12 newCurWeight; золото = itemCode 20551).
//   ★ Конверт C→S у ВСЕХ item-пакетов один: [8 токен][16 MD5][payload] — собирается ОБЩИМ
//     `UAsdaNetSubsystem::SendSecured`, второй копии сборщика в порту нет.
//   ⚠ Закон `no-reentrant-net-callbacks`: из колбэка пакета — только очередь; работа в `Tick`.
//
// ═══════════════════════════ ВЕС ══════════════════════════════════════════════════════════════
//   `CUser+416` = максимум, `CUser+418` = текущий. Клиент вес САМ НЕ СЧИТАЕТ и ничего по нему не
//   блокирует — только отображает. Максимум уже приезжает блоком 1017 char-select и живёт в
//   `UAsdaNetSubsystem::SelfWeightMax`; текущий приходит в КАЖДОМ item-пакете. Поэтому инвентарь
//   ничего своего не заводит, а зовёт `SetSelfWeight` — полосу рисует `UAsdaMiniStateSubsystem`.
//
// ═══════════════════════════ АВАТАР-СЛОТЫ 11…19 (решение юзера) ════════════════════════════════
//   Юзер: «11, ещё 9 — это под "аватар", т.е. экипировка не являющаяся основной, а для внешнего
//   вида только. В игре её не реализовали. НО когда-нибудь мы это сделаем.»
//   ⇒ РАБОЧИХ слотов 11. Массив контейнера 3 всё равно длиной `kEquipSlotsTotal` = 20, перечисление
//   `EAsdaEquipSlot` уже содержит `Avatar0..Avatar8`, а весь код адресуется через
//   `IsEquipSlotUsable()` / `GetEquipSlotCount()`. Включение = `bAvatarSlotsEnabled = true` +
//   контролы 83531…83539 (они есть только в `INVEN_NEW_WIN`); своих мы НЕ рисуем (закон `no-invented-ui`).
//   Клиент оригинала эти индексы УЖЕ обрабатывает: `uiEquipSlotSelect@0xC6A860`
//   (`if (a2 >= 0x1464B) v4 = a2 % 0x1464B + 11`).
//
// ═══════════════════════════ ИСПРАВЛЕННЫЕ ДЕФЕКТЫ ОРИГИНАЛА (закон fix-original-defects) ═══════
//   Д1 **АВТОСОРТИРОВКА НЕДОСТИЖИМА.** Живой код `inventoryAutoArrange@0xD35340` привязан к id
//      НОВОГО окна (83671 кнопка «A», 83691/83692 пункты, 83693 отмена), а у старого окна контролы
//      автосортировки (`Inven_auto_array` 80400, `_box_d1..d3` 80411…80413) БЕЗ обработчика и с
//      `Visible = 0`. По решению юзера включаем: те же контролы + та же логика.
//                                                            откат: `asda.Inven.LegacyNoAutoArrange 1`
//      ★ Подтверждено по ЖИВОМУ ДАМПУ **Asda Global** (2026-08-13,
//      `Reports/inventory_autosort_global_dump.md`; прежний `inventory_autosort_global.md`
//      снят с другой игры и признан недостоверным): там ЖИВОЕ окно инвентаря — это `INVEN_NEW_WIN`
//      (83500, глобальный объект 0x01608E20, VA Global), а шаговый драйвер вызывается из кадрового
//      цикла 0x00455A90. Раскладка пунктов Global: 83691 = loc 112400, 83692 = loc 112401,
//      **83693 = «Close» (loc 112402)** — третий пункт НЕ сортирует. Порядок категорий у обоих
//      сортирующих пунктов ОДИН (аргумент функции не читается), вторичного ключа внутри разряда
//      в Global нет. У нас: 80411 → AutoArrange(0), 80412 → AutoArrange(1), 80413 → только закрыть.
//      ★ Д9 (решение юзера 2026-08-13): МЕХАНИКУ прохода берём 1:1 с Global, а внутри разряда
//      добавляем СВОЙ вторичный ключ — код предмета по возрастанию (без него разряд 99 не
//      упорядочивается вовсе и получается «рецепт, смазка, рецепт»). То, что так же ведёт себя сам
//      Global, поводом оставить как есть не является — это случай закона `fix-original-defects`.
//                                       откат на точное поведение: `asda.Inven.LegacyArrangeOrder 1`
//      ⚠ В самом Global проход ЗАВИСАЕТ после первой же пары: байт ожидания `окно+0x1758` ставится
//      отправителем и снимается ТОЛЬКО конструктором окна (доказано двумя методами, см. .cpp).
//      Мы снимаем его по 5001 — это второй исправленный дефект внутри Д1.
//   Д2 На вкладке «Снаряжение» `SetScrollRange(4, 5)` даёт лишнюю позицию прокрутки на контейнере
//      из 30 ячеек (6 пустых). В оригинале не проявляется, т.к. полоса скрыта; мы клампим диапазон
//      по РАЗМЕРУ контейнера.                                 откат: `asda.Inven.LegacyScrollRange 1`
//   Д3 Подтверждение перед уничтожением предмета в корзине (эталон шлёт 5006 сразу, откат делает
//      только сервер — и то не делает). Диалог — эталонный `TWOBUT_MSGBOX`, своего UI не рисуем.
//                                                            откат: `asda.Inven.LegacyTrashNoConfirm 1`
//
// ═══════════════════════════ ЧТО ОСТАВЛЕНО СЛЕДУЮЩЕМУ ЗАХОДУ ═══════════════════════════════════
//   ТУЛТИП ПРЕДМЕТА (`ITEM_TIPWIN` 5000) и ОКНО ХАРАКТЕРИСТИК/СРАВНЕНИЯ — отдельная работа.
//   Здесь оставлены ТОЛЬКО чистые точки подключения (см. `FAsdaItemTooltipBridge` ниже): своего
//   UI они не рисуют и ничего не выдумывают.
#pragma once

#include "CoreMinimal.h"
#include "AsdaTickGate.h"   // ★ R4-b (14.11.2026): шаблон класса (CDO) не тикает
#include "Subsystems/GameInstanceSubsystem.h"
#include "Tickable.h"
#include "UI/AsdaScrollBarNomal.h"   // ★ D3: общая полоса прокрутки скина `Nomal_Scr`
#include "UI/AsdaUiInputTypes.h"   // ★ R1: FAsdaMouseFrame / FAsdaDragArm / FAsdaDragGhost … — поля ввода (без всего AsdaUiHelpers.h)
#include "AsdaNetQueue.h"          // TAsdaNetQueue — очередь «колбэк копит, Tick применяет»
#include "AsdaSessionReset.h"   // IAsdaSessionResettable — сброс состояния сессии
#include "UI/AsdaShopDblClick.h"   // ★ U1 (В2-A дедупа №2): FAsdaDblClick — двойной щелчок по порогу Windows
#include "UObject/SoftObjectPath.h"   // ★ load_hitch: FSoftClassPath (GetWindowClassPath — путь класса окна для предзагрузки)
#include "AsdaInventorySubsystem.generated.h"

class UUserWidget;
class UWidget;
class UImage;
class UButton;
class UTextBlock;
class UPanelWidget;
class UAsdaAnimatedPlane;
class UAsdaButtonRelay;

// ═══ Контейнер предметов — ЧИСЛА ПРОТОКОЛА (роутер `sub_4D61E0`). Менять значения НЕЛЬЗЯ. ═══
UENUM(BlueprintType)
enum class EAsdaItemContainer : uint8
{
	None      = 0 UMETA(DisplayName = "0 — нет"),
	ShellBag  = 1 UMETA(DisplayName = "1 — снаряжение/квестовые (ShellInven, 30 ячеек)"),
	SpendBag  = 2 UMETA(DisplayName = "2 — расходники/самоцветы/ЗОЛОТО (SpendCellobInven, 60 ячеек)"),
	Equipped  = 3 UMETA(DisplayName = "3 — НАДЕТОЕ (11 ячеек + 9 аватар-слотов)"),
	QuestSlot = 4 UMETA(DisplayName = "4 — слоты КВЕСТОВ (12 × 44 б, НЕ предметы)"),
	Warehouse = 5 UMETA(DisplayName = "5 — склад (до 270, страницами по 30)"),
};

// ═══ Слот экипировки = ItemRecord->itemType (сервер: `dstSlot = ItemRecord->itemType`) ═══
//   Имена 0..4, 8, 9 подтверждены `loot_system_re.md` §3 (по ним строится модель наземного лута).
//   Для 5/6/7/10 имён в срезе НЕТ — оставлены номерами (не выдумываем, закон never-invent).
//   ★ Про 5/6/7 известно поведение: при СНЯТИИ они уходят в контейнер 2, остальные — в контейнер 1
//   (`itemAction_SendUseEquipPacket@0xC69900`).
UENUM(BlueprintType)
enum class EAsdaEquipSlot : uint8
{
	Helmet  = 0  UMETA(DisplayName = "0 — шлем"),
	Chest   = 1  UMETA(DisplayName = "1 — грудь"),
	Pants   = 2  UMETA(DisplayName = "2 — штаны"),
	Boots   = 3  UMETA(DisplayName = "3 — обувь"),
	Gloves  = 4  UMETA(DisplayName = "4 — перчатки"),
	Slot5   = 5  UMETA(DisplayName = "5 — (имени в срезе нет; снимается в контейнер 2)"),
	Slot6   = 6  UMETA(DisplayName = "6 — (имени в срезе нет; снимается в контейнер 2)"),
	Slot7   = 7  UMETA(DisplayName = "7 — (имени в срезе нет; снимается в контейнер 2)"),
	Shield  = 8  UMETA(DisplayName = "8 — щит"),
	Weapon  = 9  UMETA(DisplayName = "9 — оружие"),
	Slot10  = 10 UMETA(DisplayName = "10 — (имени в срезе нет)"),
	// ★ ЗАДЕЛ ПОД АВАТАР (решение юзера): в игре не реализовано, контролов нет ни в одном живом окне.
	//   Индексы взяты из кода клиента: `uiEquipSlotSelect` 83531…83539 → 11…19.
	Avatar0 = 11 UMETA(DisplayName = "11 — аватар 0 (не реализовано в оригинале)"),
	Avatar1 = 12 UMETA(DisplayName = "12 — аватар 1 (не реализовано в оригинале)"),
	Avatar2 = 13 UMETA(DisplayName = "13 — аватар 2 (не реализовано в оригинале)"),
	Avatar3 = 14 UMETA(DisplayName = "14 — аватар 3 (не реализовано в оригинале)"),
	Avatar4 = 15 UMETA(DisplayName = "15 — аватар 4 (не реализовано в оригинале)"),
	Avatar5 = 16 UMETA(DisplayName = "16 — аватар 5 (не реализовано в оригинале)"),
	Avatar6 = 17 UMETA(DisplayName = "17 — аватар 6 (не реализовано в оригинале)"),
	Avatar7 = 18 UMETA(DisplayName = "18 — аватар 7 (не реализовано в оригинале)"),
	Avatar8 = 19 UMETA(DisplayName = "19 — аватар 8 (не реализовано в оригинале)"),
};

// ═══ ЗАПИСЬ ПРЕДМЕТА — 57 байт (0x39), одна и та же у клиента и сервера ═══
//   Конструкторы байт-в-байт: клиент `CSlot_Init@0x974330`, сервер `sub_40A9F0@0x40A9F0`.
//   ★★★ ИСТОЧНИК ИСТИНЫ — СЫРЫЕ 57 БАЙТ (`Raw`): пакеты 5000/5002/5006 несут 11-байтовый item-ref,
//   собранный ИЗ ПОЛЕЙ ЗАПИСИ (+5, +7, +4, +9, +14), а НЕ индекс ячейки. Разобранные поля ниже —
//   зеркало для чтения, редактора и дампов; писать их надо только через сеттеры (они правят и `Raw`).
USTRUCT(BlueprintType)
struct ASDAREBORN_API FAsdaItemInstance
{
	GENERATED_BODY()

	// Сырые 57 байт. Пусто ⇒ слот никогда не заполнялся (эквивалент ItemCode == 0).
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="Asda|Inventory") TArray<uint8> Raw;

	/*+0 i32 */ UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="Asda|Inventory") int32 ItemCode = 0;
	/*+4 u8  */ UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="Asda|Inventory") uint8 ContainerType = 0;
	/*+5 i16 */ UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="Asda|Inventory") int32 SlotIndex = -1;
	/*+7 i16 */ UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="Asda|Inventory") int32 SecondIndex = -1;
	/*+9 i32 */ UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="Asda|Inventory") int32 Amount = 0;
	// ★★★ +13 u8 — ТЕКУЩАЯ ПРОЧНОСТЬ предмета. ИСПРАВЛЕН МИСНЕЙМ (2026-08-25): поле называлось
	//   `EquippedFlag` («надет ли»), и имя врало — оно нигде по смыслу не использовалось, поэтому
	//   ошибка не всплывала. Доказано ДВУМЯ независимыми местами эталона:
	//     • `item_inventory.c:37018-37023` — строка тултипа «Прочность: N/M» (нотис 110308) берёт
	//       N из `запись+13`, M из `ItemRecord->field68`, и красит строку В КРАСНЫЙ (−65536)
	//       ровно при `запись+13 == 0`;
	//     • `network.c:14982` — сервер присылает это значение в пакете:
	//       `aLocalItemTemplate[57 * slotIdx + 13] = payload[27 * slotIdx + 6]`.
	//   На нём же держится предупреждение «Прочность одной из твоих вещей упала до 0» (нотис 110303,
	//   `game_main.c:1508-1524`). Максимум прочности живёт НЕ в записи, а в справочнике предмета.
	/*+13 u8 */ UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="Asda|Inventory") uint8 Durability = 0;
	// ★ +14 i16 — ВЕС СТЕКА (`weightOfStack`). CONFIRMED: `GetItemWeight@0x4D6FB0` читает `record+14`,
	//   `CharSpendItemUse` вычитает из него `itemTemplate+5` при расходе одной штуки.
	/*+14 i16*/ UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="Asda|Inventory") int32 StackWeight = 0;
	// +16/+18/+20/+22 — четыре гнезда самоцветов (код 7001+); 0 = гнезда нет/пусто.
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="Asda|Inventory") int32 Sowel0 = 0;
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="Asda|Inventory") int32 Sowel1 = 0;
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="Asda|Inventory") int32 Sowel2 = 0;
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="Asda|Inventory") int32 Sowel3 = 0;
	// ★★★ +24 u8 — УРОВЕНЬ ЗАТОЧКИ (он же «грейд» записи). Имя поля историческое (заводили его как
	//   «грейд самоцвета»), но АДРЕСНАЯ АРИФМЕТИКА эталона говорит однозначно:
	//   `g_aInvSlots` = 0x2901968, база `g_InventoryBag` = 0x2901950 ⇒ смещение 0x18 = **+24**.
	//   Это ГЕЙТ надписи «+N%» в ячейке (`CInventoryUI_RefreshBagIconsPage@0xD00990`) и он же —
	//   «заточка запись+24» в заголовке тултипа (0 → просто имя, ≥5 → «[Шедевр]»).
	//   Значение ==10 дополнительно даёт ×1.3 к бонусам самоцветов; уходит в пакет внешности (11×5 б).
	/*+24 u8 */ UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="Asda|Inventory") uint8 GemGrade = 0;
	// ★★★ +27 i16 — ПРОЦЕНТ ЗАТОЧКИ (`g_InvBag_ItemSubId` = 0x290196B ⇒ смещение 0x1B = **+27**).
	//   Печатается как «+N%» под иконкой и умножает базы статов в тултипе (секция 5).
	//   ⚠ Прошлый заход читал его по +0x23 (=35) — это середина пар заточки, отсюда мусорное «+-1%».
	/*+27 i16*/ UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="Asda|Inventory") int32 EnchantPercent = 0;
	// +29: пять пар (i16 код, i16 значение), шаг 4 — заточка/зачарование.
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="Asda|Inventory") TArray<int32> EnchantCode;
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="Asda|Inventory") TArray<int32> EnchantValue;

	static constexpr int32 kRecordSize = 57;

	bool IsEmpty() const { return ItemCode <= 0; }

	// Полная очистка (эталон `CSlot_Init`: нули, кроме +5/+7 = −1).
	//
	// ★★★ П8-7: РАСХОЖДЕНИЕ С ЭТАЛОНОМ НА 14 ПОЛЯХ — ОСТАВЛЕНО СОЗНАТЕЛЬНО.
	//   Эталонный `CSlot_Init@0x974330` (`ui_core.c:1287-1307`) кладёт −1 в ШЕСТНАДЦАТЬ полей:
	//   +5 и +7, четыре гнезда самоцветов (+16/+18/+20/+22) и десять половинок заточки с +29.
	//   Порт кладёт −1 только в +5 и +7, остальное нулями ⇒ расхождение на 14 полях.
	//
	//   ⚠ ПОПРАВКА РЕВИЗИИ ВОЛНЫ 9: ПРЕЖНЕЕ ОБОСНОВАНИЕ БЫЛО ПОСТРОЕНО НА ДВУХ ЛОЖНЫХ ПОСЫЛКАХ.
	//   Говорилось: «эталон сравнивает эти поля с `> 0`, а порт местами с `!= 0`, поэтому −1
	//   сделал бы пустую запись ЗАНЯТОЙ везде». Обе половины неверны:
	//     · у ЭТАЛОНА есть форма `>= 0` — `item_inventory.c:4154` (`*(__int16*)(slot+29) >= 0 ||
	//       *(__int16*)(slot+33) >= 0`), и это единственная форма, РАЗЛИЧАЮЩАЯ −1 и 0.
	//       Значит у оригинала −1 в этих полях ЗНАЧАЩЕЕ, а не «всё равно что ноль»;
	//     · у ПОРТА сравнений `!= 0` на этих полях РОВНО ДВА, и оба — в ДИАГНОСТИЧЕСКОМ
	//       `ToLine()` (`AsdaInventorySubsystem.cpp:406` и `:412`); сравнений `== 0` нет вовсе,
	//       все боевые — упорядоченные.
	//
	//   ★ НАСТОЯЩАЯ ПРИЧИНА НЕ ПРАВИТЬ (её прежняя редакция не называла): единственный
	//   −1-ЧУВСТВИТЕЛЬНЫЙ потребитель порта — `UAsdaItemOptionSubsystem` — читает
	//   НЕ ЭТИ ПОЛЯ, а СЫРЫЕ БАЙТЫ С ПРОВОДА (`AsdaItemOptionSubsystem.cpp:631-633`,
	//   `RdI16(It->Raw.GetData() + 29)`), и вдобавок стоит за гейтом пустоты (`:627`,
	//   `It->IsEmpty()`). Непустая запись получает поля через `SetRaw`, а не из `Clear()`.
	//   ⇒ значение-заполнитель из `Clear()` до −1-чувствительного пути НЕ ДОХОДИТ НИКОГДА,
	//   наблюдаемое расхождение РОВНО НУЛЕВОЕ, а правка сломала бы только диагностику
	//   (`ToLine()` начал бы печатать `гнёзда=[-1,-1,-1,-1]` у каждой пустой ячейки).
	//
	//   ⚠ КОГДА ЭТО ПЕРЕСТАНЕТ БЫТЬ БЕЗОБИДНЫМ: как только появится потребитель, читающий
	//   РАЗОБРАННЫЕ поля (`Sowel*`, `EnchantCode`) в форме `>= 0` или `== -1`. Тогда −1 надо
	//   будет внести ВМЕСТЕ с правкой `ToLine()`, а не вместо неё.
	void Clear();
	// Положить сырые байты и разобрать поля. Len < 57 → запись очищается.
	void SetRaw(const uint8* Src, int32 Len);
	// Перечитать поля из `Raw` (после точечной правки байтов).
	void SyncFromRaw();

	// Сеттеры, которые правят И `Raw`, И зеркало (иначе следующий 5000 уйдёт со старыми полями).
	void SetAmount(int32 V);
	void SetStackWeight(int32 V);
	void SetSlotIndex(int32 V);
	void SetContainerType(uint8 V);

	FString ToLine() const;   // одна строка для дампов
};

// ═══ ТОЧКА ПОДКЛЮЧЕНИЯ СЛЕДУЮЩЕГО ЗАХОДА: тултип предмета и окно характеристик ═══
//   ЗДЕСЬ НИЧЕГО НЕ РИСУЕТСЯ. Инвентарь только СООБЩАЕТ, что нужно показать, и по каким данным.
//   Что понадобится тому, кто будет это делать (собрано этим заходом, чтобы не реверсить заново):
//     • окно тултипа — `ITEM_TIPWIN` (IDNum 5000, `CItemTooltipWnd`, ассет `WBP_ITEM_TIPWIN`),
//       сравнение — `WBP_ItemCompareTip_Win`, самоцветы — `WBP_SOWEL_Tooltip`;
//     • строители эталона: `itemGetTooltipDataById@0xB74B30`, `itemTooltipBuildText@0xB76D00`,
//       `itemBuildLocalizedTooltip@0xBC4790`, `CItemTooltip_BuildOptionLines@0xB62C70`,
//       `ItemTooltip_PopulateWindow@0xC40A10`, `ItemTooltip_PopulateInfoPanel@0xD02C00`,
//       `invSlotBuildTooltip@0xD06670`, для надетого `equipSlotUpdateTooltip@0xC6AFB0`;
//     • источники строк: `Item_tooltip.BIN`, `SpecialItem_TooltipList.BIN`,
//       `SpecialItem_BufTooltipList.BIN`, `ItemOptionTable.BIN`, `ItemOptionValue.BIN`
//       (в порту уже DataTable: `DT_item_tooltip`, `DT_itemoptionrange`, см. `UAsdaItemTableSubsystem`);
//     • поля записи предмета для тултипа — всё есть в `FAsdaItemInstance` (Amount, 4 гнезда,
//       GemGrade, 5 заточек) и в `FAsdaItemRecord` (ItemName, RequiredLevel, ClassRestriction,
//       ItemType/SubType, Price, TradableFlag, TooltipFlag200, ItemGrade);
//     • цвета грейда — ТА ЖЕ таблица, что у наземного лута (`CItemManager_GetDropColor@0x9C45C0`),
//       отдельной «тултип-палитры» в оригинале НЕТ;
//     • ★ сравнения «старый/новый» в срезе эталона НЕТ: `equipSlotUpdateTooltip` строит тултип
//       надетого ОТДЕЛЬНЫМ вызовом, а не как дельту. Формулировка честная: «ссылок в срезе нет».
//     • правая инфо-панель окна (414/415/425/416/…/10045) уже разобрана в отчёте A §5.4 —
//       её контролы этот заход НЕ трогает, они свободны.
//   Правая инфо-панель самого окна инвентаря наполняется тем же мостом (`FillInfoPanel`).
struct FAsdaItemTooltipBridge
{
	// Показать тултип предмета ячейки у курсора. Container — `EAsdaItemContainer`.
	TFunction<void(int32 /*Container*/, int32 /*CellIndex*/, FVector2D /*ScreenPos*/)> ShowItemTooltip;
	// Спрятать тултип.
	TFunction<void()> HideItemTooltip;
	// Наполнить правую инфо-панель окна инвентаря (раскрывается кнопкой 408).
	TFunction<void(int32 /*Container*/, int32 /*CellIndex*/)> FillInfoPanel;
	// Очистить правую инфо-панель (эталон `uiInventoryDetail_Reset@0xD17300`).
	TFunction<void()> ClearInfoPanel;
	// ★ Спец-ПОДСКАЗКА при наведении на служебный контрол окна (эталон `tutorialPopupLayoutSmall`
	//   в обработчике события 8, ui_core.c:47816-47836): 449 → 98300/98400, 10008 → 110325/110300.
	TFunction<void(int32 /*TitleLocId*/, int32 /*BodyLocId*/)> ShowHintTooltip;

	bool IsBound() const { return (bool)ShowItemTooltip; }
};

// ═══ Виджеты одной ЯЧЕЙКИ сетки / слота экипировки ═══
//   Скин `NoImage_icon` имеет Type = Text, поэтому импортёр дал каждой ячейке ПАРУ:
//   план-картинку `{idc}_{juiIdx}_0` (иконка) и `TextBlock {idc}` (СЧЁТЧИК СТАКА, отдельного нет).
USTRUCT()
struct FAsdaInvCellUi
{
	GENERATED_BODY()
	UPROPERTY(Transient) TObjectPtr<UImage>     Icon = nullptr;
	UPROPERTY(Transient) TObjectPtr<UTextBlock> StackText = nullptr;
	UPROPERTY(Transient) FVector2D IconPos = FVector2D::ZeroVector;   // позиция плана в его канвасе
	UPROPERTY(Transient) FVector2D IconSize = FVector2D(32.f, 32.f);
	// ★ Позиция плана в системе координат КОРНЯ окна (сумма Offsets по цепочке CanvasPanelSlot).
	//   Нужна курсорам выбора 409/612: они лежат в ДРУГОМ канвасе (`INVEN_ITEMTRAY2_grp`), чем
	//   ячейки (`INVEN_Icon_Base_grp` со смещением 5,5) — отсюда и был сдвиг рамки на (5,5).
	UPROPERTY(Transient) FVector2D IconRootPos = FVector2D::ZeroVector;
	// Цвет счётчика, который положил импортёр из данных JUI (закон `ui-text-color-exs`).
	//   Эталон временно перекрашивает его в 0xFFFFF200 для «+N%» (`CControl_SetField923(-3584)`)
	//   и возвращает −1 (белый/исходный) для обычного числа — храним, чтобы было куда вернуть.
	UPROPERTY(Transient) FLinearColor StackTextColor = FLinearColor::White;
	// ★★★ ШТОРКА ОТКАТА НАД ЯЧЕЙКОЙ — НАШЕ ОСОЗНАННОЕ ОТСТУПЛЕНИЕ ОТ ОРИГИНАЛА (решение юзера
	//   2026-08-14). В оригинале шторки в `INVEN_WIN` НЕТ: контролов `CoolTime_Effect` в окне нет
	//   вовсе, а все 5 вызовов `hotbarPushItemCooldown@0xE48FD0` и все 12 вызовов
	//   `hotbarRefreshSlotsByItemRef@0xDF0840` адресуют только `g_pQuickSlotBar`
	//   (доказательство двумя методами — `Reports/item_cooldown_premium.md` §2).
	//   План создаётся В РАНТАЙМЕ по геометрии иконки (`UAsdaHotbarSubsystem::MakeCurtainOverCell`),
	//   визуал копируется с плана панели, перегенерация WBP НЕ запускалась.
	//   Отключается ключом `asda.Cooldown.ShowEverywhere 0`.
	UPROPERTY(Transient) FAsdaCooldownCurtain Cool;
};

// ★★★ 09.10.2026 (автолопата, ревизия six_fixes п. 6 / СРЕДН-2): МОДЕЛЬ ТАЙМЕРА ЦИКЛА — эталонный `g_pActionTimer`
//   (`CActionTimer`). Поля сняты дизассемблером с `timerStart@0xD0C820`, `updateTimeoutLevel@0x966750`,
//   `resetActiveAndCounter@0x966850`, `incRefField11@0x9668E0` и геттеров/сеттеров (отчёт `Reports/owner_1009/autoshovel.md`).
//   ★ Поле +0x2C («счётчик подборов» ревизии) — ОДНО поле с уровнем просрочки `timeoutLevel`: `timerStart` пишет в него 0,
//   `updateTimeoutLevel` поднимает до 1/2/3 при просрочке > 500/1000/1500 мс, `incRefField11` прибавляет 1 за попытку подбора.
struct FAsdaAutoDigTimer
{
	bool   bActive = false;   // +0x18 `isActive` (`get_o6`); `resetActiveAndCounter` → 0
	int32  ItemCode = 0;      // +0x1C `payloadId` (`get_o7`) — код инструмента; `resetActiveAndCounter` → 0
	int32  Mode = 0;          // +0x20 режим 33/34 (`get/set_o8`): какой инструмент применяли последним (меш на кости)
	int32  Slot = -1;         // +0x24 слот инструмента (`get/set_o9`) — пишет гейт 11/34 перед 5028
	int32  Cont = 0;          // +0x28 контейнер (`get/set_oA`): 1 — сумка, иначе расходники
	int32  Level = 0;         // +0x2C `timeoutLevel` = «счётчик подборов» (`get_oB`); повтор при ≥ 3
	double StartS = 0.0;      // +0x04 `startTick` (`timeGetTime`), с — у порта `FPlatformTime::Seconds`
	int32  DurationMs = 0;    // +0x14 `durationMs` (13000 у авто-лопаты)
	int32  Repeats = 0;       // [ПОРТ] диагностика: сколько повторных 5028 ушло с начала цикла (обнуляет `AutoDigReset`; у эталона нет)
};

UCLASS()
class ASDAREBORN_API UAsdaInventorySubsystem : public UGameInstanceSubsystem, public FTickableGameObject,
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
	virtual TStatId GetStatId() const override { RETURN_QUICK_DECLARE_CYCLE_STAT(UAsdaInventorySubsystem, STATGROUP_Tickables); }
	// ★ R4-b (14.11.2026): шаблон класса (CDO) не тикает — раньше `Tick` звался дважды за кадр, у рабочего объекта и у шаблона
	//   (ключ `asda.Perf.NoCdoTick`, разбор — `AsdaTickGate.h`). Рабочий объект тикает как раньше.
	virtual bool IsTickable() const override { return AsdaTickGate::NotCdo(this); }
	virtual bool IsTickableInEditor() const override { return false; }
	virtual bool IsTickableWhenPaused() const override { return false; }

	// ── Ёмкости контейнеров (жёстко зашиты в оригинале; расширяется только склад) ──
	// ★ [ПОРТ] 29.10.2026 (инвентарь ×2): сумка (контейнер 1) 30 → 60 — размер массива модели `Shell`. Сколько ячеек
	//   из них ДОСТУПНО сейчас — `GetShellCapacity()` (ключ `asda.Inven.ShellX2`); `GetContainerSize(1)` отдаёт именно её.
	static constexpr int32 kShellSlots      = 60;
	static constexpr int32 kShellSlotsLegacy = 30;  // ёмкость сумки эталона и старого сервера (`asda.Inven.ShellX2 0`)
	static constexpr int32 kSpendSlots      = 60;
	static constexpr int32 kEquipSlots      = 11;   // РАБОЧИХ слотов экипировки
	static constexpr int32 kEquipSlotsTotal = 20;   // 11 + 9 аватар-слотов (задел, см. шапку)
	static constexpr int32 kQuestSlots      = 12;   // ★ НЕ предметы — здесь не моделируются
	static constexpr int32 kStoreSlots      = 270;
	static constexpr int32 kStorePageSize   = 30;

	// ── Сетка окна ──
	// ★ [ПОРТ] 29.10.2026 (инвентарь ×2): 12 × 5 = 60 контролов (эталон 6 × 5 = 30). Код и ассет — ОДНИМ выпуском:
	//   готовность окна = найдено ровно `kGridCells` ячеек, старый ассет (30) делает окно мёртвым.
	static constexpr int32 kGridCells = 60;   // контролов-иконок INVEN_SET0..59
	static constexpr int32 kGridCols  = 12;   // столбцов (эталон 6)
	static constexpr int32 kGridRows  = 5;    // видимых рядов (как у эталона)

	// ★ [ПОРТ] 29.10.2026: ДОСТУПНАЯ ёмкость сумки — 60 (`kShellSlots`) или 30 (`kShellSlotsLegacy`) по ключу
	//   `asda.Inven.ShellX2`. Аксессор для чужих .cpp (имя ключа там не пишем — правило Shipping-сборки).
	static int32 GetShellCapacity();

	// ★ Золото — обычная запись контейнера 2 с этим кодом (её `Amount` = сумма денег).
	static constexpr int32 kGoldItemCode = 20551;

	// ── Ширина окна: 228 свёрнуто ↔ 648 раскрыто (`sub_930E85(m_pInvenWin, 648/228, −1)`) ──
	static constexpr int32 kWinWidthCollapsed = 228;
	static constexpr int32 kWinWidthExpanded  = 648;

	// z-order окна: выше чата (689) и журнала (690), ниже окна смерти (800).
	static constexpr int32 kInvenZ = 720;

	// idnum кнопки главного меню, которая открывает инвентарь (`NEW_SYSTEMMAIN_ITEMBUT`).
	//   ⚠ Индекс таба (1) здесь НЕ дублируем: он лежит в таблице `UAsdaSystemMenuSubsystem`,
	//   которая и есть источник истины «кнопка ↔ таб ↔ окно».
	static constexpr int32 kMenuButtonIdNum = 73402;

	// ═══════════════════ ОКНО ═══════════════════
	// Эталон `uiToggleInventoryWindow@0xE8FA10`.
	UFUNCTION(BlueprintCallable, Category="Asda|Inventory")
	void ToggleWindow();

	// Эталон `uiOpenInventoryWindow@0xCFF300` (со всеми гейтами) / `sowelWindowClose@0xCFF760`.
	UFUNCTION(BlueprintCallable, Category="Asda|Inventory")
	void OpenWindow();
	UFUNCTION(BlueprintCallable, Category="Asda|Inventory")
	void CloseWindow();

	// ★★★ ПРОГРЕВ ОКНА (задача load_hitch 11.09.2026, `Reports/load_hitch/`). Собирает окно заранее и
	//   оставляет его СКРЫТЫМ — ровно в том состоянии, в каком оно остаётся после «открыть → закрыть»
	//   (зона интерфейса без `bWindowOpen` клики не держит, см. GetWindowRect). Зовётся из TrySpawnLocalPlayer
	//   сразу после RevealWorld — в кадре снятия загрузочного экрана (НЕ из самого RevealWorld: его зовёт и
	//   поздний 4055/4056, где сборка стала бы рывком посреди игры). Без прогрева первое открытие — 278 мс:
	//   класс окна 265 мс + атласы иконок 9 мс + сборка и раскладка 45 мс. Ключ отката `asda.Inven.Prewarm 0`.
	void PrewarmWindow();
	// Объектный путь класса WBP_INVEN_WIN — ОДНА точка для EnsureWidget и предзагрузки класса менеджером загрузки.
	static FSoftClassPath GetWindowClassPath();

	UFUNCTION(BlueprintPure, Category="Asda|Inventory")
	bool IsWindowOpen() const { return bWindowOpen; }

	// Виджет окна `INVEN_WIN` (nullptr, пока окно не открывали). Нужен подсистеме тултипа: правая
	//   инфо-панель — это контролы САМОГО окна инвентаря (эталон `ItemTooltip_PopulateInfoPanel`).
	UUserWidget* GetWindowWidget() const { return InvenWidget; }

	// Раскрыть/свернуть правую панель (кнопки 408 / 412; ширина 648 ↔ 228).
	//   `bFromUser` = нажата настоящая стрелка: тогда действует эталонный гейт вкладки у 408 и
	//   запоминается ручная свёртка (Д8). Внутренние вызовы (смена вкладки, авто-раскрытие по
	//   выбору предмета) идут с `false`.
	UFUNCTION(BlueprintCallable, Category="Asda|Inventory")
	void SetPanelExpanded(bool bExpanded, bool bFromUser = false);
	UFUNCTION(BlueprintPure, Category="Asda|Inventory")
	bool IsPanelExpanded() const { return bPanelExpanded; }
	// Авто-раскрытие панели при выборе предмета (эталон item_inventory.c:36325/36337 — ячейка сумки,
	//   ui_core.c:33973 — надетый слот). `ContainerId` = 3 ⇒ надетое (раскрывается на любой вкладке);
	//   по ПУСТОЙ ячейке эталон панель не раскрывает.
	void AutoExpandOnSelect(int32 ContainerId, int32 Cell);

	// ═══════════════════ ВКЛАДКИ И ПРОКРУТКА ═══════════════════
	// 0 = «Расходники» (контейнер 2, 60), 1 = «Снаряжение» (контейнер 1, 30).
	UFUNCTION(BlueprintCallable, Category="Asda|Inventory")
	void SetTab(int32 TabIndex);
	UFUNCTION(BlueprintPure, Category="Asda|Inventory")
	int32 GetTab() const { return CurrentTab; }

	// Прокрутка В РЯДАХ (эталонное «страница» = сдвиг на 6 ячеек). Клампится по размеру контейнера.
	UFUNCTION(BlueprintCallable, Category="Asda|Inventory")
	void SetScrollRow(int32 Row);
	UFUNCTION(BlueprintCallable, Category="Asda|Inventory")
	void ScrollBy(int32 DeltaRows);
	UFUNCTION(BlueprintPure, Category="Asda|Inventory")
	int32 GetScrollRow() const { return ScrollRow[FMath::Clamp(CurrentTab, 0, 1)]; }
	// Максимальный ряд прокрутки текущей вкладки (Д2: считается по РАЗМЕРУ контейнера).
	UFUNCTION(BlueprintPure, Category="Asda|Inventory")
	int32 GetMaxScrollRow() const;

	// ★ Пара `(видимых рядов, всего рядов)` — ровно аргументы эталонного `SetScrollRange(a2, a3)`.
	//   От неё зависит ТОЛЬКО длина бегунка (`Дорожка · a2 / a3`); предел прокрутки берётся у
	//   `GetMaxScrollRow()`. При `asda.Inven.LegacyScrollRange 1` возвращает зашитые числа эталона.
	void GetScrollRangeRows(int32& OutVisibleRows, int32& OutTotalRows) const;
	// Состояние полосы прокрутки текстом (для `asda.Inven.ScrollBar` и общего дампа).
	FString DumpScrollBar() const;

	// Контейнер, который сейчас показывает сетка (2 на вкладке 0, 1 на вкладке 1).
	UFUNCTION(BlueprintPure, Category="Asda|Inventory")
	int32 GetGridContainer() const { return (CurrentTab == 1) ? 1 : 2; }

	// ═══════════════════ МОДЕЛЬ ═══════════════════
	// Ёмкость контейнера (0 = такого контейнера нет / он не моделируется).
	UFUNCTION(BlueprintPure, Category="Asda|Inventory")
	int32 GetContainerSize(int32 Container) const;

	// ★★★ «Содержимое контейнера уже получено с сервера ЦЕЛИКОМ».
	//   Пока false, ПУСТАЯ ячейка означает не «предмета нет», а «данные ещё не пришли» — и любой
	//   потребитель, который по пустоте ПРИНИМАЕТ РЕШЕНИЕ (хотбар чистит слот и отправляет 5067),
	//   обязан дождаться true. Источник факта — пакет-маркер конца выдачи: 4046 (контейнер 1),
	//   4049 (контейнер 2), 4052 (склад 5); надетое (3) приходит одним пакетом 1027.
	//   В оригинале такой проверки нет и не нужно: там `g_InventoryBag` заполняется ПРЯМО В
	//   ОБРАБОТЧИКЕ пакета, а 4045/4048 по проводу приходят РАНЬШЕ 5059 (подтверждено живым
	//   логом 2026-08-09 15:41:50.326/50.707 против 50.801). У нас оба применяются отложенно
	//   в `Tick` (закон `no-reentrant-net-callbacks`), поэтому порядок нужно гарантировать явно.
	UFUNCTION(BlueprintPure, Category="Asda|Inventory")
	bool IsContainerReceived(int32 Container) const;

	// Запись ячейки. nullptr — контейнера/индекса нет.
	const FAsdaItemInstance* GetCell(int32 Container, int32 CellIndex) const;
	FAsdaItemInstance* GetCellMutable(int32 Container, int32 CellIndex);

	UFUNCTION(BlueprintPure, Category="Asda|Inventory")
	int32 GetCellItemCode(int32 Container, int32 CellIndex) const;
	UFUNCTION(BlueprintPure, Category="Asda|Inventory")
	int32 GetCellAmount(int32 Container, int32 CellIndex) const;

	// ★ ОБЩАЯ ТОЧКА (волна 9 заточки, 25.09.2026) — порт `CInventory_CountItemById@0x9998E0` (item_inventory.c:37-70):
	//   снаряжение (`itemIsWeaponOrEquipClass`) — сумка 0…29, прочее — расходники 0…59; при `tradableFlag`
	//   складываются количества (+9), иначе считаются ячейки. Кода без записи item_list эталон считает по
	//   расходникам и падал бы на `->tradableFlag` (NULL) — здесь такие ячейки считаются по одной (как было у
	//   прежней копии квестов). Зовут квесты/диалоги (`UAsdaQuestMarkerSubsystem::CountItemById`) и окно 78200.
	int32 CountItemById(int32 ItemCode) const;
	// ★ Волна В4 ремесла (ревизия В3 МЕЛК-11): тот же обход, что у `CountItemById` (контейнер по классу предмета,
	//   30/60 ячеек, «стек = количество» при `tradableFlag`, иначе 1 за ячейку), но по КАЖДОМУ стеку: Fn(контейнер,
	//   ячейка, единиц в стеке). Одна точка для ремесла (наибольший стек, выбор стека для 6602 — К-6) вместо копии.
	void ForEachItemStack(int32 ItemCode, TFunctionRef<void(int32 /*Container*/, int32 /*Cell*/, int32 /*Units*/)> Fn) const;

	// Сумма денег = `Amount` записи с кодом 20551 в контейнере 2 (эталон: обычная запись).
	UFUNCTION(BlueprintPure, Category="Asda|Inventory")
	int32 GetGold() const;
	// ★ Порт `uiSetHonorPointsDisplay@0xB75D40`: запомнить сумму денег и перерисовать поле 407.
	//   ПУБЛИЧНА С 25.09.2026 (волна 7 заточки): ответ 6501 окна высокой переплавки 78200 несёт НОВУЮ
	//   сумму золота (@231) и ставит её тем же `uiSetHonorPointsDisplay` (network.c:16925). Сетевые
	//   приёмники инвентаря зовут её как и раньше; второй копии «поставить деньги» не заводим.
	void SetGoldDisplay(int32 NewGold);
	// ★ 30.09.2026 (волна О4 торговли): порт `invGetFreeSlotByMarker@0xD1E910` (item_inventory.c:42833) — ПЕРВАЯ ячейка контейнера 2
	//   с кодом 20551 получает количество `V` (только при V > 0; перерисовки эталон не делает). Его зовёт `uiSetHonorPointsDisplay`
	//   (значит, и `SetGoldDisplay`) и напрямую — итог обмена 5294 (network.c:14957-14966: отдал всё — в ячейке 1).
	void SetGoldCellAmount(int32 V);

	// Первый свободный слот контейнера (эталон `sub_4D6740`), −1 если мест нет.
	UFUNCTION(BlueprintPure, Category="Asda|Inventory")
	int32 FindFreeSlot(int32 Container) const;

	// ═══════════════════ ДЕЙСТВИЯ (все шлют 5000/5028 и ЖДУТ ответа) ═══════════════════
	// Переместить/поменять местами внутри контейнера ЛИБО между 1↔1, 2↔2, 5↔5 (сервер: 1↔2 ЗАПРЕЩЕНО).
	UFUNCTION(BlueprintCallable, Category="Asda|Inventory")
	bool MoveItem(int32 SrcContainer, int32 SrcCell, int32 DstContainer, int32 DstCell);

	// Надеть предмет из контейнера 1 или 2 (эталон: dstType = 3, dstSlot = ItemRecord->itemType; у КОЛЕЦ из
	//   контейнера 2 — правило пары слотов 5/6, см. `EquipItemToSlot`). Двойной клик / панель / консоль.
	UFUNCTION(BlueprintCallable, Category="Asda|Inventory")
	bool EquipItem(int32 SrcContainer, int32 SrcCell);

	// ★★★ [ОРИГИНАЛ] 26.10.2026 (баг игроков «второй слот кольца не работает»): надеть с учётом СЛОТА-ЦЕЛИ броска.
	//   `DropEquipSlot` — слот манекена под курсором при броске (0…10), −1 — двойной клик (цели нет).
	//   Эталон: кольцо (`itemType` 5 или 6) из контейнера 2 — бросок на 605 → слот 5, на 606 → слот 6
	//   (`netItem_SendUseRequest@0xD19A70`, network.c:18632-18652); иначе «первый свободный»: 5 пуст → 5, 5 занят и 6 пуст
	//   → 6, оба заняты → 5 (замена) — `CInventory_OnSlotUse@0xD07460` (item_inventory.c:37940-37956). Прочие вещи —
	//   `itemType`, куда бы ни бросили. Откат: `asda.Inven.RingSlotPick 0`.
	bool EquipItemToSlot(int32 SrcContainer, int32 SrcCell, int32 DropEquipSlot);

	// ★ Гейт эталона `itemUseCheckBagFull@0xD12370`: надеваем ДВУРУЧНОЕ (поле шаблона +0xA8 == 2),
	//   а заняты и слот 9 (оружие), и слот 8 (щит) ⇒ в сумку поедут ДВЕ вещи. Полная сумка ⇒
	//   чат-нотис **1011** «Нет места в рюкзаке.» и отказ. Нет записи в item-БД ⇒ нотис **501**.
	//   true = можно слать пакет.
	bool CheckBagRoomForTwoHand(int32 ItemCode) const;

	// ★ Ответ «ОК» на подтверждение привязки при надевании (loc **7201**, гейт `net_sendInventoryItemUse`
	//   №4): помечает запись привязанной (байт +50 = 1, как `inventoryApplySlotItem` case 3) и шлёт 5000.
	//   `DropEquipSlot` — слот-цель броска, с которого начали (−1 — двойной клик); слот считается по нему В МОМЕНТ «ОК».
	void ConfirmBindAndEquip(int32 SrcContainer, int32 SrcCell, int32 DropEquipSlot = -1);

	// Снять надетое (эталон `itemAction_SendUseEquipPacket@0xC69900`): dstType = 2 при
	//   equipSlot ∈ {5,6,7} или `ItemRecord->flagWord176`, иначе 1; dstSlot = 0 (сервер сам ищет место).
	UFUNCTION(BlueprintCallable, Category="Asda|Inventory")
	bool UnequipItem(int32 EquipSlot);
	// ★ 07.11.2026 [ИСПРАВЛЕНИЕ] (`asda.Mount.DismountOnUnequip`): слот своей заявки «снять», ждущей ответ 5001 (−1 — заявки нет), и момент
	//   последней команды «слезть», посланной из-за снятия. Разбор — у ключа в AsdaInventorySubsystem_Net.cpp.
	int32 PendingUnequipSlot = -1;
	double UnequipDismountSentS = -1000.0;
	// Снятие подтверждено сервером: своя заявка, слот пуст, транспорт числится — послать 5455 (запись (15, 2) ждёт ответа 5454).
	void DismountAfterUnequip(int32 EquipSlot);

	// Использовать расходник (5028). TargetUserIdx заполняется только при `itemSubType == 15`.
	UFUNCTION(BlueprintCallable, Category="Asda|Inventory")
	bool UseConsumable(int32 Container, int32 CellIndex, int32 TargetUserIdx = -1);

	// ★ ОФЛАЙН-ЭМУЛЯЦИЯ ПРИХОДА 5029 (консоль `asda.Inven.Recv5029`, сервер не нужен). Тело — те же
	//   24 байта, что собирает `CharSpendItemUse@0x4D8C40`; идёт штатным путём «очередь → Tick».
	void SimulateUseResult(const TArray<uint8>& Body24) { OnUseResult(Body24); }

	// ══════════ ★★★ ВОЛНА 2: ТРИ КАНАЛА ПРИМЕНЕНИЯ МИМО 5028/5029 ══════════════════════════════
	//   Развёрнутые комментарии с адресами эталона — у реализаций в .cpp.

	// A. КОРОБКИ/ПАКЕТЫ (6096 → 6097), 5 045 применяемых кодов.
	//    ★★★ ЭТАЛОННОЕ ПОВЕДЕНИЕ ВОССТАНОВЛЕНО: двойной клик НЕ шлёт 6096, а ОТКРЫВАЕТ панель
	//    состава 26500 (`item_inventory.c:37873-37878` →
	//    `stampWnd_PopulateItemSlots(g_pPackageStampWindow, code, /*stampMode*/ 1, 0)`).
	//    Пакет уходит только по кнопке 26502 через отложенную защёлку `UAsdaPackagePanelSubsystem`.
	//    Прежнее временное отступление волны 2 (двойной клик = кнопка подтверждения) СНЯТО.
	UFUNCTION(BlueprintCallable, Category="Asda|Inventory")
	bool OpenPackageItem(int32 Container, int32 CellIndex);

	// B. ГАЧА («бустер», 5468 → 5469, затем 5474 → 5473 соседям).
	//    ★★★ ЭТАЛОННОЕ ПОВЕДЕНИЕ ВОССТАНОВЛЕНО: двойной клик НЕ шлёт 5468, а ОТКРЫВАЕТ окно
	//    `BOOSTING_WIN` (10050) — `item_inventory.c:38076/38368` → `itemBoosterWnd_Open@0xD1F4A0`.
	//    Пакет уходит только по кнопке START (10069). Временное отступление волны 2 СНЯТО.
	UFUNCTION(BlueprintCallable, Category="Asda|Inventory")
	bool UseGachaItem(int32 Container, int32 CellIndex);

	// ★★★ ПРИМЕНЕНИЕ ПРИЗА ГАЧИ — ветка успеха 5469, вынесена из `ApplyGachaResult`, потому что в
	//    эталоне она вызывается ИЗ ТРЁХ РАЗНЫХ ТОЧЕК (третья найдена при повторной сверке):
	//      • кнопка OK (10070) окна 10050 — `OnRecv_ItemEnchantResult@0xD20B30` (имя врёт):
	//        эффект удачи ЕСТЬ, звука НЕТ, 5474 уходит;
	//      • ветка «окно закрыто» приёма 5469 — `network.c:17060+`: эффект + звук + 5474;
	//      • ★★★ ЗАКРЫТИЕ ОКНА — `boosterApplyResult@0xD1F830` (`shop_trade.c:12066-12093`):
	//        приз ВЫДАЁТСЯ и здесь (`OnRecv_RemoveSlot_byPtr` + `OnRecv_ItemUpdate` + вес +
	//        `CInventoryUI_ResetAndRefresh` + чат-нотис 20066), но БЕЗ эффекта, БЕЗ звука и
	//        БЕЗ 5474 — в теле нет ни `Effect_SpawnByName`, ни отправки пакета.
	//    `bPlayFxSound` — играть звук удачи; `bSpawnFx` — спавнить сам эффект; `bSend5474` —
	//    просить сервер показать эффект соседям. Раньше двух последних не было, и путь
	//    «закрыть окно / таймаут 30 с» ПРИЗ ТЕРЯЛ.
	void ApplyGachaPrize(const TArray<uint8>& Body68, bool bPlayFxSound,
	                     bool bSpawnFx = true, bool bSend5474 = true, bool bRareNotice = true);

	// C. ПРЕМИУМ/ФУНКЦИОНАЛЬНЫЕ (5450 / 5455). `Message` заполняется только у веток со строкой
	//    (переименование/объявление, поле @55 запроса, 200 б); для остальных — пустая.
	UFUNCTION(BlueprintCallable, Category="Asda|Inventory")
	bool UsePremiumItem(int32 Container, int32 CellIndex, const FString& Message = FString());
	// ★ 07.10.2026 (волна 2-Г, ревизия С-1): ОБЩАЯ ТОЧКА `CItemManager_UseInventoryItem@0xA0FDF0` для премиум-предмета —
	//   развилка `switch (subType − 35)`: case 3 (SubType 38, свиток перемещения) и case 29 (SubType 64, свиток призыва) →
	//   окно ввода имени 1552 (`UAsdaScrollCallSubsystem::OpenNameBox`), прочее → `UsePremiumItem` (5450/5455).
	//   Зовут ОБА эталонных входа: двойной клик (`CInventory_OnSlotUse`) и панель быстрых слотов (`Hotbar_SlotActivate`,
	//   ветка `else if (Table2Record)`, ui_core.c:67414/67421). Ветки 104/107 сюда НЕ входят — они только у `CInventory_OnSlotUse`.
	bool UseSpecialItemDispatch(int32 Container, int32 CellIndex);

	// ★ ОФЛАЙН-ЭМУЛЯЦИИ ПРИХОДА ОТВЕТОВ (сервер не нужен) — те же тела, что собирает сервер;
	//   идут штатным путём «колбэк → очередь → Tick», то есть заодно проверяют
	//   [[no-reentrant-net-callbacks]].
	void SimulatePackageResult(const TArray<uint8>& Body300)  { OnPackageResult(Body300); }
	void SimulateGachaResult(const TArray<uint8>& Body68)     { OnGachaResult(Body68);   }
	void SimulateFunctionResult(const TArray<uint8>& Body68)  { OnFunctionResult(Body68);}

	// Порт `itemFindSowelSlots@0xDAACF0`: подобрать до пяти ячеек-приёмников под содержимое коробки.
	//   `OutSlots15` — 5 троек {u8 контейнер, i16 слот} в порядке позиций состава; `OutCount` —
	//   сколько позиций реально используется; `OutShort*` — нехватка ячеек по контейнерам.
	//   Публичный: тем же расчётом пользуется консольная проверка состава (`asda.Inven.Package`)
	//   и им же будет пользоваться панель `Package_Contents_Win`, когда её привяжут.
	bool ReservePackageSlots(int32 ItemCode, TArray<uint8>& OutSlots15,
	                         int32& OutCount, int32& OutShortSpend, int32& OutShortBag) const;

	// ★ ОБЩАЯ ТОЧКА (закон `port-shared-points`): нарисовать иконку предмета по коду в план 32/64 px.
	//   Публичной сделана ради панели «Состав пакета» (26500) и окна «Барабан гачи» (10050) —
	//   у них ровно тот же эталонный вызов `CItemManager_GetImgOptionPtr/Value/Mask` →
	//   `TexturePath_Format` → `CControl_DrawTextureRect`. Своей копии подсистемы окон НЕ заводят.
	bool ApplyIconToImage(UImage* Img, int32 ItemCode, float DrawSize) const;
	void ClearIcon(UImage* Img) const;

	// ★★★ ОТПРАВКА 6096 — вынесена из `OpenPackageItem`, потому что в эталоне двойной клик пакет НЕ
	//   шлёт: он открывает панель 26500, а шлёт ОТЛОЖЕННАЯ ЗАЩЁЛКА панели
	//   (`itemPackAgeToggleSendAction@0xDAB6E0`, `game_main.c:1480`). Зовётся только оттуда.
	//   Подбор слотов-приёмников и проверка места делаются здесь ещё раз — как в эталонном
	//   `net_SendItemActionReq_5slot@0xDAB7E0` (`network.c:24479`), который повторяет
	//   `itemFindSowelSlots` и при нехватке МОЛЧА не шлёт (нотиса в этой точке нет).
	bool SendOpenPackage6096(int32 ItemCode, int32 ContainerId, int32 CellIndex);

	// Двойной клик по ячейке — эталон `itemUseDoubleClick(inv, slot, bFromBag)`:
	//   расходник (`ItemRecord->itemType == 11`) → 5028, иначе 5000 dstType = 3 (НАДЕТЬ).
	//   ★ Сюда же сведены развилки второго эталонного обработчика — окна сумки `CInventory_OnSlotUse@0xD07460`
	//   (коробки 200/201, обмен 202, расширитель 104 …). ★ 27.09.2026 (itemsfix, СРЕДН-3): ветка 202 (обмен 6098)
	//   есть ТОЛЬКО в `CInventory_OnSlotUse` (item_inventory.c:37894 и :38173), не в `itemUseDoubleClick`;
	//   с панели быстрого доступа порт тоже делает обмен — [ИСПРАВЛЕНИЕ ДЕФЕКТА ОРИГИНАЛА], обоснование у ветки в .cpp.
	UFUNCTION(BlueprintCallable, Category="Asda|Inventory")
	bool UseByDoubleClick(int32 Container, int32 CellIndex);

	// ★ Волна В4 ремесла: двойной клик ПО ЯЧЕЙКЕ ОКНА СУМКИ — порт `CInventory_OnSlotUse@0xD07460` поверх общего
	//   `UseByDoubleClick`. Отличие двух эталонных обработчиков, которое здесь сохранено: ветки свитков ремесла
	//   (`linkCode` 101 — изучить рецепт, 102 — поднять уровень ремесла; item_inventory.c:37833-37852) есть ТОЛЬКО в
	//   `CInventory_OnSlotUse` и только для расходников (контейнер 2, `m_nSelKind == 0`); хотбар идёт через
	//   `itemUseDoubleClick@0xD12580`, где этих веток нет — свиток с хотбара рецепт НЕ учит (ревизия реверса П-11).
	bool UseFromBagWindow(int32 Container, int32 CellIndex);

	// Уничтожить предмет (5006). Подтверждение — как в эталоне (msgbox 1523, контекст 0x7D5 = нотис 2005).
	UFUNCTION(BlueprintCallable, Category="Asda|Inventory")
	bool DestroyItem(int32 Container, int32 CellIndex);

	// ★ 26.10.2026 (баг игроков «перетаскивание за окно не удаляет»): то же уничтожение, но с ЭТАЛОННЫМ нотисом вопроса.
	//   2005 «Выбросить предмет?» — корзина 405 (`sowelHandleSlotSelect`, контекст окна количества 2); 2059 «Предмет будет
	//   уничтожен…» — бросок вне всех окон (`invSlotShowTooltipOrPopup`/`invSlotHoverShowTooltip`, контекст 1). ОК обоих
	//   вопросов — `netSendItemActionSecure` → 5006 (ui_core.c:57060-57065). Гейт окна количества у веток один.
	bool DestroyItemAsk(int32 Container, int32 CellIndex, int32 AskLocId);

	// ★ КОРЗИНА 405 — эталон `sowelHandleSlotSelect@0xD14A90`: уничтожает ВЫБРАННУЮ ячейку.
	//   Золото (20551) корзина игнорирует (`return 0` в эталоне).
	UFUNCTION(BlueprintCallable, Category="Asda|Inventory")
	bool DestroySelected();

	// Выбросить на землю (5002). ⚠ В КЛИЕНТЕ ОРИГИНАЛА ОТПРАВИТЕЛЯ НЕТ (дефект §8.3) —
	//   поэтому это НЕ цель корзины, а отдельная команда для проверки серверной ветки.
	UFUNCTION(BlueprintCallable, Category="Asda|Inventory")
	bool DropItemToGround(int32 Container, int32 CellIndex);

	// ★ АВТОСОРТИРОВКА (Д1) — эталон `inventoryAutoArrange@0xD35340`. Сортирует ТЕКУЩУЮ вкладку.
	//   Аргумент оригинала (`cmdId − 83691`) в теле функции НЕ ИСПОЛЬЗУЕТСЯ вовсе: контейнер берётся
	//   из поля вкладки `this[129]`. Мы принимаем его ради 1:1 сигнатуры и пишем в лог.
	//   ★ То же самое в Asda Global: `inventoryAutoArrange@0x0078D840` (VA Global) объявлен
	//   `(this, int a2)` и кончается `ret 4`, но `a2` не читается (Hex-Rays + сплошной дизассемблер
	//   тела 0x0078D840..0x0078E1F8 — ни одного `[ebp+8]`), а вкладка берётся из `this+0x260`,
	//   поэтому «Organize Inventory 1» и «Organize Inventory 2» раскладывают ОДИНАКОВО.
	UFUNCTION(BlueprintCallable, Category="Asda|Inventory")
	void AutoArrange(int32 Mode = 0);

	// Запросить страницу склада (4050 → 4051/4052). PageNo 1-based.
	UFUNCTION(BlueprintCallable, Category="Asda|Inventory")
	void RequestWarehousePage(int32 PageNo);

	// ★★★ ПОЛОЖИТЬ ПРЕДМЕТ ЯЧЕЙКИ В СЛОТ ПАНЕЛИ БЫСТРОГО ДОСТУПА — единая точка входа для мыши
	//   (бросок из сумки на слот) и консоли. Эталон `hotbarAssignInventoryItem@0xD11840`
	//   (ui_core.c:47231) со ВСЕМИ его гейтами; запись слота = «контейнер + индекс ячейки», сразу
	//   уходит страница 5067. Возврат: true — предмет положен, false — отказ (причина в логе/нотисе).
	UFUNCTION(BlueprintCallable, Category="Asda|Inventory")
	bool AssignToHotbarSlot(int32 ContainerId, int32 CellIndex, int32 HotbarAbsSlot);

	// ★ Применить 57-байтовую запись, пришедшую в ЧУЖОМ пакете (сейчас это 6166 «рыболовная снасть
	//   на удочку», `UAsdaFishingToolSubsystem`). Маршрут — ПО ПОЛЯМ САМОЙ ЗАПИСИ: +4 контейнер,
	//   +5 слот; `amount(+9) == −1` ⇒ слот ОЧИЩАЕТСЯ. Это ровно то, что делает эталонный
	//   `invHandleItemSlotSyncPacket@0xFCA590` (`Inventory_SetBagSlot@0xD61650` для записи и
	//   `memset(…, 0, 0x39)` для amount == −1). Модель контейнеров принадлежит инвентарю, поэтому
	//   писать в неё имеет право только он — окно снасти лишь передаёт сюда байты.
	//   ⚠ Звать ТОЛЬКО из Tick вызывающего, не из сетевого колбэка (закон no-reentrant-net-callbacks).
	//
	//   ★★★ РЕЖИМ `CountOrClear` (ремесло, волна В1 25.09.2026; ревизия `craft_re_REVIEW.md` §6).
	//   Приёмники ремесла 6599/6603/6605 применяют запись НЕ так, как общий путь: эталон
	//   (`item_enhance.c:10388-10406`, `network.c:49540-49554`, `:50041-50055`) для записи с кодом > 0
	//   при `количество (+9) > 0` ставит ТОЛЬКО количество ячейке с этим слотом (`slotTableSetFieldById
	//   @0x99A460`: контейнеры 1/2), а при `количество <= 0` УДАЛЯЕТ слот (`OnRecv_RemoveSlot_byPtr@0xD0CE30`:
	//   контейнеры 1/2). Сервер для израсходованного материала шлёт именно count = 0 (статус 3 →
	//   `sub_40A9F0` + код/контейнер/слот, GameServer.c:121803-809).
	//   ⚠ ПОЧЕМУ НЕ ПРАВИМ ОБЩИЙ ПУТЬ: «count 0 ⇒ удалить» в режиме по умолчанию СНЁС БЫ ПРЕДМЕТЫ у всех
	//   прочих вызывающих — непакуемые вещи (снаряжение) приходят с количеством 0 (Д10, см. счётчик
	//   расходников `CVarLegacyZeroCount`), а через эту точку идут записи 6166, 6505, 6501, 6582, 5029,
	//   питомцы, самоцветы и почта. Их контракт (`−1 ⇒ очистить, иначе положить`) — эталонный
	//   `invHandleItemSlotSyncPacket` — остаётся прежним; ремесло выбирает свой режим явно.
	enum class EExternalRecordMode : uint8
	{
		Default,        // −1 ⇒ очистить, иначе положить запись целиком (`invHandleItemSlotSyncPacket`)
		CountOrClear,   // ремесло: count > 0 ⇒ только количество, count <= 0 ⇒ удалить (контейнеры 1/2)
		AsIs,           // ★ 30.09 (ревизия О4 МЕЛК-3): `OnRecv_ItemUpdate` — запись кладётся КАК ЕСТЬ при любом количестве (итог обмена)
	};
	bool ApplyExternalRecord(const TArray<uint8>& Rec57, EExternalRecordMode Mode = EExternalRecordMode::Default);

	// ═══════════════════ СОБЫТИЯ ДЛЯ СМЕЖНЫХ СИСТЕМ ═══════════════════
	// Ячейка изменилась. Container == −1 ⇒ «перестроить всё» (после батчей 4045/4048/4049).
	DECLARE_MULTICAST_DELEGATE_TwoParams(FOnAsdaInvCellChanged, int32 /*Container*/, int32 /*CellIndex*/);
	FOnAsdaInvCellChanged OnCellChanged;

	// ★ Курсор над ЛЮБОЙ ячейкой сетки 10010…10039. Нужно системе самоцветов: эталонный бросок
	//   камня ИЗ ГНЕЗДА засчитывается при отпускании над `0x271A…0x2737`
	//   (`inventoryWin_OnUiCommand`, ветка `m_nSelKind == 3` → `invItemDropSendPacket`).
	UFUNCTION(BlueprintPure, Category="Asda|Inventory")
	bool IsCursorOverGridCell() const;

	// ★ НИЖНИЙ РЯД КНОПОК окна (449 / 452 / 453 / 454 / 10082). Каждая в эталоне ОТКРЫВАЕТ ЧУЖОЕ
	//   ОКНО, а не делает что-то в инвентаре:
	//     449   `Inven_ItemUpgrade_Btn`  → `uiCombineWindow_Close@0xD28340`  — окно УЛУЧШЕНИЯ снаряжения
	//                                       (подпись = нотис 98300, hover-подсказка 98300/98400);
	//     452   `Inven_Smelting_Btn`     → `tileInteractGateAndOpen(g_pItemMainWnd)` — СОЗДАНИЕ предмета
	//                                       (подпись = нотис 15305);
	//     453   `Inven_Disassembly_Btn`  → `upgradeUiOnOpen(g_pRecvUiHandler3)`      — ДЕМОНТАЖ снаряжения
	//                                       (★ кэшируется ДВАЖДЫ: подпись сперва 100003, затем 15306 —
	//                                        побеждает 15306, потому что второй `Collection_AddString` позже);
	//     454   `Inven_PetCageBtn`       → `CRefineWnd_Open`                          — КЛЕТКА ПЕТА (без подписи);
	//     10082 `Inven_primium_item_btn` → `uiBagWindow_OpenAndSync`                  — ЭЛИТНЫЙ ПОЧТОВЫЙ ЯЩИК
	//                                       (нотис 110666; эталон ещё принудительно `SetVisible(1)`).
	//   Сами эти окна — ОТДЕЛЬНЫЕ системы, в порту их пока нет. Инвентарь их НЕ рисует и ничего не
	//   выдумывает: он транслирует нажатие наружу этим делегатом (кто реализует окно — подпишется),
	//   а если подписчиков нет — только пишет в лог. Аргумент = idnum кнопки.
	DECLARE_MULTICAST_DELEGATE_OneParam(FOnAsdaInvSideButton, int32 /*IdNum*/);
	FOnAsdaInvSideButton OnSideButtonClicked;

	// ═══════ ★★★ РЕЗУЛЬТАТ МАГАЗИНА NPC — ОДНА ТОЧКА ПРИМЕНЕНИЯ В МОДЕЛЬ (окно 1300) ═══════════
	// ПОЧЕМУ ЗДЕСЬ, А НЕ В ПОДСИСТЕМЕ МАГАЗИНА: контейнеры — собственность инвентаря, и в эталоне
	//   приёмники магазина тоже пишут ПРЯМО в `g_InventoryBag` / `g_InventoryArray2`
	//   (`Recv_ShopSell_Result@0xE28FD0` и `Recv_ShopBuy_Result@0xE31630`). Второй копии модели
	//   заводить нельзя (закон `port-shared-points`).
	// ⚠ Звать ТОЛЬКО из `Tick` владельца окна, не из колбэка пакета (закон `no-reentrant-net-callbacks`).

	// 5021 (406 б), ветка успеха: до ПЯТИ записей по 57 б с адреса +1 (в теле их семь — столько
	//   крутит сервер `CharItemBuy@0x50F170`, но корзина окна вмещает пять, и эталон разбирает пять),
	//   деньги @400, вес @404 (вес обновляет сам `UAsdaNetSubsystem`).
	//   ★ ИСПРАВЛЕН ДЕФЕКТ ОРИГИНАЛА Д1 (см. .cpp): эталон для контейнера 1 сперва кладёт запись,
	//     а следом ЗАТИРАЕТ ту же ячейку нулями. Мы кладём и оставляем.
	//                                      откат на точное поведение: `asda.Shop.LegacyBuyClearsShell 1`
	void ApplyShopBuyRecords(const TArray<uint8>& Body406);

	// 5019 (62 б), ветка успеха: пять 11-байтовых item-ref с адреса +1 (слот @+0 i16, контейнер @+4 u8,
	//   новое количество @+5 i32), деньги @56, вес @60. Количество ≤ 0 ⇒ ячейка очищается.
	void ApplyShopSellRefs(const TArray<uint8>& Body62);

	// ★★★ 6572 (170 б), ветка успеха — РЕЗУЛЬТАТ РЕМОНТА (`netHandleStateArrayPacket@0xEDB2B0`,
	//   network.c:32731). Эталон делает ровно три вещи и в этом порядке:
	//     1) `uiSetHonorPointsDisplay(g_pGameClient, тело+166)` — НОВОЕ ЗОЛОТО (имя функции врёт,
	//        это деньги: подпись поля окна ремонта = нотис 13241 «Золото:»);
	//     2) 41 раз: если слот `тело+43+2i` != −1, положить `тело+125+i` в поле записи **+13**
	//        (ТЕКУЩАЯ ПРОЧНОСТЬ) того контейнера, что лежит в `тело+2+i`
	//        (2 — расходники, 1 — сумка, 3 — надетое; эталон гейтит их РАЗНЫМИ границами слота:
	//        `< 60`, `<= 29`, `<= 10`);
	//     3) перерисовать окно ремонта — это делает уже сама подсистема ремонта.
	//   ⚠ Второй записи прочности в порту нет: `FAsdaItemInstance::Durability` — зеркало `Raw+13`,
	//   поэтому правим СЫРОЙ байт и пересобираем поля (`SyncFromRaw`), иначе следующий 5000 уйдёт
	//   со старым значением.
	//   Возвращает число реально изменённых слотов (для журнала подсистемы ремонта).
	int32 ApplyEquipRepairResult6572(const TArray<uint8>& Body170);

	// ★ 08.10.2026 (рыбалка В1): применение 6570 «износ предмета» (`netApplySlotStateFlag@0xEDB150`) — зовётся из Tick
	//   (очередь `PendingPackets`) и самотестом рыбалки. @0 u8 контейнер, @1 i16 слот, @3 u8 новая прочность → поле
	//   записи +13. Границы слота эталона РАЗНЫЕ (как у 6572): 2 → `< 0x3C`, 1 → `<= 0x1D`, 3 → `0…10`; прочее — молча.
	void ApplySlotDurability6570(const TArray<uint8>& Body4);

	// ★★★ 6163 (121 б), ветка успеха — РЕЗУЛЬТАТ ПОКУПКИ В ВОЕННОМ МАГАЗИНЕ
	//   (`uiHandleItemActionResult@0xB75580`, ui_core.c:8248): деньги @3, затем ДВЕ записи по 57 б
	//   (@7 списанная цена, @64 купленный предмет). Эталон для каждой записи смотрит её поле
	//   +9 (количество): `== −1` ⇒ ячейку ОЧИСТИТЬ, `>= 0` ⇒ ПОЛОЖИТЬ запись как есть.
	void ApplyWarShopBuyRecords(const TArray<uint8>& Body121);

	// ★★★ 6554 (119 б), ветка успеха — РЕЗУЛЬТАТ СМЕНЫ ПАРАМЕТРОВ ПРЕДМЕТА (окно 50300,
	//   `Net_OnItemUpgradeResult@0xB61990`, item_inventory.c:3880). ДВЕ записи по 57 б: @1 свиток
	//   после списания, @58 снаряжение с новыми параметрами. Эталон РАЗЛИЧАЕТ КОНТЕЙНЕР записи:
	//     • слот записи (+5) == −1 ⇒ запись пропускается целиком;
	//     • контейнер 2 ⇒ `memcpy(g_InventoryArray2 + 57*слот, запись, 0x39)` — положить как есть;
	//     • контейнер 1 ⇒ `memset(g_InventoryBag + 57*слот, 0, 0x39)`, а затем
	//       `Inventory_SetBagSlot@0xD61650`, который кладёт запись обратно ТОЛЬКО при `код > 0`.
	//       То есть «очистить, и если код ненулевой — положить». Именно так свиток и исчезает,
	//       когда стопка кончилась: сервер (`recvDBItemOptionTrans@0x536A00`) присылает запись
	//       с одними лишь полями контейнер+слот, код в ней 0.
	//   ⚠ Имя эталона `UI_OnInventoryRemove` ВРЁТ: это переходник на `Inventory_SetBagSlot`,
	//     который ЗАПИСЫВАЕТ, а не удаляет (проверено по телу функции).
	//   Возвращает число реально изменённых ячеек (для журнала подсистемы окна).
	//   ★ 31.10.2026: та же раскладка и те же ветки у 6552 «перенос улучшения камнем» (`inventory_parseItemUpdatePacket
	//   @0xB65780`, item_inventory.c:4874-4908 — сверено построчно), поэтому точка общая; `Tag` — подпись журнала.
	int32 ApplyItemOptionTransRecords(const TArray<uint8>& Body119, const TCHAR* Tag = TEXT("[смена параметров] 6554"));

	// ★★★ 5434 (184 б) — РЕЗУЛЬТАТ ПЕРЕДАЧИ ПАРАМЕТРОВ (окно `EXTRACT_WIN` 16000,
	//   `Recv_ExtractShop@0xC92AD0`, network.c:15365). Эталон работает ТОЛЬКО с контейнером 1
	//   (`(char*)&g_InventoryBag + 57*слот`) и делает это В СТРОГОМ ПОРЯДКЕ:
	//     1) ОЧИСТИТЬ слот записи @1  — предмет-ИСТОЧНИК сервер уже удалил (он «сгорел»);
	//     2) ПОЛОЖИТЬ запись @115 в её слот — СВИТОК-бустер после списания (стопка кончилась ⇒
	//        сервер шлёт запись с кодом 0, и она ложится как пустая — так свиток и исчезает);
	//     3) ОЧИСТИТЬ и ПОЛОЖИТЬ запись @58 — ПРИЁМНИК с новыми параметрами.
	//   ⚠ Порядок важен: если приёмник занимал слот, который до этого писал свиток, побеждает
	//   приёмник — ровно как в эталоне.
	//   `bSuccess == false` (код 10) ⇒ эталон применяет ТОЛЬКО шаги 1 и 2 (приёмник не менялся).
	//   Золото и вес двигает сеть (`SetSelfWeight`) и вызывающая подсистема; здесь только записи.
	//   Возвращает число реально изменённых ячеек (для журнала подсистемы окна).
	int32 ApplyExtractShopRecords(const TArray<uint8>& Body184, bool bSuccess);

	// ═══════ ★★★ РЕЗУЛЬТАТ СКЛАДА — ОДНА ТОЧКА ПРИМЕНЕНИЯ В МОДЕЛЬ (окно 4000) ═══════════════════
	// ПОЧЕМУ ЗДЕСЬ, А НЕ В ПОДСИСТЕМЕ СКЛАДА: контейнеры — собственность инвентаря, и в эталоне
	//   приёмники склада тоже пишут ПРЯМО в `g_InventoryBag` / `g_InventoryArray2` и в массив
	//   склада (`inventorySwapSlots@0xE8A820`, `inventorySyncSlotsFromPacket@0xE8ADE0`).
	//   Второй копии модели заводить нельзя (закон `port-shared-points`).
	// ⚠ Звать ТОЛЬКО из `Tick` владельца окна, не из колбэка пакета (закон `no-reentrant-net-callbacks`).
	//
	// Раскладка 11-байтового item-ref (одна и та же у обоих пакетов):
	//   `+0 i16` слот · `+2 i16` второй индекс · `+4 u8` контейнер · `+5 i32` количество ·
	//   `+9 i16` вес стека. Слот < 0 ⇒ запись пустая, её пропускают.

	/** ★★★ 5009 (113 б), ветка успеха — ПОЛОЖИТЬ НА СКЛАД (`inventorySwapSlots@0xE8A820`,
	 *  item_inventory.c:58449). Тело: `@0` результат, `@1…55` 5 ссылок ИНВЕНТАРЯ,
	 *  `@56…110` 5 ссылок СКЛАДА (контейнер 5), `@111 i16` новый вес.
	 *  Эталон делает ДВА прохода и ИМЕННО В ЭТОМ ПОРЯДКЕ:
	 *    1) по ссылкам СКЛАДА: ячейка склада `storeRef.slot` (сквозной индекс 0…269) получает
	 *       ПОЛНУЮ 57-байтовую копию записи инвентаря `invRef.slot` из контейнера `invRef.container`,
	 *       после чего у копии перебиваются поля +5/+7/+4/+9/+14 значениями из `storeRef`;
	 *    2) по ссылкам ИНВЕНТАРЯ: если `invRef.amount >= 0` — записать это количество и вес
	 *       в ячейку контейнера; если `< 0` — ячейка ОЧИЩАЕТСЯ (предмет ушёл на склад целиком).
	 *  ⚠ Порядок важен: первый проход читает запись инвентаря ДО того, как второй её меняет.
	 *  Возвращает число реально изменённых ячеек (для журнала подсистемы склада). */
	int32 ApplyWarehouseKeepResult5009(const TArray<uint8>& Body113);

	/** ★★★ 5011 (117 б), ветка успеха — ЗАБРАТЬ СО СКЛАДА (`inventorySyncSlotsFromPacket@0xE8ADE0`,
	 *  item_inventory.c:58558). Тело: `@0` результат, `@1…55` 5 ссылок ИНВЕНТАРЯ,
	 *  `@56…110` 5 ссылок СКЛАДА, `@111 i32` НОВАЯ СУММА ЗОЛОТА (комиссия уже списана),
	 *  `@115 i16` вес. Два прохода:
	 *    1) ячейка контейнера `invRef.container`/`invRef.slot` получает копию записи склада
	 *       `storeRef.slot`, затем поля +5/+4/+9/+14 из `invRef`;
	 *    2) ячейка склада: `storeRef.amount == −1` ⇒ ОЧИСТИТЬ, иначе записать количество и вес.
	 *  Деньги ставятся ВСЕГДА (эталон: `uiSetHonorPointsDisplay(g_pGameClient, a4)` — имя врёт,
	 *  это золото). Возвращает число реально изменённых ячеек. */
	int32 ApplyWarehouseTakeResult5011(const TArray<uint8>& Body117);

	// Точка подключения тултипа/окна характеристик (следующий заход).
	void SetTooltipBridge(const FAsdaItemTooltipBridge& In) { Tooltip = In; }

	// ═══════════════════ ДИАГНОСТИКА ═══════════════════
	FString DumpControls() const;    // КАЖДЫЙ разобранный контрол (закон full-structural-analysis)
	// ★ Состояние ШТОРОК ОТКАТА на ячейках сумки и слотах надетого (`asda.Cooldown.Dump`).
	FString DumpCooldownCurtains() const;
	FString DumpModel() const;       // содержимое всех контейнеров
	// ★ Текущее выделение (ячейка сетки + надетый слот) и содержимое этих ячеек — `asda.Inven.SelDump`.
	FString DumpSelection() const;
	// ★ Выбрать ячейку кодом — тот же эффект, что клик (событие 0xB эталона): курсор 409/экип + инфо-панель.
	//   Нужна для PIE-проверок: без входа в мир опросчик мыши не тикает (`asda.Inven.Select`).
	void DebugSelectCell(int32 ContainerId, int32 CellIndex);
	// ★ Прокрутить очередь пришедших пакетов вручную (`asda.Inven.Pump`). В игре её крутит `Tick`,
	//   но у него гейт «мы в мире», а PIE без сервера в мир не входит.
	void DebugPumpPendingPackets() { DrainPendingPackets(); }
	// Эмуляция прихода батча содержимого (сервер не нужен): Container 1/2/5, N записей по 57 б.
	void SimulateBatch(int32 Container, const TArray<uint8>& RawRecords);
	// Эмуляция «положить предмет в ячейку» без сервера (только модель + перерисовка).
	bool DebugPutItem(int32 Container, int32 CellIndex, int32 ItemCode, int32 Amount);
	// То же + доп-опции гнёзд записи (+29/+31 гнездо 0, +33/+35 гнездо 1, i16) — проверка окна 50300 без сервера.
	bool DebugPutItemWithOptions(int32 Container, int32 CellIndex, int32 ItemCode, int32 Opt0, int32 Val0, int32 Opt1, int32 Val1);
	// ★ Волна 11 заточки: иконка надетого слота `EQUIP_SET<Slot>` — цель НАСТОЯЩЕГО симклика в самотесте
	//   (двойной щелчок «снять вещь» во время блокировки `uiCheckRecentActivity`). nullptr — слота/иконки нет.
	UWidget* DebugGetEquipSlotIcon(int32 Slot) const;
	// ★ 10.10.2026 (самотест `asda.UI.SelfTestModalLock`, ПКМ под модалкой): иконка ячейки сетки `INVEN_SET<C>` (0..29, видимая
	//   страница) — цель НАСТОЯЩЕГО симклика; ячейка модели = C + 6·ряд прокрутки. nullptr — ячейки нет.
	UWidget* DebugGetGridIcon(int32 C) const;
	// ★ 26.10.2026 (самотест `asda.SelfTest.DragDelete`): кнопка-корзина 405 — цель НАСТОЯЩЕГО симклика/броска. nullptr — нет.
	UWidget* DebugGetTrashButton() const;
	// ★ Волна 11 заточки: сколько раз двойной клик по надетому дошёл до запроса «снять» (`UnequipItem`)
	//   (счётчик растёт ДО сети — без сервера пакет не уйдёт, а сам путь клика проверить нужно).
	int32 DebugGetUnequipRequests() const { return DebugUnequipRequests; }
	// ★ Волна 11 заточки: один кадр БОЕВОГО разбора мыши окна (`TickPointer`) в обход гейта «мы в мире» `Tick`.
	//   Офлайн-прогон (-game без сервера) павна не имеет, и `Tick` выходит до опроса — без этого путь клика по
	//   ячейке/слоту самотестом не проверить. Сам разбор не меняется: тот же опрос Slate, хит-тест и ветки.
	void DebugTickPointer() { TickPointer(); }

	// ★★★ ОЧИСТИТЬ ЯЧЕЙКУ (модель + перерисовка + сброс выделения + пересборка хотбара).
	//   Эталонное `memset(&g_InventoryBag + 57 * слот, 0, 0x39)` из приёмников пакетов.
	//   ПУБЛИЧНА С 21.09.2026 (волна 3 питомцев): её зовёт окно инкубатора 42000, потому что
	//   `CPetWin_OnResult@0xDC2CA0` (`npc_mob_pet.c:5098-5110`) чистит ячейки катализатора, яйца
	//   и бустера ИЗ САМОГО ОКНА, а не из сетевого слоя. Второй копии очистки не заводим
	//   (закон `port-shared-points`): сброс выделения и обновление хотбара живут внутри.
	void ClearCell(int32 ContainerId, int32 CellIndex);

	// ★★★ ПАКЕТНОЕ ИЗМЕНЕНИЕ НЕСКОЛЬКИХ ЯЧЕЙОК ЗА ОДИН ПРИЁМ ПАКЕТА.
	//   ЗАЧЕМ. Каждая `ClearCell`/`PutRecord` тянет `NotifyCell` → `RefreshHotbarCache`, то есть
	//   ПОЛНУЮ пересборку кэша панели быстрого доступа. Эталон в приёмниках, которые трогают
	//   несколько ячеек сразу, делает перерисовку ОДИН раз в конце — например `CPetWin_OnResult`
	//   чистит три ячейки и лишь потом зовёт `CInventoryUI_ResetAndRefresh`
	//   (`npc_mob_pet.c:5098-5111` и `:5138-5151`). Внутри самой подсистемы это уже сделано
	//   (`TGuardValue<bool> Guard(bSuppressHotbarRefresh, true)` в приёме батча), но снаружи
	//   доступа к флагу не было, и окна платили тремя пересборками вместо одной.
	//   Парность обязательна: `End` пересобирает кэш один раз. Вложенность не поддерживается —
	//   счётчика у эталонного флага нет, второго заводить не будем.
	void BeginCellBatch();
	void EndCellBatch();

	// ★ 09.10.2026 (автолопата): таймер цикла — только чтение (самотест, диагностика). Разбор полей — у `FAsdaAutoDigTimer`.
	const FAsdaAutoDigTimer& GetAutoDigTimer() const { return AutoDig; }
	// ★ Ключ `asda.Shovel.AutoLoop` (1 — цикл автолопаты как в оригинале; 0 — прежний порт: цикла нет).
	static bool IsAutoLoopEnabled();
	// ★ Ключ `asda.Shovel.AutoLoopFix` (1 — исправления дефектов оригинала в цикле [ПОРТ]; 0 — поведение оригинала).
	static bool IsAutoLoopFixEnabled();

private:
	friend struct FAsdaFishAnimSelfTest;   // ★ 09.10.2026: самотест цикла автолопаты (раздел F) — `bActionPending`, сброс цикла
	friend struct FAsdaDedupW1ASelfTest;   // ★ В1 дедупа №2: прежняя сборка слотов 6096 в самотесте читает `Container()`
	friend struct FAsdaQuestJobChangeSelfTest;   // ★ 19.10.2026: самотест наград 5042/5052 возвращает `GoldDisplay` в исходное (−1)
	friend struct FAsdaPlusPackOnceSelfTest;   // ★ 08.11.2026: самотест отказа повторного зелья уровня — сброс `bActionPending`
	friend struct FAsdaInvX2SelfTest;            // ★ 29.10.2026: самотест «инвентарь ×2» — сетка `GridUi`, полоса, `ScrollRow`

	// ── ★★★ ЦИКЛ АВТОЛОПАТЫ (09.10.2026, ревизия six_fixes п. 6) ──
	FAsdaAutoDigTimer AutoDig;   // эталонный `g_pActionTimer`
	// `timerStart(g_pActionTimer, DurMs, ItemCode)` (item_inventory.c:39457): активен, отсчёт заново, уровень 0, повторы 0.
	void AutoDigStart(int32 DurMs, int32 ItemCode);
	// `resetActiveAndCounter` (+6 и +7 → 0; режим/слот/контейнер/уровень НЕ трогает — как эталон) + журнал «AUTOSABZIL : END …».
	void AutoDigReset(const TCHAR* Why);
	// Кадр цикла: покадровый сброс по состоянию своего (entity_char.c:8064-8074) + `updateTimeoutLevel` + подбор + повтор
	//   (game_main.c:2271-2307). Зовётся из `Tick` после разбора очереди пакетов.
	void TickAutoDig();
	// Порт `inventory_SendUseItemRequest@0xD13EA0` (item_inventory.c:40964-41120) — повтор применения инструмента по слоту.
	void SendAutoDigRepeat();
	// Гейты лопат 2040 / 9002 / 9001 при применении (общие для `UseConsumable` и повтора): 0 — пройдены, иначе loc нотиса.
	//   Нотис НЕ выводит — это делает вызывающий (у каждого свой журнал и своя реакция).
	int32 ShovelUseGate() const;

	// ── Жизненный цикл окна ──
	bool EnsureWidget();
	void ResetWidget();
	void SetupControls();
	void ApplyWindowVisibility();
	void RegisterZone();
	void UnregisterZone();
	bool GetWindowRect(FVector2D& OutMin, FVector2D& OutMax) const;
	bool OnWheel(int32 Notches);

	// ── ПОЛОСА ПРОКРУТКИ (CScrollBar-ребёнок 494 из скина `Nomal_Scr`; разбор — в шапке) ──
	//   Виджетов полосы в WBP нет и быть не может: движок оригинала создаёт её в рантайме по ключу
	//   `ScrollBarFileNameVert` СКИНА, а импортёр разбирает только `gamemain_jui.ini`. Поэтому
	//   четыре плана собираются здесь — из тех же данных (UV скина + атлас `n_01`), а не «на глаз».
	void BuildScrollBar();                      // создать 4 плана один раз (после SetupControls)
	void LayoutScrollBar();                     // геометрия + видимость + бегунок (эталонные формулы)
	void TickScrollBar(bool bLmb, bool bLmbDownEdge);   // стрелки / дорожка / перетаскивание бегунка
	bool IsScrollBarShown() const;              // полоса видна ⇔ видно колёсное окно 494
	// Курсор над любой частью полосы — чтобы клик по ней не уходил в ячейки/мир.
	bool IsCursorOverScrollBar() const;

	// ── Сеть ──
	// ⚠⚠ ВСЕ сетевые колбэки ниже ТОЛЬКО КЛАДУТ ТЕЛО В ОЧЕРЕДЬ (закон `no-reentrant-net-callbacks`):
	//   применение модели и любое касание виджетов — в `Tick`, иначе реентрантность рвёт DrainFrames.
	void OnBatchReceived(int32 Container, const TArray<uint8>& Raw);
	void OnBatchEnd(int32 Container);
	void OnEquipFullReceived(const TArray<uint8>& Raw11x57);
	void OnMoveResult(const TArray<uint8>& Body24);
	void OnSlotUpdate(const TArray<uint8>& Body14);
	void OnSlotRemoved(const TArray<uint8>& Body59);
	void OnUseResult(const TArray<uint8>& Body24);
	void OnDumpResult(const TArray<uint8>& Body18);
	void OnLootPicked(uint8 Result, int32 PickerUserNum, const TArray<uint8>& Body62, int32 NewWeight);
	void ApplyPendingBatches();                                        // выполняется в Tick
	// Применение одиночных пакетов из очереди (5001 / 5003 / 4999 / 5029 / 5007 / 5013).
	void ApplyMoveResult(const TArray<uint8>& Body24);
	// ★★★ СЛОТ ЭКИПИРОВКИ (контейнер 3) ИЗМЕНИЛСЯ: чат-нотис + СМЕНА МОДЕЛИ персонажа.
	//   Зовётся ИЗ `ApplyMoveResult` — ровно там же, где это делает эталон (`itemMoveApply@0xD607C0`
	//   → `inventoryApplySlotItem@0xD60BA0` case 3 при надевании и `slotItemDrop@0xD60EB0` case 3
	//   при снятии). Сервер СЕБЕ 4021/4023 не рассылает (`SendPacket_ClippingSectorInOtherUser
	//   @0x41EA80` пропускает отправителя), поэтому другого входа для своей внешности нет.
	//   `ItemCode` — предмет, который надели (bPutOn) либо сняли; нотисы 20134 / 20135 + 2033 / 7002.
	void OnSelfEquipSlotChanged(int32 EquipSlot, int32 ItemCode, bool bPutOn);
	void ApplySlotUpdate(const TArray<uint8>& Body14);
	void ApplySlotRemoved(const TArray<uint8>& Body59);
	void ApplyUseResult(const TArray<uint8>& Body24);
	// Таблица нотисов по коду результата 5029 (0…14) — `switch` эталона `OnRecv_UseItemResult@0xD0B1E0`.
	void ApplyUseRefusal(int32 Result, int32 ItemCode);

	// ★ Код результата 11 канала 5029 — КОПАНИЕ. Порт `Item_UseShovelingTool@0xD0BFE0`.
	void ApplyShovelingTool(int32 ItemCode);
	void ApplyDumpResult(const TArray<uint8>& Body18);
	void ApplyLootRecord(const TArray<uint8>& Rec57);
	bool SendMovePacket(const FAsdaItemInstance& Src, int32 DstSlot, int32 DstType, const TCHAR* Why);

	// ── ★★★ ВОЛНА 2: колбэки сети (только кладут тело в очередь) и применение в Tick ──
	void OnPackageResult(const TArray<uint8>& Body300);   // 6097
	void OnGachaResult(const TArray<uint8>& Body68);      // 5469
	void OnFunctionResult(const TArray<uint8>& Body68);   // 5451
	void OnItemRepaired(const TArray<uint8>& Entry57);    // 5457
	// ★ 08.10.2026 (рыбалка В1): 6570 — износ предмета (`netApplySlotStateFlag@0xEDB150`); колбэк только в очередь.
	void OnSlotDurability6570(const TArray<uint8>& Body4);
	void ApplyPackageResult(const TArray<uint8>& Body300);
	void ApplyGachaResult(const TArray<uint8>& Body68);
	void ApplyFunctionResult(const TArray<uint8>& Body68);
	void ApplyRepairResult(const TArray<uint8>& Entry57);

	// ★★★ 5456 — РАСШИРЕНИЕ СКЛАДА (предмет `special_itemlist.SubType == 34`, коды 30/467/541).
	//   Эталон `recvWarehouseChangeBagPage@0xF34BC0` → `warehouseChangeBagPage@0xF26820`
	//   → `inventoryChangeBagPage@0xE8E440` (`item_inventory.c:59130`). Сам разбор пакета и
	//   правила приёма ёмкости живут в `UAsdaNetSubsystem` (общая точка, там же `SelfStoreSlotsMax`);
	//   ЗДЕСЬ — только то, что эталон делает В ОКНЕ: строка в чат и ОДНОКНОПОЧНАЯ МОДАЛКА
	//   `ONEBUT_MSGBOX` 1500 через `UAsdaWindowSubsystem::ShowGameMessage`.
	//   Разбор дефекта («предмет исчез, окна нет») — `Reports/inventory_use_fix.md`.
	void OnBagExpand(int32 NewMaxSlots, bool bAccepted, int32 NoticeLocId, bool bAlsoChat);
	void ApplyBagExpand(const TArray<uint8>& Packed8);


	// ── Модель ──
	TArray<FAsdaItemInstance>* Container(int32 ContainerId);
	const TArray<FAsdaItemInstance>* Container(int32 ContainerId) const;
	void PutRecord(const uint8* Rec57, int32 ForcedContainer);   // положить запись по её собственным полям
	void NotifyCell(int32 ContainerId, int32 CellIndex);
	// ★★★ Сброс выделения, когда ВЫБРАННАЯ ячейка опустела. Эталон делает это в обеих ветках
	//   «клик по пустой ячейке» (`CInventoryUI_OnSlotClick`, item_inventory.c:36343-36352 для
	//   контейнера 1 и 36355-36361 для контейнера 2) и безусловно — при смене надетого
	//   (`Recv_ItemEquipStat@0xD0F350`, network.c:17341 `SetVisible(m_pItemTraySel, 0)`).
	//   Зовётся из `NotifyCell` — единственной точки, через которую проходят ВСЕ изменения ячеек.
	void ClearSelectionIfCellEmptied(int32 ContainerId, int32 CellIndex);
	// Разбор одиночных пакетов из очереди (вынесен из `Tick`, см. `DebugPumpPendingPackets`).
	void DrainPendingPackets();
	// Пересобрать кэш хотбара (он хранит «контейнер + ячейка», а не itemId).
	void RefreshHotbarCache();

	// ── Рендер ──
	void RefreshGrid();
	void RefreshEquip();
	void RefreshMoney();
	void RefreshTabVisual();
	void UpdateCellVisual(int32 Cell);
	void UpdateEquipVisual(int32 Slot);
	// ★★★ ШТОРКИ ОТКАТА НА ЯЧЕЙКАХ СУМКИ И СЛОТАХ НАДЕТОГО — наше отступление от оригинала
	//   (решение юзера 2026-08-14; в оригинале шторка только на панели быстрого доступа,
	//   доказательство — `Reports/item_cooldown_premium.md` §2). Зовётся ПОКАДРОВО из `Tick`
	//   при открытом окне: откат — величина непрерывная, а `RefreshGrid` дёргается по событию.
	//   Данные берутся ТОЛЬКО из реестра панели (`UAsdaHotbarSubsystem::QueryItemCooldown`),
	//   своего счётчика инвентарь не заводит (закон `port-shared-points`).
	void TickCooldownCurtains();
	void ApplyStackTextStyle(UTextBlock* Tb) const;
	// ★ Текст ПОД ИКОНКОЙ по правилам эталона (у вкладок они РАЗНЫЕ, см. .cpp).
	//   OutColorArgb: 0 = цвет не менять (эталонный `CControl_SetField923(-1)`).
	FString BuildCellText(int32 ContainerId, const FAsdaItemInstance& I, uint32& OutColorArgb) const;
	// ★ п.10: посадить курсор выбора (409/612) ровно на ячейку, переведя координаты между канвасами.
	void PlaceSelOnCell(UWidget* Sel, const UWidget* Icon,
	                    const FVector2D& CachedIconRoot, const FVector2D& CachedSelParentRoot);
	// ★ Д6: держать подписи вкладок на прямоугольниках своих кнопок-опций (окно перетаскивается).
	void SyncTabCaptions();
	// ★ Порт `String_FormatThousands@0xEC00D0`: разделитель — запятая каждые три разряда.
	static FString FormatThousands(int32 Value);

	// ── Ввод (события 5/7/8/9/0xA/0xB/0xD/0xE эталонного `CGameWindow_OnCommand`) ──
	void TickPointer();
	bool IsCursorOver(const UWidget* W) const;
	// Что под курсором: OutContainer/OutCell. false — ничего.
	bool HitTest(int32& OutContainer, int32& OutCell) const;
	// ★ Гейты захвата ОДНОЙ строкой — ими пользуется и сам `BeginDrag`, и «взвод» в `TickPointer`.
	//   Взводить надо ровно те ячейки, на которых прежний код (захват прямо из `Mouse.bLmbDown`)
	//   поднимал `bDragActive`: иначе ветка отпускания сменила бы условие срабатывания.
	bool CanBeginDrag(int32 Container, int32 Cell) const;
	// ★ Отпускание ЛКМ над той же ячейкой = клик (одиночный — выделение, второй подряд —
	//   применение, эталонное сообщение 0xD). Вызывается ИЗ ДВУХ веток `TickPointer`: «тащили и
	//   вернулись» и «порог не пройден, значит не тащили» — поэтому вынесено в метод.
	void HandleCellClickRelease(int32 SrcCont, int32 SrcCell);
	void BeginDrag(int32 Container, int32 Cell);
	void EndDrag();
	void CancelDragVisual();
	void UpdateDragGhost();
	// Бросок на цель вне инвентаря (хотбар). true — цель нашлась и обработана.
	//   ⚠⚠ ИСТОЧНИК ПЕРЕДАЁТСЯ ПАРАМЕТРАМИ, а не читается из `DragContainer`/`DragCell`: `EndDrag`
	//   гасит визуал драга (`CancelDragVisual`) ДО разбора цели, а тот сбрасывает оба поля в 0/−1.
	//   Прежняя бес­параметрическая версия читала уже обнулённые поля и всегда возвращала false —
	//   именно из-за этого бросок из сумки в панель быстрого доступа не работал вовсе.
	bool TryDropToHotbar(int32 SrcContainer, int32 SrcCell);
	// ★ 26.10.2026: бросок ВНЕ ВСЕХ ОКОН (в мир) — ветка `a3 == nullptr` эталона `inventoryWin_OnUiCommand@0xD17A80`
	//   (item_inventory.c:41656-41690): сумка → `invSlotCheckItemValid` (0 ⇒ окно 2004, 2 ⇒ вопрос 2059), расходники →
	//   `invSlotShowTooltipOrPopup` (золото — ничего; стек — окно количества, контекст 1; иначе вопрос 2059). На землю
	//   оригинал НЕ выбрасывает (отправителя 5002 нет). true — бросок обработан (вопрос/окно показаны или отказ молча).
	bool DropOutsideWindows(int32 SrcContainer, int32 SrcCell);
	// ★ 26.10.2026: гейты сообщений 5 (захват) и 7 (бросок) эталона `CGameWindow_OnCommand` (ui_core.c:47768-47800):
	//   открыт барабан бустера (`CObject_GetField254(g_pInventoryWnd)`) ⇒ 13106 в чат; активен свиток вызова хранилища
	//   (`CObject_GetField5581(g_pWarehouseWnd)`) ⇒ 10309 в чат. true — действие запрещено (нотис уже выведен).
	bool DragGateRefuses(const TCHAR* Where) const;
	void ShowCellTooltip(int32 Container, int32 Cell);
	void HideCellTooltip();

	// ── Автосортировка ──
	// Категория предмета по таблице порядка эталона; −1 = не подходит ни под одну.
	bool AutoArrangeMatches(int32 Category, int32 ItemCode, int32 SlotIdx, int32 WriteCursor) const;
	// Предикат КАТЕГОРИИ 7 эталона (`sub_FE1040@0xFE1040`): код входит в шестёрку ингредиентов
	//   какого-нибудь рецепта `Data/NPC/making_recipe.BIN`. Подробности — у определения в .cpp.
	//   ★ В Asda Global это же правило: `0x0078EA50 → 0x00A27050` (VA Global) линейно перебирает
	//   записи менеджера рецептов 0x01B8B758 (шаг 0x40, шесть кодов сырья по +8) и сравнивает с
	//   кодом предмета. Раскладка записи в Global иная, чем в нашем `making_recipe.BIN`, поэтому
	//   набор кодов берётся из НАШЕГО файла — правило одно, данные свои.
	//   ⚠ Не `static`: данные читаются из таблицы `DT_making_recipe` через `UAsdaItemTableSubsystem`,
	//   а тот живёт в `UGameInstance` (раньше метод был статическим, потому что список кодов лежал
	//   константой прямо в теле).
	bool IsCraftMaterialCode(int32 ItemCode) const;
	// Множество кодов сырья ВСЕХ рецептов (объединение полей Mat0..Mat5 таблицы `making_recipe`).
	//   Строится ЛЕНИВО при первом обращении и живёт до конца сессии: таблица статична, а перебор
	//   249 записей на каждый предмет автосортировки был бы лишним.
	const TSet<int32>& GetCraftMaterialCodes() const;
	// Кэш к `GetCraftMaterialCodes` (mutable — предикат категории зовётся из `const`-методов).
	mutable TSet<int32> CraftMaterialCodes;
	// Кэш уже построен? Ставится и при ОТСУТСТВИИ таблицы — второй попытки не будет.
	mutable bool bCraftMaterialsBuilt = false;
	void TickAutoArrange();

	// ── Главное меню: кнопка 73402 открывает/закрывает окно, подсветку ставит САМО ОКНО ──
	//   Эталон `uiOpenInventoryWindow` в самом конце зовёт `uiPanel_SetTabVisible(g_pSystemMainWnd, 1, 1)`,
	//   т.е. «чекнутость» кнопки меню — следствие открытия окна, а не наоборот.
	//   ★ В порту это выражено ЕДИНЫМ механизмом: окно один раз зовёт
	//   `UAsdaSystemMenuSubsystem::RegisterWindow(kMenuButtonIdNum)` (см. `EnsureMenuBinding`),
	//   и дальше подсветка выводится из фактической видимости `INVEN_WIN` одним местом.
	//   Своего `SetMenuTabChecked` у инвентаря БОЛЬШЕ НЕТ — второго пути не заводим.
	UFUNCTION() void OnMenuCommand(int32 ButtonIdNum, int32 TargetWindowId, bool bOpen);
	// ★ Клавиша «I» — эталон `input.c:1729` / `input.c:4285` → `uiToggleInventoryWindow(a1, 1)`.
	//   В раскладке порта это команда 33 «INTERFACE INVENTORY» (`Shortcutkey.ini`).
	UFUNCTION() void OnGameCommand(int32 Cmd);
	void EnsureMenuBinding();
	// Номер команды раскладки, открывающей инвентарь (таблица `GActionLabels`, индекс 33).
	static constexpr int32 kCmdInterfaceInventory = 33;

	// ── Мост к хотбару ──
	void BindHotbarBridge();
	// ★★★ ЯВНАЯ ТАБЛИЦА ТРАНСЛЯЦИИ: `SubType` записи хотбара → contId инвентаря.
	//   Числа СОВПАДАЮТ (1→1, 2→2, 3→3) — это доказано `hotbarRebuildSlots@0xDEC380`
	//   (ui_core.c:66949-66964): SubType 1 читает `g_InventoryBag` (контейнер 1), 2 —
	//   `g_InventoryArray2` (контейнер 2), 3 — `LocalPlayer->gap_50A[57·i+292]` (контейнер 3).
	//   ⚠ НО ИМЕНА в `EAsdaHotbarContainer` ВРУТ: там `Equipped = 2` и `EquipSet = 3`, хотя
	//   надетое — это 3, а 2 — расходники. Поэтому трансляция сделана ТАБЛИЦЕЙ, а не «как есть»:
	//   если кто-то поправит имена в хотбаре, здесь ничего не поедет.
	static int32 HotbarSubTypeToContainer(int32 HotbarSubType);
	// ★★★ ОБРАТНАЯ ТАБЛИЦА: contId инвентаря → `SubType` записи слота хотбара. 0 = контейнер в
	//   панель быстрого доступа не адресуется вовсе (склад 5 и всё неизвестное). Нужна отдельной
	//   функцией по той же причине, что и прямая: числа совпадают, но это ФАКТ ЭТАЛОНА, а не
	//   тождество — присваивание `SubType = Container` запрещено.
	static int32 ContainerToHotbarSubType(int32 ContainerId);
	// ★ Сырое i32-поле шаблона `item_list` по СМЕЩЕНИЮ В ЗАПИСИ (264 б) — колонки для него нет,
	//   но `DataHex` хранит запись 1:1. Нужно гейту «предмет нельзя класть в панель» (+0xB4).
	//   Возврат −1, если запись/таблица недоступна.
	static int32 ItemRawI32(const class UAsdaItemTableSubsystem* IT, int32 ItemCode, int32 Offset);

	// ── Помощники ──
	static void SetVis(UWidget* W, bool bVisible);
	UWidget* FindNode(const TCHAR* Exact, const TCHAR* Needle) const;
	void Notice(int32 LocId, const TCHAR* Fallback = nullptr) const;   // [ПОРТ] 09.10.2026 (мультиязычность, этап 3): nullptr → «[#id]» при пропуске строки

	/** ★★★ ВОЛНА 8 (пункт 5 сквозной ревизии питомцев): СТРОКА В ПЛАШКУ, А НЕ В ЧАТ.
	 *  Эталонный `switch` по коду 5029 использует ДВА РАЗНЫХ канала (item_inventory.c:39035-39218):
	 *    • коды 0 и 3…10 — `CChat_AddTokenizedLines(g_pChatWnd, …, −11514, 10000)` = строка чата;
	 *    • коды 12/13/14 — `CControl_SetTooltipText(g_pChatWnd, …, −11514, 10000)` (:39205-39217).
	 *  Имя `CControl_SetTooltipText@0xBA9A30` — МИСНЕЙМ: это строка в `WARMESSAGE_WIN` (221),
	 *  крупная всплывающая плашка поверх экрана (ui_core.c:12069-12081); в порту это канал
	 *  `EAsdaMsgTarget::WarWindow` — тот же, на котором сидит `UAsdaWarehouseSubsystem::NoticePlank`.
	 *  До волны 8 порт сводил оба канала в один, и два ПЕТ-НОТИСА — 106000 «Нет активного
	 *  питомца» и 106001 «Твой питомец стал взрослым» — уходили строкой в чат вместо плашки.
	 *  Цвет и TTL — эталонные (−11514 = 0xFFD306 жёлтый, 10000 мс). */
	void NoticePlank(int32 LocId, const TCHAR* Fallback = nullptr) const;   // [ПОРТ] 09.10.2026 (мультиязычность, этап 3): nullptr → «[#id]»
	FString LocText(int32 LocId, const TCHAR* Fallback = nullptr) const;     // [ПОРТ] 09.10.2026 (мультиязычность, этап 3): nullptr → «[#id]», TEXT("") → пусто

	// ══════════════════ ДАННЫЕ: МОДЕЛЬ ══════════════════
	UPROPERTY(Transient) TArray<FAsdaItemInstance> Shell;   // контейнер 1, 30
	UPROPERTY(Transient) TArray<FAsdaItemInstance> Spend;   // контейнер 2, 60
	UPROPERTY(Transient) TArray<FAsdaItemInstance> Equip;   // контейнер 3, 20 (11 рабочих + 9 аватар)
	UPROPERTY(Transient) TArray<FAsdaItemInstance> Store;   // контейнер 5, 270

	// ★★★ Битовая маска «контейнер получен целиком» (бит N = контейнер N). См. `IsContainerReceived`.
	//   Сбрасывается при разрыве соединения/смене персонажа — вместе с содержимым контейнеров.
	int32 ContainerReceivedMask = 0;
	// Были ли мы в мире на прошлом тике — для сброса `ContainerReceivedMask` при новом входе.
	bool bWasInWorld = false;

	// ══════════════════ ДАННЫЕ: ОКНО ══════════════════
	UPROPERTY(Transient) TObjectPtr<UUserWidget> InvenWidget;
	UPROPERTY(Transient) TArray<FAsdaInvCellUi>  GridUi;    // 30 ячеек INVEN_SET0..29
	UPROPERTY(Transient) TArray<FAsdaInvCellUi>  EquipUi;   // 11 слотов EQUIP_SET0..10

	UPROPERTY(Transient) TObjectPtr<UAsdaAnimatedPlane> GridSel = nullptr;   // INVEN_ITEMTRAYSEL 409
	UPROPERTY(Transient) TObjectPtr<UAsdaAnimatedPlane> EquipSel = nullptr;  // EQUIP_TRAYICONSEL 612
	UPROPERTY(Transient) TObjectPtr<UTextBlock> MoneyText = nullptr;         // INVEN_MONEYTXT 407
	UPROPERTY(Transient) TObjectPtr<UTextBlock> TabText = nullptr;           // INVEN_CHOICEBUT_Text (вкладка 0)
	// ★ Д6: у RadioButton 402 ДВЕ подписи (`CListBox_SetItemString(402, 0/1, …)`), а импортёр сделал
	//   ОДИН TextBlock на всю полосу 218 px — надпись вставала между вкладками. Второй TextBlock
	//   создаётся в рантайме и позиционируется по прямоугольнику своей кнопки-опции.
	UPROPERTY(Transient) TObjectPtr<UTextBlock> TabText1 = nullptr;          // INVEN_CHOICEBUT_Text2 (из ассета)
	// Базовые цвета подписей вкладок: снимает ИЗ АССЕТА общая точка `AsdaUi::ApplyTabCaptionState`.
	TArray<FLinearColor> TabIdleColors;
	UPROPERTY(Transient) TObjectPtr<UButton>    TabOpt0 = nullptr;           // вкладка «Расходники»
	UPROPERTY(Transient) TObjectPtr<UButton>    TabOpt1 = nullptr;           // вкладка «Снаряжение»
	UPROPERTY(Transient) TObjectPtr<UImage>     TabPlane0 = nullptr;         // INVEN_CHOICEBUT_49_0
	UPROPERTY(Transient) TObjectPtr<UImage>     TabPlane1 = nullptr;         // INVEN_CHOICEBUT_49_1
	UPROPERTY(Transient) TObjectPtr<UButton>    BtnClose = nullptr;          // 403
	UPROPERTY(Transient) TObjectPtr<UButton>    BtnExpand = nullptr;         // 408
	UPROPERTY(Transient) TObjectPtr<UButton>    BtnCollapse = nullptr;       // 412
	UPROPERTY(Transient) TObjectPtr<UButton>    BtnTrash = nullptr;          // 405
	UPROPERTY(Transient) TObjectPtr<UWidget>    RightPanel = nullptr;        // INVEN_RIGHISIDEBACK_grp 413
	UPROPERTY(Transient) TObjectPtr<UImage>     Gear = nullptr;              // Inven_Gear 450
	UPROPERTY(Transient) TObjectPtr<UAsdaAnimatedPlane> AniGear = nullptr;   // Inven_AniGear 451
	UPROPERTY(Transient) TObjectPtr<UButton>    BtnAutoArrange = nullptr;    // Inven_auto_array 80400
	UPROPERTY(Transient) TObjectPtr<UTextBlock> AutoArrangeLabel = nullptr;  // его подпись «A»
	UPROPERTY(Transient) TObjectPtr<UWidget>    AutoArrangeBox = nullptr;    // всплывашка 80410
	// 80411/80412 — «Упорядочить инвентарь 1/2» (Global loc 112400/112401), 80413 — «Закрыть»
	//   (Global loc 112402, сортировку НЕ запускает).
	UPROPERTY(Transient) TArray<TObjectPtr<UButton>>    AutoArrangeItems;    // 80411…80413
	UPROPERTY(Transient) TArray<TObjectPtr<UTextBlock>> AutoArrangeItemText;
	// ★ Аудит №3 (B4): призрак перетаскивания — общая точка `FAsdaDragGhost` (тело `UpdateDragGhost`
	//   здесь и в книге навыков совпадало ПОБАЙТОВО, различались только имена полей окна).
	UPROPERTY(Transient) FAsdaDragGhost DragGhost;
	// ── Полоса прокрутки `Nomal_Scr` (4 плана + формулы движка) ──
	//   ★ D3 (аудит `code_dedup_audit_2.md`): всё, что задано СКИНОМ и формулами `uiScrollbar*`,
	//   живёт в общем компоненте `FAsdaScrollBarNomal` (UI/AsdaScrollBarNomal.h) — раньше это был
	//   второй ПОЛНЫЙ экземпляр того же кода (первый — в книге навыков), включая UV-константы.
	UPROPERTY(Transient) FAsdaScrollBarNomal ScrollBar;
	// ★ Служебные контролы со СПЕЦ-ПОДСКАЗКОЙ при наведении (эталон, событие 8):
	//   449 «улучшение снаряжения» и 10008 «полоса прочности» под крупной иконкой предмета.
	UPROPERTY(Transient) TObjectPtr<UWidget>    HintUpgradeBtn = nullptr;    // Inven_ItemUpgrade_Btn 449
	UPROPERTY(Transient) TObjectPtr<UWidget>    HintDuriBar = nullptr;       // INVEN_EQUIICON_BAR 10008
	// ★ Нижний ряд кнопок: 449 / 452 / 453 / 454 / 10082 (см. OnSideButtonClicked выше).
	UPROPERTY(Transient) TObjectPtr<UButton>    BtnItemUpgrade = nullptr;    // 449
	UPROPERTY(Transient) TObjectPtr<UButton>    BtnSmelting = nullptr;       // 452
	UPROPERTY(Transient) TObjectPtr<UButton>    BtnDisassembly = nullptr;    // 453
	UPROPERTY(Transient) TObjectPtr<UButton>    BtnPetCage = nullptr;        // 454
	UPROPERTY(Transient) TObjectPtr<UButton>    BtnPremiumItem = nullptr;    // 10082
	UPROPERTY(Transient) TObjectPtr<UTextBlock> TxtItemUpgrade = nullptr;    // подпись 449   — нотис 98300
	UPROPERTY(Transient) TObjectPtr<UTextBlock> TxtSmelting = nullptr;       // подпись 452   — нотис 15305
	UPROPERTY(Transient) TObjectPtr<UTextBlock> TxtDisassembly = nullptr;    // подпись 453   — нотис 15306
	UPROPERTY(Transient) TObjectPtr<UTextBlock> TxtPremiumItem = nullptr;    // подпись 10082 — нотис 110666
	int32 HintShown = 0;                                                     // какой спец-подсказкой занят тултип
	UPROPERTY(Transient) TArray<TObjectPtr<UAsdaButtonRelay>> Relays;

	// ══════════════════ СОСТОЯНИЕ ══════════════════
	bool  bWindowOpen = false;
	bool  bPanelExpanded = false;
	// Д8 (просьба юзера, в эталоне такого состояния НЕТ): панель свёрнута РУКАМИ (стрелка 412) —
	//   значит авто-раскрытие по клику предмета молчит до смены вкладки / переоткрытия окна.
	bool  bPanelCollapsedByUser = false;
	bool  bControlsReady = false;
	int32 CurrentTab = 0;            // CInventoryWindow+0x204
	int32 ScrollRow[2] = { 0, 0 };   // [0] m_nEquipPage (вкладка «Расходники»), [1] m_nBagPage
	int32 SelCell = -1;              // выбранная ячейка сетки (m_nSelEquipSlot / m_nSelBagSlot)
	int32 SelEquipSlot = -1;
	// ★ Смещение системы координат «канвас курсора выбора» → «канвас ячеек» (см. FAsdaInvCellUi::IconRootPos).
	FVector2D GridSelParentRoot = FVector2D::ZeroVector;
	FVector2D EquipSelParentRoot = FVector2D::ZeroVector;

	// ★★★ ПОРТ `uiSetHonorPointsDisplay@0xB75D40` (`g_pGameClient + 770234`): у оригинала сумма денег
	//   хранится ОТДЕЛЬНЫМ полем, а не только в записи 20551 контейнера 2, и обновляется КАЖДЫМ
	//   пакетом, который её несёт (5003 gold-слот, 5007 @12, 5013 запись+9, батчи 4048).
	//   Без этого поля «золото не менялось при подборе»: запись контейнера сервер не переслал.
	//   −1 = ещё не приходило (показываем сумму из модели).
	int32 GoldDisplay = -1;

	// ★ Флаг ожидания ответа сервера (эталон `g_bInventoryActionPending`): пока true, клиент НЕ
	//   отправляет новых перемещений и НИЧЕГО не применяет локально.
	bool   bActionPending = false;
	double ActionSentAtS = 0.0;
	// ★ Эталонный `g_dwPlayerActionTimer` (`itemUseDispatch@0xA105A0`): один общий отсчёт на
	//   «сесть/слезть» у премиум-подтипа 15 — 200 мс на «слезть», 3000 мс на «сесть».
	//   У подтипа 25 (спутник) троттла в эталоне нет.
	double PremiumActionAtS = 0.0;

	// Драг
	bool  bDragActive = false;
	int32 DragContainer = 0;
	int32 DragCell = -1;

	// ★★★ ВЗВОД ЗАХВАТА — общая точка `FAsdaDragArm` (`AsdaUiHelpers.h`), одна на три окна.
	//   `KeyA` = контейнер, `KeyB` = номер ячейки. Пока взведено, но порог не пройден, отпускание
	//   ЛКМ считается ОБЫЧНЫМ кликом по ячейке (выделение / двойной клик = применить).
	//   Порог — отступление от эталона по требованию юзера: оригинал
	//   (`CControl_OnLButtonDown@0x12DB870`) начинает захват тем же кадром, что и нажатие.
	FAsdaDragArm DragArm;

	// ★★★ ДЕТЕКТОР ДВОЙНОГО КЛИКА ПО ЯЧЕЙКЕ. В эталоне применение предмета висит на СООБЩЕНИИ 0xD,
	//   а оно рождается только при `CObject_GetStateByteAt(mgr, 0) == 2`, то есть при
	//   WM_LBUTTONDBLCLK (`CControl_OnKeyDown@0x12DC240` — имя врёт, это OnLButtonDblClick;
	//   шкала событий `0x12DB630…0x12DC410`: 0xB = одиночное нажатие ЛКМ, 0xD = двойной ЛЕВЫЙ).
	//   Раньше порт применял предмет по ОДИНОЧНОМУ клику (отпускание над той же ячейкой) — из-за
	//   этого один физический двойной клик срабатывал ДВАЖДЫ (видно в живом логе 13.08: две строки
	//   «двойной клик по бустеру 66909» с разницей 78 мс).
	//   ⚠ Хотбар сюда НЕ входит: там применение остаётся ОДИНОЧНЫМ по прямому требованию юзера
	//   (осознанное отступление от эталона, см. `UAsdaHotbarSubsystem::HandleMouse`).
	//   ★ ИСПРАВЛЕНИЕ 11.10.2026 (дедуп №2, В2-A, U1): общий `FAsdaDblClick` — порог Windows `GetDoubleClickTime`, реальное время
	//   (было: `LastClickCont/LastClickCell/LastClickS` и порог `UInputSettings::DoubleClickTime` = 0,2 с; ключ отката
	//   `asda.UI.DblClickLegacy 1` возвращает его через `LegacySec`). Ключ узла: A = контейнер, B = ячейка.
	FAsdaDblClick CellDbl;
public:
	// ★ U1 (В2-A дедупа №2): КОПИЯ распознавателя двойного щелчка с прежним порогом окна (`LegacySec`) — для таблицы
	//   порогов самотеста `asda.Shop.SelfTestDblClick` (раздел U1). Своё состояние пары окна не трогает.
	FAsdaDblClick DebugDblClickProbe() const;
private:
	// ★ Волна 11 заточки: счётчик запросов «снять» (`UnequipItem` прошёл гейты слота) — только для самотеста.
	int32 DebugUnequipRequests = 0;
	// ★ Аудит №3 (B3): прежние латчи `bLmbWasDown` и `bRmbWasDown` — теперь один экземпляр общей
	//   точки: обе кнопки опрашивались В ОДНОЙ функции (`TickPointer`), значит и латч у них один
	//   (ПКМ по ячейке — меню «похвастаться», событие 0xE эталона).
	FAsdaMouseFrame Mouse;
	int32 HoverContainer = 0;
	int32 HoverCell = -1;
	bool  bTipShown = false;
	FString DragSavedStackText;

	// Очередь батчей содержимого (закон no-reentrant-net-callbacks).
	//   ★ Аудит №3 (B10): общая `TAsdaNetQueue` вместо «массив + общий `FCriticalSection`».
	//   Замок и раньше брался у каждой очереди ОТДЕЛЬНО (батчи — в `ApplyPendingBatches`, одиночные
	//   пакеты — своим блоком в `Tick`), общего снимка двух очередей не было ни разу.
	struct FPendingBatch { int32 ContainerId = 0; TArray<uint8> Raw; bool bEnd = false; };
	TAsdaNetQueue<FPendingBatch> PendingBatches;

	// Очередь ОДИНОЧНЫХ пакетов. `Kind` — что применять: 5001/5003/4999/5029/5007 либо
	//   1 = «положить 57-байтовую запись подобранного предмета» (из 5013).
	struct FPendingPacket { int32 Kind = 0; TArray<uint8> Body; };
	TAsdaNetQueue<FPendingPacket> PendingPackets;

	// Автосортировка: пары (откуда → куда), которые надо отправить по одной, ожидая 5001.
	struct FArrangePair { int32 From = 0; int32 To = 0; };
	TArray<FArrangePair> ArrangeQueue;
	int32  ArrangeContainer = 0;
	double LastAutoArrangeS = -1.0e9;   // эталон и Global: не чаще раза в 60 с (0xEA60 мс)
	// Эталонный флаг `this+0x1154` («сортировка идёт»), который ставит `inventoryAutoArrange@0xD35340`
	//   и снимает шаговый драйвер `inventoryAutoArrangeStep@0xD35E60`, выдав при этом нотис 112116.
	//   В Asda Global это байт `окно+0xDE8` (VA Global), пары — `окно+0xDEC`/`+0xDF0` (до 300),
	//   их число — `окно+0x174C`, курсор исполнения — `окно+0x1750`, вкладка прохода — `окно+0x175C`.
	//   Нужен именно отдельным полем: очередь может опустеть и на пропуске пары, а нотис — один.
	bool   bArrangeRunning = false;
	// Сколько пар сервер отклонил за текущий проход (эталон отказы не считает — это наш диагностический
	//   счётчик для лога: он показывает, что план разошёлся с реальностью).
	int32  ArrangeRefused = 0;

	// Ждём подтверждения уничтожения (для нотиса).
	bool bDumpPending = false;
	// На приёме батча содержимого ячеек десятки — ребилд хотбара делаем ОДИН раз в конце.
	bool bSuppressHotbarRefresh = false;

	FAsdaItemTooltipBridge Tooltip;
	bool bHotbarBridgeBound = false;
	bool bMenuBound = false;
	// Контроллер, на чьи делегаты клавиш мы подписаны (для отписки при смене мира).
	TWeakObjectPtr<class AAsdaPlayerController> BoundController;
	// Окно надо открыть/закрыть В TICK, а не прямо из колбэка меню/пакета (закон no-reentrant-net-callbacks
	//   + перегенерация виджетов внутри чужого обработчика). 0 = ничего, 1 = открыть, 2 = закрыть.
	int32 PendingWindowAction = 0;

public:
	// ═══════════════════ ПАРАМЕТРЫ (правятся в редакторе) ═══════════════════

	// ★ ЗАДЕЛ ПОД АВАТАР-СЛОТЫ (решение юзера). false = как в оригинале: 11 рабочих слотов.
	//   true включает индексы 11…19 в модели и в проверках; КОНТРОЛОВ для них в `INVEN_WIN` нет,
	//   поэтому рисовать их будет тот, кто добавит окно с этими ячейками (83531…83539).
	UPROPERTY(EditAnywhere, Category="Asda|Инвентарь|Аватар")
	bool bAvatarSlotsEnabled = false;

	// Сколько слотов экипировки считать РАБОЧИМИ (11 или 20 — см. флаг выше).
	UFUNCTION(BlueprintPure, Category="Asda|Inventory")
	int32 GetEquipSlotCount() const { return bAvatarSlotsEnabled ? kEquipSlotsTotal : kEquipSlots; }
	UFUNCTION(BlueprintPure, Category="Asda|Inventory")
	bool IsEquipSlotUsable(int32 Slot) const { return Slot >= 0 && Slot < GetEquipSlotCount(); }

	// Кегль счётчика стака. ★ Эталонного значения в данных НЕТ (размер шрифта в .JUI не хранится);
	//   у хотбара по просьбе юзера 7 — держим то же число, чтобы цифры выглядели одинаково.
	UPROPERTY(EditAnywhere, Category="Asda|Инвентарь|Счётчик стака")
	int32 StackCountFontPx = 7;

	// Обводка счётчика. ★ ЭТАЛОННОЕ ЗНАЧЕНИЕ — 0 (обводки нет).
	//   ★★★ ИСПРАВЛЕНО 2026-08-10 (К1 аудита `code_dedup_audit_2.md`) — ЧТО ЗНАЧИТ `TextOutLine`.
	//   БЫЛО (ОТМЕНЕНО): «свойство скина `NoImage_icon` `TextOutLine = "1"` ⇒ чёрная обводка 1 px».
	//   ПО БИНАРЮ: `TextOutLine` — это поле контрола **925** (`cfg_WriteTextOutLine @0x12E4D90`,
	//   ui_core.c:110573), и единственное его действие в `cControl_RenderLabel` (ui_core.c:110741-110746):
	//       textDrawFlags = field924;
	//       if (field925 == 1) textDrawFlags |= field926 | 0x2010;   	// перенос по словам + клип
	//       else               textDrawFlags |= field926 | 0x100;
	//   То есть 925 выбирает ФЛАГИ ВЫВОДА (перенос+обрезка рамкой), а НЕ обводку. Обводку/жирность/тень
	//   даёт поле **926** (`CClass_SetField926`): `dword_800000` = обводка, `0x200000` = жирный,
	//   `0x400000` = тень.
	//   ★★★ ПОПРАВКА 26.10.2026 (баг «число стопки плохо видно»): фраза «для счётчика эталон поле 926 НЕ ставит» была
	//   НЕВЕРНОЙ. Эталон СТАВИТ ячейкам 10010…10039 (`INVEN_SET0…29`) обводку: `CClass_SetField926(cell, 0x800000)` +
	//   `CControl_SetField924(cell, 9)` (item_inventory.c:35253), та же строка в `Tools/data/text_style926.tsv`
	//   (`INVEN_SET*` outline=1). Отсюда умолчание порта 0 было дефектом; теперь обводку ставит общая точка
	//   `AsdaItemCount::ApplyOutline` (ключ `asda.UI.ItemCountOutline`, умолчание 1 px чёрный). Это поле — значение при
	//   ключе 0 («как было в порте»).
	UPROPERTY(EditAnywhere, Category="Asda|Инвентарь|Счётчик стака")
	int32 StackOutlinePx = 0;

	UPROPERTY(EditAnywhere, Category="Asda|Инвентарь|Счётчик стака")
	FLinearColor StackOutlineColor = FLinearColor(0.f, 0.f, 0.f, 1.f);

	// Прозрачность летящего призрака и приглушённого исходного слота — как у хотбара (0.4).
	UPROPERTY(EditAnywhere, Category="Asda|Инвентарь|Перетаскивание")
	float DragGhostOpacity = 0.4f;
	UPROPERTY(EditAnywhere, Category="Asda|Инвентарь|Перетаскивание")
	float DragSourceOpacity = 0.4f;
	UPROPERTY(EditAnywhere, Category="Asda|Инвентарь|Перетаскивание")
	FVector2D DragGhostCursorOffset = FVector2D(-16.f, -16.f);

	// Сколько секунд ждать 5001, прежде чем снять флаг ожидания. ★ Эталон флаг по таймауту НЕ
	//   снимает вовсе (если ответ потерян, инвентарь «залипает» до перезахода) — это наш предохранитель.
	UPROPERTY(EditAnywhere, Category="Asda|Инвентарь")
	float ActionPendingTimeoutS = 5.f;

	// Пауза между автосортировками — эталонные 60 000 мс (`timeGetTime() - this[1712] >= 0xEA60`).
	UPROPERTY(EditAnywhere, Category="Asda|Инвентарь")
	float AutoArrangeCooldownS = 60.f;
};
