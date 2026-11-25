# Copyright (c) 2026 @sitxovski (Discord)
# ★★★ УСТАРЕЛО (2026-09-03, правка Д-1 аудита света): скрипт относится к СТАРОЙ unlit-модели
#   (per-map LightColor/LightDir/Ambient в MIC) и берёт направление света из .mapraw 'lightDir' —
#   вектор НЕВЕРНОЙ формулы (ошибка 30..90° на всех картах). Lit-мастера этих параметров не читают.
#   НЕ ЗАПУСКАТЬ. Актуальный свет карт: asda_convert_map_lit.py (sunDir из .mapraw ver>=3).
# asda_set_map_lighting.py — проставить КАЖДОЙ карте её освещение из .SCE (сцена .mapraw).
# Граф M_AsdaObject = tex*(Ambient + coolwhite*NdotL)*vtxColor (unlit). Per-map входы: Ambient (скаляр =
# среднее scene.ambient) + LightDir (вектор = scene.lightDir) — РОВНО как AsdaMapActor.cpp:319-397.
# Импортёр раньше ставил Ambient=1.0 → все карты одинаково ярко. Тут чиним на всех MIC объектов карты
# (папки Materials + Anims/_mats). Правит ТОЛЬКО ассеты-материалы, уровни/расстановку НЕ трогает.
# (Авто-экспозиция уже off в DefaultEngine.ini, иначе разница яркости не была бы видна.)
import os, struct, re, unreal
import os as _os, sys as _sys
_sys.path.insert(0, _os.path.dirname(_os.path.abspath(__file__)))
import asda_maps as _MAPS

EAL = unreal.EditorAssetLibrary
MEL = unreal.MaterialEditingLibrary
ar  = unreal.AssetRegistryHelpers.get_asset_registry()
RAW = r"@@ASTORY_DEV_ROOT_FWD@@/Data/Export/maps"
OBJ_MASTER = "/Game/Materials/M_AsdaObject"

asset = _MAPS.map_asset_name  # ★ единый санитайзер имени карты (asda_maps.map_asset_name)

def read_scene(path):
    d = open(path, "rb").read(); o = 4
    o += 4                                   # ver
    n = struct.unpack_from("<I", d, o)[0]; o += 4 + n   # name
    fog = struct.unpack_from("<I", d, o)[0]; o += 4
    fs, fe = struct.unpack_from("<ff", d, o); o += 8
    amb = struct.unpack_from("<3f", d, o); o += 12
    dif = struct.unpack_from("<3f", d, o); o += 12
    ld  = struct.unpack_from("<3f", d, o); o += 12
    return dict(fog=fog, ambient=amb, diffuse=dif, lightDir=ld)

# ★ Список карт — ЕДИНЫЙ источник asda_maps.py (все 22 карты порта).
#   Было: локальный литерал (13 копий по тулам). Состав сверен 1:1.
MAPS = _MAPS.ALL_MAPS

total = 0


def main():
    """Работа скрипта. Вынесена в функцию, чтобы импорт модуля НИЧЕГО не делал
    (закон [[tools-no-import-side-effects]])."""
    global ld, total
    for mp in MAPS:
        raw = os.path.join(RAW, mp + ".mapraw")
        if not os.path.isfile(raw) or os.path.getsize(raw) < 1000:
            continue
        sc = read_scene(raw)
        a = sc["ambient"]                                       # ★ = ЦВЕТ направленного света карты (дамп-доказано)
        ld = sc["lightDir"]
        # Эталон (дамп alpen): MatAmbient=0.6 (fill), directional-цвет = scene.ambient (тёплый). Направление = lightDir.
        AMBIENT_FILL = 0.6
        lcol  = unreal.LinearColor(a[0], a[1], a[2], 0.0)       # per-map directional ЦВЕТ (тёплый/холодный/тёмный)
        ldcol = unreal.LinearColor(ld[0], ld[1], ld[2], 0.0)    # RAW scene.lightDir
        ACCEPT = ("M_AsdaObject", "M_AsdaTerrain")              # объекты И террейн
        cnt = 0
        for sub in ("Materials", "Anims/_mats", "Terrain"):
            # ★ 16.09.2026 (обратный обход ревизии savedir12): пишет MIC под корнем карты — спрашиваем гейт.
            import asda_login_guard as _LG
            if _LG.login_map_write_blocked("/Game/Map/%s" % asset(mp), "asda_set_map_lighting.py", quiet=True):
                continue
            base = "/Game/Map/%s/%s" % (asset(mp), sub)
            for ad in ar.get_assets_by_path(base, recursive=True):
                if str(ad.asset_class_path.asset_name) != "MaterialInstanceConstant":
                    continue
                mic = EAL.load_asset(str(ad.package_name))
                par = mic.get_editor_property("parent")
                if not (par and par.get_name() in ACCEPT):
                    continue                                    # water/sky — другие мастера, пропускаем
                MEL.set_material_instance_scalar_parameter_value(mic, "Ambient", AMBIENT_FILL)
                MEL.set_material_instance_vector_parameter_value(mic, "LightColor", lcol)
                MEL.set_material_instance_vector_parameter_value(mic, "LightDir", ldcol)
                EAL.save_loaded_asset(mic)
                cnt += 1
        total += cnt
        unreal.log("СВЕТ %-20s LightColor=(%.2f,%.2f,%.2f) Ambient=%.2f dir=(%.2f,%.2f,%.2f) MIC=%d"
                   % (mp, a[0], a[1], a[2], AMBIENT_FILL, ld[0], ld[1], ld[2], cnt))
    unreal.log("=== ОСВЕЩЕНИЕ ГОТОВО: обновлено MIC=%d на %d картах ===" % (total, len(MAPS)))


# ★ ЗАКОН [[tools-no-import-side-effects]]: при импорте модуль НИЧЕГО не делает.
#   `__spec__` выставляет ЛЮБАЯ машинерия импорта (import / importlib / runpy) и НЕ
#   выставляет прямое исполнение файла (python file.py, UE -ExecutePythonScript).
#   `python -m` даёт `__name__ == "__main__"` и покрыт первым условием.
if __name__ == "__main__" or globals().get("__spec__") is None:
    main()
