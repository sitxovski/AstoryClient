// Copyright (c) 2026 @sitxovski (Discord)
// AsdaCapsuleShadow.cpp — реализация единой точки «капсульная тень тела» (см. заголовок).
#include "World/AsdaCapsuleShadow.h"
#include "AsdaDevConsole.h"   // ★ античит А2: ASDA_DEV_CVAR / ASDA_DEV_CONSOLE — дев-ключи и команды вне Shipping
#include "Components/SkeletalMeshComponent.h"
#include "HAL/IConsoleManager.h"
#include "UObject/UObjectIterator.h"
#include "Engine/SkeletalMesh.h"
#include "Rendering/SkeletalMeshRenderData.h"
#include "Rendering/SkeletalMeshLODRenderData.h"
#include "PhysicsEngine/PhysicsAsset.h"
#include "PhysicsEngine/BodySetup.h"
#include "PhysicsEngine/SkeletalBodySetup.h"
#include "GameFramework/Actor.h"
#include "Engine/World.h"
#include "PrimitiveSceneProxy.h"
#include "Components/SkyLightComponent.h"
#include "Math/SHMath.h"
#include "GameFramework/WorldSettings.h"   // ★ 26.10.2026: DynamicIndirectShadowsSelfShadowingIntensity (исправление самозатенения)
#include "SceneInterface.h"                // ★ 26.10.2026: FSceneInterface::UpdateSceneSettings
#include "Engine/Engine.h"                 // ★ 26.10.2026: GEngine->GetWorldContexts() при смене ключа

// Метка компонента тела, к которому уже применена политика: по ней смена ключа перекладывает флаг на живых.
static const FName GAsdaCapsuleShadowTag(TEXT("AsdaCapsuleShadow"));

// Значение ключа asda.Shadow.Capsule: 1 = непрямая капсульная тень у игрока/мобов/NPC включена, 0 = выключена.
static int32 GAsdaShadowCapsule = 1;

// Нижняя граница видимости непрямой капсульной тени (дефолт движка 0.1; SkinnedMeshComponent.cpp:491).
//   ★ 05.09.2026, применение сета B: на кадре alpen-5 (моб в тени кроны) пятно при 0.1 вышло почти ЧЁРНЫМ с резким краем —
//   капсула гасит AO/небо до 10 %, а в тени солнца небо там и есть весь свет. Значение вынесено в ключ
//   asda.Shadow.CapsuleMinVisibility (0..1), чтобы владелец подобрал силу без пересборки (A/B кадр _mv05 в отчёте сета B).
static float GAsdaCapsuleMinVisibility = 0.1f;

// ═══ ★ 26.10.2026 [ИСПРАВЛЕНИЕ] баг игроков «Ultra: персонаж весь в чёрной тени» (Reports/ultra_shadows/) ═══════════
//   ПРИЧИНА (доказана A/B-кадрами, shots/ отчёта): непрямая капсульная тень тела в НЕ-Lumen пути неба (DFAO bent normal +
//   ScreenSpaceAO, CapsuleShadowRendering.cpp RenderCapsuleShadowsForMovableSkylight / RenderIndirectCapsuleShadows) затеняет
//   САМОГО персонажа: его собственные капсулы накрывают поверхность тела и одежды. Движок ослабляет это лишь до
//   AWorldSettings::DynamicIndirectShadowsSelfShadowingIntensity (умолчание 0.8, WorldSettings.cpp:144; шейдер
//   CapsuleShadowShaders.usf ApplySelfShadowingIntensityForDeferred) — при MinVisibility 0.1 тело получает ~0.1–0.3 неба,
//   а в тени/контровом солнце небо и есть весь свет → силуэт. Не-Lumen путь: «Среднее» (Lumen выключен пресетом,
//   DefaultScalability.ini GI@1) и ЛЮБОЙ пресет под DX11 (выбор RHI в настройках; Lumen под D3D11 не запускается —
//   кадры dx11 отчёта: «Максимальное»/«Ультра» чёрные, тот же кадр DX12 чистый). Под Lumen (DX12, High+) капсула в
//   картинку почти не попадает, поэтому на «Ультра» у нас баг не виден.
//   ЛЕЧЕНИЕ — штатные ручки движка, функция не срезается: самозатенение капсулами = 0 (ключ ниже) + бит приёмника у частей
//   экипа (ApplyCapsuleReceiver). Пятно под ногами на земле/объектах остаётся как было (кадры «после» отчёта).
//   Ключи — дев (в Shipping константы с умолчанием: исправление включено).

