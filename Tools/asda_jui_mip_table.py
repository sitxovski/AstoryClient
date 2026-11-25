# Copyright (c) 2026 @sitxovski (Discord)
# asda_jui_mip_table.py — таблица эталона «мипы JUI-текстур оригинала» → Tools/data/asda_jui_original_miplevels.json.
# ★ 06.10.2026 (Reports/owner_1006/texture_importers_fix.md; ревизия ui_icons_nomips_redirectors_REVIEW.md, СРЕДН-1).
#
# Источник — ТОЛЬКО чтение файлов оригинальных клиентов (сам клиент не запускается):
#   juidata трёх сборок — AsdaChroniclesInterlude_MINE, XYT, Arabic ASDA Story; loose *.tex и *.tex внутри *.btz (zip).
# Поле: заголовок .TEX +0x14 MipLevels (asda_fmt_tex.read_header → .mips) — тот же метод, что в
#   Reports/owner_1006/icon_atlases_nomips.md и ui_icons_nomips_redirectors.md.
# Ключ таблицы — имя ассета UE (то же преобразование имени, что даёт импорт PNG: asda_login_guard.ue_import_package_path),
#   в нижнем регистре (имена пакетов UE регистронезависимы).
# Правило для импортёра (import_jui_textures.py): NO_MIPMAPS ставится ТОЛЬКО если nomips == true, т.е. MipLevels == 1
#   во ВСЕХ сборках, где файл есть (или явное переопределение PORT_OVERRIDES для имён, которых в оригинале нет). Разные значения между сборками → nomips = false и запись в conflicts (решать вручную).
#
# Запуск (обычный Python, без UE):  python asda_jui_mip_table.py [--check] [--out путь]
#   --check — не писать файл, а сравнить с сохранённой таблицей (код 1 при различии).
import os, sys, json, glob, zipfile, argparse

HERE = os.path.dirname(os.path.abspath(__file__))
OUT_DEFAULT = os.path.join(HERE, "data", "asda_jui_original_miplevels.json")   # путь таблицы эталона
DEST = "/Game/AsdaAssets/Textures/JUI"                                          # папка JUI-текстур порта
CLIENT_DIRS = ("AsdaChroniclesInterlude_MINE", "XYT", "Arabic ASDA Story")      # сборки-эталоны (порядок = приоритет вывода; имена = метки в json)
# ★ 17.10.2026 реорганизация: сборки лежат в <ROOT>\Donors под новыми именами папок; метки в json прежние (совместимость таблицы)
CLIENT_FOLDER = {"AsdaChroniclesInterlude_MINE": "AsdaChroniclesInterlude_MINE", "XYT": "XYT", "Arabic ASDA Story": "ArabicASDAStory"}
FORMAT, VERSION = "asda_jui_original_miplevels", 1                              # подпись формата файла

# ★ 06.10.2026 (решение координатора в рамках решения владельца «иконки 1:1 без мипов»; texture_importers_fix.md, раздел 5):
#   ЯВНЫЕ ПЕРЕОПРЕДЕЛЕНИЯ «только порт» — атласов нет ни в одной из трёх сборок (в оригинале .TEX не найден), в порту они
#   стоят на NO_MIPMAPS и должны остаться такими при пересоздании. В таблице помечаются override=true, mips={} .
PORT_OVERRIDES = {
    "tpl_talkwin": {"name": "tpl_talkwin", "nomips": True,
                    "why": "только порт: в juidata _MINE/XYT/Arabic .TEX нет; атлас окна логина, в порту NO_MIPMAPS — сохранить"},
    "tpl_yellowwin": {"name": "tpl_yellowwin", "nomips": True,
                      "why": "только порт: в juidata _MINE/XYT/Arabic .TEX нет; атлас окна логина, в порту NO_MIPMAPS — сохранить"},
    # ★ 23.10.2026: атласы иконок новых маунтов донора AsdaFableClassic (Reports/fable_mounts/FABLE_MOUNTS.md);
    #   в трёх сборках-эталонах их нет, у донора juidata\sIcon\Texture.btz MipLevels == 1 (как у premium_icon/1/2).
    "premium_icon3": {"name": "premium_icon3", "nomips": True,
                      "why": "донор AsdaFableClassic juidata\\sIcon premium_icon3.tex MipLevels=1; иконки маунтов 0054/0055/0056-0058"},
    "premium_icon4": {"name": "premium_icon4", "nomips": True,
                      "why": "донор AsdaFableClassic juidata\\sIcon premium_icon4.tex MipLevels=1; иконки маунтов 0059-0080"},
}

# ★ 29.10.2026: атласы иконок предметов донора WorldOfDreams, заведённые под именами wod_<имя> (одноимённые наши атласы — другое
#   содержимое; Reports/donor_wod/_research/icons.md). Список строит `asda_wod_items.py icons` (data/wod/wod_icon_atlases.json).
#   У 9 DDS-атласов донора MipLevels = 10 — по политике порта «иконки 1:1 без мипов» (решение владельца 06.10) всё равно NO_MIPMAPS.
_WOD_ICONS = os.path.join(os.path.dirname(os.path.abspath(__file__)), "data", "wod", "wod_icon_atlases.json")
if os.path.isfile(_WOD_ICONS):
    with open(_WOD_ICONS, encoding="utf-8") as _f:
        for _nm in json.load(_f)["atlases"]:
            PORT_OVERRIDES[_nm] = {"name": _nm, "nomips": True,
                                   "why": "донор WorldOfDreams juidata\\sIcon|gIcon Texture.btz; иконки предметов WoD (asda_wod_items.py)"}


