// Copyright (c) 2026 @sitxovski (Discord)
#include "World/AsdaLocalLightActor.h"
#include "UI/AsdaDevMenuSubsystem.h"   // ★ 16.10.2026: FogVolumetricLightScale
#include "World/AsdaLightBudgetSubsystem.h"
#include "World/AsdaCullSettings.h"   // ★ SkipWorlds: миры, где отсечка дальности не применяется (Login, L_Loading)
#include "Components/PointLightComponent.h"
#include "Components/SpotLightComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Components/DirectionalLightComponent.h"
#include "Engine/DirectionalLight.h"
#include "Engine/TextureLightProfile.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "Curves/CurveFloat.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "UObject/UObjectIterator.h"
#include "HAL/IConsoleManager.h"

DEFINE_LOG_CATEGORY_STATIC(LogAsdaLocalLight, Log, All);

// ─────────────── asda.Light.IntensityScale (регистрация единственная в Source/, grep 07.09.2026 = 0) ───────────────
// Отладочный ГЛОБАЛЬНЫЙ множитель силы всех AAsdaLocalLightActor (поверх пресета и IntensityScale экземпляра) — калибровка
// шкалы «относительно солнца» на живой карте без пересборки уровня (E-4: под фонарём +10 % при rel 1.0 — [ВКУС] владельца).
// Не персистится; при смене — переприменить пресет у всех акторов всех миров.
// ★ волна 3 п.12 (15.09.2026): ОБЩИЙ колбэк ключей, меняющих пресет (asda.Light.IntensityScale, asda.Light.DrawCull) — переприменить пресет
//   у всех акторов всех игровых миров. Раньше — две одинаковые копии тела.
static void AsdaReapplyLocalLightPresets(IConsoleVariable*)
{
	for (TObjectIterator<AAsdaLocalLightActor> It; It; ++It)
	{
		if (IsValid(*It) && It->GetWorld() && It->GetWorld()->IsGameWorld()) { It->ApplyPreset(); }
	}
}
static float GAsdaLightIntensityScale = 1.f;
static FAutoConsoleVariableRef CVarAsdaLightIntensityScale(
	TEXT("asda.Light.IntensityScale"),
	GAsdaLightIntensityScale,
	TEXT("Глобальный множитель силы всех AAsdaLocalLightActor (калибровка пресетов; 1 = как в пресетах). Только рантайм, не персистится."),
	FConsoleVariableDelegate::CreateStatic(&AsdaReapplyLocalLightPresets),
	ECVF_Default);

// ─────────────── ★★★ ВОЛНА Ф9 (10.09.2026): asda.Light.DrawCull ───────────────
//   Требование владельца: «Свет не должен исчезать при удалении камеры/игрока и рисоваться только
//   вблизи». У СВЕТОВ КАРТ (фонари, жаровни, витрины) отсечка дальности приходит из пресета
//   (1500…4000 юн) и до этой волны была безусловной везде, кроме миров UAsdaCullSettings::SkipWorlds
//   (Login, L_Loading — там сцена ×100). Ключ поднимает то же исключение на ВЕСЬ проект.
//   0 = отсечки дальности у светов карт НЕТ (MaxDrawDistance 0, поведение волны Ф9, по умолчанию);
//   1 = числа пресета, как в волнах до Ф9 (полный откат одной строкой).
//   ★ Почему это не «то же самое, что у эффект-светов»: у светов карт нет ни отбора, ни бюджета
//     количества — только бюджет ТЕНЕЙ. Значит снятие дальности здесь ничем не ограничено сверху,
//     и его цену надо мерить ОТДЕЛЬНО (что и сделано, Reports/fx_light_visible/).
static int32 GAsdaLightDrawCull = 0;
static FAutoConsoleVariableRef CVarAsdaLightDrawCull(
	TEXT("asda.Light.DrawCull"),
	GAsdaLightDrawCull,
	TEXT("Отсечка светов карт по дальности: 0 = НЕТ (свет виден на любом расстоянии, по умолчанию с волны Ф9); 1 = числа пресета MaxDrawDistance (поведение до Ф9, откат)."),
	FConsoleVariableDelegate::CreateStatic(&AsdaReapplyLocalLightPresets),
	ECVF_Default);

