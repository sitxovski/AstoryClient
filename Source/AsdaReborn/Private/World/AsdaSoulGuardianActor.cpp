// Copyright (c) 2026 @sitxovski (Discord)
// Реализация AAsdaSoulGuardianActor — см. AsdaSoulGuardianActor.h.
//   Реверс: Reports/soul_guardian_companion_re.md (§1.4 фейд, §3.2 карта слотов, §5 привязка,
//   §6 создание/загрузка, §7 автомат мотионов, §8 эффекты, §9 звуки).
#include "World/AsdaSoulGuardianActor.h"
#include "AsdaDevConsole.h"   // ★ античит А2: ASDA_DEV_CVAR / ASDA_DEV_CONSOLE — дев-ключи и команды вне Shipping
#include "UI/AsdaNotice.h"   // ★ U13 (В1 дедупа №2): AsdaNotice::ConsoleEcho — эхо консольных команд
#include "Resources/AsdaAssetPaths.h"   // ★ волна 2 п. 7 (14.09.2026): реестр путей и правил имён ассетов (генерируемый)
#include "AsdaDataTableCache.h"   // ★ Аудит №4 (B2): AsdaDt::Cached — одна ленивая загрузка DataTable на порт
#include "Resources/AsdaModelAnimSubsystem.h"   // Д2/Д8/Д9: ресурс только через менеджер

#include "Animation/AnimMontage.h"
#include "Animation/AnimNodeBase.h"
#include "Animation/AnimSequence.h"
#include "Animation/AnimationPoseData.h"
#include "Animation/Skeleton.h"
#include "Components/SkeletalMeshComponent.h"
#include "Engine/SkeletalMesh.h"
#include "Engine/World.h"
#include "EngineUtils.h"                     // TActorIterator — обход живых компаньонов (asda.SoulFx.Height)
#include "Kismet/GameplayStatics.h"

#include "AsdaEffectActor.h"
#include "World/AsdaCoord.h"                 // ★ Аудит №3 (B1): единый перевод координат игра↔UE
#include "AsdaNetSubsystem.h"                // SelfUserNum — ключ реестра компаньонов в консоли
#include "AsdaSelf.h"                         // ★ свой userIdx одной точкой (E2, 14.09.2026)
#include "Data/AsdaSkillFxRows.h"            // ★ слой 2 удара: skill_info.sif +0xD1 → DT_SkillFx
#include "Engine/DataTable.h"
#include "HAL/IConsoleManager.h"
#include "World/AsdaPlayerController.h"      // выбранная цель для слоя 2 (эталон `g_nTargetUserIdx`/`targetRenderId`; не Target_GetSelectedUserIdx — это userNum переданного)
#include "Resources/AsdaSoundSubsystem.h"
// вывод консольных команд В ИГРУ (как asda.Mini.* / asda.Soul.*)   ★ R3: комментарий снятого включения "UI/AsdaMessageSubsystem.h" (файл им не пользовался) — сохранён
#include "UI/AsdaSoulGuardSubsystem.h"
#include "World/AsdaFadeComponent.h"
#include "World/AsdaPlayerCharacter.h"
#include "World/AsdaWorldSubsystem.h"        // реестр компаньонов (свой и чужие) + опции показа

DEFINE_LOG_CATEGORY_STATIC(LogAsdaSoulGuardian, Log, All);

// Каталог импорта скелетных моделей хранителя (Tools/asda_import_soulguard_models.py).
static const TCHAR* kSoulGuardRoot = AsdaAssetPaths::kSkelRootSoulGuard;   // ★ волна 2 п. 7: корень — реестр путей
// Кость ВЛАДЕЛЬЦА, к которой эталон цепляет ВСЕ эффекты хранителя: "M_DUMMY" (муж) / "G_DUMMY" (жен).
//   В нашем скелете женские root-кости ремапнуты экспортёром G→M (см. §H5 wings_soulmate_re.md),
//   поэтому имя одно; второе оставлено фолбэком на случай моделей со своим G_DUMMY.
static const FName kOwnerFxBone(TEXT("M_DUMMY"));
static const FName kOwnerFxBoneFemale(TEXT("G_DUMMY"));
// Эффект призыва/СМЕНЫ хранителя (§8): один на все 27 записей.
static const TCHAR* kChangeJeb = TEXT("sb_change_01");
// ★ Переключатель attach-10 у эффектов хранителя (см. SpawnOwnerBoundFx). 1 = как в эталоне
//   (эффект живёт в полной матрице кости M_DUMMY владельца), 0 = прежнее поведение порта
//   (спавн в точке кости, мировые оси, без слежения) — на случай, если понадобится сравнить.
static int32 GSoulFxAttach10 = 1;
static FAutoConsoleVariableRef CVarSoulFxAttach10(
	TEXT("asda.SoulFx.Attach10"),
	GSoulFxAttach10,
	TEXT("Хранитель: эффекты на кости владельца в режиме attach-10 эталона (1) или старый спавн в точке (0)"),
	ECVF_Default);
// ★ ОТСТУПЛЕНИЕ (юзер 2026-08-08): удар хранителя разворачивает владельца к ВЫБРАННОЙ ЦЕЛИ тем же
//   механизмом, что обычная атака. 1 = разворачивать (умолчание), 0 = бить как стоим (эталон).
static int32 GSoulFxFaceTarget = 1;
static FAutoConsoleVariableRef CVarSoulFxFaceTarget(
	TEXT("asda.SoulFx.FaceTarget"),
	GSoulFxFaceTarget,
	TEXT("Хранитель: разворачивать владельца к выбранной цели при ударе (1) или бить как стоим (0, эталон)"),
	ECVF_Default);
// ★ СЛОЙ 2 УДАРА («h»-эффект скилла из skill_info.sif +0xD1). 1 = спавним (ОТСТУПЛЕНИЕ: в клиенте
//   пользователя читателя этого поля нет — обоснование в шапке Public/Data/AsdaSkillFxRows.h),
//   0 = как в сборке оригинала (только слой 1, мотионный эффект слотов 31/32/33).
static int32 GSoulFxHitFx = 1;
static FAutoConsoleVariableRef CVarSoulFxHitFx(
	TEXT("asda.SoulFx.HitFx"),
	GSoulFxHitFx,
	TEXT("Хранитель: спавнить СОБСТВЕННЫЙ эффект скилла («…_h_…», слой 2) — 1 да, 0 нет"),
	ECVF_Default);
// ★★★ ДЕФЕКТ ОРИГИНАЛА: клип ухода «off» не проигрывается ВООБЩЕ, если роспуск пришёл во время
//   однократного мотиона (полное доказательство и разбор — шапка bLegacyOffGate в .h).
//   1 = как в оригинале («испаряется»: гейт съедает мотион 9, работает только альфа-фейд),
//   0 = ИСПРАВЛЕНО: дождаться конца удара/призыва, проиграть «off» целиком, потом гасить.
static int32 GSoulFxLegacyOff = 0;
static ASDA_DEV_CVAR_REF(CVarSoulFxLegacyOff,
	TEXT("asda.SoulFx.LegacyOff"),
	GSoulFxLegacyOff,
	TEXT("Хранитель: уход как в оригинале — без клипа «off», если играет удар/призыв (1); 0 = доиграть «off» целиком"),
	ECVF_Default);
// Куда сажать слой 2: 0 = на ВЫБРАННУЮ ЦЕЛЬ (фолбэк — владелец), 1 = ВСЕГДА на владельца.
static int32 GSoulFxHitOnOwner = 0;
static FAutoConsoleVariableRef CVarSoulFxHitOnOwner(
	TEXT("asda.SoulFx.HitOnOwner"),
	GSoulFxHitOnOwner,
	TEXT("Хранитель: слой 2 удара всегда на владельце (1) или на выбранной цели с фолбэком (0)"),
	ECVF_Default);
// Длина общей части имени клипа «sb_md_ani_<fam>» — остаток служит суффиксом ассета
//   (`sb_md_ani_oh_battle_01_03` → ассет `sb_md_oh_01_battle_01_03`), см. asda_build_soulguard_models.py.
static const int32 kClipPrefixLen = 12;

// ═══════════════ нативный AnimInstance: поза целиком из слота монтажей (кросс-фейд 300 мс) ══════
void FAsdaSoulGuardAnimProxy::Initialize(UAnimInstance* InAnimInstance)
{
	FAnimInstanceProxy::Initialize(InAnimInstance);
	// Без регистрации слота движок не считает его «релевантным» и не шлёт нотифаи/веса монтажа.
	RegisterSlotNodeWithAnimInstance(FAnimSlotGroup::DefaultSlotName);
}

