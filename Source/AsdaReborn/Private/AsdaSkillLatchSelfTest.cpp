// Copyright (c) 2026 @sitxovski (Discord)
// ★★★ САМОТЕСТ «НАВЫКИ НЕ ЗАЛИПАЮТ» (31.10.2026, баг 7 игрока Player55: «после очень активного использования навыки перестают
//   работать вообще, помогает только перезапуск»). Команда `asda.Skill.SelfTestLatch`. [ПОРТ] Только офлайн (`-AsdaNoNet`):
//   входящие — штатный приём `DebugFeedPacket` (= `HandlePacket` сокета), исходящие — перехват `AsdaSelfTestKit::ArmCapture`.
//   Свой персонаж — `Asda.World.FakeSelfSpawn`, моб — запись 4013. Разбор — `UAsdaSkillUseSubsystem` (ключ `asda.Skill.LatchGuard`).
//   Шаги (заявка — путём игрока `SetTarget` + `DispatchByKind`, как ряд быстрого доступа и ряд Хранителя):
//     A) ряд Хранителя 1252 → 6158 ушёл → ОТКАЗ 6159 (код 16): состояние 25 снято сразу, следующий навык 705 уходит;
//     B) контроль `asda.Skill.LatchGuard 0`: тот же отказ — состояние 25 держится, 705 НЕ уходит (дефект воспроизведён);
//        ключ обратно 1 — предохранитель снимает защёлку ≤ 2 с, 705 уходит;
//     C) отказ 6161 без 6159 — состояние снято сразу (не через 2-секундный сторож);
//     D) 5270 без 5271 (потерян ответ, навык 787, каст 1200 мс): 14 держится время каста, затем снимается предохранителем
//        (время каста + 2 с); контроль ключа 0 — 14 вечное;
//     E) 5271 раньше 5270 (переупорядочение) — 14 снимается предохранителем;
//     F) 25 без активного навыка (навык соулмейта 5231): ответ 5232 снимает сразу; без ответа — предохранитель 2 с;
//     H) поздний 5271 СТАРОГО навыка не снимает новую заявку (ревизия п.1);
//     I) поздний отказ 5257 по другому навыку не гасит показ и заявку текущего каста (п.3);
//     J) серия молчаливых отказов (гейт 12) — одно Warning в журнал (п.5);
//     K) навык соулмейта 5231 путём лотка: гейт состояния 14 — молча, без заявки; неушедшая заявка 25 не ставит (п.2);
//     G) нагрузка: сотни нажатий 705/1252 со случайными ответами сервера (успех, отказ, потеря, дубль, 6161 до 6159,
//        смерть цели и новый моб) — после тишины навык уходит.
#include "CoreMinimal.h"

#if !UE_BUILD_SHIPPING

#include "AsdaNetSubsystem.h"
#include "AsdaBytes.h"
#include "AsdaCVarSave.h"
#include "Test/AsdaSelfTestRunGate.h"   // ★ R3: FAsdaSelfTestRunGate — узкий заголовок вместо AsdaOutgoingGate.h
#include "AsdaTestPackets.h"
#include "Test/AsdaSelfTestKit.h"
#include "UI/AsdaSkillDatabase.h"
#include "UI/AsdaSkillUseSubsystem.h"
#include "UI/AsdaSoulmateSkillSubsystem.h"
#include "Skill/AsdaSkillFxSubsystem.h"
#include "World/AsdaWorldSubsystem.h"
#include "World/AsdaPlayerController.h"
#include "World/AsdaPlayerCharacter.h"
#include "World/AsdaCoord.h"
#include "Npc/AsdaMobActor.h"
#include "Engine/Engine.h"
#include "Engine/GameInstance.h"
#include "Engine/World.h"
#include "GameFramework/PlayerController.h"
#include "HAL/IConsoleManager.h"
#include "Math/RandomStream.h"
#include "TimerManager.h"

DEFINE_LOG_CATEGORY_STATIC(LogAsdaSkillLatchTest, Log, All);

namespace AsdaSkillLatchTestLocal
{
	constexpr int32 kSelfFallback = 4322;    // свой userNum, если не известен
	constexpr int32 kMobType = 1;            // волк alpen (как у прочих самотестов)
	constexpr int32 kSpdDenom = 250;         // знаменатель скорости 4013
	constexpr int32 kRid = 31201;            // моб-цель
	constexpr int32 kSkill = 705;            // «Многократный выстрел» (лук, маска 0x400) — зонд «навык уходит»
	constexpr int32 kGuard = 1252;           // скилл Хранителя «Зов дрожи» (ответ 6159/6161)
	constexpr int32 kCast = 787;             // «Зазубренная стрела»: время каста .sif +0x2D = 1200 мс (ответ 5270 → 5271)
	constexpr int32 kStressTries = 400;      // нажатий в нагрузочном шаге

	struct FOut { uint16 Op = 0; TArray<uint8> Payload; };   // перехваченный исходящий

	struct FStep
	{
		FString Name; float TimeoutSec = 10.f;
		TFunction<void()> Start; TFunction<bool()> Poll; TFunction<void()> Check;
	};

