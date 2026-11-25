// Copyright (c) 2026 @sitxovski (Discord)
// Реализация UAsdaFishZoneSubsystem — см. шапку `Public/World/AsdaFishZoneSubsystem.h` (волна В4 рыбалки, 08.10.2026).
#include "World/AsdaFishZoneSubsystem.h"
#include "AsdaPerf.h"   // ★ R4-c (14.11.2026): области замера времени игрового потока
#include "AsdaDevConsole.h"   // ★ античит А2: ASDA_DEV_CVAR / ASDA_DEV_CONSOLE — дев-ключи и команды вне Shipping
#include "World/AsdaFishingSubsystem.h"
#include "World/AsdaWorldSubsystem.h"
#include "World/AsdaLoadManagerSubsystem.h"
#include "World/AsdaPlayerController.h"
#include "World/AsdaGroundTrace.h"
#include "World/AsdaCoord.h"
#include "UI/AsdaMinimapSubsystem.h"
#include "Resources/AsdaAssetPaths.h"
#include "Resources/AsdaModelAnimSubsystem.h"
#include "Components/SceneComponent.h"
#include "Components/SkeletalMeshComponent.h"
#include "Engine/SkeletalMesh.h"
#include "Engine/StaticMesh.h"
#include "StaticMeshResources.h"           // FStaticMeshRenderData / LOD (проба воды)
#include "Engine/StaticMeshActor.h"
#include "Components/StaticMeshComponent.h"
#include "Components/InstancedStaticMeshComponent.h"   // ★ В45fix2 (МЕЛК-4): соперник тип 4 — только экземпляры объектов карты
#include "Animation/AnimSequence.h"
#include "Engine/World.h"
#include "Engine/Engine.h"
#include "Engine/GameInstance.h"
#include "Engine/GameViewportClient.h"
#include "Framework/Application/SlateApplication.h"
#include "Framework/Application/IInputProcessor.h"
#include "Widgets/Input/SEditableText.h"
#include "Widgets/Text/SMultiLineEditableText.h"   // ★ ревизия В4 МЕЛК-2: многострочные поля тоже запираются
#include "Layout/WidgetPath.h"                      // ★ ревизия В4 МЕЛК-9: виджет фокуса — внутри игрового вьюпорта?
#include "Widgets/SViewport.h"
#include "HAL/IConsoleManager.h"
#include "InputKeyEventArgs.h"

DEFINE_LOG_CATEGORY_STATIC(LogAsdaFishZone, Log, All);

// ═════════════════════ КЛЮЧИ ═════════════════════
// [ПОРТ] Гейт окон при рыбалке (эталон `MainWndProc` g:24144: пока fishState ≠ 0, WM_KEYDOWN не уходит ни в один менеджер
//   окон — поле ввода чата клавиш не получает). В порту поле UMG стоит в цепочке Slate РАНЬШЕ контроллера, поэтому гейт —
//   Slate-препроцессор. 1 = как эталон (по умолчанию), 0 = поле ввода получает клавиши и во время рыбалки (прежний порт).
static ASDA_DEV_CVAR(int32, CVarFishKeyGuard,
	TEXT("asda.Fish.KeyGuard"), 1,
	TEXT("1 = пока идёт рыбалка, клавиши не уходят в окна/поле ввода чата (эталон MainWndProc g:24144); 0 = прежний порт."),
	ECVF_Default);

// ═════════════════════ АКТОР ЗОНЫ ═════════════════════
AAsdaFishZoneActor::AAsdaFishZoneActor()
{
	PrimaryActorTick.bCanEverTick = false;   // клип гонит сам скелетный компонент
	Root = CreateDefaultSubobject<USceneComponent>(TEXT("Root"));
	SetRootComponent(Root);
	MeshRoot = CreateDefaultSubobject<USceneComponent>(TEXT("MeshRoot"));
	MeshRoot->SetupAttachment(Root);
	// Тот же масштаб/зеркало, что у персонажей, мобов и питомцев (glTF-метры → см, компенсация хиральности).
	MeshRoot->SetRelativeScale3D(FVector(0.01f, -0.01f, 0.01f));
	Mesh = CreateDefaultSubobject<USkeletalMeshComponent>(TEXT("Mesh"));
	Mesh->SetupAttachment(MeshRoot);
	Mesh->SetAnimationMode(EAnimationMode::AnimationSingleNode);
	// Пикинг — аналитический OBB эталона; меш в трейсы видимости не попадает (иначе закрывал бы землю одиночному клику).
	Mesh->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	Mesh->SetGenerateOverlapEvents(false);
	// Время клипа идёт всегда (эталон `sceneList_UpdateAndRender` крутит клип каждый кадр); кости обновляются только в кадре.
	Mesh->VisibilityBasedAnimTickOption = EVisibilityBasedAnimTickOption::AlwaysTickPose;
}

