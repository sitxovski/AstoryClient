// Copyright (c) 2026 @sitxovski (Discord)
// ★ RAII «МИР СВОЕГО ПЕРСОНАЖА» ДЛЯ САМОТЕСТОВ (31.10.2026, починка пакетного прогона). [ПОРТ]
//
//   ЗАЧЕМ. Самотесты перехода (`asda.SelfTest.PortalPending`, `asda.SelfTest.ChannelMove`) зовут настоящие сбросы сессии
//   (`EnteringWorld`/`LeftWorld`/`MapChanged` — в игре они идут вместе с загрузкой уровня) и подают 6106 из живого журнала
//   (питомец «Dogi» призван). Уровень при этом НЕ перегружается (прогон его отменяет), поэтому после них:
//     • `UAsdaWorldSubsystem::LocalPlayerActor` обнулён, а сам павн жив и одержим контроллером — следующий самотест, который
//       ищет своего персонажа через мир (`asda.SelfTest.EscChain`), видел «персонажа нет» (12/39 в длинном пакете 31.10);
//     • питомец остаётся призванным: плашка 43200 `Pet_Status_Win` каждые 2 с всплывает поверх окон и съедала клик «ОК»
//       окна награды 540 (`asda.SelfTest.QuestDialogReward` 290/4, «5041 не ушёл»), актор питомца ходит за персонажем.
//   То же у `asda.SelfTest.BuffEnterWorld` (1021 = сброс «вход в мир», затем `Asda.World.FakeSelfSpawn` нового павна).
//   `Capture` в начале запоминает павн и «питомцев не было»; `Restore` (и деструктор), если ссылку обнулили, возвращает её на павна
//   контроллера (прогон мог заспавнить нового; тогда осиротевший прежний убирается) или на прежний павн; павн игрока, появившийся
//   за прогон и не ставший ни своим, ни зарегистрированным соседом (`FakeSelfSpawn`, а контроллер остался на прежнем), убирается; и — если до прогона питомцев не было — снимает список питомцев тем же сбросом, что и выход в выбор персонажа
//   (`UAsdaPetSubsystem` + плашка `UAsdaPetCageSubsystem`), и убирает актор питомца своего павна. Строка в лог — только если
//   что-то вернулось. Некопируемый; мир держится слабой ссылкой.
#pragma once
#include "CoreMinimal.h"
#include "UObject/WeakObjectPtrTemplates.h"

#if !UE_BUILD_SHIPPING

class AActor;
class UWorld;

struct ASDAREBORN_API FAsdaWorldSelfScope
{
	FAsdaWorldSelfScope() = default;
	// Сразу запомнить мир `W` (имя `Who` — для строки лога).
	FAsdaWorldSelfScope(UWorld* W, const TCHAR* InWho) { Capture(W, InWho); }
	~FAsdaWorldSelfScope() { Restore(); }
	FAsdaWorldSelfScope(const FAsdaWorldSelfScope&) = delete;
	FAsdaWorldSelfScope& operator=(const FAsdaWorldSelfScope&) = delete;

	// Запомнить (повторный вызов при взятом снимке ничего не делает — первое состояние главное).
	void Capture(UWorld* W, const TCHAR* InWho);
	// Вернуть запомненное и снять снимок (повторный вызов ничего не делает).
	void Restore();

private:
	TWeakObjectPtr<UWorld> WWorld;      // мир прогона (слабо: мог уйти)
	FString Who;                        // имя самотеста
	bool bArmed = false;                // снимок взят
	TWeakObjectPtr<AActor> SelfActor;   // свой павн до прогона (nullptr — его не было)
	bool bPetsEmpty = false;            // до прогона список питомцев пуст и никто не призван
	TArray<TWeakObjectPtr<AActor>> PawnsBefore;   // все павны игроков (свой и соседи) до прогона — новых сирот после прогона убираем
};

#endif // !UE_BUILD_SHIPPING
