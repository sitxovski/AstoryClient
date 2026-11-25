// Copyright (c) 2026 @sitxovski (Discord)
// AsdaQuestMarkerSubsystem_Marks.cpp — ★ R3-c 12.11.2026 (разрез AsdaQuestMarkerSubsystem.cpp, дословный перенос): метки: запись сценария и её страница, пересчёт меток, подсказка «ждёт на другой карте», цели заданий, раздача на карты и над головой, Tick.
//   Общее для частей — в AsdaQuestMarkerSubsystem_Private.h; шапка и реверс — в AsdaQuestMarkerSubsystem.cpp.
#include "Quest/AsdaQuestMarkerSubsystem.h"
#include "AsdaQuestMarkTableCache.h"   // ★ R4-c (15.11.2026): локальный кэш строк таблиц пересчёта меток
#include "AsdaPerf.h"   // ★ R4-c (14.11.2026): области замера времени игрового потока
#include "Npc/AsdaNpcGmOnly.h"   // ★ 09.11.2026: NPC «только для GM_»
#include "Quest/AsdaNsrScriptPage.h"     // ★ 04.11.2026 (questmark-chmella): одно правило «запись без страницы» для меток и разговора
#include "Npc/AsdaNpcDialogTypes.h"      // FAsdaNsrPageRow — проверка наличия страницы сценария
#include "Quest/AsdaQuestGuideTarget.h"   // ★ 31.10.2026 (баг 6): место цели строки гида без координат — общая точка с «Следовать»
#include "Quest/AsdaBoardQuestSubsystem.h"   // ★ 28.10.2026: метка доски объявлений (тип 5) на миникарте
#include "AsdaDevConsole.h"   // ★ античит А2: ASDA_DEV_CVAR / ASDA_DEV_CONSOLE — дев-ключи и команды вне Shipping
#include "World/AsdaCoord.h"
#include "World/AsdaEntityText.h"   // ★ 31.10.2026: имя цели значка карты мира — общая точка (как у гида/трекера)   // ★ N11 (В1 дедупа №2): тайловая сетка — одна точка
#include "AsdaDataTableCache.h"   // ★ Аудит №4 (B2): AsdaDt::Cached — одна ленивая загрузка DataTable на порт

#include "AsdaNetSubsystem.h"
#include "Npc/AsdaNpcActor.h"
#include "UI/AsdaMinimapSubsystem.h"
// FAsdaMarkup::StripTags — единая точка разбора тегов <F..>   ★ R3: комментарий снятого включения "UI/AsdaMarkup.h" (файл им не пользовался) — сохранён
#include "Text/AsdaTextSubsystem.h"        // нотис 6172 (строка подсказки «Минопио»)
#include "Npc/AsdaNpcDialogSubsystem.h"     // ShowQuestInfo/HideQuestInfo — окно QUEST_INFO_WIN (599)
#include "World/AsdaLoadManagerSubsystem.h"
#include "UI/AsdaWindowSubsystem.h"        // ★ Волна В4: роспись «закрыть всё» (трекер 1800)

#include "Engine/DataTable.h"
#include "Engine/Engine.h"
#include "EngineUtils.h"
#include "AsdaOutgoingGate.h"               // ★ С-Р1: общий офлайн-гейт отладочных команд (RefuseDebugWhenOnline)
#include "Containers/Ticker.h"              // ★ 08.11.2026: самотест asda.SelfTest.QuestMarkArshes (шаги по времени)
#include "UnrealClient.h"                   // FScreenshotRequest — кадры самотеста в файлы
#include "Text/AsdaLangRows.h"               // [ПОРТ] 09.10.2026 (мультиязычность): названия эпизодов и деревень на языке сессии
#include "AsdaQuestMarkerSubsystem_Private.h"   // ★ R3-c 12.11.2026: общее для частей AsdaQuestMarkerSubsystem*.cpp

// itemTooltip_BuildStatFlagsTable1@0xD84810 (сколько категорий задано) + charEquip_VerifyStatChecksum@0xD850D0
//   (все ли заданные сошлись) — теперь это ОДИН вызов общего вычислителя.
bool UAsdaQuestMarkerSubsystem::CheckRecord(const FAsdaNpcQuestScriptRow& R) const
{
	ASDA_PERF_SCOPE("Quest.CheckRecord");   // ★ R4-c (14.11.2026): область замера (AsdaPerf.h; в Shipping пусто)
	return EvalNsrCond(CondFromScriptRow(R), EAsdaCondLayout::Script, EAsdaCondOwner::MenuButton);
}

// Порт `questNpc_CollectDeliverableItems@0xD89550` (quest.c:6003) в режиме СЧЁТА `a2 == 1`.
//   Тело эталона в этом режиме:
//     • два цикла по 10 записей СКОПИРОВАННОЙ записи `SNQInfo.Bin` — сперва список «ВЫДАЁТ»
//       (`self + 23508`, категория 0), затем «ПРИНИМАЕТ» (`self + 23728`, категория 1);
//     • у записи берётся `i16 +0` — номер квеста (в порту `FAsdaSnqEntry::QuestId`) и пять
//       ключей `+68 + 4*j` (`FAsdaSnqEntry::Keys`);
//     • гейт списка «выдаёт»: `!CQuest_HasActiveQuestOrReq(mode 0)` И `!questIsCompleteFlagSet`;
//       гейт списка «принимает»: `CQuest_HasActiveQuestOrReq(mode 1)`;
//     • по каждому ключу берётся запись `SNQICT.Bin` (`findRecordByKey19150@0xD89DF0`, он же
//       `sub_930E2B`), и если её условие сошлось (`itemTooltip_BuildStatFlagsTable2` +
//       `itemVerifyAttributeSum` — в порту это `EvalSnqIctCond`), счётчик растёт;
//     • возврат: `a2 < 1 || deliverCount < 1 ? totalCount : 99`.
//   ⚠ ЧЕГО ЗДЕСЬ НЕТ И ПОЧЕМУ — гейт `questFindIdInSlotArray@0xD8B250`, который эталон ставит
//     ПЕРЕД обоими списками (`... != 2`). Он перебирает массив из 30 пар по 8 байт по смещению
//     `объект+1504`. Этот массив создаётся в `gameObj_ctor@0xD8E300`
//     (`CRT_ArrayCallbackIterator(v6 + 1504, 8, 30, sub_D8FB00)`) со значениями `{i32 -1, u8 0}`,
//     и во ВСЁМ декомпиле клиента (`RE/ida/export/AsdaStory_game.c`) записей в него нет — искал
//     и по `+ 376)` (шаг в dword), и по `+ 1504` (шаг в байтах), и по соседним элементам.
//     Поскольку сама функция первым делом отсекает `a2 <= -1`, совпасть с `-1` номер квеста не
//     может ⇒ гейт ВСЕГДА пропускает, и его отсутствие поведения не меняет. Формально честно:
//     «записей в массив в срезе декомпила нет».
//   ⚠ ДЕФЕКТ ОРИГИНАЛА (воспроизводить нечего, но зафиксируем): во втором цикле эталон зовёт этот
//     гейт с номером квеста из списка ВЫДАЧИ (`self + 22*i + 23508`), а не из списка приёма —
//     опечатка копипастой. При мёртвом массиве она ни на что не влияет.
int32 UAsdaQuestMarkerSubsystem::CollectDeliverableForNsr(const FString& NsrName) const
{
	ASDA_PERF_SCOPE("Quest.CollectDeliverable");   // ★ R4-c (14.11.2026): область замера (AsdaPerf.h; в Shipping пусто)
	HoldGameTables();   // ★ 15.09 (dt_gc_hold): таблицы — сильно у подсистемы на сессию
	// npcId владельца сценария — из шапки `.NSR` (`блок+0`), ровно как в эталоне.
	// ★ R4-c (15.11.2026): строки таблиц пересчёта — через локальный кэш меток (`AsdaQuestMarkTableCache.h`).
	const FAsdaNsrHeaderRow* Hdr = AsdaQuestMarkTableCache::Row<FAsdaNsrHeaderRow>(AsdaQuestMarkTableCache::ETable::NsrHeader, kNsrHeaderDT, NsrName, TEXT("QuestMarker"));
	if (!Hdr) { return 0; }
	// `findCopyRecordById16@0xDA11F0`: запись `SNQInfo.Bin` с этим npcId (ключ строки таблицы).
	const FAsdaNpcSnqRow* Snq = AsdaQuestMarkTableCache::RowByInt<FAsdaNpcSnqRow>(AsdaQuestMarkTableCache::ETable::Snq, kSnqDT, Hdr->NpcId, TEXT("QuestMarker"));
	if (!Snq) { return 0; }

	int32 TotalCount   = 0;   // `totalCount` эталона
	int32 DeliverCount = 0;   // `deliverCount` эталона (только список «принимает»)

	// Одна точка на оба цикла: пять ключей записи → сколько условий сошлось.
	auto CountMatchedKeys = [this, &NsrName, Hdr](const FAsdaSnqEntry& E, bool bTake) -> int32
	{
		int32 N = 0;
		for (int32 j = 0; j < 5; ++j)
		{
			if (!E.Keys.IsValidIndex(j)) { break; }
			const FAsdaSnqIctRow* R = AsdaQuestMarkTableCache::RowByInt<FAsdaSnqIctRow>(AsdaQuestMarkTableCache::ETable::SnqIct, kSnqIctDT, E.Keys[j], TEXT("QuestMarker"));
			if (!R || R->Key < 0) { continue; }        // эталон: `rec && *rec >= 0`
			// ★ 04.11.2026 (questmark-chmella): строка облачка, чьей страницы нет в сценарии, — тупик (выбор строки гасит облачко
			//   и рисует страницу `TalkId`, приём/сдача — только шагом страницы). Её не показывает облачко (`BuildQuestLists`) и не
			//   считает метка — одно правило `AsdaNsrScript::ShouldSkipRecordWithoutPage`.
			if (AsdaNsrScript::ResolveCellTalkId(NsrName, R->TalkId, R->Key) < 0) { continue; }
			if (bTake && !TakeCellOpenAtStage(Hdr->NpcId, E.QuestId, R->Key)) { continue; }   // ★ 06.11.2026: стадия, см. `TakeCellOpenAtStage`
			if (EvalSnqIctCond(*R)) { ++N; }
		}
		return N;
	};

	// ★ [ИСПРАВЛЕНИЕ] 31.10.2026 (bug-quest-follow): ячеек у NPC может быть больше 10 (таблица расширения
	//   `QuestNpcTableExt.Bin`, патч NpcServer N22) — оба цикла идут по всем ячейкам строки, как облачко (`BuildQuestLists`).
	// Цикл 1 — «ВЫДАЁТ» (категория 0): квест ещё не взят и не завершён.
	const int32 Lim = AsdaQuestGuideTarget::NpcCellLimit();   // ключ `asda.Npc.CellsBeyond10 0` — как было: 10 ячеек
	for (int32 i = 0; i < Snq->Give.Num() && i < Lim; ++i)
	{
		if (!Snq->Give.IsValidIndex(i)) { break; }
		const FAsdaSnqEntry& E = Snq->Give[i];
		if (E.QuestId < 0) { continue; }               // эталон: `SLOWORD(...) >= 0`
		if (AsdaNsrScript::StartClosed(E.QuestId)) { continue; }   // ★ 06.11.2026: выдача закрыта
		if (HasActiveQuestOrReqByKey(E.QuestId)) { continue; }   // CQuest_HasActiveQuestOrReq(0, …)
		if (IsQuestCompleted(E.QuestId)) { continue; }           // questIsCompleteFlagSet (sub_9318A8)
		TotalCount += CountMatchedKeys(E, /*bTake*/ false);
	}
	// Цикл 2 — «ПРИНИМАЕТ» (категория 1): квест лежит в слоте журнала, его можно сдать.
	for (int32 i = 0; i < Snq->Take.Num() && i < Lim; ++i)
	{
		if (!Snq->Take.IsValidIndex(i)) { break; }
		const FAsdaSnqEntry& E = Snq->Take[i];
		if (E.QuestId < 0) { continue; }
		if (!IsQuestReadyToHandIn(E.QuestId)) { continue; }      // CQuest_HasActiveQuestOrReq(1, …)
		const int32 N = CountMatchedKeys(E, /*bTake*/ true);
		DeliverCount += N;
		TotalCount   += N;
	}

	// `if (a2 < 1 || deliverCount < 1) return totalCount; else return 99;` — у нас всегда a2 == 1.
	return (DeliverCount >= 1) ? 99 : TotalCount;
}

