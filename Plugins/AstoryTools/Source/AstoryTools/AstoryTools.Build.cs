// Copyright (c) 2026 @sitxovski (Discord)
// AstoryTools — РЕДАКТОРСКИЙ модуль (в упакованный билд не попадает): общий раздел меню
// Tools ▸ Astory_Tools + редакторы игровых таблиц.
//
// ★ Зависимости от игрового модуля AsdaReborn НЕТ намеренно: редакторы работают с DataTable
//   через рефлексию (`UDataTable::CreateTableFromCSVString`) и с исходными CSV/BIN напрямую.
//   Так инструмент не ломается от правок игровых заголовков (их сейчас правят другие агенты)
//   и не тянет игровой рантайм в редакторную сборку.
using UnrealBuildTool;

public class AstoryTools : ModuleRules
{
	public AstoryTools(ReadOnlyTargetRules Target) : base(Target)
	{
		PCHUsage = PCHUsageMode.UseExplicitOrSharedPCHs;

		PublicDependencyModuleNames.AddRange(new string[]
		{
			"Core", "CoreUObject", "Engine",
			// ★ Волна 3 п.15: публичный заголовок общей базы превью (AstoryPreviewViewport.h) наследует
			//   SEditorViewport/FEditorViewportClient — зависимым модулям нужны заголовки UnrealEd
			"UnrealEd"               // GEditor, UEditorLoadingAndSavingUtils, FEditorFileUtils, SEditorViewport
		});

		PrivateDependencyModuleNames.AddRange(new string[]
		{
			"Slate", "SlateCore", "InputCore",
			"EditorFramework",       // FEditorModeTools (конструктор вьюпорт-клиента общей базы превью)
			"AdvancedPreviewScene",  // FAdvancedPreviewScene — сцена общей базы превью со светом/полом
			"ToolMenus",             // раздел меню Tools ▸ Astory_Tools
			"WorkspaceMenuStructure",// категория вкладок в Window ▸ Tools
			"Projects",              // IPluginManager
			"DesktopPlatform",       // диалоги «сохранить как» для экспорта выборки
			"ApplicationCore"        // буфер обмена (копирование сводки)
		});
	}
}
