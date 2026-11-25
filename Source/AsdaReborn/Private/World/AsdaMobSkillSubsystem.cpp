// Copyright (c) 2026 @sitxovski (Discord)
// UAsdaMobSkillSubsystem — навыки мобов (8011/8012/8013). Разбор потока: `Reports/mob_skills/FLOW.md`.
//
// ★★★ ЧТО ДЕЛАЕТ ОРИГИНАЛ (эталон, построчно; адреса — клиент Interlude 13 344 768 б):
//   8011 (network.c:38487) `netApplyCharActionState`: моб по renderId @5 → ent[1768]=0 → `gameSpawnTimedSlotEntry(навык @0,
//        вид цели @2, цель @3)` → ent[1749] = цель → SetActionState(17). (Наш сервер шлёт @2=0, @3=−1 — цель приходит в 8012.)
//   8012 (network.c:38507) `NetHandler_CharActionUpdate`: шапка 9 б + до 20 записей × 29 б; err≠0 — ничего; навык 1 — пресет
//        «пробуждение Кайи»; навык 0 или моба нет — ничего; иначе цели (`partyParseMemberList@0xF8E270`), переход по голове
//        очереди (2 → 15, 1 → 16, иначе очистка и 17), затем новая запись в хвост.
//   8013 (entity_char.c:52580) `Entity_ApplyHpRestore`: HP моба + эффект зелья.
//   Тики 17/16/15 и снаряд — у соответствующих функций ниже. Урон по игроку оригинал здесь НЕ списывает: HP жертвы приходит
//   отдельным 4058 (сервер, `server_side.md` §1) — порт его уже применяет.
//
// ★★★ ИСПРАВЛЕНИЯ ДЕФЕКТОВ ОРИГИНАЛА (ключи, 0 = как у оригинала):
//   asda.MobSkill.HitFxFix — эффект удара (фаза 4) у оригинала не появляется почти никогда:
//     (а) у снаряда режим точки удара читается из +356 ПОСЛЕ его обнуления (дизасм 0xF7F30F `mov [e+0x164],0`, затем 0xF7F4D2
//         `movzx ecx,[e+0x164]` → аргумент) ⇒ режим всегда 0 ⇒ `switch` в `CharSkill_UpdateHitDamageFx` не спавнит ничего;
//     (б) одиночная ветка (нет списка целей или есть снаряд) обнуляет +356 на ПЕРВОМ тике, если t_anim < B
//         (`jp 0xF7FCB2` → `mov [e+0x164],0`), а t_anim в состоянии 15 начинается с 0 ⇒ эффект на цели не играет.
//     Исправление: режим берётся до обнуления; одиночная ветка ждёт t_anim ≥ B, а у навыка со снарядом удар оставлен снаряду.
//   asda.MobSkill.ChatFix — ветка площади пишет в чат: блок (флаг 8) — ГОЛОЕ число; DODGE/IMMUNE — буфер прошлой строки
//     (`CChat_AddTokenizedLines(Buffer)` без `sprintf` перед ним). Исправление — строки эталонного снаряда: 20260 (блок, имя+урон),
//     20261 (уклон/неуязвимость, имя).
//   asda.MobSkill.ObserverChatFix — ветка площади пишет строки «… наносит ТЕБЕ урон» и наблюдателю (гейт `жертва-я || режим 1`
//     общий для цифры и чата). Исправление — строка чата только жертве (как у снаряда).
#include "World/AsdaMobSkillSubsystem.h"
#include "AsdaPerf.h"   // R4, доля a: области замера времени игрового потока
#include "AsdaFxClassCache.h"   // ★ R4, доля a: предзагрузка классов эффектов навыка моба
#include "World/AsdaCombatChat.h"   // ★ R5: AsdaCombatChat::Line — строка боя в системный чат (одна копия)
#include "AsdaNetSubsystem.h"
#include "AsdaBytes.h"
#include "AsdaDataTableCache.h"
#include "AsdaEffectActor.h"
#include "Data/AsdaEffectMobTableRows.h"
#include "Npc/AsdaMobActor.h"
#include "Text/AsdaTextSubsystem.h"
#include "UI/AsdaChatSubsystem.h"
#include "UI/AsdaEffectsSubsystem.h"
#include "World/AsdaBlendAnimInstance.h"     // AsdaAnimSmooth::StartHitFlinch — вздрагивание жертвы
#include "World/AsdaCombatTrace.h"
#include "World/AsdaCoord.h"
#include "World/AsdaFloatingTextSubsystem.h"
#include "World/AsdaNameplateComponent.h"
#include "World/AsdaPlayerCharacter.h"
#include "World/AsdaProjectileActor.h"
#include "World/AsdaWorldSubsystem.h"
#include "Components/SkeletalMeshComponent.h"
#include "Engine/DataTable.h"
#include "Engine/GameInstance.h"
#include "Engine/World.h"
#include "HAL/IConsoleManager.h"

DEFINE_LOG_CATEGORY_STATIC(LogAsdaMobSkill, Log, All);

// Таблицы клиента байт в байт (генератор `Tools/gen_mob_skill_tables.py`, md5 источников — в шапке файла).
#include "AsdaMobSkillTables.inl"

static int32 GAsdaMobSkill = 1;
static FAutoConsoleVariableRef CVarAsdaMobSkill(TEXT("asda.MobSkill"), GAsdaMobSkill,
	TEXT("1 — навыки мобов по оригиналу: 8011/8012 (анимация, эффекты, звук, снаряд, цифры, строки чата, вздрагивание), ")
	TEXT("8013 (лечение моба) [ОРИГИНАЛ network.c:1224/1259/1263]; 0 — пакеты только в журнал (порт до 30.10.2026)."), ECVF_Default);
static int32 GAsdaMobSkillHitFxFix = 1;
static FAutoConsoleVariableRef CVarAsdaMobSkillHitFxFix(TEXT("asda.MobSkill.HitFxFix"), GAsdaMobSkillHitFxFix,
	TEXT("1 — [ИСПРАВЛЕНИЕ] эффект удара навыка моба играет (у снаряда режим точки берётся до обнуления, одиночная ветка ждёт ")
	TEXT("задержку удара); 0 — как оригинал (эффект удара у снаряда и одиночной ветки не появляется)."), ECVF_Default);
static int32 GAsdaMobSkillChatFix = 1;
static FAutoConsoleVariableRef CVarAsdaMobSkillChatFix(TEXT("asda.MobSkill.ChatFix"), GAsdaMobSkillChatFix,
	TEXT("1 — [ИСПРАВЛЕНИЕ] строки чата площадного навыка моба: блок — 20260, уклон/неуязвимость — 20261; 0 — как оригинал ")
	TEXT("(блок — голое число, уклон/неуязвимость — без строки: у оригинала там мусор прошлой строки)."), ECVF_Default);
static int32 GAsdaMobSkillObserverChatFix = 1;
static FAutoConsoleVariableRef CVarAsdaMobSkillObserverChatFix(TEXT("asda.MobSkill.ObserverChatFix"), GAsdaMobSkillObserverChatFix,
	TEXT("1 — [ИСПРАВЛЕНИЕ] строка «навык … наносит тебе урон» площадного навыка моба — только жертве; 0 — как оригинал ")
	TEXT("(и наблюдателю в режиме показа цифр «все»)."), ECVF_Default);

