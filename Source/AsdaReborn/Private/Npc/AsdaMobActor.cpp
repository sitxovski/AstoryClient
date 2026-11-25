// Copyright (c) 2026 @sitxovski (Discord)
// Реализация AAsdaMobActor — см. AsdaMobActor.h.
#include "Npc/AsdaMobActor.h"
#include "AsdaDevConsole.h"   // ★ античит А2: ASDA_DEV_CVAR / ASDA_DEV_CONSOLE — дев-ключи и команды вне Shipping
#include "AsdaPerf.h"              // ★ R4, доля a: область замера признаков контроля моба
#include "World/AsdaPickHitbox.h"   // ★ ДЕДУП Д9: политика и подгонка капсулы пикинга — одна точка на порт
#include "World/AsdaCapsuleShadow.h"   // ★ Сет B: капсульная тень тела — одна точка на порт (игрок/моб/NPC)
#include "AsdaDataTableCache.h"   // ★ Аудит №4 (B2): AsdaDt::Cached — одна ленивая загрузка DataTable на порт
#include "Resources/AsdaModelAnimSubsystem.h"   // Д2/Д9: ресурс только через менеджер
#include "World/AsdaGroundTrace.h"
#include "World/AsdaCoord.h"                    // ★ Аудит №3 (B1/B2/B9): единый перевод координат игра↔UE и тайлы
#include "Components/SkeletalMeshComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Animation/AnimSequence.h"
#include "Animation/AnimSingleNodeInstance.h"
#include "World/AsdaNameplateComponent.h"
#include "World/AsdaEntityText.h"   // ★ волна 3 п.14: имя моба из таблицы — одна точка
#include "Text/AsdaLangRows.h"      // ★ 10.10.2026 (en_completeness): FEditorWorldScope — мир редактора без перевода имени
#include "World/AsdaFadeComponent.h"
#include "World/AsdaWorldSubsystem.h"     // ActorByUserIdx — цель преследования для доводчика
#include "Resources/AsdaSoundSubsystem.h"
#include "Engine/GameInstance.h"
#include "Components/CapsuleComponent.h"
#include "Engine/SkeletalMesh.h"
#include "Engine/StaticMesh.h"
#include "Engine/DataTable.h"
#include "Data/AsdaMobTextureRow.h"
#include "Data/AsdaMobStatsRow.h"        // ★ уровень/агрессия/полу-размер AABB типа моба
#include "Materials/MaterialInterface.h"
#include "Engine/World.h"
#include "TimerManager.h"
#include "EngineUtils.h"                 // TActorIterator — для консольного дампа плашек
#include "HAL/IConsoleManager.h"
#include "UObject/UObjectIterator.h"     // ★ Волна CPU 07.09.2026: обход запечённых мобов при смене ключа сна
#include "World/AsdaCombatTrace.h"       // ★ 18.10.2026: журнал боя — замах/флинч/смерть/труп/бег моба
#include "World/AsdaBlendAnimInstance.h" // ★ 22.10.2026: плавность — смешивание клипов и доворот модели моба
#include "Misc/ScopeExit.h"              // ★ 22.10.2026: доворот на любом выходе из Tick
#include "UI/AsdaEffectsSubsystem.h"     // ★ 26.10.2026 (bowfx): моб под ловушкой/станом/льдом — флаги +6986/+6987 эталона
#include "Npc/AsdaMobFxMaterial.h"         // ★ 01.11.2026 (mob_fx_audit): ключи материалов эффектов и гард вариантов клипов

DEFINE_LOG_CATEGORY_STATIC(LogAsdaMob, Log, All);

// ★ Останавливать ли моба по пакету «стой», когда он ближе эталонного порога 150 юнитов.
//   1 = останавливать всегда (наше исправление дефекта: иначе моб добегает старую траекторию и
//   расходится с сервером на 3-7 тайлов, а следующий отрезок он «пролетает» рывком).
//   0 = эталон: в пределах порога пакет «стой» игнорируется целиком.
// ★ 05.11.2026 [ОРИГИНАЛ] флинч (вход в состояние 13, `CCharacter_SetActionState` case 0xD): `if (prev != 7) { if (prev != 5) ent[282] = ent[144]; … }` —
//   моб запоминает ТЕКУЩУЮ клетку как серверную (её читает доводчик `charCastDirectionalEffectIfFar` на конце клипа). 1 — как эталон;
//   0 — прежний порт (клетка остаётся от последнего серверного пакета, доводчик после флинча мог поставить моба на неё).
static ASDA_DEV_CVAR(int32, CVarMobFlinchRemembersCell, TEXT("asda.Mob.FlinchRemembersCell"), 1,
	TEXT("[ОРИГИНАЛ] Флинч (13) запоминает текущую клетку моба как серверную (эталон ent[282] = ent[144]): 1 — как эталон; 0 — прежний порт."),
	ECVF_Default);

// ★ 07.11.2026 [ИСПРАВЛЕНИЕ] дефект оригинала: моб, снятый с траектории попаданием или станом, не стоит, а добегает отрезок сервера.
//   0 — эталон: стоит до доводчика (мгновенная постановка) или снапа ≥ 3 клеток — видимый телепорт.
static ASDA_DEV_CVAR(int32, CVarMobResumeServerSegment, TEXT("asda.Mob.ResumeServerSegment"), 1,
	TEXT("[ИСПРАВЛЕНИЕ] 1 — после флинча, отлёта и стана моб добегает отрезок сервера; 0 — эталон: стоит, потом рывок на клетку сервера."),
	ECVF_Default);
// ★ 07.11.2026 [ИСПРАВЛЕНИЕ] снап ≥ 3 клеток по 4015 kind=3 — короткий проход. 0 — эталон: мгновенно.
static ASDA_DEV_CVAR(int32, CVarMobSnapAsGlide, TEXT("asda.Mob.SnapAsGlide"), 1,
	TEXT("[ИСПРАВЛЕНИЕ] 1 — расхождение ≥ 3 клеток на ударе моба гасится коротким проходом; 0 — эталон: мгновенный снап."),
	ECVF_Default);
static constexpr float kSegCatchUpMul = 3.f;      // добег не быстрее 3 × скорости сервера
static constexpr float kGlideMinS = 0.12f;        // короткий проход: не короче, с
static constexpr float kGlideMaxS = 0.5f;         // короткий проход: не длиннее, с (замах ещё не дошёл до кадра удара)

static ASDA_DEV_CVAR(int32, CVarMobStopAlwaysHalts,
	TEXT("asda.Mob.StopAlwaysHalts"), 1,
	TEXT("1 = пакет 4015 kind=1 всегда останавливает моба на месте; 0 = эталон (в пределах 150 юнитов игнорируется)."),
	ECVF_Default);

// ★★★ ВОЛНА CPU 07.09.2026 (замер Unreal Insights, cpu_insights_report.md): «СОН» ЗАПЕЧЁННЫХ МОБОВ.
//   Что показал профиль на alpen Epic (область «stand», поток GameThread):
//     FActorComponentTickFunction::ExecuteTick вызывается 1077 раз ЗА КАДР, суммарно ~7 мс/кадр
//     инклюзивно — это и есть «пол» кадра, не зависящий ни от карты, ни от уровня качества.
//   Перепись живого мира тем же прогоном: тикают 1072 компонента, из них 774 принадлежат
//   ЗАПЕЧЁННЫМ мобам (387 USkeletalMeshComponent + 387 UAsdaNameplateComponent).
//   Запечённый моб в ИГРЕ скрыт НАВСЕГДА: игровые мобы — сетевые копии, они спавнятся отдельно
//   (SpawnNetMobDynamic, AsdaWorldSubsystem.cpp:1529), а запечённый служит лишь шаблоном-фолбэком.
//   `SetActorTickEnabled(false)` в BeginPlay гасит ТОЛЬКО тик самого актора; тик-функции его
//   компонентов остаются зарегистрированными в очереди движка и каждый кадр стоят диспетчеризации.
//   Гасим их явно. Гейт значимости волны видимости (asda.Vis.*) этого не покрывал: он выходит
//   рано ВНУТРИ тика, а платить за сам вызов всё равно приходилось.
static TAutoConsoleVariable<int32> CVarBakedDormantTick(
	TEXT("asda.Tick.DormantBaked"), 1,
	TEXT("1 = тик-функции компонентов ЗАПЕЧЁННЫХ (навсегда скрытых) мобов выключены; 0 = поведение до волны CPU 07.09.2026."),
	ECVF_Default);

// Последнее ПРИМЕНЁННОЕ значение ключа. Стартовое 1 совпадает с умолчанием ключа: BeginPlay
// усыпляет запечённых сам, поэтому «применять» на старте нечего.
static int32 GAsdaBakedDormantApplied = 1;

// Включить/выключить тик ТИКАЮЩИХ компонентов моба — ПОИМЁННО (меш + плашка).
//   ★ ДОВОДКА 07.09.2026 (М2 ревизии review_cpu_insights.md): раньше здесь был слепой перебор ВСЕХ
//   компонентов. Он безвреден сегодня (у моба тик возможен только у `Mesh` и `Nameplate`:
//   `StaticMeshComp`/`PickHitbox` заведены с `bCanEverTick=false` по умолчанию своих классов), но
//   принятый в порту паттерн «bCanEverTick=true + bStartWithTickEnabled=false» (AsdaFadeComponent,
//   AsdaTargetHighlightComponent) означал бы: любой будущий компонент моба, заведённый по этому
//   паттерну в конструкторе, при пробуждении был бы ПРИНУДИТЕЛЬНО включён. Теперь трогаем ровно те
//   два, которые и дали измеренные 774 лишних тика (387 мешей + 387 плашек).
//   `SetComponentTickEnabled` сам защищён проверкой `bCanEverTick` (UActorComponent.cpp) — обратный ход безопасен.
static void AsdaSetMobComponentTicks(AAsdaMobActor* Mob, bool bEnabled)
{
	if (!Mob) { return; }
	if (UActorComponent* C = Mob->Mesh)       { C->SetComponentTickEnabled(bEnabled); }
	if (UActorComponent* C = Mob->Nameplate)  { C->SetComponentTickEnabled(bEnabled); }
}

// Пересчёт по изменению ключа: нужен для замера «до/после» ЧЕРЕДУЮЩИМИСЯ СРЕЗАМИ в одном прогоне
//   (закон «один фактор за прогон» + поправка о посторонней нагрузке на машине владельца).
//   Вызывается стоком консольных переменных — то есть только в кадре, где ключ реально менялся.
static void AsdaRefreshBakedDormancy()
{
	const int32 Want = (CVarBakedDormantTick.GetValueOnGameThread() != 0) ? 1 : 0;
	if (Want == GAsdaBakedDormantApplied) { return; }
	GAsdaBakedDormantApplied = Want;
	int32 N = 0;
	for (TObjectIterator<AAsdaMobActor> It; It; ++It)
	{
		AAsdaMobActor* M = *It;
		if (!IsValid(M) || M->bNetSpawned || M->IsTemplate()) { continue; }
		const UWorld* W = M->GetWorld();
		if (!W || !W->IsGameWorld()) { continue; }
		AsdaSetMobComponentTicks(M, Want == 0);
		++N;
	}
	UE_LOG(LogAsdaMob, Log, TEXT("[cpu] сон запечённых мобов = %d, затронуто акторов: %d"), Want, N);
}

// Сток консольных переменных: движок зовёт его в кадре после любого изменения ключа.
static FAutoConsoleVariableSink GAsdaBakedDormantSink(
	FConsoleCommandDelegate::CreateStatic(&AsdaRefreshBakedDormancy));

AAsdaMobActor::AAsdaMobActor()
{
	PrimaryActorTick.bCanEverTick = true;   // тик нужен для интерполяции сетевого движения

	Mesh = CreateDefaultSubobject<USkeletalMeshComponent>(TEXT("MobMesh"));
	SetRootComponent(Mesh);
	// ★ ЗЕРКАЛО (reverse mirror_and_weapon_transform_re.md): игра ПРАВОРУКАЯ (RH), конвертер моделей сделал
	//   лишний флип (det +1 вместо −1) → модели зеркальны (право/лево перепутаны). Рантайм-фикс: негейт Y.
	Mesh->SetRelativeScale3D(FVector(1.f, -1.f, 1.f));
	// ★ 22.10.2026 (плавность): одиночный узел СО СМЕШИВАНИЕМ клипов (`World/AsdaBlendAnimInstance.h`); при
	//   `asda.Anim.BlendSec 0` / `BlendAttackSec 0` — прежний AnimationSingleNode дословно.
	AsdaAnimSmooth::Install(Mesh);
	Mesh->SetCastShadow(true);
	AsdaShadow::ApplyCapsule(Mesh);   // ★ Сет B: непрямая капсульная тень тела моба (ключ asda.Shadow.Capsule)
	Mesh->VisibilityBasedAnimTickOption = EVisibilityBasedAnimTickOption::OnlyTickPoseWhenRendered;   // мобов много

	StaticMeshComp = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("MobStatic"));
	StaticMeshComp->SetupAttachment(Mesh);
	StaticMeshComp->SetCastShadow(true);

	// ★ Хитбокс пикинга: ловит только трейсы видимости (клик/наведение), физику не трогает.
	PickHitbox = CreateDefaultSubobject<UCapsuleComponent>(TEXT("PickHitbox"));
	PickHitbox->SetupAttachment(Mesh);
	AsdaPick::Configure(PickHitbox);   // дедуп Д9: политика коллизий — общая точка
	PickHitbox->SetCapsuleSize(45.f, 80.f);   // подгоняется по габаритам меша в OnConstruction

	// ★ Плашка моба (TARGETMARK_MON): имя всегда, HP — при выборе целью (эталон).
	Nameplate = CreateDefaultSubobject<UAsdaNameplateComponent>(TEXT("Nameplate"));
	Nameplate->SetupAttachment(Mesh);
	Nameplate->Kind = EAsdaNameplateKind::Mob;
	Nameplate->bShowHp = false;
	// ★ ЭТАЛОН: у моба постоянной плашки НЕТ — имя и HP видны только при НАВЕДЕНИИ или когда моб
	//   выбран целью (окно 1600 показывается для выбранной цели, hover — временно). Контроллер
	//   включает/выключает флаг в UpdateHover/SetTarget.
	Nameplate->bAlwaysShowName = false;
}

void AAsdaMobActor::OnConstruction(const FTransform& Transform)
{
	Super::OnConstruction(Transform);
	AsdaShadow::ApplyCapsule(Mesh);   // ★ Сет B: повтор — значение ключа из ini может прийти позже конструктора CDO
	// Хитбокс по фактическим габаритам модели (масштаб актора 0.01 учитывается автоматически).
	if (PickHitbox)
	{
		// Размер — по габаритам МЕША (не актора: иначе капсула измеряет сама себя и вырождается);
		//   вся арифметика — в общей точке `AsdaPick::FitToMeshBounds` (дедуп Д9).
		AsdaPick::FitToMeshBounds(PickHitbox, Mesh, GetActorScale3D());
	}
	ResolveMobStats();   // ★ уровень/агрессия/полу-размер AABB из DT_MobStats (Mob list.BIN)
	if (Nameplate)
	{
		// Имя из DT_MobNames по MobId (+ гард по имени модели); MobName — кэш/фолбэк.
		FString Model;
		if (Mesh && Mesh->GetSkeletalMeshAsset()) { Model = Mesh->GetSkeletalMeshAsset()->GetName(); }
		// ★ 10.10.2026 (en_completeness): актор мира редактора — имя русское даже во время PIE (поле сохраняется в карту).
		const AsdaLangRows::FEditorWorldScope EditorWorldGuard(this);
		FString Nm = AsdaEntityText::MobName(MobId, Model);   // ★ волна 3 п.14: единая точка имён сущностей
		if (Nm.IsEmpty()) { Nm = MobName; } else { MobName = Nm; }
		Nameplate->DisplayName = Nm;
		// ★ Высота: эталонная константа из данных (2·halfY+50); если строки нет — прежний расчёт
		//   по капсуле ПОСЛЕ её подгонки (при динамическом спавне OnRegister считает по дефолт-капсуле
		//   45/80 × scale 0.01 → плашка оказывалась посредине модели).
		ApplyNameplateHeight();
		Nameplate->RefreshNameplate();
	}
	ApplyMobTextures();
}

// ── ★ ЭТАЛОННЫЕ ЧИСЛОВЫЕ ПОЛЯ ТИПА МОБА (DT_MobStats) ─────────────────────────────────────────
// Строка = MobId−1 (та же индексация, что у DT_MobNames/DT_MobSounds/DT_MobTextures).
// Источник каждого поля и адреса аксессоров — в Data/AsdaMobStatsRow.h.
// ★ Аудит №3 (Дд1): единая загрузка `DT_MobStats` и единый резолв строки типа моба.
//   Второй потребитель — `UAsdaSkillFxSubsystem` (размерный класс и процент размера цели) — раньше
//   грузил ту же таблицу своим `LoadObject` и держал ВТОРУЮ карту тех же чисел. Теперь оба идут сюда.
//   Кэш — слабый указатель на сам ассет: пересборка таблицы в редакторе подхватится следующим вызовом.
const FAsdaMobStatsRow* AAsdaMobActor::FindMobStatsRow(int32 InMobId)
{
	if (InMobId <= 0) { return nullptr; }
	// ★ Аудит №4 (B2): свой `static TWeakObjectPtr` заменён общей точкой `AsdaDt::Cached`.
	//   Семантика та же (слабая ссылка, пересборка в редакторе подхватится следующим вызовом),
	//   но кэш один на порт — вторая загрузка той же таблицы стала невозможной.
	//   ⚠ Подсистему звать НЕЛЬЗЯ: этот резолв дёргает `OnConstruction` запечённого моба в редакторе.
	// Строка = MobId − 1 (так её строит Tools/asda_build_mob_stats_dt.py из `Mob list.BIN`, запись 272 б).
	return AsdaDt::RowByInt<FAsdaMobStatsRow>(kMobStatsPath,
	                                          InMobId - 1, TEXT("AsdaMobStats"));
}

