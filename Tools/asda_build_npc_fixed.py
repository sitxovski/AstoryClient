# Copyright (c) 2026 @sitxovski (Discord)
# asda_build_npc_fixed.py — БАТЧ: ВСЕ модели из NpcModel.bnz → .glb с ИСПРАВЛЕННЫМ двух-позиционным bind.
# Переиспользует проверенные функции из asda_test_export_one.py (parse_jbm_ext, resolve_names,
# recover_binds, prune_unskinned, build_fixed) — скиннинг совпадает с игрой ТОЧНО (валидировано).
# Запуск (вне UE):  python asda_build_npc_fixed.py
# Выход: RawAssets/Models/Skeletal/NPC/<model>.glb  (перезаписывает старые)
import sys, os, re, zipfile, importlib.util
import os as _os, sys as _sys
_sys.path.insert(0, _os.path.dirname(_os.path.abspath(__file__)))
import asda_paths as _P
from collections import defaultdict

HERE = os.path.dirname(os.path.abspath(__file__))
OUT_DIR = os.path.join(HERE, "..", "RawAssets", "Models", "Skeletal", "NPC")
GAME = _P.DATA
NPC_MODEL = GAME + r"\NPC\Model\NpcModel.bnz"
NPC_MOTION = GAME + r"\NPC\Motion\NpcMotion.baz"

spec = importlib.util.spec_from_file_location("T", os.path.join(HERE, "asda_test_export_one.py"))
T = importlib.util.module_from_spec(spec); sys.modules["T"] = T; spec.loader.exec_module(T)
import asda_skinned_to_gltf as S
import asda_material_policy as POL

# ★★★ ПОЛУПРОЗРАЧНЫЕ ПОДСЕТКИ (2026-08-18, `Reports/jbm_blend_flag_study.md` §5).
#   NpcModel.bnz держит НЕ ТОЛЬКО NPC: там же лежит ДЕКОР КАРТ (`mdel_*`, `natu_*` — 53 модели).
#   Именно у декора стоит рендер-флаг блендинга при технике tNoSkin: 10 моделей, 109 подсеток
#   (карточки листвы `mdel_oldtree` ×44 и `mdel_irontree` ×32, дымка `mdel_buryd` ×6, пар
#   `mdel_csomachine` ×9, зеркала, ядовитый пар `natu_poisonpt`).
#   ★ У СОБСТВЕННО NPC (`npc_*`) таких подсеток НОЛЬ — проверено сплошным сканом архива
#     (jbm_blend_flag_study.md §3.2: tNoSkin с битом у npc_* = 0 из 50). Поэтому суффикс включён
#     для ВСЕГО архива: на именах слотов NPC это не отражается ни одним символом, а декор
#     получает отдельный слот и прозрачный мастер при КАЖДОЙ пересборке.
BLEND_SUFFIX = POL.BLEND_SUFFIX

# ★★★ РАЗВОД СМЕШАННЫХ СЛОТОВ ПО ТЕХНИКЕ (2026-08-18, `Reports/blend_rollout_npc_mobs.md`).
#   Метки `_blend` мало: UE сливает подсетки с одинаковым именем материала в ОДИН слот, а у NPC
#   в одной модели встречаются подсетки РАЗНЫХ техник с ОДНОЙ текстурой — 16 слотов вида
#   tSkin/tRigid + tNoSkin (`npc_f_h_centurion`, `npc_m_h_booster`, `npc_n_m_vehicle` и др.).
#   Им нужны РАЗНЫЕ мастера (непрозрачный vs masked), поэтому подсетки tNoSkin в спорном слоте
#   получают суффикс `_msk`. Имена считает asda_mesh_render_class ИЗ `meshType`/`renderFlags`,
#   поэтому пересборка воспроизводит результат целиком.
import asda_mesh_render_class as MC
import asda_skin_build as SB   # ★ дедуп №2 В1 Т10: цепочка fixed-пути — общий build_fixed_glb


def align_clip_bones_by_index(ref, jbb):
    """★★★ [ОРИГИНАЛ] 04.11.2026: кости клипов 1..N — по ИНДЕКСУ референса, как у оригинала (Ian-b у Ian, `mcrystal01-a_bone`,
    `Bone_g4` солдат шахты). Разбор и адреса — `asda_skin_build.align_clip_bones_by_index` (общая точка с мобами)."""
    return SB.align_clip_bones_by_index(ref, jbb)


