// Copyright (c) 2026 @sitxovski (Discord)
// AsdaQuestLogSubsystem_Tracker.cpp — ★ R3-c 12.11.2026 (разрез AsdaQuestLogSubsystem.cpp, дословный перенос): трекер на экране: строки прогресса, записи трекера, сохранение отслеживаемых, справочные функции, Describe.
//   Общее для частей — в AsdaQuestLogSubsystem_Private.h; шапка и реверс — в AsdaQuestLogSubsystem.cpp.
#include "Quest/AsdaQuestLogSubsystem.h"
#include "AsdaDevConsole.h"   // ★ античит А2: ASDA_DEV_CVAR / ASDA_DEV_CONSOLE — дев-ключи и команды вне Shipping
#include "UI/AsdaJuiWindowHandle.h"   // ★ волна 3 (15.09.2026): каркас окон JUI

#include "Quest/AsdaQuestMarkerSubsystem.h"
#include "Npc/AsdaNpcDialogTypes.h"          // FAsdaEpisodeRow, FAsdaQuestRewardTableRow
#include "UI/AsdaWindowSubsystem.h"
#include "UI/AsdaMessageSubsystem.h"
#include "UI/AsdaTipPanel.h"   // ★ общая точка Resize9Slice (порт uiPanel9SliceLayout)
#include "UI/AsdaUiHelpers.h"
#include "UI/AsdaInventorySubsystem.h"
#include "Text/AsdaTextSubsystem.h"
#include "Login/AsdaText.h"   // ★ 09.10.2026: FAsdaText::Get — название рейд-задания из текстовой таблицы
#include "Items/AsdaItemTableSubsystem.h"
#include "Resources/AsdaSoundSubsystem.h"
#include "World/AsdaEntityText.h"   // ★ общая точка «имя NPC/моба по id» (волна 3 п.14) — второй копии нет
#include "World/AsdaWorldSubsystem.h"      // ★ имя своего персонажа — ключ сохранения списка отслеживаемых
#include "AsdaNetSubsystem.h"
#include "Npc/AsdaNpcDialogSubsystem.h"   // ★ 19.10.2026: награды завершения 5052 — `ApplyQuestCompletionRewards5052`
#include "AsdaDataTableCache.h"

#include "Components/CheckBox.h"
#include "Components/Image.h"
#include "Components/TextBlock.h"
#include "Engine/DataTable.h"
#include "Engine/Engine.h"
#include "Kismet/GameplayStatics.h"
#include "UI/AsdaCraftSubsystem.h"   // ★ волна В3 ремесла: единый предикат «окно ремесла открыто» (`IsAnyCraftWindowOpen`)
#include "Misc/ConfigCacheIni.h"            // ★ GConfig: список отслеживаемых заданий между сессиями
#include "World/AsdaFishingSubsystem.h"     // ★ 08.10.2026 (рыбалка В4): гейт «идёт рыбалка» кнопок окна заданий (событие 66561)
#include "Text/AsdaLangRows.h"               // [ПОРТ] 09.10.2026 (мультиязычность): тексты книги задания / эпизода / рейда на языке сессии
#include "Quest/AsdaBoardQuestSubsystem.h"   // ★ 28.10.2026: задания доски объявлений 20000…30000 — тип, награды, готовность, имя
#include "UI/AsdaNotice.h"                   // ★ 31.10.2026: нотис провала отказа от задания (5007) — канал эталонного 6956
#include "Quest/AsdaQuestGuideTarget.h"      // ★ 31.10.2026 (баг 6): место цели строки гида без координат — общая точка с картой мира
#include "UI/AsdaTutorialEvents.h"   // ★ 10.11.2026 обучение: флаги шагов гида 1950 (TUTORIAL.md §2.3)
#include "AsdaQuestLogSubsystem_Private.h"   // ★ R3-c 12.11.2026: общее для частей AsdaQuestLogSubsystem*.cpp

// ★★★ Две таблицы, импортированные 2026-08-22 вместе с экранным трекером 1800: без них строку
//   «предмет : есть/надо» построить нечем (см. `Reports/quest_tracker.md` §5.3).
static const TCHAR* kSmInfoDT  = TEXT("/Game/AsdaAssets/Data/Quest/DT_SmInfo.DT_SmInfo");

// ★ Показать строку прогресса НА ЭКРАНЕ — окно 221 (`WARMESSAGE_WIN`), у которого нет подложки,
//   поэтому текст читается как «нотис без рамки». Цвет −256 (чистый жёлтый) и время жизни 10 с —
//   ровно как в эталоне (`uiChatLogAddLine(…, -256, 0, 10000.0, 1,0,0, 255)`).
//   ★ ОТСТУПЛЕНИЕ (заявка владельца 24.08.2026): добавлена ОБВОДКА текста — в оригинале у этой
//   строки только тень, и на светлом фоне её было плохо видно. Ключ отката:
//   `Asda.Quest.ProgressOutline 0`.
void UAsdaQuestLogSubsystem::ShowQuestProgressOnScreen(const FString& Line)
{
	if (Line.IsEmpty()) { return; }
	UAsdaMessageSubsystem* Msg = GetGameInstance()
		? GetGameInstance()->GetSubsystem<UAsdaMessageSubsystem>() : nullptr;
	if (!Msg) { return; }

	FAsdaMsgParams MP;
	MP.Category  = EAsdaMsgCategory::Quest;
	MP.Text      = Line;
	MP.Targets   = (int32)EAsdaMsgTarget::WarWindow;   // именно 221, не журнал
	MP.bUseColor = true;
	MP.Color     = FColor(0xFF, 0xFF, 0x00, 0xFF);     // −256 эталона
	MP.TtlMs     = 10000.f;
	// ★ ОБВОДКА — заявка владельца. Эталон у этой строки даёт только тень, и на светлом фоне
	//   жёлтый плохо читался. Тег общий для всего UI, свою обводку никто не рисует.
	//   Ключ отката к оригиналу: `Asda.Quest.ProgressOutline 0`.
	static const auto* CVarOutline = ASDA_FIND_DEV_CVAR(TEXT("Asda.Quest.ProgressOutline"));
	if (!CVarOutline || CVarOutline->GetInt() != 0) { MP.StyleTag = TEXT("<FO>"); }
	Msg->Post(MP);
	UE_LOG(LogAsdaQuestLog, Log, TEXT("[квест] прогресс на экран: «%s»%s"), *Line,
		MP.StyleTag.IsEmpty() ? TEXT("") : TEXT(" (с обводкой)"));
}

