# Copyright (c) 2026 @sitxovski (Discord)
# asda_place_effects_all.py — раскатка эффектов на ВСЕ карты (одна UE-сессия).
# Per-map: удалить EffectTex/EffectMat (пере-извлечённые un-premultiplied текстуры + свежие MIC),
# затем exec asda_place_effects.py в свежем namespace (env ASDA_MAP).
import os, re, unreal
import os as _os, sys as _sys
_sys.path.insert(0, _os.path.dirname(_os.path.abspath(__file__)))
import asda_maps as _MAPS
import asda_ue_paths as _U   # ★ волна 2 п. 7 (14.09.2026): корни и правила путей — реестр (Tools/data/asda_asset_paths.json)
import asda_login_guard as _LG   # ★ 16.09.2026 (ревизия savedir5, СРЕДН-2): гейт нужен ЗДЕСЬ, до удаления папок
EAL = unreal.EditorAssetLibrary
TOOLS = r"@@ASTORY_DEV_ROOT_FWD@@/AstoryClient/Tools"
_asset = _MAPS.map_asset_name  # ★ единый санитайзер имени карты (asda_maps.map_asset_name)
# ★ Список карт — ЕДИНЫЙ источник asda_maps.py (все 22 карты порта).
#   Было: локальный литерал (13 копий по тулам). Состав сверен 1:1.
MAPS = _MAPS.ALL_MAPS


def main():
    """Работа скрипта. Вынесена в функцию, чтобы импорт модуля НИЧЕГО не делал
    (закон [[tools-no-import-side-effects]])."""
    global MAPS
    failed = 0                     # ★ 16.09.2026 (ревизия savedir5, СРЕДН-2): итог без счётчика провалов врал
    skipped = 0                    # ★ ревизия savedir6, МЕЛК-2: пропуск «уровня нет» — законное состояние карты
    _only = os.environ.get("ASDA_MAPS", "")
    if _only: MAPS = [m for m in MAPS if m in _only.split("||")]
    for mp in MAPS:
        if not EAL.does_asset_exist(_U.level_path(mp)):
            unreal.log("FXALL пропуск (нет уровня): %s" % mp); skipped += 1; continue
        # ★★★ 16.09.2026 (ревизия savedir5, СРЕДН-2). ДРАЙВЕР САМ СНОСИТ ПАПКИ — значит и гейт нужен здесь.
        #   Раньше единственная проверка стояла внутри asda_place_effects.py, то есть ПОСЛЕ delete_directory:
        #   заблокированная карта теряла EffectTex/EffectMat, а раскатка молча шла дальше (с тех пор как драйвер
        #   ловит SystemExit — даже без обрыва). Проверяем обе вещи: сцену логина и имя карты в реестре.
        if _LG.login_map_write_blocked(_U.level_path(mp), "asda_place_effects_all.py") or \
           _LG.map_name_blocked("/Game/Map/%s" % _asset(mp), "asda_place_effects_all.py"):
            failed += 1
            continue
        for sub in ("EffectTex", "EffectMat"):
            d = "/Game/Map/%s/%s" % (_asset(mp), sub)
            if EAL.does_directory_exist(d): EAL.delete_directory(d)
        unreal.log("=== FXALL: %s ===" % mp)
        os.environ["ASDA_MAP"] = mp
        try:
            _pf = os.path.join(TOOLS, "asda_place_effects.py")
            exec(compile(open(_pf, encoding="utf-8").read(), _pf, "exec"),
                 {"__name__": "__main__", "__file__": _pf})   # __file__ нужен jem-импорту
        except (Exception, SystemExit) as e:
            # ★ 16.09.2026 (ревизия savedir4, МЕЛК-4): гейты инструмента (сцена логина, сверка имени карты)
            #   останавливаются через SystemExit(3), а он BaseException — «except Exception» его не ловил,
            #   и одно заблокированное имя оборвало бы всю раскатку эффектов по картам.
            failed += 1            # ★ ревизия savedir5, СРЕДН-2: провал должен доезжать до итоговой строки
            unreal.log_error("FXALL %s FAIL: %s" % (mp, e))
        try: unreal.SystemLibrary.collect_garbage()
        except Exception: pass
    # ★ ревизия savedir6, МЕЛК-2: «карт=N» без пропусков читалось как «обработаны все N».
    unreal.log("=== FXALL ЗАВЕРШЕНО: обработано=%d, ПРОВАЛОВ=%d, пропущено=%d, всего=%d ==="
               % (len(MAPS) - failed - skipped, failed, skipped, len(MAPS)))


# ★ ЗАКОН [[tools-no-import-side-effects]]: при импорте модуль НИЧЕГО не делает.
#   `__spec__` выставляет ЛЮБАЯ машинерия импорта (import / importlib / runpy) и НЕ
#   выставляет прямое исполнение файла (python file.py, UE -ExecutePythonScript).
#   `python -m` даёт `__name__ == "__main__"` и покрыт первым условием.
if __name__ == "__main__" or globals().get("__spec__") is None:
    main()
