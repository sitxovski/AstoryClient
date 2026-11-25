# -*- coding: utf-8 -*-
# Copyright (c) 2026 @sitxovski (Discord)
# asda_revert_fx_boost.py — ОТКАТ раската волны Ф1 одной командой: вернуть эмиттеры на ИСХОДНЫЕ MIC
#   и удалить созданные MIC-варианты `*_fxb*`.
#
# ПОЧЕМУ ОТКАТ ВООБЩЕ ВОЗМОЖЕН ОДНОЙ КОМАНДОЙ. Генератор `asda_apply_fx_boost.py` исходные MIC
#   НИКОГДА не изменяет: он ДУБЛИРУЕТ их в `<имя>_fxb<буст×100>[s<дист>]` и перенацеливает ссылку
#   эмиттера. Значит имя исходника восстанавливается из имени варианта отрезанием суффикса `_fxb…`,
#   и других следов раската нет.
#
# РЕЖИМЫ (ASDA_FXR_MODE):
#   report (ДЕФОЛТ) — ничего не пишет, только считает: сколько ссылок и вариантов будет тронуто.
#   revert          — возвращает ссылки, сохраняет BP и уровни, удаляет варианты.
#
# ОХВАТ (для проверки отката на одном эффекте):
#   ASDA_FXR_ONLY="jeb1,jeb2"          — только эти .jeb (пусто = все);
#   ASDA_FXR_MAPS="alpen,Login"        — какие уровни обходить (дефолт = ВСЕ ключи MAPS, "none" = ни одного);
#   ASDA_FXR_KEEP=1                    — ссылки вернуть, но варианты НЕ удалять
#                                        (нужно, когда откатывается лишь часть эффектов и остальные
#                                         продолжают на эти варианты ссылаться).
#
# Запуск (полный откат):
#   set ASDA_FXR_MODE=revert
#   python Tools/run_headless_safe.py Tools/asda_revert_fx_boost.py --marker "FX-REVERT ЗАВЕРШЕНО"
import json
import os
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import asda_fx_boost as FXB   # только ради jeb_key: единая нормализация имён .jeb
import unreal
import os as _fxg_os, sys as _fxg_sys
_fxg_sys.path.insert(0, _fxg_os.path.dirname(_fxg_os.path.abspath(__file__)))
import asda_fx_common as _FXC_GATE   # ★ Ф16: гейт записи эмиттеров (импорт без побочных эффектов)

EAL = unreal.EditorAssetLibrary

BP_DIR = "/Game/AsdaAssets/Effects"
MAT_DIR = "/Game/AsdaAssets/Effects/Mat"
# ★ ЧЕТВЁРТАЯ ПАПКА ВАРИАНТОВ (09.09.2026): материалы эффектов сцены логина создавал не общий
#   asda_place_effects, а asda_place_charselect_effect/glyph, и они кладут MIC в ДРУГОЙ каталог.
#   Вариант создаётся рядом с исходником, значит откат обязан подметать и его — иначе после
#   отката в проекте остались бы висячие `*_fxb*` логина.
TEX_MAT_DIR = "/Game/AsdaAssets/Textures/Effects/Mat"
# ★ Тот же расширенный список карт, что у генератора (см. asda_apply_fx_boost.MAPS): откат обязан
#   покрывать РОВНО то, что покрывает раскат, иначе часть ссылок останется на вариантах.
#   ★ дедуп №2 В1 Т14 (11.10.2026): теперь это буквально ОДНА таблица — `asda_fx_boost.effect_maps()` (из реестра карт).
MAPS = FXB.effect_maps()
# ★★★ ЗАЩИТА СВЕТА ВЛАДЕЛЬЦА (13.09.2026): уровень логина без ASDA_ALLOW_LOGIN_WRITE=1 не трогаем и не сохраняем
#   ★ 16.09.2026 (раздел 17): с разрешением save_map уровня Login в автоматике пройдёт, только если задана ещё
#   ASDA_ALLOW_LOGIN_MAP_WRITE=1 (C++-сторож AsdaLoginSaveGuard; login_save_guard.md п. 6.3). Сообщения здесь нет — пропуск молча.
from asda_login_guard import login_write_allowed   # единая проверка разрешения (без побочных эффектов)
if not login_write_allowed():
    MAPS.pop("Login", None)
# Куда класть план прогона. Переопределяется ASDA_FXR_OUT, чтобы новый прогон не затирал улики прошлого.
OUT = os.environ.get("ASDA_FXR_OUT", r"@@ASTORY_DEV_ROOT_FWD@@/Reports/fx_f1/fx_revert_plan.json")

