// Copyright (c) 2026 @sitxovski (Discord)
// ★★★ САМОТЕСТ ЗАТЕМНЕНИЯ ЭКРАНА СОЗДАНИЯ ПЕРСОНАЖА — `asda.Login.SelfTestCreateDim [shots=<папка>] [delay=N] [quit]`
//   (08.11.2026, пачка 16, баг 10: «при создании персонажа нет затемнения фона за персонажем и окном создания»).
//   Путь игрока: офлайн-экран выбора (`asda.Login.CharSelectOffline`) → экран создания (`ShowCharCreateUI`, как кнопка
//   CARASTART_BUT04). Для каждого режима ключа `asda.Login.CreateDimPlace` (1 — по умолчанию, 2 — как в эталоне, 0 — как
//   было до правки) превью пересобирается штатным `SpawnCreatePreview`, затем три независимые проверки:
//     1) геометрия: лучи камеры через центр и 4 угла кадра пересекают плоскость квада ПЕРЕД камерой, ДАЛЬШЕ персонажа и
//        внутри прямоугольника меша (в локальных осях компонента; прямоугольник — bounds меша);
//     2) отрисовка: `WasRecentlyRendered` у компонента квада;
//     3) пиксели: кадр сцены без UI с квадом и с временно спрятанным квадом (снимок без UI в файл, чтение PNG) —
//        отношение средней яркости фона вне рамки персонажа и доля пикселей персонажа, не изменившихся по яркости.
//   Ожидания: режим 1 — всё закрыто, фон ≤ 0.6 яркости, персонаж не темнеет; режим 0 — плоскость не перед камерой и фон
//   не темнеет (доказательство, что ключ 0 = поведение до правки); режим 2 — только журнал (зависит от камеры сцены).
//   `shots=<папка>` — кадры с UI `create_dim_mode<N>.png` для отчёта.
#include "CoreMinimal.h"
#include "Test/AsdaSelfTestCtx.h"   // ★ R5-c (12.11.2026): общая точка самотестов вместо своих копий помощников

#if !UE_BUILD_SHIPPING

#include "Login/AsdaLoginGameMode.h"
#include "Test/AsdaSelfTestKit.h"
#include "AsdaCVarSave.h"
#include "Components/StaticMeshComponent.h"
#include "Engine/StaticMesh.h"
#include "Engine/Engine.h"
#include "Engine/GameViewportClient.h"
#include "Engine/World.h"
#include "GameFramework/PlayerController.h"
#include "HAL/IConsoleManager.h"
#include "Misc/Paths.h"
#include "UnrealClient.h"                      // FScreenshotRequest
#include "TimerManager.h"
#include "ImageUtils.h"                       // FImageUtils::LoadImage — пиксели снимка из файла
#include "ImageCore.h"                        // FImage
#include "HAL/FileManager.h"

DEFINE_LOG_CATEGORY_STATIC(LogAsdaCreateDim, Log, All);   // журнал самотеста

// Доступ самотеста к закрытому состоянию экрана создания (friend в AsdaLoginGameMode.h).
struct FAsdaCreateDimAccess
{
	static void Show(AAsdaLoginGameMode* G) { G->ShowCharCreateUI(0); }               // открыть экран создания (как CARASTART_BUT04)
	static void Respawn(AAsdaLoginGameMode* G) { G->SpawnCreatePreview(); }           // штатная пересборка превью и квада
	static AActor* Dim(AAsdaLoginGameMode* G) { return G->CreateDimActor; }           // актор квада затемнения
	static AActor* Char(AAsdaLoginGameMode* G) { return G->CreatePreviewActor; }      // создаваемый персонаж
	static bool OnCreate(AAsdaLoginGameMode* G) { return G->CurrentScreen == EAsdaScreen::CharCreate; }   // экран создания открыт
};

