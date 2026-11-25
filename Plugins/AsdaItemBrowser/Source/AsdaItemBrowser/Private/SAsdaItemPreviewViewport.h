// Copyright (c) 2026 @sitxovski (Discord)
// SAsdaItemPreviewViewport — 3D-вьюпорт превью предмета (редакторский).
// Показывает импортированный ассет предмета (StaticMesh или SkeletalMesh), красит его ЭТАЛОННОЙ
// текстурой из img-записи и, если у предмета есть эффект (свечение заточки), спавнит BP_Fx_* в
// том же пространстве, что и модель.
//
// ГЕОМЕТРИЯ ПРЕВЬЮ = 1:1 связка «модель ↔ эффект» из игры (см. .cpp: MakeMeshLocal/MakeFxLocal):
//   в игре и меш оружия, и его свечение живут в матрице ОДНОЙ кости (8_a1), поэтому в превью
//   роль кости играет единичный трансформ, а модель/эффект получают ровно те же поправки,
//   что даёт рантайм (масштаб компонента персонажа 0.01/-0.01/0.01 и матрица FxToBone).
#pragma once

#include "CoreMinimal.h"
#include "AstoryPreviewViewport.h"   // ★ волна 3 п.15: общая база превью (сцена, пол, носители, тик мира)

class FAdvancedPreviewScene;
class FAsdaItemPreviewClient;
class UStaticMesh;
class USkeletalMesh;
class UStaticMeshComponent;
class USkeletalMeshComponent;
class UTexture2D;

// Что показать во вьюпорте (готовые ассеты; резолв путей — в слое данных).
struct FAsdaPreviewRequest
{
	UStaticMesh*   StaticMesh = nullptr;
	USkeletalMesh* SkeletalMesh = nullptr;
	UTexture2D*    Texture = nullptr;       // эталонная текстура модели (параметр "Tex")
	UClass*        FxClass = nullptr;       // BP_Fx_* эффекта (nullptr = эффекта нет)
	bool bWeaponBoneOffset = false;         // оружие: смещение (0,46.3,0) в пространстве кости (AttachWeapon)
	// Разрешить автоориентацию по габаритам. Ставится для ПРЕДМЕТОВ В РУКЕ (оружие/щит): их модели
	// авторены в осях игры и без кости персонажа лежат боком. Броня/дроп-модели авторены «стоя» —
	// у них длинная ось это ширина, доворот только испортил бы вид.
	bool bAllowAutoOrient = false;
};

class SAsdaItemPreviewViewport : public SAstoryPreviewViewport
{
public:
	SLATE_BEGIN_ARGS(SAsdaItemPreviewViewport) {}
	SLATE_END_ARGS()

	void Construct(const FArguments& InArgs);
	virtual ~SAsdaItemPreviewViewport() override;

	// Показать предмет (любой из мешей может быть nullptr — тогда модель просто не рисуется).
	void SetPreview(const FAsdaPreviewRequest& Req);
	void ClearPreview();

	// Поворот всей связки «модель + эффект» вокруг вертикали (вызывает вьюпорт-клиент).
	void ApplySpin(float YawDegrees);

	void SetAutoRotate(bool bEnable);
	bool IsAutoRotate() const;
	void SetRotateSpeed(float DegPerSec);
	float GetRotateSpeed() const;

	void SetShowFloor(bool bVisible);
	bool IsShowFloor() const { return bShowFloor; }

	// Автоориентация: доворот превью по ГЛАВНОЙ ОСИ ГАБАРИТОВ меша (модели оружия авторены в осях
	// игры — в мире их ставит кость персонажа, в превью «кости» нет и арбалеты/баллисты лежат боком).
	// Доворот применяется к СВЯЗКЕ «модель + эффект» целиком, поэтому посадка свечения не меняется.
	void SetAutoOrient(bool bEnable);
	bool IsAutoOrient() const { return bAutoOrient; }

	// ── Открыто для headless-самопроверки (AsdaItemBrowserDebug) ──
	// Трансформ модели относительно «кости» (масштаб компонента персонажа + сдвиг оружия).
	static FTransform MakeMeshLocalTransform(bool bWeaponBoneOffset);
	// Доворот по главной оси габаритов; FQuat::Identity = доворот не нужен (ось уже вертикальна/предмет «кубический»).
	static FQuat ComputeAlignRotation(const FBox& LocalBounds, const FTransform& MeshLocal);

	// Подогнать камеру под габариты текущей модели.
	void FocusOnPreview();

	// ★ 13.09.2026 (конфигуратор свечения): актор эффекта превью — ему ставится код предмета и
	//   переставляются материалы «силы свечения» после правки (мир превью в список миров движка не входит,
	//   и общий UAsdaFxLightSubsystem::NotifyGlowTablesChanged до него не дотягивается)
	AActor* GetFxActor() const { return FxActor.Get(); }

	// ★ Сообщить эффекту камеру НАШЕЙ превью-сцены (AAsdaEffectActor::bUseViewOverride).
	//   Без этого актор эффекта берёт камеру вьюпорта УРОВНЯ (движок про превью-сцены не знает):
	//   при открытой карте предмет превью оказывался дальше CullDistance ⇒ эффект не строился вовсе.
	//   Зовётся вьюпорт-клиентом каждый кадр ДО тика мира превью.
	void SetFxViewPoint(const FVector& ViewLoc, const FRotator& ViewRot);

protected:
	virtual TSharedRef<FEditorViewportClient> MakeEditorViewportClient() override;

private:
	void DestroyFxActor();

	// Сцена превью и компоненты-носители (StaticComp/SkelComp) — в базе SAstoryPreviewViewport.
	TSharedPtr<FAsdaItemPreviewClient> Client;

	TWeakObjectPtr<AActor>  FxActor;

	// Пересчитать доворот автоориентации по габаритам текущего меша.
	void UpdateAlignRotation();

	FTransform MeshLocal = FTransform::Identity;   // модель относительно «кости»
	FTransform FxLocal   = FTransform::Identity;   // эффект относительно «кости»
	FQuat AlignQuat = FQuat::Identity;             // доворот всей связки (автоориентация)
	float CurrentYaw = 0.f;
	bool  bShowFloor = true;
	bool  bAutoOrient = true;        // галка пользователя
	bool  bOrientAllowed = false;    // разрешено для текущего предмета (оружие/щит)
};
