// Copyright (c) 2026 @sitxovski (Discord)
// ★ САМОТЕСТ ЯРКОСТИ ПОКАЗА НАВЫКОВ ДРУЖБЫ (01.11.2026): `asda.SelfTest.SoulmateGlow [ids=1287,1288,1290] [shot=<папка>] [quit]`. [ИСПРАВЛЕНИЕ]
//   Решение владельца: белое свечение персонажа от навыков соулмейта — на 50 % тусклее. Ключ `asda.Soul.SkillGlowScale`
//   (AsdaSkillFxSubsystem.cpp, разбор у ключа): множитель рантайм-альфы `AAsdaEffectActor::FxAlphaScale` эффектов показа
//   1286…1355 — ею умножаются вершинная альфа всех каналов эффекта и сила его света.
//   Офлайн (`-AsdaNoNet`), карта мира. Путь игрока: свой спавн пакетом 4003, показ — `UAsdaSkillFxSubsystem::PlaySkillVisual`
//   (та же точка, что зовёт `UAsdaSkillUseSubsystem::CastSoulmateSkill` после отправки 5231).
//   Для каждого кода показа — два прогона: ключ 1 (как было) и ключ 0.5 (новое умолчание). Проверки: показ запущен, эффекты
//   родились, у каждого `FxAlphaScale` = значение ключа; сила света эффектов (сумма `GetLastIntensityCd` по кадрам) при 0.5 —
//   вдвое ниже, чем при 1. GM-«скрыться» (`SetGmHideTranslucent`): под скрытием α = HideFxAlpha × ключ, после — снова ключ.
//   Отрицательный контроль: тот же .jeb, рождённый НЕ показом навыка Дружбы (без тега), — α 1, под скрытием ровно HideFxAlpha.
//   Кадры (`shot=`) — каждые 0.5 с от 0.5 до 3.5 с:
//   `soul_<код>_k<100·ключ>_t<мс>.png`; пары «до/после» сравнивает `Tools/asda_soulmate_glow_sheet.py`.
//   Итог: «СОУЛ-СВЕЧЕНИЕ ИТОГ: ok=N fail=M».
#include "CoreMinimal.h"
#include "AsdaDevConsole.h"
#include "AsdaTestPackets.h"   // ★ R5-c (12.11.2026): общая точка самотестов вместо своих копий помощников

#if ASDA_DEV_CONSOLE && !UE_BUILD_SHIPPING

#include "AsdaNetSubsystem.h"
#include "AsdaBytes.h"
#include "AsdaEffectActor.h"
#include "Skill/AsdaSkillFxSubsystem.h"
#include "World/AsdaWorldSubsystem.h"
#include "World/AsdaPlayerCharacter.h"
#include "World/AsdaFxLightComponent.h"
#include "Test/AsdaSelfTestKit.h"
#include "Engine/Engine.h"
#include "Engine/GameInstance.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "GameFramework/PlayerController.h"
#include "Misc/Paths.h"
#include "HAL/FileManager.h"
#include "HAL/IConsoleManager.h"
#include "UnrealClient.h"   // FScreenshotRequest — кадры

DEFINE_LOG_CATEGORY_STATIC(LogAsdaSoulGlowTest, Log, All);

namespace AsdaSoulGlowTest
{
	static const TCHAR* kCmd = TEXT("asda.SelfTest.SoulmateGlow");   // имя команды
	static const TCHAR* kKey = TEXT("asda.Soul.SkillGlowScale");     // ключ яркости
	constexpr int32 kMe = 4321;                                      // свой userNum (4003)
	constexpr float kShotStep = 0.5f;                                // шаг кадров, с
	constexpr int32 kShots = 7;                                      // кадров на прогон (0.5…3.5 с)

	// Шаг сценария: true — шаг завершён, false — ждёт следующего кадра. Аргумент — секунды от начала шага.
	using FStep = TFunction<bool(float)>;

