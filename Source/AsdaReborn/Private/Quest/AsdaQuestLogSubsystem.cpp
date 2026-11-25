// Copyright (c) 2026 @sitxovski (Discord)
// UAsdaQuestLogSubsystem — реализация ЖУРНАЛА ЗАДАНИЙ (окно 700 `QUEST_WIN`).
//   Весь разбор эталона, источники чисел, раскладка контролов и список исправленных дефектов —
//   в шапке `Public/Quest/AsdaQuestLogSubsystem.h`. Здесь только код.
#include "Quest/AsdaQuestLogSubsystem.h"
#include "AsdaPerf.h"   // ★ R4-c (14.11.2026): области замера времени игрового потока
#include "AsdaDevConsole.h"   // ★ античит А2: ASDA_DEV_CVAR / ASDA_DEV_CONSOLE — дев-ключи и команды вне Shipping
#include "UI/AsdaJuiWindowHandle.h"   // ★ волна 3 (15.09.2026): каркас окон JUI
#include "UI/AsdaTradeStateSubsystem.h"   // ★ 30.09.2026 (волна Л5 торговли): гейт «лавка открыта» 22000/23000 → 107022

#include "Quest/AsdaQuestMarkerSubsystem.h"
#include "Npc/AsdaNpcDialogTypes.h"          // FAsdaEpisodeRow, FAsdaQuestRewardTableRow
#include "UI/AsdaWindowSubsystem.h"
#include "UI/AsdaSystemMenuSubsystem.h"
#include "UI/AsdaMessageSubsystem.h"
#include "UI/AsdaIconCellGrid.h"
#include "UI/AsdaTipPanel.h"   // ★ общая точка Resize9Slice (порт uiPanel9SliceLayout)
#include "UI/AsdaUiHelpers.h"
#include "UI/AsdaInventorySubsystem.h"
#include "UI/AsdaMinimapSubsystem.h"
#include "Login/AsdaText.h"   // ★ 09.10.2026: FAsdaText::Get — название рейд-задания из текстовой таблицы
#include "Login/AsdaButtonRelay.h"
#include "World/AsdaLoadManagerSubsystem.h"
#include "World/AsdaNameplateComponent.h"
#include "World/AsdaWorldSubsystem.h"      // ★ имя своего персонажа — ключ сохранения списка отслеживаемых
#include "AsdaNetSubsystem.h"
#include "Npc/AsdaNpcDialogSubsystem.h"   // ★ 19.10.2026: награды завершения 5052 — `ApplyQuestCompletionRewards5052`
#include "AsdaDataTableCache.h"

#include "Blueprint/UserWidget.h"
#include "Blueprint/WidgetTree.h"
#include "Components/CanvasPanel.h"
#include "Components/CanvasPanelSlot.h"
#include "Components/Button.h"
#include "Components/CheckBox.h"
#include "Components/Image.h"
#include "Components/ScrollBox.h"
#include "Components/TextBlock.h"
#include "Engine/DataTable.h"
#include "Engine/Engine.h"
#include "Kismet/GameplayStatics.h"
#include "HAL/IConsoleManager.h"
#include "UI/AsdaCraftSubsystem.h"   // ★ волна В3 ремесла: единый предикат «окно ремесла открыто» (`IsAnyCraftWindowOpen`)
#include "Misc/ConfigCacheIni.h"            // ★ GConfig: список отслеживаемых заданий между сессиями
#include "AsdaOutgoingGate.h"               // ★ С-Р1: общий офлайн-гейт отладочных команд (RefuseDebugWhenOnline)
#include "World/AsdaFishingSubsystem.h"     // ★ 08.10.2026 (рыбалка В4): гейт «идёт рыбалка» кнопок окна заданий (событие 66561)
#include "Text/AsdaLangRows.h"               // [ПОРТ] 09.10.2026 (мультиязычность): тексты книги задания / эпизода / рейда на языке сессии
#include "Quest/AsdaBoardQuestSubsystem.h"   // ★ 28.10.2026: задания доски объявлений 20000…30000 — тип, награды, готовность, имя
#include "UI/AsdaNotice.h"                   // ★ 31.10.2026: нотис провала отказа от задания (5007) — канал эталонного 6956
#include "Quest/AsdaQuestGuideTarget.h"      // ★ 31.10.2026 (баг 6): место цели строки гида без координат — общая точка с картой мира
#include "UI/AsdaTutorialEvents.h"   // ★ 10.11.2026 обучение: флаги шагов гида 1950 (TUTORIAL.md §2.3)
#include "AsdaQuestLogSubsystem_Private.h"   // ★ R3-c 12.11.2026: общее для частей AsdaQuestLogSubsystem*.cpp

DEFINE_LOG_CATEGORY(LogAsdaQuestLog);   // ★ R2: было DEFINE_LOG_CATEGORY_STATIC — категория общая для частей

// ★★ Волна В4 рыбалки (08.10.2026): гейт «идёт рыбалка» кнопок окна заданий. Эталон `CQuest_Episode_OnEvent@0xC5BB30`,
//   событие 66561 (нажатие кнопки, вызов 0xC5BDFD, g:285554): `if (!Input_IsTextInputFocused(g_pChatInputWnd))
//   questPanel_HandleUICommand(this, id); return 1;` — МИСНЕЙМ, это fishState ≠ 0: событие ПОГЛОЩАЕТСЯ молча. Под гейт
//   попадают все кнопки `questPanel_HandleUICommand@0xC5C3B0`: 702 крестик, 780 «ОК» окна приёма, 713 корзина,
//   739/735 разворот/сворот, 705…708 маршрут. Галка слежения 743 (события 67073/67074) и клики ячеек 720…731
//   (событие 5) гейта НЕ имеют. Общая точка (её зовёт и окно приёма 780 в `UAsdaNpcDialogSubsystem`).
bool AsdaQuestEpisodeFishingGate(const UGameInstance* GI, int32 CtrlId)
{
	if (!UAsdaFishingSubsystem::IsFishingNow(GI)) { return false; }   // ★ В3 S7 (N14): одна точка
	UE_LOG(LogAsdaQuestLog, Log, TEXT("[журнал] кнопка %d: идёт рыбалка — событие 66561 поглощено (g:285554)"), CtrlId);
	return true;
}

// ★ Переключатель исправления D7 (ярлык вкладки 742). См. развёрнутое обоснование в `SetupControls`.
static int32 GAsdaQuestLogShowTabLabel = 0;
static ASDA_DEV_CVAR_REF(CVarAsdaQuestLogShowTabLabel,
	TEXT("asda.QuestLog.ShowTabLabel"),
	GAsdaQuestLogShowTabLabel,
	TEXT("1 — показывать ярлык вкладки 742 «Глава» (исправление дефекта оригинала D7). ")
	TEXT("По умолчанию 0: в раскладке данных ярлык накрывается корзиной 713, а другой раскладки в данных нет."),
	ECVF_Default);

// ★★★ ОБВОДКА ТЕКСТА В ОКНЕ 700 — ТРЕБОВАНИЕ ВЛАДЕЛЬЦА «убрать везде».
//   ЧТО В ЭТАЛОНЕ. `CQuest_Episode_BuildUI@0xC5A950` ставит обводку (`CClass_SetField926(…, 0x800000)`)
//   РОВНО тринадцати контролам окна, и все тринадцать — это счётчики поверх иконок:
//     `quest.c:1365` — лоток целей 715…719  (`QUEST_TRAY0…4`);
//     `quest.c:1476` — счётчики базовых наград 1690…1693 (`Quest_Display_RewardIcon_0…3`);
//     `quest.c:1486` — счётчики наград на выбор 1694…1697 (`Quest_Display_CRewardIcon_0…3`).
//   Всем прочим текстам окна (заголовок 703, описание 714, список 734, подписи наград 786…789 /
//   795…798, подпись чекбокса 744, подписи кнопок маршрута) эталон обводку НЕ ставит — та, что
//   видна в порту, приехала от импортёра JUI по отменённому прочтению ключа `TextOutLine`.
//   ЧТО ДЕЛАЕМ. Владелец 2026-08-22 сказал прямо: «убери обводку текста везде — в тайтле, описании
//   квеста, его текста и прочем», перечислив в том числе подписи наград и счётчики целей.
//   ⇒ по умолчанию снимаем у ВСЕХ текстов окна, включая эти тринадцать. Это ОСОЗНАННОЕ отступление
//   от эталона по слову владельца, и оно обратимо БЕЗ ПЕРЕСБОРКИ: `asda.QuestLog.EtalonOutline 1`
//   возвращает эталонный вид (1 px у тринадцати счётчиков, 0 у остальных).
static int32 GAsdaQuestLogEtalonOutline = 0;
static ASDA_DEV_CVAR_REF(CVarAsdaQuestLogEtalonOutline,
	TEXT("asda.QuestLog.EtalonOutline"),
	GAsdaQuestLogEtalonOutline,
	TEXT("0 (по умолчанию) — обводки текста в окне 700 нет нигде (требование владельца). ")
	TEXT("1 — эталонный вид: обводка 1 px у счётчиков 715…719 / 1690…1693 / 1694…1697."),
	ECVF_Default);

// ★★★ ПЛИТКА «0 ЗОЛОТО» — ОСОЗНАННОЕ ОТСТУПЛЕНИЕ ОТ ЭТАЛОНА (решение владельца 2026-08-23).
//
//   ЧТО ДЕЛАЕТ ОРИГИНАЛ. `questBuildRewardSet@0xC663A0` в ветке «обычной» записи занимает ячейку
//   денег при `Money >= 0` (`quest.c:4111`: `if (*(int *)(… + 12) >= 0)`), а не `> 0`. В данных
//   `RewardTable.BIN` `Money == 0` у 150 записей из 560 ⇒ оригинал рисует им плитку «0 Золото»
//   и в окне приёма задания 750, и в панели наград журнала 700.
//
//   ЧТО ДЕЛАЕМ МЫ. Плитку с нулевыми деньгами НЕ показываем: ячейка 0 остаётся пустой, как любая
//   другая незанятая. Ячейки НЕ схлопываются — опыт остаётся на своём месте (ячейка 1), предметы
//   на своих (2 и 3): роль ячейки в эталоне задаётся её НОМЕРОМ, и это правило не тронуто.
//
//   ПОЧЕМУ. Прямое решение владельца от 2026-08-23: «0 Золото» — не награда, а мусор в строке.
//   ★ Это ещё и согласует три окна наград между собой: у попапа сдачи 540 ОРИГИНАЛ САМ гейтит
//     деньги строго (`questStageBuildStatFlags@0xD7E950`: `OutFlags[0] = (Obj.A16 > 0)`), и запись
//     «на выбор» в `questBuildRewardSet` — тоже (`> 0`). То есть `>= 0` в одной ветке из трёх
//     похоже на недосмотр автора, а не на замысел.
//
//   ОТКАТ БЕЗ ПЕРЕСБОРКИ: `asda.Quest.EtalonZeroMoney 1` возвращает эталонное `>= 0`.
int32 GAsdaQuestEtalonZeroMoney = 0;
static ASDA_DEV_CVAR_REF(CVarAsdaQuestEtalonZeroMoney,
	TEXT("asda.Quest.EtalonZeroMoney"),
	GAsdaQuestEtalonZeroMoney,
	TEXT("0 (по умолчанию) — плитка денег не показывается при Money == 0 (решение владельца). ")
	TEXT("1 — эталонное поведение: ячейка денег занимается при Money >= 0, то есть рисуется «0 Золото»."),
	ECVF_Default);

