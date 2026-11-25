# Copyright (c) 2026 @sitxovski (Discord)
# import_jui_textures.py — импорт UI-атласов (RawAssets/Textures/JUI/*.png) в /Game/AsdaAssets/Textures/JUI.
# Запуск: python Tools/run_headless_safe.py Tools/import_jui_textures.py --marker "ПРОВЕРКА РАСПРЕДЕЛЕНИЯ JUI: OK"
#
# ★ 06.10.2026 (Reports/owner_1006/texture_importers_fix.md; ревизия ui_icons_nomips_redirectors_REVIEW.md, СРЕДН-1):
#   БЫЛО: AutomatedAssetImportData + replace_existing=True — движок при этом «перезаписывает и настройки»
#     (Factory.cpp:389-391 → YesAll, EditorFactories.cpp:3894-3905) и включает автоопределение карты нормалей
#     (EditorFactories.cpp:4162); затем скрипт ставил NO_MIPMAPS ВСЕМ и lod_group не ставил. Переимпорт терял мипы у
#     94 текстур, у которых они есть в оригинале, уводил 15 иконок в WORLD_NORMAL_MAP и сбрасывал группу UI у 59.
#   ТЕПЕРЬ:
#   • unreal.AssetImportTask (automated, replace_existing=True, replace_existing_settings=False) + явная фабрика
#     TextureFactory (legacy-путь, без Interchange: AssetTools.cpp:3566-3569). У СУЩЕСТВУЮЩИХ ассетов движок сохраняет
#     их настройки (EditorFactories.cpp:3880, 4125-4151: группа, сжатие, sRGB, мипы, never_stream и др.) и не
#     запускает автоопределение карты нормалей; скрипт после импорта их НЕ трогает.
#   • НОВЫМ ассетам: lod_group = TEXTUREGROUP_WORLD (явно — гасит итог автоопределения карты нормалей),
#     TC_EDITOR_ICON, sRGB, never_stream, без flip green; TMGS_NO_MIPMAPS ТОЛЬКО если в таблице эталона
#     MipLevels == 1 или явное переопределение «только порт» (tpl_talkwin, tpl_yellowwin) (Tools/data/asda_jui_original_miplevels.json, генератор Tools/asda_jui_mip_table.py —
#     заголовок .TEX +0x14 из juidata _MINE/XYT/Arabic); нет в таблице или >1 — мипы остаются (FROM_TEXTURE_GROUP).
#   • После импорта (и в сухом прогоне) — проверка папки JUI с эталоном распределения (ревизия 06.10):
#     NO_MIPMAPS 2107 / FROM_TEXTURE_GROUP 94 (с 06.10, было 2105/96); WORLD 2142 / UI 59 / WORLD_NORMAL_MAP 0 (+ вклад новых ассетов),
#     у переимпортированных существующих группа/мипы/сжатие/sRGB не изменились, в Textures/Misc только Util/T_Transparent.
#     Расхождение → строки ОШИБКА и RuntimeError; маркера «ПРОВЕРКА РАСПРЕДЕЛЕНИЯ JUI: OK» нет →
#     run_headless_safe возвращает 3 (ненулевой код).
#   • Сохранение — поштучно только импортированных ассетов (строка ASDA_WRITES_ASSET на каждый), а не всей папки.
#   Ключи (run_headless_safe аргументы не передаёт — поэтому и переменные окружения):
#     --dry-run      | ASDA_TEX_DRY_RUN=1   — только план и проверка текущего состояния, ничего не импортируется и не сохраняется;
#     --only-new     | ASDA_JUI_ONLY_NEW=1  — существующие ассеты пропускать (заводить только новые);
#     ASDA_JUI_FILES=a.png,b.png             — импортировать только эти файлы из RawAssets/Textures/JUI;
#     ASDA_JUI_DEST=/Game/_tmp_...           — другая папка назначения (песочница для пробы).
import os, sys

SRC = r"@@ASTORY_DEV_ROOT@@\AstoryClient\RawAssets\Textures\JUI"
JUI_DEST = "/Game/AsdaAssets/Textures/JUI"     # боевая папка JUI-текстур
# ★ 11.10.2026: свойства, которые путь import_textures_keep_settings меняет при переимпорте сам — их возвращаем.
KEEP_EXTRA_PROPS = ("composite_texture_mode", "normalize_normals")
MISC_ROOT = "/Game/AsdaAssets/Textures/Misc"   # после 06.10 здесь только Util/T_Transparent
MISC_ALLOWED = {"/Game/AsdaAssets/Textures/Misc/Util/T_Transparent"}
OK_MARKER = "ПРОВЕРКА РАСПРЕДЕЛЕНИЯ JUI: OK"

