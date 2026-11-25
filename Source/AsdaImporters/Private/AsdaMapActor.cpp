// Copyright (c) 2026 @sitxovski (Discord)
#include "AsdaMapActor.h"
#include "ProceduralMeshComponent.h"
#include "Resources/AsdaAssetPaths.h"   // ★ волна 2 п. 7 (14.09.2026): реестр путей и правил имён ассетов (генерируемый)
#include "AsdaMapFormats.h"
#include "Resources/AsdaTextureSubsystem.h"
#include "Resources/AsdaSoundSubsystem.h"
#include "Engine/Engine.h"                 // GEngine->GetEngineSubsystem
#include "Engine/GameInstance.h"
#include "Materials/MaterialInterface.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "Engine/Texture2D.h"
#include "Engine/DirectionalLight.h"
#include "Engine/ExponentialHeightFog.h"
#include "Components/DirectionalLightComponent.h"
#include "Components/ExponentialHeightFogComponent.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "HAL/FileManager.h"
#include "UObject/ConstructorHelpers.h"

DEFINE_LOG_CATEGORY_STATIC(LogAsdaMap, Log, All);

// Единый текстур-менеджер (engine-lifetime, доступен и в редакторе). Через него грузятся ВСЕ текстуры карты.
static UAsdaTextureSubsystem* GetTexMgr()
{
	return GEngine ? GEngine->GetEngineSubsystem<UAsdaTextureSubsystem>() : nullptr;
}

void AAsdaMapActor::BeginPlay()
{
	Super::BeginPlay();
	SpawnAmbientSounds();   // рантайм: 3D-эмиттеры звука карты (в редакторе OnConstruction их не трогает)
}

// Заспавнить окружающие 3D-звуки карты (section E). Позиции — через ToUE, радиусы — с WorldScale.
void AAsdaMapActor::SpawnAmbientSounds()
{
	if (MapDir.IsEmpty()) return;
	UGameInstance* GI = GetGameInstance();
	UAsdaSoundSubsystem* SoundMgr = GI ? GI->GetSubsystem<UAsdaSoundSubsystem>() : nullptr;
	if (!SoundMgr) return;

	const asda::MapObjects mo = asda::LoadMapObjects(std::string(TCHAR_TO_UTF8(*MapDir)));
	int32 Spawned = 0;
	for (const asda::MapSound& S : mo.sounds)
	{
		if (S.wav.empty()) continue;
		// Имя wav ("S_OBJ_PROPEL1.WAV") → база ассета; PlaySound3D резолвит /Game/AsdaAssets/Sound/<base>.
		const FString Base = FPaths::GetBaseFilename(UTF8_TO_TCHAR(S.wav.c_str()));
		const FVector Loc = ToUE(S.pos.x, S.pos.y, S.pos.z);
		SoundMgr->PlaySound3D(FName(*Base), Loc, S.minR * WorldScale, S.maxR * WorldScale, /*bLoop*/true);
		++Spawned;
	}
	if (Spawned > 0)
	{
		UE_LOG(LogAsdaMap, Log, TEXT("SpawnAmbientSounds: 3D-эмиттеров=%d (карта %s)"), Spawned, *MapDir);
	}
}

AAsdaMapActor::AAsdaMapActor()
{
	PrimaryActorTick.bCanEverTick = false;
	Mesh = CreateDefaultSubobject<UProceduralMeshComponent>(TEXT("TerrainMesh"));
	RootComponent = Mesh;
	Mesh->bUseAsyncCooking = true;

	// Доп. компоненты геометрии (объекты/вода/небо). Аттач к root.
	ObjMesh = CreateDefaultSubobject<UProceduralMeshComponent>(TEXT("ObjectMesh"));
	ObjMesh->SetupAttachment(RootComponent);
	ObjMesh->bUseAsyncCooking = true;

	WaterMesh = CreateDefaultSubobject<UProceduralMeshComponent>(TEXT("WaterMesh"));
	WaterMesh->SetupAttachment(RootComponent);
	WaterMesh->bUseAsyncCooking = true;

	SkyMesh = CreateDefaultSubobject<UProceduralMeshComponent>(TEXT("SkyMesh"));
	SkyMesh->SetupAttachment(RootComponent);
	SkyMesh->bUseAsyncCooking = true;

	// Производительность: ни один слой не участвует в навигации/коллизии (террейн-коллизия
	// = NavMesh-экспорт сотен тысяч трисов → ~1 FPS). Объекты/вода/небо тоже без коллизии.
	for (UProceduralMeshComponent* C : { Mesh, ObjMesh, WaterMesh, SkyMesh })
	{
		C->SetCanEverAffectNavigation(false);
		C->SetCollisionEnabled(ECollisionEnabled::NoCollision);
		C->SetGenerateOverlapEvents(false);
	}
}

