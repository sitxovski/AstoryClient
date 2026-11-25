# Copyright (c) 2026 @sitxovski (Discord)
# asda_import_textures.py — импорт распакованных ресурсов (RawAssets/Textures) в проект UE
# с РАСКЛАДКОЙ ПО ТИПУ/ИСТОЧНИКУ (а не зеркалированием путей игры).
# Предварительно: Tools/asda_extract_textures.py (создаёт RawAssets/Textures/**/*.dds|png|bmp|tga).
# Запуск: в редакторе Tools -> Execute Python Script.
#
# Раскладка под /Game/AsdaAssets/:
#   Fonts/                 — шрифтовые атласы (font.*)
#   Textures/Maps/<map>/   — текстуры карт (террейн, объекты, вода, небо)
#   Textures/Items/        — Data/Item
#   Textures/Effects/      — Data/Effect
#   Textures/Characters/   — Data/Character
#   Textures/Mobs/         — Data/Mob
#   Textures/NPC/          — Data/NPC
#   Textures/Data/         — прочее из Data
#   Textures/System/       — system
#   Textures/Misc/         — всё остальное (★ 06.10.2026: кроме папок со своим импортёром, см. OWNED_ELSEWHERE)
#
# ВАЖНО: UE5.8 Interchange-DDS не поддерживает BC1/2/3 (DXT1/3/5) — отключаем Interchange,
# чтобы импорт шёл через legacy UTextureFactory (DXT грузит корректно).
#
# ★ 06.10.2026 (Reports/owner_1006/texture_importers_fix.md; ревизия ui_icons_nomips_redirectors_REVIEW.md, СРЕДН-1):
#   подпапки RawAssets/Textures, у которых есть СВОЙ импортёр (OWNED_ELSEWHERE: JUI, MapImage, Probe, Util, _NormalStudio),
#   и любые подпапки `_bak_*` этот скрипт НЕ раскладывает. БЫЛО: правила для них в classify() не было, они уходили в
#   «всё остальное» = Textures/Misc/<папка>. После удаления редиректоров Misc 06.10 следующий запуск завёл бы
#   ~2212 дублей JUI-иконок в Misc/JUI (с мипами и автоопределением карты нормалей — состояние 17.08),
#   + 46 дублей миникарт (Misc/MapImage), 2 пробы гачи (Misc/Probe), 1 текстуру NormalStudio.
#   Режим --dry-run (или ASDA_TEX_DRY_RUN=1 — run_headless_safe аргументы скрипту не передаёт): только план,
#   ничего не импортируется и не сохраняется. Вне UE сухой прогон тоже работает (наличие ассета — по файлу .uasset).
#   Импорт модуля ничего не делает и `unreal` не требует (закон [[tools-no-import-side-effects]]).
# ★ 06.10.2026 (там же, найдено сухим прогоном): проверка «ассет уже есть» шла по СЫРОМУ имени файла, а UE заводит
#   пакет по санитизированному (`Mountain Flamion` → `Mountain_Flamion`, asda_login_guard.ue_import_package_path) —
#   86 существующих текстур считались «новыми» и были бы перезалиты поверх с перезаписью настроек. Теперь путь пакета
#   считается как у UE. Ещё 66 файлов (миникарты map/<карта>/<карта>.png, SILHOUTTE_TEX_WHITE, NPC/Image) уже живут
#   Texture2D с тем же именем в ДРУГОЙ папке — такие не заводятся (дубль), их список печатается; завести всё же —
#   ASDA_TEX_ALLOW_DUP=1.
import os, sys

RAW_ROOT  = r"@@ASTORY_DEV_ROOT@@\AstoryClient\RawAssets\Textures"
CONTENT_DIR = r"@@ASTORY_DEV_ROOT@@\AstoryClient\Content"   # ★ 06.10.2026: /Game на диске — для сухого прогона вне UE
ASSET_ROOT = "/Game/AsdaAssets"
EXTS = (".png", ".bmp", ".tga", ".jpg")  # .dds сконвертированы в .png (asda_dds_to_png.py); UE PNG жрёт нативно
PURGE_FIRST = False  # инкрементально (зайдут только новые: Char/Mob). True = снести всё и переимпортировать
PURGE_ROOTS = [ASSET_ROOT + "/Textures", ASSET_ROOT + "/Fonts"]

