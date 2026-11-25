# Copyright (c) 2026 @sitxovski (Discord)
# asda_jui_parse.py — парсер Login.JUI (декомп-INI) в дерево контролов + резолвер JuiSkin.ini.
#
# Формат login_jui.ini: блоки контролов. Каждый блок начинается с "Name = ...", содержит поля
# ID/IDNum/Visible/Origin/Size/AlignXPos/AlignYPos/CaptionText/EXS..EXE. Вложенность детей —
# через строки CHS = "N" (children start, увеличение depth) и CHE = "N" (children end).
#
# Формат JuiSkin.ini: записи скинов. Начало записи: Name="..." / Category / Type, далее
# ES/Rect/... / SS / ImageFileName / UV(8 float) / Diffuse(ARGB hex ×4).
# Резолв: имя контрола (Name) -> запись скина по Name (Category в игре НЕ участвует, имена уникальны).
#
# Математика позиций (база 1024×768): см. calc_abs(). Топ-уровневое окно с mode 16/32 = центрирование.
# Дети (AlignX/Y mode 5) = абсолют относительно родителя (parent_abs + Origin).
#
# Использование как библиотеки:
#   from asda_jui_parse import parse_jui, load_skin, resolve_skin, calc_positions
# Либо как CLI для отладки: python asda_jui_parse.py --dump 199
import sys, os, re, codecs

# --- пути по умолчанию (реальные источники) ---
JUI_INI  = r"@@ASTORY_DEV_ROOT@@\Data\Decoded\jui\login_jui.ini"
SKIN_INI = r"@@ASTORY_DEV_ROOT@@\RE\docs\JuiSkin.ini"

SCREEN_W = 1024
SCREEN_H = 768

# ---------------------------------------------------------------------------
# 1. Парсинг login_jui.ini -> дерево узлов
# ---------------------------------------------------------------------------
class Node:
    __slots__ = ("name","idc","idnum","visible","origin","size",
                 "align_x","align_y","align_xsize","align_ysize","caption","children","abs","anchor",
                 "cap_off","cap_color","cap_align",
                 "txt_content","txt_color","txt_align","txt_pos","txt_color_hex",
                 "noselect","alwaystop","events","frame_stop")
    def __init__(self):
        self.name = ""
        self.idc = ""          # ID (строковый, напр. SERVER_SELECT_WIN)
        self.idnum = -1        # IDNum (число; 4294967295 -> -1)
        self.visible = 1
        # ── ★★★ NoSelect — ДЕКЛАРАТИВНОЕ «клик проходит НАСКВОЗЬ» (эталон `uiControlHitTest @0x12DB470`,
        #    ui_core.c:107127: при попадании и a4=1 проверяется `a6 || !sub_926291(this)` — контрол с
        #    выставленным NoSelect хит-тест НЕ проходит). В `gamemain_jui.ini` стоит у 5768 контролов.
        #    Без него полноэкранные «окна» (WorldMap_Win 1024×768, EXPBar_WIn 1023×10) накрыли бы экран.
        self.noselect = 0
        # AlwaysTop — окно живёт в отдельном верхнем списке менеджера (`topWindowList`, this+101) и
        #    НЕ переупорядочивается подъёмом при клике (`uiWindowMgrDispatchMouse @0x12AF540`).
        self.alwaystop = 0
        # ── ★★★ EFrameStop — ОДНОРАЗОВАЯ (не зациклённая) кадровая анимация плана.
        #    Правка 31.08.2026 (долг импортёра №2): признак ЕСТЬ В ДАННЫХ у КАЖДОГО контрола
        #    (`gamemain_jui.ini` — 18 924 строки `EFrameStop`, из них "1" у 216 контролов), но
        #    парсер его не читал, а импортёр создавал ВСЕ анимпланы зациклёнными (`bLoop=True`
        #    жёстко). Из-за этого одноразовые эффекты крутились вечно, и режим one-shot
        #    приходилось выставлять кодом в рантайме.
        #    СВЕРКА ПО ДАННЫМ (все 4 ini): из 216 контролов с `EFrameStop="1"` у 215 скин
        #    содержит АНИМИРОВАННЫЙ план (`Animate=1`, TotalFrame>0, FrameInterval>0) — то есть
        #    признак относится ровно к кадровой анимации, а не к чему-то ещё. В `commonwin`,
        #    `login`, `temp` контролов с "1" нет вовсе.
        #    ⚠ ЧИСЛО ПРОГОНОВ ЗДЕСЬ НЕ ЛЕЖИТ: лимит ставит рантайм (`ctrlShowWithItemFlags(ctrl, N)`,
        #    ui_core.c:109888). Поэтому в виджет уходит только «зациклено / один прогон»
        #    (`UAsdaAnimatedPlane::bLoop`), а `MaxLoops` остаётся рантайму (у плашек рулетки
        #    67007…67010 / 67015…67018 в .ini как раз `EFrameStop="0"`, а 2 прогона ставит код).
        self.frame_stop = 0
        # ── ★★★ БЛОК EVS…EVE — ДЕКЛАРАТИВНЫЕ СОБЫТИЯ контрола (эталон `CUIMgr_SerializeControlTree
        #    @0x130A580`, ui_core.c:121654 — печать `EVT`/`RET` идёт через `%X`, т.е. ШЕСТНАДЦАТЕРИЧНО!).
        #      EVT  = тип события-триггера (6 = `CControl_OnLButtonDown` 2-й raise / `OnLButtonUp`);
        #      RET  = код действия диспетчера `uiDispatchNotifyToControlByID @0x12F1840`
        #             (0x20001 anchor, 0x20002 hide, ★0x20003 ПЕРЕТАСКИВАНИЕ, 0x20004 fade, 0x20005 resize);
        #      CtrlID = IDNum ЦЕЛИ действия (`CUICtrlInfo_LoadFromXml @0x12B3FB0`: sscanf "%u").
        #    Перетаскивание окон в оригинале НЕ написано в коде — оно ОБЪЯВЛЕНО ЗДЕСЬ.
        self.events = []       # [{"evt":int, "ret":int, "ctrl_id":int, "args":{...}}]
        self.origin = (0,0)
        self.size = (0,0)
        self.align_x = (0,0,0) # AlignXPos (mode, a=2й токен, b=3й токен)
        self.align_y = (0,0,0) # AlignYPos
        self.align_xsize = (0,0,0) # AlignXSize (mode, sa, sb)
        self.align_ysize = (0,0,0) # AlignYSize
        self.anchor = None     # UMG-анкор {axmin,aymin,axmax,aymax, algx,algy, offL,offT,offR,offB} — для responsive
        self.caption = ""
        self.cap_off = (0, 0)          # CaptionInfo[0,1] = offsetX,offsetY (px, signed)
        self.cap_color = (1, 1, 1, 1)  # CaptionInfo[2] = ARGB hex цвет текста
        self.cap_align = 0             # CaptionInfo[3] = маска: 0x1 hCenter,0x2 hRight; 0x4 vCenter,0x8 vBottom
        # ── ★★★ БЛОК EXS…EXE — СВОЙСТВА CTextControl (эталон `CTextControl_LoadProps @0x12E4E20`,
        #    font_text.c:9717-9744). ЭТО И ЕСТЬ настоящий текст контрола и его цвет:
        #      TextPos    → this+4 / this+612   (смещение текста внутри rect)
        #      TextContent→ CControl_SetText    (сам текст)
        #      TextColor  → this+3692           (★ ЦВЕТ ТЕКСТА)
        #      TextAlign  → this+3696           (маска выравнивания, та же семантика, что CaptionInfo[3])
        #    ⚠ `CaptionInfo[2]` — цвет ПОДПИСИ-caption, он рисуется ТОЛЬКО при непустом `CaptionText`.
        #    У 5374 узлов там дизайнерский маркер `ff400040` (фиолетовый) — в текст он попадать НЕ ДОЛЖЕН.
        self.txt_content = None        # EXS TextContent (None = блока не было)
        self.txt_color = None          # EXS TextColor → (r,g,b,a) linear
        self.txt_color_hex = None      # EXS TextColor как в файле (для диагностики/сверки)
        self.txt_align = None          # EXS TextAlign
        self.txt_pos = None            # EXS TextPos (offX, offY)
        self.children = []
        self.abs = (0,0,0,0)   # (left, top, w, h) экранный — заполняет calc_positions

def _to_int(s, default=0):
    try:
        v = int(s)
    except Exception:
        return default
    if v == 4294967295:        # -1 в u32
        return -1
    return v

def _pair(s):
    parts = s.replace('"','').split()
    if len(parts) >= 2:
        return (float(parts[0]), float(parts[1]))
    return (0.0, 0.0)

def _triple(s):
    parts = s.replace('"','').split()
    while len(parts) < 3:
        parts.append("0")
    return (_to_int(parts[0]), _to_int(parts[1]), _to_int(parts[2]))

def _val(line):
    # line вида: Name = "Log_YellowWin"
    m = re.match(r'\s*[\w]+\s*=\s*"(.*)"\s*$', line)
    if m:
        return m.group(1)
    m = re.match(r'\s*[\w]+\s*=\s*(.*)\s*$', line)
    return m.group(1).strip() if m else ""

