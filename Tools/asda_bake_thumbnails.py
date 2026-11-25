# -*- coding: utf-8 -*-
# Copyright (c) 2026 @sitxovski (Discord)
"""asda_bake_thumbnails.py — запечь превью ассетов в пакеты.

ЗАЧЕМ. Превью в браузере ассетов пропадают после каждой перезагрузки редактора и появляются
только когда ассет откроешь. Причина не в самом редакторе, а в том, КАК ассеты создавались:
почти всё в этом проекте делают питон-скрипты в ФОНОВОМ редакторе, а он превью не рисует.
В логе сохранения это видно дословно:

    OBJ SavePackage: Generating thumbnails for [0] asset(s) ... ([1] browsable assets)

Ноль превью на один пригодный ассет — картинки в пакете нет. Браузер каждый раз рисует её заново
по памяти, а после перезапуска показывает заглушку, пока ассет не откроешь.

★★★ ПРОВЕРЕНО 24.08.2026: в фоновом режиме превью НЕ ЗАПЕЧЬ НИКАК. Пробовали и с графикой
(`--exec-cmds`, без `-NullRHI`), и с ключом `-AllowCommandletRendering` — во всех прогонах
«Generating thumbnails for [0]». Миниатюру рисует САМ БРАУЗЕР АССЕТОВ при отображении плитки,
а в фоне браузера нет — рисовать некому.

КАК ПОЛЬЗОВАТЬСЯ (единственный работающий способ):
  1. Открыть обычный редактор.
  2. В браузере ассетов пройти по нужным папкам, чтобы плитки ОТРИСОВАЛИСЬ (превью появятся).
  3. Запустить этот скрипт из окна вывода редактора:  py Tools/asda_bake_thumbnails.py
     (или просто «Save All») — тогда нарисованные превью лягут внутрь пакетов и переживут
     перезапуски.

Область по умолчанию — весь `/Game/AsdaAssets`. Сузить: `ASDA_THUMB_PATH=/Game/Map/alpen`.
Ограничить число: `ASDA_THUMB_LIMIT=50`.
"""
import os
import unreal
import sys as _sys_lg   # ★ 15.09.2026: защита сцены логина (asda_login_guard: Models/Backdrop, Char/cre_creback-a_00, cre_creation01* и др.)
_sys_lg.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import asda_login_guard as _LG

ROOT = os.environ.get("ASDA_THUMB_PATH", "/Game/AsdaAssets")
LIMIT = int(os.environ.get("ASDA_THUMB_LIMIT", "0"))

# У этих типов превью — просто иконка типа, рисовать нечего: пропускаем, чтобы не гонять зря.
SKIP_CLASSES = {
    "DataTable", "StringTable", "CurveTable", "DataAsset", "AsdaWalkGrid",
    "SoundWave", "SoundCue", "LevelSequence", "World", "BlueprintGeneratedClass",
}


def log(m):
    unreal.log("[превью] " + str(m))


def main():
    # Фоновый запуск виден по флагу «без присмотра» — в нём превью не нарисуются.
    try:
        bBackground = unreal.SystemLibrary.is_unattended()
    except Exception:
        bBackground = False
    if bBackground:
        log("⚠ ФОНОВЫЙ РЕЖИМ: превью не нарисуются. Запусти скрипт из окна вывода открытого редактора.")

    EAL = unreal.EditorAssetLibrary
    if not EAL.does_directory_exist(ROOT):
        log("каталога %s нет" % ROOT)
        log("ЗАВЕРШЕНО")
        return

    paths = EAL.list_assets(ROOT, recursive=True, include_folder=False)
    log("в каталоге %s ассетов: %d" % (ROOT, len(paths)))

    done = skipped = failed = 0
    n_login = 0   # ★ 15.09.2026: пропущено пакетов сцены логина
    for idx, p in enumerate(paths):
        if LIMIT and done >= LIMIT:
            break
        if _LG.login_package_blocked(p, "asda_bake_thumbnails.py", quiet=True):   # ★ 15.09.2026: сцена логина — пересохранять только с разрешением
            n_login += 1
            continue
        data = EAL.find_asset_data(p)
        cls = str(data.asset_class_path.asset_name) if data else ""
        if cls in SKIP_CLASSES:
            skipped += 1
            continue
        obj = EAL.load_asset(p)
        if not obj:
            failed += 1
            continue
        # Пересохранение с загруженным объектом — именно на этом шаге движок рисует превью.
        if EAL.save_loaded_asset(obj, only_if_is_dirty=False):
            done += 1
        else:
            failed += 1
        if done and done % 200 == 0:
            log("  запечено %d из %d…" % (done, len(paths)))
            unreal.SystemLibrary.collect_garbage()

    log("─── ИТОГ: пересохранено %d, пропущено по типу %d, не удалось %d ───"
        % (done, skipped, failed))
    if n_login:   # ★ 15.09.2026
        log("[LOGIN-GUARD] пропущено пакетов сцены логина: %d (пересохранение — только с ASDA_ALLOW_LOGIN_WRITE=1)" % n_login)
    log("Если в логе выше «Generating thumbnails for [0] asset(s)» — скрипт запущен в ФОНЕ.")
    log("Так превью не запечь: их рисует браузер ассетов. Открой редактор, пролистай папки и")
    log("запусти этот же скрипт из окна вывода (py Tools/asda_bake_thumbnails.py).")
    log("ЗАВЕРШЕНО")


# ★ Закон tools-no-import-side-effects (гигиена 15.09.2026): при ИМПОРТЕ main() не запускается.
#   Прямое исполнение (python file.py; UE -ExecutePythonScript / py / -run=pythonscript; exec с
#   __name__="__main__") — запускает: там __name__ == "__main__" и __spec__ отсутствует.
if __name__ == "__main__" or globals().get("__spec__") is None:
    main()
