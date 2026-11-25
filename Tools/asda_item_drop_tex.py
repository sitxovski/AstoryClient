# -*- coding: utf-8 -*-
# Copyright (c) 2026 @sitxovski (Discord)
r"""
asda_item_drop_tex.py — ОБЩАЯ ПОЛИТИКА «какая текстура у 3D-модели предмета».
Чистый Python (без unreal) — модуль импортируется и из headless-скриптов, и офлайн-аудитом.
Побочных действий при импорте НЕТ.

ЭТАЛОН (client, IDA 9.3):
  * DropItemMgr_SpawnGroundItem@0xF443D0 (item_inventory.c:67102) — выбор МОДЕЛИ по itemType:
      0 hcap / 1 chst / 2 pant / 3 shoe / 4 grov — вариант v = floor(usageCategory/10) (0..2);
      8 shld; 9 оружие по itemSubType (1 swrd, 2 spir, 3 thnd, 5 stap, 9 cbow, A lbow, B bbow);
      всё остальное (default, вкл. золото 20551) — модель из img-записи: +4 (муж) / +68 (жен).
      Первым шести случаям передаётся useCustomTexture=1, default-ветке — 0.
  * CEffectMgr_SpawnModelWithItemTexture@0xF2E040 (effects_particles.c:57452-57780) — ТЕКСТУРА:
      useCustomTexture!=0 И textureMode(=itemRecord.tooltipFlag200)<=2
          → Data\item\texture\<ИМЯ МОДЕЛИ>.TEX
      useCustomTexture==0
          → предмет: imgRecord+36 (TexMale, при g_ItemImgOptionVariant==1) / +100 (TexFemale)
          → совел  : sowelRecord+0x89
      Полученная текстура кладётся во ВСЕ сабмеши (цикл по pModelNode[1]).
  * CModel_LoadGeometry@0x119CA00 (model_anim.c:27963) — загрузчик геометрии дропа
      (resPackLoadMeshFromPath → modelMgrAddGeometryFile → сюда) текстуры НЕ читает вообще:
      слот +496 каждого сабмеша принудительно обнуляется. ⇒ СТРОКА МАТЕРИАЛА ВНУТРИ .jbm
      ОРИГИНАЛОМ НЕ ИСПОЛЬЗУЕТСЯ НИКОГДА (у 49 из 243 item-моделей она указывает на чужой
      файл или содержит корейский мусор). Импорт glTF назвал MIC по этой строке, и прежний
      фиксер подбирал текстуру ПО ИМЕНИ MIC — отсюда «красный Знак волка» и ещё 1400 предметов.

Из этого следуют две вещи:
  1) РАНТАЙМ обязан красить модель текстурой из img-записи КОНКРЕТНОГО предмета: одна и та же
     модель обслуживает разные предметы с РАЗНЫМИ текстурами (itm1_n_100_spc01_00 → spc01_02
     у 851 предмета, booster001_00 → booster001_02 у 112 и т.д.).
  2) MIC у меша — только редакторский ДЕФОЛТ. Правильный дефолт = САМАЯ ЧАСТАЯ текстура,
     которую img-таблица назначает этой модели (а не имя MIC и не имя меша).
"""
import csv, os, collections

PROJECT  = r"@@ASTORY_DEV_ROOT@@\AstoryClient"
ITEMDB   = os.path.join(PROJECT, "RawAssets", "Data", "ItemDB")

# itemSubType → префикс модели оружия (эталон §3)
WEAPON_SUBTYPE = {1: "swrd", 2: "spir", 3: "thnd", 5: "stap", 9: "cbow", 0xA: "lbow", 0xB: "bbow"}
# itemType → префикс модели экип-дропа
EQUIP_TYPE = {0: "hcap", 1: "chst", 2: "pant", 3: "shoe", 4: "grov"}


def _read_csv(name):
    path = os.path.join(ITEMDB, name)
    csv.field_size_limit(1 << 30)
    with open(path, "r", encoding="utf-8", newline="") as f:
        return list(csv.DictReader(f))


def load_tables():
    """→ (items:list[dict], img:dict[str,dict]) — item_list.csv и item_img_table.csv по _row."""
    items = _read_csv("item_list.csv")
    img = {r["_row"]: r for r in _read_csv("item_img_table.csv")}
    return items, img


