// Copyright (c) 2026 @sitxovski (Discord)
// UAsdaChatWindowSubsystem — ОСНОВНОЕ ОКНО ЧАТА CHAT_NEW (JUI idnum 2500).
//
// ═══════════════════════════ ЭТАЛОН (сборка IDB, RE/etalon) ═══════════════════════════════════
// Окно строит `CChat_BuildTabUI @0xB9E490` (chat.c:738):
//     chatNewWin = CWindowMgr_FindControlByID(g_pWindowMgr, 2500);   // chat.c:784
//     CControl_SetVisible(chatNewWin, 1);  sub_9372BC(chatNewWin, 0);  listMoveToEnd(...)
// ⇒ окно ВИДИМО с момента входа в мир и нигде не прячется.
// ПОДТВЕРЖДЕНО ЖИВЫМ ДАМПОМ КЛИЕНТА (live.dmp, g_pChatWnd = объект CChat @0x2BE06C8;
//   CControl+0x124 = isVisible, CControl+0x16C = controlId): CChat+0x004 → id 2500, isVisible = 1.
//   Для сравнения, старый чат — CChat+0x57C → id 214 (CHAT_BACKWIN) isVisible = 0,
//   CChat+0x5A8 → id 210 isVisible = 0, CChat+0x5AC → id 200 isVisible = 0,
//   CChat+0x164 → id 2540 isVisible = 0. Все они перенесены в Unused (UNUSED_REPORT.md §12).
//
// ГЕОМЕТРИЯ (JUI gamemain, база 1024×768; прогон Tools/asda_jui_parse.py::compute_anchors).
//   ВСЕ узлы окна — POINT-анкор в ЛЕВЫЙ-НИЖНИЙ угол экрана (fx = 0.0, fy = 1.0) + пиксельный
//   оффсет вверх. Схема ТА ЖЕ, что у окна 2570, и та же, что ставит импортёр:
//     CHAT_NEW           2500  abs=(  1, 580, 474×178)  off(L  1, T −188, W 474, H 178)
//     CHAT_NEW_back        −1  abs=(  2, 581, 472×174)  off(L  2, T −187, W 472, H 174)  подложка
//     CHAT_NEW_TextMove  2534  abs=(  2, 582, 474× 32)  off(L  2, T −186, W 474, H  32)  drag-зона
//     CHAT_NEW_Radio_B   2501  abs=( 10, 591,  79× 25)  off(L 10, T −177, W  79, H  25)  вкладки
//     lCHAT_NEW_ine0     2530  abs=(  1, 580, 474×170)  off(L  1, T −188, W 474, H 170)  рамка
//     CHAT_NEW_scr       2536  abs=(445, 606,  20× 10)  off(L445, T −162, W  20, H  10)  скроллбар
//     CHAT_NEW_C-List    2520  abs=( 28, 625, 439× 98)  off(L 28, T −143, W 439, H  98)  контейнер
//     CHAT_NEW_C_NOR     2521  abs=( 28, 625, 439× 98)  off(L 28, T −143, W 439, H  98)  ЛЕНТА СТРОК
//     CHAT_NEW_HomeBtn   2528  abs=(451, 627,  13× 13)  off(L451, T −141, W  13, H  13)  «в конец»
//   Окно занимает y 580…758. Журнал 2570 — y 508…578 (встык, БЕЗ наложения),
//   полоса опыта EXPBar_WIn 310 — y 758…768 (тоже без наложения). Проверено расчётом.
//
// ЛЕНТА СТРОК — `chatNewListBoxNor` (2521), настройка chat.c:796-807:
//     sub_92E0F4(list, 1); sub_93A354(list, 1);
//     uiListSetChildPlaneField(list, 0, planeW − 5);
//     sub_935C9B(list, 500);            // ★ ЛИМИТ 500 СТРОК
//     uiListSetScrollOffset(list, 2);   // ★ МЕЖСТРОЧНЫЙ ИНТЕРВАЛ 2 px (не «шаг листания»!): кладёт
//                                       //   в list+0x4E8 и считает высоту строки = 2 + glyphH
//     sub_937235(list, 0x400000);       // ★ ФЛАГ ТЕНИ (см. ниже)
//   Клик по 2521 НЕ поглощается интерфейсом: game_main.c:5748 держит whitelist
//   {2521, 2572, 2557, 2558, 159, 9501} — при LButtonDown над ними `g_WorldInputFlags |= 1`,
//   то есть клик прокидывается в мир. ★ ОТСТУПЛЕНИЕ (решение юзера от 2026-08-04, уже принятое
//   для окна 2570): у нас окно чата клик НЕ пропускает — «клик в виджете работает только в
//   виджете». Зона регистрируется в общем реестре UAsdaWindowSubsystem::RegisterUiZone.
//
// НАЧЕРТАНИЕ ТЕКСТА. Флаг 0x400000 на списке = ТЕНЬ. Расшифровка доказана по font_text.c:
//     <FB> 0x200000 — жирный (двойная отрисовка в penX±1, font_text.c:6832-6886)
//     <FD> 0x400000 — ТЕНЬ   (копия глифа со сдвигом +1/+1 цветом shadowColor, font_text.c:6887+)
//     <FO> 0x800000 — обводка (парсер font_text.c:7978-7983)
//   Кегль: в JUI размер шрифта per-control НЕ хранится (он глобальный); импортёр
//   Tools/asda_jui_generic_umg.py ставит всему UI FONT_PX = 8 → строки приводим тегом <FS08>.
//
// ═════════════════════ ТАБЛИЦА КАНАЛОВ — ★ ПОРЯДОК СЛОТОВ ≠ ПОРЯДОК loc-id ════════════════════
// `CChat_InitChannelTable` (chat.c:1296-1328) заполняет массив `gap_27C` = 10 записей × 24 байта
// (имя char[20] + цвет dword на +20), затем `memcpy(&tabFilterOpt, gap_27C, 0xF0)` (0xF0 = 10×24).
// Слоты 4/5/6 берут loc 5347/5346/5345 — номера ПЕРЕСТАВЛЕНЫ. Не выводить порядок из номеров loc!
//
//   слот  loc   название (RU)          цвет dword     ARGB
//     0   5341  [Все] Общий чат          −12602    FFFFCEC6
//     1   5342  [Все] Поиск отряда     −7564801    FF8C91FF
//     2   5343  [Все] Торговля         −6697984    FF99CC00
//     3   5344  Глобальный чат           −65536    FFFF0000
//     4   5347  Общий чат                    −1    FFFFFFFF
//     5   5346  Чат отряда             −5505159    FFABFF79
//     6   5345  Чат клана              −9446402    FF6FDBFE
//     7   5348  Шепот                    −10240    FFFFD800
//     8   5349  Чат с Другом             −28418    FFFF90FE
//     9   5350  Чат армии              −6276096    FFA03C00
//
// ПРОВЕРКА АДРЕСАМИ (не догадка): `tabFilterOpt` лежит на CChat+0x036C, цвет записи n =
//   0x36C + 24n + 20. Для n=3 → +0x3C8, для n=4 → +0x3E0 — ровно те смещения, которые
//   structs_ru.h называет `chanColorNormal` и `chanColorDefault`. Сходится побайтово.
// ★ Имена полей `chanColorWhisper` (+0x3F8) / `chanColorParty` (+0x410) / `chanColorGuild`
//   (+0x428) / `chanColorTrade` (+0x440) в structs_ru.h ВВОДЯТ В ЗАБЛУЖДЕНИЕ — это гипотезы
//   прошлых разборов; фактически это слоты 5 (Отряд) / 6 (Клан) / 7 (Шепот) / 8 (Друг).
// ПЕРЕКРЁСТНАЯ ПРОВЕРКА: все 7 масок вкладок по умолчанию (uiChatOpt_InitItems @0xBA0920)
//   сходятся именно с этим порядком слотов — «Поиск отряда» 930 = слоты {1,5,7,8,9},
//   «Торговля» 900 = {2,7,8,9}, «По всем каналам» 903 = {0,1,2,7,8,9} и т. д.
//
// КАТЕГОРИЯ → СЛОТ (`Chat_AddChatLine @0xBB14C0`, chat.c:5001-5031):
//   a2 = 0 или 100 → слот 4;  a2 = 1 → слот 3;  a2 = 2 → слот 5 (либо 41 = префикс loc 12093);
//   a2 = 3 или 101 → слот 8;  a2 = 4 → слот 6;  a2 = 5 → слот 7;  a2 = 6 → 40 (служебный);
//   a2 = 7/8/9 → слот a2 − 7 (три канала «[Все] …»).
//
// ═════════════ ★★★ ЭТАЛОННАЯ ВИДИМОСТЬ УЗЛОВ СРАЗУ ПОСЛЕ ВХОДА В МИР (аудит 2026-08-05) ════════
// Источник — ТОЛЬКО два: атрибут `Visible` узла в gamemain_jui.ini и явные вызовы
// `CControl_SetVisible` в `CChat_BuildTabUI @0xB9E490` (chat.c:739-935). Ничего не додумано.
// ★ Перенесено в ассет (15.09, разрешение владельца): эта видимость ЛЕЖИТ В WBP_CHAT_NEW (шаг
//   Tools/asda_ui_chat_markup_visibility.py в asda_ui_post_import.STEPS); код её при построении не ставит.
//   Таблица ниже — справка по эталону (номера строк chat.c — прежней нумерации).
//
//   узел                     id    JUI Visible   SetVisible в build     ИТОГ
//   CHAT_NEW                2500        1        1 (chat.c:784)         ВИДИМО
//   CHAT_NEW_back / back2     −1        1        —                      ВИДИМО (подложка Box_Black_Ot10)
//   CHAT_NEW_TextMove       2534        1        —                      ВИДИМО (NoImage — зона drag)
//   CHAT_NEW_Radio_B        2501        1        —                      ВИДИМО (вкладки)
//   lCHAT_NEW_ine0          2530        0        —                      скрыто (и line1/line2 — дети)
//   CHAT_NEW_scr (без id)     −1        0        —                      скрыто
//   CHAT_NEW_scr            2536        1        ★ 0 (chat.c:888)       ★ СКРЫТО
//   CHAT_NEW_C-List/C_NOR   2520/2521   1        —                      ВИДИМО (лента)
//   CHAT_NEW_HomeBtn        2528        1        —                      ВИДИМО
//   CHAT_NEW_Que_btn_tooltip 2580       0        —                      скрыто (открывает кнопка 2537)
//   CHAT_NEW_UDbtn          2605        0        ★ 1 (chat.c:923)       ★ ВИДИМО
//   CHAT_NEW_Quw_btn        2606        0        — (кода нет вообще)    скрыто
//   CHAT_NEW_Hbtn           2607        0        ★ 1 (chat.c:929)       ★ ВИДИМО
//   SizeWin                   −1        1        —                      ВИДИМО (NoImage-контейнер строки ввода)
//   CHAT_NEW_Bar            2532        0        0 (chat.c:838)         скрыто (легаси)
//   CHAT_NEW_Bar_TEXT       2535        1        0 (chat.c:848)         скрыто (легаси)
//   CHAT_NEW_Edit           2533        1        0 (chat.c:820)         скрыто (легаси)
//   CHAT_NEW_Enter          2529        0        —                      скрыто (легаси)
//   CHAT_NEW_SSbtn          2608        0        ★ 1 (chat.c:717)       ★ ВИДИМО — см. ниже
//   CHAT_NEW_ENG            2512        0        ★ 1 (chat.c:814)       ★ ВИДИМО
//   CHAT_NEW_HAN            2511        1        ★ 0 (chat.c:809)       ★ СКРЫТО
//   CHAT_NEW_Bar2           2618        0        0 (chat.c:841)         скрыто (появляется при вводе)
//   CHAT_NEW_Bar_TEXT2      2619        0        — (только SetText)     скрыто
//   CHAT_NEW_Edit2          2609        0        ★ 1 (chat.c:823)       ★ ВИДИМО (поле ввода)
//   CHAT_NEW_Que_btn        2537        1        —                      ★ ВИДИМО (кнопка справки)
//
// ★★★ ПОПРАВКА К ЭТОЙ ТАБЛИЦЕ (перепроверка 2026-08-05). Смотреть ТОЛЬКО `CChat_BuildTabUI`
//   БЫЛО МАЛО: часть контролов эталон показывает ПОЗЖЕ, в самом `chatInitColorPalette @0xB9E110`
//   (chat.c:666-735), который и вызывает все Build*UI. Там, после них, стоит:
//       chatSetupWin = FindControlByID(2608);  CControl_SetVisible(chatSetupWin, 1);
//   ⇒ КОМБОБОКС ВЫБОРА КАНАЛА 2608 В ОРИГИНАЛЕ ВИДЕН. Наполнение — 7 пунктов:
//       loc 5332 «ОБЩИЙ», 5331 «ГЛОБАЛЬНЫЙ», 5336 «ДРУГ», 5334 «КЛАН», 5333 «ОТРЯД»,
//       5335 «ШЕПОТ», 1465 «Прочие»;  CControl_SetScrollPos(…, 7); SetSelectedItem(…).
//   Выбор — `uiChatWindowMsgProc` (ui_core.c:12588), msgCode 0x11201, ctrlId 2608:
//       if (wparam < 6) uiQuestInput_UpdatePromptByState(this, channelOrder[wparam]);
//       channelOrder[6] = { 4, 3, 8, 6, 5, 7 };
//   то есть ОБЩИЙ→4, ГЛОБАЛЬНЫЙ→3, ДРУГ→8, КЛАН→6, ОТРЯД→5, ШЕПОТ→7, «Прочие» — без действия.
//
// ═══════════════════════════════ ЭТАП 1 vs ДАЛЬШЕ ══════════════════════════════════════════════
// ЭТАП 1 (этот файл): окно, лента строк (500, автоскролл), HomeBtn 2528, перетаскивание за 2534,
//   блокировка мирового клика через общий реестр зон, приём серверных строк на ВЫВОД,
//   гашение легаси-узлов, ★ раскладка окна по ПРАВИЛАМ JUI (ApplyWindowLayout),
//   ★ UDbtn 2605 свернуть/развернуть, ★ Hbtn 2607 замок автопрокрутки,
//   ★ Que_btn 2537 + окно справки 2580/2589 (loc 5400-5406).
// ЭТАП 2: ввод (2609, лимит 68), антифлуд-бар 2618/2619, переключение IME 2511↔2512,
//   отправка (опкоды 5084/5088), слэш-команды, фильтр мата, перенос строк токенайзером.
// ЭТАП 3: контекстные меню — CHAT_SETUP_Win 2670 (ПКМ по вкладке) и CHAT_NEW_TABX 80000 (ПКМ по имени).
// ЭТАП 4: вкладки-фильтры (2501 переключение), редактор вкладок 2590 + палитра 2700 +
//   персист ChatOpt.bin (7 вкладок × 22 байта), комбобокс канала 2608.
//
// ★★★ ИСПРАВЛЕНИЕ ПРЕЖНЕГО РАЗБОРА (дважды): `CHAT_NEW_Hbtn` (2607) — НЕ «hold-resize по высоте»
//   и НЕ hold-кнопка вообще. Флаг `CChat+497` читается во ВСЁМ срезе ровно дважды — chat.c:1828
//   и chat.c:3375 — и оба раза это «не прокручивать ленту в конец» ⇒ 2607 = ЗАМОК АВТОПРОКРУТКИ.
//   ★ Скин контрола `Chat_new_ck3` имеет `Type = "CheckButton"`, а `checkBox_OnToggle @0x12FDF70`
//   поднимает 0x10601 при ОТМЕТКЕ и 0x10602 при СНЯТИИ ⇒ это ТУМБЛЕР (полное доказательство —
//   в шапке поля `bScrollFrozen`). Прежняя трактовка «нажал/отпустил» и делала кнопку нерабочей.
//   Размер окна меняет ТОЛЬКО 2605 (`uiToggleExpandLayout @0xBB6E40`, ui_core.c:13529).
//
// ЛЕГАСИ ВНУТРИ ЖИВОГО ОКНА (эталон принудительно гасит при построении — гасим и мы, чтобы не
//   всплыло на следующих этапах): CHAT_NEW_Bar 2532 (chat.c:838), CHAT_NEW_Bar_TEXT 2535
//   (chat.c:848), CHAT_NEW_Edit 2533 (chat.c:820) — заменены на 2618/2619/2609;
//   CHAT_NEW_Enter 2529 и CHAT_NEW_Quw_btn 2606 — ссылок в срезе нет.
#pragma once

#include "CoreMinimal.h"
#include "Subsystems/GameInstanceSubsystem.h"
#include "Tickable.h"
#include "Blueprint/UserWidget.h"
#include "UI/AsdaUiInputTypes.h"   // ★ R1: FAsdaMouseFrame / FAsdaDragArm / FAsdaDragGhost … — поля ввода (без всего AsdaUiHelpers.h)
#include "Layout/Margin.h"         // FMargin
#include "Widgets/Layout/Anchors.h"   // FAnchors
#include "AsdaSessionReset.h"   // IAsdaSessionResettable — сброс состояния сессии
#include "UI/AsdaHelpTopics.h"  // FAsdaHelpTipHover — подсказки вкладок 2501 и поля ввода 2609
#include "AsdaChatWindowSubsystem.generated.h"

