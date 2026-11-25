// Copyright (c) 2026 @sitxovski (Discord)
// Реализация UAsdaQuestMarkerSubsystem — разбор эталона в Public/Quest/AsdaQuestTypes.h.
#include "Quest/AsdaQuestMarkerSubsystem.h"
#include "Npc/AsdaNpcGmOnly.h"   // ★ 09.11.2026: NPC «только для GM_»
#include "Quest/AsdaQuestGuideTarget.h"   // ★ 31.10.2026 (баг 6): место цели строки гида без координат — общая точка с «Следовать»
#include "Quest/AsdaBoardQuestSubsystem.h"   // ★ 28.10.2026: метка доски объявлений (тип 5) на миникарте
#include "AsdaDevConsole.h"   // ★ античит А2: ASDA_DEV_CVAR / ASDA_DEV_CONSOLE — дев-ключи и команды вне Shipping
#include "AsdaDataTableCache.h"   // ★ Аудит №4 (B2): AsdaDt::Cached — одна ленивая загрузка DataTable на порт

#include "AsdaNetSubsystem.h"
#include "AsdaBytes.h"                    // ★ ДЕДУП Д4: чтение little-endian из тела пакета — одна точка на порт
// FAsdaMarkup::StripTags — единая точка разбора тегов <F..>   ★ R3: комментарий снятого включения "UI/AsdaMarkup.h" (файл им не пользовался) — сохранён
#include "Npc/AsdaNpcDialogSubsystem.h"     // ShowQuestInfo/HideQuestInfo — окно QUEST_INFO_WIN (599)
#include "UI/AsdaWindowSubsystem.h"        // ★ Волна В4: роспись «закрыть всё» (трекер 1800)

#include "Engine/Engine.h"
#include "EngineUtils.h"
#include "AsdaOutgoingGate.h"               // ★ С-Р1: общий офлайн-гейт отладочных команд (RefuseDebugWhenOnline)
#include "Containers/Ticker.h"              // ★ 08.11.2026: самотест asda.SelfTest.QuestMarkArshes (шаги по времени)
#include "Text/AsdaLangRows.h"               // [ПОРТ] 09.10.2026 (мультиязычность): названия эпизодов и деревень на языке сессии
#include "AsdaQuestMarkerSubsystem_Private.h"   // ★ R3-c 12.11.2026: общее для частей AsdaQuestMarkerSubsystem*.cpp

DEFINE_LOG_CATEGORY(LogAsdaQuest);   // ★ R2: было DEFINE_LOG_CATEGORY_STATIC — категория общая для частей

static const TCHAR* kScriptDT = TEXT("/Game/AsdaAssets/Data/NPC/DT_NpcQuestScript.DT_NpcQuestScript");
static const TCHAR* kNpcDT    = TEXT("/Game/AsdaAssets/Data/NPC/DT_NpcQuestNpc.DT_NpcQuestNpc");
// Таблица эпизодов — общая точка «код эпизода ↔ ключ квеста ↔ название» (импортёр
//   `Tools/asda_import_npc_tables.py`).
// ★ ОФЛАЙН-ПОДМЕНА «уровень/класс персонажа» для проверок на PIE без сервера (консоль
//   `asda.Quest.FakeSelf`). Значение −1 = «не подменять», и тогда данные берутся штатно из
//   char-list сетевой подсистемы. В игре эти переменные всегда −1.
int32 GAsdaFakeSelfLevel = -1;
int32 GAsdaFakeSelfClass = -1;
int32 GAsdaFakeSelfSub   = -1;

static const TCHAR* kNmsDT = TEXT("/Game/AsdaAssets/Data/NPC/DT_NmsList.DT_NmsList");

// ★ ПОРТ ОПЦИИ `g_bOptFlag_15CB4E8` (байт `COptionsWnd + 1121`, чекбокс JUI `IDNum 70268`).
//   В оригинале чекбокс объявлен `Visible="0"` — в окно настроек он не выведен и игроком не
//   переключается; в живой игре подсказка работает ⇒ значение 1. Держим переключатель здесь,
//   чтобы поведение эталона было воспроизводимо, а не зашито.
int32 GAsdaQuestAwaitHint = 1;
static FAutoConsoleVariableRef CVarAsdaQuestAwaitHint(
	TEXT("Asda.Quest.AwaitHint"),
	GAsdaQuestAwaitHint,
	TEXT("Подсказка робота «Минопио» «Тебя кое-кто разыскивает…» (порт опции g_bOptFlag_15CB4E8). 0 = выключить."),
	ECVF_Default);