void AAsdaMapActor::OnConstruction(const FTransform& Transform)
{
	Super::OnConstruction(Transform);
	if (MapDir.IsEmpty()) return;
	// ★ Строим ОДИН раз на загрузку / при смене MapDir. OnConstruction вызывается на каждое
	// движение/правку актора — без этого гарда карта перестраивается постоянно → ~1 FPS.
	if (Mesh && Mesh->GetNumSections() > 0 && BuiltMapDir == MapDir)
	{
		return;
	}
	BuildMap();
}

FVector AAsdaMapActor::ToUE(float x, float y, float z) const
{
	// ★ Игра = RH, Y-up. UE = LH, Z-up. Каноническая конверсия RH-Yup→LH-Zup (det=-1):
	//   UE.X = -game.z, UE.Y = game.x, UE.Z = game.y(высота).
	// Эквивалент эмпирического «поворот 90° влево + гориз. флип». bMirrorY (по умолч. true) даёт
	// этот правильный результат; false = доп. зеркало по Y (escape hatch, если карта зеркальна).
	const float ux = -z;
	const float uy = bMirrorY ? x : -x;
	const float uz = y;
	return FVector(ux, uy, uz) * WorldScale;
}

FVector AAsdaMapActor::ToUENormal(float nx, float ny, float nz) const
{
	// Та же перестановка осей, что и ToUE, но без translation/масштаба.
	const float ux = -nz;
	const float uy = bMirrorY ? nx : -nx;
	const float uz = ny;
	return FVector(ux, uy, uz).GetSafeNormal();
}

