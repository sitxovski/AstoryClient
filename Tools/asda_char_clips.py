# -*- coding: utf-8 -*-
# Copyright (c) 2026 @sitxovski (Discord)
# asda_char_clips.py — ОБЩИЕ шаги выгрузки и импорта клипов персонажа (дедуп №2, В1 Т10/Т11, 11.10.2026).
#
# Было: каждый экспортёр мини-glb персонажа (export_char_cremmy / _emotions / _weaponsets / _sit / _skillclips /
#   _cremmy_move35 / _cremmy_showcase / _possess, export_equip_parts, fishing_export_assets.export_char_gender) нёс
#   свою копию «склейки частей»: parent_of + добавление hair-strand костей в референс-скелет + resolve_names +
#   слияние мешей, затем recover_binds + trs_refit_binds и build_fixed(bake_pose=False) + validate. Импортёры
#   клипов (import_char_emotions / _weaponsets, fishing_import_char_clips → digging) — дословную копию import_gender,
#   ещё 4 — копию блока Interchange-импорта «только анимации на существующий скелет».
# Стало: шаги здесь; печать в журнал осталась У ВЫЗЫВАЮЩИХ (у каждого свои строки — вывод прежний).
#   Модуль скин-пути (`T`, asda_test_export_one) передаёт вызывающий — тот самый объект, что у него.
#
# ★ ЗАКОН [[tools-no-import-side-effects]]: при импорте модуль ничего не делает; `unreal` — только внутри функций.
# ★ ЗАКОН [[skin-pipeline-fixed-path]]: шаги — fixed-путь (recover_binds / trs_refit_binds / build_fixed), без conv.build.
import os
import re

# Кость части вида «<родитель>_aN» / «<родитель>bN» → имя родителя (hair-strand кости причёсок: 4_a1 → 4).
_PARENT_RE = re.compile(r'^(.*?)[_]?[a-c]\d+$')


def parent_of(n):
    """Имя родителя кости части по правилу «<родитель>[_](a|b|c)N»; не подходит — None."""
    m = _PARENT_RE.match(n.strip())
    return m.group(1) if (m and m.group(1)) else None


def add_part_bones(ref_jbb, jbm):
    """Добавить в референс-скелет `ref_jbb` кости части `jbm`, которых в нём нет (hair-strand), листьями родителя:
    копия записи кости-родителя (её rest-трансформ кадра 0) с новым именем; родителя нет — копия кости 0.
    Обновляет ref_jbb['B'], если что-то добавлено. Возвращает число добавленных костей."""
    ref_bnames = {x['name'].strip().lower() for x in ref_jbb['bones']}
    added = 0
    for mm in jbm['meshes']:
        for n in (mm.get('names') or []):
            if n.strip().lower() in ref_bnames:
                continue
            par = parent_of(n)
            pl = par.lower() if par else None
            pidx = next((i for i, bb in enumerate(ref_jbb['bones'])
                         if bb['name'].strip().lower() == pl), None)
            newb = dict(ref_jbb['bones'][pidx]) if pidx is not None else dict(ref_jbb['bones'][0])
            newb['name'] = n
            ref_jbb['bones'].append(newb)
            ref_bnames.add(n.strip().lower())
            added += 1
    if added:
        ref_jbb['B'] = len(ref_jbb['bones'])
    return added


def merge_parts(T, ref_jbb, parts, read_jbm, on_added=None, on_part=None):
    """Склеить части персонажа в один набор мешей: для каждой части parse_jbm_ext → add_part_bones → resolve_names.

    read_jbm(p) — байты .jbm части p (у вызывающего свой способ чтения архива);
    on_added(p, n) — журнал «добавлено n костей» (вызывается только при n > 0);
    on_part(p, jbm) — журнал/проверка части после resolve_names.
    Возвращает dict(tex=текстура первой части, meshes=все меши) или None: часть не разобралась —
    печатается «FAIL parse part <p>» (прежняя строка всех копий)."""
    all_meshes = []
    tex0 = None
    for p in parts:
        jbm = T.parse_jbm_ext(read_jbm(p))
        if not jbm:
            print("FAIL parse part", p)
            return None
        added = add_part_bones(ref_jbb, jbm)
        if added and on_added:
            on_added(p, added)
        T.resolve_names(jbm, ref_jbb)
        if on_part:
            on_part(p, jbm)
        tex0 = tex0 or jbm['tex']
        all_meshes.extend(jbm['meshes'])
    return dict(tex=tex0, meshes=all_meshes)


def fit_binds(T, merged, ref_jbb):
    """Истинный bind склеенных мешей: recover_binds(merged, B) (без bones-регуляризации — как у всех копий) и
    ★ 2026-09-11 trs_refit_binds (bind в форму T·R·S при СТРОГОМ улучшении отклонения в UE; откат ASDA_BIND_TRS_REFIT=0).
    Возвращает (IBM_game, resid, ntrs, nfit, res): nfit — костей с восстановленным bind, res — невязки без None."""
    B = ref_jbb['B']
    IBM_game, resid = T.recover_binds(merged, B)
    ntrs = T.trs_refit_binds(merged, ref_jbb, IBM_game)
    nfit = sum(1 for x in IBM_game if x is not None)
    res = [r for r in resid if r is not None]
    return IBM_game, resid, ntrs, nfit, res


