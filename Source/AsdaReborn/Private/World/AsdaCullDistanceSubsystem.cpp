// Copyright (c) 2026 @sitxovski (Discord)
#include "World/AsdaCullDistanceSubsystem.h"
#include "AsdaDevConsole.h"   // ★ античит А2: ASDA_DEV_CVAR / ASDA_DEV_CONSOLE — дев-ключи и команды вне Shipping
#include "World/AsdaCullSettings.h"
#include "AsdaEffectActor.h"
#include "AsdaSkyDomeActor.h"
#include "AsdaLayeredCloudsActor.h"
#include "AsdaCloudFieldActor.h"
#include "Components/PrimitiveComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Components/InstancedStaticMeshComponent.h"
#include "Components/SkinnedMeshComponent.h"
#include "Components/ExponentialHeightFogComponent.h"
#include "World/AsdaViewRangeSubsystem.h"   // ★ 06.11.2026: дальность видимости
#include "Engine/StaticMesh.h"
#include "Engine/World.h"
#include "Engine/Engine.h"
#include "GameFramework/Pawn.h"
#include "GameFramework/PlayerController.h"
#include "Kismet/GameplayStatics.h"
#include "EngineUtils.h"
#include "UObject/UObjectIterator.h"
#include "Scalability.h"
#include "HAL/IConsoleManager.h"

DEFINE_LOG_CATEGORY_STATIC(LogAsdaCull, Log, All);

// Тег актора, который запрещает отсечку его примитивов (аварийный люк для карт/BP без правки кода)
static const FName GAsdaNoCullTag(TEXT("AsdaNoCull"));

// ─────────────── asda.Cull.Enable (регистрация единственная в Source/: grep "asda\.Cull" 08.09.2026 = 0) ───────────────
// Общий рубильник И КЛЮЧ ОТКАТА: 1 = отсечка работает, 0 = всем компонентам возвращаются авторские значения
// (MaxDrawDistance/InstanceEndCullDistance из ассета, у мира это 0 = «никогда не отсекать») — ровно поведение до правки.
// Персист [AsdaRender] (GRenderCVarKeys в AsdaDevMenuSubsystem.cpp), строка Ctrl+N «Отсечка дальности».
static int32 GAsdaCullEnable = 1;
static void OnAsdaCullKeyChanged(IConsoleVariable*) { UAsdaCullDistanceSubsystem::MarkAllDirty(); }
static FAutoConsoleVariableRef CVarAsdaCullEnable(
	TEXT("asda.Cull.Enable"),
	GAsdaCullEnable,
	TEXT("Отсечка дальности прорисовки геометрии мира: 1 = включена (дальности из [/Script/AsdaReborn.AsdaCullSettings]), 0 = ОТКАТ (авторские значения ассетов, ничего не отсекается)."),
	FConsoleVariableDelegate::CreateStatic(&OnAsdaCullKeyChanged),
	ECVF_Default);

// asda.Cull.Scale — общий множитель радиусов поверх правил классов (ползунок Ctrl+N «Дальность прорисовки», 0.5…2.0).
// Множится ДО зажима Min/Max? Нет — ПОСЛЕ: игрок тянет итоговую дальность, а не параметр формулы (иначе ползунок «залипал»
// бы на зажатых классах). Дальше движок домножает на r.ViewDistanceScale уровня — второй раз мы уровень не учитываем.
static float GAsdaCullScale = 1.f;
static FAutoConsoleVariableRef CVarAsdaCullScale(
	TEXT("asda.Cull.Scale"),
	GAsdaCullScale,
	TEXT("Множитель ко всем дальностям отсечки (ползунок «Дальность прорисовки» в Ctrl+N; 1.0 = радиусы конфига как есть). Умолчание в коде 1.0; в проекте задано 1.2571 в Config/DefaultEngine.ini [SystemSettings] (подбор владельца 09.09.2026)."),
	FConsoleVariableDelegate::CreateStatic(&OnAsdaCullKeyChanged),
	ECVF_Default);

// asda.Cull.DumpAfterSec — отложенный дамп после загрузки мира. Нужен офлайн-стенду замера: -ExecCmds выполняется ДО
// построения мира (Asda.Load.CurrentMap только запускает загрузку), поэтому немедленный дамп показал бы пустую сцену.
// > 0 = через столько секунд после BeginPlay напечатать таблицу классов И выполнить движковый DumpDetailedPrimitives
// (CSV Saved/Profiling/Primitives — оттуда берётся состав draw calls).
static float GAsdaCullDumpAfterSec = 0.f;
static void OnAsdaCullDumpAfterSecChanged(IConsoleVariable*) { UAsdaCullDistanceSubsystem::ArmDumpAll(GAsdaCullDumpAfterSec); }
static ASDA_DEV_CVAR_REF(CVarAsdaCullDumpAfterSec,
	TEXT("asda.Cull.DumpAfterSec"),
	GAsdaCullDumpAfterSec,
	TEXT("Через сколько секунд после загрузки мира напечатать asda.Cull.Dump и выполнить DumpDetailedPrimitives (0 = не надо)."),
	FConsoleVariableDelegate::CreateStatic(&OnAsdaCullDumpAfterSecChanged),
	ECVF_Default);

// asda.Cull.FogFloor — рантайм-переопределение множителя привязки к туману (UAsdaCullSettings::FogSafetyFactor).
// −1 = брать из конфига; ≥ 0 = взять это значение (0 = привязку к туману не применять). Нужен, чтобы развёртка по
// радиусам на замере не упиралась в пол, и чтобы владелец мог подобрать запас не пересобирая игру.
static float GAsdaCullFogFloor = -1.f;
static FAutoConsoleVariableRef CVarAsdaCullFogFloor(
	TEXT("asda.Cull.FogFloor"),
	GAsdaCullFogFloor,
	TEXT("Множитель привязки пола видимости к началу тумана: -1 = из конфига (FogSafetyFactor), 0 = привязку не применять, >0 = этот множитель."),
	FConsoleVariableDelegate::CreateStatic(&OnAsdaCullKeyChanged),
	ECVF_Default);