// ★ «Сколько НАДО» этого предмета на текущем шаге задания. ОДНА формула на систему: обычные
//   задания берут требования из под-записи шага (`DT_SmInfo`), ремесленные 20000…30000 — из
//   `DT_RaidQuestData`. Ею пользуются и строки трекера, и экранная строка прогресса.
int32 UAsdaQuestLogSubsystem::NeedCountForItem(const FAsdaQuestSlot& Slot, int32 ItemCode) const
{
	HoldGameTables();   // ★ 15.09 (dt_gc_hold): таблицы — сильно у подсистемы на сессию
	if (ItemCode <= 0) { return 0; }
	const bool bCraft = (Slot.QuestNo >= 20000 && Slot.QuestNo <= 30000);
	if (bCraft)
	{
		if (const FAsdaRaidQuestRow* R = AsdaDt::RowByInt<FAsdaRaidQuestRow>(kRaidDT, Slot.QuestNo, TEXT("QuestLog")))
		{
			for (int32 k = 0; k < R->ReqItemIds.Num(); ++k)
			{
				if (R->ReqItemIds[k] == ItemCode)
				{
					return R->ReqItemCounts.IsValidIndex(k) ? R->ReqItemCounts[k] : 0;
				}
			}
		}
		return 0;
	}
	const FAsdaEpisodeRow*   Ep = EpisodeRow(Slot.QuestId);
	const FAsdaQuestBookRow* B  = Ep ? BookRow(Ep->QstFile) : nullptr;
	if (!B) { return 0; }
	const int32 Step  = FMath::Clamp(Slot.Stage, 0, B->StepSmInfoIds.Num() - 1);
	const int32 RecId = B->StepSmInfoIds.IsValidIndex(Step) ? B->StepSmInfoIds[Step] : -1;
	const FAsdaSmInfoRow* Sm = (RecId >= 0)
		? AsdaDt::RowByInt<FAsdaSmInfoRow>(kSmInfoDT, RecId, TEXT("QuestLog"))
		: nullptr;
	if (!Sm) { return 0; }
	for (int32 i = 0; i < Sm->ReqItemIds.Num(); ++i)
	{
		if (Sm->ReqItemIds[i] == ItemCode)
		{
			return Sm->ReqItemCounts.IsValidIndex(i) ? Sm->ReqItemCounts[i] : 0;
		}
	}
	return 0;
}

// ★ 25.10.2026 (баг «2/0»): ключ отката ветки «стадия выросла» экранной строки прогресса (см. `BuildQuestProgressLine`).
//   1 = как эталон (`ItemSlot_FormatQuantityTooltip@0xC64050`), 0 = прежнее поведение порта.
static ASDA_DEV_CVAR(int32, CVarQuestProgressNeedPrevStage,
	TEXT("asda.Quest.ProgressNeedPrevStage"), 1,
	TEXT("Экранная строка прогресса задания: 1 = «надо» из прежней стадии, если последний предмет перевёл задание дальше (эталон); 0 = прежнее (текущая стадия, «N/0»)"),
	ECVF_Default);

