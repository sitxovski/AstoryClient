// Copyright (c) 2026 @sitxovski (Discord)
// ★★★ САМОТЕСТ ПРИСЕДА СТОЯ (05.11.2026, баг «присед срабатывает только в движении»). [ПОРТ] Разбор — `World/AsdaCrouchStand.h`.
//
// ЧТО ПРОВЕРЯЕТ (настоящие входы: опрос Ctrl `TickCrouchInput`, отправка 4006/4008/4024 через перехват, делегаты сети 4007/4025,
//   спаун соседа с кодом появления — тот же путь, что пакеты):
//   A. Свой присед стоя по Ctrl: клип anim_239, темп 0, кадр позы (0,333 с), 4024 код 132; поза держится; фиджета нет.
//   B. Ход вприсядку — клип anim_239 идёт (темп ≠ 0), 4024 в ходу не уходит; остановка — снова поза стоя.
//   C. Свой 4006 (статус 4) и 4008 → 4024 132 заново (сервер на 4008 пишет статус 0); вставание стоя — покой и 4024 133;
//      быстрое «вкл→выкл» за одно нажатие — ничего лишнего не уходит.
//   D. Невидимость (112/186) стоя — та же поза, без 4024.
//   E. Сосед: 4025 132 → поза приседа стоя, 4007 статус 4 → присед в ходу, 4009 → поза стоя, 4025 133 → покой; спаун с кодом 132 →
//      сразу поза; смерть соседа снимает его присед.
//   F. Ключи `asda.Crouch.StandPose 0` / `asda.Crouch.StandRelay 0` — прежнее поведение.
//   Кадры позы — `Reports/crouch_stand/shots/*.png` (стоя, в ходу, сосед стоя/в ходу, покой для сравнения).
// Консоль: `asda.SelfTest.CrouchStand [delay=N] [quit]`. Только офлайн. Строки — «САМОТЕСТ-ПРИСЕД-СТОЯ», итог — «… ИТОГ: ok=… fail=…».
#include "CoreMinimal.h"

#if !UE_BUILD_SHIPPING

#include "AsdaBytes.h"
#include "AsdaNetSubsystem.h"
#include "AsdaCVarSave.h"
#include "Test/AsdaSelfTestRunGate.h"   // FAsdaSelfTestRunGate — общий гейт запуска самотеста   ★ R3: FAsdaSelfTestRunGate — узкий заголовок вместо AsdaOutgoingGate.h
#include "Test/AsdaSelfTestKit.h"
#include "World/AsdaPlayerCharacter.h"
#include "World/AsdaPlayerController.h"
#include "World/AsdaWorldSubsystem.h"
#include "World/AsdaCrouchStand.h"
#include "World/AsdaStealthSubsystem.h"   // D: подмена «есть 112/186» (подсистема ставит признак каждый кадр по списку эффектов)
#include "Components/SkeletalMeshComponent.h"
#include "Animation/AnimSingleNodeInstance.h"
#include "Engine/Engine.h"
#include "Engine/World.h"
#include "HAL/IConsoleManager.h"
#include "Misc/Paths.h"
#include "TimerManager.h"
#include "UnrealClient.h"   // FScreenshotRequest — кадры позы

DEFINE_LOG_CATEGORY_STATIC(LogAsdaCrouchStandTest, Log, All);

namespace AsdaCrouchStandTest
{
	static const TCHAR* const kCmd = TEXT("asda.SelfTest.CrouchStand");
	static const TCHAR* const kPrefix = TEXT("САМОТЕСТ-ПРИСЕД-СТОЯ");
	static constexpr int32 kPeerUserNum = 7781;   // сосед-заглушка
	static constexpr int32 kClip = AAsdaPlayerCharacter::kClipCrouchWalk;

	// Состояние прогона (живёт между таймерами).
	struct FState
	{
		int32 Ok = 0, Fail = 0;                       // счётчики проверок
		uint32 CapGen = 0;                            // поколение перехвата отправок
		TArray<int32> Sent4024;                       // коды перехваченных 4024
		TArray<int32> Sent4006Status;                 // статус @+3 перехваченных 4006
		int32 Sent4008 = 0;                           // перехваченных 4008
		FString ShotDir;                              // папка кадров
		TWeakObjectPtr<AAsdaPlayerCharacter> Me;      // свой персонаж
		TWeakObjectPtr<AAsdaPlayerCharacter> Peer;    // сосед-заглушка
		TWeakObjectPtr<UAsdaNetSubsystem> Net;        // сеть
		TWeakObjectPtr<UAsdaWorldSubsystem> Ws;       // мир
		TWeakObjectPtr<UWorld> World;                 // мир прогона

