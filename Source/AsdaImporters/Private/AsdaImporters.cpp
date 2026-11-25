// Copyright (c) 2026 @sitxovski (Discord)
#include "Modules/ModuleManager.h"
#include "Editor/AsdaLoginSaveGuard.h"

// Слой-2 заглушка модуля импортёров. Конкретные фабрики (UFactory) добавляются по docs/formats/:
//   FTerImporter (.TER→Landscape), FJemImporter (.JEM→StaticMesh), FJbbImporter (.JBB→SkeletalMesh),
//   FTexImporter (.TEX magic 0x20040603→Texture2D), FJebImporter (.JEB→Niagara), FJuiImporter (.JUI→UMG).
//
// ★ 15.09.2026: модуль ставит движковую защиту сцены логина (AsdaLoginSaveGuard) — отказ сохранять
//   /Game/Map/Login в автоматической сессии. Модуль editor-only (фаза PostEngineInit) — в игру не попадает.
class FAsdaImportersModule : public IModuleInterface
{
public:
	// Запуск модуля: обернуть делегат сохранения пакетов.
	virtual void StartupModule() override
	{
#if WITH_EDITOR
		AsdaLoginSaveGuard::Install();
#endif
	}

	// Выгрузка модуля. ★ 16.09.2026: при выходе процесса защита НЕ снимается (поздние записи других модулей и
	//   финализаторов остаются под сторожем); снимается только при выгрузке модуля во время работы — см. AsdaLoginSaveGuard::Shutdown.
	virtual void ShutdownModule() override
	{
#if WITH_EDITOR
		AsdaLoginSaveGuard::Shutdown();
#endif
	}
};

IMPLEMENT_MODULE(FAsdaImportersModule, AsdaImporters);
