# -*- coding: utf-8 -*-
# Copyright (c) 2026 @sitxovski (Discord)
# asda_restore_char_models.py — ВОССТАНОВЛЕНИЕ ПОТЕРЯННЫХ СТАТИК-МОДЕЛЕЙ ПЕРСОНАЖА
#   (/Game/AsdaAssets/Models/Char) из уцелевшего сырья RawAssets/Models/Char/*.glb.
#
# ЗАЧЕМ. Реверт P4V 03.09.2026 20:54 снёс `Content/**.uasset`. Список потерь 04.09 строился ПО
#   УПОМИНАНИЯМ В ЛОГАХ, поэтому меши персонажа, которых в логах не было, в восстановление НЕ
#   попали: у 721 модели остались ПУСТЫЕ папки `<модель>/{StaticMeshes,Materials}` без единого
#   ассета. Симптом у владельца — «лысая женщина» на экране выбора персонажа: пропали ВСЕ 47
#   женских причёсок, и `UAsdaCharAssembly::ApplyHair` не находит меш
#   `/Game/AsdaAssets/Models/Char/<имя>/StaticMeshes/<имя>` (лог «ApplyHair: меш волос не найден»),
#   а базовая вшитая причёска к этому моменту уже погашена `HideBaseHair`.
#
# ЧТО ДЕЛАЕТ (идемпотентно, ничего не удаляет):
#   ФАЗА 1. Находит модели, у которых есть `RawAssets/Models/Char/<имя>.glb`, но НЕТ ассета
#           `<Char>/<имя>/StaticMeshes/<имя>`, и импортирует их ШТАТНЫМ импортёром — вызовом
#           `asda_import_models.import_dir` (тот же AutomatedAssetImportData, те же настройки).
#   ФАЗА 2. Приводит материалы восстановленных мешей к тому же виду, что у уцелевших мужских
#           близнецов (проверено по `ch1_m_00_hair011_00_00_0`, `ch1_m_09_chst101_00_00_0`):
#           авто-материал Interchange (MIC, имя = имя слота) чинится НА МЕСТЕ —
#           parent = мастер по `asda_material_policy.master_for` (для семейства ch1_* это
#           M_AsdaCharLit, непрозрачный: альфа у tSkin/tRigid = маска блика, а не прозрачность),
#           Tex = текстура по `asda_fix_model_materials.find_tex` (там же формула эталона для
#           причёсок hairNNN → hair00N, см. `CCharacter_LoadHairModel@0x9E2700`), Ambient = 1.0.
#           Если в слоте не MIC — создаётся `<модель>/Materials/MI_<tex>` (ветка как в
#           `asda_fix_hair_materials.py`).
#   ФАЗА 3. Сверка: сколько моделей осталось без меша, сколько слотов без текстуры, сколько
#           материалов не на ожидаемом мастере. Печатает МАРКЕР для `run_headless_safe.py`.
#
# ОХВАТ. По умолчанию — все недостающие модели в `Models/Char`. Точечно:
#   ASDA_RESTORE_ONLY="имя1,имя2" — работать только с этими моделями (для проверок).
#   ASDA_RESTORE_LIMIT="N"        — взять первые N недостающих (дымовой прогон).
#
# Запуск (охват ОБЯЗАТЕЛЕН — см. ASDA_RESTORE_LIST ниже):
#   ASDA_RESTORE_LIST=@@ASTORY_DEV_ROOT@@\Reports\audit_2026-09-02\lost_char_models_2026-09-04.txt \
#   python Tools/run_headless_safe.py Tools/asda_restore_char_models.py \
#             --marker "ВОССТАНОВЛЕНИЕ ЗАВЕРШЕНО" --idle 600 --timeout 10800
import os
import sys

import unreal

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import asda_ue_paths as _U                 # общие пути проекта
import asda_material_policy as POL         # выбор мастер-материала по эталону
import asda_import_models as AIM           # штатный импортёр .glb (import модуля побочных действий не даёт)
import asda_fix_model_materials as AFM     # резолв текстуры по имени слота (формулы причёсок/мультислотов)
import asda_fix_weapon_missing_tex as AFW  # резолв текстуры по имени МЕША (строка материала в .jbm — мусор)
import asda_login_guard as _LG            # ★ 15.09.2026: модели сцены логина не восстанавливать без ASDA_ALLOW_LOGIN_WRITE=1

