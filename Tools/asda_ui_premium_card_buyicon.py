# -*- coding: utf-8 -*-
# Copyright (c) 2026 @sitxovski (Discord)
# asda_ui_premium_card_buyicon.py — ★★★ ПОТЕРЯННЫЙ ЗНАЧОК «+» У КНОПКИ ПОКУПКИ КАРТОЧКИ 6000
#   (`WBP_SHELLITEM_STORE_WIN`, окно `SHELLITEM_STORE_WIN` 6000).
#
# ═══ ПРОБЛЕМА (жалоба владельца 2026-08-28: «другие кнопки, хотя JUI тот же») ══════════════════
# Настоящая кнопка покупки карточки — контрол **6089** `SHELLITEM_POP{nn}_BUT0`, скин `Mac_Btn_2`
# (`JuiSkin.ini:63394`: `Type = "PushButton"`, один план, три кадра, `NE_skillbar2.tga`,
# `Diffuse = FFFFC488` — та самая ОРАНЖЕВАЯ кнопка). В `.JUI` у неё есть РЕБЁНОК —
# значок `WkIcon_CirPlus`, `Origin = "9 6"`, `Size = "11 11"` (`gamemain_jui.ini:111374`), то есть
# «+» слева от подписи. ⚠ У этого ребёнка поле `ID` ПУСТОЕ, а импортёр для контрола с пустым `ID`
# узла не создаёт (класс S1a аудита) — поэтому в ассете «+» у кнопки НЕТ, хотя в оригинале он есть.
#
# ★ Значок при этом ЕСТЬ у запасной (мёртвой) кнопки `_BUT0_b`: там ребёнок назван
#   `SHELLITEM_POP{nn}_BUT0i`, `ID` непустой, и импортёр его создал (`..._BUT0i_<idx>_0`).
#   Берём ЕГО как образец: браш и геометрия копируются 1:1, ничего не подбирается.
#
# ═══ ЧТО ДЕЛАЕТ ШАГ (идемпотентно: второй прогон = 0 правок) ══════════════════════════════════
#   для каждой из 21 карточки создаёт `SHELLITEM_POP{nn}_BUY_PLUS` внутри группы кнопки покупки
#   (`SHELLITEM_POP{nn}_BUT0_grp`, у ячейки 00 — `__SHELLITEM_POP00_BUT0_grp`: у неё в `.JUI`
#   мусорный префикс `[ ` в поле `ID`, импортёр превратил его в `__`) с брашем и размером
#   узла-образца `..._BUT0i_<idx>_0` и геометрией `L9 T6 11×11` из `.JUI`.
#   Заодно печатает в журнал, есть ли у самой кнопки кисть скина (диагностика «оранжевая или нет»).
#
# Запуск: run_headless.bat @@ASTORY_DEV_ROOT@@\AstoryClient\Tools\asda_ui_premium_card_buyicon.py
#   Сухой прогон: set ASDA_BUYICON_DRYRUN=1
import os
import sys
import traceback
import unreal
# ★ дедуп №2 В1 Т12: общие помощники правки WBP (обход дерева, канвасный слот, смещения) — asda_ui_widget_common
import os as _uwc_os, sys as _uwc_sys
if _uwc_os.path.dirname(_uwc_os.path.abspath(__file__)) not in _uwc_sys.path:
    _uwc_sys.path.insert(0, _uwc_os.path.dirname(_uwc_os.path.abspath(__file__)))
import asda_ui_widget_common as _UWC   # noqa: E402

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))

EAL = unreal.EditorAssetLibrary
EPS = 0.01

ASSET = "/Game/AsdaAssets/UI/Player_UI/Premium/WBP_SHELLITEM_STORE_WIN"
CARDS = 21
# Геометрия значка — из `gamemain_jui.ini` (`Origin "9 6"`, `Size "11 11"`).
ICON_OFF = (9.0, 6.0, 11.0, 11.0)


def log(m):
    unreal.log("[значок покупки 6000] %s" % m)


walk = _UWC.walk   # ★ дедуп №2 В1 Т12: общий помощник (тело перенесено дословно)


canvas_slot = _UWC.canvas_slot   # ★ дедуп №2 В1 Т12: общий помощник (тело перенесено дословно)


set_offsets = _UWC.set_offsets   # ★ дедуп №2 В1 Т12: общий помощник (тело перенесено дословно)