// ═══ ★★★ ОСОЗНАННОЕ ОТСТУПЛЕНИЕ ОТ ОРИГИНАЛА — РЕШЕНИЕ ВЛАДЕЛЬЦА 2026-08-23 ═══════════════════
//   ВЕРХНЯЯ ГРАНИЦА УРОВНЯ У ЗАДАНИЙ СНЯТА. «Перерос задание» больше не повод его не показывать
//   и не выдавать: персонаж 40 уровня спокойно берёт задания на 7…14.
//   Нижняя граница ОСТАЁТСЯ: рано взять по-прежнему нельзя.
//
//   Почему правка нужна и на клиенте тоже. Решение принимает СЕРВЕР
//   (`NpcServer!sub_41FD70`, поле `+38` записи `ConditionTable.Bin`; см.
//   `Reports/quest_level_gate.md`), и там граница снимается правкой ДАННЫХ. Но у клиента
//   есть свои таблицы условий с тем же полем — прежде всего `SNQICT.Bin` (список заданий в
//   облачке NPC): 246 её записей отсекают 40-й уровень именно потолком. Не сними мы потолок
//   здесь — задание сервером выдавалось бы, а в списке у NPC не показывалось, и клиент спорил
//   бы с сервером.
//
//   Масштаб (замерено, а не на глаз):
//     · `ConditionTable.Bin` (сервер): 806 условий с проверкой уровня, у 319 есть потолок;
//       на 40 ур. потолок закрывал 227 заданий из 369 выдаваемых;
//     · `SNQICT.Bin` (облачко NPC): 301 запись с потолком, 246 отсекают 40 ур.;
//     · `.NSR` (шаги диалога): потолок реально задан лишь у 14 под-записей из 812 — остальные
//       уже стоят на 150, поэтому шаг «взять задание» и раньше показывался (владелец его и
//       нажимал, получая отказ от сервера);
//     · `QCT.BIN` (условие «цели выполнены»): потолок у 3 записей (квесты 114/115/116, 22…34).
//
//   ★ КЛЮЧ ОТКАТА: `Asda.Quest.LevelCapEnforce 1` — возвращает поведение оригинала целиком.
int32 GAsdaQuestLevelCapEnforce = 0;
static ASDA_DEV_CVAR_REF(CVarAsdaQuestLevelCapEnforce,
	TEXT("Asda.Quest.LevelCapEnforce"),
	GAsdaQuestLevelCapEnforce,
	TEXT("Верхняя граница уровня в условиях заданий. 0 (по умолчанию) = не проверять, ")
	TEXT("«перерос» больше не мешает; 1 = как в оригинале. Нижняя граница действует всегда."),
	ECVF_Default);

// Как часто пересчитывать метки, если не было явного события (сек). В оригинале пересчёт событийный
//   (npcUiRefreshActionList после любого изменения квеста/входа на карту); периодика — страховка.
float GAsdaQuestMarkRebuildPeriod = 1.0f;
static ASDA_DEV_CVAR_REF(CVarAsdaQuestMarkRebuildPeriod,
	TEXT("Asda.Quest.MarkRebuildPeriod"),
	GAsdaQuestMarkRebuildPeriod,
	TEXT("Период фонового пересчёта квестовых меток NPC, сек (0 = только по событиям)."),
	ECVF_Default);

// ── Инициализация ───────────────────────────────────────────────────────────────────────────────
void UAsdaQuestMarkerSubsystem::Initialize(FSubsystemCollectionBase& Collection)
{
	Super::Initialize(Collection);
	Slots.SetNum(12);
	DoneBits.SetNumZeroed(160);
	// ★ 19.10.2026 (ревизия): ниже три подписки на события сети — сеть обязана подняться раньше, не по случайности порядка.
	Collection.InitializeDependency(UAsdaNetSubsystem::StaticClass());

	if (UGameInstance* GI = GetGameInstance())
	{
		if (UAsdaNetSubsystem* Net = GI->GetSubsystem<UAsdaNetSubsystem>())
		{
			Net->OnQuestStateSync.AddUObject(this, &UAsdaQuestMarkerSubsystem::ApplyQuestStateBlob);
			// ★ 18.10.2026: новый уровень открывает квесты — метки пересчитываем сразу, не ждём фонового тика.
			Net->OnSelfLevelUp.AddWeakLambda(this, [this]()
			{
				bDirty = true;
				RebuildMarks();
			});
			// ★ 18.10.2026: смена профессии (5042) — эталон в конце ветки `jobChangeFlag` зовёт `npcUiRefreshActionList`
			//   (network.c:22023-22024): условия класса (`playerClassMatchesRequirement`) пересчитываются сразу — квесты
			//   новой профессии получают метку, квесты «без профессии» (класс 0) её теряют. Не ждём фонового тика.
			Net->OnSelfJobChanged.AddWeakLambda(this, [this](int32 OldClass, int32 Job, int32 Cls, int32 Sub)
			{
				UE_LOG(LogAsdaQuest, Log, TEXT("[квест-метки] смена профессии (5042): профессия %d, класс %d→%d, подкласс %d — пересчёт меток"),
					Job, OldClass, Cls, Sub);
				bDirty = true;
				RebuildMarks();
			});
		}
	}
	// ★ Волна В4 ремесла (ревизия В3 С-2): эталонный `uiCloseAllWindows@0xD61960` (ui_core.c:54364-54365)
	//   `if (CQuest_Episode_GetState(g_pQuestManager) == 1) questTracker_Hide(g_pQuestManager, 1)` — трекер 1800
	//   гаснет вместе с игровыми окнами. Как и у эталона, `CQuestTracker_BuildWaypointStrings` (у нас
	//   `UpdateTrackerWindow`, каждый кадр) показывает его снова, пока есть записи (quest.c:1170-1171).
	Collection.InitializeDependency(UAsdaWindowSubsystem::StaticClass());
	if (UAsdaWindowSubsystem* WM = GetGameInstance() ? GetGameInstance()->GetSubsystem<UAsdaWindowSubsystem>() : nullptr)
	{
		WM->RegisterWindowCloser(FName(TEXT("Quest_State_Win")), FSimpleDelegate::CreateWeakLambda(this, [this]()
		{
			if (IsTrackerWindowShown()) { TrackerHideWindow(); }
		}));
	}
}

void UAsdaQuestMarkerSubsystem::Deinitialize()
{
	if (UGameInstance* GI = GetGameInstance())
	{
		if (UAsdaNetSubsystem* Net = GI->GetSubsystem<UAsdaNetSubsystem>())
		{
			Net->OnQuestStateSync.RemoveAll(this);
			Net->OnSelfJobChanged.RemoveAll(this);   // ★ 18.10.2026
			Net->OnSelfLevelUp.RemoveAll(this);      // ★ 19.10.2026 (ревизия): подписка из Initialize снимается симметрично
		}
	}
	// Эталон `questTracker_Clear@0xC585B0` чистит записи трекера при выходе/смене персонажа
	//   (`quest.c:1331`, `:4690`). Виджет уносит с собой оконный реестр, нам остаётся состояние.
	TrackerClear();
	Super::Deinitialize();
}