namespace AsdaMobSkillLocal
{
	constexpr const TCHAR* kEffectTablePath = TEXT("/Game/AsdaAssets/Data/DT_MonsterSkillEffect.DT_MonsterSkillEffect");
	constexpr int32 kMaxTargets = 20;      // `for (memberIdx < 20)` network.c:38542
	constexpr int32 kRecSize = 29;         // запись цели 0x1D
	constexpr int32 kMaxFlights = 3;       // `for (targetSlot < 3)` entity_char.c:55869
	constexpr float kFlightSpeed = 2.5f;   // юнита/мс — `casterTargetDist / 2.5` skill_buff.c:10790
	// Цвета строк чата эталона (D3DCOLOR ARGB): −9053441 = 0xFF75DAFF, −33517 = 0xFFFF7D13, −1 = белый.
	const FColor kColCrit(0x75, 0xDA, 0xFF, 0xFF);
	const FColor kColHit(0xFF, 0x7D, 0x13, 0xFF);
	const FColor kColWhite(0xFF, 0xFF, 0xFF, 0xFF);

	// `sub_A526D0` (миснейм CMobManager_GetMobInfo): запись msif годна при 0 < id < count.
	bool SkillInfoValid(int32 Skill) { return Skill > 0 && Skill < kAsdaMobSkillInfoCount; }
	// msif +43 & 1 — выбор слота атаки (21 − b) и звука (14 − b).
	int32 SkillAnimBit(int32 Skill) { return SkillInfoValid(Skill) ? (kAsdaMobSkillAnim[Skill] & 1) : 0; }
	// `sub_F80690`: запись mst годна при 0 < ent[117] < count; ent[117] = MobId − 1 (`mobTableFindIndexById@0xF707D0`).
	bool CondValid(int32 TypeIdx) { return TypeIdx > 0 && TypeIdx < kAsdaMobSkillCondCount; }
	int32 CondExtraMs(int32 TypeIdx)
	{
		for (const auto& P : kAsdaMobSkillCondExtra) { if (P[0] == TypeIdx) { return P[1]; } }
		return 0;
	}
	FString Hex(const uint8* P, int32 N)
	{
		FString S; for (int32 i = 0; i < N; ++i) { S += FString::Printf(TEXT("%02X"), P[i]); } return S;
	}
}
using namespace AsdaMobSkillLocal;

// ═══ Справка ════════════════════════════════════════════════════════════════════════════════════════════════════════

int32 UAsdaMobSkillSubsystem::SlotMaskForType(int32 MobType)
{
	const int32 TypeIdx = MobType - 1;   // ent[117]
	for (const auto& P : kAsdaMobSkillSlotMask) { if (P[0] == TypeIdx) { return P[1]; } }
	return 0;
}

int32 UAsdaMobSkillSubsystem::ClipForSlot(int32 Slot)
{
	// `Effect_BuildJbbSuffixByType@0xF717C0` (таблица 0xF71974): 18 → _11, 19 → _12, 20 → _13, 21 → _14.
	return (Slot >= 18 && Slot <= 21) ? Slot - 7 : -1;
}

int32 UAsdaMobSkillSubsystem::DebugState(int32 Rid) const
{
	const FMobRt* R = Mobs.Find(Rid);
	return R ? R->State : 0;
}

int32 UAsdaMobSkillSubsystem::DebugQueueLen(int32 Rid) const
{
	const FMobRt* R = Mobs.Find(Rid);
	return R ? R->Queue.Num() : 0;
}

int32 UAsdaMobSkillSubsystem::DebugFlights(int32 Rid) const
{
	const FMobRt* R = Mobs.Find(Rid);
	return R ? R->Flights.Num() : 0;
}

void UAsdaMobSkillSubsystem::Event(const TCHAR* Kind, int32 Rid, const FString& Detail)
{
	if (Events.Num() >= 256) { Events.RemoveAt(0, Events.Num() - 255); }
	Events.Add(FString::Printf(TEXT("%s|%d|%s"), Kind, Rid, *Detail));
	UE_LOG(LogAsdaMobSkill, Log, TEXT("[навык моба] %s rid=%d %s"), Kind, Rid, *Detail);
}

const UDataTable* UAsdaMobSkillSubsystem::EffectTable() const
{
	return AsdaDt::Cached(kEffectTablePath);
}

AAsdaMobActor* UAsdaMobSkillSubsystem::FindMob(int32 Rid) const
{
	const UAsdaWorldSubsystem* WS = GetGameInstance() ? GetGameInstance()->GetSubsystem<UAsdaWorldSubsystem>() : nullptr;
	return WS ? WS->FindNetMobByRid(Rid) : nullptr;
}

AActor* UAsdaMobSkillSubsystem::FindLivePlayer(int32 UserIdx) const
{
	// `CharManager_FindByUserIdx` + `(sub_93B218(char) & 1) == 0` — живой персонаж.
	const UAsdaWorldSubsystem* WS = GetGameInstance() ? GetGameInstance()->GetSubsystem<UAsdaWorldSubsystem>() : nullptr;
	AActor* A = WS ? WS->ActorByTarget(EAsdaTargetKind::Character, UserIdx) : nullptr;
	const AAsdaPlayerCharacter* Ch = Cast<AAsdaPlayerCharacter>(A);
	return (Ch && !Ch->IsIncapacitated()) ? A : nullptr;
}

bool UAsdaMobSkillSubsystem::IsSelf(int32 UserIdx) const
{
	const UAsdaNetSubsystem* Net = GetGameInstance() ? GetGameInstance()->GetSubsystem<UAsdaNetSubsystem>() : nullptr;
	return Net && Net->IsSelfUserIdx(UserIdx);
}

// ═══ Жизненный цикл ════════════════════════════════════════════════════════════════════════════════════════════════

void UAsdaMobSkillSubsystem::Initialize(FSubsystemCollectionBase& Collection)
{
	Super::Initialize(Collection);
}

void UAsdaMobSkillSubsystem::Deinitialize()
{
	ResetSessionState(EAsdaSessionResetReason::Disconnected);
	Super::Deinitialize();
}

void UAsdaMobSkillSubsystem::ResetSessionState(EAsdaSessionResetReason Reason)
{
	// Очереди навыков и снаряды привязаны к мобам мира: любой сброс (разрыв, выход, смена карты) их обнуляет.
	for (TPair<int32, FMobRt>& P : Mobs)
	{
		if (AActor* Fx = P.Value.TrackedFx.Get()) { Fx->Destroy(); }
		for (FFlight& F : P.Value.Flights) { if (AAsdaProjectileActor* Pr = F.Proj.Get()) { Pr->Destroy(); } }
	}
	Mobs.Reset();
	UE_LOG(LogAsdaMobSkill, Verbose, TEXT("[навык моба] сброс сессии (%s)"), AsdaSessionResetReasonName(Reason));
}

// ═══ Клон записи и очередь (`gameSpawnTimedSlotEntry@0xF8C100`, `skillCloneEntryByIndex@0xF76900`) ═══════════════════

