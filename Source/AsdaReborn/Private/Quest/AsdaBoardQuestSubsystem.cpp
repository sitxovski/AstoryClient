// Copyright (c) 2026 @sitxovski (Discord)
// Реализация доски объявлений 75000 (ограниченные задания 20000…20056). Разбор с адресами — шапка
//   `Public/Quest/AsdaBoardQuestSubsystem.h` и `Docs/Port/systems/BOARD_QUEST_FLOW.md` (28.10.2026).
#include "Quest/AsdaBoardQuestSubsystem.h"
#include "AsdaPerf.h"   // ★ R4-c (14.11.2026): области замера времени игрового потока
#include "AsdaWeightWire.h"   // ★ 29.10.2026 (вес ×2): AsdaWeightFromWire — вес персонажа по проводу u16

#include "AsdaDevConsole.h"                // ASDA_DEV_CVAR / ASDA_DEV_CONSOLE
#include "AsdaNetSubsystem.h"              // 6591/6593, приём 6588…6595
#include "AsdaDataTableCache.h"            // AsdaDt::Cached
#include "AsdaBytes.h"                     // RdI16 / RdI32
#include "UI/AsdaUiHelpers.h"              // AsdaUi::*
#include "UI/AsdaJuiWindowHandle.h"        // каркас окна JUI
#include "UI/AsdaWindowSubsystem.h"        // реестр окон
#include "UI/AsdaNotice.h"                 // MsgBoxLoc / ChatColored / Plank / LocSlot
#include "UI/AsdaMessageSubsystem.h"       // EAsdaMsgCategory
#include "UI/AsdaInventorySubsystem.h"     // сумка, золото, записи 57 б
#include "UI/AsdaItemTooltipSubsystem.h"   // подсказка 3598 кнопки 75281
#include "UI/AsdaTipPanel.h"               // EAsdaTipOwner::BoardQuest
#include "Items/AsdaItemTableSubsystem.h"  // имена/записи предметов, самоцветов, спец-предметов
#include "Items/AsdaItemDbRows.h"          // FAsdaSowellRow / FAsdaSpecialItemlistRow / FAsdaSpecialitemTooltiplistRow
#include "Quest/AsdaQuestMarkerSubsystem.h"// слоты журнала (счётчики целей), уровень, метки
#include "Npc/AsdaNpcDialogSubsystem.h"    // попап наград 540 «режим доски»
#include "Resources/AsdaSoundSubsystem.h"  // звук 14 (6594 код 1)
#include "World/AsdaPlayerController.h"    // снять заявку разговора (Type5: `id5C = −1`)
#include "Login/AsdaText.h"                // FAsdaText::Get — тексты Raid_quest_data
#include "Login/AsdaButtonRelay.h"         // реле кнопок
#include "Text/AsdaTextSubsystem.h"        // FormatById — [sd1] нотиса 3600

#include "Blueprint/UserWidget.h"
#include "Blueprint/WidgetTree.h"
#include "Components/Image.h"
#include "Components/Button.h"
#include "Components/TextBlock.h"
#include "Components/PanelWidget.h"
#include "Components/CanvasPanelSlot.h"
#include "Engine/GameInstance.h"
#include "Engine/World.h"
#include "Engine/Engine.h"
#include "HAL/IConsoleManager.h"
#include "HAL/PlatformTime.h"
#include "Framework/Application/SlateApplication.h"

DEFINE_LOG_CATEGORY_STATIC(LogAsdaBoard, Log, All);

using AsdaBytes::RdI16;
using AsdaBytes::RdI32;

// ── Окно и таблицы ──
static const FName  kBoardWindowId(TEXT("Subjugate_WIN"));   // корень 75000 в `.JUI` / реестре окон
static const TCHAR* kBoardWbp = TEXT("/Game/AsdaAssets/UI/Player_UI/Quest/WBP_Subjugate_WIN.WBP_Subjugate_WIN_C");
static const TCHAR* kQuestDt  = TEXT("/Game/AsdaAssets/Data/Quest/DT_RaidQuestData.DT_RaidQuestData");   // Raid_quest_data.BIN
static const TCHAR* kNpcDt    = TEXT("/Game/AsdaAssets/Data/Quest/DT_BoardNpc.DT_BoardNpc");             // Raid_npc_data.BIN
static constexpr int32 kBoardZ = 731;   // полоса обычных окон (подъём при показе — общий `BringWindowToFront`)

// ── Loc-идентификаторы (по обработчикам эталона, §7 спецификации) ──
namespace BoardLoc
{
	constexpr int32 kStateNone    = 3573;   // состояние «нет» (оригинал — пусто)
	constexpr int32 kStateTaken   = 3574;   // «Выполняется»
	constexpr int32 kStateDone    = 3575;   // «Выполнено» (и подпись 75281 у готового к сдаче)
	constexpr int32 kStateToday   = 3576;   // «Заканчивается сегодня»
	constexpr int32 kInfinite     = 3577;   // колонка «Кол-во» у типа 0 — «∞»
	constexpr int32 kDetails      = 3590;   // 75241 «Подробности задания»
	constexpr int32 kRewardCap    = 3591;   // 75251 «<Награда за выполнение задания>»
	constexpr int32 kChoiceCap    = 3592;   // 75260 «<Выбор награды…>»
	constexpr int32 kKindBase     = 3593;   // 3593 + тип: Повторить / В день / Ограничение / Лимит
	constexpr int32 kTake         = 3597;   // 75281 «Взять задание»
	constexpr int32 kTakeTip      = 3598;   // подсказка 75281
	constexpr int32 kLeft         = 3600;   // «(Количество возможных повторений задания - [sd1])»
	constexpr int32 kOnce         = 3601;   // «(Задание можно выполнить только один раз.)»
	constexpr int32 kNoSel        = 3602;   // ОК без выбора
	constexpr int32 kLevel        = 3604;   // уровень вне диапазона
	constexpr int32 kCount        = 3630;   // 75300 «Кол-во»
	constexpr int32 kCollect      = 3631;   // «Собрать» (режим 1)
	constexpr int32 kClose        = 5035;   // 75282 «Закрыть»
	constexpr int32 kExp          = 6010;   // «Опыт»
	constexpr int32 kNoSlot       = 6176;   // «Нет свободных ячеек для предметов заданий.» (6590 код 8, 6594 код 3)
	constexpr int32 kRewardErr    = 6183;   // 6594 код 2
	constexpr int32 kKind         = 9564;   // 75060 «Вид»
	constexpr int32 kLvl          = 12215;  // 75100 «Ур.»
	constexpr int32 kQuest        = 101208; // 75140 «Задание»
	constexpr int32 kState        = 2434;   // 75180 «Состояние»
	constexpr int32 kGold         = 20198;  // «Получено [ss1] золота.»
	constexpr int32 kItemGot      = 20199;  // «Получена награда: [ss1].»
	// 6590 коды 2…8 → нотис (`buddyOnStatusUpdatePacket`, network.c:33348…33372).
	constexpr int32 kEnterFail[9] = { 0, 0, 3603, 3604, 3605, 3606, 3607, 3608, 6176 };
}
static constexpr int32 kGoldItem = 20551;          // иконка/имя золота (эталон берёт предмет 20551)
static const TCHAR* kExpIcon = TEXT("exp2");        // плоская текстура опыта
static constexpr uint32 kColRed    = 0xFFFF0000u;   // −65536
static constexpr uint32 kColWhite  = 0xFFFFFFFFu;   // −1
static constexpr uint32 kColGreen  = 0xFF00FF00u;   // −16711936
static constexpr uint32 kColYellow = 0xFFFFFF00u;   // −256

// ── Ключи (0 = поведение оригинала; обоснование — шапка .h) ──
static ASDA_DEV_CVAR(int32, CVarBoardEnabled, TEXT("asda.Board.Enabled"), 1,
	TEXT("Доска объявлений 75000: 1 — окно доски у NPC типа 5 (порт NPC_SpecialAction_Type5); 0 — прежнее поведение порта (отказ с логом)."),
	ECVF_Default);
static ASDA_DEV_CVAR(int32, CVarBoardFixMinimapLevel, TEXT("asda.Board.FixMinimapLevel"), 1,
	TEXT("Д-1: 1 — [ИСПРАВЛЕНИЕ ОРИГИНАЛА] метка доски на миникарте, если на доске есть готовое к сдаче или задание, которое можно взять сейчас ")
	TEXT("(lvMin ≤ ур. ≤ lvMax, не скрыто, узла нет или state 0 и лимит не исчерпан); ")
	TEXT("0 — как оригинал (craftCheckRecipeMaterialsAvailable: lvMin ≥ ур. && lvMax ≤ ур. — фактически только «готово к сдаче»)."),
	ECVF_Default);
static ASDA_DEV_CVAR(int32, CVarBoardFixCollectReady, TEXT("asda.Board.FixCollectReady"), 1,
	TEXT("Д-2: 1 — [ИСПРАВЛЕНИЕ ОРИГИНАЛА] покадровая готовность «Собрать»: в сумке ≥ требуемого; 0 — как оригинал (ровно ==)."),
	ECVF_Default);
static ASDA_DEV_CVAR(int32, CVarBoardFixStaleTabs, TEXT("asda.Board.FixStaleTabs"), 1,
	TEXT("Д-3: 1 — [ИСПРАВЛЕНИЕ ОРИГИНАЛА] видно ровно столько вкладок, сколько зон у доски; 0 — как оригинал (лишние ячейки остаются со старой подписью)."),
	ECVF_Default);
static ASDA_DEV_CVAR(int32, CVarBoardFixReqItems5, TEXT("asda.Board.FixReqItems5"), 1,
	TEXT("Д-4: 1 — [ИСПРАВЛЕНИЕ ОРИГИНАЛА] все 5 требуемых предметов в панели (4-й/5-й — вторым через «, » в строках 75244/75245); 0 — как оригинал (3)."),
	ECVF_Default);
static ASDA_DEV_CVAR(int32, CVarBoardFixDailyState, TEXT("asda.Board.FixDailyState"), 1,
	TEXT("Д-5: 1 — [ИСПРАВЛЕНИЕ ОРИГИНАЛА] 6595 у дневных снимает и состояние 3 «Заканчивается сегодня»; 0 — как оригинал (только счёт)."),
	ECVF_Default);
static ASDA_DEV_CVAR(int32, CVarBoardFixServerType, TEXT("asda.Board.FixServerType"), 1,
	TEXT("Д-6: 1 — [ИСПРАВЛЕНИЕ ОРИГИНАЛА] тип для 6590 и подсказки журнала из узла сервера, а после 6592/6593 узел заданий с лимитом (тип ≠ 0) ")
	TEXT("сохраняется; 0 — как оригинал (тип из таблицы клиента; 6593 у типов 2/3 узел удаляет)."),
	ECVF_Default);
static ASDA_DEV_CVAR(int32, CVarBoardFixPendingReset, TEXT("asda.Board.FixPendingReset"), 1,
	TEXT("1 — [ИСПРАВЛЕНИЕ ОРИГИНАЛА] попап 540 закрыт без сдачи → «ждущее сдачи задание» доски снимается (иначе следующая 567 у NPC ушла бы 6593); ")
	TEXT("0 — как оригинал (this[10] висит до 6593)."),
	ECVF_Default);
static ASDA_DEV_CVAR(int32, CVarBoardFixOverheadMark, TEXT("asda.Board.FixOverheadMark"), 1,
	TEXT("1 — [ИСПРАВЛЕНИЕ ОРИГИНАЛА] значок над NPC-доской: красный «!» — есть задание, готовое к сдаче, жёлтый «!» — есть задание, которое ")
	TEXT("можно взять сейчас (запрос владельца 28.10); 0 — как оригинал (у доски значка нет, `npcSlotLoadModelAndSetup` — только типы 0/4)."),
	ECVF_Default);
static ASDA_DEV_CVAR(float, CVarBoardInFlightSec, TEXT("asda.Board.InFlightTimeoutSec"), 4.f,
	TEXT("[ПОРТ] «запрос доски в полёте» (замок P63): второй 6591/6593 до ответа не шлётся; через столько секунд без ответа замок снимается (0 — без сторожа)."),
	ECVF_Default);

bool UAsdaBoardQuestSubsystem::IsEnabled() { return CVarBoardEnabled.GetValueOnGameThread() != 0; }

// ═════════════════════════════ ЖИЗНЕННЫЙ ЦИКЛ ═════════════════════════════════════════════════

