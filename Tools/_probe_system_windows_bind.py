# Copyright (c) 2026 @sitxovski (Discord)
# Проверка ПРИВЯЗКИ подсистемы UAsdaSystemOptionSubsystem к РЕАЛЬНЫМ именам виджетов ассетов
#   WBP_SYSTEM_MenuWIN_NEW (69400) и WBP_SYSTEM_Option_NEW (69500).
#   Скрипт не правит ничего: он только сверяет, что каждое имя, которое ищет код, в дереве есть
#   (закон [[no-invented-ui]]: имена берём из ассета, а не выдумываем).
import unreal

MENU = "/Game/AsdaAssets/UI/Player_UI/System/WBP_SYSTEM_MenuWIN_NEW"
OPT = "/Game/AsdaAssets/UI/Player_UI/System/WBP_SYSTEM_Option_NEW"


def collect(path):
    bp = unreal.EditorAssetLibrary.load_asset(path)
    if not bp:
        return None
    root = unreal.AsdaUmgBuilder.get_root_canvas(bp)
    names = {}

    def walk(w):
        names[w.get_name()] = w.get_class().get_name()
        if isinstance(w, unreal.PanelWidget):
            for i in range(w.get_children_count()):
                walk(w.get_child_at(i))

    walk(root)
    return names


def check(title, names, wanted):
    miss = [n for n in wanted if n not in names]
    print("--- %s: искали %d, НЕ НАЙДЕНО %d" % (title, len(wanted), len(miss)))
    for n in miss:
        print("    НЕТ: %s" % n)
    return len(miss)