// Значение ключа asda.Shadow.CapsuleSelfFix: 1 = исправление (самозатенение из ключа ниже + части экипа — приёмники),
//   0 = прежнее поведение (значение уровня, части экипа без бита приёмника).
static int32 GAsdaCapsuleSelfFix = 1;

// Значение ключа asda.Shadow.CapsuleSelfShadowing: сила самозатенения персонажа капсулами 0..1 (0 = персонаж не темнеет от
//   капсул, 0.8 = умолчание движка). Пишется в AWorldSettings::DynamicIndirectShadowsSelfShadowingIntensity игрового мира.
static float GAsdaCapsuleSelfShadowing = 0.f;

// Метка части экипа, помеченной приёмником (ApplyCapsuleReceiver): по ней смена ключей перекладывает флаг на живых частях.
static const FName GAsdaCapsuleReceiverTag(TEXT("AsdaCapsuleReceiver"));

// Исходное значение самозатенения уровня по каждому миру (до нашей записи) — для возврата при ключе 0.
static TMap<TWeakObjectPtr<AWorldSettings>, float> GAsdaSelfShadowOrig;

// Подписка на инициализацию акторов мира (одна на процесс; ставится лениво из ApplyCapsule/ApplyCapsuleReceiver).
static FDelegateHandle GAsdaSelfShadowWorldHook;

// Записать самозатенение капсулами в настройки игрового мира и передать их сцене рендера (FScene читает значение
//   только при создании и по UpdateSceneSettings — RendererScene.cpp:720-733, :1176). Редакторские миры не трогаем.
static void AsdaApplySelfShadowToWorld(UWorld* W)
{
	if (!W || !W->IsGameWorld() || !IsInGameThread()) { return; }
	AWorldSettings* WS = W->GetWorldSettings(/*bCheckStreamingPersistent*/ false, /*bChecked*/ false);
	if (!WS) { return; }
	float* Orig = GAsdaSelfShadowOrig.Find(WS);   // исходное значение уровня
	if (!Orig)
	{
		for (auto It = GAsdaSelfShadowOrig.CreateIterator(); It; ++It) { if (!It.Key().IsValid()) { It.RemoveCurrent(); } }   // мёртвые миры
		Orig = &GAsdaSelfShadowOrig.Add(WS, WS->DynamicIndirectShadowsSelfShadowingIntensity);
	}
	const float Want = (GAsdaCapsuleSelfFix != 0) ? FMath::Clamp(GAsdaCapsuleSelfShadowing, 0.f, 1.f) : *Orig;   // целевое значение
	if (FMath::IsNearlyEqual(WS->DynamicIndirectShadowsSelfShadowingIntensity, Want)) { return; }
	WS->DynamicIndirectShadowsSelfShadowingIntensity = Want;
	if (W->Scene) { W->Scene->UpdateSceneSettings(WS); }
	UE_LOG(LogTemp, Log, TEXT("[AsdaCapsuleShadow] самозатенение капсулами в мире %s: %.2f (значение уровня %.2f)"), *W->GetName(), Want, *Orig);
}

// Колбэк OnWorldInitializedActors: каждый новый игровой мир (смена карты, логин → игра) получает значение ключа.
static void AsdaOnWorldActorsInitialized(const UWorld::FActorsInitializedParams& P)
{
	AsdaApplySelfShadowToWorld(P.World);
}

// Поставить подписку на миры (один раз за процесс).
static void AsdaEnsureSelfShadowWorldHook()
{
	if (GAsdaSelfShadowWorldHook.IsValid() || !IsInGameThread()) { return; }   // первый вызов — из CDO тела на игровом потоке
	GAsdaSelfShadowWorldHook = FWorldDelegates::OnWorldInitializedActors.AddStatic(&AsdaOnWorldActorsInitialized);
}