// ════════════════════════════════════════════════════════════════════════════════════════════════
//  ОБЩЕЕ ПРАВИЛО: запись сценария без страницы (метки И разговор — одна точка)
// ════════════════════════════════════════════════════════════════════════════════════════════════
// ★★★ 04.11.2026 [ИСПРАВЛЕНИЕ ДЕФЕКТА ДАННЫХ ОРИГИНАЛА] (баг владельца: над Волшебницей Хмеллой жёлтый «!», а в разговоре только
//   «Конец»; LUFFY ур. 100). В 50 сценариях `Nsr.bsz` (все три клиента) есть записи, чьей страницы в `.NSR` НЕТ: у Хмеллы
//   #3…#8 («задание 93/99/100/135–137 в работе», красный «!») и #13…#19 («предыдущее сдано, следующее не взято», жёлтый «!»).
//   #16 — «задание 135 не взято, ур. 41–150»: страницы 17 нет, выдачи 135 нет ни у одного NPC (SNQ «выдаёт» у Хмеллы = 57/296/312).
//   Эталон: метку выбирает `questFindMatchingActionMode1@0xD8AB90` по записи, НЕ глядя на страницу; разговор
//   (`uiCraftSlotClickHandler@0xD83D90`) на той же записи зовёт `npcDialogRenderPage`, страницы нет — НИЧЕГО не рисует. То есть
//   у оригинала метка вечно обещает то, чего разговор не покажет (на уровне 41+ — у каждого, кто не брал 135).
//   5.1.6 научил разговор пропускать такую запись (ключ `asda.Npc.SkipMissingPage`), а перебор меток остался прежним — отсюда
//   «метка есть, предложить нечего». Теперь ОБА перебора зовут одну функцию и пропускают одни и те же записи.
//   Действие не теряется: список заданий из ячеек SNQ открывается только шагом состояния 5 страницы 1 записи опкода 123
//   (Фхашу, Тид, Куриан, Мелве, Язарк и ещё 6 сценариев) — эта запись стоит на `RecIndex 0`, её страница есть, и она даёт метку
//   и открывает список раньше любой записи без страницы. У 108/114/115npc страницы 1 нет — список не открывается ни в
//   разговоре, ни (теперь) меткой. Сводка 139 записей: Reports/questmark_2026-11-04/.
//   Ключ `asda.Npc.SkipMissingPage 0` — прежнее поведение обоих переборов разом.
static ASDA_DEV_CVAR(int32, CVarAsdaNpcSkipMissingPage,
	TEXT("asda.Npc.SkipMissingPage"), 1,
	TEXT("[ИСПРАВЛЕНИЕ] 1 (по умолчанию) = запись сценария NPC, чьей страницы нет в .NSR (50 сценариев оригинала), пропускается ")
	TEXT("И в разговоре, И в выборе квестовой метки (над NPC, мини-карта, карта мира, «где тебя ждут»); 0 = прежнее поведение ")
	TEXT("(разговор закрывался на такой записи, метка по ней горела)."),
	ECVF_Default);

bool AsdaNsrScript::ShouldSkipRecordWithoutPage(const FString& NsrName, int32 PageId)
{
	ASDA_PERF_SCOPE("Quest.SkipNoPage");   // ★ R4-c (14.11.2026): область замера (AsdaPerf.h; в Shipping пусто)
	if (CVarAsdaNpcSkipMissingPage.GetValueOnGameThread() == 0) { return false; }
	// Ключ строки `DT_NpcNsrPage` — «<сценарий>_<talkId>», ровно как `UAsdaNpcDialogSubsystem::FindPage`.
	// ★ R4-c (15.11.2026): через локальный кэш меток (прежде — `AsdaDt::Row` с именем `FName(Printf("%s_%d"))` на каждый вызов).
	return AsdaQuestMarkTableCache::RowByPair<FAsdaNsrPageRow>(AsdaQuestMarkTableCache::ETable::NsrPage, TEXT("/Game/AsdaAssets/Data/NPC/DT_NpcNsrPage.DT_NpcNsrPage"),
		NsrName, PageId, TEXT("NsrScriptPage")) == nullptr;
}

// ★ 06.11.2026 [ИСПРАВЛЕНИЕ ДЕФЕКТА ДАННЫХ ОРИГИНАЛА] (решение владельца): задания 156 и 157 засчитываются только за Red Turkey (тип 237),
//   у которого нет спавна ни на одной карте, поэтому выполнить их нельзя. Уже взятые задания правило не трогает: оно закрывает только выдачу.
static ASDA_DEV_CVAR(int32, CVarAsdaQuestClosedStart,
	TEXT("asda.Quest.ClosedStart"), 1,
	TEXT("[ИСПРАВЛЕНИЕ] 1 (по умолчанию) = задания 156 и 157 (цель без спавна) не выдаются и не дают метку; 0 = как в оригинале."),
	ECVF_Default);

bool AsdaNsrScript::StartClosed(int32 QuestId)
{
	return (QuestId == 156 || QuestId == 157) && CVarAsdaQuestClosedStart.GetValueOnGameThread() != 0;
}

// ★★ 04.11.2026 [ИСПРАВЛЕНИЕ ДЕФЕКТА ДАННЫХ ОРИГИНАЛА] (questmark-chmella, распоряжение владельца «всё, что можно, починить»).
//   15 из 887 ячеек облачка ведут на страницу, которой в сценарии нет: Фхашу (14) 190/191/193 выдача, 192/196 сдача; Тид (45)
//   127/128; Туран (103) 48; talkId 0 — 127npc 379 (выдача и сдача), 83npc 52, 96npc 170 (сдача); tomb1 (64) 127/128/129.
//   Эталон (`npcDialog_RenderPageByCode@0xDA2440`) гасит облачко и зовёт `npcDialogRenderPage` — страницы нет, не рисуется НИЧЕГО.
//   Приём/сдача же делаются ТОЛЬКО шагом страницы, и под-запись шага несёт в `State` КЛЮЧ условия SNQICT — тот самый ключ ячейки
//   (у шагов приёма ещё и `AcceptQuestId = 2001 + номер`: 190 → 2191 на стр. 40 Фхашу и т. д.). Для 12 ячеек из 15 такая страница
//   в сценарии ровно одна — строка ведёт на неё; у tomb1 (127/128/129) страниц не было ни в одной СВОЕЙ сборке — сценарий возвращён из донора (Fantazium/XYT/Arabic, ведомость `Tools/data/nsr_donor_scripts.json`), и его ячейки ведут на свои страницы; без страницы и без шага строка не показывается. Обычный путь
//   (страница ячейки есть, 872 из 887) не меняется. Разбор: Reports/questmark_2026-11-04/REPORT.md.
static ASDA_DEV_CVAR(int32, CVarAsdaNpcCellPageRedirect,
	TEXT("asda.Npc.CellPageRedirect"), 1,
	TEXT("[ИСПРАВЛЕНИЕ] 1 (по умолчанию) = строка облачка заданий, чьей страницы нет в .NSR, ведёт на единственную страницу этого ")
	TEXT("сценария, где шаг несёт тот же ключ условия (12 ячеек оригинала); 0 = такая строка не показывается (и не считается в метку)."),
	ECVF_Default);

