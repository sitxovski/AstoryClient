# Copyright (c) 2026 @sitxovski (Discord)
# asda_dump_skill_dispatch.py — снятие ТАБЛИЦЫ ДИСПЕТЧЕРА НАВЫКОВ из бинаря клиента.
#
# ЗАКРЫВАЕТ «дыру №1» отчёта `Reports/skill_use_system.md` §9:
#   «Полный список хардкод-загрузчиков клипов навыков (CharSkillEffect_InitDispatchTable @0xA89510)».
#
# ЧТО ДЕЛАЕТ (каждый факт — из бинаря/данных, ничего не выдумывается):
#   1. Разбирает тело `CharSkillEffect_InitDispatchTable @0xA89510` в `client\system\AsdaStory.exe`
#      (эталонная сборка 13 344 768 б) — это цепочка `mov eax,[ebp-4]; mov [eax+disp32], imm32`.
#   2. Переводит disp32 в пару (номер массива колбэков, код навыка):
#         элемент i массива k лежит по `8 + 5420*k + 4*i`   (k = 0..6, i = 0..1354)
#      Базы подтверждены `structs_ru.h` (skillCallback0..6 @ 0x8/0x1534/0x2A60/0x3F8C/0x54B8/0x69E4/0x7F10)
#      и циклом инициализации `CSkillMgr_LoadSkillData` (skill_buff.c:17244-17253, idx < 1355).
#   3. Раскручивает j_-переходники (`jmp rel32`) до настоящих функций и берёт ИМЕНА ИЗ ЭТАЛОНА
#      (`RE/etalon/*.c`, заголовки `//---- Имя @ 0xАДРЕС ----`), fallback — `RE/ida/export/functions.tsv`.
#   4. Из тела КАЖДОГО загрузчика (массив 0) вытаскивает по эталонному декомпилу:
#        · имена клипов `String_Append(&path, "<клип>.JBB")`   (клип грузится из Data\Character\Motion\)
#        · ключи комбо-таблицы `CComboTable_FindByName(mgr, "<клип>")`
#        · условие ветки: пол (из самого имени `ch4_m_` / `ch4_f_`) и `case N` по `equippedWeaponType`
#        · звуки `S_c_*.wav`, эффекты `*.jeb`
#   5. Джойнит с `combo time.bin` (COMBO0..2 = кадры удара, ×1000/30 → мс; полная длительность фазы)
#      и с `skill_info.sif` (имя навыка, +0x02, stride 273).
#   6. ВТОРОЙ ИСТОЧНИК КЛИПА — `system\SkillInfo.inl` (см. `load_skillinfo_inl`): 38 навыков идут
#      через ОДИН общий загрузчик `charLoadSkillEffectMotion@0xA75590`, у которого имени клипа в теле
#      нет — он берёт его из записи, разобранной из .inl (`CSkillMgr_LoadSkillInfoInl`, skill_buff.c:16405).
#
# ЧЕГО В ТАБЛИЦАХ НЕТ И ПОЧЕМУ (закон dead-code-markings-suspect — помечаем, а не выдумываем):
#   · Навык 400 («Нет» в `skill_info.sif`) — СЛОТ-ЗАГЛУШКА оригинала: на него приходится 122 из 1349
#     регистраций (33 загрузчика, 33 тика, …), т.е. разработчики сваливали туда всё, что не вышло в
#     релиз, и в рантайме выживает одна случайная последняя. В таблицы клипов/инвентаря он НЕ идёт.
#   · Всего 1349 записей дают 1218 ЖИВЫХ слотов `(массив, индекс)`; 131 запись — мёртвая перезапись
#     («побеждает последняя»). Колонка DeadWrites в `skill_dispatch_table.tsv` считает их поимённо.
#
# ВЫХОД (только файлы отчёта/данных, ничего в проекте не трогается):
#   Reports/skill_dispatch_table.tsv   — навык → загрузчик/тик/остальные 5 массивов
#   Reports/skill_clip_table.tsv       — навык → клип → COMBO0/1/2 → длительность → звук → .jeb
#   AstoryClient/RawAssets/Data/SkillClips.csv — то же в виде DataTable-CSV для UE
#
# Запуск: python Tools\asda_dump_skill_dispatch.py     (обычный python, UE НЕ нужен)
# ★ ЗАКОН tools-no-import-side-effects: при импорте модуль НИЧЕГО не делает — только main().
import json
import os
import re
import struct
import sys

# ---------------------------------------------------------------- пути
ROOT = os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__))))   # <ROOT> (Tools → проект → ROOT)
EXE = os.path.join(ROOT, "Donors", "client_system", "system", "AsdaStory.exe")
ETALON = os.path.join(ROOT, "RE", "etalon")
FUNCS_TSV = os.path.join(ROOT, "RE", "ida", "export", "functions.tsv")
COMBO_BIN = os.path.join(ROOT, "Donors", "TRANSLATE_unpacked", "tables_orig", "Data", "Character", "combo time.bin")
SIF = os.path.join(ROOT, "AstoryClient", "Content", "AsdaAssets", "Data", "Character", "skill_info.sif")
# ★ Второй источник клипа/эффектов — текстовый `SkillInfo.inl` (см. load_skillinfo_inl).
#   Файл ОДИНАКОВ во всех клиентах (md5 2aef727733c85fd889c0a55be97908a3), берём эталонный.
INL = os.path.join(ROOT, "Donors", "client_system", "system", "SkillInfo.inl")
REPORTS = os.path.join(ROOT, "Reports")
OUT_DISPATCH = os.path.join(REPORTS, "skill_dispatch_table.tsv")
OUT_CLIPS = os.path.join(REPORTS, "skill_clip_table.tsv")
OUT_CSV = os.path.join(ROOT, "AstoryClient", "RawAssets", "Data", "SkillClips.csv")

# ---------------------------------------------------------------- константы бинаря
EXE_SIZE = 13344768          # эталонная сборка (idb-build-differs-from-live)
FN_DISPATCH = 0xA89510       # CharSkillEffect_InitDispatchTable
# ★★★ ЧЕТВЁРТЫЙ САЙТ РЕГИСТРАЦИИ (навыки СОУЛМЕЙТА) — найден перепроверкой 2026-08-11.
#   `charFxInitSoulSkillVtbl@0xACEE20` (`effects_particles.c:28368`) вызывается из
#   `CSkillMgr_LoadSkillData@0xA79F30` СТРОКОЙ СРАЗУ ПОСЛЕ `CharSkillEffect_InitDispatchTable`
#   (`skill_buff.c:17254-17255`) и пишет в ТЕ ЖЕ семь массивов колбэков:
#     `this->gap_C[N]`    = this + 12 + N  ⇒ массив k0 (загрузчик клипа)
#     `this->gap_69E8[N]` = this + 0x69E8 + N = 8 + 5420·5 + 4 + N ⇒ массив k5 (тик анимации)
#   12 + 12 = 24 регистрации, коды навыков **1287…1300** (1292 и 1297 не заняты).
#   Форма тела та же (`mov eax,[ebp-4]; mov [eax+disp32], imm32`), проверено по байтам
#   файл. офс. 0x6C3E20: `8b 45 fc c7 80 30 14 00 00 …`.
#   Ремап «индекс умения соулмейта → код навыка» — `sub_ACEFC0@0xACEFC0`:
#     a1 < 30 → a1;  a1 >= 100 → 0;  иначе a1 + 1256 (и 0, если >= 0x54C).
FN_SOUL_DISPATCH = 0xACEE20  # charFxInitSoulSkillVtbl (навыки соулмейта, id 1287..1300)
CB_BASE = 0x8                # смещение skillCallback0 в CSkillMgr
CB_STRIDE = 5420             # 1355 элементов × 4 б — шаг между массивами колбэков
CB_COUNT = 1355              # цикл инициализации: for (idx = 0; idx < 1355; ++idx)
CB_ARRAYS = 7                # skillCallback0..skillCallback6

# Роли массивов — по ФУНКЦИЯМ-ДИСПЕТЧЕРАМ, которые их читают (эталон):
#   0  packPathDispatchEntryHandler @0xA81000   `v14[skillId + 2]`      — ЗАГРУЗКА клипа+эффектов каста
#   1  sub_A811A0                  @0xA811A0   `this[skillId + 1357]`  — вызов по позиции (x, y+20, z)
#   2  sub_A85010                  @0xA85010   `this[skillId + 2712]`
#   3  sub_A85530                  @0xA85530   `this[skillId + 4067]`
#   4  sub_A85A20                  @0xA85A20   `this[skillId + 5422]`
#   5  sub_A86660                  @0xA86660   `this[skillId + 6777]`  — ТИК анимации/фаз навыка
#   6  sub_A86700                  @0xA86700   `v6[skillId + 8132]`    — из charSkillResetEffectSlotsAndDispatch
CB_ROLE = {
    0: "loader",     # загрузчик клипа/эффектов каста
    1: "atpos",      # колбэк по мировой позиции персонажа
    2: "cb2",
    3: "cb3",
    4: "cb4",
    5: "tick",       # покадровый тик навыка
    6: "cb6",
}
CB_DISPATCHER = {
    0: "packPathDispatchEntryHandler@0xA81000",
    1: "sub_A811A0@0xA811A0",
    2: "sub_A85010@0xA85010",
    3: "sub_A85530@0xA85530",
    4: "sub_A85A20@0xA85A20",
    5: "sub_A86660@0xA86660",
    6: "sub_A86700@0xA86700",
}

FRAME_MS = 1000.0 / 30.0     # CComboManager_LoadComboTime: все значения ×33.333328
# Карта «клип → anim_NN» от экспортёра клипов (см. Tools/export_char_skillclips.py). Если её нет,
# колонка AnimIdx будет −1: таблица всё равно валидна, просто ассеты ещё не собраны.
CLIP_MAP = os.path.join(os.path.dirname(os.path.abspath(__file__)), "..", "RawAssets", "Models",
                        "Skeletal", "Characters", "skillclips", "skillclips_map.json")
SIF_REC = 0x111              # 273 б — stride записи skill_info.sif
SIF_NAME = 0x02

# ★ Загрузчик, у которого имя клипа НЕ зашито в теле, а читается из `SkillInfo.inl` (см. §6 шапки).
INL_LOADER = "charLoadSkillEffectMotion"
# ★ Слот-заглушка оригинала: 122 регистрации на один индекс, имя в `skill_info.sif` — «Нет».
STUB_SKILL = 400


# ---------------------------------------------------------------- PE
class Pe(object):
    """Минимальный разбор PE32: перевод виртуального адреса в файловое смещение."""

    def __init__(self, path):
        self.data = open(path, "rb").read()
        d = self.data
        pe = struct.unpack_from("<I", d, 0x3C)[0]
        if d[pe:pe + 4] != b"PE\0\0":
            raise RuntimeError("не PE-файл: %s" % path)
        nsec = struct.unpack_from("<H", d, pe + 6)[0]
        sizeopt = struct.unpack_from("<H", d, pe + 20)[0]
        self.imagebase = struct.unpack_from("<I", d, pe + 24 + 28)[0]
        so = pe + 24 + sizeopt
        self.secs = []
        for i in range(nsec):
            b = d[so + 40 * i: so + 40 * i + 40]
            vs, va, rs, ptr = struct.unpack_from("<IIII", b, 8)
            self.secs.append((self.imagebase + va, vs, ptr, rs))

    def v2f(self, va):
        for base, vs, ptr, rs in self.secs:
            if rs and base <= va < base + min(vs, rs):
                return ptr + (va - base)
        return None

    def cstr(self, va, maxlen=160):
        """C-строка по виртуальному адресу; None — если это не печатаемая ASCII-строка."""
        o = self.v2f(va)
        if o is None:
            return None
        e = o
        while e < len(self.data) and e - o < maxlen and self.data[e] != 0:
            e += 1
        s = self.data[o:e]
        if len(s) < 3 or not all(32 <= c < 127 for c in s):
            return None
        return s.decode("ascii")

    def follow_thunk(self, va, depth=8):
        """Раскрутка цепочки j_-переходников `jmp rel32` до настоящей функции."""
        cur = va
        for _ in range(depth):
            o = self.v2f(cur)
            if o is None or o + 5 > len(self.data) or self.data[o] != 0xE9:
                break
            cur = cur + 5 + struct.unpack_from("<i", self.data, o + 1)[0]
        return cur


# ---------------------------------------------------------------- разбор таблицы
def parse_dispatch(pe, fn_va=FN_DISPATCH, order0=0):
    """Разбор тела функции-инициализатора таблицы: список (порядок, disp, адрес_переходника).

    Одинаково подходит обоим сайтам регистрации: `CharSkillEffect_InitDispatchTable@0xA89510`
    и `charFxInitSoulSkillVtbl@0xACEE20` — тела построены из одной и той же пары инструкций.
    """
    o = pe.v2f(fn_va)
    if o is None:
        raise RuntimeError("адрес 0x%X вне секций" % fn_va)
    d = pe.data
    out = []
    p = o
    end = o + 0x8000
    order = order0
    while p < end:
        b = d[p]
        if b == 0x8B and d[p + 1] == 0x45 and d[p + 2] == 0xFC:     # mov eax, [ebp-4]
            p += 3
            continue
        if b == 0xC7 and d[p + 1] == 0x80:                          # mov [eax+disp32], imm32
            disp = struct.unpack_from("<I", d, p + 2)[0]
            val = struct.unpack_from("<I", d, p + 6)[0]
            out.append((order, disp, val))
            order += 1
            p += 10
            continue
        if b == 0xC7 and d[p + 1] == 0x40:                          # mov [eax+disp8], imm32
            disp = d[p + 2]
            val = struct.unpack_from("<I", d, p + 3)[0]
            out.append((order, disp, val))
            order += 1
            p += 7
            continue
        if b == 0xC3:                                               # ret — конец функции
            break
        p += 1                                                      # пролог/эпилог
    return out


def split_slot(disp):
    """disp → (номер массива, код навыка) либо (None, None), если смещение не элемент массива."""
    rel = disp - CB_BASE
    if rel < 0 or rel % 4:
        return None, None
    k, r = divmod(rel, CB_STRIDE)
    i = r // 4
    if i >= CB_COUNT:
        return None, None
    return k, i


# ---------------------------------------------------------------- имена функций
def load_names():
    """Адрес → имя. Приоритет — эталонный декомпил (RE/etalon/*.c), затем functions.tsv."""
    names = {}
    hdr = re.compile(r"^//----\s+(\S+)\s+@\s+0x([0-9A-Fa-f]+)")
    bodies = {}
    for fn in sorted(os.listdir(ETALON)):
        if not fn.endswith(".c"):
            continue
        path = os.path.join(ETALON, fn)
        lines = open(path, encoding="utf-8", errors="replace").read().splitlines()
        cur, start = None, 0
        for i, line in enumerate(lines):
            m = hdr.match(line)
            if not m:
                continue
            if cur:
                bodies[cur] = lines[start:i]
            names[int(m.group(2), 16)] = m.group(1)
            cur, start = m.group(1), i
        if cur:
            bodies[cur] = lines[start:]
    if os.path.exists(FUNCS_TSV):
        with open(FUNCS_TSV, encoding="utf-8", errors="replace") as f:
            next(f, None)
            for line in f:
                p = line.rstrip("\n").split("\t")
                if len(p) >= 2:
                    try:
                        a = int(p[0], 16)
                    except ValueError:
                        continue
                    names.setdefault(a, p[1])
    return names, bodies