// ★ Строка прогресса ровно эталонного формата: «< Задание > Предмет : есть/надо ».
//   Хвостовой пробел — из оригинала (`item_inventory.c:22456`), у прогресс-варианта он есть.
bool UAsdaQuestLogSubsystem::BuildQuestProgressLine(const FAsdaQuestProgressDelta& Delta, FString& OutLine) const
{
	HoldGameTables();   // ★ 15.09 (dt_gc_hold): таблицы — сильно у подсистемы на сессию
	if (!Delta.bValid) { return false; }
	UAsdaQuestMarkerSubsystem* QM = const_cast<UAsdaQuestLogSubsystem*>(this)->Marks();
	const FAsdaQuestSlot* S = QM ? SlotAt(Delta.SlotNo) : nullptr;
	if (!S) { return false; }

	// Название задания — теми же таблицами, что и заголовок записи трекера.
	FString Title;
	if (S->QuestNo >= 20000 && S->QuestNo <= 30000)
	{
		if (const FAsdaRaidQuestRow* R = AsdaDt::RowByInt<FAsdaRaidQuestRow>(kRaidDT, S->QuestNo, TEXT("QuestLog")))
		{
			Title = QLogLang::RaidName(R);   // [ПОРТ] 09.10.2026 (мультиязычность)
		}
	}
	else
	{
		const FAsdaEpisodeRow*   Ep = EpisodeRow(S->QuestId);
		const FAsdaQuestBookRow* B  = Ep ? BookRow(Ep->QstFile) : nullptr;
		// ★ [ПОРТ] 09.10.2026 (мультиязычность): пустота проверяется по РУССКОМУ полю (как раньше), показ — на языке сессии.
		if (B) { Title = B->Title.IsEmpty() ? QLogLang::EpTitle(Ep) : QLogLang::BookTitle(B); }
		else if (Ep) { Title = QLogLang::EpTitle(Ep); }
	}
	if (Title.IsEmpty()) { Title = FString::Printf(TEXT("#%d"), S->QuestNo); }

	UAsdaItemTableSubsystem* Items = GetGameInstance()
		? GetGameInstance()->GetSubsystem<UAsdaItemTableSubsystem>() : nullptr;
	FString ItemName = Items ? Items->GetItemName(Delta.ItemId) : FString();
	if (ItemName.IsEmpty()) { ItemName = FString::Printf(TEXT("#%d"), Delta.ItemId); }

	// ★★★ [ОРИГИНАЛ] 25.10.2026 (баг игроков «2/0 вместо 2/2» у последнего моба). Последний предмет переводит задание на
	//   следующую стадию В ТОМ ЖЕ 5052, а у новой стадии под-записи требований нет (−1) — «надо» выходило 0. Эталон
	//   `ItemSlot_FormatQuantityTooltip@0xC64050` различает две ветки по стадии старой и новой записи слота:
	//     · стадия НЕ выросла (`new <= old` или поле < 0) — под-запись книги `+2708 + 68·стадия` (текущая), формат
	//       «< %s > %s : %d/%d » с хвостовым пробелом;
	//     · стадия ВЫРОСЛА — под-запись `+2640 + 68·стадия`, то есть стадии (новая − 1), на которой предмет и
	//       набирался; формат БЕЗ хвостового пробела.
	//   Ремесленные 20000…30000 «надо» берут из `Raid_quest_data` (от стадии не зависит), формат без пробела, а при
	//   `Mode == 1` «есть» = штук в сумке (`CInventory_CountItemById`).
	//   [ИСПРАВЛЕНИЕ] — не повторяем: при НЕвыросшей стадии и уже набранных требованиях (`matchFiveSlotIdPairs`) эталон
	//   строку не пересобирает и показывает прошлое содержимое общего буфера (прежнюю строку «1/2»); порт пишет «N/N».
	//   Ключ отката `asda.Quest.ProgressNeedPrevStage 0` — прежнее поведение порта (всегда текущая стадия, с пробелом).
	const bool bCraft = (S->QuestNo >= 20000 && S->QuestNo <= 30000);
	const bool bOrig = CVarQuestProgressNeedPrevStage.GetValueOnGameThread() != 0;
	const bool bStageUp = bOrig && !bCraft && Delta.OldStage >= 0 && Delta.NewStage > Delta.OldStage;
	FAsdaQuestSlot NeedSlot = *S;
	if (bStageUp) { NeedSlot.Stage = Delta.NewStage - 1; }
	const int32 Need = NeedCountForItem(NeedSlot, Delta.ItemId);
	int32 Have = Delta.NewCount;
	if (bOrig && bCraft)
	{
		const FAsdaRaidQuestRow* R = AsdaDt::RowByInt<FAsdaRaidQuestRow>(kRaidDT, S->QuestNo, TEXT("QuestLog"));
		if (R && R->Mode == 1) { Have = QM->CountItemById(Delta.ItemId); }
	}
	const bool bTrailingSpace = !bOrig || (!bCraft && !bStageUp);
	OutLine = FString::Printf(TEXT("< %s > %s : %d/%d%s"), *Title, *ItemName, Have, Need, bTrailingSpace ? TEXT(" ") : TEXT(""));
	return true;
}

