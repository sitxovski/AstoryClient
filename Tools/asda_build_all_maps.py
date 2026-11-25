# Copyright (c) 2026 @sitxovski (Discord)
# asda_build_all_maps.py — батч сборки ВСЕХ карт в одной UE-сессии.
# Для каждой карты: build_map_level (террейн/объекты/материалы/вода/небо + Lit-свет) + build_map_anim (анимации).
# Каждый скрипт exec'ится в СВЕЖЕМ namespace (фрешь кэши/MAP_NAME из env). Пропуск уже собранных (resume).
#
# ★ Фазы (env ASDA_PHASE):
#   all  (по умолчанию) — полная сборка уровня + анимации. Lit-состояние (Lit-мастера, Movable Sun/Sky,
#         PPV) ставит САМ asda_build_map_level.py через asda_convert_map_lit.main() — Д-6: переимпорт
#         больше не откатывает свет на unlit/Stationary. ⚠ Полная сборка ПЕРЕСОЗДАЁТ уровень: эффекты,
#         NPC, звуки, BGM после неё нужно расставлять заново (asda_place_*_all, asda_build_npc_spawn…).
#   anim — только перепривязка анимаций (как раньше).
#   mesh — ★ Д-2: пересобрать ТОЛЬКО ассеты мешей (Models+Terrain: авторские нормали, вершинный цвет
#          террейна, Lit-мастер террейна) свежими; уровень не грузится и не меняется. Требует уровень.
#   lit  — только Lit-состояние уровня (asda_convert_map_lit.main) для уже собранных карт.
import os, re, io, unreal
import os as _os, sys as _sys
_sys.path.insert(0, _os.path.dirname(_os.path.abspath(__file__)))
import asda_maps as _MAPS
import asda_ue_paths as _U   # ★ волна 2 п. 7 (14.09.2026): корни и правила путей — реестр (Tools/data/asda_asset_paths.json)
L = unreal.log
EAL = unreal.EditorAssetLibrary
TOOLS = r"@@ASTORY_DEV_ROOT_FWD@@/AstoryClient/Tools"
_asset = _MAPS.map_asset_name  # ★ единый санитайзер имени карты (asda_maps.map_asset_name)

# ★ Список карт — ЕДИНЫЙ источник asda_maps.py (все 22 карты порта).
#   Было: локальный литерал (13 копий по тулам). Состав сверен 1:1.
MAPS = _MAPS.ALL_MAPS
BATCH_MISS = []        # ★ имена из ASDA_MAPS, которых нет среди карт (ревизия savedir12, МЕЛК-2).
                       #   Объявлено ДО использования: в первой версии стояло ниже, и при заданном
                       #   ASDA_MAPS модуль падал бы с NameError ещё до работы.
_only = os.environ.get("ASDA_MAPS", "")                      # "A||B" = собрать только эти карты
if _only:
    sel = [s.strip() for s in _only.split("||") if s.strip()]
    MAPS = [m for m in MAPS if m in sel]
    # ★ 16.09.2026 (ревизия savedir12, МЕЛК-2): то же, что уже чинили в конвертере — опечатка в ASDA_MAPS
    #   молча уменьшала охват, и батч отчитывался «ПРОВАЛОВ=0».
    _miss = [s for s in sel if s not in MAPS]
    if _miss:
        BATCH_MISS.extend(_miss)
FORCE_ALL = os.environ.get("ASDA_BATCH_FORCE", "0") == "1"   # 1 = пересобрать даже готовые

def run(script, mapname, **env):
    for k, v in env.items():
        os.environ[k] = v
    path = os.path.join(TOOLS, script)
    # ★ 03.09.2026: в globals ОБЯЗАТЕЛЕН `__file__` — запускаемые скрипты вычисляют по нему путь к
    #   Tools (для sys.path и соседних модулей). Без него батч падал на КАЖДОЙ карте с
    #   «name '__file__' is not defined», при этом счётчик писал «КАРТА ГОТОВА» (см. ниже).
    g = {"__name__": "__main__", "__file__": path}
    exec(compile(io.open(path, encoding="utf-8").read(), path, "exec"), g)

