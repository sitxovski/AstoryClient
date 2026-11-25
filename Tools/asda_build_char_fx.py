# Copyright (c) 2026 @sitxovski (Discord)
# asda_build_char_fx.py — BP-заготовки ЭФФЕКТОВ НА ПЕРСОНАЖАХ: крылья Plus Package + соулмейт.
# ЭТАЛОН (Reports/wings_soulmate_re.md):
#   Крылья: CCharacter_UpdateWingEffect@0xA4BAA0 — jeb из PlusPackageTable (муж/жен), кость "M"/"G",
#     attach 10, scale 1.0. Состав: 0x12 jem-меш крыла + 0x17 частицы-блёстки (0x17 в парсере НЕ
#     реализован — эмиттер пропускается с логом, TODO доразбор T17).
#   Соулмейт: кольцо у ног eff_1_char_99_soullink01 (2×0xE+8×0xC), снаряд-сердечко soullink02,
#     импакт soullink03, церемония бонда soulmatec-a_01.
# Пайплайн — копия asda_build_combat_impacts.py. BP-имя = BP_Fx_<база jeb>.
# ★ Аудит №4 (волна 3): ФОРМА сборки BP (чтение .jeb, режим бленда, создание BP на
#   AsdaEffectActor, строгие оси костей, гейты окружения) переехала в `asda_fx_common` — общая
#   точка семи канальных сборщиков ([[port-shared-points]]). Здесь остались канальные ОТЛИЧИЯ.
import sys, os
import os as _os, sys as _sys
_sys.path.insert(0, _os.path.dirname(_os.path.abspath(__file__)))
import asda_paths as _P
import asda_fx_common as FXC
import asda_ue_paths as _U   # ★ волна 2 п. 7 (14.09.2026): корни и правила путей — реестр (Tools/data/asda_asset_paths.json)
try: sys.stdout.reconfigure(encoding="utf-8", errors="replace")
except Exception: pass
import unreal

