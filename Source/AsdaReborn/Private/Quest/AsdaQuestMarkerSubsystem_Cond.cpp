// Copyright (c) 2026 @sitxovski (Discord)
// AsdaQuestMarkerSubsystem_Cond.cpp — ★ R3-c 12.11.2026 (разрез AsdaQuestMarkerSubsystem.cpp, дословный перенос): условия и проверки: цели, предусловия, уровень/класс/предметы, условия сценария, эпизоды, состояние задания.
//   Общее для частей — в AsdaQuestMarkerSubsystem_Private.h; шапка и реверс — в AsdaQuestMarkerSubsystem.cpp.
#include "Quest/AsdaQuestMarkerSubsystem.h"
#include "AsdaQuestMarkTableCache.h"   // ★ R4-c (15.11.2026): локальный кэш строк таблиц пересчёта меток
#include "AsdaQuestEpisodeIndex.h"   // ★ R4-c (14.11.2026): строка эпизода по номеру задания — индекс вместо обхода таблицы
#include "AsdaPerf.h"   // ★ R4-c (14.11.2026): области замера времени игрового потока
#include "Npc/AsdaNpcGmOnly.h"   // ★ 09.11.2026: NPC «только для GM_»
#include "Npc/AsdaNpcDialogTypes.h"      // FAsdaNsrPageRow — проверка наличия страницы сценария
#include "Quest/AsdaQuestGuideTarget.h"   // ★ 31.10.2026 (баг 6): место цели строки гида без координат — общая точка с «Следовать»
#include "AsdaDevConsole.h"   // ★ античит А2: ASDA_DEV_CVAR / ASDA_DEV_CONSOLE — дев-ключи и команды вне Shipping
#include "UI/AsdaInventorySubsystem.h"
#include "AsdaDataTableCache.h"   // ★ Аудит №4 (B2): AsdaDt::Cached — одна ленивая загрузка DataTable на порт

// FAsdaMarkup::StripTags — единая точка разбора тегов <F..>   ★ R3: комментарий снятого включения "UI/AsdaMarkup.h" (файл им не пользовался) — сохранён
#include "Npc/AsdaNpcDialogSubsystem.h"     // ShowQuestInfo/HideQuestInfo — окно QUEST_INFO_WIN (599)
#include "UI/AsdaWindowSubsystem.h"        // ★ Волна В4: роспись «закрыть всё» (трекер 1800)

#include "Engine/DataTable.h"
#include "Engine/Engine.h"
#include "EngineUtils.h"
#include "Containers/Ticker.h"              // ★ 08.11.2026: самотест asda.SelfTest.QuestMarkArshes (шаги по времени)
#include "Text/AsdaLangRows.h"               // [ПОРТ] 09.10.2026 (мультиязычность): названия эпизодов и деревень на языке сессии
#include "AsdaQuestMarkerSubsystem_Private.h"   // ★ R3-c 12.11.2026: общее для частей AsdaQuestMarkerSubsystem*.cpp

static const TCHAR* kEpisodeDT = TEXT("/Game/AsdaAssets/Data/NPC/DT_EpisodeTable.DT_EpisodeTable");

// ════════════════════════════════════════════════════════════════════════════════════════════════
//  УСЛОВИЕ «КВЕСТ ВЫПОЛНЕН» — таблица `QCT.BIN` через ЕДИНЫЙ вычислитель `EvalNsrCond`
// ════════════════════════════════════════════════════════════════════════════════════════════════
// Порт `charStateFindAndBuildRequest@0xC65080` (линейный поиск `rec+0 == questNo`) +
//   `netBuildCharStateRequest@0xC65140` (вычисление). Второе тело ПОБАЙТНО совпадает с
//   `charEquip_VerifyStatChecksum@0xD850D0`, уже портированным как `EvalNsrCond`, поэтому здесь
//   только перенос полей строки в общую структуру условия. ВТОРОГО вычислителя в порту нет.
static const TCHAR* kQuestCompleteDT = TEXT("/Game/AsdaAssets/Data/Quest/DT_QuestComplete.DT_QuestComplete");

// ★ ДЕДУП Д7 (`Reports/quest_npc_dedup_audit.md`). Тринадцать полей условия называются
//   ОДИНАКОВО во всех трёх строках-источниках (`.NSR`-сценарий, `QCT.BIN`, `SNQ/ICT`), потому что
//   это одна и та же эталонная структура, разложенная по трём таблицам. Механическая переписка
//   жила тремя копиями — теперь она одна, шаблоном по типу строки.
//   ⚠ СЮДА НЕ ВХОДЯТ `Opcode` и пара «слот сумки / прочее»: у неё РАЗНЫЕ смещения в раскладках
//   `Script` (+136/+140) и `SubRecord` (+142/+144), и именно в этом различии вся суть — оно
//   обязано оставаться ЯВНЫМ в каждой из трёх функций, рядом со своим обоснованием.
template <typename TRow>
static void CopyCondCommon(const TRow& R, FAsdaNsrCond& C)
{
	C.LevelMin    = R.LevelMin;    C.LevelMax   = R.LevelMax;
	C.ClassId     = R.ClassId;     C.ClassMode  = R.ClassMode;
	C.QuestFreeA  = R.QuestFreeA;  C.QuestDone  = R.QuestDone;
	C.QuestActive = R.QuestActive; C.QuestFreeB = R.QuestFreeB;
	C.QuestStage  = R.QuestStage;
	C.ObjIds      = R.ObjIds;      C.ObjCounts  = R.ObjCounts;
	C.ObjFlagNone = R.ObjFlagNone; C.ObjFlagAny = R.ObjFlagAny;
	C.ReqItemIds  = R.ReqItemIds;  C.ReqItemCounts = R.ReqItemCounts;
	C.HaveItemId  = R.HaveItemId;
	C.BagTabA     = R.BagTabA;     C.BagTabB    = R.BagTabB;
}

static FAsdaNsrCond CondFromQctRow(const FAsdaQuestCompleteRow& R)
{
	FAsdaNsrCond C;
	CopyCondCommon(R, C);
	// Раскладка `Script`: у QCT слот сумки — u16 +132, «прочее» — i32 +136, что после сдвига −4
	//   соответствует `.NSR` +136 / +140, то есть паре `BagSlot136` / `Misc140`.
	C.BagSlot136  = R.BagSlot;     C.Misc140    = R.Misc;
	return C;
}

