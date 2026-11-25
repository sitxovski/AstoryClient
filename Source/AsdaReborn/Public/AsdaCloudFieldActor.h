// Copyright (c) 2026 @sitxovski (Discord)
#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "AsdaCloudFieldActor.generated.h"

class UStaticMeshComponent;

// Поле НИЗКИХ 3D-облаков (лоу-поли блобы в стиле игры): N мешей на случайных позициях/масштабах,
// медленный дрейф по ветру с заворотом по границе поля. Реальные тени (CastShadow) от солнца.
// Всё редактируется в деталях (PostEditChangeProperty → пересборка), Seed фиксирует раскладку.
UCLASS()
class ASDAREBORN_API AAsdaCloudFieldActor : public AActor
{
	GENERATED_BODY()

public:
	AAsdaCloudFieldActor();

	UPROPERTY(EditAnywhere, Category = "Asda|CloudField")
	TObjectPtr<UMaterialInterface> Material;

	// Цельные меши облаков (метабол-блобы SM_cloudblob_*); пусто = фолбэк на композицию сфер
	UPROPERTY(EditAnywhere, Category = "Asda|CloudField")
	TArray<TObjectPtr<UStaticMesh>> CloudMeshes;

	// ---- Вид ----
	// ЦВЕТ/ЯРКОСТЬ облаков НЕ здесь: они в пер-карточном материале M_AsdaCloudMesh_<карта>
	// (Tint/ShadeColor/Intensity — правка применяется живьём через наследование MID→MIC).
	// Доля ПОЛНОСТЬЮ плотных облаков (0..1)
	UPROPERTY(EditAnywhere, Category = "Asda|CloudField|Вид", meta = (ClampMin = "0", ClampMax = "1"))
	float SolidFraction = 0.35f;

	// Сколько облаков в поле
	UPROPERTY(EditAnywhere, Category = "Asda|CloudField", meta = (ClampMin = "0", ClampMax = "64"))
	int32 Count = 38;

	// Полуразмеры поля (от позиции актора), юниты
	UPROPERTY(EditAnywhere, Category = "Asda|CloudField")
	FVector2D AreaExtent = FVector2D(40000.f, 40000.f);

	// Диапазон высот (низкие облака: 250–700 м)
	UPROPERTY(EditAnywhere, Category = "Asda|CloudField")
	float ZMin = -25000.f;
	UPROPERTY(EditAnywhere, Category = "Asda|CloudField")
	float ZMax = 0.f;

	// Диапазон масштабов облака
	UPROPERTY(EditAnywhere, Category = "Asda|CloudField")
	float ScaleMin = 0.2f;
	UPROPERTY(EditAnywhere, Category = "Asda|CloudField")
	float ScaleMax = 0.55f;

	// Ветер: направление (град, yaw) и скорость (юниты/сек)
	UPROPERTY(EditAnywhere, Category = "Asda|CloudField")
	float WindYaw = 20.f;
	UPROPERTY(EditAnywhere, Category = "Asda|CloudField")
	float WindSpeed = 800.f;

	// Замедление верхних облаков: скорость самого высокого = WindSpeed × этот коэффициент
	// (нижние летят на полной WindSpeed; чем дальше от земли — тем медленнее)
	UPROPERTY(EditAnywhere, Category = "Asda|CloudField", meta = (ClampMin = "0", ClampMax = "1"))
	float HighAltSlowdown = 0.25f;

	// Реальные тени от облаков
	UPROPERTY(EditAnywhere, Category = "Asda|CloudField")
	bool bCastShadows = true;

	// Прозрачность облака (случайная в диапазоне, пер-облако)
	UPROPERTY(EditAnywhere, Category = "Asda|CloudField", meta = (ClampMin = "0", ClampMax = "1"))
	float OpacityMin = 0.55f;
	UPROPERTY(EditAnywhere, Category = "Asda|CloudField", meta = (ClampMin = "0", ClampMax = "1"))
	float OpacityMax = 1.0f;

