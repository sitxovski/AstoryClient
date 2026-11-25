// Copyright (c) 2026 @sitxovski (Discord)
// ★ R4, шаг M0 (доля c; 14.11.2026) [ПОРТ]: САМОТЕСТ ИНСТРУМЕНТА ЗАМЕРА — `asda.Perf.SelfTest [quit]`, и учебная пара для
//   `asda.Perf.Series`: ключ `asda.Perf.SelfTestKey` + нагрузчик `asda.Perf.SelfTestSpin` (область `Perf.SelfTestSpin`).
//
// ЧТО ПРОВЕРЯЕТ (накопитель `AsdaPerf.h` — на известных числах, без догадок о времени):
//   A. область-значение 1..100: вызовов 100, сумма 5050, максимум 100, среднее 50,5, медиана 50, p95 95, один кадр;
//   B. резервуар: 10 000 одинаковых значений + один выброс — выборка ровно `kSamples`, медиана 7, максимум — выброс;
//   C. одно имя — один накопитель; сброс обнуляет; `asda.Perf.Collect 0` — область не копит;
//   D. вызов с чужого потока в накопитель не входит (считается «вне потока»);
//   E. область времени против QPC: занятое ожидание 2 мс — сумма области в пределах [1,9 мс; замер QPC + 5 %];
//   F. по кадрам: три кадра по два вызова (10 и 20) — кадров 3, сумма за кадр 30 (медиана и максимум);
//   G. файл снимка: записан, в нём заголовок и строка области A.
// В Shipping файла нет (макросы областей пусты, проверять нечего — пустоту держит сборка Shipping).
#include "CoreMinimal.h"

#include "AsdaDevConsole.h"
#include "AsdaPerf.h"

#if ASDA_DEV_CONSOLE && !UE_BUILD_SHIPPING

#include "Async/Async.h"
#include "Containers/Ticker.h"
#include "Engine/World.h"
#include "HAL/FileManager.h"
#include "HAL/IConsoleManager.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "Test/AsdaSelfTestKit.h"

DEFINE_LOG_CATEGORY_STATIC(LogAsdaPerfTest, Log, All);

// Учебный ключ правки для `asda.Perf.Series`: 1 — «новый» путь нагрузчика (короткий цикл), 0 — «старый» (втрое длиннее).
static ASDA_DEV_CVAR(int32, CVarAsdaPerfSelfTestKey, TEXT("asda.Perf.SelfTestKey"), 1,
	TEXT("[ПОРТ] Учебный ключ для asda.Perf.Series: нагрузчик asda.Perf.SelfTestSpin крутит 2000 шагов при 1 и 6000 при 0 "
	     "(результат один и тот же — сумма не зависит от ключа)."),
	ECVF_Default);

namespace AsdaPerfTest
{
	static const TCHAR* kCmd = TEXT("asda.Perf.SelfTest");   // имя команды

	// Нагрузчик учебной пары: «старый» путь считает ту же сумму втрое дольше. Возвращает сумму (равенство путей).
	static uint32 Spin(bool bNew)
	{
		ASDA_PERF_SCOPE("Perf.SelfTestSpin");
		volatile uint32 Acc = 0;
		const int32 Rounds = bNew ? 1 : 3;                 // старый путь пересчитывает то же трижды
		for (int32 R = 0; R < Rounds; ++R)
		{
			volatile uint32 Seed = 2166136261u;            // чтение volatile — чтобы компилятор не свернул три прохода в один
			uint32 H = Seed;
			for (uint32 I = 0; I < 2000u; ++I) { H = (H ^ I) * 16777619u; }
			Acc = H;
		}
		return Acc;
	}

	// Состояние прогона.
	struct FCtx
	{
		TWeakObjectPtr<UWorld> World;   // мир
		bool bQuit = false;             // выйти в конце
		int32 Ok = 0, Fail = 0;         // счёт проверок
		int32 FrameStep = 0;            // сколько кадров шага F пройдено
		uint64 LastFrame = MAX_uint64;  // кадр прошлого шага F
	};

	// Одна проверка.
	static void Chk(FCtx& C, bool bOk, const TCHAR* Name, const FString& Detail)
	{
		bOk ? ++C.Ok : ++C.Fail;
		UE_LOG(LogAsdaPerfTest, Log, TEXT("ПЕРФ-ТЕСТ %s %s — %s"), bOk ? TEXT("ok  ") : TEXT("FAIL"), Name, *Detail);
	}

	// Строка снимка по точному имени (нет — пустая строка с нулями).
	static FAsdaPerfRow Row(const TCHAR* Name)
	{
		TArray<FAsdaPerfRow> Rows;
		AsdaPerf::Snapshot(Rows, Name, true);
		for (const FAsdaPerfRow& R : Rows) { if (R.Name == Name) { return R; } }
		return FAsdaPerfRow();
	}

