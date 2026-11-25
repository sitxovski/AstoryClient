// Copyright (c) 2026 @sitxovski (Discord)
// ★ ЗОНД ПЕРВЫХ КАДРОВ ЗАПУСКА (08.11.2026, пачка 16, баг 12: «при запуске на миллисекунду видна сцена логина до логотипа»).
//   Включается только переменной окружения `ASDA_BOOT_FRAMES=<папка>` (или ключом командной строки `-AsdaBootFrames=<папка>`),
//   в Shipping не собирается. Ничего в игре не меняет — только смотрит:
//     • каждую отрисовку окна Slate (`FSlateRenderer::OnSlateWindowRendered`) — строка журнала: номер, время от старта процесса,
//       кадр движка, мир (карта, начат ли BeginPlay), состояние стартовой заставки (окно `WBP_LoadingScreen` во вьюпорте,
//       непрозрачность, видимость);
//     • каждый кадр движка (`FTSTicker`) — снимок окна ВМЕСТЕ с UI в `<папка>/boot_NNN.png` (как видит игрок), пока не снято
//       `ASDA_BOOT_FRAMES_N` кадров (по умолчанию 40).
//   Подписка ставится в `FCoreDelegates::OnPostEngineInit` — до загрузки первой карты (`GEngine->Start()`), поэтому в журнал
//   попадают и отрисовки, случившиеся до `StartPlay` логина. Разбор кадров — `Reports/batch16/login/`.
#include "CoreMinimal.h"

#if !UE_BUILD_SHIPPING

#include "Blueprint/UserWidget.h"
#include "Containers/Ticker.h"
#include "Engine/Engine.h"
#include "Engine/World.h"
#include "Framework/Application/SlateApplication.h"
#include "HAL/PlatformMisc.h"
#include "HAL/PlatformTime.h"
#include "Misc/CommandLine.h"
#include "Misc/CoreDelegates.h"
#include "Misc/Parse.h"
#include "Misc/Paths.h"
#include "Rendering/SlateRenderer.h"
#include "UnrealClient.h"                      // FScreenshotRequest
#include "UObject/UObjectIterator.h"
#include "Widgets/SWindow.h"

DEFINE_LOG_CATEGORY_STATIC(LogAsdaBootFrames, Log, All);   // журнал зонда

namespace AsdaBootFrames
{
	static FString GDir;                         // папка кадров (пусто — зонд выключен)
	static int32 GMax = 40;                      // сколько кадров снять
	static int32 GTaken = 0;                     // снято (заказано) кадров
	static int32 GRenders = 0;                   // отрисовок окна
	static FTSTicker::FDelegateHandle GTick;     // тикер снимков

	// Состояние стартовой заставки: окно WBP_LoadingScreen во вьюпорте, его непрозрачность и видимость.
	static FString LogoState()
	{
		for (TObjectIterator<UUserWidget> It; It; ++It)
		{
			UUserWidget* U = *It;
			if (!U || !U->GetClass()->GetName().StartsWith(TEXT("WBP_LoadingScreen"))) { continue; }
			if (!U->IsInViewport()) { continue; }
			return FString::Printf(TEXT("заставка=ЕСТЬ непрозр=%.2f вид=%d"), U->GetRenderOpacity(), (int32)U->GetVisibility());
		}
		return TEXT("заставка=нет");
	}

	// Игровой мир: карта и начат ли BeginPlay.
	static FString WorldState()
	{
		if (!GEngine) { return TEXT("мир=нет движка"); }
		for (const FWorldContext& Ctx : GEngine->GetWorldContexts())
		{
			UWorld* W = Ctx.World();
			if (W && W->IsGameWorld())
			{
				return FString::Printf(TEXT("мир=%s начат=%d"), *W->GetMapName(), W->HasBegunPlay() ? 1 : 0);
			}
		}
		return TEXT("мир=нет");
	}

	// Каждая отрисовка окна Slate.
	static void OnWindowRendered(SWindow& Win)
	{
		++GRenders;
		if (GRenders > GMax * 4) { return; }
		UE_LOG(LogAsdaBootFrames, Display, TEXT("[кадры старта] отрисовка #%d t=%.3f кадр=%llu окно=«%s» %s %s"),
			GRenders, FPlatformTime::Seconds() - GStartTime, (unsigned long long)GFrameCounter, *Win.GetTitle().ToString(),
			*WorldState(), *LogoState());
	}

	// Каждый кадр движка: снимок с UI, пока не снято GMax.
	static bool OnTick(float)
	{
		if (FScreenshotRequest::IsScreenshotRequested()) { return true; }
		if (GTaken >= GMax)
		{
			UE_LOG(LogAsdaBootFrames, Display, TEXT("[кадры старта] ГОТОВО: кадров %d, отрисовок окна %d"), GTaken, GRenders);
			return false;
		}
		const FString F = FPaths::Combine(GDir, FString::Printf(TEXT("boot_%03d.png"), GTaken));
		FScreenshotRequest::RequestScreenshot(F, /*bShowUI=*/true, /*bAddFilenameSuffix=*/false);
		UE_LOG(LogAsdaBootFrames, Display, TEXT("[кадры старта] снимок %03d t=%.3f кадр=%llu %s %s"),
			GTaken, FPlatformTime::Seconds() - GStartTime, (unsigned long long)GFrameCounter, *WorldState(), *LogoState());
		++GTaken;
		return true;
	}

	// Включение после инициализации движка (до загрузки первой карты).
	static void Arm()
	{
		FString Dir = FPlatformMisc::GetEnvironmentVariable(TEXT("ASDA_BOOT_FRAMES"));
		FParse::Value(FCommandLine::Get(), TEXT("-AsdaBootFrames="), Dir);
		if (Dir.IsEmpty()) { return; }
		GDir = Dir;
		const FString N = FPlatformMisc::GetEnvironmentVariable(TEXT("ASDA_BOOT_FRAMES_N"));
		if (!N.IsEmpty()) { GMax = FMath::Clamp(FCString::Atoi(*N), 1, 600); }
		if (FSlateApplication::IsInitialized())
		{
			if (FSlateRenderer* R = FSlateApplication::Get().GetRenderer())
			{
				R->OnSlateWindowRendered().AddStatic(&OnWindowRendered);
			}
		}
		GTick = FTSTicker::GetCoreTicker().AddTicker(FTickerDelegate::CreateStatic(&OnTick));
		// Первый снимок заказывается сразу: тикер впервые срабатывает уже ПОСЛЕ первой отрисовки окна (прогон 08.11: кадр 0
		//   отрисован раньше первого заказа) — так первая отрисовка тоже попадает в файл.
		FScreenshotRequest::RequestScreenshot(FPaths::Combine(GDir, TEXT("boot_first.png")), /*bShowUI=*/true, /*bAddFilenameSuffix=*/false);
		UE_LOG(LogAsdaBootFrames, Display, TEXT("[кадры старта] зонд включён: папка %s, кадров %d, t=%.3f %s"),
			*GDir, GMax, FPlatformTime::Seconds() - GStartTime, *WorldState());
	}

	// Подписка на конец инициализации движка — при загрузке модуля.
	struct FRegister
	{
		FRegister() { FCoreDelegates::GetOnPostEngineInit().AddStatic(&Arm); }
	};
	static FRegister GRegister;
}

#endif // !UE_BUILD_SHIPPING
