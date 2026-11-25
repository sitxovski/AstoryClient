# Copyright (c) 2026 @sitxovski (Discord)
# _probe_nsr_cond.py — ЗОНД: проверка раскладки блока условия в `.NSR` НА РЕАЛЬНЫХ БАЙТАХ.
#   Отвечает на три вопроса, которые нельзя решать «по документации»:
#     1) действительно ли блок условия начинается на +8 от начала под-записи (а не на +0);
#     2) какие категории условий вообще встречаются у кнопок меню и у шагов страниц;
#     3) существуют ли в данных квестовые под-блоки шага (+164, +188+140j, +1028).
#   Запуск: python _probe_nsr_cond.py   (UE не нужен)
import os, struct, sys, zipfile
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import asda_paths as _P

NSR_ZIP = os.path.join(_P.DATA, r"NPC\Episode\NPC\Nsr.bsz")
BLK_OFF, BLK_LEN = 4, 14352
OFF_ENTRY, OFF_MENU = 0x28, 0x2C
OFF_BTNS, BTN_STRIDE, BTN_COUNT = 0x2430, 848, 6
BTN_SUB_BASE, BTN_SUB_STRIDE, BTN_SUB_COUNT = 68, 156, 5
PAGE_STRIDE = 31452
STEP_BASE, STEP_STRIDE, STEP_COUNT = 16, 5228, 6
STEP_SUB_BASE, STEP_SUB_STRIDE, STEP_SUB_COUNT = 68, 1032, 5

I  = lambda b,o: struct.unpack_from("<i", b, o)[0]
U  = lambda b,o: struct.unpack_from("<H", b, o)[0]
S  = lambda b,o: struct.unpack_from("<h", b, o)[0]

def cats(b, c):
    """Порт `charEquip_CountStatCategories@0xD84C90` / `questEvalConditionFlags@0xD88990`
       в терминах СМЕЩЕНИЙ ОТ НАЧАЛА БЛОКА УСЛОВИЯ (c). Обе эталонные функции дают
       ОДИН И ТОТ ЖЕ набор — это и есть доказательство единой раскладки."""
    e = []
    if U(b,c+8) or U(b,c+6):                         e.append(0)   # уровень
    if I(b,c+12) > -1 or I(b,c+16) > -1:             e.append(1)   # класс (ветка кнопок)
    if I(b,c+24) > -1 or I(b,c+20) > -1 or I(b,c+28) > -1: e.append(2)   # квесты
    if any(I(b,c+k) > -1 for k in (84,92,100,108,116)):    e.append(3)   # предметы-цели
    if I(b,c+124) > -1:                              e.append(4)   # надетый предмет
    if I(b,c+128) > 0 or I(b,c+132) > 0:             e.append(5)   # вкладка сумки
    if U(b,c+142):                                   e.append(6)   # слот сумки
    if I(b,c+144) > 0:                               e.append(7)   # прочее
    return e