SRC_DIR = _P.EFFECT_MODEL_ORIG_DIR
OUT_PKG = _U.G_EFFECTS
JEBS = [
    # ★ 07.11.2026 ЭФФЕКТЫ ЧАСТЕЙ .PEB (семейство 305, броня Lucifer и др.): `effLoadPebEffectSet@0x9EDA00`, список —
    #   все имена из 24 файлов .PEB CharModel.bnz; рантайм — Private/World/AsdaPartFx.cpp.
    "ch1_f_00_chst305_00_00_2_0", "ch1_f_00_chst305_00_00_2_1", "ch1_f_00_chst305_00_00_3_0",
    "ch1_f_00_chst305_00_00_3_1", "ch1_f_00_hcap305_00_00_1", "ch1_f_00_hcap305_00_00_2",
    "ch1_f_00_hcap305_00_00_3", "ch1_f_00_shoe305_00_00_1", "ch1_f_00_shoe305_00_00_2",
    "ch1_f_00_shoe305_00_00_3", "ch1_m_00_chst305_00_00_1", "ch1_m_00_chst305_00_00_2",
    "ch1_m_00_chst305_00_00_2_0", "ch1_m_00_chst305_00_00_2_1", "ch1_m_00_chst305_00_00_3",
    "ch1_m_00_chst305_00_00_3_0", "ch1_m_00_chst305_00_00_3_1", "ch1_m_00_grov305_00_00_01_0",
    "ch1_m_00_grov305_00_00_01_1", "ch1_m_00_grov305_00_00_02_0", "ch1_m_00_grov305_00_00_02_1",
    "ch1_m_00_grov305_00_00_03_0", "ch1_m_00_grov305_00_00_03_1", "ch1_m_00_hcap305_00_00_1",
    "ch1_m_00_hcap305_00_00_2", "ch1_m_00_hcap305_00_00_3", "ch1_m_00_shoe305_00_00_1",
    "ch1_m_00_shoe305_00_00_2", "ch1_m_00_shoe305_00_00_3",
    # Крылья (9 вариантов PlusPackage; на нашем сервере все записи → *_ss_00)
    "eff_wing_f_big_00", "eff_wing_f_mid_00", "eff_wing_f_ms_00", "eff_wing_f_ss_00",
    "eff_wing_m_big_00", "eff_wing_m_mid_00", "eff_wing_m_mid_01", "eff_wing_m_ms_00", "eff_wing_m_ss_00",
    # Соулмейт
    "eff_1_char_99_soullink01-a_00",   # кольцо у ног (постоянное, вращается)
    "eff_1_char_99_soullink02-a_00",   # снаряд-сердечко между парой
    "eff_1_char_99_soullink03-a_00",   # импакт снаряда
    "eff_1_char_99_soulmatec-a_01",    # церемония бонда (оба)
    # ★ СЛЕДЫ ШАГОВ (24.08.2026). Эталон `CCharacter_ApplyMoveState@0xA199E0` дважды за цикл бега
    #   (на фазах 300 и 600) смотрит МАТЕРИАЛ ПОВЕРХНОСТИ — байт 0 флага тайла `.TER` — и роняет
    #   след: по земле облачко пыли, по воде всплеск (последний только если точка действительно
    #   накрыта водной геометрией). Разбор: `Reports/alpen_dancefloor_trigger.md` §5.
    "eff_1_char_99_move01-a_00",       # пыль из-под ног (материал 0)
    "eff_1_char_99_move02-a_00",       # всплеск (материал 1, над водой)
    # ★ СНАРЯД дальнобойного оружия (projectile_re.md): шлейф летящей стрелы/болта.
    #   Эталон skillFireArrowProjectile@0xA52C00 / charFx_spawnFlyArrowProjectile@0xA52FB0 —
    #   CEffect_LoadJEB("eff_0_char_03_flyarr01-a_00.jeb") в слот снаряда (self+411); эффект живёт
    #   в трансформе снаряда. Состав: 2 кольца-конуса 0xE (rollY ±500°/с — встречная закрутка).
    "eff_0_char_03_flyarr01-a_00",
    # ★ ЭФФЕКТ ПОЯВЛЕНИЯ/ВОСКРЕШЕНИЯ (spawn_revive_effect.md): «столб света» при входе в мир и
    #   после ЛЮБОГО воскрешения. Эталон: CRenderer_RenderWorldFrame@0x9773B0 (первый кадр мира),
    #   GameNet_OnLocalPlayerMapEntry@0xF43390 (опкод 4056: self И чужие) + звук S_c_entry.wav.
    #   Состав: 4×0xB квады + 8×0xE кольца + 3×0x2 биллборд-вспышки + 0xD частицы (~4.3 с).
    "eff_0_char_99_cmchar01-a_00",
    # ★ ХРАНИТЕЛЬ (Soul Guardian / «ангел»), Reports/soul_guardian_companion_re.md §8.
    #   ВСЕ эффекты хранителя крепятся к кости ВЛАДЕЛЬЦА M_DUMMY (муж) / G_DUMMY (жен), scale 1.0
    #   (эталон CCharManager_AddOrUpdateObject@0xFA6B90 и Char_SetMotion@0xFA7560) — НЕ к компаньону.
    #   Имена на диске в НИЖНЕМ регистре; в SoulGuarderTable часть записей хранит их как
    #   «Sb_md_Eff_Oh_Sk_01_00» — рантайм приводит к нижнему регистру перед сборкой имени BP.
    "sb_change_01",                      # призыв хранителя И смена уровня (bil_1..3, cy_1/2, sp_1/2)
    # 27 эффектов активных скиллов: 3 семейства × 3 уровня × 3 варианта скилла (слоты 31/32/33).
    "sb_md_eff_oh_sk_01_00", "sb_md_eff_oh_sk_01_01", "sb_md_eff_oh_sk_01_02",
    "sb_md_eff_oh_sk_02_00", "sb_md_eff_oh_sk_02_01", "sb_md_eff_oh_sk_02_02",
    "sb_md_eff_oh_sk_03_00", "sb_md_eff_oh_sk_03_01", "sb_md_eff_oh_sk_03_02",
    "sb_md_eff_cb_sk_01_00", "sb_md_eff_cb_sk_01_01", "sb_md_eff_cb_sk_01_02",
    "sb_md_eff_cb_sk_02_00", "sb_md_eff_cb_sk_02_01", "sb_md_eff_cb_sk_02_02",
    "sb_md_eff_cb_sk_03_00", "sb_md_eff_cb_sk_03_01", "sb_md_eff_cb_sk_03_02",
    "sb_md_eff_am_sk_01_00", "sb_md_eff_am_sk_01_01", "sb_md_eff_am_sk_01_02",
    "sb_md_eff_am_sk_02_00", "sb_md_eff_am_sk_02_01", "sb_md_eff_am_sk_02_02",
    "sb_md_eff_am_sk_03_00", "sb_md_eff_am_sk_03_01", "sb_md_eff_am_sk_03_02",
    # ★ СЛОЙ 2 УДАРА — 27 СОБСТВЕННЫХ эффектов скиллов хранителя («h» = hit), 2026-08-08.
    #   Имя лежит в `skill_info.sif` +0xD1 (запись 273 б, индекс = id скилла; загрузчик
    #   CSkillMgr_LoadSkillData@0xA79F30). Непустых во всём файле ровно 27 — это активные скиллы
    #   хранителя 1228..1254. Связка «запись хранителя → скилл → эффект» берётся ИЗ ДАННЫХ
    #   (SoulGuarderTable.GuardianSkillId → DT_SkillFx, Tools/asda_build_skillfx_dt.py), а НЕ формулой:
    #   у семейства `am` идентификаторы SkillType 7 и 8 переставлены (запись 18 → скилл 1249 →
    #   Am_h_02_00, запись 21 → 1246 → Am_h_01_00) — формула по индексу врала бы на 6 записях из 27.
    #   ★ Читателя поля +0xD1 в клиенте пользователя НЕТ (см. шапку Public/Data/AsdaSkillFxRows.h) —
    #   слой спавнит порт (cvar asda.SoulFx.HitFx), ассеты нужны в любом случае.
    "sb_md_eff_oh_h_01_00", "sb_md_eff_oh_h_01_01", "sb_md_eff_oh_h_01_02",
    "sb_md_eff_oh_h_02_00", "sb_md_eff_oh_h_02_01", "sb_md_eff_oh_h_02_02",
    "sb_md_eff_oh_h_03_00", "sb_md_eff_oh_h_03_01", "sb_md_eff_oh_h_03_02",
    "sb_md_eff_cb_h_01_00", "sb_md_eff_cb_h_01_01", "sb_md_eff_cb_h_01_02",
    "sb_md_eff_cb_h_02_00", "sb_md_eff_cb_h_02_01", "sb_md_eff_cb_h_02_02",
    "sb_md_eff_cb_h_03_00", "sb_md_eff_cb_h_03_01", "sb_md_eff_cb_h_03_02",
    "sb_md_eff_am_h_01_00", "sb_md_eff_am_h_01_01", "sb_md_eff_am_h_01_02",
    "sb_md_eff_am_h_02_00", "sb_md_eff_am_h_02_01", "sb_md_eff_am_h_02_02",
    "sb_md_eff_am_h_03_00", "sb_md_eff_am_h_03_01", "sb_md_eff_am_h_03_02",
    # ★★★ ЭФФЕКТЫ ПРИМЕНЕНИЯ ПРЕДМЕТА (12 шт, 2026-08-13; отчёты `Reports/item_use_wave1.md` §«три BP»
    #   и `item_use_wave2.md` §7.2, сводка `Reports/item_use_fx_build.md`).
    #   ПОЧЕМУ ИХ ЗДЕСЬ НЕ БЫЛО: в оригинале имена .jeb этого канала зашиты В КОДЕ (а не в данных), поэтому
    #   они не попали ни в один список-источник — ни в `BuffFx.csv` (эффекты СОСТОЯНИЙ, asda_build_buff_fx.py),
    #   ни в DT скиллов/крыльев. Единственное место, где список канала может жить, — этот JEBS.
    #   Все 12 крепятся к персонажу и живут одну проходку ⇒ канал ровно этого сборщика (эффекты НА персонаже).
    #   Имя ассета сборщик строит как `BP_Fx_<jeb с '-'→'_'>` — ровно то же преобразование делает
    #   `AAsdaEffectActor::SpawnByJebName` (`JebBase.Replace("-","_")`), поэтому ДЕФИС В ИМЕНАХ ЗЕЛИЙ
    #   СОХРАНЯЕМ КАК НА ДИСКЕ: файл `eff_0_othr_99_hpptn01-a_00.jeb` → ассет `BP_Fx_eff_0_othr_99_hpptn01_a_00`.
    # (а) Зелья — `SpawnItemUseFx`, linkCode 7/8/…; звук id 95/96, привязка к позиции персонажа (CharPos).
    #   Состав каждого: 5×0xD трек-частицы + 0xC биллборд + 0x1 квад + 0x2 вспышка, аддитив (диск 2/1).
    "eff_0_othr_99_hpptn01-a_00",      # мгновенное восстановление HP (звук 95, S_c_hposion)
    "eff_0_othr_99_mpptn01-a_00",      # мгновенное восстановление MP (звук 96, S_c_mposion)
    "eff_0_othr_99_elptn01-a_00",      # восстановление выносливости/EL (звука в эталоне нет)
    # (б) Гача-бустер — `SpawnBoostFx`/5473, кость `M_DUMMY`/`G_DUMMY`, attach 10.
    #   Состав: 0x1F меш-эмиттер (.jem того же имени) + 0xC биллборды + 0xD частицы; у «редкого» ещё 0x17.
    "eff_lucky_a_00",                  # обычный приз (звук s_item_boostnormal, нотис 20066)
    "eff_lucky_a_01",                  # редкий приз  (звук s_item_boostrare,   нотис 20067)
    # (в) Премиум-усиления — `SpawnPmBuffFx`, семь классов ПО ПОДТИПУ (таблица в коде эталона
    #   `charBuff_ApplyPMBuffFromPacket@0xF26110`; в `special_itemlist` имён .jeb нет вовсе). Звука нет.
    #   ⚠ Регистр: в эталоне строка `…_PMbuff_…`, на диске файл в нижнем регистре — берём как на диске.
    "eff_0_char_99_pmbuff_a_00",       # подтип 7    (CharEffect_SpawnPMBuff_00@0xF2C2F0)
    "eff_0_char_99_pmbuff_a_01",       # подтип 6    (CharEffect_SpawnPMBuff_01@0xF2C020)
    "eff_0_char_99_pmbuff_a_02",       # подтип 2    (CharEffect_SpawnPMBuff_02@0xF2CB60)
    "eff_0_char_99_pmbuff_a_03",       # подтип 1    (CharEffect_SpawnPMBuff_03@0xF2C5C0)
    "eff_0_char_99_pmbuff_a_04",       # подтип 3    (CharEffect_SpawnPMBuff_04@0xF2C890)
    "eff_0_char_99_pmbuff_a_05",       # подтип 0x10 (CharBuff_SpawnPMbuff05@0xF2CE30)
    "eff_0_char_99_pmbuff_a_06",       # подтип 0x11 (CharBuff_SpawnPMbuff06@0xF2D100)
    # (г) ★★★ ВТОРОЙ ДИСПЕТЧЕР ПРЕМИУМ-ПРЕДМЕТОВ (добавлено 2026-08-13 по живому прогону).
    #   Опкод 5451 обслуживает `netRecvItemBuffApply@0xF23C60` — там `switch (subType − 1)` на 32 кейса,
    #   а не 7. Эти .jeb вызываются оттуда и раньше не собирались вовсе, поэтому у премиум-предметов
    #   подтипов 25/41…52 эффекта не было (дефект «эффекта применения премиум зелий не вижу»).
    #   Имена — с тел спавнеров `effects_particles.c`, регистр приведён к дисковому.
    "eff_0_othr_99_pmhpre01-a_00",     # восстановление ЖИЗНЕЙ  (CharEffect_SpawnPMhpreOnDummy@0xF29AA0,
    "eff_0_othr_99_pmhpre01-a_01",     #   _v2@0xF2D950, SpawnHpRecovery@0xF28F10; звук S_e_PMhprecovery)
    "eff_0_othr_99_pmhpre01-a_02",
    "eff_0_othr_99_pmmpre01-a_00",     # восстановление МАНЫ    (CharEffect_SpawnPMmpreOnDummy@0xF294F0,
    "eff_0_othr_99_pmmpre01-a_01",     #   _v2@0xF2A170; звуки S_e_PMhprecovery / S_e_PMmprecovery)
    "eff_0_othr_99_pmmpre01-a_02",
    "eff_0_othr_99_pmalre01-a_00",     # полное восстановление  (CharEffect_SpawnPMalreOnDummy@0xF2A720,
    "eff_0_othr_99_pmalre01-a_01",     #   пять спавнов: a_00,a_01,a_02,a_00,a_00; звук S_e_PMallrecovery)
    "eff_0_othr_99_pmalre01-a_02",
    "eff_0_othr_99_pmskre01-a_00",     # сброс навыков (CharEffect_PlaySkillReset@0xF2B1A0, attach 7,
                                       #   звук S_e_PMskillreset)
    "eff_0_char_99_pmarmo01-a_00",     # разрушение брони (CharEffect_PlayArmorBreak@0xF2B4A0,
                                       #   звук S_e_PMarmor)
    # (д) Камень души — единственный .jeb ветки `itemSubType == 32` приёмника 5029
    #   (`item_inventory.c:39053`, кость 0, режим привязки 5 = CharPos, звук S_c_SmExpGift.wav).
    "eff_1_char_99_expmovg-a_01",
    # ★★★ ДВА ЭФФЕКТА ЗВАНИЙ (2026-08-30, отчёт `Reports/title_system.md` §13).
    #   Оба спавнятся НА ПЕРСОНАЖЕ и оба — вне гейта «свой игрок», то есть их видно и у соседей:
    #     • `ex_mark_003.jeb` — «секрет звания открыт» (опкод 6085),
    #       `netHandler_OnGuildTitleResult@0xE9A850` (`network.c:31468`):
    #       Effect_SpawnByName(g_pEffectMgr, "ex_mark_003.jeb", 1, x,y,z, colorArg, char->posVec, 0, 6, 0, 1.0f)
    #       Состав: 0x12 меш `ex_mark.jem` (ВОСКЛИЦАТЕЛЬНЫЙ ЗНАК) + 3×0xD частицы + 2×0xC биллборда
    #       + 0xE кольцо; самый долгий эмиттер ≈ 2.12 с.
    #     • `0018.jeb` — «звание получено» (опкод 6065),
    #       `uiTitle_OnJobChangeResult@0xE9A9F0` (`ui_core.c:82321`), те же аргументы и тот же attach-6.
    #       Состав: 14×0xB квады + 2×0x12 меша `cap_01.jem`/`cap_02.jem` (ШАПКА ВЫПУСКНИКА)
    #       + 5×0xD частицы + 2×0xE кольца; самый долгий эмиттер 5.0 с.
    #   ⚠ ЗВУКА У ОБОИХ НЕТ — доказано двумя методами: (1) в классе `CUITitle`
    #     (0xE962A0…0xE9CC80) нет ни одного `Audio_Play*`/`.wav`; (2) в самих `.jeb`/`.jet`
    #     нет ни поля звука, ни строки `.wav` (закон `never-invent-only-original`).
    "ex_mark_003",                     # 6085 «найдено» — восклицательный знак
    "0018",                            # 6065 «получено» — шапка выпускника
    # ★★★ 2026-09-27: «КОРОНА» ВЛАДЕЛЬЦА ДОБЫЧИ РЕЙД-БОССА (опкод 6126), `CharFx_UpdateKingCrown@0xA21110`:
    #   Effect_SpawnByName(g_pEffectMgr, "king_cap01.jeb", 1, x,y,z, +0x2A1F8, char->posVec, 0, 6, 0, 1.0f),
    #   затем `fx+0x2A8 = 1` — ЗАЦИКЛЕН до `CEffectMgr_StopByOwner` (тот ставит 0). Поэтому НЕ в ONE_SHOT:
    #   ветки mesh12 `build_emitter` форсят end_mode=1 (перезапуск), что и нужно вечному эффекту.
    #   Звука нет. Состав эффекта (какой меш вращается и пр.) описан ОДИН раз — в шапке
    #   `Public/World/AsdaKingCrownSubsystem.h` (сверено парсером asda_jeb_parse.py).
    "king_cap01",
    # ★★★ 2026-10-29: РЕКВИЗИТ ЭМОЦИЙ (баг игроков «у жонглирования нет мячей»). Имена зашиты в КОДЕ эталона
    #   (как у канала предметов) и потому не попали ни в один список-источник:
    #     • жонглирование (состояние 45, код 116) — `M_Ball.jeb`, attach-6 (мировая матрица персонажа, без кости):
    #       автор `Emote_Execute` ui_core.c:32479, сосед `Recv_CharSpawnFx` network.c:39813. Состав: ОДИН меш-эмиттер
    #       0x11 `M_Ball.jem` (три мяча на семи костях скина, 49 кадров = 1.6 с — ровно длина клипа жонглирования);
    #     • ухаживание (44, код 115) — ДВА эффекта на кость `_8`, attach-10: муж `M_flower01`+`M_flower02`,
    #       жен `F_flower01`+`F_flower02` (ui_core.c:32526-32559, network.c:39747-39792). `*_flower01` — меш 0x12
    #       `flower.jem` (цветок в руке), `*_flower02` — вспышка 0xD + 2×0xC; задержка старта 3.9 с (муж) / 5.0 с (жен).
    #   Регистр — как на диске (нижний); рантайм ищет BP по тому же имени (`UAsdaEffectsSubsystem::SpawnEmoteProps`).
    "m_ball",
    "m_flower01", "m_flower02",
    "f_flower01", "f_flower02",
    # ★ 2026-10-29 (combat3) [ОРИГИНАЛ] барьеры жертвы на кадре удара моба `Combat_ApplyDamage@0xF7C470` (combat_pvp.c:7800-7850):
    #   состояние 51 → `eff_1_char_06_brieg02-a_00` на кости оружия (attach 7) + `S_c_barrior3.wav`; состояние 547 →
    #   `eff_1_char_99_defen01-a_00` (матрица кастера, точка — пересечение луча «моб → жертва» с боксом жертвы) + barrior3.
    #   Рантайм — `AsdaMobHitVictimReaction` (AsdaWorldSubsystem_Hit.cpp).
    "eff_1_char_06_brieg02-a_00", "eff_1_char_99_defen01-a_00",
    # ★★★ 2026-10-30 «БИТВА АРМИЙ» (боевая фаза карты 19, `Reports/faction_select/BATTLE_SPEC.md` §4, §6). Имена зашиты в КОДЕ
    #   эталона (`CCampWar`), в данных их нет — поэтому список живёт здесь. Регистр — как на диске (нижний); рантайм ищет BP по тому
    #   же имени (`UAsdaCampWarBattleSubsystem::SpawnFx`). Все восемь ЗАЦИКЛЕНЫ (у контейнера endMode 3, у квадов endMode 1, у колец
    #   loopMode 1) и гасятся кодом (`CEffectMgr_StopByOwner`) — в ONE_SHOT не входят. Меш-эмиттеров (.jem) нет.
    #     • `camp_hand_01` — у захватчика точки, 6724 sub 1 (`Effect_SpawnByName(…, char->posVec, 0, 6, 0, 1.0)`, attach-6):
    #       4×0xB квады (spell_circle_0002, defsr101) + 0xE кольцо (bloarr01);
    #     • `camp_start` — на точке возрождения своей армии, 6728 код 10 (`CEffectMgr_CreateEffect`, без носителя):
    #       2×0xE кольца + 0xD частицы;
    #     • `camp_light/dark/chaos_01` — свет точки «временно занята» (state 1) армии 0/1/2, кадровый тик `LightFx_UpdateAll`:
    #       0xD + 0xE + 2×0xC; `…_02` — «захвачена» (state 2): 0xD + 3×0xE. Текстуры Camp_Light_01 / Camp_DARK / Camp_CHAOS.
    # (ж) ★ 06.11.2026 Итог дуэли: `PvpDuel_ApplyMatchResult@0xDE5560` (combat_pvp.c:6526/6553/6595/6619) —
    #     `Effect_SpawnByName(…, "pvp_win.jeb" / "pvp_lose.jeb", 1, x,y,z, 0, char->posVec, 0, 6, 0, 1.0f)`, attach-6, разовые (как
    #     звания). Код порта звал их с 01.10 (`SpawnTitleFx`), а ассеты не собирались: журнал «BP_Fx_pvp_win не собран».
    "pvp_win", "pvp_lose",
    "camp_hand_01", "camp_start",
    "camp_light_01", "camp_light_02", "camp_dark_01", "camp_dark_02", "camp_chaos_01", "camp_chaos_02",
]

