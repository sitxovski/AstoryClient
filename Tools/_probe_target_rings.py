# -*- coding: utf-8 -*-
# Copyright (c) 2026 @sitxovski (Discord)
"""_probe_target_rings.py — сравнить два кольца выбранной цели.

Кольцо под ногами ставится обоим видам целей: дружественным — `friendlyselect`,
враждебным — `unfriendlyselect`. Владелец видит кольцо у NPC, но не видит у монстров и игроков,
хотя код один и тот же. Значит смотреть надо не код, а САМИ ЭФФЕКТЫ: собран ли враждебный
вариант и не пустой ли он.

Печатает по каждому: существует ли Blueprint, сколько в нём эмиттеров, какие текстуры и режим
смешивания — чтобы сразу было видно, отличается ли враждебное кольцо от дружественного.

Запуск: headless через Tools/run_headless_safe.py.
"""
import unreal


def log(m):
    unreal.log("[кольца] " + str(m))


def main():
    EAL = unreal.EditorAssetLibrary
    for jeb in ("friendlyselect", "unfriendlyselect"):
        name = "BP_Fx_" + jeb
        path = "/Game/AsdaAssets/Effects/%s.%s" % (name, name)
        if not EAL.does_asset_exist(path):
            log("%-18s АССЕТА НЕТ" % jeb)
            continue
        bp = EAL.load_asset(path)
        if not bp:
            log("%-18s не загрузился" % jeb)
            continue
        cdo = unreal.get_default_object(bp.generated_class()) if hasattr(bp, "generated_class") else None
        if not cdo:
            log("%-18s BP есть, но класс не читается" % jeb)
            continue
        info = []
        for prop, label in (("effect_name", "имя"), ("emitters", "эмиттеров"),
                            ("end_mode", "режим конца"), ("scale", "масштаб")):
            try:
                v = cdo.get_editor_property(prop)
                info.append("%s=%s" % (label, len(v) if isinstance(v, (list, tuple)) else v))
            except Exception:
                pass
        log("%-18s %s" % (jeb, ", ".join(info) if info else "свойства не читаются"))
        # Состав эмиттеров: тип, текстура, бленд — по ним видно «пустышку».
        try:
            ems = cdo.get_editor_property("emitters")
            for i, em in enumerate(ems[:6]):
                bits = []
                for p, lb in (("kind", "тип"), ("texture", "текстура"), ("blend", "бленд"),
                              ("period", "длительность"), ("start_delay", "задержка")):
                    try:
                        val = em.get_editor_property(p)
                        bits.append("%s=%s" % (lb, val.get_name() if hasattr(val, "get_name") else val))
                    except Exception:
                        pass
                log("    эмиттер %d: %s" % (i, ", ".join(bits)))
        except Exception as e:
            log("    состав эмиттеров не прочитан: %s" % e)
    log("ЗАВЕРШЕНО")


# ★ Закон tools-no-import-side-effects (гигиена 15.09.2026): при ИМПОРТЕ main() не запускается.
#   Прямое исполнение (python file.py; UE -ExecutePythonScript / py / -run=pythonscript; exec с
#   __name__="__main__") — запускает: там __name__ == "__main__" и __spec__ отсутствует.
if __name__ == "__main__" or globals().get("__spec__") is None:
    main()
