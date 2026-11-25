# -*- coding: utf-8 -*-
# Copyright (c) 2026 @sitxovski (Discord)
# asda_set_shadow_physics_assets.py — назначить USkeletalMesh.ShadowPhysicsAsset = PhysicsAsset того же меша
#   у скелеталов ИГРОКА / МОБОВ / NPC (сет B «тени», 05.09.2026; план FINAL_synthesis.md §4, адреса developer_round2.md §1.3).
#
# ★ ЗАЧЕМ. Капсульные тени UE 5.8 берут капсулы ТОЛЬКО из USkeletalMesh::ShadowPhysicsAsset
#   (SkeletalMeshSceneProxy.cpp:318-345: SphereElems + SphylElems каждого BodySetup, кость по имени);
#   фолбэка на обычный PhysicsAsset НЕТ (grep ShadowPhysicsAsset SkeletalMesh.cpp = 0). Без назначения флаг
#   bCastCapsuleIndirectShadow на компоненте (AsdaCapsuleShadow.h) ничего не рисует.
#   Авто-физассеты Interchange уже лежат рядом с мешами (`<mesh>_PhysicsAsset`): MobSkel 99/99, NPC 133/133,
#   Skeletal/Characters — только у тел (6), у частей экипа физассеты сняты при импорте (import_equip_parts.py:92-94)
#   и капсулы им НЕ нужны (иначе тень от каждой части).
#
# ★ СОСТАВ ТЕЛ. Капсульная тень видит только сферы/капсулы; выпуклые/коробки игнорируются. Скрипт считает
#   по каждому физассету sphyl/sphere/box/convex и отдельно перечисляет меши, у которых сфер+капсул = 0
#   («капсул для тени не будет» — кандидаты на простой ShadowPhysicsAsset утилитой, developer_round2 §1.3).
#
# Запуск (СКРЫТО, редактор закрыт):
#   ASDA_DRY=1 python Tools/run_headless_safe.py Tools/asda_set_shadow_physics_assets.py --marker SHADOWPA-FINISHED   # сухой
#            python Tools/run_headless_safe.py Tools/asda_set_shadow_physics_assets.py --marker SHADOWPA-FINISHED   # запись
# Ключи:
#   ASDA_DRY=1        — только отчёт, ассеты не сохраняются;
#   ASDA_ROOTS="a||b" — корни поиска (умолч. Characters||MobSkel||NPC, см. ROOTS);
#   ASDA_EXCLUDE="x||y" — подстроки имени меша, которые пропускаем (умолч. "eff_0_npc_99_exclam" — квестовые метки «!»/«?»,
#                        они не тело NPC; QuestMark-компонент капсульного флага не имеет, но и ассет трогать незачем);
#   ASDA_FORCE=1      — переназначить и там, где ShadowPhysicsAsset уже задан и ≠ PhysicsAsset (умолч. — не трогать, в отчёт);
#   ASDA_PA_REPORT    — путь JSON-отчёта (умолч. Reports/render_sets_2026-09-05/shadow_pa_report.json).
# Обратимость: ASDA_CLEAR=1 — снять ShadowPhysicsAsset (None) у тех же мешей.
#
# ★ Сет C-2 (05.09.2026, review_setB_fix.md ВАЖНО-3): режим ASDA_HERO_SCALED=1 — ОТДЕЛЬНЫЙ физассет тени героя.
#   Авто-физассет тел героя ch1_*/cre_* даёт капсулы r 3–16 см (масштаб компонента 0.01, r0 337…1558 ед. меша) против 24–31 см у мобов —
#   пятно под ногами −4…−7 %, глазами не видно. Делаем КОПИЮ `<mesh>_PhysicsAsset` → `<mesh>_ShadowPA` в той же папке, укрупняем
#   сферы/капсулы через C++ UAsdaShadowPhysicsAssetLib.ScaleCapsules (r' = clamp(r·SCALE, MIN, MAX) в ед. меша; тела с r < DROP удаляются —
#   вырожденный корень r0=0.505), сохраняем копию и назначаем ТОЛЬКО в ShadowPhysicsAsset (обычный PhysicsAsset не трогаем). Только корень
#   Skeletal/Characters (6 тел). Контроль ИЗ АССЕТА: DescribeBodies до/после (кость|refIdx|refPoseZ|sphyl|sphere|box|convex|r|len) → отчёт JSON.
#   Ключи: ASDA_PA_SCALE (2.0), ASDA_PA_MIN (1000 = 10 см), ASDA_PA_MAX (3200 = 32 см), ASDA_PA_DROP (100 = 1 см), ASDA_PA_LEN (1.0),
#          ASDA_PA_FORCE=1 — пересоздать копию, если уже есть; ASDA_HERO_REVERT=1 — вернуть ShadowPhysicsAsset = PhysicsAsset (копия остаётся).
#   Повторный прогон ОБЫЧНОГО режима этого скрипта копию НЕ перепишет (ShadowPA ≠ PA → «чужой ShadowPA оставлен», без ASDA_FORCE).
#   Запуск: python Tools/run_headless_safe.py Tools/asda_set_shadow_physics_assets.py --marker SHADOWPA-FINISHED  (env ASDA_HERO_SCALED=1 ...)
# ★ 15.09.2026 (Reports/dedup_audit/tex_rebind_2026-09-15.md, раздел 16): меши сцены логина (cre_cresoul*_preview в Skeletal/Characters и любые пакеты
#   логина) во ВСЕХ режимах (обычный, ASDA_FORCE, ASDA_CLEAR, ASDA_HERO_SCALED/REVERT) без ASDA_ALLOW_LOGIN_WRITE=1 пропускаются.
import os, json, stat, unreal

