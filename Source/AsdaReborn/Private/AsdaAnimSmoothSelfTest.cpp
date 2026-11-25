// Copyright (c) 2026 @sitxovski (Discord)
// ★★★ САМОТЕСТ ПЛАВНОСТИ АНИМАЦИЙ `asda.Anim.SelfTestSmooth` (22.10.2026). [ПОРТ]
//   Что проверяется — смешивание клипов, доворот модели (свой, сосед, моб), «хвост» сетевой поправки. Разбор, эталон и
//   ключи — `World/AsdaBlendAnimInstance.h`; отчёт — `Reports/anim_smooth_2026-10-22/REPORT.md`.
//
// ★ КАК. Один и тот же сценарий прогоняется ДВАЖДЫ в одной игре (контрольная пара):
//     режим «прежний»  — все ключи asda.Anim.* = 0 (прежний порт: клип в один кадр, поворот мгновенно, снап);
//     режим «плавный»  — значения по умолчанию (0.15 / 0.06 / 600 / 360 / 0.12).
//   Сценарий (входы — НАСТОЯЩИЕ функции персонажа/мира, без клавиатуры и без пакетов в сеть):
//     E1 старт бега (покой → бег), E2 смена направления на бегу на 90°, E3 стоп, E4 разворот на месте на 180°,
//     E5 клик-ход в точку под 135° (поворот + старт), E6 стоп клика, E7 поправка позиции 4009 на 60 ед. (`NetSnap`),
//     E8 сосед: ход назад (180°, `NetMove` — его зовёт ветка соседа `OnNetEntityMove` на 4007), E9 сосед: 4009-поправка (`NetSnap`), E10 моб: ход назад (180°).
//   Каждый кадр пишется: видимый/логический курс, max скорость кости позы (см/с, компонентное пространство — без
//   движения и поворота актора), вес бленда, время клипа, «хвост».
//   Оракулы: «прежний» — скачок курса ≥ 170° за кадр, бленда нет, хвоста нет; «плавный» — шаг курса за кадр ≤ скорость × dt
//   (+0.5°), 180° доходит за 0.25…0.6 с, логический курс — сразу, бленд идёт после смены клипа, пик скорости позы на смене
//   клипа ≤ 0.75 «прежнего», время клипа через 0.3 с после старта то же (±0.05 с — кадры удара не сдвигаются), хвост
//   поправки ≥ 30 ед. в первом кадре и < 1 ед. к 0.8 с, а актор стоит в точке сервера сразу.
//   Журнал кадров — строки «САМОТЕСТ-ПЛАВНОСТЬ кадр …» (ключ `frames`), итог — «САМОТЕСТ-ПЛАВНОСТЬ ИТОГ: ok=… fail=…».
// Консоль: `asda.Anim.SelfTestSmooth [frames] [delay=N] [quit]`. Только офлайн.
#include "CoreMinimal.h"

#if !UE_BUILD_SHIPPING

#include "Test/AsdaSelfTestNet.h"   // ★ R1: тонкий доступ самотестов к сети (вместо AsdaNetSubsystem.h)
#include "AsdaOutgoingGate.h"                  // FAsdaSelfTestRunGate — отложенный старт
#include "AsdaTestPackets.h"                   // тело 4013 (спаун моба)
#include "Test/AsdaSelfTestKit.h"              // каркас: аргументы, отказ, ключи, итог, выход
#include "World/AsdaBlendAnimInstance.h"       // инстанс со смешиванием, ключи плавности
#include "World/AsdaPlayerCharacter.h"
#include "World/AsdaWorldSubsystem.h"          // соседи, мобы, OnNetEntityMove
#include "World/AsdaCoord.h"
#include "Npc/AsdaMobActor.h"
#include "Skill/AsdaSkillFxSubsystem.h"         // клип навыка (PlaySkillVisual)
#include "EngineUtils.h"                        // TActorIterator — мобы уровня и шаблоны
#include "Components/SkeletalMeshComponent.h"
#include "Containers/Ticker.h"
#include "Engine/Engine.h"
#include "Engine/GameInstance.h"
#include "Engine/World.h"
#include "GameFramework/PlayerController.h"
#include "HAL/IConsoleManager.h"
#include "TimerManager.h"

DEFINE_LOG_CATEGORY_STATIC(LogAsdaAnimSmoothTest, Log, All);

namespace AsdaAnimSmoothTestLocal
{
	static const TCHAR* kCmd = TEXT("asda.Anim.SelfTestSmooth");
	static const TCHAR* kPfx = TEXT("САМОТЕСТ-ПЛАВНОСТЬ");
	constexpr int32 kPeerNum = 7795;   // номер соседа (DebugSpawnFakePeer)
	constexpr int32 kMobRid = 7796;    // rid моба (4013)
	constexpr int32 kMobType = 1;      // волк alpen (как у asda.Mob.LivePosSelfTest)
	constexpr double kModeLen = 14.5;  // длина одного режима, с

