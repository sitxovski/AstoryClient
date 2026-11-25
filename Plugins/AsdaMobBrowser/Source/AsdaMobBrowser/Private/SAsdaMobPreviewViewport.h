// Copyright (c) 2026 @sitxovski (Discord)
// SAsdaMobPreviewViewport — 3D-превью моба (редакторское). Сцена/пол/носители — общая база SAstoryPreviewViewport.
//
// ГЕОМЕТРИЯ = как у игрового актора AAsdaMobActor: масштаб актора 0.01 × меш (1, −1, 1) (зеркальный фикс RH) ⇒ компонент
//   (0.01, −0.01, 0.01) — у скелета и у статического фолбэка одинаково (закон mob-static-scale). Материалы — оверрайд
//   DT_MobTextures того же правила, что ApplyMobTextures. Клип играется одиночным узлом (смешивание игры здесь не нужно).
//
// УПРАВЛЕНИЕ: ЛКМ — тащить влево/вправо = поворот модели; ПКМ — тащить вверх/вниз = наклон камеры; колесо — приближение;
//   двойной щелчок ЛКМ — вид по умолчанию. Камера своя (орбита движка выключена), поэтому модель не «улетает» из кадра.
#pragma once

#include "CoreMinimal.h"
#include "AstoryPreviewViewport.h"
#include "AsdaMobBrowserData.h"

class FAsdaMobPreviewClient;
class UAnimSequence;

class SAsdaMobPreviewViewport : public SAstoryPreviewViewport
{
public:
	SLATE_BEGIN_ARGS(SAsdaMobPreviewViewport) {}
	SLATE_END_ARGS()

	void Construct(const FArguments& InArgs);
	virtual ~SAsdaMobPreviewViewport() override;

	// Показать модель (ассеты уже разрешены слоем данных). bNpc — модель NPC (свой разворот к камере). Камера — по габариту.
	void SetModel(const FAsdaMobModelAssets& Assets, bool bNpc);
	void ClearModel();

	// Проиграть клип (nullptr — стоп, поза привязки). Возврат — клип реально поставлен.
	bool PlayClip(UAnimSequence* Clip, bool bLoop);
	// Текущий клип (для проверки).
	UAnimSequence* GetCurrentClip() const;
	void SetPaused(bool bPause);
	bool IsPaused() const { return bPaused; }

	// Поворот модели вокруг вертикали (градусы, 0 = лицом к камере) и вид камеры.
	void SetModelYaw(float Deg);
	float GetModelYaw() const { return ModelYaw; }
	void SetCameraPitch(float Deg);
	float GetCameraPitch() const { return CamPitch; }
	void SetZoom(float Factor);                    // 1 = по габариту; меньше — ближе
	float GetZoom() const { return Zoom; }
	void ResetView();

	void SetShowFloor(bool bVisible);
	bool IsShowFloor() const { return bShowFloor; }

	// Есть ли что показывать и чем (для подписи окна).
	bool HasSkeletal() const;
	bool HasStatic() const;
	FBox GetModelBounds() const { return ModelBounds; }

	// ── Диагностика (самопроверка): масштаб без зеркала Y и режим отображения вьюпорта ──
	void SetDebugNoMirror(bool b) { bDebugNoMirror = b; ApplyModelTransform(); }
	void SetDebugViewMode(int32 Mode);   // 0 — Lit, 1 — только освещение, 2 — без освещения, 3 — мировые нормали

	// Применить камеру к клиенту (зовёт клиент каждый кадр и методы выше).
	void ApplyCamera();

	// ── Для самопроверки (кадр без окна) ──
	TSharedPtr<FAsdaMobPreviewClient> GetPreviewClient() const { return Client; }

protected:
	virtual TSharedRef<FEditorViewportClient> MakeEditorViewportClient() override;

private:
	void ApplyModelTransform();
	// Курс камеры: со стороны направленного света сцены превью (свет из-за плеча), см. .cpp.
	float CameraYaw() const;
	void ApplyMaterials(UMeshComponent* MC, const FAsdaMobModelAssets& Assets);
	// Дождаться сборки текстур материалов модели и держать их мипы загруженными (иначе модель рисуется заглушкой).
	void EnsureTexturesReady();

	TSharedPtr<FAsdaMobPreviewClient> Client;
	FBox ModelBounds = FBox(ForceInit);   // габарит модели в мире при нулевом повороте (по нему ставится камера)
	float ModelYaw = 0.f;                 // поворот модели пользователем, град
	float BaseYaw = -90.f;                // поворот, при котором модель смотрит в камеру (у NPC — свой)
	float CamPitch = -12.f;               // наклон камеры, град (вниз — отрицательный)
	float Zoom = 1.f;                     // множитель дистанции камеры
	bool bPaused = false;
	bool bShowFloor = true;
	bool bDebugNoMirror = false;          // диагностика: масштаб (0.01, 0.01, 0.01) вместо игрового (0.01, −0.01, 0.01)
};
