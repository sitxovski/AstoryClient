// Copyright (c) 2026 @sitxovski (Discord)
// ★★★ САМОТЕСТ ПРЫЖКА И ПЛАВАНИЯ (перенос из донора AsdaFableClassic, 23.10.2026). [ПОРТ]
//
// ЧТО ПРОВЕРЯЕТ (настоящие входы, не копии):
//   A. Свой прыжок с места: `StartJump(false)` → 4024 код 131 с курсом (перехват узкий, в сеть ничего), клип anim_235,
//      по концу клипа — снова клип покоя.
//   B. Прыжок с бега: ход `NetMove` → клип anim_236, позиция продолжает меняться, по концу — снова клип бега.
//   C. Отказы своего прыжка: сидя, во время эмоции (4024 при этом не уходит).
//   D. Прыжок соседа: делегат сети `OnCharAction` (тот же путь, что пакет 4025) с кодом 131 → у соседа anim_235.
//   E. Вода: точка глубже 100 на карте (поиск по мешу воды) → свой плывёт, клип anim_238, ноги на 90 ниже поверхности,
//      брызги swimeffet01; прыжок в воде отклонён (решение владельца 28.10; ключ asda.Swim.ActionGate 0 — принят, как донор); сосед в той же воде тоже
//      плывёт (условие локальное); верхом в воде — «транспорт в воде», не плывёт; после высадки — снова плывёт; на суше —
//      снова клип покоя.
//   F. Ключи `asda.Swim.Enable 0` / `asda.Jump.Enable 0` — поведение эталона (не плывёт, прыжок отклонён).
// Консоль: `asda.SelfTest.JumpSwim [delay=N] [quit]`. Только офлайн. Строки — «САМОТЕСТ-ПРЫЖОК», итог — «… ИТОГ: ok=… fail=…».
#include "CoreMinimal.h"
#include "Test/AsdaSelfTestCtx.h"   // ★ R5-c (12.11.2026): общая точка самотестов вместо своих копий помощников
#include "Test/AsdaSelfTestInput.h"   // ★ R5-c (12.11.2026): общая точка самотестов вместо своих копий помощников

#if !UE_BUILD_SHIPPING

#include "AsdaBytes.h"
#include "AsdaEffectActor.h"
#include "AsdaNetSubsystem.h"
#include "Test/AsdaSelfTestRunGate.h"   // ★ R3: FAsdaSelfTestRunGate — узкий заголовок вместо AsdaOutgoingGate.h
#include "AsdaCVarSave.h"
#include "Test/AsdaSelfTestKit.h"
#include "World/AsdaPlayerCharacter.h"
#include "World/AsdaPlayerController.h"
#include "World/AsdaWorldSubsystem.h"
#include "World/AsdaGroundTrace.h"
#include "World/AsdaCoord.h"
#include "UI/AsdaChatWindowSubsystem.h"   // G2: строка чата
#include "World/AsdaNameplateComponent.h"   // ★ 24.10.2026: точка плашки над головой (asda.SelfTest.JumpOverhead)
#include "World/AsdaKingCrownSubsystem.h"   // ★ 24.10.2026: корона над головой (attach-6)
#include "Components/StaticMeshComponent.h"
#include "Engine/Engine.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "HAL/IConsoleManager.h"
#include "TimerManager.h"
#include "InputCoreTypes.h"
#include "GameFramework/PlayerController.h"
#include "Components/SkeletalMeshComponent.h"

DEFINE_LOG_CATEGORY_STATIC(LogAsdaJumpSwimTest, Log, All);

namespace AsdaJumpSwimTest
{
	static const TCHAR* const kCmd = TEXT("asda.SelfTest.JumpSwim");
	static const TCHAR* const kPrefix = TEXT("САМОТЕСТ-ПРЫЖОК");
	static constexpr int32 kPeerUserNum = 7771;   // сосед-заглушка

	// Состояние прогона (живёт между таймерами).
	struct FState
	{
		int32 Ok = 0, Fail = 0;                       // счётчики проверок
		uint32 CapGen = 0;                            // поколение перехвата отправок
		TArray<int32> Sent4024;                       // перехваченные коды действия 4024
		TArray<FVector2D> SentDir;                    // курс из тех же пакетов
		FVector2D LandGame = FVector2D::ZeroVector;   // точка на суше (старт)
		FVector2D WaterGame = FVector2D::ZeroVector;  // точка глубокой воды
		float WaterZ = 0.f;                           // поверхность воды в этой точке
		bool bWater = false;                          // глубокая вода найдена
		FVector RunStartLoc = FVector::ZeroVector;    // позиция в начале прыжка с бега
		int32 SplashBefore = 0;                       // брызг в мире до входа в воду
		TWeakObjectPtr<AAsdaPlayerCharacter> Me;      // свой персонаж
		TWeakObjectPtr<AAsdaPlayerCharacter> Peer;    // сосед-заглушка
		TWeakObjectPtr<UAsdaNetSubsystem> Net;        // сеть

		void Expect(bool b, const FString& What)
		{
			if (b) { ++Ok; UE_LOG(LogAsdaJumpSwimTest, Display, TEXT("%s ok   %s"), kPrefix, *What); }
			else   { ++Fail; UE_LOG(LogAsdaJumpSwimTest, Error, TEXT("%s FAIL %s"), kPrefix, *What); }
		}
	};

	// Играет ли у персонажа клип anim_<Idx>.
	static bool ClipIs(const AAsdaPlayerCharacter* C, int32 Idx)
	{
		return C && C->GetPlayingClipName().EndsWith(FString::Printf(TEXT("anim_%d"), Idx));
	}

	// Число живых брызг плавания в мире.
	static int32 CountSplashes(UWorld* W, const TCHAR* Jeb)
	{
		int32 N = 0;
		for (TActorIterator<AAsdaEffectActor> It(W); It; ++It)
		{
			if (It->GetClass()->GetName().Contains(Jeb)) { ++N; }
		}
		return N;
	}

