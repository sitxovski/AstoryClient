// Copyright (c) 2026 @sitxovski (Discord)
// ★ САМОТЕСТ «КАРТИНКА ПОСЛЕ ТЕЛЕПОРТА» (05.11.2026, баг игрока: после свитка возврата чёрный фон вместо мира, жёлтые «ступени»,
//   висящие обрывки объектов): `asda.Load.WarpShotsSelfTest [maps=3,0,1] [noshots] [quit]`.
//   Путь игрока (закон diagnostic-must-use-real-path): перенос в пределах карты — штатный приём своего 4056 (`DebugFeedPacket`): его
//   шлют свиток возврата (`CharSpendItemUse` → `CharRegenVillage(…,1)` → `CharRegenVillageMove`), воскрешение в городе на той же
//   карте, ГМ-телепорт/воскрешение на месте и зов друга на ту же карту; смена карты — `UAsdaLoadManagerSubsystem::BeginMapLoad`
//   (тот же вызов, что 4055/5055/6201/6013 с другой картой). Свой персонаж — `UAsdaWorldSubsystem::DebugSpawnSelfAt` (точка 4003).
//   Кадры PNG — `Reports/warp_shots/shots/<язык>_<карта>_<этап>_<мс>.png`; метрика кадра — доля «чёрных» пикселей сцены
//   (яркость < 12/255 в средней полосе экрана, без интерфейса) и средняя яркость; провал — чёрных > 25 % через 2.5 с после переноса.
//   Итог: «ТЕЛЕПОРТ-КАДР ИТОГ: ok=N fail=M».
#include "CoreMinimal.h"
#include "AsdaDevConsole.h"

#if ASDA_DEV_CONSOLE && !UE_BUILD_SHIPPING

#include "AsdaBytes.h"
#include "AsdaNetSubsystem.h"
#include "Test/AsdaSelfTestKit.h"
#include "Text/AsdaLang.h"
#include "World/AsdaLoadManagerSubsystem.h"
#include "World/AsdaWorldKind.h"
#include "World/AsdaWorldSubsystem.h"
#include "World/AsdaPlayerCharacter.h"
#include "Resources/AsdaVideoSettingsSubsystem.h"
#include "Components/StaticMeshComponent.h"
#include "Engine/StaticMesh.h"
#include "EngineUtils.h"
#include "GameFramework/SpringArmComponent.h"
#include "Containers/Ticker.h"
#include "Engine/Engine.h"
#include "Engine/GameInstance.h"
#include "Engine/GameViewportClient.h"
#include "Engine/World.h"
#include "Framework/Application/SlateApplication.h"
#include "GameFramework/PlayerController.h"
#include "HAL/FileManager.h"
#include "IImageWrapper.h"
#include "IImageWrapperModule.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "Modules/ModuleManager.h"
#include "UnrealClient.h"
#include "Widgets/SWindow.h"

DEFINE_LOG_CATEGORY_STATIC(LogAsdaWarpShotsTest, Log, All);

namespace AsdaWarpShotsTest
{
	static const TCHAR* kCmd = TEXT("asda.Load.WarpShotsSelfTest");   // имя команды
	static constexpr uint16 kMyUserNum = 321;                          // свой userNum на прогон
	static const float kShotTimes[] = { 0.15f, 0.5f, 1.0f, 2.5f, 4.0f };   // кадры после переноса, с

	// Карта прогона: id, тайл спавна, тайл переноса (свиток возврата — точка сохранения).
	struct FMapCase { int32 MapId; FIntPoint Spawn; FIntPoint Warp; };

	// Контекст прогона (переживает смену мира).
	struct FCtx
	{
		int32 Ok = 0, Fail = 0;                   // счёт
		bool bQuit = false, bShots = true;        // выход / кадры
		TArray<FMapCase> Maps;                    // карты прогона
		int32 MapIdx = 0;                         // текущая карта
		int32 Stage = 0;                          // этап на карте
		int32 ShotIdx = 0;                        // номер кадра после переноса
		bool bSweep = false;                      // `sweep`: после переноса обход камеры 8 углов × 2 дальности (поиск «камеры в меше»)
		int32 SweepIdx = 0;                       // номер положения камеры в обходе
		int32 SweepBad = 0;                       // положений с чёрным > 25 %
		double T0 = 0.0;                          // начало этапа
		TWeakObjectPtr<UGameInstance> GI;         // игра
		TUniquePtr<FAsdaNetSelfScope> NetScope;   // свой номер — вернуть в конце
		void Expect(bool b, const FString& What)
		{
			if (b) { ++Ok; UE_LOG(LogAsdaWarpShotsTest, Log, TEXT("ТЕЛЕПОРТ-КАДР ok   %s"), *What); }
			else   { ++Fail; UE_LOG(LogAsdaWarpShotsTest, Error, TEXT("ТЕЛЕПОРТ-КАДР FAIL %s"), *What); }
		}
	};
	using FCtxRef = TSharedRef<FCtx>;

