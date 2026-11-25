// Copyright (c) 2026 @sitxovski (Discord)
// Общая инфраструктура «сцена в render-target» — см. шапку `UI/AsdaPreviewSceneActor.h`.
// ★ ЗДЕСЬ НЕТ НИ ОДНОГО ЧИСЛА, СВОЙСТВЕННОГО КОНКРЕТНОЙ СЦЕНЕ: размер, камера и состав —
//   дело наследников (гача и превью питомца), потому что в оригинале это ДВА РАЗНЫХ RT.
#include "UI/AsdaPreviewSceneActor.h"
#include "UI/AsdaCaptureGradeIsolation.h"   // ★ 05.11.2026: ч/б смерти не ложится на 3D-окна (закрепление насыщенности захвата)
#include "AsdaDevConsole.h"                  // ASDA_DEV_CVAR — дев-ключ `asda.Death.DesatWorldOnly` (в Shipping константа)
#include "Components/SceneCaptureComponent2D.h"
#include "Engine/TextureRenderTarget2D.h"
#include "TextureResource.h"   // FTextureRenderTargetResource::ReadPixels — чтение кадра для диагностики
#include "Materials/MaterialInterface.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "HAL/IConsoleManager.h"   // проверка r.PostProcessing.PropagateAlpha: без неё альфа кадра мертва

DEFINE_LOG_CATEGORY_STATIC(LogAsdaPreviewScene, Log, All);

// ★★★ 05.11.2026 [ИСПРАВЛЕНИЕ] Разбор — `UI/AsdaCaptureGradeIsolation.h`. Дев-ключ (в Shipping — константа 1 без имени); другие
//   .cpp читают его только через `AsdaCaptureGrade::Enabled()`.
static ASDA_DEV_CVAR(int32, CVarDeathDesatWorldOnly,
	TEXT("asda.Death.DesatWorldOnly"), 1,
	TEXT("[ИСПРАВЛЕНИЕ] 1 — ч/б фильтр смерти (и любой объём пост-обработки с насыщенностью) не обесцвечивает 3D-окна, "
	     "рисуемые захватом (сцена премиум-гачи, превью питомца); 0 — прежний порт (окна серели вместе с миром)."));

bool AsdaCaptureGrade::Enabled()
{
	return CVarDeathDesatWorldOnly.GetValueOnGameThread() != 0;
}

void AsdaCaptureGrade::PinSaturation(USceneCaptureComponent2D* Capture)
{
	if (!Capture) { return; }
	FPostProcessSettings& PP = Capture->PostProcessSettings;
	// CDO и шаблоны создаются до старта движка консоли — там читаем умолчание ключа (1), это то же значение.
	const bool bOn = IsInGameThread() ? Enabled() : true;
	PP.bOverride_ColorSaturation = bOn;
	PP.ColorSaturation = FVector4(1.f, 1.f, 1.f, 1.f);
	if (bOn && Capture->PostProcessBlendWeight < 1.f) { Capture->PostProcessBlendWeight = 1.f; }
}

AAsdaPreviewSceneActor::AAsdaPreviewSceneActor()
{
	PrimaryActorTick.bCanEverTick = false;
	USceneComponent* SceneRoot = CreateDefaultSubobject<USceneComponent>(TEXT("Root"));
	SetRootComponent(SceneRoot);

	// ОДИН капчер: `SCS_FinalColorLDR` при включённом `r.PostProcessing.PropagateAlpha` даёт
	//   и тонмаппленный цвет, и настоящую альфу. Настройки ниже ОДИНАКОВЫ у обеих сцен эталона.
	Capture = CreateDefaultSubobject<USceneCaptureComponent2D>(TEXT("Capture"));
	Capture->SetupAttachment(SceneRoot);
	if (USceneCaptureComponent2D* C = Capture.Get())
	{
		C->bCaptureEveryFrame = true;
		C->bCaptureOnMovement = true;
		C->bAlwaysPersistRenderingState = true;
		// ★ В кадр попадает ТОЛЬКО то, что положили в список: в оригинале это отдельный
		//   render-target, мира в нём нет.
		C->PrimitiveRenderMode = ESceneCapturePrimitiveRenderMode::PRM_UseShowOnlyList;
		C->ShowFlags.SetAtmosphere(false);
		C->ShowFlags.SetFog(false);
		C->ShowFlags.SetVolumetricFog(false);
		C->bOverride_CustomNearClippingPlane = true;
		C->CustomNearClippingPlane = NearClip;
		AsdaCaptureGrade::PinSaturation(C);   // ★ 05.11.2026 [ИСПРАВЛЕНИЕ]: объёмы мира (ч/б смерти) не трогают насыщенность окна
	}
}