bool FAsdaSoulGuardAnimProxy::Evaluate(FPoseContext& Output)
{
	// Источник бленда — реф-поза: активных монтажей нет → компаньон стоит в реф-позе (как трек без клипа).
	Output.ResetToRefPose();
	FCompactPose SourcePose = Output.Pose;
	FBlendedCurve SourceCurve = Output.Curve;
	UE::Anim::FStackAttributeContainer SourceAttrs = Output.CustomAttributes;
	const FAnimationPoseData SourceData(SourcePose, SourceCurve, SourceAttrs);
	FAnimationPoseData OutData(Output);
	// ★★★ ВЕСА СЛОТА БЕРЁМ У ДВИЖКА, А НЕ ЗАШИВАЕМ 1.0 (живой тест 2026-08-10).
	// СИМПТОМ: «нажимаю Insert → моб умирает РАНЬШЕ, чем появляется хранитель». Хранитель тут ни при
	//   чём — игра ВСТАВАЛА. Доказательство из лога `Saved/Logs/AsdaReborn.log`: кадр **397** длится
	//   с 16:25:26.798 (пришёл 6156 «призван индекс 15») до 16:25:32.539 («хранитель создан») =
	//   **5,74 с**. Внутри кадра:
	//     26.798 RECV 6156 → 26.818 FlushAsyncLoading(609) → 27.076 `Ensure condition failed:
	//     FMath::IsNearlyEqual(InTotalNodeWeight, TotalWeight)` [AnimInstanceProxy.cpp:2019]
	//     → 28.768 `LogStats: FDebug::EnsureFailed - 1.874 s` → 28.799 второй ensure
	//     `(AdditivePoses.Num() > 0) || (NonAdditivePoses.Num() > 0)` [AnimInstanceProxy.cpp:2037]
	//     → 32.458 `FDebug::EnsureFailed - 3.690 s` (из них `SendNewReport - 3.651 s`).
	//   Стек первого ensure: `FAsdaSoulGuardAnimProxy::Evaluate()` ← `AAsdaSoulGuardianActor::
	//   InitGuardian()` ← `RefreshSoulGuardian()` ← `TickSoulGuardians()`.
	//   Причина: `InitGuardian` ставит меш (`Mesh->SetSkeletalMesh`) ДО первого `SetMotion(8)`, поэтому
	//   в момент немедленного `Evaluate` в слоте нет НИ ОДНОГО монтажа — реальный `TotalWeight = 0`,
	//   а мы передавали `TotalNodeWeight = 1.0`. В editor-сборке каждый такой ensure делает
	//   `StackWalkAndDump` + `SendNewReport` (1,9 с и 3,7 с по `LogStats`).
	//   ПОСЛЕДСТВИЕ (это и есть жалоба): всё, что сервер прислал за 5,7 с зависания, применилось ОДНИМ
	//   кадром 398 — 6161, 6157 (списание 5 монет), 6156 index=−1, 6159 успех, 4017 hp=0 «смерть моба»
	//   пришли за 4 мс подряд, и удар хранителя (32.953) оказался ПОЗЖЕ смерти цели.
	// КАК ПРАВИЛЬНО (`FAnimNode_Slot::Evaluate_AnyThread` движка): спросить веса у прокси
	//   (`GetSlotWeight`) и при пустом слоте отдать ИСХОДНУЮ позу, не зовя `SlotEvaluatePose` вовсе.
	float SlotNodeWeight = 0.f, SourceWeight = 0.f, TotalNodeWeight = 0.f;
	GetSlotWeight(FAnimSlotGroup::DefaultSlotName, SlotNodeWeight, SourceWeight, TotalNodeWeight);
	if (SlotNodeWeight <= ZERO_ANIMWEIGHT_THRESH)
	{
		return true;   // монтажей нет — компаньон стоит в реф-позе (она уже в Output)
	}
	SlotEvaluatePose(FAnimSlotGroup::DefaultSlotName, SourceData, SourceWeight, OutData,
		SlotNodeWeight, TotalNodeWeight);
	return true;
}

// ═══════════════════════════════════════ актор ══════════════════════════════════════════════════
AAsdaSoulGuardianActor::AAsdaSoulGuardianActor()
{
	PrimaryActorTick.bCanEverTick = true;
	// Тик ПОСЛЕ движения владельца: иначе компаньон строится по позе прошлого кадра (дрожание/отставание —
	//   та же грабля, что чинили у эффектов на носителе, AsdaWorldSubsystem::SpawnCharFx).
	PrimaryActorTick.TickGroup = TG_PostPhysics;

	Root = CreateDefaultSubobject<USceneComponent>(TEXT("Root"));
	SetRootComponent(Root);
	MeshRoot = CreateDefaultSubobject<USceneComponent>(TEXT("MeshRoot"));
	MeshRoot->SetupAttachment(Root);
	// Тот же масштаб/зеркало, что у персонажей, мобов и петов (glTF-метры → см, компенсация хиральности).
	MeshRoot->SetRelativeScale3D(FVector(0.01f, -0.01f, 0.01f));
	Mesh = CreateDefaultSubobject<USkeletalMeshComponent>(TEXT("Mesh"));
	Mesh->SetupAttachment(MeshRoot);
	// ★ Нативный AnimInstance вместо AnimationSingleNode: только он даёт кросс-фейд мотионов
	//   (эталон 300 мс, §7.4). Класс не привязан к скелету → один на все 9 моделей.
	Mesh->SetAnimationMode(EAnimationMode::AnimationBlueprint);
	Mesh->SetAnimInstanceClass(UAsdaSoulGuardAnimInstance::StaticClass());
	Mesh->VisibilityBasedAnimTickOption = EVisibilityBasedAnimTickOption::AlwaysTickPoseAndRefreshBones;
	Mesh->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	// Эталон гоняет компаньона в общей очереди рендера менеджера и рисует ему наземную тень-квад
	//   (objectMgr_DrawGroundShadowQuads(&g_CharManager_0), render_core.c:1459).
	Mesh->SetCastShadow(true);
}

void AAsdaSoulGuardianActor::BeginPlay()
{
	Super::BeginPlay();
}

// ── §6.1/§6.2/§6.3: создание актёра, загрузка набора, призыв ────────────────────────────────────
void AAsdaSoulGuardianActor::InitGuardian(int32 InSoulGuardIndex, int32 InOwnerUserIdx, AActor* InOwner)
{
	SoulGuardIndex = InSoulGuardIndex;
	OwnerUserIdx = InOwnerUserIdx;
	OwnerChar = InOwner;

	const UAsdaSoulGuardSubsystem* Soul = GetGameInstance()
		? GetGameInstance()->GetSubsystem<UAsdaSoulGuardSubsystem>() : nullptr;
	const FAsdaSoulGuarderRow* R = Soul ? Soul->FindGuarderByIndex(SoulGuardIndex) : nullptr;
	if (!R)
	{
		UE_LOG(LogAsdaSoulGuardian, Warning,
			TEXT("[хранитель] индекс %d: записи в SoulGuarderTable нет — компаньон не собран"), SoulGuardIndex);
		Destroy();
		return;
	}
	Row = *R;
	bRowValid = true;
	// В таблице встречается «sb_md_CB_01», а ассеты импортированы в нижнем регистре.
	ModelName = Row.Model.ToLower();

	const FString MeshPath = UAsdaModelAnimSubsystem::BuildSkelPath(kSoulGuardRoot, ModelName);   // Д8
	if (USkeletalMesh* M = UAsdaModelAnimSubsystem::LoadMeshChecked(MeshPath))                    // Д2
	{
		Mesh->SetSkeletalMesh(M);
	}
	else
	{
		UE_LOG(LogAsdaSoulGuardian, Warning,
			TEXT("[хранитель] модель '%s' не импортирована (%s) — запусти asda_build/import_soulguard_models"),
			*ModelName, *MeshPath);
	}

	// ★ Стартовая альфа 0 (эталон `CCharActor_LoadAnimSet`: self[435] = 0) и разгон 12 α/с.
	// ★ Аудит №4 (B3): общая точка `Begin` (find-or-create) вместо безусловного `NewObject`.
	UAsdaFadeComponent::Begin(this, /*bIn*/true, AppearSec, /*bDestroyOwner*/false, TEXT("SoulGuardFadeIn"));

	// Позиция сразу под владельцем, чтобы первый кадр не мигнул в начале координат.
	if (InOwner) { SetActorLocation(InOwner->GetActorLocation() + FollowOffset); }

	// Эталон: сразу после создания — `Char_SetMotion(mgr, userIdx, 8 /*CALL*/, 0, 0)`.
	SetMotion(8, /*bSpawnFx*/false, /*FxSlot*/0, /*bIgnoreGates*/true);
	// …и эффект `sb_change_01.jeb` на кости ВЛАДЕЛЬЦА — и при первом призыве, и при СМЕНЕ хранителя.
	SpawnOwnerBoundFx(kChangeJeb);

	UE_LOG(LogAsdaSoulGuardian, Log,
		TEXT("[хранитель] создан: индекс=%d («%s») userIdx=%d модель=%s тип=%d ряд=%d"),
		SoulGuardIndex, *Row.Name, OwnerUserIdx, *ModelName, Row.SkillType, Row.RowIndex + 1);
}