namespace AsdaCreateDimLocal
{
	using FAcc = FAsdaCreateDimAccess;
	static const TCHAR* kCmd = TEXT("asda.Login.SelfTestCreateDim");   // имя команды
	static const TCHAR* kKey = TEXT("asda.Login.CreateDimPlace");      // ключ расстановки квада
	static const int32 kModes[] = { 1, 2, 0 };                         // порядок режимов: по умолчанию, эталон, как было

	// Состояние прогона.
	struct FCtx
	{
		TWeakObjectPtr<UWorld> W;                 // мир
		TWeakObjectPtr<AAsdaLoginGameMode> G;     // режим логина
		bool bQuit = false;                       // выйти после итога
		FString ShotDir;                          // папка кадров с UI (shots=)
		int32 Ok = 0, Fail = 0;                   // счётчики
		int32 ModeIdx = 0;                        // индекс режима в kModes
		TArray<FColor> PixOn, PixOff;             // кадр сцены с квадом и без
		FIntPoint Size = FIntPoint::ZeroValue;    // размер кадра
		FBox2D CharBox = FBox2D(ForceInit);       // рамка персонажа на экране, пиксели
		bool bPixOn = true;                       // ждём снимок «с квадом» (true) или «без» (false)
		int32 PixTries = 0;                       // попыток дождаться файла снимка

		// Проверка: счётчик и строка журнала.
		void Expect(bool b, const FString& M)
		{
			b ? ++Ok : ++Fail;
			UE_LOG(LogAsdaCreateDim, Display, TEXT("[затемнение] %s %s"), b ? TEXT("OK  ") : TEXT("FAIL"), *M);
		}
		int32 Mode() const { return kModes[ModeIdx]; }   // текущий режим ключа
	};
	using FCtxRef = TSharedRef<FCtx>;

	// Следующий этап через Sec секунд.
	using AsdaSelfTestCtx::ByStage::After;   // ★ R5-c (12.11.2026): общая точка (`Test/AsdaSelfTestCtx.h`) — копия была дословной.

	// Компонент квада (первый статик-меш актора).
	static UStaticMeshComponent* DimMesh(AAsdaLoginGameMode* G)
	{
		AActor* D = G ? FAcc::Dim(G) : nullptr;
		return D ? D->FindComponentByClass<UStaticMeshComponent>() : nullptr;
	}

	// Пересечение луча камеры с плоскостью квада. Возвращает дальность (<0 — плоскость позади/параллельна) и попадание в прямоугольник.
	static float RayHit(const UStaticMeshComponent* SMC, const FVector& P, const FVector& Dir, bool& bInside)
	{
		bInside = false;
		const UStaticMesh* M = SMC ? SMC->GetStaticMesh() : nullptr;
		if (!M) { return -1.f; }
		const FBoxSphereBounds B = M->GetBounds();         // локальные bounds меша: плоскость по оси Y (extent.Y ≈ 0)
		const FTransform T = SMC->GetComponentTransform();
		const FVector LP = T.InverseTransformPosition(P);
		const FVector LD = T.InverseTransformVector(Dir);
		if (FMath::Abs(LD.Y) < KINDA_SMALL_NUMBER) { return -1.f; }
		const float S = (float)((B.Origin.Y - LP.Y) / LD.Y);   // параметр луча (мировая дальность: Dir единичный)
		const FVector H = LP + LD * S;
		bInside = FMath::Abs(H.X - B.Origin.X) <= B.BoxExtent.X && FMath::Abs(H.Z - B.Origin.Z) <= B.BoxExtent.Z;
		return S;
	}

	// Рамка персонажа на экране (пиксели вьюпорта) по 8 углам bounds актора.
	static FBox2D CharScreenBox(APlayerController* PC, AActor* Ch)
	{
		FBox2D Box(ForceInit);
		if (!PC || !Ch) { return Box; }
		FVector O, E; Ch->GetActorBounds(false, O, E);
		for (int32 i = 0; i < 8; ++i)
		{
			const FVector V = O + FVector((i & 1) ? E.X : -E.X, (i & 2) ? E.Y : -E.Y, (i & 4) ? E.Z : -E.Z);
			FVector2D S;
			if (PC->ProjectWorldLocationToScreen(V, S, false)) { Box += S; }
		}
		return Box;
	}

