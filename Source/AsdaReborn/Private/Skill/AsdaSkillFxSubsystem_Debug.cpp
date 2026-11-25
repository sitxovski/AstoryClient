// Copyright (c) 2026 @sitxovski (Discord)
// AsdaSkillFxSubsystem_Debug.cpp — ★ R3-c 12.11.2026 (разрез AsdaSkillFxSubsystem.cpp, дословный перенос): дев-команды эффектов навыков.
//   Общее для частей — в AsdaSkillFxSubsystem_Private.h; шапка и реверс — в AsdaSkillFxSubsystem.cpp.
#include "Skill/AsdaSkillFxSubsystem.h"
#include "AsdaDevConsole.h"   // ★ античит А2: ASDA_DEV_CVAR / ASDA_DEV_CONSOLE — дев-ключи и команды вне Shipping

#include "Animation/AnimSingleNodeInstance.h"
#include "Components/SkeletalMeshComponent.h"
#include "Engine/World.h"
#include "HAL/IConsoleManager.h"
#include "Skill/AsdaSkillClipRow.h"
#include "World/AsdaPlayerCharacter.h"
#include "World/AsdaWorldSubsystem.h"
#include "World/AsdaCombatTrace.h"   // ★ 18.10.2026: журнал боя — записи урона навыка, клип, кадр удара, снаряд, эффекты
#include "AsdaSkillFxSubsystem_Private.h"   // ★ R3-c 12.11.2026: общее для частей AsdaSkillFxSubsystem*.cpp

namespace {
// ═══ КОНСОЛЬНЫЕ КОМАНДЫ (проверка БЕЗ сервера) ═════════════════════════════════════════════════
// ★ Закон weapon-glow-saga: headless СЛЕП к billboard-ветке эффектов — проверять только в ИГРЕ.
	// Локальный персонаж (его ставит UAsdaWorldSubsystem при входе в мир).
	AAsdaPlayerCharacter* LocalChar(UWorld* W)
	{
		if (!W || !W->GetGameInstance()) { return nullptr; }
		if (UAsdaWorldSubsystem* WS = W->GetGameInstance()->GetSubsystem<UAsdaWorldSubsystem>())
		{
			return Cast<AAsdaPlayerCharacter>(WS->LocalPlayerActor);
		}
		return nullptr;
	}

	UAsdaSkillFxSubsystem* Sub(UWorld* W)
	{
		return (W && W->GetGameInstance()) ? W->GetGameInstance()->GetSubsystem<UAsdaSkillFxSubsystem>() : nullptr;
	}
}