EAL = unreal.EditorAssetLibrary
AR = unreal.AssetRegistryHelpers.get_asset_registry()

ROOTS = [r for r in os.environ.get(
    "ASDA_ROOTS",
    "/Game/AsdaAssets/Models/Skeletal/Characters||/Game/AsdaAssets/Models/MobSkel||/Game/AsdaAssets/Models/NPC"
).split("||") if r.strip()]                                   # корни поиска SkeletalMesh
EXCLUDE = [x for x in os.environ.get("ASDA_EXCLUDE", "eff_0_npc_99_exclam").split("||") if x.strip()]  # подстроки-исключения
DRY = os.environ.get("ASDA_DRY", "0") == "1"                  # сухой режим
FORCE = os.environ.get("ASDA_FORCE", "0") == "1"              # переписывать чужой ShadowPhysicsAsset
CLEAR = os.environ.get("ASDA_CLEAR", "0") == "1"              # обратимость: снять ShadowPhysicsAsset
REPORT = os.environ.get("ASDA_PA_REPORT", r"@@ASTORY_DEV_ROOT_FWD@@/Reports/render_sets_2026-09-05/shadow_pa_report.json")
LOGIN_SKIPPED = []   # ★ 15.09.2026 (раздел 16): пропущенные пакеты сцены логина за прогон


def _login_skip(pkg):
    """★ 15.09.2026 (Reports/dedup_audit/tex_rebind_2026-09-15.md, раздел 16). True — пакет сцены логина, а запись не разрешена
    (ASDA_ALLOW_LOGIN_WRITE != 1): вызывающий пропускает его; путь — в LOGIN_SKIPPED."""
    import sys as _sys_lg
    here = os.path.dirname(os.path.abspath(__file__))   # Tools — для asda_login_guard
    if here not in _sys_lg.path:
        _sys_lg.path.insert(0, here)
    from asda_login_guard import login_package_blocked
    if login_package_blocked(pkg, "asda_set_shadow_physics_assets.py", quiet=True):
        LOGIN_SKIPPED.append(pkg)
        return True
    return False


def _log_login_skipped():
    """★ 15.09.2026 (раздел 16): итог пропуска — одной строкой [LOGIN-GUARD]."""
    if LOGIN_SKIPPED:
        unreal.log_warning("[LOGIN-GUARD] [shadowPA] пакетов сцены логина пропущено: %d (%s%s) — не менялись (только с явного разрешения владельца и ASDA_ALLOW_LOGIN_WRITE=1)"
                           % (len(LOGIN_SKIPPED), ", ".join(p.rsplit("/", 1)[-1] for p in LOGIN_SKIPPED[:8]),
                              " …" if len(LOGIN_SKIPPED) > 8 else ""))


def log(m):
    unreal.log("[shadowPA] " + str(m))


def collect():
    """Пакеты SkeletalMesh под ROOTS (AssetRegistry), отсортированные, без исключений."""
    out = []
    for root in ROOTS:
        for ad in AR.get_assets_by_path(unreal.Name(root), recursive=True):
            if str(ad.asset_class_path.asset_name) != "SkeletalMesh":
                continue
            pkg = str(ad.package_name)
            if any(x in pkg for x in EXCLUDE):
                continue
            if _login_skip(pkg):   # ★ 15.09.2026 (раздел 16): меш сцены логина — во всех режимах (обычный/FORCE/CLEAR)
                continue
            out.append(pkg)
    return sorted(set(out))