	// Шаги A–E и G (в одном кадре).
	static void RunSync(FCtx& C)
	{
		// A. известные числа.
		FAsdaPerfStat& Num = AsdaPerf::Register(TEXT("Perf.SelfTest.Num"), true);
		Num.Reset();
		for (uint64 V = 1; V <= 100; ++V) { Num.Add(V); }
		FAsdaPerfRow R = Row(TEXT("Perf.SelfTest.Num"));
		Chk(C, R.Calls == 100 && R.Sum == 5050.0 && R.Max == 100.0 && FMath::IsNearlyEqual(R.Mean, 50.5), TEXT("A1 счёт, сумма, максимум, среднее"),
			FString::Printf(TEXT("вызовов %llu, сумма %.0f, макс %.0f, среднее %.2f (ждём 100, 5050, 100, 50.50)"), R.Calls, R.Sum, R.Max, R.Mean));
		Chk(C, R.Med == 50.0 && R.P95 == 95.0, TEXT("A2 медиана и p95"), FString::Printf(TEXT("медиана %.0f, p95 %.0f (ждём 50 и 95)"), R.Med, R.P95));
		Chk(C, R.Frames == 1 && R.FrameMean == 5050.0 && R.FrameMax == 5050.0 && R.bValue, TEXT("A3 один кадр, вид «число»"),
			FString::Printf(TEXT("кадров %llu, за кадр %.0f, макс за кадр %.0f"), R.Frames, R.FrameMean, R.FrameMax));

		// B. резервуар.
		FAsdaPerfStat& Res = AsdaPerf::Register(TEXT("Perf.SelfTest.Res"), true);
		Res.Reset();
		for (int32 I = 0; I < 10000; ++I) { Res.Add(7); }
		Res.Add(1000000);
		R = Row(TEXT("Perf.SelfTest.Res"));
		Chk(C, Res.NumCallSamples == FAsdaPerfStat::kSamples && R.Calls == 10001 && R.Med == 7.0 && R.Max == 1000000.0, TEXT("B резервуар"),
			FString::Printf(TEXT("в выборке %d из %d, вызовов %llu, медиана %.0f, макс %.0f"), Res.NumCallSamples, FAsdaPerfStat::kSamples, R.Calls, R.Med, R.Max));

		// C. одно имя — один накопитель; сброс; ключ сбора.
		Chk(C, &AsdaPerf::Register(TEXT("Perf.SelfTest.Num"), true) == &Num, TEXT("C1 одно имя — один накопитель"), TEXT("адрес тот же"));
		Num.Reset();
		R = Row(TEXT("Perf.SelfTest.Num"));
		Chk(C, R.Calls == 0 && R.Sum == 0.0 && R.Frames == 0 && Num.NumCallSamples == 0, TEXT("C2 сброс"), FString::Printf(TEXT("вызовов %llu, кадров %llu"), R.Calls, R.Frames));
		FAsdaPerfStat& Tm = AsdaPerf::Register(TEXT("Perf.SelfTest.Time"));
		Tm.Reset();
		const bool bWas = AsdaPerf::GCollect;
		AsdaPerf::GCollect = false;
		{ FAsdaPerfScope Off(Tm); }
		AsdaPerf::GCollect = bWas;
		Chk(C, Tm.Calls == 0, TEXT("C3 asda.Perf.Collect 0 — область не копит"), FString::Printf(TEXT("вызовов %llu"), Tm.Calls));

		// D. чужой поток.
		Async(EAsyncExecution::Thread, [&Num]() { Num.Add(5); }).Wait();
		Chk(C, Num.Calls == 0 && Num.OffThread == 1, TEXT("D вызов с чужого потока — мимо накопителя"),
			FString::Printf(TEXT("вызовов %llu, вне потока %llu"), Num.Calls, Num.OffThread));

		// E. область времени против QPC.
		const double Q0 = FPlatformTime::Seconds();
		{
			FAsdaPerfScope On(Tm);
			while (FPlatformTime::Seconds() - Q0 < 0.002) { }
		}
		const double QUs = (FPlatformTime::Seconds() - Q0) * 1.0e6;
		R = Row(TEXT("Perf.SelfTest.Time"));
		Chk(C, R.Calls == 1 && R.Sum >= 1900.0 && R.Sum <= QUs * 1.05 && !R.bValue, TEXT("E область времени против QPC"),
			FString::Printf(TEXT("область %.1f мкс, QPC снаружи %.1f мкс, тактов в мкс %.2f"), R.Sum, QUs, AsdaPerf::TicksPerUs()));

		// G. файл снимка (до шага F — тот идёт по кадрам).
		Num.Reset();
		for (uint64 V = 1; V <= 100; ++V) { Num.Add(V); }
		const FString File = FPaths::Combine(FPaths::ProjectSavedDir(), TEXT("AsdaPerf"), TEXT("selftest.tsv"));
		const bool bSaved = AsdaPerf::DumpToFile(File, TEXT("Perf.SelfTest.Num"), TEXT("# самотест"));
		FString Text;
		const bool bRead = bSaved && FFileHelper::LoadFileToString(Text, *File);
		Chk(C, bRead && Text.StartsWith(TEXT("# самотест\n")) && Text.Contains(AsdaPerf::TsvHeader()) && Text.Contains(TEXT("Perf.SelfTest.Num\tчисло\t100\t0\t5050.000")),
			TEXT("G файл снимка"), FString::Printf(TEXT("записан %d, прочитан %d, длина %d"), bSaved ? 1 : 0, bRead ? 1 : 0, Text.Len()));
		IFileManager::Get().Delete(*File, false, true, true);
	}

