// Copyright (c) 2026 @sitxovski (Discord)
// UAsdaQuestLogSubsystem — ЖУРНАЛ ЗАДАНИЙ, окно `QUEST_WIN` (JUI IDNum 700, 120 контролов,
//   `gamemain_jui.ini:418820…421505`). Класс-владелец в оригинале — `CQuest` (глобалы
//   `g_pQuestMgr` и `g_pNpcWnd` указывают на ОДИН объект размера 0x768).
//   Полный реверс — `Reports/quest_window_re.md`, реализация — `Reports/quest_window_impl.md`.
//
// ═══════════════════════════ ЧТО ЭТО ОКНО ДЕЛАЕТ ════════════════════════════════════════════════
//   · 12 ЯЧЕЕК журнала (720…731) — по одной на активное задание; иконка ячейки берётся из
//     `Episode_Table +252`, яркая у выполненного задания и тусклая у остального;
//   · ЗАГОЛОВОК 703 — название задания из `.QST` (+ « (Готово)», нотис 6163, когда шаг последний
//     и условие завершения `QCT` выполнено);
//   · ОПИСАНИЕ 714 — «нотис(3580 + вид)» + пустая строка + текст текущего шага из `.QST`;
//   · СПИСОК-СВИТОК 734 на правой странице — полное описание задания, с прокруткой;
//   · ЛОТОК ЦЕЛЕЙ 715…719 — до 5 иконок предметов/целей со счётчиком «сколько собрано»;
//   · НАГРАДЫ 1690…1693 / 786…789 — из `RewardTable.BIN` по паре (questNo, stage);
//   · 4 КНОПКИ МАРШРУТА 705…708 — «=> Цель — Карта, x:…, y:…» из `QDT.bin`;
//   · КОРЗИНА 713 — отказ от задания (модалка с нотисом 6174 → C2S 5006 под-тип 4);
//   · СТРЕЛКИ 739/735 — развернуть/свернуть правую страницу 732;
//   · ЧЕКБОКС 743 — «отслеживать задание» (экранный трекер, окно 1800).
//
// ═══════════════════════════ ГДЕ ЖИВЁТ СОСТОЯНИЕ ═══════════════════════════════════════════════
//   ★★★ СВОЕГО хранилища квестов у журнала НЕТ. Все 12 слотов и битовая карта «завершён» лежат
//   в `UAsdaQuestMarkerSubsystem` (наполняются пакетами 5047/5044/5042/5052) — ровно как в
//   оригинале, где и окно 700, и метки над NPC, и трекер читают ОДИН массив `CQuest+0x264`.
//   Журнал подписан на `UAsdaQuestMarkerSubsystem::OnQuestStateChanged` и просто перерисовывается.
//   Условие «задание выполнено» считает ЕДИНСТВЕННЫЙ вычислитель `EvalNsrCond` (через
//   `IsQuestObjectiveDone`) — второй копии в порту нет.
//
// ═══════════════════════════ ОТКРЫТИЕ И ЗАКРЫТИЕ ═══════════════════════════════════════════════
//   Две точки, обе — ТУМБЛЕР (эталон `input.c:4287`: `if (открыт) Close(); else Open();`):
//     · клавиша **Q** — `Shortcutkey.ini` строка `INTERFACE QUEST`, команда **34**;
//       обрабатывает `AAsdaPlayerController::ExecuteCommand`, `case 34`;
//     · кнопка **73404 `NEW_SYSTEMMAIN_QUESTBUT`** в окне **73400 `SYSTEMMAIN_NEW_WIN`**
//       (★ НЕ 73000, как считалось раньше). ★★★ ПОДКЛЮЧЕНИЙ ДВА, И ОНИ РАЗНЫЕ:
//         `UAsdaSystemMenuSubsystem::OnMenuCommand` — САМА КОМАНДА кнопки (открыть/закрыть);
//         `UAsdaSystemMenuSubsystem::RegisterWindow(73404)` — только ПОДСВЕТКА кнопки,
//         которая выводится из фактической видимости окна.
//       Было сделано только второе — отсюда дефект «клавиша Q работает, кнопка нет»
//       (заявка владельца Е3, `Reports/quest_tracker_fix2.md`).
//   Гейты открытия 1:1 с `uiQuestGuide_Open@0xC60280`: окно не открывается при открытых
//   парикмахерской / магазине / складе / окне предметов / диалоге количества и при
//   заблокированном инвентаре; при открытой личной лавке — нотис 107022.
//   Звуки: открытие 5, закрытие 6, смена ячейки 21, разворот/сворот страницы 12.
//
// ═══════════════════════════ ИСПРАВЛЕННЫЕ ДЕФЕКТЫ ОРИГИНАЛА ════════════════════════════════════
//   (закон `fix-original-defects`; каждое место помечено в коде `// FIX-ORIG:`)
//   D1 `miniMapClick_TravelTo@0xC692D0`: `if (a3 != -1 && a4 != 1)` — опечатка, должно быть
//      `a4 != -1`. Следствия: цель с `y == 1` не отрабатывала вовсе, а цель БЕЗ координат
//      (`y == -1`) уводила персонажа в точку −50. Чиним на `Y != -1`.
//   D2 `CQuest_RefreshEpisodeObjectiveText@0xC5E710`: у рейд-задания заголовок брался из слота 0
//      (`44 * i`, где `i` только что обнулён) вместо ВЫБРАННОГО. Чиним — берём выбранный.
//   D3 Журнал захватывал ЧУЖОЙ контрол: `FindControlByID(221)` при строке ошибки `"GET_QUESTITEM"`.
//      221 — это `WARMESSAGE_WIN` (лента военных сообщений), а настоящий `GET_QUESTITEM` (738)
//      не привязан нигде. Чиним: журнал ленту войны НЕ трогает (в порту он её и не берёт).
//   D5 `g_nQuestEpisodeCount` рос без проверки лимита 6 ⇒ после входа в мир чекбокс 743 переставал
//      работать. Чиним: считаем РЕАЛЬНО занятые записи трекера, отдельного счётчика нет.
//   D6 Чекбокс 743 один на 12 слотов, а состояние ему ставили В ЦИКЛЕ ⇒ галка отражала последний
//      обработанный слот. Чиним: галка выводится из ВЫБРАННОГО слота.
//   D7 Ярлык вкладки 742 в JUI `Visible="0"`, текст ему ставят, а `SetVisible(1)` не зовут нигде ⇒
//      подпись «Глава» (нотис 6101) не видно никогда. Чиним: показываем ярлык.
//   D8 Блок «награда на выбор» (790 / 1694…1697 / 795…798) в эталоне мёртв: привязан и погашен,
//      обращений больше нет. Реализован СТРУКТУРНО (данные из `RewardTable.BIN`, поле
//      `ChoiceItemCode`), поведение не выдумываем — по умолчанию блок скрыт, как в оригинале;
//      показывается только когда у пары (questNo, stage) реально есть записи с `ChoiceFlag`.
//   D10 Свёрнутые Hex-Rays диапазоны `id <= 2001 && id > 7000` — это «ВНЕ [2001…7000]».
//      В порту пишем честно: `!(2001 <= id && id <= 7000)`.
//
// ═══════════════════════════ МЕЖКАРТОВЫЙ АВТОПУТЬ ПО КНОПКЕ МАРШРУТА ══════════════════════════
//   ПОРТИРОВАН (2026-08-23, `Reports/quest_tracker_fix.md` §Д3): цель на ЧУЖОЙ карте отдаётся
//   в ЕДИНУЮ точку `UAsdaMinimapSubsystem::BeginMapTravel` (порт `beginMoveToTile@0xFBE140`),
//   исполнитель — `TickMapTravel` (порт блока `game_main.c:1788…1895`), следующий шаг считает
//   `UAsdaQuestMarkerSubsystem::NextPortalStep` — та же точка, что у трекера. Своего расчёта
//   и своего склада состояния журнал не заводит. Цель НА ТЕКУЩЕЙ карте — персонаж идёт в клетку.
#pragma once

