# Copyright (c) 2026 @sitxovski (Discord)
# _probe_soulguard_fx_blend.py — снимок состояния BP-эффектов хранителя: материал каждой секции,
#   наличие парного MaterialB (признак «тёмного» mod-бленда), тип эмиттера, end_mode.
#   Печатает таблицу в лог headless — используется как «до/после» для правки бленда
#   (см. Reports/soul_guardian_companion_re.md, раздел «ЭФФЕКТ УДАРА»).
# Запуск: run_headless.bat Tools\_probe_soulguard_fx_blend.py
import unreal

# ★ 2026-08-08: к 27 «sk» (слой 1, мотионный) добавлены 27 «h» (слой 2, собственный эффект скилла) —
#   в них ровно та же битая пара 10/3 у трёх файлов `oh_h_0X_00`, проверяем оба слоя разом.
NAMES = ["sb_change_01"] + [
    "sb_md_eff_%s_%s_%02d_%02d" % (fam, kind, sk, lv)
    for fam in ("oh", "cb", "am") for kind in ("sk", "h") for sk in (1, 2, 3) for lv in (0, 1, 2)]


def log(m):
    unreal.log_warning("[fxblend] %s" % m)


def main():
    EAL = unreal.EditorAssetLibrary
    n_mod = 0
    for nm in NAMES:
        path = "/Game/AsdaAssets/Effects/BP_Fx_%s" % nm.replace("-", "_")
        if not EAL.does_asset_exist(path):
            log("НЕТ ассета: %s" % path)
            continue
        bp = EAL.load_asset(path)
        cdo = unreal.get_default_object(bp.generated_class())
        ems = cdo.get_editor_property("emitters")
        mod = sum(1 for e in ems if e.get_editor_property("material_b") is not None)
        n_mod += 1 if mod else 0
        mats = []
        for e in ems:
            m = e.get_editor_property("material")
            mb = e.get_editor_property("material_b")
            mats.append("0x%X:%s%s/em%d" % (
                e.get_editor_property("jeb_type"),
                m.get_name() if m else "НЕТ",
                ("+B(" + mb.get_name() + ")") if mb else "",
                e.get_editor_property("end_mode")))
        log("%-24s эмиттеров=%-3d modB=%-3d  %s" % (nm, len(ems), mod, " | ".join(mats)))
    log("ИТОГО эффектов с парой Modulate+Additive: %d из %d" % (n_mod, len(NAMES)))


# ★★★ Закон tools-no-import-side-effects: main() только при ПРЯМОМ запуске.
#   Прежняя проверка `__name__ != "_probe_soulguard_fx_blend"` ловила лишь импорт ровно под этим именем:
#   `import Tools._probe_soulguard_fx_blend` или `importlib.util.spec_from_file_location("X", ...)` дают другое
#   имя — и main() выстреливал бы. Надёжный признак импорта — `__spec__`: его выставляет ЛЮБАЯ
#   машинерия импорта (import / importlib / пакет / runpy.run_module) и НЕ выставляет прямое
#   исполнение файла (python file.py, UE -ExecutePythonScript). `python -m` даёт "__main__".
def _is_direct_run():
    """True — файл запущен НАПРЯМУЮ, а не импортирован."""
    return __name__ == "__main__" or globals().get("__spec__") is None


if _is_direct_run():
    main()