bool UAsdaMobSkillSubsystem::CloneEntry(int32 Skill, int32 MobType, FEntry& Out) const
{
	const int32 TypeIdx = MobType - 1;   // ent[117]
	if (!CondValid(TypeIdx)) { return false; }               // `if (!v10) return 0` — записи mst нет
	if (!SkillInfoValid(Skill)) { return false; }           // `if (!MobInfo) return 0`
	const FAsdaMonsterSkillEffectRow* Row = AsdaDt::RowByInt<FAsdaMonsterSkillEffectRow>(kEffectTablePath, Skill, TEXT("AsdaMobSkill"));
	if (!Row) { return false; }                               // `if (a2 >= *this) return 0`
	// Поля структуры порта названы по прежнему разбору: RangeA/B — задержки, SlotType — флаги фаз, SlotParam — привязка.
	const FString* J[5] = { &Row->Jeb0, &Row->Jeb1, &Row->Jeb2, &Row->Jeb3, &Row->Jeb4 };
	const FString* Bn[5] = { &Row->Bone0, &Row->Bone1, &Row->Bone2, &Row->Bone3, &Row->Bone4 };
	const int32 Fl[5] = { Row->SlotType0, Row->SlotType1, Row->SlotType2, Row->SlotType3, Row->SlotType4 };
	const int32 At[5] = { Row->SlotParam0, Row->SlotParam1, Row->SlotParam2, Row->SlotParam3, Row->SlotParam4 };
	Out = FEntry();
	Out.Skill = Skill;
	for (int32 k = 0; k < 5; ++k)
	{
		Out.Jeb[k] = *J[k];
		Out.Bone[k] = *Bn[k];
		Out.Flag[k] = (uint8)Fl[k];
		Out.Attach[k] = At[k];
	}
	// ★ R4, доля a (15.11.2026) [ПОРТ, производительность]: классы эффектов записи навыка моба (слоты 1–4: каст, атака, снаряд, удар)
	//   запрашиваются асинхронно при её создании (приём 8011/8012) — к своим фазам они уже в памяти. Слот 0 (пре-каст) рождается
	//   сразу и грузится синхронно, как раньше. Показ идёт прежним кодом.
	AsdaFxClassCache::PreloadJebs(MakeArrayView(Out.Jeb + 1, 4));
	Out.DelayA = Row->RangeA;
	Out.DelayB = Row->RangeB;
	Out.bProjectile = Out.Flag[3] != 0;                       // `if (*(v3+355)) *(v3+408) = 1`
	Out.TimeoutMs = (float)(CondExtraMs(TypeIdx) + 5000);     // `+388 = (float)(mst+6 + 5000)`
	return true;
}

bool UAsdaMobSkillSubsystem::SpawnEntry(AAsdaMobActor* M, FMobRt& R, int32 Skill, int32 TargetType, int32 TargetIdx)
{
	FEntry E;
	if (!M || !CloneEntry(Skill, M->MobId, E))
	{
		Event(TEXT("НЕТ_ЗАПИСИ"), M ? M->NetRenderId : -1, FString::Printf(TEXT("навык %d тип %d — записи нет (как эталон: в очередь не кладётся)"),
			Skill, M ? M->MobId : -1));
		return false;
	}
	E.TargetType = TargetType;
	E.TargetIdx = TargetIdx;
	R.Queue.Add(MoveTemp(E));
	Event(TEXT("ОЧЕРЕДЬ"), M->NetRenderId, FString::Printf(TEXT("навык %d цель %d/%d, в очереди %d, таймаут %.0f мс"),
		Skill, TargetType, TargetIdx, R.Queue.Num(), R.Queue.Last().TimeoutMs));
	return true;
}

void UAsdaMobSkillSubsystem::SetState(AAsdaMobActor* M, FMobRt& R, int32 State)
{
	// `CCharacter_SetActionState@0xF876D0`: эффект фазы ent[4672] гасится при любой смене; время клипа ent[113] = 0.
	if (AActor* Fx = R.TrackedFx.Get()) { Fx->Destroy(); }
	R.TrackedFx.Reset();
	R.AnimMs = 0.f;
	R.ClipSlot = -1;
	R.ClipLenMs = 0.f;
	if (State == 6)
	{
		R.State = 0;
		if (M) { M->SetSkillActionState(0); M->EnterCombatIdleFromSkill(); }
		if (M) { Event(TEXT("СОСТОЯНИЕ"), M->NetRenderId, TEXT("6 (боевая стойка, навык окончен)")); }
		return;
	}
	R.State = State;
	if (M)
	{
		M->EnterSkillState(State);   // ревизия п.1: таймеры замаха/флинча и движение сняты — их конец не перебьёт навык
		R.ClipSerial = M->GetClipSerial();
		Event(TEXT("СОСТОЯНИЕ"), M->NetRenderId, FString::FromInt(State));
	}
}

bool UAsdaMobSkillSubsystem::DriveClip(AAsdaMobActor* M, FMobRt& R, int32 Slot, bool bLoop)
{
	if (R.ClipSlot != Slot)
	{
		R.ClipSlot = Slot;
		R.ClipLenMs = 0.f;
		// Слот загружен у этого типа? (`mobEffect_LoadArchiveStep@0xF81B90`) Нет — запись слота пустая: длина 0, «клип кончился сразу».
		const int32 Bit = 1 << (Slot - 18);
		if ((SlotMaskForType(M->MobId) & Bit) != 0)
		{
			R.ClipLenMs = M->PlaySkillClip(Slot, bLoop) * 1000.f;   // слот → файл — у актёра (ClipFileForSlot)
		}
		R.ClipSerial = M->GetClipSerial();
		Event(TEXT("КЛИП"), M->NetRenderId, FString::Printf(TEXT("слот %d → anim_%02d, длина %.0f мс%s"), Slot, ClipForSlot(Slot), R.ClipLenMs,
			R.ClipLenMs <= 0.f ? TEXT(" (слот пуст у типа — анимации нет)") : TEXT("")));
	}
	return R.AnimMs >= R.ClipLenMs;   // `Game_IsPausedOrMenu(клип слота)` — время клипа дошло до конца
}

// ═══ Эффекты ═══════════════════════════════════════════════════════════════════════════════════════════════════════

AActor* UAsdaMobSkillSubsystem::SpawnMobFx(AAsdaMobActor* M, const FString& Jeb, int32 Attach, int32 Skill, const TCHAR* Phase)
{
	UWorld* W = M ? M->GetWorld() : nullptr;
	if (!W || Jeb.IsEmpty()) { return nullptr; }
	// `CEffectMgr_CreateEffectFromJEB(jeb, матрица моба ent+0x168, …, attach)`: точка — ноги моба, поворот — курс моба, масштаб 1.0
	//   (`scaleAll` в путях навыка моба нет). Курс: у моба крутится меш (доворот `MeshYawOffset`), у матрицы эталона — сама сущность.
	const float Yaw = (M->Mesh ? M->Mesh->GetComponentRotation().Yaw : M->GetActorRotation().Yaw) - M->MeshYawOffset;
	AAsdaEffectActor* Fx = AAsdaEffectActor::SpawnOnCarrier(W, Jeb, M->GetActorLocation(), M, FRotator(0.f, Yaw, 0.f));
	if (!Fx)
	{
		Event(TEXT("НЕТ_BP"), M->NetRenderId, FString::Printf(TEXT("навык %d фаза %s: BP_Fx_%s не найден"), Skill, Phase, *Jeb));
		return nullptr;
	}
	// Привязка: 15 `effectNodeFollowEntityBone@0xF599C0` (мировая матрица моба по renderId, гаснет со смертью),
	//   14 `effect_UpdateAttachToTarget` (цель = сам моб: `CControl_GetField236(self)`) — следуют за мобом; 0 — стоит на месте.
	//   У порта моб поворачивает МЕШ — эффект едет за мешем (позиция и курс), масштаб мира остаётся 1.
	if ((Attach == 15 || Attach == 14) && M->Mesh)
	{
		Fx->AttachToComponent(M->Mesh, FAttachmentTransformRules::KeepWorldTransform);
	}
	Event(TEXT("ЭФФЕКТ"), M->NetRenderId, FString::Printf(TEXT("навык %d фаза %s: %s привязка %d"), Skill, Phase, *Jeb, Attach));
	ASDA_CTRACE_MOB(M->NetRenderId, EAsdaCT::Fx, TEXT("моб rid=%d '%s': навык %d фаза %s — эффект %s (привязка %d)"),
		M->NetRenderId, *M->MobName, Skill, Phase, *Jeb, Attach);
	return Fx;
}

