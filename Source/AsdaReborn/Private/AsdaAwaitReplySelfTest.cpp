// Copyright (c) 2026 @sitxovski (Discord)
// ★ САМОТЕСТ «безответная заявка удара и новая серия» (R5-fix8, 14.11.2026): `asda.Combat.SelfTestAwaitReply [delay=N] [quit]`.
//   Проверяет правку под ключом `asda.Combat.AwaitReplyByTarget` (разбор — у ключа, AsdaAttackRhythmSubsystem.cpp): очередь заявок
//   удара по мобу часов атаки (`UAsdaAttackRhythmSubsystem`) — отметки отправителей `NoteSent4026/4028` с номером цели, ответ
//   сервера `ResolveMobReply` с номером моба, эхо проверки состояния 6001 штатным приёмом (`DebugFeedPacket`). Только офлайн.
//   Шаги:
//     A) обычный порядок (ключ 1): заявки и ответы по одной цели подряд — каждый ответ своей серии, очередь пуста;
//     B) ключ 1: 4028 по мобу 100 без ответа, сразу новый бой 4026 по мобу 200, ответ по 200 → ответ НОВОЙ серии (заявка по 100
//        снята как отброшенная сервером молча); контроль ключа 0 — ответ принят за поздний (прежний дефект виден);
//     C) ключ 1: 6001 «ОК» на 4026 заявку не снимает; 6001 с причиной 2 на 4028 снимает её, и первый ответ нового боя по ТОМУ ЖЕ
//        мобу — новой серии; контроль ключа 0 — заявка остаётся, ответ принят за поздний.
//   Ничего не отправляет. Итог — «САМОТЕСТ-ЗАЯВКИ-УДАРА ИТОГ: ok=N fail=M».
#include "CoreMinimal.h"

#if !UE_BUILD_SHIPPING

#include "AsdaNetSubsystem.h"
#include "AsdaBytes.h"
#include "AsdaCVarSave.h"
#include "Test/AsdaSelfTestRunGate.h"
#include "Test/AsdaSelfTestKit.h"
#include "World/AsdaAttackRhythmSubsystem.h"
#include "Engine/Engine.h"
#include "Engine/GameInstance.h"
#include "Engine/World.h"
#include "GameFramework/PlayerController.h"
#include "HAL/IConsoleManager.h"
#include "TimerManager.h"

DEFINE_LOG_CATEGORY_STATIC(LogAsdaAwaitReplyTest, Log, All);

namespace AsdaAwaitReplyTestLocal
{
	const TCHAR* const kKey = TEXT("asda.Combat.AwaitReplyByTarget");   // ключ правки
	const TCHAR* const kTag = TEXT("САМОТЕСТ-ЗАЯВКИ-УДАРА");             // метка строк журнала
	constexpr int32 kMobA = 100;   // моб прошлого боя
	constexpr int32 kMobB = 200;   // моб нового боя

	// Счётчики и доступы прогона.
	struct FState
	{
		TWeakObjectPtr<UAsdaNetSubsystem> Net;   // сеть
		int32 Ok = 0, Fail = 0;                  // счётчики
		// Часы атаки.
		UAsdaAttackRhythmSubsystem* Rh() const
		{
			const UAsdaNetSubsystem* N = Net.Get();
			UGameInstance* GI = N ? N->GetGameInstance() : nullptr;
			return GI ? GI->GetSubsystem<UAsdaAttackRhythmSubsystem>() : nullptr;
		}
		// Проверка с записью в журнал.
		void Expect(bool bOk, const FString& What)
		{
			if (bOk) { ++Ok; } else { ++Fail; }
			UE_LOG(LogAsdaAwaitReplyTest, Log, TEXT("%s %s: %s"), kTag, bOk ? TEXT("OK") : TEXT("ПРОВАЛ"), *What);
		}
		// Эхо проверки состояния 6001 (3 б: опкод u16, причина u8) штатным приёмом.
		void Feed6001(int32 Op, uint8 Reason)
		{
			TArray<uint8> B; B.SetNumZeroed(3);
			AsdaBytes::WrI16(&B[0], Op); B[2] = Reason;
			if (UAsdaNetSubsystem* N = Net.Get()) { N->DebugFeedPacket(6001, B); }
		}
	};

