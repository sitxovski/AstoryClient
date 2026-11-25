# -*- coding: utf-8 -*-
# Copyright (c) 2026 @sitxovski (Discord)
# asda_ui_text_block_align.py — ★★★ ВЕРТИКАЛЬНОЕ ПОЛОЖЕНИЕ ПЕРЕНЕСЁННОГО ТЕКСТА И ВЫРАВНИВАНИЕ ТЕКСТА ПОЛЯ ВВОДА
#   ПО ДАННЫМ .JUI (26.09.2026, решение владельца по ассетам ремесла, п. 3 «б» и «д»).
#   Отчёт: `Reports/craft/craft_owner_items.md` §3.
#
# ═══ ЧТО В ОРИГИНАЛЕ (эталон, а не «красивее») ══════════════════════════════════════════════════
#   (д) Text-контрол с переносом. `ctrlRenderLabelQuad@0x12E5440` (ui_core.c:110741-110752): флаги текста =
#       `TextAlign | поле 926 | (поле 925 == 1 ? 0x2010 : 0x100)`; поле 925 — `TextOutLine` СКИНА (у скина `Black`
#       строк рецептов = "1", JuiSkin.ini:32029) ⇒ 0x10 = перенос по словам. Рисует `textFont_DrawRichText`: при
#       вертикальном центре (маска 0x4) верх текста = середина rect − шаг строки × ЧИСЛО СТРОК / 2
#       (font_text.c:8471-8476 / 8895-8900, `textFontCountWrapLines`), при нижнем (0x8) = низ − шаг × строк.
#       То есть оригинал центрирует ВЕСЬ блок строк. Импортёр же ставил TextBlock так, будто строка одна:
#       верх = середина − 14/2, дальше строки текут вниз — вторая строка имени рецепта 76024+10j («изготовл. малое
#       зелье / здоровья») ложилась на нижнюю границу строки таблицы (кадр `asda_craft_w012_open`).
#       ПРАВКА: слот текста — авторазмер, точка привязки = середина (низ) rect по вертикали, `Alignment.Y` = 0.5 (1.0),
#       ширина переноса — фиксированная `WrapTextAt` = ширина rect − TextPos.X (как `textFontCountWrapLines`), без
#       `AutoWrapText` (в CanvasPanel он не получает ширины на проходе измерения — закон ui-autowrap-canvas-gotcha).
#       По горизонтали — та же маска: лево / центр / право через `Alignment.X` 0 / 0.5 / 1.
#   (б) Поле ввода. `uiEditBoxRender@0x12FA100` (ui_core.c:117132): текст рисуется с флагами
#       `EXS TextAlign (this+3240) | 0x2010` — у поля количества 77537 `TextAlign = 5` (центр + центр,
#       gamemain_jui.ini:367248). `UEditableText` импортёра выравнивания не получал (всегда слева) и рисует строку
#       от ВЕРХА своего прямоугольника ⇒ «1» стояла в левом верхнем углу поля 40×20.
#       ПРАВКА: `Justification` из маски (0x1 центр, 0x2 право); по вертикали — верх прямоугольника поля сдвигается
#       так, чтобы строка высотой `GEN_TEXT_LINE_PX` (14, та же константа, что у вертикального центра TextBlock
#       импортёра) стояла по середине rect, низ поля не меняется (кликабельная зона не выходит за контрол).
#
# ═══ ОХВАТ ═══════════════════════════════════════════════════════════════════════════════════════
#   Только окна ремесла (`WINDOWS`), по правилу из данных — список печатает `--plan`. Импортёр
#   `asda_jui_generic_umg.py` с 26.09 строит эти узлы ТЕМИ ЖЕ функциями (`wrap_block_slot` / `editbox_align`),
#   поэтому после перегенерации шаг найдёт 0 правок.
#   ★★★ ОХВАТ ИМПОРТЁРА — ТОТ ЖЕ ЗАКРЫТЫЙ СПИСОК (правка 26.09.2026 по ревизии `craft_owner_items_REVIEW.md`,
#   СРЕДН-1). Правила (д)/(б) по данным сработали бы в 154 ЧУЖИХ окнах: 1903 текста и 62 поля ввода (склад,
#   сумка, обмен, лоток, чат, нумпад, ручки `*_WIN_MOVE` …). Решение владельца было «без запуска на другие
#   окна», поэтому импортёр применяет правила ТОЛЬКО к окнам `IMPORTER_SCOPE_WIN_IDS` (= ключи `WINDOWS`),
#   прочим окнам — прежнее поведение байт в байт. РАСПРОСТРАНЕНИЕ НА ДРУГИЕ ОКНА — ОТДЕЛЬНОЕ РЕШЕНИЕ ВЛАДЕЛЬЦА
#   (реестр `Docs/Port/PORT_PENDING_ON_SYSTEMS.md`, запись «Правила текста (д)/(б) импортёра — вне ремесла»).
#   Сверка охвата без редактора: `python asda_ui_text_block_align.py --scope` (4 ini: вне ремесла — 0).
#
# ═══ (а) ПОДПИСЬ 5521 ПОД ИКОНКОЙ — [ИСПРАВЛЕНИЕ ДЕФЕКТА ОРИГИНАЛА] (решение координатора 26.09.2026) ══════
#   Кнопки 77068 / 77151 / 77251 «Создание предмета»: подпись (`CaptionInfo` «3 0|−1 … 5») центрируется в
#   rect кнопки со сдвигом 3 px, иконка (`WkBtn_IconMinus` соседом 2…18 / `WkBtn_IconCheck` ребёнком 4…20)
#   рисуется ПОВЕРХ. Русская строка (101 px атласом `FONT0.BIN`) длиннее корейской «제작진행» ⇒ у оригинала
#   «С» и край «о» под иконкой (замер — `craft_owner_items.md` §3.1). Это ОТСТУПЛЕНИЕ от оригинала ради
#   читаемости: прямоугольник подписи начинается правее иконки (`CAPTION_FIX`), строка центрируется в
#   оставшейся части кнопки, кегль 8 → 7 (иначе 101 px не входят в 96/98 px). Точечно, идемпотентно, только три узла `{idc}_Text`; прочее — «осмотреть».
#   Откат — бэкап ассетов набора `CAPTION_BACKUP_SET` (или `ASDA_CRAFT_CAPTION_FIX=0` — шаг правку не делает).
#
# ═══ БЕЗОПАСНОСТЬ ═════════════════════════════════════════════════════════════════════════════════
#   • Узел трогается, только если он в ТОЧНОСТИ в виде, который даёт старый импортёр (слот = формула от плана
#     контрола `{idc}_{N}_0`, перенос/выравнивание как у импортёра). Уже исправленный — пропуск (идемпотентно).
#     Любой другой вид — строка «осмотреть», узел НЕ трогается (возможна ручная правка владельца).
#   • Снимок дерева до/после (класс, родитель, слот, видимость, непрозрачность, кисть, текст, цвет): у всех
#     прочих узлов — без изменений, «потеряно узлов: 0» — иначе исключение ДО сохранения.
#   • Ручной запуск пишет ассет только после бэкапа `_ensure_backup` → `asda_headless_tool.backup_file` (закон
#     backup-before-edit; копия создаётся самим шагом, если её ещё нет; есть — это исходник до первой правки, не трогается).
#
# Запуск (редактор ЗАКРЫТ):
#   python Tools\run_headless_safe.py Tools\asda_ui_text_block_align.py --marker "ТЕКСТ-БЛОК: ГОТОВО"
#   ASDA_TEXTBLOCK_DRYRUN=1 — сухой прогон (ничего не пишет); ASDA_TEXTBLOCK_DUMP=<файл.json> — полный дамп узлов
#   окон ремесла (для сверки до/после). Вне редактора: `python asda_ui_text_block_align.py --plan`.
#   ASDA_TEXTBLOCK_PARITY=1 — сухой прогон ИМПОРТЁРА (только чтение): слоты правил (д)/(б), посчитанные кодом
#   импортёра, против текущих ассетов (маркер «ПАРИТЕТ: ГОТОВО»). `--scope` — охват правил по 4 ini (без редактора).
import json
import os
import sys
import traceback

