// Copyright (c) 2026 @sitxovski (Discord)
#include "Modules/ModuleManager.h"
#include "AsdaTimedSend.h"
#include "AsdaLogSeal.h"

// AsdaNet — транспортный модуль.
// ★ 15.10.2026 (ревизия PORT-RHYTHM СРЕДН-2): при выгрузке модуля останавливается поток точной отправки
//   `FAsdaTimedSend` (до этого модуль был дефолтным `FDefaultModuleImpl`).
// ★ 16.10.2026 (решение владельца: журналы ритейл-клиента — только зашифрованными): модуль грузится в PreDefault — раньше
//   AsdaAntiCheat и игрового модуля — и ставит ритейл-политику журналов (AsdaLogSeal::InstallRetailPolicy: журнал движка,
//   обработчик падений, перенос старых открытых файлов). Вне Shipping без -AsdaLogSeal ничего не меняется.
class FAsdaNetModule : public FDefaultModuleImpl
{
public:
	virtual void StartupModule() override { AsdaLogSeal::InstallRetailPolicy(); }
	virtual void ShutdownModule() override
	{
		FAsdaTimedSend::Shutdown();
		AsdaLogSeal::ShutdownRetailPolicy();
	}
};

IMPLEMENT_MODULE(FAsdaNetModule, AsdaNet);
