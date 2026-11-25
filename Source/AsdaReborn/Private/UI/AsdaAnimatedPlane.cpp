// Copyright (c) 2026 @sitxovski (Discord)
#include "UI/AsdaAnimatedPlane.h"
#include "UI/AsdaUiHelpers.h"   // AsdaUi::LogicalAtlasSize — размер атласа до апскейла
#include "Engine/Texture2D.h"
#include "Widgets/Images/SImage.h"

TSharedRef<SWidget> UAsdaAnimatedPlane::RebuildWidget()
{
	TSharedRef<SWidget> Result = Super::RebuildWidget();   // создаёт MyImage (SImage) с брашем &Brush

	// Регистрируем active-timer на слейт-виджете — гарантированный per-frame тик анимации.
	EnsureTimer();
	return Result;
}

// ★★★ ПЕРЕУСТАНОВКА ТАЙМЕРА. `SWidget::RegisterActiveTimer` держит делегат ровно до тех пор, пока тот
//   возвращает `Continue`; на `Stop` Слейт СНИМАЕТ таймер (`SWidget::ExecuteActiveTimers` удаляет
//   хэндл). `RebuildWidget` после этого больше не зовётся — виджет уже построен, — поэтому
//   одноразовый план, доигравший ОДИН раз, оставался без тика НАВСЕГДА: `RestartAnim()` сбрасывал
//   `bFinished`, но продвигать кадры было уже некому, и событие «анимация завершена» больше не
//   приходило. Отсюда — «залипшие» вспышки Хранителя (их гасит событие 24 стрелки 56208) и
//   замороженные на кадре 0 шторки перезарядки в панели быстрого доступа.
void UAsdaAnimatedPlane::EnsureTimer()
{
	if (bTimerRegistered || !MyImage.IsValid())
	{
		return;
	}
	MyImage->RegisterActiveTimer(0.0f,
		FWidgetActiveTimerDelegate::CreateUObject(this, &UAsdaAnimatedPlane::TickAnim));
	bTimerRegistered = true;
}

void UAsdaAnimatedPlane::ReleaseSlateResources(bool bReleaseChildren)
{
	// Слейт-виджет уничтожен — вместе с ним умер и его active-timer.
	bTimerRegistered = false;
	Super::ReleaseSlateResources(bReleaseChildren);
}

void UAsdaAnimatedPlane::SynchronizeProperties()
{
	Super::SynchronizeProperties();
	// Сброс состояния + показать кадр 0 (в т.ч. превью в редакторе).
	StartTime = -1.0;
	LastFrame = -1;
	LoopCount = 0;
	bFinished = false;
	if (CanAnimate())
	{
		ApplyFrameUV(FMath::Clamp(StartFrame, 0, FMath::Max(0, TotalFrame - 1)));
	}
	EnsureTimer();
}

#if WITH_EDITOR
const FText UAsdaAnimatedPlane::GetPaletteCategory()
{
	return NSLOCTEXT("Asda", "AsdaPaletteCategory", "Asda");
}
#endif

void UAsdaAnimatedPlane::RestartAnim()
{
	StartTime = -1.0;
	LastFrame = -1;
	LoopCount = 0;
	bFinished = false;
	if (CanAnimate())
	{
		ApplyFrameUV(FMath::Clamp(StartFrame, 0, FMath::Max(0, TotalFrame - 1)));
	}
	// ★ Обязательно ПОСЛЕ сброса `bFinished`: без этого повторный показ одноразового плана
	//   («шторка перезарядки», «стрелка монет», SOULBALL_ANI) оставался бы без тика.
	EnsureTimer();
}

void UAsdaAnimatedPlane::StopAnim()
{
	StartTime = -1.0;
	LastFrame = -1;
	LoopCount = 0;
	// `bFinished` гасит и УЖЕ ВЗВЕДЁННЫЙ тик: он выйдет по `Stop` молча, без броадкаста события.
	bFinished = true;
	if (CanAnimate())
	{
		ApplyFrameUV(FMath::Clamp(StartFrame, 0, FMath::Max(0, TotalFrame - 1)));
	}
}

bool UAsdaAnimatedPlane::CanAnimate() const
{
	return FrameTexture != nullptr
		&& TotalFrame > 0
		&& FrameIntervalMs > 0.f
		&& FrameSizePx.X > 0.f && FrameSizePx.Y > 0.f;
}