MODE = os.environ.get("ASDA_FXR_MODE", "report")
ONLY = {FXB.jeb_key(x) for x in os.environ.get("ASDA_FXR_ONLY", "").split(",") if x.strip()}
# ★ 04.11.2026 (яркость эффектов навыков): ASDA_FXR_ONLY_FILE=<файл со списком через запятую> — тот же охват, когда список длиннее
#   предела переменной окружения (32767 символов; раскат 1235 эффектов). Добавляется к ASDA_FXR_ONLY.
if os.environ.get("ASDA_FXR_ONLY_FILE"):
    ONLY |= {FXB.jeb_key(x) for x in open(os.environ["ASDA_FXR_ONLY_FILE"], encoding="utf-8").read().split(",") if x.strip()}
MAP_KEYS = [x.strip() for x in os.environ.get("ASDA_FXR_MAPS", ",".join(MAPS)).split(",") if x.strip()]
KEEP = os.environ.get("ASDA_FXR_KEEP", "") == "1"


def origin_of(variant_name):
    """Имя исходного MIC по имени варианта: всё до первого `_fxb`.

    ★ Суффикс ставит `asda_apply_fx_boost.variant_name` и только он; `_fxb` в исходных именах
      MIC не встречается (имена вида `MI_<текстура>_<режим>`)."""
    i = variant_name.find("_fxb")
    return variant_name[:i] if i > 0 else None


def want(jeb):
    """Попадает ли эффект в текущий охват (ASDA_FXR_ONLY)."""
    return (not ONLY) or (FXB.jeb_key(jeb) in ONLY)


def process_owner(owner, jeb, plan, dry):
    """Вернуть эмиттеры носителя на исходные MIC. Возвращает True, если носитель менялся.

    ★ ГРАБЛЯ Python-UE: перебор `unreal.Array` структур отдаёт КОПИИ — элемент кладётся обратно
      по индексу, и массив целиком записывается назад (та же грабля, что в генераторе)."""
    ems = list(owner.get_editor_property("emitters"))
    changed = False
    for idx in range(len(ems)):
        e = ems[idx]
        touched = False
        for field in ("material", "material_b"):
            mic = e.get_editor_property(field)
            if not mic:
                continue
            name = mic.get_name()
            if "_fxb" not in name:
                continue
            orig = origin_of(name)
            # ★ Исходник лежит В ТОЙ ЖЕ ПАПКЕ, что и вариант (генератор дублирует на месте), а папок
            #   три: общая Effects/Mat и по одной у каждой карты (Map/<карта>/EffectMat). Жёстко
            #   зашитый MAT_DIR давал «исходник не найден» ровно на 15 ссылках карт (сухой прогон).
            folder = mic.get_path_name().split(".")[0].rsplit("/", 1)[0]
            path = "%s/%s" % (folder, orig)
            if not EAL.does_asset_exist(path):
                plan["исходник_не_найден"].append("%s / %s -> %s" % (jeb, name, path))
                continue
            plan["ссылок"].append("%s / %s -> %s" % (jeb, name, orig))
            if not dry:
                e.set_editor_property(field, EAL.load_asset(path))
                touched = True
        if touched:
            ems[idx] = e
            changed = True
    if changed:
        # ★★★ Ф16 (К-1, 20.09.2026): запись массива эмиттеров — под явным гейтом
        #   `asda.Fx.Glow.EmittersWriter` (обоснование — asda_fx_common.emitters_writer_gate):
        #   признак «пишет скрипт» не должен зависеть от формы события правки.
        with _FXC_GATE.emitters_writer_gate():
            owner.set_editor_property("emitters", ems)
    return changed


def run_blueprints(plan, dry):
    """Обход запечённых BP_Fx_*."""
    names = [n for n in EAL.list_assets(BP_DIR, False, False) if "/BP_Fx_" in n]
    touched = 0
    for n in names:
        path = n.split(".")[0]
        jeb = path.rsplit("/BP_Fx_", 1)[-1]
        if not want(jeb):
            continue
        bp = EAL.load_asset(path)
        if not bp:
            continue
        cdo = unreal.get_default_object(bp.generated_class())
        real_jeb = str(cdo.get_editor_property("effect_name")) or jeb
        if process_owner(cdo, real_jeb, plan, dry) and not dry:
            bp.modify()
            if not EAL.save_loaded_asset(bp, False):
                raise RuntimeError("BP не сохранился: %s" % path)
            touched += 1
    unreal.log("[FX-REVERT] BP просмотрено %d, возвращено %d" % (len(names), touched))