try:
    import unreal
except ImportError:
    unreal = None

HERE = os.path.dirname(os.path.abspath(__file__))
if HERE not in sys.path:
    sys.path.insert(0, HERE)
import asda_jui_parse as J  # noqa: E402

import asda_headless_tool as HT  # noqa: E402  ★ Т3 (11.10.2026): единая резервная копия `backup_file`
# ★ дедуп №2 В1 Т12: общие помощники правки WBP (обход дерева, канвасный слот, смещения) — asda_ui_widget_common
import os as _uwc_os, sys as _uwc_sys
if _uwc_os.path.dirname(_uwc_os.path.abspath(__file__)) not in _uwc_sys.path:
    _uwc_sys.path.insert(0, _uwc_os.path.dirname(_uwc_os.path.abspath(__file__)))
import asda_ui_widget_common as _UWC   # noqa: E402

# Бэкап ДО записи (★ Т3, 11.10.2026): набор `@@ASTORY_DEV_ROOT@@\_backup\<дата запуска>_craft_assets\<путь от @@ASTORY_DEV_ROOT@@>`
#   через `HT.backup_file` (была прошитая дата `2026-09-26_craft_assets`).
BACKUP_SET = "craft_assets"
AREA_ROOT = r"@@ASTORY_DEV_ROOT@@"
CONTENT_DIR = r"@@ASTORY_DEV_ROOT@@\AstoryClient\Content"
INI_DIR = r"@@ASTORY_DEV_ROOT@@\Data\Decoded\jui"

# Окна ремесла: IDNum окна .JUI → ассет. Под-окна 77100/77200 импортёр кладёт в ассет окна 77000.
WINDOWS = {
    76000: "/Game/AsdaAssets/UI/Player_UI/Craft/WBP_ITEM_making_WIN",
    77000: "/Game/AsdaAssets/UI/Player_UI/Craft/WBP_ITEM_making_R_BOX",
    77500: "/Game/AsdaAssets/UI/Player_UI/Craft/WBP_ITEM_making_Mark_WIN",
}

# ★★★ ЗАКРЫТЫЙ СПИСОК ОКОН, в которых ИМПОРТЁР применяет правила (д)/(б) (СРЕДН-1 ревизии 26.09.2026). IDNum
#   ВЕРХНЕГО окна .JUI (под-окна 77100/77200 лежат внутри 77000). Расширять — только решением владельца
#   (вне ремесла правила изменили бы 1903 текста и 62 поля ввода в 154 окнах — `--scope`).
IMPORTER_SCOPE_WIN_IDS = frozenset(WINDOWS)


def importer_scope(root):
    """Импортёр вправе применять правила (д)/(б) к окну `root` (узел верхнего окна .JUI)? — только окна ремесла."""
    return getattr(root, "idnum", -1) in IMPORTER_SCOPE_WIN_IDS


# ★★★ (а) [ИСПРАВЛЕНИЕ ДЕФЕКТА ОРИГИНАЛА] ПОДПИСЬ 5521 ПРАВЕЕ ИКОНКИ (26.09.2026, решение координатора; разбор — шапка).
#   Кнопка (IDNum) → окно .JUI. Узел подписи — `{idc}_Text` импортёра (caption-ветка: rect кнопки со сдвигом
#   `CaptionInfo[0]` = 3, центр). Иконка — `WkBtn_Icon*` соседом/ребёнком кнопки (данные .JUI).
CAPTION_FIX = {77068: 77000, 77151: 77000, 77251: 77000}
CAPTION_GAP_L = 1.0          # зазор между правым краем иконки и прямоугольником подписи, px базы
CAPTION_PAD_R = 3.0          # правое поле = левому сдвигу подписи оригинала (`CaptionInfo[0]` = 3) — рамка кнопки
# Сужение строки: «Создание предмета» шрифтом порта (Regular 8) = 101 px (`asda.UI.DumpTextMetrics`, лог
#   `Reports/craft/caption_probe_before.log`), свободно правее иконки — 98 px (77068) / 96 px (77151/77251).
#   Сужаем КЕГЛЕМ (`FSlateFontInfo::Size` — float), гарнитура та же. Замер `asda.UI.DumpTextMetrics` в -game (масштаб 1):
#   кегль 8 → 101 px (строка 13), 7.5 и 7.25 → 98 px (строка 12: растр 10 px — не входит в 96 px, «а» ложится на рамку,
#   «С» вплотную к иконке; кадры `cb2_`/`cb4_`), 7 → 84 px (строка 10: растр 9 px) — поля 6…7 px с обеих сторон
#   (кадр `cb3_`). Промежуточного размера нет: Slate растрирует целым пикселем. Отчёт — `importer_scope_fix.md` §4. ⚠ `LetterSpacing` НЕ ГОДИТСЯ (проверено в -game 26.09): Slate переводит его в px с
#   ОТБРАСЫВАНИЕМ дробной части (`SlateTextShaper.cpp:436-438`, `(int16)(LetterSpacing × масштаб × Size / 1000)`) —
#   при кегле 8 всё от −1 до −124 даёт 0, а −125 и больше сжимает каждую пару знаков на целый пиксель (≈ −16 px,
#   буквы слипаются).
CAPTION_FONT_SIZE = 7.0
# Вертикаль: подпись импортёра стоит верхом строки в (середина кнопки − 14/2); у кегля 7 строка на 3 px ниже (13 → 10),
#   поэтому верх сдвигается на половину разницы — центр строки остаётся там же, где у кегля 8.
CAPTION_TOP_SHIFT = 1.5
FONT_PX_IMPORTER = 8           # кегль подписей импортёра (`FONT_PX` в asda_jui_generic_umg.py)
CAPTION_BACKUP_SET = "craft_caption"   # набор бэкапа правки (а) (была прошитая папка `2026-09-26_craft_caption`)
CAPTION_FIX_ON = os.environ.get("ASDA_CRAFT_CAPTION_FIX", "1") != "0"


