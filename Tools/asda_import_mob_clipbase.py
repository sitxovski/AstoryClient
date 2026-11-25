# Copyright (c) 2026 @sitxovski (Discord)
# asda_import_mob_clipbase.py — ТОЧЕЧНЫЙ импорт вариантов мобов «меш +0x04 + клипы базы +0x24» в UE. [ОРИГИНАЛ]
#
# ЗАЧЕМ (30.10.2026, `Reports/mob_skills/anim_slots.md` §побочная 2): оригинал анимирует меш `Mob list +0x04` клипами
#   базы `+0x24`; у 28 мешей своих клипов нет (порт ставил их статикой без анимации, в т.ч. без навыков), у kcat03 / minewar obj_1
#   часть записей анимируется чужим набором. `asda_build_mob_fixed.py --clipbase` собирает `<меш>__<база>.glb`; этот скрипт
#   импортирует ТОЛЬКО их в `/Game/AsdaAssets/Models/MobSkel/<меш>__<база>/` — без стирания папки (в отличие от
#   `asda_import_mob_skel.py`, который чистит весь MobSkel), материалы и мастера подвязываются только в новых папках.
#   Рантайм берёт вариант по `AsdaMobClipBase.inl` (`UAsdaWorldSubsystem::SpawnNetMobDynamic`, ключ `asda.Mob.ClipBaseModel`).
# ASDA_MOBCLIPBASE_ONLY="вариант1;вариант2" — только перечисленные.
# Запуск: python Tools/run_headless_safe.py Tools/asda_import_mob_clipbase.py --marker "ГОТОВО: вариантов"  (редактор закрыт)
#
# ★ ПРАВИЛО tools-no-import-side-effects: при импорте модуля НИЧЕГО не выполняется — только main() при прямом запуске.
import os
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))

import unreal
import asda_ue_paths as _U
import asda_paths as _P
import asda_model_import_ue as MI

RAW_GLB = os.path.join(_P.ROOT, "AstoryClient", "RawAssets", "Models", "Skeletal", "Mob")
MODELS = _U.G_MODELS_MOBSKEL
TEX_ROOT = _U.G_TEXTURES
MASTER = _U.M_OBJECT_LIT
EAL = unreal.EditorAssetLibrary


def log(m):
    unreal.log_warning("[mobclipbase] %s" % m)


def main():
    only = set(x.strip().lower() for x in os.environ.get("ASDA_MOBCLIPBASE_ONLY", "").split(";") if x.strip())
    glbs = sorted(f for f in os.listdir(RAW_GLB) if f.lower().endswith(".glb") and "__" in f)
    if only:
        glbs = [f for f in glbs if f[:-4].lower() in only]
    if not glbs:
        log("вариантов нет — сначала python Tools/asda_build_mob_fixed.py --clipbase")
        return
    variants = [f[:-4] for f in glbs]
    for i in range(0, len(glbs), 8):
        MI.import_batch([os.path.join(RAW_GLB, f) for f in glbs[i:i + 8]], MODELS)
        for v in variants[i:i + 8]:
            EAL.save_directory("%s/%s" % (MODELS, v), only_if_is_dirty=True, recursive=True)
        try:
            unreal.SystemLibrary.collect_garbage()
        except Exception:
            pass
        log("  импорт %d/%d" % (min(i + 8, len(glbs)), len(glbs)))
    master = unreal.load_asset(MASTER)
    tex = MI.tex_index("noportrait_last", root=TEX_ROOT)
    n_tex = n_miss = 0
    for v in variants:
        root = "%s/%s" % (MODELS, v)   # ★ подвязка — только в папке варианта (чужие MIC не трогаются)
        st = MI.wire_materials(root, tex_for=lambda n: MI.find_tex(n, tex, "plain_notail", kind="mob"), master=master, ambient=False)
        n_tex += st["n_tex"]; n_miss += st["n_miss"]
        EAL.save_directory(root, only_if_is_dirty=True, recursive=True)
    import asda_npc_mob_masters as MM
    alias = {v.lower(): v.split("__", 1)[0].lower() for v in variants}   # класс слотов — у меша +0x04
    MM.apply(only_roots={"MobSkel"}, only_models=set(alias.keys()), model_alias=alias)
    for v in variants:
        EAL.save_directory("%s/%s" % (MODELS, v), only_if_is_dirty=True, recursive=True)
    missing = [v for v in variants
               if not EAL.does_asset_exist("%s/%s/SkeletalMeshes/%s" % (MODELS, v, v))]
    log("ГОТОВО: вариантов %d, скелеталов нет у %d %s, материалов с текстурой %d, без %d"
        % (len(variants), len(missing), missing, n_tex, n_miss))


if __name__ == "__main__" or globals().get("__spec__") is None:
    main()