EActiveTimerReturnType UAsdaAnimatedPlane::TickAnim(double InCurrentTime, float /*InDeltaTime*/)
{
	if (!CanAnimate() || bFinished)
	{
		bTimerRegistered = false;              // ★ таймер сейчас снимет Слейт — дать шанс переустановить
		return EActiveTimerReturnType::Stop;   // нет данных/завершено — снять таймер
	}
	if (StartTime < 0.0)
	{
		StartTime = InCurrentTime;
	}

	const int32 MaxIdx = TotalFrame - 1;
	const double ElapsedMs = (InCurrentTime - StartTime) * 1000.0;
	// frameIndex = floor(elapsed/FrameInterval * (TotalFrame-1)) + StartFrame — 1:1 с ui_core.c
	//   L108838-108840 (`... * gap_220[24] + gap_220[20]`, где gap_220[20] = стартовый кадр).
	int32 Frame = (MaxIdx > 0)
		? FMath::FloorToInt((float)(ElapsedMs / (double)FrameIntervalMs) * (float)MaxIdx)
		: 0;
	Frame += FMath::Clamp(StartFrame, 0, FMath::Max(0, MaxIdx));

	if (Frame > MaxIdx)
	{
		// Прогон завершён.
		++LoopCount;
		StartFrame = 0;   // эталон L108893: `gap_220[20] = 0` — стартовый кадр действует только на первый прогон
		// bLoop=true → вечный цикл (эталон: без стоп-флага sub_925D73 maxLoops игнорируется). bLoop=false →
		//   стоп после max(MaxLoops,1) прогонов.
		const int32 Limit = bLoop ? 0 : FMath::Max(MaxLoops, 1);
		if (Limit > 0 && LoopCount >= Limit)
		{
			bFinished = true;
			// One-shot завершён (эталон L108885-108887): СКРЫТЬ контрол + событие «завершено» (evt24), НЕ заморозка.
			SetVisibility(ESlateVisibility::Collapsed);
			bTimerRegistered = false;          // ★ следующий RestartAnim обязан взвести таймер заново
			OnAnimFinished.Broadcast();
			return EActiveTimerReturnType::Stop;
		}
		StartTime = InCurrentTime;   // рестарт цикла
		Frame = 0;
	}

	if (Frame != LastFrame)
	{
		LastFrame = Frame;
		ApplyFrameUV(Frame);
	}
	return EActiveTimerReturnType::Continue;
}

void UAsdaAnimatedPlane::ApplyFrameUV(int32 FrameIndex)
{
	if (!FrameTexture)
	{
		return;
	}
	// ★ ЛОГИЧЕСКИЙ размер: `FrameSizePx` задан в пикселях ИСХОДНОЙ текстуры, а она могла быть
	//   увеличена апскейлом — тогда деление на фактический размер уводит кадры атласа.
	const FIntPoint LogSize = AsdaUi::LogicalAtlasSize(FrameTexture);
	const float TexW = (float)LogSize.X;
	const float TexH = (float)LogSize.Y;
	if (TexW <= 0.f || TexH <= 0.f || FrameSizePx.X <= 0.f || FrameSizePx.Y <= 0.f)
	{
		return;
	}

	const float UStep = (float)FrameSizePx.X / TexW;
	const float VStep = (float)FrameSizePx.Y / TexH;
	const int32 Cols  = FMath::Max(1, FMath::FloorToInt(TexW / (float)FrameSizePx.X));

	int32 Col = FrameIndex % Cols;
	int32 Row = FrameIndex / Cols;
	// Guard переноса строки (ui_core.c L108849): эталон использует ПРАВЫЙ край UV кадра0 (corner3.U = BaseUV.Max.X):
	//   если правый край текущего кадра вышел за текстуру — на новую строку.
	if ((float)Col * UStep + (float)BaseUV.Max.X > 1.0f)
	{
		Col = 0;
		++Row;
	}

	const float DU = (float)Col * UStep;
	const float DV = (float)Row * VStep;
	const FBox2f Uv(
		FVector2f((float)BaseUV.Min.X + DU, (float)BaseUV.Min.Y + DV),
		FVector2f((float)BaseUV.Max.X + DU, (float)BaseUV.Max.Y + DV));

	// Обновляем браш: текстура-спрайт-лист + UV текущего кадра (тот же путь, что статичные JUI-планы порта).
	FSlateBrush B = GetBrush();
	B.SetResourceObject(FrameTexture);
	B.SetUVRegion(Uv);
	SetBrush(B);
}
