// Copyright (c) 2026 @sitxovski (Discord)
// UAsdaExtraMenuSubsystem — ДОПОЛНИТЕЛЬНОЕ МЕНЮ «Прочее» (`ETC_NEW_WIN`, JUI IDNum 74600,
//   `gamemain_jui.ini:350542`; рантайм-класс эталона `CExtraMenuWnd`, глобал `g_pExtraMenuWnd`).
//   Список пунктов-ярлыков: клик по строке открывает соответствующую систему игры, строку можно
//   перетащить в хотбар (слот типа 5). Полный реверс — `Reports/extra_menu.md`.
//
// ═══════════════════════════ КАНОН: КАКОЕ ОКНО ЖИВО ════════════════════════════════════════════
//   `ETC_NEW_WIN` (74600) — ЕДИНСТВЕННОЕ. Дубля-легаси у этого окна в `.JUI` НЕТ (grep по
//   `gamemain_jui.ini`: `ETC_NEW_WIN` — одно вхождение, строка 350542; второго `ETC*_WIN` нет),
//   поэтому закон `jui-prefer-new-windows` здесь ничего не выбирает — выбирать не из чего.
//   Ассет — `WBP_ETC_NEW_WIN` (импорт JUI, 167 виджетов, дамп `Reports/_etc_widgets.log`).
//   ПЕРЕГЕНЕРАЦИЮ WBP НЕ ЗАПУСКАЛИ (закон `no-ui-regeneration-without-permission`).
//
// ═══════════════════════════ ВСЁ СОДЕРЖИМОЕ — ИЗ ФАЙЛА ДАННЫХ ══════════════════════════════════
//   Эталон `CExtraMenu_UIInit@0xECD220` = `loadExtraMenuTable@0xECD2A0` + `quickSlotUI_RefreshIconSlots@0xECD870`.
//   Таблица `./Data/Extra_menu_table.BIN` (сигнатура «EMT», запись 116 б) задаёт СОСТАВ, ПОРЯДОК,
//   ПОДПИСИ, ИКОНКИ, КОМАНДЫ и ПОДСКАЗКИ. Раскладка полей — `Public/Data/AsdaExtraMenuRow.h`.
//   В RU-сборке владельца записей ровно **11** (1296 б = 20 + 11×116) — столько же пунктов и на
//   скриншоте. Порт читает их из `DT_ExtraMenu` (импорт `Tools/asda_build_extra_menu_dt.py`).
//
//   ★★★ В `.JUI` МЕСТ ПОД ПУНКТЫ 16, А НЕ 11, И ЭТО НЕ ОШИБКА РАЗБОРА. Окно содержит 16 строк
//   `BOX_1`…(74701…74716), у каждой подпись (74651…74666) и иконка (74611…74626); эталон в
//   `quickSlotUI_RefreshIconSlots@0xECD870` крутит `for (i = 0; i < 16; ++i)` и для `i >= count`
//   зовёт `SetVisible(строка, 0)`. То есть лишние 5 строк ГАСЯТСЯ ДАННЫМИ, а не удалены из окна.
//   Раскладка строк: колонка 1 — строки 1…8 (x=2, y=2+41k), колонка 2 — строки 9…16 (x=206).
//   При 11 записях получается 8 слева + 3 справа — ровно как на скриншоте владельца.
//
//   ★ МЁРТВЫЕ ПУНКТЫ РАЗМЕТКИ (в RU-таблице их нет, поэтому в игре они не появляются):
//   `ETC_NEW_Partyfind` (поиск отряда), `ETC_NEW_Select_Premium` (премиум-гача),
//   `ETC_NEW_PremiumShop` (премиум-магазин) — имена строк `.JUI` остались от корейской сборки.
//   ⚠ ИМЕНА КОНТРОЛОВ `.JUI` ЗДЕСЬ ВРУТ ВООБЩЕ (закон `false-negatives-method`): подпись строки
//   эталон ПЕРЕТИРАЕТ текстом из таблицы, поэтому `ETC_NEW_smelt_t` («плавка») в живой игре
//   показывает «Создание снаряжения», `ETC_NEW_set_up_t` — «Улучшение снаряжения» и так далее.
//   Строку №N занимает запись №N таблицы, а не одноимённый контрол.
//
// ═══════════════════════════ ПУНКТЫ RU-ТАБЛИЦЫ И ЧТО ОНИ ОТКРЫВАЮТ ═════════════════════════════
//   Разбор команды — `uiOpenWindowDispatch@0xECD570` (эталонный `switch`, НЕ поле таблицы;
//   поэтому и у нас это код, а не данные).
//     ★ 09.10.2026 (мультиязычность, этап 0): колонка «подск.» — id RU-файла Фантазиума; в DT_ExtraMenu теперь id
//       EN-оригинала той же сборки = эти + 5 (110930…110941; «Поиск отряда» — 110932), см. `AsdaExtraMenuRow.h`.
//     № cmd  подпись                 подск.  цель эталона                              порт
//     0  0   Меню званий             110925  `questLogWnd_Open(&g_pCharTitleMgr)` — тогл  НЕТ
//     1  1   Меню Друга              110926  `CFindSoulmateWin`: видно → скрыть,         ★ ДА
//                                            иначе запрос `netSendMaskedPacket_op5483`   (волна 1-А, 06.10.2026:
//                                            окно поиска 8200, его поднимает ответ 5485)
//     2  2   Меню личного магазина   110928  `UI_DispatchTargetContextMenu(2, 7)` →      НЕТ
//                                            если лоток не открыт — `netSendPlayerInfo6300`
//     3  5   Создание снаряжения     110929  `tileInteractGateAndOpen(g_pItemMainWnd)`   НЕТ
//     4  6   Улучшение снаряжения    110930  `uiCombineWindow_Close(g_pInventoryWnd)`†   НЕТ
//     5  4   Разборка снаряжения     110931  `upgradeUiOnOpen(g_pRecvUiHandler3)`†       НЕТ
//     6  3   Меню питомца            110932  `CRefineWnd_Open(g_bDeviceReloadPending)`†  НЕТ
//     7  8   Меню рыболовства        110933  `targetSelectNearestEntity(...)` — найти    НЕТ
//                                            ближайшее место и начать; отказ — нотис 110700
//     8  9   Книга о рыболовстве     110934  `itemCollectUiOpen(g_pUpgradeWindow)` — 74000 НЕТ
//     9 10   Элитный почтовый ящик   110935  тогл окна 69000                             ★ ДА
//    10 11   Добыча                  110936  автомат добычи (`g_pAntiCheat`†, опкод 6752) НЕТ
//     ★ 7   Поиск отряда            110927  окно 8500 `Find_Party_WIn` (тоггл)            ★ ДА
//           — ВОССТАНОВЛЕННАЯ запись; в исходной RU-таблице её нет (11 записей вместо 14)
//   † МИСНОМЕРЫ ЭТАЛОННОГО ДЕКОМПИЛА, проверены по читателям (закон `false-negatives-method`):
//     `uiCombineWindow_Close` — это ТОГГЛ окна улучшения (первая строка: видно → скрыть);
//     `upgradeUiOnOpen(CDisjoint*)` — окно РАЗБОРКИ (внутри `decomposeWnd_RefreshSlots`, нотис
//       98700 «Приступить к демонтажу»);
//     `g_bDeviceReloadPending` — указатель на окно ПИТОМЦА (`CPetWindow_RenderToTexture`,
//       `netRecvPetListPacket(CPetData*)` — `AsdaStory_game.c:19030/14332`);
//     `g_pAntiCheat` — автомат ДОБЫЧИ, а не античит: состояние 1 проверяет
//       `playerCheckEquipType10AndHeldType36()` (надета лопата + держит заряд), иначе нотис 111129;
//       состояние 2 шлёт `antiCheat_SendHeartbeat6752`.
//   ★★★ Команда 7 — «ПОИСК ОТРЯДА» (окно 8500), ВОССТАНОВЛЕНА 2026-08-29. В эталоне `case 7`
//     отсутствует (таблица переходов `0xECD6B0` ведёт код 7 в метку `default`), и в RU-таблице
//     записи с ним нет — но за ним стоит целая написанная подсистема `CFindPartyWnd`
//     (238 контролов, 8 опкодов, живой сервер). Запись возвращена в `DT_ExtraMenu`
//     (`Idx=2, Cmd=7`, иконка (2,0), подсказка 110927, подпись — текст 11800), ветка
//     `kCmdPartyFind` открывает `UAsdaPartyFindSubsystem`. Разбор —
//     `Reports/extra_menu_partyfind.md` и `Reports/party_find_8500.md`.
//
// ═══════════════════════════ ЧТО ПОРТИРОВАНО, А ЧТО ОТКАЗЫВАЕТ ЧЕСТНО ══════════════════════════
//   Портированы ТРИ пункта — «Меню званий», «Поиск отряда» (восстановлен) и «Элитный почтовый ящик» (`UAsdaPremiumPostSubsystem`, окно
//   69000). Остальные 10 систем в порту ещё нет; заглушек не делаем (`no-simplifications-stubs`),
//   молча не глотаем (`no-scope-cutting`): клик пишет причину в журнал и выдаёт нотис 990100
//   («… ещё не перенесена: [ss1]») с ПОДПИСЬЮ ПУНКТА в слоте. Все 10 записаны в
//   `Docs/Port/PORT_PENDING_ON_SYSTEMS.md`, чтобы их подключили при появлении систем.
//
// ═══════════════════════════ ВХОДЫ В ОКНО ══════════════════════════════════════════════════════
//   1. КНОПКА ТРЕЯ `NEW_ETC_M_BUT` (73409, `gamemain_jui.ini:349476`), таб 9, подсказка 9800
//      «Доп. меню». Эталон: `uiHotkey_DispatchCommand@0xE8F670` case 73409 и
//      `uiMainMenu_OnCommand@0xE8F880` case 73409 — ОБА зовут одну функцию-тоггл
//      `uiToggleSkillTreePanel@0xECDB70` (мисномер: она про доп-меню, а не про дерево навыков).
//   2. КРЕСТИК `ETC_NEW_Xbtn` (74689): `uiListControlOnUiEvent`, `a4 == 66561 && a3 == 74689`
//      → тот же тоггл.
//   3. СЛОТ ХОТБАРА ТИПА 5. Строку меню можно перетащить в квик-бар
//      (`uiHotbarSlotDispatch@0xECDEC0` → `uiHotbarAssignSlot@0xECDFB0`, слоты 73011…73022 и
//      73161…73172), и тогда пункт запускается прямо с панели. В порту приёмная сторона уже
//      готова — `FAsdaHotbarMiscBridge` (`GetExtraMenuIcon` / `OpenExtraMenuItem` /
//      `ShowExtraMenuTooltip`); мы её ЗАПОЛНЯЕМ, второй копии логики не заводим.
//
// ═══════════════════════════ ПЕРЕТАСКИВАНИЕ СТРОКИ В ПАНЕЛЬ (сделано 2026-08-30) ═══════════════
//   ★★★ ЭТО НЕ НАША ВЫДУМКА — ВОЗМОЖНОСТЬ ЕСТЬ В ОРИГИНАЛЕ. Доказано ДВУМЯ независимыми
//   методами (закон `false-negatives-method`):
//     1) КОД. `uiListControlOnUiEvent@0xECDC70` (`ui_core.c:87330…87415`):
//          case 5u:  bClickActive = 1; uiSetFocusIndexFromCmd(this, idnum);   // → возврат 2
//          case 7u:  uiHotbarSlotDispatch(this, controlUnderCursor); bClickActive = 0;
//        `uiSetFocusIndexFromCmd@0xECDE40` кладёт в поле +8 номер строки (`idnum % 74611`) и
//        возвращает **2** — а код 2 на событии 5 это и есть контракт «начать перетаскивание»:
//        по нему общий `CControl_OnLButtonDown@0x12DB870` создаёт окно-призрак
//        (`uiCreateDragImageWindow@0x12BB910`, единственный вызов на весь клиент —
//        `Reports/icon_drag_threshold.md` §1.2). `uiHotbarAssignSlot@0xECDFB0` пишет
//        `LOBYTE(Src[0]) = 5; Src[1] = поле+8` → 10-байтовая запись слота { тип 5, индекс строки }.
//     2) ЖИВАЯ БАЗА. `Joy1.dbo.CharSlot` (раскладка панели) содержит РЕАЛЬНЫЕ строки типа 5:
//        `Slotidx 65…68, Type=5, Posx = 0/1/6/7` у двух персонажей — то есть игроки оригинала
//        клали пункты доп-меню в T-ряд, и сервер это хранит. См. отчёт `extra_menu_hotbar_drag.md`.
//   ⇒ СЕРВЕРНОГО ПАТЧА НЕ ТРЕБУЕТСЯ: формат ячейки уже содержит тип 5, сервер
//   (`Recv_DBQuickSlotInfo_Save@0x4DAD60`) содержимое не валидирует вовсе — только `slot < 78`.
//
//   ГРАНИЦЫ БРОСКА (эталон `uiHotbarSlotDispatch`): принимаются РОВНО два диапазона —
//   73011…73022 (основной ряд, колонки 0…11) и 73161…73172 (T-ряд, колонки 0…11).
//   ⚠ КОЛОНКА 12 (слот оружия, 73023) пункт доп-меню НЕ ПРИНИМАЕТ — верхняя граница на одну
//   колонку короче, чем у предмета. Ровно так же ведут себя эмоция и навык.
//   Промах мимо панели — эталон НЕ ДЕЛАЕТ НИЧЕГО (`if (a2)` в `uiHotbarSlotDispatch`).
//
//   ★★★ ЧТО ИМЕННО ЛЕЖИТ В СЛОТЕ (изменено 2026-08-30 по решению владельца «переход на устойчивый
//   идентификатор»). Эталон клал ПОРЯДКОВЫЙ НОМЕР строки, и он съезжает при любом изменении
//   состава меню. Порт кладёт КОД ДЕЙСТВИЯ пункта (`rec+68`) и помечает это `SubType = 1`;
//   `SubType = 0` остаётся эталонным смыслом и читается по ИСХОДНОЙ нумерации (записи с
//   `Restored == false`). Разбор обоих видов — `FindByStableKey`, ключ отката —
//   `asda.ExtraMenu.LegacyRowKey 1`. Подробности и SQL-миграция:
//
//   ПОРОГ СДВИГА. У эталона порога нет (призрак рождается тем же кадром), но по прямому
//   требованию владельца в порту он общий для всех окон — `FAsdaDragArm::kDragStartPixels = 4`
//   (`AsdaUiHelpers.h`, ключ отката: 0 = поведение эталона 1:1). Своего порога здесь НЕТ.
//
//   ★ ОБЫЧНЫЙ КЛИК НЕ ЛОМАЕТСЯ. Отпускание над той же строкой по-прежнему открывает систему:
//   клик маршрутизирует кнопка-оверлей сетки (`UButton::OnClicked` = нажатие И отпускание над
//   ОДНОЙ зоной), а бросок в панель отпускается над ЧУЖИМ окном, где `OnClicked` не наступает
//   вовсе. Двух путей активации не заводили — оба ведут в один `ActivateItem`.
//
// ═══════════════════════════ СООБЩЕНИЯ ПРИ ПЕРЕТАСКИВАНИИ (закон audit-notices) ════════════════
//   Таблица «событие → строка локализации → канал» составлена ДО кода и оказалась ПУСТОЙ:
//     бросок мимо панели         → строки нет → эталон молчит (`if (a2)` не выполнено)
//     бросок в колонку оружия    → строки нет → эталон молчит (idnum вне диапазона)
//     «не сохранилось»           → строки нет → сервер на 5067 ОТВЕТА НЕ ШЛЁТ ВООБЩЕ
//                                  (`Recv_DBQuickSlotInfo_Save` ничего не возвращает клиенту)
//   ⇒ новых строк не заводим и не выдумываем (`never-invent-only-original`); всё, что видит
//   разработчик, идёт в журнал `LogAsdaEtc`.
//   ★★★ ГОРЯЧЕЙ КЛАВИШИ У ЭТОГО ОКНА В ОРИГИНАЛЕ НЕТ. Проверено двумя источниками:
//     • `system/Shortcutkey.ini` живого клиента — 85 строк, ни `ETC`, ни `EXTRA` в них нет;
//     • эталонная таблица подписей действий `GActionLabels[86]` (`AsdaKeybindConfig.cpp:49`) —
//       того же состава. Поэтому СВОЕЙ привязки не заводим (прямое требование задания).
//     Клавиши есть у самих ЦЕЛЕЙ пунктов (T звания, V разборка, Z ремесло, G улучшение,
//     P питомец, Y лоток) — они уже лежат в `Config/DefaultInput.ini` строки 54…81 и ждут
//     своих систем; доп-меню к ним отношения не имеет.
//
// ═══════════════════════════ ПОДСКАЗКИ (нотисы) ════════════════════════════════════════════════
//   Эталон `uiListControlOnUiEvent@0xECDC70`: событие 8 (наведение) → `tutorialShowItemNoticePopup
//   @0xECD760` = ОБЩАЯ ПЛАШКА `g_ItemTooltip` (`tutorialPopupLayoutSmall`), заголовок = подпись
//   записи, тело = строка `DescLocId`; событие 9 — вести за курсором; событие 0xA — погасить.
//   В порту это `UAsdaMiniStateSubsystem::ShowSharedTip(..., EAsdaTipOwner::ExtraMenu)` —
//   ОТДЕЛЬНЫЙ владелец обязателен (закон `ui-shared-tip-ownership`): меню живёт рядом с
//   инвентарём и хотбаром, которые каждым тиком гасят плашку СВОЕГО владельца.
//   Таблица «событие → id строки → канал» целиком — в `Reports/extra_menu.md` §5.
//
// ═══════════════════════════ ОКНО ВВОДА КОЛИЧЕСТВА (1410) ══════════════════════════════════════
//   НЕ УЧАСТВУЕТ. Сверено с `Docs/Port/systems/NUMPAD_FLOW.md` §3 (24 места вызова в срезах эталона):
//   ни одно не принадлежит `CExtraMenuWnd`; количеств окно не спрашивает вовсе.
//
// ═══════════════════════════ ИСПРАВЛЕННЫЕ ДЕФЕКТЫ ОРИГИНАЛА (закон fix-original-defects) ═══════
//   Д1 `quickSlotUI_RefreshIconSlots@0xECD870` крутит РОВНО 16 строк и при `FindControlByID`
//      == null делает `return 0`, после чего `CExtraMenu_UIInit` показывает МОДАЛЬНЫЙ
//      `MessageBoxA("ExtraMenu UI Init Error")`. Таблица — ДАННЫЕ: положив в неё 17-ю запись,
//      игрок/редактор роняет инициализацию всего доп-меню. В порту лишние записи отбрасываются
//      с явной строкой в журнал, окно продолжает работать.      `asda.ExtraMenu.LegacyOverflow 1`
//   Д2 `loadExtraMenuTable` при отсутствии файла лишь пишет в лог движка и оставляет `count = 0`;
//      окно открывается ПУСТЫМ без единого слова игроку. В порту пустая таблица — строка ошибки
//      в журнал и нотис 990101.                                  `asda.ExtraMenu.LegacySilentEmpty 1`
//   Д3 Проверка диапазона подсказки в `uiListControlOnUiEvent` (событие 8) написана как
//      `a3 >= 74611 && a3 <= nRecordCount + 74626` — верхняя граница считается от ПОСЛЕДНЕГО
//      контрола (74626) вместо ПЕРВОГО (74611), то есть шире истинной на 15. Спасает лишь то,
//      что `array116_LookupByIndexOrField` вернёт null. В порту граница — по числу записей.
//                                                                `asda.ExtraMenu.LegacyTipRange 1`
//   ⚠ НЕ ДЕФЕКТ КОДА, а данных: у записи «Добыча» подсказка 110936 в RU-таблице равна ОДНОМУ
//     ПРОБЕЛУ — тело подсказки пустое. Так в оригинале; не выдумываем (`never-invent-only-original`).
#pragma once

