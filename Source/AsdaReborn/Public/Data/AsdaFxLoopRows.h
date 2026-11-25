// Copyright (c) 2026 @sitxovski (Discord)
// AsdaFxLoopRows.h — строка DataTable `DT_FxLoop`: «как эффект ЗАЦИКЛИВАЕТСЯ в оригинале».
//
// ═════════════════════════════ ЗАЧЕМ (задача владельца 08.10.2026) ═════════════════════════════════
// «Почти все зацикленные эффекты зациклены РЕЗКО: последний кадр обрывает цикл и начинается заново».
// Разбор — `Reports/owner_1008/fx_loop.md`. Корень — сборщики BP/карт теряют ДВА вида исходных
// данных, без которых рантайм не может воспроизвести конец периода 1:1:
//   1) endMode КАЖДОГО эмиттера С ДИСКА. Ветки growQuad/mesh12 `build_emitter` пишут в ассет
//      `end_mode = 1` (рестарт) почти всем квадам и мешам, а у оригинала 0 = «застыть на КОНЦЕ»,
//      3 = «удержать часть каналов, остальные маршируют дальше» (по типу эмиттера, см.
//      `AsdaEffectActor.cpp`, блок «ЦИКЛ 1:1»). Отсюда рывок раз в период у порталов, башен,
//      оружия (луки 0x14: поворот прыгает назад) и т.д.;
//   2) КОНТЕЙНЕР эффекта — шапка `.jeb` (+0x08 длительность, +0x40 endMode). Эталон
//      `objAnimRotateUpdate@0x114E050`: по концу длительности 1 = рестарт ВСЕГО эффекта,
//      0/2 = эффект окончен (на карте холдер `sceneComposite_UpdateGridsAndEffects@0x11DD150`
//      поднимает его заново), 3 = вечный. В порту контейнера не было вовсе.
//   3) (только карты) СИД-УЗЛЫ трека: у квадов/колец с keyframe-ключами печка карт кладёт трек БЕЗ
//      КОНЕЧНОГО сида (`ASDA_MAP_KF_SEED` по умолчанию выключен), трек замирает на последнем ключе и
//      раз в период прыгает к старту. Эталон: трек = [СТАРТ @0] + ключи + [КОНЕЦ @ длительность]
//      (замер живого оригинала 15.08, `fx_pipeline_ground_truth.md`). Полный трек с сидами лежит
//      здесь, рантайм ДОПИСЫВАЕТ недостающие сид-узлы ТОЛЬКО если трек ассета совпал с «печкой без
//      сидов» поузлово по всем полям (08.10: раньше — по отпечатку-сумме); узлы ассета остаются свои,
//      отражение type-2 (`ChiralNormal`) переносится на дописанные узлы. Ручная правка ⇒ не трогается.
//
// ★ ПОЧЕМУ ТАБЛИЦА, А НЕ ПЕРЕПЕЧКА АССЕТОВ: 1282 BP_Fx и ~2400 акторов на 20 картах — это ассеты
//   владельца (свечение, ручные правки башни и т.п.). Таблица — НОВЫЙ генерируемый ассет; ни один
//   прежний ассет не переписывается. Генератор — `Tools/asda_build_fx_loop_dt.py` (из `.jeb`
//   архива, ничего не выдумывает: только поля диска и выход того же `build_emitter`).
// ★ Откат без пересборки: `asda.Fx.Loop 0` (вся логика цикла как было), `asda.Fx.LoopSeeds 0`
//   (не подставлять сид-узлы карт).
#pragma once

#include "CoreMinimal.h"
#include "Engine/DataTable.h"
#include "AsdaEffectActor.h"
#include "AsdaFxLoopRows.generated.h"

// ★ Дедуп №2, В2-C (F3, 11.10.2026): объектный путь таблицы — одна строка на порт (чтение эффект-актором и удержание на
//   сессию у `UAsdaEffectsSubsystem::HoldFxLoopTable`). Генератор — Tools/asda_build_fx_loop_dt.py.
namespace AsdaFxLoop { inline constexpr const TCHAR* kTablePath = TEXT("/Game/AsdaAssets/Data/DT_FxLoop.DT_FxLoop"); }

// Один эмиттер `.jeb` в порядке файла. Связь с эмиттером ассета — по ИМЕНИ и НОМЕРУ ПОВТОРА имени
// (сборщики пропускают часть эмиттеров, индексы разъезжаются — закон «сверять по имени»).
USTRUCT(BlueprintType)
struct FAsdaFxLoopEmitterRow
{
	GENERATED_BODY()
	UPROPERTY(EditAnywhere, BlueprintReadOnly) FString Name;          // имя эмиттера из записи (+0x00, 32 б)
	UPROPERTY(EditAnywhere, BlueprintReadOnly) int32 Occ = 0;         // номер повтора имени в файле (0 = первый)
	UPROPERTY(EditAnywhere, BlueprintReadOnly) int32 DiskType = 0;    // тип записи .jeb (0xB/0xC/0xE/0x13/0x14/0x18/…)
	UPROPERTY(EditAnywhere, BlueprintReadOnly) int32 DiskEndMode = -1; // endMode диска (у колец — loopMode); −1 = нет поля
	// ★ Только карты: ПОЛНЫЙ трек эталона (сиды + ключи), если трек ассета их потерял. Пусто = не нужен.
	UPROPERTY(EditAnywhere, BlueprintReadOnly) TArray<FAsdaFxNode> SeedTrack;
	UPROPERTY(EditAnywhere, BlueprintReadOnly) int32 SeedBaseCount = 0;  // узлов в треке ассета БЕЗ сидов (отпечаток)
	UPROPERTY(EditAnywhere, BlueprintReadOnly) float SeedBaseSum = 0.f;  // отпечаток значений того же трека (см. FxLoopTrackPrint) — только лог/совместимость
	// ★ 08.10.2026 (ревизия fx_loop, СРЕДН-2/3): трек «печка БЕЗ сидов» ЦЕЛИКОМ. Рантайм сравнивает с ним трек
	//   ассета ПОУЗЛОВО по всем полям `FAsdaFxNode` (допуск 1e-3): любая ручная правка владельца (поворот, UV,
	//   скорость, офсет на 1 ед.) ⇒ трек не трогается. Отпечаток-сумма (вес 0.001 у офсетов, допуск 0.05)
	//   пропускал правку до ~50 ед. и отражённый трек башни alpen. Пусто = подстановки нет.
	UPROPERTY(EditAnywhere, BlueprintReadOnly) TArray<FAsdaFxNode> SeedBaseTrack;
};

// Строка таблицы = один `.jeb`. Ключ строки — база имени файла в нижнем регистре, `-` → `_`.
USTRUCT(BlueprintType)
struct FAsdaFxLoopRow : public FTableRowBase
{
	GENERATED_BODY()
	UPROPERTY(EditAnywhere, BlueprintReadOnly) float ContainerDuration = 0.f;   // шапка +0x08, сек
	UPROPERTY(EditAnywhere, BlueprintReadOnly) int32 ContainerEndMode = 3;      // шапка +0x40: 0/2 конец, 1 рестарт, 3 вечный
	UPROPERTY(EditAnywhere, BlueprintReadOnly) TArray<FAsdaFxLoopEmitterRow> Emitters;   // все записи файла по порядку
};
