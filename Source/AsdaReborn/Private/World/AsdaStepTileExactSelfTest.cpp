// Copyright (c) 2026 @sitxovski (Discord)
// AsdaStepTileExactSelfTest.cpp — самотест `asda.SelfTest.StepTileExact` (11.11.2026, задача A3).
//
// Что проверяет: шаг хода не ставит персонажа в закрытую клетку вплотную к её границе (ключ `asda.Walk.StepTileExact`,
//   разбор причины — у ключа в `World/AsdaPlayerCharacter_Move.cpp`). Случайный дефект самотеста `asda.SelfTest.SwimWalls`
//   (шаг S5) здесь воспроизводится ВСЕГДА: цель хода ставится в закрытую клетку на 0,0005 единицы за границу — такая точка
//   при округлении до одинарной точности попадает ровно в границу.
//     T0  пара клеток найдена: проходимая и закрытая под ней (по оси Z), точка цели округляется в границу;
//     T1  ключ 0 — персонаж встал в закрытую клетку (воспроизведение дефекта);
//     T2  ключ 1 — персонаж остался в проходимой клетке и стоит;
//     T3  ключ 1 — обычный шаг внутри проходимой клетки по-прежнему выполняется (регресс);
//     T4  зеркальный случай (закрытая клетка НАД проходимой): цель в проходимой клетке на 0,0005 ниже границы — в пакет ушла бы
//         точка ровно на границе, то есть в закрытой клетке; и при ключе 1, и при ключе 0 персонаж туда не идёт.
// Строки — «САМОТЕСТ-КЛЕТКА-ШАГА», итог — «… ИТОГ: ok=… fail=…». Только офлайн.
#include "CoreMinimal.h"

#if !UE_BUILD_SHIPPING

#include "AsdaNetSubsystem.h"
#include "Test/AsdaSelfTestRunGate.h"   // ★ R3: FAsdaSelfTestRunGate — узкий заголовок вместо AsdaOutgoingGate.h
#include "AsdaCVarSave.h"
#include "Test/AsdaSelfTestKit.h"
#include "World/AsdaPlayerCharacter.h"
#include "World/AsdaPlayerController.h"
#include "World/AsdaCoord.h"
#include "World/AsdaWalkGrid.h"
#include "Engine/Engine.h"
#include "Engine/World.h"
#include "HAL/IConsoleManager.h"
#include "TimerManager.h"

// Журнал самотеста.
DEFINE_LOG_CATEGORY_STATIC(LogAsdaStepTileTest, Log, All);

namespace AsdaStepTileTest
{
	static const TCHAR* const kCmd = TEXT("asda.SelfTest.StepTileExact");   // имя команды
	static const TCHAR* const kPrefix = TEXT("САМОТЕСТ-КЛЕТКА-ШАГА");        // начало строк журнала
	static const TCHAR* const kKey = TEXT("asda.Walk.StepTileExact");       // проверяемый ключ
	static constexpr float kSpeed = 0.6f;                                    // скорость хода теста, ед./мс (как верхом)
	static constexpr double kInside = 0.0005;                                // на сколько цель заходит за границу, ед.
	static constexpr double kStandOff = 0.3;                                 // откуда идём: на сколько выше границы, ед.

	// Состояние прогона.
	struct FState
	{
		int32 Ok = 0, Fail = 0;                      // счётчики проверок
		int32 MapId = 3;                             // карта прогона
		bool bQuit = false;                          // выйти после прогона
		FIntPoint Open = FIntPoint(-1, -1);          // проходимая клетка (закрытая — под ней)
		FIntPoint Mirror = FIntPoint(-1, -1);        // проходимая клетка зеркального случая (закрытая — над ней)
		FVector2D Home = FVector2D::ZeroVector;      // позиция персонажа до теста
		TWeakObjectPtr<UWorld> W;
		TWeakObjectPtr<AAsdaPlayerCharacter> Me;

