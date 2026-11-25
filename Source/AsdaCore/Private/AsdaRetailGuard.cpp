// Copyright (c) 2026 @sitxovski (Discord)
// Ритейл-защита входов движка — разбор и правила у объявления (Public/AsdaRetailGuard.h). [ПОРТ] 16.10.2026, agent-retail-branding.
#include "AsdaRetailGuard.h"
#include "AsdaDevConsole.h"
#include "HAL/IConsoleManager.h"
#include "Misc/App.h"
#include "Misc/CommandLine.h"
#include "Misc/ConfigCacheIni.h"
#include "Misc/Parse.h"
#if PLATFORM_WINDOWS
#include "Windows/AllowWindowsPlatformTypes.h"
#include <windows.h>
#include <shellapi.h>
#include "Windows/HideWindowsPlatformTypes.h"
#endif

DEFINE_LOG_CATEGORY_STATIC(LogAsdaRetailGuard, Log, All);

namespace AsdaRetailGuardPriv
{
	// Правила «значение»: FParse::Value ищет ПОДСТРОКУ без границы слова (Parse.cpp, Strifind) — аргумент, где она встречается
	//   где угодно (без учёта регистра), убирается целиком. Источник каждого — разбор в BRANDING_CONSOLE_REPORT.md §3.
	static const TCHAR* const GValueRules[] = {
		TEXT("ExecCmds="),                         // UnrealEngine.cpp (вырезан в Shipping) + IoDispatcherConfig.cpp (s.PlatformIo)
		TEXT("EXEC="),                             // exec-файл команд (UnrealEngine.cpp, вырезан в Shipping)
		TEXT("DPCVars="), TEXT("DPCVar="), TEXT("ForceDPCVars="),   // cvar профиля устройства (DeviceProfileManager.cpp)
		TEXT("cvarsini="),                         // ConsoleVariables.ini из файла (ConfigCacheIni.cpp, вырезан в Shipping)
		TEXT("DeviceProfile="), TEXT("DP="),       // выбор профиля устройства — ДЕЙСТВУЕТ в Shipping
		TEXT("IniBootstrap="),                     // вся конфигурация из файла — ДЕЙСТВУЕТ в Shipping (с -textconfig)
		TEXT("BinaryConfigScalabilityOverride="),  // BinaryConfig<X>.ini из папки Config — ДЕЙСТВУЕТ в Shipping
		TEXT("ScalabilityIniPlatformOverride="),   // платформа Scalability.ini — ДЕЙСТВУЕТ в Shipping
		TEXT("FPS="), TEXT("BENCHMARKSECONDS="),   // шаг фиксированного времени (LaunchEngineLoop.cpp)
		TEXT("csvExecCmds="), TEXT("csvEventExecCmds="),            // команды CSV-профайлера (в Shipping профайлера нет)
		TEXT("CmdLineFile="), TEXT("ModulesBootstrap="),            // аргументы/модули из файла (вне Shipping/монолита)
		TEXT("iniFile="), TEXT("CustomConfig="), TEXT("ini:"),      // подмена ini (ALLOW_INI_OVERRIDE_FROM_COMMANDLINE)
		TEXT("ConfigReplayMethod="),               // режим слоёв конфигурации
	};
	// Правила «ключ»: FParse::Param — аргумент целиком равен -имя или /имя (без учёта регистра).
	static const TCHAR* const GParamRules[] = {
		TEXT("UseFixedTimeStep"), TEXT("Deterministic"), TEXT("FixedSeed"),   // фиксированный шаг — ДЕЙСТВУЕТ в Shipping
		TEXT("BENCHMARK"),                         // режим замера (в Shipping сброшен движком)
		TEXT("textconfig"),                        // текстовая конфигурация вместо BinaryConfig — ДЕЙСТВУЕТ в Shipping
		TEXT("writeIniOnLoad"), TEXT("SkipConfigRulesAllowList"),
	};
	// Ключ самопроверки Shipping: записать итог очистки в файл и выйти до движка.
	static const TCHAR* const GSelfCheckKey = TEXT("-AsdaRetailGuardSelfCheck=");