bool UAsdaQuestLogSubsystem::RebuildTrackerEntry(int32 SlotIdx, bool bSilent)
{
	HoldGameTables();   // ★ 15.09 (dt_gc_hold): таблицы — сильно у подсистемы на сессию
	UAsdaQuestMarkerSubsystem* QM = Marks();
	const FAsdaQuestSlot* S = SlotAt(SlotIdx);
	if (!QM || !IsSlotBusy(S)) { return false; }

	// Эталон первым делом спрашивает `charStateFindAndBuildRequest` — «условия квеста уже
	//   выполнены?». Если да, задание СНИМАЕТСЯ с отслеживания и галка гаснет.
	if (QM->IsQuestObjectiveDone(S->QuestNo))
	{
		if (!bSilent && QM->TrackerRemoveEntryDone(S->QuestNo)) { SaveTrackedToSettings(); }   // ★ 31.10: точка → NPC сдачи
		return false;
	}

	UAsdaItemTableSubsystem* Items = GetGameInstance() ? GetGameInstance()->GetSubsystem<UAsdaItemTableSubsystem>() : nullptr;
	auto ItemName = [Items](int32 Code) -> FString
	{
		const FString N = Items ? Items->GetItemName(Code) : FString();
		return N.IsEmpty() ? FString::Printf(TEXT("#%d"), Code) : N;
	};
	// «Сколько уже есть» по коду предмета — из счётчиков целей слота (5 пар), как в эталоне.
	auto CurCountOf = [S](int32 Code) -> int32
	{
		for (int32 j = 0; j < S->ObjIds.Num(); ++j)
		{
			if (S->ObjIds[j] == Code) { return S->ObjCounts.IsValidIndex(j) ? S->ObjCounts[j] : 0; }
		}
		return 0;
	};

	const bool bCraft = (S->QuestNo >= 20000 && S->QuestNo <= 30000);
	int32 Written = 0;

	if (!bCraft)
	{
		// ── Ветка A: обычное задание ──
		const FAsdaEpisodeRow* Ep = EpisodeRow(S->QuestId);
		const FAsdaQuestBookRow* B = Ep ? BookRow(Ep->QstFile) : nullptr;
		if (!B) { return false; }
		// Под-запись ШАГА: индекс = стадия задания (эталон берёт её из того же слота, поле +6).
		const int32 Step = FMath::Clamp(S->Stage, 0, B->StepSmInfoIds.Num() - 1);
		const int32 RecId = B->StepSmInfoIds.IsValidIndex(Step) ? B->StepSmInfoIds[Step] : -1;
		// ★★★ ОТСТУПЛЕНИЕ ОТ ЭТАЛОНА (заявка владельца 2026-08-23, разбор — `quest_tracker_fix.md` §Д1).
		//   ЭТАЛОН: `itemListRec = findRecordById_C10(g_pNpcWnd, recId); if (!itemListRec) return 0;`
		//   — у разговорного задания (сходи/осмотри/поговори) под-запись шага в `.QST` равна −1,
		//   записи с ключом −1 в `SMInfo.BIN` нет, построитель выходит нулём, и обработчик галки 743
		//   гасит её МОЛЧА. Так ведёт себя оригинал, и так вёл себя порт. Пример владельца —
		//   «Осмотр собирателя живых камней» (`25_Qst_1.QST`, все 10 под-записей = −1);
		//   таких книг 195 из 442.
		//   ПОРТ: строк «предмет : есть/надо» у такого задания не бывает по природе, но ЦЕЛЬ С
		//   КООРДИНАТАМИ у него есть (`QDT`), то есть расстояние и стрелка считаются полностью.
		//   Поэтому запись всё равно кладём — заголовком без строк (ниже, ветка `Written == 0`).
		//   Ключ отката к оригиналу: `Asda.Quest.TrackerNoItemQuests 0`.
		const FAsdaSmInfoRow* Sm = (RecId >= 0)
			? AsdaDt::RowByInt<FAsdaSmInfoRow>(kSmInfoDT, RecId, TEXT("QuestLog"))
			: nullptr;
		if (!Sm && !UAsdaQuestMarkerSubsystem::TrackerAllowNoItemQuests())
		{
			return false;                        // эталонный выход «нет требований по предметам»
		}

		const FString Title = B->Title.IsEmpty() ? QLogLang::EpTitle(Ep) : QLogLang::BookTitle(B);   // [ПОРТ] 09.10.2026 (мультиязычность)
		for (int32 i = 0; Sm && i < Sm->ReqItemIds.Num() && Written < 3; ++i)
		{
			const int32 Code = Sm->ReqItemIds[i];
			if (Code <= 0) { continue; }
			const int32 Need = Sm->ReqItemCounts.IsValidIndex(i) ? Sm->ReqItemCounts[i] : 0;
			if (bSilent) { ++Written; continue; }
            const FString Line = FString::Printf(TEXT("%s : %d/%d "), *ItemName(Code), CurCountOf(Code), Need);
			if (!QM->TrackerUpdateEntry(S->QuestNo, S->QuestNo, Title, Line, Written)) { return false; }
			++Written;
		}
	}
	else
	{
		// ── Ветка B: крафт/рейд-задание ──
		const FAsdaRaidQuestRow* R = AsdaDt::RowByInt<FAsdaRaidQuestRow>(kRaidDT, S->QuestNo, TEXT("QuestLog"));
		if (!R) { return false; }
		// Эталон: при `Mode == 1` («считать по сумке») запись в трекер вообще НЕ идёт — она тут же
		//   снимается. Повторяем.
		// ★ 28.10.2026: и «готово к сдаче» (`craftCheckRequirementItemsMet(g_pQuestCraftWnd, …) || режим == 1`, quest.c:3943).
		const UAsdaBoardQuestSubsystem* Board = GetGameInstance() ? GetGameInstance()->GetSubsystem<UAsdaBoardQuestSubsystem>() : nullptr;
		if (R->Mode == 1 || (Board && Board->IsQuestReady(S->QuestNo)))
		{
			if (!bSilent && QM->TrackerRemoveEntry(S->QuestNo)) { SaveTrackedToSettings(); }
			return false;
		}
		for (int32 k = 0; k < R->ReqItemIds.Num() && Written < 3; ++k)
		{
			const int32 Code = R->ReqItemIds[k];
			if (Code <= 0) { continue; }
			// ★ Эталон в этой ветке пишет строку ТОЛЬКО для предметов, которые есть среди целей
			//   слота (`itemMatched2 == 1`); прочие требования в трекер не попадают.
			bool bMatched = false;
			for (int32 j = 0; j < S->ObjIds.Num(); ++j) { if (S->ObjIds[j] == Code) { bMatched = true; break; } }
			if (!bMatched) { continue; }
			const int32 Need = R->ReqItemCounts.IsValidIndex(k) ? R->ReqItemCounts[k] : 0;
			if (bSilent) { ++Written; continue; }
            const FString Line = FString::Printf(TEXT("%s : %d/%d "), *ItemName(Code), CurCountOf(Code), Need);
			if (!QM->TrackerUpdateEntry(S->QuestNo, S->QuestNo, QLogLang::RaidName(R), Line, Written)) { return false; }   // [ПОРТ] 09.10.2026 (мультиязычность)
			++Written;
		}
	}

	// Эталон возвращает 1 и в том случае, когда ни одной строки не написалось (у обычной ветки),
	//   но нам такая запись бесполезна: без строк в трекере остался бы один заголовок без целей.
	//   Поэтому заголовок кладём ОТДЕЛЬНО — задание с текстовой целью (ObjType 0) всё равно должно
	//   быть видно в трекере: у него есть и расстояние, и стрелка.
	if (Written == 0)
	{
		const FAsdaEpisodeRow* Ep = EpisodeRow(S->QuestId);
		const FAsdaQuestBookRow* B = Ep ? BookRow(Ep->QstFile) : nullptr;
		const FString Title = B && !B->Title.IsEmpty() ? QLogLang::BookTitle(B) : QLogLang::EpTitle(Ep);   // [ПОРТ] 09.10.2026 (мультиязычность)
		if (Title.IsEmpty()) { return false; }
		// ★ Пускаем в трекер ТОЛЬКО задание, у которого есть цель с координатами: иначе в окне
		//   висел бы заголовок без строк, без расстояния и без стрелки — показывать нечего.
		//   Проверка идёт ТОЙ ЖЕ функцией, которой трекер каждый кадр ищет цель (второго
		//   разбора `DT_QuestGuide` в порту нет); карта здесь не важна, поэтому −1.
		UAsdaQuestMarkerSubsystem::FAsdaTrackTarget Probe;
		if (!QM->GetTrackTarget(S->QuestNo, -1, Probe))
		{
			if (!bSilent)
			{
				UE_LOG(LogAsdaQuestLog, Log,
					TEXT("[журнал] задание %d: ни строк целей, ни координат в QDT — в трекер не берём"),
					S->QuestNo);
			}
			return false;
		}
		if (bSilent) { return true; }   // проверка «а можно ли» — дальше только запись
		if (!QM->TrackerUpdateEntry(S->QuestNo, S->QuestNo, Title, FString(), 0)) { return false; }
		// Строк условий нет — показываем только заголовок с дистанцией.
		QM->TrackerSetLineCount(S->QuestNo, 0);
	}
	return true;
}