// ★ asda.Cull.VisFloor — рантайм-переопределение ПОЛА ВИДИМОСТИ в юнитах (UAsdaCullSettings::VisibilityFloorMin).
// −1 = брать из конфига; ≥ 0 = взять это число. Ради него и заведён ключ: развёртка «сколько пикселей меняется на
// радиусе X» (drawdistance_report.md §4) делается одним прогоном без пересборки и без правки ini на стенде.
static float GAsdaCullVisFloor = -1.f;
static FAutoConsoleVariableRef CVarAsdaCullVisFloor(
	TEXT("asda.Cull.VisFloor"),
	GAsdaCullVisFloor,
	TEXT("Пол видимости отсечки, юн: -1 = из конфига (VisibilityFloorMin, 15000), 0 = пола нет (правят только числа классов), >0 = это число."),
	FConsoleVariableDelegate::CreateStatic(&OnAsdaCullKeyChanged),
	ECVF_Default);

// ★ Счётчик полных проходов мира за процесс (общий на все миры). Растёт только когда проход реально выполнен — по нему
// самопроверка ползунка (asda.DevMenu.SelfTest culldrag) доказывает, что перетаскивание больше не даёт проход на кадр.
static int32 GAsdaCullApplyPasses = 0;

// Рантайм-переопределение правила класса (asda.Cull.Set) — для замерных развёрток без пересборки и переупаковки.
// Пусто = правило берётся из UAsdaCullSettings (Config/DefaultGame.ini). Числа проекта живут ТОЛЬКО в ini.
static FAsdaCullClassSpec GClassOverride[(int32)EAsdaCullClass::Count];
static bool GHasClassOverride[(int32)EAsdaCullClass::Count] = { false };

// Имя класса для консоли/лога (короткое, без префикса перечисления)
static const TCHAR* CullClassName(EAsdaCullClass C)
{
	switch (C)
	{
	case EAsdaCullClass::Terrain:   return TEXT("Terrain");
	case EAsdaCullClass::Sky:       return TEXT("Sky");
	case EAsdaCullClass::Prop:      return TEXT("Prop");
	case EAsdaCullClass::Instanced: return TEXT("Instanced");
	case EAsdaCullClass::Skeletal:  return TEXT("Skeletal");
	case EAsdaCullClass::Effect:    return TEXT("Effect");
	default:                        return TEXT("?");
	}
}

// Класс по имени из консоли (регистр не важен); Count = не распознан
static EAsdaCullClass CullClassFromName(const FString& S)
{
	for (int32 i = 0; i < (int32)EAsdaCullClass::Count; ++i)
	{
		if (S.Equals(CullClassName((EAsdaCullClass)i), ESearchCase::IgnoreCase)) { return (EAsdaCullClass)i; }
	}
	return EAsdaCullClass::Count;
}

// Действующее правило класса: рантайм-переопределение (asda.Cull.Set) главнее ini
static const FAsdaCullClassSpec* EffectiveSpec(EAsdaCullClass C)
{
	const int32 i = (int32)C;
	if (i >= 0 && i < (int32)EAsdaCullClass::Count && GHasClassOverride[i]) { return &GClassOverride[i]; }
	return UAsdaCullSettings::Get()->FindClass(C);
}

// ─────────────── asda.Cull.Dump — таблица классов текущего мира в лог ───────────────
#if ASDA_DEV_CONSOLE   // ★ античит А2: дев-команда, в Shipping не регистрируется
static FAutoConsoleCommandWithWorld CmdAsdaCullDump(
	TEXT("asda.Cull.Dump"),
	TEXT("Таблица отсечки дальности: правило и итоговая дальность по каждому классу, сколько примитивов затронуто, сколько пропущено (авторская разметка/исключения)."),
	FConsoleCommandWithWorldDelegate::CreateLambda([](UWorld* W)
	{
		if (UAsdaCullDistanceSubsystem* S = W ? W->GetSubsystem<UAsdaCullDistanceSubsystem>() : nullptr) { S->Dump(); }
		else { UE_LOG(LogAsdaCull, Warning, TEXT("asda.Cull.Dump: в этом мире подсистемы отсечки нет (не игровой мир)")); }
	}));
#endif // ASDA_DEV_CONSOLE

// ─────────────── asda.Cull.Set <класс> <SizeFactor> <Min> <Max> — правка правила на лету ───────────────
#if ASDA_DEV_CONSOLE   // ★ античит А2: дев-команда, в Shipping не регистрируется
static FAutoConsoleCommand CmdAsdaCullSet(
	TEXT("asda.Cull.Set"),
	TEXT("asda.Cull.Set <Terrain|Sky|Prop|Instanced|Skeletal|Effect> <SizeFactor> <MinDist> <MaxDist> — переопределить правило класса на лету (0 0 0 = класс не отсекать). Без аргументов — снять все переопределения."),
	FConsoleCommandWithArgsDelegate::CreateLambda([](const TArray<FString>& Args)
	{
		if (Args.Num() == 0)
		{
			for (int32 i = 0; i < (int32)EAsdaCullClass::Count; ++i) { GHasClassOverride[i] = false; }
			UE_LOG(LogAsdaCull, Log, TEXT("asda.Cull.Set: все рантайм-переопределения сняты — правила снова из Config/DefaultGame.ini"));
			UAsdaCullDistanceSubsystem::MarkAllDirty();
			return;
		}
		if (Args.Num() < 4)
		{
			UE_LOG(LogAsdaCull, Warning, TEXT("asda.Cull.Set: нужно 4 аргумента — <класс> <SizeFactor> <MinDist> <MaxDist>"));
			return;
		}
		const EAsdaCullClass C = CullClassFromName(Args[0]);
		if (C == EAsdaCullClass::Count)
		{
			UE_LOG(LogAsdaCull, Warning, TEXT("asda.Cull.Set: класс '%s' не распознан (Terrain|Sky|Prop|Instanced|Skeletal|Effect)"), *Args[0]);
			return;
		}
		FAsdaCullClassSpec S;
		S.SizeFactor  = FCString::Atof(*Args[1]);
		S.MinDistance = FCString::Atof(*Args[2]);
		S.MaxDistance = FCString::Atof(*Args[3]);
		// Все нули = «класс не отсекать» (удобный способ выключить класс на замере)
		S.bCull = !(S.SizeFactor <= 0.f && S.MinDistance <= 0.f && S.MaxDistance <= 0.f);
		// Привязку к туману сохраняем ту же, что у правила из ini (переопределение меняет только числа)
		if (const FAsdaCullClassSpec* Base = UAsdaCullSettings::Get()->FindClass(C)) { S.bUseFogFloor = Base->bUseFogFloor; }
		GClassOverride[(int32)C] = S;
		GHasClassOverride[(int32)C] = true;
		UE_LOG(LogAsdaCull, Log, TEXT("asda.Cull.Set: %s → отсекать=%d ×%.1f зажим %.0f..%.0f юн (туман учитывать=%d)"),
			CullClassName(C), S.bCull ? 1 : 0, S.SizeFactor, S.MinDistance, S.MaxDistance, S.bUseFogFloor ? 1 : 0);
		UAsdaCullDistanceSubsystem::MarkAllDirty();
	}));