# Константы импортёра — ТЕ ЖЕ значения, что в `asda_jui_generic_umg.py` (GEN_TEXT_LINE_PX, порог переноса).
LINE_PX = 14            # высота строки, по которой импортёр центрирует однострочный текст
WRAP_MIN_H = 28         # импортёр включает перенос Text-контролу высотой от 2 строк
EPS = 0.05


def log(m):
    """Строка журнала шага (в редакторе — в лог UE, вне — в stdout)."""
    if unreal:
        unreal.log("[текст-блок] %s" % m)
    else:
        print("[текст-блок] %s" % m)


# ───────────────────────────── ОБЩИЕ ПРАВИЛА (ими же пользуется импортёр) ─────────────────────────────
def text_mask(node):
    """Маска выравнивания текста контрола: `EXS TextAlign`, иначе `CaptionInfo[3]` — как у импортёра.
    ★ 07.10.2026: правило живёт в парсере (`J.label_mask`) — его зовёт сам импортёр (`add_control`)."""
    return J.label_mask(node)


def text_justify(mask):
    """Горизонталь маски выравнивания → 0 LEFT / 1 CENTER / 2 RIGHT (ключ `_JUSTIFY`) — `J.label_justify`."""
    return J.label_justify(mask)


def text_off(node, is_text_skin):
    """Смещение текста внутри rect: у Text-контролов — `EXS TextPos`, иначе `CaptionInfo[0..1]` (как импортёр)."""
    tp = getattr(node, "txt_pos", None)
    if is_text_skin and tp is not None:
        return float(tp[0]), float(tp[1])
    co = getattr(node, "cap_off", (0, 0))
    return float(co[0]), float(co[1])


def is_wrap_block(stype, h, skin_textoutline, mask):
    """Область правила (д): Text-контрол, который ПЕРЕНОСИТ и оригинал (поле 925 = `TextOutLine` скина = 1 ⇒ 0x2010),
    и импортёр (высота от двух строк), с вертикальным центром или низом. У скинов с `TextOutLine = 0` оригинал
    строки не переносит (0x100) — это другое расхождение, здесь не трогается."""
    return stype == "Text" and h >= WRAP_MIN_H and int(skin_textoutline or 0) == 1 and bool(mask & 0xC)


def wrap_block_slot(l, t, w, h, offx, offy, mask):
    """★ (д) Слот ПЕРЕНОСИМОГО текста по эталону (`textFont_DrawRichText`, вертикаль — по числу строк).
    Возврат None, если правило не применяется (нет вертикального центра/низа), иначе словарь:
      pos (x, y) — точка привязки, align (ax, ay) — `Alignment` слота, wrap_at — ширина переноса."""
    if not (mask & 0x4 or mask & 0x8):
        return None
    left, top, right, bottom = l + offx, t + offy, l + w, t + h      # rect текста: `rectLeft/Top += TextPos`
    # ★ 08.10.2026 (ревизия `poststeps_tools_harden_REVIEW.md`, МЕЛК-2): горизонталь — общая точка импортёра
    #   `J.label_justify` (центр раньше права), здесь только перевод 0/1/2 в доли слота.
    ax = (0.0, 0.5, 1.0)[J.label_justify(mask)]
    ay = 0.5 if (mask & 0x4) else 1.0
    x = left + (right - left) * ax
    y = (top + bottom) / 2.0 if (mask & 0x4) else bottom
    return {"pos": (x, y), "align": (ax, ay), "wrap_at": max(right - left, 8.0)}


def editbox_align(l, t, w, h, offx, offy, mask):
    """★ (б) Прямоугольник и выравнивание текста ПОЛЯ ВВОДА по `EXS TextAlign` (`uiEditBoxRender`).
    Возврат (offsets (L, T, W, H), justify 0/1/2): по вертикали строка высотой LINE_PX — по середине (низу) rect,
    низ поля остаётся на месте."""
    left, top, bottom = l + offx, t + offy, t + h
    if mask & 0x4:
        top2 = max(top, (top + bottom) / 2.0 - LINE_PX / 2.0)
    elif mask & 0x8:
        top2 = max(top, bottom - LINE_PX)
    else:
        top2 = top
    justify = text_justify(mask)
    return (left, top2, (l + w) - left, bottom - top2), justify


def editbox_rule(l, t, w, h, offx, offy, mask):
    """★ (б) Условие импортёра: ((L, T, W, H), justify), если правило МЕНЯЕТ поле относительно прежнего вида
    (`add_editable_text(l, t, w, h)`, LEFT), иначе None. Одна функция для импортёра и `--scope`."""
    rect, justify = editbox_align(l, t, w, h, offx, offy, mask)
    return (rect, justify) if (justify or not _near(rect, (l, t, w, h))) else None


# ───────────────────────────── ОХВАТ ИМПОРТЁРА ПО ВСЕМ 4 INI (без unreal) ─────────────────────────────
SCOPE_CLASSES = ("gamemain", "commonwin", "temp", "login")


def _class_ini(cls):
    """.ini класса так же, как `_ini_of` импортёра: приоритет локализованного."""
    p = os.path.join(INI_DIR, "%s_loc.ini" % cls)
    return p if os.path.isfile(p) else os.path.join(INI_DIR, "%s_jui.ini" % cls)


def scope_cases(gated):
    """Узлы, которым импортёр применил бы правила (д)/(б), по всем 4 ini — как в `add_control`:
    Text-ветка (узел с `ID`, скин `Text`) и EditBox-ветка (узел с `ID` или с именем правила S2 `IACT`).
    `gated=True` — с закрытым списком `importer_scope` (как сейчас), False — как было бы без него.
    Возврат: список (класс, окно-IDNum, окно-ID, вид 'text'|'edit', узел-ID)."""
    skins = J.parse_skins_full()
    out = []
    for cls in SCOPE_CLASSES:
        ini = _class_ini(cls)
        if not os.path.isfile(ini):
            continue
        for root in J.parse_jui(ini):
            if gated and not importer_scope(root):
                continue
            J.compute_anchors(root)
            iact = set(id(n) for n, _p, _nm, _st, _pl in J.missing_interactive_nodes(root, skins)[0])
            for n in _walk_nodes(root):
                sk = skins.get((n.name or "").lower()) or {}
                stype = sk.get("type", "")
                l, t, w, h = n.abs
                if w <= 0 or h <= 0:
                    continue
                mask = text_mask(n)
                if stype == "Text" and n.idc:
                    if is_wrap_block(stype, h, sk.get("textoutline", 0), mask):
                        out.append((cls, root.idnum, root.idc or root.name, "text", n.idc))
                elif stype == "EditBox" and (n.idc or id(n) in iact):
                    tp = getattr(n, "txt_pos", None) or (0, 0)
                    if editbox_rule(l, t, w, h, float(tp[0]), float(tp[1]), mask):
                        out.append((cls, root.idnum, root.idc or root.name, "edit", n.idc or "(S2)"))
    return out


