# -*- coding: utf-8 -*-
# Copyright (c) 2026 @sitxovski (Discord)
"""asda_swim_walls_report.py — ведомость «стены маски плавания» по всем картам (08.11.2026, пачка 18, баг 9).

Для каждой карты: сколько тайлов маски `SwimBits` (список, открытый на сервере 28.10) отрезано правилом `wall_tiles`
(`asda_water_swim.py`): кромка города (берег безопасной зоны) и обрыв (шаг > kMaxStep), и связные зоны стены с
прямоугольником тайлов, числом тайлов, высотой земли/воды. Проверка точек владельца (Альпен 106,384 и 54,403).
Выход: Markdown в stdout или в файл `--out`; превью PNG `--png <папка>` (серый — суша .TER, голубой — маска, пурпур — стена).
Модуль без побочных эффектов при импорте.
"""
import os
import sys
import collections

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import asda_maps as _MAPS          # noqa: E402
import asda_ter_height as _TER     # noqa: E402
import asda_water_swim as _S       # noqa: E402

OWNER_POINTS = {"alpen": [(106, 384), (54, 403)]}


def zones(tiles, W):
    """Связные (8-соседство) зоны множества тайлов: список множеств, крупные первыми."""
    left = set(tiles); out = []
    while left:
        s = left.pop(); comp = {s}; q = collections.deque([s])
        while q:
            i = q.popleft(); x, z = i % W, i // W
            for dz in (-1, 0, 1):
                for dx in (-1, 0, 1):
                    j = (z + dz) * W + x + dx
                    if j in left:
                        left.discard(j); comp.add(j); q.append(j)
        out.append(comp)
    return sorted(out, key=len, reverse=True)


def render(res, path):
    from PIL import Image
    W, H = res["W"], res["H"]
    im = Image.new("RGB", (W, H))
    px = im.load()
    for i in range(W * H):
        x, z = i % W, i // W
        c = (140, 140, 140) if res["walk"][i] else (70, 30, 30)
        if i in res["open"]:
            c = (0, 150, 210)
        if i in res["wall"]:
            c = (255, 0, 255)
        px[x, H - 1 - z] = c
    im.save(path)


def main(argv):
    import argparse
    ap = argparse.ArgumentParser(description="ведомость стен маски плавания")
    ap.add_argument("--out", default="")
    ap.add_argument("--png", default="")
    a = ap.parse_args(argv)
    L = ["# Стены маски плавания по картам (пачка 18, баг 9, 08.11.2026)", "",
         "Правило `asda_water_swim.py::wall_tiles`: тайл маски `SwimBits` остаётся проходимым, только если до него дойти от суши `.TER` "
         "4-соседством по маске, не входя в «берег» безопасной зоны (бит 0x80) и с шагом не круче %.0f. Остальное — `SwimWallBits` "
         "(ключ `asda.Swim.WallGuard`)." % _S.kMaxStep, "",
         "| Карта | Маска (сервер) | Стена всего | кромка города | обрыв | Остаётся открыто |", "|---|---:|---:|---:|---:|---:|"]
    detail = []
    for mp in _MAPS.ALL_MAPS:
        res = _S.compute(mp)
        if res is None:
            continue
        if mp in _S.EXCLUDE:
            L.append("| %s | — (исключена: %s) | 0 | 0 | 0 | 0 |" % (mp, _S.EXCLUDE[mp])); continue
        if not res["open"]:
            continue
        W = res["W"]
        L.append("| %s | %d | %d | %d | %d | %d |" % (mp, len(res["open"]), len(res["wall"]), res["wall_peace_shore"],
                                                    res["wall_step"], len(res["open"]) - len(res["wall"])))
        if a.png:
            os.makedirs(a.png, exist_ok=True)
            render(res, os.path.join(a.png, "walls_%s.png" % mp.replace(" ", "_")))
        if not res["wall"]:
            continue
        corners = _TER.TerFile(_TER.ter_path(mp)).corner_heights()
        detail += ["", "## %s — зоны стены (%d тайлов)" % (mp, len(res["wall"])), "",
                   "| # | тайлов | x | z (y на мини-карте) | земля .TER мин..макс | вода |", "|---:|---:|---|---|---|---|"]
        for n, comp in enumerate(zones(res["wall"], W), 1):
            xs = [i % W for i in comp]; zs = [i // W for i in comp]
            gs = [sum(corners[4 * i:4 * i + 4]) / 4 for i in comp]
            ws = sorted({round(res["surf"][i][0]) for i in comp})
            detail.append("| %d | %d | %d..%d | %d..%d | %.0f..%.0f | %s |" % (n, len(comp), min(xs), max(xs), min(zs), max(zs),
                                                                         min(gs), max(gs), ", ".join(map(str, ws[:4]))))
        for (x, z) in OWNER_POINTS.get(mp, []):
            i = z * W + x
            detail.append("")
            detail.append("Точка владельца x%d y%d: байт1 .TER = %d (блок), в маске = %s, в стене = %s" %
                          (x, z, _TER.TerFile(_TER.ter_path(mp)).tile_attr_byte1()[i], i in res["open"], i in res["wall"]))
    text = "\n".join(L + detail) + "\n"
    if a.out:
        open(a.out, "w", encoding="utf-8").write(text)
        print("записано", a.out)
    else:
        sys.stdout.write(text)
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
