// Copyright (c) 2026 @sitxovski (Discord)
// ★ САМОТЕСТ «эффект мгновенного состояния из пакетов HP 4058 / MP 4059» (R5-fix6, 13.11.2026): `asda.Fx.SelfTestVitalsState [delay=N] [quit]`.
//   Проверяет правку под ключом `asda.Fx.VitalsStateFx` (разбор — у `UAsdaEffectsSubsystem::OnVitalsStateFx`, AsdaEffectsSubsystem_Fx.cpp)
//   настоящим путём: входящие пакеты 4058/4059 подаются штатным приёмом (`DebugFeedPacket`), только офлайн (`-AsdaNoNet`);
//   свой персонаж — `Asda.World.FakeSelfSpawn`, моб-цель — запись 4013.
//   Шаги (эффект ищется среди акторов эффектов, РОДИВШИХСЯ после подачи пакета, по имени .jeb в имени класса и по носителю):
//     A) ключ 1, тип 4, игрок пакета — свой: 4058 с состоянием 276 → `hill01`; 267 → `bleedc01`; 275 → `manasu01`;
//        4059 с состоянием 268 → `bleedc01`; 269 → `hpreco01`; состояние вне шести (100) → новых эффектов нет;
//        296 при выбранной цели-мобе → `blosuc02` на себе и `blosuc01` на мобе;
//     B) ключ 1, тип 6 (носитель — игрок пакета, дефект оригинала исправлен): 4058 про себя → `hill01` на себе; про неизвестного
//        игрока → ничего; 4058 и 4059 про СОСЕДА → эффект на соседе, на себе — нет; ключ 2 (дословно оригинал): 4058 про соседа →
//        `hill01` на СЕБЕ, 4059 про неизвестного игрока → `hpreco01` на себе (у эталона без условия);
//     C) ключ 1, тип 1 (только цифра) → новых эффектов нет; игрок пакета неизвестен, тип 4 → ничего;
//     D) контроль, ключ 0: 4058 тип 4 состояние 276 и 4059 тип 6 → новых эффектов нет (прежнее поведение — только цифра).
//   Ничего не отправляет (исходящие перехвачены). Итог — «САМОТЕСТ-ЭФФЕКТ-HP-MP ИТОГ: ok=N fail=M».
#include "CoreMinimal.h"

#if !UE_BUILD_SHIPPING

#include "AsdaNetSubsystem.h"
#include "AsdaBytes.h"
#include "AsdaCVarSave.h"
#include "AsdaEffectActor.h"
#include "Test/AsdaSelfTestRunGate.h"
#include "AsdaTestPackets.h"
#include "Test/AsdaSelfTestKit.h"
#include "UI/AsdaEffectsSubsystem.h"
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

DEFINE_LOG_CATEGORY_STATIC(LogAsdaVitalsFxTest, Log, All);

namespace AsdaVitalsFxTestLocal
{
	constexpr int32 kSelfFallback = 4322;    // свой userNum, если не известен
	constexpr int32 kMobType = 1;            // волк alpen (как у прочих самотестов)
	constexpr int32 kSpdDenom = 250;         // знаменатель скорости 4013
	constexpr int32 kRid = 31451;            // моб-цель шага 296
	constexpr int32 kUnknownUser = 7777;     // игрок, которого в мире нет
	constexpr int32 kPeer = 3142;            // сосед: чужой пакет типа 6 играет эффект на нём, а не на своём персонаже
	constexpr int32 kHpMax = 1000;           // максимум HP в пакетах теста
	constexpr int32 kMpMax = 500;            // максимум MP в пакетах теста

	// Шаг сценария: старт, опрос «готово», проверка.
	struct FStep
	{
		FString Name; float TimeoutSec = 10.f;
		TFunction<void()> Start; TFunction<bool()> Poll; TFunction<void()> Check;
	};

