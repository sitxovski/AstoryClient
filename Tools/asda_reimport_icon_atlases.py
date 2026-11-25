# Copyright (c) 2026 @sitxovski (Discord)
# asda_reimport_icon_atlases.py — точечный импорт/переимпорт АТЛАСОВ ИКОНОК ПРЕДМЕТОВ в /Game/AsdaAssets/Textures/JUI.
#
# Зачем (история): item_img_table.bin у сборок MINE / MINE_WoS / AsdaChroniclesInterlude ПОБАЙТОВО ОДИНАКОВ, а
# художественные атласы иконок — нет: в MINE (откуда собирались RawAssets) часть ячеек залита сплошным белым.
# Пустые ячейки долиты из AsdaChroniclesInterlude (Reports/item_icons_missing/merged_cells.csv). 30.09.2026 тем же
# путём добавлены атласы оригинальной сборки, которых в порту не было: s_item_me_7, s_item_me_9, s_item_l_78
# (Reports/owner_0927/armor_75696.md §9–11). Исходник — PNG в RawAssets/Textures/JUI (из .tex через asda_fmt_tex).
#
# ★★ НАСТРОЙКИ ТЕКСТУРЫ НЕ ЗАХАРДКОЖЕНЫ (ревизия 9–10 armor_75696, 30.09.2026). Раньше скрипт жёстко ставил
#   TC_EDITOR_ICON / TMGS_NO_MIPMAPS / sRGB / never_stream, а все 116 атласов иконок в Content стоят на
#   TMGS_FROM_TEXTURE_GROUP — новые атласы выходили не такими, как старые. Теперь ВСЕ свойства (REF_PROPS) копируются
#   с эталонного ассета: ASDA_ICON_REF (по умолчанию /Game/AsdaAssets/Textures/JUI/s_item_l_18).
# ★★★ ПОЛИТИКА МИПОВ: 1:1 С ОРИГИНАЛОМ, БЕЗ МИПОВ (решение владельца 06.10.2026). У оригинала в заголовке .tex
#   (+0x14 MipLevels) у ВСЕХ s_item_*/premium_icon*/s_fish_l_*/ag_item_l_0 стоит 1. Все 125 атласов иконок предметов в
#   Textures/JUI (вкл. эталон s_item_l_18) переведены на TMGS_NO_MIPMAPS (Reports/owner_1006/icon_atlases_nomips.md),
#   поэтому копирование с эталона даёт новым импортам NO_MIPMAPS. Ключ --mips-from-tex (ASDA_ICON_MIPS_FROM_TEX=1) —
#   то же по заголовку оригинального .tex (1 -> TMGS_NO_MIPMAPS), как asda_gacha_fix_texture_mips.py.
#   ★ Защита: если эталон окажется НЕ на NO_MIPMAPS, а --mips-from-tex не задан — mip_gen_settings всё равно NO_MIPMAPS
#     (с предупреждением в лог): атлас иконок с мипами расходится с оригиналом.
#
# ★ ЗАЩИТА: существующий ассет по умолчанию ПРОПУСКАЕТСЯ. Переимпорт — только ASDA_ICON_REPLACE=1 (или --replace), и
#   перед ним копия .uasset в _backup/<дата>_icon_atlas/ с проверкой размера. После сохранения — проверка по факту:
#   дата .uasset изменилась, перечитанные свойства = целевым. «ГОТОВО» печатается только при нуле ошибок.
#
# Параметры (переменные окружения; при прямом запуске те же ключи в argv):
#   ASDA_ICON_ATLASES=a,b   — какие атласы (по умолчанию список DEFAULT_ATLASES ниже)
#   ASDA_ICON_REF=/Game/... — эталонный ассет для настроек
#   ASDA_ICON_REPLACE=1     — разрешить переимпорт существующих (с бэкапом)
#   ASDA_ICON_DRY_RUN=1     — только напечатать план (новый/существует/пропуск) и будущие свойства
#   ASDA_ICON_MIPS_FROM_TEX=1 — мипы по заголовку оригинального .tex
#   ASDA_ICON_DEST=/Game/... , ASDA_ICON_SRC=<каталог PNG> — другая папка назначения/исходников (песочница проверки)
# Запуск: run_headless_safe.py asda_reimport_icon_atlases.py --expect-asset /Game/AsdaAssets/Textures/JUI/<имя>
# Побочных эффектов при импорте модуля нет (закон tools-no-import-side-effects): работа только в main().
import datetime
import os
import shutil
import sys