def parse_jui(path=JUI_INI):
    """Возвращает список топ-уровневых Node (детей корня). Рекурсия по CHS/CHE."""
    # cp1251 — CaptionText может быть в cp1251/cp949; читаем терпимо
    with codecs.open(path, "r", encoding="cp1251", errors="replace") as f:
        lines = f.readlines()

    # ★ CHS = "N" / CHE = "N" — N это ГЛУБИНА вложенности (не push/pop счётчик). У одного контрола
    #   может быть НЕСКОЛЬКО CHS..CHE-групп подряд (напр. Txt и BtnBase — обе дети Choice отдельными группами).
    #   Ребёнок на глубине D → родитель = ПОСЛЕДНИЙ узел на глубине D-1 (depth_last[D-1]). Старый push(cur)
    #   пушил ПОСЛЕДНИЙ Name (глубокий потомок) → сиблинги вкладывались друг в друга → каскад/улёт.
    roots = []
    depth_last = {}   # глубина → последний созданный на ней узел
    active_depth = 0  # глубина следующих Name (задаётся CHS/CHE)
    cur = None        # текущий заполняемый узел (для строк-атрибутов после Name)
    in_exs = False    # ★ внутри блока EXS…EXE (свойства CTextControl: TextPos/TextContent/TextColor/TextAlign)
    in_evs = False    # ★ внутри блока EVS…EVE (декларативное событие: EVT/RET/CtrlID + аргументы действия)
    cur_ev = None     # накапливаемая запись события

    for raw in lines:
        line = raw.rstrip("\r\n")
        s = line.strip()
        if s.startswith("Name"):
            cur = Node()
            in_exs = False; in_evs = False; cur_ev = None
            cur.name = _val(line)
            if active_depth <= 0:
                roots.append(cur)
            else:
                parent = depth_last.get(active_depth - 1)
                (parent.children if parent is not None else roots).append(cur)
            depth_last[active_depth] = cur
            for _d in [k for k in depth_last if k > active_depth]:
                del depth_last[_d]           # глубже — устарело
            continue
        if cur is None:
            continue
        # ★★★ EVS…EVE — ДЕКЛАРАТИВНОЕ СОБЫТИЕ. Разбирается ПЕРВЫМ: внутри блока лежат ключи
        #     (Size/CtrlID/…), совпадающие по имени с атрибутами контрола, и попасть в них нельзя.
        if s == "EVS" or s.startswith("EVS "):
            in_evs = True
            cur_ev = {"evt": 0, "ret": 0, "ctrl_id": -1, "args": {}}
            continue
        if s == "EVE" or s.startswith("EVE "):
            if cur_ev is not None:
                cur.events.append(cur_ev)
            in_evs = False; cur_ev = None
            continue
        if in_evs:
            if cur_ev is None:
                continue
            v = _val(line)
            if s.startswith("EVT"):
                # ★ %X — ШЕСТНАДЦАТЕРИЧНО (иначе «6» совпадёт, а «20003» уедет в десятичное 20003).
                try: cur_ev["evt"] = int(v.strip() or "0", 16)
                except Exception: cur_ev["evt"] = 0
            elif s.startswith("RET"):
                try: cur_ev["ret"] = int(v.strip() or "0", 16)
                except Exception: cur_ev["ret"] = 0
            elif s.startswith("CtrlID"):
                cur_ev["ctrl_id"] = _to_int(v)       # 4294967295 → -1 (ручка без цели = баг оригинала)
            else:
                m = re.match(r'\s*([\w]+)\s*=', line)
                if m: cur_ev["args"][m.group(1)] = v
            continue
        # ★★★ EXS…EXE — свойства CTextControl. Внутри блока приоритет у них, снаружи их нет.
        if s == "EXS" or s.startswith("EXS "):
            in_exs = True
            continue
        if s == "EXE" or s.startswith("EXE "):
            in_exs = False
            continue
        if in_exs:
            if s.startswith("TextPos"):
                cur.txt_pos = _pair(_val(line))
            elif s.startswith("TextContent"):
                cur.txt_content = _val(line)
            elif s.startswith("TextColor"):
                hx = _val(line).strip()
                cur.txt_color_hex = hx
                try: cur.txt_color = _hex_argb_to_rgba(hx)
                except Exception: cur.txt_color = None
            elif s.startswith("TextAlign"):
                cur.txt_align = _to_int(_val(line), 0)
            continue
        if s.startswith("ID ") or s.startswith("ID="):
            cur.idc = _val(line)
        elif s.startswith("IDNum"):
            cur.idnum = _to_int(_val(line))
        elif s.startswith("Visible"):
            cur.visible = _to_int(_val(line), 1)
        elif s.startswith("NoSelect"):
            cur.noselect = _to_int(_val(line), 0)      # 1 = клик проходит сквозь контрол (см. Node.noselect)
        elif s.startswith("AlwaysTop"):
            cur.alwaystop = _to_int(_val(line), 0)     # 1 = верхний список менеджера, подъёму не подлежит
        elif s.startswith("EFrameStop"):
            cur.frame_stop = _to_int(_val(line), 0)    # 1 = анимация плана ОДНОРАЗОВАЯ (см. Node.frame_stop)
        elif s.startswith("Origin"):
            cur.origin = _pair(_val(line))
        elif s.startswith("Size") and not s.startswith("AlignXSize") and not s.startswith("AlignYSize"):
            cur.size = _pair(_val(line))
        elif s.startswith("AlignXPos"):
            cur.align_x = _triple(_val(line))
        elif s.startswith("AlignYPos"):
            cur.align_y = _triple(_val(line))
        elif s.startswith("AlignXSize"):
            cur.align_xsize = _triple(_val(line))
        elif s.startswith("AlignYSize"):
            cur.align_ysize = _triple(_val(line))
        elif s.startswith("CaptionText"):
            cur.caption = _val(line)
        elif s.startswith("CaptionInfo"):
            # "offX offY colorARGBhex alignMask" — эталон sscanf "%d %d %x %u" (ui_core.c:105012)
            p = _val(line).split()
            if len(p) >= 4:
                try: cur.cap_off = (int(p[0]), int(p[1]))
                except Exception: pass
                try: cur.cap_color = _hex_argb_to_rgba(p[2])
                except Exception: pass
                try: cur.cap_align = int(p[3])
                except Exception: pass
        elif s.startswith("CHS"):
            active_depth = _to_int(_val(line), active_depth + 1)          # глубина детей = N
        elif s.startswith("CHE"):
            active_depth = _to_int(_val(line), active_depth + 1) - 1      # закрыли глубину N → назад на N-1
            if active_depth < 0:
                active_depth = 0
    return roots

def find_by_idnum(nodes, idnum):
    """Поиск узла по IDNum в дереве (первое совпадение)."""
    for n in nodes:
        if n.idnum == idnum:
            return n
        r = find_by_idnum(n.children, idnum)
        if r:
            return r
    return None

# ---------------------------------------------------------------------------
# 2. Абсолютные позиции (база 1024×768)
# ---------------------------------------------------------------------------
def _resolve_size(node, parent_abs, rel_left, rel_top):
    """Эффективные (w,h) — ТОЧНАЯ копия CControl_resolveAnchorPosition @0x12DDED0 (ui_core.c ~108287).
    Порядок движка: РАЗМЕР резолвится ПЕРЕД позицией (центр%-поз требует width/height).
    Опорный размер refW/refH: ТОП-уровень = экран (sub_92A715 manager),
    ВЛОЖЕННЫЙ = размер РОДИТЕЛЯ (getSize parent) — parent_abs[2]/[3].  ← ключевой фикс.
    X-size-мод (align_xsize[0], поле +308):
      &1  abs:   ТОП w=sa;  ВЛОЖ w=sa+rel_left (движок: sa + relX, relX=childScreenX-parentScreenX)  [строки 108364-368 / 108413-416]
      &2  правый: w = refW + sa - a  (a=align_x[1]=POS-пара +320)                                     [108369-374 / 108418-423]
      &0x10 %:   w = sa/100 * refW                                                                    [108376-381 / 108425-429]
      else(0):   w = поле Size (не трогается)
    Y-size-мод (align_ysize[0], поле +316): &4/&8/&0x20 симметрично (sb=align_ysize[2], b=align_y[2], refH)."""
    sw, sh = node.size
    msx = node.align_xsize[0]; msy = node.align_ysize[0]
    sa = node.align_xsize[1];  sb = node.align_ysize[2]   # SIZE-пара (2й/3й токены)
    a  = node.align_x[1];      b  = node.align_y[2]       # POS-пара (вычитается в моде &2)
    if parent_abs is None:
        refW, refH = float(SCREEN_W), float(SCREEN_H)
    else:
        refW, refH = float(parent_abs[2]), float(parent_abs[3])
    # width
    if   msx & 0x01: w = float(sa) if parent_abs is None else float(sa + rel_left)
    elif msx & 0x02: w = refW + sa - a
    elif msx & 0x10: w = sa / 100.0 * refW
    else:            w = float(sw)
    # height
    if   msy & 0x04: h = float(sb) if parent_abs is None else float(sb + rel_top)
    elif msy & 0x08: h = refH + sb - b
    elif msy & 0x20: h = sb / 100.0 * refH
    else:            h = float(sh)
    return w, h


def calc_positions(node, parent_abs=None, is_root_window=True):
    """
    node.abs = (left, top, w, h) в базе 1024×768 — ТОЧНАЯ копия связки
    CControl_resolveAnchorPosition (size) + uiCalcAnchoredScreenPos (pos), ui_core.c.
    Дерево обходится СВЕРХУ-ВНИЗ: родитель резолвится первым, дети берут его готовый abs.
    X-значение POS = align_x[1] (2й токен, поле +320), Y = align_y[2] (3й токен, поле +324).

    POS-моды (uiCalcAnchoredScreenPos @0x12DFBB0):
      X (align_x[0], +304): &1 abs(+a) / &2 правый край(+a+parentW) / &0x10 центр a% / else = +Origin родителя.
      Y (align_y[0], +312): &4 abs / &8 низ(+parentH) / &0x20 центр b% / else.
      ВАЖНАЯ АСИММЕТРИЯ: центр% (&0x10/&0x20) вычитает width/2 (height/2) ТОЛЬКО на ТОП-уровне;
      у ВЛОЖЕННЫХ детей вычитания НЕТ (строки 109262-267/109283-288 vs 109305-310/109320-324).
    """
    ox, oy = node.origin
    mx = node.align_x[0]; my = node.align_y[0]
    a  = node.align_x[1]   # POS X-значение (поле +320)
    b  = node.align_y[2]   # POS Y-значение (поле +324)

    # relX/relY для редкого вложенного &1-size (движок: sa + смещение ребёнка отн. родителя).
    # Достаточно не-центрового приближения (&1-size + центр%-поз в реальных JUI не сочетаются).
    rel_left = ox + (a if (mx & 0x01) else 0)
    rel_top  = oy + (b if (my & 0x04) else 0)
    w, h = _resolve_size(node, parent_abs, rel_left, rel_top)

    if parent_abs is None:
        # ТОП-окно: опора = экран (sub_92A715 manager)
        if   mx & 0x10: left = a / 100.0 * SCREEN_W + ox - w / 2.0   # центр по a% (−w/2 только тут)
        elif mx & 0x02: left = ox + a + SCREEN_W                     # правый край (a обычно <0)
        elif mx & 0x01: left = float(ox + a)                         # абсолют от Origin
        else:           left = float(ox)                             # мод 0 = Origin как есть
        if   my & 0x20: top = b / 100.0 * SCREEN_H + oy - h / 2.0    # центр по b% (−h/2 только тут)
        elif my & 0x08: top = oy + b + SCREEN_H                      # нижний край
        elif my & 0x04: top = float(oy + b)                          # абсолют
        else:           top = float(oy)
    else:
        # ВЛОЖЕННЫЙ: опора = экранный прямоугольник родителя (parent.abs)
        pl, pt, pw, ph = parent_abs
        if   mx & 0x10: left = a / 100.0 * pw + ox + pl             # центр a% от ширины родителя (БЕЗ −w/2!)
        elif mx & 0x02: left = ox + a + pl + pw                     # правый край родителя
        elif mx & 0x01: left = ox + a + pl                          # абсолют отн. родителя (+a)
        else:           left = ox + pl                              # мод 0 = Origin отн. родителя
        if   my & 0x20: top = b / 100.0 * ph + oy + pt             # центр b% (БЕЗ −h/2!)
        elif my & 0x08: top = oy + b + pt + ph                     # низ родителя
        elif my & 0x04: top = oy + b + pt                           # абсолют
        else:           top = oy + pt

    node.abs = (left, top, w, h)
    for c in node.children:
        calc_positions(c, node.abs, is_root_window=False)
    return node.abs