# Одноразовые эффекты (endMode из JEB, без restart-пульса) — см. блок ONE_SHOT в build_one.
# ★ ФИКС 2026-08-07 (тест юзера «.skill 1 отображается очень некорректно»): ВСЕ 28 эффектов хранителя —
#   РАЗОВЫЕ (призыв/удар скилла), на диске у их growQuad-квадов endMode=0 (СТОП после одного прохода).
#   Без ONE_SHOT build_emitter форсит end_mode=1 (restart-пульс, закон ВЕЧНЫХ карт) ⇒ тёмные
#   modulate-квады (у sk-джебов бленд-контейнер dst=3 = «тёмный» 6:4) МИГАЛИ по кругу все ~2.4 с жизни
#   актора — юзер видел «что-то чёрное» мигающее. Тот же класс бага, что чинили у cmchar01.
# ★ 2026-08-08: сюда же добавлены 27 «h»-эффектов слоя 2 — они тоже РАЗОВЫЕ (импакт удара,
#   у всех endMode=0 на диске), причина та же: без ONE_SHOT growQuad-ветка форсит end_mode=1.
# ★ 2026-08-13: сюда же — ВСЕ 12 эффектов ПРИМЕНЕНИЯ ПРЕДМЕТА. Основание — СЫРЫЕ ДАННЫЕ ДИСКА, а не
#   догадка: у всех 12 файлов `endMode == 0` (СТОП после одной проходки) у КАЖДОГО эмиттера, а у колец
#   0xE премиум-усилений `loopMode == 0` (ring-ветка и так отдаёт 0). Порт спавнит их разово —
#   `SpawnItemUseFx`/`SpawnDummyBoneFx` ставят `SetLifeSpan(NaturalLifeSpan())`. Без ONE_SHOT две ветки
#   `build_emitter` форсят `end_mode=1` БЕЗУСЛОВНО: growQuad (есть у всех 12: квады 0xB/0xC) и mesh12
#   (0x1F у обоих `eff_lucky_*`) — эффект перезапускался бы на излёте жизни актора.
ONE_SHOT = {"eff_0_char_99_cmchar01-a_00", "sb_change_01"} | {
    "sb_md_eff_%s_%s_%02d_%02d" % (fam, kind, sk, lv)
    for fam in ("oh", "cb", "am") for kind in ("sk", "h") for sk in (1, 2, 3) for lv in (0, 1, 2)} | {
    "eff_0_othr_99_hpptn01-a_00", "eff_0_othr_99_mpptn01-a_00", "eff_0_othr_99_elptn01-a_00",
    "eff_lucky_a_00", "eff_lucky_a_01"} | {
    "eff_0_char_99_pmbuff_a_%02d" % i for i in range(7)} | {
    # ★ 2026-08-13: 12 эффектов ВТОРОГО диспетчера (5451) и камень души — тоже РАЗОВЫЕ.
    #   Основание то же, что и у первых 12: это одноразовые «вспышки» применения предмета, актор
    #   живёт одну проходку (`SpawnDummyBoneFx` ставит `SetLifeSpan(NaturalLifeSpan())`), а без
    #   ONE_SHOT ветка growQuad в `build_emitter` форсит `end_mode=1` и эффект пульсировал бы.
    "eff_0_othr_99_pmhpre01-a_00", "eff_0_othr_99_pmhpre01-a_01", "eff_0_othr_99_pmhpre01-a_02",
    "eff_0_othr_99_pmmpre01-a_00", "eff_0_othr_99_pmmpre01-a_01", "eff_0_othr_99_pmmpre01-a_02",
    "eff_0_othr_99_pmalre01-a_00", "eff_0_othr_99_pmalre01-a_01", "eff_0_othr_99_pmalre01-a_02",
    "eff_0_othr_99_pmskre01-a_00", "eff_0_char_99_pmarmo01-a_00", "eff_1_char_99_expmovg-a_01"} | {
    # ★ 2026-08-30: оба эффекта ЗВАНИЙ — РАЗОВЫЕ. Основание — СЫРЫЕ ДАННЫЕ ДИСКА, а не догадка:
    #   у всех 7 эмиттеров `ex_mark_003` и всех 22 эмиттеров `0018` на диске `endMode == 0`
    #   (у колец 0xE `loopMode == 0`). Без ONE_SHOT ветки growQuad и mesh12 в `build_emitter`
    #   форсят `end_mode = 1` БЕЗУСЛОВНО — знак и шапка перезапускались бы по кругу.
    "ex_mark_003", "0018", "pvp_win", "pvp_lose"} | {
    # ★ 2026-10-29: реквизит эмоций — РАЗОВЫЙ. Основание — сырые данные диска: у всех эмиттеров пяти файлов
    #   `endMode == 0` (мяч: меш 0x11; цветок: меш 0x12; вспышка: 0xD + квады 0xC). Без ONE_SHOT ветки mesh12 и
    #   growQuad в `build_emitter` форсят `end_mode = 1` — мячи и цветок перезапускались бы по кругу.
    "m_ball", "m_flower01", "m_flower02", "f_flower01", "f_flower02"} | {
    # ★ 2026-10-29 (combat3, реакция жертвы на удар моба): барьеры 51/547 — РАЗОВЫЕ. Сырые данные диска: brieg02 — 0xC+0x14,
    #   defen01 — 3×0xB + 0xC (+2 кольца 0xE loopMode 0), у всех квадов `endMode == 0`.
    "eff_1_char_06_brieg02-a_00", "eff_1_char_99_defen01-a_00"}

