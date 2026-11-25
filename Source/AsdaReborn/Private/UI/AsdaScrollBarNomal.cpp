// Copyright (c) 2026 @sitxovski (Discord)
// Реализация общей полосы прокрутки `Nomal_Scr` (см. AsdaScrollBarNomal.h — там весь разбор эталона).
#include "UI/AsdaScrollBarNomal.h"
#include "UI/AsdaUiHelpers.h"            // D1: единый хит-тест «курсор над виджетом»
#include "Resources/AsdaTextureSubsystem.h" // закон `resource-subsystems`: текстура только через менеджер
#include "Blueprint/UserWidget.h"
#include "Components/CanvasPanel.h"
#include "Components/CanvasPanelSlot.h"
#include "Components/Image.h"
#include "Engine/Texture2D.h"
#include "Engine/Engine.h"
#include "Framework/Application/SlateApplication.h"

DEFINE_LOG_CATEGORY_STATIC(LogAsdaScrollBar, Log, All);

bool FAsdaScrollBarNomal::Build(UUserWidget* Owner, UCanvasPanel* HostCanvas, const TCHAR* NamePrefix, int32 ZTrack)
{
	if (Track) { return true; }                       // уже собрана
	if (!Owner || !HostCanvas) { return false; }

	// ★ ЗАКОН `resource-subsystems`: атлас — только через менеджер текстур (кэш + refCount).
	//   Фолбэка прямым `LoadObject` здесь НЕТ (это был обход менеджера, B3 аудита): если менеджер
	//   не отдал текстуру — это ошибка данных, её и логируем.
	UAsdaTextureSubsystem* TexMgr = GEngine ? GEngine->GetEngineSubsystem<UAsdaTextureSubsystem>() : nullptr;
	UTexture2D* Atlas = TexMgr ? TexMgr->LoadTextureCached(AsdaNomalScr::kAtlasPath) : nullptr;
	if (!Atlas)
	{
		UE_LOG(LogAsdaScrollBar, Warning,
			TEXT("[полоса %s] атлас %s не отдан менеджером текстур — полосу не строим "
			     "(закон no-invented-ui: рисовать её «своим» цветом нельзя)"),
			NamePrefix ? NamePrefix : TEXT("?"), AsdaNomalScr::kAtlasPath);
		return false;
	}

	// Один план полосы. Тинт кисти = Diffuse плана из скина (дорожка FFFFFFFF, ↑/↓/бегунок 64FFFFFF —
	//   см. шапку `AsdaNomalScr`): оригинал умножает тексель на вершинный цвет и смешивает по альфе.
	//   `ColorAndOpacity` виджета — белый: второй раз цвет не умножаем (грабля `ui-progressbar-animate-existing`).
	auto MakePlane = [Owner, HostCanvas, Atlas](const FString& Name, const FBox2f& Uv, uint32 DiffuseArgb, int32 Z) -> UImage*
	{
		UImage* Img = NewObject<UImage>(Owner, UImage::StaticClass(),
			MakeUniqueObjectName(Owner, UImage::StaticClass(), FName(*Name)));
		if (!Img) { return nullptr; }
		FSlateBrush B;
		B.SetResourceObject(Atlas);
		B.DrawAs = ESlateBrushDrawType::Image;
		B.SetUVRegion(Uv);
		B.ImageSize = FVector2D(AsdaNomalScr::kWidth, AsdaNomalScr::kButton);
		B.TintColor = FSlateColor(AsdaNomalScr::DiffuseToTint(DiffuseArgb));
		Img->SetBrush(B);
		Img->SetColorAndOpacity(FLinearColor::White);
		// Хит-тест ведём сами (опросом курсора в `Tick`), поэтому виджету мышь не нужна — иначе он
		//   перехватит клик у общего слоя окна.
		Img->SetVisibility(ESlateVisibility::SelfHitTestInvisible);
		if (UCanvasPanelSlot* CS = HostCanvas->AddChildToCanvas(Img))
		{
			CS->SetAnchors(FAnchors(0.f, 0.f));
			CS->SetAlignment(FVector2D(0.f, 0.f));
			CS->SetAutoSize(false);
			CS->SetZOrder(Z);
		}
		return Img;
	};

	const FString Pre = NamePrefix ? FString(NamePrefix) : FString(TEXT("Scr"));
	Track = MakePlane(Pre + TEXT("_Track"), AsdaNomalScr::UvTrack(), AsdaNomalScr::kDiffuseTrack,  ZTrack);
	Thumb = MakePlane(Pre + TEXT("_Thumb"), AsdaNomalScr::UvThumb(), AsdaNomalScr::kDiffuseButton, ZTrack + 1);
	Up    = MakePlane(Pre + TEXT("_Up"),    AsdaNomalScr::UvUp(),    AsdaNomalScr::kDiffuseButton, ZTrack + 2);
	Down  = MakePlane(Pre + TEXT("_Down"),  AsdaNomalScr::UvDown(),  AsdaNomalScr::kDiffuseButton, ZTrack + 2);
	DownUvNormal = AsdaNomalScr::UvDown();
	PressedPlane = -1;
	bThumbGrab = false;
	UE_LOG(LogAsdaScrollBar, Log,
		TEXT("[полоса %s] собрана из скина Nomal_Scr (4 плана, атлас n_01)"), *Pre);
	return Track != nullptr;
}

