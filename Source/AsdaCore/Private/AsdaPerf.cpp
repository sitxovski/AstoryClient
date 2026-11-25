// Copyright (c) 2026 @sitxovski (Discord)
// ★ R4, шаг M0 (доля c; 14.11.2026) [ПОРТ]: накопитель областей замера времени игрового потока и команды
//   `asda.Perf.Dump` / `asda.Perf.Reset` / ключ `asda.Perf.Collect`. Разбор и правила — `AsdaPerf.h`.
#include "AsdaPerf.h"

#if ASDA_PERF

#include "AsdaDevConsole.h"
#include "HAL/CriticalSection.h"
#include "HAL/FileManager.h"
#include "HAL/IConsoleManager.h"
#include "HAL/PlatformTime.h"
#include "Misc/DateTime.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "Misc/ScopeLock.h"

DEFINE_LOG_CATEGORY_STATIC(LogAsdaPerf, Log, All);

bool AsdaPerf::GCollect = true;

// Ключ сбора: 0 — области не читают часы и ничего не копят (накопленное остаётся).
static FAutoConsoleVariableRef CVarAsdaPerfCollect(TEXT("asda.Perf.Collect"), AsdaPerf::GCollect,
	TEXT("[ПОРТ] Области замера времени игрового потока (AsdaPerf.h): 1 — копят (умолчание), 0 — выключены."), ECVF_Default);

namespace AsdaPerfImpl
{
	static FCriticalSection GLock;              // реестр накопителей (создание с любого потока)
	static TArray<FAsdaPerfStat*> GStats;       // все накопители (живут до конца процесса)
	static uint64 GCalTsc = 0;                  // такты области в точке калибровки
	static double GCalSec = 0.0;                // секунды QPC в точке калибровки
	static bool bCal = false;                   // точка калибровки снята

	// Снять точку калибровки (первый накопитель).
	static void Calibrate()
	{
		if (!bCal) { GCalTsc = AsdaPerf::Now(); GCalSec = FPlatformTime::Seconds(); bCal = true; }
	}

	// Перцентили выборки в единицах `PerUnit` (тактов на единицу; 1 — без перевода).
	static void Pct(const uint32* Src, int32 Num, double PerUnit, double& OutMed, double& OutP95)
	{
		TArray<uint32> A;
		A.Append(Src, Num);
		A.Sort();
		OutMed = AsdaPerf::Percentile(A, 0.5) / PerUnit;
		OutP95 = AsdaPerf::Percentile(A, 0.95) / PerUnit;
	}

	// Положить значение в резервуар: первые kSamples — подряд, дальше — с вероятностью kSamples/Seen на случайное место.
	static FORCEINLINE void Sample(uint32* Buf, int32& Num, uint64 Seen, uint32& Rng, uint64 V)
	{
		const uint32 V32 = V > (uint64)MAX_uint32 ? MAX_uint32 : (uint32)V;
		if (Num < FAsdaPerfStat::kSamples) { Buf[Num++] = V32; return; }
		Rng ^= Rng << 13; Rng ^= Rng >> 17; Rng ^= Rng << 5;   // xorshift32
		const uint64 J = (uint64)Rng % Seen;
		if (J < (uint64)FAsdaPerfStat::kSamples) { Buf[(int32)J] = V32; }
	}
}

void FAsdaPerfStat::Add(uint64 V)
{
	if (!IsInGameThread()) { FPlatformAtomics::InterlockedIncrement((volatile int64*)&OffThread); return; }
	const uint64 F = GFrameCounter;
	if (F != FrameNo) { FlushFrame(); FrameNo = F; }
	++Calls; Sum += V; if (V > Max) { Max = V; }
	FrameSum += V; ++FrameCalls;
	AsdaPerfImpl::Sample(CallSamples, NumCallSamples, Calls, Rng, V);
}

void FAsdaPerfStat::FlushFrame()
{
	if (FrameNo == MAX_uint64 || FrameCalls == 0) { return; }
	++Frames; if (FrameSum > FrameMax) { FrameMax = FrameSum; }
	AsdaPerfImpl::Sample(FrameSamples, NumFrameSamples, Frames, Rng, FrameSum);
	FrameSum = 0; FrameCalls = 0; FrameNo = MAX_uint64;
}