#include "CoreMinimal.h"
#include "AsdaTickGate.h"   // ★ R4-c (14.11.2026): шаблон класса (CDO) не тикает
#include "Subsystems/GameInstanceSubsystem.h"
#include "Tickable.h"
#include "Quest/AsdaQuestTypes.h"
#include "AsdaSessionReset.h"   // IAsdaSessionResettable — сброс состояния сессии
#include "UI/AsdaUiInputTypes.h"   // ★ R1: FAsdaMouseFrame / FAsdaDragArm / FAsdaDragGhost … — поля ввода (без всего AsdaUiHelpers.h)
#include "Engine/DataTable.h"      // FTableRowBase / UDataTable
#include "UI/AsdaShopDblClick.h"           // FAsdaDblClick — двойной щелчок по ячейке (событие 0xD, порог Windows)
#include "AsdaHeldTables.h"                // FAsdaHeldTables — удержание таблиц журнала на сессию (15.09.2026, dt_gc_hold)
#include "AsdaQuestLogSubsystem.generated.h"

class UUserWidget;
class UWidget;
class UImage;

// ★ Волна В4 рыбалки (08.10.2026): гейт «идёт рыбалка» кнопок окна заданий (`CQuest_Episode_OnEvent@0xC5BB30`, событие 66561,
//   g:285554 — нажатие поглощается молча). true — кнопку CtrlId исполнять нельзя. Тело и список кнопок — в .cpp.
ASDAREBORN_API bool AsdaQuestEpisodeFishingGate(const class UGameInstance* GI, int32 CtrlId);
class UButton;
class UCheckBox;
class UTextBlock;
class UScrollBox;
class UDataTable;
class UAsdaIconCellGrid;
class UAsdaButtonRelay;
class UAsdaQuestMarkerSubsystem;

// Одна строка маршрута («=> Цель — Карта, x:…, y:…») — то, что кнопки 705…708 показывают и куда
//   они ведут. Собирается из `DT_QuestGuide` (файл `QDT.bin`) по паре (QuestNo, Stage).
USTRUCT()
struct FAsdaQuestRoute
{
	GENERATED_BODY()

	UPROPERTY() FString Text;            // готовая подпись кнопки
	UPROPERTY() int32   MapId = -1;      // карта цели
	UPROPERTY() int32   X = -1;          // клетка (−1 = координат нет)
	UPROPERTY() int32   Y = -1;
};

