// Copyright (c) 2026 @sitxovski (Discord)
// ★★★ НАДПИСИ НАД ГОЛОВОЙ — ПРОЕКЦИЯ ПО КАМЕРЕ ЭТОГО ЖЕ КАДРА (30.10.2026, баг владельца «Entity names lag behind when moving
//   camera»: при повороте/наезде камеры таблички имён и HP «плывут», отставая от своей точки). [ОРИГИНАЛ]+[ИСПРАВЛЕНИЕ]
//
// ОРИГИНАЛ (game_main.c): кадр — `LocalPlayer_UpdateRender` (:1950, внутри `cameraUpdate@0x959880`, entity_char.c:7982 —
//   камера ЭТОГО кадра) → `gameUpdateTargetNameplates` (:2052) → `entityHud_UpdateOverheadPlates` + `npcNameplateUpdateLoop@0xF93000`
//   (:4527-4528, проекция точек над головой) → `CRenderer_RenderWorldFrame` (:2265). Плашка проецируется камерой, которой кадр
//   и рисуется — отставания нет.
//
// ЧТО БЫЛО В ПОРТЕ (порядок кадра движка, `LevelTick.cpp:1750-1906`): группы тика TG_PrePhysics…TG_PostPhysics → тикаемые
//   объекты мира (`FTickableGameObject::TickObjects(World)`) → ОБНОВЛЕНИЕ КАМЕРЫ (`PlayerController->UpdateCameraManager`) →
//   TG_PostUpdateWork → TG_LastDemotable → `FWorldDelegates::OnWorldPostActorTick`. `ULocalPlayer::GetProjectionData` берёт
//   КЭШ камеры, поэтому всё, что проецирует до обновления камеры, получает камеру ПРОШЛОГО кадра:
//   • плашки (`UAsdaNameplateComponent`, тик компонента TG_DuringPhysics) брали ещё и кадровую проекцию `UAsdaVisibilitySubsystem`,
//     собранную в его Tick ПРОШЛОГО кадра (тикаемый объект мира — после компонентов) ⇒ отставание на ДВА кадра;
//   • облачка чата (`UAsdaChatBalloonSubsystem` — тикаемая подсистема МИРА) ⇒ на один кадр;
//   • высота квестовой «!» над NPC (экранное правило `AAsdaNpcActor::ApplyQuestMarkHeight`, тик актора) ⇒ на один кадр.
//   Не отстают: вывески лавок, баннеры SoulGuard, квест-подсистема — тикаемые объекты GameInstance (мир не задан), в игре их
//   тикает `UGameEngine::Tick` (GameEngine.cpp:1973) ПОСЛЕ тика мира, т.е. уже по свежей камере; цифры урона проецируются
//   один раз при появлении и дальше стоят на экране (`CombatText`, как у оригинала).
//
// ИСПРАВЛЕНИЕ (ключ asda.Overhead.LateProject 1; 0 — прежний порядок):
//   • плашка тикает в TG_PostUpdateWork и проецирует кадровой проекцией `FreshProjection` — собранной ПОСЛЕ обновления камеры;
//   • облачка и высота квестовой «!» пересчитываются в `OnWorldPostActorTick` (`AddPostCamera`).
#pragma once

#include "CoreMinimal.h"
#include "World/AsdaVisibilitySubsystem.h"   // FAsdaFrameProjection — формат кадровой проекции (общий с подсистемой видимости)

class UWorld;

namespace AsdaOverheadLate
{
	// Рубильник asda.Overhead.LateProject.
	ASDAREBORN_API bool IsOn();

	// Кадровая проекция мир→экран по камере ЭТОГО кадра. Собирается один раз за кадр на мир; nullptr — камера в этом кадре ещё
	//   не обновлена (вызов до `UpdateCameraManager`) или данных вида нет — вызывающий падает на прежний путь.
	ASDAREBORN_API const FAsdaFrameProjection* FreshProjection(const UWorld* W);

	// Подписка «после обновления камеры» (`FWorldDelegates::OnWorldPostActorTick`): Fn(мир, шаг) — только для игровых миров
	//   и не на паузе. Снимать `RemovePostCamera` в Deinitialize/EndPlay подписчика.
	ASDAREBORN_API FDelegateHandle AddPostCamera(TFunction<void(UWorld*, float)> Fn);
	ASDAREBORN_API void RemovePostCamera(FDelegateHandle& H);
}
