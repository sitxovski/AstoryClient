# -*- coding: utf-8 -*-
# Copyright (c) 2026 @sitxovski (Discord)
r"""
asda_fx_tex_from_donor.py — ТЕКСТУРЫ ЭФФЕКТОВ НАВЫКОВ, КОТОРЫХ НЕТ В ПАКЕТЕ ЭТАЛОНА, — из донора AsdaFableClassic (10.11.2026, job4-vfx).

ЗАЧЕМ. Эффекты 4-й профессии (`Ch1_a_4th_*.jeb`, `eff_*`) ссылаются на текстуры, которых нет в `Data\Effect\Texture\Texture.btz`
  эталонного клиента (AsdaChroniclesInterlude и его копии): 4skill_dia, 4skill_dragon, 4skill_eye02/02a/02b, drop-01, face_4skill,
  mon_s_0_rwolf01-e_02. Эталон грузит текстуры эффектов ТОЛЬКО из этого пакета (`CEffectMgr` ctor, effects_particles.c:58238;
  `Effect_LoadTexture@0x116D190` кладёт в слот 0 `default.bmp` → `default.tex`). Если записи нет в пакете,
  `CTextureManager_LoadTextureCached@0x116D750` (font_text.c:4941-5135, ветка LABEL_47/48) записывает имя в первый свободный слот
  и возвращает ЕГО — с пустым указателем текстуры: эмиттер у оригинала рисуется без текстуры (ревизия 10.11; прежняя пометка
  «рисуется default.tex с красным No» неверна — слот 0 возвращается только при иных ошибках).
  Порт без текстуры рисовал движковую DefaultTexture — белый квадрат (жалоба «у навыков 4-й профессии нет эффектов»).
  [ИСПРАВЛЕНИЕ] Донор AsdaFableClassic — тот же контент: все 60 `.jeb` 4-й профессии в его EffectModel.bnz побайтно равны
  эталонным, а в его Texture.btz эти 8 текстур есть. Ничего не выдумывается — берётся файл с тем же именем из того же контента.

ЧТО ДЕЛАЕТ. Достаёт перечисленные .tex из Texture.btz донора, декодирует общим `asda_fmt_tex` (как `asda_extract_effect_textures.py`,
  unpremultiply=True) и кладёт PNG в общий источник импорта текстур эффектов `Data/Export/effects/tex_misc/<имя>.png`.
  Уже лежащий PNG не перезаписывается. Импорт в `/Game/AsdaAssets/Effects/Tex` и привязка к MI — `asda_fx_tex_donor_import_ue.py`.
  Повторять после любой перегенерации tex_misc из эталона (тот этих файлов не создаёт и не удаляет).

Запуск: python AstoryClient\Tools\asda_fx_tex_from_donor.py [--dry-run]
Модуль без побочных эффектов при импорте (закон tools-no-import-side-effects).
"""
import argparse
import os
import sys
import zipfile

TOOLS = os.path.dirname(os.path.abspath(__file__))                                  # каталог инструментов
DONOR_BTZ = r"@@ASTORY_DEV_ROOT_FWD@@/Donors/AsdaFableClassic/Data/Effect/Texture/Texture.btz"   # пакет текстур эффектов донора
ETALON_BTZ = r"@@ASTORY_DEV_ROOT_FWD@@/Donors/AsdaChroniclesInterlude/Data/Effect/Texture/Texture.btz"   # пакет эталона (проверка «нет»)
OUT = r"@@ASTORY_DEV_ROOT_FWD@@/Data/Export/effects/tex_misc"                               # источник импорта текстур эффектов
# Имена (stem в нижнем регистре, как ассеты `Effects/Tex`) → навыки, у эффектов которых текстура упомянута (сканер 10.11.2026).
NAMES = {
    "4skill_dia": "611, 615, 639, 841, 844",
    "4skill_dragon": "612",
    "4skill_eye02": "820",
    "4skill_eye02a": "820",
    "4skill_eye02b": "820",
    "drop-01": "612, 1042, 1043",
    "face_4skill": "611, 615, 639, 841, 844",
    "mon_s_0_rwolf01-e_02": "824, 843, 846, 1046",
    # ★ 10.11.2026 (ревизия): эффекты СОСТОЯНИЙ 4-й профессии из `system/abSkillInfo.inl` (обработчик `CharFx_SpawnEquipBoneEffects@0xA73C10`).
    "4skill_chain": "состояние 330 (822, 1032)",
    "4skill_eye01": "состояние 316",
    "4skill_gost_01": "состояние 330 (822, 1032)",
    "4skill_gost_02": "состояние 330 (822, 1032)",
    "4skill_tree": "состояние 344 (есть в пакете ЭТАЛОНА — берётся оттуда)",
    "eff_2_char_99_hitoup01-b_00": "состояние 330 (822, 1032)",
    "zzzzzzzzzzzzzzzzzz": "состояние 316",
}


def main():
    """Извлечь PNG. Код: 0 — готово, 2 — какой-то текстуры нет ни в эталоне, ни в доноре."""
    try:
        sys.stdout.reconfigure(encoding="utf-8", errors="replace")
    except Exception:
        pass
    ap = argparse.ArgumentParser()
    ap.add_argument("--dry-run", action="store_true", help="ничего не писать")
    a = ap.parse_args()
    sys.path.insert(0, TOOLS)
    import asda_fmt_tex as FT
    donor = zipfile.ZipFile(DONOR_BTZ)
    dn = {os.path.splitext(n)[0].lower(): n for n in donor.namelist()}
    et = {os.path.splitext(n)[0].lower() for n in zipfile.ZipFile(ETALON_BTZ).namelist()}
    etz = zipfile.ZipFile(ETALON_BTZ)
    etn = {os.path.splitext(n)[0].lower(): n for n in etz.namelist()}
    bad = 0
    for stem, skills in NAMES.items():
        # Источник: пакет ЭТАЛОНА, если текстура там есть (просто не была выгружена), иначе донор Fable.
        if stem in et:
            src_zip, src_name, src_lbl = etz, etn[stem], "эталон"
        elif stem in dn:
            src_zip, src_name, src_lbl = donor, dn[stem], "Fable"
        else:
            print("СТОП %s: нет ни в эталоне, ни в доноре" % stem)
            bad += 1
            continue
        dst = os.path.join(OUT, stem + ".png")
        if os.path.isfile(dst):
            print("уже есть %s" % dst)
            continue
        im = FT.to_pil(src_zip.read(src_name), unpremultiply=True)
        print("%s ← %s %s (%dx%d, %s)%s" % (dst, src_lbl, src_name, im.size[0], im.size[1], skills, " [сухой]" if a.dry_run else ""))
        if not a.dry_run:
            im.save(dst)
    return 2 if bad else 0


if __name__ == "__main__":
    sys.exit(main())
