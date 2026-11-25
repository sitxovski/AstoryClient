# Copyright (c) 2026 @sitxovski (Discord)
# asda_build_mob_fixed.py — БАТЧ: ВСЕ модели MobModel.bnz → скелетал .glb (конвейер NPC 1:1:
#   parse_jbm_ext → resolve_names → recover_binds → fix_degenerate_binds(«мосты») → prune → build_fixed).
# Клипы MobMotion.baz <model>_N.jbb → anim_NN (N до 15+: idle=0, действия по action-state; грузим ВСЕ).
# Запуск (вне UE):  python asda_build_mob_fixed.py [префикс-фильтр]
# Выход: RawAssets/Models/Skeletal/Mob/<model>.glb
import sys, os, re, zipfile, importlib.util
import os as _os, sys as _sys
_sys.path.insert(0, _os.path.dirname(_os.path.abspath(__file__)))
import asda_paths as _P
from collections import defaultdict

HERE = os.path.dirname(os.path.abspath(__file__))
OUT_DIR = os.path.join(HERE, "..", "RawAssets", "Models", "Skeletal", "Mob")
GAME = _P.DATA
MOB_MODEL  = GAME + r"\Mob\Model\MobModel.bnz"
MOB_MOTION = GAME + r"\Mob\Motion\MobMotion.baz"

spec = importlib.util.spec_from_file_location("T", os.path.join(HERE, "asda_test_export_one.py"))
T = importlib.util.module_from_spec(spec); sys.modules["T"] = T; spec.loader.exec_module(T)
import asda_skinned_to_gltf as S
import asda_material_policy as POL
import asda_mesh_render_class as MC
import asda_skin_build as SB   # ★ дедуп №2 В1 Т10: цепочка fixed-пути — общий build_fixed_glb

# ★★★ ИМЕНА МАТЕРИАЛОВ ПО ТЕХНИКЕ (2026-08-18, отчёт `Reports/blend_rollout_npc_mobs.md`).
#   UE сливает подсетки с ОДИНАКОВЫМ именем материала в ОДИН слот меша, а экспортёр называет
#   материал по имени текстуры подсетки. У мобов это ломает выбор мастера дважды:
#     • 26 слотов делят одну текстуру между tSkin/tRigid (альфа = маска блика ⇒ НЕПРОЗРАЧНЫЙ
#       мастер) и tNoSkin (альфа = настоящая вырезка ⇒ masked) — таким подсеткам tNoSkin
#       дописывается `_msk`;
#     • у 6 моделей (`greengnom02`, `kcat03`, `knightho02`, `madgear02`, `rcrob01`, `ruby02`,
#       17 подсеток) техника tNoSkin И взведён рендер-бит `mr+0x004 & 0x1|0x2` ⇒ прозрачность —
#       им дописывается `_blend`, и импортёр вешает `M_AsdaObjectBlendLit`.
#   Имена считает `asda_mesh_render_class.material_names()` прямо из `meshType`/`renderFlags`,
#   поэтому пересборка+переимпорт воспроизводят результат, а не «делается один раз руками».
BLEND_SUFFIX = POL.BLEND_SUFFIX