// Смена ключа в консоли/ini: переложить флаг на ВСЕ уже помеченные тела (CDO и шаблоны не трогаем — новые
//   экземпляры возьмут текущее значение в конструкторе через ApplyCapsule).
//   ★ 26.10.2026: затем части экипа (зависят от флага тела) и самозатенение во всех игровых мирах.
static void OnAsdaShadowCapsuleChanged(IConsoleVariable*)
{
	int32 Count = 0;   // сколько тел переложено (в лог)
	for (TObjectIterator<USkeletalMeshComponent> It; It; ++It)
	{
		USkeletalMeshComponent* Mesh = *It;
		if (!Mesh || Mesh->IsTemplate() || !Mesh->ComponentHasTag(GAsdaCapsuleShadowTag)) { continue; }
		AsdaShadow::ApplyCapsule(Mesh);
		++Count;
	}
	int32 Parts = 0;   // сколько частей экипа переложено (в лог)
	for (TObjectIterator<USkeletalMeshComponent> It; It; ++It)
	{
		USkeletalMeshComponent* Part = *It;
		if (!Part || Part->IsTemplate() || !Part->ComponentHasTag(GAsdaCapsuleReceiverTag)) { continue; }
		AsdaShadow::ApplyCapsuleReceiver(Part, Cast<USkeletalMeshComponent>(Part->LeaderPoseComponent.Get()));
		++Parts;
	}
	if (GEngine)
	{
		for (const FWorldContext& Ctx : GEngine->GetWorldContexts()) { AsdaApplySelfShadowToWorld(Ctx.World()); }
	}
	UE_LOG(LogTemp, Log, TEXT("[AsdaCapsuleShadow] капсульная тень=%d MinVisibility=%.2f исправление самозатенения=%d сила=%.2f — переложено тел: %d, частей экипа: %d"),
		GAsdaShadowCapsule, GAsdaCapsuleMinVisibility, GAsdaCapsuleSelfFix, GAsdaCapsuleSelfShadowing, Count, Parts);
}

// Регистрация ключей исправления (единственные в Source/, grep "asda.Shadow.CapsuleSelf" = 0 до правки 26.10.2026;
//   закон ue-console-duplicate-registration). Дев-ключи: в Shipping ключей нет, переменные остаются с умолчаниями.
static ASDA_DEV_CVAR_REF(CVarAsdaShadowCapsuleSelfFix,
	TEXT("asda.Shadow.CapsuleSelfFix"),
	GAsdaCapsuleSelfFix,
	TEXT("[ИСПРАВЛЕНИЕ 26.10.2026] 1 = персонаж не чернеет от СВОИХ капсульных теней в не-Lumen пути неба (Среднее, DX11): ")
	TEXT("самозатенение из asda.Shadow.CapsuleSelfShadowing + части экипа помечены приёмниками; 0 = прежнее поведение."),
	FConsoleVariableDelegate::CreateStatic(&OnAsdaShadowCapsuleChanged),
	ECVF_Default);

static ASDA_DEV_CVAR_REF(CVarAsdaShadowCapsuleSelfShadowing,
	TEXT("asda.Shadow.CapsuleSelfShadowing"),
	GAsdaCapsuleSelfShadowing,
	TEXT("[ИСПРАВЛЕНИЕ 26.10.2026] Сила самозатенения персонажа капсулами 0..1 (AWorldSettings::DynamicIndirectShadowsSelfShadowingIntensity): ")
	TEXT("0 = не темнеет (умолч.), 0.8 = умолчание движка. Действует при asda.Shadow.CapsuleSelfFix 1."),
	FConsoleVariableDelegate::CreateStatic(&OnAsdaShadowCapsuleChanged),
	ECVF_Default);