#if ASDA_DEV_CONSOLE   // ★ античит А2: дев-команда, в Shipping не регистрируется
namespace {
	FAutoConsoleCommandWithWorldAndArgs GCmdClipInfo(
		TEXT("asda.Skill.ClipInfo"),
		TEXT("asda.Skill.ClipInfo <кодНавыка> [подтипОружия] [пол 1/2] — все ветки клипа навыка из DT_SkillClips."),
		FConsoleCommandWithWorldAndArgsDelegate::CreateLambda([](const TArray<FString>& Args, UWorld* W)
		{
			UAsdaSkillFxSubsystem* S = Sub(W);
			if (!S || Args.Num() < 1) { UE_LOG(LogAsdaSkillFx, Warning, TEXT("нужен код навыка")); return; }
			const int32 Id = FCString::Atoi(*Args[0]);
			AAsdaPlayerCharacter* C = LocalChar(W);
			const int32 Wsub = (Args.Num() > 1) ? FCString::Atoi(*Args[1]) : (C ? C->EquippedWeaponSubType : 99);
			const int32 Sex = (Args.Num() > 2) ? FCString::Atoi(*Args[2]) : (C ? (int32)C->Sex : 1);
			TArray<const FAsdaSkillClipRow*> Rows;
			S->CollectRows(Id, Rows);
			UE_LOG(LogAsdaSkillFx, Display, TEXT("=== навык %d: строк %d (пол %d, оружие %d)"), Id, Rows.Num(), Sex, Wsub);
			for (const FAsdaSkillClipRow* R : Rows)
			{
				UE_LOG(LogAsdaSkillFx, Display,
					TEXT("  %s [%s] anim_%d оружие=%s COMBO0=%.0f/%.0f/%.0f длит=%.0fмс загрузчик=%s ")
					TEXT("источник=%s звук=%s jeb=%s поля=%s cond=%s"),
					*R->Clip, *R->Gender, R->AnimIdx, *R->WeaponTypes, R->Combo0Ms, R->Combo1Ms, R->Combo2Ms,
					R->PhaseDurMs, *R->Loader, *R->Source, *R->Sounds, *R->Jebs, *R->JebSlots, *R->Cond);
				// ★ ПРОГРАММА ПОКАЗА — то, что реально исполнит рантайм (снято с тела ТИКА, массив 5).
				UE_LOG(LogAsdaSkillFx, Display,
					TEXT("      тик=%s снаряд=%s | КАСТ: %s (кость %s) звук «%s»"),
					*R->TickFn, R->Proj ? TEXT("ДА") : TEXT("нет"),
					R->FxCast.IsEmpty() ? TEXT("—") : *R->FxCast,
					R->FxCastBone.IsEmpty() ? TEXT("—") : *R->FxCastBone, *R->SndCast);
				UE_LOG(LogAsdaSkillFx, Display,
					TEXT("      УДАР фаза0 (%.0f мс): %s | фаза1 (%.0f): %s | фаза2 (%.0f): %s | звук «%s»"),
					R->Combo0Ms, R->FxHit0.IsEmpty() ? TEXT("—") : *R->FxHit0,
					R->Combo1Ms, R->FxHit1.IsEmpty() ? TEXT("—") : *R->FxHit1,
					R->Combo2Ms, R->FxHit2.IsEmpty() ? TEXT("—") : *R->FxHit2, *R->SndHit);
				if (!R->FxNote.IsEmpty())
				{
					UE_LOG(LogAsdaSkillFx, Display, TEXT("      ★ НЕ РАЗОБРАНО: %s"), *R->FxNote);
				}
			}
			FString Why;
			float MaxMs = 0.f;
			const bool bHasMax = S->TryGetCastBarMaxMs(Id, Sex, Wsub, MaxMs);
			if (const FAsdaSkillClipRow* P = S->ResolveRow(Id, Sex, Wsub, &Why, S->GetTargetSizeClass(nullptr)))
			{
				UE_LOG(LogAsdaSkillFx, Display,
					TEXT("  ВЫБОР: %s (anim_%d), кость %s, максимум полосы = %s — %s"),
					*P->Clip, P->AnimIdx, FxBoneFor(Wsub),
					bHasMax ? (MaxMs > 0.f ? *FString::Printf(TEXT("COMBO0 %.0f мс"), MaxMs)
					                       : TEXT("0 по данным оригинала ⇒ эталонная подмена 1 мс"))
					        : TEXT("НЕИЗВЕСТЕН (строки нет)"),
					*Why);
			}
			else { UE_LOG(LogAsdaSkillFx, Display, TEXT("  ВЫБОР: нет — %s"), *Why); }
		}));
}
#endif // ASDA_DEV_CONSOLE

#if ASDA_DEV_CONSOLE   // ★ античит А2: дев-команда, в Shipping не регистрируется
namespace {
	FAutoConsoleCommandWithWorldAndArgs GCmdPlayFx(
		TEXT("asda.Skill.PlayFx"),
		TEXT("asda.Skill.PlayFx <кодНавыка> — проиграть клип+эффекты+звук навыка на своём персонаже (без сервера)."),
		FConsoleCommandWithWorldAndArgsDelegate::CreateLambda([](const TArray<FString>& Args, UWorld* W)
		{
			UAsdaSkillFxSubsystem* S = Sub(W);
			AAsdaPlayerCharacter* C = LocalChar(W);
			if (!S || !C || Args.Num() < 1)
			{
				UE_LOG(LogAsdaSkillFx, Warning, TEXT("нужен код навыка и вход в мир"));
				return;
			}
			S->PlaySkillVisual(C, FCString::Atoi(*Args[0]));
		}));
}
#endif // ASDA_DEV_CONSOLE

#if ASDA_DEV_CONSOLE   // ★ античит А2: дев-команда, в Shipping не регистрируется
namespace {
// ★ РАЗМЕРНЫЙ КЛАСС ЦЕЛИ вручную: источника значения `сущность + 7356` в порту нет (в срезе
//   бинаря только читатели), поэтому три ветки навыка 513 (`sq-hexa-s/-m/-l`) проверяются так.
	FAutoConsoleCommandWithWorldAndArgs GCmdTargetSize(
		TEXT("asda.Skill.TargetSize"),
		TEXT("asda.Skill.TargetSize <0|1|2|-1> — размерный класс цели (эталонное поле сущности +7356): ")
		TEXT("0 обычная, 1 средняя, 2 крупная, -1 снять переопределение."),
		FConsoleCommandWithWorldAndArgsDelegate::CreateLambda([](const TArray<FString>& Args, UWorld* W)
		{
			UAsdaSkillFxSubsystem* S = Sub(W);
			if (!S) { return; }
			if (Args.Num() >= 1) { S->TargetSizeOverride = FCString::Atoi(*Args[0]); }
			UE_LOG(LogAsdaSkillFx, Display,
				TEXT("[навыки-визуал] размерный класс цели: переопределение=%d, действующий=%d ")
				TEXT("(записи в поле +7356 в срезе бинаря нет — только три читателя)"),
				S->TargetSizeOverride, S->GetTargetSizeClass(nullptr));
		}));
}
#endif // ASDA_DEV_CONSOLE