void UAsdaBoardQuestSubsystem::Initialize(FSubsystemCollectionBase& Collection)
{
	Super::Initialize(Collection);
	Collection.InitializeDependency(UAsdaWindowSubsystem::StaticClass());
	Collection.InitializeDependency(UAsdaNetSubsystem::StaticClass());
	Collection.InitializeDependency(UAsdaInventorySubsystem::StaticClass());
	RowUi.SetNum(kRows);
	RewardUi.SetNum(4);
	ChoiceUi.SetNum(4);
	Tabs.SetNum(kTabs);
	UGameInstance* GI = GetGameInstance();
	if (UAsdaNetSubsystem* Net = GI ? GI->GetSubsystem<UAsdaNetSubsystem>() : nullptr)
	{
		// Закон `no-reentrant-net-callbacks`: из колбэка только КЛАДЁМ тело в очередь (разбор — в Tick).
		HBoard = Net->OnBoardPacket.AddLambda([this](int32 Op, const TArray<uint8>& B) { PendingPkts.Enqueue({ Op, B }); });
	}
	if (UAsdaInventorySubsystem* Inv = GI ? GI->GetSubsystem<UAsdaInventorySubsystem>() : nullptr)
	{
		HInvCell = Inv->OnCellChanged.AddLambda([this](int32, int32) { bInvDirty = true; });
	}
	UE_LOG(LogAsdaBoard, Log, TEXT("[доска] подсистема поднята"));
}

void UAsdaBoardQuestSubsystem::Deinitialize()
{
	UGameInstance* GI = GetGameInstance();
	if (UAsdaNetSubsystem* Net = GI ? GI->GetSubsystem<UAsdaNetSubsystem>() : nullptr) { Net->OnBoardPacket.Remove(HBoard); }
	if (UAsdaInventorySubsystem* Inv = GI ? GI->GetSubsystem<UAsdaInventorySubsystem>() : nullptr) { Inv->OnCellChanged.Remove(HInvCell); }
	ResetWidget();
	Super::Deinitialize();
}

// Сброс сессии.
//   • Окно — всегда: травел сносит виджеты, а эталон на смене карты делает `uiWindowCloseAndFreeList` (закрыть доску).
//   • Узлы — [ПОРТ] при смене карты НЕ стираются (законы session-reset-drops-early-data / session-reset-mapchanged-traps):
//     эталон их освобождает и ждёт 6588 новой карты, но у порта 6588 может прийти РАНЬШЕ сигнала смены карты (поток входа),
//     и стирание выбросило бы свежий список. Вместо стирания 6588 обновляет узлы по номеру задания (вставка/замена) — итог
//     тот же: на новой карте узлы = список сервера (сервер шлёт все узлы id > 0; убывают узлы только пакетом 6590, который
//     мы видим). Полный сброс (выход/разрыв/вход) — стирание.
void UAsdaBoardQuestSubsystem::ResetSessionState(EAsdaSessionResetReason Reason)
{
	if (bOpen)
	{
		bOpen = false;
		if (UAsdaWindowSubsystem* WM = GetGameInstance() ? GetGameInstance()->GetSubsystem<UAsdaWindowSubsystem>() : nullptr)
		{
			WM->SetWindowVisible(kBoardWindowId, false);
		}
	}
	SelRow = -1; HoverRow = -1; Tab = 0;
	Mouse.Reset();
	if (bOkTipShown)
	{
		bOkTipShown = false;
		if (UAsdaItemTooltipSubsystem* Tip = GetGameInstance() ? GetGameInstance()->GetSubsystem<UAsdaItemTooltipSubsystem>() : nullptr)
		{
			Tip->HideTooltipFor(EAsdaTipOwner::BoardQuest);
		}
	}
	InFlightSince = 0.0; InFlightOp = 0;   // ответ на запрос умершего соединения не придёт
	if (Reason == EAsdaSessionResetReason::MapChanged)
	{
		UE_LOG(LogAsdaBoard, Log, TEXT("[доска] смена карты: окно закрыто, узлов %d сохранено (6588 новой карты обновит их)"), Nodes.Num());
		return;
	}
	// LeftWorld / Disconnected / EnteringWorld: сессия персонажа кончилась или начинается новая (6588 приходит от игрового
	//   сервера ПОСЛЕ 1021 — входа, так что при EnteringWorld стирать безопасно; очередь — тоже прошлого персонажа).
	Nodes.Reset();
	PendingTurnIn = -1;
	{
		FPkt Drop;
		while (PendingPkts.Dequeue(Drop)) {}
	}
	UE_LOG(LogAsdaBoard, Log, TEXT("[доска] сброс сессии (%s): узлы и очередь очищены"), AsdaSessionResetReasonName(Reason));
}

void UAsdaBoardQuestSubsystem::Tick(float /*DeltaTime*/)
{
	ASDA_PERF_SCOPE("Tick.BoardQuest");   // ★ R4-c (14.11.2026): область замера (AsdaPerf.h; в Shipping пусто)
	DrainPackets();
	if (IsInFlight() && InFlightSince > 0.0)
	{
		const float Timeout = CVarBoardInFlightSec.GetValueOnGameThread();
		if (Timeout > 0.f && FPlatformTime::Seconds() - InFlightSince > Timeout)
		{
			UE_LOG(LogAsdaBoard, Warning, TEXT("[доска] ответ на %d не пришёл за %.1f с — замок «в полёте» снят"), InFlightOp, Timeout);
			InFlightSince = 0.0; InFlightOp = 0;
		}
	}
	TickCollectReady();
	if (bInvDirty)
	{
		bInvDirty = false;
		RefreshIfOpen();   // «есть» в панели и цвета готовности строк режима «Собрать» зависят от сумки
	}
	TickInput();
}

// ═════════════════════════════ ДАННЫЕ ═════════════════════════════════════════════════════════

int32 UAsdaBoardQuestSubsystem::HoldTables()
{
	const TCHAR* Paths[] = { kQuestDt, kNpcDt };
	return HeldTables.Hold(FName(TEXT("BoardQuest")), Paths);
}

// `CPQuest_BuildUI@0xEE8960` — таблицы грузятся один раз (`dataLoadRecordArray428` / `questLoadQuestTable`).
void UAsdaBoardQuestSubsystem::EnsureData()
{
	if (bDataLoaded) { return; }
	bDataLoaded = true;
	HoldTables();
	Quests.Reset();
	Npcs.Reset();
	if (UDataTable* Dt = AsdaDt::Cached(kQuestDt))
	{
		TArray<FAsdaRaidQuestRow*> All;
		Dt->GetAllRows<FAsdaRaidQuestRow>(TEXT("Board"), All);
		for (const FAsdaRaidQuestRow* R : All) { if (R) { Quests.Add(*R); } }
		Quests.Sort([](const FAsdaRaidQuestRow& A, const FAsdaRaidQuestRow& B) { return A.RecIndex < B.RecIndex; });
	}
	if (UDataTable* Dt = AsdaDt::Cached(kNpcDt))
	{
		TArray<FAsdaBoardNpcRow*> All;
		Dt->GetAllRows<FAsdaBoardNpcRow>(TEXT("Board"), All);
		for (const FAsdaBoardNpcRow* R : All) { if (R) { Npcs.Add(*R); } }
		Npcs.Sort([](const FAsdaBoardNpcRow& A, const FAsdaBoardNpcRow& B) { return A.RecIndex < B.RecIndex; });
	}
	// Записи без полей доски (старый ассет, RecIndex −1) — это признак непрогнанного генератора: говорим вслух.
	int32 Bad = 0;
	for (int32 k = 0; k < Quests.Num(); ++k) { if (Quests[k].RecIndex != k || Quests[k].QuestId != kQuestBase + k) { ++Bad; } }
	UE_LOG(LogAsdaBoard, Log, TEXT("[доска] таблицы: заданий %d, записей NPC %d%s"), Quests.Num(), Npcs.Num(),
		(Bad > 0 || Npcs.Num() == 0) ? TEXT(" — ★ НЕ СОШЛОСЬ: прогони Tools/asda_import_board_quests.py") : TEXT(""));
}

const FAsdaBoardNpcRow* UAsdaBoardQuestSubsystem::FindNpcRecord(int32 InNpcId) const
{
	for (const FAsdaBoardNpcRow& R : Npcs) { if (R.NpcId == InNpcId) { return &R; } }
	return nullptr;
}

const FAsdaRaidQuestRow* UAsdaBoardQuestSubsystem::QuestAt(int32 Index) const
{
	return Quests.IsValidIndex(Index) ? &Quests[Index] : nullptr;
}

const FAsdaRaidQuestRow* UAsdaBoardQuestSubsystem::FindQuest(int32 QuestId) const
{
	const_cast<UAsdaBoardQuestSubsystem*>(this)->EnsureData();
	for (const FAsdaRaidQuestRow& R : Quests) { if (R.QuestId == QuestId) { return &R; } }
	return nullptr;
}

int32 UAsdaBoardQuestSubsystem::FindQuestType(int32 QuestId) const
{
	const FAsdaRaidQuestRow* R = FindQuest(QuestId);
	return R ? R->PeriodType : -1;
}

int32 UAsdaBoardQuestSubsystem::EffectiveQuestType(int32 QuestId) const
{
	if (CVarBoardFixServerType.GetValueOnGameThread() != 0)
	{
		if (const FAsdaBoardNode* N = FindNode(QuestId)) { return N->Type; }
	}
	return FindQuestType(QuestId);
}

const FAsdaBoardNode* UAsdaBoardQuestSubsystem::FindNode(int32 QuestId) const
{
	for (const FAsdaBoardNode& N : Nodes) { if (N.QuestId == QuestId) { return &N; } }
	return nullptr;
}

FAsdaBoardNode* UAsdaBoardQuestSubsystem::FindNodeMutable(int32 QuestId)
{
	for (FAsdaBoardNode& N : Nodes) { if (N.QuestId == QuestId) { return &N; } }
	return nullptr;
}

int32 UAsdaBoardQuestSubsystem::SelfLevel() const
{
	const UAsdaQuestMarkerSubsystem* QM = GetGameInstance() ? GetGameInstance()->GetSubsystem<UAsdaQuestMarkerSubsystem>() : nullptr;
	return QM ? QM->GetSelfLevelForQuests() : 0;
}

int32 UAsdaBoardQuestSubsystem::BagCount(int32 ItemId) const
{
	const UAsdaInventorySubsystem* Inv = GetGameInstance() ? GetGameInstance()->GetSubsystem<UAsdaInventorySubsystem>() : nullptr;
	return Inv ? Inv->CountItemById(ItemId) : 0;
}

// `questRewardLookupCount@0xC65920`: счётчик цели `ItemId` у слота журнала с номером задания `QuestId` (+4 слота = i16 questNo).
int32 UAsdaBoardQuestSubsystem::JournalCount(int32 QuestId, int32 ItemId) const
{
	if (ItemId <= 0) { return 0; }
	const UAsdaQuestMarkerSubsystem* QM = GetGameInstance() ? GetGameInstance()->GetSubsystem<UAsdaQuestMarkerSubsystem>() : nullptr;
	if (!QM) { return 0; }
	for (const FAsdaQuestSlot& S : QM->GetSlots())
	{
		if (S.QuestNo != QuestId) { continue; }
		for (int32 j = 0; j < S.ObjIds.Num(); ++j)
		{
			if (S.ObjIds[j] == ItemId) { return S.ObjCounts.IsValidIndex(j) ? S.ObjCounts[j] : 0; }
		}
	}
	return 0;
}

// `craftCheckRequirementItemsMet@0xEEEBE0`: режим 1 — каждый непустой (код ≥ 0, кол-во > 0) предмет в сумке ≥ требуемого и их
//   хотя бы один; режим 0 — узел в состоянии 2; иной режим — нет.
bool UAsdaBoardQuestSubsystem::IsQuestReady(int32 QuestId) const
{
	const FAsdaRaidQuestRow* R = FindQuest(QuestId);
	if (!R) { return false; }
	if (R->Mode == 0)
	{
		const FAsdaBoardNode* N = FindNode(QuestId);
		return N && N->State == 2;
	}
	if (R->Mode != 1) { return false; }
	int32 Need = 0, Have = 0;
	for (int32 i = 0; i < 5; ++i)
	{
		const int32 Id = R->ReqItemIds.IsValidIndex(i) ? R->ReqItemIds[i] : -1;
		const int32 Cnt = R->ReqItemCounts.IsValidIndex(i) ? R->ReqItemCounts[i] : 0;
		if (Id < 0 || Cnt <= 0) { continue; }
		++Need;
		if (Cnt <= BagCount(Id)) { ++Have; }
	}
	return Need > 0 && Need == Have;
}

// Общая точка «что есть на доске»: готовое к сдаче и доступное к взятию СЕЙЧАС (С-1: уровень, не скрыто, узла нет или state 0 и
//   (тип 0 или счёт < max)). Скрытые задания не учитываются ни так, ни так (М-5).
void UAsdaBoardQuestSubsystem::ScanBoard(int32 InNpcId, bool& bOutReady, bool& bOutTake) const
{
	bOutReady = false; bOutTake = false;
	const_cast<UAsdaBoardQuestSubsystem*>(this)->EnsureData();
	const FAsdaBoardNpcRow* Rec = FindNpcRecord(InNpcId);
	if (!Rec) { return; }
	const int32 L = SelfLevel();
	for (int32 j = 0; j < Rec->QuestSlots.Num() && j < kNpcSlots; ++j)
	{
		const int32 Q = Rec->QuestSlots[j];
		const FAsdaRaidQuestRow* R = (Q >= 0) ? FindQuest(Q) : nullptr;
		if (!R || R->bHidden) { continue; }
		if (IsQuestReady(Q)) { bOutReady = true; continue; }
		if (R->LevelMin > L || L > R->LevelMax) { continue; }
		const FAsdaBoardNode* N = FindNode(Q);
		if (!N || (N->State == 0 && (R->PeriodType == 0 || N->Count < R->MaxCount))) { bOutTake = true; }
	}
}

