# -*- coding: utf-8 -*-
# Copyright (c) 2026 @sitxovski (Discord)
# asda_ui_frame_seam_more.py — ★★ ВТОРОЙ ПЛАН ШВОВ СОСТАВНЫХ РАМОК (09.11.2026, пачка 19, баг 4: окна гачи 68100, эволюции питомца,
#   поиска Друга и поиска группы). [ИСПРАВЛЕНИЕ] — правка АССЕТОВ тем же механизмом, что шаг 24.10 `asda_ui_frame_seam_overlap`
#   (его код применения, сторож «было/стало», сверка снимков), но по ДРУГОМУ закрытому плану `Tools/data/ui_frame_seam_more.json`.
#
#   ПОЧЕМУ ВТОРОЙ ПЛАН. Планировщик 24.10 брал сторону, только если кадр при 1.0 не меняется ни на пиксель; стороны и рамки с
#   полупрозрачной каймой угла он пропускал. Сканер рендером `asda.UI.FrameSeamScanSelfTest` (пурпурная подложка под окном) нашёл на
#   этих сторонах НАСТОЯЩИЕ просветы 1 px (пример: гача 1920×1080 — столбец x=1108 в верхнем ряду). План строит
#   `Tools/asda_ui_frame_seam_more_plan.py` по таблице сканера: в план идут ТОЛЬКО стороны с найденным просветом.
#
# Запуск (редактор и игра ЗАКРЫТЫ):  python Tools\run_headless_safe.py Tools\asda_ui_frame_seam_more.py --marker "шов-рамки: ГОТОВО"
#   ASDA_FRAMESEAM2_DRYRUN=1 — сухой прогон; ASDA_FRAMESEAM2_ONLY=<WBP_a,WBP_b> — только эти окна.
import os
import sys
import traceback

HERE = os.path.dirname(os.path.abspath(__file__))
if HERE not in sys.path:
    sys.path.insert(0, HERE)

DRYRUN_ENV = "ASDA_FRAMESEAM2_DRYRUN"   # ключ сухого прогона (таблица DRY_ENV раннера пост-импорта)


def main(standalone=False):
    """Применить второй план механизмом шага 24.10."""
    import asda_ui_frame_seam_overlap as O
    O.PLAN_FILE = os.path.join(HERE, "data", "ui_frame_seam_more.json")
    O.DRYRUN_ENV = DRYRUN_ENV
    O.BACKUP_SET = "frame_seam_more"
    O.ONLY_ENV = "ASDA_FRAMESEAM2_ONLY"
    # ★ 09.11.2026 решение владельца (через координатора, дословно «Да, исправить»): швы в трёх окнах сцены логина — только эти
    #   три пакета и только при ASDA_ALLOW_LOGIN_WRITE=1 процесса (C++-сторож сохранения). Login.umap шаг не открывает.
    O.LOGIN_APPROVED = {"WBP_2nd_ID_Warnning_Win", "WBP_CharName_Change_Win", "WBP_SERVER_SELECT_WIN"}
    O.main(standalone=standalone)
    O.log("шов-рамки: ГОТОВО")


if __name__ == "__main__":
    try:
        main(standalone=True)
    except Exception:
        import unreal
        unreal.log_error("[шов-рамки-2] ИСКЛЮЧЕНИЕ:\n%s" % traceback.format_exc())
