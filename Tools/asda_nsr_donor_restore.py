# -*- coding: utf-8 -*-
# Copyright (c) 2026 @sitxovski (Discord)
"""Возврат сценариев NPC (.NSR), которые в нашей сборке клиента (MINE = эталон) обрезаны до заглушки, из донорских клиентов.

Зачем (04.11.2026, questmark-chmella; распоряжение координатора «найти недостающие страницы во ВСЕХ источниках и перенести как
данные оригинала»). У Могилы лишённого гордости (tomb1, NPC 64) в нашем `Nsr.bsz` одна страница-заглушка (45 915 б), а ячейки
сервера (`QuestNpcTable.Bin`: принимает 127 и 128, выдаёт 129; ключи условий 340/343/344) ведут на страницы 10/11/12, которых нет.
Без страниц задания 127/128/129 у могилы не взять и не сдать: облачко гасится и ничего не рисует (`npcDialog_RenderPageByCode`).
Полный сценарий (266 КБ, 8 страниц, 5 записей) есть у ТРЁХ доноров с одинаковым каркасом страниц: Fantazium, ArabicASDAStory, XYT
(у XYT ещё 62 пустые записи-заглушки на несуществующие страницы — берём записи Fantazium = Arabic). Текст в донорах — корейский
оригинал (cp949); RU/EN — перевод в штатные ведомости (`data/ru_dt_overrides.json`, `data/loc/en/NpcNsrPage.json`).
Поиск по всем донорам и всем 139 записям/15 ячейкам без страниц: найден ТОЛЬКО tomb1 (`Reports/questmark_2026-11-04/REPORT.md`).

★ Условия приведены к СЕРВЕРУ (закон 29.10 «метка/вход ⇔ сервер выдаст», как `asda_nsr_script_align.py`): донорский клиент —
  вариант со сдвигом уровней (записи ур. 133, шаги ур. 140), а серверные условия ключей 340/343/344 — ур. 33…150. Правка уровня
  каждой записи/под-записи шага пишется в ведомость с `was` (донор) и `server_key`.

Что строит:
  Tools/data/nsr_donor_scripts.json — по сценарию: донор, md5 донорского .nsr, записи (ЗАМЕНЯЮТ записи сценария целиком —
      у заглушки запись #0 «ур.≥1 → стр. 1» перекрыла бы квестовые #9…#11) и страницы (ДОПИСЫВАЮТСЯ; занятый TalkId — стоп).
      Применяют `asda_import_npc_quest_data.read_scripts` и `asda_import_nsr_dialog.read_all`.
  + строки перевода (RU — `ru_dt_overrides.json`, EN — `loc/en/NpcNsrPage.json`), только недостающие ключи.
Запуск:  python asda_nsr_donor_restore.py --fetch [--write]     (--fetch — серверная ConditionTable по scp, только чтение)
"""
import hashlib
import json
import os
import sys
import zipfile

HERE = os.path.dirname(os.path.abspath(__file__))
if HERE not in sys.path:
    sys.path.insert(0, HERE)

OUT = os.path.join(HERE, "data", "nsr_donor_scripts.json")
RU_LEDGER = os.path.join(HERE, "data", "ru_dt_overrides.json")
EN_LEDGER = os.path.join(HERE, "data", "loc", "en", "NpcNsrPage.json")
DONORS = r"@@ASTORY_DEV_ROOT@@\Donors"
DT_PAGE = "/Game/AsdaAssets/Data/NPC/DT_NpcNsrPage"

# Сценарий → донор (путь от Donors) и сверочные доноры (каркас страниц и записи обязаны совпасть).
PLAN = {
    # Записи — Fantazium (= Arabic побайтно по полям; у XYT лишние пустые записи). Текст страниц — XYT: у Fantazium корейский
    #   текст этого файла утерян при перекодировке (одни «?»), у XYT он цел (cp949); каркас страниц у всех трёх одинаковый.
    "tomb1": dict(donor=r"Fantazium\data\NPC\Episode\NPC\Nsr.bsz",
                  pages_from=r"XYT\data\NPC\Episode\NPC\Nsr.bsz",
                  check=[r"ArabicASDAStory\data\NPC\Episode\NPC\Nsr.bsz"],
                  check_pages=[r"Fantazium\data\NPC\Episode\NPC\Nsr.bsz", r"ArabicASDAStory\data\NPC\Episode\NPC\Nsr.bsz"]),
}

