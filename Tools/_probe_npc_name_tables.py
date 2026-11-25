# Copyright (c) 2026 @sitxovski (Discord)
# _probe_npc_name_tables.py — сверка двух наборов имён NPC: DT_NpcNames (рантайм) и DT_npc_list (импорт).
# Только ЧТЕНИЕ, ничего не пишет в ассеты. Отчёт → Reports/npc_name_tables_probe.txt
import unreal

OUT = r"@@ASTORY_DEV_ROOT@@\Reports\npc_name_tables_probe.txt"


def rows(path):
    """Возвращает {rowName: [колонки]} через CSV-экспорт DataTable (единственный стабильный путь в py-API)."""
    dt = unreal.EditorAssetLibrary.load_asset(path)
    if not dt:
        return None, None
    csv = unreal.DataTableFunctionLibrary.export_data_table_to_csv_string(dt)
    lines = [l for l in csv.splitlines() if l.strip()]
    hdr = next(csv_reader([lines[0]]))
    out = {}
    for rec in csv_reader(lines[1:]):
        if rec:
            out[rec[0]] = rec
    return hdr, out


def csv_reader(lines):
    import csv as _csv
    return _csv.reader(lines)


def colval(hdr, rec, col):
    for i, h in enumerate(hdr):
        if h.strip().lower() == col.lower() and i < len(rec):
            return rec[i]
    return ""


def main():
    ha, a = rows("/Game/AsdaAssets/Data/NPC/DT_NpcNames")
    hb, b = rows("/Game/AsdaAssets/Text/NPC/DT_npc_list")
    L = []
    L.append("DT_NpcNames: %s  колонки=%s" % ("НЕ НАЙДЕН" if a is None else "%d строк" % len(a), ha))
    L.append("DT_npc_list: %s  колонки=%s" % ("НЕ НАЙДЕН" if b is None else "%d строк" % len(b), hb))
    if a and b:
        ka, kb = set(a), set(b)
        L.append("только в DT_NpcNames: %d  %s" % (len(ka - kb), sorted(ka - kb, key=lambda x: int(x) if x.isdigit() else 0)[:20]))
        L.append("только в DT_npc_list: %d  %s" % (len(kb - ka), sorted(kb - ka, key=lambda x: int(x) if x.isdigit() else 0)[:20]))
        same = diff = 0
        samples = []
        for k in sorted(ka & kb, key=lambda x: int(x) if x.isdigit() else 0):
            na, nb = colval(ha, a[k], "name"), colval(hb, b[k], "name")
            if na == nb:
                same += 1
            else:
                diff += 1
                if len(samples) < 40:
                    samples.append("  %s: NpcNames=%r  npc_list=%r" % (k, na, nb))
        L.append("общих ключей: %d — совпадает имя %d, различается %d" % (len(ka & kb), same, diff))
        L.append("пустых имён в DT_NpcNames: %d" % sum(1 for v in a.values() if not colval(ha, v, "name")))
        L.append("пустых имён в DT_npc_list: %d" % sum(1 for v in b.values() if not colval(hb, v, "name")))
        L.append("--- примеры расхождений ---")
        L += samples
        L.append("--- первые 15 строк каждой ---")
        for k in sorted(ka, key=lambda x: int(x) if x.isdigit() else 0)[:15]:
            L.append("  NpcNames[%s] = %r" % (k, a[k]))
        for k in sorted(kb, key=lambda x: int(x) if x.isdigit() else 0)[:15]:
            L.append("  npc_list[%s] = %r" % (k, b[k]))
    txt = "\n".join(L)
    with open(OUT, "w", encoding="utf-8") as f:
        f.write(txt)
    unreal.log_warning("[npc-probe] → %s\n%s" % (OUT, txt[:2000]))


if __name__ == "__main__":
    main()
