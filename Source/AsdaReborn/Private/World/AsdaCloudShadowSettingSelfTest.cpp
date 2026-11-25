// Copyright (c) 2026 @sitxovski (Discord)
// asda.Clouds.SelfTestShadowSetting — настройка теней снимает обе тени облаков, смена на ходу; растворение дальних силуэтов получает числа. [ПОРТ]
//   Офлайн, карта с полем облаков. Уровни и пресеты идут теми же вызовами, что окно настроек (SetShadowQuality, SetQualityPreset, CommitQuality).
//   `shots=<метка>` — кадры под облаком: уровень 3, уровень 0, уровень 0 с откатом. Запуск: `asda.Clouds.SelfTestShadowSetting quit`.
#include "CoreMinimal.h"
#include "AsdaDevConsole.h"

#if ASDA_DEV_CONSOLE && !UE_BUILD_SHIPPING

#include "AsdaCloudFieldActor.h"
#include "Components/DirectionalLightComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Containers/Ticker.h"
#include "Engine/Engine.h"
#include "Engine/GameInstance.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "HAL/IConsoleManager.h"
#include "Resources/AsdaVideoSettingsSubsystem.h"
#include "Scalability.h"
#include "Test/AsdaSelfTestKit.h"
#include "Text/AsdaLang.h"
#include "UObject/UObjectIterator.h"
#include "UnrealClient.h"
#include "World/AsdaViewRangeSubsystem.h"

DEFINE_LOG_CATEGORY_STATIC(LogAsdaCloudSetTest, Log, All);

namespace AsdaCloudSetTest
{
	static const TCHAR* const kCmd = TEXT("asda.Clouds.SelfTestShadowSetting");   // имя команды
	static constexpr double kSettle = 0.6;   // пауза после смены настройки, с

	// Шаг сценария: что поставить и что ждать.
	struct FStep
	{
		int32 Level = -1;     // уровень теней; -1 — не менять
		int32 Preset = -1;    // пресет; -1 — не менять
		int32 Mode = -1;      // asda.Clouds.FieldShadows; -1 — не менять
		bool bExpectLf = false;      // светофункция разрешена
		bool bExpectProxy = false;   // прокси отбрасывают тень
		const TCHAR* Name = TEXT("");   // подпись и метка кадра
		bool bShot = false;   // снять кадр
	};

	// Состояние прогона.
	struct FCtx
	{
		UGameInstance* GI = nullptr;             // экземпляр игры
		bool bQuit = false;                      // выйти после прогона
		int32 Ok = 0, Fail = 0;                  // счётчики проверок
		int32 Index = -1;                        // номер шага
		int32 Phase = 0;                         // 0 — поставить, 1 — проверить, 2 — ждать кадр
		double T0 = 0.0;                         // время начала фазы
		int32 Level0 = 3;                        // уровень теней до прогона
		float Wind0 = 0.f;                       // ветер поля до прогона
		FString ShotTag;                         // метка кадров
		TArray<FStep> Steps;                     // сценарий
		TUniquePtr<FAsdaCVarScope> CVars;        // ключи вернутся после прогона
		TWeakObjectPtr<AAsdaCloudFieldActor> Field;   // поле карты
		void Expect(bool b, const FString& What)  // записать проверку
		{
			if (b) { ++Ok; UE_LOG(LogAsdaCloudSetTest, Log, TEXT("ТЕНИ ОБЛАКОВ ok   %s"), *What); }
			else   { ++Fail; UE_LOG(LogAsdaCloudSetTest, Error, TEXT("ТЕНИ ОБЛАКОВ FAIL %s"), *What); }
		}
	};
	using FCtxRef = TSharedRef<FCtx>;

	// Текущее значение ключа движка.
	static int32 IntCVar(const TCHAR* Name)
	{
		IConsoleVariable* V = IConsoleManager::Get().FindConsoleVariable(Name);
		return V ? V->GetInt() : -1;
	}

	// Поставить камеру над тенью ближайшего к центру карты прокси: кадр покажет именно тень поля.
	static void AimAtCloudShadow(const FCtxRef& C, UWorld* W)
	{
		AAsdaCloudFieldActor* F = C->Field.Get();
		const UDirectionalLightComponent* Sun = nullptr;
		for (TObjectIterator<UDirectionalLightComponent> It; It; ++It) { if (It->GetWorld() == W && It->IsRegistered()) { Sun = *It; break; } }
		if (!F || !Sun) { return; }
		const FVector Dir = Sun->GetDirection();
		TInlineComponentArray<UStaticMeshComponent*> Meshes(F);
		for (const UStaticMeshComponent* M : Meshes)
		{
			if (!M->bCastHiddenShadow || Dir.Z >= -0.05f) { continue; }
			const FVector P = M->Bounds.Origin;
			const FVector G = P + Dir * ((P.Z - 500.f) / -Dir.Z);   // след на высоте рельефа
			FHitResult H;
			const bool bHit = W->LineTraceSingleByChannel(H, FVector(G.X, G.Y, 30000.f), FVector(G.X, G.Y, -30000.f), ECC_Visibility);
			if (!bHit) { continue; }
			const FVector Cam = H.ImpactPoint + FVector(-6000.f, 0.f, 5000.f);
			const FRotator Rot = (H.ImpactPoint - Cam).Rotation();
			GEngine->Exec(W, *FString::Printf(TEXT("asda.Clouds.ShotCam %.0f %.0f %.0f %.1f %.1f"), Cam.X, Cam.Y, Cam.Z, Rot.Pitch, Rot.Yaw));
			return;
		}
	}

