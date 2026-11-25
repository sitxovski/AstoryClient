// Copyright (c) 2026 @sitxovski (Discord)
// ★★ «ЭТО СЦЕНА ЛОГИНА?» И «ЭТО КАРТА ЗАГРУЗКИ?» — ОДИН ПРИЗНАК НА ПОРТ (дедуп №2, В3, F7, 12.10.2026). [ПОРТ]
//
// ЧТО БЫЛО. Признак «сцена логина» жил в двух видах и девяти местах:
//   • по ИМЕНИ (`GetName()/GetMapName().Contains("Login")`): холдер цикла эффектов (`AsdaEffectActor.cpp`), сброс сессии
//     (`AsdaSessionReset.cpp`), отложенный спаун своего персонажа и батчи окружения (`AsdaWorldSubsystem.cpp` ×3);
//   • по РЕЖИМУ игры (`GetAuthGameMode()->IsA<AAsdaLoginGameMode>()`): свечение/альфа-тест (`UAsdaVideoSettingsSubsystem::
//     IsLoginSceneWorld`), окна (`AsdaWindowSubsystem.cpp` ×3).
//   Расходятся они только там, где у мира НЕТ режима игры — редактор вне PIE (холдеру эффектов нужно именно это: он видит
//   уровень и в редакторе), — и на карте, у которой имя и режим не согласованы (такой в проекте нет: «Login» в имени у
//   одной `/Game/Map/Login`, `AAsdaLoginGameMode` задан только ей). Сверка в игре — `asda.W3.LoginKind` (отчёт `wave3_S10.md`).
//
// ПРАВИЛО (одно). Есть режим игры (игра, PIE) — решает режим: сцена логина = `AAsdaLoginGameMode` (эталон: состояния 2/3/5/6
//   главного цикла — `Game_UpdateAndRenderFrame@0xF20B80`). Режима нет (редактор вне PIE) — решает имя карты («Login»).
//   В игре это ровно прежний признак «по режиму»; для мест «по имени» в игре признаки совпадают, в редакторе — прежнее имя.
#pragma once
#include "CoreMinimal.h"

class UWorld;

namespace AsdaWorldKind
{
	// Сцена логина / выбора / создания персонажа (см. правило в шапке). nullptr — нет.
	ASDAREBORN_API bool IsLoginScene(const UWorld* World);
	// Служебная карта экрана загрузки `L_Loading` (по имени мира — у неё своего режима нет). nullptr — нет.
	ASDAREBORN_API bool IsLoadingMap(const UWorld* World);
}
