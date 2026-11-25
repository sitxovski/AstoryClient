// Copyright (c) 2026 @sitxovski (Discord)
#include "AsdaCloudFieldActor.h"
#include "Components/StaticMeshComponent.h"
#include "Engine/StaticMesh.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "HAL/IConsoleManager.h"     // FAutoConsoleVariableRef asda.Clouds.Field
#include "UObject/UObjectIterator.h"
#include "Engine/World.h"            // IsGameWorld — тумблер только для игровых миров (в редакторе облака видны всегда)

// ★ Тумблер 3D-облаков (05.09.2026, dev-меню Ctrl+N → «Небо и туман»): 1 = поле как в umap, 0 = актор скрыт в игре
//   И теневые прокси не отбрасывают тень (ревизия В-1: у прокси CastHiddenShadow — скрытие актора тень не снимало);
//   возврат 1 = авторское скрытие, не безусловное «показать» (ревизия В-2). В редакторе облака видны всегда.
//   Смена на лету перекладывает состояние на все поля игровых миров (ApplyFieldToggle).
//   Регистрация единственная в Source/ (grep "asda.Clouds.Field" = 0 до правки; закон ue-console-duplicate-registration).
static int32 GAsdaCloudsField = 1;   // текущее значение ключа asda.Clouds.Field
static void OnAsdaCloudsFieldChanged(IConsoleVariable*)
{
	int32 Count = 0;   // сколько акторов переключено (в лог)
	for (TObjectIterator<AAsdaCloudFieldActor> It; It; ++It)
	{
		if (!*It || It->IsTemplate() || !It->GetWorld() || !It->GetWorld()->IsGameWorld()) { continue; }
		It->ApplyFieldToggle(GAsdaCloudsField != 0);
		++Count;
	}
	UE_LOG(LogTemp, Log, TEXT("[AsdaCloudField] asda.Clouds.Field=%d — переключено полей: %d"), GAsdaCloudsField, Count);
}
static FAutoConsoleVariableRef CVarAsdaCloudsField(
	TEXT("asda.Clouds.Field"),
	GAsdaCloudsField,
	TEXT("1 = поле 3D-облаков (AAsdaCloudFieldActor) как в карте, 0 = скрыто в игре вместе с тенями прокси. Смена на лету переключает поля карты."),
	FConsoleVariableDelegate::CreateStatic(&OnAsdaCloudsFieldChanged),
	ECVF_Default);

// ★ [ИСПРАВЛЕНИЕ] 07.11.2026 (пачка 14): выключенные тени оставляли тени 3D-облаков. На уровне 0 (флажок « - Тени», пресет «Низкое»)
//   солнечные тени живы (r.ShadowQuality=2, матрица уровней), а тень облаков гасла только светофункцией (r.LightFunctionQuality 0):
//   теневые прокси поля отбрасывали жёсткую тень каскадами. Теперь прокси подчиняются тому же ключу уровня, смена на ходу — в Tick.
//   0 = как было (всегда отбрасывают), 1 = вместе с настройкой теней, 2 = никогда.
static int32 GAsdaCloudsFieldShadows = 1;   // текущее значение ключа asda.Clouds.FieldShadows
static FAutoConsoleVariableRef CVarAsdaCloudsFieldShadows(
	TEXT("asda.Clouds.FieldShadows"),
	GAsdaCloudsFieldShadows,
	TEXT("Тень поля 3D-облаков: 0 = всегда (как было), 1 = вместе с настройкой теней (уровень 0 снимает), 2 = никогда."),
	ECVF_Default);

// Разрешена ли тень поля сейчас: ключ и уровень теней (r.LightFunctionQuality раздаёт [ShadowQuality@N], 0 только на уровне 0).
static bool AsdaCloudFieldShadowAllowed()
{
	if (GAsdaCloudsFieldShadows == 0) { return true; }
	if (GAsdaCloudsFieldShadows >= 2) { return false; }
	static const TConsoleVariableData<int32>* Lf = IConsoleManager::Get().FindTConsoleVariableDataInt(TEXT("r.LightFunctionQuality"));
	return !Lf || Lf->GetValueOnGameThread() > 0;
}

