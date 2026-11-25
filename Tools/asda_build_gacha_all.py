# Copyright (c) 2026 @sitxovski (Discord)
# asda_build_gacha_all.py — пересобрать ВСЕ материалы премиум-гачи в ПРАВИЛЬНОМ порядке.
#
# ★ ЗАЧЕМ ОТДЕЛЬНАЯ ТОЧКА ВХОДА. Сборщики мастеров сначала УДАЛЯЮТ старый ассет
#   Порядок: три мастера, затем импортёр, который расставляет родителей и текстуры всем 100 MIC-ам
#   пяти моделей (нужно при переимпорте моделей с нуля).
#
# ★ 2026-08-18: мастера БОЛЬШЕ НЕ УДАЛЯЮТСЯ перед пересборкой —
#   `asda_gacha_light_node.make_or_reset_master` чистит граф НА МЕСТЕ. Поэтому MIC больше не
#   сиротеют, и три сборщика мастеров можно гонять по одному. Заодно это лечит отказ при открытом
#   редакторе: удалить занятый `.uasset` нельзя (`Error Code 32`), и `create_asset` возвращал None
#   посреди сборки.
# ★ Сохранение мастера теперь ЖЁСТКОЕ (`LN.save_master`): если `.uasset` занят (открыт редактор),
#   сборка падает с внятным сообщением, а не рапортует «готово», оставив на диске старую версию.
#
# Запуск: run_headless.bat asda_build_gacha_all.py     (RHI не нужен)
import os as _os
import sys as _sys
_sys.path.insert(0, _os.path.dirname(_os.path.abspath(__file__)))

import unreal

import asda_build_gacha_machine_material as MACHINE
import asda_build_gacha_glass_material as GLASS
import asda_build_gacha_shadow_material as SHADOW
import asda_import_gachya_models as IMPORT
import asda_gacha_fix_texture_mips as MIPS

# (модуль, подпись) — порядок ЗНАЧИМ, см. шапку.
STEPS = [
    (MACHINE, "мастер корпуса и призового шара M_AsdaGachaMachine"),
    (GLASS,   "мастер купола M_AsdaGachaGlass"),
    (SHADOW,  "мастер тени M_AsdaGachaShadow (+ переподвязка MIC теней)"),
    (IMPORT,  "импорт моделей и переподвязка всех MIC на мастера"),
    # ★ ПОСЛЕДНИМ: импорт может подставить текстуры заново, а число мипов читается из `.tex`
    #   оригинала (MipLevels = 1 у всех 3330 файлов) — см. `gacha_shadow_black.md` §2.
    (MIPS,    "снятие мип-цепочки с текстур гачи по MipLevels из .tex"),
]


def main():
    for mod, title in STEPS:
        unreal.log("=== [гача-сборка] %s ===" % title)
        mod.main()
    unreal.log("=== [гача-сборка] ГОТОВО: шагов %d ===" % len(STEPS))


if __name__ == "__main__":
    main()