def compute_anchors(root):
    """Заполняет node.anchor = {xspan,fx,yspan,fy} для КАЖДОГО узла + оставляет node.abs в базе 1024×768.
    Раскладка (calc_positions) ЛИНЕЙНА по (SCREEN_W, SCREEN_H): left = fx·W + const, width = γ·W + const
    (X зависит только от W, Y только от H — проверено формулами). Коэффициент fx = наклон left по W
    находим прогоном на двух ширинах; span (тянется ли контрол) — меняется ли width при смене W.
    Это 1:1 воспроизводит движок для ЛЮБЫХ align-мод и вложенности, без разбора случаев.
    fx для центр%-контрола = a/100, для правого края = 1, для abs = 0 (наклон left по W даёт это сам)."""
    global SCREEN_W, SCREEN_H
    def snap():
        d = {}
        def rec(n):
            d[id(n)] = n.abs
            for c in n.children: rec(c)
        rec(root); return d
    ow, oh = SCREEN_W, SCREEN_H
    SCREEN_W, SCREEN_H = 1024, 768;  calc_positions(root, None, True); base = snap()
    SCREEN_W, SCREEN_H = 2048, 768;  calc_positions(root, None, True); dW = snap()
    SCREEN_W, SCREEN_H = 1024, 1536; calc_positions(root, None, True); dH = snap()
    SCREEN_W, SCREEN_H = ow, oh;     calc_positions(root, None, True)   # вернуть базовый abs на узлы
    def assign(n):
        b = base[id(n)]; xw = dW[id(n)]; yh = dH[id(n)]
        fx = (xw[0] - b[0]) / 1024.0            # наклон left по W = анкор-доля (центр%→a/100, правый→1, abs→0)
        fy = (yh[1] - b[1]) / 768.0
        xspan = abs(xw[2] - b[2]) > 0.5         # ширина растёт с W → контрол тянется (фон/бар/%-размер)
        yspan = abs(yh[3] - b[3]) > 0.5
        n.anchor = {"xspan": xspan, "fx": fx, "yspan": yspan, "fy": fy}
        for c in n.children: assign(c)
    assign(root)
    return root.abs

# ---------------------------------------------------------------------------
# 3. Резолвер JuiSkin.ini: Name -> {type, tex, uv, diffuse}
# ---------------------------------------------------------------------------
def _srgb_to_linear(c):
    """sRGB [0..1] -> LINEAR [0..1]. ★ Цвета JuiSkin заданы в sRGB (как в D3D9-клиенте), а UE
    FLinearColor трактует значения как ЛИНЕЙНЫЕ и сам гамма-корректирует при выводе. Без этой
    конверсии тёмные цвета уезжают вверх: FF101010 (чёрный фон) → серый, FFFF0A00 → светло-красный."""
    return c / 12.92 if c <= 0.04045 else ((c + 0.055) / 1.055) ** 2.4


def _hex_argb_to_rgba(h):
    """'C8FFCD00' (AARRGGBB) -> (r,g,b,a) в [0..1]; RGB переводится sRGB→linear (альфа линейна)."""
    h = h.strip().split()[0] if h else "FFFFFFFF"
    h = h.replace('"','')
    # ★★★ КОРОТКАЯ ЗАПИСЬ = ЧИСЛО, А НЕ «недостающие байты — FF» (правка 2026-08-21).
    #   Строка — это hex-число ARGB: "FFFFFF" = 0x00FFFFFF, то есть альфа 0 (ПРОЗРАЧНО).
    #   Прежний rjust(8,'F') делал из неё непрозрачный белый — отсюда БЕЛЫЙ ПРЯМОУГОЛЬНИК
    #   под подписью шага в окне 570. Сверка по данным: во всём `JuiSkin.ini` 3251 значение `Diffuse`,
    #   из них 8-значных 3248, а 6-значные — только у ОДНОГО скина, и зовётся он
    #   `NoImage_PushBtn` («кнопка без картинки») — имя скина и подтверждает альфу 0. В 4 ini JUI
    #   коротких цветов (`CaptionInfo`, `TextColor`) НЕТ ни одного — правка их не трогает.
    if len(h) < 8:
        h = h.rjust(8, '0')
    a = int(h[0:2], 16) / 255.0
    r = _srgb_to_linear(int(h[2:4], 16) / 255.0)
    g = _srgb_to_linear(int(h[4:6], 16) / 255.0)
    b = _srgb_to_linear(int(h[6:8], 16) / 255.0)
    return (r, g, b, a)

def _uv8_to_box(uv):
    """'u0 v0 u0 v1 u1 v0 u1 v1' (8 float) -> (u0, v0, u1, v1)."""
    parts = uv.replace('"','').split()
    if len(parts) >= 8:
        f = [float(x) for x in parts[:8]]
        # индексы: 0,1 = u0 v0 ; 6,7 = u1 v1
        return (f[0], f[1], f[6], f[7])
    return (0.0, 0.0, 1.0, 1.0)

def _tex_path(image_file_name):
    """'NE_Booster_Center100.tga' -> ('/Game/AsdaAssets/Textures/JUI/ne_booster_center100.ne_booster_center100', 'ne_booster_center100')."""
    base = os.path.splitext(image_file_name.strip().replace('"',''))[0].lower()
    return ("/Game/AsdaAssets/Textures/JUI/%s.%s" % (base, base), base)

def load_skin(path=SKIN_INI):
    """
    Парсит JuiSkin.ini -> dict: Name(lower) -> {type, image, tex_path, tex_name, uv(4), diffuse(rgba), caption}.
    Берёт ПЕРВЫЙ state (SS=0) каждой записи.
    """
    skins = {}
    with codecs.open(path, "r", encoding="cp1251", errors="replace") as f:
        lines = f.readlines()

    cur = None
    got_ss = False
    for raw in lines:
        s = raw.strip()
        if s.startswith("Name"):
            cur_name = _val(raw)
            cur = {"name": cur_name, "type": "", "image": "", "tex_path": "", "tex_name": "",
                   "uv": (0.0,0.0,1.0,1.0), "diffuse": (1.0,1.0,1.0,1.0), "caption": "",
                   # ★ ES1 = СЛОЙ ЗАЛИВКИ у ProgressBar (ES0 = подложка). Именно в нём лежит цвет
                   #   полосы (HP_RedBar ES1 Diffuse=FFFF0A00 красный) — без него полоса рисовалась
                   #   белой подложкой (выглядело «розовым»).
                   "fill_uv": None, "fill_diffuse": None, "fill_image": "", "fill_tex_path": "", "fill_tex_name": "",
                   # ★★★ TextOutLine — ЧЁРНАЯ ОБВОДКА 1px текста Text-скина. Живёт ТОЛЬКО в JuiSkin.ini
                   #   (182 записи, значения строго "0"/"1"; в *_jui.ini его НЕТ — проверено grep'ом).
                   #   Эталон: sub_12E4D50 @0x12E4D50 читает его из cfg-map скина → CTextControl+3700
                   #   (dword 925); рендер label @ui_core.c:110729: ==1 → textDrawFlags |= 0x2010 (обводка).
                   "textoutline": 0}
            skins[cur_name.lower()] = cur
            got_ss = False
            cur_es = 0
            continue
        if cur is None:
            continue
        if s.startswith("Type"):
            cur["type"] = _val(raw)
        elif s.startswith("TextOutLine"):
            cur["textoutline"] = _to_int(_val(raw).strip().strip('"'), 0)
        elif s.startswith("ES"):
            try: cur_es = int(_val(raw))
            except Exception: cur_es = 0
        elif s.startswith("SS"):
            got_ss = True      # первый state; поля ниже — из него
        elif s.startswith("ImageFileName"):
            img = _val(raw)
            if cur_es >= 1:
                if not cur["fill_image"]:
                    cur["fill_image"] = img
                    if img and img.lower() not in ("", "none"):
                        p, n = _tex_path(img)
                        cur["fill_tex_path"] = p; cur["fill_tex_name"] = n
            elif not cur["image"]:
                cur["image"] = img
                if img and img.lower() not in ("", "none"):
                    p, n = _tex_path(img)
                    cur["tex_path"] = p
                    cur["tex_name"] = n
        elif s.startswith("UV"):
            if cur_es >= 1:
                if cur["fill_uv"] is None: cur["fill_uv"] = _uv8_to_box(_val(raw))
            elif cur["uv"] == (0.0,0.0,1.0,1.0):
                cur["uv"] = _uv8_to_box(_val(raw))
        elif s.startswith("Diffuse"):
            if cur_es >= 1:
                if cur["fill_diffuse"] is None: cur["fill_diffuse"] = _hex_argb_to_rgba(_val(raw))
            elif cur["diffuse"] == (1.0,1.0,1.0,1.0):
                cur["diffuse"] = _hex_argb_to_rgba(_val(raw))
        elif s.startswith("CaptionText") and not cur["caption"]:
            cur["caption"] = _val(raw)
    return skins

def resolve_skin(skins, name):
    """Name контрола -> запись скина или None."""
    return skins.get(name.lower())