	struct FState
	{
		const TCHAR* Cmd = nullptr; const TCHAR* Tag = nullptr;
		TWeakObjectPtr<UWorld> World;
		TWeakObjectPtr<UAsdaNetSubsystem> Net;
		TArray<FStep> Steps;
		int32 StepIdx = 0; bool bStepStarted = false; double StepStartS = 0.0;
		FTimerHandle Tick; bool bQuit = false;
		FAsdaSelfTestRunGate Gate{ TEXT(""), false };
		int32 Ok = 0, Fail = 0;
		uint32 Gen = 0; TArray<FOut> Outs; int32 OutFrom = 0;
		TUniquePtr<FAsdaNetSelfScope> SelfScope;
		bool bWasInGame = false; bool bWasStand = false;
		int32 Self = -1; FIntPoint HomeTile = FIntPoint::ZeroValue;
		int32 WasWeaponSub = 99;
		bool bSampled = false;            // ответ на заявку шага уже подан
		int32 FiredBase = 0;              // срабатывания предохранителя на старте шага
		double ReleasedAtS = -1.0;        // когда состояние стало 0 (от подачи пакета)
		double FedAtS = 0.0;              // момент подачи пакета шага
		bool bSeenBusy = false;           // после подачи состояние уже побывало ненулевым (R5-fix3: без этого отпускание не засчитывается)
		// Нагрузка: обработанные исходящие, счётчики, генератор, отложенные ответы (переупорядочение/опоздание).
		int32 StressSeen = 0, StressTries = 0, StressSent = 0, StressNextRid = kRid;
		double StressNextS = 0.0;
		double StressLastSendS = 0.0, StressMaxGapS = 0.0;   // наибольший промежуток между заявками за нагрузку
		FRandomStream Rng{ 31102026 };
		struct FLate { double AtS = 0.0; int32 Op = 0; TArray<uint8> Body; };
		TArray<FLate> Late;

		UAsdaNetSubsystem* N() const { return Net.Get(); }
		UGameInstance* GI() const { UAsdaNetSubsystem* Nn = N(); return Nn ? Nn->GetGameInstance() : nullptr; }
		UAsdaWorldSubsystem* WS() const { UGameInstance* G = GI(); return G ? G->GetSubsystem<UAsdaWorldSubsystem>() : nullptr; }
		UAsdaSkillUseSubsystem* SU() const { UGameInstance* G = GI(); return G ? G->GetSubsystem<UAsdaSkillUseSubsystem>() : nullptr; }
		AAsdaPlayerController* PC() const { UWorld* W = World.Get(); return W ? Cast<AAsdaPlayerController>(W->GetFirstPlayerController()) : nullptr; }
		AAsdaPlayerCharacter* Ch() const { AAsdaPlayerController* P = PC(); return P ? Cast<AAsdaPlayerCharacter>(P->GetPawn()) : nullptr; }
		AAsdaMobActor* Mob() const { const UAsdaWorldSubsystem* W = WS(); return W ? W->FindNetMobByRid(StressNextRid) : nullptr; }
		double El() const { return FPlatformTime::Seconds() - StepStartS; }
		int32 State() const { const UAsdaSkillUseSubsystem* U = SU(); return U ? U->GetSelfCharState() : -1; }
		int32 Active() const { const UAsdaSkillUseSubsystem* U = SU(); return U ? U->GetActiveSkill() : -2; }
		int32 Fired() const { const UAsdaSkillUseSubsystem* U = SU(); return U ? U->GetLatchGuardFired() : 0; }
		void Expect(bool bOk, const FString& What)
		{
			if (bOk) { ++Ok; } else { ++Fail; }
			UE_LOG(LogAsdaSkillLatchTest, Log, TEXT("%s %s %s: %s"), Tag, bOk ? TEXT("OK") : TEXT("ПРОВАЛ"),
				Steps.IsValidIndex(StepIdx) ? *Steps[StepIdx].Name : TEXT("?"), *What);
		}
		void Feed(int32 Op, const TArray<uint8>& B) { if (UAsdaNetSubsystem* Nn = N()) { Nn->DebugFeedPacket(Op, B); } }
		void SetKey(const TCHAR* Key, int32 V) { AsdaCVarSave::SetForTest(Key, V); }   // исходное значение вернёт `Finish` (RestoreAll)
		void Mark() { OutFrom = Outs.Num(); bSampled = false; FiredBase = Fired(); ReleasedAtS = -1.0; FedAtS = 0.0; bSeenBusy = false; }
		bool SentOp(int32 Op, int32 Code = -1) const
		{
			for (int32 i = OutFrom; i < Outs.Num(); ++i)
			{
				if (Outs[i].Op != Op) { continue; }
				if (Code < 0) { return true; }
				if (Outs[i].Payload.Num() >= 26 && AsdaBytes::RdI16(&Outs[i].Payload[24]) == Code) { return true; }
			}
			return false;
		}
		// Заявка путём игрока: выбранная цель + общий диспетчер (так зовут ряд быстрого доступа и ряд Хранителя).
		void Press(int32 Code)
		{
			AAsdaPlayerController* P = PC(); UAsdaSkillUseSubsystem* U = SU();
			if (!P || !U) { return; }
			if (AAsdaPlayerCharacter* C = Ch()) { C->EquippedWeaponSubType = (Code == kGuard) ? 5 : 10; }
			P->SetTarget(Mob());
			U->DispatchByKind(Code, -1);
		}
		// Снять свой навык штатным путём (отказ 5257 кодом 0 = «ответ пришёл») — между шагами.
		void Calm(int32 Code) { if (UAsdaSkillUseSubsystem* U = SU()) { U->SimulateCastResult5257(0, Code); } }
		// ★ R5-fix3 (13.11.2026) [ИСПРАВЛЕНИЕ ТЕСТА]: отпускание засчитывается только после того, как состояние побывало ненулевым.
		//   Поданный пакет (5270) лишь ложится в очередь подсистемы навыков — состояние 14 ставит её `Tick` позже в том же кадре.
		//   Таймер теста (0,01 с, с повтором) на длинном кадре движок вызывает несколько раз за один проход (догоняет пропущенные
		//   срабатывания): любой вызов после подачи в том же проходе видел ещё нулевое состояние и записывал «снято через 0.00 с»
		//   (шаг D красный под нагрузкой, зелёный на быстром кадре). Шаги, читающие `ReleasedAtS`, проверяют и нижнюю границу
		//   (`>= 0`): значение −1 («занятость после подачи не наблюдалась») верхнюю границу проходило бы молча.
		void NoteRelease()
		{
			if (FedAtS <= 0.0) { return; }
			const int32 St = State();
			if (St != 0) { bSeenBusy = true; return; }
			if (ReleasedAtS < 0.0 && bSeenBusy) { ReleasedAtS = FPlatformTime::Seconds() - FedAtS; }
		}
	};

