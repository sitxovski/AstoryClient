// Copyright (c) 2026 @sitxovski (Discord)
// Реализация `AsdaEntityText` — см. шапку `Public/World/AsdaEntityText.h`.
#include "World/AsdaEntityText.h"
#include "AsdaDataTableCache.h"          // AsdaDt::Cached / RowByInt — одна ленивая загрузка таблиц на порт
#include "World/AsdaWorldSubsystem.h"    // FAsdaEntityNameRow — строка DT_MobNames / DT_MobSounds / DT_NpcNames
#include "Engine/DataTable.h"
#include "Text/AsdaLang.h"        // [ПОРТ] 09.10.2026 (мультиязычность): имена NPC/мобов на языке сессии
#include "Text/AsdaLangRows.h"    // [ПОРТ] 09.10.2026 (мультиязычность): гейт ActiveForBakeable (мир редактора — без перевода)
#include "AsdaDevConsole.h"       // ASDA_DEV_CVAR — дев-ключ отката вне Shipping

// ★★★ [ОРИГИНАЛ] 25.10.2026 (баг игроков «квест засчитывает не тех мобов»; отчёт `Reports/quest_mobs/QUEST_MOB_AUDIT.md`).
//   Цель-моб гида задания (`QDT.BIN +8`) — это ИНДЕКС строки `Mob list.BIN`, а не серверный type: эталон
//   `QuestGuide_BuildObjectiveText@0xC68E00` и `CQuestTracker_BuildWaypointStrings@0xC58A10` зовут
//   `sub_939242(g_pMobMgr, id)` → `sub_97D820`: `база + 272*id + 100` — без вычитания. Порт брал строку `id − 1`
//   (правило серверного type из `MobName`), и журнал 700 / трекер 1800 называли моба СОСЕДНЕЙ строки: квест 407
//   «Воин-новичок» звал на «Мерзкого жуткозонтика» (строка 618), а сервер засчитывал «Малютку-прыгуна» (type 620 =
//   строка 619). Вторым методом сверено с сервером: `Monster.ai` даёт предмет квеста за type = TargetId + 1.
//   1 = как эталон (строка TargetId), 0 = прежнее поведение порта (строка TargetId − 1).
static ASDA_DEV_CVAR(int32, CVarQuestGuideMobRow,
	TEXT("asda.Quest.GuideMobRow"), 1,
	TEXT("Имя цели-моба гида задания (журнал 700, трекер 1800): 1 = строка Mob list = TargetId QDT, как эталон; 0 = прежнее (TargetId-1)"),
	ECVF_Default);

namespace AsdaEntityText
{
	bool QuestGuideMobRowOn()
	{
		return CVarQuestGuideMobRow.GetValueOnAnyThread() != 0;
	}

	// ★ [ПОРТ] 09.10.2026 (мультиязычность): имя строки `RowName` таблицы `T` (DT_MobNames / DT_NpcNames) на языке сессии — оверлей
	//   `MobNames` / `NpcNames`, ключ `<RowName>.Name`. Мир редактора вне PIE — русское (результат пишется в
	//   UPROPERTY запечённых акторов: `AAsdaMobActor::MobName`, плашка `DisplayName`), см. `ActiveForBakeable`.
	static FString LocName(const UDataTable* T, const FName& RowName, const FString& Ru)
	{
		if (!AsdaLangRows::ActiveForBakeable()) { return Ru; }
		return FAsdaLang::ResolveRow(T, RowName, TEXT("Name"), Ru);
	}

	const TCHAR* const kMobNamesPath  = TEXT("/Game/AsdaAssets/Data/Mob/DT_MobNames.DT_MobNames");
	const TCHAR* const kMobSoundsPath = TEXT("/Game/AsdaAssets/Data/Mob/DT_MobSounds.DT_MobSounds");
	// ★ КАНОН имён NPC = DT_NpcNames (ключ = NpcId, 1-based; эталон g_NpcTextTable + 136*(objId−1) + 4).
	//   Параллельный ассет /Game/AsdaAssets/Text/NPC/DT_npc_list — тот же npc_list.BIN, но РАСШИРЕННЫЙ слой
	//   импорта (desc/subtitle/text/family/village/tex/line) под ещё не портированные системы. Сверка 2026-08-01:
	//   204 строки в обеих, ключи 1:1, имена 203/204 (id=81 — баг экстрактора, исправлен клампом char[32] в
	//   Tools/extract_text_tables.py). ⇒ имя NPC берём ТОЛЬКО отсюда (перенесено из `EnsureNameTables` мира).
	const TCHAR* const kNpcNamesPath  = TEXT("/Game/AsdaAssets/Data/NPC/DT_NpcNames.DT_NpcNames");