	// Один кадр замера.
	struct FFrame
	{
		double T = 0.0;          // с от начала режима
		float Dt = 0.f;          // длительность кадра, с
		float SelfVis = 0.f, SelfLog = 0.f;   // курс своего: видимый / логический (UE-градусы)
		float PeerVis = 0.f, PeerLog = 0.f;   // то же у соседа
		float MobVis = 0.f, MobLog = 0.f;     // то же у моба
		float PoseSpeed = 0.f;   // max скорость кости своего (компонентное пространство, ед./с)
		float PoseJerk = 0.f;    // max вторая разность позиции кости за кадр (ед.) — рывок позы; смена клипа без бленда даёт пик
		bool bBlend = false;     // бленд идёт
		float Alpha = 1.f;       // вес нового клипа
		float ClipPos = 0.f;     // время клипа своего, с
		FString Clip;            // клип своего
		float SelfTail = 0.f, PeerTail = 0.f;  // длина «хвоста»
		FVector2D SelfPos = FVector2D::ZeroVector;   // логическая позиция своего (игровая)
		bool bMobBlend = false;  // у моба идёт бленд
	};
	// Событие сценария.
	struct FEv { const TCHAR* Name; double T; };
	static const FEv kEv[] = {
		{ TEXT("E1 старт бега"), 0.8 }, { TEXT("E2 смена направления 90° на бегу"), 1.6 }, { TEXT("E3 стоп"), 2.4 },
		{ TEXT("E4 разворот 180° на месте"), 3.2 }, { TEXT("E5 клик-ход под 135°"), 4.0 }, { TEXT("E6 стоп клика"), 4.8 },
		{ TEXT("E7 поправка 4009 (60 ед.)"), 5.4 }, { TEXT("E8 сосед: ход назад 180°"), 6.2 }, { TEXT("E9 сосед: поправка 4009"), 7.0 },
		{ TEXT("E10 моб: ход назад 180°"), 7.8 },
		// ★ ревизия 22.10 (п.5): цепочка, моб вне экрана, замах, навык.
		{ TEXT("E11 цепочка: старт бега"), 8.9 }, { TEXT("E12 цепочка: стоп через 0.05 с (посреди бленда)"), 8.95 },
		// Скрытый моб ещё 1 с считается «недавно показанным» (`USkinnedMeshComponent`: LastRenderTime > Now − 1.0) и тикает позу —
		//   ход через 1.2 с после скрытия.
		{ TEXT("E13 моб скрыт"), 9.0 }, { TEXT("E14 моб вне экрана: ход"), 10.2 }, { TEXT("E15 моб вне экрана: стоп"), 10.4 },
		{ TEXT("E16 моб снова на экране"), 10.8 },
		{ TEXT("E17 замах"), 11.0 }, { TEXT("E18 конец замаха"), 12.0 }, { TEXT("E19 клип навыка 1228"), 12.4 },
		{ TEXT("E20 навык снят (клип ~3 с не должен доиграть в следующий режим)"), 14.0 },
	};
	constexpr int32 kNumEv = UE_ARRAY_COUNT(kEv);

	struct FState
	{
		TWeakObjectPtr<UWorld> W;
		TWeakObjectPtr<AAsdaPlayerCharacter> C, Peer;
		TWeakObjectPtr<AAsdaMobActor> Mob;
		int32 Ok = 0, Fail = 0;
		bool bQuit = false, bFrames = false;
		int32 Mode = 0;                 // 0 «прежний», 1 «плавный»
		double ModeStart = 0.0;
		int32 NextEv = 0;
		TArray<FFrame> Fr[2];
		TArray<FTransform> PrevCS;      // компонентные трансформы прошлого кадра
		FVector2D Home = FVector2D::ZeroVector, PeerHome = FVector2D::ZeroVector, MobHome = FVector2D::ZeroVector;
		FVector2D SnapPoint[2] = { FVector2D::ZeroVector, FVector2D::ZeroVector };   // точка поправки E7 (по режимам)
		float PrevDt = 0.f;             // ★ R5-fix6: длина прошлого кадра замера, с (пересчёт прошлого сдвига на текущий кадр)
		TArray<FVector> PrevD;          // смещения костей прошлого кадра (для второй разности — «рывка» позы)
		EAsdaBlendSource EvSrc[2][32] = {};  // источник бленда сразу после события (свой; у событий моба — моба)
		float EvSec[2][32] = {};             // длительность бленда сразу после события
		bool bEvHasInst[2][32] = {};         // у актора события был инстанс со смешиванием
		bool bSkillOk[2] = { false, false };   // клип навыка запущен
		FTSTicker::FDelegateHandle Tick;
		TUniquePtr<FAsdaCVarScope> Cv;
		double LastWall = 0.0;
		bool bMobChecked = false;       // проверка инстанса моба сделана

		void Expect(bool b, const FString& What)
		{
			if (b) { ++Ok; } else { ++Fail; }
			UE_LOG(LogAsdaAnimSmoothTest, Log, TEXT("%s %s: %s"), kPfx, b ? TEXT("OK") : TEXT("ПРОВАЛ"), *What);
		}
	};

	static const FVector2D kD0(0.6, -0.8);           // курс старта (игровые x, z)
	static FVector2D Rot(const FVector2D& V, float Deg) { return V.GetRotated(Deg); }

	static void SetMode(FState& S, int32 Mode)
	{
		const bool bNew = (Mode == 1);
		S.Cv->Set(TEXT("asda.Anim.BlendSec"), bNew ? 0.15f : 0.f);
		S.Cv->Set(TEXT("asda.Anim.BlendAttackSec"), bNew ? 0.06f : 0.f);
		S.Cv->Set(TEXT("asda.Anim.BlendLive"), 1);
		S.Cv->Set(TEXT("asda.Anim.BlendCurve"), 1);
		S.Cv->Set(TEXT("asda.Anim.TurnDegPerSec"), bNew ? 600.f : 0.f);
		S.Cv->Set(TEXT("asda.Anim.MobTurnDegPerSec"), bNew ? 360.f : 0.f);
		S.Cv->Set(TEXT("asda.Anim.TurnEaseSec"), 0.05f);
		S.Cv->Set(TEXT("asda.Anim.SnapSmoothSec"), bNew ? 0.12f : 0.f);
		S.Cv->Set(TEXT("asda.Anim.SnapSmoothMaxUnits"), 400.f);
	}

	// Привести всех в исходное: свой/сосед/моб на своих точках, курс kD0 сразу, покой.
	static void Prepare(FState& S, UAsdaWorldSubsystem* WS)
	{
		if (AAsdaPlayerCharacter* C = S.C.Get())
		{
			C->StopEmotionClip(); C->StopMove();
			C->NetSnap(S.Home); C->FaceGameDir(kD0); C->SnapFacingToLogical();
			C->SetAnimState(EAsdaPlayerState::Idle);
		}
		if (AAsdaPlayerCharacter* P = S.Peer.Get()) { P->NetSnap(S.PeerHome); P->FaceGameDir(kD0); P->SnapFacingToLogical(); }
		// Моб: ход прошлого режима мог ещё идти (его тик крутит курс по ходу) — «стой» на месте, затем курс kD0 сразу.
		if (AAsdaMobActor* M = S.Mob.Get()) { M->NetStop(AsdaCoord::UeToGame2D(M->GetActorLocation())); M->SnapFaceGameDir(kD0); }
	}

