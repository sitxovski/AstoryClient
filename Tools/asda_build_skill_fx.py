# Copyright (c) 2026 @sitxovski (Discord)
# asda_build_skill_fx.py — BP-заготовки ЭФФЕКТОВ ПРИМЕНЕНИЯ НАВЫКОВ (BP_Fx_<база jeb>).
#
# СПИСОК .jeb НЕ ВЫДУМАН: он снят из бинаря скриптом `asda_dump_skill_dispatch.py`
#   (`CharSkillEffect_InitDispatchTable@0xA89510` → 7 массивов колбэков → строки в телах функций)
#   и лежит в `Reports/skill_effect_inventory.tsv`, колонка Kind == "jeb".
#
# Сборка каждого BP — ПОВТОРНО ИСПОЛЬЗУЕТ `asda_build_char_fx.build_one` (один код на все эффекты
#   персонажа: бленд, strict-флаги осей, one-shot, материалы) — своего пути сознательно НЕ заводим.
#
# ★ endMode: у эффектов КАРТ ветка growQuad/mesh12 в `build_emitter` форсит end_mode=1 (вечный
#   restart — закон бессмертных эффектов карт). Эффекты навыков — РАЗОВЫЕ/по данным: список
#   передаётся в `asda_build_char_fx.ONE_SHOT`, и endMode берётся С ДИСКА (0=стоп, 1/2=restart,
#   3=loop) — ровно как в оригинале.
#
# Запуск: Tools\run_headless.bat Tools\asda_build_skill_fx.py
#   ASDA_SKILLFX_ONLY="имя1;имя2" — построить только перечисленные (точечный ремонт без пересборки).
# ★ ЗАКОН tools-no-import-side-effects: при импорте модуль НИЧЕГО не делает.
# ★ Аудит №4 (волна 3): общий пролог канального сборщика (импорт модулей эффектов, нацеливание
#   `asda_place_effects` на пакет канала, гейт `*_ONLY`, расширение `ONE_SHOT`/`KF_SEED_FX`)
#   переехал в `asda_fx_common` — общая точка семи сборщиков ([[port-shared-points]]).
import os
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))

import asda_fx_common as FXC
import unreal

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.abspath(os.path.join(HERE, "..", ".."))   # <ROOT> (Tools → проект → ROOT)
INVENTORY = os.path.join(ROOT, "Reports", "skill_effect_inventory.tsv")
# ★★★ ВТОРОЙ ИСТОЧНИК (2026-08-11): визуал СОСТОЯНИЙ СОУЛМЕЙТА.
#   Эти эффекты в `skill_effect_inventory.tsv` не попадают ПО ПОСТРОЕНИЮ: их обработчики прошиты в
#   ДРУГУЮ таблицу — `skillEffectInitAnimEventTable@0xAD34A0` (30 слотов на персонаже, ключ = id
#   состояния, дёргается пакетами 0x146D/0x146E), а не в `CharSkillEffect_InitDispatchTable@0xA89510`.
#   Снимает их `Tools/asda_dump_soul_state_fx.py`; там же полное доказательство.
SOUL_STATE = os.path.join(ROOT, "Reports", "soul_state_fx.tsv")
# ★ 06.11.2026 ТРЕТИЙ ИСТОЧНИК: эффекты самого духа (слот Дружбы 0). Литералы `SoulEff_UpdateByHp@0xFB6ED0`:
#   покой при HP ≥ 35 % и тревога при HP < 35 %. В таблицу слотов они не попадают: их грузит тик объекта духа.
SOUL_BASE_JEBS = ("souleff_base_idle01", "souleff_base_hp01")
# ★ 10.11.2026 (job4-vfx, ревизия) ЧЕТВЁРТЫЙ ИСТОЧНИК: эффекты СОСТОЯНИЙ 4-й профессии из `system/abSkillInfo.inl`. Их спавнит обработчик
#   `CharFx_SpawnEquipBoneEffects@0xA73C10` (таблица игрока `charSkillFxHandlerTableInit`, skill_buff.c:1799-1806: состояния 301, 302,
#   313, 316, 317, 330, 335, 344), имена он берёт из таблицы `CSkillMgr_LoadAbSkillInfoINL@0xA73440` (skill_buff.c:16135) — в бинаре
#   литералов нет, поэтому `skill_effect_inventory.tsv` их не знает и 12 из 14 эффектов ни разу не собирались.
AB_SKILL_INL = os.path.join(ROOT, "Donors", "client_system", "system", "abSkillInfo.inl")


def log(m):
    unreal.log_warning("[skillfx] %s" % m)


