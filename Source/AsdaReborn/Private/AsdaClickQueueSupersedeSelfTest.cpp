// Copyright (c) 2026 @sitxovski (Discord)
// ★ САМОТЕСТ «ход клавишами вытесняет отложенный клик хода» (R5-fix6, 13.11.2026): `asda.Input.SelfTestClickQueueSupersede [delay=N] [quit]`.
//   Проверяет правку под ключом `asda.Input.ClickQueueSupersede` (разбор — у `AsdaSkillCastLock::SupersedeQueuedMove`,
//   AsdaSkillCastLock.cpp) настоящим путём: ход кликом (`RequestMoveTo`) оставляет запрос 4006 без ответа — защита от флуда
//   закрыта; второй клик хода встаёт в очередь (`QueueGroundMove`); клавиша W — штатным вводом контроллера. Только офлайн.
//   Шаги:
//     A) ключ 1: отложенный клик хода стоит в очереди; нажата W → очередь пуста; после ответа сервера и отпускания W пакета 4006
//        в точку отложенного клика нет;
//     B) контроль, ключ 0: нажатие W очередь не трогает; после ответа и отпускания W уходит 4006 в точку старого клика (дефект виден);
//     C) ключ 1, без клавиш: отложенный клик исполняется, как прежде (очередь работает).
//   Ничего не отправляет (исходящие перехвачены). Итог — «САМОТЕСТ-ОЧЕРЕДЬ-КЛИКА ИТОГ: ok=N fail=M».
#include "CoreMinimal.h"

#if !UE_BUILD_SHIPPING

#include "AsdaNetSubsystem.h"
#include "AsdaBytes.h"
#include "AsdaCVarSave.h"
#include "Test/AsdaSelfTestRunGate.h"
#include "Test/AsdaSelfTestKit.h"
#include "Test/AsdaSelfTestInput.h"
#include "World/AsdaSkillCastLock.h"
#include "World/AsdaPlayerController.h"
#include "World/AsdaPlayerCharacter.h"
#include "World/AsdaCoord.h"
#include "Engine/Engine.h"
#include "Engine/GameInstance.h"
#include "Engine/World.h"
#include "GameFramework/PlayerController.h"
#include "HAL/IConsoleManager.h"
#include "TimerManager.h"

DEFINE_LOG_CATEGORY_STATIC(LogAsdaClickQueueSupersedeTest, Log, All);

namespace AsdaClickQueueSupersedeTestLocal
{
	constexpr int32 kSelfFallback = 4322;   // свой userNum, если не известен
	constexpr double kNear = 150.0;         // «в точку отложенного клика»: расстояние цели 4006 до неё, игровых единиц

	// Шаг сценария: старт, опрос «готово», проверка.
	struct FStep
	{
		FString Name; float TimeoutSec = 10.f;
		TFunction<void()> Start; TFunction<bool()> Poll; TFunction<void()> Check;
	};

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
		TArray<FVector2D> Dst4006;                                // цели перехваченных 4006 (игровые x, z)
		int32 Mark = 0;                                           // отметка в `Dst4006` на начало шага
		TUniquePtr<FAsdaNetSelfScope> SelfScope;                  // «свой персонаж» сети на время прогона
		bool bWasInGame = false;                                  // прежний флаг «в игре»
		FVector2D Home = FVector2D::ZeroVector;                   // точка своего персонажа на старте
		FVector2D Queued = FVector2D::ZeroVector;                 // точка отложенного клика (игровая)
		int32 Dropped0 = 0, Exec0 = 0;                            // счётчики очереди на начало шага
		int32 PendingBeforeKey = 0, PendingAfterKey = 0;          // вид отложенного действия до и после нажатия W