// ★★★ ЕДИНСТВЕННЫЙ ПУТЬ «журнал изменился → привести трекер в соответствие». Зовётся на КАЖДОЕ
//   изменение состояния квестов, откуда бы оно ни пришло: 5047 (полный журнал после сдачи/приёма/
//   отказа), 5052 (прогресс целей), 5042 (три записи слотов в ответе на сдачу). Поэтому «задание
//   исчезло из журнала → запись исчезла из трекера» работает для ВСЕХ путей сразу, а не для
//   перечисленного списка кнопок.
void UAsdaQuestLogSubsystem::RebuildAllTrackerEntries()
{
	UAsdaQuestMarkerSubsystem* QM = Marks();
	if (!QM) { return; }

	// Восстановление списка после входа в игру (если ещё не сделано) — ровно здесь: состояние
	//   журнала уже пришло, значит есть с чем сверять сохранённые номера заданий.
	RestoreTrackedFromSettings();

	// Снимок номеров заданий: `TrackerUpdateEntry` по эталону всплывает обновлённую запись на
	//   нулевое место, то есть массив во время перестройки переупорядочивается — идти по нему
	//   напрямую нельзя.
	TArray<int32> Snapshot;
	for (const FAsdaQuestTrackEntry& E : QM->GetTrackEntries()) { Snapshot.Add(E.QuestId); }

	bool bDropped = false;
	for (int32 QuestNo : Snapshot)
	{
		// Ячейка, в которой это задание лежит СЕЙЧАС (после 5042/5047 она могла смениться).
		int32 Slot = INDEX_NONE;
		for (int32 i = 0; i < 12; ++i)
		{
			const FAsdaQuestSlot* S = SlotAt(i);
			if (IsSlotBusy(S) && S->QuestNo == QuestNo) { Slot = i; break; }
		}
		if (Slot == INDEX_NONE)
		{
			// Задания в журнале больше нет: сдано, отменено, вытеснено — запись уходит сама.
			bDropped = QM->TrackerRemoveEntry(QuestNo) || bDropped;
			continue;
		}
		RebuildTrackerEntry(Slot);   // внутри он же снимет запись, если условия уже выполнены
	}
	if (bDropped) { SaveTrackedToSettings(); }
}

// ══ ★ СОХРАНЕНИЕ СПИСКА ОТСЛЕЖИВАЕМЫХ ЗАДАНИЙ МЕЖДУ СЕССИЯМИ ═════════════════════════════════
//   ЭТАЛОН ЭТОГО НЕ ДЕЛАЕТ (`Reports/quest_tracker.md` §5.4: записи живут только в памяти,
//   `questTracker_Clear` чистит их при выходе; в `ChatOpt.bin` и прочих сохранялках трекера нет) —
//   значит это ОСОЗНАННОЕ ДОПОЛНЕНИЕ порта по требованию владельца, помеченное как отступление.
//   ГДЕ ХРАНИМ: тот же `GGameUserSettingsIni`, в котором порт уже держит `[AsdaWindowLayout]`,
//   `[AsdaLogin]`, `[AsdaVideo]`, `[AsdaKeybinds]` — второго механизма настроек не заводим.
//   ЧТО ХРАНИМ: номера ЗАДАНИЙ (`QuestNo`), а не ячеек: ячейки принадлежат серверу и в следующем
//   входе будут другими. Ключ — имя персонажа: у каждого свой список.
const TCHAR* UAsdaQuestLogSubsystem::kTrackerSection = TEXT("AsdaQuestTracker");