	// Состояние прогона.
	struct FState
	{
		TWeakObjectPtr<UWorld> W;                      // мир
		bool bQuit = false;                            // `quit` в конце
		FString ShotDir;                               // папка кадров (пусто — без кадров)
		TArray<int32> Ids;                             // коды показа
		int32 Ok = 0, Fail = 0;                        // счётчики проверок
		TArray<FStep> Steps;                           // сценарий
		int32 Idx = 0;                                 // текущий шаг
		double StepT0 = 0.0;                           // начало шага (время мира)
		FDelegateHandle H;                             // подписка на тик мира
		TUniquePtr<FAsdaNetSelfScope> NetScope;        // свой номер — вернуть после прогона
		TUniquePtr<FAsdaCVarScope> CVars;              // ключи — вернуть после прогона
		TWeakObjectPtr<UAsdaNetSubsystem> Net;         // сеть
		TWeakObjectPtr<UAsdaWorldSubsystem> WS;        // мир
		TWeakObjectPtr<UAsdaSkillFxSubsystem> Fx;      // визуал навыков
		TSet<TWeakObjectPtr<AAsdaEffectActor>> Before; // эффект-акторы до запуска показа
		TArray<TWeakObjectPtr<AAsdaEffectActor>> Cur;  // эффект-акторы текущего показа
		TMap<int32, float> LightSum;                   // сумма силы света показа по кадрам: ключ = код·10 + (0 — k 1, 1 — k 0.5)
		TWeakObjectPtr<AAsdaEffectActor> Ctrl;         // отрицательный контроль (тот же .jeb без показа навыка)
		FRotator OrigSelfRot = FRotator::ZeroRotator;  // поворот своего актёра до `Aim`
		bool bSelfRotSaved = false;                    // поворот снят
		bool bAbort = false;                           // прервать сценарий

		// Записать итог проверки.
		void Check(bool b, const FString& What)
		{
			if (b) { ++Ok;   UE_LOG(LogAsdaSoulGlowTest, Log,   TEXT("СОУЛ-СВЕЧЕНИЕ ok   %s"), *What); }
			else   { ++Fail; UE_LOG(LogAsdaSoulGlowTest, Error, TEXT("СОУЛ-СВЕЧЕНИЕ FAIL %s"), *What); }
		}
	};

	// Свой павн.
	static AAsdaPlayerCharacter* MeOf(const FState& S)
	{
		const UAsdaWorldSubsystem* WS = S.WS.Get();
		return WS ? Cast<AAsdaPlayerCharacter>(WS->LocalPlayerActor.Get()) : nullptr;
	}

	// Тело 4003 (9 б): userNum, тайлы, состояние, показ шлема.
	using AsdaTestPackets::Body4003;   // ★ R5-c (12.11.2026): общая точка (`AsdaTestPackets.h`) — копия была дословной.

	// Повернуть себя лицом к камере (слегка вполоборота).
	static void Aim(FState& S)
	{
		UWorld* W = S.W.Get();
		APlayerController* PC = W ? W->GetFirstPlayerController() : nullptr;
		AAsdaPlayerCharacter* Me = MeOf(S);
		if (!PC || !Me) { return; }
		if (!S.bSelfRotSaved) { S.OrigSelfRot = Me->GetActorRotation(); S.bSelfRotSaved = true; }
		Me->SetActorRotation(FRotator(0.f, PC->GetControlRotation().Yaw + 180.f + 20.f, 0.f));
	}

	// Кадр в папку `shot=`.
	static void Shot(const FState& S, const FString& Name)
	{
		if (S.ShotDir.IsEmpty()) { return; }
		const FString Path = FPaths::Combine(S.ShotDir, Name + TEXT(".png"));
		FScreenshotRequest::RequestScreenshot(Path, /*bShowUI=*/false, /*bAddFilenameSuffix=*/false);
		UE_LOG(LogAsdaSoulGlowTest, Log, TEXT("СОУЛ-СВЕЧЕНИЕ кадр %s"), *Path);
	}

