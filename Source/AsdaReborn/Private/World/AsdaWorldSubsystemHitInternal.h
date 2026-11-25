// Copyright (c) 2026 @sitxovski (Discord)
// AsdaWorldSubsystemHitInternal.h — общий внутренний заголовок частей `AsdaWorldSubsystem_Hit*.cpp`.
//   ★ R2 рефакторинга (10.11.2026): сюда ДОСЛОВНО вынесены помощники, нужные нескольким частям (static/анонимное
//   пространство имён сохранены — у каждой части своя копия без состояния), и объявления определений, оставшихся
//   в одной части (ключи, категория журнала). Подключать ПОСЛЕ включений пролога части: своих #include здесь нет.
#pragma once

// ── Объявления определений, оставшихся в одной части (R2) ──
#if ASDA_DEV_CONSOLE
extern TAutoConsoleVariable<int32> CVarShowMissOnCode2;            // ключ показа «Miss» по коду 2 (определение без static — AsdaWorldSubsystem_Hit.cpp)
extern TAutoConsoleVariable<int32> CVarAsdaOwnHitsServerCommit;    // ключ «свои удары подтверждает сервер» (определение без static — AsdaWorldSubsystem_Hit.cpp)
extern TAutoConsoleVariable<int32> CVarAsdaCharHitVictimFx;        // ключ эффекта жертвы при ударе по персонажу (определение без static — AsdaWorldSubsystem_Hit.cpp)
#else
extern TAsdaConstCVar<int32> CVarShowMissOnCode2;                  // Shipping: константа на месте ключа
extern TAsdaConstCVar<int32> CVarAsdaOwnHitsServerCommit;          // Shipping: константа на месте ключа
extern TAsdaConstCVar<int32> CVarAsdaCharHitVictimFx;              // Shipping: константа на месте ключа
#endif
extern int32 GAsdaOwnPhantomDropped;   // сколько своих непримененных значений не показано (определение без static — AsdaWorldSubsystem_Hit.cpp)

// ★★★ СНИМАТЬ ЛИ ПАРУ ИЗ ОЧЕРЕДИ УРОНОВ. Единая точка для разбора и для расхода буфера.
//
//   ЭТАЛОН. Правило живёт ТОЛЬКО в ветке БЛИЖНЕГО оружия `case 0..5` разбора кадра удара
//   (`Player_ProcessAttackTick`, entity_char.c:26837 — `switch (attackerChar->equippedWeaponType)`,
//   сам съём пары на entity_char.c:26886-26889):
//       if ( field_39C == field_3A0 + 1 && field_39C % 2u )   // последний шаг И нечётное комбо
//       { crit = pop(); if (crit == -1) crit = 0; else if (crit < -1) dmg &= ~0x80000000; }
//   `field_39C` = StepCount строки combo_time (пишется entity_char.c:14609), `field_3A0` = шаг.
//
//   ★ У ДАЛЬНОБОЙНОГО ОРУЖИЯ ПАРЫ НЕТ ВООБЩЕ. Ветки `case 8/0xA` (лук, арбалет, entity_char.c:27302),
//   `case 9` (entity_char.c:27564) и `case 0xB` (БАЛЛИСТА, entity_char.c:27749) содержат РОВНО ОДИН
//   `playerArrayPopFront` и условия `field_39C` не упоминают вовсе (сверено grep по телу ветки).
//
//   ★★ А в ближнем бою правило не исполняется НИКОГДА: у всех клипов обычной атаки мелея число
//   шагов ЧЁТНОЕ — `ch4_*_01_normalattack01` 4, `_01_normalattack03` 6, `_02_normalattack01` 4,
//   `_05_normalattack01` 6, `_06_normalattack01` 4, `_09_normalattack01` 4. Нечётных (3) в
//   `combo_time.csv` всего 8 строк, и все они — клипы НАВЫКОВ (`bigmeto`, `destarow`, `runfire`)
//   и эмоция `Shoveling`, а не обычная атака. То есть в оригинале это мёртвая ветка.
//
//   ⚠⚠ ДВА ТУПИКА, НЕ ПОВТОРЯТЬ (наступал 24.08.2026):
//   1) Порт применял правило ко ВСЕМ типам оружия. На баллисте (subType 11, StepCount 1) условие
//      `1 == 0+1 && 1%2` истинно на каждом замахе, и из очереди снимались ДВА элемента вместо
//      одного, хотя сервер расходует ровно один. Живой поток моба rid=37: `[181 223 -2 205]` при
//      дельтах HP 181, 223, 0, 205 — склейка 181+223 = 404 давала цифру втрое больше настоящей.
//   2) Попытка опознать спутника крита ПО ЗНАЧЕНИЮ (`<= -1`) — тоже тупик: эталон считает
//      спутником и `-1`, и любое `< -1`, то есть ровно те же коды, что и самостоятельные
//      сентинелы. На потоке rid=41 `[240 -2 -2 253]` проверка склеила `240` с сентинелом `-2` и
//      сдвинула очередь: за один замах показались три удара — «как из пулемёта».
//   Верный гейт — ТИП ОРУЖИЯ, как в эталоне.
static bool AsdaTakesCritPair(int32 WeaponSubType, int32 Steps, int32 StepIdx, const TArray<int32>& Buf, int32 k)
{
	if (WeaponSubType < 0 || WeaponSubType > 5) { return false; }     // эталон: только ближний бой
	if (Steps != StepIdx + 1 || (Steps % 2) == 0) { return false; }   // эталонное условие шага
	return (k + 1 < Buf.Num());
}
static bool AsdaHitVictimFxOn() { return CVarAsdaCharHitVictimFx.GetValueOnGameThread() != 0; }