		// Засчитать проверку и написать строку.
		void Expect(bool b, const FString& What)
		{
			if (b) { ++Ok; UE_LOG(LogAsdaStepTileTest, Display, TEXT("%s ok   %s"), kPrefix, *What); }
			else   { ++Fail; UE_LOG(LogAsdaStepTileTest, Error, TEXT("%s FAIL %s"), kPrefix, *What); }
		}
	};
	using FS = TSharedRef<FState>;

	// Отложенный шаг теста.
	static void After(const FS& S, float Delay, TFunction<void()> Fn)
	{
		UWorld* W = S->W.Get();
		if (!W) { return; }
		FTimerHandle H;
		W->GetTimerManager().SetTimer(H, FTimerDelegate::CreateLambda(MoveTemp(Fn)), FMath::Max(0.01f, Delay), false);
	}

	// Годится ли клетка: сама и клетка над ней проходимы, клетка под ней (Z − 1) закрыта, цель округляется в границу.
	static bool Fits(const UAsdaWalkGrid* G, int32 X, int32 Z)
	{
		if (!G->IsTileWalkable(X, Z) || !G->IsTileWalkable(X, Z + 1) || G->IsTileWalkable(X, Z - 1)) { return false; }
		const double Border = Z * 100.0;
		return (double)(float)(Border - kInside) == Border;
	}

	// Годится ли клетка для зеркального случая: сама и клетка под ней проходимы, клетка над ней (Z + 1) закрыта, цель в ней
	//   на 0,0005 ниже верхней границы округляется в границу.
	static bool FitsMirror(const UAsdaWalkGrid* G, int32 X, int32 Z)
	{
		if (!G->IsTileWalkable(X, Z) || !G->IsTileWalkable(X, Z - 1) || G->IsTileWalkable(X, Z + 1)) { return false; }
		const double Border = (Z + 1) * 100.0;
		return (double)(float)(Border - kInside) == Border;
	}

	// Найти пару клеток: сначала место дефекта (Альпен 106,392), затем ближайшая к нему по квадратам.
	static bool FindPair(const UAsdaWalkGrid* G, FIntPoint& Out, bool bMirror = false)
	{
		const FIntPoint C(106, 392);
		for (int32 R = 0; R < 200; ++R)
		{
			for (int32 Z = C.Y - R; Z <= C.Y + R; ++Z)
			{
				for (int32 X = C.X - R; X <= C.X + R; ++X)
				{
					if (FMath::Max(FMath::Abs(X - C.X), FMath::Abs(Z - C.Y)) != R) { continue; }
					if (X < 2 || Z < 2 || X >= G->Width - 2 || Z >= G->Height - 2) { continue; }
					if (bMirror ? FitsMirror(G, X, Z) : Fits(G, X, Z)) { Out = FIntPoint(X, Z); return true; }
				}
			}
		}
		return false;
	}

	// Итог: вернуть ключ и позицию, написать счёт.
	static void Finish(const FS& S)
	{
		UWorld* W = S->W.Get();
		if (!W) { return; }
		if (AAsdaPlayerCharacter* Me = S->Me.Get()) { Me->StopMove(); Me->NetSnapHard(S->Home); }
		AsdaCVarSave::RestoreAll(kCmd);
		AsdaSelfTestKit::Finish(W, kCmd, S->bQuit, LogAsdaStepTileTest, kPrefix, S->Ok, S->Fail,
			*FString::Printf(TEXT(" (карта %d, клетка %d,%d)"), S->MapId, S->Open.X, S->Open.Y));
	}

	// Один ход к цели за границей: ключ KeyV, затем Done с клеткой, где персонаж оказался.
	static void WalkToBorder(const FS& S, int32 KeyV, TFunction<void(FIntPoint, FVector2D, bool)> Done)
	{
		AAsdaPlayerCharacter* Me = S->Me.Get();
		if (!Me) { Finish(S); return; }
		AsdaCVarSave::SetForTest(kKey, KeyV);
		const double Cx = S->Open.X * 100.0 + 50.0;
		const double Border = S->Open.Y * 100.0;
		Me->StopMove();
		Me->NetSnapHard(FVector2D(Cx, Border + kStandOff));
		Me->NetMoveFromActor(FVector2D(Cx, Border - kInside), kSpeed);
		After(S, 0.4f, [S, Done]()
		{
			AAsdaPlayerCharacter* M = S->Me.Get();
			if (!M) { Finish(S); return; }
			const FVector2D P = M->GetGamePosXZ();
			Done(AsdaCoord::GameToTile(P), P, M->IsMoving());
		});
	}

