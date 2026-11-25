// Copyright (c) 2026 @sitxovski (Discord)
// UAsdaAnimatedPlane — переиспользуемый UMG-виджет кадровой анимации JUI-plane (система, правило classify:B).
//   Оригинал (JUI_PLANE_ANIMATION.md): plane с Animate="1" циклирует кадры спрайт-листа. Поля из JuiSkin
//   (CControl_SerializeFrameBlock@0x13048F0): ImageFileName/UV(кадр0)/FrameTextureSize/TotalFrame/FrameInterval.
//   Update-формула 1:1 из ui_core.c L108794–108888: frameIndex=floor(elapsed/FrameInterval*(TotalFrame-1)),
//   кадр→(col,row) в сетке текстуры, UV=базовый+(col*uStep,row*vStep). Рендер через FSlateBrush::SetUVRegion
//   (тот же механизм статичных JUI-планов порта). Редактируется в UMG-дизайнере (правило ue-editor-friendly).
#pragma once

#include "CoreMinimal.h"
#include "Components/Image.h"
#include "AsdaAnimatedPlane.generated.h"

// Событие завершения one-shot анимации (аналог CControl_RaiseEvent(this,0,24,1) — «анимация завершена»).
DECLARE_DYNAMIC_MULTICAST_DELEGATE(FAsdaPlaneAnimFinished);

UCLASS()
class ASDAREBORN_API UAsdaAnimatedPlane : public UImage
{
	GENERATED_BODY()

public:
	// Спрайт-лист (текстура со всеми кадрами сеткой).
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Asda|PlaneAnim")
	TObjectPtr<UTexture2D> FrameTexture;

	// Размер ОДНОГО кадра в пикселях (FrameTextureSize из JuiSkin).
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Asda|PlaneAnim")
	FVector2D FrameSizePx = FVector2D::ZeroVector;

	// Число кадров (TotalFrame).
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Asda|PlaneAnim")
	int32 TotalFrame = 0;

	// Длительность ПОЛНОГО прогона frame0→frameN-1, мс (FrameInterval — НЕ на кадр).
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Asda|PlaneAnim")
	float FrameIntervalMs = 0.f;

	// ★★★ КАДР, С КОТОРОГО НАЧИНАЕТСЯ ПРОГОН (эталон `uiControl_ShowWithItem@0x12E1040`, аргумент a2:
	//   `this[141] = a2`, а формула тика — `frameIndex = elapsed/FrameInterval*(TotalFrame-1) + this[141]`
	//   (`ui_core.c:108838-108840`). Нужен там, где анимация показывает УЖЕ ЧАСТИЧНО ПРОШЕДШЕЕ
	//   состояние: шторка перезарядки, восстановленной пакетом 5465 на логине
	//   (`Recv_SkillCoolTime@0xE4B180` считает стартовый кадр = доля пройденного × 1.18).
	//   На ЗАВЕРШЕНИИ прогона эталон обнуляет его (`gap_220[20] = 0`, ui_core.c:108893) — повторяем.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Asda|PlaneAnim")
	int32 StartFrame = 0;

	// UV-регион КАДРА 0 (min=верх-лево, max=низ-право). Кадр N сдвигается сеткой от него.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Asda|PlaneAnim")
	FBox2D BaseUV = FBox2D(FVector2D::ZeroVector, FVector2D(1.f, 1.f));

	// Зациклить (иначе — one-shot: после MaxLoops прогонов стоп + событие finished).
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Asda|PlaneAnim")
	bool bLoop = true;

	// Лимит прогонов: 0 = бесконечно (при bLoop). При !bLoop эффективный лимит = max(MaxLoops,1).
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Asda|PlaneAnim")
	int32 MaxLoops = 0;

	// Броадкастится при завершении one-shot прогона (эталонное событие 24). Для лупа (bLoop) не шлётся.
	UPROPERTY(BlueprintAssignable, Category="Asda|PlaneAnim")
	FAsdaPlaneAnimFinished OnAnimFinished;

	// Перезапустить анимацию с кадра 0 (точнее — с StartFrame) и ВЗВЕСТИ тик.
	UFUNCTION(BlueprintCallable, Category="Asda|PlaneAnim")
	void RestartAnim();

	// ★ Остановить и сбросить в кадр 0 БЕЗ взвода тика и БЕЗ события «завершено».
	//   Нужен путям «спрятать» (эталон `CControl_CloseHide@0x12E1420`): там перезапускать анимацию
	//   нельзя — обработчик события «завершено» сам зовёт CloseHide, и взвод тика на скрытом плане
	//   замкнул бы вечный круг «доиграл → спрятали → доиграл».
	UFUNCTION(BlueprintCallable, Category="Asda|PlaneAnim")
	void StopAnim();

	// Прогон завершён (для one-shot). Аналог события 24 оригинала.
	UFUNCTION(BlueprintCallable, Category="Asda|PlaneAnim")
	bool IsAnimFinished() const { return bFinished; }

protected:
	virtual TSharedRef<SWidget> RebuildWidget() override;
	virtual void SynchronizeProperties() override;
	// Слейт-виджет уничтожается — активный таймер умирает вместе с ним, сбрасываем флаг регистрации.
	virtual void ReleaseSlateResources(bool bReleaseChildren) override;
#if WITH_EDITOR
	virtual const FText GetPaletteCategory() override;
#endif

private:
	// ★★★ Взвести active-timer, если его СЕЙЧАС нет. Слейт снимает таймер, как только делегат вернул
	//   `Stop`, а `RebuildWidget` больше не вызывается — без переустановки одноразовый план
	//   ПОСЛЕ ПЕРВОГО ПРОГОНА больше никогда не тикает (разбор — `skill_use_system.md` §23.1).
	void EnsureTimer();
	// Один тик active-timer: продвинуть кадр по времени, обновить UV. Continue пока анимируем.
	EActiveTimerReturnType TickAnim(double InCurrentTime, float InDeltaTime);
	// Пересчитать UV-регион для кадра FrameIndex и записать в браш (SetUVRegion).
	void ApplyFrameUV(int32 FrameIndex);
	// Достаточно ли данных для анимации (текстура/кадры/интервал/размер кадра).
	bool CanAnimate() const;

	double StartTime = -1.0;   // время старта текущего прогона (сек, из active-timer)
	int32  LastFrame = -1;     // последний применённый кадр (не трогать UV, если не сменился)
	int32  LoopCount = 0;      // счётчик завершённых прогонов
	bool   bFinished = false;  // one-shot завершён
	bool   bTimerRegistered = false;
};