# ★ 06.10.2026: верхние папки RawAssets/Textures (нижний регистр) → импортёр, который ими владеет. Сюда скрипт НЕ лезет.
OWNED_ELSEWHERE = {
    "jui": "import_jui_textures.py -> /Game/AsdaAssets/Textures/JUI (мипы по таблице оригинала)",
    "mapimage": "asda_import_minimap_textures.py -> /Game/AsdaAssets/Textures/MapImage",
    "probe": "asda_gacha_color_check.py -> /Game/AsdaAssets/Probe",
    "util": "import_util_tex.py -> /Game/AsdaAssets/Textures/Util",
    "_normalstudio": "asda_build_materials_lit.py -> /Game/Materials/T_NS_DefaultRE",
}
BAK_PREFIX = "_bak_"            # ★ 06.10.2026: резервные подпапки (напр. JUI/_bak_icons_2026-08-01) не импортируются
MISC_PREFIX = "Textures/Misc"   # ★ 06.10.2026: «всё остальное»; после правки сюда не должно попадать ни одного файла

atools = None   # AssetTools — заводится в main() (импорт модуля без `unreal`)
EAL = None      # EditorAssetLibrary — то же; None вне UE (сухой прогон по файлам)
_LG = None      # asda_login_guard — то же
DUP_SKIPPED = []   # ★ 06.10.2026: (пакет, где уже есть) — не заведены как дубли


def _dry_run():
    """Сухой прогон: ключ --dry-run в argv или ASDA_TEX_DRY_RUN=1."""
    return "--dry-run" in sys.argv or os.environ.get("ASDA_TEX_DRY_RUN") == "1"


def _log(msg):
    """Лог: в UE — unreal.log, вне UE — print."""
    try:
        import unreal
        unreal.log(msg)
    except ImportError:
        print(msg)


def _asset_exists(path):
    """Есть ли ассет по пути /Game/... В UE — реестр; вне UE — файл .uasset в Content (только для сухого прогона)."""
    if EAL is not None:
        return EAL.does_asset_exist(path)
    rel = path[len("/Game/"):].replace("/", os.sep)
    return os.path.isfile(os.path.join(CONTENT_DIR, rel + ".uasset"))


_DUP_INDEX = None   # имя ассета (нижний регистр) → пути Texture2D в /Game; строится один раз (★ 06.10.2026)


def _dup_index():
    """Индекс имён Texture2D проекта. В UE — реестр (класс Texture2D); вне UE — имена .uasset в Content (приближённо)."""
    global _DUP_INDEX
    if _DUP_INDEX is not None:
        return _DUP_INDEX
    idx = {}
    if EAL is not None:
        import unreal
        ar = unreal.AssetRegistryHelpers.get_asset_registry()
        for ad in ar.get_assets_by_class(unreal.TopLevelAssetPath("/Script/Engine", "Texture2D"), False):
            idx.setdefault(str(ad.asset_name).lower(), []).append(str(ad.package_name))
    else:
        for dp, _dn, fn in os.walk(CONTENT_DIR):
            for f in fn:
                if f.endswith(".uasset"):
                    rel = os.path.relpath(os.path.join(dp, f[:-7]), CONTENT_DIR).replace(os.sep, "/")
                    idx.setdefault(f[:-7].lower(), []).append("/Game/" + rel)
    _DUP_INDEX = idx
    return idx


def _pkg(dest, f):
    """Путь пакета, который UE заведёт при импорте файла f в папку dest (★ 06.10.2026: санитизация имени как у UE)."""
    return _LG.ue_import_package_path(dest, f)