// ★★★ ИСПРАВЛЕНО 2026-08-23 (заявка владельца Е2, разбор — `quest_tracker_fix2.md` §Е2).
//   БЫЛО: пока имени персонажа нет, ключ подставлялся как `Tracked.Default`. Это и ломало
//   сохранение между сессиями: журнал (5047) приходит в стартовом бурсте сервера, а имя своего
//   персонажа порт узнаёт ПОЗЖЕ — в `UAsdaWorldSubsystem::TrySpawnLocalPlayer`, которая при
//   4003 «до загрузки уровня» откладывается до `PostLoadMap`. Значит восстановление шло под
//   `Tracked.Default`, `TrackedRestoredFor` запоминал именно его, а когда имя появлялось,
//   замок `TrackedRestoredFor != TrackedSettingsKey()` в `SaveTrackedToSettings` срабатывал
//   НАВСЕГДА — в файл не писалось ничего. Ровно та же гонка, что была с подсказкой робота.
//   СТАЛО: пока имени нет, ключа НЕТ (пустая строка) — и мы просто ЖДЁМ (см. `Tick`).
//   Никакого «Default» больше не существует: список принадлежит персонажу, а не сессии.
FString UAsdaQuestLogSubsystem::TrackedSettingsKey() const
{
	FString Char;
	if (const UAsdaWorldSubsystem* WS = GetGameInstance() ? GetGameInstance()->GetSubsystem<UAsdaWorldSubsystem>() : nullptr)
	{
		Char = WS->GetSelfCharName();
	}
	Char.TrimStartAndEndInline();
	if (Char.IsEmpty()) { return FString(); }   // имя ещё не известно — ключа нет
	// В ключе ini не должно быть пробелов и точек — имя персонажа их не содержит, но подстрахуемся.
	Char.ReplaceInline(TEXT(" "), TEXT("_"));
	Char.ReplaceInline(TEXT("."), TEXT("_"));
	return FString::Printf(TEXT("Tracked.%s"), *Char);
}

void UAsdaQuestLogSubsystem::SaveTrackedToSettings() const
{
	if (!GConfig) { return; }
	const UAsdaQuestMarkerSubsystem* QM = GetGameInstance() ? GetGameInstance()->GetSubsystem<UAsdaQuestMarkerSubsystem>() : nullptr;
	if (!QM) { return; }
	// ★★★ ДВА ЗАМКА ПРОТИВ ЗАТИРАНИЯ СОХРАНЁННОГО ПУСТЫМ СПИСКОМ.
	//   1) состояние с сервера ещё не пришло — список заведомо пуст, писать нечего;
	//   2) список этого персонажа ещё НЕ ВОССТАНОВЛЕН — значит мы не знаем, что затираем.
	//   Без второго замка выход в выбор персонажа (там записи чистит `TrackerClear`) записал бы
	//   в файл пустую строку, и при следующем входе восстанавливать было бы нечего.
	//   3) имени персонажа ещё нет ⇒ ключа нет: писать некуда (см. `TrackedSettingsKey`).
	if (!QM->IsQuestStateSynced()) { return; }
	const FString Key = TrackedSettingsKey();
	if (Key.IsEmpty()) { return; }
	if (TrackedRestoredFor != Key) { return; }
	TArray<FString> Ids;
	for (const FAsdaQuestTrackEntry& E : QM->GetTrackEntries()) { Ids.Add(FString::FromInt(E.QuestId)); }
	GConfig->SetString(kTrackerSection, *Key, *FString::Join(Ids, TEXT(",")), GGameUserSettingsIni);
	// ★ Явная запись на диск: без `Flush` UE держит секцию только в памяти и при вылете/закрытии
	//   процесса она пропадает. Тот же вызов, что и у раскладки окон (`FlushWindowLayout`).
	GConfig->Flush(false, GGameUserSettingsIni);
	UE_LOG(LogAsdaQuestLog, Log, TEXT("[журнал] список отслеживаемых сохранён: [%s] %s=«%s» (заданий %d)"),
		kTrackerSection, *Key, *FString::Join(Ids, TEXT(",")), Ids.Num());
}

void UAsdaQuestLogSubsystem::RestoreTrackedFromSettings()
{
	if (!GConfig) { return; }
	UAsdaQuestMarkerSubsystem* QM = Marks();
	if (!QM || !QM->IsQuestStateSynced()) { return; }   // ждём состояния журнала (5047)
	// ★★★ ВТОРОЕ УСЛОВИЕ ГОНКИ (Е2): ждём ИМЕНИ ПЕРСОНАЖА. Оно приходит позже журнала, а ключ
	//   ini состоит из него. Восстановиться раньше — значит прочитать чужой (или никакой) ключ и
	//   намертво заблокировать запись. Повторную попытку делает `Tick` — без флага «уже пробовали».
	const FString Key = TrackedSettingsKey();
	if (Key.IsEmpty())
	{
		if (!bTrackedWaitLogged)
		{
			bTrackedWaitLogged = true;
			UE_LOG(LogAsdaQuestLog, Log,
				TEXT("[журнал] журнал с сервера есть, а имени персонажа ещё нет — восстановление списка отслеживаемых ОТЛОЖЕНО"));
		}
		return;
	}
	if (TrackedRestoredFor == Key) { return; }          // одна попытка на персонажа
	TrackedRestoredFor = Key;

	// ★ УБОРКА ЗА СОБСТВЕННЫМ ДЕФЕКТОМ: старые сборки писали список под ключом `Tracked.Default`
	//   (когда имени персонажа ещё не было). Список без персонажа не принадлежит никому, и
	//   переносить его на текущего игрока нельзя — просто удаляем ключ, чтобы файл не зарастал.
	{
		FString Legacy;
		if (GConfig->GetString(kTrackerSection, TEXT("Tracked.Default"), Legacy, GGameUserSettingsIni))
		{
			GConfig->RemoveKey(kTrackerSection, TEXT("Tracked.Default"), GGameUserSettingsIni);
			GConfig->Flush(false, GGameUserSettingsIni);
			UE_LOG(LogAsdaQuestLog, Log,
				TEXT("[журнал] из ini убран устаревший ключ Tracked.Default=«%s» (наследство гонки имени персонажа)"),
				*Legacy);
		}
	}

	FString Val;
	if (!GConfig->GetString(kTrackerSection, *Key, Val, GGameUserSettingsIni) || Val.IsEmpty())
	{
		UE_LOG(LogAsdaQuestLog, Log, TEXT("[журнал] в ini нет сохранённого списка для ключа %s — восстанавливать нечего"), *Key);
		return;
	}
	TArray<FString> Ids;
	Val.ParseIntoArray(Ids, TEXT(","), true);
	int32 Restored = 0, Missing = 0;
	for (const FString& IdStr : Ids)
	{
		const int32 QuestNo = FCString::Atoi(*IdStr.TrimStartAndEnd());
		if (QuestNo <= 0) { continue; }
		if (QM->IsTrackedQuest(QuestNo)) { continue; }
		int32 Slot = INDEX_NONE;
		for (int32 i = 0; i < 12; ++i)
		{
			const FAsdaQuestSlot* S = SlotAt(i);
			if (IsSlotBusy(S) && S->QuestNo == QuestNo) { Slot = i; break; }
		}
		// Задания в журнале уже нет (сдано/отменено между сессиями) — молча пропускаем.
		if (Slot == INDEX_NONE) { ++Missing; continue; }
		if (SetSlotTracked(Slot, true)) { ++Restored; }
		else { ++Missing; }
	}
	UE_LOG(LogAsdaQuestLog, Log,
		TEXT("[журнал] список отслеживаемых восстановлен из ключа %s: из %d сохранённых поднято %d, не найдено/отклонено %d"),
		*Key, Ids.Num(), Restored, Missing);
	// ★ Состав мог измениться (задания, которых уже нет в журнале, не поднялись) — сразу
	//   приводим файл в соответствие, чтобы мёртвые номера не копились.
	if (Missing > 0) { SaveTrackedToSettings(); }
}