// ★★★ СХЛОПЫВАНИЕ ПУСТЫХ ЯЧЕЕК НАГРАДЫ — ОСОЗНАННОЕ ОТСТУПЛЕНИЕ ОТ ЭТАЛОНА
//     (решение владельца 2026-08-23, разбор целиком — `Reports/quest_reward_collapse.md`).
//
//   ЧТО ДЕЛАЕТ ОРИГИНАЛ. Роль ячейки задаётся её НОМЕРОМ: 0 деньги, 1 опыт, 2 и 3 предметы
//   (`questBuildRewardSet@0xC663A0`). Незанятая ячейка просто прячется, соседи НЕ двигаются,
//   поэтому в ряду остаются дыры.
//
//   ЧТО ДЕЛАЕМ МЫ. Дыр не оставляем: пустая ячейка выкидывается, всё правее сдвигается влево.
//   Слова владельца — сначала «скрылось 0 золота, но опыт выглядит плохо — соседнюю сдвигать
//   влево, на его место», затем уточнение: «СДВИГАТЬ ВСЕГДА, КОГДА ТАМ ПУСТОТА».
//   ⇒ схлопывается ЛЮБАЯ пустая ячейка, а не только спрятанная из-за нулевой величины.
//
//   ★ ПОРЯДОК ОСТАВШИХСЯ ПЛИТОК СОХРАНЯЕТСЯ: деньги → опыт → предмет 1 → предмет 2. Мы только
//     убираем дыры, не переставляя награды между собой.
//
//   ★★★ ГРАНИЦА РЯДОВ. Плиток четыре и лежат они в ДВА ряда по два (`.JUI`: ячейки 0/1 на y = 25,
//     ячейки 2/3 на y = 71). Выбран сдвиг СКВОЗЬ границу ряда, то есть плотная укладка в порядке
//     чтения: первая награда всегда в левой клетке верхнего ряда, дыр нет нигде. Почему так:
//       · это буквально «сдвигать всегда, когда там пустота» — при укладке «внутри каждого ряда»
//         дыры остаются (напр. квест 340: верхний ряд ПУСТОЙ, а единственная награда одиноко
//         висит в нижнем — как раз то, на что владелец жаловался);
//       · окно становится короче там, где второй ряд больше не нужен (455 пар из 525 против 424).
//     Расходятся два варианта ровно на 8 парах из 525. Если владелец захочет «только внутри ряда»
//     — это ОДНО слово: `asda.Quest.CollapseRewardCells 4`.
//
//   ★ БЛОК «НА ВЫБОР» (ячейки 4…7) ЭТА ПРАВКА НЕ ТРОГАЕТ ВООБЩЕ. Его список `OutChoice` и так
//     плотный (собирается через `Add`, пустых элементов в нём не бывает), а главное — индекс
//     строки выбора уходит на сервер. Схлопывание работает ТОЛЬКО над `OutBasic`; соответствие
//     «строка ↔ отправляемый индекс» в попапе сдачи 540 живёт в другом массиве (`RewardSelect`,
//     источник — блоки цели `.NSR`) и этим кодом не пересобирается.
//
//   ОТКАТ БЕЗ ПЕРЕСБОРКИ: `asda.Quest.CollapseRewardCells`.
//     0 — не схлопывать вовсе (эталонные фиксированные роли);
//     1 — только ячейка ДЕНЕГ, спрятанная нулём;
//     2 — любая нулевая ВЕЛИЧИНА (деньги и опыт), настоящая пустота остаётся дырой;
//     3 — ЛЮБАЯ пустота, сдвиг сквозь границу ряда (ПО УМОЛЧАНИЮ, решение владельца);
//     4 — ЛЮБАЯ пустота, но сдвиг только ВНУТРИ ряда.
//   ★ Правка живёт в ОДНОЙ точке — `UAsdaQuestLogSubsystem::BuildRewardLines`, — поэтому окно
//     приёма 750 и панель наград журнала 700 ведут себя одинаково. Попап сдачи 540 не тронут и
//     трогать нечего: он собирает СВОЙ плотный список по занятым категориям (`RewardBasic.Add`
//     в `UAsdaNpcDialogSubsystem`, порт `itemSlotSetupEquipSowelSlots`), то есть схлопывает
//     сам по себе, а нулевые величины у него отсеяны строгими порогами эталона
//     (`questStageBuildStatFlags@0xD7E950`: `OutFlags[0] = (A16 > 0)`).
int32 GAsdaQuestCollapseRewardCells = 3;
static ASDA_DEV_CVAR_REF(CVarAsdaQuestCollapseRewardCells,
	TEXT("asda.Quest.CollapseRewardCells"),
	GAsdaQuestCollapseRewardCells,
	TEXT("Схлопывать ли пустые ячейки награды (окно приёма 750 и панель журнала 700). ")
	TEXT("0 — нет, эталонные фиксированные роли. ")
	TEXT("1 — только ячейка денег, спрятанная нулём. ")
	TEXT("2 — любая нулевая величина (деньги и опыт). ")
	TEXT("3 (по умолчанию) — ЛЮБАЯ пустота, сдвиг сквозь границу ряда. ")
	TEXT("4 — любая пустота, но сдвиг только внутри ряда."),
	ECVF_Default);

// ★ 26.10.2026 [ИСПРАВЛЕНИЕ] (баг игроков «Quest window closing button changes position and shifts after moving the quests tab»,
//   волна quest_close_btn). Узлы у правого края окна (`Quest_RBase_grp` — правый угол титул-бара с крестиком 702,
//   шестерёнки 740/741) `ApplyPageExpanded` ставил АБСОЛЮТНО: `Left = исходный_Left + прирост ширины`. Но эти узлы — прямые
//   дети корневого канваса окна, и тот же слот двигают перетаскивание игроком (`ShiftTargetByKey`) и постановщик окна при
//   открытии (`PlaceWindowKeepingSavedOffset`, сдвиг игрока из раскладки). `ApplyPageExpanded` зовётся при КАЖДОМ открытии
//   (`Open`), развороте/свороте страницы (739/735) и пересоздании окна — после любого из них крестик с шестерёнкой
//   возвращались на авторский X, а окно оставалось там, куда его утащил игрок (кадр владельца 26.10: окно слева, крестик
//   справа сверху отдельно). Y не ломался: его код не трогает.
//   Теперь X ставится общей точкой `AsdaUi::ShiftSlotXFromBase` («от базы» + внешний сдвиг с прошлого вызова, ключ
//   `asda.UI.BaseShiftKeepsDrag`). Самотест — `asda.SelfTest.DragRelayout win=QUEST_WIN …` (Reports/quest_close_btn).
//   1 — так (по умолчанию); 0 — прежнее «исходный X + прирост» (сдвиг окна по X теряется).
int32 GAsdaQuestRightEdgeKeepsDrag = 1;
static ASDA_DEV_CVAR_REF(CVarAsdaQuestRightEdgeKeepsDrag,
	TEXT("asda.QuestLog.RightEdgeKeepsDrag"),
	GAsdaQuestRightEdgeKeepsDrag,
	TEXT("1 (по умолчанию) — крестик 702 и шестерёнки 740/741 окна заданий 700 при открытии и развороте страницы сохраняют ")
	TEXT("сдвиг окна перетаскиванием (исправление 26.10.2026); 0 — прежнее: X от авторской базы, крестик отрывается от рамки."),
	ECVF_Default);

// ★ 31.10.2026 [ИСПРАВЛЕНИЕ] (баг игроков: при отказе от задания «Нельзя удалить предмет», задание висит до перезапуска).
//   Поток (эталон + сервер): корзина 713 → вопрос 6174 (quest.c:1717) → «Да» (ui_core.c:57309, код 0x181E) →
//   `CQuest_Send_Accept@0xC612C0` шлёт C2S 5006 (ОБЩИЙ опкод с уничтожением предмета, под-тип +4 = 4, +0 = ячейка сервера)
//   и ставит `acceptPending` → сервер `CharItemDump@0x4D3410` (GameServer.c:83558), ветка типа 4: `sub_532B30` (эпизод
//   2001…макс или доска 20000+) → БД 2570 → `sendCharQuestDump@0x4DB510` (:86594): успех — обнуляет ячейку (`sub_4D6540`) и
//   шлёт клиенту 5007 {1, ячейка, тип 4} (для доски ещё 6590); провал БД — 5007 {0, ячейка, тип 4}; отказ проверки — 5007 {0}
//   без типа. 5047 после отказа НЕ приходит. Клиент `netOnInventoryUseResult@0xD155A0`: 0 → нотис 6956 в чат; 1 и тип 4 →
//   `uiCraftSlotConfirmRemove@0xC61A30` (снять ячейку, сброс выбора/наград, `npcUiRefreshActionList`, `acceptPending = 0`).
//   Дефекты порта: 5007 типа 4 выбрасывался инвентарём («окно инвентаря не трогаем») — задание оставалось в журнале, трекере
//   и метках до 5047 при входе; защёлки `acceptPending` не было — игрок отказывался повторно, ячейка на сервере уже пуста →
//   5007 {0} → «Нельзя удалить предмет». Дефекты оригинала (исправлены с этим же ключом): провал отказа показан строкой
//   уничтожения предмета 6956 (теперь 6192 «Ошибка данных задания»; результат 2 — 8013, как было); `acceptPending` при провале не
//   снимался (отказ блокировался до конца сессии) и при молчании сервера висел вечно (снимаем ответом и через 10 с).
//   1 (по умолчанию) — так; 0 — прежнее поведение порта (5007 типа 4 игнорируется, защёлки нет, провал = 6956).
int32 GAsdaQuestLogAbandonAck = 1;
static ASDA_DEV_CVAR_REF(CVarAsdaQuestLogAbandonAck,
	TEXT("asda.QuestLog.AbandonAck"),
	GAsdaQuestLogAbandonAck,
	TEXT("1 (по умолчанию) — ответ 5007 на отказ от задания снимает задание из журнала/трекера/меток, повторный отказ до ответа ")
	TEXT("не шлётся, провал — нотис 6192 (исправление 31.10.2026); 0 — прежнее: ответ игнорируется, провал = «Нельзя удалить предмет» 6956."),
	ECVF_Default);

// ★ 06.11.2026 [ИСПРАВЛЕНИЕ] (баг игроков: задания в журнале нельзя переместить и поменять местами). Поток (эталон + сервер):
//   нажатие на ячейку 720…731 = выбор (событие 0xB) и захват (5, `uiSlotCtrl_handleIdRange720`), сдвиг — источник приглушён
//   (6, `uiSlotSetSelectedFillText`), бросок (7, `questLogSlotClick@0xC5E1B0`): на другую ячейку — `CQuest_Send_SelectSlot@0xC5C760`,
//   C2S 5000 46 б (+0 ячейка источника, +4 = 4, +11 ячейка цели, +15 = 4; цель может быть пустой); на корзину 713 — вопрос 6174;
//   мимо или на ту же ячейку — ничего. Сервер `CharItemMove@0x4CE3E0`, ветка типа 4 → БД 2017 `UPItemMove` (`@Plate1 = 4`: правит
//   `CharEpisode.Posx`, порядок переживает перезаход) → `sub_4D12F0` case 4 → 5001 {1, подвид 1 обмен / 2 в пустую, +2 источник,
//   +6 тип, +13 цель, +17 тип}. Клиент `itemMoveApply` → `questSlotMoveSwap@0xC5CEE0`.
//   Дефекты порта: перетаскивания у ячеек не было (`MoveSlot` никто не звал), а 5001 типа 4 инвентарь отбрасывал (`Container(4)`
//   пуст). Отступление от эталона: выбор переезжает по ответу, а не при отправке — у эталона отказ сервера оставлял выбор на
//   чужой (возможно пустой) ячейке; новый перенос и отказ ждут ответа не дольше `kAbandonAckTimeoutSec`.
//   1 (по умолчанию) — так; 0 — прежнее поведение порта (перетаскивания нет, 5001 типа 4 отбрасывается).
int32 GAsdaQuestLogSlotMove = 1;
static ASDA_DEV_CVAR_REF(CVarAsdaQuestLogSlotMove,
	TEXT("asda.QuestLog.SlotMove"),
	GAsdaQuestLogSlotMove,
	TEXT("1 (по умолчанию) — ячейки журнала заданий перетаскиваются мышью, ответ 5001 переставляет задания (исправление 06.11.2026); ")
	TEXT("0 — прежнее: перетаскивания нет, ответ типа 4 отбрасывается."),
	ECVF_Default);