def skip_reason(rel_dir):
    """Почему папку (путь относительно RAW_ROOT) скрипт не раскладывает; None — раскладывает. ★ 06.10.2026."""
    low = [] if rel_dir == "." else [p.lower() for p in rel_dir.replace("\\", "/").split("/")]
    if any(p.startswith(BAK_PREFIX) for p in low):
        return "резервная подпапка _bak_*"
    if low and low[0] in OWNED_ELSEWHERE:
        return "свой импортёр: " + OWNED_ELSEWHERE[low[0]]
    return None


def disable_interchange_dds():
    import unreal
    for cmd in ("Interchange.FeatureFlags.Import.DDS 0",
                "Interchange.FeatureFlags.Import.Enabled 0"):
        try:
            unreal.SystemLibrary.execute_console_command(None, cmd)
        except Exception as e:
            unreal.log_warning("CVar '%s' не применён: %s" % (cmd, e))

def classify(rel_dir, fn):
    """rel_dir — путь относительно RAW_ROOT (как в игре). Возвращает категорию-папку под ASSET_ROOT;
    None — папку этот скрипт не раскладывает (★ 06.10.2026: skip_reason — JUI и др. со своим импортёром, `_bak_*`)."""
    if skip_reason(rel_dir):
        return None
    base = os.path.splitext(fn)[0].lower()
    parts = [] if rel_dir == "." else rel_dir.replace("\\", "/").split("/")
    low = [p.lower() for p in parts]

    if base == "font":                       # шрифтовой атлас — отдельно
        return "Fonts"

    def tail(skip):                          # хвост пути после skip сегментов
        return "/".join(parts[skip:])

    if low[:1] == ["map"]:
        t = tail(1); return "Textures/Maps/" + t if t else "Textures/Maps"
    if low[:2] == ["data", "item"]:
        t = tail(2); return "Textures/Items/" + t if t else "Textures/Items"
    if low[:2] == ["data", "effect"]:
        t = tail(2); return "Textures/Effects/" + t if t else "Textures/Effects"
    if low[:2] == ["data", "character"]:
        t = tail(2); return "Textures/Characters/" + t if t else "Textures/Characters"
    if low[:2] == ["data", "mob"]:
        t = tail(2); return "Textures/Mobs/" + t if t else "Textures/Mobs"
    if low[:2] == ["data", "npc"]:
        t = tail(2); return "Textures/NPC/" + t if t else "Textures/NPC"
    if low[:1] == ["data"]:
        t = tail(1); return "Textures/Data/" + t if t else "Textures/Data"
    if low[:1] == ["system"]:
        t = tail(1); return "Textures/System/" + t if t else "Textures/System"
    t = "/".join(parts); return "Textures/Misc/" + t if t else "Textures/Misc"

def import_group(dest, file_list, dry=False):
    """Импорт новых файлов одной папки-категории. dry=True — только вернуть, сколько было бы импортировано."""
    if not PURGE_FIRST:   # инкрементально: пропустить уже импортированные (зайдут только новые)
        file_list = [f for f in file_list if not _asset_exists(_pkg(dest, f))]   # ★ 06.10.2026: путь как у UE
        if os.environ.get("ASDA_TEX_ALLOW_DUP") != "1":   # ★ 06.10.2026: Texture2D с тем же именем в другой папке — дубль
            keep = []
            for f in file_list:
                other = _dup_index().get(_pkg(dest, f).rsplit("/", 1)[-1].lower())
                if other:
                    DUP_SKIPPED.append((_pkg(dest, f), other[0]))
                else:
                    keep.append(f)
            file_list = keep
    # ★ 15.09.2026: текстуры сцены логина (Textures/Backdrop, эффекты шкатулки/глифов) — не заводить и не перезаливать без разрешения
    file_list = [f for f in file_list if not _LG.login_package_blocked(_pkg(dest, f), "asda_import_textures.py", quiet=dry)]
    if not file_list or dry:
        return len(file_list)
    import unreal
    import asda_model_import_ue as _MIU   # ★ дедуп №2 Т17: общий импорт пачкой (путь Tools вставлен в main)
    imported = _MIU.import_batch(file_list, dest, True, skip_read_only=True)
    for a in imported:
        try:
            EAL.save_loaded_asset(a)
        except Exception:
            pass
    return len(imported)

