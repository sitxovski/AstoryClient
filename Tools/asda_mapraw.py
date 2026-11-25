# Copyright (c) 2026 @sitxovski (Discord)
# asda_mapraw.py — ЕДИНЫЙ последовательный ридер промежуточного формата `.mapraw` (магия 'AMRW').
#
# .mapraw пишет экспортёр карт (MapViewer) — это НЕ формат игры, а поток секций:
#   'AMRW' | u32 version | str name | SCENE | TERRAIN | OBJECTS | ... (состав зависит от версии)
# где str = u32 длина + байты latin1. Всё читается строго последовательно, поэтому ридер —
# просто курсор по буферу.
#
# ЗАЧЕМ модуль: тело `class R` было скопировано в 5 тулов. Отличия копий — почти целиком
# косметика (пробелы), КРОМЕ реально значимых:
#   • `fa(n)` возвращал list в 4 копиях и tuple в asda_mapanim_to_gltf.py;
#   • `u16a/u32a` были только в asda_mapanim_to_gltf.py;
#   • `skip` был в 3 копиях из 5; в asda_verify_ground_height.py метод звался `farr`, не `fa`.
# Здесь ОБЪЕДИНЕНИЕ: есть все методы, `fa` отдаёт list, `fa_t`/`farr` — tuple (как было у своих
# потребителей), так что ни одно поведение не потеряно.
#
# ★ Модуль БЕЗ побочных эффектов при импорте (правило tools-no-import-side-effects).

import struct

MAGIC = b"AMRW"


class R(object):
    """Последовательный курсор по буферу .mapraw (little-endian)."""

    def __init__(s, d):
        s.d = d
        s.o = 0

    # --- скаляры ---
    def u32(s):
        v = struct.unpack_from("<I", s.d, s.o)[0]; s.o += 4; return v

    def i32(s):
        v = struct.unpack_from("<i", s.d, s.o)[0]; s.o += 4; return v

    def f32(s):
        v = struct.unpack_from("<f", s.d, s.o)[0]; s.o += 4; return v

    # --- массивы ---
    def fa(s, n):
        """n float → list (поведение asda_export_effects / place_sounds / add_uvscroll)."""
        v = struct.unpack_from("<%df" % n, s.d, s.o); s.o += 4 * n; return list(v)

    def fa_t(s, n):
        """n float → tuple (поведение asda_mapanim_to_gltf / verify_ground_height)."""
        v = struct.unpack_from("<%df" % n, s.d, s.o); s.o += 4 * n; return v

    farr = fa_t          # историческое имя из asda_verify_ground_height.py

    def u16a(s, n):
        v = struct.unpack_from("<%dH" % n, s.d, s.o); s.o += 2 * n; return v

    def u32a(s, n):
        v = struct.unpack_from("<%dI" % n, s.d, s.o); s.o += 4 * n; return v

    def u8(s, n):
        v = s.d[s.o:s.o + n]; s.o += n; return v

    # исторические имена из asda_build_map_level.py (class Reader)
    u16arr = u16a
    u32arr = u32a
    u8arr = u8

    # --- прочее ---
    def skip(s, n):
        s.o += n

    def s_(s):
        n = s.u32(); v = s.d[s.o:s.o + n].decode("latin1", "ignore"); s.o += n; return v

    s = s_               # историческое имя из asda_build_npc_manifest.py

    def sb(s):
        """Строка БЕЗ декодирования (поведение asda_add_uvscroll.py: строки только пропускаются)."""
        n = s.u32(); v = s.d[s.o:s.o + n]; s.o += n; return v


Reader = R               # историческое имя класса из asda_build_map_level.py


# ------------------------------------------------------------------ сценовый заголовок
# Раскладка сразу за именем карты:
#   u32 fog (ARGB) | f32 fogStart | f32 fogEnd | f32×3 ambient | f32×3 diffuse | f32×3 lightDir
# Копии этого чтения жили в asda_build_map_level (Reader/read_mapraw), asda_convert_map_lit,
# asda_revert_map_lit, update_map_lighting (read_scene) и asda_build_sky_dome (parse_mapraw_fog —
# по абсолютным офсетам). Отличались ТОЛЬКО типом контейнера (list/tuple) и поведением на не-AMRW,
# поэтому здесь оба варианта явные: as_list=… и strict=….
SCENE_FIELDS_SIZE = 4 + 4 + 4 + 3 * 3 * 4      # fog + fogStart + fogEnd + 3 вектора по 3 float

# ★★★ ВЕРСИЯ 3 (2026-09-03, правка Д-1 аудита света): третий вектор сцены — НАПРАВЛЕНИЕ НА СОЛНЦЕ
#   (единичный, игровые оси Y-вверх, «от поверхности к солнцу»), взятое экспортёром ГОТОВЫМ из
#   `<map>.SCE @0xB4` — ровно тот вектор, которым светит клиент (g_vLightDir в live.dmp alpen ==
#   файл @0xB4; HLSL EmptyProject.fx:329 `max(0, dot(Normal, lhtDir))`). В ver<=2 в этом слоте лежал
#   пересчёт из углов НЕВЕРНОЙ формулой (ошибка 30..90° на всех 22 картах, 9 карт — горизонт вместо
#   зенита). Раскладка секции не менялась, поэтому геометрические потребители читают ver 2 и 3
#   одинаково, а ПОТРЕБИТЕЛИ СВЕТА обязаны брать ключ 'sunDir' — он None для ver<=2, чтобы старый
#   файл не прочитался молча с неверным солнцем (см. require_sun_dir).
SUN_DIR_MIN_VER = 3


