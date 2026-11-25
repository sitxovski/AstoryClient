// Copyright (c) 2026 @sitxovski (Discord)
// ★ САМОТЕСТ «следящий эффект гаснет вместе с умершим мобом» (R5-fix5, 13.11.2026): `asda.Fx.SelfTestDeadMobHost [delay=N] [quit]`.
//   Проверяет правку под ключом `asda.Fx.DieWithDeadMob` (разбор — AsdaFxDeadHost.h) настоящими путями игры, только офлайн
//   (`-AsdaNoNet`): свой персонаж — `Asda.World.FakeSelfSpawn`, мобы — запись 4013, входящие — `DebugFeedPacket`.
//   Шаги:
//     A) ключ 1: состояние 296 «Похищение крови» на себе при выбранной цели-мобе → эффект состояния рождается на мобе и помечен;
//        моб умирает → помеченный эффект погашен;
//     B) контроль, ключ 0: то же — эффект после смерти моба ЖИВ (прежнее поведение, дефект воспроизведён); ключ 1 — погашен;
//     C) ключ 1: фаза каста навыка 908 (успех 5257) → запись фазы каста на цели-мобе помечена; моб умирает → погашена;
//        контроль, ключ 0: запись после смерти моба жива; ключ 1 — погашена;
//     C2) ★ R5-fix6: то же для навыка 756 «Аркан» (вторая и последняя запись фазы каста с привязкой к цели-мобу) — ключ 1: погашена;
//   Смерть моба в A–C — пакет 4017 с нулевыми флагами; «погашен» проверяется через 0.12 с после смерти (не спутать с
//   естественным концом эффекта), а перед смертью эффект обязан быть жив.
//     D) ключ 1: добивающий удар навыка 906 «Молния» (5257 со смертельной записью) → эффект ветки удара на мобе НЕ помечен и
//        после смерти моба ЖИВ.
//   Ничего не отправляет (исходящие перехвачены). Итог — «САМОТЕСТ-ЭФФЕКТ-ТРУП ИТОГ: ok=N fail=M».
#include "CoreMinimal.h"

#if !UE_BUILD_SHIPPING

#include "AsdaNetSubsystem.h"
#include "AsdaBytes.h"
#include "AsdaCVarSave.h"
#include "AsdaEffectActor.h"
#include "AsdaFxDeadHost.h"
#include "Test/AsdaSelfTestRunGate.h"
#include "AsdaTestPackets.h"
#include "Test/AsdaSelfTestKit.h"
#include "UI/AsdaEffectsSubsystem.h"
#include "UI/AsdaSkillUseSubsystem.h"
#include "World/AsdaWorldSubsystem.h"
#include "World/AsdaPlayerController.h"
#include "World/AsdaPlayerCharacter.h"
#include "World/AsdaCoord.h"
#include "Npc/AsdaMobActor.h"
#include "Engine/Engine.h"
#include "Engine/GameInstance.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "GameFramework/PlayerController.h"
#include "HAL/IConsoleManager.h"
#include "TimerManager.h"

DEFINE_LOG_CATEGORY_STATIC(LogAsdaFxDeadHostTest, Log, All);

namespace AsdaFxDeadHostTestLocal
{
	constexpr int32 kSelfFallback = 4322;    // свой userNum, если не известен
	constexpr int32 kMobType = 1;            // волк alpen (как у прочих самотестов)
	constexpr int32 kSpdDenom = 250;         // знаменатель скорости 4013
	constexpr int32 kRid0 = 31401;           // первый моб-цель (дальше +1 на шаг)
	constexpr int32 kState = 296;            // состояние «Похищение крови»: эффект на цели обладателя (режим 9)
	constexpr int32 kCastSkill = 908;        // навык с записью фазы каста на цели-мобе (track:target)
	constexpr int32 kCastSkill2 = 756;       // ★ R5-fix6: «Аркан» — вторая запись фазы каста на цели-мобе (track:target, по классу размера цели)
	constexpr int32 kHitSkill = 906;         // «Молния»: эффект ветки удара на цели-мобе (track:target)
	constexpr int32 kStaff = 5;              // подтип оружия навыков 906/908
	const TCHAR* const kHitJeb = TEXT("lgtstmb03");   // имя .jeb эффекта удара навыка 906 (подстрока имени класса BP_Fx_…)

	// Перехваченный исходящий пакет (содержимое тесту не нужно — только чтобы ничего не ушло).
	struct FOut { uint16 Op = 0; };

