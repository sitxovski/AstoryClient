// Copyright (c) 2026 @sitxovski (Discord)
// AsdaNet — транспорт (FSocket) + connection-FSM поверх AsdaCore.
using UnrealBuildTool;

public class AsdaNet : ModuleRules
{
	public AsdaNet(ReadOnlyTargetRules Target) : base(Target)
	{
		PCHUsage = PCHUsageMode.UseExplicitOrSharedPCHs;
		PublicDependencyModuleNames.AddRange(new string[] { "Core", "AsdaCore" });
		PrivateDependencyModuleNames.AddRange(new string[] { "Sockets", "Networking" });
		// ★ В7c: Engine нужен ТОЛЬКО самотесту Private/Secure/AsdaTrafficSecureSelfTest.cpp — `GEngine->Exec(quit)`,
		//   чтобы `quit` шёл через UAsdaGameEngine::Exec и перехватывался пакетом asda.SelfTest.Batch (как у всех самотестов).
		PrivateDependencyModuleNames.Add("Engine");

		// ★ В7b: криптопрослойка трафика (Private/Secure/*, AsdaSecure на Windows CNG). По умолчанию
		//   выключена в рантайме (cvar asda.Net.Secure=0). bcrypt и исключения нужны ТОЛЬКО на Windows.
		//   AsdaSecure использует std::vector (может бросить bad_alloc) — включаем C++ исключения.
		bEnableExceptions = true;
		if (Target.Platform == UnrealTargetPlatform.Win64)
		{
			PublicSystemLibraries.Add("bcrypt.lib");
			// ★ 16.10.2026: пакет падения ритейл-клиента (Private/LogSeal/AsdaLogSealRetail.cpp) — MiniDumpWriteDump в память.
			PublicSystemLibraries.Add("dbghelp.lib");
		}
		// ★ 19.10.2026 (журналы релиза, часть A): в Shipping UE_LOG/UE_CLOG/UE_LOG_REF наших модулей пишут в запечатанный журнал
		//   сессии — движок установлен с NO_LOGGING, поэтому макросы переопределяет принудительно подключаемый заголовок
		//   (разбор — Source/AsdaCore/Public/AsdaShippingLog.h). Вне Shipping ничего не меняется.
		if (Target.Configuration == UnrealTargetConfiguration.Shipping)
		{
			ForceIncludeFiles.Add(System.IO.Path.GetFullPath(System.IO.Path.Combine(ModuleDirectory, "..", "AsdaCore", "Public", "AsdaShippingLog.h")));
		}
	}
}