def print_scope():
    """`--scope`: сколько узлов правила изменили бы БЕЗ закрытого списка и сколько меняют С НИМ (вне ремесла — 0)."""
    for gated in (False, True):
        cs = scope_cases(gated)
        craft = [c for c in cs if c[1] in IMPORTER_SCOPE_WIN_IDS]
        other = [c for c in cs if c[1] not in IMPORTER_SCOPE_WIN_IDS]
        print("=== %s ===" % ("С ЗАКРЫТЫМ СПИСКОМ (импортёр сейчас)" if gated else "БЕЗ ЗАКРЫТОГО СПИСКА (для сравнения)"))
        for name, lst in (("окна ремесла", craft), ("прочие окна", other)):
            print("  %s: текст %d, поле ввода %d, окон %d"
                  % (name, sum(1 for c in lst if c[3] == "text"), sum(1 for c in lst if c[3] == "edit"),
                     len(set((c[0], c[1], c[2]) for c in lst))))
        if not gated:
            per = {}
            for c in other:
                per.setdefault((c[0], c[2]), [0, 0])[0 if c[3] == "text" else 1] += 1
            for (cls, win), (nt, ne) in sorted(per.items(), key=lambda kv: -(kv[1][0] + kv[1][1]))[:25]:
                print("    %-9s %-32s текст %4d  поле %3d" % (cls, win, nt, ne))
    print("ОХВАТ: ГОТОВО")


# ───────────────────────────── ПЛАН ПО ДАННЫМ (без unreal) ─────────────────────────────
def _ini():
    """Тот же .ini, что читает импортёр (приоритет локализованного)."""
    p = os.path.join(INI_DIR, "gamemain_loc.ini")
    return p if os.path.isfile(p) else os.path.join(INI_DIR, "gamemain_jui.ini")


_walk_nodes = _UWC.walk_jui_nodes   # ★ дедуп №2 В1 Т12: общий помощник (тело перенесено дословно)


def plan():
    """Случаи правил в окнах WINDOWS: список словарей {kind: 'wrap'|'edit', win, asset, idc, idnum, skin, size, off, mask}."""
    roots = J.parse_jui(_ini())
    skins = J.parse_skins_full()
    out = []
    for win_id, asset in WINDOWS.items():
        win = J.find_by_idnum(roots, win_id)
        if win is None:
            raise RuntimeError("в %s нет окна %d" % (_ini(), win_id))
        for n in _walk_nodes(win):
            if not n.idc:
                continue
            sk = skins.get((n.name or "").lower()) or {}
            stype = sk.get("type", "")
            w, h = float(n.size[0]), float(n.size[1])
            if w <= 0 or h <= 0:
                continue
            mask = text_mask(n)
            if is_wrap_block(stype, h, sk.get("textoutline", 0), mask):
                kind = "wrap"
            elif stype == "EditBox" and (mask & 0xF):
                kind = "edit"
            else:
                continue
            out.append({"kind": kind, "win": win_id, "asset": asset, "idc": n.idc, "idnum": n.idnum,
                        # Смещение текста: у Text и EditBox — `EXS TextPos` (у поля ввода его читает
                        #   `CEditBox_LoadFromXml`, font_text.c:9926), иначе `CaptionInfo[0..1]`.
                        "skin": n.name, "size": (w, h), "off": text_off(n, stype in ("Text", "EditBox")),
                        "mask": mask})
    if CAPTION_FIX_ON:
        out += _caption_cases(roots)
    return out


def _caption_cases(roots):
    """(а) Случаи подписи 5521: {kind: 'caption', …, icon_rel — правый край иконки от левого края кнопки}."""
    out = []
    for btn_id, win_id in CAPTION_FIX.items():
        win = J.find_by_idnum(roots, win_id)
        if win is None:
            raise RuntimeError("в %s нет окна %d" % (_ini(), win_id))
        J.compute_anchors(win)
        parent = {}
        for n in _walk_nodes(win):
            for c in n.children:
                parent[id(c)] = n
        btn = J.find_by_idnum([win], btn_id)
        if btn is None or not btn.idc:
            raise RuntimeError("в окне %d нет кнопки %d" % (win_id, btn_id))
        bl, bt, bw, bh = btn.abs
        pool = list(btn.children) + list(parent[id(btn)].children if id(btn) in parent else [])
        icons = [c for c in pool if c is not btn and (c.name or "").lower().startswith("wkbtn_icon")
                 and bl <= c.abs[0] < bl + bw / 2.0 and bt <= c.abs[1] + c.abs[3] / 2.0 <= bt + bh]
        if len(icons) != 1:
            raise RuntimeError("кнопка %d: иконок слева %d (ожидалась 1)" % (btn_id, len(icons)))
        ic = icons[0]
        out.append({"kind": "caption", "win": win_id, "asset": WINDOWS[win_id], "idc": btn.idc, "idnum": btn_id,
                    "skin": btn.name, "size": (float(bw), float(bh)), "off": tuple(float(v) for v in btn.cap_off),
                    "mask": J.label_mask(btn), "icon": ic.name,     # ★ 08.10.2026 (МЕЛК-2): общая точка
                    "icon_rel": float(ic.abs[0] + ic.abs[2] - bl)})
    return out


def print_plan():
    """Печать случаев правил без редактора."""
    for c in plan():
        print("%s окно %d %s %s (%d) скин %s размер %s смещение %s маска %d%s"
              % (c["kind"], c["win"], c["asset"].rsplit("/", 1)[-1], c["idc"], c["idnum"], c["skin"],
                 c["size"], c["off"], c["mask"],
                 (" иконка %s до %.0f px → подпись %.0f…%.0f, кегль %.1f"
                  % (c["icon"], c["icon_rel"], c["icon_rel"] + CAPTION_GAP_L, c["size"][0] - CAPTION_PAD_R,
                     CAPTION_FONT_SIZE)) if c["kind"] == "caption" else ""))


# ───────────────────────────── РЕДАКТОР ─────────────────────────────
def _walk(root):
    """Все виджеты дерева UMG (корень включительно)."""
    st, out = [root], []
    while st:
        w = st.pop()
        if not w:
            continue
        out.append(w)
        if isinstance(w, unreal.PanelWidget):
            for i in range(w.get_children_count()):
                st.append(w.get_child_at(i))
    return out