// Авторские смещения узлов правого края (ключ — узел; снимаются `AsdaUi::ShiftSlotXFromBase` при первом вызове, то есть в
//   `SetupControls` до постановки окна). Слабые ключи: пересозданное окно — новые узлы; мёртвые снимает `SetupControls`.
//   Только игровой поток. Отдельно от заголовка, чтобы не пересобирать 18 включений `AsdaQuestLogSubsystem.h`.
TMap<TWeakObjectPtr<UWidget>, FMargin> GQuestRightEdgeBase;

const FName  UAsdaQuestLogSubsystem::kWindowId(TEXT("QUEST_WIN"));
const TCHAR* UAsdaQuestLogSubsystem::kWbpPath = TEXT("/Game/AsdaAssets/UI/Player_UI/Quest/WBP_QUEST_WIN.WBP_QUEST_WIN_C");

namespace QLogLang {
// ★★ [ПОРТ] 09.10.2026 (мультиязычность). Тексты задания для ПОКАЗА игроку — на языке сессии. Указатели — строки самих таблиц
//   (`BookRow` / `EpisodeRow` / `AsdaDt::RowByInt`), ключ = имя строки (обратный индекс `AsdaLangRows::Text`):
//   `QuestBook` — `<файл .QST>.Title` / `.Desc` / `.Steps.<стадия>`; `EpisodeTable` — `<EpisodeId>.Title`;
//   `RaidQuestData` — `<QuestId>.Name`. Русская сессия — прежние поля без поиска.
	// Заголовок книги задания (`.QST` +12).
	FString BookTitle(const FAsdaQuestBookRow* B)
	{
		return B ? AsdaLangRows::Text(AsdaDt::Cached(kBookDT), B, TEXT("Title"), B->Title) : FString();
	}
	// Полное описание задания (свиток 734).
	FString BookDesc(const FAsdaQuestBookRow* B)
	{
		return B ? AsdaLangRows::Text(AsdaDt::Cached(kBookDT), B, TEXT("Desc"), B->Desc) : FString();
	}
	// Текст стадии `Stage` (контрол 714); вне диапазона — пусто.
	FString BookStep(const FAsdaQuestBookRow* B, int32 Stage)
	{
		if (!B || !B->Steps.IsValidIndex(Stage)) { return FString(); }
		return AsdaLangRows::Text(AsdaDt::Cached(kBookDT), B, *FString::Printf(TEXT("Steps.%d"), Stage), B->Steps[Stage]);
	}
	// Название эпизода (`Episode_Table` +0).
	FString EpTitle(const FAsdaEpisodeRow* Ep)
	{
		return Ep ? AsdaLangRows::Text(AsdaDt::Cached(kEpisodeDT), Ep, TEXT("Title"), Ep->Title) : FString();
	}
	// Название рейд-задания (`Raid_quest_data` +12).
	// ★ [ПОРТ] 09.10.2026 (мультиязычность, этап 0): поле `Name` строки `DT_RaidQuestData` при импорте JSON совпадает с
	//   ключом строки (`"Name": "20000"` в `RawAssets/Data/Quest/RaidQuestData.json` — импортёр пишет туда QuestId), то есть
	//   текста в нём нет. Текст названия — текстовая таблица `Text/Quest/DT_Raid_quest_data` (имя @+12, исправлено на этапе 0;
	//   ведомость `RawAssets/Data/_value_overlay/Raid_quest_data.csv`), через `FAsdaText::Get` — она уже на языке сессии.
	//   Нет строки в текстовой таблице — прежнее поле (как было).
	FString RaidName(const FAsdaRaidQuestRow* R)
	{
		if (!R) { return FString(); }
		const FString T = FAsdaText::Get(TEXT("Raid_quest_data"), R->QuestId, TEXT("name"));
		return !T.IsEmpty() ? T : AsdaLangRows::Text(AsdaDt::Cached(kRaidDT), R, TEXT("Name"), R->Name);
	}
}

