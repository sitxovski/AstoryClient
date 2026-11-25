// Copyright (c) 2026 @sitxovski (Discord)
// ★★★ СТЕНД СВЕРКИ `AsdaEntityText` СО СТАРЫМИ КОПИЯМИ (волна 3 плана дублей, п.14). Только вне Shipping.
//   Отчёт: `Reports/dedup_audit/wave3_entitytext.md`.
//
// ★ ЧТО СВЕРЯЕТ. Ниже — ЗАМОРОЖЕННЫЕ копии удалённых/переведённых тел (дословно из бэкапа
//   `_backup/2026-09-15_wave3_entitytext/`, отличаются только именем функции и тем, что «мировые» копии держат
//   таблицы в локальных сильных указателях вместо членов `UAsdaWorldSubsystem`, как `EnsureNameTables`):
//     Old_WS_GetMobName / Old_WS_GetMobModelName / Old_WS_GetMobSoundBase / Old_WS_GetNpcName — мир (LoadObject);
//     Old_NP_LookupMobName / Old_NP_LookupNpcName — плашка (AsdaDt::Cached);
//     Old_NpcActor_DisplayName / Old_WS_NpcSpawn_DisplayName — «таблица, иначе запечённое»;
//     Old_QuestLog_Target / Old_QuestTracker_Target — «1 = моб, иначе NPC».
//   Против каждой — новая функция `AsdaEntityText::*` на РЕАЛЬНЫХ ассетах таблиц.
//   Входы: MobTypeId −2 … (строк+4) × модели {"", каждая из всех различных моделей таблицы, модель своей строки
//   в ВЕРХНЕМ регистре, несуществующая}; NpcId −2 … (макс. ключ+3); запечённое {"", "ЗАПЕЧЁННОЕ"}; вид {−1,0,1,2}.
// ★ КОНТРОЛЬНЫЕ ПАРЫ (закон `pixel-metric-needs-control-pairs`): стенд обязан ловить разницу —
//   К1 «имя моба без гарда модели», К2 «имя NPC со сдвигом ключа +1», К3 «модель моба со строки MobId, не MobId−1»,
//   К4 «вид цели квеста инвертирован». У каждой расхождений должно быть > 0, иначе стенд слеп → ВЕРДИКТ ПРОВАЛ.
//
// Консоль: `asda.EntityText.Stand [файл]` — пишет выводы новых функций в файл (для md5) и итог в лог
//   строкой `ENTITYTEXT_STAND ИТОГ`.
#include "CoreMinimal.h"

#if !UE_BUILD_SHIPPING

#include "World/AsdaEntityText.h"
#include "Text/AsdaLang.h"        // ★ 06.11.2026: имя на языке сессии в замороженных копиях
#include "Text/AsdaLangRows.h"    // ★ 06.11.2026: тот же гейт ActiveForBakeable, что у AsdaEntityText
#include "World/AsdaWorldSubsystem.h"   // FAsdaEntityNameRow
#include "AsdaDataTableCache.h"
#include "Engine/DataTable.h"
#include "Engine/Engine.h"        // GEngine, FWorldContext — поиск живого GameInstance для проверки GC
#include "Engine/GameInstance.h"  // UGameInstance::GetSubsystem
#include "Engine/World.h"
#include "UObject/WeakObjectPtr.h"
#include "HAL/IConsoleManager.h"
#include "HAL/PlatformTime.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "Misc/SecureHash.h"
#include "Test/AsdaSelfTestKit.h"   // ★ 11.10.2026 (В3 S1): разбор аргументов (`quit`), единый итог, выход

DEFINE_LOG_CATEGORY_STATIC(LogAsdaEntityTextStand, Log, All);

namespace AsdaEntityTextStand
{
	// Таблицы «мировых» копий — как члены MobNameTable/NpcNameTable/MobSoundTable после EnsureNameTables.
	static UDataTable* GOldMobNameTable  = nullptr;
	static UDataTable* GOldNpcNameTable  = nullptr;
	static UDataTable* GOldMobSoundTable = nullptr;