int32 AsdaNsrScript::ResolveCellTalkId(const FString& NsrName, int32 TalkId, int32 Key)
{
	ASDA_PERF_SCOPE("Quest.ResolveCellTalkId");   // ★ R4-c (14.11.2026): область замера (AsdaPerf.h; в Shipping пусто)
	if (!ShouldSkipRecordWithoutPage(NsrName, TalkId)) { return TalkId; }
	if (CVarAsdaNpcCellPageRedirect.GetValueOnGameThread() == 0 || Key <= 0) { return -1; }
	// Индекс «(сценарий, ключ состояния шага) → страницы» строится один раз на таблицу (таблица держится на сессию).
	static TWeakObjectPtr<const UDataTable> IndexedFor;
	static TMap<FString, TArray<int32>> PagesByKey;   // ключ «<сценарий>|<состояние>»
	const UDataTable* Dt = AsdaDt::Cached(TEXT("/Game/AsdaAssets/Data/NPC/DT_NpcNsrPage.DT_NpcNsrPage"));
	if (!Dt) { return -1; }
	if (IndexedFor.Get() != Dt)
	{
		PagesByKey.Reset();
		for (const TPair<FName, uint8*>& It : Dt->GetRowMap())
		{
			const FAsdaNsrPageRow* P = reinterpret_cast<const FAsdaNsrPageRow*>(It.Value);
			if (!P) { continue; }
			for (const FAsdaNsrStep& St : P->Steps)
			{
				for (const FAsdaNsrStepSub& Su : St.Subs)
				{
					if (Su.State <= 0) { continue; }
					PagesByKey.FindOrAdd(FString::Printf(TEXT("%s|%d"), *P->NsrName.ToLower(), Su.State)).AddUnique(P->TalkId);
				}
			}
		}
		IndexedFor = Dt;
	}
	const TArray<int32>* Pages = PagesByKey.Find(FString::Printf(TEXT("%s|%d"), *NsrName.ToLower(), Key));
	return (Pages && Pages->Num() == 1) ? (*Pages)[0] : -1;
}

// ★★★ 06.11.2026 [ИСПРАВЛЕНИЕ ДЕФЕКТА ОРИГИНАЛА] (questmark-vasa, баг владельца: задания со сдачей у Васы подсвечивают чужого NPC).
//   Гейт списка «принимает» в эталоне — `CQuest_HasActiveQuestOrReq(mode 1)` (quest.c:3344): только «номер задания лежит в журнале»,
//   БЕЗ стадии; стадию обычно несёт условие ключа SNQICT. Но у 7 ячеек из 486 условие и на сервере к заданию не привязано (только
//   уровень): Туран 417 (ключ 985, шаг 0) и 48 (147, шаг 1), Васа 417 (986, шаг 1) и 418 (989, шаг 1), NPC 71/72/73 задания 80/76/78
//   (234/222/228, шаг 0). Итог: с 417 на стадии 1 (сдача у Васы) красный «!» горел и над Тураном, а выбор строки вёл на страницу,
//   чей шаг не проходил. Сервер (`Process_Quest@0x420C10`) принимает ячейку только при (задание, шаг) == (задание, стадия слота) —
//   то же правило здесь, по `DT_QuestTurnIn`. Ячейки, которой нет в таблице, правило не касается (как в оригинале).
static ASDA_DEV_CVAR(int32, CVarAsdaQuestTakeCellByStage,
	TEXT("asda.Quest.TakeCellByStage"), 1,
	TEXT("[ИСПРАВЛЕНИЕ] 1 (по умолчанию) = ячейка сдачи считается в метке и показывается в разговоре только на той стадии задания, ")
	TEXT("где её примет сервер (DT_QuestTurnIn); 0 = как в оригинале: достаточно, что задание лежит в журнале."),
	ECVF_Default);

bool UAsdaQuestMarkerSubsystem::TakeCellOpenAtStage(int32 NpcId, int32 QuestNo, int32 Key) const
{
	ASDA_PERF_SCOPE("Quest.TakeCellOpenAtStage");   // ★ R4-c (14.11.2026): область замера (AsdaPerf.h; в Shipping пусто)
	if (CVarAsdaQuestTakeCellByStage.GetValueOnGameThread() == 0) { return true; }
	const UDataTable* Tbl = AsdaDt::Cached(TEXT("/Game/AsdaAssets/Data/Quest/DT_QuestTurnIn.DT_QuestTurnIn"));
	if (!Tbl) { return true; }
	bool bListed = false;
	for (const TPair<FName, uint8*>& It : Tbl->GetRowMap())
	{
		const FAsdaQuestTurnInRow* R = reinterpret_cast<const FAsdaQuestTurnInRow*>(It.Value);
		if (!R || R->NpcId != NpcId || R->QuestNo != QuestNo || !R->Keys.Contains(Key)) { continue; }
		bListed = true;
		if (IsQuestActiveWithStage(QuestNo, R->Stage)) { return true; }
	}
	return !bListed;
}

// questFindMatchingActionMode1@0xD8AB90 / Mode2@0xD8AE60 — метка по сценарию NPC.
int32 UAsdaQuestMarkerSubsystem::EvalMarkForNsr(const FString& NsrName) const
{
	ASDA_PERF_SCOPE("Quest.EvalMarkForNsr");   // ★ R4-c (14.11.2026): область замера (AsdaPerf.h; в Shipping пусто)
	const TArray<const FAsdaNpcQuestScriptRow*>* Recs = ScriptByNsr.Find(NsrName.ToLower());
	if (!Recs) { return 0; }

	for (const FAsdaNpcQuestScriptRow* Rp : *Recs)
	{
		const FAsdaNpcQuestScriptRow& R = *Rp;
		if (R.RecIndex >= 64) { break; }               // эталон сканирует РОВНО 64 записи

		if (R.Opcode == 122 || R.Opcode == 123)        // sub_D87180 — спец-запись
		{
			if (R.Opcode != 123) { continue; }         // 122 — реплика из Talk, метки не даёт
			// ★ Ветка эталона `quest.c:6489-6499` ЦЕЛИКОМ. Метку задаёт НЕ поле `Mark` записи
			//   (у 122 из 130 записей опкода 123 там 3, а метку 3 на карту порт не выводит), а
			//   КОНСТАНТА по результату счёта строк облачка:
			//     99 (есть что СДАТЬ)      → метка 2 (красный «!»);
			//     ≥ 2 (есть что взять/…)   → метка 1 (жёлтый «!»);
			//     < 2                      → `goto LABEL_2`, то есть переход к следующей записи.
			//   ВОЗВРАТ ПРЕКРАЩАЕТ ПЕРЕБОР — это важно: во всех 130 сценариях с опкодом 123 эта
			//   запись стоит на `RecIndex 0`, то есть перекрывает все обычные записи ниже.
			const int32 Deliverable = CollectDeliverableForNsr(NsrName);
			if (Deliverable < 2) { continue; }
			// ★ 04.11.2026: страницы нет — разговор эту запись пропускает, метка тоже (одно правило, см. выше).
			if (AsdaNsrScript::ShouldSkipRecordWithoutPage(NsrName, R.PageId)) { continue; }
			return (Deliverable == 99) ? 2 : 1;
		}
		if (R.Opcode == 121) { continue; }             // крафт — меток не даёт
		// ★ 04.11.2026: тот же порядок, что в разговоре: сперва условия, затем наличие страницы (без страницы — дальше).
		if (AsdaNsrScript::StartClosed(R.QuestFreeA)) { continue; }   // ★ 06.11.2026: запись выдачи закрытого задания
		if (CheckRecord(R) && !AsdaNsrScript::ShouldSkipRecordWithoutPage(NsrName, R.PageId)) { return R.Mark; }
	}
	return 0;
}

