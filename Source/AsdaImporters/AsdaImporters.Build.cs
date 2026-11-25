// Copyright (c) 2026 @sitxovski (Discord)
// AsdaImporters — editor-модуль: конвертеры проприетарных форматов → UE-ассеты.
// Реализация по docs/formats/: .TER→Landscape, .JEM→StaticMesh, .JBB→SkeletalMesh,
//   .TEX→Texture2D, .JEB→Niagara, .JUI→UMG, .ani→AnimSequence. (Слой 2)
// ★ Сюда же переехали DEV-АКТОРЫ, читающие ОРИГИНАЛЬНЫЕ файлы игры с диска:
//   AAsdaMapActor (террейн/вода/сцена/объекты из папки карты) и AAsdaSkinTuneActor (.asdaraw-тюнер).
//   Закон проекта: рантайм читает данные ТОЛЬКО из ассетов UE, чтение файлов допустимо лишь в
//   редакторных инструментах ⇒ в игровую сборку этот код больше не попадает вовсе.
using UnrealBuildTool;

public class AsdaImporters : ModuleRules
{
	public AsdaImporters(ReadOnlyTargetRules Target) : base(Target)
	{
		PCHUsage = PCHUsageMode.UseExplicitOrSharedPCHs;
		PublicDependencyModuleNames.AddRange(new string[]
		{
			"Core", "CoreUObject", "Engine", "AsdaCore"
		});
		PrivateDependencyModuleNames.AddRange(new string[]
		{
			"UnrealEd", "AssetTools", "AssetRegistry", "RenderCore", "RHI",
			// AAsdaMapActor: парсеры форматов карт (AsdaFormats — тоже editor-модуль),
			//   ProcMesh-геометрия и фасады ресурсов игрового модуля (текстуры/звук).
			"AsdaFormats", "ProceduralMeshComponent", "AsdaReborn",
			// UAsdaUmgBuilder: генератор JUI→UMG (UWidgetBlueprint/компиляция BP) — editor-only API.
			"UMG", "UMGEditor", "Kismet", "Slate", "SlateCore",
			// UAsdaMeshBuilder: FMeshDescription + FStaticMeshAttributes (авторские нормали объектов карт).
			"MeshDescription", "StaticMeshDescription",
			// UAsdaShadowPhysicsAssetLib: FPhysicsAssetUtils::CreateFromSkeletalMesh (физассет тени героя, сет C-2).
			"PhysicsUtilities",
			// UAsdaMaterialEditLib: UMaterialEditingLibrary::ConnectMaterialProperty(MP_ShadingModel) — вход скрыт от Python (сет D, листва).
			"MaterialEditor",
			// AsdaLoginSaveGuard (16.09.2026): отказ запуска WorldPartitionBuilderCommandlet, если его карта-аргумент — коллекция
			//   (TryParseCollectionPath/CollectionExists, как WorldPartitionBuilderCommandlet.cpp:92).
			"CollectionManager",
			// AsdaLoginPackageList (16.09.2026): разбор общего списка пакетов логина Tools/data/asda_login_packages.json
			//   (FJsonSerializer/FJsonObject) — тот же файл читает Tools/asda_login_guard.py.
			"Json"
		});
	}
}
