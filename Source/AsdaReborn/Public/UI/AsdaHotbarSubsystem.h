// Copyright (c) 2026 @sitxovski (Discord)
// UAsdaHotbarSubsystem — панель быстрого доступа `QUICK_NEW_WIN` (JUI IDNum 73000, gamemain_jui.ini:342386,
//   278 контролов). Полный реверс — `Reports/quick_win_re_A.md` (структура) и `quick_win_re_B.md`
//   (данные/сеть/применение); реализация и отступления — `Reports/quick_win_port.md`.
//
// ═══════════════════════════════ КАНОН: КАКОЕ ОКНО ЖИВО ════════════════════════════════════════
//   `QUICK_NEW_WIN` (73000, 468×50, Visible=1). Легаси `QUICK_WIN` (300, 551×50) МЁРТВ: первая
//   строка `CHotbar_InitUI@0xDE8D80` — `SetVisible(FindControlByID(300), 0)`, а рантайм-диспетчеры
//   (`hotbarMapMsgIdToSlot@0xDEEBB0`, `hotbarOnUiEvent@0xDF28E0`, `Hotbar_PageUpDown@0xDEEAD0`)
//   сравнивают ТОЛЬКО новые id 73xxx. Закон `jui-prefer-new-windows` здесь совпал с бинарём.
//   Ассет — `WBP_QUICK_NEW_WIN` (импорт JUI); свой UI не рисуем (закон `no-invented-ui`),
//   все параметры вынесены в UPROPERTY (закон `ui-editable-in-ue`).
//
// ═══════════════════════════════ АДРЕСНОЕ ПРОСТРАНСТВО СЛОТОВ ══════════════════════════════════
//   Массив `CHotbar::aSlotData` @ +0x69C, шаг 292 б, **78 записей** (`hotbarRebuildSlots@0xDEC380`:
//   `for (i=0; i<78; ++i)`; сервер `Recv_DBQuickSlotInfo_Save@0x4DAD60`: `if (v14 >= 0x4E) return;`).
//   Разбиение 78 = **6 «страниц» по 13**:
//     0..64  — 5 страниц основного ряда × 13 колонок (12 обычных + колонка 12 = СЛОТ ОРУЖИЯ);
//     65..76 — фиксированный «T»-ряд (12 слотов, страниц НЕ имеет) = страница 5, колонки 0..11;
//     77     — колонка 12 страницы 5: контрола НЕТ, но по сети передаётся.
//   Отсюда единая формула страницы для отправки: `page = absSlot / 13` (эталон
//   `Hotbar_SendLayout(this, slot/13)`), а обратно:
//     основной ряд: `abs = col + 13*CurrentPage` (col 0..12);
//     T-ряд:        `abs = col + 65`            (col 0..11) — ср. `Hotbar_ActivateSlotByIndex@0xDEE650`
//                   (`a3 >= 12 → a3 + 53`, т.е. 12..23 → 65..76).
//
// ═══════════════════════════════ ЗАПИСЬ СЛОТА (10 БАЙТ ПО СЕТИ) ════════════════════════════════
//   | +0 u8  Type    | 0 пусто · 1 предмет · 2 эмоция/действие · 3 навык · 4 навык соулмейта · 5 пункт доп-меню
//   | +1 u8  SubType | Type=1: 1 = сумка, 2 = надетое, 3 = комплект снаряжения
//   | +2 u16 Index   | Type=1: ИНДЕКС ЯЧЕЙКИ КОНТЕЙНЕРА · Type=2: код эмоции · Type=5: индекс пункта
//   | +4 u32 Count   | Type=1: количество в стеке (КЭШ, перечитывается из инвентаря)
//   | +8 u16 Id      | Type=1: itemCode (КЭШ) · Type=3/4: skillId/индекс записи
//   ★★★ ГЛАВНОЕ: предмет хранится как «КОНТЕЙНЕР + ИНДЕКС ЯЧЕЙКИ», НЕ как itemId. `Count`/`Id` —
//   кэш, который `hotbarRebuildSlots` перечитывает из инвентаря по (SubType, Index) при каждом
//   ребилде; ячейка опустела → слот молча чистится (`memset(&slot[0],0,0xA)`, LABEL_29/41).
//   Инвентаря в порту ещё НЕТ — модель хранит именно (контейнер, ячейка) и тянет отображение
//   через мост `FAsdaHotbarInventoryBridge` (см. ниже): появится инвентарь — переделывать нечего.
//
// ═══════════════════════════════ СЕТЬ ══════════════════════════════════════════════════════════
//   S→C **5059** (`OP_HOTBAR_LOAD_PAGE`, тело 131 = `u8 page` + 13×10) → `Hotbar_LoadPage@0xDEA3D0`.
//        Приходит ПО ОДНОЙ странице; MD5/токена в S→C нет. Обработчик пакета НЕ трогает UI напрямую
//        (закон `no-reentrant-net-callbacks`): страница копируется в очередь, применяется в `Tick`.
//   C→S **5067** (`OP_RECV_DB_QUICK_SLOT_INFO_SAVE`, тело 155 = 8 токен + 16 MD5 + 131)
//        → `Hotbar_SendLayout@0xDF2070`. Шлётся СРАЗУ на каждое изменение (одна страница; своп между
//        страницами — два пакета). Подтверждения сервер НЕ шлёт вовсе.
//   Применение предмета — **5000** (`OP_CHAR_ITEM_MOVE`, 46 = 8 + 16 + 22, код действия 3 в payload[15]),
//        эталон `net_sendInventoryItemUse@0xD11B20`.
//   ★ Адресация — сессионный `userIndex` соединения; идентификатора персонажа в теле НЕТ ВОВСЕ
//     (грабля системы Хранителя здесь не повторяется).
//   ★ В `NetOpcodes.h` есть ЧЕТЫРЕ имени-обманки: 6529, 6725, 5228, 6058 названы «quickslot/hotbar»,
//     но относятся к пати, луту и панели навыков соулмейта — к `CHotbar` отношения не имеют.
//
// ═══════════════════════════════ УСТРОЙСТВО СЛОТА (по JUI) ═════════════════════════════════════
//   Иконка+счётчик  `QUICK_NEW_SLOTn` (73011..73023), скин `NoImage_icon` (Type=Text) → в WBP это
//                   ПАРА: план-картинка `QUICK_NEW_SLOTn_<idx>_0` (иконка) и `QUICK_NEW_SLOTn`
//                   (TextBlock). ★ Отдельного контрола счётчика стака НЕТ — число рисуется ТЕКСТОМ
//                   самого контрола-иконки (`CHotbar_RefreshPage:66450`), выравнивание EXS TextAlign=9.
//   Шторка перезарядки `QUICK_NEW_COOLn` (73051..73063), скин `CoolTime_Effect`:
//                   **СПРАЙТ `NE_CoolTime32.tga`, 118 кадров 32×32, Diffuse B4000000** (не круговой бар!).
//   Вспышка «готово»  `QUICK_NEW_CENDn` (73031..73043), скин `CoolTimeAfter_Effect`:
//                   **`NE_IconEffect1.tga`, 16 кадров 64×64 @(−16,−16), Diffuse B4FFFFFF**.
//   Подложка-гнездо   безымянный план `QUICK_BACK_1` (у оружия — `QUICK_BACK_2`), 32×32, шаг 35 px.
//   Затемнение        клоны `QUICK_NEW_Disable1` (73151) / `QUICK_NEW_Disable_2` (73300), `Black_Ot125`.
//   Подпись клавиши   рантайм-клоны глифов `NEW_Key_*` (73080..73144) с родителем-слотом.
//
// ═══════════════════════════════ ПОВЕДЕНИЕ ОКНА ════════════════════════════════════════════════
//   Блока `EVS` НЕТ ни у одного из 278 контролов ⇒ **окно НЕ перетаскивается** (закон
//   `ui-window-drag-hittest`: перетаскивание — это ДАННЫЕ JUI). Анкор — POINT (1,1): у всех 278
//   `fx = fy = 1.0`, ни одного span ⇒ жёстко в ПРАВЫЙ-НИЖНИЙ угол, без растяжения. `AlwaysTop = 0`.
//   Обработчика ПКМ у хотбара НЕТ — снятие содержимого делается перетаскиванием НАРУЖУ.
//   Второй ряд включается опцией `g_bOptChildPairVisible` (`uiToggleChildPairVisible@0xDE9A40`).
//   `uiHide/ShowPanelControls` управляют НЕ видимостью слотов, а ЗАТЕМНЯЮЩИМИ оверлеями:
//   «Hide» = снять затемнение (хотбар активен), «Show» = затемнить (заблокирован). Флаг
//   `bPanelHidden` (+0x524, ctor = 1) = «хотбар активен»; при 0 `hotbarOnUiEvent` игнорирует ВСЁ.
//
// ═══════════════════════════════ СМЕНА СТРАНИЦЫ ════════════════════════════════════════════════
//   Кнопки `QUICK_NEW_UPBUT` (73327) / `QUICK_NEW_DOWNBUT` (73328) → событие 66561 (0x10401) →
//   `Hotbar_PageUpDown` (wrap 5→0 / <0→4). Клавиши PAGE 1..5 (cmd 1..5) → `Hotbar_SetPage`.
//   `Hotbar_SetPage@0xDEF110`: блокируется при открытом инвентаре; ВНЕ БОЯ уходит в
//   `sub_91E96F` = `sub_DEF3A0(localPlayer, page, targetRenderId)`, который НЕ меняет страницу сам,
//   а СТАВИТ ПЕРСОНАЖУ ОТЛОЖЕННОЕ ДЕЙСТВИЕ: `char+0x32E44 = 104 (0x68)`, `char+0x374C8 = page`,
//   `char+0x374CC = targetRenderId`. Машина состояний персонажа (switch по `char+0x32E44`,
//   `AsdaStory_game.c:83355`, case 0x68) на следующем тике делает:
//       `hotbarSetCurrentPage(page)`  →  внутри `CHotbar_RefreshPage` + `hotbarUseActiveSlotItem`
//                                        (АВТОПРИМЕНЕНИЕ слота оружия страницы);
//       в бою — дальше ничего;
//       `param32 == -1` → `CharFx_AutoSabzil_State(char, dt, 0, 0, 20)` — анимация СУБРЕЖИМА 20
//                         (смена оружия/стойка, ср. память `weapon-anim-sets`);
//       иначе          → `netSabzil_SendActionRaw(...)` + субрежим 22.
//   ⇒ Риск из реверса подтверждён: страница действительно МЕНЯЕТ НАБОР ОРУЖИЯ, и делает это
//     отложенно, через персонажа, с анимацией. Реализовано (`PendingPageAction`).
//   В БОЮ ветка другая: страница меняется сразу + `CHotbar_RefreshPage` + `hotbarUseActiveSlotItem`.
//
// ═══════════════════════════════ ИСПРАВЛЕННЫЕ ДЕФЕКТЫ ОРИГИНАЛА (закон fix-original-defects) ═════
//   Д1  Скины `QUICK_NEW_CEND13`(73043)/`QUICK_NEW_COOL13`(73063) у слота ОРУЖИЯ ПЕРЕПУТАНЫ в JUI
//       (CEND13 получил `CoolTime_Effect`, COOL13 — `CoolTimeAfter_Effect`), хотя код кладёт 73043
//       в массив вспышек, а 73063 — в массив шторок. Мы связываем контролы ПО РОЛИ СКИНА
//       (118 кадров = шторка, 16 кадров = вспышка), а не по имени.   `asda.Quick.LegacySkinSwap 1`
//   Д2  `hotbarSetCurrentPage@0xDEE9F0` гасит и тут же СНОВА показывает цифру `nPrevPage` (обе строки
//       используют `nPrevPage`; вторая должна быть `nCurrentPage`) — вне боя цифра страницы не
//       обновлялась вовсе. Чиним.                                     `asda.Quick.LegacyPageDigit 1`
//   Д3  Там же нет проверки границ `nPrevPage <= 4` (в `Hotbar_SetPage` она есть) — чтение за краем
//       массива `apPageNumText[5]`. Чиним (клампим).
//   Д4  `Hotbar_SlotActivate@0xDEDDD0` отбрасывает индекс по условию `a3 > 0x4E` (78), хотя массив
//       0..77 — индекс 78 проходил и читал за краем. Чиним на `>= 78`.
//   Д5  Сдвиг имён в T-таблице глифов: `NEW_T_Key_F12`(73276) носит скин `QSlot_F11`, а
//       `NEW_T_Key_F13`(73277) — `QSlot_F12` (idc врут, скины верны). Таблица «VK → глиф» строится
//       по СКИНУ, поэтому подписи корректны.
//   Д6  Вспышка «готово» (`CoolTimeAfter_Effect`) в срезе эталона НИГДЕ не показывается — контролы
//       73031..73043 только `CloseHide`-ятся. Ресурс есть, роль скина прямая («после кулдауна»),
//       поэтому мы её ИГРАЕМ по факту окончания перезарядки.        `asda.Quick.LegacyNoReadyFlash 1`
//   Д7  Опечатка idc `NEW_Key_Shit` (73141) при скине `QSlot_Shft` — на лукап не влияет (ищем по
//       скину), фиксируем документально.
//   ★ ЛОЖНАЯ ТРЕВОГА реверса: «у ряда 2 создаётся 11 клонов вместо 12». Клоны j=1..11 плюс сам
//     базовый контрол дают РОВНО 12 оверлеев на 12 T-слотов (как 13 = база + i=1..12 у ряда 1).
//     Ничего не меняем.
//
// ═══════════════════════════════ ЧЕГО ЕЩЁ НЕТ В ПОРТЕ ══════════════════════════════════════════
//   Инвентаря и книги навыков нет. Точки подключения — `FAsdaHotbarInventoryBridge`,
//   `FAsdaHotbarSkillBridge`, `FAsdaHotbarMiscBridge` (эмоции / доп-меню). Пока мост не
//   зарегистрирован, слоты соответствующих типов держат ДАННЫЕ (они приходят с сервера и уходят
//   обратно без потерь), но не рисуют иконку и не применяются — с одной честной записью в лог.
#pragma once

