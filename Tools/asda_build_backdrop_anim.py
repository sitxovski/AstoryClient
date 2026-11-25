# Copyright (c) 2026 @sitxovski (Discord)
# asda_build_backdrop_anim.py — UE-сторона анимации login-задника (headless).
#
# Делает три вещи:
#   1) собирает DataAsset `/Game/AsdaAssets/Config/DA_LoginBackdropAnim` (класс UAsdaBackdropAnimData)
#      из RawAssets/backdrop/backdrop_anim.json (его пишет asda_export_backdrop_anim.py);
#   2) помечает существующие акторы `BackdropElem_*` тегами (`AsdaBackdropElem` + имя элемента) и
#      переводит их корневой компонент в Movable — без этого UE молча игнорирует смену трансформа;
#   3) ставит в уровень актор-дирижёр `LoginBackdropAnimator` (AAsdaBackdropAnimator) и привязывает
#      к нему DataAsset.
#
# ★ Акторы, их материалы и трансформы НЕ пересоздаются и НЕ переставляются: правки владельца
#   (blend-группы, ручные сдвиги) остаются нетронутыми — трогаются только теги и Mobility.
# ★ Откат к статике: снять `bEnabled` у LoginBackdropAnimator или `asda.LoginBackdropAnim 0`.
#
# Запуск: python Tools/run_headless_safe.py Tools/asda_build_backdrop_anim.py
import os, json
import unreal

HERE = os.path.dirname(os.path.abspath(__file__))
DATA_JSON = os.path.join(HERE, "..", "RawAssets", "backdrop", "backdrop_anim.json")
LEVEL = "/Game/Map/Login"
DA_PKG = "/Game/AsdaAssets/Config"
DA_NAME = "DA_LoginBackdropAnim"
DA_PATH = "%s/%s" % (DA_PKG, DA_NAME)
ANIMATOR_LABEL = "LoginBackdropAnimator"
ELEM_PREFIX = "BackdropElem_"
ELEM_TAG = "AsdaBackdropElem"
FOLDER = "LoginBackdrop"

EAL = unreal.EditorAssetLibrary
ATOOLS = unreal.AssetToolsHelpers.get_asset_tools()
les = unreal.get_editor_subsystem(unreal.LevelEditorSubsystem)
eas = unreal.get_editor_subsystem(unreal.EditorActorSubsystem)


def build_data_asset(j):
    """Создать/обновить DataAsset с треками. Возвращает ассет."""
    if not EAL.does_directory_exist(DA_PKG):
        EAL.make_directory(DA_PKG)
    cls = unreal.AsdaBackdropAnimData
    if EAL.does_asset_exist(DA_PATH):
        asset = EAL.load_asset(DA_PATH)
    else:
        factory = unreal.DataAssetFactory()
        factory.set_editor_property("data_asset_class", cls)
        asset = ATOOLS.create_asset(DA_NAME, DA_PKG, cls, factory)
    if not asset:
        unreal.log_error("НЕ удалось создать %s" % DA_PATH)
        return None

    asset.set_editor_property("source_clip", j["clip"])
    asset.set_editor_property("source_model", j["model"])
    asset.set_editor_property("frame_rate", float(j["fps"]))
    asset.set_editor_property("duration", float(j["duration"]))
    asset.set_editor_property("num_keys", int(j["numKeys"]))
    asset.set_editor_property("static_elements", [unreal.Name(n) for n in j["static"]])
    asset.set_editor_property("unbound_elements", [unreal.Name(n) for n in j["unbound"]])

    tracks = []
    for i, t in enumerate(j["tracks"]):
        rec = unreal.AsdaBackdropAnimTrack()
        rec.set_editor_property("element", unreal.Name(t["element"]))
        rec.set_editor_property("bone", unreal.Name(t["bone"]))
        rec.set_editor_property("keys", [float(x) for x in t["keys"]])
        tracks.append(rec)
        if (i + 1) % 100 == 0:
            unreal.log("  треков собрано %d/%d" % (i + 1, len(j["tracks"])))
    asset.set_editor_property("tracks", tracks)
    EAL.save_loaded_asset(asset)
    unreal.log("DataAsset готов: %s (треков %d, ключей в треке %d)"
               % (DA_PATH, len(tracks), int(j["numKeys"])))
    return asset