z = zipfile.ZipFile(NSR_ZIP)
names = sorted(n for n in z.namelist() if n.lower().endswith(".nsr"))
btn_state = {}
btn_cat = {}
btn_lvl_at8, btn_lvl_at0 = [], []
step_state = {}
step_cat = {}
step_lvl_ok8 = step_lvl_ok0 = 0
step_lvl_bad8 = step_lvl_bad0 = 0
obj_active = 0; obj_accept = 0; obj_nonzero_blocks = 0
step_total = 0
class_dialog_only = 0     # ClassId/-Mode == 0 — расходятся ветки «>-1» и «>0»
res_stats = {}
for n in names:
    blob = z.read(n)
    blk = blob[BLK_OFF:BLK_OFF+BLK_LEN]
    entry = struct.unpack_from("<I", blk, OFF_ENTRY)[0]
    # ── кнопки ──
    for i in range(BTN_COUNT):
        o = OFF_BTNS + BTN_STRIDE*i
        for j in range(BTN_SUB_COUNT):
            b = o + BTN_SUB_BASE + BTN_SUB_STRIDE*j
            st = U(blk, b+148)
            btn_state[st] = btn_state.get(st,0)+1
            if st == 0: continue
            for c in cats(blk, b+8): btn_cat[c] = btn_cat.get(c,0)+1
            # гипотеза «условие с +0»: те же категории, но от b
            btn_lvl_at8.append((U(blk,b+8+8), U(blk,b+8+10), U(blk,b+8+6)))
            btn_lvl_at0.append((U(blk,b+8),  U(blk,b+10),  U(blk,b+6)))
    # ── страницы ──
    roster = BLK_OFF + BLK_LEN
    if len(blob) < roster + PAGE_STRIDE*entry: continue
    for p in range(entry):
        r = roster + PAGE_STRIDE*p
        sc = S(blob, r+14)
        for k in range(max(0, min(sc, STEP_COUNT))):
            base = r + STEP_BASE + STEP_STRIDE*k
            for m in range(STEP_SUB_COUNT):
                sb = base + STEP_SUB_BASE + STEP_SUB_STRIDE*m
                st = I(blob, sb+4)
                step_total += 1
                key = st if st in (0,1,2,3,4,5) else "квест-код"
                step_state[key] = step_state.get(key,0)+1
                if st == 0: continue
                for c in cats(blob, sb+8): step_cat[c] = step_cat.get(c,0)+1
                if I(blob,sb+12+4-4) == 0: pass
                # диапазон уровня: гипотеза A (диалог) — max=cond+8, min=cond+6
                mx8, mn8 = U(blob,sb+8+8), U(blob,sb+8+6)
                # гипотеза B (метки) — max=cond+10, min=cond+8
                mx0, mn0 = U(blob,sb+8+10), U(blob,sb+8+8)
                if mx8 or mn8:
                    if mn8 <= mx8 <= 200: step_lvl_ok8 += 1
                    else: step_lvl_bad8 += 1
                if mx0 or mn0:
                    if mn0 <= mx0 <= 200: step_lvl_ok0 += 1
                    else: step_lvl_bad0 += 1
                if I(blob,sb+12) == 0 and I(blob,sb+16) == 0: class_dialog_only += 1
                # квестовые под-блоки шага
                if I(blob, sb+1028) > 0: obj_accept += 1
                for jj in range(6):
                    if I(blob, sb+164+4*jj) > 0:
                        obj_active += 1
                        P = sb+188+140*jj
                        if any(blob[P:P+140]): obj_nonzero_blocks += 1
                        break
                r0,r1,r2 = U(blob,sb+156),U(blob,sb+158),U(blob,sb+160)
                res_stats[(r0!=0,r1!=0,r2!=0)] = res_stats.get((r0!=0,r1!=0,r2!=0),0)+1

print("файлов .NSR:", len(names))
print("КНОПКИ: состояния под-записей:", dict(sorted(btn_state.items())))
print("КНОПКИ: категории условий (от +8):", dict(sorted(btn_cat.items())))
print("ШАГИ: всего под-записей:", step_total, " состояния:", step_state)
print("ШАГИ: категории условий (от +8):", dict(sorted(step_cat.items())))
print("ШАГИ: уровень при (max=cond+8,min=cond+6)  корректных=%d бракованных=%d" % (step_lvl_ok8, step_lvl_bad8))
print("ШАГИ: уровень при (max=cond+10,min=cond+8) корректных=%d бракованных=%d" % (step_lvl_ok0, step_lvl_bad0))
print("ШАГИ: под-записей с квестом на приём (+1028>0):", obj_accept)
print("ШАГИ: под-записей с активной целью (+164+4j>0):", obj_active, " из них с непустым блоком 140 б:", obj_nonzero_blocks)
print("ШАГИ: заполненность результатов (Res0,Res1,Res2):", res_stats)