void AAsdaMapActor::BuildMap()
{
	if (!Mesh) return;
	Mesh->ClearAllMeshSections();

	if (MapDir.IsEmpty())
	{
		UE_LOG(LogAsdaMap, Warning, TEXT("BuildMap: MapDir пуст"));
		return;
	}

	const std::string dir(TCHAR_TO_UTF8(*MapDir));
	asda::PatchTerrain terr = asda::LoadPatchTerrain(dir);
	if (!terr.has || terr.patches.empty())
	{
		UE_LOG(LogAsdaMap, Warning, TEXT("BuildMap: PatchTerrain не загружен из '%s' (has=%d, patches=%d)"),
			*MapDir, terr.has ? 1 : 0, (int)terr.patches.size());
		return;
	}

	// Общий индекс-буфер (локальные индексы патча 0..vtxCount-1).
	TArray<int32> Tris;
	Tris.Reserve((int32)terr.indices.size());
	for (unsigned short v : terr.indices) Tris.Add((int32)v);

	const TArray<FVector> NoNormals;
	const TArray<FProcMeshTangent> NoTangents;

	// --- splat-материал: мастер + массивы тайлов (грузим ОДИН раз) --------------
	if (UAsdaTextureSubsystem* TexMgr = GetTexMgr()) { TexMgr->ClearCache(); } // смена карты → сброс кэша (оба уровня)
	if (!TerrainMaster)
	{
		TerrainMaster = LoadObject<UMaterialInterface>(nullptr, TEXT("/Game/Materials/M_AsdaTerrain"));
		if (!TerrainMaster)
		{
			UE_LOG(LogAsdaMap, Warning, TEXT("BuildMap: мастер-материал /Game/Materials/M_AsdaTerrain не найден — секции без splat"));
		}
	}

	TArray<uint8> ColorArr; // TerrainColor.TEX (массив тайлов-лайтмапов)
	TArray<uint8> AlphaArr; // TerrainAlpha.TEX (массив тайлов-масок splat)
	FFileHelper::LoadFileToArray(ColorArr, *(MapDir / TEXT("TerrainColor.TEX")));
	FFileHelper::LoadFileToArray(AlphaArr, *(MapDir / TEXT("TerrainAlpha.TEX")));

	int32 section = 0;
	int64 totalVerts = 0;
	for (const asda::TerrainPatch& p : terr.patches)
	{
		if (p.vtxCount <= 0) continue;
		TArray<FVector>       Verts;   Verts.Reserve(p.vtxCount);
		TArray<FVector2D>     UV0;     UV0.Reserve(p.vtxCount);
		TArray<FLinearColor>  Colors;  Colors.Reserve(p.vtxCount);

		for (int i = 0; i < p.vtxCount; ++i)
		{
			Verts.Add(ToUE(p.pos[i * 3 + 0], p.pos[i * 3 + 1], p.pos[i * 3 + 2]));
			const float u = (i * 2 + 1 < (int)p.uv0.size()) ? p.uv0[i * 2 + 0] : 0.0f;
			const float v = (i * 2 + 1 < (int)p.uv0.size()) ? p.uv0[i * 2 + 1] : 0.0f;
			UV0.Add(FVector2D(u, v));
			if (i * 4 + 3 < (int)p.col.size())
			{
				Colors.Add(FLinearColor(p.col[i*4+0] / 255.f, p.col[i*4+1] / 255.f, p.col[i*4+2] / 255.f, p.col[i*4+3] / 255.f));
			}
			else
			{
				Colors.Add(FLinearColor::White);
			}
		}

		// collision=false: коллизия террейна на 270K вершин → NavMesh экспорт 524288 трисов = ~1 FPS.
		// Ходячую коллизию добавим отдельно (упрощённую) под персонажа позже.
		Mesh->CreateMeshSection_LinearColor(section, Verts, Tris, NoNormals, UV0, Colors, NoTangents, /*collision*/ false);
		ApplyPatchMaterial(section, p, ColorArr, AlphaArr, MapDir);
		++section;
		totalVerts += p.vtxCount;
	}

	UE_LOG(LogAsdaMap, Log, TEXT("BuildMap '%s': патчей=%d, секций=%d, вершин=%lld, индексов=%d"),
		*MapDir, (int)terr.patches.size(), section, totalVerts, Tris.Num());

	// --- доп. слои карты (ПОСЛЕ террейна, террейн не трогаем) -------------------
	asda::Scene scene;
	ApplyScene(MapDir, scene);   // парсит .SCE + спавнит свет/туман; scene нужна объектам
	BuildObjects(MapDir, scene);
	BuildWater(MapDir);
	BuildSky(MapDir);

	BuiltMapDir = MapDir;        // отметка «построено для этой карты» (анти-ребилд в OnConstruction)
}