	// Проверки шага.
	static void CheckStep(const FCtxRef& C, const FStep& S)
	{
		const AAsdaCloudFieldActor* F = C->Field.Get();
		const int32 Casting = F ? F->CastingShadowProxies() : -1;
		C->Expect((IntCVar(TEXT("r.LightFunctionQuality")) > 0) == S.bExpectLf, FString::Printf(TEXT("%s: светофункция %s (r.LightFunctionQuality=%d)"),
			S.Name, S.bExpectLf ? TEXT("есть") : TEXT("снята"), IntCVar(TEXT("r.LightFunctionQuality"))));
		C->Expect((Casting > 0) == S.bExpectProxy, FString::Printf(TEXT("%s: прокси облаков %s (отбрасывают %d)"),
			S.Name, S.bExpectProxy ? TEXT("дают тень") : TEXT("без тени"), Casting));
	}

	// Конец: ключи, уровень и ветер назад, итог.
	static bool Done(const FCtxRef& C, UWorld* W)
	{
		if (UAsdaVideoSettingsSubsystem* Vid = C->GI->GetSubsystem<UAsdaVideoSettingsSubsystem>()) { Vid->SetShadowQuality(C->Level0); Vid->CommitQuality(); }
		if (AAsdaCloudFieldActor* F = C->Field.Get()) { F->WindSpeed = C->Wind0; }
		C->CVars.Reset();
		AsdaSelfTestKit::Finish(W, kCmd, C->bQuit, LogAsdaCloudSetTest, TEXT("ТЕНИ ОБЛАКОВ"), C->Ok, C->Fail);
		return false;
	}

	// Числа растворения дальних силуэтов: ползунок на ходу и откат.
	static void CheckSkyFade(const FCtxRef& C, UWorld* W, const TCHAR* Name, bool bOn)
	{
		const UAsdaViewRangeSubsystem* VR = UAsdaViewRangeSubsystem::Get(W);
		float Start = 0.f, End = 0.f;
		if (VR) { VR->DebugSkyFade(Start, End); }
		const float R = UAsdaViewRangeSubsystem::RangeUnits(W);
		const bool bGood = bOn ? (FMath::IsNearlyEqual(Start, R * 0.8f, 2.f) && FMath::IsNearlyEqual(End, R, 2.f)) : Start > 1.0e8f;
		C->Expect(bGood, FString::Printf(TEXT("%s: растворение %.0f…%.0f при дальности %.0f"), Name, Start, End, R));
	}

	// Шаг прогона; false — конец.
	static bool Step(const FCtxRef& C)
	{
		UWorld* W = C->GI ? C->GI->GetWorld() : nullptr;
		UAsdaVideoSettingsSubsystem* Vid = C->GI ? C->GI->GetSubsystem<UAsdaVideoSettingsSubsystem>() : nullptr;
		if (!W || !Vid) { return false; }
		const double Dt = FPlatformTime::Seconds() - C->T0;
		if (C->Index < 0)
		{
			C->Index = 0; C->Phase = 0;
		}
		if (C->Index >= C->Steps.Num())
		{
			// Хвост: растворение. Фазы 0..3 — ползунок 50 %, 200 %, откат, возврат.
			static const float kScale[3] = { 0.5f, 2.f, 1.f };
			const int32 Tail = C->Index - C->Steps.Num();
			if (C->Phase == 0)
			{
				if (Tail < 3) { C->CVars->Set(TEXT("asda.Cull.Scale"), kScale[Tail]); }
				else if (Tail == 3) { C->CVars->Set(TEXT("asda.ViewRange.SkyFade"), 0); }
				else { return Done(C, W); }
				C->Phase = 1; C->T0 = FPlatformTime::Seconds();
				return true;
			}
			if (Dt < kSettle) { return true; }
			CheckSkyFade(C, W, Tail < 3 ? TEXT("ползунок") : TEXT("откат"), Tail < 3);
			++C->Index; C->Phase = 0;
			return true;
		}
		const FStep& S = C->Steps[C->Index];
		if (C->Phase == 0)
		{
			if (S.Mode >= 0) { C->CVars->Set(TEXT("asda.Clouds.FieldShadows"), S.Mode); }
			if (S.Level >= 0) { Vid->SetShadowQuality(S.Level); Vid->CommitQuality(); }
			if (S.Preset >= 0) { Vid->SetQualityPreset((EAsdaQualityPreset)S.Preset); Vid->CommitQuality(); }
			C->Phase = 1; C->T0 = FPlatformTime::Seconds();
			return true;
		}
		if (C->Phase == 1)
		{
			if (Dt < kSettle * (S.bShot ? 4.0 : 1.0)) { return true; }
			CheckStep(C, S);
			if (S.bShot && !C->ShotTag.IsEmpty())
			{
				const FString File = FString::Printf(TEXT("@@ASTORY_DEV_ROOT_FWD@@/Reports/batch14_fog_pop/shots/%s_%s.png"), *C->ShotTag, S.Name);
				FScreenshotRequest::RequestScreenshot(File, /*bShowUI=*/false, /*bAddUniqueSuffix=*/false);
				C->Phase = 2; C->T0 = FPlatformTime::Seconds();
				return true;
			}
			++C->Index; C->Phase = 0;
			return true;
		}
		if (Dt < 0.5 || FScreenshotRequest::IsScreenshotRequested()) { return true; }
		++C->Index; C->Phase = 0;
		return true;
	}

