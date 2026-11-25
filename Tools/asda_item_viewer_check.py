# Copyright (c) 2026 @sitxovski (Discord)
# asda_item_viewer_check.py — самопроверка ПЕРЕДЕЛАННОГО просмотрщика предметов (плагин AsdaItemBrowser), 29.10.2026.
#   Отчёт: Reports/item_viewer/REPORT.md.
#
# ЧТО ДЕЛАЕТ.
#   Снаружи (python Tools/asda_item_viewer_check.py [--out <папка>]) — запускает редактор БЕЗ ОКНА (-RenderOffscreen,
#   закон no-visible-editor-windows) на ПУСТОЙ карте движка /Engine/Maps/Entry (стартовая Login.umap владельца НЕ
#   грузится — закон owner-login-light-protected), без сети (-AsdaNoNet), без P4 (-SCCProvider=None), ждёт маркер
#   «ЗАВЕРШЕНО: просмотрщик предметов» и добивает процесс (код возврата headless недостоверен — успех по логу).
#   Внутри UE (тот же файл, -ExecCmds="py …") — через UAsdaItemBrowserDebug:
#     • DumpBreakdown — счётчики разбивки (тип/класс/уровень/грейд) и комбинации фильтров → breakdown.txt;
#     • DumpCard      — текст игровой подсказки RU/EN для выборки кодов всех категорий → cards.txt;
#     • RenderViewer  — кадры панели (виджет рисуется в render target, окна нет) → f??_*.png.
#   Ничего не пишет в ассеты и таблицы (просмотрщик только читает).
#
# ★ Без побочных эффектов при импорте (закон tools-no-import-side-effects): всё — в main().
import os
import sys
import time

MARKER = "ЗАВЕРШЕНО: просмотрщик предметов"
DEFAULT_OUT = r"@@ASTORY_DEV_ROOT_FWD@@/Reports/item_viewer/frames"

# Выборка кодов по категориям (коды из таблиц порта; любые новые коды проверяются тем же путём).
SAMPLE_CODES = [
    ("оружие: меч, грейд 3", 65673), ("оружие: лук новичка", 22819), ("оружие премиум: баллиста", 27588),
    ("оружие: удочка", 69630), ("щит грейд 2 (премиум)", 27595), ("шлем обычный", 20556),
    ("шлем премиум (ханбок)", 23546), ("доспех ур. 70", 23331), ("доспех премиум", 23547), ("штаны", 20554),
    ("ботинки", 20555), ("перчатки", 20553), ("кольцо", 20650), ("ожерелье", 20635), ("колчан", 20562),
    ("наживка", 69667), ("зелье", 20572), ("святая вода", 23515), ("эликсир ЖК", 23510), ("свиток улучшения", 20937),
    ("материал: дерево", 20846), ("карточки/камни", 20923), ("яйцо питомца", 23209), ("коробка", 20812),
    ("рыба", 69577), ("бустер (тип 13)", 22642), ("тип 100", 23762), ("набор песка (новый)", 79720),
    ("живой камень", 7001), ("спец-предмет", 500), ("спец-предмет (маунт донора)", 559),
]