# ---------------------------------------------------------------------------
# 3b. ПОЛНЫЙ парс планов JuiSkin (ES=план, SS=кадр-состояние внутри плана)
# ---------------------------------------------------------------------------
# ★ Механизм состояний 1:1 (button_states_re.md): движок НЕ тинтит — переключает индекс кадра плана.
#   Кадр 0=normal/unchecked, 1=hover, 2=pressed/CHECKED, 3=selected(радио)/checked+hover. У КАЖДОГО
#   кадра свой ImageFileName/UV/Diffuse. plane["frames"][i] = кадр i; plane["tex/uv/diffuse"]=кадр0.
# ★★★ ЖИЛ В `asda_jui_generic_umg.py` (модуль, который тянет `unreal`). Перенесён СЮДА, чтобы им мог
#   пользоваться и мета-генератор `asda_jui_window_meta.py`, работающий вне редактора: хит-тест
#   оригинала считает ПЛАНЫ контрола (`uiControlHitTest`, ui_core.c:107127), поэтому «ловит ли контрол
#   клик» = «есть ли у его скина хоть один план» — вопрос к JuiSkin, а не к UE.
def parse_skins_full(path=SKIN_INI):
    skins = {}; cur = None; plane = None; frame = None
    for raw in codecs.open(path, "r", encoding="cp1251", errors="replace"):
        s = raw.strip()
        if s.startswith("Name"):
            # ★ textoutline: ЧЁРНАЯ ОБВОДКА 1px текста Text-скина (только JuiSkin.ini, значения "0"/"1";
            #   эталон sub_12E4D50 → CTextControl+3700, рендер ui_core.c:110729 флаг 0x2010). Лежит
            #   МЕЖДУ Type и первым ES — разбирать ДО планов.
            cur = {"type": "", "planes": [], "textoutline": 0}; skins[_val(raw).lower()] = cur; plane = None; frame = None
        elif cur is None: continue
        elif s.startswith("ScrollBarType"):
            # ★ 19.09.2026: ориентация полосы (`cfg_WriteScrollBarType` → поле +609): 0 = горизонтальная,
            #   1 = вертикальная. Нет ключа — вертикальная (умолчание `CScrollBar_ctor@0x12FE240`: +609 = 1).
            #   Нужна `scrollbar_plane_rects`. Проверяется ДО «Type», иначе префикс не различить.
            try: cur["scrollbar_type"] = int(_val(raw).strip().strip('"') or 1)
            except Exception: cur["scrollbar_type"] = 1
        elif s.startswith("Type"): cur["type"] = _val(raw)
        elif s.startswith("TextOutLine"):
            try: cur["textoutline"] = int(_val(raw).strip().strip('"') or 0)
            except Exception: cur["textoutline"] = 0
        elif s.startswith("ES"):
            plane = {"rect": (0, 0, 0, 0), "uv": (0, 0, 1, 1), "tex": "", "diffuse": (1, 1, 1, 1), "_hasrect": False, "invisible": False, "fillmode": 0, "frames": {},
                     "animate": False, "frame_px": (0.0, 0.0), "total_frame": 0, "frame_interval": 0.0}
            cur["planes"].append(plane); frame = None
        elif plane is None: continue
        elif s.startswith("SS"):
            try: fi = int(_val(raw).strip().strip('"') or 0)
            except Exception: fi = 0
            # ★★★ ПОДПИСЬ ЗАДАНА У КАЖДОГО КАДРА-СОСТОЯНИЯ ОТДЕЛЬНО (`capcol`/`cappt`/`capalign`).
            #   Эталон: рендер подписи (`cControl_RenderCaption`, ui_core.c:109129-109151) берёт цвет
            #   `sub_92BCCD(plane, -1)`, смещение `sub_92279F(plane, -1)`, выравнивание
            #   `sub_932C2B(plane, -1)` и сам текст `paneGetSlotByIndex(plane, -1)` — везде индекс
            #   кадра `-1` = АКТИВНЫЙ (`plane+494`). Загрузчик скина (ui_core.c:120184-120228)
            #   пишет их ПОКАДРОВО. ⇒ смена состояния кнопки меняет НЕ ТОЛЬКО картинку, но и цвет и
            #   положение подписи, а у скинов `NoImage_PushBtn` / `NoImage_Btn` (кадры визуально
            #   одинаковы, картинка прозрачная) подпись — ЕДИНСТВЕННОЕ, что вообще меняется.
            #   Сводка по `JuiSkin.ini`: цвет подписи различается у 252 планов (48 PushButton),
            #   смещение — у 136; см. `Reports/npc_dialog_button_states.md`.
            frame = {"tex": "", "uv": (0, 0, 1, 1), "diffuse": (1, 1, 1, 1),
                     "capcol": None, "cappt": None, "capalign": None}
            plane["frames"][fi] = frame
        elif s.startswith("FillMode"):
            try: plane["fillmode"] = int(_val(raw).strip().strip('"') or 0)
            except Exception: plane["fillmode"] = 0
        elif s.startswith("PlaneAlwaysInvisible"):
            plane["invisible"] = _val(raw).strip() in ("1", "\"1\"")
        elif s.startswith("Rect") and not plane["_hasrect"]:
            p = _val(raw).split(); plane["_hasrect"] = True
            if len(p) >= 4: plane["rect"] = tuple(float(x) for x in p[:4])
        elif s.startswith("ImageFileName"):
            img = _val(raw)
            if img and img.lower() not in ("", "none"):
                tx = os.path.splitext(img)[0].lower()
                if frame is not None and not frame["tex"]: frame["tex"] = tx
                if not plane["tex"]: plane["tex"] = tx           # кадр0 = дефолт плана
        elif s.startswith("UV"):
            uv = _uv8_to_box(_val(raw))
            if frame is not None and frame["uv"] == (0, 0, 1, 1): frame["uv"] = uv
            if plane["uv"] == (0, 0, 1, 1): plane["uv"] = uv
        elif s.startswith("Diffuse"):
            df = _hex_argb_to_rgba(_val(raw))
            if frame is not None and frame["diffuse"] == (1, 1, 1, 1): frame["diffuse"] = df
            if plane["diffuse"] == (1, 1, 1, 1): plane["diffuse"] = df
        # ── ПОДПИСЬ КАДРА-СОСТОЯНИЯ (см. комментарий у создания frame выше) ──
        #   Цвет кладём СЫРОЙ hex-строкой: у подписи он всегда 8-значный (коротких `CaptionColor`
        #   в `JuiSkin.ini` нет), а решение «sRGB→linear» принимает потребитель.
        elif s.startswith("CaptionColor"):
            if frame is not None and frame["capcol"] is None: frame["capcol"] = _val(raw).strip().strip('"')
        elif s.startswith("CaptionPoint"):
            if frame is not None and frame["cappt"] is None:
                p = _val(raw).replace('"', '').split()
                try: frame["cappt"] = (int(p[0]), int(p[1])) if len(p) >= 2 else (0, 0)
                except Exception: frame["cappt"] = (0, 0)
        elif s.startswith("CaptionAlign"):
            if frame is not None and frame["capalign"] is None:
                try: frame["capalign"] = int(_val(raw).strip().strip('"') or 0)
                except Exception: frame["capalign"] = 0
        # ── кадровая анимация plane (Animate="1") — система JUI plane-анимации ──
        elif s.startswith("Animate"):
            if _val(raw).strip().strip('"') == "1": plane["animate"] = True
        elif s.startswith("FrameTextureSize"):
            p = _val(raw).split()
            if len(p) >= 2:
                try: plane["frame_px"] = (float(p[0]), float(p[1]))
                except Exception: pass
        elif s.startswith("TotalFrame"):
            try: plane["total_frame"] = int(_val(raw).strip().strip('"') or 0)
            except Exception: pass
        elif s.startswith("FrameInterval"):
            try: plane["frame_interval"] = float(_val(raw).strip().strip('"') or 0)
            except Exception: pass
    return skins

# ---------------------------------------------------------------------------
# ★★★ ОБЩИЕ ТОЧКИ С ИМПОРТЁРОМ: раскладка планов скина и состояния подписи кнопки.
#   Обе функции ЧИСТЫЕ (без `unreal`) и вызываются и импортёром `asda_jui_generic_umg.py`
#   (там они присвоены как алиасы), и шагом `asda_ui_missing_interactive.py`.
# ---------------------------------------------------------------------------
def plane_screen_rects(planes, W, H):
    """Раскладка планов контрола. Шаблон = МАКС. границы Rect планов (sW,sH). Каждый план масштабируется
    ПРОПОРЦИОНАЛЬНО из шаблона в фактический размер контрола W×H (стретч UV-свотча под анкор-ресайз контрола).
    ★ Ключевое свойство: когда размер контрола == шаблону (напр. гендер Log_CaracterSex: контрол 69×39,
      max-rect 69×39) → масштаб=1 → планы НАТИВНЫ бок-о-бок (♂0-29, ♀40-69). Когда контрол РАСТЯНУТ анкорами
      больше шаблона (фоны заголовков/чекбоксов/прогресс-баров/логотип) → план заполняет контрол.
    План без Rect → на весь контрол. (Возврат — смещения rx,ry ОТНОСИТЕЛЬНО левого-верха; вызыватель V2(l+rx,t+ry).)"""
    rects = [p["rect"] for p in planes if p.get("_hasrect")]
    sW = max((r[2] for r in rects), default=0) or W
    sH = max((r[3] for r in rects), default=0) or H
    out = []
    for p in planes:
        x0, y0, x1, y1 = p["rect"]
        if not p.get("_hasrect") or (x1 - x0) <= 0 or (y1 - y0) <= 0:
            out.append((0.0, 0.0, W, H, p)); continue            # без Rect → на весь контрол
        rx0 = x0 / sW * W; rx1 = x1 / sW * W
        ry0 = y0 / sH * H; ry1 = y1 / sH * H
        out.append((rx0, ry0, max(rx1 - rx0, 0.5), max(ry1 - ry0, 0.5), p))
    return out