#include "CoreMinimal.h"
#include "AsdaTickGate.h"   // ★ R4-b (14.11.2026): шаблон класса (CDO) не тикает
#include "Subsystems/GameInstanceSubsystem.h"
#include "Tickable.h"
#include "Data/AsdaExtraMenuRow.h"
#include "UI/AsdaUiInputTypes.h"   // ★ R1: FAsdaMouseFrame / FAsdaDragArm / FAsdaDragGhost … — поля ввода (без всего AsdaUiHelpers.h)
#include "AsdaSessionReset.h"   // IAsdaSessionResettable — сброс состояния сессии
#include "AsdaExtraMenuSubsystem.generated.h"

class UUserWidget;
class UWidget;
class UImage;
class UTextBlock;
class UCanvasPanel;
class UAsdaIconCellGrid;
class UAsdaButtonRelay;

UCLASS()
class ASDAREBORN_API UAsdaExtraMenuSubsystem : public UGameInstanceSubsystem, public FTickableGameObject,
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

	// FTickableGameObject — ДВЕ работы:
	//   1) ЛЕНИВАЯ подписка на кнопку трея (меню и контроллер появляются позже подсистемы);
	//   2) ПОКАДРОВЫЙ ОПРОС КУРСОРА, пока окно открыто, — «нажали ≠ тащим» для перетаскивания
	//      строки в панель быстрого доступа. Наведение и обычный клик по-прежнему приходят
	//      событиями кнопок-оверлеев сетки; тик их не дублирует.
	virtual void Tick(float DeltaTime) override;
	virtual ETickableTickType GetTickableTickType() const override { return ETickableTickType::Conditional; }
	// ⚠ `bEtcDragActive` в условии обязателен: если окно спрячут ПОСРЕДИ захвата, тик обязан дожить
	//   до отпускания кнопки — иначе «призрак» останется висеть, а взвод не снимется.
	// ★ R4-b (14.11.2026): шаблон класса (CDO) не тикает — раньше `Tick` звался дважды за кадр, у рабочего объекта и у шаблона
	//   (ключ `asda.Perf.NoCdoTick`, разбор — `AsdaTickGate.h`). Рабочий объект тикает как раньше.
	virtual bool IsTickable() const override { return AsdaTickGate::NotCdo(this) && (!bMenuBound || bEtcDragActive || IsWindowVisible()); }
	virtual TStatId GetStatId() const override { RETURN_QUICK_DECLARE_CYCLE_STAT(UAsdaExtraMenuSubsystem, STATGROUP_Tickables); }

	// ══ ОКНО ══
	// Открыть/закрыть/переключить окно «Прочее». Тогл — эталонный `uiToggleSkillTreePanel@0xECDB70`.
	UFUNCTION(BlueprintCallable, Category="Asda|ДопМеню")
	void OpenWindow();

	UFUNCTION(BlueprintCallable, Category="Asda|ДопМеню")
	void CloseWindow();

	UFUNCTION(BlueprintCallable, Category="Asda|ДопМеню")
	void ToggleWindow();

	UFUNCTION(BlueprintPure, Category="Asda|ДопМеню")
	bool IsWindowVisible() const;

	// ══ ДАННЫЕ ══
	// Число ЖИВЫХ записей таблицы (после отбрасывания лишних сверх 16 — дефект Д1).
	UFUNCTION(BlueprintPure, Category="Asda|ДопМеню")
	int32 GetRecordCount() const { return Records.Num(); }

	// Запись по индексу; nullptr — индекса нет (эталон `array116_LookupByIndexOrField`, режим 1).
	const FAsdaExtraMenuRow* FindByIndex(int32 Index) const;
	// Запись по КОДУ КОМАНДЫ (эталон, режим `a3 != 1`: ищет по полю +68, побеждает последняя).
	const FAsdaExtraMenuRow* FindByCommand(int32 Cmd) const;

	// ═══ КЛЮЧ ПУНКТА В СЛОТЕ ХОТБАРА (тип 5) ═══
	// Вид ключа лежит в поле `SubType` слота (+1; в БД — `CharSlot.Plate`).
	enum : int32
	{
		// 0 — ЭТАЛОННЫЙ смысл: ключ = ПОРЯДКОВЫЙ НОМЕР строки меню.
		//   `uiHotbarAssignSlot@0xECDFB0`: `memset(Src,0,0xA); LOBYTE(Src[0])=5; Src[1]=номер;`
		//   `SubType` остаётся нулём от `memset` — поэтому 0 и означает «старая запись».
		kExtraKeyRowOrdinal = 0,
		// 1 — НАШ УСТОЙЧИВЫЙ ключ: ключ = КОД ДЕЙСТВИЯ пункта (`rec+68`).
		kExtraKeyCmd        = 1,
	};

	// ★★★ ЕДИНАЯ ТОЧКА РАЗБОРА КЛЮЧА. Ею пользуются ВСЕ три ветки типа 5 (иконка, применение,
	//   подсказка) — второй копии трактовки нет. nullptr = пункта нет (слот просто ничего не делает,
	//   ровно как эталон при `array116_LookupByIndexOrField == 0`).
	const FAsdaExtraMenuRow* FindByStableKey(int32 KeyKind, int32 Key) const;

	// Порядковый номер записи в ИСХОДНОЙ таблице (счёт только по `Restored == false`).
	//   −1 — запись восстановлена нами, в исходной таблице её не было.
	int32 OriginalOrdinalOf(int32 Index) const;

	// ══ ДЕЙСТВИЕ ══
	// Выполнить пункт по ИНДЕКСУ записи (то же, что клик по строке; зовёт и слот хотбара типа 5).
	UFUNCTION(BlueprintCallable, Category="Asda|ДопМеню")
	void ActivateItem(int32 Index);

	// Порт `uiOpenWindowDispatch@0xECD570` — разбор КОДА КОМАНДЫ.
	//   `SourceName` — подпись пункта для честного отказа (нотис 990100, слот `[ss1]`).
	void DispatchCommand(int32 Cmd, const FString& SourceName);

	// ══ ПЕРЕТАСКИВАНИЕ СТРОКИ В ПАНЕЛЬ БЫСТРОГО ДОСТУПА (слот типа 5) ══
	// ★★★ ЕДИНАЯ ТОЧКА УКЛАДКИ — порт `uiHotbarAssignSlot@0xECDFB0`:
	//     `memset(Src,0,10); LOBYTE(Src[0]) = 5; Src[1] = nSelectedSlotIndex;`
	//   то есть запись слота = { Type = 5, SubType = 0, Index = НОМЕР СТРОКИ, Count = 0, Id = 0 }.
	//   Сама запись, пересборка иконки и отправка страницы пакетом 5067 — в общей точке
	//   `UAsdaHotbarSubsystem::SetSlotContent` (эталонный `Hotbar_DragDrop_SetSlot@0xDE9DF0`);
	//   второй копии сетевого кода здесь нет.
	//   `AbsSlot < 0` ⇒ слот берётся ХИТ-ТЕСТОМ панели под курсором (`HitTestSlotUnderCursor`) —
	//   ровно та же общая точка, которой пользуются сумка, книга навыков и лоток эмоций.
	//   Явный `AbsSlot` нужен консольной проверке: мышью в headless-прогоне не бросить.
	bool DropRowToHotbar(int32 Row, int32 AbsSlot = -1);

	// ★ ДИАГНОСТИКА УСТОЙЧИВОСТИ КЛЮЧА: вставить в НАЧАЛО списка N синтетических строк, сдвинув
	//   нумерацию всех настоящих. Только рантайм, таблицу на диске не трогает; `0` — вернуть как
	//   было (перечитать `DT_ExtraMenu`). Нужна, чтобы ПОКАЗАТЬ, что слот после изменения состава
	//   меню открывает СВОЙ пункт, а не соседний.
	void DebugShiftTable(int32 Count);

	// ══ ДИАГНОСТИКА (живая, а не по своим переменным — закон из брифинга) ══
	// Состояние окна и всех 16 строк: что за запись, видима ли строка, где живая клик-зона.
	FString DumpState() const;
	// ★ Живой прямоугольник ИКОНКИ строки на экране (false — строки нет либо она не отрисована).
	//   Нужен проверке путём игрока: точка, с которой начинается перетаскивание (`asda.UI.SimDrag`).
	bool GetRowIconAbsRect(int32 Row, FVector2D& OutMin, FVector2D& OutMax) const;

	// ★★★ ПРОВЕРКА ПУТЁМ ИГРОКА: заказать НАСТОЯЩЕЕ перетаскивание строки в слот панели.
	//   Считает живые прямоугольники строки и слота и отдаёт их `UAsdaWindowSubsystem::SimulateDrag`
	//   (события Slate + покадровое ведение курсора). Никакой обработчик напрямую не зовётся —
	//   путь ровно тот же, что у мыши. false — строка или слот сейчас не отрисованы.
	bool SimDragRowToSlot(int32 Row, int32 AbsSlot, int32 HoldFrames);

	// Прогнать клик по строке ТЕМ ЖЕ путём, что и кнопка-оверлей сетки.
	UFUNCTION(BlueprintCallable, Category="Asda|ДопМеню")
	void SimulateClick(int32 Row);

	// ★ ДИАГНОСТИКА: сдвинуть слот строки. Нужна, чтобы отличить «узел не рисуется из-за места»
	//   от «узел не рисуется вообще» (закон `false-negatives-method`).
	void DebugMoveRow(int32 Row, float X, float Y);