# ★ 06.10.2026: эталон распределения папки JUI (ревизия ui_icons_nomips_redirectors_REVIEW.md, п. 1 и СРЕДН-1).
REF_TOTAL = 2201
# ★ 06.10.2026 (решение координатора, texture_importers_fix.md раздел 5): s_item_l_53_ / s_item_l_56_ приведены к оригиналу
#   (.TEX «s_item_l_53 .tex»/«s_item_l_56 .tex», MipLevels = 1) → NO_MIPMAPS. БЫЛО: 2105 / 96.
REF_MIP = {"TMGS_NO_MIPMAPS": 2107, "TMGS_FROM_TEXTURE_GROUP": 94}
REF_LOD = {"TEXTUREGROUP_WORLD": 2142, "TEXTUREGROUP_UI": 59, "TEXTUREGROUP_WORLD_NORMAL_MAP": 0}

# ★ 16.09.2026 (Reports/dedup_audit/tex_rebind_2026-09-15.md, раздел 19 МЕЛК-5 и раздел 20 СРЕДН-1): защите даётся ТОТ ЖЕ путь пакета,
#   который даст UE при импорте файла, — asda_login_guard.ue_import_package_path (там же разбор по исходникам UE 5.8:
#   SanitizeObjectName по INVALID_OBJECTNAME_CHARACTERS, затем второй проход по INVALID_LONGPACKAGE_CHARACTERS).
#   `s_item_l_53 .png` → пакет `s_item_l_53_` (есть в Content), `ne_win(2).png` → `ne_win_2_`.


def log(m):
    import unreal
    unreal.log("[import_jui] " + str(m))


def err(m):
    import unreal
    unreal.log_error("[import_jui] ОШИБКА: " + str(m))


def _opt(flag, env):
    """Ключ командной строки или переменная окружения = 1."""
    return flag in sys.argv or os.environ.get(env) == "1"


def _short(e):
    """Имя значения перечисления UE без префикса класса."""
    n = getattr(e, "name", None)
    return n if isinstance(n, str) and n else str(e).split(".")[-1].split(":")[0].strip("<> ")


def _props(t):
    """Проверяемые настройки текстуры."""
    return {"mip": _short(t.get_editor_property("mip_gen_settings")),
            "lod": _short(t.get_editor_property("lod_group")),
            "comp": _short(t.get_editor_property("compression_settings")),
            "srgb": bool(t.get_editor_property("srgb")),
            # ★ 11.10.2026 (ревизия В2): и два свойства, которые путь импорта менял сам (KEEP_EXTRA_PROPS)
            "ctm": _short(t.get_editor_property("composite_texture_mode")),
            "nn": bool(t.get_editor_property("normalize_normals"))}


def _scan(root):
    """{путь пакета: настройки} всех Texture2D под root (рекурсивно), с диска/реестра."""
    import unreal
    EAL = unreal.EditorAssetLibrary
    out = {}
    for p in EAL.list_assets(root, recursive=True, include_folder=False):
        pkg = p.split(".")[0]
        a = EAL.load_asset(pkg)
        if isinstance(a, unreal.Texture2D):
            out[pkg] = _props(a)
    return out


def _table_nomips(table, pkg):
    """True — в оригинале MipLevels == 1 (ставить NO_MIPMAPS); False — мипы есть или файла в оригинале нет."""
    e = table.get(pkg.rsplit("/", 1)[-1].lower())
    return bool(e and e.get("nomips"))