	// Шаг сценария: старт, опрос «готово», проверка.
	struct FStep
	{
		FString Name; float TimeoutSec = 10.f;
		TFunction<void()> Start; TFunction<bool()> Poll; TFunction<void()> Check;
	};

	// 5257 (8 + 29): [0] код, [1..2] кастер, [3..4] навык, [5] 1, [6..7] уровень; одна запись по мобу: урон и HP после удара.
	static TArray<uint8> Body5257(int32 Caster, int32 Skill, int32 Rid, int32 Damage, int32 HpAfter)
	{
		TArray<uint8> B; B.SetNumZeroed(8 + 29);
		B[0] = 1; AsdaBytes::WrI16(&B[1], Caster); AsdaBytes::WrI16(&B[3], Skill); B[5] = 1; AsdaBytes::WrI16(&B[6], 1);
		uint8* R = B.GetData() + 8; R[0] = 0; AsdaBytes::WrI16(R + 1, Rid); AsdaBytes::WrI32(R + 3, Damage); R[7] = 0x01;
		AsdaBytes::WrI32(R + 11, HpAfter); AsdaBytes::WrI16(R + 19, -1);
		return B;
	}

	// Состояние прогона.
	struct FState
	{
		const TCHAR* Cmd = nullptr; const TCHAR* Tag = nullptr;   // команда и метка строк журнала
		TWeakObjectPtr<UWorld> World;                             // мир
		TWeakObjectPtr<UAsdaNetSubsystem> Net;                    // сеть
		TArray<FStep> Steps;                                      // шаги
		int32 StepIdx = 0; bool bStepStarted = false; double StepStartS = 0.0;   // текущий шаг, начат ли он, время его начала
		FTimerHandle Tick; bool bQuit = false;                    // таймер шагов; выход из игры по итогу
		FAsdaSelfTestRunGate Gate{ TEXT(""), false };             // допуск прогона (не идёт ли другой самотест)
		int32 Ok = 0, Fail = 0;                                   // счётчики
		uint32 Gen = 0; TArray<FOut> Outs;                        // перехват исходящих
		TUniquePtr<FAsdaNetSelfScope> SelfScope;                  // «свой персонаж известен» на время прогона
		bool bWasInGame = false; bool bWasStand = false;          // прежние флаги сети
		int32 Self = -1; FIntPoint HomeTile = FIntPoint::ZeroValue;   // свой userNum; тайл своего персонажа
		int32 WasWeaponSub = 99;                                  // прежний подтип оружия
		int32 Rid = kRid0;                                        // моб текущего шага
		double MarkS = 0.0;                                       // отметка времени внутри шага
		bool bFlag = false;                                       // разовое действие шага сделано
		double DeadSeenS = 0.0;                                   // шаг D: когда моб впервые увиден мёртвым
		int32 HitFxAtDeath = 0;                                   // шаг D: эффектов удара на мобе в этот момент
		int32 MarkedBefore = 0;                                   // помеченных эффектов на мобе перед смертью / включением ключа