bool UAsdaBoardQuestSubsystem::IsBoardNpc(int32 InNpcId) const
{
	const_cast<UAsdaBoardQuestSubsystem*>(this)->EnsureData();
	return FindNpcRecord(InNpcId) != nullptr;
}

int32 UAsdaBoardQuestSubsystem::OverheadMark(int32 InNpcId) const
{
	if (!IsEnabled() || CVarBoardFixOverheadMark.GetValueOnGameThread() == 0) { return 0; }
	bool bReady = false, bTake = false;
	ScanBoard(InNpcId, bReady, bTake);
	return bReady ? 2 : (bTake ? 1 : 0);   // красный главнее жёлтого
}

// `craftCheckRecipeMaterialsAvailable@0xEEEE70`: по каждому слоту доски (значение = номер задания): готово к сдаче — метка;
//   иначе условие уровня. ★ Д-1 — эталон сравнивает перевёрнуто (`lvMin >= ур. && lvMax <= ур.`).
int32 UAsdaBoardQuestSubsystem::MinimapMark(int32 InNpcId) const
{
	const_cast<UAsdaBoardQuestSubsystem*>(this)->EnsureData();
	const int32 L = SelfLevel();
	const bool bFix = CVarBoardFixMinimapLevel.GetValueOnGameThread() != 0;
	if (bFix)
	{
		bool bReady = false, bTake = false;
		ScanBoard(InNpcId, bReady, bTake);   // общая точка со значком над доской (С-1, М-5)
		return (bReady || bTake) ? 1 : 0;
	}
	int32 Mark = 0;
	for (const FAsdaBoardNpcRow& Rec : Npcs)
	{
		if (Rec.NpcId != InNpcId) { continue; }
		for (int32 j = 0; j < Rec.QuestSlots.Num() && j < kNpcSlots; ++j)
		{
			const int32 Q = Rec.QuestSlots[j];
			if (Q < 0) { continue; }
			const FAsdaRaidQuestRow* R = FindQuest(Q);
			// ★ Ревизия М-5: скрытое с доски задание метку не даёт НИ по готовности, ни по уровню — проверка раньше готовности.
			if (!R || R->bHidden) { continue; }
			if (IsQuestReady(Q)) { Mark = 1; continue; }
			if (!bFix)
			{
				if (R->LevelMin >= L && R->LevelMax <= L) { Mark = 1; }   // [ОРИГИНАЛ] Д-1 — перевёрнутое условие уровня
				continue;
			}
			// ★ [ИСПРАВЛЕНИЕ ОРИГИНАЛА] Д-1 (ревизия С-1): метка «есть что взять» — только если задание можно взять СЕЙЧАС:
			//   уровень в [lvMin, lvMax], узла нет либо он в состоянии 0 и (тип 0 «Повтор» или счёт < max) — те же условия, что
			//   сервер проверяет в 6591 (коды 3/5/7). Взятое (1), выполненное (2 — его ловит готовность выше) и исчерпанное (3,
			//   count ≥ max) метку не зажигают.
			if (R->LevelMin > L || L > R->LevelMax) { continue; }
			const FAsdaBoardNode* N = FindNode(Q);
			const bool bFree = !N || (N->State == 0 && (R->PeriodType == 0 || N->Count < R->MaxCount));
			if (bFree) { Mark = 1; }
		}
	}
	return Mark;
}

FString UAsdaBoardQuestSubsystem::QuestName(int32 QuestId)
{
	const FString T = FAsdaText::Get(TEXT("Raid_quest_data"), QuestId, TEXT("name"));
	return T.IsEmpty() ? FString::Printf(TEXT("#%d"), QuestId) : T;
}

FString UAsdaBoardQuestSubsystem::QuestDesc(int32 QuestId)
{
	FString T = FAsdaText::Get(TEXT("Raid_quest_data"), QuestId, TEXT("desc"));
	T.ReplaceInline(TEXT("\r\n"), TEXT("\n"));
	return T;
}

// `insDungeon_GetSlotRecord@0x997D10` → имя зоны из `CMap_InitZoneList@0xB464C0` (world_map.c:714): loc-строка по индексу.
FString UAsdaBoardQuestSubsystem::ZoneName(const UGameInstance* GI, int32 Zone)
{
	// Индекс зоны → loc-id имени (10, 11 — пусто: эталон их не заполняет).
	static const int32 kZoneLoc[24] = { 600, 601, 602, 603, 604, 605, 606, 707, 800, 801, -1, -1,
		804, 708, 805, 806, 807, 808, 809, 810, 811, 812, 813, 814 };
	if (Zone < 0 || Zone >= 24 || kZoneLoc[Zone] < 0) { return FString(); }
	return AsdaNotice::Loc(GI, kZoneLoc[Zone]).TrimStartAndEnd();
}

// ═════════════════════════════ ОКНО ═══════════════════════════════════════════════════════════

bool UAsdaBoardQuestSubsystem::IsBoardOpen(const UGameInstance* GI)
{
	const UAsdaBoardQuestSubsystem* S = GI ? GI->GetSubsystem<UAsdaBoardQuestSubsystem>() : nullptr;
	return S && S->bOpen;
}

bool UAsdaBoardQuestSubsystem::EnsureWidget()
{
	const EAsdaJuiEnsure R = FAsdaJuiWindowHandle(kBoardWindowId, kBoardWbp, kBoardZ).Ensure(GetGameInstance(), Widget, bControlsReady,
		[this](UAsdaWindowSubsystem&) { ResetWidget(); },
		[this](UAsdaWindowSubsystem& WM)
		{
			SetupControls();
			// Зона окна — девять планов рамки `Win_Mg` (`Subjugate_WIN_0_0…8`): мировой клик сквозь окно не проходит.
			FAsdaJuiWindowHandle::RegisterFrameZone(WM, kBoardWindowId, this, Widget, bOpen, TEXT("Subjugate_WIN_0_%d"), 9);
			// ESC: окна 75000 нет в эталонной таблице `kEscCloseWindows` (CPQuest_BuildUI признак +0x228 не ставит), оригинал его по ESC не закрывает.
			//   ★ 07.11.2026 [ИСПРАВЛЕНИЕ]: закрывает список порта `kEscExtraWindows` (крестик 75289). Прочие входы — 75282, клик по земле, смена карты.
		},
		[]()
		{
			UE_LOG(LogAsdaBoard, Error, TEXT("[доска] WBP_Subjugate_WIN не загрузился (%s)"), kBoardWbp);
		});
	if (R == EAsdaJuiEnsure::Created) { bOpen = false; }
	return FAsdaJuiWindowHandle::Succeeded(R) && bControlsReady;
}

void UAsdaBoardQuestSubsystem::ResetWidget()
{
	if (UAsdaWindowSubsystem* WM = GetGameInstance() ? GetGameInstance()->GetSubsystem<UAsdaWindowSubsystem>() : nullptr)
	{
		WM->UnregisterUiZone(kBoardWindowId);
		WM->CloseWindow(kBoardWindowId);
	}
	Widget = nullptr;
	for (FAsdaBoardRowUi& U : RowUi) { U = FAsdaBoardRowUi(); }
	for (FAsdaBoardRewardUi& U : RewardUi) { U = FAsdaBoardRewardUi(); }
	for (FAsdaBoardRewardUi& U : ChoiceUi) { U = FAsdaBoardRewardUi(); }
	SelFrame.Reset(); SelFrameBase.Reset();
	TabPlanes.Reset(); TabButtons.Reset(); TabTexts.Reset();
	TabIdleBrush.Reset(); TabIdleStyle.Reset(); TabIdleCaption.Reset();
	HeadTexts.Reset(); DetailReq.Reset();
	DetailCaption = nullptr; DetailHead = nullptr; DetailDesc = nullptr; RewardCaption = nullptr; ChoiceCaption = nullptr;
	BtnOk = nullptr; BtnOkText = nullptr; BtnNo = nullptr; BtnNoText = nullptr; BtnClose = nullptr;
	Relays.Reset();
	bControlsReady = false;
	bOpen = false;
	ShownTabCells = 3;
	Mouse.Reset();
}

