# Copyright (c) 2026 @sitxovski (Discord)
# _diff_ui_state.py — СВЕРКА двух снимков `_snapshot_ui_state.py` («до» и «после» переимпорта).
#
#   ЗАЧЕМ. Полный переимпорт JUI собирает каждое окно с нуля. Доказать, что ничего накопленного не
#   потерялось, можно только МЕХАНИЧЕСКИ: всё, что было, обязано остаться; появиться может только
#   ожидаемое (состояния подписи у кнопок, снятое кольцо фокуса).
#
#   ЗАПУСК (обычный python, редактор не нужен):
#     python Tools\_diff_ui_state.py <до.json> <после.json> [<отчёт.md>]
import json
import sys

# Поля кнопки, различие которых считается ОЖИДАЕМЫМ улучшением переимпорта.
EXPECTED_BTN = {"fgN", "fgH", "fgP", "fgD", "padN", "padP", "focus"}


def load(p):
    with open(p, encoding="utf-8") as f:
        return json.load(f)


def main():
    a = load(sys.argv[1])
    b = load(sys.argv[2])
    out = open(sys.argv[3], "w", encoding="utf-8") if len(sys.argv) > 3 else sys.stdout

    def W(s=""):
        out.write(s + "\n")

    W("# Сверка снимков UI: «до» → «после»")
    W()
    W("| показатель | до | после | дельта |")
    W("|---|---:|---:|---:|")
    for k in sorted(set(a["totals"]) | set(b["totals"])):
        x = a["totals"].get(k, 0)
        y = b["totals"].get(k, 0)
        W("| %s | %d | %d | %+d |" % (k, x, y, y - x))
    W()

    # ── 1. Ассеты ────────────────────────────────────────────────────────────────────────────
    A, Bs = set(a["assets"]), set(b["assets"])
    lost_assets = sorted(A - Bs)
    new_assets = sorted(Bs - A)
    W("## 1. Ассеты")
    W()
    W("* ПРОПАЛО ассетов: **%d**%s" % (len(lost_assets),
                                       ("\n  * " + "\n  * ".join(lost_assets)) if lost_assets else ""))
    W("* появилось ассетов: %d%s" % (len(new_assets),
                                     ("\n  * " + "\n  * ".join(new_assets)) if new_assets else ""))
    W()

    # ── 2. Виджеты ───────────────────────────────────────────────────────────────────────────
    lost_w = []
    new_w = []
    changed_w = []
    for p in sorted(A & Bs):
        ta, tb = a["assets"][p]["tree"], b["assets"][p]["tree"]
        for n in sorted(set(ta) - set(tb)):
            lost_w.append((p, n, ta[n]))
        for n in sorted(set(tb) - set(ta)):
            new_w.append((p, n, tb[n]))
        for n in sorted(set(ta) & set(tb)):
            if ta[n] != tb[n]:
                changed_w.append((p, n, ta[n], tb[n]))
    W("## 2. Виджеты (дерево)")
    W()
    W("* ПРОПАЛО виджетов: **%d**" % len(lost_w))
    for p, n, s in lost_w[:400]:
        W("  * `%s` → `%s` — %s" % (p.rsplit("/", 1)[-1], n, s[:160]))
    if len(lost_w) > 400:
        W("  * … ещё %d" % (len(lost_w) - 400))
    W("* появилось виджетов: %d" % len(new_w))
    for p, n, s in new_w[:200]:
        W("  * `%s` → `%s` — %s" % (p.rsplit("/", 1)[-1], n, s[:160]))
    if len(new_w) > 200:
        W("  * … ещё %d" % (len(new_w) - 200))
    W("* изменилось строк дампа: %d" % len(changed_w))
    W()
    W("### 2.1. Изменения дампа по видам (первые 200)")
    W()
    for p, n, x, y in changed_w[:200]:
        W("* `%s`/`%s`" % (p.rsplit("/", 1)[-1], n))
        W("  * было: `%s`" % x)
        W("  * стало: `%s`" % y)
    if len(changed_w) > 200:
        W("* … ещё %d изменённых строк" % (len(changed_w) - 200))
    W()

    # ── 3. Кнопки ────────────────────────────────────────────────────────────────────────────
    def cap_states(rec):
        """Кнопки, у которых цвета подписи РАЗЛИЧАЮТСЯ по состояниям (реальный эффект)."""
        s = set()
        for n, v in rec["buttons"].items():
            if not str(v.get("fgN", "")).startswith("ЗАДАН"):
                continue
            if v.get("fgN") != v.get("fgH") or v.get("fgN") != v.get("fgP") \
               or v.get("padN") != v.get("padP"):
                s.add(n)
        return s

    lost_b = []
    new_b = []
    ch_b = []
    cap_a = cap_b = 0
    foc_a = foc_b = 0
    for p in sorted(A & Bs):
        ba, bb = a["assets"][p]["buttons"], b["assets"][p]["buttons"]
        cap_a += len(cap_states(a["assets"][p]))
        cap_b += len(cap_states(b["assets"][p]))
        foc_a += sum(1 for v in ba.values() if v.get("focus"))
        foc_b += sum(1 for v in bb.values() if v.get("focus"))
        for n in sorted(set(ba) - set(bb)):
            lost_b.append((p, n))
        for n in sorted(set(bb) - set(ba)):
            new_b.append((p, n))
        for n in sorted(set(ba) & set(bb)):
            d = [k for k in set(ba[n]) | set(bb[n]) if ba[n].get(k) != bb[n].get(k)]
            if d:
                ch_b.append((p, n, sorted(d), ba[n], bb[n]))
    W("## 3. Кнопки")
    W()
    W("* ПРОПАЛО кнопок: **%d**%s" % (len(lost_b),
                                      ("\n  * " + "\n  * ".join("%s/%s" % (p.rsplit('/', 1)[-1], n)
                                                                for p, n in lost_b)) if lost_b else ""))
    W("* появилось кнопок: %d%s" % (len(new_b),
                                    ("\n  * " + "\n  * ".join("%s/%s" % (p.rsplit('/', 1)[-1], n)
                                                              for p, n in new_b[:100])) if new_b else ""))
    W("* кнопок с РАЗЛИЧАЮЩИМИСЯ состояниями подписи: было %d → стало **%d**" % (cap_a, cap_b))
    W("* кнопок, забирающих фокус (кольцо UE): было %d → стало **%d**" % (foc_a, foc_b))
    W("* изменилось кнопок: %d" % len(ch_b))
    unexpected = [(p, n, d) for p, n, d, x, y in ch_b if set(d) - EXPECTED_BTN]
    W("* из них с НЕОЖИДАННЫМИ полями (не состояния/фокус): **%d**" % len(unexpected))
    for p, n, d in unexpected[:150]:
        W("  * `%s`/`%s`: %s" % (p.rsplit("/", 1)[-1], n, ", ".join(sorted(set(d) - EXPECTED_BTN))))
    if len(unexpected) > 150:
        W("  * … ещё %d" % (len(unexpected) - 150))
    W()

    # ── 4. Тексты ────────────────────────────────────────────────────────────────────────────
    lost_t = []
    ch_t = []
    wrap_a = wrap_b = 0
    for p in sorted(A & Bs):
        ta, tb = a["assets"][p]["texts"], b["assets"][p]["texts"]
        wrap_a += sum(1 for v in ta.values() if v.get("wrap"))
        wrap_b += sum(1 for v in tb.values() if v.get("wrap"))
        for n in sorted(set(ta) - set(tb)):
            lost_t.append((p, n, ta[n]))
        for n in sorted(set(ta) & set(tb)):
            d = [k for k in set(ta[n]) | set(tb[n]) if ta[n].get(k) != tb[n].get(k)]
            if d:
                ch_t.append((p, n, sorted(d), ta[n], tb[n]))
    W("## 4. Тексты")
    W()
    W("* ПРОПАЛО текстов: **%d**" % len(lost_t))
    for p, n, v in lost_t[:200]:
        W("  * `%s`/`%s` — %s" % (p.rsplit("/", 1)[-1], n, v))
    W("* с `auto_wrap_text=true` (в Canvas это баг): было %d → стало **%d**" % (wrap_a, wrap_b))
    W("* изменилось текстов: %d" % len(ch_t))
    for p, n, d, x, y in ch_t[:200]:
        W("  * `%s`/`%s`: %s" % (p.rsplit("/", 1)[-1], n, "; ".join(
            "%s %r→%r" % (k, x.get(k), y.get(k)) for k in d)))
    if len(ch_t) > 200:
        W("  * … ещё %d" % (len(ch_t) - 200))
    W()

    # ── 5. Мета (ручки перетаскивания, NoSelect) ─────────────────────────────────────────────
    ma, mb = a["meta"], b["meta"]
    W("## 5. Реестр `DA_JuiWindowMeta`")
    W()
    lost_win = sorted(set(ma) - set(mb))
    new_win = sorted(set(mb) - set(ma))
    W("* ПРОПАЛО окон: **%d** %s" % (len(lost_win), ", ".join(lost_win)))
    W("* появилось окон: %d %s" % (len(new_win), ", ".join(new_win)))
    lost_h = []
    new_h = []
    lost_nh = []
    for w in sorted(set(ma) & set(mb)):
        ha, hb = set(ma[w]["ручки"]), set(mb[w]["ручки"])
        for h in sorted(ha - hb):
            lost_h.append((w, h))
        for h in sorted(hb - ha):
            new_h.append((w, h))
        na, nb = set(ma[w]["noHit"]), set(mb[w]["noHit"])
        for n in sorted(na - nb):
            lost_nh.append((w, n))
    W("* ПРОПАЛО ручек перетаскивания: **%d**" % len(lost_h))
    for w, h in lost_h:
        W("  * `%s`: %s" % (w, h))
    W("* появилось/изменилось ручек: %d" % len(new_h))
    for w, h in new_h:
        W("  * `%s`: %s" % (w, h))
    W("* ПРОПАЛО NoSelect-виджетов: **%d**" % len(lost_nh))
    for w, n in lost_nh[:100]:
        W("  * `%s`: %s" % (w, n))
    W()

    # ── 6. Итог ──────────────────────────────────────────────────────────────────────────────
    bad = (len(lost_assets) + len(lost_w) + len(lost_b) + len(lost_t)
           + len(lost_win) + len(lost_h) + len(lost_nh) + len(unexpected))
    W("## 6. Итог")
    W()
    W("**Суммарно пропаж и неожиданных изменений: %d**" % bad)
    W()
    if out is not sys.stdout:
        out.close()
    print("ПРОПАЖ/НЕОЖИДАННОГО: %d" % bad)
    print("ассеты -%d  виджеты -%d/+%d (изм %d)  кнопки -%d/+%d  тексты -%d  окна_меты -%d  ручки -%d/+%d  noHit -%d"
          % (len(lost_assets), len(lost_w), len(new_w), len(changed_w), len(lost_b), len(new_b),
             len(lost_t), len(lost_win), len(lost_h), len(new_h), len(lost_nh)))


if __name__ == "__main__":
    main()