	template <class T> static T* Sub(const FCtxRef& C) { UGameInstance* GI = C->GI.Get(); return GI ? GI->GetSubsystem<T>() : nullptr; }

	// Снимок окна (с интерфейсом) синхронно: метрика сцены + PNG. BlackFrac — доля почти чёрных пикселей средней полосы.
	static bool Grab(const FCtxRef& C, const FString& Tag, float& OutBlackFrac, float& OutMeanLum)
	{
		OutBlackFrac = -1.f; OutMeanLum = -1.f;
		UGameViewportClient* GVC = GEngine ? GEngine->GameViewport : nullptr;
		TSharedPtr<SWindow> Win = GVC ? GVC->GetWindow() : nullptr;
		if (!Win.IsValid() || !FSlateApplication::IsInitialized()) { return false; }
		TArray<FColor> Px; FIntVector Sz(0, 0, 0);
		if (!FSlateApplication::Get().TakeScreenshot(Win.ToSharedRef(), Px, Sz) || Px.Num() != Sz.X * Sz.Y || Sz.X <= 0) { return false; }
		// Средняя полоса экрана по высоте 25..60 %, по ширине 30..70 % — там мир (окна/HUD по краям).
		int64 N = 0, Black = 0; double Sum = 0.0;
		for (int32 Y = Sz.Y * 25 / 100; Y < Sz.Y * 60 / 100; ++Y)
		{
			for (int32 X = Sz.X * 30 / 100; X < Sz.X * 70 / 100; ++X)
			{
				const FColor& P = Px[Y * Sz.X + X];
				const float L = 0.2126f * P.R + 0.7152f * P.G + 0.0722f * P.B;
				Sum += L; ++N; if (L < 12.f) { ++Black; }
			}
		}
		OutBlackFrac = N ? (float)Black / (float)N : -1.f;
		OutMeanLum = N ? (float)(Sum / N) : -1.f;
		if (C->bShots)
		{
			for (FColor& P : Px) { P.A = 255; }
			IImageWrapperModule& IW = FModuleManager::LoadModuleChecked<IImageWrapperModule>(TEXT("ImageWrapper"));
			TSharedPtr<IImageWrapper> Png = IW.CreateImageWrapper(EImageFormat::PNG);
			if (Png.IsValid() && Png->SetRaw(Px.GetData(), Px.Num() * sizeof(FColor), Sz.X, Sz.Y, ERGBFormat::BGRA, 8))
			{
				const FString Dir = FPaths::ConvertRelativePathToFull(FPaths::Combine(FPaths::ProjectDir(), TEXT("../Reports/warp_shots/shots")));
				IFileManager::Get().MakeDirectory(*Dir, true);
				FFileHelper::SaveArrayToFile(Png->GetCompressed(), *FPaths::Combine(Dir, FString::Printf(TEXT("%s_%s.png"), FAsdaLang::IsRu() ? TEXT("ru") : TEXT("en"), *Tag)));
			}
		}
		UE_LOG(LogAsdaWarpShotsTest, Log, TEXT("ТЕЛЕПОРТ-КАДР кадр %s: чёрных %.1f%%, средняя яркость %.1f"), *Tag, OutBlackFrac * 100.f, OutMeanLum);
		return true;
	}
	// Своё 4056 (22 б): uidx@0, тайлы @2/@4, HP@6, MP@10, состояние@20 — живой.
	static void Feed4056(const FCtxRef& C, FIntPoint T)
	{
		TArray<uint8> B; B.SetNumZeroed(22);
		AsdaBytes::WrI16(&B[0], (int16)kMyUserNum); AsdaBytes::WrI16(&B[2], (int16)T.X); AsdaBytes::WrI16(&B[4], (int16)T.Y);
		AsdaBytes::WrI32(&B[6], 100); AsdaBytes::WrI16(&B[10], 50);
		if (UAsdaNetSubsystem* Net = Sub<UAsdaNetSubsystem>(C)) { Net->SetSelfUserNumForDebug(kMyUserNum); Net->DebugFeedPacket(4056, B); }
	}
	static void Finish(const FCtxRef& C)
	{
		C->NetScope.Reset();
		UGameInstance* GI = C->GI.Get();
		AsdaSelfTestKit::Finish(GI ? GI->GetWorld() : nullptr, kCmd, C->bQuit, LogAsdaWarpShotsTest, TEXT("ТЕЛЕПОРТ-КАДР"), C->Ok, C->Fail);
	}