# ★★★ ОТСТУПЛЕНИЕ ОТ КАНОНА (2026-08-08, разрешено юзером явно; закон fix-original-defects).
# ДЕФЕКТ ДАННЫХ ОРИГИНАЛА. В заголовке .jeb (+0x44/+0x48) лежат индексы бленда; лоадеры (текстовый
#   Effect_ParseJET effects_particles.c:113262+ и двоичный CEffect_Load_JEB@0x117E8C0, оба switch 0..10)
#   переводят их как D3DBLEND = диск+1, дальше objListRenderInRange@0x114E470 → SetRenderState
#   (D3DRS_SRCBLEND, effect[162]) / (D3DRS_DESTBLEND, effect[163]).
#   Канон трёх файлов уровня 1 семейства «oh»: диск 10/3 ⇒ SRCBLEND = D3DBLEND_SRCALPHASAT (11),
#   DESTBLEND = D3DBLEND_INVSRCCOLOR (4). Бэкбуфер игры БЕЗ альфа-канала (формат берётся из режима
#   адаптера, EnumAdapterModes → X8R8G8B8/R5G6B5, render_core.c:41172/41194) ⇒ по спецификации D3D9
#   Ad = 1 ⇒ фактор SRCALPHASAT = min(As, 1−Ad) = 0 ⇒ итог = Dst×(1−SrcC): вклад источника НУЛЕВОЙ,
#   эффект ТОЛЬКО ЗАТЕМНЯЕТ кадр. Текстуры этих эмиттеров (полумесяц-слэш pbladee01-b_00, вспышка
#   ZGROW, кольцо defsr101, полосы bloarr01) — светлое-на-чёрном без альфы, то есть нарисованы под
#   АДДИТИВ; под 10/3 светлый полумесяц выжигает в кадре ЧЁРНЫЙ полумесяц.
# ДОКАЗАТЕЛЬСТВО, ЧТО ЭТО ОПЕЧАТКА АВТОРА, А НЕ ЗАМЫСЕЛ: пара 10/3 встречается РОВНО в 8 файлах из
#   2261 во всём EffectModel.bnz, и все 8 — хранительские «oh» уровня 1; при этом это ДВА уникальных
#   файла, размноженных копиями (md5 12754b86… ×4 «sk», a36f9915… ×4 «h»). Все остальные 24 эффекта
#   скиллов хранителя (и 2 053 эффекта игры) — канонический аддитив диск 4/1 = SRCALPHA:ONE.
#   Те же уровни 2 и 3 того же семейства («…_01», «…_02») используют ТУ ЖЕ художку и стоят на 4/1.
# ЛЕЧЕНИЕ: строим эти три как аддитив (как их же соседей по семейству).
# ВОЗВРАТ К КАНОНУ: переменная окружения ASDA_FX_CANON_BLEND=1 при пересборке.
BLEND_CANON_FIX = {
    "sb_md_eff_oh_sk_01_00": "add",   # слот 31 у SoulGuardIndex 0/1/2 (Хранитель зари/солнца/небес)
    "sb_md_eff_oh_sk_02_00": "add",   # слот 32 у тех же трёх записей
    "sb_md_eff_oh_sk_03_00": "add",   # слот 33 у тех же трёх записей
    # ★ ВТОРОЙ СЛОЙ (портирован 2026-08-08, см. JEBS): собственный эффект удара скилла —
    #   `skill_info.sif` +0xD1 (запись 273 б, загрузчик CSkillMgr_LoadSkillData@0xA79F30).
    #   Скиллы 1228 / 1231 / 1234 («Bashing hit», «Thief's attack», «Cut earth») ссылаются на
    #   `Sb_md_Eff_Oh_h_01_00 / _02_00 / _03_00` — те же самые битые 10/3 (проверено дампом .sif).
    "sb_md_eff_oh_h_01_00": "add",
    "sb_md_eff_oh_h_02_00": "add",
    "sb_md_eff_oh_h_03_00": "add",
}
# ★ ПОЛНАЯ ПЕРЕПРОВЕРКА БЛЕНДА ВСЕХ 76 .jeb ХРАНИТЕЛЯ (`sb_*`, 2026-08-08, сырые байты +0x44/+0x48):
#   70 файлов — канонический аддитив диск 4/1 (SRCALPHA:ONE), 6 файлов — битая пара 10/3
#   (SRCALPHASAT:INVSRCCOLOR). Битые: три `oh_sk_0X_00` (md5 12754b86) и три `oh_h_0X_00`
#   (md5 a36f9915) — РОВНО те, что перечислены выше. Других подозрительных сочетаний нет,
#   добавлять в таблицу больше нечего.