def check(dest, table, before, reimported, created):
    """Проверка после импорта (и в сухом прогоне). Возвращает список расхождений (пусто — OK)."""
    import unreal
    bad = []
    jui = _scan(JUI_DEST)
    new_in_jui = [p for p in created if p.startswith(JUI_DEST + "/")]
    exp_mip = dict(REF_MIP); exp_lod = dict(REF_LOD)
    for p in new_in_jui:   # вклад новых ассетов в эталон распределения
        k = "TMGS_NO_MIPMAPS" if _table_nomips(table, p) else "TMGS_FROM_TEXTURE_GROUP"
        exp_mip[k] = exp_mip.get(k, 0) + 1
        exp_lod["TEXTUREGROUP_WORLD"] += 1
    got_mip, got_lod = {}, {}
    for v in jui.values():
        got_mip[v["mip"]] = got_mip.get(v["mip"], 0) + 1
        got_lod[v["lod"]] = got_lod.get(v["lod"], 0) + 1
    log("JUI: текстур %d (эталон %d); mip %s (эталон %s); lod %s (эталон %s)"
        % (len(jui), REF_TOTAL + len(new_in_jui), got_mip, exp_mip, got_lod, exp_lod))
    if len(jui) != REF_TOTAL + len(new_in_jui):
        bad.append("число текстур JUI %d != %d" % (len(jui), REF_TOTAL + len(new_in_jui)))
    for k in set(exp_mip) | set(got_mip):
        if got_mip.get(k, 0) != exp_mip.get(k, 0):
            bad.append("mip %s: %d != эталон %d" % (k, got_mip.get(k, 0), exp_mip.get(k, 0)))
    for k in set(exp_lod) | set(got_lod):
        if got_lod.get(k, 0) != exp_lod.get(k, 0):
            bad.append("lod %s: %d != эталон %d" % (k, got_lod.get(k, 0), exp_lod.get(k, 0)))
    # переимпортированные существующие — настройки прежние
    cur = dict(jui)
    if dest != JUI_DEST:
        cur.update(_scan(dest))
    for p in reimported:
        if cur.get(p) != before.get(p):
            bad.append("настройки существующего %s изменились: %s -> %s" % (p, before.get(p), cur.get(p)))
        elif len(reimported) <= 50:
            log("переимпорт %s: настройки прежние %s" % (p, cur.get(p)))
    # новые — по таблице эталона
    for p in created:
        v = cur.get(p)
        want_mip = "TMGS_NO_MIPMAPS" if _table_nomips(table, p) else "TMGS_FROM_TEXTURE_GROUP"
        if not v or v["mip"] != want_mip or v["lod"] != "TEXTUREGROUP_WORLD" or v["comp"] != "TC_EDITOR_ICON" or not v["srgb"]:
            bad.append("новый %s: %s, ждали mip=%s lod=TEXTUREGROUP_WORLD comp=TC_EDITOR_ICON srgb=True" % (p, v, want_mip))
        else:
            log("новый %s: %s (по таблице: %s)" % (p, v, want_mip))
    # Misc — только T_Transparent
    misc = set(x.split(".")[0] for x in unreal.EditorAssetLibrary.list_assets(MISC_ROOT, recursive=True, include_folder=False)) \
        if unreal.EditorAssetLibrary.does_directory_exist(MISC_ROOT) else set()
    extra = sorted(misc - MISC_ALLOWED)
    log("Textures/Misc: ассетов %d, лишних %d" % (len(misc), len(extra)))
    if extra:
        bad.append("в Textures/Misc лишние ассеты: %d (первые: %s)" % (len(extra), ", ".join(extra[:5])))
    return bad