# Перевод корейского оригинала (ключ — «страница.поле»). RU — в лад с уже переведёнными репликами Тиды (tid_6/tid_7:
#   «Моей любимой дочери Тиде», «Всхлипы на Дозорном Холме»); EN — с EN-ведомостью Тиды («To my loving daughter Tid»).
TR = {
    "10.Text": ("흐으으으으…. 흐으으으으….", "Хы-ы-ы-ы…. Хы-ы-ы-ы….", "Hhhuuuh…. Hhhuuuh…."),
    "10.Step0": ("전리품을 올려 놓는다.", "Положить трофеи на могилу.", "Place the spoils on the grave."),
    "11.Text": ("흐으으으으….. 흐으으으으….", "Хы-ы-ы-ы….. Хы-ы-ы-ы….", "Hhhuuuh….. Hhhuuuh…."),
    "11.Step0": ("무덤에서 흐느끼는 소리가 들린다", "Из могилы доносятся всхлипы", "Sobbing can be heard from the grave"),
    "12.Text": ("무덤 위로 빛 바랜 편지 하나가 눈에 띈다.", "На могиле виднеется выцветшее письмо.",
                "A faded letter lying on the grave catches your eye."),
    "12.Step0": ("편지를 줍는다.", "Поднять письмо.", "Pick up the letter."),
    "24.Text": ("울음소리에서 왠지 모를 원혼이 느껴진다.", "В этом плаче чувствуется неупокоенная душа.",
                "Somehow you sense a restless spirit in the weeping."),
    "24.Step0": ("끝내기", "Конец", "End"),
    "25.Text": ("전리품을 올려놓자 울음 소리는 멈추고 싸늘한 바람만이 무덤 주위를 맴돈다.",
                "Стоило положить трофеи, как плач стих, и лишь холодный ветер кружит вокруг могилы.",
                "As you place the spoils, the weeping stops and only a chill wind circles the grave."),
    "25.Step0": ("퀘스트 완료", "Задание выполнено", "Quest Complete"),
    "26.Text": ("편지에는 ‘사랑스러운 딸 티드에게’라고 적혀 있다.", "На письме написано: «Моей любимой дочери Тиде».",
                "The letter reads: 'To my loving daughter Tid'."),
    "26.Step0": ("끝내기", "Конец", "End"),
    "63.Text": ("죽을 때를 놓친 어리석은 자여.",
                "Глупец, упустивший час своей смерти.\nЗа спиной у тебя, уцелевшего,\nлишь крики страсти, гордости и товарищей.\n\n"
                "Отчего ты не знал, что в этом мире есть жизнь, ползущая во тьме,\nи смерть, сияющая, словно свет?\n"
                "Тебе, утратившему гордость,\nдаже могила — слишком большая честь.\n",
                "O fool who missed the hour of his death.\nBehind your back, you who survived,\nthere are only the screams of passion, "
                "pride and comrades.\n\nWhy did you not know that this world holds a life that crawls through darkness\n"
                "and a death as radiant as light?\nFor you who have lost your pride,\neven a grave is too good.\n"),
    "63.Step0": ("다음", "Далее", "Next"),
    "64.Text": ("오래된 무덤이 있다. 무덤에는 빛 바랜 글씨가 씌어져 있다.", "Здесь старая могила. На ней выцветшая надпись.",
                "There is an old grave. Faded writing is carved on it."),
    "64.Step0": ("끝내기", "Конец", "End"),
}


def log(m):
    print("[nsr-donor] %s" % m)


def nsr_blob(zpath, nsr):
    zf = zipfile.ZipFile(zpath)
    for n in zf.namelist():
        if os.path.basename(n).lower() == nsr + ".nsr":
            return zf.read(n)
    raise SystemExit("нет %s.nsr в %s" % (nsr, zpath))