	// ★ 06.11.2026: копии заморожены 15.09, до мультиязычности (09.10), и возвращали только русское имя таблицы, а новые функции —
	//   имя на языке сессии. В EN стенд сравнивал русское с английским (225 168 ложных расхождений). Тот же оверлей, что у `LocName`.
	static FString StandLoc(const UDataTable* T, const FName& RowName, const FString& Ru)
	{
		if (!AsdaLangRows::ActiveForBakeable()) { return Ru; }
		return FAsdaLang::ResolveRow(T, RowName, TEXT("Name"), Ru);
	}

	static void Old_WS_EnsureNameTables()
	{
		GOldMobNameTable = LoadObject<UDataTable>(nullptr, TEXT("/Game/AsdaAssets/Data/Mob/DT_MobNames.DT_MobNames"), nullptr, LOAD_NoWarn | LOAD_Quiet);
		GOldNpcNameTable = LoadObject<UDataTable>(nullptr, TEXT("/Game/AsdaAssets/Data/NPC/DT_NpcNames.DT_NpcNames"), nullptr, LOAD_NoWarn | LOAD_Quiet);
		GOldMobSoundTable = LoadObject<UDataTable>(nullptr, TEXT("/Game/AsdaAssets/Data/Mob/DT_MobSounds.DT_MobSounds"), nullptr, LOAD_NoWarn | LOAD_Quiet);
	}

	// ── копия UAsdaWorldSubsystem::GetMobName (бэкап AsdaWorldSubsystem.cpp:5446-5476) ──
	static FString Old_WS_GetMobName(int32 MobId, const FString& ModelName)
	{
		if (!GOldMobNameTable || MobId <= 0) { return FString(); }
		const FName Row(*FString::FromInt(MobId - 1));
		if (const FAsdaEntityNameRow* R = GOldMobNameTable->FindRow<FAsdaEntityNameRow>(Row, TEXT("GetMobName"), false))
		{
			if (ModelName.IsEmpty() || R->Model.Equals(ModelName, ESearchCase::IgnoreCase))
				return StandLoc(GOldMobNameTable, Row, R->Name);
		}
		if (!ModelName.IsEmpty())
		{
			int32 Best = INDEX_NONE, BestD = MAX_int32; FString BestName;
			for (const TPair<FName, uint8*>& It : GOldMobNameTable->GetRowMap())
			{
				const FAsdaEntityNameRow* Rr = reinterpret_cast<const FAsdaEntityNameRow*>(It.Value);
				if (Rr && Rr->Model.Equals(ModelName, ESearchCase::IgnoreCase))
				{
					const int32 Idx = FCString::Atoi(*It.Key.ToString());
					const int32 D = FMath::Abs(Idx - (MobId - 1));
					if (D < BestD) { BestD = D; Best = Idx; BestName = StandLoc(GOldMobNameTable, It.Key, Rr->Name); }
				}
			}
			if (Best != INDEX_NONE) { return BestName; }
		}
		if (const FAsdaEntityNameRow* R = GOldMobNameTable->FindRow<FAsdaEntityNameRow>(Row, TEXT("GetMobName2"), false))
			return StandLoc(GOldMobNameTable, Row, R->Name);
		return FString();
	}

	// ── копия UAsdaWorldSubsystem::GetMobModelName (бэкап :1555-1563) ──
	static FString Old_WS_GetMobModelName(int32 MobId)
	{
		if (!GOldMobNameTable || MobId <= 0) { return FString(); }
		const FName Row(*FString::FromInt(MobId - 1));
		if (const FAsdaEntityNameRow* R = GOldMobNameTable->FindRow<FAsdaEntityNameRow>(Row, TEXT("GetMobModelName"), false))
			return R->Model;
		return FString();
	}

	// ── копия UAsdaWorldSubsystem::GetMobSoundBase (бэкап :5478-5486) ──
	static FString Old_WS_GetMobSoundBase(int32 MobId)
	{
		if (!GOldMobSoundTable || MobId <= 0) { return FString(); }
		const FName Row(*FString::FromInt(MobId - 1));
		if (const FAsdaEntityNameRow* R = GOldMobSoundTable->FindRow<FAsdaEntityNameRow>(Row, TEXT("GetMobSoundBase"), false))
			return R->Name;
		return FString();
	}

