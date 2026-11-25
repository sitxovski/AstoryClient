// Copyright (c) 2026 @sitxovski (Discord)
// Реализация слоя данных обозревателя — см. AsdaMobBrowserData.h.
#include "AsdaMobBrowserData.h"

#include "Engine/DataTable.h"
#include "Engine/SkeletalMesh.h"
#include "Engine/StaticMesh.h"
#include "Animation/AnimSequence.h"
#include "Materials/MaterialInterface.h"
#include "AssetRegistry/AssetRegistryModule.h"
#include "AssetRegistry/IAssetRegistry.h"
#include "HAL/IConsoleManager.h"
#include "Misc/Paths.h"
#include "Modules/ModuleManager.h"

#include "AsdaDataTableCache.h"            // AsdaDt::Cached — общая точка таблиц игры
#include "World/AsdaEntityText.h"           // MobName / MobModel / NpcName, kMobNamesPath / kNpcNamesPath — то же, что у игры
#include "Text/AsdaLang.h"                  // FAsdaLang::FindOverlay — английские имена (оверлеи MobNames / NpcNames)
#include "Npc/AsdaMobActor.h"               // AAsdaMobActor::kMobStatsPath / kMobTexturesPath
#include "Data/AsdaMobStatsRow.h"           // раскладка строки DT_MobStats
#include "Data/AsdaMobTextureRow.h"         // раскладка строки DT_MobTextures
#include "Quest/AsdaQuestTypes.h"           // раскладка строки DT_NpcQuestNpc (FAsdaNpcQuestNpcRow)
#include "Resources/AsdaAssetPaths.h"       // kSkelRootMobSkel / kSkelRootNpc / MobStaticMeshObject — реестр путей игры
#include "Resources/AsdaModelAnimSubsystem.h" // LoadMeshChecked / BuildSkel*Path — загрузка как у игры

// ★ Таблица «тип → скелетный вариант клипов базы» — ФАЙЛ ИГРОВОГО МОДУЛЯ (автоген gen_mob_skill_tables.py), не копия:
//   игра компилирует тот же файл в SpawnNetMobDynamic. Своё пространство имён — чтобы символы не пересекались.
namespace AsdaMobBrowserClipBase
{
#include "AsdaMobClipBase.inl"
}

DEFINE_LOG_CATEGORY_STATIC(LogAsdaMobBrowser, Log, All);

namespace
{
	// ★ Объектный путь DT_NpcQuestNpc — тот же ассет, что у UAsdaNpcDialogSubsystem (kDtNpc, AsdaNpcDialogSubsystem.cpp:104);
	//   там он статический в .cpp и наружу не экспортирован.
	const TCHAR* const kNpcRecordPath = TEXT("/Game/AsdaAssets/Data/NPC/DT_NpcQuestNpc.DT_NpcQuestNpc");

	// Ключ игры (чтение по имени допустимо: модуль только редакторский, в Shipping его нет).
	int32 ReadGameCVar(const TCHAR* Name, int32 Default)
	{
		if (IConsoleVariable* V = IConsoleManager::Get().FindConsoleVariable(Name)) { return V->GetInt(); }
		return Default;
	}

	// Строка таблицы по числовому ключу без StaticStruct чужого модуля: тип строки сверяем по имени структуры.
	template<typename TRow>
	const TRow* RowOf(const UDataTable* T, int32 Key, const TCHAR* StructName)
	{
		if (!T || !T->GetRowStruct() || T->GetRowStruct()->GetName() != StructName) { return nullptr; }
		return reinterpret_cast<const TRow*>(T->FindRowUnchecked(FName(*FString::FromInt(Key))));
	}

	// Числовые ключи строк таблицы (нечисловые пропускаются).
	TArray<int32> NumericKeys(const UDataTable* T)
	{
		TArray<int32> Keys;
		if (!T) { return Keys; }
		for (const TPair<FName, uint8*>& It : T->GetRowMap())
		{
			const FString K = It.Key.ToString();
			if (K.IsNumeric()) { Keys.Add(FCString::Atoi(*K)); }
		}
		return Keys;
	}

