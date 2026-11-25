// Copyright (c) 2026 @sitxovski (Discord)
// AsdaSkinTuneActor — ЖИВОЙ тюнер скелетной анимации (CPU-скиннинг каждый кадр в ProceduralMesh).
// Грузит .asdaraw (экспорт Tools/asda_export_raw.py из .jbm+.jbb) и применяет ВСЮ матовую конвенцию
// через множество EditAnywhere-контроллеров. Цель: вручную перебрать варианты и найти верный.
// UE — left-handed, row-vector (как D3D/эталон) → матрицу кости строим ТОЧНО как mathQuaternionToMatrix4x4.
// ★ EDITOR-ONLY DEV-ИНСТРУМЕНТ (модуль AsdaImporters): читает RawAssets/*.asdaraw с диска, в игровую
//   сборку не входит (закон проекта: рантайм читает данные ТОЛЬКО из ассетов UE).
#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "AsdaSkinTuneActor.generated.h"

class UProceduralMeshComponent;

// --- режимы (контроллеры) ---
UENUM(BlueprintType)
enum class EAsdaSkelMode : uint8
{
	Flat        UMETA(DisplayName="Flat (каждая кость = свой кейфрейм, абсолют)"),
	Hierarchical UMETA(DisplayName="Hierarchical Full (композит rot+pos — позиция улетает при абс.кейфреймах)"),
	HierRotOnly  UMETA(DisplayName="Hier RotOnly (от родителя только вращение, позиция абсолютная)")
};

UENUM(BlueprintType)
enum class EAsdaQuatMat : uint8
{
	EngineExact UMETA(DisplayName="EngineExact (mathQuaternionToMatrix4x4)"),
	Standard    UMETA(DisplayName="Standard (транспонированная)")
};

UENUM(BlueprintType)
enum class EAsdaBindMode : uint8
{
	Identity UMETA(DisplayName="Identity (вершины bone-local)"),
	InvRest  UMETA(DisplayName="InvRest (inverse-bind = inv(кадр0))")
};

UENUM(BlueprintType)
enum class EAsdaScaleMode : uint8
{
	ScaleRows UMETA(DisplayName="ScaleRows S*R (эталон)"),
	ScaleCols UMETA(DisplayName="ScaleCols R*S")
};

UENUM(BlueprintType)
enum class EAsdaAxisPerm : uint8
{
	XYZ, XZY, YXZ, YZX, ZXY, ZYX
};

UENUM(BlueprintType)
enum class EAsdaModel : uint8
{
	Opara    UMETA(DisplayName="opara (растение)"),
	Scorpion UMETA(DisplayName="scorpion (моб)")
};

// внутренние CPU-структуры (не UPROPERTY)
struct FAsdaVtx { FVector3f Pos, Nrm; FVector2f UV; int32 B0, B1; float W0; };
struct FAsdaMeshCPU { TArray<FAsdaVtx> Verts; TArray<int32> Idx; };
struct FAsdaKey { float TimeMs; FVector3f Pos; FQuat4f Quat; FVector3f Scale; };
struct FAsdaBone { FString Name; int32 Parent; float Conv; TArray<FAsdaKey> Keys; };

UCLASS()
class ASDAIMPORTERS_API AAsdaSkinTuneActor : public AActor
{
	GENERATED_BODY()
public:
	AAsdaSkinTuneActor();

	UPROPERTY(VisibleAnywhere) UProceduralMeshComponent* Mesh;

	// ===== ВЫБОР МОДЕЛИ =====
	UPROPERTY(EditAnywhere, Category="Asda|Model") EAsdaModel Model = EAsdaModel::Scorpion;
	// Каталог с .asdaraw (по умолчанию RawAssets/Models/_AnimTest/raw). Файлы: opara.asdaraw / scorpion.asdaraw.
	UPROPERTY(EditAnywhere, Category="Asda|Model") FString RawDir;

	// ===== СКЕЛЕТ / МАТРИЦА КОСТИ =====
	// FLAT — доказано численно: жёсткие части двигаются вместе со своей костью БЕЗ улёта (иерархия улетает:
	// кейфреймы .jbb абсолютные, иерархия повторно крутит абс.позицию родителем).
	UPROPERTY(EditAnywhere, Category="Asda|Skel") EAsdaSkelMode SkelMode = EAsdaSkelMode::Flat;
	UPROPERTY(EditAnywhere, Category="Asda|Skel") EAsdaQuatMat QuatMat = EAsdaQuatMat::EngineExact;
	UPROPERTY(EditAnywhere, Category="Asda|Skel") EAsdaScaleMode ScaleMode = EAsdaScaleMode::ScaleRows;
	UPROPERTY(EditAnywhere, Category="Asda|Skel") bool bApplyConvert = true;
	UPROPERTY(EditAnywhere, Category="Asda|Skel") bool bConjugateQuat = false;   // ВЫКЛ (подтверждено визуально: идеально)
	UPROPERTY(EditAnywhere, Category="Asda|Skel") bool bNegateQuatW = false;
	// при Hierarchical: world = local*parent (по умолч.) или parent*local
	UPROPERTY(EditAnywhere, Category="Asda|Skel") bool bHierParentFirst = false;

	// ===== INVERSE-BIND =====
	UPROPERTY(EditAnywhere, Category="Asda|Bind") EAsdaBindMode BindMode = EAsdaBindMode::InvRest;
	UPROPERTY(EditAnywhere, Category="Asda|Bind") bool bTwoBoneBlend = true;