// ★ Удержание таблиц журнала на сессию (15.09.2026, dt_gc_hold) — обоснование в объявлении. Пути — константы этого файла.
int32 UAsdaQuestLogSubsystem::HoldGameTables() const
{
	static const FName Group(TEXT("QuestLog"));
	const TCHAR* const Paths[] = { kEpisodeDT, kBookDT, kGuideDT, kRewardDT, kSmInfoDT, kRaidDT };
	return HeldTables.Hold(Group, Paths);
}

const FAsdaEpisodeRow* UAsdaQuestLogSubsystem::EpisodeRow(int32 EpisodeId) const
{
	HoldGameTables();   // ★ 15.09 (dt_gc_hold): таблицы — сильно у подсистемы на сессию
	return AsdaDt::RowByInt<FAsdaEpisodeRow>(kEpisodeDT, EpisodeId, TEXT("QuestLog"));
}

const FAsdaQuestBookRow* UAsdaQuestLogSubsystem::BookRow(const FString& QstFile) const
{
	HoldGameTables();   // ★ 15.09 (dt_gc_hold): таблицы — сильно у подсистемы на сессию
	if (QstFile.IsEmpty()) { return nullptr; }
	const UDataTable* Dt = AsdaDt::Cached(kBookDT);
	if (!Dt) { return nullptr; }
	static const FString Ctx(TEXT("QuestLog"));
	return Dt->FindRow<FAsdaQuestBookRow>(FName(*QstFile), Ctx, /*bWarnIfMissing*/ false);
}

FString UAsdaQuestLogSubsystem::TitleForSlot(int32 SlotIdx) const
{
	const FAsdaQuestSlot* S = SlotAt(SlotIdx);
	if (!IsSlotBusy(S)) { return FString(); }
	const FAsdaEpisodeRow* Ep = EpisodeRow(S->QuestId);
	if (!Ep && S->QuestNo >= 20000 && S->QuestNo <= 30000)
	{
		// ★ 28.10.2026: задание доски объявлений — имя записи доски (`+12`).
		const UAsdaBoardQuestSubsystem* Board = GetGameInstance() ? GetGameInstance()->GetSubsystem<UAsdaBoardQuestSubsystem>() : nullptr;
		return (Board && Board->FindQuest(S->QuestNo)) ? UAsdaBoardQuestSubsystem::QuestName(S->QuestNo) : FString();
	}
	if (!Ep) { return FString(); }
	const FAsdaQuestBookRow* B = BookRow(Ep->QstFile);
	return B ? QLogLang::BookTitle(B) : QLogLang::EpTitle(Ep);   // [ПОРТ] 09.10.2026 (мультиязычность)
}

FString UAsdaQuestLogSubsystem::MapNameById(int32 MapId) const
{
	if (MapId < 0) { return FString(); }
	// Имена карт живут в общей текст-таблице `Map_info` (тот же источник, что у мини-карты).
	if (UAsdaTextSubsystem* T = GetGameInstance() ? GetGameInstance()->GetSubsystem<UAsdaTextSubsystem>() : nullptr)
	{
		const FString N = T->GetText(TEXT("Map_info"), MapId, TEXT("name"));
		if (!N.IsEmpty()) { return N; }
	}
	return FString::Printf(TEXT("#%d"), MapId);
}

// Имя цели маршрута. ★ Берём его ТОЙ ЖЕ общей точкой, что и плашки над головами
//   (`AsdaEntityText`, волна 3 п.14), — второго источника имён
//   сущностей в порту нет и заводить нельзя (закон `port-shared-points`).
//   Эталон здесь зовёт `npcTalk_FindNameById(g_pNpcWnd, id)` для NPC и `sub_939242(g_pMobMgr, id)`
//   для моба — ровно эти две таблицы.
FString UAsdaQuestLogSubsystem::RouteTargetName(int32 Kind, int32 Id) const
{
	const FString N = AsdaEntityText::QuestTargetName(Kind, Id);   // 1 = моб, иначе NPC
	if (!N.IsEmpty()) { return N; }
	return FString::Printf(TEXT("#%d"), Id);
}