def body_stats(pa):
    """Состав тел физассета: {'bodies','sphyl','sphere','box','convex'} (капсульная тень видит только sphyl+sphere)."""
    st = {"bodies": 0, "sphyl": 0, "sphere": 0, "box": 0, "convex": 0}
    try:
        bodies = pa.get_editor_property("skeletal_body_setups") or []
        st["bodies"] = len(bodies)
        for b in bodies:
            ag = b.get_editor_property("agg_geom")
            for k, prop in (("sphyl", "sphyl_elems"), ("sphere", "sphere_elems"), ("box", "box_elems"), ("convex", "convex_elems")):
                try:
                    st[k] += len(ag.get_editor_property(prop) or [])
                except Exception:
                    pass
    except Exception as e:
        st["error"] = str(e)
    # ★ 05.09 (применение сета B): `skeletal_body_setups` из Python НЕ читается («Failed to find property» — UPROPERTY(instanced)
    #   без Blueprint-доступа), и первый прогон дал ложное «без капсул 235/235» (закон false-negatives-method). Второй метод —
    #   байт-скан файла PA: имена SphylElems/SphereElems/BoxElems/ConvexElems попадают в таблицу имён пакета только когда
    #   соответствующий массив непуст. Числа элементов так не узнать — считаем «есть/нет» (1/0) и помечаем источник.
    if st.get("error") or st["bodies"] == 0:
        try:
            pkg = pa.get_path_name().split(".")[0]
            f = os.path.join(unreal.Paths.project_content_dir(), pkg[len("/Game/"):] + ".uasset")
            with open(f, "rb") as h:
                b = h.read()
            for k, nm in (("sphyl", b"SphylElems"), ("sphere", b"SphereElems"), ("box", b"BoxElems"), ("convex", b"ConvexElems")):
                st[k] = 1 if nm in b else 0
            st["source"] = "bytescan"
            st.pop("error", None)
        except Exception as e2:
            st["error_bytescan"] = str(e2)
    return st


HERO_SCALED = os.environ.get("ASDA_HERO_SCALED", "0") == "1"
HERO_REVERT = os.environ.get("ASDA_HERO_REVERT", "0") == "1"
HERO_ROOT = "/Game/AsdaAssets/Models/Skeletal/Characters"
PA_SCALE = float(os.environ.get("ASDA_PA_SCALE", "2.0"))
PA_MIN = float(os.environ.get("ASDA_PA_MIN", "1000"))
PA_MAX = float(os.environ.get("ASDA_PA_MAX", "3200"))
PA_DROP = float(os.environ.get("ASDA_PA_DROP", "100"))
PA_LEN = float(os.environ.get("ASDA_PA_LEN", "1.0"))
PA_FORCE = os.environ.get("ASDA_PA_FORCE", "0") == "1"
CONTENT = "@@ASTORY_DEV_ROOT_FWD@@/AstoryClient/Content"
READONLY_LIST = "@@ASTORY_DEV_ROOT_FWD@@/AstoryClient/_backup/2026-09-05_setC2/_readonly_list.txt"


def _writable(pkg):
    """Снять read-only (Perforce) с файла пакета перед сохранением, путь — в список для reconcile."""
    f = os.path.join(CONTENT, pkg[len("/Game/"):] + ".uasset")
    if os.path.isfile(f) and not os.access(f, os.W_OK):
        os.chmod(f, stat.S_IWRITE | stat.S_IREAD)
        os.makedirs(os.path.dirname(READONLY_LIST), exist_ok=True)
        with open(READONLY_LIST, "a", encoding="utf-8") as h:
            h.write("Content%s.uasset\n" % pkg[len("/Game"):])


