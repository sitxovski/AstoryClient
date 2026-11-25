// Copyright (c) 2026 @sitxovski (Discord)
// AsdaMobBrowser — РЕДАКТОРСКИЙ модуль (Type=Editor: в упакованную игру и Shipping не попадает): обозреватель мобов
// с 3D-превью и характеристиками (просьба владельца 09.11.2026).
// Зависит от игрового модуля AsdaReborn ТОЛЬКО на чтение (таблицы мобов, правило «тип → модель», пути ассетов,
// языковые оверлеи) — рантайм-код и ассеты игры не изменяются.
using System.IO;
using UnrealBuildTool;

public class AsdaMobBrowser : ModuleRules
{
	public AsdaMobBrowser(ReadOnlyTargetRules Target) : base(Target)
	{
		PCHUsage = PCHUsageMode.UseExplicitOrSharedPCHs;

		PublicDependencyModuleNames.AddRange(new string[]
		{
			"Core", "CoreUObject", "Engine"
		});

		PrivateDependencyModuleNames.AddRange(new string[]
		{
			"Slate", "SlateCore", "InputCore",
			"UnrealEd",                 // SEditorViewport / FEditorViewportClient
			"EditorFramework",          // FEditorModeTools (вьюпорт-клиент)
			"AdvancedPreviewScene",     // FAdvancedPreviewScene — сцена превью со светом и полом
			"ToolMenus",                // пункт меню Tools ▸ Asda
			"WorkspaceMenuStructure",   // группа вкладок «Asda» в меню Window
			"AssetRegistry",            // список клипов anim_NN модели (без загрузки каждого пути наугад)
			"RenderCore", "RHI",        // FlushRenderingCommands для кадра самопроверки
			"UMG", "ImageCore",         // кадр самопроверки: FWidgetRenderer → PNG (окна нет)
			"AsdaReborn",               // DT_MobNames/DT_MobStats/DT_MobTextures, AsdaEntityText, пути ассетов, FAsdaLang
			"AstoryTools"               // общий каркас меню FAstoryToolsMenu и база превью SAstoryPreviewViewport
		});

		// ★ Таблица «тип моба → скелетный вариант клипов базы» — ТА ЖЕ, что у игры (`AsdaMobClipBase.inl`, автоген
		//   `Tools/gen_mob_skill_tables.py`). Подключаем файл игрового модуля, а не копию: при перегенерации таблицы
		//   обозреватель получает её сам (закон «одна точка», port-shared-points).
		PrivateIncludePaths.Add(Path.Combine(ModuleDirectory, "..", "..", "..", "..", "Source", "AsdaReborn", "Private", "World"));
	}
}