SRC = r"@@ASTORY_DEV_ROOT@@\AstoryClient\RawAssets\Textures\JUI"
DEST = "/Game/AsdaAssets/Textures/JUI"
CONTENT_DIR = r"@@ASTORY_DEV_ROOT@@\AstoryClient\Content\AsdaAssets\Textures\JUI"
BACKUP_ROOT = r"@@ASTORY_DEV_ROOT@@\_backup"
DEFAULT_REF = "/Game/AsdaAssets/Textures/JUI/s_item_l_18"

# атласы, у которых слияние добавило ячейки (merge_atlases → merged_cells.csv) — список ПО УМОЛЧАНИЮ
DEFAULT_ATLASES = [
    "premium_icon1", "premium_icon2", "s_item_l_113", "s_item_l_25", "s_item_l_43",
    "s_item_l_59", "s_item_l_68", "s_item_l_71", "s_item_l_77", "s_item_l_95", "s_item_s_02",
]
ATLASES = DEFAULT_ATLASES   # совместимость со старыми обёртками (подмена глобала); новым — ASDA_ICON_ATLASES

# свойства Texture2D, копируемые с эталона (все, что задают вид/качество иконки)
REF_PROPS = ["lod_group", "mip_gen_settings", "compression_settings", "srgb", "filter", "never_stream",
             "address_x", "address_y", "lod_bias", "power_of_two_mode", "max_texture_size", "compression_quality",
             "mip_load_options", "virtual_texture_streaming", "adjust_brightness", "adjust_brightness_curve",
             "adjust_saturation", "adjust_vibrance", "adjust_rgb_curve", "adjust_hue", "adjust_min_alpha",
             "adjust_max_alpha", "compression_no_alpha", "preserve_border", "flip_green_channel",
             "lossy_compression_amount", "downscale", "defer_compression", "chroma_key_texture"]


def _flag(env, arg, argv):
    return os.environ.get(env, "") == "1" or arg in argv


def _tex_mip_levels(name):
    """MipLevels из заголовка ОРИГИНАЛЬНОГО .tex (juidata MINE, затем XYT/Arabic — только чтение); None — нет файла."""
    import zipfile
    sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
    import asda_fmt_tex as _FT
    import asda_paths as _P
    roots = [_P.JUIDATA, r"@@ASTORY_DEV_ROOT@@\Donors\XYT\juidata", r"@@ASTORY_DEV_ROOT@@\Donors\ArabicASDAStory\juidata"]
    for root in roots:
        for dp, _dn, fns in os.walk(root):
            for fn in fns:
                low = fn.lower()
                p = os.path.join(dp, fn)
                if low == name.lower() + ".tex":
                    with open(p, "rb") as f:
                        h = _FT.read_header(f.read(_FT.HDR))
                    return (h.mips, p) if h else None
                if low.endswith(".btz"):
                    try:
                        z = zipfile.ZipFile(p)
                    except Exception:
                        continue
                    for n in z.namelist():
                        if os.path.basename(n).lower() == name.lower() + ".tex":
                            h = _FT.read_header(z.read(n)[:_FT.HDR])
                            return (h.mips, p + ":" + n) if h else None
    return None


_ADDR = None


