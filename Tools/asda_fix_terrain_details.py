# Copyright (c) 2026 @sitxovski (Discord)
# asda_fix_terrain_details.py — привести Detail0/Detail1/Scale0/Scale1/HasD1 у MIT_patch_N к данным
# ПРАВИЛЬНОГО <map>/<map>.TER.
#
# Причина правки: экспортёр .mapraw (Tools/MapViewer, asda_formats.cpp) выбирал .TER как «первый
# .ter по итератору каталога». В трёх папках лежит лишний .TER карты-донора, алфавитно раньше своего:
#   castle_of_chess/alpen.TER, nightvalley/conquester.TER, quizstage/OX_Quiz.TER
# ⇒ в .mapraw попадали чужие имена тайлов. Для castle_of_chess это TILE0/TILE3 — их в папке карты
# НЕТ, поэтому Detail0 оставался дефолтным (жалоба «отсутствие текстур у террейна»); для nightvalley
# тайлы conquester в папке есть, поэтому террейн рисовался ЧУЖИМИ текстурами молча.
# Экспортёр исправлен (берёт <map>.TER, регистронезависимо), этот скрипт правит уже собранные ассеты
# БЕЗ ре-экспорта карты — трогаются только 5 параметров материала патча.
#
# Вход: export/maps/ter_details.json (пишет Tools/asda_dump_ter_details.py).
# Сверяются ВСЕ карты, правятся только расходящиеся патчи.
#
# Запуск (headless):
#   UnrealEditor-Cmd.exe <проект> -ExecutePythonScript=".../asda_fix_terrain_details.py"
#     -unattended -nopause -nosplash -nosound -NullRHI
import os, json

JSON_PATH = os.environ.get("ASDA_TER_JSON", r"@@ASTORY_DEV_ROOT_FWD@@/Data/Export/maps/ter_details.json")
DRY = os.environ.get("ASDA_DRY", "") not in ("", "0")


def _pick_tex(eal, root, map_name, leaf):
    """Найти текстуру тайла. ПОРЯДОК ВАЖЕН: сначала библиотека (единый источник после сведения
    дубликатов), потом папка карты. Так исправление не плодит копий."""
    for cand in ("/Game/AsdaAssets/Textures/Maps/%s/%s" % (map_name, leaf),
                 "/Game/AsdaAssets/Textures/Maps/%s/Texture/%s" % (map_name, leaf),
                 "%s/Texture/%s" % (root, leaf)):
        if eal.does_asset_exist(cand):
            a = eal.load_asset(cand)
            if a is not None and a.get_class().get_name() != "ObjectRedirector":
                return a
    return None


def main():
    import unreal
    import sys as _sys
    _sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
    import asda_maps as _MAPS
    import asda_login_guard as _LG   # ★ 16.09.2026 (обратный обход savedir14): единый гейт сцены логина

    eal = unreal.EditorAssetLibrary
    mel = unreal.MaterialEditingLibrary
    data = json.load(open(JSON_PATH, encoding="utf-8"))
    unreal.log("=== карт в справке: %d%s ===" % (len(data), " (СУХОЙ ПРОГОН)" if DRY else ""))

    total_fixed = 0
    for map_name in sorted(data):
        asset = _MAPS.map_asset_name(map_name)
        root = "/Game/Map/%s" % asset
        # ★ 16.09.2026 (обратный обход ревизии savedir14): шаг правит и СОХРАНЯЕТ MIT_patch_N под корнем
        #   карты — гейт сцены логина ДО save_loaded_asset. Имена карт приходят из ter_details.json и
        #   реестром не сверяются.
        if _LG.login_map_write_blocked(root, "asda_fix_terrain_details.py", quiet=True):
            continue
        if not eal.does_directory_exist(root):
            unreal.log_warning("  %-22s нет папки %s" % (map_name, root))
            continue
        patches = data[map_name]["patches"]
        fixed = miss = 0
        details = []
        for pi, p in enumerate(patches):
            mp = "%s/Materials/MIT_patch_%d" % (root, pi)
            if not eal.does_asset_exist(mp):
                continue
            mic = eal.load_asset(mp)
            if not isinstance(mic, unreal.MaterialInstanceConstant):
                continue
            changed = False
            why = []
            for pname, sname, tex_leaf, scale in (("Detail0", "Scale0", p["detail0"], p["scale0"]),
                                                  ("Detail1", "Scale1", p["detail1"], p["scale1"])):
                cur = mel.get_material_instance_texture_parameter_value(mic, pname)
                cur_name = cur.get_name() if cur else ""
                # ★★★ СРАВНЕНИЕ ПО ИМЕНИ, а не по пути пакета. Иначе шаг «сведение дубликатов»
                #   откатывается: у части карт параметр уже указывает на библиотечный
                #   /Game/AsdaAssets/Textures/Maps/<map>/TILE2, и подстановка одноимённой копии
                #   /Game/Map/<map>/Texture/TILE2 вернула бы дубль (262 патча «TILE2→TILE2»).
                if tex_leaf and cur_name.lower() != tex_leaf.lower():
                    want = _pick_tex(eal, root, map_name, tex_leaf)
                    if want is None:
                        miss += 1
                        if miss <= 3:
                            details.append("патч %d: не нашёл текстуру %s" % (pi, tex_leaf))
                    else:
                        if not DRY:
                            mel.set_material_instance_texture_parameter_value(mic, pname, want)
                        why.append("%s %s→%s" % (pname, cur_name or "НЕТ", want.get_name()))
                        changed = True
                cur_s = mel.get_material_instance_scalar_parameter_value(mic, sname)
                tgt_s = scale or 1.0
                if abs(cur_s - tgt_s) > 1e-4:
                    if not DRY:
                        mel.set_material_instance_scalar_parameter_value(mic, sname, tgt_s)
                    why.append("%s %.2f→%.2f" % (sname, cur_s, tgt_s))
                    changed = True
            # ★ HasD1 — вес второго слоя: 1 только если второй тайл реально назван И импортирован
            d1_ok = bool(p["detail1"]) and _pick_tex(eal, root, map_name, p["detail1"]) is not None
            tgt_h = 1.0 if d1_ok else 0.0
            cur_h = mel.get_material_instance_scalar_parameter_value(mic, "HasD1")
            if abs(cur_h - tgt_h) > 1e-4:
                if not DRY:
                    mel.set_material_instance_scalar_parameter_value(mic, "HasD1", tgt_h)
                why.append("HasD1 %.0f→%.0f" % (cur_h, tgt_h))
                changed = True
            if changed:
                fixed += 1
                if len(details) < 6:
                    details.append("патч %d: %s" % (pi, ", ".join(why)))
                if not DRY:
                    eal.save_loaded_asset(mic)
        if fixed or miss:
            unreal.log("  %-22s исправлено патчей %2d, пропущенных текстур %d" % (map_name, fixed, miss))
            for dd in details[:6]:
                unreal.log("      %s" % dd)
            total_fixed += fixed
    unreal.log("=== ГОТОВО: патчей приведено к .TER: %d ===" % total_fixed)


if __name__ == "__main__":
    main()
