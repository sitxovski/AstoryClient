// Copyright (c) 2026 @sitxovski (Discord)
// ★★★ ОБЛАСТЬ ЗАМЕРА ВРЕМЕНИ ИГРОВОГО ПОТОКА: ОДНА ТОЧКА (R4, шаг M0, доля c; 14.11.2026). [ПОРТ]
//
// ЗАЧЕМ. Волна R4 (производительность) мерит не кадровую частоту (владелец на RDP — FPS и время GPU не критерий), а время
//   игрового потока в самом участке кода. До M0 общих средств не было — три частных счётчика (`FAsdaFxBenchTickScope`,
//   `UAsdaVisibilitySubsystem::EvalCycles`, `AsdaOverheadOcclusion::GCycles`), каждый под свою команду. Здесь — общий накопитель:
//   число вызовов, сумма, максимум, выборка для медианы и 95-го перцентиля, то же по кадрам (сумма участка за кадр).
//
// КАК ПОЛЬЗОВАТЬСЯ.
//   #include "AsdaPerf.h"
//   void UFoo::Tick(float Dt)
//   {
//       ASDA_PERF_SCOPE("Foo.Tick");          // время от этой строки до конца блока; имя — строковый литерал
//       …
//   }
//   ASDA_PERF_VALUE("Foo.Objects", N);        // число без времени (сколько объектов обошли, сколько строк собрали)
//   Вывод: `asda.Perf.Dump [file=<путь>] [<часть имени>]`, сброс: `asda.Perf.Reset`, выключить сбор: `asda.Perf.Collect 0`.
//   Серии «ключ 0/1 в одном процессе»: `asda.Perf.Series` (модуль AsdaReborn, `Private/Test/AsdaPerfSeries.cpp`),
//   снаружи — `Tools/asda_perf_series.py`. Описание: `Reports/refactor_2026-11/r4/c/M0.md`.
//
// ПРАВИЛА.
//   • Только игровой поток: вызов с другого потока в накопитель НЕ попадает (считается отдельно — «вне потока»), гонок нет.
//   • В Shipping всё пусто: макросы раскрываются в ничто, имён областей в бинаре нет (`ASDA_PERF` = 0).
//   • Часы — счётчик тактов процессора (`__rdtsc`, ≈ 0,3 нс), а не `FPlatformTime::Cycles64` (на Windows это QPC с шагом
//     100 нс — для участков в десятки наносекунд слишком грубо); перевод в микросекунды — калибровкой по QPC за всё время
//     от первой области до вывода.
//   • Цена самой области ≈ два чтения счётчика + вызов `Add` (десятки нс) и в замер участка НЕ входит (начало берётся
//     последним действием конструктора, конец — первым действием деструктора), но входит в замер ОБЪЕМЛЮЩЕЙ области.
//   • «Кадр» в столбцах `кадр_*` — кадр, в котором участок вызывался хотя бы раз (кадры без вызовов не считаются).
#pragma once

#include "CoreMinimal.h"

// 1 — области замера работают (Debug/Development/Test, редактор); 0 — Shipping (пусто).
#ifndef ASDA_PERF
	#define ASDA_PERF (!UE_BUILD_SHIPPING)
#endif

#if ASDA_PERF

#if PLATFORM_WINDOWS && PLATFORM_CPU_X86_FAMILY
	#include <intrin.h>
#endif
#include "HAL/PlatformTime.h"

// Накопитель одной области. Живёт до конца процесса (адрес стабилен — его держит `static` в месте вызова).
struct FAsdaPerfStat
{
	static constexpr int32 kSamples = 4096;   // размер выборки (резервуар): по вызовам и по кадрам

	const TCHAR* Name = TEXT("");   // имя области (копия строки, живёт до конца процесса)
	bool bValue = false;            // true — числа без времени (`ASDA_PERF_VALUE`), в микросекунды не переводятся
	uint64 Calls = 0;               // вызовов на игровом потоке
	uint64 Sum = 0;                 // сумма (такты либо значения)
	uint64 Max = 0;                 // максимум одного вызова
	uint64 OffThread = 0;           // вызовов с других потоков (в накопитель не вошли)
	uint64 Frames = 0;              // кадров с хотя бы одним вызовом (закрытых)
	uint64 FrameMax = 0;            // максимум суммы за кадр
	uint64 FrameNo = MAX_uint64;    // номер текущего (ещё не закрытого) кадра; MAX — кадра нет
	uint64 FrameSum = 0;            // сумма за текущий кадр
	uint32 FrameCalls = 0;          // вызовов в текущем кадре
	uint32 Rng = 0x9E3779B9u;       // состояние генератора резервуара (детерминированное)
	int32 NumCallSamples = 0;       // заполнено в выборке по вызовам
	int32 NumFrameSamples = 0;      // заполнено в выборке по кадрам
	uint32 CallSamples[kSamples];   // выборка по вызовам (насыщение на MAX_uint32)
	uint32 FrameSamples[kSamples];  // выборка сумм за кадр

	// Учесть один вызов (такты либо значение). С чужого потока — только `OffThread`.
	ASDACORE_API void Add(uint64 V);
	// Закрыть текущий кадр (сумму — в выборку по кадрам). Зовётся из `Add` при смене кадра и перед снимком.
	ASDACORE_API void FlushFrame();
	// Обнулить накопленное (имя и вид остаются).
	ASDACORE_API void Reset();
};

