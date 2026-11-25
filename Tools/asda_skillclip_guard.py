# Copyright (c) 2026 @sitxovski (Discord)
# ★ Решение владельца 09.10.2026 (п. 6): ЗАЩИТА чужих и правленных вручную клипов персонажа от конвейера клипов навыков.
#
# Скелет ch1_{m|f}_cremmy_preview делят несколько конвейеров, и номера anim_NN у них общие:
#   00..28, 35        — char-select/создание/мир/атаки (export_char_cremmy*.py);
#   40..85            — наборы оружия (import_char_weaponsets.py);
#   86..97            — эмоции (export_char_emotions.py);
#   100..             — клипы навыков (export/import_char_skillclips.py, карта skillclips_map.json);
#   205..209, 215..219 (м) / 210..214, 220..224 (ж) — «одержимость»: парные клипы (export_char_possess.py),
#                       ★ anim_205 (м) — клип одержимости, его перезапись и стала поводом этой защиты;
#   225..232          — рыбалка (fishing_export_assets.py, CHAR_IDX_BASE = 225);
#   233..234          — копание: 233 = лопата (состояние 40, ch4_98_Shoveling_{g}.jbb), 234 = авто-лопата (43, …_01.jbb)
#                       (digging_export_clips.py, 10.10.2026).
# Прежний распределитель export_char_skillclips.py выдавал новому клипу «первый свободный от 100» ТОЛЬКО по своей карте:
#   у мужского пола карта кончается на 204, и следующий новый клип навыка получил бы anim_205 — импорт удалил бы и
#   переписал клип одержимости (находка fishing_impl_W5a.md п. 3).
#
# Правила (общие для экспорта и импорта):
#   1) номер ЧУЖОГО конвейера (таблица ниже) клипам навыков не выдаётся и импортом не пишется НИКОГДА;
#   2) номер, под которым в Content уже лежит ассет, а карта навыков его своим не числит, — тоже чужой;
#   3) СВОЙ уже существующий ассет импорт по умолчанию НЕ перезаписывает (его могли править в редакторе) — только
#      явным списком ASDA_SKILLCLIPS_REIMPORT="m:102,105;f:110" и с бэкапом .uasset до записи.
# Модуль без побочных эффектов при импорте (закон tools-no-import-side-effects).
import os
import re

HERE = os.path.dirname(os.path.abspath(__file__))
# Папка ассетов скелета (тот же путь, что DEST_BASE импорта: G_MODELS_CHARACTERS + /ch1_{g}_cremmy_preview/SkeletalMeshes).
CONTENT_DIR = os.path.abspath(os.path.join(HERE, "..", "Content", "AsdaAssets", "Models", "Skeletal", "Characters"))


def _rng(a, b):
    """Множество номеров a..b включительно."""
    return set(range(a, b + 1))


# Номера чужих конвейеров (правило 1). Всё ниже 100 — чужое. Блок 205..224 «одержимости» резервируется ЦЕЛИКОМ у обоих полов
#   (у пола заняты только его 10 номеров, но раскладка общая — смешивать в ней клипы навыков нельзя), 225..232 — рыбалка, 233..234 — копание.
FOREIGN = {
    "m": _rng(0, 99) | _rng(205, 224) | _rng(225, 232) | _rng(233, 234),
    "f": _rng(0, 99) | _rng(205, 224) | _rng(225, 232) | _rng(233, 234),
}
# Явно названный владельцем клип (п. 6): мужской anim_205 — одержимость.
NAMED_PROTECTED = {("m", 205): "клип одержимости (export_char_possess.py, 02.09.2026)"}


def asset_dir(gender):
    """Папка .uasset клипов скелета пола gender ('m'/'f')."""
    return os.path.join(CONTENT_DIR, "ch1_%s_cremmy_preview" % gender, "SkeletalMeshes")


def asset_file(gender, idx):
    """Путь .uasset клипа anim_idx пола gender."""
    return os.path.join(asset_dir(gender), "ch1_%s_cremmy_previewanim_%d.uasset" % (gender, idx))


def on_disk(gender):
    """Номера anim_NN, ассеты которых уже лежат в Content для пола gender."""
    out = set()
    d = asset_dir(gender)
    if not os.path.isdir(d):
        return out
    pat = re.compile(r"^ch1_%s_cremmy_previewanim_(\d+)\.uasset$" % gender)
    for n in os.listdir(d):
        m = pat.match(n)
        if m:
            out.add(int(m.group(1)))
    return out


def forbidden(gender, own):
    """Номера, которые конвейер навыков НЕ может ни выдавать, ни писать: чужие (правило 1) + лежащие в Content
    и не числящиеся в его карте own (правило 2)."""
    return FOREIGN.get(gender, set()) | (on_disk(gender) - set(own))


def why(gender, idx, own):
    """Человеческая причина запрета номера (для журнала); '' — номер свободен."""
    if (gender, idx) in NAMED_PROTECTED:
        return NAMED_PROTECTED[(gender, idx)]
    if idx in FOREIGN.get(gender, set()):
        return "номер чужого конвейера (таблица FOREIGN)"
    if idx in on_disk(gender) and idx not in set(own):
        return "ассет уже лежит в Content, карта навыков его своим не числит"
    return ""


def parse_reimport(env_value):
    """'m:102,105;f:110' → {'m': {102, 105}, 'f': {110}}. Пусто — ничего не переимпортируется."""
    out = {"m": set(), "f": set()}
    for part in (env_value or "").split(";"):
        part = part.strip()
        if not part or ":" not in part:
            continue
        g, nums = part.split(":", 1)
        g = g.strip().lower()
        if g not in out:
            continue
        for x in nums.split(","):
            x = x.strip()
            if x.isdigit():
                out[g].add(int(x))
    return out