// ═════════════════════ ПРЕПРОЦЕССОР КЛАВИШ ═════════════════════
// Пока идёт рыбалка и клавиатурный фокус НЕ у игрового вьюпорта (поле ввода чата, любое окно с фокусом), событие не
//   отдаётся окну. Нажатие (и автоповтор) пересылается контроллеру — туда, куда эталон отдаёт его первым
//   (`sub_91F03B` → `Input_FishingKeyFilter`, ответ мини-игры), а контроллер при рыбалке его сам и съедает.
//   Esc/Enter/PrintScreen проходят обычным путём (их WM_KEYUP эталон пропускает, g:24218).
//   ★ Ревизия В4 ВАЖН-3 (09.10.2026): отпускание Alt гасит признак Alt фильтра мини-игры (`OnFishGuardAltReleased`) — прежде
//   нажатие сюда доходило, а отпускание только поглощалось, и признак застревал (все ответы state 4 считались Alt-сочетанием).
//   ★ Ревизия В4 МЕЛК-9: только для виджетов ИГРЫ (внутри игрового вьюпорта) — окна редактора в PIE не трогаются.
class FAsdaFishKeyGuard : public IInputProcessor
{
public:
	TWeakObjectPtr<UAsdaFishZoneSubsystem> Owner;   // владелец
	virtual void Tick(const float, FSlateApplication&, TSharedRef<ICursor>) override {}
	virtual bool HandleKeyDownEvent(FSlateApplication&, const FKeyEvent& Ev) override
	{
		UAsdaFishZoneSubsystem* O = Owner.Get();
		return O && O->GuardKey(Ev, /*bDown*/true);
	}
	virtual bool HandleKeyUpEvent(FSlateApplication&, const FKeyEvent& Ev) override
	{
		UAsdaFishZoneSubsystem* O = Owner.Get();
		return O && O->GuardKey(Ev, /*bDown*/false);
	}
	virtual const TCHAR* GetDebugName() const override { return TEXT("AsdaFishKeyGuard"); }
};

// ═════════════════════ ЖИЗНЕННЫЙ ЦИКЛ ═════════════════════
void UAsdaFishZoneSubsystem::Initialize(FSubsystemCollectionBase& Collection)
{
	Super::Initialize(Collection);
	Collection.InitializeDependency(UAsdaFishingSubsystem::StaticClass());
	if (FSlateApplication::IsInitialized())
	{
		KeyGuard = MakeShared<FAsdaFishKeyGuard>();
		KeyGuard->Owner = this;
		FSlateApplication::Get().RegisterInputPreProcessor(KeyGuard);
	}
}

void UAsdaFishZoneSubsystem::Deinitialize()
{
	ReleaseTextFieldLock();
	if (KeyGuard.IsValid() && FSlateApplication::IsInitialized())
	{
		FSlateApplication::Get().UnregisterInputPreProcessor(KeyGuard);
	}
	KeyGuard.Reset();
	DestroyZones();
	Super::Deinitialize();
}

void UAsdaFishZoneSubsystem::Tick(float /*DeltaTime*/)
{
	ASDA_PERF_SCOPE("Tick.FishZone");   // ★ R4-c (14.11.2026): область замера (AsdaPerf.h; в Shipping пусто)
	ReconcileZones();
	EnsureMapMarks();
	TickTextFieldLock();
}

// ★ В3 S7 (N14): своей реализации «идёт рыбалка» нет — одна точка подсистемы рыбалки.
bool UAsdaFishZoneSubsystem::IsFishingNow() const
{
	return UAsdaFishingSubsystem::IsFishingNow(GetGameInstance());
}

// ═════════════════════ ЗОНЫ ═════════════════════
// Вход на карту = мир уровня текущей карты открыт и в нём есть свой персонаж (эталон зовёт
//   `worldMapPlaceMarkersByType(getField3036(&g_pLocalPlayer))` из входа на карту — g:11540 первый вход, g:11958 портал).
void UAsdaFishZoneSubsystem::ReconcileZones()
{
	UGameInstance* GI = GetGameInstance();
	UWorld* W = GI ? GI->GetWorld() : nullptr;
	if (!W || !W->IsGameWorld())
	{
		if (ZonesWorld.IsValid() || Zones.Num() > 0) { DestroyZones(); }
		return;
	}
	if (ZonesWorld.IsValid() && ZonesWorld.Get() != W) { DestroyZones(); }   // прежний мир уже снесён травелом
	const UAsdaLoadManagerSubsystem* LM = GI->GetSubsystem<UAsdaLoadManagerSubsystem>();
	const int32 MapId = LM ? LM->CurrentMapId : -1;
	if (MapId < 0) { return; }
	// Уровень мира — уровень ЭТОЙ карты (между `BeginMapLoad` и травелом номер карты уже новый, а мир — старый).
	const FString Dir = LM->GetMapDirName(MapId);
	const FString WorldMap = UWorld::RemovePIEPrefix(W->GetMapName());
	if (Dir.IsEmpty() || !WorldMap.Equals(Dir, ESearchCase::IgnoreCase)) { return; }
	// Свой персонаж в мире (как `UAsdaFishingSubsystem::SelfChar`: реестр мира, офлайн — павн контроллера).
	const UAsdaWorldSubsystem* WS = GI->GetSubsystem<UAsdaWorldSubsystem>();
	const AActor* Self = (WS && IsValid(WS->LocalPlayerActor)) ? WS->LocalPlayerActor.Get() : nullptr;
	if (!Self)
	{
		const APlayerController* PC = GI->GetFirstLocalPlayerController();
		Self = PC ? PC->GetPawn() : nullptr;
	}
	if (!Self || Self->GetWorld() != W) { return; }
	if (ZonesWorld.Get() == W && ZonesMapId == MapId) { return; }
	DestroyZones();
	SpawnZones(W, MapId);
}

