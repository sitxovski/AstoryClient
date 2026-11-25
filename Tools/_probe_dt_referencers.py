# Copyright (c) 2026 @sitxovski (Discord)
# _probe_dt_referencers.py — кто ссылается на ассеты-двойники таблиц (проверка перед удалением).
# Источник истины — AssetRegistry (реестр ссылок движка), а не греп по .uasset: путь пакета
# хранится в таблице имён ПО ЧАСТЯМ, поэтому грепом ссылку можно не увидеть (ложное «нет»).
import unreal

TARGETS = [
    "/Game/AsdaAssets/Data/ItemDB/advalue",
    "/Game/AsdaAssets/Data/ItemDB/booster_list",
    "/Game/AsdaAssets/Data/ItemDB/itemoptionrange",
    "/Game/AsdaAssets/Data/ItemDB/premiumleveltable",
    "/Game/AsdaAssets/Data/CallMob",
    "/Game/AsdaAssets/Data/NPC/CallMob",
    "/Game/AsdaAssets/Data/EffectList",
    "/Game/AsdaAssets/Data/MonsterSkillEffect",
    "/Game/AsdaAssets/Data/DT_CallMob",
]


def main():
    ar = unreal.AssetRegistryHelpers.get_asset_registry()
    ar.wait_for_completion()
    for p in TARGETS:
        exists = unreal.EditorAssetLibrary.does_asset_exist(p)
        refs = ar.get_referencers(unreal.Name(p), unreal.AssetRegistryDependencyOptions())
        refs = [str(r) for r in (refs or [])]
        unreal.log_warning("[refs] %-52s есть=%-5s ссылающихся=%d %s"
                           % (p, exists, len(refs), refs[:10]))


if __name__ == "__main__":
    main()
