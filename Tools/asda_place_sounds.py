# Copyright (c) 2026 @sitxovski (Discord)
# asda_place_sounds.py — 3D-звуки карт (Pos.POS секция E) на все карты одной UE-сессией.
# Запись 0x138: wav@+0x04, pos@+0x104, minR@+0x110, maxR@+0x114 (mapraw: pos+wav+minR+maxR).
# UE: AmbientSound (loop, spatialized) + затухание как движок: inner-сфера=minR (полная громкость),
# линейный спад до maxR. Звуки per-map self-contained: /Game/Map/<map>/Sounds.
import os, unreal
import os as _os, sys as _sys
_sys.path.insert(0, _os.path.dirname(_os.path.abspath(__file__)))
import asda_coords as _C
import asda_maps as _MAPS
import asda_ue_paths as _U   # ★ волна 2 п. 7 (14.09.2026): корни и правила путей — реестр (Tools/data/asda_asset_paths.json)
import asda_mapraw as _MR                       # ★ единый ридер .mapraw (был свой class R)
import asda_paths as _P                         # ★ единый реестр корней клиента
import asda_login_guard as _LG                  # ★ 16.09.2026 (обратный обход savedir14): гейт сцены логина

RAW_DIR = _P.EXPORT_MAPS
SND_DIR = _P.SOUND_DIR                          # <CLIENT_MINE>\Data\Sound
EAL = unreal.EditorAssetLibrary
ATOOLS = unreal.AssetToolsHelpers.get_asset_tools()
_asset = _MAPS.map_asset_name                   # ★ единый санитайзер имени карты
to_ue = _C.to_ue_v   # ★ единая конверсия осей (asda_coords)

R = _MR.R

def read_sounds(path):
    r, _ver, _name = _MR.open_mapraw(path)      # проверка магии 'AMRW' + ver + name (был свой assert)
    r.u32(); r.f32(); r.f32(); r.fa(3); r.fa(3); r.fa(3)
    pc = r.u32(); ic = r.u32(); r.skip(2*ic)
    for _ in range(pc):
        vtx = r.u32()
        if vtx > 0: r.skip(vtx*3*4 + vtx*2*4 + vtx*2*4 + vtx*4)
        r.u32(); r.u32(); r.f32(); r.f32(); r.s_(); r.f32(); r.s_(); r.f32()
    mc = r.u32()
    for _ in range(mc):
        r.s_(); r.s_(); v = r.u32(); r.skip(v*8*4); icx = r.u32(); r.skip(icx*4)
    ni = r.u32()
    for _ in range(ni): r.u32(); r.skip(16*4); r.i32(); r.i32(); r.skip(4*4)   # + uvScroll
    bc = r.u32(); r.s_(); r.f32()
    for _ in range(bc):
        wv = r.u32(); r.skip(wv*5*4); wic = r.u32(); r.skip(wic*2)
    ec = r.u32()
    for _ in range(ec):
        r.fa(3); r.s_(); r.i32(); r.i32(); r.fa(16); r.f32(); r.i32()
        r.fa(16); r.i32(); r.i32(); r.s_()
    sc = r.u32(); out = []
    for _ in range(sc):
        pos = r.fa(3); wav = r.s_(); minR = r.f32(); maxR = r.f32()
        trigR = r.f32(); flags = r.i32(); atype = r.i32(); aidx = r.i32()
        out.append({"pos": pos, "wav": wav, "minR": minR, "maxR": maxR,
                    "trigR": trigR, "flags": flags, "attachType": atype, "attachIdx": aidx})
    return out

_wavidx = None
def find_wav_file(name):
    global _wavidx
    if _wavidx is None:
        _wavidx = {f.lower(): os.path.join(SND_DIR, f) for f in os.listdir(SND_DIR)}
    base = os.path.basename(name)
    p = _wavidx.get(base.lower())
    if not p and not base.lower().endswith(".wav"):
        p = _wavidx.get((base + ".wav").lower())
    return p