	// Найти точку глубокой воды (земля ниже поверхности больше чем на 150): сетка по габариту меша воды.
	static bool FindDeepWater(UWorld* W, FVector2D& OutGame, float& OutWaterZ)
	{
		UStaticMeshComponent* Wc = AsdaGround::FindWaterComponent(W);
		if (!Wc) { return false; }
		const FBox B = Wc->Bounds.GetBox();
		const int32 N = 60;
		float Best = 0.f;
		for (int32 i = 1; i < N; ++i)
		{
			for (int32 j = 1; j < N; ++j)
			{
				const float X = FMath::Lerp(B.Min.X, B.Max.X, (float)i / N);
				const float Y = FMath::Lerp(B.Min.Y, B.Max.Y, (float)j / N);
				const float Wz = AsdaGround::WaterSurfaceZ(W, X, Y);
				if (FMath::IsNaN(Wz)) { continue; }
				const float Gz = AsdaGround::GroundZ(W, X, Y, Wz + 2000.f);
				const float D = Wz - Gz;
				if (D > 150.f && D < 2000.f && (Best == 0.f || D < Best))   // самая мелкая из глубоких — ближе к берегу
				{
					Best = D; OutGame = AsdaCoord::UeToGame2D(FVector(X, Y, 0.f)); OutWaterZ = Wz;
				}
			}
		}
		return Best > 0.f;
	}

	// Высота кости корня (M у мужской модели, G у женской) над точкой актора — высота прыжка даёт сам клип.
	static float RootBoneHeight(const AAsdaPlayerCharacter* C)
	{
		if (!C) { return 0.f; }
		TArray<USkeletalMeshComponent*> Comps;
		C->GetComponents<USkeletalMeshComponent>(Comps);
		for (const USkeletalMeshComponent* K : Comps)
		{
			for (const TCHAR* Bone : { TEXT("M"), TEXT("G") })
			{
				if (K && K->GetBoneIndex(FName(Bone)) != INDEX_NONE)
				{
					return (float)(K->GetBoneLocation(FName(Bone)).Z - C->GetActorLocation().Z);
				}
			}
		}
		return 0.f;
	}

	// Нажать/отпустить клавишу штатным вводом контроллера (как `asda.Net.SelfTest4024`).
	using AsdaSelfTestInput::KeyEv;   // ★ R5-c (12.11.2026): общая точка (`Test/AsdaSelfTestInput.h`) — копия была дословной.

	// Запуск шага через Delay секунд от «сейчас».
	using AsdaSelfTestCtx::ByWorld::After;   // ★ R5-c (12.11.2026): общая точка (`Test/AsdaSelfTestCtx.h`) — копия была дословной.

