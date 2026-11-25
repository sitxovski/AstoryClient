# Copyright (c) 2026 @sitxovski (Discord)
# asda_fable_mounts_fx.py — headless UE: эффекты донора AsdaFableClassic, которых в порту не было (шлейф колёс, брызги плавания).
# Ride_effect_table донора даёт семейству «пылающих колёс» (0064/0070) шлейф itm1_n_96_vehicle_0033_e.jeb
# (файл донора побайтно равен эталону; в Ride_effect_table основной сборки на него ссылок нет — BP не собирался).
# Собирается ТОЙ ЖЕ функцией build_one штатного asda_build_mount_fx.py, но только недостающие BP — прежние 30 BP
# канала транспорта не пересохраняются. Запуск: python run_headless_safe.py asda_fable_mounts_fx.py --marker "ЗАВЕРШЕНО: fable-шлейфы"
import importlib, os, sys
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))

# (эффект, каталог .jeb; None = штатный SRC_DIR сборщика транспорта — models_orig основной сборки)
NEED = [
    ("itm1_n_96_vehicle_0033_e", None),   # шлейф колёс из RideEffect_tail.csv, которого не было среди BP_Fx_*
    # ★ 23.10.2026: брызги плавания донора (Asda2.exe 0x4D33E0: mode 0 swimeffet01 стоя, mode 1 swimeffet в движении);
    #   .jeb выгружены в Data/Export/FableEffects, текстуры swim / eff_1_char_99_swim1/3 — в effects/tex_misc.
    ("swimeffet", r"@@ASTORY_DEV_ROOT@@\Data\Export\FableEffects"),
    ("swimeffet01", r"@@ASTORY_DEV_ROOT@@\Data\Export\FableEffects"),
]


def main():
    import unreal
    M = importlib.import_module("asda_build_mount_fx")   # импорт без побочных эффектов (FXC.is_direct_run)
    mods = M.FXC.import_fx_modules(M.log)
    J, PE = mods
    M.FXC.point_pe_to(PE, M.OUT_PKG)
    std_src = M.SRC_DIR
    for eff, src in NEED:
        if unreal.EditorAssetLibrary.does_asset_exist("%s/%s" % (M.OUT_PKG, M.FXC.bp_name(eff))):
            M.log("fable: %s уже есть — пропуск" % eff)
            continue
        M.SRC_DIR = src or std_src   # build_one читает .jeb из глобала модуля
        M.log("fable: %s → эмиттеров %s" % (eff, M.build_one(J, PE, eff)))
    M.SRC_DIR = std_src
    unreal.log("ЗАВЕРШЕНО: fable-шлейфы")


if __name__ == "__main__" or globals().get("__spec__") is None:
    main()
