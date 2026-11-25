# Copyright (c) 2026 @sitxovski (Discord)
# Проба окна АУКЦИОНА `WBP_EXCHANGE_WIN` (JUI 13000) в СВЕЖЕМ процессе: что реально лежит в ассете.
#   Печатает: гнёзда самоцветов строк 1…8 (класс S3, шаг `asda_ui_missing_interactive`), кнопки
#   наборов категорий `EXCHANGER_RADIOBTN{1,2}_OPT{i}`, три комбобокса фильтров и подписи к ним,
#   три колёсных окна-списка (носители полос прокрутки).
# Запуск: UnrealEditor-Cmd ... -ExecutePythonScript="...\_probe_exchange_widgets.py"
import unreal

ASSET = "/Game/AsdaAssets/UI/Player_UI/Trade/WBP_EXCHANGE_WIN"


def log(m):
    unreal.log("[проба-аукцион] %s" % m)


def all_names(bp):
    out = {}

    def walk(w):
        try:
            kids = w.get_all_children()
        except Exception:                                        # noqa: BLE001
            return
        for c in kids:
            out[c.get_name()] = c.get_class().get_name()
            walk(c)
    walk(unreal.AsdaUmgBuilder.get_root_canvas(bp))
    return out


def main():
    bp = unreal.EditorAssetLibrary.load_asset(ASSET)
    if not bp:
        log("НЕТ ассета %s" % ASSET)
        return
    names = all_names(bp)
    log("узлов в окне: %d" % len(names))

    # ── Гнёзда самоцветов: строка 1 адресуется по `ID`, строки 2…8 — по правилу `{ЯКОРЬ}_ICO{NN}`.
    ok = miss = 0
    for row in range(1, 9):
        got = []
        for k in range(5):
            if row == 1:
                cand = [n for n in names if n.startswith("EXCHANGER_BB1_%d_" % (k + 1))]
                nm = cand[0] if cand else None
            else:
                nm = "EXCHANGER_BB%d_0_ICO%02d" % (row, k)
                nm = nm if nm in names else None
            if nm:
                got.append(nm)
                ok += 1
            else:
                miss += 1
        log("строка %d: гнёзд %d/5  %s" % (row, len(got), ", ".join(got)))
    log("ИТОГО гнёзд: есть %d, нет %d" % (ok, miss))

    # ── Безымянные планы гнёзд должны быть погашены (иначе двойной слой).
    lit = [n for n in names if n.startswith("NoImage_icon_")]
    log("планов `NoImage_icon_*` в окне: %d (гасятся шагом; счёт нужен для сверки)" % len(lit))

    log("класс узла EXCHANGER_BB2_0_ICO00 = %s" % names.get("EXCHANGER_BB2_0_ICO00", "НЕТ"))
    for pref in ("EXCHANGER_RADIOBTN1_OPT", "EXCHANGER_RADIOBTN2_OPT", "EXCHMY_CateSelBtn"):
        got = sorted(n for n in names if n.startswith(pref))
        log("%s: %d — %s" % (pref, len(got), ", ".join(got)))

    for n in ("EXCHANGER_SEARCH1", "EXCHANGER_SEARCH2", "EXCHANGER_SEARCH3",
              "EXCHANGER_TXT1", "EXCHANGER_TXT2", "EXCHANGER_TXT3",
              "EXCHMY_CateSelBtn"):
        log("%-22s %s" % (n, names.get(n, "НЕТ")))

    for pref in ("EXCHANGE_SCROLL", "EXCHMY_SCROLL", "EXCHANGER_SCROLL"):
        got = sorted(k for k in names if k.startswith(pref))
        log("%-18s → %s" % (pref, ", ".join("%s(%s)" % (g, names[g]) for g in got) or "НЕТ"))


# ★ ЗАКОН tools-no-import-side-effects.
if __name__ == "__main__":
    main()