#include "CoreMinimal.h"
#include "AsdaTickGate.h"   // ★ R4-b (14.11.2026): шаблон класса (CDO) не тикает
#include "Subsystems/GameInstanceSubsystem.h"
#include "Tickable.h"
#include "UI/AsdaTipPanel.h"   // EAsdaTipOwner — владелец ОБЩЕЙ плашки-тултипа
#include "UI/AsdaUiInputTypes.h"   // ★ R1: FAsdaMouseFrame / FAsdaDragArm / FAsdaDragGhost … — поля ввода (без всего AsdaUiHelpers.h)
#include "AsdaNetQueue.h"          // TAsdaNetQueue — очередь «колбэк копит, Tick применяет»
#include "AsdaSessionReset.h"   // IAsdaSessionResettable — сброс состояния сессии
#include "AsdaHotbarSubsystem.generated.h"

class UUserWidget;
class UWidget;
class UImage;
class UButton;
class UTextBlock;
class UAsdaAnimatedPlane;
class UAsdaButtonRelay;

// ── Тип содержимого слота (`slot[0]`, эталон `hotbarRebuildSlots` + `Hotbar_SlotActivate`) ──
UENUM(BlueprintType)
enum class EAsdaHotbarSlotType : uint8
{
	Empty      = 0 UMETA(DisplayName = "0 — пусто"),
	Item       = 1 UMETA(DisplayName = "1 — предмет (контейнер+ячейка)"),
	Emotion    = 2 UMETA(DisplayName = "2 — эмоция/действие"),
	Skill      = 3 UMETA(DisplayName = "3 — навык (по индексу записи книги)"),
	SkillAuto  = 4 UMETA(DisplayName = "4 — навык соулмейта / авто-подбор уровня"),
	ExtraMenu  = 5 UMETA(DisplayName = "5 — пункт доп-меню"),
};

// ── Контейнер предмета (`slot[1]` при Type=1) ──
UENUM(BlueprintType)
enum class EAsdaHotbarContainer : uint8
{
	// ⚠⚠⚠ ДВА РАЗНЫХ ПРОСТРАНСТВА НУМЕРАЦИИ — НЕ ПУТАТЬ (уточнено 2026-08-08 реверсом инвентаря,
	//   `Reports/inventory_re_B.md`; имена НИЖЕ менять НЕЛЬЗЯ — по ним ветвится `ActivateSlot`).
	//   (1) ЗДЕСЬ — `SubType` из 10-байтовой ЗАПИСИ СЛОТА ХОТБАРА (эталон `hotbarRebuildSlots`):
	//       1 = сумка · 2 = надетое · 3 = комплект снаряжения. Эти числа ВЕРНЫ.
	//   (2) КОНТЕЙНЕРЫ ИНВЕНТАРЯ (другая нумерация, для будущего моста и окна инвентаря):
	//       1 = снаряжение/квестовые, 30 слотов («ShellInven»)
	//       2 = расходники/самоцветы + ЗОЛОТО (itemCode 20551), 60 слотов («SpendCellobInven»)
	//       3 = НАДЕТОЕ, 11 слотов ·  4 = НЕ инвентарь вовсе (12 слотов КВЕСТОВ по 44 б)
	//       5 = склад (до 270).  «Комплект снаряжения» — не контейнер, а надстройка над 3.
	//   ⇒ При подключении моста инвентаря ОБЯЗАТЕЛЬНО транслировать SubType хотбара → contId
	//     инвентаря явной таблицей, а не «как есть»: совпадение чисел здесь обманчиво.
	//   ★★★ УТОЧНЕНО 2026-08-08 разбором `hotbarRebuildSlots@0xDEC380` (ui_core.c:66949-66964):
	//     ЧИСЛА совпадают (1→1, 2→2, 3→3), а вот ИМЕНА НИЖЕ ВРУТ — 2 это НЕ «надетое», а второй
	//     контейнер инвентаря (расходники/самоцветы/золото), надетое = 3. Имена оставлены как есть
	//     ТОЛЬКО потому, что по ним ветвится `ActivateSlot`.
	//     ★★★ СНЯТО ЛОЖНОЕ «ЭТАЛОННОЙ ВЕТКИ В СРЕЗЕ НЕТ» (второй заход, 2026-08-13): ветка есть —
	//     `Hotbar_SlotActivate@0xDEDDD0`, `ui_core.c:67378-67425`. Проверка «верхом нельзя» стоит
	//     там ВНУТРИ `if (ItemRecord)` подтипа 1 и (молча, без нотиса) в подтипе 3; у подтипа 2 и
	//     у премиум-ветки `else if (Table2Record)` её нет. `ActivateSlot` приведён к этому 1:1.
	//     Трансляция сделана таблицей `UAsdaInventorySubsystem::HotbarSubTypeToContainer`.
	None      = 0 UMETA(DisplayName = "0 — нет"),
	Bag       = 1 UMETA(DisplayName = "1 — контейнер 1: снаряжение/квестовые (30)"),
	Equipped  = 2 UMETA(DisplayName = "2 — контейнер 2: расходники/самоцветы/золото (60) ★ имя врёт, это НЕ надетое"),
	EquipSet  = 3 UMETA(DisplayName = "3 — контейнер 3: НАДЕТОЕ (11) ★ имя врёт, это не «комплект»"),
};

// ═══ Одна запись слота. Первые 5 полей — РОВНО те 10 байт, что летят по сети и лежат в БД. ═══
USTRUCT(BlueprintType)
struct ASDAREBORN_API FAsdaHotbarSlot
{
	GENERATED_BODY()

	// --- персистентная часть (10 байт) ---
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Asda|Hotbar") uint8  Type = 0;      // +0
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Asda|Hotbar") uint8  SubType = 0;   // +1
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Asda|Hotbar") int32  Index = 0;     // +2 u16
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Asda|Hotbar") int32  Count = 0;     // +4 u32 (кэш)
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Asda|Hotbar") int32  Id = 0;        // +8 u16 (кэш)

	// --- локальный кэш рендера (эталон держит его в тех же 292 байтах, +10/+0x110/+0x114/+0x118) ---
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="Asda|Hotbar") FString IconAtlas;      // +10 имя текстуры
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="Asda|Hotbar") int32   IconX = 0;      // +0x110
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="Asda|Hotbar") int32   IconY = 0;      // +0x114
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="Asda|Hotbar") int32   IconSize = 32;  // +0x118
	// true = иконка из item_img_table (+288 == 1); false = «таблица 2» / прямая текстура 32×32.
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="Asda|Hotbar") bool    bIconFromImgTable = false;
	// true = иконка это ОТДЕЛЬНАЯ текстура целиком (эталон рисует её `DrawTextureRect(tex, 0,0, 32,32)`),
	//   а не ячейка атласа. Так рисуются иконки эмоций, навыков и навыков соулмейта.
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="Asda|Hotbar") bool    bIconWholeTexture = false;

	bool IsEmpty() const { return Type == 0; }
	void Clear()
	{
		Type = 0; SubType = 0; Index = 0; Count = 0; Id = 0;
		IconAtlas.Reset(); IconX = 0; IconY = 0; IconSize = 32;
		bIconFromImgTable = false; bIconWholeTexture = false;
	}
	// Сравнение ТОЛЬКО персистентной части (для «страница изменилась → слать 5067»).
	bool WireEquals(const FAsdaHotbarSlot& O) const
	{
		return Type == O.Type && SubType == O.SubType && Index == O.Index && Count == O.Count && Id == O.Id;
	}
};

// ═══ Состояние перезарядки, как его отдаёт реестр CSkillWnd (20-байтовая структура эталона) ═══
//   `{ int state; float remain; int iconId; int slotIdx; }`:
//     state 1 = идёт перезарядка (remain — ОСТАТОК В МИЛЛИСЕКУНДАХ, уходит в FrameInterval спрайта);
//     state 2 = «особый статик-кадр» (эталон: `ShowWithItem(ctrl, iconId, 127, 8, 1000, 0)` —
//               animate=0, то есть ЗАМОРОЖЕННЫЙ кадр iconId; смысл пары 127/8 в срезе не раскрыт,
//               воспроизводим ровно как есть);
//     иначе — контрол прячется.
USTRUCT(BlueprintType)
struct ASDAREBORN_API FAsdaHotbarCooldown
{
	GENERATED_BODY()
	UPROPERTY(BlueprintReadOnly, Category="Asda|Hotbar") int32 State = 0;
	UPROPERTY(BlueprintReadOnly, Category="Asda|Hotbar") float RemainMs = 0.f;
	UPROPERTY(BlueprintReadOnly, Category="Asda|Hotbar") int32 IconFrame = 0;   // текущий кадр 0..117
};

// ⚠ СНЯТО 2026-08-14: структура `FAsdaHotbarCurtainShown` («что сейчас показано на шторке колонки»)
//   заменена ОБЩЕЙ `FAsdaCooldownCurtain` (`UI/AsdaUiHelpers.h`) — теми же тремя полями плюс сам план.
//   Причина: мест показа шторки стало три (панель, инвентарь, дерево навыков), и держать кэш и
//   механику кадров в трёх копиях запрещено (закон `port-shared-points`). Единственное поле, которое
//   в общую структуру НЕ переехало, — `FlashEndS`: вспышка «готово» есть только у панели, у ячеек
//   инвентаря и узлов дерева контрола `CoolTimeAfter_Effect` рядом нет. Оно живёт прямо в
//   `FAsdaHotbarSlotUi::FlashEndS`.