// Консоль/самотест: пересоздать зоны на следующем кадре; кэш «воды нет» сбрасывается (ревизия В4 МЕЛК-4б).
void UAsdaFishZoneSubsystem::ForceRebuild()
{
	DestroyZones();
	AsdaGround::ResetWaterCache();
}

// `mapReleaseAllSceneObjects(g_pMap+0x140)`.
void UAsdaFishZoneSubsystem::DestroyZones()
{
	for (const TWeakObjectPtr<AAsdaFishZoneActor>& Z : Zones)
	{
		if (AAsdaFishZoneActor* A = Z.Get()) { A->Destroy(); }
	}
	Zones.Reset();
	ZonesWorld.Reset();
	ZonesMapId = -1;
}

// `worldMapPlaceMarkersByType@0xFC3340` (g:603093-603132).
void UAsdaFishZoneSubsystem::SpawnZones(UWorld* W, int32 MapId)
{
	UGameInstance* GI = GetGameInstance();
	UAsdaFishingSubsystem* F = GI ? GI->GetSubsystem<UAsdaFishingSubsystem>() : nullptr;
	if (!F || !W) { return; }
	ZonesWorld = W;
	ZonesMapId = MapId;
	TArray<int32> Idx;
	F->GetSpotsOfMap(MapId, Idx);   // порядок таблицы = порядок вставки в список эталона
	// `CMap_LoadFishingZoneMesh@0xFC5BE0`: `fs_zone.jbm` + `fs_zone.jbb` (Fs_zone_00 из Fish_area_data клиент не читает).
	USkeletalMesh* ZoneMesh = UAsdaModelAnimSubsystem::LoadMeshChecked(
		UAsdaModelAnimSubsystem::BuildSkelPath(AsdaAssetPaths::kSkelRootFishing, TEXT("fs_zone")));
	UAnimSequence* ZoneClip = UAsdaModelAnimSubsystem::LoadClipChecked(
		UAsdaModelAnimSubsystem::BuildSkelAnimPath(AsdaAssetPaths::kSkelRootFishing, TEXT("fs_zone"), 0));
	if ((!ZoneMesh || !ZoneClip) && !bMeshMissingWarned)
	{
		bMeshMissingWarned = true;
		UE_LOG(LogAsdaFishZone, Error, TEXT("[зоны рыбалки] нет ассета %s/fs_zone (меш %s, клип %s) — зоны без модели (пикинг работает)"),
			AsdaAssetPaths::kSkelRootFishing, ZoneMesh ? TEXT("есть") : TEXT("НЕТ"), ZoneClip ? TEXT("есть") : TEXT("НЕТ"));
	}
	int32 NWater = 0;
	for (const int32 i : Idx)
	{
		const FAsdaFishSpotRow* S = F->GetSpot(i);
		if (!S) { continue; }
		// Позиция (X·100, h, Y·100); h — высота воды в точке (`sub_926A07`), нет воды — 0 (Vector3_Set(v5, x, 0, z)).
		const float GX = (float)S->X * kTileUnits;
		const float GZ = (float)S->Y * kTileUnits;
		const FVector2D Ue2 = AsdaCoord::GameToUe2D(FVector2D(GX, GZ));
		const float WaterZ = AsdaGround::WaterSurfaceZ(W, (float)Ue2.X, (float)Ue2.Y);
		const bool bWater = FMath::IsFinite(WaterZ);
		const float H = bWater ? WaterZ : 0.f;
		NWater += bWater ? 1 : 0;
		const FVector CenterGame(GX, H, GZ);
		FActorSpawnParameters SP;
		SP.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
		// Единичная матрица эталона: курс +90° при узле (0.01, −0.01, 0.01) даёт вершине (x, y, z) мировую (−z, x, y).
		AAsdaFishZoneActor* A = W->SpawnActor<AAsdaFishZoneActor>(AAsdaFishZoneActor::StaticClass(),
			AsdaCoord::GameToUe3D(CenterGame), FRotator(0.f, 90.f, 0.f), SP);
		if (!A) { continue; }
		A->SpotIdx = i;
		A->CenterGame = CenterGame;
		// OBB `sub_B5B030`: полуразмеры (2·r·100, 100, 2·r·100) (g:604434-604438). Меш радиусом НЕ масштабируется.
		A->HalfGame = FVector(2.f * (float)S->Radius * kTileUnits, kObbHalfY, 2.f * (float)S->Radius * kTileUnits);
		A->bOverWater = bWater;
#if WITH_EDITOR
		A->SetActorLabel(FString::Printf(TEXT("FishZone_%d_slot%d"), MapId, S->Slot));
#endif
		if (ZoneMesh)
		{
			A->Mesh->SetSkeletalMesh(ZoneMesh);
			if (ZoneClip) { A->Mesh->PlayAnimation(ZoneClip, /*bLooping=*/true); }   // `sceneList_UpdateAndRender` — петля
		}
		Zones.Add(A);
	}
	UE_LOG(LogAsdaFishZone, Log, TEXT("[зоны рыбалки] карта %d: создано %d зон (над водой %d), меш %s"),
		MapId, Zones.Num(), NWater, ZoneMesh ? TEXT("fs_zone") : TEXT("НЕТ"));
}

