// Copyright (c) 2026 @sitxovski (Discord)
// UAsdaPartyFindSubsystem — ОКНО «ПОИСК ОТРЯДА» (`Find_Party_WIn`, JUI IDNum **8500**,
//   `gamemain_jui.ini:244828`, 783×478, по центру экрана). Рантайм-класс эталона `CFindPartyWnd`,
//   глобальный статический объект `0x02C2D388` (создаётся CRT-инициализатором `0x01388A9E`).
//   Полный реверс — `Reports/extra_menu_partyfind.md`, `Reports/partyfind_3421_layout.md`
//   и `Reports/party_find_8500.md` (этот заход).
//
// ═══════════════════════ ЧТО ЭТО ЗА ФИЧА ══════════════════════════════════════════════════════
//   Две таблицы по 10 строк, объединённые общими фильтрами:
//     ЛЕВАЯ  «Список желающих вступить в отряд» (11821) — ОДИНОЧКИ: имя / класс / уровень / канал;
//     ПРАВАЯ «Игроки, ищущие отряд» (11825)            — ОТРЯДЫ:   лидер / цель / N-из-M / канал.
//   Свои данные игрок публикует сам: НЕ в отряде — встаёт в список одиночек (3410); ЛИДЕР —
//   публикует отряд (3418). Кнопка «Шепот» переводит чат в приват выбранному, «Обновить»
//   перезапрашивает обе страницы, «Изменить» правит цель и размер отряда (3424).
//
// ═══════════════════════ ★★★ ПОЧЕМУ ЭТОГО НЕ БЫЛО В ИГРЕ ══════════════════════════════════════
//   Подсистема в эталоне НАПИСАНА ЦЕЛИКОМ (~35 функций, 0xDB6120…0xDBF3E0), но у неё вырезаны
//   ВСЕ ЧЕТЫРЕ привязки — и это доказано двумя независимыми методами каждая:
//     1. Запись пункта доп-меню в `Extra_menu_table.BIN` (в RU-сборке 11 записей вместо 14);
//     2. `case 7` в `uiOpenWindowDispatch@0xECD570` — байтовая таблица переходов `0xECD6B0`
//        ведёт код 7 прямо в метку `default` `0xECD6A7`;
//     3. строка обработчика окна в маршрутизаторе `ui_DispatchCommandEvent@0xC6C650`
//        (78 оконных обработчиков, `sub_DB8AA0` среди них НЕТ);
//     4. ветка `WM_TIMER` id 26 в `MainWndProc@0x9841A0` (обработаны 5…9, 11…24, 99 — дыра на 25/26).
//   У переходников `sub_DB9B80` (тоггл) и `sub_DB8AA0` (события) — НОЛЬ вызывателей и ноль
//   DWORD-ссылок по всему образу (`RE/ghidra_legacy/tools_tmp/pf_callers.py`, контрольный прогон на живом
//   `uiToggleSkillTreePanel@0xECDB70` даёт 4). Сервер при этом ЖИВОЙ и обслуживает все восемь
//   опкодов: `sub_42A9D0@0x42A9D0` → `CPartyManager`, подбор — `CPartyMatching` в
//   `ChannelManager.exe`. Диф нашего декомпила GameServer с боевым образом — 218 байт, ни один
//   не в этой области.
//
// ═══════════════════════ ВХОД В ОКНО ══════════════════════════════════════════════════════════
//   ЕДИНСТВЕННЫЙ — пункт доп-меню «Прочее» с кодом команды **7** (`UAsdaExtraMenuSubsystem`,
//   ветка `case kCmdPartyFind`). Запись пункта восстановлена в `DT_ExtraMenu`:
//   `Idx=2, Cmd=7, иконка (2,0) атласа `Extra_menu`, подсказка 110927, подпись — текст 11800.
//   ★ Горячей клавиши у окна в оригинале НЕТ (`system/Shortcutkey.ini`, 85 строк — ни одной
//     строки поиска отряда), своей не заводим.
//   ★ Кнопка чата `CHAT_Party_BTN` (IDNum 256, `Visible="0"`) в оригинале ОБЕСТОЧЕНА: подсказка
//     12195/12196 жива (`uiHelp_ShowTooltipForControl@0xBB3D80`, тема 28), а обработчика нажатия
//     во всём `uiChatWindowMsgProc@0xBAAD50` нет — контрол 256 встречается там ровно один раз,
//     в ветке подсказок. Её включение — отдельное исправление дефекта, здесь НЕ делается
//     (окно чата — чужой файл `AsdaChatWindowSubsystem`).
//
// ═══════════════════════ ТОГГЛ (эталон `sub_DB9B80@0xDB9B80`, снят дизассемблером) ═════════════
//   if (окно уже видно)  → «свернуть»: `uiWindowCloseStartTimer` + нотис 11906 в ЧАТ, выход.
//   иначе: поднять в стек окон, показать, и если `bClosedFully` (эталон `this+0x1640 == 1`):
//     • кнопка «Изменить» (8651) видна ТОЛЬКО лидеру (`Party_IsMeBoss`);
//     • печатаются четыре счётчика страниц (8571/8572/8645/8646);
//     • сбрасывается выделение обоих списков;
//     • charId ← `g_CharManager+0x41C`, userNum ← `g_CharManager+0x2E6EC`;
//     • `Party_IsInParty() >= 2`            → мой номер в списке одиночек := 0xFFFF;
//     • в отряде и НЕ лидер                 → 3414 + 3422 (только смотрю);
//     • НЕ в отряде                         → 3410 (встать в список) + 3422;
//     • лидер                               → 3414 + 3418 (опубликовать отряд);
//   иначе (разворачиваем из «свёрнуто»)     → 3414 + 3422;
//   и в конце — `KillTimer(hwnd, 26)`.
//   ⚠ Таймер 800 мс (`SetTimer(hwnd, 26, 800)`) заводится ПРИ СВОРАЧИВАНИИ, а не при открытии:
//     пока окно свёрнуто, он держит регистрацию живой. При открытии таймер снимается.
//
// ═══════════════════════ КНОПКИ (эталон `uiPartyRecruitOnCommand@0xDB9520`) ════════════════════
//     8502 «По всему региону»  → снять фильтр карт, 3416 + 3424
//     8509 «Поиск» (левый)     → страница 1, 3414
//     8573/8574/8575/8576      → листание левого списка (вперёд/конец/назад/начало), 3414
//     8583 «Поиск» (правый)    → страница 1, 3422
//     8647/8648/8649/8650      → листание правого списка, 3422
//     8651 «Изменить»          → показать панель правки (ТОЛЬКО лидеру)
//     8652 / 8698 «Свернуть»   → свернуть + нотис 11906
//     8653 «Отмена»            → закрыть: 3412 + 3420, снять таймер, нотис 11907
//     8654 «Обновить»          → обе страницы = 1, 3414 + 3422
//     8655 «Шепот»             → чат в приват выбранному (левый список приоритетнее правого)
//     8662 «Применить» правки  → 3424, спрятать панель, нотис 11908
//     8663 «Отмена» правки     → спрятать панель
//
// ═══════════════════════ ФИЛЬТРЫ ══════════════════════════════════════════════════════════════
//   Комбобоксы (состав снят из `FindPartyWindow_InitControls@0xDB68C0`, циклы заполнения):
//     8506 `MJobSel`  профессия — 5 пунктов, тексты 11835…11839 (Все/Воин/Лучник/Маг/Новичок);
//                     в пакет: idx 0 → −1 «Все», idx 4 → 0 «Новичок», иначе idx (1/2/3);
//     8508 `MLvSel`   уровень   — 11 пунктов, 11840…11850; в пакет — ПАРА границ
//                     `10*(idx−1)+1` и `10*idx`, при idx 0 — 1 и 100;
//     8580 `PChSel`   канал     — 13 пунктов, 11860…11872 (Все + Канал 1…12); в пакет idx−1;
//     8582 `PDesSel`  цель      — 12 пунктов, 11801…11812; в пакет `remapIndexForward(idx)`;
//     8659 `EditNum`  число членов — 4 пункта, 11900…11903 («3 игрока»…«6 игроков»),
//                     по умолчанию выбран пункт 3 («6 игроков»); в пакет idx+3;
//     8661 `EditDes`  цель отряда — 12 пунктов, 11876…11887.
//   ФИЛЬТР КАРТ — 64-битная маска (`flags64_SetBit@0xDBB080`, бит = «проволочный» код карты).
//   16 кнопок карт: 8664…8670 (обычные), 8674…8677 (подземелья), 8684…8688 (элитные).
//   UI-индекс → «проволочный» код даёт `remapIndexForward`, обратно — `remapIndexReverse`.
//
// ═══════════════════════ СЕТЬ ═════════════════════════════════════════════════════════════════
//   Восемь отправителей и шесть приёмников живут в `UAsdaNetSubsystem` (общая точка, закон
//   `port-shared-points`), там же побайтовые раскладки. Кратко:
//     C→S 3410/3412/3414/3416 — список ОДИНОЧЕК (встать / уйти / страница / обновить себя);
//     C→S 3418/3420/3422/3424 — список ОТРЯДОВ  (опубликовать / снять / страница / правка);
//     S→C 3411/3413/3417/3419/3421/3425 — короткие ответы (код в `+6`, индекс в `+7`);
//     S→C **3415** (300 б) — страница одиночек, **3423** (350 б) — страница отрядов.
//   ⚠ Ответы 3411/3419 несут ПРИСВОЕННЫЙ НОМЕР записи — его надо запомнить и слать обратно
//     в 3412/3416/3420/3424, иначе сервер ответит кодом 3 «индекс вне диапазона».
//
// ═══════════════════════ НОТИСЫ: «событие → id → канал» (составлена ДО кода) ═══════════════════
//   | Событие                                  | id     | Канал                              |
//   | «Свернуть» (8652/8698) или тоггл поверх   | 11906  | ЧАТ (`Social`; эталон −11514/10000) |
//   | «Отмена» (8653)                           | 11907  | ЧАТ                                 |
//   | «Применить» правки отряда (8662)          | 11908  | ЧАТ                                 |
//   | Выбор карты-фильтра (8664…8688)           | «<имя карты> <11905>» = 11801+idx и 11905 | ЧАТ |
//   | Наведение на 8652/8698 · 8653 · 8654 · 8662 | темы 32 · 33 · 34 · 35 (`UI_BuildHelpTooltipText`) | ОБЩАЯ ПЛАШКА |
//   ★ Ошибок сервера окно НЕ показывает: приёмники эталона (`sub_DBF410`, `sub_DBF960` и др.)
//     проверяют только код 1, прочие коды игнорируют молча. Мы их ПИШЕМ В ЖУРНАЛ (закон
//     `no-simplifications-stubs`), но нотиса не выдумываем (закон `never-invent-only-original`).
//   ★ Тема подсказки берётся из `UI_BuildHelpTooltipText`, тексты 11909…11916 — как у соседей.
//
// ═══════════════════════ ОКНО ВВОДА КОЛИЧЕСТВА (1410) — НЕ УЧАСТВУЕТ ══════════════════════════
//   Сверено с `Docs/Port/systems/NUMPAD_FLOW.md`: ни одно из 24 мест вызова нумпада не принадлежит
//   `CFindPartyWnd`. Количество членов отряда вводится СВОИМ комбобоксом `Find_Party_EditNum`
//   (8659, тексты 11900…11903), а не общим нумпадом. Числовых полей ввода у окна нет вовсе.
//
// ═══════════════════════ ИСПРАВЛЕННЫЕ ДЕФЕКТЫ ОРИГИНАЛА (закон fix-original-defects) ══════════
//   Д1 `party_sendBossInfoSecure@0xDBEC10` (3424) пишет байт тела **+6 ДВАЖДЫ**: сначала
//      профессию (`g_LocalPlayer+0x445`), затем класс (`+0x438`). Второе присваивание затирает
//      первое, а байт +7 (место класса) уходит нулём. У 3410/3416/3418 те же поля верны.
//      Исправлено в `UAsdaNetSubsystem`.            `asda.PartyFind.LegacyBossJobByte 1`
//   Д2 `sub_DB8AA0`, событие `0x10601`: верхняя граница диапазона карт пишется дважды подряд
//      (`0xDB8E00: mov [ebp-0x10], 0x21F0` = 8688, затем `0xDB8E07: … 0x21EF` = 8687), между ними
//      НЕТ ни одной инструкции. Клик по последней карте `Find_Party_MapE04` **8688** («Логово
//      Дракона») проходит таблицу переходов, но отбрасывается проверкой диапазона и не работает.
//      В том же обработчике событие `0xB` режет диапазон ещё раньше — по 8685.
//                                                    `asda.PartyFind.LegacyMapRange 1`
//   Д3 ★ ДЕФЕКТ СЕРВЕРА: `Recv_Client_GetLeader` в ветке «charId тела ≠ charId сессии» шлёт
//      350-байтную страницу под опкодом **3421** вместо 3423 (`GameServer.c:33362`). Сервер НЕ
//      трогаем (закон «серверные патчи — отдельная DLL»); клиент разводит по длине тела.
//   Д4 `ChannelManager::GetLeader` кладёт в страницу ЗАФИКСИРОВАННОЕ при 3418/3424 число членов
//      (`CMrec+0x2C`), хотя тут же вычисляет живое — колонка «N/M» может врать. Серверное,
//      не лечится на клиенте; занесено в `PORT_PENDING_ON_SYSTEMS.md`.
//
// ═══════════════════════ ГРАБЛИ, УЧТЁННЫЕ ЗАРАНЕЕ ═════════════════════════════════════════════
//   • Строки списков — НЕ кнопки (`Box_WhiteOt20`), клик ловим ПО ЖИВОЙ ГЕОМЕТРИИ
//     (`AsdaUi::IsCursorOverWidget`), а не по слоту: в `.JUI` колонки строки вложены ЦЕПОЧКОЙ
//     (`MChar` → `MJob` → `MLv` → `MCh`), и координаты слотов считаются от разных начал
//     (закон `jui-cell-chain-nesting`).
//   • Импортёр РАСПЛЮЩИЛ иерархию — узлы ищем `NodeLoose` (точное имя, затем префикс) и при
//     промахе пишем искомое имя в журнал (закон `jui-flattened-hierarchy-geometry`).
//   • Списки перерисовываем БЕЗУСЛОВНО каждой перерисовкой, а не «только при изменении».
//   • Защёлка «запрос в полёте» снимается по таймауту, отказ пишет причину в журнал.
//   • Подсказку гасит ТОЛЬКО владелец (`EAsdaTipOwner::PartyFind`, закон `ui-shared-tip-ownership`).
#pragma once