AActor* UAsdaMobSkillSubsystem::SpawnHitFx(AActor* Target, const FEntry& E, int32 Rid)
{
	UWorld* W = Target ? Target->GetWorld() : nullptr;
	if (!W || E.Jeb[4].IsEmpty()) { return nullptr; }
	// `CEffectMgr_CreateEffectFromJEB(jeb4, posVec цели, …, кость Bone4, attach SlotParam4)` (entity_char.c:56033/56166).
	AAsdaPlayerCharacter* Ch = Cast<AAsdaPlayerCharacter>(Target);
	const FRotator Rot(0.f, Target->GetActorRotation().Yaw, 0.f);
	const FVector At = Ch ? FVector(Target->GetActorLocation().X, Target->GetActorLocation().Y,
		Target->GetActorLocation().Z - Ch->GetSimpleCollisionHalfHeight()) : Target->GetActorLocation();   // posVec — ноги
	AAsdaEffectActor* Fx = nullptr;
	EAsdaFxAttachMode Mode = EAsdaFxAttachMode::CharPos;
	float RiseZ = 0.f;
	if (E.Attach[4] == 8 && Ch && Ch->BaseSkel && AAsdaEffectActor::EtalonAttach(8, 0, 0.f, Mode, RiseZ))
	{
		// 8 `objCopyCharTransformOrAbort@0xF58D00` — мировая матрица ИГРОКА (общая таблица порта: 6/8 → CharWorld).
		Fx = AAsdaEffectActor::SpawnOnCarrier(W, E.Jeb[4], At, Target, Ch->BaseSkel, NAME_None, Mode, FVector(0.f, 0.f, RiseZ));
	}
	else
	{
		// 0 — статично в матрице цели. 15 — эталон ищет носителя по renderId среди сущностей-мобов (а передан userIdx игрока),
		//   [ПОРТ] эффект ставится статично в точке цели.
		Fx = AAsdaEffectActor::SpawnOnCarrier(W, E.Jeb[4], At, Target, Rot);
	}
	Event(Fx ? TEXT("УДАР") : TEXT("НЕТ_BP"), Rid, FString::Printf(TEXT("навык %d: %s на цели %s привязка %d"), E.Skill, *E.Jeb[4],
		*GetNameSafe(Target), E.Attach[4]));
	return Fx;
}

void UAsdaMobSkillSubsystem::PlayKaiyaRevive(int32 Rid)
{
	// `uiControlSpawnSelectEffect@0xF79660`: пресет №1 `Effect_InitMonKaiyaReviveDesc@0xF747D0` — один .jeb
	//   «Mon_KaiyaRevive_AC_00» (кость «Bip01 Footsteps», id 1, attach 15). На мобе по renderId — attach 15; иначе на игроке
	//   по userIdx — кость «1», attach 7.
	static const FString kJeb(TEXT("Mon_KaiyaRevive_AC_00"));
	if (AAsdaMobActor* M = FindMob(Rid))
	{
		SpawnMobFx(M, kJeb, 15, 1, TEXT("пресет Кайи"));
		return;
	}
	const UAsdaWorldSubsystem* WS = GetGameInstance() ? GetGameInstance()->GetSubsystem<UAsdaWorldSubsystem>() : nullptr;
	if (AAsdaPlayerCharacter* Ch = Cast<AAsdaPlayerCharacter>(WS ? WS->ActorByTarget(EAsdaTargetKind::Character, Rid) : nullptr))   // без «жив»
	{
		UWorld* W = Ch->GetWorld();
		const FName Bone(TEXT("1"));
		const bool bBone = Ch->BaseSkel && Ch->BaseSkel->GetBoneIndex(Bone) != INDEX_NONE;
		if (W && bBone)
		{
			EAsdaFxAttachMode Mode = EAsdaFxAttachMode::BonePos; float RiseZ = 0.f;
			AAsdaEffectActor::EtalonAttach(7, 0, 0.f, Mode, RiseZ);
			AAsdaEffectActor::SpawnOnCarrier(W, kJeb, Ch->BaseSkel->GetBoneLocation(Bone), Ch, Ch->BaseSkel, Bone, Mode, FVector(0.f, 0.f, RiseZ));
			Event(TEXT("ЭФФЕКТ"), Rid, TEXT("пресет Кайи на игроке, кость «1»"));
		}
	}
}

// ═══ Снаряд (`charSkill_InitHitDamageFx@0xA50BE0` / `CharSkill_UpdateHitDamageFx@0xA510B0`) ═════════════════════════

void UAsdaMobSkillSubsystem::LaunchFlight(AAsdaMobActor* M, FMobRt& R, FEntry& E)
{
	// Порядок эталона (entity_char.c:55862-56006): +355 и +356 обнуляются ДО цикла слотов, режим точки удара берётся
	//   из +356 уже после обнуления (дефект — см. шапку файла).
	const int32 HitModeBefore = E.Flag[4];
	E.Flag[3] = 0;
	E.Flag[4] = 0;
	const int32 HitMode = GAsdaMobSkillHitFxFix != 0 ? HitModeBefore : E.Flag[4];
	if (R.Flights.Num() >= kMaxFlights) { Event(TEXT("СНАРЯД_НЕТ"), M->NetRenderId, TEXT("все 3 слота заняты")); return; }
	AActor* Tgt = FindLivePlayer(E.TargetIdx);   // цель снаряда — игрок по userIdx +400, живой
	UWorld* W = M->GetWorld();
	if (!Tgt || !W)
	{
		Event(TEXT("СНАРЯД_НЕТ"), M->NetRenderId, FString::Printf(TEXT("навык %d: цели %d нет/мертва — снаряд не пущен"), E.Skill, E.TargetIdx));
		return;
	}
	// Старт: x/z — позиция моба (`sub_92AA21` = ent+0x150), высота — ent[110] (звуковая точка, центр); цель — звуковая точка игрока.
	const FVector From(M->GetActorLocation().X, M->GetActorLocation().Y, M->Mesh ? M->Mesh->Bounds.Origin.Z : M->GetActorLocation().Z);
	const FVector To = Tgt->GetActorLocation();
	AAsdaProjectileActor* P = W->SpawnActor<AAsdaProjectileActor>(AAsdaProjectileActor::StaticClass(), From, FRotator::ZeroRotator);
	if (!P) { return; }
	if (P->ArrowMesh) { P->ArrowMesh->SetVisibility(false, true); }   // у снаряда навыка меша нет — только .jeb полёта
	if (!E.Jeb[3].IsEmpty())
	{
		P->TrailFx = AAsdaEffectActor::SpawnByJebName(W, E.Jeb[3], From, FRotator::ZeroRotator, P);
		if (AActor* T = P->TrailFx) { T->AttachToActor(P, FAttachmentTransformRules::KeepWorldTransform); }
		else { Event(TEXT("НЕТ_BP"), M->NetRenderId, FString::Printf(TEXT("навык %d фаза 3: BP_Fx_%s не найден"), E.Skill, *E.Jeb[3])); }
	}
	FFlight F;
	F.Proj = P;
	F.Id = NextFlightId++;
	F.Skill = E.Skill;
	F.MobRid = M->NetRenderId;
	F.TargetUserIdx = E.TargetIdx;
	F.Damage = R.TargetDmg[0];          // v31 = self[1705] — урон первой записи
	F.Flags = R.TargetFlags[0];         // `sub_91E230(слот, word 3450)` — флаги первой записи
	F.HitMode = HitMode;
	F.HitJeb = E.Jeb[4];
	const int32 Rid = M->NetRenderId, Id = F.Id;
	TWeakObjectPtr<UAsdaMobSkillSubsystem> WeakThis(this);
	P->OnArrive.BindLambda([WeakThis, Rid, Id]() { if (UAsdaMobSkillSubsystem* S = WeakThis.Get()) { S->OnFlightArrived(Rid, Id); } });
	P->Launch(From, To, kFlightSpeed);
	R.Flights.Add(MoveTemp(F));
	Event(TEXT("СНАРЯД"), Rid, FString::Printf(TEXT("навык %d: %s → игрок %d, %.0f uu, режим удара %d%s"), E.Skill, *E.Jeb[3], E.TargetIdx,
		FVector::Dist(From, To), HitMode, (HitModeBefore != HitMode) ? TEXT(" (как оригинал: режим прочитан после обнуления)") : TEXT("")));
}