	// Тело — бывший `UAsdaNameplateComponent::LookupMobName` (построчно; K6: версия плашки выбрана аудитом).
	FString MobName(int32 MobTypeId, const FString& ModelName)
	{
		if (MobTypeId <= 0) { return FString(); }
		const UDataTable* T = AsdaDt::Cached(kMobNamesPath);
		if (!T) { return FString(); }
		const FName Row(*FString::FromInt(MobTypeId - 1));   // строка = MobId−1 (серверный type 1-based)
		if (const FAsdaEntityNameRow* R = T->FindRow<FAsdaEntityNameRow>(Row, TEXT("LookupMobName"), false))
		{
			if (ModelName.IsEmpty() || R->Model.Equals(ModelName, ESearchCase::IgnoreCase)) { return LocName(T, Row, R->Name); }   // [ПОРТ] 09.10.2026 (мультиязычность)
		}
		// Гард: строка type−1 дала другую модель (инстанс-мобы) → ближайшая строка с той же моделью.
		if (!ModelName.IsEmpty())
		{
			int32 BestD = MAX_int32; FString BestName; FName BestRow;   // BestRow — [ПОРТ] 09.10.2026 (мультиязычность): ключ перевода
			for (const TPair<FName, uint8*>& It : T->GetRowMap())
			{
				const FAsdaEntityNameRow* Rr = reinterpret_cast<const FAsdaEntityNameRow*>(It.Value);
				if (Rr && Rr->Model.Equals(ModelName, ESearchCase::IgnoreCase))
				{
					const int32 D = FMath::Abs(FCString::Atoi(*It.Key.ToString()) - (MobTypeId - 1));
					if (D < BestD) { BestD = D; BestName = Rr->Name; BestRow = It.Key; }
				}
			}
			if (!BestName.IsEmpty()) { return LocName(T, BestRow, BestName); }   // [ПОРТ] 09.10.2026 (мультиязычность)
		}
		if (const FAsdaEntityNameRow* R2 = T->FindRow<FAsdaEntityNameRow>(Row, TEXT("LookupMobName2"), false))
			return LocName(T, Row, R2->Name);   // [ПОРТ] 09.10.2026 (мультиязычность)
		return FString();
	}

	// Бывший `UAsdaWorldSubsystem::GetMobModelName` (на общей точке таблиц вместо своей загрузки).
	FString MobModel(int32 MobTypeId)
	{
		if (MobTypeId <= 0) { return FString(); }
		// ключ строки = MobId-1 (серверный type 1-based)
		if (const FAsdaEntityNameRow* R = AsdaDt::RowByInt<FAsdaEntityNameRow>(kMobNamesPath, MobTypeId - 1, TEXT("GetMobModelName")))
			return R->Model;
		return FString();
	}

	// Бывший `UAsdaWorldSubsystem::GetMobSoundBase`.
	FString MobSoundBase(int32 MobTypeId)
	{
		if (MobTypeId <= 0) { return FString(); }
		// строка = MobId-1 (как DT_MobNames); Name = звук-база (Mob_list @+0x44)
		if (const FAsdaEntityNameRow* R = AsdaDt::RowByInt<FAsdaEntityNameRow>(kMobSoundsPath, MobTypeId - 1, TEXT("GetMobSoundBase")))
			return R->Name;
		return FString();
	}

	// Бывшие `UAsdaNameplateComponent::LookupNpcName` ≡ `UAsdaWorldSubsystem::GetNpcName`.
	FString NpcName(int32 NpcId)
	{
		if (NpcId <= 0) { return FString(); }
		// ключ = NpcId (npc_list поле@0, 1-based)
		if (const FAsdaEntityNameRow* R = AsdaDt::RowByInt<FAsdaEntityNameRow>(kNpcNamesPath, NpcId, TEXT("LookupNpcName")))
			return LocName(AsdaDt::Cached(kNpcNamesPath), FName(*FString::FromInt(NpcId)), R->Name);   // [ПОРТ] 09.10.2026 (мультиязычность)
		return FString();
	}

	// Бывшие копии `AAsdaNpcActor::OnConstruction` и сетевого спауна NPC в `UAsdaWorldSubsystem`.
	FString NpcDisplayName(int32 NpcId, const FString& BakedName)
	{
		FString Nm = NpcName(NpcId);
		if (Nm.IsEmpty()) { Nm = BakedName; }
		return Nm;
	}

	// Бывшие копии `UAsdaQuestLogSubsystem::RouteTargetName` и `UAsdaQuestTracker` (цель гида).
	FString QuestTargetName(int32 TargetKind, int32 Id)
	{
		// Моб: `Id` — строка таблицы мобов (эталон, без сдвига) ⇒ `MobName` получает type = строка + 1. NPC — по полю id.
		if (TargetKind == 1) { return MobName(QuestGuideMobRowOn() ? Id + 1 : Id, FString()); }
		return NpcName(Id);
	}
}