// ★★★ МАСКА «КАКАЯ КАТЕГОРИЯ ЗАДАНА» У ЗАПИСИ QCT — снята с `netBuildCharStateRequest@0xC65140`
//   ДОСЛОВНО. Она НЕ совпадает со счётчиком записи сценария (`charEquip_CountStatCategories@0xD84C90`)
//   ровно в двух тестах, и оба важны на реальных данных:
//     · категория 3 «предметы-цели»: у QCT `+80/+88/… > 0`, у сценария `> -1`;
//     · категория 4 «надетый предмет»: у QCT `+120 > 0`, у сценария `+124 > -1`.
//   ⚠ ЭТО НЕ МЕЛОЧЬ. В `QCT.BIN` поле `+120` равно НУЛЮ у 158 записей из 471 — при счёте «> -1»
//     категория 4 включалась бы, `invCountItemsById(0)` возвращал бы число ВСЕХ надетых вещей (11),
//     и итог «задано == сошлось» не сходился бы никогда: живая проверка печатала
//     «надето(0): найдено 11 · итог: задано=3 сошлось=11 → НЕТ».
//   Поэтому маска подаётся ЯВНО, через уже существующий вход `ExplicitSet` (им же пользуется
//   `EvalSnqIctCond`). Второго вычислителя условий по-прежнему нет.
static void QctExplicitSet(const FAsdaQuestCompleteRow& R, bool OutSet[8])
{
	bool bAnyReqItem = false;
	for (int32 k = 0; k < R.ReqItemIds.Num(); ++k) { if (R.ReqItemIds[k] > 0) { bAnyReqItem = true; break; } }
	OutSet[0] = (R.LevelMin != 0 || R.LevelMax != 0);                              // +4  / +6
	OutSet[1] = (R.ClassId >= 0 || R.ClassMode >= 0);                              // +8  / +12
	OutSet[2] = (R.QuestDone >= 0 || R.QuestFreeA >= 0 || R.QuestActive >= 0);     // +20 / +16 / +24
	OutSet[3] = bAnyReqItem;                                                       // +80 … +112 (> 0)
	OutSet[4] = (R.HaveItemId > 0);                                                // +120 (> 0)
	OutSet[5] = (R.BagTabB > 0 || R.BagTabA > 0);                                  // +128 / +124
	OutSet[6] = (R.BagSlot != 0);                                                  // +132 (u16)
	OutSet[7] = (R.Misc > 0);                                                      // +136
}

// ★ Удержание таблиц квестов на сессию (15.09.2026, dt_gc_hold) — обоснование в объявлении. Пути — константы этого файла.
int32 UAsdaQuestMarkerSubsystem::HoldGameTables() const
{
	static const FName Group(TEXT("QuestMarker"));
	const TCHAR* const Paths[] = { kEpisodeDT, kQuestCompleteDT, kNsrHeaderDT, kSnqDT, kSnqIctDT };
	// ★ 31.10.2026 (баг 6): места мобов целей (`DT_QuestTargetArea`) читают значки карты мира и кнопки маршрута журнала
	//   (`AsdaQuestGuideTarget`) — тоже сильно на сессию: без удержания таблицу собирал GC между кадрами, и держатель сырого
	//   указателя на неё (самотест значков карты мира) падал с нарушением доступа.
	static const FName GroupArea(TEXT("QuestTargetArea"));
	const TCHAR* const AreaPaths[] = { TEXT("/Game/AsdaAssets/Data/Quest/DT_QuestTargetArea.DT_QuestTargetArea"),
		TEXT("/Game/AsdaAssets/Data/NPC/DT_NpcQuestNpc.DT_NpcQuestNpc"),     // места NPC целей (`AsdaQuestGuideTarget`)
		TEXT("/Game/AsdaAssets/Data/Quest/DT_QuestTurnIn.DT_QuestTurnIn") };  // кто принимает шаг — серверная таблица сдачи
	return HeldTables.Hold(Group, Paths) + HeldTables.Hold(GroupArea, AreaPaths) + HoldTrackerTables();
}

bool UAsdaQuestMarkerSubsystem::IsQuestObjectiveDone(int32 QuestNo) const
{
	HoldGameTables();   // ★ 15.09 (dt_gc_hold): таблицы — сильно у подсистемы на сессию
	const UDataTable* Dt = AsdaQuestMarkTableCache::Table(AsdaQuestMarkTableCache::ETable::QuestComplete, kQuestCompleteDT);   // ★ R4-c (15.11.2026): локальный кэш меток
	if (!Dt) { return false; }
	for (const TPair<FName, uint8*>& It : Dt->GetRowMap())
	{
		const FAsdaQuestCompleteRow* R = reinterpret_cast<const FAsdaQuestCompleteRow*>(It.Value);
		if (!R || R->QuestNo != QuestNo) { continue; }
		bool Set[8]; QctExplicitSet(*R, Set);
		if (EvalNsrCond(CondFromQctRow(*R), EAsdaCondLayout::Script, EAsdaCondOwner::MenuButton, nullptr, Set))
		{
			return true;
		}
	}
	return false;
}

// ★ Сырая пара полей строки QCT для окна `OTHERCHARA_DATA` 4200 — обоснование в объявлении.
//   1:1 цикл эталона `AsdaStory_game.c:401387-401392`: проход по ВСЕЙ таблице, БЕЗ выхода на первом
//   совпадении (при нескольких строках с одним `QuestNo` побеждает ПОСЛЕДНЯЯ — так у оригинала).
bool UAsdaQuestMarkerSubsystem::GetQuestCompleteRawPair(int32 QuestNo, int32& OutStageAt32,
	int32& OutFieldAt132) const
{
	HoldGameTables();   // ★ 15.09 (dt_gc_hold): таблицы — сильно у подсистемы на сессию
	const UDataTable* Dt = AsdaQuestMarkTableCache::Table(AsdaQuestMarkTableCache::ETable::QuestComplete, kQuestCompleteDT);   // ★ R4-c (15.11.2026): локальный кэш меток
	if (!Dt) { return false; }
	bool bFound = false;
	for (const TPair<FName, uint8*>& It : Dt->GetRowMap())
	{
		const FAsdaQuestCompleteRow* R = reinterpret_cast<const FAsdaQuestCompleteRow*>(It.Value);
		if (!R || R->QuestNo != QuestNo) { continue; }
		OutStageAt32  = R->QuestStage;   // rec+32
		OutFieldAt132 = R->BagSlot;      // rec+132 (u16) — у окна 4200 это переключатель цвета строки
		bFound = true;
	}
	return bFound;
}

FString UAsdaQuestMarkerSubsystem::DescribeQuestComplete(int32 QuestNo) const
{
	HoldGameTables();   // ★ 15.09 (dt_gc_hold): таблицы — сильно у подсистемы на сессию
	const UDataTable* Dt = AsdaQuestMarkTableCache::Table(AsdaQuestMarkTableCache::ETable::QuestComplete, kQuestCompleteDT);   // ★ R4-c (15.11.2026): локальный кэш меток
	if (!Dt) { return TEXT("DT_QuestComplete не загружена (импорт: Tools/asda_import_quest_tables.py)\n"); }
	FString Out = FString::Printf(TEXT("QCT для квеста %d:\n"), QuestNo);
	int32 Found = 0;
	for (const TPair<FName, uint8*>& It : Dt->GetRowMap())
	{
		const FAsdaQuestCompleteRow* R = reinterpret_cast<const FAsdaQuestCompleteRow*>(It.Value);
		if (!R || R->QuestNo != QuestNo) { continue; }
		++Found;
		FString Why;
		bool Set[8]; QctExplicitSet(*R, Set);
		const bool bOk = EvalNsrCond(CondFromQctRow(*R), EAsdaCondLayout::Script, EAsdaCondOwner::MenuButton, &Why, Set);
		Out += FString::Printf(TEXT(" строка %d → %s\n%s"), R->Index, bOk ? TEXT("ДА") : TEXT("НЕТ"), *Why);
	}
	if (!Found) { Out += TEXT(" строк с таким номером в таблице нет\n"); }
	return Out;
}