#endif // ASDA_DEV_CONSOLE

// ───────────────────────────── Подсистема ─────────────────────────────

bool UAsdaCullDistanceSubsystem::ShouldCreateSubsystem(UObject* Outer) const
{
	if (!Super::ShouldCreateSubsystem(Outer)) { return false; }
	// Только игровые миры: Game всегда; PIE — по настройке; редакторские/превью — никогда (владелец правит карту целиком)
	const UWorld* W = Cast<UWorld>(Outer);
	if (!W) { return false; }
	// Мир из списка исключений (сцена логина, экран загрузки): там отсечка не нужна и вредна
	if (UAsdaCullSettings::Get()->SkipWorlds.Contains(FName(*W->GetName()))) { return false; }
	if (W->WorldType == EWorldType::Game) { return true; }
	if (W->WorldType == EWorldType::PIE)  { return UAsdaCullSettings::Get()->bApplyInPIE; }
	return false;
}

// ★ волна 3 п.12 (15.09.2026): Initialize/Deinitialize — в UAsdaRenderQualitySubsystemBase (смена уровня → OnQualityLevelsChanged → MarkDirty)

void UAsdaCullDistanceSubsystem::OnWorldBeginPlay(UWorld& InWorld)
{
	Super::OnWorldBeginPlay(InWorld);
	// Новые акторы (NPC, мобы, лут, чужие игроки) появляются всю сессию — их примитивы дорабатываем отложенно
	BindActorSpawned(InWorld);   // ★ волна 3 п.12 (15.09.2026): подписка — в базе (вызов уходит в OnActorSpawned ниже)
	Authored.Reset();
	Pending.Reset();
	MarkDirty();
	// Отложенный дамп для офлайн-стенда (asda.Cull.DumpAfterSec задан в -ExecCmds ещё до загрузки мира)
	DumpTimer = GAsdaCullDumpAfterSec > 0.f ? GAsdaCullDumpAfterSec : -1.f;
}

void UAsdaCullDistanceSubsystem::OnActorSpawned(AActor* A)
{
	if (!A || !IsValid(A)) { return; }
	// Два прохода: сразу (0.2 с — компоненты конструктора уже зарегистрированы) и позже (2 с — сборка персонажа
	// UAsdaCharAssembly доцепляет меши экипировки уже после спавна).
	Pending.Add({ A, 0.2f, 2 });
}

void UAsdaCullDistanceSubsystem::Tick(float DeltaTime)
{
	Super::Tick(DeltaTime);

	// ★★★ THROTTLE ПОЛНОГО ПРОХОДА (ревизия review_drawdistance.md §6). Ползунок «Дальность прорисовки» ставит
	//   asda.Cull.Scale КАЖДЫЙ КАДР перетаскивания, каждый Set зовёт MarkAllDirty — без ограничителя это полный обход
	//   мира по 3238 компонентов на кадр. Ограничитель НЕ ТЕРЯЕТ последнее значение: bDirty снимается только фактическим
	//   проходом, поэтому после отпускания ручки ближайший разрешённый тик доводит настройку до всех классов
	//   (доказывается счётчиком LastAppliedScale в asda.DevMenu.SelfTest culldrag).
	if (bDirty)
	{
		const float Throttle = FMath::Max(UAsdaCullSettings::Get()->ApplyThrottleSec, 0.f);
		const double Now = FPlatformTime::Seconds();
		if (LastApplyTimeSec < 0.0 || Throttle <= 0.f || (Now - LastApplyTimeSec) >= (double)Throttle)
		{
			bDirty = false;
			Pending.Reset();   // полный проход накрывает и то, что ждало доработки
			Apply(/*bLog*/true);
		}
	}
	else if (Pending.Num() > 0)
	{
		// Доработка появившихся акторов: по таймеру каждого, без полного обхода мира
		int32 T = 0, Au = 0, Ex = 0;
		for (int32 i = Pending.Num() - 1; i >= 0; --i)
		{
			FPending& P = Pending[i];
			AActor* A = P.Actor.Get();
			if (!A || !IsValid(A)) { Pending.RemoveAtSwap(i); continue; }
			P.TimeLeft -= DeltaTime;
			if (P.TimeLeft > 0.f) { continue; }
			ApplyToActor(A, T, Au, Ex);
			if (--P.PassesLeft <= 0) { Pending.RemoveAtSwap(i); }
			else                      { P.TimeLeft = 1.8f; }   // второй проход — после сборки персонажа
		}
		// Итоги Last* описывают ПОСЛЕДНИЙ ПОЛНЫЙ проход — доработка Pending в них не подмешивается (иначе один актор,
		// обработанный двумя проходами, считался бы дважды). Её результат виден отдельной строкой Verbose.
		UE_CLOG(T + Au + Ex > 0, LogAsdaCull, Verbose, TEXT("[Отсечка] доработка появившихся акторов: проставлено %d, авторская разметка %d, исключения %d"), T, Au, Ex);
	}

	if (DumpTimer > 0.f)
	{
		DumpTimer -= DeltaTime;
		if (DumpTimer <= 0.f)
		{
			DumpTimer = -1.f;
			Dump();
#if !UE_BUILD_SHIPPING
			// Движковый дамп видимых примитивов с числом draw calls (Saved/Profiling/Primitives/PrimitivesDetailed-*.csv,
			// Renderer/Private/ViewDebug.cpp:35). Из него считается состав кадра по классам.
			if (GEngine) { GEngine->Exec(GetWorld(), TEXT("DumpDetailedPrimitives")); }
#endif
		}
	}
}

