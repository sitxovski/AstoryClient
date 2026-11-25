# Copyright (c) 2026 @sitxovski (Discord)
# Реорганизация UI: UI/Generated/* → UI/Login/* (дерево UI → CLASS → NAME), удалить Generated.
# Логин-экраны (WBP_LoginScreen/CharSelect/ServerSelect) = класс Login.
import unreal
EAL = unreal.EditorAssetLibrary
# ★ D5 (волна 2 п. 7, 14.09.2026, сверка с Content): папки /Game/AsdaAssets/UI/Generated нет (разовая реорганизация уже прошла); путь не менялся.
SRC = "/Game/AsdaAssets/UI/Generated"
DST = "/Game/AsdaAssets/UI/Login"


def main():
    """Работа скрипта. Вынесена в функцию, чтобы импорт модуля НИЧЕГО не делал
    (закон [[tools-no-import-side-effects]])."""
    if not EAL.does_directory_exist(DST):
        EAL.make_directory(DST)
    moved = 0
    # ★ 15.09.2026 (Reports/dedup_audit/tex_rebind_2026-09-15.md, раздел 13): перенос в UI/Login создаёт пакеты сцены логина (кроме LOGIN_SHARED_EXCEPTIONS) —
    #   без ASDA_ALLOW_LOGIN_WRITE=1 такие ассеты остаются в Generated, а сама Generated не удаляется (снос стёр бы их).
    import os as _os_lg, sys as _sys_lg
    _sys_lg.path.insert(0, _os_lg.path.dirname(_os_lg.path.abspath(__file__)))   # Tools — для asda_login_guard
    from asda_login_guard import login_package_blocked
    login_skipped = 0   # сколько переносов в окна логина пропущено защитой
    if EAL.does_directory_exist(SRC):
        for a in EAL.list_assets(SRC, recursive=False, include_folder=False):
            name = a.split("/")[-1].split(".")[0]
            newp = "%s/%s" % (DST, name)
            if EAL.does_asset_exist(newp):
                unreal.log("уже есть %s — пропуск" % newp); continue
            if login_package_blocked(newp, "asda_reorg_ui.py"):   # ★ 15.09.2026: окно логина — перенос только с разрешением
                login_skipped += 1; continue
            if EAL.rename_asset(a, newp):   # rename = move + оставляет redirector
                moved += 1; unreal.log("  %s -> %s" % (name, newp))
            else:
                unreal.log_warning("не перенёс %s" % a)
        # подчистить редиректоры + удалить пустую Generated
        unreal.AssetToolsHelpers.get_asset_tools().fixup_referencers_in_folders([SRC])
        try:
            if login_skipped:   # ★ 15.09.2026: в Generated остались пропущенные окна логина — папку НЕ удалять
                unreal.log_warning("[LOGIN-GUARD] asda_reorg_ui.py: %s НЕ удалена — в ней %d ассетов для UI/Login "
                                   "(перенос только с ASDA_ALLOW_LOGIN_WRITE=1)" % (SRC, login_skipped))
            else:
                EAL.delete_directory(SRC)
        except Exception as e:
            unreal.log_warning("Generated не удалён (%s) — удали вручную" % e)
    else:
        unreal.log("Generated нет — уже перенесено")
    unreal.log("=== UI реорганизован: перенесено %d в UI/Login; Generated удалён ===" % moved)


# ★ ЗАКОН [[tools-no-import-side-effects]]: при импорте модуль НИЧЕГО не делает.
#   `__spec__` выставляет ЛЮБАЯ машинерия импорта (import / importlib / runpy) и НЕ
#   выставляет прямое исполнение файла (python file.py, UE -ExecutePythonScript).
#   `python -m` даёт `__name__ == "__main__"` и покрыт первым условием.
if __name__ == "__main__" or globals().get("__spec__") is None:
    main()