// ═══ Запись контейнера инвентаря (57 байт эталона), приведённая к читаемым полям ═══
//   Раскладка подтверждена `hotbarRebuildSlots`, `net_sendInventoryItemUse@0xD11B20`:
//     u32 itemCode @0 · u8 @4 · u16 @5 · u16 @7 · u32 count @9 · u16 @14 · u8 flag @50.
//   Поля @4/@5/@7/@14 — идентификатор экземпляра предмета в БД: именно они (а НЕ индекс ячейки!)
//   уходят в пакет 5000. Поэтому мост обязан отдавать СЫРЫЕ 57 байт.
USTRUCT(BlueprintType)
struct ASDAREBORN_API FAsdaHotbarItemEntry
{
	GENERATED_BODY()
	UPROPERTY(BlueprintReadOnly, Category="Asda|Hotbar") bool  bValid = false;
	UPROPERTY(BlueprintReadOnly, Category="Asda|Hotbar") int32 ItemCode = 0;
	UPROPERTY(BlueprintReadOnly, Category="Asda|Hotbar") int32 Count = 0;
	UPROPERTY(BlueprintReadOnly, Category="Asda|Hotbar") int32 ItemType = 0;      // ItemRecord->itemType
	UPROPERTY(BlueprintReadOnly, Category="Asda|Hotbar") int32 ItemSubType = 0;   // ItemRecord->itemSubType
	// ★★★ «У предмета ЕСТЬ строка `item_list`» (эталонное `ItemRecord != nullptr`). Премиум-предметы
	//   (коды 1…558) лежат ТОЛЬКО в `special_itemlist`, у них этот флаг false. По нему ветвится
	//   `Hotbar_SlotActivate@0xDEDDD0` (`ui_core.c:67380-67425`): ветка `if (ItemRecord)` содержит
	//   проверку «верхом нельзя» (нотис 2040), а ветка `else if (Table2Record)` — НЕ содержит.
	//   Без этого флага панель блокировала верхом и премиум-канал, то есть СЛЕЗТЬ С МАУНТА
	//   кнопкой панели становилось невозможно.
	UPROPERTY(BlueprintReadOnly, Category="Asda|Hotbar") bool  bHasItemRecord = false;
	// Сырые 57 байт записи контейнера (для сборки пакета 5000 «использовать»).
	UPROPERTY(BlueprintReadOnly, Category="Asda|Hotbar") TArray<uint8> Raw57;
};

// ═══ МОСТ К ИНВЕНТАРЮ. Регистрируется будущей системой инвентаря; до тех пор пуст. ═══
//   Требования выписаны в `quick_win_re_B.md` §12.
struct FAsdaHotbarInventoryBridge
{
	// Запись контейнера (1 сумка / 2 надетое / 3 комплект) по индексу ячейки.
	TFunction<FAsdaHotbarItemEntry(int32 /*Container*/, int32 /*CellIndex*/)> GetEntry;
	// ★★★ «Содержимое контейнера уже пришло с сервера ЦЕЛИКОМ». Пока false, ответ `GetEntry` =
	//   «ячейка пуста» НЕ является фактом и по нему НЕЛЬЗЯ чистить слот: 5059 применяется в кадре,
	//   где батчи инвентаря ещё не разобраны, а очищенная страница уедет на сервер пакетом 5067
	//   при первом же следующем изменении и СОТРЁТ раскладку насовсем (живой лог 2026-08-09:
	//   5067 стр.5 в 15:42:20 унёс слоты 72/73/75, стёртые ребилдом в 15:41:51.583).
	//   Не задан ⇒ считаем «готов» (обратная совместимость с мостами без этого поля).
	TFunction<bool(int32 /*HotbarSubType*/)> IsContainerReady;
	// «Использовать из сумки» — эталон `net_sendInventoryItemUse@0xD11B20` (опкод 5000, код действия 3).
	TFunction<void(int32 /*CellIndex*/)> UseFromBag;
	// Двойной клик по надетому/комплекту — эталон `itemUseDoubleClick(wnd, idx, bFromBag)`.
	TFunction<void(int32 /*CellIndex*/, bool /*bFromBag*/)> UseByDoubleClick;
	// «Использовать из комплекта снаряжения» — эталон `netSendItemUseReq(g_pEquipSetWindow, idx)`.
	TFunction<bool(int32 /*CellIndex*/)> UseFromEquipSet;
	// ★★★ УДАЛЕНО 2026-08-15: здесь был хук `IsInventoryOpen` («открыт инвентарь → панель и смена
	//   страницы запрещены»). Это была ОШИБКА ЧТЕНИЯ эталона: `CObject_GetField254@0x95CD10`
	//   (`ui_core.c:67365` и `:67722`) — флаг «открыт барабан гачи» (`set_oFE@0xD1F7D0`, окно JUI
	//   10050), а не видимость инвентаря. Гейт перенесён в сам хотбар и спрашивает
	//   `UAsdaBoosterPanelSubsystem::IsWindowOpen()`. Хук убран, чтобы неверный смысл не всплыл
	//   заново; других обращений к нему в срезе исходников нет.
	// Тултип предмета ячейки (эталон `ItemTooltip_Build`).
	TFunction<void(int32 /*Container*/, int32 /*CellIndex*/)> ShowTooltip;

	bool IsBound() const { return (bool)GetEntry; }
};

// ═══ МОСТ К КНИГЕ НАВЫКОВ (Type 3/4). ═══
struct FAsdaHotbarSkillBridge
{
	TFunction<FString(int32 /*RecordIdx*/, bool& /*bOutUnavailable*/)> GetSkillIconName;   // + суффикс «-g»
	TFunction<int32(int32 /*SkillId*/)> GetTargetKind;                                     // 1 = на цель, 2 = боевой
	TFunction<FAsdaHotbarCooldown(int32 /*SkillId*/)> GetCooldown;
	TFunction<void(int32 /*SkillId*/, int32 /*Extra*/)> UseCombatSkill;                    // combatHandleSkillUse
	TFunction<void(int32 /*SkillId*/)> UseTargetSkill;                                     // skillUseOnTargetDispatch
	TFunction<void(int32 /*RecordIdx*/)> CastAuto;                                         // skillCastActivate (Type=4)
	TFunction<int32(int32 /*RecordIdx*/)> FindBestSlotForLevel;                            // авто-подмена уровня Type=4
	TFunction<bool()> HasSoulmate;                                                         // gap_8B4[6]==1 || gap_878[47]>-1
	// ★★★ ПОДСКАЗКА НАВЫКА. Мост ТОЛЬКО СТРОИТ ТЕКСТ, а показывает и гасит его сам хотбар — иначе
	//   плашку берёт окно навыков (владелец `SkillWin`), и его же покадровый тик гасит её на
	//   следующем кадре (закон ui-shared-tip-ownership: гасит ТОЛЬКО владелец показа).
	//   Эталон здесь `skillShowTooltipForId@0xE54120` (Type 3) и
	//   `SkillTooltip_BuildSoulmateTooltip@0xEAF450` (Type 4) — ОБА ищут запись ПО КОДУ НАВЫКА
	//   и к сетке открытого окна отношения не имеют.
	TFunction<bool(int32 /*SkillId*/, bool /*bSoulmate*/, FString& /*OutTitle*/, FString& /*OutBody*/)> BuildTooltip;

	// ★★★ ИСПРАВЛЕНО 2026-08-09. Раньше `IsBound()` смотрел ТОЛЬКО на `GetSkillIconName` — то есть
	//   отвечал на вопрос «мост рисует иконки?», а вызывающий читал его как «мост умеет применять».
	//   Пока окно навыков регистрировало четыре поля из девяти, слот навыка проходил `IsBound()`
	//   и молча умирал на `if (… && Skills.Use…)` (разбор — `hotbar_inventory_drag.md` §14).
	//   Теперь есть ТРИ раздельных вопроса, и каждая ветка спрашивает свой:
	//     `IsBound()`      — мост вообще зарегистрирован (есть иконки: рисование слотов);
	//     `CanUse()`       — мост умеет ПРИМЕНЯТЬ обычный навык (Type = 3);
	//     `CanCastAuto()`  — мост умеет применять навык соулмейта (Type = 4).
	//   Сами ветки `ActivateSlot` по-прежнему проверяют КОНКРЕТНЫЙ нужный колбэк — это не дубль,
	//   а второй рубеж: `GetTargetKind` решает, какой из двух вызывать.
	bool IsBound() const { return (bool)GetSkillIconName; }
	bool CanUse() const { return (bool)GetTargetKind && ((bool)UseCombatSkill || (bool)UseTargetSkill); }
	bool CanCastAuto() const { return (bool)CastAuto; }
};

// ═══ МОСТ К ЭМОЦИЯМ (Type 2) И ДОП-МЕНЮ (Type 5). ═══
struct FAsdaHotbarMiscBridge
{
	TFunction<FString(int32 /*EmotionCode*/)> GetEmotionIconName;   // emotionRecord+32
	TFunction<void(int32 /*EmotionCode*/)> PlayEmotion;             // uiDispatchTargetMenuByRange
	TFunction<void(int32 /*EmotionCode*/)> ShowEmotionTooltip;

	// ═══ ДОП-МЕНЮ (Type 5). ═══
	// ★★★ КЛЮЧ ПУНКТА ПРИХОДИТ ПАРОЙ (`KeyKind`, `Key`), а не одним числом.
	//   `KeyKind` — это поле `SubType` слота (+1; в БД колонка `CharSlot.Plate`):
	//     0 = ЭТАЛОННЫЙ смысл: `Key` — ПОРЯДКОВЫЙ НОМЕР строки меню (`uiHotbarAssignSlot@0xECDFB0`
	//         кладёт `Src[1] = nSelectedSlotIndex`, а `SubType` остаётся нулём от `memset`);
	//     1 = НАШ УСТОЙЧИВЫЙ ключ: `Key` — КОД ДЕЙСТВИЯ пункта (`rec+68`, тот самый, который
	//         эталон отдаёт в `uiOpenWindowDispatch`). Заведён 2026-08-30 по решению владельца:
	//         порядковый номер сдвигается при любом изменении состава меню.
	//   Развести их по ВЕЛИЧИНЕ нельзя — оба лежат в 0…11, поэтому маркер обязателен.
	//   Разбор обоих видов — в `UAsdaExtraMenuSubsystem::FindByStableKey`; хотбар в смысл ключа
	//   не вникает и просто передаёт пару дальше.
	TFunction<bool(int32 /*KeyKind*/, int32 /*Key*/, FString& /*OutTex*/, int32& /*OutX*/, int32& /*OutY*/)> GetExtraMenuIcon;
	TFunction<void(int32 /*KeyKind*/, int32 /*Key*/)> OpenExtraMenuItem;      // uiOpenWindowDispatch(rec[68])
	TFunction<void(int32 /*KeyKind*/, int32 /*Key*/)> ShowExtraMenuTooltip;
};

// ═══ Виджеты одной ВИЗУАЛЬНОЙ колонки (13 у основного ряда, 12 у T-ряда) ═══
USTRUCT()
struct FAsdaHotbarSlotUi
{
	GENERATED_BODY()
	UPROPERTY(Transient) TObjectPtr<UImage>              Icon = nullptr;       // план `{idc}_{idx}_0`
	UPROPERTY(Transient) TObjectPtr<UTextBlock>          StackText = nullptr;  // TextBlock `{idc}` — счётчик стака
	UPROPERTY(Transient) TObjectPtr<UAsdaAnimatedPlane>  Flash = nullptr;      // вспышка (16 кадров 64×64)
	UPROPERTY(Transient) TObjectPtr<UImage>              Dim = nullptr;        // затемнение (база или клон)
	UPROPERTY(Transient) TObjectPtr<UImage>              KeyGlyph = nullptr;   // клон основного глифа клавиши
	UPROPERTY(Transient) TObjectPtr<UImage>              KeyModGlyph = nullptr;// клон глифа модификатора
	// Канвас-группа слота `{idc}_grp` — импортёр кладёт КАЖДЫЙ контрол в свою группу, и координаты
	//   внутри неё ЛОКАЛЬНЫЕ. Клоны-подписи клавиш кладём именно сюда (эталон: родитель = сам слот).
	UPROPERTY(Transient) TObjectPtr<class UPanelWidget>  Group = nullptr;
	UPROPERTY(Transient) FVector2D GroupPos = FVector2D::ZeroVector;           // позиция группы в канвасе ряда
	UPROPERTY(Transient) FVector2D IconBasePos = FVector2D::ZeroVector;        // позиция иконки ВНУТРИ группы
	UPROPERTY(Transient) FVector2D IconBaseSize = FVector2D(32.f, 32.f);
	// Шторка перезарядки (118 кадров 32×32, `QUICK_NEW_COOLn`) + кэш «что на ней уже запущено».
	//   ★ ОБЩАЯ структура: тот же тип держат ячейка инвентаря и узел дерева навыков.
	UPROPERTY(Transient) FAsdaCooldownCurtain Cool;
	// ★ Страховка от «залипшей» вспышки готовности: момент, после которого её гасим принудительно,
	//   даже если план не доиграл (нет данных скина ⇒ событие «анимация завершена» не придёт).
	//   Поле панели: у ячеек инвентаря и узлов дерева контрола вспышки рядом нет.
	double FlashEndS = 0.0;
};