def tag_elements(track_names):
    """Проставить теги и Movable акторам элементов. Возвращает (помечено, из них с треком)."""
    tagged = 0
    with_track = 0
    for a in eas.get_all_level_actors():
        label = a.get_actor_label()
        if not label.startswith(ELEM_PREFIX):
            continue
        name = label[len(ELEM_PREFIX):]
        # Теги: маркер + имя элемента. Метка актора (ActorLabel) в упакованной игре недоступна —
        # именно поэтому идентификация идёт тегами, а не меткой.
        a.set_editor_property("tags", [unreal.Name(ELEM_TAG), unreal.Name(name)])
        root = a.get_component_by_class(unreal.StaticMeshComponent)
        if root:
            root.set_editor_property("mobility", unreal.ComponentMobility.MOVABLE)
        tagged += 1
        if name in track_names:
            with_track += 1
    return tagged, with_track


def place_animator(asset):
    """Поставить (или найти) актор-дирижёр и привязать к нему данные."""
    act = next((a for a in eas.get_all_level_actors()
                if a.get_actor_label() == ANIMATOR_LABEL), None)
    if act is None:
        anchor = next((a for a in eas.get_all_level_actors()
                       if a.get_actor_label() == "LoginBackdrop_Anchor"), None)
        loc = anchor.get_actor_location() if anchor else unreal.Vector(0, 0, 0)
        act = eas.spawn_actor_from_class(unreal.AsdaBackdropAnimator, loc)
        act.set_actor_label(ANIMATOR_LABEL)
        try:
            act.set_folder_path(FOLDER)
        except Exception:
            pass
        unreal.log("Актор-дирижёр создан: %s" % ANIMATOR_LABEL)
    else:
        unreal.log("Актор-дирижёр уже был в уровне: %s" % ANIMATOR_LABEL)
    act.set_editor_property("anim_data", asset)
    return act


def main():
    """Работа скрипта. Вынесена в функцию, чтобы импорт модуля НИЧЕГО не делал
    (закон [[tools-no-import-side-effects]])."""
    # ★★★ ЗАЩИТА СВЕТА ВЛАДЕЛЬЦА (13.09.2026): без ASDA_ALLOW_LOGIN_WRITE=1 — стоп до загрузки уровня
    import sys as _sys, os as _os
    _sys.path.insert(0, _os.path.dirname(_os.path.abspath(__file__)))
    from asda_headless_tool import forbid_login_write   # ★ волна 3 п.15: каркас headless-инструмента (внутри — asda_login_guard, правило то же)
    forbid_login_write(__file__)   # имя файла = прежний литерал 'asda_build_backdrop_anim.py'
    with open(DATA_JSON, "r", encoding="utf-8") as f:
        j = json.load(f)
    unreal.log("данные: клип=%s треков=%d ключей=%d fps=%.0f длительность=%.4f"
               % (j["clip"], len(j["tracks"]), j["numKeys"], j["fps"], j["duration"]))

    les.load_level(LEVEL)
    asset = build_data_asset(j)
    if not asset:
        unreal.log_error("ЗАВЕРШЕНО с ошибкой: нет DataAsset")
        return
    names = set(t["element"] for t in j["tracks"])
    tagged, with_track = tag_elements(names)
    unreal.log("помечено акторов элементов: %d (из них с треком: %d)" % (tagged, with_track))
    place_animator(asset)
    les.save_current_level()
    unreal.log("=== ЗАВЕРШЕНО: анимация задника подключена (треков %d, акторов %d) ==="
               % (len(j["tracks"]), tagged))


# ★ ЗАКОН [[tools-no-import-side-effects]]: при импорте модуль НИЧЕГО не делает.
if __name__ == "__main__" or globals().get("__spec__") is None:
    main()