void UAsdaMobSkillSubsystem::OnFlightArrived(int32 Rid, int32 FlightId)
{
	FMobRt* R = Mobs.Find(Rid);
	if (!R) { return; }
	const int32 Idx = R->Flights.IndexOfByPredicate([FlightId](const FFlight& F) { return F.Id == FlightId; });
	if (Idx == INDEX_NONE) { return; }
	const FFlight F = R->Flights[Idx];
	R->Flights.RemoveAt(Idx);
	if (AAsdaProjectileActor* P = F.Proj.Get()) { P->SetLifeSpan(0.01f); }
	UGameInstance* GI = GetGameInstance();
	// Цель — по userIdx (`CharManager_FindByUserIdx(+1780)`), без проверки «жив»; прилёт = пересечение с боксом цели (у порта —
	//   конец полёта в зафиксированную точку, как у стрел `AAsdaProjectileActor`).
	const UAsdaWorldSubsystem* WS = GI ? GI->GetSubsystem<UAsdaWorldSubsystem>() : nullptr;
	AActor* Tgt = WS ? WS->ActorByTarget(EAsdaTargetKind::Character, F.TargetUserIdx) : nullptr;
	if (!Tgt) { Event(TEXT("ПРИЛЁТ"), Rid, TEXT("цели нет — без цифр и удара")); return; }
	const bool bMe = IsSelf(F.TargetUserIdx);
	if (AAsdaMobActor* M = FindMob(F.MobRid))   // `Entity_FindByRenderId(+1792)` — моб жив в мире
	{
		UAsdaFloatingTextSubsystem* FT = GI->GetSubsystem<UAsdaFloatingTextSubsystem>();
		const bool bShow = UAsdaWorldSubsystem::ShouldShowDamageText(bMe);   // `жертва-я` или режим показа 1
		const FString Num = FString::FromInt(F.Damage);
		auto Flinch = [&]()
		{
			// `gfxBias_LazyInitHalfTexel@0xA2C7B0` (МИСНЕЙМ: старт вздрагивания) — 0xA5196B / 0xA51C72, только «жертва — я».
			AAsdaPlayerCharacter* Ch = Cast<AAsdaPlayerCharacter>(Tgt);
			if (Ch && Ch->BaseSkel && AsdaAnimSmooth::HitFlinchOn()) { AsdaAnimSmooth::StartHitFlinch(Ch->BaseSkel); Event(TEXT("ВЗДРОГ"), Rid, TEXT("жертва — я")); }
		};
		int32 Style = -1; FString Text; int32 Notice = 0; FColor Col = kColHit; bool bNumber = true;
		if (F.Damage != 0)
		{
			if (F.Flags & 0x04)      { Style = bMe ? 0xA : 0xC; Text = Num; Notice = 20240; Col = kColCrit; }
			else if (F.Flags & 0x08) { Style = 0x8; Text = Num; Notice = 20260; Col = kColHit; }
			else if (F.Flags & 0x20) { Style = 0x5; Text = TEXT("IMMUNE"); Notice = 20261; Col = kColWhite; bNumber = false; }
			else                     { Style = bMe ? 0xB : 0x3; Text = Num; Notice = 20241; Col = kColHit; }
		}
		else { Style = 0x5; Text = TEXT("MISS"); Notice = 20261; Col = kColWhite; bNumber = false; }
		if (bShow && FT) { FT->SpawnOverActor(Style, Text, Tgt); }
		if (bMe)
		{
			if (F.Damage != 0 && ((F.Flags & 0x04) || !(F.Flags & 0x28))) { Flinch(); }   // крит / обычный
			ChatNotice(Notice, M->MobName, F.Damage, bNumber, Col);
		}
		Event(TEXT("ЦИФРА"), Rid, FString::Printf(TEXT("снаряд навыка %d по %d%s: «%s» стиль %d, флаги 0x%02X%s"), F.Skill, F.TargetUserIdx,
			bMe ? TEXT(" (я)") : TEXT(""), *Text, Style, F.Flags, bShow ? TEXT("") : TEXT(" (скрыта режимом показа)")));
		ASDA_CTRACE_IF(bMe || AsdaCombatTrace::IsEngaged(Rid), EAsdaCT::DmgText, TEXT("снаряд навыка моба rid=%d (навык %d) по игроку %d: «%s» стиль %d, флаги 0x%02X"),
			Rid, F.Skill, F.TargetUserIdx, *Text, Style, F.Flags);
	}
	// Удар по прилёте — по режиму точки (`switch (+1872)`): 1 — звуковая точка (центр), 2 — `Class_GetVec3_86` (ноги),
	//   3 — ноги + высота бокса (`sub_A52600` = 2·полувысота). Статично (`CEffectMgr_CreateEffect_F561F0`, attach 0).
	if (!F.HitJeb.IsEmpty() && F.HitMode >= 1 && F.HitMode <= 3)
	{
		const AAsdaPlayerCharacter* Ch = Cast<AAsdaPlayerCharacter>(Tgt);
		const float Half = Ch ? Ch->GetSimpleCollisionHalfHeight() : 0.f;
		FVector At = Tgt->GetActorLocation();
		if (F.HitMode == 2) { At.Z -= Half; }
		else if (F.HitMode == 3) { At.Z += Half; }
		const bool bOk = AAsdaEffectActor::SpawnOnCarrier(Tgt->GetWorld(), F.HitJeb, At, Tgt) != nullptr;
		Event(bOk ? TEXT("УДАР") : TEXT("НЕТ_BP"), Rid, FString::Printf(TEXT("снаряд навыка %d: %s режим %d"), F.Skill, *F.HitJeb, F.HitMode));
	}
}

// ═══ Цифры и чат ════════════════════════════════════════════════════════════════════════════════════════════════════

void UAsdaMobSkillSubsystem::ChatNotice(int32 NoticeId, const FString& MobName, int32 Damage, bool bWithNumber, const FColor& Col) const
{
	UGameInstance* GI = GetGameInstance();
	// `CObject_SetSlotString(ctx, 0, имя моба)` + `CClass_SetSlotPair(ctx, 0, урон)` → `UI_FormatLocalizedNotice(id)` →
	//   `CChat_AddTokenizedLines(…, 10000, 0)`. ★ R5 (12.11.2026): формат и отправка — общая `AsdaCombatChat::Line`.
	AsdaCombatChat::Line(GI, NoticeId, { MobName }, bWithNumber ? TArray<int32>{ Damage } : TArray<int32>(), Col);
}