	// Клипы скелетной папки: `<имя>anim_NN` по возрастанию NN; без них — одиночный `<имя>_Anim` (как asda_place_npcs.py).
	void CollectClips(const FString& Root, const FString& Name, TArray<FAsdaMobClip>& Out)
	{
		const FString MeshPkg = UAsdaModelAnimSubsystem::BuildSkelPath(Root, Name);   // …/SkeletalMeshes/<имя>
		const FString Folder = FPaths::GetPath(MeshPkg);
		IAssetRegistry& AR = FModuleManager::LoadModuleChecked<FAssetRegistryModule>(TEXT("AssetRegistry")).Get();
		AR.ScanPathsSynchronous({ Folder }, /*bForceRescan*/ false);
		TArray<FAssetData> Assets;
		AR.GetAssetsByPath(FName(*Folder), Assets, /*bRecursive*/ false);
		const FString Prefix = Name + TEXT("anim_");
		bool bSingle = false;
		for (const FAssetData& D : Assets)
		{
			if (D.AssetClassPath != UAnimSequence::StaticClass()->GetClassPathName()) { continue; }
			const FString N = D.AssetName.ToString();
			if (N.Equals(Name + TEXT("_Anim"), ESearchCase::IgnoreCase)) { bSingle = true; continue; }
			if (!N.StartsWith(Prefix, ESearchCase::IgnoreCase)) { continue; }
			const FString Num = N.Mid(Prefix.Len());
			if (!Num.IsNumeric()) { continue; }
			FAsdaMobClip C;
			C.Index = FCString::Atoi(*Num);
			C.AssetPath = UAsdaModelAnimSubsystem::BuildSkelAnimPath(Root, Name, C.Index);
			Out.Add(C);
		}
		if (Out.Num() == 0 && bSingle)
		{
			FAsdaMobClip C;
			C.Index = 0;
			C.AssetPath = UAsdaModelAnimSubsystem::BuildSkelPath(Root, Name, TEXT("_Anim"));
			Out.Add(C);
		}
		Out.Sort([](const FAsdaMobClip& X, const FAsdaMobClip& Y) { return X.Index < Y.Index; });
	}
}

// ── Запись ────────────────────────────────────────────────────────────────────────────────────────
FString FAsdaMobEntry::DisplayName(bool bEnglish) const
{
	const FString& A = bEnglish ? NameEn : NameRu;
	const FString& B = bEnglish ? NameRu : NameEn;
	if (!A.IsEmpty()) { return A; }
	if (!B.IsEmpty()) { return B; }
	return TEXT("-");
}

int32 FAsdaMobEntry::DisplayLevel() const
{
	if (Kind != EAsdaBrowserKind::Mob) { return INDEX_NONE; }   // у NPC уровня в таблицах нет
	return Client.bValid ? Client.Level : INDEX_NONE;
}

// ── Экземпляр ─────────────────────────────────────────────────────────────────────────────────────
TSharedRef<FAsdaMobBrowserData> FAsdaMobBrowserData::Get()
{
	static TSharedPtr<FAsdaMobBrowserData> Inst;
	if (!Inst.IsValid()) { Inst = MakeShared<FAsdaMobBrowserData>(); }
	return Inst.ToSharedRef();
}

const TCHAR* FAsdaMobBrowserData::NpcRecordTablePath() { return kNpcRecordPath; }

bool FAsdaMobBrowserData::Reload()
{
	Mobs.Reset(); Npcs.Reset(); MobById.Reset(); NpcById.Reset();
	LoadError.Reset();
	LoadMobs();
	LoadNpcs();
	MobById.GenerateValueArray(Mobs);
	NpcById.GenerateValueArray(Npcs);
	const auto ById = [](const TSharedPtr<FAsdaMobEntry>& A, const TSharedPtr<FAsdaMobEntry>& B) { return A->Id < B->Id; };
	Mobs.Sort(ById);
	Npcs.Sort(ById);
	UE_LOG(LogAsdaMobBrowser, Display, TEXT("[обозреватель] мобов %d, NPC %d"), Mobs.Num(), Npcs.Num());
	return Mobs.Num() + Npcs.Num() > 0;
}

