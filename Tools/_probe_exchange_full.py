# -*- coding: utf-8 -*-
# Copyright (c) 2026 @sitxovski (Discord)
# ★ ЖИВОЙ ДАМП окна АУКЦИОНА `WBP_EXCHANGE_WIN` (JUI 13000) — ПОЛНЫЙ, для пересмотра окна.
#   Печатает: все узлы (имя, класс, видимость, абсолютный слот), разбивку по ТРЁМ панелям
#   по данным `.JUI` (иерархия в ассете расплющена!), кто из узлов НЕ попал ни в одну панель,
#   состояние подписей вкладок (цвет/текст/видимость) и перекрытия кнопок.
# Запуск: UnrealEditor-Cmd ... -ExecutePythonScript="...\_probe_exchange_full.py"
import os
import sys
import unreal

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import asda_jui_parse as P

ASSET = "/Game/AsdaAssets/UI/Player_UI/Trade/WBP_EXCHANGE_WIN"
JUI = r"@@ASTORY_DEV_ROOT@@\Data\Decoded\jui\gamemain_jui.ini"


def log(m):
    unreal.log("[аукцион-дамп] %s" % m)


def walk(root):
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


def jui_panels():
    """ID-состав трёх панелей ПО ДАННЫМ (13005 / 13200 / 13400) + общая рама."""
    roots = P.parse_jui(JUI)
    win = P.find_by_idnum(roots, 13000)
    P.calc_positions(win)

    def sub(n, acc):
        acc.append(n)
        for c in n.children:
            sub(c, acc)
        return acc

    out = {}
    allnodes = sub(win, [])
    for pid in (13005, 13200, 13400):
        p = P.find_by_idnum([win], pid)
        out[pid] = sub(p, [])
    used = set()
    for pid in out:
        for n in out[pid]:
            used.add(id(n))
    out["frame"] = [n for n in allnodes if id(n) not in used]
    return out


def slot_of(w):
    try:
        s = w.get_editor_property("slot")
        if isinstance(s, unreal.CanvasPanelSlot):
            o = s.get_editor_property("layout_data").get_editor_property("offsets")
            return (round(o.left, 1), round(o.top, 1), round(o.right, 1), round(o.bottom, 1))
    except Exception:                                        # noqa: BLE001
        pass
    return None


