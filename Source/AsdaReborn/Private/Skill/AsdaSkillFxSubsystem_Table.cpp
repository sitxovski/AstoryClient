// Copyright (c) 2026 @sitxovski (Discord)
// AsdaSkillFxSubsystem_Table.cpp — ★ R3-c 12.11.2026 (разрез AsdaSkillFxSubsystem.cpp, дословный перенос): таблица эффектов навыков: строки, размер цели, масштаб удара, полоса каста, цели.
//   Общее для частей — в AsdaSkillFxSubsystem_Private.h; шапка и реверс — в AsdaSkillFxSubsystem.cpp.
#include "Skill/AsdaSkillFxSubsystem.h"
#include "AsdaNetSubsystem.h"   // ★ R1: прямое включение (раньше приходило транзитивно)

#include "AsdaEffectActor.h"
#include "Engine/DataTable.h"
#include "Engine/World.h"
#include "HAL/IConsoleManager.h"
#include "Data/AsdaMobStatsRow.h"
#include "Npc/AsdaMobActor.h"
#include "Resources/AsdaSoundSubsystem.h"
#include "Skill/AsdaSkillClipRow.h"
#include "UI/AsdaSkillUseSubsystem.h"
#include "World/AsdaWorldSubsystem.h"
#include "World/AsdaCombatTrace.h"   // ★ 18.10.2026: журнал боя — записи урона навыка, клип, кадр удара, снаряд, эффекты
#include "World/AsdaPlayerController.h"    // ★ 29.10.2026 (Charge): сброс маршрута своего персонажа на время рывка
#include "UI/AsdaEffectsSubsystem.h"
#include "AsdaSkillFxSubsystem_Private.h"   // ★ R3-c 12.11.2026: общее для частей AsdaSkillFxSubsystem*.cpp

namespace {
	// Путь к DataTable, собранному Tools/asda_build_skill_clip_dt.py.
	const TCHAR* kTablePath = TEXT("/Game/AsdaAssets/Data/DT_SkillClips.DT_SkillClips");

	// «8|10» / «*» → подходит ли ветка данному подтипу оружия. Пусто = ветка не по оружию (подходит всегда).
	bool WeaponMatch(const FString& Spec, int32 WeaponSubType, bool& bOutIsDefault)
	{
		bOutIsDefault = false;
		if (Spec.IsEmpty()) { return true; }
		TArray<FString> Parts;
		Spec.ParseIntoArray(Parts, TEXT("|"), true);
		for (const FString& P : Parts)
		{
			if (P == TEXT("*")) { bOutIsDefault = true; continue; }
			if (FCString::Atoi(*P) == WeaponSubType) { return true; }
		}
		return false;
	}
}

// ★ Удержание DT_SkillClips на сессию (15.09.2026, dt_gc_hold) — обоснование в объявлении. Логика `EnsureTable` та же.
int32 UAsdaSkillFxSubsystem::HoldGameTables() const
{
	static const FName Group(TEXT("SkillClips"));
	const TCHAR* const Paths[] = { kTablePath };
	return HeldTables.Hold(Group, Paths);
}

void UAsdaSkillFxSubsystem::EnsureTable() const
{
	if (bTableTried && Table.IsValid()) { return; }
	HoldGameTables();   // ★ 15.09 (dt_gc_hold): первый навык — таблица сильно у подсистемы
	if (bTableTried && !Table.IsValid()) { BySkill.Reset(); }   // таблицу собрал GC — перезагрузим
	bTableTried = true;
	UDataTable* T = LoadObject<UDataTable>(nullptr, kTablePath, nullptr, LOAD_NoWarn | LOAD_Quiet);
	Table = T;
	BySkill.Reset();
	if (!T)
	{
		UE_LOG(LogAsdaSkillFx, Warning,
			TEXT("[навыки-визуал] DT_SkillClips не найден — запусти Tools/asda_dump_skill_dispatch.py "
			     "и Tools/asda_build_skill_clip_dt.py"));
		return;
	}
	for (const TPair<FName, uint8*>& It : T->GetRowMap())
	{
		const FAsdaSkillClipRow* R = reinterpret_cast<const FAsdaSkillClipRow*>(It.Value);
		if (R) { BySkill.FindOrAdd(R->SkillId).Add(R); }
	}
	UE_LOG(LogAsdaSkillFx, Log, TEXT("[навыки-визуал] DT_SkillClips: %d строк, %d навыков"),
		T->GetRowMap().Num(), BySkill.Num());
}