	// Яркость пикселя 0..255.
	static float Lum(const FColor& C) { return 0.2126f * C.R + 0.7152f * C.G + 0.0722f * C.B; }

	static void StageMode(FCtxRef C);
	static void StageGeometry(FCtxRef C);
	static void StageShotOff(FCtxRef C);
	static void StageCompare(FCtxRef C);
	static void StageUiShot(FCtxRef C);
	static void StageNextMode(FCtxRef C);

	// Конец прогона: ключ на место, превью пересобрано в исходном режиме, итог.
	static void Finish(FCtxRef C)
	{
		AsdaCVarSave::RestoreAll(kCmd);
		if (AAsdaLoginGameMode* G = C->G.Get()) { if (FAcc::OnCreate(G)) { FAcc::Respawn(G); } }
		AsdaSelfTestKit::Finish(C->W.Get(), kCmd, C->bQuit, LogAsdaCreateDim, TEXT("SelfTestCreateDim"), C->Ok, C->Fail);
	}

	// Пиксели сцены без UI: снимок в файл (`bShowUI=false`) и чтение PNG (`FImageUtils::LoadImage`).
	//   ★ Прямое чтение вьюпорта (`FViewport::ReadPixels`) под -RenderOffscreen даёт чёрный кадр, а делегат
	//   `OnScreenshotCaptured` не приходил (прогоны 08.11) — поэтому через файл, как видит снимок игрока.
	static FString PixFile(FCtxRef C, bool bOn)
	{
		const FString Dir = C->ShotDir.IsEmpty() ? FPaths::Combine(FPaths::ProjectSavedDir(), TEXT("AsdaSelfTest")) : C->ShotDir;
		return FPaths::ConvertRelativePathToFull(FPaths::Combine(Dir, FString::Printf(TEXT("dimpix_mode%d_%s.png"), C->Mode(), bOn ? TEXT("on") : TEXT("off"))));
	}
	static void StagePixWait(FCtxRef C);
	static void RequestPixels(FCtxRef C, bool bOn)
	{
		C->bPixOn = bOn;
		C->PixTries = 0;
		const FString F = PixFile(C, bOn);
		IFileManager::Get().Delete(*F, false, true, true);
		FScreenshotRequest::RequestScreenshot(F, /*bShowUI=*/false, /*bAddFilenameSuffix=*/false);
		After(C, 0.1f, &StagePixWait);
	}
	// Ожидание файла снимка и чтение пикселей.
	static void StagePixWait(FCtxRef C)
	{
		const FString F = PixFile(C, C->bPixOn);
		FImage Img;
		const bool bReady = !FScreenshotRequest::IsScreenshotRequested() && IFileManager::Get().FileExists(*F)
			&& FImageUtils::LoadImage(*F, Img);
		if (!bReady && ++C->PixTries < 50) { After(C, 0.1f, &StagePixWait); return; }
		TArray<FColor>& Px = C->bPixOn ? C->PixOn : C->PixOff;
		Px.Reset();
		// PNG снимка движок пишет BGRA8 — пиксели берём из RawData как есть (иной формат — кадр не принят, проверка провалится).
		if (bReady && Img.Format == ERawImageFormat::BGRA8 && Img.RawData.Num() == (int64)Img.SizeX * Img.SizeY * 4)
		{
			Px.SetNumUninitialized(Img.SizeX * Img.SizeY);
			FMemory::Memcpy(Px.GetData(), Img.RawData.GetData(), Img.RawData.Num());
			C->Size = FIntPoint(Img.SizeX, Img.SizeY);
		}
		else if (bReady)
		{
			UE_LOG(LogAsdaCreateDim, Warning, TEXT("[затемнение] снимок %s: формат %d — не BGRA8"), *F, (int32)Img.Format);
		}
		if (C->bPixOn) { After(C, 0.01f, &StageShotOff); } else { After(C, 0.01f, &StageCompare); }
	}