// Отсечка дальности выключена глобально ключом (см. выше). Единственная точка ответа:
// её же спрашивает страховка «MaxDrawDistance=0 → 4000» в UAsdaLightBudgetSubsystem.
bool AAsdaLocalLightActor::IsDrawCullDisabled() { return GAsdaLightDrawCull == 0; }

AAsdaLocalLightActor::AAsdaLocalLightActor()
{
	// Tick только при кривой мерцания (включается в BeginPlay)
	PrimaryActorTick.bCanEverTick = true;
	PrimaryActorTick.bStartWithTickEnabled = false;

	Root = CreateDefaultSubobject<USceneComponent>(TEXT("Root"));
	SetRootComponent(Root);
	Root->SetMobility(EComponentMobility::Movable);

	// Оба компонента созданы; активен один по LightClass пресета (см. ApplyPreset)
	Point = CreateDefaultSubobject<UPointLightComponent>(TEXT("Point"));
	Point->SetupAttachment(Root);
	Point->SetMobility(EComponentMobility::Movable);

	Spot = CreateDefaultSubobject<USpotLightComponent>(TEXT("Spot"));
	Spot->SetupAttachment(Root);
	Spot->SetMobility(EComponentMobility::Movable);
	Spot->SetVisibility(false);
	Spot->bAffectsWorld = false;

	Mesh = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("Mesh"));
	Mesh->SetupAttachment(Root);
	Mesh->SetMobility(EComponentMobility::Movable);
	Mesh->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	Mesh->SetCastShadow(false);   // столб/кристалл не должен резать тень собственного света
}

FName AAsdaLocalLightActor::RoleTag(EAsdaLightRole R)
{
	switch (R)
	{
	case EAsdaLightRole::Main: return FName(TEXT("Main"));
	case EAsdaLightRole::Fill: return FName(TEXT("Fill"));
	default:                   return FName(TEXT("Glow"));
	}
}

float AAsdaLocalLightActor::FindSunLux(UWorld* World)
{
	if (!World) { return -1.f; }
	// Sun_<map> по имени/метке (asda_convert_map_lit.py ставит метку Sun_<map>); иначе — самый яркий DirectionalLight мира
	float Best = -1.f;
	bool bBestIsSun = false;
	for (TActorIterator<ADirectionalLight> It(World); It; ++It)
	{
		ADirectionalLight* A = *It;
		UDirectionalLightComponent* C = A ? Cast<UDirectionalLightComponent>(A->GetLightComponent()) : nullptr;
		if (!C) { continue; }
		const bool bIsSun = A->GetActorNameOrLabel().StartsWith(TEXT("Sun_")) || A->GetFName().ToString().StartsWith(TEXT("Sun_"));
		if (bIsSun && !bBestIsSun) { Best = C->Intensity; bBestIsSun = true; continue; }
		if (bBestIsSun) { continue; }
		Best = FMath::Max(Best, C->Intensity);
	}
	return Best;
}

ULocalLightComponent* AAsdaLocalLightActor::GetActiveLight() const
{
	if (Spot && Spot->bAffectsWorld) { return Spot; }
	return Point;
}

bool AAsdaLocalLightActor::GetAuthoredCastShadows() const
{
	if (bOverrideShadows) { return bCastShadows; }
	if (LightRole != EAsdaLightRole::Main) { return false; }
	const FAsdaLightPresetSpec* P = UAsdaRenderSettings::Get()->FindPreset(Preset);
	return P ? P->bShadowsIfRoleAllows : true;   // Custom + Main = тени авторские
}