void AAsdaPreviewSceneActor::SetCaptureEnabled(bool bEnabled)
{
	if (USceneCaptureComponent2D* C = Capture.Get())
	{
		AsdaCaptureGrade::PinSaturation(C);   // ★ 05.11.2026: перечитать ключ `asda.Death.DesatWorldOnly` на каждом включении
		// ⚠ Сам актор и RT НЕ уничтожаем — только перестаём жечь GPU. Так делает и эталон:
		//   при обычном закрытии окна RT не освобождается, он переживает закрытие
		//   (`releaseThreeComObjects` зовётся лишь при device lost и переинициализации окна).
		C->bCaptureEveryFrame = bEnabled;
	}
}

// ЧИСЛОВОЕ ЯДРО метрики охвата. Разбор и предупреждения — у объявления в `.h`.
bool AAsdaPreviewSceneActor::MeasureFrameCoverage(FIntRect& OutRect, int32& OutHits,
	FIntPoint& OutSize, int32 Threshold) const
{
	OutRect = FIntRect(0, 0, 0, 0);
	OutHits = 0;
	OutSize = RtSize;
	if (!RT) { return false; }
	FTextureRenderTargetResource* Res = RT->GameThread_GetRenderTargetResource();
	if (!Res) { return false; }
	TArray<FColor> Px;
	if (!Res->ReadPixels(Px) || Px.Num() < 1) { return false; }
	const int32 W = RtSize.X;
	const int32 H = RtSize.Y;
	if (Px.Num() < W * H) { return false; }
	// ★★★ ПОПРАВКА РЕВИЗИИ 25.09.2026 (СРЕДНЕЕ-4): ФОН — ПО ЧЕТЫРЁМ УГЛАМ, А НЕ ПО ОДНОМУ.
	//   БОЛЕЗНЬ. Фон брался из `Px[0]` — ОДНОГО пикселя левого верхнего угла. Как только модель
	//   вырастала на весь кадр и накрывала этот угол, метрика ИНВЕРТИРОВАЛАСЬ: «фоном» объявлялся
	//   сам питомец, и охват показывал 99.55 % «непустых» при фоне RGBA(73,79,72,0) — то есть
	//   ровно наоборот. На такой метрике автоподбор камеры принимал решения вслепую.
	//   ЛЕЧЕНИЕ. Опрашиваем ЧЕТЫРЕ угла и берём цвет, который встречается чаще (при равенстве —
	//   левый верхний, как было). Дополнительно отдаём признак `bOutBgAmbiguous`: все четыре угла
	//   разные ⇒ фон определить нечем, и читатель обязан это знать, а не верить числу
	//   (закон `false-negatives-method`: метрика, которая не умеет сказать «не знаю», врёт).
	const FColor Corners[4] = { Px[0], Px[W - 1], Px[(H - 1) * W], Px[H * W - 1] };
	auto SameColor = [](const FColor& A, const FColor& B)
	{
		return A.R == B.R && A.G == B.G && A.B == B.B && A.A == B.A;
	};
	FColor Bg = Corners[0];
	int32 BestVotes = 0;
	for (int32 i = 0; i < 4; ++i)
	{
		int32 Votes = 0;
		for (int32 j = 0; j < 4; ++j) { if (SameColor(Corners[i], Corners[j])) { ++Votes; } }
		if (Votes > BestVotes) { BestVotes = Votes; Bg = Corners[i]; }
	}
	BgColorLastMeasure = Bg;
	BgAmbiguousLastMeasure = (BestVotes < 2);   // ни один угол не повторился — фона не видно
	int32 MinX = W, MinY = H, MaxX = -1, MaxY = -1;
	for (int32 Y = 0; Y < H; ++Y)
	{
		for (int32 X = 0; X < W; ++X)
		{
			const FColor& C = Px[Y * W + X];
			const int32 D = FMath::Abs(int32(C.R) - int32(Bg.R))
			              + FMath::Abs(int32(C.G) - int32(Bg.G))
			              + FMath::Abs(int32(C.B) - int32(Bg.B))
			              + FMath::Abs(int32(C.A) - int32(Bg.A));
			if (D <= Threshold) { continue; }
			++OutHits;
			MinX = FMath::Min(MinX, X); MaxX = FMath::Max(MaxX, X);
			MinY = FMath::Min(MinY, Y); MaxY = FMath::Max(MaxY, Y);
		}
	}
	if (OutHits > 0) { OutRect = FIntRect(MinX, MinY, MaxX, MaxY); }
	return true;
}