# ★★★ КАНАЛ ПРЕДМЕТОВ — 24 имени (те же, что перечислены блоками (а)…(д) выше). Нужен как ГЕЙТ:
#   эталонные ветки колец включаются ТОЛЬКО для него, чтобы полная пересборка сборщика не поменяла
#   крылья / соулмейт / хранителя / снаряд / эффект появления (закон «подтверждённые эффекты не
#   трогать при фиксах смежных»).
ITEM_FX = {
    "eff_0_othr_99_hpptn01-a_00", "eff_0_othr_99_mpptn01-a_00", "eff_0_othr_99_elptn01-a_00",
    "eff_lucky_a_00", "eff_lucky_a_01",
} | {"eff_0_char_99_pmbuff_a_%02d" % i for i in range(7)} | {
    "eff_0_othr_99_pmhpre01-a_00", "eff_0_othr_99_pmhpre01-a_01", "eff_0_othr_99_pmhpre01-a_02",
    "eff_0_othr_99_pmmpre01-a_00", "eff_0_othr_99_pmmpre01-a_01", "eff_0_othr_99_pmmpre01-a_02",
    "eff_0_othr_99_pmalre01-a_00", "eff_0_othr_99_pmalre01-a_01", "eff_0_othr_99_pmalre01-a_02",
    "eff_0_othr_99_pmskre01-a_00", "eff_0_char_99_pmarmo01-a_00", "eff_1_char_99_expmovg-a_01",
}