// ── §1.4: мягкое удаление — мотион 9 «off» + гашение ────────────────────────────────────────────
void AAsdaSoulGuardianActor::BeginSoftRemove()
{
	if (bRemoving) { return; }
	bRemoving = true;
	// ★★★ УХОД ИДЁТ ЧЕРЕЗ ОБЫЧНЫЙ ГЕЙТ МОТИОНА (симптом «хранитель бьёт и тут же исчезает»).
	//   Эталон, мягкая ветка `charMgr_RemoveSummonById` (`entity_char.c:61092-61094`):
	//       *((_BYTE *)summon + 1744) = 0;
	//       Char_SetMotion(v12, summon[189], 9, 0, 0);        // ← БЕЗ обхода гейтов
	//   а сам `Char_SetMotion@0xFA7560` (`entity_char.c:61135-61139`) начинается с
	//       if (cur != 8 || activeId >= 0x4B1) { if (cur != 9) { if (cur != 5) { …применить… } } }
	//   ⇒ при `cur == 5` (боевой мотион «скилл 1») мотион 9 «off» НЕ ставится ВООБЩЕ: удар доигрывает
	//   целиком, а актёра снимает только альфа-фейд. Порт передавал сюда `bIgnoreGates = true` и
	//   пробивал гейт: в живом логе удар слоя 2 в 16:25:32.953, а «уход, гашение» — в 32.965,
	//   через 12 мс. Сервер шлёт призыв и роспуск почти одновременно (`GameServer.c`: 6156 с тиком
	//   0x24B641 и 6157 AddFlag=0 с тиком 0x24B64C — 11 мс), и на экране компаньона держит РОВНО
	//   этот гейт плюс фейд.
	// ★★★ИСПРАВЛЕНИЕ ДЕФЕКТА ОРИГИНАЛА №2 (симптом юзера «после атаки просто испаряется»).
	//   Гейт выше съедает мотион 9 при `cur == 5`, а при `cur == 10/11` наоборот — СРЕЗАЕТ клип удара
	//   через ~60 мс (сервер шлёт роспуск почти одновременно с призывом: 6161 → 6157 AddFlag=0 → 6156
	//   index=−1 в одном тике). Итог оригинала: ряд 1 «испаряется» без ухода, ряды 2/3 не показывают
	//   удар. Исправляем ОБЕ половины одинаково для всех 27 записей — ждём конца однократного клипа.
	//   Полное обоснование и адреса — шапка `bLegacyOffGate` в .h. Откат: `asda.SoulFx.LegacyOff 1`.
	const bool bLegacyOff = (GSoulFxLegacyOff != 0) || bLegacyOffGate;
	//   Однократные мотионы эталона: 8 (призыв) и 5/10/11 (удары). Всё прочее зациклено и режется сразу.
	const bool bOneShotPlaying = (CurMotion == 8 || CurMotion == 5 || CurMotion == 10 || CurMotion == 11);
	if (!bLegacyOff && bOneShotPlaying)
	{
		OffBlockedByMotion = CurMotion;
		bOffPending = true;
		// Предохранитель: клип обязан кончиться сам (ClipTimeLeft тикает в Tick), но зависать нельзя.
		OffPendingWait = FMath::Max(0.05f, ClipTimeLeft) + 0.5f;
		UE_LOG(LogAsdaSoulGuardian, Log,
			TEXT("[хранитель] уход userIdx=%d ОТЛОЖЕН: доигрывает мотион %d (%.2f с), затем клип «off» целиком")
			TEXT(" — исправление дефекта оригинала, откат asda.SoulFx.LegacyOff 1"),
			OwnerUserIdx, CurMotion, ClipTimeLeft);
		return;   // гашение НЕ стартуем: компаньон остаётся полностью видимым до конца удара
	}
	SetMotion(9, /*bSpawnFx*/false, 0, /*bIgnoreGates*/false);
	StartFadeOut(/*bOffPlaying*/CurMotion == 9);
}

// Однократный мотион доиграл (или сработал предохранитель) — ставим «off» и гасим под его длину.
void AAsdaSoulGuardianActor::FinishPendingOff()
{
	bOffPending = false;
	// Гейт снимаем ОСОЗНАННО: блокировавший клип уже завершён, эталонное «не прерывать» отработало.
	SetMotion(9, /*bSpawnFx*/false, 0, /*bIgnoreGates*/true);
	UE_LOG(LogAsdaSoulGuardian, Log,
		TEXT("[хранитель] userIdx=%d: мотион %d доиграл → клип «off» (мотион %d)"),
		OwnerUserIdx, OffBlockedByMotion, CurMotion);
	StartFadeOut(/*bOffPlaying*/CurMotion == 9);
}

// Гашение = эталонная альфа-ветка `UpdateList_ModelAnim` (actor[435] -= 0.013 * g_FrameTimeScale).
void AAsdaSoulGuardianActor::StartFadeOut(bool bOffPlaying)
{
	// ★ИСПРАВЛЕНИЕ ДЕФЕКТА №1: эталон гасит за 2.05 с, а клип «off» длится 3.00–3.33 с ⇒ уход не доигрывал.
	//   Когда клипа «off» нет (режим оригинала), растягивать гашение не за чем — берём эталонные 2.05 с.
	const float Dur = (bLegacyFadeOut || !bOffPlaying)
		? LegacyFadeOutSec
		: FMath::Max(LegacyFadeOutSec, OffClipLength());
	// ★★★ Аудит №4 (B3): здесь дефект был ОСТРЕЕ всего — уход хранителя идёт по тому же актору,
	//   на котором при появлении уже создавался «SoulGuardFadeIn». Пока фейд-ин не доиграл (2 с),
	//   безусловный `NewObject` вешал ВТОРОЙ компонент, и хранитель оставался полупрозрачным.
	//   `Begin` переиспользует существующий: `BeginFade` корректно перезапускается поверх активного.
	if (!UAsdaFadeComponent::Begin(this, /*bIn*/false, Dur, /*bDestroyOwner*/true, TEXT("SoulGuardFadeOut")))
	{
		Destroy();
	}
	UE_LOG(LogAsdaSoulGuardian, Log, TEXT("[хранитель] уход userIdx=%d: %s, гашение %.2f с%s"),
		OwnerUserIdx,
		bOffPlaying ? TEXT("клип «off»") : TEXT("БЕЗ клипа «off» (гейт мотиона, режим оригинала)"),
		Dur, bLegacyFadeOut ? TEXT(" (режим эталона)") : TEXT(" (исправлено под длину клипа)"));
}

float AAsdaSoulGuardianActor::OffClipLength() const
{
	if (const UAnimSequence* Clip = LoadMotionClip(9)) { return Clip->GetPlayLength() + 0.2f; }
	return LegacyFadeOutSec;
}

// ── §3.2: слот switch → поле записи. Мотион → КЛИП ──────────────────────────────────────────────
UAnimSequence* AAsdaSoulGuardianActor::LoadMotionClip(int32 Motion) const
{
	if (!bRowValid || ModelName.IsEmpty()) { return nullptr; }
	FString ClipBase;
	switch (Motion)
	{
	case 0:  ClipBase = Row.AniIdle0;   break;   // слот 0  → +420  IDLE A
	case 1:  ClipBase = Row.AniIdle1;   break;   // слот 1  → +484  IDLE B
	// мотиона 2 не бывает: слот 2 в switch эталона отдаёт МЕШ (+228), а не клип
	case 3:  ClipBase = Row.AniBattle1; break;   // слот 3  → +612  ATTACK A
	case 4:  ClipBase = Row.AniBattle2; break;   // слот 4  → +676  ATTACK B
	case 5:  ClipBase = Row.AniBattle3; break;   // слот 5  → +740  SKILL 1
	case 6:  ClipBase = Row.AniBattle0; break;   // слот 6  → +548  BATTLE-IDLE
	case 7:  ClipBase = Row.AniMove;    break;   // слот 7  → +804  MOVE
	case 8:  ClipBase = Row.AniCall;    break;   // слот 8  → +292  CALL
	case 9:  ClipBase = Row.AniOff;     break;   // слот 9  → +356  OFF
	case 10: ClipBase = Row.AniSlot10;  break;   // слот 10 → +868  SKILL 2
	case 11: ClipBase = Row.AniSlot11;  break;   // слот 11 → +932  SKILL 3
	default: return nullptr;
	}
	if (ClipBase.Len() <= kClipPrefixLen) { return nullptr; }
	const FString Suffix = ClipBase.RightChop(kClipPrefixLen).ToLower();
	const FString Path = UAsdaModelAnimSubsystem::BuildSkelPath(kSoulGuardRoot, ModelName, Suffix);   // Д8
	return UAsdaModelAnimSubsystem::LoadClipChecked(Path);                                            // Д2
}

// ── §9: мотион → ЗВУК (слот 100 + мотион) ───────────────────────────────────────────────────────
FString AAsdaSoulGuardianActor::MotionSoundName(int32 Motion) const
{
	if (!bRowValid) { return FString(); }
	switch (Motion)
	{
	case 0:  return Row.SndIdle0;       // 100
	case 1:  return Row.SndIdle1;       // 101
	case 3:  return Row.SndAttack0;     // 103
	case 4:  return Row.SndAttack1;     // 104
	case 5:  return Row.SndAttack2;     // 105
	case 6:  return Row.SndBattleIdle;  // 106
	case 7:
		// ★ДЕФЕКТ ОРИГИНАЛА (CONFIRMED дизасмом, §3.2): последний звуковой case в бинаре — `cmp [ebp+0xC], 0x66`
		//   (=102), хотя по содержимому поля это звук ДВИЖЕНИЯ, т.е. должно быть 107 (мотион 7 = MOVE).
		//   Мотион 2 никогда не выставляется (слот 2 — меш) ⇒ звук бега хранителя в оригинале НЕ ИГРАЕТ.
		//   ИСПРАВЛЕНО: читаем поле SndMove. Флаг возвращает эталонное поведение.
		return bLegacyMoveSoundBug ? FString() : Row.SndMove;   // 107
	case 8:  return Row.SndCall;        // 108
	case 9:  return Row.SndOff;         // 109
	case 10: return Row.SndAttack3;     // 110
	case 11: return Row.SndAttack4;     // 111
	default: return FString();
	}
}