	// 6159 (шапка 8 б): [0] код, [1..2] кастер, [3..4] навык, [5] 0, [6..7] флаг. Отказ сервера — без записей
	//   (`RecvChar_SoulGuarderSkillUse`, GameServer.c:57274: `v26` код, `v27 = a2`, `v28` навык).
	static TArray<uint8> Body6159(int32 Res, int32 Caster, int32 Skill)
	{
		TArray<uint8> B; B.SetNumZeroed(8);
		B[0] = (uint8)Res; AsdaBytes::WrI16(&B[1], Caster); AsdaBytes::WrI16(&B[3], Skill); AsdaBytes::WrI16(&B[6], 1);
		return B;
	}
	// 6161 / 5271 (7 б): [0] код, [1..2] кастер, [3..4] навык, [5..6] флаг.
	static TArray<uint8> Body7(int32 Res, int32 Caster, int32 Skill)
	{
		TArray<uint8> B; B.SetNumZeroed(7);
		B[0] = (uint8)Res; AsdaBytes::WrI16(&B[1], Caster); AsdaBytes::WrI16(&B[3], Skill); AsdaBytes::WrI16(&B[5], 1);
		return B;
	}
	// 5270 (7 + 29·N): [0..1] кастер, [2..3] навык, [4..5] уровень, [6] записей; одна запись по себе (вид 1).
	static TArray<uint8> Body5270(int32 Caster, int32 Skill)
	{
		TArray<uint8> B; B.SetNumZeroed(7 + 29);
		AsdaBytes::WrI16(&B[0], Caster); AsdaBytes::WrI16(&B[2], Skill); AsdaBytes::WrI16(&B[4], 1); B[6] = 1;
		uint8* R = B.GetData() + 7; R[0] = 1; AsdaBytes::WrI16(R + 1, Caster); AsdaBytes::WrI16(R + 19, -1);
		return B;
	}
	// 5257 (8 + 29·N): [0] код, [1..2] кастер, [3..4] навык, [5] 1, [6..7] уровень; запись по мобу.
	static TArray<uint8> Body5257(int32 Res, int32 Caster, int32 Skill, int32 Rid)
	{
		TArray<uint8> B; B.SetNumZeroed(Res == 1 ? 8 + 29 : 8);
		B[0] = (uint8)Res; AsdaBytes::WrI16(&B[1], Caster); AsdaBytes::WrI16(&B[3], Skill); B[5] = 1; AsdaBytes::WrI16(&B[6], 1);
		if (Res == 1)
		{
			uint8* R = B.GetData() + 8; R[0] = 0; AsdaBytes::WrI16(R + 1, Rid); AsdaBytes::WrI32(R + 3, 1); R[7] = 0x01;
			AsdaBytes::WrI32(R + 11, 900); AsdaBytes::WrI16(R + 19, -1);
		}
		return B;
	}
	// 5220 (101 б на запись): [0..3] charId, [4..5] userIdx, [6..9] soulIdx (> 0 — пара есть), [32] SoulState.
	static TArray<uint8> Body5220(int32 UserIdx, int32 SoulIdx)
	{
		TArray<uint8> B; B.SetNumZeroed(101);
		AsdaBytes::WrI16(&B[4], UserIdx); AsdaBytes::WrI32(&B[6], SoulIdx); AsdaBytes::WrI16(&B[6 + 17], -1);
		return B;
	}
	// 5232 (197 б): [0] код, [1..2] userNum кастующего, [3..4] навык.
	static TArray<uint8> Body5232(int32 Res, int32 Caster, int32 Skill)
	{
		TArray<uint8> B; B.SetNumZeroed(197);
		B[0] = (uint8)Res; AsdaBytes::WrI16(&B[1], Caster); AsdaBytes::WrI16(&B[3], Skill);
		return B;
	}

	// Исполнитель шагов: спавн своего персонажа, шаги по таймеру, итог (как у `asda.Skill.SelfTestDamageText`).
	static void RunSteps(TSharedRef<FState> S, UWorld* W, const FAsdaSelfTestArgs& KitArgs, UAsdaNetSubsystem* Net)
	{
		TWeakObjectPtr<UWorld> WW(W);
		{
			FTimerHandle H; TWeakPtr<FState> WSg = S;
			W->GetTimerManager().SetTimer(H, FTimerDelegate::CreateWeakLambda(Net, [WW, WSg]()
			{
				UWorld* Wd = WW.Get(); const TSharedPtr<FState> Sg = WSg.Pin();
				if (Wd && Sg && !Sg->Gate.Pass(Wd)) { return; }
				APlayerController* P = Wd ? Wd->GetFirstPlayerController() : nullptr;
				if (Wd && (!P || !P->GetPawn()) && GEngine)
				{
					GEngine->Exec(Wd, TEXT("Asda.Load.CurrentMap 3"));
					GEngine->Exec(Wd, TEXT("Asda.World.FakeSelfSpawn 256 256"));
				}
			}), FMath::Max(0.01f, KitArgs.Delay), false);
		}
		TWeakPtr<FState> WSt = S;
		W->GetTimerManager().SetTimer(S->Tick, FTimerDelegate::CreateWeakLambda(Net, [WSt, WW]()
		{
			TSharedPtr<FState> St = WSt.Pin(); UWorld* Wd = WW.Get();
			if (!St || !Wd) { return; }
			if (!St->Gate.Pass(Wd)) { Wd->GetTimerManager().ClearTimer(St->Tick); return; }
			if (!St->Steps.IsValidIndex(St->StepIdx)) { Wd->GetTimerManager().ClearTimer(St->Tick); AsdaSelfTestKit::Quit(Wd, St->bQuit); return; }
			const double Now = FPlatformTime::Seconds();
			FStep& Stp = St->Steps[St->StepIdx];
			if (!St->bStepStarted)
			{
				St->bStepStarted = true; St->StepStartS = Now;
				UE_LOG(LogAsdaSkillLatchTest, Log, TEXT("%s шаг %s"), St->Tag, *Stp.Name);
				if (Stp.Start) { Stp.Start(); }
				return;
			}
			St->NoteRelease();
			const double El = Now - St->StepStartS;
			const bool bDone = !Stp.Poll || Stp.Poll();
			if (!bDone && El < Stp.TimeoutSec) { return; }
			if (!bDone) { St->Expect(false, FString::Printf(TEXT("шаг не завершился за %.1f с"), Stp.TimeoutSec)); }
			if (Stp.Check) { Stp.Check(); }
			++St->StepIdx; St->bStepStarted = false;
		}), 0.01f, true, KitArgs.Delay + 2.5f);
	}
}

