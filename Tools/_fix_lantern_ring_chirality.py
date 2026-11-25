# Copyright (c) 2026 @sitxovski (Discord)
# _fix_lantern_ring_chirality.py — точечный фикс 4 фонарей alpen (2026-07-31, дельта к ring0xE_etalon_re.md).
# ПРИЧИНА разворота конусов: эффективная матрица порта = Reflect(chiral)·N520, а у ВСЕХ фонарных
# attach-узлов det(N520)=+1 (live-дамп) — зеркала в оригинале НЕТ. Актор-ротации УЖЕ = N520 (сверено
# ≤0.002°). Фикс данных: chiral_normal=0; ring_base_pos = negate-z(диск pos-канала (−100,0,0)) = (−100,0,0)
# (печка хранила refl_chiral(диск)=(100,0.2,−0.63)). Повершинная numpy-сверка с оригиналом: ≤0.05 юн.
# Радуги/tower НЕ трогаются.
import sys
try: sys.stdout.reconfigure(encoding="utf-8", errors="replace")
except Exception: pass
import unreal
import os as _fxg_os, sys as _fxg_sys
_fxg_sys.path.insert(0, _fxg_os.path.dirname(_fxg_os.path.abspath(__file__)))
import asda_fx_common as _FXC_GATE   # ★ Ф16: гейт записи эмиттеров (импорт без побочных эффектов)

TARGETS = ("MapEffect_eff_alpenbluelight_a_00_01", "MapEffect_eff_alpengreenlight_a_00_01",
           "MapEffect_eff_alpenredlight_a_00_01", "MapEffect_eff_alpenyellowlight_a_00_01")


def log(m): unreal.log_warning("[lanternFix] %s" % m)


def main():
    les = unreal.get_editor_subsystem(unreal.LevelEditorSubsystem)
    eas = unreal.get_editor_subsystem(unreal.EditorActorSubsystem)
    les.load_level("/Game/Map/alpen/alpen")
    fixed = 0
    for a in list(eas.get_all_level_actors()):
        if not isinstance(a, unreal.AsdaEffectActor): continue
        if a.get_actor_label() not in TARGETS: continue
        cn = a.get_editor_property("chiral_normal")
        a.set_editor_property("chiral_normal", unreal.Vector(0, 0, 0))
        ems = a.get_editor_property("emitters")
        for i in range(len(ems)):
            e = ems[i]
            if not e.get_editor_property("ring"): continue
            nds = e.get_editor_property("nodes")
            for k in range(len(nds)):
                n = nds[k]
                old = n.get_editor_property("ring_base_pos")
                n.set_editor_property("ring_base_pos", unreal.Vector(-100.0, 0.0, 0.0))
                n.set_editor_property("ring_pos_ofs", unreal.Vector(0.0, 0.0, 0.0))
                nds[k] = n
            e.set_editor_property("nodes", nds); ems[i] = e
        # ★★★ Ф16 (К-1, 20.09.2026): запись массива эмиттеров — под явным гейтом
        #   `asda.Fx.Glow.EmittersWriter` (обоснование — asda_fx_common.emitters_writer_gate):
        #   признак «пишет скрипт» не должен зависеть от формы события правки.
        with _FXC_GATE.emitters_writer_gate():
            a.set_editor_property("emitters", ems)
        log("%s: chiral %s -> (0,0,0); rBase %s -> (-100,0,0)" %
            (a.get_actor_label(), (round(cn.x,3), round(cn.y,3), round(cn.z,3)),
             (round(old.x,3), round(old.y,3), round(old.z,3))))
        fixed += 1
    if fixed != 4:
        log("ОШИБКА: исправлено %d из 4 — уровень НЕ сохраняю" % fixed); return
    ok = les.save_current_level()
    log("исправлено 4/4, save_current_level=%s" % ok)


# ★ ЗАКОН [[tools-no-import-side-effects]]: при импорте модуль НИЧЕГО не делает.
#   `__spec__` выставляет ЛЮБАЯ машинерия импорта (import / importlib / runpy) и НЕ
#   выставляет прямое исполнение файла (python file.py, UE -ExecutePythonScript).
#   `python -m` даёт `__name__ == "__main__"` и покрыт первым условием.
if __name__ == "__main__" or globals().get("__spec__") is None:
    main()
