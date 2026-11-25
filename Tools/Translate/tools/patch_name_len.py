# -*- coding: utf-8 -*-
# Copyright (c) 2026 @sitxovski (Discord)
r"""
patch_name_len.py — снимает обрезку русских строк в шифрованных таблицах Bin.bbz.

ПРИЧИНА ОБРЕЗКИ: 'я' = 0xFF (CP1251); шифр строк «+1» превращает её в 0x00, а загрузчик берёт
длину ДО дешифровки:
      push  <адрес поля>
      call  strlen              ; 0x93BB78  <- останавливается на первой 'я'
      add   esp, 4
      mov   [ebp-X], eax
ПАТЧ: эти 9 байт заменяются на `mov eax, <размер поля>` + 4×nop, т.е. дешифруется ВСЁ поле
целиком. Регистр/стек не затрагиваются, дальше код идёт без изменений.

ЧТОБЫ СТРОКА ПРАВИЛЬНО ЗАКАНЧИВАЛАСЬ, данные перекодируются отдельно (repack_names.py):
хвост поля после имени заполняется 0x01 — после дешифровки это даёт 0x00, корректный
терминатор C-строки. Маркер конца имени Фантазиума (0x7F) при этом убирается.

Размеры полей взяты из проверенных раскладок REGISTRY (@@ASTORY_DEV_ROOT@@\AstoryClient\Tools\ItemDB\decode_item_db.py),
поэтому дешифровка не выходит за границу своего поля.
"""
import os, struct, shutil, sys

EXE = r"@@ASTORY_DEV_ROOT@@\Donors\AsdaChroniclesInterlude_MINE\system\AsdaStory.exe"
STRLEN = 0x93BB78

# VA инструкции `call strlen` -> (размер поля, что это)
# Размеры не выходят за границу своего поля: проверено по раскладкам REGISTRY
# (напр. item_tooltip: тело записи 204 = ItemCode 4 + Description 200).
SITES = {
    0x9C6B32: (64,  "item_list.ItemName"),
    0x9BD27E: (64,  "item_list.ItemName (неактивный дубль загрузчика)"),
    0x9BE80E: (32,  "sowell_list.Name"),
    0x9BF92C: (32,  "special_itemlist.Name"),
    0x9C0EE7: (64,  "title_list.Name"),
    0x9C1091: (64,  "title_list.Desc"),
    # тултип-таблицы — грузятся отдельным менеджером (объекты ecx+0x2ef938/93c/954)
    0xEC7AF3: (200, "item_tooltip.Description"),
    0xEC6323: (200, "sowell_tooltip.Text"),
    0xEC7023: (32,  "specialitem_tooltiplist.Name"),
    0xEC70C4: (180, "specialitem_tooltiplist.Tooltip"),
}

def pe_map(data):
    off = struct.unpack_from('<I', data, 0x3C)[0]
    nsec = struct.unpack_from('<H', data, off + 6)[0]
    optsz = struct.unpack_from('<H', data, off + 20)[0]
    base = struct.unpack_from('<I', data, off + 24 + 28)[0]
    secs = []
    for i in range(nsec):
        o = off + 24 + optsz + i * 40
        vsz, va, rsz, ptr = struct.unpack_from('<IIII', data, o + 8)
        secs.append((va, vsz, ptr, rsz))
    def va2off(va):
        r = va - base
        for sva, vsz, ptr, rsz in secs:
            if sva <= r < sva + max(vsz, rsz): return ptr + (r - sva)
    return va2off

def main():
    apply_it = "--apply" in sys.argv
    d = bytearray(open(EXE, 'rb').read())
    va2off = pe_map(d)
    plan = []
    for va, (size, what) in sorted(SITES.items()):
        o = va2off(va)
        # ожидаем: [o-1]=push reg (0x50..0x57), [o]=E8 rel32 -> strlen, [o+5..o+7]=83 C4 04
        push = d[o-1]
        callop = d[o]
        rel = struct.unpack_from('<i', d, o + 1)[0]
        tgt = va + 5 + rel
        addesp = bytes(d[o+5:o+8])
        ok = (0x50 <= push <= 0x57) and callop == 0xE8 and tgt == STRLEN and addesp == b"\x83\xC4\x04"
        if not ok:
            # уже пропатчено?
            if d[o-1] == 0xB8:
                plan.append((va, size, what, "УЖЕ ПРОПАТЧЕНО", None)); continue
            plan.append((va, size, what, "НЕ СОВПАЛА СИГНАТУРА — пропуск", None)); continue
        old = bytes(d[o-1:o+8])
        new = b"\xB8" + struct.pack("<I", size) + b"\x90\x90\x90\x90"
        assert len(old) == len(new) == 9
        plan.append((va, size, what, "готов", (o - 1, old, new)))

    print("%-12s %-6s %-46s %s" % ("VA", "длина", "поле", "статус"))
    for va, size, what, st, _ in plan:
        print("0x%08X %-6d %-46s %s" % (va, size, what, st))

    todo = [p for p in plan if p[4]]
    if not apply_it:
        print("\nсухой прогон. Применить: --apply  (будет создан AsdaStory.exe.orig)")
        return
    if not todo:
        print("\nнечего применять"); return
    bak = EXE + ".orig"
    if not os.path.isfile(bak):
        shutil.copy2(EXE, bak); print("\nбэкап EXE: %s" % os.path.basename(bak))
    for va, size, what, st, (off, old, new) in todo:
        d[off:off+9] = new
    open(EXE, 'wb').write(bytes(d))
    print("применено патчей: %d" % len(todo))

if __name__ == "__main__":
    main()