// ── §7.1: Char_SetMotion@0xFA7560 ───────────────────────────────────────────────────────────────
void AAsdaSoulGuardianActor::SetMotion(int32 Motion, bool bSpawnFx, int32 FxSlot, bool bIgnoreGates)
{
	if (!bIgnoreGates)
	{
		// Гейты эталона (менять мотион НЕЛЬЗЯ):
		//   cur == 8 — призыв не прерывается (кроме каста скилла хранителя, см. NotifyGuardianSkill);
		//   cur == 9 — уход не прерывается;
		//   cur == 5 — «скилл 1» не прерывается.
		if (CurMotion == 8 || CurMotion == 9 || CurMotion == 5) { return; }
	}
	if (bRemoving && Motion != 9) { return; }   // уходящего компаньона больше ничем не перебиваем

	PrevMotion = CurMotion;
	CurMotion = Motion;

	if (UAnimSequence* Clip = LoadMotionClip(Motion))
	{
		PlayMotionClip(Clip);
	}
	else
	{
		UE_LOG(LogAsdaSoulGuardian, Warning, TEXT("[хранитель] клип мотиона %d не найден (модель %s)"),
			Motion, *ModelName);
		CurClipLen = ClipTimeLeft = 1.f;   // не залипать: через секунду автомат попробует снова
	}

	// ★ Звук — 3D В ПОЗИЦИИ ВЛАДЕЛЬЦА (эталон Audio_Play3DSound(g_pSoundMgr, ВЛАДЕЛЕЦ.worldPos, ...)),
	//   без зацикливания, при КАЖДОЙ смене мотиона.
	const FString Snd = MotionSoundName(Motion);
	if (!Snd.IsEmpty())
	{
		if (UAsdaSoundSubsystem* S = GetGameInstance() ? GetGameInstance()->GetSubsystem<UAsdaSoundSubsystem>() : nullptr)
		{
			const AActor* Own = OwnerChar.Get();
			S->PlaySound3D(FName(*Snd), Own ? Own->GetActorLocation() : GetActorLocation(),
				SoundMinDist, SoundMaxDist, /*bLoop*/false);
		}
	}

	// ★ Эффект скилла — тоже НА ВЛАДЕЛЬЦЕ (кость M_DUMMY/G_DUMMY), слот 30 + вариант = 31/32/33.
	if (bSpawnFx && (Motion == 5 || Motion == 10 || Motion == 11))
	{
		FString Jeb;
		switch (FxSlot)
		{
		case 1: Jeb = Row.EffSkill1; break;
		case 2: Jeb = Row.EffSkill2; break;
		case 3: Jeb = Row.EffSkill3; break;
		default: break;
		}
		if (!Jeb.IsEmpty()) { SpawnOwnerBoundFx(Jeb.ToLower()); }
		// ★ СЛОЙ 2 (наше дополнение, cvar asda.SoulFx.HitFx): собственный «h»-эффект скилла.
		//   Момент выбран тот же, что у слоя 1 — другого в эталоне взять НЕЛЬЗЯ: в клиенте
		//   пользователя поле +0xD1 не читает никто, т.е. эталонного «когда» попросту нет.
		SpawnSkillHitFx();
	}
}

void AAsdaSoulGuardianActor::PlayMotionClip(UAnimSequence* Clip)
{
	if (!Clip || !Mesh) { return; }
	CurClipLen = FMath::Max(0.05f, Clip->GetPlayLength());
	ClipTimeLeft = CurClipLen;
	UAnimInstance* AI = Mesh->GetAnimInstance();
	if (!AI)
	{
		// Скелет ещё не проинициализирован (меш поставлен в этом же кадре) — переинициализируем.
		Mesh->InitAnim(/*bForceReinit*/true);
		AI = Mesh->GetAnimInstance();
	}
	if (!AI)
	{
		UE_LOG(LogAsdaSoulGuardian, Warning, TEXT("[хранитель] AnimInstance не создан — клип не проигран"));
		return;
	}
	// ★ Кросс-фейд ровно CrossFadeSec (эталон 300 мс). Луп — БОЛЬШИМ LoopCount: эталон по концу
	//   трека переиспускает ТОТ ЖЕ клип, если мотион не сменился; конец клипа мы всё равно считаем сами
	//   (ClipTimeLeft), чтобы отработал автомат §7.3 (перевыбор варианта покоя/атаки).
	// ★★ БАГ «замороженный компаньон» (тест 2026-08-07): здесь стоял LoopCount = 0. Движок строит
	//   сегмент монтажа длиной LoopingCount × длина клипа (FAnimSegment::GetLength,
	//   AnimCompositeBase.h:141), а Montage_PlayInternal играет только при GetPlayLength() > 0
	//   (AnimInstance.cpp:2762) ⇒ монтаж НУЛЕВОЙ длины вообще не запускался — компаньон вечно стоял
	//   в реф-позе. ФИКС: большой LoopCount (луп без blend-out; авто-blend-out при таком
	//   LoopCount не срабатывает никогда, смену клипа стыкует Montage_Play со своим BlendIn).
	constexpr int32 kLoopForever = 1000000;
	UAnimMontage* M = AI->PlaySlotAnimationAsDynamicMontage(Clip, FAnimSlotGroup::DefaultSlotName,
		CrossFadeSec, /*BlendOut*/CrossFadeSec, /*PlayRate*/1.f, kLoopForever);
	if (!M)
	{
		UE_LOG(LogAsdaSoulGuardian, Warning, TEXT("[хранитель] монтаж клипа %s не запустился"), *Clip->GetName());
	}
}

void AAsdaSoulGuardianActor::NotifyGuardianSkill(int32 Variant)
{
	// §7.2: rec[+16] == 1 → мотион 5 (эффект 31), 2 → мотион 10 (32), 3 → мотион 11 (33).
	//   Обычный (не хранительский) скилл → мотион 5 БЕЗ эффекта.
	// Каст скилла — единственное, что прерывает мотион 8 (призыв): эталонный гейт
	//   `cur == 8 && !(владелец кастует скилл id >= 0x4B1)`.
	const bool bIgnoreCallGate = (CurMotion == 8);
	// ★ ОТСТУПЛЕНИЕ ПО РЕШЕНИЮ ЮЗЕРА (2026-08-08): «сделай, чтобы asda.SoulFx.Skill применялся не на
	//   себя, а на выбранную цель». Удар хранителя разворачивает ВЛАДЕЛЬЦА к цели — тем же эталонным
	//   механизмом, что и обычная атака (`objAI_FaceTargetByUserIdx@0xA23350`, поле `AttackFaceTarget`).
	//   Почему так, а не «повернуть эффект»: слой 1 живёт в ПОЛНОМ трансформе кости владельца
	//   (attach-10), а компаньон следует матрице владельца ⇒ развернув владельца, мы разворачиваем
	//   и хранителя, и всю композицию эффекта — одним движением и без спец-случая в геометрии.
	//   В эталоне разворота именно на СКИЛЛ ХРАНИТЕЛЯ нет (там курс владельца берётся как есть).
	//   Откат: `asda.SoulFx.FaceTarget 0`.
	FaceOwnerToTarget();
	switch (Variant)
	{
	case 1: SetMotion(5,  true, 1, bIgnoreCallGate); break;
	case 2: SetMotion(10, true, 2, bIgnoreCallGate); break;
	case 3: SetMotion(11, true, 3, bIgnoreCallGate); break;
	default: SetMotion(5, false, 0, bIgnoreCallGate); break;
	}
}

// ★ Разворот владельца к выбранной цели перед ударом хранителя (см. комментарий в NotifyGuardianSkill).
//   Цель берём ТОЛЬКО у своего игрока: у чужих хранителей их цель нам неизвестна (тот же гейт, что в
//   слое 2 — `SpawnSkillHitFx`). Разворачиваем ровно так же, как бой: через `AttackFaceTarget`.
void AAsdaSoulGuardianActor::FaceOwnerToTarget() const
{
	if (GSoulFxFaceTarget == 0) { return; }
	UWorld* W = GetWorld();
	AActor* Own = OwnerChar.Get();
	if (!W || !Own) { return; }
	AAsdaPlayerCharacter* PCh = Cast<AAsdaPlayerCharacter>(Own);
	if (!PCh) { return; }
	const AAsdaPlayerController* PC = Cast<AAsdaPlayerController>(W->GetFirstPlayerController());
	if (!PC || PC->GetPawn() != Own) { return; }      // цель есть только у СВОЕГО игрока
	AActor* Tgt = PC->TargetActor.Get();
	if (!Tgt) { return; }                              // цели нет — бьём как стоим (прежнее поведение)
	const FVector TL = Tgt->GetActorLocation();
	// UE→game: gameX = UE.Y, gameZ = −UE.X. ★ Аудит №3 (B1): общая точка `AsdaCoord`.
	PCh->FaceGameDir(AsdaCoord::UeToGame2D(TL) - PCh->GetGamePosXZ());
	UE_LOG(LogAsdaSoulGuardian, Log,
		TEXT("[хранитель] удар: владелец развёрнут к цели «%s»"), *GetNameSafe(Tgt));
}