# ---------------------------------------------------------------- разбор тела загрузчика
RE_APPEND = re.compile(r'String_Append\([^,]+,\s*"([^"]+)"\)')
RE_COMBO = re.compile(r'CComboTable_FindByName\([^,]+,\s*"([^"]+)"\)')
RE_ANYSTR = re.compile(r'"([^"\\]{3,120})"')
RE_CASE = re.compile(r'^\s*case\s+(-?\w+):')
RE_SWITCH = re.compile(r'^\s*switch\s*\(\s*(.+?)\s*\)\s*$')
RE_DEFAULT = re.compile(r'^\s*default:')
RE_IF = re.compile(r'^\s*(?:\}\s*)?(?:else\s+)?if\s*\(\s*(.+?)\s*\)\s*$')
RE_ELSE = re.compile(r'^\s*(?:\}\s*)?else\s*$')
# ★ ПОЛЕ СЛОТА, куда кладётся путь эффекта. Эталон (`CCharSkillFx_InitHeavyBlow`, навык 501,
#   effects_particles.c:18234-18236):
#       CPackPath_SetEntryName(&pChar->effectSlots[a3].field_68 , "eff_1_char_99_hblowb02-a_00.jeb");
#       CPackPath_SetEntryName(&pChar->effectSlots[a3].field_168, "eff_1_char_01_hblowe01-a_00.jeb");
#       CPackPath_SetEntryName( pChar->effectSlots[a3].gap_268  , "eff_2_char_99_hblowc01-a_00.jeb");
#   В слоте эффекта четыре путевых поля по 256 б: 0x68, 0x168, 0x268, 0x368 — и КАЖДОЕ читает
#   СВОЙ тик (напр. `charFx_autoSkill501Tick@0xAB54B0` спавнит из `field_168` в момент старта,
#   `effects_particles.c:6916` — из `gap_268`). Поэтому порядок/привязка .jeb к полю — ЗНАЧАЩИЕ,
#   сортировать их нельзя (иначе теряется уже извлечённая пофазная раскладка).
#
# ★★★ ИСПРАВЛЕНО 2026-08-10: прежний шаблон `effectSlots\[[^\]]*\]\.` не переживал ВЛОЖЕННЫЙ индекс
#   `effectSlots[ effectSlots[100].field_4 ].field_168` — класс `[^\]]*` останавливался на первой
#   закрывающей скобке и шаблон возвращал `field_4` вместо `field_168`. В телах ЗАГРУЗЧИКОВ индекс
#   простой (`effectSlots[a3]`), поэтому дефект там не проявлялся; в телах ТИКОВ индекс вложен
#   ВСЕГДА — из-за этого разведка по тикам показывала «field_168 у 2 навыков» вместо реальных сотен.
#   Теперь берём ПОСЛЕДНЮЮ пару «]. поле» в строке (жадный `.*\]`), плюс отдельно именованные поля
#   звука, у которых индекса-скобки в имени нет.
RE_SLOTFIELD = re.compile(r'effectSlots\[.*\]\.(field_[0-9A-Fa-f]+|gap_[0-9A-Fa-f]+)')
# ★ 10.11.2026 (job4-vfx): `CPackPath_SetEntryName(<поле слота>, "<имя>.jeb")` в ТЕЛЕ ТИКА — тик сам пишет имя в поле (см. разбор тика).
RE_TICK_SETNAME = re.compile(r'CPackPath_SetEntryName\(\s*(\(CPackPath \*\)\s*)?(&?[^,]*effectSlots\[.*\]\.(?:field|gap)_[0-9A-Fa-f]+)\s*,\s*"([^"]+\.jeb)"\s*\)')
# Именованные поля слота, которые в эталоне уже получили имена (скобочного шаблона им мало).
NAMED_SLOTFIELD = {"pEffectSoundObj": "snd_cast"}

# ★★★ СЫРАЯ (НЕСВЁРНУТАЯ) ФОРМА ССЫЛКИ НА ПОЛЕ СЛОТА — 2026-08-10, fxcoverage-agent.
#   Часть тел Hex-Rays не свернул в `pChar->effectSlots[i].field_XX`, а оставил ДВОРД-АРИФМЕТИКОЙ:
#       v5 = CObject_ReturnThis_965CB0(&a2[368 * a2[43134] + 6359]);      // skillFx_SPskill047
#       Effect_SpawnByName(g_pEffectMgr, v5, 1, v8, v9, v10, 0, v11, 0, 6, 0, 1065353216);
#   Разбор такие строки НЕ УЗНАВАЛ, поле не находилось, и вызов спавна молча пропускался
#   (`continue`) — ровно тот класс дыры, что описан в [[jeb-emitter-routing-hole]]. Из-за этого
#   у 69 навыков с `.jeb` не было НИ ОДНОГО разобранного эффекта: их каст спавнит не тик (массив 5),
#   а колбэк массива 1, и именно у него тело осталось в сырой форме.
#
#   АРИФМЕТИКА ВОССТАНОВЛЕНА И ПЕРЕПРОВЕРЕНА ТРЕМЯ НЕЗАВИСИМЫМИ РАВЕНСТВАМИ (дворды от pChar):
#     · `368 * idx + 6335`  = `&effectSlots[idx].field_8`   (CObject_SetFloatDispatch во всех тиках)
#         ⇒ база = 6335 − 0x8/4 = 6333
#     · `*((_DWORD *)pChar + 45429)` = `effectSlots[106].field_160`  (гейт первого кадра)
#         ⇒ 6333 + 368*106 + 0x160/4 = 6333 + 39008 + 88 = 45429 ✔
#     · `a2[43134]` = `effectSlots[100].field_4` (индекс активного слота; в свёрнутых телах ровно он)
#         ⇒ 6333 + 368*100 + 1 = 43134 ✔
#   Шаг слота 368 дворд = 0x5C0 б — совпадает с объявлением EffectSlot в `structs_ru.h`.
RAW_SLOT_BASE = 6333          # дворд-индекс начала effectSlots[0] от pChar
RAW_SLOT_STRIDE = 368         # 0x5C0 / 4 — размер одного слота эффекта в двордах
# Форма `368 * <что угодно> + N` — индекс слота переменный, N несёт ТОЛЬКО смещение поля.
# ⚠ Слагаемых после `368 *` бывает НЕСКОЛЬКО, и нужное — не первое:
#     *((float *)pChar + 368 * *((_DWORD *)pChar + 43134) + *((_DWORD *)pChar + 47781) + 6355)
#   здесь 43134 — индекс активного слота, 47781 — номер фазы, и только 6355 — смещение поля
#   (0x58 = `.color`, порог COMBO). Поэтому проверяем ВСЕ слагаемые строки после `368 *`,
#   а негодные отсеиваются самой арифметикой `_rawslot_field` (выход за размер слота).
RE_RAWSLOT_MUL = re.compile(r'%d\s*\*' % RAW_SLOT_STRIDE)
RE_RAWSLOT_TERM = re.compile(r'\+\s*(\d{4,6})\b')


def _rawslot_field(n):
    """Сырой дворд-индекс из формы `368 * idx + N` → «0xNN» (смещение поля слота) либо «»."""
    k = n - RAW_SLOT_BASE
    if k < 0 or k >= RAW_SLOT_STRIDE:
        return ""                              # смещение поля не может выйти за один слот
    return "0x%X" % (k * 4)


# Смещение поля `pEffectSoundObj` внутри слота (`RE/ida/gap_breakout.tsv`: EffectSlot +0x578).
SNDOBJ_OFFSET = "0x578"

# Условия, которые ничего не говорят о ВЫБОРЕ клипа (служебные гейты слота) — в отчёт не выносим.
COND_NOISE = ("field_4 != -1", "field_4 == -1", "!pTarget", "!pChar", "== -1)", "!Entity_Find")


def _cond_useful(c):
    return c and not any(n in c for n in COND_NOISE)


# ★★★ НЕ-ОРУЖЕЙНОЕ УСЛОВИЕ ВЕТКИ: РАЗМЕРНЫЙ КЛАСС ЦЕЛИ (`сущность + 7356`).
#   `SkillEffect_SetupHexa@0xAA4A40` (effects_particles.c:19676) выбирает клип и `.jeb` так:
#       effectTier = *(_DWORD *)(pTargetEntity + 7356);
#       if (effectTier) { if (effectTier == 1) …-m_02, hexaa08… else …-l_02, hexaa09… }
#       else            { …-s_02, hexaa07… }
#   ЧТО ЭТО ЗА ПОЛЕ — снято по ДРУГИМ его читателям (писателя `+7356` в срезе НЕТ, ни в эталоне,
#   ни в мастер-декомпиле; закон dead-code-markings-suspect — пишем «ссылок на запись в срезе нет»):
#     · `effects_particles.c:59656-59668` — высота эффекта над целью: 0 → +25, 1 → +45, 2 → +60;
#     · `ui_core.c:3615-3627` — рамка панели цели: 0 → обычная, 1 → одна, 2 → другая.
#   ⇒ это РАЗМЕРНЫЙ/РАНГОВЫЙ КЛАСС сущности: 0 обычная, 1 средняя, 2 крупная. Только три значения.
RE_TIER_EQ = re.compile(r"effectTier\s*==\s*(\d+)")


def target_size(condraw):
    """Кортеж внутренних условий ветки → размерный класс цели «0»/«1»/«2» либо «» (условия нет)."""
    joined = " && ".join(condraw or ())
    if "effectTier" not in joined:
        return ""
    # ⚠ ПОРЯДОК ПРОВЕРОК ЗНАЧИМ: `else!effectTier == 1` СОДЕРЖИТ подстроку «effectTier == 1», и
    #   поиск равенства первым отдал бы ветке `-l` класс 1 вместо 2. Сначала — отрицания.
    for c in condraw or ():
        if c.startswith("else!") and "effectTier ==" in c:
            return "2"                    # else от `if (effectTier == 1)` внутри `if (effectTier)`
    for c in condraw or ():
        if c.startswith("else!") and "effectTier" in c:
            return "0"                    # else от самого `if (effectTier)`
    m = RE_TIER_EQ.search(joined)
    return m.group(1) if m else ""


def _dedup(seq):
    """Уникальные значения С СОХРАНЕНИЕМ ПОРЯДКА (порядок = порядок присваивания в эталоне)."""
    out, seen = [], set()
    for v in seq:
        if v in seen:
            continue
        seen.add(v)
        out.append(v)
    return out


# ★ ПУТЕВЫЕ поля слота эффекта — их ровно четыре (CPackPath по 256 б), проверено по всей таблице:
#   в колонке JebSlots встречаются ТОЛЬКО эти значения. Всё остальное (`field_4`, `field_8`, …) —
#   служебные поля слота, и принимать их за «поле пути эффекта» нельзя.
PATH_FIELDS = ("0x68", "0x168", "0x268", "0x368")
# Поля ИМЕНИ ЗВУКА: `pEffectSoundObj` — звук каста, `+0x598` — звук импакта.
SND_FIELDS = ("snd_cast", "0x598")


def _slot_of(line, allow=PATH_FIELDS):
    """Имя поля слота эффекта в этой строке → «0x68» / «0x168» / «snd_cast» / … . Пусто, если нет.

    `allow` ограничивает разбор ИЗВЕСТНЫМИ полями: иначе шаблон радостно вернёт служебное
    `field_4` из индекса `effectSlots[effectSlots[100].field_4]` и привязка «эффект → поле» соврёт.
    """
    for nm, alias in NAMED_SLOTFIELD.items():
        if nm in line:
            return alias if alias in allow else ""
    m = RE_SLOTFIELD.search(line)
    if m:
        tail = m.group(1).split("_", 1)[1]
        try:
            v = "0x%X" % int(tail, 16)
        except ValueError:
            v = ""
        if v and v in allow:
            return v
        if v:
            return ""          # поле слота есть, но оно служебное — сырую форму уже не ищем
    # ★ СЫРАЯ ФОРМА (Hex-Rays не свернул структуру) — см. RAW_SLOT_BASE.
    #   ⚠ Форма с ПОСТОЯННЫМ индексом слота (напр. `pChar + 45429`) НЕ РАЗБИРАЕТСЯ НАМЕРЕННО:
    #     любое 4–6-значное число, попавшее в нужный класс вычетов по модулю 368, дало бы ложное
    #     «поле 0x68». В корпусе эталона путевые поля в такой форме не встречаются вовсе
    #     (все 18 попаданий `+ 6359` и 8 попаданий `+ 6551` — форма A), а гейты (`45429`) путевыми
    #     полями не являются. Если такая форма когда-нибудь появится — она попадёт в `unknown`
    #     как нераспознанный спавн, а не будет угадана.
    m = RE_RAWSLOT_MUL.search(line)
    if m:
        for mm in RE_RAWSLOT_TERM.finditer(line, m.end()):
            v = _rawslot_field(int(mm.group(1)))
            if v == SNDOBJ_OFFSET and "snd_cast" in allow:
                return "snd_cast"
            if v and v in allow:
                return v
    return ""


def analyse_loader(body):
    """Разбор тела загрузчика по эталонному декомпилу.

    Возвращает список клипов; у каждого — ключ комбо-таблицы, ПУТЬ УСЛОВИЙ (вложенные `if`
    и `case` активного `switch`) и .jeb/.wav ИЗ ТОГО ЖЕ (или объемлющего) блока.
    Пол определяется самим именем клипа (`ch4_m_`/`ch4_f_`, `Ch1_m_`/`Ch1_f_`) — отдельного
    условия не требует.
    """
    events = []          # (path, conds, kind, value, slot)  kind: clip | key | jeb | wav
    stack = []           # список условий по глубине вложенности
    path = []            # индексы открытых блоков — «адрес» события в дереве
    pending = ""         # условие, ожидающее открывающую скобку
    last_closed = ""     # условие только что закрытого блока — им «подписывается» следующий else
    switch_sel = []      # стек селекторов switch
    depth = 0
    blockno = 0
    last_was_case = False
    for line in body:
        s = line.strip()
        m = RE_SWITCH.match(line)
        if m:
            pending = "switch:" + m.group(1)
            switch_sel.append(m.group(1))
        else:
            m = RE_IF.match(line)
            if m:
                pending = m.group(1)
            elif RE_ELSE.match(line):
                # ★ `else` НЕСЁТ ОТРИЦАНИЕ СВОЕГО `if` — во внутреннем виде `else!<условие if>`.
                #   Нужен для НЕ-ОРУЖЕЙНЫХ веток: у `SkillEffect_SetupHexa` ветка `-s` живёт именно в
                #   `else` от `if (effectTier)`, и без пары «else↔if» её было не отличить от любого
                #   другого `else`. В ОТЧЁТНУЮ колонку Cond пишется прежнее «else» (байт в байт).
                pending = "else!" + last_closed
        m = RE_CASE.match(line)
        lab = m.group(1) if m else ("default" if RE_DEFAULT.match(line) else None)
        if lab is not None:
            # `case` меняет условие на текущем уровне switch, скобок не добавляет.
            # Подряд идущие метки (провал `case 8: case 0xA:`) собираются в одно условие.
            if stack and stack[-1].startswith("case "):
                stack[-1] = ("case " + stack[-1][5:] + "|" + lab) if last_was_case else ("case " + lab)
            else:
                stack.append("case " + lab)
                path.append(-1)
            last_was_case = True
            continue
        if s:
            last_was_case = False

        for ch in s:
            if ch == "{":
                blockno += 1
                stack.append(pending)
                path.append(blockno)
                pending = ""
                depth += 1
            elif ch == "}":
                if stack:
                    last_closed = stack.pop()
                    path.pop()
                depth = max(0, depth - 1)

        cur = tuple(path)
        # ⚠ Фильтр «полезности» применяем к ОТОБРАЖАЕМОМУ виду условия: иначе внутренняя подпись
        #   `else!<if>` начала бы попадать под COND_NOISE («== -1)») и молча выкидывать ветки `else`,
        #   которые раньше в колонку Cond ПОПАДАЛИ. Сравнение старой и новой таблицы: 0 расхождений.
        conds = [c for c in stack if _cond_useful(c.split("!", 1)[0] if c.startswith("else!") else c)]
        slot = _slot_of(line)
        # ★ У ЗВУКА своё поле слота: `pEffectSoundObj` = звук КАСТА, `+0x598` = звук ИМПАКТА.
        #   Раньше слот у .wav не сохранялся вообще — из-за этого порт не мог отличить один от другого
        #   и играл оба сразу в момент старта.
        sslot = _slot_of(line, SND_FIELDS)
        for v in RE_ANYSTR.findall(line):
            low = v.lower()
            if low.endswith(".wav"):
                events.append((cur, tuple(conds), "wav", v, sslot))
            elif low.endswith(".jeb"):
                events.append((cur, tuple(conds), "jeb", v, slot))
        m = RE_COMBO.search(line)
        if m:
            events.append((cur, tuple(conds), "key", m.group(1), ""))
        m = RE_APPEND.search(line)
        if m and m.group(1).lower().endswith(".jbb"):
            events.append((cur, tuple(conds), "clip", m.group(1), ""))

    clips = []
    for n, (cur, conds, kind, val, _sl) in enumerate(events):
        if kind != "clip":
            continue
        key = ""
        for cur2, _c2, k2, v2, _s2 in events[n + 1:n + 4]:
            if k2 == "key":
                key = v2
                break
        jebs, jslots, wavs, wslots = [], [], [], []
        for cur2, _c2, k2, v2, s2 in events:
            if k2 not in ("jeb", "wav") or cur[:len(cur2)] != cur2:   # не тот же и не объемлющий блок
                continue
            if k2 == "jeb":
                jebs.append(v2)
                jslots.append(s2)
            else:
                wavs.append(v2)
                wslots.append(s2)
        # ★ Порядок НЕ сортируется — он и есть привязка к полю слота (см. RE_SLOTFIELD).
        pairs = _dedup(list(zip(jebs, jslots)))
        wpairs = _dedup(list(zip(wavs, wslots)))
        # ★ В ОТЧЁТ идёт прежний вид «else» — колонка Cond не меняется ни на байт;
        #   внутренний «else!<if>» используется только машинным разбором (target_size).
        cond = " && ".join(c.split("!", 1)[0] if c.startswith("else!") else c
                           for c in conds if not c.startswith("switch:"))
        sel = "; ".join(c[7:] for c in conds if c.startswith("switch:"))
        clips.append(dict(clip=val, key=key, cond=cond, sel=sel, condraw=tuple(conds),
                          jebs=[j for j, _s in pairs], jslots=[s for _j, s in pairs],
                          wavs=[w for w, _s in wpairs], wslots=[s for _w, s in wpairs]))
    all_pairs = _dedup([(v, s) for _c, _cc, k, v, s in events if k == "jeb"])
    all_wpairs = _dedup([(v, s) for _c, _cc, k, v, s in events if k == "wav"])
    return (clips, [w for w, _s in all_wpairs], [j for j, _s in all_pairs],
            [s for _j, s in all_pairs], [s for _w, s in all_wpairs])


