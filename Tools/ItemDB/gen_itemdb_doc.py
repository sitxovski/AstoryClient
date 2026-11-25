# -*- coding: utf-8 -*-
# Copyright (c) 2026 @sitxovski (Discord)
"""
gen_itemdb_doc.py — генерирует мастер-справочник item-БД (методика + описание каждой
таблицы с полями) ИЗ реестра decode_item_db.REGISTRY, чтобы доки были 1:1 с кодом.
Вывод: @@ASTORY_DEV_ROOT_ESC@@\\Docs\\Port\\systems\\ITEM_DB_REFERENCE.md
"""
import io, sys, importlib.util, os

SPEC = importlib.util.spec_from_file_location("decode_item_db", r"@@ASTORY_DEV_ROOT@@\AstoryClient\Tools\ItemDB\decode_item_db.py")
M = importlib.util.module_from_spec(SPEC); SPEC.loader.exec_module(M)
REG = M.REGISTRY

OUT = r"@@ASTORY_DEV_ROOT@@\Docs\Port\systems\ITEM_DB_REFERENCE.md"

# 1-строчное назначение каждой таблицы (что это в игре)
DESC = {
 'item_list.bin':'ГЛАВНАЯ БД предметов (58878): имя/тип/подтип/уровень-требование/класс/цена/грейд/imgIndex.',
 'item_img_table.bin':'Мост imgIndex→визуал: имена мешей и текстур (мужской/женский вариант).',
 'item_tooltip.bin':'Описания-тултипы предметов (по коду предмета).',
 'item_upgrade_cost.bin':'Материал апгрейда: имя(кор.) + itemCode + значение.',
 'item_upgrade_price.bin':'Цена апгрейда по индексу (index→value).',
 'itemoptiontable.bin':'Опции предметов: OptionId + printf-шаблон тултипа (RU, напр. «Максимальная атака +%d»).',
 'itemoptionvalue.bin':'Значения опций по ступеням (id + 20 пар Level/Value).',
 'itemchangegrade.bin':'Стоимость смены грейда (grade→cost).',
 'itemopgraft.bin':'Опция-графт по диапазону уровней (LevelLow..LevelHigh→Value).',
 'special_itemlist.bin':'Спец-предметы (свитки/телепорт/расходники) — код+имя+строки.',
 'specialitem_tooltiplist.bin':'Тултипы спец-предметов.',
 'specialitem_buftooltiplist.bin':'Баф-тултипы спец-предметов.',
 'event_item_list.bin':'Ивент-обмен (клиент грузит, но поля не читает — смысл на СЕРВЕРЕ: расход→награда).',
 'booster_list.bin':'Бустеры крафта: код + 35 слотов (itemCode+param) пула бустинга.',
 'premium_gatya.bin':'Премиум-гатя (гача): индекс→код предмета награды.',
 'packagelist.bin':'Список пакетов (код→имя ассета).',
 'packagetable.bin':'Состав пакетов: до 6 пар (предмет, количество).',
 'pricetable.bin':'Цены предметов в NPC-магазине (itemCode→price).',
 'decompositiontable.bin':'Разбор предмета: itemCode → 6 групп (Code,Weight,MinQty,MaxQty), взвеш.рандом-дроп (Σвесов=100000).',
 'graderate.bin':'Шансы грейда (gradeIndex→процент).',
 'upgradetable.bin':'Таблица апгрейда (id/диапазон уровней/стоимость/тир).',
 'refinehigh.bin':'Заточка высокого уровня: материалы+золото+шанс по грейду (loose Data/NPC).',
 'refineupmoney.bin':'Стоимость заточки по уровню (фикс.блок, строка=уровень).',
 'advalue.bin':'Значения IUV (Index + 2 константы; консьюмер не локализован).',
 'combo time.bin':'Комбо-скиллы: имя аним-клипа + число шагов + 3 трека таймингов кадров (×33.333 мс).',
 'charequiptable.bin':'Стартовая экипировка: код/пол(1=M,2=F)/слот/пред.код.',
 'charexp.bin':'Таблица опыта персонажа (до уровня + кумулятив).',
 'smcharexp.bin':'Таблица опыта (вариант, 30 уровней).',
 'hairtable.bin':'Причёски: id/имя/пол(1М,2Ж)/HairStyle/цена/меш/иконка/цвет/описание. Уникум=(Gender×HairStyle×Color).',
 'jobtype.bin':'Классы/профессии (имя + родительский класс).',
 'sowell_list.bin':'Камни-совелы (сокеты): тип-цель/цены/треб.уровень/маска бонус-опций/меш.',
 'sowell_tooltip.bin':'Тексты-подсказки совелов.',
 'sowell_calc_table.bin':'Коэффициенты формулы силы совела (роль каждого не подтв.).',
 'title_list.bin':'Титулы: id/имя/цвет/OptionTypeId[6]→title_option/IsPercent[6]/описание. Значение стата даёт сервер.',
 'title_list_old.bin':'ЛЕГАСИ титулы — загрузчика в клиенте НЕТ, сырой (raw).',
 'title_effect.bin':'Эффекты титулов (id + 8 имён эффектов/условий).',
 'title_option.bin':'Опции титулов (id→имя).',
 'title_rank.bin':'Атлас значков рейтинга/грейда: текстура + UV-прямоугольник значка по категории/порогу.',
 'soulmateskilltable.bin':'Соулмейт-скиллы: id/отобр.имя/внутр.имя скила/описание.',
 'soulsoulmatetext.bin':'Тексты соулмейта (type→строка).',
 'soulmateskill.ssi':'Соулмейт-скилл бинарь (без header/шифра, raw).',
 'petexptable.bin':'Опыт питомца (уровень→опыт).',
 'petoptiontable.bin':'Опции питомца (id + формат-строка + хвост).',
 'pettable.bin':'Питомцы: id/имя/грейд/факторы наград/опции/модель+части/иконка. Боевые статы серверные.',
 'skilltooltip.bin':'Тултипы скиллов (id/имя/текст).',
 'emotiontip.bin':'Эмоции (имя/тег анимации/тип).',
 'loginhelpertext.bin':'Тексты-подсказки экрана логина (RU, с тегами).',
 'stateinfotip.bin':'Тултипы состояний/баффов (id/имя/иконка/текст).',
 'soulstateinfotip.bin':'Тултипы состояний соулмейта.',
 'skill_info.sif':'Скилл-данные (raw 273-байт запись, не энум.).',
 'itemoptionrange.bin':'Диапазоны значений опций (loose ItemOptionValue.BIN, 38917): GroupId(набор)→OptionType(=itemoptiontable.OptionId)+Min/MaxValue; сервер роллит значение в диапазоне.',
}

