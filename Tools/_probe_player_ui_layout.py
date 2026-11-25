# Copyright (c) 2026 @sitxovski (Discord)
# Проверка раскладки Player_UI: счётчики по папкам, загрузка КЛАССОВ, на которые ссылается C++,
# отсутствие ObjectRedirector во всём дереве /Game/AsdaAssets/UI.
import unreal

EAL = unreal.EditorAssetLibrary
ROOT = "/Game/AsdaAssets/UI"

# ★ пути, захардкоженные в C++ (после переноса) — грузим ИМЕННО как класс, как это делает LoadClass
CPP_PATHS = [
    "/Game/AsdaAssets/UI/Player_UI/Map/WBP_MINIMAP_WIN.WBP_MINIMAP_WIN_C",
    "/Game/AsdaAssets/UI/Player_UI/Map/WBP_MINIMAPMIN_WIN.WBP_MINIMAPMIN_WIN_C",
    "/Game/AsdaAssets/UI/Player_UI/Map/WBP_MAXMAP_WIN.WBP_MAXMAP_WIN_C",
    "/Game/AsdaAssets/UI/Player_UI/HUD/WBP_VILLAGE_NAME.WBP_VILLAGE_NAME_C",
    "/Game/AsdaAssets/UI/GameMain/WBP_NAMEPLATE_MON.WBP_NAMEPLATE_MON_C",
    "/Game/AsdaAssets/UI/GameMain/WBP_NAMEPLATE_MAN1.WBP_NAMEPLATE_MAN1_C",
    "/Game/AsdaAssets/UI/Login/WBP_LOGIN_WIN.WBP_LOGIN_WIN_C",
]


def main():
    folders, redirs, bad = {}, [], []
    for a in EAL.list_assets(ROOT, recursive=True, include_folder=False):
        p = a.split(".")[0]
        d = p.rsplit("/", 1)[0].replace(ROOT + "/", "")
        folders[d] = folders.get(d, 0) + 1
        data = EAL.find_asset_data(a)
        if data and str(data.asset_class_path.asset_name) == "ObjectRedirector":
            redirs.append(p)
        if not EAL.load_asset(p):
            bad.append(p)
    total = sum(folders.values())
    for d in sorted(folders):
        unreal.log("  %-28s %d" % (d, folders[d]))
    unreal.log("=== ВСЕГО в UI: %d, редиректоров=%d, не грузится=%d ===" % (total, len(redirs), len(bad)))
    for p in redirs: unreal.log_error("РЕДИРЕКТОР: %s" % p)
    for p in bad:    unreal.log_error("НЕ ГРУЗИТСЯ: %s" % p)
    for cp in CPP_PATHS:
        cls = unreal.load_object(None, cp)
        unreal.log("  C++ путь %s → %s" % (cp, "OK" if cls else "ПРОВАЛ"))
        if not cls: unreal.log_error("C++ ПУТЬ БИТЫЙ: %s" % cp)


if __name__ == "__main__":
    main()
