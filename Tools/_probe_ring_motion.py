# -*- coding: utf-8 -*-
# Copyright (c) 2026 @sitxovski (Discord)
# _probe_ring_motion.py — ДИАГНОСТИКА (ничего не меняет): печатает, что РЕАЛЬНО лежит в собранных
# BP_Fx_* эффектов предметов по кольцам 0xE и квадам, и сверяет это с сырым .jeb.
# Повод: пользователь сообщил, что «юбка» (широкое кольцо у ног) статична.
# Запуск: UnrealEditor-Cmd.exe <uproject> -ExecutePythonScript="...\_probe_ring_motion.py" ...
import os, sys, unreal

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import asda_jeb_parse as J

PKG = "/Game/AsdaAssets/Effects"
NAMES = [n.strip() for n in os.environ.get(
    "ASDA_RINGPROBE",
    "eff_0_char_99_pmbuff_a_06;eff_0_char_99_pmbuff_a_01;eff_0_char_99_pmbuff_a_00;eff_0_char_99_pmbuff_a_03"
).split(";") if n.strip()]


def log(m):
    unreal.log_warning("[зонд-кольца] %s" % m)


def f3(v):
    return "(%.2f,%.2f,%.2f)" % (v.x, v.y, v.z)


def main():
    """Работа скрипта. Вынесена в функцию, чтобы импорт модуля НИЧЕГО не делал
    (закон [[tools-no-import-side-effects]])."""
    for eff in NAMES:
        full = "%s/BP_Fx_%s" % (PKG, eff.replace("-", "_"))
        if not unreal.EditorAssetLibrary.does_asset_exist(full):
            log("НЕТ ассета %s" % full)
            continue
        bp = unreal.EditorAssetLibrary.load_asset(full)
        cdo = unreal.get_default_object(bp.generated_class())
        ems = cdo.get_editor_property("emitters")
        jb = J.parse_effect(eff) or {"emitters": []}
        log("=== %s: эмиттеров в BP %d, в .jeb %d" % (eff, len(ems), len(jb["emitters"])))
        for i, e in enumerate(ems):
            t = e.get_editor_property("jeb_type")
            nm = e.get_editor_property("name")
            per = e.get_editor_property("period")
            endm = e.get_editor_property("end_mode")
            nodes = e.get_editor_property("nodes")
            if e.get_editor_property("ring"):
                log(" эм.%02d 0x%X «%s» РИНГ период=%.3f endMode=%d узлов=%d uTile=%.2f vA=%.2f"
                    % (i, t, nm, per, endm, len(nodes),
                       e.get_editor_property("ring_u_tile"), e.get_editor_property("ring_va")))
                for nd in nodes:
                    log("      t=%.3f scale=%.3f roll=%.2f rot=%s H=%.3f topR=%.3f botR=%.3f U=%.4f V=%.4f "
                        "base=%s posOfs=%s a=%.3f"
                        % (nd.get_editor_property("time"), nd.get_editor_property("ring_scale"),
                           nd.get_editor_property("ring_roll"), f3(nd.get_editor_property("ring_rot")),
                           nd.get_editor_property("ring_h"), nd.get_editor_property("ring_top_r"),
                           nd.get_editor_property("ring_bot_r"), nd.get_editor_property("ring_u"),
                           nd.get_editor_property("ring_v"), f3(nd.get_editor_property("ring_base_pos")),
                           f3(nd.get_editor_property("ring_pos_ofs")), nd.get_editor_property("alpha")))
            elif e.get_editor_property("single_quad"):
                log(" эм.%02d 0x%X «%s» КВАД период=%.3f endMode=%d узлов=%d орбита=%s"
                    % (i, t, nm, per, endm, len(nodes), e.get_editor_property("strict_quad_orbit")))
                for nd in nodes:
                    log("      t=%.3f sX=%.2f sY=%.2f rot=%s U=%.4f V=%.4f base=%s posOfs=%s a=%.3f"
                        % (nd.get_editor_property("time"), nd.get_editor_property("size_x"),
                           nd.get_editor_property("size_y"), f3(nd.get_editor_property("ring_rot")),
                           nd.get_editor_property("ring_u"), nd.get_editor_property("ring_v"),
                           f3(nd.get_editor_property("ring_base_pos")),
                           f3(nd.get_editor_property("ring_pos_ofs")), nd.get_editor_property("alpha")))
            else:
                log(" эм.%02d 0x%X «%s» ЧАСТИЦЫ период=%.3f endMode=%d движение=%s posB=%s posT=%s"
                    % (i, t, nm, per, endm, e.get_editor_property("emitter_motion"),
                       f3(e.get_editor_property("pos_base")), f3(e.get_editor_property("pos_target"))))
    log("ГОТОВО")


# ★ ЗАКОН [[tools-no-import-side-effects]]: при импорте модуль НИЧЕГО не делает.
#   `__spec__` выставляет ЛЮБАЯ машинерия импорта (import / importlib / runpy) и НЕ
#   выставляет прямое исполнение файла (python file.py, UE -ExecutePythonScript).
#   `python -m` даёт `__name__ == "__main__"` и покрыт первым условием.
if __name__ == "__main__" or globals().get("__spec__") is None:
    main()