	// Старт: нужно поле облаков на карте; ветер стоит, чтобы кадры совпали.
	static void Run(const TArray<FString>& A, UWorld* W)
	{
		const FAsdaSelfTestArgs KA = FAsdaSelfTestArgs::Parse(A);
		if (AsdaSelfTestKit::RefuseStart(W, kCmd, KA, 60.f)) { return; }
		UGameInstance* GI = W ? W->GetGameInstance() : nullptr;
		AAsdaCloudFieldActor* Field = nullptr;
		for (TActorIterator<AAsdaCloudFieldActor> It(W); It; ++It) { Field = *It; break; }
		if (!GI || !GI->GetSubsystem<UAsdaVideoSettingsSubsystem>() || !Field) { AsdaSelfTestKit::Refused(W, kCmd, KA.bQuit, TEXT("нет поля облаков или настроек")); return; }
		FCtxRef C = MakeShared<FCtx>();
		C->GI = GI; C->bQuit = KA.bQuit; C->Field = Field;
		C->Level0 = Scalability::GetQualityLevels().ShadowQuality;
		C->Wind0 = Field->WindSpeed;
		Field->WindSpeed = 0.f;
		C->CVars = MakeUnique<FAsdaCVarScope>(W, kCmd);
		C->CVars->Set(TEXT("asda.Clouds.FieldShadows"), 1);
		C->CVars->Set(TEXT("asda.ViewRange.SkyFade"), 1);
		C->CVars->Set(TEXT("asda.Cull.Scale"), 1.f);
		for (const FString& P : KA.Pos) { if (P.StartsWith(TEXT("shots="))) { C->ShotTag = P.Mid(6); } }
		const auto Add = [&C](int32 Level, int32 Preset, int32 Mode, bool bLf, bool bProxy, const TCHAR* Name, bool bShot = false)
		{
			FStep S; S.Level = Level; S.Preset = Preset; S.Mode = Mode; S.bExpectLf = bLf; S.bExpectProxy = bProxy; S.Name = Name; S.bShot = bShot;
			C->Steps.Add(S);
		};
		Add(3, -1, -1, true, true, TEXT("level3"), true);
		Add(0, -1, -1, false, false, TEXT("level0"), true);
		Add(-1, -1, 0, false, true, TEXT("level0_rollback"), true);
		Add(-1, -1, 1, false, false, TEXT("level0_again"));
		Add(1, -1, -1, true, true, TEXT("level1"));
		Add(2, -1, -1, true, true, TEXT("level2"));
		Add(4, -1, -1, true, true, TEXT("level4"));
		Add(0, -1, -1, false, false, TEXT("level0_live"));
		Add(4, -1, -1, true, true, TEXT("level4_live"));
		for (int32 P = 0; P < 5; ++P)
		{
			static const TCHAR* const kNames[5] = { TEXT("preset0"), TEXT("preset1"), TEXT("preset2"), TEXT("preset3"), TEXT("preset4") };
			Add(-1, P, -1, P > 0, P > 0, kNames[P]);
		}
		Add(3, -1, 2, true, false, TEXT("mode_never"));
		Add(-1, -1, 1, true, true, TEXT("mode_setting"));
		if (!C->ShotTag.IsEmpty()) { AimAtCloudShadow(C, W); }
		C->T0 = FPlatformTime::Seconds();
		UE_LOG(LogAsdaCloudSetTest, Log, TEXT("ТЕНИ ОБЛАКОВ старт: мир %s, язык %s, уровень %d"), *W->GetMapName(), FAsdaLang::IsRu() ? TEXT("ru") : TEXT("en"), C->Level0);
		FTSTicker::GetCoreTicker().AddTicker(FTickerDelegate::CreateLambda([C](float) { return Step(C); }), 0.f);
	}
}

// Консольная команда самотеста.
static FAutoConsoleCommandWithWorldAndArgs GAsdaCloudShadowSettingSelfTest(
	TEXT("asda.Clouds.SelfTestShadowSetting"),
	TEXT("Настройка теней снимает тени облаков на всех уровнях и пресетах, смена на ходу, откат, числа растворения. `shots=<метка>`. Офлайн."),
	FConsoleCommandWithWorldAndArgsDelegate::CreateStatic(&AsdaCloudSetTest::Run));

#endif
