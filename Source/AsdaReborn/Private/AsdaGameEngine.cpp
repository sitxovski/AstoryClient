// Copyright (c) 2026 @sitxovski (Discord)
// Подкласс игрового движка порта — разбор у объявления (Public/AsdaGameEngine.h).
#include "AsdaGameEngine.h"
#include "AsdaSelfTestBatch.h"
#include "AsdaRetailGuard.h"
#include "Engine/GameViewportClient.h"
#include "Engine/Console.h"
#include "GameFramework/CheatManagerDefines.h"
#include "Misc/CommandLine.h"
#include "Misc/ConfigCacheIni.h"
#include "Misc/Parse.h"
#include "ProfilingDebugging/CsvProfilerConfig.h"

#if UE_BUILD_SHIPPING
// ★ 16.10.2026 (ритейл-защита, BRANDING_CONSOLE_REPORT.md §3): что Shipping закрывает САМ — закреплено сборкой. Если умолчания
//   движка поменяются (новая версия, флаг Target), сборка упадёт здесь, а не тихо откроет вход.
static_assert(!ALLOW_CONSOLE, "Shipping: консоль (UConsole, клавиша ~) должна быть вырезана движком (Build.h ALLOW_CONSOLE)");
static_assert(!UE_WITH_CHEAT_MANAGER, "Shipping: UCheatManager не должен создаваться (CheatManagerDefines.h)");
static_assert(!ALLOW_INI_OVERRIDE_FROM_COMMANDLINE, "Shipping: -ini:/-iniFile= должны быть вырезаны (ConfigCacheIni.h)");
static_assert(DISABLE_CHEAT_CVARS, "Shipping: cheat-cvar из ini/ConsoleVariables.ini должны быть запрещены (Build.h)");
static_assert(!ENABLE_PGO_PROFILE, "Shipping: -ExecCmds/-EXEC= вырезаны только без PGO-профиля (UnrealEngine.cpp)");
static_assert(!CSV_PROFILER, "Shipping: CSV-профайлер (-csvExecCmds) должен быть вырезан (CsvProfilerConfig.h)");
#endif

#if !UE_BUILD_SHIPPING
namespace AsdaRetailGuardTest { void Schedule(UGameEngine* Engine); }   // AsdaRetailGuardSelfTest.cpp
#endif

void UAsdaGameEngine::Init(IEngineLoop* InEngineLoop)
{
	Super::Init(InEngineLoop);
	// Shipping: ALLOW_CONSOLE=0 — консоль не создаётся вовсе; Development с -AsdaRetailGuard — снимаем созданную.
	if (AsdaRetailGuard::IsActive() && GameViewport && GameViewport->ViewportConsole)
	{
		GLog->RemoveOutputDevice(GameViewport->ViewportConsole);
		GameViewport->ViewportConsole = nullptr;
	}
#if !UE_BUILD_SHIPPING
	if (FParse::Param(FCommandLine::Get(), TEXT("AsdaRetailGuardTest"))) { AsdaRetailGuardTest::Schedule(this); }
#endif
}

#if UE_ALLOW_EXEC_COMMANDS
bool UAsdaGameEngine::Exec(UWorld* InWorld, const TCHAR* Cmd, FOutputDevice& Ar)
{
	if (AsdaSelfTestBatch::InterceptQuit(Cmd)) { return true; }   // пакет самотестов: `quit` = конец текущего самотеста
	// Ритейл-защита: в Shipping UE_ALLOW_EXEC_COMMANDS=1 (флаг Target требует уникальной среды сборки) — фильтр здесь.
	//   Команда «съедается» (true), чтобы её не обработал никто дальше по цепочке.
	if (AsdaRetailGuard::IsActive() && !AsdaRetailGuard::IsExecAllowed(Cmd))
	{
		AsdaRetailGuard::NoteExecBlocked(Cmd);
		return true;
	}
	return Super::Exec(InWorld, Cmd, Ar);
}
#endif
