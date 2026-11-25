# -*- coding: utf-8 -*-
# Copyright (c) 2026 @sitxovski (Discord)
r"""
replace_all_assets.py — массовая замена РЕСУРСОВ (текстуры, меши, анимации, эффекты, звуки)
из Фантазиума в клиент СТРОГО по полному совпадению имени.

Что заменяется (по расширению):
    .tex .dds .tga .png .bmp .jpg   — текстуры
    .jbm                            — меши/модели
    .jbb                            — скелеты и анимации
    .jeb .jet .jem                  — модели и данные эффектов
    .wav .mp3 .ogg                  — звуки
Работает и с распакованными файлами (совпадение по относительному пути), и с содержимым
архивов b**z (совпадение по имени записи внутри архива). Сопоставление регистронезависимое.

ЧТО НЕ ТРОГАЕТСЯ (осознанно):
  * таблицы .bin/.BIN/.sif/.ssi/.mst/.msif — там лежит переведённый текст, замена его откатит;
  * геометрия и расстановка карт: .nbb .mbb .nlk .mlk .pos .pbb .sim .ter .tvb .tib .lin
    .ani .npc .mon .tel .sce .wat .seq — у клиента этих данных БОЛЬШЕ (например alpen.nbb
    2.81 МБ против 2.70 МБ), замена урезала бы контент карт и рассогласовала расстановку;
  * .exe/.dll/.ini/.txt/.JUI/.JUS — исполняемые файлы и настройки.

Перед записью каждый заменяемый файл/архив копируется в BACKUP с сохранением структуры,
так что откат возможен целиком.
"""
import os, io, sys, shutil, zipfile

CLIENT = r"@@ASTORY_DEV_ROOT@@\Donors\AsdaChroniclesInterlude_MINE"
FANTA = r"@@ASTORY_DEV_ROOT@@\Donors\Fantazium"
BACKUP = r"@@ASTORY_DEV_ROOT@@\Donors\TRANSLATE_unpacked\backup_assets"
REPORT = r"@@ASTORY_DEV_ROOT@@\AstoryClient\Tools\Translate\reports\replace_all_assets.txt"

ASSET_EXT = {".tex", ".dds", ".tga", ".png", ".bmp", ".jpg",
             ".jbm", ".jbb", ".jeb", ".jet", ".jem",
             ".wav", ".mp3", ".ogg"}
ARCHIVE_EXT = {".btz", ".bnz", ".baz", ".bbz", ".bsz"}
SKIP_DIRS = {"temp", "rutemp", "installer", "screen save", "backup_assets"}

# ИСКЛЮЧЕНИЯ: ресурсы, которые у Фантазиума ХУЖЕ, чем у клиента, и переносить их нельзя.
# Имена записей внутри архивов (в нижнем регистре).
EXCLUDE_ENTRIES = {
    # Атлас с кнопками ESC-меню (выход/перезапуск) и надписями режима шахт: у Фантазиума он
    # унаследован от АРАБСКОЙ сборки (خروج / إعادة تشغيل), т.е. замена делает интерфейс арабским.
    # Русского варианта нет ни в одном источнике, поэтому оставляем английский оригинал клиента.
    "n_chapter.tex",
}
# Исключения для распакованных файлов — по относительному пути в нижнем регистре.
EXCLUDE_FILES = set()

def rel_index(root):
    """{относительный путь в нижнем регистре: реальный путь} для файлов-ресурсов и архивов."""
    out = {}
    for dirpath, dirnames, files in os.walk(root):
        low = dirpath.lower()
        if any(os.sep + s in low for s in SKIP_DIRS):
            continue
        for fn in files:
            ext = os.path.splitext(fn)[1].lower()
            if ext in ASSET_EXT or ext in ARCHIVE_EXT:
                p = os.path.join(dirpath, fn)
                out[os.path.relpath(p, root).lower()] = p
    return out

def backup(path, root):
    dst = os.path.join(BACKUP, os.path.relpath(path, root))
    if not os.path.isfile(dst):
        os.makedirs(os.path.dirname(dst), exist_ok=True)
        shutil.copy2(path, dst)

