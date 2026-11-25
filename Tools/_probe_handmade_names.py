# -*- coding: utf-8 -*-
# Copyright (c) 2026 @sitxovski (Discord)
# _probe_handmade_names.py — проверка сторожей `HANDMADE` (ничего не правит).
#   Зовёт ту же `check_handmade`, что и пост-импорт: если хоть один сторож потерян — падаем.
import importlib.util, os, sys
import unreal

HERE = os.path.dirname(os.path.abspath(__file__))


def main():
    spec = importlib.util.spec_from_file_location("asda_ui_post_import",
                                                  os.path.join(HERE, "asda_ui_post_import.py"))
    P = importlib.util.module_from_spec(spec)
    sys.modules["asda_ui_post_import"] = P
    spec.loader.exec_module(P)
    bad = P.check_handmade()
    n = sum(len(v) for v in P.HANDMADE.values())
    unreal.log("[сторожа] проверено имён %d в %d окнах, потеряно %s"
               % (n, len(P.HANDMADE), bad))
    if bad:
        raise RuntimeError("сторожа HANDMADE потеряны: %s" % bad)
    unreal.log("[сторожа] ВСЕ НА МЕСТЕ")


if __name__ == "__main__":
    main()