UAsdaQuestMarkerSubsystem* UAsdaQuestLogSubsystem::Marks() const
{
	UGameInstance* GI = GetGameInstance();
	return GI ? GI->GetSubsystem<UAsdaQuestMarkerSubsystem>() : nullptr;
}

// ★ R5-fix-c (13.11.2026): тело — через фасад `AsdaNotice::Loc` (та же `UAsdaTextSubsystem::GetTextById`, теги срезаны; было своё
//   дословное тело). Дефекта не было: и прежнее тело, и фасад зовут одну функцию.
FString UAsdaQuestLogSubsystem::Loc(int32 Id) const
{
	return AsdaNotice::Loc(GetGameInstance(), Id);
}

void UAsdaQuestLogSubsystem::PlaySound(int32 Which) const
{
	if (Which <= 0) { return; }
	if (UAsdaSoundSubsystem* S = GetGameInstance() ? GetGameInstance()->GetSubsystem<UAsdaSoundSubsystem>() : nullptr)
	{
		S->PlaySound2DById(Which, false);
	}
}

FString UAsdaQuestLogSubsystem::Describe()
{
	FString Out = FString::Printf(
		TEXT("[журнал заданий] окно=%s страница=%s выбрана ячейка=%d виджет=%s\n"),
		bWindowOpen ? TEXT("открыто") : TEXT("закрыто"),
		bPageExpanded ? TEXT("развёрнута") : TEXT("свёрнута"),
		ActiveSlot, bReady ? TEXT("готов") : TEXT("НЕ ГОТОВ"));
	const UAsdaQuestMarkerSubsystem* QM = Marks();
	if (!QM) { return Out + TEXT("  подсистема состояния квестов недоступна\n"); }
	Out += FString::Printf(TEXT("  состояние с сервера: %s\n"), QM->IsQuestStateSynced() ? TEXT("получено") : TEXT("НЕТ (5047 не приходил)"));
	for (int32 i = 0; i < 12; ++i)
	{
		const FAsdaQuestSlot* S = SlotAt(i);
		if (!IsSlotBusy(S)) { continue; }
		Out += FString::Printf(TEXT("  [%2d] эпизод=%d квест=%d стадия=%d вид=%d поле-ячейки=%d цели=[%d,%d,%d,%d,%d] «%s»%s\n"),
			i, S->QuestId, S->QuestNo, S->Stage, S->ObjType, S->SlotIndex,
			S->ObjCounts.IsValidIndex(0) ? S->ObjCounts[0] : 0,
			S->ObjCounts.IsValidIndex(1) ? S->ObjCounts[1] : 0,
			S->ObjCounts.IsValidIndex(2) ? S->ObjCounts[2] : 0,
			S->ObjCounts.IsValidIndex(3) ? S->ObjCounts[3] : 0,
			S->ObjCounts.IsValidIndex(4) ? S->ObjCounts[4] : 0,
			*TitleForSlot(i), IsSlotTracked(i) ? TEXT(" [отслеживается]") : TEXT(""));
		// ★ Е1: почему галка доступна/недоступна — видно тут же, без гадания по кадру.
		Out += FString::Printf(TEXT("       галка 743: %s\n"),
			const_cast<UAsdaQuestLogSubsystem*>(this)->CanTrackSlot(i)
				? TEXT("ДОСТУПНА (есть что отслеживать)")
				: (IsSlotTracked(i) ? TEXT("доступна (снять отслеживание)")
									: TEXT("НЕДОСТУПНА — ни строк условий, ни координат цели")));
	}
	// ★ Е1: САМ ВИДЖЕТ галки — иначе «недоступна» в модели неотличимо от «недоступна на экране».
	if (TrackBox)
	{
		Out += FString::Printf(TEXT("  чекбокс 743: %s, отметка=%s, прозрачность=%.2f (подпись 744 %.2f)\n"),
			TrackBox->GetIsEnabled() ? TEXT("ДОСТУПЕН") : TEXT("НЕДОСТУПЕН (клик не проходит)"),
			TrackBox->IsChecked() ? TEXT("стоит") : TEXT("снята"),
			TrackBox->GetRenderOpacity(),
			TrackLabel ? TrackLabel->GetRenderOpacity() : -1.f);
	}
	for (int32 i = 0; i < Routes.Num(); ++i)
	{
		Out += FString::Printf(TEXT("  маршрут %d: %s (карта=%d, клетка=%d,%d)\n"),
			i, *Routes[i].Text, Routes[i].MapId, Routes[i].X, Routes[i].Y);
	}
	// ★ Строки награды 786…789 (и «на выбор» 1694…1697) — ЧИСЛАМИ, а не на глаз. Нужны с
	//   2026-08-23: после схлопывания пустых ячеек (`asda.Quest.CollapseRewardCells`) номер строки
	//   роль награды больше не задаёт, и увидеть
	//   «что где стоит» можно только так. Код 0 = иконка не предметная (вид «опыт», плоская `exp2`).
	for (int32 j = 0; j < 4; ++j)
	{
		const FString T = (RewardTexts.IsValidIndex(j) && RewardTexts[j]) ? RewardTexts[j]->GetText().ToString() : FString();
		const FString C = (ChoiceTexts.IsValidIndex(j) && ChoiceTexts[j]) ? ChoiceTexts[j]->GetText().ToString() : FString();
		const bool bIcon = RewardIcons.IsValidIndex(j) && RewardIcons[j] && RewardIcons[j]->IsVisible();
		Out += FString::Printf(TEXT("  награда %d: «%s» (код %d, иконка=%d) | на выбор: «%s»\n"),
			j, *T, RewardCodes.IsValidIndex(j) ? RewardCodes[j] : 0, bIcon ? 1 : 0, *C);
	}
	return Out;
}