def argb_to_rgb(argb):
    """u32 0xAARRGGBB → (r, g, b) в 0..1 (поведение asda_build_sky_dome.parse_mapraw_fog)."""
    return (((argb >> 16) & 0xFF) / 255.0, ((argb >> 8) & 0xFF) / 255.0, (argb & 0xFF) / 255.0)


def srgb_to_linear(c):
    """Один канал sRGB 0..1 → линейный. Формула UE FLinearColor(FColor) / FromSRGBColor (Engine Core Math/Color.h:68):
    c > 0.04045 ? pow(c/1.055 + 0.0521327, 2.4) : c/12.92  (0.0521327 = 0.055/1.055). ★ 06.09.2026 fog_srgb:
    байты цвета тумана .mapraw — sRGB (D3DRS_FOGCOLOR без SRGBWRITEENABLE), UE ждёт линейную яркость."""
    c = max(0.0, min(1.0, float(c)))
    return c / 12.92 if c <= 0.04045 else ((c + 0.055) / 1.055) ** 2.4


def rgb_srgb_to_linear(rgb):
    """(r, g, b) sRGB 0..1 → линейный кортеж (поканально srgb_to_linear)."""
    return tuple(srgb_to_linear(v) for v in rgb[:3])


def scene_fields(r, as_list=False, ver=None):
    """Прочитать поля сцены из курсора, стоящего СРАЗУ ЗА именем карты.

    as_list=False → векторы tuple (поведение asda_build_map_level.read_mapraw: r.farr(3));
    as_list=True  → векторы list  (поведение read_scene в *_map_lit / update_map_lighting).
    Порядок чтения строго последовательный, поэтому курсор двигается одинаково в обоих режимах.
    ver — версия файла (из open_mapraw); при ver>=SUN_DIR_MIN_VER третий вектор дублируется в
    ключ 'sunDir' (направление НА солнце из .SCE @0xB4), иначе 'sunDir' = None. Ключ 'lightDir'
    оставлен для совместимости: в ver<=2 это вектор неверной формулы, в ver>=3 — тот же sunDir.
    """
    v = (lambda: list(r.fa_t(3))) if as_list else (lambda: r.fa_t(3))
    sc = {"fog": r.u32(), "fogStart": r.f32(), "fogEnd": r.f32(),
          "ambient": v(), "diffuse": v(), "lightDir": v()}
    sc["sunDir"] = sc["lightDir"] if (ver is not None and ver >= SUN_DIR_MIN_VER) else None
    return sc


def require_sun_dir(sc, path=""):
    """Направление НА солнце из сцены .mapraw или ValueError, если файл старой версии.

    Потребители света (asda_convert_map_lit и т.п.) обязаны звать это, а не читать 'lightDir':
    ver<=2 несёт неверный вектор, и молча светить им нельзя — файл надо перегнать
    (`AsdaMapViewer.exe <mapRoot> --export ALL @@ASTORY_DEV_ROOT_FWD@@/Data/Export/maps`)."""
    sd = sc.get("sunDir")
    if sd is None:
        raise ValueError("%s: .mapraw ver=%s без sunDir — перегони экспорт (нужна ver>=%d)"
                         % (path or sc.get("name"), sc.get("ver"), SUN_DIR_MIN_VER))
    return sd


def read_scene(path, strict=True, as_list=True, header_bytes=4096):
    """Сценовый заголовок .mapraw по пути. Возвращает dict (+ ver/name/fog_rgb) или None.

    strict=True  — не-AMRW поднимает ValueError (поведение *_map_lit / update_map_lighting: assert);
    strict=False — не-AMRW возвращает None (поведение asda_build_sky_dome: фолбэк на туман по умолчанию).
    header_bytes — сколько байт файла читать (копии читали 4096; заголовок заведомо короче).
    """
    with open(path, "rb") as f:
        d = f.read(header_bytes) if header_bytes else f.read()
    if d[:4] != MAGIC:
        if strict:
            raise ValueError("не .mapraw (нет 'AMRW'): %s" % path)
        return None
    r = R(d)
    r.o = 4
    ver = r.u32()
    name = r.s_()
    sc = scene_fields(r, as_list=as_list, ver=ver)
    sc["ver"] = ver
    sc["name"] = name
    sc["fog_rgb"] = argb_to_rgb(sc["fog"])
    return sc


def open_mapraw(path):
    """Открыть .mapraw, проверить магию, вернуть (R, version, name) с курсором сразу за именем."""
    with open(path, "rb") as f:
        d = f.read()
    if d[:4] != MAGIC:
        raise ValueError("не .mapraw (нет 'AMRW'): %s" % path)
    r = R(d)
    r.o = 4
    ver = r.u32()
    name = r.s_()
    return r, ver, name


def is_mapraw(path):
    try:
        with open(path, "rb") as f:
            return f.read(4) == MAGIC
    except Exception:
        return False
