# -*- coding: utf-8 -*-
# Copyright (c) 2026 @sitxovski (Discord)
# asda_probe_mat_api.py — ТОЛЬКО ЧТЕНИЕ. Разведка API: как из UE-Python добраться до узлов-параметров
# мастер-материала (нужно, чтобы поменять их `default_value`).
import unreal

EAL = unreal.EditorAssetLibrary
mat = EAL.load_asset("/Game/Materials/M_AsdaCloudShadowLF")
unreal.log("[api] тип: %s" % type(mat))
props = []
for n in ("expressions", "expression_collection", "material_graph", "editor_only_data"):
    try:
        v = mat.get_editor_property(n)
        props.append("%s = %s" % (n, type(v)))
    except Exception as e:                                   # noqa: BLE001
        props.append("%s: НЕТ (%s)" % (n, e))
unreal.log("[api] свойства: %s" % " | ".join(props))
mel = [m for m in dir(unreal.MaterialEditingLibrary) if "expression" in m.lower() or "default" in m.lower()]
unreal.log("[api] MaterialEditingLibrary: %s" % ", ".join(sorted(mel)))
try:
    ed = unreal.MaterialEditingLibrary.get_material_expressions(mat)
    unreal.log("[api] get_material_expressions вернул %d" % len(ed))
except Exception as e:                                       # noqa: BLE001
    unreal.log("[api] get_material_expressions: %s" % e)
try:
    eod = unreal.MaterialEditingLibrary.get_material_editor_only_data(mat) \
        if hasattr(unreal.MaterialEditingLibrary, "get_material_editor_only_data") else None
    unreal.log("[api] editor_only_data helper: %s" % type(eod))
except Exception as e:                                       # noqa: BLE001
    unreal.log("[api] editor_only_data helper: %s" % e)
