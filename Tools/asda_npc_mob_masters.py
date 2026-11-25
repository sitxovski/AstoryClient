# Copyright (c) 2026 @sitxovski (Discord)
# asda_npc_mob_masters.py — ВЫБОР МАСТЕР-МАТЕРИАЛА ДЛЯ NPC И МОБОВ ПО ТЕХНИКЕ ПОДСЕТКИ.
#
# ★★★ ЧТО ЧИНИМ. До 2026-08-18 все MIC под `Models/NPC`, `Models/MobSkel`, `Models/Mob` сидели на
#   masked-мастере `M_AsdaObjectLit` (OpacityMask = Tex.A, clip 0.5). Мастер выбирался по ПРЕФИКСУ
#   имени ассета (`asda_material_policy.master_for`: `ch1_*` → непрозрачный, всё прочее → masked),
#   а прозрачность в оригинале решает ТЕХНИКА ШЕЙДЕРА, выбираемая ПО-МЕШНО из `meshType`
#   (`renderApplyMeshState@0x1116FE0`). У техник `tSkin`/`tRigid` альфа текстуры в прозрачность НЕ
#   идёт вовсе (`PShade()`: `Output.RGBColor.w = 1 * fog*4 * fade*0.25`) — она работает маской блика
#   и кубмапа. То есть masked-мастер вырезал у NPC и мобов то, что оригинал рисует блеском.
#   Это ровно тот баг, что чинили 2026-07-31 для `ch1_*` (`Reports/weapon_texture_alpha_fix.md`),
#   только в другой группе; замер и разбор — `Reports/blend_rollout_npc_mobs.md`.
#
# ★ КАК ВЫБИРАЕМ. Класс подсетки считает `asda_mesh_render_class.mesh_class()` из `meshType` и
#   `renderFlags` самой модели `.jbm`; мастер по классу даёт `asda_material_policy.master_for_class()`:
#       CHAR (tSkin/tRigid)                → M_AsdaCharLit       (непрозрачный, альфа → Specular)
#       MASK (tNoSkin без бита блендинга)  → M_AsdaObjectLit     (вырезка по альфе)
#       BLEND(tNoSkin с битом 0x1|0x2)     → M_AsdaObjectBlendLit(альфа-блендинг + отсечка 8/255)
#
# ★ КАК ПОПАДАЕМ В НУЖНЫЙ MIC. Слот меша в UE = УНИКАЛЬНОЕ ИМЯ МАТЕРИАЛА .glb, а имя материала —
#   имя текстуры подсетки (плюс суффикс класса у разведённых слотов). Совпадение проверено сплошным
#   замером (`Tools/_probe_npc_mob_masters.py`): у 133 моделей NPC и 99 моделей MobSkel список слотов
#   ПОБИТОВО равен списку уникальных имён материалов в порядке первого появления. Поэтому идём не по
#   индексам, а ПО ИМЕНИ СЛОТА — переупорядочивание слотов импортом ничего не сломает, а
#   несовпадение имени сразу видно в отчёте (строки «слот без эталона»).
#
# ★ ГРАБЛЯ (записана в июле, `weapon_texture_alpha_fix.md` §4): `set_material_instance_parent()`
#   НЕ помечает пакет dirty, а `save_loaded_asset(asset)` по умолчанию сохраняет только dirty ⇒
#   отчёт «переведено N», а на диск не ложится НИЧЕГО. Нужны `mic.modify()` и
#   `save_loaded_asset(mic, False)`.
#
# Запуск отдельно:  run_headless.bat @@ASTORY_DEV_ROOT@@\AstoryClient\Tools\asda_npc_mob_masters.py
#   ASDA_DRYRUN=1            — только показать «было → стало»;
#   ASDA_ONLY_ROOTS=NPC,Mob  — ограничить корни (NPC | MobSkel | Mob).
# Импортируется импортёрами (asda_import_skinned_models, asda_import_mob_skel,
#   asda_fix_npc_materials, asda_fix_mob_materials) — тогда переимпорт воспроизводит результат.
import os
import sys as _sys
_sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import zipfile

import asda_paths as _P
import asda_ue_paths as _U
import asda_jbm as JBM
import asda_mesh_render_class as MC
import asda_material_policy as POL

# (ключ, корень /Game, подпапка мешей, свойство списка материалов, архив .jbm)
# ★ Статик-мобы (`Models/Mob`) собраны из ЧИСТОЙ сборки клиента (asda_build_models.GAME = DATA_ORIG),
#   скелетал-мобы и NPC — из рабочей (DATA). Архивы РАЗНЫЕ (MobModel.bnz расходится 1 моделью из 132),
#   поэтому каждому корню — свой источник, иначе класс слота может не найтись.
ROOTS = (
    ("NPC",     _U.G_MODELS_NPC,     "SkeletalMeshes", "materials",        _P.NPC_MODEL_BNZ),
    ("MobSkel", _U.G_MODELS_MOBSKEL, "SkeletalMeshes", "materials",        _P.MOB_MODEL_BNZ),
    ("Mob",     _U.G_MODELS_MOB,     "StaticMeshes",   "static_materials",
     os.path.join(_P.DATA_ORIG, "Mob", "Model", "MobModel.bnz")),
)