void AAsdaMapActor::ApplyPatchMaterial(int32 SectionIndex, const asda::TerrainPatch& Patch,
	const TArray<uint8>& ColorArr, const TArray<uint8>& AlphaArr, const FString& Dir)
{
	if (!TerrainMaster || !Mesh)
	{
		return; // нет мастер-материала — секция остаётся с дефолтным
	}

	// Тайл = вложенный .TEX со своим 0x24-header на байт-смещении colorOff/alphaOff.
	//   Грузим через единый текстур-менеджер (кэш по offset — одинаковые тайлы секций не дублируются).
	UAsdaTextureSubsystem* TexMgr = GetTexMgr();
	UTexture2D* TileColor = nullptr;
	if (TexMgr && ColorArr.Num() > 0 && Patch.colorOff < (uint32)ColorArr.Num())
	{
		TileColor = TexMgr->LoadTexFromMemory(ColorArr.GetData() + Patch.colorOff,
			ColorArr.Num() - (int32)Patch.colorOff,
			FString::Printf(TEXT("tilecolor:%s:%u"), *Dir, Patch.colorOff));
	}
	UTexture2D* TileAlpha = nullptr;
	if (TexMgr && AlphaArr.Num() > 0 && Patch.alphaOff < (uint32)AlphaArr.Num())
	{
		TileAlpha = TexMgr->LoadTexFromMemory(AlphaArr.GetData() + Patch.alphaOff,
			AlphaArr.Num() - (int32)Patch.alphaOff,
			FString::Printf(TEXT("tilealpha:%s:%u"), *Dir, Patch.alphaOff));
	}

	const FString Detail0Name = UTF8_TO_TCHAR(Patch.detail0.c_str());
	const FString Detail1Name = UTF8_TO_TCHAR(Patch.detail1.c_str());
	// Project-first (тайлы импортированы в Maps/<карта>/<TILE>); фолбэк на диск внутри LoadObjTexture.
	UTexture2D* Detail0Tex = LoadObjTexture(Dir, Detail0Name);
	UTexture2D* Detail1Tex = LoadObjTexture(Dir, Detail1Name);

	UMaterialInstanceDynamic* MID = UMaterialInstanceDynamic::Create(TerrainMaster, this);
	if (!MID)
	{
		return;
	}

	// Текстуры: Detail1 падает на Detail0, если оверлея нет; null оставляем —
	// мастер-материал должен иметь дефолтные значения параметров (белая/чёрная).
	if (Detail0Tex) { MID->SetTextureParameterValue(TEXT("Detail0"), Detail0Tex); }
	UTexture2D* D1 = Detail1Tex ? Detail1Tex : Detail0Tex;
	if (D1)        { MID->SetTextureParameterValue(TEXT("Detail1"), D1); }
	if (TileAlpha) { MID->SetTextureParameterValue(TEXT("Mask"), TileAlpha); }
	if (TileColor) { MID->SetTextureParameterValue(TEXT("Colormap"), TileColor); }

	MID->SetScalarParameterValue(TEXT("Scale0"), Patch.scale0);
	MID->SetScalarParameterValue(TEXT("Scale1"), Patch.scale1);
	MID->SetScalarParameterValue(TEXT("HasD1"), Patch.detail1.empty() ? 0.0f : 1.0f);

	Mesh->SetMaterial(SectionIndex, MID);
}

// ===========================================================================
//  Текстуры объектов/воды/неба
// ===========================================================================
UTexture2D* AAsdaMapActor::LoadObjTexture(const FString& Dir, const FString& TexName) const
{
	UAsdaTextureSubsystem* TexMgr = GetTexMgr();
	if (!TexMgr)
	{
		return nullptr;
	}
	// 1) project-first: импортированные ассеты /Game/AsdaAssets/Textures/Maps/<карта>/[Texture/] (с кэшем).
	if (UTexture2D* T = TexMgr->LoadMapTexture(Dir, TexName))
	{
		return T;
	}
	// 2) ★ Фолбэк «сырой .TEX из папки карты» ЖИВЁТ ТЕПЕРЬ ЗДЕСЬ, в EDITOR-модуле, а не в игровом
	//    текстур-менеджере: чтение файлов оригинала допустимо только в инструментах импорта. Декодом
	//    занимается тот же менеджер (LoadTexFromMemory) — путь разбора остаётся единственным.
	const FString BaseName = FPaths::GetBaseFilename(TexName);
	const FString Candidates[] = {
		Dir / TEXT("Texture") / (BaseName + TEXT(".TEX")),
		Dir / (BaseName + TEXT(".TEX")),
		Dir / TexName,
	};
	for (const FString& Path : Candidates)
	{
		TArray<uint8> Bytes;
		if (!FPaths::FileExists(Path) || !FFileHelper::LoadFileToArray(Bytes, *Path) || Bytes.Num() == 0)
		{
			continue;
		}
		if (UTexture2D* T = TexMgr->LoadTexFromMemory(Bytes.GetData(), Bytes.Num(), Path.ToLower()))
		{
			return T;
		}
	}
	return nullptr;
}

