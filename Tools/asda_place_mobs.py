# Copyright (c) 2026 @sitxovski (Discord)
# asda_place_mobs.py — запекание МОБОВ/боссов карты в UE-уровень как AAsdaMobActor (Editor-Friendly).
# Источник: export/maps/<map>/mob_manifest.json (из серверных MonsterData_Info\<Map>.svr — те же файлы
#   читает сервер; adoption сетевым 4013 свяжет по MobId+тайлу). Модель: скелетал /Models/MobSkel/<model>
#   (идл anim_00), фолбэк — статик /Models/Mob/<model> (33 модели без клипов: tree/stone-декор).
# Конверсия координат/поворота — как NPC (у мобов авторского поворота в .svr НЕТ — ставим 0).
# Запуск: headless, ASDA_MAP=<карта>.
import os, re, json, unreal
import os as _os, sys as _sys
_sys.path.insert(0, _os.path.dirname(_os.path.abspath(__file__)))
import asda_coords as _C
import asda_ue_paths as _U
import asda_maps as _MAPS

MAP_NAME = os.environ.get("ASDA_MAP", "alpen")
MANIFEST = r"@@ASTORY_DEV_ROOT_FWD@@/Data/Export/maps/%s/mob_manifest.json" % MAP_NAME
SKEL_ROOT   = _U.G_MODELS_MOBSKEL
STATIC_ROOT = _U.G_MODELS_MOB
MOB_SCALE = 0.01   # glTF метрами (как NPC) → см. ★ Статик-фолбэк В ТЕХ ЖЕ единицах (замерено)

EAL = unreal.EditorAssetLibrary
def log(m): unreal.log("[place_mobs] " + str(m))

to_ue_v = _C.to_ue   # ★ единая конверсия осей (asda_coords)

