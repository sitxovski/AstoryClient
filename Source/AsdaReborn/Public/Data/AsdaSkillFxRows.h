// Copyright (c) 2026 @sitxovski (Discord)
// AsdaSkillFxRows.h — строка DataTable «СОБСТВЕННЫЙ ЭФФЕКТ СКИЛЛА» (второй слой удара хранителя).
//
// ═════════════════════════════ ОТКУДА РАСКЛАДКА (не догадки) ═══════════════════════════════════
// Источник — `Data\character\Bin.bbz → skill_info.sif` (копия в
//   `Content/AsdaAssets/Data/Character/skill_info.sif`). Загрузчик эталона
//   `CSkillMgr_LoadSkillData@0xA79F30`: `skillCount = размер / 0x111`, запись **273 б (0x111)**,
//   ИНДЕКС = id скилла (прямой, без базы). Доступ `CSkillMgr_GetSkillRecord@0xA86F90` (граница 1255).
//   Полная карта полей записи — `RE/ida/skillrecord_fields.tsv`.
//
// В записи ДВА строковых буфера по 39 б (проверено сканом всех 1255 записей — стартов строк ровно два):
//   +0x02  название скилла   («Bashing hit», у большинства служебных — «none»);
//   +0xD1  ИМЯ .jeb-ЭФФЕКТА («Sb_md_Eff_Oh_h_01_00») либо «-1» = эффекта нет.
// Скан всего файла: непустых (≠ «-1») ровно **27** — это активные скиллы хранителя души
//   (id 1228..1254, семейства `oh`/`cb`/`am`, буква **h = hit**). Больше НИ ОДИН скилл игры
//   собственного .jeb здесь не хранит.
//
// ★ «ССЫЛОК В СРЕЗЕ НЕТ» (закон dead-code-markings-suspect — пишем факт, а не «мёртвый код»):
//   ЧИТАТЕЛЯ поля +0xD1 в клиенте пользователя (сборка IDB 13 344 768) найти не удалось. Проверено
//   тремя независимыми способами:
//     1) карта полей `SkillRecord` (`RE/ida/skillrecord_fields.tsv`): выше +205 чтений не найдено;
//     2) grep по ПОЛНОМУ экспорту `RE/ida/export/AsdaStory_game.c` — обращений `+ 209` к таблице
//        скиллов нет (только d3dx/чужие структуры);
//     3) все 8 мест сборки имени `"%s.jeb"` в эталоне перечислены (крылья, заточка, мотион
//        хранителя, мобы) — ни одно не берёт строку из записи скилла.
//   Т.е. в этой сборке данные ЕСТЬ, а спавна НЕТ. Порт спавнит этот слой сам
//   (`AAsdaSoulGuardianActor`, cvar `asda.SoulFx.HitFx 0` — выключить), закон fix-original-defects.
//
// ★ ПОЧЕМУ ТАБЛИЦА, А НЕ ФОРМУЛА ПО ИНДЕКСУ. Соблазн вывести имя из `SoulGuardIndex`
//   (`fam[i/9]_h_%02d_%02d` от `(i%9)/3+1` и `i%3`) даёт ВЕРНЫЙ ответ только для 21 записи из 27:
//   у семейства `am` идентификаторы типов **7 и 8 переставлены** (запись 18, SkillType 7 →
//   GuardianSkillId 1249 и `Am_h_02_00`; запись 21, SkillType 8 → 1246 и `Am_h_01_00`).
//   Поэтому имя берётся ТОЛЬКО из данных: `SoulGuarderTable.GuardianSkillId` → эта таблица.
#pragma once

#include "CoreMinimal.h"
#include "Engine/DataTable.h"
#include "AsdaSkillFxRows.generated.h"

// Одна запись «скилл → собственный .jeb». Ключ строки DataTable = SkillId (десятичное, как в файле).
USTRUCT(BlueprintType)
struct FAsdaSkillFxRow : public FTableRowBase
{
	GENERATED_BODY()

	// Индекс записи в skill_info.sif = id скилла (для хранителя — 1228..1254).
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Asda|SkillFx") int32 SkillId = -1;
	// Название скилла из +0x02 (справочно, для читаемости таблицы в редакторе).
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Asda|SkillFx") FString SkillName;
	// Имя .jeb из +0xD1, КАК В ФАЙЛЕ (регистр смешанный: «Sb_md_Eff_Oh_h_01_00»).
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Asda|SkillFx") FString HitFx;
};
