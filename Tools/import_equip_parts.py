# Copyright (c) 2026 @sitxovski (Discord)
# import_equip_parts.py — headless-импорт .glb частей снаряги (chst/pant/shoe, оба пола) как SkeletalMesh
# на ОБЩИЙ скелет cre_cremmy (тот же USkeleton, что превью-тело char-select) — чтобы работал LeaderPose.
#
# ★ КЛЮЧЕВОЕ: каждая часть НЕ создаёт свой скелет. Через Interchange-pipeline override принудительно
#   назначаем существующий скелет превью-тела:
#     мужские части -> ch1_m_cremmy_preview_Skeleton, женские -> ch1_f_cremmy_preview_Skeleton.
#   (pipeline.common_skeletal_meshes_and_animations_properties.skeleton = target).
#   UE до-мёрджит недостающие кости в общий скелет → ОДИН USkeleton на пол, LeaderPose совместим.
#
# Импорт: SkeletalMesh + AnimSequence(anim_00=idle) + текстура части + материал (M_AsdaObject, Tex, Ambient=1).
# Назначение: /Game/AsdaAssets/Models/Skeletal/EquipParts/<mesh>/...
#
# Перед запуском УБИТЬ UnrealEditor-Cmd (делает обёртка PowerShell).
# Запуск:
#   run_headless.bat import_equip_parts.py            # всё
#   run_headless.bat import_equip_parts.py m          # только мужские
#   (аргументы chst/pant/shoe/m/f и --limit N сужают набор; --no-tex пропускает импорт текстур)
import os, sys, unreal

HERE = os.path.dirname(os.path.abspath(__file__))
RAW  = os.path.abspath(os.path.join(HERE, "..", "RawAssets", "Models", "Skeletal", "EquipParts"))
TEXSRC = os.path.abspath(os.path.join(HERE, "..", "RawAssets", "Textures", "Data", "Character", "Texture"))
DEST   = "/Game/AsdaAssets/Models/Skeletal/EquipParts"
TEXDEST = "/Game/AsdaAssets/Textures/Characters/Texture"
TEX_ROOT = "/Game/AsdaAssets/Textures"
MASTER   = "/Game/Materials/M_AsdaCharLit"    # ★ 2026-07-31 ПЕРСОНАЖИ = НЕПРОЗРАЧНЫЙ мастер (эталон PShade: альфа=маска блика, не прозрачность; asda_material_policy.py). ★ ПЕРСОНАЖИ = LIT (unlit-мастер давал пересвет лица/тела vs Lit-экип; см. face_overbright_fix.md)
SKEL = {"m": "/Game/AsdaAssets/Models/Skeletal/Characters/ch1_m_cremmy_preview/SkeletalMeshes/ch1_m_cremmy_preview_Skeleton",
        "f": "/Game/AsdaAssets/Models/Skeletal/Characters/ch1_f_cremmy_preview/SkeletalMeshes/ch1_f_cremmy_preview_Skeleton"}
CHUNK = 12
INCREMENTAL = True

EAL  = unreal.EditorAssetLibrary
ATOOLS = unreal.AssetToolsHelpers.get_asset_tools()
MEL  = unreal.MaterialEditingLibrary
AR   = unreal.AssetRegistryHelpers.get_asset_registry()
def log(m): unreal.log("[import_equip_parts] " + str(m))

def gender_of(name): return "m" if "_m_" in name else "f"

import os as _mi_os, sys as _mi_sys
_mi_sys.path.insert(0, _mi_os.path.dirname(_mi_os.path.abspath(__file__)))
# ★ ВОЛНА 2 (14.09.2026, дефект E11): индекс/поиск текстур, подвязка MIC, импорт и Nanite — из ЕДИНОЙ
#   библиотеки asda_model_import_ue. Пресеты повторяют прежние копии этого файла (правило дублей имён,
#   нормализация ключа) — найденная текстура прежняя; добавлено только снятие меток `_msk`/`_blend` там, где
#   раньше текстура не находилась (решение координатора, `Reports/dedup_audit/wave2_import.md` §2, §8).
import asda_model_import_ue as MI

def tex_index():
    """Индекс Texture2D (пресет «all_last» библиотеки = прежняя копия этого файла)."""
    return MI.tex_index("all_last", root=TEX_ROOT)

def find_tex(name, tex):
    """Текстура для слота/MIC (пресет «plain» библиотеки = прежняя копия этого файла)."""
    return MI.find_tex(name, tex, "plain")

def import_textures(part_names):
    files = [os.path.join(TEXSRC, p + ".png") for p in part_names]
    files = [f for f in files if os.path.exists(f)]
    if not files: return 0
    MI.import_batch(files, TEXDEST)   # ★ дедуп №2 Т17: общий импорт пачкой (AutomatedAssetImportData)
    return len(files)