// Регистрация ключа (единственная в Source/, проверено 05.09.2026; закон ue-console-duplicate-registration).
static FAutoConsoleVariableRef CVarAsdaShadowCapsule(
	TEXT("asda.Shadow.Capsule"),
	GAsdaShadowCapsule,
	TEXT("1 = непрямая капсульная тень тела у игрока/мобов/NPC (bCastCapsuleIndirectShadow; капсулы из USkeletalMesh.ShadowPhysicsAsset), ")
	TEXT("0 = выключить. Прямая капсульная (bCastCapsuleDirectShadow) не включается никогда: под VSM мертва, под CSM = двойная тень. ")
	TEXT("Смена на лету перекладывает флаг на всех живых телах."),
	FConsoleVariableDelegate::CreateStatic(&OnAsdaShadowCapsuleChanged),
	ECVF_Default);

// Регистрация ключа силы пятна (единственная в Source/, grep "asda.Shadow.CapsuleMinVisibility" = 0 до правки 05.09.2026).
//   Смена на лету — тот же колбэк: перекладывает MinVisibility на все помеченные тела.
static FAutoConsoleVariableRef CVarAsdaShadowCapsuleMinVisibility(
	TEXT("asda.Shadow.CapsuleMinVisibility"),
	GAsdaCapsuleMinVisibility,
	TEXT("Нижняя граница видимости непрямой капсульной тени тела (CapsuleIndirectShadowMinVisibility), 0..1: 0.1 = пятно до 10 % яркости ")
	TEXT("(дефолт движка, почти чёрное в тени кроны), 0.5 = вдвое мягче. Смена на лету перекладывает значение на всех живых телах."),
	FConsoleVariableDelegate::CreateStatic(&OnAsdaShadowCapsuleChanged),
	ECVF_Default);

