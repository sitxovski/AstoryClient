# -*- coding: utf-8 -*-
# Copyright (c) 2026 @sitxovski (Discord)
# set0_shot_mic_rename.py — кадр «путём игрока» после переименования 62 корейских MIC (06.09.2026):
#   герой на alpen (FakeSelfSpawn) в мужском купальном наборе 310 (chst/grov/hcap/pant/shoe 310 —
#   5 переименованных MIC на одном персонаже) + оружие; коды предметов ищутся ПО ТАБЛИЦЕ
#   (UAsdaItemTableSubsystem.GetItemMeshName), а не выдумываются. Части кладутся в контейнер 3
#   командой asda.Inven.Put (толкает модель через ОБЩУЮ точку ApplySelfEquipPart).
#   Съёмка — set0_shot_game.py как есть (env выставляем здесь и исполняем его файл).
#   Запуск: python Tools/set0_run_game.py Tools/set0_shot_mic_rename.py --map /Game/Map/alpen/alpen
import os, unreal

WANT = {  # слот контейнера 3 (= itemType) → имя меша части (папка Models/Char)
    1: "ch1_m_00_chst310_00_00_0", 2: "ch1_m_00_pant310_00_00_0", 3: "ch1_m_00_shoe310_00_00_0",
    4: "ch1_m_00_grov310_00_00_0", 0: "ch1_m_00_hcap310_00_00_0", 9: "ch1_n_90_swrd308_00_00_0"}
WANT_F = {1: "ch1_f_00_chst310_00_00_0", 2: "ch1_f_00_pant310_00_00_0", 3: "ch1_f_00_shoe310_00_00_0",
          4: "ch1_f_00_grov310_00_00_0", 0: "ch1_f_00_hcap310_00_00_0"}

def find_codes():
    it = None
    for o in unreal.ObjectIterator(unreal.AsdaItemTableSubsystem):
        it = o
    if it is None:
        unreal.log_warning("[mic_rename] UAsdaItemTableSubsystem не найден")
        return {}
    found = {}
    need = dict(WANT)
    for code in range(1, 60000):
        if not need:
            break
        m = it.get_item_mesh_name(code, True)
        if not m:
            continue
        for slot, mesh in list(need.items()):
            if m == mesh:
                f = it.get_item_mesh_name(code, False)
                found[slot] = (code, m, f)
                unreal.log("[mic_rename] слот %d: код %d meshM=%s meshF=%s" % (slot, code, m, f))
                del need[slot]
    if need:
        unreal.log_warning("[mic_rename] не нашёл коды для %s" % need)
    return found

codes = find_codes()
cmds = ["asda.Inven.Put 3 %d %d" % (slot, c[0]) for slot, c in sorted(codes.items())]
os.environ["ASDA_SET0_EXEC_POST"] = ";".join(cmds)
unreal.log("[mic_rename] EXEC_POST=%s" % os.environ["ASDA_SET0_EXEC_POST"])
os.environ.setdefault("ASDA_SET0_KEY", "alpen")
os.environ.setdefault("ASDA_SET0_TAG", "mic_rename")
os.environ.setdefault("ASDA_SET0_SPAWN_TILE", "124,208")
os.environ.setdefault("ASDA_SET0_CAMS", "@@ASTORY_DEV_ROOT_FWD@@/Reports/render_sets_2026-09-05/set0_alpen_cams.json")
os.environ.setdefault("ASDA_SET0_ONLY", "k1_a1_0208")
os.environ.setdefault("ASDA_SET0_HERO_CAM", "130,-4,155,90|face;320,-12,70,90|body;320,-12,70,-90|back")
_p = os.path.join(os.path.dirname(os.path.abspath(__file__)), "set0_shot_game.py")
exec(compile(open(_p, encoding="utf-8").read(), _p, "exec"))
