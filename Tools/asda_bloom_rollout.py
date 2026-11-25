# -*- coding: utf-8 -*-
# Copyright (c) 2026 @sitxovski (Discord)
# asda_bloom_rollout.py — раскатка КАЛИБРОВКИ ОРЕОЛА (bloom) на существующие PP-волюмы всех карт.
#
# ★★★ ЧИСЛА ВЫБРАНЫ ВЛАДЕЛЬЦЕМ 09.09.2026 — вариант `v3_rec` калибровочного ряда
#   (Reports/bloom_calibration_2026-09-09.md §3): интенсивность 4.0, порог 2.5.
#   Здесь они НЕ ЗАШИТЫ: читаются из [AsdaGlow] Config/DefaultScalability.ini (ступень 4) — тот же
#   единственный источник, из которого их берёт C++ (UAsdaVideoSettingsSubsystem::SetGlowLevel),
#   закон port-shared-points. Радиус ореола (BloomSizeScale) и блики (LensFlare*) НЕ ТРОГАЮТСЯ.
#
# ПОЧЕМУ ОТДЕЛЬНЫЙ СКРИПТ, А НЕ ПЕРЕГЕНЕРАЦИЯ asda_convert_map_lit.py: там блок PostProcess стоит под
#   `if ppv is None` («существующий PP_<карта> не трогаем», строки ~975-976), а волюмы на картах давно
#   созданы ⇒ перегенерация не изменила бы НИЧЕГО. Этот проход именно «обновить существующий волюм».
#   Значения в генератор всё равно вписаны (для карт, которые будут создаваться с нуля).
#
# ИДЕМПОТЕНТНОСТЬ: уровень, у которого обе ручки уже стоят с нужными числами и включёнными
#   override-флагами, НЕ перезаписывается и НЕ сохраняется (второй прогон печатает «без изменений»).
#
# БЭКАП: копия .umap до правки со сверкой размера и MD5 — ★ Т3 (11.10.2026) в @@ASTORY_DEV_ROOT@@\_backup\<дата>_bloom_rollout\
#   AstoryClient\Content\... (раскат 09.09 лежит в прежнем AstoryClient\_backup\2026-09-09_bloom_rollout — откат по умолчанию оттуда).
#   Существующий бэкап не перетирается (первый прогон = состояние ДО раската).
#
# Запуск (раскат, в headless-редакторе):
#   python Tools/run_headless_safe.py Tools/asda_bloom_rollout.py --marker "BLOOM-ROLLOUT-FINISHED" --timeout 3600
# Откат (БЕЗ редактора, обычный python — редактор при этом должен быть закрыт):
#   python Tools/asda_bloom_rollout.py --revert
# Переменные окружения:
#   ASDA_BLOOM_MAPS="alpen||nightvalley"  — только эти карты (по умолчанию все)
#   ASDA_BLOOM_DRYRUN=1                   — только показать, что изменилось бы (ничего не пишет)
#   ASDA_BLOOM_REVERT_FROM=<папка набора> — (★ Т3) откат из этого набора вместо прежнего 2026-09-09_bloom_rollout
import hashlib
import os
import shutil
import stat
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
# ★ 16.09.2026 (обратный обход savedir14): единый гейт сцены логина. Модуль при импорте берёт только os/re
#   и редактора не требует — годится и для режима --revert, который запускается обычным python.
import asda_login_guard as _LG

try:
    import unreal
except ImportError:                       # режим отката запускается обычным python, без редактора
    unreal = None

PROJ = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
# ★ Т3 (11.10.2026): НОВЫЕ копии — в единый корень, набор @@ASTORY_DEV_ROOT@@\_backup\<дата запуска>_bloom_rollout (make_backup).
BACKUP_SET = "bloom_rollout"
# Источник ОТКАТА (только чтение): по умолчанию прежний набор раската 09.09 — состояние ДО раската, старые наборы не
#   переносятся; ASDA_BLOOM_REVERT_FROM=<папка набора> — откат из другого набора (в т.ч. нового формата, путь от @@ASTORY_DEV_ROOT@@).
BACKUP = os.environ.get("ASDA_BLOOM_REVERT_FROM") or os.path.join(PROJ, "_backup", "2026-09-09_bloom_rollout")
# Корень @@ASTORY_DEV_ROOT@@ — для наборов нового формата (путь копии внутри набора — от него)
AREA_ROOT = os.path.dirname(PROJ)                                       # <ROOT>
INI = os.path.join(PROJ, "Config", "DefaultScalability.ini")
LEVEL = 4                                 # ступень «уровень свечения = 4» = калибровка владельца
ONLY = [s for s in os.environ.get("ASDA_BLOOM_MAPS", "").split("||") if s]
DRYRUN = os.environ.get("ASDA_BLOOM_DRYRUN", "0") == "1"
MARKER = "BLOOM-ROLLOUT-FINISHED"         # ASCII-маркер успеха для run_headless_safe.py