// ★★★ ЛИМИТА ЗДЕСЬ БОЛЬШЕ НЕТ. Раньше в этом файле жила своя копия числа
//   (`static constexpr int32 kMaxTracked = 6;`) — второе объявление ОДНОГО правила рядом с
//   `kTrackerMaxEntries` трекера. Разойдись они, журнал пустил бы галку, которую трекер молча
//   не показал бы. Теперь и журнал, и трекер спрашивают ЕДИНСТВЕННУЮ точку
//   `UAsdaQuestMarkerSubsystem::TrackerLimit()` (по умолчанию 12; `Asda.Quest.TrackerLimit 6`
//   возвращает эталонное значение оригинала). Разбор — `Reports/quest_tracker_limit12.md`.
// ════════════════════════════════════════════════════════════════════════════════════════════════
//  ЖИЗНЕННЫЙ ЦИКЛ
// ════════════════════════════════════════════════════════════════════════════════════════════════
void UAsdaQuestLogSubsystem::Initialize(FSubsystemCollectionBase& Collection)
{
	Super::Initialize(Collection);
	Collection.InitializeDependency(UAsdaQuestMarkerSubsystem::StaticClass());
	Collection.InitializeDependency(UAsdaWindowSubsystem::StaticClass());

	TrayCodes.Init(0, 5);
	RewardCodes.Init(0, 4);
	ChoiceCodes.Init(0, 4);

	// Консоль — по образцу подсистемы меток (живая проверка без мыши).
#if ASDA_DEV_CONSOLE   // ★ античит А2: дев-команда, в Shipping не регистрируется
	IConsoleManager::Get().RegisterConsoleCommand(TEXT("Asda.QuestLog.Dump"),
		TEXT("Состояние журнала заданий: ячейки, выбор, маршруты, отслеживание."),
		FConsoleCommandWithArgsDelegate::CreateWeakLambda(this, [this](const TArray<FString>&)
		{
			UE_LOG(LogAsdaQuestLog, Log, TEXT("%s"), *Describe());
		}), ECVF_Default);
	IConsoleManager::Get().RegisterConsoleCommand(TEXT("Asda.QuestLog.Open"),
		TEXT("Asda.QuestLog.Open [0|1] — окно журнала: без аргумента тумблер (как клавиша Q), с аргументом — явно закрыть/открыть."),
		FConsoleCommandWithArgsDelegate::CreateWeakLambda(this, [this](const TArray<FString>& A)
		{
			// ★ Аргумент раньше ИГНОРИРОВАЛСЯ, и `Open 1` / `Open 0` оба переключали окно —
			//   в живых проверках это давало случайное состояние. Теперь аргумент действует.
			if (A.Num() == 0) { Toggle(); return; }
			if (FCString::Atoi(*A[0]) != 0) { Open(); } else { Close(); }
		}), ECVF_Default);
	IConsoleManager::Get().RegisterConsoleCommand(TEXT("Asda.QuestLog.Select"),
		TEXT("Asda.QuestLog.Select <ячейка 0..11> — выбрать ячейку журнала (как клик мышью)."),
		FConsoleCommandWithArgsDelegate::CreateWeakLambda(this, [this](const TArray<FString>& A)
		{
			SelectSlot(A.Num() ? FCString::Atoi(*A[0]) : 0);
		}), ECVF_Default);
	IConsoleManager::Get().RegisterConsoleCommand(TEXT("Asda.QuestLog.Track"),
		TEXT("Asda.QuestLog.Track <0|1> — отслеживать выбранное задание (чекбокс 743)."),
		FConsoleCommandWithArgsDelegate::CreateWeakLambda(this, [this](const TArray<FString>& A)
		{
			SetTrackSelected(A.Num() ? (FCString::Atoi(*A[0]) != 0) : true);
		}), ECVF_Default);
#endif // ASDA_DEV_CONSOLE
	// ★ ОФЛАЙН-НАПОЛНЕНИЕ ЖУРНАЛА для живой проверки без сервера. Собирает то же 688-байтовое
	//   тело пакета 5047, что шлёт сервер, из первых N строк `DT_EpisodeTable` и пропускает его
	//   ШТАТНЫМ путём `ApplyQuestStateBlob` — второго хранилища и второго разборщика не заводим.
	//   Отличие от `asda.Quest.FakeState` (там ровно один слот и только для проверки условий):
	//   здесь заполняются все поля записи, которые читает окно 700 — номер ячейки, вид задания,
	//   стадия и пять целей со счётчиками.
	// ★ ПРОВЕРКА СОХРАНЕНИЯ СПИСКА ОТСЛЕЖИВАЕМЫХ. Без неё круг «вышел → зашёл» в PIE не
	//   воспроизвести: подсистемы живут в GameInstance и переживают перезапуск уровня.
	//   `save` — записать сейчас, `restore` — забыть, что восстановление уже было, и поднять
	//   список из ini заново (ровно то, что делает вход в игру), `show` — что лежит в файле.
#if ASDA_DEV_CONSOLE   // ★ античит А2: дев-команда, в Shipping не регистрируется
	IConsoleManager::Get().RegisterConsoleCommand(TEXT("Asda.QuestLog.Tracked"),
		TEXT("Asda.QuestLog.Tracked [show|save|restore] — сохранённый между сессиями список отслеживаемых заданий."),
		FConsoleCommandWithArgsDelegate::CreateWeakLambda(this, [this](const TArray<FString>& A)
		{
			const FString Mode = A.Num() > 0 ? A[0].ToLower() : TEXT("show");
			if (Mode == TEXT("save")) { SaveTrackedToSettings(); }
			else if (Mode == TEXT("restore"))
			{
				TrackedRestoredFor.Reset();          // как при новом входе в игру
				RestoreTrackedFromSettings();
			}
			const FString Key = TrackedSettingsKey();
			FString Val;
			if (GConfig && !Key.IsEmpty()) { GConfig->GetString(kTrackerSection, *Key, Val, GGameUserSettingsIni); }
			TArray<int32> Slots = TrackedSlotIndices();
			FString SlotsStr;
			for (int32 S : Slots) { SlotsStr += FString::Printf(TEXT("%d "), S); }
			// ★ Вся цепочка сохранения видна ОДНОЙ строкой — иначе «не сохраняется» не отличить от
			//   «сохраняется, но не читается» и от «ключа ещё нет» (это и был дефект Е2).
			UE_LOG(LogAsdaQuestLog, Log,
				TEXT("[журнал] ключ=«%s»%s; файл=«%s»; в файле [%s] «%s»; восстановлено для=«%s»; ")
				TEXT("состояние с сервера=%s; сейчас отслеживаются ячейки [%s] (%d шт.)"),
				*Key, Key.IsEmpty() ? TEXT(" (ИМЕНИ ПЕРСОНАЖА ЕЩЁ НЕТ — ждём)") : TEXT(""),
				*GGameUserSettingsIni, kTrackerSection, *Val,
				TrackedRestoredFor.IsEmpty() ? TEXT("—") : *TrackedRestoredFor,
				(Marks() && Marks()->IsQuestStateSynced()) ? TEXT("получено") : TEXT("НЕТ"),
				*SlotsStr.TrimEnd(), Slots.Num());
		}), ECVF_Default);
#endif // ASDA_DEV_CONSOLE

#if ASDA_DEV_CONSOLE   // ★ античит А2: дев-команда, в Shipping не регистрируется
	IConsoleManager::Get().RegisterConsoleCommand(TEXT("Asda.QuestLog.FakeJournal"),
		TEXT("Asda.QuestLog.FakeJournal [сколько=4] [стадия=0] [первыйЭпизод=2001] — офлайн-наполнение журнала (эмуляция 5047).\n")
		TEXT("  Третий аргумент нужен, чтобы поднять КОНКРЕТНЫЕ задания: например 2026 — «Осмотр собирателя живых камней»."),
		FConsoleCommandWithArgsDelegate::CreateWeakLambda(this, [this](const TArray<FString>& A)
		{
			// ★ С-Р1 (26.09.2026), офлайн-гейт: поддельный журнал (5047) — отказ/перенос ушёл бы с номером поддельной ячейки.
			if (FAsdaOutgoingGate::RefuseDebugWhenOnline(this, TEXT("Asda.QuestLog.FakeJournal"))) { return; }
			UAsdaQuestMarkerSubsystem* QM = Marks();
			const UDataTable* Dt = AsdaDt::Cached(kEpisodeDT);
			if (!QM || !Dt) { UE_LOG(LogAsdaQuestLog, Warning, TEXT("[журнал] нет подсистемы состояния или DT_EpisodeTable")); return; }
			const int32 Want  = FMath::Clamp(A.Num() > 0 ? FCString::Atoi(*A[0]) : 4, 1, 12);
			const int32 Stage = A.Num() > 1 ? FCString::Atoi(*A[1]) : 0;
			const int32 First = A.Num() > 2 ? FCString::Atoi(*A[2]) : 2001;

			TArray<const FAsdaEpisodeRow*> Eps;
			for (const TPair<FName, uint8*>& It : Dt->GetRowMap())
			{
				const FAsdaEpisodeRow* R = reinterpret_cast<const FAsdaEpisodeRow*>(It.Value);
				if (R && R->EpisodeId >= FMath::Max(2001, First) && !R->QstFile.IsEmpty()) { Eps.Add(R); }
			}
			Eps.Sort([](const FAsdaEpisodeRow& X, const FAsdaEpisodeRow& Y) { return X.EpisodeId < Y.EpisodeId; });

			TArray<uint8> Blob; Blob.SetNumZeroed(688);
			auto W32 = [&Blob](int32 O, int32 V) { Blob[O] = V & 0xFF; Blob[O+1] = (V>>8)&0xFF; Blob[O+2] = (V>>16)&0xFF; Blob[O+3] = (V>>24)&0xFF; };
			auto W16 = [&Blob](int32 O, int32 V) { Blob[O] = V & 0xFF; Blob[O+1] = (V>>8)&0xFF; };
			const int32 N = FMath::Min(Want, Eps.Num());
			for (int32 i = 0; i < N; ++i)
			{
				const int32 B = 44 * i;
				W32(B + 0, Eps[i]->EpisodeId);      // +0  код эпизода (>= 2001 = ячейка занята)
				Blob[B + 4] = 4;                    // +4  категория-байт («запись журнала квестов»)
				W16(B + 5, i);                      // +5  НОМЕР ЯЧЕЙКИ — им адресуют 5006/5000
				Blob[B + 7] = 2;                    // +7  вид задания: 2 = «убить/состояние»
				W16(B + 8, Eps[i]->QuestKey);       // +8  номер квеста (ключ QDT/QCT/наград)
				W16(B + 10, Stage);                 // +10 стадия
				for (int32 j = 0; j < 5; ++j) { W32(B + 14 + 6*j, 0); W16(B + 18 + 6*j, 0); }
			}
			QM->ApplyQuestStateBlob(Blob);
			UE_LOG(LogAsdaQuestLog, Log, TEXT("[журнал] ОФЛАЙН-НАПОЛНЕНИЕ: %d ячеек, стадия %d, с эпизода %d"),
				N, Stage, N > 0 ? Eps[0]->EpisodeId : -1);
			for (int32 i = 0; i < N; ++i)
			{
				UE_LOG(LogAsdaQuestLog, Log, TEXT("   ячейка %d: эпизод %d, квест %d, «%s» (книга %s)"),
					i, Eps[i]->EpisodeId, Eps[i]->QuestKey, *Eps[i]->Title, *Eps[i]->QstFile);
			}
		}), ECVF_Default);
#endif // ASDA_DEV_CONSOLE
	// ★ ОФЛАЙН-ЭМУЛЯЦИЯ ПАКЕТА 5052 «прогресс целей». Собирает НАСТОЯЩЕЕ 508-байтовое тело по
	//   эталонной раскладке и гонит его через ТОТ ЖЕ `ApplyQuestProgressBlob`, что и сеть, —
	//   доказательная проверка того, что счётчики целей в журнале действительно двигаются
	//   (без сервера этого проверить больше нечем).
#if ASDA_DEV_CONSOLE   // ★ античит А2: дев-команда, в Shipping не регистрируется
	IConsoleManager::Get().RegisterConsoleCommand(TEXT("Asda.QuestLog.FakeProgress"),
		TEXT("Asda.QuestLog.FakeProgress <ячейка> <кодЦели> <счётчик> — эмуляция пакета 5052."),
		FConsoleCommandWithArgsDelegate::CreateWeakLambda(this, [this](const TArray<FString>& A)
		{
			// ★ С-Р1 (26.09.2026), офлайн-гейт: поддельный прогресс (5052) квестов в живой сессии.
			if (FAsdaOutgoingGate::RefuseDebugWhenOnline(this, TEXT("Asda.QuestLog.FakeProgress"))) { return; }
			UAsdaQuestMarkerSubsystem* QM = Marks();
			if (!QM || A.Num() < 3) { UE_LOG(LogAsdaQuestLog, Warning, TEXT("[журнал] нужно 3 аргумента")); return; }
			const int32 SlotNo  = FMath::Clamp(FCString::Atoi(*A[0]), 0, 11);
			const int32 ObjCode = FCString::Atoi(*A[1]);
			const int32 Count   = FCString::Atoi(*A[2]);
			const FAsdaQuestSlot* S = SlotAt(SlotNo);
			if (!IsSlotBusy(S)) { UE_LOG(LogAsdaQuestLog, Warning, TEXT("[журнал] ячейка %d пуста"), SlotNo); return; }

			TArray<uint8> Body; Body.SetNumZeroed(508);
			auto W32 = [&Body](int32 O, int32 V) { Body[O] = V & 0xFF; Body[O+1] = (V>>8)&0xFF; Body[O+2] = (V>>16)&0xFF; Body[O+3] = (V>>24)&0xFF; };
			auto W16 = [&Body](int32 O, int32 V) { Body[O] = V & 0xFF; Body[O+1] = (V>>8)&0xFF; };
			W16(0, S->QuestId);          // +0  episodeId
			Body[4] = 2;                 // +4  objType != 1 ⇒ ветка ОБНОВЛЕНИЯ слота
			W32(13, 0);                  // +13 <= 0 ⇒ не ветка завершения
			// +22 — 44-байтовая запись слота журнала (раскладка 5047)
			W32(22 + 0, S->QuestId);
			Body[22 + 4] = 4;
			W16(22 + 5, SlotNo);
			Body[22 + 7] = 2;
			W16(22 + 8, S->QuestNo);
			W16(22 + 10, S->Stage);
			W32(22 + 14, ObjCode);
			W16(22 + 18, Count);
			QM->ApplyQuestProgressBlob(Body);
			UE_LOG(LogAsdaQuestLog, Log,
				TEXT("[журнал] ЭМУЛЯЦИЯ 5052: ячейка %d, цель %d = %d"), SlotNo, ObjCode, Count);
		}), ECVF_Default);
	IConsoleManager::Get().RegisterConsoleCommand(TEXT("Asda.QuestLog.Page"),
		TEXT("Asda.QuestLog.Page <0|1> — свернуть/развернуть правую страницу-свиток 732 (кнопки 739/735)."),
		FConsoleCommandWithArgsDelegate::CreateWeakLambda(this, [this](const TArray<FString>& A)
		{
			SetPageExpanded(A.Num() ? (FCString::Atoi(*A[0]) != 0) : true);
		}), ECVF_Default);
#endif // ASDA_DEV_CONSOLE
	// ★ Кнопки маршрута 705…708 («=> цель — карта, x, y») — тот же путь, что и клик мышью:
	//   зовётся ровно `TravelToRoute`, второго тела нет. Нужна для живой проверки Д3.
#if ASDA_DEV_CONSOLE   // ★ античит А2: дев-команда, в Shipping не регистрируется
	IConsoleManager::Get().RegisterConsoleCommand(TEXT("Asda.QuestLog.Route"),
		TEXT("Asda.QuestLog.Route [номер 0..3] — нажать кнопку маршрута выбранной ячейки.\n")
		TEXT("  Без аргумента — только перечислить маршруты выбранной ячейки."),
		FConsoleCommandWithArgsDelegate::CreateWeakLambda(this, [this](const TArray<FString>& A)
		{
			for (int32 i = 0; i < Routes.Num(); ++i)
			{
				UE_LOG(LogAsdaQuestLog, Log, TEXT("[журнал] маршрут %d: карта %d, клетка (%d,%d), «%s»"),
					i, Routes[i].MapId, Routes[i].X, Routes[i].Y, *Routes[i].Text);
			}
			if (A.Num() >= 1) { TravelToRoute(FCString::Atoi(*A[0])); }
		}), ECVF_Default);
#endif // ASDA_DEV_CONSOLE
	// Разбор «почему задание считается выполненным» — тем же ЕДИНСТВЕННЫМ вычислителем условий.
#if ASDA_DEV_CONSOLE   // ★ античит А2: дев-команда, в Shipping не регистрируется
	IConsoleManager::Get().RegisterConsoleCommand(TEXT("Asda.QuestLog.WhyDone"),
		TEXT("Asda.QuestLog.WhyDone [номер квеста] — разбор условия завершения по таблице QCT "
		     "(без аргумента берётся выбранная ячейка журнала)."),
		FConsoleCommandWithArgsDelegate::CreateWeakLambda(this, [this](const TArray<FString>& A)
		{
			UAsdaQuestMarkerSubsystem* QM = Marks();
			if (!QM) { return; }
			int32 QuestNo = -1;
			if (A.Num()) { QuestNo = FCString::Atoi(*A[0]); }
			else if (const FAsdaQuestSlot* S = SlotAt(ActiveSlot)) { QuestNo = S->QuestNo; }
			UE_LOG(LogAsdaQuestLog, Log, TEXT("%s"), *QM->DescribeQuestComplete(QuestNo));
		}), ECVF_Default);
	IConsoleManager::Get().RegisterConsoleCommand(TEXT("Asda.QuestLog.Abandon"),
		TEXT("Отказаться от выбранного задания (модалка → C2S 5006 под-тип 4)."),
		FConsoleCommandWithArgsDelegate::CreateWeakLambda(this, [this](const TArray<FString>&)
		{
			AbandonSelected();
		}), ECVF_Default);
#endif // ASDA_DEV_CONSOLE
}