EAsdaCullClass UAsdaCullDistanceSubsystem::ClassifyPrimitive(const UPrimitiveComponent* C)
{
	if (!C) { return EAsdaCullClass::Count; }
	const AActor* Owner = C->GetOwner();

	// Небо и облака — свои акторы порта; видны всегда, отсекать нельзя
	if (Owner && (Owner->IsA(AAsdaSkyDomeActor::StaticClass())
	           || Owner->IsA(AAsdaLayeredCloudsActor::StaticClass())
	           || Owner->IsA(AAsdaCloudFieldActor::StaticClass())))
	{
		return EAsdaCullClass::Sky;
	}
	// Эффекты порта (AAsdaEffectActor) — у них своя отсечка (AsdaEffectActor.cpp:729, поле CullDistance)
	if (Owner && Owner->IsA(AAsdaEffectActor::StaticClass()))
	{
		return EAsdaCullClass::Effect;
	}
	// Скелетал (NPC, мобы, чужие игроки, питомцы, ездовые)
	if (C->IsA(USkinnedMeshComponent::StaticClass()))
	{
		return EAsdaCullClass::Skeletal;
	}
	// ISM/HISM карты — ПРОВЕРКА РАНЬШЕ UStaticMeshComponent: UInstancedStaticMeshComponent от него наследуется
	if (C->IsA(UInstancedStaticMeshComponent::StaticClass()))
	{
		return EAsdaCullClass::Instanced;
	}
	if (const UStaticMeshComponent* SMC = Cast<UStaticMeshComponent>(C))
	{
		// Террейн/вода/купол неба карты — Tools/asda_build_map_level.py кладёт их одним мешем на всю карту
		// (build_terrain «SM_terrain», build_water «SM_water», build_sky «SM_sky») в обычный AStaticMeshActor.
		if (const UStaticMesh* M = SMC->GetStaticMesh())
		{
			const FString N = M->GetName();
			if (N.StartsWith(TEXT("SM_terrain"), ESearchCase::IgnoreCase) || N.StartsWith(TEXT("SM_water"), ESearchCase::IgnoreCase))
			{
				return EAsdaCullClass::Terrain;
			}
			if (N.StartsWith(TEXT("SM_sky"), ESearchCase::IgnoreCase))
			{
				return EAsdaCullClass::Sky;
			}
		}
		// Статичный меш на акторе-персонаже (оружие, шлем, крылья) — судьба та же, что у его скелетала
		if (Owner)
		{
			TInlineComponentArray<USkinnedMeshComponent*> Skins;
			const_cast<AActor*>(Owner)->GetComponents<USkinnedMeshComponent>(Skins);
			if (Skins.Num() > 0) { return EAsdaCullClass::Skeletal; }
		}
		return EAsdaCullClass::Prop;
	}
	// Всё остальное (процедурные меши не-эффектов, текст, отладочные примитивы) — не наше дело
	return EAsdaCullClass::Count;
}

bool UAsdaCullDistanceSubsystem::IsExcluded(const UPrimitiveComponent* C) const
{
	if (!C || !IsValid(C) || !C->IsRegistered()) { return true; }
	const AActor* Owner = C->GetOwner();
	if (!Owner) { return true; }
	// Аварийный люк карт/BP
	if (Owner->ActorHasTag(GAsdaNoCullTag)) { return true; }
	// Павн локального игрока и то, к чему он привязан (сам себя не гасим ни при каких радиусах)
	if (const UWorld* W = GetWorld())
	{
		if (const APlayerController* PC = UGameplayStatics::GetPlayerController(const_cast<UWorld*>(W), 0))
		{
			const AActor* VT = PC->GetViewTarget();
			const APawn* P = PC->GetPawn();
			for (const AActor* A = Owner; A; A = A->GetAttachParentActor())
			{
				if (A == VT || A == P) { return true; }
			}
		}
	}
	return false;
}

bool UAsdaCullDistanceSubsystem::GetWorldFog(float& OutStart, float& OutDensity, float& OutMaxOpacity) const
{
	OutStart = 0.f; OutDensity = 0.f; OutMaxOpacity = 0.f;
	const UWorld* W = GetWorld();
	if (!W) { return false; }
	// Самый дальний туман мира (StartDistance и FogDensity уже несут asda.Fog.DistanceScale — их ставит
	// UAsdaDevMenuSubsystem::ApplyFogScale прямо в компонент)
	const UExponentialHeightFogComponent* Best = nullptr;
	for (TObjectIterator<UExponentialHeightFogComponent> It; It; ++It)
	{
		const UExponentialHeightFogComponent* C = *It;
		if (!C || C->IsTemplate() || C->GetWorld() != W) { continue; }
		if (!Best || C->StartDistance > Best->StartDistance) { Best = C; }
	}
	if (!Best) { return false; }
	OutStart = Best->StartDistance;
	OutDensity = Best->FogDensity;
	OutMaxOpacity = Best->FogMaxOpacity;
	return true;
}

float UAsdaCullDistanceSubsystem::FogOpacityAt(float L) const
{
	float Start = 0.f, Density = 0.f, MaxOp = 0.f;
	if (!GetWorldFog(Start, Density, MaxOp) || Density <= 0.f || L <= Start) { return 0.f; }
	// (ln2)² = 0.4805: LineIntegral шейдера при нулевом height falloff даёт множитель ln2, а ExpFogFactor = 2^(−Integral)
	// ⇒ T = e^(−(ln2)²·D·L), D = density/1000 (SceneCore.cpp:404). Разбор — review_setA_fix.md §2.
	const float K = FMath::Square(UE_LN2) * Density / 1000.f;
	return FMath::Min(1.f - FMath::Exp(-K * (L - Start)), MaxOp);
}