		void Expect(bool b, const FString& What)
		{
			if (b) { ++Ok; UE_LOG(LogAsdaCrouchStandTest, Display, TEXT("%s ok   %s"), kPrefix, *What); }
			else   { ++Fail; UE_LOG(LogAsdaCrouchStandTest, Error, TEXT("%s FAIL %s"), kPrefix, *What); }
		}
	};

	// Компонент тела персонажа (первый скелетный — как в самотесте приседа).
	static USkeletalMeshComponent* Body(const AAsdaPlayerCharacter* C)
	{
		TArray<USkeletalMeshComponent*> Comps;
		if (C) { C->GetComponents<USkeletalMeshComponent>(Comps); }
		return Comps.Num() ? Comps[0] : nullptr;
	}
	static bool ClipIs(const AAsdaPlayerCharacter* C, int32 Idx)
	{
		return C && C->GetPlayingClipName().EndsWith(FString::Printf(TEXT("anim_%d"), Idx));
	}
	static float RateOf(const AAsdaPlayerCharacter* C) { const USkeletalMeshComponent* B = Body(C); return B ? B->GetPlayRate() : -99.f; }
	static float PosOf(const AAsdaPlayerCharacter* C)
	{
		USkeletalMeshComponent* B = Body(C);
		const UAnimSingleNodeInstance* I = B ? B->GetSingleNodeInstance() : nullptr;
		return I ? I->GetCurrentTime() : -1.f;
	}
	// Поза приседа стоя на экране: клип anim_239, темп 0, кадр позы.
	static bool InStandPose(const AAsdaPlayerCharacter* C)
	{
		return ClipIs(C, kClip) && FMath::IsNearlyZero(RateOf(C)) && FMath::IsNearlyEqual(PosOf(C), AsdaCrouchStand::PoseSec(), 0.02f);
	}
	static FString Describe(const AAsdaPlayerCharacter* C)
	{
		return FString::Printf(TEXT("клип «%s», темп %.2f, время %.3f с"), C ? *C->GetPlayingClipName() : TEXT("-"), RateOf(C), PosOf(C));
	}
	static void Shot(const FState& S, const TCHAR* Name)
	{
		FScreenshotRequest::RequestScreenshot(FPaths::Combine(S.ShotDir, FString(Name) + TEXT(".png")), /*bShowUI=*/false, /*bAddUniqueSuffix=*/false);
	}
	// Нажатие и отпускание Ctrl через опрос ввода персонажа (тот же путь, что тик контроллера; «Нажатие»).
	static void CtrlTap(AAsdaPlayerCharacter* Me)
	{
		Me->TickCrouchInput(true, false, true);
		Me->TickCrouchInput(false, false, true);
	}

	// Шаги прогона: (задержка до шага, тело). Цепочка таймеров — по одному шагу.
	struct FStep { float Delay; TFunction<void()> Fn; };
	static void RunSteps(TWeakObjectPtr<UWorld> WW, TSharedRef<TArray<FStep>> Steps, int32 I)
	{
		UWorld* W = WW.Get();
		if (!W || I >= Steps->Num()) { return; }
		FTimerHandle H;
		W->GetTimerManager().SetTimer(H, FTimerDelegate::CreateLambda([WW, Steps, I]()
		{
			(*Steps)[I].Fn();
			RunSteps(WW, Steps, I + 1);
		}), FMath::Max(0.01f, (*Steps)[I].Delay), false);
	}

