# Copyright (c) 2026 @sitxovski (Discord)
# asda_disable_nanite.py — выключить Nanite на ВСЕХ мешах проекта (2008 low-poly, Nanite не нужен).
#
# ЗАЧЕМ (сет B 05.09.2026, ревизия review_setB_prep.md КРИТ-1). `r.Nanite.ProjectEnabled=True` ставится ТОЛЬКО ради VSM
#   (UE 5.8: UseVirtualShadowMaps → DoesPlatformSupportNanite → r.Nanite.ProjectEnabled). Но импорт glTF через Interchange
#   пишет мешам `NaniteSettings.bEnabled=true` (дефолт `bBuildNanite`, InterchangeGenericMeshPipeline.h:134) — байт-скан 05.09
#   нашёл 1050 таких ассетов (Models/Char 728, Items 243, Mob 75, Clouds 4). Пока ProjectEnabled=False это было безвредно;
#   после включения они пошли бы Nanite-путём (Translucent-материалы не поддерживаются, WPO иначе, сборка Nanite-данных в DDC).
#   Поэтому: ПОЛНЫЙ проход по Content ДО применения ini, повторный байт-скан = 0.
#
# КАК. По AssetRegistry берём StaticMesh/SkeletalMesh под корнями ROOTS. Чтобы не грузить 28 000 мешей ради ~1000,
#   сначала БАЙТ-СКАН файла пакета: сериализованный `NaniteSettings` с полем `bEnabled` появляется в таблице имён только
#   когда значение НЕ дефолтное (= true). Файл без этих имён → Nanite выключен, ассет не грузим (ASDA_PRESCAN=0 отключает).
#   Попавшие — грузим, читаем `nanite_settings.enabled`, гасим, сохраняем; самопроверка чтением обратно.
#
# Запуск (headless, редактор закрыт):
#   ASDA_DRY=1 python Tools/run_headless_safe.py Tools/asda_disable_nanite.py --marker NANITE-FINISHED   # сухой: только числа
#            python Tools/run_headless_safe.py Tools/asda_disable_nanite.py --marker NANITE-FINISHED   # запись
# Ключи:
#   ASDA_ROOTS="/Game"                — корни AssetRegistry через "||" (умолч. /Game — весь Content);
#   ASDA_ONLY_MODELS="Char/имя1,Mob/имя2" — охват только этих моделей (точечный переимпорт; префильтр по пути пакета);
#   ASDA_DRY=1                         — ничего не сохранять; ASDA_PRESCAN=0 — грузить все меши без байт-скана.
# Итоговые строки лога: «NANITE: категория <корень/кат> …», «NANITE ИТОГО: … выключено=N», маркер NANITE-FINISHED.
import os
import unreal
import sys as _sys_lg   # ★ 15.09.2026: защита сцены логина (asda_login_guard: Models/Backdrop, Char/cre_creback-a_00, cre_creation01* и др.)
_sys_lg.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import asda_login_guard as _LG

ROOTS = [r for r in os.environ.get("ASDA_ROOTS", "/Game").split("||") if r.strip()]
ONLY = set(s.strip() for s in os.environ.get("ASDA_ONLY_MODELS", "").split(",") if s.strip())
DRY = os.environ.get("ASDA_DRY", "0") == "1"
PRESCAN = os.environ.get("ASDA_PRESCAN", "1") != "0"
AR  = unreal.AssetRegistryHelpers.get_asset_registry()
EAL = unreal.EditorAssetLibrary
CONTENT = os.path.normpath(unreal.Paths.project_content_dir())   # …/AsdaReborn/Content


def _scope(pkg):
    """pkg = …/Models/<корень>/<модель>/StaticMeshes|SkeletalMeshes/<ассет>; ONLY пуст → всё."""
    if not ONLY: return True
    p = pkg.split("/")
    return len(p) >= 3 and (p[-3] in ONLY or "/".join(p[-4:-2]) in ONLY)


def _pkg_file(pkg):
    """Файл пакета /Game/A/B → <Content>/A/B.uasset (для байт-скана)."""
    if not pkg.startswith("/Game/"): return None
    return os.path.join(CONTENT, pkg[len("/Game/"):].replace("/", os.sep) + ".uasset")


