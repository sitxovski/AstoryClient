# Copyright (c) 2026 @sitxovski (Discord)
# Пер-карточный материал облаков M_AsdaCloudMesh_<карта> (MIC от M_AsdaCloudMesh) + привязка к CloudField.
# Цвет по умолчанию = лифт главного цвета неба карты; юзер правит Tint/ShadeColor/Intensity в этом MIC.
import os, unreal
import sys as _sys, os as _os
_sys.path.insert(0, _os.path.dirname(_os.path.abspath(__file__)))
import asda_maps as _MAPS
import asda_login_guard as _LG   # ★ 16.09.2026 (обратный обход savedir14): единый гейт сцены логина
EAL = unreal.EditorAssetLibrary; MEL = unreal.MaterialEditingLibrary
AT = unreal.AssetToolsHelpers.get_asset_tools()
_asset = _MAPS.map_asset_name          # ★ единый санитайзер имени карты
SKY_COLORS = _MAPS.SKY_COLORS     # ★ единая таблица цвета неба (было 3 дословные копии), ключи = OUTDOOR_MAPS
les = unreal.get_editor_subsystem(unreal.LevelEditorSubsystem)
eas = unreal.get_editor_subsystem(unreal.EditorActorSubsystem)


# ★ 05.09.2026 (аудит неба sky_audit_report.md): меши облаков CloudField. Актор AAsdaCloudFieldActor при ПУСТОМ
#   `CloudMeshes` рисует фолбэк — блобы из движковых сфер (AsdaCloudFieldActor.cpp), а метабол-меши cloudblob_a..d
#   в 17 картах были проставлены в июле скриптом из scratchpad (в Tools его нет: grep cloud_meshes = 0).
#   Раскатка asda_rollout_sky_full.py их не ставила → воссозданный 05.09 alpen получил облака-сферы.
#   Единый список путей здесь; ставится ТОЛЬКО если массив пуст (существующие поля не трогаем).
CLOUD_MESH_PATHS = ["/Game/AsdaAssets/Clouds/cloudblob_%s/StaticMeshes/cloudblob_%s" % (c, c) for c in "abcd"]
CLOUD_MESH_SCALE = (0.45, 1.25)   # ScaleMin/Max для метабол-мешей (как на 17 картах июля; дамп sky_audit_dump_before.json)


def ensure_cloud_meshes(actor, log=None):
    """Заполнить пустой `cloud_meshes` актора CloudField метабол-мешами cloudblob_a..d.
    Возвращает True, если массив заполнен этим вызовом; False — уже был непустой или мешей нет на диске."""
    cur = list(actor.get_editor_property("cloud_meshes"))
    if cur:
        return False
    meshes = [EAL.load_asset(pth) for pth in CLOUD_MESH_PATHS if EAL.does_asset_exist(pth)]
    if len(meshes) != len(CLOUD_MESH_PATHS):
        (log or unreal.log_warning)("[PMC] cloudblob-мешей на диске %d/%d — cloud_meshes не заполнен (восстановить: "
                                    "asda_gen_cloud_blobs.py → импорт GLB Interchange ×100)" % (len(meshes), len(CLOUD_MESH_PATHS)))
        return False
    actor.set_editor_property("cloud_meshes", meshes)   # PostEditChange → Rebuild() → блобы вместо сфер
    # Размер облака = ТОЛЬКО ScaleMin/Max (памятка sky-system-complete: с cloudblob дефолт 0.45/1.25 = 24–66 м);
    #   умолчания класса 0.2/0.55 рассчитаны на фолбэк-сферы (лепестки 12000·S) — с мешем ~5240 юн дают облака 10–29 м,
    #   в 2.3 раза мельче, чем на остальных 17 картах. Переставляем ТОЛЬКО если стоят ровно классовые умолчания.
    if abs(actor.get_editor_property("scale_min") - 0.2) < 1e-6 and abs(actor.get_editor_property("scale_max") - 0.55) < 1e-6:
        actor.set_editor_property("scale_min", CLOUD_MESH_SCALE[0]); actor.set_editor_property("scale_max", CLOUD_MESH_SCALE[1])
    return True