// Разбор ВСЕХ контролов окна — 1:1 с `CPQuest_BuildUI@0xEE8960` (quest.c:9066-9257). Имена — живой дамп
//   `Reports/board_quest/subjugate_tree.txt` (импортёр JUI); узлы колонок строк 2…20 и количеств 75256/75258 — шаг ассета
//   `Tools/asda_ui_subjugate_rows.py` (у этих контролов в `.JUI` пустой `ID` и пустой `TextContent` — импортёр узла не строил).
void UAsdaBoardQuestSubsystem::SetupControls()
{
	bControlsReady = false;
	if (!Widget || !Widget->WidgetTree) { return; }
	auto Any = [this](const FString& Name) -> UWidget* { return Widget->GetWidgetFromName(FName(*Name)); };
	auto Txt = [&Any](const FString& Name) { return Cast<UTextBlock>(Any(Name)); };
	auto Btn = [&Any](const FString& Name) { return Cast<UButton>(Any(Name)); };
	auto ImgByPrefix = [this](const FString& Prefix) { return Cast<UImage>(AsdaUi::FindNodeByPrefix(Widget, *Prefix)); };
	// Ребёнок группы `Parent`, чьё имя начинается с `Prefix` и который `Cls`.
	auto ChildOf = [](const UWidget* Parent, const TCHAR* Prefix, UClass* Cls) -> UWidget*
	{
		const UPanelWidget* P = Cast<UPanelWidget>(Parent);
		if (!P) { return nullptr; }
		for (int32 c = 0; c < P->GetChildrenCount(); ++c)
		{
			UWidget* W = P->GetChildAt(c);
			if (W && W->IsA(Cls) && W->GetName().StartsWith(Prefix)) { return W; }
		}
		return nullptr;
	};

	// ── 20 строк [18+7i] ──
	RowUi.SetNum(kRows);
	for (int32 i = 0; i < kRows; ++i)
	{
		FAsdaBoardRowUi& U = RowUi[i];
		const FString P = FString::Printf(TEXT("Subjugate_BB1_%d"), i + 1);
		U.Main = Any(P + TEXT("_grp"));
		U.MainText = Any(P);
		U.Kind  = Txt(P + TEXT("_TXT00"));   // 75061+i «Вид»
		U.Level = Txt(P + TEXT("_TXT01"));   // 75101+i «Ур.»
		U.Name  = Txt(P + TEXT("_TXT02"));   // 75141+i «Задание»
		U.State = Txt(P + TEXT("_TXT03"));   // 75181+i «Состояние»
		U.Count = Txt(P + TEXT("_TXT04"));   // 75301+i «Кол-во»
		U.Hover = ChildOf(U.Main, TEXT("Box_BlueOt20_"), UImage::StaticClass());   // 75211+i
		U.Hits.Reset();
		if (const UPanelWidget* G = Cast<UPanelWidget>(U.Main))
		{
			// Зоны мыши колонок: `*_HIT` группы, кроме зоны самого 5×5 main (`Subjugate_BB1_n_*_HIT`).
			for (int32 c = 0; c < G->GetChildrenCount(); ++c)
			{
				UWidget* W = G->GetChildAt(c);
				const FString N = W ? W->GetName() : FString();
				if (W && N.EndsWith(TEXT("_HIT")) && !N.StartsWith(TEXT("Subjugate_BB1_"))) { U.Hits.Add(W); }
			}
		}
	}
	// ── шапка колонок (подписи ставит код) ──
	HeadTexts.Reset();
	for (int32 c = 0; c < 5; ++c) { HeadTexts.Add(Txt(FString::Printf(TEXT("Subjugate_BB1_0_TXT%02d"), c))); }
	// ── рамка выбора 75041 (8 планов `Box_White3Stroke`) и планы вкладок 75002 ──
	SelFrame.Reset(); SelFrameBase.Reset();
	TabPlanes.SetNum(kTabs); TabButtons.SetNum(kTabs); TabTexts.SetNum(kTabs);
	{
		TArray<UWidget*> All;
		Widget->WidgetTree->GetAllWidgets(All);
		TArray<TPair<int32, UImage*>> Frame;
		for (UWidget* W : All)
		{
			FString Idc; int32 Jui = 0, Plane = 0;
			if (!W || !AsdaUi::ParsePlaneName(W->GetName(), Idc, Jui, Plane)) { continue; }
			UImage* Img = Cast<UImage>(W);
			if (!Img) { continue; }
			if (Idc == TEXT("Subjugate_BB1_CLICK")) { Frame.Add({ Plane, Img }); continue; }
			if (Idc == TEXT("Subjugate_RADIO") && Plane >= 0 && Plane < kTabs) { TabPlanes[Plane] = Img; continue; }
		}
		Frame.Sort([](const TPair<int32, UImage*>& A, const TPair<int32, UImage*>& B) { return A.Key < B.Key; });
		SelFrameOrigin = FVector2D(TNumericLimits<double>::Max(), TNumericLimits<double>::Max());
		for (const TPair<int32, UImage*>& F : Frame)
		{
			SelFrame.Add(F.Value);
			const UCanvasPanelSlot* S = AsdaUi::SlotOf(F.Value);
			const FVector2D Off = S ? FVector2D(S->GetOffsets().Left, S->GetOffsets().Top) : FVector2D::ZeroVector;
			SelFrameBase.Add(Off);
			SelFrameOrigin = FVector2D(FMath::Min(SelFrameOrigin.X, Off.X), FMath::Min(SelFrameOrigin.Y, Off.Y));
		}
	}
	for (int32 n = 0; n < kTabs; ++n)
	{
		TabButtons[n] = Btn(FString::Printf(TEXT("Subjugate_RADIO_OPT%d"), n));
		TabTexts[n] = Txt(n == 0 ? FString(TEXT("Subjugate_RADIO_Text")) : FString::Printf(TEXT("Subjugate_RADIO_Text%d"), n + 1));
	}
	// ── правая панель [158…180] ──
	DetailCaption = Txt(TEXT("Subjugate_R_T1"));    // 75241
	DetailHead = Txt(TEXT("Subjugate_R_T2"));       // 75242
	DetailDesc = Txt(TEXT("Subjugate_R_T3"));       // 75243
	DetailReq.Reset();
	DetailReq.Add(Txt(TEXT("Subjugate_R_T4")));     // 75244
	DetailReq.Add(Txt(TEXT("Subjugate_R_T9_1")));   // 75245 (ID `Subjugate_R_T9` в `.JUI` повторён — импортёр дал `_1`)
	DetailReq.Add(Txt(TEXT("Subjugate_R_T10_1")));  // 75246
	RewardCaption = Txt(TEXT("Subjugate_R_T5"));    // 75251
	ChoiceCaption = Txt(TEXT("Subjugate_R_T8"));    // 75260
	RewardUi.SetNum(4); ChoiceUi.SetNum(4);
	// 75252/75253 золото, 75254/75255 опыт: иконка `Subjugate_R_M1_*` + текст ячейки `Subjugate_R_M1` + подпись `Subjugate_R_T6`.
	RewardUi[0].Icon = ImgByPrefix(TEXT("Subjugate_R_M1_")); RewardUi[0].Qty = Txt(TEXT("Subjugate_R_M1")); RewardUi[0].Label = Txt(TEXT("Subjugate_R_T6"));
	RewardUi[1].Icon = ImgByPrefix(TEXT("Subjugate_R_M2_")); RewardUi[1].Qty = Txt(TEXT("Subjugate_R_M2")); RewardUi[1].Label = Txt(TEXT("Subjugate_R_T7"));
	// 75256…75259 два предмета: подпись `Subjugate_R_T5_TXT00/01` (импортёр, класс S1a), иконка — `NoImage_icon_*` той же рамки,
	//   количество — узел шага ассета `Subjugate_R_T5_QTY00/01`.
	for (int32 n = 0; n < 2; ++n)
	{
		FAsdaBoardRewardUi& C = RewardUi[2 + n];
		C.Label = Txt(FString::Printf(TEXT("Subjugate_R_T5_TXT%02d"), n));
		C.Icon = C.Label ? Cast<UImage>(ChildOf(C.Label->GetParent(), TEXT("NoImage_icon_"), UImage::StaticClass())) : nullptr;
		C.Qty = Txt(FString::Printf(TEXT("Subjugate_R_T5_QTY%02d"), n));
	}
	// 75261…75268 четыре награды на выбор: иконка `Subjugate_R_M3…M6_*`, текст ячейки `Subjugate_R_M3…M6`, подпись `Subjugate_R_T9…T12`.
	for (int32 k = 0; k < 4; ++k)
	{
		FAsdaBoardRewardUi& C = ChoiceUi[k];
		C.Icon = ImgByPrefix(FString::Printf(TEXT("Subjugate_R_M%d_"), 3 + k));
		C.Qty = Txt(FString::Printf(TEXT("Subjugate_R_M%d"), 3 + k));
		C.Label = Txt(FString::Printf(TEXT("Subjugate_R_T%d"), 9 + k));
	}
	// ── кнопки ──
	BtnOk = Btn(TEXT("Subjugate_OKbtn"));        BtnOkText = Txt(TEXT("Subjugate_OKbtn_Text"));   // 75281
	BtnNo = Btn(TEXT("Subjugate_Xbtn"));         BtnNoText = Txt(TEXT("Subjugate_Xbtn_Text"));    // 75282
	BtnClose = Btn(TEXT("Subjugate_xxbtn"));                                                      // 75289
	auto Bind = [this](UButton* B, int32 IdNum, TFunction<void(int32)>&& Fn)
	{
		if (B) { AsdaUi::BindButton(this, B, IdNum, MoveTemp(Fn), Relays); }
	};
	Bind(BtnOk, 75281, [this](int32) { OnOkButton(); });               // 0x10401 75281 — §2.8
	Bind(BtnNo, 75282, [this](int32) { Close(); });                    // 0x10401 75282 → `questUiReset`
	Bind(BtnClose, 75289, [this](int32) { Close(); });                 // 0x10401 75289 → `questUiReset`
	for (int32 n = 0; n < kTabs; ++n)
	{
		Bind(TabButtons[n], 75002, [this, n](int32) { OnTabSelected(n); });   // 0x10501 по 75002, a6 = n
	}

	// ── статичные подписи `CPQuest_BuildUI` ──
	auto Set = [this](UTextBlock* T, int32 Loc) { if (T) { T->SetText(FText::FromString(AsdaNotice::Loc(GetGameInstance(), Loc))); } };
	Set(BtnOkText, BoardLoc::kTake);          // 75281 ← 3597 (`CWidget_SetTextAll4`)
	Set(BtnNoText, BoardLoc::kClose);         // 75282 ← 5035
	if (HeadTexts.Num() == 5)
	{
		Set(HeadTexts[0], BoardLoc::kKind);   // 75060 ← 9564
		Set(HeadTexts[1], BoardLoc::kLvl);    // 75100 ← 12215
		Set(HeadTexts[2], BoardLoc::kQuest);  // 75140 ← 101208
		Set(HeadTexts[3], BoardLoc::kState);  // 75180 ← 2434
		Set(HeadTexts[4], BoardLoc::kCount);  // 75300 ← 3630
	}
	Set(DetailCaption, BoardLoc::kDetails);   // 75241 ← 3590
	Set(RewardCaption, BoardLoc::kRewardCap); // 75251 ← 3591
	Set(ChoiceCaption, BoardLoc::kChoiceCap); // 75260 ← 3592
	if (DetailHead) { DetailHead->SetText(FText::GetEmpty()); }   // 75242 ← пусто
	if (DetailDesc) { DetailDesc->SetText(FText::GetEmpty()); }   // 75243 ← пусто
	for (UTextBlock* T : DetailReq) { if (T) { T->SetText(FText::GetEmpty()); } }   // 75244…75246 ← пусто
	for (FAsdaBoardRewardUi& C : RewardUi) { ClearRewardCell(C); }
	for (FAsdaBoardRewardUi& C : ChoiceUi) { ClearRewardCell(C); }
	// Строки скрыты; рамка выбора скрыта; вкладкам 0…2 — пустые строки.
	for (FAsdaBoardRowUi& U : RowUi) { AsdaUi::SetShownDecor(U.Main, false); AsdaUi::SetShownDecor(U.Hover, false); }
	for (UImage* F : SelFrame) { AsdaUi::SetShownDecor(F, false); }
	for (int32 n = 0; n < kTabs; ++n) { if (TabTexts[n]) { TabTexts[n]->SetText(FText::GetEmpty()); } }
	ShownTabCells = 3;

	int32 Rows = 0, Tabs5 = 0, Rew = 0;
	for (const FAsdaBoardRowUi& U : RowUi) { Rows += (U.Main && U.MainText && U.Kind && U.Level && U.Name && U.State && U.Count && U.Hover && U.Hits.Num() == 5) ? 1 : 0; }
	for (int32 n = 0; n < kTabs; ++n) { Tabs5 += (TabPlanes[n] && TabButtons[n] && TabTexts[n]) ? 1 : 0; }
	for (const FAsdaBoardRewardUi& C : RewardUi) { Rew += (C.Icon && C.Qty && C.Label) ? 1 : 0; }
	for (const FAsdaBoardRewardUi& C : ChoiceUi) { Rew += (C.Icon && C.Qty && C.Label) ? 1 : 0; }
	int32 Req = 0; for (UTextBlock* T : DetailReq) { Req += T ? 1 : 0; }
	bControlsReady = (Rows == kRows && Tabs5 == kTabs && Rew == 8 && Req == 3 && SelFrame.Num() == 8 && BtnOk && BtnNo && BtnClose
		&& DetailHead && DetailDesc);
	UE_LOG(LogAsdaBoard, Log,
		TEXT("[доска] окно 75000 разобрано: строк %d/20, вкладок %d/5, ячеек наград %d/8, требований %d/3, рамка %d/8, кнопки %d/%d/%d → %s%s"),
		Rows, Tabs5, Rew, Req, SelFrame.Num(), BtnOk ? 1 : 0, BtnNo ? 1 : 0, BtnClose ? 1 : 0,
		bControlsReady ? TEXT("ГОТОВО") : TEXT("НЕ ГОТОВО"),
		(Rows < kRows || Rew < 8) ? TEXT(" — ★ узлов шага ассета нет: прогони Tools/asda_ui_subjugate_rows.py") : TEXT(""));
}

// ═════════════════════════════ ОТКРЫТИЕ / ЗАКРЫТИЕ ════════════════════════════════════════════

// `NPC_SpecialAction_Type5@0xEE95B0` (npc_mob_pet.c:7891).
bool UAsdaBoardQuestSubsystem::OpenForNpc(int32 InNpcId)
{
	if (InNpcId < 0) { return false; }
	EnsureData();
	if (!EnsureWidget()) { return false; }
	BuildAvailableLists(InNpcId);   // нет записи NPC — эталон всё равно показывает окно с прежним содержимым (§2.4)
	// Снять цель/заявку разговора (`g_nTargetUserIdx = NaN`, `localPlayer[1].id5C = −1`).
	if (UWorld* W = GetGameInstance() ? GetGameInstance()->GetWorld() : nullptr)
	{
		if (AAsdaPlayerController* PC = Cast<AAsdaPlayerController>(W->GetFirstPlayerController()))
		{
			PC->ClearPendingNpcTalk(TEXT("открыта доска объявлений (Type5)"));
		}
	}
	RebuildTabs();
	Tab = 0;                          // `CControl_SetSelectedIndex(75002, 0); this[2] = 0`
	PopulateList(Tab);
	PopulateDetail(HoverRow);         // эталон зовёт панель с `this[3]` (под курсором; на закрытом окне −1 — очистка)
	SelRow = -1;                      // [ПОРТ] у эталона [1] уже −1 после `questUiReset` прошлого закрытия
	if (BtnOkText) { BtnOkText->SetText(FText::FromString(AsdaNotice::Loc(GetGameInstance(), BoardLoc::kTake))); }   // 75281 ← 3597
	for (UImage* F : SelFrame) { AsdaUi::SetShownDecor(F, false); }
	if (UAsdaWindowSubsystem* WM = GetGameInstance()->GetSubsystem<UAsdaWindowSubsystem>())
	{
		WM->SetWindowVisible(kBoardWindowId, true);
		WM->BringWindowToFront(kBoardWindowId);   // `CClass_MapInsert(g_pWindowMgr, 75000)`
	}
	bOpen = true;
	Mouse.Reset();
	UE_LOG(LogAsdaBoard, Log, TEXT("[доска] открыта: npcId=%d, вкладок %d, уровень %d"), InNpcId, TabCount, SelfLevel());
	return true;
}

// `questUiReset@0xEE9790`.
void UAsdaBoardQuestSubsystem::Close()
{
	HoverRow = -1;          // this[3]
	SelRow = -1;            // this[1]
	NpcId = -1;             // this[4] (IDA зовёт поле activeSlot — по смещению это номер NPC)
	for (UImage* F : SelFrame) { AsdaUi::SetShownDecor(F, false); }   // рамка 75041
	SetHover(-1);
	if (bOkTipShown)
	{
		bOkTipShown = false;
		if (UAsdaItemTooltipSubsystem* Tip = GetGameInstance() ? GetGameInstance()->GetSubsystem<UAsdaItemTooltipSubsystem>() : nullptr)
		{
			Tip->HideTooltipFor(EAsdaTipOwner::BoardQuest);
		}
	}
	if (bOpen)
	{
		bOpen = false;
		if (UAsdaWindowSubsystem* WM = GetGameInstance() ? GetGameInstance()->GetSubsystem<UAsdaWindowSubsystem>() : nullptr)
		{
			WM->SetWindowVisible(kBoardWindowId, false);
		}
		UE_LOG(LogAsdaBoard, Log, TEXT("[доска] закрыта"));
	}
	Mouse.Reset();
}

