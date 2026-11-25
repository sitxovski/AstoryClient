// Copyright (c) 2026 @sitxovski (Discord)
#include "UI/AsdaSkillDatabase.h"

#include "Items/AsdaItemTableSubsystem.h"
#include "Items/AsdaItemDbRows.h"
#include "Data/AsdaSkillCoinRow.h"   // ★ R3: FAsdaSkillCoinRow — узкий заголовок вместо AsdaSoulGuardRows.h
#include "AsdaBytes.h"                      // ★ Аудит №3 (A5): единые читалки сырых записей порта
#include "Engine/DataTable.h"
#include "Engine/GameInstance.h"
#include "Text/AsdaLang.h"                 // [ПОРТ] 09.10.2026 (мультиязычность): имена навыков/классов на языке сессии

DEFINE_LOG_CATEGORY_STATIC(LogAsdaSkillDb, Log, All);

// ── Разбор сырых записей (колонка DataHex = ВСЯ запись таблицы 1:1) ───────────────────────────
//
// ★ Аудит №3 (A5): пять СВОИХ читалок (`SkHexToBytes`/`SkU8`/`SkI16`/`SkI32`/`SkAscii`) сняты —
//   теперь база навыков читает записи той же точкой, что и весь порт (`Public/AsdaBytes.h`):
//     SkHexToBytes → AsdaBytes::HexToBytes   SkU8 → AsdaBytes::SafeU8
//     SkI16        → AsdaBytes::SafeI16      SkI32 → AsdaBytes::SafeI32
//     SkAscii      → AsdaBytes::AsciiFieldAt
//   Тела прежних копий совпадали с общими ПОБАЙТОВО (включая трактовку битой записи: не-hex ⇒
//   `HexToBytes` = false, чтение за границей ⇒ 0, не-ASCII ⇒ пустое поле), поэтому смены поведения
//   здесь нет ни в одной ветке — в отличие от копий на `FParse::HexDigit` (A6, другие файлы).

// ── Загрузка ──────────────────────────────────────────────────────────────────────────────────

// ★ Аудит №3 (A12/B7): подъём базы. Таблицы предметов поднимаем ПЕРВЫМИ — из них берётся `SkillCoin`
//   (`Load` без них отказывается читать и пишет ошибку в лог).
void UAsdaSkillDatabase::Initialize(FSubsystemCollectionBase& Collection)
{
	Super::Initialize(Collection);
	Collection.InitializeDependency(UAsdaItemTableSubsystem::StaticClass());
	Load(GetGameInstance());
	// ★ 10.10.2026 (язык на лету, только до входа в мир): имена навыков и классов собраны выше на прежнем языке —
	//   пересобрать таблицы целиком (указателей на `Defs` до входа в мир никто не держит).
	FAsdaLang::OnLanguageChanged().AddWeakLambda(this, [this]()
	{
		bLoaded = false;
		const int32 N = Load(GetGameInstance());
		UE_LOG(LogAsdaSkillDb, Log, TEXT("[навыки] язык «%s»: база пересобрана, навыков %d"), FAsdaLang::Code(FAsdaLang::Current()), N);
	});
}

void UAsdaSkillDatabase::Deinitialize()
{
	FAsdaLang::OnLanguageChanged().RemoveAll(this);   // ★ 10.10.2026: подписка «язык на лету»
	Super::Deinitialize();
}

