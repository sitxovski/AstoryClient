// Copyright (c) 2026 @sitxovski (Discord)
// AsdaPickHitbox — ЕДИНАЯ точка «хитбокс пикинга» для всех кликабельных сущностей мира.
//
// ЗАЧЕМ ОТДЕЛЬНЫМ ЗАГОЛОВКОМ. Ни NPC, ни моб, ни лут, ни персонаж игрока не наследуются друг от
//   друга (все — прямые `AActor`/`APawn`), поэтому общего предка, куда положить это знание, нет.
//   А знание тут ровно одно и очень капризное:
//     · капсула ловит ТОЛЬКО трейсы видимости (`ECC_Visibility`) и ничего больше — иначе она
//       начинает участвовать в физике и в трейсе высоты земли (грабля, описанная в
//       `Public/World/AsdaGroundTrace.h`: чужие хитбоксы съедали итерации трейса);
//     · оверлап-события выключены — их некому слушать, а стоят они каждый кадр.
//   Раньше эти семь строк стояли ЧЕТЫРЬМЯ копиями (NPC, моб, лут, игрок), и любая правка политики
//   коллизий требовала вспомнить все четыре. Теперь точка ОДНА (закон `port-shared-points`).
//
// ★ АУДИТ ДУБЛЕЙ Д9: `Reports/quest_npc_dedup_audit.md`. На эту точку переведены NPC и моб
//   (зона аудита); лут и персонаж игрока — в списке «за пределами зоны», ждут отдельного слова.
#pragma once

#include "CoreMinimal.h"
#include "Components/CapsuleComponent.h"
#include "Components/SkeletalMeshComponent.h"

namespace AsdaPick
{
	// Политика коллизий капсулы пикинга. Зовётся ОДИН раз, в конструкторе актора, сразу после
	//   `CreateDefaultSubobject` и `SetupAttachment`. Стартовый размер капсулы остаётся за
	//   вызывающим: он у каждой сущности свой (NPC 45/90, моб 45/80, лут 30/30, игрок 35/80).
	inline void Configure(UCapsuleComponent* Capsule)
	{
		if (!Capsule) { return; }
		Capsule->SetCollisionEnabled(ECollisionEnabled::QueryOnly);
		Capsule->SetCollisionObjectType(ECC_WorldDynamic);
		Capsule->SetCollisionResponseToAllChannels(ECR_Ignore);
		Capsule->SetCollisionResponseToChannel(ECC_Visibility, ECR_Block);
		Capsule->SetGenerateOverlapEvents(false);
	}

	// Подгонка капсулы под ФАКТИЧЕСКИЕ габариты скелетного меша. Зовётся из `OnConstruction`:
	//   меш к этому моменту уже назначен, а масштаб актора известен.
	// ★ ПОЧЕМУ ГАБАРИТЫ МЕША, А НЕ АКТОРА: `GetActorBounds` включает саму капсулу, и она начинает
	//   мерить себя — с каждой перестройкой конструкции размер уползал бы.
	// ★ ПОЧЕМУ ДЕЛИМ НА `ScaleZ`: модели импортированы ×100 и стоят в масштабе 0.01, а
	//   `SetCapsuleSize` задаёт размер В ЛОКАЛЬНЫХ единицах компонента, которые масштаб актора
	//   потом домножит ещё раз. Без деления капсула была бы в сто раз больше модели.
	// Минимумы 30 (полувысота) и 25 (радиус) — чтобы по мелкой модели всё равно можно было кликнуть.
	inline void FitToMeshBounds(UCapsuleComponent* Capsule, const USkeletalMeshComponent* Mesh,
	                            const FVector& ActorScale)
	{
		if (!Capsule || !Mesh) { return; }
		const FBoxSphereBounds MB = Mesh->Bounds;
		const float ScaleZ = FMath::Max(FMath::Abs(ActorScale.Z), 1e-4f);
		const float HalfH  = FMath::Max(MB.BoxExtent.Z, 30.f) / ScaleZ;
		const float Rad    = FMath::Max(FMath::Max(MB.BoxExtent.X, MB.BoxExtent.Y), 25.f) / ScaleZ;
		Capsule->SetCapsuleSize(Rad, HalfH);
		Capsule->SetRelativeLocation(FVector(0.f, 0.f, HalfH));
	}
}