		// Короткие доступы: сеть, экземпляр игры, подсистемы мира / состояний / навыков, свой контроллер и персонаж, моб шага.
		UAsdaNetSubsystem* N() const { return Net.Get(); }
		UGameInstance* GI() const { UAsdaNetSubsystem* Nn = N(); return Nn ? Nn->GetGameInstance() : nullptr; }
		UAsdaWorldSubsystem* WS() const { UGameInstance* G = GI(); return G ? G->GetSubsystem<UAsdaWorldSubsystem>() : nullptr; }
		UAsdaEffectsSubsystem* FX() const { UGameInstance* G = GI(); return G ? G->GetSubsystem<UAsdaEffectsSubsystem>() : nullptr; }
		UAsdaSkillUseSubsystem* SU() const { UGameInstance* G = GI(); return G ? G->GetSubsystem<UAsdaSkillUseSubsystem>() : nullptr; }
		AAsdaPlayerController* PC() const { UWorld* W = World.Get(); return W ? Cast<AAsdaPlayerController>(W->GetFirstPlayerController()) : nullptr; }
		AAsdaPlayerCharacter* Ch() const { AAsdaPlayerController* P = PC(); return P ? Cast<AAsdaPlayerCharacter>(P->GetPawn()) : nullptr; }
		AAsdaMobActor* Mob() const { const UAsdaWorldSubsystem* W = WS(); return W ? W->FindNetMobByRid(Rid) : nullptr; }
		double El() const { return FPlatformTime::Seconds() - StepStartS; }    // секунд с начала шага
		double Since() const { return FPlatformTime::Seconds() - MarkS; }     // секунд с отметки внутри шага
		// Проверка с записью в журнал («OK» / «ПРОВАЛ» + имя шага).
		void Expect(bool bOk, const FString& What)
		{
			if (bOk) { ++Ok; } else { ++Fail; }
			UE_LOG(LogAsdaFxDeadHostTest, Log, TEXT("%s %s %s: %s"), Tag, bOk ? TEXT("OK") : TEXT("ПРОВАЛ"),
				Steps.IsValidIndex(StepIdx) ? *Steps[StepIdx].Name : TEXT("?"), *What);
		}
		void Feed(int32 Op, const TArray<uint8>& B) { if (UAsdaNetSubsystem* Nn = N()) { Nn->DebugFeedPacket(Op, B); } }   // входящий пакет штатным приёмом
		void SetKey(int32 V) { AsdaCVarSave::SetForTest(TEXT("asda.Fx.DieWithDeadMob"), V); }   // ключ правки; исходное вернёт `Finish`
		void Exec(const TCHAR* C) { if (GEngine && World.IsValid()) { GEngine->Exec(World.Get(), C); } }   // консольная команда
		// Живые эффекты, следящие за мобом: помеченные (гаснут с мобом) и непомеченные (ветка удара).
		void CountFollowers(const AAsdaMobActor* M, int32& OutMarked, int32& OutPlain) const
		{
			OutMarked = 0; OutPlain = 0;
			UWorld* W = World.Get();
			if (!W || !M) { return; }
			for (TActorIterator<AAsdaEffectActor> It(W); It; ++It)
			{
				const AAsdaEffectActor* E = *It;
				if (!IsValid(E) || E->IsActorBeingDestroyed() || E->FollowActor != M) { continue; }
				if (E->Tags.Contains(AsdaFxDeadHost::Tag())) { ++OutMarked; } else { ++OutPlain; }
			}
		}
		// Живые НЕпомеченные эффекты, следящие за мобом, в имени класса которых есть подстрока (эффект удара — по имени .jeb:
		//   за мобом следят и посторонние эффекты, например метка выбранной цели).
		int32 PlainNamed(const TCHAR* Sub) const
		{
			int32 N = 0;
			UWorld* W = World.Get(); const AAsdaMobActor* M = Mob();
			if (!W || !M) { return 0; }
			for (TActorIterator<AAsdaEffectActor> It(W); It; ++It)
			{
				const AAsdaEffectActor* E = *It;
				if (!IsValid(E) || E->IsActorBeingDestroyed() || E->FollowActor != M || E->Tags.Contains(AsdaFxDeadHost::Tag())) { continue; }
				if (E->GetClass()->GetName().Contains(Sub)) { ++N; }
			}
			return N;
		}
		int32 Marked() const { int32 A = 0, B = 0; CountFollowers(Mob(), A, B); return A; }   // помеченных эффектов на мобе шага
		int32 Plain() const { int32 A = 0, B = 0; CountFollowers(Mob(), A, B); return B; }    // непомеченных эффектов на мобе шага
		// Новый моб шага: прежний снимается (4011), новый приходит записью 4013.
		void NextMob()
		{
			if (Mob()) { TArray<uint8> B; B.SetNumZeroed(2); AsdaBytes::WrI16(&B[0], Rid); Feed(4011, B); }
			++Rid;
			Feed(4013, AsdaTestPackets::MobSpawn4013(Rid, kMobType, HomeTile + FIntPoint(4, 0), 1, kSpdDenom));
		}
		// Выбрать моба целью (так делает щелчок игрока).
		void Target() { if (AAsdaPlayerController* P = PC()) { P->SetTarget(Mob()); } }
		// Снять свой навык и состояние между шагами.
		void Calm()
		{
			Exec(TEXT("asda.Buff.Remove 296 0"));
			if (UAsdaSkillUseSubsystem* U = SU()) { U->DebugSetCastState(0, -1); }
		}
	};

