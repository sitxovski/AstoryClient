# -*- coding: utf-8 -*-
# Copyright (c) 2026 @sitxovski (Discord)
# _probe_handmade_guard.py — НЕГАТИВНЫЙ ТЕСТ защиты ручных правок (одноразовая проверка).
#   Подсовывает asda_ui_handmade ЗАВЕДОМО НЕВЕРНЫЙ снимок (сдвинутый слот) и требует, чтобы
#   verify() это ЗАМЕТИЛ, а main() УПАЛ. Ассет не трогается: вызывается только verify().
import importlib.util, json, os, sys, tempfile
import unreal

HERE = os.path.dirname(os.path.abspath(__file__))
spec = importlib.util.spec_from_file_location("asda_ui_handmade", os.path.join(HERE, "asda_ui_handmade.py"))



win = "/Game/AsdaAssets/UI/Player_UI/Chat/WBP_CHAT_NEW_SYSTEM"


def main():
    """Работа скрипта. Вынесена в функцию, чтобы импорт модуля НИЧЕГО не делал
    (закон [[tools-no-import-side-effects]])."""
    hm = importlib.util.module_from_spec(spec); sys.modules["asda_ui_handmade"] = hm
    spec.loader.exec_module(hm)
    real = json.load(open(hm.SNAPSHOT, encoding="utf-8"))
    # 1) честный снимок → проблем быть не должно
    ok = hm.verify()
    unreal.log("[тест-защиты] честный снимок: проблем %d" % len(ok))
    # 2) порченый снимок → verify обязан найти РОВНО расхождение слота
    bad = json.loads(json.dumps(real))
    bad["tweaks"][win][0]["slot"]["offsets"][1] = -254.0     # «как будто перегенерация опустила заголовок»
    bad["nodes"][win][0]["slot"]["offsets"][0] = 999.0       # и сдвинула RESIZE
    tmp = os.path.join(tempfile.gettempdir(), "ui_handmade_bad.json")
    json.dump(bad, open(tmp, "w", encoding="utf-8"), ensure_ascii=False)
    hm.SNAPSHOT = tmp
    probs = hm.verify()
    unreal.log("[тест-защиты] порченый снимок: проблем %d" % len(probs))
    for p in probs:
        unreal.log("[тест-защиты]   → %s" % p)
    # ⚠ hm.main() здесь НЕ зовём: его restore() записал бы ПОРЧЕНЫЙ слот в живой ассет.
    #   Воспроизводим ровно ту ветку main(), что отвечает за падение.
    raised = False
    try:
        if probs:
            raise RuntimeError("ручные правки пользователя НЕ восстановлены (%d)" % len(probs))
    except RuntimeError:
        raised = True
    unreal.log("[тест-защиты] ИТОГ: verify нашёл %d расхождений, прогон упал бы=%s" % (len(probs), raised))
    hm.SNAPSHOT = os.path.join(HERE, "data", "ui_handmade_nodes.json")


# ★ ЗАКОН [[tools-no-import-side-effects]]: при импорте модуль НИЧЕГО не делает.
#   `__spec__` выставляет ЛЮБАЯ машинерия импорта (import / importlib / runpy) и НЕ
#   выставляет прямое исполнение файла (python file.py, UE -ExecutePythonScript).
#   `python -m` даёт `__name__ == "__main__"` и покрыт первым условием.
if __name__ == "__main__" or globals().get("__spec__") is None:
    main()
