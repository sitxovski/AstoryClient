# Copyright (c) 2026 @sitxovski (Discord)
# asda_build_buff_fx.py — BP-заготовки ЭФФЕКТОВ СОСТОЯНИЙ (баффов): BP_Fx_<база jeb>.
#
# СПИСОК .jeb НЕ ВЫДУМАН: он снят с бинаря (`Reports/skill_buff_fx.tsv`) и разобран по телам
#   обработчиков эталона скриптом `Tools/asda_dump_buff_fx.py` → `RawAssets/Data/BuffFx.csv`
#   (колонки `PlayerFx`/`MobFx`). Канал целиком — `Reports/skill_fx_anim.md` §16/§17.
#
# Сборка каждого BP ПОВТОРНО ИСПОЛЬЗУЕТ `asda_build_char_fx.build_one` — своего пути сознательно нет.
# ★ endMode: эффекты состояний, как и эффекты применения навыков, берут endMode С ДИСКА
#   (`asda_build_char_fx.ONE_SHOT`), иначе ветки growQuad/mesh12 форсят end_mode=1 (закон бессмертных
#   эффектов КАРТ) и бафф пульсирует. ДОЛГОЖИТЕЛЬСТВО баффа обеспечивает не endMode, а рантайм:
#   актор живёт, пока состояние висит в списке (`UAsdaEffectsSubsystem`), и гасится при снятии.
#
# Запуск: Tools\run_headless.bat Tools\asda_build_buff_fx.py
#   ASDA_BUFFFX_ONLY="имя1;имя2" — построить только перечисленные (точечный ремонт).
# ★ ЗАКОН [[tools-no-import-side-effects]]: при импорте модуль НИЧЕГО не делает.
# ★ Аудит №4 (волна 3): общий пролог канального сборщика переехал в `asda_fx_common` — общая
#   точка семи сборщиков ([[port-shared-points]]). Здесь остались отличия канала состояний.
import csv
import io
import os
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))

import asda_fx_common as FXC
import unreal

HERE = os.path.dirname(os.path.abspath(__file__))
PROJ = os.path.abspath(os.path.join(HERE, ".."))
CSV_PATH = os.path.join(PROJ, "RawAssets", "Data", "BuffFx.csv")


def log(m):
    unreal.log_warning("[bufffx] %s" % m)


def jeb_list():
    """Уникальные .jeb канала состояний. Порядок алфавитный (детерминизм пересборки)."""
    if not os.path.exists(CSV_PATH):
        log("НЕТ %s — сначала python Tools\\asda_dump_buff_fx.py" % CSV_PATH)
        return []
    out = set()
    with io.open(CSV_PATH, encoding="utf-8-sig", newline="") as f:
        for row in csv.DictReader(f):
            for cell in (row.get("PlayerFx", ""), row.get("MobFx", "")):
                for rec in cell.split(";"):
                    if rec.strip():
                        out.add(rec.split("|")[0].strip().lower())
    return sorted(out)


def main():
    mods = FXC.import_fx_modules(log, with_char_fx=True)
    if mods is None:
        return
    J, PE, CFX = mods
    FXC.point_pe_to(PE, CFX.OUT_PKG)

    jebs = FXC.only_filter(jeb_list(), "ASDA_BUFFFX_ONLY")
    if not jebs:
        log("список пуст — нечего строить")
        return

    # ★★★ endMode «как на диске» + СИД-УЗЛЫ ТРЕКА для канала состояний (этап 2 раскатки,
    #   `Reports/item_fx_ring_motion.md`). См. тот же комментарий в `asda_build_skill_fx.py`.
    #   Замер пересечений списка состояний: с ITEM_FX — 0, с JEBS персонажа
    #   (крылья/соулмейт/хранитель) — 0, с транспортом — 0, с шаблоном свечения заточки — 0;
    #   с каналом навыков — 13 (оба канала в этом же этапе).
    FXC.extend_channel_gates(CFX, jebs)

    # ★ ASDA_BUFFFX_FORCE=1 — пересобрать уже существующие BP канала. Нужен для раскатки правок
    #   ОБЩЕГО кода (сид-узлы трека): обычный прогон только ДОБАВЛЯЕТ недостающие BP и потому
    #   изменения кода до канала не доезжают. Дефолт (не задан) = прежнее поведение «не трогать
    #   существующие».
    force = FXC.env_flag("ASDA_BUFFFX_FORCE")
    ok, skipped, missing = FXC.build_channel(J, PE, CFX, jebs, log,
                                             skip_existing=True, force=force)
    log("ГОТОВО: построено %d, уже были %d, всего в канале %d" % (ok, skipped, len(jebs)))
    if missing:
        # Честно: этих файлов НЕТ ни в EffectModel.bnz оригинала, ни в 8 других сборках клиента
        #   (§16.5) — дефект ДАННЫХ оригинала, подмену «похожим» НЕ делаем ([[never-invent-only-original]]).
        log("НЕТ НА ДИСКЕ (%d): %s" % (len(missing), ", ".join(missing)))


# ★ ЗАКОН [[tools-no-import-side-effects]]: разбор признака прямого запуска — в шапке
#   `asda_fx_common.is_direct_run` (аудит №4, волна 3: тело жило 14 дословными копиями).
if FXC.is_direct_run(globals()):
    main()
