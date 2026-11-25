# -*- coding: utf-8 -*-
# Copyright (c) 2026 @sitxovski (Discord)
# asda_login_light_balance_1031.py — ★ 31.10.2026: точечная запись баланса света сцены логина в Login.umap (разрешение владельца
#   на задачу «логин в игре ≠ редактор + самый красивый баланс света и Holy Light»). Меняет РОВНО 4 значения:
#     HolyLight_SpotLight (BP-экземпляр): сила 5 → 8 лм, VolumetricScatteringIntensity 150 → 320 (луч в объёмном тумане);
#     SpotLight (ближний свет на шкатулку): сила 22.63 → 30 лм;
#     PostProcessVolume (объём карты): AutoExposureBias = 1.75 (переопределение; было не задано = проектные 1.0).
#   Подбор — кадрами игры (Reports/login_light_1031, вариант B). Всё остальное в уровне не трогается.
#   Режимы (env ASDA_LL_MODE): "export" — только выгрузка уровня в T3D (сверка, ничего не пишет); "apply" — выгрузка ДО,
#   правка, выгрузка ПОСЛЕ, сохранение уровня. Запись — только с ASDA_ALLOW_LOGIN_WRITE=1 и ASDA_ALLOW_LOGIN_MAP_WRITE=1.
# Запуск: python Tools/run_headless_safe.py Tools/asda_login_light_balance_1031.py --marker "БАЛАНС ГОТОВ"
import os
import sys
import unreal

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import asda_login_guard   # без побочных эффектов при импорте

OUTD = os.environ.get("ASDA_LL_OUT", r"@@ASTORY_DEV_ROOT_FWD@@/Reports/login_light_1031")
MODE = os.environ.get("ASDA_LL_MODE", "export")
TAG = os.environ.get("ASDA_LL_TAG", "t3d")
# Целевые значения варианта B (метка актора → поле → значение)
HOLY = {"intensity": 8.0, "volumetric_scattering_intensity": 320.0}
SPOT = {"intensity": 30.0}
EXPOSURE_BIAS = 1.75


def log(m):
    unreal.log("[баланс] " + str(m))


def export_t3d(world, path):
    """Полная текстовая выгрузка уровня (все акторы и свойства) — для сверки «что изменено, что нет»."""
    task = unreal.AssetExportTask()
    task.object = world
    task.filename = path
    task.selected = False
    task.replace_identical = True
    task.prompt = False
    task.automated = True
    task.exporter = unreal.LevelExporterT3D()
    ok = unreal.Exporter.run_asset_export_task(task)
    log("T3D %s → %s" % ("OK" if ok else "ОШИБКА", path))
    return ok


def set_light(actor, values):
    """Значения на световом компоненте актора (экземпляр уровня)."""
    for c in actor.get_components_by_class(unreal.LightComponent):
        for k, v in values.items():
            old = c.get_editor_property(k)
            c.set_editor_property(k, v)
            log("%s.%s: %s → %s" % (actor.get_actor_label(), k, old, c.get_editor_property(k)))


def main():
    les = unreal.get_editor_subsystem(unreal.LevelEditorSubsystem)
    eas = unreal.get_editor_subsystem(unreal.EditorActorSubsystem)
    if MODE == "apply":
        asda_login_guard.forbid_login_write("asda_login_light_balance_1031")
    les.load_level("/Game/Map/Login")
    world = unreal.get_editor_subsystem(unreal.UnrealEditorSubsystem).get_editor_world()
    export_t3d(world, "%s/%s_before.t3d" % (OUTD, TAG))
    if MODE != "apply":
        log("БАЛАНС ГОТОВ (только выгрузка)")
        return
    found = set()
    for a in eas.get_all_level_actors():
        lab = a.get_actor_label()
        if lab == "HolyLight_SpotLight":
            set_light(a, HOLY); found.add(lab)
        elif lab == "SpotLight":
            set_light(a, SPOT); found.add(lab)
        elif lab == "PostProcessVolume" and isinstance(a, unreal.PostProcessVolume):
            s = a.get_editor_property("settings")
            old = (s.get_editor_property("override_auto_exposure_bias"), s.get_editor_property("auto_exposure_bias"))
            s.set_editor_property("override_auto_exposure_bias", True)
            s.set_editor_property("auto_exposure_bias", EXPOSURE_BIAS)
            a.set_editor_property("settings", s)
            log("PostProcessVolume.AutoExposureBias: %s → (True, %s)" % (old, EXPOSURE_BIAS))
            found.add(lab)
    if found != {"HolyLight_SpotLight", "SpotLight", "PostProcessVolume"}:
        log("ОШИБКА: найдены не все акторы %s — уровень НЕ сохраняется" % sorted(found))
        return
    export_t3d(world, "%s/%s_after.t3d" % (OUTD, TAG))
    saved = les.save_current_level()
    log("сохранение уровня: %s" % saved)
    if saved:
        log("БАЛАНС ГОТОВ")


if __name__ == "__main__" or globals().get("__spec__") is None:
    main()
