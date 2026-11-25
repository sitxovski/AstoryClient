// Copyright (c) 2026 @sitxovski (Discord)
#include "Modules/ModuleManager.h"
#include "AsdaRetailGuard.h"

// AsdaCore — движко-агностичное ядро.
// ★ 16.10.2026 (agent-retail-branding): свой класс модуля ради ритейл-защиты входов движка (AsdaRetailGuard.h). Фаза загрузки
//   модуля в AsdaReborn.uproject — EarliestPossible: StartupModule идёт сразу после инициализации GConfig (AppInit), ДО того как
//   движок применит [SystemSettings]/[ConsoleVariables] и профиль устройства, — иначе снятие пользовательских ini опоздало бы.
//   Модуль зависит только от Core, поэтому ранняя загрузка безопасна.
class FAsdaCoreModule : public IModuleInterface
{
public:
	// Ритейл-защита: очистка командной строки, снятие пользовательских ini, сброс фиксированного шага (только когда активна).
	virtual void StartupModule() override { AsdaRetailGuard::OnModuleStartup(); }
};

IMPLEMENT_MODULE(FAsdaCoreModule, AsdaCore);