int32 UAsdaSkillDatabase::Load(UGameInstance* GI)
{
	if (bLoaded) { return Defs.Num(); }
	Defs.Reset();
	Jobs.Reset();

	UAsdaItemTableSubsystem* IT = GI ? GI->GetSubsystem<UAsdaItemTableSubsystem>() : nullptr;
	if (!IT)
	{
		UE_LOG(LogAsdaSkillDb, Error, TEXT("[навыки] нет UAsdaItemTableSubsystem — таблицы не читаются"));
		return 0;
	}

	// 1) skill_info.sif (273 б): числовые поля навыка + позиция узла в сетке.
	if (UDataTable* Dt = IT->GetDataTable(FName(TEXT("skill_info"))))
	{
		for (const TPair<FName, uint8*>& It : Dt->GetRowMap())
		{
			const FAsdaSkillInfoRow* R = reinterpret_cast<const FAsdaSkillInfoRow*>(It.Value);
			if (!R) { continue; }
			TArray<uint8> Rec;
			if (!AsdaBytes::HexToBytes(R->DataHex, Rec) || Rec.Num() < 273) { continue; }

			FAsdaSkillDef D;
			D.SkillCode      = AsdaBytes::SafeI16(Rec, 0x00);
			D.Name           = FAsdaLang::ResolveRow(Dt, It.Key, TEXT("Name"), R->Name);   // [ПОРТ] 09.10.2026 (мультиязычность): оверлей `skill_info`
			D.Kind           = AsdaBytes::SafeU8(Rec, 0x2A);
			D.MaxLevel       = AsdaBytes::SafeU8(Rec, 0x2B);
			D.SubType        = AsdaBytes::SafeU8(Rec, 0x2C);
			D.CastTimeMs     = AsdaBytes::SafeI16(Rec, 0x2D);   // ★ 31.10.2026 (баг 7): время каста — срок предохранителя состояния 14
			D.CooldownMs     = AsdaBytes::SafeI32(Rec, 0x2F);
			D.CastType       = AsdaBytes::SafeU8(Rec, 0x33);
			D.TargetSlotType = AsdaBytes::SafeU8(Rec, 0x34);
			D.RangeMeters    = AsdaBytes::SafeU8(Rec, 0x35);
			D.ResourceType   = AsdaBytes::SafeU8(Rec, 0x36);
			D.AoeRadius      = AsdaBytes::SafeU8(Rec, 0x37);
			D.MpCostBase     = AsdaBytes::SafeI16(Rec, 0x3A);
			D.MpCostPerLevel = AsdaBytes::SafeI16(Rec, 0x3C);
			D.MsBase         = AsdaBytes::SafeI16(Rec, 0x3E);
			D.MsPerLevel     = AsdaBytes::SafeI16(Rec, 0x40);
			D.SrBase         = AsdaBytes::SafeU8(Rec, 0x44);
			D.SrPerLevel     = AsdaBytes::SafeU8(Rec, 0x45);
			D.S1Base         = AsdaBytes::SafeI16(Rec, 0x4A);
			D.S1PerLevel     = AsdaBytes::SafeI16(Rec, 0x4C);
			// Три buff-слота (источники [s2]/[s3]/[sdt1..3]/[sdtt1..3] в описании навыка).
			D.Buff0StateId          = AsdaBytes::SafeI16(Rec, 0x48);   // ★ id состояния слота 0 (эталон buff0SkillId)
			D.Buff0Duration         = AsdaBytes::SafeI32(Rec, 0x4E);
			D.Buff0DurationPerLevel = AsdaBytes::SafeI32(Rec, 0x52);
			D.Buff0Cooldown         = AsdaBytes::SafeI16(Rec, 0x56);
			D.Buff1StateId          = AsdaBytes::SafeI16(Rec, 0x59);   // ★ id состояния слота 1 (шаг блока 0x11)
			D.S2Base                = AsdaBytes::SafeI16(Rec, 0x5B);
			D.S2PerLevel            = AsdaBytes::SafeI16(Rec, 0x5D);
			D.Buff1Duration         = AsdaBytes::SafeI32(Rec, 0x5F);
			D.Buff1DurationPerLevel = AsdaBytes::SafeI32(Rec, 0x63);
			D.Buff1Cooldown         = AsdaBytes::SafeI16(Rec, 0x67);
			D.Buff2StateId          = AsdaBytes::SafeI16(Rec, 0x6A);   // ★ id состояния слота 2 (шаг блока 0x11)
			D.S3Base                = AsdaBytes::SafeI16(Rec, 0x6C);
			D.S3PerLevel            = AsdaBytes::SafeI16(Rec, 0x6E);
			D.Buff2Duration         = AsdaBytes::SafeI32(Rec, 0x70);
			D.Buff2DurationPerLevel = AsdaBytes::SafeI32(Rec, 0x74);
			D.Buff2Cooldown         = AsdaBytes::SafeI16(Rec, 0x78);
			// Маска оружия — 8 байт с +0x83 (запись НЕ выровнена, читаем побайтно).
			{
				int64 M = 0;
				for (int32 b = 0; b < 8; ++b) { M |= ((int64)AsdaBytes::SafeU8(Rec, 0x83 + b)) << (8 * b); }
				D.WeaponMask = M;
			}
			// ★ Группа общего отката (`skillStartGroupCooldown@0xE57860`): 6 × i16 с +0xA7, −1 = пусто.
			{
				D.ChainSkillIds.Reset(6);
				for (int32 k = 0; k < 6; ++k)
				{
					const int32 Id = AsdaBytes::SafeI16(Rec, 0xA7 + 2 * k);
					if (Id != -1) { D.ChainSkillIds.Add(Id); }
				}
			}
			D.BehaviorType   = AsdaBytes::SafeU8(Rec, 0x8B);   // реакция цели на удар (отброс); 0 = сильный + наземные тайлы
			D.ElementAttr    = AsdaBytes::SafeU8(Rec, 0x8C);
			D.SrCostDivisor  = FMath::Max(1, AsdaBytes::SafeU8(Rec, 0xB6));
			D.SpCostBase     = AsdaBytes::SafeU8(Rec, 0xBB);
			D.SpCostPerTier  = AsdaBytes::SafeU8(Rec, 0xBC);
			D.Grade          = AsdaBytes::SafeU8(Rec, 0xBD);
			D.GradeNum       = AsdaBytes::SafeU8(Rec, 0xBE);
			D.EffectSkillId0 = AsdaBytes::SafeI16(Rec, 0xC8);
			D.EffectLevel0   = AsdaBytes::SafeU8(Rec, 0xCA);
			D.EffectSkillId1 = AsdaBytes::SafeI16(Rec, 0xCB);
			D.EffectLevel1   = AsdaBytes::SafeU8(Rec, 0xCD);
			if (D.SkillCode < 0) { continue; }
			Defs.Add(D.SkillCode, D);
		}
	}
	else
	{
		UE_LOG(LogAsdaSkillDb, Error, TEXT("[навыки] DT_skill_info не найдена"));
	}

	// 2) Skilltooltip.BIN (504 б): локализованные имя/описание + ★ имя файла иконки (+36).
	//    Строка таблицы именуется индексом записи == skillCode (`uiGetRecordByIndex@0xE52930`).
	if (UDataTable* Dt = IT->GetDataTable(FName(TEXT("skilltooltip"))))
	{
		for (const TPair<FName, uint8*>& It : Dt->GetRowMap())
		{
			const FAsdaSkilltooltipRow* R = reinterpret_cast<const FAsdaSkilltooltipRow*>(It.Value);
			if (!R) { continue; }
			FAsdaSkillDef* D = Defs.Find(R->Id);
			if (!D) { continue; }
			// ★ [ПОРТ] 09.10.2026 (мультиязычность): имя/описание навыка — на языке сессии (оверлей `skilltooltip`, ключи `<_row>.Name` / `<_row>.Text`).
			if (!R->Name.IsEmpty()) { D->Name = FAsdaLang::ResolveRow(Dt, It.Key, TEXT("Name"), R->Name); }
			D->Description = FAsdaLang::ResolveRow(Dt, It.Key, TEXT("Text"), R->Text);
			TArray<uint8> Rec;
			if (AsdaBytes::HexToBytes(R->DataHex, Rec))
			{
				D->IconName   = AsdaBytes::AsciiFieldAt(Rec, 36, 64);
				// +500 i32 — ступень профессии (в этой сборке значения 1..3; тултип → нотис 8427 + N).
				D->JobStepReq = AsdaBytes::SafeI32(Rec, 500);
			}
		}
	}
	else
	{
		UE_LOG(LogAsdaSkillDb, Warning, TEXT("[навыки] DT_skilltooltip не найдена — иконок и описаний не будет"));
	}

	// 3) SkillCoinTable.bin: три числа под иконкой слота.
	// ★ Аудит №3 (Дд4): своего `LoadObject<UDataTable>(DT_SkillCoin)` здесь БОЛЬШЕ НЕТ. Таблицу уже
	//   держит менеджер (`GExtraTables` → `AsdaItemTableSubsystem.cpp:404`, тот же путь ассета) и
	//   строит по ней КЛЮЧЕВОЙ индекс SkillId, поэтому `GetSkillCoin` резолвит запись без обхода
	//   (`BuildKeyIndex`, там же :443). Вторая загрузка означала вторую копию 1255 строк в памяти:
	//   правка таблицы разъезжалась между окном навыков и хранителем души.
	// ⚠ ПОРЯДОК СОХРАНЁН 1:1: прежний цикл шёл ПО СТРОКАМ таблицы и при дублях SkillId оставлял
	//   ПОСЛЕДНЮЮ строку обхода `GetRowMap`; ключевой индекс менеджера строится тем же обходом и с
	//   тем же правилом («при дублях ключа — последняя строка»), поэтому итог совпадает. Навык, у
	//   которого строки монет нет, остаётся с нулями — как и раньше.
	for (TPair<int32, FAsdaSkillDef>& It : Defs)
	{
		FAsdaSkillCoinRow Row;
		if (!IT->GetSkillCoin(It.Key, Row)) { continue; }
		It.Value.Coin0 = Row.Coin0; It.Value.Coin1 = Row.Coin1; It.Value.Coin2 = Row.Coin2;
	}

	// 4) JobType.BIN: 45 записей, id = индекс строки.
	if (UDataTable* Dt = IT->GetDataTable(FName(TEXT("jobtype"))))
	{
		for (const TPair<FName, uint8*>& It : Dt->GetRowMap())
		{
			const FAsdaJobtypeRow* R = reinterpret_cast<const FAsdaJobtypeRow*>(It.Value);
			if (!R) { continue; }
			FAsdaJobDef J;
			J.JobId     = FCString::Atoi(*It.Key.ToString());
			J.JobName   = FAsdaLang::ResolveRow(Dt, It.Key, TEXT("JobName"), R->JobName);       // [ПОРТ] 09.10.2026 (мультиязычность): оверлей `jobtype`
			J.ParentJob = FAsdaLang::ResolveRow(Dt, It.Key, TEXT("ParentJob"), R->ParentJob);   // [ПОРТ] 09.10.2026 (мультиязычность)
			Jobs.Add(J.JobId, J);
		}
	}

	bLoaded = (Defs.Num() > 0);
	UE_LOG(LogAsdaSkillDb, Log, TEXT("[навыки] таблицы: skill_info %d записей, классов %d"), Defs.Num(), Jobs.Num());
	return Defs.Num();
}

