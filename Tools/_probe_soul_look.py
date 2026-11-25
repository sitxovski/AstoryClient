# -*- coding: utf-8 -*-
# Copyright (c) 2026 @sitxovski (Discord)
# Пробник (ТОЛЬКО ЧТЕНИЕ): подтвердить призрачный вид силуэта «души» после правки.
#   Проверяет: мастер M_AsdaSoulSilhouette (blend/shading/twoSided), у каждого MIC силуэта —
#   родитель + текстура BaseTex, у нативных текстур — sRGB/компрессию. Ничего не меняет и не сохраняет.
import unreal
EAL = unreal.EditorAssetLibrary
MEL = unreal.MaterialEditingLibrary
AR  = unreal.AssetRegistryHelpers.get_asset_registry()
def log(m): unreal.log("[probe_soul] " + str(m))

MASTER = "/Game/Materials/M_AsdaSoulSilhouette"
PREVIEWS = ["cre_cresoulman_preview", "cre_cresoulwoman_preview",
            "cre_cresoulmwwoman_preview", "cre_cresoulwmman_preview"]
BASE = "/Game/AsdaAssets/Models/Skeletal/Characters"
TEXES = ["/Game/AsdaAssets/Textures/Characters/Texture/cre_cresoulman-a_00",
         "/Game/AsdaAssets/Textures/Characters/Texture/cre_cresoulwoman-a_00"]


def main():
    m = unreal.load_asset(MASTER)
    if m:
        log("МАСТЕР %s: blend=%s shading=%s twoSided=%s"
            % (MASTER, m.get_editor_property("blend_mode"),
               m.get_editor_property("shading_model"), m.get_editor_property("two_sided")))
    else:
        log("!! МАСТЕР НЕ НАЙДЕН: %s" % MASTER)

    for t in TEXES:
        tx = unreal.load_asset(t)
        if tx:
            log("ТЕКСТУРА %s: sRGB=%s compression=%s"
                % (t.split("/")[-1], tx.get_editor_property("srgb"),
                   tx.get_editor_property("compression_settings")))
        else:
            log("!! ТЕКСТУРА НЕ НАЙДЕНА: %s" % t)

    for pv in PREVIEWS:
        sub = "%s/%s/Materials" % (BASE, pv)
        for ad in AR.get_assets_by_path(unreal.Name(sub), recursive=True):
            if str(ad.asset_class_path.asset_name) != "MaterialInstanceConstant":
                continue
            a = unreal.load_asset(str(ad.package_name))
            par = a.get_editor_property("parent")
            parname = par.get_name() if par else "НЕТ"
            tv = MEL.get_material_instance_texture_parameter_value(a, "BaseTex")
            tvn = tv.get_name() if tv else "НЕТ"
            log("%s :: %s -> parent=%s BaseTex=%s"
                % (pv, str(ad.asset_name), parname, tvn))
    log("ЗАВЕРШЕНО")


if __name__ == "__main__" or globals().get("__spec__") is None:
    main()
