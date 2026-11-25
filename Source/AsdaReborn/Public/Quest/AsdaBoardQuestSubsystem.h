// Copyright (c) 2026 @sitxovski (Discord)
// UAsdaBoardQuestSubsystem — ★★★ ДОСКА ОБЪЯВЛЕНИЙ: «ограниченные задания» (LimitationQuest, 토벌퀘스트) 20000…20056,
//   окно `Subjugate_WIN` (JUI IDNum 75000), NPC-доски 9 `andres` (карта 0) и 10 `10npc` (карта 7), тип-функции NPC 5.
//   Порт 28.10.2026. Полный разбор (источник истины) — `Docs/Port/systems/BOARD_QUEST_FLOW.md`; отчёт волны —
//   `Reports/board_quest/CLIENT.md`.
//
// ═══════════════════ ЧТО ЭТО В ЭТАЛОНЕ ═══════════════════════════════════════════════════════════════════════════
//   Объект `g_pQuestCraftWnd` (миснеймы IDA: `CQuestCraftWnd`, `CPQuest_*` — к ремеслу отношения не имеет). Поля объекта
//   (dword-индексы, §3 спецификации) перенесены в поля подсистемы с теми же номерами в комментариях:
//   [1] выбранная строка, [2] вкладка, [3] строка под курсором, [4] npcId, [5] число вкладок, [10] задание, ждущее сдачи,
//   [11…17] контролы окна, [18+7i] строки списка, [158…180] правая панель, [181]/[182] таблицы, [183+5n] вкладки, [208] узлы.
//
// ═══════════════════ ЧТО ДЕЛАЕТ ═════════════════════════════════════════════════════════════════════════════════
//   • Данные (`CPQuest_BuildUI@0xEE8960`): `DT_BoardNpc` (Raid_npc_data.BIN) и `DT_RaidQuestData` (Raid_quest_data.BIN),
//     генератор `Tools/asda_import_board_quests.py`, числа — клиентская таблица Interlude (решение владельца 28.10).
//   • Узлы состояния (8 б: questId, тип, счёт, состояние 0/1/2/3) — приём 6588 (при каждом входе на карту), 6590, 6594, 6595.
//   • Окно: клик по NPC типа 5 → `NPC_SpecialAction_Type5@0xEE95B0` (фильтр `questUiBuildAvailableLists@0xEEACE0`, список
//     `QuestUI_PopulateQuestList@0xEEB150`, панель `ItemDetail_PopulateWithSowels@0xEEB990`, события `questUiOnControlEvent@0xEE9860`).
//   • «Взять» → 6591; «Выполнено» → попап наград 540 «режим доски» (`UAsdaNpcDialogSubsystem::ShowBoardRewardPopup`) → 6593.
//   • Фон: покадровая готовность «Собрать» (`mapObjBeginFirstReadyCraft@0xEEF010`), метка доски на миникарте
//     (`craftCheckRecipeMaterialsAvailable@0xEEEE70`), данные журнала/трекера для заданий 20000…30000.
//
// ═══════════════════ ИСПРАВЛЕНИЯ ОРИГИНАЛА (§13 спецификации; решение владельца 28.10 — исправлять, ключ 0 = как оригинал) ═══
//   Д-1 `asda.Board.FixMinimapLevel`   — метка миникарты: условие уровня эталона перевёрнуто (lvMin ≥ ур. && lvMax ≤ ур.).
//   Д-2 `asda.Board.FixCollectReady`   — покадровая готовность «Собрать»: `>=` вместо `==` (лишний предмет — тоже готово).
//   Д-3 `asda.Board.FixStaleTabs`      — вкладки сверх числа зон доски скрываются (эталон оставлял старые подписи).
//   Д-4 `asda.Board.FixReqItems5`      — в панели все 5 требуемых предметов (эталон — 3; 4-й/5-й строк в окне нет —
//                                        они идут вторыми через «, » в строках 75244/75245: предмет k → строка k % 3).
//   Д-5 `asda.Board.FixDailyState`     — 6595 снимает и состояние 3 «Заканчивается сегодня» у дневных (эталон — только счёт).
//   Д-6 `asda.Board.FixServerType`     — тип узла для 6590 и подсказки журнала — из узла сервера (эталон — из таблицы клиента).
//   Д-7 (без ключа)                    — диапазон колонки «Кол-во» 75301…75320 (эталон 75301…75321 — 21 значение).
//   Плюс найденные при порте: хвост-паддинг 6588 (узлы вне таблицы отбрасываются), сброс «ждущего сдачи» при закрытии
//   попапа 540 без сдачи (`asda.Board.FixPendingReset`), бит карты завершения 20000+ за её границей (журнал).
#pragma once