		// Короткие доступы: сеть, свой контроллер и персонаж.
		UAsdaNetSubsystem* N() const { return Net.Get(); }
		AAsdaPlayerController* PC() const { UWorld* W = World.Get(); return W ? Cast<AAsdaPlayerController>(W->GetFirstPlayerController()) : nullptr; }
		AAsdaPlayerCharacter* Ch() const { AAsdaPlayerController* P = PC(); return P ? Cast<AAsdaPlayerCharacter>(P->GetPawn()) : nullptr; }
		double El() const { return FPlatformTime::Seconds() - StepStartS; }   // секунд с начала шага
		// Проверка с записью в журнал («OK» / «ПРОВАЛ» + имя шага).
		void Expect(bool bOk, const FString& What)
		{
			if (bOk) { ++Ok; } else { ++Fail; }
			UE_LOG(LogAsdaClickQueueSupersedeTest, Log, TEXT("%s %s %s: %s"), Tag, bOk ? TEXT("OK") : TEXT("ПРОВАЛ"),
				Steps.IsValidIndex(StepIdx) ? *Steps[StepIdx].Name : TEXT("?"), *What);
		}
		void SetKey(int32 V) { AsdaCVarSave::SetForTest(TEXT("asda.Input.ClickQueueSupersede"), V); }   // ключ правки; исходное вернёт `Finish`
		// Сколько 4006 после отметки шага ушло в точку отложенного клика.
		int32 SentToQueued() const
		{
			int32 Cnt = 0;
			for (int32 i = Mark; i < Dst4006.Num(); ++i) { if (FVector2D::Distance(Dst4006[i], Queued) < kNear) { ++Cnt; } }
			return Cnt;
		}
		// Исходное состояние шага: персонаж стоит дома, клавиши отпущены, очередь и запрос хода сняты.
		void Calm()
		{
			// W здесь не отпускается: отпускание и нажатие в одном кадре движок сводит к «не нажата» (нажатие шага потерялось бы).
			AsdaSkillCastLock::Reset(TEXT("самотест очереди клика: исходное состояние"));
			if (UAsdaNetSubsystem* Nn = N()) { Nn->ClearMoveRequestInFlight(TEXT("самотест очереди клика: исходное состояние")); }
			if (AAsdaPlayerCharacter* C = Ch()) { C->StopMove(); C->NetSnap(Home); }
		}
		// Ход кликом (запрос 4006 остаётся без ответа — защита от флуда закрыта) и второй клик хода в очередь.
		void ClickAndQueue()
		{
			AAsdaPlayerController* P = PC();
			if (!P) { return; }
			Mark = Dst4006.Num();
			Dropped0 = AsdaSkillCastLock::DebugDroppedCount(); Exec0 = AsdaSkillCastLock::DebugExecutedCount();
			P->RequestMoveTo(Home + FVector2D(600.0, 0.0));
			Queued = Home + FVector2D(0.0, 900.0);
			const FVector2D Ue = AsdaCoord::GameToUe2D(Queued);
			const FVector Loc = Ch() ? Ch()->GetActorLocation() : FVector::ZeroVector;
			AsdaSkillCastLock::QueueGroundMove(P, FVector(Ue.X, Ue.Y, Loc.Z));
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
				St->bStepStarted = true; St->StepStartS = Now;
				UE_LOG(LogAsdaClickQueueSupersedeTest, Log, TEXT("%s шаг %s"), St->Tag, *Stp.Name);
				if (Stp.Start) { Stp.Start(); }
				return;
			}
			const bool bDone = !Stp.Poll || Stp.Poll();
			if (!bDone && Now - St->StepStartS < Stp.TimeoutSec) { return; }
			if (Stp.Check) { Stp.Check(); }
			++St->StepIdx; St->bStepStarted = false;
		}), 0.05f, true, KitArgs.Delay + 2.5f);
	}
}

// Самотест «ход клавишами вытесняет отложенный клик хода» — запуск и сценарий.
struct FAsdaClickQueueSupersedeSelfTest
{
	// Запуск: разбор аргументов (`delay=N`, `quit`), сборка шагов A–C и их исполнение по таймеру.
	static void Run(const TArray<FString>& Args, UWorld* W);
};