# ---------------------------------------------------------------- разбор тела ТИКА (массив 5)
#
# ★★★ ЧТО ЗДЕСЬ СНИМАЕТСЯ И ЗАЧЕМ.
#   Загрузчик (массив 0) НИЧЕГО не спавнит — он только РАСКЛАДЫВАЕТ имена `.jeb`/`.wav` по полям
#   слота эффекта. Спавнит ТИК (массив 5). Значит «какой эффект когда играет» знает только тик,
#   и без его разбора порт вынужден вываливать все `.jeb` разом на старте — ровно это и было
#   («импакта не видно»). Разбор ниже — механический, по эталонному декомпилу, без догадок.
#
# РОЛИ ПОЛЕЙ СЛОТА — подтверждены на трёх независимых функциях:
#   1) `SkillEffect_UpdateScaledStrikeHit@0xA72610` (effects_particles.c:10045):
#        первый кадр (`effectSlots[106].field_160 == -1`):
#            Effect_SpawnByName(mgr, СЛОТ+0x268, 1, кость.xyz, слот, позаКастера, boneIdx, /*attach*/7, …)
#            кость = «8_a_End»  ⇒  0x268 = ЭФФЕКТ КАСТА НА КОНЧИКЕ ОРУЖИЯ;
#        порог удара (`slot.color <= прогресс`, `color` = COMBO0):
#            цель-МОБ  (`pCaster[1].gap_0[2] & 1`): CEffectMgr_CreateEffect_F561F0(СЛОТ+0x168, поза цели)
#                                                   + CEffectMgr_scaleAll(fx, масштабЦели/100)
#            цель-ИГРОК:                            CEffectMgr_CreateEffect_F561F0(СЛОТ+0x68 , поза цели)
#            звук в обоих случаях — СЛОТ+0x598, канал 3.
#   2) `CSkillCast_ApplyEntityHitEffect@0xA7B4A0` (skill_buff.c:17509) — фазовый вариант того же:
#        `effectSlots[112].field_3A0` = НОМЕР ФАЗЫ; фаза 0 → 0x68 (урон `effectSlots[100].field_64`),
#        фаза 1 → 0x168 (урон `effectSlots[100].field_68`), фаза ≥2 → НИЧЕГО (ранний return).
#        Позиция — точка пересечения луча «кастер→цель» с AABB цели (`effectCollideRayWithObject`).
#   3) `CSkillCast_ApplySingleTargetHit@0xA7AF20` (skill_buff.c:17402) — то же для цели-ИГРОКА,
#        та же развилка фаз 0→0x68 / 1→0x168, позиция — soundPos цели.
#   ⇒ ОБЩЕЕ ПРАВИЛО: 0x68 = импакт фазы 0, 0x168 = импакт фазы 1, 0x268 = эффект каста на кости,
#     0x368 = эффект, ставящийся МИРОВОЙ МАТРИЦЕЙ персонажа (`effectMgrSpawnJEB`).
#     Звук: `pEffectSoundObj` = звук КАСТА (канал 18, у кости), `+0x598` = звук ИМПАКТА (канал 3/25).
#   ⚠ Это правило НЕ универсально: часть тиков переиспользует 0x168 ещё и как визуал старта
#     (`charFx_autoSkill501Tick@0xAB54B0` спавнит 0x168 в позиции персонажа на первом кадре).
#     Поэтому роль берётся ИЗ ТЕЛА КОНКРЕТНОГО ТИКА, а не назначается по номеру поля.
#
# Функции-спавнеры и что означает точка привязки:
FX_SPAWNERS = {
    "Effect_SpawnByName":             "bone",    # attach 7: кость даёт ПОЗИЦИЮ (type-2 модель)
    "CEffectMgr_CreateEffect_F561F0": "world",   # свободный эффект в мировой точке
    "effectMgrCreateEffectEx":        "attach",  # привязка к сущности (attach 13)
    "effectMgrSpawnJEB":              "matrix",  # полная мировая матрица
}
# ★★★ НОСИТЕЛЬ СНАРЯДА — ДВА РАЗНЫХ, И ЭТО НЕ СИНОНИМЫ (снято 2026-08-10, effects_particles.c):
#   · `effect_spawnDirectedFromTo@0xA59B30` — ПОЛНЫЙ носитель: грузит `.jeb` ПОЛЁТА в `+520`
#     (`CEffect_LoadJEB_Wrapper`), кладёт имя эффекта удара в `+8`, звук удара в `+264`,
#     skillId в `+1796`, номер фазы в `+1878` и ставит флаг `+1804 = 1` («визуал полёта есть»);
#   · `effectInitCollisionTrace@0xA596F0` — БЕСТЕЛЕСНЫЙ носитель: заполняет ТОЛЬКО трассер
#     (`+1600`), id цели (`+1780`), id эффекта урона (`+1784`) и `+1788 = 1`. Ни `.jeb`, ни звука,
#     ни `+1804` — значит визуала полёта у него НЕТ ПО УСТРОЙСТВУ, это чистая ЗАДЕРЖКА урона.
#   Прежний общий кортеж `FX_PROJECTILE` смешивал их (плюс низкоуровневый `collisionTrace_Init`,
#   который зовут ОБА) — из-за этого `Proj = 1` вставало у тиков без единого снаряда.
# ★★★ ПОЛНЫХ НОСИТЕЛЕЙ НА САМОМ ДЕЛЕ ТРИ (добрано 2026-08-10, fxcoverage-agent).
#   Механический критерий «полного носителя»: тело грузит `.jeb` в `+520` через
#   `CEffect_LoadJEB_Wrapper` и сбрасывает `+1804`. Ему отвечают РОВНО три функции слайса:
#     · `effect_spawnDirectedFromTo@0xA59B30`   — зовут ТИКИ (массив 5);
#     · `effect_spawnDirectedInstance@0xA59820` — зовёт `effectSpawnAtBoneItem` (массив 4);
#     · `effectSpawnProjectileJeb@0xA59420`     — зовут `effectSpawnAtBone8aEnd` и
#                                                 `charEffectSpawnAttached` (массив 4).
#   Знали только про первую — поэтому у ДЕВЯТИ лучных навыков (702, 703, 704, 707, 708, 709, 711,
#   712, 714) и у 905/915 стрела/снаряд не заводились вовсе: `Proj` стоял 0, а поле `0x168`
#   (визуал полёта) отчитывалось как «читателей в теле тика нет». Найдено сплошным перебором
#   стоков `qmemcpy(<буфер>, &слот.<путевое поле>, 0x100)` по всему эталону — других стоков нет.
FX_PROJ_FULL = ("effect_spawnDirectedFromTo", "effect_spawnDirectedInstance",
                "effectSpawnProjectileJeb")
FX_PROJ_DELAY = "effectInitCollisionTrace"
# ★★★ ЧЕТВЁРТАЯ, ОТДЕЛЬНАЯ СЕМЬЯ СНАРЯДА — `skillProjectile_createNode@0xA60590` (добрано
#   2026-08-10). Здесь имена НЕ в полях слота, а ЛИТЕРАЛАМИ в теле тика
#   (`CharSkill_UpdateGrenadeFx@0xB43EC0`, навыки 604 «Граната» и 607 «Осколочная граната»):
#       strcpy(&effectParams[11], "eff_1_char_99_grebom01-a_00.jeb");   // ЭФФЕКТ УДАРА
#       strcpy(&effectParams[75], "S_c_SPskill024_01.wav");             // ЗВУК УДАРА
#       skillProjectile_createNode(ctx, "eff_1_char_99_grened01-a_00.jeb", скорость, …);  // ПОЛЁТ
#   Разбор этой формы не знал ни одна ветка: у 604/607 в отчёте стояло «эффектов 0», хотя оба
#   `.jeb` лежат на диске. Других вызывающих у функции в срезе нет (проверено grep по эталону).
FX_PROJ_NODE = "skillProjectile_createNode"

# ★★★ РАЗОБРАННЫЕ ДО КОНЦА «нераспознанные» спавны (2026-08-10, fxcoverage-agent).
#   Ключ — (функция, спавнер); значение — готовая строка для FxNote вместо сырого куска кода.
#
#   `CSkillCast_ApplyAoEHitEffects` (`skill_buff.c:16529`) спавнит имя НЕ из поля слота, а из
#   записи `SkillInfo.inl`: `CObject_ReturnThis_965CB0(&skillData[524*eb + 6])` = FILE_NAME
#   подсекции `eb`, где `eb` = **2** (первый проход) и **10** (второй) — цикл `effectPassIdx 0..1`.
#   Спавн идёт по КАЖДОЙ из 20 целей списка (`pChar + hitIdx + 86276`), а точка рождения выбирается
#   полем `APPEND_TYPE` подсекции (`skillData[524*eb + 134]`, байт `+1048*eb + 268`) — ровно тремя
#   ветками таблицы APPEND_TYPE_HIT:
#       0 → `CEffectMgr_CreateEffect_F561F0` в позиции цели      (`world:target`)
#       1 → `effectMgrCreateEffectEx`, attach 13 к сущности цели (`attach:target`)
#       2 → `effectMgrSpawnJEB` в точке пересечения луча с AABB  (`hitpoint:target`)
#   Звук — `SOUND_FILE_NAME_A` (`+266` = байт +532) и `SOUND_FILE_NAME_B` (`+396` = байт +792) тех
#   же подсекций. Порог вызова в `Char_UpdateSkillAnim_MultiPhase` — `slot.color` БЕЗ индекса фазы
#   (entity_char.c:30030-30041) ⇒ это ФАЗА 0.
#   ⚠ `skillData[524*eb + 524]` (= `SPECIALEFFECT`) спавн НЕ гейтит: под ним только
#     `sub_93536D(g_pRenderer)`. Первая гипотеза «подсекции 2/10 не играют» ОТОЗВАНА.
#   ⇒ Разбор этих подсекций перенесён в `build_fx_inl` (колонка FxHit0), крик из noroute снят.
_AOE_KNOWN = ("AoE-эффект подсекций 2/10 разобран: см. FxHit0 (APPEND_TYPE → world/attach/hitpoint "
              "у цели), CSkillCast_ApplyAoEHitEffects")
KNOWN_NOROUTE = {("CSkillCast_ApplyAoEHitEffects", s):
                 ("CSkillCast_ApplyAoEHitEffects", s, _AOE_KNOWN)
                 for s in ("CEffectMgr_CreateEffect_F561F0", "effectMgrCreateEffectEx",
                           "effectMgrSpawnJEB")}
# Пофазный спавн подсекции `SkillInfo.inl` (ветка .inl): аргумент — номер подсекции.
RE_DESC_SPAWN = re.compile(r'CharFx_SpawnEffectFromDesc\([^,]+,\s*(\d+)\s*,')
# Помощники импакта: вызываются ИЗ тика на пороге COMBO, поля слота читают уже они.
HIT_HELPERS = ("CSkillCast_ApplySingleTargetHit", "CSkillCast_ApplyEntityHitEffect",
               "CSkillCast_ApplyAoEHitEffects", "Skill_ApplyAoeHitToTargets")
# Имена, в которые заходить БЕССМЫСЛЕННО (утилиты рантайма) — отсекаем до разбора тела.
HELPER_SKIP = ("CObject_", "CClass_", "Math_", "Struct_", "CPackPath_", "String_", "CRT_",
               "sprintf", "qmemcpy", "strcmp", "strlen", "CEffectMgr_", "Effect_Spawn",
               "effectMgr", "Entity_", "CharManager_", "List_", "D3DX", "sub_")


def _is_fx_helper(name, bodies):
    """Функция, в которую ИМЕЕТ СМЫСЛ зайти: у неё есть тело, она спавнит эффект и читает поле слота.

    Критерий механический, без списка «избранных»: иначе половина импактов теряется — их спавнит не
    сам тик, а вызванный им помощник (`CSkillCast_ApplyEntityHitEffect` и три десятка ему подобных).
    """
    if any(name.startswith(p) for p in HELPER_SKIP):
        return False
    b = bodies.get(name)
    if not b:
        return False
    txt = "\n".join(b)
    # ★ Критерий УЖЕ, чем «упоминает effectSlots»: тело обязано читать ИМЕННО ПУТЕВОЕ ПОЛЕ слота
    #   (0x68/0x168/0x268/0x368) или поле звука. Иначе разбор уходит в служебные подпрограммы —
    #   и в программу навыка 501 приезжают `itm1_n_00_vehicle_0000_e.jeb` и `fish_end_01.jeb`
    #   из совершенно посторонних веток (транспорт, рыбалка).
    if not any(t in txt for t in ("field_68", "field_168", "gap_268", "field_268",
                                  "gap_368", "field_368", "field_598", "pEffectSoundObj")):
        return False
    return any(fn in txt for fn in FX_SPAWNERS) or "sound3DPlay" in txt
# Признаки контекста в условиях (снято с эталона, см. блок выше).
# ★ Блок ПЕРВОГО КАДРА. Кроме именованного `effectSlots[106].field_160` тот же флаг встречается в
#   «сыром» виде `*((_DWORD *)pChar + 45429) == -1` (Hex-Rays не свернул поле): 106*0x5C0/4 + база
#   даёт ровно 45429, и обе записи стоят в одном и том же месте — гейте первого кадра показа
#   (сравнить `Char_UpdateSkillAnim_MultiPhase` и `SkillEffect_UpdateScaledStrikeHit`).
# ★★★ ВТОРАЯ ФОРМА ТОГО ЖЕ ГЕЙТА — `!= 1` (добрано 2026-08-10, fxcoverage-agent).
#   Эталон пишет гейт первого кадра ДВУМЯ способами, и оба закрываются записью «уже показано»:
#     `if (slot106.field_160 == -1) { …спавн…; slot106.field_160 = 0; }`   (ветка UpdateScaledStrikeHit)
#     `if (slot106.field_160 != 1)  { …спавн…; slot106.field_160 = 1; }`   (ветка charFx_*Tick,
#         напр. `CChar_UpdateSkillAnim_IronBody@0xB3DF40`, `charFxSkillTick_op312@0xB3DD10`,
#         `charFx_AutoActionUpdate_B41200@0xB41200`)
#   Форма `!= 1` не была в списке — из-за этого спавн/звук таких тиков не получал контекста
#   «первый кадр» и уезжал в `unknown` (а спуск в помощников из этого блока не выполнялся вовсе).
G_FIRSTFRAME = ("field_160 == -1", "field_4 == -1", "45429) == -1", "+ 45429) == -1",
                "field_160 != 1", "45429) != 1", "+ 45429) != 1")
