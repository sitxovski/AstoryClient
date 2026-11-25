// Copyright (c) 2026 @sitxovski (Discord)
// ★ САМОТЕСТ ритма keepalive 1000 (античит, блок Б плана альфы, 14.10.2026): `asda.Net.SelfTestHeartbeat [quit]`.
//   Эталон — поток оригинала `NetHeartbeatThread@0x95C330` (реверс `Reports/anticheat/wSHIP_logs/clock_re.md` §4.1):
//   раз в 50 мс, |t − метка| > 8000 мс (модуль разности) при живой сессии без кода кика → 1000, метка = t.
//   Сервер GS `sub_5271F0`: пинг раньше 6000 мс после прошлого → Log_SpeedHack «Warning» (серверный детектор ускорения часов).
//   Гоняется НАСТОЯЩИЙ планировщик `UAsdaNetSubsystem::StepNetHeartbeat` с подставным временем шагом 50 мс (как поток
//   оригинала); метка подсистемы сохраняется и возвращается. Ничего не отправляет: посылку решает Tick, здесь только решение.
//   Проверки:
//     H1  честные часы, 120 с игровой сессии → интервалы между пингами в (8.00; 8.05] с, пингов 14;
//     H2  нет ни одного интервала < 6 с (сервер не пишет Warning честному клиенту);
//     H3  часы ×1.5 (как speedhack) → реальный интервал ≈ 5.37 с < 6 с — сервер увидит (порог оригинала ×1.33);
//     H4  часы ×1.3 → реальный интервал > 6 с — ниже порога, как у оригинала;
//     H5  скачок часов НАЗАД на 20 с → пинг сразу (модуль разности);
//     H6  код кика (LastKickCode ≠ 0xFF) → пингов нет;  H7  нет сессии/сокета → пингов нет;
//     H8  первая проверка без метки → пинга нет, отсчёт с этого момента;  H9  ровно 8.000 с → ещё нет (строго больше).
#include "CoreMinimal.h"
#include "AsdaDevConsole.h"

#if ASDA_DEV_CONSOLE

#include "AsdaNetSubsystem.h"
#include "Engine/GameInstance.h"
#include "Engine/World.h"
#include "HAL/IConsoleManager.h"
#include "Test/AsdaSelfTestKit.h"

DEFINE_LOG_CATEGORY_STATIC(LogAsdaHeartbeatTest, Log, All);

namespace AsdaHeartbeatTest
{
	static const TCHAR* GCmd = TEXT("asda.Net.SelfTestHeartbeat");   // имя самотеста

	// Счётчики и запись проверок.
	struct FRun
	{
		int32 Ok = 0, Fail = 0;   // счётчики
		void Expect(bool b, const FString& What)   // проверка с записью в лог
		{
			if (b) { ++Ok; UE_LOG(LogAsdaHeartbeatTest, Log, TEXT("ПУЛЬС ok   %s"), *What); }
			else { ++Fail; UE_LOG(LogAsdaHeartbeatTest, Error, TEXT("ПУЛЬС FAIL %s"), *What); }
		}
	};

	// Прогон планировщика: `RealSec` реального времени шагом 50 мс, часы клиента идут в `Speed` раз быстрее.
	//   Возвращает моменты пингов в РЕАЛЬНОМ времени (то, что видит сервер своим GetTickCount).
	static TArray<double> Simulate(UAsdaNetSubsystem* N, double RealSec, double Speed, bool bLogin, bool bGame, uint8 Kick, bool bSock = true)
	{
		TArray<double> Pings;
		const double ClientT0 = 1000.0;                 // произвольная ненулевая точка отсчёта часов клиента
		N->SetNetHeartbeatStampForTest(ClientT0);       // как после подключения (ResetNetHeartbeatStamp)
		const int32 Steps = FMath::RoundToInt(RealSec / 0.05);
		for (int32 K = 1; K <= Steps; ++K)
		{
			const double Real = K * 0.05;   // шаг потока оригинала — WaitForSingleObject(…, 50 мс)
			if (N->StepNetHeartbeat(ClientT0 + Real * Speed, bSock, bLogin, bGame, Kick)) { Pings.Add(Real); }
		}
		return Pings;
	}

	// Интервалы между соседними пингами (первый — от момента подключения, t=0).
	static TArray<double> Intervals(const TArray<double>& P)
	{
		TArray<double> R;
		double Prev = 0.0;
		for (double T : P) { R.Add(T - Prev); Prev = T; }
		return R;
	}
}

