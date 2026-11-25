# -*- coding: utf-8 -*-
# Copyright (c) 2026 @sitxovski (Discord)
# asda_build_fx_loop_dt.py — DataTable `DT_FxLoop` («как эффект ЗАЦИКЛИВАЕТСЯ в оригинале»), 08.10.2026.
#
# ЗАЧЕМ. Задача владельца «почти все зацикленные эффекты зациклены РЕЗКО». Рантайм
#   (`AAsdaEffectActor`, блок «ЦИКЛ 1:1», ключ `asda.Fx.Loop`) воспроизводит конец периода и контейнер
#   эффекта по эталону, но для этого ему нужны данные, которые сборщики BP/карт в ассеты НЕ кладут:
#     * endMode КАЖДОЙ записи с диска (growQuad/mesh12 пишут в ассет 1 безусловно; у колец loopMode);
#     * шапка `.jeb`: длительность (+0x08) и endMode КОНТЕЙНЕРА (+0x40);
#     * (только карты) полный трек квада/кольца с СТАРТ/КОНЕЦ-сидами там, где печка карт положила трек
#       без них (`ASDA_MAP_KF_SEED` по умолчанию выключен).
#   Разбор и числа — `Reports/owner_1008/fx_loop.md`. Строка таблицы описана в
#   `Source/AsdaReborn/Public/Data/AsdaFxLoopRows.h` (ТРЕБУЕТ сборки C++).
#
# ИСТОЧНИКИ (ничего не выдумывается):
#   * `.jeb` архива `Donors/TRANSLATE_unpacked/models_orig/Data/Effect/Model` через `asda_jeb_parse.parse_jeb` (тот же
#     парсер, что у всех сборщиков);
#   * сид-трек — выход ТОГО ЖЕ `asda_place_effects.build_emitter` с флагами печки карт, один раз с
#     `kf_seed_nodes=True`, один раз без; источник записей — кэш печки `export/maps/<карта>/effects.json`.
#     Трек «без сидов» кладётся рядом ЦЕЛИКОМ (`SeedBaseTrack`, 08.10.2026): рантайм допишет сид-узлы
#     ТОЛЬКО если трек ассета ему равен поузлово по всем полям (ручная правка владельца ⇒ не трогаем;
#     отражение type-2 `ChiralNormal` переносится на дописанные узлы). Отпечаток-сумма — только для лога.
#   * Материалы при этом НЕ создаются и НЕ сохраняются: `get_fx_material`/`ensure_texture` печки на время
#     генерации подменены пустышками (иначе печка пишет MIC/текстуры карты).
#
# ВЫХОД: `/Game/AsdaAssets/Data/DT_FxLoop` (новый генерируемый ассет; прежние ассеты не трогаются) +
#   промежуточный JSON `Tools/_out/FxLoop.json` (вне Content).
# Запуск: python Tools/run_headless_safe.py Tools/asda_build_fx_loop_dt.py --marker "ЦИКЛ-ТАБЛИЦА ГОТОВО"
# ★ ПРАВИЛО tools-no-import-side-effects: при импорте модуль ничего не делает — только через main().
import glob
import json
import os
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
if HERE not in sys.path:
    sys.path.insert(0, HERE)

import asda_paths as AP   # ★ 08.10.2026 (МЕЛК-7 ревизии): путь архива .jeb — из общего модуля путей (без побочных эффектов)

SRC = AP.EFFECT_MODEL_ORIG_DIR   # Donors/TRANSLATE_unpacked/models_orig/Data/Effect/Model — тот же каталог, что прежний хардкод
MAPS_RAW = os.path.join(r"@@ASTORY_DEV_ROOT@@", "Data", "Export", "maps")
OUT_JSON = os.path.join(HERE, "_out", "FxLoop.json")
# ★ 08.10.2026 (МЕЛК-7 ревизии): лог генерации — рядом с отчётом задачи, а не только во временной папке прогона
OUT_LOG = os.path.join(r"@@ASTORY_DEV_ROOT@@", "Reports", "owner_1008", "fx_loop", "gen_fxloop_dt.log")
PKG = "/Game/AsdaAssets/Data"
NAME = "DT_FxLoop"
ROW_STRUCT = "/Script/AsdaReborn.AsdaFxLoopRow"