def main():
    # ★ 16.09.2026 (ревизия раздела 22, СРЕДН-2): имя карты приходит из ASDA_MAP и НИ С ЧЕМ не сверяется
    #   (asda_maps.map_asset_name только чистит символы) — ASDA_MAP=Login дал бы пути сцены логина. Уровень
    #   логина открывает только ASDA_ALLOW_LOGIN_MAP_WRITE=1 (единое правило, asda_login_guard).
    import asda_login_guard as _LG
    # ★ 16.09.2026 (ревизия §7, п. 5): плюс сверка имени карты с реестром (Tools/data/asda_map_registry.json) —
    #   опечатка в ASDA_MAP иначе молча даёт произвольный корень /Game/Map/<мусор>.
    if _LG.login_map_write_blocked(LEVEL, "asda_place_mobs.py") or \
       _LG.map_name_blocked(LEVEL, "asda_place_mobs.py"): return
    if not EAL.does_asset_exist(LEVEL):
        unreal.log_error("Нет уровня %s" % LEVEL); return
    # ★ 11.10.2026: запрет запекания мобов на карте (реестр карт, "mob_bake_off" — решение владельца по nightvalley).
    #   Не ошибка: раскатка asda_place_mobs_all идёт дальше, уровень не открывается и не сохраняется.
    import asda_map_registry as _REG
    _off = {k.lower(): v for k, v in _REG.mob_bake_off().items()}
    if MAP_NAME.lower() in _off:
        log("ПРОПУСК %s: mob_bake_off — %s" % (MAP_NAME, _off[MAP_NAME.lower()])); return
    if not os.path.isfile(MANIFEST.replace("/", os.sep)):
        unreal.log_error("Нет манифеста %s" % MANIFEST); return
    data = json.load(open(MANIFEST, encoding="utf-8"))
    log("карта %s: мобов=%d (из %s)" % (MAP_NAME, data["count"], data["svrFile"]))

    les = unreal.get_editor_subsystem(unreal.LevelEditorSubsystem)
    eas = unreal.get_editor_subsystem(unreal.EditorActorSubsystem)
    les.load_level(LEVEL)

    old = [a for a in eas.get_all_level_actors() if isinstance(a, unreal.AsdaMobActor)]
    for a in old: eas.destroy_actor(a)
    if old: log("удалено прежних моб-акторов: %d" % len(old))

    placed = missing = static_used = 0
    for m in data["mob"]:
        model = m["model"]
        skel  = "%s/%s/SkeletalMeshes/%s" % (SKEL_ROOT, model, model)
        stat  = "%s/%s/StaticMeshes/%s" % (STATIC_ROOT, model, model)
        use_skel = EAL.does_asset_exist(skel)
        if not use_skel and not EAL.does_asset_exist(stat):
            unreal.log_warning("нет модели: %s (type=%d idx=%d)" % (model, m["type"], m["idx"]))
            missing += 1; continue
        loc = to_ue_v(m["gx"], m["gy"], m["gz"])
        act = eas.spawn_actor_from_class(unreal.AsdaMobActor, loc)
        act.set_actor_location(loc, False, False)
        act.set_editor_property("mob_id", m["type"])
        act.set_editor_property("tile_x", m["tileX"])
        act.set_editor_property("tile_z", m["tileZ"])
        act.set_editor_property("regen_tag", m["regen"])
        act.set_editor_property("group_id", m["group"])
        act.set_editor_property("roam_rect", [int(x) for x in m["roam"]])
        act.set_editor_property("patrol", [unreal.IntPoint(int(p[0]), int(p[1])) for p in m["patrol"]])
        # ★ .svr+0x49==1 = CALL/EVENT-моб (босс/ивент, спавн по вызову — сервер обнуляет авто-реген).
        #   В редакторе виден (спавн-точка), в игре скрыт (BeginPlay bCallMob), папка отдельная.
        is_call = bool(m["enable"])
        act.set_editor_property("call_mob", is_call)
        if use_skel:
            act.mesh.set_skinned_asset_and_update(unreal.load_asset(skel))
            act.mesh.set_world_scale3d(unreal.Vector(MOB_SCALE, MOB_SCALE, MOB_SCALE))
            anim = skel + "anim_00"
            if EAL.does_asset_exist(anim):
                a = unreal.load_asset(anim)
                act.set_editor_property("idle_anim", a)
                act.mesh.set_editor_property("animation_mode", unreal.AnimationMode.ANIMATION_SINGLE_NODE)
                ad = act.mesh.get_editor_property("animation_data")
                ad.set_editor_property("anim_to_play", a)
                act.mesh.set_editor_property("animation_data", ad)
        else:
            act.static_mesh_comp.set_static_mesh(unreal.load_asset(stat))
            # ★★★ Статик-фолбэк масштабируется ТАК ЖЕ, как скелетал. Он импортирован тем же
            #   конвертером glTF, т.е. в МЕТРАХ (замер: mon_s_0_pawn02 = 21244 ед., скелетал
            #   mon_s_0_mosrd01 = 20782 ед.) — прежнее «статики в см → scale 1» давало мобов
            #   в 100 раз больше (1602 актора на 11 картах).
            act.mesh.set_world_scale3d(unreal.Vector(MOB_SCALE, MOB_SCALE, MOB_SCALE))
            static_used += 1
        # ★ Эталонные текстуры ПО ТИПУ (DT_MobTextures): одна модель обслуживает много типов с разными
        #   .TEX (перекраски) — см. Tools/asda_mob_texmap.py. OnConstruction отработал ДО set mob_id,
        #   поэтому доводим явно, чтобы запечённый актор (и его сетевые клоны-Template) были верны.
        try:
            act.apply_mob_textures()
        except Exception as e:
            unreal.log_warning("apply_mob_textures %s: %s" % (model, e))
        short = re.sub(r'^l?mon_[a-z]_[0-9]_', '', model).replace('-a_00', '').replace('-a_0', '_v')
        act.set_actor_label("MOB_%03d_%s%s" % (m["idx"], short, "_CALL" if is_call else ""))
        act.set_folder_path("MOB/Event" if is_call else "MOB")
        placed += 1
    les.save_current_level()
    log("ГОТОВО: размещено %d (статик-фолбэк %d), без модели %d" % (placed, static_used, missing))


def _script_main():
    """Работа скрипта. Вынесена в функцию, чтобы импорт модуля НИЧЕГО не делал
    (закон [[tools-no-import-side-effects]])."""
    global LEVEL
    ASSET    = _MAPS.map_asset_name(MAP_NAME)  # ★ единый санитайзер имени карты (asda_maps.map_asset_name)
    LEVEL    = _U.level_path(MAP_NAME)   # ★ волна 2 п. 7: путь уровня — реестр путей
    main()


# ★ ЗАКОН [[tools-no-import-side-effects]]: при импорте модуль НИЧЕГО не делает.
#   `__spec__` выставляет ЛЮБАЯ машинерия импорта (import / importlib / runpy) и НЕ
#   выставляет прямое исполнение файла (python file.py, UE -ExecutePythonScript).
#   `python -m` даёт `__name__ == "__main__"` и покрыт первым условием.
if __name__ == "__main__" or globals().get("__spec__") is None:
    _script_main()