def main():
    only = sys.argv[1] if len(sys.argv) > 1 else None
    zm = zipfile.ZipFile(MOB_MODEL); za = zipfile.ZipFile(MOB_MOTION)
    models = [n[:-4] for n in zm.namelist() if n.lower().endswith(".jbm")]
    clipmap = defaultdict(list)
    for mn in za.namelist():
        if not mn.lower().endswith(".jbb"): continue
        m = re.match(r"(.+)_(\d+)$", mn[:-4])
        if m: clipmap[m.group(1)].append((int(m.group(2)), mn))
    os.makedirs(OUT_DIR, exist_ok=True)
    ok = skip = fail = 0; worst = []
    for model in sorted(models):
        if only and not model.startswith(only): continue
        clips_meta = sorted(clipmap.get(model, []))
        if not clips_meta:
            skip += 1; print("  SKIP (без клипов):", model); continue
        try:
            jbm = T.parse_jbm_ext(zm.read(model + ".jbm"))
            if not jbm: fail += 1; print("  FAIL parse:", model); continue
            clips = []
            for N, mn in clips_meta:
                jbb = S.parse_jbb(za.read(mn))
                if jbb: clips.append(("anim_%02d" % N, jbb))
            if not clips: skip += 1; continue
            # ★ 04.11.2026 (npc_talk_anim/мобы): кости клипов — по ИНДЕКСУ клипа 0 (оригинал), отсутствующие в клипе другой оснастки —
            #   за родителем. Разбор — `asda_skin_build.align_clip_bones_by_index` / `fill_missing_bones_from_parent`.
            for cname, cj in clips[1:]:
                nren = SB.align_clip_bones_by_index(clips[0][1], cj)
                if nren: print("  %-40s %s: имена %d костей взяты у клипа 0 по индексу" % (model, cname, nren))
                nfill = SB.fill_missing_bones_from_parent(clips[0][1], cj)
                if nfill: print("  %-40s %s: достроены за родителем: %s" % (model, cname, ", ".join(nfill)))
            # ★ дедуп №2 В2 (11.10.2026): БЕЗ bones-регуляризации — ОСОЗНАННО, не расхождение копий. Замер
            #   `_probe_skin_err.measure` (сборка в песочницу, ассеты не тронуты): NPC 133 модели по всем клипам — ошибка
            #   скина та же (макс. разница 0.00001); мобы 99 по референс-клипу — 98 без изменений, mon_b_4_firegolem-a_00
            #   хуже вдвое (274 → 587). Отчёт `Reports/dedup_audit2/wave2_T.md`. Не включать recover_bones=True.
            # ★ дедуп №2 В1 Т10: цепочка resolve_names → recover_binds(БЕЗ bones, как было у мобов) →
            #   fix_degenerate_binds («мосты»: кости <4 фит-пар) → trs_refit_binds → prune → build_fixed → validate —
            #   общий asda_skin_build.build_fixed_glb; строки журнала шагов печатает _step в прежнем порядке.
            # ★★★ 2026-09-11: bind в форму T·R·S там, где это уменьшает отклонение от игры В UE
            #   (узел glTF не несёт сдвига; UE берёт обратную bind из ref-скелета, а не из IBM).
            #   Лечит «скукожена часть костей» — зеркало бага «огромные головы». Откат:
            #   ASDA_BIND_TRS_REFIT=0. Подробности — в шапке T.trs_refit_binds.
            def _step(name, val, model=model):
                """Журнал шагов fixed-пути (прежние строки сборщика мобов)."""
                if name == "mixed" and val:
                    print("  %-32s разнопозные меши на кости: %s" % (model, val))
                if name == "degenerate" and val:
                    print("  %-32s фикс вырожденных bind: %d вершин" % (model, val))
                elif name == "trs" and val:
                    print("  %-32s bind TRS-перефит костей: %d (макс. было %.1f -> стало %.1f)"
                          % (model, len(val), max(a for _n, a, _b in val), max(b for _n, _a, b in val)))
            out = os.path.join(OUT_DIR, model + ".glb")
            rep = SB.build_fixed_glb(jbm, clips, out, recover_bones=False, fix_degenerate=True, mixed_bind_fix=True,
                                     blend_mat_suffix=BLEND_SUFFIX, on_step=_step,
                                     mat_names_fn=lambda j: MC.material_names(j["meshes"], split=True, enable_blend=True))
            be = rep["bind_err"]
            ok += 1
            if be > 1.0: worst.append((model, be))
            if ok % 20 == 0: print("  ...%d собрано" % ok)
        except Exception as e:
            fail += 1; print("  FAIL %s: %s" % (model, e))
    print("=" * 60)
    print("MOB FIXED build: OK=%d SKIP=%d FAIL=%d → %s" % (ok, skip, fail, OUT_DIR))
    if worst:
        print("⚠️ bind-фит >1.0 (проверить визуально):")
        for m, e in sorted(worst, key=lambda x: -x[1])[:15]: print("   %-32s err=%.2f" % (m, e))

# ★★★ 30.10.2026 КЛИПЫ БАЗЫ +0x24 (`Reports/mob_skills/anim_slots.md` §побочная 2). Оригинал берёт МЕШ моба по полю
#   `Mob list.BIN +0x04` (`sub_F83080`), а КЛИПЫ — по полю +0x24 (`Effect_BuildJbbSuffixByType@0xF717C0`: `imul 0x110; lea esi,
#   [edx+eax+0x24]`). У 30 пар меш ≠ база: 28 мешей своих клипов не имеют вовсе (pawn02←pawn01, bishp02←bishp01,
#   sandpick01←epick02, lavagol01←lava01 …) — порт ставил их статикой без анимации; у kcat03 и minewar obj_1 свои клипы есть,
#   но записи 764/776/788 и 847–849 оригинал анимирует клипами базы. Проход собирает ВАРИАНТ `<меш>__<база>.glb`: меш +0x04 и
#   клипы базы +0x24 (имена anim_NN = номер файла `_N.jbb`, как у всех мобов). Скин-кости привязываются по ИМЕНИ (тот же
#   fixed-путь). Рантайм берёт вариант по таблице `AsdaMobClipBase.inl` (ключ `asda.Mob.ClipBaseModel`). Прежние .glb не трогаются.
MOB_LIST = GAME + r"\Mob\Mob list.BIN"