FString UAsdaSkillDatabase::GetJobName(int32 JobId) const
{
	if (const FAsdaJobDef* J = Jobs.Find(JobId)) { return J->JobName; }
	return FString::Printf(TEXT("job%d"), JobId);
}

// ── Правила класс → деревья ───────────────────────────────────────────────────────────────────

int32 UAsdaSkillDatabase::BandFromJob(int32 JobId)
{
	if (JobId >= 1  && JobId <= 11) { return 1; }
	if (JobId >= 12 && JobId <= 22) { return 2; }
	if (JobId >= 23 && JobId <= 33) { return 3; }
	return 0;
}

int32 UAsdaSkillDatabase::TreeFromGrade(int32 Grade)
{
	switch (Grade)
	{
	case 1: case 4: case 7:  return 0;
	case 2: case 5: case 8:  return 1;
	case 3: case 6: case 9:  return 2;
	case 10: case 11: case 12: return 3;
	default: return -1;
	}
}

int32 UAsdaSkillDatabase::GradeForBandTree(int32 Band, int32 TreeIdx)
{
	if (Band < 1 || Band > 3 || TreeIdx < 0 || TreeIdx > 3) { return -1; }
	// band 1 → {1,2,3,10}; band 2 → {4,5,6,11}; band 3 → {7,8,9,12}
	return (TreeIdx == 3) ? (9 + Band) : (3 * (Band - 1) + TreeIdx + 1);
}