	// ASCII-нижний регистр (правила — ASCII; кириллица в аргументах не совпадёт ни с одним правилом).
	static inline TCHAR Lower(TCHAR C) { return (C >= TEXT('A') && C <= TEXT('Z')) ? TCHAR(C - TEXT('A') + TEXT('a')) : C; }

	// Длина строки без CRT (работает до инициализации чего угодно).
	static int32 Len(const TCHAR* S) { int32 N = 0; while (S && S[N]) { ++N; } return N; }

	// Подстрока Needle в Hay без учёта регистра.
	static bool ContainsI(const TCHAR* Hay, const TCHAR* Needle)
	{
		const int32 HL = Len(Hay), NL = Len(Needle);
		for (int32 I = 0; I + NL <= HL; ++I)
		{
			int32 J = 0;
			while (J < NL && Lower(Hay[I + J]) == Lower(Needle[J])) { ++J; }
			if (J == NL) { return true; }
		}
		return false;
	}

	// Равенство без учёта регистра.
	static bool EqualsI(const TCHAR* A, const TCHAR* B)
	{
		const int32 AL = Len(A);
		if (AL != Len(B)) { return false; }
		for (int32 I = 0; I < AL; ++I) { if (Lower(A[I]) != Lower(B[I])) { return false; } }
		return true;
	}

	// Начинается ли A с префикса P (без учёта регистра).
	static bool StartsWithI(const TCHAR* A, const TCHAR* P)
	{
		const int32 PL = Len(P);
		if (Len(A) < PL) { return false; }
		for (int32 I = 0; I < PL; ++I) { if (Lower(A[I]) != Lower(P[I])) { return false; } }
		return true;
	}

	// Итог очистки до WinMain (Shipping): имена сработавших правил (указатели на литералы — без выделения памяти).
	static const TCHAR* GPreMainRemoved[64] = {};
	static int32 GPreMainRemovedNum = 0;      // сколько аргументов убрано до WinMain
	static bool GPreMainFallback = false;     // пересборка не прошла сверку — оставлен только путь exe
	static bool GPreMainRan = false;          // очистка до WinMain выполнялась (монолитный Shipping)

	static int32 GProbeRuns = 0;              // запуски дев-пробы asda.RetailGuard.Probe
	static int32 GExecBlocked = 0;            // отклонённые команды Exec

	// Журнал старта (строки для самотеста и Development-лога).
	static TArray<FString>& StartupLog() { static TArray<FString> L; return L; }

#if PLATFORM_WINDOWS
	// Буфер символов на куче процесса (без аллокатора UE — работает до main).
	struct FRawBuf
	{
		wchar_t* P = nullptr;   // данные
		int32 N = 0;            // длина
		int32 Cap = 0;          // ёмкость (символов)
		explicit FRawBuf(int32 InCap) : Cap(InCap) { P = (wchar_t*)::HeapAlloc(::GetProcessHeap(), HEAP_ZERO_MEMORY, sizeof(wchar_t) * (InCap + 1)); }
		~FRawBuf() { if (P) { ::HeapFree(::GetProcessHeap(), 0, P); } }
		void Put(wchar_t C) { if (P && N < Cap) { P[N++] = C; P[N] = 0; } }
		void Put(const wchar_t* S, int32 L) { for (int32 I = 0; I < L; ++I) { Put(S[I]); } }
	};