// Категория строки чата = аргумент a2 функции Chat_AddChatLine @0xBB14C0 (chat.c:5002).
//   Значения — ровно те, что встречаются в вызовах эталона; имена наши, по факту ветки.
UENUM(BlueprintType)
enum class EAsdaChatCat : uint8
{
	Common      = 0,    // → слот 4 «Общий чат»       (chanColorDefault, белый)
	Global      = 1,    // → слот 3 «Глобальный чат»  (chanColorNormal, красный)
	Party       = 2,    // → слот 5 «Чат отряда»      (префикс loc 12093 «Отряд» при colorType 41)
	Friend      = 3,    // → слот 8 «Чат с Другом»
	Clan        = 4,    // → слот 6 «Чат клана»
	Whisper     = 5,    // → слот 7 «Шепот»
	Service     = 6,    // → служебный colorType 40
	AllCommon   = 7,    // → слот 0 «[Все] Общий чат»
	AllFindTeam = 8,    // → слот 1 «[Все] Поиск отряда»
	AllTrade    = 9,    // → слот 2 «[Все] Торговля»
	Common100   = 100,  // → слот 4 (та же ветка, что 0)
	Friend101   = 101   // → слот 8 (та же ветка, что 3)
};

UCLASS()
class ASDAREBORN_API UAsdaChatWindowSubsystem : public UGameInstanceSubsystem, public FTickableGameObject,
	public IAsdaSessionResettable
{
	GENERATED_BODY()

	// Самотест `asda.SelfTest.HoverNoSelect` наводит курсор на плитку вкладки 2501 и поле ввода 2609 (живая геометрия).
	friend struct FAsdaHoverNoSelectSelfTest;

public:
	// ★ ОБЩАЯ ТОЧКА (закон `port-shared-points`): мат-фильтр эталона `Chat_MatchBannedWord@0xBB2830`
	//   зовёт не только чат — его же зовёт кнопка 14215 «Создание клана» перед отправкой 4104
	//   (`uiPanel_HandleCommand`, `case 0x3787`, `ui_core.c:15351` → нотис 7009). Поэтому проверка
	//   объявлена публично, а не заново переписана в клановой подсистеме.
	//   ⚠ Таблица `CurseWords` намеренно ПУСТА (требование юзера) — фильтр фактически выключен.
	bool IsCursed(const FString& In) const;

	virtual void Initialize(FSubsystemCollectionBase& Collection) override;
	virtual void Deinitialize() override;

	// ── IAsdaSessionResettable (К-3) ──
	// Окно, его защёлка «запрос в полёте» и полученные от сервера данные
	// принадлежат СЕССИИ и переживали разрыв связи вместе с GameInstance.
	virtual void ResetSessionState(EAsdaSessionResetReason Reason) override;

	// FTickableGameObject: окно самовосстанавливается после травела (виджеты умирают с миром).
	virtual void Tick(float DeltaTime) override;
	virtual bool IsTickable() const override { return !IsTemplate(); }
	virtual TStatId GetStatId() const override { RETURN_QUICK_DECLARE_CYCLE_STAT(UAsdaChatWindowSubsystem, STATGROUP_Tickables); }

	// ★ ЕДИНАЯ ТОЧКА ВЫВОДА СТРОКИ — порт `Chat_AddChatLine @0xBB14C0`.
	//   ★★★ ФАКТ РЕВЕРСА (2026-08-05): переданный цвет `a4` в эталоне ПЕРЕТИРАЕТСЯ цветом канала
	//   для ВСЕХ категорий, кроме `a2 == 6` (colorType 40) и неизвестных значений — только там
	//   `textColor` остаётся равным `a4`. Поэтому `FallbackColor` применяется ТОЛЬКО когда слот
	//   канала не определён; иначе цвет строки берётся из таблицы каналов. Раньше порт мог бы
	//   «уважать» переданный цвет всегда — это было бы расхождение с оригиналом.
	UFUNCTION(BlueprintCallable, Category="Asda|ChatWindow")
	void AddChatLine(EAsdaChatCat Category, const FString& Text, FColor FallbackColor = FColor(255, 255, 255, 255));

	// То же по сырому числу категории (для сетевых веток, где a2 приходит числом).
	//   RowBgColor / bRowBg — ЗАЛИВКА ФОНА СТРОКИ. Это и есть эталонный «ранговый цвет»:
	//   `Chat_AddChatLine(..., a8 = rankColor, a9 = 1)` → `uiChatLogAddLine` кладёт a8 в поле
	//   строки +2760, а a9 — в флаг +3108; при отрисовке (ui_core.c:113035-113038)
	//   `if (line[3108] == 1) bgColor = *(DWORD*)(line + 2760)` рисуется прямоугольник-подложка.
	//   ★ Ширина префикса (`a7`, поле строки +3104) во ВСЁМ срезе только ПИШЕТСЯ и ни разу не
	//   читается ⇒ «двухцветная строка через ширину префикса» в этой сборке НЕ реализована.
	//   ★ SenderName — ИМЯ ОТПРАВИТЕЛЯ ИЗ ПАКЕТА (а не выковырянное из текста). Нужно двум местам:
	//     (1) разметке строки — ник красится отдельным сегментом (белый + чёрная обводка);
	//     (2) hover/ПКМ-меню — ник берётся отсюда, а не «из первых скобок», иначе добавленный
	//         префикс канала «[Общ.]» подменил бы ник (см. Д21 п.6).
	//   ★ PrefixSlotOverride — слот ТОЛЬКО для префикса/обводки, когда категория эталона слота не
	//     даёт. Нужен ровно одному опкоду: клан-чат **6719** приходит категорией 6 (colorType 40) —
	//     ЕДИНСТВЕННОЙ, где эталон НЕ перетирает переданный цвет, поэтому слота канала у неё нет,
	//     и без override гильдейские сообщения остались бы без «[Гильдия]». INDEX_NONE = как обычно.
	void AddChatLineRaw(int32 Category, const FString& Text,
	                    FColor FallbackColor = FColor(255, 255, 255, 255),
	                    FColor RowBgColor = FColor(0, 0, 0, 0), bool bRowBg = false,
	                    const FString& SenderName = FString(),
	                    int32 PrefixSlotOverride = INDEX_NONE);

	// ★★★ СЛУЖЕБНАЯ СТРОКА ЛЕНТЫ = ДУБЛЬ ОПОВЕЩЕНИЯ/БАННЕРА (приёмник EAsdaMsgTarget::ChatRibbon).
	//   ЭТАЛОН ДЕЛАЛ ЭТО САМ, двумя разными вызовами:
	//     • редкая добыча — `Chat_AddChatLine(g_pChatWnd, /*канал*/10, msg, −16711936 зелёный, …)`
	//       (item_inventory.c:39570 свой подбор/гача/заточка; network.c:3750 чужой, приём 6131).
	//       Канала 10 в `switch` нет ⇒ `default:` (chat.c:5024-5026) оставляет инициализаторы:
	//       colorType = 4 (слот строки), needPrefix = 0, textColor = a4 (цвет вызова НЕ перетирается
	//       цветом канала);
	//     • объявление администрации 3113 — `uiChatLogAddLine(listBoxSys, текст, −65536 КРАСНЫЙ,
	//       0, 0.0f, 0,0,0, /*слот*/255)` (ui_core.c:12656).
	//   ЧЕМ ОТЛИЧАЕТСЯ ОТ AddChatLineRaw (её семантика эталонная и её НЕ трогаем):
	//     • слот задаётся ЯВНО, а не выводится из категории. −1 = «канала нет» — 1:1 эталонный
	//       слот 255: фильтр вкладок пропускает такую строку всегда (ui_core.c:111448 сравнивает
	//       `line[+3460] >= chanCount`, у нас ветка `if (L.Slot >= 0 && …)` в RebuildLines);
	//       4 = эталонный слот анонса редкой добычи (виден на вкладке 0 «Объединение», её маска
	//       1023 — единственная с битом 4);
	//     • PrefixSlot ВСЕГДА −1 ⇒ BuildLineMarkup идёт СЛУЖЕБНОЙ веткой: весь текст красится
	//       базовым цветом `BaseColor`, а inline-теги `<FC=грейд>…</F>` внутри строки переживают
	//       разметку (лямбда Restore возвращает базу после каждого `</F>`) — строка в ленте
	//       выглядит ровно так же, как в HERO_NOTICE: имя предмета и процент цветом грейда;
	//     • bNoNick = true ⇒ эталонный фолбэк ParseNickFromLine («ник между ПЕРВЫМИ [ ]») к строке
	//       НЕ применяется. Без этого «[ Меч Дьявола ]» из анонса заточки стал бы «ником»: цвет
	//       грейда затёрся бы белым сегментом, hover-плашка и ПКМ-меню 80000 сели бы на название
	//       предмета, а «Блокировать чат» отправил бы в игнор-лист название предмета.
	//   ⚠ Подставлять сюда `Nick = имя игрока` НЕЛЬЗЯ: строка ушла бы в канальную ветку разметки,
	//   где текст красится ЦВЕТОМ КАНАЛА, и золотой/красный/зелёный анонса вместе с цветом грейда
	//   были бы потеряны.
	//   ★ Цвет приходит в sRGB и уходит тегом `<FC=FFRRGGBB>` — де-гаммит его разметка
	//   (закон ui-srgb-gamma-rule: FromSRGBColor применяется только к ЗАЛИВКАМ).
	void AddAnnounceLine(const FString& MarkupText, FColor BaseColor, int32 Slot = INDEX_NONE);

	// ★★★ СОКРАЩЁННОЕ ИМЯ КАНАЛА ДЛЯ ПРЕФИКСА СТРОКИ («[Общ.]», «[Глоб.]», «[Поиск отр.]»…).
	//   ⚠ ОТСТУПЛЕНИЕ ОТ ОРИГИНАЛА (требование юзера 2026-08-06): в эталоне префикса канала у
	//   обычных строк НЕТ — там канал несёт ЦВЕТ САМОГО ТЕКСТА (`Chat_AddChatLine` красит строку
	//   цветом слота), а «[имя канала] » приписывается ТОЛЬКО в режиме осады
	//   (`CChat::bChannelPrefixMode`, chat.c:5102-5121, и там имя берётся из loc 12092+noticeId).
	//   Сокращения — НАШИ (в данных игры их нет): полные названия слотов (loc 5341…5350) в ленту
	//   не влезают. Таблица живёт в .cpp рядом с kChannels, менять — там.
	static FString SlotShortName(int32 Slot);   // [ПОРТ] 09.10.2026 (мультиязычность, этап 3): строка таблицы (язык сессии), не литерал

	// ★★★ ПОДПИСЬ КАНАЛА ДЛЯ ВЫВОДА В ИНТЕРФЕЙС. Берёт строку из таблицы текстов по loc-id и
	//   применяет ПЕРЕИМЕНОВАНИЯ порта. Сейчас переименование одно (требование юзера 2026-08-06):
	//   **«По всем каналам» → «Все каналы»**. Таблицу текстов игры НЕ правим — только вывод,
	//   поэтому новая подпись появляется сразу везде: вкладка-фильтр № 6, выпадающий список,
	//   приглашение режима, логи. Все места вывода названий каналов обязаны идти через неё.
	FString UiChannelText(int32 LocId) const;
	// Та же замена для уже готовой строки (когда loc-id недоступен).
	static FString RenameChannelText(const FString& In);
	// ★ 10.10.2026 (en_completeness): свой loc-id порта, которым заменяется строка игры LocId (5417 → 990458), иначе 0.
	static int32 RenamedChannelLoc(int32 LocId);
	// ★ 10.10.2026 (en_completeness): Name — подпись по умолчанию для loc-строки LocId на ЛЮБОМ языке порта (RU из
	//   таблицы текстов, EN/AR из оверлеев, с переименованием порта)? Так `ChatOpt.bin`, записанный в русской сессии,
	//   не оставляет русские вкладки/каналы в английской: такая запись снова становится loc-вкладкой.
	//   ★ 10.10.2026 (en_terms_fix): вкладки, чьё имя задал игрок, порт помечает в хвосте записи `ChatOpt.bin` (после
	//   нуля C-строки, раскладка эталона та же) — для них эта сверка не делается, «Party» игрока в RU остаётся его именем.
	static bool IsDefaultNameAnyLang(const FString& Name, int32 LocId);

	// ★★★ РЕЖИМ ПРЕФИКСА КАНАЛА (`CChat::bChannelPrefixMode`, +0x06A0).
	//   РЕВЕРС ЗАКРЫТ (2026-08-05): это НЕ опция игрока, НЕ чекбокс и НЕ поле ChatOpt.bin
	//   (CChat_LoadChatOpt/SaveChatOpt пишут только tabFilterOpt, tabCount и записи вкладок).
	//   Записей во всём срезе ровно две: chat.c:225 (сброс в 0 при инициализации CChat) и
	//   chat.c:5607 `toggleField105Swap @0xBB4CA0`. Единственный вызывающий —
	//   `campWarWnd_ToggleVisible @0xCA1690` (combat_pvp.c:5451), то есть флаг живёт вместе с
	//   окном ОСАДЫ/лагерной войны и включается в том числе ПАКЕТОМ (вход в зону осады —
	//   network.c:43115/43119, завершение — 0x1A51 network.c:1058).
	//   Побочный эффект в оригинале: вместе с флагом набор вкладок переключается на индекс 9
	//   (с сохранением предыдущего) — это уже логика вкладок, ЭТАП 4.
	//   Здесь реализован ТОЛЬКО эффект на строку: при 1 к строке добавляется «[канал] ».
	UFUNCTION(BlueprintCallable, Category="Asda|ChatWindow")
	void SetChannelPrefixMode(bool bEnabled);
	UFUNCTION(BlueprintPure, Category="Asda|ChatWindow")
	bool IsChannelPrefixMode() const { return bChannelPrefixMode; }

	// ★ РАНГОВЫЙ ЦВЕТ (эталон Recv_ChatNormal, network.c:10442-10462) — РЕВЕРС ЗАКРЫТ.
	//   rankLevel приходит DWORD'ом в пакете; ветка активна при 1 ≤ rankLevel ≤ 100.
	//     rankLevel == 1      → 0xFF000000
	//     rankLevel == 2 или 3→ 0xFF726CFF
	//     иначе (4..100)      → линейный лерп по rankLevel/100:
	//                           R 0→109, G 54→207, B 99→246 (при 100 получается 0xFF6DCFF6)
	//   Цвет идёт в ЗАЛИВКУ ФОНА строки (см. AddChatLineRaw), а к тексту дописывается «[#N] ».
	static FColor RankColor(int32 RankLevel);

	// Номер слота канала по категории (эталонный switch chat.c:5027-5075). −1 = служебный/неизвестный.
	static int32 CategoryToSlot(int32 Category);
	// Цвет слота канала ПО УМОЛЧАНИЮ (таблица из шапки, sRGB). Слот вне 0..9 → белый.
	//   ★ Это ТОЛЬКО дефолт — заводское значение записи `tabFilterOpt[slot]`. Действующий цвет
	//   канала берётся через ChanColor() (его правит палитра 2700), см. ниже.
	static FColor SlotColor(int32 Slot);
	// ★★★ ДЕЙСТВУЮЩИЙ ЦВЕТ КАНАЛА = ЖИВАЯ ЗАПИСЬ `tabFilterOpt[slot].color` (эталон `CChat+0x36C`,
	//   10 записей по 24 б: `char name[20]` + `DWORD colorARGB`). У нас это `ChanOpts[slot].Color`.
	//   ★ ДОКАЗАТЕЛЬСТВО, ЧТО ЦВЕТ ТЕКСТА ЛЕНТЫ БЕРЁТСЯ ИМЕННО ОТСЮДА (реверс 2026-08-06):
	//   декомпилятор разложил ОДИН массив на «отдельные» поля структуры, и это сбило прежний
	//   разбор. `Chat_AddChatLine` (chat.c:5027-5079) кладёт в textColor:
	//       colorType 3 → chanColorNormal  @0x3C8 = 0x36C + 24*3 + 20  ⇒ tabFilterOpt[3].color
	//       colorType 4 → chanColorDefault @0x3E0 = 0x36C + 24*4 + 20  ⇒ tabFilterOpt[4].color
	//       colorType 5 → chanColorWhisper @0x3F8 ⇒ [5]   colorType 6 → chanColorParty @0x410 ⇒ [6]
	//       colorType 7 → chanColorGuild   @0x428 ⇒ [7]   colorType 8 → chanColorTrade @0x440 ⇒ [8]
	//       colorType 0..2 → `tabNameColorRecs[24*colorType]` @0x380 = та же запись [0..2]
	//   То есть ВСЕ шесть «отдельных полей цвета» физически лежат ВНУТРИ массива tabFilterOpt,
	//   который правит палитра 2700 и коммитит ОК окна 2590 (`memcpy(self+219, self+279, 0xF0)`,
	//   chat.c:2090 — 240 б = 10×24). ⇒ смена цвета канала В ОРИГИНАЛЕ меняет цвет текста ленты.
	FColor ChanColor(int32 Slot) const;
	// loc-id названия слота канала (таблица из шапки). Слот вне 0..9 → 0.
	static int32 SlotNameLocId(int32 Slot);

	// Курсор над окном чата? Это и есть провайдер зоны в общем реестре UAsdaWindowSubsystem.
	UFUNCTION(BlueprintPure, Category="Asda|ChatWindow")
	bool IsCursorOverWindow() const;

	// Диагностика: строки/лимит/видимость (asda.ChatWin.Dump).
	FString DebugState() const;
	// ★ 10.10.2026 (modal_lock3, самотест колеса `asda.UI.SelfTestModalLock`): лента 2521 — смещение прокрутки, смещение конца и
	//   центр ленты в абсолютных координатах Slate. false — ленты нет или она не отрисована.
	bool DebugLinesScroll(float& OutOffset, float& OutEnd, FVector2D& OutCenter) const;
	// ★ 10.10.2026 (modal_lock3, тот же самотест): прокрутить ленту 2521 в конец (как кнопка 2528 «домой»).
	void DebugLinesToEnd() { OnHomeBtnClicked(); }
	// ★ 12.10.2026 (самотест чата отряда `asda.Party.ChatSelfTest`): ПОСЛЕДНЯЯ строка истории ленты — текст, цвет текста, слот
	//   канала, ник; false — истории нет. Только чтение.
	bool DebugLastLogLine(FString& OutRaw, FColor& OutColor, int32& OutSlot, FString& OutNick) const;
	// ★ 12.10.2026 (тот же самотест): индексы вкладок, чья маска пропускает слот `Slot` (фильтр вкладок ленты), и активная вкладка.
	TArray<int32> DebugTabsShowingSlot(int32 Slot) const;
	int32 DebugActiveTab() const { return ActiveTab; }
	// ★ 12.10.2026 (самотест `asda.Chat.ChannelGateSelfTest`, порядок «гейт канала → антифлуд»): снимок счётчиков антифлуда
	//   `chatAntiFloodCheck` — быстрые отправки, повторы, время последней учтённой отправки, последний учтённый текст, блок.
	//   По нему проверка «отказ канала лимит антифлуда не тратит». Только чтение.
	void DebugAntiFloodSnapshot(int32& OutFast, int32& OutRepeat, double& OutLastSend, FString& OutLastText, bool& bOutBlocked) const
	{
		OutFast = FloodFastCount; OutRepeat = FloodRepeatCount; OutLastSend = FloodLastSend; OutLastText = FloodLastText; bOutBlocked = bFloodBlocked;
	}
	// ★ 12.10.2026 (тот же самотест): обнулить антифлуд и кулдауны каналов 0/1/2 (как снятие блока по таймеру, без строки 7033);
	//   с `bBlocked` = true — поставить блок антифлуда на 60 с (ветка «блок активен → loc 7032»). Только самотест.
	void DebugResetAntiFlood(bool bBlocked);
	// ★ 12.10.2026 (тот же самотест): сколько строк истории ленты содержат `Needle` (Slot ≥ −1 — только строки этого слота;
	//   −2 — любого) — счёт «нотис появился именно сейчас» (до/после). Только чтение.
	int32 DebugCountLogLines(const FString& Needle, int32 Slot = -2) const
	{
		int32 N = 0;
		for (const FChatLogLine& L : LogHistory) { if (L.Raw.Contains(Needle) && (Slot < -1 || L.Slot == Slot)) { ++N; } }
		return N;
	}
	// ★ 12.10.2026 (тот же самотест): адресат шёпота режима 7 — прочитать / выставить (пустой — ветка «никто не услышит»).
	const FString& DebugWhisperTarget() const { return WhisperTarget; }
	void DebugSetWhisperTarget(const FString& Name) { WhisperTarget = Name; }
	// ★ Рантайм-доказательство переноса/центровки подписей справки 2580: класс, autowrap,
	//   WrapTextAt, ширина слота, фактическая ширина отрисованного текста (asda.ChatWin.DumpTip).
	void DumpQueTip() const;
	// ★ ПОЛНЫЙ структурный дамп дерева окна (закон full-structural-analysis-always):
	//   имя / класс / видимость / анкор с пометкой SPAN|POINT / позиция+размер / z / родитель
	//   + строка-СВЕРКА нашего слота ленты со слотом плана импортёра + зоны интерфейса.
	void DumpWidgets() const;

	// ═══════════════════════ ЭТАП 2 — ВВОД И ОТПРАВКА (2026-08-05) ════════════════════════════
	// СОСТОЯНИЯ ВВОДА `CChat[105]` (`inputState`) — таблица эталона, CHAT_SYSTEM_OVERVIEW §7.1.
	//   Приглашение в поле ввода = loc **5316 + state** (`uiQuestInput_UpdatePromptByState`).
	//     0 → 5316 «[По всем каналам]»   отправка ChatInput_SendChannel(0) + полоса антифлуда
	//     1 → 5317 «[Поиск отряда]»      ChatInput_SendChannel(1) + полоса
	//     2 → 5318 «[Торговля]»          ChatInput_SendChannel(2) + полоса
	//     3 → 5319 «[Глобальный чат]»    ChatInput_SendNormal
	//     4 → 5320 «[Общий]»             ChatInput_SendShout
	//     5 → 5321 «[Отряд]»             ChatInput_SendParty   (нужен отряд, иначе loc 6928) — 5105, @0 = индекс отряда (12.10.2026)
	//     6 → 5322 «[Клан]»              chatSendMessage       (нужен клан,  иначе loc 7071)
	//     7 → 5323 «[Шепчемся с %s]»     ChatInput_ParseWhisper → опкод 5088
	//     8 → 5325                       ChatInput_SendWhisperReply (соулмейт)
	//   Комбобокс 2608 переключает ровно эти состояния: channelOrder[] = {4,3,8,6,5,7}.
	//   Клавиатурные команды оригинала (Shortcutkey.ini, таблица UAsdaKeybindConfig):
	//     66 CHAT OPEN CLOSE · 67 CHAT NORMAL · 68 CHAT SHOUT · 69 CHAT ANSWER · 70 CHAT WHISPER
	//     71 CHAT PARTY · 72 CHAT CLAN · 73 CHAT SOULMATE.
	void ToggleInput();                       // 66 — открыть/закрыть строку ввода
	void OpenInput(int32 State);              // 67-73 — открыть ввод сразу в нужном состоянии
	void CloseInput();                        // Esc — отмена
	// ★★★ Открыть ввод с ГОТОВЫМ ТЕКСТОМ — порт `uiChatInputShowWithText@0xBA3E80`.
	void OpenInputWithText(const FString& Text);
	// ★★★ Шёпот, выбранный МЫШЬЮ (кнопка 8655 поиска отряда, меню игрока, меню ника в ленте) —
	//   порт `CChatList_OnSelectWhisper@0xDBB7C0`: в поле ввода встаёт литерал «/w имя ».
	void BeginWhisperTo(const FString& Name);
	void AnswerLastWhisper();                 // 69 CHAT ANSWER — шёпот последнему отправителю
	void NoteWhisperFrom(const FString& Name) { LastWhisperFrom = Name; }   // из приёма 5089
	bool IsInputActive() const { return bInputActive; }
	// ★★★ СВЕРКА ФЛАГА НАБОРА С ФАКТИЧЕСКИМ ФОКУСОМ (см. определение в .cpp). Зовётся из Tick.
	void ReconcileInputFocus();
	void ReconcileClosedInputFocus();   // ★ 06.11.2026: закрытая строка — фокус поля (ключ `asda.Chat.FocusIsTyping`)
	// Дамп «латч против факта» для консоли `asda.ChatWin.InputState`.
	FString DebugInputState() const;
	// Сменить состояние ввода (комбобокс 2608, слэш-команда, хоткей). Обновляет приглашение.
	void SetInputState(int32 State, const FString& WhisperTo = FString());
	int32 GetInputState() const { return InputState; }

	// ★ Запустить кулдаун-полосу в поле ввода (контрол 2618, скин Red_Bar_R).
	//   Причина печатается в лог и в подпись 2619: «кулдаун канала» / «антифлуд» / «повторы» /
	//   «запрещённое слово» / «тест».
	//   ★★★ ОТСТУПЛЕНИЕ ОТ ОРИГИНАЛА: Channel 0..2 = ОТДЕЛЬНЫЙ таймер «широкого» канала
	//   (в игре счётчик один общий, см. блок у ChannelCooldownLeft). INDEX_NONE = общий
	//   кулдаун наших отступлений (антифлуд/мат), он виден в любом режиме.
	void StartCooldown(float Seconds, const FString& Reason, int32 Channel = INDEX_NONE);

	// Имя «широкого» канала 0..2 для логов и подписи 2619.
	static FString WideChannelName(int32 Slot);   // [ПОРТ] 09.10.2026 (мультиязычность, этап 3): строка таблицы (язык сессии)

	// Сводка по списку каналов 2608, слоям «поверх всего» и кулдаун-полосе (asda.ChatWin.DumpCombo).
	FString DebugCombo() const;

	// ★★★ Цвета ВСЕХ подписей окна 2500 — из `EXS TextColor` JUI, а не из `CaptionInfo`
	//   (импортёр кладёт в подпись служебный `ff400040` — тот самый «фиолетовый текст»).
	void ApplyEtalonTextColors();

	// Id зоны в общем реестре = имя окна оригинала.
	static const FName kZoneId;

	// ★ Id «слоя поверх всего» для раскрытого списка каналов 2608 (см. SetChannelListOpen).
	static const FName kComboClaimId;
	// ★ Id «слоя поверх всего» для контекстных меню 80000 / 2670 (одно меню за раз).
	static const FName kMenuClaimId;

	// Тест-обёртки консольных команд (кнопки 2605 и 2537/2589 без мыши).
	void DebugToggleExpand();
	void DebugToggleHelp(bool bShow);
	void DebugSubmit(const FString& Text);      // asda.ChatWin.Say — путь Enter без поля
	void DebugParse(const FString& Text);       // asda.ChatWin.Parse — только разбор

	// ═════════ ЭТАП 3 — КОНТЕКСТНЫЕ МЕНЮ ЧАТА И ИГНОР-ЛИСТ ═══════════════════════════════════
	// ПОЛНЫЙ РЕВЕРС: `Reports/chat_context_menu_and_limits.md` (агент `chatre-agent`, 2026-08-05).
	// Здесь — только то, что нужно знать вызывающему; вся доказательная часть в отчёте и в
	// шапках реализации `Private/UI/AsdaChatContextMenu.cpp`.
	//
	//   • ПКМ по ленте 2521 (событие 15 = правый клик-отпускание, ui_core.c:12505-12509) при
	//     непустом `g_szParsedNameBuf` → окно **CHAT_NEW_TABX 80000** в точке (курсорX+32, курсорY−81);
	//   • ПКМ по панели вкладок 2501 → окно **CHAT_SETUP_Win 2670** РОВНО в точке курсора;
	//   • других ПКМ-веток у окна чата НЕТ (сплошной просмотр всех `case` в `uiChatWindowMsgProc`).

	// ★ ИГНОР-ЛИСТ (эталон: контейнер элементов `char[128]` в `CChat+0x738`).
	//   ЛИМИТА НЕТ, В ФАЙЛ НЕ СОХРАНЯЕТСЯ, НА СЕРВЕР НЕ УХОДИТ — живёт до выхода из игры.
	//   Опкода «заблокировать игрока» нет ни в клиенте, ни в GameServer.
	UFUNCTION(BlueprintPure, Category="Asda|ChatWindow")
	bool IsChatIgnored(const FString& Name) const;
	// Тоггл, ровно как кнопка 80002 (`ui_core.c:12359`): есть в списке → убрать, иначе добавить.
	UFUNCTION(BlueprintCallable, Category="Asda|ChatWindow")
	void ToggleChatIgnore(const FString& Name);
	const TArray<FString>& GetChatIgnoreList() const { return IgnoreList; }
	// ★★★ КАКИЕ ОПКОДЫ ИГНОР ГЛУШИТ.
	//   ЭТАЛОН: `strSetContains` вызывается ровно в ДВУХ приёмниках — `Recv_ChatNormal`
	//   (5085, network.c:10404) и `Recv_WhisperOrSystemMessage` (6561, network.c:10876).
	//   В 5087 / 5089 / 6719 / 5106 / 4121 его НЕТ ⇒ в оригинале «Блокировать чат» НЕ глушит
	//   глобальный чат, шёпот, клан, отряд и системный канал.
	//   ⚠⚠⚠ ОСОЗНАННОЕ ОТСТУПЛЕНИЕ (требование юзера 2026-08-06, Д27 п.2): «сообщения могут
	//   продолжать приходить — нужно, чтобы игнор распространялся на ВСЕ каналы». Поэтому список
	//   расширен до ВСЕХ СЕМИ входящих чат-опкодов. НЕ «чинить» обратно к двум.
	//   ★ Второе условие фильтра — `FAsdaChatLineIn::bSenderAuthored`: глушим только строки,
	//   АВТОРОМ которых является заблокированный игрок. Служебные уведомления шёпота
	//   (5089 msgType 0/2/3 «игрок не в сети») и ЭХО СВОЕГО исходящего шёпота (5089 msgType 1,
	//   flag != 0) авторства собеседника не имеют и не глушатся — иначе пропали бы ответы
	//   на собственные действия игрока.
	static bool IgnoreFiltersOpcode(int32 Opcode)
	{
		return Opcode == 5085 || Opcode == 5087 || Opcode == 5089 || Opcode == 5106
		    || Opcode == 4121 || Opcode == 6561 || Opcode == 6719;
	}
	// Сводка: игнор-лист, состояние обоих меню, ник под курсором (asda.ChatWin.DumpMenu).
	FString DebugContextMenus() const;

	// ★★★ ЛИМИТ ДЛИНЫ СООБЩЕНИЯ — ПАРАМЕТР, А НЕ КОНСТАНТА.
	//   Эталон: `ctrlEditSetTextLength(chatNewEdit, 68)` (chat.c:829) — жёсткий стоп набора
	//   (лимит доводится до Win32-поля через EM_LIMITTEXT), а не обрезка при отправке.
	//   ★ ЗНАЧЕНИЕ ПО УМОЛЧАНИЮ У НАС — **136** (решение юзера 2026-08-05, вдвое от эталона).
	//   ⚠⚠⚠ СЕРВЕР: в трёх обработчиках чата (5084 `Recv_NormalChatClient@0x547030`,
	//   5086 `recvGlobalChatClient@0x547670`, 4120 `sub_403F50@0x403F50`) `memcpy` в `char[80]`
	//   идёт ДО проверки длины ⇒ сообщение длиннее 80 байт РОНЯЕТ GameServer. 136 безопасно
	//   ТОЛЬКО с установленным серверным патчем **P14** (`chat_message_length_x2`, лимит 160).
	//   Переключение БЕЗ ПЕРЕСБОРКИ — консольная переменная `asda.Chat.InputMaxChars`
	//   (в игре: `asda.Chat.InputMaxChars 68`; постоянно: DefaultEngine.ini [SystemSettings]).
	static int32 GetInputMaxChars();
	static void SetInputMaxChars(int32 NewLimit);
	// ★★★ ДЕЙСТВУЮЩИЙ лимит для ТЕКУЩЕГО режима ввода. Отличается от общего ровно в одном месте:
	//   ★ КАНАЛ АРМИИ/ФРАКЦИИ (режим 9, опкод **6718**) ОСТАЁТСЯ НА 80 БАЙТАХ.
	//   Причина в самом протоколе, а не в патче: 6718 — пакет ФИКСИРОВАННОЙ длины 252 с текстовым
	//   полем ровно 200 б (`memcpy 0xC8`), ПОДПИСАННЫЙ MD5 (`Net_Md5Digest16(&g_NetMd5Salt, pktBody, 0xE4, md5)`),
	//   а сервер пересылает его дальше опкодом 6719 пакетом ФИКСИРОВАННОЙ длины 101 с записью
	//   `char[80]` и проверкой `sub_523F00(pkt, 0x6C, …)` ровно 108 байт вместе с подписью.
	//   ⇒ увеличить текст в этом канале = сломать и подписываемый блок на клиенте, и проверку на
	//   сервере. Серверный патч **P14 этот канал сознательно НЕ трогает** (см. §7.5 п.2 реверса),
	//   поэтому ограничение доводится КЛИЕНТСКОЙ проверкой — здесь.
	UFUNCTION(BlueprintPure, Category="Asda|ChatWindow")
	int32 GetActiveInputMaxChars() const;
	// ★ [ПОРТ] Ревизия arabic_text_REVIEW С3 (08.10.2026): укладывается ли текст ввода S в лимит активного режима в БАЙТАХ
	//   ПРОВОДА. В клан-чате (режим 6) при «Друг» дополнительно префикс loc 6937 + текст ≤ 160 б (P14): русский префикс с
	//   арабским текстом — смешанная строка, она уходит UTF-8 (2 б/букву), и посимвольный счёт молча терял до половины реплики.
	bool IsWithinActiveInputLimit(const FString& S) const;
	// Самая длинная голова S по границе символа, которая укладывается в `IsWithinActiveInputLimit`.
	FString ClampToActiveInputLimit(const FString& S) const;
	// Лимит канала армии (режим 9): серверная запись 6719 — `char[80]`; ★ 31.10.2026: 79 байт провода + ноль-терминатор в поле
	//   [28..107] тела 6718 (`User_Game_RegistCancel_2`, S:41990 — `strcpy` поля 80 с терминатором).
	static constexpr int32 kInputMaxCharsArmy = 79;
	// ★ 31.10.2026: «категория ленты» отказа канала = однокнопочное окно (каналы 0/1/2 в бою — loc 10222), см. `SendCurrentChannel`.
	static constexpr int32 kDenyMsgBox = -2;

	// ★★★ ЧИСЛА ЛИМИТА. `kInputMaxCharsEtalon` — значение ОРИГИНАЛА (chat.c:829) и безопасное
	//   значение отката на стоковом сервере; `kInputMaxCharsDefault` — наше умолчание (вдвое);
	//   `kInputMaxCharsCeiling` — потолок клиентских буферов (кламп send-путей эталона = 199).
	//   Действующее значение читается ТОЛЬКО через GetInputMaxChars().
	static constexpr int32 kInputMaxCharsEtalon  = 68;
	static constexpr int32 kInputMaxCharsDefault = 136;
	static constexpr int32 kInputMaxCharsCeiling = 199;

	// Описание одной кнопки всплывающего меню (публично: таблицы кнопок 80000/2670 живут в
	//   Private/UI/AsdaChatContextMenu.cpp). Скины `SmWtBtn_W143` / `SmGyBtn_W143` /
	//   `SmGyBtn_W143_2` — все три Type = "PushButton", по ТРИ состояния SS0/SS1/SS2
	//   (обычная / наведение / нажатие), атлас `NE_cooltime_mini.tga`. Различаются только
	//   Diffuse-тинтом и цветом подписи — таблица скинов в .cpp.
	struct FAsdaPopupBtn
	{
		const TCHAR* Node = nullptr;    // имя узла-плиты в дереве виджетов (подпись = <Node>_Text)
		int32 IdNum = 0;                // idnum контрола в JUI (для логов и дампа)
		int32 LocId = 0;                // loc-id подписи (0 = текст ставит код)
		const TCHAR* Skin = nullptr;    // ключ скина в kBtnSkins
		float X = 0.f, Y = 0.f;         // позиция внутри окна, базовые px (из gamemain_jui.ini)
		bool  bMarkupCaption = false;   // ★ цвет задан РАЗМЕТКОЙ <FC=..> в самой loc-строке
	};

	// Тест-обёртки меню без мыши (asda.ChatWin.NickMenu / asda.ChatWin.TabMenu / asda.ChatWin.Ignore).
	void DebugShowNickMenu(const FString& Name);
	void DebugShowTabMenu();
	// ★ 10.10.2026: меню вкладок 2670 показано (видимость слоя; её же спрашивает проход ESC через `RegisterEscPopup`).
	bool IsTabMenuShown() const;
	// ★ 07.11.2026 тест-обёртки самотеста щелчка мимо меню.
	void DebugHideContextMenus() { HideNickMenu(); HideTabMenu(); }           // закрыть оба меню
	void DebugSetChannelListOpen(bool bOpen) { SetChannelListOpen(bOpen); }   // раскрыть или свернуть список каналов
	bool DebugIsChannelListOpen() const { return bComboOpen; }                // список каналов раскрыт

private:
	// Одна строка ленты чата (данные, переживают травел). Объявлена ПЕРВОЙ в приватной секции:
	//   на неё опираются и раскладка ленты, и блок контекстных меню (перекраска выделенной строки).
	struct FChatLogLine
	{
		FString Raw;                              // готовый текст строки (с префиксом канала, если он был)
		FColor  Color = FColor::White;            // цвет текста = цвет слота канала
		int32   Slot = -1;                        // слот канала (−1 = служебный)
		FColor  RowBg = FColor(0, 0, 0, 0);       // заливка фона строки (ранговый цвет)
		bool    bRowBg = false;                   // флаг заливки (эталонное поле строки +3108)
		// ★ ДОБАВЛЕНО 2026-08-06 (Д21 п.6). ОБА поля — вне `Raw`, чтобы НЕ ломать эталонный
		//   разбор ника «между первыми […]» (Д20.2) и hover-плашку под ником.
		FString Nick;                             // имя отправителя ИЗ ПАКЕТА (пусто = служебная строка)
		FString ChanPrefix;                       // «[Общ.]» — рисуется ПЕРЕД Raw только в разметке
		// Слот, чьим цветом красятся префикс и обводка сообщения. Обычно = Slot; отличается
		//   ровно в одном месте — клан-чат 6719 (см. AddChatLineRaw, параметр PrefixSlotOverride).
		int32   PrefixSlot = -1;
		// ★ ДОБАВЛЕНО 2026-08-06 (дубль оповещений в ленту). СТРОКА НЕ НАПИСАНА ИГРОКОМ —
		//   это анонс/нотис/системный дубль (см. AddAnnounceLine). Идейно то же, что
		//   `FAsdaChatLineIn::bSenderAuthored` у входящих чат-пакетов, но для УЖЕ ЛЕЖАЩЕЙ строки.
		//   Запрещает эталонный фолбэк `ParseNickFromLine`: в анонсе ПЕРВЫЕ скобки — это НАЗВАНИЕ
		//   ПРЕДМЕТА («… [ Меч Дьявола ] +45%»), и без флага к нему прицепились бы ник, hover-плашка,
		//   ПКМ-меню 80000 и «Блокировать чат» (в игнор ушло бы название предмета).
		bool    bNoNick = false;
	};

	// ── Жизненный цикл ──
	bool EnsureWidgets();
	void ResetWidgets();
	void RegisterZone();
	void UnregisterZone();

	// Построить панель строк внутри импортированного WBP на rect ленты 2521.
	class UScrollBox* MakeLinesPanel();
	// Перестроить виджеты-строки из пережившей травел истории.
	void RebuildLines();
	// Добавить одну строку-виджет в панель + вытеснить самую старую при переполнении.
	//   L.bRowBg → строка заворачивается в UBorder с заливкой L.RowBg (эталонная подложка ранга).
	//   Одновременно строка кладётся в VisibleLines — параллельный ленте список данных.
	void PushLineWidget(const FChatLogLine& L);

	// ── Мышь (единый хит-тест по геометрии: работает с ЛЮБЫМ классом виджета) ──
	//   ГРАБЛЯ №1/№2 (стоили 6 кругов на окне 2570): контролы, добавленные пользователем вручную
	//   в WBP-редакторе, могут быть ЛЮБОГО класса (у 2570 это оказались UImage, а не UButton),
	//   а у UImage нет OnPressed и SImage не поглощает клик. Поэтому никаких Cast<UButton> при
	//   поиске узлов и никакой зависимости от доставки событий Slate.
	void TickPointer(float DeltaTime);
	bool IsUnderCursor(const class UWidget* W, const FVector2D& CursorAbs) const;
	// ★ Экранный курсор → локальные (базовые) координаты корневого канваса окна: учитывает и
	//   DPI-масштаб, и ПОЛОЖЕНИЕ ОКНА ИГРЫ. Прежнее `Cur / GetViewportScale()` теряло второе и в
	//   оконном режиме уводило всплывающие меню от курсора (живой тест 2026-08-06).
	FVector2D ScreenToWindowBase(const FVector2D& ScreenPos) const;
	// Узел окна по имени среди ВСЕХ UWidget: точное имя, затем вхождение подстроки. Тип НЕ предполагаем.
	class UWidget* FindNodeByName(const TCHAR* Exact, const TCHAR* Needle) const;
	// ★ Перенесено в ассет (15.09, разрешение владельца): NormalizeControlSlot удалён — слоты WBP_CHAT_NEW уже POINT.

	// Прямоугольник окна в АБСОЛЮТНЫХ экранных координатах — ТОЛЬКО по узлам, которые окно и
	//   образуют (подложка/рамка/контейнер ленты). Объединение ВСЕХ узлов брать НЕЛЬЗЯ: один
	//   случайно растянутый пользователем контрол раздует зону на весь экран (грабля №4).
	bool GetWindowScreenRect(FVector2D& OutMin, FVector2D& OutMax) const;

	// Прямоугольник РАСКРЫТОГО списка каналов 2608 в абсолютных экранных координатах.
	//   Нужен отдельно: список выходит ВВЕРХ за корпус окна и в GetWindowScreenRect не входит.
	bool GetComboListScreenRect(FVector2D& OutMin, FVector2D& OutMax) const;

	// ★★★ ПЕРЕТАСКИВАНИЕ ОКНА УБРАНО ПОЛНОСТЬЮ (требование юзера 2026-08-05).
	//   Было: зона CHAT_NEW_TextMove (2534) таскала окно, а `ApplyWindowOffset` двигал корневой
	//   слот WBP. Убрано и состояние (WinOffset / DragStart*), и обработчик, и клампы — окно
	//   всегда стоит там, где велит раскладка, низ закреплён. Узел 2534 остался ровно с той
	//   видимостью, что даёт эталон, но мышь больше не ловит.
	//   Единственный способ менять размер — растягивание ВВЕРХ (кромка ниже) и кнопка 2605.
	//   Создать невидимую зону захвата верхней кромки (в WBP её нет — это добавка по требованию
	//   юзера, а не выдуманный элемент интерфейса: ничего не рисует, только ловит мышь).
	void EnsureResizeHandle();

	// ★★★ ЕДИНАЯ РАСКЛАДКА ОКНА ПО ПРАВИЛАМ JUI (аудит 2026-08-05).
	//   Все узлы окна выводятся ИЗ ТРЁХ ЧИСЕЛ — левый край L, верх T и высота H окна 2500 —
	//   по режимам Align* из gamemain_jui.ini (проверено: при L=1,T=580,H=178 таблица совпадает
	//   с фактическими слотами импортированного WBP до десятых). Отсюда:
	//     • перетаскивание = смена L/T (★ РАНЬШЕ двигали слот САМОГО WBP: он превращался из
	//       полноэкранного в 474×178, и все дети, у которых анкор «низ экрана» + пиксельный
	//       оффсет, пересчитывались от высоты 178 вместо 768 → всё содержимое подскакивало
	//       на −10 px по Y и +1 px по X. Это был реальный баг, теперь корень не трогаем);
	//     • свернуть/развернуть (2605) = смена H (170 ↔ 340) и T (580 ↔ 410), низ окна на 750.
	//   Строка таблицы раскладки: имя узла в WBP + прямоугольник в базовых px.
	//   bLocal = узел лежит внутри группы (координаты локальные), иначе экранный слот.
	struct FChatLayoutEntry
	{
		const TCHAR* Name = nullptr;
		bool  bLocal = false;
		float X = 0.f, Y = 0.f, W = 0.f, H = 0.f;
	};
	// Эталонная раскладка окна для заданных L/T/H — ОДИН источник и для применения, и для аудита.
	static void BuildLayoutTable(float L, float T, float H, float W, FVector2D TipOff, TArray<FChatLayoutEntry>& Out);
	void ApplyWindowLayout(float L, float T, float H, float W);
	// ★ Зона захвата ПРАВОЙ кромки — горизонтальное растягивание (см. разбор у kWinMaxW).
	void EnsureResizeRight();
	float BaseWidth() const;
	// Пересчёт ширины переноса строк ленты после изменения ширины окна.
	void ApplyLinesWidth();              // ширина окна с учётом растягивания вправо
	void RefreshLayout();                 // пересчитать раскладку из текущего состояния
	float BaseTop() const;                // верх окна без учёта перетаскивания
	float BaseHeight() const;             // высота окна без учёта перетаскивания

	void ScrollByLines(int32 Lines);      // листание ленты (шаг — наш, см. kScrollStep)
	float MeasureLineHeight() const;      // фактическая высота строки (десайр-сайз первой)
	UFUNCTION() void OnHomeBtnClicked();  // 2528 — прокрутка в конец + возврат автоскролла
	void OnUDbtnClicked();                // 2605 — uiToggleExpandLayout 1:1
	void SetQueTipVisible(bool bVisible); // 2580 — окно справки кнопки 2537
	void BuildQueTipTexts();              // loc 5400-5406 в узлы ассета TXT00…06 (контролы 2581…2586, 2588)
	// ── ЭТАП 2: ввод ──
	void BuildInputWidgets();                 // поле 2609, приглашение, полоса антифлуда
	UFUNCTION() void OnInputCommitted(const FText& Text, ETextCommit::Type CommitMethod);
	// ★ ЖЁСТКИЙ СТОП НАБОРА — аналог EM_LIMITTEXT, который эталон ставит полю 2609
	//   (`ctrlEditSetTextLength` → SendMessageA(hEdit, EM_LIMITTEXT, лимит, 0), ui_core.c:116681).
	//   В UMG у UEditableText лимита нет, поэтому обрезаем в обработчике изменения текста:
	//   набрать больше GetInputMaxChars() физически невозможно, как и в оригинале.
	UFUNCTION() void OnInputChanged(const FText& Text);
	void SubmitInput(const FString& Raw);     // Enter — разбор + отправка
	bool ParseSlashCommand(FString& InOut);   // 1:1 ChatInput_ParseCommand: «/X …» → смена состояния
	bool AntiFloodAllows();                   // chatAntiFloodCheck: можно ли слать по каналам 0/1/2
	void TickAntiFlood(float Dt);             // тик антифлуда и кулдаун-полосы
	void ApplyCooldownBar();                  // перерисовать полосу 2618 и подпись 2619
	void NoteSentText(const FString& Text);             // учёт повтора для антифлуда
	// Отправка по текущему режиму ввода. true = пакет реально ушёл в сокет (нужно вызывающему,
	//   чтобы поднять кулдаун-полосу ровно там же, где это делает эталон — ПОСЛЕ отправки).
	bool SendCurrentChannel(const FString& Text);
	// ★ Проверка доступности канала 1:1 с chat.c:3860-3970; OutDenyText — эталонная loc-строка.
	//   ★ 12.10.2026 [ОРИГИНАЛ]: OutFeedCategory ≥ 0 — отказ печатается НЕ в системный журнал, а строкой ленты этой категории
	//   (`Chat_AddChatLine(chat, кат., …)`): шёпот без адресата (кат. 5, loc 6943), «Друг» без пары/имени (кат. 101, loc 6929).
	bool ChannelAllowed(int32 State, FString& OutDenyText, bool& bOutTopWindow, int32* OutFeedCategory = nullptr) const;
	// ★ 12.10.2026 [ОРИГИНАЛ]: канал вообще не проходит антифлуд у эталона (клан 6 — `chatSendMessage`, армия 9 —
	//   `Clan_Send_SetNotice`: вызова `chatAntiFloodCheck` в них нет). Ключ отката `asda.Chat.ChannelGateLegacy 1` → false.
	bool ChannelSkipsAntiFlood(int32 State) const;
	void BuildChannelCombo();             // 2608 — кнопка и раскрывающийся список каналов ПО СКИНАМ
	// ── ПАНЕЛЬ ВКЛАДОК 2501: 7 вкладок × маска каналов, ряд из 5 + стрелки листания ──
	void BuildChatTabs();
	void RefreshChatTabs();
	// ★★★ СКОЛЬКО ВКЛАДОК ВЛЕЗАЕТ В ТЕКУЩУЮ ШИРИНУ ОКНА — ЭТО ЭТАЛОННАЯ ФОРМУЛА, а не выдумка.
	//   `CChat_BuildTabUI` (chat.c:795) зовёт `uiControlCountFittingChildren(radio, 0, ширина_окна − 10)`,
	//   а тело функции (ui_core.c:115677-115722) накапливает ширины детей и увеличивает счётчик,
	//   пока сумма не превысила лимит ⇒ при плитке 79 px это floor((W − 10) / 79).
	//   При авторских 474 даёт 5 — ровно то, что видно на скриншоте оригинала. Единственное
	//   отличие порта: W у нас МЕНЯЕТСЯ (горизонтальный ресайз — наше отступление), поэтому
	//   счёт пересчитывается на каждом шаге растягивания, а не один раз при постройке.
	int32 VisibleTabCount() const;
	void SelectChatTab(int32 Tab);
	void ScrollChatTabs(int32 Delta);
	int32 TabHitTest(const FVector2D& Cur) const;

	// ══════════════ ★★★ КОЛЕСО МЫШИ НАД СПИСКАМИ (осознанное отступление, 2026-08-06) ═══════════
	// ЭТАЛОН отдаёт колесо интерфейсу ПЕРВЫМ (`CWindowMgr_QueueMouseWheel`, запись типа 7,
	//   game_main.c:5871), но что именно делает с ним список — из разобранного среза не следует;
	//   у окон 2500/2590 листание описано ТОЛЬКО кнопками-стрелками и ползунками (2536/2598/2639).
	//   Поэтому прокрутка колесом — НАШЕ добавление по требованию юзера, с шагом = шагу стрелок
	//   (kScrollStep строк у ленты, 1 позиция у списков вкладок/каналов). Мир колесо не получает:
	//   `AAsdaPlayerController::OnWheel` спрашивает реестр окон ДО зума камеры.
	bool OnWheelChatWindow(int32 Notches);   // окно чата 2500: лента 2521 и ряд вкладок 2501
	bool OnWheelTabEditor(int32 Notches);    // редактор вкладок 2590: список 2597 и каналы 2639
	// ★★★ ВКЛАДКИ СТАЛИ ДАННЫМИ, А НЕ КОНСТАНТАМИ (2026-08-06). Раньше маска и подпись жили в двух
	//   `static const` таблицах — из-за этого пункты меню 2670 («Добавить/Удалить/Изменить
	//   заголовок/Настройки») физически не могли ничего изменить. Теперь это `ChatTabs`, точный
	//   аналог `CChat::tabRecordList` эталона (запись = имя + маска каналов).
	int32   TabMask(int32 Tab) const;    // маска каналов (биты = слоты 0…9)
	int32   TabLocId(int32 Tab) const;   // loc-id подписи; 0 = пользовательская вкладка
	FString TabName(int32 Tab) const;    // готовая подпись (loc или пользовательское имя)
	void    InitDefaultTabs();           // семь эталонных вкладок (uiChatOpt_InitItems)
	void    InitDefaultChanOpts();       // 10 опций каналов из шаблона игры (имя loc + цвет)

	// ═══════════ ЭТАП 4: РЕДАКТОР ВКЛАДОК 2590, ПАЛИТРА 2700, ПЕРСИСТ ChatOpt.bin ═══════════════
	// ПОЛНЫЙ РЕВЕРС: `Reports/chat_tab_editor_2590_re.md`. Здесь — только то, что нужно знать
	//   вызывающему; доказательная часть в отчёте и в шапках `Private/UI/AsdaChatTabEditor.cpp`.
public:
	// Операции над вкладками — ровно те, что делают пункты меню 2670.
	//   Все ЧЕТЫРЕ мутируют РАБОЧИЕ буферы либо живые данные точно как эталонные функции
	//   `chatAddFilterEntry` / `chatMacro_setShortcutText` / `chatTabRemoveSelected` / `chatInitFilterTabs`.
	bool AddChatTab(const FString& Name);        // новая вкладка, маска 0 (эталон: буфер обнулён)
	// ★★★ ЦЕЛЕВАЯ ВКЛАДКА ОПЕРАЦИИ.
	//   ЭТАЛОН берёт `self[108]` = АКТИВНУЮ вкладку и для «Изменить заголовок» (chat.c:2119
	//   `stl_vectorAt_1002(self + 344, self[108])`), и для «Удалить вкладку» (chat.c:2110
	//   `stl_vectorAt_1002(self + 340, self[108])`). Правый клик вкладку НЕ выбирает: список
	//   пишет индекс выбранного (`CListBox+0x51C`) только по ЛЕВОМУ отпусканию —
	//   `uiList_OnMouseUpSelect @0x12EB9F0` начинается с `if (!a2)`, где a2 = номер кнопки мыши.
	//   ⚠⚠⚠ ОСОЗНАННОЕ ОТСТУПЛЕНИЕ (требование юзера 2026-08-06, Д27 п.5): действия применяются
	//   к ТОЙ вкладке, по которой вызвано меню 2670 (`TabMenuTargetIdx`), а не к активной.
	bool RenameTab(int32 Tab, const FString& Name);
	bool RemoveTab(int32 Tab);                   // последнюю оставшуюся удалить нельзя (эталон tabCount>1)
	bool RenameActiveTab(const FString& Name) { return RenameTab(ActiveTab, Name); }
	bool RemoveActiveTab()                    { return RemoveTab(ActiveTab); }
	void ResetTabsToDefault();                   // сброс к семи эталонным (chatInitFilterTabs)
	// Открыть/закрыть редактор вкладок 2590 (эталон `uiRefreshSkillSlotList(this, 1)` = снимок + показ).
	//   ★ TargetTab ≥ 0 — открыть СРАЗУ на этой вкладке (эталон открывает на `stateFlags1`, то есть
	//     на последней выбранной в списке 2597; отступление — см. RenameTab).
	void ShowTabEditor(bool bSnapshot, int32 TargetTab = INDEX_NONE);
	void HideTabEditor();
	// Персист `./ChatOpt.bin` (эталон CChat_Save/LoadChatOpt). Формат — см. .cpp.
	void SaveChatOpt() const;
	bool LoadChatOpt();
	// ★★★ ЛИМИТ ДЛИНЫ ИМЕНИ ВКЛАДКИ — **15 СИМВОЛОВ, РЕШЕНИЕ ЮЗЕРА** (2026-08-06), сознательное
	//   отступление от эталона. Эталон проверяет `strlen(Str) <= 0xA` = 10 БАЙТ (в CP949 это 5
	//   корейских слогов, отсюда «5» в подсказке loc 5338). В CP1251 — кодировке нашего клиента —
	//   каждый представимый символ ровно один байт, поэтому 15 символов кириллицы = 15 байт и
	//   спокойно укладываются в эталонное поле записи `char name[20]` (19 + завершающий 0).
	//   ⚠ Цифра в подсказке/сообщении об отказе приводится к правде — см. `TabNameLimitText`.
	static constexpr int32 kTabNameMaxChars = 15;
	// Текст loc 5338 с приведённой к правде цифрой лимита (используют И подсказка диалога 1580,
	//   И сообщение об отказе в ленте — иначе игрок видит «5», а действует другое число).
	FString TabNameLimitText() const;
	// Диагностика ЭТАПА 4 (asda.ChatWin.DumpTabs): вкладки, маски, цвета каналов, состояние редактора.
	FString DebugTabs() const;
	// ★★★ Д28: сквозная проверка ЦЕПОЧКИ ЦВЕТА КАНАЛА без участия UI.
	//   DebugChanColors — живая и рабочая таблицы + сколько строк ленты красится каждым слотом.
	FString DebugChanColors() const;
	//   SetChanColorDirect — записать цвет прямо в ЖИВУЮ запись `tabFilterOpt[slot]` (наш ChanOpts),
	//   сохранить файл и перекрасить ленту. Ровно то же, что делает ОК окна 2590, но адресно:
	//   доказывает, работает ли рендер-путь, отдельно от пути «палитра → буфер → коммит».
	bool SetChanColorDirect(int32 Slot, FColor Color);
	// ★ Полный структурный разбор ОКНА 2590 в рантайме (asda.ChatWin.DumpTabEditor): имя, класс,
	//   видимость и слот КАЖДОГО узла. Headless-дамп дерева WBP невозможен — свойство `WidgetTree`
	//   в UE-Python защищённое (проверено 2026-08-06), поэтому разбор снимается только так.
	FString DebugTabEditorWidgets() const;
	// Тест-обёртки без мыши.
	void DebugAddTab(const FString& Name)    { AddChatTab(Name); }
	void DebugRenameTab(const FString& Name) { RenameActiveTab(Name); }
	void DebugRemoveTab()                    { RemoveActiveTab(); }
	void DebugShowTabEditor()                { ShowTabEditor(true); }

private:
	// Фабрики виджетов ряда и досоздание плиток под текущее число вкладок.
	// ★ Host — окно-хозяин: ОДНИ И ТЕ ЖЕ фабрики обслуживают и ряд вкладок 2501 в окне чата, и
	//   столбец клон-ячеек списка 2597 в редакторе 2590 (второго механизма не заводим).
	//   Parent — панель, в которую класть виджет; nullptr = корневой канвас окна.
	class UImage*     MakeTabQuad(class UUserWidget* Host, class UPanelWidget* Parent, const TCHAR* Name, int32 Z);
	class UTextBlock* MakeTabCap(class UUserWidget* Host, class UPanelWidget* Parent, const TCHAR* Name, int32 Z);
	void              EnsureTabWidgets(int32 N);
	void RefreshChannelCombo(int32 HoverIdx);   // перерисовать подпись и строки (наведение)
	// ★ Центровка подписи выбранного канала по телу комбобокса 2608 — ЕДИНСТВЕННОЕ место, где
	//   задаётся её геометрия (иначе любой путь обновления подписи центровку терял, живой тест
	//   2026-08-06). Слот AutoSize + Alignment(0.5,0.5) в центр области ⇒ точный центр по обеим осям.
	//   ★★★ ЖИВОЙ ТЕСТ 2026-08-06 (третий заход, «подпись съезжает»): ПРИЧИНА была не в тексте, а в
	//   РАЗНЫХ СИСТЕМАХ КООРДИНАТ. Тело комбобокса `CHAT_NEW_SSbtn_29_0` — ЛОКАЛЬНЫЙ ребёнок группы
	//   `SizeWin_grp`, которую `BuildLayoutTable` кладёт в `T + H − 170`; подпись же висела на
	//   КОРНЕВОМ канвасе по АБСОЛЮТНОМУ `kComboY = 726`. Совпадает это лишь при нетронутой раскладке
	//   (T + H = 758); после кнопки 2605/растягивания низ окна = 750 ⇒ тело уезжает на 8 px вверх,
	//   а подпись остаётся ⇒ «съехала». Лечение: подпись живёт В ТОЙ ЖЕ ГРУППЕ и в тех же локальных
	//   координатах, что и тело, — тогда ЛЮБОЕ изменение раскладки двигает их вместе, без пересчёта.
	void ApplyComboLabelSlot(class UCanvasPanelSlot* CS) const;
	// ★ ЕДИНАЯ точка геометрии комбобокса: подпись + раскрытый список. Зовут ВСЕ пути, включая
	//   ApplyWindowLayout (кнопка 2605, обе кромки ресайза, восстановление размера, загрузка).
	void ApplyComboGeometry();
	// Текущий АБСОЛЮТНЫЙ Y тела комбобокса 2608 (для раскрывающегося списка, который живёт на
	//   корневом канвасе и в группу не помещается — он выше её верхней кромки).
	float ComboBodyAbsY() const;
	void SetChannelListOpen(bool bOpen);        // раскрыть/свернуть список
	void SelectChannelIndex(int32 Idx);         // выбрать пункт (channelOrder[] = {4,3,8,6,5,7})
	// ★ Перетаскивание окна справки 2580 — ОТСТУПЛЕНИЕ ОТ ЭТАЛОНА по требованию юзера
	//   (2026-08-05). В оригинале 2580 не двигается: у него нет ни зоны-«ручки» вроде 2534,
	//   ни обработчика перемещения — окно просто показывается на своём месте.
	void ApplyQueTipOffset(FVector2D NewOffset);

	// ══════════ ЭТАП 3: КОНТЕКСТНЫЕ МЕНЮ 80000 / 2670 И HOVER НИКА В ЛЕНТЕ 2521 ═══════════════
	// Построение (один раз на экземпляр окна) и показ/скрытие обоих меню.
	void BuildContextMenus();
	class UCanvasPanel* BuildPopupWindow(const TCHAR* Name, float W, float H,
	                                     const TCHAR* MoveNode, float MoveW, float MoveH,
	                                     const FAsdaPopupBtn* Btns, int32 NumBtns);
	void ShowNickMenu(const FString& Name, const FVector2D& BasePos);
	void HideNickMenu();
	// ★ TargetTab — вкладка, ПО КОТОРОЙ вызвано меню (см. TabMenuTargetIdx). −1 = попали не по
	//   вкладке (стрелки прокрутки) ⇒ цель = активная вкладка, как в эталоне.
	void ShowTabMenu(const FVector2D& BasePos, int32 TargetTab = INDEX_NONE);
	void HideTabMenu();
	// Подпись кнопки 80002 = loc 111943 / 111944 по факту наличия ника в игноре (ui_core.c:13571).
	void RefreshNickMenuBlockCaption();

public:
	// ★ ЗАМОК АВТОПРОКРУТКИ 2607 (флаг CChat+497). Тумблер, см. поле bScrollFrozen.
	UFUNCTION(BlueprintCallable, Category="Asda|ChatWindow")
	void SetScrollFrozen(bool bOn);
	UFUNCTION(BlueprintPure, Category="Asda|ChatWindow")
	bool IsScrollFrozen() const { return bScrollFrozen; }
private:
	// Привести кадр чекбокса 2607 к текущему значению флага (SS0/SS1 ↔ SS2/SS3).
	void RefreshScrollLockVisual();
	// Поставить окно меню в точку (базовые px) — общая для 80000 и 2670.
	void PlaceMenu(class UCanvasPanel* Menu, const FVector2D& BasePos, float W, float H);
	// Состояние скина кнопок (наведение/нажатие) для одного меню.
	void RefreshPopupBtnStates(class UCanvasPanel* Menu, const FAsdaPopupBtn* Btns, int32 Num,
	                           const FVector2D& Cur, bool bDown);
	// То же сразу для обоих меню (таблицы кнопок живут в AsdaChatContextMenu.cpp). Зовётся из TickPointer.
	void RefreshMenuHover(const FVector2D& Cur, bool bDown);
	// Нажатие по меню. true = нажатие поглощено меню (дальше по цепочке TickPointer не идти).
	bool HandleMenuMouseDown(const FVector2D& Cur);
	// Отпускание: команда пункта выполняется, если отпустили над той же кнопкой (как WM_COMMAND).
	void HandleMenuMouseUp(const FVector2D& Cur);
	// Перетаскивание меню за зону 80009 / 2675.
	void TickMenuDrag(const FVector2D& Cur, float UiScale);
	// ★ HOVER НИКА В ЛЕНТЕ 2521 + разбор ника из разметки строки (см. .cpp, §2 отчёта).
	void TickRibbonHover(const FVector2D& Cur);
	// Ник = текст между ПЕРВЫМИ '[' и ']'; OutPlain = всё, что было ДО '[' (для левой границы).
	static FString ParseNickFromLine(const FString& Raw, FString& OutPlain);
	// Ширина отрисованного текста в БАЗОВЫХ px (разметка снимается, кегль kFontPx) — аналог
	//   `textFontMeasureMarkupString`.
	float MeasureMarkupWidth(const FString& S) const;
	// Разметка одной строки ленты (общая для добавления и для перекраски при выделении).
	FString BuildLineMarkup(const FChatLogLine& L, bool bSelected) const;
	// Перекрасить строку ленты по индексу (выделение/снятие выделения) — эталон красит текст
	//   выбранной строки цветом `item[+2760] | 0xFF000000`.
	void RepaintRibbonLine(int32 Idx);
	// Прямоугольник строки ленты в АБСОЛЮТНЫХ экранных координатах.
	bool GetRibbonLineRect(int32 Idx, FVector2D& OutMin, FVector2D& OutMax) const;
	// Три плиты подсветки ленты (создаются один раз вместе с лентой).
	void EnsureRibbonHighlights();
	// Прямоугольник ОТКРЫТОГО меню в абсолютных экранных координатах (зона перехвата клика).
	bool GetMenuScreenRect(FVector2D& OutMin, FVector2D& OutMax) const;

	UPROPERTY(Transient) TObjectPtr<class UCanvasPanel> NickMenu;   // CHAT_NEW_TABX (80000)
	UPROPERTY(Transient) TObjectPtr<class UCanvasPanel> TabMenu;    // CHAT_SETUP_Win (2670)
	// Плиты подсветки в ленте 2521. Эталон рисует ДО ТРЁХ квадов за кадр:
	//   строка под курсором (hover), ВЫБРАННАЯ строка (инверсия её цвета) и вторая плашка
	//   ровно под ником в hover-строке. Здесь — по виджету на каждый.
	UPROPERTY(Transient) TObjectPtr<class UImage> RibbonRowBg;    // hover-строка   0x96F0F0F0
	UPROPERTY(Transient) TObjectPtr<class UImage> RibbonSelBg;    // выбранная строка ~цвет строки
	UPROPERTY(Transient) TObjectPtr<class UImage> RibbonNameBg;   // плашка ПОД НИКОМ 0x96F0F0F0
	// Строки, реально лежащие в ленте (параллельно детям LinesBox) — нужны для разбора ника
	//   под курсором и для перекраски выделенной строки.
	TArray<FChatLogLine> VisibleLines;
	// ★ Аналог `g_szParsedNameBuf`: ник в строке ПОД КУРСОРОМ. Пусто ⇒ ПКМ ничего не открывает.
	FString RibbonHoverNick;
	FString RibbonHoverPlain;            // текст до '[' (левая граница плашки ника)
	int32   RibbonHoverIdx = -1;         // строка под курсором (эталон CListBox +0x518)
	int32   RibbonSelIdx   = -1;         // ВЫБРАННАЯ строка (эталон CListBox +0x51C)
	// ★ Аудит №3 (B3): фронт ПКМ (событие 15 = отпускание) больше не отдельный латч — он в
	//   `Mouse` (см. блок «Состояние» ниже): ЛКМ и ПКМ опрашивал ОДИН `TickPointer`.
	// Перетаскивание меню за зону 80009 / 2675 (JUI-событие EVT 6 → RET 20003).
	class UCanvasPanel* MenuDragTarget = nullptr;
	FVector2D MenuDragStartMouse = FVector2D::ZeroVector;
	FVector2D MenuDragStartPos   = FVector2D::ZeroVector;
	FVector2D NickMenuPos = FVector2D::ZeroVector;   // позиция меню 80000 в базовых px
	FVector2D TabMenuPos  = FVector2D::ZeroVector;   // позиция меню 2670 в базовых px
	// ★ ИГНОР-ЛИСТ. Эталон: контейнер `char[128]` в `CChat+0x738`; лимита нет, дубликатов нет
	//   (добавление всегда через тоггл, который сначала делает `strSetContains`).
	TArray<FString> IgnoreList;
	int32 MenuPressedBtn = -1;           // кнопка, на которой началось нажатие (состояние SS2)

	// ── Виджеты (пересоздаются после травела) ──
	UPROPERTY(Transient) TObjectPtr<UUserWidget>       ChatWidget;   // WBP_CHAT_NEW (2500)
	UPROPERTY(Transient) TObjectPtr<class UScrollBox>  LinesBox;     // лента строк на rect 2521
	UPROPERTY(Transient) TObjectPtr<class UWidget>     HomeBtnSrc;   // 2528, класс НЕ предполагаем
	UPROPERTY(Transient) TObjectPtr<class UWidget>     ResizeTopSrc; // зона захвата верхней кромки
	UPROPERTY(Transient) TObjectPtr<class UWidget>     UDbtnSrc;     // 2605, свернуть/развернуть
	UPROPERTY(Transient) TObjectPtr<class UWidget>     HbtnSrc;      // 2607, замок автопрокрутки
	UPROPERTY(Transient) TObjectPtr<class UWidget>     QueBtnSrc;    // 2537, кнопка справки
	UPROPERTY(Transient) TObjectPtr<class UWidget>     QueTipSrc;    // 2580, окно справки
	UPROPERTY(Transient) TObjectPtr<class UWidget>     QueTipXSrc;   // 2589, крестик справки
	UPROPERTY(Transient) TObjectPtr<class UWidget>     QueTipMoveSrc;// шапка справки — зона перетаскивания
	// ★ Хранить как UWidget*: `ApplyFormatted` ЗАМЕНЯЕТ UTextBlock на URichTextBlock и возвращает
	//   НОВЫЙ виджет — старый указатель после первого вызова мёртв. Переприсваиваем возвращённый.
	UPROPERTY(Transient) TObjectPtr<class UWidget>     ComboLabel;   // 2608 — подпись выбранного канала
	UPROPERTY(Transient) TObjectPtr<class UCanvasPanel> ComboList;   // 2608 — раскрывающийся список
	UPROPERTY(Transient) TArray<TObjectPtr<class UWidget>> ComboRows; // 2608 — семь строк списка
	UPROPERTY(Transient) TArray<TObjectPtr<class UTextBlock>> QueTipTexts; // 7 подписей справки 2580 — узлы ассета TXT00…06, не создаются кодом
	UPROPERTY(Transient) TArray<TObjectPtr<class UImage>>     TabQuads;    // плитки вкладок 2501
	UPROPERTY(Transient) TArray<TObjectPtr<class UTextBlock>> TabCaps;     // подписи вкладок
	UPROPERTY(Transient) TObjectPtr<class UImage>     TabArrowL;
	UPROPERTY(Transient) TObjectPtr<class UImage>     TabArrowR;
	UPROPERTY(Transient) TObjectPtr<class UTextBlock> TabArrowLCap;
	UPROPERTY(Transient) TObjectPtr<class UTextBlock> TabArrowRCap;
	// ★★★ ЗАПИСЬ ВКЛАДКИ — аналог элемента `CChat::tabRecordList` (эталон: 22 байта, `char[20]`
	//   имя + WORD маска каналов на +20). У нас имя строкой, плюс `LocId` для СЕМИ ЭТАЛОННЫХ
	//   вкладок: у них подписи нет в записи — она берётся из таблицы текстов (loc 5411…5417),
	//   и должна переводиться/переименовываться вместе со всем UI (см. UiChannelText).
	struct FAsdaChatTab
	{
		FString Name;        // подпись пользовательской вкладки (при LocId == 0)
		int32   Mask = 1023; // маска каналов: бит i = показывать слот i
		int32   LocId = 0;   // loc-id подписи эталонной вкладки; 0 = пользовательская
	};
	TArray<FAsdaChatTab> ChatTabs;       // ★ ЖИВОЙ список вкладок (эталон `tabRecordList`)
	// ★ РАБОЧАЯ КОПИЯ редактора 2590 (эталон `tabRecordListMirror` @+0x560). Открытие окна снимает
	//   live → edit, ОК/X коммитит edit → live. Отдельной «Отмены» у окна НЕТ — её роль играет
	//   «не нажать ОК/X» (в эталоне так же: chat.c:2087-2093, крестик делает ТО ЖЕ, что и ОК).
	TArray<FAsdaChatTab> ChatTabsEdit;
	// ★ ОПЦИЯ КАНАЛА (эталон `ChatChannelOpt`, 24 байта: `char name[20]` + `DWORD colorARGB`).
	//   Живой массив `tabFilterOpt` @+0x36C и его рабочая копия `tabFilterOptBackup` @+0x45C.
	//   Имя канала — из таблицы текстов (loc 5341…5350), цвет игрок правит палитрой 2700.
	struct FAsdaChanOpt
	{
		FString Name;
		FColor  Color = FColor::White;
	};
	TArray<FAsdaChanOpt> ChanOpts;       // живой (10 записей)
	TArray<FAsdaChanOpt> ChanOptsEdit;   // рабочая копия редактора
	int32 EditTabIdx = 0;                // эталон stateFlags1 — вкладка, ВЫБРАННАЯ В СПИСКЕ редактора
	int32 EditChanScroll = 0;            // эталон stateFlags3 — прокрутка списка каналов (0…2)
	int32 EditColorRow = 0;              // эталон stateFlags2 — строка, по которой открыли палитру
	FColor EditColorPicked = FColor::White;   // эталон `CChat+0x228` (this[138]) — ВЫБРАННЫЙ цвет
	FColor EditColorOrig   = FColor::White;   // эталон `CChat+0x224` (this[137]) — ИСХОДНЫЙ цвет строки
	int32 EditTabScroll = 0;             // прокрутка списка вкладок 2597
	int32 kTabEditorRows = 8;            // строк каналов в окне 2590 (эталон: цикл for i < 8)
	UPROPERTY(Transient) TObjectPtr<class UUserWidget> TabEditorWidget;   // WBP_CHAT_TAB_WIN (2590)
	UPROPERTY(Transient) TObjectPtr<class UUserWidget> ColorWidget;       // WBP_CHAT_Color_Select_Win (2700)
	UPROPERTY(Transient) TArray<TObjectPtr<class UAsdaButtonRelay>> TabEditorRelays;
	// ★★★ СТОЛБЕЦ КЛОН-ЯЧЕЕК СПИСКА 2597 (эталон: `uiListAppendClonedCell(chatTabBButton)` на каждую
	//   вкладку + `chatListBoxPopulate` переписывает подписи всех tabCount ячеек, chat.c:1957).
	UPROPERTY(Transient) TArray<TObjectPtr<class UImage>>     EditTabCells;      // ячейки-клоны
	UPROPERTY(Transient) TArray<TObjectPtr<class UTextBlock>> EditTabCellCaps;   // их подписи
	UPROPERTY(Transient) TObjectPtr<class UImage>     EditTabArrowUp;            // прокрутка вверх
	UPROPERTY(Transient) TObjectPtr<class UImage>     EditTabArrowDn;            // прокрутка вниз
	UPROPERTY(Transient) TObjectPtr<class UTextBlock> EditTabArrowUpCap;
	UPROPERTY(Transient) TObjectPtr<class UTextBlock> EditTabArrowDnCap;
	// Панель-хозяйка ячеек (группа импортёра, в которой лежит образец) и слот образца — по ним
	//   клонируются все ячейки: ровно как эталон клонирует ПОСЛЕДНЮЮ ячейку списка.
	UPROPERTY(Transient) TObjectPtr<class UPanelWidget> EditTabCellHost;
	FMargin  EditTabCellOff  = FMargin(0.f);   // слот образца-ячейки (локальные координаты группы)
	FMargin  EditTabCapOff   = FMargin(0.f);   // слот образца-подписи
	FAnchors EditTabCellAnch = FAnchors(0.f);  // анкоры образца — схема импортёра 1:1
	bool  bTabEditorListBuilt = false;
	int32 EditTabHoverIdx   = -1;              // ячейка списка 2597 под курсором (кадр SS2 скина)
	int32 EditArrowHoverIdx = -1;              // 0 = ▲ под курсором, 1 = ▼, −1 = нет
	// ★★★ ОБРАЗЦЫ ЦВЕТА И ПЛИТКИ ПАЛИТРЫ — СОБСТВЕННЫЕ ВИДЖЕТЫ-ЗАЛИВКИ.
	//   ПРИЧИНА (живой тест 2026-08-06: «плитки палитры пустые, цвета каналов не заданы»):
	//   у контролов 2641…2648 (образцы цвета строк) и 2711…2730 (плитки палитры) скин
	//   `NoImage_icon`, а его `Type = "Text"` (JuiSkin.ini) ⇒ импортёр создаёт для них
	//   **UTextBlock**, а не UImage. Код красил их через `Cast<UImage>` — приведение всегда
	//   давало nullptr, и цвет не появлялся НИКОГДА. Это та же грабля, что с полосой 2639.
	//   ЭТАЛОН рисует их так: `CControl_DrawTextureRect(ctrl, "n_01.tex", 56, 464, 24, 12)` —
	//   белый прямоугольник атласа — и `CControl_SetValueAll4(ctrl, ARGB, 0)` — заливка цветом
	//   (chat.c:1188-1196 для палитры, chat.c:1136-1145 для образцов строк). Воспроизводим
	//   ровно это: UImage с UV указанного прямоугольника n_01 и тинтом (sRGB→linear).
	UPROPERTY(Transient) TArray<TObjectPtr<class UImage>> EditRowSwatches;   // 2641…2648, по строке
	UPROPERTY(Transient) TArray<TObjectPtr<class UImage>> PaletteTiles;      // 2711…2730
	UPROPERTY(Transient) TObjectPtr<class UImage> ColorPrevPicked;           // 2731 «Выбранный цвет»
	UPROPERTY(Transient) TObjectPtr<class UImage> ColorPrevOrig;             // 2732 «Исходный цвет»
	// ★ 8-я строка каналов (2617/2628/2648) в JUI имеет ПУСТОЙ `ID`, поэтому импортёр назвал её
	//   узлы по скину — точного имени у них нет. Клонируем её из образца 1-й строки со сдвигом
	//   на эталонный шаг (те же ABS, что в отчёте §1.1) — приём `uiListAppendClonedCell`.
	UPROPERTY(Transient) TObjectPtr<class UCheckBox>  EditRow8Check;
	UPROPERTY(Transient) TObjectPtr<class UTextBlock> EditRow8Label;
	// Эталон `FColor` → заливка: одна фабрика на образцы строк, плитки палитры и превью.
	class UImage* MakeSwatch(class UUserWidget* Host, class UPanelWidget* Parent, const TCHAR* Name, int32 Z);
	// Разместить виджет-заливку ровно поверх контрола-«хозяина» (его слот = слот из JUI).
	static void PlaceOverWidget(class UWidget* What, class UWidget* Over);
	bool bTabEditorBuilt = false;
	bool bColorBuilt = false;
	// Внутреннее (реализация — AsdaChatTabEditor.cpp).
	void BuildTabEditor();
	void RefreshTabEditor();
	void CommitTabEditor();
	// Команда пункта меню 2670 (2671/2672/2673/2674) — реальное действие через эталонные диалоги.
	void HandleTabMenuCommand(int32 IdNum);
	// Мышь внутри редактора 2590 и палитры 2700 (хит-тест по геометрии, как во всём окне чата).
	void TickTabEditorPointer(const FVector2D& Cur, bool bLmbEdge);
	void ShowColorSelect(int32 Row);
	void HideColorSelect();
	void RefreshColorSelect();
	int32 ChanIndexForRow(int32 Row) const { return EditChanScroll + Row; }
	// ── СПИСОК ВКЛАДОК 2597: столбец клон-ячеек (эталон `uiListAppendClonedCell` ×tabCount) ──
	//   Тот же приём, что уже применён к ряду вкладок 2501 (`EnsureTabWidgets`): импортированная
	//   ячейка гасится, столбец рисуется теми же фабриками MakeTabQuad/MakeTabCap.
	void  BuildTabEditorList();                       // один раз на экземпляр окна 2590
	void  EnsureTabEditorCells(int32 N);              // довести число ячеек до N
	void  RefreshTabEditorList();                     // разложить ячейки, подписи, состояния, стрелки
	int32 EditTabVisibleCount() const;                // сколько ячеек влезает (эталонная формула)
	int32 EditTabCellHitTest(const FVector2D& Cur) const;   // индекс вкладки под курсором (−1 = мимо)
	void  ScrollTabEditorList(int32 Delta);           // прокрутка столбца (аналог скроллбара 2598)
	// Найти виджет импортированного окна по ТОЧНОМУ имени, а если его нет — по ПРЕФИКСУ.
	//   ★ Импортёр даёт точное имя idc только интерактив-контролам и меткам; планы статик-узлов
	//   называются `<idc>_<порядковый>_<план>` — точного имени у них НЕТ (грабля 2026-08-06:
	//   хит-тест по имени `listCHAT_TAB_WIN_B2_ListBox` всегда возвращал nullptr).
	static class UWidget* FindByPrefix(class UUserWidget* Host, const TCHAR* Prefix, class UClass* OfClass = nullptr);
	static const FName kTabEditorId;
	static const FName kColorSelectId;
	// ★★★ ПАЛИТРА ОКНА 2700 — 20 ПЛИТОК (2711…2730), ЗНАЧЕНИЯ НАЙДЕНЫ И ПОДТВЕРЖДЕНЫ ТРОЙНО.
	//   Отчёт: `Reports/chat_color_palette_re.md` (2026-08-06). Источник — `chatInitColorPalette
	//   @0xB9E110` (`RE/etalon/chat.c:684-706`): константы пишутся НЕ в `colorPalette` (+0x224), а в
	//   соседний буфер `msgColorPaletteExt` (+0x228); билдер читает `msgColorPaletteExt[4*i + 4]`,
	//   i = 0..19 ⇒ `CChat+0x22C … +0x278`. Подтверждено байтами EXE (`mov [eax+disp32], imm32`),
	//   декомпилом и живым дампом (`CChat` — статический глобал VA 0x02BE06C8).
	//   ⚠ Ранее здесь стояло «тела функции в срезе НЕТ» — промах: искали по имени `colorPalette`.
	//   `CChat+0x224` и `+0x228` — НЕ плитки, а служебные слоты «исходный» / «выбранный» цвет
	//   (оба стартуют с `FF000000`, `chat.c:1204/1213`).
	//   ★ Гамма: цвета уходят в заливку контрола ⇒ они sRGB → в UE через `FromSRGBColor`.
	static constexpr int32 kPaletteSlots = 20;
	static const FColor kPaletteColors[kPaletteSlots];
	// Служебные слоты палитры (эталон: оба стартуют с FF000000).
	static constexpr uint32 kPaletteInitArgb = 0xFF000000u;
	int32 ActiveTab = 0;                 // активная вкладка (эталон: chatTabSelectFilter(this, 0) при старте)
	int32 TabScroll = 0;                 // первая видимая вкладка в ряду
	int32 TabHoverIdx = -1;              // вкладка под курсором (состояние SS2 скина)
	// ★ Вкладка, ПО КОТОРОЙ вызвано ПКМ-меню 2670 (снимается TabHitTest в момент открытия).
	//   −1 = меню открыто не по вкладке (по стрелке прокрутки) ⇒ цель = активная, как в эталоне.
	int32 TabMenuTargetIdx = INDEX_NONE;
	// Цель пунктов меню 2670: вкладка под ПКМ, а если её нет — активная (эталонное поведение).
	int32 MenuTargetTab() const
	{
		return ChatTabs.IsValidIndex(TabMenuTargetIdx) ? TabMenuTargetIdx : ActiveTab;
	}
	int32 ArrowHoverIdx = -1;            // 0 = ◄ под курсором, 1 = ►, −1 = нет
	bool  bTabsBuilt = false;
	// Игровой шрифт (Default-строка DT_AsdaRichStyle) — им рисуются подписи справки.
	FSlateFontInfo TipFont;
	UPROPERTY(Transient) TObjectPtr<class UEditableText> InputEdit;   // 2609 — поле ввода
	UPROPERTY(Transient) TObjectPtr<class UTextBlock> InputPrefix;    // приглашение loc 5316+state
	UPROPERTY(Transient) TObjectPtr<class UWidget>   FloodBarFill;    // 2618 — заливка полосы антифлуда
	UPROPERTY(Transient) TObjectPtr<class UTextBlock> FloodText;      // 2619 — подпись полосы
	UPROPERTY(Transient) TObjectPtr<class UWidget>     PressedNode;  // узел, на котором начали нажатие

	// ── Данные (переживают травел: виджеты умирают вместе с миром, история — нет) ──
	//   (сама структура FChatLogLine объявлена выше — её использует и блок контекстных меню.)
	TArray<FChatLogLine> LogHistory;

	// ── Состояние ──
	bool  bResizingTop = false;          // тянем ВЕРХНЮЮ кромку окна
	// ★ Аудит №3 (B3): прежние латчи `bPrevLmbDown` + `bPrevRmbDown` — ОДИН экземпляр общей точки
	//   (обе кнопки опрашивались в одном `TickPointer`, и оба сброса стояли в одной `ResetWidgets`).
	FAsdaMouseFrame Mouse;
	// ★ 30.10.2026 [ОРИГИНАЛ] подсказки окна чата при наведении (`uiChatWindowMsgProc` событие 8 → `tutorialPopupLayoutSmall`:
	//   вкладка 2501 → 5361/5362, поле ввода 2609 → 5363/5364; 0xA → гашение). Владелец плашки `EAsdaTipOwner::ChatWin`.
	FAsdaHelpTipHover HelpTip;
	float DragStartH = 0.f;              // высота окна в момент захвата кромки (базовые px)
	float DragStartMouseY = 0.f;         // Y курсора в момент захвата (базовые px)
	// ★ Перенесено в ассет (15.09, разрешение владельца): bWarnedSpanSlot удалён вместе с NormalizeControlSlot.
	// ★ Свернуть/развернуть (2605, uiToggleExpandLayout @0xBB6E40).
	//   a2 = (CChat+496 == 0): развернуть → Origin.y = −170, высота 340; свернуть → 0 и 170.
	//   Верх окна = Origin.y + (−188) + 768 ⇒ 410 развёрнуто / 580 свёрнуто, НИЗ ВСЕГДА 750.
	//   ★ До ПЕРВОГО нажатия высота = 178 из JUI (низ 758) — эталон приводит её к 170 только
	//   первым тоглом, воспроизводим 1:1, поэтому нужен отдельный флаг «тоглили хоть раз».
	bool  bExpanded = false;
	bool  bLayoutToggled = false;
	// ★★★ ПАМЯТЬ РАЗМЕРА ДЛЯ КНОПКИ 2605 — ОТСТУПЛЕНИЕ ОТ ОРИГИНАЛА (требование юзера 2026-08-06).
	//   ЭТАЛОН: `uiToggleExpandLayout @0xBB6E40` (ui_core.c:13529) знает РОВНО ДВА состояния —
	//   высота 170 (Origin.y = 0) и высота 340 (Origin.y = −170); ШИРИНУ он не трогает вообще,
	//   и никакой «памяти» у него нет (размеры даже не сохраняются в ChatOpt.bin — chat.c:1397).
	//   ПОРТ: кнопка стала тоглом «в дефолт ↔ в запомненный размер» ПО ОБЕИМ осям:
	//     • окно НЕ в дефолте (474 × 170) → запоминаем текущие W/H и схлопываем в дефолт;
	//     • окно в дефолте → возвращаем запомненные W/H (если их нет — эталонные 474 × 340).
	//   Ручное растягивание автоматически делает текущий размер новым «развёрнутым» (окно просто
	//   перестаёт быть дефолтным), отдельного флага для этого не нужно. Живёт ТОЛЬКО в сессии:
	//   в файл не пишется — как и в оригинале.
	float SavedW = 474.f;
	float SavedH = 340.f;
	bool  bHasSavedSize = false;
	// Текущая высота окна при РУЧНОМ растягивании верхней кромки. Живёт в той же шкале, что и
	//   кнопка 2605 (170…340), поэтому тогл — это просто «прыжок» между концами шкалы.
	float CurH = kHCollapsed;
	bool  bQueTipVisible = false;        // окно справки 2580 открыто
	bool  bQueTipTextsBuilt = false;     // подписи справки уже созданы
	bool  bChannelComboBuilt = false;    // комбобокс 2608 наполнен
	// ── ЭТАП 2: состояние ввода ──
	bool  bInputActive = false;          // строка ввода открыта и в фокусе
	// ★ Отсрочка сверки флага с фактическим фокусом: между `OpenInput` и моментом, когда Slate
	//   реально отдаст фокус полю 2609, проходит несколько кадров (виджет может ещё строиться).
	//   Пока счётчик не истёк, «фокуса нет» не считается потерей — иначе ввод гасил бы сам себя.
	int32 InputFocusGraceFrames = 0;
	// ★ 06.11.2026: сколько кадров после `CloseInput` фокус поля считается хвостом закрытия, а не кликом игрока (`asda.Chat.FocusIsTyping`).
	int32 CloseFocusDropFrames = 0;
	static constexpr int32 kCloseFocusDropFrames = 4;
	static constexpr int32 kInputFocusGraceFrames = 8;   // ~8 кадров ≈ 130 мс при 60 к/с
	int32 InputState = 4;                // CChat[105]; 4 = «[Общий]» — то же, что первый пункт 2608
	FString WhisperTarget;               // адресат шёпота (state 7), loc 5323 «Шепчемся с %s»
	FString LastWhisperFrom;             // от кого пришёл последний шёпот — для «R» (CHAT ANSWER)
	// ★ Истории ввода стрелками в ОРИГИНАЛЕ НЕТ (VK_UP/VK_DOWN в срезе отсутствуют),
	//   поэтому её и не заводим — «не выдумывать».
	// Антифлуд: полоса 2618 с диапазоном float 1.0…32.0 ≈ 30 секунд, подпись 2619 = loc 5326,
	//   отказ = loc 7032. Действует на «широкие» каналы 0/1/2 (chatAntiFloodCheck).
	// ── Антифлуд `chatAntiFloodCheck @0xBB4950` (chat.c:5536) и кулдаун каналов ──
	//   Счётчики эталона: [3547] «быстрее 1 с», лимит [3549] = 5 → блок 60 000 мс + loc 7030;
	//   [3548] повторов одного текста в окне 60 с, лимит [3550] = 10 → блок 600 000 мс + loc 7031;
	//   снятие по таймеру 0x1C → `chatAntiFloodReset` + loc 7033; отказ во время блока — loc 7032.
	int32 FloodFastCount = 0;
	int32 FloodRepeatCount = 0;
	double FloodLastSend = 0.0;
	double FloodRepeatWindow = 0.0;
	FString FloodLastText;
	bool  bFloodBlocked = false;
	float FloodBlockLeft = 0.f;
	static constexpr int32 kFloodFastLimit = 5;
	static constexpr int32 kFloodRepeatLimit = 10;
	// ★ Полоса 2618 / подпись 2619 — ЭТО НЕ индикатор антифлуда, а КУЛДАУН «широких»
	//   каналов: `uiProgressBarSetVisible` (ui_core.c:11804) ставит stateFlags4 = 30, а
	//   `uiProgressBarTickCountdown` (ui_core.c:11791) раз в секунду уменьшает его на 1 и рисует
	//   значение 30 − stateFlags4 ⇒ полоса РАСТЁТ 1.0 → 30.0 за 30 секунд.
	//   Пока кулдаун идёт, `ChatInput_SendChannel` вообще не вызывается (chat.c:3878).
	float FloodLeft = 0.f;
	static constexpr float kFloodSeconds = 30.f;

	// ★★★ ОТСТУПЛЕНИЕ ОТ ОРИГИНАЛА (решение юзера 2026-08-05) — НЕ «ЧИНИТЬ» ОБРАТНО!
	//   В ИГРЕ СЧЁТЧИК ОДИН ОБЩИЙ на все три «широких» канала: `CChat::stateFlags4` — ОДИН int
	//   в объекте чата (`uiProgressBarSetVisible`, ui_core.c:11804 → `if (!*(this+112)) *(this+112) = 30;`;
	//   `uiProgressBarTickCountdown`, ui_core.c:11791 → `--this->stateFlags4;`), и проверка перед
	//   отправкой (chat.c:3878 `if (strcmp(...) && !v37[112])`) — БЕЗ индексации по каналу, общая
	//   для всей ветки `case 0u: case 1u: case 2u`. То есть в оригинале «Торговля» затыкает и
	//   «По всем каналам», и «Поиск отряда».
	//   ★ ПОРТ ПО РЕШЕНИЮ ЮЗЕРА ДЕЛАЕТ ИНАЧЕ: СВОЙ таймер на КАЖДЫЙ из трёх слотов
	//   (0 «По всем каналам», 1 «Поиск отряда», 2 «Торговля»), длительность прежняя 30 с.
	//   Блокируется только тот канал, чей таймер идёт; полоса 2618 и подпись 2619 показывают
	//   таймер ТОЛЬКО текущего выбранного канала. Сводка отступлений — chat_main_window.md.
	static constexpr int32 kWideChannelCount = 3;
	float   ChannelCooldownLeft[kWideChannelCount]  = { 0.f, 0.f, 0.f };
	float   ChannelCooldownTotal[kWideChannelCount] = { 0.f, 0.f, 0.f };
	FString ChannelCooldownReason[kWideChannelCount];

	// Кулдаун НАШИХ отступлений (антифлуд 60/600 с, мат-фильтр) — он общий и виден в ЛЮБОМ
	//   режиме, иначе игрок не поймёт, почему чат молчит. К каналам отношения не имеет.
	float CooldownLeft = 0.f;
	float CooldownTotal = 0.f;
	FString CooldownReason;
	// ★ В ЭТАЛОНЕ ТАЙМЕРА ПРИ МАТЕ НЕТ — длительность наша, по просьбе юзера.
	static constexpr float kCurseCooldownSec = 30.f;

	// ★★★ ОТКЛЮЧЕНО 2026-08-05 ПОСЛЕ ЖИВОГО ТЕСТА. НЕ ВКЛЮЧАТЬ ОБРАТНО.
	//   Была попытка слать «широкие» каналы опкодом 5086, положив номер канала в байт +2.
	//   Живой тест: «Торговля» приходила как **[Сервер]**, «Поиск отряда» — как **[кан.]**,
	//   «По всем каналам» — вообще без подписи. Юзер сверил с ОРИГИНАЛЬНЫМ клиентом: тот
	//   показывает ровно то же самое ⇒ приём исправен, врала ОТПРАВКА.
	//   ПРИЧИНА: байт +2 пакета 5086 — это НЕ номер чат-канала, а ОБЛАСТЬ ОХВАТА глобального
	//   объявления (0 обычный / 1 «кан.» loc 1187 / 2 «Сервер» loc 1109; >2 клиент строку не
	//   строит вовсе). Эталонный клиент кладёт туда ВСЕГДА 0 (`msgHeader[2] = 0`, chat.c:3053).
	//   ⇒ числа, дающего «Торговля»/«Поиск отряда», в 5086 НЕ СУЩЕСТВУЕТ.
	//   Правильный маршрут — эталонная пара 6560 → 6561 (см. SendChatChannel6560): номер канала
	//   там равен индексу режима ввода 1:1. Требует серверного патча (в GameServer.exe нет ни
	//   6560, ни 6561). Полный разбор — chat_main_window.md, Д15.
	static constexpr bool kWideChannelsVia5086 = false;
	// (числа лимита длины сообщения объявлены в публичной секции — см. GetInputMaxChars)
	static constexpr int32 kInputHistoryMax = 32;   // глубина истории ввода
	// ★★★ ТАБЛИЦА МАТ-ФИЛЬТРА — НАМЕРЕННО ПУСТА (требование юзера 2026-08-05).
	//   Каркас (точка вызова + замена на loc 9005) реализован, слова НЕ заполняются ⇒ фильтр
	//   фактически выключен. ВКЛЮЧАЕТСЯ ЗАПОЛНЕНИЕМ ЭТОГО МАССИВА (разбор CurseTable.BIN).
	TArray<FString> CurseWords;
	// Номер канала из комбобокса 2608 (channelOrder[] = {4,3,8,6,5,7}); по умолчанию 4 = ОБЩИЙ.
	//   Используется отправкой на ЭТАПЕ 2 — здесь только запоминается и пишется в лог.
	int32 SelectedChannel = 4;
	int32 ComboIndex = 0;                // выбранный пункт списка 2608 (0 = ОБЩИЙ)
	int32 ComboHover = -2;               // строка под курсором (−2 = ещё не считали)
	bool  bComboOpen = false;            // список 2608 раскрыт
	// Перетаскивание окна справки 2580 (отступление по требованию юзера).
	FVector2D QueTipOffset = FVector2D::ZeroVector;
	bool  bTipDragging = false;
	FVector2D TipDragStartMouse = FVector2D::ZeroVector;
	FVector2D TipDragStartOffset = FVector2D::ZeroVector;
	// Текущий прямоугольник окна в базовых px (то, что реально разложено ApplyWindowLayout).
	float WinL = 1.f;
	float WinT = 580.f;
	float WinH = 178.f;
	float WinW = 474.f;
	// Фактическая ширина переноса строк (аналог CChat+0x5D4 chatLineMaxWidth). 0 = ещё не считана.
	float LinesWrapW = 0.f;
	// ★★★ ГОРИЗОНТАЛЬНОЕ РАСТЯГИВАНИЕ ОКНА — ОТСТУПЛЕНИЕ ОТ ОРИГИНАЛА (решение юзера 2026-08-05).
	//   РЕВЕРС: в оригинале окно чата 2500 РАЗМЕР НЕ МЕНЯЕТ ВООБЩЕ — ни по одной оси.
	//     • класс `CResizeWindow` в движке есть (`uiControl_ClampMoveResize`, ui_core.c:119292,
	//       обрабатывает X и Y независимо), но создаётся только при `Type = "ResizeWindow"`
	//       (ui_core.c:105552), а этой строки в gamemain_jui.ini НЕТ НИ РАЗУ;
	//     • контрол `SizeWin` в дереве 2500 есть, но строка "SizeWin" не встречается ни в одном .c
	//       среза — обработчика у него нет;
	//     • кнопка 2605 (`CHAT_NEW_UDbtn`) в коде только `SetVisible` (chat.c:923-926), больше нигде;
	//     • кнопка 2607 (`CHAT_NEW_Hbtn`) обрабатывается (ui_core.c:12303/12573), но пишет ФЛАГ
	//       АВТОСКРОЛЛА (+497), а не размер;
	//     • размеры окна НЕ сохраняются: `CChat_SaveChatOpt` (chat.c:1397-1402) пишет в ChatOpt.bin
	//       только фильтры каналов и записи вкладок.
	//   ⇒ И вертикальный ресайз (сделан раньше), и горизонтальный — НАШИ отступления. Реализованы
	//   в одном стиле: отдельная зона-кромка, без выдуманных кнопок.
	float CurW = 474.f;
	// Минимум = авторская ширина (требование юзера: «стандартный размер и есть минимальный»).
	// Максимум = правый край базового экрана минус поля кнопок 2605/2606/2607 (22 px) и зазор 4 px:
	//   kWinX(1) + W + 22 + 4 <= 1024  ⇒  W <= 997.
	static constexpr float kWinMinW = 474.f;             // = kWinW, литералом: объявлено выше по файлу
	static constexpr float kWinMaxW = 997.f;             // 1024 (база) − kWinX(1) − кнопки 22 − зазор 4
	static constexpr float kResizeBandW = 8.f;   // ширина полосы захвата правой кромки
	bool  bResizingRight = false;
	float ResizeStartX = 0.f;                    // курсор в момент нажатия (экранные px)
	float ResizeStartW = 474.f;                  // ширина окна в момент нажатия (базовые px)
	UPROPERTY(Transient) TObjectPtr<class UWidget> ResizeRightSrc;
	FDelegateHandle HChatLine;           // подписка на UAsdaNetSubsystem::OnChatLine
	// Режим префикса канала (флаг осады). См. SetChannelPrefixMode.
	bool  bChannelPrefixMode = false;
	// ★ АВТОСКРОЛЛ 1:1. Реверс 2026-08-05: «проверка был-ли-скролл-внизу» в Chat_AddChatLine
	//   (chat.c:5088-5095) — МЁРТВЫЙ КОД: `scrollWasAtBottom` обнуляется прямо перед `if`,
	//   поэтому внутренний uiScrollbarSetPos недостижим. Настоящий автоскролл живёт ниже, в
	//   `chatLog_AddTokenizedLinesTab3` (chat.c:3375): после добавления строк скроллбар ставится
	//   в конец БЕЗУСЛОВНО — если не поднят флаг CChat+497. ★ 2607 — это ЗАМОК АВТОПРОКРУТКИ
	//   (единственные два чтения флага, chat.c:1828 и chat.c:3375, гасят «прокрутить в конец»),
	//   а НЕ «ручка ресайза», как считал самый первый разбор.
	//
	// ★★★ ИСПРАВЛЕНИЕ РАЗБОРА (2026-08-06, живой тест юзера «кнопка заморозки не работает»).
	//   Было: 2607 считали HOLD-кнопкой — флаг поднимался на НАЖАТИИ и падал на ОТПУСКАНИИ,
	//   потому что msgCode 0x10601/0x10602 приняли за «press/release». ЭТО НЕВЕРНО.
	//   ДОКАЗАТЕЛЬСТВА (три независимых):
	//     1) Скин контрола 2607 — `Chat_new_ck3`, и у него в `JuiSkin.ini` стоит `Type = "CheckButton"`
	//        (JUISKIN.ini:2428-2430), то есть ТУМБЛЕР. Кадры парные: SS0/SS1 = UV 0.742…0.785
	//        (выключено), SS2/SS3 = UV 0.789…0.832 (включено).
	//     2) `checkBox_OnToggle @0x12FDF70` (ui_core.c:117862): по клику, если состояние
	//        `this+608` было включено — поднимает событие **67074 (0x10602)** и ставит план 0;
	//        иначе поднимает **67073 (0x10601)** и ставит план 2. То есть 0x10601 = «ОТМЕЧЕН»,
	//        0x10602 = «СНЯТ», а вовсе не «нажал/отпустил».
	//     3) Тот же `case` оконной процедуры чата теми же кодами обрабатывает контролы 2610…2617
	//        (`sub_9333C9(pThis, …, 1)` / `(…, 0)`) — это ГАЛОЧКИ КАНАЛОВ редактора 2590,
	//        заведомо тумблеры; при трактовке «нажал/отпустил» бит канала снимался бы сразу же.
	//   ⇒ 2607 — ТУМБЛЕР: включён = автопрокрутка заморожена НАВСЕГДА, пока не выключат.
	//   Что именно замораживается (сплошной просмотр обоих чтений флага):
	//     • строки В ЛЕНТУ ПРИХОДЯТ КАК ОБЫЧНО (`uiChatLogAddLine` зовётся ДО проверки флага,
	//       chat.c:3366-3374), вытеснение старых по лимиту 500 тоже работает;
	//     • гасится ТОЛЬКО `uiScrollbarSetPos(list, конец)` — «прыжок к последней строке»;
	//     • при ПЕРЕКЛЮЧЕНИИ ВКЛАДКИ (`chatTabSelectFilter`, chat.c:1828) прыжок к концу тоже
	//       гасится, а сам флаг НЕ сбрасывается — ни вкладкой, ни переполнением буфера,
	//       ни травелом карты: единственный, кто его снимает, — повторный клик по 2607.
	//     • у журнала 2570 своего замка нет: `chatLog_AddTokenizedLinesEx` (chat.c:3417) флаг не
	//       читает, у него условие «скроллбар и так внизу» — журнал НЕ трогаем.
	bool  bScrollFrozen = false;

	// ── Константы эталона ──
	static constexpr int32 kMaxLines   = 500;   // sub_935C9B(chatNewListBoxNor, 500), chat.c:801
	// ★ НАШ шаг листания (эталон его не задаёт: `uiListSetScrollOffset(list,2)` — межстрочный
	//   интервал 2 px, ui_core.c:111621; исправлено 2026-08-06).
	static constexpr int32 kScrollStep = 2;
	static constexpr float kFontPx     = 8.f;   // FONT_PX импортёра (в JUI кегль не хранится)
	static constexpr int32 kChatZ      = 689;   // на 1 НИЖЕ журнала 2570 (690) — окна встык, не спорят
	// База JUI и rect'ы контролов (compute_anchors, см. шапку).
	//   ★ ПО ОДНОМУ ОБЪЯВЛЕНИЮ НА СТРОКУ: MSVC даёт C2487 на multi-declarator `static constexpr`
	//   внутри класса с dll-интерфейсом (ASDAREBORN_API) — «member of dll interface class may not
	//   be declared with dll interface».
	static constexpr float kWinX  = 1.f;    // CHAT_NEW 2500
	static constexpr float kWinY  = 580.f;
	static constexpr float kWinW  = 474.f;
	static constexpr float kWinH  = 178.f;
	static constexpr float kListX = 28.f;   // CHAT_NEW_C_NOR 2521 — лента строк
	static constexpr float kListY = 625.f;
	static constexpr float kListW = 439.f;
	static constexpr float kListH = 98.f;
	static constexpr float kHomeX = 451.f;  // CHAT_NEW_HomeBtn 2528
	static constexpr float kHomeY = 627.f;
	static constexpr float kHomeW = 13.f;
	static constexpr float kHomeH = 13.f;
	// Свернуть/развернуть (uiToggleExpandLayout): высоты и верх окна.
	static constexpr float kHCollapsed = 170.f;
	static constexpr float kHExpanded  = 340.f;
	static constexpr float kTopCollapsed = 580.f;
	static constexpr float kTopExpanded  = 410.f;
	// ★ НИЗ ОКНА ЗАКРЕПЛЁН. `uiToggleExpandLayout` даёт верх 580 при высоте 170 и 410 при 340 —
	//   в обоих случаях низ 750. Растягивание вверх работает в ТОЙ ЖЕ шкале, поэтому пределы
	//   ручного растягивания = эталонные концы шкалы, ничего не выдумано:
	//     минимум 170 = свёрнутое окно, максимум 340 = развёрнутое.
	//   ★ До первого касания (ни тогла, ни растягивания) высота = 178 из JUI, низ 758 — ровно как
	//   в оригинале до первого нажатия 2605.
	static constexpr float kWinBottomFixed = 750.f;
	// Толщина зоны захвата верхней кромки (базовые px): 3 px наружу + 5 внутрь окна.
	static constexpr float kResizeBandOut = 3.f;
	static constexpr float kResizeBandH   = 8.f;
	// Окно справки 2580: rect из JUI и «шапка» для перетаскивания (без крестика 2589 справа).
	static constexpr float kTipX = 446.f;
	static constexpr float kTipY = 371.f;
	static constexpr float kTipW = 276.f;
	static constexpr float kTipH = 310.f;
	static constexpr float kTipHeaderW = 250.f;   // 276 − 26: правее сидит крестик 2589
	static constexpr float kTipHeaderH = 24.f;
	// Пункты комбобокса 2608. Эталонные шесть (chat.c:719-731) + «Поиск отряда»/«Торговля»/«Армия»
	//   по требованию юзера (в оригинале широкие каналы только по слэш-командам).
	//   ★★★ «По всем каналам» (слот 0) из списка ОТПРАВКИ УБРАН — решение юзера 2026-08-05
	//   (серверный патч делается только под каналы 1 и 2). ⚠ Вкладка-ФИЛЬТР № 6 с тем же
	//   названием (маска 903) — ДРУГАЯ сущность и остаётся. Заполняются в BuildChannelCombo.
	TArray<int32> ComboLoc;      // loc-id подписи пункта
	TArray<int32> ComboSlots;    // номер канала/режима (−1 = «Прочие», без действия)
	// ★ Длина списка = ComboSlots.Num(); константа держится синхронной (проверка в BuildChannelCombo).
	static constexpr int32 kComboItems = 9;
	// ★ ФАКТИЧЕСКОЕ число видимых пунктов (пункт «Армия» вне битвы армий скрыт). По нему считается
	//   высота раскрытой плиты и Y каждой строки — иначе внизу остаётся пустая строка.
	int32 ComboVisibleCount = kComboItems;
	// Плиты подсветки строк списка (по одной на пункт, лежат ПОД текстом).
	UPROPERTY(Transient) TArray<TObjectPtr<class UImage>> ComboRowBgs;
	// ★★★ ЦВЕТА ПОДСВЕТКИ ПУНКТА — ИЗ ДВИЖКА, НЕ ИЗ СКИНА (ui_core.c:112969, тело отрисовки CListBox):
	//     if ( i == *(this + 326) ) SetRenderState(dev, 60 /*D3DRS_TEXTUREFACTOR*/, -1762594576);
	//     if ( i == *(this + 327) ) SetRenderState(dev, 60, ~*(_DWORD *)(item + 2760));
	//   +326 = строка под курсором, +327 = выбранная (обе −1 в ctor, ui_core.c:111211).
	//   HOVER: −1762594576 = **0x96F0F0F0** (альфа 0x96 ≈ 59 %, цвет F0F0F0).
	//   ВЫБРАННЫЙ: инверсия поля +2760; список каналов заводится `CClass_DrawText(…, −1, 0)`
	//   (chat.c:728/731) ⇒ 4-й аргумент 0 ⇒ поле 0 ⇒ ~0 = **0xFFFFFFFF**, сплошная белая плита.
	static constexpr FColor kComboHoverColor = FColor(0xF0, 0xF0, 0xF0, 0x96);
	static constexpr FColor kComboSelColor   = FColor(0xFF, 0xFF, 0xFF, 0xFF);
	// ★ z-порядок раскрытого списка каналов — ВЫШЕ всех наших окон (чат 689, журнал 690, справка).
	static constexpr int32 kComboListZ = 9000;
	// ★★★ ГРУППА НИЖНЕЙ СТРОКИ `SizeWin_grp` — В НЕЙ ЖИВУТ КОМБОБОКС 2608, ПОЛЕ 2609 И ПОЛОСА 2618.
	//   `BuildLayoutTable` кладёт её в `(L, T + H − kSizeWinDY)`, а тело комбобокса внутри неё —
	//   в локальный `kSizeWinComboLocalY`. Числа были литералами (170 и 138) в двух местах, из-за
	//   чего подпись канала, посчитанная в АБСОЛЮТНЫХ координатах, расходилась с телом при любой
	//   смене высоты окна. Держим их ЯВНО и используем во всех точках.
	static constexpr float kSizeWinDY         = 170.f;
	static constexpr float kSizeWinComboLocalY = 138.f;
	// Вкладки: 7 записей (uiChatOpt_InitItems), плитка 79×25 (скин TalkBar_2 Rect «0 0 79 25»),
	//   в ряд влезает (474 − 10) / 79 = 5 (chat.c:790 uiControlCountFittingChildren).
	// ★ Число ДЕФОЛТНЫХ вкладок эталона (uiChatOpt_InitItems). Фактическое число вкладок —
	//   `ChatTabs.Num()`: список динамический (меню 2670 добавляет/удаляет).
	static constexpr int32 kTabCountDefault = 7;
	// ★ kTabsVisible — ЧИСЛО ПРИ АВТОРСКОЙ ШИРИНЕ 474 (справочное, для логов и сверки с оригиналом).
	//   Фактическое число вкладок в ряду считает VisibleTabCount() по ТЕКУЩЕЙ ширине окна той же
	//   эталонной формулой (chat.c:795). При 474 обе величины совпадают.
	static constexpr int32 kTabsVisible = 5;
	static constexpr float kTabW = 79.f;
	static constexpr float kTabH = 25.f;
	static constexpr float kTabArrowW = 22.f;
	// ═════════ СПИСОК ВКЛАДОК 2597 В РЕДАКТОРЕ 2590 — СТОЛБЕЦ КЛОН-ЯЧЕЕК ═══════════════════════
	// Числа — из `gamemain_jui.ini` (отчёт `chat_tab_editor_2590_re.md` §1.1), база 1024×768:
	//   2597 `CHAT_TAB_WIN_B_button`  ABS (427, 266) 79×25, скин TalkBar_2 (тот же, что у ряда 2501);
	//   2598 `CHAT_TAB_WIN_B_listbox` ABS (407, 264) 114×200 — область прокрутки списка.
	// Шаг клонирования = ВЫСОТА ячейки: `uiListAppendClonedCell @0x12F5C60` сдвигает клон на
	//   `cellHeight = this[498] − this[496]` (вертикальная раскладка, `this[632] != 1`).
	static constexpr float kEditListX   = 427.f;
	static constexpr float kEditListY   = 266.f;
	static constexpr float kEditCellW   = 79.f;
	static constexpr float kEditCellH   = 25.f;
	static constexpr float kEditScrollX = 407.f;
	static constexpr float kEditScrollY = 264.f;
	static constexpr float kEditScrollW = 114.f;
	static constexpr float kEditScrollH = 200.f;
	// Ширина стрелки прокрутки списка = полоса 2598 СПРАВА от ячеек: (407 + 114) − (427 + 79) = 15.
	static constexpr float kEditArrowW  = 15.f;
	// Геометрия 2608 (JUI 28,726,100x20) и его скина Bk_ComboBox: стрелка ES=1 «90 0 110 20»
	//   ⇒ РОВНО 20x20, кладётся своим размером к правому краю; тело растягивается на остаток.
	static constexpr float kComboX = 28.f;
	static constexpr float kComboY = 726.f;
	static constexpr float kComboW = 100.f;
	static constexpr float kComboH = 20.f;
	static constexpr float kComboArrowW = 20.f;
	// ★ КЕГЛЬ ПОДПИСИ ВЫБРАННОГО КАНАЛА — на 1 МЕНЬШЕ общего UI-кегля (требование юзера 2026-08-06:
	//   «уменьшить на 1 размер текст и выровнять ровно по центру этой области»). Подпись
	//   центрируется по ТЕЛУ комбобокса (kComboX, kComboY, kComboW − kComboArrowW, kComboH):
	//   по горизонтали — Justification=Center, по вертикали — слотом, по замеренной высоте строки.
	//   ⚠ Отступление: в эталоне подпись рисуется от CaptionPoint (5,4) скина Bk_ComboBox, тем же
	//   глобальным кеглем, без центровки.
	static constexpr float kComboLabelFontPx = kFontPx - 1.f;
	// Высота строки списка = Rect скина DarkGrey_List «0 0 50 20».
	static constexpr float kComboRowH = 20.f;
	// Поле ввода 2609 (JUI 128,726,336x20) и полоса антифлуда 2618 (там же, ширина 336).
	static constexpr float kEditX = 128.f;
	static constexpr float kEditY = 726.f;
	static constexpr float kEditW = 336.f;
	static constexpr float kEditH = 20.f;
	// ★ Стартовое смещение окна справки 2580 вправо (требование юзера «на 15% правее»).
	//   Считаем от ШИРИНЫ БАЗОВОГО ЭКРАНА: 15 % от 1024 = 153.6 px. От ширины окна (276) вышло бы
	//   41 px — почти незаметно, а «15 % от координаты» — величина без физического смысла.
	//   Окно при этом целиком остаётся на экране: 446 + 153.6 + 276 = 875.6 < 1024.
	static constexpr float kTipStartShiftFrac = 0.15f;

	// ═════════ ГЕОМЕТРИЯ КОНТЕКСТНЫХ МЕНЮ — ПРЯМО ИЗ gamemain_jui.ini ════════════════════════════
	// CHAT_NEW_TABX (80000): Size «158 96», скин Popup_SmWin (gamemain_jui.ini:374802-374938).
	static constexpr float kNickMenuW = 158.f;
	static constexpr float kNickMenuH = 96.f;
	// CHAT_SETUP_Win (2670): Size «158 106», тот же скин (gamemain_jui.ini:97652-97780).
	static constexpr float kTabMenuW = 158.f;
	static constexpr float kTabMenuH = 106.f;
	// Кнопки обоих меню: скины *_W143 — Rect «0 0 143 20».
	static constexpr float kMenuBtnW = 143.f;
	static constexpr float kMenuBtnH = 20.f;
	// ★ Смещение меню 80000 от курсора — `uiShowFriendNotice @0xBB6FD0` (ui_core.c:13563):
	//   `CControl_SetField74_75(this[425], cursor.x + 32, cursor.y − 81)`. Меню всплывает
	//   ВВЕРХ-ВПРАВО от курсора. У окна 2670 смещения НЕТ (`uiWindowShowHideAndRegister`, +0/+0).
	static constexpr float kNickMenuDX = 32.f;
	static constexpr float kNickMenuDY = -81.f;
	// z-порядок всплывающих меню — выше выпадающего списка каналов (9000), см. SetChannelListOpen.
	static constexpr int32 kMenuZ = 9500;
	// ★★★ ПОДСВЕТКА В ЛЕНТЕ 2521 — ЦИФРЫ ДВИЖКА (ui_core.c:112969 и 113126), НЕ СКИНА:
	//   hover-строка и плашка ПОД НИКОМ рисуются ОДНИМ И ТЕМ ЖЕ цветом
	//   `SetRenderState(60 /*D3DRS_TEXTUREFACTOR*/, −1762594576)` = **0x96F0F0F0**.
	//   ⚠ Это цвет-МНОЖИТЕЛЬ рендер-стейта, а не тексель sRGB-текстуры ⇒ гамма-коррекцию
	//   `AsdaSrgbHexToLinear` к нему применять НЕЛЬЗЯ (тот же вывод, что и для списка 2608, Д17.1).
	static constexpr FColor kRibbonHoverColor = FColor(0xF0, 0xF0, 0xF0, 0x96);
	// ★ ЗАШИТАЯ КОНСТАНТА 44 из формулы правой границы плашки ника (ui_core.c:113126-113182):
	//     left = rect.left + ширина(текста до '['),  right = ширина(ника) + 44  ← АБСОЛЮТНАЯ
	//   Число 44 = левый инсет ленты + ширина '[' в авторской раскладке. Формула воспроизводится
	//   КАК ЕСТЬ (1:1), «чинить» её запрещено — при другом положении окна она «съезжает» и в оригинале.
	static constexpr float kRibbonNamePadRight = 44.f;
};