def skeleton(pages):
    return [(p["TalkId"], [[(s["State"], s.get("AcceptQuestId"), s.get("BranchTalkId"), s.get("Res0"), s.get("Res1"), s.get("Res2"))
                            for s in st["Subs"]] for st in p["Steps"]]) for p in sorted(pages, key=lambda p: p["TalkId"])]


def build(server_cond_fn):
    import asda_import_nsr_dialog as D
    import asda_import_npc_quest_data as Q
    base = zipfile.ZipFile(D.NSR_ZIP)
    rows = []
    tr_ru, tr_en = [], []
    for nsr, plan in PLAN.items():
        blob = nsr_blob(os.path.join(DONORS, plan["donor"]), nsr)
        pblob = nsr_blob(os.path.join(DONORS, plan["pages_from"]), nsr)
        _, _, pages = D.parse_nsr(nsr, pblob, "cp949")
        recs = [r for r in Q.parse_nsr(blob) if Q.rec_is_useful(r)]
        for c in plan["check"]:
            b2 = nsr_blob(os.path.join(DONORS, c), nsr)
            r2 = [r for r in Q.parse_nsr(b2) if Q.rec_is_useful(r)]
            if r2 != recs:
                raise SystemExit("записи %s у %s расходятся с донором" % (nsr, c))
        for c in plan["check_pages"]:
            if skeleton(D.parse_nsr(nsr, nsr_blob(os.path.join(DONORS, c), nsr), "cp949")[2]) != skeleton(pages):
                raise SystemExit("каркас страниц %s у %s расходится с донором" % (nsr, c))
        have = {p["TalkId"] for p in D.parse_nsr(nsr, nsr_blob(D.NSR_ZIP, nsr))[2]}
        pages = [p for p in pages if p["TalkId"] not in have]

        aligned = []
        # Шаги: уровень под-записи с ключом сервера (State = ключ ConditionTable) → уровень сервера.
        key_of = {}
        for p in pages:
            for si, st in enumerate(p["Steps"]):
                for ui, su in enumerate(st["Subs"]):
                    sc = server_cond_fn(su["State"]) if su["State"] > 100 else None
                    if not sc:
                        continue
                    key_of[(su["Cond"]["QuestActive"], su["Cond"]["QuestFreeA"], su["Cond"]["QuestStage"])] = su["State"]
                    for f in ("LevelMin", "LevelMax"):
                        if su["Cond"][f] != sc[f]:
                            aligned.append(dict(where="page %d step %d sub %d" % (p["TalkId"], si, ui), field=f,
                                                was=su["Cond"][f], now=sc[f], server_key=su["State"]))
                            su["Cond"][f] = sc[f]
        # Записи: та же пара условий, что у шага с серверным ключом (127/0 ↔ 340, 128/1 ↔ 343, 129 свободен ↔ 344).
        for r in recs:
            k = None
            for (qa, qf, qs), key in key_of.items():
                if (r["QuestActive"] >= 0 and r["QuestActive"] == qa and r["QuestStage"] == qs) or \
                   (r["QuestFreeA"] >= 0 and r["QuestFreeA"] == qf):
                    k = key
            sc = server_cond_fn(k) if k else None
            if not sc:
                continue
            for f in ("LevelMin", "LevelMax"):
                if r[f] != sc[f]:
                    aligned.append(dict(where="record #%d" % r["RecIndex"], field=f, was=r[f], now=sc[f], server_key=k))
                    r[f] = sc[f]
        for p in pages:
            p["NsrName"] = nsr
            p["Name"] = "%s_%d" % (nsr, p["TalkId"])
        for r in recs:
            r["NsrName"] = nsr
        rows.append(dict(nsr=nsr, donor=plan["donor"], donor_md5=hashlib.md5(blob).hexdigest(),
                         pages_from=plan["pages_from"], pages_md5=hashlib.md5(pblob).hexdigest(),
                         same_in=plan["check"] + plan["check_pages"], replace_records=True,
                         records=recs, pages=pages, aligned=aligned))
        log("%s: записей %d, страниц %d (дописываются), правок уровня по серверу %d" % (nsr, len(recs), len(pages), len(aligned)))

        # Перевод: ключи и «was» — из разобранного донора (текст страницы целиком, подпись шага 0).
        for p in pages:
            for fld in ("Text", "Step0"):
                ko, ru, en = TR["%d.%s" % (p["TalkId"], fld)]
                src = p["Text"] if fld == "Text" else p["Steps"][0]["Caption"]
                if not src.startswith(ko):
                    raise SystemExit("%s_%d.%s: в доноре %r, перевод ждёт %r" % (nsr, p["TalkId"], fld, src[:40], ko))
                if fld == "Text":
                    tr_ru.append(dict(row=p["Name"], field="Text", index=None, sub=None, was=src, now=ru, dt=DT_PAGE))
                    tr_en.append(dict(key="%s.Text" % p["Name"], value=en, was=None,
                                      why="04.11.2026 questmark-chmella: страница из донора (корейский оригинал), перевод", source="manual"))
                else:
                    tr_ru.append(dict(row=p["Name"], field="Steps", index=0, sub="Caption", was=src, now=ru, dt=DT_PAGE))
                    tr_en.append(dict(key="%s.Steps.0.Caption" % p["Name"], value=en, was=None,
                                      why="04.11.2026 questmark-chmella: страница из донора (корейский оригинал), перевод", source="manual"))
    return rows, tr_ru, tr_en