	static bool Step(const FCtxRef& C)
	{
		UGameInstance* GI = C->GI.Get();
		if (!GI) { return false; }
		UWorld* W = GI->GetWorld();
		UAsdaLoadManagerSubsystem* LM = GI->GetSubsystem<UAsdaLoadManagerSubsystem>();
		UAsdaWorldSubsystem* WS = GI->GetSubsystem<UAsdaWorldSubsystem>();
		if (!LM || !WS) { return false; }
		const double Now = FPlatformTime::Seconds();
		const double Dt = Now - C->T0;
		if (C->MapIdx >= C->Maps.Num()) { Finish(C); return false; }
		const FMapCase& M = C->Maps[C->MapIdx];
		const FString Dir = LM->GetMapDirName(M.MapId);
		switch (C->Stage)
		{
		case 0:   // нужная карта: если не она — смена карты штатным BeginMapLoad
			if (LM->CurrentMapId == M.MapId && W && !AsdaWorldKind::IsLoadingMap(W)) { C->Stage = 2; C->T0 = Now; return true; }
			UE_LOG(LogAsdaWarpShotsTest, Log, TEXT("ТЕЛЕПОРТ-КАДР карта %d (%s): BeginMapLoad"), M.MapId, *Dir);
			LM->BeginMapLoad(M.MapId);
			C->Stage = 1; C->T0 = Now;
			return true;
		case 1:   // ждём целевую карту
			if (W && !AsdaWorldKind::IsLoadingMap(W) && W->HasBegunPlay() && Dt > 1.0 && !LM->IsLoadingScreenVisible())
			{
				C->Stage = 2; C->T0 = Now;
			}
			else if (Dt > 90.0) { C->Expect(false, FString::Printf(TEXT("карта %d не загрузилась за 90 с"), M.MapId)); ++C->MapIdx; C->Stage = 0; }
			return true;
		case 2:   // спавн
			LM->CurrentMapId = M.MapId;
			WS->DebugSpawnSelfAt(M.Spawn.X, M.Spawn.Y);
			C->Stage = 3; C->T0 = Now;
			return true;
		case 3:   // 4 с на осадку (затемнение смены карты, подкачка) → кадр «до» → перенос 4056
		{
			if (Dt < 4.0) { return true; }
			float B = 0, L = 0;
			Grab(C, FString::Printf(TEXT("%s_0before"), *Dir), B, L);
			C->Expect(B >= 0.f && B < 0.25f, FString::Printf(TEXT("%s: до переноса мир виден (чёрных %.1f%%)"), *Dir, B * 100.f));
			UE_LOG(LogAsdaWarpShotsTest, Log, TEXT("ТЕЛЕПОРТ-КАДР %s: своё 4056 → тайл (%d,%d) (свиток возврата / воскрешение в городе)"), *Dir, M.Warp.X, M.Warp.Y);
			Feed4056(C, M.Warp);
			C->Stage = 4; C->T0 = Now; C->ShotIdx = 0;
			return true;
		}
		case 4:   // кадры после переноса
		{
			if (C->ShotIdx < (int32)UE_ARRAY_COUNT(kShotTimes))
			{
				if (Dt < kShotTimes[C->ShotIdx]) { return true; }
				float B = 0, L = 0;
				Grab(C, FString::Printf(TEXT("%s_1warp_%04d"), *Dir, (int32)(kShotTimes[C->ShotIdx] * 1000.f)), B, L);
				if (kShotTimes[C->ShotIdx] >= 2.5f)
				{
					C->Expect(B >= 0.f && B < 0.25f, FString::Printf(TEXT("%s: через %.1f с после переноса мир виден (чёрных %.1f%%, яркость %.0f)"),
						*Dir, kShotTimes[C->ShotIdx], B * 100.f, L));
				}
				++C->ShotIdx;
				return true;
			}
			if (C->bSweep && C->SweepIdx == 0 && W)
			{
				// Опись примитивов у точки переноса (радиус 4000): кто Nanite, кто нет, дальности отсечки — сверка с кадром игрока.
				const APlayerController* PC0 = GI->GetFirstLocalPlayerController();
				const FVector P0 = (PC0 && PC0->GetPawn()) ? PC0->GetPawn()->GetActorLocation() : FVector::ZeroVector;
				int32 NN = 0, NonN = 0;
				for (TActorIterator<AActor> It(W); It; ++It)
				{
					TArray<UStaticMeshComponent*> Comps; It->GetComponents(Comps);
					for (UStaticMeshComponent* Smc : Comps)
					{
						if (!Smc || !Smc->GetStaticMesh() || !Smc->IsVisible()) { continue; }
						const FBoxSphereBounds Bd = Smc->Bounds;
						if (FVector::Dist(Bd.Origin, P0) - Bd.SphereRadius > 4000.f) { continue; }
						const bool bNan = Smc->GetStaticMesh()->HasValidNaniteData();   // ★ 10.11: IsNaniteEnabled только в редакторе — Development-сборка не собиралась
						bNan ? ++NN : ++NonN;
						UE_LOG(LogAsdaWarpShotsTest, Log, TEXT("ТЕЛЕПОРТ-КАДР опись: %s | %s | nanite=%d | дальность=%.0f | R=%.0f"),
							*It->GetName(), *Smc->GetStaticMesh()->GetName(), bNan ? 1 : 0, Smc->CachedMaxDrawDistance, Bd.SphereRadius);
					}
				}
				UE_LOG(LogAsdaWarpShotsTest, Log, TEXT("ТЕЛЕПОРТ-КАДР опись итог: nanite %d, не nanite %d"), NN, NonN);
			}
			if (C->bSweep && C->SweepIdx < 16)
			{
				// Камера: угол 45° × i, дальность 500 / 1270 (кламп колеса эталона 170..1270). Кадр — через 0.6 с после постановки.
				const APlayerController* PC = GI->GetFirstLocalPlayerController();
				AAsdaPlayerCharacter* Ch = PC ? Cast<AAsdaPlayerCharacter>(PC->GetPawn()) : nullptr;
				if (Dt < 0.6 && C->SweepIdx > 0) { return true; }
				if (C->SweepIdx > 0 || Dt >= 0.6)
				{
					if (C->SweepIdx > 0)
					{
						float B = 0, L = 0;
						const int32 Prev = C->SweepIdx - 1;
						Grab(C, FString::Printf(TEXT("%s_3sweep_y%03d_d%04d"), *Dir, (Prev % 8) * 45, Prev < 8 ? 500 : 1270), B, L);
						if (B > 0.25f) { ++C->SweepBad; }
					}
				}
				if (Ch && Ch->CameraArm && C->SweepIdx < 16)
				{
					const int32 I = C->SweepIdx;
					if (APlayerController* PCm = GI->GetFirstLocalPlayerController()) { PCm->SetControlRotation(FRotator(-20.f, (I % 8) * 45.f, 0.f)); }
					Ch->CameraArm->TargetArmLength = (I < 8) ? 500.f : 1270.f;
				}
				++C->SweepIdx; C->T0 = Now;
				return true;
			}
			if (C->bSweep && C->SweepIdx == 16)
			{
				if (Dt < 0.6) { return true; }
				float B = 0, L = 0;
				Grab(C, FString::Printf(TEXT("%s_3sweep_y315_d1270"), *Dir), B, L);
				if (B > 0.25f) { ++C->SweepBad; }
				++C->SweepIdx;
				C->Expect(C->SweepBad == 0, FString::Printf(TEXT("%s: обход камеры у точки переноса — положений с чёрным > 25%%: %d из 16"), *Dir, C->SweepBad));
				C->SweepBad = 0;
			}
			// Обратный перенос (второй свиток/ГМ-телепорт) — к точке спавна.
			C->SweepIdx = 0;
			Feed4056(C, M.Spawn);
			C->Stage = 5; C->T0 = Now;
			return true;
		}
		case 5:
		{
			if (Dt < 3.0) { return true; }
			float B = 0, L = 0;
			Grab(C, FString::Printf(TEXT("%s_2back_3000"), *Dir), B, L);
			C->Expect(B >= 0.f && B < 0.25f, FString::Printf(TEXT("%s: после обратного переноса мир виден (чёрных %.1f%%)"), *Dir, B * 100.f));
			++C->MapIdx; C->Stage = 0; C->T0 = Now;
			return true;
		}
		default:
			return false;
		}
	}