// ===========================================================================
//  1) Статические объекты карты (Pos.POS + Pos0.PBB)
// ===========================================================================
void AAsdaMapActor::BuildObjects(const FString& Dir, const asda::Scene& InScene)
{
	if (!ObjMesh)
	{
		return;
	}
	ObjMesh->ClearAllMeshSections();

	const std::string dirU(TCHAR_TO_UTF8(*Dir));
	asda::MapObjects mo = asda::LoadMapObjects(dirU);
	if (!mo.has || mo.instances.empty() || mo.meshes.empty())
	{
		UE_LOG(LogAsdaMap, Log, TEXT("BuildObjects: объектов нет (has=%d, inst=%d, meshes=%d)"),
			mo.has ? 1 : 0, (int)mo.instances.size(), (int)mo.meshes.size());
		return;
	}

	// Мастер-материал объектов (грузим лениво один раз).
	if (!ObjectMaster)
	{
		ObjectMaster = LoadObject<UMaterialInterface>(nullptr, AsdaAssetPaths::kMaterialObject);   // ★ волна 2 п. 7: реестр путей
		if (!ObjectMaster)
		{
			UE_LOG(LogAsdaMap, Warning, TEXT("BuildObjects: мастер /Game/Materials/M_AsdaObject не найден — секции без MID"));
		}
	}

	// Параметры освещения сцены (общие для всех объектов).
	// ★ Материал считает N·L с нормалями в UE-пространстве → вектор света тоже в UE-осях
	//   (та же конверсия, что у нормалей мешей; ревизия review_lighting_sun_and_ambient.md п.7).
	const FVector LightDirUE = ToUENormal(InScene.lightDir.x, InScene.lightDir.y, InScene.lightDir.z);
	const FLinearColor LightDirCol((float)LightDirUE.X, (float)LightDirUE.Y, (float)LightDirUE.Z, 0.0f);
	const float Ambient = (InScene.ambient.x + InScene.ambient.y + InScene.ambient.z) / 3.0f > 0.0f
		? (InScene.ambient.x + InScene.ambient.y + InScene.ambient.z) / 3.0f
		: 0.8f;

	// ★ БАТЧИНГ ПО ТЕКСТУРЕ: вместо секции-на-instance (~20K draw-call'ов + 20K MID = 1 FPS,
	// «Descriptor cache ran out», OOM) склеиваем геометрию ВСЕХ инстансов с одной текстурой в ОДНУ
	// секцию. 20K секций → ~число уникальных текстур (~100). Геометрии мало (420K вершин), узкое место = секции.
	struct FBatch
	{
		TArray<FVector>   Verts;
		TArray<FVector>   Normals;
		TArray<FVector2D> UV0;
		TArray<int32>     Tris;
	};
	TMap<FString, FBatch> Batches;

	for (const asda::ObjInstance& inst : mo.instances)
	{
		if (inst.meshIndex < 0 || inst.meshIndex >= (int)mo.meshes.size())
		{
			continue;
		}
		const asda::PbbMesh& m = mo.meshes[inst.meshIndex];
		if (m.vtxCount <= 0 || m.idx.empty())
		{
			continue;
		}
		const int vtx = m.vtxCount;
		if ((int)m.pos.size() < vtx * 3)
		{
			continue;
		}
		const bool hasNrm = (int)m.nrm.size() >= vtx * 3;
		const bool hasUv  = (int)m.uv.size()  >= vtx * 2;

		const FString TexName = UTF8_TO_TCHAR(m.texture.c_str());
		FBatch& B = Batches.FindOrAdd(TexName);
		const int32 Base = B.Verts.Num();   // смещение индексов для этого инстанса в склеенном буфере

		for (int i = 0; i < vtx; ++i)
		{
			const float gx = m.pos[i * 3 + 0] + inst.pos.x;
			const float gy = m.pos[i * 3 + 1] + inst.pos.y;
			const float gz = m.pos[i * 3 + 2] + inst.pos.z;
			B.Verts.Add(ToUE(gx, gy, gz));
			B.Normals.Add(hasNrm ? ToUENormal(m.nrm[i * 3 + 0], m.nrm[i * 3 + 1], m.nrm[i * 3 + 2]) : FVector::UpVector);
			B.UV0.Add(FVector2D(hasUv ? m.uv[i * 2 + 0] : 0.0f, hasUv ? m.uv[i * 2 + 1] : 0.0f));
		}
		for (unsigned short id : m.idx)
		{
			if ((int)id < vtx) B.Tris.Add(Base + (int32)id);
		}
	}

	const TArray<FProcMeshTangent> NoTangents;
	const TArray<FLinearColor> NoColors;
	int32 section = 0;
	int64 totalVerts = 0;
	for (TPair<FString, FBatch>& Kv : Batches)
	{
		FBatch& B = Kv.Value;
		if (B.Verts.Num() == 0 || B.Tris.Num() < 3)
		{
			continue;
		}
		ObjMesh->CreateMeshSection_LinearColor(section, B.Verts, B.Tris, B.Normals, B.UV0,
			NoColors, NoTangents, /*collision*/ false);
		if (ObjectMaster)
		{
			UMaterialInstanceDynamic* MID = UMaterialInstanceDynamic::Create(ObjectMaster, this);
			if (MID)
			{
				if (UTexture2D* Tex = LoadObjTexture(Dir, Kv.Key))
				{
					MID->SetTextureParameterValue(TEXT("Tex"), Tex);
				}
				MID->SetVectorParameterValue(TEXT("LightDir"), LightDirCol);
				MID->SetScalarParameterValue(TEXT("Ambient"), Ambient);
				ObjMesh->SetMaterial(section, MID);
			}
		}
		++section;
		totalVerts += B.Verts.Num();
	}

	UE_LOG(LogAsdaMap, Log, TEXT("BuildObjects '%s': инстансов=%d, текстур-секций=%d, вершин=%lld"),
		*Dir, (int)mo.instances.size(), section, totalVerts);
}

