// Copyright (c) 2026 @sitxovski (Discord)
#include "AsdaCloudShadowsActor.h"
#include "AsdaSkyDomeActor.h"        // AsdaSkyCloudSpeedFactor — множитель скорости облаков неба (asda.Clouds.SlowSky)
#include "HAL/IConsoleManager.h"     // FAutoConsoleVariableRef asda.Clouds.Shadows
#include "UObject/UObjectIterator.h"
#include "Engine/DirectionalLight.h"
#include "Components/LightComponent.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "Engine/World.h"                      // мир дев-камеры кадров (asda.Clouds.ShotCam)
#include "Camera/CameraActor.h"                // транзиентная камера кадров
#include "Camera/CameraComponent.h"            // SetConstraintAspectRatio камеры кадров
#include "GameFramework/PlayerController.h"    // вид контроллера на камеру кадров

AAsdaCloudShadowsActor::AAsdaCloudShadowsActor()
{
	PrimaryActorTick.bCanEverTick = false;
	RootComponent = CreateDefaultSubobject<USceneComponent>(TEXT("Root"));
}

void AAsdaCloudShadowsActor::OnConstruction(const FTransform&)
{
	Apply();
}

void AAsdaCloudShadowsActor::BeginPlay()
{
	Super::BeginPlay();
	Apply();
}

#if WITH_EDITOR
void AAsdaCloudShadowsActor::PostEditChangeProperty(FPropertyChangedEvent& E)
{
	Super::PostEditChangeProperty(E);
	Apply();
}
#endif

// ★ Тумблер теней облаков (05.09.2026, dev-меню Ctrl+N → «Небо и туман»): 1 = Light Function на солнце (как было),
//   0 = функция света снята (солнце без облачных пятен). Смена на лету переприменяет все акторы карты.
//   Регистрация единственная в Source/ (grep "asda.Clouds.Shadows" = 0 до правки; закон ue-console-duplicate-registration).
static int32 GAsdaCloudShadows = 1;   // текущее значение ключа asda.Clouds.Shadows
static void OnAsdaCloudShadowsChanged(IConsoleVariable*)
{
	int32 Count = 0;   // сколько акторов переприменено (в лог)
	for (TObjectIterator<AAsdaCloudShadowsActor> It; It; ++It)
	{
		if (!*It || It->IsTemplate() || !It->GetWorld()) { continue; }   // без мира не трогаем (ревизия 05.09 М-4)
		It->Apply();
		++Count;
	}
	UE_LOG(LogTemp, Log, TEXT("[AsdaCloudShadows] asda.Clouds.Shadows=%d — переприменено акторов: %d"), GAsdaCloudShadows, Count);
}
static FAutoConsoleVariableRef CVarAsdaCloudShadows(
	TEXT("asda.Clouds.Shadows"),
	GAsdaCloudShadows,
	TEXT("1 = тени облаков (Light Function AAsdaCloudShadowsActor на солнце карты), 0 = снять. Смена на лету переприменяет акторы карты."),
	FConsoleVariableDelegate::CreateStatic(&OnAsdaCloudShadowsChanged),
	ECVF_Default);

// ★ [ИСПРАВЛЕНИЕ] 01.11.2026 (волна cloud_shadows, Reports/cloud_shadows): владелец — тени облаков почти чёрные
//   («белые облака не дают чёрных пятен»). Рабочий мастер M_AsdaCloudShadowLF гасит солнце в ядре облака целиком
//   (Strength 1, край A·5). Варианты на выбор владельцу — отдельный мастер M_AsdaCloudShadowLFVar
//   (Tools/asda_build_cloud_shadow_var.py) с параметрами из таблицы ниже; 0 = прежний мастер (как было).
//   Теней облаков в оригинале нет (CLOUDE2 клиент грузит только для купола) — это добавка порта [ПОРТ].
static int32 GAsdaCloudShadowStyle = 4;   // текущее значение ключа asda.Clouds.ShadowStyle; умолчание 4 = D «дымка 30%» (выбор владельца 01.11.2026), 0 = прежний мастер
static FAutoConsoleVariableRef CVarAsdaCloudShadowStyle(
	TEXT("asda.Clouds.ShadowStyle"),
	GAsdaCloudShadowStyle,
	TEXT("Вид теней облаков: 0 = как было (ядро гасит солнце полностью), 1 = A тень 55%, 2 = B 40% + мягкий край, 3 = C разная плотность 12..80%, 4 = D дымка 30%, 5 = E мягкий край + плотность 36..65%. Умолчание 4 (выбор владельца). Смена на лету."),
	FConsoleVariableDelegate::CreateStatic(&OnAsdaCloudShadowsChanged),
	ECVF_Default);
static float GAsdaCloudShadowStrength = -1.f;   // текущее значение ключа asda.Clouds.ShadowStrength (<0 = по виду)
static FAutoConsoleVariableRef CVarAsdaCloudShadowStrength(
	TEXT("asda.Clouds.ShadowStrength"),
	GAsdaCloudShadowStrength,
	TEXT("Подбор глубины теней облаков для видов 1..5 (0..1, доля погашенного солнца в ядре облака); меньше 0 = из таблицы вида. Смена на лету."),
	FConsoleVariableDelegate::CreateStatic(&OnAsdaCloudShadowsChanged),
	ECVF_Default);

