// Copyright (c) 2026 @sitxovski (Discord)
// UAsdaNpcDialogSubsystem — ОКНО РАЗГОВОРА С NPC (JUI `NPC_LINE NPC`, IDNum 570).
//
// ═══════════════════ КЛАССИФИКАЦИЯ (закон `classify-before-building`) ═══════════════════════════
//   ОДНА новая подсистема + точечные правки существующих классов. Своё состояние у неё есть и оно
//   переживает смену карты (текущий NPC, загруженный сценарий, текущая страница, история страниц),
//   закрываться она обязана вместе со всеми игровыми окнами (`RegisterWindowCloser`) — контракт
//   ровно тот же, что у `UAsdaPremiumGachaSubsystem`, по образцу которого она и написана.
//   Класть это в `AAsdaPlayerController` нельзя (он пересоздаётся при травеле), в
//   `UAsdaQuestMarkerSubsystem` — тоже (он про метки и UI знать не должен).
//
// ═══════════════════════════════ ЧТО ДЕЛАЕТ ЭТАЛОН ═════════════════════════════════════════════
// Полный разбор — `Reports/npc_interaction_re.md`. Цепочка открытия (адреса — VA, ASLR off):
//   1. `NPC_OpenWindow@0xD99D80` (npc_mob_pet.c:2457) — гейты, поиск записи `npc_list.BIN` по npcId,
//      загрузка `<nsrName>.NSR`, портрет `Data\NPC\Image\<npc_list+0x46>.TEX`;
//   2. `npcCraftSlotClick_Handle@0xD9C470` (npc_mob_pet.c:3265) — выбор СТАРТОВОЙ страницы:
//      ищет в 64 записях сценария команду с opcode **121** и рисует её `pageId`;
//   3. `npcDialogRenderPage@0xD9C7B0` (npc_mob_pet.c:3323) — заголовок, текст страницы, кнопки;
//   4. `regionWindow_OpenAndPathMove@0xD99290` (ui_core.c:61076) — остановить игрока (path-move
//      «в себя»), `uiCaptureOpenWindowsState` + `uiCloseAllWindows`, показать 573/570/572.
// Закрытие — `UI_resetAllWindowsAndLeave_5045@0xD995C0` (ui_core.c:61142).
// Кнопки — `craftWnd_onButtonClick@0xDA0CA0` + `uiNpcOnTabButton@0xD92120` (581…585 меню,
//   578…580 шаги, 575 «закрыть», 576/577 «Назад»/«Далее»), исполнение — `uiCraftSlotClickHandler`.
//
// ═══════════════════════ ЧТО ДОБАВИЛ ЗАХОД Э3…Э5 ═══════════════════════════════════════════════
//   · УСЛОВИЯ показа/исполнения (`netSerializeCharStatPacketA/B`) — своей копии вычислителя ЗДЕСЬ
//     НЕТ: единственная в порту живёт в `UAsdaQuestMarkerSubsystem::EvalNsrCond`, сюда она приходит
//     вызовом. Категории: уровень, класс, квесты, предметы-цели, надетый предмет, вкладка сумки,
//     слот сумки, «прочее».
//   · ДИСПЕТЧЕР КОМАНД 30001…30088 / 31000 — полный порт таблицы `CGameUI_HandleControlCommand`.
//     Исполняется всё, что не требует чужого ОКНА: телепорт (5477), обмен с NPC (6098), призыв
//     рейд-босса (6124), крафт (6091), событийный NPC (6059), прыжок к цели (6146), CampWar (6746),
//     подсказка 6178 и закрытие. Остальное — честный отказ + строка в реестре отложенного.
//   · ШАГИ СТРАНИЦЫ: `craftSlotFindFirstStateIndex`, `quest_GetEntryProgressState`, третья ветка
//     `uiCraftSlotClickHandler`, приём/сдача шага квеста (5043 → 5044), переходы по страницам.
//   · ОБЛАЧКО ВЫБОРА КВЕСТА `MULTYQUEST_WIN` (2406) и подсказка `QUEST_INFO_WIN` (599).
//
// ═══════════════════════ ГРАНИЦА ПЕРВОГО ЗАХОДА (Э0…Э2 плана §12.3) ═════════════════════════════
//   СДЕЛАНО ЗДЕСЬ: данные `.NSR` (Э0), клик + подбегание (Э1, живёт в контроллере), само окно и
//     ЕГО НАПОЛНЕНИЕ (Э2) — заголовок, портрет, текст страницы, 5 кнопок меню, 3 кнопки шагов,
//     «Назад»/«Далее», закрытие, разбор нажатий (порт `uiNpcOnTabButton` + `craftWnd_onButtonClick`).
//   СЛЕДУЮЩИЙ ЗАХОД (Э3…Э6): диспетчер кодов 30001…30088 (`CGameUI_HandleControlCommand@0xD87590`),
//     исполнение шагов квеста (`quest_GetEntryProgressState` + опкоды 5043/5044), облачко выбора
//     квеста `MULTYQUEST_WIN` (2406), попап наград `EPISODEPOP_WIN` (540), `QUEST_INFO_WIN` (599).
//   ★ ТОЧКИ РАСШИРЕНИЯ ОСТАВЛЕНЫ ЯВНО: `ExecuteCommand` (единственный вход «код → действие»),
//     `RunStepAction` (единственный вход «шаг → действие»), `OnDialogButton` (единственный вход
//     «нажата кнопка окна»). Ни один из них не «заглушка»: то, что реализуемо на локальных данных
//     (закрытие 31000, переход на страницу), выполняется; всё, чья подсистема ещё не портирована,
//     сообщается игроку явной строкой и пишется в `Docs/Port/PORT_PENDING_ON_SYSTEMS.md`
//     (★ исправление D6 оригинала: там такая кнопка молча не делала НИЧЕГО).
#pragma once

#include "CoreMinimal.h"
#include "AsdaTickGate.h"   // ★ R4-b (14.11.2026): шаблон класса (CDO) не тикает
#include "Subsystems/GameInstanceSubsystem.h"
#include "Npc/AsdaNpcDialogTypes.h"
#include "Tickable.h"                  // FTickableGameObject — покадровый опрос курсора над иконками награды
#include "Styling/SlateTypes.h"        // FTextBlockStyle — стиль 587, снятый из JUI
#include "Framework/Text/TextLayout.h" // ETextJustify — выравнивание 587
#include "AsdaSessionReset.h"   // IAsdaSessionResettable — сброс состояния сессии
#include "AsdaHeldTables.h"                // FAsdaHeldTables — удержание таблиц диалога на сессию (15.09.2026, dt_gc_hold)
#include "AsdaNpcDialogSubsystem.generated.h"

class AAsdaNpcActor;
class UUserWidget;
class UWidget;
class UImage;
class UTextBlock;
class UButton;
class UScrollBox;
class UAsdaButtonRelay;
class UDataTable;

