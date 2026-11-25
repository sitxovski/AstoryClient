# Copyright (c) 2026 @sitxovski (Discord)
# Правка ассета /Game/AsdaAssets/UI/Admin/Admin_login (окно входа админ-режима).
#   ЧТО ЧИНИМ: группа кнопки подтверждения LOGIN_BUTTOn лежала в АБСОЛЮТНЫХ координатах — анкор (0,0),
#   Origin 588x316, — тогда как всё остальное окно (рамка 2nd_ID_Warnning_Win_0_0..8, заголовок,
#   поле ввода) сидит на анкоре (0.5, 0.45). Две анкор-системы = кнопка уезжает вправо, и её смещение
#   зависит от разрешения экрана. Плюс вся группа была Collapsed (кнопки просто не было видно).
#   ЧТО ДЕЛАЕМ: приводим группу к анкор-системе рамки, ставим её по центру рамки ниже поля ввода,
#   показываем группу/базу/боковинки/кнопку, подпись — SelfHitTestInvisible (закон: текст поверх
#   кнопки не ловит мышь, иначе нет hover/pressed/click). Иконку «минус» оставляем свёрнутой.
#   Раскладка считается ИЗ САМОГО АССЕТА (те же формулы, что были в рантайме) — скрипт идемпотентен.
import unreal

ASSET = "/Game/AsdaAssets/UI/Admin/Admin_login.Admin_login"
DESIGN_W, DESIGN_H = 1024.0, 768.0   # дизайн-база JUI (X от 1024, Y от 768)

def L(m): unreal.log("[FIXADM] " + str(m))


def W(name): return unreal.find_object(wt, name)

def rect(name):
    """(L, T, W, H, anchors, alignment) в дизайн-базе — формула CenterModalContent."""
    w = W(name)
    if not w: return None
    sl = w.slot
    ld = sl.get_editor_property("layout_data")
    off = ld.get_editor_property("offsets"); a = ld.get_editor_property("anchors"); al = ld.get_editor_property("alignment")
    ww, hh = off.right, off.bottom
    if ww <= 0 or hh <= 0: return None
    return (a.minimum.x * DESIGN_W + off.left - al.x * ww,
            a.minimum.y * DESIGN_H + off.top  - al.y * hh, ww, hh, a, al)




# ── видимость: группа и оформление показаны, кнопка кликабельна, подпись сквозная ──
VIS = unreal.SlateVisibility


def main():
    """Работа скрипта. Вынесена в функцию, чтобы импорт модуля НИЧЕГО не делал
    (закон [[tools-no-import-side-effects]])."""
    global ld, name, w, wt
    bp = unreal.load_object(None, ASSET)
    wt = unreal.find_object(bp, "WidgetTree")
    assert bp and wt, "не найден ассет/WidgetTree"
    # ── рамка окна: общий bbox 9 кусков + её анкор ──
    fr_l, fr_t, fr_r, fr_b, fr_a = 1e9, 1e9, -1e9, -1e9, None
    for i in range(9):
        r = rect("2nd_ID_Warnning_Win_0_%d" % i)
        if not r: continue
        fr_l = min(fr_l, r[0]); fr_t = min(fr_t, r[1])
        fr_r = max(fr_r, r[0] + r[2]); fr_b = max(fr_b, r[1] + r[3])
        if fr_a is None: fr_a = r[4]
    grp = rect("LOGIN_BUTTOn")
    inp = rect("Admin_password_input")
    assert fr_a and grp, "нет рамки окна или группы LOGIN_BUTTOn"
    input_bottom = (inp[1] + inp[3]) if inp else fr_t
    L("рамка [%.0f..%.0f]x[%.0f..%.0f] анкор(%.2f,%.2f); группа было (%.0f,%.0f) %.0fx%.0f анкор(%.2f,%.2f); низ поля %.1f"
      % (fr_l, fr_r, fr_t, fr_b, fr_a.minimum.x, fr_a.minimum.y, grp[0], grp[1], grp[2], grp[3],
         grp[4].minimum.x, grp[4].minimum.y, input_bottom))
    # ── целевая позиция: по центру рамки, ниже поля ввода, внутри нижней кромки ──
    new_l = (fr_l + fr_r) * 0.5 - grp[2] * 0.5
    new_t = max(grp[1], input_bottom + 4.0)
    new_t = min(new_t, fr_b - 6.0 - grp[3])
    slot = W("LOGIN_BUTTOn").slot
    ld = slot.get_editor_property("layout_data")
    ld.set_editor_property("anchors", unreal.Anchors(fr_a.minimum, fr_a.maximum))
    ld.set_editor_property("offsets", unreal.Margin(
        new_l - fr_a.minimum.x * DESIGN_W + grp[5].x * grp[2],
        new_t - fr_a.minimum.y * DESIGN_H + grp[5].y * grp[3],
        grp[2], grp[3]))
    slot.set_editor_property("layout_data", ld)
    L("группа стало (%.0f,%.0f) анкор(%.2f,%.2f) offsets(%.1f,%.1f,%.0f,%.0f)"
      % (new_l, new_t, fr_a.minimum.x, fr_a.minimum.y,
         ld.get_editor_property("offsets").left, ld.get_editor_property("offsets").top, grp[2], grp[3]))
    for name, v in (("LOGIN_BUTTOn",                  VIS.SELF_HIT_TEST_INVISIBLE),
                    ("BOOSTER_PUR_OKBTN_Base_317_0",  VIS.SELF_HIT_TEST_INVISIBLE),
                    ("BOOSTER_PUR_OKBTN_LBase_318_0", VIS.SELF_HIT_TEST_INVISIBLE),
                    ("BOOSTER_PUR_OKBTN_RBase_319_0", VIS.SELF_HIT_TEST_INVISIBLE),
                    ("BOOSTER_PUR_OKBTN",             VIS.VISIBLE),
                    ("BOOSTER_PUR_OKBTN_Text",        VIS.SELF_HIT_TEST_INVISIBLE)):
        w = W(name)
        if not w: L("НЕТ виджета %s" % name); continue
        w.set_editor_property("visibility", v)
    # Поле пароля: скрытый ввод прямо в ассете (в рантайме дублируется как защита от случайной правки).
    ed = W("Password-Input-box")
    if ed: ed.set_editor_property("is_password", True)
    unreal.AsdaUmgBuilder.compile_and_save(bp)
    unreal.EditorAssetLibrary.save_loaded_asset(bp, False)
    L("ассет сохранён")


# ★ ЗАКОН [[tools-no-import-side-effects]]: при импорте модуль НИЧЕГО не делает.
#   `__spec__` выставляет ЛЮБАЯ машинерия импорта (import / importlib / runpy) и НЕ
#   выставляет прямое исполнение файла (python file.py, UE -ExecutePythonScript).
#   `python -m` даёт `__name__ == "__main__"` и покрыт первым условием.
if __name__ == "__main__" or globals().get("__spec__") is None:
    main()