// ===========================================================================
//  2) Вода (.WAT)
// ===========================================================================
void AAsdaMapActor::BuildWater(const FString& Dir)
{
	if (!WaterMesh)
	{
		return;
	}
	WaterMesh->ClearAllMeshSections();

	// Имя карты = имя папки; основной кандидат <dir>/<folder>.WAT, иначе любой *.WAT.
	const FString Folder = FPaths::GetCleanFilename(FPaths::GetPath(Dir / TEXT("x"))); // имя последней директории
	FString WatPath = Dir / (Folder + TEXT(".WAT"));
	if (!FPaths::FileExists(WatPath))
	{
		TArray<FString> Found;
		IFileManager::Get().FindFiles(Found, *(Dir / TEXT("*.WAT")), /*Files*/ true, /*Dirs*/ false);
		if (Found.Num() > 0)
		{
			WatPath = Dir / Found[0];
		}
		else
		{
			UE_LOG(LogAsdaMap, Log, TEXT("BuildWater: .WAT не найден в '%s'"), *Dir);
			return;
		}
	}

	const std::string watU(TCHAR_TO_UTF8(*WatPath));
	asda::WaterMesh water = asda::LoadWaterMesh(watU);
	if (!water.has || water.bodies.empty())
	{
		UE_LOG(LogAsdaMap, Log, TEXT("BuildWater: вода пуста (has=%d, bodies=%d)"),
			water.has ? 1 : 0, (int)water.bodies.size());
		return;
	}

	if (!WaterMaster)
	{
		WaterMaster = LoadObject<UMaterialInterface>(nullptr, TEXT("/Game/Materials/M_AsdaWater"));
		if (!WaterMaster)
		{
			UE_LOG(LogAsdaMap, Warning, TEXT("BuildWater: мастер /Game/Materials/M_AsdaWater не найден — секции без MID"));
		}
	}

	// Базовая водная текстура (одна на весь водоём).
	const FString WaterTexName = UTF8_TO_TCHAR(water.texName.c_str());
	UTexture2D* WaterTex = LoadObjTexture(Dir, WaterTexName);

	const TArray<FVector> NoNormals;
	const TArray<FProcMeshTangent> NoTangents;
	const TArray<FLinearColor> NoColors;
	int32 section = 0;

	for (const asda::WaterBody& body : water.bodies)
	{
		const int vtx = (int)body.pos.size() / 3;
		if (vtx <= 0 || body.idx.empty())
		{
			continue;
		}
		const bool hasUv = (int)body.uv.size() >= vtx * 2;

		TArray<FVector>   Verts; Verts.Reserve(vtx);
		TArray<FVector2D> UV0;   UV0.Reserve(vtx);
		for (int i = 0; i < vtx; ++i)
		{
			Verts.Add(ToUE(body.pos[i * 3 + 0], body.pos[i * 3 + 1], body.pos[i * 3 + 2]));
			const float u = hasUv ? body.uv[i * 2 + 0] : 0.0f;
			const float v = hasUv ? body.uv[i * 2 + 1] : 0.0f;
			UV0.Add(FVector2D(u, v));
		}

		TArray<int32> Tris;
		Tris.Reserve((int32)body.idx.size());
		for (unsigned short id : body.idx)
		{
			if ((int)id < vtx) Tris.Add((int32)id);
		}
		if (Tris.Num() < 3)
		{
			continue;
		}

		WaterMesh->CreateMeshSection_LinearColor(section, Verts, Tris, NoNormals, UV0,
			NoColors, NoTangents, /*collision*/ false);

		if (WaterMaster)
		{
			UMaterialInstanceDynamic* MID = UMaterialInstanceDynamic::Create(WaterMaster, this);
			if (MID)
			{
				if (WaterTex)
				{
					MID->SetTextureParameterValue(TEXT("BaseTex"), WaterTex);
				}
				MID->SetScalarParameterValue(TEXT("HasBase"), WaterTexName.IsEmpty() ? 0.0f : 1.0f);
				WaterMesh->SetMaterial(section, MID);
			}
		}

		++section;
	}

	UE_LOG(LogAsdaMap, Log, TEXT("BuildWater '%s': bodies=%d, секций=%d, level=%.1f"),
		*WatPath, (int)water.bodies.size(), section, water.level);
}