	static void FireEvent(FState& S, int32 I, UAsdaWorldSubsystem* WS)
	{
		AAsdaPlayerCharacter* C = S.C.Get();
		AAsdaPlayerCharacter* P = S.Peer.Get();
		AAsdaMobActor* M = S.Mob.Get();
		const FVector2D D90 = Rot(kD0, 90.f);
		switch (I)
		{
		case 0: if (C) { C->SetMoveDirection(kD0, 0.35f, kD0, false); } break;
		case 1: if (C) { C->SetMoveDirection(D90, 0.35f, D90, false); } break;
		case 2: if (C) { C->StopMove(); } break;
		case 3: if (C) { C->FaceGameDir(-D90); } break;
		case 4: if (C) { const FVector2D Cur = C->GetGamePosXZ(); C->NetMove(Cur, Cur + Rot(-D90, 135.f) * 400.f, 0.35f); } break;
		case 5: if (C) { C->StopMove(); } break;
		case 6: if (C) { S.SnapPoint[S.Mode] = C->GetGamePosXZ() + FVector2D(60.f, 0.f); C->NetSnap(S.SnapPoint[S.Mode]); } break;
		// Сосед: ровно то, что зовёт ветка соседа `UAsdaWorldSubsystem::OnNetEntityMove` (4007 result 1 → `NetMove`, 4009 → `NetSnap`).
		case 7: if (P) { const FVector2D Cur = P->GetGamePosXZ(); P->NetMove(Cur, Cur - kD0 * 300.f, 0.35f); } break;
		case 8: if (P) { P->NetSnap(P->GetGamePosXZ() + FVector2D(0.f, 50.f)); } break;
		case 9: if (M) { const FVector2D Cur = AsdaCoord::UeToGame2D(M->GetActorLocation()); M->NetMove(Cur, Cur - kD0 * 300.f, 0.4f); } break;   // 0.75 с — закончит до конца режима
		case 10: if (C) { C->SetMoveDirection(kD0, 0.35f, kD0, false); } break;
		case 11: if (C) { C->StopMove(); } break;
		case 12: if (M) { M->SetActorHiddenInGame(true); } break;
		case 13: if (M) { const FVector2D Cur = AsdaCoord::UeToGame2D(M->GetActorLocation()); M->NetMove(Cur, Cur + kD0 * 300.f, 0.4f); } break;
		case 14: if (M) { M->NetStop(AsdaCoord::UeToGame2D(M->GetActorLocation())); } break;
		case 15: if (M) { M->SetActorHiddenInGame(false); } break;
		case 16: if (C) { C->PlayAttackClip(C->EquippedWeaponSubType); } break;
		case 17: if (C) { C->StopAttackAnim(); } break;
		case 18:
			if (C && WS && WS->GetGameInstance())
			{
				UAsdaSkillFxSubsystem* Sfx = WS->GetGameInstance()->GetSubsystem<UAsdaSkillFxSubsystem>();
				S.bSkillOk[S.Mode] = Sfx && Sfx->PlaySkillVisual(C, 1228);   // навык с клипом у обоих полов (как у asda.Fish.AnimSelfTest)
			}
			break;
		case 19:
			if (C && WS && WS->GetGameInstance())
			{
				if (UAsdaSkillFxSubsystem* Sfx = WS->GetGameInstance()->GetSubsystem<UAsdaSkillFxSubsystem>()) { Sfx->StopSkillVisual(C); }
			}
			break;
		default: break;
		}
		// Источник и длительность бленда сразу после события (мобу — его инстанс).
		const bool bMobEv = (I == 9 || (I >= 12 && I <= 15));
		USkeletalMeshComponent* Sk = bMobEv ? (M ? M->Mesh.Get() : nullptr) : (C ? C->BaseSkel.Get() : nullptr);
		const UAsdaBlendAnimInstance* BI = Sk ? Cast<UAsdaBlendAnimInstance>(Sk->GetAnimInstance()) : nullptr;
		if (I < 32)
		{
			S.bEvHasInst[S.Mode][I] = BI != nullptr;
			S.EvSrc[S.Mode][I] = BI ? BI->GetLastBlendSource() : EAsdaBlendSource::None;
			S.EvSec[S.Mode][I] = BI ? BI->GetLastBlendSec() : 0.f;
		}
		UE_LOG(LogAsdaAnimSmoothTest, Log, TEXT("%s режим %s: %s"), kPfx, S.Mode ? TEXT("плавный") : TEXT("прежний"), kEv[I].Name);
	}

	// Скорость и «рывок» позы: max по костям |Δp| / dt и |Δp(t) − Δp(t−1)| (компонентное пространство × масштаб компонента).
	static void PoseMetrics(FState& S, USkeletalMeshComponent* Sk, float Dt, float& OutSpeed, float& OutJerk)
	{
		OutSpeed = 0.f; OutJerk = 0.f;
		if (!Sk || Dt <= 0.f) { return; }
		const TArray<FTransform>& CS = Sk->GetComponentSpaceTransforms();
		const float Scale = (float)Sk->GetComponentScale().GetAbs().X;
		TArray<FVector> D; D.SetNumZeroed(CS.Num());
		if (S.PrevCS.Num() == CS.Num())
		{
			for (int32 i = 0; i < CS.Num(); ++i)
			{
				D[i] = (CS[i].GetLocation() - S.PrevCS[i].GetLocation()) * Scale;
				OutSpeed = FMath::Max(OutSpeed, (float)D[i].Size() / Dt);
				// ★ R5-fix6 (13.11.2026) [ИСПРАВЛЕНИЕ ТЕСТА]: прошлый сдвиг кости пересчитывается на длину текущего кадра (ревизия: так скачок позы в длинном кадре не занижается). Прежде «рывок»
				//   был разностью сдвигов за два соседних кадра как есть: при неровном кадре (нагрузка) плавное движение давало ложный
				//   пик «скорость × разница длин кадров» (журнал rx_r5fix3: «плавный» 165/108/102 против 16/53/12 на ровном кадре), и
				//   проверка «рывок снижен» краснела. При ровном кадре 1/60 с значение то же, что прежде.
				if (S.PrevD.Num() == D.Num() && S.PrevDt > 0.f) { OutJerk = FMath::Max(OutJerk, (float)(D[i] - S.PrevD[i] * (Dt / S.PrevDt)).Size()); }
			}
		}
		S.PrevCS = CS;
		S.PrevD = MoveTemp(D);
		S.PrevDt = Dt;
	}