bool AAsdaLocalLightActor::IsNoDrawCullWorld() const
{
	// ★★★ ВОЛНА Ф9: ключ asda.Light.DrawCull 0 поднимает исключение «без отсечки» на ВЕСЬ проект —
	//   требование владельца «свет не должен исчезать при удалении камеры». Проверяется ПЕРВЫМ:
	//   список миров остаётся на случай отката ключом (в Login/L_Loading отсечки не должно быть
	//   ни при каких значениях — там камера в 68 000–87 000 юн от постановки).
	if (IsDrawCullDisabled()) { return true; }
	// Список — общий с отсечкой мешей (одна настройка на проект, [/Script/AsdaReborn.AsdaCullSettings] SkipWorlds)
	const UWorld* W = GetWorld();
	return W && UAsdaCullSettings::Get()->SkipWorlds.Contains(FName(*W->GetName()));
}

void AAsdaLocalLightActor::ApplyInvariants(ULocalLightComponent* L, const FAsdaLightPresetSpec* P) const
{
	if (!L) { return; }
	L->SetMobility(EComponentMobility::Movable);                     // r.AllowStaticLighting=False — иначе не светит (аудит E01)
	L->SetIntensityUnits(ELightUnits::Candelas);                     // Unitless непереносим (1 кд = 625)
	if (UPointLightComponent* PL = Cast<UPointLightComponent>(L))    // Spot наследует Point
	{
		PL->SetUseInverseSquaredFalloff(P ? P->bUseInverseSquaredFalloff : true);
	}
	// MaxDrawDistance > 0 обязательно (аудит E02/W03): дефолт — из пресета или из настроек.
	// ★★★ ИСКЛЮЧЕНИЕ — МИРЫ БЕЗ ОТСЕЧКИ ДАЛЬНОСТИ (10.09.2026): в Login/L_Loading (UAsdaCullSettings::SkipWorlds)
	//   камера стоит в 68 000–87 000 юн от постановки (сцена ×100), и дальность 1500–4000 юн из пресета гасит
	//   ЛЮБОЙ поставленный туда фонарь ещё до того, как он что-то осветит. Там 0 = «никогда не гаснет» — авторское.
	const UAsdaRenderSettings* S = UAsdaRenderSettings::Get();
	if (L->MaxDrawDistance <= 0.f && !IsNoDrawCullWorld())
	{
		L->SetMaxDrawDistance(P ? P->MaxDrawDistance : S->DefaultMaxDrawDistance);
		L->SetMaxDistanceFadeRange(P ? P->MaxDistanceFadeRange : S->DefaultMaxDistanceFadeRange);
	}
	L->SetCastVolumetricShadow(false);                               // отдельный проход на свет (аудит W07)
	L->SetUseRayTracedDistanceFieldShadows(false);                   // DF-тени локальных грубые (аудит W06)
	L->SetUseIESBrightness(false);                                   // яркость остаётся из кд
}

void AAsdaLocalLightActor::ApplyRoleTag()
{
	static const FName Roles[] = { FName(TEXT("Main")), FName(TEXT("Fill")), FName(TEXT("Glow")) };
	for (const FName& R : Roles) { Tags.Remove(R); }
	Tags.AddUnique(RoleTag(LightRole));
}

void AAsdaLocalLightActor::PushAuthoredShadows(ULocalLightComponent* L, bool bAuthored)
{
	if (!L) { return; }
	UWorld* W = GetWorld();
	UAsdaLightBudgetSubsystem* B = W ? W->GetSubsystem<UAsdaLightBudgetSubsystem>() : nullptr;
	if (B)
	{
		// Игровой мир: авторское значение — в кэш подсистемы; она же решит по бюджету на ближайшем Tick
		B->SetAuthoredCastShadows(L, bAuthored);
	}
	else
	{
		// Редактор (не игровой мир): авторское значение видно сразу
		L->SetCastShadows(bAuthored);
	}
}