namespace
{
	// Параметры одного вида теней облаков (имена = скаляры M_AsdaCloudShadowLFVar).
	struct FAsdaCloudShadowStyle
	{
		float Strength;   // глубина: доля погашенного солнца в ядре облака
		float EdgeLo;     // альфа маски, с которой начинается тень
		float EdgeHi;     // альфа маски, на которой тень полная
		float Smooth;     // 0 = линейный край, 1 = smoothstep (мягкий)
		float DensMin;    // плотность самых «тонких» облаков (1 = все одинаковы)
		float NoiseGain;  // усиление поля плотности (больше — больше плотных облаков)
	};
	// Виды 1..5 (индекс = значение ключа − 1). Дефолты мастера = вид 0 (Strength 1, 0..0.2, линейно, DensMin 1).
	const FAsdaCloudShadowStyle GAsdaCloudShadowStyles[] = {
		{ 0.55f, 0.00f, 0.20f, 0.f, 1.00f, 3.f },   // A: та же форма, солнце гаснет на 55% (прозрачнее на 45%)
		{ 0.40f, 0.02f, 0.45f, 1.f, 1.00f, 3.f },   // B: 40% + широкий мягкий край
		{ 0.80f, 0.02f, 0.30f, 1.f, 0.15f, 3.f },   // C: разная плотность облаков — от 12% до 80%
		{ 0.30f, 0.00f, 0.60f, 1.f, 1.00f, 3.f },   // D: лёгкая дымка — 30%, очень мягкий край
		{ 0.65f, 0.03f, 0.40f, 1.f, 0.55f, 3.f },   // E: мягкий край + разная плотность, 36..65%
	};
	const TCHAR* const GAsdaCloudShadowVarPath = TEXT("/Game/Materials/M_AsdaCloudShadowLFVar.M_AsdaCloudShadowLFVar");   // мастер видов
}

#if !UE_BUILD_SHIPPING
// Дев-камера кадров волны cloud_shadows: `asda.Clouds.ShotCam x y z pitch yaw` — транзиентная камера в точке, вид
//   контроллера на неё (офлайн BugItGo двигает павна, а игровая камера третьего лица pitch/высоту не берёт).
static TWeakObjectPtr<ACameraActor> GAsdaCloudShotCam;   // камера, созданная командой (переиспользуется)
static FAutoConsoleCommandWithWorldAndArgs CmdAsdaCloudShotCam(
	TEXT("asda.Clouds.ShotCam"),
	TEXT("Дев-камера кадров: x y z pitch yaw — поставить камеру и смотреть через неё."),
	FConsoleCommandWithWorldAndArgsDelegate::CreateLambda([](const TArray<FString>& Args, UWorld* World)
	{
		APlayerController* PC = World ? World->GetFirstPlayerController() : nullptr;
		if (!PC || Args.Num() < 5) { UE_LOG(LogTemp, Warning, TEXT("[AsdaCloudShadows] ShotCam: нужен мир с контроллером и 5 чисел")); return; }
		const FVector Loc(FCString::Atof(*Args[0]), FCString::Atof(*Args[1]), FCString::Atof(*Args[2]));
		const FRotator Rot(FCString::Atof(*Args[3]), FCString::Atof(*Args[4]), 0.f);
		ACameraActor* Cm = GAsdaCloudShotCam.Get();
		if (!Cm || Cm->GetWorld() != World)
		{
			FActorSpawnParameters SP;
			SP.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
			SP.ObjectFlags |= RF_Transient;
			Cm = World->SpawnActor<ACameraActor>(ACameraActor::StaticClass(), Loc, Rot, SP);
			if (Cm && Cm->GetCameraComponent()) { Cm->GetCameraComponent()->SetConstraintAspectRatio(false); }
			GAsdaCloudShotCam = Cm;
		}
		if (!Cm) { return; }
		Cm->SetActorLocationAndRotation(Loc, Rot);
		PC->SetViewTargetWithBlend(Cm, 0.f);
		UE_LOG(LogTemp, Log, TEXT("[AsdaCloudShadows] ShotCam: камера %s поворот %s"), *Loc.ToCompactString(), *Rot.ToCompactString());
	}));
#endif