	// Кавычки по правилам CommandLineToArgvW (обратные косые перед кавычкой удваиваются) — для буфера GetCommandLineW.
	static void PutArgvQuoted(FRawBuf& B, const wchar_t* Arg)
	{
		bool bNeed = Arg[0] == 0;
		for (const wchar_t* C = Arg; *C; ++C) { if (*C == L' ' || *C == L'\t' || *C == L'\n' || *C == L'\v' || *C == L'"') { bNeed = true; } }
		if (!bNeed) { B.Put(Arg, Len(Arg)); return; }
		B.Put(L'"');
		for (const wchar_t* C = Arg;; ++C)
		{
			int32 Slashes = 0;
			while (*C == L'\\') { ++C; ++Slashes; }
			if (*C == 0) { for (int32 I = 0; I < Slashes * 2; ++I) { B.Put(L'\\'); } break; }
			if (*C == L'"') { for (int32 I = 0; I < Slashes * 2 + 1; ++I) { B.Put(L'\\'); } B.Put(L'"'); }
			else { for (int32 I = 0; I < Slashes; ++I) { B.Put(L'\\'); } B.Put(*C); }
		}
		B.Put(L'"');
	}

	// Кавычки в формате FCommandLine (как ProcessCommandLine в LaunchWindows.cpp): -Ключ="значение с пробелами".
	static void PutUeQuoted(FString& Out, const FString& Arg)
	{
		if (!Arg.Contains(TEXT(" ")))
		{
			Out += Arg;
			return;
		}
		int32 Quote = 0;
		int32 Sep = INDEX_NONE;
		if (Arg.StartsWith(TEXT("-")) && Arg.FindChar(TEXT('='), Sep)) { Quote = Sep + 1; }
		Out += Arg.Left(Quote) + TEXT("\"") + Arg.Mid(Quote) + TEXT("\"");
	}

	// Сырой диапазон argv[0] (правило CommandLineToArgvW для имени программы: в кавычках — до кавычки, иначе — до пробела).
	static int32 Argv0Span(const wchar_t* P)
	{
		int32 I = 0;
		if (P[0] == L'"')
		{
			I = 1;
			while (P[I] && P[I] != L'"') { ++I; }
			if (P[I] == L'"') { ++I; }
			return I;
		}
		while (P[I] && P[I] != L' ' && P[I] != L'\t') { ++I; }
		return I;
	}

	// UTF-16 → UTF-8 и запись в файл (самопроверка Shipping; без аллокатора UE).
	static void WriteUtf8(HANDLE H, const wchar_t* S, int32 L)
	{
		if (L <= 0) { return; }
		const int32 Need = ::WideCharToMultiByte(CP_UTF8, 0, S, L, nullptr, 0, nullptr, nullptr);
		if (Need <= 0) { return; }
		char* Tmp = (char*)::HeapAlloc(::GetProcessHeap(), 0, Need);
		if (!Tmp) { return; }
		::WideCharToMultiByte(CP_UTF8, 0, S, L, Tmp, Need, nullptr, nullptr);
		DWORD Wr = 0;
		::WriteFile(H, Tmp, (DWORD)Need, &Wr, nullptr);
		::HeapFree(::GetProcessHeap(), 0, Tmp);
	}
	static void WriteW(HANDLE H, const wchar_t* S) { WriteUtf8(H, S, Len(S)); }   // строка целиком
#endif
}

using namespace AsdaRetailGuardPriv;

bool AsdaRetailGuard::IsBannedArg(const TCHAR* Arg, const TCHAR** OutRule)
{
	if (!Arg) { return false; }
	for (const TCHAR* R : GValueRules)
	{
		if (ContainsI(Arg, R)) { if (OutRule) { *OutRule = R; } return true; }
	}
	if (Arg[0] == TEXT('-') || Arg[0] == TEXT('/'))
	{
		for (const TCHAR* R : GParamRules)
		{
			if (EqualsI(Arg + 1, R)) { if (OutRule) { *OutRule = R; } return true; }
		}
	}
	return false;
}

