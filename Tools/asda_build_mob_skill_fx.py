# Copyright (c) 2026 @sitxovski (Discord)
# asda_build_mob_skill_fx.py — BP ЭФФЕКТОВ НАВЫКОВ МОБОВ (канал Monster_SkillEffect.BIN). [ПОРТ]
#
# ЗАЧЕМ (30.10.2026, система навыков мобов, `Reports/mob_skills/FLOW.md`): автомат 8011/8012 (`UAsdaMobSkillSubsystem`)
#   спавнит .jeb пяти фаз записи навыка (пре-каст, каст, атака, полёт, удар) — `SpawnByJebName` → `BP_Fx_<jeb>`. Из 74
#   уникальных имён BP были у 6 (`Reports/owner_0927/gfx_w1/B_mob_skill_fx.md` §2.5); остальные строит этот канал.
# Источник имён — сама таблица клиента-эталона (`asda_effect_mob_tables.parse_monster_skill_effect`, 91 запись), только
#   слоты с флагом фазы (без флага эталон слот не играет). Имя на диске — в НИЖНЕМ регистре (как у прочих каналов).
#   Запись 41 (`"Mon_10mAreaStun40p_AC_00` с кавычкой) у оригинала не грузится — имя с кавычкой не строим ([[never-invent-only-original]]).
# Форма сборки — общая (`asda_fx_common.build_channel` поверх `asda_build_char_fx.build_one`), сид-узлы трека и эталонный end_mode
#   включены тем же `extend_channel_gates`, что у каналов навыков/состояний. Масштаб 1.0 (у эталона `scaleAll` в путях навыка моба нет).
# По умолчанию ТОЛЬКО ДОБАВЛЯЕТ недостающие BP (существующие — в том числе 6 прежних `BP_Fx_mon_*` — не трогает).
#   ASDA_MOBSKILLFX_ONLY="имя1;имя2" — только перечисленные; ASDA_MOBSKILLFX_FORCE=1 — пересобрать существующие канала.
# Запуск: Tools/run_headless_safe.py Tools\asda_build_mob_skill_fx.py  (редактор закрыт)
#
# ★ ПРАВИЛО tools-no-import-side-effects: при импорте модуля НИЧЕГО не выполняется — только main() при прямом запуске.
import os
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))

import asda_fx_common as FXC
import unreal


def log(m):
    unreal.log_warning("[mobskillfx] %s" % m)


def jeb_list():
    """Уникальные .jeb навыков мобов со взведённым флагом фазы. Порядок алфавитный (детерминизм пересборки)."""
    import asda_effect_mob_tables as T
    out = set()
    for r in T.parse_monster_skill_effect():
        for k in range(5):
            name = (r.get("Jeb%d" % k) or "").strip()
            if not name or not r.get("SlotType%d" % k):
                continue
            if name.startswith('"'):
                log("запись %d слот %d: имя с кавычкой «%s» — у оригинала не грузится, не строим" % (r["EffectId"], k, name))
                continue
            out.add(name.lower())
    return sorted(out)


def main():
    mods = FXC.import_fx_modules(log, with_char_fx=True)
    if mods is None:
        return
    J, PE, CFX = mods
    FXC.point_pe_to(PE, CFX.OUT_PKG)
    jebs = FXC.only_filter(jeb_list(), "ASDA_MOBSKILLFX_ONLY")
    if not jebs:
        log("список пуст — нечего строить")
        return
    FXC.extend_channel_gates(CFX, jebs)
    force = FXC.env_flag("ASDA_MOBSKILLFX_FORCE")
    ok, skipped, missing = FXC.build_channel(J, PE, CFX, jebs, log, skip_existing=True, force=force)
    log("ГОТОВО: построено %d, уже были %d, всего в канале %d" % (ok, skipped, len(jebs)))
    if missing:
        log("НЕТ НА ДИСКЕ (%d): %s" % (len(missing), ", ".join(missing)))


if FXC.is_direct_run(globals()):
    main()