def _scan_client(root, FT, LG):
    """Все .tex одной сборки (loose + в .btz) → {ключ: {"name", "mips", "src"}}. Первое вхождение имени выигрывает
    (loose раньше архивов — как asda_extract_all_jui_tex.py)."""
    res = {}

    def put(fname, data, src):
        stem_png = os.path.splitext(os.path.basename(fname))[0] + ".png"
        name = LG.ue_import_package_path(DEST, stem_png).rsplit("/", 1)[-1]
        key = name.lower()
        if key in res:
            return
        h = FT.read_header(data[:FT.HDR])
        res[key] = {"name": name, "mips": (h.mips if h else None), "src": src}

    jd = os.path.join(root, "juidata")
    loose = sorted(set(glob.glob(os.path.join(jd, "**", "*.tex"), recursive=True) +
                       glob.glob(os.path.join(jd, "**", "*.TEX"), recursive=True)))
    for p in loose:
        with open(p, "rb") as f:
            put(p, f.read(FT.HDR), os.path.relpath(p, root))
    for arc in sorted(glob.glob(os.path.join(jd, "**", "*.btz"), recursive=True)):
        try:
            z = zipfile.ZipFile(arc)
        except Exception:
            continue
        for n in z.namelist():
            if n.lower().endswith(".tex"):
                with z.open(n) as f:
                    put(n, f.read(FT.HDR), os.path.relpath(arc, root) + "!" + n)
    return res


def build(area_root):
    """Собрать таблицу эталона (dict для json)."""
    sys.path.insert(0, HERE)
    import asda_fmt_tex as FT
    import asda_login_guard as LG
    per = {c: _scan_client(os.path.join(area_root, "Donors", CLIENT_FOLDER[c]), FT, LG) for c in CLIENT_DIRS}
    keys = sorted(set().union(*[set(v) for v in per.values()]))
    entries, conflicts = {}, []
    for k in keys:
        vals = {c: per[c][k]["mips"] for c in CLIENT_DIRS if k in per[c]}
        name = next(per[c][k]["name"] for c in CLIENT_DIRS if k in per[c])
        known = set(v for v in vals.values() if v is not None)
        nomips = (known == {1})
        if len(known) > 1:
            conflicts.append(k)
        entries[k] = {"name": name, "mips": vals, "nomips": nomips}
    for k, ov in PORT_OVERRIDES.items():   # ★ 06.10.2026: переопределения только для имён, которых в оригинале НЕТ
        if k in entries:
            raise RuntimeError("переопределение %s: имя есть в оригинале — переопределять оригинал нельзя" % k)
        entries[k] = {"name": ov["name"], "mips": {}, "nomips": ov["nomips"], "override": True, "why": ov["why"]}
    counts = {c: len(per[c]) for c in CLIENT_DIRS}
    return {
        "format": FORMAT, "version": VERSION,
        "note": "MipLevels (+0x14 заголовка .TEX) JUI-текстур оригинала; nomips=true ⇔ MipLevels==1 во всех сборках, где есть файл. "
                "Генератор: Tools/asda_jui_mip_table.py. Отчёт: Reports/owner_1006/texture_importers_fix.md",
        "clients": list(CLIENT_DIRS), "files_per_client": counts,
        "overrides": sorted(PORT_OVERRIDES), "total": len(entries), "nomips_true": sum(1 for e in entries.values() if e["nomips"]),
        "conflicts": conflicts, "entries": entries,
    }


def load_table(path=OUT_DEFAULT):
    """Прочитать таблицу эталона → {ключ_нижний_регистр: запись}. Неверный формат — исключение (fail-closed)."""
    with open(path, encoding="utf-8") as f:
        d = json.load(f)
    if d.get("format") != FORMAT or d.get("version") != VERSION or not isinstance(d.get("entries"), dict):
        raise RuntimeError("таблица эталона мипов %s: неверный формат" % path)
    return d["entries"]


def main():
    """Работа скрипта (импорт модуля ничего не делает — закон [[tools-no-import-side-effects]])."""
    try: sys.stdout.reconfigure(encoding="utf-8", errors="replace")
    except Exception: pass
    ap = argparse.ArgumentParser()
    ap.add_argument("--out", default=OUT_DEFAULT)
    ap.add_argument("--check", action="store_true")
    ap.add_argument("--root", default=os.environ.get("ASDA_ROOT", r"@@ASTORY_DEV_ROOT@@"))
    a = ap.parse_args()
    d = build(a.root)
    print("сборки: %s; имён всего %d, MipLevels==1 везде: %d, конфликтов между сборками: %d"
          % (d["files_per_client"], d["total"], d["nomips_true"], len(d["conflicts"])))
    if d["conflicts"]:
        print("конфликты: " + ", ".join(d["conflicts"]))
    if a.check:
        old = json.load(open(a.out, encoding="utf-8"))
        same = old.get("entries") == d["entries"]
        print("сверка с %s: %s" % (a.out, "совпадает" if same else "РАЗЛИЧАЕТСЯ"))
        return 0 if same else 1
    os.makedirs(os.path.dirname(a.out), exist_ok=True)
    with open(a.out, "w", encoding="utf-8") as f:
        json.dump(d, f, ensure_ascii=False, indent=1, sort_keys=True)
    print("записано: " + a.out)
    return 0


# ★ ЗАКОН [[tools-no-import-side-effects]]: при импорте модуль НИЧЕГО не делает.
if __name__ == "__main__":
    sys.exit(main())