def _same(a, b):
    """Равенство значений свойств. ★ ревизия 11 СРЕДН-1: сравнивать через `==` (структуры UE сравниваются по
    значению); `str()` структуры содержит адрес объекта «(0x…)» и давал ложное «свойства != целевым».
    Если `==` бросает исключение — сравниваем строки с вырезанными адресами."""
    global _ADDR
    import re
    _ADDR = _ADDR or re.compile(r"\(0x[0-9A-Fa-f]+\)")
    try:
        if bool(a == b):
            return True
    except Exception:
        pass
    # ★ песочница 30.09: у PerPlatformFloat (`downscale`) `==` двух РАЗНЫХ объектов даёт False при равных значениях.
    #   Для структур сравниваем текстовое представление значения: export_text(), иначе str() без адреса «(0x…)».
    for fn in ("export_text",):
        if hasattr(a, fn) and hasattr(b, fn):
            try:
                return getattr(a, fn)() == getattr(b, fn)()
            except Exception:
                pass
    return _ADDR.sub("", str(a)) == _ADDR.sub("", str(b))


def _content_dir(dest):
    """/Game/<путь> -> <проект>/Content/<путь> (для проверки даты .uasset и бэкапа)."""
    root = os.path.normpath(os.path.join(os.path.dirname(os.path.abspath(__file__)), "..", "Content"))
    return os.path.join(root, *dest[len("/Game/"):].split("/")) if dest.startswith("/Game/") else CONTENT_DIR