	// Шаг F: три кадра по два вызова. false — тикер снят.
	static bool StepFrames(const TSharedRef<FCtx>& C)
	{
		FAsdaPerfStat& Fr = AsdaPerf::Register(TEXT("Perf.SelfTest.Frames"), true);
		if (C->LastFrame == GFrameCounter) { return true; }   // тикер дважды в одном кадре — ждём следующий
		C->LastFrame = GFrameCounter;
		if (C->FrameStep == 0) { Fr.Reset(); }
		if (C->FrameStep < 3) { Fr.Add(10); Fr.Add(20); ++C->FrameStep; return true; }
		const FAsdaPerfRow R = Row(TEXT("Perf.SelfTest.Frames"));
		Chk(*C, R.Calls == 6 && R.Frames == 3 && R.FrameMed == 30.0 && R.FrameMax == 30.0 && R.FrameMean == 30.0, TEXT("F по кадрам"),
			FString::Printf(TEXT("вызовов %llu, кадров %llu, за кадр медиана %.0f, макс %.0f (ждём 6, 3, 30, 30)"), R.Calls, R.Frames, R.FrameMed, R.FrameMax));
		// Учебная пара: оба пути нагрузчика дают одно и то же (самотест равенства старого и нового пути — образец для правок волны).
		const uint32 SNew = Spin(true), SOld = Spin(false);
		Chk(*C, SNew == SOld, TEXT("H равенство путей учебного нагрузчика"), FString::Printf(TEXT("новый %u, старый %u"), SNew, SOld));
		for (const TCHAR* N : { TEXT("Perf.SelfTest.Num"), TEXT("Perf.SelfTest.Res"), TEXT("Perf.SelfTest.Time"), TEXT("Perf.SelfTest.Frames") })
		{
			AsdaPerf::Register(N).Reset();   // за собой: области самотеста в чужой снимок не попадут
		}
		AsdaSelfTestKit::Finish(C->World.Get(), kCmd, C->bQuit, LogAsdaPerfTest, TEXT("ПЕРФ-ТЕСТ"), C->Ok, C->Fail);
		return false;
	}

	// Команда самотеста.
	static void Run(const TArray<FString>& Args, UWorld* W)
	{
		const FAsdaSelfTestArgs A = FAsdaSelfTestArgs::Parse(Args);
		const TSharedRef<FCtx> C = MakeShared<FCtx>();
		C->World = W; C->bQuit = A.bQuit;
		RunSync(*C);
		FTSTicker::GetCoreTicker().AddTicker(FTickerDelegate::CreateLambda([C](float) { return StepFrames(C); }), 0.f);
	}
}

// Консольная команда самотеста инструмента замера.
static FAutoConsoleCommandWithWorldAndArgs GAsdaPerfSelfTest(
	TEXT("asda.Perf.SelfTest"),
	TEXT("[ПОРТ] Самотест инструмента замера AsdaPerf.h (накопитель, резервуар, кадры, чужой поток, часы против QPC, файл). [quit]."),
	FConsoleCommandWithWorldAndArgsDelegate::CreateLambda([](const TArray<FString>& A, UWorld* W) { AsdaPerfTest::Run(A, W); }));

// Нагрузчик учебной пары: `asda.Perf.SelfTestSpin` — один проход пути по ключу `asda.Perf.SelfTestKey`.
static FAutoConsoleCommand GAsdaPerfSelfTestSpin(
	TEXT("asda.Perf.SelfTestSpin"),
	TEXT("[ПОРТ] Учебный нагрузчик для asda.Perf.Series: один проход области Perf.SelfTestSpin по ключу asda.Perf.SelfTestKey."),
	FConsoleCommandDelegate::CreateLambda([]() { AsdaPerfTest::Spin(CVarAsdaPerfSelfTestKey.GetValueOnGameThread() != 0); }));

#endif // ASDA_DEV_CONSOLE && !UE_BUILD_SHIPPING
