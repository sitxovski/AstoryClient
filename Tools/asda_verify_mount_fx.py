# Copyright (c) 2026 @sitxovski (Discord)
# asda_verify_mount_fx.py — проверка ассетов ШЛЕЙФОВ и ЗВУКОВ ЕЗДЫ (mount_system_re.md §6.5).
# Считает: BP_Fx_* шлейфов и у скольких непустой список эмиттеров; строки DT_RideEffect (или CSV,
#   если структура ещё не собрана); наличие USoundWave по всем звуковым полям таблицы (+68/+132/+196).
# Запуск: Tools/run_headless.bat Tools\asda_verify_mount_fx.py
import struct, unreal
import os as _au_os, sys as _au_sys   # ★ волна 2 п. 7 (14.09.2026): реестр путей ассетов (Tools/asda_ue_paths.py)
_au_sys.path.insert(0, _au_os.path.dirname(_au_os.path.abspath(__file__)))
import asda_ue_paths as _U   # ★ волна 2 п. 7 (14.09.2026): корни и правила путей — реестр (Tools/data/asda_asset_paths.json)

import asda_paths as _P   # ★ гигиена 15.09.2026 (§4 п.2): корни путей — Tools/asda_paths.py (ASDA_ROOT и др.)
BIN = _P.ROOT + "\\AstoryClient\\RawAssets\\Data\\Character\\Ride_effect_table.bin"
CSV = _P.ROOT + "\\AstoryClient\\RawAssets\\Data\\RideEffect.csv"
FX_PKG, SND_PKGS = _U.G_EFFECTS, (_U.G_SOUND, _U.G_SOUNDS_ALT)
DT = "/Game/AsdaAssets/Data/DT_RideEffect"
HDR, REC = 20, 260
EAL = unreal.EditorAssetLibrary


def log(m): unreal.log_warning("[verify-mount-fx] %s" % m)


def table():
    b = open(BIN, "rb").read()
    cnt = struct.unpack_from("<i", b, 4)[0]
    effs, snds = [], []
    for i in range(cnt):
        o = HDR + REC * i
        e = b[o + 4:o + 68].split(b"\0")[0].decode("latin1").lower()
        if e.endswith(".jeb"): e = e[:-4]
        if e and e not in effs: effs.append(e)
        for off in (68, 132, 196):
            s = b[o + off:o + off + 64].split(b"\0")[0].decode("latin1").lower()
            if s.endswith(".wav"): s = s[:-4]
            if s and s not in snds: snds.append(s)
    return cnt, sorted(effs), sorted(snds)


def find_sound(n):
    for p in SND_PKGS:
        f = "%s/%s" % (p, n)
        if EAL.does_asset_exist(f): return f
    # регистр в именах файлов оригинала гуляет (s_e_riding_Cbike.wav) — ищем по реестру
    ar = unreal.AssetRegistryHelpers.get_asset_registry()
    for p in SND_PKGS:
        for a in ar.get_assets_by_path(p, recursive=True):
            if str(a.asset_name).lower() == n: return str(a.package_name)
    return None


def main():
    cnt, effs, snds = table()
    log("Ride_effect_table: записей=%d, уникальных эффектов=%d, уникальных звуков=%d" % (cnt, len(effs), len(snds)))

    have_bp = empty = 0
    miss_bp = []
    for e in effs:
        full = "%s/%s" % (FX_PKG, _U.fx_bp_name(e))   # ★ волна 2 п. 7: как рантайм («-»→«_»; в данных дефисов нет — строка та же)
        if not EAL.does_asset_exist(full):
            miss_bp.append(e); continue
        have_bp += 1
        bp = EAL.load_asset(full)
        try:
            n = len(unreal.get_default_object(bp.generated_class()).get_editor_property("emitters"))
        except Exception:
            n = -1
        if n <= 0:
            empty += 1; log("  ПУСТОЙ список эмиттеров: %s" % full)
    log("ШЛЕЙФЫ: BP=%d/%d, из них с непустыми эмиттерами=%d; нет BP: %s"
        % (have_bp, len(effs), have_bp - empty, ", ".join(miss_bp) or "—"))

    ok = []
    miss_snd = []
    for s in snds:
        (ok if find_sound(s) else miss_snd).append(s)
    log("ЗВУКИ: есть в Content=%d/%d; отсутствуют (%d): %s"
        % (len(ok), len(snds), len(miss_snd), ", ".join(miss_snd) or "—"))

    if EAL.does_asset_exist(DT):
        d = EAL.load_asset(DT)
        log("DT_RideEffect: строк=%d" % len(d.get_row_names()))
    else:
        try:
            n = sum(1 for _ in open(CSV, encoding="utf-8")) - 1
        except Exception:
            n = -1
        log("DT_RideEffect ЕЩЁ НЕ СОЗДАН (нужна сборка C++ FAsdaRideEffectRow); CSV готов: строк=%d" % n)


# ★ ЗАКОН [[tools-no-import-side-effects]]: при импорте модуль НИЧЕГО не делает.
#   `__spec__` выставляет ЛЮБАЯ машинерия импорта (import / importlib / runpy) и НЕ
#   выставляет прямое исполнение файла (python file.py, UE -ExecutePythonScript).
#   `python -m` даёт `__name__ == "__main__"` и покрыт первым условием.
if __name__ == "__main__" or globals().get("__spec__") is None:
    main()
