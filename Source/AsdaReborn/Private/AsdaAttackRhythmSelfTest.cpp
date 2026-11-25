// Copyright (c) 2026 @sitxovski (Discord)
// ★★★ САМОТЕСТ РИТМА ОБЫЧНОЙ АТАКИ ПО СЕРВЕРНЫМ ЧАСАМ (15.10.2026, `Reports/anticheat/wave_PORT_attack_rhythm.md`). [ПОРТ]
//
// ★ ЗАЧЕМ. NpcServer молча отбрасывает удар 4028, пришедший раньше своей длительности (Combo.lst/Skill.lst, баффы).
//   Правка `UAsdaAttackRhythmSubsystem` + точки отправки замаха (`AAsdaPlayerCharacter::BeginSendSegment`) обязана:
//   (1) никогда не слать 4028 раньше серверной длительности; (2) не замедлять честный бой (интервал ≤ длительность + 5 %);
//   (3) держать анимацию на тех же часах (удар уходит в эталонной точке клипа); (4) повторный клик не начинает серию;
//   (5) после навыка 4028 не раньше Skill.lst; (6) баффы скорости и ×0.5 — по формуле сервера.
//
// ★ КАК. Офлайн, ТЕМ ЖЕ путём, что в игре: свой персонаж (`Asda.World.FakeSelfSpawn`), моб-заглушка рядом, боевой тик
//   контроллера (`AttackTick` → 4008/4026 → замах → `OnSwingCycleCompleted` → `SendAttackNext4028`), признак «в мире» и
//   ПОЛНЫЙ перехват исходящих (`AsdaSelfTestKit::ArmCapture`: сокета нет, всё — в перехватчик). Время каждого 4026/4028 —
//   `FPlatformTime` в перехватчике; там же снимаются требование часов, позиция клипа и точки отправки замаха.
//   ★ Контрольные пары (закон pixel-metric-needs-control-pairs): `asda.Combat.ServerRhythm 0` — лук снова стреляет чаще
//   сервера, повторный клик снова даёт вторую 4026; без них проверки были бы слепыми.
//
// Консоль: `asda.Combat.SelfTestRhythm [delay=N] [quit]`. Только офлайн. Строки — «САМОТЕСТ-РИТМ», итог — «САМОТЕСТ-РИТМ ИТОГ».
#include "CoreMinimal.h"

#if !UE_BUILD_SHIPPING

#include "AsdaNetSubsystem.h"
#include "AsdaCVarSave.h"                      // ключи на время прогона (вернутся в итоге)
#include "Test/AsdaSelfTestRunGate.h"   // FAsdaSelfTestRunGate — офлайн повторно на отложенном старте   ★ R3: FAsdaSelfTestRunGate — узкий заголовок вместо AsdaOutgoingGate.h
#include "Test/AsdaSelfTestKit.h"              // каркас: аргументы, отказ, перехват, итог, выход
#include "World/AsdaAttackRhythmSubsystem.h"   // проверяемые часы
#include "World/AsdaPlayerController.h"        // боевой тик, клик по цели
#include "World/AsdaPlayerCharacter.h"         // замах, точки отправки, позиция клипа
#include "World/AsdaWorldSubsystem.h"          // величина состояния
#include "World/AsdaCoord.h"                   // игра ↔ UE
#include "Items/AsdaItemTableSubsystem.h"      // оружие каждого типа
#include "UI/AsdaEffectsSubsystem.h"           // состояния 27/28/162/206 своему
#include "UI/AsdaSkillDatabase.h"              // навык-источник состояния
#include "UI/AsdaSkillUseSubsystem.h"          // состояние действия 25/24/14 (сценарий настоящего навыка)
#include "Npc/AsdaMobActor.h"                  // моб-заглушка
#include "Engine/Engine.h"
#include "Engine/GameInstance.h"
#include "Engine/World.h"
#include "GameFramework/PlayerController.h"
#include "HAL/IConsoleManager.h"
#include "TimerManager.h"
#include "Misc/App.h"                        // FApp::GetDeltaTime — допуск фазы по кадру

DEFINE_LOG_CATEGORY_STATIC(LogAsdaRhythmTest, Log, All);

namespace AsdaRhythmTestLocal
{
	// Один перехваченный удар.
	struct FCap
	{
		int32 Op = 0;               // 4026 / 4028
		double T = 0.0;             // FPlatformTime в момент отправки
		double PrevS = 0.0;         // часы: время прошлого принятого (до этой отправки)
		int32 ReqMs = 0;            // часы: сколько нужно было ждать
		float ClipPos = 0.f;        // позиция клипа замаха (клип-секунды)
		float ClipLen = 0.f;        // длина клипа
		float Rate = 0.f;           // скорость отрезка замаха
		float Rewind = 0.f;         // точка перемотки цикла
		float FrameS = 0.f;         // длительность кадра в момент перехвата (допуск фазы: перехват точного удара — в кадре после ухода)
		float LateFrameS = 0.f;     // ★ R5-fix6 (ревизия): наибольший из трёх последних кадров — только для границы «не позже» (допуск фазы берёт FrameS)
		TArray<float> Pts;          // точки отправки текущего прохода
		bool bEvened = false;       // ★ mobpos_swing: отрезок первого прохода шёл темпом цикла (позже минимума сервера — законно)
		float PlannedMs = 0.f;      // ★ mobpos_swing: длительность этого отрезка по плану замаха, мс
	};
	// Шаг сценария.
	struct FStep
	{
		FString Name;
		float MinSec = 0.f, TimeoutSec = 10.f;
		TFunction<void()> Start;
		TFunction<bool()> Poll;
		TFunction<void()> Check;
	};
	struct FState
	{
		TWeakObjectPtr<UWorld> World;
		TWeakObjectPtr<UAsdaNetSubsystem> Net;
		TArray<FStep> Steps;
		int32 StepIdx = 0;
		bool bStepStarted = false;
		double StepStartS = 0.0;
		FTimerHandle Tick;
		bool bQuit = false;
		FAsdaSelfTestRunGate Gate{ TEXT("asda.Combat.SelfTestRhythm"), false };
		int32 Ok = 0, Fail = 0;
		TArray<FCap> Caps;
		int32 Mark = 0;             // начало текущего сценария в Caps
		// Рывки игрового потока (реальный промежуток между тиками исполнителя > 100 мс): таймеры замаха живут игровым временем,
		//   и при рывке (зажим шага кадра) точка удара опаздывает — удар позже, никогда не раньше. Интервалы, задевшие рывок,
		//   из проверки «не позже +5 %» исключаются и считаются отдельно (сама проверка «не раньше» к ним применяется).
		double LastTickS = 0.0;
		TArray<TPair<double, double>> Stalls;
		// ★ R5-fix6: длины трёх последних кадров игры — удар уходит на тике, чей кадр мог быть длиннее кадра перехвата.
		float RecentFrameS[3] = { 0.f, 0.f, 0.f };
		uint64 RecentFrameNo = 0;
		// Наибольший из трёх последних кадров, с.
		float RecentFrameMaxS() const { return FMath::Max3(RecentFrameS[0], RecentFrameS[1], RecentFrameS[2]); }
		// Учесть текущий кадр (один раз на кадр игры).
		void NoteFrame()
		{
			if (RecentFrameNo == GFrameCounter) { return; }
			RecentFrameNo = GFrameCounter;
			RecentFrameS[0] = RecentFrameS[1]; RecentFrameS[1] = RecentFrameS[2]; RecentFrameS[2] = (float)FApp::GetDeltaTime();
		}
		uint32 Gen = 0;             // поколение перехватчика
		TWeakObjectPtr<AAsdaMobActor> Mob;
		TWeakObjectPtr<AAsdaMobActor> Mob2;   // вторая цель (сценарий смены цели)
		double OldEarliest = 0.0;             // самый ранний 4028 до события сценария (навык)
		int32 Phase = 0;                      // фаза сценария (навык по-настоящему, сосед)
		bool bFlagA = false, bFlagB = false;  // наблюдения сценария
		TArray<TPair<int32, uint64>> Wire;    // «провод» самотеста: опкод и 8 байт секрета в порядке ухода
		FCriticalSection WireLock;            // провод пишет и поток точной отправки
		FVector2D Home = FVector2D::ZeroVector;
		int32 SavedWeapon = 0, SavedEquipped = 99;
		bool bWasInGame = false;
		TUniquePtr<FAsdaNetSelfScope> SelfScope;
		double ProbeS = 0.0;        // момент события сценария (клик, навык)
		int32 ProbeIdx = -1;        // индекс перехвата на момент события
		bool bProbeDone = false;
		bool bLoopAliveAfterSkill = true;

