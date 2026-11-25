// Copyright (c) 2026 @sitxovski (Discord)
// AsdaAntiCheat — клиентский модуль Easy Anti-Cheat (EOS C SDK 1.19.2.1, режим ClientServer), Э1-Г 16.10.2026.
//   Контракт компонентов: Reports/anticheat/eac/CONTRACT.md; план: Reports/anticheat/EAC_PLAN.md §3.1 К4;
//   отчёт: Reports/anticheat/eac/CLIENT_REPORT.md.
//   Состав: UAsdaAntiCheatSubsystem (поток входа EOS Connect → BeginSession → канал 15901 к сайдкару),
//   UAsdaAntiCheatSettings (DefaultGame.ini), кодек кадров §3, машина состояний, самотест asda.SelfTest.Eac.
using UnrealBuildTool;

public class AsdaAntiCheat : ModuleRules
{
	public AsdaAntiCheat(ReadOnlyTargetRules Target) : base(Target)
	{
		PCHUsage = PCHUsageMode.UseExplicitOrSharedPCHs;
		PublicDependencyModuleNames.AddRange(new string[] { "Core", "CoreUObject", "Engine", "DeveloperSettings" });
		PrivateDependencyModuleNames.AddRange(new string[]
		{
			"AsdaCore",     // ASDA_DEV_CVAR / ASDA_DEV_CONSOLE (AsdaDevConsole.h) — дев-ключи не регистрируются в Shipping
			"AsdaNet",      // FAsdaConnection + AsdaSecure (SubServer::Eac = 4) — канал 15901 к сайдкару
			"HTTP",         // POST AuthBaseUrl/token и /refresh (HTTPS, AsdaId)
			"Json",         // тела запросов/ответов AsdaId
			"AsdaEOSSDK"    // свой ThirdParty-модуль EOS SDK 1.19.2.1 (НЕ движковый EOSSDK)
		});
		// ★ 19.10.2026 (журналы релиза, часть A): в Shipping UE_LOG/UE_CLOG/UE_LOG_REF наших модулей пишут в запечатанный журнал
		//   сессии — движок установлен с NO_LOGGING, поэтому макросы переопределяет принудительно подключаемый заголовок
		//   (разбор — Source/AsdaCore/Public/AsdaShippingLog.h). Вне Shipping ничего не меняется.
		if (Target.Configuration == UnrealTargetConfiguration.Shipping)
		{
			ForceIncludeFiles.Add(System.IO.Path.GetFullPath(System.IO.Path.Combine(ModuleDirectory, "..", "AsdaCore", "Public", "AsdaShippingLog.h")));
		}
	}
}