bool FAsdaScrollBarNomal::Adopt(UImage* InTrack, UImage* InUp, UImage* InDown, UImage* InThumb)
{
	// Планы из ассета: все четыре есть и лежат в одном канвасе (координаты `Layout` — его координаты).
	if (!InTrack || !InUp || !InDown || !InThumb) { return false; }
	const UCanvasPanelSlot* S0 = AsdaUi::SlotOf(InTrack);
	if (!S0 || !AsdaUi::SlotOf(InUp) || !AsdaUi::SlotOf(InDown) || !AsdaUi::SlotOf(InThumb)) { return false; }
	if (InUp->GetParent() != InTrack->GetParent() || InDown->GetParent() != InTrack->GetParent()
		|| InThumb->GetParent() != InTrack->GetParent())
	{
		return false;
	}
	Track = InTrack; Up = InUp; Down = InDown; Thumb = InThumb;
	// ★ Волна 13 (М-5): обычная UV стрелки ↓ — из ассета (могла быть правка владельца), не из констант.
	{
		const FBox2D R = InDown->GetBrush().GetUVRegion();
		DownUvNormal = FBox2f(FVector2f((float)R.Min.X, (float)R.Min.Y), FVector2f((float)R.Max.X, (float)R.Max.Y));
	}
	PressedPlane = -1;
	bThumbGrab = false;
	UE_LOG(LogAsdaScrollBar, Log, TEXT("[полоса %s] взята из ассета окна (4 плана скина Nomal_Scr)"), *InTrack->GetName());
	return true;
}

// Локальный помощник видимости: полоса не должна отбирать клик у мира (мышь ловим опросом).
// ★ Волна 1 (14.09.2026, В3): правило видимости — общая точка `AsdaUi::SetShownDecor` (режим тот же).
static void ScrSetVis(UImage* W, bool bOn)
{
	AsdaUi::SetShownDecor(W, bOn);
}

void FAsdaScrollBarNomal::Hide()
{
	ScrSetVis(Track, false); ScrSetVis(Up, false);
	ScrSetVis(Down, false);  ScrSetVis(Thumb, false);
	bThumbGrab = false;
	BarHeight = 0.f; ThumbLen = 0.f; ThumbTop = 0.f;
}

// ★ Волна 14 (МЕЛК-1 ревизии волны 13): длина бегунка — ТОЧНО float-арифметикой эталона (разбор — у объявления).
//   `float_control(precise)`: компилятору нельзя ни переставлять операции, ни заменять деление на 100
//   умножением на 0.01 — игровые (не редакторские) сборки UE идут с /fp:fast, и 1 px ушёл бы обратно.
#if defined(_MSC_VER) || defined(__clang__)
#pragma float_control(precise, on, push)
#endif
int32 FAsdaScrollBarNomal::EtalonThumbLen(int32 LenJ, int32 RangeA2, int32 RangeA3)
{
	// Сюда эталон попадает только при `a3 − a2 > 0` и `a2 != a3` (иначе ветка скрытия бегунка), так что
	//   a3 == 0 возможно лишь при отрицательном a2 — делить на ноль всё равно не даём.
	if (RangeA3 == 0) { return (int32)AsdaNomalScr::kThumbMin; }
	const float V15 = ((float)RangeA2 * 100.0f) / (float)RangeA3;   // fild a2 · fmul 100.0f · fidiv a3 · fstp dword
	const float T   = ((float)LenJ * V15) / 100.0f;                  // fild len · fmul v15 · fdiv 100.0f
	int32 R = (int32)T;                                               // `_ftol@0xFF1BB8`: усечение к нулю
	if (R < (int32)AsdaNomalScr::kThumbMin) { R = (int32)AsdaNomalScr::kThumbMin; }   // if (+628 < 6) +628 = 6
	return R;
}
#if defined(_MSC_VER) || defined(__clang__)
#pragma float_control(pop)
#endif