TSharedPtr<FAsdaMobEntry> FAsdaMobBrowserData::Find(EAsdaBrowserKind K, int32 Id) const
{
	const TSharedPtr<FAsdaMobEntry>* P = (K == EAsdaBrowserKind::Npc ? NpcById : MobById).Find(Id);
	return P ? *P : nullptr;
}

void FAsdaMobBrowserData::LoadMobs()
{
	const UDataTable* Names = AsdaDt::Cached(AsdaEntityText::kMobNamesPath);
	if (!Names) { LoadError += FString::Printf(TEXT("нет %s; "), AsdaEntityText::kMobNamesPath); return; }
	const UDataTable* Stats = AsdaDt::Cached(AAsdaMobActor::kMobStatsPath);
	for (const int32 Row : NumericKeys(Names))
	{
		const int32 Id = Row + 1;   // строка = MobId − 1
		TSharedPtr<FAsdaMobEntry> E = MakeShared<FAsdaMobEntry>();
		E->Kind = EAsdaBrowserKind::Mob;
		E->Id = Id;
		E->NameRu = AsdaEntityText::MobName(Id, FString());   // вне PIE — имя русского ассета (как у игры в редакторе)
		E->Model = AsdaEntityText::MobModel(Id);
		FString En;
		if (FAsdaLang::FindOverlay(EAsdaLang::En, TEXT("MobNames"), FString::Printf(TEXT("%d.Name"), Row), En)) { E->NameEn = En; }
		if (const FAsdaMobStatsRow* R = RowOf<FAsdaMobStatsRow>(Stats, Row, TEXT("AsdaMobStatsRow")))
		{
			FAsdaMobClientStats& C = E->Client;
			C.bValid = true;
			C.Level = R->Level; C.MaxHp = R->MaxHp; C.Exp = R->Exp;
			C.Element = R->Rank;          // мисней колонки: Rank = стихия (+0xF8)
			C.Rank = R->Aggression;       // мисней колонки: Aggression = ранг (+0xFC)
			C.AttackFirst = R->AttackFirst; C.Lives = R->Lives;
			C.SizeClass = R->SizeClass; C.SizePercent = R->SizePercent;
			C.HalfX = R->HalfX; C.HalfY = R->HalfY; C.HalfZ = R->HalfZ;
			C.SoundRange = R->SoundRange;
		}
		MobById.Add(Id, E);
	}
}

void FAsdaMobBrowserData::LoadNpcs()
{
	const UDataTable* Names = AsdaDt::Cached(AsdaEntityText::kNpcNamesPath);
	const UDataTable* Recs = AsdaDt::Cached(kNpcRecordPath);
	if (!Names && !Recs) { LoadError += TEXT("нет DT_NpcNames и DT_NpcQuestNpc; "); return; }
	TSet<int32> Ids(NumericKeys(Names));
	Ids.Append(NumericKeys(Recs));
	for (const int32 Id : Ids)
	{
		if (Id <= 0) { continue; }
		TSharedPtr<FAsdaMobEntry> E = MakeShared<FAsdaMobEntry>();
		E->Kind = EAsdaBrowserKind::Npc;
		E->Id = Id;
		E->NameRu = AsdaEntityText::NpcName(Id);   // ключ = NpcId; вне PIE — русское
		FString En;
		if (FAsdaLang::FindOverlay(EAsdaLang::En, TEXT("NpcNames"), FString::Printf(TEXT("%d.Name"), Id), En)) { E->NameEn = En; }
		if (const FAsdaNpcQuestNpcRow* R = RowOf<FAsdaNpcQuestNpcRow>(Recs, Id, TEXT("AsdaNpcQuestNpcRow")))
		{
			FAsdaNpcInfo& N = E->Npc;
			N.bValid = true;
			N.ModelName = R->ModelName; N.NsrName = R->NsrName;
			N.PlateHeight = R->PlateHeight; N.MapId = R->MapId;
			N.WorldX = R->WorldX; N.WorldY = R->WorldY;
			N.NpcType = R->NpcType;
			E->Model = R->ModelName;   // модель NPC = npc_list +0x46 (манифест запекания и портрет диалога)
		}
		NpcById.Add(Id, E);
	}
}