void AAsdaMobActor::ResolveMobStats()
{
	if (MobId <= 0) { return; }
	const FAsdaMobStatsRow* R = FindMobStatsRow(MobId);   // ★ Аудит №3 (Дд1): одна загрузка на весь порт
	if (!R) { return; }
	MobLevel      = R->Level;
	MobAggression = R->Aggression;
	PlateHalfY    = R->HalfY;
	// ★★★ Д7/Д8 (2026-08-11): поля типа, которые эталон переносит в сущность при КАЖДОМ спауне.
	//   MaxHp — `sub_932483 → 0xF31040` = `Mob list.BIN +0x94` (`ent[285]`); ранг +0xF8 (`ent+6978`),
	//   «бьёт первым» +0x100 (`ent+6977`), «жизни» +0x10C (`ent+18700` = сегменты полосы HP).
	//   Доказательство переноса — `Recv_MobSpawn@0xF305E0` (network.c:37178-37181) и его дословный
	//   повтор в ветке спауна из пакета удара (`Recv_CombatHit@0xF310E0`, game.c:548217-548222).
	MobTypeMaxHp   = R->MaxHp;
	MobRank        = R->Rank;
	MobAttackFirst = R->AttackFirst;
	MobLives       = R->Lives;
	// ★ Портрет типа для панели цели (`PVP_IMAGE`): имя атласа +0xD4, номер ячейки +0xF4.
	MobPortraitTex  = R->PortraitTex;
	MobPortraitCell = R->PortraitCell;
	if (Nameplate)
	{
		Nameplate->MobLevel            = MobLevel;
		Nameplate->MobAggression       = MobAggression;
		Nameplate->bAutoLevelDiffColor = true;   // ★ моб красится по разнице уровней (эталон)
	}
}

// ★★★ ЭТАЛОННАЯ ВЫСОТА ПЛАШКИ МОБА: H = 2·halfY(Mob list.BIN +0x84+4) + 50.
//   Вывод целиком — в шапке поля PlateHalfY. Возвращает 0, если эталонных данных нет.
float AAsdaMobActor::ResolvePlateHeight() const
{
	if (PlateHeightOverride > 0.f) { return PlateHeightOverride; }
	if (PlateHalfY > 0.f) { return 2.f * PlateHalfY + PlateExtraOffset; }
	return 0.f;
}

void AAsdaMobActor::ApplyNameplateHeight()
{
	if (!Nameplate) { return; }
	// Правило «высота из данных, иначе расчёт по капсуле» одно на порт — оно у самой плашки
	//   (`UAsdaNameplateComponent::ApplyHeightFromData`, дедуп Д13). Своей копии здесь нет.
	const float H = ResolvePlateHeight();
	Nameplate->ApplyHeightFromData(H);
	UE_LOG(LogAsdaMob, Verbose,
		TEXT("[плашка] моб id=%d rid=%d '%s' H=%.1f (источник: %s) | уровень моба=%d, агрессия=%d"),
		MobId, NetRenderId, *MobName, H > 0.f ? H : Nameplate->GetRelativeLocation().Z,
		H > 0.f ? (PlateHeightOverride > 0.f ? TEXT("PlateHeightOverride")
		                                     : TEXT("Mob list.BIN +0x84+4 (halfY): 2*halfY+50"))
		        : TEXT("верх капсулы + оффсет (эталонных данных нет)"),
		MobLevel, MobAggression);
}

// ── ★ ЭТАЛОННЫЕ ТЕКСТУРЫ ПО ТИПУ МОБА (DT_MobTextures) ────────────────────────────────────────
// Оригинал берёт текстуру КАЖДОГО сабмеша не из модели, а по MobId: `<map>.MON` → индекс,
// `<map>.MLK` → пары (A,B) на сабмеш, `<map>.MBB` → имя «\Data\Mob\texture\<имя>.TEX» группы A
// (эталон modelBuildMeshFromCache@0x11A45E0 копирует дескриптор `a8[2] + 32*A` в слот сабмеша +8;
// вызов из mobEffect_LoadArchiveStep@0xF81B90 состояние 0x15; имена групп — CMap_LoadNLK@0x1165760).
// Одна модель обслуживает много типов с разными текстурами (перекраски), поэтому материал ассета
// меша — лишь набор БАЗОВОГО типа, а конкретный тип доводится оверрайдом по имени слота.
void AAsdaMobActor::ApplyMobTextures()
{
	if (MobId <= 0) { return; }
	UMeshComponent* MC = nullptr;
	FString MeshName;
	if (Mesh && Mesh->GetSkeletalMeshAsset())      { MC = Mesh;           MeshName = Mesh->GetSkeletalMeshAsset()->GetName(); }
	else if (StaticMeshComp && StaticMeshComp->GetStaticMesh()) { MC = StaticMeshComp; MeshName = StaticMeshComp->GetStaticMesh()->GetName(); }
	if (!MC) { return; }
	// ★ 01.11.2026 (mob_fx_audit): ключи материалов эффектов — на ЛЮБОМ выходе (и без строки DT: материалы ассета меша).
	ON_SCOPE_EXIT { AsdaMobFx::ApplyKeys(MC); };
	// ★ Аудит №4 (B2): через общую точку `AsdaDt` (строка = MobId−1, как DT_MobNames/DT_MobSounds).
	const FAsdaMobTextureRow* R = AsdaDt::RowByInt<FAsdaMobTextureRow>(
		kMobTexturesPath, MobId - 1, TEXT("ApplyMobTextures"));
	if (!R || R->Slots.Num() == 0 || R->Slots.Num() != R->Materials.Num()) { return; }

	// Гард: строка описывает слоты СВОЕЙ модели — на чужом меше оверрайд не применяем.
	// ★ 01.11.2026 [ИСПРАВЛЕНИЕ] скелетный вариант клипов базы `<меш>__<база>` (30.10, asda.Mob.ClipBaseModel) собран из той же
	//   .jbm, слоты те же — принимается (иначе у 30 мешей пер-типовая текстура не применялась: Чепухон 806 ходил в текстуре
	//   pheseus02 вместо pheseus03). Ключ asda.Mob.TexGuardClipBase (0 — как прежде: вариант отвергается).
	const bool bClipBaseVariant = AsdaMobFx::ClipBaseGuardOn() && !R->Model.IsEmpty()
		&& MeshName.StartsWith(R->Model + TEXT("__"), ESearchCase::IgnoreCase);
	if (!R->Model.IsEmpty() && !MeshName.Equals(R->Model, ESearchCase::IgnoreCase) && !bClipBaseVariant) { return; }

	const TArray<FName> SlotNames = MC->GetMaterialSlotNames();
	for (int32 i = 0; i < SlotNames.Num(); ++i)
	{
		const int32 K = R->Slots.IndexOfByKey(SlotNames[i]);
		if (K == INDEX_NONE) { continue; }
		if (UMaterialInterface* M = R->Materials[K].LoadSynchronous())
		{
			if (MC->GetMaterial(i) != M) { MC->SetMaterial(i, M); }
		}
	}
}

void AAsdaMobActor::BeginPlay()
{
	Super::BeginPlay();
	// ★ Тени ВКЛЮЧЕНЫ (курс «современно», юзер 2026-07-23): CSM 2 каскада + DistanceScale 0.7 режут
	//   стоимость — рисуются только близкие; прежнее принудительное выключение (перф-страх 387 мобов) убрано.
	// ★ Запечённый актор (превью редактора / шаблон клона) — в ИГРЕ скрыт: игровые мобы = сетевые копии
	//   (спавн по 4013, движение по повторным 4013, деспавн 4011), как в оригинале. bCallMob для копий
	//   не важен — сервер сам решает, когда call-моб существует (пакет = существует).
	// ★ Скрытый шаблон — БЕЗ КОЛЛИЗИИ: невидимые капсулы/статик-меши блокировали трейс земли — в местах
	//   стопок call-мобов (.svr: kaiya×15/spade×15 на одном тайле) пробивной цикл GroundZ сгорал на капсулах
	//   и возвращал высоту НАД ними → «невидимые бугры», игроки/мобы взлетали (юзер 2026-07-29).
	ApplyMobTextures();   // ★ пер-типовые текстуры (DT_MobTextures) — и для запечённых, и для сетевых
	if (!bNetSpawned)
	{
		SetActorHiddenInGame(true); SetActorTickEnabled(false); SetActorEnableCollision(false);
		// ★★★ ВОЛНА CPU 07.09.2026: гасим и ТИК КОМПОНЕНТОВ. `SetActorTickEnabled` их не трогает —
		//   тик-функции меша и плашки остаются в очереди движка и стоят диспетчеризации каждый кадр
		//   (Insights: 774 из 1077 вызовов ExecuteTick за кадр на alpen — это запечённые мобы).
		//   Актор скрыт навсегда: сетевые мобы спавнятся отдельно (SpawnNetMobDynamic), а этот
		//   служит лишь шаблоном-фолбэком. Откат — ключ asda.Tick.DormantBaked 0.
		if (CVarBakedDormantTick.GetValueOnGameThread() != 0) { AsdaSetMobComponentTicks(this, false); }
		return;
	}
	// ★ Сетевая копия: тик компонентов ВКЛЮЧЁН явно. В обычном пути это ничего не меняет (умолчание
	//   класса), но клон, спавненный ИЗ ЗАПЕЧЁННОГО ШАБЛОНА (фолбэк AsdaWorldSubsystem.cpp:4802,
	//   `SP.Template = Tmpl`), унаследовал бы усыплённое состояние шаблона — и не анимировался бы.
	AsdaSetMobComponentTicks(this, true);
	// ★ ДОВОДКА 07.09.2026 (В2 ревизии review_cpu_insights.md): СИММЕТРИЯ — будим и тик САМОГО актора.
	//   Разбор по движку: клон спавнится с `SP.Template = Tmpl` (AsdaWorldSubsystem.cpp:4802), а копия
	//   свойств шаблона (FObjectInitializer::InitProperties, UObjectGlobals.cpp:4450) переносит только
	//   UPROPERTY. У FTickFunction отражены лишь `bCanEverTick` и `bStartWithTickEnabled`
	//   (EngineBaseTypes.h:211-217), а РАБОЧИЙ флаг `bTickEnabled` — приватный и НЕ отражён; вдобавок
	//   `TStructOpsTypeTraits<FTickFunction>` объявляет `WithCopy = false` (EngineBaseTypes.h:555), т.е.
	//   сырого memcpy структуры тоже нет. Значит выключенный `SetActorTickEnabled(false)` шаблона клону
	//   НЕ наследуется: `AActor::RegisterActorTickFunctions` (Actor.cpp:1693) включает тик по
	//   `bStartWithTickEnabled` (у класса он true). То же и с компонентами — вызов выше тоже страховочный.
	//   Оставляем ОБА вызова явными: они бесплатны (флаг уже в нужном состоянии), делают пару
	//   «усыпить/разбудить» симметричной и не зависят от неотражённой детали реализации движка.
	SetActorTickEnabled(true);
	if (IdleAnim && Mesh && Mesh->GetSkeletalMeshAsset()) { AsdaAnimSmooth::PlayClip(Mesh, IdleAnim, /*bLoop=*/true, /*бленд*/0.f); }   // ★ 22.10.2026: пробуждение — без бленда
}

// ★ ДОВОДКА 07.09.2026 (В1 ревизии review_cpu_insights.md): ПУБЛИЧНОЕ пробуждение запечённого моба.
//   Дев-команда `asda.Target.RingNearest mob` (AsdaPlayerController.cpp) ПОКАЗЫВАЕТ запечённого актора,
//   чтобы владелец глазами проверил посадку кольца цели и высоту плашки. После волны «сна запечённых»
//   у такого актора усыплены меш и плашка — он был бы виден, но без позы и без имени над головой.
//   Команда зовёт этот помощник рядом с `SetActorHiddenInGame(false)`. На игровой путь не влияет.
void AAsdaMobActor::SetDormantComponentTicks(bool bEnabled)
{
	AsdaSetMobComponentTicks(this, bEnabled);
	SetActorTickEnabled(bEnabled);   // у запечённого тик актора выключен BeginPlay'ем — тоже возвращаем
}

// Клип моба по индексу (anim_NN у скелетал-версии). Имя ассета = <mesh-path>anim_NN.
// ★ 30.10.2026 [ОРИГИНАЛ] слот → файл клипа моба. Ключ отката `asda.Mob.ClipSlotMap 0` — слоты 0–8 играют файл с тем же номером
//   (прежний порт: замах варианта 1 = `_6`, крит/промах = вариантный клип).
static int32 GAsdaMobClipSlotMap = 1;
static FAutoConsoleVariableRef CVarAsdaMobClipSlotMap(TEXT("asda.Mob.ClipSlotMap"), GAsdaMobClipSlotMap,
	TEXT("1 — клипы моба по таблице слотов оригинала (Effect_BuildJbbSuffixByType@0xF717C0): слот 6 (замах варианта 1) = файл _5, ")
	TEXT("крит и коды удара < −1 — слот 7 (Combat_ApplyDamage@0xF7C470); 0 — прежний порт (слот = номер файла, замах по варианту)."),
	ECVF_Default);

int32 AAsdaMobActor::ClipFileForSlot(int32 Slot)
{
	if (GAsdaMobClipSlotMap == 0 && Slot >= 0 && Slot <= 8) { return Slot; }
	switch (Slot)
	{
	case 0: case 1: case 2: case 3: case 4: case 5: return Slot;
	case 6:  return 5;     // загрузчик: `push 5` @0xF81E52 → слот 6 (файл `_6.jbb` моб не грузит никогда)
	case 7:  return 7;
	case 8:  return 8;
	case 14: return 9;
	case 15: return 10;
	case 18: case 19: case 20: case 21: return Slot - 7;
	default: return -1;   // 9–13, 16, 17 — у моба пусто
	}
}

void AAsdaMobActor::PlayClip(int32 ClipIdx, bool bLoop, float Rate)
{
	// ★ 30.10.2026 навык моба: любой клип = смена состояния (эталон `SetActionState`), навык 15/16/17 им перебит.
	++ClipSerial;
	if (!bPlayingSkillClip) { SkillActionState = 0; }
	if (!Mesh || !Mesh->GetSkeletalMeshAsset()) { return; }
	const int32 File = ClipFileForSlot(ClipIdx);   // ★ 30.10.2026: слот → файл клипа (6 → _5 и т.д.)
	if (File < 0) { return; }                      // пустой слот оригинала — клип не меняется
	const FString MeshPath = Mesh->GetSkeletalMeshAsset()->GetPathName();   // .../SkeletalMeshes/<name>.<name>
	FString Base = MeshPath;
	int32 Dot; if (Base.FindChar('.', Dot)) { Base = Base.Left(Dot); }      // убрать .<obj>
	const FString ClipPath = FString::Printf(TEXT("%sanim_%02d"), *Base, File);
	if (UAnimSequence* Clip = UAsdaModelAnimSubsystem::LoadClipChecked(ClipPath))   // Д2
	{
		// ★ 22.10.2026 (плавность): смена клипа со смешиванием позы. Замах (`ClipAttackBase`…+2) — короткий бленд эталона
		//   (`asda.Anim.BlendAttackSec`, [ОРИГИНАЛ] 60 мс), прочие — `asda.Anim.BlendSec`. Время клипа не трогается.
		const bool bAttackClip = (ClipIdx >= ClipAttackBase && ClipIdx <= ClipAttackBase + 2);
		AsdaAnimSmooth::PlayClip(Mesh, Clip, bLoop, bAttackClip ? AsdaAnimSmooth::BlendAttackSec() : AsdaAnimSmooth::BlendSec());
		// ★ Темп клипа = эталонный множитель времени анимации (ent[122] для движения, ent[4826] для
		//   замаха). Ставим ВСЕГДА — иначе прошлый темп протёк бы в следующий клип.
		Mesh->SetPlayRate(FMath::Max(0.01f, Rate));
	}
}