def main():
    bp = unreal.EditorAssetLibrary.load_asset(ASSET)
    if not bp:
        log("НЕТ ассета %s" % ASSET)
        return
    root = unreal.AsdaUmgBuilder.get_root_canvas(bp)
    ws = walk(root)
    log("узлов в ассете: %d" % len(ws))

    panels = jui_panels()
    for pid in (13005, 13200, 13400):
        log("данные .JUI: панель %d — узлов %d" % (pid, len(panels[pid])))
    log("данные .JUI: общая рама — узлов %d: %s" % (
        len(panels["frame"]), ", ".join(sorted(n.idc or ("<" + n.name + ">") for n in panels["frame"]))))

    # ── Множества базовых idc каждой панели ──
    sets = {}
    for k in (13005, 13200, 13400, "frame"):
        sets[k] = set(n.idc for n in panels[k] if n.idc)

    def base_of(nm):
        """Базовый idc узла ассета: срезаем хвосты плана/подписи/кнопки/иконки."""
        s = nm
        for tail in ("_grp",):
            if s.endswith(tail):
                s = s[:-len(tail)]
        parts = s.split("_")
        # план `<idc>_<juiIdx>_<plane>`
        if len(parts) >= 3 and parts[-1].isdigit() and parts[-2].isdigit():
            return "_".join(parts[:-2])
        for pref in ("OPT", "ICO", "Text"):
            if parts[-1].startswith(pref):
                cand = "_".join(parts[:-1])
                if cand:
                    return cand
        return s

    counts = {13005: 0, 13200: 0, 13400: 0, "frame": 0}
    orphan = []
    for w in ws:
        nm = w.get_name()
        b = base_of(nm)
        hit = None
        for k in (13005, 13200, 13400, "frame"):
            if b in sets[k] or nm in sets[k]:
                hit = k
                break
        if hit is None:
            orphan.append((nm, w.get_class().get_name()))
        else:
            counts[hit] += 1
    log("узлы ассета по панелям: 13005=%d  13200=%d  13400=%d  рама=%d  БЕЗ ПАНЕЛИ=%d"
        % (counts[13005], counts[13200], counts[13400], counts["frame"], len(orphan)))
    for nm, cl in sorted(orphan)[:80]:
        log("   без панели: %-44s %s" % (nm, cl))

    # ── Вкладки: кнопки, подписи, цвет ──
    byname = {}
    for w in ws:
        byname.setdefault(w.get_name(), w)
    for idc, n in (("EXCHANGER_RADIOBTN1", 8), ("EXCHANGER_RADIOBTN2", 7),
                   ("EXCHANGER_RADIOBTN3", 9), ("EXCHMY_CateSelBtn", 2)):
        log("--- переключатель %s ---" % idc)
        for i in range(n):
            b = byname.get("%s_OPT%d" % (idc, i))
            t = byname.get("%s_Text" % idc if i == 0 else "%s_Text%d" % (idc, i + 1))
            bs = slot_of(b) if b else None
            ts = slot_of(t) if t else None
            col = txt = vis = "-"
            if t:
                try:
                    c = t.get_editor_property("color_and_opacity").get_editor_property("specified_color")
                    col = "%.3f %.3f %.3f" % (c.r, c.g, c.b)
                except Exception:                            # noqa: BLE001
                    pass
                txt = str(t.get_editor_property("text"))
                vis = str(t.get_editor_property("visibility"))
            log("  OPT%d btn=%s%s  txt=%s%s цвет=%s текст=%r vis=%s"
                % (i, "да" if b else "НЕТ", bs or "", "да" if t else "НЕТ", ts or "", col, txt, vis))

    # ── Видимость ключевых кнопок и их слоты ──
    for nm in ("EXCH_OK", "EXCH_CANCEL", "EXCH_SELDEL", "EXCHMY_DEL", "EXCHMY_CLOSE",
               "EXCHANGER_BUY", "EXCHANGER_CANCEL", "EXCHANGER_RESCAN",
               "EXCHANGER_PREBTN1", "EXCHANGER_NEXTBTN1", "EXCHANGER_STATBTN1", "EXCHANGER_ENDBTN1",
               "EXCHANGE_TITLE_DEL", "EXCH_REGISTRATION", "EXCH_MYITEM", "EXCHANGER_WIN"):
        w = byname.get(nm)
        if not w:
            log("КНОПКА/УЗЕЛ %-22s НЕТ В АССЕТЕ" % nm)
            continue
        log("%-22s %-22s vis=%-24s slot=%s" % (nm, w.get_class().get_name(),
            str(w.get_editor_property("visibility")), slot_of(w)))

    # ── Всё, что лежит ПОВЕРХ кнопки (тот же канвас, пересечение слотов, больший zorder) ──
    def zof(w):
        try:
            return w.get_editor_property("slot").get_editor_property("z_order")
        except Exception:                                    # noqa: BLE001
            return 0
    for nm in ("EXCH_OK", "EXCHANGER_BUY", "EXCHANGER_RESCAN", "EXCHANGER_RADIOBTN1_OPT0"):
        a = byname.get(nm)
        sa = slot_of(a)
        if not a or not sa:
            continue
        za = zof(a)
        over = []
        for w in ws:
            if w is a:
                continue
            sb = slot_of(w)
            if not sb:
                continue
            if sa[0] < sb[0] + sb[2] and sb[0] < sa[0] + sa[2] and sa[1] < sb[1] + sb[3] and sb[1] < sa[1] + sa[3]:
                v = str(w.get_editor_property("visibility"))
                if zof(w) >= za and "HIT_TEST_INVISIBLE" not in v and "COLLAPSED" not in v and "HIDDEN" not in v:
                    over.append("%s(%s,z=%d,%s)" % (w.get_name(), w.get_class().get_name(), zof(w), v))
        log("ПОВЕРХ %-26s z=%d: %s" % (nm, za, ", ".join(over) if over else "ничего"))


if __name__ == "__main__":
    main()