float UAsdaCullDistanceSubsystem::FogDistanceForOpacity(float Target) const
{
	float Start = 0.f, Density = 0.f, MaxOp = 0.f;
	if (!GetWorldFog(Start, Density, MaxOp) || Density <= 0.f) { return 0.f; }
	// Цель выше потолка непрозрачности компонента недостижима ни на какой дистанции
	if (Target <= 0.f || Target >= 1.f || Target > MaxOp) { return 0.f; }
	const float K = FMath::Square(UE_LN2) * Density / 1000.f;
	return Start + FMath::Loge(1.f / (1.f - Target)) / K;
}

float UAsdaCullDistanceSubsystem::VisibilityFloor() const
{
	const UAsdaCullSettings* S = UAsdaCullSettings::Get();
	// Пол видимости: рантайм-ключ главнее конфига (−1 = ключ не задан). ★ Число 15 000 — из ПИКСЕЛЬНОГО замера
	// (alpen, 5 камер: шум 1.26 %, 31250 → 2.38 %, 18750 → 4.80 %, 15000 → 5.79 %, 7500 → 14.60 %), а НЕ из физики
	// тумана: на 18 750 эффективных непрозрачность тумана alpen всего 62 % (ревизия review_drawdistance.md §1).
	const float FloorMin = GAsdaCullVisFloor >= 0.f ? GAsdaCullVisFloor : S->VisibilityFloorMin;
	// Привязка к началу тумана только ПОДНИМАЕТ пол — на картах с дальним туманом (flammio, sunnycoast, battlefield,
	// silalis, conquester) 15 000 юн мало, там туман ещё не начался. Опустить пол ниже FloorMin она не может.
	const float Factor = GAsdaCullFogFloor >= 0.f ? GAsdaCullFogFloor : S->FogSafetyFactor;
	float Start = 0.f, Density = 0.f, MaxOp = 0.f;
	const float FogPart = (Factor > 0.f && GetWorldFog(Start, Density, MaxOp)) ? Start * Factor : 0.f;
	const float Floor = FMath::Max(FloorMin, FogPart);
	if (Floor <= 0.f) { return 0.f; }
	// Потолок: выше дальности динамических теней карты (20 000 юн) отсечка не даёт ничего — 73 % draw calls кадра
	// приходится на проход теней, а теневые кастеры отсекаются по дальности примитива (ShadowSetup.cpp:2293).
	return S->FogFloorMax > 0.f ? FMath::Min(Floor, S->FogFloorMax) : Floor;
}

float UAsdaCullDistanceSubsystem::DistanceFor(EAsdaCullClass Cls, float BoundsRadius) const
{
	const FAsdaCullClassSpec* Spec = EffectiveSpec(Cls);
	if (!Spec || !Spec->bCull) { return 0.f; }   // 0 = никогда не отсекать (поведение порта до правки)
	// ★ 06.11.2026: дальность видимости. Рендер обрывается сразу за полным туманом на любом пресете; движок домножит на r.ViewDistanceScale, поэтому он делится заранее.
	if (Spec->bUseFogFloor)
	{
		const float ViewRange = UAsdaViewRangeSubsystem::RangeUnits(GetWorld());
		if (ViewRange > 0.f)
		{
			static IConsoleVariable* Vds = IConsoleManager::Get().FindConsoleVariable(TEXT("r.ViewDistanceScale"));
			// ★ 07.11.2026: движок меряет расстояние до центра объекта, а купол растворяет каждый пиксель до дальности.
			//   Запас на радиус: ближний край крупного объекта успевает раствориться раньше, чем объект отсечётся.
			const float Margin = UAsdaViewRangeSubsystem::IsSkyFadeOn() ? FMath::Min(BoundsRadius, ViewRange * 0.3f) : 0.f;
			return (ViewRange * 1.03f + Margin) / FMath::Max(Vds ? Vds->GetFloat() : 1.f, 0.05f);
		}
	}
	float D = FMath::Clamp(BoundsRadius * Spec->SizeFactor, Spec->MinDistance, Spec->MaxDistance);
	// ★ Пол видимости: радиус, на котором пиксельный замер не отличает отсечку от шума повтора (15 000 юн, alpen).
	//   Поднимаем (не опускаем) — отсечка не должна быть ближе, чем видит игрок. Обоснование числа и почему это НЕ
	//   «за туманом всё равно не видно» — в UAsdaCullSettings::VisibilityFloorMin.
	if (Spec->bUseFogFloor) { D = FMath::Max(D, VisibilityFloor()); }
	// Ползунок игрока — поверх итога; движок дальше сам домножит на r.ViewDistanceScale уровня качества
	D *= FMath::Max(GAsdaCullScale, 0.01f);
	return D;
}

void UAsdaCullDistanceSubsystem::SetMaxDrawDistanceDirect(UPrimitiveComponent* C, float NewDistance)
{
	if (!C || NewDistance < 0.f) { return; }
	// ★★★ ПОЧЕМУ НЕ SetCullDistance (ревизия review_drawdistance.md §6). Штатный путь при УВЕЛИЧЕНИИ дальности
	//   (ветка PrimitiveComponent.cpp:2473-2480 «OldLDMaxDrawDistance == CachedMaxDrawDistance») зовёт
	//   UWorld::UpdateCullDistanceVolumes, а тот делает TActorIterator<ACullDistanceVolume> ПО ВСЕМУ МИРУ
	//   (World.cpp:3068-3073) — на каждый из 3238 компонентов, каждый кадр перетаскивания ползунка вправо.
	//   Волюмы отсечки в проекте не используются вовсе (grep ACullDistanceVolume по Source/Tools/Content = 0;
	//   карты строит Tools/asda_build_map_level.py, он таких акторов не создаёт), поэтому их пересчёт нам не нужен.
	//   Итог: пишем LDMaxDrawDistance (поле «желаемой» дальности, чтобы откат вернул авторское значение) и обновляем
	//   действующую CachedMaxDrawDistance штатным ENGINE_API-сеттером (PrimitiveComponent.h:3027) — он один и делает
	//   полезную работу: FScene::UpdatePrimitiveDrawDistance для прокси. Дальше движок сам домножает на r.ViewDistanceScale.
	if (C->LDMaxDrawDistance != NewDistance || C->CachedMaxDrawDistance != NewDistance)
	{
		C->LDMaxDrawDistance = NewDistance;
		C->SetCachedMaxDrawDistance(NewDistance);
	}
}