#include "CoreMinimal.h"
#include "Subsystems/GameInstanceSubsystem.h"
#include "Tickable.h"
#include "UI/AsdaUiInputTypes.h"   // ★ R1: FAsdaMouseFrame / FAsdaDragArm / FAsdaDragGhost … — поля ввода (без всего AsdaUiHelpers.h)
#include "Layout/Margin.h"         // FMargin
#include "AsdaSessionReset.h"   // IAsdaSessionResettable — сброс состояния сессии
#include "AsdaPartyFindSubsystem.generated.h"

class UUserWidget;
class UWidget;
class UImage;
class UTextBlock;
class UButton;
class UAsdaButtonRelay;
class UAsdaJuiComboBox;

// ── Строка ЛЕВОГО списка (одиночки). Раскладка записи пакета 3415 — 29 б (§3 отчёта). ──
USTRUCT()
struct FAsdaPartyFindSingle
{
	GENERATED_BODY()

	UPROPERTY(Transient) int32 UserNum = 0;    // +0x00 u16
	UPROPERTY(Transient) int32 CharId  = -1;   // +0x02 i32; −1 = конец списка
	UPROPERTY(Transient) int32 ClassId = 0;    // +0x06 u8 → текст `12300 + ClassId` (колонка «Профессия»)
	UPROPERTY(Transient) int32 Level   = 0;    // +0x07 u8 → «Ур. N» (текст 12215)
	UPROPERTY(Transient) int32 Channel = -1;   // +0x08 i8, 0-based; при выводе +1
	UPROPERTY(Transient) FString Name;         // +0x09 char[20], NUL-terminated
};