// ★ Доводка 05.09.2026 (review_setB_apply.md §4 шаг 2): диагностика «изнутри» — почему у тела нет капсульного прохода.
//   Печатает для КАЖДОГО помеченного тела ровно то, что читает FSkeletalMeshSceneProxy (SkeletalMeshSceneProxy.cpp:315-346)
//   и что режет CapsuleShadowRendering.cpp:709/:855 (CastsDynamicShadow && CastsDynamicIndirectShadow): меш, ShadowPhysicsAsset,
//   тела PA → BoneName / индекс кости в RefSkeleton / число Sphyl+Sphere, флаги компонента, скрытые секции (bAnySectionCastsShadow),
//   Bounds, мировой масштаб и флаги живого прокси. Только чтение; регистрация единственная в Source/ (grep 05.09 = 0).
static void AsdaShadowCapsuleDump(const TArray<FString>& Args)
{
	int32 Bodies = 0;   // сколько тел распечатано
	for (TObjectIterator<USkeletalMeshComponent> It; It; ++It)
	{
		USkeletalMeshComponent* Mesh = *It;
		if (!Mesh || Mesh->IsTemplate() || !Mesh->ComponentHasTag(GAsdaCapsuleShadowTag)) { continue; }
		if (!Mesh->GetWorld() || !Mesh->GetWorld()->IsGameWorld()) { continue; }   // редакторские превью не интересны
		++Bodies;
		USkeletalMesh* Skel = Mesh->GetSkeletalMeshAsset();
		UPhysicsAsset* PA = Skel ? Skel->GetShadowPhysicsAsset() : nullptr;
		const AActor* Owner = Mesh->GetOwner();
		const FBoxSphereBounds B = Mesh->Bounds;
		const FVector Scale = Mesh->GetComponentTransform().GetScale3D();
		// Скрытые секции LOD0: у прокси bCastDynamicShadow &&= bAnySectionCastsShadow — если скрыты ВСЕ, тень (и капсулы) не собирается.
		int32 NumSections = 0, HiddenSections = 0;
		if (Skel && Skel->GetResourceForRendering() && Skel->GetResourceForRendering()->LODRenderData.Num() > 0)
		{
			const FSkeletalMeshLODRenderData& LOD = Skel->GetResourceForRendering()->LODRenderData[0];
			NumSections = LOD.RenderSections.Num();
			for (int32 i = 0; i < NumSections; ++i) { if (!Mesh->IsMaterialSectionShown(LOD.RenderSections[i].MaterialIndex, 0)) { ++HiddenSections; } }
		}
		const FPrimitiveSceneProxy* Proxy = Mesh->SceneProxy;
		UE_LOG(LogTemp, Log, TEXT("[CapsuleDump] %s/%s mesh=%s shadowPA=%s | CastShadow=%d bCastDynamicShadow=%d CapsuleIndirect=%d CapsuleDirect=%d MinVis=%.2f | visible=%d hiddenInGame=%d ownerHidden=%d | sections=%d hidden=%d | scale=(%.3f %.3f %.3f) bounds=origin(%.0f %.0f %.0f) r=%.1f | proxy=%s CastsDynamicShadow=%d CastsDynamicIndirectShadow=%d"),
			Owner ? *Owner->GetName() : TEXT("?"), *Mesh->GetName(),
			Skel ? *Skel->GetPathName() : TEXT("NULL"), PA ? *PA->GetPathName() : TEXT("NULL"),
			Mesh->CastShadow ? 1 : 0, Mesh->bCastDynamicShadow ? 1 : 0, Mesh->bCastCapsuleIndirectShadow ? 1 : 0, Mesh->bCastCapsuleDirectShadow ? 1 : 0,
			Mesh->CapsuleIndirectShadowMinVisibility,
			Mesh->IsVisible() ? 1 : 0, Mesh->bHiddenInGame ? 1 : 0, (Owner && Owner->IsHidden()) ? 1 : 0,
			NumSections, HiddenSections, Scale.X, Scale.Y, Scale.Z, B.Origin.X, B.Origin.Y, B.Origin.Z, B.SphereRadius,
			Proxy ? TEXT("есть") : TEXT("НЕТ"), Proxy ? (Proxy->CastsDynamicShadow() ? 1 : 0) : -1, Proxy ? (Proxy->CastsDynamicIndirectShadow() ? 1 : 0) : -1);
		if (!PA) { continue; }
		int32 Found = 0, Shapes = 0;   // тел с найденной костью / всего капсул+сфер на найденных костях
		for (int32 i = 0; i < PA->SkeletalBodySetups.Num(); ++i)
		{
			const USkeletalBodySetup* BS = PA->SkeletalBodySetups[i];
			if (!BS) { UE_LOG(LogTemp, Log, TEXT("[CapsuleDump]   body[%d] = NULL"), i); continue; }
			const int32 BoneIdx = Skel ? Skel->GetRefSkeleton().FindBoneIndex(BS->BoneName) : INDEX_NONE;
			const int32 CompBoneIdx = Mesh->GetBoneIndex(BS->BoneName);
			const int32 NSphyl = BS->AggGeom.SphylElems.Num(), NSphere = BS->AggGeom.SphereElems.Num();
			if (BoneIdx != INDEX_NONE) { ++Found; Shapes += NSphyl + NSphere; }
			const FString Sz = (NSphyl > 0) ? FString::Printf(TEXT(" r0=%.3f len0=%.3f"), BS->AggGeom.SphylElems[0].Radius, BS->AggGeom.SphylElems[0].Length) : FString();
			UE_LOG(LogTemp, Log, TEXT("[CapsuleDump]   body[%d] bone='%s' refIdx=%d compIdx=%d sphyl=%d sphere=%d box=%d convex=%d%s"),
				i, *BS->BoneName.ToString(), BoneIdx, CompBoneIdx, NSphyl, NSphere, BS->AggGeom.BoxElems.Num(), BS->AggGeom.ConvexElems.Num(), *Sz);
		}
		UE_LOG(LogTemp, Log, TEXT("[CapsuleDump]   ИТОГО тел=%d костей найдено=%d фигур на найденных=%d refBones=%d"),
			PA->SkeletalBodySetups.Num(), Found, Shapes, Skel ? Skel->GetRefSkeleton().GetNum() : -1);
	}
	UE_LOG(LogTemp, Log, TEXT("[CapsuleDump] помеченных тел в игровом мире: %d (ключи asda.Shadow.Capsule=%d MinVisibility=%.2f)"), Bodies, GAsdaShadowCapsule, GAsdaCapsuleMinVisibility);
	// ★ 26.10.2026 [ИСПРАВЛЕНИЕ]: части экипа-приёмники (бит GBuffer) и самозатенение в игровых мирах.
	int32 Receivers = 0, ReceiversOn = 0, ReceiversWithProxyBit = 0;   // всего меток / с флагом / с флагом на живом прокси
	for (TObjectIterator<USkeletalMeshComponent> It; It; ++It)
	{
		const USkeletalMeshComponent* Part = *It;
		if (!Part || Part->IsTemplate() || !Part->ComponentHasTag(GAsdaCapsuleReceiverTag)) { continue; }
		if (!Part->GetWorld() || !Part->GetWorld()->IsGameWorld()) { continue; }
		++Receivers;
		if (Part->bCastCapsuleIndirectShadow) { ++ReceiversOn; }
		if (Part->SceneProxy && Part->SceneProxy->HasDynamicIndirectShadowCasterRepresentation()) { ++ReceiversWithProxyBit; }
	}
	UE_LOG(LogTemp, Log, TEXT("[CapsuleDump] части экипа-приёмники: %d, с флагом %d, с битом на прокси %d; исправление=%d сила=%.2f"),
		Receivers, ReceiversOn, ReceiversWithProxyBit, GAsdaCapsuleSelfFix, GAsdaCapsuleSelfShadowing);
	if (GEngine)
	{
		for (const FWorldContext& Ctx : GEngine->GetWorldContexts())
		{
			const UWorld* W = Ctx.World();
			const AWorldSettings* WS = (W && W->IsGameWorld()) ? W->GetWorldSettings(false, false) : nullptr;
			if (WS) { UE_LOG(LogTemp, Log, TEXT("[CapsuleDump] мир %s: самозатенение капсулами %.2f"), *W->GetName(), WS->DynamicIndirectShadowsSelfShadowingIntensity); }
		}
	}

	// ★ Направление непрямой тени = максимум SH-яркости IrradianceEnvironmentMap Movable-SkyLight (CapsuleShadowRendering.cpp:811-819) и
	//   фейд ShapeFadeAlpha = 1 − clamp(2(−Z − cos(r.CapsuleShadowFadeAngleFromVertical))/(1 − cos)) (:836-839; шейдер CapsuleShadowShaders.usf:526
	//   тем же числом умножает РАДИУС капсулы). При alpha == 0 капсулы примитива не собираются вовсе → прохода нет (:842, :1028).
	//   Печатаем то же число, что считает рендерер — по нему доводка 05.09 нашла причину «герой без капсул» на flamion/nightvalley/chess.
	static const auto* CVarFade = IConsoleManager::Get().FindTConsoleVariableDataFloat(TEXT("r.CapsuleShadowFadeAngleFromVertical"));
	const float FadeAngle = CVarFade ? CVarFade->GetValueOnGameThread() : PI / 3.f;
	const float CosFade = FMath::Cos(FadeAngle);
	for (TObjectIterator<USkyLightComponent> It; It; ++It)
	{
		USkyLightComponent* Sky = *It;
		if (!Sky || Sky->IsTemplate() || !Sky->GetWorld() || !Sky->GetWorld()->IsGameWorld()) { continue; }
		const FSHVector2 Lum = FSHVectorRGB2(Sky->GetIrradianceEnvironmentMap()).GetLuminance();
		const FVector Dir = Lum.GetMaximumDirection();
		const float Alpha = (CosFade < 1.f) ? 1.f - FMath::Clamp(2.f * (float(-Dir.Z) - CosFade) / (1.f - CosFade), 0.f, 1.f) : 1.f;
		UE_LOG(LogTemp, Log, TEXT("[CapsuleDump] SkyLight %s: mobility=%d realtime=%d lowerBlack=%d intensity=%.3f | SH L0=%.4f L1=(%.4f %.4f %.4f) | maxDir=(%.3f %.3f %.3f) | fadeAngle=%.4f cos=%.4f → ShapeFadeAlpha=%.3f%s"),
			*Sky->GetOwner()->GetName(), (int32)Sky->Mobility, Sky->bRealTimeCapture ? 1 : 0, Sky->bLowerHemisphereIsBlack ? 1 : 0, Sky->Intensity,
			Lum.V[0], Lum.V[1], Lum.V[2], Lum.V[3], Dir.X, Dir.Y, Dir.Z, FadeAngle, CosFade, Alpha,
			Alpha <= 0.f ? TEXT("  ← КАПСУЛЫ ГАСЯТСЯ ФЕЙДОМ") : TEXT(""));
	}
}