def main():
    """Батч сборки карт. Вынесен в функцию — при импорте модуль НИЧЕГО не делает.

    ★★★ ЗАКОН [[tools-no-import-side-effects]], аудит №4 (волна 3). Здесь весь батч стоял ГОЛЫМ
      на уровне модуля: любой `import asda_build_all_maps` запускал полную пересборку ВСЕХ 22
      карт через `exec` дочерних скриптов — самый дорогой сайд-эффект во всём `Tools/`
      (второй по тяжести после `asda_place_effects_all.py`).
    """
    done = 0; skipped = 0
    failed = 0
    PHASE = os.environ.get("ASDA_PHASE", "all")               # all | anim | mesh | lit (см. шапку)
    for mp in MAPS:
        lvl = _U.level_path(mp)
        if PHASE in ("mesh", "lit"):
            # фазы «поверх готового»: нужен уровень; ASDA_BATCH_FORCE не участвует
            if not EAL.does_asset_exist(lvl):
                L("=== ПРОПУСК (уровня нет): %s ===" % mp); skipped += 1; continue
            L("=== ФАЗА %s: %s ===" % (PHASE, mp))
            try:
                if PHASE == "mesh":
                    os.environ["ASDA_FORCE"] = "1"
                    os.environ.pop("ASDA_LIMIT", None)
                    run("asda_build_map_level.py", mp, ASDA_MAP=mp, ASDA_MESH_ONLY="1")
                else:
                    run("asda_convert_map_lit.py", mp, ASDA_MAP=mp)
            # ★ 16.09.2026 (ревизия savedir4, МЕЛК-4): SystemExit — это BaseException, и «except Exception»
            #   его НЕ ловит; гейты инструментов (сцена логина, сверка имени карты) останавливаются именно им.
            except (Exception, SystemExit) as e:
                # ★ 03.09.2026: провал больше НЕ считается успехом. Раньше счётчик писал
                #   «КАРТА ГОТОВА» и «собрано=21» даже когда падали все 21 — отчёт врал.
                import traceback
                L("%s %s FAIL: %s" % (PHASE.upper(), mp, e))
                L(traceback.format_exc()[:1500])
                failed += 1
                try: unreal.SystemLibrary.collect_garbage()
                except Exception: pass
                continue
            try: unreal.SystemLibrary.collect_garbage()
            except Exception: pass
            L("=== КАРТА ГОТОВА: %s (%d/%d) ===" % (mp, done + 1, len(MAPS)))
            done += 1
            continue
        if not FORCE_ALL and EAL.does_asset_exist(lvl):
            L("=== ПРОПУСК (уже собрана): %s ===" % mp); skipped += 1; continue
        L("=== СБОРКА КАРТЫ: %s ===" % mp)
        if PHASE != "anim":
            os.environ["ASDA_FORCE"] = "1"
            os.environ.pop("ASDA_SKIP_IMPORT", None)
            os.environ.pop("ASDA_LIMIT", None)
            os.environ.pop("ASDA_MESH_ONLY", None)
            try:
                run("asda_build_map_level.py", mp, ASDA_MAP=mp)
            except (Exception, SystemExit) as e:          # ★ 16.09.2026 (ревизия savedir4, МЕЛК-4): SystemExit гейтов
                # ★ 16.09.2026 (ревизия savedir5, СРЕДН-1): провал уровня — это ПРОВАЛ КАРТЫ. Без счётчика и
                #   `continue` карта печаталась как «ГОТОВА», итог врал «ПРОВАЛОВ=0», а сборка анимаций шла по
                #   несобранному уровню. Ветка фаз mesh/lit так поступает с 03.09 — приводим к ней.
                import traceback
                L("LEVEL %s FAIL: %s" % (mp, e))
                L(traceback.format_exc()[:1500])
                failed += 1
                try: unreal.SystemLibrary.collect_garbage()
                except Exception: pass
                continue
        try:
            # anim-only: обычно SKIP_IMPORT=1 (перепривязка). ASDA_ANIM_REIMPORT=1 → реимпорт нового glb (геометрия изменилась).
            skip = "1" if (PHASE == "anim" and os.environ.get("ASDA_ANIM_REIMPORT", "0") != "1") else "0"
            run("asda_build_map_anim.py", mp, ASDA_MAP=mp, ASDA_SKIP_IMPORT=skip)
        except (Exception, SystemExit) as e:              # ★ 16.09.2026 (ревизия savedir4, МЕЛК-4): SystemExit гейтов
            # ★ 16.09.2026 (ревизия savedir5, СРЕДН-1): то же и для анимаций — «ГОТОВА» печатается только когда
            #   обе части прошли; иначе отчёт врёт, а карту потом никто не пересоберёт.
            import traceback
            L("ANIM %s FAIL: %s" % (mp, e))
            L(traceback.format_exc()[:1500])
            failed += 1
            try: unreal.SystemLibrary.collect_garbage()
            except Exception: pass
            continue
        try: unreal.SystemLibrary.collect_garbage()
        except Exception: pass
        L("=== КАРТА ГОТОВА: %s (%d/%d) ===" % (mp, done + 1, len(MAPS)))
        done += 1

    if BATCH_MISS:
        L("=== БАТЧ: имена из ASDA_MAPS не найдены среди карт: %s (разделитель — «||») ===" % ", ".join(BATCH_MISS))
        failed += len(BATCH_MISS)
    L("=== БАТЧ ЗАВЕРШЁН: собрано=%d, ПРОВАЛОВ=%d, пропущено=%d, всего=%d ===" % (done, failed, skipped, len(MAPS)))


# ★ ЗАКОН [[tools-no-import-side-effects]]: при импорте модуль НИЧЕГО не делает.
#   `__spec__` выставляет ЛЮБАЯ машинерия импорта (import / importlib / runpy) и НЕ
#   выставляет прямое исполнение файла (python file.py, UE -ExecutePythonScript).
#   `python -m` даёт `__name__ == "__main__"` и покрыт первым условием.
# ★ Прежняя строка `main = None` («некоторые exec'нутые скрипты определяют main(); не вызываем
#   повторно») больше не нужна: дочерние скрипты exec'ятся в СВОЁМ словаре globals, до этого
#   модуля их `main` не доходит, а свой батч теперь и есть `main()`.
if __name__ == "__main__" or globals().get("__spec__") is None:
    main()