def slot_class_index(archive):
    """{модель(lower): {имя слота: класс}} по архиву моделей."""
    out = {}
    with zipfile.ZipFile(archive) as z:
        for n in z.namelist():
            if not n.lower().endswith(".jbm"):
                continue
            hdr = JBM.parse_header(z.read(n))
            if not hdr:
                continue
            model = os.path.splitext(os.path.basename(n))[0].lower()
            out[model] = MC.slot_classes(hdr["recs"], split=True, enable_blend=True)
    return out


def _master_cache(unreal):
    cache = {}
    for cls in (MC.CLS_CHAR, MC.CLS_MASK, MC.CLS_BLEND):
        path = POL.master_for_class(cls)
        cache[cls] = unreal.load_asset(path)
        if not cache[cls]:
            unreal.log_warning("[мастера NPC/мобов] НЕТ мастера %s (класс %s) — соберите его "
                               "(asda_build_materials_lit.py / asda_build_blend_material.py)" % (path, cls))
    return cache


def apply(only_roots=None, only_models=None, dry=False, log=None, model_alias=None):
    """Переподвязывает MIC слотов на мастер по классу подсетки. Идемпотентно.

    only_roots  — множество ключей ROOTS ('NPC'/'MobSkel'/'Mob'); None = все.
    only_models — множество имён моделей (lower); None = все.
    model_alias — {имя папки (lower): имя модели в архиве (lower)} — ★ 30.10.2026 для вариантов «меш + клипы базы»
                  `<меш>__<база>` (`asda_import_mob_clipbase.py`): класс слота берётся у меша. None = как прежде.
    Возвращает dict со статистикой.
    """
    import unreal
    EAL = unreal.EditorAssetLibrary
    MEL = unreal.MaterialEditingLibrary
    if log is None:
        def log(m): unreal.log_warning("[мастера NPC/мобов] %s" % m)

    masters = _master_cache(unreal)
    st = {"changed": 0, "same": 0, "no_etalon": [], "no_slot": [], "by_class": {}, "changes": []}
    for key, root, sub, prop, archive in ROOTS:
        if only_roots and key not in only_roots:
            continue
        if not EAL.does_directory_exist(root):
            continue
        idx = slot_class_index(archive)
        for d in EAL.list_assets(root, recursive=False, include_folder=True):
            model = d.rstrip("/").rsplit("/", 1)[-1]
            if not model or (only_models and model.lower() not in only_models):
                continue
            mesh = unreal.load_asset("%s/%s/%s/%s" % (root, model, sub, model))
            if not mesh:
                continue
            slots = idx.get((model_alias or {}).get(model.lower(), model.lower()))
            if not slots:
                st["no_etalon"].append("%s/%s" % (key, model)); continue
            for entry in mesh.get_editor_property(prop):
                name = str(entry.get_editor_property("material_slot_name"))
                mic = entry.get_editor_property("material_interface")
                if not isinstance(mic, unreal.MaterialInstanceConstant):
                    continue
                cls = slots.get(name)
                if cls is None:
                    st["no_slot"].append("%s/%s слот %s" % (key, model, name)); continue
                master = masters.get(cls)
                if master is None:
                    continue
                st["by_class"][cls] = st["by_class"].get(cls, 0) + 1
                cur = mic.get_editor_property("parent")
                if cur and cur.get_name() == master.get_name():
                    st["same"] += 1
                    continue
                st["changes"].append("%-8s %-32s слот %-34s %-20s → %s"
                                     % (key, model, name, cur.get_name() if cur else "НЕТ",
                                        master.get_name()))
                st["changed"] += 1
                if not dry:
                    MEL.set_material_instance_parent(mic, master)
                    # ★ Грабля июля: без modify()+save(..., False) правка не ложится на диск.
                    mic.modify()
                    EAL.save_loaded_asset(mic, False)
    log("переведено: %d | уже верных: %d | по классам: %s%s"
        % (st["changed"], st["same"], st["by_class"], "  [DRYRUN]" if dry else ""))
    for c in st["changes"][:400]:
        unreal.log("   " + c)
    if len(st["changes"]) > 400:
        log("   ... ещё %d строк" % (len(st["changes"]) - 400))
    for c in sorted(set(st["no_etalon"])):
        log("   МОДЕЛИ НЕТ В АРХИВЕ: " + c)
    for c in sorted(set(st["no_slot"])):
        log("   СЛОТА НЕТ В ЭТАЛОНЕ (нужен переэкспорт .glb): " + c)
    return st


def main():
    roots = set(s.strip() for s in os.environ.get("ASDA_ONLY_ROOTS", "").split(",") if s.strip())
    models = set(s.strip().lower() for s in os.environ.get("ASDA_ONLY_MODELS", "").split(",") if s.strip())
    apply(only_roots=roots or None, only_models=models or None,
          dry=os.environ.get("ASDA_DRYRUN", "") not in ("", "0"))
    import unreal
    unreal.log("=== NPC/MOB MASTER FIX DONE ===")


# ★ ЗАКОН [[tools-no-import-side-effects]]: при импорте модуль НИЧЕГО не делает.
if __name__ == "__main__" or globals().get("__spec__") is None:
    main()