// ★ ВОЛНА CPU 07.09.2026 (cpu_visibility_report.md): шаг ветра раз в N кадров.
//   38 якорей поля тащат за собой ТЕНЕВЫЕ ПРОКСИ (CastHiddenShadow) — каждый их сдвиг помечает
//   render-transform примитива грязным и инвалидирует страницы VSM/CSM на потоке рендера
//   (cpu_gamethread_plan.md §2 п.4: [ОЦЕНКА RT 0.3–1 мс]). Шаг раз в N кадров даёт то же движение
//   накопленным Dt: позиция на кадрах-шагах СОВПАДАЕТ с покадровым интегрированием (шаг = Dir × Speed × ΣDt),
//   а между ними облако стоит; сдвигов прокси (и инвалидаций VSM) в N раз меньше.
//   ★ ПРАВКА по ревизии review_cpu_visibility.md В-1 (07.09.2026): прежнее обоснование «сдвиг субпиксельный»
//     НЕВЕРНО. При WindSpeed=800 юн/с и 60 fps шаг за 4 кадра = 53 юн; тень облака на земле — тоже 53 юн
//     (направленный свет: тень едет ровно на смещение облака). Угловой размер шага у КРАЯ тени близко к
//     игроку (дистанция ~5000 юн): ~13 px @1080p/FOV90 15 раз/с — на РЕЗКОМ крае это читалось бы «ступеньками».
//     Реальная защита — мягкость/низкий контраст тени от полупрозрачного блоба, а не «субпиксельность».
//   ★ Интерполяция позиции прокси между обновлениями НЕ вводится намеренно: сгладить = двигать прокси
//     КАЖДЫЙ кадр = ровно цена шага 1 (та же инвалидация VSM), т.е. вся экономия волны теряется —
//     «дешёвой» интерполяции здесь не существует. Поэтому выбор — только между дискретными N.
//   ★ ДЕФОЛТ снижен 4 → 2 (review_cpu_visibility.md В-1): при N=2 шаг = 26 юн ≈ 6.7 px @30 Гц — ниже порога
//     заметности «ступенек» на мягком крае; экономии остаётся ~2/3 (сдвигов 30/с вместо 60/с базовых
//     против 15/с при N=4). N=4 остаётся выбором в Ctrl+N для слабых GPU. 1 = каждый кадр (как раньше).
//   Регистрация имени единственная в Source/.
static int32 GAsdaCloudsUpdateEvery = 2;   // текущее значение ключа asda.Clouds.UpdateEvery (умолчание в коде 2, см. выше; в проекте задано 1 в Config/DefaultEngine.ini [SystemSettings] — подбор владельца 09.09.2026)
static FAutoConsoleVariableRef CVarAsdaCloudsUpdateEvery(
	TEXT("asda.Clouds.UpdateEvery"),
	GAsdaCloudsUpdateEvery,
	TEXT("Каждые N кадров двигать якоря поля 3D-облаков и их теневые прокси (1 = каждый кадр, 2 = через кадр). Умолчание в коде 2; в проекте задано 1 в Config/DefaultEngine.ini [SystemSettings] (подбор владельца 09.09.2026)."),
	ECVF_Default);

// ★ Волна CPU, мелочь М-2 ревизии (07.09.2026): состояние общего рубильника «Оптимизация видимости» (asda.Vis.Enable)
//   применительно к облакам. true = штатно (эффективный шаг = asda.Clouds.UpdateEvery); false = рубильник выключен →
//   эффективный шаг ПРИНУДИТЕЛЬНО 1 (облака двигаются покадрово, как до волны). База asda.Clouds.UpdateEvery при этом
//   НЕ меняется — это тонкая настройка, к которой возвращаемся при включении рубильника. Пишется колбэком asda.Vis.Enable.
static bool GAsdaCloudsVisMasterOn = true;