// ★★★ К-3 / находка Р1 отчёта `06_quest_ревизия.md`: состояние квестов не сбрасывалось при выходе
//   в выбор персонажа. Подсистема живёт на `GameInstance`, её `Deinitialize` при выходе из мира НЕ
//   вызывается, а `bStateSynced` не сбрасывался в `false` НИГДЕ. Практика: вошли персонажем A,
//   вышли, вошли персонажем B — `RebuildMarks()` (тикает раз в секунду) до прихода 5047 раздавал
//   метки и строил трекер по квестам ПЕРСОНАЖА A.
//   Комментарий `AsdaQuestLogSubsystem.cpp:2355` утверждал, что записи чистит `TrackerClear` при
//   выходе в выбор персонажа, — это было неверно: у него было ровно три вызова, и ни один не
//   привязан к выходу из мира. Теперь привязан — вот этот.
void UAsdaQuestMarkerSubsystem::ResetSessionState(EAsdaSessionResetReason Reason)
{
	if (!AsdaIsFullSessionReset(Reason))
	{
		// СМЕНА КАРТЫ: журнал квестов на сервере цел, `Slots`/`DoneBits` остаются. Сбрасываем
		// только то, что посчитано ДЛЯ КАРТЫ: метки над NPC умершего мира и баннер ожидания.
		MarkByNpcId.Reset();
		ShownMapId = -1;
		AwaitShownOnMap = -1;
		bAwaitHintPending = false;
		bDirty = true;
		return;
	}

	// ПОЛНЫЙ СБРОС: следующий вход может быть ДРУГИМ персонажем.
	UE_LOG(LogAsdaQuest, Log,
		TEXT("[квесты] сброс сессии (%s): состояние синхронизировано=%d, меток %d, записей трекера %d"),
		AsdaSessionResetReasonName(Reason), bStateSynced ? 1 : 0, MarkByNpcId.Num(), GetTrackEntries().Num());

	for (FAsdaQuestSlot& S : Slots) { S = FAsdaQuestSlot(); }
	FMemory::Memzero(DoneBits.GetData(), DoneBits.Num());
	bStateSynced = false;          // ★ то самое поле, которое не сбрасывалось нигде

	MarkByNpcId.Reset();
	ForcedMarkForAll = -1;
	ShownMapId = -1;
	RebuildAccum = 0.0;
	bDirty = true;

	AwaitMarkup.Empty();
	ShownAwaitMaps.Reset();
	AwaitShownOnMap = -1;
	bAwaitHintPending = false;
	AwaitGate = EAsdaAwaitGate::Unknown;

	TrackerClear();
}

void UAsdaQuestMarkerSubsystem::EnsureTables()
{
	if (bTablesLoaded) { return; }
	bTablesLoaded = true;

	// ★ Аудит №4 (B2/A30): загрузка — через общую точку `AsdaDt::Cached`. `DT_NpcQuestNpc` читает
	//   ещё и `AAsdaNpcActor` (высота плашки), и до сведения таблица грузилась ДВАЖДЫ.
	//   Свои поля-указатели остаются: ниже по ним строятся ИНДЕКСЫ (`ScriptByNsr`, `NpcRows`) —
	//   это уже раскладка конкретного потребителя, ей место здесь.
	ScriptTable = AsdaDt::Cached(kScriptDT);
	NpcTable    = AsdaDt::Cached(kNpcDT);

	ScriptByNsr.Reset();
	NpcRows.Reset();

	if (ScriptTable)
	{
		for (const TPair<FName, uint8*>& It : ScriptTable->GetRowMap())
		{
			const FAsdaNpcQuestScriptRow* R = reinterpret_cast<const FAsdaNpcQuestScriptRow*>(It.Value);
			if (!R) { continue; }
			ScriptByNsr.FindOrAdd(R->NsrName.ToLower()).Add(R);
		}
		for (TPair<FString, TArray<const FAsdaNpcQuestScriptRow*>>& It : ScriptByNsr)
		{
			// Порядок просмотра эталона — по индексу записи в файле (первое совпадение выигрывает).
			It.Value.Sort([](const FAsdaNpcQuestScriptRow& A, const FAsdaNpcQuestScriptRow& B)
			              { return A.RecIndex < B.RecIndex; });
		}
	}
	if (NpcTable)
	{
		for (const TPair<FName, uint8*>& It : NpcTable->GetRowMap())
		{
			if (const FAsdaNpcQuestNpcRow* R = reinterpret_cast<const FAsdaNpcQuestNpcRow*>(It.Value))
			{
				NpcRows.Add(R);
			}
		}
		NpcRows.Sort([](const FAsdaNpcQuestNpcRow& A, const FAsdaNpcQuestNpcRow& B) { return A.NpcId < B.NpcId; });
	}

	// ── Справочники подсказки «Минопио» (`TIM_List.BIN` / `NMS_list.BIN`) ──
	TimTable = AsdaDt::Cached(kTimDT);
	NmsTable = AsdaDt::Cached(kNmsDT);
	TimRows.Reset();
	NmsRows.Reset();
	if (TimTable)
	{
		for (const TPair<FName, uint8*>& It : TimTable->GetRowMap())
		{
			if (const FAsdaTimListRow* R = reinterpret_cast<const FAsdaTimListRow*>(It.Value)) { TimRows.Add(R); }
		}
		// Эталон ищет по этой таблице ЛИНЕЙНО (`204*j`, сравнение `+0`), порядок значения не имеет;
		//   сортируем по MapId только ради воспроизводимого вывода консоли.
		TimRows.Sort([](const FAsdaTimListRow& A, const FAsdaTimListRow& B) { return A.MapId < B.MapId; });
	}
	if (NmsTable)
	{
		// ★ Эталон адресуется ПРЯМО индексом (`44 * mapId`), поэтому здесь нужен массив, у которого
		//   позиция == MapId, а не произвольный порядок строк DataTable.
		int32 MaxId = -1;
		for (const TPair<FName, uint8*>& It : NmsTable->GetRowMap())
		{
			if (const FAsdaNmsListRow* R = reinterpret_cast<const FAsdaNmsListRow*>(It.Value)) { MaxId = FMath::Max(MaxId, R->MapId); }
		}
		NmsRows.SetNumZeroed(MaxId + 1);
		for (const TPair<FName, uint8*>& It : NmsTable->GetRowMap())
		{
			const FAsdaNmsListRow* R = reinterpret_cast<const FAsdaNmsListRow*>(It.Value);
			if (R && NmsRows.IsValidIndex(R->MapId)) { NmsRows[R->MapId] = R; }
		}
	}

	UE_LOG(LogAsdaQuest, Log, TEXT("[квест-метки] подсказка «Минопио»: TIM_List=%d карт, NMS_list=%d строк"),
		TimRows.Num(), NmsRows.Num());
	if (!TimTable || !NmsTable)
	{
		UE_LOG(LogAsdaQuest, Warning, TEXT("[квест-метки] НЕТ таблиц подсказки (%s / %s) — окно 599 ")
			TEXT("«Тебя кое-кто разыскивает…» показываться не будет. Импорт: Tools/asda_import_await_maps.py"),
			kTimDT, kNmsDT);
	}

	UE_LOG(LogAsdaQuest, Log, TEXT("[квест-метки] таблицы: сценариев NSR=%d (NPC-сценариев %d), записей npc_list=%d"),
		ScriptTable ? ScriptTable->GetRowMap().Num() : 0, ScriptByNsr.Num(), NpcRows.Num());
	if (!ScriptTable || !NpcTable)
	{
		UE_LOG(LogAsdaQuest, Warning, TEXT("[квест-метки] НЕТ таблиц (%s / %s) — метки показываться не будут. ")
			TEXT("Импорт: Tools/asda_import_npc_quest_data.py"), kScriptDT, kNpcDT);
	}
}