def log(msg):
    if unreal:
        unreal.log("[BLOOM] %s" % msg)
    else:
        print("[BLOOM] %s" % msg)


def warn(msg):
    if unreal:
        unreal.log_warning("[BLOOM] %s" % msg)
    else:
        print("[BLOOM][!] %s" % msg)


def md5(path):
    """MD5 файла — общий `asda_headless_tool.md5_file` (дедуп №2 Т19; ленивый импорт, как у make_backup)."""
    import asda_headless_tool as HT
    return HT.md5_file(path)


def read_step(level):
    """Пара (интенсивность, порог) ступени из [AsdaGlow] — единственный источник чисел."""
    want = {"intensity@%d" % level: None, "threshold@%d" % level: None}
    section = False
    with open(INI, "r", encoding="utf-8-sig", errors="replace") as f:
        for line in f:
            s = line.strip()
            if s.startswith("["):
                section = (s.lower() == "[asdaglow]")
                continue
            if not section or "=" not in s or s.startswith((";", "#")):
                continue
            k, _, v = s.partition("=")
            k = k.strip().lower()
            if k in want:
                want[k] = float(v.strip())
    if None in want.values():
        raise RuntimeError("в [AsdaGlow] %s нет Intensity@%d/Threshold@%d" % (INI, level, level))
    return want["intensity@%d" % level], want["threshold@%d" % level]


def make_backup(umap):
    """Копия уровня ДО правки со сверкой размера и MD5 — `asda_headless_tool.backup_file(verify="md5")` (★ Т3, 11.10.2026):
    набор @@ASTORY_DEV_ROOT_ESC@@\\_backup\\<дата запуска>_bloom_rollout\\<путь от @@ASTORY_DEV_ROOT_ESC@@> (был прошитый
    AstoryClient\\_backup\\2026-09-09_bloom_rollout). Существующую копию не перетираем. Возвращает строку для журнала."""
    import asda_headless_tool as HT
    msgs = []
    dst = HT.backup_file(umap, BACKUP_SET, verify="md5", log=msgs.append)
    if dst is None:
        raise RuntimeError("нет файла уровня для бэкапа: %s" % umap)
    return msgs[-1] if msgs else dst


def revert():
    """Откат: вернуть уровни из _backup поверх Content. Редактор должен быть ЗАКРЫТ."""
    if not os.path.isdir(BACKUP):
        warn("откат: папки %s нет — откатывать нечего" % BACKUP)
        return 1
    n = 0
    for root, _dirs, files in os.walk(BACKUP):
        for fn in files:
            if not fn.lower().endswith(".umap"):
                continue
            src = os.path.join(root, fn)
            rel = os.path.relpath(src, BACKUP)
            # ★ Т3: набор нового формата хранит путь от @@ASTORY_DEV_ROOT@@ (AstoryClient\Content\…), прежний — от проекта (Content\…)
            parts = rel.replace("/", os.sep).split(os.sep)
            low = [p.lower() for p in parts[:2]]
            if low == ["port", "asdareborn"]:             # набор прежней раскладки: port\AsdaReborn\Content\…
                dst = os.path.join(PROJ, *parts[2:])
            elif low[:1] == ["astoryclient"]:             # набор нового формата: AstoryClient\Content\… (от ROOT)
                dst = os.path.join(PROJ, *parts[1:])
            else:                                         # прежний формат: от проекта (Content\…)
                dst = os.path.join(PROJ, rel)
            # ★★★ 16.09.2026 (МЕЛК-1 ревизии волны гейтов). Это копирование идёт МИМО редактора: ни делегаты
            #   сохранения, ни обёртка файловой системы UE, ни Python-гейты инструментов его не видят. Проверяем
            #   КАЖДЫЙ файл назначения сами — тем же приёмом, что драйверы сетов A–E (guarded_copy).
            # ★ 16.09.2026 (СРЕДН-2 финальной ревизии): откат копирует ТОЛЬКО *.umap, то есть САМИ УРОВНИ,
            #   а уровень сцены логина открывает ASDA_ALLOW_LOGIN_MAP_WRITE — не «ассетный» ASDA_ALLOW_LOGIN_WRITE.
            #   Прежняя проверка спрашивала слабый ключ, и он в одиночку перезаписал бы Login.umap бэкапом.
            if _LG.login_map_write_blocked(dst, "asda_bloom_rollout.py --revert"):
                warn("откат ПРОПУЩЕН для %s: это уровень сцены логина со светом владельца" % os.path.relpath(dst, PROJ))
                continue
            shutil.copy2(src, dst)
            if os.path.getsize(dst) != os.path.getsize(src) or md5(dst) != md5(src):
                raise RuntimeError("откат %s не сошёлся по размеру/MD5" % dst)
            n += 1
            log("откат: %s" % os.path.relpath(dst, PROJ))
    log("откат завершён: уровней возвращено %d" % n)
    return 0