# ★★★ БЛОК ПОРОГА УДАРА — ИМЕНОВАННАЯ И СЫРАЯ ФОРМЫ (сырая добрана 2026-08-10, fxcoverage-agent).
#   Именованная: `if (slot.color <= прогресс)`, флаг «уже сработало» `effectSlots[112].field_394`.
#   Сырая (Hex-Rays не свернул структуру, напр. `charAnim_UpdateActionBlend@0xAB3060`):
#       if ( !*((_DWORD *)pChar + 47778) ) {                       // 47778 = slot112.field_394
#         hitFrameTime = *((float *)pChar + 368*idx + *((_DWORD *)pChar + 47781) + 6355);
#         if ( hitFrameTime <= currentFrame ) { … Skill_PlayHitEffectOnEntity(…) … }
#   Арифметика сырых индексов (база 6333, шаг 368) — та же, что у RAW_SLOT_BASE:
#     47778 − 6333 = 41445 = 368*112 + 229 → слот 112, поле 0x394 («удар этой фазы уже сыгран»);
#     47781 − 6333 = 41448 = 368*112 + 232 → слот 112, поле 0x3A0 (НОМЕР ФАЗЫ);
#     6355  − 6333 = 22                    → поле 0x58 (`.color`, порог COMBO текущей фазы).
#   Без этих трёх токенов удары таких тиков не получали контекста «порог» и разбор не спускался
#   в `Skill_PlayHitEffectOnChar/OnEntity` — эффекты фаз пропадали целиком.
G_HIT = (".color", "field_394", "47778)", "+ 6355)", "+ 6355]")
# Номер фазы: именованное `effectSlots[112].field_3A0` либо его СЫРОЙ вид `*((_DWORD *)pChar + 47781)`.
PHASE_TOKEN = r'(?:field_3A0|47781\))'
RE_PHASE_EQ = re.compile(PHASE_TOKEN + r'\s*(==|!=)\s*(\d+)')
RE_PHASE_TRUTH = re.compile(r'^(!)?[A-Za-z_>\-\.\[\]0-9]*field_3A0\)?$')
RE_CALL = re.compile(r'\b([A-Za-z_][A-Za-z_0-9]{3,})\s*\(')
# Кость привязки эффекта каста (обе ветки — см. FxBoneFor в AsdaSkillFxSubsystem.cpp).
RE_BONE_LIT = re.compile(r'List_FindIndexByName\([^,]+,\s*"([^"]+)"\)')
# ★ Часть тиков не берёт имя из поля слота, а ПИШЕТ ЕГО ЛИТЕРАЛОМ прямо в вызов спавна
#   (`charFx_PlayHpCongEffect`: Effect_SpawnByName(mgr, "eff_1_char_99_hpcong01-a_00.jeb", …)).
#   Без этой ветки такие эффекты просто исчезали из программы показа — а их 64 строки только у
#   одного тика. Псевдополе «lit:<имя>» несёт имя прямо в себе.
RE_JEB_LIT = re.compile(r'"([^"]+\.jeb)"', re.I)
# То же для ЗВУКА: часть тиков не читает поле слота, а зовёт `Audio_Play3DSound(..., "S_c_….wav", 0)`.
RE_WAV_LIT = re.compile(r'"([^"]+\.wav)"', re.I)
# Хвост вызова `Effect_SpawnByName(..., <attach>, 0, 1065353216)` — режим привязки эталона.
RE_ATTACH_TAIL = re.compile(r',\s*(\d+)\s*,\s*0\s*,\s*1065353216\s*\)')
# Режимы привязки Effect_SpawnByName (эталон): 7/10 — кость даёт позицию, 6 — следование за
# персонажем, 0 — свободный эффект в мировой точке.
# ★ 04.11.2026 [ОРИГИНАЛ] (skillvfx: «Молния» мага не держится на мобе): режимы, у которых обработчик кадра ведёт эффект ЗА ЦЕЛЬЮ
#   (`CEffectMgr_Construct@0xF551C0` таблица обработчиков, вызов `objectMgr_TickEffects@0xF57990`): 3 effectSyncPosToRenderEntity,
#   9 effectSyncPosToOwnerEntity, 12 effectUpdateEntityAttachPos (+25/45/60), 13 effectPlaceAboveEntity (+200),
#   14 effect_UpdateAttachToTarget (матрица цели), 15 effectNodeFollowEntityBone (кость цели) → привязка «track» (рантайм крепит
#   эффект к цели и не масштабирует его по размеру цели — scaleAll у этих спавнов эталон не зовёт). Раньше они падали в умолчание
#   и рождались неподвижными в точке цели на момент каста.
ATTACH_MODE = {7: "bone", 10: "bone", 6: "follow", 5: "follow", 0: "world",
               3: "track", 9: "track", 12: "track", 13: "track", 14: "track", 15: "track"}


def _walk_guarded(body):
    """Итератор «строка тела → кортеж активных условий» (тот же посимвольный разбор, что у загрузчика).

    ★ Ветка `else` НЕСЁТ ОТРИЦАНИЕ СВОЕГО `if`: пишем её как `else!<условие if>`. Без этого ветка
      `else` у `if (effectSlots[112].field_3A0)` выглядела как безусловная — и фаза 0 (именно она
      живёт в этом `else`, см. `CSkillCast_ApplyEntityHitEffect`) теряла свой номер.
    """
    stack, pending, last_closed = [], "", ""
    for lineno, line in enumerate(body):
        s = line.strip()
        m = RE_SWITCH.match(line)
        if m:
            pending = "switch:" + m.group(1)
        else:
            m = RE_IF.match(line)
            if m:
                pending = m.group(1)
            elif RE_ELSE.match(line):
                pending = "else!" + last_closed
        yield lineno, line, tuple(stack)
        for ch in s:
            if ch == "{":
                stack.append(pending)
                pending = ""
            elif ch == "}":
                if stack:
                    last_closed = stack.pop()


def _phase_of(conds):
    """Номер фазы из условий на `effectSlots[112].field_3A0` (см. CSkillCast_Apply*Hit*).

    Эталон устроен так: `if (field_3A0)` … `if (field_3A0 != 1) return;` ⇒ ветка ФАЗЫ 1,
    ветка `else` того же `if` ⇒ ФАЗА 0. Возвращает номер или None, если фаза не определяется.
    """
    phase = None
    truthy = False
    for c in conds:
        if "field_3A0" not in c and "47781)" not in c:   # именованная и сырая форма
            continue
        m = RE_PHASE_EQ.search(c)
        if m:
            # `if (field_3A0 != 1) return;` — весь остаток блока живёт ровно при фазе 1.
            phase = int(m.group(2))
            continue
        if c.startswith("else!"):
            phase = 0 if phase is None else phase     # `else` от `if (field_3A0)` = фаза 0
        else:
            truthy = True
    if phase is None and truthy:
        phase = 1                    # вошли в `if (field_3A0)` без уточнения — это не фаза 0
    return phase


# ★ Номер фазы, выраженный ЛОКАЛЬНОЙ копией счётчика. Тики 701/707/903 читают
#   `effectSlots[112].field_3A0` в переменную (`phaseIndex`, `effectStage`) и дальше ветвятся по ней:
#       phaseIndex = pChar->effectSlots[112].field_3A0;
#       if (phaseIndex) { if (phaseIndex == 1) …ФАЗА 1… else …ФАЗА 2… } else …ФАЗА 0…
#   Без разбора этой формы фаза снаряда у всех трёх навыков определялась как 0, и второй/третий
#   выстрелы («Тройной выстрел» 701, «Повторный выстрел» 707) уезжали в фазу 0.
RE_PHASE_LOCAL_EQ = re.compile(r'\b(?:phaseIndex|effectStage|phase|stage)\s*(==|!=)\s*(\d+)', re.I)
RE_PHASE_LOCAL_TRUE = re.compile(r'^(else!)?!?\s*(?:phaseIndex|effectStage|phase|stage)\)?$', re.I)


def _phase_of_local(conds):
    """Номер фазы из локальной копии счётчика фаз. None — форма не распознана."""
    phase, truthy = None, False
    for c in conds:
        m = RE_PHASE_LOCAL_EQ.search(c)
        if m:
            n = int(m.group(2))
            if c.startswith("else!"):
                # `else` от `if (phaseIndex == 1)` внутри `if (phaseIndex)` — это ФАЗА 2.
                phase = 2 if n == 1 else phase
            else:
                phase = n if m.group(1) == "==" else phase
            continue
        if RE_PHASE_LOCAL_TRUE.match(c.strip()):
            if c.startswith("else!"):
                phase = 0 if phase is None else phase        # `else` от `if (phaseIndex)` = ФАЗА 0
            else:
                truthy = True
    if phase is None and truthy:
        phase = 1
    return phase


def analyse_tick(tickname, bodies, _depth=0, _base=None, _seen=None, _ctx=None):
    """ПРОГРАММА ЭФФЕКТОВ тика: какое поле слота, когда и куда спавнится.

    Возвращает dict:
        cast   : [(поле, привязка, кость)]   — спавн на первом кадре показа
        hit    : {фаза: [(поле, привязка)]}  — спавн на пороге COMBO<фаза>
        hitany : [(поле, привязка)]          — спавн на пороге, фаза которого не определилась
        snd    : {"cast": поле, "hit": поле}
        proj   : True, если тик заводит НОСИТЕЛЬ СНАРЯДА (урон едет в нём)
        desc   : [номера подсекций .inl, спавнящихся на старте]  (ветка Char_UpdateSkillAnim_MultiPhase)
        dmg    : True, если тик показывает цифры урона / «MISS»
        unknown: [(поле, привязка)] — спавн, чей контекст механически НЕ определился (честная дыра)
    ★ Закон jeb-emitter-routing-hole: неучтённый случай обязан ПОПАСТЬ В ОТЧЁТ (`unknown`),
      а не быть проглоченным.
    """
    res = _base if _base is not None else dict(cast=[], hit={}, hitany=[], snd={}, proj=False,
                                               desc=[], dmg=False, unknown=[], fns=[],
                                               projfx={}, projdelay=False, noroute=[], sndlit={})
    res.setdefault("noroute", [])     # старые вызовы с готовым `_base` не знают о новых ключах
    res.setdefault("sndlit", {})
    _seen = _seen if _seen is not None else set()
    if tickname in _seen or _depth > 2:
        return res
    _seen.add(tickname)
    body = bodies.get(tickname)
    if not body:
        return res
    res["fns"].append(tickname)
    bone = ""
    for lineno, line, conds in _walk_guarded(body):
        m = RE_BONE_LIT.search(line)
        if m:
            bone = m.group(1)
        joined = "\n".join(conds)
        # ★ Контекст ВЫЗОВА наследуется: помощник, вызванный на первом кадре, спавнит эффект каста,
        #   а вызванный на пороге COMBO — эффект удара. Без наследования помощник всегда считался бы
        #   «ударным» и эффекты каста ушли бы не в ту колонку.
        first = any(g in joined for g in G_FIRSTFRAME) or bool(_ctx and _ctx.get("first"))
        hit = (any(g in joined for g in G_HIT) or bool(_ctx and _ctx.get("hit"))) and not first
        if _ctx and _ctx.get("conds"):
            joined = _ctx["conds"] + "\n" + joined
            conds = tuple(_ctx["conds"].split("\n")) + conds
        for d in RE_DESC_SPAWN.finditer(line):
            n = int(d.group(1))
            if n not in res["desc"]:
                res["desc"].append(n)
        # ★ 10.11.2026 (job4-vfx, баг «у Blow нет эффектов») [ОРИГИНАЛ]: часть тиков САМА кладёт имя эффекта удара в поле слота
        #   прямо перед вызовом показа удара — `CharSkill_UpdateValkyrieFx@0xABCD80` (512 «Удар», skill_buff.c:22690-22695):
        #       CPackPath_SetEntryName(&slot.field_68 , "eff_1_char_02_rgeblwe03-a_00.jeb");
        #       CPackPath_SetEntryName(&slot.field_168, "eff_1_char_02_rgeblwe04-a_00.jeb");
        #       Skill_PlayHitEffectOnEntity_Snd(…)   // спавнит field_68 / field_168 у цели
        #   Загрузчик эти поля не заполняет, поэтому джойн «загрузчик → поле → тик» давал удару пустые имена — и молча (имени нет,
        #   поля нет в отчёте). Так терялись эффекты удара 504/507/508/512. Имена, записанные телом тика, — источник поля наравне с
        #   загрузчиком (`build_fx_hardcode` добавляет их к именам загрузчика).
        for ms in RE_TICK_SETNAME.finditer(line):
            fkey = _slot_of(ms.group(2))      # тот же вид ключа, что у полей загрузчика: «0x68» / «0x168» / …
            if fkey:
                lst = res.setdefault("tickset", {}).setdefault(fkey, [])
                if ms.group(3) not in lst:
                    lst.append(ms.group(3))
        # ★★★ СНАРЯД. Полный носитель (`effect_spawnDirectedFromTo`) НЕСЁТ визуал полёта — снимаем и
        #   номер фазы (из тех же условий, что и у удара), и ПОЛЕ, откуда берётся `.jeb` полёта.
        #   Бестелесный (`effectInitCollisionTrace`) визуала не несёт вовсе — помечаем отдельно, чтобы
        #   порт не заводил ему невидимый носитель и не писал ложных предупреждений.
        if any(p + "(" in line.replace(" ", "") or p in line for p in FX_PROJ_FULL):
            res["proj"] = True
            ph = _phase_of(conds)
            if ph is None:
                # Фаза не выражена через `field_3A0`, но у этих тиков она читается из ЛОКАЛЬНОЙ копии
                # (`phaseIndex`/`effectStage` = `effectSlots[112].field_3A0`, см. тела 701/707/903).
                ph = _phase_of_local(conds)
            res["projfx"].setdefault(0 if ph is None else ph, _proj_fields(body, lineno))
        if FX_PROJ_NODE in line:
            res["proj"] = True
            ph = _phase_of(conds)
            if ph is None:
                ph = _phase_of_local(conds)
            res["projfx"].setdefault(0 if ph is None else ph, _projnode_fields(body, lineno))
        if FX_PROJ_DELAY in line:
            res["projdelay"] = True
        if ("FloatingText_Spawn" in line or "ShowSkillDamageText" in line
                or "ShowDamageFeedback" in line):
            res["dmg"] = True
        slot = _slot_of(line)
        for fn, anchor in FX_SPAWNERS.items():
            if fn + "(" not in line.replace(" ", "") and fn not in line:
                continue
            # Имя эффекта обычно приходит переменной, присвоенной строкой выше, — поле берём из
            # той же строки, а если его там нет, ищем ближайшее присваивание выше по телу.
            f = slot or _slot_near(body, lineno)
            lit = RE_JEB_LIT.search(line)
            if not f and lit:
                f = "lit:" + lit.group(1)            # имя-литерал прямо в вызове спавна
            if not f:
                # ★★★ Закон [[jeb-emitter-routing-hole]]: НЕМОЙ `continue` здесь и был дырой.
                #   Вызов спавнера в теле ЕСТЬ, а источник имени не распознан — это обязано
                #   попасть в отчёт (колонка FxNote), а не исчезнуть.
                #   Разобранные до конца случаи заменяются ТОЧНОЙ формулировкой (KNOWN_NOROUTE),
                #   чтобы отчёт не кричал про то, что уже объяснено.
                res["noroute"].append(KNOWN_NOROUTE.get((tickname, fn))
                                      or (tickname, fn, line.strip()[:120]))
                continue
            am = RE_ATTACH_TAIL.search(line)
            if am:
                anchor = ATTACH_MODE.get(int(am.group(1)), anchor)
            elif fn == "Effect_SpawnByName":
                # ★ РЕЖИМ ПРИВЯЗКИ ЧЕРЕЗ ПЕРЕМЕННУЮ. Сигнатура эталона:
                #   `Effect_SpawnByName(mgr, имя, 1, x, y, z, слот, поза, кость, ATTACH, 0, 1.0f)`
                #   ⇒ режим — аргумент №9. Хвостовой шаблон `, N, 0, 1065353216)` ловит только
                #   ЛИТЕРАЛЫ, а половина тиков собирает все аргументы в массив (`v20[72] = 6;`
                #   … `Effect_SpawnByName(mgr, v5, v20[65], …, v20[74])`) — там режим не читался и
                #   attach-6 («следует за персонажем») ошибочно выглядел как привязка к кости.
                aargs = _call_args(body, lineno, fn)
                if len(aargs) > 9:
                    v = _resolve_literal(body, lineno, aargs[9])
                    if v is not None:
                        anchor = ATTACH_MODE.get(v, anchor)
            # `Effect_SpawnByName` без литерала кости в этом же блоке — спавн в позиции ПЕРСОНАЖА
            # (`charFx_autoSkill501Tick`: attach 6, координаты = charPos), а не «на кости».
            a = anchor if (anchor != "bone" or bone) else "char"
            # ★★★ ТОЧКА РОЖДЕНИЯ — из АРГУМЕНТОВ ПОЗИЦИИ, а не из контекста фазы (см. _origin_near).
            org = _origin_near(body, lineno, line, fn)
            if org:
                a = "%s:%s" % (a, org)
            if first:
                res["cast"].append((f, a, bone))
            elif hit:
                ph = _phase_of(conds)
                if ph is None:
                    ph = _phase_of_local(conds)
                if ph is None and "field_3A0" not in joined and "47781)" not in joined:
                    # Порог БЕЗ индекса фазы вообще (`slot.color <= прогресс`, флаг «уже сработало»
                    # `field_394`) — это ровно COMBO0, т.е. ФАЗА 0. Так устроен
                    # `SkillEffect_UpdateScaledStrikeHit`: у него одна-единственная фаза.
                    ph = 0 if "field_394" in joined else None
                if ph is None:
                    res["hitany"].append((f, a, _target_kind(joined)))
                else:
                    res["hit"].setdefault(ph, []).append((f, a, _target_kind(joined)))
            else:
                res["unknown"].append((f, a))
            break
        if "sound3DPlay" in line or "Audio_Play3DSound" in line:
            f = _slot_of(line, SND_FIELDS) or _slot_near(body, lineno, SND_FIELDS)
            if f == "snd_cast":
                res["snd"]["cast"] = f
            elif f:
                res["snd"]["hit"] = f
            else:
                # ★ ЗВУК-ЛИТЕРАЛ ПРЯМО В ТИКЕ (добрано 2026-08-10). Загрузчик кладёт в
                #   `pEffectSoundObj` СВОЙ .wav, а тик может играть ВТОРОЙ, зашитый литералом:
                #   `charFxSkillTick_op312` → "S_c_SPskill009.wav" на первом кадре, при том что
                #   загрузчик навыка 786 положил в слот "S_c_hill2.wav". Без этой ветки второй звук
                #   пропадал вовсе.
                w = RE_WAV_LIT.search(line)
                if w:
                    res["sndlit"].setdefault("cast" if first else ("hit" if hit else "any"),
                                             w.group(1))
        # ★ Спускаемся в помощника ТОЛЬКО из осмысленного контекста (первый кадр либо порог удара).
        #   Хвост тика (`Game_IsPausedOrMenu` → возврат в покой, авто-действия, транспорт) контекста
        #   показа не имеет, и заход туда тащил в таблицу навыка чужие эффекты.
        if _depth < 2 and (first or hit):
            for c in RE_CALL.finditer(line):
                nm = c.group(1)
                # Обобщённый спуск — только на ОДИН уровень; глубже ходим лишь по явным помощникам
                # импакта, иначе разбор расползается по всему рантайму.
                if nm in HIT_HELPERS or (_depth == 0 and _is_fx_helper(nm, bodies)):
                    analyse_tick(nm, bodies, _depth + 1, res, _seen,
                                 dict(first=first, hit=hit or nm in HIT_HELPERS, conds=joined))
    if _depth == 0 and res["unknown"] and not res["hit"] and not res["hitany"]:
        # ★ У тика НЕТ порога удара вообще (ни `slot.color`, ни `field_394`) — значит спавн не может
        #   быть «эффектом удара»: это одноразовый эффект самого применения (щиты, ауры, бафы,
        #   напр. `CharSkill_DefenseShieldTick`). Переносим в каст, а не прячем в «не определилось».
        res["cast"].extend((f, a, "") for f, a in res["unknown"])
        res["unknown"] = []
    return res