// ── Состояние квестов (пакет 5047, 688 байт) ────────────────────────────────────────────────────
// Раскладка: 12 слотов по 44 б, затем 160 б битовой карты завершения (её адрес в эталоне —
//   CQuest+1140 = CQuest+0x264 + 528, то есть ровно за слотами).
void UAsdaQuestMarkerSubsystem::ApplyQuestStateBlob(const TArray<uint8>& Blob)
{
	if (Blob.Num() < 688)
	{
		UE_LOG(LogAsdaQuest, Warning, TEXT("[квест-метки] 5047: тело %d б (ожидалось 688) — игнор"), Blob.Num());
		return;
	}
	const uint8* P = Blob.GetData();
	// Своей сборки байт нет (дедуп Д4): порядок и знаковость знает `AsdaBytes`, лямбды лишь держат базу.
	auto I32 = [P](int32 O) { return AsdaBytes::RdI32(&P[O]); };
	auto I16 = [P](int32 O) { return AsdaBytes::RdI16(&P[O]); };

	Slots.SetNum(12);
	int32 Active = 0;
	for (int32 i = 0; i < 12; ++i)
	{
		const int32 B = 44 * i;
		FAsdaQuestSlot& S = Slots[i];
		S.QuestId   = I32(B + 0);
		S.Category  = (int32)P[B + 4];
		S.SlotIndex = I16(B + 5);
		S.ObjType   = (int32)P[B + 7];
		S.QuestNo   = I16(B + 8);
		S.Stage     = I16(B + 10);
		S.ObjIds.SetNum(5); S.ObjCounts.SetNum(5);
		for (int32 j = 0; j < 5; ++j)
		{
			S.ObjIds[j]    = I32(B + 14 + 6 * j);
			S.ObjCounts[j] = I16(B + 18 + 6 * j);
		}
		if (S.QuestId >= 2001) { ++Active; }
	}
	DoneBits.SetNumZeroed(160);
	FMemory::Memcpy(DoneBits.GetData(), P + 528, 160);

	int32 DoneCount = 0;
	for (uint8 Byte : DoneBits) { DoneCount += FMath::CountBits(Byte); }

	bStateSynced = true;
	bDirty = true;
	UE_LOG(LogAsdaQuest, Log, TEXT("[квест-метки] 5047: активных слотов=%d, завершённых квестов=%d"), Active, DoneCount);
	RebuildMarks();
	// ★ Единая точка «состояние журнала изменилось» — на неё подписаны окно 700 и трекер 1800.
	OnQuestStateChanged.Broadcast();
}