FString AAsdaPreviewSceneActor::DescribeFrameCoverage(int32 Threshold) const
{
	// Разбор метода и предупреждения — у объявления в `.h`. ТЕЛО ОДНО с `MeasureFrameCoverage`:
	//   здесь только словесная обёртка над его числами (закон `check-existing-before-creating`).
	FIntRect R; int32 Hits = 0; FIntPoint Sz(0, 0);
	if (!RT) { return TEXT("буфера нет"); }
	if (!MeasureFrameCoverage(R, Hits, Sz, Threshold))
	{
		return TEXT("пиксели не прочитались (ресурс буфера не создан или размер не сошёлся)");
	}
	const FColor Bg = BgColorLastMeasure;
	const float Pct = (Sz.X * Sz.Y) > 0 ? (100.f * float(Hits) / float(Sz.X * Sz.Y)) : 0.f;
	// ★ Признак «фон определить нечем» печатаем ВСЕГДА, когда он взведён: без него число
	//   выглядит достоверным, а оно в этот момент может быть инвертированным (см. СРЕДНЕЕ-4).
	const TCHAR* Amb = BgAmbiguousLastMeasure
		? TEXT(" ⚠ ФОН НЕЯСЕН (все четыре угла разные) — числу верить нельзя") : TEXT("");
	if (Hits == 0)
	{
		return FString::Printf(TEXT("буфер %dx%d, фон RGBA(%d,%d,%d,%d), НЕПУСТЫХ ПИКСЕЛЕЙ НЕТ (0.00%%) — В КАДРЕ ПУСТО%s"),
			Sz.X, Sz.Y, Bg.R, Bg.G, Bg.B, Bg.A, Amb);
	}
	return FString::Printf(
		TEXT("буфер %dx%d, фон RGBA(%d,%d,%d,%d), непустых %d (%.2f%%), охват (%d,%d)-(%d,%d) = %dx%d%s"),
		Sz.X, Sz.Y, Bg.R, Bg.G, Bg.B, Bg.A, Hits, Pct, R.Min.X, R.Min.Y, R.Max.X, R.Max.Y,
		R.Max.X - R.Min.X + 1, R.Max.Y - R.Min.Y + 1, Amb);
}