void UAsdaQuestLogSubsystem::Deinitialize()
{
	UnregisterZones();
	if (UGameInstance* GI = GetGameInstance())
	{
		if (UAsdaQuestMarkerSubsystem* QM = GI->GetSubsystem<UAsdaQuestMarkerSubsystem>())
		{
			QM->OnQuestStateChanged.RemoveAll(this);
		}
		if (UAsdaBoardQuestSubsystem* Board = GI->GetSubsystem<UAsdaBoardQuestSubsystem>()) { Board->OnBoardChanged.RemoveAll(this); }
		if (UAsdaNetSubsystem* Net = GI->GetSubsystem<UAsdaNetSubsystem>())
		{
			Net->OnQuestProgress.RemoveAll(this);
		}
		if (UAsdaSystemMenuSubsystem* Menu = GI->GetSubsystem<UAsdaSystemMenuSubsystem>())
		{
			Menu->OnMenuCommand.RemoveDynamic(this, &UAsdaQuestLogSubsystem::OnMenuCommand);
			Menu->UnregisterWindow(kMenuButtonIdNum);
		}
		if (UAsdaWindowSubsystem* WM = GI->GetSubsystem<UAsdaWindowSubsystem>())
		{
			WM->UnregisterWindowCloser(kWindowId);
			WM->CloseWindow(kWindowId);
		}
	}
	Win = nullptr; bReady = false; Relays.Reset();
	Super::Deinitialize();
}

// ★★★ К-3 (пара к Р1 в `UAsdaQuestMarkerSubsystem`): окно журнала, выбранная ячейка и признак
//   «трекер восстановлен для персонажа X» относятся к СЕССИИ. Комментарий на `:2355` обещал, что
//   при выходе в выбор персонажа записи чистит `TrackerClear` — обещание теперь выполняется
//   (`UAsdaQuestMarkerSubsystem::ResetSessionState`), а здесь снимается собственная защёлка.
void UAsdaQuestLogSubsystem::ResetSessionState(EAsdaSessionResetReason Reason)
{
	// При смене карты журнал на сервере цел — трогаем только маршруты, они посчитаны для карты.
	Routes.Reset();
	bDirty = true;
	CancelSlotDrag();   // ★ 06.11.2026: призрак принадлежит окну — на смене карты и выходе «в руке» ничего не остаётся
	SlotDbl.Reset();    // ★ 10.11.2026: серия щелчков не переживает сброс
	if (!AsdaIsFullSessionReset(Reason)) { return; }

	UE_LOG(LogAsdaQuestLog, Log, TEXT("[журнал] сброс сессии (%s): окно=%d, ячейка=%d, трекер восстановлен для «%s»"),
		AsdaSessionResetReasonName(Reason), bWindowOpen ? 1 : 0, ActiveSlot, *TrackedRestoredFor);
	if (bWindowOpen) { Close(); }
	bPageExpanded = false;
	ActiveSlot = -1;
	// Имя персонажа, для которого трекер уже восстановлен из ini: у следующего входа оно другое.
	TrackedRestoredFor.Empty();
	bTrackedWaitLogged = false;
	// ★ ДЛИНУ СОХРАНЯЕМ (2026-09-02). Эти три массива — фиксированной длины (5/4/4, см. `Initialize`),
	//   и часть кода читает их по индексу. `Reset()` делает массив ПУСТЫМ: сейчас это не падает
	//   только потому, что `RefreshRewards`/заполнение лотка переинициализируют их у себя в начале.
	//   Такой же `Reset()` у `BurstCache` хранителя душ уже уронил редактор
	//   (`Array index out of bounds: 2 into an array of size 0`), поэтому здесь — `Init`, как в
	//   `Initialize`: обнуляем ЗНАЧЕНИЯ, а не длину.
	TrayCodes.Init(0, 5); RewardCodes.Init(0, 4); ChoiceCodes.Init(0, 4);
	// ★ 31.10.2026: ожидание ответа на отказ от задания относится к сессии (новый персонаж — новый журнал).
	bAbandonPending = false; AbandonSlotIdx = -1; AbandonServerSlot = -1; AbandonQuestNo = 0; bAbandonWasTracked = false;
	bMovePending = false;   // ★ 06.11.2026: то же для ожидания ответа на перенос
}

void UAsdaQuestLogSubsystem::Tick(float /*DeltaTime*/)
{
	ASDA_PERF_SCOPE("Tick.QuestLog");   // ★ R4-c (14.11.2026): область замера (AsdaPerf.h; в Shipping пусто)
	UGameInstance* GI = GetGameInstance();
	if (!GI) { return; }

	// Подписки ЛЕНИВЫЕ и ровно по одной: сеть и метки поднимаются не раньше нас.
	if (!bStateBound)
	{
		if (UAsdaQuestMarkerSubsystem* QM = GI->GetSubsystem<UAsdaQuestMarkerSubsystem>())
		{
			QM->OnQuestStateChanged.AddWeakLambda(this, [this]()
			{
				bDirty = true;
				// ★ Экранный трекер 1800 живёт независимо от того, ОТКРЫТО ли окно журнала: строки
				//   «есть/надо» обязаны двигаться от пакета 5052 даже при закрытом журнале. В эталоне
				//   так же — `QuestReward_UpdateProgressList` зовётся из обработчика пакета, а не из
				//   отрисовки окна.
				RebuildAllTrackerEntries();
			});
			bStateBound = true;
			// ★ 28.10.2026: узлы доски (6588/6590/6595, «Собрать») меняют иконки ячеек 20000+ и строки трекера.
			if (UAsdaBoardQuestSubsystem* Board = GI->GetSubsystem<UAsdaBoardQuestSubsystem>())
			{
				Board->OnBoardChanged.AddWeakLambda(this, [this]() { bDirty = true; RebuildAllTrackerEntries(); });
			}
			UE_LOG(LogAsdaQuestLog, Log, TEXT("[журнал] подписка на изменения состояния квестов установлена"));
		}
	}
	// ★★★ 5052 «прогресс целей» — раньше пакет не обрабатывался вовсе. Разбор байт делает
	//   подсистема-хранилище (единственная), здесь только маршрутизация и звук/строка игроку.
	if (UAsdaNetSubsystem* Net = GI->GetSubsystem<UAsdaNetSubsystem>())
	{
		if (!Net->OnQuestProgress.IsBoundToObject(this))
		{
			Net->OnQuestProgress.AddWeakLambda(this, [this](const TArray<uint8>& Body)
			{
				UAsdaQuestMarkerSubsystem* QM = Marks();
				if (!QM) { return; }
				bool bQuestFinished = false;
				FAsdaQuestProgressDelta Delta;
				const int32 SlotNo = QM->ApplyQuestProgressBlob(Body, &bQuestFinished, &Delta);
				// ★★★ ДОБАВЛЕНО 2026-08-22 (`Reports/quest_notice_sound_fix.md` §2.3).
				//   Ветка ЗАВЕРШЕНИЯ эталона (`questItemResult_apply`, quest.c:3446-3457:
				//   `missionSetEntryFlag` + `csoundPlayName2DStore(g_pSoundMgr, 14, 0)`) журнал не
				//   переписывает, поэтому `ApplyQuestProgressBlob` возвращает −1 — и звук 14
				//   терялся на раннем возврате. Теперь ветка различима явным признаком.
				if (bQuestFinished)
				{
					PlaySound(14);
					// ★ 19.10.2026: сразу за звуком 14 эталон зовёт `charApplyFullStatusPacket(g_pNpcWnd, тело)` (quest.c:3455) —
					//   блок наград завершения (золото/предметы/очки/обаяние); общая точка строк наград — диалог NPC.
					if (UAsdaNpcDialogSubsystem* Dlg = GetGameInstance()->GetSubsystem<UAsdaNpcDialogSubsystem>())
					{
						Dlg->ApplyQuestCompletionRewards5052(Body);
					}
					// ★ Эталон в ветке завершения строку 6019 НЕ шлёт, но показывает на экране
					//   «<Эпизод> (Предмет задания Готово)» — тем же жёлтым в окне без рамки.
					//   Эталон (`quest.c:3499-3501`): «<Эпизод> (<6161><6004>)» — строки берутся из
					//   таблицы текста, а не сочиняются здесь.
					const FString EpTitle = (SlotNo >= 0) ? TitleForSlot(SlotNo) : FString();
					ShowQuestProgressOnScreen(FString::Printf(TEXT("%s (%s%s)"),
						*EpTitle, *Loc(6161), *Loc(6004)));
					return;
				}
				if (SlotNo < 0) { return; }

				// ★★★ ЭКРАННАЯ СТРОКА ПРОГРЕССА — то, чего порту не хватало. Эталон
				//   (`questItemResult_apply`) кроме строки в журнал пишет ещё и на экран, в окно 221
				//   (`uiChatLogAddLine(…, −256, 0, 10000.0, …)`): «< Задание > Предмет : есть/надо».
				//   У окна 221 нет подложки, поэтому это выглядит как золотой текст без рамки.
				{
					FString Line;
					if (BuildQuestProgressLine(Delta, Line)) { ShowQuestProgressOnScreen(Line); }
				}
				// Эталон `questItemResult_apply`: звук 13 и строка нотиса 6019 в системный чат.
				PlaySound(13);
				if (UAsdaMessageSubsystem* Msg = GetGameInstance()->GetSubsystem<UAsdaMessageSubsystem>())
				{
					// ★ ЦВЕТ — как в эталоне, а не общий для категории. `questItemResult_apply`
					//   (`quest.c:3487`) зовёт `CChat_AddTokenizedLines(…, -256, 10000, 0)`, где
					//   −256 = 0xFFFFFF00 — ЧИСТЫЙ ЖЁЛТЫЙ. Наш маршрут категории «Квест» красит
					//   золотым 0xFFD306, и строка прогресса выглядела иначе, чем в игре.
					FAsdaMsgParams MP;
					MP.Category  = EAsdaMsgCategory::Quest;
					MP.LocId     = 6019;
					// [ПОРТ] 09.10.2026 (мультиязычность, этап 3): RU-фолбэк убран — нет строки 6019 → «[#6019]» (общая точка
					//   UAsdaMessageSubsystem), английский игрок русского не увидит.
					MP.bUseColor = true;
					MP.Color     = FColor(0xFF, 0xFF, 0x00, 0xFF);   // −256 эталона
					Msg->Post(MP);
				}
				bDirty = true;
			});
		}
	}

	// Подключение к кнопке 73404 главного меню. ДВЕ РАЗНЫЕ ВЕЩИ, и раньше делалась только вторая:
	//   1) `OnMenuCommand` — САМА КОМАНДА кнопки (эталон: событие 67073/67074 → диспетчер);
	//   2) `RegisterWindow` — только ПОДСВЕТКА кнопки из фактической видимости окна.
	//   Без (1) клик по кнопке не открывал журнал (клавиша Q при этом работала — у неё свой путь).
	if (!bMenuRegistered)
	{
		if (UAsdaSystemMenuSubsystem* Menu = GI->GetSubsystem<UAsdaSystemMenuSubsystem>())
		{
			Menu->OnMenuCommand.AddDynamic(this, &UAsdaQuestLogSubsystem::OnMenuCommand);
			Menu->RegisterWindow(kMenuButtonIdNum, kWindowId);
			bMenuRegistered = true;
			UE_LOG(LogAsdaQuestLog, Log,
				TEXT("[журнал] кнопка %d (NEW_SYSTEMMAIN_QUESTBUT, окно 73400) подключена: команда + подсветка окна %s"),
				kMenuButtonIdNum, *kWindowId.ToString());
		}
	}

	// ★★★ ДОГОНЯЮЩЕЕ ВОССТАНОВЛЕНИЕ СПИСКА ОТСЛЕЖИВАЕМЫХ (лечение Е2).
	//   `RebuildAllTrackerEntries` зовётся только на ИЗМЕНЕНИЕ состояния квестов, а имя персонажа
	//   появляется ПОЗЖЕ последнего такого изменения (см. `TrackedSettingsKey`). Поэтому попытку
	//   повторяем здесь — ровно до первого успеха для этого ключа; смена персонажа меняет ключ и
	//   восстановление произойдёт заново само. Вызов дешёвый: обе проверки — сравнение строк.
	{
		const FString Key = TrackedSettingsKey();
		if (!Key.IsEmpty() && TrackedRestoredFor != Key)
		{
			RestoreTrackedFromSettings();
		}
	}

	if (bWindowOpen) { TickSlotDrag(); }

	if (bWindowOpen && bDirty)
	{
		bDirty = false;
		RefreshAll();
	}
}