void UAsdaMobSkillSubsystem::ShowAreaTexts(AAsdaMobActor* M, FMobRt& R, const FEntry& E)
{
	// entity_char.c:56150-56290 — по каждой записи цели: эффект удара (если есть) и цифра/строка чата.
	UGameInstance* GI = GetGameInstance();
	UAsdaFloatingTextSubsystem* FT = GI ? GI->GetSubsystem<UAsdaFloatingTextSubsystem>() : nullptr;
	const int32 HitFlag = E.Flag[4];
	for (int32 i = 0; i < R.TargetCount; ++i)
	{
		AActor* Tgt = FindLivePlayer(R.TargetIdx[i]);
		if (!Tgt) { continue; }
		if (HitFlag > 0 && R.AnimMs >= E.DelayB) { SpawnHitFx(Tgt, E, M->NetRenderId); }
		const bool bMe = IsSelf(R.TargetIdx[i]);
		if (!(bMe || UAsdaWorldSubsystem::GetDamageTextMode() == 1)) { continue; }   // `field_40 || режим == 1`
		const int32 Fl = R.TargetFlags[i], Dmg = R.TargetDmg[i];
		if ((Fl & 1) == 0) { continue; }
		const bool bChat = bMe || GAsdaMobSkillObserverChatFix == 0;
		const bool bFix = GAsdaMobSkillChatFix != 0;
		int32 Style = -1; FString Text;
		if (Fl & 0x04)
		{
			if (Dmg > 0) { Style = 0xC; Text = FString::FromInt(Dmg); if (bChat) { ChatNotice(20240, M->MobName, Dmg, true, kColCrit); } }
		}
		else if (Fl & 0x08)
		{
			Style = 0x10; Text = FString::FromInt(Dmg);
			if (bChat)
			{
				if (bFix) { ChatNotice(20260, M->MobName, Dmg, true, kColHit); }
				else if (UAsdaChatSubsystem* Chat = GI->GetSubsystem<UAsdaChatSubsystem>()) { Chat->AddSystemMessage(Text, kColHit, EAsdaSysTrack::A, 10000.f); }
			}
		}
		else if (Fl & 0x10) { Style = 0xB; Text = TEXT("DODGE"); if (bChat && bFix) { ChatNotice(20261, M->MobName, 0, false, kColHit); } }   // цвет ветки площади −33517
		else if (Fl & 0x20) { Style = 0xB; Text = TEXT("IMMUNE"); if (bChat && bFix) { ChatNotice(20261, M->MobName, 0, false, kColHit); } }
		else if (Dmg > 0)   { Style = 0xB; Text = FString::FromInt(Dmg); if (bChat) { ChatNotice(20241, M->MobName, Dmg, true, kColHit); } }
		if (Style >= 0 && FT) { FT->SpawnOverActor(Style, Text, Tgt); }
		if (Style >= 0)
		{
			Event(TEXT("ЦИФРА"), M->NetRenderId, FString::Printf(TEXT("площадь навыка %d по %d%s: «%s» стиль %d, флаги 0x%02X"), E.Skill, R.TargetIdx[i],
				bMe ? TEXT(" (я)") : TEXT(""), *Text, Style, Fl));
			ASDA_CTRACE_IF(bMe || AsdaCombatTrace::IsEngaged(M->NetRenderId), EAsdaCT::DmgText, TEXT("навык моба rid=%d '%s' (навык %d) по игроку %d: «%s» стиль %d, флаги 0x%02X"),
				M->NetRenderId, *M->MobName, E.Skill, R.TargetIdx[i], *Text, Style, Fl);
		}
	}
}

// ═══ Приём ═════════════════════════════════════════════════════════════════════════════════════════════════════════

void UAsdaMobSkillSubsystem::OnNet8011(const uint8* Body, int32 Len)
{
	if (!Body || Len < 7) { UE_LOG(LogAsdaMobSkill, Warning, TEXT("8011 тело мало (%d)"), Len); return; }
	const int32 Skill = AsdaBytes::RdI16(Body + 0);
	const int32 TargetType = Body[2];
	const int32 TargetIdx = AsdaBytes::RdI16(Body + 3);
	const int32 Rid = AsdaBytes::RdI16(Body + 5);
	if (GAsdaMobSkill == 0) { Event(TEXT("8011_ВЫКЛ"), Rid, FString::Printf(TEXT("навык %d (asda.MobSkill 0)"), Skill)); return; }
	AAsdaMobActor* M = FindMob(Rid);
	if (!M) { Event(TEXT("8011_НЕТ_МОБА"), Rid, FString::Printf(TEXT("навык %d"), Skill)); return; }
	FMobRt& R = Mobs.FindOrAdd(Rid);
	R.PhaseMs = 0.f;                                   // ent[1768] = 0
	SpawnEntry(M, R, Skill, TargetType, TargetIdx);
	M->SetChaseTargetUserIdx(TargetIdx);              // ent[1749] = цель @3
	SetState(M, R, 17);
	Event(TEXT("8011"), Rid, FString::Printf(TEXT("каст навыка %d, цель %d/%d, моб '%s' тип %d"), Skill, TargetType, TargetIdx, *M->MobName, M->MobId));
	ASDA_CTRACE_MOB(Rid, EAsdaCT::MobAttack, TEXT("8011 моб rid=%d '%s': начало каста навыка %d → состояние 17"), Rid, *M->MobName, Skill);
}

void UAsdaMobSkillSubsystem::OnNet8012(const uint8* Body, int32 Len)
{
	if (!Body || Len < 9) { UE_LOG(LogAsdaMobSkill, Warning, TEXT("8012 тело мало (%d)"), Len); return; }
	const int32 Err = Body[0];
	const int32 Skill = AsdaBytes::RdI16(Body + 1);
	const int32 Rid = AsdaBytes::RdI16(Body + 3);
	const int32 Cond = (int8)Body[5];
	const int32 TargetType = Body[6];
	const int32 TargetIdx = AsdaBytes::RdI16(Body + 7);
	// Записи: `(payloadSize − 19) / 29` целочисленно — хвост сервера (паддинг 0..19 б) отбрасывается; записи сверх 20 — тоже.
	const int32 NRec = FMath::Min((Len - 9) / kRecSize, kMaxTargets);
	if (GAsdaMobSkill == 0) { Event(TEXT("8012_ВЫКЛ"), Rid, FString::Printf(TEXT("навык %d (asda.MobSkill 0)"), Skill)); return; }
	if (Err != 0) { Event(TEXT("8012_ОШИБКА"), Rid, FString::Printf(TEXT("навык %d err=%d — эталон ничего не делает"), Skill, Err)); return; }
	if (Skill == 1) { PlayKaiyaRevive(Rid); Event(TEXT("8012"), Rid, TEXT("навык 1 — пресет «пробуждение Кайи»")); return; }
	AAsdaMobActor* M = (Skill != 0) ? FindMob(Rid) : nullptr;
	if (!M) { Event(TEXT("8012_НЕТ_МОБА"), Rid, FString::Printf(TEXT("навык %d"), Skill)); return; }
	FMobRt& R = Mobs.FindOrAdd(Rid);
	M->SetChaseTargetUserIdx(TargetIdx);              // targetEntity[1749] = шапка @7
	// `partyParseMemberList@0xF8E270`: до 20 записей, стоп на индексе −1 (незаполненные записи = −1, `sub_F34480`).
	R.TargetCount = 0;
	for (int32 i = 0; i < NRec; ++i)
	{
		const uint8* Rec = Body + 9 + i * kRecSize;
		const int32 Idx = AsdaBytes::RdI16(Rec + 1);
		if (Idx == -1) { break; }
		R.TargetIdx[i] = Idx;
		R.TargetDmg[i] = AsdaBytes::RdI32(Rec + 3);
		R.TargetFlags[i] = Rec[7];
		R.TargetCount = i + 1;
		// Вид цели «моб» (шапка @6 == 0) и клетка @15/@17 > 0 — моб-цель ставится в клетку (`playerSetWorldPosition`).
		const int32 TX = AsdaBytes::RdI16(Rec + 15), TZ = AsdaBytes::RdI16(Rec + 17);
		if (TargetType == 0 && TX > 0 && TZ > 0)
		{
			if (AAsdaMobActor* T = FindMob(Idx)) { T->NetSnapPos(AsdaCoord::TileCenterGame(TX, TZ)); Event(TEXT("ПЕРЕНОС"), Idx, FString::Printf(TEXT("клетка %d,%d"), TX, TZ)); }
		}
	}
	R.PhaseMs = 0.f;                                   // targetEntity[1768] = 0
	FString How;
	if (R.Queue.Num() == 0) { SetState(M, R, 17); How = TEXT("очередь пуста → 17"); }
	else
	{
		const FEntry& Head = R.Queue[0];
		if (Head.Skill == Skill && Head.Marker == 2) { R.Queue.RemoveAt(0); SetState(M, R, 15); How = TEXT("голова в касте → 15"); }
		else if (Head.Skill == Skill && Head.Marker == 1) { R.Queue.RemoveAt(0); SetState(M, R, 16); How = TEXT("голова в пре-касте → 16"); }
		else { R.Queue.Reset(); SetState(M, R, 17); How = TEXT("голова чужая → очередь очищена, 17"); }   // `sub_925B5C`
	}
	SpawnEntry(M, R, Skill, TargetType, TargetIdx);
	Event(TEXT("8012"), Rid, FString::Printf(TEXT("навык %d усл %d цель %d/%d, записей %d (%d б): %s"), Skill, Cond, TargetType, TargetIdx,
		R.TargetCount, Len, *How));
#if ASDA_COMBAT_TRACE
	// Журнал боя уровнем 1 — если среди целей я (или цель шапки я), либо моб в нашем бою.
	bool bAnyMe = IsSelf(TargetIdx);
	for (int32 i = 0; i < R.TargetCount && !bAnyMe; ++i) { bAnyMe = IsSelf(R.TargetIdx[i]); }
	ASDA_CTRACE_IF(bAnyMe || AsdaCombatTrace::IsEngaged(Rid), EAsdaCT::MobAttack, TEXT("8012 моб rid=%d '%s': навык %d, целей %d%s — %s"),
		Rid, *M->MobName, Skill, R.TargetCount, bAnyMe ? TEXT(" (я среди целей)") : TEXT(""), *How);
#endif
	UE_LOG(LogAsdaMobSkill, Verbose, TEXT("8012 сырое: %s"), *Hex(Body, FMath::Min(Len, 9 + NRec * kRecSize)));
}