// ════════════════════════════════════════════════════════════════════════════════════════════════
//  ЗАПИСЬ СЛОТА ЖУРНАЛА ИЗ ОТВЕТА СЕРВЕРА (без ожидания следующего 5047)
// ════════════════════════════════════════════════════════════════════════════════════════════════
// ★★★ ПОРТ `cquestStoreQuestSlotData@0xC61F60`. Эталон в `CQuest_RecvPacket@0xD9D260`, ветка
//   `case 0:`, копирует 47 байт тела 5044 и раздаёт их в журнал квестов. Раскладка тела снята не
//   с клиента (там IDA слепила аргументы в кашу), а с ОТПРАВИТЕЛЯ на сервере —
//   `GameServer!sub_45CA70@0x45CA70`, где буфер собирается по полям:
//       +0 u8 код · +1 i16 talkId · +3 i32 episodeId · +7 u8 =4 · +8 i16 номер слота ·
//       +10 u8 =2 (состояние «в работе») · +11 i16 номер квеста · +13 i16 стадия ·
//       +15 i16 время приёма · далее 5 пар (i32 id цели, i16 счётчик) с шагом 6 начиная с +17.
//   Раскладка слота журнала (44 б) — та же, что у пакета 5047, см. `ApplyQuestStateBlob`.
// ★ ЗАЧЕМ: без этой записи локальное состояние оставалось «квест не взят», шаг диалога
//   продолжал предлагаться, и повторное нажатие получало от сервера отказ 6. Разбор —
//   `Reports/quest_accept_fix.md`.
int32 UAsdaQuestMarkerSubsystem::ApplyAcceptedQuestSlot(const TArray<uint8>& Body)
{
	if (Body.Num() < 47)
	{
		UE_LOG(LogAsdaQuest, Warning,
			TEXT("[квест-метки] 5044: тело %d б (нужно 47) — слот журнала не записан"), Body.Num());
		return -1;
	}
	const uint8* P = Body.GetData();
	// Своей сборки байт нет (дедуп Д4): порядок и знаковость знает `AsdaBytes`, лямбды лишь держат базу.
	auto I32 = [P](int32 O) { return AsdaBytes::RdI32(&P[O]); };
	auto I16 = [P](int32 O) { return AsdaBytes::RdI16(&P[O]); };

	const int32 EpisodeId = I32(3);
	int32       SlotNo    = I16(8);
	const int32 QuestNo   = I16(11);
	const int32 Stage     = I16(13);

	if (Slots.Num() != 12) { Slots.SetNum(12); }

	// Сервер всегда присылает номер слота (`sub_4D6740` вернул его перед вставкой). Если он всё же
	//   −1 — берём первый свободный, как `questFindFreeSlot@0xC482A0`.
	if (SlotNo < 0 || SlotNo > 11)
	{
		SlotNo = -1;
		for (int32 i = 0; i < 12; ++i) { if (Slots[i].QuestId < 2001) { SlotNo = i; break; } }
		if (SlotNo < 0)
		{
			UE_LOG(LogAsdaQuest, Warning,
				TEXT("[квест-метки] 5044: сервер не указал слот, свободных нет — квест %d не записан"), QuestNo);
			return -1;
		}
	}

	FAsdaQuestSlot& S = Slots[SlotNo];
	S.QuestId   = EpisodeId;
	S.QuestNo   = QuestNo;
	S.Stage     = Stage;
	S.SlotIndex = SlotNo;              // тело 5044 несёт номер ячейки отдельным полем (+8)
	S.Category  = (int32)P[7];         // +7 у отправителя всегда 4 («запись журнала квестов»)
	S.ObjType   = (int32)P[10];        // +10 = 2 «квест в работе» (`sub_45CA70`)
	S.ObjIds.SetNum(5); S.ObjCounts.SetNum(5);
	for (int32 j = 0; j < 5; ++j)
	{
		S.ObjIds[j]    = I32(17 + 6 * j);
		S.ObjCounts[j] = I16(21 + 6 * j);
	}

	bStateSynced = true;   // журнал содержит хотя бы одну достоверную запись
	bDirty = true;
	UE_LOG(LogAsdaQuest, Log,
		TEXT("[квест-метки] 5044: квест %d (эпизод %d, стадия %d) записан в слот %d журнала"),
		QuestNo, EpisodeId, Stage, SlotNo);
	RebuildMarks();
	// ★ Единая точка «состояние журнала изменилось» — на неё подписаны окно 700 и трекер 1800.
	OnQuestStateChanged.Broadcast();
	return SlotNo;
}

// ★ ПОРТ приёма 5042 (`GameServer!recvRewardQuestProcess@0x45CF90`). Сервер отдаёт обновлённую
//   44-байтовую запись слота (`Src`, кладётся в тело по смещению +15) и признак «шаг/квест».
//   Ветка `LOBYTE(v16[2]) == 2` на сервере ЗАВЕРШАЕТ квест: взводит бит карты завершения
//   (`pUser->gap_552E[490]` — та же карта, что приезжает в 5047 по смещению 528) и освобождает
//   слот (`sub_4D6540`). Иначе — просто двигает стадию.
// ★ 28.10.2026 ДОСКА ОБЪЯВЛЕНИЙ — порт `questClearObjectiveEntries@0xC63A80` (quest.c:3612), зовёт приём 6594 код 1.
//   Эталон: `if (questId >= 2001) for i<12: if (слот.+0 == questId && слот.+5 (i16) == номер ячейки) { бит questId карты;
//   memcpy(слот, пустой, 44); }` → скрыть контрол 28 журнала, `questLogRefreshSlotIcons`, `sub_92F459`.
//   ★ [ИСПРАВЛЕНИЕ ОРИГИНАЛА] бит карты завершения — только внутри 160 байт (у заданий доски 20000+ эталон пишет за её концом).
int32 UAsdaQuestMarkerSubsystem::ClearBoardQuestSlot(int32 QuestId, int32 SlotIndex)
{
	if (QuestId < 2001) { return 0; }
	if (Slots.Num() != 12) { Slots.SetNum(12); }
	if (DoneBits.Num() != 160) { DoneBits.SetNumZeroed(160); }
	int32 Cleared = 0;
	for (int32 i = 0; i < 12; ++i)
	{
		if (Slots[i].QuestId != QuestId || Slots[i].SlotIndex != SlotIndex) { continue; }
		if (QuestId < DoneBits.Num() * 8) { DoneBits[QuestId >> 3] |= (uint8)(1u << (QuestId & 7)); }
		Slots[i] = FAsdaQuestSlot();
		++Cleared;
	}
	UE_LOG(LogAsdaQuest, Log, TEXT("[квест-метки] 6594: задание доски %d, ячейка %d — снято слотов журнала %d"), QuestId, SlotIndex, Cleared);
	bDirty = true;
	RebuildMarks();
	OnQuestStateChanged.Broadcast();   // журнал 700, трекер 1800 — перерисовка (эталон `questLogRefreshSlotIcons`)
	return Cleared;
}

