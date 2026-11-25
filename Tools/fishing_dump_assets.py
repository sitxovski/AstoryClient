# Copyright (c) 2026 @sitxovski (Discord)
# fishing_dump_assets.py — РЫБАЛКА, волна В5а: проверочный дамп импортированных ассетов (headless, только чтение).
#
# Что проверяется:
#   1) клипы персонажа ch1_{m|f}_cremmy_previewanim_225..232: есть, скелет = ch1_{g}_cremmy_preview_Skeleton,
#      длительность/кадры, ИМЕНА ТРЕКОВ ⊆ костей скелета (как у эмоций anim_86..97 — контрольная пара);
#   2) скелеталы /Game/AsdaAssets/Models/Skeletal/Fishing/*: кости (обязательные якоря 8a1_dummy/Bone24 у удочек,
#      fs_Bone01 у рыб), число клипов и их длительность, материалы (MIC → родитель, текстура Tex);
#   3) BP_Fx_fish_end_01/02: ассет есть, CDO effect_name, число эмиттеров.
# Результат: JSON в Reports/owner_1008/fishing_W5a_assets_dump.json + строки журнала «[fishing-dump]».
# Запуск: python Tools/run_headless_safe.py Tools/fishing_dump_assets.py --marker "FISHING_DUMP_DONE"
# ★ ЗАКОН [[tools-no-import-side-effects]]: при импорте модуль НИЧЕГО не делает.
import json
import os
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import unreal
import asda_ue_paths as _U

HERE = os.path.dirname(os.path.abspath(__file__))
# Файл результата дампа (читают отчёт волны и волна 5)
OUT_JSON = os.path.abspath(os.path.join(HERE, "..", "..", "Reports", "owner_1008",
                                        "fishing_W5a_assets_dump.json"))
CHAR_BASE = _U.G_MODELS_CHARACTERS + "/ch1_%s_cremmy_preview/SkeletalMeshes/ch1_%s_cremmy_preview"  # база клипов
CHAR_IDX = list(range(225, 233))      # клипы рыбалки персонажа
CTRL_IDX = [86, 93]                   # контрольная пара: эмоции (happy01, dance01) — должны пройти так же
FISH_DIR = _U.G_MODELS_SKELETAL + "/Fishing"   # каталог скелеталов рыбалки
FX = ["fish_end_01", "fish_end_02"]  # эффекты рыбалки
REQUIRED = {"ch1_n_00_fird": ["8a1_dummy", "Bone24"], "fs_fsm_": ["fs_Bone01"], "fs_stuff_": ["fs_Bone01"]}

AR = unreal.AssetRegistryHelpers.get_asset_registry()
MEL = unreal.MaterialEditingLibrary


def log(m):
    """Строка журнала дампа."""
    unreal.log_warning("[fishing-dump] %s" % m)


def bone_names(skel):
    """Имена костей скелета (референс-поза)."""
    try:
        pose = unreal.AnimPoseExtensions.get_reference_pose(skel)
        return [str(n) for n in unreal.AnimPoseExtensions.get_bone_names(pose)]
    except Exception as e:
        log("! кости %s: %s" % (skel.get_name() if skel else None, e))
        return []


def anim_info(a, skel_bones=None):
    """Длительность, кадры, скелет и треки клипа (+ треки вне скелета)."""
    sk = a.get_editor_property("skeleton")
    try:
        tracks = [str(t) for t in unreal.AnimationLibrary.get_animation_track_names(a)]
    except Exception:
        tracks = []
    try:
        nfr = int(unreal.AnimationLibrary.get_num_frames(a))
    except Exception:
        nfr = -1
    bones = skel_bones if skel_bones is not None else (bone_names(sk) if sk else [])
    bl = {b.lower() for b in bones}
    return dict(length=round(float(a.get_editor_property("sequence_length")), 4), frames=nfr,
                skeleton=sk.get_name() if sk else None, tracks=len(tracks),
                tracks_outside_skeleton=[t for t in tracks if t.lower() not in bl])


def mat_info(skm):
    """Слоты материалов скелетала: имя слота → MIC, родитель, текстура Tex."""
    out = []
    for sm in skm.get_editor_property("materials"):
        mi = sm.get_editor_property("material_interface")
        rec = dict(slot=str(sm.get_editor_property("material_slot_name")), mat=mi.get_name() if mi else None)
        if isinstance(mi, unreal.MaterialInstanceConstant):
            par = mi.get_editor_property("parent")
            tex = MEL.get_material_instance_texture_parameter_value(mi, "Tex")
            rec.update(parent=par.get_name() if par else None, tex=tex.get_path_name() if tex else None)
        out.append(rec)
    return out


