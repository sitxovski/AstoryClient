# -*- coding: utf-8 -*-
# Copyright (c) 2026 @sitxovski (Discord)
"""_probe_weapon_fx_seam.py — ЗАМЕР РАЗРЫВА НА СТЫКЕ ЦИКЛА у канала свечения оружия/щита.
Ничего не меняет.

Зачем. Эмиттер в режиме РЕСТАРТ (`endMode/loopMode = 1`) в конце периода МГНОВЕННО возвращает все
каналы к базовым сидам (эталон `fxTrack_updateKeyframe@0x11FD800:93901-93926` — снап, не доезд).
Значит видимость стыка определяется тем, насколько значение НА КОНЦЕ трека отличается от значения
В НАЧАЛЕ трека. С КОНЕЦ-сидом (`animComputeKeyframeDeltas@0x1202BE0`) конец трека = конечный сид
записи, и у большинства эмиттеров канала он равен начальному ⇒ стык нулевой. Без КОНЦА-сида трек
замирает на последнем ФАЙЛОВОМ ключе и стык виден.

Замер идёт по РЕАЛЬНО СОБРАННЫМ узлам BP (то, что увидит рантайм), а не по `.jeb`:
для каждого эмиттера берём узел с максимальным `time` и узел с `time == 0` и считаем
относительный разрыв по альфе и по размеру.
"""
import os
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))

try:
    sys.stdout.reconfigure(encoding="utf-8", errors="replace")
except Exception:
    pass

import unreal
import asda_jeb_parse as J

PKG = "/Game/AsdaAssets/Effects"
# Канал заточки: BP_Fx_<база>_<буква>_<редкость>_ma_<состояние>
import re
NAME_RE = re.compile(r"^BP_Fx_(ohs|spr|ths|bow|xbow|bali|stf|shd)_[a-l]_(co|uc|ra|he|le)_ma_(01|02|03)$")


def log(m):
    unreal.log_warning("[стык-цикла] %s" % m)


def rel(a, b):
    d = max(abs(a), abs(b))
    return 0.0 if d < 1e-4 else abs(a - b) / d


EAL = unreal.EditorAssetLibrary


def main():
    """Зонд. Вынесен в функцию — при импорте модуль НИЧЕГО не делает
    (закон [[tools-no-import-side-effects]], аудит №4 волна 3)."""
    gaps = []
    gaps_before = []      # «как было 14.08»: конец периода = последний ФАЙЛОВЫЙ ключ
    gaps_after_kf = []    # та же выборка, но с КОНЦОМ-узлом — прямое A/B
    n_bp = n_em = n_restart = 0
    worst = []

    for a in EAL.list_assets(PKG, False, False):
        name = a.split(".")[-1]
        if not NAME_RE.match(name):
            continue
        obj = EAL.load_asset(a.split(".")[0])
        if obj is None or not hasattr(obj, "generated_class"):
            continue
        try:
            cdo = unreal.get_default_object(obj.generated_class())
            eff = cdo.get_editor_property("effect_name")
            ems = cdo.get_editor_property("emitters")
        except Exception:
            continue
        if not eff:
            continue
        n_bp += 1
        for e in ems:
            try:
                nodes = e.get_editor_property("nodes")
                end_mode = int(e.get_editor_property("end_mode"))
            except Exception:
                continue
            if len(nodes) < 2:
                continue
            n_em += 1
            # ФРИЗ (3) и СТОП (0) стыка не имеют — их не меряем.
            if end_mode not in (1, 2):
                continue
            n_restart += 1
            srt = sorted(nodes, key=lambda n: n.get_editor_property("time"))
            first, last = srt[0], srt[-1]
            # ★ A/B НА ОДНОЙ И ТОЙ ЖЕ ВЫБОРКЕ, без пересборки: «как было 14.08» = трек БЕЗ КОНЦА-узла,
            #   т.е. значение на конце периода = предпоследний узел (последний файловый ключ).
            #   Считаем только там, где файловые ключи есть (узлов ≥ 3), иначе вариант «без конца»
            #   не определён.
            prev_last = srt[-2] if len(srt) >= 3 else None
            try:
                is_ring = bool(e.get_editor_property("ring"))
            except Exception:
                is_ring = False
            # Геометрический канал: у кольца — радиус верхней кромки и высота, у квада — полу-экстенты.
            props = ("ring_top_r", "ring_h", "ring_scale") if is_ring else ("size_x", "size_y")

            def geom(n):
                g = 0.0
                for prop in props:
                    try:
                        g = max(g, abs(n.get_editor_property(prop)))
                    except Exception:
                        continue
                return g

            def seam(a, b):
                """★ ВИДИМЫЙ разрыв, а не арифметический. Скачок альфы при НУЛЕВОЙ геометрии глазу
                не виден (полотно схлопнуто), и наоборот. Поэтому метрика = «яркость на экране»
                ≈ alpha × геометрия, плюс отдельно сама геометрия (форма видна и при равной альфе)."""
                va = a.get_editor_property("alpha") * geom(a)
                vb = b.get_editor_property("alpha") * geom(b)
                return max(rel(va, vb), rel(geom(a), geom(b)))

            g = seam(first, last)
            gaps.append(g)
            if prev_last is not None:
                gaps_before.append(seam(first, prev_last))
                gaps_after_kf.append(g)
            if g > 0.10:
                worst.append((g, "%s/%s" % (eff, e.get_editor_property("name"))))

    gaps.sort()
    worst.sort(reverse=True)
    if gaps:
        med = gaps[len(gaps) // 2]
        p90 = gaps[int(len(gaps) * 0.9)]
        big = 100.0 * sum(1 for x in gaps if x > 0.10) / len(gaps)
        log("BP канала заточки: %d | эмиттеров с треком: %d | из них РЕСТАРТ: %d" % (n_bp, n_em, n_restart))
        log("РАЗРЫВ НА СТЫКЕ (0 = бесшовно): медиана %.3f | 90 проц %.3f | доля >10%% = %.1f%%" % (med, p90, big))
        log("нулевых стыков: %d из %d" % (sum(1 for x in gaps if x < 1e-4), len(gaps)))
        if gaps_before:
            b = sorted(gaps_before)
            a = sorted(gaps_after_kf)
            log("A/B на ОДНОЙ выборке (эмиттеры С файловыми ключами, РЕСТАРТ, %d шт.):" % len(b))
            log("   БЫЛО (без КОНЦА-узла): медиана %.3f | 90 проц %.3f | доля >10%% = %.1f%% | нулевых %d"
                % (b[len(b) // 2], b[int(len(b) * 0.9)], 100.0 * sum(1 for x in b if x > 0.10) / len(b),
                   sum(1 for x in b if x < 1e-4)))
            log("   СТАЛО (с КОНЦОМ-узлом): медиана %.3f | 90 проц %.3f | доля >10%% = %.1f%% | нулевых %d"
                % (a[len(a) // 2], a[int(len(a) * 0.9)], 100.0 * sum(1 for x in a if x > 0.10) / len(a),
                   sum(1 for x in a if x < 1e-4)))
        for g, nm in worst[:10]:
            log("  остаточный стык %.2f: %s" % (g, nm))
    else:
        log("нечего мерить")


# ★ ЗАКОН [[tools-no-import-side-effects]]: при импорте модуль НИЧЕГО не делает.
#   `__spec__` выставляет ЛЮБАЯ машинерия импорта (import / importlib / runpy) и НЕ
#   выставляет прямое исполнение файла (python file.py, UE -ExecutePythonScript).
if __name__ == "__main__" or globals().get("__spec__") is None:
    main()