# ★★★ ГЕЙТ СИД-УЗЛОВ ТРЕКА (`kf_seed_nodes`, отчёт `Reports/item_fx_ring_motion.md`).
#   Трек эмиттера в эталоне = СТАРТ-сид(t=0) + ключи файла + КОНЕЦ-сид(t=lifetime); в файле лежат
#   только промежуточные ключи. Канал предметов вылечен 2026-08-14 первым, дальше раскатываем
#   ПОЭТАПНО — это МНОЖЕСТВО имён, которое каждый канальный сборщик РАСШИРЯЕТ СВОИМ списком
#   (ровно тот же приём, что уже применяется к `ONE_SHOT`):
#       asda_build_skill_fx.py  → CFX.KF_SEED_FX |= set(jebs)   (навыки + состояния соулмейта)
#       asda_build_buff_fx.py   → CFX.KF_SEED_FX |= set(jebs)   (эффекты состояний/баффов)
#   Расширение живёт ТОЛЬКО в процессе своего сборщика ⇒ ни крылья/соулмейт/хранитель (JEBS этого
#   файла), ни свечение заточки, ни транспорт/лут/импакты (у них свои `build_one`), ни эффекты карт
#   (они вообще не BP, а запечённые акторы) измениться не могут.
#   ЭТАП 3 (2026-08-14): к каналу предметов добавлен ВЕСЬ char-канал (`JEBS` этого файла) — крылья,
#   кольцо соулмейта, хранитель души, появление персонажа, снаряды. Замер по каналу: 53 эффекта с
#   keyframe-ключами, в них 91 кольцо (из них 68 с ЕДИНСТВЕННЫМ ключом = были статичны) и 150 квадов.
KF_SEED_FX = set(ITEM_FX) | {str(x).lower() for x in JEBS}

# ★★★ [ПОРТ] ЭТАЛОННАЯ РАСКЛАДКА ОФСЕТОВ КОЛЬЦА 0xE ДЛЯ КАНАЛА, КОТОРЫЙ РАНТАЙМ УЖЕ ЧИТАЕТ
#   ЭТАЛОННОЙ ВЕТКОЙ (2026-09-11, задача «эффекты навыков не как в игре»).
#   ДЕФОЛТ — ПУСТО: ни один существующий канал не меняется, пока свой сборщик не расширит множество
#   (тот же приём, что у `ONE_SHOT` / `KF_SEED_FX` / `HOLD_LAST_FRAME`).
#
#   ЗАЧЕМ. `build_emitter` кладёт офсеты кольца ДВУМЯ разными раскладками:
#     * ЛЕГАСИ (дефолт): `ring_base_pos = basePos + pos` БЕЗ негации z, у трек-узлов офсет зашит
#       КОНСТАНТОЙ [0,0,0] (поля узла [1/3/5]=basePos и [7/9/11]=pos не читаются вообще);
#     * ЭТАЛОН (`ring_track_pos=True`): `ring_base_pos = negate-z(POS-канал)`,
#       `ring_pos_ofs = basePos` (сырой мир), офсеты трек-узлов ЧИТАЮТСЯ.
#   Рантайм-ветка `bEtalonMapPath` (`AsdaEffectActor.cpp`) ждёт ИМЕННО ЭТАЛОННУЮ раскладку: она
#   снимает негацию (`PC = (x, y, −z)`) и гонит канал через R²·N48·N520.
#   А включается она у эффектов НАВЫКОВ безусловно — `AsdaSkillFxSubsystem` ставит актору
#   `bStrictQuadAttach = GSkillFxStrictQuad` (`asda.Skill.StrictFx`, дефолт 1), и
#   `bEtalonMapPath = !bBaseAlreadyUE && (!bStrictRingBasePos || bStrictQuadAttach)` ⇒ true.
#   ИТОГ ДО ПРАВКИ (замер по 256 .jeb навыков, `Reports/fx_skill/`):
#     · 347 колец из 955 приезжали с офсетом [0,0,0] — центр схлопывался в НАЧАЛО эффекта
#       (`eff_1_char_05_sspee01-a_00`: 22 цилиндра вместо веера +62…+108 вверх и до 320 вперёд
#        лежат В ОДНОЙ ТОЧКЕ у ног — плотный ком вместо веера, «то ли больше»);
#     · у колец БЕЗ трек-узлов z офсета ПЕРЕВОРАЧИВАЛСЯ (двойная негация не сходилась):
#       `eff_1_char_06_bigmeob03-a_00` CIL_05 эталон (0,2000,2000) → порт (0,−2000,2000), Δ 4000.
#   ОТКАТ: `ASDA_SKILLFX_NO_RING_ETALON=1` + пересборка канала (сборщик тогда множество не полнит).
RING_ETALON_FX = set()

# ★★★ «ДЕРЖАТЬ ПОСЛЕДНИЙ КАДР» — ПО УМОЛЧАНИЮ ПУСТО, ТО ЕСТЬ НИ ОДНОГО ЭФФЕКТА НЕ КАСАЕТСЯ.
#   Множество расширяет СВОЙ канальный сборщик (как `ONE_SHOT`/`KF_SEED_FX`) и только в своём
#   процессе. Сейчас единственный житель — канал кольца выбора цели
#   (`asda_build_target_ring_fx.py`): его квад обязан отыграть проходку ОДИН раз и ЗАМЕРЕТЬ на
#   конечном диаметре, оставаясь видимым (решение владельца 2026-08-21). Механика и мотивировка —
#   в шапке `asda_fx_common.hold_last_frame`; отчёт `Reports/npc_target_ring_fix2.md` §2.
#   ⚠ ПОРЯДОК: применяется ПОСЛЕ ветки `ONE_SHOT` и ПЕРЕОПРЕДЕЛЯЕТ её `end_mode` (0 → 3).
HOLD_LAST_FRAME = set()