static FAutoConsoleCommandWithWorldAndArgs GAsdaNetSelfTestHeartbeat(
	TEXT("asda.Net.SelfTestHeartbeat"),
	TEXT("asda.Net.SelfTestHeartbeat [quit] — ритм keepalive 1000 как у NetHeartbeatThread@0x95C330 оригинала (> 8 с по стенным часам, ")
	TEXT("модуль разности, гейт кика/сессии); подставное время, ничего не отправляет. Офлайн."),
	FConsoleCommandWithWorldAndArgsDelegate::CreateLambda([](const TArray<FString>& A, UWorld* W)
	{
		using namespace AsdaHeartbeatTest;
		const FAsdaSelfTestArgs Args = FAsdaSelfTestArgs::Parse(A);
		UGameInstance* GI = W ? W->GetGameInstance() : nullptr;
		if (AsdaSelfTestKit::RefuseStart(GI, GCmd, Args)) { return; }
		UAsdaNetSubsystem* N = GI ? GI->GetSubsystem<UAsdaNetSubsystem>() : nullptr;
		if (!N) { AsdaSelfTestKit::Refused(GI, GCmd, Args.bQuit, TEXT("нет подсистемы сети")); return; }

		FRun R;
		const double SavedStamp = N->GetNetHeartbeatStamp();

		// H1/H2 — честные часы, игровая сессия, 120 с.
		{
			const TArray<double> P = Simulate(N, 120.0, 1.0, false, true, 0xFF);
			const TArray<double> I = Intervals(P);
			double MinI = 1e9, MaxI = 0.0;
			for (double X : I) { MinI = FMath::Min(MinI, X); MaxI = FMath::Max(MaxI, X); }
			// Шаг 50 мс и двоичная запись 0.05: разность ровно на границе может выйти 8.000…01 — тогда пинг на 160-м шаге
			//   (8.00 с), иначе на 161-м (8.05 с). Обе картины = эталон (поток оригинала тоже квантован по 50 мс).
			R.Expect(P.Num() == 14 || P.Num() == 15, FString::Printf(TEXT("H1: за 120 с пингов %d (ждали 14–15 — каждые 8.00–8.05 с)"), P.Num()));
			R.Expect(MinI >= 8.0 - 1e-9 && MaxI <= 8.05 + 1e-6, FString::Printf(TEXT("H1: интервалы %.3f…%.3f с (ждали [8.00; 8.05])"), MinI, MaxI));
			R.Expect(MinI >= 6.0, FString::Printf(TEXT("H2: нет интервала < 6 с (минимум %.3f) — серверный Warning честному не грозит"), MinI));
		}
		// H3 — ускорение ×1.5.
		{
			const TArray<double> I = Intervals(Simulate(N, 60.0, 1.5, false, true, 0xFF));
			double MaxI = 0.0;
			for (double X : I) { MaxI = FMath::Max(MaxI, X); }
			R.Expect(I.Num() > 0 && MaxI < 6.0, FString::Printf(TEXT("H3: часы ×1.5 → реальный интервал до %.3f с < 6 с — сервер пишет Warning (пингов %d)"), MaxI, I.Num()));
		}
		// H4 — ускорение ×1.3 (ниже порога ×1.33 оригинала).
		{
			const TArray<double> I = Intervals(Simulate(N, 60.0, 1.3, false, true, 0xFF));
			double MinI = 1e9;
			for (double X : I) { MinI = FMath::Min(MinI, X); }
			R.Expect(I.Num() > 0 && MinI > 6.0, FString::Printf(TEXT("H4: часы ×1.3 → реальный интервал от %.3f с > 6 с (порог оригинала ×1.33)"), MinI));
		}
		// H5 — скачок назад.
		{
			N->SetNetHeartbeatStampForTest(5000.0);
			const bool b1 = N->StepNetHeartbeat(5000.0 + 1.0, true, false, true, 0xFF);    // через 1 с — рано
			const bool b2 = N->StepNetHeartbeat(5000.0 - 20.0, true, false, true, 0xFF);   // часы ушли НАЗАД на 21 с от метки
			R.Expect(!b1 && b2, FString::Printf(TEXT("H5: скачок часов назад → пинг сразу (через 1 с: %d, после скачка: %d)"), b1 ? 1 : 0, b2 ? 1 : 0));
		}
		// H6 — код кика.
		{
			const int32 Cnt = Simulate(N, 30.0, 1.0, false, true, 6).Num();
			R.Expect(Cnt == 0, FString::Printf(TEXT("H6: при коде кика 6 пингов %d (ждали 0)"), Cnt));
		}
		// H7 — нет сессии / нет сокета; логин-сессия пингуется.
		{
			const int32 NoSess = Simulate(N, 30.0, 1.0, false, false, 0xFF).Num();
			const int32 NoSock = Simulate(N, 30.0, 1.0, true, false, 0xFF, /*bSock*/false).Num();
			const int32 Login = Simulate(N, 30.0, 1.0, true, false, 0xFF).Num();
			R.Expect(NoSess == 0 && NoSock == 0 && Login == 3, FString::Printf(TEXT("H7: без сессии %d, без сокета %d, логин-сессия 30 с %d (ждали 0/0/3)"), NoSess, NoSock, Login));
		}
		// H8/H9 — первая проверка без метки и граница ровно 8 с.
		{
			N->SetNetHeartbeatStampForTest(0.0);
			const bool bFirst = N->StepNetHeartbeat(777.0, true, false, true, 0xFF);
			const bool bStamped = FMath::IsNearlyEqual(N->GetNetHeartbeatStamp(), 777.0);
			R.Expect(!bFirst && bStamped, FString::Printf(TEXT("H8: без метки пинга нет, метка = момент проверки (%d/%d)"), bFirst ? 1 : 0, bStamped ? 1 : 0));
			const bool bAt8 = N->StepNetHeartbeat(777.0 + 8.0, true, false, true, 0xFF);
			const bool bAfter8 = N->StepNetHeartbeat(777.0 + 8.001, true, false, true, 0xFF);
			R.Expect(!bAt8 && bAfter8, FString::Printf(TEXT("H9: ровно 8.000 с — нет (%d), 8.001 с — да (%d)"), bAt8 ? 1 : 0, bAfter8 ? 1 : 0));
		}

		N->SetNetHeartbeatStampForTest(SavedStamp);   // метка подсистемы возвращена
		AsdaSelfTestKit::Finish(W, GCmd, Args.bQuit, LogAsdaHeartbeatTest, TEXT("ПУЛЬС"), R.Ok, R.Fail);
	}));

#endif // ASDA_DEV_CONSOLE