// `questUiBuildAvailableLists@0xEEACE0` (quest.c:9725).
void UAsdaBoardQuestSubsystem::BuildAvailableLists(int32 InNpcId)
{
	if (Quests.Num() == 0) { return; }           // `if (!this[181]) return 0`
	NpcId = InNpcId;                             // `v16[4] = a2`
	const FAsdaBoardNpcRow* Rec = FindNpcRecord(InNpcId);
	if (!Rec)
	{
		UE_LOG(LogAsdaBoard, Warning, TEXT("[доска] npcId=%d нет в DT_BoardNpc — список не перестроен (эталон показывает прежний)"), InNpcId);
		return;
	}
	for (FAsdaBoardTab& T : Tabs) { T.Zone = -1; T.Indices.Reset(); }
	int32 K = 0;
	for (int32 j = 0; j < 5; ++j)
	{
		const int32 Z = Rec->Zones.IsValidIndex(j) ? Rec->Zones[j] : -1;
		if (Z >= 0 && K < kTabs) { Tabs[K++].Zone = Z; }
	}
	TabCount = K;                                // `v16[5] = v14`
	// Кандидаты: ИНДЕКСЫ k, у которых слот ≥ 0; запись берётся как `questTable[k]` (а не по номеру из слота).
	TArray<int32> Cand;
	for (int32 k = 0; k < kNpcSlots && k < Rec->QuestSlots.Num(); ++k) { if (Rec->QuestSlots[k] >= 0) { Cand.Add(k); } }
	const int32 L = SelfLevel();
	for (int32 m = 0; m < Cand.Num(); ++m)
	{
		const int32 Idx = Cand[m];
		const FAsdaRaidQuestRow* R = QuestAt(Idx);
		if (!R) { continue; }
		if (R->bHidden) { continue; }   // [ПОРТ] скрыто строкой данных (`bHidden`, ведомость board_quest_hidden.json)
		for (int32 n = 0; n < TabCount; ++n)
		{
			if (R->Zone != Tabs[n].Zone) { continue; }
			if (R->LevelMax >= L) { Tabs[n].Indices.Add(Idx); continue; }   // показывается всегда (и выше уровня — красным)
			// Перерос: только если есть узел и (тип ≤ 1 или счёт < max) и (state ≠ 3 или режим «Собрать»).
			const FAsdaBoardNode* N = FindNode(R->QuestId);
			if (N && (R->PeriodType <= 1 || N->Count < R->MaxCount) && (N->State != 3 || R->Mode == 1)) { Tabs[n].Indices.Add(Idx); }
		}
	}
	UE_LOG(LogAsdaBoard, Log, TEXT("[доска] фильтр npcId=%d: зон %d, кандидатов %d, по вкладкам %d/%d/%d/%d/%d (уровень %d)"),
		InNpcId, TabCount, Cand.Num(), Tabs[0].Indices.Num(), Tabs[1].Indices.Num(), Tabs[2].Indices.Num(), Tabs[3].Indices.Num(),
		Tabs[4].Indices.Num(), L);
}

// Вкладки 75002 (Type5 шаг 4): ячеек меньше числа зон — клонировать (`uiListAppendClonedCell`), подписи ← имя зоны.
//   ★ Д-3: лишние ячейки эталон не удаляет — со старыми подписями. Исправление — видно ровно TabCount.
void UAsdaBoardQuestSubsystem::RebuildTabs()
{
	if (ShownTabCells < TabCount) { ShownTabCells = FMath::Min(TabCount, kTabs); }
	const bool bFix = CVarBoardFixStaleTabs.GetValueOnGameThread() != 0;
	const int32 Visible = bFix ? TabCount : ShownTabCells;
	for (int32 n = 0; n < kTabs; ++n)
	{
		const bool bShow = n < Visible;
		if (TabPlanes.IsValidIndex(n)) { AsdaUi::SetShownDecor(TabPlanes[n], bShow); }
		if (TabButtons.IsValidIndex(n)) { AsdaUi::SetShown(TabButtons[n], bShow); }
		if (TabTexts.IsValidIndex(n) && TabTexts[n])
		{
			AsdaUi::SetShownDecor(TabTexts[n], bShow);
			if (n < TabCount) { TabTexts[n]->SetText(FText::FromString(ZoneName(GetGameInstance(), Tabs[n].Zone))); }
		}
	}
	if (TabPlanes.Num() == kTabs && TabButtons.Num() == kTabs && TabTexts.Num() == kTabs)
	{
		AsdaUi::ApplyTabState(TabPlanes, TabTexts, TabButtons, Tab, TabIdleBrush, TabIdleStyle, TabIdleCaption);
	}
}

// `QuestUI_PopulateQuestList@0xEEB150` (quest.c:9814).
void UAsdaBoardQuestSubsystem::PopulateList(int32 InTab)
{
	for (FAsdaBoardRowUi& U : RowUi)
	{
		if (U.Name)  { U.Name->SetText(FText::GetEmpty()); }
		if (U.Level) { U.Level->SetText(FText::GetEmpty()); }
		if (U.Kind)  { U.Kind->SetText(FText::GetEmpty()); }
		if (U.State) { U.State->SetText(FText::GetEmpty()); }
		AsdaUi::SetShownDecor(U.Main, false);
	}
	if (!Tabs.IsValidIndex(InTab)) { return; }
	const UGameInstance* GI = GetGameInstance();
	const int32 L = SelfLevel();
	const TArray<int32>& Idx = Tabs[InTab].Indices;
	for (int32 j = 0; j < Idx.Num() && j < kRows; ++j)
	{
		const FAsdaRaidQuestRow* R = QuestAt(Idx[j]);
		FAsdaBoardRowUi& U = RowUi[j];
		if (!R) { continue; }
		const int32 Diff = R->LevelMin - L;
		uint32 Col = (Diff > -4) ? ((Diff > 0) ? kColRed : kColWhite) : kColGreen;
		FString State;
		FString Cnt;
		const FAsdaBoardNode* N = FindNode(R->QuestId);
		if (N)
		{
			if ((IsQuestReady(R->QuestId) && N->State > 0) || N->State == 2)
			{
				State = AsdaNotice::Loc(GI, BoardLoc::kStateDone);
				Col = kColYellow;
			}
			else if (N->State == 1) { State = AsdaNotice::Loc(GI, BoardLoc::kStateTaken); }
			else if (N->State == 3) { State = AsdaNotice::Loc(GI, BoardLoc::kStateToday); Col = kColRed; }
			else { State = AsdaNotice::Loc(GI, BoardLoc::kStateNone); }
			Cnt = (R->PeriodType != 0) ? FString::Printf(TEXT("%d / %d"), N->Count, R->MaxCount) : AsdaNotice::Loc(GI, BoardLoc::kInfinite);
		}
		else
		{
			Cnt = (R->PeriodType != 0) ? FString::Printf(TEXT("%d / %d"), 0, R->MaxCount) : AsdaNotice::Loc(GI, BoardLoc::kInfinite);
			State = AsdaNotice::Loc(GI, BoardLoc::kStateNone);
		}
		const FLinearColor Lc = AsdaUi::SrgbU32ToLinear(Col);
		auto Put = [&Lc](UTextBlock* T, const FString& S)
		{
			if (!T) { return; }
			T->SetText(FText::FromString(S));
			T->SetColorAndOpacity(FSlateColor(Lc));
		};
		const int32 KindLoc = (R->Mode == 1) ? BoardLoc::kCollect : (BoardLoc::kKindBase + R->PeriodType);
		Put(U.Count, Cnt.TrimEnd());
		Put(U.Name, QuestName(R->QuestId));
		Put(U.Level, FString::Printf(TEXT("%d/%d"), R->LevelMin, R->LevelMax));
		Put(U.Kind, FString::Printf(TEXT("<%s>"), *AsdaNotice::Loc(GI, KindLoc).TrimStartAndEnd()));
		Put(U.State, State.TrimEnd());
		// `sub_91F036(·, 13)` у Ур./Вид/Состояние — по центру по обеим осям; та же раскладка лежит в ассете (маска `.JUI` 5).
		AsdaUi::SetShownDecor(U.Main, true);
	}
}

// `ItemDetail_PopulateWithSowels@0xEEB990` (item_inventory.c:65971).
void UAsdaBoardQuestSubsystem::PopulateDetail(int32 Row)
{
	const UGameInstance* GI = GetGameInstance();
	const int32 ListSize = Tabs.IsValidIndex(Tab) ? Tabs[Tab].Indices.Num() : 0;
	if (ListSize <= Row) { return; }   // `if (listSize <= listIndex) return 0` — без очистки
	if (DetailHead) { DetailHead->SetText(FText::GetEmpty()); }
	if (DetailDesc) { DetailDesc->SetText(FText::GetEmpty()); }
	for (UTextBlock* T : DetailReq) { if (T) { T->SetText(FText::GetEmpty()); } }
	for (FAsdaBoardRewardUi& C : RewardUi) { ClearRewardCell(C); }
	for (FAsdaBoardRewardUi& C : ChoiceUi) { ClearRewardCell(C); }
	if (Row < 0) { return; }           // −1 — только очистка (`this[8] = −1; this[9] = 0`)
	const FAsdaRaidQuestRow* R = QuestAt(Tabs[Tab].Indices[Row]);
	if (!R) { return; }
	const int32 KindLoc = (R->Mode == 1) ? BoardLoc::kCollect : (BoardLoc::kKindBase + R->PeriodType);
	const FString Name = QuestName(R->QuestId);
	if (DetailHead)
	{
		DetailHead->SetText(FText::FromString(FString::Printf(TEXT("[%s] [%d / %d] %s"),
			*AsdaNotice::Loc(GI, KindLoc).TrimStartAndEnd(), R->LevelMin, R->LevelMax, *Name)));
	}
	FString Desc = QuestDesc(R->QuestId);
	if (R->PeriodType == 1)
	{
		const FAsdaBoardNode* N = FindNode(R->QuestId);
		const int32 Left = N ? (R->MaxCount - N->Count) : R->MaxCount;
		FString Line;
		if (Left <= 0 && N) { Line = AsdaNotice::Loc(GI, BoardLoc::kOnce); }
		else if (UAsdaTextSubsystem* TS = GI ? GI->GetSubsystem<UAsdaTextSubsystem>() : nullptr)
		{
			Line = TS->FormatById(BoardLoc::kLeft, {}, { Left });   // `CClass_SetSlotPair(ctx, 0, остаток)` → [sd1]
		}
		Desc = FString::Printf(TEXT("%s\n %s"), *Desc, *Line);
	}
	if (DetailDesc) { DetailDesc->SetText(FText::FromString(Desc)); }
	// Требуемые предметы: эталон — первые 3 (`m < 3`); ★ Д-4 — все 5 (4-й/5-й — вторым в строках 75244/75245).
	const UAsdaItemTableSubsystem* IT = GI ? GI->GetSubsystem<UAsdaItemTableSubsystem>() : nullptr;
	const int32 MaxReq = (CVarBoardFixReqItems5.GetValueOnGameThread() != 0) ? 5 : 3;
	TArray<FString> Lines; Lines.SetNum(3);
	for (int32 m = 0; m < MaxReq; ++m)
	{
		const int32 Id = R->ReqItemIds.IsValidIndex(m) ? R->ReqItemIds[m] : -1;
		if (Id < 0 || !IT || !IT->GetItem(Id)) { continue; }   // эталон: `CItemManager_GetItemRecord` — только предметы
		const int32 Have = (R->Mode == 1) ? BagCount(Id) : JournalCount(R->QuestId, Id);
		const int32 Need = R->ReqItemCounts.IsValidIndex(m) ? R->ReqItemCounts[m] : 0;
		const FString S = FString::Printf(TEXT("%s: %d/%d "), *IT->GetItemName(Id), Have, Need);
		FString& Dst = Lines[m % 3];
		Dst = Dst.IsEmpty() ? S : FString::Printf(TEXT("%s, %s"), *Dst.TrimEnd(), *S);
	}
	for (int32 i = 0; i < 3; ++i) { if (DetailReq.IsValidIndex(i) && DetailReq[i]) { DetailReq[i]->SetText(FText::FromString(Lines[i])); } }
	// Награда: золото (иконка предмета 20551), опыт (`exp2`), 2 предмета; затем 4 на выбор.
	UAsdaInventorySubsystem* Inv = GetGameInstance() ? GetGameInstance()->GetSubsystem<UAsdaInventorySubsystem>() : nullptr;
	if (R->Gold > 0 && IT && IT->GetItem(kGoldItem))
	{
		FAsdaBoardRewardUi& C = RewardUi[0];
		if (C.Label) { C.Label->SetText(FText::FromString(FString::Printf(TEXT("%d %s"), R->Gold, *IT->GetItemName(kGoldItem)))); }
		if (C.Icon && Inv) { AsdaUi::SetShownDecor(C.Icon, Inv->ApplyIconToImage(C.Icon, kGoldItem, 32.f)); }
	}
	if (R->Exp > 0)
	{
		FAsdaBoardRewardUi& C = RewardUi[1];
		if (C.Label) { C.Label->SetText(FText::FromString(FString::Printf(TEXT("%d %s"), R->Exp, *AsdaNotice::Loc(GI, BoardLoc::kExp)))); }
		if (C.Icon) { AsdaUi::SetShownDecor(C.Icon, AsdaUi::ApplyFlatJuiIcon(C.Icon, kExpIcon, 32.f)); }
	}
	for (int32 n = 0; n < 2; ++n)
	{
		const int32 Id = R->RewardItemIds.IsValidIndex(n) ? R->RewardItemIds[n] : -1;
		if (Id >= 0) { SetRewardCell(RewardUi[2 + n], Id, R->RewardItemCounts.IsValidIndex(n) ? R->RewardItemCounts[n] : 0, true); }
	}
	for (int32 k = 0; k < 4; ++k)
	{
		const int32 Id = R->ChoiceItemIds.IsValidIndex(k) ? R->ChoiceItemIds[k] : -1;
		if (Id >= 0) { SetRewardCell(ChoiceUi[k], Id, R->ChoiceItemCounts.IsValidIndex(k) ? R->ChoiceItemCounts[k] : 0, true); }
	}
}

