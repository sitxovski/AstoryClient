// Copyright (c) 2026 @sitxovski (Discord)
// AsdaCapsuleShadow — ЕДИНАЯ точка «капсульная тень тела» для живых сущностей мира (игрок, моб, NPC).
//
// ЗАЧЕМ ОТДЕЛЬНЫМ ЗАГОЛОВКОМ. Как и у `AsdaPickHitbox.h`: игрок/моб/NPC не наследуются друг от друга, общего
//   предка нет, а политика одна и капризная (закон `port-shared-points`):
//     · включаем ТОЛЬКО непрямую капсульную тень (`bCastCapsuleIndirectShadow`) — пятно под телом в непрямом
//       слое (Sky), которое видно и в тени дерева; формально пишется множителем в SceneColor ДО RenderLights
//       (CapsuleShadowRendering.cpp:1171, DeferredShadingRenderer.cpp:3441) и не зависит от VSM/CSM.
//       ★ Ревизия 05.09 (review_setB_prep.md ВАЖНО-5/М-9): у нас запечённого непрямого света нет, поэтому в картинку
//       капсула попадает ЧЕРЕЗ `SceneTextures.ScreenSpaceAO` (CapsuleShadowRendering.cpp:1010-1012 — load + умножение),
//       который читает RenderDeferredReflectionsAndSkyLighting (IndirectLightRendering.cpp:1942). Требования: на уровне есть
//       Movable SkyLight (ветка CapsuleShadowRendering.cpp:811-819, направление из IrradianceEnvironmentMap) и включён
//       AO-проход (r.AmbientOcclusion.Method GTAO/SSAO, r.AmbientOcclusionLevels>0); без AO эффекта не будет.
//       В A/B-кадрах смотреть ShowFlag.VisualizeSSAO / `vis ScreenSpaceAO`; r.AmbientOcclusionStaticFraction ни при чём;
//     · прямую (`bCastCapsuleDirectShadow`) НЕ включаем: под VSM она мертва (ShadowSetup.cpp:3346
//       `check(!UseVirtualShadowMaps)` — прямые капсулы живут только в SetupInteractionShadows), а под CSM
//       дала бы ВТОРУЮ прямую тень поверх теневой карты тела (двойная тень);
//     · `CapsuleIndirectShadowMinVisibility` — из ключа `asda.Shadow.CapsuleMinVisibility` (0..1, умолч. 0.1 = дефолт движка;
//       при 0.1 пятно в тени кроны почти чёрное — A/B 0.5 в setB_apply_report.md §7.2; значение выбирает владелец).
//   Капсулы берутся ТОЛЬКО из `USkeletalMesh::ShadowPhysicsAsset` (SkeletalMeshSceneProxy.cpp:318-345), фолбэка
//   на обычный PhysicsAsset нет — его назначает `Tools/asda_set_shadow_physics_assets.py`; без него флаг безвреден.
//
// Ключи (все — единственные регистрации в Source/, grep 05.09.2026; закон `ue-console-duplicate-registration`):
//   · `asda.Shadow.Capsule` (0/1, умолч. 1) — A/B в `-game` без пересборки; смена перекладывает флаг на всех уже живых
//     телах (метка компонента `AsdaCapsuleShadow`);
//   · `asda.Shadow.CapsuleMinVisibility` (float 0..1, умолч. 0.1) — сила пятна, смена перекладывается тем же колбэком;
//   · ★ 26.10.2026 `asda.Shadow.CapsuleSelfFix` (дев, 0/1, умолч. 1; 0 = прежнее поведение) и `asda.Shadow.CapsuleSelfShadowing`
//     (дев, 0..1, умолч. 0) — самозатенение персонажа своими капсулами: AWorldSettings::DynamicIndirectShadowsSelfShadowingIntensity
//     каждого игрового мира + метка частей экипа приёмниками (ApplyCapsuleReceiver). Разбор — у объявления ключей в .cpp;
//   · `asda.Shadow.CapsuleDump` (команда) — дамп «изнутри»: меш/ShadowPhysicsAsset/тела PA→кость/капсулы, флаги компонента
//     и живого прокси, скрытые секции, bounds (доводка 05.09, review_setB_apply.md §4 шаг 2).
//
// Сет B «тени» 05.09.2026: план FINAL_synthesis.md §4, адреса developer_round2.md §1.3, skeptic_round2.md Г2/З10.
#pragma once

#include "CoreMinimal.h"

class USkeletalMeshComponent;

namespace AsdaShadow
{
	// Применить политику капсульной тени к КОМПОНЕНТУ ТЕЛА (BaseSkel игрока, MobMesh, NpcMesh — не к частям экипа
	//   и не к квестовой метке). Зовётся из конструктора актора сразу после `SetCastShadow(true)`; у моба и NPC — повторно из
	//   `OnConstruction` (значение ключа из ini может прийти позже CDO); у игрока (`AsdaPlayerCharacter.cpp:80`) — только из
	//   конструктора: игрок спавнится после загрузки ini, а смена ключа перекладывается колбэком на все живые тела
	//   (review_setB_fix.md МЕЛОЧЬ-3, доводка 05.09). Идемпотентна.
	ASDAREBORN_API void ApplyCapsule(USkeletalMeshComponent* Mesh);

	// ★ 26.10.2026 [ИСПРАВЛЕНИЕ] (баг игроков «Ultra: персонаж весь в чёрной тени», Reports/ultra_shadows/):
	//   часть экипа персонажа (оверлей Equip_* под LeaderPose тела) помечается ПРИЁМНИКОМ капсульной тени — флагом
	//   bCastCapsuleIndirectShadow без собственных капсул (у мешей частей нет ShadowPhysicsAsset, import_equip_parts.py),
	//   ради бита GBuffer HasDynamicIndirectShadowCasterRepresentation: только по нему движок ослабляет самозатенение
	//   (CapsuleShadowShaders.usf ApplySelfShadowingIntensityForDeferred). Без бита одежда оставалась чёрной даже при
	//   самозатенении 0. Меш части с собственным ShadowPhysicsAsset не помечается (иначе вторая тень от его капсул).
	//   Body — тело-ведущий (BaseSkel); метка действует, только если тело само отбрасывает капсульную тень. Идемпотентна.
	ASDAREBORN_API void ApplyCapsuleReceiver(USkeletalMeshComponent* Part, const USkeletalMeshComponent* Body);

	// Текущее значение ключа `asda.Shadow.Capsule` (для логов/диагностики).
	ASDAREBORN_API bool IsCapsuleEnabled();
}