// ★★★ ЗВУКИ МОБОВ ТОЛЬКО В ОБЗОРЕ ОРИГИНАЛА [ПОРТ — компенсация серверного AoI 256] (разбор palace_of_queen).
//   В оригинале сервер рассылает мобов по сетке секторов 30×30 тайлов (MapInfo.txt, 3×3 соседа), а клиент
//   держит лишь мобов с сектор-дистанцией ≤ 1 (entityListUpdateFade@0xF78660: сектор = world/3000) —
//   дальних мобов у него просто НЕТ, и их шаги не звучат. Гейта по дальности у самого звука шага эталон не
//   имеет (effect_UpdateProjectileTick@0xF7BDD0: условия только «жив» ent[285]>1 и два флага-заморозки).
//   У нас серверный сектор 256 → клиент знает мобов всей карты (в `palace_of_queen.svr` 248 записей
//   против ~39 в окне оригинала — пересчитано 17.09.2026, прежняя цифра «83» была неверной), и каждый
//   звучит. Гейт возвращает ровно ту слышимую картину, что была у игрока оригинала. 0 = звучат все мобы.
static int32 GAsdaMobSoundAoiGate = 1;
static FAutoConsoleVariableRef CVarAsdaMobSoundAoiGate(
	TEXT("asda.Sound.MobAoiGate"),
	GAsdaMobSoundAoiGate,
	TEXT("1 = звуки моба (шаги, рык, удар, смерть) только в обзоре оригинала: сектор 30 тайлов, дистанция ≤ 1; 0 = все мобы карты."),
	ECVF_Default);

void AAsdaMobActor::PlayMobSound(int32 Phase)
{
	if (SoundBase.IsEmpty()) { return; }
	UGameInstance* GI = GetGameInstance();
	UAsdaSoundSubsystem* Snd = GI ? GI->GetSubsystem<UAsdaSoundSubsystem>() : nullptr;
	if (!Snd) { return; }
	if (GAsdaMobSoundAoiGate != 0)
	{
		const UAsdaWorldSubsystem* WS = GI->GetSubsystem<UAsdaWorldSubsystem>();
		const AActor* Me = WS ? WS->LocalPlayerActor.Get() : nullptr;
		// ★ 17.09.2026: FAIL-CLOSED. Раньше проверка стояла под `if (IsValid(Me) && …)`, и пока
		//   локальный игрок не выставлен (вход в мир, смена карты, офлайн-крюк), гейт молча пропускал
		//   звуки ВСЕХ мобов карты — на palace_of_queen это 248 записей против 39 в окне оригинала.
		//   Не знаем, где игрок, — не звучим: гейт для того и заведён.
		if (!IsValid(Me) || Me->GetWorld() != GetWorld()) { return; }
		{
			// Сектор оригинала = 30 тайлов (world/3000); сравнение по Чебышёву, как в эталонном AoI-деспауне.
			constexpr float kSectorUnits = 30.f * AsdaCoord::kTileUnits;
			const FVector2D P = AsdaCoord::UeToGame2D(Me->GetActorLocation());
			const FVector2D M = AsdaCoord::UeToGame2D(GetActorLocation());
			const int32 DX = FMath::Abs(FMath::FloorToInt(P.X / kSectorUnits) - FMath::FloorToInt(M.X / kSectorUnits));
			const int32 DZ = FMath::Abs(FMath::FloorToInt(P.Y / kSectorUnits) - FMath::FloorToInt(M.Y / kSectorUnits));
			if (DX > 1 || DZ > 1) { return; }   // вне обзора оригинала — такого моба у игрока не было бы
		}
	}
	// Эталон Sound_BuildEntryWavName: "<база>_<фаза>.wav" (фаза 11 → _99).
	const FString Nm = FString::Printf(TEXT("%s_%d"), *SoundBase, Phase == 11 ? 99 : Phase);
	// ★ 30.10.2026: класс — по фазе (шаги 1/2, удары 5–10, навыки 11–14, голос — прочие; правило `ClassifyByName`), размер — по
	//   приставке mon_s/m/b/l; источник — сам моб (не больше N голосов класса на моба: серия шагов не копится).
	Snd->PlaySound3DAs(FName(*Nm), GetActorLocation(), EAsdaSoundClass::Auto, this);
}

float AAsdaMobActor::TerrainZ(float UeX, float UeY) const
{
	// ★ Высота ЗЕМЛИ (SM_terrain*), НЕ первого хита: одиночный трейс сажал мобов на кроны деревьев.
	return AsdaGround::GroundZ(GetWorld(), UeX, UeY, GetActorLocation().Z, this);
}

void AAsdaMobActor::FaceGameDir(const FVector2D& DirGame)
{
	if (DirGame.IsNearlyZero() || !Mesh) { return; }
	// ★ Аудит №3 (B9): курс из игрового направления — общая математика `AsdaCoord`. Доворот модели
	//   (`MeshYawOffset`) и НАБОР крутимых компонентов у моба свои: скелетный меш + статический.
	const float Yaw = AsdaCoord::FaceYawFromGameDir(DirGame) + MeshYawOffset;
	// ★ 22.10.2026 (плавность): логический курс — сразу; видимый меш доворачивается в `TickTurnSmooth`
	//   ([ОРИГИНАЛ] `charUpdateRotateAndFx@0xF86CB0`: `ent[130]` идёт к `ent[142]` шагом 0.0062831854 рад/мс = 360 °/с).
	//   Сразу (как прежде) — при ключе 0, у скрытого/спящего моба и в первые 0.25 с жизни (спавн).
	FacingYawLogical = FRotator::NormalizeAxis(Yaw);
	bFacingYawSet = true;
	const bool bSnap = AsdaAnimSmooth::MobTurnDegPerSec() <= 0.f || IsHidden() || !IsActorTickEnabled() || !HasActorBegunPlay()
		|| GetGameTimeSinceCreation() < 0.25f;
	if (!bSnap) { return; }
	Mesh->SetWorldRotation(FRotator(0.f, Yaw, 0.f));
	if (StaticMeshComp) { StaticMeshComp->SetWorldRotation(FRotator(0.f, Yaw, 0.f)); }
}

// ★ 22.10.2026: логический курс меша (до первого поворота — поворот меша).
float AAsdaMobActor::GetFacingYawLogical() const
{
	if (bFacingYawSet) { return FacingYawLogical; }
	return Mesh ? Mesh->GetComponentRotation().Yaw : GetActorRotation().Yaw;
}

// ★ 22.10.2026: повернуть сразу — логический и видимый курс.
void AAsdaMobActor::SnapFaceGameDir(const FVector2D& DirGame)
{
	if (DirGame.IsNearlyZero() || !Mesh) { return; }
	const float Yaw = AsdaCoord::FaceYawFromGameDir(DirGame) + MeshYawOffset;
	FacingYawLogical = FRotator::NormalizeAxis(Yaw);
	bFacingYawSet = true;
	Mesh->SetWorldRotation(FRotator(0.f, Yaw, 0.f));
	if (StaticMeshComp) { StaticMeshComp->SetWorldRotation(FRotator(0.f, Yaw, 0.f)); }
}

// ★ 22.10.2026 (плавность): доворот видимого меша (и статического меша моба) к логическому курсу.
void AAsdaMobActor::TickTurnSmooth(float DeltaSeconds)
{
	if (!bFacingYawSet || !Mesh) { return; }
	const float Cur = Mesh->GetComponentRotation().Yaw;
	if (FMath::IsNearlyZero(FMath::FindDeltaAngleDegrees(Cur, FacingYawLogical), 0.01f)) { return; }
	const float NewYaw = AsdaAnimSmooth::StepYaw(Cur, FacingYawLogical, AsdaAnimSmooth::MobTurnDegPerSec(), DeltaSeconds);
	Mesh->SetWorldRotation(FRotator(0.f, NewYaw, 0.f));
	if (StaticMeshComp) { StaticMeshComp->SetWorldRotation(FRotator(0.f, NewYaw, 0.f)); }
}

void AAsdaMobActor::NetMove(const FVector2D& CurGame, const FVector2D& DstGame, float SpeedUnitsPerMs)
{
	if (bDead) { return; }
	// ★ Эталон Recv_CombatHit kind=1/2: `if (state==13||state==14) return` — ТОЛЬКО хит-флинч.
	//   Состояния замаха (3) в гейте НЕТ: move-пакет прерывает замах через SetActionState(target,2).
	if (IsMoveGated())
	{
		ASDA_CTRACE_V(EAsdaCT::Move, TEXT("моб rid=%d '%s': ход НЕ принят — идёт клип попадания (state 13)"), NetRenderId, *MobName);
		return;
	}
#if ASDA_COMBAT_TRACE
	if (IsAttackAnimActive())
	{
		ASDA_CTRACE_MOB(NetRenderId, EAsdaCT::MobAttack, TEXT("моб rid=%d '%s': замах ПРЕРВАН пакетом хода (SetActionState 2) → (%.0f,%.0f)"),
			NetRenderId, *MobName, DstGame.X, DstGame.Y);
	}
#endif
	AttackAnimUntilS = 0.0;   // SetActionState(2) прерывает клип замаха (эталон)
	bAnimGateWas = false;     // замах снят намеренно — конец клипа НЕ наступил, буфер не сливаем
	ASDA_CTRACE_V(EAsdaCT::Move, TEXT("моб rid=%d '%s': ход (%.0f,%.0f) → (%.0f,%.0f) v=%.3f юн/мс%s"), NetRenderId, *MobName,
		CurGame.X, CurGame.Y, DstGame.X, DstGame.Y, SpeedUnitsPerMs, bCombatStance ? TEXT(", выход из боевой стойки") : TEXT(""));
	NetMoveState = 2; bCombatStance = false;   // эталон projInitTrajectoryToTarget: ent[120]=2, ent[237]=0
	MoveDstGame = DstGame;
	MoveSpeed = (SpeedUnitsPerMs > 0.f) ? SpeedUnitsPerMs : 0.35f;   // ★ нормированная скорость бывает малой — не подменяем
	// ★ НЕ снапить в серверный cur на каждый AoI-пакет: между апдейтами актор идёт сам, и постоянный
	//   SetActorLocation(cur) давал «телепорты между позициями». Снап — только при большом рассинхроне.
	const FVector L = GetActorLocation();
	const FVector2D NowGame = AsdaCoord::UeToGame2D(L);   // ★ Аудит №3 (B1)
	if (FVector2D::Distance(NowGame, CurGame) > AsdaCoord::kDesyncSnapUnits)   // ★ Аудит №3 (B9): порог 150
	{
		const FVector2D UeXY = AsdaCoord::GameToUe2D(CurGame);   // ★ Аудит №3 (B1)
		SetActorLocation(FVector(UeXY.X, UeXY.Y, TerrainZ(UeXY.X, UeXY.Y)));
	}
	FaceGameDir(DstGame - ((FVector2D::Distance(NowGame, CurGame) > AsdaCoord::kDesyncSnapUnits) ? CurGame : NowGame));
	if (!bMoving) { PlayClip(ClipMove, true, MoveAnimRate); MoveClipLenS = CurrentClipLength(); MoveClipTimeS = 0.f; bStepSoundDue = true; }
	bMoving = true;
}

// ★★★ 1:1 с эталоном Recv_CombatHit@0xF310E0 case 1 (kind=1, «стоп»), дизасм 0xF31190..0xF312B4:
//     if (GetActionState==13 || ==14) return;                 // хит-флинч — пакет игнорируется
//     dist² = Math_DistanceSquared3D(CMap_GetTileAt(@7,@9), ТЕКУЩАЯ позиция);
//     fcomp flt_13AB6A8 (=22500.0 = 150²); if (dist² <= 22500) → ВЫХОД БЕЗ ЕДИНОГО ДЕЙСТВИЯ;
//     иначе: снятие с тайл-грида при state 1|3, SetActionState(target,1), playerSetWorldPosition(тайл).
//   Ключевое отличие от прежнего порта: при рассинхроне ≤150 оригинал НЕ ДЕЛАЕТ НИЧЕГО — он даже не
//   останавливает моба и не тянет его к серверной клетке. Моб продолжает свою траекторию и сам встаёт
//   по прибытии в dst. «Мягкий стоп» порта (дойти до cur) был отсебятиной: он сбивал цель движения на
//   промежуточную точку и портил ход, а расхождение всё равно не лечил.
void AAsdaMobActor::NetStop(const FVector2D& CurGame)
{
	if (bDead) { return; }
	if (IsMoveGated())   // эталон: гейт только state 13/14 (хит)
	{
		ASDA_CTRACE_V(EAsdaCT::Move, TEXT("моб rid=%d '%s': «стой» НЕ принят — идёт клип попадания (state 13)"), NetRenderId, *MobName);
		return;
	}
	const FVector L = GetActorLocation();
	const FVector2D NowGame = AsdaCoord::UeToGame2D(L);   // ★ Аудит №3 (B1)
	// ★ Аудит №3 (B9): тот же порог рассинхрона, что и у NetMove/игрока — `kDesyncSnapUnits` = 150 (150² = 22500).
	const float DistToServer = FVector2D::Distance(NowGame, CurGame);
	// ★★★ ИСПРАВЛЕНИЕ ДЕФЕКТА ОРИГИНАЛА — «МОБ ПРОБЕГАЕТ МИМО СВОЕЙ ОСТАНОВКИ».
	//   Эталон `Recv_CombatHit@0xF310E0` case 1 (network.c:37556-37581) держит И остановку
	//   (`CCharacter_SetActionState(target, 1)`), И перестановку позиции ВНУТРИ одного условия
	//   `dist² > flt_13AB6A8 (=150²)`. То есть пока моб ближе 150 юнитов к серверной клетке, пакет
	//   «стой» не делает НИЧЕГО — актор продолжает идти по старой траектории до её конца.
	//
	//   Чем это оборачивается (живой лог 24.08.2026, моб rid=253 'Ножепрыг-дозорный'):
	//     15:27:50 «иду (94,64) → (83,59)», скорость 100/1250 ⇒ отрезок 15.1 с;
	//     15:27:56 «стой, я на (89,62)» — сервер передумал на 6-й секунде, мы в 40 юнитах, молчим;
	//     наш актор идёт дальше до (83,59) и к следующему пакету расходится на 3.5-6.7 тайла;
	//     15:28:01 «иду (89,62) → (89,67)» — нормировка скорости по времени отрезка гонит актора
	//     из (83,59) в (89,67) вдвое быстрее обычного. Это и видно как рывок/телепорт.
	//
	//   Останавливаемся ВСЕГДА и ровно там, где стоим (без перестановки). Порог 150 остаётся
	//   эталонным и по-прежнему решает ТОЛЬКО одно — ставить ли актора на серверную клетку.
	//   Ключ отката к эталонному поведению: `asda.Mob.StopAlwaysHalts 0`.
	const bool bSnap = DistToServer > AsdaCoord::kDesyncSnapUnits;
#if ASDA_COMBAT_TRACE
	if (IsAttackAnimActive() && (bSnap || (CVarMobStopAlwaysHalts.GetValueOnGameThread() != 0 && bMoving)))
	{
		ASDA_CTRACE_MOB(NetRenderId, EAsdaCT::MobAttack, TEXT("моб rid=%d '%s': замах ПРЕРВАН пакетом «стой» (SetActionState 1), до серверной клетки %.0f юн"),
			NetRenderId, *MobName, DistToServer);
	}
	ASDA_CTRACE_V(EAsdaCT::Move, TEXT("моб rid=%d '%s': «стой» у (%.0f,%.0f), до серверной клетки %.0f юн (порог %.0f) → %s"),
		NetRenderId, *MobName, CurGame.X, CurGame.Y, DistToServer, (float)AsdaCoord::kDesyncSnapUnits,
		bSnap ? TEXT("снап на клетку + покой")
			: (CVarMobStopAlwaysHalts.GetValueOnGameThread() == 0 ? TEXT("эталон: ничего")
			: (bMoving ? TEXT("стоп на месте + покой") : TEXT("уже стоит"))));
#endif
	if (!bSnap)
	{
		if (CVarMobStopAlwaysHalts.GetValueOnGameThread() == 0) { return; }   // эталон: ничего
		if (!bMoving) { return; }                                             // уже стоим
		AttackAnimUntilS = 0.0;
		bAnimGateWas = false;
		bMoving = false;
		NetMoveState = 2;
		EnterIdle();
		return;
	}
	AttackAnimUntilS = 0.0;   // SetActionState(1) прерывает замах (эталон)
	bAnimGateWas = false;
	bGlide = false;   // ★ 07.11.2026: постановка на клетку отменяет короткий проход
	const FVector2D UeXY = AsdaCoord::GameToUe2D(CurGame);   // ★ Аудит №3 (B1)
	SetActorLocation(FVector(UeXY.X, UeXY.Y, TerrainZ(UeXY.X, UeXY.Y)));
	bMoving = false;
	NetMoveState = 2;
	EnterIdle();   // эталон SetActionState(1): клип 0 + флаг «в бою» = 0
}

