# Copyright (c) 2026 @sitxovski (Discord)
# asda_fix_fastbuild_bounds.py — ЛЕЧЕНИЕ НА МЕСТЕ StaticMesh'ей с мусорными/NaN габаритами
#   рендер-данных (в куке: «FStaticMeshRenderData::Serialize found NaN in Bounds of ...»,
#   «...found NaN when saving CardRepresentationData...», затем handled ensure в FQuat(FMatrix)).
#
# ★★★ ПРИЧИНА (доказана по исходникам движка UE 5.8, Engine/Private/StaticMesh.cpp):
#   1) Меши строились вызовом `sm.build_from_static_mesh_descriptions([desc])`, т.е. с ДЕФОЛТНЫМ
#      fast_build=True. В UStaticMesh::BuildFromMeshDescriptions (стр. 8928-8931) это ВЗВОДИТ и
#      СЕРИАЛИЗУЕТ в ассет флаг `UStaticMesh::bDoFastBuild` (UPROPERTY, StaticMesh.h:1394).
#   2) При КАЖДОЙ последующей сборке рендер-данных (в т.ч. при куке) FStaticMeshRenderData::Cache
#      видит `Owner->bDoFastBuild` (стр. 4321) и идёт «быстрой» веткой: AllocateLODResources +
#      BuildFromMeshDescription по LOD'ам. Эта ветка, в отличие от обычной (MeshBuilderModule::
#      BuildMesh), **НЕ присваивает FStaticMeshRenderData::Bounds**. Поле объявлено как
#      `FBoxSphereBounds Bounds;` (StaticMeshResources.h:795) и НЕ инициализируется в конструкторе
#      FStaticMeshRenderData (StaticMesh.cpp:2342) → остаётся МУСОРОМ неинициализированной памяти.
#      (Присваивание Bounds есть только на стр. 9019 — в ветке fast build самого
#      BuildFromMeshDescriptions, т.е. в момент ПЕРВИЧНОЙ сборки, но не в Cache.)
#   3) В редакторе это незаметно: UStaticMesh::CalculateExtendedBounds (стр. 7285) берёт габариты из
#      CachedMeshDescriptionBounds, а не из RenderData->Bounds — поэтому get_bounds() показывает
#      корректные значения, объекты нормально рисуются и куллятся. Мусор всплывает ТОЛЬКО при
#      сериализации рендер-данных, т.е. в куке.
#   4) Мусор детерминирован (одинаковый набор мешей в двух независимых прогонах кука) — он
#      «заморожен» в записях DDC. Примерно у 7 % мешей битовый узор мусора оказывается NaN — это и
#      есть те 1491 меш; у остальных мусор конечный и молча проходит проверку ContainsNaN.
#
# ★ ЛЕЧЕНИЕ (без переимпорта, средствами UE):
#   Снять bDoFastBuild и пересобрать рендер-данные обычным билдером, который габариты считает.
#   Из Python свойство недоступно («Property 'bDoFastBuild' ... is protected and cannot be read»),
#   но движок сам сбрасывает его в UStaticMesh::PostEditChangeProperty (стр. 5445-5453):
#   при изменении NaniteSettings с bEnabled=True → `bDoFastBuild = false`. Поэтому:
#     (а) включаем Nanite  → PostEditChangeProperty → bDoFastBuild=false + полный Build();
#     (б) выключаем Nanite → PostEditChangeProperty → ещё один полный Build() (Nanite не нужен:
#         геометрия 2008 года, low-poly — см. asda_disable_nanite.py);
#     (в) сохраняем ассет.
#   После этого Cache идёт веткой MeshBuilderModule::BuildMesh, которая заполняет Bounds из
#   реальной геометрии → в кук уходят валидные габариты.
#
# ★★ ВТОРАЯ ПОЛОВИНА ЛЕЧЕНИЯ — ОТРАВЛЕННЫЙ DDC. Мусорные габариты уже «заморожены» в записях DDC:
#   ключ (BuildStaticMeshDerivedDataKeySuffix, стр. 3727) флаг bDoFastBuild НЕ учитывает, поэтому
#   после простого снятия флага Cache находит СТАРУЮ запись и опять поднимает NaN (проверено:
#   после снятия флага меш при перезагрузке снова давал «found NaN in Bounds»).
#   В ключ входит идентификатор бабл-даты mesh-description (стр. 3825
#   `KeySuffix += SrcModel.GetMeshDescriptionBulkData()->GetUnversionedIdString()`), а
#   UStaticMesh::BuildFromMeshDescriptions с bCommitMeshDescription=True перекоммичивает описание
#   с НОВЫМ GUID. Поэтому третьим шагом перекоммичиваем то же самое описание LOD0 через
#   `build_from_static_mesh_descriptions([desc], False, False)` (fast_build=False!): геометрия та же,
#   ключ DDC новый, сборка идёт обычным билдером → в DDC ложатся корректные габариты.
#
# Запуск: headless.
#   ASDA_LIST=<файл со списком пакетов>  — лечить только их (по одному пути в строке);
#   ASDA_ROOTS="/Game/Map||/Game/AsdaAssets/Models/Backdrop" — иначе обход этих корней целиком;
#   ASDA_LIMIT=N — обработать только первые N (проба).
import os, unreal