	static void Sample(FState& S, double T, float Dt)
	{
		FFrame F; F.T = T; F.Dt = Dt;
		if (AAsdaPlayerCharacter* C = S.C.Get())
		{
			F.SelfVis = C->GetVisualFacingYawUE(); F.SelfLog = C->GetFacingYawUE();
			PoseMetrics(S, C->BaseSkel, Dt, F.PoseSpeed, F.PoseJerk);
			if (UAsdaBlendAnimInstance* I = C->BaseSkel ? Cast<UAsdaBlendAnimInstance>(C->BaseSkel->GetAnimInstance()) : nullptr)
			{
				F.bBlend = I->IsBlendingGT(); F.Alpha = I->GetBlendAlphaGT();
			}
			F.ClipPos = C->BaseSkel ? C->BaseSkel->GetPosition() : 0.f;
			F.Clip = C->GetPlayingClipName();
			F.SelfTail = (float)C->GetVisualTail().Size();
			F.SelfPos = C->GetGamePosXZ();
		}
		if (AAsdaPlayerCharacter* P = S.Peer.Get()) { F.PeerVis = P->GetVisualFacingYawUE(); F.PeerLog = P->GetFacingYawUE(); F.PeerTail = (float)P->GetVisualTail().Size(); }
		if (AAsdaMobActor* M = S.Mob.Get())
		{
			F.MobVis = M->Mesh ? (float)M->Mesh->GetComponentRotation().Yaw : 0.f; F.MobLog = M->GetFacingYawLogical();
			if (UAsdaBlendAnimInstance* MI = M->Mesh ? Cast<UAsdaBlendAnimInstance>(M->Mesh->GetAnimInstance()) : nullptr) { F.bMobBlend = MI->IsBlendingGT(); }
		}
		if (S.bFrames)
		{
			UE_LOG(LogAsdaAnimSmoothTest, Log, TEXT("%s кадр %s t=%.3f dt=%.4f свой вид/лог %.1f/%.1f поза %.0f рывок %.2f бленд %d α=%.2f клип %s@%.3f хвост %.1f | сосед %.1f/%.1f хвост %.1f | моб %.1f/%.1f"),
				kPfx, S.Mode ? TEXT("плавный") : TEXT("прежний"), F.T, F.Dt, F.SelfVis, F.SelfLog, F.PoseSpeed, F.PoseJerk, F.bBlend ? 1 : 0, F.Alpha,
				*F.Clip, F.ClipPos, F.SelfTail, F.PeerVis, F.PeerLog, F.PeerTail, F.MobVis, F.MobLog);
		}
		S.Fr[S.Mode].Add(MoveTemp(F));
	}

	// Окно кадров [T0, T1).
	template <typename Fn> static void ForWin(const TArray<FFrame>& Fr, double T0, double T1, Fn&& F)
	{
		for (int32 i = 0; i < Fr.Num(); ++i) { if (Fr[i].T >= T0 && Fr[i].T < T1) { F(i, Fr[i]); } }
	}
	using FYawGet = float (*)(const FFrame&);
	static float SelfV(const FFrame& F) { return F.SelfVis; }  static float SelfL(const FFrame& F) { return F.SelfLog; }
	static float PeerV(const FFrame& F) { return F.PeerVis; }  static float PeerL(const FFrame& F) { return F.PeerLog; }
	static float MobV(const FFrame& F)  { return F.MobVis; }   static float MobL(const FFrame& F)  { return F.MobLog; }