private:
	// ── данные ──
	UPROPERTY(Transient) TArray<FAsdaExtraMenuRow> Records;

	// ── окно и его узлы ──
	UPROPERTY(Transient) TObjectPtr<UUserWidget> Win = nullptr;
	// 16 строк-канвасов в порядке `.JUI` (дети `ETC_NEW_Scroll_grp`, только `UCanvasPanel`).
	UPROPERTY(Transient) TArray<TObjectPtr<UWidget>> RowGroups;
	UPROPERTY(Transient) TArray<TObjectPtr<UTextBlock>> RowLabels;
	UPROPERTY(Transient) TArray<TObjectPtr<UImage>> RowIcons;
	// Клик-зоны строк (общая точка `port-shared-points`, а не своя копия оверлея).
	UPROPERTY(Transient) TObjectPtr<UAsdaIconCellGrid> RowGrid = nullptr;
	// GC-якоря обработчиков крестика.
	UPROPERTY(Transient) TArray<TObjectPtr<UAsdaButtonRelay>> Relays;

	bool bReady = false;
	bool bMenuBound = false;
	bool bTipShown = false;
	int32 TipRow = -1;

	// ── ПЕРЕТАСКИВАНИЕ СТРОКИ (эталон: событие 5 «взять» → событие 7 «бросить») ──
	//   Прозрачность «призрака» и приглушённой строки-источника — те же 40 %, что у лотка эмоций,
	//   книги навыков и сумки (значение согласовано с владельцем, менять поштучно нельзя).
	static constexpr float kRowDragGhostOpacity  = 0.4f;
	static constexpr float kRowDragSourceOpacity = 0.4f;
	FAsdaDragArm RowDragArm;                            // общий порог «нажали ≠ тащим»
	UPROPERTY(Transient) FAsdaDragGhost RowDragGhost;   // летящая за курсором иконка
	bool bEtcDragActive = false;

	// ── внутреннее ──
	void LoadTable();
	bool EnsureWindow();
	void BuildRows();
	void RefreshRows();
	void ShowRowTip(int32 Row);
	void HideRowTip();
	// Покадровый опрос курсора: взвод/начало/завершение захвата строки.
	void TickPointer();
	// Захват строки (эталон msg 5: гасит подсказку и отвечает кодом 2 → движок поднимает призрак).
	void BeginRowDrag(int32 Row);
	// Снять ТОЛЬКО визуал захвата (призрак + приглушение строки-источника).
	void CancelRowDragVisual(int32 Row);
	// Отпускание: без сдвига — обычный клик (его отдаёт кнопка-оверлей сетки), со сдвигом — бросок.
	void EndRowDrag(bool bWasDragging, int32 Row);
	// Отказ «система не портирована»: строка в журнал + нотис 990100 с подписью пункта.
	void RefuseNotPorted(int32 Cmd, const FString& SourceName, const TCHAR* What);

	UFUNCTION() void OnMenuCommand(int32 ButtonIdNum, int32 TargetWindowId, bool bOpen);
};
