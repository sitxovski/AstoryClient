// Copyright (c) 2026 @sitxovski (Discord)
#include "AsdaSkyDomeActor.h"
#include "Components/StaticMeshComponent.h"
#include "AsdaView.h"   // ★ волна 3 п.11 (15.09.2026): общая точка наблюдателя (своя копия GetViewLocation снята, E7)
#include "AsdaCloudShadowsActor.h"               // переприменение теней облаков при смене asda.Clouds.SlowSky
#include "HAL/IConsoleManager.h"                 // FAutoConsoleVariableRef asda.Clouds.SlowSky
#include "UObject/UObjectIterator.h"
#include "Materials/MaterialInstanceDynamic.h"

// ★ [ИСПРАВЛЕНИЕ] 29.10.2026 — скорость облаков неба (баг-репорт игроков «We need to decrease speed of clouds»,
//   владелец: «в skybox-е анимации и тени … в два раза на всех картах замедли»). 3D-слой (CloudField) владелец
//   замедлил сам в уровнях — здесь его нет. Скорость задают материалы: купол — `Time × ScrollBoost × ScrollUV`
//   (`M_AsdaSkyDome`, пер-карточный `MI_skydome.ScrollBoost = 9`), тени — `Time × SpeedU/V` (`M_AsdaCloudShadowLF`,
//   0.01/0). Уровни (.umap) по запрету владельца не трогаем, поэтому множитель — в коде акторов, ко всем картам сразу.
static int32 GAsdaCloudsSlowSky = 1;               // текущее значение ключа asda.Clouds.SlowSky
static constexpr float kAsdaSlowSkyFactor = 0.5f;  // во сколько раз медленнее при ключе 1 (просьба владельца: вдвое)

float AsdaSkyCloudSpeedFactor()
{
	return GAsdaCloudsSlowSky != 0 ? kAsdaSlowSkyFactor : 1.f;
}

// Смена ключа на лету: переприменить купола и тени облаков всех миров. ⚠ Скролл = Time × скорость, поэтому
//   облака при переключении один раз «перескакивают» в новую фазу; при загрузке карты (BeginPlay) скачка нет.
static void OnAsdaCloudsSlowSkyChanged(IConsoleVariable*)
{
	int32 Domes = 0;     // сколько куполов переприменено (в лог)
	int32 Shadows = 0;   // сколько акторов теней облаков переприменено (в лог)
	for (TObjectIterator<AAsdaSkyDomeActor> It; It; ++It)
	{
		if (!*It || It->IsTemplate() || !It->GetWorld()) { continue; }
		It->ApplyCloudSpeed();
		++Domes;
	}
	for (TObjectIterator<AAsdaCloudShadowsActor> It; It; ++It)
	{
		if (!*It || It->IsTemplate() || !It->GetWorld()) { continue; }
		It->Apply();
		++Shadows;
	}
	UE_LOG(LogTemp, Log, TEXT("[AsdaSky] множитель скорости облаков неба %.2f — куполов %d, теней облаков %d"),
		AsdaSkyCloudSpeedFactor(), Domes, Shadows);
}
static FAutoConsoleVariableRef CVarAsdaCloudsSlowSky(
	TEXT("asda.Clouds.SlowSky"),
	GAsdaCloudsSlowSky,
	TEXT("[ИСПРАВЛЕНИЕ 29.10.2026] 1 = облака неба (скролл купола) и их тени на земле движутся вдвое медленнее, 0 = прежняя скорость. 3D-облака не затрагивает."),
	FConsoleVariableDelegate::CreateStatic(&OnAsdaCloudsSlowSkyChanged),
	ECVF_Default);

AAsdaSkyDomeActor::AAsdaSkyDomeActor()
{
	PrimaryActorTick.bCanEverTick = true;
	Dome = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("Dome"));
	RootComponent = Dome;
	Dome->SetMobility(EComponentMobility::Movable);
	Dome->SetCastShadow(false);
	Dome->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	// Небо не должно попадать в дистанц-кулл и влиять на стриминг-bounds
	Dome->bAffectDistanceFieldLighting = false;
	// ★ 05.09.2026 (доводка сета A, review_setA_rollout.md §4 (д)): материал купола стал TRANSLUCENT unlit с
	//   Apply Fogging=off — только так в UE 5.8 меш выходит из-под ExponentialHeightFog (opaque+IsSky туман
	//   получает, Material.h:1088). Транслюценты сортируются по приоритету, потом по расстоянию до центра bounds;
	//   центр bounds купола = камера + 14 250 юн, т.е. без приоритета купол рисовался бы ПОВЕРХ всех транслюцентов
	//   дальше 142 м (3D-облака CloudField на z=35 000, дальние эффекты). Меньший приоритет рисуется раньше
	//   (BasePassRendering.cpp:317) — купол под всеми (эффекты 5, LayeredClouds 4, Login 1). То же значение кладёт
	//   генератор Tools/asda_build_sky_dome.py (DOME_SORT_PRIORITY); здесь — умолчание для всех уже размещённых
	//   акторов (в уровнях поле не сериализовано, пока равно умолчанию).
	Dome->SetTranslucentSortPriority(-1000);
}