// Раскладка для `Layout` (★ волна 15: форма одна, прежняя с долей удалена): планы по `uiScrollbarLayoutPlanes`
//   и бегунок по `uiScrollbarLayoutThumb`.
//   `ThumbLenScaled` — уже посчитанная длина бегунка в единицах хост-канваса (0 — бегунка нет).
static void AsdaScrLayoutPlanes(FAsdaScrollBarNomal& Bar, float S, float TrackLen, float ThumbLenScaled,
                                int32 MaxRow, int32 Pos)
{
	const float W   = AsdaNomalScr::kWidth  * S;
	const float Btn = AsdaNomalScr::kButton * S;
	Bar.ThumbLen = ThumbLenScaled;
	Bar.ThumbTop = Btn;
	if (Bar.ThumbLen > 0.f)
	{
		// `uiScrollbarLayoutThumb`: верх = стрелка + (дорожка − бегунок)·P/max; при P = max низ бегунка = низ дорожки.
		const float Den = TrackLen - Bar.ThumbLen;
		const int32 P   = FMath::Clamp(Pos, 0, MaxRow);
		Bar.ThumbTop = Btn + ((Den > 0.f) ? Den * (float)P / (float)MaxRow : 0.f);
	}

	auto Place = [&Bar, W](UImage* Img, float LocalY, float H)
	{
		if (UCanvasPanelSlot* CS = Img ? AsdaUi::SlotOf(Img) : nullptr)
		{
			CS->SetOffsets(FMargin(Bar.Origin.X, Bar.Origin.Y + LocalY, W, H));
		}
	};
	Place(Bar.Up,    0.f,                 Btn);
	Place(Bar.Track, Btn,                 TrackLen);
	Place(Bar.Down,  Bar.BarHeight - Btn, Btn);
	Place(Bar.Thumb, Bar.ThumbTop,        FMath::Max(1.f, Bar.ThumbLen));

	ScrSetVis(Bar.Up, true);
	ScrSetVis(Bar.Down, true);
	ScrSetVis(Bar.Track, true);
	// `uiScrollbarSetRange`: при `max <= 0` флаг +632 снимается и бегунок схлопывается в ноль —
	//   стрелки и дорожка при этом остаются на экране. Воспроизводим ровно это.
	ScrSetVis(Bar.Thumb, Bar.ThumbLen > 0.f);
}

void FAsdaScrollBarNomal::Layout(const FVector2D& InOrigin, float InBarHeight, float Scale,
                                 int32 RangeA2, int32 RangeA3, int32 MaxRow, int32 Pos)
{
	if (!Track) { return; }
	const float S = (Scale > 0.f) ? Scale : 1.f;
	Origin    = InOrigin;
	BarHeight = InBarHeight;
	const float TrackLen = FMath::Max(1.f, BarHeight - 2.f * AsdaNomalScr::kButton * S);
	float Len = 0.f;
	if (MaxRow > 0 && RangeA3 > 0)
	{
		// ★ Волна 14 (МЕЛК-2): эталон считает в пикселях `.JUI`, масштаб порта — множитель ПОСЛЕ отбрасывания:
		//   S·EtalonThumbLen(len/S), а не ⌊len·S·f⌋. Длина дорожки в `.JUI` целая — возвращаем её к целому.
		const int32 LenJ = FMath::RoundToInt(TrackLen / S);
		const int32 ThumbJ = EtalonThumbLen(LenJ, RangeA2, RangeA3);
		Len = (float)ThumbJ * S;
		// Сверка с эталоном без правки кода: -LogCmds="LogAsdaScrollBar VeryVerbose".
		UE_LOG(LogAsdaScrollBar, VeryVerbose,
			TEXT("[полоса %s] SetRange(%d, %d): дорожка %d px .JUI ×%.3f → бегунок %d px .JUI, макс %d, позиция %d"),
			*Track->GetName(), RangeA2, RangeA3, LenJ, S, ThumbJ, MaxRow, Pos);
	}
	AsdaScrLayoutPlanes(*this, S, TrackLen, Len, MaxRow, Pos);
}

bool FAsdaScrollBarNomal::IsCursorOver() const
{
	if (!Track) { return false; }
	return AsdaUi::IsCursorOverWidget(Up)    || AsdaUi::IsCursorOverWidget(Down)
	    || AsdaUi::IsCursorOverWidget(Thumb) || AsdaUi::IsCursorOverWidget(Track);
}