	// Режим: ключ, пересборка превью штатным путём.
	static void StageMode(FCtxRef C)
	{
		AAsdaLoginGameMode* G = C->G.Get();
		if (!G || !FAcc::OnCreate(G)) { C->Expect(false, TEXT("экран создания не открыт")); Finish(C); return; }
		AsdaCVarSave::SetForTest(kKey, C->Mode());
		FAcc::Respawn(G);
		After(C, 0.8f, &StageGeometry);
	}

	// Геометрия и отрисовка квада, затем снимок «с квадом».
	static void StageGeometry(FCtxRef C)
	{
		AAsdaLoginGameMode* G = C->G.Get();
		UWorld* W = C->W.Get();
		APlayerController* PC = W ? W->GetFirstPlayerController() : nullptr;
		UStaticMeshComponent* SMC = DimMesh(G);
		AActor* Ch = G ? FAcc::Char(G) : nullptr;
		const int32 M = C->Mode();
		C->Expect(SMC != nullptr && Ch != nullptr, FString::Printf(TEXT("режим %d: квад и персонаж созданы (квад=%d персонаж=%d)"), M, SMC ? 1 : 0, Ch ? 1 : 0));
		if (!SMC || !Ch || !PC || !PC->PlayerCameraManager) { StageNextMode(C); return; }
		const FVector Cam = PC->PlayerCameraManager->GetCameraLocation();
		const float CharDist = (float)(Ch->GetActorLocation() - Cam).Size();
		int32 VX = 0, VY = 0; PC->GetViewportSize(VX, VY);
		const FVector2D Pts[5] = { {0.5, 0.5}, {0.01, 0.01}, {0.99, 0.01}, {0.01, 0.99}, {0.99, 0.99} };
		int32 Covered = 0; bool bCenterBehindChar = false; float CenterT = -1.f;
		for (int32 i = 0; i < 5; ++i)
		{
			FVector P, D;
			if (!PC->DeprojectScreenPositionToWorld(Pts[i].X * VX, Pts[i].Y * VY, P, D)) { continue; }
			bool bIn = false;
			const float T = RayHit(SMC, P, D, bIn);
			const bool bHit = T > 0.f && bIn && T > CharDist;
			Covered += bHit ? 1 : 0;
			if (i == 0) { CenterT = T; bCenterBehindChar = bHit; }
			UE_LOG(LogAsdaCreateDim, Display, TEXT("[затемнение] режим %d: луч (%.2f,%.2f) дальность=%.1f внутри=%d персонаж=%.1f"), M, Pts[i].X, Pts[i].Y, T, bIn ? 1 : 0, CharDist);
		}
		const bool bRendered = SMC->WasRecentlyRendered(0.5f);
		UE_LOG(LogAsdaCreateDim, Display, TEXT("[затемнение] режим %d: актор=%s масштаб=%s камера=%s отрисован=%d"), M,
			*FAcc::Dim(G)->GetActorLocation().ToString(), *SMC->GetComponentScale().ToString(), *Cam.ToString(), bRendered ? 1 : 0);
		if (M == 1)
		{
			C->Expect(Covered == 5, FString::Printf(TEXT("режим 1: квад за персонажем закрывает центр и 4 угла кадра (%d/5, центр %.1f > %.1f)"), Covered, CenterT, CharDist));
			C->Expect(bRendered, TEXT("режим 1: квад отрисован (WasRecentlyRendered)"));
		}
		else if (M == 0)
		{
			C->Expect(!bCenterBehindChar, FString::Printf(TEXT("режим 0 (как было): плоскость НЕ перед камерой за персонажем (центр %.1f)"), CenterT));
		}
		else
		{
			C->Expect(FAcc::Dim(G)->GetActorLocation().IsNearlyZero(1.f), TEXT("режим 2 (эталон): модель в начале координат мира"));
			UE_LOG(LogAsdaCreateDim, Display, TEXT("[затемнение] режим 2 (журнал): закрыто %d/5, центр %s"), Covered, bCenterBehindChar ? TEXT("за персонажем") : TEXT("НЕ закрыт"));
		}
		C->CharBox = CharScreenBox(PC, Ch);
		RequestPixels(C, true);
	}

