# Copyright (c) 2026 @sitxovski (Discord)
# asda_fx_jem_model_patch.py — МНОГОМЕШЕВЫЕ .jem В УЖЕ СОБРАННЫХ BP ЭФФЕКТОВ (30.10.2026, точечная правка ПО ДАННЫМ).
#   ★ 30.10.2026 (вторая волна): и ОДНОМЕШЕВЫЕ .jem с матрицей узла не-масштабом (`SINGLE_TARGETS`, список .jem —
#   `asda_place_effects.JEM_SINGLE_NODE_FIX`): прежде из матрицы узла брался только элемент [0][0].
#
#   Дефект: общий сборщик `asda_place_effects.build_emitter` брал из .jem только `meshes[0]` и из матрицы его узла —
#   только элемент [0][0]. Эталон рисует ВСЕ узлы модели, каждый в своей матрице (CMesh_RenderNode@0x125C4C0,
#   CEffectMesh_DrawLeaf@0x129B5D0 → MultiplyTransform; разбор — в шапке `asda_jem_parse.model_mesh`). Сборщик исправлен
#   (`asda_place_effects.jem_render_mesh`), но канальные сборщики пересобирают BP ЦЕЛИКОМ (а копание и состояния вообще
#   пропускают готовые), а в BP могут быть правки владельца. Поэтому здесь в существующих BP переписываются ТОЛЬКО поля
#   геометрии меш-эмиттеров с многомешевым .jem: MeshVerts / MeshNormals / MeshUV0 / SkinFrames / SkinBones /
#   MeshVertBone. Материалы, узлы трека, флаги, свечение и прочие эмиттеры не трогаются; эмиттер BP сверяется с диском
#   по имени и типу (несовпадение — BP пропускается целиком).
#
#   Запуск (UE закрыт на этих ассетах; дату файла сверяет обёртка):
#     MSYS_NO_PATHCONV=1 python Tools/run_headless_safe.py Tools/asda_fx_jem_model_patch.py --marker "[jemmodel] ГОТОВО"
#   ASDA_JEMPATCH_ONLY="jeb1;jeb2" — только перечисленные; ASDA_JEMPATCH_DRY=1 — посчитать и ничего не сохранять.
#   Откат: бэкап .uasset (@@ASTORY_DEV_ROOT@@\_backup\2026-10-30_jem_multimesh\) или пересборка канала с ASDA_FX_JEM_FIRST_MESH=1.
# ★ ЗАКОН [[tools-no-import-side-effects]]: при импорте модуль НИЧЕГО не делает.
import os
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))

import asda_fx_common as FXC   # общие точки канальных сборщиков (без побочных действий при импорте)

FX_DIR = "/Game/AsdaAssets/Effects"   # пакет BP эффектов персонажей/навыков/состояний/копания (`BP_Fx_<jeb>`)

# Собранные BP, чьи меш-эмиттеры берут многомешевый .jem (перепись 2261 .jeb EffectModel.bnz × 127 .jem, 30.10.2026).
#   База .jeb → .jem, ради которого правится (для журнала; источник истины — сам .jeb на диске).
TARGETS = {
    "m_ball": "m_ball",                                            # жонглирование (эмоция), 3 мяча
    "eff_wing_f_big_00": "eff_f_wing_big_00",                       # крылья PlusPackage (2 крыла: второе — зеркало)
    "eff_wing_f_ms_00": "eff_f_wing_sm_00",
    "eff_wing_m_ms_00": "eff_m_wing_sm_00",
    "eff_wing_m_big_00": "eff_wing_m_big_00",
    "eff_wing_m_mid_00": "eff_m_wing_middle_00",                   # 3 части
    "eff_lucky_a_01": "eff_lucky_a_01",                             # гача: редкий выигрыш
    "sb_md_eff_am_h_01_02": "mage_stone",                           # хранитель: камни (9)
    "sb_md_eff_am_h_02_02": "mage_stone",
    "sb_md_eff_am_h_03_02": "mage_stone",
    "eff_1_char_02_parlac01-a_00": "eff_1_char_02_parlac02-a_00",   # навык «Испуг»
    "eff_2_char_99_attsdw01-a_00": "eff_1_char_02_parlac02-a_00",   # состояние «Бессилие»
    "eff_2_char_99_attsdw01-a_01": "eff_1_char_02_parlac02-a_00",
    "eff_2_char_99_attsdw01-a_02": "eff_1_char_02_parlac02-a_00",
    "eff_1_char_06_firqukw03-a_00": "eff_1_char_06_firqukb01-a_00", # «Песчаная ловушка»: 6 камней кольцом
    "ch1_a_4th_ac_06_1": "206",                                     # «Устройство авто/самозащиты»
    "eff_0_char_99_c-shovel-a_00": "eff_0_char_99_c-shovel02-a_00", # копание: удар, 6 комьев
}