def scrollbar_plane_rects(planes, W, H, vertical):
    """★★★ Раскладка четырёх планов ПОЛОСЫ ПРОКРУТКИ (скины типа `ScrollBar`, напр. `SideBar_Scr`) —
    ТОЧНАЯ копия `uiScrollbarLayoutPlanes@0x12FE500` (ui_core.c:118002) + `uiScrollbarLayoutThumb@0x12FEA40`.
    Добавлено 19.09.2026: `plane_screen_rects` растягивал планы ПРОПОРЦИОНАЛЬНО, и у полосы громкости окна
    настроек 69500 (контрол 136×11 при шаблоне скина 76×11) стрелки «−»/«+» и бегунок 11×11 выходили
    19.7×11 — «растянутые и масштабированные» кнопки из жалобы владельца.
    ЭТАЛОН (горизонтальная, +609 = 0; вертикальная — симметрично по Y):
      • сдвиг = W − (правый край плана 2 − левый край плана 1) — насколько контрол шире шаблона;
      • план 0 (жёлоб): правый край += сдвиг — тянется ТОЛЬКО он;
      • план 1 (стрелка «−»): как в шаблоне;
      • план 2 (стрелка «+»): оба края += сдвиг — уезжает к правому краю, размер свой;
      • план 3 (бегунок): размер свой, позицию ставит `uiScrollbarLayoutThumb` по значению полосы —
        в данных оставляем точку шаблона (её всё равно переставит рантайм);
      • поперёк оси все планы = 0…H.
    Возврат — как у `plane_screen_rects`: (rx, ry, rw, rh, plane) относительно левого-верха контрола.
    Не 4 плана с Rect — None (вызывающий откатывается на общую раскладку)."""
    if len(planes) != 4 or not all(p.get("_hasrect") for p in planes):
        return None
    r = [p["rect"] for p in planes]
    if vertical:
        delta = H - (r[2][3] - r[1][1])
    else:
        delta = W - (r[2][2] - r[1][0])
    out = []
    for j, p in enumerate(planes):
        x0, y0, x1, y1 = r[j]
        if vertical:
            if j == 0: y1 += delta
            elif j == 2: y0 += delta; y1 += delta
            x0, x1 = 0.0, float(W)
        else:
            if j == 0: x1 += delta
            elif j == 2: x0 += delta; x1 += delta
            y0, y1 = 0.0, float(H)
        out.append((float(x0), float(y0), max(float(x1 - x0), 0.5), max(float(y1 - y0), 0.5), p))
    return out


# ── ★★★ ПРЯМОУГОЛЬНИК УЗЛА-ПОДПИСИ (TextBlock импортёра) — ОБЩАЯ ТОЧКА (07.10.2026) ────────────────
#   Отчёт `Reports/owner_1007/label_band_and_8500.md`. Импортёр рисует подпись на «сыром» rect контрола:
#   верх строки = верх + TextPos.y + vy, где vy — запечённая однострочная центровка по маске (0x4 центр:
#   (h − 14)/2, 0x8 низ: h − 14). ДО 07.10 высота узла оставалась ПОЛНОЙ (max(h, 14)) — узел сдвигался вниз
#   целиком и вылезал за низ контрола на TextPos.y + vy. Хит-тест окна 1:1 с оригиналом (`HitTestWidgetTree`,
#   `AsdaWindowSubsystem.cpp`) меряет прямоугольник узла, и у узла, вылезшего ЗА РАМКУ ОКНА, полоса под рамкой
#   съедала щелчок в мир (магазин NPC `NpcShop_Win03` +9.5 px, лента чата `CHAT_NEW_C_List` +7 px). В оригинале
#   контрол — прямоугольник Origin/Size, текст рисуется ВНУТРИ него, полосы нет.
#   ПРАВИЛО (решение владельца 07.10): верх строки тот же (пиксель в пиксель), а узел, который вылез бы за НИЗ
#   РАМКИ ОКНА (`frame_bottom` — низ корневого контрола .JUI в координатах того же родителя), обрезается по низу
#   своего контрола: высота = min(max(h, 14), max(низ контрола − верх строки, 14)). Нижний предел 14 — высота
#   строки допущения импортёра: строке, которая сама не помещается в контрол, узел не урезается (её и оригинал
#   рисует за краем). Ширина/лево не меняются. Текст Slate рисуется от ВЕРХА своего узла, клипа у узлов
#   импортёра нет (INHERIT) ⇒ видимое положение строки не меняется ни на пиксель.
#   ★ ПОЧЕМУ ТОЛЬКО ЗА РАМКОЙ ОКНА. Тот же сдвиг есть у 6 447 узлов в 228 окнах, но внутри рамки полоса ничего не
#   ловит: там клик и так принадлежит окну (фон/рамка окна под ним). Общее правило «всегда по низу контрола»
#   переписало бы 6 282 узла и сломало сверку «было» у 9 шагов пост-импорта (164 узла, которые они уже правят) —
#   это отдельное решение владельца (реестр `PORT_PENDING_ON_SYSTEMS.md`); `frame_bottom=None` — именно оно.
#   Пользователи: импортёр `asda_jui_generic_umg.add_control` (обе ветки TextBlock), шаг S1a
#   `asda_ui_missing_text_nodes.py`, генератор таблицы шага `asda_ui_label_band.py` (через офлайн-прогон
#   импортёра `asda_jui_importer_sim.py`). `legacy=True` — прежний вид (высота max(h, 14)): им шаги узнают «было»,
#   им же откат импортёра (`ASDA_LABEL_BAND_LEGACY=1`).
# ★★★ 07.10.2026 — ВЫРАВНИВАНИЕ ТЕКСТА УЗЛА-ПОДПИСИ: ОБЩАЯ ТОЧКА (ревизия `Reports/owner_1007/poststeps_idempotent_REVIEW.md`,
#   СРЕДН-1 / МЕЛК-2). Источник маски: у Text-контрола — `EXS TextAlign` (эталон `CTextControl_LoadProps`, this+3696),
#   у НЕ-Text контрола (подпись `{idc}_Text`) — `CaptionInfo[3]` (у таких `EXS TextAlign` не бывает ни разу по 4 ini).
#   Пользователи: импортёр `asda_jui_generic_umg.add_control`, шаг `asda_fix_minimap_ui.apply_text`, правила
#   `asda_ui_text_block_align.text_mask/text_justify`. Раньше шаг мини-карты брал маску только из `EXS TextAlign` и подписи
#   кнопок («Найти моё местоположение», кнопка каналов) получал LEFT вместо центра по `CaptionInfo`.
def label_mask(node):
    """Маска выравнивания текста контрола: `EXS TextAlign`, иначе `CaptionInfo[3]` (0x1 hCenter, 0x2 hRight,
    0x4 vCenter, 0x8 vBottom)."""
    ta = getattr(node, "txt_align", None)
    return ta if ta is not None else getattr(node, "cap_align", 0)


def label_justify(mask):
    """Горизонталь маски → 0 LEFT / 1 CENTER / 2 RIGHT (аргумент `halign` у `AsdaUmgBuilder.add_text`).
    Приоритет как у импортёра: бит 0x1 (центр) раньше 0x2 (право)."""
    return 1 if (mask & 0x1) else (2 if (mask & 0x2) else 0)


LABEL_LINE_PX = 14     # допущение импортёра о высоте строки (`GEN_TEXT_LINE_PX`, C++ `kJuiGenAssumedLineH`)
LABEL_EPS = 0.01       # допуск сравнения «низ узла ниже рамки окна», px базы


def label_slot(l, t, w, h, offx, offy, mask, legacy=False, frame_bottom=None):
    """Слот узла-подписи импортёра (x, y, ширина, высота) для контрола (l, t, w, h) в координатах родителя,
    смещения TextPos/CaptionInfo (offx, offy) и маски выравнивания `mask` (0x4 центр, 0x8 низ по вертикали).
    `frame_bottom` — низ рамки окна в тех же координатах: узел обрезается по низу контрола, только если вылез бы
    за неё (None — всегда, общее правило; `legacy` — никогда, прежний вид)."""
    vy = (h - LABEL_LINE_PX) / 2 if (mask & 0x4) else (h - LABEL_LINE_PX) if (mask & 0x8) else 0
    x = l + offx
    y = t + offy + max(vy, 0)
    tw = max(w - offx, 8)
    th = max(h, LABEL_LINE_PX)
    if legacy or (frame_bottom is not None and y + th <= frame_bottom + LABEL_EPS):
        return x, y, tw, th
    return x, y, tw, min(th, max(t + h - y, LABEL_LINE_PX))


# ★ 07.10.2026 (ревизия `Reports/owner_1007/label_band_and_8500_REVIEW.md`, МЕЛК-3). Шаги пост-импорта, которые
#   ПЕРЕСТАВЛЯЮТ подпись импортёра (`asda_ui_icon_caption_fit`, `asda_ui_text_block_align` (а)/(б)/(д)), держали
#   свою копию прежней высоты max(h, 14). Рамки окна в редакторе у них нет, поэтому вид импортёра узнаётся по
#   ВХОДУ: импортёр с 07.10 даёт либо прежний слот (узел в рамке), либо обрезанный по низу контрола (за рамкой) —
#   `label_slot_forms` возвращает оба; `label_height_like` даёт высоту нового прямоугольника подписи (верх `y`)
#   в том же виде, в каком пришёл вход. Так итог шага повторяет решение импортёра о рамке без копии формулы.
def label_slot_forms(l, t, w, h, offx, offy, mask):
    """(прежний вид, вид «по низу контрола») слота подписи импортёра — два допустимых входа шагов пост-импорта.
    Совпадают, когда обрезать нечего."""
    return (label_slot(l, t, w, h, offx, offy, mask, legacy=True),
            label_slot(l, t, w, h, offx, offy, mask, frame_bottom=None))


def label_height_like(t, h, y, cut):
    """Высота прямоугольника подписи с верхом `y` у контрола (верх `t`, высота `h`) по правилу `label_slot`:
    `cut=False` — прежний вид max(h, 14), `cut=True` — по низу контрола (не меньше строки 14)."""
    return label_slot(0.0, t, 0.0, h, 0.0, y - t, 0, legacy=not cut)[3]