void UAsdaSkillDatabase::GetTreeSkills(int32 Band, int32 TreeIdx, TArray<int32>& OutCodes) const
{
	OutCodes.Reset();
	const int32 Grade = GradeForBandTree(Band, TreeIdx);
	if (Grade < 0) { return; }
	// Эталон `skillRebuildPageLists@0xE541E0` перебирает id 501..1254 и отбирает по grade полосы.
	for (const TPair<int32, FAsdaSkillDef>& It : Defs)
	{
		if (It.Key < 501 || It.Key > 1254) { continue; }
		const FAsdaSkillDef& D = It.Value;
		if (D.Grade != Grade) { continue; }
		if (!D.HasGridSlot()) { continue; }
		OutCodes.Add(It.Key);
	}
	OutCodes.Sort([this](const int32& A, const int32& B)
	{
		const FAsdaSkillDef* Da = Defs.Find(A);
		const FAsdaSkillDef* Db = Defs.Find(B);
		const int32 Sa = Da ? Da->AbsSlot() : 0;
		const int32 Sb = Db ? Db->AbsSlot() : 0;
		return (Sa != Sb) ? (Sa < Sb) : (A < B);
	});
}

int32 UAsdaSkillDatabase::GetTreeMaxRow(int32 Band, int32 TreeIdx) const
{
	TArray<int32> Codes;
	GetTreeSkills(Band, TreeIdx, Codes);
	int32 MaxRow = 0;
	for (int32 C : Codes)
	{
		if (const FAsdaSkillDef* D = Defs.Find(C)) { MaxRow = FMath::Max(MaxRow, D->SpCostPerTier); }
	}
	return MaxRow;
}

