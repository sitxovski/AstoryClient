# Copyright (c) 2026 @sitxovski (Discord)
# asda_place_cloud_shadow_actors.py — актор CloudShadows per map (редактирование теней облаков в деталях
# актора: Sun/CloudTexture/TileSize/SpeedU/V/Strength/Contrast; OnConstruction сам вешает MID на солнце).
# Заменяет прямую MIC-привязку (MI_cloudshadow удаляется). env ASDA_SKY_MAPS — фильтр порций.
import os, re, unreal
import os as _os, sys as _sys
_sys.path.insert(0, _os.path.dirname(_os.path.abspath(__file__)))
import asda_maps as _MAPS
import asda_login_guard as _LG   # ★ 16.09.2026 (обратный обход savedir14): единый гейт сцены логина
import asda_model_import_ue as _MIU   # ★ дедуп №2 В1 Т13: общий поиск текстуры в <root>/Texture
EAL = unreal.EditorAssetLibrary
MASTER = "/Game/Materials/M_AsdaCloudShadowLF"
_asset = _MAPS.map_asset_name  # ★ единый санитайзер имени карты (asda_maps.map_asset_name)

# Текстура по имени в <root>/Texture — общая реализация `asda_model_import_ue.find_texture_in_dir` (дедуп №2 Т13).
find_texture = _MIU.find_texture_in_dir

def process(mp):
    root = "/Game/Map/%s" % _asset(mp); lvl = "%s/%s" % (root, _asset(mp))
    # ★ 16.09.2026 (обратный обход ревизии savedir14). Инструмент грузит уровень, спавнит в нём актор,
    #   удаляет MIC под корнем карты и СОХРАНЯЕТ уровень — гейт сцены логина ДО load_level и до первой
    #   записи. Проверяется КОРЕНЬ карты: под ним всё, что инструмент трогает (у сцены логина корень
    #   /Game/Map/Login и есть сам уровень со светом владельца).
    if _LG.login_map_write_blocked(root, "asda_place_cloud_shadow_actors.py", quiet=True):
        return
    if not EAL.does_asset_exist(lvl):
        unreal.log("[CSA] %s: нет уровня — пропуск" % mp); return
    les = unreal.get_editor_subsystem(unreal.LevelEditorSubsystem)
    eas = unreal.get_editor_subsystem(unreal.EditorActorSubsystem)
    les.load_level(lvl)
    tex = find_texture(root, "SKY_DOME_CLOUDE2")
    sun = None
    for a in list(eas.get_all_level_actors()):
        cls = a.get_class().get_name()
        if cls == "DirectionalLight": sun = a
        elif cls == "AsdaCloudShadowsActor": eas.destroy_actor(a)
    if not sun or not tex:
        unreal.log("[CSA] %s: sun=%s tex=%s — пропуск" % (mp, bool(sun), bool(tex))); return
    a = eas.spawn_actor_from_class(unreal.AsdaCloudShadowsActor, unreal.Vector(0, 0, 20000))
    a.set_actor_label("CloudShadows")
    a.set_editor_property("sun", sun)
    a.set_editor_property("cloud_texture", tex)
    a.set_editor_property("master", EAL.load_asset(MASTER))
    # ★★★ 2026-08-27: `strength` / `contrast` (и `tile_size` / `speed_u` / `speed_v`) У АКТОРА
    #   БОЛЬШЕ НЕТ — эти постоянные числа переехали в дефолты мастера `M_AsdaCloudShadowLF`
    #   (плотность 1.0 = ядро гасит солнце, контраст 5.0 добивает мягкую маску CLOUDE2).
    #   §6.2 аудита `Reports/audit_runtime_style.md`; хранитель значений —
    #   `Tools/asda_bake_sky_material_params.py`. Вызовы убраны: с удалёнными UPROPERTY
    #   `set_editor_property` бросал бы исключение и `process()` молча пропускал бы карту целиком.
    mic = root + "/Materials/MI_cloudshadow"
    if EAL.does_asset_exist(mic): EAL.delete_asset(mic)
    les.save_current_level()
    unreal.log("[CSA] %s: актор CloudShadows размещён" % mp)

# ★ Список карт — ЕДИНЫЙ источник asda_maps.py (18 открытых карт (без 4 закрытых/инстансных) — небо/облака).
#   Было: локальный литерал (13 копий по тулам). Состав сверен 1:1.
MAPS = _MAPS.OUTDOOR_MAPS
_only = os.environ.get("ASDA_SKY_MAPS", "")
if _only: MAPS = [m for m in MAPS if m in _only.split("||")]


def main():
    """Работа скрипта. Вынесена в функцию, чтобы импорт модуля НИЧЕГО не делал
    (закон [[tools-no-import-side-effects]])."""
    global mp
    for mp in MAPS:
        try: process(mp)
        except Exception as e: unreal.log_error("[CSA] %s FAIL: %s" % (mp, e))
        try: unreal.SystemLibrary.collect_garbage()
        except Exception: pass
    unreal.log("=== CSA ЗАВЕРШЕНО ===")


# ★ ЗАКОН [[tools-no-import-side-effects]]: при импорте модуль НИЧЕГО не делает.
#   `__spec__` выставляет ЛЮБАЯ машинерия импорта (import / importlib / runpy) и НЕ
#   выставляет прямое исполнение файла (python file.py, UE -ExecutePythonScript).
#   `python -m` даёт `__name__ == "__main__"` и покрыт первым условием.
if __name__ == "__main__" or globals().get("__spec__") is None:
    main()