def main():
    """Работа скрипта. Вынесена в функцию, чтобы импорт модуля НИЧЕГО не делал
    (закон [[tools-no-import-side-effects]])."""
    import unreal
    sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
    import asda_login_guard as _LG
    import asda_jui_mip_table as _MT
    EAL = unreal.EditorAssetLibrary
    dry = _opt("--dry-run", "ASDA_TEX_DRY_RUN")
    only_new = _opt("--only-new", "ASDA_JUI_ONLY_NEW")
    dest = os.environ.get("ASDA_JUI_DEST", JUI_DEST).rstrip("/")
    table = _MT.load_table()   # fail-closed: нет/битая таблица эталона → исключение, импорта нет
    log("таблица эталона: %d имён; режим: %s%s; папка: %s"
        % (len(table), "СУХОЙ ПРОГОН" if dry else "импорт", ", только новые" if only_new else "", dest))

    files = [os.path.join(SRC, f) for f in sorted(os.listdir(SRC)) if f.lower().endswith(".png")]   # подпапки (_bak_*) не берутся
    only = [x.strip().lower() for x in os.environ.get("ASDA_JUI_FILES", "").split(",") if x.strip()]
    if only:
        files = [f for f in files if os.path.basename(f).lower() in only]
        log("фильтр ASDA_JUI_FILES: файлов %d из %d заданных" % (len(files), len(only)))
    # ★ 16.09.2026 (раздел 17): атласы окон логина (Tools/data/asda_login_packages.json) без ASDA_ALLOW_LOGIN_WRITE=1 не импортируются.
    login_skipped = [f for f in files if _LG.login_package_blocked(_LG.ue_import_package_path(dest, f),
                                                                  "import_jui_textures.py", quiet=True)]
    files = [f for f in files if f not in login_skipped]
    if login_skipped:
        unreal.log_warning("[LOGIN-GUARD] import_jui_textures.py: атласов окон логина пропущено %d (%s) — только с %s=1"
                           % (len(login_skipped), ", ".join(os.path.basename(f) for f in login_skipped), _LG.ALLOW_ENV))

    plan_new, plan_re = [], []
    for f in files:
        pkg = _LG.ue_import_package_path(dest, f)
        (plan_re if EAL.does_asset_exist(pkg) else plan_new).append((f, pkg))
    if only_new:
        plan_re = []
    nn = sum(1 for _f, p in plan_new if _table_nomips(table, p))
    log("ПЛАН: новых %d (NO_MIPMAPS по таблице %d, с мипами %d; группа WORLD), переимпорт существующих %d (настройки сохраняются)"
        % (len(plan_new), nn, len(plan_new) - nn, len(plan_re)))
    for f, p in plan_new[:50]:
        log("  новый: %s <- %s (%s)" % (p, os.path.basename(f), "NO_MIPMAPS" if _table_nomips(table, p) else "мипы"))

    before = {}
    # ★ 11.10.2026 (решение владельца по `Reports/dedup_audit2/wave2_T_followup.md` §П.1): путь
    #   `import_textures_keep_settings` при переимпорте сам меняет у СУЩЕСТВУЮЩЕЙ текстуры два свойства, которых
    #   никто не просил (composite_texture_mode, normalize_normals). Запоминаем их до импорта и возвращаем перед
    #   сохранением — путь 06.10 (проверен на 2 201 текстуре) остаётся, побочный эффект снят.
    keep_extra = {}
    for _f, p in plan_re:
        a = EAL.load_asset(p)
        if isinstance(a, unreal.Texture2D):
            before[p] = _props(a)
            keep_extra[p] = {k: a.get_editor_property(k) for k in KEEP_EXTRA_PROPS}

    created, reimported = [], [p for _f, p in plan_re]
    if not dry and (plan_new or plan_re):
        # ★ дедуп №2 В2 Т17: создание задач (replace_existing_settings=False, TextureFactory, save=False) и импорт —
        #   общая `asda_model_import_ue.import_textures_keep_settings` (тело перенесено дословно).
        import asda_model_import_ue as _MIU
        tasks = _MIU.import_textures_keep_settings([f for f, _p in plan_new + plan_re], dest, save=False)
        new_set = set(p for _f, p in plan_new)
        for t in tasks:
            for op in (t.get_editor_property("imported_object_paths") or []):
                pkg = str(op).split(".")[0]
                a = EAL.load_asset(pkg)
                if not isinstance(a, unreal.Texture2D):
                    continue
                if pkg in new_set:   # ★ только новым — явные настройки; существующие не трогаем
                    a.set_editor_property("lod_group", unreal.TextureGroup.TEXTUREGROUP_WORLD)
                    a.set_editor_property("compression_settings", unreal.TextureCompressionSettings.TC_EDITOR_ICON)
                    a.set_editor_property("srgb", True)
                    a.set_editor_property("never_stream", True)
                    a.set_editor_property("flip_green_channel", False)
                    a.set_editor_property("mip_gen_settings",
                                          unreal.TextureMipGenSettings.TMGS_NO_MIPMAPS if _table_nomips(table, pkg)
                                          else unreal.TextureMipGenSettings.TMGS_FROM_TEXTURE_GROUP)
                    created.append(pkg)
                elif pkg in keep_extra:   # ★ 11.10.2026: вернуть два свойства, изменённые путём импорта
                    for k, v in keep_extra[pkg].items():
                        if a.get_editor_property(k) != v:
                            log("  вернул %s.%s → %s" % (pkg, k, v))
                            a.set_editor_property(k, v)
                if _LG.login_package_blocked(pkg, "import_jui_textures.py"):
                    continue
                if EAL.save_loaded_asset(a, False):
                    unreal.log("ASDA_WRITES_ASSET: %s" % pkg)
                else:
                    err("не сохранён %s" % pkg)
        log("импортировано: новых %d, переимпорт %d" % (len(created), len(reimported)))
    elif dry:
        reimported = []   # в сухом прогоне ничего не переимпортировано

    bad = check(dest, table, before, reimported, created)
    if bad:
        for b in bad:
            err(b)
        raise RuntimeError("import_jui_textures: проверка распределения JUI — расхождений %d" % len(bad))
    log(OK_MARKER)
    log("ГОТОВО")


# ★ ЗАКОН [[tools-no-import-side-effects]]: при импорте модуль НИЧЕГО не делает.
#   `__spec__` выставляет ЛЮБАЯ машинерия импорта (import / importlib / runpy) и НЕ
#   выставляет прямое исполнение файла (python file.py, UE -ExecutePythonScript).
#   `python -m` даёт `__name__ == "__main__"` и покрыт первым условием.
if __name__ == "__main__" or globals().get("__spec__") is None:
    main()
