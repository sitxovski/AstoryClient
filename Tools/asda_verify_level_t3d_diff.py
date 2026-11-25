# Copyright (c) 2026 @sitxovski (Discord)
# asda_verify_level_t3d_diff.py — сравнение T3D уровня до/после (выход asda_verify_level_t3d.py), вне UE.
#   Разбивает уровень на блоки акторов верхнего уровня (по имени объекта), выкидывает держатель AsdaMapObjects_*
#   и сравнивает остальные ТЕКСТОМ целиком (все не-дефолтные свойства акторов и их компонентов).
#   Код выхода 0 — вне держателя различий нет; 1 — есть (печатаются имена акторов и первые строки расхождений).
# Запуск: python asda_verify_level_t3d_diff.py <папка T3D> <карта> [<карта> ...]
import os, re, sys, collections

HOLDER = 'ActorLabel="AsdaMapObjects_'
STRICT = False   # см. _allowed
# Поля, которые движок/эффект ПЕРЕСОБИРАЕТ при каждой загрузке со случайным разбросом (частицы AsdaEffectActor в
# ProceduralMeshComponent): один и тот же .umap, загруженный дважды, даёт разные значения (проверено 28.10 на
# t_castle_of_chess: бэкап vs бэкап — 97 акторов, только эти два поля). Исключаются из сравнения.
GENERATED = ("ProcMeshSections(", "LocalBounds=")


def _generated(line):
    """Строка — генерируемое при загрузке поле (см. GENERATED)."""
    return line.strip().startswith(GENERATED)


def blocks(path, asset):
    raw = open(path, "rb").read()
    # UE пишет T3D в UTF-16 (с BOM), только если в тексте есть не-ASCII символы; иначе — однобайтово
    txt = raw.decode("utf-16") if raw[:2] in (bytes([0xFF, 0xFE]), bytes([0xFE, 0xFF])) else raw.decode("utf-8", "replace")
    txt = txt.replace("/Game/_VerifyTmp/%s.%s" % (asset, asset), "/Game/Map/%s/%s.%s" % (asset, asset, asset))
    txt = txt.replace("/Game/_VerifyTmp/%s" % asset, "/Game/Map/%s/%s" % (asset, asset))
    lines = txt.splitlines()
    out = {}; cur = None; buf = []; depth = 0
    for ln in lines:
        s = ln.strip()
        if cur is None:
            m = re.match(r'Begin Actor .*?Name=(\S+)', s)
            if m:
                cur = m.group(1); buf = [ln]; depth = 1
            continue
        buf.append(ln)
        if s.startswith("Begin "):
            depth += 1
        elif s.startswith("End "):
            depth -= 1
            if depth == 0:
                out[cur] = "\n".join(buf); cur = None
    return out


# Строгий режим (--strict): допустимо ТОЛЬКО то, что даёт любое пересохранение уровня редактором без правок
# (контроль 28.10: бэкап загружен и сохранён без изменений → ровно эти поля): GUID освещения компонентов,
# пересоздаваемые динамические материалы (MID — их объекты и ссылки на них, в т.ч. LightFunctionMaterial солнца на
# MID теней облаков и OverrideMaterials эффектов/воды), служебные поля пересборки воды и тела облаков.
def _allowed(line):
    s = line.strip()
    return (s.startswith("LightingGuid=") or "MaterialInstanceDynamic" in s or "MaterialInstanceEditorOnlyData" in s
            or s.startswith("LastAuthority=") or s.startswith("BodyInstance="))


def _key(actor, line):
    """Ключ «класс актора :: свойство» для классификации строки (номера в именах объектов отброшены)."""
    cls = re.sub(r"_\d+$", "", actor)
    prop = re.sub(r'Name="[^"]*"', 'Name=*', re.split(r"=\(|=", line.strip())[0])[:90]
    return cls + " :: " + prop


def _lines(v):
    return collections.Counter(l.strip() for l in v.splitlines() if not _generated(l))


def _diffs(a, b):
    """{актор: Counter различающихся строк} вне держателя."""
    out = {}
    for k in set(a) | set(b):
        if HOLDER in a.get(k, "") or HOLDER in b.get(k, ""):
            continue
        x = _lines(a.get(k, "")); y = _lines(b.get(k, ""))
        d = (x - y) + (y - x)
        if d:
            out[k] = d
    return out


def main():
    """Режим 1: до/после. Если рядом есть <карта>_before2.t3d (бэкап, загруженный второй раз) — контроль шума:
    ключ «класс::свойство», который расходится между двумя загрузками ОДНОГО файла хоть на одной карте, считается
    пересчитываемым при загрузке; остальные расхождения до/после — РЕАЛЬНЫЕ изменения (код выхода 1)."""
    global STRICT
    args = sys.argv[1:]
    STRICT = "--strict" in args
    args = [x for x in args if x != "--strict"]
    after_tag = "_after_late.t3d" if STRICT else "_after.t3d"   # строгий режим: текущий уровень выгружен ВТОРЫМ
    d = args[0]; maps = args[1:]
    noise = collections.Counter(); per = {}
    for asset in maps:
        a = blocks(os.path.join(d, asset + "_before.t3d"), asset)
        b = blocks(os.path.join(d, asset + after_tag), asset)
        p2 = os.path.join(d, asset + "_before2.t3d")
        if os.path.exists(p2):
            for k, dl in _diffs(a, blocks(p2, asset)).items():
                for l in dl:
                    noise[_key(k, l)] += 1
        ha = [k for k, v in a.items() if HOLDER in v]; hb = [k for k, v in b.items() if HOLDER in v]
        per[asset] = (len(a) - len(ha), len(b) - len(hb), ha, hb, _diffs(a, b))
    bad = 0
    for asset in maps:
        na, nb, ha, hb, dd = per[asset]
        real = {}
        for k, dl in dd.items():
            r = [l for l in dl.elements() if ((not _allowed(l)) if STRICT else (_key(k, l) not in noise))]
            if r:
                real[k] = r
        print("%-20s акторов вне держателя %d/%d, держатель %s/%s, расхождений %d (из них шум загрузки %d), РЕАЛЬНЫХ %d"
              % (asset, na, nb, ha, hb, len(dd), len(dd) - len(real), len(real)))
        if na != nb or len(ha) != 1 or len(hb) != 1:
            bad += 1; print("   ! число акторов/держателей не совпало")
        for k, r in list(real.items())[:10]:
            print("   ~", k)
            for l in r[:4]:
                print("      ", l[:170])
        bad += len(real) > 0
    print("ключи шума загрузки (бэкап vs тот же бэкап): " + "; ".join("%s ×%d" % kv for kv in noise.most_common()))
    sys.exit(1 if bad else 0)


if __name__ == "__main__":
    main()
