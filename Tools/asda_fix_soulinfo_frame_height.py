# Copyright (c) 2026 @sitxovski (Discord)
# asda_fix_soulinfo_frame_height.py — ТОЧЕЧНАЯ правка АССЕТА окна 114 `WBP_SOULINFO_WIN`.
#
# ЗАЧЕМ. Тултип соулмейта, режим 2 («ОДЕРЖИМОСТЬ»): русский текст типа 2 из
#   `DT_soulsoulmatetext` (17 записей, 21 явный перенос + переносы длинных строк = ~26-31
#   отображаемая строка при межстрочном 14 px) НЕ помещается в фиксированный нин-патч окна.
#   Оригинал (`uiStatusPanel_ShowByMode@0xF083F0`) высоту рамки от текста НЕ считает — корейский
#   текст в 405 px влезал, русский длиннее и ВЫЛЕЗАЕТ за нижнюю кромку рамки (баг владельца).
#
# ЧТО ДЕЛАЕМ (Вариант А — фикс-рамка ПРАВИЛЬНОГО размера, по прямому разрешению владельца на
#   правку ИМЕННО этого ассета; закон `no-ui-regeneration` для окна 114 снят владельцем):
#   растим рамку и текстовый бокс TXT02 ТОЛЬКО ВНИЗ, сохраняя левый-верх и ширину.
#   Оригинал: рамка 270×405, бокс TXT02 227×365. Становится: рамка 270×485, бокс 227×445
#   (вмещает 31.7 строки ×14 px ≥ худший случай 31 стр/434 px + запас). Ширину/позицию окна НЕ
#   трогаем (закон `ru-text-wider-than-field` — тот же класс).
#
# КАК РАСТИТ (нин-патч = 9 point-анкор Image на RootCanvas, offsets: R=ширина, B=высота):
#   • средний ряд `SOULINFO_WIN_0_3/4/5` — высота (offset.bottom) 363 → 443 (верх T НЕ меняем);
#   • нижний ряд `SOULINFO_WIN_0_6/7/8` — верх (offset.top)     181.5 → 261.5 (высота 21 та же);
#   • текст `SOULINFO_TXT02` — высота (offset.bottom) 365 → 445 (верх T тот же, перенос уже вкл.);
#   • верхний ряд `_0_0/1/2`, а также TXT00/TXT01 и их *_HIT — НЕ трогаем (режим 1 короткий,
#     сидит у верха; его пустое место снизу — как в оригинале фикс-рамки).
#
# ★★★ ИДЕМПОТЕНТНОСТЬ (обязательна). Значения задаются АБСОЛЮТНО, а не приростом, и перед правкой
#   слот сверяется с ДВУМЯ допустимыми состояниями: ИСХОДНОЕ (импортёр только что сгенерил) или
#   УЖЕ ЦЕЛЕВОЕ (скрипт уже отработал). Любое третье значение = чужая правка → НЕ трогаем и НЕ
#   сохраняем. Поэтому повторный прогон безопасен и НЕ удваивает высоту.
#
# ★★★ ПОСЛЕ ПОЛНОЙ ПЕРЕГЕНЕРАЦИИ UI ЭТОТ ШАГ НАДО ПОВТОРИТЬ. Ассет собирает
#   `asda_jui_generic_umg.py` из `Data/Decoded/jui/login_jui.ini`, где у окна 114 зашито `Size="270 405"`
#   (это ДЕКОМПИЛ ОРИГИНАЛА — править его нельзя, закон `never-invent-only-original`). Значит
#   перегенерация вернёт рамку 405 и баг вместе с ней (грабля «перегенерация стирает»).
#
# Запуск ТОЛЬКО через Tools/run_headless_safe.py (скрытый headless, успех ПО ЛОГУ):
#   python Tools/run_headless_safe.py Tools/asda_fix_soulinfo_frame_height.py \
#          --marker "SOULINFO_RESIZE_DONE" --require "SOULINFO_RESIZE_DONE"
import unreal

B   = unreal.AsdaUmgBuilder
EAL = unreal.EditorAssetLibrary

WBP = "/Game/AsdaAssets/UI/Login/WBP_SOULINFO_WIN"

# Узел → (поле слота, ИСХОДНОЕ значение импортёра, ЦЕЛЕВОЕ значение).
#   "bottom" = высота слота (point-анкор), "top" = верх слота.
PLAN = {
    "SOULINFO_TXT02":   ("bottom", 365.0, 445.0),   # бокс текста режима 2
    "SOULINFO_WIN_0_3": ("bottom", 363.0, 443.0),   # средний ряд нин-патча: левый край
    "SOULINFO_WIN_0_4": ("bottom", 363.0, 443.0),   #                        заливка
    "SOULINFO_WIN_0_5": ("bottom", 363.0, 443.0),   #                        правый край
    "SOULINFO_WIN_0_6": ("top",    181.5, 261.5),   # нижний ряд нин-патча: левый угол
    "SOULINFO_WIN_0_7": ("top",    181.5, 261.5),   #                       нижняя кромка
    "SOULINFO_WIN_0_8": ("top",    181.5, 261.5),   #                       правый угол
}
EPS = 0.6   # допуск сравнения float (как в остальных шагах пост-импорта)