	// Исполнитель шагов: спавн своего персонажа, шаги по таймеру.
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
				St->bStepStarted = true; St->StepStartS = Now; St->MarkS = Now; St->bFlag = false; St->DeadSeenS = 0.0;
				UE_LOG(LogAsdaFxDeadHostTest, Log, TEXT("%s шаг %s"), St->Tag, *Stp.Name);
				if (Stp.Start) { Stp.Start(); }
				return;
			}
			const double El = Now - St->StepStartS;
			const bool bDone = !Stp.Poll || Stp.Poll();
			if (!bDone && El < Stp.TimeoutSec) { return; }
			if (!bDone) { St->Expect(false, FString::Printf(TEXT("шаг не завершился за %.1f с"), Stp.TimeoutSec)); }
			if (Stp.Check) { Stp.Check(); }
			++St->StepIdx; St->bStepStarted = false;
		}), 0.05f, true, KitArgs.Delay + 2.5f);
	}
}

// Самотест «следящий эффект гаснет вместе с умершим мобом» — запуск и сценарий.
struct FAsdaFxDeadMobHostSelfTest
{
	// Запуск: разбор аргументов (`delay=N`, `quit`), сборка шагов A–D и их исполнение по таймеру.
	static void Run(const TArray<FString>& Args, UWorld* W);
};

