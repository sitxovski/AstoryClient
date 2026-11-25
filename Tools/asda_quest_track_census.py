# -*- coding: utf-8 -*-
# Copyright (c) 2026 @sitxovski (Discord)
"""asda_quest_track_census.py — ПЕРЕПИСЬ ЗАДАНИЙ по тому, что экранный трекер вообще может
показать. Отвечает на вопрос владельца «сколько заданий из 442 попадает в каждую категорию».

★ ЗАЧЕМ ОТДЕЛЬНЫЙ СКРИПТ. Ответ нужен ЧИСЛАМИ и воспроизводимо: правило «галка недоступна»
  живёт в коде (`UAsdaQuestLogSubsystem::CanTrackSlot`), но проверить его на всех 442 книгах
  в игре нельзя — журнал держит 12 ячеек. Здесь то же правило считается по ТЕМ ЖЕ таблицам,
  из которых порт строит свои `DataTable`, — второго источника данных не заводим.

КАТЕГОРИИ (ровно те, что различает `RebuildTrackerEntry`):
  A — есть строки требований: под-запись шага `.QST` (`StepSmInfoIds[стадия]`) указывает на
      запись `SMInfo` хотя бы с одним `ReqItemIds[i] > 0` ⇒ в трекере будут строки
      «предмет : есть/надо». Эталон берёт такое задание, порт тоже.
  B — строк нет, но у пары (QuestNo, стадия) есть строка `QDT` с координатами (X/Y != −1) ⇒
      эталон отказывает молча, ПОРТ берёт: заголовок + расстояние + стрелка
      (отступление №3, ключ отката `Asda.Quest.TrackerNoItemQuests 0`).
  C — ни того, ни другого ⇒ показывать нечего, галка 743 НЕДОСТУПНА
      (отступление №5, ключ отката `Asda.Quest.TrackerLockUselessCheckbox 0`).

ЗАПУСК: python Tools\asda_quest_track_census.py
ИСТОЧНИКИ (RawAssets, те же файлы идут в DataTable):
  Data/Quest/QuestBook.json, SmInfo.json, QuestGuide.json, Data/NPC/EpisodeTable.json
"""
import collections
import json
import os

RAW = os.path.join(os.path.dirname(os.path.abspath(__file__)), "..", "RawAssets")

# Строк условий на запись — столько же контролов `Quest_State_Condition{i}_{1..3}` в окне 1800.
MAX_LINES = 3


def load(*parts):
    with open(os.path.join(RAW, *parts), encoding="utf-8-sig") as f:
        return json.load(f)


def lines_for(sm_by_id, sm_id):
    """Сколько строк «предмет : есть/надо» напишет построитель для этой под-записи шага."""
    if sm_id is None or sm_id < 0:
        return 0
    rec = sm_by_id.get(sm_id)
    if not rec:            # эталон: `findRecordById_C10` не нашёл запись ⇒ выход нулём
        return 0
    return min(MAX_LINES, sum(1 for code in rec["ReqItemIds"] if code > 0))


def main():
    books = load("Data", "Quest", "QuestBook.json")
    sminfo = load("Data", "Quest", "SmInfo.json")
    guide = load("Data", "Quest", "QuestGuide.json")
    episodes = load("Data", "NPC", "EpisodeTable.json")

    sm_by_id = {r["RecId"]: r for r in sminfo}

    # книга (.QST) -> номера заданий: `Episode_Table.QuestKey` и есть `QuestNo` пакета 5047
    book_keys = collections.defaultdict(list)
    for e in episodes:
        book_keys[e["QstFile"]].append(e["QuestKey"])

    # (QuestNo, стадия) -> есть ли координаты цели; и все стадии, упомянутые в QDT
    coords = set()
    guide_stages = collections.defaultdict(set)
    for g in guide:
        guide_stages[g["QuestNo"]].add(g["Stage"])
        if g["X"] != -1 and g["Y"] != -1:
            coords.add((g["QuestNo"], g["Stage"]))

    by_book = collections.Counter()
    by_stage = collections.Counter()
    orphans = []
    examples = collections.defaultdict(list)

    for b in books:
        keys = book_keys.get(b["FileName"], [])
        if not keys:
            orphans.append(b["FileName"])
        ids = b["StepSmInfoIds"]
        steps = b["Steps"]
        # Стадии, которые у книги реально существуют: непустой текст шага, либо ссылка на
        #   SmInfo, либо строка QDT. Пустая книга считается одностадийной.
        stages = set()
        for i in range(len(ids)):
            if (i < len(steps) and steps[i].strip()) or ids[i] != -1:
                stages.add(i)
        for k in keys:
            stages |= guide_stages.get(k, set())
        if not stages:
            stages = {0}

        has_lines = has_coord = False
        for st in sorted(stages):
            n = lines_for(sm_by_id, ids[st]) if st < len(ids) else 0
            c = any((k, st) in coords for k in keys)
            has_lines |= n > 0
            has_coord |= c
            by_stage["A" if n > 0 else ("B" if c else "C")] += 1

        cat = "A" if has_lines else ("B" if has_coord else "C")
        by_book[cat] += 1
        if len(examples[cat]) < 10:
            examples[cat].append("%s (задание %s)" % (b["FileName"], keys or "нет эпизода"))

    total = len(books)
    names = {
        "A": "есть строки требований (SmInfo с ReqItemId > 0)",
        "B": "строк нет, но есть координаты цели в QDT",
        "C": "ни строк, ни координат — галка 743 НЕДОСТУПНА",
    }
    print("КНИГ ЗАДАНИЙ ВСЕГО: %d" % total)
    for c in "ABC":
        print("  %s. %-52s %4d  (%.1f %%)" % (c, names[c], by_book[c], 100.0 * by_book[c] / total))
    print("  контроль: книг без единой ссылки на SmInfo = %d" % (total - by_book["A"]))
    print()

    tot_st = sum(by_stage.values())
    print("ПО ПАРАМ (книга, стадия) — что даст трекер, когда задание стоит на этой стадии: %d пар" % tot_st)
    for c in "ABC":
        print("  %s. %-52s %4d  (%.1f %%)" % (c, names[c], by_stage[c], 100.0 * by_stage[c] / tot_st))
    print()

    print("Книг, на которые не ссылается ни один эпизод Episode_Table (в журнал попасть не могут): %d"
          % len(orphans))
    if orphans:
        print("  " + ", ".join(orphans))
    print()
    for c in "ABC":
        print("Примеры категории %s: %s" % (c, "; ".join(examples[c])))


# ★ ЗАКОН tools-no-import-side-effects: при импорте модуль ничего не делает.
if __name__ == "__main__":
    main()