def row_key(base):
    """Ключ строки: база имени .jeb в нижнем регистре, `-` → `_` (как ищет рантайм)."""
    k = base.lower().replace("-", "_")
    return k[:-4] if k.endswith(".jeb") else k


def disk_end(em):
    """endMode записи с диска: у колец — loopMode, у прочих — endMode; нет поля → −1."""
    p = em.get("params") or {}
    v = p.get("loopMode") if p.get("ring") else p.get("endMode")
    try:
        return int(v)
    except (TypeError, ValueError):
        return -1


def _v(v):
    return {"X": float(v.x), "Y": float(v.y), "Z": float(v.z)}


def node_dict(nd):
    """FAsdaFxNode (unreal-структура) → словарь для JSON DataTable (имена свойств C++)."""
    c = nd.get_editor_property("color")
    return {
        "Time": float(nd.get_editor_property("time")),
        "Rate": float(nd.get_editor_property("rate")),
        "SizeX": float(nd.get_editor_property("size_x")),
        "SizeY": float(nd.get_editor_property("size_y")),
        "Speed": float(nd.get_editor_property("speed")),
        "ConeAngle": float(nd.get_editor_property("cone_angle")),
        "Alpha": float(nd.get_editor_property("alpha")),
        "Color": {"R": float(c.r), "G": float(c.g), "B": float(c.b), "A": float(c.a)},
        "Gravity": float(nd.get_editor_property("gravity")),
        "ConeAxis": _v(nd.get_editor_property("cone_axis")),
        "RingScale": float(nd.get_editor_property("ring_scale")),
        "RingRoll": float(nd.get_editor_property("ring_roll")),
        "RingRot": _v(nd.get_editor_property("ring_rot")),
        "RingH": float(nd.get_editor_property("ring_h")),
        "RingTopR": float(nd.get_editor_property("ring_top_r")),
        "RingBotR": float(nd.get_editor_property("ring_bot_r")),
        "RingU": float(nd.get_editor_property("ring_u")),
        "RingV": float(nd.get_editor_property("ring_v")),
        "RingBasePos": _v(nd.get_editor_property("ring_base_pos")),
        "RingPosOfs": _v(nd.get_editor_property("ring_pos_ofs")),
    }


# Скалярные поля узла (имена JSON = свойства C++ `FAsdaFxNode`) и векторные/цветовые — для поузлового сравнения.
NODE_SCALARS = ("Time", "Rate", "SizeX", "SizeY", "Speed", "ConeAngle", "Alpha", "Gravity", "RingScale", "RingRoll",
                "RingH", "RingTopR", "RingBotR", "RingU", "RingV")
NODE_VECTORS = ("ConeAxis", "RingRot", "RingBasePos", "RingPosOfs")


def node_equal(a, b, tol=1e-3):
    """★ 08.10.2026 (МЕЛК B3 ревизии): узлы равны ПО ВСЕМ ПОЛЯМ (как `AsdaFxLoopNodesEqual` в рантайме)."""
    def near(x, y):
        return abs(x - y) <= tol + 1e-5 * max(abs(x), abs(y))
    if not all(near(a[k], b[k]) for k in NODE_SCALARS):
        return False
    if not all(near(a["Color"][c], b["Color"][c]) for c in "RGBA"):
        return False
    return all(near(a[k][c], b[k][c]) for k in NODE_VECTORS for c in "XYZ")


def track_equal(na, nb):
    """Треки равны поузлово по всем полям (прежнее «равны по отпечатку» пропускало правки вне суммы)."""
    return len(na) == len(nb) and all(node_equal(x, y) for x, y in zip(na, nb))


