# Copyright (c) 2026 @sitxovski (Discord)
# asda_portal_pockets.py — аудит ВСЕХ порталов карт и список «карманов за порталом», которые закрываются на клиенте
#   (04.11.2026, баг владельца «портал Fireway Forest → Flabis: прошёл сквозь кольцо, ничего не случилось, ушёл под карту»).
#
# ЧТО ТАКОЕ КАРМАН. Зона портала (серверный прямоугольник `<Карта>_PortalConfig.Ini`, копия — `portal_edges` реестра карт)
#   может граничить с проходимыми по `.TER` тайлами, до которых НЕЛЬЗЯ дойти иначе как через саму зону (тупик за порталом).
#   В оригинале туда не попадают: сервер на смене тайла внутри зоны шлёт 5055 и уводит на другую карту. Если же переход не
#   сработал (рассинхрон хода с сервером, задержка ответа), персонаж проходит зону насквозь и оказывается в кармане.
#   У Fireway Forest → Flabis карман (42 тайла, x 83..87 × z 40..48) — это ВНУТРЕННОСТЬ домика-портала `mdel_portal01`:
#   высота земли `.TER` там падает до 7051 при полу домика ≈ 8000 → персонаж идёт ПОД полом («ушёл под карту»).
#
# ПРАВИЛО 1 — КАРМАН [ИСПРАВЛЕНИЕ дефекта данных оригинала]: карман закрывается, если ≥ ROOF_SHARE его тайлов накрыты
#   геометрией объектов карты, лежащей выше земли больше чем на ROOF_ABOVE (персонаж там ходит ПОД объектом). Открытая земля за
#   порталом (обычные края карты) не трогается. На 04.11.2026 таких карманов ровно один — Fireway Forest → Flabis.
# ПРАВИЛО 2 — ПАРИТЕТ С СЕРВЕРОМ [ИСПРАВЛЕНИЕ дефекта данных оригинала]: тайл, проходимый по `.TER` клиента, но закрытый битом 0
#   `.sim` GameServer, закрывается и на клиенте. Сервер ведёт ход сам (`MoveSyncProcess@0x5427D0`): на таком тайле его шаг
#   встаёт («don't move tile»), а клиент идёт дальше — позиции расходятся («CharStop Revision Pos» / «CharMove Hack Position»
#   в Log_Game), и проверка зоны портала `sub_52C4C0` (её зовёт ТОЛЬКО шаг сервера) не срабатывает. Обратное (клиент закрыт,
#   сервер открыт — вода плавания и т. п.) не трогается: строже сервера быть можно, мягче нельзя.
# Закрытие — только на клиенте (`DA_WalkGrid.ClosedBits`, ключ `asda.Walk.ClosedTiles`, 0 — как эталон).
#
# ЗАПУСК (обычный python, unreal не нужен):
#   python Tools/asda_portal_pockets.py --audit <отчёт.md>   — таблица по всем зонам (проходимость клиент/сервер, кольцо портала,
#                                                              карманы, крыша, рассинхрон .TER↔.sim у подхода)
#   python Tools/asda_portal_pockets.py --write              — записать `Tools/data/walk_closed/<карта>.json`
#   --sim-dir <папка .SIM сервера> — ЖИВЫЕ `.sim` GameServer (после выкладки плавания 28.10 они отличаются от стока). Снять копию
#     только чтением: scp -i %USERPROFILE%\.ssh\@@ASTORY_SSH_KEY_NAME@@ "Administrator@@@ASTORY_SERVER_VPN_IP@@:@@ASTORY_SERVER_ROOT_FWD@@/Working_ASDA/v2/GameServer/MapData_Info/*.sim" <папка>
#     (и `*.SIM` — у части файлов имя в верхнем регистре). md5 каждого .sim пишется в выходной json.
# Ассет пишет `Tools/asda_export_closedbits.py` (headless). Самотест — `asda.SelfTest.ClosedTiles`.
#
# ★ Модуль БЕЗ побочных эффектов при импорте (закон tools-no-import-side-effects): работа — только в main().
import argparse
import hashlib
import json
import os
import struct
import sys

