# Copyright (c) 2026 @sitxovski (Discord)
# _restore_skydome_mesh.py — ВОССТАНОВЛЕНИЕ ассета SM_skydome карты (env ASDA_MAP) после bulk-delete /Models
#   первым прогоном ASDA_MESH_ONLY 03.09.2026. Только меш: та же build_dome_mesh из asda_build_sky_dome.py,
#   материалы — существующие MI_skydome/MI_skyhorizon; уровень и мастера НЕ трогаются.
import os as _os, sys as _sys, os
_sys.path.insert(0, _os.path.dirname(_os.path.abspath(__file__)))
import unreal
import asda_build_sky_dome as SKY
import asda_login_guard as _LG   # ★ 16.09.2026 (обратный обход savedir14): единый гейт сцены логина
EAL = unreal.EditorAssetLibrary
def main():
    import asda_maps as _MAPS
    SKY.DEST_ROOT = "/Game/Map/" + _MAPS.map_asset_name(SKY.MAP_NAME)   # как в SKY._script_main (там же задаётся)
    # ★ 16.09.2026 (обратный обход ревизии savedir14): имя карты приходит из ASDA_MAP (через
    #   asda_build_sky_dome.MAP_NAME) и ни с чем не сверяется, а скрипт пересобирает и СОХРАНЯЕТ SM_skydome
    #   под корнем карты. Гейт сцены логина — ДО разбора JBM и до save_loaded_asset. Без quiet: скрипт
    #   разовый, на одну карту, и молчаливый холостой прогон был бы непонятен.
    if _LG.login_map_write_blocked(SKY.DEST_ROOT, "_restore_skydome_mesh.py"):
        return
    mdir = os.path.join(SKY.CLIENT_MAP, SKY.MAP_NAME)
    idx, verts, vtx, tri = SKY.parse_jbm(os.path.join(mdir, "SKY_DOME.JBM"))
    sm = SKY.build_dome_mesh(idx, verts)
    mic = EAL.load_asset(SKY.DEST_ROOT + "/Materials/MI_skydome"); micH = EAL.load_asset(SKY.DEST_ROOT + "/Materials/MI_skyhorizon")
    sm.set_editor_property("static_materials", [
        unreal.StaticMaterial(material_interface=mic,  material_slot_name="Clouds"),
        unreal.StaticMaterial(material_interface=micH, material_slot_name="Horizon")])
    EAL.save_loaded_asset(sm)
    unreal.log("[restore_skydome] ГОТОВО %s: SM_skydome vtx=%d tri=%d, MIC=%s/%s" % (SKY.MAP_NAME, vtx, tri, bool(mic), bool(micH)))
if __name__ == "__main__" or globals().get("__spec__") is None:
    main()