	static void Run(const TArray<FString>& A, UWorld* W)
	{
		const FAsdaSelfTestArgs KA = FAsdaSelfTestArgs::Parse(A);
		if (AsdaSelfTestKit::RefuseStart(W, kCmd, KA)) { return; }
		UGameInstance* GI = W ? W->GetGameInstance() : nullptr;
		UAsdaNetSubsystem* Net = GI ? GI->GetSubsystem<UAsdaNetSubsystem>() : nullptr;
		if (!Net || !GI->GetSubsystem<UAsdaWorldSubsystem>() || !GI->GetSubsystem<UAsdaLoadManagerSubsystem>())
		{
			AsdaSelfTestKit::Refused(W, kCmd, KA.bQuit, TEXT("нет подсистем")); return;
		}
		FCtxRef C = MakeShared<FCtx>();
		C->GI = GI; C->bQuit = KA.bQuit; C->bShots = !KA.Has(TEXT("noshots")); C->bSweep = KA.Has(TEXT("sweep"));
		// Альпия: спавн в поле → свиток возврата в точку сохранения города (125,390, как у игрока 130,375).
		C->Maps = { { 3, FIntPoint(256, 256), FIntPoint(130, 375) }, { 0, FIntPoint(256, 256), FIntPoint(150, 360) }, { 1, FIntPoint(256, 256), FIntPoint(120, 120) } };
		for (const FString& P : KA.Pos)
		{
			if (!P.StartsWith(TEXT("maps="))) { continue; }
			TArray<FString> Ids; P.Mid(5).ParseIntoArray(Ids, TEXT(","));
			TArray<FMapCase> Keep;
			for (const FString& S : Ids) { for (const FMapCase& M : C->Maps) { if (M.MapId == FCString::Atoi(*S)) { Keep.Add(M); } } }
			if (Keep.Num() > 0) { C->Maps = Keep; }
		}
		// `preset=low|medium|high|epic` — пресет качества игрока (журнал Player55 02.11: «пресет=Low», все sg.* = 0) штатной командой меню.
		for (const FString& P : KA.Pos)
		{
			if (!P.StartsWith(TEXT("preset="))) { continue; }
			const int32 L = FCString::Atoi(*P.Mid(7));
			if (UAsdaVideoSettingsSubsystem* V = GI->GetSubsystem<UAsdaVideoSettingsSubsystem>()) { V->SetQualityPreset((EAsdaQualityPreset)FMath::Clamp(L, 0, 4)); }
			UE_LOG(LogAsdaWarpShotsTest, Log, TEXT("ТЕЛЕПОРТ-КАДР пресет качества %d (как у игрока)"), L);
		}
		C->NetScope = MakeUnique<FAsdaNetSelfScope>(Net, kCmd);
		Net->SetSelfUserNumForDebug(kMyUserNum);
		if (UAsdaLoadManagerSubsystem* LM = GI->GetSubsystem<UAsdaLoadManagerSubsystem>(); LM && LM->CurrentMapId < 0) { LM->CurrentMapId = 3; }
		C->T0 = FPlatformTime::Seconds();
		FTSTicker::GetCoreTicker().AddTicker(FTickerDelegate::CreateLambda([C](float) { return Step(C); }), 0.f);
	}
}

// Консольная команда самотеста.
static FAutoConsoleCommandWithWorldAndArgs GAsdaWarpShotsSelfTest(
	TEXT("asda.Load.WarpShotsSelfTest"),
	TEXT("Самотест картинки после телепорта (05.11): на картах 3/0/1 своё 4056 (свиток возврата, воскрешение в городе, ГМ) и обратно; "
	     "кадры PNG в Reports/warp_shots/shots и доля чёрных пикселей. Офлайн, карта мира. [maps=3,0,1] [noshots] `quit`."),
	FConsoleCommandWithWorldAndArgsDelegate::CreateLambda([](const TArray<FString>& A, UWorld* W) { AsdaWarpShotsTest::Run(A, W); }));

#endif // ASDA_DEV_CONSOLE && !UE_BUILD_SHIPPING