def main():
    global atools, EAL, _LG
    sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
    import asda_login_guard as _LG   # ★ 15.09.2026: защита сцены логина (Models/Backdrop, Char/cre_creback-a_00, cre_creation01* и др.)
    dry = _dry_run()
    try:
        import unreal
        atools = unreal.AssetToolsHelpers.get_asset_tools()
        EAL = unreal.EditorAssetLibrary
    except ImportError:
        if not dry:
            raise RuntimeError("asda_import_textures: импорт возможен только в UE; вне UE — только --dry-run")
    if dry and PURGE_FIRST:
        raise RuntimeError("asda_import_textures: --dry-run несовместим с PURGE_FIRST=True")
    if not dry:
        disable_interchange_dds()
    if PURGE_FIRST:
        # ★ 15.09.2026: снос Textures целиком уносит и текстуры сцены логина — только с ASDA_ALLOW_LOGIN_WRITE=1
        _LG.forbid_login_write("asda_import_textures.py", target="Textures целиком (PURGE_FIRST; в т.ч. Textures/Backdrop и эффекты шкатулки)")
        for r in PURGE_ROOTS:
            if EAL.does_directory_exist(r):
                _log("Чистка старых ассетов: %s" % r)
                EAL.delete_directory(r)

    # Сгруппировать файлы по целевой папке-категории.
    buckets = {}
    skipped = {}   # ★ 06.10.2026: причина → число файлов, которые этот скрипт не раскладывает
    for root, _dirs, files in os.walk(RAW_ROOT):
        rel = os.path.relpath(root, RAW_ROOT).replace("\\", "/")
        for fn in files:
            if not fn.lower().endswith(EXTS):
                continue
            cat = classify(rel, fn)
            if cat is None:
                why = skip_reason(rel)
                skipped[why] = skipped.get(why, 0) + 1
                continue
            dest = "%s/%s" % (ASSET_ROOT, cat)
            buckets.setdefault(dest, []).append(os.path.join(root, fn))
    for why in sorted(skipped):
        _log("asda_import_textures: пропущено файлов %d — %s" % (skipped[why], why))

    _log("asda_import_textures: целевых папок=%d (Interchange-DDS отключён, legacy DXT)%s"
         % (len(buckets), " — СУХОЙ ПРОГОН" if dry else ""))
    total = 0
    misc_new = 0   # ★ 06.10.2026: сколько файлов легло бы в Textures/Misc (должно быть 0)
    keys = sorted(buckets.keys())
    for i, dest in enumerate(keys):
        n = import_group(dest, buckets[dest], dry=dry)
        total += n
        if dest.startswith("%s/%s" % (ASSET_ROOT, MISC_PREFIX)):
            misc_new += n
        if n:
            _log("[%d/%d] %s : %s%d" % (i + 1, len(keys), dest, "план +" if dry else "+", n))
    if DUP_SKIPPED:
        _log("asda_import_textures: не заведено дублей (Texture2D с тем же именем уже есть в другой папке): %d" % len(DUP_SKIPPED))
        for pkg, other in DUP_SKIPPED:
            _log("  дубль: %s — уже есть %s" % (pkg, other))
    if dry:
        _log("asda_import_textures: СУХОЙ ПРОГОН — ничего не импортировано; новых в плане=%d, из них в %s=%d"
             % (total, MISC_PREFIX, misc_new))
        _log("asda_import_textures: ПЛАН ГОТОВ")
    else:
        _log("asda_import_textures: импортировано всего=%d -> %s (в %s: %d)" % (total, ASSET_ROOT, MISC_PREFIX, misc_new))


# ★ ЗАКОН [[tools-no-import-side-effects]]: при импорте модуль НИЧЕГО не делает.
#   `__spec__` выставляет ЛЮБАЯ машинерия импорта (import / importlib / runpy) и НЕ
#   выставляет прямое исполнение файла (python file.py, UE -ExecutePythonScript).
#   `python -m` даёт `__name__ == "__main__"` и покрыт первым условием.
if __name__ == "__main__" or globals().get("__spec__") is None:
    main()
