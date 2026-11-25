# Copyright (c) 2026 @sitxovski (Discord)
# _verify_item_use_fx.py — ПРОВЕРКА (не сборщик!) 24 BP эффектов ПРИМЕНЕНИЯ ПРЕДМЕТА.
# Зачем отдельный файл: закон false-negatives-method — факт «BP собран/поле легло» доказывается ВТОРЫМ
#   методом: загрузкой самого ассета редактором и запуском НАСТОЯЩЕГО рантайма, а не наличием файла.
# Что сверяется по каждому из 24:
#   1) имя ассета = ровно то, что построит `AAsdaEffectActor::SpawnByJebName` («BP_Fx_» + Replace('-','_'));
#      строки .jeb взяты ИЗ КОДА порта (AsdaInventorySubsystem.cpp / AsdaEffectsSubsystem.cpp), не с чужих слов;
#   2) класс грузится по тому же пути `/Game/AsdaAssets/Effects/<имя>.<имя>_C`, что и в LoadClass;
#   3) число эмиттеров CDO == числу эмиттеров в .jeb на диске (ни один не потерян молча);
#   4) `effect_name` CDO == база .jeb (с дефисом, как на диске);
#   5) `end_mode` квад/меш-эмиттеров == endMode диска (0 = разовый) — контроль попадания в ONE_SHOT;
#   6) у каждого эмиттера с текстурой материал НЕ дефолтный (иначе «белый квад»);
#   7) ★ 2026-08-14: ДВИЖЕНИЕ ЭМИТТЕРА — `pos_target`/`emit_rot_start`/`emit_rot_end` совпадают с диском,
#      а `emitter_motion` взведён у всех партикл-эмиттеров (иначе марш точки эмиссии и вращение
#      не исполняются — это и был баг «эффект статичен и смещён», `Reports/item_fx_audit.md`).
# Запуск: UnrealEditor-Cmd.exe <проект> -ExecutePythonScript=<этот файл> -unattended -nullrhi …
import os, sys
import os as _os, sys as _sys
_sys.path.insert(0, _os.path.dirname(_os.path.abspath(__file__)))
import asda_paths as _P
try: sys.stdout.reconfigure(encoding="utf-8", errors="replace")
except Exception: pass
import unreal

# База .jeb → откуда строка взята в коде порта (для протокола сверки).
JEBS = [
    ("eff_0_othr_99_hpptn01-a_00", "AsdaInventorySubsystem.cpp linkCode 7/200 (звук 95)"),
    ("eff_0_othr_99_mpptn01-a_00", "AsdaInventorySubsystem.cpp linkCode 8/201 (звук 96)"),
    ("eff_0_othr_99_elptn01-a_00", "AsdaInventorySubsystem.cpp linkCode 5 (без звука)"),
    ("eff_lucky_a_00",             "AsdaEffectsSubsystem.cpp SpawnBoostFx (обычный)"),
    ("eff_lucky_a_01",             "AsdaEffectsSubsystem.cpp SpawnBoostFx (редкий)"),
    ("eff_0_char_99_pmbuff_a_00",  "SpawnPmBuffFx подтип 7"),
    ("eff_0_char_99_pmbuff_a_01",  "SpawnPmBuffFx подтип 6"),
    ("eff_0_char_99_pmbuff_a_02",  "SpawnPmBuffFx подтип 2"),
    ("eff_0_char_99_pmbuff_a_03",  "SpawnPmBuffFx подтип 1"),
    ("eff_0_char_99_pmbuff_a_04",  "SpawnPmBuffFx подтип 3"),
    ("eff_0_char_99_pmbuff_a_05",  "SpawnPmBuffFx подтип 0x10 (и 19)"),
    ("eff_0_char_99_pmbuff_a_06",  "SpawnPmBuffFx подтип 0x11 — «Трофейникус» (код 378)"),
    ("eff_0_othr_99_pmhpre01-a_00", "SpawnPmItemUseFx подтипы 25/42/44/52"),
    ("eff_0_othr_99_pmhpre01-a_01", "то же"),
    ("eff_0_othr_99_pmhpre01-a_02", "то же"),
    ("eff_0_othr_99_pmmpre01-a_00", "SpawnPmItemUseFx подтипы 43/45"),
    ("eff_0_othr_99_pmmpre01-a_01", "то же"),
    ("eff_0_othr_99_pmmpre01-a_02", "то же"),
    ("eff_0_othr_99_pmalre01-a_00", "SpawnPmItemUseFx подтип 47"),
    ("eff_0_othr_99_pmalre01-a_01", "то же"),
    ("eff_0_othr_99_pmalre01-a_02", "то же"),
    ("eff_0_othr_99_pmskre01-a_00", "SpawnPmItemUseFx подтип 36 (сброс навыков)"),
    ("eff_0_char_99_pmarmo01-a_00", "SpawnPmItemUseFx подтип 41 (разрушение брони)"),
    ("eff_1_char_99_expmovg-a_01",  "AsdaInventorySubsystem.cpp itemSubType 32 (Камень души)"),
]
OUT_PKG = "/Game/AsdaAssets/Effects"
PARTICLE_TYPES = (0xD, 3, 0x1B, 0x17, 0x20)


