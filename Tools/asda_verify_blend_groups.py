# Copyright (c) 2026 @sitxovski (Discord)
# asda_verify_blend_groups.py — ПРОВЕРКА ЧИСЛАМИ раскатки прозрачности (декор карт / сцена
# создания-выбора персонажа / модели предметов).  Ничего не меняет — только считает и сверяет.
#
# Что сверяется, по каждой модели:
#   • сколько СЛОТОВ у меша в UE и на каком мастере сидит каждый;
#   • сколько подсеток `.jbm` прошли критерий `asda_jbm.is_alpha_blend`;
#   • совпадает ли множество ПОМЕЧЕННЫХ слотов с множеством текстур помеченных подсеток.
# Слот UE = уникальное имя материала (Interchange дедуплицирует одноимённые), поэтому 44 карточки
# листвы `mdel_oldtree` дают ОДИН слот `natu_bigtree03-a_00_blend` — это правильно и ожидаемо.
#
# Запуск: UnrealEditor-Cmd.exe <proj> -ExecutePythonScript=<этот файл> -unattended -nopause
#         -nosplash -nosound -NullRHI -abslog=...
import os, zipfile, glob
import unreal
import sys as _sys
_sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import asda_paths as _P
import asda_ue_paths as _U
import asda_material_policy as POL
import asda_jbm as JBM

EAL = unreal.EditorAssetLibrary


def log(m):
    unreal.log("[blend-проверка] " + str(m))


def jbm_expect(recs):
    """Ожидание из данных: (сколько подсеток помечено, множество имён материалов с меткой).

    ★★★ 2026-08-18. Ожидание считается ПО ДЕЙСТВУЮЩЕЙ ПОЛИТИКЕ, а не «по данным всегда»:
      при выключенной раскатке (`POL.BLEND_ROLLOUT_ENABLED=False`) прозрачных слотов вне гачи
      быть НЕ ДОЛЖНО ни одного, поэтому ожидаемое множество ПУСТО. Счётчик помеченных подсеток
      (`n`) остаётся честным числом из данных — он показывает, сколько слотов вернётся, когда
      раскатку включат обратно одним флагом.
    """
    n = 0; names = set()
    for r in recs:
        if JBM.is_alpha_blend(r):
            n += 1
            names.add(os.path.splitext(r["tex"])[0].lower() + POL.BLEND_SUFFIX)
    return n, (names if POL.blend_rollout_enabled() else set())


def ue_slots(pkg):
    """[(имя слота, имя мастера)] меша UE или None, если ассета нет."""
    m = EAL.load_asset(pkg)
    if not m:
        return None
    prop = "static_materials" if isinstance(m, unreal.StaticMesh) else "materials"
    out = []
    for s in m.get_editor_property(prop):
        mi = s.get_editor_property("material_interface")
        par = mi.get_editor_property("parent") if isinstance(mi, unreal.MaterialInstanceConstant) else None
        out.append((str(s.get_editor_property("material_slot_name")), par.get_name() if par else "—"))
    return out


def check(title, items, pkg_fmt):
    """items = [(имя модели, recs из .jbm)]. pkg_fmt — шаблон пути меша в UE."""
    log("=" * 90)
    log("%s" % title)
    log("%-28s %5s %6s %8s %8s %s" % ("модель", "слот", "blend", "подсеток", "совпало", ""))
    t_slots = t_blend = t_sub = 0; bad = 0
    for name, recs in items:
        want_n, want_names = jbm_expect(recs)
        slots = ue_slots(pkg_fmt % (name, name))
        if slots is None:
            log("%-28s АССЕТА НЕТ В ПРОЕКТЕ" % name); bad += 1; continue
        got_names = set(s.lower() for s, p in slots if p == POL.MASTER_BLEND.rsplit("/", 1)[-1])
        # Нормализация имени перед сравнением:
        #   • у слота может быть числовой хвост Interchange (`..._blend1`, `..._blend43`);
        #   • ★ в `matTable` встречаются имена текстур С ПРОБЕЛОМ (`itm1_n_00_scro003_00 .png`),
        #     а UE в имени ассета заменяет пробел на `_` — иначе ложное расхождение.
        norm = lambda x: (POL.strip_blend(x).rstrip("0123456789").lower().replace(" ", "_")
                          + POL.BLEND_SUFFIX)
        ok = set(map(norm, got_names)) == set(map(norm, want_names))
        t_slots += len(slots); t_blend += len(got_names); t_sub += want_n
        if not ok:
            bad += 1
        log("%-28s %5d %6d %8d %8s  %s" % (name, len(slots), len(got_names), want_n,
                                           "ДА" if ok else "★НЕТ", sorted(got_names)[:3]))
        if not ok:
            log("      ждали: %s" % sorted(want_names))
    log("ИТОГО %s: слотов=%d, прозрачных слотов=%d, помеченных подсеток в данных=%d, расхождений=%d"
        % (title, t_slots, t_blend, t_sub, bad))
    return t_slots, t_blend, t_sub, bad