// ── Эталонные проверки ──────────────────────────────────────────────────────────────────────────
// questIsCompleteFlagSet@0xC61E20: (1<<(id%64)) & qword[id/64] карты по адресу CQuest+1140.
bool UAsdaQuestMarkerSubsystem::IsQuestCompleted(int32 QuestNo) const
{
	if (QuestNo < 0 || QuestNo >= DoneBits.Num() * 8) { return false; }
	return (DoneBits[QuestNo >> 3] & (1u << (QuestNo & 7))) != 0;
}

// questFindActiveSlotByType@0xC61EE0: слот+8 == номер && слот+0 >= 2001.
bool UAsdaQuestMarkerSubsystem::IsQuestActive(int32 QuestNo) const
{
	for (const FAsdaQuestSlot& S : Slots)
	{
		if (S.QuestNo == QuestNo && S.QuestId >= 2001) { return true; }
	}
	return false;
}

// questHasActiveSlotByTypeAndParam@0xC61DA0: + слот+10 == стадия.
bool UAsdaQuestMarkerSubsystem::IsQuestActiveWithStage(int32 QuestNo, int32 Stage) const
{
	for (const FAsdaQuestSlot& S : Slots)
	{
		if (S.QuestNo == QuestNo && S.QuestId >= 2001 && S.Stage == Stage) { return true; }
	}
	return false;
}

// CQuest_MatchObjective@0xC626B0 (bTyped=false — слот ищется только по номеру квеста) и
// CQuest_MatchObjective_typed@0xC621E0 (bTyped=true — дополнительно сверяется стадия слота).
bool UAsdaQuestMarkerSubsystem::MatchObjectives(const FAsdaNsrCond& R, bool bTyped) const
{
	// Требуемые цели: id (ObjIds) + количества (ObjCounts), ровно 5 пар.
	auto ObjId  = [&R](int32 k) { return R.ObjIds.IsValidIndex(k)    ? R.ObjIds[k]    : -1; };
	auto ObjCnt = [&R](int32 k) { return R.ObjCounts.IsValidIndex(k) ? R.ObjCounts[k] : 0;  };

	// Сколько целей реально задано — это и есть a3 (счётчик у эталона).
	int32 Want = 0;
	for (int32 k = 0; k < 5; ++k) { if (ObjId(k) > -1) { ++Want; } }

	for (const FAsdaQuestSlot& S : Slots)
	{
		if (S.QuestNo != R.QuestActive || S.QuestId < 2001) { continue; }
		if (bTyped && S.Stage != R.QuestStage) { continue; }

		// Ветка ObjFlagNone (u16 rec+78): «ни одна цель НЕ должна совпасть».
		if (R.ObjFlagNone != 0)
		{
			int32 Hit = 0;
			for (int32 j = 0; j < 5; ++j)
			{
				for (int32 k = 0; k < 5; ++k)
				{
					if (S.ObjIds.IsValidIndex(j) && S.ObjIds[j] == ObjId(k) && S.ObjIds[j] > -1) { ++Hit; }
				}
			}
			return Hit == 0;
		}
		// Ветка ObjFlagAny (u16 rec+80): достаточно ОДНОГО совпадения id+количество.
		int32 Match = 0;
		for (int32 j = 0; j < 5; ++j)
		{
			for (int32 k = 0; k < 5; ++k)
			{
				if (S.ObjIds.IsValidIndex(j) && S.ObjCounts.IsValidIndex(j)
					&& S.ObjIds[j] == ObjId(k) && S.ObjCounts[j] == ObjCnt(k) && S.ObjIds[j] > -1)
				{
					++Match;
				}
			}
		}
		return (R.ObjFlagAny != 0) ? (Match > 0) : (Match == Want);
	}
	return false;
}

// questCheckAllPrereqDone@0xD86770 — категория 2 (предыдущие квесты цепочки).
bool UAsdaQuestMarkerSubsystem::CheckQuestPrereq(const FAsdaNsrCond& R) const
{
	int32 Required = 0, Matched = 0;

	// rec+24 — «квест ЗАВЕРШЁН». В эталоне здесь ПРИСВОЕНИЕ (не ++), сохраняем 1:1.
	if (R.QuestDone >= 0)
	{
		if (!IsQuestActive(R.QuestDone)) { Matched = IsQuestCompleted(R.QuestDone) ? 1 : 0; }
		Required = 1;
	}
	// rec+32 и rec+20 — «квест НЕ взят И НЕ завершён» (порядок как в эталоне: сначала +32, потом +20).
	if (R.QuestFreeB >= 0)
	{
		if (!IsQuestActive(R.QuestFreeB) && !IsQuestCompleted(R.QuestFreeB)) { ++Matched; }
		++Required;
	}
	if (R.QuestFreeA >= 0)
	{
		if (!IsQuestActive(R.QuestFreeA) && !IsQuestCompleted(R.QuestFreeA)) { ++Matched; }
		++Required;
	}
	// rec+28 — «квест В РАБОТЕ» (+ стадия rec+36 и/или цели rec+40…).
	if (R.QuestActive >= 0)
	{
		if (IsQuestActive(R.QuestActive))
		{
			const int32 Stage = R.QuestStage;
			const int32 Obj0  = R.ObjIds.IsValidIndex(0) ? R.ObjIds[0] : -1;
			if (Stage <= -1 || Obj0 <= -1)
			{
				if (Obj0 <= -1)
				{
					if (Stage <= -1)
					{
						if (Stage == -1) { ++Matched; }          // ни стадии, ни целей — достаточно «в работе»
					}
					else if (IsQuestActiveWithStage(R.QuestActive, Stage))
					{
						++Matched;                               // ★ ОСНОВНОЙ путь метки 2 (448 записей)
					}
				}
				else if (MatchObjectives(R, /*bTyped=*/false))
				{
					++Matched;
				}
			}
			else if (MatchObjectives(R, /*bTyped=*/true))
			{
				++Matched;
			}
		}
		++Required;
	}
	return Required == Matched && Required > 0 && Matched > 0;
}