void AAsdaLocalLightActor::ApplyPreset()
{
	const UAsdaRenderSettings* S = UAsdaRenderSettings::Get();
	const FAsdaLightPresetSpec* P = S->FindPreset(Preset);   // nullptr при Custom (или если пресет не сгенерирован)

	// Активный компонент по классу пресета (Custom — оставить как есть, по умолчанию Point)
	const bool bSpot = P ? (P->LightClass == EAsdaLightClass::Spot) : (Spot && Spot->bAffectsWorld);
	ULocalLightComponent* Active   = bSpot ? static_cast<ULocalLightComponent*>(Spot) : static_cast<ULocalLightComponent*>(Point);
	ULocalLightComponent* Inactive = bSpot ? static_cast<ULocalLightComponent*>(Point) : static_cast<ULocalLightComponent*>(Spot);
	if (Inactive)
	{
		Inactive->bAffectsWorld = false;
		Inactive->SetVisibility(false);
		Inactive->SetCastShadows(false);
		ApplyInvariants(Inactive, P);   // и у спящего близнеца MaxDrawDistance>0/Movable — аудит уровня не должен видеть «сырой» компонент
		Inactive->MarkRenderStateDirty();
	}
	if (!Active) { return; }
	Active->bAffectsWorld = true;
	Active->SetVisibility(true);
	Active->MarkRenderStateDirty();

	// Сила: переопределение → солнце карты → alpen-число пресета
	ResolvedSunLux = bScaleBySun ? FindSunLux(GetWorld()) : -1.f;
	float I = 0.f;
	if (bOverrideIntensity)
	{
		I = IntensityCd; ResolvedIntensitySource = TEXT("своя (кд)");
	}
	else if (P)
	{
		if (bScaleBySun && ResolvedSunLux > 0.f)
		{
			I = P->IntensityRelSun * ResolvedSunLux * FMath::Square(P->DRefM);
			ResolvedIntensitySource = FString::Printf(TEXT("rel %.2f × E_sun %.2f лк × %.1f м²"), P->IntensityRelSun, ResolvedSunLux, FMath::Square(P->DRefM));
		}
		else
		{
			I = P->IntensityCdAlpen;
			ResolvedIntensitySource = bScaleBySun ? TEXT("alpen-число пресета (солнца на карте нет)") : TEXT("alpen-число пресета");
		}
		I *= IntensityScale * GAsdaLightIntensityScale;
	}
	else
	{
		I = Active->Intensity; ResolvedIntensitySource = TEXT("Custom: значение компонента");
	}

	if (P)
	{
		// Цвет
		if (bOverrideColor)             { Active->SetUseTemperature(false); Active->SetLightFColor(LightColorSRGB); }
		else if (P->bUseTemperature)    { Active->SetUseTemperature(true);  Active->SetTemperature(P->TemperatureK); Active->SetLightFColor(FColor::White); }
		else                            { Active->SetUseTemperature(false); Active->SetLightFColor(P->LightColorSRGB); }
		// Форма
		Active->SetAttenuationRadius(bOverrideRadius ? AttenuationRadius : P->AttenuationRadius);
		if (UPointLightComponent* PL = Cast<UPointLightComponent>(Active))
		{
			PL->SetSourceRadius(P->SourceRadius);
			PL->SetSoftSourceRadius(P->SoftSourceRadius);
			PL->SetSourceLength(P->SourceLength);
		}
		if (USpotLightComponent* SL = Cast<USpotLightComponent>(Active))
		{
			SL->SetInnerConeAngle(P->InnerConeAngle);
			SL->SetOuterConeAngle(P->OuterConeAngle);
		}
		// Дальность (пресет всегда >0). ★ В мирах без отсечки (Login/L_Loading) число пресета НЕ навязываем:
		//   1500–4000 юн там меньше, чем расстояние от камеры до всей сцены (68 000–87 000 юн), и фонарь
		//   гаснет, не успев ничего осветить. 0 = «никогда не гаснет» (см. ApplyInvariants).
		const bool bNoDrawCull = IsNoDrawCullWorld();
		Active->SetMaxDrawDistance(bNoDrawCull ? 0.f : P->MaxDrawDistance);
		Active->SetMaxDistanceFadeRange(bNoDrawCull ? 0.f : P->MaxDistanceFadeRange);
		// Lumen / туман
		Active->SetAffectGlobalIllumination(P->bAffectGlobalIllumination);
		Active->SetIndirectLightingIntensity(P->IndirectLightingIntensity);
		Active->SetVolumetricScatteringIntensity(P->VolumetricScatteringIntensity * UAsdaDevMenuSubsystem::FogVolumetricLightScale(GetWorld()));   // ★ 16.10.2026: луч под разрежённый туман игры
		// IES (по желанию пресета)
		UTextureLightProfile* IES = P->IES.IsNull() ? nullptr : P->IES.LoadSynchronous();
		Active->SetIESTexture(IES);
		// Меш свечения: эмиссив-материал + признак источника для Lumen
		if (Mesh)
		{
			if (P->bMeshEmissiveLightSource) { Mesh->SetEmissiveLightSource(true); }
			if (!P->EmissiveMaterial.IsNull() && Mesh->GetStaticMesh())
			{
				UMaterialInterface* Base = P->EmissiveMaterial.LoadSynchronous();
				if (Base && (!EmissiveMID || EmissiveMID->Parent != Base))
				{
					EmissiveMID = UMaterialInstanceDynamic::Create(Base, this);
				}
				if (EmissiveMID)
				{
					EmissiveMID->SetScalarParameterValue(TEXT("EmissiveMultiplier"), P->EmissiveMultiplier);
					for (int32 i = 0; i < Mesh->GetNumMaterials(); ++i) { Mesh->SetMaterial(i, EmissiveMID); }
				}
			}
		}
	}

	ApplyInvariants(Active, P);
	Active->SetIntensity(I);
	BaseIntensityCd = I;
	ResolvedIntensityCd = I;

	ApplyRoleTag();
	PushAuthoredShadows(Active, GetAuthoredCastShadows());
}

