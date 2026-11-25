// Copyright (c) 2026 @sitxovski (Discord)
// FAsdaSkillClipRow — строка таблицы «НАВЫК → КЛИП ПРИМЕНЕНИЯ → COMBO0 → ЭФФЕКТЫ/ЗВУКИ».
//
// ИСТОЧНИК (всё снято с бинаря/данных, ничего не выдумано; закон never-invent-only-original):
//   · `CharSkillEffect_InitDispatchTable @0xA89510` — 7 массивов колбэков CSkillMgr по 1355 записей,
//     индекс = КОД НАВЫКА; элемент i массива k лежит по `8 + 5420*k + 4*i`
//     (skillCallback0..6 @ 0x8/0x1534/0x2A60/0x3F8C/0x54B8/0x69E4/0x7F10, `structs_ru.h`).
//     Массив 0 = ЗАГРУЗЧИК (диспетчер `packPathDispatchEntryHandler@0xA81000`, `v14[skillId + 2]`),
//     массив 5 = ТИК фаз (`sub_A86660@0xA86660`, `this[skillId + 6777]`).
//   · тело каждого загрузчика в эталонном декомпиле: `String_Append(&path, "<клип>.JBB")` +
//     `CComboTable_FindByName(mgr, "<клип>")` + ветвление по полу и `equippedWeaponType`.
//   · `combo time.bin` (заголовок 20 б, 308 записей × 156 б; +8 имя клипа, +48 полная длительность
//     фазы, +84 КАДРЫ УДАРА) — все значения ×1000/30 ⇒ МИЛЛИСЕКУНДЫ.
//     COMBO0 = момент первого удара = МАКСИМУМ полосы применения (`skill_use_system.md` §1.6-1.7).
//   · `skill_info.sif` (запись 273 б, индекс = код навыка, +0x02 название).
//   · `system\SkillInfo.inl` — ВТОРОЙ источник клипа. 38 навыков (611-616, 632-639, 820, 823-825,
//     841-847, 1041-1046, 1062-1068) зарегистрированы на ОДИН общий загрузчик
//     `charLoadSkillEffectMotion@0xA75590`, у которого имени клипа в теле нет: он читает его из
//     записи .inl по `EffectSlotByType[64*(gender-1) + 2883]` (`entity_char.c:29389`). Раскладка
//     записи (12056 б = 0x2F18) разобрана по `CSkillMgr_LoadSkillInfoInl` (`skill_buff.c:16405`):
//     +11532 MALE_ANI_NAME (dword 2883), +11788 FEMALE_ANI_NAME (2883+64), +12044 COMBO0 (dword 3011).
//     COMBO0..2 в .inl — УЖЕ миллисекунды (в слот кладутся `(float)(int)` без пересчёта), поэтому
//     колонка Combo0Ms однородна с веткой `combo time.bin`, где кадры переводятся в мс ещё при
//     загрузке (`CComboManager_LoadComboTime@0x9BC5F0` ×33.333328).
//
// ЧЕГО В ТАБЛИЦЕ НЕТ: навык 400 («Нет» в `skill_info.sif`) — слот-заглушка оригинала: на его индекс
//   приходится 122 из 1349 регистраций (117 из них мёртвые перезаписи), выжившие функции случайны.
//
// Готовит CSV: `Tools/asda_dump_skill_dispatch.py`; собирает DataTable: `Tools/asda_build_skill_clip_dt.py`.
// Отчёт: `Reports/skill_fx_anim.md`, сырые таблицы `Reports/skill_clip_table.tsv`.
#pragma once

#include "CoreMinimal.h"
#include "Engine/DataTable.h"
#include "AsdaSkillClipRow.generated.h"

USTRUCT(BlueprintType)
struct ASDAREBORN_API FAsdaSkillClipRow : public FTableRowBase
{
	GENERATED_BODY()