TOOLS_DIR = os.path.dirname(os.path.abspath(__file__))   # папка Tools
if TOOLS_DIR not in sys.path:
    sys.path.insert(0, TOOLS_DIR)

import asda_paths as _P              # noqa: E402  пути проекта
import asda_ter_height as _TER       # noqa: E402  разбор .TER (проходимость, высоты)
import asda_water_swim as _WS        # noqa: E402  чтение объектов .mapraw и растеризация треугольников

REGISTRY = os.path.join(TOOLS_DIR, "data", "asda_map_registry.json")      # реестр карт (portal_edges = серверные зоны)
OUT_DIR = os.path.join(TOOLS_DIR, "data", "walk_closed")                  # выход: закрытые тайлы по картам
DEFAULT_SIM_DIR = os.path.join(_P.ROOT, "Donors", "GameServer_stock", "MapData_Info")   # стоковые .SIM GameServer (= живые, md5 сверен 04.11)
GRID = 512            # тайлов по стороне у всех карт
CELL = 100.0          # игровых единиц на тайл
POCKET_MAX = 3000     # компонента больше — это не карман, а основная часть карты (обход обрывается)
ROOF_ABOVE = 150.0    # треугольник объекта выше земли больше чем на это — «крыша» над тайлом
ROOF_SHARE = 0.9      # доля накрытых тайлов, с которой карман закрывается
APPROACH = 12         # окрестность зоны (тайлов) для поиска рассинхрона .TER↔.sim у подхода
NEIGH = ((1, 0), (-1, 0), (0, 1), (0, -1))   # 4-соседство, как у шага сервера по тайлам


def load_registry():
    """Реестр карт: (карты по id, список зон порталов)."""
    reg = json.load(open(REGISTRY, encoding="utf-8"))
    return {m["id"]: m for m in reg["maps"]}, reg["portal_edges"]


class MapData(object):
    """Данные одной карты: байт1 флагов .TER, высоты, .SIM сервера, инстансы объектов .mapraw."""

    def __init__(self, folder, sim_path):
        self.folder = folder                                                  # имя папки карты (клиент)
        self.attr = _TER.TerFile(_TER.ter_path(folder)).tile_attr_byte1()     # байт1 флага тайла (блок ∈ BLOCK_VALUES)
        self.height = _TER.get(folder)                                        # высота земли (интерполяция эталона)
        self.sim = open(sim_path, "rb").read() if sim_path and os.path.isfile(sim_path) else None   # .SIM сервера или None
        self.sim_md5 = hashlib.md5(self.sim).hexdigest() if self.sim else None                 # отпечаток источника паритета
        self._objects = None                                                  # ленивый кэш (модели, инстансы)

    def client_blocked(self, x, z):
        """Тайл непроходим у клиента (байт1 .TER)."""
        return self.attr[z * GRID + x] in _TER.BLOCK_VALUES

    def server_blocked(self, x, z):
        """Тайл непроходим у сервера (бит 0 .SIM); None — .SIM нет."""
        if self.sim is None:
            return None
        return (struct.unpack_from("<I", self.sim, 8 + 4 * (z * GRID + x))[0] & 1) != 0

    def ground(self, x, z):
        """Высота земли в центре тайла."""
        return self.height.GroundY(x * CELL + CELL / 2, z * CELL + CELL / 2)

    def objects(self):
        """Инстансы объектов с мировыми вершинами: [(имя, вершины, индексы)]."""
        if self._objects is None:
            models, inst = _WS._read_mapraw_objects(os.path.join(_WS.MAPRAW_DIR, self.folder + ".mapraw"))
            out = []
            for mi, m in inst:
                if mi >= len(models):
                    continue
                nm, pos, idx = models[mi]
                if not pos or not idx:
                    continue
                pts = [(pos[k] * m[0] + pos[k + 1] * m[4] + pos[k + 2] * m[8] + m[12],
                        pos[k] * m[1] + pos[k + 1] * m[5] + pos[k + 2] * m[9] + m[13],
                        pos[k] * m[2] + pos[k + 1] * m[6] + pos[k + 2] * m[10] + m[14]) for k in range(0, len(pos), 3)]
                out.append((nm, pts, idx))
            self._objects = out
        return self._objects


