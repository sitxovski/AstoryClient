// Copyright (c) 2026 @sitxovski (Discord)
// AsdaMobStatsRow.h — строка DataTable `DT_MobStats`: ЭТАЛОННЫЕ ЧИСЛОВЫЕ ПОЛЯ ТИПА МОБА
//   из `Data\Mob\Mob list.BIN` (заголовок 0x14, запись 0x110 = 272 байта, RowName = MobId−1).
//
// ★ ОТКУДА КАЖДОЕ ПОЛЕ (реверс 2026-08-03, дизасм аксессоров CMobMgr + декомпил спавна):
//   Все аксессоры имеют одну форму `rec = *(mgr+0xC) + typeIdx*0x110; return *(rec + OFF);`
//     • sub_930AC5 → 0xF31080 : OFF 0x84, копирует ТРИ float  — ПОЛУ-РАЗМЕР AABB (halfX, halfY, halfZ)
//     • sub_932483 → 0xF31040 : OFF 0x94, int               — MaxHp (сущность +1140)
//     • sub_91F789 → 0xF83400 : OFF 0x98, int               — УРОВЕНЬ (кладётся в сущность +0x1DC=476)
//     • sub_933400 → 0xF83440 : OFF 0x9C, int               — экспа/награда (сущность self[1].gap_C[84])
//     • sub_93DA7C → 0xF833C0 : OFF 0x104, int              — радиус звука (fadeStart = (v·100)²)
//   Байты, которые Recv_MobSpawn@0xF305E0 переносит в сущность (network.c:37178-37181):
//     • +0xF8 → сущность +6978 : РАНГ/иконка (0..5, шесть контролов панели цели)
//     • +0xFC → сущность +6979 : АГРЕССИЯ — ветка ЦВЕТА ИМЕНИ (1/4/5 = по разнице уровней,
//                                2 и 3 = фиксированные цвета; см. FAsdaMobNameColorRow)
//     • +0x100 → сущность +6977: признак «атакует первым» (контрол BAT_FIRST панели цели)
//     • +0x10C → сущность +18700: число «жизней» (сегментов) полосы HP у боссов
//
// ★ ВЫСОТА ПЛАШКИ ИМЕНИ (закон [[nameplate-height-from-data]] — константа из ДАННЫХ, не габариты меша):
//   спавн строит AABB: half = (+0x84), центр = мировая позиция, затем `центр.Y += half.Y`;
//   плашка садится на ВЕРХ AABB (`helper_getSumF` = центр.Y + half.Y = земля + 2·halfY)
//   и поднимается ещё на +50.0 (game_main.c:4541 / 4750). ⇒ H = 2·HalfY + 50.
//
// Импортёр: Tools/asda_build_mob_stats_dt.py. Применение: AAsdaMobActor::ResolveMobStats().
#pragma once

#include "CoreMinimal.h"
#include "Engine/DataTable.h"
#include "AsdaMobStatsRow.generated.h"

USTRUCT(BlueprintType)
struct FAsdaMobStatsRow : public FTableRowBase
{
	GENERATED_BODY()

	// Имя модели `.jbm` типа (Mob list.BIN +0x04) — гард применения к чужому мешу.
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Asda|MobStats") FString Model;

	// Читаемое имя типа (+0x64, cp1251) — для сверки строк в редакторе.
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Asda|MobStats") FString Name;

	// Полу-размер AABB (+0x84, три float). HalfY — ЕДИНСТВЕННЫЙ источник высоты плашки.
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Asda|MobStats") float HalfX = 0.f;
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Asda|MobStats") float HalfY = 0.f;
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Asda|MobStats") float HalfZ = 0.f;

	// MaxHp (+0x94) и УРОВЕНЬ (+0x98) типа — уровень нужен для цвета имени по разнице уровней.
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Asda|MobStats") int32 MaxHp = 0;
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Asda|MobStats") int32 Level = 0;

	// Опыт/награда (+0x9C) и радиус звука (+0x104) — переносим ради полноты записи.
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Asda|MobStats") int32 Exp = 0;
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Asda|MobStats") int32 SoundRange = 0;

	// ★★★ РАЗМЕРНЫЙ КЛАСС ТИПА (+0x90, int) — сущность **+7356**. ДОКАЗАНО ПО БИНАРЮ 2026-08-11:
	//   `mobEffect_LoadArchiveStep@0xF81B90+0xEEF` (VA `0xF82A7F`) делает
	//     `mov ecx,[entity+0x1D4]; mov ecx, g_MobList(0x0349FAE8); call 0x9246BC → jmp 0xF82F10;
	//      mov [entity+0x1CBC], eax`,
	//   а `sub_F82F10` (`entity_char.c:68677`) = `return *(*(this+3) + 272·typeIdx + 144)` = **+0x90**.
	//   Второй писатель — конструктор `gameObjCtorInit@0xF810B0+0x280` (VA `0xF81330`,
	//   `mov dword [eax+0x1CBC], 0`), т.е. у НЕ-мобов класс всегда 0.
	//   Читатели поля: выбор ветки Hexa (`eff_1_char_01_hexaa08/hexah02…`, `effects_particles.c:19676`),
	//   вариант `-a_00/01/02` и высота привязок 12/13 (+25/+45/+60 — `effects_particles.c:59656-59668`).
	//   Распределение по 854 записям: {0: 646, 1: 193, 2: 15}.
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Asda|MobStats") int32 SizeClass = 0;