def _target_kind(joined_conds):
    """Вид цели по условию ветки эталона.

    `SkillEffect_UpdateScaledStrikeHit`: ветка `(*(_WORD *)&pCaster[1].gap_0[2] & 1) != 0` ищет цель
    через `Entity_FindByRenderId` ⇒ это МОБ/сущность; ветка `else` — через `CharManager_FindByUserIdx`
    ⇒ ИГРОК. Тот же признак повторяется в `CSkillCast_Apply*Hit*` (`netWordBlock20`).
    """
    mob = ("gap_0[2] & 1", "netWordBlock20[2] & 1")
    for c in joined_conds.split("\n"):
        for token in mob:
            if token in c:
                # `else!<условие>` — это ОТРИЦАНИЕ ветки моба, т.е. цель-игрок.
                return "char" if c.strip().startswith("else!") else "mob"
    if "gap_0[4] & 1" in joined_conds or "netWordBlock20 & 1" in joined_conds:
        return "char"
    return ""


# ══════════════════════════════════════════════════════════════════════════════════════════════════
# ★★★ ТОЧКА РОЖДЕНИЯ ЭФФЕКТА («у цели» или «у игрока») — снимается из АРГУМЕНТОВ ПОЗИЦИИ вызова.
#
# ЗАЧЕМ. Привязка (`bone`/`world`/`matrix`/`attach`) отвечает на вопрос «КАК эффект держится», но НЕ
#   отвечает на вопрос «ГДЕ он рождается»: `CEffectMgr_CreateEffect_F561F0` одинаково спавнит и у
#   цели (`pTargetChar->worldPos*`), и у кастера (`pChar->soundPos*`), и на кости оружия
#   (`ItemByIndex[12..14]`). Порт до сих пор выводил точку из КОНТЕКСТА («это удар ⇒ у цели»), и
#   поэтому эффекты, которые эталон рождает У ПЕРСОНАЖА в момент удара (напр. `CEntity_PlayEffectWithBone`
#   спавнит `gap_368` мировой матрицей КАСТЕРА), уезжали к цели — и наоборот.
#
# КАК. У всех четырёх спавнеров аргументы 4/5/6 — это x/y/z (либо первые три элемента матрицы 4×4).
#   Берём имя переменной, раскручиваем ближайшие ВЫШЕ присваивания (до 4 переходов, включая
#   `qmemcpy(dst, src, …)`) и классифицируем итоговое выражение по имени носителя. 140 из 144 точек
#   разрешаются с ПЕРВОГО перехода — остальные добираются рекурсией.
RE_ORIGIN_TARGET = re.compile(
    r'\btarget|\bpTarget\w*|\btargetEntity\w*|\btargetChar\w*|\btargetUser\b|\bpTargetW\b', re.I)
RE_ORIGIN_CASTER = re.compile(
    r'\bpChar\b|\bcaster\w*|\bpAttacker\b|\bpCaster\w*|\bthis->|\bplayerObj\b', re.I)
# Кость оружия: `CClass_GetItemByIndex(...)` кладёт узел кости, его 12/13/14 — трансляция матрицы.
RE_ORIGIN_BONE = re.compile(r'ItemByIndex|pBoneNode|boneMatrix|BoneMtx|bonePos|endBonePos', re.I)
# ★ СЫРАЯ позиция КАСТЕРА (2026-08-10, fxcoverage-agent). В несвёрнутых телах (колбэки массива 1,
#   напр. `skillFx_SPskill047`) координаты спавна приходят так:
#       v8 = a2[111]; v9 = a2[112]; v10 = a2[113];
#       Effect_SpawnByName(mgr, имя, 1, v8, v9, v10, …, /*attach*/6, …);
#   Дворды 111/112/113 = байты 0x1BC/0x1C0/0x1C4 = `CCharacter.soundPosX/Y/Z` — та самая позиция
#   персонажа, которую свёрнутые тела печатают как `pChar->soundPosX` (её же берёт соседний
#   `CObj_GetPosition(a2 + 108, …)`: дворд 108 = байт 0x1B0 = `gap_1B0`, ровно перед soundPos).
#   Без этого у 58 строк колонка точки рождения оставалась пустой и рантайм шёл в запасную ветку.
#   Шаблон намеренно ПОЛНОСОВПАДАЮЩИЙ (`^имя[11X]$`), чтобы не ловить чужие индексы в выражениях.
RE_ORIGIN_CASTER_RAW = re.compile(r'^\s*&?[A-Za-z_]\w*\[(?:111|112|113)\]\s*$')
# Точка луча × AABB цели (`collideRayAabbAllFaces` / `effectCollideRayWithObject`) — «в точке попадания».
RE_ORIGIN_HITPOINT = re.compile(r'collideRayAabb|effectCollideRayWithObject|\bposOut\b|\bhitPos\b', re.I)


def _origin_classify(expr):
    """Выражение-источник координаты → «target» / «caster» / «bone» / «hitpoint» / «» (не опознано)."""
    if not expr:
        return ""
    if RE_ORIGIN_HITPOINT.search(expr):
        return "hitpoint"
    # ⚠ ПОРЯДОК ЗНАЧИМ: `pTargetChar` содержит и «target», и «Char» — цель проверяем ПЕРВОЙ.
    if RE_ORIGIN_TARGET.search(expr):
        return "target"
    if RE_ORIGIN_BONE.search(expr):
        return "bone"
    if RE_ORIGIN_CASTER.search(expr) or RE_ORIGIN_CASTER_RAW.match(expr):
        return "caster"
    return ""


def _resolve_var(body, idx, var, depth=8):
    """Ближайшее ВЫШЕ присваивание переменной (в т.ч. через `qmemcpy`) → правая часть, рекурсивно.

    Возвращает выражение, которое удалось классифицировать, либо последнее найденное.
    """
    seen = set()
    cur = var
    for _ in range(depth):
        # Снимаем приведения типа и адресную арифметику: `(int)v14[11]`, `*(_DWORD *)&x`, `LODWORD(y)`.
        head = re.sub(r'^\s*(?:LODWORD|HIDWORD|SLODWORD|LOWORD|SLOWORD)\s*\(', "", cur.strip())
        head = re.sub(r'\(\s*(?:unsigned\s+|signed\s+|const\s+)?[A-Za-z_]\w*\s*\**\s*\)', "", head)
        head = head.lstrip("*&( ")
        m0 = re.match(r'([A-Za-z_]\w*)\s*(\[[^\]]*\])?', head)
        if not m0:
            break
        base, index = m0.group(1), m0.group(2) or ""
        if base in seen:
            break
        seen.add(base)
        # ⚠ ИНДЕКС ОБЯЗАТЕЛЕН ПРИ ПОИСКЕ. Позиции почти всегда пишутся В СОСЕДНИЕ ЭЛЕМЕНТЫ ОДНОГО
        #   массива (`matrixWork[10] = 1; matrixWork[11] = targetEntity[84];`), и поиск «любого
        #   присваивания massив[...]» цеплял СТРОКОЙ НИЖЕ константу `1` и обрывал цепочку.
        idx_pat = re.escape(index) if index else r'(?:\[[^\]]*\])?'
        found = ""
        for j in range(idx - 1, max(-1, idx - 120), -1):
            ln = body[j]
            # ★ `qmemcpy(dst, src, N)` — ПОЭЛЕМЕНТНАЯ копия: индекс ПЕРЕНОСИТСЯ на источник
            #   (`effectArgs[12]` ← `qmemcpy(effectArgs, matrixA, 0x40)` ⇒ дальше искать `matrixA[12]`).
            #   Без переноса цепочка теряла индекс и уходила к соседнему элементу матрицы — из-за
            #   этого 74 эффекта удара оставались без точки рождения.
            m = re.search(r'qmemcpy\(\s*&?%s\s*,\s*(.+?)\s*,\s*(?:sizeof\([^)]*\)|0x[0-9A-Fa-f]+u?)\s*\)'
                          % re.escape(base), ln)
            if m:
                found = m.group(1).strip() + index
                break
            m = re.match(r'\s*\*?\(?[A-Za-z_ ]*\*?\)?\s*&?%s\s*%s\s*=\s*(.+?);\s*$'
                         % (re.escape(base), idx_pat), ln)
            if m:
                found = m.group(1).strip()
                break
            # ★ ГЕТТЕРЫ С ВЫХОДНЫМ ПАРАМЕТРОМ: `CObj_GetPosition(<чей объект>, буфер)` и
            #   `Class_GetVec3_86(<чей объект>, буфер)` пишут позицию В БУФЕР, присваивания нет.
            #   Источник — ПЕРВЫЙ аргумент (`pTargetChar->gap_1B0` = цель, `pChar` = кастер).
            #   Через них рождаются эффекты ветки `case 4:` помощников импакта (52 записи удара).
            m = re.search(r'(?:CObj_GetPosition|Class_GetVec3_86)\(\s*(.+?)\s*,\s*&?%s\s*\)'
                          % re.escape(base), ln)
            if m:
                found = m.group(1).strip()
                break
        # ★ Запасной проход БЕЗ индекса: переменная-указатель получает значение целиком
        #   (`Vec3_86 = Class_GetVec3_86(pChar, vec3Out);`), а читается поэлементно (`Vec3_86[1]`).
        if not found and index:
            for j in range(idx - 1, max(-1, idx - 120), -1):
                m = re.match(r'\s*\*?\(?[A-Za-z_ ]*\*?\)?\s*&?%s\s*=\s*(.+?);\s*$' % re.escape(base), body[j])
                if m:
                    found = m.group(1).strip()
                    break
        if not found:
            break
        if _origin_classify(found):
            return found
        cur = found
    return ""


# ★ ГДЕ В ВЫЗОВЕ ЛЕЖИТ ПОЗИЦИЯ (индекс первого из трёх аргументов, считая от 0):
#   `Effect_SpawnByName(mgr, имя, 1, x, y, z, слот, поза, кость, attach, 0, 1.0f)`      → 3
#   `CEffectMgr_CreateEffect_F561F0(mgr, имя, 1, x, y, z, 0, 1.0f)`                     → 3
#   `effectMgrCreateEffectEx(mgr, имя, 1, x, y, z, id, id, 13, 0, 1.0f)`                → 3
#   `effectMgrSpawnJEB(mgr, имя, 1, m00…m33, 0, 1.0f)` — ПОЛНАЯ МАТРИЦА 4×4, позиция это
#     ТРАНСЛЯЦИЯ (m30/m31/m32) ⇒ смещение 3 + 12 = 15. Без этой поправки у `effectMgrSpawnJEB`
#     разбирался ПЕРВЫЙ РЯД матрицы (масштаб/поворот) и источник не определялся никогда — ровно
#     это и оставляло 74 эффекта удара без точки рождения.
SPAWNER_POS_ARG = {
    "Effect_SpawnByName": 3,
    "CEffectMgr_CreateEffect_F561F0": 3,
    "effectMgrCreateEffectEx": 3,
    "effectMgrSpawnJEB": 15,
}


def _call_args(body, idx, spawner):
    """Аргументы вызова `spawner`, СКЛЕЕННЫЕ ПО СТРОКАМ.

    ⚠ Hex-Rays переносит длинные вызовы (`effectMgrSpawnJEB` с матрицей 4×4 — 21 аргумент), и разбор
      «в пределах одной строки» на них молча возвращал пусто: у `Skill_PlayHitEffectOnChar_Snd` и
      родственных так терялось 36 точек рождения эффекта удара.
    """
    line = body[idx]
    p = line.find(spawner)
    if p < 0:
        return []
    tail = line[line.find("(", p):]
    depth, buf, j = 0, "", idx
    while j < min(len(body), idx + 40):
        for ch in tail:
            if ch == "(":
                depth += 1
                if depth == 1:
                    continue
            elif ch == ")":
                depth -= 1
                if depth == 0:
                    return [a.strip() for a in _split_args(buf)]
            buf += ch
        j += 1
        tail = body[j] if j < len(body) else ""
    return []


def _split_args(s):
    """Разбить список аргументов по запятым ВЕРХНЕГО уровня (скобки внутри не режем)."""
    out, depth, cur = [], 0, ""
    for ch in s:
        if ch in "([":
            depth += 1
        elif ch in ")]":
            depth -= 1
        if ch == "," and depth == 0:
            out.append(cur)
            cur = ""
            continue
        cur += ch
    if cur.strip():
        out.append(cur)
    return out


def _resolve_literal(body, idx, arg):
    """Аргумент → ЧИСЛОВАЯ КОНСТАНТА (сам литерал либо ближайшее выше присваивание). None — не число."""
    a = arg.strip()
    if re.match(r'^\d+$', a):
        return int(a)
    m0 = re.match(r'([A-Za-z_]\w*)\s*(\[[^\]]*\])?', a)
    if not m0:
        return None
    base, index = m0.group(1), m0.group(2) or ""
    pat = re.compile(r'^\s*%s\s*%s\s*=\s*(\d+);\s*$' % (re.escape(base), re.escape(index)))
    for j in range(idx - 1, max(-1, idx - 60), -1):
        m = pat.match(body[j])
        if m:
            return int(m.group(1))
    return None


def _origin_near(body, idx, line, spawner):
    """Точка рождения эффекта для вызова `spawner` в строке `line` (индекс `idx` в теле)."""
    args = _call_args(body, idx, spawner)
    if not args:
        return ""
    p = SPAWNER_POS_ARG.get(spawner, 3)
    if len(args) < p + 3:
        return ""
    for a in args[p:p + 3]:
        k = _origin_classify(a) or _origin_classify(_resolve_var(body, idx, a))
        if k:
            return k
    return ""


# ★★★ РАСКЛАДКА ВЫЗОВА `effect_spawnDirectedFromTo` (проверена на всех ЧЕТЫРЁХ тиках-снарядоносцах).
#   Hex-Rays печатает КАЖДЫЙ vararg отдельной строкой, поэтому `aN` лежит ровно в строке `idx + N`
#   (строка `idx` — сам вызов, `idx+1` — носитель `a1`, `idx+2` — `double`, `idx+3` — `a3`):
#       a3  … a66  (256 б) — ИМЯ .jeb ПОЛЁТА  → `CEffect_LoadJEB_Wrapper(носитель+520, …)`
#       a67 … a82  (64 б)  — матрица кости запуска (её трансляция m30/m31/m32 = точка старта)
#       a83 … a85          — позиция цели (фиксируется на выстреле, наведения нет)
#       a86 / a87          — id цели / id эффекта урона
#       a88 … a151 (256 б) — ИМЯ .jeb УДАРА    → носитель `+8`
#       a152… a159 (32 б)  — ИМЯ .wav УДАРА    → носитель `+264`
#       a160 / a161 / a162 — controlId / skillId (`+1796`) / номер фазы (`+1878`)
#   Значения a3/a88/a152 приезжают локальными массивами, заполненными `qmemcpy` из поля слота
#   (`field_168`, `gap_268`, `gap_368`, `field_598`), из подсекции `.inl` либо из ЛИТЕРАЛА
#   (`strcpy(v46.pathBuf, "eff_1_char_07_runfirb04-a_00.jeb")` — навык 707, у него удар пофазный).
def _proj_arg_source(body, idx, argno):
    """Источник vararg-а `argno` вызова снаряда: поле слота / «inlPS» / «lit:<имя>» / «»."""
    if idx + argno >= len(body):
        return ""
    first = body[idx + argno].strip().rstrip(",);")
    m = re.match(r'([A-Za-z_]\w*)\[\d+\]', first)
    arr = m.group(1) if m else first
    for j in range(idx - 1, max(-1, idx - 60), -1):
        m = re.search(r'qmemcpy\(\s*&?%s(?:\[\d+\])?\s*,\s*(.+?)\s*,\s*(?:sizeof\([^)]*\)|0x[0-9A-Fa-f]+u?)\s*\)'
                      % re.escape(arr), body[j])
        if not m:
            continue
        src = m.group(1)
        f = _slot_of(src, PATH_FIELDS + SND_FIELDS)
        if f:
            return f
        if re.search(r'skillData\[524 \* \w+ \+ 6\]', src):
            return "inlPS"            # подсекция 2·фаза+3 записи SkillInfo.inl (ps = 3/5/7) — ПОЛЁТ
        if re.search(r'skillData\[524 \* \w+ \+ 530\]', src):
            return "inlPS1"           # подсекция 2·фаза+4 — УДАР
        # Имя собрано локально: ищем `strcpy(<var>.pathBuf, "…jeb")` выше по телу.
        var = src.strip().lstrip("&").split(",")[0].strip()
        for k in range(j - 1, max(-1, j - 40), -1):
            m2 = re.search(r'strcpy\(\s*%s\.pathBuf\s*,\s*"([^"]+)"\s*\)' % re.escape(var), body[k])
            if m2:
                return "lit:" + m2.group(1)
        return ""
    return ""