int32 UAsdaSkillDatabase::TreeTabLocId(int32 Band, int32 TreeIdx)
{
	if (Band < 1 || Band > 3 || TreeIdx < 0 || TreeIdx > 2) { return 0; }
	return 9400 + 3 * (Band - 1) + TreeIdx;
}

int32 UAsdaSkillDatabase::Job4TabLocId(int32 Band, int32 SubJobId)
{
	switch (Band)
	{
	case 1: if (SubJobId == 6  || SubJobId == 7)  { return 9422; }
	        if (SubJobId == 4  || SubJobId == 5)  { return 9423; } break;
	case 2: if (SubJobId == 15 || SubJobId == 16) { return 9424; }
	        if (SubJobId == 17 || SubJobId == 18) { return 9425; } break;
	case 3: if (SubJobId == 26 || SubJobId == 27) { return 9426; }
	        if (SubJobId == 28 || SubJobId == 29) { return 9427; } break;
	default: break;
	}
	return 0;
}

// ── Дампы ─────────────────────────────────────────────────────────────────────────────────────

FString UAsdaSkillDatabase::DumpSummary() const
{
	TMap<int32, int32> ByGrade;
	for (const TPair<int32, FAsdaSkillDef>& It : Defs)
	{
		if (It.Key < 501 || It.Key > 1254) { continue; }
		ByGrade.FindOrAdd(It.Value.Grade)++;
	}
	FString S = FString::Printf(TEXT("Навыков в таблице: %d, классов: %d\n"), Defs.Num(), Jobs.Num());
	TArray<int32> Grades; ByGrade.GetKeys(Grades); Grades.Sort();
	for (int32 G : Grades)
	{
		const int32 Tree = TreeFromGrade(G);
		S += FString::Printf(TEXT("  grade %-2d → вкладка %-2d : %d навыков\n"), G, Tree, ByGrade[G]);
	}
	for (int32 Band = 1; Band <= 3; ++Band)
	{
		for (int32 T = 0; T < 4; ++T)
		{
			TArray<int32> Codes; GetTreeSkills(Band, T, Codes);
			S += FString::Printf(TEXT("  полоса %d вкладка %d (grade %d): %d узлов, рядов %d\n"),
				Band, T, GradeForBandTree(Band, T), Codes.Num(), GetTreeMaxRow(Band, T) + 1);
		}
	}
	return S;
}

FString UAsdaSkillDatabase::DumpSkill(int32 SkillCode) const
{
	const FAsdaSkillDef* D = Defs.Find(SkillCode);
	if (!D) { return FString::Printf(TEXT("навык %d в таблице не найден"), SkillCode); }
	return FString::Printf(
		TEXT("навык %d «%s»\n  иконка=%s  вид=%d макс.ур=%d подтип=%d grade=%d (вкладка %d)\n")
		TEXT("  сетка: ряд=%d колонка=%d абс.слот=%d\n")
		TEXT("  кулдаун=%d мс  MP=%d+%d/ур  дальность=%d  AoE=%d  цель=%d  каст=%d\n")
		TEXT("  эффект S1=%d+%d/ур  доп.навыки: %d(ур %d), %d(ур %d)\n")
		// ★ Буфф-слоты «состояние → своя пара величин»: без них не виден дефект оригинала с S1
		//   (см. `UAsdaWorldSubsystem::GetAttackTempoMul`). −1 = слот пуст.
		TEXT("  буфф-слоты: [0]=%d S1=%d+%d/ур  [1]=%d S2=%d+%d/ур  [2]=%d S3=%d+%d/ур\n")
		TEXT("  монеты: %d / %d / %d\n  описание: %s"),
		D->SkillCode, *D->Name, *D->IconName, D->Kind, D->MaxLevel, D->SubType, D->Grade, TreeFromGrade(D->Grade),
		D->SpCostPerTier, D->SpCostBase, D->AbsSlot(),
		D->CooldownMs, D->MpCostBase, D->MpCostPerLevel, D->RangeMeters, D->AoeRadius, D->TargetSlotType, D->CastType,
		D->S1Base, D->S1PerLevel, D->EffectSkillId0, D->EffectLevel0, D->EffectSkillId1, D->EffectLevel1,
		D->Buff0StateId, D->S1Base, D->S1PerLevel,
		D->Buff1StateId, D->S2Base, D->S2PerLevel,
		D->Buff2StateId, D->S3Base, D->S3PerLevel,
		D->Coin0, D->Coin1, D->Coin2, *D->Description);
}