// ════════════════════════════════════════════════════════════════════════════════════════════════
//  ВИДЖЕТ
// ════════════════════════════════════════════════════════════════════════════════════════════════
bool UAsdaQuestLogSubsystem::EnsureWidget()
{
	// ★ Каркас окна — `FAsdaJuiWindowHandle::Ensure` (волна 3 аудита дублей): гвард `IsWindowLive`,
	//   сброс протухшего окна, открытие WBP, привязка, скрытие — в одной точке, в прежнем порядке.
	const EAsdaJuiEnsure R = FAsdaJuiWindowHandle(kWindowId, kWbpPath, kWinZ).Ensure(GetGameInstance(), Win, bReady,
		[this](UAsdaWindowSubsystem& WM)
		{
			WM.CloseWindow(kWindowId);
			Win = nullptr;
			bReady = false;
		},
		[this](UAsdaWindowSubsystem&)
		{
			SetupControls();
			RegisterZones();
			// В `.JUI` у корня 700 `Visible = "0"` — окно рождается скрытым.
		},
		[]()
		{
			UE_LOG(LogAsdaQuestLog, Error, TEXT("[журнал] WBP_QUEST_WIN не загрузился (%s)"), kWbpPath);
		},
		[this](UAsdaWindowSubsystem& WM)
		{
			// Роспись «как меня закрыть по-игровому» (порт строки эталонного `uiCloseAllWindows`).
			WM.RegisterWindowCloser(kWindowId, FSimpleDelegate::CreateWeakLambda(this, [this]()
			{
				if (bWindowOpen) { Close(); }
			}));
		});
	return FAsdaJuiWindowHandle::Succeeded(R) && bReady;
}

// ★★★ УСТОЙЧИВЫЙ ПОИСК УЗЛА (владелец 2026-08-25: «в окне квестов нет иконок наград»).
//   В живом логе окно 700 разбиралось ДВАЖДЫ: до перехода на другую карту — «награды=да»,
//   после — «награды=НЕТ». Имена этих узлов несут порядковые суффиксы импортёра
//   (`Quest_Display_RewardIcon_0_71_0`), и точный поиск об них спотыкается.
//   Здесь: сначала точное имя, затем — по префиксу до порядкового хвоста. Если и так не нашли,
//   пишем в журнал ИМЯ — чтобы следующий такой случай не пришлось искать вслепую.
static UWidget* NodeLoose(const UUserWidget* Host, const TCHAR* Exact, const TCHAR* What)
{
	if (UWidget* W = AsdaUi::FindNodeExact(Host, Exact)) { return W; }
	if (!Host || !Host->WidgetTree) { return nullptr; }
	// Префикс = имя без хвоста «_<число>_<число>» (его дописывает импортёр окон).
	FString Want(Exact);
	{
		int32 Cut = INDEX_NONE;
		FString Tail = Want;
		for (int32 Pass = 0; Pass < 2; ++Pass)
		{
			int32 Us = INDEX_NONE;
			if (!Tail.FindLastChar(TEXT('_'), Us)) { break; }
			const FString Part = Tail.Mid(Us + 1);
			if (Part.IsEmpty() || !Part.IsNumeric()) { break; }
			Tail = Tail.Left(Us);
			Cut = Us;
		}
		if (Cut != INDEX_NONE) { Want = Want.Left(Cut); }
	}
	UWidget* Found = nullptr;
	Host->WidgetTree->ForEachWidget([&Found, &Want](UWidget* W)
	{
		if (Found || !W) { return; }
		if (W->GetName().StartsWith(Want, ESearchCase::IgnoreCase)) { Found = W; }
	});
	if (!Found)
	{
		UE_LOG(LogAsdaQuestLog, Warning, TEXT("[журнал] узел не найден: %s (искали «%s», префикс «%s»)"),
			What ? What : TEXT("?"), Exact, *Want);
	}
	return Found;
}