	static void Run(const TArray<FString>& Args, UWorld* W)
	{
		const FAsdaSelfTestArgs A = FAsdaSelfTestArgs::Parse(Args);
		if (!W) { AsdaSelfTestKit::Refused(nullptr, kCmd, A.bQuit, TEXT("нет мира")); return; }
		if (AsdaSelfTestKit::RefuseStart(W, kCmd, A)) { return; }
		TSharedRef<FAsdaSelfTestRunGate> Gate = MakeShared<FAsdaSelfTestRunGate>(kCmd, A.bQuit);
		TSharedRef<FState> S = MakeShared<FState>();
		TWeakObjectPtr<UWorld> WW(W);
		const bool bQuit = A.bQuit;

		// t = delay: офлайн-спавн своего павна штатными крюками.
		After(W, A.Delay, [WW, Gate]()
		{
			UWorld* Wd = WW.Get();
			if (!Wd || !Gate->Pass(Wd)) { return; }
			if (AsdaSelfTestKit::NeedSelfSpawn(Wd) && GEngine)
			{
				GEngine->Exec(Wd, TEXT("Asda.Load.CurrentMap 3"));
				GEngine->Exec(Wd, TEXT("Asda.World.FakeSelfSpawn 256 256"));
			}
		});

		// Финал (общий для всех веток и отказов).
		auto Finish = [WW, S, bQuit]()
		{
			UWorld* Wd = WW.Get();
			if (!Wd) { return; }
			if (S->Net.IsValid() && S->CapGen) { AsdaSelfTestKit::DisarmCapture(S->Net.Get(), S->CapGen); S->CapGen = 0; }
			AsdaCVarSave::RestoreAll(kCmd);   // ключи самотеста — к исходным значениям
			AsdaSelfTestKit::Finish(Wd, kCmd, bQuit, LogAsdaJumpSwimTest, kPrefix, S->Ok, S->Fail,
				*FString::Printf(TEXT(" (4024: %d)"), S->Sent4024.Num()));
		};

		// t = delay + 2: подготовка и шаг A.
		const float T0 = A.Delay + 2.f;
		After(W, T0, [WW, Gate, S, bQuit, Finish]()
		{
			UWorld* Wd = WW.Get();
			if (!Wd || !Gate->Pass(Wd)) { return; }
			AAsdaPlayerController* PC = Cast<AAsdaPlayerController>(Wd->GetFirstPlayerController());
			AAsdaPlayerCharacter* Me = PC ? Cast<AAsdaPlayerCharacter>(PC->GetPawn()) : nullptr;
			UAsdaNetSubsystem* Net = AsdaSelfTestKit::NetOf(Wd);
			if (!Me || !Net) { AsdaSelfTestKit::Refused(Wd, kCmd, bQuit, TEXT("нет своего персонажа / сети")); return; }
			S->Me = Me; S->Net = Net;
			S->CapGen = AsdaSelfTestKit::ArmCapture(Net, [S](uint16 Op, const uint8* P, int32 Len, bool, TCHAR)
			{
				if (Op == 4024 && P && Len >= 29)
				{
					S->Sent4024.Add((int32)AsdaBytes::RdU16(P + 18));
					S->SentDir.Add(FVector2D(AsdaBytes::RdF32(P + 21), AsdaBytes::RdF32(P + 25)));
				}
			}, 0, kCmd);
			if (S->CapGen == 0) { AsdaSelfTestKit::Refused(Wd, kCmd, bQuit, TEXT("перехват отправок занят")); return; }
			AsdaCVarSave::ArmWorldGuard(Wd, kCmd);   // мир ушёл раньше итога — ключи вернёт сторож
			AsdaCVarSave::SetForTest(TEXT("asda.Swim.Enable"), 1);
			AsdaCVarSave::SetForTest(TEXT("asda.Swim.ActionGate"), 1);
			AsdaCVarSave::SetForTest(TEXT("asda.Jump.Enable"), 1);
			S->LandGame = Me->GetGamePosXZ();
			Me->SetAnimState(EAsdaPlayerState::Idle);
			S->Expect(!Me->IsSwimming(), FString::Printf(TEXT("A0 на суше не плывёт (позиция %.0f,%.0f)"), S->LandGame.X, S->LandGame.Y));
			const int32 N0 = S->Sent4024.Num();
			const float H0 = RootBoneHeight(Me);
			const bool bStarted = Me->StartJump(false);
			After(Wd, 0.6f, [S, H0]()
			{
				const float H1 = RootBoneHeight(S->Me.Get());
				S->Expect(H1 - H0 > 80.f, FString::Printf(TEXT("A5 корень поднялся клипом: %.0f → %.0f (физики нет)"), H0, H1));
			});
			S->Expect(bStarted && Me->IsJumpActive() && ClipIs(Me, AAsdaPlayerCharacter::kClipJumpIdle),
				FString::Printf(TEXT("A1 прыжок с места: начат=%d, клип «%s» (ждём anim_235)"), bStarted ? 1 : 0, *Me->GetPlayingClipName()));
			const bool b131 = S->Sent4024.Num() == N0 + 1 && S->Sent4024.Last() == AAsdaPlayerCharacter::kJumpActionCode;
			S->Expect(b131 && FMath::IsNearlyEqual((float)S->SentDir.Last().Size(), 1.f, 0.02f),
				FString::Printf(TEXT("A2 ушёл 4024 код %d, |курс| = %.3f"), S->Sent4024.Num() > N0 ? S->Sent4024.Last() : -1,
					S->SentDir.Num() ? (float)S->SentDir.Last().Size() : -1.f));
			// Удержание до конца клипа (1.5 с): цепочка — без отпускания клавиши новый прыжок не начинается раньше 0.868 с.
			Me->TickJumpInput(true, true);
			S->Expect(S->Sent4024.Num() == N0 + 1, TEXT("A3 удержание в начале клипа нового прыжка не даёт"));
			Me->TickJumpInput(false, true);

			After(Wd, 1.8f, [WW, S, Finish]()
			{
				UWorld* W2 = WW.Get(); AAsdaPlayerCharacter* Me2 = S->Me.Get();
				if (!W2 || !Me2) { Finish(); return; }
				S->Expect(!Me2->IsJumpActive() && !ClipIs(Me2, AAsdaPlayerCharacter::kClipJumpIdle),
					FString::Printf(TEXT("A4 после клипа прыжок снят, клип «%s»"), *Me2->GetPlayingClipName()));
				// ── B: прыжок с бега ──
				S->RunStartLoc = Me2->GetActorLocation();
				const FVector2D From = Me2->GetGamePosXZ();
				Me2->NetMove(From, From + FVector2D(600.f, 0.f), 0.35f);
				Me2->StartJump(false);
				S->Expect(Me2->IsJumpActive() && ClipIs(Me2, AAsdaPlayerCharacter::kClipJumpRun) && Me2->IsMoving(),
					FString::Printf(TEXT("B1 прыжок с бега: клип «%s» (ждём anim_236), идёт=%d"), *Me2->GetPlayingClipName(), Me2->IsMoving() ? 1 : 0));

				After(W2, 0.5f, [WW, S, Finish]()
				{
					UWorld* W3 = WW.Get(); AAsdaPlayerCharacter* Me3 = S->Me.Get();
					if (!W3 || !Me3) { Finish(); return; }
					const float Moved = FVector::Dist2D(S->RunStartLoc, Me3->GetActorLocation());
					S->Expect(Moved > 50.f, FString::Printf(TEXT("B2 ход во время прыжка продолжается: прошёл %.0f"), Moved));
					Me3->StopMove();
					// Ход → покой посреди прыжка: клип прыжка с места с перенесённым временем.
					S->Expect(ClipIs(Me3, AAsdaPlayerCharacter::kClipJumpIdle) && Me3->IsJumpActive(),
						FString::Printf(TEXT("B3 остановка посреди прыжка: клип «%s» (ждём anim_235)"), *Me3->GetPlayingClipName()));

					After(W3, 1.6f, [WW, S, Finish]()
					{
						UWorld* W4 = WW.Get(); AAsdaPlayerCharacter* Me4 = S->Me.Get();
						if (!W4 || !Me4) { Finish(); return; }
						S->Expect(!Me4->IsJumpActive(), TEXT("B4 прыжок с бега завершён"));
						// ── C: отказы ──
						const int32 N1 = S->Sent4024.Num();
						Me4->SetAnimState(EAsdaPlayerState::Sit);
						const bool bSit = Me4->StartJump(false);
						S->Expect(!bSit && S->Sent4024.Num() == N1, FString::Printf(TEXT("C1 сидя прыжок отклонён (%s), 4024 нет"), *Me4->GetLastJumpRefusal()));
						Me4->SetAnimState(EAsdaPlayerState::Move); Me4->SetAnimState(EAsdaPlayerState::Idle);
						Me4->PlayEmotionClip(33);
						const bool bEmo = Me4->StartJump(false);
						S->Expect(!bEmo && S->Sent4024.Num() == N1, FString::Printf(TEXT("C2 во время эмоции отклонён (%s), 4024 нет"), *Me4->GetLastJumpRefusal()));
						Me4->StopEmotionClip();
						AsdaCVarSave::SetForTest(TEXT("asda.Jump.Enable"), 0);
						S->Expect(!Me4->StartJump(false), TEXT("F1 asda.Jump.Enable 0 — прыжка нет (эталон)"));
						AsdaCVarSave::SetForTest(TEXT("asda.Jump.Enable"), 1);
						// ── D: сосед ──
						UAsdaWorldSubsystem* WS = W4->GetGameInstance()->GetSubsystem<UAsdaWorldSubsystem>();
						if (WS) { WS->DebugSpawnFakePeer(kPeerUserNum, TEXT("Прыгун"), S->LandGame + FVector2D(300.f, 300.f)); }
						AAsdaPlayerCharacter* P = WS ? Cast<AAsdaPlayerCharacter>(WS->ActorByUserIdx(kPeerUserNum)) : nullptr;
						S->Peer = P;
						if (P && S->Net.IsValid())
						{
							P->SetAnimState(EAsdaPlayerState::Idle);
							const int32 N2 = S->Sent4024.Num();
							S->Net->OnCharAction.Broadcast(kPeerUserNum, AAsdaPlayerCharacter::kJumpActionCode, 0.f, 0.f);
							S->Expect(P->IsJumpActive() && ClipIs(P, AAsdaPlayerCharacter::kClipJumpIdle) && S->Sent4024.Num() == N2,
								FString::Printf(TEXT("D1 сосед по 4025 код 131: прыгает=%d, клип «%s», своих 4024 нет"), P->IsJumpActive() ? 1 : 0, *P->GetPlayingClipName()));
						}
						else { S->Expect(false, TEXT("D1 сосед-заглушка не появился")); }
						// ── E: вода ──
						S->bWater = FindDeepWater(W4, S->WaterGame, S->WaterZ);
						S->Expect(S->bWater, FString::Printf(TEXT("E0 глубокая вода найдена: (%.0f,%.0f) поверхность %.0f"), S->WaterGame.X, S->WaterGame.Y, S->WaterZ));
						if (!S->bWater) { Finish(); return; }
						S->SplashBefore = CountSplashes(W4, TEXT("swimeffet01"));
						Me4->NetSnapHard(S->WaterGame);
						if (AAsdaPlayerCharacter* P2 = S->Peer.Get()) { P2->NetSnapHard(S->WaterGame + FVector2D(60.f, 0.f)); }

						After(W4, 2.2f, [WW, S, Finish]()
						{
							UWorld* W5 = WW.Get(); AAsdaPlayerCharacter* Me5 = S->Me.Get();
							if (!W5 || !Me5) { Finish(); return; }
							const float Z = Me5->GetActorLocation().Z;
							S->Expect(Me5->IsSwimming() && ClipIs(Me5, AAsdaPlayerCharacter::kClipSwimIdle),
								FString::Printf(TEXT("E1 свой в глубокой воде плывёт: %d, клип «%s» (ждём anim_238)"), Me5->IsSwimming() ? 1 : 0, *Me5->GetPlayingClipName()));
							S->Expect(FMath::IsNearlyEqual(Z, S->WaterZ - 20.f, 2.f), FString::Printf(TEXT("E2 стоя в воде ноги на 20 ниже поверхности (донор; asda.Swim.HeightFix): Z %.1f, поверхность %.1f"), Z, S->WaterZ));
							const int32 Spl = CountSplashes(W5, TEXT("swimeffet01"));
							S->Expect(Spl > S->SplashBefore, FString::Printf(TEXT("E3 брызги swimeffet01 у плывущего на месте: %d → %d"), S->SplashBefore, Spl));
							AAsdaPlayerCharacter* P = S->Peer.Get();
							S->Expect(P && P->IsSwimming() && ClipIs(P, AAsdaPlayerCharacter::kClipSwimIdle),
								FString::Printf(TEXT("E4 сосед в той же воде плывёт (условие локальное): %d, клип «%s»"), P && P->IsSwimming() ? 1 : 0, P ? *P->GetPlayingClipName() : TEXT("—")));
							// ★ 28.10.2026 (решение владельца): плывущий не прыгает — `asda.Swim.ActionGate 1` отказ, 0 — как донор (принят).
							const bool bJ = Me5->StartJump(false);
							S->Expect(!bJ && !Me5->IsJumpActive() && ClipIs(Me5, AAsdaPlayerCharacter::kClipSwimIdle),
								FString::Printf(TEXT("E5 прыжок в воде отклонён (%s), клип плавания: «%s»"), *Me5->GetLastJumpRefusal(), *Me5->GetPlayingClipName()));
							AsdaCVarSave::SetForTest(TEXT("asda.Swim.ActionGate"), 0);
							const bool bJ0 = Me5->StartJump(false);
							S->Expect(bJ0 && ClipIs(Me5, AAsdaPlayerCharacter::kClipSwimIdle),
								FString::Printf(TEXT("E5 ключ asda.Swim.ActionGate 0 — прыжок в воде принят, клип плавания остаётся (донор): «%s»"), *Me5->GetPlayingClipName()));
							AsdaCVarSave::SetForTest(TEXT("asda.Swim.ActionGate"), 1);
							// Транспорт в воде.
							UAsdaWorldSubsystem* WS = W5->GetGameInstance()->GetSubsystem<UAsdaWorldSubsystem>();
							FString Model, Set; float H = 0.f;
							if (WS && WS->LookupMount(522, Model, Set, H))
							{
								Me5->SetMount(Model, Set, H, FString(), FString(), FString(), FString());
								S->Expect(Me5->IsMountInWater() && !Me5->IsSwimming() && !Me5->IsJumpActive(),
									FString::Printf(TEXT("E6 верхом в воде: транспорт в воде=%d, плывёт=%d, прыжок снят=%d"),
										Me5->IsMountInWater() ? 1 : 0, Me5->IsSwimming() ? 1 : 0, Me5->IsJumpActive() ? 0 : 1));
								Me5->ClearMount();
								S->Expect(Me5->IsSwimming() && ClipIs(Me5, AAsdaPlayerCharacter::kClipSwimIdle),
									FString::Printf(TEXT("E7 после высадки в воде снова плывёт, клип «%s»"), *Me5->GetPlayingClipName()));
							}
							else { S->Expect(false, TEXT("E6 транспорт 522 не найден")); }
							// Ход в воде: swim01; остановка — снова swimloop01.
							Me5->NetMove(Me5->GetGamePosXZ(), Me5->GetGamePosXZ() + FVector2D(200.f, 0.f), 0.35f);
							S->Expect(ClipIs(Me5, AAsdaPlayerCharacter::kClipSwimMove),
								FString::Printf(TEXT("E9 плывёт в движении: клип «%s» (ждём anim_237)"), *Me5->GetPlayingClipName()));
							Me5->StopMove();
							S->Expect(ClipIs(Me5, AAsdaPlayerCharacter::kClipSwimIdle),
								FString::Printf(TEXT("E10 остановился в воде: клип «%s» (ждём anim_238)"), *Me5->GetPlayingClipName()));
							AsdaCVarSave::SetForTest(TEXT("asda.Swim.Enable"), 0);
							Me5->NetSnapHard(S->WaterGame);
							S->Expect(!Me5->IsSwimming(), TEXT("F2 asda.Swim.Enable 0 — в воде не плывёт (эталон)"));
							AsdaCVarSave::SetForTest(TEXT("asda.Swim.Enable"), 1);
							Me5->NetSnapHard(S->LandGame);
							S->Expect(!Me5->IsSwimming() && !ClipIs(Me5, AAsdaPlayerCharacter::kClipSwimIdle),
								FString::Printf(TEXT("E8 на суше снова не плывёт, клип «%s»"), *Me5->GetPlayingClipName()));
							// ── G: настоящий путь клавиши — Пробел штатным вводом, опрос команды 95 в тике персонажа ──
							After(W5, 1.6f, [WW, S, Finish]()
							{
								UWorld* W6 = WW.Get(); AAsdaPlayerCharacter* Me6 = S->Me.Get();
								if (!W6 || !Me6) { Finish(); return; }
								const int32 N3 = S->Sent4024.Num();
								KeyEv(W6, EKeys::SpaceBar, true);
								After(W6, 0.3f, [WW, S, Finish, N3]()
								{
									UWorld* W7 = WW.Get(); AAsdaPlayerCharacter* Me7 = S->Me.Get();
									KeyEv(W7, EKeys::SpaceBar, false);
									S->Expect(Me7 && Me7->IsJumpActive() && S->Sent4024.Num() == N3 + 1 && S->Sent4024.Last() == AAsdaPlayerCharacter::kJumpActionCode,
										FString::Printf(TEXT("G1 Пробел (команда 95) → прыжок и 4024 код 131: прыгает=%d, 4024 +%d"),
											Me7 && Me7->IsJumpActive() ? 1 : 0, S->Sent4024.Num() - N3));
									// G2 (ревизия КРИТ-1): строка чата открыта — Пробел печатается, прыжка и 4024 нет.
									After(W7, 1.8f, [WW, S, Finish]()
									{
										UWorld* W8 = WW.Get(); AAsdaPlayerCharacter* Me8 = S->Me.Get();
										UAsdaChatWindowSubsystem* Cw = W8 ? W8->GetGameInstance()->GetSubsystem<UAsdaChatWindowSubsystem>() : nullptr;
										if (!W8 || !Me8 || !Cw) { S->Expect(false, TEXT("G2 нет чата")); Finish(); return; }
										Cw->OpenInput(0);
										const int32 N4 = S->Sent4024.Num();
										KeyEv(W8, EKeys::SpaceBar, true);
										After(W8, 0.3f, [WW, S, Finish, N4]()
										{
											UWorld* W9 = WW.Get(); AAsdaPlayerCharacter* Me9 = S->Me.Get();
											KeyEv(W9, EKeys::SpaceBar, false);
											UAsdaChatWindowSubsystem* Cw2 = W9 ? W9->GetGameInstance()->GetSubsystem<UAsdaChatWindowSubsystem>() : nullptr;
											const bool bInput = Cw2 && Cw2->IsInputActive();
											S->Expect(bInput && Me9 && !Me9->IsJumpActive() && S->Sent4024.Num() == N4,
												FString::Printf(TEXT("G2 набор в чате (строка открыта=%d): Пробел — не прыжок, 4024 +%d"), bInput ? 1 : 0, S->Sent4024.Num() - N4));
											if (Cw2) { Cw2->CloseInput(); }
											Finish();
										});
									});
								});
							});
						});
					});
				});
			});
		});
	}
}