void AAsdaCloudShadowsActor::Apply()
{
	if (!Sun || !Master) { return; }
	ULightComponent* LC = Sun->GetLightComponent();
	if (!LC) { return; }
	// Ключ выключен → снять функцию света и выйти (материал не создаём).
	if (GAsdaCloudShadows == 0) { LC->SetLightFunctionMaterial(nullptr); return; }
	// ★ 01.11.2026: вид теней 1..5 → мастер вариантов M_AsdaCloudShadowLFVar; нет ассета → прежний мастер (вид 0).
	const int32 Style = FMath::Clamp(GAsdaCloudShadowStyle, 0, (int32)UE_ARRAY_COUNT(GAsdaCloudShadowStyles));   // вид после ограничения
	UMaterialInterface* Parent = Master;   // родитель MID для текущего вида
	if (Style > 0)
	{
		static TWeakObjectPtr<UMaterialInterface> VarMaster;   // загруженный мастер видов (один на процесс)
		if (!VarMaster.IsValid()) { VarMaster = LoadObject<UMaterialInterface>(nullptr, GAsdaCloudShadowVarPath); }
		if (VarMaster.IsValid()) { Parent = VarMaster.Get(); }
		else { UE_LOG(LogTemp, Warning, TEXT("[AsdaCloudShadows] нет мастера видов %s — тени как было"), GAsdaCloudShadowVarPath); }
	}
	// Один MID на актор: создаём лишь если его нет или сменился мастер (М-3).
	if (!LightFunctionMID || LightFunctionMID->Parent != Parent) { LightFunctionMID = UMaterialInstanceDynamic::Create(Parent, this); }
	UMaterialInstanceDynamic* MID = LightFunctionMID;
	if (Parent != Master)
	{
		const FAsdaCloudShadowStyle& S = GAsdaCloudShadowStyles[Style - 1];   // параметры вида
		const float Strength = (GAsdaCloudShadowStrength >= 0.f) ? FMath::Clamp(GAsdaCloudShadowStrength, 0.f, 1.f) : S.Strength;
		MID->SetScalarParameterValue(TEXT("Strength"), Strength);
		MID->SetScalarParameterValue(TEXT("EdgeLo"), S.EdgeLo);
		MID->SetScalarParameterValue(TEXT("EdgeHi"), S.EdgeHi);
		MID->SetScalarParameterValue(TEXT("Smooth"), S.Smooth);
		MID->SetScalarParameterValue(TEXT("DensMin"), S.DensMin);
		MID->SetScalarParameterValue(TEXT("NoiseGain"), S.NoiseGain);
		// Масштаб и скорость пятен — как у рабочего мастера (одна картинка облаков, другая только глубина/край).
		float Tile = 60000.f;   // мир-единиц на повтор текстуры (дефолт рабочего мастера)
		if (Master->GetScalarParameterValue(FHashedMaterialParameterInfo(TEXT("TileSize")), Tile)) { MID->SetScalarParameterValue(TEXT("TileSize"), Tile); }
	}
	// ★★★ ПОСТОЯННЫЕ ПАРАМЕТРЫ ПЕРЕЕХАЛИ В ДЕФОЛТЫ МАСТЕРА `M_AsdaCloudShadowLF` (2026-08-27,
	//   §6.2 аудита `Reports/audit_runtime_style.md`). Раньше здесь в рантайме ставились
	//   `TileSize / SpeedU / SpeedV / Strength / Contrast` — все пять одинаковы на ВСЕХ 18 картах
	//   (замер `Tools/asda_probe_sky_params.py`, лог `Reports/headless_probe_sky2.log`).
	//   Значения в мастере приведены к тем, что работали: `Strength = 1`, `Contrast = 5`
	//   (было 0.45 и 3.0 — их и перебивал код), `TileSize = 60000`, `SpeedU = 0.01`, `SpeedV = 0`.
	//   Правит их шаг `Tools/asda_bake_sky_material_params.py`; менять — в самом материале.
	// ⚠ ТЕКСТУРА ОСТАЁТСЯ В КОДЕ: `CloudTex` — ПЕР-КАРТОЧНАЯ (`SKY_DOME_CLOUDE2` своей карты),
	//   её в общий материал не положить, а пер-карточный `MI_cloudshadow` был осознанно удалён
	//   в пользу этого актора (`Tools/asda_place_cloud_shadow_actors.py:3`).
	if (CloudTexture) { MID->SetTextureParameterValue(TEXT("CloudTex"), CloudTexture); }
	// ★ [ИСПРАВЛЕНИЕ] 29.10.2026: скорость теней облаков × AsdaSkyCloudSpeedFactor() (ключ asda.Clouds.SlowSky,
	//   1 = вдвое медленнее, 0 = как было) — синхронно со скроллом купола. Исходные SpeedU/SpeedV — из мастера
	//   (не из MID), поэтому повторное применение множитель не накапливает.
	static const FName NSpeedU(TEXT("SpeedU"));   // скорость пятен по U, UV/с (дефолт мастера 0.01)
	static const FName NSpeedV(TEXT("SpeedV"));   // скорость пятен по V, UV/с (дефолт мастера 0)
	const float Factor = AsdaSkyCloudSpeedFactor();
	for (const FName& Speed : { NSpeedU, NSpeedV })
	{
		float Base = 0.f;   // скорость мастера до множителя
		if (Master->GetScalarParameterValue(FHashedMaterialParameterInfo(Speed), Base)) { MID->SetScalarParameterValue(Speed, Base * Factor); }
	}
	LC->SetLightFunctionMaterial(MID);
}
