# -*- coding: utf-8 -*-
# Copyright (c) 2026 @sitxovski (Discord)
# asda_bloom_verify_all.py — НЕЗАВИСИМАЯ СВЕРКА КАЛИБРОВКИ ОРЕОЛА по ВСЕМ уровням /Game/Map.
#
# ЗАЧЕМ. Раскат (Tools/asda_bloom_rollout.py) один раз уже СОВРАЛ: у alpen сохранение молча упало
#   («MoveFile … Error Code 32» — файл держал другой процесс редактора), а самопроверка «по актору
#   в памяти» этого не увидела. Поэтому здесь НИЧЕГО не пишется и ничего не берётся «из памяти
#   прошлого шага»: свежий редактор ОТКРЫВАЕТ каждый уровень С ДИСКА и печатает фактические числа
#   его PP-волюмов — интенсивность, порог, радиус ореола и все три ручки бликов, вместе с
#   override-флагами (значение без взведённого override движок игнорирует, поэтому печатаем оба).
#
# Эталон интенсивности/порога берётся ОТТУДА ЖЕ, откуда его берёт C++ и раскат:
#   [AsdaGlow] Config/DefaultScalability.ini, ступень 4 (закон общих точек порта).
#   Радиус и блики раскат НЕ ТРОГАЛ — они печатаются как есть, для протокола.
#
# Уровни без PP-волюма (Login, L_Loading, _nltest/lvl) — ожидаемое исходное состояние, а не промах
#   раската: у сцены логина ореол даёт служебный PP_Glow из C++.
#
# Запуск (headless, окон не показывает):
#   python Tools/run_headless_safe.py Tools/asda_bloom_verify_all.py \
#          --marker "BLOOM-VERIFY-FINISHED" --timeout 1800 --idle 300
# Переменные окружения:
#   ASDA_BLOOM_MAPS="alpen||nightvalley"  — сверить только эти уровни (по умолчанию все)
#   ASDA_BLOOM_VERIFY_JSON=<путь>         — куда положить машинный отчёт (по умолчанию рядом с логами)
import json
import os
import sys

try:
    import unreal
except ImportError:
    unreal = None

PROJ = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
INI = os.path.join(PROJ, "Config", "DefaultScalability.ini")
LEVEL = 4                                  # ступень «уровень свечения = 4» = калибровка владельца
ONLY = [s for s in os.environ.get("ASDA_BLOOM_MAPS", "").split("||") if s]
OUT_JSON = os.environ.get("ASDA_BLOOM_VERIFY_JSON",
                          os.path.join(os.path.dirname(PROJ), "Reports", "bloom_verify_all.json"))   # <ROOT>\Reports
MARKER = "BLOOM-VERIFY-FINISHED"           # ASCII-маркер успеха для run_headless_safe.py

# Ручки, которые печатаем у каждого волюма: (имя свойства, имя override-флага, подпись).
KNOBS = [
    ("bloom_intensity",        "override_bloom_intensity",        "интенсивность"),
    ("bloom_threshold",        "override_bloom_threshold",        "порог"),
    ("bloom_size_scale",       "override_bloom_size_scale",       "радиус"),
    ("lens_flare_intensity",   "override_lens_flare_intensity",   "блики: сила"),
    ("lens_flare_threshold",   "override_lens_flare_threshold",   "блики: порог"),
    ("lens_flare_bokeh_size",  "override_lens_flare_bokeh_size",  "блики: боке"),
]


def log(msg):
    if unreal:
        unreal.log("[СВЕРКА] %s" % msg)
    else:
        print("[СВЕРКА] %s" % msg)


def read_step(level):
    """Пара (интенсивность, порог) ступени из [AsdaGlow] — единственный источник эталонных чисел."""
    want = {"intensity@%d" % level: None, "threshold@%d" % level: None}
    section = False
    with open(INI, "r", encoding="utf-8-sig", errors="replace") as f:
        for line in f:
            s = line.strip()
            if s.startswith("["):
                section = (s.lower() == "[asdaglow]")
                continue
            if not section or "=" not in s or s.startswith((";", "#")):
                continue
            k, _, v = s.partition("=")
            if k.strip().lower() in want:
                want[k.strip().lower()] = float(v.strip())
    if None in want.values():
        raise RuntimeError("в [AsdaGlow] %s нет Intensity@%d/Threshold@%d" % (INI, level, level))
    return want["intensity@%d" % level], want["threshold@%d" % level]


def umap_for(package):
    """/Game/Map/alpen/alpen → <проект>/Content/Map/alpen/alpen.umap"""
    return os.path.join(PROJ, "Content", package[len("/Game/"):].replace("/", os.sep) + ".umap")


def prop(struct, name):
    """Свойство структуры или None, если движок его не отдаёт (имя разошлось между версиями)."""
    try:
        return struct.get_editor_property(name)
    except Exception:
        return None