def _slot(w):
    """Слот канваса виджета или None."""
    try:
        s = w.get_editor_property("slot")
    except Exception:
        return None
    return s if isinstance(s, unreal.CanvasPanelSlot) else None


def _r(v):
    """Округление для сверки (шум float правкой не считается)."""
    return round(float(v), 3)


def _xy(v):
    """Пара чисел из вектора Slate (в UE 5.8 часть свойств — `DeprecateSlateVector2D`)."""
    from asda_ui_handmade import _xy as hx
    return tuple(_r(c) for c in hx(v))


def _slot_sig(w):
    """(смещения L,T,R,B; анкоры; alignment; auto_size) слота канваса или None."""
    s = _slot(w)
    if s is None:
        return None
    lay = s.get_editor_property("layout_data")
    m = lay.get_editor_property("offsets")
    a = lay.get_editor_property("anchors")
    return ((_r(m.left), _r(m.top), _r(m.right), _r(m.bottom)),
            _xy(a.minimum) + _xy(a.maximum), _xy(lay.get_editor_property("alignment")),
            bool(s.get_editor_property("auto_size")), int(s.get_editor_property("z_order")))


def _brush_sig(w):
    """Кисть картинки: (текстура, UV, тинт RGBA, draw_as, размер) или None."""
    if not isinstance(w, unreal.Image):
        return None
    br = w.get_editor_property("brush")
    res = br.get_editor_property("resource_object")
    tint = br.get_editor_property("tint_color").get_editor_property("specified_color")
    try:
        uv = br.get_editor_property("uv_region")
        uvs = _xy(uv.min) + _xy(uv.max)
    except Exception:
        uvs = None
    return (res.get_path_name() if res else None, uvs, (_r(tint.r), _r(tint.g), _r(tint.b), _r(tint.a)),
            str(br.get_editor_property("draw_as")), _xy(br.get_editor_property("image_size")))


def _text_sig(w):
    """Текст: (строка, цвет, justification, auto_wrap, wrap_at, кегль, начертание, межбуквенный интервал) у TextBlock;
    (подсказка, justification, кегль, цвет стиля) у EditableText; иначе None."""
    if isinstance(w, unreal.TextBlock):
        c = w.get_editor_property("color_and_opacity").get_editor_property("specified_color")
        f = w.get_editor_property("font")
        return ("TB", str(w.get_editor_property("text")), (_r(c.r), _r(c.g), _r(c.b), _r(c.a)),
                str(w.get_editor_property("justification")), bool(w.get_editor_property("auto_wrap_text")),
                _r(w.get_editor_property("wrap_text_at")), _r(f.get_editor_property("size")),
                str(f.get_editor_property("typeface_font_name")), int(f.get_editor_property("letter_spacing")))
    if isinstance(w, unreal.EditableText):
        st = w.get_editor_property("widget_style")
        f = st.get_editor_property("font")
        c = st.get_editor_property("color_and_opacity").get_editor_property("specified_color")
        return ("ED", str(w.get_editor_property("hint_text")), str(w.get_editor_property("justification")),
                int(f.get_editor_property("size")), (_r(c.r), _r(c.g), _r(c.b), _r(c.a)))
    return None


def _snapshot(root):
    """Снимок дерева: имя → (класс, родитель, слот, видимость, непрозрачность, кисть, текст)."""
    snap = {}
    for w in _walk(root):
        par = w.get_parent()
        snap[w.get_name()] = (w.get_class().get_name(), par.get_name() if par else "-", _slot_sig(w),
                              str(w.get_editor_property("visibility")),
                              _r(w.get_editor_property("render_opacity")), _brush_sig(w), _text_sig(w))
    return snap


def _asset_file(path):
    """Файл .uasset пакета /Game/…"""
    return os.path.join(CONTENT_DIR, path[len("/Game/"):].replace("/", os.sep) + ".uasset")


def _ensure_backup(path, set_name=BACKUP_SET, set_dir=None, verify="size"):
    """Бэкап ассета `/Game/…` ДО записи — общая `asda_headless_tool.backup_file` (★ Т3, 11.10.2026).
    set_name — имя набора (`<дата запуска>_<set_name>` в @@ASTORY_DEV_ROOT_ESC@@\\_backup); set_dir — готовая папка набора из
    переменной окружения шага (переопределение, None — нет); verify — "size"/"md5". Копия уже есть — НЕ перезаписывается
    и повторный запуск НЕ роняет (прежде несовпадение размера после первой записи давало исключение); нет файла
    ассета — исключение. Возвращает путь копии."""
    f = _asset_file(path)
    bak = HT.backup_file(f, set_name, set_dir=set_dir, verify=verify, log=log)
    if bak is None:
        raise RuntimeError("нет файла ассета %s — бэкап невозможен, запись запрещена" % f)
    return bak


def _plane_rect(nodes, idc):
    """Прямоугольник (l, t, w, h) плана 0 контрола — узел импортёра `{idc}_{N}_0` в том же родителе."""
    pref = J._san(idc) + "_"
    for name, w in nodes.items():
        if name.startswith(pref) and name.endswith("_0") and isinstance(w, unreal.Image):
            mid = name[len(pref):-2]
            if mid.isdigit():
                s = _slot_sig(w)
                if s:
                    return w, s[0]
    return None, None


def _set_slot(w, offsets, align, auto_size):
    """Запись слота: смещения (L, T, R, B), alignment, auto_size (анкоры не меняются)."""
    s = _slot(w)
    lay = s.get_editor_property("layout_data")
    lay.set_editor_property("offsets", unreal.Margin(offsets[0], offsets[1], offsets[2], offsets[3]))
    lay.set_editor_property("alignment", unreal.Vector2D(align[0], align[1]))
    s.set_editor_property("layout_data", lay)
    s.set_editor_property("auto_size", auto_size)


_JUSTIFY = {0: "LEFT", 1: "CENTER", 2: "RIGHT"}


def _near(a, b):
    """Покомпонентное равенство чисел с допуском EPS."""
    return len(a) == len(b) and all(abs(float(x) - float(y)) <= EPS for x, y in zip(a, b))


