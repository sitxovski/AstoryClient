// Copyright (c) 2026 @sitxovski (Discord)
// ★ R5 (12.11.2026, дубли): ОДНА копия «скрыть/показать подвесные эффект-акторы владельца». До R5 — две дословные копии:
//   `AsdaSetAttachedFxHidden` (AsdaWorldSubsystemInternal.h, части мира и дальность обзора) и
//   `AsdaStealthSetAttachedFxHidden` (AsdaStealthSubsystem.cpp). Тело и комментарий перенесены без изменений.
#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "AsdaEffectActor.h"   // AAsdaEffectActor — класс подвесного эффекта

// ★★★ [ПОРТ] ПОДВЕСНЫЕ ЭФФЕКТЫ ГАСЯТСЯ ВМЕСТЕ С ПЕРСОНАЖЕМ.
//   [ОРИГИНАЛ] у эталона это бесплатно: крылья, свечение оружия и ауры — не отдельные объекты, а
//   узлы того же `CCharacter`, и один бит `char+0x40C` убирает их вместе с телом.
//   [ПОРТ] у нас каждый такой эффект — САМОСТОЯТЕЛЬНЫЙ актор (`AAsdaEffectActor`), лишь
//   приаттаченный к персонажу, а `SetActorHiddenInGame` на родителя чужие акторы не трогает.
//   Без этой функции скрытый оператор оставлял бы в воздухе висящие крылья и свечение меча —
//   то есть выдавал бы себя ровно тем, что должен был спрятать.
//   Рекурсивно: часть эффектов висит вторым ярусом (эффект на эффекте, см. AsdaSkillFxSubsystem).
static void AsdaSetAttachedFxHidden(AActor* Owner, bool bHidden)
{
	if (!Owner) { return; }
	TArray<AActor*> Attached;
	Owner->GetAttachedActors(Attached, /*bResetArray*/true, /*bRecursivelyIncludeAttachedActors*/true);
	for (AActor* A : Attached)
	{
		if (A && A->IsA<AAsdaEffectActor>()) { A->SetActorHiddenInGame(bHidden); }
	}
}