def make_task(gp, part, skeleton_asset):
    """AssetImportTask: части в СВОЮ папку DEST/<part>, ПРИНУДИТЕЛЬНО общий скелет, БЕЗ авто-материала
    (чтобы меш сохранил чистое имя <part> без коллизии; MIC создаём сами в post_process)."""
    pipe = unreal.InterchangeGenericAssetsPipeline()
    pipe.get_editor_property("common_skeletal_meshes_and_animations_properties").set_editor_property("skeleton", skeleton_asset)
    # ★ Авторские нормали из glb (JBM float 3..5): Interchange по умолчанию их ПЕРЕСЧИТЫВАЕТ (SharedSettings.h:127 bRecomputeNormals=true) — 1081 часть экипа жила с пересчётом до 05.09.2026 (review_normals_ue.md).
    pipe.get_editor_property("common_meshes_properties").set_editor_property("recompute_normals", False)
    mp = pipe.get_editor_property("material_pipeline")
    mp.set_editor_property("import_materials", False)     # не создавать авто-Material (иначе имя-коллизия -> меш '<part>1')
    tp = mp.get_editor_property("texture_pipeline") if hasattr(mp, "get_editor_property") else None
    ov = unreal.InterchangePipelineStackOverride(); ov.add_pipeline(pipe)
    t = unreal.AssetImportTask()
    t.set_editor_property("filename", gp)
    t.set_editor_property("destination_path", DEST + "/" + part)
    t.set_editor_property("automated", True)
    t.set_editor_property("replace_existing", True)
    t.set_editor_property("save", False)
    t.set_editor_property("options", ov)
    return t

def post_process(part, tex, master):
    """После импорта: MIC (M_AsdaObject+Tex+Ambient) на слот меша, nanite off, чистка PhysicsAsset."""
    sub = DEST + "/" + part
    sm = None; anim = None
    for ad in AR.get_assets_by_path(unreal.Name(sub), recursive=True):
        cn = str(ad.asset_class_path.asset_name)
        if cn == "SkeletalMesh": sm = unreal.load_asset(str(ad.package_name))
        elif cn == "PhysicsAsset":
            try: EAL.delete_asset(str(ad.package_name).split(".")[0])
            except Exception: pass
    if not sm:
        log("  !! %s: SkeletalMesh не найден после импорта" % part); return False
    # MIC
    mic_path = sub + "/" + part + "_mat"
    if EAL.does_asset_exist(mic_path):
        mic = unreal.load_asset(mic_path)
    else:
        mic = ATOOLS.create_asset(part + "_mat", sub, unreal.MaterialInstanceConstant,
                                  unreal.MaterialInstanceConstantFactoryNew())
    MEL.set_material_instance_parent(mic, master)
    tp = find_tex(part, tex)
    if tp: MEL.set_material_instance_texture_parameter_value(mic, "Tex", unreal.load_asset(tp))
    MEL.set_material_instance_scalar_parameter_value(mic, "Ambient", 1.0)
    # назначить MIC в слоты меша (import_materials=False => слотов может не быть — создаём один)
    mats = list(sm.get_editor_property("materials"))
    if not mats:
        mats = [unreal.SkeletalMaterial(material_interface=mic, material_slot_name=unreal.Name("mat0"))]
    else:
        for ms in mats: ms.set_editor_property("material_interface", mic)
    sm.set_editor_property("materials", mats)
    # nanite off
    try:
        ns = sm.get_editor_property("nanite_settings")
        if ns and ns.get_editor_property("enabled"):
            ns.set_editor_property("enabled", False); sm.set_editor_property("nanite_settings", ns)
    except Exception: pass
    return (tp is not None)

def wire_materials(dest, tex, master):
    """Все MIC под dest → master + Tex (+Ambient). Мастер ОДИН на все слоты (MASTER).
    ★ master_for НЕ ПОДКЛЮЧЁН — решение координатора волны 2 (14.09.2026): подключение сменит родителя существующих MIC при следующем прогоне (слоты с меткой _blend ушли бы на политику blend/masked вместо M_AsdaCharLit), а владелец правит ассеты руками — не терять. Отчёт `Reports/dedup_audit/wave2_import.md` §3.3, §8."""
    return MI.wire_materials(dest, tex_for=lambda n: find_tex(n, tex), master=master)["n_all"]

def disable_nanite(dest):
    """Снять Nanite ТОЛЬКО со SkeletalMesh под dest (как было) — asda_model_import_ue.disable_nanite."""
    MI.disable_nanite(dest, classes=("SkeletalMesh",))