	// Поворот в окне события: max шаг за кадр, max превышение «скорость×dt», время до цели (±1°), логический курс сразу.
	struct FYawRes { float MaxStep = 0.f; float MaxOver = -999.f; float Reach = -1.f; float Turn = 0.f; bool bLogicalNow = false; };
	static FYawRes YawWin(const TArray<FFrame>& Fr, double TE, FYawGet V, FYawGet L, float Rate)
	{
		FYawRes R;
		int32 First = -1;
		ForWin(Fr, TE, TE + 0.9, [&](int32 i, const FFrame& F)
		{
			if (First < 0)
			{
				First = i;
				if (i > 0) { R.Turn = FMath::Abs(FMath::FindDeltaAngleDegrees(V(Fr[i - 1]), L(F))); }
				R.bLogicalNow = true;   // логический курс уже в цели в первом кадре после события (сверка ниже)
			}
			if (i > 0)
			{
				const float Step = FMath::Abs(FMath::FindDeltaAngleDegrees(V(Fr[i - 1]), V(F)));
				R.MaxStep = FMath::Max(R.MaxStep, Step);
				R.MaxOver = FMath::Max(R.MaxOver, Step - (Rate * F.Dt + 0.5f));
			}
			if (R.Reach < 0.f && FMath::Abs(FMath::FindDeltaAngleDegrees(V(F), L(F))) <= 1.f) { R.Reach = (float)(F.T - TE); }
		});
		if (First >= 0 && Fr.IsValidIndex(First + 3))   // логический курс не «плывёт»: первые кадры = последний кадр окна
		{
			R.bLogicalNow = FMath::Abs(FMath::FindDeltaAngleDegrees(L(Fr[First]), L(Fr[First + 3]))) <= 0.5f;
		}
		return R;
	}
	// Пик «рывка» позы в окне смены клипа и число кадров бленда / его длительность.
	static void PoseWin(const TArray<FFrame>& Fr, double TE, float& OutPeak, int32& OutBlendFrames, float& OutBlendSec, float& OutMidAlpha, float& OutMaxDt)
	{
		OutPeak = 0.f; OutBlendFrames = 0; OutBlendSec = 0.f; OutMidAlpha = -1.f; OutMaxDt = 0.f;
		double First = -1.0, Last = -1.0;   // время первого и последнего кадра с блендом
		ForWin(Fr, TE, TE + 0.5, [&](int32, const FFrame& F)
		{
			if (F.T < TE + 0.35) { OutPeak = FMath::Max(OutPeak, F.PoseJerk); }
			if (F.bBlend) { ++OutBlendFrames; OutBlendSec += F.Dt; OutMaxDt = FMath::Max(OutMaxDt, F.Dt); if (First < 0.0) { First = F.T; } Last = F.T; }
		});
		// ★ ревизия 22.10 (п.5): вес — в СЕРЕДИНЕ бленда.
		// ★ R5-fix6 (13.11.2026) [ИСПРАВЛЕНИЕ ТЕСТА]: середина — между первым и последним кадром с блендом. Прежде бралось «время
		//   события + половина длительности», а бленд начинается кадром позже события: при длинном кадре ближайшим к такой середине
		//   оказывался первый кадр бленда (вес 0,17). При ровном кадре 1/60 с точка та же с точностью до кадра.
		double Best = 1e9; const double Mid = (First >= 0.0) ? 0.5 * (First + Last) : TE;
		ForWin(Fr, TE, TE + 0.5, [&](int32, const FFrame& F)
		{
			if (F.bBlend && FMath::Abs(F.T - Mid) < Best) { Best = FMath::Abs(F.T - Mid); OutMidAlpha = F.Alpha; }
		});
	}
	static const TCHAR* SrcName(EAsdaBlendSource V)
	{
		switch (V)
		{
		case EAsdaBlendSource::Live: return TEXT("живой клип");
		case EAsdaBlendSource::Snapshot: return TEXT("снимок");
		case EAsdaBlendSource::ChainSnapshot: return TEXT("снимок (цепочка)");
		case EAsdaBlendSource::SameFrame: return TEXT("тот же кадр");
		case EAsdaBlendSource::Unseen: return TEXT("не показывался");
		default: return TEXT("нет");
		}
	}
	static float ClipPosAt(const TArray<FFrame>& Fr, double T)
	{
		for (const FFrame& F : Fr) { if (F.T >= T) { return F.ClipPos; } }
		return -1.f;
	}
	static void TailWin(const TArray<FFrame>& Fr, double TE, bool bPeer, float& OutFirst, float& OutAt08)
	{
		OutFirst = -1.f; OutAt08 = -1.f;
		ForWin(Fr, TE, TE + 0.79, [&](int32, const FFrame& F) { if (OutFirst < 0.f) { OutFirst = bPeer ? F.PeerTail : F.SelfTail; } OutAt08 = bPeer ? F.PeerTail : F.SelfTail; });
	}