EAL = unreal.EditorAssetLibrary
AR = unreal.AssetRegistryHelpers.get_asset_registry()


def log(m):
    unreal.log("[fix_fb] " + str(m))


def _pkg(path):
    """'/Game/A/SM_x.SM_x' → '/Game/A/SM_x' (имя пакета)."""
    head, _, tail = path.rpartition("/")
    return head + "/" + tail.split(".")[0] if head else path


def bounds_str(sm):
    """Габариты, которые редактор отдаёт наружу (ExtendedBounds) — для журнала до/после."""
    b = sm.get_bounds()
    return "origin=(%.3f,%.3f,%.3f) extent=(%.3f,%.3f,%.3f) r=%.3f" % (
        b.origin.x, b.origin.y, b.origin.z,
        b.box_extent.x, b.box_extent.y, b.box_extent.z, b.sphere_radius)


def _vid(i):
    """unreal.VertexID из индекса (конструктор без имени аргумента)."""
    return unreal.VertexID(i)


def has_nan_geometry(sm):
    """Проверка сырых позиций вершин LOD0 на NaN/inf (на случай, если мусор в самой геометрии)."""
    d = sm.get_static_mesh_description(0)
    if d is None:
        return 0
    n = d.get_vertex_count()
    bad = 0
    for i in range(n):
        vid = _vid(i)
        try:
            if not d.is_vertex_valid(vid):
                continue
            p = d.get_vertex_position(vid)
        except Exception:
            continue
        for c in (p.x, p.y, p.z):
            if c != c or abs(c) > 1e30:
                bad += 1
                break
    return bad


def sanitize_geometry(sm):
    """NaN/inf-компоненты позиций → 0. Возвращает число исправленных вершин."""
    d = sm.get_static_mesh_description(0)
    if d is None:
        return 0
    n = d.get_vertex_count()
    fixed = 0
    for i in range(n):
        vid = _vid(i)
        try:
            if not d.is_vertex_valid(vid):
                continue
            p = d.get_vertex_position(vid)
        except Exception:
            continue
        c = [p.x, p.y, p.z]
        dirty = False
        for k in range(3):
            if c[k] != c[k] or abs(c[k]) > 1e30:
                c[k] = 0.0
                dirty = True
        if dirty:
            d.set_vertex_position(vid, unreal.Vector(c[0], c[1], c[2]))
            fixed += 1
    return fixed


def clear_fast_build(sm):
    """Снять bDoFastBuild через штатный обработчик движка (включить-выключить Nanite).

    UStaticMesh::PostEditChangeProperty: при изменении NaniteSettings с bEnabled=True движок
    выполняет `bDoFastBuild = false` и затем полный Build(). Второе изменение (выключение Nanite)
    оставляет флаг снятым и пересобирает меш уже обычным билдером — с корректными Bounds.
    """
    ns = sm.get_editor_property("nanite_settings")
    was = bool(ns.enabled)
    ns.set_editor_property("enabled", True)
    sm.set_editor_property("nanite_settings", ns)      # → bDoFastBuild = false + Build()
    ns2 = sm.get_editor_property("nanite_settings")
    ns2.set_editor_property("enabled", was)
    sm.set_editor_property("nanite_settings", ns2)     # → Build() обычным билдером
    return True


def _slots(sm):
    """Слепок слотов материалов — чтобы доказать, что пересборка их не потеряла."""
    out = []
    for m in sm.get_editor_property("static_materials"):
        mi = m.material_interface
        out.append("%s=%s" % (m.material_slot_name, mi.get_path_name() if mi else "None"))
    return out