# Присваивание буфера аргументов из поля слота: `qmemcpy(v10, &слот.field_68, 0x100u)` либо
# со СМЕЩЕНИЕМ внутрь того же буфера: `qmemcpy(&v10[64], &слот.field_598, 0x20u)`.
RE_PROJ_BUF = re.compile(r'qmemcpy\(\s*&?([A-Za-z_]\w*)(?:\[(\d+)\])?\s*,\s*(.+?)\s*,'
                         r'\s*(?:sizeof\([^)]*\)|0x[0-9A-Fa-f]+u?)\s*\)')
# Ссылка на буфер в СТРОКЕ АРГУМЕНТА вызова: `v8[0],`.
RE_PROJ_ARG = re.compile(r'^&?([A-Za-z_]\w*)\[(\d+)\]$')


def _proj_fields(body, idx):
    """Вызов носителя снаряда → dict(flight=…, hit=…, snd=…): откуда берётся полёт/удар/звук.

    ★★★ РАЗБОР ОБОБЩЁН (2026-08-10). Раньше номера vararg-ов были ЗАШИТЫ (3 / 88 / 152) — числа,
    снятые с ОДНОГО носителя `effect_spawnDirectedFromTo`. У двух других носителей
    (`effect_spawnDirectedInstance`, `effectSpawnProjectileJeb`) и сигнатура короче, и буферы
    упакованы иначе: `effectSpawnAtBoneItem` кладёт в ОДИН буфер `v10` сразу два поля —
    `[0..63] = 0x68` и `[64..71] = 0x598`. Жёсткие номера там дают мусор.

    Механика разбора:
      1) выше вызова собираем карту «буфер[смещение] → поле слота» из всех `qmemcpy`;
      2) идём по СТРОКАМ АРГУМЕНТОВ вызова, каждая вида `<буфер>[N],`, и разворачиваем её в поле;
      3) ПЕРВОЕ путевое поле в порядке аргументов — ПОЛЁТ, ВТОРОЕ — УДАР, поле `0x598` — ЗВУК.
    Проверка на эталоне `effect_spawnDirectedFromTo` (навык 903): полёт `0x368`, удар `0x168`,
    звук `0x598` — ровно то же, что давали прежние жёсткие 3/88/152.
    """
    bufs = {}                                   # буфер → [(смещение_в_двордах, поле)]
    for j in range(idx - 1, max(-1, idx - 60), -1):
        m = RE_PROJ_BUF.search(body[j])
        if not m:
            continue
        var, off, src = m.group(1), int(m.group(2) or 0), m.group(3)
        f = _slot_of(src, PATH_FIELDS + SND_FIELDS)
        if not f:
            f = _proj_src_special(body, j, src)
        if f:
            bufs.setdefault(var, []).append((off, f))
    order = []
    for k in range(idx + 1, min(len(body), idx + 200)):
        s = body[k].strip().rstrip(",);")
        if not s:
            continue
        m = RE_PROJ_ARG.match(s)
        if not m:
            if body[k].rstrip().endswith(");"):
                break
            continue
        ent = bufs.get(m.group(1))
        if not ent:
            if body[k].rstrip().endswith(");"):
                break
            continue
        base = int(m.group(2))
        # Все поля этого буфера, начиная с указанного смещения, уезжают в вызов ПОДРЯД —
        # поэтому и «хвостовое» поле (звук в `v10[64]`) попадает в порядок аргументов.
        for off, f in sorted(ent):
            if off >= base and f not in order:
                order.append(f)
        if body[k].rstrip().endswith(");"):
            break
    if not order:
        # Резерв: прежние жёсткие номера vararg-ов полного носителя (тики массива 5).
        return dict(flight=_proj_arg_source(body, idx, 3),
                    hit=_proj_arg_source(body, idx, 88),
                    snd=_proj_arg_source(body, idx, 152))
    # ⚠ Источником полёта/удара бывает НЕ ТОЛЬКО поле слота: `CharFx_BuildSwordStormTrailEffect`
    #   (навык 910) и `CharFx_BuildStormTrapTrailEffect` (914) кладут имена ЛИТЕРАЛАМИ
    #   (`strcpy(jebPath1.pathBuf, "eff_1_char_06_swdstme02-a_00.jeb")`), а ветка `.inl` — номером
    #   подсекции. Фильтровать по PATH_FIELDS нельзя: именно из-за этого у 910/914 стояло
    #   «Proj=1, но записи @projectile нет». Звуковые поля исключаем явно, остальное — путь.
    path = [f for f in order if f not in ("0x598", "snd_cast")]
    snd = next((f for f in order if f == "0x598"), "")
    return dict(flight=path[0] if path else "", hit=path[1] if len(path) > 1 else "", snd=snd)


def _projnode_fields(body, idx):
    """`skillProjectile_createNode` → dict(flight/hit/snd) ИЗ ЛИТЕРАЛОВ тела (см. FX_PROJ_NODE).

    ПОЛЁТ — второй аргумент вызова (строка `idx+2`); УДАР и ЗВУК УДАРА — литералы ближайших
    `strcpy(&<буфер>[N], "…")` выше по телу (11 = `.jeb` удара, 75 = `.wav` удара; смещения на
    номера НЕ завязаны — берём по расширению, чтобы разбор не сломался о переименование локала).
    """
    flight = hit = snd = ""
    if idx + 2 < len(body):
        m = RE_JEB_LIT.search(body[idx + 2])
        if m:
            flight = "lit:" + m.group(1)
    for j in range(idx - 1, max(-1, idx - 60), -1):
        if "strcpy(" not in body[j]:
            continue
        m = RE_JEB_LIT.search(body[j])
        if m and not hit:
            hit = "lit:" + m.group(1)
        m = RE_WAV_LIT.search(body[j])
        if m and not snd:
            snd = "lit:" + m.group(1)
    return dict(flight=flight, hit=hit, snd=snd)


def _proj_src_special(body, j, src):
    """Источник буфера, не являющийся полем слота: подсекция `.inl` либо локально собранное имя."""
    if re.search(r'skillData\[524 \* \w+ \+ 6\]', src):
        return "inlPS"               # подсекция 2·фаза+3 записи SkillInfo.inl — ПОЛЁТ
    if re.search(r'skillData\[524 \* \w+ \+ 530\]', src):
        return "inlPS1"              # подсекция 2·фаза+4 — УДАР
    var = src.strip().lstrip("&").split(",")[0].strip()
    for k in range(j - 1, max(-1, j - 40), -1):
        m = re.search(r'strcpy\(\s*%s\.pathBuf\s*,\s*"([^"]+)"\s*\)' % re.escape(var), body[k])
        if m:
            return "lit:" + m.group(1)
    return ""


def _slot_near(body, idx, allow=PATH_FIELDS):
    """Поле слота из ближайшего ВЫШЕ присваивания вида `v = CObject_ReturnThis_965CB0(&…field_XXX)`.

    В эталоне имя эффекта почти всегда достаётся отдельной строкой, а в строку спавна попадает уже
    переменная — без этого «дотягивания» привязка «эффект → поле слота» терялась бы.
    """
    line = body[idx]
    var = ""
    m = re.search(r'CEffectMgr_CreateEffect_F561F0\([^,]+,\s*([A-Za-z_]\w*)|'
                  r'Effect_SpawnByName\([^,]+,\s*([A-Za-z_]\w*)|'
                  r'effectMgrSpawnJEB\([^,]+,\s*([A-Za-z_]\w*)|'
                  r'effectMgrCreateEffectEx\([^,]+,\s*([A-Za-z_]\w*)|'
                  r'sound3DPlay\w*\([^)]*?,\s*\(int\)([A-Za-z_]\w*)\)', line)
    if m:
        var = next((g for g in m.groups() if g), "")
    for j in range(idx - 1, max(-1, idx - 60), -1):
        ln = body[j]
        if var and not re.match(r'\s*%s\s*=' % re.escape(var), ln):
            continue
        s = _slot_of(ln, allow)
        if s and ("ReturnThis_965CB0" in ln or "CClass_GetThis" in ln):
            return s
    return ""


# ---------------------------------------------------------------- сборка пофазных колонок
#
# ★★★ APPEND_TYPE — СЕМАНТИКА ВОССТАНОВЛЕНА ЦЕЛИКОМ (2026-08-10).
#   Поле есть ТОЛЬКО у ветки `SkillInfo.inl` (`+1048*i + 268`); у хардкод-загрузчиков его нет в
#   принципе — они кладут имена сразу в поля слота. Читателей у APPEND_TYPE ровно два, и правила
#   у них РАЗНЫЕ:
#
#   1) `CharFx_SpawnEffectFromDesc@0xA75920` — эффекты СТАРТА (подсекции 0, 1, 9):
#        кость = APPEND_BONE_NAME, boneIdx = List_FindIndexByName(модель, кость)
#        boneIdx != -1 и at ∈ {7, 10} → Effect_SpawnByName(…, позаКости, boneIdx, attach 7)  = НА КОСТИ
#        at == 6                      → Effect_SpawnByName(…, позаПерсонажа, attach 6)       = СЛЕДУЕТ ЗА ПЕРСОНАЖЕМ
#        at != 0 (прочие: 1, 2, …)    → то же, attach принудительно 6                        = СЛЕДУЕТ ЗА ПЕРСОНАЖЕМ
#        at == 0                      → effectMgrSpawnJEB(…, МИРОВАЯ МАТРИЦА персонажа)      = СВОБОДНЫЙ, НЕ СЛЕДУЕТ
#        затем звук SOUND_FILE_NAME_A с задержкой SOUND_FILE_PLAY_DELAY_A в 3D-очередь.
#   2) `CEffect_UpdateSkillCast@0xA5A060` — эффект УДАРА снаряда (подсекции 4, 6, 8),
#      `skillRow[262*(2*фаза+4) + 67]` — та же ячейка +268:
#        at == 0 → CEffectMgr_CreateEffect_F561F0 в позиции цели                 = СВОБОДНЫЙ У ЦЕЛИ
#        at == 1 → effectMgrCreateEffectEx(…, controlId цели, attach 13)         = ПРИКРЕПЛЁН К ЦЕЛИ
#        at == 2 → луч «кастер→цель» × AABB цели, effectMgrSpawnJEB в точке      = В ТОЧКЕ ПОПАДАНИЯ
#        в конце: CEffectMgr_scaleAll(fx, масштабЦели/100) — эффект удара МАСШТАБИРУЕТСЯ ПО ЦЕЛИ.
# ★ Точка рождения записана прямо в привязке («привязка:источник»): у эффектов СТАРТА источник
#   всегда КАСТЕР (кость оружия либо его мировая матрица), у эффектов УДАРА снаряда — всегда ЦЕЛЬ
#   (позиция цели / attach 13 к цели / точка луча × AABB цели).
APPEND_TYPE_CAST = {0: "world:caster", 6: "follow:caster", 7: "bone:caster", 10: "bone:caster"}
APPEND_TYPE_HIT = {0: "world:target", 1: "attach:target", 2: "hitpoint:target"}


def _noroute_text(t):
    """Запись `noroute` → строка отчёта. Разобранные случаи (KNOWN_NOROUTE) идут БЕЗ крика."""
    if t in KNOWN_NOROUTE.values():
        return ""          # случай разобран до конца (см. KNOWN_NOROUTE) — в отчёт не выносим
    return "НЕ РАЗОБРАН СПАВН: %s → %s: %s" % t


def _fx_join(pairs):
    """[(имя, привязка[, видЦели])] → «имя@привязка/видЦели;…» без повторов, порядок сохранён."""
    # Дубли схлопываются по паре «имя + вид цели»: один и тот же `.jeb` эталон спавнит в нескольких
    # ветках одного блока (луч попал в AABB / не попал; `CreateEffect_F561F0` / `effectMgrSpawnJEB`),
    # но эффект от этого ОДИН. Побеждает ПЕРВАЯ привязка — она же первая в порядке кода.
    out, seen = [], set()
    for p in pairs:
        name, anchor = p[0], p[1]
        kind = p[2] if len(p) > 2 and p[2] else ""
        key = (name.lower(), kind)
        if key in seen:
            continue
        seen.add(key)
        out.append("%s@%s%s" % (name, anchor, "/" + kind if kind else ""))
    return ";".join(out)


def build_fx_hardcode(prog, jebs, jslots, wavs, wslots):
    """Джойн «загрузчик разложил имена по полям» × «тик читает поля по фазам» → колонки показа.

    Загрузчик даёт ПОЛЕ → ИМЯ, тик даёт ПОЛЕ → КОГДА/КУДА. Ни одна из половин по отдельности
    не отвечает на вопрос «что играет в момент удара» — отсюда и была дыра «импакта не видно».
    """
    byfield = {}
    for name, f in zip(jebs, jslots):
        if f:
            byfield.setdefault(f, []).append(name)
    # ★ 10.11.2026 (job4-vfx): имя, которое тик пишет в поле сам (`prog["tickset"]`, см. разбор тика), ПЕРЕЗАПИСЫВАЕТ поле на пороге
    #   удара — помощник удара читает уже его, поэтому оно заменяет имя загрузчика (ревизия 10.11: дописывание давало «fatsth01;fatale03»).
    for f, names_t in (prog.get("tickset") or {}).items():
        byfield[f] = list(names_t)
    sndfield = {}
    for name, f in zip(wavs, wslots):
        if f:
            sndfield.setdefault(f, name)

    def names(f):
        if f.startswith("lit:"):
            return [f[4:]]                            # имя пришло литералом из тела тика
        return byfield.get(f, [])

    used = set()
    cast, hits = [], {0: [], 1: [], 2: []}
    bone = ""
    for f, anchor, bn in prog["cast"]:
        bone = bone or bn
        for n in names(f):
            cast.append((n, anchor))
            used.add(f)
    # ★★★ СНАРЯД ПОФАЗНО. Носитель — не «привязка», а отдельная сущность: он несёт ВИЗУАЛ ПОЛЁТА
    #   (поле `flight`), ЭФФЕКТ УДАРА (`hit`) и ЗВУК УДАРА (`snd`) и показывает их ПО ПРИЛЁТУ.
    #   До этой правки колонка `Proj` стояла у 90 строк, а имя `.jeb` полёта было лишь у 2 — потому
    #   что имя лежит в ПОЛЕ СЛОТА (0x168 у навыков 701/707, 0x268/0x368 у 903), а разбор его не брал.
    proj_snd = ""
    for ph, pf in sorted((prog.get("projfx") or {}).items()):
        if ph not in hits:
            continue
        for n in names(pf.get("flight") or ""):
            hits[ph].append((n, "projectile", ""))
            used.add(pf["flight"])
        for n in names(pf.get("hit") or ""):
            # Эффект удара снаряда рождается У ЦЕЛИ (`CEffect_UpdateSkillCast@0xA5A060`).
            hits[ph].append((n, "world:target", ""))
            used.add(pf["hit"])
        if pf.get("snd") and not proj_snd:
            proj_snd = pf["snd"]
    for ph in (0, 1, 2):
        for entry in prog["hit"].get(ph, []):
            f, anchor, tk = entry[0], entry[1], entry[2] if len(entry) > 2 else ""
            for n in names(f):
                hits[ph].append((n, anchor, tk))
                used.add(f)
    unknown = []
    for entry in prog["hitany"] + [(f, a, "") for f, a in prog["unknown"]]:
        f, anchor = entry[0], entry[1]
        for n in names(f):
            unknown.append("%s@%s(фаза не определилась)" % (n, anchor))
            used.add(f)
    # ★ Закон jeb-emitter-routing-hole: поле, которое загрузчик ЗАПОЛНИЛ, а тик НИ РАЗУ не читает,
    #   обязано попасть в отчёт, а не потеряться.
    for f in sorted(byfield):
        if f not in used:
            unknown.append("%s(поле %s — читателей в теле тика нет)" % (";".join(byfield[f]), f))
    # ★★★ И ОБРАТНОЕ: вызов спавнера, чей источник имени не распознан, тоже идёт в отчёт.
    for t in prog.get("noroute", []):
        txt = _noroute_text(t)
        if txt:
            unknown.append(txt)
    # Звук-литерал из тела тика дополняет (не подменяет) звук из поля слота.
    lit_cast = (prog.get("sndlit") or {}).get("cast") or (prog.get("sndlit") or {}).get("any", "")
    lit_hit = (prog.get("sndlit") or {}).get("hit", "")
    snd_cast = sndfield.get("snd_cast", "")
    if lit_cast and lit_cast.lower() != snd_cast.lower():
        snd_cast = ";".join(x for x in (snd_cast, lit_cast) if x)
    # Звук удара: поле слота 0x598, либо звук, приехавший В НОСИТЕЛЕ снаряда (полем или литералом).
    snd_hit = sndfield.get("0x598", "")
    if not snd_hit and proj_snd:
        snd_hit = proj_snd[4:] if proj_snd.startswith("lit:") else sndfield.get(proj_snd, "")
    if lit_hit and lit_hit.lower() != snd_hit.lower():
        snd_hit = ";".join(x for x in (snd_hit, lit_hit) if x)
    return dict(cast=_fx_join(cast), bone=bone,
                hit0=_fx_join(hits[0]), hit1=_fx_join(hits[1]), hit2=_fx_join(hits[2]),
                snd_cast=snd_cast, snd_hit=snd_hit,
                note="; ".join(unknown), proj=bool(prog.get("projfx")))


