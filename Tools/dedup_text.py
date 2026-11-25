# Copyright (c) 2026 @sitxovski (Discord)
import unreal
EAL=unreal.EditorAssetLibrary
dupes=["/Game/AsdaAssets/Text/Skill/DT_SkillTreeTooltip","/Game/AsdaAssets/Text/Skill/DT_Skilltooltip","/Game/AsdaAssets/Text/Skill/DT_SoulstateInfotip","/Game/AsdaAssets/Text/Emotion/DT_EmotionTip","/Game/AsdaAssets/Text/Item/DT_hairtable","/Game/AsdaAssets/Text/Item/DT_ItemOptionTable"]


def main():
    """Работа скрипта. Вынесена в функцию, чтобы импорт модуля НИЧЕГО не делал
    (закон [[tools-no-import-side-effects]])."""
    for p in dupes:
        if EAL.does_asset_exist(p): EAL.delete_asset(p); unreal.log("DELETED %s"%p)
        else: unreal.log("нет %s"%p)
    # пустые папки
    for f in ["/Game/AsdaAssets/Text/Emotion","/Game/AsdaAssets/Text/Item"]:
        if EAL.does_directory_exist(f) and not EAL.list_assets(f): EAL.delete_directory(f); unreal.log("папка удалена %s"%f)


# ★ ЗАКОН [[tools-no-import-side-effects]]: при импорте модуль НИЧЕГО не делает.
#   `__spec__` выставляет ЛЮБАЯ машинерия импорта (import / importlib / runpy) и НЕ
#   выставляет прямое исполнение файла (python file.py, UE -ExecutePythonScript).
#   `python -m` даёт `__name__ == "__main__"` и покрыт первым условием.
if __name__ == "__main__" or globals().get("__spec__") is None:
    main()
