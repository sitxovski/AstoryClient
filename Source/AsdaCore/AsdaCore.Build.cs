// Copyright (c) 2026 @sitxovski (Discord)
// AsdaCore — движко-агностичное ядро (1:1 протокол). Минимум зависимостей.
// Файлы протокола (ChannelCipher/PacketFrame/NetOpcodes) — ЧИСТЫЙ C++ без UE-типов,
// чтобы собираться и тестироваться вне UE (standalone harness против живого сервера).
using UnrealBuildTool;

public class AsdaCore : ModuleRules
{
	public AsdaCore(ReadOnlyTargetRules Target) : base(Target)
	{
		PCHUsage = PCHUsageMode.UseExplicitOrSharedPCHs;
		// Только Core: ядро не тянет Engine. Протокол-код не использует даже Core-типы.
		PublicDependencyModuleNames.AddRange(new string[] { "Core" });
		// ★ 16.10.2026 (ритейл-защита, AsdaRetailGuard.cpp): CommandLineToArgvW — тот же разбор командной строки, что у движка
		//   (LaunchWindowsStartup); в модульной сборке редактора shell32 надо подключить явно.
		if (Target.Platform == UnrealTargetPlatform.Win64)
		{
			PublicSystemLibraries.Add("shell32.lib");
		}
		// ★ 19.10.2026 (журналы релиза, часть A): в Shipping UE_LOG/UE_CLOG/UE_LOG_REF наших модулей пишут в запечатанный журнал
		//   сессии — движок установлен с NO_LOGGING, поэтому макросы переопределяет принудительно подключаемый заголовок
		//   (разбор — Source/AsdaCore/Public/AsdaShippingLog.h). Вне Shipping ничего не меняется.
		if (Target.Configuration == UnrealTargetConfiguration.Shipping)
		{
			ForceIncludeFiles.Add(System.IO.Path.GetFullPath(System.IO.Path.Combine(ModuleDirectory, "Public", "AsdaShippingLog.h")));
		}
	}
}