	static void Judge(FState& S)
	{
		const TArray<FFrame>& O = S.Fr[0];
		const TArray<FFrame>& N = S.Fr[1];
		const float kPlayerRate = 600.f, kMobRate = 360.f;
		// ── Курс ──
		struct FYawCase { int32 Ev; FYawGet V, L; float Rate; const TCHAR* Who; float MinTurn; };
		const FYawCase Yc[] = {
			{ 1, SelfV, SelfL, kPlayerRate, TEXT("свой, E2 90° на бегу"), 80.f },
			{ 3, SelfV, SelfL, kPlayerRate, TEXT("свой, E4 180° на месте"), 170.f },
			{ 4, SelfV, SelfL, kPlayerRate, TEXT("свой, E5 клик 135°"), 120.f },
			{ 7, PeerV, PeerL, kPlayerRate, TEXT("сосед, E8 180°"), 170.f },
			{ 9, MobV,  MobL,  kMobRate,    TEXT("моб, E10 180°"), 170.f },
		};
		for (const FYawCase& Y : Yc)
		{
			const FYawRes Ro = YawWin(O, kEv[Y.Ev].T, Y.V, Y.L, Y.Rate);
			const FYawRes Rn = YawWin(N, kEv[Y.Ev].T, Y.V, Y.L, Y.Rate);
			S.Expect(Ro.Turn >= Y.MinTurn && Ro.MaxStep >= Y.MinTurn - 1.f, FString::Printf(
				TEXT("прежний [%s]: поворот %.1f° в ОДИН кадр (max шаг %.1f°) — прежнее поведение сохранено ключом 0"), Y.Who, Ro.Turn, Ro.MaxStep));
			const float Expect = Rn.Turn / Y.Rate;
			S.Expect(Rn.Turn >= Y.MinTurn && Rn.MaxOver <= 0.f && Rn.bLogicalNow && Rn.Reach >= Expect * 0.85f && Rn.Reach <= Expect + 0.25f,
				FString::Printf(TEXT("плавный [%s]: поворот %.1f° за %.3f с (ожидание %.3f…%.3f), max шаг %.1f°/кадр, превышение скорости %.2f° (≤ 0), логический курс сразу %d"),
					Y.Who, Rn.Turn, Rn.Reach, Expect * 0.85f, Expect + 0.25f, Rn.MaxStep, Rn.MaxOver, Rn.bLogicalNow ? 1 : 0));
		}
		// ── Смешивание клипов: E1 старт, E3 стоп, E6 стоп клика ──
		const int32 ClipEv[] = { 0, 2, 5 };
		int32 Better = 0;
		for (const int32 E : ClipEv)
		{
			float Po, Pn, BsO, BsN, MaO, MaN, MdO, MdN; int32 BfO, BfN;
			PoseWin(O, kEv[E].T, Po, BfO, BsO, MaO, MdO);
			PoseWin(N, kEv[E].T, Pn, BfN, BsN, MaN, MdN);
			S.Expect(BfO == 0, FString::Printf(TEXT("прежний [%s]: бленда нет (кадров %d), пик рывка позы %.2f ед./кадр"), kEv[E].Name, BfO, Po));
			// ★ R5-fix6 (13.11.2026) [ИСПРАВЛЕНИЕ ТЕСТА]: длительность бленда меряется суммой длин кадров, в которых он шёл, — она
			//   длиннее ключа на неполные первый и последний кадры. Верхняя граница 0,26 с писалась под кадр 1/60 с; при длинном кадре
			//   она — ключ 0,15 с + два наибольших кадра бленда (журнал rx_r5fix3: 4 кадра / 0,262 с). При кадре ≤ 55 мс граница прежняя.
			const float BsMax = FMath::Max(0.26f, 0.15f + 2.f * MdN);
			S.Expect(BfN >= 2 && BsN >= 0.08f && BsN <= BsMax && MaN >= 0.25f && MaN <= 0.75f, FString::Printf(
				TEXT("плавный [%s]: бленд %d кадров / %.3f с (ключ 0.15, граница %.3f), вес в середине бленда %.2f (0.25…0.75), пик рывка позы %.2f (прежний %.2f)"),
				kEv[E].Name, BfN, BsN, BsMax, MaN, Pn, Po));
			if (Pn <= Po * 0.75f) { ++Better; }
			UE_LOG(LogAsdaAnimSmoothTest, Log, TEXT("%s замер [%s]: пик рывка позы прежний %.2f → плавный %.2f (%.0f %%)"),
				kPfx, kEv[E].Name, Po, Pn, Po > 0.f ? 100.f * Pn / Po : 0.f);
		}
		S.Expect(Better >= 2, FString::Printf(TEXT("рывок позы на смене клипа снижен ≥ 25 %% в %d из 3 переходов (нужно ≥ 2)"), Better));
		// ── Время клипа не сдвигается (кадры удара/шага/звуков привязаны к нему) ──
		{
			const float Co = ClipPosAt(O, kEv[0].T + 0.3), Cn = ClipPosAt(N, kEv[0].T + 0.3);
			S.Expect(Co >= 0.f && Cn >= 0.f && FMath::Abs(Co - Cn) <= 0.05f, FString::Printf(
				TEXT("время клипа бега через 0.3 с после старта: прежний %.3f, плавный %.3f (разница ≤ 0.05 — тайминги клипа не сдвинуты)"), Co, Cn));
		}
		// ── Хвост поправки ──
		for (int32 k = 0; k < 2; ++k)
		{
			const int32 E = k ? 8 : 6;
			float FoO, AtO, FoN, AtN;
			TailWin(O, kEv[E].T, k == 1, FoO, AtO);
			TailWin(N, kEv[E].T, k == 1, FoN, AtN);
			S.Expect(FoO == 0.f && AtO == 0.f, FString::Printf(TEXT("прежний [%s]: хвоста нет (%.1f / %.1f) — снап"), kEv[E].Name, FoO, AtO));
			S.Expect(FoN >= 30.f && AtN < 1.f, FString::Printf(TEXT("плавный [%s]: хвост в первом кадре %.1f ед. (≥ 30), к 0.8 с %.2f (< 1)"), kEv[E].Name, FoN, AtN));
		}
		// ── ★ ревизия 22.10 (п.5): источники, цепочка, моб вне экрана, замах, навык ──
		for (int32 m = 0; m < 2; ++m)
		{
			const bool bNew = (m == 1);
			const TCHAR* Md = bNew ? TEXT("плавный") : TEXT("прежний");
			auto Ev = [&](int32 I, EAsdaBlendSource WantNew, float WantSec, const TCHAR* What)
			{
				const EAsdaBlendSource Got = S.EvSrc[m][I]; const float Sec = S.EvSec[m][I];
				const bool bOk = S.bEvHasInst[m][I] && (bNew ? (Got == WantNew && FMath::IsNearlyEqual(Sec, WantSec, 0.001f)) : (Got == EAsdaBlendSource::None && Sec == 0.f));
				S.Expect(bOk, FString::Printf(TEXT("%s [%s]: %s — источник «%s», бленд %.3f с (ждали %s)"), Md, kEv[I].Name, What, SrcName(Got), Sec,
					bNew ? *FString::Printf(TEXT("«%s», %.2f с"), SrcName(WantNew), WantSec) : TEXT("без бленда")));
			};
			Ev(0, EAsdaBlendSource::Live, 0.15f, TEXT("покой → бег"));
			Ev(9, EAsdaBlendSource::Live, 0.15f, TEXT("моб: покой → ход"));
			Ev(11, EAsdaBlendSource::ChainSnapshot, 0.15f, TEXT("смена посреди бленда — снимок смешанной позы"));
			Ev(16, EAsdaBlendSource::Live, 0.06f, TEXT("вход в замах — BlendAttackSec"));
			if (S.bSkillOk[m]) { Ev(18, EAsdaBlendSource::Live, 0.06f, TEXT("клип навыка — BlendAttackSec")); }
			else { S.Expect(false, FString::Printf(TEXT("%s [%s]: клип навыка 1228 не запущен (PlaySkillVisual = false)"), Md, kEv[18].Name)); }
			// Моб вне экрана: вторая смена при скрытом мобе — без бленда («не показывался»), после показа бленда нет.
			{
				const EAsdaBlendSource Got = S.EvSrc[m][14];
				S.Expect(S.bEvHasInst[m][14] && Got == (bNew ? EAsdaBlendSource::Unseen : EAsdaBlendSource::None) && S.EvSec[m][14] == 0.f,
					FString::Printf(TEXT("%s [%s]: смена клипа скрытого моба — источник «%s», бленда нет"), Md, kEv[14].Name, SrcName(Got)));
				int32 Bl = 0, Fr = 0;
				ForWin(S.Fr[m], kEv[15].T, kEv[15].T + 0.3, [&](int32, const FFrame& F) { ++Fr; if (F.bMobBlend) { ++Bl; } });
				S.Expect(Fr > 0 && Bl == 0, FString::Printf(TEXT("%s [%s]: после показа моба бленда нет (кадров с блендом %d из %d) — устаревшая поза не мелькает"),
					Md, kEv[15].Name, Bl, Fr));
			}
			// Бленд моба при ходе (E10): кадры с блендом есть только в плавном режиме.
			{
				int32 Bl = 0; float Sec = 0.f, MaxDt = 0.f;
				ForWin(S.Fr[m], kEv[9].T, kEv[9].T + 0.5, [&](int32, const FFrame& F) { if (F.bMobBlend) { ++Bl; Sec += F.Dt; MaxDt = FMath::Max(MaxDt, F.Dt); } });
				const float SecMax = FMath::Max(0.26f, 0.15f + 2.f * MaxDt);   // ★ R5-fix6: граница с учётом длины кадра — как у бленда своего выше
				S.Expect(bNew ? (Bl >= 2 && Sec >= 0.08f && Sec <= SecMax) : (Bl == 0), FString::Printf(TEXT("%s [%s]: бленд клипа моба %d кадров / %.3f с (граница %.3f)"),
					Md, kEv[9].Name, Bl, Sec, SecMax));
			}
		}
		// Логическая позиция своего — в точке поправки сразу (оба режима).
		for (int32 m = 0; m < 2; ++m)
		{
			FVector2D Pos = FVector2D::ZeroVector; bool bGot = false;
			ForWin(S.Fr[m], kEv[6].T, kEv[6].T + 0.1, [&](int32, const FFrame& F) { if (!bGot) { Pos = F.SelfPos; bGot = true; } });
			const float Err = (float)FVector2D::Distance(Pos, S.SnapPoint[m]);
			S.Expect(bGot && Err < 1.f, FString::Printf(TEXT("%s [E7]: логическая позиция в точке сервера сразу (%.0f, %.0f), отклонение %.2f"),
				m ? TEXT("плавный") : TEXT("прежний"), Pos.X, Pos.Y, Err));
		}
	}