// ── Модель ────────────────────────────────────────────────────────────────────────────────────────
FAsdaMobModelAssets FAsdaMobBrowserData::ResolveModel(const FAsdaMobEntry& E)
{
	FAsdaMobModelAssets A;
	A.Model = E.Model;
	if (A.Model.IsEmpty()) { return A; }

	if (E.Kind == EAsdaBrowserKind::Npc)
	{
		// NPC: скелет /Models/NPC/<модель> (asda_place_npcs.py: нет меша — NPC не ставится; статического фолбэка нет).
		static const FString kNpcRoot(AsdaAssetPaths::kSkelRootNpc);
		A.SkelModel = A.Model;
		A.Skel = UAsdaModelAnimSubsystem::LoadMeshChecked(UAsdaModelAnimSubsystem::BuildSkelPath(kNpcRoot, A.Model));
		if (A.Skel) { CollectClips(kNpcRoot, A.Model, A.Clips); }
		return A;
	}

	// Моб: ровно правило UAsdaWorldSubsystem::SpawnNetMobDynamic.
	static const FString kRoot(AsdaAssetPaths::kSkelRootMobSkel);
	A.SkelModel = A.Model;
	if (ReadGameCVar(TEXT("asda.Mob.ClipBaseModel"), 1) != 0)   // тот же ключ и то же умолчание (1), что у игры
	{
		for (const AsdaMobBrowserClipBase::FAsdaMobClipBaseRow& R : AsdaMobBrowserClipBase::kAsdaMobClipBase)
		{
			if (R.MobId == E.Id) { A.SkelModel = R.Variant; A.bClipBaseVariant = true; break; }
		}
	}
	A.Skel = UAsdaModelAnimSubsystem::LoadMeshChecked(UAsdaModelAnimSubsystem::BuildSkelPath(kRoot, A.SkelModel));
	if (!A.Skel && A.SkelModel != A.Model)
	{
		A.SkelModel = A.Model;   // как у игры: варианта нет — меш +0x04 как прежде
		A.bClipBaseVariant = false;
		A.Skel = UAsdaModelAnimSubsystem::LoadMeshChecked(UAsdaModelAnimSubsystem::BuildSkelPath(kRoot, A.Model));
	}
	if (!A.Skel)
	{
		A.Static = LoadObject<UStaticMesh>(nullptr, *AsdaAssetPaths::MobStaticMeshObject(A.Model), nullptr, LOAD_NoWarn | LOAD_Quiet);
	}
	if (A.Skel) { CollectClips(kRoot, A.SkelModel, A.Clips); }

	// Пер-типовые текстуры — правило AAsdaMobActor::ApplyMobTextures (строка MobId−1, гард «меш той же модели»,
	//   вариант `<меш>__<база>` принимается при ключе asda.Mob.TexGuardClipBase, умолчание 1).
	const UObject* MeshObj = A.Skel ? static_cast<const UObject*>(A.Skel) : static_cast<const UObject*>(A.Static);
	if (MeshObj)
	{
		if (const FAsdaMobTextureRow* R = RowOf<FAsdaMobTextureRow>(AsdaDt::Cached(AAsdaMobActor::kMobTexturesPath), E.Id - 1, TEXT("AsdaMobTextureRow")))
		{
			const FString MeshName = MeshObj->GetName();
			const bool bVariantOk = ReadGameCVar(TEXT("asda.Mob.TexGuardClipBase"), 1) != 0 && !R->Model.IsEmpty()
				&& MeshName.StartsWith(R->Model + TEXT("__"), ESearchCase::IgnoreCase);
			const bool bGuard = R->Model.IsEmpty() || MeshName.Equals(R->Model, ESearchCase::IgnoreCase) || bVariantOk;
			if (bGuard && R->Slots.Num() > 0 && R->Slots.Num() == R->Materials.Num())
			{
				for (int32 i = 0; i < R->Slots.Num(); ++i)
				{
					if (UMaterialInterface* M = R->Materials[i].LoadSynchronous()) { A.MaterialOverrides.Emplace(R->Slots[i], M); }
				}
			}
		}
	}
	return A;
}