RAW_CHAR = os.path.join(AIM.RAW_ROOT, "Char")      # D:\...\RawAssets\Models\Char
DEST     = _U.G_MODELS_CHAR                        # /Game/AsdaAssets/Models/Char
BATCH    = 40                                      # моделей за один вызов импортёра (память/GC)

ONLY  = set(s.strip() for s in os.environ.get("ASDA_RESTORE_ONLY", "").split(",") if s.strip())
LIMIT = int(os.environ.get("ASDA_RESTORE_LIMIT", "0") or 0)
# ★ ОХВАТ СПИСКОМ (ASDA_RESTORE_LIST=<файл>, по имени модели в строке). Нужен именно он, а не
#   «пройтись по всей папке»: у части УЦЕЛЕВШИХ моделей текстура назначена ПО ДАННЫМ
#   (`asda_fix_items_loot_tex`, `asda_fix_npc_materials`), и слепой проход по имени слота
#   откатил бы эти правки. Работаем строго по списку потерь.
_LIST = os.environ.get("ASDA_RESTORE_LIST", "").strip()
if _LIST and os.path.isfile(_LIST):
    with open(_LIST, "r", encoding="utf-8") as _f:
        ONLY |= set(s.strip() for s in _f if s.strip() and not s.startswith("#"))

EAL    = unreal.EditorAssetLibrary
ATOOLS = unreal.AssetToolsHelpers.get_asset_tools()
MEL    = unreal.MaterialEditingLibrary
AR     = unreal.AssetRegistryHelpers.get_asset_registry()


def log(m):
    unreal.log_warning("[restore-char] %s" % m)


def mesh_pkg(name):
    """Путь ассета статик-меша модели <name> (раскладка импортёра glTF Interchange)."""
    return "%s/%s/StaticMeshes/%s" % (DEST, name, name)


def raw_names():
    """Имена всех моделей, для которых есть сырьё .glb (с учётом охвата ASDA_RESTORE_ONLY)."""
    names = sorted(os.path.splitext(f)[0] for f in os.listdir(RAW_CHAR) if f.lower().endswith(".glb"))
    if not ONLY:
        raise RuntimeError("Охват не задан: нужен ASDA_RESTORE_LIST=<файл со списком> либо "
                           "ASDA_RESTORE_ONLY=имя1,имя2 (слепой проход по всей папке запрещён — "
                           "он откатил бы правки материалов, сделанные по данным)")
    names = [n for n in names if n in ONLY]   # ★ 15.09: сначала охват, затем пропуск сцены логина (строка в лог только по названным)
    names = [n for n in names if not _LG.login_package_blocked("%s/%s" % (DEST, n), "asda_restore_char_models.py")]
    return [n for n in names if n in ONLY]


def slot_needs_work(name, slot):
    """True, если материал слота ещё не приведён к целевому виду (мастер по политике + Tex)."""
    mi = slot.material_interface
    if not isinstance(mi, unreal.MaterialInstanceConstant):
        return True
    par = mi.get_editor_property("parent")
    if not par or par.get_path_name().split(".")[0] != POL.master_for(
            name, str(slot.get_editor_property("material_slot_name"))):
        return True
    try:
        return MEL.get_material_instance_texture_parameter_value(mi, unreal.Name("Tex")) is None
    except Exception:
        return True


def models_to_work():
    """(что импортировать, над чем работать). Работать надо и над недоимпортированными, и над теми,
    у кого меш уже есть, но материал не доведён (незаконченный прошлый прогон)."""
    to_import, work = [], []
    for n in raw_names():
        pkg = mesh_pkg(n)
        if not EAL.does_asset_exist(pkg):
            to_import.append(n)
            work.append(n)
            continue
        mesh = unreal.load_asset(pkg)
        if mesh and any(slot_needs_work(n, s) for s in mesh.get_editor_property("static_materials")):
            work.append(n)
    if LIMIT > 0:
        work = work[:LIMIT]
        to_import = [n for n in to_import if n in set(work)]
    return to_import, work