void FAsdaClickQueueSupersedeSelfTest::Run(const TArray<FString>& Args, UWorld* W)
{
	using namespace AsdaClickQueueSupersedeTestLocal;
	const TCHAR* const kCmd = TEXT("asda.Input.SelfTestClickQueueSupersede");   // имя команды
	const FAsdaSelfTestArgs KitArgs = FAsdaSelfTestArgs::Parse(Args);
	static const uint16 kGuardOps[] = { 4006, 4008 };   // опкоды, которые самотест не должен выпустить в сеть
	if (AsdaSelfTestKit::RefuseStart(W, kCmd, KitArgs, 0.f, MakeArrayView(kGuardOps))) { return; }
	UGameInstance* GIw = W ? W->GetGameInstance() : nullptr;
	UAsdaNetSubsystem* Net = GIw ? GIw->GetSubsystem<UAsdaNetSubsystem>() : nullptr;
	if (!Net) { AsdaSelfTestKit::Refused(W, kCmd, KitArgs.bQuit, TEXT("нет подсистемы сети")); return; }

	TSharedRef<FState> S = MakeShared<FState>();
	S->Cmd = kCmd; S->Tag = TEXT("САМОТЕСТ-ОЧЕРЕДЬ-КЛИКА");
	S->Net = Net; S->World = W; S->bQuit = KitArgs.bQuit; S->Gate = FAsdaSelfTestRunGate{ kCmd, KitArgs.bQuit };
	S->SelfScope = MakeUnique<FAsdaNetSelfScope>(Net, kCmd);

	// Добавить шаг сценария.
	auto Add = [S](const FString& Name, float Timeout, TFunction<void()> Start, TFunction<bool()> Poll, TFunction<void()> Check)
	{
		FStep St; St.Name = Name; St.TimeoutSec = Timeout; St.Start = MoveTemp(Start); St.Poll = MoveTemp(Poll); St.Check = MoveTemp(Check);
		S->Steps.Add(MoveTemp(St));
	};
	// Пара шагов «клик в очереди → нажата W» и «ответ сервера, W отпущена». bSupersede — ждём вытеснения (ключ 1) или исполнения старого клика (ключ 0).
	auto AddKeyCase = [S, Add](const FString& Name, bool bSupersede)
	{
		Add(Name + TEXT(": клик в очереди, нажата W"), 3.f, [S]()
		{
			S->Calm();
			S->ClickAndQueue();
			S->PendingBeforeKey = AsdaSkillCastLock::DebugPendingActionKind();
			AsdaSelfTestInput::KeyEv(S->World.Get(), EKeys::W, true);
		}, [S, bSupersede]()
		{
			// Событие: очередь опустела (вытеснение) либо прошло 0,4 с (контроль — очередь должна остаться).
			return bSupersede ? (AsdaSkillCastLock::DebugPendingActionKind() == 0 || S->El() > 2.0) : S->El() > 0.4;
		}, [S, bSupersede]()
		{
			S->PendingAfterKey = AsdaSkillCastLock::DebugPendingActionKind();
			S->Expect(S->PendingBeforeKey != 0, FString::Printf(TEXT("отложенный клик хода стоит в очереди до нажатия W (вид действия %d)"), S->PendingBeforeKey));
			S->Expect(bSupersede ? (S->PendingAfterKey == 0 && AsdaSkillCastLock::DebugDroppedCount() == S->Dropped0 + 1) : (S->PendingAfterKey != 0),
				FString::Printf(TEXT("после нажатия W: вид отложенного действия %d, снято без исполнения %d (ждём: %s)"), S->PendingAfterKey,
					AsdaSkillCastLock::DebugDroppedCount() - S->Dropped0, bSupersede ? TEXT("очередь пуста, снято 1") : TEXT("очередь стоит — прежнее поведение")));
		});
		Add(Name + TEXT(": ответ сервера, W отпущена"), 4.f, [S]()
		{
			// Сначала только отпускание W: ответ сервера придёт позже (опрос ниже), когда клавиатура хода уже не активна, — иначе
			//   отложенный клик исполнился бы под активной клавиатурой и был бы отброшен штатным гейтом (game.c:13684).
			AsdaSelfTestInput::KeyEv(S->World.Get(), EKeys::W, false);
		}, [S, bSupersede]()
		{
			// «Сервер отвечает» на каждый запрос хода (и на ход клавишами, начатый нажатием W): защита от флуда открывается.
			if (UAsdaNetSubsystem* Nn = S->N()) { if (S->El() > 0.3 && Nn->IsMoveRequestInFlight()) { Nn->ClearMoveRequestInFlight(TEXT("самотест очереди клика: ответ сервера")); } }
			return bSupersede ? S->El() > 1.3 : (S->SentToQueued() > 0 || S->El() > 2.5);
		}, [S, bSupersede]()
		{
			const int32 Sent = S->SentToQueued();
			S->Expect(bSupersede ? Sent == 0 : Sent > 0, FString::Printf(TEXT("пакетов 4006 в точку отложенного клика: %d (ждём: %s)"), Sent,
				bSupersede ? TEXT("0 — старый клик не исполняется после хода клавишами") : TEXT("> 0 — старый клик исполнился после отпускания W, дефект виден")));
		});
	};

	Add(TEXT("подготовка"), 5.f, [S, Net]()
	{
		AAsdaPlayerCharacter* C = S->Ch();
		if (!C || !S->PC()) { S->Expect(false, TEXT("нет своего персонажа после FakeSelfSpawn")); return; }
		S->Home = C->GetGamePosXZ();
		if (!Net->bSelfUserNumKnown) { Net->SelfUserNum = kSelfFallback; Net->bSelfUserNumKnown = true; }
		Net->bSelfDead = false;
		TWeakPtr<FState> WSp = S;
		S->Gen = AsdaSelfTestKit::ArmCapture(Net, [WSp](uint16 Op, const uint8* P, int32 Len, bool, TCHAR)
		{
			TSharedPtr<FState> St = WSp.Pin();
			if (St && Op == 4006 && P && Len >= 37) { St->Dst4006.Add(FVector2D(AsdaBytes::RdF32(P + 16 + 13), AsdaBytes::RdF32(P + 16 + 17))); }
		}, (uint16)0, S->Cmd);
		S->Expect(S->Gen != 0, TEXT("перехват исходящих взведён"));
		S->bWasInGame = Net->bInGameConnected; Net->bInGameConnected = true;
		AsdaCVarSave::SetForTest(TEXT("asda.Input.ClickQueue"), 1);
		S->SetKey(1);
	}, [S]() { return S->El() > 0.5; }, nullptr);

	// ── A. ключ 1: ход клавишами вытесняет отложенный клик ──
	AddKeyCase(TEXT("A ключ 1"), true);
	// ── B. контроль, ключ 0: прежнее поведение — старый клик исполняется после отпускания W ──
	Add(TEXT("B ключ 0"), 1.f, [S]() { S->SetKey(0); }, nullptr, nullptr);
	AddKeyCase(TEXT("B ключ 0"), false);
	Add(TEXT("B ключ 1"), 1.f, [S]() { S->SetKey(1); }, nullptr, nullptr);
	// ── C. ключ 1, без клавиш: очередь работает, как прежде ──
	Add(TEXT("C без клавиш: клик в очереди, ответ сервера"), 4.f, [S]()
	{
		S->Calm();
		S->ClickAndQueue();
		if (UAsdaNetSubsystem* Nn = S->N()) { Nn->ClearMoveRequestInFlight(TEXT("самотест очереди клика: ответ сервера")); }
	}, [S]() { return S->SentToQueued() > 0 || S->El() > 3.0; }, [S]()
	{
		S->Expect(S->SentToQueued() > 0 && AsdaSkillCastLock::DebugExecutedCount() > S->Exec0,
			FString::Printf(TEXT("отложенный клик исполнен: пакетов 4006 в его точку %d, исполнено из очереди %d"), S->SentToQueued(), AsdaSkillCastLock::DebugExecutedCount() - S->Exec0));
	});

	// ── D/E. ★ R5-fix8 (14.11.2026): прочие новые намерения игрока (клавиши «сесть/встать» 55 и «атака» 59) вытесняют отложенный
	//   клик хода при ключе 2; при ключе 1 (только ход клавишами, как в R5-fix6) клик остаётся — прежнее поведение видно ──
	auto AddCmdCase = [S, Add](const FString& Name, int32 Key, int32 Cmd)
	{
		Add(Name, 3.f, [S, Key, Cmd]()
		{
			S->SetKey(Key);
			S->Calm();
			S->ClickAndQueue();
			S->PendingBeforeKey = AsdaSkillCastLock::DebugPendingActionKind();
			AsdaSkillCastLock::DebugExecuteCommand(S->PC(), Cmd);
			S->PendingAfterKey = AsdaSkillCastLock::DebugPendingActionKind();   // вытеснение — в самом вызове команды
		}, [S]() { return S->El() > 0.3; }, [S, Key, Cmd]()
		{
			S->Expect(S->PendingBeforeKey != 0, FString::Printf(TEXT("отложенный клик хода стоит в очереди до клавиши %d (вид действия %d)"), Cmd, S->PendingBeforeKey));
			S->Expect(Key >= 2 ? S->PendingAfterKey == 0 : S->PendingAfterKey != 0,
				FString::Printf(TEXT("после клавиши %d при ключе %d: вид отложенного действия %d (ждём: %s)"), Cmd, Key, S->PendingAfterKey,
					Key >= 2 ? TEXT("очередь пуста — клик вытеснен") : TEXT("очередь стоит — прежнее поведение")));
			if (Cmd == 55) { AsdaSkillCastLock::DebugExecuteCommand(S->PC(), 55); }   // встать обратно
			S->Calm();   // уборка шага: очередь и запрос хода сняты, персонаж стоит дома (следующему тесту игры — прежнее место)
		});
	};
	AddCmdCase(TEXT("D ключ 2: клик в очереди, клавиша «сесть/встать»"), 2, 55);
	AddCmdCase(TEXT("D контроль ключа 1: клик в очереди, клавиша «сесть/встать»"), 1, 55);
	AddCmdCase(TEXT("E ключ 2: клик в очереди, клавиша «атака»"), 2, 59);
	AddCmdCase(TEXT("E контроль ключа 1: клик в очереди, клавиша «атака»"), 1, 59);
	Add(TEXT("ключ по умолчанию"), 1.f, [S]() { S->SetKey(2); }, nullptr, nullptr);

	Add(TEXT("итог"), 2.f, [S, Net]()
	{
		AsdaSelfTestInput::KeyEv(S->World.Get(), EKeys::W, false);
		S->Calm();
		AsdaSelfTestKit::DisarmCapture(Net, S->Gen);
		Net->bInGameConnected = S->bWasInGame;
		S->SelfScope.Reset();
	}, [S]() { return S->El() > 0.3; }, [S]()
	{
		AsdaSelfTestKit::Finish(S->World.Get(), S->Cmd, false, LogAsdaClickQueueSupersedeTest, S->Tag, S->Ok, S->Fail);
	});
	RunSteps(S, W, KitArgs, Net);
}

static FAutoConsoleCommandWithWorldAndArgs GAsdaSelfTestClickQueueSupersede(
	TEXT("asda.Input.SelfTestClickQueueSupersede"),
	TEXT("Самотест R5-fix6 «ход клавишами вытесняет отложенный клик хода» (ключ asda.Input.ClickQueueSupersede 1): нажатие W снимает клик из ")
	TEXT("очереди, старый клик после отпускания W не исполняется; контроль ключа 0 — исполняется; без клавиш очередь работает. ")
	TEXT("Только офлайн. `delay=N` `quit`."),
	FConsoleCommandWithWorldAndArgsDelegate::CreateLambda([](const TArray<FString>& A, UWorld* W)
	{
		AsdaCVarSave::ArmWorldGuard(W, TEXT("asda.Input.SelfTestClickQueueSupersede"));
		FAsdaClickQueueSupersedeSelfTest::Run(A, W);
	}));

#endif // !UE_BUILD_SHIPPING