void AAsdaSoulGuardianActor::PlayMotionForTest(int32 Motion, bool bIgnoreGates)
{
	// Слот 2 — это МЕШ, мотиона 2 не существует; сообщаем честно, а не молча проигрываем чужой клип.
	if (Motion == 2)
	{
		UE_LOG(LogAsdaSoulGuardian, Warning, TEXT("[хранитель] мотиона 2 не существует (слот 2 = меш модели)"));
		return;
	}
	const int32 FxSlot = (Motion == 5) ? 1 : (Motion == 10 ? 2 : (Motion == 11 ? 3 : 0));
	SetMotion(Motion, FxSlot != 0, FxSlot, bIgnoreGates);
}

// ── общая часть обоих слоёв удара: загрузка BP + спавн + время жизни ───────────────────────────
AAsdaEffectActor* AAsdaSoulGuardianActor::SpawnFxActor(const FString& JebBase,
	const FVector& Loc, const FRotator& Rot) const
{
	UWorld* W = GetWorld();
	if (!W || JebBase.IsEmpty()) { return nullptr; }
	// Д4: единый спавн эффект-BP (имя + LoadClass + SpawnActor) — общий с AsdaWorldSubsystem::SpawnCharFx.
	// Эффекты хранителя ОДНОРАЗОВЫЕ (призыв/удар) — время жизни по самому долгому эмиттеру.
	// ★ Аудит №3 (B5): расчёт живёт в самом акторе (`NaturalLifeSpan`) — данные там же, где и формула.
	// ★ Дедуп №2 В1-C (F12): спавн и срок — общей точкой одноразового эффекта на носителе (поворот — свой, без привязки).
	AAsdaEffectActor* E = AAsdaEffectActor::SpawnOnCarrier(W, JebBase, Loc, OwnerChar.Get(), Rot);
	if (!E)
	{
		UE_LOG(LogAsdaSoulGuardian, Warning,
			TEXT("[хранитель] эффект BP_Fx_%s не собран/не заспавнен (запусти asda_build_char_fx.py)"),
			*JebBase.Replace(TEXT("-"), TEXT("_")));
		return nullptr;
	}
	return E;
}

// ── §8: эффект на кости ВЛАДЕЛЬЦА (не компаньона) ──────────────────────────────────────────────
void AAsdaSoulGuardianActor::SpawnOwnerBoundFx(const FString& JebBase) const
{
	UWorld* W = GetWorld();
	AActor* Own = OwnerChar.Get();
	if (!W || !Own || JebBase.IsEmpty()) { return; }
	// Точка кости владельца M_DUMMY (жен. G_DUMMY ремапится экспортёром в M_DUMMY).
	//   Скелет берём ЯВНО (BaseSkel), а не FindComponentByClass: у персонажа скелеталок много
	//   (BaseSkel + Equip_* с LeaderPose + MountMesh + ClanFlagMesh), порядок обхода OwnedComponents
	//   не определён — та же грабля, что чинили у свечения заточки (AsdaWorldSubsystem.cpp).
	AAsdaPlayerCharacter* PCh = Cast<AAsdaPlayerCharacter>(Own);
	USkeletalMeshComponent* Sk = PCh ? ToRawPtr(PCh->BaseSkel) : Own->FindComponentByClass<USkeletalMeshComponent>();
	FName FxBone = NAME_None;
	FVector Start = Own->GetActorLocation();
	if (Sk)
	{
		if (Sk->DoesSocketExist(kOwnerFxBone))            { FxBone = kOwnerFxBone; }
		else if (Sk->DoesSocketExist(kOwnerFxBoneFemale)) { FxBone = kOwnerFxBoneFemale; }
		if (FxBone != NAME_None) { Start = Sk->GetSocketLocation(FxBone); }
	}
	AAsdaEffectActor* E = SpawnFxActor(JebBase, Start, FRotator::ZeroRotator);
	if (!E) { return; }
	// ★★★ ATTACH-10 (2026-08-08). ЭТАЛОН: обе точки спавна эффектов хранителя передают
	//   Effect_SpawnByName(..., boneIdx("M_DUMMY"/"G_DUMMY"), /*attach*/10, 0, 1.0f):
	//     Char_SetMotion@0xFA7560 (entity_char.c:61165) — удар скилла, слоты 31/32/33;
	//     CCharManager_AddOrUpdateObject@0xFA6B90 (entity_char.c:60973 и :61017) — sb_change_01.
	//   Attach-10 = эффект живёт в ПОЛНОМ трансформе живой кости (позиция И ориентация) — ровно то,
	//   что в порту делает bFollowBoneFull (тот же режим у свечения заточки и у крыльев).
	//   ДО этой правки эффект просто спавнился в точке кости с FRotator::ZeroRotator и НИ ЗА ЧЕМ не
	//   следил ⇒ все квады/кольца строились в МИРОВЫХ осях: полумесяц-слэш лежал горизонтальной
	//   «лепёшкой» в одной и той же стороне света независимо от того, куда повёрнут персонаж
	//   («очень плоское» в тесте юзера), а дрейф pos-канала sp_1 (50,150,0)→(50,150,100) уезжал не
	//   вперёд от персонажа, а по мировой оси.
	//   Откат: asda.SoulFx.Attach10 0 (вернёт прежнее поведение «спавн в точке, без слежения»).
	// ★★★ ПЕРВОПРИЧИНА «ударная волна идёт в сторону от взгляда» (тест юзера 2026-08-08).
	//   Правка attach-10 выше НЕ РАБОТАЛА НИ РАЗУ: гейт слежения в тике эффекта —
	//   `else if (bFollowPosition && FollowActor)` (AsdaEffectActor.cpp:354), а `bFollowPosition`
	//   имеет ДЕФОЛТ false (AsdaEffectActor.h:238 «поставили 1 раз при импорте, двигается руками» —
	//   это про КАРТ-эффекты) и здесь никто его не ставил. Оба других рантайм-носителя attach-10
	//   ставят его явно: свечение заточки (AsdaWorldSubsystem.cpp:3360/4302) и шлейф транспорта
	//   (AsdaPlayerCharacter.cpp:1175). Без него актор эффекта так и оставался с FRotator::ZeroRotator
	//   из SpawnActor ⇒ ВСЕ квады/кольца/частицы строились в МИРОВЫХ осях: у лучника вся композиция
	//   (в данных она вынесена на game-Z = +150..300 «перед лицом») уезжала в одну и ту же сторону
	//   света независимо от того, куда смотрит персонаж.
	//   Проверка направления после фикса (сверено ДВАЖДЫ, см. отчёт «НАПРАВЛЕНИЕ И СЛОИ»):
	//     game-оси = X вбок, Y вверх, **Z ВПЕРЁД** (мера мешей: ботинок ch1_m_00_shoe001 z∈[−8.8;+28.6]
	//     — носок вперёд; лицо ch1_m_00_face001 z∈[−0.3;+14.7]; перчатки — симметрия ровно по X);
	//     цепочка порта `to_ue_v` + FxToBone даёт ровно ту же game→сырой-скелет матрицу, что и
	//     экспорт МЕША (обе = (gx, −gz, gy)) ⇒ эффект садится в тот же кадр, что видимая модель,
	//     а attach-10 добавляет курс владельца. Отдельная «поправка на взгляд» НЕ НУЖНА.
	if (E && GSoulFxAttach10 != 0 && Sk && FxBone != NAME_None)
	{
		// ★ Д1: attach-10 через ЕДИНУЮ точку (режим OwnerBoundFx = boneFull + strictQuad). bFollowPosition
		//   (гейт тика — его забыли ⇒ боевой баг «волна в сторону от взгляда») ставит сам ConfigureBoneAttach.
		//   strictQuad: эталон effectQuad_updateAndDrawBillboard@0x120AEB0 — roll крутит геометрию, трансляция
		//   = basePos + posChan·(R·N48·N520); тик после позы носителя — иначе дрожание. Всё внутри метода.
		E->ConfigureBoneAttach(Own, Sk, FxBone, EAsdaFxAttachMode::OwnerBoundFx);
	}
}