// ═════════════════════ ПИКИНГ ═════════════════════
// `collide_rayVsObb6FacesHit@0x1199C20`: шесть граней в порядке makePlaneFromBounds (X−), boxBuildRightFacePlane (X+),
//   boxBuildTopFacePlane (Y+), boxBuildBottomFacePlane (Y−), boxBuildFrontFacePlane (Z+), mathPlaneFromObjectEdge (Z−);
//   для каждой `mathRayPlaneIntersect@0x112FF70` (параллель → нет; режим 1 — t ≥ 0) и проверка двух других осей
//   включительно; ПЕРВАЯ подошедшая грань даёт ответ.
bool UAsdaFishZoneSubsystem::RayVsObb6FacesHit(const FVector& O, const FVector& D, const FVector& C, const FVector& Hf, float& OutT)
{
	struct FFace { int32 Axis; float Sign; };
	static const FFace kFaces[6] = { {0, -1.f}, {0, 1.f}, {1, 1.f}, {1, -1.f}, {2, 1.f}, {2, -1.f} };
	for (const FFace& Fc : kFaces)
	{
		const int32 A = Fc.Axis;
		const double Dn = D[A];
		if (FMath::IsNearlyZero(Dn)) { continue; }   // `sub_91E951` — параллельно плоскости
		const double Plane = C[A] + Fc.Sign * Hf[A];
		const double T = (Plane - O[A]) / Dn;
		if (T < 0.0) { continue; }                    // режим луча (a4 = 1)
		const FVector P = O + D * T;
		bool bIn = true;
		for (int32 B = 0; B < 3; ++B)
		{
			if (B == A) { continue; }
			if (P[B] > C[B] + Hf[B] || P[B] < C[B] - Hf[B]) { bIn = false; break; }
		}
		if (bIn) { OutT = (float)T; return true; }
	}
	return false;
}

// `pickClosestFromList@0xFC6360` (g:604629): ближайшая; при равенстве — первая в списке (`v22 > v21` строго).
int32 UAsdaFishZoneSubsystem::PickZoneByRay(const FVector& UeOrigin, const FVector& UeDir, float& OutDist) const
{
	const FVector OG = AsdaCoord::UeToGame3D(UeOrigin);
	const FVector DG = AsdaCoord::UeToGame3D(UeDir).GetSafeNormal();   // поворот осей — длина сохраняется
	int32 Best = -1;
	float BestT = -1.f;
	for (const TWeakObjectPtr<AAsdaFishZoneActor>& Z : Zones)
	{
		const AAsdaFishZoneActor* A = Z.Get();
		if (!A) { continue; }
		float T = 0.f;
		if (!RayVsObb6FacesHit(OG, DG, A->CenterGame, A->HalfGame, T)) { continue; }
		if (BestT < 0.f || BestT > T) { BestT = T; Best = A->SpotIdx; }
	}
	if (Best >= 0) { OutDist = BestT; }
	return Best;
}