// ── Пересчёт и раздача ──────────────────────────────────────────────────────────────────────────
void UAsdaQuestMarkerSubsystem::ReadSelfLevelClass(int32& OutLevel, int32& OutClass, int32& OutSubClass) const
{
	ASDA_PERF_SCOPE("Quest.ReadSelfLevelClass");   // ★ R4-c (14.11.2026): область замера (AsdaPerf.h; в Shipping пусто)
	OutLevel = 0; OutClass = 0; OutSubClass = 0;
	UGameInstance* GI = GetGameInstance();
	UAsdaNetSubsystem* Net = GI ? GI->GetSubsystem<UAsdaNetSubsystem>() : nullptr;
	if (!Net) { return; }
	// Персонаж, которым вошли в мир: запись char-list с тем же СЫРЫМ slotId, что ушёл в select(1016).
	//   Если по какой-то причине не нашли — берём первый непустой слот (лучше, чем нулевой уровень).
	const FAsdaCharRecord* Chosen = nullptr;
	for (const FAsdaCharRecord& C : Net->CharList)
	{
		if (C.Slot == 0xFF) { continue; }
		if (!Chosen) { Chosen = &C; }
		if (Net->LastSelectedRawSlot != 0 && C.RawSlotId == Net->LastSelectedRawSlot) { Chosen = &C; break; }
	}
	if (Chosen) { OutLevel = Chosen->Level; OutClass = Chosen->Job; OutSubClass = Chosen->SubClass; }
	// ★★★ 18.10.2026 (жалоба игроков: «после 5-го уровня квесты не появляются, после перезапуска — появляются»).
	//   Запись char-list — СНИМОК экрана выбора персонажа (1005/1017), за сессию она не меняется: повышение уровня
	//   (4036) и смена профессии (5042) её не трогают, и условия NSR видели уровень/класс на момент входа. Эталон
	//   читает живые поля CCharacter (+0x43A уровень, +0x438/+0x439 класс/подкласс). В порту их зеркала —
	//   `SelfLevel` (4005-self/4036), `SelfClassId` и `SelfSubJob` (1017/4005-self/5042) — берём их, как только
	//   они известны; снимок char-list остаётся только запасом до первого пакета.
	if (Net->SelfLevel > 0)    { OutLevel = Net->SelfLevel; }
	if (Net->SelfClassId >= 0) { OutClass = Net->SelfClassId; }
	if (Net->SelfSubJob >= 0)  { OutSubClass = Net->SelfSubJob; }
	// Подмена (только офлайн-проверки, см. `asda.Quest.FakeSelf`).
	if (GAsdaFakeSelfLevel >= 0) { OutLevel = GAsdaFakeSelfLevel; }
	if (GAsdaFakeSelfClass >= 0) { OutClass = GAsdaFakeSelfClass; }
	if (GAsdaFakeSelfSub   >= 0) { OutSubClass = GAsdaFakeSelfSub; }
}

void UAsdaQuestMarkerSubsystem::RebuildMarks()
{
	ASDA_PERF_SCOPE("Quest.RebuildMarks");   // ★ R4-c (14.11.2026): область замера (AsdaPerf.h; в Shipping пусто)
	EnsureTables();
	MarkByNpcId.Reset();
	// ★ 31.10.2026: задания, снятые с трекера выполненными, держат точку NPC сдачи только пока лежат в журнале
	//   (сдано или брошено — ячейка пуста — точка уходит).
	for (auto It = TrackedDoneQuests.CreateIterator(); It; ++It)
	{
		if (FindQuestSlotNo(*It) < 0) { It.RemoveCurrent(); }
	}

	UGameInstance* GI = GetGameInstance();
	UAsdaLoadManagerSubsystem* LM = GI ? GI->GetSubsystem<UAsdaLoadManagerSubsystem>() : nullptr;
	const int32 MapId = LM ? LM->CurrentMapId : -1;
	ShownMapId = MapId;

	// ★ ЧЕСТНО: пока сервер не прислал 5047, состояния квестов НЕТ — меток тоже нет (ничего не выдумываем).
	if (bStateSynced && ScriptTable && NpcTable)
	{
		// npcUi_BuildNpcActionList@0xD97650: только записи ТЕКУЩЕЙ карты с типом 0, 4 или 5.
		for (const FAsdaNpcQuestNpcRow* N : NpcRows)
		{
			if (!N || N->MapId != MapId) { continue; }
			if (AsdaNpcGmOnly::HiddenForSelfOnMap(GI, N->NpcId, N->MapId)) { continue; }   // ★ 09.11.2026: NPC «только для GM_» — меток нет
			if (N->NpcType != 0 && N->NpcType != 4 && N->NpcType != 5) { continue; }
			// ★ 28.10.2026: тип 5 (доска объявлений) эталон считает НЕ по сценарию, а `craftCheckRecipeMaterialsAvailable`
			//   (`npcUi_HandleNpcInteract@0xD97A10`, npc_mob_pet.c:1696) и кладёт ТОЛЬКО на карту (`partyListPushEntry`, режим 1),
			//   метки над головой у доски нет — поэтому она в `MarkByNpcId` не идёт, её точку добавляет `PushMapMarks`.
			if (N->NpcType == 5 && UAsdaBoardQuestSubsystem::IsEnabled()) { continue; }
			const int32 Mark = EvalMarkForNsr(N->NsrName);
			if (Mark >= 1 && Mark <= 3) { MarkByNpcId.Add(N->NpcId, Mark); }
		}
	}

	PushMapMarks(MapId);
	PushOverheadMarks();

	// ★ Порт хвоста `npcUiRefreshActionList@0xD97510`: сразу после построения списка меток эталон
	//   зовёт сбор карт «где тебя ждут» — теми же данными, тем же вычислителем.
	ComputeAwaitMaps(MapId);
	MaybeShowAwaitHint(MapId);

	bDirty = false;
}

// ══════════════════════════════════════════════════════════════════════════════════════════════════
//  ПОДСКАЗКА «МИНОПИО»: сбор карт, где ждёт НОВЫЙ квест
// ══════════════════════════════════════════════════════════════════════════════════════════════════
// Порт `itemFilterValidRecipeList@0xD97D30` (`item_inventory.c:53070`, имя IDA ошибочно — к рецептам
//   отношения не имеет) вместе с `npcData_buildTypeList@0xD98A70`, `npcData_LoadModelEntry@0xD98D40`,
//   `listAddUniqueSlot10@0xD98E80` и вторым проходом `buffNameList_filterActiveAndRefresh@0xD98030`.
// Разбор — `Reports/minopio_window.md` §3.
// Порт эталонного линейного поиска по `+0` строки `TIM_List.BIN` (дедуп Д5 — см. .h).
const FAsdaTimListRow* UAsdaQuestMarkerSubsystem::TimRowByMapId(int32 MapId) const
{
	for (const FAsdaTimListRow* T : TimRows) { if (T && T->MapId == MapId) { return T; } }
	return nullptr;
}

void UAsdaQuestMarkerSubsystem::ComputeAwaitMaps(int32 MapId)
{
	ASDA_PERF_SCOPE("Quest.ComputeAwaitMaps");   // ★ R4-c (14.11.2026): область замера (AsdaPerf.h; в Shipping пусто)
	AwaitMaps.Reset();
	AwaitMarkup.Reset();

	// Эталон: `if (npcCount <= 0) return;` — пока таблицы нет, считать нечего.
	if (NpcRows.Num() == 0 || TimRows.Num() == 0 || NmsRows.Num() == 0) { return; }
	// Метки считаются только после прихода 5047 — без состояния журнала список был бы выдумкой.
	if (!bStateSynced || !ScriptTable) { return; }

	// ★★★ ДЕФЕКТ ОРИГИНАЛА §7.3: эталон читает `NMS[44*mapId]` БЕЗ проверки границ, а записей
	//   в файле всего 12 при 23+ картах в игре — на картах с id ≥ 12 он берёт мусор за буфером.
	//   Здесь честная проверка: нет строки — подсказки нет (в отчёте помечено как исправление).
	if (MapId < 0 || !NmsRows.IsValidIndex(MapId) || NmsRows[MapId] == nullptr) { return; }

	// Порт `listAddUniqueSlot10@0xD98E80`: 10 слотов, без дублей, переполнение молча игнорируется.
	auto AddUniqueSlot10 = [this](int32 Id)
	{
		if (AwaitMaps.Num() >= 10 || AwaitMaps.Contains(Id)) { return; }
		AwaitMaps.Add(Id);
	};

	// Шаг 1 — кандидаты из строки текущей карты, отсев по TIM_List (эталон делает это ДО обхода NPC).
	for (int32 Cand : NmsRows[MapId]->Related)
	{
		if (Cand < 0 || !TimRowByMapId(Cand)) { continue; }   // белый список TIM_List — общая точка Д5

		// Шаг 2 — `npcData_buildTypeList@0xD98A70`: есть ли на карте `Cand` хоть один NPC, который
		//   прямо сейчас может ВЫДАТЬ новый квест. Тип NPC у сборщика {0, 4} — БЕЗ 5 (крафт),
		//   в отличие от списка точек мини-карты {0, 4, 5}; метка обязана быть РОВНО 1 (жёлтый «!»).
		//   ⇒ «сдать» (метка 2, красный «!») и «?» (метка 3) в подсказку НЕ ПОПАДАЮТ.
		//   Дословно `npcData_LoadModelEntry@0xD98D40`: `return MatchingActionMode2 == 1;`
		//   ★ Это ПОВЕДЕНИЕ ОРИГИНАЛА и решение владельца (2026-08-22): места сдачи не добавляем.
		bool bAnyGiver = false;
		for (const FAsdaNpcQuestNpcRow* N : NpcRows)
		{
			if (!N || N->MapId != Cand) { continue; }
			if (AsdaNpcGmOnly::HiddenForSelfOnMap(GetGameInstance(), N->NpcId, N->MapId)) { continue; }   // ★ 09.11.2026: NPC «только для GM_» — меток нет
			if (N->NpcType != 0 && N->NpcType != 4) { continue; }
			if (N->NpcId <= 0) { continue; }
			if (EvalMarkForNsr(N->NsrName) == 1) { bAnyGiver = true; break; }
		}
		if (bAnyGiver) { AddUniqueSlot10(Cand); }
	}

	AwaitMarkup = BuildAwaitHintMarkup();
}

