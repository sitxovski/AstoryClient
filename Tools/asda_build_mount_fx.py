# Copyright (c) 2026 @sitxovski (Discord)
# asda_build_mount_fx.py — BP-заготовки ШЛЕЙФОВ ЕЗДЫ на транспорте (mount_system_re.md §5).
# ЭТАЛОН: charRideEffectTick (item_inventory.c, вызов из тика «в седле и state==1»):
#   акк. 2500 мс → Effect_SpawnByName(rec+4 /*EffectFile*/, кость "M_DUMMY"/"G_DUMMY", attach 6, scale 1.0);
#   акк. 2000 мс → loop-звук rec+68 (2D для себя / 3D для чужих).
# Имя эффекта берётся из Data/Ride_effect_table.bin (поле EffectFile@+4), т.е. список строится
#   ПО ДАННЫМ, а не руками. Файлы .jeb — в @@ASTORY_DEV_ROOT@@\Donors\TRANSLATE_unpacked\models_orig\Data\Effect\Model.
# Пайплайн — копия asda_build_char_fx.py (parse_jeb + PE.build_emitter + BP на AsdaEffectActor).
# BP-имя = BP_Fx_<база jeb>.  Запуск: Tools/run_headless.bat Tools\asda_build_mount_fx.py
# ★ Аудит №4 (волна 3): форма сборки BP переехала в `asda_fx_common`; здесь остались ТОЛЬКО
#   отличия канала транспорта.
import sys, os, struct
import os as _os, sys as _sys
_sys.path.insert(0, _os.path.dirname(_os.path.abspath(__file__)))
import asda_paths as _P
import asda_fx_common as FXC
import asda_ue_paths as _U   # ★ волна 2 п. 7 (14.09.2026): корни и правила путей — реестр (Tools/data/asda_asset_paths.json)
try: sys.stdout.reconfigure(encoding="utf-8", errors="replace")
except Exception: pass
import unreal

SRC_DIR = _P.EFFECT_MODEL_ORIG_DIR
TABLE   = os.path.join(_P.ROOT, "AstoryClient", "RawAssets", "Data", "Character", "Ride_effect_table.bin")
OUT_PKG = _U.G_EFFECTS
REC     = 260          # запись Ride_effect_table (loadRideEffectTable @0xF23980: fread 0x104)
HDR     = 20

# ★ Хардкод-эффекты клиента ВНЕ таблицы (имена зашиты строками в EXE):
#   itm1_n_00_vehicle_0000_e — «вспышка белого дыма» посадки/высадки транспорта.
#   Эталон: посадка (свой, 0x154B) Recv_MountVehicleRide@0xF2BCF0; высадка (bcast 0x154E)
#   CItem_OnRemoveByTable2Record@0xF264F0 и авто-высадка CChar_SetState@0xA13D50 case 5/24 —
#   везде Effect_SpawnByName на кости M_DUMMY/G_DUMMY, attach 7, scale 1.0.
EXTRA   = ["itm1_n_00_vehicle_0000_e"]


def log(m): unreal.log_warning("[mountfx] %s" % m)


def table_effects():
    """Уникальные имена .jeb из Ride_effect_table.bin (EffectFile@+4), в нижнем регистре, без расширения."""
    b = open(TABLE, "rb").read()
    cnt = struct.unpack_from("<i", b, 4)[0]
    assert b[12:15] == b"RET", "не Ride_effect_table.bin"
    out = []
    for i in range(cnt):
        o = HDR + REC * i
        s = b[o + 4:o + 68].split(b"\0")[0].decode("latin1").lower()
        if s.endswith(".jeb"): s = s[:-4]
        if s and s not in out: out.append(s)
    return sorted(out)