	// ── копия UAsdaWorldSubsystem::GetNpcName (бэкап :5488-5496) ──
	static FString Old_WS_GetNpcName(int32 NpcId)
	{
		if (!GOldNpcNameTable || NpcId <= 0) { return FString(); }
		const FName Row(*FString::FromInt(NpcId));
		if (const FAsdaEntityNameRow* R = GOldNpcNameTable->FindRow<FAsdaEntityNameRow>(Row, TEXT("GetNpcName"), false))
			return StandLoc(GOldNpcNameTable, Row, R->Name);
		return FString();
	}

	// ── копия UAsdaNameplateComponent::LookupNpcName (бэкап AsdaNameplateComponent.cpp:605-612) ──
	static FString Old_NP_LookupNpcName(int32 NpcId)
	{
		if (NpcId <= 0) { return FString(); }
		if (const FAsdaEntityNameRow* R = AsdaDt::RowByInt<FAsdaEntityNameRow>(TEXT("/Game/AsdaAssets/Data/NPC/DT_NpcNames.DT_NpcNames"), NpcId, TEXT("LookupNpcName")))
			return StandLoc(AsdaDt::Cached(TEXT("/Game/AsdaAssets/Data/NPC/DT_NpcNames.DT_NpcNames")), FName(*FString::FromInt(NpcId)), R->Name);
		return FString();
	}

	// ── копия UAsdaNameplateComponent::LookupMobName (бэкап :614-642) ──
	static FString Old_NP_LookupMobName(int32 MobId, const FString& ModelName)
	{
		if (MobId <= 0) { return FString(); }
		const UDataTable* T = AsdaDt::Cached(TEXT("/Game/AsdaAssets/Data/Mob/DT_MobNames.DT_MobNames"));
		if (!T) { return FString(); }
		const FName Row(*FString::FromInt(MobId - 1));
		if (const FAsdaEntityNameRow* R = T->FindRow<FAsdaEntityNameRow>(Row, TEXT("LookupMobName"), false))
		{
			if (ModelName.IsEmpty() || R->Model.Equals(ModelName, ESearchCase::IgnoreCase)) { return StandLoc(T, Row, R->Name); }
		}
		if (!ModelName.IsEmpty())
		{
			int32 BestD = MAX_int32; FString BestName;
			for (const TPair<FName, uint8*>& It : T->GetRowMap())
			{
				const FAsdaEntityNameRow* Rr = reinterpret_cast<const FAsdaEntityNameRow*>(It.Value);
				if (Rr && Rr->Model.Equals(ModelName, ESearchCase::IgnoreCase))
				{
					const int32 D = FMath::Abs(FCString::Atoi(*It.Key.ToString()) - (MobId - 1));
					if (D < BestD) { BestD = D; BestName = StandLoc(T, It.Key, Rr->Name); }
				}
			}
			if (!BestName.IsEmpty()) { return BestName; }
		}
		if (const FAsdaEntityNameRow* R2 = T->FindRow<FAsdaEntityNameRow>(Row, TEXT("LookupMobName2"), false))
			return StandLoc(T, Row, R2->Name);
		return FString();
	}

	// ── копия AAsdaNpcActor::OnConstruction (бэкап AsdaNpcActor.cpp:579-580) ──
	static FString Old_NpcActor_DisplayName(int32 NpcId, const FString& NpcName)
	{
		FString Nm = Old_NP_LookupNpcName(NpcId);
		if (Nm.IsEmpty()) { Nm = NpcName; }
		return Nm;
	}

	// ── копия сетевого спауна NPC (бэкап AsdaWorldSubsystem.cpp:1165-1166) ──
	static FString Old_WS_NpcSpawn_DisplayName(int32 NpcId, const FString& NpcName)
	{
		FString NpcNm = Old_WS_GetNpcName(NpcId);
		if (NpcNm.IsEmpty()) { NpcNm = NpcName; }
		return NpcNm;
	}

	// ── копия UAsdaQuestLogSubsystem::RouteTargetName, ядро (бэкап AsdaQuestLogSubsystem.cpp:2517-2519) ──
	static FString Old_QuestLog_Target(int32 Kind, int32 Id)
	{
		return (Kind == 1) ? Old_NP_LookupMobName(Id, FString()) : Old_NP_LookupNpcName(Id);
	}