// ★ R5-fix2 (13.11.2026, уточнение по декомпилу): поле `entity[116]` (+0x1D0) — НЕ «категория сущности», а `loadSubState`,
//   стадия покадровой загрузки ресурсов моба (`mobEffect_LoadArchiveStep`, npc_mob_pet.c:8951-9315; 23 = загрузка завершена;
//   ida/structs_ru.h:578). Условие `entity[116] != 23` значит «моб ЕЩЁ ГРУЗИТСЯ»: такого (он не рисуется) эталон ставит в клетку
//   пакета, а загруженного не переставляет и ведёт «догоняющей» скоростью. В порту моб готов сразу при создании, поэтому
//   ветка «не переставлять» — единственная, и вывод ниже («позиция из пакета не применяется») остаётся верным.
// ★★★ Эталон Recv_MobSpawnLoop@0xF30330 (пакет 4019) для МОБА:
//   `if (entity[116] != 23) playerSetWorldPosition(...)`  — 23 = моб загружен (см. уточнение выше) ⇒ позиция из пакета НЕ
//   применяется вообще; затем `if (!entity[237]) projInitTrajectoryToTarget(entity, dstTile, 100.0/spd)`
//   — траектория строится ИЗ ТЕКУЩЕЙ мировой позиции сущности. Никакого снапа на cur-тайл.
//   Порт раньше звал сюда NetStop(cur) и телепортировал РАЗНЫХ мобов в одну и ту же клетку.
// ★ «СТОЯТЬ»: снять цель движения и встать в idle, НЕ трогая позицию (эталон при cur==dst делает
//   ровно CCharacter_SetActionState(mob, 1) — ни снапа, ни траектории).
// ★ Безусловный снап (эталон: 4013 всегда зовёт playerSetWorldPosition на cur-тайл, и для НОВОЙ,
//   и для СУЩЕСТВУЮЩЕЙ сущности — общий хвост Recv_MobSpawn, вызов @0xF30899). Порога нет.
void AAsdaMobActor::NetSnapPos(const FVector2D& CurGame)
{
	bGlide = false;   // ★ 07.11.2026: прямая постановка отменяет короткий проход
	const FVector2D UeXY = AsdaCoord::GameToUe2D(CurGame);   // ★ Аудит №3 (B1)
	SetActorLocation(FVector(UeXY.X, UeXY.Y, TerrainZ(UeXY.X, UeXY.Y)));
}

// ★ 07.11.2026 [ИСПРАВЛЕНИЕ] разбор в объявлении.
void AAsdaMobActor::NoteServerMovePacket(uint8 Kind, const FVector2D& CurGame, const FVector2D& DstGame, float SpeedUnitsPerMs, float AnimRate)
{
	const bool bMove = (Kind == 2 || Kind == 5 || Kind == 7 || Kind == 0xE) && SpeedUnitsPerMs > 1e-6f && !CurGame.Equals(DstGame, 1.f);
	if (!bMove || CVarMobResumeServerSegment.GetValueOnGameThread() == 0) { bSegActive = false; return; }
	bSegActive = true;
	SegState = Kind;
	SegDstGame = DstGame;
	SegSrvSpeed = SpeedUnitsPerMs;
	SegAnimRate = (AnimRate > 0.f) ? AnimRate : 1.f;
	SegRemainS = FVector2D::Distance(CurGame, DstGame) / SpeedUnitsPerMs / 1000.f;
}

float AAsdaMobActor::SegResumeSpeed(float DistUnits) const
{
	const float Vs = FMath::Max(1e-3f, SegSrvSpeed);
	const float RemMs = SegRemainS * 1000.f;
	const float V = (RemMs > 1.f) ? DistUnits / RemMs : kSegCatchUpMul * Vs;   // сервер уже пришёл — предельный добег
	return FMath::Clamp(V, Vs, kSegCatchUpMul * Vs);
}

void AAsdaMobActor::ResumeServerSegment()
{
	const FVector2D NowGame = AsdaCoord::UeToGame2D(GetActorLocation());
	const float Dist = FVector2D::Distance(NowGame, SegDstGame);
	if (Dist <= 5.f) { bSegActive = false; return; }   // уже в цели (порог как у StartChase)
	const float V = SegResumeSpeed(Dist);
	UE_LOG(LogAsdaMob, Log, TEXT("[отрезок] rid=%d '%s': добег к (%.0f,%.0f), до цели %.0f юн, серверу идти %.2f с, v=%.3f юн/мс (сервер %.3f)"),
		NetRenderId, *MobName, SegDstGame.X, SegDstGame.Y, Dist, SegRemainS, V, SegSrvSpeed);
	if (SegState == 2)
	{
		bSegKeep = true;
		NetTrajectory(SegDstGame, V, SegAnimRate);
		bSegKeep = false;
	}
	else
	{
		if (SegState == 0xE) { bKeepPosOnArrive = true; }   // страх: как у самого пакета 0xE (состояние 5, по прибытии позицию не снапить)
		StartChase(SegState == 0xE ? 5 : SegState, SegDstGame, V, SegAnimRate);
	}
	MoveSrvSpeed = SegSrvSpeed;
	if (!bMoving) { bSegActive = false; }   // уже в цели
}

// ★ 07.11.2026 [ИСПРАВЛЕНИЕ] разбор в объявлении.
void AAsdaMobActor::NetSnapOrGlide(const FVector2D& CurGame)
{
	if (CVarMobSnapAsGlide.GetValueOnGameThread() == 0 || bDead || bMoving) { NetSnapPos(CurGame); return; }
	GlideStartG = AsdaCoord::UeToGame2D(GetActorLocation());
	GlideDstG = CurGame;
	const float V = kSegCatchUpMul * FMath::Max(0.2f, MoveSrvSpeed) * 1000.f;   // юн/с
	GlideDurS = FMath::Clamp(FVector2D::Distance(GlideStartG, GlideDstG) / V, kGlideMinS, kGlideMaxS);
	GlideElapsedS = 0.f;
	bGlide = true;
}

void AAsdaMobActor::NetHalt()
{
	bSegActive = false;   // ★ 07.11.2026: моба остановили — отрезка больше нет (и в клипе попадания)
	if (bDead || IsMoveGated()) { return; }   // эталонный гейт state 13/14 (хит); труп состояние не меняет
#if ASDA_COMBAT_TRACE
	if (IsAttackAnimActive())
	{
		ASDA_CTRACE_MOB(NetRenderId, EAsdaCT::MobAttack, TEXT("моб rid=%d '%s': замах ПРЕРВАН остановкой (NetHalt, SetActionState 1)"), NetRenderId, *MobName);
	}
	ASDA_CTRACE_V(EAsdaCT::Move, TEXT("моб rid=%d '%s': остановка на месте (NetHalt)%s"), NetRenderId, *MobName,
		bMoving ? TEXT(", был в движении → покой") : TEXT(""));
#endif
	AttackAnimUntilS = 0.0;                   // SetActionState(1) прерывает замах (эталон)
	bAnimGateWas = false;
	NetMoveState = 2;
	if (bMoving) { EnterIdle(); }             // эталон SetActionState(1)
	bMoving = false;
	MoveDstGame = AsdaCoord::UeToGame2D(GetActorLocation());   // ★ Аудит №3 (B1): цель = текущая точка
}

// ★ R5-fix D6 (13.11.2026, чистка): «ДОГОНЯЮЩАЯ» СКОРОСТЬ МОБА ПО ОТРЕЗКУ ПАКЕТА — одна функция на ходьбу (`NetTrajectoryTimed`)
//   и преследование/боевое перемещение (`NetChaseTimed`). До R5-fix эти восемь строк стояли двумя дословными копиями.
//   Сверено с декомпилом `Recv_CombatHit@0xF310E0`: все три ветки эталона считают ОДИНАКОВО —
//     ходьба (network.c:37618-37637), case 5 (:37893-37916), kind 7 (:37968-37992):
//       v0 = 100.0 / spd;  segTime = |dst − curПакета| / v0;  V = |dst − наша позиция| / segTime.
//   Различие веток — только в том, какой функции траектории отдаётся V. Дефекта нет, поведение не менялось.
//   NowGame — наша позиция моба; CurPktGame/DstGame — клетки пакета; PktSpeedUnitsPerMs — скорость пакета, юн/мс.
static float AsdaMobCatchUpSpeed(const FVector2D& NowGame, const FVector2D& CurPktGame, const FVector2D& DstGame, float PktSpeedUnitsPerMs)
{
	const float SegLen = FVector2D::Distance(CurPktGame, DstGame);   // отрезок, который сервер отвёл мобу
	const float ActLen = FVector2D::Distance(NowGame,    DstGame);   // сколько до dst нам
	float V = PktSpeedUnitsPerMs;
	if (SegLen > 0.001f && PktSpeedUnitsPerMs > 1e-6f)
	{
		const float SegTimeMs = SegLen / PktSpeedUnitsPerMs;         // время отрезка по серверу
		if (SegTimeMs > 1e-6f) { V = ActLen / SegTimeMs; }           // ← та самая «догоняющая» скорость
	}
	return V;
}

// ★★★ Эталонная нормировка скорости по ВРЕМЕНИ отрезка — см. развёрнутый разбор в AsdaMobActor.h.
//   v' = |dst − позиция| / ( |dst − curПакета| / (100/spd) ).
void AAsdaMobActor::NetTrajectoryTimed(const FVector2D& CurPktGame, const FVector2D& DstGame, float PktSpeedUnitsPerMs,
                                       float AnimRate)
{
	MoveAnimRate = (AnimRate > 0.f) ? AnimRate : 1.f;
	const FVector L = GetActorLocation();
	const FVector2D NowGame = AsdaCoord::UeToGame2D(L);   // ★ Аудит №3 (B1)
	const float V = AsdaMobCatchUpSpeed(NowGame, CurPktGame, DstGame, PktSpeedUnitsPerMs);   // ★ R5-fix D6: общая «догоняющая» скорость
	bSegKeep = true;    // ★ 07.11.2026: отрезок этого пакета уже записан
	NetTrajectory(DstGame, V, MoveAnimRate);
	bSegKeep = false;
	if (PktSpeedUnitsPerMs > 1e-6f) { MoveSrvSpeed = PktSpeedUnitsPerMs; }   // ★ mobpos_swing: скорость сервера, не нормированная
}

// ★ Замер рассинхрона с серверной клеткой (диагностика, поведение не меняет).
float AAsdaMobActor::NoteDesync(const FVector2D& CurPktGame)
{
	const FVector L = GetActorLocation();
	const float D = FVector2D::Distance(AsdaCoord::UeToGame2D(L), CurPktGame);   // ★ Аудит №3 (B1)
	DesyncLastU = D;
	DesyncMaxU  = FMath::Max(DesyncMaxU, D);
	DesyncSumU += (double)D;
	++DesyncN;
	return D;
}

void AAsdaMobActor::NetTrajectory(const FVector2D& DstGame, float SpeedUnitsPerMs, float AnimRate)
{
	if (!bSegKeep) { bSegActive = false; }   // ★ 07.11.2026: прямой вызов (4013/4019) — отрезок 4015 устарел
	if (bDead) { return; }
	if (IsMoveGated())   // эталонный гейт state 13/14 — траектория не строится
	{
		ASDA_CTRACE_V(EAsdaCT::Move, TEXT("моб rid=%d '%s': траектория НЕ принята — идёт клип попадания (state 13)"), NetRenderId, *MobName);
		return;
	}
	MoveAnimRate = (AnimRate > 0.f) ? AnimRate : 1.f;
#if ASDA_COMBAT_TRACE
	if (IsAttackAnimActive())
	{
		ASDA_CTRACE_MOB(NetRenderId, EAsdaCT::MobAttack, TEXT("моб rid=%d '%s': замах ПРЕРВАН траекторией (SetActionState 2) → (%.0f,%.0f)"),
			NetRenderId, *MobName, DstGame.X, DstGame.Y);
	}
	ASDA_CTRACE_V(EAsdaCT::Move, TEXT("моб rid=%d '%s': траектория (ходьба) → (%.0f,%.0f) v=%.3f юн/мс%s"), NetRenderId, *MobName,
		DstGame.X, DstGame.Y, SpeedUnitsPerMs, bCombatStance ? TEXT(", выход из боевой стойки") : TEXT(""));
#endif
	AttackAnimUntilS = 0.0;          // SetActionState(2) прерывает клип замаха (эталон)
	bAnimGateWas = false;            // замах снят намеренно — конец клипа НЕ наступил, буфер не сливаем
	// ★ Эталон projInitTrajectoryToTarget@0xF85690: ставит ent[120] = 2 (ходьба) и ГАСИТ флаг «в бою»
	//   (`*((_WORD*)self + 237) = 0`) — мирное перемещение выводит моба из боевой стойки.
	NetMoveState = 2;
	bCombatStance = false;
	const FVector L = GetActorLocation();
	const FVector2D NowGame = AsdaCoord::UeToGame2D(L);   // ★ Аудит №3 (B1)
	// ★★★ СЛУЧАЙНЫЙ СДВИГ ЦЕЛИ — воспроизведение эталона ДОСЛОВНО. Обе функции построения «ходячей»
	//   кривой начинаются одинаково (entity_char.c:57116-57119 в `projInitTrajectoryToTarget@0xF85690`
	//   и :57190-57193 в `projectileInitTrajectory@0xF85A20`):
	//       srand(time(nullptr));
	//       randOffset = (float)(rand() % 10);
	//       dstX += randOffset;  dstZ += randOffset;      // ОДНО И ТО ЖЕ число в обе оси
	//   Через эти две функции идут ВСЕ мирные траектории моба: 4013 состояние 2, 4019 и 4015 kind=2.
	//   `srand` на каждый пакет — дефект оригинала (в пределах одной секунды сдвиг у всех мобов
	//   одинаковый, и он же перезапускает глобальную последовательность rand(), из которой берёт
	//   направление джиттер каста). Воспроизводим и его: FMath::RandInit == srand, FMath::Rand == rand.
	FMath::RandInit((int32)FDateTime::UtcNow().ToUnixTimestamp());
	const float RandOffset = (float)(FMath::Rand() % 10);
	MoveDstGame = DstGame + FVector2D(RandOffset, RandOffset);
	MoveSpeed = (SpeedUnitsPerMs > 0.f) ? SpeedUnitsPerMs : 0.35f;   // ★ нормированная скорость бывает малой — не подменяем
	MoveStartGame = NowGame; MoveSrvSpeed = MoveSpeed;               // ★ mobpos_swing: начало траектории (признак «призрака»)
	if (FVector2D::Distance(NowGame, MoveDstGame) <= 5.f)
	{
		// Цель = текущая точка: траектория вырождена — моб просто стоит (эталон: скорость 0 → idle).
		if (bMoving) { EnterIdle(); }
		bMoving = false;
		return;
	}
	FaceGameDir(MoveDstGame - NowGame);
	if (!bMoving) { PlayClip(ClipMove, true, MoveAnimRate); MoveClipLenS = CurrentClipLength(); MoveClipTimeS = 0.f; bStepSoundDue = true; }
	bMoving = true;
}

// ══════════════════ ★★★ ПРЕСЛЕДОВАНИЕ (4015 kind=5/0xE) И БОЕВОЕ ПЕРЕМЕЩЕНИЕ (kind=7) ══════════════════
//
// Эталон, `Recv_CombatHit@0xF310E0` case 5 (network.c:37841-37917) — дословный порядок действий:
//     ent[122] = (i16)pkt@17 * 1e-4;                                  // темп клипа
//     if (state == 3 || state == 13 || state == 14) { ent[1753] = 1; memcpy(ent+1754, pkt, 0x25); }
//     else {
//       if (pkt@7 == pkt@11 && pkt@9 == pkt@13) return 1;              // cur == dst ⇒ ничего
//       ent[1749] = pkt@0;                                            // userIdx цели преследования
//       charSetTerrainCellIndex(ent, тайл(@11,@13));                   // ЗАПОМНИТЬ dst-клетку
//       if (!ent[237]) { звук фазы 3 (рык агра); ent[237] = 1; }       // ровно один раз на агро
//       v0      = 100.0 / (i16)pkt@15;                                 // скорость пакета, юн/мс
//       segTime = |dst − curПАКЕТА| / v0;                              // ВРЕМЯ отрезка по серверу
//       playerCastDirectionalEffect2(ent, dst, |dst − НАША позиция| / segTime);
//     }
// `playerCastDirectionalEffect2@0xF865C0` = та же кривая, что у ходьбы, но: (а) сдвигает dst на ±30
//   юнитов по одному из 8 направлений, (б) ставит ent[120] = 5, (в) флаг «в бою» НЕ гасит.
//   `playerCastDirectionalEffect@0xF86180` (kind=7) отличается ровно одним: ent[120] = 7.
// Покадровый тик `effect_UpdateProjectileTick@0xF7BDD0` в состояниях 5/7 крутит слот 3 (бег) и
//   играет звук фазы 12, в состоянии 2 — слот 2 (ходьба) и фазу 2.
void AAsdaMobActor::NetChaseTimed(uint8 MoveState, const FVector2D& CurPktGame, const FVector2D& DstGame,
                                  float PktSpeedUnitsPerMs, float AnimRate)
{
	const FVector L = GetActorLocation();
	const FVector2D NowGame = AsdaCoord::UeToGame2D(L);   // ★ Аудит №3 (B1)
	const float V = AsdaMobCatchUpSpeed(NowGame, CurPktGame, DstGame, PktSpeedUnitsPerMs);   // ★ R5-fix D6: общая «догоняющая» скорость
	StartChase(MoveState, DstGame, V, AnimRate);
	if (PktSpeedUnitsPerMs > 1e-6f) { MoveSrvSpeed = PktSpeedUnitsPerMs; }   // ★ mobpos_swing: скорость сервера, не нормированная
}