// Порт склейки строки из `BuffNameList_BuildColoredText@0xD98210` (`skill_buff.c:29826-29904`).
FString UAsdaQuestMarkerSubsystem::BuildAwaitHintMarkup() const
{
	if (AwaitMaps.Num() == 0) { return FString(); }

	// Цвет назначается по ПОРЯДКОВОМУ НОМЕРУ совпадения, первый — без запятой. Логика эталонная
	//   (`BuffNameList_BuildColoredText@0xD98210`, `switch (matchCount)`, `skill_buff.c:29849-29888`).
	//   Порядок цветов — дословно из веток 1…8; обрати внимание, что САМ ЭТАЛОН уже переиспользует
	//   первый цвет на седьмом слоте (ветки 1 и 7 — обе `FFFF8040`), то есть ряд у него замкнутый.
	static const TCHAR* kColors[8] = {
		TEXT("FFFF8040"), TEXT("FFFF80FF"), TEXT("FFFF0000"), TEXT("FF00FF00"),
		TEXT("FF800080"), TEXT("FFFFFFFF"), TEXT("FFFF8040"), TEXT("FF00FFFF"),
	};

	FString List;
	int32 MatchCount = 0;
	for (int32 Id : AwaitMaps)
	{
		const FAsdaTimListRow* Row = TimRowByMapId(Id);   // дедуп Д5
		if (!Row) { continue; }                     // второй проход `buffNameList_filterActiveAndRefresh`
		++MatchCount;

		// ★★★ FIX-ORIG (дефект §7.4 отчёта `Reports/minopio_window.md`).
		//   ЭТАЛОН: слотов списка ДЕСЯТЬ (`CQuestMgr+1420`, `listAddUniqueSlot10@0xD98E80`), а веток
		//     `switch` — ВОСЕМЬ; девятая и десятая карты попадают в `default: break`, где в строку
		//     не дописывается НИЧЕГО — ни цвета, ни запятой, ни самого названия. Карта, которую
		//     сборщик честно нашёл, молча пропадает из подсказки.
		//   ЧИНИМ: продолжаем ряд ПО ТОМУ ЖЕ ПРАВИЛУ, которым эталон набирает первые восемь, —
		//     цвета идут по кругу (`% 8`). Ничего не выдумано: эталон сам берёт на седьмом слоте
		//     цвет первого, то есть замыкание ряда — его собственный приём, мы лишь доводим его
		//     до конца. Слот 9 получает цвет слота 1, слот 10 — цвет слота 2.
		//   ⚠ НА ТЕКУЩИХ ДАННЫХ ВЕТКА НЕДОСТИЖИМА, и это ИЗМЕРЕНО, а не предположено: в `TIM_List.BIN`
		//     8 карт, но НИ ОДНА строка `NMS_list.BIN` не ссылается больше чем на 6 из них (у всех
		//     12 строк это один и тот же набор {0,1,2,3,6,7}) ⇒ `MatchCount ≤ 6`. Правка страхует
		//     данные, где список расширят.
		const TCHAR* Color = kColors[(MatchCount - 1) % 8];

		List += FString::Printf(TEXT("%s<FC=%s>%s</F>"),
			MatchCount == 1 ? TEXT("") : TEXT(", "), Color,
			*AsdaLangRows::Text(AsdaDt::Cached(kTimDT), Row, TEXT("VillageName"), Row->VillageName));   // [ПОРТ] 09.10.2026 (мультиязычность): оверлей `TimList`, ключ `<MapId>.VillageName`
	}
	if (MatchCount == 0) { return FString(); }

	UGameInstance* GI = GetGameInstance();
	UAsdaTextSubsystem* Txt = GI ? GI->GetSubsystem<UAsdaTextSubsystem>() : nullptr;
	// ★ Нотис 6172 берётся СЫРЫМ: у строки нет ни слотов `[ssN]`, ни printf-аргументов — эталон
	//   склеивает обычным `sprintf("%s %s", …)`, а не `FormatById`/`FormatPrintfById`.
	const FString Notice = Txt ? Txt->GetTextByIdRaw(6172) : FString();
	if (Notice.IsEmpty()) { return List; }

	// Порт `charClassifyAscii@0x989320`: класс 2 = ПЕРВЫЙ символ — латинская буква (`A-Za-z`).
	//   Тогда «фраза, потом список», иначе — «список, потом фраза».
	//   ★ ДЕФЕКТ ОРИГИНАЛА §7.6: кириллица в класс 2 не попадает (байт ≥ 0x80 при знаковом `char`
	//   даёт класс 1, как у хангыля), поэтому русская строка склеивается «список — фраза».
	//   Повторяем 1:1.
	const TCHAR C0 = Notice[0];
	const bool bLatinFirst = (C0 >= TEXT('A') && C0 <= TEXT('Z')) || (C0 >= TEXT('a') && C0 <= TEXT('z'));
	return bLatinFirst ? (Notice + TEXT(" ") + List) : (List + TEXT(" ") + Notice);
}

// Порт условий показа из `BuffNameList_BuildColoredText@0xD98210` + вызовов
//   `npcExchangeUiSetText@0xDA3D60` / `npcExchangeUiReset@0xDA3DF0`.
const TCHAR* UAsdaQuestMarkerSubsystem::AwaitGateName(EAsdaAwaitGate Gate)
{
	switch (Gate)
	{
	case EAsdaAwaitGate::NoDialog:   return TEXT("подсистемы диалога NPC нет");
	case EAsdaAwaitGate::Option:     return TEXT("опция выключена (Asda.Quest.AwaitHint 0)");
	case EAsdaAwaitGate::Map19:      return TEXT("карта 19 (Поле битвы) — эталон гасит подсказку");
	case EAsdaAwaitGate::Empty:      return TEXT("список пуст — показывать нечего");
	case EAsdaAwaitGate::Level:      return TEXT("уровень <= 2");
	case EAsdaAwaitGate::DialogOpen: return TEXT("открыт разговор с NPC");
	case EAsdaAwaitGate::Loading:    return TEXT("мир ещё не показан (загрузочный экран) — показ отложен");
	case EAsdaAwaitGate::Unchanged:  return TEXT("состав списка не менялся — второй раз не поднимаем");
	case EAsdaAwaitGate::Shown:      return TEXT("ПОКАЗАНО");
	default:                         return TEXT("ещё не считали");
	}
}

// Одна строка в лог на СМЕНУ причины. Без этого условия лог сорился бы раз в секунду —
//   `RebuildMarks()` тикает периодически (`Asda.Quest.MarkRebuildPeriod`).
void UAsdaQuestMarkerSubsystem::LogAwaitGate(EAsdaAwaitGate Gate, const FString& Detail)
{
	if (Gate == AwaitGate) { return; }
	AwaitGate = Gate;
	UE_LOG(LogAsdaQuest, Log, TEXT("[Минопио] %s"), *Detail);
}

