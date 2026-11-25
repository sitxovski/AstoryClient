# Copyright (c) 2026 @sitxovski (Discord)
# export_char_fable_clips.py — МИНИ-glb с клипами персонажа из донора AsdaFableClassic: набор «jump» (ПРЫЖОК и ПЛАВАНИЕ,
#   anim_235..238) и набор «crouch» (ХОДЬБА ВПРИСЯДКУ, anim_239).
#
# ИСТОЧНИК: @@ASTORY_DEV_ROOT@@\Donors\AsdaFableClassic\Data\Character\Motion\CharMotion.baz — клипы ch4_{m|f}_*, которых нет
#   в основной сборке (Reports/fable_mounts/FABLE_MOUNTS.md, раздел про клипы персонажа). Набор костей у них тот же,
#   что у основного набора ch4_{m|f} эталона (сверено: 50 костей у m, 54–55 у f, имена совпадают), поэтому скелет/бинд
#   строятся ТОЧНО как в `export_char_emotions.py` (ref-клип cre_cremmy-a_00_0 и части — из основной сборки).
#
# РАСКЛАДКА АССЕТОВ (порт). Занято: 00…28, 35, 40…97, 100…209, 215…219, 225…238. Наборы:
#   jump   → fable_clips/ : 235 idlejump01 (прыжок с места)  236 runjump01 (прыжок с бега)
#                           237 swim01                       238 swimloop01
#   crouch → fable_crouch/: 239 hidewalk_01 — ★ 24.10.2026 ходьба вприсядку. Файл донора лежит в наборе _05
#            (ch4_{m|f}_05_hidewalk_01.jbb, 45 кадров, 30 к/с, 1.467 с, цикл замкнут, корень на месте), но Asda2.exe грузит
#            его в слот 40 модели НЕЗАВИСИМО от оружия (`0x45ED29`: push 1 / +0xA00, сразу за swim01 в +0x9C0) и играет вместо
#            клипа хода (слот 5) при состояниях невидимости 112/186 (`sub_4C2440` → `0x482500`/`0x4A3AF0`/`0x4A8130`).
#            Поэтому здесь ОДИН клип на пол, взятый из _05 через подмену имени (SET_ALIAS).
# Вывод: RawAssets/Models/Skeletal/Characters/<папка набора>/ch1_{g}_cremmy_preview.glb (существующие glb других наборов
#   не трогаются); импорт — import_char_fable_clips.py (только анимации на существующий скелет; набор — ASDA_FABLE_CLIPSET).
# Запуск: python export_char_fable_clips.py [jump|crouch] [m|f]   (по умолчанию jump и оба пола — поведение до 24.10)
import os, sys, zipfile
import importlib.util

HERE = os.path.dirname(os.path.abspath(__file__))
spec = importlib.util.spec_from_file_location("ECE", os.path.join(HERE, "export_char_emotions.py"))
ECE = importlib.util.module_from_spec(spec); sys.modules["ECE"] = ECE; spec.loader.exec_module(ECE)
ECC = ECE.ECC
if HERE not in sys.path:
    sys.path.insert(0, HERE)
import asda_paths as _P   # noqa: E402

DONOR_MOTION = os.path.join(_P.DONORS, "AsdaFableClassic", "Data", "Character", "Motion", "CharMotion.baz")

# (имя клипа в наборе _00, индекс ассета anim_NN, состояние — для журнала: здесь «состояний эталона» нет, клипы донора)
PLAN_JUMP = [
    ("idlejump01", 235, 0),
    ("runjump01",  236, 0),
    ("swim01",     237, 0),
    ("swimloop01", 238, 0),
]
# ★ 24.10.2026: ходьба вприсядку (донорский hidewalk_01, слот 40 Asda2.exe).
PLAN_CROUCH = [
    ("hidewalk_01", 239, 0),
]
# Набор → (план, папка вывода в RawAssets/.../Characters).
CLIPSETS = {
    "jump":   (PLAN_JUMP,   "fable_clips"),
    "crouch": (PLAN_CROUCH, "fable_crouch"),
}
# Клип, лежащий у донора НЕ в наборе _00: имя клипа → номер набора в имени файла (export_gender всегда спрашивает _00).
SET_ALIAS = {
    "hidewalk_01": "05",
}


class _MergedZip:
    """Чтение клипа: сначала архив донора (новые клипы), иначе основной (ref-клип cre_cremmy-a_00_0).
    Имя «ch4_<g>_00_<клип>.jbb» для клипов из SET_ALIAS читается как «ch4_<g>_<набор>_<клип>.jbb»."""
    def __init__(self, donor, main):
        self.donor, self.main = donor, main

    @staticmethod
    def _alias(name):
        for clip, setno in SET_ALIAS.items():
            for g in ("m", "f"):
                if name == "ch4_%s_00_%s.jbb" % (g, clip):
                    return "ch4_%s_%s_%s.jbb" % (g, setno, clip)
        return name

    def read(self, name):
        name = self._alias(name)
        try:
            return self.donor.read(name)
        except KeyError:
            return self.main.read(name)


def main():
    args = [a.lower() for a in sys.argv[1:]]
    sets = [a for a in args if a in CLIPSETS] or ["jump"]
    genders = [g for g in args if g in ("m", "f")] or ["m", "f"]
    zmodel = zipfile.ZipFile(ECC.MODEL_ARC)
    zmot = _MergedZip(zipfile.ZipFile(DONOR_MOTION), zipfile.ZipFile(ECC.MOTION_ARC))
    allok = True
    for s in sets:
        plan, folder = CLIPSETS[s]
        ECE.PLAN = plan          # общая реализация export_gender — со своим списком клипов и папкой вывода
        ECE.OUT_DIR = os.path.join(ECC.OUT_DIR, folder)
        print("\n##### НАБОР %s -> %s" % (s, ECE.OUT_DIR))
        for g in genders:
            allok = ECE.export_gender(g, zmodel, zmot) and allok
    print("\n=== ИТОГ:", "ВСЕ OK" if allok else "ЕСТЬ ПРОБЛЕМЫ", "===")


# ★ ЗАКОН [[tools-no-import-side-effects]]: при импорте модуль НИЧЕГО не делает.
if __name__ == "__main__" or globals().get("__spec__") is None:
    main()