// ── ★ СЛОЙ 2 УДАРА: «h»-эффект скилла (skill_info.sif +0xD1 → DT_SkillFx) ──────────────────────
FString AAsdaSoulGuardianActor::FindSkillHitJeb() const
{
	if (!bRowValid || Row.GuardianSkillId < 0) { return FString(); }
	const TCHAR* const kSkillFxDT = kSkillFxPath;   // путь — объявление в шапке (15.09.2026, dt_gc_hold)
	// ★★★ Аудит №4 (B2/A13): здесь кэша НЕ БЫЛО ВОВСЕ — `LoadObject` звался на КАЖДЫЙ удар
	//   хранителя, то есть в горячем боевом пути. Теперь через общую точку `AsdaDt::Cached`:
	//   загрузка одна, дальнейшие вызовы — поиск в карте.
	const UDataTable* DT = AsdaDt::Cached(kSkillFxDT);
	if (!DT)
	{
		UE_LOG(LogAsdaSoulGuardian, Warning,
			TEXT("[хранитель] DT_SkillFx не собран (Tools/asda_build_skillfx_dt.py) — слой 2 удара пропущен"));
		return FString();
	}
	// Ключ строки = id скилла десятичным (см. asda_build_skillfx_dt.py, колонка `_row`).
	const FName Key(*FString::FromInt(Row.GuardianSkillId));
	if (const FAsdaSkillFxRow* R = DT->FindRow<FAsdaSkillFxRow>(Key, TEXT("SoulGuardHitFx"), /*bWarn*/false))
	{
		return R->HitFx.ToLower();
	}
	return FString();
}

void AAsdaSoulGuardianActor::SpawnSkillHitFx() const
{
	if (!bSpawnSkillHitFx || GSoulFxHitFx == 0) { return; }
	const FString Jeb = FindSkillHitJeb();
	if (Jeb.IsEmpty()) { return; }
	AActor* Own = OwnerChar.Get();
	UWorld* W = GetWorld();
	if (!W || !Own) { return; }

	// Куда сажать. «h»-эффекты центрированы в (0, 0..15, 0) — это ЗЕМЛЯ ПОД ЦЕЛЬЮ, а не носитель
	//   (дамп всех 27: pos-каналы нулевые у 26 из 27). Цель берём ту же, что и сам скилл, —
	//   выбранную игроком (эталон `g_nTargetUserIdx`/`targetRenderId`; у нас AAsdaPlayerController::TargetActor).
	//   (★ 27.09.2026: `Target_GetSelectedUserIdx` — миснейм IDA, = `Char_GetHoveredUserIdx@0x966490`, возвращает userNum ПЕРЕДАННОГО персонажа).
	//   Цели нет (тест `asda.SoulFx.Skill` без цели) — сажаем на владельца, чтобы эффект был виден.
	AActor* Host = nullptr;
	if (!bHitFxOnOwner && GSoulFxHitOnOwner == 0)
	{
		if (const AAsdaPlayerController* PC = Cast<AAsdaPlayerController>(W->GetFirstPlayerController()))
		{
			// Цель валидна только для СВОЕГО игрока: у чужих хранителей их цель нам неизвестна.
			if (PC->GetPawn() == Own) { Host = PC->TargetActor.Get(); }
		}
	}
	const bool bOnTarget = (Host != nullptr);
	if (!Host) { Host = Own; }

	// Ориентация: берём КУРС ВЛАДЕЛЬЦА (удар прилетает от него). Позиция — начало координат хоста
	//   (у наших персонажей и мобов это ноги, а «h»-эффекты как раз центрированы по земле).
	FRotator Rot = FRotator::ZeroRotator;
	if (const AAsdaPlayerCharacter* PCh = Cast<AAsdaPlayerCharacter>(Own))
	{
		Rot = FRotator(0.f, PCh->MeshRoot ? PCh->MeshRoot->GetComponentRotation().Yaw : 0.f, 0.f);
	}
	if (AAsdaEffectActor* E = SpawnFxActor(Jeb, Host->GetActorLocation(), Rot))
	{
		// Импакт живёт в точке попадания и НЕ ездит за целью (эталонной привязки для слоя нет —
		//   в клиенте пользователя его вообще никто не спавнит, см. шапку AsdaSkillFxRows.h).
		E->bFollowPosition = false;
		UE_LOG(LogAsdaSoulGuardian, Log,
			TEXT("[хранитель] слой 2 (удар): %s → %s «%s» (скилл %d)"),
			*Jeb, bOnTarget ? TEXT("ЦЕЛЬ") : TEXT("владелец"), *GetNameSafe(Host), Row.GuardianSkillId);
	}
}

// ── состояние владельца в кодах эталона ────────────────────────────────────────────────────────
int32 AAsdaSoulGuardianActor::ReadOwnerState(const AActor* Own) const
{
	const AAsdaPlayerCharacter* PC = Cast<AAsdaPlayerCharacter>(Own);
	if (!PC) { return 0; }
	if (PC->AnimState == EAsdaPlayerState::Dead) { return 6; }
	// Замах/цикл обычной атаки — эталонное состояние 5 (у нас его признак — активный клип атаки).
	if (PC->IsAttackAnimActive()) { return 5; }
	switch (PC->AnimState)
	{
	case EAsdaPlayerState::Move: return 1;
	case EAsdaPlayerState::Sit:  return 9;
	default:                     return 0;
	}
}

// ── §7.2: реакция на СМЕНУ состояния владельца ─────────────────────────────────────────────────
void AAsdaSoulGuardianActor::OnOwnerStateChanged(int32 NewState)
{
	switch (NewState)
	{
	case 1: SetMotion(7, false, 0); break;                                            // MOVE  → мотион 7
	case 9: SetMotion(6, false, 0); break;                                            // SIT   → боевая стойка
	case 5: SetMotion(FMath::Rand() % 10 >= AttackBThreshold ? 4 : 3, false, 0); break; // атака → ATTACK B/A
	default: break;                                                                    // покой — по концу клипа
	}
	// Состояния 13/14/24/27 (каст скилла) приходят отдельным каналом: для своего игрока —
	//   UAsdaSoulGuardSubsystem::TryUseSkill → NotifyGuardianSkill (там известен ТОЧНЫЙ id скилла,
	//   как и в эталоне, который читает SkillCoinTable[activeId].Field16).
}

// ── §7.3: реакция по КОНЦУ клипа ────────────────────────────────────────────────────────────────
void AAsdaSoulGuardianActor::OnClipFinished()
{
	const AActor* Own = OwnerChar.Get();
	const int32 St = ReadOwnerState(Own);

	// Однократные мотионы: призыв (8) и «скилл 1» (5) по завершении уходят в покой.
	if (CurMotion == 8 || CurMotion == 5)
	{
		PrevMotion = CurMotion;
		CurMotion = 0;
		SetMotion(0, false, 0, /*bIgnoreGates*/true);
		return;
	}
	if (CurMotion == 9) { return; }   // уход — доигрываем до уничтожения актора фейдом

	if (St == 0)
	{
		// Покой: 70 % IDLE A, 30 % IDLE B (эталон rand()%10 >= 3).
		SetMotion(FMath::Rand() % 10 >= IdleAThreshold ? 0 : 1, false, 0);
		return;
	}
	if (St == 5)
	{
		SetMotion(FMath::Rand() % 10 >= AttackBThreshold ? 4 : 3, false, 0);
		return;
	}
	if (St == 1)
	{
		// ★ Бег С ЦЕЛЬЮ → боевая стойка (эталон: targetRenderId != −1 и состояние == 1 → мотион 6).
		const AAsdaPlayerCharacter* PC = Cast<AAsdaPlayerCharacter>(Own);
		const bool bHasTarget = PC && PC->AttackFaceTarget.IsValid();
		SetMotion(bHasTarget ? 6 : 7, false, 0);
		return;
	}
	if (St == 9) { SetMotion(6, false, 0); return; }
	// Прочее — эталон переиспускает тот же трек (у нас клип и так зациклен монтажом).
}