void UAsdaQuestMarkerSubsystem::MaybeShowAwaitHint(int32 MapId)
{
	UGameInstance* GI = GetGameInstance();
	UAsdaNpcDialogSubsystem* Dlg = GI ? GI->GetSubsystem<UAsdaNpcDialogSubsystem>() : nullptr;
	if (!Dlg)
	{
		LogAwaitGate(EAsdaAwaitGate::NoDialog, TEXT("не показываю: подсистемы диалога NPC нет"));
		return;
	}

	// (0) ★★★ МИР ЕЩЁ НЕ ПОКАЗАН — ОТЛОЖИТЬ (разбор `Reports/minopio_window_fix.md`).
	//     Эталон зовёт сбор+показ (`npcUiRefreshActionList`) в КОНЦЕ автомата загрузки, когда
	//     `g_GameState = 1` и окна мира уже открыты (`game_main.c:3445`, `:3872`). У порта расчёт
	//     запускает приход журнала 5047, а он прилетает ещё под загрузочным экраном: баннер
	//     поднимался невидимым и через 0,15–0,25 с его сносил `CloseAll()` внутри
	//     `UAsdaWorldSubsystem::RevealWorld()` (именно там снимается экран). Поэтому здесь —
	//     ВЫХОД БЕЗ ЗАПИСИ СОСТОЯНИЯ ПОКАЗА: иначе страховка «состав не менялся» (шаг 6) навсегда
	//     закрыла бы дорогу второму, уже видимому показу. `Tick` поднимет баннер первым же кадром
	//     после снятия экрана.
	{
		UAsdaLoadManagerSubsystem* LM = GI->GetSubsystem<UAsdaLoadManagerSubsystem>();
		if (LM && LM->IsLoadingScreenVisible())
		{
			bAwaitHintPending = true;
			LogAwaitGate(EAsdaAwaitGate::Loading,
				FString::Printf(TEXT("не показываю: идёт загрузка карты %d, показ отложен до снятия экрана (карт в списке %d)"),
					MapId, AwaitMaps.Num()));
			return;
		}
	}

	// (1) Опция `g_bOptFlag_15CB4E8`.
	if (GAsdaQuestAwaitHint == 0)
	{
		bAwaitHintPending = false;
		LogAwaitGate(EAsdaAwaitGate::Option, TEXT("не показываю: опция выключена (Asda.Quest.AwaitHint 0)"));
		return;
	}

	// (2) `if (g_nCurMapId == 19) npcExchangeUiReset(this);` — на «Поле битвы» (CampWar) подсказка
	//     принудительно гасится. Гасим ТОЛЬКО свой баннер: в порту то же окно занимает подсказка
	//     точки сохранения, и сбивать её мы права не имеем (в эталоне владелец один — `g_pNpcWnd`).
	if (MapId == 19)
	{
		if (bAwaitHintShown) { Dlg->HideQuestInfo(); bAwaitHintShown = false; }
		ShownAwaitMaps.Reset();
		AwaitShownOnMap = -1;
		bAwaitHintPending = false;
		LogAwaitGate(EAsdaAwaitGate::Map19, TEXT("не показываю: карта 19 (Поле битвы) — эталон подсказку гасит"));
		return;
	}

	// (3) Ничего не набралось — эталон текст не ставит вовсе (окно остаётся как было).
	if (AwaitMarkup.IsEmpty())
	{
		ShownAwaitMaps.Reset();
		AwaitShownOnMap = MapId;
		bAwaitHintPending = false;
		LogAwaitGate(EAsdaAwaitGate::Empty,
			FString::Printf(TEXT("не показываю: на карте %d список «где ждут» пуст — ни на одной связанной карте нет NPC ")
				TEXT("типа 0/4 с меткой 1 (новый квест можно взять); состояние с сервера=%s"),
				MapId, bStateSynced ? TEXT("есть") : TEXT("НЕТ (5047 не приходил)")));
		return;
	}

	// (4) `if (level > 2)` — уровень СТРОГО больше 2.
	int32 Level = 0, Cls = 0, Sub = 0;
	ReadSelfLevelClass(Level, Cls, Sub);
	if (Level <= 2)
	{
		bAwaitHintPending = false;
		LogAwaitGate(EAsdaAwaitGate::Level,
			FString::Printf(TEXT("не показываю: уровень персонажа %d, эталон требует строго > 2"), Level));
		return;
	}

	// (5) Окно разговора с NPC открыто — эталон в этом случае даже не собирает список
	//     (`npcUiRefreshActionList`: `if (!this[4099])`).
	if (Dlg->IsDialogOpen())
	{
		// Отложенный показ НЕ снимаем: закроет диалог — покажем.
		LogAwaitGate(EAsdaAwaitGate::DialogOpen, TEXT("не показываю: открыт разговор с NPC"));
		return;
	}

	// ★ ОТЛИЧИЕ ПОРТА ПО НЕОБХОДИМОСТИ. В эталоне сбор идёт СТРОГО ПО СОБЫТИЮ (вход в мир, смена
	//   карты, повышение уровня, изменение журнала), поэтому баннер поднимается ровно один раз на
	//   событие. У нас `RebuildMarks()` дополнительно тикает раз в секунду как страховка
	//   (`Asda.Quest.MarkRebuildPeriod`), и без этой проверки окно висело бы вечно. Условие
	//   «карта сменилась ИЛИ состав списка изменился» даёт ровно эталонный набор моментов показа.
	if (MapId == AwaitShownOnMap && ShownAwaitMaps == AwaitMaps)
	{
		bAwaitHintPending = false;
		LogAwaitGate(EAsdaAwaitGate::Unchanged,
			FString::Printf(TEXT("не показываю: на карте %d состав списка (%d карт) не менялся с прошлого показа"),
				MapId, AwaitMaps.Num()));
		return;
	}

	ShownAwaitMaps = AwaitMaps;
	AwaitShownOnMap = MapId;
	bAwaitHintShown = true;
	bAwaitHintPending = false;
	AwaitGate = EAsdaAwaitGate::Shown;

	// ★ Строка уходит СЫРОЙ, с тегами `<FC=…>`: с 2026-08-22 `ShowQuestInfoEx` прогоняет её через
	//   `UAsdaRichText::ApplyFormatted` внутри себя (и переприсваивает свой кэш `QiText`), поэтому
	//   цвета названий карт видны. `bMarkBusy = false` — порт `BuffNameList_BuildColoredText@0xD98210`:
	//   он ставит только признак +370 и НЕ взводит +4211, из-за чего висящую подсказку «Минопио»
	//   уведомление о смерти не стирает, а откладывает в очередь (`uiPostDeathRedNotice`).
	Dlg->ShowQuestInfoEx(AwaitMarkup, /*bMarkBusy*/ false, /*bMayDefer*/ false);

	UE_LOG(LogAsdaQuest, Log, TEXT("[Минопио] карта %d, ждут на %d: «%s»"),
		MapId, AwaitMaps.Num(), *AwaitMarkup);
}

FString UAsdaQuestMarkerSubsystem::DescribeAwaitMaps() const
{
	FString Names;
	for (int32 Id : AwaitMaps)
	{
		if (const FAsdaTimListRow* T = TimRowByMapId(Id))   // дедуп Д5
		{
			Names += (Names.IsEmpty() ? TEXT("") : TEXT(", ")) + T->VillageName;
		}
	}
	int32 Level = 0, Cls = 0, Sub = 0;
	ReadSelfLevelClass(Level, Cls, Sub);
	// ★ Состояние ОБОИХ гейтов момента: экран загрузки (гейт 0) и разговор с NPC (гейт 5).
	//   Без них диагностика врала: список считался, а показа не было — и причина не называлась.
	UGameInstance* GI = GetGameInstance();
	const UAsdaLoadManagerSubsystem* LM = GI ? GI->GetSubsystem<UAsdaLoadManagerSubsystem>() : nullptr;
	const UAsdaNpcDialogSubsystem* Dlg = GI ? GI->GetSubsystem<UAsdaNpcDialogSubsystem>() : nullptr;
	return FString::Printf(
		TEXT("карта=%d уровень=%d (нужен >2) опция=%d состояние_с_сервера=%s | TIM_List=%d NMS_list=%d\n")
		TEXT("загрузочный экран=%s разговор с NPC=%s показ отложен=%s | причина: %s\n")
		TEXT("ждут на картах: %s%s\n")
		TEXT("сырая строка: %s"),
		AwaitShownOnMap, Level, GAsdaQuestAwaitHint,
		bStateSynced ? TEXT("да") : TEXT("НЕТ (пакет 5047 не приходил)"),
		TimRows.Num(), NmsRows.Num(),
		(LM && LM->IsLoadingScreenVisible()) ? TEXT("ВИСИТ") : TEXT("снят"),
		(Dlg && Dlg->IsDialogOpen()) ? TEXT("открыт") : TEXT("закрыт"),
		bAwaitHintPending ? TEXT("да") : TEXT("нет"),
		AwaitGateName(AwaitGate),
		AwaitMaps.Num() ? *FString::JoinBy(AwaitMaps, TEXT(" "), [](int32 I) { return FString::FromInt(I); }) : TEXT("—"),
		Names.IsEmpty() ? TEXT("") : *FString::Printf(TEXT(" (%s)"), *Names),
		AwaitMarkup.IsEmpty() ? TEXT("(пусто)") : *AwaitMarkup);
}

// ★★★ [ОРИГИНАЛ] 30.10.2026 (баг владельца, выпуск 5.0.2: «на большой карте у Inferion, Flammio и др. нет значков квестов»).
//   Эталон `MiniMap_OpenBigMap@0xD66D40` (world_map.c:7688) при показе ЛЮБОЙ карты в окне 1710 (своя, выбранная в списке
//   «Область») зовёт `npcUi_BuildNpcActionList(g_pNpcWnd, показанная_карта, 1)` — метки NPC ЭТОЙ карты строятся во второй
//   список мини-карты (`partyListPushEntry`, `a6 = 1` → `this+239`), и большая карта их рисует. Порт считал метки только для
//   NPC текущей карты (вызов с `a3 = 0`), поэтому на чужой карте значков не было вовсе. 0 = прежнее поведение порта.
static ASDA_DEV_CVAR(int32, CVarAsdaMapOtherMapQuestMarks,
	TEXT("asda.Map.OtherMapQuestMarks"), 1,
	TEXT("1 = большая карта показывает метки квестов NPC той карты, что выбрана в окне (как оригинал); 0 = только текущей карты."),
	ECVF_Default);

// ★★★ [ПОРТ] 31.10.2026 ЗНАЧКИ ЦЕЛЕЙ ВЗЯТЫХ ЗАДАНИЙ НА КАРТЕ МИРА (решение владельца «добавить»). У оригинала большая карта
//   рисует только квестодателей (`npcUi_BuildNpcActionList`, список `+239`) — значков целей у него нет. Источники — данные
//   клиента и сервера, ничего не выдумывается:
//     1) гид `QDT.bin` (`DT_QuestGuide`) — координаты цели шага (моб/NPC), те же, что у строки журнала «=> цель — карта, x, y»;
//     2) шаг без координат гида — места мобов, дающих предмет шага (`DT_QuestTargetArea`: серверные `Monster.ai` + `.svr`);
//     3) условие завершения выполнено (`IsQuestObjectiveDone`, `QCT.BIN`) — NPC, который задание ПРИНИМАЕТ (`DT_NpcSnq`).
//   Только для заданий в журнале (слот занят), которые отслеживаются экранным трекером, только для показанной карты.
//   0 = значков целей нет.
static ASDA_DEV_CVAR(int32, CVarAsdaMapQuestTargets,
	TEXT("asda.Map.QuestTargets"), 1,
	TEXT("1 = карта мира показывает значки целей взятых заданий (места мобов, точки гида, NPC сдачи) с подсказкой; 0 = нет."),
	ECVF_Default);