#if UE_BUILD_SHIPPING && PLATFORM_WINDOWS && IS_MONOLITHIC
// ── до WinMain: пересборка буфера GetCommandLineW() без запрещённых аргументов ────────────────────────────────────────
//   LaunchWindowsStartup разбирает именно этот буфер (CommandLineToArgvW(GetCommandLineW()) → GSavedCommandLine) — значит,
//   движок не увидит убранного ни на одном шаге (включая IniBootstrap/textconfig в InitializeConfigSystem до модулей).
namespace AsdaRetailGuardPreMain
{
	static void Run()
	{
		GPreMainRan = true;
		wchar_t* P = ::GetCommandLineW();
		if (!P) { return; }
		const int32 OrigLen = Len(P);
		int Argc = 0;
		LPWSTR* Argv = ::CommandLineToArgvW(P, &Argc);
		if (!Argv) { return; }
		const wchar_t* SelfCheckPath = nullptr;   // путь файла самопроверки (если задан ключ)
		bool bKeep[512];
		const int32 N = FMath::Min(Argc, 512);
		bool bAny = Argc > 512;                   // аргументов больше предела — оставляем только путь exe (безопасно)
		for (int32 I = 1; I < N; ++I)
		{
			const TCHAR* Rule = nullptr;
			bKeep[I] = !AsdaRetailGuard::IsBannedArg(Argv[I], &Rule);
			if (!bKeep[I])
			{
				bAny = true;
				if (GPreMainRemovedNum < (int32)UE_ARRAY_COUNT(GPreMainRemoved)) { GPreMainRemoved[GPreMainRemovedNum] = Rule; }
				++GPreMainRemovedNum;
			}
			if (StartsWithI(Argv[I], GSelfCheckKey)) { SelfCheckPath = Argv[I] + Len(GSelfCheckKey); }
		}
		if (bAny)
		{
			const int32 A0 = Argv0Span(P);
			int32 Cap = A0 + 2;
			for (int32 I = 1; I < N; ++I) { Cap += Len(Argv[I]) * 2 + 4; }
			FRawBuf B(Cap);
			B.Put(P, A0);
			int32 Kept = 0;
			if (Argc <= 512)
			{
				for (int32 I = 1; I < N; ++I)
				{
					if (!bKeep[I]) { continue; }
					B.Put(L' ');
					PutArgvQuoted(B, Argv[I]);
					++Kept;
				}
			}
			// Сверка: новый буфер разбирается обратно ровно в сохранённые аргументы и не длиннее исходного.
			bool bOk = B.P && B.N <= OrigLen;
			if (bOk)
			{
				int Argc2 = 0;
				LPWSTR* Argv2 = ::CommandLineToArgvW(B.P, &Argc2);
				bOk = Argv2 && Argc2 == Kept + 1;
				for (int32 I = 1, K = 1; bOk && I < N; ++I)
				{
					if (!bKeep[I]) { continue; }
					bOk = ::lstrcmpW(Argv[I], Argv2[K]) == 0;
					++K;
				}
				if (Argv2) { ::LocalFree(Argv2); }
			}
			if (bOk)
			{
				::memcpy(P, B.P, sizeof(wchar_t) * B.N);
				P[B.N] = 0;
			}
			else
			{
				// Не сошлось — оставляем только путь exe: игре аргументы не нужны (бутстрапер EAC запускает её без ключей).
				GPreMainFallback = true;
				P[A0] = 0;
			}
		}
		if (SelfCheckPath && *SelfCheckPath)
		{
			// Самопроверка упаковки: итог очистки в файл и немедленный выход — движок, окна, сеть и EAC не поднимаются.
			HANDLE H = ::CreateFileW(SelfCheckPath, GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
			if (H != INVALID_HANDLE_VALUE)
			{
				WriteW(H, ASDA_RETAIL_GUARD_TAG);
				WriteW(H, L"\nremoved=");
				for (int32 I = 0; I < FMath::Min(GPreMainRemovedNum, (int32)UE_ARRAY_COUNT(GPreMainRemoved)); ++I)
				{
					if (I) { WriteW(H, L","); }
					WriteW(H, GPreMainRemoved[I]);
				}
				WriteW(H, GPreMainFallback ? L"\nfallback=1\ncmdline=" : L"\nfallback=0\ncmdline=");
				WriteW(H, ::GetCommandLineW());
				WriteW(H, L"\n");
				::CloseHandle(H);
			}
			::LocalFree(Argv);
			::TerminateProcess(::GetCurrentProcess(), 0);
		}
		::LocalFree(Argv);
	}

	// Статическая инициализация монолитного exe идёт до WinMain.
	struct FAtStartup { FAtStartup() { Run(); } };
	static FAtStartup GAtStartup;
}
#endif

bool AsdaRetailGuard::IsActive()
{
#if UE_BUILD_SHIPPING
	return true;
#else
	static const bool bFlag = FParse::Param(FCommandLine::Get(), TEXT("AsdaRetailGuard"));
	return bFlag;
#endif
}

FString AsdaRetailGuard::FilterCommandLine(const TCHAR* CmdLine, TArray<FString>* OutRemoved)
{
	if (!CmdLine) { return FString(); }
#if PLATFORM_WINDOWS
	const FString Full = FString(TEXT("x ")) + CmdLine;   // argv[0] — заглушка, как у LaunchWindowsStartup
	int Argc = 0;
	LPWSTR* Argv = ::CommandLineToArgvW(*Full, &Argc);
	if (!Argv) { return CmdLine; }
	FString Out;
	bool bAny = false;
	for (int32 I = 1; I < Argc; ++I)
	{
		const TCHAR* Rule = nullptr;
		if (IsBannedArg(Argv[I], &Rule))
		{
			bAny = true;
			if (OutRemoved) { OutRemoved->Add(Rule); }
			continue;
		}
		Out += TEXT(" ");
		PutUeQuoted(Out, Argv[I]);
	}
	::LocalFree(Argv);
	return bAny ? Out : FString(CmdLine);
#else
	return CmdLine;
#endif
}

int32 AsdaRetailGuard::StripUserSavedConfig(TArray<FString>* OutLog)
{
	if (!GConfig) { return 0; }
	int32 Removed = 0;
	for (const FString& Name : GConfig->GetFilenames())
	{
		FConfigBranch* B = GConfig->FindBranchWithNoReload(NAME_None, Name);
		if (!B || B->SavedLayer.Num() == 0) { continue; }
		const FString Ini = B->IniName.ToString();
		if (Ini == TEXT("GameUserSettings")) { continue; }   // настройки игрока (разрешение, звук, графика) — законный файл игры
#if !UE_BUILD_SHIPPING
		if (Ini.Contains(TEXT("Editor"))) { continue; }      // Development-самотест: редакторские файлы не трогаем
#endif
		TArray<FString> Sections;
		B->SavedLayer.GetKeys(Sections);
		for (const FString& S : Sections)
		{
			// Сохранённый слой применяется заменой секции целиком (bIsSavedConfigFile) — обратное действие: секция из pak.
			B->InMemoryFile.Remove(S);
			if (const FConfigSection* Base = B->FinalCombinedLayers.FindSection(S))
			{
				B->InMemoryFile.Add(S, *Base);
			}
			++Removed;
			if (OutLog) { OutLog->Add(FString::Printf(TEXT("%s [%s]"), *Ini, *S)); }
		}
		B->SavedLayer.Empty();
	}
	return Removed;
}

bool AsdaRetailGuard::IsExecAllowed(const TCHAR* Cmd, FString* OutWhy)
{
	FString S(Cmd ? Cmd : TEXT(""));
	S.TrimStartAndEndInline();
	TArray<FString> Parts;
	S.ParseIntoArray(Parts, TEXT("|"), true);
	for (FString& Part : Parts)
	{
		Part.TrimStartAndEndInline();
		FString Tok = Part;
		int32 Sp = INDEX_NONE;
		if (Part.FindChar(TEXT(' '), Sp)) { Tok = Part.Left(Sp); }
		if (Tok.IsEmpty() || Tok.Equals(TEXT("quit"), ESearchCase::IgnoreCase) || Tok.Equals(TEXT("exit"), ESearchCase::IgnoreCase))
		{
			continue;
		}
		IConsoleVariable* Var = IConsoleManager::Get().FindConsoleVariable(*Tok, false);
		if (Var && !Var->TestFlags(ECVF_Cheat))
		{
			continue;   // существующий НЕ-читерский cvar (настройки графики/звука; движок сам применяет SetBy-приоритеты)
		}
		if (OutWhy) { *OutWhy = FString::Printf(TEXT("«%s» — не quit/exit и не обычный cvar"), *Tok); }
		return false;
	}
	return true;
}

void AsdaRetailGuard::NoteExecBlocked(const TCHAR* Cmd)
{
	++GExecBlocked;
	UE_LOG(LogAsdaRetailGuard, Warning, TEXT("RETAILGUARD: Exec отклонён: «%s»"), Cmd ? Cmd : TEXT(""));
}

int32 AsdaRetailGuard::GetExecBlockedCount() { return GExecBlocked; }

const TArray<FString>& AsdaRetailGuard::GetStartupLog() { return StartupLog(); }

int32 AsdaRetailGuard::GetProbeRuns() { return GProbeRuns; }

void AsdaRetailGuard::OnModuleStartup()
{
	if (!IsActive()) { return; }
	TArray<FString>& L = StartupLog();
	L.Add(FString::Printf(TEXT("%s: активна (до WinMain: %s, убрано %d%s)"), ASDA_RETAIL_GUARD_TAG,
		GPreMainRan ? TEXT("да") : TEXT("нет — не монолитный Shipping"), GPreMainRemovedNum,
		GPreMainFallback ? TEXT(", оставлен только путь exe") : TEXT("")));
	for (int32 I = 0; I < FMath::Min(GPreMainRemovedNum, (int32)UE_ARRAY_COUNT(GPreMainRemoved)); ++I)
	{
		L.Add(FString::Printf(TEXT("до WinMain убран аргумент по правилу %s"), GPreMainRemoved[I]));
	}
	// FCommandLine (Development: модульная сборка, очистки до WinMain нет; Shipping — повторная страховка).
	TArray<FString> Removed;
	const FString Clean = FilterCommandLine(FCommandLine::Get(), &Removed);
	if (Removed.Num() > 0)
	{
		FCommandLine::Set(*Clean);
		for (const FString& R : Removed) { L.Add(FString::Printf(TEXT("FCommandLine: убран аргумент по правилу %s"), *R)); }
	}
	// Фиксированный шаг времени (-UseFixedTimeStep/-Deterministic разобраны движком раньше модулей) — выключить.
	FApp::SetUseFixedTimeStep(false);
	FApp::bUseFixedSeed = false;
	// Пользовательские ini — до применения [SystemSettings]/[ConsoleVariables] (фаза модуля EarliestPossible).
	TArray<FString> IniLog;
	const int32 NIni = StripUserSavedConfig(&IniLog);
	for (const FString& S : IniLog) { L.Add(FString::Printf(TEXT("снят пользовательский слой ini: %s"), *S)); }
	L.Add(FString::Printf(TEXT("итог старта: аргументов убрано %d, секций ini снято %d"), GPreMainRemovedNum + Removed.Num(), NIni));
	for (const FString& S : L) { UE_LOG(LogAsdaRetailGuard, Display, TEXT("RETAILGUARD: %s"), *S); }
}

#if ASDA_DEV_CONSOLE
// Дев-проба для самотеста: считает запуски и пишет маркер в лог. Через -ExecCmds/Exec при активной защите выполняться не должна.
static FAutoConsoleCommand GAsdaRetailGuardProbe(
	TEXT("asda.RetailGuard.Probe"),
	TEXT("Проба ритейл-защиты (самотест Tools/asda_retail_guard_check.py): пишет RETAILGUARD-PROBE-RAN <аргументы>."),
	FConsoleCommandWithArgsDelegate::CreateLambda([](const TArray<FString>& Args)
	{
		++GProbeRuns;
		UE_LOG(LogAsdaRetailGuard, Display, TEXT("RETAILGUARD-PROBE-RAN %s"), *FString::Join(Args, TEXT(" ")));
	}));
#endif