void FAsdaFxDeadMobHostSelfTest::Run(const TArray<FString>& Args, UWorld* W)
{
	using namespace AsdaFxDeadHostTestLocal;
	const TCHAR* const kCmd = TEXT("asda.Fx.SelfTestDeadMobHost");
	const FAsdaSelfTestArgs KitArgs = FAsdaSelfTestArgs::Parse(Args);
	static const uint16 kGuardOps[] = { 4006, 4008, 5256 };
	if (AsdaSelfTestKit::RefuseStart(W, kCmd, KitArgs, 0.f, MakeArrayView(kGuardOps))) { return; }
	UGameInstance* GIw = W ? W->GetGameInstance() : nullptr;
	UAsdaNetSubsystem* Net = GIw ? GIw->GetSubsystem<UAsdaNetSubsystem>() : nullptr;
	if (!Net) { AsdaSelfTestKit::Refused(W, kCmd, KitArgs.bQuit, TEXT("нет подсистемы сети")); return; }

	TSharedRef<FState> S = MakeShared<FState>();
	S->Cmd = kCmd; S->Tag = TEXT("САМОТЕСТ-ЭФФЕКТ-ТРУП");
	S->Net = Net; S->World = W; S->bQuit = KitArgs.bQuit; S->Gate = FAsdaSelfTestRunGate{ kCmd, KitArgs.bQuit };
	S->SelfScope = MakeUnique<FAsdaNetSelfScope>(Net, kCmd);

	auto Add = [S](const FString& Name, float Timeout, TFunction<void()> Start, TFunction<bool()> Poll, TFunction<void()> Check)
	{
		FStep St; St.Name = Name; St.TimeoutSec = Timeout; St.Start = MoveTemp(Start); St.Poll = MoveTemp(Poll); St.Check = MoveTemp(Check);
		S->Steps.Add(MoveTemp(St));
	};
	// Шаг «новый моб на месте».
	auto AddMob = [S, Add](const FString& Name)
	{
		Add(Name, 5.f, [S]() { S->Calm(); S->NextMob(); }, [S]() { return S->El() > 0.6 && S->Mob() && !S->Mob()->IsDead(); }, [S]()
		{
			S->Expect(S->Mob() != nullptr && !S->Mob()->IsDead(), FString::Printf(TEXT("моб rid=%d на месте и жив"), S->Rid));
		});
	};
	// Шаг «состояние 296 при выбранной цели-мобе → эффект состояния на мобе помечен».
	auto AddState = [S, Add](const FString& Name)
	{
		Add(Name, 4.f, [S]()
		{
			S->Target();
			if (UAsdaEffectsSubsystem* E = S->FX()) { E->AddOrUpdateSkill(E->SelfUserIdx(), kState, 0, 0, 0, 120, 0); }
		}, [S]() { return S->Marked() > 0 && S->El() > 0.3; }, [S]()
		{
			S->Expect(S->Marked() > 0, FString::Printf(TEXT("эффект состояния %d родился на цели-мобе и помечен: помеченных %d, прочих %d"),
				kState, S->Marked(), S->Plain()));
		});
	};
	// Шаг «моб умирает» + проверка числа помеченных эффектов через 0.4 с.
	auto AddDeath = [S, Add](const FString& Name, bool bMustDie)
	{
		// Смерть — пакетом 4017 с нулевыми флагами (основной канал смерти моба); если пакет не убил за 1 с — напрямую, с записью
		//   в журнал. Проверка — через 0.12 с после того, как моб увиден мёртвым (два-три тика): так гашение правкой не спутать
		//   с естественным концом эффекта; перед смертью эффект обязан быть жив.
		Add(Name, 4.f, [S]()
		{
			S->MarkedBefore = S->Marked(); S->DeadSeenS = 0.0;
			TArray<uint8> D; D.SetNumZeroed(100); AsdaBytes::WrI16(&D[0], kMobType); AsdaBytes::WrI16(&D[2], S->Rid);
			S->Feed(4017, D);
		}, [S]()
		{
			AAsdaMobActor* M = S->Mob();
			if (!M) { return true; }
			if (!M->IsDead())
			{
				if (S->El() > 1.0 && !S->bFlag)
				{
					S->bFlag = true;
					UE_LOG(LogAsdaFxDeadHostTest, Log, TEXT("%s пакет 4017 моба не убил за 1 с — смерть подаётся напрямую"), S->Tag);
					M->NetDeath();
				}
				return false;
			}
			if (S->DeadSeenS <= 0.0) { S->DeadSeenS = FPlatformTime::Seconds(); }
			return FPlatformTime::Seconds() - S->DeadSeenS > 0.12;
		}, [S, bMustDie]()
		{
			const AAsdaMobActor* M = S->Mob();
			const int32 Mk = S->Marked();
			S->Expect(S->MarkedBefore > 0, FString::Printf(TEXT("перед смертью моба помеченный эффект был жив: %d"), S->MarkedBefore));
			S->Expect(M && M->IsDead(), FString::Printf(TEXT("моб мёртв, актор трупа ещё есть (смерть %s)"), S->bFlag ? TEXT("подана напрямую") : TEXT("от пакета 4017")));
			S->Expect(bMustDie ? (Mk == 0) : (Mk > 0), bMustDie
				? FString::Printf(TEXT("через 0.12 с после смерти помеченных эффектов над трупом %d (ждём 0 — погашены, как эталон)"), Mk)
				: FString::Printf(TEXT("через 0.12 с после смерти помеченных эффектов над трупом %d (ждём > 0 — прежнее поведение, дефект воспроизведён)"), Mk));
		});
	};

	Add(TEXT("подготовка"), 5.f, [S, Net]()
	{
		AAsdaPlayerCharacter* C = S->Ch();
		if (!C || !S->PC() || !S->WS()) { S->Expect(false, TEXT("нет своего персонажа/мира после FakeSelfSpawn")); return; }
		S->HomeTile = AsdaCoord::GameToTile(C->GetGamePosXZ());
		if (!Net->bSelfUserNumKnown) { Net->SelfUserNum = kSelfFallback; Net->bSelfUserNumKnown = true; }
		Net->bSelfDead = false;
		S->Self = (int32)Net->SelfUserNum;
		TWeakPtr<FState> WSp = S;
		S->Gen = AsdaSelfTestKit::ArmCapture(Net, [WSp](uint16 Op, const uint8*, int32, bool, TCHAR)
		{
			TSharedPtr<FState> St = WSp.Pin(); if (!St) { return; }
			St->Outs.AddDefaulted_GetRef().Op = Op;
		}, (uint16)0, S->Cmd);
		S->Expect(S->Gen != 0, TEXT("перехват исходящих взведён"));
		S->bWasInGame = Net->bInGameConnected; Net->bInGameConnected = true;
		S->WasWeaponSub = C->EquippedWeaponSubType; C->EquippedWeaponSubType = kStaff;
		S->bWasStand = Net->bStandCaptureSkillUse; Net->bStandCaptureSkillUse = true;
		S->SetKey(1);
	}, [S]() { return S->El() > 0.5; }, nullptr);

	// ── A. ключ 1: эффект состояния игрока на цели-мобе гаснет со смертью моба ──
	AddMob(TEXT("A моб"));
	AddState(TEXT("A состояние 296 на себе, цель — моб (ключ 1)"));
	AddDeath(TEXT("A моб умирает (ключ 1) — эффект состояния погашен"), true);

	// ── B. контроль: ключ 0 — прежнее поведение; ключ 1 на живой сцене — гаснет ──
	AddMob(TEXT("B моб"));
	Add(TEXT("B ключ 0"), 1.f, [S]() { S->SetKey(0); }, nullptr, nullptr);
	AddState(TEXT("B состояние 296 на себе, цель — моб (ключ 0)"));
	AddDeath(TEXT("B моб умирает (ключ 0) — эффект состояния жив над трупом"), false);
	// Шаг «ключ 1 на живой сцене»: эффект, висящий над трупом при ключе 0, гаснет за два-три тика после включения ключа.
	auto AddKeyOnLive = [S, Add](const FString& Name)
	{
		Add(Name, 3.f, [S]() { S->MarkedBefore = S->Marked(); S->SetKey(1); }, [S]() { return S->El() > 0.12; }, [S]()
		{
			S->Expect(S->MarkedBefore > 0 && S->Marked() == 0,
				FString::Printf(TEXT("помеченных эффектов над трупом до включения ключа %d (ждём > 0), через 0.12 с после — %d (ждём 0)"), S->MarkedBefore, S->Marked()));
		});
	};
	AddKeyOnLive(TEXT("B ключ 1 на живой сцене — эффект над трупом погашен"));

	// Шаг «фаза каста навыка 908 по мобу → запись фазы каста на мобе помечена».
	auto AddCast = [S, Add](const FString& Name, int32 Skill = kCastSkill)
	{
		Add(Name, 4.f, [S, Skill]()
		{
			S->Target();
			S->Feed(5257, Body5257(S->Self, Skill, S->Rid, 1, 900));
		}, [S]() { return S->Marked() > 0 && S->El() > 0.3; }, [S, Skill]()
		{
			S->Expect(S->Marked() > 0, FString::Printf(TEXT("запись фазы каста навыка %d родилась на цели-мобе и помечена: помеченных %d, прочих %d"),
				Skill, S->Marked(), S->Plain()));
		});
	};

	// ── C. ключ 1: запись фазы каста навыка 908 на цели-мобе гаснет со смертью моба; контроль ключа 0 ──
	AddMob(TEXT("C моб"));
	AddCast(TEXT("C фаза каста навыка 908 по мобу (ключ 1; успех 5257, удар не смертельный)"));
	AddDeath(TEXT("C моб умирает (ключ 1) — запись фазы каста погашена"), true);
	AddMob(TEXT("C контроль: моб"));
	Add(TEXT("C контроль: ключ 0"), 1.f, [S]() { S->SetKey(0); }, nullptr, nullptr);
	AddCast(TEXT("C контроль: фаза каста навыка 908 по мобу (ключ 0)"));
	AddDeath(TEXT("C контроль: моб умирает (ключ 0) — запись фазы каста жива над трупом"), false);
	AddKeyOnLive(TEXT("C контроль: ключ 1 на живой сцене — запись фазы каста погашена"));

	// ── C2. ★ R5-fix6: ключ 1 — запись фазы каста навыка 756 «Аркан» на цели-мобе гаснет со смертью моба ──
	AddMob(TEXT("C2 моб"));
	AddCast(TEXT("C2 фаза каста навыка 756 по мобу (ключ 1; успех 5257, удар не смертельный)"), kCastSkill2);
	AddDeath(TEXT("C2 моб умирает (ключ 1) — запись фазы каста навыка 756 погашена"), true);

	// ── D. ключ 1: эффект ветки удара (добивающий удар 906) не помечен и после смерти моба жив ──
	AddMob(TEXT("D моб"));
	Add(TEXT("D добивающий удар навыка 906 (5257, запись HP после = 0) — эффект удара жив после смерти моба"), 8.f, [S]()
	{
		S->Target();
		S->Feed(5257, Body5257(S->Self, kHitSkill, S->Rid, 9999, 0));
		// Смерть — как у сервера: 4017 с нулевыми флагами сразу за 5257 (лента попаданий придерживает её до кадра удара).
		TArray<uint8> D; D.SetNumZeroed(100); AsdaBytes::WrI16(&D[0], kMobType); AsdaBytes::WrI16(&D[2], S->Rid);
		S->Feed(4017, D);
	}, [S]()
	{
		// Ждём кадр удара: на мобе появился эффект ветки удара (по имени .jeb навыка 906), затем смерть моба от пакета. Если за
		//   1,5 с после кадра удара пакет моба не убил — убиваем сами (об этом пишется строка), чтобы проверка всё равно исполнилась.
		AAsdaMobActor* M = S->Mob();
		if (!M) { return false; }
		if (!S->bFlag)
		{
			if (S->PlainNamed(kHitJeb) <= 0) { return false; }
			S->bFlag = true; S->MarkS = FPlatformTime::Seconds();
			UE_LOG(LogAsdaFxDeadHostTest, Log, TEXT("%s D: кадр удара через %.2f с — эффектов удара «%s» на мобе %d, моб %s"), S->Tag, S->El(),
				kHitJeb, S->PlainNamed(kHitJeb), M->IsDead() ? TEXT("уже мёртв (смерть от пакета в кадре удара)") : TEXT("ещё жив"));
			return false;
		}
		if (!M->IsDead())
		{
			if (S->Since() <= 1.5) { return false; }
			UE_LOG(LogAsdaFxDeadHostTest, Log, TEXT("%s D: пакет смерти моба не убил за 1.5 с после кадра удара — смерть подаётся напрямую"), S->Tag);
			M->NetDeath();
			S->MarkS = FPlatformTime::Seconds();
			return false;
		}
		if (S->DeadSeenS <= 0.0) { S->DeadSeenS = FPlatformTime::Seconds(); S->HitFxAtDeath = S->PlainNamed(kHitJeb); }
		return FPlatformTime::Seconds() - S->DeadSeenS > 0.25;
	}, [S]()
	{
		const AAsdaMobActor* M = S->Mob();
		S->Expect(S->bFlag, FString::Printf(TEXT("кадр удара наступил: на мобе родился эффект ветки удара «%s»"), kHitJeb));
		S->Expect(M && M->IsDead(), TEXT("моб мёртв, актор трупа ещё есть"));
		S->Expect(S->HitFxAtDeath > 0, FString::Printf(TEXT("в кадре, где моб уже мёртв, эффектов удара на нём %d (ждём > 0)"), S->HitFxAtDeath));
		S->Expect(S->PlainNamed(kHitJeb) > 0 && S->Marked() == 0,
			FString::Printf(TEXT("через 0.25 с после смерти: эффектов удара «%s» %d (ждём > 0 — живы), помеченных %d (ждём 0 — удар не помечается)"),
				kHitJeb, S->PlainNamed(kHitJeb), S->Marked()));
	});

	Add(TEXT("итог"), 2.f, [S, Net]()
	{
		S->Calm();
		{ TArray<uint8> B; B.SetNumZeroed(2); AsdaBytes::WrI16(&B[0], S->Rid); S->Feed(4011, B); }
		if (AAsdaPlayerController* P = S->PC()) { P->SetTarget(nullptr); P->bAutoAttack = false; P->ExitCombatStance(); }
		if (AAsdaPlayerCharacter* C = S->Ch()) { C->EquippedWeaponSubType = S->WasWeaponSub; }
		Net->ClearMoveRequestInFlight(TEXT("самотест эффекта над трупом: итог"));
		Net->bStandCaptureSkillUse = S->bWasStand;
		AsdaSelfTestKit::DisarmCapture(Net, S->Gen);
		Net->bInGameConnected = S->bWasInGame;
		S->SelfScope.Reset();
	}, [S]() { return S->El() > 0.3; }, [S]()
	{
		AsdaSelfTestKit::Finish(S->World.Get(), S->Cmd, false, LogAsdaFxDeadHostTest, S->Tag, S->Ok, S->Fail);
	});
	RunSteps(S, W, KitArgs, Net);
}

static FAutoConsoleCommandWithWorldAndArgs GAsdaSelfTestFxDeadMobHost(
	TEXT("asda.Fx.SelfTestDeadMobHost"),
	TEXT("Самотест R5-fix5 «следящий эффект гаснет вместе с умершим мобом»: эффект состояния 296 на цели-мобе и запись фазы каста навыка 908 ")
	TEXT("гаснут со смертью моба (ключ asda.Fx.DieWithDeadMob 1), контроль ключа 0 воспроизводит эффект над трупом; эффект ветки удара ")
	TEXT("(добивающий удар 906) жив. Только офлайн. `delay=N` `quit`."),
	FConsoleCommandWithWorldAndArgsDelegate::CreateLambda([](const TArray<FString>& A, UWorld* W)
	{
		AsdaCVarSave::ArmWorldGuard(W, TEXT("asda.Fx.SelfTestDeadMobHost"));
		FAsdaFxDeadMobHostSelfTest::Run(A, W);
	}));

#endif // !UE_BUILD_SHIPPING