def phase_import(names):
    """ФАЗА 1 — импорт .glb штатным импортёром, порциями по BATCH."""
    done = 0
    for i in range(0, len(names), BATCH):
        chunk = names[i:i + BATCH]
        AIM.import_dir(RAW_CHAR, ["%s.glb" % n for n in chunk])
        got = sum(1 for n in chunk if EAL.does_asset_exist(mesh_pkg(n)))
        done += got
        log("импорт %d/%d: пришло %d из %d" % (min(i + BATCH, len(names)), len(names), got, len(chunk)))
        try:
            unreal.SystemLibrary.collect_garbage()
        except Exception:
            pass
    return done


def phase_materials(names, tex):
    """ФАЗА 2 — материалы восстановленных мешей приводятся к виду уцелевших близнецов."""
    n_slot = n_fixed = n_ok = n_miss = n_new = 0
    miss = []
    for name in names:
        pkg = mesh_pkg(name)
        mesh = unreal.load_asset(pkg)
        if not mesh:
            continue
        mats = mesh.get_editor_property("static_materials")
        changed = False
        for idx, slot in enumerate(mats):
            n_slot += 1
            sname = str(slot.get_editor_property("material_slot_name"))
            tp = AFM.find_tex(sname, tex)
            if not tp:
                # ★ ДЕФЕКТ ИСХОДНЫХ ДАННЫХ: строка материала внутри .jbm у части брони/оружия —
                #   МУСОР (корейская кодировка «Åõ±¸», «ÇÏÀÇ») либо имя ЧУЖОЙ модели. Оригинал эту
                #   строку не читает вообще (текстура берётся из item_img_table,
                #   CCharacter_SetParts_Armor@0x9EB3C0), но в редакторе такой меш белый. Штатное
                #   правило проекта — текстура ПО ИМЕНИ САМОГО МЕША (asda_fix_weapon_missing_tex);
                #   ровно так выглядят уцелевшие мужские близнецы этих же моделей.
                tp = AFW.guess_tex(name, tex)
            if not tp:
                # Текстуры-кандидата нет вовсе (у `ch1_?_09_chst308` её нет и в АРХИВЕ — уцелевший
                #   мужской близнец точно так же без Tex). Мастер всё равно ставим: у glTF-дефолта
                #   `MI_Default_Opaque` НЕТ параметра `Tex`, и рантайм-подстановка текстуры предмета
                #   (`UAsdaCharAssembly::ApplyItemTexture`, эталон CCharacter_SetParts_Armor@0x9EB3C0)
                #   не сработала бы вообще — вещь осталась бы белой и В ИГРЕ, а не только в редакторе.
                n_miss += 1
                miss.append("%s :: %s" % (name, sname))
                mi = slot.material_interface
                if isinstance(mi, unreal.MaterialInstanceConstant):
                    master = unreal.load_asset(POL.master_for(name, sname))
                    par = mi.get_editor_property("parent")
                    if not par or par.get_path_name() != master.get_path_name():
                        mi.set_editor_property("parent", master)
                        MEL.set_material_instance_scalar_parameter_value(mi, "Ambient", 1.0)
                        mi.modify()
                        EAL.save_loaded_asset(mi, False)
                continue
            texobj = unreal.load_asset(tp)
            master = unreal.load_asset(POL.master_for(name, sname))
            mi = slot.material_interface
            if isinstance(mi, unreal.MaterialInstanceConstant):
                cur = None
                try:
                    cur = MEL.get_material_instance_texture_parameter_value(mi, unreal.Name("Tex"))
                except Exception:
                    cur = None
                par = mi.get_editor_property("parent")
                if cur and texobj and cur.get_path_name() == texobj.get_path_name() \
                   and par and par.get_path_name() == master.get_path_name():
                    n_ok += 1
                    continue
                # авто-материал Interchange (имя = имя слота): чиним НА МЕСТЕ — parent + Tex + Ambient
                mi.set_editor_property("parent", master)
                MEL.set_material_instance_texture_parameter_value(mi, "Tex", texobj)
                MEL.set_material_instance_scalar_parameter_value(mi, "Ambient", 1.0)
                mi.modify()
                EAL.save_loaded_asset(mi, False)
                n_fixed += 1
            else:
                # в слоте не MIC (обычный Material либо пусто) — реперентить нечего, создаём MI_<tex>
                texname = tp.rsplit("/", 1)[-1]
                micdir = "%s/%s/Materials" % (DEST, name)
                micpath = "%s/MI_%s" % (micdir, texname)
                mic = unreal.load_asset(micpath) if EAL.does_asset_exist(micpath) else \
                    ATOOLS.create_asset("MI_%s" % texname, micdir, unreal.MaterialInstanceConstant,
                                        unreal.MaterialInstanceConstantFactoryNew())
                mic.set_editor_property("parent", master)
                MEL.set_material_instance_texture_parameter_value(mic, "Tex", texobj)
                MEL.set_material_instance_scalar_parameter_value(mic, "Ambient", 1.0)
                mic.modify()
                EAL.save_loaded_asset(mic, False)
                mesh.set_material(idx, mic)
                changed = True
                n_new += 1
        if changed:
            EAL.save_loaded_asset(mesh)
    log("материалы: слотов=%d починено_на_месте=%d создано_MI=%d уже_ок=%d без_текстуры=%d"
        % (n_slot, n_fixed, n_new, n_ok, n_miss))
    for s in sorted(set(miss)):
        log("  без текстуры: %s" % s)
    return n_miss