// ═══ Одна иконка уведомления (P_ICON00_N..06_N + рамка P_ICONSEL) ═══
USTRUCT()
struct FAsdaHotbarNoticeUi
{
	GENERATED_BODY()
	UPROPERTY(Transient) TObjectPtr<UImage>             Icon = nullptr;
	UPROPERTY(Transient) TObjectPtr<UAsdaAnimatedPlane> SelFrame = nullptr;   // Slot_AniSelect (4 кадра, 300 мс)
	// ★★★ ГРУППА-РОДИТЕЛЬ `P_ICON0X_N_grp`. В `.JUI` у слотов 73350…73356 стоит `Visible = "0"`, а
	//   ДЕТЕЙ у них ЕСТЬ (рамка `P_ICONSEL0X_N`, 73360…73366) — значит импортёр делает из слота
	//   CanvasPanel-ГРУППУ и по правилу `asda_jui_generic_umg.py` (`eff_hidden` → `COLLAPSED`)
	//   сворачивает ИМЕННО ГРУППУ. Свёрнутый родитель в Slate не рисует и не хит-тестит поддерево,
	//   поэтому включать надо и её, а не только `Icon`/`SelFrame` (те же грабли уже разбирались у
	//   панели бустера — `AsdaBoosterPanelSubsystem.cpp`, «гасим и подложку, и кнопку, и подпись»).
	UPROPERTY(Transient) TObjectPtr<UWidget> Group = nullptr;
	UPROPERTY(Transient) int32 NoticeType = -1;                               // -1 = слот свободен

	// ── ★★★ ПУЛЬСАЦИЯ ALPHA (эталон `uiControlPairToggleBlink@0xDF65B0` + таймер 500 мс) ──
	//   `statusBarAddIcon@0xDF3F80` взводит `SetTimer(hWnd, i + 18, 500, NULL)` и ставит фазу 1,
	//   предварительно запустив `SetColor4(0xFF)` + `objStartTimedAction(255 → 0, 500 мс)`.
	//   Каждый тик таймера (`WindowProc_OnTimerCtrlArray@0x986A30` → `uiControlPairToggleBlink`):
	//     фаза 1 → `SetColor4(0)`,    прогон 0 → 255 за 500 мс;
	//     фаза 0 → `SetColor4(0xFF)`, прогон 255 → 128 за 1000 мс (обрывается следующим тиком);
	//     фаза инвертируется.
	//   ⇒ видимый цикл 1000 мс: 0→255 за 500 мс, затем 255→~191 за 500 мс. Воспроизводим 1:1
	//   на дельте кадра; в UMG эталонному `SetColor4` соответствует `SetRenderOpacity`
	//   (тинт брашa трогать нельзя — им задан сам рисунок иконки).
	float BlinkTimer   = 0.f;    // до следующего тика таймера 500 мс
	int32 BlinkPhase   = 1;      // `aNoticeIcon[12*i+8]`
	float BlinkFrom    = 255.f;  // начало текущего прогона `objStartTimedAction`
	float BlinkTo      = 0.f;    // конец текущего прогона
	float BlinkDur     = 0.5f;   // длительность прогона, с
	float BlinkElapsed = 0.f;    // сколько прогона прошло, с
};

