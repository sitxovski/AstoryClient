# Copyright (c) 2026 @sitxovski (Discord)
# asda_fix_mob_materials.py — ЭТАЛОННАЯ привязка текстур к материалам мобов
#   (/Models/Mob = статик, /Models/MobSkel = скелетал).
#
# ★ БЫЛО (до 2026-08-02): текстура подбиралась ЭВРИСТИКОЙ по имени MIC/модели (чоп цифр, чоп «_NN»,
#   фолбэк «имя модели», переключение на «-b_» если в имени слота есть «-b_»). Эвристика ошибалась
#   там, где matTable модели содержит авторские имена: у `mon_s_0_ragepig01-a_00` слоты названы
#   `ragepig`/`ragepig_hammer` — молот получал текстуру тела `-a_00` вместо `-b_00`.
#
# ★ СТАЛО — эталон (полное описание правила и адреса: Tools/asda_mob_texmap.py):
#   текстура сабмеша моба берётся НЕ из модели, а по ТИПУ моба:
#     <map>.MON → индекс типа → <map>.MLK (пары (A,B), по одной на сабмеш) → <map>.MBB (имя .TEX группы A).
#   Эталон modelBuildMeshFromCache@0x11A45E0 копирует дескриптор текстуры `a8[2] + 32*A` в слот
#   сабмеша (+8); matTable из `.jbm` для мобов не используется — значим только пер-меш `matIdx`
#   (@mr+0x1F4), он группирует сабмеши.
#
#   Одна модель обслуживает МНОГО типов с РАЗНЫМИ текстурами (перекраски). На ассет меша кладётся
#   набор ДОМИНИРУЮЩЕГО типа (самый частый среди типов этой модели; при равенстве — минимальный
#   MobId), а точный набор конкретного типа доводит рантайм — AAsdaMobActor::ApplyMobTextures()
#   по DT_MobTextures (импортёр Tools/asda_build_mob_texture_dt.py).
#
# Запуск (headless):  UnrealEditor-Cmd.exe <uproject> -ExecutePythonScript=".../asda_fix_mob_materials.py"
# ★ Охват: ASDA_ONLY_MODELS="Mob/имя1,MobSkel/имя2" — чинить ТОЛЬКО эти модели (точечно после
#   переимпорта). Элемент «<корень>/<модель>» различает СТАТИК (Mob) и СКЕЛЕТАЛ (MobSkel);
#   голое имя совпадёт в обоих корнях.
# ★ ASDA_DRYRUN=1 — только показать «было → стало», ничего не сохранять.
import os, sys, collections
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import unreal
import asda_ue_paths as _U
import asda_mob_texmap as _T
import asda_model_import_ue as MI   # ★ 15.09.2026: единое правило дублей имён текстур (Reports/dedup_audit/tex_dup_rule.md)

ONLY   = set(s.strip() for s in os.environ.get("ASDA_ONLY_MODELS", "").split(",") if s.strip())
DRYRUN = os.environ.get("ASDA_DRYRUN", "") not in ("", "0")

EAL = unreal.EditorAssetLibrary
AR  = unreal.AssetRegistryHelpers.get_asset_registry()
MEL = unreal.MaterialEditingLibrary
MASTER   = _U.M_OBJECT_LIT
TEX_ROOT = _U.G_TEXTURES
ROOTS    = ((_U.G_MODELS_MOB, "StaticMeshes", "static_materials"),
            (_U.G_MODELS_MOBSKEL, "SkeletalMeshes", "materials"))
MOB_KIND = "mob"   # ★ 15.09.2026: тип модели для выбора копии текстуры — Textures/Mobs/Texture (архив Data\Mob\Texture\Texture.btz)


def log(m): unreal.log_warning("[fix_mob_mat] %s" % m)


def texture_index():
    """★ 15.09.2026 (tex_dup_rule.md): индекс Texture2D через asda_model_import_ue. Охват прежний — корень TEX_ROOT (Textures)
    без UI-портретов `/Portrait/` (пресет noportrait_last). БЫЛО: свой индекс, при дублях имени побеждала ПЕРВАЯ копия в порядке
    реестра (setdefault) — выбор зависел от порядка реестра. СТАЛО: все копии в `copies` по рангу, копию выбирает tex_for_stem."""
    return MI.tex_index("noportrait_last")


