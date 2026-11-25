# -*- coding: utf-8 -*-
# Copyright (c) 2026 @sitxovski (Discord)
# asda_fx_tex_donor_import_ue.py — ИМПОРТ недостающих текстур эффектов (PNG из `asda_fx_tex_from_donor.py`) и привязка к их MI
#   (10.11.2026, job4-vfx). Запуск в редакторе без окна: python Tools/run_headless_safe.py Tools/asda_fx_tex_donor_import_ue.py
#   --marker "ЗАВЕРШЕНО: текстуры эффектов донора". ASDA_FXTEX_DRYRUN=1 — только проверка, без записи.
#
# Для каждого имени из `asda_fx_tex_from_donor.NAMES`:
#   1) если `/Game/AsdaAssets/Effects/Tex/<имя>` нет — импорт PNG общим `asda_model_import_ue.import_batch` и настройки как у
#      эталонной текстуры той же папки (`eff_1_char_01_hexaa07-b_00`: сжатие/мипы/стриминг/sRGB копируются, а не выдумываются);
#   2) у MI `/Game/AsdaAssets/Effects/Mat/MI_<имя>_add` и `MI_<имя>_add_fxb300` параметр `SpriteTexture` — эта текстура, но
#      ТОЛЬКО если там сейчас пусто или движковая DefaultTexture (своя ручная правка MI не затирается — СТОП с сообщением).
#   Ничего другого (BP эффектов, мастера, прочие MI) не трогает.
import os
import sys

import unreal

TOOLS = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, TOOLS)
import asda_fx_tex_from_donor as D  # noqa: E402  перечень имён и папка PNG

EAL = unreal.EditorAssetLibrary
MEL = unreal.MaterialEditingLibrary
TEX = "/Game/AsdaAssets/Effects/Tex"                    # папка текстур эффектов персонажа/навыков
MAT = "/Game/AsdaAssets/Effects/Mat"                    # папка их MI
REF = TEX + "/eff_1_char_01_hexaa07-b_00"               # соседняя текстура тех же эффектов (образец настроек)
PROPS = ("compression_settings", "mip_gen_settings", "never_stream", "srgb", "lod_group", "filter")   # копируемые настройки


def log(m):
    """Строка в журнал с префиксом шага."""
    unreal.log_warning("[fx-tex-donor] %s" % m)


def main():
    """Импорт + привязка. Итог — строка «ЗАВЕРШЕНО: текстуры эффектов донора …»."""
    dry = bool(os.environ.get("ASDA_FXTEX_DRYRUN"))
    ref = EAL.load_asset(REF)
    if not ref:
        log("СТОП: нет образца настроек %s" % REF)
        log("ЗАВЕРШЕНО: текстуры эффектов донора, ошибка")
        return
    done, stop = 0, 0
    for stem in D.NAMES:
        tp = "%s/%s" % (TEX, stem)
        png = os.path.join(D.OUT, stem + ".png")
        mis = ["%s/MI_%s_add" % (MAT, stem), "%s/MI_%s_add_fxb300" % (MAT, stem)]
        # Сначала проверка MI: заглушка или пусто — можно; иначе — чужая правка, имя целиком пропускаем.
        mobj = []
        ok = True
        for mp in mis:
            mic = EAL.load_asset(mp) if EAL.does_asset_exist(mp) else None
            if not mic:
                log("СТОП %s: нет %s" % (stem, mp)); ok = False; break
            tv = MEL.get_material_instance_texture_parameter_value(mic, "SpriteTexture")
            if tv is not None and tv.get_name() not in ("DefaultTexture", stem):
                log("СТОП %s: в %s уже текстура %s — не трогаю" % (stem, mp, tv.get_path_name())); ok = False; break
            mobj.append(mic)
        if not ok:
            stop += 1
            continue
        if dry:
            log("сухо %s: PNG %s, ассет %s, MI %d" % (stem, "есть" if os.path.isfile(png) else "НЕТ", "есть" if EAL.does_asset_exist(tp) else "будет", len(mobj)))
            done += 1
            continue
        if not EAL.does_asset_exist(tp):
            if not os.path.isfile(png):
                log("СТОП %s: нет PNG %s (сначала asda_fx_tex_from_donor.py)" % (stem, png)); stop += 1
                continue
            import asda_model_import_ue as MIU
            for a in MIU.import_batch([png], TEX):
                EAL.save_loaded_asset(a)
        t = EAL.load_asset(tp)
        if not isinstance(t, unreal.Texture2D):
            log("СТОП %s: импорт не дал Texture2D %s" % (stem, tp)); stop += 1
            continue
        for p in PROPS:
            t.set_editor_property(p, ref.get_editor_property(p))
        EAL.save_loaded_asset(t)
        for mic in mobj:
            MEL.set_material_instance_texture_parameter_value(mic, "SpriteTexture", t)
            EAL.save_loaded_asset(mic)
        log("%s: текстура %s (%dx%d), MI %d" % (stem, tp, t.blueprint_get_size_x(), t.blueprint_get_size_y(), len(mobj)))
        done += 1
    log("ЗАВЕРШЕНО: текстуры эффектов донора, готово %d, стоп %d%s" % (done, stop, " [СУХОЙ]" if dry else ""))


# ★ ЗАКОН [[tools-no-import-side-effects]]: при импорте модуль НИЧЕГО не делает.
if __name__ == "__main__" or globals().get("__spec__") is None:
    main()
