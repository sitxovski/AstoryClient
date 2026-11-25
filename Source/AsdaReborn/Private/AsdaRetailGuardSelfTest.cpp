// Copyright (c) 2026 @sitxovski (Discord)
// Самотест ритейл-защиты входов движка (AsdaCore/Public/AsdaRetailGuard.h) — ТОЛЬКО Development. [ПОРТ] 16.10.2026,
//   agent-retail-branding. Запускается ключом -AsdaRetailGuardTest из UAsdaGameEngine::Init (не командой консоли: при защите
//   -ExecCmds вычищается, пакетный прогон asda.SelfTest.Batch на нём и стоит). Два прогона делает Tools/asda_retail_guard_check.py:
//   «защита» (-AsdaRetailGuard) и «контроль» (без него) с одними и теми же входами: -ExecCmds с дев-пробой, -DP=, -UseFixedTimeStep,
//   -FPS=, пользовательский Saved/Config/<платформа>/Engine.ini с [SystemSettings] и [ConsoleVariables]. Самотест только
//   ПЕЧАТАЕТ наблюдения «RETAILGUARD-TEST ключ=значение», вердикт по паре прогонов выносит скрипт: в контроле вход обязан
//   сработать (иначе проверка ничего не доказывает), под защитой — нет.
#include "CoreMinimal.h"

#if !UE_BUILD_SHIPPING
#include "AsdaRetailGuard.h"
#include "Containers/Ticker.h"
#include "Engine/GameEngine.h"
#include "Engine/GameViewportClient.h"
#include "HAL/IConsoleManager.h"
#include "Misc/App.h"
#include "Misc/CommandLine.h"
#include "Misc/ConfigCacheIni.h"

DEFINE_LOG_CATEGORY_STATIC(LogAsdaRetailGuardTest, Log, All);

namespace AsdaRetailGuardTest
{
	// Пауза после Init до снятия наблюдений, с: мир загружен, отложенные команды (-ExecCmds) отработали.
	constexpr float kDelaySec = 12.f;

	// Строка наблюдения для скрипта.
	static void Fact(const TCHAR* Key, const FString& Value)
	{
		UE_LOG(LogAsdaRetailGuardTest, Display, TEXT("RETAILGUARD-TEST %s=%s"), Key, *Value);
	}

	// Целое значение cvar (или «нет», если ключа нет).
	static FString CVarInt(const TCHAR* Name)
	{
		IConsoleVariable* V = IConsoleManager::Get().FindConsoleVariable(Name, false);
		return V ? FString::FromInt(V->GetInt()) : FString(TEXT("нет"));
	}

	// Снять наблюдения и выйти.
	static void Run(TWeakObjectPtr<UGameEngine> WeakEngine)
	{
		UGameEngine* Engine = WeakEngine.Get();
		UWorld* World = Engine && Engine->GameViewport ? Engine->GameViewport->GetWorld() : nullptr;
		Fact(TEXT("active"), AsdaRetailGuard::IsActive() ? TEXT("1") : TEXT("0"));
		Fact(TEXT("probe_runs_execcmds"), FString::FromInt(AsdaRetailGuard::GetProbeRuns()));
		// Реальный путь Exec: GEngine->Exec → UAsdaGameEngine::Exec (фильтр) → UGameEngine::Exec.
		const int32 Before = AsdaRetailGuard::GetProbeRuns();
		const int32 BlockedBefore = AsdaRetailGuard::GetExecBlockedCount();
		if (Engine) { Engine->Exec(World, TEXT("asda.RetailGuard.Probe exec_path")); }
		Fact(TEXT("probe_runs_exec_path"), FString::FromInt(AsdaRetailGuard::GetProbeRuns() - Before));
		Fact(TEXT("exec_blocked_delta"), FString::FromInt(AsdaRetailGuard::GetExecBlockedCount() - BlockedBefore));
		// Решения фильтра на образцах (не зависят от режима — функция чистая).
		Fact(TEXT("allow_quit"), AsdaRetailGuard::IsExecAllowed(TEXT("quit")) ? TEXT("1") : TEXT("0"));
		Fact(TEXT("allow_cvar"), AsdaRetailGuard::IsExecAllowed(TEXT("r.OneFrameThreadLag 1")) ? TEXT("1") : TEXT("0"));
		Fact(TEXT("allow_open"), AsdaRetailGuard::IsExecAllowed(TEXT("open Login")) ? TEXT("1") : TEXT("0"));
		Fact(TEXT("allow_show"), AsdaRetailGuard::IsExecAllowed(TEXT("show fog")) ? TEXT("1") : TEXT("0"));
		Fact(TEXT("allow_pipe"), AsdaRetailGuard::IsExecAllowed(TEXT("r.OneFrameThreadLag 1|stat fps")) ? TEXT("1") : TEXT("0"));
		Fact(TEXT("allow_probe"), AsdaRetailGuard::IsExecAllowed(TEXT("asda.RetailGuard.Probe")) ? TEXT("1") : TEXT("0"));
		// Командная строка после очистки.
		const TCHAR* Cmd = FCommandLine::Get();
		Fact(TEXT("cmd_execcmds"), FCString::Stristr(Cmd, TEXT("ExecCmds=")) ? TEXT("1") : TEXT("0"));
		Fact(TEXT("cmd_dp"), FCString::Stristr(Cmd, TEXT("DP=")) ? TEXT("1") : TEXT("0"));
		Fact(TEXT("cmd_fixedstep"), FCString::Stristr(Cmd, TEXT("UseFixedTimeStep")) ? TEXT("1") : TEXT("0"));
		Fact(TEXT("cmd_fps"), FCString::Stristr(Cmd, TEXT("FPS=")) ? TEXT("1") : TEXT("0"));
		Fact(TEXT("cmd_keep_nonet"), FCString::Stristr(Cmd, TEXT("AsdaNoNet")) ? TEXT("1") : TEXT("0"));
		Fact(TEXT("fixed_timestep"), FApp::UseFixedTimeStep() ? TEXT("1") : TEXT("0"));
		// Пользовательский Engine.ini: значения cvar и секции в GConfig.
		Fact(TEXT("cvar_systemsettings"), CVarInt(TEXT("r.OneFrameThreadLag")));
		Fact(TEXT("cvar_consolevariables"), CVarInt(TEXT("r.SceneRenderTargetResizeMethod")));
		FString V;
		Fact(TEXT("ini_systemsettings_key"), GConfig && GConfig->GetString(TEXT("SystemSettings"), TEXT("r.OneFrameThreadLag"), V, GEngineIni) ? V : FString(TEXT("нет")));
		Fact(TEXT("ini_marker_key"), GConfig && GConfig->GetString(TEXT("AsdaRetailGuardTest"), TEXT("Marker"), V, GEngineIni) ? V : FString(TEXT("нет")));
		Fact(TEXT("viewport_console"), Engine && Engine->GameViewport && Engine->GameViewport->ViewportConsole ? TEXT("1") : TEXT("0"));
		for (const FString& L : AsdaRetailGuard::GetStartupLog()) { Fact(TEXT("startup"), L); }
		UE_LOG(LogAsdaRetailGuardTest, Display, TEXT("RETAILGUARD-TEST ИТОГ: наблюдения сняты"));
		FPlatformMisc::RequestExit(false, TEXT("AsdaRetailGuardTest"));
	}

	void Schedule(UGameEngine* Engine)
	{
		TWeakObjectPtr<UGameEngine> Weak(Engine);
		FTSTicker::GetCoreTicker().AddTicker(FTickerDelegate::CreateLambda([Weak](float) { Run(Weak); return false; }), kDelaySec);
	}
}
#endif