// ★ 15.10.2026 (волна mobpos_swing, ревизия СРЕДН-2). Идущий моб разошёлся с живой клеткой сервера на ≥ 3 клетки
//   (4017/4027). Снап + стоп здесь давал рывок на каждом ударе/регене и останавливал моба, которого сервер ведёт дальше.
//   Эталонный способ синхронизации идущего — нормировка скорости по времени отрезка (`Recv_CombatHit` case 2, network.c:
//   37613-37637): v' = |dst − НАША позиция| / (|dst − позиция СЕРВЕРА| / v_сервера) — моб приходит в dst одновременно с
//   сервером. Границы 0.25…4 × v_сервера — чтобы почти дошедший сервер (|dst − сервер| → 0) не давал телепорт. [ПОРТ]
void AAsdaMobActor::CatchUpToServerCell(const FVector2D& LiveGame)
{
	if (bDead || !bMoving) { return; }
	const FVector2D NowGame = AsdaCoord::UeToGame2D(GetActorLocation());
	const float Rem = FVector2D::Distance(NowGame, MoveDstGame);
	const float SrvRem = FVector2D::Distance(LiveGame, MoveDstGame);
	const float Vs = FMath::Max(1e-3f, MoveSrvSpeed);
	const float V = (SrvRem > 1.f) ? (Rem * Vs / SrvRem) : (4.f * Vs);
	MoveSpeed = FMath::Clamp(V, 0.25f * Vs, 4.f * Vs);
	ASDA_CTRACE_V(EAsdaCT::Move, TEXT("моб rid=%d '%s': догон серверной клетки — нам до цели %.0f юн, серверу %.0f → v=%.3f юн/мс (сервер %.3f)"),
		NetRenderId, *MobName, Rem, SrvRem, MoveSpeed, Vs);
}

void AAsdaMobActor::NetChaseRaw(uint8 MoveState, const FVector2D& DstGame, float SpeedUnitsPerMs, float AnimRate)
{
	bSegActive = false;   // ★ 07.11.2026: 4013 с бегом — отрезок 4015 устарел
	StartChase(MoveState, DstGame, SpeedUnitsPerMs, AnimRate);
}

// ★ Джиттер точки назначения — эталон playerCastDirectionalEffect/2 дословно:
//     offsetX = offsetZ = 30.0;
//     dirIndex = 8 * rand() / 0x7FFF;
//     if (ent[1837] == dirIndex) { if (++dirIndex > 7) dirIndex = 0;  ent[1837] = dirIndex; }
//     switch (dirIndex) { ... 8 комбинаций ±30 по X и Z ... }
//   Запись ent[1837] в оригинале стоит ТОЛЬКО внутри ветки совпадения (то есть «прошлый индекс»
//   почти всегда остаётся старым) — воспроизводим ровно так, это дефект оригинала без последствий.
FVector2D AAsdaMobActor::ApplyCastJitter(const FVector2D& DstGame)
{
	const float OffX = 30.f, OffZ = 30.f;
	// `8 * rand() / 0x7FFF` при rand() == 0x7FFF даёт 8 — у оригинала это уходит в `default:`
	//   (та же комбинация, что case 4). Клампа НЕ ставим: воспроизводим ветвление один в один.
	int32 DirIndex = (int32)((8 * FMath::Rand()) / 0x7FFF);
	if (LastCastDirIdx == DirIndex)
	{
		if (++DirIndex > 7) { DirIndex = 0; }
		LastCastDirIdx = DirIndex;
	}
	FVector2D D = DstGame;
	switch (DirIndex)
	{
	case 0: D.X -= OffX; D.Y -= OffZ; break;
	case 1:              D.Y -= OffZ; break;
	case 2: D.X += OffX; D.Y -= OffZ; break;
	case 3: D.X += OffX;              break;
	case 4: D.X += OffX; D.Y += OffZ; break;
	case 5:              D.Y += OffZ; break;
	case 6: D.X -= OffX; D.Y += OffZ; break;
	case 7: D.X -= OffX;              break;
	default: D.X += OffX; D.Y += OffZ; break;   // эталонный default (индекс 8 при rand() == 0x7FFF)
	}
	return D;
}

void AAsdaMobActor::StartChase(uint8 MoveState, const FVector2D& DstGame, float SpeedUnitsPerMs, float AnimRate)
{
	if (bDead) { return; }
	// Гейта здесь нет: состояния 3/13/14 отсекает вызывающий (эталон буферизует пакет ДО траектории).
#if ASDA_COMBAT_TRACE
	if (IsAttackAnimActive())
	{
		ASDA_CTRACE_MOB(NetRenderId, EAsdaCT::MobAttack, TEXT("моб rid=%d '%s': замах ПРЕРВАН погоней (состояние %d)"), NetRenderId, *MobName, (int32)MoveState);
	}
#endif
	AttackAnimUntilS = 0.0;
	bAnimGateWas = false;
	MoveAnimRate = (AnimRate > 0.f) ? AnimRate : 1.f;
	const bool bWasRunState = (NetMoveState == 5 || NetMoveState == 7);
	NetMoveState = MoveState;
	const FVector2D Dst = ApplyCastJitter(DstGame);
	const FVector L = GetActorLocation();
	const FVector2D NowGame = AsdaCoord::UeToGame2D(L);   // ★ Аудит №3 (B1)
	MoveDstGame = Dst;
	MoveSpeed = (SpeedUnitsPerMs > 0.f) ? SpeedUnitsPerMs : 0.35f;
	MoveStartGame = NowGame; MoveSrvSpeed = MoveSpeed;               // ★ mobpos_swing: начало траектории (признак «призрака»)
	if (FVector2D::Distance(NowGame, Dst) <= 5.f)
	{
		// Уже стоим в цели: эталонный тик состояния 5 при нулевом остатке уводит в SetActionState(6).
		ASDA_CTRACE_V(EAsdaCT::Move, TEXT("моб rid=%d '%s': погоня (состояние %d) — уже в цели (%.0f,%.0f) → боевая стойка"),
			NetRenderId, *MobName, (int32)MoveState, Dst.X, Dst.Y);
		bMoving = false;
		EnterCombatIdle();
		return;
	}
#if ASDA_COMBAT_TRACE
	if (!bMoving || !bWasRunState)   // вход в бег — событие; следующие отрезки той же погони — частые
	{
		ASDA_CTRACE_IF(AsdaCombatTrace::IsEngaged(NetRenderId) || AsdaCombatTrace::IsUserRelevant(ChaseTargetUserIdx), EAsdaCT::Move, TEXT("моб rid=%d '%s': ПОГОНЯ начата (состояние %d) → (%.0f,%.0f), до цели %.0f юн, v=%.3f юн/мс, темп клипа ×%.2f, цель userIdx=%d"),
			NetRenderId, *MobName, (int32)MoveState, Dst.X, Dst.Y, FVector2D::Distance(NowGame, Dst), MoveSpeed, MoveAnimRate, ChaseTargetUserIdx);
	}
	else
	{
		ASDA_CTRACE_V(EAsdaCT::Move, TEXT("моб rid=%d '%s': погоня, отрезок → (%.0f,%.0f), до цели %.0f юн, v=%.3f юн/мс"),
			NetRenderId, *MobName, Dst.X, Dst.Y, FVector2D::Distance(NowGame, Dst), MoveSpeed);
	}
#endif
	FaceGameDir(Dst - NowGame);
	// Клип бега перезапускаем только на ВХОДЕ в бег: эталон в состояниях 5/7 время анимации (ent[113])
	//   между отрезками НЕ обнуляет — SetActionState там не вызывается, поэтому шаг бежит непрерывно.
	if (!bMoving || !bWasRunState)
	{
		PlayClip(ClipRun, true, MoveAnimRate);
		MoveClipLenS = CurrentClipLength();
		MoveClipTimeS = 0.f;
		bStepSoundDue = true;
	}
	else if (Mesh) { Mesh->SetPlayRate(FMath::Max(0.01f, MoveAnimRate)); }
	bMoving = true;
}

// ★ Рык агра — эталон case 5: звук фазы 3 ровно один раз, пока флаг «в бою» не сброшен.
void AAsdaMobActor::PlayAggroRoarOnce()
{
	if (bCombatStance) { return; }
	PlayMobSound(3);
	bCombatStance = true;
}

// ★ Порт charSetTerrainCellIndex@0xF86C60 (`ent[282] = terrainCellIndexFromXZChecked(...)`).
//   Единственный ЧИТАТЕЛЬ поля в эталоне — `charCastDirectionalEffectIfFar@0xF869E0` (см. ниже).
void AAsdaMobActor::RememberServerCell(const FVector2D& CellGame)
{
	ServerCellGame = CellGame;
	bHasServerCell = true;
}

// ══════════ ★★★ ДОВОДЧИК НА СЕРВЕРНУЮ КЛЕТКУ — charCastDirectionalEffectIfFar@0xF869E0 ══════════
//
// Эталон (entity_char.c:57467-57541) дословно:
//     pTarget = CharManager_FindByUserIdx(g_CharManager, ent[1749]);        // цель преследования
//     if (!pTarget)                                    goto CHECK_CELL;
//     dx = |pTarget->worldPosX − ent.pos.x|;  dz = |pTarget->worldPosZ − ent.pos.z|;
//     if (dx > 250.0 && dz > 250.0)                    goto CHECK_CELL;     // ★ И, не ИЛИ
//     return;                                                              // цель рядом — ничего
//   CHECK_CELL:
//     objPos = getMapObjectPosByIndex(ent[282]);                            // запомненная клетка
//     if (|тайл(objPos.x) − тайл(pos.x)| >= 3 || |тайл(objPos.z) − тайл(pos.z)| >= 3)
//         playerCastDirectionalEffect2(ent, objPos, 100.0 / ent[126]);
//
// ★★★ ЧТО ТАКОЕ `ent[126]` (байтовый оффсет +0x1F8) — РАЗОБРАНО ПО БИНАРЮ, а не «нет ссылок в срезе».
//   Сплошной скан AsdaStory.exe (13 344 768 б, ImageBase 0x400000) по ВСЕМ кодировкам обращения к
//   `[reg+1F8h]` (x87 fld/fst/fstp/fadd/fmul/fdiv/fdivr/fild, mov reg↔mem, mov mem,imm32) даёт по
//   всему образу ровно три места, работающих с ПЕРСОНАЖЕМ:
//     0xD3F598  `mov [eax+1F8h], 0`        — playerInventoryState_ctor@0xD3F310 (конструктор сущности,
//                                            он же entity_char.c:45821 `v7[126] = 0`);
//     0xF0114D  `mov [eax+1F8h], edx`      — charDataCopyAssign@0xF00F40 (копия поля из другой сущности);
//     0xF86BAA  `fdiv dword [eax+1F8h]`    — ЭТОТ САМЫЙ доводчик (единственный читатель).
//   Иных записей в образе НЕТ ⇒ поле всегда 0 ⇒ `100.0 / 0 = +INF`.
//   Что делает +INF в кривой: `playerCastDirectionalEffect2` кладёт её в `effectCurveNodeInit`, а тик
//   прибытия `charAnim_UpdateActionTick@0xF80A50` при исчерпании времени кривой ставит
//   `SetActionState(6)` и `playerSetWorldPosition(ent[132..134])` — то есть В ТОЧКУ НАЗНАЧЕНИЯ.
//   При бесконечной скорости это происходит на ПЕРВОМ ЖЕ тике ⇒ у оригинала доводчик = мгновенная
//   постановка моба на запомненную серверную клетку + боевая стойка. Ровно это и воспроизводим
//   (джиттер ±30 из playerCastDirectionalEffect2 при этом никуда не девается — он в кривой).
void AAsdaMobActor::CastDirectionalEffectIfFar()
{
	if (bDead || !bHasServerCell || !GetWorld()) { return; }
	const FVector L = GetActorLocation();
	const FVector2D NowGame = AsdaCoord::UeToGame2D(L);   // ★ Аудит №3 (B1)
	// Шаг 1 — цель преследования (ent[1749]). Найдена И ближе порога хотя бы по одной оси ⇒ выход.
	if (const UGameInstance* GI = GetGameInstance())
	{
		if (const UAsdaWorldSubsystem* WS = GI->GetSubsystem<UAsdaWorldSubsystem>())
		{
			if (const AActor* T = WS->ActorByUserIdx(ChaseTargetUserIdx))
			{
				const FVector TL = T->GetActorLocation();
				const FVector2D TG = AsdaCoord::UeToGame2D(TL);   // ★ Аудит №3 (B1)
				if (!(FMath::Abs(TG.X - NowGame.X) > 250.f && FMath::Abs(TG.Y - NowGame.Y) > 250.f)) { return; }
			}
		}
	}
	// Шаг 2 — расхождение с запомненной клеткой в ТАЙЛАХ, по каждой оси отдельно.
	const FIntPoint CellTile = AsdaCoord::GameToTile(ServerCellGame);   // ★ Аудит №3 (B2)
	const FIntPoint NowTile  = AsdaCoord::GameToTile(NowGame);          // ★ Аудит №3 (B2)
	const int32 CellTx = CellTile.X, CellTz = CellTile.Y;
	const int32 NowTx  = NowTile.X,  NowTz  = NowTile.Y;
	if (FMath::Abs(CellTx - NowTx) < 3 && FMath::Abs(CellTz - NowTz) < 3) { return; }
	// Шаг 3 — playerCastDirectionalEffect2(клетка, +INF): джиттер ±30, состояние 5, прибытие в тот же
	//   тик ⇒ позиция = точка назначения, затем SetActionState(6).
	const FVector2D Dst = ApplyCastJitter(ServerCellGame);
	AttackAnimUntilS = 0.0;
	bAnimGateWas = false;
	bGlide = false;   // ★ 07.11.2026: доводчик сам ставит на клетку
	NetMoveState = 5;
	MoveDstGame = Dst;
	bMoving = false;
	bKeepPosOnArrive = false;   // мгновенное прибытие — тик 0xE-флага не увидит, гасим как эталон
	FaceGameDir(Dst - NowGame);
	const FVector2D UeXY = AsdaCoord::GameToUe2D(Dst);   // ★ Аудит №3 (B1)
	SetActorLocation(FVector(UeXY.X, UeXY.Y, TerrainZ(UeXY.X, UeXY.Y)));
	EnterCombatIdle();
	UE_LOG(LogAsdaMob, Log,
		TEXT("[доводчик] rid=%d '%s': конец клипа, буфер пуст, до серверной клетки (%d,%d) ≥3 тайлов ")
		TEXT("(были в (%d,%d)) → мгновенная постановка (эталон 100.0/ent[126] = +INF) + боевая стойка"),
		NetRenderId, *MobName, CellTx, CellTz, NowTx, NowTz);
	ASDA_CTRACE_MOB(NetRenderId, EAsdaCT::Move, TEXT("моб rid=%d '%s': доводчик — до серверной клетки (%d,%d) ≥3 тайлов (был в (%d,%d)) → мгновенная постановка + боевая стойка"),
		NetRenderId, *MobName, CellTx, CellTz, NowTx, NowTz);
}

// ★★★ Положить пакет в буфер отложенных (эталон ent[1753] = 1 + memcpy(ent+1754, pkt, 0x25)).
//   Буфер ОДИН: новый отложенный пакет затирает старый — ровно как memcpy в эталоне.
void AAsdaMobActor::BufferNetAction(uint8 Kind, const FVector2D& CurGame, const FVector2D& DstGame,
                                    float SpeedUnitsPerMs, float AnimRate)
{
	Pending.bValid   = true;
	Pending.Kind     = Kind;
	Pending.CurGame  = CurGame;
	Pending.DstGame  = DstGame;
	Pending.Speed    = SpeedUnitsPerMs;
	Pending.AnimRate = (AnimRate > 0.f) ? AnimRate : 1.f;
}