	// T4 — зеркальный случай при ключе KeyV (сначала 1, затем 0), после — итог.
	static void MirrorCase(const FS& S, int32 KeyV)
	{
		AAsdaPlayerCharacter* Me = S->Me.Get();
		if (!Me || !Me->WalkGrid) { Finish(S); return; }
		if (KeyV == 1)
		{
			const bool bFound = FindPair(Me->WalkGrid, S->Mirror, /*bMirror*/ true);
			S->Expect(bFound, FString::Printf(TEXT("T4 зеркальная пара: клетка (%d,%d) проходима, клетка над ней закрыта"), S->Mirror.X, S->Mirror.Y));
			if (!bFound) { Finish(S); return; }
		}
		AsdaCVarSave::SetForTest(kKey, KeyV);
		const double Cx = S->Mirror.X * 100.0 + 50.0;
		const double Border = (S->Mirror.Y + 1) * 100.0;      // верхняя граница проходимой клетки = начало закрытой
		const double Start = Border - 0.3;
		Me->StopMove();
		Me->NetSnapHard(FVector2D(Cx, Start));
		Me->NetMoveFromActor(FVector2D(Cx, Border - kInside), kSpeed);
		After(S, 0.4f, [S, KeyV, Start]()
		{
			AAsdaPlayerCharacter* M = S->Me.Get();
			if (!M) { Finish(S); return; }
			const FVector2D P = M->GetGamePosXZ();
			const FIntPoint SentTile = AsdaCoord::GameToTile(FVector2D((double)(float)P.X, (double)(float)P.Y));   // клетка позиции, как её увидит сервер
			S->Expect(FMath::IsNearlyEqual(P.Y, Start, 0.0001) && SentTile == S->Mirror && !M->IsMoving(),
				FString::Printf(TEXT("T4 ключ %d: к точке вплотную под закрытой клеткой не пошёл (позиция %.6f, клетка позиции в пакете %d,%d, идёт %d)"),
					KeyV, P.Y, SentTile.X, SentTile.Y, M->IsMoving() ? 1 : 0));
			if (KeyV == 1) { MirrorCase(S, 0); } else { Finish(S); }
		});
	}