void UAsdaMobSkillSubsystem::OnNet8013(const uint8* Body, int32 Len)
{
	if (!Body || Len < 16) { UE_LOG(LogAsdaMobSkill, Warning, TEXT("8013 тело мало (%d)"), Len); return; }
	const int32 Rid = AsdaBytes::RdI16(Body + 6);
	int32 Hp = AsdaBytes::RdI32(Body + 12);
	if (GAsdaMobSkill == 0) { Event(TEXT("8013_ВЫКЛ"), Rid, FString::FromInt(Hp)); return; }
	AAsdaMobActor* M = FindMob(Rid);
	UAsdaWorldSubsystem* WS = GetGameInstance() ? GetGameInstance()->GetSubsystem<UAsdaWorldSubsystem>() : nullptr;
	// Состояние сущности эталон не проверяет — HP и эффект ставятся и трупу (entity_char.c:52597–52640).
	if (!M || !WS) { Event(TEXT("8013_НЕТ_МОБА"), Rid, FString::FromInt(Hp)); return; }
	// `Entity_ApplyHpRestore@0xF34500`: > max типа — до max + эффект; 0..max — HP + эффект; < 0 — 0 без эффекта.
	//   Максимум — табличный HP типа (`sub_932483(g_pMobMgr, ent[117])`), у порта `MobTypeMaxHp` (иначе текущий MaxHp).
	const int32 MaxHp = M->MobTypeMaxHp > 0 ? M->MobTypeMaxHp : M->MaxHp;
	bool bFx = true;
	if (Hp > MaxHp && MaxHp > 0) { Hp = MaxHp; }
	else if (Hp < 0) { Hp = 0; bFx = false; }
	// [ОРИГИНАЛ] HP пишется напрямую (ent[284] = ent[283] = hp) — мимо «пола полосы в бою»: это лечение, а не снимок урона.
	const int32 WasHp = M->CurHp;   // полоса до лечения (прибавка лечения — для отсева записей, посчитанных сервером до него)
	M->CurHp = Hp;
	// ★ 05.11.2026 [ИСПРАВЛЕНИЕ] (bug-mob-hp-zero, ключ `asda.Combat.HealEpoch`): лечение — новая эпоха HP. Без сброса прогноз прошлой
	//   эпохи отбрасывал все снимки выше себя, и полоса босса уходила в 0 при живом (вылеченном) боссе (журнал Кайи 01.11).
	WS->OnMobHealed(M, WasHp, Hp);
	M->ApplyPacketHpToMax(Hp, TEXT("8013 лечение моба"));
	// Полоса плашки — тем же вызовом, что `UAsdaWorldSubsystem::UpdateMobHpBar` (сегменты = «жизни» типа).
	if (M->Nameplate) { M->Nameplate->SetHp(M->CurHp, M->MaxHp, /*Segments=*/M->MobLives); }
	if (bFx)
	{
		// `effectMgrCreateEffectEx("eff_0_othr_99_hpptn01-a_00.jeb", pos = sub_92AA21(моб), renderId, 0, attach 9)` — позиция сущности.
		AAsdaEffectActor::SpawnOnCarrier(M->GetWorld(), TEXT("eff_0_othr_99_hpptn01-a_00"), M->GetActorLocation(), M);
	}
	Event(TEXT("8013"), Rid, FString::Printf(TEXT("лечение: HP %d / %d%s"), Hp, MaxHp, bFx ? TEXT(" + эффект зелья") : TEXT("")));
	ASDA_CTRACE_MOB(Rid, EAsdaCT::HpPlate, TEXT("8013 моб rid=%d '%s': лечение навыком, HP → %d / %d"), Rid, *M->MobName, Hp, MaxHp);
}

// ═══ Тики состояний ════════════════════════════════════════════════════════════════════════════════════════════════

void UAsdaMobSkillSubsystem::TickPrecast(AAsdaMobActor* M, FMobRt& R, float DtMs)
{
	// `charActionTick_State23@0xF7E870` (entity_char.c:55525).
	R.AnimMs += DtMs;
	R.PhaseMs += DtMs;
	if (R.Queue.Num() == 0) { SetState(M, R, 6); return; }
	FEntry& E = R.Queue[0];
	if (!E.Flag[0])
	{
		if (AActor* Fx = R.TrackedFx.Get()) { Fx->Destroy(); }
		SetState(M, R, 16);
		return;
	}
	const bool bClipEnd = DriveClip(M, R, 18, /*bLoop=*/true);
	E.Marker = 1;
	if (!E.Jeb[0].IsEmpty())
	{
		R.TrackedFx = SpawnMobFx(M, E.Jeb[0], E.Attach[0], E.Skill, TEXT("0 пре-каст"));
		E.Jeb[0].Empty();
		M->PlayMobSound(11);   // `_99.wav`
	}
	if (bClipEnd || E.TimeoutMs <= R.PhaseMs)
	{
		if (E.TimeoutMs <= R.PhaseMs)
		{
			E.Flag[0] = 0;
			SetState(M, R, 16);
			R.PhaseMs = 0.f;
		}
		R.AnimMs = 0.f;
	}
}