// ★★★ СЛИВ БУФЕРА ПО КОНЦУ КЛИПА замаха/флинча. Эталонные места слива (все три ведут себя одинаково):
//   Combat_ApplyDamage@0xF7C470 (combat_pvp.c:8168-8210), charAction_tickWarp23@0xF79CE0
//   (entity_char.c:54826/54900), effectObjectTickActionState@0xF7A720 / effectTimer_updateTick@0xF7AC70.
//     if (ent[1753]) { ent[1753] = 0;
//                      switch (буфер+6) { 3 → SetActionState(3) с вариантом из буфера+29;
//                                         5, 0xE → playerCastDirectionalEffect2(dst, 100/spd);
//                                         7      → playerCastDirectionalEffect (dst, 100/spd); } }
//     else           { SetActionState(6); }        // боевая стойка
//   ⚠ Скорость СЫРАЯ, без нормировки по времени отрезка — так во всех местах слива.
//   ⚠ kind=3 в буфер моба попасть не может: `Recv_CombatHit` case 3 сначала целиком отрабатывает
//     состояния 13/14/15/16/17 и делает return, поэтому внутренняя проверка `ent[120] == 13`
//     (network.c:37789) недостижима. Буфер kind=3 ставит только «игроцкий» хвост хендлера
//     (network.c:38270) — там гейта на состояние 13 сверху нет. Для мобов ветки 3 в сливе нет.
void AAsdaMobActor::FlushPendingNetAction()
{
	if (bDead) { Pending.bValid = false; return; }
	if (Pending.bValid)
	{
		const FAsdaMobPendingAction P = Pending;
		Pending.bValid = false;
		UE_LOG(LogAsdaMob, Log, TEXT("[отложенный] rid=%d '%s': клип доигран → применяю отложенный пакет kind=%d dst=(%.0f,%.0f) v=%.3f юн/мс"),
			NetRenderId, *MobName, (int32)P.Kind, P.DstGame.X, P.DstGame.Y, P.Speed);
		ASDA_CTRACE_MOB(NetRenderId, EAsdaCT::MobAttack, TEXT("моб rid=%d '%s': клип замаха/попадания доигран → отложенный пакет kind=%d → (%.0f,%.0f) v=%.3f юн/мс"),
			NetRenderId, *MobName, (int32)P.Kind, P.DstGame.X, P.DstGame.Y, P.Speed);
		// ★ Ветка 0xE слива взводит тот же флаг «по прибытии позицию не снапить», что и сам пакет
		//   (entity_char.c:54873 `*((_WORD *)character + 3492) = 1;` перед вызовом кривой).
		if (P.Kind == 0xE) { bKeepPosOnArrive = true; }
		// ★ 07.11.2026 [ИСПРАВЛЕНИЕ]: пакет пролежал в буфере весь клип, а сервер уже бежит — скорость по остатку отрезка, а не сырая.
		//   Цель — из отрезка: за клип мог прийти более новый пакет хода, которого в буфере нет.
		if (bSegActive) { ResumeServerSegment(); return; }
		StartChase(P.Kind == 7 ? 7 : 5, P.DstGame, P.Speed, P.AnimRate);
		return;
	}
	// Буфер пуст. Если движение уже перезапущено пришедшим пакетом — состояние 2/5/7, стойку не ставим.
	ASDA_CTRACE_MOB(NetRenderId, EAsdaCT::MobAttack, TEXT("моб rid=%d '%s': клип замаха/попадания доигран, буфер пуст → %s"),
		NetRenderId, *MobName, bMoving ? TEXT("уже идёт (стойку не ставим)") : TEXT("боевая стойка (SetActionState 6)"));
	if (bMoving) { return; }
	EnterCombatIdle();
	if (bSegActive) { return; }   // ★ 07.11.2026 [ИСПРАВЛЕНИЕ]: сервер ведёт отрезок — моб добежит (Tick), доводчик не рвёт его на клетку
	// ★ Эталон в двух местах слива зовёт доводчик СРАЗУ ПОСЛЕ SetActionState(6)
	//   (entity_char.c:54883, :54960; effects_particles.c:66311) — см. CastDirectionalEffectIfFar.
	CastDirectionalEffectIfFar();
}

// ★ Порт `Entity_GetActionState` — эталонный НОМЕР состояния сущности, собранный из полей порта.
//   Соответствие однозначно по тому, чем эти поля ставятся:
//     10 — bDead (SetActionState case 0xA);          13 — HitAnimUntilS (case 0xD, хит-флинч);
//      3 — AttackAnimUntilS (case 3, замах);         2/5/7 — NetMoveState при bMoving (ent[120]);
//      6 — стойка (bCombatStance без движения);       1 — мирный покой.
int32 AAsdaMobActor::GetEtalonActionState() const
{
	if (bDead) { return 10; }
	if (IsKnockActive()) { return 14; }   // ★ 05.11.2026 отлёт
	if (IsHitAnimActive()) { return 13; }
	if (IsAttackAnimActive()) { return 3; }
	if (SkillActionState != 0) { return SkillActionState; }   // ★ 30.10.2026: навык моба 15/16/17
	if (bMoving) { return (int32)NetMoveState; }
	return bCombatStance ? 6 : 1;
}

// ★ SetActionState(1): мирный покой — клип 0, флаг «в бою» сброшен (эталон case 1).
void AAsdaMobActor::EnterIdle()
{
	bCombatStance = false;
	PlayClip(ClipIdle, true);
}

// ★ SetActionState(6): боевая стойка — клип 1, флаг «в бою» = 1 (эталон case 6).
void AAsdaMobActor::EnterCombatIdle()
{
	bCombatStance = true;
	PlayClip(ClipCombatIdle, true);
}

// ★ Звук шага с однократной проверкой наличия wav — см. объявление в AsdaMobActor.h.
void AAsdaMobActor::PlayStepSound(int32 Phase)
{
	int8& State = (Phase == 2) ? StepSndWalk : StepSndRun;
	if (State == 0) { return; }
	if (State < 0)
	{
		if (SoundBase.IsEmpty()) { State = 0; return; }
		UGameInstance* GI = GetGameInstance();
		UAsdaSoundSubsystem* Snd = GI ? GI->GetSubsystem<UAsdaSoundSubsystem>() : nullptr;
		if (!Snd) { return; }   // подсистемы ещё нет — проверку не фиксируем, попробуем позже
		const FString Nm = FString::Printf(TEXT("%s_%d"), *SoundBase, Phase);
		State = Snd->ResolveSound(FName(*Nm)) ? 1 : 0;   // ResolveSound молчалив и кэширует сам
		if (State == 0) { return; }
	}
	PlayMobSound(Phase);
}

// Длина клипа, который сейчас стоит в single-node инстансе (сек; 0 — клипа нет).
float AAsdaMobActor::CurrentClipLength() const
{
	return (Mesh && Mesh->GetSingleNodeInstance()) ? Mesh->GetSingleNodeInstance()->GetLength() : 0.f;
}

// ★ 30.10.2026 навык моба: клип слота навыка. Движение на время навыка стоит (эталон: тики 15/16/17 позицию не двигают —
//   `playerSetWorldPosition` зовут только тики движения). Клипа нет — длина 0, автомат идёт по своим таймаутам.
float AAsdaMobActor::PlaySkillClip(int32 Slot, bool bLoop)
{
	if (bDead) { return 0.f; }
	bMoving = false;
	bCombatStance = true;   // эталон SetActionState case 0x11 (17): `*((_WORD*)ent + 237) = 1`; 15/16 поле не трогают — стойка уже взведена
	const int32 Before = ClipSerial;
	bPlayingSkillClip = true;
	PlayClip(Slot, bLoop);
	bPlayingSkillClip = false;
	const bool bHasClip = Mesh && Mesh->GetSingleNodeInstance() && ClipSerial != Before
		&& Mesh->GetSingleNodeInstance()->GetAnimationAsset()
		&& Mesh->GetSingleNodeInstance()->GetAnimationAsset()->GetName().EndsWith(FString::Printf(TEXT("anim_%02d"), ClipFileForSlot(Slot)));
	return bHasClip ? CurrentClipLength() : 0.f;
}

void AAsdaMobActor::EnterSkillState(int32 State)
{
	if (bDead) { return; }
	bSegActive = false;   // ★ 07.11.2026 [ИСПРАВЛЕНИЕ]: сервер прервал бег навыком, добег сорвал бы его клип
	AttackAnimUntilS = 0.0;
	HitAnimUntilS = 0.0;
	bAnimGateWas = false;   // конец замаха/флинча больше не наступит — слива буфера нет
	bMoving = false;        // тики 15/16/17 позицию не двигают
	if (State == 17) { bCombatStance = true; }
	SkillActionState = State;
}

void AAsdaMobActor::FaceTowardGame(const FVector2D& TargetGame)
{
	const FVector2D Dir = TargetGame - AsdaCoord::UeToGame2D(GetActorLocation());
	if (!Dir.IsNearlyZero()) { FaceGameDir(Dir); }
}

void AAsdaMobActor::NetHitFlinch(const FVector2D& AttackerGame)
{
	// ★ 05.11.2026 [ПОРТ] идущий отлёт (состояние 14) — сам реакция на удар: флинч (13) его не перебивает, иначе клип 14 и сдвиг рвутся.
	if (IsKnockActive()) { return; }
	if (bDead || IsAttackAnimActive())   // замах/смерть флинч не перебивает (эталон)
	{
		ASDA_CTRACE_MOB(NetRenderId, EAsdaCT::Fx, TEXT("моб rid=%d '%s': клип попадания НЕ играем — %s"), NetRenderId, *MobName,
			bDead ? TEXT("труп") : TEXT("идёт замах (флинч его не перебивает)"));
		return;
	}
	// ★ АГРО-ВИЗУАЛ: получив удар, моб разворачивается к атакующему (сервер ведёт его к нам движением,
	//   но поворот приходит только с move-пакетами — без этого моб «не замечает» бьющего).
	if (!AttackerGame.IsNearlyZero())
	{
		const FVector L = GetActorLocation();
		FaceGameDir(AttackerGame - AsdaCoord::UeToGame2D(L));   // ★ Аудит №3 (B1)
	}
	// ★ Эталон: удар переводит моба в state 13 — он ОСТАНАВЛИВАЕТСЯ (движение на это время игнорируется),
	//   а не «идёт дальше как ни в чём не бывало» (юзер 2026-07-29).
	// ★ 05.11.2026 [ОРИГИНАЛ] case 0xD: `if (prev != 7) { if (prev != 5) ent[282] = ent[144]; }` — ключ asda.Mob.FlinchRemembersCell.
	if (CVarMobFlinchRemembersCell.GetValueOnGameThread() != 0)
	{
		const int32 Prev = GetEtalonActionState();
		if (Prev != 7 && Prev != 5) { RememberServerCell(AsdaCoord::UeToGame2D(GetActorLocation())); }
	}
	bMoving = false;
	// ★ Эталон SetActionState case 0xD (state 13): `*((_WORD*)ent + 237) = 1` — флинч ставит «в бою»,
	//   и `if (prev != 5) ent[282] = ent[144]` — запоминает текущую клетку как целевую.
	bCombatStance = true;
	PlayMobSound(7);          // эталон: звук получения урона = фаза 7
	PlayClip(ClipHit, false); // клип 8 one-shot
	const float Len = CurrentClipLength();
	const float Hold = FMath::Clamp(Len > 0.f ? Len : 0.4f, 0.15f, 0.8f);
	HitAnimUntilS = GetWorld()->GetTimeSeconds() + (double)Hold;
	ASDA_CTRACE_MOB(NetRenderId, EAsdaCT::Fx, TEXT("моб rid=%d '%s': клип попадания anim_%02d (длина %.2f с, держит %.2f с — движение стоит), звук фазы 7, %s, HP %d/%d"),
		NetRenderId, *MobName, ClipHit, Len, Hold, AttackerGame.IsNearlyZero() ? TEXT("без разворота") : TEXT("развёрнут к атакующему"),
		CurHp, MaxHp);
	// ★★★ Конец клипа флинча ловит Tick (детектор bAnimGateWas) — он же СЛИВАЕТ буфер отложенного
	//   пакета (эталон charAction_tickWarp23@0xF79CE0). Прежний таймер, возвращавший клип idle/move,
	//   убран: он не знал про буфер и уводил моба в мирный покой вместо боевой стойки (SetActionState(6)).
	bAnimGateWas = true;
}

// ★ 05.11.2026 [ОРИГИНАЛ] отлёт — разбор в объявлении.
void AAsdaMobActor::NetKnockBack(int32 Type, const FVector2D& PushDirGame, const FVector2D& FaceDirGame)
{
	UWorld* W = GetWorld();
	if (!W || bDead || MobAggression == 3 || IsKnockActive() || (Type != 1 && Type != 2)) { return; }
	// `CCharacter_SetActionState` case 0xE (вход в 14): `if (prev != 5) ent[282] = ent[144]` — запомнить ТЕКУЩУЮ клетку как серверную
	//   (её читает доводчик `charCastDirectionalEffectIfFar` на конце клипа); звук фазы 9 (тип 1) / 10 (тип 2); клип слота 14 / 15.
	{ const int32 Prev = GetEtalonActionState(); if (Prev != 5 && Prev != 7) { RememberServerCell(AsdaCoord::UeToGame2D(GetActorLocation())); } }   // case 0xE: prev 7 — ветка целиком пропущена
	PlayMobSound(Type == 1 ? 9 : 10);
	const FVector2D Face = !FaceDirGame.IsNearlyZero() ? FaceDirGame : -PushDirGame;
	if (!Face.IsNearlyZero()) { FaceGameDir(Face); }   // эталон: `target+636..644 = Vector3_Negate(forward кастера)`
	bMoving = false;
	bCombatStance = true;              // SetActionState case 0xE: `*((_WORD*)ent + 237) = 1`
	AttackAnimUntilS = 0.0;
	const int32 Slot = (Type == 1) ? 14 : 15;
	PlayClip(Slot, false);
	const float Len = CurrentClipLength();
	// Конец состояния = конец клипа. Клипа у типа нет — сдвиг (600 мс) всё равно доводится, удержание — по его длине.
	const float Hold = (Len > 0.f) ? Len : (Type == 1 ? 0.6f : 0.2f);
	HitAnimUntilS = W->GetTimeSeconds() + (double)Hold;
	bAnimGateWas = true;               // конец удержания сливает отложенный серверный ход (эталон +1753)
	bKnockState = true;
	bKnockMove = (Type == 1) && !PushDirGame.IsNearlyZero();
	KnockStartG = AsdaCoord::UeToGame2D(GetActorLocation());
	KnockDirG = PushDirGame.GetSafeNormal();
	KnockElapsedMs = 0.f;
	// ★ 07.11.2026 [ИСПРАВЛЕНИЕ]: NpcServer переставляет моба в клетку отброса (`sub_4349A0`, зовут CounterAttack `sub_43C830` :43992 и 8215 `sub_436E90`)
	//   и пересчитывает путь к ТОЙ ЖЕ цели (:43998, :40481). Остаток отрезка считаем от точки отброса, иначе добег рассчитан на старое время.
	if (bSegActive && bKnockMove) { SegRemainS = FVector2D::Distance(KnockStartG + KnockDirG * 450.f, SegDstGame) / FMath::Max(1e-3f, SegSrvSpeed) / 1000.f; }
	ASDA_CTRACE_MOB(NetRenderId, EAsdaCT::Fx, TEXT("моб rid=%d '%s': ОТЛЁТ %s — клип слота %d (длина %.2f с), %s"), NetRenderId, *MobName,
		Type == 1 ? TEXT("сильный") : TEXT("слабый"), Slot, Len,
		bKnockMove ? *FString::Printf(TEXT("сдвиг 450 за 600 мс к (%.0f,%.0f)"), KnockStartG.X + KnockDirG.X * 450.f, KnockStartG.Y + KnockDirG.Y * 450.f)
		           : TEXT("без сдвига"));
}

float AAsdaMobActor::NetAttack(int32 Variant, const FVector2D& VictimGame, float AnimRate, bool bRoarAndFace, int32 AttackResult)
{
	if (bDead)
	{
		ASDA_CTRACE_IF(AsdaCombatTrace::IsEngaged(NetRenderId) || AsdaCombatTrace::IsUserRelevant(ChaseTargetUserIdx), EAsdaCT::MobAttack, TEXT("моб rid=%d '%s': замах НЕ играем — труп"), NetRenderId, *MobName);
		return 0.f;
	}
	// ★★★ ПОЧЕМУ ЗДЕСЬ bMoving = false — ПРОВЕРЕНО ПО БАЙТАМ (2026-08-11, mobmovefix-agent).
	//   План правки предлагал «не гасить движение в kind=3, эталон его не отменяет». Это НЕВЕРНО:
	//   `CCharacter_SetActionState(target, 3)` переводит сущность в состояние 3, а диспетчер тиков
	//   (entity_char.c:56793, `switch (ent[120] - 1)`) в состоянии 3 зовёт ТОЛЬКО
	//   `Combat_ApplyDamage@0xF7C470`, в котором НЕТ ни `effectNodeUpdateWorldPos`, ни
	//   `playerSetWorldPosition` — то есть кривая движения на время замаха НЕ продвигается и моб
	//   стоит. Движение возвращает не «несброшенный флаг», а СЛИВ БУФЕРА по концу клипа
	//   (combat_pvp.c:8168-8210) — он и реализован в FlushPendingNetAction.
	bMoving = false;
	// Эталон SetActionState case 3: `*((_WORD*)ent + 237) = 1` — замах ставит флаг «в бою».
	bCombatStance = true;
	// ★ Разворот к жертве и рык — принадлежность ВЕТКИ ПАКЕТА 4015 kind=3, а не состояния 3:
	//   `CCharacter_SetActionState@0xF876D0` case 3 делает ровно `ent[237] = 1; ent[175] = 0.0;` —
	//   ни звука, ни поворота. Запись 4013 в состоянии 3 (`--state == 2`, network.c:37256) зовёт
	//   только `playerSetWorldPosition(cur)` + `SetActionState(mob, 3)` ⇒ туда идёт bRoarAndFace=false.
	if (bRoarAndFace)
	{
		const FVector L = GetActorLocation();
		FaceGameDir(VictimGame - AsdaCoord::UeToGame2D(L));   // ★ Аудит №3 (B1)
		PlayMobSound(Variant ? 31 : 30);   // эталон kind=3: рык замаха (31 при варианте, иначе 30)
	}
	const float Rate = (AnimRate > 0.f) ? AnimRate : 1.f;
	// ★ 30.10.2026 [ОРИГИНАЛ] слот замаха: `ent[1743] >= −1` → вариант + 5, иначе 7 (крит бит 31, коды −2…); файл — по таблице слотов.
	const int32 AttackSlot = (GAsdaMobClipSlotMap != 0 && AttackResult < -1) ? 7 : ClipAttackBase + FMath::Clamp(Variant, 0, 2);
	PlayClip(AttackSlot, false, Rate);
	const float Len = CurrentClipLength();
	// Тик замаха считает `ent[113] += dt * ent[4826]` ⇒ клип длится Len/Rate секунд реального времени.
	const float Hold = FMath::Max(0.2f, (Len > 0.f ? Len : 1.0f) / Rate);
	AttackAnimUntilS = GetWorld()->GetTimeSeconds() + (double)Hold;
#if ASDA_COMBAT_TRACE
	// Журнал боя (волна 2, дубль): ветку 4015 kind=3 (рык + разворот) уровнем 1 пишет мир («4015 kind=3 … ЗАМАХ по игроку»,
	//   с жертвой и результатом) — здесь только частый уровень. Замах из 4013 (состояние 3) мир не пишет — тут уровень 1
	//   для нашего боя: моб наш (IsEngaged) ИЛИ его жертва-цель наша (ChaseTargetUserIdx).
	if (bRoarAndFace)
	{
		ASDA_CTRACE_V(EAsdaCT::MobAttack, TEXT("моб rid=%d '%s': ЗАМАХ вариант %d — клип anim_%02d длина %.2f с, темп ×%.3f → %.2f с реальн.; %s, жертва у (%.0f,%.0f), HP моба %d/%d"),
			NetRenderId, *MobName, Variant, ClipFileForSlot(AttackSlot), Len, Rate, Hold,
			Variant ? TEXT("рык 31 + разворот") : TEXT("рык 30 + разворот"), VictimGame.X, VictimGame.Y, CurHp, MaxHp);
	}
	else
	{
		ASDA_CTRACE_IF(AsdaCombatTrace::IsEngaged(NetRenderId) || AsdaCombatTrace::IsUserRelevant(ChaseTargetUserIdx), EAsdaCT::MobAttack,
			TEXT("моб rid=%d '%s': ЗАМАХ вариант %d — клип anim_%02d длина %.2f с, темп ×%.3f → %.2f с реальн.; без рыка/разворота (4013 в состоянии 3), жертва у (%.0f,%.0f), цель userIdx=%d, HP моба %d/%d"),
			NetRenderId, *MobName, Variant, ClipFileForSlot(AttackSlot), Len, Rate, Hold,
			VictimGame.X, VictimGame.Y, ChaseTargetUserIdx, CurHp, MaxHp);
	}
#endif
	// ★★★ Конец клипа замаха ловит Tick — он же сливает буфер отложенного пакета
	//   (эталон Combat_ApplyDamage: буфер есть → траектория, пусто → SetActionState(6)).
	bAnimGateWas = true;
	return Hold;
}