bool AAsdaPreviewSceneActor::EnsureRenderTargetSized(FIntPoint WantRt, FIntPoint WantScreen)
{
	// Защита от вырожденных размеров: нулевой RT движок создать не даст.
	WantRt.X = FMath::Max(WantRt.X, 1);
	WantRt.Y = FMath::Max(WantRt.Y, 1);
	WantScreen.X = FMath::Max(WantScreen.X, 1);
	WantScreen.Y = FMath::Max(WantScreen.Y, 1);
	if (RT && RtSize == WantRt && ScreenSize == WantScreen)
	{
		return false;
	}
	ScreenSize = WantScreen;
	RtSize = WantRt;

	// ⚠ СОЗДАНИЕ БУФЕРА ВОЗВРАЩЕНО К ВИДУ ВОЛНЫ 9 (24.09.2026). В ходе разбора дефекта «превью
	//   пустое» здесь пробовались и штатная фабрика `UKismetRenderingLibrary::CreateRenderTarget2D`,
	//   и sRGB-формат, и линейная гамма — НИ ОДИН вариант картинку в контроле не изменил, а риск
	//   задеть ВТОРОГО потребителя этого класса (сцена гачи) реальный. Поэтому оставлено как было;
	//   почему кадр не доходил до контрола на самом деле (UV-регион кисти) — в шапке `.h`.
	UTextureRenderTarget2D* T = NewObject<UTextureRenderTarget2D>(this);
	T->RenderTargetFormat = ETextureRenderTargetFormat::RTF_RGBA8;
	// Очистка кадра в оригинале: `Clear(…, D3DCLEAR_TARGET|D3DCLEAR_ZBUFFER, 0x00000000, 1.0f, 0)`
	//   — у ОБЕИХ сцен (`npc_mob_pet.c:159` у гачи и `:5654` у питомца) ⇒ фон полностью прозрачный.
	T->ClearColor = FLinearColor::Transparent;
	T->bAutoGenerateMips = false;
	T->bForceLinearGamma = false;
	T->InitAutoFormat(RtSize.X, RtSize.Y);
	T->UpdateResourceImmediate(true);
	RT = T;
	// ★ По этим числам видно, каким движок объявит буфер материалу склейки (тип сэмплера).
	//   Без них разбор «почему кадр не виден» идёт вслепую.
	UE_LOG(LogAsdaPreviewScene, Log, TEXT("[превью-сцена] буфер %dx%d создан: SRGB=%d, линейная гамма=%d"),
		RtSize.X, RtSize.Y, RT->SRGB ? 1 : 0, RT->bForceLinearGamma ? 1 : 0);
	if (Capture) { Capture->TextureTarget = RT; }

	// ★★★ Альфа в кадре живёт ТОЛЬКО при включённом альфа-канале пост-обработки. Без него
	//   `SCS_FinalColorLDR` отдаёт альфу 255 везде, и кадр закроет окно чёрным прямоугольником.
	if (const IConsoleVariable* CVarAlpha = IConsoleManager::Get().FindConsoleVariable(TEXT("r.PostProcessing.PropagateAlpha")))
	{
		if (CVarAlpha->GetInt() == 0)
		{
			UE_LOG(LogAsdaPreviewScene, Error,
				TEXT("[превью-сцена] r.PostProcessing.PropagateAlpha = 0 — альфа кадра будет 255 везде ")
				TEXT("и сцена закроет контрол непрозрачным прямоугольником. Включите её в DefaultEngine.ini."));
		}
	}

	// Кисть: цвет и альфа — из ОДНОГО RT (`Opacity = 1 − SceneColor.A`).
	if (UMaterialInterface* Master = Cast<UMaterialInterface>(SceneBrushMaterialPath.TryLoad()))
	{
		BrushMaster = Master;   // ★ 24.09: нужен каноническому пути UMG — см. `GetSceneBrushMaster`
		BrushMID = UMaterialInstanceDynamic::Create(Master, this);
		BrushMID->SetTextureParameterValue(TEXT("SceneColor"), RT);
	}
	else
	{
		UE_LOG(LogAsdaPreviewScene, Warning,
			TEXT("[превью-сцена] нет материала %s — без него фон кадра будет непрозрачным"),
			*SceneBrushMaterialPath.ToString());
	}
	return true;
}