void FAsdaPerfStat::Reset()
{
	Calls = 0; Sum = 0; Max = 0; OffThread = 0; Frames = 0; FrameMax = 0; FrameNo = MAX_uint64; FrameSum = 0; FrameCalls = 0;
	Rng = 0x9E3779B9u; NumCallSamples = 0; NumFrameSamples = 0;
}

FAsdaPerfStat& AsdaPerf::Register(const TCHAR* Name, bool bValue)
{
	FScopeLock L(&AsdaPerfImpl::GLock);
	AsdaPerfImpl::Calibrate();
	for (FAsdaPerfStat* S : AsdaPerfImpl::GStats) { if (FCString::Strcmp(S->Name, Name) == 0) { return *S; } }
	FAsdaPerfStat* S = new FAsdaPerfStat();
	const int32 Len = FCString::Strlen(Name);
	TCHAR* Copy = new TCHAR[Len + 1];
	FCString::Strncpy(Copy, Name, Len + 1);
	S->Name = Copy; S->bValue = bValue;
	AsdaPerfImpl::GStats.Add(S);
	return *S;
}

void AsdaPerf::Reset()
{
	FScopeLock L(&AsdaPerfImpl::GLock);
	for (FAsdaPerfStat* S : AsdaPerfImpl::GStats) { S->Reset(); }
}

double AsdaPerf::TicksPerUs()
{
	{
		FScopeLock L(&AsdaPerfImpl::GLock);
		AsdaPerfImpl::Calibrate();
	}
	// Интервал калибровки не короче 20 мс (занятое ожидание — без сна, чтобы не отдать квант).
	while (FPlatformTime::Seconds() - AsdaPerfImpl::GCalSec < 0.020) { }
	const double Sec = FPlatformTime::Seconds() - AsdaPerfImpl::GCalSec;
	const uint64 Tsc = Now() - AsdaPerfImpl::GCalTsc;
	return Sec > 0.0 ? (double)Tsc / (Sec * 1.0e6) : 1.0;
}

double AsdaPerf::Percentile(const TArray<uint32>& Sorted, double P)
{
	const int32 N = Sorted.Num();
	if (N <= 0) { return 0.0; }
	const int32 I = FMath::Clamp(FMath::CeilToInt32(P * (double)N) - 1, 0, N - 1);   // ближайший ранг
	return (double)Sorted[I];
}

void AsdaPerf::Snapshot(TArray<FAsdaPerfRow>& Out, const FString& Filter, bool bWithEmpty)
{
	check(IsInGameThread());
	const double Tpu = TicksPerUs();
	FScopeLock L(&AsdaPerfImpl::GLock);
	for (FAsdaPerfStat* S : AsdaPerfImpl::GStats)
	{
		if (!Filter.IsEmpty() && !FString(S->Name).Contains(Filter)) { continue; }
		S->FlushFrame();
		if (S->Calls == 0 && S->OffThread == 0 && !bWithEmpty) { continue; }
		const double U = S->bValue ? 1.0 : Tpu;
		FAsdaPerfRow& R = Out.AddDefaulted_GetRef();
		R.Name = S->Name; R.bValue = S->bValue; R.Calls = S->Calls; R.OffThread = S->OffThread;
		R.Sum = (double)S->Sum / U; R.Mean = S->Calls ? R.Sum / (double)S->Calls : 0.0; R.Max = (double)S->Max / U;
		AsdaPerfImpl::Pct(S->CallSamples, S->NumCallSamples, U, R.Med, R.P95);
		R.Frames = S->Frames; R.FrameMean = S->Frames ? R.Sum / (double)S->Frames : 0.0; R.FrameMax = (double)S->FrameMax / U;
		AsdaPerfImpl::Pct(S->FrameSamples, S->NumFrameSamples, U, R.FrameMed, R.FrameP95);
	}
	Out.Sort([](const FAsdaPerfRow& A, const FAsdaPerfRow& B) { return A.Name < B.Name; });
}

FString AsdaPerf::TsvHeader()
{
	return TEXT("область\tвид\tвызовов\tвне_потока\tсумма\tсреднее\tмедиана\tp95\tмакс\tкадров\tкадр_среднее\tкадр_медиана\tкадр_p95\tкадр_макс");
}