static FAutoConsoleCommandWithWorldAndArgs GAsdaJumpSwimSelfTest(
	TEXT("asda.SelfTest.JumpSwim"),
	TEXT("Самотест прыжка и плавания из донора AsdaFableClassic: прыжок с места/с бега (4024 код 131), отказы, прыжок соседа "
	     "по 4025, глубокая вода у себя и соседа, брызги, транспорт в воде, ключи asda.Jump.Enable/asda.Swim.Enable. "
	     "Только офлайн. `delay=N` `quit`."),
	FConsoleCommandWithWorldAndArgsDelegate::CreateLambda([](const TArray<FString>& A, UWorld* W)
	{
		AsdaJumpSwimTest::Run(A, W);
	}));

// ═══ ★ 24.10.2026: САМОТЕСТ «ШАПКИ» НАД ГОЛОВОЙ В ПРЫЖКЕ (`asda.SelfTest.JumpOverhead [delay=N] [quit]`). [ПОРТ] ═══════════════
//   Владелец: при прыжке поднималось только тело, а HP/ник/звания/клан/значки (один виджет плашки) и корона оставались на месте.
//   Проверяет на настоящих путях: свой прыжок (`StartJump(false)`, 4024 перехвачен) и прыжок соседа (делегат 4025 код 131):
//     H1/H2  на земле до прыжка подъёма нет — точка плашки = её компонент (как раньше);
//     H3/H4  пик подъёма точки плашки своего и соседа совпадает с пиком подъёма кости корня (±3), а сам пик > 80;
//     H5     корона владельца добычи (6126 своему charId) в пике поднимается так же;
//     H6/H7  после клипа подъём 0, точка плашки и корона на прежней высоте (±0.5);
//     H8     ключ `asda.Jump.OverheadFollow 0` — тело прыгает, плашка на месте (поведение 23.10);
//     H9     плавание: плашка держится актора (в воде высота меняется у самого актора) — смещение плашки от актора как на суше.
//   Строки — «САМОТЕСТ-ПРЫЖОК-ШАПКА», итог — «… ИТОГ: ok=… fail=…».
namespace AsdaJumpOverheadTest
{
	static const TCHAR* const kCmd = TEXT("asda.SelfTest.JumpOverhead");
	static const TCHAR* const kPrefix = TEXT("САМОТЕСТ-ПРЫЖОК-ШАПКА");
	static constexpr int32 kPeerUserNum = 7772;     // сосед-заглушка
	static constexpr int32 kTestCharId = 4242424;   // свой charId на время теста (если сеть его не дала)