// playerClassRangeMatch@0xD85530 — категория 0 (a1 = LevelMax, a2 = LevelMin).
bool UAsdaQuestMarkerSubsystem::CheckLevel(const FAsdaNsrCond& R) const
{
	int32 Level = 0, Cls = 0, Sub = 0;
	ReadSelfLevelClass(Level, Cls, Sub);
	// ★ ОСОЗНАННОЕ ОТСТУПЛЕНИЕ (см. `Asda.Quest.LevelCapEnforce` в шапке файла): потолок уровня
	//   поднимается до 150 — ровно того значения, которым «нет потолка» обозначено и в самих
	//   данных, и в серверной проверке (`sub_41FD70` бракует всё, что больше `0x96`). Ветки
	//   эталонной функции при этом НЕ меняются: меняется только входное число, как и на сервере,
	//   где правится байт `+38` записи условия. Вырожденный случай «максимум не задан» (`A1 == 0`)
	//   не трогаем — у эталона там своя ветка-инверсия, и данных с таким сочетанием нет.
	int32 A1 = R.LevelMax;
	const int32 A2 = R.LevelMin;
	if (!GAsdaQuestLevelCapEnforce && A1 > 0 && A1 < 150) { A1 = 150; }
	if (A1 == A2 && Level == A2) { return true; }
	if (A2 > 0 && A1 > 0 && Level >= A2 && Level <= A1) { return true; }
	// Эталон: иначе 0, КРОМЕ случая «минимум задан, максимум не задан, уровень НИЖЕ минимума» → 1
	//   (единственная ветка, где функция возвращает 1 без совпадения диапазона).
	if (A2 <= 0 || A1 > 0 || Level >= A2) { return false; }
	return true;
}

// classGradeIsInRange@0xD85AD0 — таблица «класс попадает в грейд» (порт 1:1, включая ветки a3=1/2).
static int32 AsdaClassGradeIsInRange(int32 Cls, int32 Want, int32 Mode, int32 Tier, int32 bAlt)
{
	int32 V = 0;
	if (Mode == 1)
	{
		switch (Tier)
		{
		case 0:
			switch (Want)
			{
			case 1: if (Cls >= 1) { V = (Cls <= 7); } break;
			case 2: if (Cls >= 2) { V = (Cls <= 7); } break;
			case 3: if (Cls >= 3) { V = (Cls <= 7); } break;
			case 4: if (Cls == 4 || Cls == 5) { V = 1; } break;
			case 5: V = (Cls == 5); break;
			case 6: if (Cls == 6 || Cls == 7) { V = 1; } break;
			case 7: V = (Cls == 7); break;
			default: return V;
			}
			break;
		case 1:
			switch (Want)
			{
			case 12: if (Cls >= 12) { V = (Cls <= 18); } break;
			case 13: if (Cls >= 13) { V = (Cls <= 18); } break;
			case 14: if (Cls >= 14) { V = (Cls <= 18); } break;
			case 15: if (Cls == 15 || Cls == 16) { V = 1; } break;
			case 16: V = (Cls == 16); break;
			case 17: if (Cls == 17 || Cls == 18) { V = 1; } break;
			case 18: V = (Cls == 18); break;
			default: return V;
			}
			break;
		case 2:
			switch (Want)
			{
			case 23: if (Cls >= 23) { V = (Cls <= 29); } break;
			case 24: if (Cls >= 24) { V = (Cls <= 29); } break;
			case 25: if (Cls >= 25) { V = (Cls <= 29); } break;
			case 26: if (Cls == 26 || Cls == 27) { V = 1; } break;
			case 27: V = (Cls == 27); break;
			case 28: if (Cls == 28 || Cls == 29) { V = 1; } break;
			case 29: V = (Cls == 29); break;
			default: return V;
			}
			break;
		case 3:
			switch (Want)
			{
			case 34: if (Cls == 34 || Cls == 35) { V = 1; } break;
			case 35: V = (Cls == 35); break;
			case 36: if (Cls == 36 || Cls == 37) { V = 1; } break;
			case 37: V = (Cls == 37); break;
			case 38: if (Cls == 38 || Cls == 39) { V = 1; } break;
			case 39: V = (Cls == 39); break;
			default: return V;
			}
			break;
		case 4:
			switch (Want)
			{
			case 40: if ((Cls >= 1 && Cls <= 7) || (Cls >= 12 && Cls <= 18) || (Cls >= 23 && Cls <= 29) || (Cls >= 34 && Cls <= 39)) { V = 1; } break;
			case 41: if ((Cls >= 2 && Cls <= 7) || (Cls >= 13 && Cls <= 18) || (Cls >= 24 && Cls <= 29) || (Cls >= 34 && Cls <= 39)) { V = 1; } break;
			case 42: if ((Cls >= 3 && Cls <= 7) || (Cls >= 14 && Cls <= 18) || (Cls >= 25 && Cls <= 29) || (Cls >= 34 && Cls <= 39)) { V = 1; } break;
			case 43:
				if ((Cls >= 4 && Cls <= 7) || (Cls >= 15 && Cls <= 18) || (Cls >= 26 && Cls <= 29) || (Cls >= 34 && Cls <= 39)) { V = 1; }
				if (bAlt && (Cls == 1 || Cls == 12 || Cls == 23)) { ++V; }
				break;
			case 44:
				if (Cls == 5 || Cls == 7 || Cls == 16 || Cls == 18 || Cls == 27 || Cls == 29 || (Cls >= 34 && Cls <= 39)) { V = 1; }
				if (bAlt && (Cls == 2 || Cls == 13 || Cls == 24)) { ++V; }
				break;
			default: return V;
			}
			break;
		default: return V;
		}
	}
	else if (Mode == 2)
	{
		switch (Tier)
		{
		case 0:
			switch (Want)
			{
			case 1: V = (Cls < 1); break;
			case 2: V = (Cls < 2); break;
			case 3: V = (Cls < 3); break;
			case 4: V = (Cls < 4); break;
			case 5: V = (Cls < 5); break;
			case 6: V = (Cls < 4); break;   // эталон: именно < 4 (не < 6)
			case 7: if (Cls == 6 || Cls < 4) { V = 1; } break;
			default: return V;
			}
			break;
		case 1:
			switch (Want)
			{
			case 12: V = (Cls == 0); break;
			case 13: if (Cls == 12 || Cls == 0) { V = 1; } break;
			case 14: if ((Cls < 14 && Cls >= 12) || Cls == 0) { V = 1; } break;
			case 15: if ((Cls < 15 && Cls >= 12) || Cls == 0) { V = 1; } break;
			case 16: if ((Cls < 16 && Cls >= 12) || Cls == 0) { V = 1; } break;
			case 17: if ((Cls >= 12 && Cls <= 14) || Cls == 0) { V = 1; } break;
			case 18: if (Cls == 17 || (Cls >= 12 && Cls <= 14) || Cls == 0) { V = 1; } break;
			default: return V;
			}
			break;
		case 2:
			switch (Want)
			{
			case 23: V = (Cls == 0); break;
			case 24: if (Cls == 23 || Cls == 0) { V = 1; } break;
			case 25: if ((Cls < 25 && Cls >= 23) || Cls == 0) { V = 1; } break;
			case 26: if ((Cls < 26 && Cls >= 23) || Cls == 0) { V = 1; } break;
			case 27: if ((Cls < 27 && Cls >= 23) || Cls == 0) { V = 1; } break;
			case 28: if ((Cls < 26 && Cls >= 23) || Cls == 0) { V = 1; } break;   // эталон: < 26
			case 29: if (Cls == 28 || (Cls < 26 && Cls >= 23) || Cls == 0) { V = 1; } break;
			default: return V;
			}
			break;
		case 3:
			switch (Want)
			{
			case 34: if ((Cls >= 1 && Cls < 4) || (Cls >= 12 && Cls < 15) || (Cls >= 23 && Cls < 26)) { V = 1; } break;
			case 35: if ((Cls >= 1 && Cls < 4) || (Cls >= 12 && Cls < 15) || (Cls >= 23 && Cls < 26) || Cls == 34) { V = 1; } break;
			case 36: if ((Cls >= 1 && Cls < 4) || (Cls >= 12 && Cls < 15) || (Cls >= 23 && Cls < 26)) { V = 1; } break;
			case 37: if ((Cls >= 1 && Cls < 4) || (Cls >= 12 && Cls < 15) || (Cls >= 23 && Cls < 26) || Cls == 36) { V = 1; } break;
			case 38: if ((Cls >= 1 && Cls < 4) || (Cls >= 12 && Cls < 15) || (Cls >= 23 && Cls < 26)) { V = 1; } break;
			case 39: if ((Cls >= 1 && Cls < 4) || (Cls >= 12 && Cls < 15) || (Cls >= 23 && Cls < 26) || Cls == 38) { V = 1; } break;
			default: return V;
			}
			break;
		case 4:
			switch (Want)
			{
			case 40: V = (Cls == 0); break;
			case 41: if (Cls == 1 || Cls == 12 || Cls == 23) { V = 1; } break;
			case 42: if ((Cls >= 1 && Cls <= 2) || (Cls >= 12 && Cls <= 13) || (Cls >= 23 && Cls <= 24)) { V = 1; } break;
			case 43:
				if ((Cls >= 1 && Cls <= 3) || (Cls >= 12 && Cls <= 14) || (Cls >= 23 && Cls <= 25)) { V = 1; }
				if (bAlt && Cls == 0) { ++V; }
				break;
			case 44:
				if ((Cls >= 1 && Cls <= 4) || Cls == 6 || (Cls >= 12 && Cls <= 14) || Cls == 17
					|| (Cls >= 23 && Cls <= 26) || Cls == 28) { V = 1; }
				if (bAlt && (Cls == 1 || Cls == 12 || Cls == 23)) { ++V; }
				break;
			default: return V;
			}
			break;
		default: return V;
		}
	}
	return V;
}