# Ручная яркость удержанного кадра (None = стартовая альфа самого файла). Ставит канальный сборщик.
HOLD_LAST_FRAME_ALPHA = None

# ★ 2026-10-30: МНОГОМЕШЕВЫЕ .jem собирает сама общая ветка `asda_place_effects.build_emitter`
#   (`asda_jem_parse.model_mesh`: все меши в пространстве модели, с матрицей своего узла). Прежняя склейка мячей
#   (гейт MULTI_MESH_MERGE, 29.10) клала вершины без матрицы узла — мячи m_ball стояли мимо своих костей на
#   −20.5 / +1.8 / +23.2; она заменена общим путём.


def log(m): unreal.log_warning("[charfx] %s" % m)


def build_one(J, PE, eff_name):
    out_name = FXC.bp_name(eff_name)
    jb = FXC.read_jeb(J, SRC_DIR, eff_name, log)
    if jb is None: return False
    emitters = jb.get("emitters", [])
    bd = jb.get("blendDst", 1)
    blend_add = FXC.blend_mode(jb)
    # ★ Отступление BLEND_CANON_FIX (см. комментарий у таблицы). Канон возвращается ASDA_FX_CANON_BLEND=1.
    if eff_name in BLEND_CANON_FIX and os.environ.get("ASDA_FX_CANON_BLEND", "") != "1":
        log("%s: бленд диска %d/%d (SRCALPHASAT:INVSRCCOLOR = только затемнение, дефект данных) "
            "→ ОТСТУПЛЕНИЕ: строим как '%s'" % (eff_name, jb.get("blendSrc", -1), bd, BLEND_CANON_FIX[eff_name]))
        blend_add = BLEND_CANON_FIX[eff_name]
    full = "%s/%s" % (OUT_PKG, out_name)
    bp, cdo = FXC.ensure_effect_bp(out_name, OUT_PKG, log)
    if bp is None: return False
    cdo.set_editor_property("effect_name", eff_name)
    ems = []
    skipped17 = 0   # T17 теперь реализован (§G wings_soulmate_re); счётчик = сколько построено, для лога
    for em in emitters:
        try:
            # ★★★ ring_track_pos ДЛЯ КАНАЛА ПРЕДМЕТОВ (2026-08-14, `item_fx_audit.md` §10.3).
            #   В ветке 0xE С ТРЕК-УЗЛАМИ офсет кольца передавался жёсткой константой [0,0,0] —
            #   поля узла [1/3/5]=basePos и [7/9/11]=pos не читались ВООБЩЕ (это та же
            #   первопричина, что чинили у свечения оружия: «бьёт ИЗ КИСТИ»). Замер по каналу:
            #   15 из 24 колец предметов имеют трек-узлы, и у них офсет НЕНУЛЕВОЙ
            #   (`pmbuff_a_00..a_04` pos=(0,5,0); Камень души (0,−22,0)/(0,−15,0)/(0,−30,0)) ⇒
            #   кольца садились в точку привязки вместо своей высоты. Флаг заодно кладёт каналы в
            #   ЭТАЛОННОЙ раскладке (`ring_base_pos` = negate-z(pos-канал), `ring_pos_ofs` = basePos),
            #   которую ждёт ветка `bEtalonMapPath`, включаемая рантаймом `asda.Item.StrictFx`.
            #   Гейт по ITEM_FX — чтобы чужие каналы не поменялись даже при полной пересборке.
            #   ★★★ kf_seed_nodes (2026-08-14, `item_fx_ring_motion.md`): трек эмиттера в эталоне =
            #   СТАРТ-сид(t=0) + ключи файла + КОНЕЦ-сид(t=lifetime); порт строил его ТОЛЬКО из
            #   ключей файла. У 12 колец канала предметов (`pmbuff_a_00..a_04` Object1/Object2,
            #   `a_05` under_cil01/02) ключ РОВНО ОДИН ⇒ `NodeAt` отдавал его на любом времени и
            #   «юбка» у ног стояла с постоянным радиусом и альфой. Тем же флагом квадам канала
            #   возвращается потерянный КОНЕЦ-узел. Гейт по ITEM_FX — на картах 34 кольца и 95
            #   квадов с ключами (принятые эффекты не трогаем).
            #   `ASDA_FX_NO_KF_SEED=1` — A/B-переключатель для замера «до/после» живым зондом
            #   (пересборка канала без сид-узлов); в обычной работе не задаётся.
            #   ★★★ RING_ETALON_FX (2026-09-11) — то же самое для канала, чей РАНТАЙМ уже идёт
            #   эталонной веткой колец (навыки: `asda.Skill.StrictFx` = 1). Разбор — в шапке
            #   множества; дефолт пуст ⇒ прочие каналы не затронуты.
            #   ★ ДВА ГЕЙТА, А НЕ ОДИН: `ring_track_pos` правит ветку кольца С ТРЕК-УЗЛАМИ (и сид-узлы),
            #   а кольцо БЕЗ узлов лежит в отдельной ветке, у которой свой гейт `pure_ring_attached`
            #   (`asda_place_effects.build_emitter`). Замер на прототипах: с одним лишь
            #   `ring_track_pos` у `eff_1_char_06_bigmeob03-a_00` кольцо CIL_05 (узлов нет) осталось
            #   с легаси-раскладкой и Δ 4000 юн никуда не делся. Для ITEM_FX второй гейт НЕ включаем —
            #   канал предметов принят владельцем в текущем виде (закон «не трогать принятые»).
            _ring_et = eff_name in RING_ETALON_FX
            fx = PE.build_emitter(em, blend_add,
                                  pure_ring_attached=_ring_et,
                                  ring_track_pos=(eff_name in ITEM_FX or _ring_et),
                                  kf_seed_nodes=(eff_name in KF_SEED_FX
                                                 and os.environ.get("ASDA_FX_NO_KF_SEED", "") != "1"))
            if fx is None:
                log("  %s: эмиттер %s (тип 0x%X) → None (не поддержан)" % (eff_name, em.get("name", "?"), em.get("type", 0)))
                continue
            if em.get("type") == 0x17: skipped17 += 1
            # ★ СТРОГИЕ ОСИ эффекта на ЖИВОЙ КОСТИ — ТОЛЬКО для эффектов персонажей (крылья и пр.):
            #   знак канальных вращений сверен с live.dmp по всем трём осям (§H6), pos-канал идёт
            #   через ПОЛНУЮ W (§H3), у биллборд-квадов включается орбита pos-канала (§H10: кольцо
            #   соулмейта 8×0xC, pos=(50,0,0), rotY 0→360 = облёт вокруг персонажа), у колец 0xE —
            #   basePos через локаль актора. Карты (фонари/радуга) калиброваны под ИСТОРИЧЕСКИЙ
            #   знак и остаются с дефолтами False — иначе разворот на 180° (регрессия 2026-07-30).
            #   Набор квадов у ЭТОГО канала ШИРЕ прочих — с типом 2 (близнец 0xC, cmchar01).
            #   Разбор — в шапке `asda_fx_common.apply_bone_fx_flags`.
            FXC.apply_bone_fx_flags(fx, em.get("type"), FXC.QUAD_TYPES_WITH_2)
            # ★★★ ПРИЗНАК ДАННЫХ «офсеты кольца уложены ЭТАЛОННОЙ раскладкой» (2026-09-11).
            #   Ставится РОВНО там, где печка её действительно уложила, — иначе рантайм читал бы
            #   ассет по способу привязки актора, а не по тому, что в ассете лежит (разбор — в шапке
            #   `AAsdaEffectActor`/`FAsdaFxEmitter::bRingEtalonLayout`). ITEM_FX сюда НЕ входит:
            #   канал предметов принят владельцем в текущем виде.
            if _ring_et and em.get("type") in (0xE, 4, 0x15, 0x1A) and not em.get("mesh"):
                try:
                    fx.set_editor_property("ring_etalon_layout", True)
                except Exception:
                    pass
            # ★ ONE-SHOT эффекты (cmchar01 — появление/воскрешение): build_emitter для growQuad жёстко
            #   ставит end_mode=1 (restart-пульс, закон ВЕЧНЫХ карт) → столб света перезапускался бы
            #   каждые life-секунды. Возвращаем endMode из JEB (cmchar01: 0 = стоп), как у импактов.
            #   Крылья/соулмейт (луперы) в ONE_SHOT не входят — их поведение НЕ меняется.
            # 0x13/0x14/0x19 добавлены в growQuad-ветку build_emitter 2026-08-07 (жёсткий end_mode=1
            #   там общий) — у one-shot эффектов возвращаем endMode из JEB и для них.
            # ★ ТИП 1 добавлен 2026-08-14 вместе с его переводом в квад-ветку build_emitter
            #   (`item_fx_audit.md` §7): та ветка форсит end_mode=1, а у вспышек зелий
            #   (`HP_C_SW`/`MP_P_SW00`/`EL_P_SW00`) на диске endMode=0 — без этой строки они бы
            #   пульсировали. Дефект поймал `_verify_item_use_fx.py` (3 расхождения из 24).
            if eff_name in ONE_SHOT and em.get("type") in (1, 2, 0xB, 0xC, 0x13, 0x14, 0x18, 0x19) \
                    and (em.get("params") or {}).get("growQuad"):
                # ★ УТОЧНЕНО 2026-08-14 (`Reports/fx_scale_timing_audit.md` §4): брать endMode
                #   с диска БУКВАЛЬНО нельзя — 3 означает РАЗНОЕ у двух машин обновления. С треком
                #   (`effectUpdateKeyframeTrack@0x11F7B10`) 3 = ФРИЗ на последнем узле; БЕЗ ключей
                #   (`fxParticle_stepAnim@0x1218650` → `fxParticleCommitPos@0x1215830` →
                #   `fxParticleSaveTransformState@0x120AAE0`) 3 = РЕСТАРТ с базовых сидов. Прежняя
                #   строка ставила фриз обоим ⇒ 33 квада в 4 эффектах замирали вместо лупа.
                FXC.set_end_mode(fx, PE.etalon_end_mode(em))
            # ★ ТО ЖЕ ДЛЯ МЕШ-ЭМИТТЕРОВ 0x11/0x12/0x1F (2026-08-08, слой 2 удара). Ветка `mesh12`
            #   в build_emitter тоже ставит end_mode=1 БЕЗУСЛОВНО (она писалась под крылья и jewels —
            #   вечные луперы), а у «h»-эффектов хранителя на диске endMode=0: камень/трещина
            #   (Stone_ani_01.jem, mage_stone.jem, crack2) должны отыграть ОДИН раз и застыть,
            #   иначе за время жизни актора импакт перезапускается. Крылья/соулмейт/jewels не
            #   затронуты — они не входят в ONE_SHOT.
            if eff_name in ONE_SHOT and em.get("type") in FXC.MESH_TYPES \
                    and (em.get("params") or {}).get("mesh12"):
                FXC.set_end_mode(fx, PE.etalon_end_mode(em))   # ★ та же формула, см. выше
            # ★★★ УДЕРЖАНИЕ ПОСЛЕДНЕГО КАДРА (кольцо выбора цели). Гейт по умолчанию ПУСТ ⇒ для
            #   всех прочих каналов строка — no-op. Стоит ПОСЛЕДНЕЙ намеренно: она переопределяет
            #   `end_mode`, выставленный веткой `ONE_SHOT` (см. шапку `HOLD_LAST_FRAME`).
            if eff_name in HOLD_LAST_FRAME and (em.get("params") or {}).get("growQuad"):
                FXC.hold_last_frame(fx, log, HOLD_LAST_FRAME_ALPHA)
            ems.append(fx)
        except Exception as ex:
            log("  %s: эмиттер %s (тип 0x%X) пропущен: %s" % (eff_name, em.get("name", "?"), em.get("type", 0), ex))
    # ★★★ ЧЕСТНО ПРО СОХРАНЕНИЕ (2026-08-21): `.uasset`, занятый ДРУГИМ процессом (открытый
    #   редактор), не сохраняется — `MoveFile … Error Code 32`. Раньше строка «создан …»
    #   печаталась всё равно, и пересборка кольца цели «удалась», хотя на диске остался старый
    #   ассет со старым `end_mode`. Ложное «да» — худший вид отчёта, поэтому теперь пишем правду
    #   и возвращаем False (канал посчитает эффект НЕ построенным).
    if not FXC.finish_effect_bp(bp, cdo, ems):
        log("НЕ СОХРАНЁН %s: файл занят другим процессом (закрой редактор UE и повтори). "
            "На диске остался ПРЕЖНИЙ ассет." % full)
        return False
    log("создан %s: эмиттеров %d (0x17 построено %d), бленд=%s" % (full, len(ems), skipped17, blend_add))
    return True