	// Снимок «без квада»: квад временно спрятан.
	static void StageShotOff(FCtxRef C)
	{
		if (AActor* D = C->G.IsValid() ? FAcc::Dim(C->G.Get()) : nullptr) { D->SetActorHiddenInGame(true); }
		After(C, 0.25f, [](FCtxRef R) { RequestPixels(R, false); });
	}

	// Сравнение кадров: фон вне рамки персонажа и неизменная часть персонажа.
	static void StageCompare(FCtxRef C)
	{
		if (AActor* D = C->G.IsValid() ? FAcc::Dim(C->G.Get()) : nullptr) { D->SetActorHiddenInGame(false); }
		const int32 M = C->Mode();
		const int32 N = C->Size.X * C->Size.Y;
		if (C->PixOn.Num() != N || C->PixOff.Num() != N || N == 0)
		{
			C->Expect(false, FString::Printf(TEXT("режим %d: кадры сцены не получены"), M));
			After(C, 0.2f, &StageUiShot);
			return;
		}
		// Рамка персонажа: внутренняя часть (середина 50% ширины) — там тело; фон считаем вне всей рамки с запасом.
		const FBox2D Box = C->CharBox.bIsValid ? C->CharBox : FBox2D(FVector2D(0, 0), FVector2D(0, 0));
		const FBox2D Pad(Box.Min - FVector2D(8, 8), Box.Max + FVector2D(8, 8));
		const float Cx = (float)Box.GetCenter().X, Hw = (float)Box.GetExtent().X * 0.5f;
		double BgOn = 0, BgOff = 0; int64 BgN = 0; int64 ChN = 0, ChSame = 0;
		for (int32 y = 0; y < C->Size.Y; y += 2)
		{
			for (int32 x = 0; x < C->Size.X; x += 2)
			{
				const int32 I = y * C->Size.X + x;
				const float A = Lum(C->PixOn[I]), B = Lum(C->PixOff[I]);
				const bool bInPad = Box.bIsValid && x >= Pad.Min.X && x <= Pad.Max.X && y >= Pad.Min.Y && y <= Pad.Max.Y;
				if (!bInPad) { BgOn += A; BgOff += B; ++BgN; continue; }
				if (x >= Cx - Hw && x <= Cx + Hw && y >= Box.Min.Y && y <= Box.Max.Y && B > 20.f)
				{
					++ChN;
					if (A >= B * 0.85f) { ++ChSame; }
				}
			}
		}
		const float Ratio = BgOff > 0 ? (float)(BgOn / BgOff) : 1.f;
		const float Same = ChN > 0 ? (float)ChSame / (float)ChN : 0.f;
		UE_LOG(LogAsdaCreateDim, Display, TEXT("[затемнение] режим %d: фон %.1f → %.1f (отношение %.3f), рамка персонажа %s, неизменных пикселей персонажа %.2f (%lld)"),
			M, BgN ? BgOff / BgN : 0.0, BgN ? BgOn / BgN : 0.0, Ratio, *Box.ToString(), Same, ChN);
		if (M == 1)
		{
			C->Expect(Ratio <= 0.6f, FString::Printf(TEXT("режим 1: фон за персонажем затемнён (яркость ×%.3f ≤ 0.6)"), Ratio));
			C->Expect(Same >= 0.2f, FString::Printf(TEXT("режим 1: персонаж не затемнён (неизменных пикселей %.2f ≥ 0.2)"), Same));
		}
		else if (M == 0)
		{
			C->Expect(Ratio >= 0.97f, FString::Printf(TEXT("режим 0 (как было): фон не затемнён (яркость ×%.3f ≥ 0.97)"), Ratio));
		}
		After(C, 0.2f, &StageUiShot);
	}

