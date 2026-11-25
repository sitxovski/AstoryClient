# Copyright (c) 2026 @sitxovski (Discord)
# asda_audit_npc_anim_assets.py — ТОЛЬКО ЧТЕНИЕ: аудит AnimSequence всех моделей NPC в проекте (04.11.2026, npc_talk_anim).
#   Для каждой папки /Game/AsdaAssets/Models/NPC/<model>/SkeletalMeshes: меш, его скелет, клипы anim_NN (или _Anim),
#   у каждого клипа — скелет (тот ли), число ключей и длина, дорожки костей; кости меша, на которые есть вес (по скин-весам
#   LOD0 не читаются из Python — берём ВСЕ кости референс-скелета меша) без дорожки = кость в клипе стоит в позе привязки
#   (дефект Ian: подмеш «npc_m_m_Ian-b_00» в разговорных клипах). NaN/огромный масштаб в дорожках.
#   Итог — JSON в Reports/npc_talk_anim/ue_audit.json и строка-маркер «АУДИТ NPC-КЛИПОВ: ГОТОВО».
# Запуск: python Tools\run_headless_safe.py Tools\asda_audit_npc_anim_assets.py --marker "АУДИТ NPC-КЛИПОВ: ГОТОВО"
# ★ ЗАКОН tools-no-import-side-effects: при импорте модуль НИЧЕГО не делает.
import os
import re
import json
import math

ROOT = "/Game/AsdaAssets/Models/NPC"
OUT = r"@@ASTORY_DEV_ROOT@@\Reports\npc_talk_anim\ue_audit.json"