UCLASS()
class ASDAREBORN_API UAsdaQuestLogSubsystem : public UGameInstanceSubsystem, public FTickableGameObject,
	public IAsdaSessionResettable
{
	GENERATED_BODY()
	friend struct FAsdaFishZoneSelfTest;   // ★ 08.10.2026 (рыбалка В4): самотест гейта «идёт рыбалка» `asda.Fish.ZoneSelfTest`
	friend struct FAsdaNavAttackSelfTest;  // ★ 26.10.2026: самотест `asda.Combat.SelfTestNavAttack` — маршруты и кнопки 705…708
	friend struct FAsdaIconsAfterTravelSelfTest;   // ★ 04.11.2026: самотест `asda.SelfTest.IconsAfterTravel` — иконки наград после смены карты

public:
	virtual void Initialize(FSubsystemCollectionBase& Collection) override;
	virtual void Deinitialize() override;

	// ── IAsdaSessionResettable (К-3) ──
	// Окно журнала, выбранная ячейка и восстановленный трекер принадлежат ПЕРСОНАЖУ.
	virtual void ResetSessionState(EAsdaSessionResetReason Reason) override;

	virtual void Tick(float DeltaTime) override;
	virtual TStatId GetStatId() const override { RETURN_QUICK_DECLARE_CYCLE_STAT(UAsdaQuestLogSubsystem, STATGROUP_Tickables); }
	// ★ R4-c (14.11.2026): шаблон класса (CDO) не тикает — раньше `Tick` звался дважды за кадр, у рабочего объекта и у шаблона
	//   (ключ `asda.Perf.NoCdoTick`, разбор — `AsdaTickGate.h`). Рабочий объект тикает как раньше.
	virtual bool IsTickable() const override { return AsdaTickGate::NotCdo(this); }
	virtual bool IsTickableInEditor() const override { return false; }
	virtual bool IsTickableWhenPaused() const override { return false; }

	// ── Открытие/закрытие (порт `uiQuestGuide_Open@0xC60280` / `uiWnd_Close@0xC604C0`) ──
	UFUNCTION(BlueprintCallable, Category="Asda|QuestLog") void Open();
	UFUNCTION(BlueprintCallable, Category="Asda|QuestLog") void Close();
	// `uiWnd_Close@0xC604C0` буквально — БЕЗ гейта «открыто»: звук 6 и скрытие даже у закрытого окна (эталон зовёт так
	//   при смерти своего персонажа, entity_char.c:16898). Волна В6 ремесла (ревизия В5 МЕЛК-5).
	void CloseUnconditional();
	// Тумблер — ровно то, что делают и клавиша Q (`input.c:4287`), и кнопка 73404.
	UFUNCTION(BlueprintCallable, Category="Asda|QuestLog") void Toggle();
	UFUNCTION(BlueprintPure,     Category="Asda|QuestLog") bool IsOpen() const { return bWindowOpen; }

	// ── Работа со ячейками журнала ──
	// Выбрать ячейку (порт `uiQuestSlotSelectToggle@0xC60130`): звук 12, рамка 737, перерисовка.
	UFUNCTION(BlueprintCallable, Category="Asda|QuestLog") void SelectSlot(int32 SlotIdx);
	UFUNCTION(BlueprintPure,     Category="Asda|QuestLog") int32 GetSelectedSlot() const { return ActiveSlot; }
	// Перенести задание в другую ячейку журнала (C2S 5000, пара типов 4→4). Зовёт бросок ячейки на ячейку (`EndSlotDrag`).
	UFUNCTION(BlueprintCallable, Category="Asda|QuestLog") void MoveSlot(int32 SrcSlot, int32 DstSlot);
	// ★ 06.11.2026 [ИСПРАВЛЕНИЕ] Ответ 5001 на перенос. Инвентарь зовёт сразу после снятия своего флага ожидания: успех с типом 4
	//   применяется здесь (эталон `itemMoveApply` → `questSlotMoveSwap@0xC5CEE0`) и возвращается true — инвентарю пакет не трогать;
	//   отказ снимает ожидание и возвращает false — нотисы по таблице кодов остаются за инвентарём. Ключ `asda.QuestLog.SlotMove`.
	bool ConsumeItemMove5001(const TArray<uint8>& Body24);
	// Ждёт ли журнал ответа 5001 на перенос (живо не дольше `kAbandonAckTimeoutSec`) — гейт новых переносов и отказа, самотест.
	bool IsMovePending() const;
	// Идёт ли перетаскивание ячейки (призрак «в руке») — для самотеста.
	bool IsSlotDragActive() const { return SlotDragSrc >= 0; }
	// Отказаться от выбранного задания: модалка (нотис 6174) → C2S 5006 под-тип 4.
	UFUNCTION(BlueprintCallable, Category="Asda|QuestLog") void AbandonSelected();
	// ★ 31.10.2026 [ИСПРАВЛЕНИЕ] (баг игроков «Cannot delete item» при отказе от задания). Ответ на отказ сервер шлёт ТЕМ ЖЕ
	//   опкодом 5007, что и на уничтожение предмета (`sendCharQuestDump@0x4DB510`, тело 18 б: +0 результат, +1 i16 ячейка,
	//   +5 тип = 4), и больше НИЧЕГО (5047 не шлёт). Эталон `netOnInventoryUseResult` при типе 4 отдаёт пакет журналу
	//   (`uiCraftSlotConfirmRemove@0xC61A30`). Порт выбрасывал такой 5007 — задание висело до перезахода, а повторный отказ
	//   получал от сервера 0 → нотис 6956 «Нельзя удалить предмет». Единственный приёмник: инвентарь зовёт это ПЕРВЫМ
	//   делом в `ApplyDumpResult`; true = пакет принадлежит журналу (инвентарю его не трогать). `bInventoryDumpPending` —
	//   ждёт ли сам инвентарь ответа на своё уничтожение (тело отказа `CharItemDump` типа не несёт), `InventoryDumpSentAt` —
	//   когда ушла его 5006 (`FPlatformTime::Seconds`); оба ожидания живы только `kAbandonAckTimeoutSec` (молча уроненный
	//   пакет не должен навсегда уводить чужой провал не тому приёмнику). Ключ `asda.QuestLog.AbandonAck`.
	bool ConsumeItemDump5007(const TArray<uint8>& Body18, bool bInventoryDumpPending, double InventoryDumpSentAt);
	// Ждёт ли журнал ответа сервера на отказ (эталон `CQuest::acceptPending`) — для самотеста и журналов.
	bool IsAbandonPending() const { return bAbandonPending; }
	// Развернуть/свернуть правую страницу-свиток 732 (кнопки 739 / 735, звук 12).
	UFUNCTION(BlueprintCallable, Category="Asda|QuestLog") void SetPageExpanded(bool bExpanded);
	UFUNCTION(BlueprintPure,     Category="Asda|QuestLog") bool IsPageExpanded() const { return bPageExpanded; }
	// Чекбокс 743 «отслеживать задание» (лимит 6 — нотис 7008 при превышении).
	UFUNCTION(BlueprintCallable, Category="Asda|QuestLog") void SetTrackSelected(bool bTrack);
	// То же для ЛЮБОЙ ячейки журнала, а не только выбранной. Единственная реализация пути
	//   «включить/выключить отслеживание»; `SetTrackSelected` — тонкая обёртка над ней для
	//   выбранной ячейки (чекбокс 743 всегда относится к выбранной).
	UFUNCTION(BlueprintCallable, Category="Asda|QuestLog") bool SetSlotTracked(int32 SlotIdx, bool bTrack);
	UFUNCTION(BlueprintPure,     Category="Asda|QuestLog") bool IsSlotTracked(int32 SlotIdx) const;
	// ★★★ «А есть ли что отслеживать у этой ячейки?» (заявка владельца Е1, `quest_tracker_fix2.md`).
	//   Считает ТОТ ЖЕ построитель `RebuildTrackerEntry` в режиме `bSilent` — второго свода правил
	//   «можно/нельзя» не заводим, иначе галка и трекер разойдутся (это и был дефект эталона D5).
	//   false = записи в трекере не появится ни при каком раскладе: ни строк «предмет : есть/надо»,
	//   ни цели с координатами в `QDT` (либо условия задания уже выполнены). Чекбокс 743 в этом
	//   случае делается НЕДОСТУПНЫМ, а не «ставится и тут же гаснет», как в оригинале.
	UFUNCTION(BlueprintPure,     Category="Asda|QuestLog") bool CanTrackSlot(int32 SlotIdx);
	// ★★★ СПИСКА «какие ячейки отслеживаются» ЗДЕСЬ БОЛЬШЕ НЕТ (закон «Запрет дублирования»).
	//   Раньше журнал вёл свой массив `TrackedSlots`, и он дублировал записи трекера
	//   (`UAsdaQuestMarkerSubsystem::GetTrackEntries`) — причём по НОМЕРУ ЯЧЕЙКИ, а ячейка
	//   принадлежит серверу: после пакета 5042/5047 в ней может оказаться ДРУГОЕ задание, и
	//   пометка «отслеживается» уезжала на чужой квест. Теперь владелец состояния один — трекер,
	//   а ключ — НОМЕР ЗАДАНИЯ (`QuestNo`), который живёт дольше ячейки и переживает вход-выход.
	//   Ячейки, помеченные отслеживанием, при необходимости вычисляются: `TrackedSlotIndices()`.
	TArray<int32> TrackedSlotIndices() const;
	// Маршрут выбранного задания (то, что стоит на кнопках 705…708) — им же пользуется трекер.
	const TArray<FAsdaQuestRoute>& GetRoutes() const { return Routes; }
	// ★ ВОССТАНОВЛЕНИЕ СПИСКА ОТСЛЕЖИВАЕМЫХ ПОСЛЕ ВХОДА В ИГРУ (наше дополнение, эталон списка
	//   не сохранял — см. `Reports/quest_tracker.md` §5.4). Номера заданий читаются из ini
	//   один раз и применяются, как только с сервера приедет состояние журнала (5047): чего в
	//   журнале уже нет — просто не восстанавливается, без ошибок и пустых строк.
	void RestoreTrackedFromSettings();

	// Записать текущий список отслеживаемых заданий в ini. Зовётся из ВСЕХ мест, где состав
	//   меняется (пометил/снял/сдал/отказался/задание ушло из журнала) — отдельного «сохранить»
	//   у игрока нет, как и у раскладки окон.
	void SaveTrackedToSettings() const;

	// Ключ ini для ТЕКУЩЕГО персонажа: `Tracked.<Имя>`. У каждого персонажа свой список — квесты
	//   у них разные. Имя берётся из общей точки `UAsdaWorldSubsystem::GetSelfCharName()`.
	//   ★ Пока имени НЕТ — возвращается ПУСТАЯ строка, и ни чтение, ни запись не происходят
	//   (лечение Е2: раньше здесь подставлялся `Tracked.Default`, из-за чего сохранение
	//   блокировалось навсегда — подробности в теле функции и в `quest_tracker_fix2.md`).
	FString TrackedSettingsKey() const;

	// Секция ini — тот же файл, где живут `[AsdaWindowLayout]` и прочие настройки порта.
	static const TCHAR* kTrackerSection;

	// Строки маршрута для ЛЮБОГО задания (нужны трекеру: он показывает несколько заданий сразу,
	//   сколько именно — единая точка `UAsdaQuestMarkerSubsystem::TrackerLimit()`).
	TArray<FAsdaQuestRoute> BuildRoutesFor(int32 QuestNo, int32 Stage) const;
	// Название задания по номеру ячейки журнала («» — ячейка пуста).
	FString TitleForSlot(int32 SlotIdx) const;

	// ★★★ ПОРТ `QuestReward_UpdateProgressList@0xC65AA0` — СТРОКИ «предмет : есть/надо» ДЛЯ
	//   ЭКРАННОГО ТРЕКЕРА 1800. Живёт здесь, а не у трекера, ровно как в эталоне: там это метод
	//   объекта окна 700 (`CQuest`), и именно он зовёт `questTracker_UpdateEntry`.
	//   Возврат: false — задание в трекер НЕ попало (условия уже выполнены / нет данных /
	//   исчерпан лимит 6). Эталон при false снимает галку чекбокса 743.
	//   `bSilent` = эталонный аргумент `a3`: считать, но ничего не писать (проверка «а можно ли»).
	bool RebuildTrackerEntry(int32 SlotIdx, bool bSilent = false);

	// ★ УДЕРЖАНИЕ ТАБЛИЦ ЖУРНАЛА (15.09.2026, отчёт dt_gc_hold). `DT_EpisodeTable`/`DT_QuestBook`/`DT_QuestGuide`/
	//   `DT_QuestRewardTable`/`DT_SmInfo`/`DT_RaidQuestData` читаются через слабый `AsdaDt::Cached` — в -game GC
	//   выгружал их между обновлениями журнала/трекера. Держим сильно, лениво при первом обращении
	//   (из окна приёма 750 — через `UAsdaNpcDialogSubsystem::HoldGameTables`). Возврат — сколько удержано.
	int32 HoldGameTables() const;
	const FAsdaHeldTables& GetHeldTables() const { return HeldTables; }

	// Пересобрать строки ВСЕХ отслеживаемых заданий — по приходу нового состояния журнала
	//   (5047 / 5052 / 5042). В эталоне это происходит само: `QuestReward_UpdateProgressList`
	//   вызывается из обработчика каждого такого пакета.
	void RebuildAllTrackerEntries();

	// Диагностика для консоли `Asda.QuestLog.Dump`.
	// ★ НЕ const: показывает и доступность галки 743, а её считает построитель `CanTrackSlot`.
	FString Describe();

private:
	// ★★★ ЭКРАННАЯ СТРОКА ПРОГРЕССА ЗАДАНИЯ (эталон `questItemResult_apply@0xC63010` +
	//   `ItemSlot_FormatQuantityTooltip@0xC64050`). Формат ровно как в игре:
	//       «< Название задания > Название предмета : есть/надо »
	//   Показывается в окне 221 (`WARMESSAGE_WIN`) — у него нет подложки, поэтому текст выглядит
	//   «нотисом без рамки». Цвет −256 = чистый жёлтый, время жизни 10 секунд.
	//   Возвращает false, если по этой цели строку собрать не из чего (нет записи в таблицах).
	bool BuildQuestProgressLine(const struct FAsdaQuestProgressDelta& Delta, FString& OutLine) const;

	// Показать готовую строку прогресса на экране (окно 221, жёлтым, 10 секунд, с обводкой).
	void ShowQuestProgressOnScreen(const FString& Line);

	// «Сколько НАДО» этого предмета в текущем шаге задания. ОДНА формула на систему: ею пользуются
	//   и строки трекера, и экранная строка прогресса — второй копии заводить нельзя.
	int32 NeedCountForItem(const struct FAsdaQuestSlot& Slot, int32 ItemCode) const;

private:
	// ── реле кликов (нужны UFUNCTION для AddDynamic) ──
	UFUNCTION() void OnCloseClicked();      // 702 QUEST_TOPCLOSE
	UFUNCTION() void OnTrashClicked();      // 713 QUEST_TRASHBUT
	UFUNCTION() void OnExpandClicked();     // 739 QUEST_SIDEBUT
	UFUNCTION() void OnCollapseClicked();   // 735 QUEST_CLOSEBUT
	UFUNCTION() void OnTrackChanged(bool bIsChecked);   // 743 Quest_State_ChkBtn
	// ★★★ КОМАНДА КНОПКИ 73404 главного меню (`SYSTEMMAIN_NEW_WIN`). Раньше журнал только
	//   РЕГИСТРИРОВАЛ окно у панели (`RegisterWindow`) — это ведёт лишь ПОДСВЕТКУ кнопки, а сам
	//   клик уходил в делегат `UAsdaSystemMenuSubsystem::OnMenuCommand`, на который никто не был
	//   подписан. Инвентарь (73402), навыки (73403) и гача (73410) подписаны — журнал не был,
	//   поэтому клавиша Q работала, а кнопка нет. Точка та же, что у соседей: своей ветки
	//   открытия окна не заводим (эталон `uiHotkey_DispatchCommand` case 34 = та же команда,
	//   что у клавиши Q).
	UFUNCTION() void OnMenuCommand(int32 ButtonIdNum, int32 TargetWindowId, bool bOpen);

	// ── жизненный цикл виджета ──
	bool EnsureWidget();
	void SetupControls();
	void RegisterZones();
	void UnregisterZones();
	bool GetWindowRect(FVector2D& OutMin, FVector2D& OutMax) const;
	bool OnWheel(int32 Notches);

	// ── отрисовка (порт эталонных Refresh-функций) ──
	void RefreshAll();                 // `questLogRefreshSlotIcons` + `questRewardRefreshSlot` + маршруты
	void RefreshSlotIcons();           // `CQuestLog_RefreshSlotIcons@0xC5D500` — 12 ячеек
	void RefreshTitleAndText();        // `CQuest_RefreshEpisodeObjectiveText@0xC5E710` + `questPanel_RefreshSlot@0xC5EE50`
	void RefreshRewards();             // `questBuildRewardSet@0xC663A0` + `questRewardRefreshSlot@0xC68AC0`

public:
	// ════════════════════════════════════════════════════════════════════════════════════════════
	//  ★★★ ОБЩАЯ ТОЧКА «набор наград квеста» — порт `questBuildRewardSet@0xC663A0`.
	//
	//  Ищет в `DT_QuestRewardTable` (импорт `Data\NPC\RewardTable.BIN`) ВСЕ записи с парой
	//  (`QuestId` == QuestNo, `Stage` == Stage) и раскладывает их по ВОСЬМИ ячейкам с фиксированными
	//  ролями — 0 деньги, 1 опыт, 2/3 предметы, 4…7 «на выбор». Промах повторяется с `Stage + 1`
	//  (эталон `questBuildCurrentRewardSet@0xC687A0` и приём 5044 делают это одинаково).
	//
	//  `OutBasic` ВСЕГДА длиной 4; пустая ячейка = `Kind == -1` (эталон её прячет, а не схлопывает).
	//  `OutChoice` — от 0 до 4 записей. Возвращает true, если набор найден.
	//
	//  ★★★ ОТСТУПЛЕНИЕ 2026-08-23: перед возвратом ряд `OutBasic` проходит через
	//    `CollapseRewardCells` — ПУСТАЯ ячейка выкидывается, а соседи сдвигаются влево, чтобы дыр
	//    в ряду не оставалось («сдвигать всегда, когда там пустота»). ⇒ номер ячейки в `OutBasic`
	//    больше НЕ равен роли награды: вид берётся из поля `Kind`. Порядок наград сохраняется.
	//    `OutChoice` НЕ трогается (его индекс уходит на сервер).
	//    Ключ отката `asda.Quest.CollapseRewardCells 0`.
	//
	//  ⚠ Метод статический и живёт ЗДЕСЬ, потому что таблица наград — данные журнала заданий;
	//    второй копии разбора нет (ей пользуется и окно приёма задания 750 в `UAsdaNpcDialogSubsystem`).
	// ════════════════════════════════════════════════════════════════════════════════════════════
	static bool BuildRewardLines(int32 QuestNo, int32 Stage,
		TArray<struct FAsdaQuestRewardLine>& OutBasic, TArray<struct FAsdaQuestRewardLine>& OutChoice);

private:
	/** ★ Сдвиг плиток влево, чтобы в ряду наград не оставалось дыр. Тело ОДНО на все окна — его
	 *  зовёт `BuildRewardLines`. Признаки `bZeroMoney`/`bZeroExp` нужны осторожным режимам 1 и 2
	 *  ключа и логу («почему ячейка опустела»). Обоснование, выбор поведения на границе рядов и
	 *  ключ отката — блок CVar `asda.Quest.CollapseRewardCells` в .cpp, разбор и кадры —
	 *  `Reports/quest_reward_collapse.md`. */
	static void CollapseRewardCells(TArray<struct FAsdaQuestRewardLine>& Basic,
		bool bZeroMoney, bool bZeroExp, int32 QuestNo, int32 Stage);

	void RefreshRoutes();              // `QuestGuide_BuildObjectiveText@0xC68E00`
	// Одна иконка награды: предмет — через общую точку инвентаря, «опыт» — плоской текстурой.
	void DrawRewardIcon(UImage* Img, int32 ItemCode, const TCHAR* FlatIcon);
	void ApplyPageExpanded();          // видимость правой страницы 732 и стрелок 739/735
	// Обводка текста окна: один проход по ВСЕМУ дереву через общую точку
	//   `AsdaUi::ApplyTextOutlineTree` (обоснование — блок `asda.QuestLog.EtalonOutline` в .cpp).
	void ApplyTextOutline();

	// ── отказ от задания: ожидание ответа 5007 (эталон `acceptPending` / `acceptSlotIndex`, quest.c:3004-3005) ──
	bool   bAbandonPending    = false;   // 5006 под-тип 4 ушёл, ответа 5007 ещё нет — повторно не шлём
	int32  AbandonSlotIdx     = -1;      // ячейка журнала (индекс массива 0…11), от которой отказались
	int32  AbandonServerSlot  = -1;      // номер ячейки сервера (поле записи +5), ушедший в 5006
	int32  AbandonQuestNo     = 0;       // номер задания (поле +8) — ключ трекера
	bool   bAbandonWasTracked = false;   // задание отслеживалось до отказа — при отказе сервера вернуть отслеживание
	double AbandonSentAt      = 0.0;     // FPlatformTime::Seconds() отправки — снятие защёлки по молчанию сервера

	// ── перетаскивание ячеек журнала 720…731 (эталон события 5/6/7 `CQuest_Episode_OnEvent`) ──
	void TickSlotDrag();               // покадровый опрос мыши: взвод, порог захвата, бросок
	void BeginSlotDrag(int32 SlotIdx); // событие 5: выбрать ячейку, поднять призрак, приглушить источник (событие 6, FillMode 5)
	void CancelSlotDrag();             // снять призрак и вернуть иконку; действия нет
	void EndSlotDrag();                // событие 7 `questLogSlotClick@0xC5E1B0`: бросок на ячейку / корзину 713 / мимо
	int32 SlotUnderCursor() const;     // индекс ячейки под курсором или −1 (окно журнала должно быть верхним в этой точке)
	bool IsCursorOnQuestWindow() const; // курсор над окном журнала, не перекрытым другим окном
	FAsdaMouseFrame SlotMouse;         // опрос мыши кадра
	// ★ 10.11.2026 [ОРИГИНАЛ] событие 0xD (двойной щелчок) по ячейке 720…731 → `uiQuestSlotSelectToggle@0xC60130`:
	//   выбрать занятую ячейку и развернуть/свернуть страницу разговора (`this+151`). Прежде порт ловил только щелчок.
	void OnSlotClicked(int32 SlotIdx);  // реле кнопки `QUEST_INVENn_HIT`: щелчок (выбор) + распознавание двойного
	FAsdaDblClick SlotDbl;              // распознаватель двойного щелчка (порог `GetDoubleClickTime`)
	FAsdaDragArm SlotDragArm;          // нажатие и порог сдвига (`KeyA` = ячейка)
	UPROPERTY(Transient) FAsdaDragGhost SlotDragGhost;   // летящая за курсором иконка задания
	int32 SlotDragSrc = -1;            // ячейка «в руке» (−1 — захвата нет)

	// ── перенос: ожидание ответа 5001 ──
	mutable bool bMovePending = false; // 5000 ушла, ответа нет — новый перенос и отказ не начинаем
	double MoveSentAt = 0.0;           // FPlatformTime::Seconds() отправки — снятие ожидания по молчанию сервера

	// ── данные ──
	const FAsdaQuestSlot* SlotAt(int32 Idx) const;
	// «Ячейка занята» — эталонная константа: `questId >= 2001`.
	static bool IsSlotBusy(const FAsdaQuestSlot* S) { return S && S->QuestId >= 2001; }
	// D10: свёрнутый Hex-Rays тест «ВНЕ диапазона [2001…7000]», записанный честно.
	static bool IsEpisodeQuestId(int32 Id) { return Id >= 2001 && Id <= 7000; }
	// Строка `DT_EpisodeTable` по коду эпизода (`+200`).
	const struct FAsdaEpisodeRow* EpisodeRow(int32 EpisodeId) const;
	// Строка `DT_QuestBook` по имени файла `.QST` из строки эпизода.
	const FAsdaQuestBookRow* BookRow(const FString& QstFile) const;
	// Сильные ссылки на таблицы журнала (см. `HoldGameTables`).
	UPROPERTY(Transient)
	mutable FAsdaHeldTables HeldTables;
	// Имя карты по её id (для строки маршрута).
	FString MapNameById(int32 MapId) const;
	// Имя цели маршрута: NPC (`TargetKind == 0`) или моб (`== 1`).
	FString RouteTargetName(int32 Kind, int32 Id) const;
	// Идти к клетке маршрута (порт `miniMapClick_TravelTo@0xC692D0` с исправлением D1).
	void TravelToRoute(int32 RouteIdx);

	UAsdaQuestMarkerSubsystem* Marks() const;
	FString Loc(int32 Id) const;
	void PlaySound(int32 Which) const;

	// ── состояние ──
	UPROPERTY(Transient) TObjectPtr<UUserWidget> Win = nullptr;
	bool bReady = false;
	bool bWindowOpen = false;
	bool bPageExpanded = false;
	bool bMenuRegistered = false;
	bool bStateBound = false;
	bool bDirty = true;

	// Выбранная ячейка журнала (эталон `CQuest+0x240 activeSlot`). −1 — ничего не выбрано.
	int32 ActiveSlot = -1;

	// Для какого персонажа список отслеживаемых уже восстановлен из ini (пусто — ни для кого).
	//   Смена персонажа меняет ключ → восстановление произойдёт заново, без всяких сбросов вручную.
	FString TrackedRestoredFor;
	// Строка «журнал есть, имени персонажа нет» пишется ОДИН раз за сессию, иначе Tick зальёт лог.
	bool bTrackedWaitLogged = false;
	// Маршруты выбранного задания (максимум 4 — столько кнопок).
	TArray<FAsdaQuestRoute> Routes;

	// ── виджеты ──
	UPROPERTY(Transient) TObjectPtr<UTextBlock> TitleText   = nullptr;   // 703
	UPROPERTY(Transient) TObjectPtr<UTextBlock> DisplayText = nullptr;   // 714
	UPROPERTY(Transient) TObjectPtr<UTextBlock> TabBarText  = nullptr;   // 742 (D7)
	UPROPERTY(Transient) TObjectPtr<UTextBlock> TrackLabel  = nullptr;   // 744
	UPROPERTY(Transient) TObjectPtr<UTextBlock> RewardHdrA  = nullptr;   // 785
	UPROPERTY(Transient) TObjectPtr<UTextBlock> RewardHdrB  = nullptr;   // 790 (D8)
	UPROPERTY(Transient) TObjectPtr<UCheckBox>  TrackBox    = nullptr;   // 743
	UPROPERTY(Transient) TObjectPtr<UButton>    BtnClose    = nullptr;   // 702
	UPROPERTY(Transient) TObjectPtr<UButton>    BtnTrash    = nullptr;   // 713
	UPROPERTY(Transient) TObjectPtr<UButton>    BtnExpand   = nullptr;   // 739
	UPROPERTY(Transient) TObjectPtr<UButton>    BtnCollapse = nullptr;   // 735
	UPROPERTY(Transient) TObjectPtr<UWidget>    RightPage   = nullptr;   // 732 (группа)
	UPROPERTY(Transient) TObjectPtr<UImage>     SlotSel     = nullptr;   // 737 рамка выделения
	// Девять планов рамки окна `QUEST_WIN_0_0…0_8` — перетягиваются при развороте страницы.
	UPROPERTY(Transient) TArray<TObjectPtr<UImage>>     FrameNine;
	// Узлы с `.JUI` `AlignXPos` режима 2 у САМОГО окна (правый угол титул-бара с крестиком 702 и
	//   шестерёнки 740/741): при развороте едут вправо вместе с краем окна. `RightEdgeBaseX` —
	//   их исходный отступ слева, снятый один раз при разборе контролов.
	UPROPERTY(Transient) TArray<TObjectPtr<UWidget>>    RightEdgeNodes;
	TArray<float>                                       RightEdgeBaseX;
	UPROPERTY(Transient) TArray<TObjectPtr<UImage>>     SlotIcons;       // 720…731
	UPROPERTY(Transient) TArray<TObjectPtr<UTextBlock>> TrayCounts;      // счётчики целей 715…719
	UPROPERTY(Transient) TArray<TObjectPtr<UButton>>    RouteBtns;       // 705…708
	UPROPERTY(Transient) TArray<TObjectPtr<UTextBlock>> RouteTexts;
	UPROPERTY(Transient) TArray<TObjectPtr<UTextBlock>> RewardTexts;     // 786…789
	UPROPERTY(Transient) TArray<TObjectPtr<UTextBlock>> ChoiceTexts;     // 795…798 (D8)
	UPROPERTY(Transient) TObjectPtr<UScrollBox> DescScroll = nullptr;    // список 734
	UPROPERTY(Transient) TObjectPtr<UTextBlock> DescText   = nullptr;
	UPROPERTY(Transient) TObjectPtr<UAsdaIconCellGrid> TrayGrid   = nullptr;  // 715…719 (цели — всегда предметы)
	// ★ Иконки наград сеткой не рисуются: среди видов есть ОПЫТ (плоская текстура `exp2`), у
	//   которого кода предмета нет вовсе. См. `DrawRewardIcon`.
	UPROPERTY(Transient) TArray<TObjectPtr<UImage>> RewardIcons;              // 1690…1693
	UPROPERTY(Transient) TArray<TObjectPtr<UImage>> ChoiceIcons;              // 1694…1697 (D8)
	UPROPERTY(Transient) TArray<TObjectPtr<UAsdaButtonRelay>> Relays;

	// Коды предметов-целей и наград текущего слота — их читают провайдеры сеток иконок.
	TArray<int32> TrayCodes;
	TArray<int32> RewardCodes;
	TArray<int32> ChoiceCodes;

	// ── константы окна ──
	static const FName   kWindowId;
	static const TCHAR*  kWbpPath;
	// AlwaysTop="1" у JUI-корня ⇒ чуть выше инвентаря (720), но сильно ниже всплывашек (9000).
	static constexpr int32 kWinZ = 725;
	// Ширина окна: свёрнутое — из `.JUI` (`QUEST_WIN Size = "338 636"`), развёрнутое — константа
	//   эталона `sub_930E85(pMainWindow, 653, -1)` в `uiToggleStatePanelOn@0xC5DF10`.
	static constexpr float kCollapsedW = 338.f;
	static constexpr float kExpandedW  = 653.f;
	// IDNum кнопки главного меню, открывающей это окно (★ окно 73400, а НЕ 73000).
	static constexpr int32 kMenuButtonIdNum = 73404;
	// Насколько приглушается недоступная галка 743 и её подпись 744 (Е1). ★ Число НЕ выдумано:
	//   ровно так порт уже показывает запрет там, где в JUI-атласе нет disabled-кадра, —
	//   кнопка «Да» модалки с гейтом (`AsdaWindowSubsystem.cpp`, `SetIsEnabled(false)` + 0.55).
	//   Вид запрета в игре должен быть один, поэтому берём тот же, а не свой.
	static constexpr float kDisabledOpacity = 0.55f;
	// Единая точка «сделать галку 743 доступной/недоступной» — её зовёт и перерисовка окна,
	//   и обработчик самого чекбокса.
	void ApplyTrackBoxEnabled(bool bEnabled);
};