EAsdaScrollAction FAsdaScrollBarNomal::Tick(bool bLmb, bool bLmbDownEdge, int32 MaxRow, int32& OutValue)
{
	OutValue = 0;
	if (!Track || !FSlateApplication::IsInitialized())
	{
		bThumbGrab = false;
		PressedPlane = -1;
		return EAsdaScrollAction::None;
	}

	// Состояние SS2 («нажата») во всём скине есть только у стрелки ↓ — только её UV и переключаем.
	//   ★ Волна 13 (М-5): база — UV из кисти плана (`DownUvNormal`), нажатая = база + сдвиг SS2−SS0 скина.
	auto Press = [this](int32 Plane)
	{
		if (PressedPlane == Plane) { return; }
		PressedPlane = Plane;
		if (Down)
		{
			const FVector2f Shift = AsdaNomalScr::UvDownPressed().Min - AsdaNomalScr::UvDown().Min;
			FSlateBrush B = Down->GetBrush();
			B.SetUVRegion(Plane == 2 ? FBox2f(DownUvNormal.Min + Shift, DownUvNormal.Max + Shift) : DownUvNormal);
			Down->SetBrush(B);
		}
	};

	const FVector2D Cur = FSlateApplication::Get().GetCursorPos();

	// ── бегунок схвачен (латч +608 эталона держится до отпускания ЛКМ) ──
	if (bThumbGrab)
	{
		if (!bLmb) { bThumbGrab = false; Press(-1); return EAsdaScrollAction::None; }
		const FGeometry& TrG = Track->GetCachedGeometry();
		const float TrackTopAbs = (float)TrG.GetAbsolutePosition().Y;
		const float TrackLenAbs = (float)(TrG.GetLocalSize().Y * TrG.Scale);
		const FGeometry& ThG = Thumb ? Thumb->GetCachedGeometry() : TrG;
		const float ThumbLenAbs = (float)(ThG.GetLocalSize().Y * ThG.Scale);
		const float Den = TrackLenAbs - ThumbLenAbs;
		if (Den > 0.f && MaxRow > 0)
		{
			// ЭТАЛОН: бегунок ЦЕНТРИРУЕТСЯ под курсором (`mouseY -= thumbHeight >> 1`), точки
			//   захвата нет; результат усекается к нулю (машинный cvttsd2si) и клампится [0, max].
			const float Y = (float)Cur.Y - TrackTopAbs - ThumbLenAbs * 0.5f;
			OutValue = FMath::Clamp(FMath::TruncToInt((float)MaxRow * (Y / Den)), 0, MaxRow);
			return EAsdaScrollAction::SetRow;
		}
		return EAsdaScrollAction::None;
	}

	if (!bLmb) { Press(-1); }
	if (!bLmbDownEdge) { return EAsdaScrollAction::None; }

	// ── план 1: строка вверх (событие 69633) ──
	if (AsdaUi::IsCursorOverWidget(Up))   { Press(1); OutValue = -1; return EAsdaScrollAction::ScrollBy; }
	// ── план 2: строка вниз (событие 69634) ──
	if (AsdaUi::IsCursorOverWidget(Down)) { Press(2); OutValue = +1; return EAsdaScrollAction::ScrollBy; }
	// ── план 3: захват бегунка (латч, дальше 69637) ──
	if (ThumbLen > 0.f && AsdaUi::IsCursorOverWidget(Thumb)) { Press(3); bThumbGrab = true; return EAsdaScrollAction::None; }
	// ── план 0: дорожка — «страница» вверх/вниз (69635 / 69636). Эталон сравнивает курсор с НИЖНЕЙ
	//    границей бегунка: выше неё → вверх, ниже → вниз (внутри бегунка ветка не выполняется вовсе).
	if (AsdaUi::IsCursorOverWidget(Track))
	{
		Press(0);
		if (ThumbLen <= 0.f || !Thumb) { return EAsdaScrollAction::None; }
		const FGeometry& ThG = Thumb->GetCachedGeometry();
		const float ThumbBottomAbs = (float)(ThG.GetAbsolutePosition().Y + ThG.GetLocalSize().Y * ThG.Scale);
		OutValue = ((float)Cur.Y <= ThumbBottomAbs) ? -1 : +1;
		return EAsdaScrollAction::ScrollBy;
	}
	return EAsdaScrollAction::None;
}

FString FAsdaScrollBarNomal::Dump() const
{
	return FString::Printf(
		TEXT("планы: дорожка=%s ↑=%s ↓=%s бегунок=%s\n")
		TEXT("позиция (в хост-канвасе): (%.0f, %.0f) %.0f×%.0f · бегунок длина=%.0f верх=%.0f · захват: %s\n"),
		Track ? TEXT("да") : TEXT("НЕТ"), Up ? TEXT("да") : TEXT("НЕТ"),
		Down ? TEXT("да") : TEXT("НЕТ"), Thumb ? TEXT("да") : TEXT("НЕТ"),
		Origin.X, Origin.Y, AsdaNomalScr::kWidth, BarHeight, ThumbLen, ThumbTop,
		bThumbGrab ? TEXT("да") : TEXT("нет"));
}