	// Кадр с UI для отчёта (shots=).
	static void StageUiShot(FCtxRef C)
	{
		if (C->ShotDir.IsEmpty()) { StageNextMode(C); return; }
		const FString F = FPaths::Combine(C->ShotDir, FString::Printf(TEXT("create_dim_mode%d.png"), C->Mode()));
		FScreenshotRequest::RequestScreenshot(F, /*bShowUI=*/true, /*bAddFilenameSuffix=*/false);
		UE_LOG(LogAsdaCreateDim, Display, TEXT("[затемнение] кадр с UI: %s"), *F);
		After(C, 0.6f, &StageNextMode);
	}

	// Следующий режим или конец.
	static void StageNextMode(FCtxRef C)
	{
		if (++C->ModeIdx < (int32)UE_ARRAY_COUNT(kModes)) { StageMode(C); return; }
		Finish(C);
	}

	// Старт: экран выбора офлайн → экран создания.
	static void StageOpenCreate(FCtxRef C)
	{
		AAsdaLoginGameMode* G = C->G.Get();
		if (!G) { C->Expect(false, TEXT("нет режима логина")); Finish(C); return; }
		FAcc::Show(G);
		C->Expect(FAcc::OnCreate(G), TEXT("экран создания открыт штатным ShowCharCreateUI"));
		After(C, 2.0f, &StageMode);
	}
}

static FAutoConsoleCommandWithWorldAndArgs GAsdaLoginSelfTestCreateDim(
	TEXT("asda.Login.SelfTestCreateDim"),
	TEXT("asda.Login.SelfTestCreateDim [shots=<папка>] [delay=N] [quit] — ОФЛАЙН-самотест 08.11.2026 (пачка 16, баг 10): затемнение за ")
	TEXT("создаваемым персонажем (квад blackscreen) в режимах asda.Login.CreateDimPlace 1/2/0: геометрия лучей камеры, отрисовка, пиксели кадра."),
	FConsoleCommandWithWorldAndArgsDelegate::CreateLambda([](const TArray<FString>& A, UWorld* W)
	{
		using namespace AsdaCreateDimLocal;
		AsdaCVarSave::ArmWorldGuard(W, kCmd);
		const FAsdaSelfTestArgs KitArgs = FAsdaSelfTestArgs::Parse(A);
		if (AsdaSelfTestKit::RefuseStart(W, kCmd, KitArgs)) { return; }
		if (!W) { return; }
		FCtxRef C = MakeShared<FCtx>();
		C->W = W;
		C->bQuit = KitArgs.bQuit;
		for (const FString& P : KitArgs.Pos) { if (P.StartsWith(TEXT("shots="))) { C->ShotDir = P.Mid(6); } }
		auto Go = [C]()
		{
			if (AsdaSelfTestKit::RefuseDeferred(C->W.Get(), kCmd, C->bQuit)) { return; }
			UWorld* LW = C->W.Get();
			AAsdaLoginGameMode* G = LW ? Cast<AAsdaLoginGameMode>(LW->GetAuthGameMode()) : nullptr;
			if (!G) { AsdaSelfTestKit::Refused(LW, kCmd, C->bQuit, TEXT("нет AAsdaLoginGameMode")); return; }
			C->G = G;
			if (GEngine) { GEngine->Exec(LW, TEXT("asda.Login.CharSelectOffline")); }   // путь игрока: экран выбора → создание
			After(C, 3.0f, &StageOpenCreate);
		};
		if (KitArgs.Delay <= 0.f) { Go(); return; }
		FTimerHandle H;
		W->GetTimerManager().SetTimer(H, FTimerDelegate::CreateLambda(Go), KitArgs.Delay, false);
	}));

#endif // !UE_BUILD_SHIPPING
