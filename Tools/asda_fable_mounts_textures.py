# Copyright (c) 2026 @sitxovski (Discord)
# asda_fable_mounts_textures.py — текстуры НОВЫХ маунтов донора AsdaFableClassic → PNG для импорта в UE.
# Донор: @@ASTORY_DEV_ROOT@@\Donors\AsdaFableClassic (отчёт Reports/fable_mounts/FABLE_MOUNTS.md).
# Берёт из Data\Character\Texture\Texture.btz донора ТОЛЬКО <модель>_t.tex моделей itm1_n_96_vehicle_0053…0080
# (эталон SetParts_RidingSystem@0x9EC900 п.7: транспорт носит одну текстуру <ModelEntry>_t во всех подсетках)
# и пишет PNG в RawAssets/Textures/Data/Character/Texture — туда же, где лежат _t прежних 52 моделей,
# чтобы asda_import_textures.py разложил их в /Game/AsdaAssets/Textures/Characters/Texture.
# Уже существующие PNG не перезаписываются (только новые имена).
# Запуск: python asda_fable_mounts_textures.py
import os, io, re, sys, zipfile
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import asda_paths as _P
import asda_extract_archive_textures as _X   # тот же декодер .TEX (0x24 заголовок + DDS/PNG/…), что у прежних текстур

DONOR_BTZ = os.path.join(_P.DONORS, "AsdaFableClassic", "Data", "Character", "Texture", "Texture.btz")
OUT = os.path.join(_P.PORT_DIR, "AstoryClient", "RawAssets", "Textures", "Data", "Character", "Texture")
NEW_T = re.compile(r"^itm1_n_96_vehicle_00(5[3-9]|[67]\d|80)_t\.tex$", re.I)   # 28 новых моделей 0053…0080


def main():
    z = zipfile.ZipFile(DONOR_BTZ)
    done, skip = 0, 0
    for name in sorted(z.namelist()):
        if not NEW_T.match(os.path.basename(name)):
            continue
        dst = os.path.join(OUT, os.path.splitext(os.path.basename(name))[0].lower() + ".png")
        if os.path.exists(dst):
            skip += 1
            continue
        payload = z.read(name)[_X.HDR:]
        img = _X.to_rgba(payload)
        img.save(dst)
        done += 1
        print("  +", os.path.basename(dst), img.size)
    print("ГОТОВО: новых PNG %d, уже было %d" % (done, skip))


if __name__ == "__main__":
    main()