def is_subsequence(base, full):
    """Узлы «печки без сидов» входят в полный трек подпоследовательностью (сиды только добавляются) —
    то же условие проверяет рантайм перед дописыванием сидов."""
    i = 0
    for n in full:
        if i < len(base) and node_equal(base[i], n):
            i += 1
    return i == len(base)


def track_print(nodes):
    """Отпечаток трека — ТА ЖЕ формула, что `AsdaFxLoopTrackPrint` в AsdaEffectActor.cpp."""
    s = 0.0
    for n in nodes:
        c = n["Color"]; b = n["RingBasePos"]; o = n["RingPosOfs"]
        s += (n["Time"] + n["Alpha"] + c["R"] + c["G"] + c["B"] + n["RingScale"]
              + 0.001 * (n["SizeX"] + n["SizeY"] + n["RingH"] + n["RingTopR"] + n["RingBotR"]
                         + b["X"] + b["Y"] + b["Z"] + o["X"] + o["Y"] + o["Z"]))
    return s


def main():
    import unreal
    import asda_jeb_parse as J
    import asda_place_effects as PE
    from asda_dt_common import fill_dt

    lines = []

    def log(m):
        lines.append(m)
        unreal.log_warning("[fxloop-dt] %s" % m)

    # ── печка карт без побочных эффектов: материалы/текстуры не трогаем ──────────────────────────
    #   ★ 08.10.2026 (МЕЛК-7 ревизии): подмена ВОЗВРАЩАЕТСЯ в finally — в открытом редакторе модуль печки
    #   живёт в sys.modules, и следующая печка карт молча собрала бы эмиттеры без материалов.
    orig_mat, orig_tex = PE.get_fx_material, PE.ensure_texture
    PE.get_fx_material = lambda tex, mode: None
    PE.ensure_texture = lambda tstem: False
    try:
        _generate(unreal, J, PE, fill_dt, log)
    finally:
        PE.get_fx_material, PE.ensure_texture = orig_mat, orig_tex
        try:
            os.makedirs(os.path.dirname(OUT_LOG), exist_ok=True)
            with open(OUT_LOG, "w", encoding="utf-8", newline="\n") as fh:
                fh.write("\n".join(lines) + "\n")
        except OSError as ex:
            unreal.log_warning("[fxloop-dt] лог генерации не записан: %s" % ex)