def log(m): unreal.log_warning("[проверка-предмет-фх] %s" % m)


def _ue(v):
    """game (x,y,z) → UE-вектор тем же законом, что и сборщик (asda_coords.to_ue_v)."""
    return (-v[2], v[0], v[1])


def main():
    import asda_jeb_parse as J
    ok_all = 0
    problems = []
    motion_total = motion_moving = 0
    for eff, origin in JEBS:
        # Имя ассета строим ТЕМ ЖЕ преобразованием, что и C++ (дефис → подчёркивание).
        bp_name = "BP_Fx_" + eff.replace("-", "_")
        path = "%s/%s.%s_C" % (OUT_PKG, bp_name, bp_name)
        cls = unreal.load_class(None, path)
        if not cls:
            problems.append("%s: класс НЕ грузится по %s" % (eff, path)); continue
        cdo = unreal.get_default_object(cls)
        ems = cdo.get_editor_property("emitters") or []
        name_prop = cdo.get_editor_property("effect_name")
        # Эталон — сам .jeb на диске.
        src = os.path.join(_P.EFFECT_MODEL_ORIG_DIR, eff + ".jeb")
        jb = J.parse_jeb(open(src, "rb").read())
        disk = jb.get("emitters", [])
        bad = []
        if len(ems) != len(disk):
            bad.append("эмиттеров %d, на диске %d" % (len(ems), len(disk)))
        if str(name_prop) != eff:
            bad.append("effect_name=%r, ожидалось %r" % (str(name_prop), eff))
        # end_mode квад/меш-эмиттеров: на диске у всех 24 endMode=0 (разовый проигрыш).
        for i, em in enumerate(disk):
            p = em.get("params") or {}
            if not (p.get("growQuad") or p.get("mesh12")):
                continue
            want = int(p.get("endMode", 0))
            try: got = int(ems[i].get_editor_property("end_mode"))
            except Exception: continue
            if got != want:
                bad.append("эмиттер %d (тип 0x%X): end_mode=%d, на диске %d" % (i, em.get("type", 0), got, want))
        # Материал: не дефолтный там, где у эмиттера есть текстура.
        for i, em in enumerate(disk):
            tex = (em.get("tex") or "").strip()
            if not tex or os.path.splitext(os.path.basename(tex))[0].lower() == "default":
                continue
            try: mat = ems[i].get_editor_property("material")
            except Exception: continue
            if mat is None:
                bad.append("эмиттер %d: материал не назначен (текстура %s)" % (i, tex))
        # ★ ДВИЖЕНИЕ ЭМИТТЕРА (2026-08-14): марш точки эмиссии + вращение.
        for i, em in enumerate(disk):
            if em.get("type") not in PARTICLE_TYPES:
                continue
            p = em.get("params") or {}
            if "posTarget" not in p:
                continue
            motion_total += 1
            try:
                got_flag = bool(ems[i].get_editor_property("emitter_motion"))
                pt = ems[i].get_editor_property("pos_target")
                rs = ems[i].get_editor_property("emit_rot_start")
                re_ = ems[i].get_editor_property("emit_rot_end")
            except Exception as ex:
                bad.append("эмиттер %d: полей движения нет в классе (%s)" % (i, ex)); continue
            if not got_flag:
                bad.append("эмиттер %d: emitter_motion НЕ взведён (марш не исполнится)" % i); continue
            want_pt = _ue(p["posTarget"])
            if max(abs(pt.x - want_pt[0]), abs(pt.y - want_pt[1]), abs(pt.z - want_pt[2])) > 1e-3:
                bad.append("эмиттер %d: pos_target=%s, ожидалось %s" % (i, pt, want_pt))
            rx = p.get("rotX") or [0, 0, 0]; ry = p.get("rotY") or [0, 0, 0]; rz = p.get("rotZ") or [0, 0, 0]
            if max(abs(rs.x - rx[0]), abs(rs.y - ry[0]), abs(rs.z - rz[0])) > 1e-3:
                bad.append("эмиттер %d: emit_rot_start=%s, ожидалось (%s,%s,%s)" % (i, rs, rx[0], ry[0], rz[0]))
            if max(abs(re_.x - rx[1]), abs(re_.y - ry[1]), abs(re_.z - rz[1])) > 1e-3:
                bad.append("эмиттер %d: emit_rot_end=%s, ожидалось (%s,%s,%s)" % (i, re_, rx[1], ry[1], rz[1]))
            moves = (max(abs(a - b) for a, b in zip(p["posTarget"], p["posBase"])) > 1e-4
                     or max(abs(v) for v in list(rx) + list(ry) + list(rz)) > 1e-4)
            if moves:
                motion_moving += 1
        if bad:
            problems.append("%s [%s]: %s" % (eff, origin, "; ".join(bad)))
        else:
            ok_all += 1
            log("OK %s → %s (эмиттеров %d) ← %s" % (eff, bp_name, len(ems), origin))
    log("ИТОГ: чисто %d/%d; партикл-эмиттеров с данными движения %d, из них реально движущихся %d"
        % (ok_all, len(JEBS), motion_total, motion_moving))
    for p in problems:
        log("ПРОБЛЕМА: %s" % p)

    # ★★★ РАНТАЙМ-ЗОНД: спавним реальные BP и гоняем настоящий Tick (консольная команда
    #   `asda.Fx.MotionProbe`, реализована в AsdaEffectActor.cpp). Печатает точку эмиссии на пяти
    #   отсчётах — «разброс 0.0» у эмиттера с данными движения означал бы, что рантайм их не читает.
    try:
        w = unreal.EditorLevelLibrary.get_editor_world()
    except Exception:
        w = None
    if w:
        for eff in ("eff_0_char_99_pmbuff_a_06", "eff_0_othr_99_hpptn01-a_00",
                    "eff_1_char_99_expmovg-a_01", "eff_0_othr_99_pmhpre01-a_00"):
            log("── рантайм-зонд %s ──" % eff)
            unreal.SystemLibrary.execute_console_command(w, "asda.Fx.MotionProbe %s 2" % eff)
    else:
        log("рантайм-зонд пропущен: редакторского мира нет")


# Закон tools-no-import-side-effects: main() только при ПРЯМОМ запуске (см. шапку asda_build_char_fx.py —
#   `__spec__` выставляет любая машинерия импорта и не выставляет прямое исполнение файла).
def _is_direct_run():
    """True — файл запущен НАПРЯМУЮ (python file.py, UE -ExecutePythonScript), а не импортирован."""
    return __name__ == "__main__" or globals().get("__spec__") is None


if _is_direct_run():
    main()