	// 4058 (17 б): [0..1] userIdx, [2..5] максимум HP, [6..9] текущее HP, [10] тип обновления, [11..12] id состояния, [13..16] второе HP.
	static TArray<uint8> Body4058(int32 Uidx, int32 CurHp, uint8 UpdT, int32 StateId)
	{
		TArray<uint8> B; B.SetNumZeroed(17);
		AsdaBytes::WrI16(&B[0], Uidx); AsdaBytes::WrI32(&B[2], kHpMax); AsdaBytes::WrI32(&B[6], CurHp); B[10] = UpdT;
		AsdaBytes::WrI16(&B[11], StateId); AsdaBytes::WrI32(&B[13], CurHp);
		return B;
	}
	// 4059 (11 б): [0..1] userIdx, [2..3] максимум MP, [4..5] текущее MP, [6] тип обновления, [7..8] id состояния, [9..10] второе MP.
	static TArray<uint8> Body4059(int32 Uidx, int32 CurMp, uint8 UpdT, int32 StateId)
	{
		TArray<uint8> B; B.SetNumZeroed(11);
		AsdaBytes::WrI16(&B[0], Uidx); AsdaBytes::WrI16(&B[2], kMpMax); AsdaBytes::WrI16(&B[4], CurMp); B[6] = UpdT;
		AsdaBytes::WrI16(&B[7], StateId); AsdaBytes::WrI16(&B[9], CurMp);
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
		uint32 Gen = 0;                                           // поколение перехвата исходящих
		TUniquePtr<FAsdaNetSelfScope> SelfScope;                  // «свой персонаж известен» на время прогона
		bool bWasInGame = false;                                  // прежний флаг «в игре»
		bool bWasSelfDead = false;                                // прежний флаг «свой мёртв»
		bool bSelfDeadSaved = false;                              // R5-fix7 (ревизия): флаг «свой мёртв» снят — его можно возвращать
		bool bHadSelfVitals = false; FAsdaVitals SelfVitalsWas;   // HP/MP своего персонажа до прогона (вернуть)
		int32 Self = -1; FIntPoint HomeTile = FIntPoint::ZeroValue;   // свой userNum; тайл своего персонажа
		TSet<TWeakObjectPtr<AAsdaEffectActor>> Before;            // акторы эффектов, жившие до подачи пакета шага
		int32 Digits0 = 0;                                        // ★ R5-fix8: счётчик показанных цифр дельты на начало шага
		int32 Hp = 500, Mp = 250;                                 // значения HP/MP следующего пакета (меняются, чтобы дельта была не ноль)

		// Короткие доступы: сеть, экземпляр игры, подсистемы мира / состояний, свой контроллер и персонаж, моб шага 296.
		UAsdaNetSubsystem* N() const { return Net.Get(); }
		UGameInstance* GI() const { UAsdaNetSubsystem* Nn = N(); return Nn ? Nn->GetGameInstance() : nullptr; }
		UAsdaWorldSubsystem* WS() const { UGameInstance* G = GI(); return G ? G->GetSubsystem<UAsdaWorldSubsystem>() : nullptr; }
		UAsdaEffectsSubsystem* FX() const { UGameInstance* G = GI(); return G ? G->GetSubsystem<UAsdaEffectsSubsystem>() : nullptr; }
		AAsdaPlayerController* PC() const { UWorld* W = World.Get(); return W ? Cast<AAsdaPlayerController>(W->GetFirstPlayerController()) : nullptr; }
		AAsdaPlayerCharacter* Ch() const { AAsdaPlayerController* P = PC(); return P ? Cast<AAsdaPlayerCharacter>(P->GetPawn()) : nullptr; }
		AAsdaMobActor* Mob() const { const UAsdaWorldSubsystem* W = WS(); return W ? W->FindNetMobByRid(kRid) : nullptr; }
		AActor* Peer() const { const UAsdaWorldSubsystem* W = WS(); return W ? W->ActorByUserIdx(kPeer) : nullptr; }   // сосед (чужой игрок)
		double El() const { return FPlatformTime::Seconds() - StepStartS; }    // секунд с начала шага
		// Проверка с записью в журнал («OK» / «ПРОВАЛ» + имя шага).
		void Expect(bool bOk, const FString& What)
		{
			if (bOk) { ++Ok; } else { ++Fail; }
			UE_LOG(LogAsdaVitalsFxTest, Log, TEXT("%s %s %s: %s"), Tag, bOk ? TEXT("OK") : TEXT("ПРОВАЛ"),
				Steps.IsValidIndex(StepIdx) ? *Steps[StepIdx].Name : TEXT("?"), *What);
		}
		void Feed(int32 Op, const TArray<uint8>& B) { if (UAsdaNetSubsystem* Nn = N()) { Nn->DebugFeedPacket(Op, B); } }   // входящий пакет штатным приёмом
		void SetKey(int32 V) { AsdaCVarSave::SetForTest(TEXT("asda.Fx.VitalsStateFx"), V); }   // ключ правки; исходное вернёт `Finish`
		// Запомнить живые акторы эффектов — всё, что родится позже, считается «новым».
		void Snapshot()
		{
			Before.Reset();
			if (UWorld* W = World.Get()) { for (TActorIterator<AAsdaEffectActor> It(W); It; ++It) { Before.Add(*It); } }
		}
		// Новые (после `Snapshot`) живые эффекты на носителе Host (nullptr — на любом), в имени класса которых есть Sub (nullptr — любые).
		int32 NewFx(const AActor* Host, const TCHAR* Sub) const
		{
			int32 Cnt = 0;
			UWorld* W = World.Get();
			if (!W) { return 0; }
			for (TActorIterator<AAsdaEffectActor> It(W); It; ++It)
			{
				AAsdaEffectActor* E = *It;
				if (!IsValid(E) || E->IsActorBeingDestroyed() || Before.Contains(E)) { continue; }
				if (Host && E->FollowActor != Host && E->GetOwner() != Host) { continue; }
				if (Sub && !E->GetClass()->GetName().Contains(Sub)) { continue; }
				++Cnt;
			}
			return Cnt;
		}
		// Следующие значения HP / MP пакета: каждое отличается от прежнего (цифра дельты рисуется, как в игре).
		int32 NextHp() { Hp = (Hp == 500) ? 600 : 500; return Hp; }
		int32 NextMp() { Mp = (Mp == 250) ? 300 : 250; return Mp; }
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
				St->bStepStarted = true; St->StepStartS = Now;
				UE_LOG(LogAsdaVitalsFxTest, Log, TEXT("%s шаг %s"), St->Tag, *Stp.Name);
				if (Stp.Start) { Stp.Start(); }
				return;
			}
			const double El = Now - St->StepStartS;
			const bool bDone = !Stp.Poll || Stp.Poll();
			if (!bDone && El < Stp.TimeoutSec) { return; }
			if (Stp.Check) { Stp.Check(); }
			++St->StepIdx; St->bStepStarted = false;
		}), 0.05f, true, KitArgs.Delay + 2.5f);
	}
}