// Тип 4 эталона — объекты карты (`collide_rayVsMapObjects`). Террейн, вода и небо карты (`SM_terrain*`, `SM_water`, `SM_sky`)
//   в пикинг эталона не входят вовсе.
//   ★ Ревизия В4 ВАЖН-2 (09.10.2026): решение — по КОМПОНЕНТУ попадания. Объекты карты в порту — экземпляры ISM на простом
//   `AActor` «AsdaMapObjects_<карта>» (`Tools/asda_build_map_level.py`: `spawn_actor_from_class(unreal.Actor)` +
//   `InstancedStaticMeshComponent` на модель; так же их классифицирует `AsdaCullDistanceSubsystem`), а террейн/вода/небо —
//   `AStaticMeshActor` с одним мешем на карту. Прежняя проверка класса актора (`AStaticMeshActor`) ни одного объекта карты не
//   видела — зона выбиралась сквозь дома, деревья и мосты.
//   ★ В45fix2 (ревизия В4/В5, МЕЛК-4, 09.10.2026): соперник сужен до объектов карты. Прежде годился ЛЮБОЙ
//   `UStaticMeshComponent` с мешем (кроме террейна/воды/неба) — в том числе меши системных акторов, классифицируемых пикингом
//   как «земля» (таблички лавок, объекты войны лагерей и т.п.), а у эталона тип 4 — только `collide_rayVsMapObjects`.
//   Теперь требуется: компонент — экземпляры (ISM/HISM, как ставит сборщик уровня и как их узнаёт `AsdaCullDistanceSubsystem`),
//   держатель — ПРОСТОЙ `AActor` (сборщик: `spawn_actor_from_class(unreal.Actor)` с меткой «AsdaMapObjects_<карта>»; метка
//   в собранной игре недоступна, поэтому проверяется класс держателя, а не имя). Иные акторы — отдельным решением.
bool UAsdaFishZoneSubsystem::IsMapObjectHit(const UPrimitiveComponent* HitComp)
{
	const UInstancedStaticMeshComponent* C = Cast<UInstancedStaticMeshComponent>(HitComp);   // HISM — наследник ISM
	const UStaticMesh* SM = C ? C->GetStaticMesh() : nullptr;
	if (!SM) { return false; }
	const AActor* Holder = C->GetOwner();
	if (!Holder || Holder->GetClass() != AActor::StaticClass()) { return false; }
	const FString N = SM->GetName();
	return !(N.StartsWith(TEXT("SM_terrain")) || N.StartsWith(TEXT("SM_water")) || N.StartsWith(TEXT("SM_sky")));
}

// ★ [ПОРТ] 12.10.2026 — отступление от эталона ПО КЛЮЧУ `asda.Fish.ZonePickWaterOnly` (разбор — шапка .h, отчёт
//   `owner_1012/fishzone/fishzone_fix.md`). Эталон террейн в пикинге не видит и предлагает рыбалку по двойному клику на суше
//   в ±2r тайлов от спота; здесь — «точка клика на воде»: либо луч упёрся в меш воды (`SM_water`, если у него есть коллизия),
//   либо над точкой есть плоскость воды `.WAT` (`AsdaGround::WaterSurfaceZ`, тот же запрос, что высота зоны у эталона) и точка
//   не выше её поверхности (дно под водой; берег выше воды — не вода). Нет воды над точкой (NaN) — не вода.
bool UAsdaFishZoneSubsystem::IsWaterHit(UWorld* W, const UPrimitiveComponent* HitComp, const FVector& UePoint)
{
	if (const UStaticMeshComponent* SMC = Cast<UStaticMeshComponent>(HitComp))
	{
		const UStaticMesh* SM = SMC->GetStaticMesh();
		if (SM && SM->GetName().StartsWith(TEXT("SM_water"))) { return true; }
	}
	if (!W) { return false; }
	const float WaterZ = AsdaGround::WaterSurfaceZ(W, (float)UePoint.X, (float)UePoint.Y);
	if (!FMath::IsFinite(WaterZ)) { return false; }
	return (float)UePoint.Z <= WaterZ + kWaterHitTolerance;
}

// ═════════════════════ МЕТКИ КАРТЫ ═════════════════════
// `minimapDrawMarkerList@0xB56330`: перебор ВСЕЙ таблицы спотов с фильтром `+0 == mapId` (миникарта — текущая карта,
//   большая карта — показываемая) — поэтому в общий список кладутся все споты со своим MapId; фильтр делает отрисовка.
void UAsdaFishZoneSubsystem::EnsureMapMarks()
{
	UGameInstance* GI = GetGameInstance();
	UAsdaMinimapSubsystem* MM = GI ? GI->GetSubsystem<UAsdaMinimapSubsystem>() : nullptr;
	UAsdaFishingSubsystem* F = GI ? GI->GetSubsystem<UAsdaFishingSubsystem>() : nullptr;
	if (!MM || !F) { return; }
	const TArray<FAsdaMapMark>* Cur = MM->FindMarks(EAsdaMapMark::Fishing);
	if (Cur && Cur->Num() == F->GetSpotNum()) { return; }
	TArray<FAsdaMapMark> Out;
	for (int32 i = 0; i < F->GetSpotNum(); ++i)
	{
		const FAsdaFishSpotRow* S = F->GetSpot(i);
		if (!S) { continue; }
		FAsdaMapMark M;
		M.TileX = S->X; M.TileY = S->Y; M.MapId = S->MapId; M.Kind = EAsdaMapMark::Fishing;
		Out.Add(M);
	}
	if (Out.Num() == 0) { return; }
	MM->SetMarks(EAsdaMapMark::Fishing, Out);
}