# Кадры: (имя, код, поиск, таблица, категория, класс, режим класса, ур.от, ур.до, годится ур., грейд, премиум,
#         группировка, вид подсказки, профессия зрителя, уровень зрителя, глубина раскрытия)
#   категории (EAsdaItemCat): 0 оружие 1 щит 2 шлем 3 доспех … 9 боеприпасы 10 зелья 13 материалы 15 коробки 17 бустеры 19 самоцветы 20 спец
FRAMES = [
    # Обзор дерева по каждой группировке (без выбора): верхние уровни раскрыты.
    ("f00a_tree_by_type",       0, "", 0, -1, -1, 0, 0, 0, 0, -1, -1, 0, 0, 0, 0, 1),
    ("f00b_tree_by_class",      0, "", 0, -1, -1, 0, 0, 0, 0, -1, -1, 1, 0, 0, 0, 1),
    ("f00c_tree_by_level",      0, "", 0, -1, -1, 0, 0, 0, 0, -1, -1, 2, 0, 0, 0, 0),
    ("f00d_tree_by_grade",      0, "", 0, -1, -1, 0, 0, 0, 0, -1, -1, 3, 0, 0, 0, 0),
    ("f01_type_tree_armor70",   23331, "", 0, -1, -1, 0, 0, 0, 0, -1, -1, 0, 0, 1, 70, 1),
    ("f02_weapon_sword_g3",     65673, "", 0, 0, -1, 0, 0, 0, 0, -1, -1, 0, 0, 3, 50, 2),
    ("f03_weapon_prem_ballista",27588, "", 0, 0, -1, 0, 0, 0, 0, -1, 1, 0, 0, 16, 45, 2),
    ("f04_class_archer_reach",  27589, "", 0, 0, 12, 2, 30, 50, 0, -1, -1, 1, 0, 14, 40, 3),
    ("f05_level_grouping",      20650, "", 0, -1, -1, 0, 0, 0, 0, -1, -1, 2, 0, 0, 10, 1),
    ("f06_grade_grouping",      27595, "", 0, -1, -1, 0, 0, 0, 0, -1, -1, 3, 0, 1, 40, 1),
    ("f07_helmet_premium",      23546, "", 0, 2, -1, 0, 0, 0, 0, -1, 1, 0, 0, 1, 5, 2),
    ("f08_potion_simple",       20572, "зелье", 0, -1, -1, 0, 0, 0, 0, -1, -1, 0, 0, 0, 1, -1),
    ("f09_box_contents",        20812, "", 0, 15, -1, 0, 0, 0, 0, -1, -1, 0, 0, 0, 1, 2),
    ("f10_material_wood",       20846, "", 0, 13, -1, 0, 0, 0, 0, -1, -1, 0, 0, 0, 1, 2),
    ("f11_sowel",               7001,  "", 2, -1, -1, 0, 0, 0, 0, -1, -1, 0, 0, 0, 1, 1),
    ("f12_special",             500,   "", 3, -1, -1, 0, 0, 0, 0, -1, -1, 0, 0, 0, 1, 1),
    ("f13_search_code_461",     461,   "461", 0, -1, -1, 0, 0, 0, 0, -1, -1, 0, 0, 0, 1, -1),
    ("f14_search_en_potion",    20571, "potion", 0, -1, -1, 0, 0, 0, 0, -1, -1, 0, 0, 0, 1, -1),
    ("f15_booster_type13",      22642, "", 0, 17, -1, 0, 0, 0, 0, -1, -1, 0, 0, 0, 1, 2),
    ("f16_new_code_sand",       79720, "", 0, -1, -1, 0, 0, 0, 0, -1, -1, 0, 0, 0, 1, 1),
    ("f17_ammo_quiver",         20562, "", 0, 9, -1, 0, 0, 0, 0, -1, -1, 0, 1, 12, 1, 2),
    ("f18_rod",                 69630, "", 0, 0, -1, 0, 0, 0, 0, -1, -1, 0, 0, 0, 1, 2),
    # Высокие кадры — вся карточка целиком (опции itemoptionrange, «лежит в коробках», набор SetCode, описание, ассеты).
    ("f19_full_card_sword",     65673, "", 0, 0, -1, 0, 0, 0, 0, -1, -1, 0, 0, 3, 50, 2, 3200),
    ("f20_full_card_helm_prem", 23546, "", 0, 2, -1, 0, 0, 0, 0, -1, 1, 0, 0, 1, 5, 2, 3200),
    ("f21_full_card_potion",    20572, "", 0, 10, -1, 0, 0, 0, 0, -1, -1, 0, 0, 0, 1, 2, 2400),
]


