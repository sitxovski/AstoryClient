# Copyright (c) 2026 @sitxovski (Discord)
# asda_dump_peb_sets.py — ИНВЕНТАРИЗАЦИЯ .PEB (наборы собственных эффектов ПРЕДМЕТА ЭКИПИРОВКИ).
#
# ЭТАЛОН: effLoadPebEffectSet@0x9EDA00 (effects_particles.c:3196) — вызывается при постановке КАЖДОЙ
#   части экипа (entity_char.c:12030/12248/12463/12778/13076/13374/13674/14385/14795 — a3 = слот 0..6,
#   где 6 = ОРУЖИЕ), имя файла = "<имя модели предмета>.PEB" (ImgRecord+4 муж / +68 жен).
#   Формат: magic u32 = 0x20060810, затем РОВНО 3 записи по 260 б: i32 nodeIdx + char[256] имя .jeb.
#   nodeIdx == -1 или пустое имя → запись пропускается.
#   Эффект грузится в свой CEffectMgr, а КАЖДЫЙ КАДР (entity_char.c:22394-22402) получает матрицу
#     W = CChar_GetClassItemByIndex(pChar+2, nodeIdx) × worldMatrix(pChar+92)
#   т.е. nodeIdx индексирует КОСТЬ ПЕРСОНАЖА (тот же список, что у крыльев/свечения), а НЕ узел
#   модели предмета. Рендер — CRenderQueue_AddNode (entity_char.c:8969).
#
# ВЫВОД (данные этой сборки клиента): .PEB есть ТОЛЬКО у 24 файлов брони (chst/grov/hcap/shoe 305,
#   м/ж × 3 варианта) — У ОРУЖИЯ .PEB НЕТ НИ ОДНОГО ⇒ «собственных» эффектов оружия в оригинале нет,
#   светящееся оружие = эффект ЗАТОЧКИ (CCharacter_UpdateHeldItemEffect), см. weapon_glow_re.md.
#
# Запуск: python asda_dump_peb_sets.py [каталог_моделей]
import os, sys, struct, glob
import os as _os, sys as _sys
_sys.path.insert(0, _os.path.dirname(_os.path.abspath(__file__)))
import asda_paths as _P

MODEL_DIR = _P.CHAR_MODEL_ORIG_DIR
FX_DIR    = _P.EFFECT_MODEL_ORIG_DIR
PEB_MAGIC = 0x20060810
REC_SIZE  = 4 + 256          # i32 nodeIdx + char[256] имя .jeb
REC_COUNT = 3                # эталон читает РОВНО 3 записи


def parse_peb(path):
    """→ [(nodeIdx, jebName), ...] (только валидные записи) либо None, если файл не .PEB."""
    d = open(path, "rb").read()
    if len(d) < 4 or struct.unpack_from("<I", d, 0)[0] != PEB_MAGIC:
        return None
    out = []
    for i in range(REC_COUNT):
        o = 4 + REC_SIZE * i
        if o + REC_SIZE > len(d):
            break
        idx = struct.unpack_from("<i", d, o)[0]
        nm = d[o + 4:o + 4 + 256].split(b"\x00")[0].decode("latin1", "ignore").strip()
        if idx == -1 or not nm:
            continue
        out.append((idx, nm))
    return out


def main(model_dir=MODEL_DIR):
    # NTFS регистронезависима: два шаблона дают дубли — дедуплицируем по нижнему регистру пути.
    seen, files = set(), []
    for p in sorted(glob.glob(os.path.join(model_dir, "*.peb")) + glob.glob(os.path.join(model_dir, "*.PEB"))):
        if p.lower() in seen:
            continue
        seen.add(p.lower())
        files.append(p)
    print("Найдено .PEB: %d (каталог %s)" % (len(files), model_dir))
    total = miss = 0
    for p in files:
        recs = parse_peb(p)
        if recs is None:
            print("  %-34s НЕ .PEB (magic не 0x20060810)" % os.path.basename(p))
            continue
        for idx, nm in recs:
            total += 1
            base = nm[:-4] if nm.lower().endswith(".jeb") else nm
            have = os.path.isfile(os.path.join(FX_DIR, base + ".jeb"))
            if not have:
                miss += 1
            print("  %-34s кость#%-3d %-38s %s" % (os.path.basename(p), idx, nm, "ok" if have else "НЕТ .jeb"))
    print("ИТОГО записей: %d, из них без ассета .jeb: %d" % (total, miss))


if __name__ == "__main__":
    main(sys.argv[1] if len(sys.argv) > 1 else MODEL_DIR)