// ★★★ [ИСПРАВЛЕНИЕ ДЕФЕКТА ОРИГИНАЛА] 29.10.2026 (жалоба «квест склада не берётся: диалог обрывается кнопкой
//   „Завершить разговор“»). Класс 99 в условии = «любая профессия, кроме новичка»: так его понимает СЕРВЕР —
//   NpcServer `sub_41FD70@0x41FD70` (RE/ida/npc_work/NpcServer.exe.c, CheckClass): `if (iJob_Type == 99) { if (!PlayerJob)
//   return 5; }` — отказ только при основном классе 0, режим (`iJob_Type_Case`) не смотрится. Клиент оригинала 99 не знает:
//   `playerClassMatchesRequirement@0xD85600` в режиме 1 ищет 99 в таблице грейдов `classGradeIsInRange`, где его нет ⇒ ложь
//   ВСЕГДА. В данных класс 99/режим 1 стоит у четырёх квестов: 206 «Золотая копилка» (Рейчел, склад), 157 «Просьба Бритца»,
//   154 «Новое лицо» и 156 «Подходящий подарок 2» (Трими) — шаг «Принять» (NSR) и строка облачка (SNQICT 493/400/394/398).
//   Итог у оригинала: квест не виден в облачке, а с вводной страницы сценария «Принять» уводит на страницу «Спасибо…»
//   без 5043 — квест не берётся никогда. 0 = как оригинал.
static int32 GAsdaQuestClass99AnyJob = 1;
static ASDA_DEV_CVAR_REF(CVarAsdaQuestClass99AnyJob,
	TEXT("asda.Quest.Class99AnyJob"),
	GAsdaQuestClass99AnyJob,
	TEXT("1 = условие «класс 99» квестов выполняется для любой профессии, кроме новичка (как на сервере); 0 = как оригинал (не выполняется никогда)."),
	ECVF_Default);

// playerClassMatchesRequirement@0xD85600 — категория 1 (a1 = ClassId, a2 = ClassMode).
bool UAsdaQuestMarkerSubsystem::CheckClass(const FAsdaNsrCond& R) const
{
	int32 Level = 0, C0 = 0, C1 = 0;
	ReadSelfLevelClass(Level, C0, C1);
	const int32 A1 = R.ClassId, A2 = R.ClassMode;

	// [ИСПРАВЛЕНИЕ] класс 99 — по правилу сервера (разбор у ключа выше): основной класс не новичок.
	if (A1 == 99 && GAsdaQuestClass99AnyJob != 0) { return C0 != 0; }

	// ★ В данных клиента ВСЕ записи с меткой используют ровно эту ветку (ClassMode == 0).
	if (A2 == 0)
	{
		int32 Acc = 0;
		if (A1 >= 0)
		{
			if (C0 == A1 && C1 == 0) { ++Acc; }
			if (C1 == A1 && C1 != 0) { ++Acc; }
			if (Acc > 0) { return true; }
		}
		return false;
	}
	if (A2 != 1 && A2 != 2)
	{
		return (A2 == 100 && C0 == A1);
	}
	if (A1 == 0) { return true; }

	int32 Acc = 0;
	if (A1 >= 1 && A1 <= 7)
	{
		if (AsdaClassGradeIsInRange(C0, A1, A2, 0, 0) > 0) { ++Acc; }
		if (C1 && AsdaClassGradeIsInRange(C1, A1, A2, 0, 0) > 0) { ++Acc; }
		if (Acc > 0) { return true; }
	}
	if (A1 >= 12 && A1 <= 18)
	{
		if (AsdaClassGradeIsInRange(C0, A1, A2, 1, 0) > 0) { ++Acc; }
		if (C1 && AsdaClassGradeIsInRange(C1, A1, A2, 1, 0) > 0) { ++Acc; }
		if (Acc > 0) { return true; }
	}
	if (A1 >= 23 && A1 <= 29)
	{
		if (AsdaClassGradeIsInRange(C0, A1, A2, 2, 0) > 0) { ++Acc; }
		if (C1 && AsdaClassGradeIsInRange(C1, A1, A2, 2, 0) > 0) { ++Acc; }
		if (Acc > 0) { return true; }
	}
	if (A1 >= 34 && A1 <= 39)
	{
		if (AsdaClassGradeIsInRange(C0, A1, A2, 3, 0) > 0) { ++Acc; }
		// ★ 1:1 С ЭТАЛОНОМ: во второй проверке оригинал снова передаёт ОСНОВНОЙ класс (C0), а не
		//   субкласс — вероятная опечатка автора игры, но воспроизводим как есть.
		if (C1 && AsdaClassGradeIsInRange(C0, A1, A2, 3, 0) > 0) { ++Acc; }
		if (Acc > 0) { return true; }
	}
	if (A1 != 40 && A1 != 41 && A1 != 42)
	{
		if (A1 == 43 || A1 == 44)
		{
			const int32 M = (A2 == 1) ? 1 : 2;
			if (AsdaClassGradeIsInRange(C0, A1, M, 4, 0) > 0) { ++Acc; }
			if (AsdaClassGradeIsInRange(C1, A1, M, 4, 1) > 0) { ++Acc; }
			if (Acc > 0) { return true; }
		}
		return Acc > 0;
	}
	if (AsdaClassGradeIsInRange(C0, A1, A2, 4, 0) > 0) { ++Acc; }
	return Acc > 0;
}