# ── ★★★ СОСТОЯНИЯ ПОДПИСИ КНОПКИ (цвет и смещение текста на наведении/нажатии) ──────────────────
#   ЭТАЛОН. Кадр-состояние плана несёт не только картинку: загрузчик скина (`ui_core.c:120184-120228`)
#   кладёт в КАЖДЫЙ кадр `CaptionColor` / `CaptionPoint` / `CaptionAlign` / `CaptionText`, а рендер
#   подписи (`ui_core.c:109129-109151`) читает их у АКТИВНОГО кадра: цвет `sub_92BCCD(plane, -1)`,
#   смещение `sub_92279F(plane, -1)`. Кадры: 0 покой, 1 наведение, 2 нажатие.
#   ⇒ hover/press у кнопки меняют И ПОДПИСЬ; у скинов `NoImage_PushBtn` / `NoImage_Btn` картинка
#   кадров одинаковая и прозрачная, поэтому подпись — ЕДИНСТВЕННОЕ, что там вообще меняется.
#
#   КАДР 0 ПЕРЕКРЫТ КОНТРОЛОМ. `LoadJUI_ParseNode` (`ui_core.c:105014-105026`) пишет `CaptionInfo`
#   контрола в кадр `-1` = АКТИВНЫЙ, а активен при загрузке кадр 0. То есть покой берётся из
#   `CaptionInfo` контрола, а наведение/нажатие остаются «скиновыми».
#
#   ★ ДЕФЕКТ ДАННЫХ ОРИГИНАЛА (исправлен, закон `fix-original-defects`). У 51 контрола
#   `CaptionInfo`-смещение по X не совпадает со «скиновым», и в оригинале подпись на наведении
#   прыгала по горизонтали (напр. `EXCH_OK`: покой X=0, наведение X=3 из скина `WkBtn_W100`).
#   Авторская правка контрола очевидно относится ко ВСЕМ состояниям, поэтому смещения кадров 1/2
#   берём ОТНОСИТЕЛЬНО кадра 0: `CaptionInfo + (кадрN − кадр0)`. По всем 130 скинам `PushButton`
#   у кадра 1 смещение равно кадру 0, так что наведение подпись не двигает — двигает только нажатие.
def caption_state_args(node, plane):
    """(цвет покоя, цвет наведения, цвет нажатия, смещение покоя, смещение нажатия) либо None,
    если у скина одно состояние подписи (тогда ставить в стиль нечего)."""
    fr = (plane or {}).get("frames") or {}
    f0 = fr.get(0) or {}
    f1 = fr.get(1) or f0
    f2 = fr.get(2) or f0
    # Покой — из CaptionInfo контрола (он перекрывает кадр 0), фолбэк — цвет кадра 0 скина.
    c0 = getattr(node, "cap_color", None)
    if c0 is None:
        c0 = _hex_argb_to_rgba(f0.get("capcol") or "FFFFFFFF")
    c1 = _hex_argb_to_rgba(f1.get("capcol")) if f1.get("capcol") else c0
    c2 = _hex_argb_to_rgba(f2.get("capcol")) if f2.get("capcol") else c0
    p0 = f0.get("cappt") or (0, 0)
    p2 = f2.get("cappt") or p0
    off0 = getattr(node, "cap_off", (0, 0))
    off2 = (off0[0] + (p2[0] - p0[0]), off0[1] + (p2[1] - p0[1]))
    if c1 == c0 and c2 == c0 and tuple(off2) == tuple(off0):
        return None          # скин состояний подписи не задаёт — стиль не трогаем
    return (c0, c1, c2, off0, off2)


# ---------------------------------------------------------------------------
# ★★★ ОБЩАЯ ТОЧКА: контролы, у которых текст живёт ТОЛЬКО в `EXS TextContent`
# ---------------------------------------------------------------------------
def _san(s):
    """Санитайзер имени виджета — ТОТ ЖЕ, что `san()` импортёра (`asda_jui_generic_umg.py:97`)."""
    return re.sub(r"[^A-Za-z0-9_]", "_", (s or "").strip()) or "win"


def missing_text_nodes(root):
    """★★★ КОНТРОЛЫ КЛАССА S1a: пустой `ID`, пустой `CaptionText`, НЕПУСТОЙ `EXS TextContent`.

    ЗАЧЕМ. `CTextControl_LoadProps` (эталон `font_text.c:9721-9735`) читает `TextContent` и
    отдаёт его В САМ КОНТРОЛ через `CControl_SetText` — это НАСТОЯЩИЙ текст Text-контрола.
    Импортёр же брал только `CaptionText` (`asda_jui_generic_umg.py`, переменная `cap`), поэтому
    у такого контрола НЕ появлялось ни одного узла: именованная ветка требует `idc`, а
    caption-ветка — непустой `cap`. По 4 ini таких контролов 209 в 19 окнах (все — `gamemain`).

    ИМЯ УЗЛА (общее правило импортёра и шага `asda_ui_missing_text_nodes.py` — менять только
    В ОБОИХ): `{ЯКОРЬ}_TXT{NN}`, где ЯКОРЬ — `ID` ближайшего предка (у контрола своего `ID` нет),
    а при отсутствии такого предка — `ID`/`Name` корневого окна; `NN` — порядковый номер среди
    контролов ЭТОГО ЖЕ якоря в порядке документа. Индекс обхода (как в `base = имя_idx`) НЕ
    используется намеренно: он сдвигается от любой правки данных, а имя узла должно быть стабильным.

    Возвращает список `(node, parent, name)` в порядке обхода документа. `parent` = None у корня.
    ⚠ `calc_positions`/`compute_anchors` эта функция НЕ вызывает — координаты берёт вызыватель.
    """
    def pick(n):
        cap = (n.caption or "").strip()
        txt = (n.txt_content or "").strip() if n.txt_content else ""
        return "TXT" if ((not n.idc) and (not cap) and txt) else None
    return [(n, p, nm) for n, p, nm, _k in anchored_names(root, pick)]


def caption_slot_yields(root, skin_type_of):
    """★★★ ПУСТОЙ СЛОТ ПОДПИСИ, КОТОРЫЙ УСТУПАЕТ ИМЯ НАСТОЯЩЕМУ КОНТРОЛУ (25.09.2026, окно 50300).

    ЗАЧЕМ. Импортёр с 31.08 делает узел подписи `{idc}_Text` и при ПУСТОМ `CaptionText`, если у
    контрола «настроен слот» (`CaptionInfo` с маской выравнивания или смещением) — см. `cap_slot` в
    `asda_jui_generic_umg.add_control`. Но в окне бывает НАСТОЯЩИЙ контрол, чей `ID` уже равен
    `{idc}_Text`: у `ItemOptionValue_2_b1` (скин `Box_WhiteOt20`, `CaptionInfo = "0 0 ffe9d6bc 5"`)
    ребёнок — Text-контрол 50343 `ItemOptionValue_2_b1_Text`. Родитель обходится ПЕРВЫМ, пустая подпись
    забирает имя, а живая строка списка получает `…_Text_1` (`uniq_name`). Подсистема окна пишет по
    имени в пустышку, та лежит ПОД чёрным планом строки, а на экране остаётся текст ассета
    «Макс. HP +1» (дефект «первая строка панели 0», `optwin_fix.md`).

    КОГДА СЛОТ УСТУПАЕТ (все условия — по данным):
      • `CaptionText` пуст (слот создан только правилом `cap_slot`; с непустой подписью коллизий
        в 4 ini нет — проверено тем же отчётом);
      • имя `{san(idc)}_Text` совпадает с `ID` ДРУГОГО контрола того же окна;
      • у контрола НЕТ `IDNum` (−1): эталон находит контролы окна только по номеру
        (`CWindowMgr_FindControlByID`, ui_core.c:104592), значит подпись такому контролу записать
        НЕКОМУ — она всегда пуста и ничего не рисует. Так у 50343/50363: `itemUpgradeWindowInitControls`
        (item_inventory.c:3739) берёт 50340…50351 / 50360…50371 по номерам, `ItemOptionValue_2_b1` не трогает.
    Коллизии С `IDNum` (2 случая: `CAMPWAR_Out_Button` 17520, `CAMPWAR_Button` 49500) НЕ уступают:
      их подпись эталон ЗАПИСЫВАЕТ (`Collection_AddString`, world_map.c:7175, combat_pvp.c:4865), и
      код порта миникарты пишет её в узел `CAMPWAR_Out_Button_Text` осознанно.
    Сверка по 4 ini: коллизий пустого слота 3, уступает 1 (`ItemOptionValue_WIN`).

    skin_type_of(node) → `Type` скина узла (у Text-контролов подписи-слота нет вовсе, их пропускаем).
    Возвращает список `(node, имя_подписи)` в порядке обхода. Координаты берутся из `node.abs`
    (их заполняет `compute_anchors`), при пустом `abs` — из `node.size`.
    """
    nodes = []

    def walk(n):
        nodes.append(n)
        for c in n.children:
            walk(c)
    walk(root)
    ids = {}
    for n in nodes:
        if n.idc:
            ids.setdefault(_san(n.idc), []).append(n)
    out = []
    for n in nodes:
        if not n.idc or (n.caption or "").strip() or n.idnum >= 0:
            continue
        if (skin_type_of(n) or "") == "Text":
            continue
        w, h = (n.abs[2], n.abs[3]) if (n.abs and (n.abs[2] or n.abs[3])) else n.size
        if not (w > 0 and h > 0 and (n.cap_align != 0 or tuple(n.cap_off) != (0, 0))):
            continue
        name = _san(n.idc) + "_Text"
        if any(o is not n for o in ids.get(name, ())):
            out.append((n, name))
    return out


