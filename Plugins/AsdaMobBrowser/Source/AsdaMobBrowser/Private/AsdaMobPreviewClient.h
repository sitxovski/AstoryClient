// Copyright (c) 2026 @sitxovski (Discord)
// FAsdaMobPreviewClient — вьюпорт-клиент 3D-превью моба: своё управление мышью (поворот модели, наклон, приближение).
//   Тик мира превью и общие настройки (VMI_Lit, без сетки, realtime) — база FAstoryPreviewViewportClient.
#pragma once

#include "CoreMinimal.h"
#include "AstoryPreviewViewport.h"

class SAsdaMobPreviewViewport;
class FAdvancedPreviewScene;

class FAsdaMobPreviewClient : public FAstoryPreviewViewportClient
{
public:
	FAsdaMobPreviewClient(FAdvancedPreviewScene* InScene, const TSharedRef<SAsdaMobPreviewViewport>& InViewport);

	// ЛКМ/ПКМ/колесо — своё управление; остальное — движку.
	virtual bool InputKey(const FInputKeyEventArgs& Args) override;
	// Движение мыши при зажатой кнопке → поворот модели / наклон камеры.
	virtual bool InputAxis(const FInputKeyEventArgs& Args) override;

	bool IsRotating() const { return bRotating; }   // ЛКМ зажата (для самопроверки)

	static constexpr float kDegPerPixel = 0.5f;     // чувствительность: градусов на пиксель мыши

protected:
	virtual void PostWorldTick(float DeltaSeconds) override;

private:
	TWeakPtr<SAsdaMobPreviewViewport> ViewportWidget;
	bool bRotating = false;   // ЛКМ зажата — тащим поворот модели
	bool bTilting = false;    // ПКМ зажата — тащим наклон камеры
};
