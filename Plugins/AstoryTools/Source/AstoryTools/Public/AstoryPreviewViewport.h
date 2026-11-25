// Copyright (c) 2026 @sitxovski (Discord)
// SAstoryPreviewViewport / FAstoryPreviewViewportClient — ★ ОБЩАЯ БАЗА редакторских 3D-превью
//   (волна 3 п.15 плана дублей, 15.09.2026; аудит `Reports/dedup_audit/04_cross_layer.md` S4/X12).
//
// Зачем. Два окна превью (`SAsdaItemPreviewViewport` браузера предметов и `SAsdaNormalStudioViewport`
//   студии нормалей) дословно повторяли одно устройство: сцена `FAdvancedPreviewScene` с полом,
//   `SEditorViewport`, компоненты-носители статики и скелетки, вьюпорт-клиент с орбитальной камерой и
//   ручным тиком мира превью. Здесь это устройство собрано один раз; наследники добавляют только своё
//   (эффект на кости, автоориентацию, материалы нормалей, свет).
//
// ★ Поведение наследников не меняется: порядок настройки клиента, тика и уничтожения — тот же, что был в
//   каждой копии (сверено построчно при переносе).
//
// Использование:
//   class SMyViewport : public SAstoryPreviewViewport { ... Construct(): ConstructPreviewScene(true); ... }
//   class FMyClient : public FAstoryPreviewViewportClient { PreWorldTick()/PostWorldTick() — своё до/после тика мира }
#pragma once

#include "CoreMinimal.h"
#include "SEditorViewport.h"
#include "EditorViewportClient.h"

class FAdvancedPreviewScene;
class UStaticMeshComponent;
class USkeletalMeshComponent;

// Вьюпорт-клиент превью: настройки как у превью-вьюпортов движка + тик мира превью.
class ASTORYTOOLS_API FAstoryPreviewViewportClient : public FEditorViewportClient
{
public:
	// InViewLocation/InViewRotation — стартовая камера; bSeparateTranslucency — отдельный проход
	//   прозрачности (нужен эффектам-квадам браузера предметов, студии нормалей — нет).
	FAstoryPreviewViewportClient(FAdvancedPreviewScene* InScene, const TSharedRef<SEditorViewport>& InViewport,
		const FVector& InViewLocation, const FRotator& InViewRotation, bool bSeparateTranslucency);

	// Тик: клиент движка → PreWorldTick → тик мира превью (иначе акторы/частицы не симулируются) → PostWorldTick.
	virtual void Tick(float DeltaSeconds) override;

protected:
	// Своё ДО тика мира превью (напр. камера превью-сцены для эффекта).
	virtual void PreWorldTick(float DeltaSeconds) {}
	// Своё ПОСЛЕ тика мира превью (автовращение модели, вращение света).
	virtual void PostWorldTick(float DeltaSeconds) {}
};

// Виджет превью: сцена + компоненты-носители. Сам по себе не создаётся (клиента делает наследник).
class ASTORYTOOLS_API SAstoryPreviewViewport : public SEditorViewport
{
public:
	// Сбрасывает сцену ПОСЛЕ деструктора наследника (как было в обеих копиях: сцена — последней).
	virtual ~SAstoryPreviewViewport() override;

protected:
	// Создать сцену превью (видимость пола — сразу), построить SEditorViewport (он вызывает
	//   MakeEditorViewportClient наследника) и завести компоненты-носители статики и скелетки.
	//   Зовётся первой строкой Construct() наследника.
	void ConstructPreviewScene(bool bFloorVisible);

	// Сцена превью (свет, пол, окружение движка)
	TSharedPtr<FAdvancedPreviewScene> PreviewScene;
	// Компоненты-носители: живут в PreviewScene (она держит ссылку для GC), дальше меняется только меш/материалы
	UStaticMeshComponent*   StaticComp = nullptr;
	USkeletalMeshComponent* SkelComp = nullptr;
};
