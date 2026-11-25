# Copyright (c) 2026 @sitxovski (Discord)
# asda_gacha_fix_texture_mips.py — снять мип-цепочку с текстур сцены премиум-гачи ПО ДАННЫМ
# оригинала.
#
# ★★★ ЗАЧЕМ (доказано байтами, `Reports/gacha_shadow_black.md` §2).
#   Заголовок `.tex` клиента несёт поле **MipLevels** (`imageHdr[3]`, файл+20), и загрузчик
#   передаёт его прямо в `D3DXCreateTextureFromFileInMemoryEx` (font_text.c:3869/3891).
#   У ВСЕХ 3330 файлов `Data/Character/Texture/*.tex` там стоит **1** ⇒ у оригинала мип-цепочки
#   нет вовсе, и на любом расстоянии рисуется мип 0.
#   У нас же `mip_gen_settings = FROM_TEXTURE_GROUP` (группа WORLD) строит полную цепочку, и на
#   сцене с ПЯТЬЮ автоматами видеокарта берёт мип 2-3. Замер (`asda_gacha_shadow_check.py`):
#   медианная насыщенность корпуса падает 0.677 → 0.352, непрозрачность ядра тени 0.800 → 0.733
#   при медиане 0.239 → 0.118. Это ровно жалоба владельца: «бледные лутбоксы, серые тени».
#
# ★ НИЧЕГО НЕ ВЫДУМАНО: число мипов для каждой текстуры читается из ЕЁ ЖЕ `.tex` в
#   `Donors/TRANSLATE_unpacked/textures_orig`. Если файла нет — текстура пропускается и печатается предупреждение
#   (никаких «поставим на глаз»).
#
# Запуск: run_headless.bat asda_gacha_fix_texture_mips.py
import os
import struct
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import asda_fmt_tex as _FT   # общий разбор заголовка .TEX (волна 2 аудита дублей, E9)
import asda_ue_paths as _U   # ★ волна 2 п. 7 (14.09.2026): корни и правила путей — реестр (Tools/data/asda_asset_paths.json)

import unreal

EAL = unreal.EditorAssetLibrary
MEL = unreal.MaterialEditingLibrary
AR = unreal.AssetRegistryHelpers.get_asset_registry()

GACHYA_ROOT = _U.G_MODELS_GACHYA
TEX_ORIG_DIRS = [r"@@ASTORY_DEV_ROOT_FWD@@/Donors/TRANSLATE_unpacked/textures_orig/Data/Character/Texture"]
TEX_MAGIC = 0x20040603


def log(m):
    unreal.log("[гача-мипы] " + str(m))


def orig_mip_levels(tex_name):
    """MipLevels из заголовка оригинального `.tex`; None — файла нет."""
    for d in TEX_ORIG_DIRS:
        for cand in (tex_name, tex_name.lower(), tex_name.upper()):
            p = os.path.join(d, cand + ".tex")
            if os.path.isfile(p):
                with open(p, "rb") as f:
                    head = f.read(_FT.HDR)
                # ★ 14.09.2026: MipLevels = поле +0x14 (эталон font_text.c:3860-3891: imageHdr = файл+8,
                #   imageHdr[3]). Прежний разбор `<7I` брал +0x10 — там на данных ВСЕГДА 1 (сверено с DDS
                #   dwMipMapCount у 1915/1915). Для 11 текстур гачи (PNG) +0x14 тоже 1 — результат тот же.
                hdr = _FT.read_header(head)
                if hdr is None:
                    return None
                return (hdr.mips, hdr.width, hdr.height, hdr.fmt)
    return None


def main():
    seen = {}
    for ad in AR.get_assets_by_path(unreal.Name(GACHYA_ROOT), recursive=True):
        if str(ad.asset_class_path.asset_name) != "MaterialInstanceConstant":
            continue
        mic = EAL.load_asset(str(ad.package_name))
        if not mic:
            continue
        tex = MEL.get_material_instance_texture_parameter_value(mic, "Tex")
        if tex:
            seen[tex.get_path_name()] = tex
    log("текстур сцены гачи найдено: %d" % len(seen))

    fixed, already, missing = 0, 0, []
    for path, tex in sorted(seen.items()):
        name = tex.get_name()
        info = orig_mip_levels(name)
        if info is None:
            missing.append(name)
            log("⚠ %s — оригинального .tex не нашлось, НЕ трогаю" % name)
            continue
        mips, w, h, fmt = info
        want = unreal.TextureMipGenSettings.TMGS_NO_MIPMAPS if mips == 1 \
            else unreal.TextureMipGenSettings.TMGS_FROM_TEXTURE_GROUP
        cur = tex.get_editor_property("mip_gen_settings")
        if cur == want:
            already += 1
            log("%-22s уже %s (оригинал %dx%d, MipLevels=%d)" % (name, want, w, h, mips))
            continue
        tex.set_editor_property("mip_gen_settings", want)
        # ★ Без мипов текстуру нельзя стримить кусками — держим целиком, иначе UE подставит
        #   низкое разрешение и мы вернёмся к той же мыльности с другой стороны.
        tex.set_editor_property("never_stream", True)
        if not EAL.save_loaded_asset(tex):
            raise RuntimeError("НЕ СОХРАНИЛОСЬ: %s (редактор открыт? Error Code 32)" % path)
        fixed += 1
        log("%-22s %s -> %s  (оригинал %dx%d, MipLevels=%d, формат %d)"
            % (name, cur, want, w, h, mips, fmt))

    log("=== готово: изменено %d, уже верно %d, без оригинала %d %s ==="
        % (fixed, already, len(missing), missing if missing else ""))
    if missing:
        raise RuntimeError("не нашлось оригиналов .tex для: %s" % ", ".join(missing))


if __name__ == "__main__":
    main()