// ★ Единая точка запуска фейд-аута (посмертный / AoI). Эталон различает их флагами 4 и 0x100 и
//   уничтожает сущность по завершении (charRemoveEntity / charDespawnRemove) — у нас Destroy владельца.
void AAsdaMobActor::StartFadeOut(EAsdaMobFade Kind, float DurationS)
{
	if (Kind == EAsdaMobFade::None || FadeKind != EAsdaMobFade::None) { return; }   // второй фейд не вешаем
	ASDA_CTRACE_MOB(NetRenderId, EAsdaCT::Corpse, TEXT("моб rid=%d '%s': ФЕЙД %s %.2f с → по концу актёр удаляется (Destroy фейд-компонентом)%s"),
		NetRenderId, *MobName, Kind == EAsdaMobFade::Corpse ? TEXT("трупа") : TEXT("ухода из обзора (AoI)"), DurationS,
		(bDead && DeathAtS > 0.0 && GetWorld()) ? *FString::Printf(TEXT(", %.2f с после смерти"), GetWorld()->GetTimeSeconds() - DeathAtS) : TEXT(""));
	FadeKind = Kind;
	// ★ Аудит №4 (B3): через общую точку `Begin` (find-or-create). Безусловный `NewObject` вешал
	//   ВТОРОЙ фейд-компонент, если моб уже гас/появлялся — и второй принимал MID первого за
	//   «оригинальный материал» (разбор в шапке `UAsdaFadeComponent::Begin`).
	UAsdaFadeComponent::Begin(this, /*bIn=*/false, DurationS, /*bDestroyOwner=*/true);
}

void AAsdaMobActor::NetDeath()
{
	if (bDead)
	{
		ASDA_CTRACE_V(EAsdaCT::Death, TEXT("моб rid=%d '%s': повторная смерть — уже труп, пропуск"), NetRenderId, *MobName);
		return;
	}
	ASDA_CTRACE_MOB(NetRenderId, EAsdaCT::HpPlate, TEXT("моб rid=%d '%s': CurHp актёра %d → 0 (max %d) — смерть"), NetRenderId, *MobName, CurHp, MaxHp);
	ASDA_CTRACE_MOB(NetRenderId, EAsdaCT::Death, TEXT("моб rid=%d '%s': СМЕРТЬ у актёра — %s%s%s, пикинг и HP-полоса плашки сняты, звук фазы 4"),
		NetRenderId, *MobName, IsAttackAnimActive() ? TEXT("замах оборван") : TEXT("замаха не было"),
		Pending.bValid ? TEXT(", отложенный пакет выброшен") : TEXT(""), bMoving ? TEXT(", движение остановлено") : TEXT(""));
	bDead = true; bMoving = false;
	bSegActive = false; bGlide = false;   // ★ 07.11.2026
	CurHp = 0;
	DeathAtS = GetWorld() ? GetWorld()->GetTimeSeconds() : 0.0;
	AttackAnimUntilS = 0.0;   // труп не «доигрывает» замах
	bAnimGateWas = false;
	Pending.bValid = false;   // отложенный пакет трупу не применяется
	bCombatStance = false;    // эталон SetActionState case 0xA: `*((_WORD*)ent + 237) = 0`
	NetMoveState = 2;
	PlayMobSound(4);   // эталон SetActionState case 0xA: звук смерти = фаза 4
	// Эталон SetActionState(10): труп больше не цель/не пикается (снятие с тайл-грида + сброс таргет-HUD).
	if (PickHitbox) { PickHitbox->SetCollisionEnabled(ECollisionEnabled::NoCollision); }
	if (Nameplate) { Nameplate->bShowHp = false; Nameplate->bAlwaysShowName = false; Nameplate->RefreshNameplate(); }
	// ★★★ КРУГ 6 — ИСПРАВЛЕНИЕ ВЫВОДА КРУГА 5 («труп остаётся сущностью навсегда»). Флаг 16, по
	//   которому entityListUpdateFade@0xF78660 зовёт charRemoveEntity, ставится ИМЕННО ПОСМЕРТНЫМ
	//   ФЕЙДОМ: charAnimAdvanceActionTimer@0xF80930 по окончании death-клипа делает `flags |= 4` и
	//   `+0x258 = 1500.0f`, а charUpdateActionState23@0xF844F0 (`if (flags & 4) { f258 -= dt;
	//   alpha = f258/1500; ... else { flags &= ~4; flags |= 0x10; } }`) доводит его до нуля.
	//   ⇒ Труп В ОРИГИНАЛЕ ГАСНЕТ И УДАЛЯЕТСЯ через (длина death-клипа + фейд), rid освобождается,
	//   и пришедший через ~5 с серверный реген создаёт моба ЗАНОВО В СЕРВЕРНОЙ КЛЕТКЕ. Порт же держал
	//   труп вечно и «поднимал» его на месте смерти — юзер видел «при убийстве моб появляется сразу же».
	//   death-клип один раз (нелуповый SingleNode замирает на последнем кадре), затем — фейд в Tick.
	PlayClip(ClipDeath, false);
	// Длина death-клипа = момент, когда эталон взводит флаг 4 (`clipLen <= t`).
	const float ClipLen = (Mesh && Mesh->GetSingleNodeInstance()) ? Mesh->GetSingleNodeInstance()->GetLength() : 1.0f;
	CorpseFadeAtS = DeathAtS + (double)FMath::Clamp(ClipLen, 0.2f, 5.0f);
	UE_LOG(LogAsdaMob, Log, TEXT("[труп] rid=%d '%s': death-клип %.2fс → фейд %.2fс → удаление сущности (эталон флаги 4→0x10); респавн пришлёт СЕРВЕР"),
		NetRenderId, *MobName, ClipLen, CorpseFadeSeconds);
	ASDA_CTRACE_MOB(NetRenderId, EAsdaCT::Corpse, TEXT("моб rid=%d '%s': клип смерти anim_%02d %.2f с (без петли) → фейд трупа %.2f с через %.2f с → удаление; респавн пришлёт сервер"),
		NetRenderId, *MobName, ClipDeath, ClipLen, CorpseFadeSeconds, FMath::Clamp(ClipLen, 0.2f, 5.0f));
}

// ★★★ 18.10.2026 MaxHp МОБА ПАКЕТАМИ НЕ ПОДНИМАЕТСЯ — разбор у объявления `IsMaxHpFixed` (AsdaMobActor.h).
static ASDA_DEV_CVAR(int32, CVarAsdaMobMaxHpFixed,
	TEXT("asda.Combat.MobMaxHpFixed"), 1,
	TEXT("1 (по умолчанию) = максимум HP моба — из таблицы типа (DT_MobStats, синхронизирован с сервером); боевые пакеты HP (4015, ")
	TEXT("4029, лента) его не поднимают — HP > MaxHp: строка «АНОМАЛИЯ: HP > MaxHp» (раз на моба), полоса упирается в 100 %; каналы ")
	TEXT("спауна/респауна (4013, 4019, 4017 flags&4, оживление) HP выше таблицы считают серверным максимумом. 0 = прежний подъём MaxHp ")
	TEXT("вслед за HP пакета (дефект клиента прятался: 54/54 вместо 54/45)."),
	ECVF_Default);

bool AAsdaMobActor::IsMaxHpFixed()
{
	return CVarAsdaMobMaxHpFixed.GetValueOnGameThread() != 0;
}

void AAsdaMobActor::ApplyPacketHpToMax(int32 Hp, const TCHAR* Chan, bool bSpawnChan)
{
	if (Hp <= MaxHp) { return; }
	if (IsMaxHpFixed() && MobTypeMaxHp > 0 && !bSpawnChan)
	{
		// [ОРИГИНАЛ] max — из таблицы типа; боевое значение выше него — аномалия (сервер/таблица разошлись или арифметика клиента).
		if (!bHpAboveMaxNoted)
		{
			bHpAboveMaxNoted = true;
			UE_LOG(LogAsdaMob, Log, TEXT("[моб] rid=%d '%s': АНОМАЛИЯ: HP > MaxHp — %d > %d из %s; максимум не поднимается (таблица типа %d), полоса 100 %% (%s; строка — один раз на моба)"),
				NetRenderId, *MobName, Hp, MaxHp, Chan ? Chan : TEXT("?"), MobTypeMaxHp, ASDA_DEV_TEXT(TEXT("откат asda.Combat.MobMaxHpFixed 0"), TEXT("боевой канал")));
		}
		ASDA_CTRACE_MOB(NetRenderId, EAsdaCT::HpPlate, TEXT("моб rid=%d '%s': АНОМАЛИЯ: HP > MaxHp — %d > %d (канал %s, max типа %d): максимум НЕ поднимается, полоса упирается в 100 %%"),
			NetRenderId, *MobName, Hp, MaxHp, Chan ? Chan : TEXT("?"), MobTypeMaxHp);
		return;
	}
	ASDA_CTRACE_MOB(NetRenderId, EAsdaCT::HpPlate, TEXT("моб rid=%d '%s': MaxHp поднят %d → %d вслед за HP пакета (%s%s)"),
		NetRenderId, *MobName, MaxHp, Hp, Chan ? Chan : TEXT("?"),
		bSpawnChan ? TEXT(", канал спауна/респауна — серверный максимум") : ((MobTypeMaxHp > 0) ? TEXT(", ключ MobMaxHpFixed 0") : TEXT(", строки типа нет")));
	MaxHp = Hp;
}

// ★★★ ОЖИВЛЕНИЕ ТОЙ ЖЕ СУЩНОСТИ (эталон Recv_MobSpawn@0xF305E0, ветка «rid найден»):
//   `mob[283]=mob[284]=hp@9; mob[147]=10000.0f;  if (mob[121] & 0x100) { mob[121]=1;
//    array_releaseAllElements(mob); objArr_setField7Broadcast(mob,0); mob[146]=0; }`
//   — HP восстановлено, AoI-таймер взведён, ИДУЩИЙ ФЕЙД ОТМЕНЁН и альфа возвращена. Позицию ставит
//   вызывающий (4013 — снап на cur; 4019 — НЕ ставит вовсе).
void AAsdaMobActor::NetRevive(int32 NewHp)
{
	if (!bDead) { return; }
	// ★ КРУГ 9: посмертный фейд (эталонный флаг 4) НЕ отменяется ничем — charUpdateActionState23@0xF844F0
	//   доводит его до флага 0x10 и charRemoveEntity независимо от состояния/HP. Труп, начавший гаснуть,
	//   обречён; вызывающие это гейтят, но держим гейт и здесь (защита от будущих каналов).
	if (FadeKind == EAsdaMobFade::Corpse)
	{
		ASDA_CTRACE_MOB(NetRenderId, EAsdaCT::Corpse, TEXT("моб rid=%d '%s': оживление (HP %d) ОТКЛОНЕНО — труп уже гаснет, придёт новой сущностью"),
			NetRenderId, *MobName, NewHp);
		return;
	}
	ASDA_CTRACE_MOB(NetRenderId, EAsdaCT::HpPlate, TEXT("моб rid=%d '%s': CurHp актёра %d → %d (max %d%s) — оживление той же сущности"),
		NetRenderId, *MobName, CurHp, FMath::Max(1, NewHp), MaxHp,
		FMath::Max(1, NewHp) > MaxHp ? TEXT(", HP выше max — см. строку max ниже") : TEXT(""));
	ASDA_CTRACE_MOB(NetRenderId, EAsdaCT::Death, TEXT("моб rid=%d '%s': ОЖИЛ (труп не начал гаснуть) — покой, пикинг возвращён%s"),
		NetRenderId, *MobName, FadeKind == EAsdaMobFade::Aoi ? TEXT(", AoI-фейд отменён (фейд-ин)") : TEXT(""));
	bDead = false;
	bMoving = false;
	AttackAnimUntilS = 0.0;
	HitAnimUntilS = 0.0;
	bAnimGateWas = false;
	Pending.bValid = false;
	bCombatStance = false;   // ожившая сущность выходит в состояние 1 (эталон: хвост 4013 / 4017 flags&4)
	NetMoveState = 2;
	bServerDead = false;
	DeathAtS = 0.0; CorpseFadeAtS = 0.0;
	CurHp = FMath::Max(1, NewHp);
	ApplyPacketHpToMax(CurHp, TEXT("оживление"), /*bSpawnChan=*/true);   // ★ 18.10.2026: ключ asda.Combat.MobMaxHpFixed (0 — прежний подъём max)
	// ★ Отменяется ТОЛЬКО AoI-фейд — эталон Recv_MobSpawn@0xF305E0: `if (mob[121] & 0x100) { mob[121] = 1;
	//   array_releaseAllElements(mob); objArr_setField7Broadcast(mob, 0); mob[146] = 0; }` — сбрасывается
	//   именно флаг 0x100, а вместо него ставится бит 0 = ФЕЙД-ИН 1500 мс с альфы 0. Посмертный фейд
	//   (флаг 4) здесь НЕ снимается: в оригинале начавший гаснуть труп доводится до удаления, и моб
	//   приходит новой сущностью. Воспроизводим ровно это.
	//   ★ ДЕДУП Д12: своей копии снятия фейда здесь нет — она одна, `CancelAoiFade()` (ровно эта
	//   ветка эталона), и внутри стоит та же проверка `FadeKind == Aoi`.
	CancelAoiFade();
	SetActorHiddenInGame(false);
	SetActorEnableCollision(true);
	if (PickHitbox) { PickHitbox->SetCollisionEnabled(ECollisionEnabled::QueryOnly); }
	//   Плашка возвращается в исходное состояние моба (имя по наведению/цели — как в конструкторе).
	if (Nameplate) { Nameplate->bShowHp = false; Nameplate->bAlwaysShowName = false; Nameplate->RefreshNameplate(); }
	PlayClip(ClipIdle, true);
}

// ★ КРУГ 9: отмена AoI-фейда у ЖИВОГО моба повторной 4013 (эталон Recv_MobSpawn@0xF305E0:
//   `if (mob[121] & 0x100) { mob[121] = 1; array_releaseAllElements(mob); objArr_setField7Broadcast(mob,0);
//    mob[146] = 0; }` — снимается ТОЛЬКО 0x100, взамен бит 0 = фейд-ин 1500 мс с альфы 0).
void AAsdaMobActor::CancelAoiFade()
{
	if (FadeKind != EAsdaMobFade::Aoi) { return; }
	TArray<UAsdaFadeComponent*> Fades;
	GetComponents<UAsdaFadeComponent>(Fades);
	for (UAsdaFadeComponent* F : Fades) { if (F) { F->BeginFade(true, FadeInSeconds); } }
	FadeKind = EAsdaMobFade::None;
	SetActorHiddenInGame(false);
}

