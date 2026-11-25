// Copyright (c) 2026 @sitxovski (Discord)
// ★★★ САМОТЕСТ ХОДЬБЫ ВПРИСЯДКУ И ВИДА НЕВИДИМОСТИ (24.10.2026). [ПОРТ]/[ОРИГИНАЛ]
//
// ЧТО ПРОВЕРЯЕТ (настоящие входы, не копии):
//   A. Ctrl: одиночное нажатие (опрос `TickCrouchInput` и настоящий ввод контроллера) — присед вкл/выкл; Ctrl + новая клавиша
//      (сочетание) — не переключает; статус хода 4006 = 4 в приседе, 2 — без.
//   B. Ход вприсядку: клип anim_239, темп по формуле донора и за скоростью (0.35 → 0.175 — вдвое медленнее).
//   C. 4006: настоящий `SendMove4006` (перехват, в сеть ничего) — статус @+3 = 4 / 2.
//   D. Свой грант 4007 с подтипом 4 (делегат сети, тот же путь, что пакет) исполняется.
//   E. Сосед: 4007 с подтипом 4 → присед (anim_239), подтип 2 → обычный бег.
//   F. Отказы и снятие: верхом, сидя, мёртвым; посадка снимает присед; ключи Enable/NetStatus/HideAnim.
//   G. Невидимость 112/186 (подмена подсистемы): сосед — затухание, через 2 с не рисуется (актор скрыт), отряд 0.2, Друг 0.4,
//      детектор 154 в радиусе 0.5, вне радиуса — снова скрыт; ход невидимого — присед (ключ HideAnim 0 — walk01);
//      цель-невидимка снимается; снятие — снова виден, α 1; свой — 0.3 и проявление до 1; ключ asda.Stealth.Visual 0.
//   H. ★ Ход вприсядку назад (24.10.2026): время клипа (позиция по кадрам и знак темпа) — вперёд / пятясь назад (без перезапуска
//      клипа) / вбок / по диагонали назад и вперёд / пятка хода «в точку»; ключ asda.Crouch.BackwardReverse 0 — прежнее (вперёд);
//      сосед (и невидимый сосед) по 4007 назад — курс = направление шага, клип вперёд (как оригинал: пятки соседу не приходит);
//      свой под невидимостью (без Ctrl) пятясь — назад.
// Консоль: `asda.SelfTest.Crouch [delay=N] [quit]`. Только офлайн. Строки — «САМОТЕСТ-ПРИСЕД», итог — «… ИТОГ: ok=… fail=…».
#include "CoreMinimal.h"
#include "Quest/AsdaQuestLogSubsystem.h"   // ★ R4-хвосты-c (15.11.2026): закрыть журнал заданий, открытый шагом Ctrl+Q
#include "Test/AsdaSelfTestCtx.h"   // ★ R5-c (12.11.2026): общая точка самотестов вместо своих копий помощников
#include "Test/AsdaSelfTestInput.h"   // ★ R5-c (12.11.2026): общая точка самотестов вместо своих копий помощников

#if !UE_BUILD_SHIPPING

#include "AsdaBytes.h"
#include "AsdaNetSubsystem.h"
#include "Test/AsdaSelfTestRunGate.h"   // FAsdaSelfTestRunGate   ★ R3: FAsdaSelfTestRunGate — узкий заголовок вместо AsdaOutgoingGate.h
#include "AsdaCVarSave.h"
#include "Test/AsdaSelfTestKit.h"
#include "World/AsdaPlayerCharacter.h"
#include "World/AsdaPlayerController.h"
#include "World/AsdaWorldSubsystem.h"
#include "World/AsdaStealthSubsystem.h"
#include "AsdaEffectActor.h"     // эффекты на невидимом (К1 ревизии)
#include "Engine/Engine.h"
#include "Engine/World.h"
#include "HAL/IConsoleManager.h"
#include "TimerManager.h"
#include "InputCoreTypes.h"
#include "GameFramework/PlayerController.h"
#include "Components/SkeletalMeshComponent.h"
#include "Animation/AnimSingleNodeInstance.h"   // клип и длина тела (раздел H)
#include "Animation/AnimSequenceBase.h"

DEFINE_LOG_CATEGORY_STATIC(LogAsdaCrouchTest, Log, All);

namespace AsdaCrouchTest
{
	static const TCHAR* const kCmd = TEXT("asda.SelfTest.Crouch");
	static const TCHAR* const kPrefix = TEXT("САМОТЕСТ-ПРИСЕД");
	static constexpr int32 kPeerUserNum = 7781;   // сосед-заглушка

	// Состояние прогона (живёт между таймерами).
	struct FState
	{
		int32 Ok = 0, Fail = 0;                       // счётчики проверок
		uint32 CapGen = 0;                            // поколение перехвата отправок
		TArray<int32> Status4006;                     // статус хода @+3 перехваченных 4006
		bool bQuestWasOpen = false;                   // ★ R4-хвосты-c (15.11.2026): журнал заданий был открыт до прогона (тогда в конце не закрываем)
		FVector2D Land = FVector2D::ZeroVector;       // точка старта
		TWeakObjectPtr<AAsdaPlayerCharacter> Me;      // свой персонаж
		TWeakObjectPtr<AAsdaPlayerCharacter> Peer;    // сосед-заглушка
		TWeakObjectPtr<UAsdaNetSubsystem> Net;        // сеть
		TWeakObjectPtr<UAsdaStealthSubsystem> Stealth; // подсистема невидимости
		TWeakObjectPtr<AAsdaEffectActor> FxAttached;  // эффект, приаттаченный к соседу до скрытия
		TWeakObjectPtr<AAsdaEffectActor> FxFollow;    // эффект, следующий за соседом (FollowActor) до скрытия
		TWeakObjectPtr<AAsdaEffectActor> FxLate;      // эффект, наложенный ПОСЛЕ скрытия (FollowActor)

		void Expect(bool b, const FString& What)
		{
			if (b) { ++Ok; UE_LOG(LogAsdaCrouchTest, Display, TEXT("%s ok   %s"), kPrefix, *What); }
			else   { ++Fail; UE_LOG(LogAsdaCrouchTest, Error, TEXT("%s FAIL %s"), kPrefix, *What); }
		}
	};

	// Играет ли у персонажа клип anim_<Idx>.
	static bool ClipIs(const AAsdaPlayerCharacter* C, int32 Idx)
	{
		return C && C->GetPlayingClipName().EndsWith(FString::Printf(TEXT("anim_%d"), Idx));
	}

	// Темп компонента тела.
	static float RateOf(const AAsdaPlayerCharacter* C)
	{
		TArray<USkeletalMeshComponent*> Comps;
		if (C) { C->GetComponents<USkeletalMeshComponent>(Comps); }
		return Comps.Num() ? Comps[0]->GetPlayRate() : 0.f;   // ★ 24.10.2026: темп может быть отрицательным — «нет компонента» = 0
	}

	// ★ 24.10.2026: длина текущего клипа тела, с (0 — клипа нет).
	static float ClipLenOf(const AAsdaPlayerCharacter* C)
	{
		TArray<USkeletalMeshComponent*> Comps;
		if (C) { C->GetComponents<USkeletalMeshComponent>(Comps); }
		UAnimSingleNodeInstance* SN = Comps.Num() ? Comps[0]->GetSingleNodeInstance() : nullptr;   // GetCurrentAsset — не const
		const UAnimSequenceBase* Seq = SN ? Cast<UAnimSequenceBase>(SN->GetCurrentAsset()) : nullptr;
		return Seq ? Seq->GetPlayLength() : 0.f;
	}