def clip_base_pairs():
    """{(меш, база)} из Mob list.BIN, где база клипов +0x24 ≠ меш +0x04 (имена без расширения)."""
    import struct
    b = open(MOB_LIST, "rb").read()
    n = struct.unpack_from("<I", b, 4)[0]
    def cs(o, k):
        s = b[o:o + k].split(b"\0")[0].decode("cp1251", "ignore")
        return s[:-4] if s.lower().endswith((".jbm", ".jbb")) else s
    out = set()
    for i in range(n):
        o = 0x14 + i * 0x110
        mesh, base = cs(o + 0x04, 0x20), cs(o + 0x24, 0x20)
        if mesh and base and mesh.lower() != base.lower():
            out.add((mesh, base))
    return sorted(out)


def variant_name(mesh, base):
    """Имя варианта ассета: `<меш>__<база>` (нижний регистр — как у ключей клипмапа)."""
    return "%s__%s" % (mesh.lower(), base.lower())


def main_clip_base(only=None):
    zm = zipfile.ZipFile(MOB_MODEL); za = zipfile.ZipFile(MOB_MOTION)
    jbms = {n[:-4].lower(): n for n in zm.namelist() if n.lower().endswith(".jbm")}
    clipmap = defaultdict(list)
    for mn in za.namelist():
        if not mn.lower().endswith(".jbb"): continue
        m = re.match(r"(.+)_(\d+)$", mn[:-4])
        if m: clipmap[m.group(1).lower()].append((int(m.group(2)), mn))
    os.makedirs(OUT_DIR, exist_ok=True)
    ok = fail = 0
    for mesh, base in clip_base_pairs():
        vn = variant_name(mesh, base)
        if only and not vn.startswith(only): continue
        jm = jbms.get(mesh.lower()); clips_meta = sorted(clipmap.get(base.lower(), []))
        if not jm or not clips_meta:
            fail += 1; print("  НЕТ ДАННЫХ %s: меш %s, клипов базы %d" % (vn, "есть" if jm else "НЕТ", len(clips_meta))); continue
        try:
            jbm = T.parse_jbm_ext(zm.read(jm))
            clips = []
            for N, mn in clips_meta:
                jbb = S.parse_jbb(za.read(mn))
                if jbb: clips.append(("anim_%02d" % N, jbb))
            # ★ 04.11.2026 (npc_talk_anim/мобы): кости клипов — по ИНДЕКСУ клипа 0 (оригинал), отсутствующие в клипе другой оснастки —
            #   за родителем. Разбор — `asda_skin_build.align_clip_bones_by_index` / `fill_missing_bones_from_parent`.
            for cname, cj in clips[1:]:
                nren = SB.align_clip_bones_by_index(clips[0][1], cj)
                if nren: print("  %-40s %s: имена %d костей взяты у клипа 0 по индексу" % (vn, cname, nren))
                nfill = SB.fill_missing_bones_from_parent(clips[0][1], cj)
                if nfill: print("  %-40s %s: достроены за родителем: %s" % (vn, cname, ", ".join(nfill)))
            out = os.path.join(OUT_DIR, vn + ".glb")
            rep = SB.build_fixed_glb(jbm, clips, out, recover_bones=False, fix_degenerate=True, mixed_bind_fix=True,
                                     blend_mat_suffix=BLEND_SUFFIX,
                                     mat_names_fn=lambda j: MC.material_names(j["meshes"], split=True, enable_blend=True))
            ok += 1
            print("  %-56s клипов %2d bind_err=%.4f" % (vn, len(clips), rep["bind_err"]))
        except Exception as e:
            fail += 1; print("  FAIL %s: %s" % (vn, e))
    print("MOB CLIP-BASE variants: OK=%d FAIL=%d → %s" % (ok, fail, OUT_DIR))


if __name__ == "__main__":
    if len(sys.argv) > 1 and sys.argv[1] == "--clipbase":
        main_clip_base(sys.argv[2] if len(sys.argv) > 2 else None)
    else:
        main()