// ===========================================================================
//  3) Небо (SKY_DOME.JBM)
// ===========================================================================
void AAsdaMapActor::BuildSky(const FString& Dir)
{
	if (!SkyMesh)
	{
		return;
	}
	SkyMesh->ClearAllMeshSections();

	// ★ Небо = БОЛЬШАЯ СФЕРА-небосвод (как в оригинале/MapViewer: сфера вокруг камеры),
	// текстурируется ПРОЦЕДУРНО в материале по направлению из камеры (фишай cloud-cap +
	// azimuth mountain-band). SKY_DOME.JBM — НЕ используется (он только для валидации в вьюере).
	UTexture2D* CloudTex    = LoadObjTexture(Dir, TEXT("SKY_DOME_CLOUDE1")); // фото неба (cap)
	UTexture2D* MountainTex = LoadObjTexture(Dir, TEXT("SKY_DOME"));         // горная полоса (alpha)
	if (!CloudTex && !MountainTex)
	{
		return; // текстур неба нет — пропускаем
	}

	if (!SkyMaster)
	{
		SkyMaster = LoadObject<UMaterialInterface>(nullptr, TEXT("/Game/Materials/M_AsdaSky"));
		if (!SkyMaster)
		{
			UE_LOG(LogAsdaMap, Warning, TEXT("BuildSky: мастер /Game/Materials/M_AsdaSky не найден"));
		}
	}

	// Генерим UV-сферу большого радиуса (canvas). Материал проецирует по реальной CameraPositionWS,
	// поэтому статичная сфера в origin корректна, пока камера внутри.
	const float R = 400000.0f;                 // ~4 км — охватывает карту, в пределах far-плоскости
	const int   Rings = 24, Sectors = 48;
	TArray<FVector>   Verts;  Verts.Reserve((Rings + 1) * (Sectors + 1));
	TArray<FVector2D> UV0;    UV0.Reserve((Rings + 1) * (Sectors + 1));
	for (int r = 0; r <= Rings; ++r)
	{
		const float lat = PI * ((float)r / Rings) - (PI * 0.5f); // -90..+90
		const float cl = FMath::Cos(lat), sl = FMath::Sin(lat);
		for (int s = 0; s <= Sectors; ++s)
		{
			const float lon = 2.0f * PI * ((float)s / Sectors);
			Verts.Add(FVector(R * cl * FMath::Cos(lon), R * cl * FMath::Sin(lon), R * sl)); // Z-up
			UV0.Add(FVector2D((float)s / Sectors, (float)r / Rings));
		}
	}
	const int Stride = Sectors + 1;
	TArray<int32> Tris; Tris.Reserve(Rings * Sectors * 6);
	for (int r = 0; r < Rings; ++r)
	{
		for (int s = 0; s < Sectors; ++s)
		{
			const int a = r * Stride + s;
			const int b = a + Stride;
			Tris.Add(a); Tris.Add(b); Tris.Add(a + 1);
			Tris.Add(a + 1); Tris.Add(b); Tris.Add(b + 1);
		}
	}

	const TArray<FVector> NoNormals;
	const TArray<FProcMeshTangent> NoTangents;
	const TArray<FLinearColor> NoColors;
	SkyMesh->CreateMeshSection_LinearColor(0, Verts, Tris, NoNormals, UV0, NoColors, NoTangents, /*collision*/ false);

	if (SkyMaster)
	{
		UMaterialInstanceDynamic* MID = UMaterialInstanceDynamic::Create(SkyMaster, this);
		if (MID)
		{
			if (CloudTex)    MID->SetTextureParameterValue(TEXT("CloudTex"), CloudTex);
			if (MountainTex) MID->SetTextureParameterValue(TEXT("MountainTex"), MountainTex);
			MID->SetScalarParameterValue(TEXT("SkyScale"), 1.0f);
			MID->SetScalarParameterValue(TEXT("Raise"), -0.05f);
			MID->SetScalarParameterValue(TEXT("Stretch"), 0.35f);
			SkyMesh->SetMaterial(0, MID);
		}
	}
	SkyMesh->SetCastShadow(false); // небо не отбрасывает тени

	UE_LOG(LogAsdaMap, Log, TEXT("BuildSky '%s': сфера R=%.0f, cloud=%d mtn=%d"),
		*Dir, R, CloudTex ? 1 : 0, MountainTex ? 1 : 0);
}

