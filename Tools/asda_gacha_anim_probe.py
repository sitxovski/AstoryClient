# Copyright (c) 2026 @sitxovski (Discord)
# asda_gacha_anim_probe.py — ФАКТИЧЕСКИЙ замер того, что реально лежит в проекте у автоматов гачи:
#   1) длительность/частота/число кадров КАЖДОГО импортированного клипа (5 моделей × 5 клипов);
#   2) состав слотов материалов КАЖДОЙ скелеталки + мастер и режим смешивания каждого слота;
#   3) карта «секция LOD0 → слот материала» — чтобы знать, какая ПОДСЕТКА чем красится.
#
# Зачем: жалобы владельца «анимация короче, чем в оригинале» и «серый материал под автоматами и
# шаром». Закон [[full-structural-analysis-always]] — сначала выписать КАЖДЫЙ элемент, потом править.
#
# Эталон, с чем сверяем (числа получены из данных клиента, не на глаз):
#   · `.jbb` заголовок: длительность (float @0x30, МИЛЛИСЕКУНДЫ) и fps (float @0x34 = 30.0);
#     у автоматов _00/_03/_04 = 2333.33 мс (71 ключ), _01/_02 = 2000.0 мс (61 ключ);
#   · `animSkeletonAdvanceTimeMtx@0x112AB50`: `time += dt` БЕЗ множителя ⇒ скорость ровно 1.0;
#   · `animSampleTransformAtTime@0x11D7D30`: `frame = (uint)(time * 0.03)` c зажимом на keyCount−1
#     ⇒ время движка в миллисекундах, шаг кадра 1/0.03 = 33.33 мс = 30 fps, выборка БЕЗ интерполяции;
#   · `Game_IsPausedOrMenu@0x99ED60` (мисномер): `slot[13] >= slot[11]` = «клип доиграл».
#
# Только ЧТЕНИЕ. Запуск: run_headless.bat asda_gacha_anim_probe.py
import unreal

SKEL_ROOT = "/Game/AsdaAssets/Models/Skeletal/Gachya"
MODELS = ["md_gachya_%s_00" % c for c in "abcde"]
# Длительности из .jbb (мс) — эталон для сверки: слот 0..4.
JBB_MS = [2333.33, 2000.0, 2000.0, 2333.33, 2333.33]

EAL = unreal.EditorAssetLibrary


def log(m):
    unreal.log("[гача-аним] " + str(m))


def prop(o, n, d="?"):
    try:
        return o.get_editor_property(n)
    except Exception:
        return d


def dump_anims():
    """Длительность каждого клипа против эталонного числа из .jbb."""
    bad = 0
    for name in MODELS:
        for slot in range(5):
            path = "%s/%s/SkeletalMeshes/%sanim_%02d" % (SKEL_ROOT, name, name, slot)
            a = EAL.load_asset(path)
            if not a:
                log("★ НЕТ клипа %s" % path)
                bad += 1
                continue
            # sequence_length у UAnimSequence read-only, берём через библиотеку
            try:
                sec = unreal.AnimationLibrary.get_sequence_length(a)
            except Exception:
                sec = prop(a, "sequence_length", -1.0)
            try:
                frames = unreal.AnimationLibrary.get_num_frames(a)
            except Exception:
                frames = -1
            rate = prop(a, "rate_scale", -1.0)
            fps = prop(a, "target_frame_rate", None)
            want = JBB_MS[slot] / 1000.0
            delta = (sec - want) * 1000.0
            mark = "OK " if abs(delta) <= 1.0 else "★★★"
            if abs(delta) > 1.0:
                bad += 1
            log("%s %-18s слот %d: длит=%.4f с (эталон %.4f с, расх. %+.1f мс) кадров=%s "
                "rate_scale=%s частота=%s" % (mark, name, slot, sec, want, delta, frames, rate, fps))
    log("клипов с расхождением: %d" % bad)


def dump_slots():
    """Слоты материалов и карта секций LOD0 → слот (какая подсетка каким материалом красится)."""
    for name in MODELS:
        mesh = EAL.load_asset("%s/%s/SkeletalMeshes/%s" % (SKEL_ROOT, name, name))
        if not mesh:
            log("★ НЕТ меша %s" % name)
            continue
        mats = prop(mesh, "materials", [])
        log("=== %s: слотов=%d" % (name, len(mats)))
        for i, sm in enumerate(mats):
            mi = sm.get_editor_property("material_interface")
            slot = sm.get_editor_property("material_slot_name")
            if not mi:
                log("  слот %2d '%s' = ★ ПУСТО (движок подставит DefaultMaterial — РОВНО СЕРЫЙ)" % (i, slot))
                continue
            par = mi.get_editor_property("parent") if isinstance(mi, unreal.MaterialInstanceConstant) else None
            base = par if par else mi
            log("  слот %2d '%s' = %s мастер=%s блендмод=%s модель=%s two_sided=%s" % (
                i, slot, mi.get_name(), base.get_name() if base else "—",
                prop(base, "blend_mode"), prop(base, "shading_model"), prop(base, "two_sided")))
        # секции LOD0: сколько их и на какой слот каждая смотрит
        try:
            lod0 = prop(mesh, "lod_info")[0]
            sections = prop(lod0, "sections", [])
            m = []
            for si, s in enumerate(sections):
                m.append("%d→%s" % (si, prop(s, "material_index", "?")))
            log("  секции LOD0 (%d): %s" % (len(sections), ", ".join(m)))
        except Exception as e:
            log("  секции LOD0 прочитать не удалось: %s" % e)


def main():
    dump_anims()
    dump_slots()
    log("=== готово ===")


if __name__ == "__main__" or globals().get("__spec__") is None:
    main()