void UAsdaQuestLogSubsystem::SetupControls()
{
	bReady = false;
	// ★ 06.11.2026: призрак — узел прежнего экземпляра окна; новое окно начинает без захвата (`window-pointers-stale-after-travel`).
	SlotDragGhost = FAsdaDragGhost(); SlotDragArm.Disarm(); SlotDragSrc = -1; SlotMouse.Reset();
	if (!Win || !Win->WidgetTree) { return; }

	TitleText   = Cast<UTextBlock>(Node(Win, QLogUi::Title));
	DisplayText = Cast<UTextBlock>(Node(Win, QLogUi::Display));
	TabBarText  = Cast<UTextBlock>(Node(Win, QLogUi::TabBarText));
	TrackLabel  = Cast<UTextBlock>(Node(Win, QLogUi::TrackLabel));
	RewardHdrA  = Cast<UTextBlock>(Node(Win, QLogUi::RewardHdrA));
	RewardHdrB  = Cast<UTextBlock>(Node(Win, QLogUi::RewardHdrB));
	TrackBox    = Cast<UCheckBox> (Node(Win, QLogUi::TrackBox));
	BtnClose    = Cast<UButton>   (Node(Win, QLogUi::BtnClose));
	BtnTrash    = Cast<UButton>   (Node(Win, QLogUi::BtnTrash));
	BtnExpand   = Cast<UButton>   (Node(Win, QLogUi::BtnExpand));
	BtnCollapse = Cast<UButton>   (Node(Win, QLogUi::BtnCollaps));
	RightPage   = Node(Win, QLogUi::RightPage);
	SlotSel     = Cast<UImage>    (Node(Win, QLogUi::SlotSel));

	// ★ Девять планов рамки окна `QUEST_WIN_0_0…0_8` — их надо ПЕРЕТЯГИВАТЬ при развороте
	//   страницы-свитка (см. `ApplyPageExpanded`).
	FrameNine.Reset();
	for (int32 i = 0; i < 9; ++i)
	{
		FrameNine.Add(Cast<UImage>(Node(Win, *FString::Printf(TEXT("QUEST_WIN_0_%d"), i))));
	}

	// ★ УЗЛЫ, ПРИВЯЗАННЫЕ К ПРАВОМУ КРАЮ САМОГО ОКНА (`.JUI` `AlignXPos = "2 …"` — режим 2,
	//   «отсчёт от правого края родителя»). Родитель у них — окно 700, а оно при развороте
	//   страницы становится шире, значит эти узлы обязаны уехать вправе вместе с краем.
	//   Во всём окне таких ровно ТРИ (проверено сканом всех 120 контролов блока `.JUI`: режим 2
	//   стоит у 31 контрола, но у остальных 28 родитель — вложенная группа неизменной ширины):
	//     `Quest_RBase` (правый угол титул-бара, внутри него крестик 702 `QUEST_TOPCLOSE`),
	//     `Quest_Gear` 740 и `Quest_AniGear` 741 (шестерёнка у крестика).
	//   Импортёр JUI выставляет им ФИКСИРОВАННЫЙ отступ (он раскладывает по экрану, а не по
	//   изменяемому родителю), поэтому сдвиг делаем в рантайме — ровно на прирост ширины окна.
	RightEdgeNodes.Reset();
	RightEdgeBaseX.Reset();
	// ★ 26.10.2026: базы прежнего (снятого) экземпляра окна больше не нужны — снять мёртвые ключи.
	for (auto It = GQuestRightEdgeBase.CreateIterator(); It; ++It) { if (!It.Key().IsValid()) { It.RemoveCurrent(); } }
	for (const TCHAR* Nm : { TEXT("Quest_RBase_grp"), TEXT("Quest_Gear_1_0"), TEXT("Quest_AniGear_2_0") })
	{
		UWidget* W = Node(Win, Nm);
		const UCanvasPanelSlot* CS = W ? AsdaUi::SlotOf(W) : nullptr;
		if (!CS) { continue; }
		RightEdgeNodes.Add(W);
		RightEdgeBaseX.Add(CS->GetOffsets().Left);
	}

	SlotIcons.Reset();
	for (int32 i = 0; i < 12; ++i) { SlotIcons.Add(Cast<UImage>(Node(Win, QLogUi::SlotIcon[i]))); }
	TrayCounts.Reset();
	for (int32 i = 0; i < 5; ++i) { TrayCounts.Add(Cast<UTextBlock>(Node(Win, QLogUi::TrayText[i]))); }
	RouteBtns.Reset(); RouteTexts.Reset();
	for (int32 i = 0; i < 4; ++i)
	{
		RouteBtns.Add(Cast<UButton>(Node(Win, QLogUi::RouteBtn[i])));
		RouteTexts.Add(Cast<UTextBlock>(Node(Win, QLogUi::RouteText[i])));
	}
	RewardTexts.Reset(); ChoiceTexts.Reset();
	for (int32 i = 0; i < 4; ++i)
	{
		RewardTexts.Add(Cast<UTextBlock>(Node(Win, QLogUi::RewardText[i])));
		ChoiceTexts.Add(Cast<UTextBlock>(Node(Win, QLogUi::ChoiceText[i])));
	}

	// ── Подписи, которые в оригинале ставит КОД в рантайме (в `.JUI` их нет) ──
	if (TrackLabel)  { TrackLabel->SetText(FText::FromString(Loc(QLoc::kTrackLabel))); }
	if (RewardHdrA)  { RewardHdrA->SetText(FText::FromString(Loc(QLoc::kRewardBase))); }
	if (RewardHdrB)  { RewardHdrB->SetText(FText::FromString(Loc(QLoc::kRewardChoice))); }
	if (TabBarText)
	{
		// FIX-ORIG D7. В `.JUI` ярлык вкладки 742 `Visible="0"`, текст ему в `CQuest_Episode_BuildUI`
		//   ставят (нотис 6101 «Глава»), а `SetVisible(1)` не зовут НИГДЕ — подпись не видно никогда.
		//   Текст ставим ВСЕГДА (как эталон), показ — по переключателю.
		// ★★★ ПОЧЕМУ ПО УМОЛЧАНИЮ ВЫКЛЮЧЕНО. Проверено живым PIE: включённый ярлык (скин
		//   `QuestTabBtn_Img`, 72×22, светлый) вылезает ровно под КОРЗИНУ 713 (`Trash_Btn`, 22×22),
		//   которая в раскладке стоит на тех же координатах. В оригинале этого никто не видел
		//   именно потому, что ярлык был невидим, и «правильной» раскладки для видимого ярлыка в
		//   данных НЕТ — придумывать её нельзя (закон `never-invent-only-original`).
		//   Поэтому дефект задокументирован, исправление реализовано и включается одной командой:
		//     `asda.QuestLog.ShowTabLabel 1`
		TabBarText->SetText(FText::FromString(Loc(QLoc::kTabChapter)));
		const bool bShowTab = (GAsdaQuestLogShowTabLabel != 0);
		TabBarText->SetVisibility(bShowTab ? ESlateVisibility::SelfHitTestInvisible : ESlateVisibility::Collapsed);
		if (UWidget* Img = Node(Win, QLogUi::TabBarImg))
		{
			Img->SetVisibility(bShowTab ? ESlateVisibility::SelfHitTestInvisible : ESlateVisibility::Collapsed);
		}
	}
	// FIX-ORIG D8: блок «награда на выбор» в эталоне гасится в BuildUI и больше не оживает.
	//   Оставляем скрытым по умолчанию — показывать его будет только `RefreshRewards`, если у
	//   пары (questNo, stage) реально есть записи с `ChoiceFlag`.
	if (RewardHdrB) { RewardHdrB->SetVisibility(ESlateVisibility::Collapsed); }

	// ── Кнопки ──
	if (BtnClose)    { BtnClose->OnClicked.RemoveAll(this);    BtnClose->OnClicked.AddDynamic(this, &UAsdaQuestLogSubsystem::OnCloseClicked); }
	if (BtnTrash)    { BtnTrash->OnClicked.RemoveAll(this);    BtnTrash->OnClicked.AddDynamic(this, &UAsdaQuestLogSubsystem::OnTrashClicked); }
	if (BtnExpand)   { BtnExpand->OnClicked.RemoveAll(this);   BtnExpand->OnClicked.AddDynamic(this, &UAsdaQuestLogSubsystem::OnExpandClicked); }
	if (BtnCollapse) { BtnCollapse->OnClicked.RemoveAll(this); BtnCollapse->OnClicked.AddDynamic(this, &UAsdaQuestLogSubsystem::OnCollapseClicked); }
	if (TrackBox)    { TrackBox->OnCheckStateChanged.RemoveAll(this); TrackBox->OnCheckStateChanged.AddDynamic(this, &UAsdaQuestLogSubsystem::OnTrackChanged); }

	// ── Кнопки маршрута 705…708: реле с индексом ──
	Relays.Reset();
	for (int32 i = 0; i < 4; ++i)
	{
		if (!RouteBtns[i]) { continue; }
		UAsdaButtonRelay* R = NewObject<UAsdaButtonRelay>(this);
		R->Index = i;
		R->Callback = [this](int32 Idx) { if (AsdaQuestEpisodeFishingGate(GetGameInstance(), 705 + Idx)) { return; } TravelToRoute(Idx); };   // ★ В4 рыбалки
		RouteBtns[i]->OnClicked.AddDynamic(R, &UAsdaButtonRelay::OnClicked);
		AsdaUi::AnchorRelay(Relays, R, RouteBtns[i]);   // ★ 31.10.2026: якорь чистит мёртвые/перепривязанные (`asda.UI.RelayAnchorPrune`)
	}

	// ── 12 ячеек журнала: прозрачные клик-кнопки поверх планов-иконок ──
	//   Сеткой `UAsdaIconCellGrid` здесь пользоваться НЕЛЬЗЯ: она рисует иконку ПРЕДМЕТА из атласа
	//   item-таблицы, а иконка ячейки журнала — отдельная текстура по имени из `Episode_Table +252`.
	//   Клик-оверлей поэтому свой, но ровно той же конструкции (реле + прозрачная кнопка).
	//   ★★★ САМИ КНОПКИ ПЕРЕЕХАЛИ В АССЕТ (2026-08-27, требование «править в ассете, а не кодом»).
	//   ЧТО БЫЛО ЗДЕСЬ. Цикл конструировал 12 `UButton` с прозрачным стилем, ставил каждой позицию
	//   плана-иконки, размер 32×32 (`.JUI Size = "32 32"`) и z=50.
	//   ПОЧЕМУ УБРАНО. Позиции — сетка 6×2 с шагом 33 px, вбитая импортёром из `.JUI`
	//   (`QUEST_INVEN0_28_0` … `QUEST_INVEN11_39_0` в группе `Quest_Inven_Base_grp`), размер
	//   постоянен. От рантайм-состояния не зависит ничто: содержимое ячейки меняет ТЕКСТУРУ плана,
	//   а не его прямоугольник.
	//   ГДЕ ЖИВЁТ ТЕПЕРЬ. Узлы `QUEST_INVEN{0..11}_HIT` лежат в `WBP_QUEST_WIN`; создаёт их шаг
	//   `Tools/asda_ui_geometry_bake.py` (блок G4), он в `STEPS`, сторожа — в `HANDMADE`.
	//   Здесь остаётся ТОЛЬКО привязка делегатов — это логика, а не геометрия.
	int32 BoundCells = 0;
	for (int32 i = 0; i < 12; ++i)
	{
		UButton* Btn = Cast<UButton>(AsdaUi::FindNodeByName(Win,
			*FString::Printf(TEXT("QUEST_INVEN%d_HIT"), i), nullptr));
		if (!Btn) { continue; }
		AsdaUi::BindButton(this, Btn, i, [this](int32 Idx) { OnSlotClicked(Idx); }, Relays);   // ★ U20: реле клика — общая точка
		++BoundCells;
	}
	if (BoundCells != 12)
	{
		UE_LOG(LogAsdaQuestLog, Warning,
			TEXT("[журнал] клик-реле ячеек: привязано %d из 12 — прогони "
			     "Tools/asda_ui_geometry_bake.py (шаг G4)"), BoundCells);
	}

	// ── Лоток целей — ОБЩАЯ сетка иконок предметов (второй копии не заводим). Цели всегда
	//   ПРЕДМЕТЫ, поэтому атласная сетка тут подходит.
	{
		TArray<FName> Cells;
		for (int32 i = 0; i < 5; ++i) { Cells.Add(FName(QLogUi::TrayIcon[i])); }
		TrayGrid = NewObject<UAsdaIconCellGrid>(this);
		TrayGrid->SetCellSizePx(32.f);                // JUI: ячейки лотка 32×32
		TrayGrid->SetupNamed(Win, Cells, /*bMale*/ true,
			[this](int32 Cell) { return TrayCodes.IsValidIndex(Cell) ? TrayCodes[Cell] : 0; },
			[](int32) {});                            // клик по цели в эталоне ничего не делает
	}
	// ── Иконки наград сеткой НЕ рисуются: среди наград есть виды БЕЗ кода предмета —
	//   ОПЫТ (плоская текстура `exp2`) и ХАРИЗМА (`charisma-icon`), которых атласная сетка
	//   нарисовать не может (у неё вход — код предмета). Именно поэтому у «150 Опыт» иконки не было.
	//   Рисуем теми же двумя общими точками, что и попап наград 540:
	//     предмет → `UAsdaInventorySubsystem::ApplyIconToImage`, плоская текстура →
	//     `UAsdaTextureSubsystem::LoadTextureCached`. Своей третьей логики иконок не заводим.
	// ★★★ 18.10.2026 (иконок «золото/опыт» нет после перехода на карту): окно пересоздаётся (`EnsureWidget`), а массивы
	//   без сброса росли до 8 — `Redraw` рисовал в картинки [0..3] СТАРОГО, снятого экземпляра. Сброс, как у соседних массивов.
	RewardIcons.Reset(); ChoiceIcons.Reset();
	for (int32 i = 0; i < 4; ++i)
	{
		RewardIcons.Add(Cast<UImage>(NodeLoose(Win, QLogUi::RewardIcon[i], TEXT("иконка награды"))));
		ChoiceIcons.Add(Cast<UImage>(NodeLoose(Win, QLogUi::ChoiceIcon[i], TEXT("иконка награды на выбор"))));
	}

	// ── Список-свиток 734 `QUEST_CENTERLIN` ──
	//   У JUI это list-контрол (EXS `VertScrollPosType="1"`), поэтому импортёр оставил на его месте
	//   только зону хит-теста. Строим прокручиваемый текст ровно в её прямоугольнике.
	if (UWidget* Hit = Node(Win, QLogUi::ListHit))
	{
		UCanvasPanelSlot* HS = AsdaUi::SlotOf(Hit);
		UPanelWidget* Host = Hit->GetParent();
		if (HS && Host)
		{
			DescScroll = Win->WidgetTree->ConstructWidget<UScrollBox>(UScrollBox::StaticClass(), TEXT("QUEST_CENTERLIN_Scroll"));
			DescText   = Win->WidgetTree->ConstructWidget<UTextBlock>(UTextBlock::StaticClass(), TEXT("QUEST_CENTERLIN_Text"));
			if (DescScroll && DescText)
			{
				// ★ Закон `ui-autowrap-canvas-gotcha`: автоперенос без ЯВНОЙ ширины в канвасе ломает
				//   раскладку. Внутри ScrollBox ширина определена, но ставим её явно — надёжнее.
				DescText->SetAutoWrapText(true);
				DescText->SetWrapTextAt(FMath::Max(40.f, (float)HS->GetSize().X - 8.f));
				if (DisplayText) { DescText->SetFont(DisplayText->GetFont()); }
				// EXS списка 734 в JUI цвета текста не задаёт; эталон кладёт строки чёрным
				//   (`uiChatLogAddLine(..., -16777216, ...)`) — лист «бумажный», светлый.
				DescText->SetColorAndOpacity(FSlateColor(AsdaUi::SrgbU32ToLinear(0xFF000000)));
				DescScroll->AddChild(DescText);
				if (UCanvasPanelSlot* NS = Cast<UCanvasPanelSlot>(Host->AddChild(DescScroll)))
				{
					NS->SetAnchors(HS->GetAnchors());
					NS->SetPosition(HS->GetPosition());
					NS->SetSize(HS->GetSize());
					NS->SetAlignment(HS->GetAlignment());
					NS->SetZOrder(10);
				}
			}
		}
	}

	bPageExpanded = false;
	ApplyPageExpanded();
	ApplyTextOutline();
	bReady = (TitleText != nullptr && SlotIcons.Num() == 12 && SlotIcons[0] != nullptr);
	UE_LOG(LogAsdaQuestLog, Log,
		TEXT("[журнал] окно 700 разобрано: заголовок=%s ячеек=%d лоток=%s награды=%s список=%s маршрут-кнопок=%d"),
		TitleText ? TEXT("да") : TEXT("НЕТ"),
		SlotIcons.Num(),
		TrayGrid ? TEXT("да") : TEXT("НЕТ"), (RewardIcons.Num() == 4 && RewardIcons[0]) ? TEXT("да") : TEXT("НЕТ"),
		DescText ? TEXT("да") : TEXT("НЕТ"),
		(RouteBtns.Num() == 4 && RouteBtns[0]) ? 4 : 0);
}