	// Пики подъёма за прыжок одного персонажа.
	struct FPeaks
	{
		float Body = 0.f;    // кость корня − отметка до прыжка
		float Plate = 0.f;   // точка плашки − точка до прыжка
		float Crown = 0.f;   // корона − до прыжка
	};

	// Состояние прогона (живёт между таймерами).
	struct FState
	{
		int32 Ok = 0, Fail = 0;                       // счётчики проверок
		uint32 CapGen = 0;                            // поколение перехвата отправок
		int32 SavedCharId = 0;                        // SelectedCharId до теста
		bool bCharIdSet = false;                      // тест подменил SelectedCharId
		TWeakObjectPtr<AAsdaPlayerCharacter> Me;      // свой
		TWeakObjectPtr<AAsdaPlayerCharacter> Peer;    // сосед
		TWeakObjectPtr<UAsdaNetSubsystem> Net;        // сеть
		TWeakObjectPtr<AAsdaEffectActor> Crown;       // корона своего
		float MeH0 = 0.f, PeerH0 = 0.f;               // высота корня до прыжка
		float MePlate0 = 0.f, PeerPlate0 = 0.f;       // Z точки плашки до прыжка
		float Crown0 = 0.f;                           // Z короны до прыжка
		float LandPlateOff = 0.f;                     // плашка − актор на суше (для H9)
		FVector2D LandGame = FVector2D::ZeroVector;   // точка старта на суше (вернуть туда в конце — следующие самотесты пакета)
		bool bLandKnown = false;                      // точка старта снята
		FPeaks MeP, PeerP;                            // пики
		FTimerHandle Sampler;                         // покадровый сбор пиков