void AAsdaSoulGuardianActor::Tick(float DeltaSeconds)
{
	Super::Tick(DeltaSeconds);
	AActor* Own = OwnerChar.Get();
	if (!Own)
	{
		// Эталон: владельца в списке нет → актёр уничтожается (без «off»-анимации).
		Destroy();
		return;
	}

	// ── §5: жёсткая привязка. world = Translate(0, +30 + высотаТранспорта, 0) × ownerWorldMatrix ──
	FRotator OwnRot = Own->GetActorRotation();
	if (const USceneComponent* OwnMesh = Own->FindComponentByClass<USkeletalMeshComponent>())
	{
		OwnRot = OwnMesh->GetComponentRotation();
		OwnRot.Pitch = 0.f; OwnRot.Roll = 0.f;   // компаньон держится горизонтально
	}
	FVector Offset = FollowOffset;
	FVector BodyLoc = Own->GetActorLocation();
	if (const AAsdaPlayerCharacter* PC = Cast<AAsdaPlayerCharacter>(Own))
	{
		// Посадка на транспорт: эталон прибавляет heightOffset записи спец-предмета (§5).
		Offset.Z += PC->MountHeightOffset;
		// ★ 22.10.2026 (плавность, `World/AsdaBlendAnimInstance.h`): привязка к ТЕЛУ владельца — актор + «хвост» сетевой
		//   поправки (матрица эталона = матрица модели владельца, поворот уже берётся от меша). Ключ 0 — хвоста нет.
		BodyLoc += PC->GetVisualTail();
		// ★ 24.10.2026 [ПОРТ]: в прыжке (донор AsdaFableClassic) тело поднимает клип — хранитель держится тела (asda.Jump.OverheadFollow).
		BodyLoc.Z += PC->GetOverheadLiftZ();
	}
	SetActorLocation(BodyLoc + OwnRot.RotateVector(Offset));
	if (bWorldRotationFollowsOwner) { MeshRoot->SetWorldRotation(FRotator(0.f, OwnRot.Yaw, 0.f)); }

	// Видимость наследуется от владельца (эталон: RenderCtx_SetEnable по флагу рендера владельца).
	// ★★★ [ПОРТ] + метка GM-скрытия (доводка P39, 27.09.2026): свой персонаж оператора под hide НЕ спрятан,
	//   а полупрозрачен (asda.Gm.HideTranslucent), поэтому одного `IsHidden()` владельца мало — без метки
	//   этот тик каждый кадр возвращал бы хранителя, которого `SetCharHiddenByGm` прячет «и для себя».
	static const FName kGmTag(kAsdaGmHiddenTagName);
	SetActorHiddenInGame(Own->IsHidden() || Own->Tags.Contains(kGmTag));

	// ── §7: автомат мотионов ──
	const int32 St = ReadOwnerState(Own);
	if (bFirstFrame) { bFirstFrame = false; LastOwnerState = St; }
	if (St != LastOwnerState)
	{
		LastOwnerState = St;
		if (!bRemoving) { OnOwnerStateChanged(St); }
	}
	if (bOffPending) { OffPendingWait -= DeltaSeconds; }
	if (ClipTimeLeft > 0.f)
	{
		ClipTimeLeft -= DeltaSeconds;
		if (ClipTimeLeft <= 0.f)
		{
			ClipTimeLeft = CurClipLen;   // клип зациклен монтажом; отсчёт следующего прохода
			// ★ Отложенный уход: однократный клип (призыв/удар) доиграл ровно один проход — время «off».
			if (bOffPending)     { FinishPendingOff(); }
			else if (!bRemoving) { OnClipFinished(); }
		}
	}
	// Предохранитель: клипа нет / он не кончается — уход всё равно обязан состояться.
	if (bOffPending && OffPendingWait <= 0.f) { FinishPendingOff(); }
}

FString AAsdaSoulGuardianActor::DumpLine() const
{
	return FString::Printf(
		TEXT("  userIdx=%d индекс=%d («%s») тип=%d ряд=%d модель=%s мотион=%d(пред %d) клип %.2f/%.2f%s"),
		OwnerUserIdx, SoulGuardIndex, bRowValid ? *Row.Name : TEXT("?"),
		bRowValid ? Row.SkillType : -1, bRowValid ? Row.RowIndex + 1 : -1, *ModelName,
		CurMotion, PrevMotion, FMath::Max(0.f, CurClipLen - ClipTimeLeft), CurClipLen,
		bOffPending ? TEXT(" [УХОД ОТЛОЖЕН: доигрывает однократный мотион]")
		            : (bRemoving ? TEXT(" [УХОДИТ]") : TEXT("")));
}

// ═══════════════════════════════════ КОНСОЛЬ (вывод В ИГРУ) ═════════════════════════════════════
//   Юзер запускает команды В ИГРЕ и файл лога не читает, поэтому каждая строка уходит и в лог,
//   и в системный журнал чата — тем же маршрутизатором, что у asda.Mini.* / asda.Soul.*.

static UAsdaWorldSubsystem* SgWorldOf(UWorld* W)
{
	return (W && W->GetGameInstance()) ? W->GetGameInstance()->GetSubsystem<UAsdaWorldSubsystem>() : nullptr;
}

// ★ U13 (В1 дедупа №2): тело — общая точка `AsdaNotice::ConsoleEcho` (прежняя копия была дословной, менялась только категория лога).
#if !UE_BUILD_SHIPPING   // ★ античит А2 (12.10.2026): отладка самотестов/дев-команд — только вне Shipping
static void SgEcho(UWorld* W, const FString& Text)
{
	AsdaNotice::ConsoleEcho(W, Text, LogAsdaSoulGuardian);
}
#endif // !UE_BUILD_SHIPPING

// userIdx локального игрока (ключ реестра компаньонов).
static int32 SgSelfUserIdx(UWorld* W)
{
	const UGameInstance* GI = W ? W->GetGameInstance() : nullptr;
	const UAsdaNetSubsystem* Net = GI ? GI->GetSubsystem<UAsdaNetSubsystem>() : nullptr;
	// E2: до 4003 номер неизвестен ⇒ 0 — ровно прежнее значение (SelfUserNum сбрасывается в 0 вместе с признаком).
	return AsdaSelf::GetUserIdxOr(Net, 0);
}

// Компаньон локального игрока (nullptr + подсказка, если его нет).
static AAsdaSoulGuardianActor* SgSelfGuardian(UWorld* W)
{
	UAsdaWorldSubsystem* WS = SgWorldOf(W);
	return WS ? WS->FindSoulGuardian(SgSelfUserIdx(W)) : nullptr;
}

#if ASDA_DEV_CONSOLE   // ★ античит А2: дев-команда, в Shipping не регистрируется
static FAutoConsoleCommandWithWorldAndArgs GSoulFxDump(
	TEXT("asda.SoulFx.Dump"),
	TEXT("Хранитель-3D: состояние компаньонов (свой + чужие), опции показа, мотион, клип"),
	FConsoleCommandWithWorldAndArgsDelegate::CreateLambda([](const TArray<FString>&, UWorld* W)
	{
		if (UAsdaWorldSubsystem* WS = SgWorldOf(W)) { SgEcho(W, WS->DumpSoulGuardians()); }
	}));
#endif // ASDA_DEV_CONSOLE

// asda.SoulFx.Show <0..26> — показать компаньона СЕБЕ (локальная эмуляция 6156 про своего игрока).
#if ASDA_DEV_CONSOLE   // ★ античит А2: дев-команда, в Shipping не регистрируется
static FAutoConsoleCommandWithWorldAndArgs GSoulFxShow(
	TEXT("asda.SoulFx.Show"),
	TEXT("Хранитель-3D: показать компаньона себе — asda.SoulFx.Show <SoulGuardIndex 0..26>"),
	FConsoleCommandWithWorldAndArgsDelegate::CreateLambda([](const TArray<FString>& A, UWorld* W)
	{
		UAsdaWorldSubsystem* WS = SgWorldOf(W);
		if (!WS) { return; }
		const int32 Idx = A.Num() > 0 ? FCString::Atoi(*A[0]) : 0;
		if (Idx < 0 || Idx > 26)
		{
			SgEcho(W, TEXT("Хранитель-3D: индекс вне 0..26 (27 записей SoulGuarderTable)"));
			return;
		}
		WS->OnSoulGuardIndex(SgSelfUserIdx(W), Idx);
		WS->TickSoulGuardians();
		SgEcho(W, FString::Printf(TEXT("Хранитель-3D: призван индекс %d (userIdx=%d)"), Idx, SgSelfUserIdx(W)));
	}));
#endif // ASDA_DEV_CONSOLE

#if ASDA_DEV_CONSOLE   // ★ античит А2: дев-команда, в Shipping не регистрируется
static FAutoConsoleCommandWithWorldAndArgs GSoulFxHide(
	TEXT("asda.SoulFx.Hide"),
	TEXT("Хранитель-3D: убрать своего компаньона (эмуляция 6156 с индексом −1: клип «off» + гашение)"),
	FConsoleCommandWithWorldAndArgsDelegate::CreateLambda([](const TArray<FString>&, UWorld* W)
	{
		UAsdaWorldSubsystem* WS = SgWorldOf(W);
		if (!WS) { return; }
		WS->OnSoulGuardIndex(SgSelfUserIdx(W), -1);
		WS->TickSoulGuardians();
		SgEcho(W, TEXT("Хранитель-3D: уход (мотион 9 «off» + гашение)"));
	}));
#endif // ASDA_DEV_CONSOLE

// asda.SoulFx.Height <единиц> — высота компаньона над владельцем (локальная ось Z владельца).
//   Эталон = 30 (константы `.rdata@0x1398260`/`+0x14347DC`, дизасм 0xFAA520). ★ По решению юзера
//   (2026-08-07) умолчание = 33 (+10 %). Команда правит ЖИВЫЕ акторы и умолчание для новых.
#if ASDA_DEV_CONSOLE   // ★ античит А2: дев-команда, в Shipping не регистрируется
static FAutoConsoleCommandWithWorldAndArgs GSoulFxHeight(
	TEXT("asda.SoulFx.Height"),
	TEXT("Хранитель-3D: высота над владельцем — asda.SoulFx.Height <единиц> (эталон 30, наше умолчание 95)"),
	FConsoleCommandWithWorldAndArgsDelegate::CreateLambda([](const TArray<FString>& A, UWorld* W)
	{
		if (!W || A.Num() < 1)
		{
			SgEcho(W, TEXT("использование: asda.SoulFx.Height <единиц>   (эталон 30, наше умолчание 95 (подобрано глазами))"));
			return;
		}
		const float H = FCString::Atof(*A[0]);
		int32 N = 0;
		for (TActorIterator<AAsdaSoulGuardianActor> It(W); It; ++It)
		{
			It->FollowOffset.Z = H;
			++N;
		}
		SgEcho(W, FString::Printf(
			TEXT("Хранитель-3D: высота = %.1f (эталон 30; наше умолчание 95) — обновлено акторов: %d"), H, N));
	}));