	// ★ 24.10.2026: ход времени клипа T0 → T1 с учётом петли (кратчайший сдвиг по кругу длины L).
	static float WrapDelta(float T0, float T1, float L)
	{
		float D = T1 - T0;
		if (L > 0.f) { if (D > 0.5f * L) { D -= L; } else if (D < -0.5f * L) { D += L; } }
		return D;
	}

	// ★ 24.10.2026: «вперёд»/«назад» для строки отчёта.
	static const TCHAR* DirWord(float V) { return V > 0.f ? TEXT("вперёд") : (V < 0.f ? TEXT("назад") : TEXT("стоит")); }

	// Нажать/отпустить клавишу штатным вводом контроллера.
	using AsdaSelfTestInput::KeyEv;   // ★ R5-c (12.11.2026): общая точка (`Test/AsdaSelfTestInput.h`) — копия была дословной.

	// Запуск шага через Delay секунд от «сейчас».
	using AsdaSelfTestCtx::ByWorld::After;   // ★ R5-c (12.11.2026): общая точка (`Test/AsdaSelfTestCtx.h`) — копия была дословной.

	// α / фаза / «рисуется» по подсистеме.
	static FString StealthStr(const FState& S, const AAsdaPlayerCharacter* C, float& A, bool& R)
	{
		int32 Ph = 0; A = -1.f; R = true;
		const bool bHas = S.Stealth.IsValid() && C && S.Stealth->GetStateFor(C, A, Ph, R);
		return bHas ? FString::Printf(TEXT("α %.2f, фаза %d, рисуется %d, скрыт %d"), A, Ph, R ? 1 : 0, C->IsHidden() ? 1 : 0) : TEXT("нет состояния");
	}

	// ★ 04.11.2026 [ИСПРАВЛЕНИЕ самотеста]: ожидаемая α чужого невидимки ПО ЧАСАМ ПОДСИСТЕМЫ (фаза 0x10000): за одно и то же dt
	//   `CharManager_UpdateAll` гасит 0.00025, `charEntityUpdateTick` — 0.0005 ⇒ α = 1 − 0.00075·PhaseMs (до 0). Прежде α сверялась
	//   с задержкой таймера, а таймер мира и тик подсистемы расходятся на просадке кадра (журнал 04.11 15:24: кадр 184→185 = 1.13 с
	//   после первой загрузки BP_Fx_king_cap01; тик клампит dt до 0.25 с и идёт ПОСЛЕ таймеров кадра) — ложные FAIL G1/G4a/G5/G5b.
	static float OtherFadeAlpha(const FState& S, const AAsdaPlayerCharacter* C, float& OutPhaseMs)
	{
		OutPhaseMs = (S.Stealth.IsValid() && C) ? S.Stealth->GetPhaseMsFor(C) : -1.f;
		return FMath::Max(0.f, 1.f - 0.00075f * FMath::Max(0.f, OutPhaseMs));
	}

	// Опрос условия каждые 0.05 с игрового времени до `TimeoutSec`, затем `Then` (условие проверяет сам `Then`).
	static void PollThen(TWeakObjectPtr<UWorld> WW, TFunction<bool()> Cond, float TimeoutSec, TFunction<void()> Then)
	{
		UWorld* W = WW.Get(); if (!W) { Then(); return; }
		if (Cond() || TimeoutSec <= 0.f) { Then(); return; }
		After(W, 0.05f, [WW, Cond, TimeoutSec, Then]() { PollThen(WW, Cond, TimeoutSec - 0.05f, Then); });
	}

