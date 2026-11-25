// Copyright (c) 2026 @sitxovski (Discord)
// FAsdaBuffFxRow — строка DataTable `DT_BuffFx`: ВИЗУАЛ ОДНОГО СОСТОЯНИЯ (баффа), id 0..349.
//
// ═══════════════════ ЭТАЛОН (третий канал визуала навыков, `skill_fx_anim.md` §16/§17) ═══════════
//
// ЦЕПОЧКА. `skill_info.sif[skillId].buff0/1/2 SkillId` (+0x48/+0x59/+0x6A, int16) — это id
//   СОСТОЯНИЯ 0..349, а НЕ id навыка. Он индексирует ДВЕ таблицы обработчиков по 350 записей:
//     игрок — `charSkillFxHandlerTableInit@0x9AB290`      → `*((_DWORD*)pChar + 52304 + buffId)`
//     моб   — `CCharEffect_InitSkillEffectTable@0xF601D0` → `*((_DWORD*)pMob  +  1844 + buffId)`
//   Вызывают `skillBuffAdd@0xF836E0` и `cChar_restartSkillEffectByType@0xA2AD60` (сеть 5272/4102).
//   Тело обработчика спавнит `.jeb` ЛИТЕРАЛОМ; долгоживущие кладут хэндлы в запись баффа по
//   +24/+28 (`fx0`/`fx1`), откуда их гасит `buffStopEffectsBySkillId@0xF83930`.
//
// ПРОГРАММА ПОКАЗА (`PlayerFx` / `MobFx`) — записи через `;`, поля записи через `|`:
//     <jeb>|<attach>|<bone>|<slot>|<loop>|<variant>|<owner>
//   attach  — режим привязки эталона (номер в таблице 16 хендлеров `CEffectMgr`):
//             3  `effectSyncPosToRenderEntity`   — позиция СУЩНОСТИ (renderId), без ориентации;
//             5  `effectNodeTrackTargetChar`     — позиция ПЕРСОНАЖА + 15 по игровой Y;
//             6  `effectNodeFollowPlayerBone`    — ПОЛНАЯ мировая матрица персонажа (кость не нужна);
//             7  `effectNodeUpdateCasterItemPos` — позиция КОСТИ + 15 по игровой Y;
//             8  `objCopyCharTransformOrAbort`   — полная матрица персонажа (высота = soundPos);
//             9  `effectSyncPosToOwnerEntity`    — позиция сущности (её «звуковая» точка);
//             12 `effectUpdateEntityAttachPos`   — позиция сущности + 25/45/60 по размерному классу;
//             13 `effectPlaceAboveEntity`        — над сущностью.
//   bone    — имя кости эталона («4», «M_DUMMY»); пусто = кость не используется.
//             ★ Гендерная развилка эталона `gender == 1 ? "M_DUMMY" : "G_DUMMY"` в порт не едет:
//               наш скелет унисекс, экспортёр ремапит G_DUMMY→M_DUMMY (§H5 wings_soulmate_re.md).
//   slot    — 0 = хэндл ложится в `rec+24` (fx0), 1 = в `rec+28` (fx1), −1 = НЕ сохраняется.
//             ★ ЭТО И ЕСТЬ ПРИЗНАК ДОЛГОЖИВУЩЕГО ЭФФЕКТА: только сохранённое можно погасить при
//               снятии состояния; несохранённое — разовая вспышка, догорает сама.
//   loop    — значение поля эффекта `+170` (0/1/3; −1 = обработчик его не трогает).
//   variant — размерный класс ЦЕЛИ (`сущность+7356`): −1 = ветки нет, 0/1/2 = только этот класс.
//   owner   — на кого вешать: `self` — обладатель состояния, `target` — его текущая цель,
//             `caster` — тот, кто наложил (только `Effect_SpawnCounterOnChar@0xF695B0` и
//             `Effect_SpawnDridg5OnChar@0xF6F280`).
//
// Источник строк: `Tools/asda_dump_buff_fx.py` → `Content/AsdaAssets/Data/BuffFx.csv`
//   (импорт — `Tools/asda_build_buff_fx_dt.py`).
#pragma once

#include "CoreMinimal.h"
#include "Engine/DataTable.h"
#include "AsdaBuffFxRow.generated.h"

USTRUCT(BlueprintType)
struct FAsdaBuffFxRow : public FTableRowBase
{
	GENERATED_BODY()

	// id состояния 0..349 (он же ключ строки).
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Asda|BuffFx") int32 Id = -1;
	// Имя состояния из `skill_info.sif` (для логов и консольной проверки).
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Asda|BuffFx") FString BuffName;
	// Имя обработчика эталона у игрока / у моба (чтобы по логу можно было найти ветку в эталоне).
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Asda|BuffFx") FString PlayerFn;
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Asda|BuffFx") FString MobFn;
	// Программы показа (см. раскладку в шапке файла). Пусто = у оригинала визуала нет.
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Asda|BuffFx") FString PlayerFx;
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Asda|BuffFx") FString MobFx;
	// Звук наложения состояния (`Audio_Play3DSound` в теле обработчика).
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Asda|BuffFx") FString Wav;
	// Навыки, накладывающие это состояние («код/слот», через `;») — справочно для проверки.
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Asda|BuffFx") FString Skills;
};

// Одна разобранная запись программы показа (строка `PlayerFx`/`MobFx` между `;`).
USTRUCT()
struct FAsdaBuffFxSpawn
{
	GENERATED_BODY()

	UPROPERTY() FString Jeb;          // базовое имя .jeb (без расширения)
	UPROPERTY() int32 Attach = 0;     // режим привязки эталона (3/5/6/7/8/9/12/13)
	UPROPERTY() FName Bone;           // кость эталона либо NAME_None
	UPROPERTY() int32 Slot = -1;      // 0 = fx0 (rec+24), 1 = fx1 (rec+28), −1 = не хранится
	UPROPERTY() int32 Loop = -1;      // поле эффекта +170
	UPROPERTY() int32 Variant = -1;   // размерный класс цели (−1 = ветки нет)
	UPROPERTY() FName Owner;          // self / target / caster
};