def build_fx_inl(rec):
    """Пофазная раскладка ветки `SkillInfo.inl` — по `Char_UpdateSkillAnim_MultiPhase@0xA76E70`.

    Раскладка снята из арифметики самого эталона (`skillData` — `_WORD *`, шаг подсекции 1048 б):
        skillData[524*ps + 2  ] = байт 1048*ps + 4     — флаг «есть эффект» подсекции ps  (гейт фазы)
        skillData[524*ps + 6  ] = байт 1048*ps + 12    — FILE_NAME подсекции ps           = СНАРЯД
        skillData[524*ps + 530] = байт 1048*(ps+1)+12  — FILE_NAME подсекции ps+1         = УДАР
        skillData[524*ps + 790] = байт 1048*(ps+1)+532 — SOUND_FILE_NAME_A подсекции ps+1 = ЗВУК УДАРА
      где ps = 3, 5, 7 для фаз 0, 1, 2  ⇒  ПАРЫ подсекций (3,4), (5,6), (7,8).
    Подсекции 0, 1, 9 спавнятся на первом кадре через `CharFx_SpawnEffectFromDesc` (APPEND_TYPE выше).
    Подсекции 2 и 10 в этом тике не читаются — «читателей в срезе нет» (dead-code-markings-suspect).
    """
    b = {x["idx"]: x for x in rec["blocks"]}

    def at(i, table):
        """APPEND_TYPE подсекции i → «привязка:источник».

        Дефолт разный у двух читателей и снят с эталона:
          · старт (`CharFx_SpawnEffectFromDesc`): `else if (at != 0)` → attach 6 = `follow:caster`;
          · удар снаряда (`CEffect_UpdateSkillCast`): веток ровно три (0/1/2), прочих значений в
            данных нет — оставляем `world:target` и это видно в колонке.
        """
        dflt = "follow:caster" if table is APPEND_TYPE_CAST else "world:target"
        try:
            return table.get(int(b[i].get("append_type") or 0), dflt)
        except (ValueError, KeyError):
            return dflt

    cast = [(b[i]["file"], at(i, APPEND_TYPE_CAST)) for i in (0, 1, 9) if i in b]
    hits, note = {}, []
    for ph, ps in ((0, 3), (1, 5), (2, 7)):
        e = []
        if ps in b:
            e.append((b[ps]["file"], "projectile"))          # летящий носитель: старт с кости, dist/2.5
        if ps + 1 in b:
            e.append((b[ps + 1]["file"], at(ps + 1, APPEND_TYPE_HIT)))
        hits[ph] = e
    # ★★★ ПОДСЕКЦИИ 2 И 10 — AoE-ЭФФЕКТ УДАРА (добрано 2026-08-10, fxcoverage-agent; см. KNOWN_NOROUTE).
    #   `Char_UpdateSkillAnim_MultiPhase` на пороге `slot.color` БЕЗ индекса фазы (⇒ ФАЗА 0) зовёт
    #   `CSkillCast_ApplyAoEHitEffects`, а тот проходит по 20 целям и спавняет FILE_NAME подсекций
    #   2 и 10 по их собственному `APPEND_TYPE` (0 world / 1 attach / 2 hitpoint у ЦЕЛИ).
    #   До этой правки они отчитывались как «читателей в срезе нет» — неверно.
    # ★ 04.11.2026 [ОРИГИНАЛ]: у фазы 0 СО СНАРЯДОМ (подсекция 3; в данных — только 820) площадной удар — отдельный момент
    #   (порог фазы, до вылета), а удар носителя — по прилёте. Вид «aoe» не даёт `_fx_join` схлопнуть одноимённые записи и говорит
    #   FirePhase родить её до вылета. Без снаряда вид не ставится — строки прочих навыков не меняются.
    aoe_kind = "aoe" if 3 in b else ""
    for i in (2, 10):
        if i in b:
            hits[0].append((b[i]["file"], at(i, APPEND_TYPE_HIT), aoe_kind))
    for i in sorted(b):
        if i in (0, 1, 9, 3, 4, 5, 6, 7, 8):
            continue
        if i in (2, 10):
            continue                       # AoE-удар фазы 0 — уже в hits[0], см. блок выше
        note.append("подсекция %d (%s) — читателей в срезе нет" % (i, b[i]["file"]))
    snd_hit = ""
    # ★ Подсекции 2 и 10 добавлены 2026-08-10: `CSkillCast_ApplyAoEHitEffects` ставит в очередь
    #   команду со звуком `SOUND_FILE_NAME_A` (`+266` = байт +532) и `_B` (`+396` = байт +792)
    #   ИМЕННО этих подсекций — это звук AoE-удара.
    for ps in (4, 6, 8, 2, 10):
        if ps in b and b[ps].get("wav_a"):
            snd_hit = b[ps]["wav_a"]
            break
    snd_cast = ""
    for i in (0, 1, 9):
        if i in b and b[i].get("wav_a"):
            snd_cast = b[i]["wav_a"]
            break
    # ★★★ ГЕЙТ СНАРЯДА — ДАННЫЕ, А НЕ ТЕЛО ТИКА. `Char_UpdateSkillAnim_MultiPhase@0xA76E70` —
    #   ОДНА функция на все 38 навыков ветки `.inl`, и ветка снаряда в ней закрыта проверкой
    #   `if (LOBYTE(skillData[524 * phaseSlot + 2]))` = «у подсекции ps есть FILE_NAME».
    #   В данных подсекция 3/5/7 есть ровно у ОДНОГО навыка этой ветки — 820 «Меткий стрелок».
    #   Прежний `Proj` брался из тела тика и потому стоял у всех 38 (76 строк) — ложно.
    return dict(cast=_fx_join(cast), bone="",
                hit0=_fx_join(hits[0]), hit1=_fx_join(hits[1]), hit2=_fx_join(hits[2]),
                snd_cast=snd_cast, snd_hit=snd_hit, note="; ".join(note),
                proj=any(ps in b for ps in (3, 5, 7)))


# ---------------------------------------------------------------- combo time.bin
def load_combo_time(path):
    """`combo time.bin` → {имя_клипа_lower: dict}. Кадры ×1000/30 = миллисекунды."""
    if not os.path.exists(path):
        return {}
    b = open(path, "rb").read()
    ver = struct.unpack_from("<f", b, 0)[0]
    count = struct.unpack_from("<i", b, 4)[0]
    out = {}
    base = 20
    rec = 156
    for i in range(count):
        o = base + rec * i
        if o + rec > len(b):
            break
        name = b[o + 8:o + 48].split(b"\0")[0].decode("cp1251", errors="replace").strip()
        if not name:
            continue
        window = struct.unpack_from("<f", b, o + 44)[0]
        dur = struct.unpack_from("<9f", b, o + 48)
        hit = struct.unpack_from("<9f", b, o + 84)
        out[name.lower()] = dict(
            idx=i, name=name, window=window * FRAME_MS,
            dur=[round(x * FRAME_MS, 1) for x in dur[:3]],
            hit=[round(x * FRAME_MS, 1) for x in hit[:3]],
        )
    out["__meta__"] = dict(version=ver, count=count)
    return out


# ---------------------------------------------------------------- SkillInfo.inl
def load_skillinfo_inl(path):
    """`system\\SkillInfo.inl` → {код навыка: запись}. ВТОРОЙ источник имени клипа.

    ★ ЗАЧЕМ. 38 навыков (611-616, 632-639, 820, 823-825, 841-847, 1041-1046, 1062-1068) имеют в
    массиве 0 ОДИН И ТОТ ЖЕ загрузчик `charLoadSkillEffectMotion@0xA75590`, в теле которого имени
    клипа нет: он берёт его из записи по `EffectSlotByType[64*(gender-1) + 2883]`
    (`entity_char.c:29389`). Что это за структура — разобрано ДО КОНЦА по
    `CSkillMgr_LoadSkillInfoInl` (`skill_buff.c:16405-16465`), раскладка сходится побайтово:

        запись = operator new(0x2F18)          = 12056 б
        +0                       skillId
        +1048*i + 4   (i=0..10)  флаг «есть эффект» (1, если FILE_NAME непустой)
        +1048*i + 8              PRESENT_TIME
        +1048*i + 12             FILE_NAME            (CPackPath, 256 б)
        +1048*i + 268            APPEND_TYPE
        +1048*i + 272            APPEND_BONE_NAME     (256 б)
        +1048*i + 528/532        SOUND_FILE_PLAY_DELAY_A / SOUND_FILE_NAME_A
        +1048*i + 788/792        SOUND_FILE_PLAY_DELAY_B / SOUND_FILE_NAME_B
        +1048*i + 1048           SPECIALEFFECT
        +11532                   MALE_ANI_NAME        (256 б)  = dword-индекс 2883  ← ТО САМОЕ
        +11788                   FEMALE_ANI_NAME      (256 б)  = 2883 + 64          ← ТО САМОЕ
        +12044/12048/12052       COMBO0 / COMBO1 / COMBO2      = dword 3011/3012/3013
      11532 + 256 + 256 + 12 = 12056 = 0x2F18 — зазора нет, раскладка полная.

    ★ ЕДИНИЦЫ. COMBO0..2 здесь УЖЕ МИЛЛИСЕКУНДЫ и кладутся в слот как `(float)(int)` без пересчёта
      (`entity_char.c:29472`). Это согласовано с `combo time.bin`, у которой кадры переводятся в мс
      ЕЩЁ ПРИ ЗАГРУЗКЕ (`CComboManager_LoadComboTime@0x9BC5F0`, item_inventory.c:428-435: все поля
      +44/+48..+84..+120 умножаются на 33.333328). Пересчёт ×1000/30 в этом скрипте повторяет именно
      его — поэтому обе колонки Combo*Ms однородны.

    ★ ОХВАТ. Клиент читает ТОЛЬКО секции с номерами 600..1099 (`for (skillId = 600; skillId < 1100)`)
      и только при `EXIST == 1` — здесь ровно та же граница, ничего сверх файла не добавляется.
    """
    if not os.path.exists(path):
        return {}
    raw = open(path, "rb").read().decode("cp949", errors="replace")
    sect = {}
    cur = None
    for line in raw.replace("\r\n", "\n").split("\n"):
        s = line.split(";")[0].strip()      # ';' — комментарий, как у GetPrivateProfileString
        if not s:
            continue
        m = re.match(r"^\[([0-9\-]+)\]$", s)
        if m:
            cur = m.group(1)
            sect[cur] = {}
            continue
        if cur and "=" in s:
            k, v = s.split("=", 1)
            sect[cur][k.strip().upper()] = v.strip()
    out = {}
    for name, kv in sect.items():
        if "-" in name:
            continue
        sid = int(name)
        if sid < 600 or sid >= 1100 or kv.get("EXIST") != "1":
            continue
        blocks = []
        for i in range(11):                                   # ровно 11 подсекций, как в эталоне
            b = sect.get("%d-%d" % (sid, i))
            if not b:
                continue
            fn = b.get("FILE_NAME", "")
            if not fn:                                        # флаг «есть эффект» = FILE_NAME непуст
                continue
            blocks.append(dict(idx=i, file=fn,
                               append_type=b.get("APPEND_TYPE", ""),
                               bone=b.get("APPEND_BONE_NAME", ""),
                               wav_a=b.get("SOUND_FILE_NAME_A", ""),
                               wav_b=b.get("SOUND_FILE_NAME_B", "")))
        wavs = []
        for i in range(11):
            b = sect.get("%d-%d" % (sid, i)) or {}
            for k in ("SOUND_FILE_NAME_A", "SOUND_FILE_NAME_B"):
                if b.get(k):
                    wavs.append(b[k])
        out[sid] = dict(
            male=kv.get("MALE_ANI_NAME", ""), female=kv.get("FEMALE_ANI_NAME", ""),
            combo=[float(kv.get("COMBO%d" % i, "0") or 0) for i in range(3)],
            blocks=blocks, wavs=_dedup(wavs),
        )
    return out


