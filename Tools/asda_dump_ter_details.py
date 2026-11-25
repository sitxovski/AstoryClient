# Copyright (c) 2026 @sitxovski (Discord)
# asda_dump_ter_details.py — снять per-patch detail-тайлы из ПРАВИЛЬНОГО <map>/<map>.TER (системный Python).
#
# Зачем отдельно от экспортёра: в трёх папках карт лежит ЛИШНИЙ .TER от карты-донора
# (castle_of_chess/alpen.TER, nightvalley/conquester.TER, quizstage/OX_Quiz.TER), и прежний
# экспортёр брал «первый .ter по итератору каталога» → в .mapraw попадали ЧУЖИЕ имена тайлов.
# Оригинал строит путь `<cwd>\Map\<map>\<map>.TER` (CMap_LoadTER @0x1133580) — имя папки дважды.
#
# Структура .TER (magic 0x20050802), проверена сверкой: alpen.TER из папки castle_of_chess даёт
# ровно те TILE0/TILE3, что лежат в текущем .mapraw ⇒ разбор верен.
#   header 0x2f6c при file+12: +0x14c таблица имён 40 x 0x100 ASCII; +0x2da4 u32 count1;
#   +0x29f0 u32 patchCount. Записи патчей 0x198 при file+12+0x2f6c+count1*0x14:
#   +0x108 i32 индекс имени тайла A, +0x10c i32 индекс тайла B, +0x08 f32 UV-масштаб A,
#   +0x48 f32 UV-масштаб B.
#
# Запуск: python asda_dump_ter_details.py <clientMapRoot> <outJson>
import os, sys, json, struct, re


def read_ter(path):
    b = open(path, "rb").read()
    if len(b) < 0x3000 or struct.unpack_from("<I", b, 0)[0] != 0x20050802:
        return None
    H = 12
    names = []
    for i in range(40):
        o = H + 0x14c + i * 0x100
        names.append(b[o:o + 0x100].split(b"\0")[0].decode("cp1251", "replace"))
    count1 = struct.unpack_from("<I", b, H + 0x2da4)[0]
    pc = struct.unpack_from("<I", b, H + 0x29f0)[0]
    sec = H + 0x2f6c + count1 * 0x14
    out = []
    for P in range(pc):
        r = sec + P * 0x198
        if r + 0x198 > len(b):
            break
        ia = struct.unpack_from("<i", b, r + 0x108)[0]
        ib = struct.unpack_from("<i", b, r + 0x10c)[0]
        out.append({
            "detail0": _leaf(names[ia] if 0 <= ia < 40 else ""),
            "scale0": struct.unpack_from("<f", b, r + 0x08)[0],
            "detail1": _leaf(names[ib] if 0 <= ib < 40 else ""),
            "scale1": struct.unpack_from("<f", b, r + 0x48)[0],
        })
    return out


def _leaf(p):
    r"""`\MAP\ALPEN\TILE0.TGA` → `TILE0` (как UE санитайзит имя ассета при импорте)."""
    if not p:
        return ""
    base = os.path.splitext(p.replace("\\", "/").rstrip("/").split("/")[-1])[0]
    return re.sub(r'[^A-Za-z0-9_-]', '_', base)


def main():
    if len(sys.argv) < 3:
        print("usage: asda_dump_ter_details.py <clientMapRoot> <outJson>")
        return
    root, out_path = sys.argv[1], sys.argv[2]
    res = {}
    for name in sorted(os.listdir(root)):
        d = os.path.join(root, name)
        if not os.path.isdir(d):
            continue
        # ★ строго <map>.TER; регистр в части карт отличается (Castle_of_Chaos.TER, DragonLaiR.TER)
        want = (name + ".ter").lower()
        hit = next((f for f in os.listdir(d) if f.lower() == want), None)
        if not hit:
            continue
        patches = read_ter(os.path.join(d, hit))
        if patches:
            res[name] = {"terFile": hit, "patches": patches}
            print("%-22s %s: патчей %d" % (name, hit, len(patches)))
    json.dump(res, open(out_path, "w", encoding="utf-8"), ensure_ascii=False, indent=1)
    print("→ %s (карт %d)" % (out_path, len(res)))


if __name__ == "__main__":
    main()
