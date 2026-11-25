// Copyright (c) 2026 @sitxovski (Discord)
// AsdaItemBrowser — РЕДАКТОРСКИЙ модуль (в игру не попадает): браузер предметов item-БД
// с деревом категорий, иконками из атласов JUI и 3D-превью модели + эффектов заточки.
// Зависит от игрового модуля AsdaReborn ТОЛЬКО на чтение (структуры записей item-БД,
// декодер cp1251, класс AAsdaEffectActor) — рантайм-код не изменяется.
using UnrealBuildTool;

public class AsdaItemBrowser : ModuleRules
{
	public AsdaItemBrowser(ReadOnlyTargetRules Target) : base(Target)
	{
		PCHUsage = PCHUsageMode.UseExplicitOrSharedPCHs;

		PublicDependencyModuleNames.AddRange(new string[]
		{
			"Core", "CoreUObject", "Engine"
		});

		PrivateDependencyModuleNames.AddRange(new string[]
		{
			"Slate", "SlateCore", "InputCore",
			"UnrealEd",                 // SEditorViewport / FEditorViewportClient / GEditor
			"EditorFramework",          // FEditorModeTools (вьюпорт-клиент)
			"AdvancedPreviewScene",     // FAdvancedPreviewScene — сцена превью со светом/полом
			"ToolMenus",                // пункт меню Tools
			"WorkspaceMenuStructure",   // категория вкладки в Window
			"Projects",                 // IPluginManager (пути плагина)
			"RenderCore", "RHI",
			"ProceduralMeshComponent",  // самопроверка геометрии эффекта (секции UProceduralMeshComponent)
			"AssetRegistry",            // список моделей проекта для «Студии нормалей»
			"AsdaReborn",               // FAsdaItemRecord/FAsdaImgRecord, FAsdaLoginData, AAsdaEffectActor
			// ★ 13.09.2026 «Конфигуратор свечения Asda»: запись таблицы свечения идёт через Tools/asda_fx_glow_config.py
			//   (единый источник — модуль генератора ini) во встроенном Python редактора; обмен с ним — JSON-файлами
			"PythonScriptPlugin", "Json",
			// ★★★ РЕВИЗИЯ Ф11 (С1, 20.09.2026): ШТАТНЫЙ РАЗБОР meta EditCondition.
			//   Конфигуратор прячет поля, не работающие при нынешних числах, и до этой правки считал
			//   выражение СВОЕЙ грамматикой — только потому, что модуль PropertyEditor не был подключён
			//   и штатного API никто не искал (закон check-existing-before-creating). Штатное:
			//   EditConditionEvaluator::IsPropertyEditable(FProperty*, UScriptStruct*, void*)
			//   — Engine/Source/Editor/PropertyEditor/Public/EditConditionEvaluator.h (UE 5.8).
			//   Своя грамматика снята: у движка && и || РАВНОГО приоритета и правоассоциативны
			//   (EditConditionParser.cpp:774-775, ExpressionParserTypes.h:582-588), у C-семантики — нет,
			//   и первое же новое условие без скобок разошлось бы молча.
			"PropertyEditor",
			// Вопрос о несохранённых правках конфигуратора при выходе из редактора (IMainFrameModule::RegisterCanCloseEditor)
			"MainFrame",
			// ★ Волна 3 п.15: общий редакторский каркас — меню FAstoryToolsMenu и база превью SAstoryPreviewViewport
			//   (каркас без игровых зависимостей; браузер зависит от него, а не наоборот)
			"AstoryTools",
			// ★ 29.10.2026 (переделка просмотрщика): самопроверка рисует панель в render target без окна
			//   (FWidgetRenderer — модуль UMG) и пишет кадр PNG (FImageView — ImageCore)
			"UMG", "ImageCore"
		});
	}
}