UCLASS()
class ASDAREBORN_API UAsdaHotbarSubsystem : public UGameInstanceSubsystem, public FTickableGameObject,
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
	virtual TStatId GetStatId() const override { RETURN_QUICK_DECLARE_CYCLE_STAT(UAsdaHotbarSubsystem, STATGROUP_Tickables); }
	// ★ R4-b (14.11.2026): шаблон класса (CDO) не тикает — раньше `Tick` звался дважды за кадр, у рабочего объекта и у шаблона
	//   (ключ `asda.Perf.NoCdoTick`, разбор — `AsdaTickGate.h`). Рабочий объект тикает как раньше.
	virtual bool IsTickable() const override { return AsdaTickGate::NotCdo(this); }
	virtual bool IsTickableInEditor() const override { return false; }
	virtual bool IsTickableWhenPaused() const override { return false; }

	// ── Константы адресного пространства (эталон, не менять) ──
	static constexpr int32 kSlotCount   = 78;   // hotbarRebuildSlots: for (i=0; i<78; ++i)
	static constexpr int32 kPageCount   = 6;    // hotbar_SendAllLayouts: ehGuard = 6
	static constexpr int32 kColsPerPage = 13;   // 12 обычных + колонка 12 = слот оружия
	static constexpr int32 kMainPages   = 5;    // переключаемых страниц основного ряда (0..4)
	static constexpr int32 kWeaponCol   = 12;   // колонка слота оружия
	static constexpr int32 kTRowFirst   = 65;   // абсолютный индекс T1
	static constexpr int32 kTRowCols    = 12;
	static constexpr int32 kWireSlotBytes = 10; // байт на слот по сети
	static constexpr int32 kWirePageBytes = 1 + kColsPerPage * kWireSlotBytes;   // 131

	// ═══════════════════ ТОЧКИ ВХОДА ДЛЯ ГОРЯЧИХ КЛАВИШ (их делает ДРУГОЙ агент) ═══════════════
	//   Соответствие cmd → метод (эталон `hotkeyDispatchAction@0xE3CD90`):
	//     cmd 1..5   PAGE 1..5          → SetPage(cmd - 1)
	//     cmd 6..17  BAR SLOT 1..12     → ActivateSlotByColumn(cmd - 6)        (колонки 0..11)
	//     cmd 18..29 NEW BAR SLOT 1..12 → ActivateSlotByColumn(cmd - 18 + 12)  (колонки 12..23 → слоты 65..76)
	//   На ОТПУСКАНИИ клавиши обязателен `ReleaseSlotByColumn(тот же индекс)` — это сброс
	//   антидребезга (эталон `objClearArraySlot@0xD78410`, массив `CHotbar+0x5FA0[24]`).

	// Активировать слот по «клавишной» колонке 0..23 (эталон `Hotbar_ActivateSlotByIndex@0xDEE650`).
	UFUNCTION(BlueprintCallable, Category="Asda|Hotbar")
	void ActivateSlotByColumn(int32 KeyColumn);

	// Сброс антидребезга по отпусканию клавиши (эталон `objClearArraySlot`).
	UFUNCTION(BlueprintCallable, Category="Asda|Hotbar")
	void ReleaseSlotByColumn(int32 KeyColumn);

	// Сменить страницу 0..4 (эталон `Hotbar_SetPage@0xDEF110` со всеми проверками и отложенным действием).
	UFUNCTION(BlueprintCallable, Category="Asda|Hotbar")
	void SetPage(int32 NewPage);

	// Листание кнопками/колесом (эталон `Hotbar_PageUpDown@0xDEEAD0`): +1 с wrap 5→0, −1 с wrap <0→4.
	UFUNCTION(BlueprintCallable, Category="Asda|Hotbar")
	void PageUpDown(bool bUp);

	UFUNCTION(BlueprintPure, Category="Asda|Hotbar")
	int32 GetCurrentPage() const { return CurrentPage; }

	// ═══════════════════ ПУБЛИЧНАЯ МОДЕЛЬ ═══════════════════
	// Абсолютный индекс слота: основной ряд `col + 13*page`, T-ряд `col + 65`.
	UFUNCTION(BlueprintPure, Category="Asda|Hotbar")
	int32 AbsSlot(int32 Column, bool bTRow) const;

	UFUNCTION(BlueprintPure, Category="Asda|Hotbar")
	const FAsdaHotbarSlot& GetSlot(int32 AbsIndex) const;

	// Положить содержимое в слот (эталон `Hotbar_DragDrop_SetSlot@0xDE9DF0`): запись 10 байт,
	//   пересборка кэша/иконки/тултипа, перерисовка и НЕМЕДЛЕННАЯ отправка страницы (5067).
	UFUNCTION(BlueprintCallable, Category="Asda|Hotbar")
	bool SetSlotContent(int32 AbsIndex, uint8 Type, uint8 SubType, int32 Index, int32 Count, int32 Id);

	// ★★★ ПОЛОЖИТЬ НАВЫК В СЛОТ — порт `Skill_SendLearnOrLevelUp@0xE55FB0` (имя в IDA ошибочно:
	//   функция ничего не «учит», она укладывает НАВЫК в квикслот). Эталон собирает 10-байтовую
	//   запись `{ Type = 3, SubType = 0, Index = 0, Count = 0, Id = skillCode }` и отдаёт её
	//   `Hotbar_DragDrop_SetSlot@0xDE9DF0`, который пишет её ПОВЕРХ содержимого (занятый слот —
	//   ЗАМЕНА, не обмен и не отказ), пересобирает иконку и шлёт страницу пакетом 5067.
	//   ⚠ КОЛОНКА 12 (слот оружия) НАВЫК НЕ ПРИНИМАЕТ: диспетчер `Skill_SendSkillReset@0xE55EA0`
	//   принимает idnum 0x11D33…0x11D3E (73011…73022) и 0x11DC9…0x11DD4 (73161…73172) — на одну
	//   колонку КОРОЧЕ, чем у предмета (там верхняя граница 0x11D3F / 0x11DD5 = слот оружия).
	UFUNCTION(BlueprintCallable, Category="Asda|Hotbar")
	bool AssignSkillToSlot(int32 AbsIndex, int32 SkillCode);
	// ★ 07.11.2026: положить навык Дружбы — порт `hotbarAssignQuickSlot@0xE68E00`: запись `{ Type = 4, Id = навык }` поверх содержимого,
	//   диапазон броска тот же, что у навыка (колонка оружия не принимает).
	bool AssignSoulSkillToSlot(int32 AbsIndex, int32 SkillId);

	// Колонка слота по абсолютному индексу: основной ряд `abs % 13`, T-ряд `abs − 65`.
	UFUNCTION(BlueprintPure, Category="Asda|Hotbar")
	int32 ColumnOfAbsSlot(int32 AbsIndex) const;

	// Очистить слот (эталон `hotbarSlotRemoveAndSync@0xDEA650`) + отправка страницы.
	UFUNCTION(BlueprintCallable, Category="Asda|Hotbar")
	void ClearSlot(int32 AbsIndex);

	// Поменять местами два слота (эталон `CHotbar_SwapSlots@0xDEA470`): страницы обоих уходят на сервер.
	UFUNCTION(BlueprintCallable, Category="Asda|Hotbar")
	void SwapSlots(int32 AbsA, int32 AbsB);

	// Применить содержимое слота (эталон `Hotbar_SlotActivate@0xDEDDD0` со всеми блокировками).
	UFUNCTION(BlueprintCallable, Category="Asda|Hotbar")
	void ActivateSlot(int32 AbsIndex);
	// Самотест (волна В3 ремесла): спросить эталонные гейты `Hotbar_SlotActivate` без применения слота — тот же
	//   `IsActivationBlocked`, что стоит первой строкой `ActivateSlot` (закон `diagnostic-must-use-real-path`).
	bool IsActivationBlockedForCheck(FString& OutWhy) const { return IsActivationBlocked(OutWhy); }

	// Полная пересборка кэша всех 78 слотов из живых данных (эталон `hotbarRebuildSlots@0xDEC380`).
	UFUNCTION(BlueprintCallable, Category="Asda|Hotbar")
	void RebuildSlots();

	// Заново применить оформление счётчика стака ко всем 25 ячейкам (после правки кегля/обводки).
	UFUNCTION(BlueprintCallable, Category="Asda|Hotbar")
	void RefreshStackTextStyle();

	// Диагностика: во что разрешается иконка для типа/идентификатора (эмоция, навык, навык соулмейта).
	FString ProbeIcon(int32 Type, int32 IdOrCode) const;

	// Пересобрать ПОДПИСИ КЛАВИШ на слотах из текущего конфига (эталон `hotkeyBar_RebuildAndRefresh@0xDF5BC0`).
	//   ⚠ Только подписи: обработку самих нажатий делает система клавиш.
	UFUNCTION(BlueprintCallable, Category="Asda|Hotbar")
	void RebuildKeyHints();
	// Таблица «ряд → колонка → слот → команда Shortcutkey.ini → клавиша» (`asda.Quick.KeyMap`).
	UFUNCTION(BlueprintCallable, Category="Asda|Hotbar")
	FString DumpKeyMap() const;

	// Перерисовать видимую страницу (эталон `CHotbar_RefreshPage@0xDEA6F0`) + T-ряд.
	UFUNCTION(BlueprintCallable, Category="Asda|Hotbar")
	void RefreshPage();

	// ★ 27.09.2026 (pkt4024_facing.md §6): порт `CHotbar_UpdateAnimSlots@0xDF6980` — перерисовать 13 слотов ТЕКУЩЕЙ
	//   страницы (T-ряд эталон здесь не трогает); иконка `act_updown`/`act_standup` выбирается по состоянию
	//   персонажа в `SitStandIconName`. Зовёт `AAsdaPlayerController::ToggleSitStand`.
	void UpdateSitStandIcons();
	// Правило всех отрисовок хотбара эталона (`CHotbar_RefreshPage`/`UpdateSlot`/`RefreshSlots`/`RefreshSlotByType`/
	//   `UpdateAnimSlots`, ui_core.c:66415/66544/66694/66848/69755): имя `act_updown` или `act_standup` → при состоянии
	//   своего персонажа 7 или 8 `act_standup`, иначе `act_updown`; прочие имена — как есть. При
	//   `asda.Char.SitToggleEtalon 0` — имя как есть (прежний порт).
	FString SitStandIconName(const FString& Tag) const;

	// ★ Абсолютный индекс слота ПОД КУРСОРОМ (−1 = курсор не над слотом).
	//   Нужен ИНВЕНТАРЮ: эталон ловит бросок предмета в квикслот диапазонами idnum 73011…73023 и
	//   73161…73173 внутри `inventoryWin_OnUiCommand@0xD17A80` → `hotbarAssignInventoryItem@0xD11840`.
	//   У нас мышь опрашивается, поэтому «диапазон idnum под курсором» = этот хит-тест.
	UFUNCTION(BlueprintPure, Category="Asda|Hotbar")
	int32 HitTestSlotUnderCursor() const;

	/** ★ ЖИВОЙ ПРЯМОУГОЛЬНИК ИКОНКИ СЛОТА на экране (false — слот не показан).
	 *  ЗАЧЕМ. Перетаскивание нельзя проверить «по числам»: нужен путь игрока — нажатие, движение,
	 *  отпускание НАД СЛОТОМ (`asda.UI.SimDrag`). Координату цели брать больше неоткуда: слот
	 *  канваса про абсолютное положение врёт (закон `jui-cell-chain-nesting`), а сама панель
	 *  живёт в отдельном окне. Отдаём ту же геометрию, по которой считает `HitTestSlot`.
	 *  ⚠ Слот основного ряда виден, только если его страница СЕЙЧАС открыта (`AbsIndex / 13 ==
	 *  CurrentPage`); T-ряд — только когда он показан. Иначе false. */
	bool GetSlotIconAbsRect(int32 AbsIndex, FVector2D& OutMin, FVector2D& OutMax) const;

	// ═══════════════════ БЛОКИРОВКА / ВИДИМОСТЬ ═══════════════════
	// Эталон `uiHidePanelControls@0xDE9C60` (снять затемнение = хотбар АКТИВЕН) /
	//        `uiShowPanelControls@0xDE9D30` (затемнить = хотбар ЗАБЛОКИРОВАН).
	UFUNCTION(BlueprintCallable, Category="Asda|Hotbar")
	void SetPanelActive(bool bActive);

	UFUNCTION(BlueprintPure, Category="Asda|Hotbar")
	bool IsPanelActive() const { return bPanelActive; }

	// Опция «показывать второй ряд» (эталон `uiToggleChildPairVisible@0xDE9A40` + `g_bOptChildPairVisible`).
	UFUNCTION(BlueprintCallable, Category="Asda|Hotbar")
	void SetSecondRowVisible(bool bVisible);

	UFUNCTION(BlueprintPure, Category="Asda|Hotbar")
	bool IsSecondRowVisible() const { return bSecondRowVisible; }

	// ═══════════════════ ПЕРЕЗАРЯДКА ═══════════════════
	// ★★★ ВЗВОД ОТКАТА ПРЕДМЕТА — полная эталонная сигнатура `hotbarPushItemCooldown@0xE48FD0`
	//   `(hotbar, itemCode, containerType, slotIndex)`. ⚠ Имена параметров в декомпиле ВРУТ
	//   (`colorType` = containerType, `duration` = slotIndex) — сигнатура снята по вызовам из
	//   `OnRecv_UseItemResult@0xD0B1E0` (item_inventory.c:39089/39093/39120/39152).
	//   Длительность и группу подсистема берёт САМА из записи предмета (`UAsdaItemTableSubsystem::
	//   GetItemCooldown` = гейт `itemSubType ∈ {15,16} || linkCode ∈ {7,8,200,201}` + `u16 @0xD0`).
	//   Ничего не делает, если гейт не прошёл или длительность нулевая — ровно как эталон.
	UFUNCTION(BlueprintCallable, Category="Asda|Hotbar")
	void PushItemCooldown(int32 ItemCode, int32 ContainerType, int32 SlotIndex);

	// Взвод отката НАПРЯМУЮ, без чтения таблицы (консольная проверка `asda.Quick.Cool` и офлайн-эмуляция).
	//   `Group` = 0 ⇒ откат ляжет только на этот код+контейнер, как в клиенте оригинала.
	UFUNCTION(BlueprintCallable, Category="Asda|Hotbar")
	void PushItemCooldownRaw(int32 ItemCode, int32 ContainerType, int32 SlotIndex, int32 Group, int32 DurationMs);

	// ★★★ ПРЕД-ПРОВЕРКА ПЕРЕД ОТПРАВКОЙ 5028 — порт `lookupClassStatusByKey@0xE4A0C0`
	//   (`Item_UseSelectedAndSend@0xD0A060` отказывает при результате <= 1 с нотисом 9565).
	//   Возвращает 3, если записи об откате нет (применять МОЖНО), иначе состояние 0/1/2.
	UFUNCTION(BlueprintPure, Category="Asda|Hotbar")
	int32 GetItemCooldownState(int32 ItemCode, int32 ContainerType, int32 SlotIndex) const;

	// Остаток отката предмета в секундах (0 = готов). Нужен диагностике и нотису «жди отката».
	UFUNCTION(BlueprintPure, Category="Asda|Hotbar")
	float GetItemCooldownRemainSec(int32 ItemCode, int32 ContainerType, int32 SlotIndex) const;

	// ★★★ ВОССТАНОВЛЕНИЕ ОТКАТОВ ПРЕМИУМ-ПРЕДМЕТОВ С СЕРВЕРА — порт ВТОРОГО цикла
	//   `Recv_SkillCoolTime@0xE4B180` (`network.c:28806-28832`), блок 5465 со смещения +180.
	//   На вход — ровно этот блок (до 30 записей по 6 б: @0 i32 код, @4 i16 остаток В СЕКУНДАХ).
	//   Полная длительность в пакет НЕ входит: эталон берёт её из `SpecialItemRecord.cooldownBase`,
	//   а долю пройденного считает как `(cooldownBase − остаток*1000) / cooldownBase * 100`.
	void ApplyServerItemCooldowns(const TArray<uint8>& Block30x6);

	// ★ Снять откат предмета досрочно по коду — приём 5452 (`recvItemRemoveById@0xF34B30`).
	//   Сервер шлёт 5452 ровно через `CooldownBase` мс после применения (доказано хронометражем
	//   живого лога 14.08: 123 → 30.0 с, 465 → 1.8 с, 462 → 2.0 с, 464 → 1.8 с), то есть это
	//   АВТОРИТЕТНОЕ «откат кончился». Синхронизируем свой таймер с сервером.
	UFUNCTION(BlueprintCallable, Category="Asda|Hotbar")
	void ClearItemCooldownByCode(int32 ItemCode);

	// Поставить перезарядку навыку (Type 3/4 адресуются skillId).
	UFUNCTION(BlueprintCallable, Category="Asda|Hotbar")
	void PushSkillCooldown(int32 SkillId, int32 DurationMs);

	// Реестр перезарядок целиком (эталон держит его в CSkillWnd — здесь до появления книги навыков).
	UFUNCTION(BlueprintCallable, Category="Asda|Hotbar")
	void ClearAllCooldowns();
	// ★ 19.10.2026 (GM-команда 6058): сбросить откаты НАВЫКОВ панели, не трогая откаты предметов (у эталона они в другом
	//   векторе `CHotbar+56` и 6058 их не касается), и перерисовать страницу (`CHotbar_RefreshPage`).
	void ClearSkillCooldowns();

	// ═════════ ★★★ ОБЩАЯ ТОЧКА: «ПОКАЖИ ОТКАТ» ДЛЯ ЛЮБОГО ОКНА (закон `port-shared-points`) ════════
	//
	// ЗАЧЕМ. С 2026-08-14 шторку рисует не только панель, но и ячейки инвентаря/снаряжения и узлы
	//   дерева навыков (решение юзера, см. `IsCooldownShownEverywhere`). Новые места показа обязаны
	//   спрашивать ЭТОТ реестр, а не заводить свой счётчик: реестр откатов предметов (`ItemCooldowns`,
	//   он же вектор `hotbar+56` эталона) — один, и живёт здесь.
	//
	// Возврат — та же 20-байтовая структура, что отдаёт реестр `CSkillWnd` эталона:
	//   `State` 0 = готово / 1 = идёт откат (`RemainMs` — ОСТАТОК в мс) / 2 = замороженный кадр;
	//   `IconFrame` — кадр, с которого начинать прогон (доля УЖЕ пройденного отката).

	// Откат ПРЕДМЕТА по коду и контейнеру. `Container`: 1 = снаряжение/квестовые (ShellInven),
	//   2 = расходники (SpendCellobInven), 3 = надетое, −1 = «любой» (контейнер неизвестен).
	//   Поиск идёт по ГРУППЕ отката (правило и сервера, и клиента — `itemColorTypeMatch@0xDF67C0`),
	//   поэтому «другое зелье той же группы» тоже показывается в откате.
	UFUNCTION(BlueprintPure, Category="Asda|Hotbar")
	FAsdaHotbarCooldown QueryItemCooldown(int32 ItemCode, int32 Container) const;

	// Откат НАВЫКА по коду. Авторитет — книга навыков (её реестр = вектор `CSkillWnd+56` эталона),
	//   если она зарегистрировала мост; иначе локальный `SkillCooldowns` (панель до появления книги).
	//   ⚠ Книга навыков зовёт ЭТОТ метод, а он возвращается в неё через мост — это НЕ рекурсия:
	//   лямбда моста читает свою `TMap` напрямую. Так у всех окон один вход и один ответ.
	UFUNCTION(BlueprintPure, Category="Asda|Hotbar")
	FAsdaHotbarCooldown QuerySkillCooldown(int32 SkillId) const;

	// ★ Ключ `asda.Cooldown.ShowEverywhere`: 1 (умолчание) — шторка везде, 0 — как в оригинале.
	//   Что именно значит «как в оригинале» для каждого окна — расписано у самого ключа в .cpp.
	static bool IsCooldownShownEverywhere();

	// План-ОБРАЗЕЦ шторки (`QUICK_NEW_COOL1`, скин `CoolTime_Effect`). Отсюда чужие окна берут ВЕСЬ
	//   визуал — атлас, размер кадра, число кадров, UV кадра 0, Diffuse, — чтобы не выдумывать свой
	//   (закон `no-invented-ui`). nullptr = панель ещё не построена в этом кадре.
	const class UAsdaAnimatedPlane* GetCurtainTemplate() const;

	// Создать шторку НАД ЯЧЕЙКОЙ окна, у которого своего контрола шторки в .JUI нет (инвентарь).
	//   Кладётся в тот же канвас и на тот же прямоугольник, что `CellIcon`, z = z(иконки) + 1.
	//   Визуал целиком копируется с `GetCurtainTemplate()`. Перегенерация WBP при этом НЕ нужна
	//   (тот же приём, что у подписей ячеек панели состава пакета). nullptr = образца ещё нет.
	class UAsdaAnimatedPlane* MakeCurtainOverCell(UUserWidget* Host, UWidget* CellIcon, const TCHAR* NameHint) const;

	// ★★★ ЕДИНСТВЕННАЯ РЕАЛИЗАЦИЯ ОТРИСОВКИ ШТОРКИ. Порт `hudSlotUpdateCooldown@0xDF23D0` /
	//   `hotbarUpdateCooldownVisual@0xDF2440` / `uiQuickSlotUpdateCooldownDisplay@0xDF24F0` —
	//   все три одинаковы по схеме:
	//     state==1 && remain>0 → uiControl_ShowWithItem(ctrl, кадр, 117, 118, (u64)remainMs, animate=1)
	//     state==2             → uiControl_ShowWithItem(ctrl, кадр, 127,   8,          1000, animate=0)
	//     иначе                → CControl_CloseHide(ctrl)
	//   `Key` — что показано в ячейке (слот панели / код предмета / код навыка): смена ключа
	//   перезапускает прогон. `NowS` — время мира в секундах (у всех вызывающих оно уже под рукой).
	//   Возврат: true — шторка сейчас показана.
	bool DriveCurtain(FAsdaCooldownCurtain& C, int32 Key, const FAsdaHotbarCooldown& CD, double NowS) const;

	// ═══════════════════ ИКОНКИ УВЕДОМЛЕНИЙ (P_ICON00..06) ═══════════════════
	//   Эталон `statusBarAddIcon@0xDF3F80` / `MsgNotify_TypeToIcon@0xDF4450` / `uiNoticeButtonDispatch@0xDF4F10`.
	// ★ 06.10.2026 (ревизия 2-В, М-4): true — значку нашёлся СВОБОДНЫЙ слот (в эталоне только тогда взводятся флаг и таймер типа 14).
	UFUNCTION(BlueprintCallable, Category="Asda|Hotbar")
	bool AddNoticeIcon(int32 NoticeType);
	UFUNCTION(BlueprintCallable, Category="Asda|Hotbar")
	void RemoveNoticeIcon(int32 NoticeType);
	UFUNCTION(BlueprintCallable, Category="Asda|Hotbar")
	void ClearNoticeIcons();

	// ★ Снять ВСЕ слоты навыков соулмейта (Type = 4) и пересобрать кэш — порт `hotbarClearType4Slots`
	//   + `hotbarRebuildSlots` эталона. Зовётся при разрыве связи (5207 код 3): навыки Дружбы
	//   становятся недоступны, их слоты обязаны опустеть. Возвращает число снятых слотов.
	UFUNCTION(BlueprintCallable, Category="Asda|Hotbar")
	int32 ClearSoulmateSkillSlots();
	// Клик по иконке уведомления: окна-получатели (пати/соулмейт/магазин/обмен) ещё не портированы,
	//   поэтому событие уходит делегатом и в системный журнал.
	//   ★ Тип 15 (элитный почтовый ящик) подписчика УЖЕ имеет — `UAsdaPremiumPostSubsystem`.
	DECLARE_MULTICAST_DELEGATE_OneParam(FOnAsdaHotbarNotice, int32 /*NoticeType*/);
	FOnAsdaHotbarNotice OnNoticeIconClicked;
	// ★ 29.09.2026 (волна О2 торговли, ревизия О1 МЕЛК-4): тело клика по значку уведомления типа `Type` — ТОТ ЖЕ путь, что
	//   у `TickNoticeClick` после хит-теста рамки (`uiNoticeButtonHandler@0xDF4DB0`: гейт 5042 для 4/5 при открытом окне обмена,
	//   делегат, снятие значка). Нужен самотестам: мышью в -game без окна не кликнуть, а второй путь клика заводить нельзя.
	//   false — значка такого типа нет.
	bool HandleNoticeIconClick(int32 Type);
	// Висит ли сейчас иконка такого типа (нужно владельцам уведомлений, чтобы не дублировать показ).
	UFUNCTION(BlueprintPure, Category="Asda|Hotbar")
	bool HasNoticeIcon(int32 NoticeType) const;

	/** ★ Дамп семи слотов уведомлений: тип, видимость ГРУППЫ/иконки/рамки, альфа, экранный
	 *  прямоугольник. Команда `asda.Quick.NoticeDump` — живая проверка жалобы «иконки не видно». */
	FString DumpNoticeIcons();

	// ═══════════════════ МОСТЫ К БУДУЩИМ СИСТЕМАМ ═══════════════════
	void SetInventoryBridge(const FAsdaHotbarInventoryBridge& In) { Inventory = In; RebuildSlots(); RefreshPage(); }
	// Мост к окну навыков ставится один раз (см. Tick) — флаг не даёт перетирать модель каждый кадр.
	bool bSkillBridgeBound = false;

	void SetSkillBridge(const FAsdaHotbarSkillBridge& In)         { Skills = In;    RebuildSlots(); RefreshPage(); }
	void SetMiscBridge(const FAsdaHotbarMiscBridge& In)           { Misc = In;      RebuildSlots(); RefreshPage(); }
	// ★ ЧТЕНИЕ моста — чтобы владельцы РАЗНЫХ его полей не затирали друг друга.
	//   Поля моста заполняют ДВЕ подсистемы: эмоции — `UAsdaCharaInfoSubsystem`,
	//   пункты доп-меню — `UAsdaExtraMenuSubsystem`. Обе обязаны делать read-modify-write
	//   (`FAsdaHotbarMiscBridge B = Hb->GetMiscBridge(); B.<своё> = …; Hb->SetMiscBridge(B);`),
	//   иначе та, что подключилась позже, обнуляла бы чужие лямбды — и слоты соответствующего
	//   типа молча переставали работать.
	const FAsdaHotbarMiscBridge& GetMiscBridge() const            { return Misc; }
	// ★★★ ОБЩАЯ ТОЧКА ТАБЛИЦЫ ЭМОЦИЙ (`emotiontip.bin`). Списки строит и держит ЭТА подсистема —
	//   окно характеристик (вкладка эмоций, `UAsdaCharaInfoSubsystem`) берёт иконки и подсказки
	//   ОТСЮДА, а не заводит второй разбор той же таблицы.
	bool ResolveEmotionIcon(int32 Code, FString& OutTex, bool& bOutFullSize) const;
	bool ResolveEmotionTip(int32 Code, FString& OutText) const;
	// Сколько записей реально загрузилось в группу 0/1/2 (у эталона слотов в лотке БОЛЬШЕ, чем записей).
	int32 EmotionCountInGroup(int32 Group) const;
	// Есть ли у игрока соулмейт — от этого зависит серый суффикс `_g` у трёх «парных» действий.
	bool HasSoulmate() const;
	// ★ 06.11.2026 [ИСПРАВЛЕНИЕ] (ключ `asda.Soul.SlotTruth`): Друг В ИГРЕ — слот пары есть и `gap_8B4[6] == 1` (запись @25). От этого, а не
	//   от наличия пары, у эталона зависят серый `-g` навыков Дружбы (`hotbarRebuildSlots`) и отказ 8162 (`Hotbar_SlotActivate` case 4).
	bool IsFriendInGame() const;