def _handle(case, nodes, dry):
    """Один случай: вернуть ('fixed'|'ok'|'inspect', текст, имя узла). Пишет в виджет только при dry=False."""
    idc = J._san(case["idc"])
    if case["kind"] == "caption":
        return _handle_caption(case, nodes, dry)
    w = nodes.get(idc)
    if w is None:
        return "inspect", "%s: узла %s нет" % (case["idc"], idc), idc
    cw, ch = case["size"]
    offx, offy = case["off"]
    mask = case["mask"]
    cur = _slot_sig(w)
    plane, prect = _plane_rect(nodes, case["idc"])
    if prect is not None:
        # Левый верхний угол контрола — по его плану 0 (узел импортёра `{idc}_{N}_0` того же родителя).
        l, t, pw, ph = prect
        if not _near((pw, ph), (cw, ch)):
            return "inspect", "%s: план %s×%s ≠ .JUI %s×%s" % (idc, pw, ph, cw, ch), idc
        if (w.get_parent().get_name() if w.get_parent() else "") != (plane.get_parent().get_name() if plane.get_parent() else ""):
            return "inspect", "%s: узел и план в разных группах" % idc, idc
    elif case["kind"] == "wrap" and cur is not None and not cur[3]:
        # Плана нет (скин без видимого плана, напр. `NoImage` у 77532): угол выводится из самого узла по формуле
        #   импортёра (tx = l + offx, ty = t + offy + vy); размер узла ниже всё равно сверяется с .JUI.
        vy0 = (ch - LINE_PX) / 2.0 if (mask & 0x4) else ((ch - LINE_PX) if (mask & 0x8) else 0.0)
        l, t = cur[0][0] - offx, cur[0][1] - offy - max(vy0, 0.0)
    elif case["kind"] == "wrap" and cur is not None and cur[3]:
        # Уже авторазмер: угол — из точки привязки нового вида (обратная формула `wrap_block_slot`).
        ax, ay = cur[2]
        l = cur[0][0] - offx - (cw - offx) * ax
        t = (cur[0][1] - ch) if ay >= 1.0 else (2.0 * cur[0][1] - offy - ch) / 2.0
    else:
        return "inspect", "%s: нет плана контрола — угол не определить" % idc, idc
    if case["kind"] == "wrap":
        if not isinstance(w, unreal.TextBlock):
            return "inspect", "%s: не TextBlock" % idc, idc
        tgt = wrap_block_slot(l, t, cw, ch, offx, offy, mask)
        want_off = (tgt["pos"][0], tgt["pos"][1], 0.0, 0.0)
        # Готово? (слот и перенос уже в новом виде)
        if cur[3] and _near(cur[0][:2], want_off[:2]) and _near(cur[2], tgt["align"]) \
                and not w.get_editor_property("auto_wrap_text") \
                and abs(float(w.get_editor_property("wrap_text_at")) - tgt["wrap_at"]) <= EPS:
            return "ok", "%s: уже по эталону" % idc, idc
        # Старый вид импортёра: tx = l+offx, ty = t+offy+vy (vy — однострочный центр/низ), ширина max(w−offx,8), высота
        #   по `J.label_slot`. ★ 07.10.2026 (ревизия label_band МЕЛК-3): общая точка вместо копии формулы; принимаются
        #   оба вида импортёра — прежний max(h, 14) и «по низу контрола» (узел за рамкой окна). Итог от вида не зависит.
        old, old_cut = J.label_slot_forms(l, t, cw, ch, offx, offy, mask)
        if cur[3] or not (_near(cur[0], old) or _near(cur[0], old_cut)) or not _near(cur[2], (0.0, 0.0)) \
                or not w.get_editor_property("auto_wrap_text"):
            return "inspect", "%s: слот %s не совпадает с видом импортёра %s — НЕ трогаю (возможна ручная правка)" \
                % (idc, cur, old), idc
        if not dry:
            w.set_editor_property("auto_wrap_text", False)
            w.set_editor_property("wrap_text_at", float(tgt["wrap_at"]))
            _set_slot(w, want_off, tgt["align"], True)
        return "fixed", "%s: слот %s → точка (%.1f, %.1f), alignment %s, авторазмер, перенос по %.0f px" \
            % (idc, cur[0], want_off[0], want_off[1], tgt["align"], tgt["wrap_at"]), idc
    # kind == "edit"
    if not isinstance(w, unreal.EditableText):
        return "inspect", "%s: не EditableText" % idc, idc
    (el, et, ew, eh), justify = editbox_align(l, t, cw, ch, offx, offy, mask)
    jcur = str(w.get_editor_property("justification")).split(".")[-1].split(":")[0].strip().upper()
    if _near(cur[0], (el, et, ew, eh)) and jcur == _JUSTIFY[justify] and not cur[3]:
        return "ok", "%s: уже по эталону" % idc, idc
    old = (l, t, cw, ch)          # импортёр: add_editable_text(V2(l, t), V2(w, h)), выравнивания нет (LEFT)
    if cur[3] or not _near(cur[0], old) or jcur != "LEFT":
        return "inspect", "%s: слот %s / выравнивание %s не совпадают с видом импортёра — НЕ трогаю" % (idc, cur, jcur), idc
    if not dry:
        w.set_editor_property("justification", getattr(unreal.TextJustify, _JUSTIFY[justify]))
        _set_slot(w, (el, et, ew, eh), (0.0, 0.0), False)
    return "fixed", "%s: слот %s → %s, выравнивание LEFT → %s" % (idc, cur[0], (el, et, ew, eh), _JUSTIFY[justify]), idc