	// Сценарий B: 4028 по мобу A без ответа, новый бой по мобу B. Возврат: ответ нового боя принят как ответ НОВОЙ серии.
	static bool OtherTargetReplyIsFresh(FState& S, int32& OutLeft)
	{
		UAsdaAttackRhythmSubsystem* Rh = S.Rh();
		Rh->DebugReset();
		Rh->NoteSent4026(true, kMobA);
		Rh->ResolveMobReply(true, nullptr, kMobA);      // ответ на 4026 прошлого боя
		Rh->NoteSent4028(true, kMobA);                  // сервер промолчит
		Rh->NoteSent4026(true, kMobB);                  // новый бой раньше чем через секунду
		const uint32 Fresh = Rh->GetSeriesGen();
		const uint32 Got = Rh->ResolveMobReply(true, nullptr, kMobB);
		OutLeft = Rh->DebugAwaitCount();
		return Got == Fresh;
	}

	// Сценарий C: 4028 отклонён эхом 6001, новый бой по ТОМУ ЖЕ мобу. Возврат: ответ нового боя — новой серии.
	static bool RefusedByCheckReplyIsFresh(FState& S, int32& OutAfterOk, int32& OutAfterRefuse)
	{
		UAsdaAttackRhythmSubsystem* Rh = S.Rh();
		Rh->DebugReset();
		Rh->NoteSent4026(true, kMobA);
		S.Feed6001(4026, 0);                            // эхо «ОК» — заявка остаётся ждать боевой ответ
		OutAfterOk = Rh->DebugAwaitCount();
		Rh->ResolveMobReply(true, nullptr, kMobA);
		Rh->NoteSent4028(true, kMobA);
		S.Feed6001(4028, 2);                            // сервер отклонил по состоянию — боевого ответа не будет
		OutAfterRefuse = Rh->DebugAwaitCount();
		Rh->NoteSent4026(true, kMobA);                  // новый бой по тому же мобу
		const uint32 Fresh = Rh->GetSeriesGen();
		return Rh->ResolveMobReply(true, nullptr, kMobA) == Fresh;
	}
}

// Самотест «безответная заявка удара и новая серия» — запуск и сценарий.
struct FAsdaAwaitReplySelfTest
{
	// Запуск: разбор аргументов (`delay=N`, `quit`), проверки A–C по таймеру после задержки.
	static void Run(const TArray<FString>& Args, UWorld* W);
};

