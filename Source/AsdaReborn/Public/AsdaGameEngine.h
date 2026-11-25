// Copyright (c) 2026 @sitxovski (Discord)
// ★ Подкласс игрового движка порта (11.10.2026, пакетный прогон самотестов). [ПОРТ]
//   Подключён в `Config/DefaultEngine.ini` ([/Script/Engine.Engine] GameEngine=/Script/AsdaReborn.AsdaGameEngine).
//   Единственное отличие от `UGameEngine`: во время пакетного прогона (`asda.SelfTest.Batch`) команда `quit`/`exit`
//   не закрывает игру, а отмечает конец текущего самотеста (`AsdaSelfTestBatch::InterceptQuit`). Вне пакета — как было.
//   Редактор (`UUnrealEdEngine`, PIE) этим классом не пользуется.
// ★ 16.10.2026 (ритейл-защита, agent-retail-branding; AsdaCore/Public/AsdaRetailGuard.h): при активной защите (Shipping —
//   всегда) `Exec` пропускает только quit/exit и не-читерские cvar, консоль вьюпорта снимается после `Init`.
#pragma once
#include "CoreMinimal.h"
#include "Engine/GameEngine.h"
#include "AsdaGameEngine.generated.h"

UCLASS()
class ASDAREBORN_API UAsdaGameEngine : public UGameEngine
{
	GENERATED_BODY()

public:
	// После `UGameEngine::Init`: при активной ритейл-защите снять консоль вьюпорта; Development — запуск самотеста защиты.
	virtual void Init(IEngineLoop* InEngineLoop) override;

#if UE_ALLOW_EXEC_COMMANDS
	// Перехват `quit` во время пакетного прогона самотестов; фильтр ритейл-защиты; остальные команды — `UGameEngine::Exec`.
	virtual bool Exec(UWorld* InWorld, const TCHAR* Cmd, FOutputDevice& Ar = *GLog) override;
#endif
};