def build_one(J, PE, eff_name):
    out_name = FXC.bp_name(eff_name)
    jb = FXC.read_jeb(J, SRC_DIR, eff_name, log)
    if jb is None: return None
    emitters = jb.get("emitters", [])
    blend_add = FXC.blend_mode(jb)
    full = "%s/%s" % (OUT_PKG, out_name)
    bp, cdo = FXC.ensure_effect_bp(out_name, OUT_PKG, log)
    if bp is None: return None
    cdo.set_editor_property("effect_name", eff_name)
    ems = []
    for em in emitters:
        try:
            # ★★★ kf_seed_nodes (этап 3 раскатки, `Reports/item_fx_ring_motion.md`): трек в
            #   эталоне = СТАРТ-сид(t=0) + ключи файла + КОНЕЦ-сид(t=lifetime); в файле лежат только
            #   промежуточные ключи. Замер по каналу транспорта: 4 эффекта с ключами, колец с
            #   ключами 0, квадов 95 (у квадов возвращается потерянный КОНЕЦ-узел).
            #   Откат: `ASDA_FX_NO_KF_SEED=1` + пересборка канала.
            # ★★★ etalon_end (2026-08-14, `Reports/fx_scale_timing_audit.md` §4): у канала
            #   транспорта переопределения `end_mode` НЕ БЫЛО ⇒ ветки growQuad/mesh12 форсили
            #   `end_mode = 1` (вечный рестарт), хотя на диске у 349 эмиттеров `endMode = 0` (СТОП).
            #   Формула — `PE.etalon_end_mode` (дизасм двух машин обновления).
            #   Откат: `ASDA_FX_NO_ETALON_ENDMODE=1` + пересборка канала.
            fx = PE.build_emitter(em, blend_add,
                                  kf_seed_nodes=FXC.kf_seed_enabled(),
                                  etalon_end=FXC.etalon_end_enabled())
            if fx is None:
                log("  %s: эмиттер %s (тип 0x%X) → None (не поддержан)" % (eff_name, em.get("name", "?"), em.get("type", 0)))
                continue
            # ★ Строгие оси эффекта на ЖИВОЙ КОСТИ (здесь M_DUMMY/G_DUMMY) — как в char-fx, сверено
            #   по live.dmp. Карты остаются на дефолтах False (§H6/§H3/§H10). Разбор — в шапке
            #   `asda_fx_common.apply_bone_fx_flags`. Набор квадов у транспорта БЕЗ типа 2.
            FXC.apply_bone_fx_flags(fx, em.get("type"))
            ems.append(fx)
        except Exception as ex:
            log("  %s: эмиттер %s (тип 0x%X) пропущен: %s" % (eff_name, em.get("name", "?"), em.get("type", 0), ex))
    # ★ ЧЕСТНО ПРО СОХРАНЕНИЕ (2026-08-21, см. шапку `asda_fx_common.finish_effect_bp`):
    #   занятый другим процессом `.uasset` НЕ сохраняется, а строка «создан…» печаталась всё равно.
    if not FXC.finish_effect_bp(bp, cdo, ems):
        log("НЕ СОХРАНЁН %s: файл занят другим процессом (закрой редактор UE и повтори)" % full)
        return 0
    log("создан %s: эмиттеров %d, бленд=%s" % (full, len(ems), blend_add))
    return len(ems)


def main():
    mods = FXC.import_fx_modules(log)
    if mods is None: return
    J, PE = mods
    FXC.point_pe_to(PE, OUT_PKG)
    jebs = table_effects()
    for eff in EXTRA:
        if eff not in jebs: jebs.append(eff)
    log("в таблице уникальных эффектов: %d (+%d хардкод клиента)" % (len(jebs) - len(EXTRA), len(EXTRA)))
    ok = empty = 0
    missing = []
    for eff in jebs:
        n = build_one(J, PE, eff)
        if n is None: missing.append(eff)
        else:
            ok += 1
            if n == 0: empty += 1
    log("ГОТОВО: BP создано %d/%d (пустых эмиттер-списков %d)" % (ok, len(jebs), empty))
    if missing: log("НЕТ .jeb (пропущены): %s" % ", ".join(missing))


# ★★★ ЗАКОН [[tools-no-import-side-effects]]. ЗДЕСЬ СТОЯЛ ГОЛЫЙ `main()` (аудит №4, волна 3):
#   любой `import asda_build_mount_fx` молча пересобирал и перезаливал ВЕСЬ канал транспорта.
#   Замер сухим прогоном подтвердил дефект: на ОДНОМ импорте создавалось 30 BP на 405 эмиттеров.
if FXC.is_direct_run(globals()):
    main()