	// ★ 24.10.2026 — раздел H: ход вприсядку назад. Шаги идут таймерами (позиция клипа меряется за 0.12 с), в конце — `Finish`.
	static void RunBackward(TWeakObjectPtr<UWorld> WW, TSharedRef<FState> S, TFunction<void()> Finish)
	{
		AAsdaPlayerCharacter* Me = S->Me.Get(); UWorld* W = WW.Get();
		if (!Me || !W) { Finish(); return; }
		AsdaCVarSave::SetForTest(TEXT("asda.Crouch.BackwardReverse"), 1);
		Me->StopMove(); Me->SetAnimState(EAsdaPlayerState::Idle);
		Me->SetCrouch(true, TEXT("самотест H"));
		const FVector2D Fw(1.f, 0.f);
		Me->SetMoveDirection(Fw, 0.35f, Fw, false);   // W: вперёд, лицом по ходу
		After(W, 0.3f, [WW, S, Finish, Fw]()   // бленд входа (0.15 с) окончен
		{
			AAsdaPlayerCharacter* M1 = S->Me.Get(); if (!M1) { Finish(); return; }
			const float RF = RateOf(M1);
			S->Expect(ClipIs(M1, AAsdaPlayerCharacter::kClipCrouchWalk) && RF > 0.f && M1->CrouchClipTimeSign() > 0.f,
				FString::Printf(TEXT("H1 вперёд вприсядку: клип «%s», темп %.2f (> 0), знак %+.0f"), *M1->GetPlayingClipName(), RF, M1->CrouchClipTimeSign()));
			const float T0 = M1->GetAttackClipPositionSec();
			After(WW.Get(), 0.12f, [WW, S, Finish, Fw, RF, T0]()
			{
				AAsdaPlayerCharacter* M2 = S->Me.Get(); if (!M2) { Finish(); return; }
				const float L = ClipLenOf(M2);
				const float Tb = M2->GetAttackClipPositionSec();
				const float DF = WrapDelta(T0, Tb, L);
				S->Expect(DF > 0.f, FString::Printf(TEXT("H1b время клипа идёт вперёд: %.3f → %.3f (сдвиг %+.3f, длина %.3f)"), T0, Tb, DF, L));
				// S: шаг к камере, лицом вперёд (как контроллер: курс = −направление, пятка).
				M2->SetMoveDirection(-Fw, 0.35f, Fw, true);
				const float Ta = M2->GetAttackClipPositionSec();
				const float RB = RateOf(M2);
				S->Expect(ClipIs(M2, AAsdaPlayerCharacter::kClipCrouchWalk) && RB < 0.f && FMath::IsNearlyEqual(-RB, RF, 0.02f) && M2->CrouchClipTimeSign() < 0.f,
					FString::Printf(TEXT("H2 пятясь (S): клип «%s», темп %.2f (= −%.2f), знак %+.0f"), *M2->GetPlayingClipName(), RB, RF, M2->CrouchClipTimeSign()));
				S->Expect(FMath::Abs(WrapDelta(Tb, Ta, L)) < 0.02f,
					FString::Printf(TEXT("H2a смена W→S без перезапуска клипа: позиция %.3f → %.3f"), Tb, Ta));
				After(WW.Get(), 0.12f, [WW, S, Finish, Fw, Ta, L]()
				{
					AAsdaPlayerCharacter* M3 = S->Me.Get(); if (!M3) { Finish(); return; }
					const float T3 = M3->GetAttackClipPositionSec();
					const float DB = WrapDelta(Ta, T3, L);
					S->Expect(DB < 0.f, FString::Printf(TEXT("H2b время клипа идёт назад: %.3f → %.3f (сдвиг %+.3f)"), Ta, T3, DB));
					// Вбок (проекция 0) — вперёд; диагональ назад (S+A, курс = −направление) — назад; диагональ вперёд — вперёд.
					const FVector2D Side(0.f, 1.f);
					M3->SetMoveDirection(Side, 0.35f, Fw, false);
					const float RS = RateOf(M3);
					S->Expect(RS > 0.f && M3->CrouchClipTimeSign() > 0.f, FString::Printf(TEXT("H3a вбок (курс ⟂ ходу): темп %.2f — %s"), RS, DirWord(RS)));
					const FVector2D DiagB = FVector2D(-1.f, 1.f).GetSafeNormal();
					M3->SetMoveDirection(DiagB, 0.35f, -DiagB, true);
					const float RDB = RateOf(M3);
					S->Expect(RDB < 0.f && M3->CrouchClipTimeSign() < 0.f, FString::Printf(TEXT("H3b диагональ назад (S+A): темп %.2f — %s"), RDB, DirWord(RDB)));
					const FVector2D DiagF = FVector2D(1.f, 1.f).GetSafeNormal();
					M3->SetMoveDirection(DiagF, 0.35f, DiagF, false);
					const float RDF = RateOf(M3);
					S->Expect(RDF > 0.f && M3->CrouchClipTimeSign() > 0.f, FString::Printf(TEXT("H3c диагональ вперёд (W+A): темп %.2f — %s"), RDF, DirWord(RDF)));
					// Пятка хода «в точку» (эталонная клавиатурная схема: грант к точке позади, лицом от неё).
					M3->SetEtalonBackpedal(true);
					M3->NetMoveFromActor(M3->GetGamePosXZ() - Fw * 600.f, 0.35f);
					const float RP = RateOf(M3);
					S->Expect(ClipIs(M3, AAsdaPlayerCharacter::kClipCrouchWalk) && RP < 0.f,
						FString::Printf(TEXT("H3d ход «в точку» пятясь (эталонная пятка): клип «%s», темп %.2f — %s"), *M3->GetPlayingClipName(), RP, DirWord(RP)));
					M3->SetEtalonBackpedal(false);
					const float RP2 = RateOf(M3);
					S->Expect(RP2 > 0.f, FString::Printf(TEXT("H3e пятка снята — лицом к точке, темп %.2f — %s"), RP2, DirWord(RP2)));
					M3->StopMove();
					// Ключ отката: пятясь — клип вперёд (прежний порт).
					AsdaCVarSave::SetForTest(TEXT("asda.Crouch.BackwardReverse"), 0);
					M3->SetMoveDirection(Fw, 0.35f, Fw, false);
					M3->SetMoveDirection(-Fw, 0.35f, Fw, true);
					const float RK = RateOf(M3);
					const float TK0 = M3->GetAttackClipPositionSec();
					S->Expect(ClipIs(M3, AAsdaPlayerCharacter::kClipCrouchWalk) && RK > 0.f && M3->CrouchClipTimeSign() > 0.f,
						FString::Printf(TEXT("H4 asda.Crouch.BackwardReverse 0 — пятясь клип «%s» вперёд (темп %.2f), прежнее"), *M3->GetPlayingClipName(), RK));
					After(WW.Get(), 0.12f, [WW, S, Finish, Fw, TK0]()
					{
						AAsdaPlayerCharacter* M4 = S->Me.Get(); UAsdaNetSubsystem* Net4 = S->Net.Get(); UWorld* W4 = WW.Get();
						if (!M4 || !Net4 || !W4) { Finish(); return; }
						const float TK1 = M4->GetAttackClipPositionSec();
						const float DK = WrapDelta(TK0, TK1, ClipLenOf(M4));
						S->Expect(DK > 0.f, FString::Printf(TEXT("H4b ключ 0: время клипа пятясь идёт вперёд (сдвиг %+.3f)"), DK));
						AsdaCVarSave::SetForTest(TEXT("asda.Crouch.BackwardReverse"), 1);
						// ★ R4, доля a, запись 9 (15.11.2026) [ИСПРАВЛЕНИЕ ТЕСТА]: на шаги H5–H6 урезание анимации соседей снято. H6 подставляет
						//   соседу курс против хода на один тик; под урезанием поза считается раз в несколько кадров с накопленным временем, и при
						//   кадрах от ~27 мс подставной тик растягивался на всё окно 0,12 с (прогоны `t.MaxFPS 30`: 2 из 3 красные, без урезания 3 из 3
						//   зелёные). Шаг проверяет курс и направление клипа, не урезание. Исходное значение вернёт общий откат ключей теста.
						AsdaCVarSave::SetForTest(TEXT("asda.Anim.Throttle"), 0);
						M4->StopMove(); M4->SetCrouch(false, TEXT("самотест H"));
						// ── Сосед вприсядку (4007 подтип 4): вперёд, затем назад — курс соседа = направление хода (пятки в 4007 нет). ──
						UAsdaWorldSubsystem* WS = W4->GetGameInstance()->GetSubsystem<UAsdaWorldSubsystem>();
						AAsdaPlayerCharacter* P = WS ? Cast<AAsdaPlayerCharacter>(WS->ActorByUserIdx(kPeerUserNum + 1)) : nullptr;
						if (!P) { S->Expect(false, TEXT("H5 сосед-заглушка №2 не найден")); Finish(); return; }
						const FVector2D Pc = P->GetGamePosXZ();
						Net4->OnEntityMove.Broadcast(kPeerUserNum + 1, 1, Pc, Pc + Fw * 600.f, 0.175f, false, 4, -1);
						Net4->OnEntityMove.Broadcast(kPeerUserNum + 1, 1, P->GetGamePosXZ(), P->GetGamePosXZ() - Fw * 600.f, 0.175f, false, 4, -1);
						const float RN = RateOf(P);
						const float FaceDot = FVector2D::DotProduct(P->GetFacingDirGame(), -Fw);
						S->Expect(ClipIs(P, AAsdaPlayerCharacter::kClipCrouchWalk) && RN > 0.f && FaceDot > 0.9f,
							FString::Printf(TEXT("H5 сосед 4007 назад: развёрнут по ходу (курс·ход %.2f), клип «%s», темп %.2f — %s (как оригинал)"),
								FaceDot, *P->GetPlayingClipName(), RN, DirWord(RN)));
						// Курс соседа ведёт шаг хода «в точку» (каждый тик — по направлению шага, пятки у соседа нет), поэтому и через
						//   0.12 с курс по ходу, а время клипа идёт вперёд. Подставной курс против хода тик сразу переписывает.
						const float TN0 = P->GetAttackClipPositionSec();
						P->FaceGameDir(Fw);
						After(W4, 0.12f, [WW, S, Finish, Fw, TN0]()
						{
							UWorld* W5 = WW.Get(); UAsdaNetSubsystem* Net5 = S->Net.Get();
							UAsdaWorldSubsystem* WS5 = W5 ? W5->GetGameInstance()->GetSubsystem<UAsdaWorldSubsystem>() : nullptr;
							AAsdaPlayerCharacter* P5 = WS5 ? Cast<AAsdaPlayerCharacter>(WS5->ActorByUserIdx(kPeerUserNum + 1)) : nullptr;
							AAsdaPlayerCharacter* M5 = S->Me.Get();
							if (!W5 || !P5 || !Net5 || !M5) { Finish(); return; }
							const float R5 = RateOf(P5);
							const float D5 = WrapDelta(TN0, P5->GetAttackClipPositionSec(), ClipLenOf(P5));
							const float Dot5 = FVector2D::DotProduct(P5->GetFacingDirGame(), -Fw);
							S->Expect(ClipIs(P5, AAsdaPlayerCharacter::kClipCrouchWalk) && R5 > 0.f && D5 > 0.f && Dot5 > 0.9f,
								FString::Printf(TEXT("H6 сосед идёт назад 0.12 с: курс·ход %.2f (тик ведёт курс по шагу), темп %.2f, время клипа %+.3f — вперёд"),
									Dot5, R5, D5));
							P5->StopMove();
							// Невидимый сосед (присед по невидимости, подтип 2) назад по 4007 — так же: по ходу, клип вперёд.
							AAsdaPlayerCharacter* PS = S->Peer.Get();
							if (!PS || !PS->IsSkillStealth()) { S->Expect(false, TEXT("H7 невидимый сосед не найден")); Finish(); return; }
							const FVector2D Ps = PS->GetGamePosXZ();
							Net5->OnEntityMove.Broadcast(kPeerUserNum, 1, Ps, Ps - Fw * 600.f, 0.175f, false, 2, -1);
							const float R7 = RateOf(PS);
							S->Expect(ClipIs(PS, AAsdaPlayerCharacter::kClipCrouchWalk) && R7 > 0.f && FVector2D::DotProduct(PS->GetFacingDirGame(), -Fw) > 0.9f,
								FString::Printf(TEXT("H7 невидимый сосед 4007 назад: клип «%s», по ходу, темп %.2f — %s"), *PS->GetPlayingClipName(), R7, DirWord(R7)));
							PS->StopMove();
							// Свой под невидимостью (без Ctrl): пятясь — присед назад.
							if (S->Stealth.IsValid()) { S->Stealth->DebugForceInvisible(M5, 1); }
							M5->SetMoveDirection(Fw, 0.35f, Fw, false);
							M5->SetMoveDirection(-Fw, 0.35f, Fw, true);
							After(W5, 0.12f, [S, Finish]()
							{
								AAsdaPlayerCharacter* M6 = S->Me.Get(); if (!M6) { Finish(); return; }
								const float R8 = RateOf(M6);
								S->Expect(!M6->IsCrouchOn() && M6->IsSkillStealth() && ClipIs(M6, AAsdaPlayerCharacter::kClipCrouchWalk) && R8 < 0.f,
									FString::Printf(TEXT("H8 свой невидимый (без Ctrl) пятясь: клип «%s», темп %.2f — %s"), *M6->GetPlayingClipName(), R8, DirWord(R8)));
								M6->StopMove();
								if (S->Stealth.IsValid()) { S->Stealth->DebugForceInvisible(M6, 0); }
								Finish();
							});
						});
					});
				});
			});
		});
	}