// ── Строка ПРАВОГО списка (отряды). Раскладка записи пакета 3423 — 34 б (§2.2 отчёта). ──
USTRUCT()
struct FAsdaPartyFindParty
{
	GENERATED_BODY()

	UPROPERTY(Transient) int32 UserNum   = 0;   // +0x00 u16 — userNum ЛИДЕРА
	UPROPERTY(Transient) int32 CharId    = -1;  // +0x02 i32; −1 = конец списка
	UPROPERTY(Transient) int32 RawByte6  = 0;   // +0x06 u8 — эталон СОХРАНЯЕТ и НИГДЕ не выводит
	UPROPERTY(Transient) int32 RawByte7  = 0;   // +0x07 u8 — то же (см. §8.1 отчёта: смысл не доказан)
	UPROPERTY(Transient) int32 Channel   = -1;  // +0x08 i8, 0-based
	UPROPERTY(Transient) FString LeaderName;    // +0x09 char[20]
	UPROPERTY(Transient) int32 MaxMembers = 0;  // +0x1D u8 — требуемое число членов
	UPROPERTY(Transient) int32 CurMembers = 0;  // +0x1E u8 — число членов на момент 3418/3424
	UPROPERTY(Transient) int32 PartyIndex = -1; // +0x1F u16 — эталон это поле НЕ читает
	UPROPERTY(Transient) int32 GoalWire   = -1; // +0x21 i8 — «проволочный» код карты-цели
};