	// ── копия UAsdaQuestTracker (бэкап AsdaQuestTracker.cpp:558-560) ──
	static FString Old_QuestTracker_Target(int32 TargetKind, int32 TargetId)
	{
		return (TargetKind == 1) ? Old_NP_LookupMobName(TargetId, FString()) : Old_NP_LookupNpcName(TargetId);
	}

	// Счётчик одной пары «старое ↔ новое».
	struct FPair
	{
		const TCHAR* Name;
		int64 Cmp = 0;
		int64 Diff = 0;
		FString FirstDiff;
		void Check(const FString& Old, const FString& New, const FString& Input)
		{
			++Cmp;
			// Сравнение ПОБАЙТОВО (регистр важен): имя уходит на экран как есть.
			if (!Old.Equals(New, ESearchCase::CaseSensitive))
			{
				if (Diff == 0) { FirstDiff = FString::Printf(TEXT("%s: старое «%s» новое «%s»"), *Input, *Old, *New); }
				++Diff;
			}
		}
	};

	static void Run(const TArray<FString>& Args)
	{
		const double T0 = FPlatformTime::Seconds();
		// ★ 11.10.2026 (В3 S1): разбор каркаса — `quit` (его дописывает пакетный прогон) не становится путём файла вывода;
		//   с `quit` стенд выходит сам и печатает единый итог (прежде пакет стоял до таймаута).
		const FAsdaSelfTestArgs KA = FAsdaSelfTestArgs::Parse(Args);
		const FString OutPath = KA.Str(0, *FPaths::Combine(FPaths::ProjectSavedDir(), TEXT("EntityTextStand.txt")));

		Old_WS_EnsureNameTables();
		const UDataTable* MobT = AsdaDt::Cached(AsdaEntityText::kMobNamesPath);
		const UDataTable* NpcT = AsdaDt::Cached(AsdaEntityText::kNpcNamesPath);
		const UDataTable* SndT = AsdaDt::Cached(AsdaEntityText::kMobSoundsPath);
		if (!MobT || !NpcT || !SndT || !GOldMobNameTable || !GOldNpcNameTable || !GOldMobSoundTable)
		{
			UE_LOG(LogAsdaEntityTextStand, Error, TEXT("ENTITYTEXT_STAND ИТОГ: ВЕРДИКТ ПРОВАЛ — нет таблиц (мобы=%d NPC=%d звуки=%d)"),
				MobT != nullptr, NpcT != nullptr, SndT != nullptr);
			AsdaSelfTestKit::Finish(nullptr, TEXT("asda.EntityText.Stand"), KA.bQuit, LogAsdaEntityTextStand, TEXT("ENTITYTEXT_STAND"), 0, 1, TEXT(" (нет таблиц)"));
			return;
		}

		// Диапазоны ключей и все различные модели — из самих таблиц.
		int32 MobRows = 0, SndMaxKey = -1, NpcMaxKey = 0;
		TArray<FString> Models;
		TSet<FString> ModelsLower;
		for (const TPair<FName, uint8*>& It : MobT->GetRowMap())
		{
			++MobRows;
			const FAsdaEntityNameRow* R = reinterpret_cast<const FAsdaEntityNameRow*>(It.Value);
			if (R && !ModelsLower.Contains(R->Model.ToLower())) { ModelsLower.Add(R->Model.ToLower()); Models.Add(R->Model); }
		}
		for (const TPair<FName, uint8*>& It : SndT->GetRowMap()) { SndMaxKey = FMath::Max(SndMaxKey, FCString::Atoi(*It.Key.ToString())); }
		for (const TPair<FName, uint8*>& It : NpcT->GetRowMap()) { NpcMaxKey = FMath::Max(NpcMaxKey, FCString::Atoi(*It.Key.ToString())); }
		const int32 MobIdMax = FMath::Max(MobRows, SndMaxKey + 1) + 4;
		const int32 NpcIdMax = NpcMaxKey + 3;

		FPair P_WS_MobName{ TEXT("мир GetMobName ↔ MobName") };
		FPair P_NP_MobName{ TEXT("плашка LookupMobName ↔ MobName") };
		FPair P_K6{ TEXT("(справка K6) мир GetMobName ↔ плашка LookupMobName") };
		FPair P_Model{ TEXT("мир GetMobModelName ↔ MobModel") };
		FPair P_Sound{ TEXT("мир GetMobSoundBase ↔ MobSoundBase") };
		FPair P_WS_Npc{ TEXT("мир GetNpcName ↔ NpcName") };
		FPair P_NP_Npc{ TEXT("плашка LookupNpcName ↔ NpcName") };
		FPair P_NpcActor{ TEXT("актор NPC (таблица|запечённое) ↔ NpcDisplayName") };
		FPair P_WS_NpcSpawn{ TEXT("спаун NPC мира (таблица|запечённое) ↔ NpcDisplayName") };
		FPair P_QLog{ TEXT("журнал RouteTargetName ↔ QuestTargetName") };
		FPair P_QTrk{ TEXT("трекер цели ↔ QuestTargetName") };
		FPair K1{ TEXT("К1 контроль: имя моба без гарда") };
		FPair K2{ TEXT("К2 контроль: NpcId+1") };
		FPair K3{ TEXT("К3 контроль: модель со строки MobId") };
		FPair K4{ TEXT("К4 контроль: вид цели инвертирован") };

		TArray<FString> Out;
		Out.Reserve(200000);
		int64 NonEmptyNew = 0;
		const FString kNoModel(TEXT("нет_такой_модели_zz"));

		// ── мобы ──
		for (int32 Id = -2; Id <= MobIdMax; ++Id)
		{
			TArray<FString> Variants;
			Variants.Add(FString());
			Variants.Append(Models);
			Variants.Add(AsdaEntityText::MobModel(Id).ToUpper());
			Variants.Add(kNoModel);
			for (const FString& M : Variants)
			{
				const FString In = FString::Printf(TEXT("моб %d модель «%s»"), Id, *M);
				const FString New = AsdaEntityText::MobName(Id, M);
				const FString OldW = Old_WS_GetMobName(Id, M);
				const FString OldN = Old_NP_LookupMobName(Id, M);
				P_WS_MobName.Check(OldW, New, In);
				P_NP_MobName.Check(OldN, New, In);
				P_K6.Check(OldW, OldN, In);
				// К1: без гарда = прямая строка.
				K1.Check(Old_NP_LookupMobName(Id, FString()), New, In);
				if (!New.IsEmpty()) { ++NonEmptyNew; }
				Out.Add(FString::Printf(TEXT("MOB\t%d\t%s\t%s"), Id, *M, *New));
			}
			const FString In = FString::Printf(TEXT("моб %d"), Id);
			const FString NewModel = AsdaEntityText::MobModel(Id);
			P_Model.Check(Old_WS_GetMobModelName(Id), NewModel, In);
			K3.Check(Old_WS_GetMobModelName(Id + 1), NewModel, In);
			const FString NewSnd = AsdaEntityText::MobSoundBase(Id);
			P_Sound.Check(Old_WS_GetMobSoundBase(Id), NewSnd, In);
			Out.Add(FString::Printf(TEXT("MODEL\t%d\t%s\tSND\t%s"), Id, *NewModel, *NewSnd));
		}

		// ── NPC ──
		const FString kBaked(TEXT("ЗАПЕЧЁННОЕ"));
		for (int32 Id = -2; Id <= NpcIdMax; ++Id)
		{
			const FString In = FString::Printf(TEXT("NPC %d"), Id);
			const FString New = AsdaEntityText::NpcName(Id);
			P_WS_Npc.Check(Old_WS_GetNpcName(Id), New, In);
			P_NP_Npc.Check(Old_NP_LookupNpcName(Id), New, In);
			K2.Check(Old_NP_LookupNpcName(Id + 1), New, In);
			for (const FString& Baked : { FString(), kBaked })
			{
				const FString InB = FString::Printf(TEXT("NPC %d запечённое «%s»"), Id, *Baked);
				const FString NewD = AsdaEntityText::NpcDisplayName(Id, Baked);
				P_NpcActor.Check(Old_NpcActor_DisplayName(Id, Baked), NewD, InB);
				P_WS_NpcSpawn.Check(Old_WS_NpcSpawn_DisplayName(Id, Baked), NewD, InB);
				Out.Add(FString::Printf(TEXT("NPCD\t%d\t%s\t%s"), Id, *Baked, *NewD));
			}
			Out.Add(FString::Printf(TEXT("NPC\t%d\t%s"), Id, *New));
		}

		// ── цели квестов: все id обоих диапазонов × вид ──
		const int32 QMax = FMath::Max(MobIdMax, NpcIdMax);
		for (int32 Kind : { -1, 0, 1, 2 })
		{
			for (int32 Id = -2; Id <= QMax; ++Id)
			{
				const FString In = FString::Printf(TEXT("цель вид %d id %d"), Kind, Id);
				const FString New = AsdaEntityText::QuestTargetName(Kind, Id);
				// ★ 25.10.2026 [ОРИГИНАЛ]: цель-моб гида = строка `Id` (эталон), старые копии брали `Id − 1` — при ключе
				//   `QuestGuideMobRowOn()` старому ядру отдаётся `Id + 1`, т.е. сверяется та же строка таблицы.
				const int32 OldId = (Kind == 1 && AsdaEntityText::QuestGuideMobRowOn()) ? Id + 1 : Id;
				P_QLog.Check(Old_QuestLog_Target(Kind, OldId), New, In);
				P_QTrk.Check(Old_QuestTracker_Target(Kind, OldId), New, In);
				K4.Check(Old_QuestTracker_Target(Kind == 1 ? 0 : 1, Id), New, In);
				Out.Add(FString::Printf(TEXT("QT\t%d\t%d\t%s"), Kind, Id, *New));
			}
		}

		FString Joined = FString::Join(Out, TEXT("\n"));
		FFileHelper::SaveStringToFile(Joined, *OutPath, FFileHelper::EEncodingOptions::ForceUTF8WithoutBOM);
		FTCHARToUTF8 U(*Joined);
		const FString Md5 = FMD5::HashBytes((const uint8*)U.Get(), U.Length());

		int64 TotalCmp = 0, TotalDiff = 0;
		for (FPair* P : { &P_WS_MobName, &P_NP_MobName, &P_Model, &P_Sound, &P_WS_Npc, &P_NP_Npc, &P_NpcActor, &P_WS_NpcSpawn, &P_QLog, &P_QTrk })
		{
			TotalCmp += P->Cmp; TotalDiff += P->Diff;
			UE_LOG(LogAsdaEntityTextStand, Log, TEXT("ENTITYTEXT_STAND ПАРА %s: сравнений %lld, расхождений %lld%s%s"),
				P->Name, P->Cmp, P->Diff, P->Diff ? TEXT(" — первое: ") : TEXT(""), *P->FirstDiff);
		}
		UE_LOG(LogAsdaEntityTextStand, Log, TEXT("ENTITYTEXT_STAND СПРАВКА %s: сравнений %lld, расхождений %lld%s%s"),
			P_K6.Name, P_K6.Cmp, P_K6.Diff, P_K6.Diff ? TEXT(" — первое: ") : TEXT(""), *P_K6.FirstDiff);
		bool bControlsOk = true;
		for (FPair* P : { &K1, &K2, &K3, &K4 })
		{
			bControlsOk &= (P->Diff > 0);
			UE_LOG(LogAsdaEntityTextStand, Log, TEXT("ENTITYTEXT_STAND КОНТРОЛЬ %s: сравнений %lld, расхождений %lld (нужно > 0) — %s"),
				P->Name, P->Cmp, P->Diff, *P->FirstDiff);
		}
		const bool bOk = (TotalDiff == 0) && bControlsOk && NonEmptyNew > 0;
		UE_LOG(LogAsdaEntityTextStand, Log,
			TEXT("ENTITYTEXT_STAND ИТОГ: строк мобов %d, моделей %d, MobId −2..%d, NpcId −2..%d; сравнений %lld, расхождений %lld; ")
			TEXT("непустых имён мобов %lld; контроли %s; md5 вывода %s; файл %s; %.1f с; ВЕРДИКТ %s"),
			MobRows, Models.Num(), MobIdMax, NpcIdMax, TotalCmp, TotalDiff, NonEmptyNew,
			bControlsOk ? TEXT("все поймали") : TEXT("СЛЕПЫ"), *Md5, *OutPath, FPlatformTime::Seconds() - T0,
			bOk ? TEXT("УСПЕХ") : TEXT("ПРОВАЛ"));
		// ★ В3 S1: единый итог для пакета (вердикт стенда одной проверкой) и выход по `quit`.
		AsdaSelfTestKit::Finish(nullptr, TEXT("asda.EntityText.Stand"), KA.bQuit, LogAsdaEntityTextStand, TEXT("ENTITYTEXT_STAND"), bOk ? 1 : 0, bOk ? 0 : 1);
	}
}