def hero_scaled():
    """Режим сета C-2: копия авто-PA героя с укрупнёнными капсулами → ShadowPhysicsAsset (см. шапку)."""
    lib = getattr(unreal, "AsdaShadowPhysicsAssetLib", None)
    if lib is None:
        raise RuntimeError("нет unreal.AsdaShadowPhysicsAssetLib — собрать AsdaRebornEditor (Source/AsdaImporters/.../AsdaShadowPhysicsAssetLib)")
    rep = {"mode": "hero_scaled", "revert": HERO_REVERT, "dry": DRY, "params": {"scale": PA_SCALE, "min": PA_MIN, "max": PA_MAX, "drop": PA_DROP, "len": PA_LEN},
           "meshes": {}}
    pkgs = [p for p in AR.get_assets_by_path(unreal.Name(HERO_ROOT), recursive=True) if str(p.asset_class_path.asset_name) == "SkeletalMesh"]
    pkgs = [p for p in pkgs if not _login_skip(str(p.package_name))]   # ★ 15.09.2026 (раздел 16): меши сцены логина (cre_cresoul*_preview)
    _log_login_skipped()
    log("герои: %d SkeletalMesh под %s; scale=%.2f min=%.0f max=%.0f drop=%.0f len=%.2f dry=%s revert=%s" % (
        len(pkgs), HERO_ROOT, PA_SCALE, PA_MIN, PA_MAX, PA_DROP, PA_LEN, DRY, HERO_REVERT))
    for ad in sorted(pkgs, key=lambda a: str(a.package_name)):
        pkg = str(ad.package_name)
        sk = ad.get_asset()
        pa = sk.get_editor_property("physics_asset")
        spa = sk.get_editor_property("shadow_physics_asset")
        d = {"pa": pa.get_path_name() if pa else None, "shadow_pa_before": spa.get_path_name() if spa else None}
        if pa is None:
            d["error"] = "нет PhysicsAsset"; rep["meshes"][pkg] = d; continue
        if HERO_REVERT:
            if not DRY:
                _writable(pkg); sk.set_editor_property("shadow_physics_asset", pa); EAL.save_loaded_asset(sk)
            d["shadow_pa_after"] = sk.get_editor_property("shadow_physics_asset").get_path_name()
            rep["meshes"][pkg] = d; log("REVERT %s: ShadowPA=%s" % (ad.asset_name, d["shadow_pa_after"])); continue
        src = pa.get_path_name().split(".")[0]
        dst = src.rsplit("/", 1)[0] + "/" + str(ad.asset_name) + "_ShadowPA"
        d["bodies_src"] = [str(x) for x in lib.describe_bodies(pa, sk)]
        if DRY:
            d["dst"] = dst; rep["meshes"][pkg] = d
            log("СУХО %s: копия %s, тел %d" % (ad.asset_name, dst, len(d["bodies_src"]))); continue
        if _login_skip(dst):   # ★ 15.09.2026 (раздел 16): копия физассета легла бы в пакеты сцены логина
            d["note"] = "копия в пакетах сцены логина — пропущено защитой (ASDA_ALLOW_LOGIN_WRITE=1)"; rep["meshes"][pkg] = d; continue
        if EAL.does_asset_exist(dst):
            if PA_FORCE:
                _writable(dst)
                if not EAL.delete_asset(dst):
                    raise RuntimeError("не удалить старую копию %s" % dst)
            else:
                d["note"] = "копия уже есть — переиспользуется (ASDA_PA_FORCE=1 пересоздаст)"
        if not EAL.does_asset_exist(dst):
            if not EAL.duplicate_asset(src, dst):
                raise RuntimeError("duplicate_asset %s -> %s вернул None" % (src, dst))
            new_pa = EAL.load_asset(dst)
            n = lib.scale_capsules(new_pa, PA_SCALE, PA_MIN, PA_MAX, PA_DROP, PA_LEN)
            if n <= 0:
                raise RuntimeError("ScaleCapsules %s вернул %d" % (dst, n))
            new_pa.modify()
            if not EAL.save_loaded_asset(new_pa, False):
                raise RuntimeError("не сохранилась копия %s" % dst)
        new_pa = EAL.load_asset(dst)
        d["dst"] = dst
        d["bodies_dst"] = [str(x) for x in lib.describe_bodies(new_pa, sk)]
        _writable(pkg)
        sk.set_editor_property("shadow_physics_asset", new_pa)
        sk.modify()
        if not EAL.save_loaded_asset(sk, False):
            raise RuntimeError("не сохранился меш %s" % pkg)
        chk = sk.get_editor_property("shadow_physics_asset")
        d["shadow_pa_after"] = chk.get_path_name() if chk else None
        if d["shadow_pa_after"] != new_pa.get_path_name():
            raise RuntimeError("%s: после записи ShadowPA=%s" % (pkg, d["shadow_pa_after"]))
        rep["meshes"][pkg] = d
        log("%s: тел %d → %d; ShadowPA=%s" % (ad.asset_name, len(d["bodies_src"]), len(d["bodies_dst"]), dst.rsplit("/", 1)[-1]))
        for b in d["bodies_dst"]:
            log("    " + b)
    os.makedirs(os.path.dirname(REPORT), exist_ok=True)
    with open(REPORT, "w", encoding="utf-8") as f:
        json.dump(rep, f, ensure_ascii=False, indent=1)
    log("отчёт: " + REPORT)
    unreal.log("SHADOWPA-FINISHED")


