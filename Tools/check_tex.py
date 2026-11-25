# Copyright (c) 2026 @sitxovski (Discord)
import unreal


def main():
    """Работа скрипта. Вынесена в функцию, чтобы импорт модуля НИЧЕГО не делал
    (закон [[tools-no-import-side-effects]])."""
    for nm in ["n_01","ne_skillbar1","ne_ap_roulette"]:
        t=unreal.load_asset("/Game/AsdaAssets/Textures/JUI/%s"%nm)
        if not t: unreal.log("MISS %s"%nm); continue
        f=t.get_editor_property("filter"); m=t.get_editor_property("mip_gen_settings"); g=t.get_editor_property("lod_group")
        unreal.log("TEXCHK %s filter=%s mip=%s group=%s"%(nm,f,m,g))


# ★ ЗАКОН [[tools-no-import-side-effects]]: при импорте модуль НИЧЕГО не делает.
#   `__spec__` выставляет ЛЮБАЯ машинерия импорта (import / importlib / runpy) и НЕ
#   выставляет прямое исполнение файла (python file.py, UE -ExecutePythonScript).
#   `python -m` даёт `__name__ == "__main__"` и покрыт первым условием.
if __name__ == "__main__" or globals().get("__spec__") is None:
    main()