def run_levels(plan, dry):
    """Обход эффект-акторов, размещённых на уровнях карт."""
    for key in MAP_KEYS:
        if key == "none" or key not in MAPS:
            continue
        unreal.EditorLoadingAndSavingUtils.load_map(MAPS[key])
        world = unreal.EditorLevelLibrary.get_editor_world()
        acts = unreal.GameplayStatics.get_all_actors_of_class(world, unreal.AsdaEffectActor)
        touched = 0
        for a in acts:
            jeb = str(a.get_editor_property("effect_name"))
            if not want(jeb):
                continue
            if process_owner(a, jeb, plan, dry):
                touched += 1
        unreal.log("[FX-REVERT] карта %s: акторов %d, возвращено %d" % (key, len(acts), touched))
        if touched and not dry:
            unreal.EditorLoadingAndSavingUtils.save_map(world, MAPS[key])


def drop_variants(plan, dry):
    """Удалить MIC-варианты. Вызывается ПОСЛЕ возврата всех ссылок, иначе останутся битые ссылки.

    ★ Варианты лежат РЯДОМ С ИСХОДНИКОМ, а у эффектов, размещённых на уровнях, исходники живут в
      папке карты (`/Game/Map/<карта>/EffectMat`), а не в общей `Effects/Mat`; у эффектов сцены
      логина — в третьем месте, `AsdaAssets/Textures/Effects/Mat` (их клал другой place-скрипт).
      Обходим все три вида каталогов.
    ★ ЧАСТИЧНЫЙ ОТКАТ НЕ УДАЛЯЕТ НИЧЕГО. Один и тот же вариант (ключ = текстура+буст) делят разные
      эффекты: удалив его при откате ОДНОГО эффекта, мы оставили бы битые ссылки у остальных."""
    victims = []
    for d in (MAT_DIR, TEX_MAT_DIR) + tuple("/Game/Map/%s/EffectMat" % k for k in MAP_KEYS if k in MAPS):
        if EAL.does_directory_exist(d):
            victims += [n.split(".")[0] for n in EAL.list_assets(d, False, False) if "_fxb" in n]
    victims = sorted(set(victims))
    plan["варианты"] = victims
    if dry or KEEP or ONLY:
        unreal.log("[FX-REVERT] вариантов к удалению %d (не удаляем: dry=%s keep=%s частичный охват=%s)"
                   % (len(victims), dry, KEEP, bool(ONLY)))
        return
    dead = 0
    for p in victims:
        if EAL.delete_asset(p):
            dead += 1
        else:
            plan["не_удалилось"].append(p)
    unreal.log("[FX-REVERT] вариантов удалено %d из %d" % (dead, len(victims)))


def main():
    """Работа скрипта (закон tools-no-import-side-effects: импорт ничего не делает)."""
    dry = (MODE != "revert")
    unreal.log("[FX-REVERT] режим=%s (dry=%s) охват: only=%s карты=%s keep=%s"
               % (MODE, dry, sorted(ONLY) or "—", MAP_KEYS, KEEP))
    plan = {"ссылок": [], "исходник_не_найден": [], "варианты": [], "не_удалилось": []}
    run_blueprints(plan, dry)
    run_levels(plan, dry)
    drop_variants(plan, dry)
    os.makedirs(os.path.dirname(OUT), exist_ok=True)
    with open(OUT, "w", encoding="utf-8") as f:
        json.dump({"режим": MODE, "dry": dry,
                   "ссылок_возвращено": len(plan["ссылок"]),
                   "вариантов": len(plan["варианты"]),
                   "исходник_не_найден": plan["исходник_не_найден"],
                   "не_удалилось": plan["не_удалилось"],
                   "ссылки": sorted(set(plan["ссылок"]))}, f, ensure_ascii=False, indent=1)
    unreal.log("[FX-REVERT] ссылок возвращено=%d, вариантов=%d, исходник не найден=%d, не удалилось=%d"
               % (len(plan["ссылок"]), len(plan["варианты"]),
                  len(plan["исходник_не_найден"]), len(plan["не_удалилось"])))
    unreal.log("[FX-REVERT] план записан: %s" % OUT)
    unreal.log("FX-REVERT ЗАВЕРШЕНО")


# ★ ЗАКОН tools-no-import-side-effects: при импорте модуль НИЧЕГО не делает.
if __name__ == "__main__" or globals().get("__spec__") is None:
    main()