// Ячейка награды: имя — предмет → самоцвет → спец-предмет (порядок эталона), количество — текстом иконки при > 0.
void UAsdaBoardQuestSubsystem::SetRewardCell(FAsdaBoardRewardUi& C, int32 ItemId, int32 Count, bool bQtyOnIcon)
{
	UGameInstance* GI = GetGameInstance();
	const UAsdaItemTableSubsystem* IT = GI ? GI->GetSubsystem<UAsdaItemTableSubsystem>() : nullptr;
	UAsdaInventorySubsystem* Inv = GI ? GI->GetSubsystem<UAsdaInventorySubsystem>() : nullptr;
	FString Name;
	FAsdaSowellRow Sw;
	FAsdaSpecialItemlistRow Sp;
	FAsdaSpecialitemTooltiplistRow Tip;
	if (IT && IT->GetItem(ItemId)) { Name = IT->GetItemName(ItemId); }
	else if (IT && IT->GetSowell(ItemId, Sw)) { Name = Sw.Name; }
	else if (IT && IT->GetSpecialItem(ItemId, Sp) && IT->GetSpecialItemTooltip(ItemId, Tip)) { Name = Tip.Name; }
	if (C.Label) { C.Label->SetText(FText::FromString(Name)); }
	if (bQtyOnIcon && C.Qty)
	{
		C.Qty->SetText(Count > 0 ? FText::FromString(FString::Printf(TEXT("%d"), Count)) : FText::GetEmpty());
	}
	if (C.Icon && Inv) { AsdaUi::SetShownDecor(C.Icon, Inv->ApplyIconToImage(C.Icon, ItemId, 32.f)); }
}

// Пустая ячейка: тексты пусты, иконка — пустая текстура 32×32 (`getSubObject_p3081112`) = прозрачная кисть.
void UAsdaBoardQuestSubsystem::ClearRewardCell(FAsdaBoardRewardUi& C)
{
	if (C.Label) { C.Label->SetText(FText::GetEmpty()); }
	if (C.Qty) { C.Qty->SetText(FText::GetEmpty()); }
	if (C.Icon) { AsdaUi::ClearImageBrush(C.Icon); }
}

// Подпись 75281 по выбранной строке (хвост события 11): готово к сдаче → 3575, иначе 3597.
void UAsdaBoardQuestSubsystem::ApplyOkCaption()
{
	if (SelRow < 0 || !Tabs.IsValidIndex(Tab) || !Tabs[Tab].Indices.IsValidIndex(SelRow) || !BtnOkText) { return; }
	const FAsdaRaidQuestRow* R = QuestAt(Tabs[Tab].Indices[SelRow]);
	if (!R) { return; }
	const FAsdaBoardNode* N = FindNode(R->QuestId);
	const bool bReady = N && IsQuestReady(R->QuestId) && N->State > 0;
	BtnOkText->SetText(FText::FromString(AsdaNotice::Loc(GetGameInstance(), bReady ? BoardLoc::kStateDone : BoardLoc::kTake)));
}

// Рамка 75041 на позицию `row.main` строки (y − 2): 8 планов сдвигаются на одну дельту от исходной раскладки.
void UAsdaBoardQuestSubsystem::PlaceSelectFrame(int32 Row)
{
	if (!RowUi.IsValidIndex(Row) || SelFrame.Num() != SelFrameBase.Num()) { return; }
	const FAsdaBoardRowUi& U = RowUi[Row];
	const UCanvasPanelSlot* GS = AsdaUi::SlotOf(U.Main);
	const UCanvasPanelSlot* MS = AsdaUi::SlotOf(U.MainText);
	if (!GS || !MS) { return; }
	// Позиция main в координатах LBOX = оффсет группы + оффсет main внутри группы (рамка и строки — дети LBOX).
	const FVector2D Main(GS->GetOffsets().Left + MS->GetOffsets().Left, GS->GetOffsets().Top + MS->GetOffsets().Top);
	const FVector2D Want(Main.X, Main.Y - 2.f);
	const FVector2D Delta = Want - SelFrameOrigin;
	for (int32 k = 0; k < SelFrame.Num(); ++k)
	{
		if (UCanvasPanelSlot* S = AsdaUi::SlotOf(SelFrame[k]))
		{
			const FMargin O = S->GetOffsets();
			S->SetOffsets(FMargin(SelFrameBase[k].X + Delta.X, SelFrameBase[k].Y + Delta.Y, O.Right, O.Bottom));
		}
		AsdaUi::SetShownDecor(SelFrame[k], true);
	}
}

void UAsdaBoardQuestSubsystem::SetHover(int32 Row)
{
	for (int32 i = 0; i < RowUi.Num(); ++i) { AsdaUi::SetShownDecor(RowUi[i].Hover, i == Row); }
}

// Событие 11 по колонкам строки (quest.c:9472-9550).
void UAsdaBoardQuestSubsystem::OnRowClick(int32 Row)
{
	if (Row < 0) { return; }
	SelRow = Row;
	SetHover(-1);
	PlaceSelectFrame(Row);
	PopulateDetail(Row);
	ApplyOkCaption();
	UE_LOG(LogAsdaBoard, Log, TEXT("[доска] строка %d выбрана (вкладка %d)"), Row, Tab);
}

// Событие 0x10501 по 75002 (quest.c:9339-9346).
void UAsdaBoardQuestSubsystem::OnTabSelected(int32 InTab)
{
	if (InTab < 0 || InTab >= kTabs) { return; }
	Tab = InTab;
	SelRow = -1;
	PopulateList(Tab);
	PopulateDetail(SelRow);
	for (UImage* F : SelFrame) { AsdaUi::SetShownDecor(F, false); }
	if (TabPlanes.Num() == kTabs && TabButtons.Num() == kTabs && TabTexts.Num() == kTabs)
	{
		AsdaUi::ApplyTabState(TabPlanes, TabTexts, TabButtons, Tab, TabIdleBrush, TabIdleStyle, TabIdleCaption);
	}
	UE_LOG(LogAsdaBoard, Log, TEXT("[доска] вкладка %d (зона %d), строк %d"), Tab, Tabs.IsValidIndex(Tab) ? Tabs[Tab].Zone : -1,
		Tabs.IsValidIndex(Tab) ? Tabs[Tab].Indices.Num() : 0);
}

void UAsdaBoardQuestSubsystem::MsgBox(int32 LocId, int32 EtalonKey)
{
	DebugLastMsgBoxLoc = LocId;
	AsdaNotice::MsgBoxLoc(GetGameInstance(), EAsdaMsgCategory::Quest, LocId, AsdaLoc::kBtnOk, EtalonKey);
}

// Кнопка 75281 «Взять задание» / «Выполнено» (quest.c:9348-9389, §2.8).
void UAsdaBoardQuestSubsystem::OnOkButton()
{
	const FAsdaRaidQuestRow* R = (SelRow >= 0 && Tabs.IsValidIndex(Tab) && Tabs[Tab].Indices.IsValidIndex(SelRow))
		? QuestAt(Tabs[Tab].Indices[SelRow]) : nullptr;
	if (!R) { MsgBox(BoardLoc::kNoSel, -1); return; }           // 3602
	const int32 L = SelfLevel();
	if (L < R->LevelMin) { MsgBox(BoardLoc::kLevel, -1); return; }   // 3604, окно остаётся
	const int32 Q = R->QuestId;
	const FString Name = QuestName(Q);
	const int32 LvMax = R->LevelMax;
	const FAsdaBoardNode* N = FindNode(Q);
	const bool bTurnIn = N && IsQuestReady(Q) && N->State > 0;
	// ★ [ПОРТ] ревизия М-1: замок «запрос в полёте» (P63 сервера отклоняет повтор до ответа БД). Эталон замка не знает и шлёт
	//   всегда; порт повтор не шлёт — и тогда НЕ закрывает окно (выбор строки сохранён, игрок жмёт ещё раз после ответа),
	//   чтобы нажатие не пропадало молча. Нотиса у эталона для этого нет — своего не выдумываем, причина — в журнал.
	if (!bTurnIn && L <= LvMax && IsInFlight())
	{
		UE_LOG(LogAsdaBoard, Log, TEXT("[доска] 6591 по заданию %d отложен: прошлый запрос %d ещё без ответа — окно остаётся открытым"), Q, InFlightOp);
		return;
	}
	Close();                                                    // `questUiReset` — окно закрывается до решения
	if (bTurnIn)
	{
		PendingTurnIn = Q;                                      // `this[10] = questId`
		UAsdaNpcDialogSubsystem* Dlg = GetGameInstance() ? GetGameInstance()->GetSubsystem<UAsdaNpcDialogSubsystem>() : nullptr;
		const bool bShown = Dlg && Dlg->ShowBoardRewardPopup(Q, Name);   // `uiQuestWin_openReward(g_pNpcWnd, q, имя)`
		UE_LOG(LogAsdaBoard, Log, TEXT("[доска] задание %d готово к сдаче → попап наград 540 %s"), Q, bShown ? TEXT("показан") : TEXT("НЕ показан"));
		return;
	}
	if (L <= LvMax)
	{
		UAsdaNetSubsystem* Net = GetGameInstance() ? GetGameInstance()->GetSubsystem<UAsdaNetSubsystem>() : nullptr;
		TArray<uint8> Body; Body.SetNumZeroed(4); FMemory::Memcpy(Body.GetData(), &Q, 4);
		NoteSent(6591, Body);
		if (Net && Net->SendBoardEnter6591(Q)) { InFlightSince = FPlatformTime::Seconds(); InFlightOp = 6591; }
		return;
	}
	MsgBox(BoardLoc::kLevel, -1);                               // 3604 (уровень выше lvMax)
}

bool UAsdaBoardQuestSubsystem::IsInFlight() const { return InFlightOp != 0; }

bool UAsdaBoardQuestSubsystem::SendTurnIn6593(int32 QuestId, int32 Choice)
{
	// ★ [ПОРТ] ревизия М-1: при занятом замке 6593 НЕ уходит, а ждущее сдачи задание (`this[10]`) и попап 540 остаются —
	//   игрок жмёт 567 ещё раз после ответа (false — вызывающий попап не закрывает). Эталон сбрасывает `this[10]` после отправки.
	if (IsInFlight())
	{
		UE_LOG(LogAsdaBoard, Log, TEXT("[доска] 6593 по заданию %d отложен: прошлый запрос %d ещё без ответа — попап и ждущее сдачи сохранены"), QuestId, InFlightOp);
		return false;
	}
	PendingTurnIn = -1;   // `netSendTsMaskedReq6593` после отправки ставит `this[10] = −1`
	TArray<uint8> Body; Body.SetNumZeroed(5); FMemory::Memcpy(Body.GetData(), &QuestId, 4); Body[4] = (uint8)Choice;
	NoteSent(6593, Body);
	UAsdaNetSubsystem* Net = GetGameInstance() ? GetGameInstance()->GetSubsystem<UAsdaNetSubsystem>() : nullptr;
	const bool bOk = Net && Net->SendBoardResult6593(QuestId, Choice);
	if (bOk) { InFlightSince = FPlatformTime::Seconds(); InFlightOp = 6593; }
	return true;   // запрос не задержан замком (провал самой отправки — как у эталона, попап закрывается)
}