#include "CoreMinimal.h"
#include "AsdaTickGate.h"   // ★ R4-c (14.11.2026): шаблон класса (CDO) не тикает
#include "Subsystems/GameInstanceSubsystem.h"
#include "Tickable.h"
#include "AsdaSessionReset.h"
#include "AsdaHeldTables.h"
#include "Quest/AsdaQuestTypes.h"
#include "UI/AsdaUiInputTypes.h"   // ★ R1: FAsdaMouseFrame / FAsdaDragArm / FAsdaDragGhost … — поля ввода (без всего AsdaUiHelpers.h)
#include "Containers/Queue.h"      // TQueue — очередь пакетов из колбэка сети
#include "Styling/SlateTypes.h"     // FButtonStyle — кэш стилей вкладок (`AsdaUi::ApplyTabState`)
#include "AsdaBoardQuestSubsystem.generated.h"

class UUserWidget;
class UWidget;
class UTextBlock;
class UImage;
class UButton;
class UAsdaWindowSubsystem;
class UAsdaButtonRelay;

// Узел состояния задания (8 б эталона и сервера `CUser[1]+0xB6E+681+8·idx`): `+0 i32 questId`, `+4 u8 тип` (серверный),
//   `+5 i16 count`, `+7 u8 state` (0 доступно / 1 взято / 2 цели выполнены / 3 лимит исчерпан на сегодня/навсегда).
struct FAsdaBoardNode
{
	int32 QuestId = -1;   // +0
	int32 Type = 0;       // +4 (u8)
	int32 Count = 0;      // +5 (i16)
	int32 State = 0;      // +7 (u8; эталон сравнивает знаковым char)
};

// Вкладка доски `[183+5n]`: зона и индексы k заданий (`questTable[k]`), прошедших фильтр.
struct FAsdaBoardTab
{
	int32 Zone = -1;          // зона (индекс `CMap_InitZoneList`)
	TArray<int32> Indices;    // индексы k записи `questTable` (не номера заданий — §2.4)
};

// Строка левого списка `[18+7i]`: main 75021+i, Вид 75061+i, Ур. 75101+i, Задание 75141+i, Состояние 75181+i, Кол-во 75301+i,
//   подсветка 75211+i. Узлы: `Subjugate_BB1_{i+1}_grp`, `Subjugate_BB1_{i+1}_TXT00…04` (шаг ассета
//   `Tools/asda_ui_subjugate_rows.py` для строк 2…20), подсветка — план `Box_BlueOt20_*` группы.
USTRUCT()
struct FAsdaBoardRowUi
{
	GENERATED_BODY()
	UPROPERTY(Transient) TObjectPtr<UWidget>    Main = nullptr;    // [18+7i] группа строки (видимость строки)
	UPROPERTY(Transient) TObjectPtr<UWidget>    MainText = nullptr;// контрол 75021+i (его позиция — опора рамки выбора 75041)
	UPROPERTY(Transient) TObjectPtr<UTextBlock> Kind = nullptr;    // [19+7i] 75061+i «<Повтор>»
	UPROPERTY(Transient) TObjectPtr<UTextBlock> Level = nullptr;   // [20+7i] 75101+i «мин/макс»
	UPROPERTY(Transient) TObjectPtr<UTextBlock> Name = nullptr;    // [21+7i] 75141+i имя задания
	UPROPERTY(Transient) TObjectPtr<UTextBlock> State = nullptr;   // [22+7i] 75181+i состояние
	UPROPERTY(Transient) TObjectPtr<UTextBlock> Count = nullptr;   // [23+7i] 75301+i «%d / %d» / «∞»
	UPROPERTY(Transient) TObjectPtr<UWidget>    Hover = nullptr;   // [24+7i] 75211+i подсветка наведения
	UPROPERTY(Transient) TArray<TObjectPtr<UWidget>> Hits;         // зоны мыши пяти колонок (их объединение — строка)
};

// Ячейка награды правой панели: иконка 32×32, её подпись-количество (текст той же ячейки) и подпись-имя.
USTRUCT()
struct FAsdaBoardRewardUi
{
	GENERATED_BODY()
	UPROPERTY(Transient) TObjectPtr<UImage>     Icon = nullptr;    // иконка (75252/54/56/58, 75261/63/65/67)
	UPROPERTY(Transient) TObjectPtr<UTextBlock> Qty = nullptr;     // текст той же ячейки (количество)
	UPROPERTY(Transient) TObjectPtr<UTextBlock> Label = nullptr;   // подпись (75253/55/57/59, 75262/64/66/68)
};