def fix_mesh(pkg):
    sm = unreal.load_asset(pkg)
    if not isinstance(sm, unreal.StaticMesh):
        return None
    before = bounds_str(sm)
    slots_before = _slots(sm)
    nan_verts = has_nan_geometry(sm)
    if nan_verts:
        sanitize_geometry(sm)
    # (1) снять bDoFastBuild
    clear_fast_build(sm)
    # (2) перекоммитить описание LOD0 → новый GUID → новый ключ DDC → честная пересборка
    desc = sm.get_static_mesh_description(0)
    if desc is not None:
        sm.build_from_static_mesh_descriptions([desc], False, False)
    # (3) вернуть слоты материалов, если пересборка их переписала
    slots_after = _slots(sm)
    if slots_after != slots_before:
        sm.set_editor_property("static_materials", _saved_slots(sm, slots_before))
        slots_after = _slots(sm)
    after = bounds_str(sm)
    EAL.save_loaded_asset(sm)
    return (before, after, nan_verts, slots_before, slots_after)


def _saved_slots(sm, snapshot):
    """Восстановление массива StaticMaterial из слепка вида 'Slot=/Path/To/Mat'."""
    out = []
    for s in snapshot:
        name, _, path = s.partition("=")
        mi = unreal.load_asset(path) if path and path != "None" else None
        out.append(unreal.StaticMaterial(material_interface=mi, material_slot_name=unreal.Name(name)))
    return out


def collect():
    lst = os.environ.get("ASDA_LIST", "")
    if lst and os.path.isfile(lst):
        return [_pkg(l.strip()) for l in open(lst, "r", encoding="utf-8") if l.strip()]
    roots = os.environ.get("ASDA_ROOTS", "/Game/Map||/Game/AsdaAssets/Models/Backdrop").split("||")
    out = []
    for r in roots:
        for ad in AR.get_assets_by_path(unreal.Name(r.strip()), recursive=True):
            if str(ad.asset_class_path.asset_name) == "StaticMesh":
                out.append(str(ad.package_name))
    return out


def main():
    pkgs = collect()
    # ★ 15.09.2026: меши сцены логина (Models/Backdrop — корень по умолчанию; Char/cre_creback-a_00, cre_creation01*) — только с
    #   ASDA_ALLOW_LOGIN_WRITE=1 (asda_login_guard). Пропуск до обработки, итог одной строкой.
    import sys as _sys_lg   # ★ 15.09.2026: защита сцены логина (asda_login_guard: Models/Backdrop, Char/cre_creback-a_00, cre_creation01* и др.)
    _sys_lg.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
    import asda_login_guard as _LG
    _n_all = len(pkgs)
    pkgs = [p for p in pkgs if not _LG.login_package_blocked(p, "asda_fix_fastbuild_bounds.py", quiet=True)]
    if len(pkgs) != _n_all:
        log("[LOGIN-GUARD] пропущено мешей сцены логина: %d (запись — только с ASDA_ALLOW_LOGIN_WRITE=1)" % (_n_all - len(pkgs)))
    lim = int(os.environ.get("ASDA_LIMIT", "0"))
    if lim > 0:
        pkgs = pkgs[:lim]
    log("к обработке: %d" % len(pkgs))
    ok = skipped = 0
    verbose = (os.environ.get("ASDA_VERBOSE", "0") == "1")
    for i, p in enumerate(pkgs):
        try:
            r = fix_mesh(p)
        except Exception as e:
            log("ОШИБКА %s: %s" % (p, e))
            skipped += 1
            continue
        if r is None:
            skipped += 1
            continue
        ok += 1
        if verbose or i < 5:
            log("%s\n    ДО:    %s\n    ПОСЛЕ: %s\n    NaN-вершин было: %d\n    слоты до:    %s\n    слоты после: %s"
                % (p, r[0], r[1], r[2], r[3], r[4]))
        elif r[3] != r[4]:
            log("%s: слоты материалов РАЗОШЛИСЬ %s → %s" % (p, r[3], r[4]))
        if (i + 1) % 200 == 0:
            log("...%d/%d" % (i + 1, len(pkgs)))
            try:
                unreal.SystemLibrary.collect_garbage()
            except Exception:
                pass
    log("ГОТОВО: вылечено %d, пропущено %d" % (ok, skipped))


if __name__ == "__main__":
    main()