	// ★★★ ПРОЦЕНТНЫЙ РАЗМЕР ТИПА (+0x108, int) — сущность **+7360**, геттер `get_o730@0x99F150`
	//   (`entity_char.c:495`: `return *(this + 1840)`), он же `sub_936A29`.
	//   Писатель: `mobEffect_LoadArchiveStep@0xF81B90+0xF0C` (VA `0xF82A9C`,
	//   `call 0x91FDA1 → jmp 0xF82F50; mov [entity+0x1CC0], eax`), где
	//   `sub_F82F50` = `*(*(this+3) + 272·typeIdx + 264)` = **+0x108**;
	//   у НЕ-мобов конструктор `gameObjCtorInit+0x28D` (VA `0xF8133D`) кладёт **100** ⇒ множитель 1.0.
	//   Применение: `SkillEffect_UpdateScaledStrikeHit@0xA72610` (`effects_particles.c:10167`)
	//   `CEffectMgr_scaleAll(fx+12, sub_936A29(цель) / 100.0)` — МАСШТАБ ЭФФЕКТА УДАРА.
	//   ⚠ Значения 100 в данных НЕТ НИ У ОДНОГО моба: {50:6, 80:4, 120:116, 150:105, 180:108,
	//   200:270, 220:165, 230:24, 250:34, 300:11, 350:2, 500:9} ⇒ множитель 0.5…5.0.
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Asda|MobStats") int32 SizePercent = 100;

	// ★★★ ДВА МИСНЕЙМА, ВСКРЫТЫЕ ПРИ ПОРТЕ ПАНЕЛИ ЦЕЛИ (2026-08-24, закон false-negatives-method).
	//   Имена колонок оставлены как есть (переименование сломало бы уже сгенерированный
	//   `DT_MobStats` и всех его читателей), но ЧИТАТЬ их надо так:
	//
	//   `Rank`       = +0xF8 → сущность **+6978** — это НЕ ранг, а ИНДЕКС СТИХИИ 0..5 для меток
	//        `PVP_MARK00..05` панели цели (Огонь/Вода/Земля/Ветер/Свет/Тьма). Доказательство —
	//        `hudShowMobTargetInfo` (ui_core.c:75756-75757): `if (ent[6978] < 6u)
	//        SetVisible(pPvpMark0[ent[6978]], 1)`. Распределение по 854 записям Mob list.BIN:
	//        {0:194, 1:99, 2:390, 3:55, 5:116} — все значения < 6 (Света, индекс 4, в данных нет).
	//
	//   `Aggression` = +0xFC → сущность **+6979** — это и есть РАНГ (он же «уровень агрессии»),
	//        по нему идёт `switch` панели цели: 1 и 5 → кольцо Normal, 2 → Named, 3 → Boss,
	//        4 → Elite; он же выбирает ветку цвета имени. Распределение {1:629, 2:8, 3:87, 4:130}.
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Asda|MobStats") int32 Rank = 0;
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Asda|MobStats") int32 Aggression = 0;
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Asda|MobStats") int32 AttackFirst = 0;
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Asda|MobStats") int32 Lives = 0;

	// ── ★ ПОРТРЕТ ТИПА для панели цели `PVP_IMAGE` (эталон hudShowMobTargetInfo, ui_core.c:75762-75769) ──
	//   `texPosY = g_pMobInfoTable + 272*typeIdx;`
	//   `texPosX = sub_935651(*(rec + 244), &texPosYOut);`            // 244 = 0xF4 — НОМЕР ЯЧЕЙКИ
	//   `CControl_DrawTextureRect(pPvpImage, TexturePath_Format(rec + 212), X, Y, 46, 46);` // 212 = 0xD4 — ИМЯ АТЛАСА
	//   `sub_935651` — тонк на `gridIndexToXY_11col@0xEBFFE0`: X = 46·(i%11), Y = 46·(i/11).
	//   В данных всего три атласа — mon_icon_01 (594 типа), mon_icon_02 (240), mon_icon_03 (20);
	//   номера ячеек 0..120. Все три текстуры уже лежат в `/Game/AsdaAssets/Textures/JUI`.
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Asda|MobStats") FString PortraitTex;
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Asda|MobStats") int32 PortraitCell = 0;
};