// ═══════════════════════ ЕДИНЫЙ ВЫЧИСЛИТЕЛЬ УСЛОВИЙ (см. объявление в .h) ═══════════════════════
// Порт `CInventory_CountItemById@0x9998E0`.
int32 UAsdaQuestMarkerSubsystem::CountItemById(int32 ItemCode) const
{
	// ★ Волна 9 заточки (25.09.2026): тело переехало в ОБЩУЮ точку `UAsdaInventorySubsystem::CountItemById`
	//   (её же зовёт окно 78200). Поведение прежнее: те же контейнеры 1/2 размером 30/60, тот же счёт стопок,
	//   код без записи — по расходникам поштучно; `itemType <= 4` теперь беззнаково, как в эталоне
	//   (отрицательных itemType в item_list нет — проверено по всем 58 878 записям).
	UGameInstance* GI = GetGameInstance();
	const UAsdaInventorySubsystem* Inv = GI ? GI->GetSubsystem<UAsdaInventorySubsystem>() : nullptr;
	return Inv ? Inv->CountItemById(ItemCode) : 0;
}

// Порт `invCountItemsById@0xD86C60`: скан ОДИННАДЦАТИ слотов НАДЕТОГО (контейнер 3).
int32 UAsdaQuestMarkerSubsystem::CountEquippedById(int32 ItemCode) const
{
	UGameInstance* GI = GetGameInstance();
	UAsdaInventorySubsystem* Inv = GI ? GI->GetSubsystem<UAsdaInventorySubsystem>() : nullptr;
	if (!Inv) { return 0; }
	int32 N = 0;
	for (int32 i = 0; i < 11; ++i)
	{
		if (Inv->GetCellItemCode(3, i) == ItemCode) { ++N; }
	}
	return N;
}

// Порт `invIsActiveBagTab@0xD86CF0`: открыта ли вкладка сумки A или B.
//   ⚠ Эталонное поле `gap_43B[31]` — НОМЕР ОТКРЫТОЙ ВКЛАДКИ; в порту это `GetTab()`
//   (0 = «Расходники», 1 = «Снаряжение»).
bool UAsdaQuestMarkerSubsystem::IsActiveBagTab(int32 A, int32 B) const
{
	UGameInstance* GI = GetGameInstance();
	UAsdaInventorySubsystem* Inv = GI ? GI->GetSubsystem<UAsdaInventorySubsystem>() : nullptr;
	if (!Inv) { return false; }
	const int32 Tab = Inv->GetTab();
	if (A > 0 && Tab == A) { return true; }
	return B > 0 && Tab == B;
}

// Порт `inventoryBagHasSlot@0xD86D80`: 0 → «есть свободная ячейка в сумке»
//   (эталон: `i16 gap_43B[33] > -1`); 1 → «есть соулмейт» (`i32 gap_878[47] > 0`).
bool UAsdaQuestMarkerSubsystem::BagHasSlot(int32 Kind) const
{
	UGameInstance* GI = GetGameInstance();
	UAsdaInventorySubsystem* Inv = GI ? GI->GetSubsystem<UAsdaInventorySubsystem>() : nullptr;
	if (Kind == 0)
	{
		return Inv && Inv->FindFreeSlot(2) > -1;
	}
	if (Kind == 1)
	{
		// Соулмейта в порту нет (окно `SOULMATE_WIN` не портировано) — честно «нет», а не выдумка.
		UE_LOG(LogAsdaQuest, Verbose,
			TEXT("[условие] категория 6 вида 1 (соулмейт) — подсистемы соулмейта в порту нет, условие не выполнено"));
		return false;
	}
	return false;
}

// Порт `questCheckRequiredItems5` → `invHasRequiredItems@0xD86E40` (категория 3).
//   ★ ИСПРАВЛЕНИЕ ДЕФЕКТА ОРИГИНАЛА D12: у эталона ВНУТРЕННИЕ циклы индексируют сумку НЕ своим
//   счётчиком `j`, а внешним `i` (`g_InventoryBag + 57 * i` при `for (j …)`), то есть 30 или 60
//   раз проверяется ОДНА И ТА ЖЕ ячейка — номер требования. Здесь сканируется вся сумка.
bool UAsdaQuestMarkerSubsystem::CheckRequiredItems(const FAsdaNsrCond& C) const
{
	int32 Want = 0;
	for (int32 k = 0; k < 5; ++k)
	{
		if (C.ReqItemIds.IsValidIndex(k) && C.ReqItemIds[k] > -1) { ++Want; }
	}
	int32 Have = 0;
	for (int32 k = 0; k < 5; ++k)
	{
		const int32 Id  = C.ReqItemIds.IsValidIndex(k)    ? C.ReqItemIds[k]    : -1;
		const int32 Cnt = C.ReqItemCounts.IsValidIndex(k) ? C.ReqItemCounts[k] : 0;
		if (Id <= -1) { continue; }
		if (CountItemById(Id) >= FMath::Max(1, Cnt)) { ++Have; }
	}
	return Want == Have && Want > 0 && Have > 0;
}

FAsdaNsrCond UAsdaQuestMarkerSubsystem::CondFromScriptRow(const FAsdaNpcQuestScriptRow& R)
{
	FAsdaNsrCond C;
	CopyCondCommon(R, C);                              // дедуп Д7 — общие тринадцать полей
	C.Opcode = R.Opcode;
	C.BagSlot136 = R.BagSlot; C.Misc140 = R.MiscCond;   // раскладка ЗАПИСИ СЦЕНАРИЯ
	return C;
}