void AAsdaCloudFieldActor::SetVisMasterEnabled(bool bEnabled)
{
	if (GAsdaCloudsVisMasterOn == bEnabled) { return; }   // без изменений — не шумим в лог
	GAsdaCloudsVisMasterOn = bEnabled;
	const int32 Base = FMath::Clamp(GAsdaCloudsUpdateEvery, 1, 32);
	const int32 Eff = bEnabled ? Base : 1;                 // выключен рубильник → покадрово (шаг 1)
	UE_LOG(LogTemp, Log, TEXT("[AsdaCloudField] мастер-рубильник видимости=%d → эффективный шаг облаков=%d (база asda.Clouds.UpdateEvery=%d)"),
		bEnabled ? 1 : 0, Eff, Base);
}

AAsdaCloudFieldActor::AAsdaCloudFieldActor()
{
	PrimaryActorTick.bCanEverTick = true;
	RootComponent = CreateDefaultSubobject<USceneComponent>(TEXT("Root"));
}

void AAsdaCloudFieldActor::OnConstruction(const FTransform&)
{
	Rebuild();
}

void AAsdaCloudFieldActor::BeginPlay()
{
	Super::BeginPlay();
	Rebuild();
	// Сохранённый выбор игрока (asda.Clouds.Field из GameUserSettings.ini [AsdaRender]) — после сборки компонентов;
	// первое применение запоминает авторское IsHidden() актора (к нему вернёт ключ 1).
	ApplyFieldToggle(GAsdaCloudsField != 0);
}

void AAsdaCloudFieldActor::ApplyFieldToggle(bool bFieldOn)
{
	// Авторское скрытие — один раз, ДО первого изменения (после него IsHidden() уже наше).
	if (!bAuthoredCached) { bAuthoredHidden = IsHidden(); bAuthoredCached = true; }
	SetActorHiddenInGame(bFieldOn ? bAuthoredHidden : true);
	// Теневые прокси: при 0 — не отбрасывают (CastHiddenShadow иначе даёт тень от скрытого); при 1 — как задано bCastShadows.
	// Флаги пересоздают render state прокси (SetCastShadow/SetCastHiddenShadow → MarkRenderStateDirty) — проверено кадрами
	// alpen с остановленным ветром (devmenu_fix_report.md §В-1): тень уходит при 0 и возвращается при 1.
	bShadowsAllowedLast = AsdaCloudFieldShadowAllowed();
	const bool bShadows = bFieldOn && bCastShadows && bShadowsAllowedLast;
	int32 Proxies = 0;   // сколько прокси переключено (в лог)
	for (UStaticMeshComponent* Sh : ShadowProxies)
	{
		if (!Sh) { continue; }
		Sh->SetCastShadow(bShadows);
		Sh->SetCastHiddenShadow(bShadows);
		++Proxies;
	}
	UE_LOG(LogTemp, Log, TEXT("[AsdaCloudField] %s: ключ=%d → hidden=%d (авторское %d), теневых прокси %d → тень %s"),
		*GetName(), bFieldOn ? 1 : 0, IsHidden() ? 1 : 0, bAuthoredHidden ? 1 : 0, Proxies, bShadows ? TEXT("вкл") : TEXT("выкл"));
}

int32 AAsdaCloudFieldActor::CastingShadowProxies() const
{
	int32 Casting = 0;   // прокси, которые сейчас отбрасывают тень
	for (const UStaticMeshComponent* Sh : ShadowProxies)
	{
		if (Sh && Sh->CastShadow && Sh->bCastHiddenShadow) { ++Casting; }
	}
	return Casting;
}

