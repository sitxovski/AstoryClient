# -*- coding: utf-8 -*-
# Copyright (c) 2026 @sitxovski (Discord)
r"""
translate_dialogues.py — перевод оставшихся реплик NPC и строк UI.

РЕПЛИКИ (Data\NPC\Episode\Talk\*.BIN, stride 208, текст с +5).
406 непереведённых реплик — это всего 10 уникальных строк, размноженных по вариантам ответов.
Имена и терминология взяты из npc_list.BIN, где эти же NPC уже названы по-русски:
    110 Обелиск концентрации   111 Древо красного света   112 Темный мозран
    113 Темные живые камни      114 Трус Монтенегро        115 Знак черепа
    4  Торговец Джон            5  Продавец живых камней Виана
    18 Воительница Джоэль       79 Отступник Ротон
Отсюда: Mosran = мозран, Montentro = Монтенегро, sowel = живой камень (как в sowell_list),
devils = дьяволы (форма из реплик других NPC).

СТРОКИ UI (system\error list.bin, запись 256 Б, шифр «байт+100», заполнитель 0x64).
Переводится только надпись кнопки OK. Остальное из отчёта НЕ переводится осознанно:
чат-команды (/all, /party, /A, /P …) — это вводимые игроком команды, перевод их сломает;
GXP, MIC, F1 ~ F5, TBD, [sd1], MipFilter — технические метки и плейсхолдеры;
строки про HackShield и «Да (Insert)/Нет (Delete)» уже русские.
"""
import os, io, sys, struct, glob, shutil

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from langdet import lang

CLIENT = r"@@ASTORY_DEV_ROOT@@\Donors\AsdaChroniclesInterlude_MINE"
BACKUP = r"@@ASTORY_DEV_ROOT@@\AstoryClient\Tools\Translate\out_client"
REPORT = r"@@ASTORY_DEV_ROOT@@\AstoryClient\Tools\Translate\reports\translate_dialogues.txt"

DIALOGUES = {
    "There is an Obelisk":
        "Здесь Обелиск концентрации.",
    "There is a tree emitting a red light":
        "Здесь древо, излучающее красный свет.",
    "There is a dark Mosran":
        "Здесь темный мозран.",
    "There is a dreadful sowel with a black aura.":
        "Здесь ужасный живой камень с черной аурой.",
    "I'm Montentro. People think I'm a coward but they just don't know me ":
        "Я Монтенегро. Меня считают трусом, но обо мне просто ничего не знают.",
    "Hey, this is a dangerous place. I suggest that you avoid coming this way..ahahahaha":
        "Эй, это опасное место. Советую тебе здесь не ходить... ахахаха",
    "Welcome. The devils are increasing rapidly, aren't they?":
        "Добро пожаловать. Дьяволы размножаются всё быстрее, не правда ли?",
    "Hello.":
        "Здравствуй.",
    "Welcome. Take a good look.":
        "Добро пожаловать. Посмотри хорошенько.",
    "You have something to do with me? I don't think so. I don't have no business with a little kid like you.":
        "У тебя ко мне дело? Не думаю. С малявками вроде тебя мне говорить не о чем.",
}

UI_STRINGS = {"OK": "ОК"}

def avail(b, off, limit):
    end = off
    while end < off + limit and b[end] != 0: end += 1
    z = end
    while z < off + limit and b[z] == 0: z += 1
    return (end - off) + (z - end)

def put(b, off, limit, text):
    if len(text) + 1 > avail(b, off, limit):
        return False
    b[off:off + len(text)] = text
    b[off + len(text)] = 0
    return True