	// Конец прогона (зовётся из тикера; тикер снимается возвратом false).
	static void Finish(TSharedRef<FState> S)
	{
		S->Tick.Reset();
		UWorld* W = S->W.Get();
		S->Expect(S->Peer.IsValid(), TEXT("сосед на месте до конца прогона (DebugSpawnFakePeer)"));
		S->Expect(S->Mob.IsValid(), TEXT("моб на месте до конца прогона (4013)"));
		if (W && S->Fr[0].Num() > 10 && S->Fr[1].Num() > 10) { Judge(*S); }
		else { S->Expect(false, FString::Printf(TEXT("кадров мало: прежний %d, плавный %d"), S->Fr[0].Num(), S->Fr[1].Num())); }
		if (AAsdaMobActor* M = S->Mob.Get()) { M->Destroy(); }
		// Сосед прогона убирается штатно (4011, как у asda.Combat.SelfTestObserverSync) — следующему самотесту пакета не достаётся.
		if (UAsdaNetSubsystem* Nn = AsdaSelfTestKit::NetOf(W))
		{
			TArray<uint8> B; B.SetNumZeroed(2); B[0] = (uint8)(kPeerNum & 0xFF); B[1] = (uint8)((kPeerNum >> 8) & 0xFF);
			AsdaTestNet::DebugFeedPacket(Nn, 4011, B);
		}
		if (S->Cv) { S->Cv->Restore(); }
		AsdaSelfTestKit::Finish(W, kCmd, S->bQuit, LogAsdaAnimSmoothTest, kPfx, S->Ok, S->Fail,
			*FString::Printf(TEXT(" (кадров: прежний %d, плавный %d)"), S->Fr[0].Num(), S->Fr[1].Num()));
	}
}

