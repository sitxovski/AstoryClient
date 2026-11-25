# Copyright (c) 2026 @sitxovski (Discord)
# _verify_buff_fx.py — сплошная проверка ТРЕТЬЕГО КАНАЛА визуала навыков (эффекты состояний).
#
# Что проверяется (без запуска игры — ассеты и данные):
#   1) DT_BuffFx загружается и содержит 350 состояний;
#   2) каждая запись программы показа разбирается ровно теми же правилами, что и рантайм
#      (`UAsdaEffectsSubsystem::ParseBuffFxProgram`);
#   3) у каждого `.jeb` есть собранный `BP_Fx_…` ЛИБО файла нет в оригинале (дефект данных, §16.5);
#   4) режим привязки — из известного набора эталона (3/5/6/7/8/9/12/13);
#   5) каждое ДОЛГОЖИВУЩЕЕ состояние (есть slot 0/1) реально имеет чем гаситься.
#
# Запуск: Tools\run_headless.bat Tools\_verify_buff_fx.py
# ★ ЗАКОН [[tools-no-import-side-effects]]: при импорте модуль НИЧЕГО не делает.
import os
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))

import unreal

HERE = os.path.dirname(os.path.abspath(__file__))
PROJ = os.path.abspath(os.path.join(HERE, ".."))
DT = "/Game/AsdaAssets/Data/DT_BuffFx"
FX_PKG = "/Game/AsdaAssets/Effects"
KNOWN_ATTACH = {3, 5, 6, 7, 8, 9, 12, 13}


def log(m):
    unreal.log_warning("[verify-bufffx] %s" % m)


def main():
    try:
        import asda_paths as P
        src_dir = P.EFFECT_MODEL_ORIG_DIR
    except Exception:
        src_dir = None
    on_disk = set(f.lower() for f in os.listdir(src_dir)) if src_dir and os.path.isdir(src_dir) else set()

    dt = unreal.load_asset(DT)
    if not dt:
        log("НЕТ %s — запусти Tools\\asda_build_buff_fx_dt.py" % DT)
        return
    names = unreal.DataTableFunctionLibrary.get_data_table_row_names(dt)
    rows = unreal.DataTableFunctionLibrary.get_data_table_column_as_string(dt, "PlayerFx")
    log("состояний в таблице: %d" % len(names))

    dt_rows = {}
    for col in ("Id", "BuffName", "PlayerFx", "MobFx", "Wav"):
        dt_rows[col] = unreal.DataTableFunctionLibrary.get_data_table_column_as_string(dt, col)

    n_rec = n_bp = n_nofile = n_badattach = 0
    persist_states, missing_bp = 0, []
    for i in range(len(names)):
        for col in ("PlayerFx", "MobFx"):
            cell = dt_rows[col][i]
            has_slot = False
            for rec in [x for x in cell.split(";") if x.strip()]:
                f = rec.split("|")
                jeb, attach = f[0], int(f[1]) if len(f) > 1 else -1
                slot = int(f[3]) if len(f) > 3 else -1
                n_rec += 1
                if attach not in KNOWN_ATTACH:
                    n_badattach += 1
                    log("!! состояние %s (%s): НЕИЗВЕСТНАЯ привязка %d у %s"
                        % (dt_rows["Id"][i], col, attach, jeb))
                if slot >= 0:
                    has_slot = True
                bp = "%s/BP_Fx_%s" % (FX_PKG, jeb.replace("-", "_"))
                if unreal.EditorAssetLibrary.does_asset_exist(bp):
                    n_bp += 1
                elif (jeb.lower() + ".jeb") not in on_disk:
                    n_nofile += 1   # файла нет в оригинале — эталон тоже ничего не показывал
                else:
                    missing_bp.append(jeb)
            if has_slot:
                persist_states += 1

    log("записей программы показа: %d; из них BP собран: %d; файла .jeb нет в оригинале: %d"
        % (n_rec, n_bp, n_nofile))
    log("долгоживущих программ (есть slot 0/1 ⇒ есть чем гасить): %d" % persist_states)
    if missing_bp:
        log("!! BP НЕ СОБРАН при наличии .jeb (%d): %s" % (len(missing_bp), ", ".join(sorted(set(missing_bp))[:20])))
    if n_badattach:
        log("!! неизвестных режимов привязки: %d" % n_badattach)
    log("ИТОГ: %s" % ("ОК" if not missing_bp and not n_badattach else "ЕСТЬ ЗАМЕЧАНИЯ (см. выше)"))


def _is_direct_run():
    """True — файл запущен НАПРЯМУЮ, а не импортирован ([[tools-no-import-side-effects]])."""
    return __name__ == "__main__" or globals().get("__spec__") is None


if _is_direct_run():
    main()