def tex_for_stem(tex, stem, model_dir):
    """★ 15.09.2026. Package path текстуры `stem` (имя из эталона .MBB) для модели моба в папке `model_dir` или None.
    Модель из DATA_PINNED_TEX (asda_model_import_ue) получает закреплённую копию, если её имя совпадает с эталонным; иначе —
    строка в лог и None (слот не трогаем: закрепление менять только решением). Остальные — копия из папки своего типа
    (kind="mob"), при её отсутствии — копия с наименьшим рангом (tex_lookup)."""
    pin = MI.data_pinned_dir(model_dir)
    if pin:
        pinned = MI.DATA_PINNED_TEX[pin]
        if pinned.rsplit("/", 1)[-1].lower() == stem.lower():
            return pinned
        log("⚠ %s закреплена данными (%s), а эталон даёт %s — слот не трогаю" % (model_dir, pinned, stem))
        return None
    return MI.tex_lookup(tex, stem.lower(), MOB_KIND)


def slot_to_matidx(slots, jb):
    """СЛОТ → matIdx: сначала по ИМЕНИ (имя слота = имя материала matTable без расширения),
    остаток — позиционно в порядке ПЕРВОГО ПОЯВЛЕНИЯ matIdx среди сабмешей.

    ★ 2026-08-18: имя слота может нести МЕТКУ КЛАССА (`_msk`, `_blend`) — так разводятся подсетки
      РАЗНЫХ техник, делящие одну текстуру (asda_mesh_render_class). Текстура у них ОБЩАЯ, поэтому
      перед сопоставлением со `stems` метку снимаем; иначе такой слот не нашёлся бы по имени и ушёл
      бы в позиционный остаток — то есть получил бы ЧУЖУЮ текстуру."""
    import asda_mesh_render_class as _MC
    stems = [os.path.splitext(t)[0] for t in jb["matTable"]]
    order = []
    for mi in jb["matIdx"]:
        if mi not in order:
            order.append(mi)
    res, taken = [None] * len(slots), set()
    for i, s0 in enumerate(slots):
        s = _MC.strip_class(s0)
        if s in stems:
            mi = stems.index(s)
            if mi not in taken:
                res[i] = mi
                taken.add(mi)
    rest = [mi for mi in order if mi not in taken]
    for i in range(len(slots)):
        if res[i] is None and rest:
            res[i] = rest.pop(0)
    return res


def dominant_set(mobs):
    """Набор {matIdx: tex} ДОМИНИРУЮЩЕГО типа модели: самый частый; при равенстве — минимальный MobId."""
    cnt = collections.Counter(tuple(sorted(d.items())) for d in mobs.values())
    top = max(cnt.values())
    best, best_id = None, None
    for mid in sorted(mobs):
        k = tuple(sorted(mobs[mid].items()))
        if cnt[k] == top and best is None:
            best, best_id = mobs[mid], mid
    return best, best_id