bool UAsdaQuestMarkerSubsystem::EvalNsrCond(const FAsdaNsrCond& C, EAsdaCondLayout Layout,
                                            EAsdaCondOwner Owner, FString* OutWhy,
                                            const bool* ExplicitSet) const
{
	// Слот сумки и «прочее» лежат в двух раскладках на разных смещениях — выбираем нужную пару.
	const int32 BagSlot = (Layout == EAsdaCondLayout::Script) ? C.BagSlot136 : C.BagSlot142;
	const int32 Misc    = (Layout == EAsdaCondLayout::Script) ? C.Misc140    : C.Misc144;

	// ── «Сколько категорий задано» ──
	// ★ ЕДИНСТВЕННОЕ расхождение эталонных близнецов: категорию 1 (класс)
	//   `charEquip_CountStatCategories@0xD84C90` (КНОПКА и запись сценария) включает по `> -1`,
	//   а `questEvalConditionFlags@0xD88990` (ШАГ страницы) — по `> 0`. Воспроизводим 1:1.
	const bool bClassSet = (Owner == EAsdaCondOwner::Step)
		? (C.ClassId > 0 || C.ClassMode > 0)
		: (C.ClassId > -1 || C.ClassMode > -1);

	bool AnyReqItem = false;
	for (int32 k = 0; k < C.ReqItemIds.Num(); ++k) { if (C.ReqItemIds[k] > -1) { AnyReqItem = true; break; } }

	bool bSet[8] = {
		(C.LevelMin != 0 || C.LevelMax != 0),                                   // 0 уровень
		bClassSet,                                                              // 1 класс
		(C.QuestDone > -1 || C.QuestFreeA > -1 || C.QuestActive > -1),          // 2 квесты
		AnyReqItem,                                                             // 3 предметы-цели
		(C.HaveItemId > -1),                                                    // 4 надетый предмет
		(C.BagTabA > 0 || C.BagTabB > 0),                                       // 5 вкладка сумки
		(BagSlot != 0),                                                         // 6 слот сумки
		(Misc > 0),                                                             // 7 прочее
	};

	if (ExplicitSet) { for (int32 k = 0; k < 8; ++k) { bSet[k] = ExplicitSet[k]; } }

	int32 Required = 0, Matched = 0;
	static const TCHAR* kNames[8] = {
		TEXT("уровень"), TEXT("класс"), TEXT("квесты"), TEXT("предметы"),
		TEXT("надето"), TEXT("вкладка сумки"), TEXT("слот сумки"), TEXT("прочее")
	};
	for (int32 Cat = 0; Cat < 8; ++Cat)
	{
		if (!bSet[Cat]) { continue; }
		++Required;
		bool bOk = false;
		switch (Cat)
		{
		case 0: bOk = CheckLevel(C); break;
		case 1: bOk = CheckClass(C); break;
		case 2: bOk = CheckQuestPrereq(C); break;
		case 3: bOk = CheckRequiredItems(C); break;
		// Эталон здесь ПРИБАВЛЯЕТ счётчик найденного, а не 1: у `invCountItemsById` результат может
		//   быть > 1, и тогда `задано == сошлось` уже не выполнится. Воспроизводим ровно так.
		case 4: { const int32 N = CountEquippedById(C.HaveItemId); Matched += N; bOk = false;
		          if (OutWhy) { *OutWhy += FString::Printf(TEXT("  надето(%d): найдено %d\n"), C.HaveItemId, N); }
		          continue; }
		case 5: bOk = IsActiveBagTab(C.BagTabA, C.BagTabB); break;
		case 6: bOk = BagHasSlot(BagSlot); break;
		case 7:
			// ★ РАЗОБРАНО 2026-08-20. `sub_9282DA` — переходник (`E9` @0x9282DA, проверено по
			//   бинарю) на `retC_0_6E10@0xD86E10` (item_inventory.c:52544), тело которого целиком:
			//   `return 0;` — аргумент не читается. То есть категория 7 в ОРИГИНАЛЕ — заглушка,
			//   условие ложно ВСЕГДА и ни от каких данных не зависит. Поэтому здесь не «пока не
			//   умеем», а точное 1:1; предупреждения быть не должно.
			//   ⇒ Следствие: любая запись с заданной категорией 7 не выполнится НИКОГДА (у эталона
			//     `задано == сошлось` при `Required > 0` уже не сойдётся). В данных `.NSR` этой
			//     сборки категория не встречается ни разу (зонд `_probe_nsr_cond.py`: у кнопок
			//     категории {0,1,2,4}, у шагов {0,1,2,3,4}), так что следствие ненаблюдаемо.
			UE_LOG(LogAsdaQuest, Verbose,
				TEXT("[условие] категория 7 («прочее», значение %d): в оригинале это заглушка ")
				TEXT("retC_0_6E10@0xD86E10 (`return 0`) — условие ложно всегда"), Misc);
			bOk = false;
			break;
		default: break;
		}
		if (bOk) { ++Matched; }
		if (OutWhy)
		{
			*OutWhy += FString::Printf(TEXT("  %s: задано, %s\n"), kNames[Cat], bOk ? TEXT("сошлось") : TEXT("НЕ сошлось"));
		}
	}
	const bool bResult = (Required == Matched && Required > 0 && Matched > 0);
	if (OutWhy)
	{
		*OutWhy += FString::Printf(TEXT("  итог: задано=%d сошлось=%d → %s\n"),
			Required, Matched, bResult ? TEXT("ДА") : TEXT("НЕТ"));
	}
	return bResult;
}

bool UAsdaQuestMarkerSubsystem::EvalSnqIctCond(const FAsdaSnqIctRow& R, FString* OutWhy) const
{
	ASDA_PERF_SCOPE("Quest.EvalSnqIctCond");   // ★ R4-c (14.11.2026): область замера (AsdaPerf.h; в Shipping пусто)
	FAsdaNsrCond C;
	CopyCondCommon(R, C);                              // дедуп Д7 — общие тринадцать полей
	C.BagSlot142 = R.BagSlot; C.Misc144 = R.Misc;      // раскладка ПОД-ЗАПИСИ
	// ★ У этой записи «категория задана» решают ОТДЕЛЬНЫЕ БАЙТЫ-флаги, а не значения полей.
	const bool Set[8] = {
		R.HasLevel != 0, R.HasClass != 0, R.HasQuest != 0, R.HasReqItems != 0,
		R.HasEquipped != 0, R.HasBagTab != 0, R.BagSlot != 0, R.Misc > 0,
	};
	return EvalNsrCond(C, EAsdaCondLayout::SubRecord, EAsdaCondOwner::MenuButton, OutWhy, Set);
}

// `questSlots_HasFreeSlot@0xC64A30`: 12 слотов по 44 б, занят при `i32 +0 > 0`.
bool UAsdaQuestMarkerSubsystem::HasFreeQuestSlot() const
{
	int32 Busy = 0;
	for (const FAsdaQuestSlot& S : Slots) { if (S.QuestId > 0) { ++Busy; } }
	return Busy < 12;
}

// `questEpisode_FindWordKeyById@0xC648F0`: `Episode_Table +200` → `+236`.
int32 UAsdaQuestMarkerSubsystem::EpisodeIdToQuestKey(int32 EpisodeId) const
{
	HoldGameTables();   // ★ 15.09 (dt_gc_hold): таблицы — сильно у подсистемы на сессию
	if (const FAsdaEpisodeRow* R = AsdaDt::RowByInt<FAsdaEpisodeRow>(kEpisodeDT, EpisodeId, TEXT("QuestMarker")))
	{
		return R->QuestKey;
	}
	return -1;
}