	// Живые эффект-акторы мира.
	static TArray<AAsdaEffectActor*> LiveFx(UWorld* W)
	{
		TArray<AAsdaEffectActor*> Out;
		for (TActorIterator<AAsdaEffectActor> It(W); W && It; ++It)
		{
			if (IsValid(*It) && !It->IsPendingKillPending()) { Out.Add(*It); }
		}
		return Out;
	}

	// Сила света эффектов текущего показа (кд, последнее применение).
	static float LightOf(const FState& S)
	{
		float Sum = 0.f;
		for (const TWeakObjectPtr<AAsdaEffectActor>& WE : S.Cur)
		{
			const AAsdaEffectActor* E = WE.Get();
			if (!E) { continue; }
			TInlineComponentArray<UAsdaFxLightComponent*> Ls(E);
			for (const UAsdaFxLightComponent* L : Ls) { Sum += L ? L->GetLastIntensityCd() : 0.f; }
		}
		return Sum;
	}

	// α всех эффектов текущего показа ≈ `Want`.
	static bool CurAlphaIs(const FState& S, float Want, FString& Out)
	{
		bool b = S.Cur.Num() > 0;
		for (const TWeakObjectPtr<AAsdaEffectActor>& WE : S.Cur)
		{
			const AAsdaEffectActor* E = WE.Get();
			if (!E) { continue; }
			Out += FString::Printf(TEXT(" %s(α×%.3f)"), *E->EffectName, E->FxAlphaScale);
			b &= FMath::IsNearlyEqual(E->FxAlphaScale, Want, 0.001f);
		}
		return b;
	}

	// Конец прогона: показ погашен, поворот и ключи возвращены.
	static void Finish(const TSharedRef<FState>& SP)
	{
		FState& S = *SP;
		FWorldDelegates::OnWorldPostActorTick.Remove(S.H);
		if (AAsdaPlayerCharacter* Me = MeOf(S))
		{
			if (UAsdaSkillFxSubsystem* Fx = S.Fx.Get(); Fx && Fx->IsSkillVisualActive(Me)) { Fx->StopSkillVisual(Me); }
			if (S.bSelfRotSaved) { Me->SetActorRotation(S.OrigSelfRot); }
			Me->SetGmHideTranslucent(false);
		}
		if (AAsdaEffectActor* C = S.Ctrl.Get()) { C->Destroy(); }
		S.CVars.Reset();
		S.NetScope.Reset();
		AsdaSelfTestKit::Finish(S.W.Get(), kCmd, S.bQuit, LogAsdaSoulGlowTest, TEXT("СОУЛ-СВЕЧЕНИЕ"), S.Ok, S.Fail);
	}

	// Тик сценария.
	static void Tick(const TSharedRef<FState>& InSP, UWorld* InW)
	{
		const TSharedRef<FState> SP = InSP;   // своя ссылка: `Finish` снимает подписку вместе с копией в лямбде
		FState& S = *SP;
		UWorld* W = S.W.Get();
		if (!W) { FWorldDelegates::OnWorldPostActorTick.Remove(S.H); return; }
		if (InW != W) { return; }
		while (S.Steps.IsValidIndex(S.Idx) && !S.bAbort)
		{
			const float T = (float)(W->GetTimeSeconds() - S.StepT0);
			if (!S.Steps[S.Idx](T)) { return; }
			++S.Idx;
			S.StepT0 = W->GetTimeSeconds();
		}
		Finish(SP);
	}