FString AAsdaCloudFieldActor::DescribeFieldState() const
{
	int32 Casting = 0;   // прокси с флагами тени (CastShadow && bCastHiddenShadow)
	int32 InScene = 0;   // прокси, у которых есть живой FPrimitiveSceneProxy (реально в сцене рендера)
	for (const UStaticMeshComponent* Sh : ShadowProxies)
	{
		if (!Sh) { continue; }
		if (Sh->CastShadow && Sh->bCastHiddenShadow) { ++Casting; }
		if (Sh->GetSceneProxy() != nullptr) { ++InScene; }
	}
	return FString::Printf(TEXT("%s [мир %s]: hidden=%d авторское=%d(снято=%d) прокси=%d флаги тени=%d в сцене=%d bCastShadows=%d"),
		*GetName(), GetWorld() ? *GetWorld()->GetName() : TEXT("-"), IsHidden() ? 1 : 0, bAuthoredHidden ? 1 : 0, bAuthoredCached ? 1 : 0,
		ShadowProxies.Num(), Casting, InScene, bCastShadows ? 1 : 0);
}

#if WITH_EDITOR
void AAsdaCloudFieldActor::PostEditChangeProperty(FPropertyChangedEvent& E)
{
	Super::PostEditChangeProperty(E);
	Rebuild();
}
#endif