def phase_verify(names):
    """ФАЗА 3 — сверка конечного состояния (меш есть, материал на ожидаемом мастере, Tex задан)."""
    no_mesh, bad_master, no_tex = [], [], []
    for name in names:
        pkg = mesh_pkg(name)
        mesh = unreal.load_asset(pkg)
        if not mesh:
            no_mesh.append(name)
            continue
        for slot in mesh.get_editor_property("static_materials"):
            sname = str(slot.get_editor_property("material_slot_name"))
            mi = slot.material_interface
            if not mi:
                bad_master.append("%s :: %s :: слот пуст" % (name, sname))
                continue
            want = POL.master_for(name, sname)
            par = mi.get_editor_property("parent") if isinstance(mi, unreal.MaterialInstanceConstant) else None
            if not par or par.get_path_name().split(".")[0] != want:
                bad_master.append("%s :: %s :: %s" % (name, sname, par.get_name() if par else "нет parent"))
            try:
                t = MEL.get_material_instance_texture_parameter_value(mi, unreal.Name("Tex"))
            except Exception:
                t = None
            if not t:
                no_tex.append("%s :: %s" % (name, sname))
    log("СВЕРКА: без меша=%d, не тот мастер=%d, без Tex=%d (из %d моделей)"
        % (len(no_mesh), len(bad_master), len(no_tex), len(names)))
    for s in no_mesh[:40]:
        log("  НЕТ МЕША: %s" % s)
    for s in bad_master[:40]:
        log("  МАСТЕР: %s" % s)
    for s in no_tex[:40]:
        log("  БЕЗ Tex: %s" % s)
    return len(no_mesh), len(bad_master), len(no_tex)


def main():
    to_import, names = models_to_work()
    log("в %s: без меша=%d, требуют работы всего=%d" % (DEST, len(to_import), len(names)))
    if not names:
        log("ВОССТАНОВЛЕНИЕ ЗАВЕРШЕНО: восстанавливать нечего")
        return
    got = phase_import(to_import)
    log("ФАЗА 1: импортировано мешей %d из %d" % (got, len(to_import)))
    tex = AFM.tex_index()
    log("текстур в индексе: %d" % len(tex))
    phase_materials(names, tex)
    # ★ 16.09.2026 (раздел 22, Reports/dedup_audit/savedir_guard_2026-09-16.md): в
    #   /Game/AsdaAssets/Models/Char лежат cre_creback-a_00, cre_creation01*, cre_cresowell0*, blackscreen —
    #   пакеты сцены логина. Из ВОССТАНОВЛЕНИЯ они отсеяны выше (raw_names → login_package_blocked), но
    #   save_directory по всей папке пересохранял их всё равно.
    _LG.save_dir_login_safe(DEST, "asda_restore_char_models.py")
    phase_verify(names)
    log("ВОССТАНОВЛЕНИЕ ЗАВЕРШЕНО")


# ★ ЗАКОН [[tools-no-import-side-effects]]: при импорте модуль НИЧЕГО не делает.
if __name__ == "__main__" or globals().get("__spec__") is None:
    main()
