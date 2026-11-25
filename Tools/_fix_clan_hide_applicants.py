# Copyright (c) 2026 @sitxovski (Discord)
# _fix_clan_hide_applicants.py — ТОЧЕЧНАЯ правка WBP_COMMUNITY_WIN (окно 6400, вкладка «Клан», панель «Предводитель» 35000).
#
# [ИСПРАВЛЕНИЕ] по решению владельца 01.11.2026 (дословно: «Нет, просто скрой эту кнопку вообще и сдвинь остальные выше»).
#   Пункт 0 полосы страниц 35001 — «Кандидаты» (loc 17101). У оригинала эта страница не открывается НИКОГДА: `uiSelectPanelTab`
#   case 0 безусловно даёт нотис 17107 «Временно недоступно», а системы заявок в клиенте-эталоне нет вовсе (разбор сессии clan-window).
#   Убираем пункт и поднимаем пять оставшихся на его место тем же шагом 44 px (дамп `_dump_clan_submenu_wbp.py`):
#     фон пункта `CmtyClan_SubMenuBtn_521_<i>` · кнопка `CmtyClan_SubMenuBtn_OPT<i>` · подпись `CmtyClan_SubMenuTxt0<i>` и её
#     хит-зона `CmtyClan_SubMenuTxt0<i>_<n>_HIT`. Пункт 0 — свёрнут (Collapsed). Имена и индексы НЕ меняются: OPT<i> по-прежнему
#     открывает страницу i (код `UAsdaCommunityClanSubsystem::BindTabControls`), меняется только место на экране.
#   Высота группы `CmtyClan_SubMenuBtn_grp` 263 → 219 (пять пунктов). Оригинальный JUI (`gamemain_jui.ini`) не трогается.
#   Логика (ключ `asda.Clan.HideApplicantsPage`) держит пункт 0 свёрнутым и при показе подрежима по префиксам.
# Побочных эффектов при импорте нет (закон tools-no-import-side-effects): всё в main().
import unreal

ASSET = "/Game/AsdaAssets/UI/Player_UI/Social/WBP_COMMUNITY_WIN"
STEP = 44.0
GROUP = "CmtyClan_SubMenuBtn_grp"


def collect(w, out):
    out[w.get_name()] = w
    try:
        cnt = w.get_children_count()
    except Exception:
        cnt = 0
    for i in range(cnt):
        c = w.get_child_at(i)
        if c:
            collect(c, out)


def item_names(all_names, i):
    """Узлы пункта i полосы: фон, кнопка, подпись, хит-зона подписи."""
    res = ["CmtyClan_SubMenuBtn_521_%d" % i, "CmtyClan_SubMenuBtn_OPT%d" % i, "CmtyClan_SubMenuTxt%02d" % i]
    pref = "CmtyClan_SubMenuTxt%02d_" % i
    res += [n for n in all_names if n.startswith(pref) and n.endswith("_HIT")]
    return res


def main():
    bp = unreal.load_asset(ASSET)
    root = unreal.AsdaUmgBuilder.get_root_canvas(bp) if bp else None
    if not root:
        unreal.log_warning("RESULT НЕТ ассета/корня")
        unreal.log("ЗАВЕРШЕНО")
        return
    nodes = {}
    collect(root, nodes)
    names = list(nodes.keys())
    # Защита от повторного прогона: пункт 1 уже на месте пункта 0 — ничего не делаем.
    opt1 = nodes.get("CmtyClan_SubMenuBtn_OPT1")
    if opt1 and isinstance(opt1.slot, unreal.CanvasPanelSlot) and abs(opt1.slot.get_position().y) < 0.5:
        unreal.log("RESULT уже применено (OPT1 на y=0) — пропуск")
        unreal.log("ЗАВЕРШЕНО")
        return
    changed = 0
    for n in item_names(names, 0):
        w = nodes.get(n)
        if not w:
            unreal.log_warning("RESULT пропуск (нет узла): %s" % n)
            continue
        w.set_visibility(unreal.SlateVisibility.COLLAPSED)
        changed += 1
        unreal.log("RESULT свёрнут %s" % n)
    for i in range(1, 6):
        for n in item_names(names, i):
            w = nodes.get(n)
            s = w.slot if w else None
            if not s or not isinstance(s, unreal.CanvasPanelSlot):
                unreal.log_warning("RESULT пропуск (нет узла/слота): %s" % n)
                continue
            p = s.get_position()
            s.set_position(unreal.Vector2D(p.x, p.y - STEP))
            changed += 1
            unreal.log("RESULT сдвинут %-34s y %.1f → %.1f" % (n, p.y, p.y - STEP))
    g = nodes.get(GROUP)
    if g and isinstance(g.slot, unreal.CanvasPanelSlot):
        sz = g.slot.get_size()
        g.slot.set_size(unreal.Vector2D(sz.x, sz.y - STEP))
        unreal.log("RESULT группа %s высота %.1f → %.1f" % (GROUP, sz.y, sz.y - STEP))
    ok = unreal.EditorAssetLibrary.save_loaded_asset(bp, only_if_is_dirty=False)
    unreal.log("RESULT сохранение=%s изменено_узлов=%d" % (ok, changed))
    unreal.log("ЗАВЕРШЕНО")


if __name__ == "__main__" or globals().get("__spec__") is None:
    main()