void UAsdaCullDistanceSubsystem::RestoreAuthored(UPrimitiveComponent* C, const FAuthored& A)
{
	if (!C || !IsValid(C)) { return; }
	if (A.bInstanced)
	{
		if (UInstancedStaticMeshComponent* ISM = Cast<UInstancedStaticMeshComponent>(C)) { ISM->SetCullDistances(A.InstStart, A.InstEnd); }
		// ISM мы правим ДВУМЯ полями (инстансы + сам компонент) — возвращаем оба
	}
	// Тем же прямым путём: авторское значение мира = 0 («никогда не отсекать»), и это тоже увеличение дальности
	SetMaxDrawDistanceDirect(C, A.MaxDrawDistance);
}

bool UAsdaCullDistanceSubsystem::ApplyToComponent(UPrimitiveComponent* C, int32& OutAuthored, int32& OutExcluded)
{
	if (IsExcluded(C)) { ++OutExcluded; return false; }
	const EAsdaCullClass Cls = ClassifyPrimitive(C);
	if (Cls == EAsdaCullClass::Count) { ++OutExcluded; return false; }

	UInstancedStaticMeshComponent* ISM = Cast<UInstancedStaticMeshComponent>(C);

	// Авторские значения — при первом касании компонента (до любой нашей правки)
	FAuthored* A = Authored.Find(C);
	if (!A)
	{
		FAuthored New;
		New.bInstanced = (ISM != nullptr);
		if (ISM) { ISM->GetCullDistances(New.InstStart, New.InstEnd); }
		New.MaxDrawDistance = C->LDMaxDrawDistance;   // у ISM тоже: мы правим и дальность самого компонента
		A = &Authored.Add(C, New);
	}

	// Счётчики класса — ДО любых отсевов и до отката: разбивка кадра нужна и при выключенной отсечке.
	// ★ Копит их ТОЛЬКО полный проход (bCountingPass): доработка появившихся акторов (Pending) идёт по актору дважды
	//   и раньше давала двойной счёт классов в дампе (косметика ревизии §5).
	if (bCountingPass)
	{
		const int32 Ci = (int32)Cls;
		++LastByClass[Ci];
		LastSectionsByClass[Ci] += FMath::Max(C->GetNumMaterials(), 1);
		if (ISM) { LastInstancesByClass[Ci] += ISM->GetInstanceCount(); }
	}

	// Откат: вернуть авторское и выйти
	if (GAsdaCullEnable == 0)
	{
		RestoreAuthored(C, *A);
		return true;
	}

	// ★ Авторская разметка главнее рантайма (закон «дефект разметки чинится в ассете»): компонент, которому дальность
	//   задали в ассете, мы не трогаем вовсе.
	const bool bAuthoredHasDistance = A->bInstanced ? (A->InstEnd > 0) : (A->MaxDrawDistance > 0.f);
	if (bAuthoredHasDistance) { ++OutAuthored; return false; }

	// Радиус габаритов. У ISM карты габариты компонента — во всю карту (все инстансы одной модели), поэтому размер
	// берётся у САМОГО МЕША (один инстанс) с учётом масштаба компонента.
	float Radius = 0.f;
	if (ISM)
	{
		if (const UStaticMesh* M = ISM->GetStaticMesh())
		{
			const FVector Sc = ISM->GetComponentScale();
			Radius = M->GetBounds().SphereRadius * FMath::Max3(FMath::Abs(Sc.X), FMath::Abs(Sc.Y), FMath::Abs(Sc.Z));
		}
	}
	else
	{
		Radius = (float)C->Bounds.SphereRadius;
	}

	const float D = DistanceFor(Cls, Radius);
	if (D <= 0.f)
	{
		// Класс не отсекается по правилу (bCull=False → DistanceFor = 0). ★ 12.10.2026: если ЭТОТ компонент мы уже трогали
		//   раньше (правило класса сменили на лету — `asda.Cull.Set Skeletal 0 0 0`), вернуть авторское значение: иначе
		//   у NPC/мобов оставалась бы прежняя дальность до перезагрузки карты. С ini (Skeletal bCull=False с 12.10.2026)
		//   компонент никто не трогал — поля равны авторским, записи нет (проверка здесь, чтобы ISM не получал SetCullDistances зря).
		bool bTouched = (C->LDMaxDrawDistance != A->MaxDrawDistance);
		if (ISM && A->bInstanced) { int32 S0 = 0, E0 = 0; ISM->GetCullDistances(S0, E0); bTouched |= (S0 != A->InstStart || E0 != A->InstEnd); }
		if (bTouched) { RestoreAuthored(C, *A); }
		++OutExcluded;
		return false;
	}

	if (ISM)
	{
		// Пер-инстансная отсечка: Start = 0 (ближней границы нет), End = наша дальность.
		// Движок сам домножит на r.ViewDistanceScale (InstancedStaticMesh.cpp:1317,1376,2046).
		ISM->SetCullDistances(0, FMath::RoundToInt(D));   // дёшево: FScene::UpdateInstanceCullDistance, обхода мира нет
		// ★★★ И ТУТ ЖЕ — ДАЛЬНОСТЬ ВСЕГО КОМПОНЕНТА. Замер 08.09.2026 (drawdistance_report.md §1): 73 % всех draw calls
		//   кадра — проход теней (ShadowDepths 3094 из 4386), а теневых кастеров движок отсеивает по дальности
		//   ПРИМИТИВА, а не инстанса: ShadowSetup.cpp:2293-2296 читает PrimitiveSceneInfoCompact.MaxDrawDistance.
		//   Одной пер-инстансной отсечки мало — тени ISM не отсекались вовсе. Дальность компонента берём с запасом
		//   на его габариты (инстансы одной модели разнесены по карте): у компонента, чьи габариты больше дальности,
		//   отсечка просто не сработает — и это правильно, иначе исчезли бы ближние инстансы.
		SetMaxDrawDistanceDirect(C, D + (float)C->Bounds.SphereRadius);
	}
	else
	{
		// LDMaxDrawDistance + пересчёт CachedMaxDrawDistance; движок множит на r.ViewDistanceScale (SceneVisibility.cpp:493).
		// Прямой путь вместо SetCullDistance — обоснование в SetMaxDrawDistanceDirect (обход мира на каждый компонент).
		SetMaxDrawDistanceDirect(C, D);
	}
	return true;
}