def zone_cells(e):
    """Тайлы зоны: POSX..POSX+RANGEX × POSZ..POSZ+RANGEZ включительно (проверка sub_52C4C0: v9 <= x <= v9+RANGE)."""
    return [(x, z) for x in range(e["x0"], e["x1"] + 1) for z in range(e["z0"], e["z1"] + 1)]


def pockets(md, zone):
    """Компоненты проходимых тайлов, примыкающие к зоне и достижимые ТОЛЬКО через неё (обход без входа в зону)."""
    zs = set(zone)
    seen = set()
    comps = []
    for (x, z) in zone:
        for dx, dz in NEIGH:
            n = (x + dx, z + dz)
            if n in zs or n in seen or not (0 <= n[0] < GRID and 0 <= n[1] < GRID) or md.client_blocked(*n):
                continue
            comp = []
            queue = [n]
            seen.add(n)
            while queue and len(comp) < POCKET_MAX:
                c = queue.pop()
                comp.append(c)
                for ddx, ddz in NEIGH:
                    m2 = (c[0] + ddx, c[1] + ddz)
                    if m2 in zs or m2 in seen or not (0 <= m2[0] < GRID and 0 <= m2[1] < GRID) or md.client_blocked(*m2):
                        continue
                    seen.add(m2)
                    queue.append(m2)
            if len(comp) < POCKET_MAX and not queue:
                comps.append(sorted(comp))
    return comps


def roofed(md, cells):
    """Тайлы из cells, над которыми лежит треугольник объекта выше земли > ROOF_ABOVE: {тайл: имя объекта}."""
    cs = set(cells)
    xs = [c[0] for c in cells]
    zz = [c[1] for c in cells]
    bx0, bx1 = (min(xs) - 1) * CELL, (max(xs) + 2) * CELL
    bz0, bz1 = (min(zz) - 1) * CELL, (max(zz) + 2) * CELL
    hit = {}
    for nm, pts, idx in md.objects():
        if max(p[0] for p in pts) < bx0 or min(p[0] for p in pts) > bx1:
            continue
        if max(p[2] for p in pts) < bz0 or min(p[2] for p in pts) > bz1:
            continue
        tris = [(pts[idx[t]], pts[idx[t + 1]], pts[idx[t + 2]]) for t in range(0, len(idx) - 2, 3)
                if max(idx[t], idx[t + 1], idx[t + 2]) < len(pts)]

        def put(i, u, v, tri, nm=nm):
            tx, tz = i % GRID, i // GRID
            if (tx, tz) in cs and min(tri[0][1], tri[1][1], tri[2][1]) > md.ground(tx, tz) + ROOF_ABOVE:
                hit[(tx, tz)] = nm
        _WS._raster_tris(tris, GRID, GRID, CELL, put)
    return hit


def portal_rings(md):
    """Кольца-эффекты портала (`mdel_portaleff*`) карты: [(имя, x0, x1, z0, z1)] в тайлах."""
    out = []
    for nm, pts, _idx in md.objects():
        if "portaleff" in nm.lower():
            out.append((nm, min(p[0] for p in pts) / CELL, max(p[0] for p in pts) / CELL,
                        min(p[2] for p in pts) / CELL, max(p[2] for p in pts) / CELL))
    return out