def _handle_caption(case, nodes, dry):
    """(а) [ИСПРАВЛЕНИЕ ДЕФЕКТА ОРИГИНАЛА] Подпись `{idc}_Text` кнопки — правее иконки, кегль CAPTION_FONT_SIZE.
    Вид импортёра: слот (Lкн + CaptionInfo[0], T, Wкн − CaptionInfo[0], H), центр, кегль FONT_PX_IMPORTER, интервал 0 →
    правится; уже исправленный → «готово»; любой иной → «осмотреть» (возможна ручная правка), не трогается."""
    idc = J._san(case["idc"])
    tname = idc + "_Text"
    tw, bw = nodes.get(tname), nodes.get(idc)
    if not isinstance(tw, unreal.TextBlock) or bw is None:
        return "inspect", "%s: нет подписи-TextBlock %s или кнопки" % (idc, tname), tname
    if (tw.get_parent().get_name() if tw.get_parent() else "") != (bw.get_parent().get_name() if bw.get_parent() else ""):
        return "inspect", "%s: подпись и кнопка в разных группах" % tname, tname
    ts, bs = _slot_sig(tw), _slot_sig(bw)
    if ts is None or bs is None or ts[1] != bs[1] or bs[3] or ts[3]:
        return "inspect", "%s: слоты подписи/кнопки не канвасные, с разными анкорами или авторазмер" % tname, tname
    bl, bt, bwid, bh = bs[0]
    cw, ch = case["size"]
    if not _near((bwid, bh), (cw, ch)):
        return "inspect", "%s: кнопка %s×%s ≠ .JUI %s×%s" % (tname, bwid, bh, cw, ch), tname
    capx, capy = case["off"]
    mask = case["mask"]
    # Вид импортёра (caption-ветка `add_control`) — общая точка `J.label_slot` (★ 07.10.2026, ревизия label_band
    #   МЕЛК-3): tx = l + offx, ty = t + offy + vy, ширина max(w − offx, 8); высота в двух допустимых видах — прежний
    #   max(h, 14) и «по низу кнопки» (узел за рамкой окна; кнопка 77251 лежит в под-окне 77200 ниже рамки 77000,
    #   а 77151 из под-окна 77100 — в рамке, её подпись не режется).
    old, old_band = J.label_slot_forms(bl, bt, bwid, bh, capx, capy, mask)
    cut = _near(ts[0], old_band) and not _near(ts[0], old)       # вход «по низу кнопки» — итог тоже по низу
    old_t = old[1]
    left = bl + case["icon_rel"] + CAPTION_GAP_L
    top = old_t + CAPTION_TOP_SHIFT
    # Итог: высота по правилу `label_slot` в виде входа (после перегенерации высота 16,5 только у 77251 —
    #   кнопка 77151 лежит в рамке, у неё остаётся 20);
    #   «готово» — любой из двух видов (прежний итог шага max(h, 14) тоже — уже собранные ассеты не переписываются).
    want_forms = [(left, top, (bl + bwid - CAPTION_PAD_R) - left, J.label_height_like(bt, bh, top, c))
                  for c in (False, True)]
    want = want_forms[1] if cut else want_forms[0]
    font = tw.get_editor_property("font")
    ls = int(font.get_editor_property("letter_spacing"))
    size = float(font.get_editor_property("size"))
    just = str(tw.get_editor_property("justification")).split(".")[-1].split(":")[0].strip().upper()
    if just != "CENTER" or not _near(ts[2], (0.0, 0.0)) or ls != 0:
        return "inspect", "%s: выравнивание %s / alignment %s / интервал %d — не вид импортёра" % (tname, just, ts[2], ls), tname
    if any(_near(ts[0], wf) for wf in want_forms) and abs(size - CAPTION_FONT_SIZE) <= EPS:
        return "ok", "%s: уже исправлено (подпись %.0f…%.0f, кегль %.1f)" % (tname, want[0], want[0] + want[2], size), tname
    if not (_near(ts[0], old) or _near(ts[0], old_band)) or abs(size - FONT_PX_IMPORTER) > EPS:
        return "inspect", "%s: слот %s / кегль %.1f не совпадают с видом импортёра %s / %d — НЕ трогаю"             % (tname, ts[0], size, old, FONT_PX_IMPORTER), tname
    if not dry:
        _set_slot(tw, want, (0.0, 0.0), False)
        font.set_editor_property("size", CAPTION_FONT_SIZE)
        tw.set_editor_property("font", font)
    return "fixed", "%s: [ИСПРАВЛЕНИЕ ДЕФЕКТА ОРИГИНАЛА] слот %s → %s (правее иконки %s), кегль %d → %.1f"         % (tname, ts[0], want, case["icon"], FONT_PX_IMPORTER, CAPTION_FONT_SIZE), tname


def parity():
    """★ СУХОЙ ПРОГОН ИМПОРТЁРА (СРЕДН-2 ревизии 26.09.2026), только чтение: для каждого узла правил (д)/(б) окон
    ремесла считает слот ТЕМ ЖЕ кодом импортёра (`add_control` → `wrap_block_slot` / `editbox_rule` →
    `_apply_anchor` через `_anchor_math`, раскладка групп `build_tree` через `_subtree_bounds`) и сверяет с ТЕКУЩИМ
    ассетом. Совпадение ⇒ перегенерация окна ремесла даст тот же результат, что шаг (в т.ч. 77532, mark_d_1/2).
    Импортёр ничего не собирает и не пишет: модуль лишь импортируется (без побочных эффектов)."""
    import importlib
    G = importlib.import_module("asda_jui_generic_umg")
    G.SKINS = G.parse_skins_full(G.SKIN_INI)
    EAL = unreal.EditorAssetLibrary
    roots = J.parse_jui(_ini())
    total = bad = anchored = 0
    for win_id, asset in WINDOWS.items():
        root = J.find_by_idnum(roots, win_id)
        J.compute_anchors(root)
        if not G._align_scope(root):
            raise RuntimeError("окно %d вне охвата импортёра" % win_id)
        bp = EAL.load_asset(asset)
        nodes = {w.get_name(): w for w in _walk(unreal.AsdaUmgBuilder.get_root_canvas(bp))}
        seq = []

        def walk(n, ox, oy, is_top, is_root):
            """Порядок и начала координат — как `build_tree` импортёра."""
            if is_root:
                seq.append((n, 0.0, 0.0, True))
                for c in n.children:
                    walk(c, 0.0, 0.0, True, False)
            elif n.children:
                gx0, gy0, _gx1, _gy1 = G._subtree_bounds(n)
                seq.append((n, gx0, gy0, False))
                for c in n.children:
                    walk(c, gx0, gy0, False, False)
            else:
                seq.append((n, ox, oy, is_top))
        walk(root, 0.0, 0.0, True, True)
        for n, ox, oy, top in seq:
            if not n.idc:
                continue
            sk = G.SKINS.get(G.skin_key_for(n, quiet=True)) or {}
            stype = sk.get("type", "")
            l, t, w, h = n.abs
            l -= ox
            t -= oy
            if w <= 0 or h <= 0:
                continue
            m = text_mask(n)
            if stype == "Text" and is_wrap_block(stype, h, sk.get("textoutline", 0), m):
                offx, offy = text_off(n, True)
                wb = wrap_block_slot(l, t, w, h, offx, offy, m)
                off, align, auto = (wb["pos"][0], wb["pos"][1], 0.0, 0.0), wb["align"], True
                keep = True
            elif stype == "EditBox":
                tp = getattr(n, "txt_pos", None) or (0, 0)
                er = editbox_rule(l, t, w, h, float(tp[0]), float(tp[1]), m)
                if not er:
                    continue
                off, align, auto, keep = er[0], (0.0, 0.0), False, False
            else:
                continue
            anch = None
            if top and getattr(n, "anchor", None):
                anch, off, align = G._anchor_math(off, n.anchor, align if keep else (0.0, 0.0))
                anchored += 1
            wdg = nodes.get(J._san(n.idc))
            cur = _slot_sig(wdg) if wdg else None
            want_anch = None if anch is None else (_r(anch[0]), _r(anch[1]), _r(anch[2]), _r(anch[3]))
            ok = cur is not None and _near(cur[0], off) and _near(cur[2], align) and cur[3] == auto \
                and (want_anch is None or _near(cur[1], want_anch))
            total += 1
            bad += 0 if ok else 1
            log("паритет %s %s%s: импортёр %s align %s%s | ассет %s → %s"
                % (asset.rsplit("/", 1)[-1], n.idc, " [корень окна]" if anch else "",
                   tuple(_r(v) for v in off), align, (" анкоры %s" % (want_anch,)) if anch else "",
                   cur, "СОВПАДАЕТ" if ok else "РАСХОЖДЕНИЕ"))
    log("ПАРИТЕТ импортёр = ассет: узлов %d (из них у корня окна %d), расхождений %d" % (total, anchored, bad))
    log("ПАРИТЕТ: ГОТОВО")


