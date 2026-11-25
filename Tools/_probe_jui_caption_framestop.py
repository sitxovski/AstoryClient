# Copyright (c) 2026 @sitxovski (Discord)
# Проба-доказательство двух правок импортёра JUI от 31.08.2026:
#   долг №1 — узел подписи `<idc>_Text` создаётся и при ПУСТОМ `CaptionText`
#             (контрол с настроенным слотом подписи: `CaptionInfo` с ненулевой маской/смещением);
#   долг №2 — признак `EFrameStop` из `gamemain_jui.ini` доезжает до виджета
#             `UAsdaAnimatedPlane` (`bLoop = False` вместо прежней жёсткой константы `True`).
#
# Что делает: перегенерирует ДВА окна (ASDA_ONLY_WIN — точечно, не массово) и печатает по ним
#   срез узлов. Список окон берётся из переменной окружения ASDA_PROBE_WINS
#   (по умолчанию `MINISTATE_WIN,COMMUNITY_WIN`).
#
# ⚠ Побочных эффектов при импорте нет (закон [[tools-no-import-side-effects]]).
import unreal, importlib.util, os, sys

HERE = os.path.dirname(os.path.abspath(__file__))
WINS = os.environ.get("ASDA_PROBE_WINS", "MINISTATE_WIN,COMMUNITY_WIN")
# Пути ассетов окон, по которым снимаем срез (имена — как в Content/AsdaAssets/UI).
PATHS = {
    "MINISTATE_WIN": "/Game/AsdaAssets/UI/Player_UI/HUD/WBP_MINISTATE_WIN",
    "COMMUNITY_WIN": "/Game/AsdaAssets/UI/Player_UI/Social/WBP_COMMUNITY_WIN",
}


def _regen():
    """Точечная перегенерация окон списка WINS штатным импортёром."""
    os.environ["ASDA_ONLY_WIN"] = WINS
    spec = importlib.util.spec_from_file_location(
        "asda_jui_generic_umg", os.path.join(HERE, "asda_jui_generic_umg.py"))
    mod = importlib.util.module_from_spec(spec)
    sys.modules["asda_jui_generic_umg"] = mod
    spec.loader.exec_module(mod)          # импорт НИЧЕГО не делает — работу зовём явно
    mod._script_main()


def _probe(win):
    path = PATHS.get(win)
    bp = unreal.EditorAssetLibrary.load_asset(path) if path else None
    print("=== СРЕЗ %s (%s): %s" % (win, path, "OK" if bp else "НЕ ЗАГРУЗИЛСЯ"))
    if not bp:
        return
    root = unreal.AsdaUmgBuilder.get_root_canvas(bp)
    texts = []      # узлы-подписи с ПУСТЫМ текстом (долг №1)
    planes = []     # анимпланы (долг №2)
    total = [0]

    def walk(w):
        total[0] += 1
        nm = w.get_name()
        if isinstance(w, unreal.TextBlock):
            try:
                s = str(w.get_editor_property("text"))
            except Exception:
                s = "?"
            if nm.endswith("_Text") and not s.strip():
                texts.append(nm)
        if isinstance(w, unreal.AsdaAnimatedPlane):
            # ⚠ UE-Python срезает у булевых UPROPERTY ведущее «b»: C++ `bLoop` → python `loop`.
            #   Имя `b_loop` НЕ существует — оставлено запасным вариантом на случай иной сборки.
            val = None
            for pn in ("loop", "b_loop"):
                try:
                    val = bool(w.get_editor_property(pn)); break
                except Exception:
                    continue
            try:
                planes.append((nm, val if val is not None else "ОШИБКА чтения",
                               int(w.get_editor_property("total_frame")),
                               float(w.get_editor_property("frame_interval_ms"))))
            except Exception as e:
                planes.append((nm, "ОШИБКА чтения: %s" % e, 0, 0.0))
        if isinstance(w, unreal.PanelWidget):
            for i in range(w.get_children_count()):
                walk(w.get_child_at(i))

    walk(root)
    print("  всего виджетов: %d" % total[0])
    print("  ПУСТЫХ узлов `*_Text` (долг №1): %d" % len(texts))
    for n in sorted(texts):
        print("     %s" % n)
    oneshot = [p for p in planes if p[1] is False]        # bLoop=False = один прогон и стоп
    print("  анимпланов всего: %d, из них ОДНОРАЗОВЫХ (bLoop=False, долг №2): %d"
          % (len(planes), len(oneshot)))
    for p in planes:
        print("     %s  bLoop=%s  TotalFrame=%s  FrameInterval=%s мс" % p)


def main():
    print("=== ПРОБА ДОЛГОВ ИМПОРТЁРА: окна %s ===" % WINS)
    # ASDA_PROBE_ONLY=1 — только снять срез с УЖЕ собранных ассетов, без перегенерации.
    if os.environ.get("ASDA_PROBE_ONLY", "") != "1":
        _regen()
    else:
        print("(ASDA_PROBE_ONLY=1 — перегенерация пропущена, читаем готовые ассеты)")
    for w in [x.strip() for x in WINS.split(",") if x.strip()]:
        _probe(w)
    print("ЗАВЕРШЕНО")


# ★ ЗАКОН [[tools-no-import-side-effects]]: при импорте модуль НИЧЕГО не делает.
if __name__ == "__main__" or globals().get("__spec__") is None:
    main()