// `CQuest_Episode_FindByWord236@0xC60A30`: `+236` → строка `+0`.
FString UAsdaQuestMarkerSubsystem::EpisodeTitleByQuestKey(int32 QuestKey) const
{
	HoldGameTables();   // ★ 15.09 (dt_gc_hold): таблицы — сильно у подсистемы на сессию
	const UDataTable* Dt = AsdaDt::Cached(kEpisodeDT);
	if (!Dt) { return FString(); }
	for (const TPair<FName, uint8*>& It : Dt->GetRowMap())
	{
		const FAsdaEpisodeRow* R = reinterpret_cast<const FAsdaEpisodeRow*>(It.Value);
		// ★ [ПОРТ] 09.10.2026 (мультиязычность): оверлей `EpisodeTable`, ключ `<EpisodeId>.Title`.
		if (R && R->QuestKey == QuestKey) { return AsdaLangRows::Text(Dt, R, TEXT("Title"), R->Title); }
	}
	return FString();
}

// `CQuest_Episode_FindById@0xC60A30`: код эпизода (`+200`) → название (`+0`). Ключ строки
//   `DT_EpisodeTable` — сам код эпизода, поэтому перебора нет.
FString UAsdaQuestMarkerSubsystem::EpisodeTitleByEpisodeId(int32 EpisodeId) const
{
	HoldGameTables();   // ★ 15.09 (dt_gc_hold): таблицы — сильно у подсистемы на сессию
	if (const FAsdaEpisodeRow* R = AsdaDt::RowByInt<FAsdaEpisodeRow>(kEpisodeDT, EpisodeId, TEXT("QuestMarker")))
	{
		return AsdaLangRows::Text(AsdaDt::Cached(kEpisodeDT), R, TEXT("Title"), R->Title);   // [ПОРТ] 09.10.2026 (мультиязычность)
	}
	return FString();
}

// ★ Порт `CQuest_HasActiveQuestOrReq@0xC62E10` (quest.c:3343) в режиме `a2 == 0` ЦЕЛИКОМ.
//   Тело эталона:
//       1) в `Episode_Table` ищется запись с `+236 == a3` (ПОСЛЕДНЯЯ совпавшая — цикл без break);
//       2) если она есть и её `+284` (категория) == 1 — считаются занятые слоты журнала, у которых
//          эпизод (`CQuest_Episode_FindById(слот+0)`) тоже категории 1 и с ДРУГИМ кодом эпизода
//          (`+200`). Нашёлся хоть один → 1. Смысл: у игрока не может быть двух «категорийных»
//          заданий сразу;
//       3) финальный цикл по 12 слотам: `слот+0 == a3` → 1.
//   ⚠ ДЕФЕКТ ОРИГИНАЛА (не наш): в п.3 сравнивается КОД ЭПИЗОДА слота (`+0`, ≥ 2001) с КЛЮЧОМ
//     КВЕСТА `a3` (сотни) — совпасть они практически не могут, и вся ветка мертва. Воспроизводим
//     как есть: «исправление» здесь сделало бы гейт сдачи СТРОЖЕ оригинала и рубило бы сдачу.
bool UAsdaQuestMarkerSubsystem::HasActiveQuestOrReqByKey(int32 QuestKey) const
{
	ASDA_PERF_SCOPE("Quest.HasActiveQuestOrReq");   // ★ R4-c (14.11.2026): область замера (AsdaPerf.h; в Shipping пусто)
	HoldGameTables();   // ★ 15.09 (dt_gc_hold): таблицы — сильно у подсистемы на сессию
	const UDataTable* Dt = AsdaQuestMarkTableCache::Table(AsdaQuestMarkTableCache::ETable::Episode, kEpisodeDT);   // ★ R4-c (15.11.2026): локальный кэш меток
	// ★ R4-c (14.11.2026): последняя строка эпизода с этим номером задания — через индекс (`AsdaQuestEpisodeIndex.h`).
	const FAsdaEpisodeRow* Want = AsdaQuestEpisodeIndex::LastByQuestKey(Dt, QuestKey);
	if (Want && Want->Category == 1)
	{
		for (const FAsdaQuestSlot& S : Slots)
		{
			if (S.QuestId <= 0) { continue; }
			const FAsdaEpisodeRow* E = AsdaQuestMarkTableCache::RowByInt<FAsdaEpisodeRow>(AsdaQuestMarkTableCache::ETable::Episode, kEpisodeDT, S.QuestId, TEXT("QuestMarker"));
			if (E && E->Category == 1 && E->EpisodeId != Want->EpisodeId) { return true; }
		}
	}
	for (const FAsdaQuestSlot& S : Slots)
	{
		if (S.QuestId == QuestKey) { return true; }
	}
	return false;
}

bool UAsdaQuestMarkerSubsystem::IsQuestTaken(int32 QuestNo) const
{
	return IsQuestActive(QuestNo);
}

// Порт `CQuest_HasActiveQuestOrReq@0xC62E10` (quest.c:3343, `INDEX.tsv` строка 5839) в режиме
//   `a2 == 1`. Тело этой ветки эталона целиком:
//       for (ei = 0; ei < 12; ++ei) if (*(i16*)(this + 0x26C + 44*ei) == a3) return 1;
//       return 0;
//   Слоты журнала: база `CQuest+0x264`, страйд 44, 12 штук; `0x26C` = слот+8 = НОМЕР квеста
//   (`FAsdaQuestSlot::QuestNo`). ⇒ «квест готов к сдаче» в КЛИЕНТЕ = «квест с этим номером просто
//   занимает слот журнала», и ничего больше.
//   ★ ИСПРАВЛЕНО 2026-08-20: прежняя версия была СТРОЖЕ оригинала — требовала ещё и «ни одной
//     незакрытой цели», из-за чего правая колонка облачка 2406 показывала меньше строк, чем
//     оригинал. Полнота целей в оригинале проверяется ПОЗЖЕ и в другом месте —
//     `questNpc_CollectDeliverableItems@0xD89550` циклом по пяти требуемым предметам
//     (`itemVerifyAttributeSum`), а не здесь.
//   ⚠ Эталон НЕ проверяет занятость слота (`+0 >= 2001`) и сравнивает сырой i16 — воспроизводим
//     ровно так же, вместе со следствием: у пустого слота сервер шлёт своё значение в `+8`, и если
//     оно случайно совпадёт с запрошенным номером, оригинал тоже ответит «да».
bool UAsdaQuestMarkerSubsystem::IsQuestReadyToHandIn(int32 QuestNo) const
{
	ASDA_PERF_SCOPE("Quest.IsQuestReadyToHandIn");   // ★ R4-c (14.11.2026): область замера (AsdaPerf.h; в Shipping пусто)
	for (const FAsdaQuestSlot& S : Slots)
	{
		if (S.QuestNo == QuestNo) { return true; }
	}
	return false;
}

int32 UAsdaQuestMarkerSubsystem::FindQuestSlotNo(int32 QuestNo) const
{
	for (int32 i = 0; i < Slots.Num(); ++i)
	{
		if (Slots[i].QuestNo == QuestNo && Slots[i].QuestId >= 2001) { return i; }
	}
	return -1;
}