def main():
    only = sys.argv[1] if len(sys.argv) > 1 else None   # необязательно: префикс-фильтр (напр. "npc_")
    zm = zipfile.ZipFile(NPC_MODEL); za = zipfile.ZipFile(NPC_MOTION)
    models = [n[:-4] for n in zm.namelist() if n.lower().endswith(".jbm")]
    # индекс клипов по базовому имени модели:  model -> [(N, jbbname)]
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
            skip += 1; continue                      # без клипов (статика/вариант) — пропускаем
        try:
            jbm = T.parse_jbm_ext(zm.read(model + ".jbm"))
            if not jbm: fail += 1; print("  FAIL parse:", model); continue
            clips = []
            for N, mn in clips_meta:
                jbb = S.parse_jbb(za.read(mn))
                if jbb: clips.append(("anim_%02d" % N, jbb))
            if not clips: skip += 1; continue
            # ★ 04.11.2026: кости клипов 1..N — по ИНДЕКСУ референса (как оригинал), см. align_clip_bones_by_index
            for cname, cj in clips[1:]:
                nren = align_clip_bones_by_index(clips[0][1], cj)
                if nren: print("  %-32s %s: имена %d костей взяты у клипа 0 по индексу" % (model, cname, nren))
                nfill = SB.fill_missing_bones_from_parent(clips[0][1], cj)   # ★ 04.11.2026: кость без дорожки — за родителем
                if nfill: print("  %-32s %s: достроены за родителем: %s" % (model, cname, ", ".join(nfill)))
            # ★ дедуп №2 В2 (11.10.2026): БЕЗ bones-регуляризации — ОСОЗНАННО, не расхождение копий. Замер
            #   `_probe_skin_err.measure` (сборка в песочницу, ассеты не тронуты): NPC 133 модели по всем клипам — ошибка
            #   скина та же (макс. разница 0.00001); мобы 99 по референс-клипу — 98 без изменений, mon_b_4_firegolem-a_00
            #   хуже вдвое (274 → 587). Отчёт `Reports/dedup_audit2/wave2_T.md`. Не включать recover_bones=True.
            # ★ дедуп №2 В1 Т10: цепочка resolve_names → recover_binds(БЕЗ bones, как было у NPC) →
            #   fix_degenerate_binds («мосты»-ленты: кости <4 фит-пар) → trs_refit_binds → prune → build_fixed →
            #   validate — общий asda_skin_build.build_fixed_glb; строки журнала шагов печатает _step в прежнем порядке.
            # ★★★ 2026-09-11: bind в форму T·R·S там, где это уменьшает отклонение от игры В UE
            #   (узел glTF не несёт сдвига; UE берёт обратную bind из ref-скелета, а не из IBM).
            #   Лечит «скукожена часть костей» — зеркало бага «огромные головы». Откат:
            #   ASDA_BIND_TRS_REFIT=0. Подробности — в шапке T.trs_refit_binds.
            def _step(name, val, model=model):
                """Журнал шагов fixed-пути (прежние строки сборщика NPC)."""
                if name == "mixed" and val:
                    print("  %-32s разнопозные меши на кости: %s" % (model, val))
                if name == "degenerate" and val:
                    print("  %-32s фикс вырожденных bind-костей: %d вершин" % (model, val))
                elif name == "trs" and val:
                    print("  %-32s bind TRS-перефит костей: %d (макс. было %.1f -> стало %.1f)"
                          % (model, len(val), max(a for _n, a, _b in val), max(b for _n, _a, b in val)))
            out = os.path.join(OUT_DIR, model + ".glb")
            rep = SB.build_fixed_glb(jbm, clips, out, recover_bones=False, fix_degenerate=True, mixed_bind_fix=True,
                                     blend_mat_suffix=BLEND_SUFFIX, on_step=_step,
                                     mat_names_fn=lambda j: MC.material_names(j["meshes"], split=True, enable_blend=True))
            be = rep["bind_err"]
            ok += 1
            if be > 1.0: worst.append((model, be))           # где фит bind не сошёлся — на заметку
            if ok % 20 == 0: print("  ...%d собрано" % ok)
        except Exception as e:
            fail += 1; print("  FAIL %s: %s" % (model, e))
    print("=" * 60)
    print("NPC FIXED build: OK=%d SKIP(без клипов)=%d FAIL=%d → %s" % (ok, skip, fail, OUT_DIR))
    if worst:
        print("⚠️ модели с bind-реконструкцией >1.0 (проверить визуально):")
        for m, e in sorted(worst, key=lambda x: -x[1])[:15]: print("   %-32s err=%.2f" % (m, e))

if __name__ == "__main__":
    main()