#if ASDA_DEV_CONSOLE   // ★ античит А2: дев-команда, в Shipping не регистрируется
namespace {
// ★★★ СТЫК «УРОН ↔ ВИЗУАЛ»: что сейчас припарковано и чего ждёт.
//   Отвечает на главный вопрос живой проверки: «цифра запаздывает/убегает — где она сидит?»
	FAutoConsoleCommandWithWorld GCmdVerifyJoin(
		TEXT("asda.VerifySkillHitJoin"),
		TEXT("asda.VerifySkillHitJoin — показы навыков, их фазы и ПРИПАРКОВАННЫЕ записи 4102 ")
		TEXT("(урон ждёт порога фазы или прилёта носителя)."),
		FConsoleCommandWithWorldDelegate::CreateLambda([](UWorld* W)
		{
			UAsdaSkillFxSubsystem* S = Sub(W);
			if (!S) { UE_LOG(LogAsdaSkillFx, Warning, TEXT("нет подсистемы визуала навыков")); return; }
			S->DumpHitJoin();
		}));
}
#endif // ASDA_DEV_CONSOLE

#if ASDA_DEV_CONSOLE   // ★ античит А2: дев-команда, в Shipping не регистрируется
namespace {
	FAutoConsoleCommandWithWorld GCmdStopFx(
		TEXT("asda.Skill.StopFx"),
		TEXT("asda.Skill.StopFx — погасить показ навыка на своём персонаже."),
		FConsoleCommandWithWorldDelegate::CreateLambda([](UWorld* W)
		{
			if (UAsdaSkillFxSubsystem* S = Sub(W)) { S->StopSkillVisual(LocalChar(W)); }
		}));
}
#endif // ASDA_DEV_CONSOLE

#if ASDA_DEV_CONSOLE   // ★ античит А2: дев-команда, в Shipping не регистрируется
namespace {
	FAutoConsoleCommandWithWorld GCmdVerify(
		TEXT("asda.VerifySkillFx"),
		TEXT("asda.VerifySkillFx — что реально показывается: клип, кадр, эффект-акторы, их привязка и секции."),
		FConsoleCommandWithWorldDelegate::CreateLambda([](UWorld* W)
		{
			AAsdaPlayerCharacter* C = LocalChar(W);
			UAsdaSkillFxSubsystem* S = Sub(W);
			if (!C || !S) { UE_LOG(LogAsdaSkillFx, Warning, TEXT("нет персонажа/подсистемы")); return; }
			UE_LOG(LogAsdaSkillFx, Display, TEXT("=== VerifySkillFx: пол=%d оружие=%d показ=%s"),
				(int32)C->Sex, C->EquippedWeaponSubType, S->IsSkillVisualActive(C) ? TEXT("ИДЁТ") : TEXT("нет"));
			if (C->BaseSkel)
			{
				UAnimationAsset* Cur = C->BaseSkel->GetSingleNodeInstance()
					? C->BaseSkel->GetSingleNodeInstance()->GetAnimationAsset() : nullptr;
				UE_LOG(LogAsdaSkillFx, Display, TEXT("  клип на меше: %s  позиция=%.3fс"),
					*GetNameSafe(Cur), C->BaseSkel->GetPosition());
				// Кость показываем ТУ, которую реально выберет спавн при текущем оружии (8_a_End / 8_b1).
				const TCHAR* Bone = FxBoneFor(C->EquippedWeaponSubType);
				const int32 BoneIdx = C->BaseSkel->GetBoneIndex(FName(Bone));
				UE_LOG(LogAsdaSkillFx, Display, TEXT("  кость %s (по оружию %d): индекс=%d мир=%s"),
					Bone, C->EquippedWeaponSubType, BoneIdx,
					BoneIdx != INDEX_NONE ? *C->BaseSkel->GetBoneLocation(FName(Bone)).ToString() : TEXT("НЕТ"));
			}
			// ★ Эффекты показа НЕ парентятся к персонажу (ConfigureBoneAttach ведёт их тиком), поэтому
			//   печатаем их из самого показа, а не из `GetAttachedActors` — та печатала 0 при живом показе.
			S->DumpActiveFx();
		}));
}
#endif // ASDA_DEV_CONSOLE