void UAsdaCullDistanceSubsystem::ApplyToActor(AActor* A, int32& OutTouched, int32& OutAuthored, int32& OutExcluded)
{
	if (!A || !IsValid(A)) { return; }
	TInlineComponentArray<UPrimitiveComponent*> Comps;
	A->GetComponents<UPrimitiveComponent>(Comps);
	for (UPrimitiveComponent* C : Comps)
	{
		if (ApplyToComponent(C, OutAuthored, OutExcluded)) { ++OutTouched; }
	}
}

void UAsdaCullDistanceSubsystem::Apply(bool bLog, bool bForce)
{
	UWorld* W = GetWorld();
	if (!W) { return; }

	// Один раз за процесс — сверка «что реально прочиталось из Config/DefaultGame.ini» (ревизия §3)
	UAsdaCullSettings::ValidateConfig();

	// ★ Отпечаток настроек: если с прошлого прохода не изменилось НИЧЕГО, ставить нечего — второй Apply на загрузке
	//   (кадры 0 и 1: OnWorldBeginPlay и накат персиста [AsdaRender] из dev-меню) писал в мир те же числа и вторую
	//   строку в лог (косметика ревизии §5). Новые акторы сюда не относятся — их ведёт Pending.
	uint32 Sig = ::GetTypeHash((uint32)GAsdaCullEnable);
	Sig = HashCombine(Sig, ::GetTypeHash(GAsdaCullScale));
	Sig = HashCombine(Sig, ::GetTypeHash(VisibilityFloor()));
	for (int32 i = 0; i < (int32)EAsdaCullClass::Count; ++i)
	{
		const FAsdaCullClassSpec* Sp = EffectiveSpec((EAsdaCullClass)i);
		if (!Sp) { Sig = HashCombine(Sig, 0u); continue; }
		Sig = HashCombine(Sig, (uint32)(Sp->bCull ? 1 : 0));
		Sig = HashCombine(Sig, ::GetTypeHash(Sp->SizeFactor));
		Sig = HashCombine(Sig, ::GetTypeHash(Sp->MinDistance));
		Sig = HashCombine(Sig, ::GetTypeHash(Sp->MaxDistance));
		Sig = HashCombine(Sig, (uint32)(Sp->bUseFogFloor ? 1 : 0));
	}
	if (!bForce && LastApplyTimeSec >= 0.0 && Sig == LastApplySignature)
	{
		LastApplyTimeSec = FPlatformTime::Seconds();   // проход состоялся бы сейчас — throttle считает от этого момента
		UE_CLOG(bLog, LogAsdaCull, Verbose, TEXT("[Отсечка] настройки не менялись с прошлого прохода — полный обход мира пропущен [мир %s]"), *W->GetName());
		return;
	}

	// Кэш авторских значений: выбросить умершие компоненты
	for (auto It = Authored.CreateIterator(); It; ++It) { if (!It->Key.IsValid()) { It.RemoveCurrent(); } }

	LastTouched = 0; LastSkippedAuthored = 0; LastSkippedExcluded = 0;
	for (int32 i = 0; i < (int32)EAsdaCullClass::Count; ++i) { LastByClass[i] = 0; LastSectionsByClass[i] = 0; LastInstancesByClass[i] = 0; }

	// Единственный полный обход мира — при загрузке карты и при смене ключа/уровня. Дальше только точечная доработка
	// появившихся акторов (Pending), как у бюджета теней (памятка о спайке GT на полном TActorIterator).
	const double T0 = FPlatformTime::Seconds();
	bCountingPass = true;
	for (TActorIterator<AActor> It(W); It; ++It)
	{
		ApplyToActor(*It, LastTouched, LastSkippedAuthored, LastSkippedExcluded);
	}
	bCountingPass = false;
	const double Msec = (FPlatformTime::Seconds() - T0) * 1000.0;

	LastApplyTimeSec = FPlatformTime::Seconds();
	LastApplySignature = Sig;
	LastAppliedScale = GAsdaCullScale;
	++GAsdaCullApplyPasses;

	if (bLog)
	{
		const float Floor = VisibilityFloor();
		UE_LOG(LogAsdaCull, Log, TEXT("[Отсечка] asda.Cull.Enable=%d Scale=%.2f, пол видимости %.0f юн, r.ViewDistanceScale=%.2f (уровень sg.ViewDistanceQuality) — "
			"проставлено %d, пропущено: авторская разметка %d, исключения %d; проход %.2f мс, всего проходов %d [мир %s]"),
			GAsdaCullEnable, GAsdaCullScale, Floor,
			IConsoleManager::Get().FindConsoleVariable(TEXT("r.ViewDistanceScale")) ? IConsoleManager::Get().FindConsoleVariable(TEXT("r.ViewDistanceScale"))->GetFloat() : 1.f,
			LastTouched, LastSkippedAuthored, LastSkippedExcluded, Msec, GAsdaCullApplyPasses, *W->GetName());
	}
}

int32 UAsdaCullDistanceSubsystem::GetApplyPassCount()
{
	return GAsdaCullApplyPasses;
}