def _bytes_say_nanite(pkg):
    """True, если в первых 256К файла есть имена NaniteSettings и bEnabled (сериализуется только не-дефолт = true).
    None — файла нет/не прочитался (тогда грузим ассет как раньше)."""
    f = _pkg_file(pkg)
    if not f or not os.path.isfile(f): return None
    try:
        with open(f, "rb") as h: b = h.read(262144)
    except OSError:
        return None
    return (b"NaniteSettings" in b) and (b"bEnabled" in b)


def _category(pkg):
    """Ключ статистики: первые три сегмента пути после /Game (AsdaAssets/Models/Char)."""
    p = [x for x in pkg.split("/") if x]
    return "/".join(p[1:4]) if len(p) > 1 else pkg


def main():
    stats = {}          # категория → [мешей, байт-скан «есть», загружено, было включено, выключено, ошибок]
    n = pre = loaded = was_on = off = err = 0
    n_login = 0   # ★ 15.09.2026: пропущено мешей сцены логина
    for root in ROOTS:
        for ad in AR.get_assets_by_path(unreal.Name(root), recursive=True):
            cls = str(ad.asset_class_path.asset_name)
            if cls not in ("StaticMesh", "SkeletalMesh"): continue
            pkg = str(ad.package_name)
            if not _scope(pkg): continue
            if _LG.login_package_blocked(pkg, "asda_disable_nanite.py", quiet=True):   # ★ 15.09.2026: сцена логина
                n_login += 1; continue
            st = stats.setdefault(_category(pkg), [0, 0, 0, 0, 0, 0])
            n += 1; st[0] += 1
            if PRESCAN:
                say = _bytes_say_nanite(pkg)
                if say is False: continue            # в файле нет NaniteSettings.bEnabled → выключен, не грузим
                if say: pre += 1; st[1] += 1
            m = unreal.load_asset(pkg)
            if not m:
                err += 1; st[5] += 1; unreal.log_warning("NANITE: не загрузился %s" % pkg); continue
            loaded += 1; st[2] += 1
            try:
                ns = m.get_editor_property("nanite_settings")
                if ns and ns.get_editor_property("enabled"):
                    was_on += 1; st[3] += 1
                    if not DRY:
                        ns.set_editor_property("enabled", False)
                        m.set_editor_property("nanite_settings", ns)
                        if not EAL.save_loaded_asset(m):
                            err += 1; st[5] += 1; unreal.log_warning("NANITE: не сохранился %s" % pkg); continue
                        if bool(m.get_editor_property("nanite_settings").get_editor_property("enabled")):
                            err += 1; st[5] += 1; unreal.log_error("NANITE: самопроверка — всё ещё включён %s" % pkg); continue
                        off += 1; st[4] += 1
            except Exception as e:
                err += 1; st[5] += 1
                unreal.log_warning("NANITE: сбой %s: %s" % (ad.asset_name, str(e)[:80]))
    for k in sorted(stats):
        s = stats[k]
        if s[1] or s[3] or s[5]:
            unreal.log("NANITE: категория %-40s мешей=%d байт-скан=%d загружено=%d включено=%d выключено=%d ошибок=%d"
                       % (k, s[0], s[1], s[2], s[3], s[4], s[5]))
    unreal.log("NANITE ИТОГО%s: корни=%s мешей=%d байт-скан=%d загружено=%d включено=%d выключено=%d ошибок=%d"
               % (" (СУХОЙ)" if DRY else "", "||".join(ROOTS), n, pre, loaded, was_on, off, err))
    if n_login:   # ★ 15.09.2026
        unreal.log_warning("[LOGIN-GUARD] asda_disable_nanite.py: пропущено мешей сцены логина: %d (ASDA_ALLOW_LOGIN_WRITE=1)" % n_login)
    unreal.log("NANITE-FINISHED")   # ASCII-маркер для run_headless_safe.py --marker


# ★ ЗАКОН [[tools-no-import-side-effects]]: при импорте модуль НИЧЕГО не делает.
#   `__spec__` выставляет ЛЮБАЯ машинерия импорта (import / importlib / runpy) и НЕ
#   выставляет прямое исполнение файла (python file.py, UE -ExecutePythonScript).
#   `python -m` даёт `__name__ == "__main__"` и покрыт первым условием.
if __name__ == "__main__" or globals().get("__spec__") is None:
    main()
