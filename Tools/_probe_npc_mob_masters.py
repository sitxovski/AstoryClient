# Copyright (c) 2026 @sitxovski (Discord)
# _probe_npc_mob_masters.py — РАЗВЕДКА (только чтение) перед фиксом мастеров NPC/мобов.
#   1) свойства и параметры мастеров M_AsdaObjectLit / M_AsdaCharLit (что наследует новый blend-мастер);
#   2) инвентаризация MIC по корням Models/NPC, Models/MobSkel, Models/Mob — сколько на каком мастере;
#   3) ★ ПРОВЕРКА МОДЕЛИ СЛОТОВ: гипотеза «UE сливает подсетки с ОДИНАКОВЫМ именем материала в ОДИН
#      слот меша» (у экспортёра имя материала = имя текстуры без расширения). Тогда слот = уникальная
#      текстура модели в порядке первого появления, и мастер выбирается ПО СЛОТУ, а не по мешу.
# Запуск: run_headless.bat @@ASTORY_DEV_ROOT@@\AstoryClient\Tools\_probe_npc_mob_masters.py
import os, sys, zipfile, collections
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import unreal
import asda_ue_paths as _U
import asda_paths as _P
import asda_jbm
from asda_npc_mob_masters import ROOTS

EAL = unreal.EditorAssetLibrary
AR = unreal.AssetRegistryHelpers.get_asset_registry()
MEL = unreal.MaterialEditingLibrary


def log(m): unreal.log_warning("[probe] %s" % m)


def inventory():
    """Мастера по MIC-АССЕТАМ и (важнее) по РЕАЛЬНО ИСПОЛЬЗУЕМЫМ СЛОТАМ мешей.

    ★ Считать надо именно слоты: у модели MIC-ассетов столько же, сколько подсеток, но меш
      ссылается лишь на один MIC на слот (остальные — сироты от дедупа Interchange по имени).
    """
    for key, root, sub, prop, _arc in ROOTS:
        cnt = collections.Counter()
        for ad in AR.get_assets_by_path(unreal.Name(root), recursive=True):
            if str(ad.asset_class_path.asset_name) != "MaterialInstanceConstant":
                continue
            a = unreal.load_asset(str(ad.package_name))
            par = a.get_editor_property("parent") if a else None
            cnt[par.get_name() if par else "НЕТ"] += 1
        log("%s: MIC-АССЕТОВ по мастерам: %s" % (root, dict(cnt)))
        slot_cnt = collections.Counter()
        for d in EAL.list_assets(root, recursive=False, include_folder=True):
            model = d.rstrip("/").rsplit("/", 1)[-1]
            if not model:
                continue
            mesh = unreal.load_asset("%s/%s/%s/%s" % (root, model, sub, model))
            if not mesh:
                continue
            for e in mesh.get_editor_property(prop):
                mi = e.get_editor_property("material_interface")
                par = mi.get_editor_property("parent") if isinstance(mi, unreal.MaterialInstanceConstant) else None
                slot_cnt[par.get_name() if par else "НЕТ"] += 1
        log("%s: СЛОТОВ МЕШЕЙ по мастерам: %s" % (root, dict(slot_cnt)))


def uniq_tex(recs):
    """Уникальные имена материалов (=текстура без расширения) в порядке ПЕРВОГО появления."""
    out = []
    for r in recs:
        b = os.path.splitext(r["tex"])[0] or "mat"
        if b not in out: out.append(b)
    return out


def check(root, sub, prop, archive):
    z = zipfile.ZipFile(archive)
    names = {n.lower(): n for n in z.namelist() if n.lower().endswith(".jbm")}
    models = [d.rstrip("/").rsplit("/", 1)[-1]
              for d in EAL.list_assets(root, recursive=False, include_folder=True)]
    ok = bad = miss = 0; bad_list = []; used_mic = 0
    for model in [m for m in models if m]:
        key = (model + ".jbm").lower()
        mesh = unreal.load_asset("%s/%s/%s/%s" % (root, model, sub, model))
        if key not in names or not mesh:
            miss += 1; continue
        h = asda_jbm.parse_header(z.read(names[key]))
        if not h: miss += 1; continue
        mats = mesh.get_editor_property(prop)
        slots = [str(m.get_editor_property("material_slot_name")) for m in mats]
        used_mic += len(mats)
        exp = uniq_tex(h["recs"])
        if slots == exp[:len(slots)]:
            ok += 1
        else:
            bad += 1
            if len(bad_list) < 10:
                bad_list.append((model, len(slots), len(exp),
                                 slots[:8], exp[:8]))
    log("%s: слот==уникальная текстура — СОВПАЛО %d, не совпало %d, пропущено %d; слотов всего %d"
        % (root, ok, bad, miss, used_mic))
    for b in bad_list:
        log("   расхождение %s: слотов %d, уник.текстур %d\n      UE  : %s\n      .jbm: %s" % b)


def main():
    inventory()
    for root, sub, prop, arc in ((_U.G_MODELS_NPC, "SkeletalMeshes", "materials", _P.NPC_MODEL_BNZ),
                                 (_U.G_MODELS_MOBSKEL, "SkeletalMeshes", "materials", _P.MOB_MODEL_BNZ),
                                 (_U.G_MODELS_MOB, "StaticMeshes", "static_materials",
                                  os.path.join(_P.DATA_ORIG, "Mob", "Model", "MobModel.bnz"))):
        check(root, sub, prop, arc)
    # точечный дамп двух моделей: какие MIC реально стоят в слотах
    for path in ("%s/npc_f_h_charlotte-a_00/SkeletalMeshes/npc_f_h_charlotte-a_00" % _U.G_MODELS_NPC,
                 "%s/mon_s_0_ruby02-a_00/StaticMeshes/mon_s_0_ruby02-a_00" % _U.G_MODELS_MOB,
                 "%s/mon_s_0_rcrob01-a_00/SkeletalMeshes/mon_s_0_rcrob01-a_00" % _U.G_MODELS_MOBSKEL):
        m = unreal.load_asset(path)
        if not m: log("НЕТ меша %s" % path); continue
        prop = "materials" if isinstance(m, unreal.SkeletalMesh) else "static_materials"
        for i, s in enumerate(m.get_editor_property(prop)):
            mi = s.get_editor_property("material_interface")
            log("   %s слот %d: имя=%s MIC=%s" % (os.path.basename(path), i,
                                                  s.get_editor_property("material_slot_name"),
                                                  mi.get_name() if mi else "НЕТ"))
    unreal.log("=== PROBE DONE ===")


if __name__ == "__main__" or globals().get("__spec__") is None:
    main()
