// Copyright (c) 2026 @sitxovski (Discord)
// AsdaSkillUseSubsystemInternal.h — общий внутренний заголовок частей `AsdaSkillUseSubsystem*.cpp`.
//   ★ R2 рефакторинга (10.11.2026): сюда ДОСЛОВНО вынесены помощники, нужные нескольким частям (static/анонимное
//   пространство имён сохранены — у каждой части своя копия без состояния), и объявления определений, оставшихся
//   в одной части (ключи, категория журнала). Подключать ПОСЛЕ включений пролога части: своих #include здесь нет.
#pragma once

DECLARE_LOG_CATEGORY_EXTERN(LogAsdaSkillUse, Log, All);   // категория журнала — одна на все части (определение в базовой части)
// ── Объявления определений, оставшихся в одной части (R2) ──
bool AsdaSkillApproachOverAttackOn();   // ключ `asda.Skill.ApproachOverAttack` включён? (определение — AsdaSkillUseSubsystem.cpp)
#if ASDA_DEV_CONSOLE
extern TAutoConsoleVariable<int32> CVarAsdaSkillLatchGuard;   // asda.Skill.LatchGuard (определение без static — AsdaSkillUseSubsystem.cpp)
#else
extern TAsdaConstCVar<int32> CVarAsdaSkillLatchGuard;         // Shipping: константа на месте ключа
#endif

// ★ 28.10.2026 [ПОРТ] (решение владельца): плывущему — только ход и предметы (тело — AsdaPlayerCharacter_Move.cpp).
bool AsdaSwimActionLocked(const class AAsdaPlayerCharacter* Ch);
#if ASDA_COMBAT_TRACE
namespace AsdaCtPc
{
	FString PlayerNick(const AAsdaPlayerCharacter* C);      // ник персонажа с плашки
}
#endif

// ★★★ ПЕРЕВОД ДЛИТЕЛЬНОСТИ КЛИПА В РЕАЛЬНОЕ ВРЕМЯ (16.09.2026, вторая половина системы темпа).
//   `T.FullMs` и `T.Combo0Ms` — это КЛИП-ВРЕМЯ (мс), снятое с данных оригинала. Эталон отмеряет
//   срок состояния 24 часами анимобъекта навыка, а те идут шагом `dt · animSpeedScale`
//   (`CCharacter + 0x500`; см. `UAsdaWorldSubsystem::GetAnimSpeedScale`). Значит при множителе
//   1.2 клип кончается на 20 % раньше по стенным часам — и срок показа обязан укоротиться так же,
//   иначе состояние 24 переживало бы картинку и держало доворот к цели и гейт клипа.
//   Множитель берётся у СВОЕГО персонажа: обе точки вызова стоят под `if (bMine)`.
static double AsdaCastShowSeconds(UGameInstance* GI, float ClipMs)
{
	if (ClipMs <= 0.f) { return 0.0; }
	float Scale = 1.f;
	if (const UAsdaWorldSubsystem* WS = GI ? GI->GetSubsystem<UAsdaWorldSubsystem>() : nullptr)
	{
		if (const AAsdaPlayerCharacter* Self = Cast<AAsdaPlayerCharacter>(WS->LocalPlayerActor))
		{
			Scale = FMath::Max(0.1f, WS->GetAnimSpeedScale(Self));
		}
	}
	return (double)ClipMs / 1000.0 / (double)Scale;
}
namespace
{

	// ★★★ ЧЕТЫРЕ ВИДА НАВЫКА = `targetSlotType` (`.sif +0x34`, серверное `SkillInfo[209·id + 52]`).
	//   Значения проверены на реальном `skill_info.sif`: у применимых классовых навыков встречаются
	//   ТОЛЬКО 1, 2, 5, 6 (0 — исключительно пассивы подтипа 3). Тексты — нотисы `значение + 9488`.
	FORCEINLINE const TCHAR* TargetSlotTypeName(int32 V)
	{
		switch (V)
		{
		case 1: return TEXT("только на себя");
		case 2: case 3: return TEXT("на игрока и на себя (группа)");
		case 5: return TEXT("только на игрока");
		case 6: return TEXT("на моба или игрока (атака)");
		case 7: return TEXT("по точке земли");
		default: return TEXT("не задан (пассив)");
		}
	}
}

// Включён ли ключ `asda.Skill.LatchGuard` (аксессор — имя ключа не попадает в рабочий код).
static bool AsdaSkillLatchGuardOn() { return CVarAsdaSkillLatchGuard.GetValueOnGameThread() != 0; }

// ★ 25.10.2026 (ревизия 1.1): номер прихода 5271 — общий счётчик книги навыков (там же нумеруются 5465 и 5274). Книги нет — 0
//   («неизвестен», взвод не сверяется).
static uint64 AsdaNextCdArrivalSeq(UGameInstance* GI)
{
	UAsdaSkillWindowSubsystem* SW = GI ? GI->GetSubsystem<UAsdaSkillWindowSubsystem>() : nullptr;
	return SW ? SW->NextCooldownArrivalSeq() : 0;
}

void AsdaSkillFxSetShowFromCastIdle(bool bOn);   // ★ 06.11.2026: показ запущен ответом 5270 (AsdaSkillFxSubsystem.cpp)