	// Разделы по порядку.
	static void Begin(const FS& S)
	{
		UWorld* W = S->W.Get();
		AAsdaPlayerController* PC = W ? Cast<AAsdaPlayerController>(W->GetFirstPlayerController()) : nullptr;
		AAsdaPlayerCharacter* Me = PC ? Cast<AAsdaPlayerCharacter>(PC->GetPawn()) : nullptr;
		if (!Me || !Me->WalkGrid) { AsdaSelfTestKit::Refused(W, kCmd, S->bQuit, TEXT("нет своего персонажа / сетки карты")); return; }
		S->Me = Me;
		S->Home = Me->GetGamePosXZ();
		AsdaCVarSave::ArmWorldGuard(W, kCmd);
		const UAsdaWalkGrid* G = Me->WalkGrid;
		const bool bFound = FindPair(G, S->Open);
		S->Expect(bFound, FString::Printf(TEXT("T0 карта %d: клетка (%d,%d) проходима, клетка под ней закрыта; цель %.4f округляется в границу %.1f"),
			S->MapId, S->Open.X, S->Open.Y, S->Open.Y * 100.0 - kInside, S->Open.Y * 100.0));
		if (!bFound) { Finish(S); return; }
		WalkToBorder(S, 0, [S](FIntPoint T, FVector2D P, bool bMoving)
		{
			S->Expect(T.Y == S->Open.Y - 1, FString::Printf(TEXT("T1 ключ 0: персонаж встал в закрытую клетку — воспроизведение дефекта (клетка %d,%d, позиция %.6f)"),
				T.X, T.Y, P.Y));
			WalkToBorder(S, 1, [S](FIntPoint T2, FVector2D P2, bool bMoving2)
			{
				S->Expect(T2 == S->Open && !bMoving2, FString::Printf(TEXT("T2 ключ 1: в закрытую клетку не вошёл и стоит (клетка %d,%d, позиция %.6f, идёт %d)"),
					T2.X, T2.Y, P2.Y, bMoving2 ? 1 : 0));
				// Регресс: обычный ход внутри проходимой клетки (от +0,3 до +30 ед. над границей) выполняется.
				AAsdaPlayerCharacter* Me2 = S->Me.Get();
				if (!Me2) { Finish(S); return; }
				const double Cx = S->Open.X * 100.0 + 50.0;
				const double Border = S->Open.Y * 100.0;
				Me2->StopMove();
				Me2->NetSnapHard(FVector2D(Cx, Border + kStandOff));
				Me2->NetMoveFromActor(FVector2D(Cx, Border + 30.0), kSpeed);
				After(S, 0.4f, [S, Border]()
				{
					AAsdaPlayerCharacter* M = S->Me.Get();
					if (!M) { Finish(S); return; }
					const FVector2D P3 = M->GetGamePosXZ();
					S->Expect(FMath::IsNearlyEqual(P3.Y, Border + 30.0, 0.01) && AsdaCoord::GameToTile(P3) == S->Open,
						FString::Printf(TEXT("T3 ключ 1: обычный ход внутри проходимой клетки дошёл до цели (позиция %.6f, цель %.6f)"), P3.Y, Border + 30.0));
					MirrorCase(S, 1);
				});
			});
		});
	}

	// Команда: загрузка карты, свой персонаж, затем разделы.
	static void Run(const TArray<FString>& Args, UWorld* W)
	{
		const FAsdaSelfTestArgs A = FAsdaSelfTestArgs::Parse(Args);
		if (!W) { AsdaSelfTestKit::Refused(nullptr, kCmd, A.bQuit, TEXT("нет мира")); return; }
		if (AsdaSelfTestKit::RefuseStart(W, kCmd, A)) { return; }
		TSharedRef<FAsdaSelfTestRunGate> Gate = MakeShared<FAsdaSelfTestRunGate>(kCmd, A.bQuit);
		FS S = MakeShared<FState>();
		S->W = W; S->MapId = A.Int(0, 3); S->bQuit = A.bQuit;
		After(S, A.Delay, [S, Gate]()
		{
			UWorld* Wd = S->W.Get();
			if (!Wd || !Gate->Pass(Wd) || !GEngine) { return; }
			GEngine->Exec(Wd, *FString::Printf(TEXT("Asda.Load.CurrentMap %d"), S->MapId));
			GEngine->Exec(Wd, TEXT("Asda.World.FakeSelfSpawn 256 256"));
		});
		After(S, A.Delay + 3.f, [S, Gate]()
		{
			UWorld* Wd = S->W.Get();
			if (!Wd || !Gate->Pass(Wd)) { return; }
			Begin(S);
		});
	}
}

// Консольная команда самотеста.
static FAutoConsoleCommandWithWorldAndArgs GAsdaStepTileExactSelfTest(
	TEXT("asda.SelfTest.StepTileExact"),
	TEXT("Самотест клетки шага (задача A3): ход к точке в закрытой клетке вплотную к её границе. При asda.Walk.StepTileExact 0 "
	     "персонаж встаёт в закрытую клетку (воспроизведение дефекта), при 1 — нет; обычный ход внутри клетки выполняется. "
	     "`<id карты>` `delay=N` `quit`; игра — на уровне этой карты. Только офлайн."),
	FConsoleCommandWithWorldAndArgsDelegate::CreateLambda([](const TArray<FString>& A, UWorld* W)
	{
		AsdaStepTileTest::Run(A, W);
	}));

#endif // !UE_BUILD_SHIPPING
