# Copyright (c) 2026 @sitxovski (Discord)
# Проверка целостности слоя навыков (headless): DT_SkillClips ↔ ассеты клипов ↔ BP эффектов.
# НИЧЕГО не меняет — только читает и печатает. Запуск: run_headless.bat Tools\_verify_skill_clips.py
import os

import unreal

DT = "/Game/AsdaAssets/Data/DT_SkillClips.DT_SkillClips"
CHAR = "/Game/AsdaAssets/Models/Skeletal/Characters"
FXPKG = "/Game/AsdaAssets/Effects"
EAL = unreal.EditorAssetLibrary


def log(m):
    unreal.log_warning("[verify-skillclips] %s" % m)


def main():
    dt = unreal.load_asset(DT)
    if not dt:
        log("НЕТ %s" % DT)
        return
    names = unreal.DataTableFunctionLibrary.get_data_table_row_names(dt)
    # Значения берём из CSV-источника таблицы (питон-API чтения строк DataTable в 5.8 нет),
    # а количество строк сверяем с самим ассетом — так проверяются ОБА конца пайплайна.
    import csv as _csv
    HERE = os.path.dirname(os.path.abspath(__file__))
    CSVP = os.path.abspath(os.path.join(HERE, "..", "RawAssets", "Data", "SkillClips.csv"))
    rows = []
    with open(CSVP, encoding="utf-8-sig") as f:
        for d in _csv.DictReader(f):
            rows.append(type("R", (), dict(skill_id=int(d["SkillId"]), clip=d["Clip"],
                                           gender=d["Gender"], anim_idx=int(d["AnimIdx"]),
                                           jebs=d["Jebs"]))())
    log("строк в DT: %d | строк в CSV: %d %s" % (len(names), len(rows),
        "СОВПАДАЕТ" if len(names) == len(rows) else "!! РАСХОЖДЕНИЕ"))

    skills = set()
    miss_anim, miss_fx, no_idx = [], set(), []
    seen_anim, seen_fx = set(), set()
    for r in rows:
        skills.add(r.skill_id)
        idx = r.anim_idx
        g = r.gender or "m"
        if idx < 0:
            no_idx.append((r.skill_id, r.clip))
            continue
        base = "ch1_%s_cremmy_preview" % g
        path = "%s/%s/SkeletalMeshes/%sanim_%d" % (CHAR, base, base, idx)
        if path in seen_anim:
            pass
        else:
            seen_anim.add(path)
            if not EAL.does_asset_exist(path):
                miss_anim.append(path)
        for j in [x for x in (r.jebs or "").split(";") if x]:
            b = os.path.splitext(j)[0].lower().replace("-", "_")
            p = "%s/BP_Fx_%s" % (FXPKG, b)
            if p in seen_fx:
                continue
            seen_fx.add(p)
            if not EAL.does_asset_exist(p):
                miss_fx.add(p)

    log("навыков: %d | уникальных клип-ассетов: %d | уникальных BP эффектов: %d"
        % (len(skills), len(seen_anim), len(seen_fx)))
    log("строк без AnimIdx: %d %s" % (len(no_idx), no_idx[:5]))
    log("НЕТ клип-ассетов: %d %s" % (len(miss_anim), sorted(miss_anim)[:5]))
    log("НЕТ BP эффектов: %d %s" % (len(miss_fx), sorted(miss_fx)[:5]))
    log("ВЕРДИКТ: %s" % ("OK" if (not miss_anim and not miss_fx and not no_idx) else "ЕСТЬ ПРОБЕЛЫ (см. выше)"))


# ★★★ Закон tools-no-import-side-effects: main() только при ПРЯМОМ запуске.
#   Прежняя проверка `__name__ != "_verify_skill_clips"` ловила лишь импорт ровно под этим именем:
#   `import Tools._verify_skill_clips` или `importlib.util.spec_from_file_location("X", ...)` дают другое
#   имя — и main() выстреливал бы. Надёжный признак импорта — `__spec__`: его выставляет ЛЮБАЯ
#   машинерия импорта (import / importlib / пакет / runpy.run_module) и НЕ выставляет прямое
#   исполнение файла (python file.py, UE -ExecutePythonScript). `python -m` даёт "__main__".
def _is_direct_run():
    """True — файл запущен НАПРЯМУЮ, а не импортирован."""
    return __name__ == "__main__" or globals().get("__spec__") is None


if _is_direct_run():
    main()