def main():
    import unreal
    reg = unreal.AssetRegistryHelpers.get_asset_registry()
    assets = reg.get_assets_by_path(ROOT, recursive=True)
    by_folder = {}
    for ad in assets:
        cls = str(ad.asset_class_path.asset_name)
        if cls not in ("SkeletalMesh", "AnimSequence"):
            continue
        folder = str(ad.package_path)
        by_folder.setdefault(folder, {"mesh": [], "anims": []})
        by_folder[folder]["mesh" if cls == "SkeletalMesh" else "anims"].append(ad)
    res = []
    for folder in sorted(by_folder):
        ent = by_folder[folder]
        model = folder.split("/")[-2] if folder.endswith("SkeletalMeshes") else folder.split("/")[-1]
        rec = {"model": model, "issues": [], "clips": []}
        if not ent["mesh"]:
            rec["issues"].append("нет SkeletalMesh")
            res.append(rec)
            continue
        sm = ent["mesh"][0].get_asset()
        skel = sm.get_editor_property("skeleton")
        rec["skeleton"] = skel.get_path_name() if skel else None
        sk_bones = []
        # имена костей референс-скелета меша — через SkeletonModifier (есть в Python 5.x)
        try:
            mod = unreal.SkeletonModifier()
            mod.set_skeletal_mesh(sm)
            sk_bones = [str(b) for b in mod.get_all_bone_names()]
        except Exception as e:
            rec["issues"].append("кости скелета не прочитаны: %s" % e)
        rec["bones"] = len(sk_bones)
        all_tracks = set()
        for ad in sorted(ent["anims"], key=lambda a: str(a.asset_name)):
            seq = ad.get_asset()
            name = str(ad.asset_name)
            m = re.search(r"anim_(\d+)$", name)
            N = int(m.group(1)) if m else (0 if name.endswith("_Anim") else -1)
            c = {"name": name, "N": N}
            s2 = seq.get_editor_property("skeleton")
            c["skeleton_ok"] = bool(s2 and skel and s2.get_path_name() == skel.get_path_name())
            dm = seq.get_editor_property("data_model_interface")
            tracks = [str(t).lower() for t in dm.get_bone_track_names()]
            all_tracks |= set(tracks)
            c["tracks"] = len(tracks)
            c["keys"] = int(dm.get_number_of_keys())
            c["len"] = round(float(seq.get_play_length()), 3)
            c["missing"] = [b for b in sk_bones if b.lower() not in tracks and not b.lower().endswith("proxytruerootjoint")]
            bad = 0
            maxs = 0.0
            moving = {}
            frames = list(range(0, max(c["keys"], 1), 4)) + [max(c["keys"] - 1, 0)]
            for t in tracks:
                pts = []
                for fr in frames:
                    x = unreal.AnimationLibrary.get_bone_pose_for_frame(seq, t, fr, False)
                    v = (x.translation.x, x.translation.y, x.translation.z, x.rotation.x, x.rotation.y, x.rotation.z, x.rotation.w,
                         x.scale3d.x, x.scale3d.y, x.scale3d.z)
                    if any(not math.isfinite(a) for a in v):
                        bad += 1
                        continue
                    maxs = max(maxs, abs(x.scale3d.x), abs(x.scale3d.y), abs(x.scale3d.z))
                    pts.append(v)
                # «движется» = позиция гуляет > 1 ед. UE или кватернион > 1e-3 за клип
                mv = 0.0
                if pts:
                    for k in range(7):
                        col = [q[k] for q in pts]
                        mv = max(mv, (max(col) - min(col)) * (1.0 if k >= 3 else 0.001))
                moving[t] = mv > 1e-3
            c["moving"] = moving
            c["nan_keys"] = bad
            c["max_scale"] = round(maxs, 3)
            if not c["skeleton_ok"]:
                rec["issues"].append("%s: чужой скелет" % name)
            if bad:
                rec["issues"].append("%s: NaN в %d ключах" % (name, bad))
            if maxs > 50:
                rec["issues"].append("%s: масштаб %.1f" % (name, maxs))
            rec["clips"].append(c)
        # ★ подпись дефекта Ian: кость движется в idle (клип 0), а в клипе N стоит намертво (импорт залил её позой привязки)
        idle = next((c for c in rec["clips"] if c["N"] == 0), None)
        for c in rec["clips"]:
            if idle is None or c is idle:
                continue
            frozen = [t for t, mv in idle["moving"].items() if mv and t in c["moving"] and not c["moving"][t]]
            c["frozen_vs_idle"] = frozen
            if frozen:
                rec.setdefault("notes", []).append("%s: неподвижны, хотя движутся в idle: %s" % (c["name"], frozen[:6]))
        for c in rec["clips"]:
            c.pop("moving", None)
        # кость без дорожки в одном клипе, но с дорожкой в другом — признак дефекта сопоставления имён
        for c in rec["clips"]:
            gap = [b for b in c["missing"] if b.lower() in all_tracks]
            c["missing_vs_other_clips"] = gap
            if gap:
                rec["issues"].append("%s: нет дорожек %s (в других клипах есть)" % (c["name"], gap[:6]))
        res.append(rec)
        unreal.log_warning("[npc-аудит] %-30s клипов %d костей %d %s %s" % (model, len(rec["clips"]), rec["bones"], "; ".join(rec["issues"]),
                           ("| " + "; ".join(rec.get("notes", []))) if rec.get("notes") else ""))
    os.makedirs(os.path.dirname(OUT), exist_ok=True)
    with open(OUT, "w", encoding="utf-8") as f:
        json.dump(res, f, ensure_ascii=False, indent=1)
    bad_models = [r["model"] for r in res if r["issues"]]
    unreal.log_warning("[npc-аудит] моделей %d, с замечаниями %d: %s" % (len(res), len(bad_models), ", ".join(bad_models)))
    unreal.log_warning("АУДИТ NPC-КЛИПОВ: ГОТОВО")


if __name__ == "__main__" or globals().get("__spec__") is None:
    try:
        main()
    except Exception:
        import traceback
        import unreal
        unreal.log_error("[npc-аудит] ИСКЛЮЧЕНИЕ:\n%s" % traceback.format_exc())