		UAsdaNetSubsystem* N() const { return Net.Get(); }
		AAsdaPlayerController* PC() const { UWorld* W = World.Get(); return W ? Cast<AAsdaPlayerController>(W->GetFirstPlayerController()) : nullptr; }
		AAsdaPlayerCharacter* Ch() const { AAsdaPlayerController* P = PC(); return P ? Cast<AAsdaPlayerCharacter>(P->GetPawn()) : nullptr; }
		UAsdaAttackRhythmSubsystem* Rh() const { UAsdaNetSubsystem* Nn = N(); return Nn && Nn->GetGameInstance() ? Nn->GetGameInstance()->GetSubsystem<UAsdaAttackRhythmSubsystem>() : nullptr; }
		UGameInstance* GI() const { UAsdaNetSubsystem* Nn = N(); return Nn ? Nn->GetGameInstance() : nullptr; }
		int32 Count(int32 Op, int32 From) const { int32 R = 0; for (int32 i = From; i < Caps.Num(); ++i) { if (Caps[i].Op == Op) { ++R; } } return R; }
		void Expect(bool bOk, const FString& What)
		{
			if (bOk) { ++Ok; } else { ++Fail; }
			UE_LOG(LogAsdaRhythmTest, Log, TEXT("САМОТЕСТ-РИТМ %s %s: %s"), bOk ? TEXT("OK") : TEXT("ПРОВАЛ"),
				Steps.IsValidIndex(StepIdx) ? *Steps[StepIdx].Name : TEXT("?"), *What);
		}
	};

	// Перехват исходящих самотеста (4026/4028/5256): время, требование часов, позиция и точки замаха.
	static uint32 ArmSelfCapture(const TSharedPtr<FState>& S)
	{
		TWeakPtr<FState> WS = S;
		return AsdaSelfTestKit::ArmCapture(S->N(), [WS](uint16 Op, const uint8*, int32, bool, TCHAR)
		{
			TSharedPtr<FState> St = WS.Pin();
			if (!St || (Op != 4026 && Op != 4028 && Op != 5256)) { return; }
			FCap Cp; Cp.Op = Op; Cp.T = FPlatformTime::Seconds();
			// Удар, ушедший с потока точной отправки, перехватчик видит в следующем кадре — время берём фактическое.
			if (Op == 4028 && St->N() && St->N()->LastTimedSendAtS > 0.0) { Cp.T = St->N()->LastTimedSendAtS; }
			St->NoteFrame();
			Cp.FrameS = FApp::GetDeltaTime();
			Cp.LateFrameS = FMath::Max((float)FApp::GetDeltaTime(), St->RecentFrameMaxS());   // ★ R5-fix6: наибольший из трёх последних кадров
			if (const UAsdaAttackRhythmSubsystem* R = St->Rh()) { Cp.PrevS = R->GetLastAcceptS(); Cp.ReqMs = R->GetRequiredMs(); }
			if (const AAsdaPlayerCharacter* Cc = St->Ch())
			{
				Cp.ClipPos = Cc->GetAttackClipPositionSec();
				Cp.Rate = Cc->GetAttackSyncRate();
				Cp.Rewind = Cc->GetAttackLoopRewindSec();
				Cp.bEvened = Cc->IsAttackSegmentEvened();
				Cp.PlannedMs = Cc->GetAttackSegmentPlannedSec() * 1000.f;
				Cp.Pts = Cc->GetAttackSendPoints(Cc->IsAttackFirstPass());
				Cp.ClipLen = Cp.Pts.Num() > 0 ? Cp.Pts.Last() : 0.f;
				// Длина клипа: последняя точка не всегда конец прохода (ближний бой) — берём из точек первого прохода и цикла.
				for (const float P : Cc->GetAttackSendPoints(true)) { Cp.ClipLen = FMath::Max(Cp.ClipLen, P); }
			}
			St->Caps.Add(MoveTemp(Cp));
		}, (uint16)0, TEXT("asda.Combat.SelfTestRhythm"));
	}

	// Моб-заглушка (как у самотеста 4007): признаки сетевого моба до BeginPlay, высота персонажа.
	static AAsdaMobActor* SpawnMob(UWorld* W, const FVector2D& G, float Z, int32 Rid)
	{
		if (!W) { return nullptr; }
		FActorSpawnParameters SP;
		SP.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
		SP.bDeferConstruction = true;
		AAsdaMobActor* M = W->SpawnActor<AAsdaMobActor>(AAsdaMobActor::StaticClass(), SP);
		if (!M) { return nullptr; }
		M->bNetSpawned = true; M->NetRenderId = Rid; M->CurHp = 100000; M->MaxHp = 100000;
		const FVector2D Ue = AsdaCoord::GameToUe2D(G);
		M->FinishSpawning(FTransform(FVector(Ue.X, Ue.Y, Z)));
		M->SetActorLocation(FVector(Ue.X, Ue.Y, Z));
		return M;
	}

	// Первое оружие данного подтипа в таблице предметов (тип 9, не удочка). 0 — нет.
	static int32 FindWeapon(const UAsdaItemTableSubsystem* IT, int32 Wt)
	{
		if (!IT) { return 0; }
		for (int32 Code = 20000; Code < 80000; ++Code)
		{
			const FAsdaItemRecord* R = IT->GetItem(Code);
			if (R && R->ItemType == 9 && R->ItemSubType == Wt && R->ItemVariant != 1 && R->AttackRangeTiles > 0) { return Code; }
		}
		return 0;
	}

	// Навык, у которого состояние StateId лежит в буфф-слоте и величина при уровне 1 ненулевая. −1 — нет.
	static int32 FindStateSkill(const UAsdaSkillDatabase* Db, int32 StateId)
	{
		if (!Db) { return -1; }
		for (int32 Code = 0; Code < 1300; ++Code)
		{
			const FAsdaSkillDef* D = Db->Get(Code);
			if (!D) { continue; }
			const int32 Slot = D->BuffSlotOfState(StateId);
			if (Slot == INDEX_NONE) { continue; }
			int32 B = 0, P = 0; D->BuffSlotScale(Slot, B, P);
			if (B + P != 0 && D->MaxLevel >= 1) { return Code; }
		}
		return -1;
	}

	// Расстояние фазы клипа до ближайшей точки отправки (по кругу длины клипа: конец прохода ≡ 0/перемотка).
	static float PhaseErr(const FCap& C, float Rewind)
	{
		float Best = 1e9f;
		for (const float P : C.Pts)
		{
			float D = FMath::Abs(C.ClipPos - P);
			if (P >= C.ClipLen - 1e-3f)   // точка «конец прохода»: удар уходит сразу после перезапуска клипа (позиция 0 или перемотка)
			{
				D = FMath::Min(D, FMath::Min(FMath::Abs(C.ClipPos), FMath::Abs(C.ClipPos - Rewind)));
			}
			Best = FMath::Min(Best, D);
		}
		return Best;
	}
}

struct FAsdaAttackRhythmSelfTest
{
	static void Run(const TArray<FString>& Args, UWorld* W);
};