// ★ Ревизия В4 МЕЛК-9 (09.10.2026): виджет — часть игрового UI (лежит внутри игрового `SViewport`: UMG-окна порта
//   добавляются в оверлей вьюпорта). Окна/панели редактора в PIE и прочие окна приложения — нет.
bool UAsdaFishZoneSubsystem::IsGameUiWidget(const TSharedPtr<SWidget>& W)
{
	if (!W.IsValid() || !FSlateApplication::IsInitialized()) { return false; }
	const TSharedPtr<SViewport> Vp = (GEngine && GEngine->GameViewport) ? GEngine->GameViewport->GetGameViewportWidget() : nullptr;
	if (!Vp.IsValid()) { return false; }
	if (W == StaticCastSharedPtr<SWidget>(Vp)) { return true; }
	FWidgetPath Path;
	if (!FSlateApplication::Get().FindPathToWidget(W.ToSharedRef(), Path)) { return false; }
	return Path.ContainsWidget(Vp.Get());
}

// ═════════════════════ ГЕЙТ ОКОН ПРИ РЫБАЛКЕ ═════════════════════
bool UAsdaFishZoneSubsystem::GuardKey(const FKeyEvent& Ev, bool bDown)
{
	if (CVarFishKeyGuard.GetValueOnGameThread() == 0 || !IsFishingNow()) { return false; }
	const FKey K = Ev.GetKey();
	if (!K.IsValid() || K.IsMouseButton() || K.IsGamepadKey() || K.IsTouch()) { return false; }
	// Их KEYUP эталон пропускает окнам (g:24218) — обычный путь Slate (PrintScreen у UE — платформенный опрос, не событие).
	if (K == EKeys::Escape || K == EKeys::Enter) { return false; }
	if (!FSlateApplication::IsInitialized()) { return false; }
	FSlateApplication& App = FSlateApplication::Get();
	const TSharedPtr<SWidget> Focus = App.GetUserFocusedWidget(Ev.GetUserIndex());
	const TSharedPtr<SViewport> Vp = (GEngine && GEngine->GameViewport) ? GEngine->GameViewport->GetGameViewportWidget() : nullptr;
	// Фокус у игрового вьюпорта (или ни у кого) — событие и так придёт контроллеру, где его разбирает фильтр В2.
	if (!Focus.IsValid() || (Vp.IsValid() && Focus == StaticCastSharedPtr<SWidget>(Vp))) { return false; }
	// ★ Ревизия В4 МЕЛК-9: фокус вне игрового UI (панель редактора в PIE) — не трогаем.
	if (!IsGameUiWidget(Focus)) { return false; }
	UGameInstance* GI = GetGameInstance();
	AAsdaPlayerController* PC = GI ? Cast<AAsdaPlayerController>(GI->GetFirstLocalPlayerController()) : nullptr;
	const bool bAltKey = (K == EKeys::LeftAlt || K == EKeys::RightAlt);
	// ★ Ревизия В4 МЕЛК-6: `asda.Fish.EatSysKeys 0` — Alt-сочетания и F10 (WM_SYSKEYDOWN) эталон не глушит: окну отдаются
	//   (отпускание Alt всё равно гасит признак Alt фильтра мини-игры). ★ В3 S7 (N14): одно правило `ShouldPassFishSysKey`.
	if (AAsdaPlayerController::ShouldPassFishSysKey(K, Ev.IsAltDown() || (PC && PC->IsFishAltHeldForCheck())))
	{
		if (!bDown && bAltKey && PC) { PC->OnFishGuardAltReleased(); }
		return false;
	}
	++KeyGuardEaten;
	if (bDown)
	{
		// Нажатие — туда, куда эталон отдаёт его первым: фильтр мини-игры контроллера (`Input_FishingKeyFilter`).
		if (PC)
		{
			PC->InputKey(FInputKeyEventArgs::CreateSimulated(K, Ev.IsRepeat() ? IE_Repeat : IE_Pressed, 1.f));
		}
	}
	else if (bAltKey && PC)
	{
		// ★ Ревизия В4 ВАЖН-3: отпускание Alt гасит признак Alt фильтра мини-игры (нажатие сюда пришло — значит и отпускание
		//   обязано его снять; иначе ответы state 4 навсегда засчитывались бы как Alt-сочетания).
		PC->OnFishGuardAltReleased();
	}
	UE_LOG(LogAsdaFishZone, Verbose, TEXT("[рыбалка] клавиша %s (%s) не отдана окну с фокусом %s (MainWndProc g:24144)"),
		*K.ToString(), bDown ? TEXT("нажатие") : TEXT("отпускание"), *Focus->GetTypeAsString());
	return true;
}