def parity_tiles(md):
    """Тайлы карты, проходимые по .TER клиента и закрытые битом 0 .sim сервера (границы 1..510, как у IsTileWalkable)."""
    if md.sim is None:
        return []
    return [(x, z) for z in range(1, GRID - 1) for x in range(1, GRID - 1)
            if not md.client_blocked(x, z) and md.server_blocked(x, z)]


def analyse(sim_dir):
    """Аудит всех зон: список словарей (по зоне) и закрываемые тайлы {папка карты: {тайлы}}."""
    maps, edges = load_registry()
    cache = {}
    rows = []
    closed = {}
    for e in edges:
        m = maps[e["from"]]
        if e["from"] not in cache:
            cache[e["from"]] = MapData(m["folder"], os.path.join(sim_dir, m["server"]["mapinfo_name"] + ".SIM"))
        md = cache[e["from"]]
        zone = zone_cells(e)
        c_open = [c for c in zone if not md.client_blocked(*c)]
        s_open = [c for c in zone if md.server_blocked(*c) is False]
        rings = [r for r in portal_rings(md)
                 if r[2] >= e["x0"] - 1 and r[1] <= e["x1"] + 2 and r[4] >= e["z0"] - 1 and r[3] <= e["z1"] + 2]
        near = [(x, z) for x in range(max(1, e["x0"] - APPROACH), min(GRID - 2, e["x1"] + APPROACH) + 1)
                for z in range(max(1, e["z0"] - APPROACH), min(GRID - 2, e["z1"] + APPROACH) + 1)]
        mismatch = [c for c in near if not md.client_blocked(*c) and md.server_blocked(*c)]
        pk = []
        for comp in pockets(md, zone):
            roof = roofed(md, comp)
            hs = [md.ground(x, z) for x, z in comp]
            close = len(roof) >= ROOF_SHARE * len(comp)
            pk.append(dict(tiles=len(comp), roofed=len(roof), ground=(round(min(hs)), round(max(hs))),
                           objects=sorted(set(roof.values()))[:4], close=close,
                           bbox=(min(c[0] for c in comp), max(c[0] for c in comp), min(c[1] for c in comp), max(c[1] for c in comp))))
            if close:
                closed.setdefault(m["folder"], set()).update(comp)
        dest = maps.get(e["to"], {}).get("folder", "?")
        rows.append(dict(file=e["file"], src=m["folder"], frm=e["from"], to=e["to"], dest=dest,
                         zone="%d..%d × %d..%d" % (e["x0"], e["x1"], e["z0"], e["z1"]), cells=len(zone),
                         client_open=len(c_open), server_open=len(s_open) if md.sim else None,
                         rings=["%s x %.1f..%.1f z %.1f..%.1f" % r for r in rings], pockets=pk, mismatch=len(mismatch),
                         mismatch_cells=mismatch[:6]))
    parity = {}
    sims = {}
    for mid, m in sorted(maps.items(), key=lambda kv: (kv[0] is None, kv[0] or 0)):
        nm = m.get("server", {}).get("mapinfo_name")
        if mid is None or not nm or nm.startswith("^"):
            continue
        try:
            md = cache.get(mid) or MapData(m["folder"], os.path.join(sim_dir, nm + ".SIM"))
        except (OSError, ValueError, TypeError):
            continue                                     # карты без .TER (mine) — проходимости клиента нет, паритет не нужен
        cache[mid] = md
        sims[m["folder"]] = md.sim_md5
        t = parity_tiles(md)
        if t:
            parity[m["folder"]] = t
    return rows, closed, parity, sims