	// ---- Рандом ----
	// Зерно раскладки (та же цифра = та же расстановка)
	UPROPERTY(EditAnywhere, Category = "Asda|CloudField|Рандом")
	int32 Seed = 7;
	// Долей (сфер) в облаке: мин/макс
	UPROPERTY(EditAnywhere, Category = "Asda|CloudField|Рандом", meta = (ClampMin = "2", ClampMax = "16"))
	int32 LobesMin = 5;
	UPROPERTY(EditAnywhere, Category = "Asda|CloudField|Рандом", meta = (ClampMin = "2", ClampMax = "16"))
	int32 LobesMax = 8;
	// Джиттер размеров долей (0 = ровные, 0.3 = рваные)
	UPROPERTY(EditAnywhere, Category = "Asda|CloudField|Рандом", meta = (ClampMin = "0", ClampMax = "0.5"))
	float SizeJitter = 0.15f;

	virtual void OnConstruction(const FTransform& Xf) override;
	virtual void BeginPlay() override;   // PIE/игра: дубликат уровня несёт компоненты, но Transient-якоря пусты — пересборка

	// ★ 05.09.2026 (ревизия review_devmenu_tabs.md В-1/В-2) — применить тумблер asda.Clouds.Field к этому полю.
	//   bFieldOn=false: актор скрыт в игре И теневые прокси перестают отбрасывать тень (у прокси CastHiddenShadow —
	//   без этого «облаков нет, а пятна теней ползут»). bFieldOn=true: возвращается ЗАПОМНЕННОЕ авторское скрытие актора
	//   (bAuthoredHidden из umap), а не безусловное «показать», и прокси снова отбрасывают тень (если bCastShadows).
	void ApplyFieldToggle(bool bFieldOn);
	// Строка состояния для самопроверки (asda.DevMenu.SelfTest clouds): скрыт / авторское / прокси всего / отбрасывают тень.
	FString DescribeFieldState() const;
	// Сколько теневых прокси сейчас отбрасывают тень (самотест настройки теней).
	int32 CastingShadowProxies() const;
	// ★ Волна CPU, мелочь М-2 ревизии review_cpu_visibility.md (07.09.2026): общий рубильник «Оптимизация видимости»
	//   (asda.Vis.Enable) должен возвращать облакам покадровый шаг. Вызывается колбэком asda.Vis.Enable из
	//   AsdaVisibilitySubsystem.cpp: bEnabled=false → эффективный шаг облаков ПРИНУДИТЕЛЬНО 1 (база asda.Clouds.UpdateEvery
	//   не трогается — это тонкая настройка, к ней возвращаемся при bEnabled=true). Статик: рубильник один на процесс,
	//   поля всех игровых миров читают его в Tick. Регистрации ключа здесь нет (это не CVar) — дублей не создаёт.
	static void SetVisMasterEnabled(bool bEnabled);
	virtual void Tick(float Dt) override;
	virtual bool ShouldTickIfViewportsOnly() const override { return true; }
#if WITH_EDITOR
	virtual void PostEditChangeProperty(FPropertyChangedEvent& E) override;
#endif

private:
	UPROPERTY(Transient)
	TArray<TObjectPtr<USceneComponent>> CloudAnchors;
	UPROPERTY(Transient)
	TArray<float> AnchorSpeedFactor;
	UPROPERTY(Transient)
	TArray<TObjectPtr<UMaterialInstanceDynamic>> CloudMIDs;
	// Теневые прокси (невидимые меши с CastHiddenShadow), собранные Rebuild — их тень гасит ApplyFieldToggle(false).
	UPROPERTY(Transient)
	TArray<TObjectPtr<UStaticMeshComponent>> ShadowProxies;
	// Авторское состояние bHidden актора (из umap), снятое при ПЕРВОМ применении тумблера; к нему возвращает ключ 1.
	bool bAuthoredHidden = false;
	// bAuthoredHidden уже снят? До первого применения IsHidden() = авторское значение.
	bool bAuthoredCached = false;
	// Разрешение тени поля на момент последнего применения: Tick сверяет его с ключом и настройкой теней.
	bool bShadowsAllowedLast = true;

	// ★ Волна CPU 07.09.2026 (asda.Clouds.UpdateEvery): счётчик кадров до следующего шага ветра
	//   и накопленный за пропущенные кадры Dt (путь облаков не теряется, шаг просто реже).
	int32 WindFrameCounter = 0;
	float WindPendingDt = 0.f;

	void Rebuild();
};