# ---------------------------------------------------------------- skill_info.sif
def load_sif(path):
    """skill_info.sif → {id: имя навыка}. Пустая запись = LOWORD == -1."""
    if not os.path.exists(path):
        return {}
    b = open(path, "rb").read()
    out = {}
    for i in range(len(b) // SIF_REC):
        rec = b[i * SIF_REC:(i + 1) * SIF_REC]
        if struct.unpack_from("<h", rec, 0)[0] == -1:
            continue
        z = rec.find(b"\0", SIF_NAME)
        if z < 0:
            z = len(rec)
        out[i] = rec[SIF_NAME:z].decode("cp1251", errors="replace")
    return out


# ---------------------------------------------------------------- вывод
def main():
    if not os.path.exists(EXE):
        print("НЕТ ФАЙЛА: %s" % EXE)
        return 1
    size = os.path.getsize(EXE)
    if size != EXE_SIZE:
        print("ВНИМАНИЕ: размер %d != эталонных %d — сверка не гарантирована" % (size, EXE_SIZE))
    pe = Pe(EXE)
    raw = parse_dispatch(pe)
    # ★★★ Второй сайт регистрации — навыки СОУЛМЕЙТА (1287..1300). Порядок продолжаем сквозным:
    #   `CSkillMgr_LoadSkillData` зовёт его ПОСЛЕ основного, значит при конфликте слота побеждает он.
    raw_soul = parse_dispatch(pe, FN_SOUL_DISPATCH, order0=len(raw))
    print("соулмейт-таблица charFxInitSoulSkillVtbl@0x%X: %d регистраций" % (FN_SOUL_DISPATCH, len(raw_soul)))
    raw = raw + raw_soul
    names, bodies = load_names()
    combo = load_combo_time(COMBO_BIN)
    sifnames = load_sif(SIF)
    inl = load_skillinfo_inl(INL)

    table = {}          # skillId -> {k: (имя, адрес)}
    stray = []          # записи вне 7 массивов — перечисляются честно
    dead = {}           # skillId -> число МЁРТВЫХ перезаписей (слот уже был занят)
    filled = set()      # занятые слоты (массив, индекс)
    for order, disp, val in raw:
        real = pe.follow_thunk(val)
        nm = names.get(real, "sub_%X" % real)
        k, i = split_slot(disp)
        if k is None or k >= CB_ARRAYS:
            stray.append((order, disp, real, nm, k, i))
            continue
        if (k, i) in filled:
            dead[i] = dead.get(i, 0) + 1        # «побеждает последняя» — эта запись затирается
        filled.add((k, i))
        table.setdefault(i, {})[k] = (nm, real)

    os.makedirs(REPORTS, exist_ok=True)

    # --- 1) таблица диспетчера
    #     DeadWrites — сколько регистраций этого навыка затёрты последующими (в рантайме их нет).
    #     Note — почему строку нельзя трактовать как обычный навык.
    with open(OUT_DISPATCH, "w", encoding="utf-8") as f:
        f.write("SkillId\tSkillName\t" + "\t".join("%s(%d)" % (CB_ROLE[k], k) for k in range(CB_ARRAYS))
                + "\tDeadWrites\tNote\n")
        for sid in sorted(table):
            cells = []
            for k in range(CB_ARRAYS):
                cells.append(table[sid][k][0] if k in table[sid] else "")
            note = ""
            if sid == STUB_SKILL:
                note = ("СЛОТ-ЗАГЛУШКА: имя в skill_info.sif — «Нет», на индекс приходится %d "
                        "регистраций; выжившие функции случайны, в таблицы клипов не идёт" % (dead.get(sid, 0) + len(table[sid])))
            elif table[sid].get(0) and table[sid][0][0] == INL_LOADER:
                note = "клип/эффекты из SkillInfo.inl (в теле загрузчика их нет)"
            f.write("%d\t%s\t%s\t%d\t%s\n" % (sid, sifnames.get(sid, ""), "\t".join(cells),
                                              dead.get(sid, 0), note))
    print("записано %s (%d навыков)" % (OUT_DISPATCH, len(table)))

    # --- 2) таблица клипов
    clipmap = {}
    if os.path.exists(CLIP_MAP):
        clipmap = json.load(open(CLIP_MAP, encoding="utf-8"))

    def weapon_types(cond, sel):
        """Условие выбора клипа → список подтипов оружия в ДЕСЯТИЧНОМ виде.

        Заполняется ТОЛЬКО когда селектор действительно `equippedWeaponType` (switch или прямое
        сравнение) — иначе пусто, а сырое условие остаётся в колонке Cond (закон «не выдумывать»).
        `*` = ветка default (любое прочее оружие).
        """
        if "equippedWeaponType" not in (sel or "") and "equippedWeaponType" not in (cond or ""):
            return ""
        vals = []
        for m in re.finditer(r"case ([\w|]+)", cond or ""):
            for lab in m.group(1).split("|"):
                if lab == "default":
                    vals.append("*")
                else:
                    try:
                        vals.append(str(int(lab, 0)))
                    except ValueError:
                        vals.append(lab)
        if not vals:
            for m in re.finditer(r"equippedWeaponType\s*==\s*(\w+)", cond or ""):
                try:
                    vals.append(str(int(m.group(1), 0)))
                except ValueError:
                    pass
        return "|".join(vals)

    # ★ ШАПКА И СТРОКА СОБИРАЮТСЯ ИЗ ОДНОГО СПИСКА. Раньше шапка TSV писалась вручную и отстала от
    #   строки на две колонки (не было AnimIdx и WeaponTypes) — любой разбор по именам съезжал с 7-й
    #   колонки. Теперь рассинхрон невозможен по построению.
    #
    # ★★★ ПОФАЗНЫЕ КОЛОНКИ (добавлены 2026-08-10). До них порт вываливал ВСЕ `.jeb` навыка на
    #   старте показа — поэтому «импакта не видно»: эффект удара играл в момент замаха и в позиции
    #   кастера. Теперь роль каждого `.jeb` снимается из ТЕЛА ТИКА (`analyse_tick`), а не угадывается.
    #     FxCast    — эффекты, спавнящиеся на ПЕРВОМ КАДРЕ; формат `имя@привязка`
    #                 (`bone` = кость `8_a_End`/`8_b1`, `char` = позиция персонажа, `world`, `matrix`)
    #     FxCastBone— имя кости из тика (литерал `List_FindIndexByName(..., "8_a_End")`)
    #     FxHit0..2 — эффекты УДАРА фаз 0/1/2 (пороги COMBO0/1/2); формат `имя@привязка/видЦели`,
    #                 `видЦели` = `mob` (сущность) / `char` (игрок) / пусто (ветка общая)
    #     SndCast   — .wav каста (поле `pEffectSoundObj`), SndHit — .wav удара (поле `+0x598`)
    #     Proj      — 1, если тик заводит НОСИТЕЛЬ СНАРЯДА (урон едет в нём, показывается по прилёте)
    #     TickFn    — имя тик-функции массива 5 (для сверки с эталоном)
    #     FxNote    — ЧЕСТНАЯ пометка: что в этой строке механически НЕ разобралось
    COLS = ["SkillId", "SkillName", "Loader", "LoaderAddr", "Clip", "Gender", "AnimIdx", "WeaponTypes",
            "ComboKey", "Cond", "Selector", "Combo0Ms", "Combo1Ms", "Combo2Ms", "PhaseDurMs",
            "Sounds", "Jebs", "JebSlots", "Source",
            "TickFn", "FxCast", "FxCastBone", "FxHit0", "FxHit1", "FxHit2",
            "SndCast", "SndHit", "Proj", "ProjDelay", "TargetSize", "FxNote"]
    # В CSV (DataTable) колонка LoaderAddr не нужна — она чисто отчётная.
    CSV_COLS = [c for c in COLS if c != "LoaderAddr"]

    lines = []
    csv = ["_row," + ",".join(CSV_COLS)]
    nrow = 0
    nclip = 0
    noclip = []
    ninl = 0

    def emit(row):
        """row — словарь по именам COLS. Пишет строку и в TSV, и в CSV."""
        nonlocal nrow
        nrow += 1
        lines.append("\t".join(str(row.get(c, "")) for c in COLS))
        csv.append(",".join('"%s"' % str(x).replace('"', '""')
                            for x in [nrow] + [row.get(c, "") for c in CSV_COLS]))

    for sid in sorted(table):
        ent = table[sid].get(0)
        if not ent:
            continue
        if sid == STUB_SKILL:
            continue                       # слот-заглушка (см. шапку) — в таблицу клипов не идёт
        loader, addr = ent

        # ── ветка A: имя клипа из SkillInfo.inl (загрузчик общий, тела с именем нет) ──────────────
        if loader == INL_LOADER:
            rec = inl.get(sid)
            if not rec:
                noclip.append((sid, loader))
                continue
            ninl += 1
            jebs = _dedup([b["file"] for b in rec["blocks"]])
            # Слот здесь — НОМЕР ПОДСЕКЦИИ .inl (`[id-N]`), а не поле 0x68/0x168: у этого пути
            # эффекты живут в самой записи, и тик берёт их по индексу подсекции
            # (`Char_UpdateSkillAnim_MultiPhase`: подсекции 0/1/9 на старте, 3/5/7 — пороги фаз).
            jslots = []
            seen_j = set()
            for b in rec["blocks"]:
                if b["file"] in seen_j:
                    continue
                seen_j.add(b["file"])
                jslots.append("inl%d" % b["idx"])
            tickfn = table[sid].get(5, ("", 0))[0]
            fx = build_fx_inl(rec)
            prog = analyse_tick(tickfn, bodies) if tickfn else None
            for cbk in (3, 1, 4):
                if prog and table[sid].get(cbk):
                    analyse_tick(table[sid][cbk][0], bodies, 1, prog, set(),
                                 dict(first=True, hit=False, conds=""))
            # ★ Закон jeb-emitter-routing-hole действует и в ветке `.inl`: нераспознанный вызов
            #   спавнера обязан быть виден в отчёте, хотя раскладка здесь идёт по данным записи.
            if prog and prog.get("noroute"):
                fx["note"] = "; ".join(x for x in [fx["note"]]
                                       + [_noroute_text(t) for t in prog["noroute"]] if x)
                # пустые строки (разобранные случаи) отфильтрованы условием `if x` выше
            for gender, clip in (("m", rec["male"]), ("f", rec["female"])):
                if not clip:
                    continue
                nclip += 1
                base = os.path.splitext(clip)[0]
                emit(dict(SkillId=sid, SkillName=sifnames.get(sid, ""), Loader=loader,
                          LoaderAddr="0x%X" % addr, Clip=clip, Gender=gender,
                          AnimIdx=clipmap.get(gender, {}).get(base, -1), WeaponTypes="",
                          ComboKey="", Cond="", Selector="",
                          Combo0Ms=rec["combo"][0], Combo1Ms=rec["combo"][1], Combo2Ms=rec["combo"][2],
                          PhaseDurMs="", Sounds=";".join(rec["wavs"]),
                          Jebs=";".join(jebs), JebSlots=";".join(jslots), Source="SkillInfo.inl",
                          TickFn=tickfn, FxCast=fx["cast"], FxCastBone=fx["bone"],
                          FxHit0=fx["hit0"], FxHit1=fx["hit1"], FxHit2=fx["hit2"],
                          SndCast=fx["snd_cast"], SndHit=fx["snd_hit"],
                          Proj=1 if fx["proj"] else 0,
                          ProjDelay=1 if (prog and prog.get("projdelay")) else 0,
                          FxNote=fx["note"]))
            continue

        # ── ветка B: имя клипа зашито в тело загрузчика ───────────────────────────────────────────
        body = bodies.get(loader, [])
        clips, sounds, jebs, jslots, wslots = analyse_loader(body)
        if not clips:
            noclip.append((sid, loader))
        # ★ ПРОГРАММА ПОКАЗА — из тела ТИКА (массив 5). Именно она отвечает на вопрос
        #   «что играет на старте, а что в момент удара»; загрузчик этого не знает.
        tickfn = table[sid].get(5, ("", 0))[0]
        prog = analyse_tick(tickfn, bodies) if tickfn else None
        # ★ Массив 3 — НАЧАЛО ПОКАЗА, а не «сброс полей»: `effectPlayGenderSoundCastBone@0xA85800`
        #   и его близнецы играют ЗВУК КАСТА (`pEffectSoundObj`, канал 20..27 по полу) и перематывают
        #   анимобъект (`CObject_SetField13(&slot.field_8, 0)`). Без разбора этой ветки звук каста
        #   у 501 и ему подобных выпадал из таблицы.
        # ★ Массивы 1 и 4 — тоже СПАВНЕРЫ МОМЕНТА ПРИМЕНЕНИЯ, а не служебные:
        #   1 (`sub_A811A0@0xA811A0`, `this[skillId+1357](x, y+20, z)`) — эффект в МИРОВОЙ ПОЗИЦИИ
        #     персонажа (`skillFx_SPskill047`: Effect_SpawnByName(…, attach 6));
        #   4 (`sub_A85A20@0xA85A20`) — эффект НА КОСТИ ПРЕДМЕТА (`effectSpawnAtBoneItem`).
        #   У 78 навыков тик (массив 5) не спавнит ничего вообще — весь визуал живёт именно здесь;
        #   без разбора этих двух веток их эффекты выглядели бы «без читателей».
        for cbk in (3, 1, 4):
            if prog and table[sid].get(cbk):
                analyse_tick(table[sid][cbk][0], bodies, 1, prog, set(),
                             dict(first=True, hit=False, conds=""))
        seen = set()
        for c in clips:
            sig = (c["clip"], c["cond"], c["sel"])
            if sig in seen:
                continue
            seen.add(sig)
            nclip += 1
            base = os.path.splitext(c["clip"])[0]
            ct = combo.get((c["key"] or base).lower()) or combo.get(base.lower())
            g = re.search(r"_([mf])_", c["clip"])
            gender = g.group(1) if g else ""
            cj = c["jebs"] or jebs
            cs = c["jslots"] or jslots
            cw = c["wavs"] or sounds
            cws = c["wslots"] or wslots
            if prog:
                fx = build_fx_hardcode(prog, cj, cs, cw, cws)
            else:
                fx = dict(cast="", bone="", hit0="", hit1="", hit2="", snd_cast="", snd_hit="",
                          proj=False,
                          note="тика (массив 5) у навыка нет — программа показа неизвестна")
            emit(dict(SkillId=sid, SkillName=sifnames.get(sid, ""), Loader=loader,
                      LoaderAddr="0x%X" % addr, Clip=c["clip"], Gender=gender,
                      AnimIdx=clipmap.get(gender, {}).get(base, -1),
                      WeaponTypes=weapon_types(c["cond"], c["sel"]), ComboKey=c["key"],
                      Cond=c["cond"], Selector=c["sel"],
                      Combo0Ms=ct["hit"][0] if ct else "", Combo1Ms=ct["hit"][1] if ct else "",
                      Combo2Ms=ct["hit"][2] if ct else "", PhaseDurMs=ct["dur"][0] if ct else "",
                      Sounds=";".join(cw),
                      Jebs=";".join(cj), JebSlots=";".join(cs), Source="хардкод-загрузчик",
                      TickFn=tickfn, FxCast=fx["cast"], FxCastBone=fx["bone"],
                      FxHit0=fx["hit0"], FxHit1=fx["hit1"], FxHit2=fx["hit2"],
                      SndCast=fx["snd_cast"], SndHit=fx["snd_hit"],
                      Proj=1 if fx.get("proj") else 0,
                      ProjDelay=1 if (prog and prog.get("projdelay")) else 0,
                      TargetSize=target_size(c.get("condraw")), FxNote=fx["note"]))
    with open(OUT_CLIPS, "w", encoding="utf-8") as f:
        f.write("\t".join(COLS) + "\n")
        f.write("\n".join(lines) + "\n")
    with open(OUT_CSV, "w", encoding="utf-8-sig") as f:
        f.write("\n".join(csv) + "\n")
    print("записано %s (%d строк, %d уникальных веток клипа)" % (OUT_CLIPS, nrow, nclip))
    print("записано %s" % OUT_CSV)

    # --- 3) полный инвентарь ресурсов навыка: по ВСЕМ СЕМИ массивам колбэков
    #        (эффекты/звуки/кости живут не только в загрузчике, но и в тике и в остальных ветках)
    RE_BONE = re.compile(r'^(?:\d+_[a-z]\d*(?:_[A-Za-z]+)?|M|G|M_DUMMY|G_DUMMY)$')
    inv = []
    for sid in sorted(table):
        if sid == STUB_SKILL:
            continue                       # слот-заглушка: ресурсы случайно выживших функций — не его
        # ★ Для .inl-навыков ресурсы лежат НЕ в теле функции, а в записи `SkillInfo.inl`.
        #   Пишем их с тем же Kind, чтобы экспорт клипов (`export_char_skillclips.py`, Kind=="clip")
        #   и сборка BP эффектов (`asda_build_skill_fx.py`, Kind=="jeb") подхватили их без правок.
        rec = inl.get(sid) if (table[sid].get(0) and table[sid][0][0] == INL_LOADER) else None
        if rec:
            fname = table[sid][0][0]
            for v in _dedup([rec["male"], rec["female"]]):
                if v:
                    inv.append((sid, sifnames.get(sid, ""), 0, CB_ROLE[0], fname, "clip", v))
            for v in _dedup([b["file"] for b in rec["blocks"]]):
                inv.append((sid, sifnames.get(sid, ""), 0, CB_ROLE[0], fname, "jeb", v))
            for v in rec["wavs"]:
                inv.append((sid, sifnames.get(sid, ""), 0, CB_ROLE[0], fname, "wav", v))
            for v in _dedup([b["bone"] for b in rec["blocks"] if b["bone"]]):
                inv.append((sid, sifnames.get(sid, ""), 0, CB_ROLE[0], fname, "bone", v))
        for k in range(CB_ARRAYS):
            if k not in table[sid]:
                continue
            fname = table[sid][k][0]
            if rec and k == 0:
                continue                   # тело общего загрузчика имён не содержит — уже отдано выше
            seen_v = set()
            for line in bodies.get(fname, []):
                for v in RE_ANYSTR.findall(line):
                    low = v.lower()
                    if low.endswith(".wav"):
                        kind = "wav"
                    elif low.endswith(".jeb"):
                        kind = "jeb"
                    elif low.endswith(".jbb"):
                        kind = "clip"
                    elif low.endswith(".jem"):
                        kind = "jem"
                    elif RE_BONE.match(v):
                        kind = "bone"
                    else:
                        continue
                    if (k, kind, v) in seen_v:
                        continue
                    seen_v.add((k, kind, v))
                    inv.append((sid, sifnames.get(sid, ""), k, CB_ROLE[k], fname, kind, v))
    out_inv = os.path.join(REPORTS, "skill_effect_inventory.tsv")
    with open(out_inv, "w", encoding="utf-8") as f:
        f.write("SkillId\tSkillName\tCb\tRole\tFunction\tKind\tValue\n")
        for r in inv:
            f.write("\t".join(str(x) for x in r) + "\n")
    kinds = {}
    for r in inv:
        kinds[r[5]] = kinds.get(r[5], 0) + 1
    print("записано %s (%d строк: %s)" % (out_inv, len(inv), kinds))

    # --- 4) честный отчёт о пробелах
    print("--- регистраций в теле функции: %d; ЖИВЫХ слотов (массив,индекс): %d; мёртвых перезаписей: %d"
          % (len(raw), len(filled), len(raw) - len(filled) - len(stray)))
    if dead:
        top = sorted(dead.items(), key=lambda kv: -kv[1])[:5]
        # ⚠ БЕЗ «→»: консоль Windows отдаёт cp1251, и стрелка роняла скрипт `UnicodeEncodeError`
        #   в САМОЙ ПОСЛЕДНЕЙ строке — после того как все файлы уже записаны (2026-08-11).
        print("--- больше всего перезаписей: %s" % ", ".join("навык %d: %d" % (s, n) for s, n in top))
    print("--- навыков в таблице: %d; загрузчик есть у %d" % (len(table), sum(1 for s in table if 0 in table[s])))
    print("--- навыков с клипом из SkillInfo.inl: %d (загрузчик %s)" % (ninl, INL_LOADER))
    print("--- слот-заглушка %d («%s») исключена из таблиц клипов и инвентаря"
          % (STUB_SKILL, sifnames.get(STUB_SKILL, "")))
    print("--- загрузчиков БЕЗ клипа в теле: %d" % len(noclip))
    for sid, ld in noclip[:40]:
        print("      %4d  %s" % (sid, ld))
    if stray:
        print("--- записей вне 7 массивов колбэков: %d (перечислены полностью)" % len(stray))
        for order, disp, real, nm, k, i in stray:
            print("      #%d disp=0x%X k=%s i=%s %s" % (order, disp, k, i, nm))
    if combo:
        print("--- combo time.bin: version=%.2f count=%d" % (combo["__meta__"]["version"], combo["__meta__"]["count"]))
    return 0


if __name__ == "__main__":
    sys.exit(main())