def resolve_drop_visual(rec, img):
    """Эталонный разбор ОДНОГО предмета → (modelName, texName, bCustomTexture).

    modelName/texName могут быть пустыми, если у предмета нет модели (img-запись пустая).
    """
    it  = int(rec["ItemType"])
    sub = int(rec["ItemSubType"])
    uc  = int(rec["UsageCategory"])
    tf  = int(rec["TooltipFlag200"])
    v   = max(0, min(2, uc // 10))

    model, custom = "", False
    if it in EQUIP_TYPE:
        model, custom = "Itm1_n_00_%s_%02d_00" % (EQUIP_TYPE[it], v), True
    elif it == 8:
        model, custom = "Itm1_n_00_shld_00_00", True
    elif it == 9 and sub in WEAPON_SUBTYPE:
        model, custom = "Itm1_n_00_%s_00_00" % WEAPON_SUBTYPE[sub], True

    ir = img.get(rec["ImgIndex"])
    if not custom:
        model = (ir["MeshMale"].strip() if ir else "")

    if custom:
        tex = model if tf <= 2 else ""          # гейт textureMode эталона
    else:
        tex = (ir["TexMale"].strip() if ir else "")
    return model, tex, custom


def build_mesh_default_tex(items=None, img=None):
    """→ dict[mesh_lower] = имя текстуры-дефолта (самая частая по img-таблице для этой модели).

    Только default-ветка (img-модели): у экип/оружейных дроп-моделей текстура задаётся предметом
    и одноимённая модели, дефолт для них тривиален и добавляется отдельно.
    """
    if items is None or img is None:
        items, img = load_tables()
    votes = collections.defaultdict(collections.Counter)
    for rec in items:
        model, tex, custom = resolve_drop_visual(rec, img)
        if custom or not model or not tex:
            continue
        votes[model.lower()][tex] += 1
    return {m: c.most_common(1)[0][0] for m, c in votes.items()}


# ─────────────────────────────────────────────────────────────────────────────
# ДОБАВЛЕНО 2026-08-01 (круг «Сияющая руда»): ветки, которые первый аудит НЕ проверял.
# Он проверял только «есть ли ассеты», но не «ту ли запись мы взяли» и не «привязан ли материал».
# ─────────────────────────────────────────────────────────────────────────────

def check_img_binding(items=None, img=None):
    """Самопроверка связки item → img-запись ПО ДАННЫМ САМОГО КЛИЕНТА.

    В каждой записи item_img_table первые 4 байта DataHex — itemCode владельца (эталон: массив
    g_ItemImgTable индексируется itemRecord.imgIndex, а запись хранит свой itemCode). Значит
    правильность отображения ImgIndex → строка можно доказать, не веря экспортёру.

    → (ok, mismatched:list[(itemCode, name, imgIndex, кодВЗаписи)], no_record:list[itemCode])
    """
    if items is None or img is None:
        items, img = load_tables()
    ok, mismatched, no_record = 0, [], []
    for rec in items:
        ir = img.get(rec["ImgIndex"])
        if not ir:
            no_record.append(rec["ItemCode"])
            continue
        owner = int.from_bytes(bytes.fromhex(ir["DataHex"])[:4], "little")
        if owner == int(rec["ItemCode"]):
            ok += 1
        else:
            mismatched.append((rec["ItemCode"], rec["ItemName"], rec["ImgIndex"], owner))
    return ok, mismatched, no_record


def resolve_icon(rec, img):
    """Иконка предмета (эталон: imgRecord+132 атлас муж / +172 жен, индекс +164 / +204).

    → (atlasName, iconIndex) либо ("", -1), если у предмета нет img-записи.
    """
    ir = img.get(rec["ImgIndex"])
    if not ir:
        return "", -1
    b = bytes.fromhex(ir["DataHex"])
    atlas = b[132:132 + 32].split(b"\0")[0].decode("latin1", "ignore").strip()
    idx = int.from_bytes(b[164:168], "little", signed=True) if len(b) >= 168 else -1
    return atlas, idx


def asset_name(name):
    """Имя ассета UE для файла-исходника: UE санитизирует недопустимые символы (пробел/точку → '_').

    Нужна, чтобы аудит не считал «текстуры нет», когда файл клиента назван с хвостовым пробелом
    (реальный случай: 'itm1_n_00_booster001_02 .TEX' → ассет 'itm1_n_00_booster001_02_').
    """
    out = []
    for ch in name:
        out.append(ch if (ch.isalnum() or ch in "_-") else "_")
    return "".join(out)