void UAsdaCullDistanceSubsystem::Dump()
{
	// Свежий срез (класс и дальность могли поменяться ключом) — и заодно пересчёт счётчиков по классам.
	// bForce: дампу проход нужен всегда, даже если настройки те же (он и живёт ради свежих счётчиков).
	Apply(/*bLog*/false, /*bForce*/true);

	const UAsdaCullSettings* S = UAsdaCullSettings::Get();
	const float Floor = VisibilityFloor();
	IConsoleVariable* VD = IConsoleManager::Get().FindConsoleVariable(TEXT("r.ViewDistanceScale"));
	const float VDScale = VD ? VD->GetFloat() : 1.f;

	UE_LOG(LogAsdaCull, Log, TEXT("──── asda.Cull.Dump [мир %s] ────"), GetWorld() ? *GetWorld()->GetName() : TEXT("?"));
	UE_LOG(LogAsdaCull, Log, TEXT("Enable=%d Scale=%.2f | пол видимости %.0f юн (минимум %.0f%s, начало тумана ×%.2f%s, потолок %.0f) | r.ViewDistanceScale=%.2f | проходов %d"),
		GAsdaCullEnable, GAsdaCullScale, Floor,
		GAsdaCullVisFloor >= 0.f ? GAsdaCullVisFloor : S->VisibilityFloorMin,
		GAsdaCullVisFloor >= 0.f ? TEXT(" ключом") : TEXT(" из конфига"),
		GAsdaCullFogFloor >= 0.f ? GAsdaCullFogFloor : S->FogSafetyFactor,
		GAsdaCullFogFloor >= 0.f ? TEXT(" ключом") : TEXT(" из конфига"), S->FogFloorMax, VDScale, GAsdaCullApplyPasses);

	// ★ ДИАГНОСТИКА ТУМАНА (ревизия review_drawdistance.md §1: обоснование «за туманом объект и так не виден» было
	//   НЕВЕРНЫМ и никем не проверялось). Печатаем честные числа этой карты: где туман начинается, какую непрозрачность
	//   он даёт на самом полу видимости и на каком расстоянии он действительно прячет объект.
	{
		float FStart = 0.f, FDens = 0.f, FMaxOp = 0.f;
		if (GetWorldFog(FStart, FDens, FMaxOp))
		{
			const float Target = FMath::Clamp(S->FogDiagOpacity, 0.f, 0.99f);
			const float LHide = FogDistanceForOpacity(Target);
			const float OpAtFloor = FogOpacityAt(Floor) * 100.f;
			const float OpAtEff = FogOpacityAt(Floor * VDScale * FMath::Max(GAsdaCullScale, 0.01f)) * 100.f;
			if (LHide > 0.f)
			{
				UE_LOG(LogAsdaCull, Log, TEXT("Туман карты: начало %.0f юн, плотность %.4f, потолок непрозрачности %.2f | "
					"на полу видимости %.0f юн непрозрачность %.0f %%, на эффективном радиусе %.0f юн — %.0f %% | %.0f %% туман набирает лишь на %.0f юн. "
					"★ Пол видимости выбран ПИКСЕЛЬНЫМ ЗАМЕРОМ, не туманом."),
					FStart, FDens, FMaxOp, Floor, OpAtFloor, Floor * VDScale * FMath::Max(GAsdaCullScale, 0.01f), OpAtEff, Target * 100.f, LHide);
			}
			else
			{
				UE_LOG(LogAsdaCull, Log, TEXT("Туман карты: начало %.0f юн, плотность %.4f, потолок непрозрачности %.2f | на полу видимости %.0f юн непрозрачность %.0f %% | "
					"непрозрачности %.0f %% этот туман НЕ ДОСТИГАЕТ НИКОГДА (потолок компонента ниже цели). ★ Пол видимости выбран пиксельным замером, не туманом."),
					FStart, FDens, FMaxOp, Floor, OpAtFloor, Target * 100.f);
			}
		}
		else
		{
			UE_LOG(LogAsdaCull, Log, TEXT("Тумана в мире нет — пол видимости целиком из VisibilityFloorMin (%.0f юн, пиксельный замер)"), Floor);
		}
	}
	UE_LOG(LogAsdaCull, Log, TEXT("%-10s %-6s %-7s %-9s %-9s %-6s %-11s %-9s %-10s %s"),
		TEXT("класс"), TEXT("отсек"), TEXT("×разм"), TEXT("мин,юн"), TEXT("макс,юн"), TEXT("пол"),
		TEXT("примитивов"), TEXT("секций"), TEXT("инстансов"), TEXT("итог с уровнем, юн"));
	for (int32 i = 0; i < (int32)EAsdaCullClass::Count; ++i)
	{
		const EAsdaCullClass C = (EAsdaCullClass)i;
		const FAsdaCullClassSpec* Sp = EffectiveSpec(C);
		if (!Sp)
		{
			UE_LOG(LogAsdaCull, Log, TEXT("%-10s %-6s (правила нет — не отсекается), примитивов %d, секций %d"), CullClassName(C), TEXT("нет"), LastByClass[i], LastSectionsByClass[i]);
			continue;
		}
		// Итог показываем для мелкого (зажим снизу) и крупного (зажим сверху) объекта класса — вилка дальностей
		const float Lo = DistanceFor(C, 0.f) * VDScale;
		const float Hi = DistanceFor(C, 1.0e6f) * VDScale;
		UE_LOG(LogAsdaCull, Log, TEXT("%-10s %-6d %-7.1f %-9.0f %-9.0f %-6d %-11d %-9d %-10d %.0f..%.0f"),
			CullClassName(C), Sp->bCull ? 1 : 0, Sp->SizeFactor, Sp->MinDistance, Sp->MaxDistance,
			Sp->bUseFogFloor ? 1 : 0, LastByClass[i], LastSectionsByClass[i], LastInstancesByClass[i], Lo, Hi);
	}
	UE_LOG(LogAsdaCull, Log, TEXT("Проставлено %d, пропущено: авторская разметка ассета %d, исключения (небо/террейн/игрок/тег/чужой тип) %d"),
		LastTouched, LastSkippedAuthored, LastSkippedExcluded);
}

void UAsdaCullDistanceSubsystem::ArmDumpAll(float Seconds)
{
	if (Seconds <= 0.f) { return; }
	ForEachInstance<UAsdaCullDistanceSubsystem>([Seconds](UAsdaCullDistanceSubsystem& S) { S.DumpTimer = Seconds; });   // ★ волна 3 п.12 (15.09.2026)
}

void UAsdaCullDistanceSubsystem::MarkAllDirty()
{
	ForEachInstance<UAsdaCullDistanceSubsystem>([](UAsdaCullDistanceSubsystem& S) { S.MarkDirty(); });   // ★ волна 3 п.12 (15.09.2026)
}