def main(atlases=None, argv=None):
    """Работа скрипта (закон tools-no-import-side-effects: при import модуль ничего не делает)."""
    import unreal
    EAL = unreal.EditorAssetLibrary
    argv = sys.argv[1:] if argv is None else argv

    def log(m): unreal.log("[icon-atlas] " + str(m))
    def err(m): unreal.log_error("[icon-atlas] " + str(m))

    if atlases is None:
        env = os.environ.get("ASDA_ICON_ATLASES", "").strip()
        atlases = [a.strip() for a in env.split(",") if a.strip()] if env else list(ATLASES)
    dry = _flag("ASDA_ICON_DRY_RUN", "--dry-run", argv)
    replace = _flag("ASDA_ICON_REPLACE", "--replace", argv)
    mips_from_tex = _flag("ASDA_ICON_MIPS_FROM_TEX", "--mips-from-tex", argv)
    ref_path = os.environ.get("ASDA_ICON_REF", DEFAULT_REF)
    dest = os.environ.get("ASDA_ICON_DEST", DEST).rstrip("/")      # песочница: /Game/_tmp_…
    src = os.environ.get("ASDA_ICON_SRC", SRC)                      # каталог PNG
    content_dir = _content_dir(dest)
    errors = 0

    ref = unreal.load_asset(ref_path)
    if not isinstance(ref, unreal.Texture2D):
        err("эталонный ассет %s не найден или не Texture2D — стоп" % ref_path)
        return 1
    ref_vals = {}
    for p in REF_PROPS:
        try:
            ref_vals[p] = ref.get_editor_property(p)
        except Exception:
            pass   # свойства нет в этой версии движка — не копируем
    log("эталон настроек: %s (%d свойств)%s" % (ref_path, len(ref_vals), ", мипы — по .tex" if mips_from_tex else ""))

    plan = []
    for a in atlases:
        png = os.path.join(src, a + ".png")
        exists = EAL.does_asset_exist(dest + "/" + a)
        if not os.path.exists(png):
            plan.append((a, "НЕТ PNG", png)); continue
        if exists and not replace:
            plan.append((a, "существует — пропуск (переимпорт только с ASDA_ICON_REPLACE=1)", png)); continue
        plan.append((a, "переимпорт" if exists else "новый", png))

    def target_vals(a):
        vals = dict(ref_vals)
        if mips_from_tex:
            info = _tex_mip_levels(a)
            if info is None:
                return None, "нет оригинального .tex для --mips-from-tex"
            mips, src = info
            vals["mip_gen_settings"] = (unreal.TextureMipGenSettings.TMGS_NO_MIPMAPS if mips == 1
                                        else unreal.TextureMipGenSettings.TMGS_FROM_TEXTURE_GROUP)
            return vals, "MipLevels=%d (%s)" % (mips, src)
        # ★ 06.10.2026: атласы иконок — 1:1 с оригиналом, без мипов, даже если эталон вдруг с мипами
        if vals.get("mip_gen_settings") != unreal.TextureMipGenSettings.TMGS_NO_MIPMAPS:
            vals["mip_gen_settings"] = unreal.TextureMipGenSettings.TMGS_NO_MIPMAPS
            return vals, "эталон с мипами — принудительно NO_MIPMAPS (оригинал MipLevels=1)"
        return vals, ""

    for a, what, _png in plan:
        log("план: %-16s %s" % (a, what))
    if dry:
        for a, what, _png in plan:
            if what in ("новый", "переимпорт"):
                vals, note = target_vals(a)
                log("будущие свойства %s: %s %s" % (a, {k: str(v) for k, v in (vals or {}).items()}, note))
        log("DRY-RUN: ничего не импортировано")
        return 0

    todo = [(a, png) for a, what, png in plan if what in ("новый", "переимпорт")]
    if not todo:
        log("нечего импортировать")
        log("ГОТОВО")
        return 0
    # бэкап существующих перед переимпортом
    stamp = datetime.date.today().isoformat()
    before = {}
    for a, _png in todo:
        f = os.path.join(content_dir, a + ".uasset")
        before[a] = os.path.getmtime(f) if os.path.exists(f) else None
        if os.path.exists(f):
            bdir = os.path.join(BACKUP_ROOT, stamp + "_icon_atlas")
            os.makedirs(bdir, exist_ok=True)
            dst = os.path.join(bdir, a + ".uasset")
            shutil.copy2(f, dst)
            if os.path.getsize(dst) != os.path.getsize(f):
                err("бэкап %s не совпал по размеру — стоп" % a)
                return 1
            log("бэкап %s -> %s" % (a, dst))

    # ★ 29.10.2026 (wod_items): путь Tools раньше вставлялся только в _tex_mip_levels (ветка --mips-from-tex) — без неё
    #   импорт падал ModuleNotFoundError. Вставляем здесь.
    sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
    import asda_model_import_ue as _MIU   # ★ дедуп №2 Т17: общий импорт пачкой (путь Tools вставлен в main)
    assets = _MIU.import_batch([png for _a, png in todo], dest, bool(replace))
    by_name = {x.get_name(): x for x in assets if isinstance(x, unreal.Texture2D)}
    for a, _png in todo:
        t = by_name.get(a) or unreal.load_asset(dest + "/" + a)
        if not isinstance(t, unreal.Texture2D):
            err("%s: импорт не дал Texture2D" % a); errors += 1; continue
        vals, note = target_vals(a)
        if vals is None:
            err("%s: %s" % (a, note)); errors += 1; continue
        t.set_editor_properties(vals)
        unreal.log("ASDA_WRITES_ASSET: %s/%s" % (dest, a))
        if not EAL.save_loaded_asset(t, only_if_is_dirty=False):
            err("%s: сохранение вернуло False" % a); errors += 1; continue
        f = os.path.join(content_dir, a + ".uasset")
        if not os.path.exists(f) or (before[a] is not None and os.path.getmtime(f) <= before[a]):
            err("%s: дата .uasset не изменилась — сохранение молча не прошло" % a); errors += 1; continue
        t2 = unreal.load_asset(dest + "/" + a)
        bad = [k for k, v in vals.items() if not _same(t2.get_editor_property(k), v)]
        if bad:
            err("%s: после сохранения свойства != целевым: %s" % (a, bad)); errors += 1; continue
        log("%s: импортирован, свойства = эталону %s %s" % (a, ref_path, note))
    log("импортировано %d, ошибок %d" % (len(todo) - errors, errors))
    if errors == 0:
        log("ГОТОВО")
    return 1 if errors else 0


if __name__ == "__main__":
    main()