def _in_ue(out_dir):
    """Тело внутри редактора: дампы + кадры, затем маркер."""
    import unreal
    D = unreal.AsdaItemBrowserDebug
    os.makedirs(out_dir, exist_ok=True)
    t0 = time.time()
    with open(os.path.join(out_dir, "breakdown.txt"), "w", encoding="utf-8") as f:
        f.write(D.dump_breakdown())
    with open(os.path.join(out_dir, "cards.txt"), "w", encoding="utf-8") as f:
        for what, code in SAMPLE_CODES:
            f.write("\n######## %s — %d ########\n" % (what, code))
            f.write(D.dump_card(code, 0, 1, 40))
        # Виды подсказки и профиль зрителя: полный vs простой у зелья; профессия, которой предмет не подходит.
        f.write("\n######## зелье — полный построитель ########\n" + D.dump_card(20572, 1, 0, 1))
        f.write("\n######## лук для Бойца (класс не подходит, уровень 1) ########\n" + D.dump_card(27589, 0, 1, 1))
        f.write("\n######## лук для Лучника ур. 45 ########\n" + D.dump_card(27589, 0, 14, 45))
    for fr in FRAMES:
        name = fr[0]
        png = os.path.join(out_dir, name + ".png").replace("\\", "/")
        r = D.render_viewer(png, fr[1], fr[2], fr[3], fr[4], fr[5], fr[6], fr[7], fr[8], fr[9], fr[10], fr[11],
                            fr[12], fr[13], fr[14], fr[15], fr[16], 1900, fr[17] if len(fr) > 17 else 1200)
        unreal.log(r)
    unreal.log("%s за %.1f с, кадров %d" % (MARKER, time.time() - t0, len(FRAMES)))


def _outside(out_dir):
    """Снаружи: скрытый редактор на пустой карте, ожидание маркера по логу, добивание по PID."""
    here = os.path.dirname(os.path.abspath(__file__))
    sys.path.insert(0, here)
    import asda_pie_harness as PH
    os.makedirs(out_dir, exist_ok=True)
    log = os.path.join(out_dir, "headless_item_viewer.log").replace("\\", "/")
    script = os.path.abspath(__file__).replace("\\", "/")
    env = dict(os.environ)
    env["ASDA_ITEMVIEWER_OUT"] = out_dir
    cmd = [PH.UE_CMD, PH.UPROJECT, "/Engine/Maps/Entry", "-ExecCmds=py %s" % script] + list(PH.QUIET_ARGS) + \
          ["-RenderOffscreen", "-ResX=1920", "-ResY=1200", "-stdout", "-FullStdOutLogOutput", "-abslog=%s" % log, PH.SCC_OFF]
    if os.path.exists(log):
        os.remove(log)
    p = PH.popen_hidden(cmd, env=env)
    t0 = time.time()
    ok = False
    while time.time() - t0 < 1200:
        time.sleep(5)
        txt = ""
        if os.path.exists(log):
            with open(log, encoding="utf-8", errors="replace") as f:
                txt = f.read()
        if MARKER in txt:
            ok = True
            break
        if "Fatal error" in txt or "Assertion failed" in txt or p.poll() is not None:
            break
    time.sleep(2)
    PH.kill_tree(p.pid)
    print("РЕЗУЛЬТАТ: %s; лог %s; кадры и дампы — %s" % ("УСПЕХ" if ok else "ПРОВАЛ", log, out_dir))
    return 0 if ok else 3


def main():
    try:
        import unreal  # noqa: F401  — внутри редактора
        _in_ue(os.environ.get("ASDA_ITEMVIEWER_OUT", DEFAULT_OUT))
        return 0
    except ImportError:
        pass
    out = DEFAULT_OUT
    if "--out" in sys.argv:
        out = sys.argv[sys.argv.index("--out") + 1]
    return _outside(out)


if __name__ == "__main__":
    sys.exit(main())