// Регистрация команды дампа (единственная в Source/, grep "asda.Shadow.CapsuleDump" = 0 до правки 05.09.2026; закон ue-console-duplicate-registration).
#if ASDA_DEV_CONSOLE   // ★ античит А2: дев-команда, в Shipping не регистрируется
static FAutoConsoleCommand CmdAsdaShadowCapsuleDump(
	TEXT("asda.Shadow.CapsuleDump"),
	TEXT("Дамп капсульной тени всех помеченных тел (игрок/мобы/NPC): меш, ShadowPhysicsAsset, тела PA → кость/индекс/число капсул, флаги компонента и прокси, скрытые секции, bounds. Только чтение."),
	FConsoleCommandWithArgsDelegate::CreateStatic(&AsdaShadowCapsuleDump),
	ECVF_Default);
#endif // ASDA_DEV_CONSOLE

namespace AsdaShadow
{
	void ApplyCapsule(USkeletalMeshComponent* Mesh)
	{
		if (!Mesh) { return; }
		const bool bOn = GAsdaShadowCapsule != 0;   // текущее значение ключа
		Mesh->ComponentTags.AddUnique(GAsdaCapsuleShadowTag);
		// Сеттеры сами помечают render state dirty только при изменении — вызов идемпотентен и безопасен в конструкторе.
		Mesh->SetCastCapsuleIndirectShadow(bOn);
		Mesh->SetCastCapsuleDirectShadow(false);
		Mesh->SetCapsuleIndirectShadowMinVisibility(FMath::Clamp(GAsdaCapsuleMinVisibility, 0.f, 1.f));   // текущее значение ключа
		// ★ 26.10.2026 [ИСПРАВЛЕНИЕ]: самозатенение капсулами — в мир этого тела (если он игровой) и подписка на следующие миры.
		AsdaEnsureSelfShadowWorldHook();
		//   Только с игрового потока: конструкторы запечённых в уровень мобов/NPC могут идти на потоке асинхронной загрузки,
		//   а UpdateSceneSettings ставит команду рендера (такие миры получат значение из подписки OnWorldInitializedActors).
		if (!Mesh->IsTemplate() && IsInGameThread()) { AsdaApplySelfShadowToWorld(Mesh->GetWorld()); }
	}