def same_margin(m, want):
    return (abs(m.left - want[0]) < EPS and abs(m.top - want[1]) < EPS
            and abs(m.right - want[2]) < EPS and abs(m.bottom - want[3]) < EPS)


def main():
    dry = bool(os.environ.get("ASDA_BUYICON_DRYRUN"))
    B = unreal.AsdaUmgBuilder
    if not EAL.does_asset_exist(ASSET):
        log("НЕТ АССЕТА %s" % ASSET)
        return
    bp = EAL.load_asset(ASSET)
    root = B.get_root_canvas(bp)
    if not root:
        log("!! нет корневого канваса у %s" % ASSET)
        return

    nodes = {}
    for w in walk(root):
        nodes.setdefault(w.get_name(), w)

    added = fixed = noicon = nogrp = 0
    style_seen = None
    for i in range(CARDS):
        nn = "%02d" % i
        # ★ У ячейки 00 имена настоящих кнопок несут мусорный префикс `__` (см. шапку).
        grp = nodes.get("SHELLITEM_POP%s_BUT0_grp" % nn) or nodes.get("__SHELLITEM_POP%s_BUT0_grp" % nn)
        if not isinstance(grp, unreal.PanelWidget):
            nogrp += 1
            continue
        # Диагностика вида самой кнопки (кисть кадра «покой» скина `Mac_Btn_2`).
        if style_seen is None:
            btn = nodes.get("SHELLITEM_POP%s_BUT0" % nn) or nodes.get("__SHELLITEM_POP%s_BUT0" % nn)
            if btn is not None:
                try:
                    st = btn.get_editor_property("widget_style")
                    nb = st.get_editor_property("normal")
                    res = nb.get_editor_property("resource_object")
                    style_seen = res.get_name() if res else "НЕТ КИСТИ"
                except Exception as e:
                    style_seen = "не прочитан стиль (%s)" % e

        # Образец значка «+» — у запасной кнопки `_BUT0_b` (там `ID` непустой, узел импортирован).
        sample = None
        for n, w in nodes.items():
            if n.startswith("SHELLITEM_POP%s_BUT0i" % nn) and isinstance(w, unreal.Image):
                sample = w
                break
        name = "SHELLITEM_POP%s_BUY_PLUS" % nn
        cur = nodes.get(name)
        if cur is None:
            if sample is None:
                noicon += 1
                continue
            if not dry:
                img = B.add_image(bp, grp, name,
                                  unreal.Vector2D(ICON_OFF[0], ICON_OFF[1]),
                                  unreal.Vector2D(ICON_OFF[2], ICON_OFF[3]), None,
                                  unreal.Box2D(), unreal.LinearColor(1.0, 1.0, 1.0, 1.0),
                                  False, unreal.Margin(0.0, 0.0, 0.0, 0.0))
                if img:
                    # Браш копируем ЦЕЛИКОМ у образца — та же текстура, тот же UV-регион.
                    img.set_editor_property("brush", sample.get_editor_property("brush"))
                    # Значок не должен перехватывать мышь у кнопки (закон `selfhittest-vs-hittest-invisible`).
                    img.set_visibility(unreal.SlateVisibility.HIT_TEST_INVISIBLE)
                    set_offsets(img, ICON_OFF)
                    nodes[name] = img
            added += 1
        else:
            s = canvas_slot(cur)
            if s is not None:
                off = s.get_editor_property("layout_data").get_editor_property("offsets")
                if not same_margin(off, ICON_OFF):
                    if not dry:
                        set_offsets(cur, ICON_OFF)
                    fixed += 1

    if not dry and (added or fixed):
        # ⚠ `CompileAndSave` НЕ СОХРАНЯЕТ (имя врёт) — сохраняет `save_loaded_asset`.
        B.compile_and_save(bp)
        if not EAL.save_loaded_asset(bp, False):
            raise RuntimeError("не удалось сохранить %s" % ASSET)
    log("кисть кнопки покупки (кадр «покой» скина Mac_Btn_2): %s" % style_seen)
    log("готово: значков создано %d, геометрий поправлено %d, без образца %d, без группы %d%s"
        % (added, fixed, noicon, nogrp, " (СУХОЙ ПРОГОН)" if dry else ""))


if __name__ == "__main__":
    try:
        main()
    except Exception:
        unreal.log_error("[значок покупки 6000] ИСКЛЮЧЕНИЕ:\n%s" % traceback.format_exc())
