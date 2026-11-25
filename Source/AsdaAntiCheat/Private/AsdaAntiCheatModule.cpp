// Copyright (c) 2026 @sitxovski (Discord)
// AsdaAntiCheatModule.cpp — модуль AsdaAntiCheat: категория журнала, EOS_Shutdown и сброс AsdaEac.log при выгрузке.
#include "Modules/ModuleManager.h"
#include "AsdaAntiCheatLog.h"
#include "AsdaEosSdk.h"
#include "AsdaEacDiagLog.h"
#include "Misc/CommandLine.h"

DEFINE_LOG_CATEGORY(LogAsdaAntiCheat);

// Модуль клиентского античита. Платформу EOS создаёт/освобождает UAsdaAntiCheatSubsystem; здесь — только завершение SDK.
class FAsdaAntiCheatModule : public IModuleInterface
{
public:
	// ★ Живая проверка 16.10: метка старта модуля прямым Win32 (AsdaEac_raw.log) — доказывает, что код модуля выполняется
	//   и под Astory.exe (бутстрапер EAC), независимо от файлового слоя UE и AsdaEac.log.
	virtual void StartupModule() override
	{
		FAsdaEacDiag::RawMark(FString::Printf(TEXT("StartupModule AsdaAntiCheat, exe %s, cmd «%s»"), FPlatformProcess::ExecutablePath(), FCommandLine::Get()));
	}

	// EOS_Shutdown — один раз на процесс, после того как все платформы освобождены подсистемами.
	virtual void ShutdownModule() override
	{
		FAsdaEosSdk::Get().Shutdown();
		FAsdaEacDiag::Shutdown();   // ★ живая проверка 16.10: дописать очередь AsdaEac.log и остановить нить записи
	}
};

IMPLEMENT_MODULE(FAsdaAntiCheatModule, AsdaAntiCheat);