void AAsdaCloudFieldActor::Rebuild()
{
	// зачистка ПО ДЕТЯМ КОРНЯ (после PIE-дубликации Transient-массив пуст, а сериализованные компоненты есть)
	if (RootComponent)
	{
		TArray<USceneComponent*> Kids;
		RootComponent->GetChildrenComponents(true, Kids);
		for (USceneComponent* K : Kids)
		{
			if (K) { K->DestroyComponent(); }
		}
	}
	CloudAnchors.Reset();
	AnchorSpeedFactor.Reset();
	CloudMIDs.Reset();
	ShadowProxies.Reset();
	UStaticMesh* Sphere = LoadObject<UStaticMesh>(nullptr, TEXT("/Engine/BasicShapes/Sphere.Sphere"));
	if (!Sphere || !Material) { return; }
	FRandomStream Rnd(Seed);
	// стратифицированная сетка по всей площади + блоб из 5-8 движковых сфер на облако
	const int32 GridN = FMath::CeilToInt(FMath::Sqrt((float)Count));
	TArray<int32> Cells;
	for (int32 ci = 0; ci < GridN * GridN; ++ci) { Cells.Add(ci); }
	for (int32 ci = Cells.Num() - 1; ci > 0; --ci) { Cells.Swap(ci, Rnd.RandRange(0, ci)); }
	const FVector2D CellSize(2.f * AreaExtent.X / GridN, 2.f * AreaExtent.Y / GridN);
	int32 Made = 0;
	for (int32 k = 0; k < Cells.Num() && Made < Count; ++k)
	{
		const int32 gx = Cells[k] % GridN, gy = Cells[k] / GridN;
		const float S = Rnd.FRandRange(ScaleMin, ScaleMax);
		const FVector2D P(-AreaExtent.X + (gx + 0.5f + Rnd.FRandRange(-0.38f, 0.38f)) * CellSize.X,
		                  -AreaExtent.Y + (gy + 0.5f + Rnd.FRandRange(-0.38f, 0.38f)) * CellSize.Y);
		const float SizeT = (ScaleMax > ScaleMin) ? (S - ScaleMin) / (ScaleMax - ScaleMin) : 0.5f;
		const float Z = FMath::Lerp(ZMax, ZMin, SizeT) + Rnd.FRandRange(-0.12f, 0.12f) * (ZMax - ZMin);
		USceneComponent* Anchor = NewObject<USceneComponent>(this);
		Anchor->SetupAttachment(RootComponent);
		Anchor->SetMobility(EComponentMobility::Movable);
		Anchor->SetRelativeLocation(FVector(P.X, P.Y, Z));
		Anchor->SetRelativeRotation(FRotator(0, Rnd.FRandRange(0.f, 360.f), 0));
		Anchor->RegisterComponent();
		CloudAnchors.Add(Anchor);
		// ветер по высоте: низ = полная скорость, верх = ×HighAltSlowdown
		const float HeightT = (ZMax > ZMin) ? FMath::Clamp((Z - ZMin) / (ZMax - ZMin), 0.f, 1.f) : 0.f;
		AnchorSpeedFactor.Add(FMath::Lerp(1.f, HighAltSlowdown, HeightT));
		// плавный рандом прозрачности; доля SolidFraction — ПОЛНОСТЬЮ плотные
		float Op = FMath::Lerp(OpacityMin, OpacityMax, Rnd.FRand());
		if (Rnd.FRand() < SolidFraction) { Op = 1.f; }
		UMaterialInstanceDynamic* MID = UMaterialInstanceDynamic::Create(Material, this);
		MID->SetScalarParameterValue(TEXT("Opacity"), FMath::Clamp(Op, 0.15f, 1.f));
		CloudMIDs.Add(MID);
		if (CloudMeshes.Num() > 0)
		{
			// ЦЕЛЬНЫЙ меш (метабол-блоб): нет внутренних стенок — чистый транслюцент
			UStaticMesh* BM = CloudMeshes[Rnd.RandRange(0, CloudMeshes.Num() - 1)];
			if (BM)
			{
				const FVector BScale(S * Rnd.FRandRange(0.9f, 1.1f), S * Rnd.FRandRange(0.85f, 1.f), S * Rnd.FRandRange(0.8f, 1.f));
				UStaticMeshComponent* C = NewObject<UStaticMeshComponent>(this);
				C->SetupAttachment(Anchor);
				C->SetStaticMesh(BM);
				C->SetMaterial(0, MID);
				C->SetMobility(EComponentMobility::Movable);
				C->SetCastShadow(false);
				C->bAffectDistanceFieldLighting = false;   // ★ Сет E-1: облака — не в DF-сцене (DFAO/DF-тени/глобальный SDF)
				C->SetCollisionEnabled(ECollisionEnabled::NoCollision);
				C->SetRelativeScale3D(BScale);
				C->RegisterComponent();
				if (bCastShadows)
				{
					UStaticMeshComponent* Sh = NewObject<UStaticMeshComponent>(this);
					Sh->SetupAttachment(Anchor);
					Sh->SetStaticMesh(BM);
					Sh->SetMobility(EComponentMobility::Movable);
					Sh->SetCollisionEnabled(ECollisionEnabled::NoCollision);
					Sh->SetVisibility(false);
					Sh->SetHiddenInGame(true);
					Sh->SetCastShadow(true);
					Sh->SetCastHiddenShadow(true);
					// ★ Сет E-1 (06.09.2026): теневой прокси с CastShadow=true при Mesh Distance Fields попал бы в DF-сцену
					//   (PrimitiveComponent.h:562-564: bAffectDistanceFieldLighting действует при CastShadow) и дал бы
					//   DF-тень солнца поверх LightFunction-теней облаков + вошёл бы в глобальный SDF (план сета E §3.4).
					Sh->bAffectDistanceFieldLighting = false;
					Sh->SetRelativeScale3D(BScale);
					Sh->RegisterComponent();
					ShadowProxies.Add(Sh);
				}
			}
			++Made;
			continue;
		}
		const int32 Lobes = Rnd.RandRange(FMath::Min(LobesMin, LobesMax), FMath::Max(LobesMin, LobesMax));
		const float Len = 12000.f * S, BaseR = 3300.f * S;
		for (int32 li = 0; li < Lobes; ++li)
		{
			const float T = (Lobes > 1) ? (float)li / (float)(Lobes - 1) : 0.5f;
			const float R = BaseR * (0.55f + 0.45f * FMath::Sin(PI * T)) * Rnd.FRandRange(1.f - SizeJitter, 1.f + SizeJitter);
			const FVector LobePos((T - 0.5f) * Len, Rnd.FRandRange(-0.12f, 0.12f) * Len, R * 0.15f);
			const FVector LobeScale(R / 50.f, R / 50.f * Rnd.FRandRange(0.8f, 0.95f), R / 50.f * Rnd.FRandRange(0.55f, 0.7f));
			UStaticMeshComponent* C = NewObject<UStaticMeshComponent>(this);
			C->SetupAttachment(Anchor);
			C->SetStaticMesh(Sphere);
			C->SetMaterial(0, MID);
			C->SetMobility(EComponentMobility::Movable);
			C->SetCastShadow(false);
			C->bAffectDistanceFieldLighting = false;   // ★ Сет E-1: облака — не в DF-сцене
			C->SetCollisionEnabled(ECollisionEnabled::NoCollision);
			C->SetRelativeLocation(LobePos);
			C->SetRelativeScale3D(LobeScale);
			C->RegisterComponent();
			if (bCastShadows)
			{
				// теневой прокси: невидимая сфера, отбрасывающая тень
				UStaticMeshComponent* Sh = NewObject<UStaticMeshComponent>(this);
				Sh->SetupAttachment(Anchor);
				Sh->SetStaticMesh(Sphere);
				Sh->SetMobility(EComponentMobility::Movable);
				Sh->SetCollisionEnabled(ECollisionEnabled::NoCollision);
				Sh->SetVisibility(false);
				Sh->SetHiddenInGame(true);
				Sh->SetCastShadow(true);
				Sh->SetCastHiddenShadow(true);
				Sh->bAffectDistanceFieldLighting = false;   // ★ Сет E-1: см. выше (прокси-коробка)
				Sh->SetRelativeLocation(LobePos);
				Sh->SetRelativeScale3D(LobeScale);
				Sh->RegisterComponent();
				ShadowProxies.Add(Sh);
			}
		}
		++Made;
	}
}