	static void Run(const TArray<FString>& Args, UWorld* W)
	{
		const FAsdaSelfTestArgs A = FAsdaSelfTestArgs::Parse(Args);
		if (!W) { AsdaSelfTestKit::Refused(nullptr, kCmd, A.bQuit, TEXT("нет мира")); return; }
		if (AsdaSelfTestKit::RefuseStart(W, kCmd, A)) { return; }
		TSharedRef<FAsdaSelfTestRunGate> Gate = MakeShared<FAsdaSelfTestRunGate>(kCmd, A.bQuit);
		TSharedRef<FState> S = MakeShared<FState>();
		TWeakObjectPtr<UWorld> WW(W);
		S->World = W;
		S->ShotDir = FPaths::ConvertRelativePathToFull(FPaths::Combine(FPaths::ProjectDir(), TEXT("../Reports/crouch_stand/shots")));
		const bool bQuit = A.bQuit;
		TSharedRef<TArray<FStep>> St = MakeShared<TArray<FStep>>();
		TSharedRef<bool> Abort = MakeShared<bool>(false);   // прогон прерван (нет персонажа / перехвата) — шаги молчат

		auto Finish = [WW, S, bQuit]()
		{
			UWorld* Wd = WW.Get();
			if (!Wd) { return; }
			if (S->Net.IsValid() && S->CapGen) { AsdaSelfTestKit::DisarmCapture(S->Net.Get(), S->CapGen); S->CapGen = 0; }
			if (AAsdaPlayerCharacter* Me = S->Me.Get())
			{
				Me->SetCrouch(false, TEXT("самотест: итог"));
				if (UAsdaStealthSubsystem* Sx = Me->GetGameInstance()->GetSubsystem<UAsdaStealthSubsystem>()) { Sx->DebugForceInvisible(Me, -1); }
			}
			AsdaCVarSave::RestoreAll(kCmd);
			AsdaSelfTestKit::Finish(Wd, kCmd, bQuit, LogAsdaCrouchStandTest, kPrefix, S->Ok, S->Fail,
				*FString::Printf(TEXT(" (4024: %d, 4006: %d, 4008: %d)"), S->Sent4024.Num(), S->Sent4006Status.Num(), S->Sent4008));
		};
		auto Live = [S, Abort]() { return !*Abort && S->Me.IsValid() && S->Net.IsValid(); };

		// t = delay: офлайн-спавн своего павна штатными крюками.
		St->Add({ A.Delay, [WW, Gate]()
		{
			UWorld* Wd = WW.Get();
			if (!Wd || !Gate->Pass(Wd)) { return; }
			if (AsdaSelfTestKit::NeedSelfSpawn(Wd) && GEngine)
			{
				GEngine->Exec(Wd, TEXT("Asda.Load.CurrentMap 3"));
				GEngine->Exec(Wd, TEXT("Asda.World.FakeSelfSpawn 256 256"));
			}
		} });
		// Подготовка: персонаж, перехват 4006/4008/4024, ключи.
		St->Add({ 2.f, [WW, Gate, S, Abort, bQuit]()
		{
			UWorld* Wd = WW.Get();
			if (!Wd || !Gate->Pass(Wd)) { *Abort = true; return; }
			AAsdaPlayerController* PC = Cast<AAsdaPlayerController>(Wd->GetFirstPlayerController());
			AAsdaPlayerCharacter* Me = PC ? Cast<AAsdaPlayerCharacter>(PC->GetPawn()) : nullptr;
			UAsdaNetSubsystem* Net = AsdaSelfTestKit::NetOf(Wd);
			if (!Me || !Net) { *Abort = true; AsdaSelfTestKit::Refused(Wd, kCmd, bQuit, TEXT("нет своего персонажа / сети")); return; }
			S->Me = Me; S->Net = Net; S->Ws = Wd->GetGameInstance()->GetSubsystem<UAsdaWorldSubsystem>();
			S->CapGen = AsdaSelfTestKit::ArmCapture(Net, [S](uint16 Op, const uint8* P, int32 Len, bool, TCHAR)
			{
				if (Op == 4024 && P && Len >= 29) { S->Sent4024.Add((int32)AsdaBytes::RdU16(P + 18)); }
				if (Op == 4006 && P && Len >= 20) { S->Sent4006Status.Add((int32)AsdaBytes::RdU16(P + 16 + 3)); }
				if (Op == 4008) { ++S->Sent4008; }
			}, 0, kCmd);
			if (S->CapGen == 0) { *Abort = true; AsdaSelfTestKit::Refused(Wd, kCmd, bQuit, TEXT("перехват отправок занят")); return; }
			AsdaCVarSave::ArmWorldGuard(Wd, kCmd);
			AsdaCVarSave::SetForTest(TEXT("asda.Crouch.Enable"), 1);
			AsdaCVarSave::SetForTest(TEXT("asda.Crouch.HideAnim"), 1);
			AsdaCVarSave::SetForTest(TEXT("asda.Crouch.HoldMode"), 0);   // «Нажатие» — одиночный Ctrl
			AsdaCVarSave::SetForTest(TEXT("asda.Crouch.StandPose"), 1);
			AsdaCVarSave::SetForTest(TEXT("asda.Crouch.StandPoseFrame"), AsdaCrouchStand::kPoseFrameDefault);
			AsdaCVarSave::SetForTest(TEXT("asda.Crouch.StandRelay"), 1);
			Me->SetCrouch(false, TEXT("самотест: старт"));
			Me->SetSkillStealth(false);
			Me->StopMove();
			Me->SetAnimState(EAsdaPlayerState::Idle);
		} });
		// Кадр сравнения: обычный покой.
		St->Add({ 1.0f, [S, Live]()
		{
			if (!Live()) { return; }
			AAsdaPlayerCharacter* Me = S->Me.Get();
			S->Expect(!Me->IsCrouchOn() && !ClipIs(Me, kClip), FString::Printf(TEXT("A0 стоит без приседа — покой: %s"), *Describe(Me)));
			Shot(*S, TEXT("00_idle_normal"));
		} });
		// A: Ctrl стоя.
		TSharedRef<int32> NA = MakeShared<int32>(0);   // число 4024 до шага (сравнение «ушло ли»)
		St->Add({ 0.6f, [S, Live, NA]()
		{
			if (!Live()) { return; }
			AAsdaPlayerCharacter* Me = S->Me.Get();
			*NA = S->Sent4024.Num();
			CtrlTap(Me);
			S->Expect(Me->IsCrouchOn(), FString::Printf(TEXT("A1 Ctrl стоя — присед включён (отказ: «%s»)"), *Me->GetLastCrouchRefusal()));
		} });
		St->Add({ 0.5f, [S, Live, NA]()
		{
			if (!Live()) { return; }
			AAsdaPlayerCharacter* Me = S->Me.Get();
			S->Expect(S->Sent4024.Num() == *NA + 1 && S->Sent4024.Last() == AsdaCrouchStand::kStandOnCode,
				FString::Printf(TEXT("A2 соседям ушёл 4024 код %d (ждём 132), отправок %d"), S->Sent4024.Num() > *NA ? S->Sent4024.Last() : -1, S->Sent4024.Num() - *NA));
			S->Expect(InStandPose(Me), FString::Printf(TEXT("A3 поза приседа стоя (кадр %.3f с): %s"), AsdaCrouchStand::PoseSec(), *Describe(Me)));
			Shot(*S, TEXT("01_crouch_stand"));
		} });
		St->Add({ 1.5f, [S, Live]()
		{
			if (!Live()) { return; }
			AAsdaPlayerCharacter* Me = S->Me.Get();
			S->Expect(InStandPose(Me), FString::Printf(TEXT("A4 через 1,5 с поза держится (кадр не уехал): %s"), *Describe(Me)));
		} });
		// B: ход вприсядку и остановка.
		St->Add({ 0.1f, [S, Live, NA]()
		{
			if (!Live()) { return; }
			AAsdaPlayerCharacter* Me = S->Me.Get();
			*NA = S->Sent4024.Num();
			const FVector2D From = Me->GetGamePosXZ();
			Me->NetMove(From, From + FVector2D(900.f, 0.f), 0.175f);
		} });
		St->Add({ 0.5f, [S, Live, NA]()
		{
			if (!Live()) { return; }
			AAsdaPlayerCharacter* Me = S->Me.Get();
			S->Expect(Me->IsMoving() && ClipIs(Me, kClip) && FMath::Abs(RateOf(Me)) > 0.1f,
				FString::Printf(TEXT("B1 ход вприсядку — клип идёт: %s"), *Describe(Me)));
			S->Expect(S->Sent4024.Num() == *NA, FString::Printf(TEXT("B2 в ходу 4024 не уходит (сток его отбрасывает): +%d"), S->Sent4024.Num() - *NA));
			Shot(*S, TEXT("02_crouch_move"));
		} });
		St->Add({ 0.4f, [S, Live]()
		{
			if (!Live()) { return; }
			S->Me->StopMove();
		} });
		St->Add({ 0.5f, [S, Live]()
		{
			if (!Live()) { return; }
			AAsdaPlayerCharacter* Me = S->Me.Get();
			S->Expect(!Me->IsMoving() && InStandPose(Me), FString::Printf(TEXT("B3 остановка вприсядку — снова поза стоя: %s"), *Describe(Me)));
		} });
		// C: свой 4006 (статус 4) и 4008 настоящими отправками (перехват) → 4024 132 заново.
		St->Add({ 0.2f, [S, Live, NA]()
		{
			if (!Live()) { return; }
			AAsdaPlayerCharacter* Me = S->Me.Get(); UAsdaNetSubsystem* Net = S->Net.Get();
			const FVector2D C = Me->GetGamePosXZ();
			const int32 M0 = S->Sent4006Status.Num();
			Net->SendMove4006((float)C.X, (float)C.Y, (float)C.X + 300.f, (float)C.Y, -1);
			S->Expect(S->Sent4006Status.Num() == M0 + 1 && S->Sent4006Status.Last() == AAsdaPlayerCharacter::kCrouchMoveStatus,
				FString::Printf(TEXT("C1 свой 4006 в приседе — статус %d (ждём 4)"), S->Sent4006Status.Num() > M0 ? S->Sent4006Status.Last() : -1));
			*NA = S->Sent4024.Num();
			const int32 Stop0 = S->Sent4008;
			const bool bStop = Net->SendStop4008((float)C.X, (float)C.Y);
			S->Expect(bStop && S->Sent4008 == Stop0 + 1, FString::Printf(TEXT("C2 свой 4008 ушёл: %d"), bStop ? 1 : 0));
		} });
		St->Add({ 0.5f, [S, Live, NA]()
		{
			if (!Live()) { return; }
			S->Expect(S->Sent4024.Num() == *NA + 1 && S->Sent4024.Last() == AsdaCrouchStand::kStandOnCode,
				FString::Printf(TEXT("C3 после 4008 присевший объявил присед стоя заново: 4024 код %d, отправок %d"),
					S->Sent4024.Num() > *NA ? S->Sent4024.Last() : -1, S->Sent4024.Num() - *NA));
		} });
		St->Add({ 0.4f, [S, Live, NA]()
		{
			if (!Live()) { return; }
			AAsdaPlayerCharacter* Me = S->Me.Get();
			*NA = S->Sent4024.Num();
			CtrlTap(Me);
			S->Expect(!Me->IsCrouchOn(), TEXT("C4 Ctrl стоя — встал"));
		} });
		St->Add({ 0.5f, [S, Live, NA]()
		{
			if (!Live()) { return; }
			AAsdaPlayerCharacter* Me = S->Me.Get();
			S->Expect(S->Sent4024.Num() == *NA + 1 && S->Sent4024.Last() == AsdaCrouchStand::kStandOffCode,
				FString::Printf(TEXT("C4b соседям ушёл 4024 код %d (ждём 133)"), S->Sent4024.Num() > *NA ? S->Sent4024.Last() : -1));
			S->Expect(!ClipIs(Me, kClip) && FMath::IsNearlyEqual(RateOf(Me), 1.f, 0.01f), FString::Printf(TEXT("C5 встал — обычный покой: %s"), *Describe(Me)));
			// Быстрое «вкл→выкл» (два нажатия подряд, раньше 250 мс после прошлой отправки): соседи уже знают «встал» — ничего не уходит.
			*NA = S->Sent4024.Num();
			CtrlTap(Me); CtrlTap(Me);
		} });
		St->Add({ 0.6f, [S, Live, NA]()
		{
			if (!Live()) { return; }
			AAsdaPlayerCharacter* Me = S->Me.Get();
			S->Expect(!Me->IsCrouchOn() && S->Sent4024.Num() == *NA, FString::Printf(TEXT("C6 вкл→выкл подряд — лишних 4024 нет: +%d"), S->Sent4024.Num() - *NA));
		} });
		// D: невидимость стоя.
		St->Add({ 0.1f, [S, Live, NA]()
		{
			if (!Live()) { return; }
			*NA = S->Sent4024.Num();
			if (UAsdaStealthSubsystem* Sx = S->Me->GetGameInstance()->GetSubsystem<UAsdaStealthSubsystem>()) { Sx->DebugForceInvisible(S->Me.Get(), 1); }
		} });
		St->Add({ 0.5f, [S, Live, NA]()
		{
			if (!Live()) { return; }
			AAsdaPlayerCharacter* Me = S->Me.Get();
			S->Expect(InStandPose(Me) && S->Sent4024.Num() == *NA,
				FString::Printf(TEXT("D1 невидимость стоя — поза приседа, 4024 нет (+%d): %s"), S->Sent4024.Num() - *NA, *Describe(Me)));
			if (UAsdaStealthSubsystem* Sx = Me->GetGameInstance()->GetSubsystem<UAsdaStealthSubsystem>()) { Sx->DebugForceInvisible(Me, 0); }
		} });
		St->Add({ 0.5f, [S, Live]()
		{
			if (!Live()) { return; }
			AAsdaPlayerCharacter* Me = S->Me.Get();
			S->Expect(!ClipIs(Me, kClip), FString::Printf(TEXT("D2 невидимость снята — покой: %s"), *Describe(Me)));
		} });
		// E: сосед.
		St->Add({ 0.1f, [S, Live]()
		{
			if (!Live() || !S->Ws.IsValid()) { return; }
			AAsdaPlayerCharacter* Me = S->Me.Get();
			S->Ws->DebugSpawnFakePeer(kPeerUserNum, TEXT("Присед"), Me->GetGamePosXZ() + FVector2D(200.f, 150.f));
			S->Peer = Cast<AAsdaPlayerCharacter>(S->Ws->ActorByUserIdx(kPeerUserNum));
			S->Expect(S->Peer.IsValid(), TEXT("E0 сосед-заглушка появился"));
		} });
		St->Add({ 0.5f, [S, Live]()
		{
			if (!Live() || !S->Peer.IsValid()) { return; }
			AAsdaPlayerCharacter* P = S->Peer.Get();
			S->Net->OnCharAction.Broadcast(kPeerUserNum, AsdaCrouchStand::kStandOnCode, 0.f, 0.f);
			S->Expect(P->IsNetCrouch(), TEXT("E1 сосед 4025 132 — присед соседа"));
		} });
		St->Add({ 0.5f, [S, Live]()
		{
			if (!Live() || !S->Peer.IsValid()) { return; }
			AAsdaPlayerCharacter* P = S->Peer.Get();
			S->Expect(InStandPose(P), FString::Printf(TEXT("E2 сосед стоя — поза приседа: %s"), *Describe(P)));
			Shot(*S, TEXT("03_peer_crouch_stand"));
			const FVector2D Pc = P->GetGamePosXZ();
			S->Net->OnEntityMove.Broadcast(kPeerUserNum, 1, Pc, Pc + FVector2D(0.f, 700.f), 0.175f, false, AAsdaPlayerCharacter::kCrouchMoveStatus, -1);
		} });
		St->Add({ 0.5f, [S, Live]()
		{
			if (!Live() || !S->Peer.IsValid()) { return; }
			AAsdaPlayerCharacter* P = S->Peer.Get();
			S->Expect(P->IsNetCrouch() && ClipIs(P, kClip) && FMath::Abs(RateOf(P)) > 0.1f, FString::Printf(TEXT("E3 сосед 4007 статус 4 — присед в ходу: %s"), *Describe(P)));
			Shot(*S, TEXT("04_peer_crouch_move"));
			S->Net->OnEntityMove.Broadcast(kPeerUserNum, 1, P->GetGamePosXZ(), P->GetGamePosXZ(), 0.175f, true, 0, -1);   // 4009 — стоп
		} });
		St->Add({ 0.5f, [S, Live]()
		{
			if (!Live() || !S->Peer.IsValid()) { return; }
			AAsdaPlayerCharacter* P = S->Peer.Get();
			S->Expect(P->IsNetCrouch() && InStandPose(P), FString::Printf(TEXT("E4 сосед остановился вприсядку (4009) — поза стоя: %s"), *Describe(P)));
			S->Net->OnCharAction.Broadcast(kPeerUserNum, AsdaCrouchStand::kStandOffCode, 0.f, 0.f);
		} });
		St->Add({ 0.5f, [S, Live]()
		{
			if (!Live() || !S->Peer.IsValid()) { return; }
			AAsdaPlayerCharacter* P = S->Peer.Get();
			S->Expect(!P->IsNetCrouch() && !ClipIs(P, kClip), FString::Printf(TEXT("E5 сосед 4025 133 — встал, покой: %s"), *Describe(P)));
			// E6: появление с кодом 132.
			S->Ws->DebugSpawnFakePeerState(kPeerUserNum + 1, TEXT("Присед2"), S->Me->GetGamePosXZ() + FVector2D(-200.f, 150.f), AsdaCrouchStand::kStandOnCode);
		} });
		St->Add({ 0.5f, [S, Live]()
		{
			if (!Live()) { return; }
			const AAsdaPlayerCharacter* P2 = Cast<AAsdaPlayerCharacter>(S->Ws->ActorByUserIdx(kPeerUserNum + 1));
			S->Expect(P2 && P2->IsNetCrouch() && InStandPose(P2), FString::Printf(TEXT("E6 сосед появился с кодом 132 — сразу поза стоя: %s"), *Describe(P2)));
			Shot(*S, TEXT("05_peer_spawn_crouch_stand"));
			// E7: смерть соседа снимает его присед.
			if (AAsdaPlayerCharacter* P = S->Peer.Get())
			{
				S->Net->OnCharAction.Broadcast(kPeerUserNum, AsdaCrouchStand::kStandOnCode, 0.f, 0.f);
				P->SetAnimState(EAsdaPlayerState::Dead);
				S->Expect(!P->IsNetCrouch(), TEXT("E7 смерть соседа снимает его присед"));
			}
		} });
		// F: ключи 0 — прежнее поведение.
		St->Add({ 0.3f, [S, Live, NA]()
		{
			if (!Live()) { return; }
			AAsdaPlayerCharacter* Me = S->Me.Get();
			AsdaCVarSave::SetForTest(TEXT("asda.Crouch.StandPose"), 0);
			AsdaCVarSave::SetForTest(TEXT("asda.Crouch.StandRelay"), 0);
			*NA = S->Sent4024.Num();
			CtrlTap(Me);
		} });
		St->Add({ 0.5f, [S, Live, NA]()
		{
			if (!Live()) { return; }
			AAsdaPlayerCharacter* Me = S->Me.Get();
			S->Expect(Me->IsCrouchOn() && !ClipIs(Me, kClip) && S->Sent4024.Num() == *NA,
				FString::Printf(TEXT("F1 ключи 0: присед стоя — обычный покой и без 4024 (как до 05.11): %s, +%d"), *Describe(Me), S->Sent4024.Num() - *NA));
			if (AAsdaPlayerCharacter* P2 = Cast<AAsdaPlayerCharacter>(S->Ws->ActorByUserIdx(kPeerUserNum + 1)))
			{
				S->Net->OnCharAction.Broadcast(kPeerUserNum + 1, AsdaCrouchStand::kStandOffCode, 0.f, 0.f);
				S->Expect(P2->IsNetCrouch(), TEXT("F2 ключ StandRelay 0 — чужой 133 не обрабатывается (как до 05.11)"));
			}
			CtrlTap(Me);
		} });
		St->Add({ 0.3f, [Finish]() { Finish(); } });
		RunSteps(WW, St, 0);
	}
}

static FAutoConsoleCommandWithWorldAndArgs GAsdaCrouchStandSelfTest(
	TEXT("asda.SelfTest.CrouchStand"),
	TEXT("Самотест приседа стоя: поза кадром anim_239 (свой по Ctrl, невидимость, сосед), ход и остановка, 4024 132/133 после "
	     "Ctrl/4008, чужие 4025 132/133 и код появления 132, ключи asda.Crouch.StandPose/StandRelay 0. Кадры — Reports/crouch_stand/shots. "
	     "Только офлайн. `delay=N` `quit`."),
	FConsoleCommandWithWorldAndArgsDelegate::CreateLambda([](const TArray<FString>& A, UWorld* W)
	{
		AsdaCrouchStandTest::Run(A, W);
	}));

#endif // !UE_BUILD_SHIPPING