#if !UE_BUILD_SHIPPING
	// Самотест `asda.Soulmate.OfflineGateSelfTest`: имя иконки навыка Дружбы (с `-g` или без).
	FString DebugSoulSkillIcon(int32 SkillId) const { FString T; ResolveSoulSkillIcon(SkillId, T); return T; }
#endif


	// ═══════════════════ ДИАГНОСТИКА ═══════════════════
	// Многострочный дамп КАЖДОГО разобранного контрола (закон `full-structural-analysis-always`).
	FString DumpControls() const;
	// Дамп модели: 78 слотов, страница, состояние перезарядок.
	FString DumpSlots() const;
	// ★ 17.10.2026 Самотест иконок эмоций (`asda.Quick.SelfTestEmoteIcons`): каждую эмоцию всех трёх списков кладёт в слот
	//   (имя из таблицы и из моста окна эмоций), проверяет размер кроя 64/32 и UV кисти (0,0)–(1,1). Слот и мост восстанавливаются,
	//   сеть не трогается. → многострочный отчёт с последней строкой «ИТОГ: ok=N fail=M».
	FString SelfTestEmoteIcons();
	// ★ Состояние ШТОРОК панели (`asda.Cooldown.Dump`): по одной строке на горящую колонку.
	//   Нужен сверке «шторка зажглась/погасла ОДНОВРЕМЕННО в трёх окнах» — у инвентаря и книги
	//   навыков есть свой такой же метод, команда печатает все три подряд.
	FString DumpCooldownCurtains() const;
	// Эмуляция прихода 5059 (одна страница) из консоли.
	void SimulateLoadPage(int32 Page, const TArray<uint8>& Body130);

private:
	// ── Клавиши. Контроллер (`AAsdaPlayerController`) шлёт ДИНАМИЧЕСКИЕ делегаты `OnHotbarPage` /
	//   `OnHotbarSlot`; подписываемся лениво в `Tick`, когда контроллер появился.
	//   ⚠ У контроллера НЕТ события отпускания: `PollKeys` стреляет РОВНО ОДИН раз по фронту.
	//   Поэтому защёлку `KeyLatch` (эталон `CHotbar+0x5FA0[24]`) снимаем раз в кадр в `Tick`,
	//   а не по отпусканию — иначе слот срабатывал бы один-единственный раз за сессию.
	UFUNCTION() void OnKeyHotbarPage(int32 PageIndex);
	UFUNCTION() void OnKeyHotbarSlot(int32 Column);
	void EnsureKeyBinding();
	// Подписи клавиш собираются, КОГДА появилась раскладка (контроллер поднимается позже подсистемы),
	//   и пересобираются при её перечитывании с диска. Ревизия — дешёвый слепок источника раскладки.
	bool   bKeyHintsBound   = false;
	uint32 KeyHintsRevision = 0;
	// ★ R4-b 14.11.2026 (B6): хэш пути раскладки считается заново, только когда сам путь изменился (сверка байтов).
	FString HintPathCache;          // путь раскладки, по которому посчитан хэш
	uint32  HintPathHash = 0;       // `GetTypeHash(HintPathCache)`
	bool    bHintPathCached = false; // кэш заполнен (первый вызов считает хэш всегда)


	// ── Жизненный цикл окна ──
	bool EnsureWidget();
	void ResetWidget();
	void SetupControls();

	// ── Сеть ──
	void OnHotbarPageReceived(uint8 Page, const TArray<uint8>& Raw130);   // колбэк пакета: ТОЛЬКО в очередь
	void ApplyPendingPages();                                            // выполняется в Tick
	void SendPageLayout(int32 Page);                                     // 5067

public:
	// ★ Сохранить ВСЕ страницы разом. Оригинал (`hotbar_SendAllLayouts`) делает это перед каждым
	//   переходом — портал 5055, призыв 6013, смена канала 6015, короткий варп 6201: иначе слоты,
	//   переставленные после последнего сохранения, теряются при разрыве соединения.
	//   Страницы, не менявшиеся с прошлой отправки, `SendPageLayout` пропускает сам.
	void SendAllPageLayouts();

	// ★ Слот панели следует за перенесённым предметом (эталон `hotbarSetSlotsHighlight@0xDF1360` и `hotbarSetSlotHighlightByKey@0xDF11C0`,
	//   зовутся из `itemMoveApply@0xD607C0`). Звать ДО перестановки ячеек. `bSwap` — обмен: слоты цели уходят на ячейку источника.
	void FollowItemMove(int32 SrcSubType, int32 SrcCell, int32 DstSubType, int32 DstCell, bool bSwap);