def dump(path, obj):
    raw = open(path, "rb").read() if os.path.isfile(path) else b""
    crlf = b"\r\n" in raw
    s = json.dumps(obj, ensure_ascii=False, indent=1)
    if crlf:
        s = s.replace("\n", "\r\n")
    open(path, "wb").write(s.encode("utf-8"))


def main():
    import argparse
    ap = argparse.ArgumentParser()
    ap.add_argument("--fetch", action="store_true", help="снять ConditionTable.Bin с dev (scp, только чтение)")
    ap.add_argument("--server-dir", default="", help="папка с ConditionTable.Bin вместо --fetch")
    ap.add_argument("--write", action="store_true", help="записать ведомости (иначе только отчёт)")
    a = ap.parse_args()
    sys.stdout.reconfigure(encoding="utf-8")
    import tempfile
    import asda_nsr_script_align as A
    sdir = a.server_dir
    if a.fetch:
        import asda_snq_server_sync as S
        sdir = tempfile.mkdtemp(prefix="nsr_donor_")
        S.fetch_server(sdir)
    if not sdir:
        raise SystemExit("нужен --fetch или --server-dir")
    srv = open(os.path.join(sdir, "ConditionTable.Bin"), "rb").read()
    rows, tr_ru, tr_en = build(lambda k: A.server_cond(srv, k) if k else None)
    for r in rows:
        for x in r["aligned"]:
            log("  выравнивание %s %s: %s → %s (ключ сервера %s)" % (x["where"], x["field"], x["was"], x["now"], x["server_key"]))
    if not a.write:
        log("сухой прогон: ведомости не записаны (--write)")
        return
    dump(OUT, {"_comment": "Сценарии .NSR из донорских клиентов вместо заглушек нашей сборки. Строит ТОЛЬКО "
                           "Tools/asda_nsr_donor_restore.py (разбор — в его шапке). records ЗАМЕНЯЮТ записи сценария, "
                           "pages ДОПИСЫВАЮТСЯ; aligned — правки уровня по серверной ConditionTable (was = донор).",
               "rows": rows})
    ru = json.load(open(RU_LEDGER, encoding="utf-8"))
    have = {(e["dt"], e["row"], e["field"], e.get("index"), e.get("sub")) for e in ru["rows"]}
    add = [e for e in tr_ru if (e["dt"], e["row"], e["field"], e["index"], e["sub"]) not in have]
    ru["rows"].extend(add)
    dump(RU_LEDGER, ru)
    en = json.load(open(EN_LEDGER, encoding="utf-8"))
    have = {e["key"] for e in en["rows"]}
    add_en = [e for e in tr_en if e["key"] not in have]
    en["rows"].extend(add_en)
    dump(EN_LEDGER, en)
    log("записано: %s; RU +%d; EN +%d" % (OUT, len(add), len(add_en)))


if __name__ == "__main__":
    main()