	// Код навыка (индекс в массивах колбэков CSkillMgr, он же id записи skill_info.sif).
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Asda|Skill") int32 SkillId = 0;
	// Название навыка из skill_info.sif +0x02 (cp1251). «Нет» — так в данных оригинала.
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Asda|Skill") FString SkillName;
	// Имя функции-загрузчика из массива 0 (для сверки с эталоном и диагностики).
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Asda|Skill") FString Loader;
	// Имя файла клипа как в коде (`ch4_m_01_dswing_00.jbb`) — грузится из Data\Character\Motion\.
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Asda|Skill") FString Clip;
	// Пол ветки: "m" / "f" (определяется самим именем клипа — отдельного условия не требует).
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Asda|Skill") FString Gender;
	// Индекс ассета ch1_{g}_cremmy_previewanim_NN (Tools/export_char_skillclips.py). −1 = не импортирован.
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Asda|Skill") int32 AnimIdx = -1;
	// Подтипы оружия, при которых берётся эта ветка: «8|10», «5», «*» = default. Пусто = ветка не по оружию.
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Asda|Skill") FString WeaponTypes;
	// Ключ поиска в combo time.bin (`CComboTable_FindByName`) — обычно имя клипа без расширения.
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Asda|Skill") FString ComboKey;
	// СЫРОЕ условие ветки из эталонного декомпила (для веток НЕ по оружию: размер цели и т.п.).
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Asda|Skill") FString Cond;
	// Селектор switch эталона (обычно `pChar->equippedWeaponType`).
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Asda|Skill") FString Selector;
	// Кадр 1-го удара, мс = МАКСИМУМ полосы применения (combo time.bin +84[0] × 1000/30).
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Asda|Skill") float Combo0Ms = 0.f;
	// Кадры 2-го и 3-го ударов, мс (0 = фазы нет).
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Asda|Skill") float Combo1Ms = 0.f;
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Asda|Skill") float Combo2Ms = 0.f;
	// Полная длительность фазы 0, мс (combo time.bin +48[0] × 1000/30).
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Asda|Skill") float PhaseDurMs = 0.f;
	// Звуки ветки (имена .wav из тела загрузчика), через «;».
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Asda|Skill") FString Sounds;
	// Эффекты ветки (имена .jeb), через «;». ★ ПОРЯДОК ЗНАЧАЩИЙ — он же порядок присваивания в
	// эталоне и соответствует колонке JebSlots позиция-в-позицию. Сортировать нельзя.
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Asda|Skill") FString Jebs;
	// ★ Куда эталон кладёт каждый .jeb — параллельно колонке Jebs, через «;».
	//   · «0x68 / 0x168 / 0x268 / 0x368» — путевое поле СЛОТА ЭФФЕКТА (4 CPackPath по 256 б).
	//     Пример 501 (`effects_particles.c:18234`): 0x68=hblowb02, 0x168=hblowe01, 0x268=hblowc01.
	//     Каждое поле читает СВОЙ тик: `charFx_autoSkill501Tick@0xAB54B0` спавнит из 0x168 на старте,
	//     `effects_particles.c:6916` — из 0x268.
	//   · «inlN» — номер подсекции `[id-N]` в `SkillInfo.inl` (ветка .inl).
	//     `Char_UpdateSkillAnim_MultiPhase@0xA76E70`: подсекции 0/1/9 спавнятся на старте
	//     (`CharFx_SpawnEffectFromDesc(mgr, 0|1|9, …)`), 3/5/7 — пороги фаз COMBO0/1/2.
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Asda|Skill") FString JebSlots;
	// Откуда взята строка: «хардкод-загрузчик» или «SkillInfo.inl».
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Asda|Skill") FString Source;