private:

	// ── Рендер ──
	void RefreshRow(bool bTRow);
	void UpdateSlotVisual(int32 Column, bool bTRow);
	void UpdateCooldownVisual(int32 Column, bool bTRow);
	// ★ R4-b 14.11.2026 (B5): тело `UpdateCooldownVisual` с миром и часами реестра, взятыми раз за кадр вызывающим.
	void UpdateCooldownVisualAt(int32 Column, bool bTRow, const UWorld* World, double Now);
	bool ApplyIconToImage(UImage* Img, const FAsdaHotbarSlot& S) const;

	// ── Иконки уведомлений (`P_ICON00..06` / `P_ICONSEL00..06`) ──
	/** Порт `buffSlotCompact` + `MsgNotify_RefreshIcons`: сдвинуть оставшиеся уведомления влево. */
	void CompactNoticeIcons();
	/** Порт `uiControlPairToggleBlink@0xDF65B0` (таймеры 18…24, период 500 мс): пульсация alpha. */
	void TickNoticeBlink(float DeltaTime);
	/** Порт `uiNoticeButtonHandler@0xDF4DB0`: клик по рамке `P_ICONSEL` → делегат + снятие. */
	bool TickNoticeClick();
	void UpdatePageDigits();

	// ★★★ ОФОРМЛЕНИЕ СЧЁТЧИКА СТАКА. Счётчик — это ТЕКСТ самого контрола-иконки, поэтому его вид
	//   задаёт блок `EXS` этого контрола в .JUI: `TextPos = "0 0"`, `TextColor = FFFFFFFF`,
	//   **`TextAlign = "9"`**.
	//   Маска 9 = 0x1 (hCenter) | 0x8 (vBottom) ⇒ по горизонтали ЦЕНТР, по вертикали НИЗ ячейки.
	//   ★ Дефект импорта (Д8): генератор взял маску из `CaptionInfo` («0 0 ff400040 **5**» = центр+центр)
	//   вместо `EXS TextAlign` — ровно та же подмена, что была с ЦВЕТОМ (закон `ui-text-color-exs`).
	//   Доказательство из ассета: слот-иконка лежит в группе на (16,16) 32×32, а TextBlock — на
	//   (16, **25**), т.е. смещён на (32−14)/2 = 9 (формула ветки vCenter), а должен быть прижат к низу.
	//   Чиним в рантайме: перегенерация WBP запрещена.
	//   ★ ОБВОДКА: эталон её СТАВИТ (поле 926 = 0x800000, ui_core.c:65971) — с 26.10.2026 её даёт
	//   `AsdaItemCount::ApplyOutline` (см. поправку у `StackOutlinePx` ниже).
	void ApplyStackTextStyle(class UTextBlock* Tb, const FAsdaHotbarSlotUi& Ui) const;

	// ── Иконки типов 2/3/4 БЕЗ окон эмоций и книги навыков: имя текстуры берётся ПРЯМО ИЗ ТАБЛИЦ ──
	//   Эмоции: `emotiontip.bin` (265 б/запись), иконка = строка @+32 (колонка `Tag`), группа = байт @+64
	//     (колонка `Type`). Эталон `EmotionTip_Load@0xC529D0` раскладывает записи по ТРЁМ спискам, а
	//     `emotionTip_lookupByCode@0xC52870` адресует их кодом: 0..99 → список 0, 100..199 → список 1
	//     (индекс `code%100`), 200..299 → список 2 (индекс `code%200`).
	void BuildEmotionLists();
	// ★ Подсказка ЭМОЦИИ. Эталон `uiSlotHoverTooltip` case 2 → `CControl_SetState(&g_ItemTooltip, 3, code…)`
	//   → `uiResolveLinkTooltip` case 3 → `emotionTip_lookupByCode@0xC52870` → `tooltipShowAtCursor(rec)`,
	//   то есть текст = строка в НАЧАЛЕ 265-байтной записи `EmotionTip.BIN` (в порту колонка `Name`).
	//   Раскладка по трём спискам — ровно та же, что у иконки (`ResolveEmotionIcon`).
	//   Навык (Type 3): `Id` слота = ИНДЕКС записи в `skilltooltip.bin` (504 б; эталон
	//     `uiGetRecordByIndex@0xE52930` — обращение по индексу, не по коду), иконка = ASCII @+36.
	bool ResolveSkillIcon(int32 RecordIdx, FString& OutTex) const;
	//   Навык соулмейта (Type 4): `Id` = skillId, поиск по `record[0]` в `soulmateskilltable.bin`
	//     (272 б; эталон `skillFindEntryById@0xE52980`), иконка = @+36 (колонка `SkillName`).
	bool ResolveSoulSkillIcon(int32 SkillId, FString& OutTex) const;

	// ── Модель ──
	void RebuildOneSlot(int32 AbsIndex);
	FAsdaHotbarCooldown FindCooldown(const FAsdaHotbarSlot& S) const;
	// Остаток+длительность → эталонная 20-байтовая структура состояния (одна формула стартового кадра).
	FAsdaHotbarCooldown MakeCooldownState(double RemainS, double TotalS) const;
	// Локальный реестр перезарядок панели (без обращения к мосту книги навыков).
	FAsdaHotbarCooldown LocalSkillCooldown(int32 Id) const;
	bool SlotItemIsWeapon(int32 AbsIndex) const;   // эталон `hotbarSlotItemIsType9@0xDF2730`
	void UseActiveWeaponSlot();                    // эталон `hotbarUseActiveSlotItem@0xDF2670`
	bool IsActivationBlocked(FString& OutWhy) const;

	// ── Ввод (события 5/6/7/8/9/0xA/0xD эталонного `hotbarOnUiEvent`) ──
	void TickPointer();
	bool IsCursorOver(const UWidget* W) const;
	int32 HitTestSlot(bool& bOutTRow, int32& OutColumn) const;   // абс. индекс под курсором, −1 если нет
	void BeginDrag(int32 AbsIndex);
	void EndDrag(int32 AbsTarget, bool bTargetTRow, int32 TargetColumn);
	void CancelDragVisual();
	// «Призрак» перетаскивания: тот же браш, что у иконки слота, тянется за курсором с alpha 40 %.
	//   Эталон рисует ту же плоскость у курсора движком (FillMode 5, см. отчёт); у нас — явный UImage.
	void UpdateDragGhost();
	void ShowSlotTooltip(int32 AbsIndex);
	void HideSlotTooltip();

	// ── Отложенное действие персонажа (эталон `sub_DEF3A0`, состояние 104/0x68) ──
	void QueuePageAction(int32 Page, int32 TargetRenderId);
	void TickPageAction();

	// ── Помощники ──
	static void SetVis(UWidget* W, bool bVisible);
	static bool GetSlotRect(const UWidget* W, FVector2D& OutPos, FVector2D& OutSize);
	static void SetSlotPos(UWidget* W, const FVector2D& Pos);
	// Клон плана-картинки. Dest = канвас-родитель клона; nullptr → канвас исходника.
	//   ⚠ Координаты в UMG ЛОКАЛЬНЫ для канваса, поэтому клон, который должен встать на слот,
	//   обязан лежать В ТОМ ЖЕ канвасе, где считается его позиция (эталон: родитель = сам слот).
	UImage* CloneImage(UImage* Src, const TCHAR* NameHint, class UPanelWidget* Dest = nullptr);
	void Notice(int32 LocId) const;   // [ПОРТ] 09.10.2026 (мультиязычность, этап 3): без RU-фолбэка (нет строки → «[#id]»)

	// ══════════════════ ДАННЫЕ ══════════════════
	UPROPERTY(Transient) TObjectPtr<UUserWidget> QuickWidget;

	// Летящий за курсором «призрак» перетаскиваемой иконки (создаётся лениво в корневом канвасе).
	//   ★ Аудит №3 (B4): создание/слежение/гашение живут в общей точке `FAsdaDragGhost` — раньше это
	//   был один из ТРЁХ побайтово похожих экземпляров (панель, инвентарь, книга навыков).
	UPROPERTY(Transient) FAsdaDragGhost DragGhost;

	// Модель 78 слотов (эталон `aSlotData`).
	UPROPERTY(Transient) TArray<FAsdaHotbarSlot> Slots;
	// Снимок последней ОТПРАВЛЕННОЙ раскладки страницы — чтобы не слать 5067 без изменений.
	UPROPERTY(Transient) TArray<FAsdaHotbarSlot> SentSnapshot;

	// Виджеты: 13 колонок основного ряда (12 обычных + оружие) и 12 колонок T-ряда.
	UPROPERTY(Transient) TArray<FAsdaHotbarSlotUi> MainUi;
	UPROPERTY(Transient) TArray<FAsdaHotbarSlotUi> TUi;

	UPROPERTY(Transient) TArray<TObjectPtr<UImage>>  PageDigits;      // QUICK_NEW_NUM1..NUM5 (+NUM6, не грузится)
	UPROPERTY(Transient) TObjectPtr<UImage>          PageDigit6 = nullptr;
	UPROPERTY(Transient) TObjectPtr<UButton>         BtnPageUp = nullptr;    // 73327
	UPROPERTY(Transient) TObjectPtr<UButton>         BtnPageDown = nullptr;  // 73328
	UPROPERTY(Transient) TObjectPtr<UWidget>         RowMainBase = nullptr;  // QUICK_NEW_SlotBtnBase_1 (73030)
	UPROPERTY(Transient) TObjectPtr<UWidget>         RowTBase = nullptr;     // QUICK_NEW_SlotBtnBase_2 (73160)
	UPROPERTY(Transient) TObjectPtr<UWidget>         RowTAnchor = nullptr;   // QUICK_NEW_SlotBase_2 (73195)
	UPROPERTY(Transient) TObjectPtr<UWidget>         KeysBase1 = nullptr;    // Quick_Keys_Base_1 (73070)
	UPROPERTY(Transient) TObjectPtr<UWidget>         KeysBase2 = nullptr;    // Quick_Keys_Base_2 (73220)
	// Таблица глифов клавиш ряда 1 (65 шт, 73080..73144) — ключ = имя скина `QSlot_*` без префикса.
	UPROPERTY(Transient) TMap<FString, TObjectPtr<UImage>> KeyGlyphs;
	UPROPERTY(Transient) TArray<FAsdaHotbarNoticeUi> NoticeUi;
	UPROPERTY(Transient) TArray<TObjectPtr<UAsdaButtonRelay>> Relays;

	// ── Состояние ──
	int32 CurrentPage = 0;                 // CHotbar+0x514
	int32 PrevPage = 0;                    // CHotbar+0x518
	bool  bPanelActive = true;             // CHotbar+0x524 bPanelHidden (ctor = 1)
	bool  bSecondRowVisible = true;        // g_bOptChildPairVisible
	bool  bControlsReady = false;
	int32 WidgetGeneration = 0;

	// Драг (эталон nSwapCol/nSwapSlot @+0x684/+0x688, ctor = −1; bDragActive @+0x528).
	int32 DragSrcSlot = -1;
	int32 DragSrcColumn = -1;
	bool  bDragSrcTRow = false;
	bool  bDragActive = false;

	// ★★★ ВЗВОД ЗАХВАТА — общая точка `FAsdaDragArm` (`AsdaUiHelpers.h`), одна на три окна.
	//   `KeyA` = АБСОЛЮТНЫЙ номер слота, `KeyB` не используется (−1). Пока взведено, но порог не
	//   пройден, отпускание ЛКМ над тем же слотом = ОДИНОЧНОЕ применение (решение юзера).
	//   Порог — отступление от эталона: оригинал (`CControl_OnLButtonDown@0x12DB870`) начинает
	//   захват тем же кадром, что и нажатие. Разбор: `Reports/icon_drag_threshold.md`.
	FAsdaDragArm DragArm;
	// ★ Аудит №3 (B3): прежний латч `bLmbWasDown` — теперь общая точка. Экземпляр ОДИН: опрос у
	//   панели ровно один (`TickPointer`), и его ранние выходы (модалка, чужой слой поверх, панель
	//   заблокирована) обязаны латч НЕ трогать — поэтому `Update()` стоит там же, где стоял опрос.
	FAsdaMouseFrame Mouse;
	FString DragSavedStackText;            // CHotbar+0x52C

	int32 HoverSlot = -1;                  // CHotbar+0x54C
	bool  bTipShown = false;
	// ★ От чьего имени сейчас показана ОБЩАЯ плашка (см. `EAsdaTipOwner`): у предмета показ уходит
	//   в инвентарь (владелец `Item`), у навыка/эмоции — свой (`Hotbar`). Гасить надо того же.
	EAsdaTipOwner TipOwner = EAsdaTipOwner::None;

	// Антидребезг клавиш (эталон `aSlotData + 22788`, массив 24 dword по адресу CHotbar+0x5FA0:
	//   `Hotbar_ActivateSlotByIndex` ставит 1, `objClearArraySlot` по отпусканию сбрасывает).
	//   ★ У НАС отпускания нет как события (`PollKeys` даёт один импульс по фронту) — защёлка
	//   снимается в начале следующего кадра в `Tick`, см. комментарий там.
	TArray<uint8> KeyLatch;


	// Реестр перезарядок. В оригинале он живёт в `CSkillWnd`; книги навыков в порту ещё нет,
	//   поэтому держим его здесь. Появится книга — она регистрирует `Skills.GetCooldown`,
	//   и этот реестр остаётся только для предметов.
	struct FCooldownRec { double EndS = 0.0; double TotalS = 0.0; };
	TMap<int32, FCooldownRec> SkillCooldowns;   // ключ = skillId

	// ★★★ ОТКАТЫ ПРЕДМЕТОВ — не `TMap<itemCode>`, а СПИСОК записей, как вектор `hotbar+56` эталона.
	//   Эталонная запись (`hotbarPushItemCooldown@0xE48FD0`): +0 kind=2 (предмет), +12 itemCode,
	//   +16 containerType, +18 slotIndex, +20 state, далее startTime/duration/иконка 117.
	//   ⚠ Ключ ОБЯЗАН включать контейнер: эталон сравнивает через `itemColorTypeMatch(itemCode,
	//   slotItemCode, containerType)`, а `TMap<itemCode>` терял бы контейнер и гасил чужие слоты.
	//   ★ ГРУППА (`Group`) — НАШЕ ДОПОЛНЕНИЕ ПОВЕРХ КЛИЕНТА ОРИГИНАЛА (закон `fix-original-defects`):
	//   сервер (`CheckSpendItemCoolTime@0x4DA9F0`, таблица `CUser+0x6F`, 30 × 7 б) ведёт откат
	//   ТОЛЬКО по группе `template+59`, а клиент оригинала — по коду+контейнеру+слоту. Из-за этого
	//   в оригинале после «Зелья здоровья» соседний код той же группы 1 выглядел готовым, игрок жал
	//   его и получал 5029 result=3 (нотис 9565). Авторитет — сервер, поэтому гасим ПО ГРУППЕ.
	struct FItemCooldownRec
	{
		int32  ItemCode  = 0;      // +12 эталона: ПОЛНЫЙ код (не усечённый до 16 бит)
		// +16: 1 = снаряжение/квестовые (ShellInven), 2 = расходники (SpendCellobInven), 3 = надетое.
		// ★ −1 = «контейнер неизвестен»: так приходят откаты, восстановленные пакетом 5465 — в его
		//   записи (6 б) есть только код и остаток, и эталон поля +16/+18 такой записи не заполняет
		//   вовсе (`Recv_SkillCoolTime@0xE4B180` пишет лишь `itemCoolMsg[3] = itemCode`).
		int32  Container = 0;
		int32  SlotIndex = -1;     // +18: ячейка контейнера, из которой применяли
		int32  Group     = 0;      // серверная группа отката (`item_list +0xCC` = `template +59`)
		double EndS      = 0.0;    // момент готовности (эталон: startTime + duration)
		double TotalS    = 0.0;    // полная длительность — знаменатель доли для кадра шторки
	};
	TArray<FItemCooldownRec> ItemCooldowns;

	// Живая запись отката, накрывающая (код, контейнер): совпадение по группе ЛИБО по коду+контейнеру.
	//   nullptr = откат истёк или его не было. Истёкшие записи чистит `PruneItemCooldowns`.
	const FItemCooldownRec* FindItemCooldownRec(int32 ItemCode, int32 Container) const;
	// Снять истёкшие записи (эталон чистит их в `TimerProcess`; у нас — лениво, перед каждым поиском).
	void PruneItemCooldowns();
	// Слоты, у которых перезарядка шла на прошлом кадре — для запуска вспышки «готово» (Д6).
	TSet<int32> WasCoolingSlots;

	// Отложенное действие смены страницы (эталон: состояние персонажа 104/0x68).
	bool  bPageActionPending = false;
	int32 PendingPage = 0;
	int32 PendingTargetRenderId = -1;

	// Очередь входящих страниц 5059 (закон `no-reentrant-net-callbacks`: колбэк только копит).
	//   ★ Аудит №3 (B10): своё поле-массив + свой `FCriticalSection` заменены общей `TAsdaNetQueue`
	//   (тело «взять всё разом и очистить» было побайтово одним и тем же в шести подсистемах).
	struct FPendingPage { uint8 Page = 0; TArray<uint8> Raw; };
	TAsdaNetQueue<FPendingPage> PendingPages;

	// Три списка эмоций из `emotiontip` в ПОРЯДКЕ ФАЙЛА (эталон `EmotionTip_Load`):
	//   [0] — записи с `Type == 0`; [1] — `Type == 1`, КРОМЕ `act_cancellation`;
	//   [2] — `Type == 2`, КРОМЕ `mes_couple`. Записи `Type == 3` не попадают никуда.
	//   Хранится имя иконки (колонка `Tag` = смещение +32 записи).
	TArray<FString> EmotionList[3];
	// Текст подсказки той же записи (поле `Name`, смещение 0 записи) — параллельно спискам иконок.
	TArray<FString> EmotionTip[3];
	bool bEmotionListsBuilt = false;

	// Контроллер, на делегаты которого мы подписаны (для отписки при смене мира).
	TWeakObjectPtr<class AAsdaPlayerController> BoundController;

	// Есть ли у игрока соулмейт (эталон: `gap_8B4[6] == 1 || gap_878[47] > -1`). Нужен для суффикса
	//   «-g»/«_g» у недоступных иконок навыка соулмейта и трёх парных эмоций.
	bool bSelfHasSoulmate = false;
	// ★ 06.11.2026: последний виденный «Друг в игре» — по смене перерисовываются иконки (сверка в `Tick`, ключ `asda.Soul.SlotTruth`).
	bool bSelfMateInGame = false;

	// Мосты
	FAsdaHotbarInventoryBridge Inventory;
	FAsdaHotbarSkillBridge     Skills;
	FAsdaHotbarMiscBridge      Misc;
	mutable bool bLoggedNoInventory = false;
	// Разовая запись «контейнер ещё не получен целиком» (гейт в `RebuildOneSlot`, ветка Type=1).
	mutable bool bLoggedContainerNotReady = false;