def write_glb(T, merged, clips, out, IBM_game):
    """build_fixed(bake_pose=False) — рантайм-скиннинг на общем скелете — и validate. → (bind_err, maxr, biggest)."""
    invBind, _bindG, W = T.build_fixed(merged, clips, out, IBM_game, bake_pose=False)
    return T.validate(merged, clips[0][1], IBM_game, invBind, W)


# ─────────────────────────────────────────────────────────────── импорт клипов в UE
def anim_only_pipeline(skel):
    """Interchange-override: класть ТОЛЬКО анимации на уже существующий скелет `skel` (без материалов и текстур)."""
    import unreal
    pipe = unreal.InterchangeGenericAssetsPipeline()
    csa = pipe.get_editor_property("common_skeletal_meshes_and_animations_properties")
    csa.set_editor_property("skeleton", skel)
    csa.set_editor_property("import_only_animations", True)
    mp = pipe.get_editor_property("material_pipeline")
    mp.set_editor_property("import_materials", False)
    try:
        mp.get_editor_property("texture_pipeline").set_editor_property("import_textures", False)
    except Exception:
        pass
    ov = unreal.InterchangePipelineStackOverride()
    ov.add_pipeline(pipe)
    return ov


def import_anims_to_tmp(glb, skel, tmp):
    """Interchange-импорт .glb ТОЛЬКО анимациями на существующий скелет `skel` во временную папку `tmp`
    (материалы и текстуры не импортируются, ассеты не сохраняются). Папка `tmp` очищается заранее."""
    import unreal
    EAL = unreal.EditorAssetLibrary
    if EAL.does_directory_exist(tmp):
        EAL.delete_directory(tmp)
    ov = anim_only_pipeline(skel)
    t = unreal.AssetImportTask()
    t.set_editor_property("filename", glb)
    t.set_editor_property("destination_path", tmp)
    t.set_editor_property("automated", True)
    t.set_editor_property("replace_existing", True)
    t.set_editor_property("save", False)
    t.set_editor_property("options", ov)
    unreal.AssetToolsHelpers.get_asset_tools().import_asset_tasks([t])


def import_gender_clips(g, raw, skel_path, tmp_base, dest_base, expect, log):
    """Импорт мини-glb клипов пола `g` (`raw`/ch1_<g>_cremmy_preview.glb) на существующий скелет `skel_path`:
    import_anims_to_tmp → «…anim_NN» каждого номера из `expect` переносится на `dest_base % g`/ch1_<g>_cremmy_previewanim_NN
    (прежний ассет удаляется), сохраняется; временная папка удаляется; проверка длины и скелета каждого клипа.
    Одна реализация import_gender импортёров эмоций, наборов оружия и рыбалки (через неё — копания). → True/False."""
    import unreal
    EAL = unreal.EditorAssetLibrary
    AR = unreal.AssetRegistryHelpers.get_asset_registry()
    glb = os.path.join(raw, "ch1_%s_cremmy_preview.glb" % g)
    if not os.path.exists(glb):
        log("НЕТ glb: %s — пропуск" % glb); return False
    skel = unreal.load_asset(skel_path)
    if not skel:
        log("!! скелет не найден: %s" % skel_path); return False
    tmp = tmp_base + "_" + g
    import_anims_to_tmp(glb, skel, tmp)

    found = {}
    for ad in AR.get_assets_by_path(unreal.Name(tmp), recursive=True):
        cn = str(ad.asset_class_path.asset_name); an = str(ad.asset_name)
        if cn != "AnimSequence": continue
        m = re.search(r'anim_(\d+)$', an)
        if m: found[int(m.group(1))] = str(ad.package_name)
    miss = [n for n in expect if n not in found]
    if miss:
        log("!! [%s] не импортировались клипы: %s" % (g, miss)); return False

    ok = True
    for n in expect:
        dst = (dest_base % g) + "/ch1_%s_cremmy_previewanim_%02d" % (g, n)
        if EAL.does_asset_exist(dst):
            EAL.delete_asset(dst)
        if not EAL.rename_asset(found[n], dst):
            log("!! [%s] rename %s -> %s не удался" % (g, found[n], dst)); ok = False; continue
        EAL.save_asset(dst, only_if_is_dirty=False)
    EAL.delete_directory(tmp)   # выбрасывает "__ref_ignore" и остатки

    # верификация: длина > 0 и скелет тот самый
    bad = []
    for n in expect:
        dst = (dest_base % g) + "/ch1_%s_cremmy_previewanim_%02d" % (g, n)
        a = unreal.load_asset(dst)
        if not a: bad.append((n, "нет ассета")); continue
        ln = float(a.get_editor_property("sequence_length"))
        sk = a.get_editor_property("skeleton")
        if ln <= 0 or sk is None or sk.get_name() != os.path.basename(skel_path):
            bad.append((n, "len=%.3f skel=%s" % (ln, sk.get_name() if sk else "NONE")))
    if bad:
        log("!! [%s] проблемные клипы: %s" % (g, bad)); ok = False
    else:
        log("[%s] OK: %d клипов anim_%d..%d на скелете %s"
            % (g, len(expect), min(expect), max(expect), os.path.basename(skel_path)))
    return ok