def anchored_names(root, pick):
    """★★★ ОБЩЕЕ ПРАВИЛО ИМЁН для контролов, у которых в данных `ID` ПУСТ.

    Одно и то же правило нужно ТРЁМ местам: импортёру (`asda_jui_generic_umg.py`), шагу
    `asda_ui_missing_text_nodes.py` (класс S1a — подписи) и шагу
    `asda_ui_missing_interactive.py` (класс S2 — кнопки/чекбоксы). Заведено ОДНОЙ функцией,
    чтобы имена не разъехались: разъезд означал бы дубли узлов после перегенерации.

    `pick(node)` → короткий КОД ВИДА узла (`"TXT"`, `"BTN"`, `"CHK"`, …) либо None.
    Имя: `{ЯКОРЬ}_{КОД}{NN}`, где ЯКОРЬ — `ID` ближайшего предка (у контрола своего `ID` нет),
    а при отсутствии такого предка — `ID`/`Name` корневого окна; `NN` — порядковый номер среди
    контролов ЭТОГО ЖЕ якоря И ЭТОГО ЖЕ вида в порядке документа. Индекс обхода (как в
    `base = имя_idx`) НЕ используется намеренно: он сдвигается от любой правки данных, а имя
    узла должно быть стабильным.

    Возвращает список `(node, parent, name, kind)` в порядке обхода документа (`parent` = None
    у корня). ⚠ `calc_positions`/`compute_anchors` не вызывает — координаты берёт вызыватель.
    """
    found = []          # (node, parent, anchor, kind)
    # ★★★ ЗАНЯТЫЕ ИМЕНА окна. Схема `{ЯКОРЬ}_{КОД}{NN}` МОЖЕТ СОВПАСТЬ с настоящим `ID` соседа:
    #   в `SHELLITEM_STORE2_WIN` есть контролы с `ID = "SHELLITEM2_POP00_TXT00"` и `..._TXT01`
    #   (`gamemain_loc.ini:294247`), а якорем для их безымянных соседей выступает как раз
    #   `SHELLITEM2_POP00`. Без этой брони шаг решил бы, что узел «уже есть», и оставил бы
    #   дефект (2 контрола из 209). Резервируем всё, что порождает импортёр из `ID`/`Name`.
    taken = set()
    def walk(n, parent, anchor):
        s = _san(n.idc) if n.idc else ""
        if s:
            taken.add(s); taken.add(s + "_Text")
        taken.add(_san(n.idc or n.name) + "_grp")
        kind = pick(n)
        if kind:
            found.append((n, parent, anchor, kind))
        kid_anchor = n.idc or anchor
        for c in n.children:
            walk(c, n, kid_anchor)
    walk(root, None, "")
    seq = {}
    out = []
    for n, parent, anchor, kind in found:
        a = _san(anchor or root.idc or root.name)
        k = seq.get((a, kind), 0)
        while "%s_%s%02d" % (a, kind, k) in taken:   # номер занят настоящим `ID` — берём следующий
            k += 1
        nm = "%s_%s%02d" % (a, kind, k)
        seq[(a, kind)] = k + 1
        taken.add(nm)
        out.append((n, parent, nm, kind))
    return out


# ---------------------------------------------------------------------------
# ★★★ КЛАСС S2: пустой `ID` + ИНТЕРАКТИВНЫЙ скин ⇒ кликабельного виджета нет
# ---------------------------------------------------------------------------
# Типы скинов, для которых импортёр делает интерактивный виджет (белый список
#   `asda_jui_generic_umg.add_control`: `if node.idc and stype in (...)`).
IACT_TYPES = ("PushButton", "EditBox", "ComboBox", "CheckButton", "RadioButton")

# Код вида узла в имени (см. `anchored_names`). Разные коды у разных типов, чтобы имя само
#   говорило, ЧТО это, и чтобы нумерация чекбоксов не смешивалась с нумерацией кнопок.
IACT_KIND = {"PushButton": "BTN", "CheckButton": "CHK", "RadioButton": "SEL",
             "ComboBox": "CMB", "EditBox": "EDT"}

# ★★★ КОНТРОЛЫ КЛАССА S2, КОТОРЫЕ УЖЕ СТРОИТ РАНТАЙМ ПОРТА. Создавать им узел в ассете НЕЛЬЗЯ:
#   получилось бы ДВА кликабельных виджета в одной точке, и верхний съел бы клики у нижнего
#   (закон `selfhittest-vs-hittest-invisible`, прецеденты с магазином и складом).
#   Снято двумя способами: (1) grep имён планов `{скин}_{idx}_0`, которые ищет код;
#   (2) grep имени СКИНА по всем `Source/**`. Формат: окно → {IDNum: где именно строит порт}.
#   ⚠ Убирать отсюда строку можно ТОЛЬКО вместе с удалением соответствующего кода порта.
# ★★★ РЕВИЗИЯ 2026-08-27 (решение владельца «если это СТАТИКА, пусть живёт в виджете»).
#   Из списка УБРАНЫ 5 контролов, у которых существование, геометрия и класс полностью заданы
#   данными `.JUI` — вместе с УДАЛЕНИЕМ соответствующего кода порта (иначе дубль и перехват мыши):
#     2617  `CHAT_TAB_WIN`          → узел `CHAT_TAB_WIN_B2_C1_CHK00`
#     68357 `Premium_Pick_View_Win` → узел `Premium_Pick_View_Win_BTN00`
#     68511 `Premium_Pick_select_2` → узел `Premium_Pick_n1_BTN00`
#     50392 `ItemOptionValue_WIN`   → узел `ItemOptionValue_Top_Text_BTN00`
#     69081 `Primium_Item_post_WIN` → узлы `Primium_Item_post_BOX_SEL00_OPT0..6`
#   Оставшиеся 17 — НЕ статика, разбор причин у каждой записи ниже.
PORT_RUNTIME_OWNED = {
    # ★★★ НЕ СТАТИКА — ЭКРАННУЮ РАСКЛАДКУ СТРОИТ 3D-СЦЕНА, А НЕ ДАННЫЕ.
    #   Кнопки «крутить» пяти автоматов гачи (оверлеи `GACHA_SPIN_0..4` над планами `Mac_Btn_2_*_0`).
    #   Доказательства, что данные `.JUI` НЕ описывают экранную раскладку этих кнопок:
    #     (1) все пять полос `Premium_PickBAR_{1..5}` (68010+10i) объявлены в ОДНОЙ точке —
    #         `abs = (0, 0, 76, 62)`, а сами кнопки — в одной точке `(-35, 92, 150, 25)`
    #         (в собранных WBP это `Mac_Btn_2_grp…_grp_4`, все на `off(93, 92)`);
    #     (2) позицию каждой полосы КАЖДЫЙ КАДР ставит проекция 3D-автомата
    #         (`AsdaPremiumGachaSubsystem.cpp:2854-2879`, `SceneActor->GetSlotScreenPos`;
    #          эталон `ui_core.c:7128-7148` — «(screenX − w/2, screenY − h/2 + yOffset)»);
    #     (3) сколько полос ВИДНО, решает сервер: `N = SceneActor->GetMachineCount()`.
    #   Следствие: заводить узлы в ассете нельзя — пять кнопок легли бы штабелем, и защита
    #   от перехвата мыши (ниже) законно отвергла бы 68062 как накрывающий 68061.
    "Premium_PickBAR_Win": {n: "Private/UI/AsdaPremiumGachaSubsystem.cpp:461-469 (НЕ статика: "
                               "полосы позиционирует проекция 3D-сцены, число полос — сервер)"
                            for n in range(68061, 68066)},
    # ★★★ НЕ СТАТИКА — ПЛАН ДВОЙНОГО НАЗНАЧЕНИЯ, ВИДИМОСТЬ ОТ ДАННЫХ СЕРВЕРА.
    #   12 кнопок покупки военного магазина. Убирать тут НЕЧЕГО: порт не строит виджета вовсе,
    #   он опрашивает ЖИВУЮ ГЕОМЕТРИЮ уже существующего плана `BKBtn_W122_{B+2}_0`
    #   (`AsdaWarShopSubsystem.cpp:354` — `C.BuyButton = W`, `:743` — `IsCursorOverWidget`).
    #   Этот же план лежит в `C.CardParts`, а `ShowCard` (`:515-521`) гасит ВЕСЬ список вместе
    #   с карточкой; сколько карточек заполнено, решает страница строк сервера (`BuildPage`,
    #   `RowIndex < 0`). Своя кнопка осталась бы видимой и кликабельной у пустых карточек.
    "HONOR_itemmall_Win": {67212 + 10 * i: "Private/UI/AsdaWarShopSubsystem.cpp:354, :743 "
                                           "(НЕ статика: план = элемент CardParts, видимость "
                                           "карточки задаёт страница сервера)"
                           for i in range(12)},
}


def _iact_planes(node, skins):
    """Видимые текстурные планы контрола — ТОЧНО как их отбирает `add_control` импортёра."""
    sk = skins.get((node.name or "").lower())
    l, t, w, h = node.abs
    if not (sk and sk["planes"] and w > 0 and h > 0):
        return sk, []
    return sk, [p for p in sk["planes"]
                if p["tex"] and not p["invisible"] and p.get("fillmode", 0) != 5]


def plane_frame(pl, i):
    """Кадр-состояние `i` плана скина → `(имя текстуры, uv, diffuse)`.

    ★ ОБЩАЯ ТОЧКА с `asda_jui_generic_umg.add_control` (вложенная `frame_of`) и шагом
    `asda_ui_missing_interactive.py`. Кадр, у которого нет ни своей текстуры, ни своего UV,
    заменяется кадром 0, а при его отсутствии — самим планом (скин задал не все состояния).
    """
    fr = pl.get("frames", {})
    f = fr.get(i) or fr.get(0)
    if f and (f.get("tex") or f.get("uv") != (0, 0, 1, 1)):
        return (f.get("tex") or pl["tex"], f["uv"], f["diffuse"])
    return (pl["tex"], pl["uv"], pl["diffuse"])


def _rect_overlap(a, b):
    """Площадь пересечения двух прямоугольников (l, t, w, h) в базовых координатах JUI."""
    dx = min(a[0] + a[2], b[0] + b[2]) - max(a[0], b[0])
    dy = min(a[1] + a[3], b[1] + b[3]) - max(a[1], b[1])
    return dx * dy if (dx > 0 and dy > 0) else 0.0