void AAsdaCloudFieldActor::Tick(float Dt)
{
	Super::Tick(Dt);
	// Настройка теней сменилась на ходу: прокси переключаются с полем вместе, ключ поля не трогается.
	if (const UWorld* TickWorld = GetWorld(); TickWorld && TickWorld->IsGameWorld() && AsdaCloudFieldShadowAllowed() != bShadowsAllowedLast)
	{
		ApplyFieldToggle(GAsdaCloudsField != 0);
	}
	if (WindSpeed <= 0.f || CloudAnchors.Num() == 0) { return; }
	// ★ Шаг раз в N кадров с НАКОПЛЕННЫМ Dt (см. asda.Clouds.UpdateEvery выше): пройденный путь тот же,
	//   число сдвигов теневых прокси (и инвалидаций VSM) — в N раз меньше. В редакторе N игнорируем.
	{
		// ★ М-2: общий рубильник asda.Vis.Enable=0 форсит шаг 1 (покадрово), не трогая базу asda.Clouds.UpdateEvery.
		const int32 N = GAsdaCloudsVisMasterOn ? FMath::Clamp(GAsdaCloudsUpdateEvery, 1, 32) : 1;
		const UWorld* W = GetWorld();
		if (N > 1 && W && W->IsGameWorld())
		{
			WindPendingDt += Dt;
			if (++WindFrameCounter < N) { return; }
			WindFrameCounter = 0;
			Dt = WindPendingDt;
			WindPendingDt = 0.f;
		}
	}
	const float Rad = FMath::DegreesToRadians(WindYaw);
	const FVector Dir = FVector(FMath::Cos(Rad), FMath::Sin(Rad), 0);
	for (int32 ai = 0; ai < CloudAnchors.Num(); ++ai)
	{
		USceneComponent* A = CloudAnchors[ai];
		if (!A) { continue; }
		const float F = AnchorSpeedFactor.IsValidIndex(ai) ? AnchorSpeedFactor[ai] : 1.f;
		FVector L = A->GetRelativeLocation() + Dir * WindSpeed * F * Dt;
		const float MX = AreaExtent.X * 1.15f, MY = AreaExtent.Y * 1.15f;
		if (L.X >  MX) L.X = -MX;  if (L.X < -MX) L.X = MX;
		if (L.Y >  MY) L.Y = -MY;  if (L.Y < -MY) L.Y = MY;
		A->SetRelativeLocation(L);
	}
}