def ensure_cloud_mic(mp):
    """MIC M_AsdaCloudMesh_<карта> (создать/обновить, сохранить) — ЕДИНСТВЕННЫЙ носитель цвета облаков карты.
    ★ 05.09.2026 вынесено в функцию: asda_rollout_sky_full.place_cloud_field ставил цвет на актор (cloud_color/shade_color),
      но эти поля с актора сняты (памятка sky-system-complete: «актор цвета НЕ трогает — был конфликт-перезатир») —
      раскатка неба на alpen падала «Failed to find property 'cloud_color'». Теперь обе точки зовут одну функцию."""
    root = "/Game/Map/%s" % _asset(mp)
    nm = "M_AsdaCloudMesh_%s" % _asset(mp)
    p = root + "/Materials/" + nm
    mic = EAL.load_asset(p) if EAL.does_asset_exist(p) else \
          AT.create_asset(nm, root + "/Materials", unreal.MaterialInstanceConstant, unreal.MaterialInstanceConstantFactoryNew())
    MEL.set_material_instance_parent(mic, EAL.load_asset("/Game/Materials/M_AsdaCloudMesh"))
    col = SKY_COLORS[mp]
    cc = [0.4 + 0.6 * c for c in col]; sc = [c * 0.62 for c in cc]
    MEL.set_material_instance_vector_parameter_value(mic, "Tint", unreal.LinearColor(cc[0], cc[1], cc[2], 1.0))
    MEL.set_material_instance_vector_parameter_value(mic, "ShadeColor", unreal.LinearColor(sc[0], sc[1], sc[2], 1.0))
    MEL.set_material_instance_scalar_parameter_value(mic, "Intensity", 1.0)
    EAL.save_loaded_asset(mic)
    return mic, nm


def main():
    """Работа скрипта. Вынесена в функцию, чтобы импорт модуля НИЧЕГО не делал
    (закон [[tools-no-import-side-effects]])."""
    MAPS = _MAPS.select_maps(_MAPS.SKY_MAPS, env="ASDA_SKY_MAPS")   # фильтр "a||b" — порционная раскатка
    for mp in MAPS:
        root = "/Game/Map/%s" % _asset(mp); lvl = "%s/%s" % (root, _asset(mp))
        # ★ 16.09.2026 (обратный обход ревизии savedir14): ensure_cloud_mic создаёт и СОХРАНЯЕТ MIC под
        #   корнем карты, следом идут load_level и save_current_level — гейт сцены логина ДО всего этого.
        if _LG.login_map_write_blocked(root, "asda_percloud_mics.py", quiet=True):
            continue
        if not EAL.does_asset_exist(lvl):
            unreal.log("[PMC] %s: нет уровня" % mp); continue
        mic, nm = ensure_cloud_mic(mp)
        les.load_level(lvl)
        for a in eas.get_all_level_actors():
            if a.get_class().get_name() == "AsdaCloudFieldActor":
                a.set_editor_property("material", mic)   # PostEditChange → Rebuild → MID-ы от нового MIC
        les.save_current_level()
        unreal.log("[PMC] %s: %s привязан" % (mp, nm))
        try: unreal.SystemLibrary.collect_garbage()
        except Exception: pass
    unreal.log("=== PMC ЗАВЕРШЕНО ===")


# ★ ЗАКОН [[tools-no-import-side-effects]]: при импорте модуль НИЧЕГО не делает.
#   `__spec__` выставляет ЛЮБАЯ машинерия импорта (import / importlib / runpy) и НЕ
#   выставляет прямое исполнение файла (python file.py, UE -ExecutePythonScript).
#   `python -m` даёт `__name__ == "__main__"` и покрыт первым условием.
if __name__ == "__main__" or globals().get("__spec__") is None:
    main()