	void ApplyCapsuleReceiver(USkeletalMeshComponent* Part, const USkeletalMeshComponent* Body)
	{
		if (!Part) { return; }
		AsdaEnsureSelfShadowWorldHook();
		Part->ComponentTags.AddUnique(GAsdaCapsuleReceiverTag);
		// Меш части со своим ShadowPhysicsAsset дал бы вторую тень от своих капсул — такой не помечаем.
		const USkeletalMesh* PartMesh = Part->GetSkeletalMeshAsset();
		const bool bOwnCapsules = PartMesh && PartMesh->GetShadowPhysicsAsset() != nullptr;
		// Тело само отбрасывает капсульную тень (помечено ApplyCapsule и флаг включён) — иначе приёмник не нужен.
		const bool bBodyCasts = Body && Body->ComponentHasTag(GAsdaCapsuleShadowTag) && Body->bCastCapsuleIndirectShadow;
		const bool bOn = GAsdaCapsuleSelfFix != 0 && bBodyCasts && !bOwnCapsules;   // итог: ставить бит приёмника
		Part->SetCastCapsuleIndirectShadow(bOn);
		Part->SetCastCapsuleDirectShadow(false);
		if (bOwnCapsules)
		{
			UE_LOG(LogTemp, Warning, TEXT("[AsdaCapsuleShadow] часть %s: у меша свой ShadowPhysicsAsset — приёмником не помечена"), *Part->GetName());
		}
	}

	bool IsCapsuleEnabled()
	{
		return GAsdaShadowCapsule != 0;
	}
}