void UAsdaSkillFxSubsystem::CollectRows(int32 SkillId, TArray<const FAsdaSkillClipRow*>& Out) const
{
	EnsureTable();
	if (const TArray<const FAsdaSkillClipRow*>* Rows = BySkill.Find(SkillId)) { Out = *Rows; }
}

// ★★★ РАЗМЕРНЫЕ ПОЛЯ ТИПА МОБА (`DT_MobStats`): `SizeClass` (`+0x90` → сущность `+7356`) и
//   `SizePercent` (`+0x108` → сущность `+7360`).
// ★ Аудит №3 (Дд1): своя загрузка таблицы и своя карта `MobSizeByMobId` УБРАНЫ — ровно те же строки
//   уже держит `AAsdaMobActor::FindMobStatsRow` (уровень/агрессия/полу-размер плашки). Была вторая
//   копия одних и тех же чисел в памяти: правка данных разъезжалась между двумя читателями.
int32 UAsdaSkillFxSubsystem::GetTargetSizeClass(const AActor* Target) const
{
	// Консольный оверрайд для живой проверки всех трёх веток — приоритетнее данных.
	if (TargetSizeOverride >= 0) { return FMath::Clamp(TargetSizeOverride, 0, 2); }
	// ★★★ ИСТОЧНИК НАЙДЕН (2026-08-11). Прежний комментарий «писателя `+7356` в срезе нет» ОПРОВЕРГНУТ
	//   сканом `.text` по disp32 `0x1CBC`: пишут ровно две точки —
	//     · `gameObjCtorInit@0xF810B0+0x280` (VA 0xF81330) `mov dword [obj+0x1CBC], 0` — НЕ-моб ⇒ 0;
	//     · `mobEffect_LoadArchiveStep@0xF81B90+0xEEF` (VA 0xF82A7F) — результат `sub_F82F10`
	//       (`entity_char.c:68677`: `*(*(this+3) + 272·typeIdx + 144)` = `Mob list.BIN +0x90`).
	//   ⇒ у персонажей/NPC класс 0, у моба — поле его ТИПА. Ровно это и делаем.
	const AAsdaMobActor* M = Cast<AAsdaMobActor>(Target);
	if (!M || M->MobId <= 0) { return 0; }
	// ★ Аудит №3 (Дд1): единая точка чтения DT_MobStats.
	if (const FAsdaMobStatsRow* R = AAsdaMobActor::FindMobStatsRow(M->MobId)) { return FMath::Clamp(R->SizeClass, 0, 2); }
	return 0;
}

int32 UAsdaSkillFxSubsystem::GetTargetSizePercent(const AActor* Target)
{
	// ★★★ `get_o730@0x99F150` = `return *(this + 1840)` (байт +7360). Конструктор кладёт 100
	//   (VA 0xF8133D), загрузчик моба — `Mob list.BIN +0x108` (VA 0xF82A9C → `sub_F82F50`).
	//   ⇒ у персонажей/NPC 100 (множитель 1.0), у мобов 50…500.
	const AAsdaMobActor* M = Cast<AAsdaMobActor>(Target);
	if (!M || M->MobId <= 0) { return 100; }
	// ★ Аудит №3 (Дд1): единая точка чтения DT_MobStats.
	if (const FAsdaMobStatsRow* R = AAsdaMobActor::FindMobStatsRow(M->MobId)) { return (R->SizePercent > 0) ? R->SizePercent : 100; }
	return 100;
}