static const TCHAR* kQuestGuideDT  = TEXT("/Game/AsdaAssets/Data/Quest/DT_QuestGuide.DT_QuestGuide");
static const TCHAR* kQuestAreaDT   = TEXT("/Game/AsdaAssets/Data/Quest/DT_QuestTargetArea.DT_QuestTargetArea");

void UAsdaQuestMarkerSubsystem::BuildQuestTargetMarks(int32 ShownMap, TArray<FAsdaMapMark>& Out) const
{
	if (ShownMap < 0 || CVarAsdaMapQuestTargets.GetValueOnGameThread() == 0 || !bStateSynced) { return; }
	HoldGameTables();   // ★ 31.10.2026 (баг 6): гид, места мобов и SNQ — сильно у подсистемы на сессию (dt_gc_hold)
	UGameInstance* GI = GetGameInstance();
	UAsdaTextSubsystem* TS = GI ? GI->GetSubsystem<UAsdaTextSubsystem>() : nullptr;
	const FString MapName = TS ? TS->GetText(TEXT("Map_info"), ShownMap, TEXT("name")) : FString();
	const FString MapStr = MapName.IsEmpty() ? FString::Printf(TEXT("#%d"), ShownMap) : MapName;
	TSet<FIntPoint> Seen;   // одна точка карты — один значок (несколько заданий в одной клетке не плодят стопку)
	auto Add = [&](int32 TileX, int32 TileY, const FString& Title, const FString& Target)
	{
		if (TileX < 0 || TileY < 0) { return; }
		const FIntPoint K(TileX, TileY);
		FAsdaMapMark M;
		M.MapId = ShownMap; M.TileX = TileX; M.TileY = TileY; M.Kind = EAsdaMapMark::QuestTarget;
		M.TipTitle = Title;
		// Строка цели — формат журнала 1:1 (`QuestGuide_BuildObjectiveText@0xC68E00`).
		M.TipBody = FString::Printf(TEXT("=> %s - %s, x:%d, y: %d"), *Target, *MapStr, TileX, TileY);
		if (Seen.Contains(K))
		{
			for (FAsdaMapMark& E : Out)
			{
				if (E.TileX == TileX && E.TileY == TileY && !E.TipTitle.Contains(Title)) { E.TipTitle += TEXT(", ") + Title; }
			}
			return;
		}
		Seen.Add(K);
		Out.Add(M);
	};
	const UDataTable* Guide = AsdaDt::Cached(kQuestGuideDT);
	const UDataTable* Area  = AsdaDt::Cached(kQuestAreaDT);
	const UDataTable* Snq   = AsdaDt::Cached(kSnqDT);
	for (const FAsdaQuestSlot& S : Slots)
	{
		if (S.QuestId < 2001 || S.QuestNo < 0) { continue; }   // слот пуст (эталонная константа занятости)
		// ★ 31.10.2026 (решение владельца): только задания, которые сейчас в экранном трекере (окно 1800) — тот же список
		//   `TrackEntries`, что у трекера; вынули из трекера — точки пропадают, добавили — появляются (пересчёт в Tracker*).
		//   Задание, снятое с трекера потому, что его цель выполнена, остаётся с точкой NPC сдачи до сдачи/отказа.
		if (!IsQuestTargetShown(S.QuestNo)) { continue; }
		// Название: задание доски объявлений (20000+) — из её таблицы (`Raid_quest_data`, RU/EN), прочие — из таблицы эпизодов.
		FString Title = (S.QuestNo >= UAsdaBoardQuestSubsystem::kQuestBase) ? UAsdaBoardQuestSubsystem::QuestName(S.QuestNo)
			: EpisodeTitleByQuestKey(S.QuestNo);
		if (Title.IsEmpty()) { Title = FString::Printf(TEXT("#%d"), S.QuestNo); }
		// 3) Условие завершения выполнено — значок на NPC, который задание принимает.
		//   ★ 31.10.2026 (баг 6): NPC сдачи и его место — ОБЩАЯ точка с кнопками маршрута журнала (`AsdaQuestGuideTarget::TurnInNpcs`),
		//   чтобы значок и «Следовать» вели в одно место. Ключ `asda.Quest.FollowDoneToNpc 1`: один NPC, у которого СЕРВЕР
		//   примет сдачу на этой стадии (`DT_QuestTurnIn`), место — его серверный спавн; такого NPC нет — значки строк гида шага,
		//   как у кнопок журнала. Ключ 0 — как было: все NPC с ячейкой сдачи клиентского SNQ, место из `npc_list`.
		if (IsQuestObjectiveDone(S.QuestNo) && Snq)
		{
			TArray<AsdaQuestGuideTarget::FTurnIn> Npcs;
			const bool bByStep = AsdaQuestGuideTarget::FollowDoneToNpcOn();
			AsdaQuestGuideTarget::TurnInNpcs(this, S.QuestNo, S.Stage, bByStep, Npcs);
			if (bByStep && Npcs.Num() > 1)
			{
				// Законная пара — тот же один NPC, что у кнопки журнала (ближайший к персонажу).
				UWorld* W = GI ? GI->GetWorld() : nullptr;
				const APawn* Me = (W && W->GetFirstPlayerController()) ? W->GetFirstPlayerController()->GetPawn() : nullptr;
				const UAsdaLoadManagerSubsystem* LM = GI ? GI->GetSubsystem<UAsdaLoadManagerSubsystem>() : nullptr;
				AsdaQuestGuideTarget::FTurnIn One;
				if (AsdaQuestGuideTarget::PickTurnIn(Npcs, LM ? LM->CurrentMapId : -1,
					Me ? AsdaCoord::GameToTile(AsdaCoord::UeToGame2D(Me->GetActorLocation())) : FIntPoint(0, 0), One))
				{
					Npcs = { One };
				}
			}
			for (const AsdaQuestGuideTarget::FTurnIn& T : Npcs)
			{
				if (T.Place.MapId == ShownMap) { Add(T.Place.TileX, T.Place.TileY, Title, AsdaEntityText::QuestTargetName(0, T.NpcId)); }
			}
			if (Npcs.Num() > 0 || !AsdaQuestGuideTarget::FollowDoneToNpcOn()) { continue; }
		}
		// 1) Гид: координаты цели шага (у шага может быть до 4 целей на разных картах).
		bool bGuideHasXY = false;
		if (Guide)
		{
			for (const TPair<FName, uint8*>& It : Guide->GetRowMap())
			{
				const FAsdaQuestGuideRow* G = reinterpret_cast<const FAsdaQuestGuideRow*>(It.Value);
				if (!G || G->QuestNo != S.QuestNo || G->Stage != S.Stage || G->X == -1 || G->Y == -1) { continue; }
				bGuideHasXY = true;
				if (G->MapId == ShownMap) { Add(G->X, G->Y, Title, AsdaEntityText::QuestTargetName(G->TargetKind, G->TargetId)); }
			}
		}
		// 2) Координат в гиде нет — места мобов, дающих предмет этого шага (серверный спавн).
		if (!bGuideHasXY && Area)
		{
			for (const TPair<FName, uint8*>& It : Area->GetRowMap())
			{
				const FAsdaQuestTargetAreaRow* A = reinterpret_cast<const FAsdaQuestTargetAreaRow*>(It.Value);
				if (!A || A->QuestNo != S.QuestNo || A->Stage != S.Stage || A->MapId != ShownMap) { continue; }
				// Серверный тип моба = строка таблицы мобов + 1; общая точка имени цели ждёт строку (как гид).
				Add(A->TileX, A->TileY, Title, AsdaEntityText::QuestTargetName(1, A->MobType - 1));
			}
		}
	}
}