def tname(t):
    if t.startswith('s'): return f'FString (char[{t[1:]}])'
    return {'i32':'int32','u32':'int64(u32)','i16':'int32(i16)','u16':'int32(u16)',
            'u8':'int32(u8)','f32':'float'}.get(t, t)

lines=[]
w=lines.append


def main():
    """Работа скрипта. Вынесена в функцию, чтобы импорт модуля НИЧЕГО не делал
    (закон [[tools-no-import-side-effects]])."""
    w('# Item-БД AsdaStory → UE-порт: методика и справочник таблиц\n')
    w('_Автогенерация из `Tools/decode_item_db.py` (REGISTRY) — 1:1 с рабочим кодом. '
      'Не редактировать вручную: правь реестр и перегенери `Tools/gen_itemdb_doc.py`._\n')
    w('## 1. Методика (как получено)\n')
    w('**Источник:** `Data/Character/Bin.bbz` (стандартный ZIP/deflate) — 50 таблиц + мусор.\n')
    w('**Единый формат:** header 20б (`float ver@0 / int count@4 / int extra@8 / char[4] tag@12 / pad@16`) '
      '+ `count × stride` записей. Проверка целостности: `count == (usize−headerSize)/stride` побайтово.\n')
    w('**Два семейства:** (A) 16-байт MD5-префикс на запись + шифр полей (DWORD/WORD −const, строки ±1 CP1251) — '
      'item_list/tooltip/price/decomp/grade/upgrade/sowell/title; (B) raw-memcpy без шифра — img/pet/soulmate/booster/tip. '
      'Нестандартные `.ssi/.sif` — без header (raw stride). `title_list_old` — без загрузчика (легаси, raw).\n')
    w('**Реверс форматов:** для каждой таблицы найден клиентский загрузчик `CXxx_LoadYyy` в эталоне '
      '`RE/ida/export/AsdaStory_game.c` → stride + раскладка полей + шифр. Отчёты: '
      '`Reports/item_db_formats_{A,B,C}.md`, `item_table_format.md`.\n')
    w('**Осмысленный нейминг полей:** смысл каждого поля определён по тому, как оно ИСПОЛЬЗУЕТСЯ после загрузки '
      '(`record->field` в даунстриме) + перекрёстно с GameServer (те же таблицы, лог-строки с именами полей). '
      'Карты: `Reports/column_naming_{A,B}.md`. Уверенность: HIGH=чтение поля в коде; MED=структура ясна из данных; '
      'LOW=не подтверждено → нейтральное имя (`Unk<off>`/`Reserved`/`Field<off>`) или сырой блок. **Домыслов нет.**\n')
    w('**Пайплайн переноса в UE:** `decode_item_db.py` (декод→CSV+JSON, колонка `DataHex`=сырая запись для '
      'неполных раскладок) → `gen_itemdb_structs.py` (USTRUCT(BlueprintType) на таблицу + манифест) → '
      '`gen_itemdb_datatables.py` (headless CSVImportFactory → `/Game/AsdaAssets/Data/ItemDB/DT_<table>`). '
      'Подсистема `UAsdaItemTableSubsystem`: типизир. BP-геттеры + мост itemId→меш + RU-подмена имён.\n')
    w('**Грабли реимпорта (закреплены):** (1) колонка CSV должна совпадать с UPROPERTY — hex-колонка = `DataHex` '
      '(не `_data_hex`), иначе редактор роняет строки; (2) `ImportKeyField="_row"` зашит во все DataTable, иначе '
      'редакторный «Реимпорт» требует ключ вручную и опустошает таблицу.\n')
    w('## 2. Справочник таблиц и полей\n')
    w(f'Всего таблиц: **{len(REG)}**. Тип `FString (char[N])` = строка фикс. длины; `DataHex` = сырой остаток '
      'записи (полные данные 1:1 для полей с неполной раскладкой).\n')
    for name in REG:
        spec = REG[name]
        cols = spec['cols']; note = spec.get('note',''); md5 = spec.get('md5',0)
        w(f'### `{name}`  (stride={spec["stride"]}{", MD5-префикс 16б" if md5 else ""})')
        w(DESC.get(name, '_(назначение уточняется)_'))
        if note: w(f'> Заметка: {note}')
        w('')
        w('| offset | тип | поле | ')
        w('|---:|---|---|')
        for off, typ, nm in cols:
            w(f'| {off} | {tname(typ)} | `{nm}` |')
        if spec.get('dump_hex'):
            w('| — | FString | `DataHex` (сырой остаток записи, 1:1) |')
        w('')
    os.makedirs(os.path.dirname(OUT), exist_ok=True)
    with io.open(OUT, 'w', encoding='utf-8') as f:
        f.write('\n'.join(lines))
    print('OK ->', OUT, '| таблиц:', len(REG), '| строк:', len(lines))


# ★ ЗАКОН [[tools-no-import-side-effects]]: при импорте модуль НИЧЕГО не делает.
#   `__spec__` выставляет ЛЮБАЯ машинерия импорта (import / importlib / runpy) и НЕ
#   выставляет прямое исполнение файла (python file.py, UE -ExecutePythonScript).
#   `python -m` даёт `__name__ == "__main__"` и покрыт первым условием.
if __name__ == "__main__" or globals().get("__spec__") is None:
    main()