UCLASS()
class ASDAREBORN_API UAsdaBoardQuestSubsystem : public UGameInstanceSubsystem, public FTickableGameObject,
	public IAsdaSessionResettable
{
	GENERATED_BODY()

	friend struct FAsdaBoardQuestSelfTest;   // офлайн-самотест `asda.SelfTest.BoardQuest`

public:
	// Подписки на сеть (6588/6589/6590/6594/6595) и инвентарь (режим «Собрать»).
	virtual void Initialize(FSubsystemCollectionBase& Collection) override;
	// Снять подписки, окно.
	virtual void Deinitialize() override;
	// Сброс сессии: окно — всегда; узлы — только при конце сессии персонажа (6588 новой карты НЕ теряется — §ResetSessionState).
	virtual void ResetSessionState(EAsdaSessionResetReason Reason) override;
	// Кадр: очередь пакетов → покадровая готовность «Собрать» → опрос мыши окна.
	virtual void Tick(float DeltaTime) override;
	// Стат тикера.
	virtual TStatId GetStatId() const override { RETURN_QUICK_DECLARE_CYCLE_STAT(UAsdaBoardQuestSubsystem, STATGROUP_Tickables); }
	// В редакторе не тикаем.
	// ★ R4-c (14.11.2026): шаблон класса (CDO) не тикает — раньше `Tick` звался дважды за кадр, у рабочего объекта и у шаблона
	//   (ключ `asda.Perf.NoCdoTick`, разбор — `AsdaTickGate.h`). Рабочий объект тикает как раньше.
	virtual bool IsTickable() const override { return AsdaTickGate::NotCdo(this); }
	virtual bool IsTickableInEditor() const override { return false; }
	// На паузе не тикаем.
	virtual bool IsTickableWhenPaused() const override { return false; }

	// ═════════════════════ ЧИСЛА ЭТАЛОНА ═════════════════════
	static constexpr int32 kRows = 20;           // строк списка (75021…75040)
	static constexpr int32 kTabs = 5;            // вкладок-зон (TabBar_W108_5Btn, `+8+4j`, j 0…4)
	static constexpr int32 kNpcSlots = 100;      // слотов заданий у записи NPC (`+28+4k`, k 0…99)
	static constexpr int32 kQuestBase = 20000;   // номер задания = 20000 + индекс
	static constexpr int32 kChoiceNone = 99;     // байт выбора 6593 «без выбора»

	// ═════════════════════ ПУБЛИЧНЫЙ ВХОД ═════════════════════
	// Есть ли включённая доска (`asda.Board.Enabled`, 1 по умолчанию; 0 — прежнее поведение порта: отказ с логом).
	static bool IsEnabled();
	// Открыто ли окно доски у этого экземпляра игры (гейты: клик по земле, окно ремесла 76000).
	static bool IsBoardOpen(const UGameInstance* GI);
	// ★ `NPC_SpecialAction_Type5@0xEE95B0`: наполнить и показать окно 75000 для npcId. false — npcId < 0, окна нет.
	bool OpenForNpc(int32 NpcId);
	// ★ `questUiReset@0xEE9790`: закрыть окно (вкладки в исходный порядок, выбор/наведение снять, рамка 75041 скрыта).
	void Close();
	// Окно видно.
	bool IsOpen() const { return bOpen; }

	// ═════════════════════ ДАННЫЕ ДЛЯ СМЕЖНЫХ СИСТЕМ (журнал, трекер, метки, попап 540) ═════════════════════
	// `mapObjFindEntryById@0xEECC90`: запись задания по номеру (nullptr — нет).
	const FAsdaRaidQuestRow* FindQuest(int32 QuestId) const;
	// `mapObjFindValueById@0xEECC00`: тип периода задания по ТАБЛИЦЕ КЛИЕНТА (−1 — нет записи).
	int32 FindQuestType(int32 QuestId) const;
	// Тип периода для 6590/подсказки журнала: [ИСПРАВЛЕНИЕ ОРИГИНАЛА Д-6] из узла сервера, если он есть (ключ
	//   `asda.Board.FixServerType`), иначе — из таблицы клиента, как эталон.
	int32 EffectiveQuestType(int32 QuestId) const;
	// `listFindNodeByKey@0xEECD20`: узел состояния (nullptr — нет).
	const FAsdaBoardNode* FindNode(int32 QuestId) const;
	// `craftCheckRequirementItemsMet@0xEEEBE0`: «задание готово к сдаче» (режим 1 — всё в сумке ≥, режим 0 — узел state 2).
	bool IsQuestReady(int32 QuestId) const;
	// `craftCheckRecipeMaterialsAvailable@0xEEEE70`: метка доски npcId на миникарте (1 — есть, 0 — нет).
	int32 MinimapMark(int32 NpcId) const;
	// ★ 28.10.2026 [ИСПРАВЛЕНИЕ ОРИГИНАЛА] (ключ `asda.Board.FixOverheadMark`, запрос владельца): значок НАД доской, как у
	//   квестодателей. У эталона его нет (`npcSlotLoadModelAndSetup@0xD8A6B0` ставит метку только типам 0 и 4). 2 — красный «!»
	//   (есть задание доски, готовое к сдаче; главнее), 1 — жёлтый «!» (есть задание, которое можно взять сейчас — условия метки
	//   миникарты С-1), 0 — нет / не доска / ключ 0. Рисует `UAsdaQuestMarkerSubsystem::PushOverheadMarks`.
	int32 OverheadMark(int32 NpcId) const;
	// Доска ли это (есть запись `Raid_npc_data`).
	bool IsBoardNpc(int32 NpcId) const;
	// Задание, ждущее сдачи через попап 540 (`this[10]`, `sub_936EB1`): −1 — нет.
	int32 GetPendingTurnIn() const { return PendingTurnIn; }
	// [ИСПРАВЛЕНИЕ] попап 540 закрыт без сдачи → `this[10] = −1` (ключ `asda.Board.FixPendingReset`).
	void OnRewardPopupHidden();
	// ★ `netSendTsMaskedReq6593@0xEED4C0`: 6593 (questId, выбор) и `this[10] = −1`. Один запрос в полёте: false — задержан замком
	//   (ждущее сдачи и попап сохраняются, ревизия М-1); true — запрос отправлен (или провалилась сама отправка, как у эталона).
	bool SendTurnIn6593(int32 QuestId, int32 Choice);
	// Изменилось состояние доски (узлы/таблицы) — журнал, трекер и метки перерисовываются.
	DECLARE_MULTICAST_DELEGATE(FOnAsdaBoardChanged);
	FOnAsdaBoardChanged OnBoardChanged;
	// Имя задания (`rec+12`) на языке сессии (текстовая таблица `Raid_quest_data`, поле name).
	static FString QuestName(int32 QuestId);
	// Описание задания (`rec+132`) на языке сессии.
	static FString QuestDesc(int32 QuestId);
	// Имя зоны вкладки (`insDungeon_GetSlotRecord@0x997D10` = loc-строка `CMap_InitZoneList@0xB464C0`, 32 б).
	static FString ZoneName(const UGameInstance* GI, int32 Zone);
	// Удержание таблиц доски на сессию (закон dt_gc_hold); число удержанных.
	int32 HoldTables();

	// ═════════════════════ ДИАГНОСТИКА / САМОТЕСТ ═════════════════════
	// Подать пакет в очередь (тот же путь, что колбэк сети) и сразу разобрать.
	void DebugFeed(int32 Opcode, const TArray<uint8>& Body);
	// Состояние модели и окна строкой (`asda.Board.Dump`).
	FString DumpState() const;
	// Разобранные контролы окна с живой геометрией (`asda.Board.Controls`).
	FString DumpControls() const;
	// Корень окна (для кликов по живой геометрии самотеста).
	UUserWidget* DebugWidget() const { return Widget; }

private:
	// ── данные ──
	void EnsureData();                                 // `CPQuest_BuildUI`: таблицы (один раз, с удержанием)
	const FAsdaBoardNpcRow* FindNpcRecord(int32 NpcId) const;   // `questFindRecordById@0xEEB0C0`
	// Что есть на доске: готовое к сдаче / доступное сейчас (без скрытых; условия С-1). Общая точка миникарты и значка.
	void ScanBoard(int32 NpcId, bool& bOutReady, bool& bOutTake) const;
	const FAsdaRaidQuestRow* QuestAt(int32 Index) const;        // `questTable[k]` (по ИНДЕКСУ записи)
	FAsdaBoardNode* FindNodeMutable(int32 QuestId);
	int32 SelfLevel() const;                           // `CClass_GetField142C(&g_InventoryBag)->level`
	int32 BagCount(int32 ItemId) const;                // `CInventory_CountItemById`
	int32 JournalCount(int32 QuestId, int32 ItemId) const;   // `questRewardLookupCount@0xC65920`
	bool bDataLoaded = false;
	TArray<FAsdaRaidQuestRow> Quests;                  // [182] записи заданий по индексу k (RecIndex)
	TArray<FAsdaBoardNpcRow> Npcs;                     // [181] записи NPC-досок
	UPROPERTY(Transient) FAsdaHeldTables HeldTables;   // удержание DT (закон dt_gc_hold)

	// ── узлы [208] ──
	TArray<FAsdaBoardNode> Nodes;

	// ── окно ──
	bool EnsureWidget();                               // ленивое создание окна 75000
	void ResetWidget();                                // забыть виджеты (окно снесено)
	void SetupControls();                              // разбор ВСЕХ контролов (1:1 с `CPQuest_BuildUI`)
	void BuildAvailableLists(int32 NpcId);             // `questUiBuildAvailableLists@0xEEACE0`
	void RebuildTabs();                                // подписи/видимость вкладок 75002 (Type5 шаг 4)
	void PopulateList(int32 Tab);                      // `QuestUI_PopulateQuestList@0xEEB150`
	void PopulateDetail(int32 Row);                    // `ItemDetail_PopulateWithSowels@0xEEB990`
	void ApplyOkCaption();                             // 75281 ← 3575 (готово к сдаче) / 3597
	void PlaceSelectFrame(int32 Row);                  // рамка 75041 на позицию строки (y − 2)
	void SetHover(int32 Row);                          // подсветка 75211+i (−1 — снять все)
	void OnRowClick(int32 Row);                        // событие 11 по колонкам строки
	void OnTabSelected(int32 Tab);                     // событие 0x10501 по 75002
	void OnOkButton();                                 // 0x10401 по 75281 (§2.8)
	void SetRewardCell(FAsdaBoardRewardUi& C, int32 ItemId, int32 Count, bool bQtyOnIcon);   // иконка/подпись предмета
	void ClearRewardCell(FAsdaBoardRewardUi& C);
	void MsgBox(int32 LocId, int32 EtalonKey);         // ONEBUT `CItem_BuildTooltipText(g_pItemTooltipUI, 0, ключ, …)`
	void TickInput();                                  // покадровый опрос мыши (события 8/9/10/11)
	bool IsCursorOverRow(int32 Row) const;
	void RefreshIfOpen();                              // перерисовать список/панель открытого окна после смены узлов
	bool bOpen = false;
	bool bControlsReady = false;
	UPROPERTY(Transient) TObjectPtr<UUserWidget> Widget = nullptr;
	UPROPERTY(Transient) TArray<FAsdaBoardRowUi> RowUi;
	UPROPERTY(Transient) TArray<TObjectPtr<UImage>> SelFrame;      // 75041 `Subjugate_BB1_CLICK` — 8 планов рамки
	TArray<FVector2D> SelFrameBase;                                // их исходные оффсеты (рамка JUI 7,30)
	FVector2D SelFrameOrigin = FVector2D::ZeroVector;              // левый-верх рамки в разметке (минимум оффсетов)
	UPROPERTY(Transient) TArray<TObjectPtr<UImage>> TabPlanes;     // 75002 — планы `Subjugate_RADIO_*_0…4`
	UPROPERTY(Transient) TArray<TObjectPtr<UButton>> TabButtons;   // `Subjugate_RADIO_OPT0…4`
	UPROPERTY(Transient) TArray<TObjectPtr<UTextBlock>> TabTexts;  // `Subjugate_RADIO_Text`, `_Text2…5`
	TArray<FSlateBrush> TabIdleBrush;                              // кэш общей точки `AsdaUi::ApplyTabState`
	TArray<FButtonStyle> TabIdleStyle;
	TArray<FLinearColor> TabIdleCaption;
	UPROPERTY(Transient) TArray<TObjectPtr<UTextBlock>> HeadTexts; // 75060 Вид / 75100 Ур. / 75140 Задание / 75180 Состояние / 75300 Кол-во
	UPROPERTY(Transient) TObjectPtr<UTextBlock> DetailCaption = nullptr;   // 75241 «Подробности задания» 3590
	UPROPERTY(Transient) TObjectPtr<UTextBlock> DetailHead = nullptr;      // [158] 75242
	UPROPERTY(Transient) TObjectPtr<UTextBlock> DetailDesc = nullptr;      // [159] 75243
	UPROPERTY(Transient) TArray<TObjectPtr<UTextBlock>> DetailReq;         // [160…162] 75244…75246
	UPROPERTY(Transient) TObjectPtr<UTextBlock> RewardCaption = nullptr;   // [163] 75251 «<Награда…>» 3591
	UPROPERTY(Transient) TArray<FAsdaBoardRewardUi> RewardUi;              // [164…171] золото / опыт / предмет 1 / предмет 2
	UPROPERTY(Transient) TObjectPtr<UTextBlock> ChoiceCaption = nullptr;   // [172] 75260 «<Выбор награды…>» 3592
	UPROPERTY(Transient) TArray<FAsdaBoardRewardUi> ChoiceUi;              // [173…180] 4 награды на выбор
	UPROPERTY(Transient) TObjectPtr<UButton> BtnOk = nullptr;              // [14] 75281
	UPROPERTY(Transient) TObjectPtr<UTextBlock> BtnOkText = nullptr;
	UPROPERTY(Transient) TObjectPtr<UButton> BtnNo = nullptr;              // [15] 75282
	UPROPERTY(Transient) TObjectPtr<UTextBlock> BtnNoText = nullptr;
	UPROPERTY(Transient) TObjectPtr<UButton> BtnClose = nullptr;           // 75289 «×»
	UPROPERTY(Transient) TArray<TObjectPtr<UAsdaButtonRelay>> Relays;      // реле кнопок (общая точка `AsdaUi::BindButton`)
	int32 SelRow = -1;          // [1]
	int32 Tab = 0;              // [2]
	int32 HoverRow = -1;        // [3]
	int32 NpcId = -1;           // [4]
	int32 TabCount = 0;         // [5]
	int32 PendingTurnIn = -1;   // [10]
	int32 ShownTabCells = 3;    // число ячеек списка вкладок (`GetListItemCount`; в JUI 3, `uiListAppendClonedCell` — до 5)
	TArray<FAsdaBoardTab> Tabs; // [183+5n]
	bool bOkTipShown = false;   // подсказка 3598 кнопки 75281 показана
	FAsdaMouseFrame Mouse;      // покадровый опрос мыши

	// ── сеть ──
	struct FPkt { int32 Op = 0; TArray<uint8> Body; };
	TQueue<FPkt> PendingPkts;                          // очередь из колбэка сети (no-reentrant-net-callbacks)
	FDelegateHandle HBoard;                            // подписка на `UAsdaNetSubsystem::OnBoardPacket`
	FDelegateHandle HInvCell;                          // подписка на `UAsdaInventorySubsystem::OnCellChanged`
	bool bInvDirty = false;                            // сумка менялась (режим «Собрать» / окно)
	void DrainPackets();
	void Apply6588(const TArray<uint8>& B);            // `packetParseRecordList8@0xEEE190`
	void Apply6590(const TArray<uint8>& B);            // `buddyOnStatusUpdatePacket@0xEEDAF0`
	void Apply6594(const TArray<uint8>& B);            // `recv_ItemExchangeResult@0xEEE3D0`
	void Apply6595();                                  // `itemListClearReadyFlags@0xEEE300`
	void UpsertNode(const FAsdaBoardNode& N);
	void RemoveNode(int32 QuestId);                    // `containerRemoveEntryById`
	static FAsdaBoardNode ReadNode(const uint8* P);
	void TickCollectReady();                           // `mapObjBeginFirstReadyCraft@0xEEF010`
	void NotifyChanged();                              // OnBoardChanged + метки + перерисовка открытого окна
	// «Запрос в полёте» (замок P63, семья 2): второй 6591/6593 до ответа не шлём.
	double InFlightSince = 0.0;                        // время отправки (0 — свободно)
	int32 InFlightOp = 0;                              // 6591 / 6593
	bool IsInFlight() const;

	// ── самотест ──
	static constexpr int32 kDebugSentCap = 16;         // предел журнала отправок (ревизия М-4)
	TArray<TPair<int32, TArray<uint8>>> DebugSent;     // последние отправки доски (опкод, тело) — только вне Shipping, ≤ kDebugSentCap
	void NoteSent(int32 Op, const TArray<uint8>& Body);   // дописать в DebugSent (вне Shipping, с пределом)
	int32 DebugLastMsgBoxLoc = -1;                     // loc последнего ONEBUT доски
};