def dump(path_json):
    """Полный дамп узлов окон WINDOWS (класс, родитель, слот, видимость, кисть, текст) в JSON — сверка до/после."""
    EAL = unreal.EditorAssetLibrary
    out = {}
    for asset in WINDOWS.values():
        bp = EAL.load_asset(asset)
        root = unreal.AsdaUmgBuilder.get_root_canvas(bp) if bp else None
        out[asset] = {k: repr(v) for k, v in sorted(_snapshot(root).items())} if root else "НЕ ЗАГРУЖЕН"
    with open(path_json, "w", encoding="utf-8", newline="\n") as f:
        json.dump(out, f, ensure_ascii=False, indent=1)
    log("дамп узлов: %s (%s)" % (path_json, ", ".join("%s=%d" % (k.rsplit("/", 1)[-1], len(v)) for k, v in out.items())))


def main(standalone=False):
    """Применить правила (д)/(б) к окнам WINDOWS (см. шапку). `standalone` — ручной запуск: бэкап обязателен."""
    if os.environ.get("ASDA_TEXTBLOCK_PARITY"):
        parity()                  # только чтение — ни одного ассета не пишет
        return
    dry = bool(os.environ.get("ASDA_TEXTBLOCK_DRYRUN"))
    dpath = os.environ.get("ASDA_TEXTBLOCK_DUMP")
    if dpath:
        dump(dpath)
    EAL = unreal.EditorAssetLibrary
    B = unreal.AsdaUmgBuilder
    cases = plan()
    by_asset = {}
    for c in cases:
        by_asset.setdefault(c["asset"], []).append(c)
    fixed_total = ok_total = 0
    inspect = []
    for asset, lst in by_asset.items():
        bp = EAL.load_asset(asset)
        root = B.get_root_canvas(bp) if bp else None
        if not root:
            raise RuntimeError("нет корневого канваса у %s" % asset)
        nodes = {w.get_name(): w for w in _walk(root)}
        before = _snapshot(root)
        # Сухая проверка всех случаев окна — ДО любой записи.
        verdicts = [_handle(c, nodes, True) for c in lst]
        for v, msg, _n in verdicts:
            log("%s [%s] %s" % (asset.rsplit("/", 1)[-1], {"fixed": "править", "ok": "готово", "inspect": "осмотреть"}[v], msg))
        todo = [c for c, (v, _m, _n) in zip(lst, verdicts) if v == "fixed"]
        ok_total += sum(1 for v, _m, _n in verdicts if v == "ok")
        inspect += ["%s: %s" % (asset, m) for v, m, _n in verdicts if v == "inspect"]
        if dry or not todo:
            continue
        if standalone:
            # (а) пишет в свой набор бэкапов (`<дата>_craft_caption`), (д)/(б) — в набор `<дата>_craft_assets`.
            _ensure_backup(asset, CAPTION_BACKUP_SET if any(c["kind"] == "caption" for c in todo) else BACKUP_SET)
        changed_names = set()
        for c in todo:
            v, msg, name = _handle(c, nodes, False)
            changed_names.add(name)
        after = _snapshot(B.get_root_canvas(bp))
        lost = [n for n in before if n not in after]
        extra = [n for n in after if n not in before]
        changed = [n for n in before if n in after and before[n] != after[n]]
        foreign = [n for n in changed if n not in changed_names]
        # У изменённых узлов обязаны совпасть класс, родитель, видимость, непрозрачность, кисть и САМ текст/цвет.
        #   Меняться вправе только слот и свойства раскладки текста (justification / перенос); у подписей (а) — ещё кегль.
        caption_names = set(J._san(c["idc"]) + "_Text" for c in todo if c["kind"] == "caption")

        def _keep(sig, name):
            """Неизменяемая часть текстовой подписи: TB — (вид, текст, цвет, кегль (кроме (а)), начертание, интервал);
            ED — всё, кроме justification."""
            if not sig:
                return sig
            if sig[0] == "TB":
                return (sig[0], sig[1], sig[2], None if name in caption_names else sig[6], sig[7], sig[8])
            return (sig[0], sig[1], sig[3], sig[4])
        broken = [n for n in changed_names
                  if before[n][:2] != after[n][:2] or before[n][3:6] != after[n][3:6]
                  or _keep(before[n][6], n) != _keep(after[n][6], n)
                  or (before[n][2] and after[n][2] and (before[n][2][1] != after[n][2][1] or before[n][2][4] != after[n][2][4]))]
        log("%s: узлов до %d, после %d; правлено узлов %d; потеряно узлов: %d; лишних: %d; изменено чужих: %d; "
            "испорчено у правленых: %d" % (asset.rsplit("/", 1)[-1], len(before), len(after), len(changed_names),
                                          len(lost), len(extra), len(foreign), len(broken)))
        if lost or extra or foreign or broken:
            raise RuntimeError("сверка не прошла: потеряно %s, лишних %s, чужих %s, испорчено %s — НЕ сохраняю"
                               % (lost, extra, foreign, broken))
        for n in sorted(changed_names):
            log("   %s: %s → %s" % (n, before[n][2], after[n][2]))
        # ⚠ `CompileAndSave` НЕ СОХРАНЯЕТ (имя врёт) — сохраняет `save_loaded_asset`.
        B.compile_and_save(bp)
        if not EAL.save_loaded_asset(bp, False):
            raise RuntimeError("не удалось сохранить %s" % asset)
        fixed_total += len(changed_names)
    for s in inspect:
        log("осмотреть: %s" % s)
    if dpath and not dry and fixed_total:
        dump(dpath.replace(".json", "_after.json"))
    log("случаев правил %d: исправлено %d, уже по эталону %d, осмотреть %d%s"
        % (len(cases), fixed_total, ok_total, len(inspect), " (СУХОЙ ПРОГОН)" if dry else ""))
    log("ТЕКСТ-БЛОК: ГОТОВО")


# ★★★ ЗАКОН tools-no-import-side-effects: при импорте модуля (раннер пост-импорта, импортёр) — ничего.
if __name__ == "__main__":
    if "--scope" in sys.argv:
        if hasattr(sys.stdout, "reconfigure"):
            sys.stdout.reconfigure(errors="replace")
        print_scope()
    elif "--plan" in sys.argv or unreal is None:
        if hasattr(sys.stdout, "reconfigure"):
            sys.stdout.reconfigure(errors="replace")
        print_plan()
    else:
        try:
            main(standalone=True)
        except Exception:
            unreal.log_error("[текст-блок] ИСКЛЮЧЕНИЕ:\n%s" % traceback.format_exc())