FString AsdaPerf::TsvRow(const FAsdaPerfRow& R)
{
	return FString::Printf(TEXT("%s\t%s\t%llu\t%llu\t%.3f\t%.4f\t%.4f\t%.4f\t%.3f\t%llu\t%.3f\t%.3f\t%.3f\t%.3f"),
		*R.Name, R.bValue ? TEXT("число") : TEXT("мкс"), R.Calls, R.OffThread, R.Sum, R.Mean, R.Med, R.P95, R.Max,
		R.Frames, R.FrameMean, R.FrameMed, R.FrameP95, R.FrameMax);
}

bool AsdaPerf::DumpToFile(const FString& Path, const FString& Filter, const FString& Note)
{
	TArray<FAsdaPerfRow> Rows;
	Snapshot(Rows, Filter);
	FString Text = Note;
	if (!Text.IsEmpty() && !Text.EndsWith(TEXT("\n"))) { Text += TEXT("\n"); }
	Text += FString::Printf(TEXT("# asda.Perf.Dump %s; тактов в микросекунде %.2f; время — мкс игрового потока\n"),
		*FDateTime::Now().ToString(), TicksPerUs());
	Text += TsvHeader() + TEXT("\n");
	for (const FAsdaPerfRow& R : Rows) { Text += TsvRow(R) + TEXT("\n"); }
	IFileManager::Get().MakeDirectory(*FPaths::GetPath(Path), true);
	return FFileHelper::SaveStringToFile(Text, *Path, FFileHelper::EEncodingOptions::ForceUTF8WithoutBOM);
}

// `asda.Perf.Dump [file=<путь>] [<часть имени>]` — снимок в журнал; с `file=` — ещё и в файл (без пути: `file=` → Saved/AsdaPerf/).
static FAutoConsoleCommand GAsdaPerfDump(
	TEXT("asda.Perf.Dump"),
	TEXT("[ПОРТ] Области замера времени игрового потока (AsdaPerf.h): вывести накопленное в журнал. [file=<путь>] — ещё и в файл "
	     "(табуляция, UTF-8; пустой путь — Saved/AsdaPerf/perf_<время>.tsv); [<часть имени>] — только области с этим в имени."),
	FConsoleCommandWithArgsDelegate::CreateLambda([](const TArray<FString>& Args)
	{
		FString Filter, File; bool bFile = false;
		for (const FString& A : Args)
		{
			if (A.StartsWith(TEXT("file="))) { bFile = true; File = A.Mid(5); }
			else { Filter = A; }
		}
		TArray<FAsdaPerfRow> Rows;
		AsdaPerf::Snapshot(Rows, Filter);
		UE_LOG(LogAsdaPerf, Log, TEXT("ПЕРФ областей %d (тактов в мкс %.2f): %s"), Rows.Num(), AsdaPerf::TicksPerUs(),
			*AsdaPerf::TsvHeader().Replace(TEXT("\t"), TEXT(" | ")));
		for (const FAsdaPerfRow& R : Rows) { UE_LOG(LogAsdaPerf, Log, TEXT("ПЕРФ %s"), *AsdaPerf::TsvRow(R).Replace(TEXT("\t"), TEXT(" | "))); }
		if (bFile)
		{
			if (File.IsEmpty())
			{
				File = FPaths::Combine(FPaths::ProjectSavedDir(), TEXT("AsdaPerf"),
					FString::Printf(TEXT("perf_%s.tsv"), *FDateTime::Now().ToString(TEXT("%Y%m%d_%H%M%S"))));
			}
			const bool bOk = AsdaPerf::DumpToFile(File, Filter);
			UE_LOG(LogAsdaPerf, Log, TEXT("ПЕРФ файл %s: %s"), bOk ? TEXT("записан") : TEXT("НЕ ЗАПИСАН"), *File);
		}
	}));

// `asda.Perf.Reset` — обнулить все накопители.
static FAutoConsoleCommand GAsdaPerfReset(
	TEXT("asda.Perf.Reset"),
	TEXT("[ПОРТ] Области замера времени игрового потока (AsdaPerf.h): обнулить накопленное."),
	FConsoleCommandDelegate::CreateLambda([]()
	{
		AsdaPerf::Reset();
		UE_LOG(LogAsdaPerf, Log, TEXT("ПЕРФ сброс: накопители обнулены"));
	}));

#endif // ASDA_PERF