def main():
    master = EAL.load_asset(POL.MASTER_BLEND)
    if not master:
        log("★ НЕТ мастера %s" % POL.MASTER_BLEND); return
    log("мастер: %s | dither=%s clip=%.4f" % (POL.MASTER_BLEND,
        master.get_editor_property("dither_opacity_mask"),
        master.get_editor_property("opacity_mask_clip_value")))
    log("ПОЛИТИКА: BLEND_ROLLOUT_ENABLED=%s ⇒ класс BLEND вне гачи уходит на %s"
        % (POL.blend_rollout_enabled(), POL.blend_master()))
    if not POL.blend_rollout_enabled():
        log("     ⇒ ожидаемое число прозрачных слотов ВНЕ ГАЧИ = 0 во всех группах и в заднике")

    # ---- декор карт (NpcModel.bnz) ----
    z = zipfile.ZipFile(_P.NPC_MODEL_BNZ)
    decor = []
    for n in sorted(z.namelist()):
        if not n.lower().endswith(".jbm") or not n.lower().startswith(("mdel_", "natu_")):
            continue
        h = JBM.parse_header(z.read(n))
        if h and any(JBM.is_alpha_blend(r) for r in h["recs"]):
            decor.append((n[:-4], h["recs"]))
    a1 = check("ДЕКОР КАРТ", decor, _U.G_MODELS_NPC + "/%s/SkeletalMeshes/%s")

    # ---- предметы (loose .jbm, сборка ORIG — её читает asda_build_models) ----
    items = []
    seen = {}
    for f in glob.glob(os.path.join(_P.DATA_ORIG, "Item", "Model", "*.jbm")):
        seen[os.path.basename(f).lower()] = f
    for f in sorted(seen.values()):
        h = JBM.parse_header(open(f, "rb").read())
        if h and any(JBM.is_alpha_blend(r) for r in h["recs"]):
            items.append((os.path.splitext(os.path.basename(f))[0], h["recs"]))
    a2 = check("ПРЕДМЕТЫ", items, _U.G_MODELS_ITEMS + "/%s/StaticMeshes/%s")

    # ---- сцена создания/выбора персонажа (скелетные) ----
    zc = zipfile.ZipFile(_P.CHAR_MODEL_BNZ)
    scene = []
    for n in ("cre_creation01-a_00.jbm", "cre_creback-a_00.jbm"):
        h = JBM.parse_header(zc.read(n))
        if h:
            scene.append((n[:-4], h["recs"]))
    a3 = check("СЦЕНА (скелетные)", scene, _U.G_MODELS_CHAR + "/%s/SkeletalMeshes/%s")

    # ---- задник логина: 697 статик-акторов уровня ----
    import json
    bd = os.path.abspath(os.path.join(os.path.dirname(os.path.abspath(__file__)), "..",
                                      "RawAssets", "backdrop", "backdrop_elements.json"))
    les = unreal.get_editor_subsystem(unreal.LevelEditorSubsystem)
    eas = unreal.get_editor_subsystem(unreal.EditorActorSubsystem)
    les.load_level("/Game/Map/Login")
    with open(bd, "r", encoding="utf-8") as f:
        elements = json.load(f)["elements"]
    # ★ Ожидание — по действующей политике (см. jbm_expect): при выключенной раскатке ни один из
    #   697 акторов задника не должен сидеть на прозрачном мастере.
    _roll = POL.blend_rollout_enabled()
    want = {"BackdropElem_%s" % e["name"]: (bool(e.get("blend")) and _roll) for e in elements}
    n = nb = mism = 0
    for a in eas.get_all_level_actors():
        lbl = a.get_actor_label()
        if lbl not in want:
            continue
        smc = a.get_component_by_class(unreal.StaticMeshComponent)
        mi = smc.get_material(0) if smc else None
        par = mi.get_editor_property("parent") if isinstance(mi, unreal.MaterialInstanceConstant) else None
        isb = bool(par) and par.get_name() == POL.MASTER_BLEND.rsplit("/", 1)[-1]
        n += 1
        if isb: nb += 1
        if isb != want[lbl]: mism += 1
    log("=" * 90)
    log("ЗАДНИК ЛОГИНА: акторов найдено=%d из %d, на прозрачном мастере=%d (ждали %d), расхождений=%d"
        % (n, len(want), nb, sum(1 for v in want.values() if v), mism))

    log("=" * 90)
    log("СВОДКА: декор слотов=%d/прозрачных=%d/подсеток=%d/расх=%d | предметы %d/%d/%d/%d | "
        "сцена %d/%d/%d/%d | задник %d/%d/расх=%d"
        % (a1 + a2 + a3 + (n, nb, mism)))


if __name__ == "__main__" or globals().get("__spec__") is None:
    main()