// ── Подписи ───────────────────────────────────────────────────────────────────────────────────────
FString FAsdaMobBrowserData::ClipLabel(EAsdaBrowserKind K, int32 Index)
{
	if (K == EAsdaBrowserKind::Npc)
	{
		// Состояние сущности → клип (AAsdaNpcActor::kNpcAnimStateToClip = { 0, 2, 3, 4, 1, 2, 0, 3, 4, 5 }, эталон uiPanelSelectTab):
		//   клип 0 — покой (состояния 0, 6); 2 — разговор (состояние 1, «РАЗГОВОР» эталона) и 5; прочие — номером состояния.
		switch (Index)
		{
		case 0: return TEXT("Idle");
		case 1: return TEXT("State 4");
		case 2: return TEXT("Talk (state 1, 5)");
		case 3: return TEXT("State 2, 7");
		case 4: return TEXT("State 3, 8");
		case 5: return TEXT("State 9");
		default: return FString::Printf(TEXT("Clip %d"), Index);
		}
	}
	// Моб: подпись — по СЛОТАМ игры, которые берут этот файл (обратное к AAsdaMobActor::ClipFileForSlot, та же функция игры,
	//   ключ asda.Mob.ClipSlotMap). Смысл слотов — AAsdaMobActor: 0 покой, 1 боевая стойка, 2 шаг, 3 бег, 4 смерть, 5 замах,
	//   6 замах варианта 1, 7 крит/коды удара < −1, 8 флинч, 18–21 навыки (PlaySkillClip). 14/15 — смысл не доказан: только номер.
	TArray<FString> Parts;
	for (int32 Slot = 0; Slot < 32; ++Slot)
	{
		if (AAsdaMobActor::ClipFileForSlot(Slot) != Index) { continue; }
		switch (Slot)
		{
		case 0: Parts.Add(TEXT("Idle")); break;
		case 1: Parts.Add(TEXT("Combat stance")); break;
		case 2: Parts.Add(TEXT("Walk")); break;
		case 3: Parts.Add(TEXT("Run")); break;
		case 4: Parts.Add(TEXT("Death")); break;
		case 5: Parts.Add(TEXT("Attack")); break;
		case 6: Parts.Add(TEXT("Attack variant 1")); break;
		case 7: Parts.Add(TEXT("Critical hit")); break;
		case 8: Parts.Add(TEXT("Hit reaction")); break;
		case 18: case 19: case 20: case 21: Parts.Add(FString::Printf(TEXT("Skill %d"), Slot - 17)); break;
		default: Parts.Add(FString::Printf(TEXT("Slot %d"), Slot)); break;
		}
	}
	return Parts.Num() > 0 ? FString::Join(Parts, TEXT(" / ")) : FString(TEXT("not used by the game"));
}

// Сравнение имён по правилам языка (ICU): кириллица без учёта регистра, как в словаре.
int32 FAsdaMobBrowserData::CompareNames(const FString& A, const FString& B)
{
	return FText::FromString(A).CompareTo(FText::FromString(B), ETextComparisonLevel::Secondary);
}

// Строка для поиска: нижний регистр по правилам языка (ICU) — «Волк» находит «волк».
FString FAsdaMobBrowserData::SearchKey(const FString& S)
{
	return FText::FromString(S).ToLower().ToString();
}

FString FAsdaMobBrowserData::RankLabel(int32 Rank)
{
	// Панель цели hudShowMobTargetInfo: 1 и 5 → Normal, 2 → Named, 3 → Boss, 4 → Elite (AsdaMobStatsRow.h).
	switch (Rank)
	{
	case 1: case 5: return TEXT("Normal");
	case 2: return TEXT("Named");
	case 3: return TEXT("Boss");
	case 4: return TEXT("Elite");
	default: return TEXT("-");
	}
}

FString FAsdaMobBrowserData::ElementLabel(int32 Element)
{
	// Метки PVP_MARK00..05 панели цели: Огонь/Вода/Земля/Ветер/Свет/Тьма (AsdaMobStatsRow.h, ui_core.c:75756).
	static const TCHAR* const kNames[] = { TEXT("Fire"), TEXT("Water"), TEXT("Earth"), TEXT("Wind"), TEXT("Light"), TEXT("Dark") };
	return (Element >= 0 && Element < 6) ? FString(kNames[Element]) : FString(TEXT("-"));
}