	// Сценарий.
	static void Build(const TSharedRef<FState>& SP)
	{
		FState* S = &SP.Get();
		auto Do = [](TFunction<void()> Fn) { return FStep([Fn](float) { Fn(); return true; }); };
		auto Wait = [](float Sec) { return FStep([Sec](float T) { return T >= Sec; }); };

		// ═══ Спавн ═══
		S->Steps.Add(Do([S]()
		{
			GEngine->Exec(S->W.Get(), TEXT("Asda.Load.CurrentMap 3"));
			S->Net->DebugFeedPacket(4003, Body4003(kMe));
		}));
		S->Steps.Add(Wait(4.0f));   // эффект появления (столб света) ≈3 с
		S->Steps.Add(Do([S]()
		{
			AAsdaPlayerCharacter* Me = MeOf(*S);
			S->Check(Me && Me->BaseSkel, FString::Printf(TEXT("0 свой павн %s"), Me ? TEXT("есть") : TEXT("НЕТ")));
			if (!Me || !Me->BaseSkel) { S->bAbort = true; return; }
			Aim(*S);
		}));
		S->Steps.Add(Wait(0.5f));

		// ═══ Показы: ключ 1 (как было) и 0.5 (умолчание) ═══
		const float Ks[2] = { 1.0f, 0.5f };
		for (const int32 Id : S->Ids)
		{
			for (const float K : Ks)
			{
				S->Steps.Add(Do([S, Id, K]()
				{
					S->CVars->Set(kKey, K);
					AAsdaPlayerCharacter* Me = MeOf(*S);
					S->Before.Reset();
					for (AAsdaEffectActor* E : LiveFx(S->W.Get())) { S->Before.Add(E); }
					Aim(*S);
					const bool bOn = S->Fx->PlaySkillVisual(Me, Id, kMe);
					S->Check(bOn, FString::Printf(TEXT("%d k=%.2f: показ запущен"), Id, K));
				}));
				S->Steps.Add(Wait(0.15f));
				S->Steps.Add(Do([S, Id, K]()
				{
					int32 N = 0, Bad = 0;
					FString Names;
					S->Cur.Reset();
					for (AAsdaEffectActor* E : LiveFx(S->W.Get()))
					{
						if (S->Before.Contains(E)) { continue; }
						++N;
						S->Cur.Add(E);
						if (!FMath::IsNearlyEqual(E->FxAlphaScale, K, 0.001f)) { ++Bad; }
						Names += FString::Printf(TEXT(" %s(α×%.2f)"), *E->EffectName, E->FxAlphaScale);
					}
					S->Check(N > 0 && Bad == 0, FString::Printf(TEXT("%d k=%.2f: новых эффектов %d, с множителем ≠ ключу %d —%s"),
						Id, K, N, Bad, *Names));
				}));
				for (int32 i = 1; i <= kShots; ++i)
				{
					S->Steps.Add(Wait(i == 1 ? kShotStep - 0.15f : kShotStep));
					const int32 Ms = FMath::RoundToInt(i * kShotStep * 1000.f);
					S->Steps.Add(Do([S, Id, K, Ms]()
					{
						Shot(*S, FString::Printf(TEXT("soul_%d_k%03d_t%04d"), Id, FMath::RoundToInt(K * 100.f), Ms));
						const float Li = LightOf(*S);
						S->LightSum.FindOrAdd(Id * 10 + (K < 1.f ? 1 : 0)) += Li;
						UE_LOG(LogAsdaSoulGlowTest, Log, TEXT("СОУЛ-СВЕЧЕНИЕ свет %d k=%.2f t=%d мс: %.1f кд"), Id, K, Ms, Li);
					}));
				}
				S->Steps.Add(Wait(0.5f));
				S->Steps.Add(Do([S]()
				{
					AAsdaPlayerCharacter* Me = MeOf(*S);
					if (Me && S->Fx->IsSkillVisualActive(Me)) { S->Fx->StopSkillVisual(Me); }
				}));
				S->Steps.Add(Wait(2.0f));   // хвосты эффектов догорают
			}
			// Свет: при ключе 0.5 сумма силы по тем же кадрам — вдвое ниже (допуск на мерцание/ход силы).
			S->Steps.Add(Do([S, Id]()
			{
				const float L1 = S->LightSum.FindRef(Id * 10), L05 = S->LightSum.FindRef(Id * 10 + 1);
				const float R = L1 > 0.f ? L05 / L1 : -1.f;
				S->Check(L1 > 0.f && R > 0.35f && R < 0.65f,
					FString::Printf(TEXT("%d свет: сумма силы по кадрам k1 %.1f кд, k0.5 %.1f кд, отношение %.2f (ждём ≈0.5)"), Id, L1, L05, R));
			}));
		}

		// ═══ GM-«скрыться» во время показа: множитель соулмейта не теряется ═══
		S->Steps.Add(Do([S]()
		{
			S->CVars->Set(kKey, 0.5f);
			S->CVars->Set(TEXT("asda.Gm.HideTranslucent"), 1);
			AAsdaPlayerCharacter* Me = MeOf(*S);
			S->Before.Reset();
			for (AAsdaEffectActor* E : LiveFx(S->W.Get())) { S->Before.Add(E); }
			S->Fx->PlaySkillVisual(Me, S->Ids[0], kMe);
			// Отрицательный контроль: тот же .jeb, рождённый мимо показа навыка Дружбы, прицеплен к персонажу.
			AAsdaEffectActor* C = AAsdaEffectActor::SpawnByJebName(S->W.Get(), TEXT("eff_1_char_99_soulritec-a_00"),
				Me->GetActorLocation(), FRotator::ZeroRotator, Me);
			if (C) { C->AttachToActor(Me, FAttachmentTransformRules::KeepWorldTransform); C->SetLifeSpan(6.f); }
			S->Ctrl = C;
		}));
		S->Steps.Add(Wait(0.3f));
		S->Steps.Add(Do([S]()
		{
			S->Cur.Reset();
			for (AAsdaEffectActor* E : LiveFx(S->W.Get()))
			{
				if (!S->Before.Contains(E) && E != S->Ctrl.Get()) { S->Cur.Add(E); }
			}
			const AAsdaEffectActor* C = S->Ctrl.Get();
			S->Check(C && FMath::IsNearlyEqual(C->FxAlphaScale, 1.f, 0.001f) && FMath::IsNearlyEqual(AsdaSoulSkillGlowFactor(C), 1.f),
				FString::Printf(TEXT("G0 контроль (тот же .jeb без показа навыка): α×%.3f, множитель соулмейта %.2f — ждём 1"),
					C ? C->FxAlphaScale : -1.f, AsdaSoulSkillGlowFactor(C)));
			MeOf(*S)->SetGmHideTranslucent(true);
		}));
		S->Steps.Add(Wait(0.3f));
		S->Steps.Add(Do([S]()
		{
			const IConsoleVariable* HV = IConsoleManager::Get().FindConsoleVariable(TEXT("asda.Gm.HideFxAlpha"));
			const float H = HV ? FMath::Clamp(HV->GetFloat(), 0.f, 1.f) : 1.f;
			FString Names;
			const bool b = CurAlphaIs(*S, H * 0.5f, Names);
			const AAsdaEffectActor* C = S->Ctrl.Get();
			S->Check(b, FString::Printf(TEXT("G1 под скрытием: α показа = HideFxAlpha %.2f × 0.5 —%s"), H, *Names));
			S->Check(C && FMath::IsNearlyEqual(C->FxAlphaScale, H, 0.001f),
				FString::Printf(TEXT("G2 контроль под скрытием: α×%.3f — ждём ровно HideFxAlpha %.2f"), C ? C->FxAlphaScale : -1.f, H));
			MeOf(*S)->SetGmHideTranslucent(false);
		}));
		S->Steps.Add(Wait(0.3f));
		S->Steps.Add(Do([S]()
		{
			FString Names;
			const bool b = CurAlphaIs(*S, 0.5f, Names);
			const AAsdaEffectActor* C = S->Ctrl.Get();
			S->Check(b, FString::Printf(TEXT("G3 после скрытия: α показа снова 0.5 (было бы 1 без исправления) —%s"), *Names));
			S->Check(C && FMath::IsNearlyEqual(C->FxAlphaScale, 1.f, 0.001f),
				FString::Printf(TEXT("G4 контроль после скрытия: α×%.3f — ждём 1"), C ? C->FxAlphaScale : -1.f));
			AAsdaPlayerCharacter* Me = MeOf(*S);
			if (Me && S->Fx->IsSkillVisualActive(Me)) { S->Fx->StopSkillVisual(Me); }
		}));
		S->Steps.Add(Wait(1.0f));
	}
}