# ★★★ 30.10.2026: BP с ОДНОМЕШЕВЫМ .jem из `asda_place_effects.JEM_SINGLE_NODE_FIX` (матрица узла меша целиком, а не
#   элемент [0][0]; перепись 2261 .jeb: 98 собранных BP на 39 из 56 .jem). База .jeb → .jem через «;» (для журнала).
SINGLE_TARGETS = {
    "ch1_a_4th_ac_01_2a": "201-01",
    "ch1_a_4th_ac_05_1": "201-01",
    "ch1_a_4th_mg_05_1": "laurel wreath",
    "ch1_a_4th_mg_06_2a": "4skill_stone",
    "eff_0_char_99_c-shovel-a_01": "eff_0_char_99_c-shovel01-a_00",
    "eff_0_char_99_c-shovel-b_01": "eff_0_char_99_c-shovel01-a_00",
    "eff_0_othr_99_pmhpre01-a_01": "eff_0_othr_99_pmhpre01-a_00",
    "eff_0_othr_99_pmhpre01-a_02": "eff_0_othr_99_pmhpre01-a_00",
    "eff_0_othr_99_pmmpre01-a_01": "eff_0_othr_99_pmhpre01-a_00",
    "eff_0_othr_99_pmmpre01-a_02": "eff_0_othr_99_pmhpre01-a_00",
    "eff_1_char_02_parlac01-a_00": "eff_1_char_02_parlac01-a_00",
    "eff_1_char_02_rgeblwe04-a_00": "eff_1_char_02_rgeblwe01-a_00;eff_1_char_02_rgeblwe01-b_00",
    "eff_1_char_03_trapth01-a_00": "eff_1_char_03_trapth01-a_00",
    "eff_1_char_03_trapth01-a_01": "eff_1_char_03_trapth01-a_00",
    "eff_1_char_03_trapth01-a_02": "eff_1_char_03_trapth01-a_00",
    "eff_1_char_06_firqukw03-a_00": "eff_1_char_06_firqukb01-b_00",
    "eff_1_char_06_stmtrpe03-a_00": "eff_1_char_06_stmtrpg01-b_00",
    "eff_1_char_06_stmtrpg01-a_00": "eff_1_char_06_stmtrpg01-b_00",
    "eff_1_char_06_stmtrpg02-a_00": "eff_1_char_06_stmtrpg01-b_00",
    "eff_1_char_99_grened01-a_00": "eff_1_char_99_grened01-a_00",
    "eff_1_char_99_tforeh01-a_00": "eff_1_char_99_tforeh01-a_00",
    "eff_2_char_99_attsdw01-a_00": "eff_1_char_02_parlac01-a_00",
    "eff_2_char_99_attsdw01-a_01": "eff_1_char_02_parlac01-a_00",
    "eff_2_char_99_attsdw01-a_02": "eff_1_char_02_parlac01-a_00",
    "eff_2_char_99_defdw401-a_00": "eff_1_char_02_amorde02-a_00;eff_1_char_02_amorde03-a_00",
    "eff_2_char_99_defdw401-a_01": "eff_1_char_02_amorde02-a_00;eff_1_char_02_amorde03-a_00",
    "eff_2_char_99_defdw401-a_02": "eff_1_char_02_amorde02-a_00;eff_1_char_02_amorde03-a_00",
    "eff_2_char_99_defdwe01-a_00": "eff_1_char_02_amorde01-a_00",
    "eff_2_char_99_defmine05-a_00": "eff_2_char_99_defmine05-a_00",
    "eff_2_char_99_freez301-a_00": "eff_1_char_03_trapth01-a_00",
    "eff_2_char_99_freez301-a_01": "eff_1_char_03_trapth01-a_00",
    "eff_2_char_99_freez301-a_02": "eff_1_char_03_trapth01-a_00",
    "eff_2_char_99_sleep01-a_00": "eff_2_char_99_sleep01-a_00;eff_2_char_99_sleep01-b_00",
    "eff_2_char_99_sleep01-a_01": "eff_2_char_99_sleep01-a_00;eff_2_char_99_sleep01-b_00",
    "eff_2_char_99_sleep01-a_02": "eff_2_char_99_sleep01-a_00;eff_2_char_99_sleep01-b_00",
    "eff_2_char_99_timbom01-a_00": "eff_2_char_99_bomsta01-a_00;eff_2_char_99_bomsta01-a_01",
    "eff_2_char_99_timbom01-a_01": "eff_2_char_99_bomsta01-a_00;eff_2_char_99_bomsta01-a_01",
    "eff_2_char_99_timbom01-a_02": "eff_2_char_99_bomsta01-a_00;eff_2_char_99_bomsta01-a_01",
    "eff_lucky_a_00": "eff_lucky_a_00",
    "f_flower01": "flower",
    "fish_end_01": "fish_eff",
    "fish_end_02": "nonfish_eff",
    "m_flower01": "flower",
    "ragepig_attack_cri_01": "ragepig_stone_ani_01",
    "sb_md_eff_am_h_01_02": "stone_ani_01",
    "sb_md_eff_am_h_02_02": "stone_ani_01",
    "sb_md_eff_am_h_03_02": "stone_ani_01",
    "sb_md_eff_oh_h_01_02": "stone_ani_01",
    "sb_md_eff_oh_h_02_02": "stone_ani_01",
    "sb_md_eff_oh_h_03_02": "stone_ani_01",
    "shd_a_co_ma_01": "shd_a_mesh",
    "shd_a_he_ma_01": "shd_a_mesh",
    "shd_a_ra_ma_01": "shd_a_mesh",
    "shd_a_uc_ma_01": "shd_a_mesh",
    "shd_b_co_ma_01": "shd_b_mesh",
    "shd_b_he_ma_01": "shd_b_mesh",
    "shd_b_ra_ma_01": "shd_b_mesh",
    "shd_b_uc_ma_01": "shd_b_mesh",
    "shd_c_co_ma_01": "shd_c_mesh",
    "shd_c_he_ma_01": "shd_c_mesh",
    "shd_c_ra_ma_01": "shd_c_mesh",
    "shd_c_uc_ma_01": "shd_c_mesh",
    "shd_d_co_ma_01": "shd_d_mesh",
    "shd_d_he_ma_01": "shd_d_mesh",
    "shd_d_ra_ma_01": "shd_d_mesh",
    "shd_d_uc_ma_01": "shd_d_mesh",
    "shd_e_co_ma_01": "shd_e_mesh",
    "shd_e_he_ma_01": "shd_e_mesh",
    "shd_e_ra_ma_01": "shd_e_mesh",
    "shd_e_uc_ma_01": "shd_e_mesh",
    "shd_f_co_ma_01": "shd_f_mesh",
    "shd_f_he_ma_01": "shd_f_mesh",
    "shd_f_ra_ma_01": "shd_f_mesh",
    "shd_f_uc_ma_01": "shd_f_mesh",
    "shd_g_co_ma_01": "shd_g_mesh",
    "shd_g_he_ma_01": "shd_g_mesh",
    "shd_g_ra_ma_01": "shd_g_mesh",
    "shd_g_uc_ma_01": "shd_g_mesh",
    "shd_h_co_ma_01": "shd_h_mesh",
    "shd_h_he_ma_01": "shd_h_mesh",
    "shd_h_ra_ma_01": "shd_h_mesh",
    "shd_h_uc_ma_01": "shd_h_mesh",
    "shd_i_co_ma_01": "shd_i_mesh",
    "shd_i_he_ma_01": "shd_i_mesh",
    "shd_i_ra_ma_01": "shd_i_mesh",
    "shd_i_uc_ma_01": "shd_i_mesh",
    "shd_j_co_ma_01": "shd_j_mesh",
    "shd_j_he_ma_01": "shd_j_mesh",
    "shd_j_ra_ma_01": "shd_j_mesh",
    "shd_j_uc_ma_01": "shd_j_mesh",
    "shd_k_co_ma_01": "shd_k_mesh",
    "shd_k_he_ma_01": "shd_k_mesh",
    "shd_k_ra_ma_01": "shd_k_mesh",
    "shd_k_uc_ma_01": "shd_k_mesh",
    "shd_l_co_ma_01": "shd_l_mesh",
    "shd_l_he_ma_01": "shd_l_mesh",
    "shd_l_ra_ma_01": "shd_l_mesh",
    "shd_l_uc_ma_01": "shd_l_mesh",
}