// ★ R4, доля a (14.11.2026; запись 11 от 15.11.2026 — прежний путь и ключ убраны) [ПОРТ, производительность]: признаки контроля
//   моба в кадровом тике. Прежний код у КАЖДОГО моба каждый кадр искал подсистему эффектов (`GetSubsystem`) и дважды искал
//   список состояний моба (`HasMobAnimFreeze`, `HasMobMoveLock`). Теперь подсистема держится слабой ссылкой в акторе, а оба
//   признака берутся одним поиском (`MobControlFlags`). Итог тот же: `bLock` включает `bFreeze`.
static void AsdaMobControlFlags(const UGameInstance* GI, TWeakObjectPtr<UAsdaEffectsSubsystem>& Cache, int32 RenderId, bool& bFreeze, bool& bLock)
{
	ASDA_PERF_SCOPE("Mob.ControlGate");
	const UAsdaEffectsSubsystem* Fx = Cache.Get();
	if (!Fx)
	{
		UAsdaEffectsSubsystem* Found = GI->GetSubsystem<UAsdaEffectsSubsystem>();
		Cache = Found;
		Fx = Found;
	}
	if (Fx)
	{
		bool bMoveLock = false;
		Fx->MobControlFlags(RenderId, bFreeze, bMoveLock);
		bLock = bFreeze || bMoveLock;
	}
}

void AAsdaMobActor::Tick(float DeltaSeconds)
{
	Super::Tick(DeltaSeconds);
	ON_SCOPE_EXIT { TickTurnSmooth(DeltaSeconds); };   // ★ 22.10.2026: доворот видимого меша — после всей логики кадра
	// ★★★ КРУГ 9: механизм PendingReviveHp УДАЛЁН. По байтам эталона 4019 труп НЕ поднимает:
	//   entityListUpdateFade@0xF78660 умеет только ЗАГОНЯТЬ в state 10 (`if (i[284] <= 0)
	//   SetActionState(i,10)`) — ветки «HP вернулся → выйти из 10» НЕ СУЩЕСТВУЕТ. Из state 10
	//   выводят ТОЛЬКО явные SetActionState-каналы: хвост 4013 (0xF309F3/0xF30AB6/0xF30BC1)
	//   и 4017 flags&4 (SetActionState(entity,1)). Труп с возвращённым по 4019 HP доигрывает
	//   смерть до удаления (тик state 10 → флаг 4 → фейд → 0x10 → charRemoveEntity).
	// ★★★ ПОСМЕРТНЫЙ ФЕЙД + УДАЛЕНИЕ ТРУПА (эталон: charAnimAdvanceActionTimer@0xF80930 по концу
	//   death-клипа взводит флаг 4 и таймер 1500 мс; charUpdateActionState23@0xF844F0 гасит альфу и
	//   ставит флаг 0x10; entityListUpdateFade@0xF78660 по флагу 16 зовёт charRemoveEntity).
	//   Именно это возвращает оригинальную картину «убитый моб гаснет и исчезает, а через ~5 с сервер
	//   спавнит его заново в СВОЕЙ клетке» вместо «встал ровно там, где умер».
	if (bDead && FadeKind == EAsdaMobFade::None && CorpseFadeAtS > 0.0
		&& GetWorld() && GetWorld()->GetTimeSeconds() >= CorpseFadeAtS)
	{
		UE_LOG(LogAsdaMob, Log, TEXT("[труп] rid=%d '%s': клип доигран (%.2fс с момента смерти) → фейд %.2fс → удаление"),
			NetRenderId, *MobName, GetWorld()->GetTimeSeconds() - DeathAtS, CorpseFadeSeconds);
		StartFadeOut(EAsdaMobFade::Corpse, CorpseFadeSeconds);
	}
	// ★★★ КОНЕЦ КЛИПА ЗАМАХА/ФЛИНЧА → СЛИВ БУФЕРА ОТЛОЖЕННОГО ПАКЕТА. Порт эталонного механизма,
	//   разложенного у оригинала по трём тикам состояний (Combat_ApplyDamage для 3,
	//   charAction_tickWarp23 для 13, effectObjectTickActionState/effectTimer_updateTick для 14):
	//   как только клип доигран — либо применяется отложенный пакет движения, либо моб уходит в
	//   боевую стойку (SetActionState(6)). Детектор строго на СПАДЕ: пакеты движения, которые
	//   намеренно снимают замах (NetMove/NetTrajectory/NetHalt/NetStop), гасят bAnimGateWas сами —
	//   там конец клипа не наступил и сливать нечего.
	// ★ 05.11.2026 [ОРИГИНАЛ] сильный отлёт: `charUpdateActionState23` case 13 — позиция = старт + dir·450·(t/600), на 600 мс — конец
	//   (без гейтов пути +6986: эталон двигает и под станом). Время — эффективное (`+6987` оледенение/окаменение — время стоит).
	if (bKnockMove && !bDead)
	{
		const UAsdaEffectsSubsystem* FxK = GetGameInstance() ? GetGameInstance()->GetSubsystem<UAsdaEffectsSubsystem>() : nullptr;
		const bool bFrozenK = FxK && FxK->HasMobAnimFreeze(NetRenderId);
		if (!bFrozenK) { KnockElapsedMs += DeltaSeconds * 1000.f; }
		const float T = FMath::Clamp(KnockElapsedMs / 600.f, 0.f, 1.f);
		NetSnapPos(KnockStartG + KnockDirG * (450.f * T));
		if (T >= 1.f) { bKnockMove = false; }
	}
	// ★ 07.11.2026 [ИСПРАВЛЕНИЕ] короткий проход к клетке сервера (см. NetSnapOrGlide); новое движение его отменяет.
	if (bGlide)
	{
		if (bDead || bMoving) { bGlide = false; }
		else
		{
			GlideElapsedS += DeltaSeconds;
			const float T = (GlideDurS > 0.f) ? FMath::Clamp(GlideElapsedS / GlideDurS, 0.f, 1.f) : 1.f;
			const FVector2D P = FMath::Lerp(GlideStartG, GlideDstG, T);
			const FVector2D UeG = AsdaCoord::GameToUe2D(P);
			SetActorLocation(FVector(UeG.X, UeG.Y, TerrainZ(UeG.X, UeG.Y)));
			if (T >= 1.f) { bGlide = false; }
		}
	}
	{
		const bool bGateNow = IsAttackAnimActive() || IsHitAnimActive();
		if (bAnimGateWas && !bGateNow) { bAnimGateWas = false; bKnockState = false; bKnockMove = false; FlushPendingNetAction(); }
		else { bAnimGateWas = bGateNow; }
	}
	// ★★★ 26.10.2026 (bowfx, баг «ловушка не видна — цель как будто не поймана») [ОРИГИНАЛ] флаги сущности, пересчитываемые каждый
	//   кадр в `charUpdateActionState23@0xF844F0` (entity_char.c:56783-56784): `+6986` (состояния 3/14/8/2/9 — стан, абсолютный стан,
	//   сон, ЛОВУШКА, замаскированная ловушка) — тики пути НЕ двигают позицию (`effect_UpdateProjectileTick`/`charUpdateMoveTick`:
	//   `playerSetWorldPosition` под `!6987 && !6986`); `+6987` (0/1 — оледенение/окаменение) — ещё и время анимации стоит
	//   (`effectiveDelta = 0`). Прежний порт доводил моба по старому маршруту — «попался в ловушку, а бежит». Ключ отката
	//   `asda.Mob.ControlFreeze 0` (проверяется внутри `HasMobMoveLock`/`HasMobAnimFreeze`).
	{
		static const FName kCtrlFreezeTag(TEXT("AsdaCtrlFreeze"));   // метка «анимацию заморозил этот гейт» (не путать со сном видео-настроек)
		bool bLock = false, bFreeze = false;
		if (bNetSpawned && !bDead && GetGameInstance())
		{
			// ★ R4, доля a (14.11.2026): чтение признаков — `AsdaMobControlFlags` (подсистема запомнена, один поиск списка состояний).
			AsdaMobControlFlags(GetGameInstance(), PerfFxSub, NetRenderId, bFreeze, bLock);
		}
		if (Mesh)
		{
			const bool bTagged = Tags.Contains(kCtrlFreezeTag);
			if (bFreeze && !bTagged)
			{
				Tags.Add(kCtrlFreezeTag);
				Mesh->bPauseAnims = true;
				ASDA_CTRACE_MOB(NetRenderId, EAsdaCT::Fx, TEXT("моб rid=%d '%s': оледенение/окаменение — анимация остановлена (+6987)"), NetRenderId, *MobName);
			}
			else if (!bFreeze && bTagged)
			{
				Tags.Remove(kCtrlFreezeTag);
				Mesh->bPauseAnims = false;
				ASDA_CTRACE_MOB(NetRenderId, EAsdaCT::Fx, TEXT("моб rid=%d '%s': заморозка снята — анимация идёт"), NetRenderId, *MobName);
			}
		}
		// ★ 07.11.2026 [ИСПРАВЛЕНИЕ] отрезок сервера: под станом сервер стоит и время отрезка не идёт. Актор, снятый с траектории, добегает.
		if (bSegActive && !bDead)
		{
			if (!bLock) { SegRemainS = FMath::Max(0.f, SegRemainS - DeltaSeconds); }
			if (!bLock && !bMoving && !bGlide && !IsMoveGated() && !IsAttackAnimActive() && !Pending.bValid) { ResumeServerSegment(); }
		}
		if (bLock && bMoving)
		{
			ASDA_CTRACE_V(EAsdaCT::Move, TEXT("моб rid=%d '%s': под контролем (+6986/+6987) — позиция по пути не сдвигается"), NetRenderId, *MobName);
			return;
		}
	}
	if (!bMoving || bDead || IsMoveGated()) { return; }   // эталон: движение стопорит ТОЛЬКО хит (state 13/14)
	// ★★★ ЗВУК ШАГА ПО ЦИКЛУ КЛИПА — эталон effect_UpdateProjectileTick@0xF7BDD0:
	//     if (ent[120] == 2) { if (ent[113] == 0) звук фазы 2; }   // ходьба
	//     else               { if (ent[113] == 0) звук фазы 12; }  // бег (состояния 5/7)
	//     ent[113] += dt * ent[122];                                // темп клипа из пакета @17
	//     ... по концу клипа ent[113] = 0  ⇒  звук повторяется каждый цикл анимации.
	if (bStepSoundDue) { bStepSoundDue = false; PlayStepSound(NetMoveState == 2 ? 2 : 12); }
	MoveClipTimeS += DeltaSeconds * MoveAnimRate;
	if (MoveClipLenS > 0.f && MoveClipTimeS > MoveClipLenS) { MoveClipTimeS = 0.f; bStepSoundDue = true; }
	// UE→game XZ текущей позиции.
	const FVector L = GetActorLocation();
	const FVector2D CurGame = AsdaCoord::UeToGame2D(L);   // ★ Аудит №3 (B1)
	const FVector2D Delta = MoveDstGame - CurGame;
	const float Dist = Delta.Size();
	const float Step = MoveSpeed * 1000.f * DeltaSeconds;
	if (Dist <= Step || Dist < 1.f)
	{
		bSegActive = false;   // ★ 07.11.2026: отрезок пройден
		// ★★★ ФЛАГ `*((_WORD*)ent + 3492)` (ставит ветка 4015 kind=0xE / её слив): тик прибытия
		//   `charAnim_UpdateActionTick@0xF80A50` при взведённом флаге ГАСИТ его и оставляет ТЕКУЩУЮ
		//   позицию (`playerSetWorldPosition(CClass_SetField177(ent))`) вместо снапа на конец кривой
		//   (`ent[132..134]`). Состояние ниже выставляется в обоих случаях одинаково.
		if (bKeepPosOnArrive) { bKeepPosOnArrive = false; }
		else
		{
			const FVector2D UeXY = AsdaCoord::GameToUe2D(MoveDstGame);   // ★ Аудит №3 (B1)
			SetActorLocation(FVector(UeXY.X, UeXY.Y, TerrainZ(UeXY.X, UeXY.Y)));
		}
		bMoving = false;
		ASDA_CTRACE_V(EAsdaCT::Move, TEXT("моб rid=%d '%s': прибыл в (%.0f,%.0f) (состояние %d) → %s"), NetRenderId, *MobName,
			MoveDstGame.X, MoveDstGame.Y, (int32)NetMoveState,
			IsActionLocked() ? TEXT("замах/попадание идёт — состояние решит слив буфера")
				: ((NetMoveState == 5 || (NetMoveState == 7 && bCombatStance)) ? TEXT("боевая стойка") : TEXT("покой")));
		// ★★★ КУДА МОБ УХОДИТ ПО ПРИБЫТИИ — по эталонным тикам состояний:
		//   · состояние 2 (ходьба)   — character_UpdateMoveActionState@0xF7B6D0 → SetActionState(1), покой;
		//   · состояние 5 (бег/агро) — charAnim_UpdateActionTick@0xF80A50   → SetActionState(6) БЕЗУСЛОВНО;
		//   · состояние 7 (боевое)   — charUpdateMoveTick@0xF80D20:
		//         `if (ent[237]) SetActionState(6); else SetActionState(1);` — «в бою → стойка, иначе покой».
		if (!IsActionLocked())   // замах/хит не перебиваем — их конец обработает слив буфера
		{
			if      (NetMoveState == 5) { EnterCombatIdle(); }
			else if (NetMoveState == 7) { bCombatStance ? EnterCombatIdle() : EnterIdle(); }
			else                        { EnterIdle(); }
		}
		return;
	}
	const FVector2D Dir = Delta / Dist;
	const FVector2D NewGame = CurGame + Dir * Step;
	const FVector2D UeXY = AsdaCoord::GameToUe2D(NewGame);   // ★ Аудит №3 (B1)
	SetActorLocation(FVector(UeXY.X, UeXY.Y, TerrainZ(UeXY.X, UeXY.Y)));
	FaceGameDir(Dir);
}

// ── ★ ДОКАЗАТЕЛЬНЫЙ ДАМП ПЛАШЕК МОБОВ (высота + цвет имени по разнице уровней) ────────────────
//   Печатает по каждому живому мобу мира: откуда взята высота, уровни/дельту и итоговый цвет.
//   Проверять после любой правки высоты/цвета — это тот самый «рантайм-пруф» из закона
//   [[nameplate-height-from-data]] и [[full-structural-analysis-always]].
#if ASDA_DEV_CONSOLE   // ★ античит А2: дев-команда, в Shipping не регистрируется
static FAutoConsoleCommandWithWorldAndArgs GAsdaMobPlateDump(
	TEXT("Asda.Mobs.PlateDump"),
	TEXT("Плашки мобов: высота (источник) + уровень моба/игрока, дельта и цвет имени. "
	     "С аргументом <n> — ограничить вывод n строками."),
	FConsoleCommandWithWorldAndArgsDelegate::CreateStatic(
		[](const TArray<FString>& Args, UWorld* World)
		{
			if (!World) { return; }
			const int32 Limit = Args.Num() >= 1 ? FMath::Max(1, FCString::Atoi(*Args[0])) : 200;
			const int32 PlayerLevel = UAsdaNameplateComponent::ResolveSelfLevel(World);
			int32 N = 0, Fixed = 0, Fallback = 0;
			for (TActorIterator<AAsdaMobActor> It(World); It; ++It)
			{
				AAsdaMobActor* M = *It;
				if (!M || M->IsHidden()) { continue; }
				const float H = M->ResolvePlateHeight();
				(H > 0.f) ? ++Fixed : ++Fallback;
				bool bHas = false; int32 Diff = 0;
				const FLinearColor C = UAsdaNameplateComponent::ResolveMobNameColor(
					M->MobLevel, PlayerLevel, M->MobAggression, bHas, Diff);
				const FColor S = C.ToFColor(true);
				const float ShownH = (H > 0.f) ? H
					: (M->Nameplate ? M->Nameplate->GetRelativeLocation().Z * FMath::Abs(M->GetActorScale3D().Z) : 0.f);
				UE_LOG(LogAsdaMob, Log,
					TEXT("[плашка] моб rid=%d id=%d '%s' H=%.1f (источник: %s) | уровень моба=%d, игрока=%d, дельта=%d → цвет #%02X%02X%02X%s"),
					M->NetRenderId, M->MobId, *M->MobName, ShownH,
					H > 0.f ? TEXT("Mob list.BIN +0x84+4 halfY → 2*halfY+50")
					        : TEXT("верх капсулы + оффсет (эталонных данных нет)"),
					M->MobLevel, PlayerLevel, Diff, S.R, S.G, S.B,
					bHas ? TEXT("") : TEXT(" (правило цвета не сработало: агрессия вне 1..5)"));
				if (++N >= Limit) { break; }
			}
			UE_LOG(LogAsdaMob, Log,
				TEXT("[плашка] ИТОГО мобов %d: по данным Mob list.BIN %d, фолбэк по капсуле %d; уровень игрока=%d"),
				N, Fixed, Fallback, PlayerLevel);
		}));
#endif // ASDA_DEV_CONSOLE