// ★ Ревизия М-4: журнал отправок доски — только вне Shipping и не длиннее `kDebugSentCap` (для диагностики).
void UAsdaBoardQuestSubsystem::NoteSent(int32 Op, const TArray<uint8>& Body)
{
#if !UE_BUILD_SHIPPING
	if (DebugSent.Num() >= kDebugSentCap) { DebugSent.RemoveAt(0); }
	DebugSent.Add({ Op, Body });
#endif
}

void UAsdaBoardQuestSubsystem::OnRewardPopupHidden()
{
	if (PendingTurnIn >= 0 && CVarBoardFixPendingReset.GetValueOnGameThread() != 0)
	{
		UE_LOG(LogAsdaBoard, Log, TEXT("[доска] попап 540 закрыт без сдачи — ждущее сдачи задание %d снято (исправление)"), PendingTurnIn);
		PendingTurnIn = -1;
	}
}

void UAsdaBoardQuestSubsystem::RefreshIfOpen()
{
	if (!bOpen) { return; }
	PopulateList(Tab);
	PopulateDetail(SelRow);
	ApplyOkCaption();
}

// ═════════════════════════════ СЕТЬ ═══════════════════════════════════════════════════════════

void UAsdaBoardQuestSubsystem::DebugFeed(int32 Opcode, const TArray<uint8>& Body)
{
	PendingPkts.Enqueue({ Opcode, Body });
	DrainPackets();
}

void UAsdaBoardQuestSubsystem::DrainPackets()
{
	FPkt P;
	bool bAny = false;
	while (PendingPkts.Dequeue(P))
	{
		EnsureData();
		bAny = true;
		switch (P.Op)
		{
		case 6588: Apply6588(P.Body); break;
		case 6589: UE_LOG(LogAsdaBoard, Log, TEXT("[доска] 6589 — пустой (эталон `return 1`)")); break;
		case 6590: Apply6590(P.Body); break;
		case 6594: Apply6594(P.Body); break;
		case 6595: Apply6595(); break;
		default: break;
		}
	}
	if (bAny) { NotifyChanged(); }
}

FAsdaBoardNode UAsdaBoardQuestSubsystem::ReadNode(const uint8* P)
{
	FAsdaBoardNode N;
	N.QuestId = RdI32(P);
	N.Type = P[4];
	N.Count = RdI16(P + 5);
	N.State = (int8)P[7];
	return N;
}

void UAsdaBoardQuestSubsystem::UpsertNode(const FAsdaBoardNode& N)
{
	if (FAsdaBoardNode* E = FindNodeMutable(N.QuestId)) { *E = N; } else { Nodes.Add(N); }
}

void UAsdaBoardQuestSubsystem::RemoveNode(int32 QuestId)
{
	Nodes.RemoveAll([QuestId](const FAsdaBoardNode& N) { return N.QuestId == QuestId; });
}

// `packetParseRecordList8@0xEEE190`: узлы `(длина − 9) >> 3` добавляются в список (без очистки и без дедупа).
//   [ПОРТ] вставка/замена по номеру — список переживает смену карты (см. ResetSessionState).
//   ★ [ИСПРАВЛЕНИЕ ОРИГИНАЛА] сервер шлёт 6588 с паддингом 0…19 б (`sub_5643E0`, a8 = 1), и эталон, считая узлы по длине
//   кадра, при паддинге ≥ 7 б добавлял 1–2 мусорных узла. Здесь — только целые узлы с номером из таблицы заданий.
void UAsdaBoardQuestSubsystem::Apply6588(const TArray<uint8>& B)
{
	const int32 Num = B.Num() / 8;
	int32 Taken = 0, Dropped = 0;
	for (int32 i = 0; i < Num; ++i)
	{
		const FAsdaBoardNode N = ReadNode(B.GetData() + 8 * i);
		if (!FindQuest(N.QuestId)) { ++Dropped; continue; }
		UpsertNode(N);
		++Taken;
	}
	UE_LOG(LogAsdaBoard, Log, TEXT("[доска] 6588: тело %d б → узлов %d принято, %d отброшено (хвост/вне таблицы); всего узлов %d"),
		B.Num(), Taken, Dropped, Nodes.Num());
}

// `buddyOnStatusUpdatePacket@0xEEDAF0` (network.c:33249): 11 б = u8 код, i16 подопкод, узел 8 б.
void UAsdaBoardQuestSubsystem::Apply6590(const TArray<uint8>& B)
{
	if (B.Num() < 11) { return; }
	const int32 Code = B[0];
	const int32 Sub = RdI16(B.GetData() + 1);
	const FAsdaBoardNode N = ReadNode(B.GetData() + 3);
	if (InFlightOp == 6591 && (Sub == 6591 || Code != 1)) { InFlightSince = 0.0; InFlightOp = 0; }   // ответ на «взять»
	const bool bFixType = CVarBoardFixServerType.GetValueOnGameThread() != 0;
	// Тип задания для веток 6592/6593/5006: эталон — по таблице клиента (`mapObjFindValueById`), Д-6 — тип узла сервера.
	const int32 T = bFixType ? N.Type : FindQuestType(N.QuestId);
	switch (Code)
	{
	case 1:
		if (Sub > 6591)
		{
			if (Sub != 6592 && Sub != 6593) { return; }
			// Эталон: тип ≠ 1 → узел удаляется (у 2/3 теряется счёт до перезахода); Д-6 — удаляется только тип 0 («Повтор»).
			const bool bKeep = bFixType ? (T != 0) : (T == 1);
			if (!bKeep) { RemoveNode(N.QuestId); }
			else if (FAsdaBoardNode* E = FindNodeMutable(N.QuestId)) { *E = N; }
		}
		else if (Sub == 6591)
		{
			UpsertNode(N);   // взято: вставить или заменить
		}
		else if (Sub != 0)
		{
			if (Sub != 5006) { return; }
			if (T == 1 || T == 2 || T == 3) { if (FAsdaBoardNode* E = FindNodeMutable(N.QuestId)) { *E = N; } }
			else { RemoveNode(N.QuestId); }
		}
		else
		{
			if (FAsdaBoardNode* E = FindNodeMutable(N.QuestId)) { *E = N; }   // 0 — цели выполнены и т. п.: заменить, если есть
		}
		UE_LOG(LogAsdaBoard, Log, TEXT("[доска] 6590 код 1 подоп %d: задание %d тип %d счёт %d состояние %d; узлов %d"),
			Sub, N.QuestId, N.Type, N.Count, N.State, Nodes.Num());
		return;
	default:
		if (Code >= 2 && Code <= 8)
		{
			UE_LOG(LogAsdaBoard, Log, TEXT("[доска] 6590 код %d (задание %d) → нотис %d"), Code, N.QuestId, BoardLoc::kEnterFail[Code]);
			MsgBox(BoardLoc::kEnterFail[Code], -1);
		}
		else
		{
			UE_LOG(LogAsdaBoard, Log, TEXT("[доска] 6590 код %d — эталон молчит"), Code);
		}
		return;
	}
}

// `recv_ItemExchangeResult@0xEEE3D0` (network.c:33423): 484 б.
void UAsdaBoardQuestSubsystem::Apply6594(const TArray<uint8>& B)
{
	if (B.Num() < 484) { return; }
	if (InFlightOp == 6593) { InFlightSince = 0.0; InFlightOp = 0; }
	UGameInstance* GI = GetGameInstance();
	const int32 Code = B[0];
	if (Code == 1)
	{
		if (UAsdaSoundSubsystem* Snd = GI ? GI->GetSubsystem<UAsdaSoundSubsystem>() : nullptr) { Snd->PlaySound2DById(14); }
		const int32 QuestId = RdI32(B.GetData() + 24);
		const int32 SlotIdx = RdI16(B.GetData() + 22);
		if (UAsdaQuestMarkerSubsystem* QM = GI ? GI->GetSubsystem<UAsdaQuestMarkerSubsystem>() : nullptr)
		{
			QM->ClearBoardQuestSlot(QuestId, SlotIdx);   // `questClearObjectiveEntries(g_pQuestMgr, questId, слот)`
		}
		const FColor Yellow((uint32)kColYellow);
		auto Pair = [GI, &Yellow](const FString& Text)   // `CChat_AddTokenizedLines(…, −256, 10000)` + `CControl_SetTooltipText(…)`
		{
			AsdaNotice::ChatColored(GI, EAsdaMsgCategory::System, Text, Yellow, 10000.f);
			AsdaNotice::Plank(GI, EAsdaMsgCategory::System, Text, Yellow, 10000.f, 6594);
		};
		UAsdaInventorySubsystem* Inv = GI ? GI->GetSubsystem<UAsdaInventorySubsystem>() : nullptr;
		const int32 GoldAdd = RdI32(B.GetData() + 5);
		if (Inv) { Inv->SetGoldDisplay(Inv->GetGold() + GoldAdd); }   // `uiSetHonorPointsDisplay` + `invGetFreeSlotByMarker`
		Pair(AsdaNotice::LocSlot(GI, BoardLoc::kGold, AsdaUi::FormatThousands(GoldAdd)));
		const UAsdaItemTableSubsystem* IT = GI ? GI->GetSubsystem<UAsdaItemTableSubsystem>() : nullptr;
		for (int32 i = 0; i < 3; ++i)
		{
			const int32 Off = 28 + FAsdaItemInstance::kRecordSize * i;
			if (RdI16(B.GetData() + Off + 5) < 0) { continue; }
			TArray<uint8> Rec(B.GetData() + Off, FAsdaItemInstance::kRecordSize);
			if (Inv) { Inv->ApplyExternalRecord(Rec, UAsdaInventorySubsystem::EExternalRecordMode::AsIs); }   // `OnRecv_ItemUpdate`
			const int32 Id = RdI32(B.GetData() + Off);
			FString Name;
			FAsdaSowellRow Sw;
			FAsdaSpecialItemlistRow Sp;
			FAsdaSpecialitemTooltiplistRow Tip;
			if (IT && IT->GetItem(Id)) { Name = IT->GetItemName(Id); }
			else if (IT && IT->GetSowell(Id, Sw)) { Name = Sw.Name; }
			else if (IT && IT->GetSpecialItem(Id, Sp) && IT->GetSpecialItemTooltip(Id, Tip)) { Name = Tip.Name; }
			if (!Name.IsEmpty()) { Pair(AsdaNotice::LocSlot(GI, BoardLoc::kItemGot, Name)); }
		}
		if (UAsdaNetSubsystem* Net = GI ? GI->GetSubsystem<UAsdaNetSubsystem>() : nullptr)
		{
			Net->SetSelfWeight(AsdaWeightFromWire(RdI32(B.GetData() + 9)), -1, TEXT("6594"));   // ★ 29.10.2026: вес u16   // `HUD_UpdateMiniWeightBar(+9)`
		}
		// 5 изменённых ячеек сумки (+199 + 57u): количество > 0 — только количество, ≤ 0 — удалить (`slotTableSetFieldById` /
		//   `OnRecv_RemoveSlot_byPtr`) — это режим `CountOrClear` общей точки инвентаря.
		for (int32 u = 0; u < 5; ++u)
		{
			const int32 Off = 199 + FAsdaItemInstance::kRecordSize * u;
			if (RdI16(B.GetData() + Off + 5) < 0) { continue; }
			TArray<uint8> Rec(B.GetData() + Off, FAsdaItemInstance::kRecordSize);
			if (Inv) { Inv->ApplyExternalRecord(Rec, UAsdaInventorySubsystem::EExternalRecordMode::CountOrClear); }
		}
		UE_LOG(LogAsdaBoard, Log, TEXT("[доска] 6594 код 1: задание %d сдано, слот журнала %d, золото +%d"), QuestId, SlotIdx, GoldAdd);
	}
	else if (Code == 2) { MsgBox(BoardLoc::kRewardErr, 24); }
	else if (Code == 3) { MsgBox(BoardLoc::kNoSlot, 24); }
	else { UE_LOG(LogAsdaBoard, Log, TEXT("[доска] 6594 код %d — эталон молчит"), Code); }
	// `uiExpandPartyPanel(g_pUIManager)` — пара к `uiTogglePanelTab(1)` показа попапа 540; порт панели при попапе не прячет.
}

// `itemListClearReadyFlags@0xEEE300`: у узлов с типом 1 (тип УЗЛА) счёт := 0. ★ Д-5 — и состояние 3 → 0.
void UAsdaBoardQuestSubsystem::Apply6595()
{
	const bool bFix = CVarBoardFixDailyState.GetValueOnGameThread() != 0;
	int32 N1 = 0;
	for (FAsdaBoardNode& N : Nodes)
	{
		if (N.Type != 1) { continue; }
		N.Count = 0;
		if (bFix && N.State == 3) { N.State = 0; }
		++N1;
	}
	UE_LOG(LogAsdaBoard, Log, TEXT("[доска] 6595 суточный сброс: дневных узлов %d%s"), N1, bFix ? TEXT(" (и состояние 3 → 0)") : TEXT(""));
}