def main():
    total_missing = 0

    menu = collect(MENU)
    if menu is None:
        print("МЕНЮ НЕ ЗАГРУЗИЛОСЬ")
        print("ЗАВЕРШЕНО")
        return
    total_missing += check("ESC-меню 69400", menu, [
        "SYSTEM_MENU_btn1", "SYSTEM_MENU_btn2", "SYSTEM_MENU_btn3",
        "SYSTEM_MENU_btn4", "SYSTEM_MENU_Xbtn",
    ])

    opt = collect(OPT)
    if opt is None:
        print("НАСТРОЙКИ НЕ ЗАГРУЗИЛИСЬ")
        print("ЗАВЕРШЕНО")
        return

    wanted = [
        "SYSTEM_Option_NEW_Radio_btn_OPT0", "SYSTEM_Option_NEW_Radio_btn_OPT1",
        "SYSTEM_Option_NEW_Radio_btn_OPT2", "SYSTEM_Option_NEW_Radio_btn_Text",
        "SYSTEM_Option_NEW_Radio_btn_Text2", "SYSTEM_Option_NEW_Radio_btn_Text3",
        "SYSTEM_Op_INT_grp", "SYSTEM_Op_SYSTEM_grp", "SYSTEM_Op_HOTKEYS_grp",
        "SYSTEM_Op_INT_1_grp", "SYSTEM_Op_INT_2_grp", "SYSTEM_Op_INT_3_grp",
        "SYSTEM_Op_SYSTEM_souund_grp", "SYSTEM_Op_SYSTEM_screen_grp", "SYSTEM_Op_SYSTEM_3d_OP_grp",
        "SYSTEM_Op_HOTKEYS_INTER_grp", "SYSTEM_Op_HOTKEYS_PARTY_grp",
        "SYSTEM_Op_HOTKEYS_CHAT_grp", "SYSTEM_Op_HOTKEYS_FEEL_grp",
        "SYSTEM_Op_INT_DD_grp", "SYSTEM_Op_SYSTEM_DD_grp", "SYSTEM_Op_HOTKEYS_DD_grp",
        "SYSTEM_Op_INT_DD_SEL00_OPT0", "SYSTEM_Op_SYSTEM_DD_SEL00_OPT0", "SYSTEM_Op_HOTKEYS_DD_SEL00_OPT0",
        "SYSTEM_Option_APP", "SYSTEM_Option_OK", "SYSTEM_Option_NO", "SYSTEM_Option_NEW_XX",
        "SYSTEM_default", "SYSTEM_cancel",
        "SYSTEM_Option_APP_Text", "SYSTEM_Option_OK_Text", "SYSTEM_Option_NO_Text",
        "SYSTEM_default_Text", "SYSTEM_cancel_Text",
        "SYSTEM_Op_SYSTEM_SYS22_CMB00", "SYSTEM_O3_SYSTEM_SYS33_CMB00",
        "SYSTEM_Op_SYSTEM_SYS22_TXT01", "SYSTEM_Op_SYSTEM_SYS22_TXT02",
        "SYSTEM_O3_SYSTEM_SYS33_TXT09",
    ]
    for i in range(3):
        wanted.append("SYSTEM_Op_INT_DD_TXT%02d" % i)
        wanted.append("SYSTEM_Op_SYSTEM_DD_TXT%02d" % i)
    for i in range(4):
        wanted.append("SYSTEM_Op_HOTKEYS_DD_TXT%02d" % i)
    for i in range(7):
        wanted.append("SYSTEM_Op_INT_1_CHK%02d" % i)
        wanted.append("SYSTEM_Op_INT_1_TXT%02d" % i)
    for i in range(6):
        wanted.append("SYSTEM_Op_INT_2_CHK%02d" % i)
        wanted.append("SYSTEM_Op_INT_2_TXT%02d" % i)
    for i in range(10):
        wanted.append("SYSTEM_Op_INT_3_CHK%02d" % i)
        wanted.append("SYSTEM_Op_INT_3_TXT%02d" % i)
    for i in range(2):
        wanted.append("SYSTEM_Op_SYSTEM_SYS_11_CHK%02d" % i)
        wanted.append("SYSTEM_Op_SYSTEM_SYS_11_TXT%02d" % i)
    wanted.append("SYSTEM_Op_SYSTEM_SYS22_CHK00")
    wanted.append("SYSTEM_Op_SYSTEM_SYS22_TXT00")
    for i in range(9):
        wanted.append("SYSTEM_O3_SYSTEM_SYS33_CHK%02d" % i)
        wanted.append("SYSTEM_O3_SYSTEM_SYS33_TXT%02d" % i)
    wanted.append("PICK_BACK_grp")
    for i in range(1, 84):
        wanted.append("PICK_BACK_grp_%d" % i)
    total_missing += check("Окно настроек 69500", opt, wanted)

    # Оверлей SYSTEM_CLICK и планы полос: проверяем, что нашлись по префиксу.
    clicks = [n for n in opt if n.startswith("SYSTEM_CLICK")]
    sidebars = [n for n in opt if n.startswith("SideBar_Scr")]
    combos = [n for n in opt if n.startswith("Bk_ComboBox")]
    talkbar = [n for n in opt if n.startswith("TalkBar_system_D") and n.endswith("_txt")]
    blue = [n for n in opt if n.startswith("Box_BlueOt20")]
    print("--- по префиксам: SYSTEM_CLICK %d (ждём 8), SideBar_Scr %d (ждём 12 = 3 полосы x 4 плана),"
          " Bk_ComboBox %d, TalkBar_*_txt %d (ждём 3), Box_BlueOt20 %d (ждём 3)"
          % (len(clicks), len(sidebars), len(combos), len(talkbar), len(blue)))
    if len(clicks) != 8 or len(sidebars) != 12 or len(talkbar) != 3 or len(blue) != 3:
        total_missing += 1

    print("ИТОГО НЕНАЙДЕННЫХ ГРУПП: %d" % total_missing)
    print("ЗАВЕРШЕНО")


# ★ ЗАКОН [[tools-no-import-side-effects]]: при импорте модуль НИЧЕГО не делает.
if __name__ == "__main__" or globals().get("__spec__") is None:
    main()