def merge_archive(cpath, fpath, apply_it, stats, log):
    """Заменить внутри архива клиента записи, чьи имена есть в архиве Фантазиума."""
    try:
        zc = zipfile.ZipFile(cpath); zf = zipfile.ZipFile(fpath)
    except Exception as e:
        log.append("%s\tОШИБКА открытия: %s" % (cpath, e)); return
    fmap = {n.lower(): n for n in zf.namelist()}
    plan = []
    skipped_here = 0
    for info in zc.infolist():
        if info.is_dir(): continue
        ext = os.path.splitext(info.filename)[1].lower()
        if ext not in ASSET_EXT: continue
        low = info.filename.lower()
        if low in EXCLUDE_ENTRIES or os.path.basename(low) in EXCLUDE_ENTRIES:
            skipped_here += 1
            continue
        src = fmap.get(low)
        if src is None: continue
        if zf.read(src) != zc.read(info): plan.append((info.filename, src))
    if skipped_here:
        stats["excluded"] += skipped_here
        log.append("%s\tпропущено по списку исключений: %d" % (os.path.relpath(cpath, CLIENT), skipped_here))
    if not plan:
        zc.close(); zf.close(); return
    stats["arch_entries"] += len(plan)
    stats["arch_files"] += 1
    for a, b in plan[:0]: pass
    log.append("%s\tзаменяется записей: %d" % (os.path.relpath(cpath, CLIENT), len(plan)))
    if not apply_it:
        zc.close(); zf.close(); return
    backup(cpath, CLIENT)
    repl = {a: zf.read(b) for a, b in plan}
    tmp = cpath + ".tmp"
    with zipfile.ZipFile(tmp, "w", zipfile.ZIP_DEFLATED) as zout:
        for info in zc.infolist():
            data = repl.get(info.filename)
            if data is None: data = zc.read(info)
            zi = zipfile.ZipInfo(info.filename, date_time=info.date_time)
            zi.compress_type = zipfile.ZIP_DEFLATED
            zi.external_attr = info.external_attr
            zout.writestr(zi, data)
    zc.close(); zf.close()
    os.replace(tmp, cpath)

def main():
    apply_it = "--apply" in sys.argv
    print("индексирую файлы…")
    C = rel_index(CLIENT)
    F = rel_index(FANTA)
    print("ресурсов у клиента: %d | у Фантазиума: %d" % (len(C), len(F)))
    stats = dict(loose=0, loose_bytes=0, arch_files=0, arch_entries=0, same=0, nopair=0, excluded=0)
    log = []
    for rel, cpath in sorted(C.items()):
        ext = os.path.splitext(rel)[1].lower()
        if rel in EXCLUDE_FILES or os.path.basename(rel) in EXCLUDE_ENTRIES:
            stats["excluded"] += 1
            log.append("%s\tпропущен по списку исключений" % rel)
            continue
        fpath = F.get(rel)
        if fpath is None:
            stats["nopair"] += 1
            continue
        if ext in ARCHIVE_EXT:
            merge_archive(cpath, fpath, apply_it, stats, log)
            continue
        a = open(cpath, "rb").read(); b = open(fpath, "rb").read()
        if a == b:
            stats["same"] += 1
            continue
        stats["loose"] += 1; stats["loose_bytes"] += len(b)
        if apply_it:
            backup(cpath, CLIENT)
            shutil.copy2(fpath, cpath)
    print("\n%-40s %s" % ("распакованных файлов заменено:", stats["loose"]))
    print("%-40s %.1f МБ" % ("их объём:", stats["loose_bytes"] / 1048576))
    print("%-40s %d в %d архивах" % ("записей в архивах заменено:", stats["arch_entries"], stats["arch_files"]))
    print("%-40s %s" % ("совпадали побайтово (не тронуты):", stats["same"]))
    print("%-40s %s" % ("нет пары у Фантазиума:", stats["nopair"]))
    print("%-40s %s" % ("пропущено по исключениям:", stats["excluded"]))
    os.makedirs(os.path.dirname(REPORT), exist_ok=True)
    with io.open(REPORT, "w", encoding="utf-8-sig") as f:
        f.write("# Массовая замена ресурсов из Фантазиума (по полному совпадению имён)\n")
        f.write("# распакованных=%d, записей в архивах=%d, архивов=%d, идентичны=%d, без пары=%d\n\n"
                % (stats["loose"], stats["arch_entries"], stats["arch_files"], stats["same"], stats["nopair"]))
        f.write("\n".join(log))
    if not apply_it:
        print("\nсухой прогон. Применить: --apply (бэкап в %s)" % BACKUP)
    else:
        print("\nприменено. Бэкап оригиналов: %s" % BACKUP)
    print("отчёт: %s" % REPORT)

if __name__ == "__main__":
    main()