static FAutoConsoleCommandWithWorldAndArgs GAsdaSelfTestSoulmateGlow(
	TEXT("asda.SelfTest.SoulmateGlow"),
	TEXT("Самотест яркости показа навыков Дружбы (ключ asda.Soul.SkillGlowScale): показ 1287/1288/1290 на своём персонаже "
	     "при ключе 1 и 0.5, множитель альфы эффектов = ключу. Офлайн, карта мира. `ids=1287,1290` — коды, `shot=<папка>` — кадры, `quit`."),
	FConsoleCommandWithWorldAndArgsDelegate::CreateLambda([](const TArray<FString>& Args, UWorld* W)
	{
		using namespace AsdaSoulGlowTest;
		const FAsdaSelfTestArgs KA = FAsdaSelfTestArgs::Parse(Args);
		if (AsdaSelfTestKit::RefuseStart(W, kCmd, KA)) { return; }
		UGameInstance* GI = W ? W->GetGameInstance() : nullptr;
		UAsdaNetSubsystem* Net = GI ? GI->GetSubsystem<UAsdaNetSubsystem>() : nullptr;
		UAsdaWorldSubsystem* WS = GI ? GI->GetSubsystem<UAsdaWorldSubsystem>() : nullptr;
		UAsdaSkillFxSubsystem* Fx = GI ? GI->GetSubsystem<UAsdaSkillFxSubsystem>() : nullptr;
		if (!Net || !WS || !Fx || !IConsoleManager::Get().FindConsoleVariable(kKey))
		{
			AsdaSelfTestKit::Refused(W, kCmd, KA.bQuit, TEXT("нет подсистем сети/мира/визуала навыков или ключа яркости"));
			return;
		}
		TSharedRef<FState> SP = MakeShared<FState>();
		SP->W = W; SP->bQuit = KA.bQuit; SP->Net = Net; SP->WS = WS; SP->Fx = Fx;
		for (const FString& P : KA.Pos)
		{
			if (P.StartsWith(TEXT("shot="), ESearchCase::IgnoreCase))
			{
				SP->ShotDir = FPaths::ConvertRelativePathToFull(P.Mid(5));
				IFileManager::Get().MakeDirectory(*SP->ShotDir, /*Tree*/true);
			}
			else if (P.StartsWith(TEXT("ids="), ESearchCase::IgnoreCase))
			{
				TArray<FString> L;
				P.Mid(4).ParseIntoArray(L, TEXT(","));
				for (const FString& X : L) { SP->Ids.Add(FCString::Atoi(*X)); }
			}
		}
		// Три разных показа: SoulRite (soulritec-a_00), зов (soulcallh-a_00), бафф (attbomg-a_01 + attbomh-a_00).
		if (SP->Ids.Num() == 0) { SP->Ids = { 1287, 1288, 1290 }; }
		SP->NetScope = MakeUnique<FAsdaNetSelfScope>(Net, kCmd);
		SP->CVars = MakeUnique<FAsdaCVarScope>(W, kCmd);
		Build(SP);
		SP->StepT0 = W->GetTimeSeconds();
		SP->H = FWorldDelegates::OnWorldPostActorTick.AddLambda([SP](UWorld* InW, ELevelTick, float) { Tick(SP, InW); });
	}));

#endif // ASDA_DEV_CONSOLE && !UE_BUILD_SHIPPING