def write_audit(rows, path):
    """Markdown-таблица аудита."""
    # Таблица — на английском (отчёт координатору, решение 04.11.2026).
    lines = ["| # | Map → destination | Zone (tiles) | Walkable client/server | Portal ring at zone | Dead-end pockets behind zone (tiles, roofed, ground) | .TER open / .sim blocked near approach (±%d) | Verdict |" % APPROACH,
             "|---|---|---|---|---|---|---|---|"]
    for i, r in enumerate(rows, 1):
        pk = "; ".join("%d t., roofed %d, ground %d..%d%s%s" % (p["tiles"], p["roofed"], p["ground"][0], p["ground"][1],
                                                                (" (" + ", ".join(p["objects"]) + ")") if p["objects"] else "",
                                                                " **→ CLOSED**" if p["close"] else "") for p in r["pockets"]) or "—"
        ok = r["client_open"] > 0 and (r["server_open"] is None or r["server_open"] > 0)
        verdict = "OK" if ok and not any(p["close"] for p in r["pockets"]) else ("fixed: pocket closed" if ok else "ZONE NOT WALKABLE")
        if ok and r["mismatch"]:
            verdict += "; fixed: approach tiles closed on client (server parity)"
        lines.append("| %d | %s (%d) → %s (%d) | %s | %d/%s of %d | %s | %s | %d%s | %s |" % (
            i, r["src"], r["frm"], r["dest"], r["to"], r["zone"], r["client_open"],
            "—" if r["server_open"] is None else r["server_open"], r["cells"],
            "; ".join(r["rings"]) or "none (portal without ring)", pk, r["mismatch"],
            (" " + str(r["mismatch_cells"])) if r["mismatch"] else "", verdict))
    open(path, "w", encoding="utf-8").write("\n".join(lines) + "\n")


def write_closed(closed, parity, sims):
    """`Tools/data/walk_closed/<карта>.json` (карманы + паритет с сервером); файлы карт без закрытий удаляются."""
    os.makedirs(OUT_DIR, exist_ok=True)
    folders = set(closed) | set(parity)
    for f in os.listdir(OUT_DIR):
        if f.endswith(".json") and f[:-5] not in folders:
            os.remove(os.path.join(OUT_DIR, f))
    for folder in sorted(folders):
        pk = sorted(closed.get(folder, ()))
        pr = sorted(parity.get(folder, ()))
        data = {"map": folder, "generator": "asda_portal_pockets.py (04.11.2026)", "server_sim_md5": sims.get(folder),
                "pocket_reason": "тупик за зоной портала под геометрией объектов",
                "pocket_tiles": [[x, z] for x, z in pk],
                "parity_reason": "проходим по .TER, закрыт битом 0 .sim GameServer",
                "parity_tiles": [[x, z] for x, z in pr]}
        json.dump(data, open(os.path.join(OUT_DIR, folder + ".json"), "w", encoding="utf-8"), ensure_ascii=False, indent=0)
        print("записано: %s — карман %d, паритет %d" % (folder, len(pk), len(pr)))


def main(argv=None):
    """Точка входа: --audit <md> и/или --write."""
    ap = argparse.ArgumentParser(description="Аудит порталов и карманы за порталом")
    ap.add_argument("--audit", help="путь отчёта .md")
    ap.add_argument("--write", action="store_true", help="записать Tools/data/walk_closed/<карта>.json")
    ap.add_argument("--sim-dir", default=DEFAULT_SIM_DIR, help="папка .SIM сервера")
    a = ap.parse_args(argv)
    try:
        sys.stdout.reconfigure(encoding="utf-8", errors="replace")   # консоль cp1251 не берёт «→»
    except Exception:
        pass
    rows, closed, parity, sims = analyse(a.sim_dir)
    for r in rows:
        print("%-18s %2d→%-2d %-18s клиент %2d сервер %-4s кольцо %d карманы %s рассинхрон %d" % (
            r["src"], r["frm"], r["to"], r["zone"], r["client_open"], r["server_open"], len(r["rings"]),
            [(p["tiles"], p["roofed"], p["close"]) for p in r["pockets"]], r["mismatch"]))
    if a.audit:
        write_audit(rows, a.audit)
        print("отчёт: " + a.audit)
    if a.write:
        write_closed(closed, parity, sims)
    return 0


if __name__ == "__main__":
    sys.exit(main())