struct FAsdaSkillLatchSelfTest
{
	static void Run(const TArray<FString>& Args, UWorld* W);
};

void FAsdaSkillLatchSelfTest::Run(const TArray<FString>& Args, UWorld* W)
{
	using namespace AsdaSkillLatchTestLocal;
	const TCHAR* const kCmd = TEXT("asda.Skill.SelfTestLatch");
	const FAsdaSelfTestArgs KitArgs = FAsdaSelfTestArgs::Parse(Args);
	static const uint16 kGuardOps[] = { 4006, 4008, 5231, 5256, 6158 };
	if (AsdaSelfTestKit::RefuseStart(W, kCmd, KitArgs, 0.f, MakeArrayView(kGuardOps))) { return; }
	UGameInstance* GIw = W ? W->GetGameInstance() : nullptr;
	UAsdaNetSubsystem* Net = GIw ? GIw->GetSubsystem<UAsdaNetSubsystem>() : nullptr;
	if (!Net) { AsdaSelfTestKit::Refused(W, kCmd, KitArgs.bQuit, TEXT("нет подсистемы сети")); return; }

	TSharedRef<FState> S = MakeShared<FState>();
	S->Cmd = kCmd; S->Tag = TEXT("САМОТЕСТ-ЗАЩЁЛКИ-НАВЫКОВ");
	S->Net = Net; S->World = W; S->bQuit = KitArgs.bQuit; S->Gate = FAsdaSelfTestRunGate{ kCmd, KitArgs.bQuit };
	S->SelfScope = MakeUnique<FAsdaNetSelfScope>(Net, kCmd);
	auto Add = [S](const FString& Name, float Timeout, TFunction<void()> Start, TFunction<bool()> Poll, TFunction<void()> Check)
	{
		FStep St; St.Name = Name; St.TimeoutSec = Timeout; St.Start = MoveTemp(Start); St.Poll = MoveTemp(Poll); St.Check = MoveTemp(Check);
		S->Steps.Add(MoveTemp(St));
	};
	// 5251 — изученные навыки (34 б на запись): код@0, уровень@2, 1@3.
	auto Body5251 = [](const TArray<int32>& Codes)
	{
		TArray<uint8> B; B.SetNumZeroed(34 * Codes.Num());
		for (int32 i = 0; i < Codes.Num(); ++i) { uint8* P = B.GetData() + 34 * i; AsdaBytes::WrI16(P, Codes[i]); P[2] = 5; P[3] = 1; }
		return B;
	};
	// Зонд «навык уходит»: нажатие 705 путём игрока, ждём 5256; затем штатное снятие навыка.
	auto AddProbe = [S, Add](const FString& Name, bool bMustGo)
	{
		Add(Name, bMustGo ? 3.f : 1.5f, [S]() { S->Mark(); S->Press(kSkill); },
			[S, bMustGo]() { return bMustGo ? S->SentOp(5256, kSkill) : S->El() > 1.2; }, [S, bMustGo]()
		{
			const bool bSent = S->SentOp(5256, kSkill);
			S->Expect(bSent == bMustGo, bMustGo
				? FString::Printf(TEXT("заявка 5256 навыка %d ушла путём игрока"), kSkill)
				: FString::Printf(TEXT("заявка 5256 навыка %d НЕ ушла (состояние %d, активный %d) — защёлка держит"), kSkill, S->State(), S->Active()));
			if (bMustGo) { S->Calm(kSkill); }   // зонд «не уходит» защёлку не трогает — её снимает следующий шаг
		});
	};
	auto Idle = [S]() { return S->Active() < 0 && S->State() == 0; };
	auto AddPause = [Add, Idle]() { Add(TEXT("пауза (навык свободен)"), 6.f, nullptr, [Idle]() { return Idle(); }, nullptr); };

	Add(TEXT("подготовка"), 5.f, [S, Net, Body5251]()
	{
		AAsdaPlayerCharacter* C = S->Ch();
		if (!C || !S->PC() || !S->WS()) { S->Expect(false, TEXT("нет своего персонажа/мира после FakeSelfSpawn")); return; }
		S->HomeTile = AsdaCoord::GameToTile(C->GetGamePosXZ());
		if (!Net->bSelfUserNumKnown) { Net->SelfUserNum = kSelfFallback; Net->bSelfUserNumKnown = true; }
		Net->bSelfDead = false;
		S->Self = (int32)Net->SelfUserNum;
		TWeakPtr<FState> WSp = S;
		S->Gen = AsdaSelfTestKit::ArmCapture(Net, [WSp](uint16 Op, const uint8* P, int32 Len, bool, TCHAR)
		{
			TSharedPtr<FState> St = WSp.Pin(); if (!St) { return; }
			FOut& O = St->Outs.AddDefaulted_GetRef(); O.Op = Op; if (P && Len > 0) { O.Payload.Append(P, Len); }
		}, (uint16)0, S->Cmd);
		S->Expect(S->Gen != 0, TEXT("перехват исходящих взведён"));
		S->bWasInGame = Net->bInGameConnected; Net->bInGameConnected = true;
		S->WasWeaponSub = C->EquippedWeaponSubType; C->EquippedWeaponSubType = 10;
		S->bWasStand = Net->bStandCaptureSkillUse; Net->bStandCaptureSkillUse = true;
		S->Feed(5251, Body5251({ kSkill }));
		Net->SetVitalsMp(S->Self, 99999, 99999, TEXT("самотест защёлок навыков"), true);
		S->SetKey(TEXT("asda.Skill.LatchGuard"), 1);
		S->Feed(4013, AsdaTestPackets::MobSpawn4013(kRid, kMobType, S->HomeTile + FIntPoint(4, 0), 1, kSpdDenom));
	}, [S]() { return S->El() > 1.0 && S->Mob(); }, [S]()
	{
		const UAsdaSkillDatabase* Db = S->GI() ? S->GI()->GetSubsystem<UAsdaSkillDatabase>() : nullptr;
		const FAsdaSkillDef* Dc = Db ? Db->Get(kCast) : nullptr;
		S->Expect(S->Mob() != nullptr, TEXT("моб на месте"));
		S->Expect(Dc && Dc->CastTimeMs == 1200, FString::Printf(TEXT("время каста навыка %d из .sif +0x2D = %d мс (ждём 1200)"), kCast, Dc ? Dc->CastTimeMs : -1));
	});
	AddProbe(TEXT("0 зонд: навык уходит"), true);
	AddPause();

	// ── A. отказ 6159 Хранителя путём игрока ──
	Add(TEXT("A отказ 6159 (код 16) — состояние 25 снято сразу"), 4.f, [S]() { S->Mark(); S->Press(kGuard); }, [S]()
	{
		if (!S->bSampled && S->SentOp(6158))
		{
			S->bSampled = true; S->FedAtS = FPlatformTime::Seconds();
			S->Expect(S->State() == 25, FString::Printf(TEXT("после 6158 состояние 25 (заявка в пути): %d"), S->State()));
			S->Feed(6159, Body6159(16, S->Self, kGuard));
		}
		return S->bSampled && FPlatformTime::Seconds() - S->FedAtS > 0.5;
	}, [S]()
	{
		S->Expect(S->SentOp(6158), TEXT("заявка 6158 ряда Хранителя ушла путём игрока"));
		S->Expect(S->State() == 0 && S->Active() < 0, FString::Printf(TEXT("после отказа: состояние %d, активный %d (ждём 0 / −1)"), S->State(), S->Active()));
		S->Expect(S->Fired() == S->FiredBase, TEXT("снято ответом, а не предохранителем"));
	});
	AddProbe(TEXT("A зонд: навык уходит после отказа 6159"), true);
	AddPause();

	// ── B. контроль: прежнее поведение (ключ 0) — защёлка вечная; ключ 1 — предохранитель ──
	Add(TEXT("B контроль asda.Skill.LatchGuard 0: отказ 6159 оставляет 25"), 6.f, [S]()
	{
		S->Mark(); S->SetKey(TEXT("asda.Skill.LatchGuard"), 0); S->Press(kGuard);
	}, [S]()
	{
		if (!S->bSampled && S->SentOp(6158)) { S->bSampled = true; S->FedAtS = FPlatformTime::Seconds(); S->Feed(6159, Body6159(16, S->Self, kGuard)); }
		return S->bSampled && FPlatformTime::Seconds() - S->FedAtS > 3.0;
	}, [S]()
	{
		S->Expect(S->State() == 25 && S->Active() < 0, FString::Printf(TEXT("прежний порт: состояние %d, активный %d через 3 с — защёлка (дефект воспроизведён)"), S->State(), S->Active()));
	});
	AddProbe(TEXT("B зонд при ключе 0: навык НЕ уходит"), false);
	Add(TEXT("B ключ 1 — предохранитель снимает 25 без активного навыка"), 4.f, [S]()
	{
		S->Mark(); S->FedAtS = FPlatformTime::Seconds(); S->SetKey(TEXT("asda.Skill.LatchGuard"), 1);
	}, [S]() { return S->State() == 0; }, [S]()
	{
		S->Expect(S->State() == 0, FString::Printf(TEXT("состояние снято за %.2f с (предел 2 с + тик)"), S->ReleasedAtS));
		S->Expect(S->Fired() == S->FiredBase + 1, FString::Printf(TEXT("предохранитель сработал: +%d"), S->Fired() - S->FiredBase));
	});
	AddProbe(TEXT("B зонд: навык снова уходит"), true);
	AddPause();

	// ── C. отказ 6161 без 6159 ──
	Add(TEXT("C отказ 6161 без 6159 — состояние снято сразу"), 4.f, [S]() { S->Mark(); S->Press(kGuard); }, [S]()
	{
		if (!S->bSampled && S->SentOp(6158)) { S->bSampled = true; S->FedAtS = FPlatformTime::Seconds(); S->Feed(6161, Body7(20, S->Self, kGuard)); }
		return S->bSampled && FPlatformTime::Seconds() - S->FedAtS > 0.5;
	}, [S]()
	{
		S->Expect(S->State() == 0 && S->Active() < 0, FString::Printf(TEXT("состояние %d, активный %d за 0.5 с (сторож 2 с ещё не мог)"), S->State(), S->Active()));
		S->Expect(S->Fired() == S->FiredBase, TEXT("снято ответом 6161"));
	});
	AddProbe(TEXT("C зонд"), true);
	AddPause();

	// ── D. 5270 без 5271 ──
	Add(TEXT("D 5270 без 5271 (787, каст 1200 мс) — предохранитель через каст + 2 с"), 7.f, [S]()
	{
		S->Mark(); S->FedAtS = FPlatformTime::Seconds(); S->Feed(5270, Body5270(S->Self, kCast));
	}, [S]()
	{
		if (!S->bSampled && FPlatformTime::Seconds() - S->FedAtS > 2.5)
		{
			S->bSampled = true;
			S->Expect(S->State() == 14 && S->Active() == kCast, FString::Printf(TEXT("через 2.5 с каст ещё идёт законно: состояние %d, активный %d"), S->State(), S->Active()));
		}
		return S->bSampled && S->State() == 0;
	}, [S]()
	{
		S->Expect(S->State() == 0 && S->ReleasedAtS >= 3.0 && S->ReleasedAtS <= 4.5,
			FString::Printf(TEXT("состояние 14 снято через %.2f с (ждём 3.2 ± тик)"), S->ReleasedAtS));
		S->Expect(S->Fired() == S->FiredBase + 1, TEXT("снято предохранителем"));
	});
	AddProbe(TEXT("D зонд"), true);
	AddPause();
	Add(TEXT("D контроль ключа 0: 14 без 5271 держится"), 7.f, [S]()
	{
		S->Mark(); S->SetKey(TEXT("asda.Skill.LatchGuard"), 0); S->FedAtS = FPlatformTime::Seconds(); S->Feed(5270, Body5270(S->Self, kCast));
	}, [S]() { return FPlatformTime::Seconds() - S->FedAtS > 5.5; }, [S]()
	{
		S->Expect(S->State() == 14, FString::Printf(TEXT("прежний порт: через 5.5 с состояние %d (вечное 14 — дефект воспроизведён)"), S->State()));
		S->SetKey(TEXT("asda.Skill.LatchGuard"), 1);
	});
	Add(TEXT("D ключ 1 — снято"), 6.f, nullptr, [S]() { return S->State() == 0; }, [S]()
	{
		S->Expect(S->State() == 0, TEXT("после включения ключа 14 снято предохранителем"));
	});
	AddPause();

	// ── E. 5271 раньше 5270 ──
	Add(TEXT("E 5271 раньше 5270 — 14 снимается предохранителем"), 7.f, [S]()
	{
		S->Mark(); S->FedAtS = FPlatformTime::Seconds();
		S->Feed(5271, Body7(1, S->Self, kCast)); S->Feed(5270, Body5270(S->Self, kCast));
	}, [S]() { return FPlatformTime::Seconds() - S->FedAtS > 0.3 && S->State() == 0; }, [S]()
	{
		S->Expect(S->State() == 0 && S->ReleasedAtS >= 0.0 && S->ReleasedAtS <= 4.5, FString::Printf(TEXT("состояние снято через %.2f с"), S->ReleasedAtS));
	});
	AddProbe(TEXT("E зонд"), true);
	AddPause();

	// ── F. 25 без активного навыка (навык соулмейта 5231) ──
	Add(TEXT("F чужой/поздний 5232 без своей заявки 5231 состояние не снимает"), 3.f, [S]()
	{
		S->Mark();
		if (UAsdaSkillUseSubsystem* U = S->SU()) { U->DebugSetCastState(25, -1); }   // ровно то, что оставляет CastSoulmateSkill
		S->FedAtS = FPlatformTime::Seconds(); S->Feed(5232, Body5232(5, S->Self, 32));
	}, [S]() { return FPlatformTime::Seconds() - S->FedAtS > 0.3; }, [S]()
	{
		S->Expect(S->State() == 25 && S->Fired() == S->FiredBase, FString::Printf(TEXT("состояние %d (ждём 25: ответ не к заявке — ревизия п.2)"), S->State()));
	});
	Add(TEXT("F 25 без активного навыка и без ответа — предохранитель 2 с"), 5.f, [S]()
	{
		S->Mark();
		if (UAsdaSkillUseSubsystem* U = S->SU()) { U->DebugSetCastState(25, -1); }
		S->FedAtS = FPlatformTime::Seconds();
	}, [S]() { return S->State() == 0; }, [S]()
	{
		S->Expect(S->State() == 0 && S->ReleasedAtS >= 0.0 && S->ReleasedAtS <= 2.6 && S->Fired() == S->FiredBase + 1, FString::Printf(TEXT("снято через %.2f с предохранителем"), S->ReleasedAtS));
	});
	AddProbe(TEXT("F зонд"), true);
	AddPause();

	// ── H. поздний 5271 старого навыка (ревизия п.1) ──
	Add(TEXT("H поздний 5271 навыка 787 не снимает новую заявку 705"), 4.f, [S]() { S->Mark(); S->Press(kSkill); }, [S]()
	{
		if (!S->bSampled && S->SentOp(5256, kSkill)) { S->bSampled = true; S->FedAtS = FPlatformTime::Seconds(); S->Feed(5271, Body7(1, S->Self, kCast)); }
		return S->bSampled && FPlatformTime::Seconds() - S->FedAtS > 0.4;
	}, [S]()
	{
		S->Expect(S->State() == 25 && S->Active() == kSkill, FString::Printf(TEXT("заявка 705 цела: состояние %d, активный %d (ждём 25 / 705)"), S->State(), S->Active()));
		S->Calm(kSkill);
	});
	AddPause();
	// ── I. поздний отказ 5257 по другому навыку не гасит текущий показ (п.3) ──
	Add(TEXT("I отказ 5257 по 705 во время показа 1252 — показ и активный 1252 целы"), 5.f, [S]()
	{
		S->Mark();
		if (AAsdaPlayerCharacter* C = S->Ch()) { C->EquippedWeaponSubType = 5; }
		S->Feed(5257, Body5257(1, S->Self, kGuard, S->StressNextRid));
	}, [S]()
	{
		const UAsdaSkillFxSubsystem* Fx = S->GI() ? S->GI()->GetSubsystem<UAsdaSkillFxSubsystem>() : nullptr;
		const bool bShow = Fx && S->Ch() && Fx->IsSkillVisualActive(S->Ch());
		if (!S->bSampled && S->El() > 0.3)
		{
			S->bSampled = true;
			S->Expect(bShow && S->Active() == kGuard && S->State() == 24, FString::Printf(TEXT("до отказа: показ %d, активный %d, состояние %d"), bShow ? 1 : 0, S->Active(), S->State()));
			S->Feed(5257, Body5257(2, S->Self, kSkill, S->StressNextRid));
			S->FedAtS = FPlatformTime::Seconds();
		}
		return S->bSampled && FPlatformTime::Seconds() - S->FedAtS > 0.3;
	}, [S]()
	{
		const UAsdaSkillFxSubsystem* Fx = S->GI() ? S->GI()->GetSubsystem<UAsdaSkillFxSubsystem>() : nullptr;
		const bool bShow = Fx && S->Ch() && Fx->IsSkillVisualActive(S->Ch());
		S->Expect(bShow, TEXT("показ 1252 не погашен поздним отказом по 705"));
		S->Expect(S->Active() == kGuard && S->State() == 24, FString::Printf(TEXT("активный %d, состояние %d (ждём 1252 / 24)"), S->Active(), S->State()));
		S->Calm(kGuard);
		if (AAsdaPlayerCharacter* C = S->Ch()) { C->EquippedWeaponSubType = 10; }
	});
	AddPause();
	// ── J. серия молчаливых отказов → одно Warning (п.5) ──
	Add(TEXT("J 5 нажатий за 1 с при состоянии 25 — одно Warning в журнал"), 3.f, [S]()
	{
		S->Mark(); S->FiredBase = S->SU() ? S->SU()->GetSilentRefuseWarns() : 0;
		if (UAsdaSkillUseSubsystem* U = S->SU()) { U->DebugSetCastState(25, -1); }
		S->FedAtS = FPlatformTime::Seconds(); S->StressTries = 0; S->StressNextS = 0.0;
	}, [S]()
	{
		const double Now = FPlatformTime::Seconds();
		if (S->StressTries < 5 && Now >= S->StressNextS) { S->Press(kSkill); ++S->StressTries; S->StressNextS = Now + 0.2; }
		return S->StressTries >= 5 && Now - S->FedAtS > 1.2;
	}, [S]()
	{
		const int32 W = S->SU() ? S->SU()->GetSilentRefuseWarns() - S->FiredBase : -1;
		S->Expect(W == 1, FString::Printf(TEXT("Warning «серия молчаливых отказов» записано %d раз (ждём 1 на серию)"), W));
		S->Expect(!S->SentOp(5256, kSkill), TEXT("заявки за серию не ушли (состояние 25)"));
		if (UAsdaSkillUseSubsystem* U = S->SU()) { U->DebugSetCastState(0, -1); }
	});
	AddPause();
	// ── K. навык соулмейта 5231 (п.2) ──
	Add(TEXT("K навык соулмейта: гейт 14 молча, неушедшая заявка без защёлки"), 5.f, [S]()
	{
		S->Mark();
		S->Feed(5220, Body5220(S->Self, 5));   // пара есть (soulIdx 5)
		if (UAsdaSkillUseSubsystem* U = S->SU()) { U->DebugSetCastState(14, kCast); }
		if (UAsdaSoulmateSkillSubsystem* SS = S->GI() ? S->GI()->GetSubsystem<UAsdaSoulmateSkillSubsystem>() : nullptr) { SS->UseSkill(30); }
	}, [S]()
	{
		if (!S->bSampled && S->El() > 0.3)
		{
			S->bSampled = true;
			S->Expect(!S->SentOp(5231) && S->State() == 14 && S->Active() == kCast, FString::Printf(TEXT("при 14: 5231 не ушёл, состояние %d (14 не перетёрто)"), S->State()));
			if (UAsdaSkillUseSubsystem* U = S->SU()) { U->DebugSetCastState(0, -1); }
			S->OutFrom = S->Outs.Num();
			if (UAsdaSoulmateSkillSubsystem* SS = S->GI() ? S->GI()->GetSubsystem<UAsdaSoulmateSkillSubsystem>() : nullptr) { SS->UseSkill(30); }
			S->FedAtS = FPlatformTime::Seconds();
		}
		return S->bSampled && FPlatformTime::Seconds() - S->FedAtS > 0.3;
	}, [S]()
	{
		// Офлайн отправитель 5231 молчит (нужен живой сокет, перехвата стойки у него нет) — проверяем, что НЕушедшая заявка
		//   защёлку не ставит (прежде порт ставил 25 и при неудачной отправке). Путь «ушла → 5232» — только на живом.
		S->Expect(!S->SentOp(5231) && S->State() == 0, FString::Printf(TEXT("5231 не ушёл (офлайн) — состояние %d (ждём 0, без защёлки)"), S->State()));
		S->Feed(5220, Body5220(S->Self, 0));   // пары снова нет
	});
	AddPause();

	// ── G. нагрузка ──
	Add(FString::Printf(TEXT("G нагрузка: %d нажатий со случайными ответами"), kStressTries), 120.f, [S]()
	{
		S->Mark(); S->StressSeen = S->Outs.Num(); S->StressTries = 0; S->StressSent = 0; S->StressNextS = 0.0; S->Late.Reset();
		S->StressLastSendS = FPlatformTime::Seconds(); S->StressMaxGapS = 0.0;
	}, [S]()
	{
		const double Now = FPlatformTime::Seconds();
		// Опоздавшие/переставленные ответы.
		for (int32 i = S->Late.Num() - 1; i >= 0; --i)
		{
			if (Now >= S->Late[i].AtS) { const FState::FLate L = S->Late[i]; S->Late.RemoveAt(i); S->Feed(L.Op, L.Body); }
		}
		// Ответы на новые заявки.
		for (; S->StressSeen < S->Outs.Num(); ++S->StressSeen)
		{
			const FOut& O = S->Outs[S->StressSeen];
			if (O.Op != 5256 && O.Op != 6158) { continue; }
			++S->StressSent;
			S->StressMaxGapS = FMath::Max(S->StressMaxGapS, Now - S->StressLastSendS); S->StressLastSendS = Now;
			const int32 Roll = S->Rng.RandRange(0, 99);
			if (O.Op == 6158)
			{
				if (Roll < 35) { S->Feed(6161, Body7(1, S->Self, kGuard)); S->Feed(6159, Body6159(1, S->Self, kGuard)); }        // успех
				else if (Roll < 65) { S->Feed(6159, Body6159(16 + (Roll % 5), S->Self, kGuard)); }                                    // отказ 6159
				else if (Roll < 80) { S->Feed(6161, Body7(20, S->Self, kGuard)); }                                                    // отказ 6161
				else if (Roll < 90) { FState::FLate L; L.AtS = Now + 0.6; L.Op = 6159; L.Body = Body6159(1, S->Self, kGuard); S->Late.Add(L); }   // опоздание
				// иначе — ответ потерян
			}
			else
			{
				if (Roll < 30) { S->Feed(5257, Body5257(1, S->Self, kSkill, S->StressNextRid)); }                                   // успех
				else if (Roll < 50) { S->Feed(5257, Body5257(2, S->Self, kSkill, S->StressNextRid)); }                              // отказ
				else if (Roll < 62) { S->Feed(5257, Body5257(1, S->Self, kSkill, S->StressNextRid)); S->Feed(5257, Body5257(1, S->Self, kSkill, S->StressNextRid)); }   // дубль
				else if (Roll < 74) { S->Feed(5270, Body5270(S->Self, kSkill)); FState::FLate L; L.AtS = Now + 2.0; L.Op = 5271; L.Body = Body7(1, S->Self, kSkill); S->Late.Add(L); }   // каст 2 с
				else if (Roll < 82) { S->Feed(5271, Body7(1, S->Self, kSkill)); S->Feed(5270, Body5270(S->Self, kSkill)); }          // переставлены
				else if (Roll < 90) { S->Feed(5270, Body5270(S->Self, kSkill)); }                                                    // 5271 потерян
				// иначе — ответ потерян
			}
		}
		// Смерть цели и новый моб — каждые 60 нажатий.
		if (S->StressTries > 0 && S->StressTries % 60 == 0 && S->StressNextS > 0.0 && Now >= S->StressNextS)
		{
			TArray<uint8> B; B.SetNumZeroed(2); AsdaBytes::WrI16(&B[0], S->StressNextRid); S->Feed(4011, B);
			++S->StressNextRid;
			S->Feed(4013, AsdaTestPackets::MobSpawn4013(S->StressNextRid, kMobType, S->HomeTile + FIntPoint(4, 0), 1, kSpdDenom));
		}
		if (S->StressTries < kStressTries && Now >= S->StressNextS)
		{
			S->Press((S->StressTries % 3 == 2) ? kGuard : kSkill);
			++S->StressTries;
			S->StressNextS = Now + 0.12;
		}
		return S->StressTries >= kStressTries && S->Late.Num() == 0;
	}, [S]()
	{
		// Законные паузы: потерянный ответ — сторож 2 с; потерянный 5271 у 705 — каст 2 с + 2 с; плюс шаг нажатий 0.12 с.
		S->Expect(S->StressSent >= 30, FString::Printf(TEXT("за %d нажатий ушло заявок %d (5256 + 6158)"), S->StressTries, S->StressSent));
		S->Expect(S->StressMaxGapS <= 6.0, FString::Printf(TEXT("наибольший промежуток между заявками %.2f с (законный предел 4 с + шаг; вечной защёлки нет)"), S->StressMaxGapS));
		UE_LOG(LogAsdaSkillLatchTest, Log, TEXT("%s нагрузка: нажатий %d, заявок %d, наибольший промежуток %.2f с, срабатываний предохранителя %d"), S->Tag, S->StressTries, S->StressSent, S->StressMaxGapS, S->Fired() - S->FiredBase);
	});
	Add(TEXT("G тишина: все защёлки снялись"), 8.f, nullptr, [Idle]() { return Idle(); }, [S, Idle]()
	{
		S->Expect(Idle(), FString::Printf(TEXT("после нагрузки состояние %d, активный %d (ждём 0 / −1)"), S->State(), S->Active()));
	});
	AddProbe(TEXT("G зонд после нагрузки: навык уходит"), true);

	Add(TEXT("итог"), 2.f, [S, Net]()
	{
		{ TArray<uint8> B; B.SetNumZeroed(2); AsdaBytes::WrI16(&B[0], S->StressNextRid); S->Feed(4011, B); }
		if (AAsdaPlayerController* P = S->PC()) { P->SetTarget(nullptr); P->bAutoAttack = false; P->ExitCombatStance(); }
		if (AAsdaPlayerCharacter* C = S->Ch()) { C->EquippedWeaponSubType = S->WasWeaponSub; }
		if (UAsdaSkillUseSubsystem* U = S->SU()) { U->DebugSetCastState(0, -1); }
		Net->ClearMoveRequestInFlight(TEXT("самотест защёлок навыков: итог"));
		Net->bStandCaptureSkillUse = S->bWasStand;
		AsdaSelfTestKit::DisarmCapture(Net, S->Gen);
		Net->bInGameConnected = S->bWasInGame;
		S->SelfScope.Reset();
	}, [S]() { return S->El() > 0.3; }, [S]()
	{
		AsdaSelfTestKit::Finish(S->World.Get(), S->Cmd, false, LogAsdaSkillLatchTest, S->Tag, S->Ok, S->Fail);
	});
	RunSteps(S, W, KitArgs, Net);
}

static FAutoConsoleCommandWithWorldAndArgs GAsdaSelfTestSkillLatch(
	TEXT("asda.Skill.SelfTestLatch"),
	TEXT("Самотест «навыки не залипают» (31.10, баг 7): отказ 6159/6161 Хранителя, 5270 без 5271, 5271 раньше 5270, 25 без активного "
	     "навыка (5231/5232), нагрузка 400 нажатий со случайными ответами; контроль asda.Skill.LatchGuard 0 воспроизводит вечную "
	     "защёлку. Только офлайн. `delay=N` `quit`."),
	FConsoleCommandWithWorldAndArgsDelegate::CreateLambda([](const TArray<FString>& A, UWorld* W)
	{
		AsdaCVarSave::ArmWorldGuard(W, TEXT("asda.Skill.SelfTestLatch"));
		FAsdaSkillLatchSelfTest::Run(A, W);
	}));

#endif // !UE_BUILD_SHIPPING