namespace {
// ══ ★★★ ЗАКОН МАСШТАБА ЭФФЕКТА УДАРА ЦЕЛИКОМ (перенос 2026-08-14) ═══════════════════════════════
// Сплошная опись ВСЕХ вызовов `CEffectMgr_scaleAll` в эталонном срезе — 29 сайтов:
//   · 17 переменных  — `sub_936A29(цель)/100`, все до одного с целью-СУЩНОСТЬЮ (мобом):
//     `combat_pvp.c:2149/2253/2267/2492/2589/2601`, `effects_particles.c:5548/10169/11157`,
//     `entity_char.c:936/1008/1020`, `skill_buff.c:549/688/699/16771`;
//   · 10 констант 1.5 — ОБЫЧНАЯ АТАКА по цели-ПЕРСОНАЖУ (перечислены у `kCharHitFxScaleBasic`);
//   · 2 константы 2.0 — эффект удара НАВЫКА по цели-ПЕРСОНАЖУ (`effects_particles.c:11389`,
//     `skill_buff.c:17066`).
// ⇒ Множитель ВЫБИРАЕТСЯ по виду цели, а НЕ перемножается: в обеих функциях с константой 2.0
//   ветки взаимоисключающие по цели (`if (pTargetEntity) {…%/100…}` / `if (pTargetChar) {…2.0…}`),
//   и объекты эффекта там РАЗНЫЕ (`Effect_F561F0` против `pEffectChar`) — ни один эффект не
//   получает двух `scaleAll`. Поэтому `%/100 × 2.0` быть не может (проверено чтением, не догадкой).
// ⚠ Один сайт вне закона: удар МОБА по игроку (`Combat_ApplyDamage@0xF7C470`, `eff_0_mon_99_battk01`)
//   спавнится БЕЗ `scaleAll` ⇒ 1.0. В порту это `ShowPlayerHit` — его не трогаем.
// ОТКАТ: `asda.Fx.EtalonCharHitScale 0` — вернуть прежнее поведение (у целей-персонажей 1.0).
	int32 GEtalonCharHitScale = 1;
	FAutoConsoleVariableRef GEtalonCharHitScaleRef(
		TEXT("asda.Fx.EtalonCharHitScale"),
		GEtalonCharHitScale,
		TEXT("Эффект удара по цели-ПЕРСОНАЖУ: 1 = эталонные константы (навык 2.0, обычная атака 1.5), ")
		TEXT("0 = прежнее 1.0. На цели-мобов не влияет (там всегда %размера/100)."));
}

// Общая половина закона: у моба множитель = процент размера / 100, у персонажа — переданная константа.
static float AsdaHitFxScale(const AActor* Target, float CharConst)
{
	if (Target && Target->IsA(AAsdaMobActor::StaticClass()))
	{
		return (float)UAsdaSkillFxSubsystem::GetTargetSizePercent(Target) / 100.f;
	}
	// Цель — персонаж (или цель неизвестна: у эталона тогда работает та же char-ветка спавна).
	return (GEtalonCharHitScale != 0) ? CharConst : 1.f;
}

float UAsdaSkillFxSubsystem::GetSkillHitFxScale(const AActor* Target)
{
	return AsdaHitFxScale(Target, kCharHitFxScaleSkill);
}

float UAsdaSkillFxSubsystem::GetBasicAttackHitFxScale(const AActor* Target)
{
	return AsdaHitFxScale(Target, kCharHitFxScaleBasic);
}