	// ═══ ★★★ ПРОГРАММА ПОКАЗА: ЧТО ИГРАЕТ НА СТАРТЕ, А ЧТО В МОМЕНТ УДАРА ═══════════════════════
	// Загрузчик (массив 0) НИЧЕГО не спавнит — он раскладывает имена по полям слота. Спавнит ТИК
	// (массив 5) и его помощники. Колонки ниже снимает `analyse_tick` в
	// `Tools/asda_dump_skill_dispatch.py` МЕХАНИЧЕСКИ из эталонного декомпила.
	//
	// РОЛИ ПОЛЕЙ СЛОТА (подтверждены на трёх независимых функциях эталона):
	//   `SkillEffect_UpdateScaledStrikeHit@0xA72610` — первый кадр: `слот+0x268` через
	//     `Effect_SpawnByName(..., boneIdx, attach 7)` по кости `8_a_End` = ЭФФЕКТ КАСТА НА ОРУЖИИ;
	//     порог `slot.color <= прогресс` (color = COMBO0): цель-МОБ → `+0x168` в позиции цели с
	//     `CEffectMgr_scaleAll(fx, масштабЦели/100)`, цель-ИГРОК → `+0x68`; звук — `+0x598`, канал 3.
	//   `CSkillCast_ApplyEntityHitEffect@0xA7B4A0` / `CSkillCast_ApplySingleTargetHit@0xA7AF20` —
	//     фазовый вариант: `effectSlots[112].field_3A0` = НОМЕР ФАЗЫ, фаза 0 → `+0x68`,
	//     фаза 1 → `+0x168`, фаза ≥2 → ранний return (эффекта нет).
	//   ⚠ Правило НЕ универсально (часть тиков переиспользует `+0x168` ещё и как визуал старта),
	//     поэтому роль берётся из тела КОНКРЕТНОГО тика, а не назначается по номеру поля.
	//
	// Формат ячейки: `имя.jeb@привязка[:источник][/видЦели]`, несколько — через «;».
	//   привязка (КАК держится): `bone` (кость оружия, attach 7) · `follow` (следует за персонажем,
	//             attach 6) · `char` (позиция персонажа) · `world` (свободный в мировой точке) ·
	//             `matrix` (полной мировой матрицей) · `attach` (прикреплён к цели, attach 13) ·
	//             `projectile` (ЛЕТЯЩИЙ НОСИТЕЛЬ)
	//   ★★★ источник (ГДЕ РОЖДАЕТСЯ) — `caster` · `target` · `bone` (кость оружия кастера) ·
	//             `hitpoint` (точка луча «кастер→цель» × AABB цели). Снимается МЕХАНИЧЕСКИ из
	//             аргументов позиции вызова спавна (`_origin_near`), а НЕ выводится из фазы.
	//     ⚠ ЗАЧЕМ ОТДЕЛЬНАЯ ОСЬ. Привязка на вопрос «где рождается» не отвечает:
	//       `CEffectMgr_CreateEffect_F561F0` одинаково зовут и с позицией цели
	//       (`pTargetChar->soundPos*`), и с позицией кастера (`pChar->soundPos*`), и с точкой на
	//       кости (`ItemByIndex[12..14]`). Порт раньше выводил точку из КОНТЕКСТА («это фаза удара
	//       ⇒ у цели») — и эффекты, которые эталон рождает У ПЕРСОНАЖА в момент удара (навык 905:
	//       `effectMgrSpawnJEB(gap_368, МИРОВАЯ МАТРИЦА КАСТЕРА)`), уезжали к цели, а эффекты у
	//       цели — к персонажу. Это и есть живой симптом «эффекты не на цели, а на игроке».
	//     Пусто = источник механически не определился — рантайм пишет предупреждение и берёт
	//       ЗАПАСНОЙ вариант (удар → цель, каст → кастер), закон dead-code-markings-suspect.
	//   видЦели: `mob` (сущность) · `char` (игрок) · пусто (ветка общая)
	// Имя тик-функции массива 5 (для сверки с эталоном и диагностики).
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Asda|SkillFx") FString TickFn;
	// Эффекты ПЕРВОГО КАДРА показа.
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Asda|SkillFx") FString FxCast;
	// Кость привязки эффекта каста из тика («8_a_End» / «8_b1»); пусто = привязки к кости нет.
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Asda|SkillFx") FString FxCastBone;
	// Эффекты УДАРА фаз 0/1/2 — играют при `COMBO<N> <= прогресс клипа`.
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Asda|SkillFx") FString FxHit0;
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Asda|SkillFx") FString FxHit1;
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Asda|SkillFx") FString FxHit2;
	// Звук каста (поле `pEffectSoundObj`) и звук удара (поле `+0x598`).
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Asda|SkillFx") FString SndCast;
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Asda|SkillFx") FString SndHit;
	// 1 = тик заводит НОСИТЕЛЬ СНАРЯДА (`effect_spawnDirectedFromTo@0xA59B30`): старт с кости
	//   оружия, скорость `dist/2.5` (2.5 юнита/мс), урон и эффект удара едут В СНАРЯДЕ и
	//   показываются ПО ПРИЛЁТЕ (`CEffect_UpdateSkillCast@0xA5A060`).
	//   ★★★ ГЕЙТ — ДАННЫЕ, А НЕ ТЕЛО ТИКА (исправлено 2026-08-10). Прежде `Proj` ставился по одному
	//   лишь наличию вызова в теле тика, а `Char_UpdateSkillAnim_MultiPhase@0xA76E70` — ОДНА функция
	//   на все 38 навыков ветки `SkillInfo.inl`: признак вставал у 90 строк вместо 12. В самой
	//   функции ветка снаряда закрыта проверкой `if (LOBYTE(skillData[524*phaseSlot + 2]))` =
	//   «у подсекции 3/5/7 есть FILE_NAME», и в данных она есть ровно у ОДНОГО навыка ветки — 820.
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Asda|SkillFx") int32 Proj = 0;
	// ★ 1 = тик заводит БЕСТЕЛЕСНЫЙ носитель `effectInitCollisionTrace@0xA596F0`: он заполняет ТОЛЬКО
	//   трассер (`+1600`), id цели (`+1780`) и id эффекта урона (`+1784`); ни `.jeb` в `+520`, ни
	//   имени удара в `+8`, ни звука в `+264`, ни флага `+1804` — визуала полёта у него НЕТ ПО
	//   УСТРОЙСТВУ. Это ЧИСТАЯ ЗАДЕРЖКА урона на время `dist/2.5`. Навыки 905 «Огненный шар» и
	//   915: их видимый эффект спавнит сам тик в момент запуска, а урон приходит с задержкой.
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Asda|SkillFx") int32 ProjDelay = 0;
	// ★★★ НЕ-ОРУЖЕЙНОЕ УСЛОВИЕ ВЕТКИ — РАЗМЕРНЫЙ КЛАСС ЦЕЛИ (`сущность + 7356`), «0»/«1»/«2».
	//   `SkillEffect_SetupHexa@0xAA4A40` (навык 513): 0 → `sq-hexa-s_02` + `hexaa07`,
	//   1 → `-m_02` + `hexaa08`, 2 (иначе) → `-l_02` + `hexaa09`. Пусто = ветка от размера не зависит.
	//   ЧТО ЭТО ЗА ПОЛЕ — снято по двум другим его читателям: высота эффекта над целью
	//   (`effects_particles.c:59656`: 0→+25, 1→+45, 2→+60) и рамка панели цели
	//   (`ui_core.c:3615`). ⚠ ЗАПИСИ в `+7356` в срезе НЕТ ни в эталоне, ни в мастер-декомпиле —
	//   формулировка по закону dead-code-markings-suspect: «ссылок на запись в срезе нет».
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Asda|SkillFx") FString TargetSize;
	// ★ ЧЕСТНАЯ ПОМЕТКА: что в этой строке механически НЕ разобралось. Пример: «поле 0x268 —
	//   читателей в теле тика нет» (закон dead-code-markings-suspect: не «мёртвый», а «ссылок в
	//   срезе нет»). Подсистема печатает это в логе показа, а не проглатывает.
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Asda|SkillFx") FString FxNote;
};