void UAsdaQuestMarkerSubsystem::PushMapMarks(int32 MapId)
{
	ASDA_PERF_SCOPE("Quest.PushMapMarks");   // ★ R4-c (14.11.2026): область замера (AsdaPerf.h; в Shipping пусто)
	UGameInstance* GI = GetGameInstance();
	UAsdaMinimapSubsystem* MM = GI ? GI->GetSubsystem<UAsdaMinimapSubsystem>() : nullptr;
	if (!MM) { return; }

	TArray<FAsdaMapMark> M1, M2;
	UAsdaBoardQuestSubsystem* Board = GI->GetSubsystem<UAsdaBoardQuestSubsystem>();
	// Вторая карта — показанная в открытом окне большой карты, если это не текущая (эталонный вызов с `a3 = 1`).
	const int32 ShownMap = (CVarAsdaMapOtherMapQuestMarks.GetValueOnGameThread() != 0 && MM->IsBigMapOpen())
		? MM->GetDisplayedMapId() : -1;
	for (const FAsdaNpcQuestNpcRow* N : NpcRows)
	{
		if (N && ShownMap >= 0 && ShownMap != MapId && N->MapId == ShownMap && bStateSynced && ScriptTable)
		{
			// Тот же отбор, что в `RebuildMarks` (`npcUi_BuildNpcActionList`: типы 0, 4, 5) и та же метка по сценарию NPC —
			//   она от карты не зависит (`EvalMarkForNsr`); доска объявлений — своим счётом, как ниже для текущей карты.
			int32 Mk = 0;
			if (N->NpcType == 5 && Board && UAsdaBoardQuestSubsystem::IsEnabled()) { Mk = Board->MinimapMark(N->NpcId); }
			else if (N->NpcType == 0 || N->NpcType == 4 || N->NpcType == 5) { Mk = EvalMarkForNsr(N->NsrName); }
			if (Mk == 1 || Mk == 2)
			{
				FAsdaMapMark P;
				P.MapId = ShownMap;
				P.TileX = FMath::FloorToInt(N->WorldX / AsdaCoord::kTileUnits);
				P.TileY = FMath::FloorToInt(N->WorldY / AsdaCoord::kTileUnits);
				P.Kind  = (Mk == 2) ? EAsdaMapMark::Quest2 : EAsdaMapMark::Quest;
				((Mk == 2) ? M2 : M1).Add(P);
			}
			continue;
		}
		if (!N || N->MapId != MapId) { continue; }
		if (AsdaNpcGmOnly::HiddenForSelfOnMap(GetGameInstance(), N->NpcId, N->MapId)) { continue; }   // ★ 09.11.2026: NPC «только для GM_» — меток нет
		// ★ 28.10.2026: доска объявлений (тип 5) — точка «!» (метка 1) при `craftCheckRecipeMaterialsAvailable == 1`.
		if (N->NpcType == 5 && Board && UAsdaBoardQuestSubsystem::IsEnabled())
		{
			if (bStateSynced && Board->MinimapMark(N->NpcId) == 1)
			{
				FAsdaMapMark P;
				P.MapId = MapId;
				P.TileX = FMath::FloorToInt(N->WorldX / AsdaCoord::kTileUnits);
				P.TileY = FMath::FloorToInt(N->WorldY / AsdaCoord::kTileUnits);
				P.Kind  = EAsdaMapMark::Quest;
				M1.Add(P);
			}
			continue;
		}
		const int32* Mk = MarkByNpcId.Find(N->NpcId);
		if (!Mk) { continue; }
		// Метка 3 («?») на карту НЕ идёт: partyListPushEntry зовётся только при метке 1 или 2.
		if (*Mk != 1 && *Mk != 2) { continue; }
		FAsdaMapMark P;
		P.MapId = MapId;
		// ★ N11: размер тайла — `AsdaCoord::kTileUnits`. Деление остаётся во float (координаты NPC — float), как было:
		//   `GameToTile` делит в double, на краю клетки это могло бы дать другой тайл.
		P.TileX = FMath::FloorToInt(N->WorldX / AsdaCoord::kTileUnits);
		P.TileY = FMath::FloorToInt(N->WorldY / AsdaCoord::kTileUnits);
		P.Kind  = (*Mk == 2) ? EAsdaMapMark::Quest2 : EAsdaMapMark::Quest;
		((*Mk == 2) ? M2 : M1).Add(P);
	}
	MM->SetMarks(EAsdaMapMark::Quest,  M1);
	MM->SetMarks(EAsdaMapMark::Quest2, M2);
	// ★ [ПОРТ] 31.10.2026 значки целей взятых заданий — для карты, показанной в открытом окне карты мира.
	TArray<FAsdaMapMark> Targets;
	BuildQuestTargetMarks(MM->IsBigMapOpen() ? MM->GetDisplayedMapId() : -1, Targets);
	MM->SetMarks(EAsdaMapMark::QuestTarget, Targets);
}

void UAsdaQuestMarkerSubsystem::PushOverheadMarks()
{
	ASDA_PERF_SCOPE("Quest.PushOverheadMarks");   // ★ R4-c (14.11.2026): область замера (AsdaPerf.h; в Shipping пусто)
	UWorld* W = GetGameInstance() ? GetGameInstance()->GetWorld() : nullptr;
	if (!W) { return; }
	const UAsdaBoardQuestSubsystem* Board = GetGameInstance()->GetSubsystem<UAsdaBoardQuestSubsystem>();
	for (TActorIterator<AAsdaNpcActor> It(W); It; ++It)
	{
		AAsdaNpcActor* Npc = *It;
		if (!Npc) { continue; }
		const int32* Mk = MarkByNpcId.Find(Npc->NpcId);
		int32 Mark = Mk ? *Mk : 0;
		// ★ 28.10.2026 [ИСПРАВЛЕНИЕ ОРИГИНАЛА] (ключ `asda.Board.FixOverheadMark`): NPC-доска объявлений получает значок по
		//   своим заданиям — красный «!» есть что сдать, жёлтый «!» есть что взять (`UAsdaBoardQuestSubsystem::OverheadMark`).
		//   У эталона значка над доской нет. Считается только после 5047 (как и метки квестодателей).
		if (Board && Board->IsBoardNpc(Npc->NpcId)) { Mark = bStateSynced ? Board->OverheadMark(Npc->NpcId) : 0; }
		// ★ Диагностический принудительный режим (консоль `Asda.Quest.MarkAll`) — см. ForcedMarkForAll.
		Npc->SetQuestMark(ForcedMarkForAll >= 0 ? ForcedMarkForAll : Mark);
	}
}

// ★ ДИАГНОСТИКА: принудительная метка на всех NPC карты (проверка высоты без сервера).
void UAsdaQuestMarkerSubsystem::SetForcedMarkForAll(int32 Mark)
{
	ForcedMarkForAll = (Mark >= 0 && Mark <= 3) ? Mark : -1;
	PushOverheadMarks();
	UE_LOG(LogAsdaQuest, Log, TEXT("[квест-метки] ПРИНУДИТЕЛЬНАЯ метка на всех NPC: %s"),
		ForcedMarkForAll >= 0 ? *FString::FromInt(ForcedMarkForAll) : TEXT("выключено"));
}

void UAsdaQuestMarkerSubsystem::Tick(float DeltaTime)
{
	ASDA_PERF_SCOPE("Tick.QuestMarker");   // ★ R4-c (14.11.2026): область замера (AsdaPerf.h; в Shipping пусто)
	UGameInstance* GI = GetGameInstance();
	UAsdaLoadManagerSubsystem* LM = GI ? GI->GetSubsystem<UAsdaLoadManagerSubsystem>() : nullptr;

	// ★★★ ЭКРАННЫЙ ТРЕКЕР ЗАДАНИЙ (окно 1800) — КАЖДЫЙ КАДР И ДО ПРОВЕРКИ КАРТЫ.
	//   В эталоне `CQuestTracker_BuildWaypointStrings@0xC58A10` зовётся из `MiniMap_Render@0xB4FCC0`,
	//   то есть в кадровом цикле отрисовки: иначе дистанция отставала бы от шага игрока, а стрелка
	//   компаса — от поворота камеры. От готовности карты он не зависит: без неё просто нет цели,
	//   а заголовки записей всё равно показываются. Тело — `Private/Quest/AsdaQuestTracker.cpp`.
	UpdateTrackerWindow(DeltaTime);

	if (!LM || LM->CurrentMapId < 0) { return; }

	if (LM->CurrentMapId != ShownMapId) { bDirty = true; }

	RebuildAccum += DeltaTime;
	const bool bPeriodic = (GAsdaQuestMarkRebuildPeriod > 0.f) && (RebuildAccum >= GAsdaQuestMarkRebuildPeriod);
	if (bDirty || bPeriodic)
	{
		RebuildAccum = 0.0;
		RebuildMarks();
		// ⚠ `OnQuestStateChanged` здесь НЕ шлём: это ПЕРИОДИЧЕСКАЯ страховка пересчёта меток,
		//   состояние журнала при ней не менялось. Иначе окно 700 и трекер 1800 перерисовывались
		//   бы раз в секунду впустую (и сбрасывали бы прокрутку списка).
	}

	// ★★★ ОТЛОЖЕННАЯ ПОДСКАЗКА «МИНОПИО». Расчёт запускает приход журнала 5047 — он прилетает ещё
	//   под загрузочным экраном, а эталон поднимает баннер в КОНЦЕ автомата загрузки, когда мир уже
	//   показан (`npcUiRefreshActionList`, `game_main.c:3445` / `:3872`). Пока экран висит,
	//   `MaybeShowAwaitHint` выходит по гейту (0) и взводит `bAwaitHintPending`; здесь показ
	//   доводится до конца в ПЕРВОМ ЖЕ кадре после `RevealWorld` (он же снимает экран и зовёт
	//   `CloseAll()`, который раньше и сносил невидимый баннер). Проверка дешёвая — сам список уже
	//   посчитан, повторного обхода NPC нет. Разбор: `Reports/minopio_window_fix.md`.
	if (bAwaitHintPending) { MaybeShowAwaitHint(LM->CurrentMapId); }

}

int32 UAsdaQuestMarkerSubsystem::GetMarkForNpcId(int32 NpcId) const
{
	const int32* M = MarkByNpcId.Find(NpcId);
	return M ? *M : 0;
}

FString UAsdaQuestMarkerSubsystem::DescribeMarks() const
{
	int32 N1 = 0, N2 = 0, N3 = 0;
	for (const TPair<int32, int32>& It : MarkByNpcId)
	{
		if (It.Value == 1) { ++N1; } else if (It.Value == 2) { ++N2; } else if (It.Value == 3) { ++N3; }
	}
	int32 Active = 0, Done = 0;
	for (const FAsdaQuestSlot& S : Slots) { if (S.QuestId >= 2001) { ++Active; } }
	for (uint8 B : DoneBits) { Done += FMath::CountBits(B); }
	return FString::Printf(
		TEXT("карта=%d состояние_с_сервера=%s слотов_активно=%d завершено=%d | метки: жёлтый!=%d красный!=%d ?=%d ")
		TEXT("| таблицы: сценариев=%d npc=%d"),
		ShownMapId, bStateSynced ? TEXT("да") : TEXT("НЕТ (пакет 5047 не приходил)"),
		Active, Done, N1, N2, N3, ScriptByNsr.Num(), NpcRows.Num());
}