UCLASS()
class ASDAREBORN_API UAsdaPartyFindSubsystem : public UGameInstanceSubsystem, public FTickableGameObject,
	public IAsdaSessionResettable
{
	GENERATED_BODY()

public:
	virtual void Initialize(FSubsystemCollectionBase& Collection) override;
	virtual void Deinitialize() override;

	// ── IAsdaSessionResettable (К-3) ──
	// Номера записей MemberRegId/LeaderRegId выдаёт СЕРВЕР на время сессии,
	// а таймер свёрнутой панели продолжает слать 3414/3422 в мёртвый сокет.
	virtual void ResetSessionState(EAsdaSessionResetReason Reason) override;

	virtual void Tick(float DeltaTime) override;
	virtual ETickableTickType GetTickableTickType() const override { return ETickableTickType::Conditional; }
	// Тик нужен, пока окно открыто (клики по строкам и картам, комбобоксы) ИЛИ пока идёт
	//   таймер 800 мс свёрнутого окна, ИЛИ пока висит защёлка «запрос в полёте».
	virtual bool IsTickable() const override { return !IsTemplate() && (bOpen || bMinimizedTimer || bReqInFlight); }
	virtual bool IsTickableInEditor() const override { return false; }
	virtual TStatId GetStatId() const override { RETURN_QUICK_DECLARE_CYCLE_STAT(UAsdaPartyFindSubsystem, STATGROUP_Tickables); }

	// ══ ОКНО ══ (порт `sub_DB9B80@0xDB9B80` — ТОГГЛ; вход из доп-меню, команда 7)
	UFUNCTION(BlueprintCallable, Category="Asda|ПоискОтряда") void ToggleWindow();
	UFUNCTION(BlueprintCallable, Category="Asda|ПоискОтряда") void OpenWindow();
	/** Порт `uiFindPartyWindowClose@0xDB9ED0`: очистить таблицы, 3412 + 3420, снять таймер 26. */
	UFUNCTION(BlueprintCallable, Category="Asda|ПоискОтряда") void CloseWindow();
	/** Порт `uiWindowCloseStartTimer@0xDB9FF0`: спрятать окно, но ОСТАТЬСЯ в списках и завести
	 *  таймер автообновления 800 мс. Именно это делают «Свернуть» (8652/8698) и системное закрытие. */
	UFUNCTION(BlueprintCallable, Category="Asda|ПоискОтряда") void MinimizeWindow();
	UFUNCTION(BlueprintPure,     Category="Asda|ПоискОтряда") bool IsWindowOpen() const { return bOpen; }

	// ══ ДИАГНОСТИКА (жмёт САМИ кнопки и читает состояние ВИДЖЕТОВ, а не свои переменные) ══
	FString DumpState() const;
	/** Прогнать нажатие кнопки окна ТЕМ ЖЕ путём, что и живая кнопка: по IDNum контрола. */
	UFUNCTION(BlueprintCallable, Category="Asda|ПоискОтряда") void SimulateClick(int32 IdNum);
	/** Выбрать пункт комбобокса ТЕМ ЖЕ путём, что и мышью (для сплошного обхода контролов). */
	UFUNCTION(BlueprintCallable, Category="Asda|ПоискОтряда") void SimulateComboPick(int32 IdNum, int32 Index);
	/** Прогнать выбор строки списка (0 = левый список, 1 = правый) — тот же путь, что у клика. */
	UFUNCTION(BlueprintCallable, Category="Asda|ПоискОтряда") void SimulateRowClick(int32 Side, int32 Row);
	/** ★★★ ЖИВАЯ геометрия зелёной рамки карт и самих кнопок (`GetCachedGeometry`, не слоты).
	 *  Нужна для доказательства, что рамка стоит НА выбранной плитке, а не в углу окна: слоты
	 *  из ассета в этом окне врут — семь кнопок завёрнуты в собственный под-канвас
	 *  (закон `jui-uneven-node-layout`), и их отступ внутри него равен (0,0).
	 *  ⚠ Числа честны только в `-game` с настоящим RHI: под `-NullRHI` Slate не рисует и
	 *  прямоугольники приходят нулевыми (закон `ui-check-game-path-geometry`). */
	FString DumpMapHighlight() const;
	/** ★ ДИАГНОСТИКА: сбросить сессионный признак «подсказка первого открытия показана». */
	void ResetFirstOpenNotice() { bFirstOpenNoticeShown = false; NoticeShownForChar.Reset(); }

private:
	// ── окно и узлы ──
	UPROPERTY(Transient) TObjectPtr<UUserWidget> Win = nullptr;

	/** ★ Имя персонажа, которому подсказка первого открытия уже показана. СЕССИОННОЕ поле:
	 *  ни в базу, ни в конфиг не сохраняется — перезашёл, и подсказка появится снова.
	 *  Привязка именно к ИМЕНИ (а не к процессу) нужна на случай смены персонажа без
	 *  перезапуска клиента: у другого персонажа подсказка обязана показаться заново. */
	UPROPERTY(Transient) FString NoticeShownForChar;
	/** Подсказка уже показана в этой сессии. Отдельный флаг нужен потому, что имя персонажа
	 *  может быть ещё пустым — по «имя не пусто» защёлка не срабатывала бы вовсе. */
	UPROPERTY(Transient) bool bFirstOpenNoticeShown = false;

	// Левый список: 10 строк × (плашка + 4 колонки + подсветка выбора)
	UPROPERTY(Transient) TArray<TObjectPtr<UWidget>>    MRowPlate;   // 8510 + 6k `Find_Party_MList0N`
	UPROPERTY(Transient) TArray<TObjectPtr<UTextBlock>> MRowName;    // 8511 + 6k `MChar0N`
	UPROPERTY(Transient) TArray<TObjectPtr<UTextBlock>> MRowJob;     // 8512 + 6k `MJob0N`
	UPROPERTY(Transient) TArray<TObjectPtr<UTextBlock>> MRowLevel;   // 8513 + 6k `MLv0N`
	UPROPERTY(Transient) TArray<TObjectPtr<UTextBlock>> MRowChannel; // 8514 + 6k `MCh0N`
	UPROPERTY(Transient) TArray<TObjectPtr<UWidget>>    MRowCursor;  // 8515 + 6k `MCur0N` (подсветка наведения)
	// ★ Рамка выбранной строки 8570 в ассете — НЕ один узел, а ВОСЕМЬ планов скина
	//   `Box_Green3Strok` (`Find_Party_MSel_64_0…7`), двигать их надо ВМЕСТЕ.
	UPROPERTY(Transient) TArray<TObjectPtr<UWidget>> MSelPlanes;      // 8570
	UPROPERTY(Transient) TObjectPtr<UTextBlock> MTxtPage = nullptr;  // 8571 текущая страница
	UPROPERTY(Transient) TObjectPtr<UTextBlock> MTxtTotal = nullptr; // 8572 всего страниц

	// Правый список — то же самое
	UPROPERTY(Transient) TArray<TObjectPtr<UWidget>>    PRowPlate;   // 8584 + 6k
	UPROPERTY(Transient) TArray<TObjectPtr<UTextBlock>> PRowLeader;  // 8585 + 6k
	UPROPERTY(Transient) TArray<TObjectPtr<UTextBlock>> PRowGoal;    // 8586 + 6k
	UPROPERTY(Transient) TArray<TObjectPtr<UTextBlock>> PRowNum;     // 8587 + 6k
	UPROPERTY(Transient) TArray<TObjectPtr<UTextBlock>> PRowChannel; // 8588 + 6k
	UPROPERTY(Transient) TArray<TObjectPtr<UWidget>>    PRowCursor;  // 8589 + 6k
	UPROPERTY(Transient) TArray<TObjectPtr<UWidget>> PSelPlanes;      // 8644 — тоже 8 планов
	UPROPERTY(Transient) TObjectPtr<UTextBlock> PTxtPage = nullptr;  // 8645
	UPROPERTY(Transient) TObjectPtr<UTextBlock> PTxtTotal = nullptr; // 8646

	// Кнопки. Ключ карты — IDNum контрола (имена `.JUI` врут, закон `jui-importer-widget-naming`).
	UPROPERTY(Transient) TMap<int32, TObjectPtr<UButton>> Buttons;
	UPROPERTY(Transient) TArray<TObjectPtr<UAsdaButtonRelay>> Relays;   // GC-якоря обработчиков

	// Карты-фильтры: IDNum → узел. ★ Импортёр сделал их `UCheckBox` (скин `MapName_*` —
	//   двухсостоянийный), и это ВЕРНО: эталонная маска `flags64_SetBit@0xDBB080` только СТАВИТ
	//   биты и никогда не снимает, то есть выбор карт — множественный (закон
	//   `jui-checkbutton-vs-radiobutton`).
	UPROPERTY(Transient) TMap<int32, TObjectPtr<UWidget>> MapNodes;
	UPROPERTY(Transient) TArray<TObjectPtr<UWidget>> MapSelPlanes;      // 8694 — 8 планов рамки
	// ★★★ АВТОРСКАЯ раскладка планов всех трёх рамок: замеряется ОДИН РАЗ в `SetupControls`,
	//   до первого сдвига. Без этого повторные переносы накапливают ошибку и рамка уезжает
	//   (закон `jui-flattened-hierarchy-geometry`, случай «крестик пропал после открытия»).
	TMap<TObjectPtr<UWidget>, FMargin> FrameBase;
	// База отсчёта дельты: слот ПЕРВОЙ строки каждого списка и первой кнопки карт.
	FVector2D MSelBase = FVector2D::ZeroVector;
	FVector2D PSelBase = FVector2D::ZeroVector;
	FVector2D MapSelBase = FVector2D::ZeroVector;
	// ★ 25.10.2026 [ИСПРАВЛЕНИЕ] (`asda.UI.BaseShiftKeepsDrag`): та же сумма отступов у РОДИТЕЛЯ планов каждой рамки на момент
	//   базы. Перетаскивание окна сдвигает и строку (входит в `PfWindowOffsetOf` строки), и группу рамки — без вычета сдвига
	//   родителя рамка уезжала на ДВОЙНОЙ вектор перетаскивания (самотест `asda.SelfTest.DragRelayout` «отряд»).
	FVector2D MSelParentBase = FVector2D::ZeroVector;
	FVector2D PSelParentBase = FVector2D::ZeroVector;
	FVector2D MapSelParentBase = FVector2D::ZeroVector;

	// Комбобоксы
	UPROPERTY(Transient) TObjectPtr<UAsdaJuiComboBox> CmbJob   = nullptr;  // 8506
	UPROPERTY(Transient) TObjectPtr<UAsdaJuiComboBox> CmbLevel = nullptr;  // 8508
	UPROPERTY(Transient) TObjectPtr<UAsdaJuiComboBox> CmbChan  = nullptr;  // 8580
	UPROPERTY(Transient) TObjectPtr<UAsdaJuiComboBox> CmbGoal  = nullptr;  // 8582
	UPROPERTY(Transient) TObjectPtr<UAsdaJuiComboBox> CmbEditNum = nullptr;// 8659
	UPROPERTY(Transient) TObjectPtr<UAsdaJuiComboBox> CmbEditDes = nullptr;// 8661

	// Панель правки отряда 8656 (видна только лидеру и только по кнопке 8651)
	UPROPERTY(Transient) TObjectPtr<UWidget> EditBox = nullptr;

	// ── данные списков (зеркала буферов `CFindPartyWnd`, шаг 244 / 248 б) ──
	UPROPERTY(Transient) TArray<FAsdaPartyFindSingle> Singles;
	UPROPERTY(Transient) TArray<FAsdaPartyFindParty>  Parties;

	// ── состояние (имена по эталонным смещениям, чтобы сверять было чем) ──
	bool bReady    = false;   // this+0x164C — контролы привязаны
	bool bOpen     = false;   // окно видно
	bool bClosedFully = true; // this+0x1640: 1 = «полностью закрыто», 0 = «свёрнуто»
	bool bWholeRegion = true; // this+0x1644: фильтр карт снят («По всему региону»)
	bool bTipShown = false;   // this+0x1648
	int32 MemberRegId = -1;   // this+0x1634, −1/0xFFFF = меня нет в списке одиночек
	int32 LeaderRegId = -1;   // this+0x1636, −1/0xFFFF = моего отряда нет в списке
	uint32 MapMaskA = 0;      // this+0x1638
	uint32 MapMaskB = 0;      // this+0x163C
	int32 MSelIdx = -1;       // this+0x2DC — выбранная строка левого списка
	int32 MCurPage = 1;       // this+0x2E0
	int32 MReqPage = 1;       // this+0x2E4
	int32 MTotalPages = 1;    // this+0x2E8
	int32 PSelIdx = -1;       // this+0xC74
	int32 PCurPage = 1;       // this+0xC78
	int32 PReqPage = 1;       // this+0xC7C
	int32 PTotalPages = 1;    // this+0xC80

	// Таймер 800 мс свёрнутого окна (эталон `SetTimer(hwnd, 26, 800)` / `KillTimer(hwnd, 26)`).
	bool  bMinimizedTimer = false;
	float MinimizedAccum = 0.f;

	// Защёлка «запрос в полёте»: снимается ответом или ТАЙМАУТОМ, отказ пишет причину в журнал.
	bool  bReqInFlight = false;
	float ReqAccum = 0.f;
	int32 ReqOpcode = 0;

	FAsdaMouseFrame Mouse;
	int32 HoverRowM = -1;
	int32 HoverRowP = -1;

	// ── внутреннее ──
	bool EnsureWindow();
	void ResetWidget();
	void SetupControls();
	void RegisterZone();
	void BindButton(int32 IdNum, const TCHAR* Idc);
	/** ★★★ НАШЕ СВЕРХ ЭТАЛОНА: модальная подсказка при ПЕРВОМ за сессию открытии окна тем, кто
	 *  реально встал в список (одиночка — 3410, глава — 3418). Рядовому члену отряда НЕ
	 *  показывается: он никуда не регистрируется, сообщение было бы неверным.
	 *  Строки наши, > 990000: 990120 одиночке, 990121 главе (см. `PfLoc`). */
	void ShowFirstOpenNotice(const FString& SelfName, bool bAsLeader);

	void OnCommand(int32 IdNum);                 // порт `uiPartyRecruitOnCommand@0xDB9520`
	// Порт ветки события 0x10601. `bChecked` — новое состояние галки: у эталона снимающей
	//   ветки нет вовсе (дефект Д3), у нас снятие убирает бит из маски.
	void OnMapChecked(int32 IdNum, bool bChecked);
	void OnComboChanged(int32 IdNum);            // порт ветки события 0x11201
	void RefreshLeftList();                      // порт `findPartyRefreshListA@0xDBABA0`
	void RefreshRightList();                     // порт `findPartyRefreshListB@0xDBADB0`
	void ClearTables();                          // порт `tableResetRecords10` + `tableClearRecords10`
	void SelectRow(int32 Side, int32 Row);       // подсветка + запись выбранного индекса
	void ApplyMapHighlight(int32 WireCode);      // порт `uiFindParty_ShowTooltipAt@0xDBB330`
	void ShowEditPanel(bool bShow);
	void SendGetMember();                        // 3414 с текущими фильтрами
	void SendGetLeader();                        // 3422 с текущими фильтрами
	void OnAck(int32 Opcode, const TArray<uint8>& Body);
	void OnPage(int32 Opcode, const TArray<uint8>& Body);
	void NoticeLoc(int32 LocId);
	FString Loc(int32 LocId) const;
	void TickRowHover();
	void FillCombos();
};