public:
	// ═══════════════════ ПАРАМЕТРЫ (все правятся в редакторе) ═══════════════════

	// Число кадров шторки перезарядки — из скина `CoolTime_Effect` (TotalFrame=118, EndFrame=117).
	//   Эти же числа эталон передаёт в `uiControl_ShowWithItem(ctrl, frame, 117, 118, remainMs, 1)`.
	UPROPERTY(EditAnywhere, Category="Asda|Hotbar|Перезарядка")
	int32 CurtainTotalFrames = 118;

	// Кадр/интервал «особого статик-состояния» state==2 — эталонная пара (127, 8, 1000, animate=0).
	UPROPERTY(EditAnywhere, Category="Asda|Hotbar|Перезарядка")
	int32 State2TotalFrames = 8;
	UPROPERTY(EditAnywhere, Category="Asda|Hotbar|Перезарядка")
	int32 State2IntervalMs = 1000;

	// Длительность вспышки «готово» — скин `CoolTimeAfter_Effect` (16 кадров, FrameInterval 500 мс).
	UPROPERTY(EditAnywhere, Category="Asda|Hotbar|Перезарядка")
	float ReadyFlashMs = 500.f;

	// Глобальный анти-спам навыков — эталон `g_dwLastSkillDispatchTime`, `timeGetTime() - t > 0xC8`.
	UPROPERTY(EditAnywhere, Category="Asda|Hotbar")
	int32 SkillThrottleMs = 200;

	// Перетаскивание слота требует удержания Shift — эталонная опция `g_bHotbarRequireShift`
	//   (в срезе найдено только ЧТЕНИЕ флага; где он выставляется в окне настроек — не найдено).
	UPROPERTY(EditAnywhere, Category="Asda|Hotbar")
	bool bDragRequiresShift = false;

	// Шаг колонок по X (JUI: 0, 35, 70, …, 385) и смещение слота оружия (−72 от колонки 0).
	UPROPERTY(EditAnywhere, Category="Asda|Hotbar")
	float ColumnStepPx = 35.f;

	// ═════════════════ ОФОРМЛЕНИЕ СЧЁТЧИКА СТАКА (правится в редакторе) ═════════════════════════
	// ★ Кегль. Эталонного значения в данных НЕТ: размер шрифта в .JUI не хранится вовсе (он глобальный
	//   для всего UI), импортёр ставит всему UI 8 px. 8 px жирным поверх ячейки 32×32 читается крупно,
	//   поэтому по просьбе юзера (2026-08-08) счётчик уменьшён до 7. ЭТО НАШ ПАРАМЕТР, не эталон.
	UPROPERTY(EditAnywhere, Category="Asda|Hotbar|Счётчик стака")
	int32 StackCountFontPx = 7;

	// Толщина чёрной обводки счётчика. ★ ЭТАЛОННОЕ ЗНАЧЕНИЕ — 0 (обводки нет).
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
	//   ★★★ ПОПРАВКА 26.10.2026 (баг «число стопки плохо видно»): «для счётчика эталон поле 926 НЕ ставит» — НЕВЕРНО.
	//   Эталон ставит ячейкам 73011…73023 обводку: `CClass_SetField926(slot, 0x800000)` (ui_core.c:65971; второй ряд —
	//   :66110), строки `QUICK_NEW_SLOT*` в `Tools/data/text_style926.tsv` (outline=1). Обводку теперь даёт общая точка
	//   `AsdaItemCount::ApplyOutline` (ключ `asda.UI.ItemCountOutline`, умолчание 1 px чёрный); это поле — значение
	//   при ключе 0 («как было в порте»).
	UPROPERTY(EditAnywhere, Category="Asda|Hotbar|Счётчик стака")
	int32 StackOutlinePx = 0;

	UPROPERTY(EditAnywhere, Category="Asda|Hotbar|Счётчик стака")
	FLinearColor StackOutlineColor = FLinearColor(0.f, 0.f, 0.f, 1.f);

	// Отступ строки от нижней кромки ячейки (эталонная маска 0x8 = «вплотную к низу» ⇒ 0).
	UPROPERTY(EditAnywhere, Category="Asda|Hotbar|Счётчик стака")
	float StackBottomPadPx = 0.f;

	// ═════════════════ ПРИЗРАК ПЕРЕТАСКИВАНИЯ (правится в редакторе) ═══════════════════════════
	// Прозрачность летящей за курсором иконки. ★ Юзер попросил 40 % (alpha 0.4). Эталонное значение
	//   в срезе НЕ раскрыто: оригинал рисует ту же плоскость у курсора движком (FillMode 5), а alpha
	//   этого режима зашит в D3D-слое рендера, которого в декомпиле нет. Держим 0.4 по просьбе юзера.
	UPROPERTY(EditAnywhere, Category="Asda|Hotbar|Перетаскивание")
	float DragGhostOpacity = 0.4f;

	// Прозрачность ИСХОДНОГО слота, пока тащим (эталон гасит его через FillMode 5 = «не рисовать здесь,
	//   рисовать у курсора»; у нас слот приглушается, чтобы было видно, откуда взяли). 1.0 = не гасить.
	UPROPERTY(EditAnywhere, Category="Asda|Hotbar|Перетаскивание")
	float DragSourceOpacity = 0.4f;

	// Смещение призрака от курсора (эталонного значения в срезе нет — по умолчанию центр иконки 32×32).
	UPROPERTY(EditAnywhere, Category="Asda|Hotbar|Перетаскивание")
	FVector2D DragGhostCursorOffset = FVector2D(-16.f, -16.f);

	// z-order: между полосой опыта (90) и мини-статусом (95); AlwaysTop=0 у всех 278 контролов.
	static constexpr int32 kQuickZ = 92;

private:
	double LastSkillDispatchS = -1.0;
};

// ★ 06.11.2026 [ИСПРАВЛЕНИЕ]: включён ли ключ `asda.Soul.SlotTruth` — пара и «Друг в игре» читаются из карточки соулмейта (5220 по charId).
ASDAREBORN_API bool AsdaSoulSlotTruthOn();
