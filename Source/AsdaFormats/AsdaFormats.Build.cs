// Copyright (c) 2026 @sitxovski (Discord)
// AsdaFormats — порт парсеров форматов карт из Tools/MapViewer (asda_formats.cpp).
// ★ EDITOR-ONLY (Type="Editor" в .uproject): парсеры читают ОРИГИНАЛЬНЫЕ файлы игры (std::ifstream по
//   .TER/.BIN/.WAT/.SCE/Pos.POS). По закону проекта рантайм данные берёт ТОЛЬКО из ассетов UE, поэтому
//   модуль в игровую сборку не входит. Единственный потребитель — AAsdaMapActor (тоже в AsdaImporters).
// Чистый C++ (plain-структуры, без raylib/без UObject) → собирается и тестируется вне UE.
// Геометрия/террейн/модели/сцена/вода/объекты. Эффекты (JEB/JEM) НЕ портируются (пока).
using UnrealBuildTool;

public class AsdaFormats : ModuleRules
{
	public AsdaFormats(ReadOnlyTargetRules Target) : base(Target)
	{
		PCHUsage = PCHUsageMode.UseExplicitOrSharedPCHs;
		PublicDependencyModuleNames.AddRange(new string[] { "Core" });
	}
}