const FAsdaSkillClipRow* UAsdaSkillFxSubsystem::ResolveRow(int32 SkillId, int32 Sex, int32 WeaponSubType,
	FString* OutWhy, int32 TargetSizeClass) const
{
	EnsureTable();
	const TArray<const FAsdaSkillClipRow*>* Rows = BySkill.Find(SkillId);
	if (!Rows || Rows->Num() == 0)
	{
		if (OutWhy) { *OutWhy = TEXT("строк нет: навык не проходит через хардкод-загрузчик"); }
		return nullptr;
	}
	const FString WantG = (Sex == 2) ? TEXT("f") : TEXT("m");
	// ★★★ НЕ-ОРУЖЕЙНОЕ УСЛОВИЕ: РАЗМЕРНЫЙ КЛАСС ЦЕЛИ (`сущность + 7356`, значения 0/1/2).
	//   Реализовано ровно как в `SkillEffect_SetupHexa@0xAA4A40`: если у навыка вообще есть строки
	//   с непустым `TargetSize`, ветка выбирается ПО КЛАССУ ЦЕЛИ, и только внутри неё — по оружию.
	const FString WantSize = FString::FromInt(FMath::Clamp(TargetSizeClass, 0, 2));
	bool bHasSizeBranches = false;
	for (const FAsdaSkillClipRow* R : *Rows)
	{
		if (R && !R->TargetSize.IsEmpty()) { bHasSizeBranches = true; break; }
	}
	const FAsdaSkillClipRow* Exact = nullptr;
	const FAsdaSkillClipRow* Def = nullptr;
	const FAsdaSkillClipRow* AnyGender = nullptr;
	for (const FAsdaSkillClipRow* R : *Rows)
	{
		if (!R || (!R->Gender.IsEmpty() && R->Gender != WantG)) { continue; }
		// Строка с ЯВНЫМ размерным классом годится только своему классу; строка без класса (её
		// эталон пишет в ветке «цель не сущность») остаётся запасной.
		if (bHasSizeBranches && !R->TargetSize.IsEmpty() && R->TargetSize != WantSize) { continue; }
		if (!AnyGender) { AnyGender = R; }
		bool bIsDefault = false;
		if (WeaponMatch(R->WeaponTypes, WeaponSubType, bIsDefault))
		{
			if (!bIsDefault) { Exact = R; break; }
		}
		if (bIsDefault && !Def) { Def = R; }
	}
	const FAsdaSkillClipRow* Pick = Exact ? Exact : (Def ? Def : AnyGender);
	if (OutWhy)
	{
		*OutWhy = Exact ? TEXT("точное совпадение по подтипу оружия")
			: (Def ? TEXT("ветка default по оружию") : TEXT("первая строка нужного пола"));
		if (bHasSizeBranches)
		{
			*OutWhy += FString::Printf(TEXT("; размерный класс цели %s (поле сущности +7356)"), *WantSize);
		}
		// ★ Честно: остальные не-оружейные условия (не размер цели) в порту НЕ разобраны — сырой
		//   текст условия эталона печатаем прямым текстом, чтобы это было видно, а не пряталось.
		if (Pick && !Pick->Cond.IsEmpty() && Pick->WeaponTypes.IsEmpty() && Pick->TargetSize.IsEmpty())
		{
			*OutWhy += FString::Printf(TEXT("; ★ ветка имеет НЕ-оружейное условие эталона: %s"), *Pick->Cond);
		}
	}
	return Pick;
}

bool UAsdaSkillFxSubsystem::TryGetCastBarMaxMs(int32 SkillId, int32 Sex, int32 WeaponSubType,
	float& OutMaxMs) const
{
	const FAsdaSkillClipRow* R = ResolveRow(SkillId, Sex, WeaponSubType);
	if (!R)
	{
		OutMaxMs = 0.f;
		return false;                      // строки нет — величина НЕИЗВЕСТНА
	}
	// Строка есть, но COMBO0 == 0 — это ЗНАЧАЩИЙ ноль (174 строки из 1664: 69 клипов без записи в
	//   `combo time.bin` + 105 с hit[0] == 0). Эталон в этом случае подставляет 1.0 —
	//   см. `AsdaCastBarSubsystem::ApplyValue`, там же и точная единица измерения.
	OutMaxMs = R->Combo0Ms;
	return true;
}

