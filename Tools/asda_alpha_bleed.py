# -*- coding: utf-8 -*-
# Copyright (c) 2026 @sitxovski (Discord)
# asda_alpha_bleed.py — заполнение цвета под прозрачностью текстур прозрачных частей снаряжения (07.11.2026, summer-hem). [ИСПРАВЛЕНИЕ]
#
# ★ ЗАЧЕМ. Под прозрачными участками текстур лежит белый (реже чёрный) фон. Оригинал смешивал край по альфе (SRCALPHA), и фон
#   был не виден. M_AsdaCharLitMask рисует непрозрачно всё с альфой от 8/255, а фильтрация и мипы смешивают цвет края с фоном —
#   у вырезанных вещей выходила белая кайма (зубцы подола Summer Festival Set и ещё 200 текстур).
# ★ ЧТО ДЕЛАЕТ. Цвет прозрачных текселей заменяется цветом ближайшего видимого текселя. Альфа не меняется, форма вырезки та же.
#     Mask  — источник цвета a >= 128, меняются тексели a < 128 (если таких источников нет — как у Blend);
#     Blend — источник a >= 8, меняются только a < 8: полутона вуали авторские.
#   Повторный прогон ничего не меняет. Список текстур и режимы — из AsdaCharAlphaParts.inl (генератор aa_gen_table.py):
#   текстура с режимами Blend и Mask у разных мешей идёт по правилу Blend.
# ★ ГДЕ ВЫЗЫВАЕТСЯ. Все записи PNG персонажа, которые попадают в таблицу: asda_extract_archive_textures.py, asda_wod_items.py.
#   После перегенерации таблицы (новые пары) — прогнать `--apply` и импортировать изменившиеся файлы.
#
# Запуск (системный Python, unreal не нужен):
#   python Tools/asda_alpha_bleed.py --check    только проверка: сколько файлов таблицы не заполнено (код 1, если есть)
#   python Tools/asda_alpha_bleed.py --apply    заполнить файлы RawAssets (альфа сверяется до записи)
# Импорт модуля ничего не делает (закон tools-no-import-side-effects).
import argparse
import os
import re
import sys

TOOLS_DIR = os.path.dirname(os.path.abspath(__file__))   # папка Tools
PROJECT_DIR = os.path.dirname(TOOLS_DIR)                 # корень проекта UE
INL_PATH = os.path.join(PROJECT_DIR, "Source", "AsdaReborn", "Private", "Characters", "AsdaCharAlphaParts.inl")   # таблица прозрачных частей
TEX_DIR = os.path.join(PROJECT_DIR, "RawAssets", "Textures", "Data", "Character", "Texture")                       # PNG текстур персонажа
ALPHA_REF = 8        # эталонная отсечка D3DRS_ALPHAREF: альфа ниже неё не рисуется
MASK_CORE = 128      # режим Mask: с этой альфы цвет считается цветом самой вещи
MODE_BLEND = 1       # режим Blend таблицы (AsdaCharAlphaParts.inl)

_modes_cache = None  # кэш таблицы {имя текстуры в нижнем регистре: режим}


def table_modes():
    """Таблица {текстура (нижний регистр): режим} из AsdaCharAlphaParts.inl; записи «любая текстура» не входят."""
    global _modes_cache
    if _modes_cache is None:
        text = open(INL_PATH, encoding="utf-8-sig").read()
        modes = {}
        for tex, mode in re.findall(r'\{ TEXT\("[^"]*"\), TEXT\("([^"]+)"\), (\d), (?:true|false) \}', text):
            key = tex.lower()
            modes[key] = min(modes.get(key, 9), int(mode))   # есть Blend — правило Blend
        _modes_cache = modes
    return _modes_cache


def bleed_rgba(rgba, mode):
    """Заполнить цвет массива HxWx4 (uint8) по режиму таблицы. Возвращает новый массив; без видимых текселей — исходный."""
    import numpy as np
    from scipy import ndimage
    alpha = rgba[..., 3]
    if (alpha >= ALPHA_REF).all():
        return rgba   # отсечённых текселей нет — кайме взяться неоткуда, полутона авторские
    core = alpha >= (ALPHA_REF if mode == MODE_BLEND else MASK_CORE)
    if not core.any():
        core = alpha >= ALPHA_REF
    if not core.any() or core.all():
        return rgba
    iy, ix = ndimage.distance_transform_edt(~core, return_distances=False, return_indices=True)
    out = rgba.copy()
    hole = ~core
    out[hole, :3] = rgba[iy[hole], ix[hole], :3]
    assert np.array_equal(out[..., 3], alpha)   # альфа обязана остаться прежней
    return out


def bleed_file(path, write=True):
    """Обработать PNG, если его текстура есть в таблице. Возвращает True, если файл не был заполнен (и при write — записан)."""
    import numpy as np
    from PIL import Image
    mode = table_modes().get(os.path.splitext(os.path.basename(path))[0].lower())
    if mode is None:
        return False
    src = np.array(Image.open(path).convert("RGBA"))
    dst = bleed_rgba(src, mode)
    if np.array_equal(src, dst):
        return False
    if write:
        Image.fromarray(dst, "RGBA").save(path, "PNG")
    return True


def main():
    ap = argparse.ArgumentParser(description="Заполнение цвета под прозрачностью у текстур таблицы прозрачных частей")
    ap.add_argument("--check", action="store_true", help="только проверить")
    ap.add_argument("--apply", action="store_true", help="заполнить файлы RawAssets")
    args = ap.parse_args()
    if args.check == args.apply:
        ap.error("нужен ровно один из ключей --check, --apply")
    sys.stdout.reconfigure(encoding="utf-8", errors="replace")
    total = changed = missing = 0
    for name in sorted(table_modes()):
        path = os.path.join(TEX_DIR, name + ".png")
        if not os.path.exists(path):
            missing += 1
            print("нет файла:", name)
            continue
        total += 1
        if bleed_file(path, write=args.apply):
            changed += 1
            if args.check:
                print("не заполнено:", name)
    print("текстур таблицы %d, файлов %d, %s %d, нет файла %d" % (
        len(table_modes()), total, "заполнено" if args.apply else "не заполнено", changed, missing))
    return 1 if args.check and changed else 0


if __name__ == "__main__":
    sys.exit(main())