def jeb_list():
    """Уникальные .jeb навыков ИЗ ОБОИХ снятых с бинаря источников. Порядок — алфавитный (детерминизм).

    1) `skill_effect_inventory.tsv` — таблица навыков (`CharSkillEffect_InitDispatchTable@0xA89510`);
    2) `soul_state_fx.tsv`         — таблица состояний соулмейта (`skillEffectInitAnimEventTable@0xAD34A0`).
    """
    out = set()
    if not os.path.exists(INVENTORY):
        log("НЕТ %s — сначала python Tools\\asda_dump_skill_dispatch.py" % INVENTORY)
    else:
        with open(INVENTORY, encoding="utf-8") as f:
            next(f, None)
            for line in f:
                p = line.rstrip("\n").split("\t")
                if len(p) >= 7 and p[5] == "jeb":
                    out.add(os.path.splitext(p[6])[0].lower())
    if os.path.exists(SOUL_STATE):
        with open(SOUL_STATE, encoding="utf-8") as f:
            next(f, None)
            for line in f:
                p = line.rstrip("\n").split("\t")
                if len(p) >= 4 and p[3]:
                    for j in p[3].split(";"):
                        if j.strip():
                            out.add(os.path.splitext(j.strip())[0].lower())
    else:
        log("НЕТ %s — состояния соулмейта пропущены (python Tools\\asda_dump_soul_state_fx.py)" % SOUL_STATE)
    out.update(SOUL_BASE_JEBS)
    # 4) `abSkillInfo.inl` — записи `FILE_NAME=<имя>.jeb` подсекций [<состояние>-<n>] (см. AB_SKILL_INL).
    if os.path.exists(AB_SKILL_INL):
        with open(AB_SKILL_INL, encoding="cp949", errors="replace") as f:
            for line in f:
                if line.startswith("FILE_NAME=") and line[10:].strip():
                    out.add(os.path.splitext(line[10:].strip())[0].lower())
    else:
        log("НЕТ %s — эффекты состояний 4-й профессии пропущены" % AB_SKILL_INL)
    return sorted(out)


def main():
    mods = FXC.import_fx_modules(log, with_char_fx=True)
    if mods is None:
        return
    J, PE, CFX = mods
    FXC.point_pe_to(PE, CFX.OUT_PKG)

    jebs = FXC.only_filter(jeb_list(), "ASDA_SKILLFX_ONLY")
    if not jebs:
        log("список пуст — нечего строить")
        return

    # ★ endMode «как на диске» + СИД-УЗЛЫ ТРЕКА для канала навыков (этап 2 раскатки,
    #   `Reports/item_fx_ring_motion.md`). Эталон достраивает трек СТАРТ-узлом (t=0) и
    #   КОНЕЦ-узлом (t=lifetime) из базовых сидов записи, в файле лежат только промежуточные ключи
    #   (`animSampleCurveAtTime@0x11FCEC0` + `effectUpdateKeyframeTrack@0x11F7B10`). Без них
    #   кольцо/квад с ЕДИНСТВЕННЫМ ключом статичен на всё время жизни, а с несколькими — теряет
    #   начало и конец анимации.
    #   Расширение множеств живёт только в этом процессе ⇒ чужие каналы не затрагиваются
    #   (замер: пересечений списка навыков с ITEM_FX / JEBS персонажа / транспортом / свечением
    #    заточки — НОЛЬ). Разбор — в шапке `asda_fx_common.extend_channel_gates`.
    FXC.extend_channel_gates(CFX, jebs)

    # ★★★ [ПОРТ] ЭТАЛОННАЯ РАСКЛАДКА ОФСЕТОВ КОЛЬЦА 0xE ДЛЯ КАНАЛА НАВЫКОВ (2026-09-11).
    #   Рантайм эффектов навыка ЧИТАЕТ кольца эталонной веткой: `AsdaSkillFxSubsystem` ставит
    #   актору `bStrictQuadAttach = GSkillFxStrictQuad` (`asda.Skill.StrictFx`, дефолт 1), а
    #   `bEtalonMapPath = !bBaseAlreadyUE && (!bStrictRingBasePos || bStrictQuadAttach)` ⇒ true.
    #   Печка же клала ЛЕГАСИ-раскладку (`ring_base_pos = basePos + pos` без негации z; у трек-узлов
    #   офсет — жёсткий [0,0,0]). Контракт печки и рантайма не сходился с 2026-08-10:
    #     · 347 колец из 955 садились в НАЧАЛО эффекта (потеря 62…4000 юн);
    #     · у колец без трек-узлов z офсета переворачивался (Δ до 4000 юн).
    #   Полные замеры — `Reports/fx_skill/`. Расширение живёт ТОЛЬКО в этом процессе ⇒ чужие
    #   каналы (карты, крылья, соулмейт, транспорт, свечение заточки, предметы) не затронуты.
    #   ОТКАТ: `ASDA_SKILLFX_NO_RING_ETALON=1` + пересборка канала.
    if os.environ.get("ASDA_SKILLFX_NO_RING_ETALON", "") != "1":
        CFX.RING_ETALON_FX = set(CFX.RING_ETALON_FX) | set(jebs)
        log("эталонная раскладка колец 0xE: включена для %d эффектов канала" % len(jebs))
    else:
        log("эталонная раскладка колец 0xE: ОТКЛЮЧЕНА (ASDA_SKILLFX_NO_RING_ETALON=1)")

    ok, _skipped, missing = FXC.build_channel(J, PE, CFX, jebs, log)
    log("ГОТОВО: %d/%d построено" % (ok, len(jebs)))
    if missing:
        # Честно: этих файлов НЕТ в EffectModel.bnz оригинала — дефект данных, не наша потеря.
        log("НЕТ НА ДИСКЕ (%d): %s" % (len(missing), ", ".join(missing)))


# ★★★ Закон tools-no-import-side-effects: main() только при ПРЯМОМ запуске.
#   Разбор признака — в шапке `asda_fx_common.is_direct_run` (аудит №4, волна 3: тело жило
#   14 дословными копиями по Tools/).
if FXC.is_direct_run(globals()):
    main()
