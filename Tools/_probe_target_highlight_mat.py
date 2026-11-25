# Copyright (c) 2026 @sitxovski (Discord)
import unreal
p="/Game/Materials/M_AsdaTargetHighlight"
mel=unreal.MaterialEditingLibrary
exprs = list(mel.get_inputs_for_material_expression) if False else None
# перебор выражений через селекцию невозможен — читаем входы конкретных нод по свойствам
def dump(e, fields):
    for f in fields:
        try:
            inp = e.get_editor_property(f)
            ex = inp.expression if hasattr(inp,"expression") else None
            unreal.log("[G] %s.%s <- %s (output_index=%s, mask=%s)" % (
                e.get_name(), f, ex.get_name() if ex else "НЕ ПОДКЛЮЧЕНО",
                getattr(inp,"output_index","?"), getattr(inp,"mask","?")))
        except Exception as ex2:
            unreal.log("[G] %s.%s -> %s" % (e.get_name(), f, ex2))


def main():
    """Работа скрипта. Вынесена в функцию, чтобы импорт модуля НИЧЕГО не делал
    (закон [[tools-no-import-side-effects]])."""
    m=unreal.EditorAssetLibrary.load_asset(p)
    emis = mel.get_material_property_input_node(m, unreal.MaterialProperty.MP_EMISSIVE_COLOR)
    unreal.log("[G] EMISSIVE <- %s" % emis.get_name())
    dump(emis, ["a","b","const_a","const_b"])


# ★ ЗАКОН [[tools-no-import-side-effects]]: при импорте модуль НИЧЕГО не делает.
#   `__spec__` выставляет ЛЮБАЯ машинерия импорта (import / importlib / runpy) и НЕ
#   выставляет прямое исполнение файла (python file.py, UE -ExecutePythonScript).
#   `python -m` даёт `__name__ == "__main__"` и покрыт первым условием.
if __name__ == "__main__" or globals().get("__spec__") is None:
    main()
