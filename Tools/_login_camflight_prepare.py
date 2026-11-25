# -*- coding: utf-8 -*-
# Copyright (c) 2026 @sitxovski (Discord)
# _login_camflight_prepare.py — подготовка перевода перелёта логина «сцена летит → камера летит» (13.09.2026).
#   1) Сохраняет ПРЕЖНЮЮ секвенцию LS_LoginFlight побайтно как LS_LoginFlight_SceneMove (ассет отката).
#      ★ Делать ДО asda_build_login_flight_sequence.py в режиме camera: тот удаляет и пересоздаёт LS_LoginFlight.
#      Гвард: если LS_LoginFlight_SceneMove уже есть — НЕ перезаписываем (повторный прогон не затрёт откат
#      новой камерной секвенцией).
#   2) DA_LoginConfig: FlightSequenceSceneMove → этот ассет, bFlightMovesCamera = true (режим по умолчанию).
#      Требует собранного C++ с новыми полями (AsdaLoginConfig.h, 13.09.2026).
# Запуск (СКРЫТО, редактор закрыт):
#   python Tools/run_headless_safe.py Tools/_login_camflight_prepare.py --marker CAMFLIGHT-PREPARE-FINISHED
import unreal

EAL = unreal.EditorAssetLibrary
SRC = "/Game/AsdaAssets/Cinematics/LS_LoginFlight"
DST = "/Game/AsdaAssets/Cinematics/LS_LoginFlight_SceneMove"
CFG = "/Game/AsdaAssets/Config/DA_LoginConfig"


def log(m):
    unreal.log_warning("[camflight-prepare] %s" % m)


def bindings_of(path):
    seq = unreal.load_asset(path)
    return [str(b.get_display_name()) for b in seq.get_bindings()] if seq else None


def main():
    # ★★★ ЗАЩИТА СЦЕНЫ ЛОГИНА (15.09.2026, Reports/dedup_audit/tex_rebind_2026-09-15.md, раздел 16): пишет ТОЛЬКО пакеты сцены логина — дубликат
    #   LS_LoginFlight_SceneMove и DA_LoginConfig (их грузит лишь AsdaLoginGameMode.cpp). Без ASDA_ALLOW_LOGIN_WRITE=1 — стоп до загрузки.
    import os as _os_lg, sys as _sys_lg
    _sys_lg.path.insert(0, _os_lg.path.dirname(_os_lg.path.abspath(__file__)))   # Tools — для asda_login_guard
    from asda_login_guard import forbid_login_write
    forbid_login_write("_login_camflight_prepare.py", "Cinematics/LS_LoginFlight_SceneMove и Config/DA_LoginConfig (дубликат, свойства, сохранение)")
    src_b = bindings_of(SRC)
    log("%s: биндинги %s" % (SRC, src_b))
    if EAL.does_asset_exist(DST):
        log("%s уже есть (биндинги %s) — НЕ перезаписываю" % (DST, bindings_of(DST)))
    else:
        if src_b is None or "LoginCamera" in src_b:
            raise RuntimeError("исходная LS_LoginFlight уже камерная или не загрузилась (%s) — откатный ассет из неё "
                               "делать нельзя; соберите его asda_build_login_flight_sequence.py с "
                               "ASDA_LOGIN_FLIGHT_MODE=scene" % src_b)
        dup = EAL.duplicate_asset(SRC, DST)
        if dup is None:
            raise RuntimeError("duplicate_asset не создал %s" % DST)
        EAL.save_loaded_asset(dup)
        log("создан %s (биндинги %s)" % (DST, bindings_of(DST)))
    cfg = EAL.load_asset(CFG)
    if cfg is None:
        raise RuntimeError("не загрузился " + CFG)
    cfg.set_editor_property("flight_sequence_scene_move", unreal.load_asset(DST))
    cfg.set_editor_property("flight_moves_camera", True)
    EAL.save_loaded_asset(cfg)
    log("DA_LoginConfig: flight_sequence=%s, flight_sequence_scene_move=%s, flight_moves_camera=%s" % (
        cfg.get_editor_property("flight_sequence"), cfg.get_editor_property("flight_sequence_scene_move"),
        cfg.get_editor_property("flight_moves_camera")))
    log("CAMFLIGHT-PREPARE-FINISHED")


# ★ ЗАКОН [[tools-no-import-side-effects]]: при импорте модуль НИЧЕГО не делает.
if __name__ == "__main__" or globals().get("__spec__") is None:
    main()