// ★ 06.11.2026 Порт `questSlotMoveSwap@0xC5CEE0` — обоснование у объявления в .h.
bool UAsdaQuestMarkerSubsystem::ApplyQuestSlotMove(int32 SrcSlot, int32 DstSlot, bool bSwap)
{
	if (Slots.Num() != 12) { Slots.SetNum(12); }
	if (SrcSlot < 0 || SrcSlot > 11 || DstSlot < 0 || DstSlot > 11 || SrcSlot == DstSlot || Slots[SrcSlot].QuestId < 2001)
	{
		UE_LOG(LogAsdaQuest, Warning, TEXT("[квест-метки] 5001 перенос: ячейки %d → %d не применимы — ничего не тронуто"), SrcSlot, DstSlot);
		return false;
	}
	if (bSwap)
	{
		Swap(Slots[SrcSlot], Slots[DstSlot]);
		Slots[SrcSlot].SlotIndex = SrcSlot;
	}
	else
	{
		Slots[DstSlot] = Slots[SrcSlot];
		Slots[SrcSlot] = FAsdaQuestSlot();   // эталон `CQuest_Slot_Clear`
	}
	Slots[DstSlot].SlotIndex = DstSlot;
	UE_LOG(LogAsdaQuest, Log, TEXT("[квест-метки] 5001 перенос: ячейка %d → %d (%s), задание %d"),
		SrcSlot, DstSlot, bSwap ? TEXT("обмен") : TEXT("в пустую"), Slots[DstSlot].QuestNo);
	bDirty = true;
	RebuildMarks();
	OnQuestStateChanged.Broadcast();   // окно 700, трекер 1800
	return true;
}

// ★ 31.10.2026 Порт `uiCraftSlotRemoveAndReset@0xC61AD0` — обоснование у объявления в .h.
//   Эталон берёт ячейку `acceptSlotIndex` и чистит её, только если поле +5 записи равно номеру ячейки из 5007 (+1); то же
//   здесь, а при `PreferIdx == −1` (ответ пришёл после снятия защёлки по молчанию сервера) ячейка ищется по тому же полю.
//   Сервер в той же ветке (`sub_4D6540`, тип 4) у себя запись уже обнулил — после 5007 ячейка на сервере свободна.
int32 UAsdaQuestMarkerSubsystem::ApplyAbandonAck(int32 ServerSlot, int32 PreferIdx)
{
	if (Slots.Num() != 12) { Slots.SetNum(12); }
	auto Matches = [this, ServerSlot](int32 i) { return Slots[i].QuestId >= 2001 && Slots[i].SlotIndex == ServerSlot; };
	int32 Idx = -1;
	if (PreferIdx >= 0 && PreferIdx < 12 && Matches(PreferIdx)) { Idx = PreferIdx; }
	for (int32 i = 0; Idx < 0 && i < 12; ++i) { if (Matches(i)) { Idx = i; } }
	if (Idx < 0)
	{
		UE_LOG(LogAsdaQuest, Warning, TEXT("[квест-метки] 5007 отказ: ячейки сервера %d в журнале нет (ожидалась ячейка %d) — ничего не снято"),
			ServerSlot, PreferIdx);
		return -1;
	}
	const int32 QuestId = Slots[Idx].QuestId;
	const int32 QuestNo = Slots[Idx].QuestNo;
	Slots[Idx] = FAsdaQuestSlot();   // эталон: memcpy(слот, пустая запись, 44)
	UE_LOG(LogAsdaQuest, Log, TEXT("[квест-метки] 5007 отказ подтверждён: ячейка %d (сервер %d), эпизод %d, задание %d — снята"),
		Idx, ServerSlot, QuestId, QuestNo);
	bDirty = true;
	RebuildMarks();                    // `npcUiRefreshActionList` — метки NPC и миникарты
	OnQuestStateChanged.Broadcast();   // `questLogRefreshSlotIcons` — окно 700, трекер 1800
	return Idx;
}