void AAsdaLocalLightActor::OnConstruction(const FTransform& Xf)
{
	Super::OnConstruction(Xf);
	ApplyPreset();
}

void AAsdaLocalLightActor::BeginPlay()
{
	Super::BeginPlay();
	// В игре конструкция уровня не переисполняется — пересчитать от солнца карты и сообщить бюджету авторские тени
	ApplyPreset();
	SetActorTickEnabled(FlickerCurve != nullptr);
	UE_LOG(LogAsdaLocalLight, Verbose, TEXT("%s: пресет %s, роль %s, %.3f кд (%s), тени авторские %d"), *GetActorNameOrLabel(),
		*UEnum::GetValueAsString(Preset), *RoleTag(LightRole).ToString(), ResolvedIntensityCd, *ResolvedIntensitySource, GetAuthoredCastShadows() ? 1 : 0);
}

void AAsdaLocalLightActor::Tick(float DeltaSeconds)
{
	Super::Tick(DeltaSeconds);
	if (!FlickerCurve) { SetActorTickEnabled(false); return; }
	// Мерцание: множитель силы по кривой (X = фаза 0..1 периода 1/FlickerHz), амплитуда сжимает отклонение от 1
	FlickerPhase = FMath::Fmod(FlickerPhase + DeltaSeconds * FlickerHz, 1.f);
	const float K = 1.f + FlickerAmplitude * (FlickerCurve->GetFloatValue(FlickerPhase) - 1.f);
	if (ULocalLightComponent* L = GetActiveLight()) { L->SetIntensity(FMath::Max(0.f, BaseIntensityCd * K)); }
}

#if WITH_EDITOR
void AAsdaLocalLightActor::PostEditChangeProperty(FPropertyChangedEvent& E)
{
	const FName N = E.GetPropertyName();
	if (N == GET_MEMBER_NAME_CHECKED(AAsdaLocalLightActor, Preset))
	{
		// Смена пресета — роль по умолчанию пресета (владелец может сменить следом)
		if (const FAsdaLightPresetSpec* P = UAsdaRenderSettings::Get()->FindPreset(Preset)) { LightRole = P->DefaultRole; }
	}
	Super::PostEditChangeProperty(E);   // → RerunConstructionScripts → OnConstruction → ApplyPreset
}
#endif
