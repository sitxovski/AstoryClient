# -*- coding: utf-8 -*-
# Copyright (c) 2026 @sitxovski (Discord)
# make_launcher_icon.py — иконка лаунчера БЕЗ белого фона (★ 17.10.2026, просьба владельца «аккуратно вырезать белый фон»).
#
# Исходник — арт владельца Data/OwnerArt/icon.png (не меняется). Вырезка фона и запись ICO — ОДНА точка с иконкой игры:
# AstoryClient/Tools/make_app_icon.py (cut_background + build_ico_bytes: 16…256, 256 — PNG, прочие — BMP32; даунскейл по
# предумноженной альфе). Поэтому res/app.ico лаунчера байт в байт равен Build/Windows/Application.ico игры.
#   → res/icon_round.png (вырезанный значок в полный размер, для справки и оформления)
#   → res/app.ico (launcher.rc: ICON "app.ico"; установщик берёт его же — DEPLOY/Installer/Branding/make_branding.py)
# После смены app.ico: `touch res/launcher.rc` (Ninja не видит зависимость .rc от .ico), пересобрать лаунчер, выпустить канал
# launcher (DEPLOY\Patch\5_Launcher_Release.bat), затем DEPLOY\Build\5_Collect_GAME.bat и 6_Installer_Build.bat.
# Запуск: python AstoryClient/Launcher/tools/make_launcher_icon.py [--check]
# При импорте ничего не делает (закон tools-no-import-side-effects).
import argparse
import os
import sys

HERE = os.path.dirname(os.path.abspath(__file__))                              # …\AstoryClient\Launcher\tools
LAUNCHER = os.path.dirname(HERE)                                                # …\AstoryClient\Launcher
PORT = os.path.dirname(os.path.dirname(LAUNCHER))                                                # …\port
SRC_PNG = os.path.join(PORT, "Data", "OwnerArt", "icon.png")                    # исходник владельца (только чтение)
OUT_PNG = os.path.join(LAUNCHER, "res", "icon_round.png")                       # вырезанный значок, полный размер
OUT_ICO = os.path.join(LAUNCHER, "res", "app.ico")                              # иконка лаунчера
APP_TOOLS = os.path.join(PORT, "AstoryClient", "Tools")                           # там make_app_icon.py (общая точка)


def load_mi():
    """Модуль make_app_icon (вырезка фона и запись ICO)."""
    if APP_TOOLS not in sys.path:
        sys.path.insert(0, APP_TOOLS)
    import make_app_icon
    return make_app_icon


def build(check_only=False, log=print):
    """Вырезать фон, записать icon_round.png и app.ico (байты ico — только если изменились). → код выхода."""
    from PIL import Image
    if not os.path.isfile(SRC_PNG):
        raise SystemExit("ОТКАЗ: нет исходника %s" % SRC_PNG)
    MI = load_mi()
    img, info = MI.cut_background(Image.open(SRC_PNG))
    log("исходник %s: %s" % (SRC_PNG, info))
    if check_only:
        return 0
    img.save(OUT_PNG, optimize=True)
    data = MI.build_ico_bytes(SRC_PNG)
    old = open(OUT_ICO, "rb").read() if os.path.isfile(OUT_ICO) else b""
    if data != old:
        with open(OUT_ICO, "wb") as f:
            f.write(data)
    ok, desc = MI.check_ico(OUT_ICO)
    log("иконка лаунчера: %s (%d байт, %s) — %s" % (OUT_ICO, len(data), "изменена" if data != old else "без изменений", desc))
    return 0 if ok else 1


def main(argv=None):
    """Точка входа командной строки."""
    ap = argparse.ArgumentParser(description="иконка лаунчера без белого фона из Data/OwnerArt/icon.png")
    ap.add_argument("--check", action="store_true", help="только замер, без записи")
    a = ap.parse_args(argv)
    return build(a.check)


if __name__ == "__main__":
    sys.exit(main())