static FAutoConsoleCommand GAsdaEntityTextStandCmd(
	TEXT("asda.EntityText.Stand"),
	TEXT("Стенд сверки AsdaEntityText со старыми копиями имён/моделей/звуков сущностей на реальных таблицах. Аргумент: файл вывода."),
	FConsoleCommandWithArgsDelegate::CreateStatic(&AsdaEntityTextStand::Run));

// ★★★ ПРОВЕРКА УДЕРЖАНИЯ ТАБЛИЦ ИМЁН ПРИ СБОРКЕ МУСОРА (ревизия п.14, 15.09.2026).
//   Вопрос: переживают ли `DT_MobNames`/`DT_NpcNames`/`DT_MobSounds` полный GC (`obj gc` =
//   `CollectGarbage(GARBAGE_COLLECTION_KEEPFLAGS)`) при живом GameInstance, когда мир их удерживает.
//   Шаги (каждый — строка `ENTITYTEXT_GC …` в лог):
//     1) мировая подсистема живого GameInstance; есть ли таблицы в памяти (`FindObject`, без загрузки);
//     2) КОНТРОЛЬ (только если мир их ещё НЕ удерживал): таблицы берутся одной слабой `AsdaDt::Cached`,
//        `obj gc` → ожидаем, что хотя бы одна выгружена (иначе проверка слепа: их держит кто-то ещё);
//     3) `UAsdaWorldSubsystem::HoldEntityNameTables()` — тот же вызов, что в спаунах мира;
//        слабые указатели ДО GC → `obj gc` → те же объекты живы и `FindObject` их находит.
//   ⚠ Шаг 2 портит сырые указатели `GOld*` стенда (после GC могут висеть): запускать ПОСЛЕ `asda.EntityText.Stand`.
namespace AsdaEntityTextStand
{
	static int32 CountLoaded(const TCHAR* Tag)
	{
		int32 N = 0;
		for (const TCHAR* P : { AsdaEntityText::kMobNamesPath, AsdaEntityText::kNpcNamesPath, AsdaEntityText::kMobSoundsPath })
		{
			const bool bIn = FindObject<UDataTable>(nullptr, P) != nullptr;   // только поиск в памяти, НЕ загрузка
			N += bIn ? 1 : 0;
			UE_LOG(LogAsdaEntityTextStand, Log, TEXT("ENTITYTEXT_GC %s: %s в памяти=%d"), Tag, P, bIn ? 1 : 0);
		}
		return N;
	}