def ensure_sound(mapname, wav):
    stem = _asset(os.path.splitext(os.path.basename(wav))[0])
    dest = "/Game/Map/%s/Sounds" % _asset(mapname)
    ap = "%s/%s" % (dest, stem)
    if EAL.does_asset_exist(ap):
        return EAL.load_asset(ap)
    src = find_wav_file(wav)
    if not src:
        return None
    d = unreal.AutomatedAssetImportData()
    d.set_editor_property("destination_path", dest)
    d.set_editor_property("filenames", [src])
    d.set_editor_property("replace_existing", True)
    for a in ATOOLS.import_assets_automated(d):
        try:
            a.set_editor_property("looping", True)          # эмбиент-эмиттеры зациклены
            EAL.save_loaded_asset(a)
        except Exception: pass
    return EAL.load_asset(ap) if EAL.does_asset_exist(ap) else None

# ★ Список карт — ЕДИНЫЙ источник asda_maps.py (все 22 карты порта).
#   Было: локальный литерал (13 копий по тулам). Состав сверен 1:1.
MAPS = _MAPS.ALL_MAPS
_only = os.environ.get("ASDA_MAPS", "")
if _only: MAPS = [m for m in MAPS if m in _only.split("||")]

les = unreal.get_editor_subsystem(unreal.LevelEditorSubsystem)
eas = unreal.get_editor_subsystem(unreal.EditorActorSubsystem)


def main():
    """Работа скрипта. Вынесена в функцию, чтобы импорт модуля НИЧЕГО не делал
    (закон [[tools-no-import-side-effects]])."""
    global a
    for mp in MAPS:
        raw = os.path.join(RAW_DIR, mp + ".mapraw")
        lvl = _U.level_path(mp)
        # ★ 16.09.2026 (обратный обход ревизии savedir14): инструмент импортирует .wav в
        #   /Game/Map/<карта>/Sounds, сносит и спавнит акторы и СОХРАНЯЕТ уровень — гейт сцены логина ДО
        #   load_level и до импорта. Спрашиваем обе точки записи: сам уровень и корень карты (звуки под ним).
        if _LG.login_map_write_blocked(lvl, "asda_place_sounds.py", quiet=True) or \
           _LG.login_map_write_blocked("/Game/Map/%s" % _asset(mp), "asda_place_sounds.py", quiet=True):
            continue
        if not os.path.isfile(raw) or os.path.getsize(raw) < 1000 or not EAL.does_asset_exist(lvl):
            continue
        snds = read_sounds(raw)
        les.load_level(lvl)
        for a in list(eas.get_all_level_actors()):
            if a.get_actor_label().startswith("MapSound_"):
                try: eas.destroy_actor(a)
                except Exception: pass
        placed = 0; nowav = 0
        for sd in snds:
            sw = ensure_sound(mp, sd["wav"])
            if not sw: nowav += 1; continue
            amb = eas.spawn_actor_from_class(unreal.AmbientSound, to_ue(sd["pos"]))
            amb.set_actor_label("MapSound_%s" % _asset(os.path.splitext(sd["wav"])[0]))
            ac = amb.get_editor_property("audio_component")
            ac.set_editor_property("sound", sw)
            ac.set_editor_property("auto_activate", True)
            att = unreal.SoundAttenuationSettings()
            att.set_editor_property("attenuate", True)
            att.set_editor_property("attenuation_shape", unreal.AttenuationShape.SPHERE)   # сфера minR
            mn = max(float(sd["minR"]), 0.0); mx = max(float(sd["maxR"]), mn + 1.0)
            att.set_editor_property("attenuation_shape_extents", unreal.Vector(mn, 0.0, 0.0))
            att.set_editor_property("falloff_distance", mx - mn)                            # спад до maxR (как движок)
            ac.set_editor_property("override_attenuation", True)
            ac.set_editor_property("attenuation_overrides", att)
            placed += 1
        les.save_current_level()
        unreal.log("ЗВУКИ %-18s размещено=%d, нет.wav=%d (записей=%d)" % (mp, placed, nowav, len(snds)))
    unreal.log("=== ЗВУКИ КАРТ ГОТОВО ===")


# ★ ЗАКОН [[tools-no-import-side-effects]]: при импорте модуль НИЧЕГО не делает.
#   `__spec__` выставляет ЛЮБАЯ машинерия импорта (import / importlib / runpy) и НЕ
#   выставляет прямое исполнение файла (python file.py, UE -ExecutePythonScript).
#   `python -m` даёт `__name__ == "__main__"` и покрыт первым условием.
if __name__ == "__main__" or globals().get("__spec__") is None:
    main()