// Самотест «эффект мгновенного состояния из пакетов HP 4058 / MP 4059» — запуск и сценарий.
struct FAsdaVitalsStateFxSelfTest
{
	// Запуск: разбор аргументов (`delay=N`, `quit`), сборка шагов A–D и их исполнение по таймеру.
	static void Run(const TArray<FString>& Args, UWorld* W);
};

void FAsdaVitalsStateFxSelfTest::Run(const TArray<FString>& Args, UWorld* W)
{
	using namespace AsdaVitalsFxTestLocal;
	const TCHAR* const kCmd = TEXT("asda.Fx.SelfTestVitalsState");
	const FAsdaSelfTestArgs KitArgs = FAsdaSelfTestArgs::Parse(Args);
	static const uint16 kGuardOps[] = { 4006, 4008 };   // опкоды, которые самотест не должен выпустить в сеть
	if (AsdaSelfTestKit::RefuseStart(W, kCmd, KitArgs, 0.f, MakeArrayView(kGuardOps))) { return; }
	UGameInstance* GIw = W ? W->GetGameInstance() : nullptr;
	UAsdaNetSubsystem* Net = GIw ? GIw->GetSubsystem<UAsdaNetSubsystem>() : nullptr;
	if (!Net) { AsdaSelfTestKit::Refused(W, kCmd, KitArgs.bQuit, TEXT("нет подсистемы сети")); return; }

	TSharedRef<FState> S = MakeShared<FState>();
	S->Cmd = kCmd; S->Tag = TEXT("САМОТЕСТ-ЭФФЕКТ-HP-MP");
	S->Net = Net; S->World = W; S->bQuit = KitArgs.bQuit; S->Gate = FAsdaSelfTestRunGate{ kCmd, KitArgs.bQuit };
	S->SelfScope = MakeUnique<FAsdaNetSelfScope>(Net, kCmd);

	// Добавить шаг сценария.
	auto Add = [S](const FString& Name, float Timeout, TFunction<void()> Start, TFunction<bool()> Poll, TFunction<void()> Check)
	{
		FStep St; St.Name = Name; St.TimeoutSec = Timeout; St.Start = MoveTemp(Start); St.Poll = MoveTemp(Poll); St.Check = MoveTemp(Check);
		S->Steps.Add(MoveTemp(St));
	};
	// Шаг «пакет → на своём персонаже родился эффект с именем Sub»: ждём события (до 3 с), а не времени.
	//   bMp — пакет 4059; Uidx < 0 — пакет про себя.
	auto AddExpectFx = [S, Add](const FString& Name, bool bMp, int32 Uidx, uint8 UpdT, int32 StateId, const TCHAR* Sub)
	{
		Add(Name, 3.f, [S, bMp, Uidx, UpdT, StateId]()
		{
			S->Snapshot();
			const int32 U = Uidx < 0 ? S->Self : Uidx;
			if (bMp) { S->Feed(4059, Body4059(U, S->NextMp(), UpdT, StateId)); } else { S->Feed(4058, Body4058(U, S->NextHp(), UpdT, StateId)); }
		}, [S, Sub]() { return S->NewFx(S->Ch(), Sub) > 0; }, [S, bMp, UpdT, StateId, Sub]()
		{
			S->Expect(S->NewFx(S->Ch(), Sub) > 0, FString::Printf(TEXT("пакет %d тип %d состояние %d: на своём персонаже родился эффект «%s» (новых с этим именем %d, новых всего %d)"),
				bMp ? 4059 : 4058, (int32)UpdT, StateId, Sub, S->NewFx(S->Ch(), Sub), S->NewFx(nullptr, nullptr)));
		});
	};
	// Шаг «пакет → новых эффектов нет»: 0.5 с после подачи (эффект рождается в том же вызове приёма — запас на кадры).
	auto AddExpectNone = [S, Add](const FString& Name, bool bMp, int32 Uidx, uint8 UpdT, int32 StateId)
	{
		Add(Name, 3.f, [S, bMp, Uidx, UpdT, StateId]()
		{
			S->Snapshot();
			const int32 U = Uidx < 0 ? S->Self : Uidx;
			if (bMp) { S->Feed(4059, Body4059(U, S->NextMp(), UpdT, StateId)); } else { S->Feed(4058, Body4058(U, S->NextHp(), UpdT, StateId)); }
		}, [S]() { return S->El() > 0.5; }, [S, bMp, UpdT, StateId]()
		{
			// Только эффекты на своём персонаже и на мобе-цели: эффекты карты рождаются сами и к пакету не относятся.
			const int32 NewOnMe = S->NewFx(S->Ch(), nullptr), NewOnMob = S->Mob() ? S->NewFx(S->Mob(), nullptr) : 0;
			S->Expect(NewOnMe == 0 && NewOnMob == 0, FString::Printf(TEXT("пакет %d тип %d состояние %d: новых эффектов на своём %d, на мобе %d (ждём 0 и 0)"),
				bMp ? 4059 : 4058, (int32)UpdT, StateId, NewOnMe, NewOnMob));
		});
	};

	Add(TEXT("подготовка"), 5.f, [S, Net]()
	{
		AAsdaPlayerCharacter* C = S->Ch();
		if (!C || !S->PC() || !S->WS() || !S->FX()) { S->Expect(false, TEXT("нет своего персонажа/мира после FakeSelfSpawn")); return; }
		S->HomeTile = AsdaCoord::GameToTile(C->GetGamePosXZ());
		if (!Net->bSelfUserNumKnown) { Net->SelfUserNum = kSelfFallback; Net->bSelfUserNumKnown = true; }
		S->bWasSelfDead = Net->bSelfDead; S->bSelfDeadSaved = true; Net->bSelfDead = false;
		S->Self = (int32)Net->SelfUserNum;
		if (const FAsdaVitals* V0 = Net->GetVitals(S->Self)) { S->bHadSelfVitals = true; S->SelfVitalsWas = *V0; }
		S->Gen = AsdaSelfTestKit::ArmCapture(Net, [](uint16, const uint8*, int32, bool, TCHAR) {}, (uint16)0, S->Cmd);
		S->Expect(S->Gen != 0, TEXT("перехват исходящих взведён"));
		S->bWasInGame = Net->bInGameConnected; Net->bInGameConnected = true;
		S->SetKey(1);
		// Исходные HP/MP своего персонажа известны — первая дельта не ноль.
		S->Feed(4058, Body4058(S->Self, 400, 0, 0));
		S->Feed(4059, Body4059(S->Self, 200, 0, 0));
	}, [S]() { return S->El() > 0.5; }, nullptr);

	// ── A. ключ 1, тип 4: шесть состояний эталона у игрока пакета ──
	AddExpectFx(TEXT("A1 4058 тип 4, состояние 276 «Лечение»"), false, -1, 4, 276, TEXT("hill01"));
	AddExpectFx(TEXT("A2 4058 тип 4, состояние 267 «Мгновенное понижение здоровья»"), false, -1, 4, 267, TEXT("bleedc01"));
	AddExpectFx(TEXT("A3 4058 тип 4, состояние 275 «Ментальный шок»"), false, -1, 4, 275, TEXT("manasu01"));
	AddExpectFx(TEXT("A4 4059 тип 4, состояние 268 «Мгновенное понижение маны»"), true, -1, 4, 268, TEXT("bleedc01"));
	AddExpectFx(TEXT("A5 4059 тип 4, состояние 269 «Мгновенное увеличение маны»"), true, -1, 4, 269, TEXT("hpreco01"));
	AddExpectNone(TEXT("A6 4058 тип 4, состояние 100 — вне шести состояний эталона"), false, -1, 4, 100);
	Add(TEXT("A7 моб-цель"), 5.f, [S]()
	{
		S->Feed(4013, AsdaTestPackets::MobSpawn4013(kRid, kMobType, S->HomeTile + FIntPoint(4, 0), 1, kSpdDenom));
	}, [S]() { return S->El() > 0.6 && S->Mob() && !S->Mob()->IsDead(); }, [S]()
	{
		S->Expect(S->Mob() != nullptr, FString::Printf(TEXT("моб rid=%d на месте"), kRid));
		if (AAsdaPlayerController* P = S->PC()) { P->SetTarget(S->Mob()); }
	});
	Add(TEXT("A8 4058 тип 4, состояние 296 «Похищение крови» при выбранной цели-мобе"), 3.f, [S]()
	{
		S->Snapshot();
		S->Feed(4058, Body4058(S->Self, S->NextHp(), 4, 296));
	}, [S]() { return S->NewFx(S->Ch(), TEXT("blosuc02")) > 0 && S->NewFx(S->Mob(), TEXT("blosuc01")) > 0; }, [S]()
	{
		S->Expect(S->NewFx(S->Ch(), TEXT("blosuc02")) > 0, FString::Printf(TEXT("на своём персонаже родился «blosuc02»: %d"), S->NewFx(S->Ch(), TEXT("blosuc02"))));
		S->Expect(S->NewFx(S->Mob(), TEXT("blosuc01")) > 0, FString::Printf(TEXT("на цели-мобе родился «blosuc01»: %d"), S->NewFx(S->Mob(), TEXT("blosuc01"))));
	});

	// ── B. ключ 1, тип 6: эффект на своём персонаже ──
	AddExpectFx(TEXT("B1 4058 тип 6 про себя — «Лечение» на своём"), false, -1, 6, 0, TEXT("hill01"));
	AddExpectNone(TEXT("B2 4058 тип 6 про неизвестного игрока — ничего (игрока пакета нет)"), false, kUnknownUser, 6, 0);
	AddExpectNone(TEXT("B3 4059 тип 6 про неизвестного игрока — ничего (ключ 1: носитель — игрок пакета)"), true, kUnknownUser, 6, 0);
	// Сосед: чужой пакет типа 6 (сервер шлёт его рассылкой по сектору) — эффект на соседе, не на своём персонаже.
	Add(TEXT("B4 сосед"), 5.f, [S]()
	{
		if (UAsdaWorldSubsystem* Ws = S->WS()) { Ws->DebugSpawnFakePeer(kPeer, TEXT("Сосед"), AsdaCoord::TileCenterGame(S->HomeTile.X - 3, S->HomeTile.Y)); }
	}, [S]() { return S->El() > 0.6 && S->Peer(); }, [S]() { S->Expect(S->Peer() != nullptr, FString::Printf(TEXT("сосед userIdx=%d на месте"), kPeer)); });
	// Шаг «пакет типа 6 про соседа»: bMp — пакет 4059; bOnPeer — ждём эффект на соседе (ключ 1) либо на своём (ключ 2).
	auto AddPeer6 = [S, Add](const FString& Name, bool bMp, bool bOnPeer, const TCHAR* Sub)
	{
		Add(Name, 3.f, [S, bMp]()
		{
			S->Snapshot();
			if (bMp) { S->Feed(4059, Body4059(kPeer, S->NextMp(), 6, 0)); } else { S->Feed(4058, Body4058(kPeer, S->NextHp(), 6, 0)); }
		}, [S]() { return S->El() > 0.5; }, [S, bMp, bOnPeer, Sub]()
		{
			const int32 OnPeer = S->NewFx(S->Peer(), Sub), OnMe = S->NewFx(S->Ch(), nullptr), OnMeSub = S->NewFx(S->Ch(), Sub);
			S->Expect(bOnPeer ? (OnPeer > 0 && OnMe == 0) : (OnPeer == 0 && OnMeSub > 0),
				FString::Printf(TEXT("пакет %d тип 6 про соседа: эффектов «%s» на соседе %d, на своём %d, новых эффектов на своём всего %d (ждём: %s)"),
					bMp ? 4059 : 4058, Sub, OnPeer, OnMeSub, OnMe, bOnPeer ? TEXT("на соседе есть, на своём нет") : TEXT("на соседе нет, на своём есть — дословно оригинал")));
		});
	};
	AddPeer6(TEXT("B5 ключ 1: 4058 тип 6 про соседа — «hill01» на соседе"), false, true, TEXT("hill01"));
	AddPeer6(TEXT("B6 ключ 1: 4059 тип 6 про соседа — «hpreco01» на соседе"), true, true, TEXT("hpreco01"));
	Add(TEXT("B ключ 2 (дословно оригинал)"), 1.f, [S]() { S->SetKey(2); }, nullptr, nullptr);
	AddPeer6(TEXT("B7 ключ 2: 4058 тип 6 про соседа — «hill01» на СВОЁМ (дефект оригинала воспроизведён)"), false, false, TEXT("hill01"));
	AddExpectFx(TEXT("B8 ключ 2: 4059 тип 6 про неизвестного игрока — «hpreco01» на своём (у оригинала без условия)"), true, kUnknownUser, 6, 0, TEXT("hpreco01"));
	Add(TEXT("B ключ 1"), 1.f, [S]() { S->SetKey(1); }, nullptr, nullptr);

	// ── C. ключ 1: тип 1 — только цифра; игрок пакета неизвестен при типе 4 — ничего ──
	AddExpectNone(TEXT("C1 4058 тип 1 (цифра без эффекта)"), false, -1, 1, 276);
	AddExpectNone(TEXT("C2 4058 тип 4, состояние 276, игрок пакета неизвестен"), false, kUnknownUser, 4, 276);

	// ── E. ★ R5-fix8 (14.11.2026): цифры дельты — НЫНЕШНЕЕ ПОВЕДЕНИЕ ПОРТА (осознанное расхождение с оригиналом: «наше лучше»,
	//   слово владельца 14.11); значения ключей `asda.Vitals.MpDigit` 0 / `asda.Vitals.HpZeroDigit` 1 — как оригинал (контроль) ──
	//   Шаг «пакет → число показанных цифр дельты»: bMp — пакет 4059; bSame — то же значение, что в прошлом пакете (дельта 0).
	auto AddDigit = [S, Add](const FString& Name, bool bMp, uint8 UpdT, bool bSame, const TCHAR* Key, int32 KeyVal, int32 Want)
	{
		Add(Name, 3.f, [S, bMp, UpdT, bSame, Key, KeyVal]()
		{
			AsdaCVarSave::SetForTest(Key, KeyVal);
			S->Digits0 = S->WS() ? S->WS()->DebugVitalsDigits : 0;
			if (bMp) { S->Feed(4059, Body4059(S->Self, bSame ? S->Mp : S->NextMp(), UpdT, 0)); }
			else { S->Feed(4058, Body4058(S->Self, bSame ? S->Hp : S->NextHp(), UpdT, 0)); }
		}, [S]() { return S->El() > 0.3; }, [S, bMp, UpdT, bSame, Key, KeyVal, Want]()
		{
			const int32 Got = (S->WS() ? S->WS()->DebugVitalsDigits : 0) - S->Digits0;
			S->Expect(Got == Want, FString::Printf(TEXT("пакет %d тип %d, дельта %s, ключ %s %d: показано цифр %d (ждём %d)"),
				bMp ? 4059 : 4058, (int32)UpdT, bSame ? TEXT("0") : TEXT("не 0"), Key, KeyVal, Got, Want));
		});
	};
	// Подготовка: значения HP/MP теста общие с шагами по соседу — известное игре значение своего персонажа задаётся тихим
	//   пакетом (тип 0, цифры нет), иначе дельта первого шага не та, что названа в шаге.
	Add(TEXT("E подготовка: своя мана известна (тихий пакет 4059)"), 1.f, [S]() { S->Feed(4059, Body4059(S->Self, S->NextMp(), 0, 0)); }, nullptr, nullptr);
	AddDigit(TEXT("E1 мана 4059 тип 4: цифра есть (порт, ключ по умолчанию 1)"), true, 4, false, TEXT("asda.Vitals.MpDigit"), 1, 1);
	AddDigit(TEXT("E2 мана 4059 тип 6: цифра есть (порт, ключ по умолчанию 1)"), true, 6, false, TEXT("asda.Vitals.MpDigit"), 1, 1);
	AddDigit(TEXT("E3 контроль ключа 0: мана 4059 тип 4 — цифры нет (как оригинал)"), true, 4, false, TEXT("asda.Vitals.MpDigit"), 0, 0);
	Add(TEXT("E ключ MpDigit 1"), 1.f, [S]() { AsdaCVarSave::SetForTest(TEXT("asda.Vitals.MpDigit"), 1); }, nullptr, nullptr);
	Add(TEXT("E подготовка: своё здоровье известно (тихий пакет 4058)"), 1.f, [S]() { S->Feed(4058, Body4058(S->Self, S->Hp, 0, 0)); }, nullptr, nullptr);
	AddDigit(TEXT("E4 здоровье 4058 тип 1, дельта 0: цифры нет (порт, ключ по умолчанию 0)"), false, 1, true, TEXT("asda.Vitals.HpZeroDigit"), 0, 0);
	AddDigit(TEXT("E5 здоровье 4058 тип 4, дельта 0: цифры нет (порт, ключ по умолчанию 0)"), false, 4, true, TEXT("asda.Vitals.HpZeroDigit"), 0, 0);
	AddDigit(TEXT("E6 контроль ключа 1: здоровье 4058 тип 1, дельта 0 — цифра «0» есть (как оригинал)"), false, 1, true, TEXT("asda.Vitals.HpZeroDigit"), 1, 1);
	AddDigit(TEXT("E7 здоровье 4058 тип 1, дельта не 0: цифра есть при любом ключе"), false, 1, false, TEXT("asda.Vitals.HpZeroDigit"), 0, 1);
	AddDigit(TEXT("E8 здоровье 4058 тип 0, дельта 0: цифры нет при любом ключе (тихая запись)"), false, 0, true, TEXT("asda.Vitals.HpZeroDigit"), 1, 0);
	Add(TEXT("E ключ HpZeroDigit 0"), 1.f, [S]() { AsdaCVarSave::SetForTest(TEXT("asda.Vitals.HpZeroDigit"), 0); }, nullptr, nullptr);

	// ── D. контроль, ключ 0: прежнее поведение — эффектов нет ──
	Add(TEXT("D ключ 0"), 1.f, [S]() { S->SetKey(0); }, nullptr, nullptr);
	AddExpectNone(TEXT("D1 ключ 0: 4058 тип 4, состояние 276 — эффекта нет (как было)"), false, -1, 4, 276);
	AddExpectNone(TEXT("D2 ключ 0: 4059 тип 6 — эффекта нет (как было)"), true, -1, 6, 0);
	Add(TEXT("D ключ 1"), 1.f, [S]() { S->SetKey(1); }, nullptr, nullptr);
	AddExpectFx(TEXT("D3 ключ 1 снова: 4058 тип 4, состояние 276 — эффект есть"), false, -1, 4, 276, TEXT("hill01"));

	Add(TEXT("итог"), 2.f, [S, Net]()
	{
		{ TArray<uint8> B; B.SetNumZeroed(2); AsdaBytes::WrI16(&B[0], kRid); S->Feed(4011, B); }
		{ TArray<uint8> B; B.SetNumZeroed(2); AsdaBytes::WrI16(&B[0], kPeer); S->Feed(4011, B); }   // сосед шага B
		if (AAsdaPlayerController* P = S->PC()) { P->SetTarget(nullptr); }
		// R5-fix7 (14.11.2026) [ИСПРАВЛЕНИЕ ТЕСТА]: пакет 4011 удаляет только игроков (оригинал `Recv_LogOutPacket@0xF3AE00`) — тестовый моб оставался в мире и доставался следующему тесту той же игры; актор удаляется явно.
		if (AAsdaMobActor* Mm = S->Mob()) { Mm->Destroy(); }
		AsdaSelfTestKit::DisarmCapture(Net, S->Gen);
		Net->bInGameConnected = S->bWasInGame;
		// ★ R5-fix7: за собой — записи HP/MP тестовых игроков сняты, свои HP/MP и флаг смерти возвращены.
		Net->VitalsByUserIdx.Remove(kUnknownUser);
		Net->VitalsByUserIdx.Remove(kPeer);
		if (S->bHadSelfVitals) { Net->VitalsByUserIdx.Add(S->Self, S->SelfVitalsWas); Net->OnVitalsChanged.Broadcast(S->Self, S->SelfVitalsWas); }
		else { Net->VitalsByUserIdx.Remove(S->Self); }
		if (S->bSelfDeadSaved) { Net->bSelfDead = S->bWasSelfDead; }   // ревизия: возвращать только снятое
		S->SelfScope.Reset();
	}, [S]() { return S->El() > 0.3; }, [S]()
	{
		AsdaSelfTestKit::Finish(S->World.Get(), S->Cmd, false, LogAsdaVitalsFxTest, S->Tag, S->Ok, S->Fail);
	});
	RunSteps(S, W, KitArgs, Net);
}

static FAutoConsoleCommandWithWorldAndArgs GAsdaSelfTestFxVitalsState(
	TEXT("asda.Fx.SelfTestVitalsState"),
	TEXT("Самотест R5-fix6 «эффект мгновенного состояния из пакетов HP 4058 / MP 4059»: тип 4 — состояния 267/268/269/275/276/296 у игрока ")
	TEXT("пакета, тип 6 — лечение/мана на своём персонаже (ключ asda.Fx.VitalsStateFx 1); контроль ключа 0 — эффектов нет. ")
	TEXT("Только офлайн. `delay=N` `quit`."),
	FConsoleCommandWithWorldAndArgsDelegate::CreateLambda([](const TArray<FString>& A, UWorld* W)
	{
		AsdaCVarSave::ArmWorldGuard(W, TEXT("asda.Fx.SelfTestVitalsState"));
		FAsdaVitalsStateFxSelfTest::Run(A, W);
	}));

#endif // !UE_BUILD_SHIPPING