TArray<int32> UAsdaQuestMarkerSubsystem::ApplyTurnInResult(int32 QuestNo, int32 Stage, bool bFinished, const TArray<uint8>& Body)
{
	TArray<int32> GrantedEpisodes;
	if (Slots.Num() != 12) { Slots.SetNum(12); }
	if (DoneBits.Num() != 160) { DoneBits.SetNumZeroed(160); }

	int32 SlotNo = -1;
	for (int32 i = 0; i < 12; ++i) { if (Slots[i].QuestNo == QuestNo && Slots[i].QuestId >= 2001) { SlotNo = i; break; } }

	if (bFinished)
	{
		if (QuestNo >= 0 && QuestNo < DoneBits.Num() * 8)
		{
			DoneBits[QuestNo >> 3] |= (uint8)(1u << (QuestNo & 7));
		}
		if (SlotNo >= 0)
		{
			Slots[SlotNo] = FAsdaQuestSlot();   // `sub_4D6540` — слот освобождается
		}
		UE_LOG(LogAsdaQuest, Log,
			TEXT("[квест-метки] 5042: квест %d ЗАВЕРШЁН — слот %d освобождён, бит карты завершения взведён"),
			QuestNo, SlotNo);
	}
	else if (SlotNo >= 0)
	{
		Slots[SlotNo].Stage = Stage;
		// Обновлённая запись слота приезжает целиком — если она есть, берём цели из неё.
		if (Body.Num() >= 15 + 44)
		{
			const uint8* P = Body.GetData() + 15;
			// Своей сборки байт нет (дедуп Д4) — см. `AsdaBytes`.
			auto I32 = [P](int32 O) { return AsdaBytes::RdI32(&P[O]); };
			auto I16 = [P](int32 O) { return AsdaBytes::RdI16(&P[O]); };
			FAsdaQuestSlot& S = Slots[SlotNo];
			S.QuestId   = I32(0);
			S.Category  = (int32)P[4];
			S.SlotIndex = I16(5);
			S.ObjType   = (int32)P[7];
			S.QuestNo   = I16(8);
			S.Stage     = I16(10);
			S.ObjIds.SetNum(5); S.ObjCounts.SetNum(5);
			for (int32 j = 0; j < 5; ++j)
			{
				S.ObjIds[j]    = I32(14 + 6 * j);
				S.ObjCounts[j] = I16(18 + 6 * j);
			}
		}
		UE_LOG(LogAsdaQuest, Log,
			TEXT("[квест-метки] 5042: квест %d — стадия %d, слот %d обновлён"), QuestNo, Stage, SlotNo);
	}
	else
	{
		UE_LOG(LogAsdaQuest, Warning,
			TEXT("[квест-метки] 5042: квест %d в журнале не найден — обновлять нечего"), QuestNo);
	}

	// ★★★ ДОБАВЛЕНО 2026-08-22 (`Reports/quest_turnin_notice_fix.md`). Тело 5042 несёт НЕ ОДНУ
	//   запись слота, а ТРИ: `+15` — обновлённая запись сдаваемого квеста (разобрана выше), и
	//   `+59`/`+103` — ДВЕ записи КВЕСТОВ, ВЫДАННЫХ ВМЕСТЕ СО СДАЧЕЙ. Эталон
	//   `net_ApplyPlayerFullUpdate@0xD828A0` (network.c:22026-22038) кладёт их в журнал вызовом
	//   `CQuest_SetActiveSlot@0xC62120`:
	//       if (запись+0 >= 2001 && слот[запись+5] +0 <= 2001) { memcpy(слот, запись, 44);
	//                                                            questLogRefreshSlotIcons();
	//                                                            CQuest_SetEpisodeObjective(слот); }
	//   — то есть НОМЕР ЯЧЕЙКИ берётся из САМОЙ ЗАПИСИ (`+5`, i16), и запись кладётся ТОЛЬКО в
	//   свободную ячейку. Порт этих двух записей не разбирал вовсе ⇒ цепочка «сдал — сразу получил
	//   следующий» обрывалась: новый квест не появлялся ни в журнале, ни в метках до перезахода.
	//   Это тот же класс дефекта, что был у 5044 (см. `quest_accept_fix.md` §4.4).
	for (int32 Off : { 59, 103 })
	{
		if (Body.Num() < Off + 44) { continue; }
		const uint8* P = Body.GetData() + Off;
		// Своей сборки байт нет (дедуп Д4) — см. `AsdaBytes`.
		auto G32 = [P](int32 O) { return AsdaBytes::RdI32(&P[O]); };
		auto G16 = [P](int32 O) { return AsdaBytes::RdI16(&P[O]); };
		const int32 NewEpisode = G32(0);
		if (NewEpisode < 2001) { continue; }                 // эталон: `if (Src >= 2001)`
		const int32 NewSlot = G16(5);                        // ★ номер ячейки — ПОЛЕ записи (+5)
		if (NewSlot < 0 || NewSlot > 11) { continue; }
		if (Slots[NewSlot].QuestId > 2001)                   // эталон: `<= 2001` — только в свободную
		{
			UE_LOG(LogAsdaQuest, Warning,
				TEXT("[квест-метки] 5042: выданный квест (эпизод %d) просится в занятую ячейку %d — эталон её не трогает"),
				NewEpisode, NewSlot);
			continue;
		}
		FAsdaQuestSlot& N = Slots[NewSlot];
		N.QuestId   = NewEpisode;
		N.Category  = (int32)P[4];
		N.SlotIndex = NewSlot;
		N.ObjType   = (int32)P[7];
		N.QuestNo   = G16(8);
		N.Stage     = G16(10);
		N.ObjIds.SetNum(5); N.ObjCounts.SetNum(5);
		for (int32 j = 0; j < 5; ++j)
		{
			N.ObjIds[j]    = G32(14 + 6 * j);
			N.ObjCounts[j] = G16(18 + 6 * j);
		}
		bStateSynced = true;
		GrantedEpisodes.Add(NewEpisode);
		UE_LOG(LogAsdaQuest, Log,
			TEXT("[квест-метки] 5042: вместе со сдачей ВЫДАН квест %d (эпизод %d, стадия %d) — ячейка %d (тело +%d)"),
			N.QuestNo, N.QuestId, N.Stage, NewSlot, Off);
	}

	bDirty = true;
	RebuildMarks();
	// ★ Единая точка «состояние журнала изменилось» — на неё подписаны окно 700 и трекер 1800.
	OnQuestStateChanged.Broadcast();
	return GrantedEpisodes;
}