void UAsdaMobSkillSubsystem::TickCast(AAsdaMobActor* M, FMobRt& R, float DtMs)
{
	// `charState_UpdateCast@0xF7ECD0` (entity_char.c:55647).
	R.AnimMs += DtMs;
	R.PhaseMs += DtMs;
	if (R.Queue.Num() == 0) { SetState(M, R, 6); return; }
	FEntry& E = R.Queue[0];
	if (!E.Flag[1]) { SetState(M, R, 15); return; }
	const bool bClipEnd = DriveClip(M, R, 19, /*bLoop=*/true);
	E.Marker = 2;
	if (!E.Jeb[1].IsEmpty())
	{
		R.TrackedFx = SpawnMobFx(M, E.Jeb[1], E.Attach[1], E.Skill, TEXT("1 каст"));
		E.Jeb[1].Empty();
		M->PlayMobSound(12);
	}
	if (bClipEnd || E.TimeoutMs <= R.AnimMs)
	{
		if (E.TimeoutMs <= R.PhaseMs)
		{
			E.Flag[1] = 0;
			SetState(M, R, 15);
			R.PhaseMs = 0.f;
		}
		R.AnimMs = 0.f;
	}
}

void UAsdaMobSkillSubsystem::TickAttack(AAsdaMobActor* M, FMobRt& R, float DtMs)
{
	// `charExecuteAttackAction@0xF7F1F0` (entity_char.c:55766).
	R.AnimMs += DtMs;
	if (R.Queue.Num() == 0) { SetState(M, R, 6); return; }
	const int32 Rid = M->NetRenderId;
	const int32 AnimBit = SkillAnimBit(R.Queue[0].Skill);
	// ── фаза 3: снаряд при t_anim ≥ A ──
	if (R.Queue[0].Flag[3] && R.AnimMs >= R.Queue[0].DelayA) { LaunchFlight(M, R, R.Queue[0]); }
	FEntry& E = R.Queue[0];
	// ── фаза 4: удар ──
	if (R.TargetCount == 0 || E.bProjectile)
	{
		// Одиночная ветка (`!self[1693] || +408`).
		if (E.TargetType == 1 || E.Flag[4])
		{
			bool bClear = true;
			if (E.Flag[4])
			{
				if (GAsdaMobSkillHitFxFix != 0 && E.bProjectile)
				{
					bClear = false;   // [ИСПРАВЛЕНИЕ] удар навыка со снарядом — у снаряда (режим точки), а не на кадре B
				}
				else if (R.AnimMs >= E.DelayB)
				{
					if (AActor* Tgt = FindLivePlayer(E.TargetIdx)) { SpawnHitFx(Tgt, E, Rid); }
				}
				else if (GAsdaMobSkillHitFxFix != 0)
				{
					bClear = false;   // [ИСПРАВЛЕНИЕ] ждать задержку удара B (оригинал обнуляет +356 на первом тике)
				}
			}
			if (bClear) { E.Flag[4] = 0; }
		}
	}
	else
	{
		// Ветка «площадь»: по списку целей из 8012.
		if (!E.Flag[4]) { E.DelayB = 0.f; }               // `if (!hitFlag) +384 = 0`
		if (R.AnimMs >= E.DelayB)
		{
			if (SkillInfoValid(E.Skill))
			{
				ShowAreaTexts(M, R, E);
				E.Flag[4] = 0;
			}
			R.TargetCount = 0;                             // `sub_91E7BC` — список целей очищен
		}
	}
	// ── фаза 2: клип атаки, эффект, звук ──
	if (E.Flag[2])
	{
		const int32 Slot = 21 - AnimBit;
		const bool bClipEnd = DriveClip(M, R, Slot, /*bLoop=*/false);
		if (!E.Jeb[2].IsEmpty())
		{
			R.TrackedFx = SpawnMobFx(M, E.Jeb[2], E.Attach[2], E.Skill, TEXT("2 атака"));
			M->PlayMobSound(14 - AnimBit);
			E.Jeb[2].Empty();
		}
		if (bClipEnd)
		{
			E.Flag[2] = 0;
			if (!E.Flag[4] && !E.Flag[3])
			{
				R.Queue.Reset();                           // `sub_925B5C`
				SetState(M, R, 6);
			}
		}
	}
	else if (!E.Flag[4] && !E.Flag[3])
	{
		R.Queue.Reset();
		SetState(M, R, 6);
	}
}

void UAsdaMobSkillSubsystem::Tick(float DeltaTime)
{
	ASDA_PERF_SCOPE("Tick.MobSkill");   // R4, доля a: область замера (AsdaPerf.h; в Shipping пусто)
	if (Mobs.Num() == 0) { return; }
	const float DtMs = DeltaTime * 1000.f;
	UGameInstance* GI = GetGameInstance();
	const UAsdaEffectsSubsystem* Fx = GI ? GI->GetSubsystem<UAsdaEffectsSubsystem>() : nullptr;
	const UAsdaWorldSubsystem* WS = GI ? GI->GetSubsystem<UAsdaWorldSubsystem>() : nullptr;
	for (auto It = Mobs.CreateIterator(); It; ++It)
	{
		FMobRt& R = It.Value();
		AAsdaMobActor* M = FindMob(It.Key());
		if (!IsValid(M))
		{
			// Моба нет в мире — его очередь и снаряды исчезают вместе с сущностью (эталон: слоты — часть сущности).
			if (AActor* T = R.TrackedFx.Get()) { T->Destroy(); }
			for (FFlight& F : R.Flights) { if (AAsdaProjectileActor* P = F.Proj.Get()) { P->Destroy(); } }
			It.RemoveCurrent();
			continue;
		}
		// Снаряды, исчезнувшие без прилёта (уничтожены миром), — убрать из слотов.
		R.Flights.RemoveAll([](const FFlight& F) { return !F.Proj.IsValid(); });
		if (R.State != 0)
		{
			// Моба перевели в другое состояние (движение/замах 4015, запись 4013, удар игрока, смерть) — эталонный SetActionState
			//   гасит эффект фазы ent[4672]; очередь при этом НЕ чистится.
			if (M->IsDead() || M->GetClipSerial() != R.ClipSerial)
			{
				if (AActor* T = R.TrackedFx.Get()) { T->Destroy(); }
				R.TrackedFx.Reset();
				Event(TEXT("ПЕРЕБИТ"), It.Key(), FString::Printf(TEXT("состояние %d сменено другим действием моба"), R.State));
				R.State = 0;
				continue;
			}
			// Заморозка (оледенение/окаменение, +6987) — время сущности стоит (`effectiveDelta = 0`).
			const float D = (Fx && Fx->HasMobAnimFreeze(It.Key())) ? 0.f : DtMs;
			switch (R.State)
			{
			case 17: TickPrecast(M, R, D); break;
			case 16: TickCast(M, R, D); break;
			case 15: TickAttack(M, R, D); break;
			default: break;
			}
			if (R.State != 0) { R.ClipSerial = M->GetClipSerial(); }
			// Разворот к цели ent[1749] (`charUpdateActionState23`, entity_char.c:56913): 15/16 — всегда, 17 — если цель задана;
			//   типы 845..848 (ent[117]) не поворачиваются.
			const int32 TypeIdx = M->MobId - 1;
			const int32 Tu = M->GetChaseTargetUserIdx();
			//   Гейт эталона `!ent+6987 && !ent+6986` (0xF84B11–0xF84B2C): замороженный/оглушённый/спящий/в ловушке не поворачивается.
			const bool bCtrl = Fx && (Fx->HasMobAnimFreeze(It.Key()) || Fx->HasMobMoveLock(It.Key()));
			if (R.State != 0 && WS && !bCtrl && !(TypeIdx >= 845 && TypeIdx <= 848) && (R.State != 17 || Tu != -1))
			{
				if (const AActor* T = WS->ActorByTarget(EAsdaTargetKind::Character, Tu)) { M->FaceTowardGame(AsdaCoord::UeToGame2D(T->GetActorLocation())); }
			}
		}
		if (R.State == 0 && R.Queue.Num() == 0 && R.Flights.Num() == 0 && R.TargetCount == 0 && !R.TrackedFx.IsValid())
		{
			It.RemoveCurrent();   // моб без навыка — запись не держим
		}
	}
}