// Символы (`OnKeyChar`) Slate-препроцессор не видит — на время рыбалки поле ввода с фокусом ставится «только чтение»
//   (эталон: при fishState ≠ 0 поле не получает клавиш вовсе), по окончании возвращается прежний признак.
//   ★ Ревизия В4 МЕЛК-2 (09.10.2026): однострочное `SEditableText` И многострочное `SMultiLineEditableText` (у обоих
//   `SetIsReadOnly`/`IsTextReadOnly`; многострочное в порту — `UMultiLineEditableText`, окно соулмейта). ★ МЕЛК-9: только поле
//   игрового UI (внутри игрового вьюпорта) — поля редактора в PIE не запираются.
void UAsdaFishZoneSubsystem::TickTextFieldLock()
{
	const bool bWant = CVarFishKeyGuard.GetValueOnGameThread() != 0 && IsFishingNow() && FSlateApplication::IsInitialized();
	TSharedPtr<SWidget> Cur = LockedEdit.Pin();
	if (!bWant) { ReleaseTextFieldLock(); NotGameFocus.Reset(); return; }
	const TSharedPtr<SWidget> Focus = FSlateApplication::Get().GetUserFocusedWidget(0);
	const FName T = Focus.IsValid() ? Focus->GetType() : NAME_None;
	const bool bSingle = (T == FName(TEXT("SEditableText")));
	const bool bMulti = (T == FName(TEXT("SMultiLineEditableText")));
	if (!bSingle && !bMulti)
	{
		ReleaseTextFieldLock();
		return;
	}
	if (Cur.IsValid() && Cur == Focus) { return; }
	// Поле вне игрового UI: поиск пути виджета — один раз на смену фокуса.
	if (NotGameFocus.IsValid() && NotGameFocus.Pin() == Focus) { return; }
	if (!IsGameUiWidget(Focus))
	{
		ReleaseTextFieldLock();
		NotGameFocus = Focus;
		return;
	}
	ReleaseTextFieldLock();
	if (bMulti)
	{
		TSharedPtr<SMultiLineEditableText> Edit = StaticCastSharedPtr<SMultiLineEditableText>(Focus);
		bLockedEditWasReadOnly = Edit->IsTextReadOnly();
		Edit->SetIsReadOnly(true);
	}
	else
	{
		TSharedPtr<SEditableText> Edit = StaticCastSharedPtr<SEditableText>(Focus);
		bLockedEditWasReadOnly = Edit->IsTextReadOnly();
		Edit->SetIsReadOnly(true);
	}
	LockedEdit = Focus;
	bLockedEditMulti = bMulti;
	UE_LOG(LogAsdaFishZone, Log, TEXT("[рыбалка] поле ввода (%s) с фокусом заперто на время рыбалки (MainWndProc g:24144)"),
		bMulti ? TEXT("многострочное") : TEXT("однострочное"));
}

void UAsdaFishZoneSubsystem::ReleaseTextFieldLock()
{
	if (TSharedPtr<SWidget> W = LockedEdit.Pin())
	{
		if (bLockedEditMulti) { StaticCastSharedPtr<SMultiLineEditableText>(W)->SetIsReadOnly(bLockedEditWasReadOnly); }
		else                  { StaticCastSharedPtr<SEditableText>(W)->SetIsReadOnly(bLockedEditWasReadOnly); }
		UE_LOG(LogAsdaFishZone, Log, TEXT("[рыбалка] поле ввода отперто"));
	}
	LockedEdit.Reset();
	bLockedEditWasReadOnly = false;
	bLockedEditMulti = false;
}

// ═════════════════════ ДИАГНОСТИКА ═════════════════════
FString UAsdaFishZoneSubsystem::DumpZones() const
{
	FString S = FString::Printf(TEXT("[зоны рыбалки] карта %d, зон %d, клавиш поглощено %d, поле заперто %d\n"),
		ZonesMapId, Zones.Num(), KeyGuardEaten, IsTextFieldLocked() ? 1 : 0);
	for (const TWeakObjectPtr<AAsdaFishZoneActor>& Z : Zones)
	{
		const AAsdaFishZoneActor* A = Z.Get();
		if (!A) { S += TEXT("  (снесена)\n"); continue; }
		const FVector L = A->GetActorLocation();
		S += FString::Printf(TEXT("  спот %d: игра (%.0f, %.0f, %.0f) ½ (%.0f, %.0f, %.0f) вода %d → UE (%.0f, %.0f, %.0f), меш %s\n"),
			A->SpotIdx, A->CenterGame.X, A->CenterGame.Y, A->CenterGame.Z, A->HalfGame.X, A->HalfGame.Y, A->HalfGame.Z,
			A->bOverWater ? 1 : 0, L.X, L.Y, L.Z,
			(A->Mesh && A->Mesh->GetSkeletalMeshAsset()) ? *A->Mesh->GetSkeletalMeshAsset()->GetName() : TEXT("нет"));
	}
	return S;
}