def verify_skeleton(part, expected_pkg):
    """Проверить, что SkeletalMesh части ссылается на ОБЩИЙ скелет (тот же, что превью-тело)."""
    sub = DEST + "/" + part
    exp = expected_pkg.split(".")[0]
    found = False; ok = False; strays = []
    for ad in AR.get_assets_by_path(unreal.Name(sub), recursive=True):
        cn = str(ad.asset_class_path.asset_name)
        if cn == "SkeletalMesh":
            found = True
            sk = unreal.load_asset(str(ad.package_name)).get_editor_property("skeleton")
            skpath = sk.get_path_name().split(".")[0] if sk else None
            ok = (skpath == exp)
            if not ok: log("  !! %s: скелет=%s (ожидался %s)" % (part, skpath, exp))
        elif cn == "Skeleton" and str(ad.package_name).split(".")[0] != exp:
            strays.append(str(ad.package_name).split(".")[0])
    if not found: log("  !! %s: SkeletalMesh не найден" % part)
    return (ok and found), strays

def parse_args():
    tags = ("chst", "pant", "shoe"); genders = ("m", "f"); limit = None; do_tex = True; wipe = False
    for a in [x.lower() for x in sys.argv]:
        if a in ("chst", "pant", "shoe", "grov", "hcap"): tags = (a,)
        elif a in ("m", "f"): genders = (a,)
        elif a == "--no-tex": do_tex = False
        elif a == "--wipe": wipe = True
        elif a.startswith("--limit="): limit = int(a.split("=")[1])
    return tags, genders, limit, do_tex, wipe

def main():
    tags, genders, limit, do_tex, wipe = parse_args()
    if wipe and EAL.does_directory_exist(DEST):
        log("WIPE %s" % DEST); EAL.delete_directory(DEST)
    if not os.path.isdir(RAW):
        log("НЕТ папки RawAssets EquipParts: %s" % RAW); return
    allglb = sorted(f for f in os.listdir(RAW) if f.lower().endswith(".glb"))
    sel = [f for f in allglb if any(t in f for t in tags) and gender_of(f) in genders]
    if limit: sel = sel[:limit]
    log("glb всего=%d, к обработке=%d (tags=%s genders=%s)" % (len(allglb), len(sel), tags, genders))

    # инкрементально: пропуск уже импортированных (--force = переимпортировать существующие, напр. после перезапекания)
    force = any(a.lower() == "--force" for a in sys.argv)
    todo = []
    for f in sel:
        part = os.path.splitext(f)[0]
        sub = DEST + "/" + part
        if INCREMENTAL and not force and EAL.does_directory_exist(sub) and EAL.list_assets(sub):
            continue
        todo.append(part)
    log("к импорту=%d (пропуск существующих=%d)" % (len(todo), len(sel) - len(todo)))

    skel_assets = {g: unreal.load_asset(SKEL[g]) for g in genders}
    for g in genders:
        if not skel_assets[g]:
            log("!! НЕ загрузился скелет %s (%s) — части этого пола пропущены" % (g, SKEL[g]))

    master = unreal.load_asset(MASTER)
    total = mats = texn = nok = 0; all_strays = set()
    for i in range(0, len(todo), CHUNK):
        chunk = todo[i:i+CHUNK]
        if do_tex: texn += import_textures(chunk)
        tasks = [make_task(os.path.join(RAW, p + ".glb"), p, skel_assets[gender_of(p)])
                 for p in chunk if skel_assets[gender_of(p)]]
        if tasks: ATOOLS.import_asset_tasks(tasks)
        tex = tex_index()
        for p in chunk:
            if not skel_assets[gender_of(p)]: continue
            if post_process(p, tex, master): mats += 1
            ok, strays = verify_skeleton(p, SKEL[gender_of(p)])
            if ok: nok += 1
            all_strays.update(strays)
        total += len(chunk)
        EAL.save_directory(DEST, only_if_is_dirty=True, recursive=True)
        try: unreal.SystemLibrary.collect_garbage()
        except Exception: pass
        log("импортировано %d/%d (скелет-ok=%d, tex-mat=%d)" % (min(i+CHUNK, len(todo)), len(todo), nok, mats))

    # удалить случайно созданные левые скелеты (не должно быть — override даёт общий)
    for s in sorted(all_strays):
        if s.startswith(DEST) and EAL.does_asset_exist(s):
            try: EAL.delete_asset(s); log("удалён левый скелет %s" % s)
            except Exception as e: log("не удалить %s: %s" % (s, e))
    EAL.save_directory(DEST, only_if_is_dirty=False, recursive=True)

    log("=== ИТОГ: импортировано=%d, текстур=%d, материалов(с tex)=%d, общий-скелет-ok=%d/%d ===" % (
        total, texn, mats, nok, len(todo)))
    log("Ассеты: %s | Скелеты(общие): %s" % (DEST, SKEL))


# ★ ЗАКОН [[tools-no-import-side-effects]]: при импорте модуль НИЧЕГО не делает.
#   `__spec__` выставляет ЛЮБАЯ машинерия импорта (import / importlib / runpy) и НЕ
#   выставляет прямое исполнение файла (python file.py, UE -ExecutePythonScript).
#   `python -m` даёт `__name__ == "__main__"` и покрыт первым условием.
if __name__ == "__main__" or globals().get("__spec__") is None:
    main()