// Строка снимка: накопленное одной областью, время — в микросекундах (у областей-значений — в единицах значения).
struct FAsdaPerfRow
{
	FString Name;             // имя области
	bool bValue = false;      // числа без времени
	uint64 Calls = 0;         // вызовов
	uint64 OffThread = 0;     // вызовов вне игрового потока
	double Sum = 0.0;         // сумма
	double Mean = 0.0;        // среднее на вызов
	double Med = 0.0;         // медиана на вызов (по выборке)
	double P95 = 0.0;         // 95-й перцентиль на вызов (по выборке)
	double Max = 0.0;         // максимум вызова
	uint64 Frames = 0;        // кадров с вызовами
	double FrameMean = 0.0;   // среднее за кадр
	double FrameMed = 0.0;    // медиана за кадр
	double FrameP95 = 0.0;    // 95-й перцентиль за кадр
	double FrameMax = 0.0;    // максимум за кадр
};

namespace AsdaPerf
{
	// Идёт ли сбор (ключ `asda.Perf.Collect`, умолчание 1). Читается областью до чтения часов.
	ASDACORE_API extern bool GCollect;

	// Часы области: такты процессора (на прочих платформах — `FPlatformTime::Cycles64`).
	FORCEINLINE uint64 Now()
	{
#if PLATFORM_WINDOWS && PLATFORM_CPU_X86_FAMILY
		return __rdtsc();
#else
		return FPlatformTime::Cycles64();
#endif
	}

	// Накопитель по имени (одно имя — один накопитель, откуда бы ни звали). Потокобезопасно; `bValue` задаёт вид при создании.
	ASDACORE_API FAsdaPerfStat& Register(const TCHAR* Name, bool bValue = false);
	// Обнулить все накопители.
	ASDACORE_API void Reset();
	// Тактов часов области в одной микросекунде (калибровка по QPC с момента первой области; при коротком интервале —
	//   занятое ожидание до 20 мс).
	ASDACORE_API double TicksPerUs();
	// Снимок: строки по областям с вызовами (или всем при `bWithEmpty`), имя содержит `Filter` (пусто — все), по имени.
	ASDACORE_API void Snapshot(TArray<FAsdaPerfRow>& Out, const FString& Filter = FString(), bool bWithEmpty = false);
	// Заголовок таблицы (табуляция) и строка таблицы для снимка.
	ASDACORE_API FString TsvHeader();
	ASDACORE_API FString TsvRow(const FAsdaPerfRow& R);
	// Записать снимок в файл UTF-8 (заголовок + строки; `Note` — строки-комментарии `# …` в начале). false — не записалось.
	ASDACORE_API bool DumpToFile(const FString& Path, const FString& Filter = FString(), const FString& Note = FString());
	// Перцентиль `P` (0..1) отсортированной выборки (ближайший ранг); пустая — 0.
	ASDACORE_API double Percentile(const TArray<uint32>& Sorted, double P);
}

// Область замера: от создания до уничтожения.
struct FAsdaPerfScope
{
	FAsdaPerfStat* S;   // накопитель (nullptr — сбор выключен)
	uint64 T0;          // такты входа

	FORCEINLINE explicit FAsdaPerfScope(FAsdaPerfStat& In) : S(AsdaPerf::GCollect ? &In : nullptr), T0(0) { if (S) { T0 = AsdaPerf::Now(); } }
	FORCEINLINE ~FAsdaPerfScope() { if (S) { const uint64 T1 = AsdaPerf::Now(); S->Add(T1 - T0); } }
	FAsdaPerfScope(const FAsdaPerfScope&) = delete;
	FAsdaPerfScope& operator=(const FAsdaPerfScope&) = delete;
};

#define ASDA_PERF_JOIN2(A, B) A##B
#define ASDA_PERF_JOIN(A, B) ASDA_PERF_JOIN2(A, B)
// Время от этой строки до конца блока — в накопитель `NameLiteral` (строковый литерал без TEXT()).
#define ASDA_PERF_SCOPE(NameLiteral) \
	static FAsdaPerfStat& ASDA_PERF_JOIN(AsdaPerfStat_, __LINE__) = AsdaPerf::Register(TEXT(NameLiteral)); \
	FAsdaPerfScope ASDA_PERF_JOIN(AsdaPerfScope_, __LINE__)(ASDA_PERF_JOIN(AsdaPerfStat_, __LINE__))
// Число без времени — в накопитель-значение `NameLiteral`.
#define ASDA_PERF_VALUE(NameLiteral, V) \
	do { static FAsdaPerfStat& AsdaPerfValueStat = AsdaPerf::Register(TEXT(NameLiteral), true); \
	     if (AsdaPerf::GCollect) { AsdaPerfValueStat.Add((uint64)(V)); } } while (0)

#else   // !ASDA_PERF — Shipping: пусто, имён областей в бинаре нет

#define ASDA_PERF_SCOPE(NameLiteral)
#define ASDA_PERF_VALUE(NameLiteral, V)

#endif  // ASDA_PERF