def main():
    """Работа скрипта. Вынесена в функцию, чтобы импорт модуля НИЧЕГО не делал (закон [[tools-no-import-side-effects]])."""
    if HERO_SCALED or HERO_REVERT:
        hero_scaled()
        return
    pkgs = collect()
    _log_login_skipped()   # ★ (раздел 16)
    log("корни: %s | кандидатов SkeletalMesh: %d | dry=%s force=%s clear=%s" % (ROOTS, len(pkgs), DRY, FORCE, CLEAR))
    rep = {"roots": ROOTS, "dry": DRY, "clear": CLEAR, "total": len(pkgs),
           "assigned": [], "already": [], "no_pa": [], "foreign_kept": [], "no_capsules": [], "cleared": [], "errors": [],
           "body_stats": {}}
    for p in pkgs:
        sk = unreal.load_asset(p)
        if not isinstance(sk, unreal.SkeletalMesh):
            rep["errors"].append({"pkg": p, "why": "не SkeletalMesh после загрузки"}); continue
        try:
            pa = sk.get_editor_property("physics_asset")
            spa = sk.get_editor_property("shadow_physics_asset")
        except Exception as e:
            rep["errors"].append({"pkg": p, "why": "чтение свойств: %s" % e}); continue
        if CLEAR:
            if spa is not None:
                if not DRY:
                    sk.set_editor_property("shadow_physics_asset", None); EAL.save_loaded_asset(sk)
                rep["cleared"].append(p)
            continue
        if pa is None:
            rep["no_pa"].append(p); continue
        st = body_stats(pa); rep["body_stats"][p] = st
        if st.get("sphyl", 0) + st.get("sphere", 0) == 0:
            rep["no_capsules"].append({"pkg": p, "pa": pa.get_path_name(), "stats": st})
        if spa is not None and spa.get_path_name() == pa.get_path_name():
            rep["already"].append(p); continue
        if spa is not None and not FORCE:
            rep["foreign_kept"].append({"pkg": p, "shadow_pa": spa.get_path_name(), "pa": pa.get_path_name()}); continue
        if not DRY:
            sk.set_editor_property("shadow_physics_asset", pa)
            if not EAL.save_loaded_asset(sk):
                rep["errors"].append({"pkg": p, "why": "save_loaded_asset=False"}); continue
            # самопроверка ИЗ АССЕТА после записи
            chk = sk.get_editor_property("shadow_physics_asset")
            if chk is None or chk.get_path_name() != pa.get_path_name():
                rep["errors"].append({"pkg": p, "why": "после записи shadow_physics_asset=%s" % (chk.get_path_name() if chk else None)}); continue
        rep["assigned"].append(p)
    # итоги
    n_caps = sum(1 for p, st in rep["body_stats"].items() if st.get("sphyl", 0) + st.get("sphere", 0) > 0)
    log("ИТОГО: всего %d | назначено %d%s | уже было %d | без PhysicsAsset %d | чужой ShadowPA оставлен %d | "
        "без сфер/капсул (тени не будет) %d из %d с PA | снято %d | ошибок %d"
        % (rep["total"], len(rep["assigned"]), " (СУХО)" if DRY else "", len(rep["already"]), len(rep["no_pa"]),
           len(rep["foreign_kept"]), len(rep["no_capsules"]), len(rep["body_stats"]), len(rep["cleared"]), len(rep["errors"])))
    if rep["no_pa"]:
        log("без PhysicsAsset (первые 20): " + ", ".join(x.rsplit("/", 1)[-1] for x in rep["no_pa"][:20]))
    if rep["no_capsules"]:
        log("без сфер/капсул (первые 20): " + ", ".join(x["pkg"].rsplit("/", 1)[-1] for x in rep["no_capsules"][:20]))
    for e in rep["errors"]:
        unreal.log_error("[shadowPA] %s: %s" % (e["pkg"], e["why"]))
    try:
        os.makedirs(os.path.dirname(REPORT), exist_ok=True)
        with open(REPORT, "w", encoding="utf-8") as f:
            json.dump(rep, f, ensure_ascii=False, indent=1)
        log("отчёт: " + REPORT)
    except Exception as e:
        unreal.log_warning("[shadowPA] отчёт не записан: %s" % e)
    unreal.log("SHADOWPA-FINISHED")   # ASCII-маркер для run_headless_safe.py --marker


# ★ ЗАКОН [[tools-no-import-side-effects]]: при импорте модуль НИЧЕГО не делает
#   (`__spec__` выставляет любая машинерия импорта и не выставляет прямое исполнение файла / -ExecutePythonScript).
if __name__ == "__main__" or globals().get("__spec__") is None:
    main()