def missing_interactive_nodes(root, skins, win_id=None):
    """★★★ КОНТРОЛЫ КЛАССА S2: пустой `ID` + скин ИНТЕРАКТИВНОГО типа.

    ЗАЧЕМ. `add_control` создаёт кнопку/чекбокс/комбо ТОЛЬКО при непустом `node.idc`
    (`asda_jui_generic_umg.py`, ветка `if node.idc and stype in (...)`). У контрола без `ID`
    остаются одни планы-картинки: элемент НАРИСОВАН, а нажать не по чему — это функциональный
    дефект, а не косметика. По всем четырём `.ini` таких контролов 151 в 24 окнах.

    Имя узла — общее правило `anchored_names`: `{ЯКОРЬ}_{BTN|CHK|SEL|CMB|EDT}{NN}`.
    ★ Имена присваиваются ВСЕМ найденным контролам ДО отсева, поэтому изменение списков-исключений
      не сдвигает имена уже созданных узлов.

    ОТСЕВ (возвращается отдельным списком — это НЕ «пропало», это осознанный отказ):
      • `w <= 0` или `h <= 0` — импортёр для такого контрола не делает вообще ничего;
      • контрол из `PORT_RUNTIME_OWNED` — кликабельный виджет ему уже строит код порта;
      • ★★★ ПЕРЕХВАТ МЫШИ: контрол ВИДИМ (ни он, ни предки не `Visible = "0"`) и его прямоугольник
        пересекается с прямоугольником ДРУГОГО видимого интерактива окна — своего (`ID` есть) или
        принятого ранее контрола этого же класса. Новый виджет кладётся ПОСЛЕДНИМ в панель, то
        есть ВЫШЕ соседей, и съел бы их клики (прецеденты: магазин, склад).
        Свёрнутый (`Collapsed`) виджет мышь не ловит, поэтому у скрытых контролов пересечения
        не проверяются — они воспроизводят раскладку оригинала как есть.

    ★★★ ЧАСТНЫЙ СЛУЧАЙ, ПРОВЕРЕННЫЙ ПО ЭТАЛОНУ: `CHAR_NEW_WIN` / `CHAR_NEW_box1_BTN01`.
    В данных ВНЕШНИЙ `PushButton` со скином `RBox_Lg` содержит СВОЕГО РЕБЁНКА `CHAR_NEW_box1_Rbtn`
    (IDNum 84007, кнопка RESET) — оба 90x19 в одной точке. Правило отвергает внешний как перехват
    мыши, и это СОВПАДАЕТ с оригиналом, а не обходит его:
      (1) у внешнего контрола `IDNum` ОТСУТСТВУЕТ (`gamemain_jui.ini`: `IDNum = "4294967295"`,
          парсер даёт -1), а диспетчер оригинала адресует контрол ИМЕННО по `IDNum`
          (`OnControlMessage(this, IDNum, msg)`) — обработчика у безномерного контрола быть не может;
      (2) второй метод (закон `false-negatives-method`): скан 4-байтовых констант в самом клиенте
          `client/system/AsdaStory.exe` (13 344 768 б, сборка эталона) — 84000 / 84005 / 84006 /
          84007 / 84399 встречаются НОЛЬ раз, тогда как IDNum соседних окон находятся штатно
          (68011 x2, 68061 x2, 68357 x12, 68511 x2, 50392 x2, 69081 x2, 67212 x5, 2617 x4).
          То есть окно 84000 клиент не обслуживает вовсе — ни внешний контрол, ни RESET 84007.
    Вывод: внешний узел остаётся БЕЗ кнопки; его вид даёт уже существующий план `RBox_Lg_173_0`,
    а клик — ребёнок `CHAR_NEW_box1_Rbtn`, которого импортёр сделал по его собственному `ID`.

    Возврат: `(entries, skipped)`, где
      entries = [(node, parent, name, stype, planes)] — то, что надо создать;
      skipped = [(node, name, stype, причина)]        — то, что создавать НЕЛЬЗЯ.
    ⚠ Требует уже посчитанных `node.abs` (`compute_anchors`) — прямоугольники берутся оттуда.
    """
    win = win_id or _san(root.idc or root.name)
    owned = PORT_RUNTIME_OWNED.get(win, {})

    def pick(n):
        st = (skins.get((n.name or "").lower()) or {}).get("type", "")
        return IACT_KIND.get(st) if (not n.idc) and st in IACT_TYPES else None

    # Эффективная скрытость — ТОТ ЖЕ расчёт, что `eff_hidden` в `build_tree` импортёра
    #   (корень исключён: у него `Visible = "0"` означает «окно закрыто», а не «узел свёрнут»).
    hid = {}
    def mark(n, h, is_root):
        eff = h or ((not is_root) and getattr(n, "visible", 1) == 0)
        hid[id(n)] = eff
        for c in n.children:
            mark(c, eff, False)
    mark(root, False, True)

    # Прямоугольники ВИДИМЫХ интерактивов, которые импортёр уже создаёт (у них есть `ID`).
    busy = []
    def scan(n):
        st = (skins.get((n.name or "").lower()) or {}).get("type", "")
        l, t, w, h = n.abs
        if n.idc and st in IACT_TYPES and w > 0 and h > 0 and not hid[id(n)]:
            busy.append((_san(n.idc), (l, t, w, h)))
        for c in n.children:
            scan(c)
    scan(root)

    entries, skipped = [], []
    for n, parent, nm, _kind in anchored_names(root, pick):
        st = (skins.get((n.name or "").lower()) or {}).get("type", "")
        l, t, w, h = n.abs
        if w <= 0 or h <= 0:
            skipped.append((n, nm, st, "размер %gx%g — импортёр не создаёт ничего" % (w, h)))
            continue
        if n.idnum in owned:
            skipped.append((n, nm, st, "кликабельный виджет уже строит порт: %s" % owned[n.idnum]))
            continue
        if not hid[id(n)]:
            bad = None
            for nm2, r2 in busy:
                a = _rect_overlap((l, t, w, h), r2)
                if a > 4.0:
                    bad = (nm2, a)
                    break
            if bad:
                skipped.append((n, nm, st, "ПЕРЕХВАТ МЫШИ: накрыл бы `%s` (%d px²)"
                                % (bad[0], int(bad[1]))))
                continue
            busy.append((nm, (l, t, w, h)))     # принятый узел сам становится препятствием
        _sk, planes = _iact_planes(n, skins)
        entries.append((n, parent, nm, st, planes))
    return entries, skipped


# ---------------------------------------------------------------------------
# ★★★ КЛАСС S3: пустой `ID` + скин-ДЕРЖАТЕЛЬ ИКОНКИ ⇒ узла с адресуемым именем нет
# ---------------------------------------------------------------------------
# ЧТО ЭТО. Тот же корень, что у S1a и S2: `add_control` именует виджет по `node.idc`, а при пустом
#   `ID` план приезжает под именем СКИНА с индексом обхода (`NoImage_icon_<idx>_0`, закон
#   `jui-importer-widget-naming`). Для ПОДПИСИ это класс S1a, для КНОПКИ — S2; для ЯЧЕЙКИ-ИКОНКИ
#   (скин `NoImage_icon`, Type = "Text", один текстурный план) отдельного класса не было, и
#   подсистема порта такую ячейку адресовать не могла: индекс обхода сдвигается от любой правки
#   данных. Правило имён — ОБЩЕЕ, `anchored_names`: `{ЯКОРЬ}_ICO{NN}`.
#
# ПОЧЕМУ ТАБЛИЦА, А НЕ «ВСЕ ПОДРЯД». Контролов с пустым `ID` и скином `NoImage_icon` в четырёх
#   `.ini` СОТНИ, и у большинства ячейку уже адресует владелец другим способом (по цепочке детей —
#   склад, сумки 5…8; явным списком имён — `UAsdaIconCellGrid::SetupNamed`, призы гачи 68311…68335).
#   Плодить им вторые узлы нельзя: получился бы двойной слой картинки. Поэтому берём ТОЛЬКО те,
#   которые разобраны по эталону и нужны коду порта. Формат: `IDNum → зачем` (ссылка обязательна).
ICON_HOLDER_NODES = {}
# ★★★ АУКЦИОН `EXCHANGE_WIN` (13000), гнёзда самоцветов строк выдачи поиска.
#   Данные: контейнер `EXCHANGER_BB{n}_0` (13510/13520/…/13580, Size 4x4) и ПЯТЬ иконок 16x16
#   внутри него (13511…13515, 13521…13525, …, 13581…13585). ★ У строки 1 иконки имеют `ID`
#   (`EXCHANGER_BB1_1`…`_5`), у строк 2…8 `ID` ПУСТ — `gamemain_jui.ini` строки 230996…232974.
#   Разбор: `Reports/npc_exchange_win_13000.md` §2.4.
#   ★ Что делает оригинал: `uiAvatarMapItemToCostumeSlot@0xD423A0` (`ui_core.c:50409-50455`)
#   отображает клик по ЛЮБОЙ из шести IDNum строки в НОМЕР СТРОКИ (0…7) и выбирает её лот.
#   Иконку самоцвета туда не рисует НИКТО: ни `CItemMediation_BuildUI`, ни
#   `CSatisfyWindow_RefreshItemSlots@0xD58810` (рисует только 201…205 — иконка/имя/цена/продавец/
#   галочка). Второй метод: во всём `ida/export/AsdaStory_ida.c` (1 293 125 строк) константы
#   13510…13585 встречаются РОВНО ДВА раза — обе в этом же диапазоне-`if`.
for _row in range(2, 9):                      # строки 2…8; у строки 1 `ID` есть, правило не нужно
    for _k in range(1, 6):                    # пять гнёзд
        ICON_HOLDER_NODES[13500 + _row * 10 + _k] = (
            "аукцион 13000: гнездо самоцвета %d строки выдачи %d "
            "(`uiAvatarMapItemToCostumeSlot@0xD423A0`)" % (_k, _row))


def missing_icon_nodes(root, skins, win_id=None):
    """Контролы класса S3 из таблицы `ICON_HOLDER_NODES`, найденные в этом окне.

    Возврат — тот же 5-кортеж, что у `missing_interactive_nodes`, с псевдо-типом `"IconHolder"`:
    `[(node, parent, name, "IconHolder", planes)]`. Отсева здесь нет: таблица уже и есть отсев.
    ⚠ Требует посчитанных `node.abs` (`compute_anchors`).
    """
    def pick(n):
        return "ICO" if ((not n.idc) and n.idnum in ICON_HOLDER_NODES) else None

    del win_id                                 # имя окна нужно только вызывателю, для логов
    out = []
    for n, parent, nm, _kind in anchored_names(root, pick):
        _sk, planes = _iact_planes(n, skins)
        out.append((n, parent, nm, "IconHolder", planes))
    return out


# ---------------------------------------------------------------------------
# CLI-отладка
# ---------------------------------------------------------------------------
def _dump(node, skins, depth=0):
    sk = resolve_skin(skins, node.name)
    tex = sk["tex_name"] if sk else "?"
    uv = sk["uv"] if sk else None
    print("  "*depth + "[%s] id=%s idnum=%s vis=%d abs=%s tex=%s uv=%s cap=%r" % (
        node.name, node.idc, node.idnum, node.visible,
        tuple(round(x,1) for x in node.abs), tex, uv, node.caption[:20]))
    for c in node.children:
        _dump(c, skins, depth+1)

if __name__ == "__main__":
    target = None
    if "--dump" in sys.argv:
        i = sys.argv.index("--dump")
        if i+1 < len(sys.argv):
            target = int(sys.argv[i+1])
    roots = parse_jui()
    skins = load_skin()
    print("roots: %d, skins: %d" % (len(roots), len(skins)))
    if target is not None:
        n = find_by_idnum(roots, target)
        if n:
            calc_positions(n, parent_abs=None, is_root_window=True)
            _dump(n, skins)
        else:
            print("IDNum %d not found" % target)
