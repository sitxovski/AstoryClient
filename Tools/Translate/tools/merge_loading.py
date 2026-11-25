# -*- coding: utf-8 -*-
# Copyright (c) 2026 @sitxovski (Discord)
r"""
merge_loading.py — русификация ЗАГРУЗОЧНЫХ ЭКРАНОВ.

Механизм выбора экрана (реверс `world_map.c:120-245`, CMap::LoadLoadingScreen):
  roll = rand() % 100
  if roll < adChance:  каталог `map\load\ad_img*.*`      -> индекс = GetField2(g_dwDisplayOptionFlags) % N
  else:                каталог `map\<карта>\load\*.*`    -> индекс = rand() % N
  если подходящих файлов НЕТ -> fallback `map\load\start_logo.TEX`
Файлы отбираются ПО РАСШИРЕНИЮ, имя роли не играет (каталоги пропускаются по FILE_ATTRIBUTE_DIRECTORY).
=> достаточно заменить/убрать файлы в каталоге; переименование ничего не ломает.

Что делает скрипт:
  * `ad_img000..005` — заменяет русскими из Фантазиума (форматы сверяются: DDS/DXT1 1024x1024, 11 мипов);
  * `ad_img006..008` — английские, пары в Фантазиуме нет: убирает в подкаталог `_eng_backup`,
    иначе они выпадали бы примерно в трети показов (выбор равновероятный);
  * экраны карт (`map\<карта>\load`) — по умолчанию НЕ трогает, см. --maps ниже.

Про экраны карт: у обоих клиентов это ОДНИ И ТЕ ЖЕ иллюстрации локаций, текста на них нет —
отличается только логотип в шапке (у Фантазиума это его собственный логотип с адресом сайта).
Перевода такая замена не даёт, поэтому по умолчанию она выключена. Флаг --maps включает её.
"""
import os, sys, struct, shutil

CLIENT = r"@@ASTORY_DEV_ROOT@@\Donors\AsdaChroniclesInterlude_MINE"
FANTA  = r"@@ASTORY_DEV_ROOT@@\Donors\Fantazium"
HDR = 0x24

def tex_info(p):
    d = open(p, "rb").read(); pl = d[HDR:]
    if pl[:4] == b"DDS ":
        h, w = struct.unpack_from("<II", pl, 12)
        return ("dds", w, h, pl[84:88], struct.unpack_from("<I", pl, 28)[0])
    if pl[:4] == b"\x89PNG":
        w, h = struct.unpack_from(">II", pl, 16); return ("png", w, h, b"", 0)
    if pl[:2] == b"\xff\xd8": return ("jpg", 0, 0, b"", 0)
    if pl[:2] == b"BM":
        w, h = struct.unpack_from("<ii", pl, 18); return ("bmp", w, abs(h), b"", 0)
    return ("?", 0, 0, b"", 0)

def backup(dst):
    b = dst + ".translate_bak"
    if not os.path.isfile(b):
        shutil.copy2(dst, b)

def do_ads():
    O = os.path.join(CLIENT, "map", "load")
    F = os.path.join(FANTA, "map", "load")
    fm = {x.lower(): x for x in os.listdir(F)}
    replaced = moved = 0
    engdir = os.path.join(O, "_eng_backup")
    for fn in sorted(os.listdir(O)):
        low = fn.lower()
        if not low.startswith("ad_img"):
            continue
        dst = os.path.join(O, fn)
        src = fm.get(low)
        if src:
            sp = os.path.join(F, src)
            a, b = tex_info(dst), tex_info(sp)
            if a[0] != b[0] or (a[1], a[2]) != (b[1], b[2]):
                print("  ! %s: формат/размер расходится %s -> %s — заменяю (полноэкранный кадр, "
                      "атлас-координат нет)" % (fn, a[:3], b[:3]))
            backup(dst)
            shutil.copy2(sp, dst)
            replaced += 1
        else:
            os.makedirs(engdir, exist_ok=True)
            shutil.move(dst, os.path.join(engdir, fn))
            moved += 1
    print("реклама: заменено русскими=%d, убрано английских (нет пары)=%d -> %s"
          % (replaced, moved, os.path.relpath(engdir, CLIENT)))

def do_maps(only_missing=False):
    """Экраны карт. only_missing=True -> заполнить ТОЛЬКО карты, у которых каталога load нет
    вовсе (там клиент сейчас показывает фолбэк start_logo.TEX)."""
    O = os.path.join(CLIENT, "map"); F = os.path.join(FANTA, "map")
    # карты клиента ищем по САМОМУ КАТАЛОГУ КАРТЫ, а не по наличию подпапки load:
    # battlefield/inferion/quizstage существуют, но подпапки load у них нет.
    fmaps = {d.lower(): d for d in os.listdir(F) if os.path.isdir(os.path.join(F, d))}
    n_map = n_file = n_created = 0
    for d in sorted(os.listdir(O)):
        if d.lower() == "load" or not os.path.isdir(os.path.join(O, d)):
            continue
        fd = fmaps.get(d.lower())
        if not fd:
            continue
        src_dir = os.path.join(F, fd, "load")
        if not os.path.isdir(src_dir):
            continue
        src_files = [x for x in os.listdir(src_dir)
                     if x.lower().endswith(".tex") and os.path.isfile(os.path.join(src_dir, x))]
        if not src_files:
            continue
        ld = os.path.join(O, d, "load")
        existed = os.path.isdir(ld)
        if only_missing and existed:
            continue
        if not existed:
            os.makedirs(ld); n_created += 1
        else:
            bak = os.path.join(ld, "_orig_backup")
            os.makedirs(bak, exist_ok=True)
            for fn in list(os.listdir(ld)):
                p = os.path.join(ld, fn)
                if os.path.isfile(p):
                    shutil.move(p, os.path.join(bak, fn))
        for fn in src_files:
            shutil.copy2(os.path.join(src_dir, fn), os.path.join(ld, fn)); n_file += 1
        n_map += 1
    print("экраны карт: карт обработано=%d (из них создан load с нуля=%d), файлов положено=%d"
          % (n_map, n_created, n_file))

if __name__ == "__main__":
    if "--maps-only" not in sys.argv:
        do_ads()
    if "--maps" in sys.argv or "--maps-only" in sys.argv:
        do_maps(only_missing=False)
    elif "--maps-missing" in sys.argv:
        do_maps(only_missing=True)
    else:
        print("экраны карт не тронуты. --maps-missing = только пустые карты, --maps = все")
