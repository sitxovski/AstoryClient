# -*- coding: utf-8 -*-
# Copyright (c) 2026 @sitxovski (Discord)
# asda_fx_glow_layout_run.py — печатает состав категории Effect Glow командой asda.Fx.Glow.Layout.
#   Отдельный запускатель нужен потому, что -ExecCmds в РЕДАКТОРЕ этой сборки не исполняется вовсе
#   (замер 10.09.2026), а путём игрока (-game) консоль работает.
import os


def main():
    import unreal
    st = {"n": 0, "t": 0.0, "w": None, "done": False}

    def tick(dt):
        st["n"] += 1
        st["t"] += float(dt)
        if st["n"] == 1:
            for o in unreal.ObjectIterator(unreal.World):
                if o.get_name() not in ("Untitled", "None"):
                    st["w"] = o
            return
        if st["w"] is None:
            return
        if not st["done"] and st["t"] > 3.0:
            st["done"] = True
            unreal.SystemLibrary.execute_console_command(st["w"], "asda.Fx.Glow.Layout")
            unreal.log("[layout] команда отправлена")
        if st["done"] and st["t"] > 8.0:
            unreal.log("LAYOUT-RUN ЗАВЕРШЕНО")
            unreal.SystemLibrary.execute_console_command(st["w"], "quit")

    unreal.register_slate_post_tick_callback(tick)


# ★ Закон tools-no-import-side-effects (гигиена 15.09.2026): при ИМПОРТЕ main() не запускается.
#   Прямое исполнение (python file.py; UE -ExecutePythonScript / py / -run=pythonscript; exec с
#   __name__="__main__") — запускает: там __name__ == "__main__" и __spec__ отсутствует.
if __name__ == "__main__" or globals().get("__spec__") is None:
    main()