void FAsdaAwaitReplySelfTest::Run(const TArray<FString>& Args, UWorld* W)
{
	using namespace AsdaAwaitReplyTestLocal;
	const TCHAR* const kCmd = TEXT("asda.Combat.SelfTestAwaitReply");
	const FAsdaSelfTestArgs KitArgs = FAsdaSelfTestArgs::Parse(Args);
	static const uint16 kGuardOps[] = { 4026, 4028 };   // опкоды, которые самотест не должен выпустить в сеть
	if (AsdaSelfTestKit::RefuseStart(W, kCmd, KitArgs, 0.f, MakeArrayView(kGuardOps))) { return; }
	UGameInstance* GIw = W ? W->GetGameInstance() : nullptr;
	UAsdaNetSubsystem* Net = GIw ? GIw->GetSubsystem<UAsdaNetSubsystem>() : nullptr;
	if (!Net) { AsdaSelfTestKit::Refused(W, kCmd, KitArgs.bQuit, TEXT("нет подсистемы сети")); return; }

	TSharedRef<FState> S = MakeShared<FState>();
	S->Net = Net;
	const bool bQuit = KitArgs.bQuit;
	TWeakObjectPtr<UWorld> WW(W);
	FTimerHandle H;
	W->GetTimerManager().SetTimer(H, FTimerDelegate::CreateWeakLambda(Net, [S, WW, kCmd, bQuit]()
	{
		UWorld* Wd = WW.Get();
		UAsdaAttackRhythmSubsystem* Rh = S->Rh();
		if (!Wd || !Rh) { S->Expect(false, TEXT("нет мира или часов атаки")); AsdaSelfTestKit::Finish(Wd, kCmd, bQuit, LogAsdaAwaitReplyTest, kTag, S->Ok, S->Fail); return; }

		// ── A. обычный порядок ──
		AsdaCVarSave::SetForTest(kKey, 1);
		Rh->DebugReset();
		Rh->NoteSent4026(true, kMobA);
		const uint32 G1 = Rh->GetSeriesGen();
		const uint32 R1 = Rh->ResolveMobReply(true, nullptr, kMobA);
		Rh->NoteSent4028(true, kMobA);
		const uint32 R2 = Rh->ResolveMobReply(true, nullptr, kMobA);
		S->Expect(R1 == G1 && R2 == G1 && Rh->DebugAwaitCount() == 0,
			FString::Printf(TEXT("A обычный порядок: ответы серии #%u — #%u и #%u, заявок в очереди %d (ждём 0)"), G1, R1, R2, Rh->DebugAwaitCount()));

		// ── B. ответ по другой цели снимает безответную заявку прошлого боя ──
		int32 Left = -1;
		const bool bFreshB = OtherTargetReplyIsFresh(*S, Left);
		S->Expect(bFreshB && Left == 0, FString::Printf(TEXT("B ключ 1: 4028 по мобу %d без ответа, новый бой по мобу %d — первый ответ нового боя принят НОВОЙ серией (%s), заявок осталось %d (ждём 0)"),
			kMobA, kMobB, bFreshB ? TEXT("да") : TEXT("НЕТ — поздний"), Left));
		AsdaCVarSave::SetForTest(kKey, 0);
		const bool bFreshB0 = OtherTargetReplyIsFresh(*S, Left);
		S->Expect(!bFreshB0, FString::Printf(TEXT("B контроль ключа 0: первый ответ нового боя принят за поздний (%s), заявок осталось %d — прежний дефект виден"),
			bFreshB0 ? TEXT("нет — новой серией") : TEXT("да"), Left));

		// ── C. эхо проверки состояния 6001 ──
		AsdaCVarSave::SetForTest(kKey, 1);
		int32 AfterOk = -1, AfterRefuse = -1;
		const bool bFreshC = RefusedByCheckReplyIsFresh(*S, AfterOk, AfterRefuse);
		S->Expect(AfterOk == 1, FString::Printf(TEXT("C ключ 1: 6001 «ОК» на 4026 заявку не снимает (в очереди %d, ждём 1)"), AfterOk));
		S->Expect(AfterRefuse == 0, FString::Printf(TEXT("C ключ 1: 6001 с причиной 2 на 4028 снимает заявку (в очереди %d, ждём 0)"), AfterRefuse));
		S->Expect(bFreshC, FString::Printf(TEXT("C ключ 1: первый ответ нового боя по тому же мобу — новой серии (%s)"), bFreshC ? TEXT("да") : TEXT("НЕТ — поздний")));
		AsdaCVarSave::SetForTest(kKey, 0);
		const bool bFreshC0 = RefusedByCheckReplyIsFresh(*S, AfterOk, AfterRefuse);
		S->Expect(AfterRefuse == 1 && !bFreshC0, FString::Printf(TEXT("C контроль ключа 0: отклонённая заявка осталась (в очереди %d), первый ответ нового боя принят за поздний (%s) — прежний дефект виден"),
			AfterRefuse, bFreshC0 ? TEXT("нет") : TEXT("да")));

		Rh->DebugReset();   // за собой: очередь заявок и серия теста сняты
		AsdaSelfTestKit::Finish(Wd, kCmd, bQuit, LogAsdaAwaitReplyTest, kTag, S->Ok, S->Fail);
	}), FMath::Max(0.05f, KitArgs.Delay + 1.0f), false);
}

static FAutoConsoleCommandWithWorldAndArgs GAsdaSelfTestCombatAwaitReply(
	TEXT("asda.Combat.SelfTestAwaitReply"),
	TEXT("Самотест R5-fix8 «безответная заявка удара и новая серия»: ответ сервера по другой цели и отказ состояния 6001 снимают безответные ")
	TEXT("заявки (ключ asda.Combat.AwaitReplyByTarget 1); контроль ключа 0 — первый ответ нового боя принимается за поздний. Только офлайн. `delay=N` `quit`."),
	FConsoleCommandWithWorldAndArgsDelegate::CreateLambda([](const TArray<FString>& A, UWorld* W)
	{
		AsdaCVarSave::ArmWorldGuard(W, TEXT("asda.Combat.SelfTestAwaitReply"));
		FAsdaAwaitReplySelfTest::Run(A, W);
	}));

#endif // !UE_BUILD_SHIPPING