	static void Run(const TArray<FString>& Args, UWorld* W)
	{
		const FAsdaSelfTestArgs A = FAsdaSelfTestArgs::Parse(Args);
		if (!W) { AsdaSelfTestKit::Refused(nullptr, kCmd, A.bQuit, TEXT("нет мира")); return; }
		if (AsdaSelfTestKit::RefuseStart(W, kCmd, A)) { return; }
		TSharedRef<FAsdaSelfTestRunGate> Gate = MakeShared<FAsdaSelfTestRunGate>(kCmd, A.bQuit);
		TSharedRef<FState> S = MakeShared<FState>();
		TWeakObjectPtr<UWorld> WW(W);
		const bool bQuit = A.bQuit;
		// ★ R4-хвосты-c (15.11.2026): шаг с настоящим Ctrl+Q открывает журнал заданий (клавиша Q). Прежде он оставался открытым до конца пакета;
		//   самотест, ставящий своего заново (точка входа 4003 выбрасывает окна), оставлял признак «открыт» без окна, и
		//   `asda.SelfTest.QuestSlotMove` следом получал 16/33. Запоминаем, был ли журнал открыт, и в конце закрываем открытый нами.
		if (const UGameInstance* GI0 = W->GetGameInstance())
		{
			if (const UAsdaQuestLogSubsystem* QL0 = GI0->GetSubsystem<UAsdaQuestLogSubsystem>()) { S->bQuestWasOpen = QL0->IsOpen(); }
		}

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

		auto Finish = [WW, S, bQuit]()
		{
			UWorld* Wd = WW.Get();
			if (!Wd) { return; }
			if (S->Stealth.IsValid())
			{
				S->Stealth->DebugForceDetector(0.f);
				if (S->Me.IsValid())   { S->Stealth->DebugForceInvisible(S->Me.Get(), -1); }
				if (S->Peer.IsValid()) { S->Stealth->DebugForceInvisible(S->Peer.Get(), -1); S->Stealth->DebugForceRelation(S->Peer.Get(), -1, -1); }
			}
			if (S->Me.IsValid()) { S->Me->SetCrouch(false, TEXT("итог самотеста")); }
			if (S->Net.IsValid() && S->CapGen) { AsdaSelfTestKit::DisarmCapture(S->Net.Get(), S->CapGen); S->CapGen = 0; }
			if (const UGameInstance* GI1 = Wd->GetGameInstance())   // ★ R4-хвосты-c (15.11.2026): журнал заданий, открытый шагом Ctrl+Q, закрываем
			{
				UAsdaQuestLogSubsystem* QL1 = GI1->GetSubsystem<UAsdaQuestLogSubsystem>();
				if (QL1 && QL1->IsOpen() && !S->bQuestWasOpen) { QL1->Close(); }
			}
			AsdaCVarSave::RestoreAll(kCmd);
			AsdaSelfTestKit::Finish(Wd, kCmd, bQuit, LogAsdaCrouchTest, kPrefix, S->Ok, S->Fail,
				*FString::Printf(TEXT(" (4006: %d)"), S->Status4006.Num()));
		};

		After(W, A.Delay + 2.f, [WW, Gate, S, bQuit, Finish]()
		{
			UWorld* Wd = WW.Get();
			if (!Wd || !Gate->Pass(Wd)) { return; }
			AAsdaPlayerController* PC = Cast<AAsdaPlayerController>(Wd->GetFirstPlayerController());
			AAsdaPlayerCharacter* Me = PC ? Cast<AAsdaPlayerCharacter>(PC->GetPawn()) : nullptr;
			UAsdaNetSubsystem* Net = AsdaSelfTestKit::NetOf(Wd);
			UAsdaStealthSubsystem* St = Wd->GetGameInstance()->GetSubsystem<UAsdaStealthSubsystem>();
			if (!Me || !Net || !St) { AsdaSelfTestKit::Refused(Wd, kCmd, bQuit, TEXT("нет своего персонажа / сети / подсистемы невидимости")); return; }
			S->Me = Me; S->Net = Net; S->Stealth = St;
			S->CapGen = AsdaSelfTestKit::ArmCapture(Net, [S](uint16 Op, const uint8* P, int32 Len, bool, TCHAR)
			{
				if (Op == 4006 && P && Len >= 43) { S->Status4006.Add((int32)AsdaBytes::RdU16(P + 16 + 3)); }
			}, 0, kCmd);
			if (S->CapGen == 0) { AsdaSelfTestKit::Refused(Wd, kCmd, bQuit, TEXT("перехват отправок занят")); return; }
			AsdaCVarSave::ArmWorldGuard(Wd, kCmd);
			for (const TCHAR* K : { TEXT("asda.Crouch.Enable"), TEXT("asda.Crouch.HideAnim"), TEXT("asda.Crouch.NetStatus"),
			                        TEXT("asda.Crouch.ClipTempo"), TEXT("asda.Stealth.Visual"), TEXT("asda.Stealth.ClearTarget"),
			                        TEXT("asda.Crouch.BackwardReverse") })
			{
				AsdaCVarSave::SetForTest(K, 1);
			}
			// ★ 25.10.2026: разделы A–H проверяют режим «Нажатие» — пункт «Присед» окна настроек на время прогона не действует
			//   (режим «Зажатие» проверяет `asda.SelfTest.CrouchMode`).
			AsdaCVarSave::SetForTest(TEXT("asda.Crouch.HoldMode"), 0);
			S->Land = Me->GetGamePosXZ();
			Me->SetAnimState(EAsdaPlayerState::Idle);

			// ── A: Ctrl ──
			Me->TickCrouchInput(true, false, true);
			S->Expect(!Me->IsCrouchOn(), TEXT("A1 нажатие Ctrl само по себе не переключает (переключает отпускание)"));
			Me->TickCrouchInput(false, false, true);
			S->Expect(Me->IsCrouchOn() && Me->GetMoveStatusFor4006() == AAsdaPlayerCharacter::kCrouchMoveStatus,
				FString::Printf(TEXT("A2 одиночное нажатие Ctrl — присед ВКЛ, статус 4006 = %d (ждём 4)"), Me->GetMoveStatusFor4006()));
			Me->TickCrouchInput(true, false, true); Me->TickCrouchInput(true, true, true); Me->TickCrouchInput(false, false, true);
			S->Expect(Me->IsCrouchOn(), TEXT("A3 Ctrl + новая клавиша (сочетание) — присед не переключается"));
			Me->TickCrouchInput(true, false, true); Me->TickCrouchInput(false, false, false);   // ввод заглушён (чат)
			S->Expect(Me->IsCrouchOn(), TEXT("A4 при заглушённом вводе (набор в чате) Ctrl не переключает"));
			Me->TickCrouchInput(true, false, true); Me->TickCrouchInput(false, false, true);
			S->Expect(!Me->IsCrouchOn() && Me->GetMoveStatusFor4006() == 2, FString::Printf(TEXT("A5 ещё раз — ВЫКЛ, статус %d (ждём 2)"), Me->GetMoveStatusFor4006()));
			// Настоящий ввод: Ctrl вниз/вверх через контроллер (опрос в тике персонажа).
			KeyEv(Wd, EKeys::LeftControl, true);
			After(Wd, 0.2f, [WW, S, Finish]()
			{
				UWorld* W2 = WW.Get(); if (!W2) { Finish(); return; }
				KeyEv(W2, EKeys::LeftControl, false);
				After(W2, 0.2f, [WW, S, Finish]()
				{
					UWorld* W3 = WW.Get(); AAsdaPlayerCharacter* Me3 = S->Me.Get();
					if (!W3 || !Me3) { Finish(); return; }
					S->Expect(Me3->IsCrouchOn(), TEXT("A6 настоящий ввод: левый Ctrl нажат и отпущен — присед ВКЛ"));
					// Ctrl + Q (новая клавиша при зажатом Ctrl) — сочетание.
					KeyEv(W3, EKeys::LeftControl, true);
					After(W3, 0.15f, [WW, S, Finish]()
					{
						UWorld* W4 = WW.Get(); if (!W4) { Finish(); return; }
						KeyEv(W4, EKeys::Q, true);
						After(W4, 0.15f, [WW, S, Finish]()
						{
							UWorld* W5 = WW.Get(); if (!W5) { Finish(); return; }
							KeyEv(W5, EKeys::Q, false); KeyEv(W5, EKeys::LeftControl, false);
							After(W5, 0.2f, [WW, S, Finish]()
							{
								UWorld* W6 = WW.Get(); AAsdaPlayerCharacter* Me6 = S->Me.Get();
								if (!W6 || !Me6) { Finish(); return; }
								S->Expect(Me6->IsCrouchOn(), TEXT("A7 настоящий ввод: Ctrl+Q — сочетание, присед не снят"));

								// ── B: ход вприсядку ──
								const FVector2D From = Me6->GetGamePosXZ();
								Me6->NetMove(From, From + FVector2D(800.f, 0.f), 0.35f);
								const float R1 = RateOf(Me6);
								S->Expect(ClipIs(Me6, AAsdaPlayerCharacter::kClipCrouchWalk) && Me6->IsMoving(),
									FString::Printf(TEXT("B1 ход вприсядку: клип «%s» (ждём anim_239), идёт=%d"), *Me6->GetPlayingClipName(), Me6->IsMoving() ? 1 : 0));
								S->Expect(R1 > 0.5f && FMath::IsNearlyEqual(R1, Me6->CrouchClipRate(), 0.02f),
									FString::Printf(TEXT("B2 темп клипа по донору при 0.35: %.2f"), R1));
								Me6->SetServerMoveSpeed(0.175f);
								After(W6, 0.2f, [WW, S, Finish, R1]()
								{
									UWorld* W7 = WW.Get(); AAsdaPlayerCharacter* Me7 = S->Me.Get(); UAsdaNetSubsystem* Net7 = S->Net.Get();
									if (!W7 || !Me7 || !Net7) { Finish(); return; }
									const float R2 = RateOf(Me7);
									S->Expect(FMath::IsNearlyEqual(R2, R1 * 0.5f, 0.03f), FString::Printf(TEXT("B3 скорость 0.175 — темп вдвое: %.2f → %.2f"), R1, R2));
									// ── C: 4006 ──
									const int32 N0 = S->Status4006.Num();
									const FVector2D Cur = Me7->GetGamePosXZ();
									Net7->SendMove4006((float)Cur.X, (float)Cur.Y, (float)Cur.X + 500.f, (float)Cur.Y);
									Me7->SetCrouch(false, TEXT("самотест C"));
									Net7->SendMove4006((float)Cur.X, (float)Cur.Y, (float)Cur.X + 500.f, (float)Cur.Y);
									S->Expect(S->Status4006.Num() >= N0 + 2 && S->Status4006[N0] == 4 && S->Status4006.Last() == 2,
										FString::Printf(TEXT("C1 4006: в приседе статус %d, без — %d (ждём 4 / 2)"),
											S->Status4006.Num() > N0 ? S->Status4006[N0] : -1, S->Status4006.Num() ? S->Status4006.Last() : -1));
									S->Expect(!ClipIs(Me7, AAsdaPlayerCharacter::kClipCrouchWalk),
										FString::Printf(TEXT("C2 присед снят на ходу — клип хода «%s» (не anim_239)"), *Me7->GetPlayingClipName()));
									AsdaCVarSave::SetForTest(TEXT("asda.Crouch.NetStatus"), 0);
									Me7->SetCrouch(true, TEXT("самотест C3"));
									S->Expect(Me7->GetMoveStatusFor4006() == 2, TEXT("C3 asda.Crouch.NetStatus 0 — в приседе статус 2 (эталон)"));
									AsdaCVarSave::SetForTest(TEXT("asda.Crouch.NetStatus"), 1);
									Me7->StopMove();
									// ── D: свой грант с подтипом 4 (клавиатурный ход: грант несёт скорость сервера) ──
									const int32 SelfIdx = (int32)Net7->SelfUserNum;
									const FVector2D C0 = Me7->GetGamePosXZ();
									Me7->SetMoveDirection(FVector2D(1.f, 0.f), 0.35f, FVector2D(1.f, 0.f), false);
									Net7->OnEntityMove.Broadcast(SelfIdx, 1, C0, C0 + FVector2D(800.f, 0.f), 0.21f, false, 3, -1);
									const float Sp3 = Me7->GetMoveSpeed();
									Net7->OnEntityMove.Broadcast(SelfIdx, 1, C0, C0 + FVector2D(800.f, 0.f), 0.175f, false, 4, -1);
									const float Sp4 = Me7->GetMoveSpeed();
									S->Expect(FMath::IsNearlyEqual(Sp3, 0.35f, 0.001f) && FMath::IsNearlyEqual(Sp4, 0.175f, 0.001f),
										FString::Printf(TEXT("D1 свой грант: подтип 3 отброшен (скорость %.3f), подтип 4 исполнен (скорость %.3f)"), Sp3, Sp4));
									Me7->StopMove();
									// ── E: сосед ──
									UAsdaWorldSubsystem* WS = W7->GetGameInstance()->GetSubsystem<UAsdaWorldSubsystem>();
									if (WS) { WS->DebugSpawnFakePeer(kPeerUserNum, TEXT("Тень"), S->Land + FVector2D(300.f, 300.f)); }
									AAsdaPlayerCharacter* P = WS ? Cast<AAsdaPlayerCharacter>(WS->ActorByUserIdx(kPeerUserNum)) : nullptr;
									S->Peer = P;
									if (!P) { S->Expect(false, TEXT("E0 сосед-заглушка не появился")); Finish(); return; }
									const FVector2D Pc = P->GetGamePosXZ();
									Net7->OnEntityMove.Broadcast(kPeerUserNum, 1, Pc, Pc + FVector2D(600.f, 0.f), 0.175f, false, 4, -1);
									S->Expect(P->IsNetCrouch() && ClipIs(P, AAsdaPlayerCharacter::kClipCrouchWalk),
										FString::Printf(TEXT("E1 сосед 4007 подтип 4: присед=%d, клип «%s»"), P->IsNetCrouch() ? 1 : 0, *P->GetPlayingClipName()));
									Net7->OnEntityMove.Broadcast(kPeerUserNum, 1, P->GetGamePosXZ(), Pc + FVector2D(900.f, 0.f), 0.35f, false, 2, -1);
									S->Expect(!P->IsNetCrouch() && !ClipIs(P, AAsdaPlayerCharacter::kClipCrouchWalk),
										FString::Printf(TEXT("E2 сосед подтип 2: обычный ход, клип «%s»"), *P->GetPlayingClipName()));
									P->StopMove();
									// E3: сосед появился посреди хода вприсядку (код состояния появления @102 = 4).
									if (WS) { WS->DebugSpawnFakePeerState(kPeerUserNum + 1, TEXT("Тень2"), S->Land + FVector2D(-300.f, 300.f), AAsdaPlayerCharacter::kCrouchMoveStatus); }
									const AAsdaPlayerCharacter* P2 = WS ? Cast<AAsdaPlayerCharacter>(WS->ActorByUserIdx(kPeerUserNum + 1)) : nullptr;
									S->Expect(P2 && P2->IsNetCrouch(), FString::Printf(TEXT("E3 сосед появился со статусом 4 — сразу присед: %d"), P2 && P2->IsNetCrouch() ? 1 : 0));
									// ── F: отказы и снятие ──
									Me7->SetCrouch(false, TEXT("самотест F3"));
									Me7->SetAnimState(EAsdaPlayerState::Sit);
									S->Expect(!Me7->SetCrouch(true, TEXT("самотест F3")) && Me7->GetLastCrouchRefusal() == TEXT("сидит"),
										FString::Printf(TEXT("F3 сидя отклонён (%s)"), *Me7->GetLastCrouchRefusal()));
									Me7->SetAnimState(EAsdaPlayerState::Move); Me7->SetAnimState(EAsdaPlayerState::Idle);
									Me7->SetCrouch(true, TEXT("самотест F1"));
									FString Model, Set; float H = 0.f;
									if (WS && WS->LookupMount(522, Model, Set, H))
									{
										Me7->SetMount(Model, Set, H, FString(), FString(), FString(), FString());
										S->Expect(!Me7->IsCrouchOn(), FString::Printf(TEXT("F1 посадка снимает присед (%s)"), *Me7->GetLastCrouchRefusal()));
										const bool bM = Me7->SetCrouch(true, TEXT("самотест F2"));
										S->Expect(!bM && Me7->GetLastCrouchRefusal() == TEXT("верхом"), FString::Printf(TEXT("F2 верхом присед отклонён (%s)"), *Me7->GetLastCrouchRefusal()));
										Me7->ClearMount();
									}
									else { S->Expect(false, TEXT("F1 транспорт 522 не найден")); }
									AsdaCVarSave::SetForTest(TEXT("asda.Crouch.Enable"), 0);
									S->Expect(!Me7->SetCrouch(true, TEXT("самотест F4")), TEXT("F4 asda.Crouch.Enable 0 — приседа нет"));
									AsdaCVarSave::SetForTest(TEXT("asda.Crouch.Enable"), 1);

									// ── G: невидимость соседа ──
									UAsdaStealthSubsystem* St7 = S->Stealth.Get();
									St7->DebugForceRelation(P, 0, 0);
									// Эффекты на соседе ДО скрытия: приаттаченный и следующий за носителем (FollowActor, канал состояний attach-3/5/9/12).
									if (AAsdaEffectActor* Fa = AAsdaEffectActor::SpawnByJebName(W7, TEXT("king_cap01"), P->GetActorLocation(), FRotator::ZeroRotator, P))
									{
										Fa->AttachToActor(P, FAttachmentTransformRules::KeepWorldTransform); S->FxAttached = Fa;
									}
									if (AAsdaEffectActor* Ff = AAsdaEffectActor::SpawnByJebName(W7, TEXT("king_cap01"), P->GetActorLocation(), FRotator::ZeroRotator, P))
									{
										Ff->FollowActor = P; Ff->bFollowPosition = true; S->FxFollow = Ff;
									}
									S->Expect(S->FxAttached.IsValid() && S->FxFollow.IsValid(), TEXT("G0 эффекты на соседе созданы (BP_Fx_king_cap01)"));
									St7->DebugForceInvisible(P, 1);
									After(W7, 0.3f, [WW, S, Finish]()
									{
										AAsdaPlayerCharacter* P8 = S->Peer.Get(); if (!P8) { Finish(); return; }
										float A8 = 0.f; bool R8 = true; const FString Str = StealthStr(*S, P8, A8, R8);
										// Ревизия ВАЖНО-1: шаг эталона 0.00025 + 0.0005 = 0.00075/мс → за 0.3 с α ≈ 0.78 (не ×10).
										float Ph8 = -1.f; const float E8 = OtherFadeAlpha(*S, P8, Ph8);
										S->Expect(P8->IsSkillStealth() && Ph8 > 0.f && A8 > 0.f && A8 < 1.f && FMath::IsNearlyEqual(A8, E8, 0.02f) && R8,
											FString::Printf(TEXT("G1 сосед стал невидимым — гаснет темпом эталона (α = 1 − 0.00075·%.0f мс = %.2f): %s"), Ph8, E8, *Str));
										P8->NetMove(P8->GetGamePosXZ(), P8->GetGamePosXZ() + FVector2D(300.f, 0.f), 0.175f);
										S->Expect(ClipIs(P8, AAsdaPlayerCharacter::kClipCrouchWalk),
											FString::Printf(TEXT("G2 ход невидимого — присед донора: «%s»"), *P8->GetPlayingClipName()));
										P8->StopMove();
										AsdaCVarSave::SetForTest(TEXT("asda.Crouch.HideAnim"), 0);
										P8->NetMove(P8->GetGamePosXZ(), P8->GetGamePosXZ() + FVector2D(300.f, 0.f), 0.175f);
										S->Expect(P8->IsMoving() && !ClipIs(P8, AAsdaPlayerCharacter::kClipCrouchWalk),
											FString::Printf(TEXT("G3 asda.Crouch.HideAnim 0 — клип хода «%s» (walk01 эталона, не присед)"), *P8->GetPlayingClipName()));
										AsdaCVarSave::SetForTest(TEXT("asda.Crouch.HideAnim"), 1);
										P8->StopMove();
										// Цель-невидимка снимается.
										if (UWorld* W8 = WW.Get()) { if (AAsdaPlayerController* PC8 = Cast<AAsdaPlayerController>(W8->GetFirstPlayerController())) { PC8->TargetActor = P8; } }
										After(WW.Get(), 0.65f, [WW, S]()   // t ≈ 0.95 с: ещё рисуется
										{
											AAsdaPlayerCharacter* Pm = S->Peer.Get(); if (!Pm) { return; }
											float Am = 0.f; bool Rm = true; const FString Str = StealthStr(*S, Pm, Am, Rm);
											float Phm = -1.f; const float Em = OtherFadeAlpha(*S, Pm, Phm);
											S->Expect(Em > 0.f ? (Rm && FMath::IsNearlyEqual(Am, Em, 0.02f)) : (!Rm && Am <= 0.f),
												FString::Printf(TEXT("G4a на %.0f мс фазы α = %.2f (ждём %.2f, пока α > 0 — рисуется): %s"), Phm, Am, Em, *Str));
										});
										// t ≈ 1.55 с по таймеру, затем — пока подсистема не досчитает фазу 0x10000 (2000 мс своих часов).
										After(WW.Get(), 1.25f, [WW, S, Finish]() { PollThen(WW, [S]()
										{
											float A = 1.f; int32 Ph = 0; bool R = true;
											return !S->Stealth.IsValid() || !S->Peer.IsValid() || !S->Stealth->GetStateFor(S->Peer.Get(), A, Ph, R) || Ph != 1;
										}, 6.f, [WW, S, Finish]()
										{
											UWorld* W9 = WW.Get(); AAsdaPlayerCharacter* P9 = S->Peer.Get(); if (!W9 || !P9) { Finish(); return; }
											AAsdaPlayerController* PC9 = Cast<AAsdaPlayerController>(W9->GetFirstPlayerController());
											S->Expect(PC9 && PC9->TargetActor.Get() != P9, TEXT("G4 цель-невидимка снята (эталон game.c:10153)"));
											float A9 = 0.f; bool R9 = true; const FString Str = StealthStr(*S, P9, A9, R9);
											S->Expect(!R9 && A9 <= 0.f && P9->IsHidden(), FString::Printf(TEXT("G5 по концу фазы затухания (2000 мс часов подсистемы) чужой не рисуется: %s"), *Str));
											// К1: эффекты, наложенные ДО скрытия (приаттаченный и следующий), скрыты; наложенный ПОСЛЕ — скроет себя сам.
											const AAsdaEffectActor* Fa = S->FxAttached.Get(); const AAsdaEffectActor* Ff = S->FxFollow.Get();
											S->Expect(Fa && Ff && Fa->IsHidden() && Ff->IsHidden(), FString::Printf(TEXT("G5b эффекты до скрытия скрыты: приаттаченный %d, следующий %d"),
												Fa && Fa->IsHidden() ? 1 : 0, Ff && Ff->IsHidden() ? 1 : 0));
											if (AAsdaEffectActor* Fl = AAsdaEffectActor::SpawnByJebName(W9, TEXT("king_cap01"), P9->GetActorLocation(), FRotator::ZeroRotator, P9))
											{
												Fl->FollowActor = P9; Fl->bFollowPosition = true; S->FxLate = Fl;
											}
											After(W9, 0.2f, [S]()
											{
												const AAsdaEffectActor* Fl = S->FxLate.Get();
												S->Expect(Fl && Fl->IsHidden(), FString::Printf(TEXT("G5c эффект, наложенный ПОСЛЕ скрытия, скрыл себя сам: %d"), Fl && Fl->IsHidden() ? 1 : 0));
											});
											// член отряда — ПОСЛЕ проверки G5c (иначе сосед стал бы виден отряду и эффект не обязан скрываться)
											After(W9, 0.25f, [S]() { if (S->Stealth.IsValid() && S->Peer.IsValid()) { S->Stealth->DebugForceRelation(S->Peer.Get(), 1, 0); } });
											After(W9, 1.25f, [WW, S, Finish]()
											{
												UWorld* W10 = WW.Get(); AAsdaPlayerCharacter* P10 = S->Peer.Get(); if (!W10 || !P10) { Finish(); return; }
												float A10 = 0.f; bool R10 = true; const FString Str = StealthStr(*S, P10, A10, R10);
												S->Expect(R10 && FMath::IsNearlyEqual(A10, 0.2f, 0.01f) && !P10->IsHidden(), FString::Printf(TEXT("G6 член отряда — α 0.2: %s"), *Str));
												S->Stealth->DebugForceRelation(P10, 0, 1);   // Друг
												After(W10, 0.6f, [WW, S, Finish]()
												{
													UWorld* W11 = WW.Get(); AAsdaPlayerCharacter* P11 = S->Peer.Get(); if (!W11 || !P11) { Finish(); return; }
													float A11 = 0.f; bool R11 = true; const FString Str = StealthStr(*S, P11, A11, R11);
													S->Expect(R11 && FMath::IsNearlyEqual(A11, 0.4f, 0.01f), FString::Printf(TEXT("G7 Друг — α 0.4: %s"), *Str));
													S->Stealth->DebugForceRelation(P11, 0, 0);
													S->Stealth->DebugForceDetector(5000.f);    // детектор 154, в радиусе
													After(W11, 2.5f, [WW, S, Finish]()
													{
														UWorld* W12 = WW.Get(); AAsdaPlayerCharacter* P12 = S->Peer.Get(); if (!W12 || !P12) { Finish(); return; }
														float A12 = 0.f; bool R12 = true; const FString Str = StealthStr(*S, P12, A12, R12);
														S->Expect(R12 && FMath::IsNearlyEqual(A12, 0.5f, 0.02f), FString::Printf(TEXT("G8 детектор 154 в радиусе — α 0.5: %s"), *Str));
														S->Stealth->DebugForceDetector(1.f);      // вне радиуса
														After(W12, 2.6f, [WW, S, Finish]()
														{
															UWorld* W13 = WW.Get(); AAsdaPlayerCharacter* P13 = S->Peer.Get(); if (!W13 || !P13) { Finish(); return; }
															float A13 = 0.f; bool R13 = true; const FString Str = StealthStr(*S, P13, A13, R13);
															S->Expect(!R13 && P13->IsHidden(), FString::Printf(TEXT("G9 детектор вне радиуса — снова не рисуется: %s"), *Str));
															S->Stealth->DebugForceDetector(0.f);
															// ВАЖНО-2: наблюдатель под GM-скрытием видит невидимок (α 0 → 0.5, game.c:55656-55664).
															if (AAsdaPlayerCharacter* MeG = S->Me.Get()) { MeG->SetGmHideTranslucent(true); }
															After(W13, 0.15f, [WW, S, Finish]()
															{
															AAsdaPlayerCharacter* P13g = S->Peer.Get(); AAsdaPlayerCharacter* MeG2 = S->Me.Get(); UWorld* W13g = WW.Get();
															if (!P13g || !MeG2 || !W13g) { Finish(); return; }
															float Ag = 0.f; bool Rg = true; const FString StrG = StealthStr(*S, P13g, Ag, Rg);
															S->Expect(Rg && !P13g->IsHidden() && Ag > 0.3f, FString::Printf(TEXT("G9b наблюдатель под GM-скрытием видит невидимого: %s"), *StrG));
															MeG2->SetGmHideTranslucent(false);
															S->Stealth->DebugForceInvisible(P13g, 0);   // снятие
															After(W13g, 0.3f, [WW, S, Finish]()
															{
																UWorld* W14 = WW.Get(); AAsdaPlayerCharacter* P14 = S->Peer.Get(); AAsdaPlayerCharacter* Me14 = S->Me.Get();
																if (!W14 || !P14 || !Me14) { Finish(); return; }
																float A14 = 0.f; bool R14 = true; const FString Str = StealthStr(*S, P14, A14, R14);
																S->Expect(R14 && !P14->IsHidden() && A14 >= 0.999f && !P14->IsSkillStealth(),
																	FString::Printf(TEXT("G10 снятие у чужого — виден сразу, α 1: %s"), *Str));
																// Свой.
																S->Stealth->DebugForceInvisible(Me14, 1);
																After(W14, 2.3f, [WW, S, Finish]()
																{
																	UWorld* W15 = WW.Get(); AAsdaPlayerCharacter* Me15 = S->Me.Get(); if (!W15 || !Me15) { Finish(); return; }
																	float A15 = 0.f; bool R15 = true; const FString Str = StealthStr(*S, Me15, A15, R15);
																	S->Expect(R15 && !Me15->IsHidden() && FMath::IsNearlyEqual(A15, 0.3f, 0.01f) && Me15->IsStealthTranslucencyActive(),
																		FString::Printf(TEXT("G11 свой невидимый — α 0.3, полупрозрачен, виден себе: %s"), *Str));
																	S->Stealth->DebugForceInvisible(Me15, 0);
																	After(W15, 0.4f, [WW, S, Finish]()
																	{
																		AAsdaPlayerCharacter* Me16 = S->Me.Get(); if (!Me16) { Finish(); return; }
																		float A16 = 0.f; bool R16 = true; const FString Str = StealthStr(*S, Me16, A16, R16);
																		S->Expect(A16 > 0.3f && A16 < 1.f, FString::Printf(TEXT("G12 свой: снятие — проявление идёт: %s"), *Str));
																		After(WW.Get(), 1.0f, [WW, S, Finish]()
																		{
																			AAsdaPlayerCharacter* Me17 = S->Me.Get(); AAsdaPlayerCharacter* P17 = S->Peer.Get(); if (!Me17 || !P17) { Finish(); return; }
																			float A17 = 0.f; bool R17 = true; const FString Str = StealthStr(*S, Me17, A17, R17);
																			S->Expect(A17 >= 0.999f && !Me17->IsStealthTranslucencyActive(), FString::Printf(TEXT("G13 свой: за 1 с α 1: %s"), *Str));
																			// Ключ отката вида.
																			AsdaCVarSave::SetForTest(TEXT("asda.Stealth.Visual"), 0);
																			S->Stealth->DebugForceInvisible(P17, 1);
																			After(WW.Get(), 2.4f, [WW, S, Finish]()
																			{
																				AAsdaPlayerCharacter* P18 = S->Peer.Get(); if (!P18) { Finish(); return; }
																				float A18 = 0.f; bool R18 = true; const FString Str = StealthStr(*S, P18, A18, R18);
																				S->Expect(R18 && !P18->IsHidden() && A18 >= 0.999f && P18->IsSkillStealth(),
																					FString::Printf(TEXT("G14 asda.Stealth.Visual 0 — невидимый рисуется как обычно (анимация остаётся): %s"), *Str));
																				// D2 (в конце — ход не должны прерывать следующие шаги): присед переключён, пока прежний 4006
																				//   в пути, — статус досылается после ответа.
																				UWorld* W7 = WW.Get(); AAsdaPlayerCharacter* Me7 = S->Me.Get(); UAsdaNetSubsystem* Net7 = S->Net.Get();
																				if (!W7 || !Me7 || !Net7) { Finish(); return; }
																				Me7->SetCrouch(false, TEXT("самотест D2"));
																				Me7->NetMove(Me7->GetGamePosXZ(), Me7->GetGamePosXZ() + FVector2D(800.f, 0.f), 0.35f);   // ход «в точку»
																				const bool bConn0 = Net7->bInGameConnected;
																				Net7->bInGameConnected = true;
																				Net7->bMoveRequestInFlight = true; Net7->MoveRequestStampMs = UAsdaNetSubsystem::MoveClockMs();
																				const int32 ND = S->Status4006.Num();
																				Me7->SetCrouch(true, TEXT("самотест D2"));
																				const int32 NDin = S->Status4006.Num();
																				Net7->ClearMoveRequestInFlight(TEXT("самотест D2: ответ пришёл"));
																				After(W7, 0.25f, [S, Finish, ND, NDin, bConn0]()
																				{
																					UAsdaNetSubsystem* NetD = S->Net.Get(); AAsdaPlayerCharacter* MeD = S->Me.Get();
																					if (NetD) { NetD->bInGameConnected = bConn0; NetD->ClearMoveRequestInFlight(TEXT("самотест D2: конец")); }
																					S->Expect(NDin == ND && S->Status4006.Num() > ND && S->Status4006.Last() == 4,
																						FString::Printf(TEXT("D2 в пути 4006 не ушёл (%d), после ответа дослан со статусом %d"), NDin - ND,
																							S->Status4006.Num() > ND ? S->Status4006.Last() : -1));
																					if (MeD) { MeD->StopMove(); }
																					// A8 (ревизия): AltGr = LCtrl + RAlt в одном кадре — присед не переключается.
																					UWorld* WA = MeD ? MeD->GetWorld() : nullptr;
																					if (!WA) { Finish(); return; }
																					const bool bBefore = MeD->IsCrouchOn();
																					KeyEv(WA, EKeys::LeftControl, true); KeyEv(WA, EKeys::RightAlt, true);
																					After(WA, 0.15f, [S, Finish, bBefore]()
																					{
																						AAsdaPlayerCharacter* MeA = S->Me.Get(); UWorld* WB = MeA ? MeA->GetWorld() : nullptr;
																						if (!WB) { Finish(); return; }
																						KeyEv(WB, EKeys::RightAlt, false); KeyEv(WB, EKeys::LeftControl, false);
																						After(WB, 0.2f, [S, Finish, bBefore]()
																						{
																							AAsdaPlayerCharacter* MeC = S->Me.Get();
																							S->Expect(MeC && MeC->IsCrouchOn() == bBefore, FString::Printf(TEXT("A8 AltGr (LCtrl+RAlt в одном кадре) — присед не переключён: было %d, стало %d"),
																								bBefore ? 1 : 0, MeC && MeC->IsCrouchOn() ? 1 : 0));
																							RunBackward(TWeakObjectPtr<UWorld>(MeC ? MeC->GetWorld() : nullptr), S, Finish);   // ★ 24.10.2026: раздел H
																						});
																					});
																				});
																			});
																		});
																	});
																});
															});
														});
																												});
});
												});
											});
										}); });
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

static FAutoConsoleCommandWithWorldAndArgs GAsdaCrouchSelfTest(
	TEXT("asda.SelfTest.Crouch"),
	TEXT("Самотест ходьбы вприсядку (Ctrl, anim_239, 4006 статус 4, свой и чужой грант) и вида невидимости 112/186 (затухание, отряд 0.2, "
	     "Друг 0.4, детектор 0.5, снятие, свой 0.3). Только офлайн. `delay=N` `quit`."),
	FConsoleCommandWithWorldAndArgsDelegate::CreateLambda([](const TArray<FString>& A, UWorld* W)
	{
		AsdaCrouchTest::Run(A, W);
	}));

#endif // !UE_BUILD_SHIPPING