GEOM_FIELDS = ("mesh_verts", "mesh_normals", "mesh_uv0", "skin_frames", "skin_bones", "mesh_vert_bone")


def log(m):
    import unreal
    unreal.log_warning("[jemmodel] %s" % m)


def patch_one(jeb, dry):
    """Переписать геометрию многомешевых меш-эмиттеров одного BP. (сохранён?, число правленых эмиттеров)."""
    import unreal
    import asda_jeb_parse as J
    import asda_place_effects as PE
    EAL = unreal.EditorAssetLibrary
    JEM = PE._load_jem_module()
    bpp = "%s/%s" % (FX_DIR, FXC.bp_name(jeb))
    jb = J.parse_effect(jeb)
    if not jb or not EAL.does_asset_exist(bpp):
        log("%s: нет .jeb или BP %s — пропуск" % (jeb, bpp))
        return False, 0
    bp = EAL.load_asset(bpp)
    cdo = unreal.get_default_object(bp.generated_class())
    ems = list(cdo.get_editor_property("emitters"))
    disk = jb["emitters"]
    # BP строятся из эмиттеров диска с пропуском неподдержанных — сопоставляем по имени и типу по порядку.
    di = 0
    changed = []
    for e in ems:
        nm, tp = e.get_editor_property("name"), int(e.get_editor_property("jeb_type"))
        while di < len(disk) and not (disk[di]["name"] == nm and disk[di]["type"] == tp):
            di += 1
        if di >= len(disk):
            log("%s: эмиттер BP '%s'/0x%X не найден на диске — BP пропущен" % (jeb, nm, tp))
            return False, 0
        d = disk[di]; di += 1
        if d["type"] not in FXC.MESH_TYPES or not d.get("mesh") or not e.get_editor_property("mesh_emitter"):
            continue
        meshes = JEM.load_jem(d.get("jem") or d["name"]) or []
        if not meshes or not PE.jem_uses_model(meshes, d):   # многомешевый или одномешевый из JEM_SINGLE_NODE_FIX
            continue
        before = len(e.get_editor_property("mesh_verts"))
        m = PE.jem_render_mesh(meshes, d)
        # скин-поля сначала очищаем: у модели без анимации их не должно остаться от прежнего meshes[0]
        e.set_editor_property("skin_frames", [])
        e.set_editor_property("skin_bones", 0)
        e.set_editor_property("mesh_vert_bone", [])
        frames, nf = PE.apply_jem_geometry(e, m, d.get("jem") or d["name"])
        changed.append("%s/0x%X %s: мешей %d, вершин %d → %d, костей %d, кадров %d"
                       % (nm, tp, d.get("jem"), len(meshes), before, len(m["verts"]),
                          len(m["palette"]) if frames else 0, nf))
    if not changed:
        log("%s: меш-эмиттеров с моделью .jem нет — без правок" % jeb)
        return False, 0
    for c in changed:
        log("%s: %s" % (jeb, c))
    if dry:
        log("%s: СУХОЙ ПРОГОН — не сохранён" % jeb)
        return False, len(changed)
    with FXC.emitters_writer_gate():
        cdo.set_editor_property("emitters", ems)
    saved = bool(EAL.save_loaded_asset(bp))
    log("%s: сохранён=%s" % (jeb, saved))
    if saved:
        print("ASDA_WRITES_ASSET: %s" % bpp)
    return saved, len(changed)


def main():
    dry = FXC.env_flag("ASDA_JEMPATCH_DRY")
    jebs = FXC.only_filter(sorted(set(TARGETS) | set(SINGLE_TARGETS)), "ASDA_JEMPATCH_ONLY")
    ok = bad = 0
    for jeb in jebs:
        try:
            saved, n = patch_one(jeb, dry)
        except Exception as ex:
            log("%s: ОШИБКА %s" % (jeb, ex))
            saved, n = False, 0
        if saved or (dry and n):
            ok += 1
        else:
            bad += 1
    log("ГОТОВО: %s %d, без записи %d из %d" % ("посчитано" if dry else "сохранено", ok, bad, len(jebs)))


if FXC.is_direct_run(globals()):
    main()