float UAsdaSkillFxSubsystem::GetCastBarMaxMs(int32 SkillId, int32 Sex, int32 WeaponSubType) const
{
	float Max = 0.f;
	TryGetCastBarMaxMs(SkillId, Sex, WeaponSubType, Max);
	return Max;
}

// ═══ ★★★ ПРОГРАММА ПОКАЗА: РАЗБОР, ЦЕЛЬ, ФАЗЫ ══════════════════════════════════════════════════
void UAsdaSkillFxSubsystem::ParseFxList(const FString& Cell, TArray<FAsdaSkillFxSpawn>& Out)
{
	Out.Reset();
	if (Cell.IsEmpty()) { return; }
	TArray<FString> Items;
	Cell.ParseIntoArray(Items, TEXT(";"), true);
	for (const FString& ItRaw : Items)
	{
		// ★ 10.11.2026 (job4-vfx): суффикс «~rec» — эффект отложенной записи (FAsdaSkillFxSpawn::bRecord).
		FString It = ItRaw;
		const bool bRec = It.RemoveFromEnd(TEXT("~rec"));
		FString Name = It, Tail;
		if (!It.Split(TEXT("@"), &Name, &Tail)) { Tail.Reset(); }
		FString Anchor = Tail, Kind;
		if (!Tail.Split(TEXT("/"), &Anchor, &Kind)) { Kind.Reset(); }
		// ★★★ Привязка и ТОЧКА РОЖДЕНИЯ — две разные оси, разделены двоеточием («world:target»).
		FString AnchorOnly = Anchor, Origin;
		if (!Anchor.Split(TEXT(":"), &AnchorOnly, &Origin)) { Origin.Reset(); }
		FAsdaSkillFxSpawn S;
		S.Jeb = FPaths::GetBaseFilename(Name).ToLower();
		S.Anchor = AnchorOnly.IsEmpty() ? FName(TEXT("world")) : FName(*AnchorOnly);
		S.Origin = Origin.IsEmpty() ? NAME_None : FName(*Origin);
		S.TargetKind = Kind.IsEmpty() ? NAME_None : FName(*Kind);
		S.bRecord = bRec;
		// ★★★ Закон [[jeb-emitter-routing-hole]] (2026-08-10): НЕИЗВЕСТНЫЙ вид привязки обязан
		//   КРИЧАТЬ, а не тихо ехать по ветке «свободный эффект». Полный список снят с колонок
		//   таблицы (`skill_clip_table.tsv`, 25 сочетаний): world / bone / matrix / attach /
		//   follow / char / projectile — других в данных нет. Если разбор бинаря когда-нибудь
		//   добавит восьмой — он будет виден в логе первым же кастом, а не через полгода.
		static const FName KnownAnchors[] = {
			FName(TEXT("world")), FName(TEXT("bone")), FName(TEXT("matrix")), FName(TEXT("attach")),
			FName(TEXT("follow")), FName(TEXT("char")), FName(TEXT("projectile")),
			FName(TEXT("track")) };   // ★ 04.11.2026: attach-режимы эталона 3/9/12/13/14/15 — эффект едет за целью (см. SpawnFx)
		bool bKnownAnchor = false;
		for (const FName& K : KnownAnchors) { if (S.Anchor == K) { bKnownAnchor = true; break; } }
		if (!bKnownAnchor)
		{
			UE_LOG(LogTemp, Warning,
				TEXT("[навыки-визуал] НЕИЗВЕСТНАЯ привязка «%s» в записи «%s» — эффект поедет по "
					 "ветке по умолчанию; проверь Tools/asda_dump_skill_dispatch.py"),
				*S.Anchor.ToString(), *It);
		}
		if (!S.Jeb.IsEmpty()) { Out.Add(MoveTemp(S)); }
	}
}