def main():
    mods = FXC.import_fx_modules(log)
    if mods is None: return
    J, PE = mods
    FXC.point_pe_to(PE, OUT_PKG)
    # ★ ASDA_CHARFX_ONLY="имя1;имя2" — построить только перечисленные (закон «ПОДТВЕРЖДЁННЫЕ эффекты
    #   НЕ ТРОГАТЬ при фиксах смежных»: точечный импорт нового эффекта без пересборки крыльев/соулмейта).
    #   `lower=False` — сравнение БЕЗ приведения регистра, ровно как было у этого канала.
    jebs = FXC.only_filter(JEBS, "ASDA_CHARFX_ONLY", lower=False)
    ok = 0
    for eff in jebs:
        ok += 1 if build_one(J, PE, eff) else 0
    log("ГОТОВО: %d/%d" % (ok, len(jebs)))


# ★★★ Закон tools-no-import-side-effects: main() НЕ безусловно на уровне модуля — иначе любой
#   `import asda_build_char_fx` (в т.ч. из пробника или из сборщиков навыков/состояний, которые
#   импортируют этот модуль ради `build_one`) молча перезаливал бы все BP эффектов персонажей.
#   Разбор признака прямого запуска — в шапке `asda_fx_common.is_direct_run` (аудит №4, волна 3:
#   тело жило 14 дословными копиями по Tools/).
if FXC.is_direct_run(globals()):
    main()