// `mapObjBeginFirstReadyCraft@0xEEF010` (каждый кадр, game_main.c:1973): первый узел state 1, режим «Собрать», всё в сумке →
//   state 2 локально + перерисовка ячеек журнала. ★ Д-2: эталон сравнивает `==` (лишний предмет — «не готово»).
void UAsdaBoardQuestSubsystem::TickCollectReady()
{
	if (Nodes.Num() == 0) { return; }
	const bool bFix = CVarBoardFixCollectReady.GetValueOnGameThread() != 0;
	for (FAsdaBoardNode& N : Nodes)
	{
		if (N.State != 1) { continue; }
		const FAsdaRaidQuestRow* R = FindQuest(N.QuestId);
		if (!R || R->Mode != 1) { continue; }
		int32 Need = 0, Ok = 0;
		for (int32 i = 0; i < 5; ++i)
		{
			const int32 Id = R->ReqItemIds.IsValidIndex(i) ? R->ReqItemIds[i] : -1;
			const int32 Cnt = R->ReqItemCounts.IsValidIndex(i) ? R->ReqItemCounts[i] : 0;
			if (Id < 0 || Cnt <= 0) { continue; }
			++Need;
			const int32 Have = BagCount(Id);
			if (bFix ? (Have >= Cnt) : (Have == Cnt)) { ++Ok; }
		}
		if (Need > 0 && Need == Ok)
		{
			N.State = 2;
			UE_LOG(LogAsdaBoard, Log, TEXT("[доска] задание %d «Собрать»: всё в сумке → состояние 2 (локально)"), N.QuestId);
			NotifyChanged();
			return;
		}
	}
}

void UAsdaBoardQuestSubsystem::NotifyChanged()
{
	OnBoardChanged.Broadcast();
	if (UAsdaQuestMarkerSubsystem* QM = GetGameInstance() ? GetGameInstance()->GetSubsystem<UAsdaQuestMarkerSubsystem>() : nullptr)
	{
		QM->RebuildMarks();   // метка доски на миникарте зависит от узлов
	}
	RefreshIfOpen();
}

// ═════════════════════════════ ПОКАДРОВЫЙ ОПРОС ═══════════════════════════════════════════════

bool UAsdaBoardQuestSubsystem::IsCursorOverRow(int32 Row) const
{
	if (!RowUi.IsValidIndex(Row) || !FSlateApplication::IsInitialized()) { return false; }
	const FVector2D C = FSlateApplication::Get().GetCursorPos();
	for (const TObjectPtr<UWidget>& W : RowUi[Row].Hits)
	{
		FVector2D A, B;
		if (W && AsdaUi::WidgetAbsRect(W, A, B) && C.X >= A.X && C.X < B.X && C.Y >= A.Y && C.Y < B.Y) { return true; }
	}
	return false;
}

// События мыши окна (`questUiOnControlEvent`): 9 наведение → подсветка, 10 уход → снять, 11 ЛКМ → выбор, 8 подсказка 75281.
void UAsdaBoardQuestSubsystem::TickInput()
{
	if (!bOpen || !bControlsReady) { return; }
	Mouse.Update();
	UGameInstance* GI = GetGameInstance();
	const UAsdaWindowSubsystem* WM = GI ? GI->GetSubsystem<UAsdaWindowSubsystem>() : nullptr;
	if (AsdaUi::IsModalDispatchBlockedFor(GI, kBoardWindowId)
		|| (WM && (WM->IsPointerBlockedByTopmost(kBoardWindowId) || WM->IsCursorOverHigherWindow(kBoardWindowId))))
	{
		if (HoverRow >= 0) { HoverRow = -1; SetHover(-1); }
		Mouse.Reset();
		return;
	}
	// Подсказка 75281 (события 8/10).
	const bool bOverOk = BtnOk && AsdaUi::IsCursorOverWidget(BtnOk);
	if (bOverOk != bOkTipShown)
	{
		UAsdaItemTooltipSubsystem* Tip = GI ? GI->GetSubsystem<UAsdaItemTooltipSubsystem>() : nullptr;
		if (Tip)
		{
			if (bOverOk)
			{
				FAsdaTipMetrics M; M.FontPx = 8; M.MaxW = 218.f;
				Tip->ShowTextTip(EAsdaTipOwner::BoardQuest, AsdaNotice::Loc(GI, BoardLoc::kTakeTip), FString(), M);
			}
			else { Tip->HideTooltipFor(EAsdaTipOwner::BoardQuest); }
		}
		bOkTipShown = bOverOk;
	}
	// Строка под курсором (9) / уход (10). Строки за списком скрыты — событий у них нет.
	int32 Over = -1;
	const int32 Shown = Tabs.IsValidIndex(Tab) ? FMath::Min(Tabs[Tab].Indices.Num(), kRows) : 0;
	for (int32 i = 0; i < Shown && Over < 0; ++i) { if (IsCursorOverRow(i)) { Over = i; } }
	if (Over != HoverRow)
	{
		HoverRow = Over;
		// Наведение на рамку выбранной строки (событие 9 по 75041) подсвечивает выбранную — она и есть строка под курсором.
		SetHover(Over);   // 9 — подсветка строки под курсором, 10 — снять все
	}
	if (Mouse.bLmbDown && Over >= 0) { OnRowClick(Over); }
}

// ═════════════════════════════ ДИАГНОСТИКА ════════════════════════════════════════════════════

FString UAsdaBoardQuestSubsystem::DumpState() const
{
	FString S = FString::Printf(TEXT("=== доска объявлений: окно %s, npcId=%d, вкладок %d, вкладка %d, строка %d, наведение %d, ждёт сдачи %d, в полёте %d ===\n"),
		bOpen ? TEXT("ОТКРЫТО") : TEXT("закрыто"), NpcId, TabCount, Tab, SelRow, HoverRow, PendingTurnIn, InFlightOp);
	S += FString::Printf(TEXT("таблицы: заданий %d, записей NPC %d; уровень %d\n"), Quests.Num(), Npcs.Num(), SelfLevel());
	for (int32 n = 0; n < TabCount && n < Tabs.Num(); ++n)
	{
		S += FString::Printf(TEXT("вкладка %d: зона %d «%s», заданий %d:"), n, Tabs[n].Zone, *ZoneName(GetGameInstance(), Tabs[n].Zone), Tabs[n].Indices.Num());
		for (int32 k : Tabs[n].Indices) { S += FString::Printf(TEXT(" %d"), kQuestBase + k); }
		S += TEXT("\n");
	}
	S += FString::Printf(TEXT("узлов %d:"), Nodes.Num());
	for (const FAsdaBoardNode& N : Nodes) { S += FString::Printf(TEXT(" [%d т%d с%d с%d]"), N.QuestId, N.Type, N.Count, N.State); }
	S += TEXT("\n");
	for (int32 i = 0; i < RowUi.Num(); ++i)
	{
		const FAsdaBoardRowUi& U = RowUi[i];
		if (!U.Main || !AsdaUi::IsShown(U.Main)) { continue; }
		S += FString::Printf(TEXT("строка %d: «%s» «%s» «%s» «%s» «%s»\n"), i,
			U.Kind ? *U.Kind->GetText().ToString() : TEXT("—"), U.Count ? *U.Count->GetText().ToString() : TEXT("—"),
			U.Level ? *U.Level->GetText().ToString() : TEXT("—"), U.Name ? *U.Name->GetText().ToString() : TEXT("—"),
			U.State ? *U.State->GetText().ToString() : TEXT("—"));
	}
	return S;
}

FString UAsdaBoardQuestSubsystem::DumpControls() const
{
	FString S = TEXT("=== окно доски 75000: разобранные контролы (живая геометрия) ===\n");
	auto Line = [&S](const FString& Id, const UWidget* W)
	{
		FVector2D Mn = FVector2D::ZeroVector, Mx = FVector2D::ZeroVector;
		const bool bGeo = W && AsdaUi::WidgetAbsRect(W, Mn, Mx);
		S += FString::Printf(TEXT("%-28s %-34s %s %s\n"), *Id, W ? *W->GetName() : TEXT("— НЕТ —"),
			bGeo ? *FString::Printf(TEXT("rect=(%.0f,%.0f)-(%.0f,%.0f)"), Mn.X, Mn.Y, Mx.X, Mx.Y) : TEXT("rect=нет"),
			(W && AsdaUi::IsShown(W)) ? TEXT("видим") : TEXT("скрыт"));
	};
	for (int32 i = 0; i < RowUi.Num(); ++i)
	{
		const FAsdaBoardRowUi& U = RowUi[i];
		Line(FString::Printf(TEXT("%d строка"), 75021 + i), U.Main);
		Line(FString::Printf(TEXT("%d Вид"), 75061 + i), U.Kind);
		Line(FString::Printf(TEXT("%d Ур."), 75101 + i), U.Level);
		Line(FString::Printf(TEXT("%d Задание"), 75141 + i), U.Name);
		Line(FString::Printf(TEXT("%d Состояние"), 75181 + i), U.State);
		Line(FString::Printf(TEXT("%d Кол-во"), 75301 + i), U.Count);
		Line(FString::Printf(TEXT("%d подсветка"), 75211 + i), U.Hover);
	}
	for (int32 n = 0; n < TabPlanes.Num(); ++n) { Line(FString::Printf(TEXT("75002 вкладка %d"), n), TabPlanes[n]); }
	Line(TEXT("75242"), DetailHead); Line(TEXT("75243"), DetailDesc);
	for (int32 i = 0; i < DetailReq.Num(); ++i) { Line(FString::Printf(TEXT("%d требование"), 75244 + i), DetailReq[i]); }
	for (int32 i = 0; i < RewardUi.Num(); ++i)
	{
		Line(FString::Printf(TEXT("%d иконка"), 75252 + 2 * i), RewardUi[i].Icon);
		Line(FString::Printf(TEXT("%d подпись"), 75253 + 2 * i), RewardUi[i].Label);
	}
	for (int32 i = 0; i < ChoiceUi.Num(); ++i)
	{
		Line(FString::Printf(TEXT("%d иконка"), 75261 + 2 * i), ChoiceUi[i].Icon);
		Line(FString::Printf(TEXT("%d подпись"), 75262 + 2 * i), ChoiceUi[i].Label);
	}
	for (int32 k = 0; k < SelFrame.Num(); ++k) { Line(FString::Printf(TEXT("75041 план %d"), k), SelFrame[k]); }
	Line(TEXT("75281"), BtnOk); Line(TEXT("75282"), BtnNo); Line(TEXT("75289"), BtnClose);
	return S;
}

#if ASDA_DEV_CONSOLE
// Открыть доску NPC без клика (тот же путь, что развилка типа 5): `asda.Board.Open <npcId>`.
static FAutoConsoleCommandWithWorldAndArgs GBoardOpen(TEXT("asda.Board.Open"),
	TEXT("Доска объявлений: открыть окно 75000 для NPC-доски (`asda.Board.Open 9|10`)."),
	FConsoleCommandWithWorldAndArgsDelegate::CreateLambda([](const TArray<FString>& A, UWorld* W)
	{
		UGameInstance* GI = W ? W->GetGameInstance() : nullptr;
		if (UAsdaBoardQuestSubsystem* S = GI ? GI->GetSubsystem<UAsdaBoardQuestSubsystem>() : nullptr)
		{
			S->OpenForNpc(A.Num() > 0 ? FCString::Atoi(*A[0]) : 9);
		}
	}));
static FAutoConsoleCommandWithWorldAndArgs GBoardDump(TEXT("asda.Board.Dump"),
	TEXT("Доска объявлений: состояние модели и окна в журнал."),
	FConsoleCommandWithWorldAndArgsDelegate::CreateLambda([](const TArray<FString>&, UWorld* W)
	{
		UGameInstance* GI = W ? W->GetGameInstance() : nullptr;
		if (const UAsdaBoardQuestSubsystem* S = GI ? GI->GetSubsystem<UAsdaBoardQuestSubsystem>() : nullptr)
		{
			TArray<FString> L; S->DumpState().ParseIntoArrayLines(L);
			for (const FString& X : L) { UE_LOG(LogAsdaBoard, Log, TEXT("%s"), *X); }
		}
	}));
static FAutoConsoleCommandWithWorldAndArgs GBoardControls(TEXT("asda.Board.Controls"),
	TEXT("Доска объявлений: разобранные контролы окна 75000 с живой геометрией."),
	FConsoleCommandWithWorldAndArgsDelegate::CreateLambda([](const TArray<FString>&, UWorld* W)
	{
		UGameInstance* GI = W ? W->GetGameInstance() : nullptr;
		if (const UAsdaBoardQuestSubsystem* S = GI ? GI->GetSubsystem<UAsdaBoardQuestSubsystem>() : nullptr)
		{
			TArray<FString> L; S->DumpControls().ParseIntoArrayLines(L);
			for (const FString& X : L) { UE_LOG(LogAsdaBoard, Log, TEXT("%s"), *X); }
		}
	}));
#endif