def _generate(unreal, J, PE, fill_dt, log):
    """Тело генерации (вызывается из main() под try/finally, см. там)."""

    # ── 1. все .jeb архива: контейнер + endMode каждой записи ──────────────────────────────────
    rows = {}
    nfiles = nem = 0
    for f in sorted(glob.glob(os.path.join(SRC, "*.jeb"))):
        base = os.path.splitext(os.path.basename(f))[0]
        try:
            jb = J.parse_jeb(open(f, "rb").read())
        except Exception as ex:
            log("пропуск %s: %s" % (base, ex))
            continue
        if not jb:
            # ★ 08.10.2026 (МЕЛК B6 ревизии): пустой/нечитаемый файл (`eff_mdel_alpen_pado-10.jeb`, 76 б) — в лог,
            #   а не молча. Ни карта, ни BP его не размещают.
            log("пропуск %s: парсер вернул пусто (%d б)" % (base, os.path.getsize(f)))
            continue
        h = jb.get("header") or {}
        occ = {}
        ems = []
        for em in jb.get("emitters", []):
            nm = em.get("name", "")
            k = occ.get(nm, 0); occ[nm] = k + 1
            ems.append({"Name": nm, "Occ": k, "DiskType": int(em.get("type", 0)), "DiskEndMode": disk_end(em),
                        "SeedTrack": [], "SeedBaseCount": 0, "SeedBaseSum": 0.0, "SeedBaseTrack": []})
            nem += 1
        rows[row_key(base)] = {"ContainerDuration": float(h.get("duration", 0.0) or 0.0),
                               "ContainerEndMode": int(h.get("endMode", 3) if h.get("endMode") is not None else 3),
                               "Emitters": ems}
        nfiles += 1
    log("архив: файлов %d, записей %d" % (nfiles, nem))

    # ── 2. сид-треки карт: печка карт с сидами и без, по кэшу размещения ───────────────────────
    nseed = nsame = nmiss = nsub = 0
    for cache in sorted(glob.glob(os.path.join(MAPS_RAW, "*", "effects.json"))):
        try:
            data = json.load(open(cache, encoding="utf-8"))
        except Exception:
            continue
        for jname, jb in (data.get("jeb") or {}).items():
            row = rows.get(row_key(jname))
            if not row:
                nmiss += 1
                continue
            bm = jb.get("blendMode") or ("add" if jb.get("blendDst", 1) == 1 else ("mod" if jb.get("blendDst") == 3 else "alpha"))
            occ = {}
            for em in jb.get("emitters", []):
                nm = em.get("name", "")
                k = occ.get(nm, 0); occ[nm] = k + 1
                p = em.get("params") or {}
                if not p.get("trackNodes"):
                    continue
                t = em.get("type")
                is_ring = (t == 0xE and not em.get("mesh"))
                is_quad = bool(p.get("growQuad"))
                if not (is_ring or is_quad):
                    continue
                tgt = next((r for r in row["Emitters"] if r["Name"] == nm and r["Occ"] == k), None)
                if tgt is None or tgt["SeedTrack"]:
                    continue
                try:
                    a = PE.build_emitter(em, bm, reflect_n=None, authored_rot=None, pure_ring_attached=is_ring,
                                         ring_track_pos=is_ring, kf_seed_nodes=False, etalon_end=False,
                                         holder_restart=True)
                    b = PE.build_emitter(em, bm, reflect_n=None, authored_rot=None, pure_ring_attached=is_ring,
                                         ring_track_pos=is_ring, kf_seed_nodes=True, etalon_end=False,
                                         holder_restart=True)
                except Exception as ex:
                    log("сборка %s/%s: %s" % (jname, nm, ex))
                    continue
                if a is None or b is None:
                    continue
                na = [node_dict(x) for x in a.get_editor_property("nodes")]
                nb = [node_dict(x) for x in b.get_editor_property("nodes")]
                if track_equal(na, nb):
                    nsame += 1
                    continue
                if not is_subsequence(na, nb):
                    # сиды поменяли и сами ключи — рантайм дописать «только недостающее» не сможет: не кладём
                    nsub += 1
                    log("сид-трек %s/%s#%d: узлы без сидов не входят в полный трек подпоследовательностью — пропуск"
                        % (jname, nm, k))
                    continue
                tgt["SeedTrack"] = nb
                tgt["SeedBaseCount"] = len(na)
                tgt["SeedBaseSum"] = track_print(na)
                tgt["SeedBaseTrack"] = na   # ★ 08.10.2026 (СРЕДН-3): весь трек «без сидов» — рантайм сравнивает поузлово
                nseed += 1
    log("сид-треки карт: подготовлено %d, уже совпадают %d, не подпоследовательность %d, эффектов кэша без строки %d"
        % (nseed, nsame, nsub, nmiss))

    # ── 3. JSON → DataTable ──────────────────────────────────────────────────────────────────────
    out = []
    for k in sorted(rows):
        d = dict(rows[k]); d["Name"] = k
        out.append(d)
    os.makedirs(os.path.dirname(OUT_JSON), exist_ok=True)
    text = json.dumps(out, ensure_ascii=False)
    with open(OUT_JSON, "w", encoding="utf-8") as fh:
        fh.write(text)
    log("JSON: %d строк → %s (%d байт)" % (len(out), OUT_JSON, len(text)))
    dt = fill_dt(unreal, PKG, NAME, ROW_STRUCT, text, log, fmt="json",
                 missing_hint="Public/Data/AsdaFxLoopRows.h")
    if dt:
        unreal.log_warning("ASDA_WRITES_ASSET: %s/%s" % (PKG, NAME))
        log("строк в таблице: %d" % len(dt.get_row_names()))
    log("ЦИКЛ-ТАБЛИЦА ГОТОВО")


if __name__ == "__main__":
    main()