	static void RunGcCheck(const TArray<FString>& Args)   // мир находим сами — нужен ИГРОВОЙ, а не мир консоли
	{
		UAsdaWorldSubsystem* WS = nullptr;
		UWorld* GameWorld = nullptr;
		for (const FWorldContext& Ctx : GEngine->GetWorldContexts())
		{
			if (Ctx.OwningGameInstance && Ctx.World() && Ctx.World()->IsGameWorld())
			{
				WS = Ctx.OwningGameInstance->GetSubsystem<UAsdaWorldSubsystem>();
				if (WS) { GameWorld = Ctx.World(); break; }
			}
		}
		if (!WS)
		{
			UE_LOG(LogAsdaEntityTextStand, Error, TEXT("ENTITYTEXT_GC ИТОГ: ВЕРДИКТ ПРОВАЛ — нет живого GameInstance с UAsdaWorldSubsystem"));
			return;
		}
		UE_LOG(LogAsdaEntityTextStand, Log, TEXT("ENTITYTEXT_GC мир: %s (игровой=%d), удержание мира до проверки=%d"),
			*GameWorld->GetMapName(), GameWorld->IsGameWorld() ? 1 : 0, WS->IsEntityNameTablesHeld() ? 1 : 0);
		const int32 NStart = CountLoaded(TEXT("старт"));

		// ── 2) КОНТРОЛЬ: без удержания мира слабый кэш таблицы НЕ спасает ──
		int32 NCtrlAfter = -1;   // −1 = контроль невозможен (мир уже удерживал таблицы до команды)
		if (!WS->IsEntityNameTablesHeld())
		{
			const bool bCtrlLoaded = AsdaDt::Cached(AsdaEntityText::kMobNamesPath) && AsdaDt::Cached(AsdaEntityText::kNpcNamesPath)
				&& AsdaDt::Cached(AsdaEntityText::kMobSoundsPath);
			CountLoaded(TEXT("контроль до GC"));
			GEngine->Exec(GameWorld, TEXT("obj gc"));   // = CollectGarbage(GARBAGE_COLLECTION_KEEPFLAGS)
			NCtrlAfter = CountLoaded(TEXT("контроль после obj gc"));
			UE_LOG(LogAsdaEntityTextStand, Log, TEXT("ENTITYTEXT_GC КОНТРОЛЬ: загружены слабо=%d, после obj gc в памяти %d из 3 (нужно < 3 — иначе проверка слепа)"),
				bCtrlLoaded ? 1 : 0, NCtrlAfter);
		}
		else
		{
			UE_LOG(LogAsdaEntityTextStand, Warning, TEXT("ENTITYTEXT_GC КОНТРОЛЬ: пропущен — мир уже удерживал таблицы до команды"));
		}

		// ── 3) удержание миром (тот же вызов, что в спаунах) → obj gc → те же объекты живы ──
		const int32 NHeld = WS->HoldEntityNameTables();
		TArray<TWeakObjectPtr<UDataTable>> Before;
		for (const TCHAR* P : { AsdaEntityText::kMobNamesPath, AsdaEntityText::kNpcNamesPath, AsdaEntityText::kMobSoundsPath })
		{
			Before.Add(FindObject<UDataTable>(nullptr, P));
		}
		CountLoaded(TEXT("удержание до GC"));
		GEngine->Exec(GameWorld, TEXT("obj gc"));
		const int32 NAfter = CountLoaded(TEXT("удержание после obj gc"));
		int32 NSame = 0;
		for (int32 i = 0; i < Before.Num(); ++i)
		{
			const UDataTable* Now = FindObject<UDataTable>(nullptr,
				i == 0 ? AsdaEntityText::kMobNamesPath : (i == 1 ? AsdaEntityText::kNpcNamesPath : AsdaEntityText::kMobSoundsPath));
			NSame += (Before[i].IsValid() && Before[i].Get() == Now) ? 1 : 0;   // тот же объект, не перезагруженный
		}
		// Второй вызов удержания обязан ничего не грузить и вернуть то же число.
		const int32 NHeld2 = WS->HoldEntityNameTables();

		const bool bControlOk = (NCtrlAfter >= 0 && NCtrlAfter < 3);
		const bool bOk = (NHeld == 3) && (NHeld2 == 3) && (NAfter == 3) && (NSame == 3);
		UE_LOG(LogAsdaEntityTextStand, Log,
			TEXT("ENTITYTEXT_GC ИТОГ: старт в памяти %d; контроль без удержания после obj gc %d; удержано миром %d (повтор %d); ")
			TEXT("после obj gc в памяти %d, тех же объектов %d из 3; контроль %s; ВЕРДИКТ %s"),
			NStart, NCtrlAfter, NHeld, NHeld2, NAfter, NSame,
			bControlOk ? TEXT("поймал выгрузку") : (NCtrlAfter < 0 ? TEXT("НЕВОЗМОЖЕН") : TEXT("СЛЕП")),
			bOk ? TEXT("УСПЕХ") : TEXT("ПРОВАЛ"));
	}
}

static FAutoConsoleCommand GAsdaEntityTextGcCheckCmd(
	TEXT("asda.EntityText.GcCheck"),
	TEXT("Проверка: таблицы имён сущностей переживают obj gc, когда их удерживает UAsdaWorldSubsystem (нужен живой GameInstance). Запускать после asda.EntityText.Stand."),
	FConsoleCommandWithArgsDelegate::CreateStatic(&AsdaEntityTextStand::RunGcCheck));


#endif // !UE_BUILD_SHIPPING