struct FAsdaAnimSmoothSelfTest
{
	static void Run(const TArray<FString>& Args, UWorld* W)
	{
		using namespace AsdaAnimSmoothTestLocal;
		const FAsdaSelfTestArgs A = FAsdaSelfTestArgs::Parse(Args);
		if (!W) { AsdaSelfTestKit::Refused(nullptr, kCmd, A.bQuit, TEXT("нет мира")); return; }
		if (AsdaSelfTestKit::RefuseStart(W, kCmd, A)) { return; }
		TSharedRef<FAsdaSelfTestRunGate> Gate = MakeShared<FAsdaSelfTestRunGate>(kCmd, A.bQuit);
		TWeakObjectPtr<UWorld> WW(W);
		const bool bQuit = A.bQuit, bFrames = A.Has(TEXT("frames"));
		FTimerHandle H0;
		W->GetTimerManager().SetTimer(H0, FTimerDelegate::CreateLambda([WW, Gate]()
		{
			UWorld* Wd = WW.Get();
			if (!Wd || !Gate->Pass(Wd)) { return; }
			if (AsdaSelfTestKit::NeedSelfSpawn(Wd) && GEngine)
			{
				GEngine->Exec(Wd, TEXT("Asda.Load.CurrentMap 3"));
				GEngine->Exec(Wd, TEXT("Asda.World.FakeSelfSpawn 256 256"));
			}
		}), FMath::Max(0.01f, A.Delay), false);
		FTimerHandle H1;
		W->GetTimerManager().SetTimer(H1, FTimerDelegate::CreateLambda([WW, Gate, bQuit, bFrames]()
		{
			UWorld* Wd = WW.Get();
			if (!Wd || !Gate->Pass(Wd)) { return; }
			APlayerController* PC = Wd->GetFirstPlayerController();
			AAsdaPlayerCharacter* C = PC ? Cast<AAsdaPlayerCharacter>(PC->GetPawn()) : nullptr;
			UAsdaNetSubsystem* Net = AsdaSelfTestKit::NetOf(Wd);
			UAsdaWorldSubsystem* WS = Wd->GetGameInstance() ? Wd->GetGameInstance()->GetSubsystem<UAsdaWorldSubsystem>() : nullptr;
			if (!C || !Net || !WS) { AsdaSelfTestKit::Refused(Wd, kCmd, bQuit, TEXT("нет своего персонажа / сети / мира")); return; }
			TSharedRef<FState> S = MakeShared<FState>();
			S->W = Wd; S->C = C; S->bQuit = bQuit; S->bFrames = bFrames;
			S->Cv = MakeUnique<FAsdaCVarScope>(Wd, kCmd);
			S->Cv->Set(TEXT("t.MaxFPS"), 60);   // ровный кадр: пики скорости позы сравнимы между режимами
			S->Home = C->GetGamePosXZ();
			S->PeerHome = S->Home + FVector2D(-300.f, 300.f);
			WS->DebugSpawnFakePeer(kPeerNum, TEXT("СоседПлавность"), S->PeerHome);
			S->Peer = Cast<AAsdaPlayerCharacter>(WS->GetPlayerActorByUserNum(kPeerNum));
			const FIntPoint HomeTile = AsdaCoord::GameToTile(S->Home);
			AsdaTestNet::DebugFeedPacket(Net, 4013, AsdaTestPackets::MobSpawn4013(kMobRid, kMobType, HomeTile + FIntPoint(4, -4), /*State*/1, 250));
			S->Mob = WS->FindNetMobByRid(kMobRid);
			S->Expect(C->BaseSkel && Cast<UAsdaBlendAnimInstance>(C->BaseSkel->GetAnimInstance()) != nullptr,
				TEXT("у своего персонажа инстанс со смешиванием (UAsdaBlendAnimInstance)"));
			if (AAsdaPlayerCharacter* P = S->Peer.Get())
			{
				S->Expect(P->BaseSkel && Cast<UAsdaBlendAnimInstance>(P->BaseSkel->GetAnimInstance()) != nullptr, TEXT("у соседа инстанс со смешиванием"));
			}
			// ★ ревизия 22.10 (п.5): у ВСЕХ мобов мира — запечённых в уровне, их шаблонов (скрытых) и сетевых — класс инстанса со
			//   смешиванием (свойства, сохранённые в уровне, могли бы перебить умолчание конструктора).
			{
				int32 NAll = 0, NOk = 0, NHidden = 0, NInst = 0;
				FString Bad;
				for (TActorIterator<AAsdaMobActor> It(Wd); It; ++It)
				{
					AAsdaMobActor* Mb = *It;
					++NAll; if (Mb->IsHidden()) { ++NHidden; }
					const bool bInst = Mb->Mesh && Cast<UAsdaBlendAnimInstance>(Mb->Mesh->GetAnimInstance()) != nullptr;
					if (bInst) { ++NInst; }
					if (AsdaAnimSmooth::IsInstalled(Mb->Mesh) || bInst) { ++NOk; }
					else if (Bad.Len() < 300) { Bad += Mb->GetName() + TEXT(" "); }
				}
				S->Expect(NAll > 0 && NOk == NAll, FString::Printf(TEXT("мобы мира (уровень + шаблоны + сетевые): со смешиванием %d из %d (скрытых %d, инстанс создан у %d)%s%s"),
					NOk, NAll, NHidden, NInst, Bad.IsEmpty() ? TEXT("") : TEXT(" — без: "), *Bad));
			}
			S->Mode = 0; SetMode(*S, 0); Prepare(*S, WS);
			S->ModeStart = FPlatformTime::Seconds(); S->LastWall = S->ModeStart; S->NextEv = 0;
			S->Tick = FTSTicker::GetCoreTicker().AddTicker(FTickerDelegate::CreateLambda([S](float InDt) -> bool
			{
				UWorld* Wt = S->W.Get();
				UAsdaWorldSubsystem* WSt = (Wt && Wt->GetGameInstance()) ? Wt->GetGameInstance()->GetSubsystem<UAsdaWorldSubsystem>() : nullptr;
				if (!Wt || !S->C.IsValid() || !WSt) { Finish(S); return false; }
				const double Now = FPlatformTime::Seconds();
				const float Dt = FMath::Max(1e-4f, InDt); S->LastWall = Now;   // dt кадра движка — тот же, что получил тик актора
				if (!S->bMobChecked)
				{
					if (!S->Mob.IsValid()) { S->Mob = WSt->FindNetMobByRid(kMobRid); }
					if (AAsdaMobActor* M = S->Mob.Get())
					{
						S->bMobChecked = true;
						S->Expect(M->Mesh && Cast<UAsdaBlendAnimInstance>(M->Mesh->GetAnimInstance()) != nullptr, TEXT("у моба инстанс со смешиванием"));
						M->SnapFaceGameDir(kD0);
					}
				}
				const double T = Now - S->ModeStart;
				while (S->NextEv < kNumEv && T >= kEv[S->NextEv].T) { FireEvent(*S, S->NextEv, WSt); ++S->NextEv; }
				Sample(*S, T, Dt);
				if (T >= kModeLen)
				{
					if (S->Mode == 0)
					{
						S->Mode = 1; SetMode(*S, 1); Prepare(*S, WSt);
						S->ModeStart = FPlatformTime::Seconds(); S->LastWall = S->ModeStart; S->NextEv = 0; S->PrevCS.Reset();
					}
					else { Finish(S); return false; }
				}
				return true;
			}));
		}), A.Delay + 2.f, false);
	}
};

static FAutoConsoleCommandWithWorldAndArgs GAsdaAnimSmoothSelfTest(
	TEXT("asda.Anim.SelfTestSmooth"),
	TEXT("Самотест плавности анимаций (22.10.2026): смешивание клипов стой/бег/стоп, доворот модели (свой 600°/с, сосед, моб 360°/с), ")
	TEXT("«хвост» поправки 4009; контрольная пара — все ключи asda.Anim.* = 0. Только офлайн. `frames` — журнал кадров, `delay=N` `quit`."),
	FConsoleCommandWithWorldAndArgsDelegate::CreateLambda([](const TArray<FString>& A, UWorld* W) { FAsdaAnimSmoothSelfTest::Run(A, W); }));

#endif // !UE_BUILD_SHIPPING