def main():
    """Полный дамп; печатает маркер FISHING_DUMP_DONE и сводку ok/fail."""
    res = dict(char={}, models={}, fx={})
    fails = []
    for g in ("m", "f"):
        base = CHAR_BASE % (g, g)
        skel = unreal.load_asset(_U.skel_mesh(_U.G_MODELS_CHARACTERS, _U.char_preview_name(g == "m"), "_Skeleton"))
        sb = bone_names(skel)
        res["char"][g] = dict(skeleton=skel.get_name() if skel else None, skeleton_bones=len(sb), clips={})
        for n in CHAR_IDX + CTRL_IDX:
            a = unreal.load_asset("%sanim_%02d" % (base, n))
            if not a:
                fails.append("char %s anim_%d нет" % (g, n))
                continue
            inf = anim_info(a, sb)
            res["char"][g]["clips"]["anim_%d" % n] = inf
            log("char %s anim_%d len=%.4f fr=%d skel=%s треков=%d вне скелета=%s"
                % (g, n, inf["length"], inf["frames"], inf["skeleton"], inf["tracks"], inf["tracks_outside_skeleton"]))
            if n in CHAR_IDX and (inf["length"] <= 0 or inf["skeleton"] != (skel.get_name() if skel else "")
                                  or inf["tracks_outside_skeleton"] or inf["tracks"] == 0):
                fails.append("char %s anim_%d: %s" % (g, n, inf))

    for ad in AR.get_assets_by_path(unreal.Name(FISH_DIR), recursive=True):
        if str(ad.asset_class_path.asset_name) != "SkeletalMesh":
            continue
        skm = unreal.load_asset(str(ad.package_name))
        name = skm.get_name()
        sk = skm.get_editor_property("skeleton")
        sb = bone_names(sk)
        anims = {}
        for ad2 in AR.get_assets_by_path(unreal.Name(os.path.dirname(str(ad.package_name))), recursive=False):
            if str(ad2.asset_class_path.asset_name) == "AnimSequence":
                a = unreal.load_asset(str(ad2.package_name))
                anims[a.get_name()[len(name):]] = anim_info(a, sb)
        rec = dict(path=str(ad.package_name), skeleton=sk.get_name() if sk else None, bones=sb,
                   clips=anims, materials=mat_info(skm))
        res["models"][name] = rec
        need = next((v for k, v in REQUIRED.items() if name.lower().startswith(k.lower())), [])
        miss = [b for b in need if b.lower() not in {x.lower() for x in sb}]
        nomat = [m for m in rec["materials"] if not m.get("tex")]
        bad = [k for k, v in anims.items() if v["length"] <= 0 or v["tracks_outside_skeleton"]
               or not k.startswith("anim_")]   # путь рантайма: «<модель>anim_%02d»
        log("%s кости=%d клипов=%d %s матер=%s%s%s%s" % (
            name, len(sb), len(anims),
            ",".join("%s:%.3f" % (k, v["length"]) for k, v in sorted(anims.items())),
            [(m["mat"], (m.get("tex") or "НЕТ").split(".")[-1]) for m in rec["materials"]],
            (" !нет костей %s" % miss) if miss else "", (" !без текстуры") if nomat else "",
            (" !плохие клипы %s" % bad) if bad else ""))
        if miss or nomat or bad or not anims:
            fails.append("model %s miss=%s nomat=%d bad=%s anims=%d" % (name, miss, len(nomat), bad, len(anims)))

    for j in FX:
        bpn = _U.fx_bp_name(j)
        path = "%s/%s" % (_U.G_EFFECTS, bpn)
        bp = unreal.load_asset(path)
        if not bp:
            fails.append("fx %s нет" % path)
            res["fx"][j] = None
            continue
        cdo = unreal.get_default_object(bp.generated_class())
        try:
            ems = cdo.get_editor_property("emitters")
            nem = len(ems)
            types = [(str(e.get_editor_property("name")), int(e.get_editor_property("jeb_type")),
                      int(e.get_editor_property("end_mode")),
                      (e.get_editor_property("material").get_name() if e.get_editor_property("material") else None))
                     for e in ems]
        except Exception as e:
            nem, types = -1, str(e)
        res["fx"][j] = dict(path=path, effect_name=str(cdo.get_editor_property("effect_name")), emitters=nem,
                            types=types)
        log("fx %s effect_name=%s эмиттеров=%s типы=%s" % (bpn, res["fx"][j]["effect_name"], nem, types))
        if nem <= 0:
            fails.append("fx %s без эмиттеров" % bpn)

    res["fails"] = fails
    os.makedirs(os.path.dirname(OUT_JSON), exist_ok=True)
    with open(OUT_JSON, "w", encoding="utf-8") as f:
        json.dump(res, f, ensure_ascii=False, indent=1)
    log("моделей=%d, ошибок=%d" % (len(res["models"]), len(fails)))
    for x in fails:
        log("FAIL " + x)
    log("FISHING_DUMP_DONE %s" % ("OK" if not fails else "FAIL"))


if __name__ == "__main__" or globals().get("__spec__") is None:
    main()
