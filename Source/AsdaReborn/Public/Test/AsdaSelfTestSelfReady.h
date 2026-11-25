// Copyright (c) 2026 @sitxovski (Discord)
// ★ 01.11.2026 (изоляция пакетного прогона самотестов) [ПОРТ]: «свой персонаж готов к прогону».
//
//   ЗАЧЕМ. Самотесты офлайн-старта заводили своего персонажа (`Asda.World.FakeSelfSpawn`) только при отсутствии павна. Но
//   предыдущий самотест пакета мог оставить павн живым и одержимым, а мир — без ссылки на него или номер своего — «неизвестным»:
//   настоящие сбросы сессии (`asda.Fish.AnimSelfTest` — «разрыв связи», после него в мире «свой персонаж «»»), возврат
//   `FAsdaNetSelfScope`. Тогда свой грант 4007 никого не двигал (`asda.Move.SelfTest4007` 86/51 в пакете), а `Me()` возвращал пусто
//   (`asda.UI.SelfTestDuelLoser` 0/1). Эта проверка — общая для стартов таких самотестов: павн есть, номер своего известен,
//   мир знает свой павн. Иначе вызывающий спавнит своего заново (сам спавн ставит номер известным и ссылку мира).
#pragma once

#include "CoreMinimal.h"

#if !UE_BUILD_SHIPPING

#include "AsdaNetSubsystem.h"
#include "World/AsdaWorldSubsystem.h"
#include "Engine/GameInstance.h"
#include "Engine/World.h"
#include "GameFramework/PlayerController.h"

namespace AsdaSelfTestSelfReady
{
	// Готов ли свой персонаж мира `W` к офлайн-прогону (павн, номер своего, ссылка мира на свой павн).
	inline bool IsReady(UWorld* W)
	{
		const APlayerController* P = W ? W->GetFirstPlayerController() : nullptr;   // свой контроллер
		const UGameInstance* GI = W ? W->GetGameInstance() : nullptr;               // экземпляр игры
		const UAsdaNetSubsystem* Net = GI ? GI->GetSubsystem<UAsdaNetSubsystem>() : nullptr;
		const UAsdaWorldSubsystem* WS = GI ? GI->GetSubsystem<UAsdaWorldSubsystem>() : nullptr;
		return P && P->GetPawn() && Net && Net->IsSelfUserNumKnown() && WS && IsValid(WS->LocalPlayerActor)
			&& WS->LocalPlayerActor.Get() == P->GetPawn();
	}
}

#endif // !UE_BUILD_SHIPPING