// ===========================================================================
//  4) Сцена: туман + направленный свет
// ===========================================================================
void AAsdaMapActor::ApplyScene(const FString& Dir, asda::Scene& OutScene)
{
	OutScene = asda::Scene{}; // дефолты, если .SCE нет

	// Имя карты = имя последней директории.
	const FString Folder = FPaths::GetCleanFilename(FPaths::GetPath(Dir / TEXT("x")));
	FString ScePath = Dir / (Folder + TEXT(".SCE"));
	if (!FPaths::FileExists(ScePath))
	{
		TArray<FString> Found;
		IFileManager::Get().FindFiles(Found, *(Dir / TEXT("*.SCE")), /*Files*/ true, /*Dirs*/ false);
		if (Found.Num() > 0)
		{
			ScePath = Dir / Found[0];
		}
		else
		{
			UE_LOG(LogAsdaMap, Log, TEXT("ApplyScene: .SCE не найден в '%s' — дефолтная сцена"), *Dir);
			return;
		}
	}

	const std::string sceU(TCHAR_TO_UTF8(*ScePath));
	asda::Scene scene = asda::LoadScene(sceU);
	if (!scene.valid)
	{
		UE_LOG(LogAsdaMap, Log, TEXT("ApplyScene: .SCE не распарсен ('%s')"), *ScePath);
		return;
	}
	OutScene = scene;
	// ★ НИКАКОГО спавна акторов здесь! Спавн света/тумана в OnConstruction грязнит мир →
	// OnConstruction вызывается заново → бесконечная перестройка + накопление = 1 FPS/OOM.
	// Свет даёт DirectionalLight/SkyLight уровня (из импорт-скрипта). Объекты берут lightDir/ambient
	// из OutScene через MID-параметры. Туман/точный свет .SCE настроим отдельно (не в конструкции).
	UE_LOG(LogAsdaMap, Log, TEXT("ApplyScene '%s': fog(%d,%d,%d) start=%.0f end=%.0f light=(%.2f,%.2f,%.2f)"),
		*ScePath, scene.fogColor.r, scene.fogColor.g, scene.fogColor.b,
		scene.fogStart, scene.fogEnd, scene.lightDir.x, scene.lightDir.y, scene.lightDir.z);
}