// Импакт нашего/чужого обычного удара (реверс SkillEffect_GetBasicAttackJebByClassWeapon@0xA52720:
//   выбор battk01/02/03/04 по (weaponSubType, hitIdx)).
static const TCHAR* BasicAttackImpactBP(int32 W, int32 Hit)
{
	auto B02 = TEXT("BP_ImpactBattk02"); auto B03 = TEXT("BP_ImpactBattk03");
	auto B04 = TEXT("BP_ImpactBattk04"); auto B01 = TEXT("BP_ImpactBattk01");
	switch (W)
	{
	case 0: case 4: return (Hit == 1) ? B02 : (Hit == 2) ? B03 : B01;
	case 1:  return (Hit == 0 || Hit == 4) ? B02 : (Hit == 2 || Hit == 3) ? B03 : (Hit == 6) ? B04 : B01;
	case 2:  return (Hit == 0 || Hit == 2) ? B02 : (Hit == 4 || Hit == 5) ? B03 : B01;
	case 3:  return (Hit == 0) ? B02 : (Hit == 2) ? B04 : B01;
	case 5:  return (Hit == 0 || Hit == 2) ? B02 : B01;
	case 6:  return (Hit == 0) ? B02 : (Hit == 1 || Hit == 2) ? B03 : B01;
	case 8: case 9: case 10: case 11: return B03;
	default: return B01;
	}
}

// ═══════════════ ★ СНАРЯДЫ ДАЛЬНОБОЙНОГО ОРУЖИЯ (реверс Reports/projectile_re.md) ═══════════════
//
// Полный поток эталона (Player_ProcessAttackTick@0x99F2A0 для себя, CCharacter_AttackAnimTick@0x9A2200
//   для соседей — ветки switch(equippedWeaponType) идентичны):
//     8/0xA лук      : кадр 0 → «стрела на тетиве» (charObjectArray_initSixSlots, меш ..._0),
//                      кадр удара → выстрел (skillFireArrowProjectile) + S_c_bnormal1.wav,
//                      сразу после выстрела снова накладывается следующая стрела;
//     9 арбалет      : кадр удара → выстрел (charFx_spawnFlyArrowProjectile) + S_c_cbnormal1.wav;
//     0xB баллиста   : кадр удара → выстрел (charFx_spawnFlyArrowProjectile) + S_c_balnormal1.wav.
//   Урон УЕЗЖАЕТ В СНАРЯДЕ (playerArrayPopFront → slot[337]) и показывается ПО ПРИЛЁТЕ
//   (Combat_ProcessHitResultAndShowDamage@0xA53370) вместе с импактом eff_0_char_99_battk03 и звуком
//   попадания (S_c_bsnormal2 / S_c_cbnormal2 / S_c_balnormal3). Отдельного сетевого пакета о выстреле НЕТ.
static const TCHAR* kArrowMeshFly  = TEXT("/Game/AsdaAssets/Models/Char/ch1_n_00_arrs001_00_00_1/StaticMeshes/ch1_n_00_arrs001_00_00_1.ch1_n_00_arrs001_00_00_1");
static const TCHAR* kArrowMeshNock = TEXT("/Game/AsdaAssets/Models/Char/ch1_n_00_arrs001_00_00_0/StaticMeshes/ch1_n_00_arrs001_00_00_0.ch1_n_00_arrs001_00_00_0");
// ★ Дедуп №2 В1-C (F5): .jeb шлейфа стрелы — одно имя на спавн, лог и проверку `asda.VerifyProjectile` (было 3 литерала).
static const TCHAR* kArrowTrailJeb = TEXT("eff_0_char_03_flyarr01-a_00");