void FAsdaAttackRhythmSelfTest::Run(const TArray<FString>& Args, UWorld* W)
{
	using namespace AsdaRhythmTestLocal;
	const FAsdaSelfTestArgs KitArgs = FAsdaSelfTestArgs::Parse(Args);
	static const uint16 kGuardOps[] = { 4026, 4028 };
	if (AsdaSelfTestKit::RefuseStart(W, TEXT("asda.Combat.SelfTestRhythm"), KitArgs, 0.f, MakeArrayView(kGuardOps))) { return; }
	UGameInstance* GIw = W ? W->GetGameInstance() : nullptr;
	UAsdaNetSubsystem* Net = GIw ? GIw->GetSubsystem<UAsdaNetSubsystem>() : nullptr;
	if (!Net) { UE_LOG(LogAsdaRhythmTest, Warning, TEXT("САМОТЕСТ-РИТМ: нет подсистемы сети")); return; }

	TSharedRef<FState> S = MakeShared<FState>();
	S->Net = Net; S->World = W; S->bQuit = KitArgs.bQuit; S->Gate.bQuit = KitArgs.bQuit;

	auto Add = [S](const FString& Name, float MinSec, float Timeout, TFunction<void()> Start, TFunction<bool()> Poll, TFunction<void()> Check)
	{
		FStep St; St.Name = Name; St.MinSec = MinSec; St.TimeoutSec = Timeout;
		St.Start = MoveTemp(Start); St.Poll = MoveTemp(Poll); St.Check = MoveTemp(Check);
		S->Steps.Add(MoveTemp(St));
	};

	// Сброс боя между сценариями: цикл, моб, часы, состояния.
	auto ResetCombat = [S]()
	{
		AAsdaPlayerController* PC = S->PC(); AAsdaPlayerCharacter* C = S->Ch();
		if (PC)
		{
			PC->bAutoAttack = false; PC->bCombatStarted = false; PC->bInCombatStance = false; PC->bWaitingCombatAck = false;
			PC->NextAttackTickTime = 0.0; PC->TargetActor = nullptr;
			PC->GetWorldTimerManager().ClearTimer(PC->DeferredAttackSendTimer);
		}
		if (C) { C->StopAttackAnim(); C->AttackFaceTarget = nullptr; C->NetSnap(S->Home); }
		if (AAsdaMobActor* M = S->Mob.Get()) { M->Destroy(); }
		S->Mob = nullptr;
		if (AAsdaMobActor* M2 = S->Mob2.Get()) { M2->Destroy(); }
		S->Mob2 = nullptr;
		if (UAsdaAttackRhythmSubsystem* R = S->Rh()) { R->DebugReset(); R->TestWtOverride = -1; R->TestRangeOverride = -1; }
		if (UGameInstance* G = S->GI())
		{
			if (UAsdaEffectsSubsystem* Fx = G->GetSubsystem<UAsdaEffectsSubsystem>())
			{
				const int32 U = Fx->SelfUserIdx();
				for (const int32 Id : { 27, 28, 52, 12, 162, 206, 174, 233, 247 }) { Fx->RemoveBySkillState(U, Id, 0); }
			}
		}
		S->Mark = S->Caps.Num(); S->ProbeS = 0.0; S->ProbeIdx = -1; S->bProbeDone = false; S->bLoopAliveAfterSkill = true;
		S->OldEarliest = 0.0; S->Phase = 0; S->bFlagA = false; S->bFlagB = false;
		if (UGameInstance* G2 = S->GI()) { if (UAsdaSkillUseSubsystem* SU = G2->GetSubsystem<UAsdaSkillUseSubsystem>()) { SU->DebugSetCastState(0, -1); } }
	};
	// Надеть оружие подтипа Wt (0 — без оружия) и начать бой по мобу рядом (второй клик = `EngageMobFromClick`).
	auto Engage = [S, ResetCombat](int32 Wt)
	{
		ResetCombat();
		UAsdaNetSubsystem* N = S->N(); AAsdaPlayerController* PC = S->PC(); AAsdaPlayerCharacter* C = S->Ch();
		UGameInstance* G = S->GI();
		if (!N || !PC || !C || !G) { return; }
		const UAsdaItemTableSubsystem* IT = G->GetSubsystem<UAsdaItemTableSubsystem>();
		N->SelfWeaponItemId = (Wt > 0) ? FindWeapon(IT, Wt) : 0;
		C->EquippedWeaponSubType = (Wt > 0) ? Wt : 99;
		const FVector UeHome = C->GetActorLocation();
		AAsdaMobActor* M = SpawnMob(S->World.Get(), S->Home + FVector2D(60.f, 0.f), (float)UeHome.Z, 5151);
		S->Mob = M;
		PC->TargetActor = M;
		PC->EngageMobFromClick(M);   // штатный путь «второго клика»: бой, 4026 по прибытии (рядом — сразу)
	};
	auto Need = [S](int32 N4028) { return [S, N4028]() { return S->Count(4028, S->Mark) >= N4028; }; };

	// Проверка интервалов и фазы замаха текущего сценария. MaxOver — допуск сверху (доля), FirstReq — ждём базу.
	auto CheckIntervals = [S](const FString& Tag, double MaxOver, int32 ExpectReqFirst)
	{
		int32 N28 = 0, Early = 0, Late = 0, LateStall = 0, PhaseBad = 0;
		int32 Evened = 0, EvenedOff = 0;   // ★ mobpos_swing: первый проход темпом цикла (позже минимума — по плану замаха)
		double WorstEarly = 0.0, WorstLate = 0.0; float WorstPhase = 0.f;
		FString List;
		const AAsdaPlayerCharacter* C = S->Ch();
		for (int32 i = S->Mark; i < S->Caps.Num(); ++i)
		{
			const FCap& Cp = S->Caps[i];
			if (Cp.Op != 4028) { continue; }
			++N28;
			const double Gap = (Cp.T - Cp.PrevS) * 1000.0;
			const double Req = (double)Cp.ReqMs;
			// «Не раньше» — против серверной длительности И с запасом отправки (ревизия СРЕДН-4).
			const double Need = Req + UAsdaAttackRhythmSubsystem::MarginMsFor(Cp.ReqMs);
			if (Req > 0.0 && Gap < Need - 0.5) { ++Early; WorstEarly = FMath::Max(WorstEarly, Need - Gap); }
			bool bStall = false;
			for (const TPair<double, double>& St : S->Stalls) { if (St.Value > Cp.PrevS && St.Key < Cp.T) { bStall = true; break; } }
			// ★ mobpos_swing: отрезок первого прохода шёл темпом цикла (ровный темп серии, как оригинал) — удар законно позже
			//   минимума сервера; сверяем с планом замаха (клип-расстояние / скорость) с тем же допуском.
			if (Cp.bEvened)
			{
				++Evened;
				if (Gap > (double)Cp.PlannedMs * (1.0 + MaxOver) + 30.0 && !bStall) { ++EvenedOff; WorstLate = FMath::Max(WorstLate, Gap / FMath::Max(1.0, (double)Cp.PlannedMs) - 1.0); }
			}
			// ★ R5-fix6 (13.11.2026) [ИСПРАВЛЕНИЕ ТЕСТА]: удар уходит на первом тике игры после срока — опоздание не больше длины
			//   кадра, в котором он ушёл. Допуск «+5 %» (36…72 мс) писался под кадр 1/60 с; при кадре 60–100 мс (три игры разом)
			//   честный удар выходил «позже на 5,2–6,8 %», а исключение было только для рывков > 100 мс. Граница — большее из прежней
			//   и «длительность + запас отправки + длина кадра + 3 мс». При кадре 1/60 с граница прежняя.
			//   ★ Ревизия: к прежней границе прибавляется только длина кадра СВЕРХ 1/60 с — при ровном кадре граница строго прежняя.
			else if (Req > 0.0 && Gap > Req * (1.0 + MaxOver) + FMath::Max(0.0, (double)Cp.LateFrameS - 1.0 / 60.0) * 1000.0)
			{
				if (bStall) { ++LateStall; } else { ++Late; WorstLate = FMath::Max(WorstLate, Gap / Req - 1.0); }
			}
			const float Ph = PhaseErr(Cp, Cp.Rewind);
			// Фаза: допуск — один кадр рендера на скорости отрезка (клип-время) плюс запас.
			// Допуск фазы (ревизия НИЗК-7): точка замаха приходит за кадр до дедлайна, перехват точного удара — в кадре после ухода:
			//   до двух кадров клип-времени на скорости отрезка, плюс 20 мс.
			bool bStallNear = false;   // рывок рядом с ударом сдвигает позицию клипа к моменту перехвата
			for (const TPair<double, double>& St : S->Stalls) { if (St.Value > Cp.T - 0.1 && St.Key < Cp.T + 0.2) { bStallNear = true; break; } }
			if (!bStallNear && Ph > (2.f * FMath::Max(0.004f, Cp.FrameS)) * FMath::Max(1.f, Cp.Rate) + 0.02f) { ++PhaseBad; WorstPhase = FMath::Max(WorstPhase, Ph); }
			if (List.Len() < 400) { List += FString::Printf(TEXT(" %.0f/%d"), Gap, Cp.ReqMs); }
		}
		S->Expect(N28 > 0, FString::Printf(TEXT("%s: ушло 4028: %d"), *Tag, N28));
		S->Expect(Early == 0, FString::Printf(TEXT("%s: ни один 4028 не раньше серверной длительности + запас (раньше: %d, худший на %.1f мс); интервал/нужно:%s"),
			*Tag, Early, WorstEarly, *List));
		S->Expect(Late == 0, FString::Printf(TEXT("%s: ни один 4028 не позже длительности + %.0f %% (позже: %d, худший +%.1f %%; позже из-за рывка игрового потока > 100 мс: %d)"),
			*Tag, MaxOver * 100.0, Late, WorstLate * 100.0, LateStall));
		S->Expect(PhaseBad == 0, FString::Printf(TEXT("%s: удар уходит в эталонной точке клипа (мимо: %d, худший %.3f с)"), *Tag, PhaseBad, WorstPhase));
		if (Evened > 0)
		{
			S->Expect(EvenedOff == 0, FString::Printf(TEXT("%s: удары первого прохода темпом цикла (%d) ушли по плану замаха +%.0f %% (мимо: %d)"),
				*Tag, Evened, MaxOver * 100.0, EvenedOff));
		}
		if (ExpectReqFirst > 0)
		{
			int32 First = -1;
			for (int32 i = S->Mark; i < S->Caps.Num(); ++i) { if (S->Caps[i].Op == 4028) { First = S->Caps[i].ReqMs; break; } }
			S->Expect(First == ExpectReqFirst, FString::Printf(TEXT("%s: требование к первому 4028 = %d мс (ждём %d)"), *Tag, First, ExpectReqFirst));
		}
		(void)C;
	};

	// ★ mobpos_swing (задача 2): РОВНОСТЬ ТЕМПА СЕРИИ — скорости клипа всех отрезков серии (с «взвода») отличаются не больше
	//   чем на MaxRatio (max/min). Возвращает отношение (для контрольной пары).
	auto CheckEvenTempo = [S](const FString& Tag, double MaxRatio, bool bExpectEven) -> double
	{
		const AAsdaPlayerCharacter* C = S->Ch();
		const TArray<float> R = C ? C->GetAttackSegmentRates() : TArray<float>();
		float Mn = TNumericLimits<float>::Max(), Mx = 0.f; FString List;
		for (const float X : R) { Mn = FMath::Min(Mn, X); Mx = FMath::Max(Mx, X); if (List.Len() < 200) { List += FString::Printf(TEXT(" %.3f"), X); } }
		const double Ratio = (R.Num() > 0 && Mn > 0.f) ? (double)Mx / (double)Mn : 0.0;
		if (bExpectEven)
		{
			S->Expect(R.Num() >= 3 && Ratio <= MaxRatio, FString::Printf(TEXT("%s: темп замаха ровный — скорость клипа по отрезкам серии max/min = %.3f ≤ %.2f (отрезков %d):%s"),
				*Tag, Ratio, MaxRatio, R.Num(), *List));
		}
		return Ratio;
	};

	// ─────────────────────── 0. Таблицы и формула сервера (без мира) ───────────────────────
	Add(TEXT("таблицы Combo.lst/Skill.lst и формула"), 0.f, 1.f, [S]()
	{
		using R = UAsdaAttackRhythmSubsystem;
		struct FRow { int32 Wt; int32 B0, B1, B2, N; };
		const FRow Rows[] = { {0, 900, 900, 0, 2}, {1, 900, 900, 900, 3}, {2, 900, 900, 900, 3}, {3, 1620, 1620, 1620, 3},
		                      {4, 0, 0, 0, 0}, {5, 1230, 1290, 0, 2}, {8, 1440, 0, 0, 1}, {9, 1080, 0, 0, 1}, {10, 1440, 0, 0, 1}, {11, 900, 0, 0, 1} };
		bool bAll = true; FString Bad;
		for (const FRow& Rw : Rows)
		{
			const bool bOk = R::ComboBaseMs(Rw.Wt, 0) == Rw.B0 && R::ComboBaseMs(Rw.Wt, 1) == Rw.B1 && R::ComboBaseMs(Rw.Wt, 2) == Rw.B2
				&& R::ComboStepCount(Rw.Wt) == Rw.N && R::ComboBaseMs(Rw.Wt, 0, 1) == Rw.B0;
			if (!bOk) { bAll = false; Bad += FString::Printf(TEXT(" wt%d"), Rw.Wt); }
		}
		S->Expect(bAll, FString::Printf(TEXT("Combo.lst: база и число шагов по типам оружия, строки 0 и 1 (сверка с дампом сервера)%s"), *Bad));
		S->Expect(R::SkillLockMs(305) == 1000 && R::SkillLockMs(333) == 3800 && R::SkillLockMs(510) == 1500 && R::SkillLockMs(548) == 2000
			&& R::SkillLockMs(405) == 0 && R::SkillLockMs(599) == 0 && R::SkillLockMs(800) == 0 && R::SkillLockMs(1210) == 0,
			TEXT("Skill.lst: 305→1000, 333→3800, 510→1500, 548→2000; 405/599/800/1210 → 0"));
		// Формула `sub_45F3A0` (ручной счёт): 1440·1440/(1440·20/100+1440)=1200; 1620²/(1620−486)=2314.28→2314; 1230·10%: 1118.18→1118.
		S->Expect(R::ServerFormulaMs(900, 0, false, false, 1) == 900, TEXT("формула: 900, B=0 → 900"));
		S->Expect(R::ServerFormulaMs(1440, 20, false, false, 15) == 1200, FString::Printf(TEXT("формула: 1440, B=+20 → 1200 (%d)"), R::ServerFormulaMs(1440, 20, false, false, 15)));
		S->Expect(R::ServerFormulaMs(1620, -30, false, false, 2) == 2314, FString::Printf(TEXT("формула: 1620, B=−30 → 2314 (%d)"), R::ServerFormulaMs(1620, -30, false, false, 2)));
		S->Expect(R::ServerFormulaMs(1230, 10, false, false, 1) == 1118, FString::Printf(TEXT("формула: 1230, B=+10 → 1118 (%d)"), R::ServerFormulaMs(1230, 10, false, false, 1)));
		S->Expect(R::ServerFormulaMs(900, 0, true, false, 1) == 450 && R::ServerFormulaMs(900, 0, true, false, 15) == 900,
			TEXT("×0.5 состояния 162 — только при дальности оружия ≤ 3 (ближний бой)"));
		S->Expect(R::ServerFormulaMs(1440, 0, false, true, 15) == 720 && R::ServerFormulaMs(1440, 0, false, true, 3) == 1440,
			TEXT("×0.5 состояния 206 — только при дальности > 3 (дальнобойное)"));
		S->Expect(R::ServerFormulaMs(1080, 20, false, true, 15) == 450, FString::Printf(TEXT("формула: 1080, B=+20, ×0.5 → 450 (%d)"), R::ServerFormulaMs(1080, 20, false, true, 15)));
		S->Expect(R::ServerFormulaMs(900, -100, false, false, 1) == 0, TEXT("B=−100: знаменатель 0 → ftol(∞) → 0 (проверки нет, как у сервера)"));
	}, []() { return true; }, []() {});

	// ─────────────────────── Подготовка: персонаж, перехват, «в мире» ───────────────────────
	Add(TEXT("подготовка"), 0.f, 1.f, [S, Net]()
	{
		AAsdaPlayerCharacter* C = S->Ch(); AAsdaPlayerController* PC = S->PC();
		if (!C || !PC) { S->Expect(false, TEXT("нет своего персонажа/контроллера после FakeSelfSpawn")); return; }
		S->Home = C->GetGamePosXZ();
		S->SavedWeapon = Net->SelfWeaponItemId;
		S->SavedEquipped = C->EquippedWeaponSubType;
		S->SelfScope = MakeUnique<FAsdaNetSelfScope>(Net, TEXT("asda.Combat.SelfTestRhythm"));
		if (!Net->bSelfUserNumKnown) { Net->SelfUserNum = 4321; Net->bSelfUserNumKnown = true; }   // свой номер — для списка состояний
		Net->bSelfDead = false;
		S->Gen = ArmSelfCapture(S);
		S->Expect(S->Gen != 0, TEXT("перехват исходящих взведён"));
		Net->bStandCaptureSkillUse = true;   // заявка навыка — настоящим отправителем под перехватом
		S->bWasInGame = Net->bInGameConnected;
		Net->bInGameConnected = true;
		AsdaCVarSave::SetForTest(TEXT("asda.Combat.ServerRhythm"), 1);
		AsdaCVarSave::SetForTest(TEXT("asda.Combat.RhythmFromAnim"), 1);
		AsdaCVarSave::SetForTest(TEXT("asda.Combat.AttackTempoItems"), 0);
		AsdaCVarSave::SetForTest(TEXT("asda.Combat.EvenSwingTempo"), 1);   // ★ mobpos_swing
		S->Expect(S->Rh() != nullptr, TEXT("подсистема серверных часов атаки есть"));
	}, []() { return true; }, []() {});

	// ─────────────────────── 1. Каждый тип оружия: интервалы, фаза ───────────────────────
	struct FW { int32 Wt; const TCHAR* Name; };
	const FW Weapons[] = { {1, TEXT("одноручный меч")}, {2, TEXT("копьё")}, {3, TEXT("двуручный меч")}, {5, TEXT("посох")},
	                       {9, TEXT("арбалет")}, {10, TEXT("лук")}, {11, TEXT("баллиста")}, {0, TEXT("без оружия")} };
	for (const FW& Wp : Weapons)
	{
		const int32 Wt = Wp.Wt;
		const FString Tag = FString::Printf(TEXT("wt=%d %s"), Wt, Wp.Name);
		Add(Tag, 0.f, 16.f, [S, Engage, Wt]()
		{
			Engage(Wt);
			if (Wt > 0) { S->Expect(S->N() && S->N()->SelfWeaponItemId > 0, FString::Printf(TEXT("оружие подтипа %d найдено: %d"), Wt, S->N() ? S->N()->SelfWeaponItemId : 0)); }
		}, Need(6), [S, CheckIntervals, CheckEvenTempo, Tag, Wt]()
		{
			S->Expect(S->Count(4026, S->Mark) == 1, FString::Printf(TEXT("%s: одна 4026 на серию (%d)"), *Tag, S->Count(4026, S->Mark)));
			CheckIntervals(Tag, 0.05, UAsdaAttackRhythmSubsystem::ComboBaseMs(Wt, 0));
			CheckEvenTempo(Tag, 1.20, true);   // ★ mobpos_swing: «пам-пам-пам», а не «пампам-пам»
		});
	}

	// ─────────────────────── 1б. Контроль ровного темпа: asda.Combat.EvenSwingTempo 0 — баллиста снова «пампам» ───────────
	Add(TEXT("контроль: EvenSwingTempo 0, баллиста"), 0.f, 12.f, [S, Engage]()
	{
		AsdaCVarSave::SetForTest(TEXT("asda.Combat.EvenSwingTempo"), 0);
		Engage(11);
	}, Need(3), [S, CheckEvenTempo]()
	{
		const double Ratio = CheckEvenTempo(TEXT("контроль"), 0.0, false);
		S->Expect(Ratio > 1.5, FString::Printf(TEXT("без ровного темпа первый отрезок баллисты снова сжат (max/min = %.2f > 1.5) — проверка не слепая"), Ratio));
		AsdaCVarSave::SetForTest(TEXT("asda.Combat.EvenSwingTempo"), 1);
	});

	// ─────────────────────── 2. Контроль: часы выключены — лук стреляет чаще сервера ───────────────────────
	Add(TEXT("контроль: ServerRhythm 0, лук"), 0.f, 12.f, [S, Engage]()
	{
		AsdaCVarSave::SetForTest(TEXT("asda.Combat.ServerRhythm"), 0);
		Engage(10);
	}, Need(4), [S]()
	{
		int32 Fast = 0; FString List;
		for (int32 i = S->Mark + 1; i < S->Caps.Num(); ++i)
		{
			if (S->Caps[i].Op != 4028 || S->Caps[i - 1].Op == 0) { continue; }
			const double Gap = (S->Caps[i].T - S->Caps[i - 1].T) * 1000.0;
			if (Gap < 1440.0 * 0.9) { ++Fast; }
			List += FString::Printf(TEXT(" %.0f"), Gap);
		}
		S->Expect(Fast > 0, FString::Printf(TEXT("без часов лук бьёт чаще 1440 мс (прежний дефект виден, проверка не слепая): быстрых %d, интервалы:%s"), Fast, *List));
		AsdaCVarSave::SetForTest(TEXT("asda.Combat.ServerRhythm"), 1);
	});

	// ─────────────────────── 3. Баффы скорости и ×0.5 ───────────────────────
	// Состояние своему: штатный конструктор записи (как 5272).
	auto AddState = [S](int32 StateId, int32 Stack) -> int32
	{
		UGameInstance* G = S->GI();
		UAsdaEffectsSubsystem* Fx = G ? G->GetSubsystem<UAsdaEffectsSubsystem>() : nullptr;
		const UAsdaSkillDatabase* Db = G ? G->GetSubsystem<UAsdaSkillDatabase>() : nullptr;
		if (!Fx) { return -1; }
		const int32 Src = FindStateSkill(Db, StateId);
		Fx->AddOrUpdateSkill(Fx->SelfUserIdx(), StateId, Src >= 0 ? Src : 0, Stack, 2, 600, 1);
		return Src;
	};
	auto SelfPct = [S](int32 StateId) -> int32
	{
		UGameInstance* G = S->GI();
		const UAsdaWorldSubsystem* WSs = G ? G->GetSubsystem<UAsdaWorldSubsystem>() : nullptr;
		const UAsdaEffectsSubsystem* Fx = G ? G->GetSubsystem<UAsdaEffectsSubsystem>() : nullptr;
		return (WSs && Fx) ? WSs->GetStateBonusPct(Fx->SelfUserIdx(), StateId) : 0;
	};
	Add(TEXT("бафф 27 (+скорость), лук"), 0.f, 14.f, [S, Engage, AddState]()
	{
		Engage(10);
		const int32 Src = AddState(27, 1);
		S->Expect(Src >= 0, FString::Printf(TEXT("навык-источник состояния 27 найден: %d"), Src));
	}, Need(5), [S, CheckIntervals, SelfPct]()
	{
		const int32 Pct = SelfPct(27);
		const int32 Want = UAsdaAttackRhythmSubsystem::ServerFormulaMs(1440, Pct, false, false, 15);
		S->Expect(Pct > 0 && Want < 1440, FString::Printf(TEXT("27: +%d %% → длительность %d мс (< 1440)"), Pct, Want));
		CheckIntervals(TEXT("бафф 27"), 0.05, Want);
	});
	Add(TEXT("дебафф 28 (−скорость, стек 2), лук"), 0.f, 18.f, [S, Engage, AddState]()
	{
		Engage(10);
		const int32 Src = AddState(28, 2);
		S->Expect(Src >= 0, FString::Printf(TEXT("навык-источник состояния 28 найден: %d"), Src));
	}, Need(4), [S, CheckIntervals, SelfPct]()
	{
		const int32 Pct = SelfPct(28);
		const int32 Want = UAsdaAttackRhythmSubsystem::ServerFormulaMs(1440, -Pct * 2, false, false, 15);
		S->Expect(Pct > 0 && Want > 1440, FString::Printf(TEXT("28: −%d %% ×2 стека (в сомнении медленнее) → %d мс (> 1440)"), Pct, Want));
		CheckIntervals(TEXT("дебафф 28"), 0.05, Want);
	});
	Add(TEXT("состояние 206 (×0.5 дальнобойным), арбалет"), 0.f, 12.f, [S, Engage, AddState]()
	{
		Engage(9);
		AddState(206, 1);
	}, Need(6), [S, CheckIntervals]()
	{
		CheckIntervals(TEXT("206 арбалет"), 0.05, 540);
	});
	Add(TEXT("состояние 162 (×0.5 ближним), одноручный меч"), 0.f, 12.f, [S, Engage, AddState]()
	{
		Engage(1);
		AddState(162, 1);
	}, Need(6), [S, CheckIntervals]()
	{
		CheckIntervals(TEXT("162 меч"), 0.05, 450);
	});
	Add(TEXT("контроль: состояние 162 на луке не действует"), 0.f, 12.f, [S, Engage, AddState]()
	{
		Engage(10);
		AddState(162, 1);
	}, Need(3), [S, CheckIntervals]()
	{
		CheckIntervals(TEXT("162 лук"), 0.05, 1440);
	});

	// ─────────────────────── 4. Повторный клик посреди серии ───────────────────────
	for (const int32 K : { 1, 0 })
	{
		Add(FString::Printf(TEXT("повторный клик по цели, ServerRhythm %d"), K), 0.f, 14.f, [S, Engage, K]()
		{
			AsdaCVarSave::SetForTest(TEXT("asda.Combat.ServerRhythm"), K);
			Engage(1);
		}, [S, K]()
		{
			// После второго 4028 — два клика по той же цели с паузой, затем ещё четыре удара.
			const int32 N28 = S->Count(4028, S->Mark);
			AAsdaPlayerController* PC = S->PC();
			if (!S->bProbeDone && N28 >= 2 && PC && S->Mob.IsValid())
			{
				S->bProbeDone = true; S->ProbeS = FPlatformTime::Seconds(); S->ProbeIdx = S->Caps.Num();
				PC->EngageMobFromClick(S->Mob.Get());
			}
			if (S->bProbeDone && S->ProbeIdx >= 0 && FPlatformTime::Seconds() - S->ProbeS > 0.35 && PC && S->Mob.IsValid() && S->ProbeIdx != -2)
			{
				PC->EngageMobFromClick(S->Mob.Get());
				S->ProbeIdx = -2;
			}
			// Часы выключены (контроль): прежний ритм — один удар на проход клипа (3 с), шесть ударов не дождаться;
			//   контролю нужны лишь оба клика и хотя бы три удара.
			return K ? (N28 >= 6) : (S->ProbeIdx == -2 && N28 >= 3);
		}, [S, CheckIntervals, K]()
		{
			const int32 N26 = S->Count(4026, S->Mark);
			if (K)
			{
				S->Expect(N26 == 1, FString::Printf(TEXT("часы вкл: повторный клик новой 4026 не дал (4026: %d)"), N26));
				CheckIntervals(TEXT("повторный клик"), 0.05, 900);
			}
			else
			{
				S->Expect(N26 >= 2, FString::Printf(TEXT("контроль, часы выкл: повторный клик даёт новую 4026, как прежде (4026: %d)"), N26));
			}
			AsdaCVarSave::SetForTest(TEXT("asda.Combat.ServerRhythm"), 1);
		});
	}

	// ─────────────────────── 5. Навык посреди автобоя ───────────────────────
	struct FSk { int32 Wt; int32 Code; };
	for (const FSk Sk : { FSk{1, 510}, FSk{10, 548}, FSk{1, 405} })
	{
		const int32 LockMs = UAsdaAttackRhythmSubsystem::SkillLockMs(Sk.Code);
		Add(FString::Printf(TEXT("навык %d (Skill.lst %d мс) посреди боя, wt=%d"), Sk.Code, LockMs, Sk.Wt), 0.f, 16.f, [S, Engage, Sk]()
		{
			Engage(Sk.Wt);
		}, [S, Sk]()
		{
			const int32 N28 = S->Count(4028, S->Mark);
			if (!S->bProbeDone && N28 >= 2)
			{
				S->bProbeDone = true; S->ProbeS = FPlatformTime::Seconds(); S->ProbeIdx = S->Caps.Num();
				if (UAsdaNetSubsystem* Nn = S->N()) { Nn->CancelPending4028(TEXT("самотест: навык")); }   // как отправитель 5256
				if (UAsdaAttackRhythmSubsystem* R = S->Rh()) { S->OldEarliest = R->EarliestNext4028S(); R->NoteSkillStart(Sk.Code); }
				const AAsdaPlayerCharacter* C = S->Ch();
				S->bLoopAliveAfterSkill = C && C->IsAttackLoopAlive();
			}
			return S->bProbeDone && S->Count(4028, S->ProbeIdx) >= 3;
		}, [S, CheckIntervals, LockMs]()
		{
			S->Expect(!S->bLoopAliveAfterSkill, TEXT("заявка навыка сняла цикл замаха (эталон: состояние 25)"));
			double FirstAfter = -1.0; int32 ReqAfter = -1;
			for (int32 i = S->ProbeIdx; i < S->Caps.Num(); ++i)
			{
				if (S->Caps[i].Op == 4028) { FirstAfter = (S->Caps[i].T - S->ProbeS) * 1000.0; ReqAfter = S->Caps[i].ReqMs; break; }
			}
			// Ревизия НИЗК-1: дедлайн после заявки = максимум(прежний, заявка + Skill.lst).
			const double NeedMs = FMath::Max((S->OldEarliest - S->ProbeS) * 1000.0, (double)LockMs);
			S->Expect(FirstAfter >= NeedMs - 0.5,
				FString::Printf(TEXT("первый 4028 после навыка через %.0f мс ≥ max(прежний дедлайн, Skill.lst %d) = %.0f (требование часов %d)"),
					FirstAfter, LockMs, NeedMs, ReqAfter));
			const AAsdaPlayerCharacter* C = S->Ch();
			S->Expect(C && C->IsAttackLoopAlive(), TEXT("замах возобновился после навыка"));
			// Интервалы всей серии (включая удар после навыка — его требование = Skill.lst; верхний предел к нему не применяется,
			//   если ждали дольше: возобновление ждёт показ навыка и запуск замаха).
			int32 Early = 0;
			for (int32 i = S->Mark; i < S->Caps.Num(); ++i)
			{
				const FCap& Cp = S->Caps[i];
				if (Cp.Op == 4028 && Cp.ReqMs > 0 && (Cp.T - Cp.PrevS) * 1000.0 < (double)Cp.ReqMs - 0.5) { ++Early; }
			}
			S->Expect(Early == 0, FString::Printf(TEXT("ни один 4028 серии с навыком не раньше часов (раньше: %d)"), Early));
		});
	}


	// ─────────────────────── 5б. Навык НАСТОЯЩИМ отправителем 5256 и гейт «навык занят» (ревизия НИЗК-7) ───────────────────────
	Add(TEXT("навык 510 через SendSkillUse5256, состояния 25 → 24 → 0"), 0.f, 18.f, [S, Engage]()
	{
		Engage(1);
	}, [S]()
	{
		const double Now = FPlatformTime::Seconds();
		UGameInstance* G = S->GI();
		UAsdaSkillUseSubsystem* SU = G ? G->GetSubsystem<UAsdaSkillUseSubsystem>() : nullptr;
		if (!SU || !S->N()) { return true; }
		if (S->Phase == 0 && S->Count(4028, S->Mark) >= 2)
		{
			S->Phase = 1; S->ProbeS = Now; S->ProbeIdx = S->Caps.Num();
			if (UAsdaAttackRhythmSubsystem* R = S->Rh()) { S->OldEarliest = R->EarliestNext4028S(); }
			SU->DebugSetCastState(25, 510);                                   // заявка ушла (эталон: состояние 25)
			S->N()->SendSkillUse5256(510, 0, 0, 0, 5151);                      // НАСТОЯЩИЙ отправитель (под перехватом)
			const AAsdaPlayerCharacter* C = S->Ch();
			S->bLoopAliveAfterSkill = C && C->IsAttackLoopAlive();
		}
		if (S->Phase == 1 && Now - S->ProbeS > 0.3) { S->Phase = 2; SU->DebugSetCastState(24, 510); }   // ответ 5257 — показ
		if (S->Phase == 2 && Now - S->ProbeS > 2.5) { S->Phase = 3; SU->DebugSetCastState(0, -1); }     // показ кончился
		return S->Phase == 3 && S->Count(4028, S->ProbeIdx) >= 2;
	}, [S]()
	{
		S->Expect(S->Count(5256, S->Mark) == 1, FString::Printf(TEXT("заявка 5256 ушла через перехватчик (%d)"), S->Count(5256, S->Mark)));
		S->Expect(!S->bLoopAliveAfterSkill, TEXT("настоящий отправитель 5256 снял цикл замаха"));
		double FirstAfter = -1.0;
		for (int32 i = S->ProbeIdx; i < S->Caps.Num(); ++i) { if (S->Caps[i].Op == 4028) { FirstAfter = (S->Caps[i].T - S->ProbeS) * 1000.0; break; } }
		S->Expect(FirstAfter >= 2500.0, FString::Printf(TEXT("пока навык занят (25/24, 2.5 с), 4028 нет: первый через %.0f мс"), FirstAfter));
		S->Expect(FirstAfter >= 1500.0, TEXT("и не раньше Skill.lst 1500"));
	});

	// ─────────────────────── 5в. Смена оружия, снятие баффа, смена цели посреди серии (ревизия СРЕДН-3, НИЗК-7) ───────────
	Add(TEXT("смена оружия посреди серии: одноручный → двуручный"), 0.f, 16.f, [S, Engage]()
	{
		Engage(1);
	}, [S]()
	{
		if (S->Phase == 0 && S->Count(4028, S->Mark) >= 2)
		{
			S->Phase = 1; S->ProbeIdx = S->Caps.Num();
			UGameInstance* G = S->GI();
			if (S->N() && G) { S->N()->SelfWeaponItemId = FindWeapon(G->GetSubsystem<UAsdaItemTableSubsystem>(), 3); }   // как ответ 5001
		}
		return S->Phase == 1 && S->Count(4028, S->ProbeIdx) >= 2;
	}, [S]()
	{
		const FCap* First = nullptr;
		for (int32 i = S->ProbeIdx; i < S->Caps.Num(); ++i) { if (S->Caps[i].Op == 4028) { First = &S->Caps[i]; break; } }
		const double Gap = First ? (First->T - First->PrevS) * 1000.0 : -1.0;
		S->Expect(First && First->ReqMs == 1620 && Gap >= 1620.0,
			FString::Printf(TEXT("после смены на двуручный требование к удару = max(900, 1620) = %d, интервал %.0f мс ≥ 1620"), First ? First->ReqMs : -1, Gap));
	});
	Add(TEXT("снятие ×0.5 (206) посреди серии, арбалет"), 0.f, 16.f, [S, Engage, AddState]()
	{
		Engage(9);
		AddState(206, 1);
	}, [S]()
	{
		if (S->Phase == 0 && S->Count(4028, S->Mark) >= 2)
		{
			S->Phase = 1; S->ProbeIdx = S->Caps.Num();
			if (UGameInstance* G = S->GI()) { if (UAsdaEffectsSubsystem* Fx = G->GetSubsystem<UAsdaEffectsSubsystem>()) { Fx->RemoveBySkillState(Fx->SelfUserIdx(), 206, 0); } }
		}
		return S->Phase == 1 && S->Count(4028, S->ProbeIdx) >= 2;
	}, [S]()
	{
		const FCap* First = nullptr;
		for (int32 i = S->ProbeIdx; i < S->Caps.Num(); ++i) { if (S->Caps[i].Op == 4028) { First = &S->Caps[i]; break; } }
		const double Gap = First ? (First->T - First->PrevS) * 1000.0 : -1.0;
		S->Expect(First && First->ReqMs == 1080 && Gap >= 1080.0,
			FString::Printf(TEXT("после снятия 206 требование = max(540, 1080) = %d, интервал %.0f мс ≥ 1080"), First ? First->ReqMs : -1, Gap));
	});
	Add(TEXT("смена цели посреди серии"), 0.f, 16.f, [S, Engage]()
	{
		Engage(1);
	}, [S]()
	{
		AAsdaPlayerController* PC = S->PC(); AAsdaPlayerCharacter* C = S->Ch();
		if (S->Phase == 0 && S->Count(4028, S->Mark) >= 2 && PC && C)
		{
			S->Phase = 1; S->ProbeIdx = S->Caps.Num();
			AAsdaMobActor* M2 = SpawnMob(S->World.Get(), S->Home + FVector2D(0.f, 60.f), (float)C->GetActorLocation().Z, 5152);
			S->Mob2 = M2;
			PC->SetTarget(M2);              // первый клик — выбор
			PC->EngageMobFromClick(M2);     // второй клик — бой по новой цели
		}
		return S->Phase == 1 && S->Count(4028, S->ProbeIdx) >= 3;
	}, [S, CheckIntervals]()
	{
		S->Expect(S->Count(4026, S->ProbeIdx) == 1, FString::Printf(TEXT("новая цель — новая серия: 4026 после смены %d"), S->Count(4026, S->ProbeIdx)));
		CheckIntervals(TEXT("смена цели"), 0.05, 900);
	});

	// ─────────────────────── 5г. Монотонность бонуса и максимум дедлайна навыка (ревизия СРЕДН-6, НИЗК-1) ───────────────────
	Add(TEXT("бонус B зажат снизу, навык не сокращает дедлайн"), 0.f, 2.f, [S, ResetCombat, AddState]()
	{
		ResetCombat();
		UAsdaAttackRhythmSubsystem* R = S->Rh();
		if (!R) { return; }
		R->TestWtOverride = 10; R->TestRangeOverride = 15;
		AddState(28, 99);   // переоценка замедления: стек 99
		const UAsdaAttackRhythmSubsystem::FDurParts P = R->ComputeDur(0);
		S->Expect(P.BonusPct == UAsdaAttackRhythmSubsystem::kMinBonusPct && P.DurMs > 1440 && P.DurMs == UAsdaAttackRhythmSubsystem::ServerFormulaMs(1440, -95, false, false, 15),
			FString::Printf(TEXT("B зажат до %d (не ≤ −100 — гейт не исчезает): длительность %d мс"), P.BonusPct, P.DurMs));
		if (UGameInstance* G = S->GI()) { if (UAsdaEffectsSubsystem* Fx = G->GetSubsystem<UAsdaEffectsSubsystem>()) { Fx->RemoveBySkillState(Fx->SelfUserIdx(), 28, 0); } }
		R->DebugReset(); R->TestWtOverride = 10; R->TestRangeOverride = 15;
		R->NoteSent4026();
		const double E0 = R->EarliestNext4028S();
		R->NoteSkillStart(405);    // Skill.lst 0
		const double E1 = R->EarliestNext4028S();
		R->NoteSkillStart(1210);   // код за таблицей
		const double E2 = R->EarliestNext4028S();
		R->NoteSkillStart(333);    // 3800 мс
		const double E3 = R->EarliestNext4028S();
		S->Expect(FMath::IsNearlyEqual(E0, E1, 1e-6) && FMath::IsNearlyEqual(E0, E2, 1e-6) && E3 > E0 + 2.0,
			FString::Printf(TEXT("дедлайн: после удара +%.0f мс; навык 405 (0) и 1210 (вне таблицы) не сокращают; 333 (3800) продлевает до +%.0f мс"),
				(E0 - FPlatformTime::Seconds()) * 1000.0, (E3 - FPlatformTime::Seconds()) * 1000.0));
		R->DebugReset(); R->TestWtOverride = -1; R->TestRangeOverride = -1;
	}, []() { return true; }, []() {});

	// ─────────────────────── 5д. Сосед: выход из боевого покоя через 10 с, 4205 перевзводит (ревизия СРЕДН-1) ───────────────
	Add(TEXT("сосед: боевой покой 10 с, 4205 перевзводит"), 0.f, 22.f, [S, ResetCombat]()
	{
		ResetCombat();
		UGameInstance* G = S->GI();
		UAsdaWorldSubsystem* WSs = G ? G->GetSubsystem<UAsdaWorldSubsystem>() : nullptr;
		if (!WSs) { return; }
		WSs->DebugSpawnFakePeer(7701, TEXT("СоседРитм"), S->Home + FVector2D(0.f, -300.f));
		if (AAsdaPlayerCharacter* P = Cast<AAsdaPlayerCharacter>(WSs->GetPlayerActorByUserNum(7701))) { P->RearmCombatStance(); }
		S->ProbeS = FPlatformTime::Seconds(); S->Phase = 0;
	}, [S]()
	{
		UGameInstance* G = S->GI();
		UAsdaWorldSubsystem* WSs = G ? G->GetSubsystem<UAsdaWorldSubsystem>() : nullptr;
		AAsdaPlayerCharacter* P = WSs ? Cast<AAsdaPlayerCharacter>(WSs->GetPlayerActorByUserNum(7701)) : nullptr;
		if (!P || !S->N()) { return true; }
		const double El = FPlatformTime::Seconds() - S->ProbeS;
		if (S->Phase == 0 && El > 6.0)
		{
			S->Phase = 1;
			TArray<uint8> B; B.SetNumZeroed(2); const int16 U = 7701; FMemory::Memcpy(B.GetData(), &U, 2);
			S->N()->DebugFeedPacket(4205, B);   // штатный приём 4205 по ЧУЖОМУ userIdx
		}
		if (S->Phase == 1 && El > 10.6) { S->Phase = 2; S->bFlagA = P->IsCombatStance(); }   // без 4205 стойка снялась бы в 10.0
		if (S->Phase == 2 && El > 16.6) { S->Phase = 3; S->bFlagB = P->IsCombatStance(); }
		return S->Phase == 3;
	}, [S]()
	{
		S->Expect(S->bFlagA, TEXT("4205 по соседу перевзвёл боевой покой (на 10.6 с стойка ещё стоит)"));
		S->Expect(!S->bFlagB, TEXT("через 10 с после 4205 сосед вышел в мирный покой (стойка не вечная)"));
	});


	// ─────────────────────── 7. Анти-реплей-номер в порядке ухода (ревизия сети, БЛОКЕР-1) ───────────────────────
	//   Удар 4028 ставится к дедлайну (+30 мс), сразу за ним игровой поток шлёт защищённый 5416 — он уходит РАНЬШЕ удара.
	//   Номера на «проводе» (провод самотеста у соединения, под его замком) обязаны строго расти в порядке ухода. Контрольная
	//   пара: `asda.Net.SecretAtSend 0` (номер при сборке, как прежде) — удар уходит с МЕНЬШИМ номером, чем пакет перед ним.
	for (const int32 K : { 1, 0 })
	{
		Add(FString::Printf(TEXT("анти-реплей-номер в порядке ухода, SecretAtSend %d"), K), 0.f, 10.f, [S, ResetCombat, K]()
		{
			ResetCombat();
			UAsdaNetSubsystem* N = S->N();
			if (!N) { return; }
			AsdaCVarSave::SetForTest(TEXT("asda.Net.SecretAtSend"), K);
			AsdaSelfTestKit::DisarmCapture(N, S->Gen); S->Gen = 0;   // без перехватчика: путь живой отправки
			S->Wire.Reset();
			TWeakPtr<FState> WS = S;
			S->bFlagA = N->DebugAttachTestWire([WS](uint16 Op, const uint8* B, int32 Len)
			{
				TSharedPtr<FState> St = WS.Pin();
				if (!St || !B || Len < 8) { return; }
				uint64 Tick = 0; FMemory::Memcpy(&Tick, B, 8);
				FScopeLock L(&St->WireLock);
				St->Wire.Add(TPair<int32, uint64>(Op, Tick));
			});
			S->Phase = 0;
		}, [S]()
		{
			UAsdaNetSubsystem* N = S->N();
			if (!N || !S->bFlagA) { return true; }
			if (N->HasPending4028()) { return false; }
			if (S->Phase >= 10) { return true; }
			if (UAsdaAttackRhythmSubsystem* R = S->Rh()) { R->DebugReset(); }
			N->SendAttackNext4028At(5151, 0.f, 0.f, FPlatformTime::Seconds() + 0.03);   // удар — к дедлайну
			N->SendAgainUserInfo((uint16)(100 + S->Phase));                               // защищённый пакет в окне ожидания
			++S->Phase;
			return false;
		}, [S, K]()
		{
			UAsdaNetSubsystem* N = S->N();
			TArray<TPair<int32, uint64>> W;
			{ FScopeLock L(&S->WireLock); W = S->Wire; }
			if (N) { N->DebugDetachTestWire(); }
			S->Gen = N ? ArmSelfCapture(S) : 0;   // перехват обратно (итог его снимет)
			S->Expect(S->bFlagA, TEXT("провод самотеста подключён"));
			// Номер из секрета: позиции bitPosTable младших 23 бит (выше — шум QPC), минус база 0x57A7A5.
			static const int32 kBitPos[23] = { 3, 4, 8, 11, 12, 14, 15, 16, 18, 20, 21, 25, 26, 27, 28, 30, 32, 33, 36, 42, 43, 45, 47 };
			auto Decode = [](uint64 T) { int64 V = 0; for (int32 i = 0; i < 23; ++i) { if (T & ((uint64)1 << kBitPos[i])) { V |= (int64)1 << i; } } return V - 0x57A7A5; };
			int32 Down = 0, After = 0, N28 = 0; FString List;
			for (int32 i = 0; i < W.Num(); ++i)
			{
				const int64 C = Decode(W[i].Value);
				if (W[i].Key == 4028) { ++N28; if (i > 0 && W[i - 1].Key == 5416) { ++After; } }
				if (i > 0 && C <= Decode(W[i - 1].Value)) { ++Down; }
				if (List.Len() < 300) { List += FString::Printf(TEXT(" %d:%lld"), W[i].Key, C); }
			}
			S->Expect(N28 == 10 && After == 10, FString::Printf(TEXT("каждый удар 4028 ушёл ПОСЛЕ защищённого 5416, поставленного позже (%d из %d)"), After, N28));
			if (K) { S->Expect(Down == 0, FString::Printf(TEXT("номера на проводе строго растут в порядке ухода (нарушений %d):%s"), Down, *List)); }
			else   { S->Expect(Down > 0, FString::Printf(TEXT("контроль, номер при сборке: удар уходит с меньшим номером (нарушений %d) — дефект виден"), Down)); }
			AsdaCVarSave::SetForTest(TEXT("asda.Net.SecretAtSend"), 1);
		});
	}

	// ─────────────────────── 8. 4205 по своему: вход в боевой покой (решение владельца) ───────────────────────
	for (const int32 K : { 1, 0 })
	{
		Add(FString::Printf(TEXT("4205 по своему персонажу, ServerRhythm %d"), K), 0.2f, 2.f, [S, ResetCombat, K]()
		{
			ResetCombat();
			AsdaCVarSave::SetForTest(TEXT("asda.Combat.ServerRhythm"), K);
			AAsdaPlayerCharacter* C = S->Ch(); AAsdaPlayerController* PC = S->PC(); UAsdaNetSubsystem* N = S->N();
			if (!C || !PC || !N) { return; }
			PC->ExitCombatStance();
			if (K == 0) { C->RearmCombatStance(); PC->CombatStanceUntil = FPlatformTime::Seconds() + 10.0; }   // контроль: из стойки
			TArray<uint8> B; B.SetNumZeroed(2); const int16 U = (int16)N->SelfUserNum; FMemory::Memcpy(B.GetData(), &U, 2);
			N->DebugFeedPacket(4205, B);
		}, []() { return true; }, [S, K]()
		{
			AAsdaPlayerCharacter* C = S->Ch(); AAsdaPlayerController* PC = S->PC();
			const double Left = PC ? PC->CombatStanceUntil - FPlatformTime::Seconds() : -1.0;
			if (K) { S->Expect(C && C->IsCombatStance() && Left > 9.0 && Left <= 10.0, FString::Printf(TEXT("4205: вход в боевой покой, до мирной стойки %.1f с (эталон 10)"), Left)); }
			else   { S->Expect(C && !C->IsCombatStance(), TEXT("контроль, ключ 0: прежнее поведение порта — 4205 выводит из стойки")); }
			AsdaCVarSave::SetForTest(TEXT("asda.Combat.ServerRhythm"), 1);
			if (PC) { PC->ExitCombatStance(); }
		});
	}

	// ─────────────────────── 6. Последний барьер: ранний прямой вызов отправителя ───────────────────────
	Add(TEXT("барьер отправителя 4028"), 0.f, 10.f, [S, Engage]()
	{
		Engage(10);
	}, [S]()
	{
		if (!S->bProbeDone && S->Count(4028, S->Mark) >= 1)
		{
			S->bProbeDone = true;
			const int32 Before = S->Caps.Num();
			UAsdaNetSubsystem* N = S->N();
			const bool bRet = N ? N->SendAttackNext4028(5151, 0.f, 0.f) : true;
			S->Expect(!bRet && S->Caps.Num() == Before, TEXT("4028 сразу после удара отправитель НЕ пропускает (возврат false, в провод ничего)"));
		}
		return S->bProbeDone;
	}, []() {});

	// ─────────────────────── Итог ───────────────────────
	Add(TEXT("итог"), 0.f, 1.f, [S, ResetCombat, Net]()
	{
		ResetCombat();
		// ★ 06.11.2026 (баг 7, изоляция пакета): сосед сценария 5д уходит штатным 4011, иначе остаётся в реестре мира следующих тестов.
		{ TArray<uint8> B; B.SetNumZeroed(2); const int16 U = 7701; FMemory::Memcpy(B.GetData(), &U, 2); Net->DebugFeedPacket(4011, B); }
		AsdaSelfTestKit::DisarmCapture(Net, S->Gen);
		Net->bStandCaptureSkillUse = false;
		Net->bInGameConnected = S->bWasInGame;
		Net->SelfWeaponItemId = S->SavedWeapon;
		if (AAsdaPlayerCharacter* C = S->Ch()) { C->EquippedWeaponSubType = S->SavedEquipped; }
		S->SelfScope.Reset();
		AsdaSelfTestKit::Finish(S->World.Get(), TEXT("asda.Combat.SelfTestRhythm"), false, LogAsdaRhythmTest, TEXT("САМОТЕСТ-РИТМ"),
			S->Ok, S->Fail, *FString::Printf(TEXT(" (перехвачено 4026: %d, 4028: %d)"), S->Count(4026, 0), S->Count(4028, 0)));
	}, []() { return true; }, []() {});

	// ─────────────────────── Спавн и исполнитель ───────────────────────
	TWeakObjectPtr<UWorld> WW(W);
	{
		FTimerHandle H;
		TWeakPtr<FState> WSg = S;
		W->GetTimerManager().SetTimer(H, FTimerDelegate::CreateWeakLambda(Net, [WW, WSg]()
		{
			UWorld* Wd = WW.Get();
			const TSharedPtr<FState> Sg = WSg.Pin();
			if (Wd && Sg && !Sg->Gate.Pass(Wd)) { return; }
			APlayerController* P = Wd ? Wd->GetFirstPlayerController() : nullptr;
			if (Wd && (!P || !P->GetPawn()) && GEngine)
			{
				GEngine->Exec(Wd, TEXT("Asda.Load.CurrentMap 3"));
				GEngine->Exec(Wd, TEXT("Asda.World.FakeSelfSpawn 256 256"));
			}
		}), FMath::Max(0.01f, KitArgs.Delay), false);
	}
	TWeakPtr<FState> WS = S;
	W->GetTimerManager().SetTimer(S->Tick, FTimerDelegate::CreateWeakLambda(Net, [WS, WW]()
	{
		TSharedPtr<FState> St = WS.Pin(); UWorld* Wd = WW.Get();
		if (!St || !Wd) { return; }
		if (!St->Gate.Pass(Wd)) { Wd->GetTimerManager().ClearTimer(St->Tick); return; }
		if (!St->Steps.IsValidIndex(St->StepIdx))
		{
			Wd->GetTimerManager().ClearTimer(St->Tick);
			AsdaSelfTestKit::Quit(Wd, St->bQuit);
			return;
		}
		const double Now = FPlatformTime::Seconds();
		if (St->LastTickS > 0.0 && Now - St->LastTickS > 0.1) { St->Stalls.Add(TPair<double, double>(St->LastTickS, Now)); }
		St->NoteFrame();   // ★ R5-fix6: длина кадра — в историю трёх последних
		St->LastTickS = Now;
		FStep& Stp = St->Steps[St->StepIdx];
		if (!St->bStepStarted)
		{
			St->bStepStarted = true; St->StepStartS = Now;
			UE_LOG(LogAsdaRhythmTest, Log, TEXT("САМОТЕСТ-РИТМ шаг %s"), *Stp.Name);
			if (Stp.Start) { Stp.Start(); }
			return;
		}
		const double El = Now - St->StepStartS;
		const bool bDone = El >= Stp.MinSec && (!Stp.Poll || Stp.Poll());
		if (!bDone && El < Stp.TimeoutSec) { return; }
		if (!bDone) { St->Expect(false, FString::Printf(TEXT("шаг не завершился за %.1f с (4026: %d, 4028: %d)"), Stp.TimeoutSec,
			St->Count(4026, St->Mark), St->Count(4028, St->Mark))); }
		if (Stp.Check) { Stp.Check(); }
		++St->StepIdx; St->bStepStarted = false;
	}), 0.01f, true, KitArgs.Delay + 2.5f);
}

static FAutoConsoleCommandWithWorldAndArgs GAsdaSelfTestAttackRhythm(
	TEXT("asda.Combat.SelfTestRhythm"),
	TEXT("Самотест серверного ритма обычной атаки: таблицы Combo.lst/Skill.lst и формула NpcServer; для меча, копья, двуручного, "
	     "посоха, арбалета, лука, баллисты и без оружия — 4028 не раньше серверной длительности и не позже +5 %, удар в эталонной "
	     "точке клипа; баффы 27/28, ×0.5 162/206; повторный клик; навык посреди боя; барьер отправителя; контрольные пары "
	     "asda.Combat.ServerRhythm 0. Только офлайн. `delay=N` `quit`."),
	FConsoleCommandWithWorldAndArgsDelegate::CreateLambda([](const TArray<FString>& A, UWorld* W)
	{
		AsdaCVarSave::ArmWorldGuard(W, TEXT("asda.Combat.SelfTestRhythm"));
		FAsdaAttackRhythmSelfTest::Run(A, W);
	}));

#endif // !UE_BUILD_SHIPPING