def verify():
    inten, thr = read_step(LEVEL)
    log("эталон из [AsdaGlow] ступень %d: интенсивность=%.3f порог=%.3f" % (LEVEL, inten, thr))
    ar = unreal.AssetRegistryHelpers.get_asset_registry()
    les = unreal.get_editor_subsystem(unreal.LevelEditorSubsystem)
    eas = unreal.get_editor_subsystem(unreal.EditorActorSubsystem)

    packages = sorted(str(ad.package_name) for ad in ar.get_assets_by_path("/Game/Map", recursive=True)
                      if str(ad.asset_class_path.asset_name) == "World")
    report = {"reference": {"intensity": inten, "threshold": thr, "step": LEVEL},
              "levels": [], "ok": 0, "bad": 0, "novol": 0}
    for pkg in packages:
        name = pkg.split("/")[-1]
        if ONLY and name not in ONLY:
            continue
        umap = umap_for(pkg)
        if not os.path.isfile(umap):
            log("%s: файла уровня нет (%s) — пропуск" % (name, umap))
            continue
        # Уровень открывается С ДИСКА заново: предыдущий шаг мог оставить в памяти «правильный» актор.
        les.load_level(pkg)
        vols = [a for a in eas.get_all_level_actors()
                if isinstance(a, unreal.PostProcessVolume) and a.get_actor_label().startswith("PP_")]
        entry = {"level": name, "package": pkg, "umap_size": os.path.getsize(umap), "volumes": []}
        if not vols:
            entry["status"] = "БЕЗ ВОЛЮМА"
            report["novol"] += 1
            report["levels"].append(entry)
            log("%-20s PP-волюма НЕТ" % name)
            continue
        level_ok = True
        for v in vols:
            s = v.get_editor_property("settings")
            vals = {}
            for pname, oname, _cap in KNOBS:
                vals[pname] = prop(s, pname)
                vals[oname] = prop(s, oname)
            gi, gt = vals["bloom_intensity"], vals["bloom_threshold"]
            hit = (bool(vals["override_bloom_intensity"]) and bool(vals["override_bloom_threshold"])
                   and gi is not None and gt is not None
                   and abs(float(gi) - inten) < 1e-4 and abs(float(gt) - thr) < 1e-4)
            level_ok = level_ok and hit
            entry["volumes"].append({"label": v.get_actor_label(), "hit": hit,
                                     "values": {k: (float(x) if isinstance(x, float) else x)
                                                for k, x in vals.items()}})
            log("%-20s %-14s инт %s%-7s порог %s%-7s радиус %s%-7s | блики: сила %s%-7s порог %s%-7s боке %s%-7s | %s"
                % (name, v.get_actor_label(),
                   "*" if vals["override_bloom_intensity"] else " ", _f(gi),
                   "*" if vals["override_bloom_threshold"] else " ", _f(gt),
                   "*" if vals["override_bloom_size_scale"] else " ", _f(vals["bloom_size_scale"]),
                   "*" if vals["override_lens_flare_intensity"] else " ", _f(vals["lens_flare_intensity"]),
                   "*" if vals["override_lens_flare_threshold"] else " ", _f(vals["lens_flare_threshold"]),
                   "*" if vals["override_lens_flare_bokeh_size"] else " ", _f(vals["lens_flare_bokeh_size"]),
                   "СОВПАЛО" if hit else "НЕ СОВПАЛО"))
        entry["status"] = "СОВПАЛО" if level_ok else "НЕ СОВПАЛО"
        report["ok" if level_ok else "bad"] += 1
        report["levels"].append(entry)

    log("ИТОГО: совпало %d, НЕ совпало %d, без волюма %d, уровней всего %d"
        % (report["ok"], report["bad"], report["novol"], len(report["levels"])))
    if report["bad"]:
        for e in report["levels"]:
            if e["status"] == "НЕ СОВПАЛО":
                unreal.log_error("[СВЕРКА] РАСХОЖДЕНИЕ: %s" % e["level"])
    try:
        os.makedirs(os.path.dirname(OUT_JSON), exist_ok=True)
        with open(OUT_JSON, "w", encoding="utf-8") as f:
            json.dump(report, f, ensure_ascii=False, indent=1)
        log("машинный отчёт: %s" % OUT_JSON)
    except Exception as ex:
        log("отчёт не записан: %s" % ex)
    # Маркер печатаем ВСЕГДА: он означает «сверка доведена до конца», а не «всё совпало».
    log(MARKER)


def _f(x):
    """Число в строку так, чтобы None не ронял форматирование."""
    return "нет" if x is None else ("%.3f" % float(x))


def _script_main():
    if unreal is None:
        print("нужен редактор: python Tools/run_headless_safe.py Tools/asda_bloom_verify_all.py "
              "--marker \"%s\"" % MARKER)
        sys.exit(2)
    verify()


# ★ ЗАКОН [[tools-no-import-side-effects]]: при импорте модуль НИЧЕГО не делает.
if __name__ == "__main__" or globals().get("__spec__") is None:
    _script_main()
