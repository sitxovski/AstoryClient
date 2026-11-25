# Copyright (c) 2026 @sitxovski (Discord)
# asda_verify_mod_smoke.py — верификация mod-бленда: мастера + акторы flammio (a_09/a_10)
import unreal
EAL = unreal.EditorAssetLibrary
les = unreal.get_editor_subsystem(unreal.LevelEditorSubsystem)
eas = unreal.get_editor_subsystem(unreal.EditorActorSubsystem)
cnt = {"mod_ok": 0, "mod_bad": 0}


def main():
    """Работа скрипта. Вынесена в функцию, чтобы импорт модуля НИЧЕГО не делал
    (закон [[tools-no-import-side-effects]])."""
    for p in ("/Game/Effects/M_AsdaEffectMod", "/Game/Effects/M_AsdaEffectModAdd"):
        m = EAL.load_asset(p)
        unreal.log("[VERIFY] %s: exists=%s blend=%s shading=%s two_sided=%s" % (
            p, m is not None,
            m.get_editor_property("blend_mode") if m else "-",
            m.get_editor_property("shading_model") if m else "-",
            m.get_editor_property("two_sided") if m else "-"))
    les.load_level("/Game/Map/flammio/flammio")
    for a in eas.get_all_level_actors():
        if a.get_class().get_name() != "AsdaEffectActor": continue
        nm = a.get_editor_property("effect_name")
        if nm not in ("eff_flammio-a_09", "eff_flammio-a_10"): continue
        ok = True
        for em in a.get_editor_property("emitters"):
            mat = em.get_editor_property("material"); matb = em.get_editor_property("material_b")
            mn = mat.get_name() if mat else "None"; bn = matb.get_name() if matb else "None"
            if not mn.endswith("_mod") or not bn.endswith("_modadd"): ok = False
            if cnt["mod_ok"] + cnt["mod_bad"] == 0:
                unreal.log("[VERIFY] пример %s em=%s mat=%s matB=%s" % (nm, em.get_editor_property("name"), mn, bn))
        cnt["mod_ok" if ok else "mod_bad"] += 1
    unreal.log("[VERIFY] flammio a_09/a_10: ok=%d bad=%d" % (cnt["mod_ok"], cnt["mod_bad"]))


# ★ ЗАКОН [[tools-no-import-side-effects]]: при импорте модуль НИЧЕГО не делает.
#   `__spec__` выставляет ЛЮБАЯ машинерия импорта (import / importlib / runpy) и НЕ
#   выставляет прямое исполнение файла (python file.py, UE -ExecutePythonScript).
#   `python -m` даёт `__name__ == "__main__"` и покрыт первым условием.
if __name__ == "__main__" or globals().get("__spec__") is None:
    main()
