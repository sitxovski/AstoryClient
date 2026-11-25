# Copyright (c) 2026 @sitxovski (Discord)
# Контроль фикса ring_track_pos: печатает офсет узлов колец 0xE у BP свечения.
# Ожидание после фикса (bali_e_co_ma_03): ccyl1 (70,-5,19), ccyl2 (70,5,19), ccyl3 (30,-12,12), ccyl4 (30,12,12).
import sys, unreal
try: sys.stdout.reconfigure(encoding="utf-8", errors="replace")
except Exception: pass

def log(m): unreal.log_warning("[ringchk] %s" % m)


def main():
    """Работа скрипта. Вынесена в функцию, чтобы импорт модуля НИЧЕГО не делал
    (закон [[tools-no-import-side-effects]])."""
    for nm in ["BP_Fx_bali_e_co_ma_03", "BP_Fx_bali_d_co_ma_03", "BP_Fx_xbow_f_co_ma_03"]:
        path = "/Game/AsdaAssets/Effects/%s" % nm
        if not unreal.EditorAssetLibrary.does_asset_exist(path):
            log("НЕТ %s" % nm); continue
        bp = unreal.EditorAssetLibrary.load_asset(path)
        cdo = unreal.get_default_object(bp.generated_class())
        log("=== %s" % nm)
        for e in cdo.get_editor_property("emitters"):
            if not e.get_editor_property("ring"): continue
            ns = e.get_editor_property("nodes")
            vals = []
            for n in ns:
                b = n.get_editor_property("ring_base_pos"); o = n.get_editor_property("ring_pos_ofs")
                vals.append("t=%.2f posChan(negZ)=(%.1f,%.1f,%.1f) basePos=(%.1f,%.1f,%.1f) h=%.1f scale=%.1f topR=%.1f" % (
                    n.get_editor_property("time"), b.x, b.y, b.z, o.x, o.y, o.z,
                    n.get_editor_property("ring_h"), n.get_editor_property("ring_scale"),
                    n.get_editor_property("ring_top_r")))
            log("  %-8s узлов=%d  %s" % (e.get_editor_property("name"), len(ns), " | ".join(vals)))
    log("готово")


# ★ ЗАКОН [[tools-no-import-side-effects]]: при импорте модуль НИЧЕГО не делает.
#   `__spec__` выставляет ЛЮБАЯ машинерия импорта (import / importlib / runpy) и НЕ
#   выставляет прямое исполнение файла (python file.py, UE -ExecutePythonScript).
#   `python -m` даёт `__name__ == "__main__"` и покрыт первым условием.
if __name__ == "__main__" or globals().get("__spec__") is None:
    main()
