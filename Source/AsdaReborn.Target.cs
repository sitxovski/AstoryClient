// Copyright (c) 2026 @sitxovski (Discord)
// AsdaReborn — game target.
using UnrealBuildTool;
using System.Collections.Generic;

public class AsdaRebornTarget : TargetRules
{
	public AsdaRebornTarget(TargetInfo Target) : base(Target)
	{
		Type = TargetType.Game;
		DefaultBuildSettings = BuildSettingsVersion.V7;
		IncludeOrderVersion = EngineIncludeOrderVersion.Latest;
		// ★ AsdaFormats здесь НЕТ: модуль editor-only (парсеры оригинальных .TER/.WAT/.SCE/Pos.POS
		//   через std::ifstream) — в игровую сборку он не входит, рантайм читает только ассеты UE.
		ExtraModuleNames.AddRange(new string[] { "AsdaCore", "AsdaNet", "AsdaReborn" });

		// ★ Волна В2 античита (12.10.2026): раздача — Shipping. Явно закрепляем умолчания движка, чтобы они не
		//   «уехали» с версией BuildSettings: без логов движка (03_client.md §1.2; с 19.10 журнал наших модулей
		//   пишется, но только запечатанным — см. ниже) и без check()/ensure(). Development не трогаем: там остаются консоль, cvar и полные логи самотестов.
		// ★ 19.10.2026 (журналы релиза, часть A): Shipping ПИШЕТ журнал наших модулей — запечатанным, файл на сессию
		//   (Saved/Logs/Sessions/*.alog, AsdaLogSeal). bUseLoggingInShipping при этом остаётся false СОЗНАТЕЛЬНО: свойство требует
		//   уникального окружения сборки, а его UBT на установленном движке запрещает; движковый Core собран с NO_LOGGING.
		//   UE_LOG наших модулей в Shipping переопределён (Source/AsdaCore/Public/AsdaShippingLog.h, ForceIncludeFiles в Build.cs).
		if (Target.Configuration == UnrealTargetConfiguration.Shipping)
		{
			bUseLoggingInShipping = false;
			bUseChecksInShipping = false;
		}
	}
}