// ════════════════════════════════════════════════════════════════════════════════════════════════
//  S2C 5052 — ПРОГРЕСС ЦЕЛЕЙ КВЕСТА (порт `questItemResult_apply@0xC63010`, тело 508 б)
// ════════════════════════════════════════════════════════════════════════════════════════════════
// Смещения выведены из эталона по базе `CQuest+1324` (`memcpy(this+1324, pkt+9, 0x1FC)`):
//   `*(int*)(this+1337)` → тело +13 (ветка) · `*(u8*)(this+1328)` → тело +4 (objType) ·
//   `(char*)this+1346` (11 int) → тело +22 = ЗАПИСЬ СЛОТА ЖУРНАЛА · `*(i16*)(this+1351)` → тело +27
//   = слот+5 = номер слота журнала · `*(u8*)(this+1353)` → тело +29 = слот+7 ·
//   цели `*(u32*)((char*)this + 6*j + 1360)` → тело +36+6j = слот+14+6j.
// ⇒ по +22 лежит РОВНО тот же 44-байтовый слот, что и в 5047, и его надо положить в журнал.
// ★ Ветка «objType == 1 && тело+13 > 0» в эталоне журнал НЕ переписывает: там срабатывает
//   `missionSetEntryFlag` (флаг задания-миссии) и полное обновление характеристик персонажа.
//   Возвращаем −1: слот не трогали.
int32 UAsdaQuestMarkerSubsystem::ApplyQuestProgressBlob(const TArray<uint8>& Body, bool* bOutQuestFinished,
	FAsdaQuestProgressDelta* OutDelta)
{
	if (bOutQuestFinished) { *bOutQuestFinished = false; }
	if (Body.Num() < 66)   // 22 + 44 — минимум, при котором есть запись слота
	{
		UE_LOG(LogAsdaQuest, Warning,
			TEXT("[квест-метки] 5052: тело %d б (нужно не меньше 66) — прогресс не применён"), Body.Num());
		return -1;
	}
	const uint8* P = Body.GetData();
	// Своей сборки байт нет (дедуп Д4): порядок и знаковость знает `AsdaBytes`, лямбды лишь держат базу.
	auto I32 = [P](int32 O) { return AsdaBytes::RdI32(&P[O]); };
	auto I16 = [P](int32 O) { return AsdaBytes::RdI16(&P[O]); };

	const int32 ObjType   = (int32)P[4];
	const int32 FinishSel = I32(13);
	const int32 SlotNo    = I16(27);
	const int32 QuestId   = I32(22);
	const int32 QuestNo   = I16(22 + 8);
	const int32 Stage     = I16(22 + 10);

	// Эталонная развилка 1:1: две ветки, и каждая ОТСЕКАЕТ чужой objType ранним возвратом.
	if (FinishSel > 0)
	{
		if (ObjType != 1)
		{
			UE_LOG(LogAsdaQuest, Verbose,
				TEXT("[квест-метки] 5052: ветка завершения при objType=%d — эталон здесь выходит"), ObjType);
			return -1;
		}
		UE_LOG(LogAsdaQuest, Log,
			TEXT("[квест-метки] 5052: ветка завершения (квест %d, эпизод %d) — журнал не переписывается"),
			QuestNo, QuestId);
		// ★ Эталон играет здесь звук 14 (`csoundPlayName2DStore(g_pSoundMgr, 14, 0)`, quest.c:3454).
		//   Сама подсистема звук не играет (её дело — состояние), признак уходит вызывающему.
		if (bOutQuestFinished) { *bOutQuestFinished = true; }
		bDirty = true;

	RebuildMarks();
		OnQuestStateChanged.Broadcast();
		return -1;
	}
	if (ObjType == 1)
	{
		UE_LOG(LogAsdaQuest, Verbose, TEXT("[квест-метки] 5052: objType=1 без ветки завершения — эталон выходит"));
		return -1;
	}
	if (SlotNo < 0 || SlotNo > 11)
	{
		UE_LOG(LogAsdaQuest, Warning, TEXT("[квест-метки] 5052: номер слота %d вне 0…11 — пропуск"), SlotNo);
		return -1;
	}
	if (Slots.Num() != 12) { Slots.SetNum(12); }

	FAsdaQuestSlot& S = Slots[SlotNo];
	const int32 PrevSum = S.ObjCounts.Num() ? S.ObjCounts[0] : -1;
	// ★ Снимок ДО перезаписи — по нему видно, какая цель выросла (нужно экранной строке прогресса).
	const TArray<int32> OldIds    = S.ObjIds;
	const TArray<int32> OldCounts = S.ObjCounts;
	// ★ 25.10.2026 (баг «2/0»): стадия до пакета — только если слот держал ТО ЖЕ задание (эталон сравнивает
	//   эпизод и номер задания старой и новой записи, `ItemSlot_FormatQuantityTooltip@0xC64050`).
	const int32 OldStage = (S.QuestId == QuestId && S.QuestNo == QuestNo) ? S.Stage : -1;
	S.QuestId   = QuestId;
	S.Category  = (int32)P[22 + 4];
	S.SlotIndex = SlotNo;
	S.ObjType   = (int32)P[22 + 7];
	S.QuestNo   = QuestNo;
	S.Stage     = Stage;
	S.ObjIds.SetNum(5); S.ObjCounts.SetNum(5);
	for (int32 j = 0; j < 5; ++j)
	{
		S.ObjIds[j]    = I32(22 + 14 + 6 * j);
		S.ObjCounts[j] = I16(22 + 18 + 6 * j);
	}

	// ★ Первая цель, у которой счётчик прибавился: эталон (`itemSlot_DiffGridSnapshot`) показывает
	//   на экране ровно ОДНУ выросшую цель, а не весь список.
	if (OutDelta)
	{
	for (int32 j = 0; j < 5; ++j)
	{
		if (S.ObjIds[j] == 0) { continue; }
		int32 Was = 0;
		for (int32 k = 0; k < OldIds.Num(); ++k)
		{
			if (OldIds[k] == S.ObjIds[j])
			{
				Was = OldCounts.IsValidIndex(k) ? OldCounts[k] : 0;
				break;
			}
		}
		if (S.ObjCounts[j] > Was)
		{
			OutDelta->bValid   = true;
			OutDelta->SlotNo   = SlotNo;
			OutDelta->QuestNo  = QuestNo;
			OutDelta->ItemId   = S.ObjIds[j];
			OutDelta->NewCount = S.ObjCounts[j];
			OutDelta->OldCount = Was;
			OutDelta->OldStage = OldStage;
			OutDelta->NewStage = Stage;
			break;
		}
	}
	}

	bStateSynced = true;
	bDirty = true;
	UE_LOG(LogAsdaQuest, Log,
		TEXT("[квест-метки] 5052: слот %d — квест %d стадия %d, цели [%d,%d,%d,%d,%d] (было %d у первой)"),
		SlotNo, QuestNo, Stage, S.ObjCounts[0], S.ObjCounts[1], S.ObjCounts[2], S.ObjCounts[3], S.ObjCounts[4], PrevSum);
	RebuildMarks();
	OnQuestStateChanged.Broadcast();
	return SlotNo;
}