#endif // ASDA_DEV_CONSOLE

// asda.SoulFx.Set <семейство 1..3> <уровень 1..3> — семейство oh/cb/am и уровень.
//   Индекс записи = (семейство−1)*9 + (уровень−1): SkillType 1/4/7 — первый класс каждой ветки
//   (модель зависит ТОЛЬКО от семейства и уровня, §4 отчёта; имя/иконка/бафф — от класса).
#if ASDA_DEV_CONSOLE   // ★ античит А2: дев-команда, в Shipping не регистрируется
static FAutoConsoleCommandWithWorldAndArgs GSoulFxSet(
	TEXT("asda.SoulFx.Set"),
	TEXT("Хранитель-3D: задать семейство и уровень — asda.SoulFx.Set <1 oh | 2 cb | 3 am> <уровень 1..3>"),
	FConsoleCommandWithWorldAndArgsDelegate::CreateLambda([](const TArray<FString>& A, UWorld* W)
	{
		UAsdaWorldSubsystem* WS = SgWorldOf(W);
		if (!WS || A.Num() < 2)
		{
			SgEcho(W, TEXT("использование: asda.SoulFx.Set <1 oh | 2 cb | 3 am> <уровень 1..3>"));
			return;
		}
		const int32 Fam = FMath::Clamp(FCString::Atoi(*A[0]), 1, 3);
		const int32 Lvl = FMath::Clamp(FCString::Atoi(*A[1]), 1, 3);
		const int32 Idx = (Fam - 1) * 9 + (Lvl - 1);
		WS->OnSoulGuardIndex(SgSelfUserIdx(W), Idx);
		WS->TickSoulGuardians();
		static const TCHAR* kFam[] = { TEXT("oh (ближний бой)"), TEXT("cb (дальний)"), TEXT("am (магия)") };
		SgEcho(W, FString::Printf(TEXT("Хранитель-3D: семейство %s, уровень %d → индекс %d"),
			kFam[Fam - 1], Lvl, Idx));
	}));
#endif // ASDA_DEV_CONSOLE

// asda.SoulFx.Motion <0..11> [обойти гейты 0|1] — проиграть КАЖДУЮ из 12 анимаций.
#if ASDA_DEV_CONSOLE   // ★ античит А2: дев-команда, в Shipping не регистрируется
static FAutoConsoleCommandWithWorldAndArgs GSoulFxMotion(
	TEXT("asda.SoulFx.Motion"),
	TEXT("Хранитель-3D: проиграть мотион — asda.SoulFx.Motion <0..11> [обойти гейты 8/9/5: 0|1] "
	     "(0 покой A, 1 покой B, 3 атака A, 4 атака B, 5 скилл 1, 6 стойка, 7 бег, 8 призыв, 9 уход, 10 скилл 2, 11 скилл 3)"),
	FConsoleCommandWithWorldAndArgsDelegate::CreateLambda([](const TArray<FString>& A, UWorld* W)
	{
		AAsdaSoulGuardianActor* G = SgSelfGuardian(W);
		if (!G)
		{
			SgEcho(W, TEXT("Хранитель-3D: компаньона нет — сначала asda.SoulFx.Show <индекс>"));
			return;
		}
		const int32 M = A.Num() > 0 ? FCString::Atoi(*A[0]) : 0;
		const bool bForce = A.Num() > 1 ? (FCString::Atoi(*A[1]) != 0) : true;
		G->PlayMotionForTest(M, bForce);
		SgEcho(W, FString::Printf(TEXT("Хранитель-3D: мотион %d%s\n%s"),
			M, bForce ? TEXT(" (гейты обойдены)") : TEXT(""), *G->DumpLine()));
	}));
#endif // ASDA_DEV_CONSOLE

// asda.SoulFx.Skill <1|2|3> — боевой вариант: мотион 5/10/11 + эффект слота 31/32/33 на ВЛАДЕЛЬЦЕ.
#if ASDA_DEV_CONSOLE   // ★ античит А2: дев-команда, в Shipping не регистрируется
static FAutoConsoleCommandWithWorldAndArgs GSoulFxSkill(
	TEXT("asda.SoulFx.Skill"),
	TEXT("Хранитель-3D: боевой скилл — asda.SoulFx.Skill <1|2|3> (мотион 5/10/11 + эффект 31/32/33)"),
	FConsoleCommandWithWorldAndArgsDelegate::CreateLambda([](const TArray<FString>& A, UWorld* W)
	{
		AAsdaSoulGuardianActor* G = SgSelfGuardian(W);
		if (!G)
		{
			SgEcho(W, TEXT("Хранитель-3D: компаньона нет — сначала asda.SoulFx.Show <индекс>"));
			return;
		}
		const int32 V = A.Num() > 0 ? FMath::Clamp(FCString::Atoi(*A[0]), 1, 3) : 1;
		G->NotifyGuardianSkill(V);
		SgEcho(W, FString::Printf(TEXT("Хранитель-3D: скилл вариант %d → мотион %d, эффект слота %d"),
			V, V == 1 ? 5 : (V == 2 ? 10 : 11), 30 + V));
	}));
#endif // ASDA_DEV_CONSOLE

#if ASDA_DEV_CONSOLE   // ★ античит А2: дев-команда, в Shipping не регистрируется
static FAutoConsoleCommandWithWorldAndArgs GSoulFxShowSelf(
	TEXT("asda.SoulFx.ShowSelf"),
	TEXT("Хранитель-3D: показывать СВОЕГО компаньона — asda.SoulFx.ShowSelf <0|1> (эталон g_bFxShowSelfAngel, чекбокс 877)"),
	FConsoleCommandWithWorldAndArgsDelegate::CreateLambda([](const TArray<FString>& A, UWorld* W)
	{
		UAsdaWorldSubsystem* WS = SgWorldOf(W);
		if (!WS) { return; }
		if (A.Num() > 0) { WS->SetShowAngel(/*bSelf*/true, FCString::Atoi(*A[0]) != 0); }
		SgEcho(W, FString::Printf(TEXT("Хранитель-3D: показ СВОЕГО = %s"),
			WS->GetShowAngel(true) ? TEXT("вкл") : TEXT("ВЫКЛ")));
	}));
#endif // ASDA_DEV_CONSOLE

#if ASDA_DEV_CONSOLE   // ★ античит А2: дев-команда, в Shipping не регистрируется
static FAutoConsoleCommandWithWorldAndArgs GSoulFxShowOther(
	TEXT("asda.SoulFx.ShowOther"),
	TEXT("Хранитель-3D: показывать компаньонов ЧУЖИХ игроков — asda.SoulFx.ShowOther <0|1> (эталон g_bFxShowOtherAngel, чекбокс 879)"),
	FConsoleCommandWithWorldAndArgsDelegate::CreateLambda([](const TArray<FString>& A, UWorld* W)
	{
		UAsdaWorldSubsystem* WS = SgWorldOf(W);
		if (!WS) { return; }
		if (A.Num() > 0) { WS->SetShowAngel(/*bSelf*/false, FCString::Atoi(*A[0]) != 0); }
		SgEcho(W, FString::Printf(TEXT("Хранитель-3D: показ ЧУЖИХ = %s"),
			WS->GetShowAngel(false) ? TEXT("вкл") : TEXT("ВЫКЛ")));
	}));
#endif // ASDA_DEV_CONSOLE

// asda.SoulFx.Peer <userIdx> <индекс> — эмуляция 6156 ПРО СОСЕДА (проверка компаньона чужого игрока
//   без второго клиента: userIdx берётся из asda.SoulFx.Dump / плашек соседей).
#if ASDA_DEV_CONSOLE   // ★ античит А2: дев-команда, в Shipping не регистрируется
static FAutoConsoleCommandWithWorldAndArgs GSoulFxPeer(
	TEXT("asda.SoulFx.Peer"),
	TEXT("Хранитель-3D: эмулировать 6156 про СОСЕДА — asda.SoulFx.Peer <userIdx> <индекс 0..26, −1 убрать>"),
	FConsoleCommandWithWorldAndArgsDelegate::CreateLambda([](const TArray<FString>& A, UWorld* W)
	{
		UAsdaWorldSubsystem* WS = SgWorldOf(W);
		if (!WS || A.Num() < 2)
		{
			SgEcho(W, TEXT("использование: asda.SoulFx.Peer <userIdx> <индекс 0..26, −1 убрать>"));
			return;
		}
		const int32 Uid = FCString::Atoi(*A[0]);
		const int32 Idx = FCString::Atoi(*A[1]);
		WS->OnSoulGuardIndex(Uid, Idx);
		WS->TickSoulGuardians();
		SgEcho(W, FString::Printf(TEXT("Хранитель-3D: сосед userIdx=%d → индекс %d"), Uid, Idx));
	}));
#endif // ASDA_DEV_CONSOLE