		void Expect(bool b, const FString& What)
		{
			if (b) { ++Ok; UE_LOG(LogAsdaJumpSwimTest, Display, TEXT("%s ok   %s"), kPrefix, *What); }
			else   { ++Fail; UE_LOG(LogAsdaJumpSwimTest, Error, TEXT("%s FAIL %s"), kPrefix, *What); }
		}
	};

	// Плашка персонажа.
	static UAsdaNameplateComponent* PlateOf(const AAsdaPlayerCharacter* C)
	{
		return C ? C->FindComponentByClass<UAsdaNameplateComponent>() : nullptr;
	}
	// Z точки, которую плашка проецирует на экран (та же, что в `UpdateScreenPosition`).
	static float PlateZ(const AAsdaPlayerCharacter* C)
	{
		const UAsdaNameplateComponent* N = PlateOf(C);
		return N ? (float)N->GetPlateWorldPoint().Z : 0.f;
	}
	// Запуск шага через Delay секунд (мир мог уйти — тогда ничего).
	static void Later(const TWeakObjectPtr<UWorld>& WW, float Delay, TFunction<void()> Fn)
	{
		if (UWorld* W = WW.Get()) { AsdaJumpSwimTest::After(W, Delay, MoveTemp(Fn)); }
	}

	static void Run(const TArray<FString>& Args, UWorld* W)
	{
		using namespace AsdaJumpSwimTest;
		const FAsdaSelfTestArgs A = FAsdaSelfTestArgs::Parse(Args);
		if (!W) { AsdaSelfTestKit::Refused(nullptr, kCmd, A.bQuit, TEXT("нет мира")); return; }
		if (AsdaSelfTestKit::RefuseStart(W, kCmd, A)) { return; }
		TSharedRef<FAsdaSelfTestRunGate> Gate = MakeShared<FAsdaSelfTestRunGate>(kCmd, A.bQuit);
		TSharedRef<FState> S = MakeShared<FState>();
		TWeakObjectPtr<UWorld> WW(W);
		const bool bQuit = A.bQuit;

		// t = delay: офлайн-спавн своего павна штатными крюками.
		After(W, A.Delay, [WW, Gate]()
		{
			UWorld* Wd = WW.Get();
			if (!Wd || !Gate->Pass(Wd)) { return; }
			if (AsdaSelfTestKit::NeedSelfSpawn(Wd) && GEngine)
			{
				GEngine->Exec(Wd, TEXT("Asda.Load.CurrentMap 3"));
				GEngine->Exec(Wd, TEXT("Asda.World.FakeSelfSpawn 256 256"));
			}
		});

		// Финал (общий для всех веток): корона, charId, перехват, ключи — к исходным.
		auto Finish = [WW, S, bQuit]()
		{
			UWorld* Wd = WW.Get();
			if (!Wd) { return; }
			Wd->GetTimerManager().ClearTimer(S->Sampler);
			if (S->bLandKnown && S->Me.IsValid()) { S->Me->NetSnapHard(S->LandGame); }   // из воды H9 — назад на сушу
			if (UAsdaKingCrownSubsystem* Cr = Wd->GetGameInstance()->GetSubsystem<UAsdaKingCrownSubsystem>()) { Cr->DebugResetAll(); }
			if (S->bCharIdSet && S->Net.IsValid()) { S->Net->SelectedCharId = S->SavedCharId; }
			if (S->Net.IsValid() && S->CapGen) { AsdaSelfTestKit::DisarmCapture(S->Net.Get(), S->CapGen); S->CapGen = 0; }
			AsdaCVarSave::RestoreAll(kCmd);
			AsdaSelfTestKit::Finish(Wd, kCmd, bQuit, LogAsdaJumpSwimTest, kPrefix, S->Ok, S->Fail, TEXT(""));
		};

		// Покадровый сбор пиков за Dur секунд, затем Then().
		auto Sample = [WW, S](float Dur, TFunction<void()> Then)
		{
			UWorld* Wd = WW.Get();
			if (!Wd) { return; }
			S->MeP = FPeaks(); S->PeerP = FPeaks();
			const double T0 = Wd->GetTimeSeconds();
			Wd->GetTimerManager().SetTimer(S->Sampler, FTimerDelegate::CreateLambda([WW, S, T0, Dur, Then]()
			{
				UWorld* W2 = WW.Get();
				if (!W2) { return; }
				if (const AAsdaPlayerCharacter* Me = S->Me.Get())
				{
					S->MeP.Body = FMath::Max(S->MeP.Body, RootBoneHeight(Me) - S->MeH0);
					S->MeP.Plate = FMath::Max(S->MeP.Plate, PlateZ(Me) - S->MePlate0);
					if (const AAsdaEffectActor* Cr = S->Crown.Get()) { S->MeP.Crown = FMath::Max(S->MeP.Crown, (float)Cr->GetActorLocation().Z - S->Crown0); }
				}
				if (const AAsdaPlayerCharacter* P = S->Peer.Get())
				{
					S->PeerP.Body = FMath::Max(S->PeerP.Body, RootBoneHeight(P) - S->PeerH0);
					S->PeerP.Plate = FMath::Max(S->PeerP.Plate, PlateZ(P) - S->PeerPlate0);
				}
				if (W2->GetTimeSeconds() - T0 >= Dur)
				{
					// Снятие таймера уничтожает этот же делегат (и захваченный Then) — следующий шаг запускаем отдельным таймером
					//   с копией, а не из тела делегата.
					TFunction<void()> Next = Then;
					W2->GetTimerManager().ClearTimer(S->Sampler);
					After(W2, 0.01f, MoveTemp(Next));
				}
			}), 0.005f, true);
		};

		// t = delay + 2: подготовка.
		After(W, A.Delay + 2.f, [WW, Gate, S, bQuit, Finish, Sample]()
		{
			UWorld* Wd = WW.Get();
			if (!Wd || !Gate->Pass(Wd)) { return; }
			AAsdaPlayerController* PC = Cast<AAsdaPlayerController>(Wd->GetFirstPlayerController());
			AAsdaPlayerCharacter* Me = PC ? Cast<AAsdaPlayerCharacter>(PC->GetPawn()) : nullptr;
			UAsdaNetSubsystem* Net = AsdaSelfTestKit::NetOf(Wd);
			UAsdaWorldSubsystem* WS = Wd->GetGameInstance()->GetSubsystem<UAsdaWorldSubsystem>();
			UAsdaKingCrownSubsystem* Cr = Wd->GetGameInstance()->GetSubsystem<UAsdaKingCrownSubsystem>();
			if (!Me || !Net || !WS || !Cr || !PlateOf(Me)) { AsdaSelfTestKit::Refused(Wd, kCmd, bQuit, TEXT("нет своего персонажа / плашки / сети")); return; }
			S->Me = Me; S->Net = Net;
			S->LandGame = Me->GetGamePosXZ(); S->bLandKnown = true;
			S->CapGen = AsdaSelfTestKit::ArmCapture(Net, [](uint16, const uint8*, int32, bool, TCHAR) {}, 0, kCmd);   // 4024 прыжка — в перехват, не в сеть
			if (S->CapGen == 0) { AsdaSelfTestKit::Refused(Wd, kCmd, bQuit, TEXT("перехват отправок занят")); return; }
			AsdaCVarSave::ArmWorldGuard(Wd, kCmd);   // мир ушёл раньше итога — ключи вернёт сторож
			AsdaCVarSave::SetForTest(TEXT("asda.Jump.Enable"), 1);
			AsdaCVarSave::SetForTest(TEXT("asda.Swim.Enable"), 1);
			AsdaCVarSave::SetForTest(TEXT("asda.Jump.OverheadFollow"), 1);
			Me->SetAnimState(EAsdaPlayerState::Idle);
			// Сосед рядом и корона над своим (6126 своему charId — штатный приём подсистемы).
			WS->DebugSpawnFakePeer(kPeerUserNum, TEXT("Прыгун2"), Me->GetGamePosXZ() + FVector2D(150.f, 150.f));
			AAsdaPlayerCharacter* P = Cast<AAsdaPlayerCharacter>(WS->ActorByUserIdx(kPeerUserNum));
			S->Peer = P;
			if (P) { P->SetAnimState(EAsdaPlayerState::Idle); }
			S->SavedCharId = Net->SelectedCharId;
			if (Net->SelectedCharId == 0) { Net->SelectedCharId = kTestCharId; S->bCharIdSet = true; }
			Cr->DebugResetAll();
			Cr->OnCharIdByteUpdate(Net->SelectedCharId, 1);
			Cr->UpdateAllCrowns();
			bool bPend = false; int32 Vis = -1; AAsdaEffectActor* Fx = nullptr;
			Cr->GetCrownState(Net->SelectedCharId, bPend, Vis, Fx);
			S->Crown = Fx;
			S->Expect(Fx != nullptr, FString::Printf(TEXT("H0 корона над своим создана (charId %d)"), Net->SelectedCharId));
			S->Expect(P != nullptr && PlateOf(P) != nullptr, TEXT("H0 сосед с плашкой появился"));

			// Дать позе/плашке/короне устояться и снять отметки «на земле».
			Later(WW, 0.5f, [WW, S, Finish, Sample]()
			{
				AAsdaPlayerCharacter* Me2 = S->Me.Get(); AAsdaPlayerCharacter* P2 = S->Peer.Get();
				if (!WW.IsValid() || !Me2 || !P2) { Finish(); return; }
				const UAsdaNameplateComponent* N = PlateOf(Me2);
				S->Expect(Me2->GetOverheadLiftZ() == 0.f && FMath::IsNearlyEqual((float)N->GetPlateWorldPoint().Z, (float)N->GetComponentLocation().Z, 0.01f),
					FString::Printf(TEXT("H1 на земле подъёма нет: подъём %.2f, точка плашки %.2f = компонент %.2f"),
						Me2->GetOverheadLiftZ(), N->GetPlateWorldPoint().Z, N->GetComponentLocation().Z));
				S->Expect(P2->GetOverheadLiftZ() == 0.f, FString::Printf(TEXT("H2 у соседа на земле подъёма нет: %.2f"), P2->GetOverheadLiftZ()));
				S->MeH0 = RootBoneHeight(Me2); S->PeerH0 = RootBoneHeight(P2);
				S->MePlate0 = PlateZ(Me2); S->PeerPlate0 = PlateZ(P2);
				S->Crown0 = S->Crown.IsValid() ? (float)S->Crown->GetActorLocation().Z : 0.f;
				S->LandPlateOff = S->MePlate0 - (float)Me2->GetActorLocation().Z;
				const bool bMe = Me2->StartJump(false);
				if (S->Net.IsValid()) { S->Net->OnCharAction.Broadcast(kPeerUserNum, AAsdaPlayerCharacter::kJumpActionCode, 0.f, 0.f); }
				S->Expect(bMe && P2->IsJumpActive(), FString::Printf(TEXT("H2 прыжок начат: свой=%d, сосед по 4025=%d"), bMe ? 1 : 0, P2->IsJumpActive() ? 1 : 0));
				Sample(1.4f, [WW, S, Finish, Sample]()
				{
					const FPeaks M = S->MeP, Pp = S->PeerP;
					S->Expect(M.Body > 80.f && FMath::Abs(M.Plate - M.Body) <= 3.f,
						FString::Printf(TEXT("H3 свой: пик тела %.1f, пик плашки %.1f (разница ≤ 3)"), M.Body, M.Plate));
					S->Expect(Pp.Body > 80.f && FMath::Abs(Pp.Plate - Pp.Body) <= 3.f,
						FString::Printf(TEXT("H4 сосед: пик тела %.1f, пик плашки %.1f (разница ≤ 3)"), Pp.Body, Pp.Plate));
					S->Expect(S->Crown.IsValid() && FMath::Abs(M.Crown - M.Body) <= 3.f,
						FString::Printf(TEXT("H5 корона: пик %.1f при пике тела %.1f (разница ≤ 3)"), M.Crown, M.Body));
					Later(WW, 0.8f, [WW, S, Finish, Sample]()
					{
						AAsdaPlayerCharacter* Me3 = S->Me.Get(); AAsdaPlayerCharacter* P3 = S->Peer.Get();
						if (!WW.IsValid() || !Me3 || !P3) { Finish(); return; }
						S->Expect(!Me3->IsJumpActive() && Me3->GetOverheadLiftZ() == 0.f && FMath::Abs(PlateZ(Me3) - S->MePlate0) <= 0.5f
							&& P3->GetOverheadLiftZ() == 0.f && FMath::Abs(PlateZ(P3) - S->PeerPlate0) <= 0.5f,
							FString::Printf(TEXT("H6 приземлились: подъём свой %.2f / сосед %.2f, плашка Δ%.2f / Δ%.2f"),
								Me3->GetOverheadLiftZ(), P3->GetOverheadLiftZ(), PlateZ(Me3) - S->MePlate0, PlateZ(P3) - S->PeerPlate0));
						S->Expect(S->Crown.IsValid() && FMath::Abs((float)S->Crown->GetActorLocation().Z - S->Crown0) <= 0.5f,
							FString::Printf(TEXT("H7 корона на прежней высоте: Δ%.2f"), S->Crown.IsValid() ? (float)S->Crown->GetActorLocation().Z - S->Crown0 : -1.f));
						// H8: ключ отката.
						AsdaCVarSave::SetForTest(TEXT("asda.Jump.OverheadFollow"), 0);
						Me3->StartJump(false);
						Sample(1.0f, [WW, S, Finish]()
						{
							const FPeaks M = S->MeP;
							S->Expect(M.Body > 80.f && M.Plate <= 0.5f,
								FString::Printf(TEXT("H8 asda.Jump.OverheadFollow 0: тело %.1f, плашка %.2f (на месте, как 23.10)"), M.Body, M.Plate));
							AsdaCVarSave::SetForTest(TEXT("asda.Jump.OverheadFollow"), 1);
							Later(WW, 0.8f, [WW, S, Finish]()
							{
								UWorld* W4 = WW.Get(); AAsdaPlayerCharacter* Me4 = S->Me.Get();
								if (!W4 || !Me4) { Finish(); return; }
								// H9: плавание — высота меняется у актора, плашка держится его.
								FVector2D WaterGame = FVector2D::ZeroVector; float WaterZ = 0.f;
								if (!AsdaJumpSwimTest::FindDeepWater(W4, WaterGame, WaterZ)) { S->Expect(false, TEXT("H9 глубокая вода не найдена")); Finish(); return; }
								Me4->NetSnapHard(WaterGame);
								Later(WW, 1.0f, [S, Finish, WaterZ]()
								{
									AAsdaPlayerCharacter* Me5 = S->Me.Get();
									if (!Me5) { Finish(); return; }
									const float Off = PlateZ(Me5) - (float)Me5->GetActorLocation().Z;
									S->Expect(Me5->IsSwimming() && FMath::IsNearlyEqual((float)Me5->GetActorLocation().Z, WaterZ - 20.f, 2.f)
										&& FMath::Abs(Off - S->LandPlateOff) <= 1.f && Me5->GetOverheadLiftZ() == 0.f,
										FString::Printf(TEXT("H9 в воде плывёт=%d, ноги %.1f (поверхность %.1f), плашка над актором %.1f (на суше %.1f)"),
											Me5->IsSwimming() ? 1 : 0, Me5->GetActorLocation().Z, WaterZ, Off, S->LandPlateOff));
									Finish();
								});
							});
						});
					});
				});
			});
		});
	}
}

static FAutoConsoleCommandWithWorldAndArgs GAsdaJumpOverheadSelfTest(
	TEXT("asda.SelfTest.JumpOverhead"),
	TEXT("Самотест «шапки» над головой в прыжке: плашка своего и соседа и корона поднимаются с телом (пик ±3), на земле — как раньше, "
	     "ключ asda.Jump.OverheadFollow 0, плавание. Только офлайн. `delay=N` `quit`."),
	FConsoleCommandWithWorldAndArgsDelegate::CreateLambda([](const TArray<FString>& A, UWorld* W)
	{
		AsdaJumpOverheadTest::Run(A, W);
	}));

#endif // !UE_BUILD_SHIPPING