// Диагностика высоты воды в точке спота: компонент воды, его коллизия и трейсы (только журнал).
#if ASDA_DEV_CONSOLE   // ★ античит А2: дев-команда, в Shipping не регистрируется
static FAutoConsoleCommandWithWorldAndArgs GAsdaFishWaterProbe(
	TEXT("asda.Fish.WaterProbe"),
	TEXT("asda.Fish.WaterProbe <тайлX> <тайлY> — диагностика высоты воды в точке: компонент SM_water, коллизия, трейсы"),
	FConsoleCommandWithWorldAndArgsDelegate::CreateLambda([](const TArray<FString>& A, UWorld* W)
	{
		if (!W || A.Num() < 2) { return; }
		const FVector2D G(FCString::Atof(*A[0]) * 100.f, FCString::Atof(*A[1]) * 100.f);
		const FVector2D U = AsdaCoord::GameToUe2D(G);
		UStaticMeshComponent* C = AsdaGround::FindWaterComponent(W);
		FString S = FString::Printf(TEXT("[зоны рыбалки] проба воды (%.0f,%.0f) → UE (%.0f,%.0f): компонент %s"), G.X, G.Y, U.X, U.Y,
			C ? *C->GetPathName() : TEXT("НЕТ"));
		if (C)
		{
			const FBox B = C->Bounds.GetBox();
			S += FString::Printf(TEXT(", коллизия %d, профиль %s, BodySetup %s, тело %d, габарит Z %.0f…%.0f"),
				(int32)C->GetCollisionEnabled(), *C->GetCollisionProfileName().ToString(),
				C->GetBodySetup() ? TEXT("есть") : TEXT("НЕТ"), C->GetBodyInstance() && C->GetBodyInstance()->IsValidBodyInstance() ? 1 : 0,
				B.Min.Z, B.Max.Z);
		}
		S += FString::Printf(TEXT(", WaterSurfaceZ %.1f, TerrainMeshZ %.1f"), AsdaGround::WaterSurfaceZ(W, U.X, U.Y), AsdaGround::TerrainMeshZ(W, U.X, U.Y));
		if (C)
		{
			// Трейсы по компоненту воды в обе стороны и простой/сложной коллизией (односторонность треугольников, нет сложной).
			for (int32 Mode = 0; Mode < 4; ++Mode)
			{
				const bool bUp = (Mode & 1) != 0, bComplex = (Mode & 2) != 0;
				FHitResult Hw;
				FCollisionQueryParams Q(SCENE_QUERY_STAT(AsdaWaterProbe), bComplex);
				const FVector From(U.X, U.Y, bUp ? -1.0e5f : 1.0e5f), To(U.X, U.Y, bUp ? 1.0e5f : -1.0e5f);
				const bool bHit = C->LineTraceComponent(Hw, From, To, Q);
				S += FString::Printf(TEXT(", %s/%s: %s"), bUp ? TEXT("снизу") : TEXT("сверху"), bComplex ? TEXT("сложн") : TEXT("прост"),
					bHit ? *FString::Printf(TEXT("%.1f"), Hw.ImpactPoint.Z) : TEXT("нет"));
			}
			const UStaticMesh* SM = C->GetStaticMesh();
			S += FString::Printf(TEXT(", CPU-доступ %d, LOD0 вершин %d"), SM && SM->bAllowCPUAccess ? 1 : 0,
				SM && SM->GetRenderData() && SM->GetRenderData()->LODResources.Num() ? SM->GetRenderData()->LODResources[0].GetNumVertices() : -1);
		}
		FHitResult H;
		if (W->LineTraceSingleByChannel(H, FVector(U.X, U.Y, 1.0e5f), FVector(U.X, U.Y, -1.0e5f), ECC_Visibility))
		{
			S += FString::Printf(TEXT(", первый по видимости: %s Z %.1f"), H.GetActor() ? *H.GetActor()->GetName() : TEXT("-"), H.ImpactPoint.Z);
		}
		UE_LOG(LogAsdaFishZone, Display, TEXT("%s"), *S);
	}));
#endif // ASDA_DEV_CONSOLE

#if ASDA_DEV_CONSOLE   // ★ античит А2: дев-команда, в Shipping не регистрируется
static FAutoConsoleCommandWithWorldAndArgs GAsdaFishZoneDump(
	TEXT("asda.Fish.ZoneDump"),
	TEXT("asda.Fish.ZoneDump [rebuild] — зоны рыбалки текущей карты (позиции, OBB, вода, меш); rebuild — пересоздать"),
	FConsoleCommandWithWorldAndArgsDelegate::CreateLambda([](const TArray<FString>& A, UWorld* W)
	{
		UGameInstance* GI = W ? W->GetGameInstance() : nullptr;
		UAsdaFishZoneSubsystem* Z = GI ? GI->GetSubsystem<UAsdaFishZoneSubsystem>() : nullptr;
		if (!Z) { return; }
		if (A.Num() > 0 && A[0].Equals(TEXT("rebuild"), ESearchCase::IgnoreCase)) { Z->ForceRebuild(); }
		UE_LOG(LogAsdaFishZone, Display, TEXT("%s"), *Z->DumpZones());
	}));
#endif // ASDA_DEV_CONSOLE