// ★ ОБВОДКА ТЕКСТА ОКНА 700 — один проход по ВСЕМУ дереву (обоснование — в блоке CVar выше).
//   Список — ровно те тринадцать контролов, которым обводку ставит эталон; при выключенном
//   `asda.QuestLog.EtalonOutline` он пуст, и обводки не остаётся нигде.
//   ★ Обход — ОБЩАЯ точка `AsdaUi::ApplyTextOutlineTree`; своей копии не заводим. Её же зовёт
//     попап наград 540 (`Reports/quest_reward_window_fix.md`).
//   Почему проходом по дереву, а не поимённым списком «снять у этих»: поимённый список
//   (а) легко недосчитать — именно так обводка осталась у счётчиков и подписей, и
//   (б) не знает о НОВЫХ текстовых узлах, которые приносит переимпорт JUI уже обведёнными.
void UAsdaQuestLogSubsystem::ApplyTextOutline()
{
	// ★★★ ТЕЛО УБРАНО 2026-08-27: обводка расставлена по данным оригинала В САМИХ АССЕТАХ
	//   (шаг `Tools/asda_ui_text_style926.py` по таблице `Tools/data/text_style926.tsv` —
	//   реверс поля 926; правило импортёра «скиновый `TextOutLine=1` ⇒ обводка», читавшее
	//   поле 925 «флаги переноса», отключено).
	//   ★ ТОЧНОЕ СОВПАДЕНИЕ: в данных у окна `QUEST_WIN` обводка положена РОВНО 13 контролам —
	//   715…719 (`QUEST_TRAY0…4`), 1690…1693 (`Quest_Display_RewardIcon_0…3`) и
	//   1694…1697 (`Quest_Display_CRewardIcon_0…3`), — то есть в точности тому списку, который
	//   держался здесь вручную. Проход по дереву стал избыточным, а после отключения правила
	//   импортёра — и вредным: он перезаписывал бы уже правильный ассет.
	//   Функция сохранена как точка вызова (её зовут из нескольких мест) и намеренно пуста.
	(void)Win;
}

void UAsdaQuestLogSubsystem::RegisterZones()
{
	UGameInstance* GI = GetGameInstance();
	UAsdaWindowSubsystem* WM = GI ? GI->GetSubsystem<UAsdaWindowSubsystem>() : nullptr;
	if (!WM) { return; }
	WM->RegisterUiZone(kWindowId, FAsdaUiZoneRect::CreateWeakLambda(this,
		[this](FVector2D& Mn, FVector2D& Mx) { return GetWindowRect(Mn, Mx); }));
	WM->RegisterWheelHandler(kWindowId,
		FAsdaUiWheelHandler::CreateUObject(this, &UAsdaQuestLogSubsystem::OnWheel));
}

void UAsdaQuestLogSubsystem::UnregisterZones()
{
	if (UGameInstance* GI = GetGameInstance())
	{
		if (UAsdaWindowSubsystem* WM = GI->GetSubsystem<UAsdaWindowSubsystem>())
		{
			WM->UnregisterUiZone(kWindowId);
			WM->UnregisterWheelHandler(kWindowId);
		}
	}
}

bool UAsdaQuestLogSubsystem::GetWindowRect(FVector2D& OutMin, FVector2D& OutMax) const
{
	if (!bWindowOpen || !Win) { return false; }
	bool bAny = false;
	FVector2D Mn(BIG_NUMBER, BIG_NUMBER), Mx(-BIG_NUMBER, -BIG_NUMBER);
	auto Accum = [&](const UWidget* W)
	{
		if (!W || W->GetVisibility() == ESlateVisibility::Collapsed) { return; }
		FVector2D P, Q;
		if (!AsdaUi::WidgetAbsRect(W, P, Q)) { return; }
		Mn = FVector2D(FMath::Min(Mn.X, P.X), FMath::Min(Mn.Y, P.Y));
		Mx = FVector2D(FMath::Max(Mx.X, Q.X), FMath::Max(Mx.Y, Q.Y));
		bAny = true;
	};
	// Рамка окна — девять кусков `QUEST_WIN_0_0…0_8`; плюс правая страница, когда развёрнута.
	for (int32 i = 0; i < 9; ++i) { Accum(Node(Win, *FString::Printf(TEXT("QUEST_WIN_0_%d"), i))); }
	if (bPageExpanded) { Accum(RightPage); }
	if (!bAny) { return false; }
	OutMin = Mn; OutMax = Mx;
	return true;
}

bool UAsdaQuestLogSubsystem::OnWheel(int32 Notches)
{
	if (!bWindowOpen || !DescScroll) { return false; }
	if (!AsdaUi::IsCursorOverWidget(DescScroll)) { return false; }
	DescScroll->SetScrollOffset(FMath::Max(0.f, DescScroll->GetScrollOffset() - 30.f * (float)Notches));
	return true;
}

// ════════════════════════════════════════════════════════════════════════════════════════════════
//  ОТКРЫТИЕ / ЗАКРЫТИЕ (порт `uiQuestGuide_Open@0xC60280` и `uiWnd_Close@0xC604C0`)
// ════════════════════════════════════════════════════════════════════════════════════════════════
void UAsdaQuestLogSubsystem::Open()
{
	UGameInstance* GI = GetGameInstance();
	UAsdaWindowSubsystem* WM = GI ? GI->GetSubsystem<UAsdaWindowSubsystem>() : nullptr;
	if (!WM) { return; }

	// ── ГЕЙТЫ эталона. Оригинал молча не открывает окно при открытых парикмахерской, магазине,
	//   складе, окне предметов, диалоге количества и при ЗАБЛОКИРОВАННОМ инвентаре; при открытой
	//   личной лавке пишет нотис 107022.
	//   В порту из этого списка представлены: инвентарь (флаг «окно занято обменом/лавкой») и
	//   общий признак «игровые окна убраны полноэкранной сценой». Остальные окна списка в порт
	//   ещё не портированы — строка занесена в `PORT_PENDING_ON_SYSTEMS.md`, чтобы гейты доросли
	//   вместе с этими окнами, а не потерялись.
	if (WM->ArePlayerWindowsHidden())
	{
		UE_LOG(LogAsdaQuestLog, Log, TEXT("[журнал] открытие отклонено: активна полноэкранная сцена"));
		return;
	}
	// ★ Волна В3 ремесла (ревизия В0–В2 С-1): из списка гейтов `uiQuestGuide_Open@0xC60280` (ui_core.c:33571)
	//   подключены окна ремесла — `!tileInteract_isControlVisible(g_pItemMainWnd) && !isBoundControlVisible(
	//   g_pItemQtyMakeDlg)`: при открытом 76000/77500 окно молча не открывается.
	if (UAsdaCraftSubsystem::IsAnyCraftWindowOpen(GI))
	{
		UE_LOG(LogAsdaQuestLog, Log, TEXT("[журнал] открытие отклонено молча: открыто окно ремесла 76000/77500 (ui_core.c:33571)"));
		return;
	}
	// ★ Волна Л5 торговли (30.09.2026): [ОРИГИНАЛ] `uiQuestGuide_Open@0xC60280` (ui_core.c:33573-33576) — после молчаливых гейтов
	//   `if (sub_934E86(g_pSellWindow) || sub_929DC4(g_bSomeFeatureEnabled))` → 107022 в чат (−11514, 10 с), окно не открывается
	//   (проверка идёт ДО `if (!this->flagWndOpen)` — при уже открытом журнале нотис тоже выводится).
	if (UAsdaTradeStateSubsystem::IsSelfStoreOpen(GI))
	{
		UAsdaTradeStateSubsystem::ChatStoreGate(GI, QLoc::kOwnShop, TEXT("uiQuestGuide_Open"));
		return;
	}
	if (bWindowOpen) { return; }               // эталон: `if (!this->flagWndOpen)`
	// ★ 10.11.2026 [ОРИГИНАЛ] обучение: `uiQuestGuide_Open` (ui_core.c:33580) — внутри `!flagWndOpen` первой строкой байт 92.
	AsdaTutorial::Mark(GetGameInstance(), EAsdaTutorialFlag::QuestWindowOpen);
	if (!EnsureWidget()) { return; }

	bWindowOpen = true;
	PlaySound(QSnd::Open);
	WM->SetWindowVisible(kWindowId, true);
	WM->BringWindowToFront(kWindowId);
	// Эталон при открытии показывает свёрнутое состояние (`SetVisible(pQuestTabA,1)`,
	//   `SetVisible(pQuestTabB,0)` + `CloseHide`) — то есть правая страница спрятана.
	bPageExpanded = false;
	ApplyPageExpanded();
	// Первый выбор: эталон в `questRewardRefreshSlot` работает с текущим activeSlot; если ничего
	//   не выбрано — берём первую ЗАНЯТУЮ ячейку, иначе окно открывалось бы пустым.
	if (ActiveSlot < 0 || !IsSlotBusy(SlotAt(ActiveSlot)))
	{
		ActiveSlot = -1;
		for (int32 i = 0; i < 12; ++i) { if (IsSlotBusy(SlotAt(i))) { ActiveSlot = i; break; } }
	}
	RefreshAll();
	UE_LOG(LogAsdaQuestLog, Log, TEXT("[журнал] окно 700 открыто (выбрана ячейка %d)"), ActiveSlot);
}

void UAsdaQuestLogSubsystem::Close()
{
	if (!bWindowOpen) { return; }
	CancelSlotDrag();   // ★ 06.11.2026: закрытое окно не держит призрак
	SlotDbl.Reset();    // ★ 10.11.2026: щелчок до закрытия не складывается с щелчком после открытия
	bWindowOpen = false;
	PlaySound(QSnd::Close);
	if (UGameInstance* GI = GetGameInstance())
	{
		if (UAsdaWindowSubsystem* WM = GI->GetSubsystem<UAsdaWindowSubsystem>())
		{
			WM->SetWindowVisible(kWindowId, false);
		}
	}
	UE_LOG(LogAsdaQuestLog, Log, TEXT("[журнал] окно 700 закрыто"));
}

void UAsdaQuestLogSubsystem::CloseUnconditional()
{
	if (bWindowOpen) { Close(); return; }   // открыто — обычное закрытие (звук 6 внутри)
	// Закрыто: у эталона `uiWnd_Close` гейта нет — первой строкой `csoundPlayName2DStore(6)` (ui_core.c:33607), затем
	//   скрытие уже скрытого окна. Порт повторяет звук; скрывать нечего.
	PlaySound(QSnd::Close);
	UE_LOG(LogAsdaQuestLog, Log, TEXT("[журнал] uiWnd_Close при закрытом окне 700: только звук 6 (как эталон)"));
}

void UAsdaQuestLogSubsystem::Toggle()
{
	// Эталон `input.c:4287`, `case 34`: `if (g_bQuestGuideOpen) uiWnd_Close(); else uiQuestGuide_Open();`
	if (bWindowOpen) { Close(); } else { Open(); }
}

// ★★★ Кнопка 73404 `NEW_SYSTEMMAIN_QUESTBUT` панели главного меню 73400.
//   Эталон: `checkBox_OnToggle@0x12FDF70` шлёт окну 67073/67074, `uiWindowMsgProc@0xE90220`
//   отдаёт их в `uiHotkey_DispatchCommand` / `uiMainMenu_OnCommand`, а те зовут ТУ ЖЕ команду 34,
//   что и клавиша Q (`input.c:4287`). Поэтому здесь именно Open/Close журнала, без своей ветки.
//   ⚠ Колбэк прилетает ИЗНУТРИ обработчика чекбокса панели, но `Open`/`Close` трогают только своё
//   окно и сам чекбокс панели не переключают — реентрантности нет (та же схема, что у навыков).
void UAsdaQuestLogSubsystem::OnMenuCommand(int32 ButtonIdNum, int32 /*TargetWindowId*/, bool bOpen)
{
	if (ButtonIdNum != kMenuButtonIdNum) { return; }
	UE_LOG(LogAsdaQuestLog, Log, TEXT("[журнал] кнопка меню %d → %s"),
		ButtonIdNum, bOpen ? TEXT("открыть") : TEXT("закрыть"));
	if (bOpen) { Open(); } else { Close(); }
}

void UAsdaQuestLogSubsystem::OnCloseClicked() { if (AsdaQuestEpisodeFishingGate(GetGameInstance(), 702)) { return; } Close(); }