def log(m):
    unreal.log("[soulfix] " + str(m))


def walk(w, out):
    """Собрать имя→виджет по всему дереву (имена в пределах WBP уникальны)."""
    try:
        out[w.get_name()] = w
    except Exception:                                            # noqa: BLE001
        return
    if isinstance(w, unreal.PanelWidget):
        for i in range(w.get_children_count()):
            c = w.get_child_at(i)
            if c:
                walk(c, out)


def canvas_slot(w):
    """Слот виджета, если он лежит в CanvasPanel (иначе None)."""
    try:
        s = w.slot
    except Exception:                                            # noqa: BLE001
        return None
    return s if isinstance(s, unreal.CanvasPanelSlot) else None


def off_str(o):
    return "off(L%.1f T%.1f R%.1f B%.1f)" % (o.left, o.top, o.right, o.bottom)


def main():
    # ★★★ ЗАЩИТА СЦЕНЫ ЛОГИНА (15.09.2026, Reports/dedup_audit/tex_rebind_2026-09-15.md, раздел 13): правит и сохраняет ТОЛЬКО /Game/AsdaAssets/UI/Login/WBP_SOULINFO_WIN —
    #   окно 114, его открывает лишь AsdaLoginGameMode.cpp.
    #   Запуск только с явного разрешения владельца и ASDA_ALLOW_LOGIN_WRITE=1 (иначе стоп, код 3).
    import os as _os_lg, sys as _sys_lg
    _sys_lg.path.insert(0, _os_lg.path.dirname(_os_lg.path.abspath(__file__)))   # Tools — для asda_login_guard
    from asda_login_guard import forbid_login_write
    forbid_login_write("asda_fix_soulinfo_frame_height.py", "UI/Login/WBP_SOULINFO_WIN (окно 114 одержимости на выборе персонажа)")
    bp = EAL.load_asset(WBP)
    if not bp:
        log("!! НЕ загружен %s" % WBP); return
    canvas = B.get_root_canvas(bp)
    if not canvas:
        log("!! нет root canvas"); return

    wid = {}
    walk(canvas, wid)

    log("── ТЕКУЩЕЕ состояние (все SOULINFO_*) ──")
    for nm in sorted(k for k in wid if k.startswith("SOULINFO")):
        s = canvas_slot(wid[nm])
        if s:
            log("%-22s %s" % (nm, off_str(s.get_offsets())))

    todo, already, bad = [], 0, []
    for nm, (field, src, dst) in PLAN.items():
        w = wid.get(nm)
        s = canvas_slot(w) if w else None
        if not s:
            bad.append("%s: узла/слота нет" % nm); continue
        cur = getattr(s.get_offsets(), field)
        if abs(cur - dst) < EPS:
            already += 1                                        # уже применено — повторный прогон
        elif abs(cur - src) < EPS:
            todo.append((nm, s, field, dst))                    # исходное — правим
        else:
            bad.append("%s.%s = %.1f (ни исходное %.1f, ни целевое %.1f)" % (nm, field, cur, src, dst))

    if bad:
        log("!! НЕОЖИДАННОЕ состояние — НИЧЕГО не меняю и НЕ сохраняю:")
        for b in bad:
            log("   " + b)
        return

    if not todo:
        log("правка УЖЕ применена (%d/%d узлов в целевом состоянии) — ассет не трогаю" % (already, len(PLAN)))
        log("SOULINFO_RESIZE_DONE")
        return

    for nm, s, field, dst in todo:
        o = s.get_offsets()
        vals = {"left": o.left, "top": o.top, "right": o.right, "bottom": o.bottom}
        vals[field] = dst
        s.set_offsets(unreal.Margin(vals["left"], vals["top"], vals["right"], vals["bottom"]))

    log("── ПОСЛЕ правки (все SOULINFO_*) ──")
    for nm in sorted(k for k in wid if k.startswith("SOULINFO")):
        s = canvas_slot(wid[nm])
        if s:
            log("%-22s %s" % (nm, off_str(s.get_offsets())))

    B.compile_and_save(bp)
    if not EAL.save_loaded_asset(bp, False):
        log("!! НЕ сохранён %s" % WBP); return
    log("применено правок: %d (уже было: %d) → рамка 270×485, бокс TXT02 227×445" % (len(todo), already))
    log("SOULINFO_RESIZE_DONE")


# ★ Закон tools-no-import-side-effects (гигиена 15.09.2026): при ИМПОРТЕ main() не запускается.
#   Прямое исполнение (python file.py; UE -ExecutePythonScript / py / -run=pythonscript; exec с
#   __name__="__main__") — запускает: там __name__ == "__main__" и __spec__ отсутствует.
if __name__ == "__main__" or globals().get("__spec__") is None:
    main()
