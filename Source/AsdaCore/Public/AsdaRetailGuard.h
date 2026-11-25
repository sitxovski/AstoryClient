// Copyright (c) 2026 @sitxovski (Discord)
// ★★★ РИТЕЙЛ-ЗАЩИТА ВХОДОВ ДВИЖКА: командная строка, пользовательские ini, Exec (решение владельца 16.10.2026,
//   agent-retail-branding; отчёт `Reports/anticheat/eac/BRANDING_CONSOLE_REPORT.md`). [ПОРТ]
//
// ЗАЧЕМ. Движок установленный (D:/Games/UE_5.8): правила Target с уникальной средой сборки (bUseExecCommandsInShipping=false,
//   bWithFixedTimeStepSupport=false, GlobalDefinitions с allow-list командной строки) UBT отвергает. Что движок уже закрывает в
//   Shipping сам (ALLOW_CONSOLE=0, UE_WITH_CHEAT_MANAGER=0, -ExecCmds/-EXEC= и -cvarsini/-dpcvars вырезаны препроцессором, -ini:
//   и -iniFile= — ALLOW_INI_OVERRIDE_FROM_COMMANDLINE=0, cheat-cvar из ini — DISABLE_CHEAT_CVARS), закреплено static_assert в
//   AsdaGameEngine.cpp. Открытым остаётся (проверено по исходникам 5.8):
//   1. Ключи командной строки, которые Shipping РАЗБИРАЕТ: -IniBootstrap=<файл> (вся конфигурация из чужого файла),
//      -textconfig, -BinaryConfigScalabilityOverride=, -ScalabilityIniPlatformOverride=, -DeviceProfile= / -DP=,
//      -UseFixedTimeStep / -Deterministic / -FPS= (фиксированный шаг времени = ускорение игры), -ExecCmds= (IoDispatcher).
//      FParse::Value ищет ПОДСТРОКУ (без границы слова: «-xDP=…» тоже сработает) — правила ниже это учитывают.
//   2. Пользовательский слой ini: %LOCALAPPDATA%\AsdaReborn\Saved\Config\Windows\<Engine|Scalability|DeviceProfiles|…>.ini
//      читается и в cooked-сборке (bAllowGeneratedIniWhenCooked для GConfig) — [SystemSettings]/[ConsoleVariables] ставят любой
//      НЕ-читерский cvar (туман, отсечка, тени…), [/Script/Engine.RendererSettings] и т. п. — настройки проекта.
//   3. UE_ALLOW_EXEC_COMMANDS=1 в Shipping: GEngine->Exec принимает любые команды движка (open/show/stat/…).
//
// ЧТО ДЕЛАЕТ (активна: Shipping — всегда; Development — только с ключом -AsdaRetailGuard, для самотеста):
//   • до WinMain (Shipping, монолитный exe) — буфер GetCommandLineW() пересобирается без запрещённых аргументов (разбор —
//     тем же CommandLineToArgvW, что у движка в LaunchWindowsStartup); -stdout НЕ трогаем — его вычищает LogSeal
//     (AsdaNet/Private/LogSeal/AsdaLogSealRetail.cpp);
//   • StartupModule AsdaCore (фаза EarliestPossible — сразу после инициализации GConfig, ДО применения cvar из ini):
//     FCommandLine очищается тем же фильтром; со всех ветвей GConfig, кроме GameUserSettings, снимается сохранённый слой
//     (секции из Saved/Config возвращаются к значениям из pak); FApp::SetUseFixedTimeStep(false);
//   • UAsdaGameEngine::Exec — пропускает только quit/exit и чтение/запись существующих НЕ-читерских cvar.
//   Проверка: Shipping — ключ -AsdaRetailGuardSelfCheck=<файл> (только запись итога очистки и немедленный выход до движка),
//   Development — самотест -AsdaRetailGuardTest (Tools/asda_retail_guard_check.py).
#pragma once

#include "CoreMinimal.h"

// Метка версии защиты в exe (UTF-16) — по ней упаковка (Tools/eac_package.py) проверяет, что защита вкомпилирована.
#define ASDA_RETAIL_GUARD_TAG TEXT("AsdaRetailGuard/v1")

namespace AsdaRetailGuard
{
	// Защита активна: Shipping — всегда; иначе — только с ключом -AsdaRetailGuard (Development-самотест).
	ASDACORE_API bool IsActive();

	// Аргумент (argv-токен без внешних кавычек) запрещён? OutRule — имя сработавшего правила (для журнала/самотеста).
	ASDACORE_API bool IsBannedArg(const TCHAR* Arg, const TCHAR** OutRule = nullptr);

	// Командная строка формата FCommandLine (без пути exe) → без запрещённых аргументов; OutRemoved — имена правил.
	ASDACORE_API FString FilterCommandLine(const TCHAR* CmdLine, TArray<FString>* OutRemoved = nullptr);

	// Снять сохранённый слой пользовательских ini со всех ветвей GConfig, кроме GameUserSettings.
	//   Возврат — число снятых секций; OutLog — «<ветвь> [<секция>]» на каждую.
	ASDACORE_API int32 StripUserSavedConfig(TArray<FString>* OutLog = nullptr);

	// Команда Exec разрешена в режиме защиты? (quit/exit, существующий не-читерский cvar; части через «|» — каждая.)
	ASDACORE_API bool IsExecAllowed(const TCHAR* Cmd, FString* OutWhy = nullptr);

	// Учёт отказа Exec (счётчик для самотеста и журнала Development).
	ASDACORE_API void NoteExecBlocked(const TCHAR* Cmd);

	// Сколько команд Exec отклонено с начала процесса.
	ASDACORE_API int32 GetExecBlockedCount();

	// Что сделано на старте (очистка командной строки, снятые секции ini) — строки для журнала/самотеста.
	ASDACORE_API const TArray<FString>& GetStartupLog();

	// Сколько раз выполнилась дев-команда-проба asda.RetailGuard.Probe (самотест: при защите должно быть 0).
	ASDACORE_API int32 GetProbeRuns();

	// Вызывается из StartupModule AsdaCore: очистка FCommandLine, снятие пользовательских ini, сброс фикс. шага.
	void OnModuleStartup();
}