def umap_for(package):
    """/Game/Map/alpen/alpen → <проект>/Content/Map/alpen/alpen.umap"""
    return os.path.join(PROJ, "Content", package[len("/Game/"):].replace("/", os.sep) + ".umap")


def rollout():
    inten, thr = read_step(LEVEL)
    log("ступень %d из [AsdaGlow]: интенсивность=%.3f порог=%.3f (выбор владельца 09.09.2026, v3_rec)"
        % (LEVEL, inten, thr))
    ar = unreal.AssetRegistryHelpers.get_asset_registry()
    les = unreal.get_editor_subsystem(unreal.LevelEditorSubsystem)
    eas = unreal.get_editor_subsystem(unreal.EditorActorSubsystem)

    packages = sorted(str(ad.package_name) for ad in ar.get_assets_by_path("/Game/Map", recursive=True)
                      if str(ad.asset_class_path.asset_name) == "World")
    changed = skipped = novol = 0
    failed = []          # уровни, у которых сохранение не прошло (файл не изменился)
    for pkg in packages:
        name = pkg.split("/")[-1]
        # ★★★ ЗАЩИТА СВЕТА ВЛАДЕЛЬЦА. ★ 16.09.2026 (обратный обход savedir14): БЫЛ пропуск ЛИТЕРАЛОМ
        #   (name == "Login" + ASDA_ALLOW_LOGIN_WRITE). Литерал не узнаёт ни /Game/Map/Login/<что угодно>,
        #   ни прочие пакеты сцены логина, и спрашивал НЕ ТУ переменную: сам уровень открывает только
        #   ASDA_ALLOW_LOGIN_MAP_WRITE=1 (раздел 17). Теперь — общий гейт по единому списку пакетов логина,
        #   ДО load_level и до save_current_level. Без quiet: пропуск в обходе всех уровней должен быть виден
        #   в логе, как и у прежнего литерала.
        if _LG.login_map_write_blocked(pkg, "asda_bloom_rollout.py"):
            continue
        if ONLY and name not in ONLY:
            continue
        umap = umap_for(pkg)
        if not os.path.isfile(umap):
            warn("%s: файла уровня нет (%s) — пропуск" % (name, umap))
            continue
        les.load_level(pkg)
        vols = [a for a in eas.get_all_level_actors()
                if isinstance(a, unreal.PostProcessVolume) and a.get_actor_label().startswith("PP_")]
        if not vols:
            # У сцены логина PP-волюма нет ВОВСЕ (проверено 09.09) — ей ореол даёт служебный PP_Glow
            #   из C++, уровень мы не трогаем. Здесь это не ошибка, а ожидаемое состояние.
            log("%s: PP-волюмов нет — уровень не трогаю" % name)
            novol += 1
            continue
        need = []
        for v in vols:
            s = v.get_editor_property("settings")
            same = (bool(s.get_editor_property("override_bloom_intensity"))
                    and bool(s.get_editor_property("override_bloom_threshold"))
                    and abs(float(s.get_editor_property("bloom_intensity")) - inten) < 1e-4
                    and abs(float(s.get_editor_property("bloom_threshold")) - thr) < 1e-4)
            if not same:
                need.append(v)
        if not need:
            log("%s: уже %.3f/%.3f на %d волюме(ах) — без изменений" % (name, inten, thr, len(vols)))
            skipped += 1
            continue
        if DRYRUN:
            log("%s: ИЗМЕНИЛОСЬ БЫ на %d волюме(ах) (dry-run)" % (name, len(need)))
            changed += 1
            continue
        log("%s: %s" % (name, make_backup(umap)))
        before = md5(umap)
        # Perforce держит файлы read-only; без снятия флага save_current_level молча не запишет
        #   (тот же приём, что в asda_convert_map_lit._level_writable).
        if not os.access(umap, os.W_OK):
            os.chmod(umap, stat.S_IWRITE | stat.S_IREAD)
            log("%s: снят read-only" % name)
        for v in need:
            s = v.get_editor_property("settings")
            # ★★★ Калибровка владельца 09.09.2026 (v3_rec). Ставим РОВНО две ручки: вес ореола и отсечку.
            #   Радиус (bloom_size_scale) и блики (lens_flare_*) не переопределяем — владелец решил «как есть».
            s.set_editor_property("override_bloom_intensity", True)
            s.set_editor_property("bloom_intensity", inten)
            s.set_editor_property("override_bloom_threshold", True)
            s.set_editor_property("bloom_threshold", thr)
            v.set_editor_property("settings", s)
        les.save_current_level()
        # ★★★ САМОПРОВЕРКА ИЗ ФАЙЛА, А НЕ ИЗ ПАМЯТИ. Грабля прогона 09.09.2026: у alpen сохранение упало
        #   («MoveFile … Error Code 32» = файл занят другим процессом, LogSavePackage: Error saving), а
        #   проверка по АКТОРУ этого не заметила — актор в памяти был правильный, и скрипт отрапортовал
        #   «записано». Живой прогон игры потом показал в волюме старые 0.35/−1. Поэтому сначала — что
        #   БАЙТЫ УРОВНЯ ИЗМЕНИЛИСЬ, и только потом значения из актора.
        for attempt in (1, 2, 3):
            if md5(umap) != before:
                break
            warn("%s: попытка %d — файл уровня НЕ изменился (сохранение не прошло), повторяю" % (name, attempt))
            if not os.access(umap, os.W_OK):
                os.chmod(umap, stat.S_IWRITE | stat.S_IREAD)
            les.save_current_level()
        if md5(umap) == before:
            unreal.log_error("[BLOOM] %s: СОХРАНЕНИЕ НЕ ПРОШЛО — файл уровня не изменился (см. LogSavePackage выше)" % name)
            failed.append(name)
            continue
        bad = []
        for v in [a for a in eas.get_all_level_actors()
                  if isinstance(a, unreal.PostProcessVolume) and a.get_actor_label().startswith("PP_")]:
            s = v.get_editor_property("settings")
            gi = float(s.get_editor_property("bloom_intensity"))
            gt = float(s.get_editor_property("bloom_threshold"))
            if abs(gi - inten) > 1e-4 or abs(gt - thr) > 1e-4:
                bad.append("%s=%.3f/%.3f" % (v.get_actor_label(), gi, gt))
        if bad:
            unreal.log_error("[BLOOM] %s: после записи волюм отдаёт не то — %s" % (name, ", ".join(bad)))
        else:
            log("%s: записано и сверено из актора — %d волюм(ов) = %.3f/%.3f"
                % (name, len(vols), inten, thr))
        changed += 1
    log("итог: обновлено %d, уже было %d, без PP-волюма %d, НЕ СОХРАНИЛОСЬ %d, всего уровней %d"
        % (changed, skipped, novol, len(failed), len(packages)))
    if failed:
        unreal.log_error("[BLOOM] уровни, где сохранение не прошло: %s" % ", ".join(failed))
    else:
        unreal.log(MARKER)


def _script_main():
    """Работа скрипта. Вынесена в функцию, чтобы импорт модуля НИЧЕГО не делал
    (закон [[tools-no-import-side-effects]])."""
    if "--revert" in sys.argv or os.environ.get("ASDA_BLOOM_MODE", "") == "revert":
        sys.exit(revert())
    if unreal is None:
        print("нужен редактор: python Tools/run_headless_safe.py Tools/asda_bloom_rollout.py "
              "--marker \"%s\"" % MARKER)
        sys.exit(2)
    rollout()


# ★ ЗАКОН [[tools-no-import-side-effects]]: при импорте модуль НИЧЕГО не делает.
#   `__spec__` выставляет ЛЮБАЯ машинерия импорта (import / importlib / runpy) и НЕ
#   выставляет прямое исполнение файла (python file.py, UE -ExecutePythonScript).
#   `python -m` даёт `__name__ == "__main__"` и покрыт первым условием.
if __name__ == "__main__" or globals().get("__spec__") is None:
    _script_main()