	// ===== ОСИ (game LH Y-up → UE LH Z-up) =====
	UPROPERTY(EditAnywhere, Category="Asda|Axes") EAsdaAxisPerm AxisPerm = EAsdaAxisPerm::XZY;
	UPROPERTY(EditAnywhere, Category="Asda|Axes") bool bNegX = false;
	UPROPERTY(EditAnywhere, Category="Asda|Axes") bool bNegY = false;
	UPROPERTY(EditAnywhere, Category="Asda|Axes") bool bNegZ = false;
	UPROPERTY(EditAnywhere, Category="Asda|Axes") bool bReverseWinding = false;
	UPROPERTY(EditAnywhere, Category="Asda|Axes") FRotator ExtraRotate = FRotator::ZeroRotator;
	UPROPERTY(EditAnywhere, Category="Asda|Axes", meta=(ClampMin="0.001")) float GlobalScale = 1.0f;

	// ===== АНИМАЦИЯ =====
	// ГЛАВНЫЙ переключатель: Анимирован (бежит время) / Статический (замороженная поза кадра ManualTimeMs).
	UPROPERTY(EditAnywhere, Category="Asda|Anim") bool bAnimated = true;
	UPROPERTY(EditAnywhere, Category="Asda|Anim") bool bSlerp = true;
	UPROPERTY(EditAnywhere, Category="Asda|Anim", meta=(ClampMin="0.0")) float TimeScale = 1.0f;
	// Кадр для статической позы (мс). Работает когда bAnimated=false.
	UPROPERTY(EditAnywhere, Category="Asda|Anim", meta=(ClampMin="0.0", EditCondition="!bAnimated")) float ManualTimeMs = 0.0f;

	// ===== ОТЛАДКА / ОБРЕЗКА КОСТЕЙ =====
	UPROPERTY(EditAnywhere, Category="Asda|Debug") bool bDrawBones = false;
	UPROPERTY(EditAnywhere, Category="Asda|Debug") bool bDrawBoneNames = false;
	// ДИАГНОСТИКА: показать сырые вершины .jbm БЕЗ скиннинга (только оси). Геометрия чистая? → разлёт в скине.
	UPROPERTY(EditAnywhere, Category="Asda|Debug") bool bRawNoSkin = false;
	// Показать только ОДИН меш-кусок (-1 = все). Найти, какой из частей разлетается.
	UPROPERTY(EditAnywhere, Category="Asda|Debug") int32 SingleMeshIndex = -1;
	UPROPERTY(VisibleAnywhere, Category="Asda|Debug") int32 NumMeshes = 0;
	// Обрезка как наш prune_bones: не-скиннутые ЛИСТОВЫЕ кости замораживаются в rest (не летают, не тянут детей).
	UPROPERTY(EditAnywhere, Category="Asda|Debug") bool bStripUnskinnedLeaves = false;
	// Рисовать скелет только для костей, реально влияющих на меш.
	UPROPERTY(EditAnywhere, Category="Asda|Debug") bool bDrawOnlySkinned = false;
	// Заморозить в rest все кости с индексом >= N (-1 = выкл). Бинпоиск «улетающей» кости.
	UPROPERTY(EditAnywhere, Category="Asda|Debug") int32 FreezeBonesFromIndex = -1;
	// Обрезать улетевшие вершины: если |pos|>порога — притянуть к порогу (0 = выкл). Усмиряет картинку.
	UPROPERTY(EditAnywhere, Category="Asda|Debug", meta=(ClampMin="0.0")) float ClampVertDist = 0.0f;
	UPROPERTY(VisibleAnywhere, Category="Asda|Debug") int32 NumBones = 0;
	UPROPERTY(VisibleAnywhere, Category="Asda|Debug") int32 NumSkinnedBones = 0;
	UPROPERTY(VisibleAnywhere, Category="Asda|Debug") int32 NumKeptBones = 0;
	UPROPERTY(VisibleAnywhere, Category="Asda|Debug") int32 NumVerts = 0;
	UPROPERTY(VisibleAnywhere, Category="Asda|Debug") float DurationMs = 0.0f;

	virtual void OnConstruction(const FTransform& Xform) override;
	virtual void Tick(float DeltaSeconds) override;
	virtual bool ShouldTickIfViewportsOnly() const override { return true; }  // живая анимация в редакторе
#if WITH_EDITOR
	virtual void PostEditChangeProperty(FPropertyChangedEvent& E) override;
#endif

private:
	TArray<FAsdaMeshCPU> Meshes;
	TArray<FAsdaBone> Bones;
	TArray<uint8> bSkinnedBone;   // кость влияет на меш (есть вершины)
	TArray<uint8> bKeptBone;      // осталась после prune (не-скиннутый лист — нет)
	float Fps = 30.0f;
	float PlayTimeMs = 0.0f;
	bool bLoaded = false;
	void ComputeBoneFlags();

	FString CurrentRawPath() const;
	bool LoadRaw();
	void BuildSections();                       // создать секции PMC (геометрия)
	void UpdateSkin();                           // пересчитать скин на текущем времени
	FMatrix44f BoneLocalMatrix(const FAsdaKey& K, float Conv) const;
	void SampleBone(const FAsdaBone& B, float TimeMs, FAsdaKey& Out) const;
	void ComputeWorld(float TimeMs, TArray<FMatrix44f>& OutWorld) const;
	FMatrix44f GlobalBasis() const;
};