// ★★★ `ApplySkyParams` УДАЛЁН 2026-08-27 (§6.2 аудита `Reports/audit_runtime_style.md`).
//   Он ставил в рантайме ровно два ПОСТОЯННЫХ числа — `ScrollBoost` слоту облаков и `HorizonBoost`
//   ленте горизонта — и ради них строил по два MID на карту поверх пер-карточных MIC.
//   ЗАМЕР ДО ПРАВКИ (`Tools/asda_probe_sky_params.py`, лог `Reports/headless_probe_sky2.log`):
//   на ВСЕХ 18 картах актор нёс `CloudsSpeedBoost = 9`, `HorizonBoost = 1.725`, а в ассетах лежало
//   `MI_skydome.ScrollBoost = 10` (код его перебивал) и `MI_skyhorizon.HorizonBoost = 1.725` (дубль).
//   Теперь `MI_skydome.ScrollBoost = 9` на всех 18 картах ⇒ КАРТИНКА ТА ЖЕ, а число видно и
//   правится в редакторе материала. Хранитель значения — `Tools/asda_bake_sky_material_params.py`,
//   генератор неба `Tools/asda_build_sky_dome.py` кладёт то же 9.0.
//   ⚠ Остальные параметры купола (скорости `ScrollU1/V1/U2/V2` из .TER, фог, `DomeScale`) как были
//   в пер-карточных MIC, так и остались — их этот актор никогда не трогал.

void AAsdaSkyDomeActor::BeginPlay()
{
	Super::BeginPlay();
	ApplyCloudSpeed();
}

void AAsdaSkyDomeActor::ApplyCloudSpeed()
{
	UWorld* World = GetWorld();
	// Только игровой мир: в окне редактора уровня MID на компоненте сохранился бы в .umap (запрет владельца).
	if (!Dome || !World || !World->IsGameWorld()) { return; }
	static const FName NScrollBoost(TEXT("ScrollBoost"));   // множитель скорости обоих слоёв купола (M_AsdaSkyDome)
	const float Factor = AsdaSkyCloudSpeedFactor();
	const int32 NumSlots = Dome->GetNumMaterials();
	CloudSpeedMIDs.SetNum(NumSlots);
	CloudSpeedBase.SetNum(NumSlots);
	for (int32 Slot = 0; Slot < NumSlots; ++Slot)
	{
		TObjectPtr<UMaterialInstanceDynamic>& MID = CloudSpeedMIDs[Slot];
		if (!MID)
		{
			// Ключ 0 и слот ещё не трогали — материал как был (прежнее поведение бит в бит).
			if (Factor == 1.f) { continue; }
			UMaterialInterface* Current = Dome->GetMaterial(Slot);
			float BaseBoost = 0.f;   // ScrollBoost карты до множителя
			// Слот без ScrollBoost (лента горизонта M_AsdaSkyHorizon не скроллится) — пропуск.
			if (!Current || !Current->GetScalarParameterValue(FHashedMaterialParameterInfo(NScrollBoost), BaseBoost)) { continue; }
			CloudSpeedBase[Slot] = BaseBoost;
			// ★ Уже MID (сохранён в уровне: silalis, rein, nightvalley, alpen_dungeon …) — правим его самого. MID поверх MID
			//   НЕЛЬЗЯ: движок не берёт MID в родители и рисует такой материал материалом по умолчанию
			//   (MaterialInstance.cpp:1043 «Don't allow MIDs as parents») — небо стало бы серой сеткой (замер 29.10).
			if (UMaterialInstanceDynamic* LevelMID = Cast<UMaterialInstanceDynamic>(Current))
			{
				MID = LevelMID;
			}
			else
			{
				MID = UMaterialInstanceDynamic::Create(Current, this);
				Dome->SetMaterial(Slot, MID);
			}
		}
		MID->SetScalarParameterValue(NScrollBoost, CloudSpeedBase[Slot] * Factor);
	}
}

void AAsdaSkyDomeActor::Tick(float DeltaSeconds)
{
	Super::Tick(DeltaSeconds);
	if (!bFollowCamera) { return; }
	FVector View;
	if (AsdaView::GetViewLocation(GetWorld(), View))
	{
		// Оригинал: трансляция world-матрицы узла = ПОЛНАЯ позиция камеры (X,Y,Z) — лента горизонта
		// всегда на уровне глаз (gridCell_InitData @0xF2F690).
		SetActorLocation(View);
	}
}

// ★ волна 3 п.11 (15.09.2026): своя копия AAsdaSkyDomeActor::GetViewLocation СНЯТА — купол следует за
//   AsdaView::GetViewLocation. ★ ИСПРАВЛЕНИЕ E7: копия брала в окне редактора первый перспективный вьюпорт уровня БЕЗ
//   проверки мира, а в игровом мире без контроллера (первые кадры PIE) — вьюпорт редактора (другой мир). Последствия:
//   купол превью-сцены следует за своим видом; в PIE до появления контроллера — за отрисованным видом PIE (или стоит),
//   а не прыгает к камере редактора. В игре с камерой и в окне редактора уровня — без изменений.