AActor* UAsdaSkillFxSubsystem::ResolveTarget(bool& bOutIsMob) const
{
	bOutIsMob = false;
	UGameInstance* GI = GetGameInstance();
	if (!GI) { return nullptr; }
	// ★ ТОЛЬКО ЧТЕНИЕ чужой подсистемы: `GetSelectedTarget` — порт эталонного индекса выбранной цели
	//   `g_nTargetUserIdx` / `LocalPlayer->targetRenderId` (+0x32DFC) (★ 27.09.2026: `Target_GetSelectedUserIdx` — миснейм IDA, = `Char_GetHoveredUserIdx@0x966490`, возвращает userNum ПЕРЕДАННОГО персонажа), тот самый индекс, который эталон кладёт в слот при
	//   применении и по которому тик потом ищет цель (`Entity_FindByRenderId` / `FindByUserIdx`).
	const UAsdaSkillUseSubsystem* Use = GI->GetSubsystem<UAsdaSkillUseSubsystem>();
	UAsdaWorldSubsystem* WS = GI->GetSubsystem<UAsdaWorldSubsystem>();
	if (!Use || !WS) { return nullptr; }
	int32 TType = 0, TIdx = -1;
	FVector2D TPos = FVector2D::ZeroVector;
	float TDist = 0.f;
	if (!Use->GetSelectedTarget(TType, TIdx, TPos, TDist) || TIdx < 0) { return nullptr; }
	// ★ Аудит №3 (B6): развилка «(вид, индекс) → актор» — через единую точку `ActorByTarget`.
	//   ⚠ ЗДЕСЬ СВОЯ КОНВЕНЦИЯ, НЕ 29-байтная запись: `GetSelectedTarget` отдаёт **0 = МОБ**
	//   (индекс = `NetRenderId`), **1 = ПЕРСОНАЖ** (индекс = userNum) — см. его тело и поле
	//   `ApproachTargetType`. Эталонная глобаль того же смысла (`g_nInputControlState`) ИНВЕРТИРОВАНА
	//   (1 = сущность, skill_buff.c:31652; 0 = персонаж, skill_buff.c:31789), поэтому байт сюда не
	//   передаём, а переводим в вид ПРЯМО ЗДЕСЬ, по контракту именно этого источника.
	const EAsdaTargetKind Kind = (TType == 1) ? EAsdaTargetKind::Character : EAsdaTargetKind::Entity;
	bOutIsMob = (Kind == EAsdaTargetKind::Entity);
	return WS->ActorByTarget(Kind, TIdx);
}

// ★★★ ЦЕЛЬ ПОКАЗА ИЗ ЗАПИСЕЙ ПАКЕТА (объявление и доказательство — в заголовке).
//   Берём ПЕРВУЮ запись батча, ровно как эталон берёт `targetWords[0]`.
bool UAsdaSkillFxSubsystem::FindCastRecTarget(int32 CasterUserIdx, int32 SkillId,
	int32& OutKind, int32& OutIdx) const
{
	const FPendingCastHits* Best = nullptr;
	for (const FPendingCastHits& P : PendingCastHits)
	{
		if (P.CasterUserIdx != CasterUserIdx) { continue; }
		// Совпадение кода навыка — ПРЕДПОЧТЕНИЕ, а не условие: у одного персонажа одновременно идёт
		//   ровно один каст (эталон: единственный слот `effectSlots[100].field_4`).
		if (SkillId >= 0 && P.SkillId == SkillId) { Best = &P; break; }
		if (!Best) { Best = &P; }
	}
	if (!Best) { return false; }
	for (const UAsdaNetSubsystem::FAsdaSkillHitRec& H : Best->Hits)
	{
		if (H.TargetIdx < 0) { continue; }
		OutKind = H.TargetKind;
		OutIdx  = H.TargetIdx;
		return true;
	}
	return false;
}