UCLASS()
class ASDAREBORN_API UAsdaNpcDialogSubsystem : public UGameInstanceSubsystem, public FTickableGameObject,
	public IAsdaSessionResettable
{
	GENERATED_BODY()

public:
	virtual void Initialize(FSubsystemCollectionBase& Collection) override;
	virtual void Deinitialize() override;

	// ── IAsdaSessionResettable (К-3) ──
	// Открытый диалог NPC и защёлки приёма/сдачи квеста относятся к СЕССИИ.
	virtual void ResetSessionState(EAsdaSessionResetReason Reason) override;

	// ★ ПОКАДРОВЫЙ ОПРОС КУРСОРА. У эталона наведение приходит СООБЩЕНИЕМ окна (`uiWindowMsgHandler`
	//   msg 8/9/0xA), у порта такого канала нет — курсор опрашивает сама подсистема-владелец окна,
	//   ровно как это делает `UAsdaPremiumGachaSubsystem::TickTooltips` (та же общая плашка, тот же
	//   закон `ui-shared-tip-ownership`). ★ Волна В8 (К-9/W8-2): тик ещё и исполняет блок смерти своего персонажа
	//   (`bLeaveOnDeathPending`) одним проходом — 700 → окна ремесла (при ключе asda.Craft.FixDeathRestoreOrder 1) →
	//   5045 → магазины → окна ремесла (при ключе 0, порядок эталона entity_char.c:16898-16911).
	virtual void Tick(float DeltaTime) override;
	virtual TStatId GetStatId() const override { RETURN_QUICK_DECLARE_CYCLE_STAT(UAsdaNpcDialogSubsystem, STATGROUP_Tickables); }
	// ★ R4-b (14.11.2026): шаблон класса (CDO) не тикает — раньше `Tick` звался дважды за кадр, у рабочего объекта и у шаблона
	//   (ключ `asda.Perf.NoCdoTick`, разбор — `AsdaTickGate.h`). Рабочий объект тикает как раньше.
	virtual bool IsTickable() const override { return AsdaTickGate::NotCdo(this); }
	virtual bool IsTickableInEditor() const override { return false; }
	virtual bool IsTickableWhenPaused() const override { return false; }

	/** Открыть диалог с конкретным актором NPC (порт `NPC_OpenWindow` + `npcCraftSlotClick_Handle`
	 *  + `regionWindow_OpenAndPathMove`). false — гейт не пустил либо данных на этого NPC нет. */
	UFUNCTION(BlueprintCallable, Category="Asda|NPC-диалог")
	bool OpenForNpc(AAsdaNpcActor* Npc);

	/** То же по чистому npcId (консольная проверка и вход без актора). */
	UFUNCTION(BlueprintCallable, Category="Asda|NPC-диалог")
	bool OpenForNpcId(int32 NpcId);

	/** ★ УДЕРЖАНИЕ ТАБЛИЦ ДИАЛОГА (15.09.2026, отчёт dt_gc_hold). Таблицы диалога (`DT_NpcQuestNpc`, `DT_NpcQuestScript`,
	 *  `DT_NpcNsrHeader`/`Button`/`Page`, `DT_Talk`, `DT_WarpList`, `DT_NpcSnq`, `DT_SnqIct`, `DT_CallMob`, `DT_EventItem`)
	 *  читаются через слабый `AsdaDt::Cached` — в -game GC выгружал их между разговорами, и открытие диалога /
	 *  листание страниц синхронно грузило таблицы заново. Держим сильно, лениво при первом открытии диалога;
	 *  заодно — таблицы журнала (награды окна 750). Возврат — сколько таблиц диалога удержано. */
	int32 HoldGameTables();
	const FAsdaHeldTables& GetHeldTables() const { return HeldTables; }

	/** Закрыть окно (порт `UI_resetAllWindowsAndLeave_5045@0xD995C0`). */
	UFUNCTION(BlueprintCallable, Category="Asda|NPC-диалог")
	void Close();
	// Закрыватель разговора в росписи «закрыть всё» (`CloseAllPlayerWindows`). ★ Волна В5: у эталона `uiCloseAllWindows`
	//   разговор НЕ закрывает вовсе (в списке ui_core.c:54349-54389 окна диалога нет); роспись — давнее решение порта.
	//   Спрятанный под дочерним окном разговор (`dialogOnConfirmClose` — 6800/16000/6000) при этом жив и должен пережить
	//   «закрыть всё» дочернего окна (элитный магазин 6000 зовёт его при открытии, `charBeginAutoMove`), иначе снимок
	//   окон разговора терялся бы, а HUD разворачивался под открытым магазином.
	void CloseFromCloseAll();

	// ★ Волна В4 ремесла (ревизия В3 С-1/С-3): эталонная пара «спрятать разговор под дочерним окном / выйти из разговора
	//   по закрытию дочернего окна».
	// Порт `dialogOnConfirmClose(g_pNpcWnd, 1)@0xDA3BB0` (ui_core.c:61645-61653): прячет ТОЛЬКО окно 570, гасит флаг
	//   списка сущностей и разговорную петлю NPC; признак «идёт разговор» ([4099]), снимок окон и свёрнутый HUD
	//   остаются до выхода из разговора. Зовут открытия дочерних окон: магазин снаряжения 6800 (`shopWin_Open`,
	//   shop_trade.c:3857), передача параметров 16000 (`extractShop_Open`, item_enhance.c:940).
	void HideForChildWindow(const TCHAR* Who);
	// Порт `UI_resetAllWindowsAndLeave_5045` из ЗАКРЫТИЯ дочернего окна или смерти (`shopWin_Close` shop_trade.c:3918,
	//   `uiInventoryResetAndLeave` ui_core.c:28553, `extractShop_Close` item_enhance.c:1003, `CCharacter_SetAction`
	//   entity_char.c:16900): разговор идёт — `Close()`; нет — эталонный хвост 5045 без диалога: восстановить снимок
	//   окон (`uiCloseAllToggleWindows`) и вернуть HUD (`uiExpandPartyPanel`).
	void ResetAllWindowsAndLeave(const TCHAR* Why);
	// Окно 570 спрятано под дочерним окном (`dialogOnConfirmClose`), разговор при этом продолжается.
	bool IsHiddenForChildWindow() const { return bDialogOpen && bHiddenForChild; }
	// ★ Волна В5 (ревизия В4 С-3): хвост смерти своего персонажа ПОСЛЕ 5045 (`CCharacter_SetAction`, entity_char.c:16901-16906):
	//   магазин снаряжения 6800 (`shopWin_Close`), элитный магазин 6000 (`uiInventoryResetAndLeave`), парикмахерская
	//   (`uiLeaveAndResetAllWindows`) — каждое открытое окно закрывается своей игровой точкой, в порядке эталона.
	//   76000 и 77500 закрывают свои подсистемы по той же смерти (entity_char.c:16908-16911).
	void CloseShopsAfterDeath();

	/** Порт `this[4099]` — «окно диалога сейчас открыто». */
	UFUNCTION(BlueprintPure, Category="Asda|NPC-диалог")
	bool IsDialogOpen() const { return bDialogOpen; }

	UFUNCTION(BlueprintPure, Category="Asda|NPC-диалог")
	int32 GetCurrentNpcId() const { return CurrentNpcId; }

	UFUNCTION(BlueprintPure, Category="Asda|NPC-диалог")
	int32 GetCurrentTalkId() const { return CurrentTalkId; }

	/** Актор собеседника (`this[436]`-сущность). Нужен точке «игрок начал двигаться»: выделение
	 *  снимается ТОЛЬКО с него, чтобы клик по ДРУГОЙ цели не гасил свежий выбор. */
	class AAsdaNpcActor* GetCurrentNpcActor() const { return CurrentNpcActor.Get(); }

	/** Порт `npcDialogRenderPage@0xD9C7B0`. TalkId == 0 → выход из диалога (так эталон и закрывает
	 *  окно по кнопке «Конец»: у шага в данных лежит результат-talkId 0). */
	UFUNCTION(BlueprintCallable, Category="Asda|NPC-диалог")
	void RenderPage(int32 TalkId);

	/** Единственный вход «нажата кнопка окна»: порт `uiNpcOnTabButton@0xD92120` +
	 *  `craftWnd_onButtonClick@0xDA0CA0`. CtrlId — IDNum контрола JUI (575…585). */
	UFUNCTION(BlueprintCallable, Category="Asda|NPC-диалог")
	void OnDialogButton(int32 CtrlId);

	/** ★ ЖИВОЙ ДАМП ОКНА (консоль `asda.Npc.DialogDump`) — доказательная проверка на PIE, а не по
	 *  наличию имён в оффлайн-дампе `.uasset` (закон `false-negatives-method`). Печатает
	 *  ЭФФЕКТИВНУЮ видимость каждого контрола (с проходом вверх по родителям), его подпись,
	 *  наличие обработчика и текущее состояние страницы. */
	FString DumpWindow() const;

	// ★ ЖИВОЙ ДАМП ОБЛАЧКА ВЫБОРА КВЕСТА (консоль `asda.Npc.MultiQuestDump`).
	FString DumpMultiQuest() const;

	/** Порт `questDlgUpdateEpisodeState@0xDA1A60`: собрать оба списка (кого выдаёт / кого принимает
	 *  этот NPC), разложить окно 2406 по эталонной формуле и показать его. */
	UFUNCTION(BlueprintCallable, Category="Asda|NPC-диалог")
	void ShowMultiQuest();

	/** Порт `npcExchangeUiSetText@0xDA3D60` / `npcExchangeUiReset@0xDA3DF0` — подсказка `QUEST_INFO`
	 *  (587) в окне `QUEST_INFO_WIN` (599) с авто-скрытием по таймеру.
	 *  Текст СЫРОЙ, с тегами `<FC=…>`/`<FB>`/`<FS##>` — раскраску делает `ApplyFormatted`. */
	UFUNCTION(BlueprintCallable, Category="Asda|NPC-диалог")
	void ShowQuestInfo(const FString& Text) { ShowQuestInfoEx(Text, /*bMarkBusy*/ true, /*bMayDefer*/ false); }

	/** ★ ОБЩИЙ порт постановки текста в баннер 599. Два признака — эталонные поля `g_pNpcWnd`:
	 *  · `bMarkBusy` = +4211 «показ занят». Взводит его ТОЛЬКО `npcExchangeUiSetText@0xDA3D60`
	 *    (подсказка точки сохранения, итог CampWar). `BuffNameList_BuildColoredText@0xD98210`
	 *    (подсказка «Минопио») ставит лишь +370 — и потому НЕ защищает свой баннер от перебивания.
	 *  · `bMayDefer` = поведение `uiPostDeathRedNotice` (`ui_core.c:32946`):
	 *    `if (+370 != 1 || +4211) показать_сразу; else в_очередь;` — то есть уведомление о смерти
	 *    не перебивает висящую подсказку «Минопио», а ждёт своей очереди.
	 *  Возвращает true, если текст показан сразу; false — если ушёл в очередь. */
	void ShowQuestInfoEx(const FString& Text, bool bMarkBusy, bool bMayDefer);

	/** Порт `npcExchangeUiReset@0xDA3DF0`. ⚠ Очередь (+4210/+16848) НЕ трогает — как в эталоне. */
	UFUNCTION(BlueprintCallable, Category="Asda|NPC-диалог")
	void HideQuestInfo();

	/** ★ ЖИВОЙ ДАМП ПОДСКАЗКИ 599 (консоль `asda.Npc.QuestInfoDump`): эффективная видимость окна и
	 *  крестика, признаки +370/+4211, остаток таймера авто-скрытия. */
	FString DumpQuestInfo() const;

	/** ★ ЖИВАЯ ПРОВЕРКА КРЕСТИКА (консоль `asda.Npc.QuestInfoX`): нажать `QUEST_INFO_x` (586) ТЕМ ЖЕ
	 *  путём, что и мышь — через `OnClicked` самой кнопки, а не вызовом `OnDialogButton` напрямую.
	 *  Иначе проверялась бы вторая реализация правила (закон `false-negatives-method`).
	 *  false — кнопки нет (окно не собрано). */
	bool PressQuestInfoClose();

	/** Выбор строки облачка (порт `npcDialog_RenderPageByCode`; консоль `asda.Npc.MultiQuestPick`). */
	UFUNCTION(BlueprintCallable, Category="Asda|NPC-диалог")
	void PickMultiQuest(int32 CtrlId, int32 Index) { OnMultiQuestPick(CtrlId, Index); }

	/** Прогнать код диспетчера ТЕМ ЖЕ путём, что нажатие кнопки (консоль `asda.Npc.Command`). */
	UFUNCTION(BlueprintCallable, Category="Asda|NPC-диалог")
	void RunCommandForCheck(int32 Code, int32 Param) { ExecuteCommand(Code, Param); }

	/** ★ Разбор условий ВСЕХ под-записей шагов страницы: «категория задана / сошлась» и итог.
	 *  Доказательная проверка вычислителя на живом PIE (консоль `asda.Npc.CondDump`). */
	FString DumpConditions(int32 TalkId) const;

	/** Э6, живая проверка попапа наград без сервера: подставить тройку и пойти ТЕМ ЖЕ путём
	 *  (`asda.Npc.RewardShow`). Отдельной тестовой ветки нет — зовётся штатный `ShowRewardPopup`. */
	void ShowRewardPopupForCheck(const FString& NsrName, int32 TalkId, int32 StepIndex, int32 SubIndex);
	/** Э6: перечислить по данным все страницы, у которых шаг несёт награду (`asda.Npc.RewardScan`). */
	FString DumpRewardScan() const;
	/** Э6: живой разбор ПОКАЗАННОГО попапа 540 — обводка каждой подписи, заголовок, центр экрана,
	 *  подписи панели выбора и ЧЕТЫРЕ ГРАНИ рамки выделения. */
	FString DumpRewardWindow() const;
	/** ★ Э6: клик по строке панели ВЫБОРА — ровно то, что делает прозрачная кнопка
	 *  `UAsdaIconCellGrid` поверх строки (порт `itemTabWnd_onTabButtonCommand@0xD94DE0`).
	 *  Отдельной тестовой ветки нет: и живой клик, и консоль `asda.Npc.RewardSelect` идут сюда. */
	void ClickRewardSelectCell(int32 Cell);

	// ═══════════════ ★ 28.10.2026 ДОСКА ОБЪЯВЛЕНИЙ: попап наград 540 в «режиме доски» ═══════════════
	/** Порт `uiQuestWin_openReward@0xD93160` (ui_core.c:60109): попап 540 `EPISODEPOP_WIN` с наградами записи доски
	 *  (`CQuestWin_AddRewardItem@0xD93390`: 0 золото, 1 опыт, 2…5 «на выбор», 6…7 предметы), раскладка
	 *  `uiWindow_relayoutByState(выбор[0] < 0 ? 0 : 1)`, кнопка 567 ← 6004, заголовок 568 ← имя задания, `this[437] = 1`.
	 *  Кнопка 567 в этом режиме шлёт 6593 через `UAsdaBoardQuestSubsystem` (ветки `uiCraftSlotClickSendReq`).
	 *  Окно разговора 570 при этом НЕ открыто (вход — кнопка окна доски 75281). false — записи доски нет. */
	bool ShowBoardRewardPopup(int32 QuestId, const FString& Title);
	/** Самотест доски: попап 540 виден, выбранная строка «на выбор» и число гарантированных/выбираемых слотов. */
	bool DebugRewardPopupVisible() const;
	int32 DebugRewardSelIndex() const { return RewardSelIndex; }
	int32 DebugRewardBasicNum() const { return RewardBasic.Num(); }
#if !UE_BUILD_SHIPPING
	// ★ 31.10.2026 самотест GC-якорей (`asda.SelfTest.RelayAnchors`): создать окна 540/750/2400/599, затем ПЕРЕСОЗДАТЬ окно
	//   разговора 570 (тот же путь `EnsureWindow`) при живых попапах; вернуть их кнопки 567/780/2411/586 (nullptr — окна нет).
	TArray<UButton*> DebugRecreateTalkKeepPopups();
#endif
	/** Самотест доски: нажатие 567 тем же путём, что кнопка (`OnDialogButton(567)`). */
	void DebugPressRewardOk() { OnDialogButton(567); }
	/** Самотест доски: скрыть попап 540 тем же телом, что закрыватель окна (`HideRewardPopup`). */
	void DebugHideRewardPopup() { HideRewardPopup(); }

	// ═══════════════ ОБМЕН ПО ТАБЛИЦЕ СОБЫТИЙ (6098) — ОДНА ТОЧКА ═══════════════
	/** `npcExchangeBuyRequest@0xDA3F60` (npc_mob_pet.c:3814): проверка пяти пар «предмет/кол-во»
	 *  строки `Type` таблицы `g_pNpcExchangeTable` (DT_EventItem), нотис 98101 при нехватке,
	 *  иначе пакет 6098 (`SelfCharId`, `NpcId`, `Type`). Зовут и кнопки NPC (30052… и др.),
	 *  и использование предмета подтипа 202 (NpcId = −1, как эталон). */
	bool RequestEventExchange(int32 Type, int32 NpcId);
	/** `objFindRecordIndexByField@0xDA4060` (item_inventory.c:53496): ПЕРВАЯ строка таблицы, у которой
	 *  среди пяти требуемых предметов есть `ItemCode`; −1 — нет такой. */
	int32 FindEventExchangeRowByItem(int32 ItemCode) const;
	/** ★★ [ПРОТОКОЛ][ИСПРАВЛЕНИЕ ДЕФЕКТА ОРИГИНАЛА] 27.09.2026 (itemsfix, СРЕДН-4). Сторож обмена 6098:
	 *  сервер по номеру строки берёт требования из СВОЕЙ `EventItem_Table.txt`, а у RU-клиента 22 строки
	 *  из 68 требуют другое (строки 5-7 — сервер спишет 75514…75516 и 50×75462; 17 — 50×56042; 18/38/39 —
	 *  на сервере пусты и т. д.). True — требования строки клиента (`DT_EventItem`, семантика проверки
	 *  клиента) совпадают с требованиями той же строки сервера (`DT_EventItemServer`, снимок таблицы
	 *  сервера, `Tools/asda_build_event_item_server_dt.py`), т. е. сервер спишет ровно то, что проверил
	 *  клиент. False — запрос слать нельзя; `OutWhy` — причина для лога. Нет серверной таблицы — False
	 *  (закрыто: без неё безопасность не доказать). Откат к поведению оригинала: `asda.EventExchange.ServerGuard 0`.
	 *  Протокол не меняется: 6098 либо уходит как у оригинала, либо не уходит вовсе. */
	bool IsEventExchangeServerSafe(int32 Type, FString* OutWhy = nullptr) const;
	/** ★★ 27.09.2026 (itemsdatafix, КРИТ-2). То же, но требования клиента заданы ЯВНО — тем, что реально проверяет
	 *  кнопка при нажатии (30048: 47902 ≥ 11; 30049: 47903…47906 по ≥ 1 — зашиты в диспетчер эталона, строку
	 *  таблицы кнопки не читают). `ClientReq` — пары «код, нужно ≥». Совпало с серверной строкой `Type` — True. */
	bool IsEventExchangeServerSafe(int32 Type, const TArray<TPair<int32, int32>>& ClientReq, FString* OutWhy = nullptr) const;
	/** ★★ 27.09.2026 (itemsdatafix, КРИТ-2). Сторож для КНОПОК 30048/30049: сравнивает с сервером (строки 3/4) ровно
	 *  то, что кнопка проверяет при нажатии. Одна точка и для кнопок, и для самотестов. Иной код — False. */
	bool IsExchangeButtonServerSafe(int32 Code, FString* OutWhy = nullptr) const;

	/** ★ 19.10.2026 — награды ЗАВЕРШЕНИЯ квеста по 5052: `questItemResult_apply@0xC63010` (quest.c:3455) зовёт
	 *  `charApplyFullStatusPacket@0xD83670` (entity_char.c:46312) с телом 5052. Тот собирает блок в раскладке 5042
	 *  (`memcpy(&statusBlock[10], тело + 17, 0x1EB)`) и печатает те же строки `chatBuildTransactionNotice`, что сдача 5042,
	 *  но БЕЗ вида 3 (6019): 2 золото → 4 предметы → 5 очки умений → 6 обаяние. Зовёт журнал квестов из ветки завершения
	 *  5052, после звука 14. Ключ — `asda.Quest.TurnInRewardLines`. Разбор — у `AsdaQuestTurnInReward` в .cpp. */
	void ApplyQuestCompletionRewards5052(const TArray<uint8>& Body5052);

private:
	// ── окно ──
	bool EnsureWindow();
	void SetupControls();
	/** Показать/скрыть узел, РАЗВЕРНУВ всех его свёрнутых родителей (закон: `Visible="0"` из .JUI
	 *  импортёр протаскивает рекурсией на ВСЁ поддерево). */
	void SetVis(UWidget* W, bool bVisible);
	static void ExpandBranch(UWidget* W);

	/** Порт `npcDialogPanelReset@0xD9C5D0`: гасит и очищает заголовок, тело, обе стрелки,
	 *  три кнопки шагов и пять кнопок меню. */
	void ResetPanel();

	/** Порт `uiQuestList_populateSteps@0xDA2600`: подписи кнопок меню 581…585 из `DT_NpcNsrButton`,
	 *  показываются первые `MenuCount` (эталон требует `MenuCount < 6`). */
	void FillMenuButtons();

	/** Порт `listPanel_refreshSlotStates@0xDA2810` (+ исправления D1/D2, см. .cpp). */
	void FillStepButtons(const FAsdaNsrPageRow& Page);

	/** Текст страницы в тело `NPC_TTALK`: разбивка по строкам, перенос по ширине контрола − 12,
	 *  подстановка токена `[NAME]`, цвет 0xFF000000 (эталон `uiChatLogAddLine(..., -16777216, ...)`). */
	void FillPageText(const FAsdaNsrPageRow& Page);

	/** Портрет NPC в `NPC_POSI_26_0` (256×512): `Textures/NPC/Portrait/<ModelName>`. */
	void FillPortrait(const struct FAsdaNpcQuestNpcRow& NpcRec);

	// ── данные ──
	/** Строка `npc_list.BIN` по npcId (`DT_NpcQuestNpc`, ключ = npcId). */
	const FAsdaNpcQuestNpcRow* FindNpcRow(int32 NpcId) const;
	// Сильные ссылки на таблицы диалога (см. `HoldGameTables`).
	UPROPERTY(Transient)
	FAsdaHeldTables HeldTables;
	/** Шапка сценария (`DT_NpcNsrHeader`, ключ = nsrName). */
	const FAsdaNsrHeaderRow* FindHeader(const FString& NsrName) const;
	/** Страница (`DT_NpcNsrPage`, ключ = `<nsrName>_<talkId>`). */
	const FAsdaNsrPageRow* FindPage(const FString& NsrName, int32 TalkId) const;
	/** Кнопка меню (`DT_NpcNsrButton`, ключ = `<nsrName>_<idx>`). */
	const FAsdaNsrButtonRow* FindButton(const FString& NsrName, int32 Index) const;
	/** Стартовая страница: запись сценария с `Opcode == 121` в `DT_NpcQuestScript`
	 *  (порт `npcCraftSlotClick_Handle`). −1 — такой записи у NPC нет. */
	int32 FindStartTalkId(const FString& NsrName) const;

	// ── действия ──
	/** ★ ЕДИНСТВЕННЫЙ вход «код команды → действие» (порт `CGameUI_HandleControlCommand@0xD87590`).
	 *  Сейчас исполняет то, что не требует чужих подсистем (31000 «закрыть»); для остальных 46
	 *  кодов, встречающихся в данных, выдаёт ЯВНОЕ сообщение (исправление D6) и пишет строку в лог.
	 *  Сюда же Э3 доложит остальную таблицу — точка расширения ровно одна. */
	void ExecuteCommand(int32 Code, int32 Param);

	/** ★ ЕДИНСТВЕННЫЙ вход «шаг диалога → действие» (порт третьей ветки `uiCraftSlotClickHandler`). */
	void RunStepAction(int32 StepIndex);

	/** Ветка «нажата кнопка меню» того же `uiCraftSlotClickHandler` (флаг эталона `this[24073]`):
	 *  цикл по пяти под-записям кнопки, состояние 1 = «конец списка», 2 = «проверить условие». */
	void RunMenuButton(int32 ButtonIndex);

	/** Ветка `else if (field2_object)` того же `uiCraftSlotClickHandler` (ui_core.c:59111-59139):
	 *  обход 64 ЗАПИСЕЙ СЦЕНАРИЯ `.NSR` (`DT_NpcQuestScript`) с диспетчером по опкоду
	 *  123 / 122 / 121 / прочее. Включается переходником `sub_933A04` → `sub_D94C60@0xD94C60`,
	 *  который ставит `field2_object = 1` и зовёт обработчик заново. */
	void RunScriptRecordScan();

	/** Порт `npcTalkGetEpisodeRecordOff@0xD94D40`: СЛУЧАЙНАЯ из 30 реплик файла
	 *  `./Data/NPC/Episode/Talk/<nsrName>.bin` (в порту — `DT_Talk`, поля `npc` / `line`).
	 *  Пустая строка, если файла у сценария нет. */
	FString PickEpisodeTalkLine() const;

	/** Вычислитель условий — ПРОКСИ в `UAsdaQuestMarkerSubsystem::EvalNsrCond`. Своей копии нет. */
	bool EvalCond(const FAsdaNsrCond& C, bool bStepOwner, FString* OutWhy = nullptr) const;

	/** Порт `craftSlotFindFirstStateIndex@0xD94AB0`: первое состояние 2/3/4/5 среди пяти под-записей
	 *  ВЫБРАННОГО шага → 1/2/3/4 (0 — ничего). Именно 4 включает облачко выбора квеста. */
	int32 CraftSlotFindFirstStateIndex(int32 StepIndex) const;

	/** Порт `quest_GetEntryProgressState@0xD892D0`: 1 = шаг ВЫДАЁТ квест, 4 = цель доступна к сдаче,
	 *  2 = цель есть, но не сдаётся, 3 = у под-записи состояние 5, 0 = ничего. */
	int32 GetEntryProgressState(const FAsdaNsrStep& Step) const;

	/** Порт `questNpc_IsTargetAvailable@0xD894C0`. */
	static bool IsTargetAvailable(const FAsdaNsrStepObj& Obj);

	/** Порт `gridScanAnyActiveCell@0xD7E2E0` (сравнение с −1, а не с 0 — см. .cpp). */
	bool GridScanAnyActiveCell(const FAsdaNsrStep& Step) const;

	/** Приём 5044 (делегат `UAsdaNetSubsystem::OnQuestStepResult`) — ответ на 5043 «принять». */
	void HandleQuestStepResult(int32 Result, int32 TalkId, const TArray<uint8>& Body);

	/** Приём 5042 (делегат `UAsdaNetSubsystem::OnQuestTurnInResult`) — ответ на 5041 «сдать шаг».
	 *  Отдельный канал: у сдачи свой опкод запроса и свой опкод ответа, см.
	 *  `Reports/quest_accept_fix.md` §5. */
	void HandleQuestTurnInResult(int32 StepCode, int32 QuestNo, int32 Stage, int32 FinishType,
	                             int32 TalkId, const TArray<uint8>& Body);

	/** Порт `sendStartQuestBookProcess@0xD9D9D0` режима 1 («принять») и режима 2 («сдать»):
	 *  перевод кода эпизода в ключ квеста, поиск слота в `SNQInfo.Bin` и отправка 5043. */
	void SendQuestAccept(int32 StepIndex, int32 SubIndex, const struct FAsdaNsrStepSub& S);
	/** Режим 2 «СДАТЬ». `ObjIdx` — индекс БЛОКА ЦЕЛИ (он же индекс выбранной награды): именно он
	 *  уходит третьим аргументом `sendStartQuestBookProcess` и байтом `+4` тела 5041. */
	void SendQuestComplete(int32 StepIndex, int32 SubIndex, int32 ObjIdx);
	/** Порт `questCheckAcceptPrereq@0xD8AE50` — гейт ПЕРЕД отправкой сдачи (нотисы 6193/6194/6195).
	 *  Разбор полей блока цели — в .cpp над телом. */
	bool CheckTurnInPrereq(const struct FAsdaNsrStepObj& Obj) const;
	/** Порт `uiCraftSlotClickSendReq@0xD95190` — обработчик кнопки 567 «ОК» под попапом наград. */
	void ConfirmRewardPopup();
	/** Нотис 20197 «Получено задание [ss1].» — ОДНА точка на приём 5044 и на выдачу при сдаче 5042
	 *  (эталон показывает его в обоих местах). Обоснование места показа — в .cpp над телом. */
	void PostQuestReceivedNotice(int32 EpisodeId, int32 QuestKeyFallback, int32 SrcOpcode);

	// ★ 27.09.2026: `SelectedTargetUserIdx()` удалён — 6091 и 6146 берут СВОЙ userNum (`Target_GetSelectedUserIdx` — миснейм
	//   геттера номера переданного персонажа; отчёт `Reports/owner_0927/charid_senders_audit.md`).

	// ── облачко выбора квеста `MULTYQUEST_WIN` (2406) и подсказка `QUEST_INFO_WIN` (599) ──
	bool EnsureMultiQuestWindow();
	bool EnsureQuestInfoWindow();
	/** Порт `questNpc_CollectDeliverableItems@0xD89550` (режим `a2 == 0`): два списка по 10 записей.
	 *  ★ Гейты списков ОБЩИЕ с метками над головой NPC и на карте — там та же эталонная функция
	 *    портирована в режиме счёта (`UAsdaQuestMarkerSubsystem::CollectDeliverableForNsr`).
	 *    Менять гейт только в одном из двух мест НЕЛЬЗЯ: подсказки разойдутся. Разбор — в .cpp. */
	void BuildQuestLists();
	void HideMultiQuest();
	/** Порт `npcDialog_RenderPageByCode@0xDA2440` + `npcDialog_RenderPageByMode@0xDA2510`. */
	void OnMultiQuestPick(int32 CtrlId, int32 Index);
	/** Вернуть все двигаемые узлы облачка в исходную (JUI) геометрию — база переразметки. */
	void ResetMultiQuestGeometry();
	/** Порт `questDlg_LayoutEpisodeText@0xDA1BB0`: высота окна по числу строк + ширина по тексту.
	 *  Возвращает итоговый размер рамки в дизайн-базе 1024×768. */
	FVector2D RelayoutMultiQuest(int32 Mode, int32 Give, int32 Take);
	/** Перекрасить строки под текущий выбор (эталонное поле списка `+327`). */
	void ApplyMultiQuestSelection();

	// ── состояние (имена полей эталона — в комментариях) ──
	bool bReady = false;
	bool bDialogOpen = false;          // `this[4099]`
	bool bHiddenForChild = false;      // окно 570 спрятано `dialogOnConfirmClose` (разговор продолжается, волна В4)
	bool bLeaveOnDeathPending = false; // смерть своего персонажа: 5045 в Tick (закон `no-reentrant-net-callbacks`)
	bool bInClose = false;             // идёт `Close()` — закрытия дочерних окон не зовут `ResetAllWindowsAndLeave` повторно
	FDelegateHandle DeathHandle;       // подписка на `UAsdaNetSubsystem::OnCharDeath`
	int32 CurrentNpcId = -1;           // `this[436]`
	FString CurrentNsr;                // имя сценария (npc_list +0x26)
	int32 CurrentTalkId = -1;          // `this[7]`
	int32 CurrentPageIndex = -1;       // `this[14]`
	int32 MenuCount = 0;               // блок NSR +0x2C
	int32 PreStepIndex = -1;           // `this[12]` — шаг, повешенный на `NPC_PRE_BUT`
	int32 NextStepIndex = -1;          // `this[13]` — шаг, повешенный на `NPC_NEXT_BUT`
	// `this[4204…4206]` — какой ШАГ страницы сидит на кнопке `NPC_QA_BUT00…02`.
	int32 QaSlot[3] = { -1, -1, -1 };
	// renderId NPC, с которым идёт разговор (для сброса отложенной цели у контроллера).
	int32 CurrentNpcRenderId = -1;
	// ★ Актор собеседника — нужен закрытию окна: `dialogOnConfirmClose@0xDA3BB0` зовёт
	//   `objSetFlag0x1000ById(g_pEntityList, this[436])`, то есть просит ИМЕННО ЭТОГО NPC доиграть
	//   разговорную петлю и вернуться в idle. Слабая ссылка: NPC может деспавниться при травеле.
	TWeakObjectPtr<class AAsdaNpcActor> CurrentNpcActor;

	// ── Э3…Э5 ──
	// `this[4]` — ВЫБРАННЫЙ шаг страницы (его ставят кнопки 576/577/578…580) либо номер кнопки меню.
	int32 SelIndex = -1;
	// `this[24073]` — одноразовый флаг «нажата кнопка МЕНЮ» (гасится сразу после обработки).
	bool bMenuButtonPending = false;
	// `this[437]` — режим «показан попап наград»: в нём окно принимает ТОЛЬКО кнопку 567.
	bool bRewardPopupMode = false;
	// ── поля, закрытые 2026-08-20 вместе со строками 57 / 61 / 62 реестра отложенного ──
	// `[this+0x49C]` — флаг сессии, который ставит `sub_D94C60@0xD94C60` перед повторным входом в
	//   `uiCraftSlotClickHandler`. Читателей во ВСЁМ бинаре нет (сплошной скан секции `.text`):
	//   пишется там и сбрасывается в `UI_resetAllWindowsAndLeave_5045@0xD999DC`. Держим ради 1:1 —
	//   на поведение он не влияет, но без него потерялся бы факт «клик ушёл по ветке сценария».
	bool bScriptClickSessionFlag = false;
	// ── ★ ВОЛНА 1-А, ревизия §7 (06.10.2026): «ПЕРВАЯ ВСТРЕЧА С NPC ЗА ЗАПУСК КЛИЕНТА» ──
	//   `[this+0x4B0 + npcId]` — байт «встреча с этим NPC уже была». Пишет ЕДИНСТВЕННЫЙ писатель — уход от NPC
	//   `UI_resetAllWindowsAndLeave_5045@0xD999AA` (вместе с отправкой 5045), обнуляет только конструктор
	//   `gameObj_ctor@0xD8E300` ⇒ живёт весь запуск клиента: сброс сессии его НЕ трогает (у порта — GameInstance).
	//   [ОРИГИНАЛ] массив 200 байт, индекс npcId без проверки (у npcId 200…204 запись уходит в соседние поля —
	//   дефект оригинала НЕ воспроизводим: у порта массив на 256 бит с проверкой границы).
	TBitArray<> MetNpc;
	// `[this+0x498]` — «это ПЕРВАЯ встреча» (`toggle_SelectOrClear@0xD94A30`: байт 0 → 1, иначе 0); по нему
	//   `npcCraftSlotClick_Handle` выбирает страницу 121 (первая встреча и тип 0) или перебор записей.
	bool bFirstMeet = false;
	// `[this+0x4A0]` — npcId первой встречи (у toggle: выбран → npcId, иначе −1); уходит телом 5045.
	int32 FirstMeetNpcId = -1;
	// `npc_list[+0x4AC]+124` — тип NPC собеседника (5045 шлётся только у типа 0).
	int32 FirstMeetNpcType = 0;
public:
	// Встреча с NPC уже была (байт `+0x4B0 + npcId`). Для самотеста волны 1-А.
	bool IsNpcMet(int32 NpcId) const { return MetNpc.IsValidIndex(NpcId) && MetNpc[NpcId]; }
	// Сбросить отметки встреч — ТОЛЬКО для самотестов (эталон их не сбрасывает до конца запуска).
	void DebugResetNpcMet() { MetNpc.Init(false, 256); }
private:
	// `[this+0x4A4]` (он же `gap_2D0[468]` на записи и `gap_268[572]` на чтении) — ОДНОРАЗОВЫЙ флаг
	//   «следующая страница берёт текст не из `.NSR`, а из `Talk/<nsr>.bin`». Ставит его ветка
	//   опкода 122 в `uiCraftSlotClickHandler` (ui_core.c:59122), читает и ТУТ ЖЕ гасит
	//   `npcDialogRenderPage` (npc_mob_pet.c:3400). Приписка нотиса 111929 в этой ветке НЕ делается.
	bool bTalkPagePending = false;
	// Бит 0 в `g_pEntityList+0x1048` — ГЛОБАЛЬНЫЙ флаг «диалог NPC открыт». Ставит
	//   `sub_91EB81 → sub_D99580@0xD99580` (`|= 1`) из `regionWindow_OpenAndPathMove@0xD99290`
	//   (ui_core.c:61123), снимает `sub_9394A4 → sub_9663E0@0x9663E0` (`&= ~1`) из четырёх мест:
	//   `dialogOnConfirmClose@0xDA3BDD`, `UI_resetAllWindowsAndLeave_5045@0xD99A2C`,
	//   `charLeaveWorldCleanup@0xF8F7EE` и разбор мира в `MainLoop_Tick_SoundUpdate@0x96435E`.
	//   Читателей в срезе эталона НЕТ — держим ради 1:1 и ради честного лога.
	bool bEntityListDialogOpen = false;
	// `gap_28[32]` = БАЙТ `+72` окна — РЕЖИМ панелей попапа наград (0 гарантированная, 1 обе,
	//   2 только выбираемая). Пишет его `sub_921250 → binTableLookup3D@0xD7E3C0` (bin_tables.c:178)
	//   строкой НЕПОСРЕДСТВЕННО перед чтением (ui_core.c:59180-59181), читает
	//   `uiQuestRewardPanelPopulate@0xD93F90` → `questBuildRewardSlots@0xD7E4E0`.
	//   ⚠ При «оба поля нулевые» эталон поле НЕ пишет — значение остаётся с прошлого раза; точки
	//   сброса в срезе нет, поэтому и здесь оно живёт всё время работы подсистемы.
	int32 RewardPanelMode = 0;
	// `this[159]` / `this[158]` — «заявка 5043 в полёте» (принять / сдать); при повторном нажатии
	//   эталон показывает нотис 6523 / 6524 и пакет НЕ шлёт.
	bool bAcceptInFlight = false;
	bool bCompleteInFlight = false;
	// ★ 19.10.2026 [ПОРТ] Момент отправки заявки (FPlatformTime, с) — для сторожа «сервер промолчал»: на 5043 с
	//   неверным слотом выдачи сервер не отвечает вовсе (JOB4, 11.09.2026), и флаг висел до перезахода — любой
	//   клик по квесту давал 6523/6524 (жалоба игроков 19.10). Эталон защёлкивался так же; см. `ExpireStaleInFlight`.
	double AcceptSentAtS = 0.0;
	double CompleteSentAtS = 0.0;
	// Снять «заявку в полёте», если ответа нет дольше сторожа (`asda.Quest.InFlightTimeoutSec`). Зовётся из гейта нажатия.
	void ExpireStaleInFlight();
	// Сохранённая тройка (страница, шаг, под-запись) для режима «сдать» — эталон `this[24101…24103]`.
	int32 SavedTalkId = -1, SavedStep = -1, SavedSub = -1;
	// ★ `this[290]` — «какая вкладка чужого окна сейчас открыта» из диспетчера команд. Ветки 30001…
	//   30004/30010/30011/30012/30026 сверяются с ним и НЕ повторяют открытие. Окон нет, но само
	//   поле воспроизводим: без него порядок отказов отличался бы от оригинала.
	int32 ShopTabState = -1;

	// Строка одного из двух списков облачка: `self[24013 + 3i]` / `self[24043 + 3i]`.
	struct FAsdaQuestPick { int32 QuestId = -1; int32 Key = -1; int32 TalkId = 0; };
	TArray<FAsdaQuestPick> GiveList;   // «в процессе / выдаёт» → список 2408
	TArray<FAsdaQuestPick> TakeList;   // «завершено / принимает» → список 2410

	// ══ Э6: ПОПАП НАГРАД `EPISODEPOP_WIN` (540) ══════════════════════════════════════════════════
	//  Один слот награды — порт записи `this->gap_75C[32*i + 14708…]`, которую заполняют
	//  `itemSlotSetupEquipSowel@0xD7F130` (гарантированные) и `questRewardSlotParse@0xD81460`
	//  (выбираемая), а рисует `QuestReward_SetSlotDisplay@0xD80230`.
	struct FAsdaRewardSlot
	{
		int32 Kind = -1;     // `+14716` — вид награды 0…12 (таблица в .cpp)
		int32 Amount = 0;    // `+14528` — скалярная величина (деньги / опыт / харизма / код эпизода)
		int32 ItemId = -1;   // `+14532` — код предмета или самоцвета
		int32 Count = 0;     // `+14536` — количество предметов
		bool  bSowel = false;// `+14540` — 1 = запись найдена в таблице самоцветов, 0 = обычный предмет
		int32 PickCode = -1; // `+14736` — код «цель активна» (`ObjActive[i]`), которым выбор уходит на сервер
	};

	/** Порт `questStageBuildStatFlags@0xD7E950`: отметить 13 категорий награды и вернуть их число. */
	static int32 BuildRewardStatFlags(const struct FAsdaNsrStepObj& Obj, bool OutFlags[13]);
	/** Порт `craftCheckSlotsValid@0xD83CC0`: «награда состоит ТОЛЬКО из видов 8/9». */
	static bool CraftCheckSlotsValid(const bool Flags[13]);
	/** Порт `itemSlotSetupEquipSowel@0xD7F130`: разложить категорию `Kind` в слот. */
	void FillRewardSlotFromObj(const struct FAsdaNsrStepObj& Obj, int32 Kind, FAsdaRewardSlot& Out) const;
	/** Порт `questRewardSlotParse@0xD81460`: ВЫБИРАЕМАЯ награда (ровно один вид из семи). */
	bool ParseSelectRewardSlot(const struct FAsdaNsrStepObj& Obj, int32 PickCode, FAsdaRewardSlot& Out) const;
	/** Порт `QuestReward_SetSlotDisplay@0xD80230` + `uiQuickSlotPanelBSetRow@0xD960E0`. */
	void ApplyRewardSlotDisplay(bool bSelectPanel, int32 SlotIdx, const FAsdaRewardSlot& Slot);
	bool EnsureRewardWindow();
	/** Обводка контролов попапа 540 строго по эталону: она есть ТОЛЬКО у `EPISODEPOP_ITEMICON01…10`. */
	void ApplyRewardOutline();
	/** Запомнить девять планов рамки 540 и прозрачную ручку — по ним считается высота окна. */
	void CacheRewardFrame();
	/** Развести подзаголовок 555 и подсказку 554: русский текст шире корейского поля 100 px. */
	void FitSelectHintAfterTitle();
	/** Порт `uiWindow_relayoutByState@0xD941A0`: высота рамки + место панели выбора + кнопка «низ». */
	void RelayoutRewardWindow(int32 NumBasic, int32 NumSelect, bool bSelectPanel);
	/** Порт `uiQuestRewardPanelPopulate@0xD93F90`: собрать и показать попап 540. */
	bool ShowRewardPopup(int32 StepIndex, int32 SubIndex);
	void HideRewardPopup();

	// ════════════════════════════════════════════════════════════════════════════════════════════
	//  ОКНО ПРИЁМА ЗАДАНИЯ `QUEST_Confirm_Win` (JUI 750) — порт `CQuest_PopulateRewardWindow@0xC66A20`
	//
	//  ★ Это НЕ попап сдачи 540: другое окно (скин `Popup_Win`, тёмная рамка), другой момент показа
	//    (приём 5044, `resultCode == 0`), другой источник данных (`RewardTable.BIN` по паре
	//    (questNo, stage), а не блоки цели `.NSR`) и другая кнопка (780 «ОК» — только закрыть).
	//    Полный разбор — `Reports/quest_accept_window.md`.
	// ════════════════════════════════════════════════════════════════════════════════════════════
	bool EnsureQuestAcceptWindow();
public:
	/** Собрать и показать окно 750 по паре (questNo, stage) и коду эпизода (для строки 20197).
	 *  Публичный, потому что этим же телом идут и приём 5044, и консольная проверка
	 *  `asda.Npc.AcceptShow` — отдельной тестовой ветки НЕТ. */
	bool ShowQuestAcceptWindow(int32 EpisodeId, int32 QuestNo, int32 Stage);
	void HideQuestAcceptWindow();
	/** Числовой дамп состояния окна 750 (`asda.Npc.AcceptDump`): видимость, высота, текст плиток. */
	FString DumpQuestAcceptWindow() const;
private:
	/** Порт `questRewardPanelResize@0xC676E0`: три высоты окна — 214 / 250 / 390 при ширине 354. */
	void RelayoutQuestAcceptWindow(int32 Height);
	/** Отрисовка ОДНОЙ плитки (порт веток `rewardType` 0/1/2 в `CQuest_PopulateRewardWindow`). */
	void ApplyAcceptRewardTile(bool bChoicePanel, int32 TileIdx, const struct FAsdaQuestRewardLine& Line);
	/** Выбор строки в панели 553 (`EPISODEPOP_SELECTCUR`). */
	void OnRewardSelect(int32 SlotIdx);
	/** ★ Порт msg 8/9/0xA окна 540 (`uiWindowMsgHandler@0xD91DB0` → `uiShowEquipSlotTooltip2`):
	 *  подсказка предмета при наведении на ИКОНКУ награды. Показ идёт в ОБЩУЮ плашку
	 *  `UAsdaItemTooltipSubsystem` от имени владельца `EAsdaTipOwner::QuestReward`; гасим только
	 *  через `HideTooltipFor(владелец)` — закон `ui-shared-tip-ownership`. */
	void TickRewardTooltips();

public:
	/** ★ Единое тело показа подсказки слота награды (его зовёт и покадровый опрос курсора, и
	 *  консоль `asda.Npc.RewardTip <0|1> <слот>`). false — слот пуст либо его вид вне 8…12,
	 *  то есть у эталона подсказки здесь нет. */
	bool ShowRewardSlotTip(bool bSelectPanel, int32 SlotIdx);

private:

	// ── виджеты попапа 540 ──
	UPROPERTY(Transient) TObjectPtr<UUserWidget> RwWin = nullptr;            // 540
	UPROPERTY(Transient) TObjectPtr<UWidget>     RwBasicGrp = nullptr;       // 541 EPISODEBASIC_WIN
	UPROPERTY(Transient) TObjectPtr<UWidget>     RwSelectGrp = nullptr;      // 553 EPISODESELECT_WIN
	UPROPERTY(Transient) TObjectPtr<UTextBlock>  RwBasicTitle = nullptr;     // 542 EPISODE_BASICTITLE
	UPROPERTY(Transient) TObjectPtr<UTextBlock>  RwSelectTitle = nullptr;    // 555 EPISODE_SELECTTITLE
	UPROPERTY(Transient) TObjectPtr<UTextBlock>  RwSelectHint = nullptr;     // 554 EPISODEPOP_TEXT02 (нотис 12005)
	UPROPERTY(Transient) TObjectPtr<UTextBlock>  RwEpisodeTitle = nullptr;   // 568 EPISODEPOP_TITLE00
	UPROPERTY(Transient) TObjectPtr<UButton>     RwEndBut = nullptr;         // 567 EPISODEPOP_ENDBUT
	/** ★ Клик по строке панели ВЫБОРА (порт `itemTabWnd_onTabButtonCommand@0xD94DE0`, контролы
	 *  556…565 и 2403/2404). Прозрачные клик-кнопки поверх JUI-строк даёт ОБЩАЯ точка
	 *  `UAsdaIconCellGrid` — своей копии оверлея не заводим. Иконки строк рисует
	 *  `ApplyRewardSlotDisplay`, поэтому `Refresh()` у сетки НЕ зовётся. */
	UPROPERTY(Transient) TObjectPtr<class UAsdaIconCellGrid> RwSelGrid = nullptr;
	UPROPERTY(Transient) TArray<TObjectPtr<UWidget>>    RwBasicBack;         // 543/545/547/549/551/2400
	UPROPERTY(Transient) TArray<TObjectPtr<UTextBlock>> RwBasicName;         // 589…593, 2402
	UPROPERTY(Transient) TArray<TObjectPtr<UImage>>     RwBasicIcon;         // 544/546/548/550/552/2401
	UPROPERTY(Transient) TArray<TObjectPtr<UTextBlock>> RwBasicQty;          // текст «×N» поверх иконки
	UPROPERTY(Transient) TArray<TObjectPtr<UWidget>>    RwSelBack;           // 556/558/560/562/564/2403
	UPROPERTY(Transient) TArray<TObjectPtr<UTextBlock>> RwSelName;           // 594…598, 2405
	UPROPERTY(Transient) TArray<TObjectPtr<UImage>>     RwSelIcon;           // 557/559/561/563/565/2404
	UPROPERTY(Transient) TArray<TObjectPtr<UTextBlock>> RwSelQty;
	/** ★ 566 `EPISODEPOP_SELECTCUR` — рамка выделения выбранной награды. Скин `Box_Orange3Stroke`
	 *  (`JuiSkin.ini`) — это `ResizeWindow` из ДЕВЯТИ планов сетки 3×3 по 3 px, у которого средний
	 *  план объявлен `PlaneAlwaysInvisible = "1"` (потому рамка и полая). Импортёр невидимые планы
	 *  отбрасывает, поэтому в WBP их восемь — `EPISODEPOP_SELECTCUR_44_0…7`. Массив держим ДЕВЯТКОЙ
	 *  с `nullptr` в центре: этого требует общая точка `UAsdaTipPanel::Resize9Slice`, и так индекс
	 *  плана в массиве совпадает с местом в сетке. */
	UPROPERTY(Transient) TArray<TObjectPtr<UImage>>     RwSelCursor;         // 566 EPISODEPOP_SELECTCUR
	// ── рамка окна 540: девять планов 9-slice + прозрачная ручка перетаскивания той же геометрии ──
	//   Импортёр положил их ПРЯМО в корневую канву окна (не в группу), поэтому раскладка считается
	//   от ТЕКУЩЕГО положения ручки — это делает пересчёт независимым от того, центрировано окно или нет.
	UPROPERTY(Transient) TArray<TObjectPtr<UImage>>     RwFrame;             // EPISODEPOP_WIN_0_0…0_8
	UPROPERTY(Transient) TObjectPtr<UWidget>           RwDrag = nullptr;     // EPISODEPOP_WIN_0_DRAG
	UPROPERTY(Transient) TObjectPtr<UWidget>           RwEndButText = nullptr; // подпись кнопки 567
	// ── виджеты окна приёма задания 750 `QUEST_Confirm_Win` ──
	//   Имена сняты ЖИВЫМ ДАМПОМ (`Tools/_probe_quest_accept_win.py`, лог
	//   `Reports/headless__probe_quest_accept_win.log`), а не угаданы: у плитки есть группа
	//   `…_G<k>_grp`, подпись `…_TE<k>` (TextBlock), картинка иконки `…_MARK<k>_<n>_0` (Image) и
	//   ОДНОИМЁННЫЙ с иконкой TextBlock `…_MARK<k>` — в нём количество (импортёр так разводит
	//   текст и план, ровно как в окне 540).
	UPROPERTY(Transient) TObjectPtr<UUserWidget> AcWin = nullptr;            // 750
	UPROPERTY(Transient) TArray<TObjectPtr<UImage>> AcFrame;                 // QUEST_Confirm_Win_0_0…0_8
	UPROPERTY(Transient) TObjectPtr<UWidget>    AcDrag = nullptr;            // QUEST_Confirm_Win_0_DRAG
	UPROPERTY(Transient) TObjectPtr<UTextBlock> AcTopText = nullptr;         // 751 QUEST_Confirm_Text_Top
	UPROPERTY(Transient) TObjectPtr<UWidget>    AcT1Grp = nullptr;           // группа «Обычная награда»
	UPROPERTY(Transient) TObjectPtr<UWidget>    AcT2Grp = nullptr;           // группа «Дополнительная награда»
	UPROPERTY(Transient) TObjectPtr<UTextBlock> AcT1Title = nullptr;         // 760 (нотис 12003)
	UPROPERTY(Transient) TObjectPtr<UTextBlock> AcT2Title = nullptr;         // 770 (нотис 12004)
	UPROPERTY(Transient) TArray<TObjectPtr<UWidget>>    AcT1Back;            // 752…755 группы плиток
	UPROPERTY(Transient) TArray<TObjectPtr<UTextBlock>> AcT1Name;            // 761/763/765/767
	UPROPERTY(Transient) TArray<TObjectPtr<UImage>>     AcT1Icon;            // картинки 762…768
	UPROPERTY(Transient) TArray<TObjectPtr<UTextBlock>> AcT1Qty;             // количество поверх иконки
	UPROPERTY(Transient) TArray<TObjectPtr<UWidget>>    AcT2Back;            // 756…759
	UPROPERTY(Transient) TArray<TObjectPtr<UTextBlock>> AcT2Name;            // 771/773/775/777
	UPROPERTY(Transient) TArray<TObjectPtr<UImage>>     AcT2Icon;            // картинки 772…778
	UPROPERTY(Transient) TArray<TObjectPtr<UTextBlock>> AcT2Qty;
	UPROPERTY(Transient) TObjectPtr<UButton>    AcOkBut = nullptr;           // 780 QUEST_Confirm_OK
	UPROPERTY(Transient) TObjectPtr<UWidget>    AcBtnGrp = nullptr;          // Btn_CBase_grp — привязана к НИЗУ
	UPROPERTY(Transient) TObjectPtr<UTextBlock> AcOkText = nullptr;          // подпись кнопки (нотис 2288)

	// Собранные слоты и текущий выбор (эталон `gap_75C[14916]` / `[14917]` / `gap_8[56]`).
	TArray<FAsdaRewardSlot> RewardBasic;
	TArray<FAsdaRewardSlot> RewardSelect;
	int32 RewardSelIndex = -1;
	/** Какая иконка сейчас держит подсказку: −1 нет, 0…5 гарантированная награда, 6…11 выбор.
	 *  Эталон повторное msg 8 на том же контроле не шлёт — храним «уже показано», чтобы не
	 *  пересобирать плашку каждый кадр (и не сбрасывать её укладку у курсора). */
	int32 RwTipHover = -1;
	/** Счётчик тиков опроса курсора — доказательство «тик жив» в `asda.Npc.RewardDump`.
	 *  Подсказка по наведению держится ТОЛЬКО на нём, и ноль здесь сразу отделяет «не тикает»
	 *  от «тикает, но не попадает по иконке» (закон `false-negatives-method`). */
	int64 RwTipTicks = 0;

	// ── виджеты облачка 2406 и подсказки 599 ──
	UPROPERTY(Transient) TObjectPtr<UUserWidget> MqWin = nullptr;        // 2406
	UPROPERTY(Transient) TObjectPtr<UWidget>     MqTxt00 = nullptr;      // 2407 «В процессе»
	UPROPERTY(Transient) TObjectPtr<UWidget>     MqTxt01 = nullptr;      // 2409 «Завершено»
	UPROPERTY(Transient) TObjectPtr<UTextBlock>  MqTxt00Text = nullptr;
	UPROPERTY(Transient) TObjectPtr<UTextBlock>  MqTxt01Text = nullptr;
	UPROPERTY(Transient) TObjectPtr<UScrollBox>  MqList00 = nullptr;     // 2408 (создаётся кодом)
	UPROPERTY(Transient) TObjectPtr<UScrollBox>  MqList01 = nullptr;     // 2410 (создаётся кодом)
	UPROPERTY(Transient) TObjectPtr<UButton>     MqClose = nullptr;      // 2411
	// ── переразметка облачка 2406 «по контенту» (порт `questDlg_LayoutEpisodeText@0xDA1BB0`) ──
	//   Рамка окна (скин `Popup_SmWin`, `Type = "ResizeWindow"`) импортёром положена ДЕВЯТЬЮ
	//   планами прямо в корневую канву (`MULTYQUEST_WIN_0_0…0_8`), поэтому меняется она общей
	//   точкой `UAsdaTipPanel::Resize9Slice` с явным `Origin`, как рамка попапа наград 540.
	UPROPERTY(Transient) TArray<TObjectPtr<UImage>> MqFrame;             // 9 планов рамки 2406
	UPROPERTY(Transient) TObjectPtr<UImage>      MqHdrBar00 = nullptr;   // жёлтая плашка шапки 2407
	UPROPERTY(Transient) TObjectPtr<UImage>      MqHdrBar01 = nullptr;   // красная плашка шапки 2409
	UPROPERTY(Transient) TObjectPtr<UImage>      MqRBase00 = nullptr;    // правый «уголок» шапки 2407
	UPROPERTY(Transient) TObjectPtr<UImage>      MqRBase01 = nullptr;    // правый «уголок» шапки 2409
	/** Исходная (запечённая импортёром из `.JUI`) геометрия узлов, которые двигает переразметка.
	 *  Нужна потому, что показ окна идёт МНОГО раз с разными режимами, а `CenterWindowOnScreen`
	 *  переводит корневые слоты на якорь 0.5 — считать «от текущего» после этого нельзя. */
	//   Якоря у всех этих узлов в `.JUI` точечные (0,0)-(0,0), поэтому хранить их не нужно —
	//   сброс ставит ровно точечный якорь; храним смещения и выравнивание.
	struct FMqBase { TWeakObjectPtr<UWidget> W; FMargin Off; FVector2D Align; };
	TArray<FMqBase> MqBaseGeom;
	/** Реле строк списков (владеет лямбдой клика) — живут столько же, сколько окно. */
	UPROPERTY(Transient) TArray<TObjectPtr<UObject>> MqRowRelays;
	/** Выбранная строка (эталонное поле списка `+327`): контрол 2408/2410 и индекс, −1 = нет. */
	int32 MqSelCtrl = -1;
	int32 MqSelIndex = -1;
	UPROPERTY(Transient) TObjectPtr<UUserWidget> QiWin = nullptr;        // 599
	// ★ 587 — ПОСЛЕ первого показа это `URichTextBlock`: `UAsdaRichText::ApplyFormatted`
	//   ПОДМЕНЯЕТ `UTextBlock` в том же слоте (имя и геометрия сохраняются). Поэтому тип общий,
	//   а поле переприсваивается возвращённым узлом — иначе кэш осиротеет на выброшенном виджете.
	UPROPERTY(Transient) TObjectPtr<UWidget>     QiText = nullptr;       // 587
	UPROPERTY(Transient) TObjectPtr<UButton>     QiClose = nullptr;      // 586
	UPROPERTY(Transient) TObjectPtr<UWidget>     QiTalkBox = nullptr;    // группа пузыря 599
	UPROPERTY(Transient) TObjectPtr<UWidget>     QiRobot = nullptr;      // Ani_GodRobot_Alarm (16 кадров)
	// Таймер авто-скрытия подсказки (эталон `uiNpcExchangeTimerTick`).
	FTimerHandle QuestInfoTimer;
	// Истёк показ баннера: порт ветки `now - start > 10000` тика — гасим и разбираем очередь.
	void OnQuestInfoExpired();
	// ── Состояние баннера 599 (эталонные поля `g_pNpcWnd`) ──
	bool bQiActive = false;      // +370 «подсказка активна»
	bool bQiBusy = false;        // +4211 «показ занят» (ставит только `npcExchangeUiSetText`)
	bool bQiQueued = false;      // +4210 «в очереди лежит второй текст»
	FString QiQueuedText;        // +16848 буфер отложенного текста (в эталоне 253 б, ровно ОДИН слот)
	// Стиль контрола 587 из JUI, снятый ДО подмены на rich (кегль/цвет/обводка/выравнивание/перенос).
	//   Без него новый `URichTextBlock` взял бы дефолт из `DT_AsdaRichStyle` и потерял вид окна.
	FTextBlockStyle QiBaseStyle;
	bool bQiStyleCaptured = false;
	ETextJustify::Type QiJustify = ETextJustify::Left;
	bool bQiWrap = true;
	// Подписка на 5044 (`UAsdaNetSubsystem::OnQuestStepResult`).
	FDelegateHandle QuestStepResultHandle;
	// Подписка на 5042 (`UAsdaNetSubsystem::OnQuestTurnInResult`).
	FDelegateHandle QuestTurnInResultHandle;

	// ── виджеты ──
	UPROPERTY(Transient) TObjectPtr<UUserWidget> Win = nullptr;             // 570
	UPROPERTY(Transient) TObjectPtr<UTextBlock>  NameText = nullptr;        // 573 NPC_TNAME
	UPROPERTY(Transient) TObjectPtr<UImage>      Portrait = nullptr;        // NPC_POSI (план 256×512)
	UPROPERTY(Transient) TObjectPtr<UScrollBox>  TalkLines = nullptr;       // тело 574 NPC_TTALK (создаётся кодом)
	UPROPERTY(Transient) TObjectPtr<UWidget>     TalkBoxGrp = nullptr;      // группа 571 NPC_TALKBOX
	UPROPERTY(Transient) TObjectPtr<UWidget>     BubbleGrp = nullptr;       // группа 572 NPC_Talkbox2
	UPROPERTY(Transient) TObjectPtr<UWidget>     FuncBoxGrp = nullptr;      // группа NPC_Function_Box
	UPROPERTY(Transient) TObjectPtr<UButton>     CloseBtn = nullptr;        // 575
	UPROPERTY(Transient) TObjectPtr<UWidget>     PreGrp = nullptr;          // группа 576
	UPROPERTY(Transient) TObjectPtr<UWidget>     NextGrp = nullptr;         // группа 577
	UPROPERTY(Transient) TObjectPtr<UTextBlock>  PreText = nullptr;         // подпись 615 (создаётся кодом)
	UPROPERTY(Transient) TObjectPtr<UTextBlock>  NextText = nullptr;        // подпись 616 (создаётся кодом)
	UPROPERTY(Transient) TArray<TObjectPtr<UButton>>    QaButtons;          // 578…580
	UPROPERTY(Transient) TArray<TObjectPtr<UTextBlock>> QaTexts;
	UPROPERTY(Transient) TArray<TObjectPtr<UButton>>    ShopButtons;        // 581…585
	UPROPERTY(Transient) TArray<TObjectPtr<UTextBlock>> ShopTexts;
	UPROPERTY(Transient) TArray<TObjectPtr<UAsdaButtonRelay>> Relays;       // GC-якоря обработчиков окна разговора 570
	// ★ 31.10.2026 [ИСПРАВЛЕНИЕ] (`asda.Npc.PopupRelayAnchor`): GC-якоря кнопок САМОСТОЯТЕЛЬНЫХ окон подсистемы — попап наград 540
	//   («ОК» 567), окно приёма 750 (780), облачко 2400 (2411), подсказка 599 (586). Раньше они лежали в `Relays`, а его сбрасывает
	//   пересоздание окна 570 (`EnsureWindow`), тогда как сами окна живы и заново не привязываются: реле оставалось без якоря и
	//   после ближайшей сборки мусора кнопка молча переставала работать (попап доски 75000, потом первый за сессию разговор с NPC —
	//   «Готово» не сдаёт задание). У каждого окна СВОЙ якорь; он сбрасывается, только когда пересоздаётся само это окно.
	UPROPERTY(Transient) TArray<TObjectPtr<UAsdaButtonRelay>> RwRelays;   // попап наград 540: «ОК» 567
	UPROPERTY(Transient) TArray<TObjectPtr<UAsdaButtonRelay>> AcRelays;   // окно приёма 750: «ОК» 780
	UPROPERTY(Transient) TArray<TObjectPtr<UAsdaButtonRelay>> MqRelays;   // облачко 2400: крестик 2411
	UPROPERTY(Transient) TArray<TObjectPtr<UAsdaButtonRelay>> QiRelays;   // подсказка 599: крестик 586
	// Куда класть реле кнопки самостоятельного окна: его собственный якорь `Own` (ключ 1) или прежний общий `Relays` (ключ 0).
	TArray<TObjectPtr<UAsdaButtonRelay>>& PopupRelayAnchor(TArray<TObjectPtr<UAsdaButtonRelay>>& Own);

	// Ширина тела реплики в базовых пикселях (снимается с живого контрола `NPC_TTALK_12_HIT`;
	//   эталон берёт её виртуальным методом vtbl+24 и вычитает 12 — см. `npcDialogRenderPage`).
	float TalkWrapWidth = 338.f;

	// ★ 18.10.2026: самотест `asda.Quest.SelfTestJobChange` строит списки «выдаёт/принимает» (`BuildQuestLists`)
	//   у выбранного NPC — тот же путь, что облачко 2406 в живом диалоге.
	friend struct FAsdaQuestJobChangeSelfTest;
};
