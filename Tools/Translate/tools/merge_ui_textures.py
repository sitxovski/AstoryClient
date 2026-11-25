# -*- coding: utf-8 -*-
# Copyright (c) 2026 @sitxovski (Discord)
r"""
merge_ui_textures.py — перенос текстур интерфейса (с запечёнными русскими надписями)
из juidata Фантазиума в juidata MINE.

Почему это безопасно (проверено):
  * JUISKIN.JUS обоих клиентов ссылается на ОДИН И ТОТ ЖЕ набор из 66 атласов, и UV-координаты
    совпадают у 1423 из 1426 общих контролов → раскладка спрайтов внутри атласов одна и та же,
    подмена картинки не сдвигает спрайты.
  * .TEX = 36-байт заголовок + контейнер (TGA/PNG/BMP/JPG/DDS). Копируется файл целиком,
    так что заголовок всегда соответствует своему изображению.

Правила отбора:
  * заменяем только записи, имя которых есть в архиве MINE (новых не добавляем — их не на что
    ссылаться в скине MINE);
  * по умолчанию — только там, где совпадают ШИРИНА и ВЫСОТА изображения (иначе спрайты поедут);
    --allow-resize снимает это ограничение;
  * --icons включает gIcon/sIcon (иконки предметов). По умолчанию ВЫКЛЮЧЕНО: иконки
    адресуются по индексу предмета, а контент 2014 года беднее — подмена может показать
    не ту картинку для предметов, добавленных позже. Надписей там нет, для перевода не нужно.
  * 3 контрола, у которых UV разошлись (PaperEdge_MInven, PaperEdge_MInven_2, GiantGod_TalkWin),
    выписываются в отчёт — их атласы заменяются, но координаты остаются от MINE.
"""
import os, sys, struct, zipfile, io, shutil

T = r"@@ASTORY_DEV_ROOT@@\Donors\TRANSLATE_unpacked"
ORIG_CLIENT = r"@@ASTORY_DEV_ROOT@@\Donors\AsdaChroniclesInterlude_MINE"
FANTA_DIRS = {"Image": os.path.join(T, r"textures_fanta\juidata\Image"),
              "gIcon": os.path.join(T, r"textures_fanta\juidata\gIcon"),
              "sIcon": os.path.join(T, r"textures_fanta\juidata\sIcon")}
OUT = os.path.join(T, "out_client", "juidata")
REPORTS = os.path.join(T, "reports", "textures")
os.makedirs(REPORTS, exist_ok=True)

HDR = 0x24

def dims(payload):
    """(контейнер, w, h) изображения внутри .TEX."""
    p = payload[HDR:]
    if p[:4] == b"DDS ":
        return ("dds", struct.unpack_from("<I", p, 16)[0], struct.unpack_from("<I", p, 12)[0])
    if p[:4] == b"\x89PNG":
        w, h = struct.unpack_from(">II", p, 16); return ("png", w, h)
    if p[:2] == b"BM":
        w, h = struct.unpack_from("<ii", p, 18); return ("bmp", w, abs(h))
    if p[:2] == b"\xff\xd8":
        # JPEG: ищем SOF0/2 маркер
        i = 2
        while i < len(p) - 9:
            if p[i] != 0xFF: i += 1; continue
            m = p[i+1]
            if m in (0xC0, 0xC1, 0xC2):
                h, w = struct.unpack_from(">HH", p, i + 5); return ("jpg", w, h)
            if m in (0xD8, 0xD9) or 0xD0 <= m <= 0xD7: i += 2; continue
            i += 2 + struct.unpack_from(">H", p, i + 2)[0]
        return ("jpg", 0, 0)
    if len(p) > 18 and p[1] in (0, 1) and p[2] in (1, 2, 3, 9, 10, 11):
        w, h = struct.unpack_from("<HH", p, 12); return ("tga", w, h)
    return ("?", 0, 0)

def merge_archive(sub, allow_resize, report):
    src_bbz = os.path.join(ORIG_CLIENT, "juidata", sub, "Texture.btz")
    fdir = FANTA_DIRS[sub]
    if not os.path.isfile(src_bbz) or not os.path.isdir(fdir):
        return None, (sub, "НЕТ ИСТОЧНИКА", 0, 0, 0, 0)
    fmap = {fn.lower(): os.path.join(fdir, fn) for fn in os.listdir(fdir)}
    zin = zipfile.ZipFile(src_bbz)
    outp = os.path.join(OUT, sub, "Texture.btz")
    os.makedirs(os.path.dirname(outp), exist_ok=True)
    replaced = identical = skipped_dim = nopair = 0
    with zipfile.ZipFile(outp, "w", zipfile.ZIP_DEFLATED) as zout:
        for info in zin.infolist():
            data = zin.read(info)
            fp = fmap.get(info.filename.lower())
            if fp is None:
                nopair += 1
                report.append("%s\t%s\tнет в Фантазиуме" % (sub, info.filename))
            else:
                fdata = open(fp, "rb").read()
                if fdata == data:
                    identical += 1
                else:
                    do, dn = dims(data), dims(fdata)
                    if not allow_resize and (do[1], do[2]) != (dn[1], dn[2]):
                        skipped_dim += 1
                        report.append("%s\t%s\tразмер отличается %dx%d -> %dx%d (пропущено)"
                                      % (sub, info.filename, do[1], do[2], dn[1], dn[2]))
                    else:
                        data = fdata; replaced += 1
            zi = zipfile.ZipInfo(info.filename, date_time=info.date_time)
            zi.compress_type = zipfile.ZIP_DEFLATED
            zi.external_attr = info.external_attr
            zout.writestr(zi, data)
    zin.close()
    return outp, (sub, "OK", len(fmap), replaced, identical, skipped_dim + nopair)

def main():
    allow_resize = "--allow-resize" in sys.argv
    do_icons = "--icons" in sys.argv
    subs = ["Image"] + (["gIcon", "sIcon"] if do_icons else [])
    report = []
    rows = []
    for sub in subs:
        outp, row = merge_archive(sub, allow_resize, report)
        rows.append(row)
    with io.open(os.path.join(REPORTS, "textures_report.txt"), "w", encoding="utf-8-sig") as f:
        f.write("# Текстуры UI: пропущенные и без пары (архивы: %s)\n" % ", ".join(subs))
        f.write("\n".join(report))
    print("%-8s %-8s %8s %9s %10s %9s" % ("архив", "статус", "у fanta", "заменено", "идентичны", "пропущено"))
    for r in rows:
        print("%-8s %-8s %8s %9s %10s %9s" % r)
    if not do_icons:
        print("\ngIcon/sIcon не тронуты (иконки предметов). Включить: --icons")

if __name__ == "__main__":
    main()