def main():
    master = unreal.load_asset(MASTER)
    R   = _T.resolve()
    JB  = _T.jbm_index()
    TEX = texture_index()
    log("эталон: типов %d, моделей %d | текстур в индексе %d | конфликтов %d, аномалий %d"
        % (len(R["by_mob"]), len(R["by_model"]), len(TEX), len(R["conflicts"]), len(R["anomalies"])))

    changed, same, no_etalon, no_tex = [], 0, [], []
    seen_mic = {}                       # MIC → (модель, слот, текстура) — ловим общие MIC у разных слотов
    for root, sub, prop in ROOTS:
        kind = root.rsplit("/", 1)[-1]
        for d in EAL.list_assets(root, recursive=False, include_folder=True):
            model = d.rstrip("/").rsplit("/", 1)[-1]
            if ONLY and not (model in ONLY or ("%s/%s" % (kind, model)) in ONLY):
                continue
            mesh = unreal.load_asset("%s/%s/%s/%s" % (root, model, sub, model))
            if not mesh:
                continue
            jb = JB.get(model.lower())
            mobs = R["by_model_lc"].get(model.lower())
            if not jb or not mobs:
                no_etalon.append("%s/%s" % (kind, model)); continue
            base, base_id = dominant_set(mobs)
            mats  = mesh.get_editor_property(prop)
            slots = [str(m.get_editor_property("material_slot_name")) for m in mats]
            mapping = slot_to_matidx(slots, jb)
            for i, m in enumerate(mats):
                mi_asset = m.get_editor_property("material_interface")
                if not mi_asset or not isinstance(mi_asset, unreal.MaterialInstanceConstant):
                    continue
                mi = mapping[i]
                tex_name = base.get(mi) if mi is not None else None
                if not tex_name:
                    no_tex.append("%s/%s слот %s (matIdx %s)" % (kind, model, slots[i], mi)); continue
                stem = _T.tex_stem(tex_name)
                pkg = tex_for_stem(TEX, stem, "%s/%s" % (root, model))   # ★ 15.09.2026: копия по типу (mob) и DATA_PINNED_TEX
                if not pkg:
                    no_tex.append("%s/%s слот %s → нет текстуры %s" % (kind, model, slots[i], stem)); continue
                key = mi_asset.get_path_name()
                if key in seen_mic and seen_mic[key][2].lower() != stem.lower():
                    log("⚠ MIC %s используется двумя слотами с РАЗНЫМИ текстурами: %s и %s/%s"
                        % (mi_asset.get_name(), seen_mic[key], model, slots[i]))
                seen_mic[key] = (model, slots[i], stem)

                MEL.set_material_instance_parent(mi_asset, master)
                cur = None
                try:
                    cur = MEL.get_material_instance_texture_parameter_value(mi_asset, "Tex")
                except Exception:
                    pass
                if cur and cur.get_name().lower() == stem.lower():
                    same += 1
                    continue
                changed.append("%-8s %-32s слот %-28s %-30s → %s (тип %d)"
                               % (kind, model, slots[i], cur.get_name() if cur else "НЕТ", stem, base_id))
                if not DRYRUN:
                    MEL.set_material_instance_texture_parameter_value(mi_asset, "Tex", unreal.load_asset(pkg))
                    EAL.save_loaded_asset(mi_asset)

    # ★★★ МАСТЕР ПО ТЕХНИКЕ ПОДСЕТКИ (2026-08-18, `Reports/blend_rollout_npc_mobs.md`).
    #   Выше слотам ставился ОДИН masked-мастер `M_AsdaObjectLit` — это верно только для техники
    #   tNoSkin. У tSkin/tRigid (856 подсеток мобов) альфа текстуры в прозрачность не идёт
    #   (`PShade()`: `Output.RGBColor.w = 1*fog*4*fade*0.25`), она маскирует блик, поэтому masked
    #   вырезал геометрию. А у 17 подсеток шести моделей техника tNoSkin И взведён рендер-бит
    #   `mr+0x004 & 0x1|0x2` — им нужен прозрачный мастер. Класс слота считается из `meshType`
    #   модели `.jbm`, поэтому прогон идемпотентен и воспроизводится при переимпорте.
    if not DRYRUN:
        import asda_npc_mob_masters as _MM
        _MM.apply(only_roots={"Mob", "MobSkel"})
    log("ИЗМЕНЕНО: %d | уже верных: %d | слотов без эталонной текстуры: %d | моделей без эталона: %d%s"
        % (len(changed), same, len(no_tex), len(no_etalon), "  [DRYRUN]" if DRYRUN else ""))
    for c in changed:
        unreal.log("   " + c)
    for c in sorted(set(no_tex)):
        log("   без текстуры: " + c)
    for c in sorted(set(no_etalon)):
        log("   без эталонных данных (нет ни на одной карте): " + c)
    unreal.log("=== MOB MAT FIX DONE ===")


# ★ ЗАКОН [[tools-no-import-side-effects]]: при импорте модуль НИЧЕГО не делает.
#   `__spec__` выставляет ЛЮБАЯ машинерия импорта (import / importlib / runpy) и НЕ
#   выставляет прямое исполнение файла (python file.py, UE -ExecutePythonScript).
#   `python -m` даёт `__name__ == "__main__"` и покрыт первым условием.
if __name__ == "__main__" or globals().get("__spec__") is None:
    main()