def do_talk(log, skip):
    tdir = os.path.join(CLIENT, "Data", "NPC", "Episode", "Talk")
    total = 0
    for path in sorted(glob.glob(os.path.join(tdir, "*.BIN")) + glob.glob(os.path.join(tdir, "*.bin"))):
        d = bytearray(open(path, "rb").read())
        if len(d) < 20: continue
        orig = bytes(d)
        cnt = struct.unpack_from("<I", d, 4)[0]
        base, stride, toff = 20, 208, 5
        limit = stride - toff
        n = 0
        for i in range(cnt):
            o = base + i * stride
            if o + stride > len(d): break
            raw = bytes(d[o + toff:o + stride]); z = raw.find(b"\x00")
            cur = raw[:z if z >= 0 else limit]
            ru = DIALOGUES.get(cur.decode("cp1251", "replace"))
            if ru is None: continue
            rec = bytearray(d[o:o + stride])
            if not put(rec, toff, limit, ru.encode("cp1251")):
                skip.append("%s\t%d\tне влезает\t%s" % (os.path.basename(path), i, ru)); continue
            d[o:o + stride] = rec
            n += 1
        if n:
            # служебные 5 байт каждой записи и структура файла не должны меняться
            for i in range(cnt):
                o = base + i * stride
                if o + stride > len(d): break
                if orig[o:o + toff] != bytes(d[o:o + toff]):
                    raise AssertionError("%s: изменены служебные байты записи %d" % (path, i))
            assert len(d) == len(orig), "изменился размер файла"
            rel = os.path.relpath(path, CLIENT)
            bak = os.path.join(BACKUP, rel)
            os.makedirs(os.path.dirname(bak), exist_ok=True)
            if not os.path.isfile(bak): shutil.copy2(path, bak)
            open(path, "wb").write(bytes(d))
            log.append("%s\tреплик заменено: %d" % (os.path.basename(path), n))
            total += n
    return total

def do_error_list(log, skip):
    p = os.path.join(CLIENT, "system", "error list.bin")
    d = bytearray(open(p, "rb").read())
    orig = bytes(d)
    cnt = struct.unpack_from("<I", d, 0)[0]
    n = 0
    for i in range(cnt):
        o = 4 + 256 * i
        plain = bytes(((b - 100) & 0xFF) for b in d[o:o + 256])
        z = plain.find(b"\x00")
        cur = plain[:z if z >= 0 else 256].decode("cp1251", "replace")
        ru = UI_STRINGS.get(cur)
        if ru is None: continue
        t = ru.encode("cp1251")
        if len(t) + 1 > 256:
            skip.append("error list\t%d\tне влезает\t%s" % (i, ru)); continue
        # шифр: байт+100; заполнитель 0x64 (после дешифровки даёт 0x00)
        d[o:o + 256] = bytes(((c + 100) & 0xFF) for c in t) + b"\x64" * (256 - len(t))
        n += 1
    if n:
        assert len(d) == len(orig), "изменился размер error list.bin"
        # массив ID (идёт после строк) обязан остаться байт-в-байт
        idoff = 4 + 256 * cnt
        assert orig[idoff:] == bytes(d[idoff:]), "повреждён массив ID"
        bak = os.path.join(BACKUP, "system", "error list.bin")
        os.makedirs(os.path.dirname(bak), exist_ok=True)
        if not os.path.isfile(bak): shutil.copy2(p, bak)
        open(p, "wb").write(bytes(d))
        log.append("error list.bin\tстрок заменено: %d (OK -> ОК)" % n)
    return n

def main():
    log, skip = [], []
    a = do_talk(log, skip)
    b = do_error_list(log, skip)
    print("реплик переведено: %d | строк UI: %d" % (a, b))
    for l in log: print("   " + l)
    os.makedirs(os.path.dirname(REPORT), exist_ok=True)
    with io.open(REPORT, "w", encoding="utf-8-sig") as f:
        f.write("# Перевод реплик и строк UI\n")
        f.write("\n".join(log))
        if skip: f.write("\n\n# Пропущено\n" + "\n".join(skip))
    print("\nпропущено: %d | отчёт: %s" % (len(skip), REPORT))

if __name__ == "__main__":
    main()
