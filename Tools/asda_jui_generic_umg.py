# Copyright (c) 2026 @sitxovski (Discord)
# Генерик JUI-дерево → UMG (v2): по WBP на топ-окно, дерево UI/CLASS/NAME.
# v2 фиксы: (1) МНОГОПЛАНОВЫЕ скины (9-slice окна/ResizeWindow) — раньше брался ТОЛЬКО план 0 → окно
#   рисовалось одной растянутой картинкой. Теперь парсим ВСЕ планы и добавляем каждый как image по
#   9-slice grid-растяжке (углы фикс, края/центр тянутся к размеру контрола). (2) ТЕКСТ: captions —
#   корейский cp949 (парсер читал cp1251 → мусор); перекодируем cp1251-байты→cp949 (читаемо). RU/EN
#   перевод статических captions — отдельно (они дизайн-плейсхолдеры; рантайм-текст из .BIN-таблиц).
# ★★★ ШРИФТ ПОДПИСЕЙ (24.09.2026). Генератор гарнитуру НЕ задаёт — её ставит
#   `AsdaUmgBuilder::AddText`, и по умолчанию она теперь `Regular` (вес 400 — ровно тот, которым
#   оригинал печёт свой растровый атлас `JUIData/font/FONT0.BIN`; до 24.09 здесь молча получался
#   `Bold`, шире эталона на 7 %). Разбор — у объявления `AddText` в `AsdaUmgBuilder.h`.
#
# ★★★ ПРАВИЛО НА БУДУЩЕЕ: ГАРНИТУРУ ЗАДАВАТЬ ЯВНО ВСЕГДА, ИНАЧЕ ДЕФЕКТ ВЕРНЁТСЯ САМ.
#   ОТКУДА БРАЛСЯ `Bold`. В ассетах гарнитуры НЕ БЫЛО ВООБЩЕ — все 6648 переходов волны 24.09
#   это «свойства нет» → `Regular`. `Bold` приходил из CDO движка:
#   `Engine/Source/Runtime/UMG/Private/Components/TextBlock.cpp:36`
#   `Font = FSlateFontInfo(RobotoFontObj.Object, 24, FName("Bold"))`. Тегированная сериализация
#   пишет только ОТЛИЧИЯ от архетипа, поэтому прежнее явное `"Bold"` СОВПАДАЛО с дефолтом CDO и
#   в пакет не попадало — при загрузке бралось из CDO.
#   ⇒ ЛЮБОЙ НОВЫЙ `TextBlock` БЕЗ ЯВНО ЗАДАННОЙ ГАРНИТУРЫ СНОВА БУДЕТ `Bold`: любой будущий
#   импорт, пост-шаг или ручное добавление подписи в редакторе вернёт дефект МОЛЧА. Теперь
#   `Regular` отличается от дефолта CDO и пишется в пакет — но только там, где мы его поставили.
#   Этим же объясняются 26 окон сцены логина (сторож их не пускает): у них 299 узлов без гарнитуры.
import unreal, importlib.util, os, re, sys, json, copy
_DBG = os.environ.get("JUI_DUMP_POS")   # если задан — путь к файлу для дампа позиций (проверка раскладки)

HERE = os.path.dirname(os.path.abspath(__file__))
TRANS_JSON = os.path.join(HERE, "kor_ru_ui.json")   # словарь корейский→русский (переводим captions статически)
INI_DIR = r"@@ASTORY_DEV_ROOT@@\Data\Decoded\jui"
SKIN_INI = r"@@ASTORY_DEV_ROOT@@\RE\docs\JuiSkin.ini"
TEX_SRC = r"@@ASTORY_DEV_ROOT@@\AstoryClient\RawAssets\Textures\JUI"
TEX_DEST = "/Game/AsdaAssets/Textures/JUI"
CLASSES = {"gamemain": "GameMain", "commonwin": "CommonWin", "temp": "Temp", "login": "Login"}
sel = ([a.lower() for a in os.environ.get("ASDA_JUI_CLASS", "").split() if a.lower() in CLASSES]
       or [a.lower() for a in sys.argv[1:] if a.lower() in CLASSES]
       or ["gamemain", "commonwin", "temp"])

# ═══════ ★★★ ПОДМЕНА СКИНА ОКНА — ОТСТУПЛЕНИЕ ПО ТРЕБОВАНИЮ ВЛАДЕЛЬЦА 25.09.2026 ═══════════════
#
# ТРЕБОВАНИЕ. Окно «О питомце» (43300 `Pet_Tooltips_Win`) владелец просил сделать ПОЛНОЦЕННЫМ
#   окном и, отдельной репликой, НЕПРОЗРАЧНЫМ.
#
# ПОЧЕМУ ОНО ПРОЗРАЧНОЕ. Не из-за тинта и не из-за кода: у всех девяти планов рамки тинт
#   (1,1,1,1). Прозрачность лежит В ПИКСЕЛЯХ АТЛАСА. Замер `RawAssets/Textures/JUI/n_01.png`
#   (512×512 RGBA): у скина `Tool_Tip` середина — пиксели (94,423)-(97,425), альфа 153 из 255
#   (60 %); у скина настоящего окна — (480,439)-(483,442), альфа 255. Плюс у `Tool_Tip` в
#   `JuiSkin.ini` стоит `PlaneAlphaBlending = "1"`, у оконного скина — "0".
#
# ПОЧЕМУ НЕ ПРАВИМ АТЛАС. Скин `Tool_Tip` носят ОДИННАДЦАТЬ окон (`gamemain_jui.ini`, 11 вхождений
#   `Name = "Tool_Tip"`): 5000 `ITEM_TIPWIN` (подсказка предмета — самая частая в игре), 43300,
#   43450, 2580, 56400, 56420, 13700, 71150, 77100, 77200, 81150. Правка пикселей атласа сделала
#   бы непрозрачными ВСЕ ОДИННАДЦАТЬ ради одного нашего — недопустимо.
#
# ЧТО ДЕЛАЕМ. Окну 43300 подставляем скин, которым нарисованы ВСЕ настоящие окна этого набора:
#   43000 `Pet_Cage_Win`, 43500 `Pet_Evolution_Win`, 44200 `Pet_LimitBreak_Win`, 42000
#   `Pet_Hatchery_Win` — все четыре несут `Black_BottomRound_White2Border`. То есть окно «О
#   питомце» становится похоже на своих соседей, а не на подсказку. Чужие десять окон со скином
#   `Tool_Tip` НЕ ЗАТРОНУТЫ ВООБЩЕ.
#   ★ ЧИСЛО ПЛАНОВ НЕ МЕНЯЕТСЯ: у `Tool_Tip` их 9, и оконный скин в собранном ассете даёт РОВНО
#     9 (проверено по дампам `wbp_raw/WBP_Pet_Cage_Win.txt`, `WBP_Pet_Evolution_Win.txt`,
#     `WBP_Pet_LimitBreak_Win.txt` — у каждого `*_0_0 … *_0_8`). Значит имена планов и
#     регистрация зоны окна в коде (`RegisterFrameZone(..., "Pet_Tooltips_Win_0_%d", 9)`)
#     остаются верными.
#
# ★★★ ПОЧЕМУ ЭТО ЗДЕСЬ, А НЕ ТОЛЬКО В АССЕТЕ. Ассет `WBP_Pet_Tooltips_Win` ГЕНЕРИРУЕТСЯ этим
#   скриптом из `.JUI`. Правка, сделанная руками в ассете, живёт до первого переимпорта разметки
#   и молча исчезает (закон `tools-no-import-side-effects`). Закрепление в генераторе — и есть
#   настоящее лечение: переимпорт теперь не стирает правку, а ВОСПРОИЗВОДИТ её.
#
# ★ КЛЮЧ ОТКАТА. `asda.Pet.InfoWindow` сюда НЕ ДОТЯГИВАЕТСЯ и не может: это консольная
#   переменная C++, живущая в запущенной игре, а генератор — офлайновый python, который собирает
#   ассет заранее и про игру ничего не знает. Поэтому откат здесь СВОЙ и того же вида, каким порт
#   уже помечает отступления генераторов (`ASDA_STYLE926_SANMATCH_ALL`, `ASDA_ALLOW_LOGIN_WRITE`):
#   переменная окружения. `ASDA_PET_INFO_OPAQUE=0` при регенерации вернёт окну эталонный
#   `Tool_Tip`, то есть полупрозрачную подсказку, как в оригинале.
SKIN_OVERRIDE = {
    "pet_tooltips_win": "Black_BottomRound_White2Border",   # 43300 «О питомце», см. разбор выше
}
SKIN_OVERRIDE_ON = os.environ.get("ASDA_PET_INFO_OPAQUE", "1") != "0"


def skin_key_for(node, quiet=False):
    """Имя скина узла с учётом подмены выше. Без подмены — дословно `Name` из `.JUI`.
    `quiet=True` — тот же ключ, но без строки «ОТСТУПЛЕНИЕ» в журнале (★ волна 15, МЕЛК-3 ревизии optwin:
    пре-проход `caption_slot_yields` спрашивает тип скина у КАЖДОГО узла окна, и без этого флага строка
    для окон с подменой дублировалась; её по-прежнему пишет ровно один вызов — в `add_control`)."""
    if SKIN_OVERRIDE_ON:
        ov = SKIN_OVERRIDE.get((getattr(node, "idc", "") or "").lower())
        if ov:
            if not quiet:
                unreal.log("[скин] ★ ОТСТУПЛЕНИЕ 25.09.2026: окну «%s» вместо «%s» ставится «%s» "
                           "(непрозрачное окно по требованию владельца; откат ASDA_PET_INFO_OPAQUE=0)"
                           % (node.idc, node.name, ov))
            return ov.lower()
    return (getattr(node, "name", "") or "").lower()

spec = importlib.util.spec_from_file_location("J", os.path.join(HERE, "asda_jui_parse.py"))
J = importlib.util.module_from_spec(spec); sys.modules["J"] = J; spec.loader.exec_module(J)

EAL = unreal.EditorAssetLibrary
ATOOLS = unreal.AssetToolsHelpers.get_asset_tools()
B = unreal.AsdaUmgBuilder
_texcache = {}

# ★ РАЗМЕР ШРИФТА UI (замер оригинала, font_and_name_colors_re.md): пиксельный атлас FONT0.BIN даёт
#   CAP-HEIGHT 8 px, масштабирования нет, второго кегля в игре НЕТ (чат/окна/плашки одного размера).
#   Наш Arial при Size 13 давал cap 12.4 px = 1.55× крупнее оригинала → правильный размер 8.
FONT_PX = 8

# ★★★ ДОПУЩЕНИЕ О ВЫСОТЕ СТРОКИ (только для ЗАПЕКАНИЯ вертикальной центровки).
#   Импортёр работает без Slate и не может измерить шрифт, поэтому сдвиг считается по этому числу.
#   ЖИВОЙ ЗАМЕР (`asda.UI.DumpTextMetrics`, отчёт `ministate_name_align.md`): реальная
#   `GetMaxCharacterHeight` игрового шрифта на кегле 8 = 13-14 px ⇒ допущение верное.
#   ★ ОБЩАЯ ТОЧКА: то же число зашито в `AsdaUi::JuiBakedTextVOffset` (`kJuiGenAssumedLineH`) —
#   рантайм СНИМАЕТ этот запечённый сдвиг, прежде чем поставить ИЗМЕРЕННЫЙ. Если меняете
#   здесь — поменяйте и там, иначе подписи съедут на разницу/2.
GEN_TEXT_LINE_PX = 14
# ★ Откат правила прямоугольника подписи (07.10.2026, `J.label_slot`): `ASDA_LABEL_BAND_LEGACY=1` — прежняя высота
#   узла max(h, 14) (узел вылезает за низ контрола на TextPos.y + vy). Читается при импорте модуля импортёра.
LABEL_BAND_LEGACY = os.environ.get("ASDA_LABEL_BAND_LEGACY", "") == "1"


def V2(x, y): return unreal.Vector2D(float(x), float(y))
def BOX(uv): return unreal.Box2D(unreal.Vector2D(uv[0], uv[1]), unreal.Vector2D(uv[2], uv[3]))
def LC(c): return unreal.LinearColor(c[0], c[1], c[2], c[3])
WHITE = unreal.LinearColor(1, 1, 1, 1)

def fix_text(s):
    # Локализованный JUI (LanguageUpdate): EN(ascii, ~275) + KR(cp949, остальное). Русского в JUI НЕТ
    # (RU только в .BIN-данных, рантайм). Приоритет: EN как есть; KR → словарь-перевод (RU), иначе читаемый корейский.
    if not s: return ""
    b = s.encode("cp1251", "replace")
    if all(x < 0x80 for x in b): return s                    # EN/ascii/символы — как есть
    kr = b.decode("cp949", "replace")                        # high-байт = корейский cp949
    key = kr.strip().rstrip("~").strip()                     # нормализация: хвостовой ~ = дизайн-маркер
    return _TRANS.get(key, _TRANS.get(kr.strip(), kr))       # RU-перевод из словаря или читаемый корейский

# ---- полный парс планов JuiSkin: ES=план, SS=кадр-состояние внутри плана ----
# ★★★ Реализация ПЕРЕЕХАЛА в `asda_jui_parse.parse_skins_full` (модуль без зависимости от `unreal`),
#   чтобы теми же планами пользовался мета-генератор `asda_jui_window_meta.py` вне редактора.
parse_skins_full = J.parse_skins_full


def apply_ui_texture_settings(tex):
    # ★ Оригинал рисует ВЕСЬ UI с POINT-фильтром (D3DTEXF_POINT) — WindowMgr_RenderControls@0x97D8F0.
    #   Линейная фильтрация в UE размывает 5px-свотч, растянутый на весь контрол → «градиент».
    #   Фикс 1:1: Nearest + без мипов + UI-группа. Тогда стретч свотча = ровная пиксельная заливка.
    if not tex: return
    try:
        tex.set_editor_property("filter", unreal.TextureFilter.TF_NEAREST)
        tex.set_editor_property("mip_gen_settings", unreal.TextureMipGenSettings.TMGS_NO_MIPMAPS)
        tex.set_editor_property("lod_group", unreal.TextureGroup.TEXTUREGROUP_UI)
        tex.set_editor_property("never_stream", True)
        EAL.save_loaded_asset(tex)
    except Exception as e:
        unreal.log_warning("tex settings %s: %s" % (getattr(tex, "get_name", lambda: "?")(), e))

def ensure_texture(base):
    if not base: return None
    if base in _texcache: return _texcache[base]
    tp = "%s/%s" % (TEX_DEST, base); tex = None
    # ★ 16.09.2026 (Reports/dedup_audit/tex_rebind_2026-09-15.md, раздел 17): JUI-атлас окон логина (christmas_00/01, halloween,
    #   ne_godrobot02/03, tpl_talkwin/yellowwin; Tools/data/asda_login_packages.json) без ASDA_ALLOW_LOGIN_WRITE=1 не импортируется
    #   и не пересохраняется (apply_ui_texture_settings) — существующий только загружается для ссылок; строка [LOGIN-GUARD] — _login_blocked.
    if _login_blocked(tp):
        tex = unreal.load_asset(tp) if EAL.does_asset_exist(tp) else None
        if tex is None:   # ★ 16.09.2026 (раздел 19, СРЕДН-2): атласа нет, а импортировать его без разрешения нельзя —
            #   окно соберётся с ПУСТОЙ картинкой. БЫЛО: молча None, дефект виден только на экране.
            #   Поведение НЕ меняется (возвращается тот же None) — добавлена только явная строка в лог.
            unreal.log_warning("[LOGIN-GUARD] asda_jui_generic_umg.py: атлас отсутствует %s — окно собирается БЕЗ картинки "
                               "(импорт из %s.png — только с явного разрешения владельца и ASDA_ALLOW_LOGIN_WRITE=1)"
                               % (tp, os.path.join(TEX_SRC, base)))
        _texcache[base] = tex
        return tex
    if EAL.does_asset_exist(tp):
        tex = unreal.load_asset(tp)
        apply_ui_texture_settings(tex)   # правим уже импортированные (линейные) тоже
    else:
        src = os.path.join(TEX_SRC, base + ".png")
        if os.path.isfile(src):
            d = unreal.AutomatedAssetImportData(); d.set_editor_property("destination_path", TEX_DEST)
            d.set_editor_property("filenames", [src]); d.set_editor_property("replace_existing", True)
            for a in ATOOLS.import_assets_automated(d):
                try: apply_ui_texture_settings(a); tex = a
                except Exception: pass
    _texcache[base] = tex
    return tex

def san(s): return re.sub(r"[^A-Za-z0-9_]", "_", (s or "").strip()) or "win"

_wnames = set()   # занятые имена виджетов в текущем окне (интерактив по idc должен быть уникален)
# ★★★ Имена узлов-подписей для контролов класса S1a (текст только в `EXS TextContent`, `ID` пуст):
#   id(node) → `{ЯКОРЬ}_TXT{NN}`. Заполняется пре-проходом в `build_window` из ОБЩЕГО правила
#   `asda_jui_parse.missing_text_nodes` (им же пользуется шаг `asda_ui_missing_text_nodes.py`).
_TXTNAME = {}
# ★★★ Имена ИНТЕРАКТИВНЫХ узлов для контролов класса S2 (пустой `ID`, но скин-интерактив):
#   id(node) → `{ЯКОРЬ}_{BTN|CHK|SEL|CMB|EDT}{NN}`. Заполняется пре-проходом в `build_window` из
#   ОБЩЕГО правила `asda_jui_parse.missing_interactive_nodes` — им же пользуется шаг
#   `Tools/asda_ui_missing_interactive.py`, который достраивает эти узлы в УЖЕ СОБРАННЫЕ ассеты.
#   Совпадение имён и делает перегенерацию согласованной с шагом (шаг видит узел и не дублирует).
#   ⚠ В словарь попадают ТОЛЬКО ПРИНЯТЫЕ контролы: отсеянные (их кликабельный виджет строит код
#   порта либо новый узел съел бы клики у соседа) остаются как были — без интерактивного виджета.
_IACTNAME = {}
# ★★★ id(node) контролов, чей ПУСТОЙ слот подписи уступает имя `{idc}_Text` настоящему контролу окна
#   (общее правило `asda_jui_parse.caption_slot_yields`, 25.09.2026 — окно 50300). Заполняет
#   пре-проход `build_window`; тем же правилом пользуется шаг `asda_ui_caption_name_yield.py`.
_CAPYIELD = set()
_check_rects = {}  # idc чекбокса(upper) → (t,h) бокса; метка {idc}_TXT центрируется по вертикали БОКСА, не своей
def uniq_name(nm):
    nm = nm or "ctl"
    if nm not in _wnames: _wnames.add(nm); return nm
    i = 1
    while "%s_%d" % (nm, i) in _wnames: i += 1
    r = "%s_%d" % (nm, i); _wnames.add(r); return r

# ★ ОБЩАЯ ТОЧКА: реализация переехала в `asda_jui_parse` (модуль без `unreal`), чтобы теми же
#   прямоугольниками планов пользовался шаг `asda_ui_missing_interactive.py`.
plane_screen_rects = J.plane_screen_rects

# ── Проверка прозрачности UV-региона в атласе (PNG-альфа) — для авто-починки битых сегментов рамок ──
#   В UE-python нет PIL → минимальный декодер PNG (zlib + un-filter). Кэш на путь. Даёт долю непрозрачных пикселей.
import zlib as _zlib, struct as _struct
_RAW_TEX_DIR = r"@@ASTORY_DEV_ROOT@@\AstoryClient\RawAssets\Textures"
_png_cache = {}   # path → (W,H,pixels bytes,bpp,alpha_index|None)
_tex_path_cache = {}

def _find_png(tex):
    key = (tex or "").lower()
    if key in _tex_path_cache: return _tex_path_cache[key]
    found = None
    for root, _d, files in os.walk(_RAW_TEX_DIR):
        for f in files:
            if f.lower() == key + ".png": found = os.path.join(root, f); break
        if found: break
    _tex_path_cache[key] = found
    return found

def _load_png(path):
    if path in _png_cache: return _png_cache[path]
    try:
        data = open(path, "rb").read()
        assert data[:8] == b"\x89PNG\r\n\x1a\n"
        off = 8; W = H = bitd = ct = 0; idat = b""
        while off < len(data):
            ln = _struct.unpack(">I", data[off:off+4])[0]; typ = data[off+4:off+8]
            chunk = data[off+8:off+8+ln]; off += 12 + ln
            if typ == b"IHDR": W, H, bitd, ct = _struct.unpack(">IIBB", chunk[:10])
            elif typ == b"IDAT": idat += chunk
            elif typ == b"IEND": break
        if bitd != 8 or ct not in (4, 6):   # поддержка 8-бит RGBA/GA (у остального альфы нет — считаем непрозрачным)
            _png_cache[path] = (W, H, b"", 0, None); return _png_cache[path]
        raw = _zlib.decompress(idat); ch = 4 if ct == 6 else 2; bpp = ch; stride = W * bpp
        out = bytearray(); prev = bytearray(stride); pos = 0
        for _y in range(H):
            fl = raw[pos]; pos += 1; line = bytearray(raw[pos:pos+stride]); pos += stride
            if fl == 1:
                for i in range(bpp, stride): line[i] = (line[i] + line[i-bpp]) & 255
            elif fl == 2:
                for i in range(stride): line[i] = (line[i] + prev[i]) & 255
            elif fl == 3:
                for i in range(stride):
                    a = line[i-bpp] if i >= bpp else 0; line[i] = (line[i] + ((a + prev[i]) >> 1)) & 255
            elif fl == 4:
                for i in range(stride):
                    a = line[i-bpp] if i >= bpp else 0; b = prev[i]; c = prev[i-bpp] if i >= bpp else 0
                    p = a + b - c; pa = abs(p-a); pb = abs(p-b); pc = abs(p-c)
                    line[i] = (line[i] + (a if (pa <= pb and pa <= pc) else (b if pb <= pc else c))) & 255
            out += line; prev = line
        _png_cache[path] = (W, H, bytes(out), bpp, bpp - 1)   # alpha = последний байт пикселя
    except Exception:
        _png_cache[path] = (0, 0, b"", 0, None)
    return _png_cache[path]

def _uv_opaque_ratio(tex, uv):
    """Доля непрозрачных пикселей в UV-регионе (0..1). Нет данных альфы → 1.0 (считаем видимым)."""
    path = _find_png(tex)
    if not path: return 1.0
    W, H, px, bpp, ai = _load_png(path)
    if ai is None or not px or W == 0: return 1.0
    u0, v0, u1, v1 = uv
    x0, x1 = sorted((int(min(u0, u1) * W), int(max(u0, u1) * W)))
    y0, y1 = sorted((int(min(v0, v1) * H), int(max(v0, v1) * H)))
    x0 = max(0, x0); y0 = max(0, y0); x1 = min(W-1, max(x0, x1)); y1 = min(H-1, max(y0, y1))
    op = tot = 0
    for yy in range(y0, y1+1):
        base = yy * W * bpp
        for xx in range(x0, x1+1):
            tot += 1
            if px[base + xx*bpp + ai] >= 128: op += 1
    return (op / tot) if tot else 1.0

def _uv_flip(uv, mode):
    u0, v0, u1, v1 = uv
    if mode == "x":  return (u1, v0, u0, v1)
    if mode == "y":  return (u0, v1, u1, v0)
    return (u1, v1, u0, v0)   # xy

def nine_slice_grid(planes, W, H):
    """Истинный 9-slice для скинов-рамок типа ResizeWindow (Win_Tab, *_Border): планы образуют
    сетку 3×3 (ровно 4 x-границы и 4 y-границы). Углы — ФИКС-размер (дизайн-пиксели), края
    тянутся по одной оси, центр — по обеим. Так рамка не «плывёт». Возврат None если не 3×3."""
    xs = sorted(set(round(v, 2) for p in planes for v in (p["rect"][0], p["rect"][2])))
    ys = sorted(set(round(v, 2) for p in planes for v in (p["rect"][1], p["rect"][3])))
    if len(xs) != 4 or len(ys) != 4: return None
    sW, sH = xs[3], ys[3]; lb, rb, tb, bb = xs[1], xs[2], ys[1], ys[2]
    if W < sW or H < sH: return None                              # контрол меньше шаблона — не грид-путь
    def mapx(c):
        if c <= lb: return c
        if c >= rb: return W - (sW - c)
        return lb + (c - lb) / (rb - lb) * (W - lb - (sW - rb)) if rb > lb else c
    def mapy(c):
        if c <= tb: return c
        if c >= bb: return H - (sH - c)
        return tb + (c - tb) / (bb - tb) * (H - tb - (sH - bb)) if bb > tb else c
    out = []
    for p in planes:
        x0, y0, x1, y1 = p["rect"]
        rx0, rx1, ry0, ry1 = mapx(x0), mapx(x1), mapy(y0), mapy(y1)
        out.append((rx0, ry0, max(rx1 - rx0, 0.5), max(ry1 - ry0, 0.5), p))
    # ★ АВТО-ПОЧИНКА битых сегментов рамки: у части скинов (напр. Log_GiantGod_TalkWin) UV нижней/правой сторон
    #   указывают на ПРОЗРАЧНЫЙ участок атласа → грань не рисуется. Рамка 9-slice симметрична → берём UV зеркального
    #   валидного сегмента (право←лево flipX, низ←верх flipY, угол←диагональ flipXY). Части остаются отдельными.
    tex = planes[0]["tex"] if planes else ""
    if tex:
        gx = {xs[0]: 0, xs[1]: 1, xs[2]: 2}; gy = {ys[0]: 0, ys[1]: 1, ys[2]: 2}
        cells = {}
        for idx, (_rx, _ry, _rw, _rh, p) in enumerate(out):
            cx = gx.get(round(p["rect"][0], 2)); cy = gy.get(round(p["rect"][1], 2))
            if cx is not None and cy is not None: cells[(cx, cy)] = idx
        for (c, r), idx in cells.items():
            p = out[idx][4]
            if _uv_opaque_ratio(tex, p["uv"]) >= 0.05: continue      # сегмент видим — не трогаем
            for (mc, mr, fl) in ((2 - c, r, "x"), (c, 2 - r, "y"), (2 - c, 2 - r, "xy")):
                src = cells.get((mc, mr))
                if src is None: continue
                su = out[src][4]["uv"]
                if _uv_opaque_ratio(tex, su) < 0.05: continue
                np = dict(p); np["uv"] = _uv_flip(su, fl)
                out[idx] = out[idx][:4] + (np,)
                break
    return out

# ═══════════════ ★★★ КОНТЕКСТ ОКНА: РУЧКИ ПЕРЕТАСКИВАНИЯ + ХИТ-ТЕСТ (данные .JUI) ═══════════════
# Полный разбор — `Reports/ui_window_drag_and_hittest.md`.
#   • ручка = контрол с `EVS{EVT=6, RET=0x20003, CtrlID=<IDNum цели>}`; движок сдвигает ЦЕЛЬ на
#     дельту курсора (`uiControl_RepositionByDelta @0x12F1C30`). Списка окон в коде нет и не будет.
#   • `NoSelect="1"` = клик проходит сквозь контрол (`uiControlHitTest @0x12DB470`).
#   • у контрола со скином без текстур (напр. `NoImage`) план ЕСТЬ и клик он ЛОВИТ, а виджета в WBP
#     не появлялось — для таких создаём невидимую hit-зону `<base>_HIT` (ручкам — `<base>_DRAG`).
DRAG_EVT = 6
DRAG_RET = 0x20003
_CTX = None   # словарь контекста текущего окна (см. build_window)


def _is_drag_handle(node):
    for e in getattr(node, "events", ()):
        if e["evt"] == DRAG_EVT and e["ret"] == DRAG_RET:
            return e
    return None


def _apply_text_outline(tb):
    """★★★ ЧЁРНАЯ ОБВОДКА 1px текста — свойство СКИНА `TextOutLine = "1"` (дефект Д8, вторая половина).
    Источник подтверждён дважды: (1) данные — TextOutLine есть ТОЛЬКО в JuiSkin.ini (182 записи,
    значения строго "0"/"1" → толщина константная 1px), в *_jui.ini его нет; (2) декомпил —
    sub_12E4D50 @0x12E4D50 читает его из cfg-map скина в CTextControl+3700 (dword 925), label-рендер
    @ui_core.c:110729 при ==1 добавляет флаг 0x2010 (контурный режим шрифта). Цвет — чёрный
    (эталонные скрины юзера, quick_win_port.md Д8; так же в рантайм-фиксах ApplyStackTextStyle).
    Правится тем же приёмом, что asda_fix_minimap_ui.apply_text (прецедент Python-обводки)."""
    if tb is None or not isinstance(tb, unreal.TextBlock):
        return
    try:
        f = tb.get_editor_property("font")
        o = f.get_editor_property("outline_settings")
        o.set_editor_property("outline_size", 1)
        o.set_editor_property("outline_color", unreal.LinearColor(0, 0, 0, 1))
        # Обводка рисуется и под полупрозрачным текстом — как контур в оригинале (ср. ApplyTextOutline C++).
        try: o.set_editor_property("separate_fill_alpha", False)
        except Exception: pass
        f.set_editor_property("outline_settings", o)
        tb.set_editor_property("font", f)
    except Exception as e:
        unreal.log_warning("обводка %s: %s" % (tb.get_name(), e))


# ★ ОБЩАЯ ТОЧКА: разбор состояний подписи переехал в `asda_jui_parse.caption_state_args`
#   (реверс и обоснование — в докстринге там). Тем же разбором пользуется шаг
#   `asda_ui_missing_interactive.py`, достраивающий кнопки контролов класса S2.
_caption_state_args = J.caption_state_args


def _apply_caption_states(btn, node, plane):
    """Записать состояния подписи в `FButtonStyle` кнопки (см. `UAsdaUmgBuilder::ApplyButtonCaptionStates`).
    Цвет/сдвиг соседнего `<Кнопка>_Text` применяет рантайм — `AsdaUi::BindButtonCaptionStates`."""
    if btn is None:
        return False
    args = _caption_state_args(node, plane)
    if not args:
        return False
    c0, c1, c2, off0, off2 = args
    try:
        B.apply_button_caption_states(btn, LC(c0), LC(c1), LC(c2),
                                      V2(off0[0], off0[1]), V2(off2[0], off2[1]))
        return True
    except Exception as e:
        unreal.log_warning("состояния подписи %s: %s" % (btn.get_name(), e))
        return False


def add_control(bp, canvas, node, idx, ox=0.0, oy=0.0):
    # ox,oy = origin родителя-контейнера (ИЕРАРХИЯ): все позиции узла рисуем ЛОКАЛЬНО относительно него,
    #   чтобы контрол лежал ВНУТРИ своей группы (CanvasPanel) и двигался вместе с ней. Верхний уровень ox=oy=0.
    l, t, w, h = node.abs
    l -= ox; t -= oy
    base = "%s_%d" % (san(node.idc or node.name), idx)
    if _DBGF is not None:
        _DBGF.write("%s\t%.1f\t%.1f\t%.1f\t%.1f\n" % (base, l, t, w, h))
    made = []   # созданные виджеты (для сворачивания скрытых)
    # ★ Имя скина берётся через `skin_key_for`, а не напрямую из `.JUI`: у окна 43300 оно
    #   подменяется по требованию владельца 25.09.2026 (полный разбор — у `SKIN_OVERRIDE`).
    sk = SKINS.get(skin_key_for(node))
    stype = (sk or {}).get("type", "")
    planes = []
    if sk and sk["planes"] and w > 0 and h > 0:
        # видимые планы с картинкой; FillMode=5 = план без картинки (только текст) → пропуск
        planes = [p for p in sk["planes"] if p["tex"] and not p["invisible"] and p.get("fillmode", 0) != 5]
    # ★★★ ИМЯ ИНТЕРАКТИВНОГО ВИДЖЕТА. Раньше здесь стояло `bool(node.idc)`, и у контрола с ПУСТЫМ
    #   `ID` кликабельного виджета не появлялось вовсе — класс дефектов S2 аудита
    #   (`Reports/audit_runtime_widget_creation.md:55`): 151 контрол в 24 окнах нарисован,
    #   но нажать не по чему. Теперь при пустом `ID` имя берётся из ОБЩЕГО правила
    #   `J.missing_interactive_nodes` (пре-проход `_IACTNAME`), а контролы, которым узел ставить
    #   НЕЛЬЗЯ (кликабельный виджет уже строит порт; либо новый узел накрыл бы соседнюю кнопку),
    #   в словарь не попадают и ведут себя как прежде.
    iname = san(node.idc) if node.idc else _IACTNAME.get(id(node))
    # Интерактив рисует СВОЙ фон (StyledButton/CheckBox по кадрам) → план0 как image НЕ дублируем.
    is_btn1 = bool(iname) and stype == "PushButton" and len(planes) == 1 and w > 0 and h > 0
    # CheckButton (toggle, напр. LOGIN_CHECK/Log_Box_Check) сам рисует свой план кадрами SS0(unchecked)/SS2(checked)
    #   через UCheckBox → план0 как статик-image НЕ дублируем. RadioButton (gender Log_CaracterSex, 2 плана = 2 выбора)
    #   рисует ВСЕ планы как image (визуал), клик — отдельными OPT-кнопками.
    is_check = bool(iname) and stype == "CheckButton" and len(planes) >= 1 and w > 0 and h > 0
    iact_own_bg = is_btn1 or is_check
    if planes:
        # ★ 19.09.2026: у полосы прокрутки (`ScrollBar`) тянется только жёлоб, стрелки и бегунок держат
        #   свой размер (`uiScrollbarLayoutPlanes`) — см. `J.scrollbar_plane_rects`.
        layout = None
        if stype == "ScrollBar":
            layout = J.scrollbar_plane_rects(planes, w, h, (sk or {}).get("scrollbar_type", 1) != 0)
        layout = layout or (nine_slice_grid(planes, w, h) if len(planes) >= 5 else None) or plane_screen_rects(planes, w, h)
        for j, (rx, ry, rw, rh, p) in enumerate(layout):
            if iact_own_bg and j == 0: continue   # план0 рисует сам интерактив-виджет (кадры состояний)
            tex = ensure_texture(p["tex"])
            if not tex: continue
            wn = "%s_%d" % (base, j)
            # ★ Анимир. plane (Animate="1") → UAsdaAnimatedPlane (система кадровой анимации), иначе статичный image.
            if p.get("animate") and p.get("total_frame", 0) > 0 and p.get("frame_interval", 0) > 0 \
               and p.get("frame_px", (0, 0))[0] > 0 and p.get("frame_px", (0, 0))[1] > 0:
                fpx = p["frame_px"]
                # ★★★ ЗАЦИКЛЕННОСТЬ — ИЗ ДАННЫХ (`EFrameStop` контрола), а не жёсткое `True`
                #   (правка 31.08.2026, долг импортёра №2). Раньше здесь стояла константа `True`,
                #   и ВСЕ анимпланы приходили в виджет зациклёнными: признак `EFrameStop` из
                #   `gamemain_jui.ini` в ассет не попадал вовсе, режим «один прогон и стоп»
                #   приходилось выставлять кодом подсистем.
                #   ОСНОВАНИЕ (данные, все 4 ini): `EFrameStop` объявлен у КАЖДОГО контрола
                #   (18 924 строки в gamemain), значение "1" — у 216, и у 215 из них скин несёт
                #   АНИМИРОВАННЫЙ план — то есть признак относится ровно к кадровой анимации.
                #   Окна с "1": SKILL_WIN 90, QUICK_NEW_WIN 48, QUICK_WIN 31, MINISTATE_WIN 7,
                #   ITEM_making_Mark_WIN 7, Pet_Hatchery_Win 5, Item_Initializ_Win 5 и ещё 13 окон.
                #   ⚠ ЧИСЛО ПРОГОНОВ в данных НЕ ЛЕЖИТ: `MaxLoops` виджета остаётся 0 (при
                #   `bLoop=False` эффективный лимит = 1 прогон — `UAsdaAnimatedPlane`), а лимит >1
                #   ставит рантайм, как оригинал (`ctrlShowWithItemFlags(ctrl, 2)`, ui_core.c:109888).
                #   Поэтому плашки рулетки (67007…67010 / 67015…67018) правка НЕ меняет: у них в
                #   .ini `EFrameStop="0"`, их два прогона по-прежнему задаёт код.
                loop = (getattr(node, "frame_stop", 0) != 1)
                try: made.append(B.add_animated_plane(bp, canvas, wn, V2(l + rx, t + ry), V2(rw, rh),
                    tex, BOX(p["uv"]), LC(p["diffuse"]), V2(fpx[0], fpx[1]),
                    int(p["total_frame"]), float(p["frame_interval"]), loop))
                except Exception: pass
            else:
                try: made.append(B.add_image(bp, canvas, wn, V2(l + rx, t + ry), V2(rw, rh), tex, BOX(p["uv"]), LC(p["diffuse"]), False, NO_M))
                except Exception: pass
    # ★ Раскладка текста 1:1 по эталону: шрифт 14px; CaptionInfo=offX,offY,colorARGB,alignMask
    #   (0x1 hCenter,0x2 hRight,0x4 vCenter,0x8 vBottom).
    cap = fix_text(getattr(node, "caption", "") or "")
    # ★★★ ТЕКСТ ПОДПИСИ: `CaptionText`, А ПРИ ЕГО ПУСТОТЕ — `EXS TextContent` (правка 2026-08-27,
    #   класс дефектов S1a аудита `Reports/audit_runtime_vs_assets_SUMMARY.md`).
    #   ОСНОВАНИЕ (эталон): `CTextControl_LoadProps` (`font_text.c:9721-9735`) читает `TextContent`
    #   и отдаёт его В САМ КОНТРОЛ через `CControl_SetText` — это НАСТОЯЩИЙ текст Text-контрола;
    #   `CaptionText` же — отдельное поле-подпись, его рисует `cControl_RenderLabel`
    #   (`ui_core.c:110741`). Раньше здесь бралось ТОЛЬКО `CaptionText`, и у контрола с ПУСТЫМ
    #   `ID`, пустым `CaptionText` и живым `EXS TextContent` не появлялось НИ ОДНОГО текстового
    #   узла: именованная ветка ниже требует `node.idc`, а caption-ветка — непустой `cap`.
    #   СВЕРКА ПО ДАННЫМ (все 4 ini, сухой прогон `set ASDA_MISSTXT_DRYRUN=1` +
    #   `Tools/asda_ui_missing_text_nodes.py`, лог `Reports/headless_misstxt_dry2.log`): 209 контролов
    #   в 19 окнах `gamemain`; в `commonwin`/`temp`/`login` — ни одного. Текст НЕ выдуман: он
    #   лежит в `EXS TextContent` и проходит тот же `fix_text` (cp949 → словарь перевода).
    if not cap.strip():
        cap = fix_text(getattr(node, "txt_content", "") or "")
    # ★★★ ВЫРАВНИВАНИЕ — ИЗ `EXS TextAlign`, если оно есть (правка 2026-08-06).
    #   Это ТА ЖЕ ошибка, что была с цветом (закон ui-text-color-exs): `CaptionInfo[3]` — маска
    #   подписи-caption, а у Text-контролов работает `EXS TextAlign` (эталон CTextControl_LoadProps,
    #   поле this+3696). Симптом: MINISTATE_HPTEXT/MPTEXT/CharName имеют CaptionInfo-маску 0
    #   (лево/верх) при `EXS TextAlign = 5` (центр+центр) — числа HP/MP стояли не по центру полос.
    #   Семантика маски одинаковая: 0x1 hCenter, 0x2 hRight, 0x4 vCenter, 0x8 vBottom.
    #   Сверка по всем 4 ini (2026-08-08): Text-контролов 7862, `EXS TextAlign` есть у ВСЕХ (0 без);
    #   у 6518 он ОТЛИЧАЕТСЯ от CaptionInfo[3] (дефект Д8). У НЕ-Text контролов с подписью EXS
    #   TextAlign НЕ бывает (0 из всех) → фолбэк на cap_align обслуживает ровно caption-путь.
    #   ★ 07.10.2026: правило вынесено в ОБЩУЮ ТОЧКУ парсера (`J.label_mask` / `J.label_justify`) — ею же пользуется
    #   шаг пост-импорта `asda_fix_minimap_ui` (ревизия `poststeps_idempotent_REVIEW.md`, СРЕДН-1). Поведение то же.
    m = J.label_mask(node)
    halign = J.label_justify(m)
    # ★★★ СМЕЩЕНИЕ ТЕКСТА — у Text-контролов из `EXS TextPos` (эталон label-рендер ui_core.c:110726:
    #   rectLeft += this[152]; rectTop += this[153] — это TextPos из CTextControl_LoadProps), а
    #   `CaptionInfo[0..1]` — смещение ПОДПИСИ-caption (та же семья, что цвет/выравнивание; 1265
    #   Text-контролов имеют TextPos ≠ CaptionInfo-off, обычно ±1px). Caption-путь остаётся на cap_off.
    is_text_skin = (stype == "Text")
    tp = getattr(node, "txt_pos", None)
    if is_text_skin and tp is not None:
        offx, offy = tp
    else:
        offx, offy = getattr(node, "cap_off", (0, 0))
    # ★★★ ЦВЕТ ТЕКСТА — ТОЛЬКО ИЗ `EXS TextColor` (эталон `CTextControl_LoadProps`, поле this+3692).
    #   `CaptionInfo[2]` — цвет ПОДПИСИ-caption и рисуется ЛИШЬ при непустом `CaptionText`; у 5374
    #   узлов там дизайнерский маркер `ff400040` (фиолетовый), и раньше он попадал В ТЕКСТ — отсюда
    #   «фиолетовые заголовки окон». Сверка по всем 4 ini: 7180 Text-контролов имели цвет из
    #   CaptionInfo, отличный от EXS TextColor, из них 5373 — фиолетовые; без EXS TextColor —
    #   ровно ОДИН Text-контрол (для него и только для него остаётся фолбэк на caption-цвет).
    txtcol = getattr(node, "txt_color", None)
    col = LC(txtcol if txtcol else getattr(node, "cap_color", (1, 1, 1, 1)))
    # ★★★ ПРЯМОУГОЛЬНИК УЗЛА-ПОДПИСИ — ОБЩАЯ ТОЧКА `J.label_slot` (правка 07.10.2026, отчёт
    #   `Reports/owner_1007/label_band_and_8500.md`). Верх строки как прежде: t + TextPos.y + vy (vy — запечённая
    #   однострочная центровка по маске, (h−14)/2 | h−14 | 0). БЫЛО: высота узла max(h, 14) — узел, сдвинутый на vy,
    #   вылезал за низ контрола (полоса 9.5 px под рамкой магазина NPC, 7 px под чатом ловила щелчок в мир через
    #   хит-тест окна). СТАЛО: у такого узла низ — по низу контрола (не меньше строки 14), видимый текст не сдвигается.
    #   Откат — `ASDA_LABEL_BAND_LEGACY=1` (прежний вид; им же шаг `asda_ui_label_band.py` узнаёт «было»).
    #   Обрезка — только у узла, который вылез бы за НИЗ РАМКИ ОКНА (низ корневого контрола .JUI в координатах этого
    #   родителя; разбор «почему только за рамкой» — у `J.label_slot`).
    _fb = (_CTX["root_bottom"] - oy) if (_CTX is not None and _CTX.get("root_bottom") is not None) else None
    tx, ty, tw, th = J.label_slot(l, t, w, h, offx, offy, m, legacy=LABEL_BAND_LEGACY, frame_bottom=_fb)
    # ★ ИНТЕРАКТИВ/МЕТКА по типу скина. Состояния = КАДРЫ плана (button_states_re.md, 1:1, БЕЗ выдумок):
    #   кадр0=normal/unchecked, 1=hover, 2=pressed/checked, 3=selected. Каждый кадр = свой UV/tex/Diffuse.
    # ★ ОБЩАЯ ТОЧКА с шагом `asda_ui_missing_interactive.py` — реализация в парсере.
    frame_of = J.plane_frame
    def jui_frame(pl, i):
        """Кадр-состояние плана → `unreal.AsdaJuiFrame` (текстура + UV + Diffuse-тинт).
        ★ У кадров ОДНОГО плана различаться может ВСЁ ТРИ: у 141 контрола со скином `Box_Check`
          кадры 0/1 отличаются только Diffuse (0.79 приглушён → белый на наведении), кадры 2/3
          несут другой UV (бокс с галочкой), а у `MapName_Elite/Indun/Normal` кадр 3 лежит
          вообще в ДРУГОМ атласе (`ne_skillbar3`). Поэтому кадр передаётся структурой целиком."""
        tn, uv, dif = frame_of(pl, i)
        fr = unreal.AsdaJuiFrame()
        fr.set_editor_property("texture", ensure_texture(tn))
        fr.set_editor_property("uv", BOX(uv))
        fr.set_editor_property("tint", LC(dif))
        return fr
    text_done = False
    # ★ `Text` остаётся в ветке ТОЛЬКО с настоящим `ID`: у безымянных Text-контролов свой класс
    #   дефектов (S1a) и своё правило имени `{ЯКОРЬ}_TXT{NN}` — см. `_TXTNAME` и caption-ветку ниже.
    if (iname or (node.idc and stype == "Text")) \
            and stype in ("PushButton", "EditBox", "ComboBox", "CheckButton", "RadioButton", "Text") \
            and w > 0 and h > 0:
        wname = uniq_name(iname or san(node.idc))
        try:
            if stype == "PushButton" and is_btn1:
                # StyledButton: кадр0=Normal, кадр1=Hover, кадр2=Pressed — РЕАЛЬНЫЕ UV/тинты кадров из скина.
                t0, u0, d0 = frame_of(planes[0], 0); t1, u1, d1 = frame_of(planes[0], 1); t2, u2, d2 = frame_of(planes[0], 2)
                tex0 = ensure_texture(t0)
                if tex0:
                    btnw = B.add_styled_button(bp, canvas, wname, V2(l, t), V2(w, h), tex0,
                        BOX(u0), LC(d0), BOX(u1), LC(d1), BOX(u2), LC(d2), "")
                else:
                    btnw = B.add_button(bp, canvas, wname, V2(l, t), V2(w, h))
                made.append(btnw)
                _apply_caption_states(btnw, node, planes[0])
            elif stype == "PushButton":
                # Кнопка с НЕСКОЛЬКИМИ планами: картинку рисуют планы, кнопка — прозрачная ловушка
                #   клика. Состояния ПОДПИСИ у неё всё равно есть (они живут в кадрах плана 0).
                btnw = B.add_button(bp, canvas, wname, V2(l, t), V2(w, h))
                made.append(btnw)
                if planes:
                    _apply_caption_states(btnw, node, planes[0])
            elif stype == "EditBox":
                # ★★★ ЦВЕТ ПОЛЯ ВВОДА — ИЗ `EXS TextColor` (правка 2026-08-27, класс С2 аудита
                #   `Reports/audit_runtime_style.md`). Раньше сюда жёстко уходил `WHITE`, и
                #   цвет из данных терялся у ВСЕХ полей ввода.
                #   СВЕРКА ПО ДАННЫМ (все 4 ini, парсер импортёра): контролов со скином типа
                #   `EditBox` — 66 (62 gamemain + 4 login), и `EXS TextColor` есть у ВСЕХ 66
                #   (без него — НИ ОДНОГО). Значения не одинаковые: `FFC6C6C6` у большинства,
                #   `FFFFFFFF` у полей цены лотка/аукциона, `FF9DFF9D` у имени питомца,
                #   `FFFF1740` у поля крика, `FF000000` у «о себе», `FFDCDCDC` у чата лотка.
                #   ⚠ Поле чата 2609 (`CHAT_NEW_Edit2`) — ИСКЛЮЧЕНИЕ ОРИГИНАЛА, а не наше:
                #   эталон `chat.c:831-833` ПОВЕРХ данных ставит ему `0xCCFFFFFF`, и порт делает
                #   то же в `AsdaChatWindowSubsystem::BuildInputWidgets` — эта правка законна.
                #   ★ ОБЩАЯ ТОЧКА: то же значение кладёт в УЖЕ СОБРАННЫЕ окна идемпотентный шаг
                #   `Tools/asda_ui_editbox_colors.py` (он же в `STEPS` пост-импорта).
                edw = B.add_editable_text(bp, canvas, wname, V2(l, t), V2(w, h), "", FONT_PX, col, False)
                made.append(edw)
                # ★★★ ВЫРАВНИВАНИЕ ТЕКСТА ПОЛЯ — ИЗ `EXS TextAlign` / `EXS TextPos` (правка 26.09.2026, окно 77500,
                #   `Reports/craft/craft_owner_items.md` §3). Эталон `uiEditBoxRender@0x12FA100` (ui_core.c:117132)
                #   рисует текст поля флагами `TextAlign (this+3240) | 0x2010` в rect поля, сдвинутом на `TextPos`
                #   (`CEditBox_LoadFromXml`, font_text.c:9926-9937). Раньше поле всегда было «слева сверху».
                #   ОБЩАЯ ТОЧКА с шагом пост-импорта `asda_ui_text_block_align` (функция `editbox_rule`).
                #   ★★★ ОХВАТ — ТОЛЬКО ОКНА РЕМЕСЛА (`_CTX["align_scope"]`, закрытый список
                #   `asda_ui_text_block_align.IMPORTER_SCOPE_WIN_IDS`; ревизия 26.09.2026 СРЕДН-1). Вне ремесла
                #   правило поменяло бы 62 поля ввода (чат, нумпад, лоток, обмен …) — это отдельное решение владельца
                #   (реестр PORT_PENDING_ON_SYSTEMS.md); прочим окнам — прежний вид (LEFT, rect контрола).
                try:
                    import asda_ui_text_block_align as TBA
                    _tp = getattr(node, "txt_pos", None) or (0, 0)
                    _er = TBA.editbox_rule(l, t, w, h, float(_tp[0]), float(_tp[1]), m) \
                        if (_CTX is not None and _CTX.get("align_scope")) else None
                    if edw and _er:
                        (_el, _et, _ew, _eh), _ej = _er
                        edw.set_editor_property("justification", getattr(unreal.TextJustify, TBA._JUSTIFY[_ej]))
                        TBA._set_slot(edw, (_el, _et, _ew, _eh), (0.0, 0.0), False)
                except Exception as _e:                                   # noqa: BLE001
                    unreal.log_warning("[jui] выравнивание поля %s не применено: %s" % (wname, _e))
            elif stype == "ComboBox":
                made.append(B.add_combo_box(bp, canvas, wname, V2(l, t), V2(w, h)))   # опции задаёт рантайм (напр. созвездие)
            elif stype == "CheckButton":
                # ★ Настоящий toggle-чекбокс (LOGIN_CHECK / Log_Box_Check): UCheckBox с ТОЧНЫМ именем idc (по нему
                #   C++ зовёт SetIsChecked). НЕ OPT-планы (это не мульти-выбор).
                # ★★★ ПОЛНЫЙ НАБОР СОСТОЯНИЙ (правка дефекта Д6, 2026-08-08). Раньше сюда уходили только
                #   кадры 0 и 2, БЕЗ тинтов, а hovered/pressed получали ту же кисть — все 156 кнопок-тогглов
                #   порта приходили «мёртвыми» (нарушение закона ui-button-hover-click-rule). Скан всех 38
                #   CheckButton-скинов JuiSkin.ini: у КАЖДОГО ровно 1 план и ровно 4 кадра SS0..SS3, и
                #   различаются они по-разному — 11 скинов только Diffuse, 10 Diffuse+UV, 5 только UV,
                #   3 ещё и ТЕКСТУРОЙ (MapName_*). Отдаём все четыре кадра целиком, раскладку по шести
                #   состояниям Slate делает `UAsdaUmgBuilder::AddCheckBox` (button_states_re.md).
                if planes:
                    made.append(B.add_check_box(bp, canvas, iname, V2(l, t), V2(w, h),
                                                jui_frame(planes[0], 0),    # normal / снято
                                                jui_frame(planes[0], 1),    # наведение
                                                jui_frame(planes[0], 2),    # отмечено / нажато
                                                jui_frame(planes[0], 3)))   # отмечено + наведение
            elif stype == "RadioButton":
                # Мульти-выбор (напр. гендер ♂ ES0 / ♀ ES1): планы-иконки уже нарисованы (нативный Rect), каждый план =
                #   отдельный ВЫБОР → на КАЖДЫЙ план клик-кнопка {idc}_OPT{i} над его rect.
                #   C++ бинжит OPT0→муж, OPT1→жен (клик по иконке выбирает её, НЕ toggle). Планы=визуал, кнопки=ввод.
                # ★★★ 2026-08-08: кнопка больше не голый NoDraw — ей отдаются кадры 1 (наведение) и 2
                #   (нажатие) СВОЕГО плана, а покой остаётся прозрачным. Так вариант получает hover/press
                #   (закон ui-button-hover-click-rule), но НЕ перекрывает план-картинку, которую рантайм
                #   красит при выборе (`AAsdaLoginGameMode::UpdateGenderHighlight` и т.п.).
                lay = plane_screen_rects(planes, w, h)
                for pj, (rx, ry, rw, rh, _p) in enumerate(lay):
                    made.append(B.add_option_button(bp, canvas, "%s_OPT%d" % (iname, pj),
                                                    V2(l + rx, t + ry), V2(rw, rh),
                                                    jui_frame(_p, 1), jui_frame(_p, 2)))
            elif stype == "Text":   # метка: именованный TextBlock (даже пустой — C++ ставит текст в рантайме)
                # ★ Рисуем НА СЫРОМ rect из JUI (EXS TextPos + EXS TextAlign), как оригинал — без нашей
                #   вертикальной центровки. wrap ТОЛЬКО многострочным (h>=28 = 2+ строки 13px): autowrap
                #   в CanvasPanel ломает right-justify и даёт наезд колонок (закон ui-autowrap-canvas-gotcha).
                tbw = B.add_text(bp, canvas, wname, V2(tx, ty), V2(tw, th), cap, FONT_PX, col, halign,
                                 h >= 28)
                made.append(tbw)
                # ★★★ ПЕРЕНОСИМЫЙ ТЕКСТ ЦЕНТРИРУЕТСЯ ВСЕМ БЛОКОМ СТРОК (правка 26.09.2026, окна ремесла,
                #   `Reports/craft/craft_owner_items.md` §3). Эталон `textFont_DrawRichText` (font_text.c:8471-8476,
                #   8895-8900): при вертикальном центре верх текста = середина rect − шаг строки × ЧИСЛО СТРОК / 2.
                #   Раньше узел стоял как однострочный (верх = середина − 7), и вторая строка сползала на нижнюю
                #   границу (имя рецепта 76024+10j). Область — `is_wrap_block` (перенос у оригинала: `TextOutLine`
                #   скина = 1). ⚠ У ПРЯМЫХ ДЕТЕЙ КОРНЯ окна после `add_control` работает `_apply_anchor` (Python,
                #   ниже в этом файле) — он сохраняет `Alignment` ровно у узлов из `_CTX["keep_align"]`, которые
                #   помечает эта ветка (иначе 77532 `ITEM_making_M_text`, `mark_d_1/2` уехали бы на полразмера).
                #   C++ `UAsdaUmgBuilder::ApplyGroupAnchor` к этому НЕ относится (вызовов у него нет).
                #   ОБЩАЯ ТОЧКА с шагом пост-импорта `asda_ui_text_block_align` (функция `wrap_block_slot`).
                #   ★★★ ОХВАТ — ТОЛЬКО ОКНА РЕМЕСЛА (`_CTX["align_scope"]`; ревизия 26.09.2026 СРЕДН-1). Вне ремесла
                #   правило поменяло бы 1903 текста в 154 окнах (склад, сумка, обмен, ручки `*_WIN_MOVE` …) —
                #   это отдельное решение владельца (реестр PORT_PENDING_ON_SYSTEMS.md); прочим окнам — прежний вид.
                try:
                    import asda_ui_text_block_align as TBA
                    if tbw and _CTX is not None and _CTX.get("align_scope") \
                            and TBA.is_wrap_block(stype, h, (sk or {}).get("textoutline", 0), m):
                        _wb = TBA.wrap_block_slot(l, t, w, h, offx, offy, m)
                        tbw.set_editor_property("auto_wrap_text", False)
                        tbw.set_editor_property("wrap_text_at", float(_wb["wrap_at"]))
                        TBA._set_slot(tbw, (_wb["pos"][0], _wb["pos"][1], 0.0, 0.0), _wb["align"], True)
                        _CTX["keep_align"].add(tbw.get_name())
                except Exception as _e:                                   # noqa: BLE001
                    unreal.log_warning("[jui] блок переноса %s не применён: %s" % (wname, _e))
                # ★★★ ОБВОДКА ЗДЕСЬ БОЛЬШЕ НЕ СТАВИТСЯ (правка 2026-08-27).
                #   Прежнее правило «скиновый TextOutLine=1 ⇒ обводка» НЕВЕРНО и накрывало
                #   7059 контролов из 7862. Поле 925 (`TextOutLine`) выбирает ФЛАГИ ПЕРЕНОСА строк
                #   (`cControl_RenderLabel`, ui_core.c:110741-110746), а обводку даёт поле 926
                #   (`0x800000`, font_text.c:6952). По оригиналу обводка положена 747 контролам.
                #   Стиль текста расставляет шаг пост-импорта `asda_ui_text_style926.py`
                #   по таблице `Tools/data/text_style926.tsv` (реверс всех 252 вызовов двух сеттеров).
                text_done = True
        except Exception: pass
    # подпись НЕ-Text контролов (кнопки/окна). Имя = {idc}_Text (находимо из C++ → рантайм-loc-подпись
    #   по string-ID из error list.bin, как игра; baked-caption = фолбэк). Иначе base_txt.
    # ★ Горизонт-выравнивание caption кнопки по cap_off (offsetX): смещён вправо → иконка слева → текст ВПРАВО;
    #   смещён влево → иконка справа → текст ВЛЕВО; нет смещения → ЦЕНТР. Вертикаль — как в JUI (ty=t+offy+vy); узел за рамкой окна — по низу контрола (`J.label_slot`).
    # ★★★ УЗЕЛ ПОДПИСИ СОЗДАЁТСЯ И ПРИ ПУСТОМ `CaptionText` (правка 31.08.2026, долг импортёра №1).
    #   БЫЛО: ветка входила только при непустой подписи, поэтому у контрола, которому строку кладёт
    #   КОД, узла `{idc}_Text` не появлялось вовсе. Живой случай: строки списка приятелей
    #   `CMTY_FRND_ONNAME1..7` (окно 14500 `COMMUNITY_WIN`, скин `Box_YellowOt20`,
    #   `CaptionText = ""`, `CaptionInfo = "0 0 ffe9d6bc 4"`) — имя приятеля не выводилось НИ В ОДНОЙ
    #   строке, и подсистеме `AsdaCommunityFriendSubsystem` пришлось достраивать надпись в рантайме
    #   (`FindLabel`/`EnsureLabel`). Пустая подпись — ЗАКОННОЕ состояние: в оригинале текст ставится
    #   через `CControl_SetCaption`, а слот подписи у контрола есть всегда.
    #   ★ КАКИЕ КОНТРОЛЫ ПОЛУЧАЮТ ПУСТОЙ УЗЕЛ — «СЛОТ ПОДПИСИ НАСТРОЕН» (обоснование выбора):
    #     контрол имеет `ID` (иначе код не может к нему обратиться и текст поставить некому)
    #     И его `CaptionInfo` несёт НЕнулевую маску выравнивания ЛИБО ненулевое смещение.
    #   ОСНОВАНИЕ (сверка по всем 4 ini): у контролов с ФАКТИЧЕСКОЙ подписью это выполняется в
    #     972 случаях из 972 (gamemain 881, login 87, temp 3, commonwin 1) — то есть «настроенный
    #     слот» является наблюдаемым признаком используемой подписи, а `CaptionInfo = "0 0 <цвет> 0"`
    #     — признак «слот не заполняли». Без этого отбора пустой узел получили бы 6 019 контролов
    #     (одних только фонов-`StaticWindow` 4 337), с отбором — 245 (gamemain 217, login 27,
    #     temp 1), в т.ч. все 7 строк `CMTY_FRND_ONNAME`.
    #   ⚠ Text-контролы (`stype == "Text"`) сюда не попадают: у них узел создаётся ветвью выше
    #     (`text_done`) и ИМЕНУЕТСЯ `{idc}` — правило имён не меняем.
    #   Параметры (шрифт FONT_PX, цвет `col`, выравнивание `halign`, позиция `tx/ty`, перенос по
    #     высоте) берутся ТЕМИ ЖЕ, что и для непустой подписи — общий код ниже.
    cap_slot = bool(node.idc) and w > 0 and h > 0 \
        and (getattr(node, "cap_align", 0) != 0 or tuple(getattr(node, "cap_off", (0, 0))) != (0, 0))
    # ★★★ 25.09.2026: пустой слот, чьё имя `{idc}_Text` — `ID` НАСТОЯЩЕГО контрола окна, а у самого контрола
    #   нет `IDNum` (подпись ему эталон записать не может), узла НЕ получает — иначе он забирает имя, а живой
    #   контрол уезжает в `…_Text_1` (окно 50300, строка 50343). Правило — `J.caption_slot_yields`.
    if cap_slot and id(node) in _CAPYIELD:
        cap_slot = False
    if (cap.strip() or cap_slot) and not text_done:
        # ★ Подпись кнопки/окна НА СЫРОМ rect из JUI (cap_off + cap_align), как оригинал — без наших эвристик.
        #   Подписи кнопок однострочные (h<28) → bWrap=False (autowrap-закон, ui_charselect_fixes.md §2).
        # ★ ИМЯ. У контрола с `idc` — `{idc}_Text`. У контрола БЕЗ `idc` обычное имя `base_txt`
        #   содержит порядковый индекс обхода (`NoImage_57_txt`) — оно нечитаемо и сдвигается от
        #   любой правки данных. Для класса S1a (текст только в `EXS TextContent`) имя берётся из
        #   ОБЩЕГО правила `J.missing_text_nodes` — `{ЯКОРЬ-ID-предка}_TXT{NN}`. То же правило
        #   применяет шаг `Tools/asda_ui_missing_text_nodes.py`, который достраивает эти узлы в
        #   УЖЕ СОБРАННЫЕ ассеты; совпадение имён и делает перегенерацию с ним согласованной
        #   (после перегенерации шаг видит узел на месте и ничего не дублирует).
        tname = (uniq_name(san(node.idc) + "_Text") if node.idc
                 else (_TXTNAME.get(id(node)) or (base + "_txt")))
        try:
            tbw = B.add_text(bp, canvas, tname, V2(tx, ty), V2(tw, th), cap, FONT_PX, col, halign,
                             h >= 28)
            made.append(tbw)
            # ★★★ Обводка не ставится и здесь — по той же причине (см. выше):
            #   правило «TextOutLine ⇒ обводка» отменено, стиль расставляет `asda_ui_text_style926.py`.
        except Exception: pass
    # ★ ПРАВИЛО: подписи/метки НЕ перехватывают мышь. Текст рисуется ПОВЕРХ кнопки (добавлен позже = выше по z);
    #   при hit-testable TextBlock мышь до кнопки не доходит → НЕТ hover/press/click. Все TextBlock → SelfHitTestInvisible
    #   (рендерятся, но клики/наведение проходят сквозь к кнопке под ними). Иначе у styled-кнопок нет визуальных состояний.
    for wgt in made:
        if isinstance(wgt, unreal.TextBlock):
            try: wgt.set_editor_property("visibility", unreal.SlateVisibility.SELF_HIT_TEST_INVISIBLE)
            except Exception: pass
    # ═══ ★★★ ЗОНЫ ХИТ-ТЕСТА И ЗАХВАТА (данные .JUI, см. блок «КОНТЕКСТ ОКНА» выше) ═══
    if _CTX is not None and w > 0 and h > 0:
        ev = _is_drag_handle(node)
        # У контрола есть план (значит, в оригинале он ЛОВИТ клик), но текстурного плана нет →
        #   виджета в WBP не было бы вовсе. Ставим невидимую зону ровно по rect контрола.
        has_plane = bool(sk and sk["planes"])
        needs_hit = has_plane and node.noselect == 0 and not planes
        if ev is not None or needs_hit:
            zname = base + ("_DRAG" if ev is not None else "_HIT")
            try:
                z = B.add_hit_zone(bp, canvas, zname, V2(l, t), V2(w, h))
                if z:
                    made.append(z)
                    if ev is not None:
                        _CTX["handles"].append({"widget": z.get_name(), "node": node, "ev": ev})
            except Exception as e:
                unreal.log_warning("hit-зона %s: %s" % (zname, e))
    made = [x for x in made if x]
    if _CTX is not None:
        names = [x.get_name() for x in made]
        # NoSelect=1 → все виджеты контрола обязаны пропускать клик НАСКВОЗЬ (реестр для рантайма).
        if node.noselect == 1:
            _CTX["nohit"].extend(names)
        # Контрол — ЦЕЛЬ чьей-то ручки и БЕЗ детей → двигать надо именно эти виджеты
        #   (у цели с детьми двигается её контейнер-группа, имя пишется в build_tree).
        if node.idnum >= 0 and node.idnum in _CTX["targets"] and not node.children:
            _CTX["target_widgets"].setdefault(node.idnum, []).extend(names)
    return made

def _apply_anchor(wgt, node):
    # ★ ПЕР-КОНТРОЛЬНЫЙ анкор (jui_align_layout.md): базовый rect виджета лежит в слоте (top-left, offsets=Pos+Size,
    #   поставлен ApplyResponsiveSlot). Ставим долю-экрана анкор из node.anchor{xspan,fx,yspan,fy}:
    #   - НЕ-span (фикс px): point-анкор в fx (центр%→a/100, правый→1, abs→0), offL=wl−fx·1024, size=ww → держит px-размер.
    #   - span (тянется): анкор [wl/1024 .. (wl+ww)/1024], offsets 0 → растягивается с экраном (фон/бар/%-размер).
    #   X зависит от W (база 1024), Y от H (база 768). DPI=1.0 → offsets в пикселях базы.
    # ★★★ ALIGNMENT (правка 26.09.2026, ревизия `craft_owner_items_REVIEW.md` СРЕДН-2). Раньше здесь ВСЕГДА
    #   ставилось `Alignment = (0,0)`. У переносимого текста окон ремесла (`asda_ui_text_block_align.wrap_block_slot`:
    #   точка привязки + `Alignment` 0.5/1 + авторазмер) это превращало «центр блока в точке» в «левый-верх в
    #   точке» — текст уезжал на полразмера вправо-вниз (77532 `ITEM_making_M_text`, `mark_d_1/2`). Теперь
    #   `Alignment` СОХРАНЯЕТСЯ ровно у узлов, которые пометила Text-ветка `add_control` (`_CTX["keep_align"]`,
    #   только окна ремесла — закрытый список); ВСЕМ ПРОЧИМ узлам — прежний сброс в (0,0) без изменений.
    #   Расчёт вынесен в чистую `_anchor_math` — ею же сверяет сухой прогон
    #   (`asda_ui_text_block_align.py`, `ASDA_TEXTBLOCK_PARITY=1`).
    #   ⚠ C++ `UAsdaUmgBuilder::ApplyGroupAnchor` здесь НЕ участвует (вызовов у него нет; Python-аналог
    #   `apply_group_anchor` — в `asda_jui_to_umg.py`, сцена выбора персонажа).
    a = getattr(node, "anchor", None)
    if not a: return
    try: slot = wgt.slot
    except Exception: return
    if not isinstance(slot, unreal.CanvasPanelSlot): return
    off = slot.get_offsets()   # Margin: left=posX, top=posY, right=width, bottom=height
    keep = _CTX is not None and wgt.get_name() in _CTX.get("keep_align", ())
    align = (0.0, 0.0)
    if keep:
        try:
            _al = slot.get_alignment()
            align = (float(_al.x), float(_al.y))
        except Exception: pass
    (axmin, aymin, axmax, aymax), (offL, offT, offR, offB), (alx, aly) = \
        _anchor_math((off.left, off.top, off.right, off.bottom), a, align)
    try:
        slot.set_anchors(unreal.Anchors(unreal.Vector2D(axmin, aymin), unreal.Vector2D(axmax, aymax)))
        slot.set_offsets(unreal.Margin(offL, offT, offR, offB))
        slot.set_alignment(unreal.Vector2D(alx, aly))
    except Exception: pass


def _anchor_math(off, a, align=(0.0, 0.0)):
    """Чистый расчёт `_apply_anchor`: off = (posX, posY, width, height) слота, a = node.anchor, align — итоговый
    `Alignment` ((0,0) у всех, кроме `keep_align`). Возврат ((minX, minY, maxX, maxY), (L, T, R, B), align)."""
    wl, wt, ww, wh = off
    if a["xspan"]:
        axmin, axmax, offL, offR = wl / 1024.0, (wl + ww) / 1024.0, 0.0, 0.0
    else:
        fx = a["fx"]; axmin = axmax = fx; offL = wl - fx * 1024.0; offR = ww
    if a["yspan"]:
        aymin, aymax, offT, offB = wt / 768.0, (wt + wh) / 768.0, 0.0, 0.0
    else:
        fy = a["fy"]; aymin = aymax = fy; offT = wt - fy * 768.0; offB = wh
    return (axmin, aymin, axmax, aymax), (offL, offT, offR, offB), (float(align[0]), float(align[1]))

def _align_scope(root):
    """Окно `root` в закрытом списке правил текста (д)/(б) (окна ремесла)? Сбой импорта шага — False (прежний вид)."""
    try:
        import asda_ui_text_block_align as TBA
        return bool(TBA.importer_scope(root))
    except Exception as e:                                        # noqa: BLE001
        unreal.log_warning("[jui] охват правил текста не определён (%s) — окно без правил (д)/(б)" % e)
        return False


def _is_scroll_viewport(n):
    """Узел — зона прокрутки (`WheelWindow`): группа = СВОЙ прямоугольник + обрезка по нему (см. `_subtree_bounds`)."""
    return (SKINS.get((n.name or "").lower()) or {}).get("type", "") == "WheelWindow"


def _subtree_bounds(n):
    # Габариты узла + ВСЕХ потомков (база 1024×768). Контейнер-группа берёт эти границы, чтобы дети, выходящие
    #   за rect узла (в JUI встречается: в *_BASE вложены соседние кнопки), НЕ выпадали за CanvasPanel и ловили клик.
    l, t, w, h = n.abs
    minx, miny, maxx, maxy = l, t, l + w, t + h
    # ★★★ 19.09.2026: ЗОНА ПРОКРУТКИ (`WheelWindow`, напр. `WheelWin_Nomal` 69649 страницы клавиш окна
    #   настроек) — это ОКНО ПРОСМОТРА, а не контейнер: её дети — прокручиваемое содержимое, которое
    #   ДОЛЖНО выходить за её прямоугольник и обрезаться им. Прежде группа брала union потомков
    #   (484×346 превращалось в 486×655), обрезки не было, и список клавиш вылезал за рамку окна вниз,
    #   а при прокрутке — вверх; заодно раздувался и контейнер вкладки (681×450 → 681×659).
    if _is_scroll_viewport(n):
        return minx, miny, maxx, maxy
    for c in n.children:
        cl, ct, cx, cy = _subtree_bounds(c)
        minx = min(minx, cl); miny = min(miny, ct); maxx = max(maxx, cx); maxy = max(maxy, cy)
    return minx, miny, maxx, maxy

_LAYOUT = None
def _layout_map():
    """имя WBP → СПИСОК папок, в которых ассет с таким именем уже лежит.

    ★ Раньше это был dict «имя → папка» (last-wins). При одинаковом имени в двух папках
    (`CommonWin/WBP_COMMONWIN_MSGBOX` и `Player_UI/Common/WBP_COMMONWIN_MSGBOX`) сборки ДВУХ
    РАЗНЫХ классов .JUI писались в одну и ту же папку, затирая друг друга. Теперь хранится
    список, а выбор делает `_layout_pkg` с учётом класса-владельца.
    """
    global _LAYOUT
    if _LAYOUT is None:
        _LAYOUT = {}
        for a in EAL.list_assets("/Game/AsdaAssets/UI", recursive=True, include_folder=False):
            p = a.split(".")[0]
            _LAYOUT.setdefault(p.rsplit("/", 1)[-1], []).append(p.rsplit("/", 1)[0])
    return _LAYOUT


def _login_blocked(path):
    """★ 15.09.2026 (Reports/dedup_audit/tex_rebind_2026-09-15.md, раздел 13): True — пакет `path` принадлежит сцене логина (asda_login_guard: UI/Login, кроме
    LOGIN_SHARED_EXCEPTIONS, и др.), а ASDA_ALLOW_LOGIN_WRITE != 1: пакет пропустить, строка [LOGIN-GUARD] уже в логе."""
    import sys as _sys_lg
    _here = HERE   # Tools — для asda_login_guard
    if _here not in _sys_lg.path:
        _sys_lg.path.insert(0, _here)
    from asda_login_guard import package_blocked_for   # ★ дедуп №2 Т18: имя инструмента — из __file__
    return package_blocked_for(__file__, path)


# Домашние папки классов .JUI (куда генератор кладёт окна, если их не разложили тематически).
_CLASS_HOME = {c: "/Game/AsdaAssets/UI/%s" % f for c, f in
               {"gamemain": "GameMain", "commonwin": "CommonWin",
                "temp": "Temp", "login": "Login"}.items()}


def _layout_pkg(wname, default_pkg):
    """Папка, в которой ПЕРЕСОБРАТЬ окно `wname` (класс задаёт `default_pkg`).

    Правило: (1) если ассет уже лежит в домашней папке этого класса — там; (2) иначе среди
    существующих мест берём то, что НЕ является домашней папкой ЧУЖОГО класса (так gamemain-окно,
    разложенное в `Player_UI/...`, не уезжает в `CommonWin/`); (3) иначе — домашняя папка класса.
    """
    places = _layout_map().get(wname) or []
    if not places:
        return default_pkg
    if default_pkg in places:
        return default_pkg
    foreign = set(_CLASS_HOME.values()) - {default_pkg}
    own = [p for p in places if p not in foreign]
    return own[0] if own else default_pkg


# ★★★ ОКНА, ОБЪЯВЛЕННЫЕ СРАЗУ В НЕСКОЛЬКИХ КЛАССАХ .JUI (`COMMONWIN_MSGBOX` 8000 в gamemain+commonwin,
#   `Job_Guide_Win` 31000 в gamemain+temp). В оригинале это РАЗНЫЕ менеджеры окон
#   (`g_pWindowMgr` / `g_pGameWindowMgr`, ветка `g_GameState != 1` в game_main.c), т.е. два РАЗНЫХ
#   окна с одинаковым именем. В порту они схлопывались в один ассет `WBP_<ID>` (побеждала последняя
#   сборка) — из-за этого ручек в мете было 142 вместо 144. Теперь неглавная сборка получает суффикс
#   класса: `WBP_COMMONWIN_MSGBOX_CommonWin`, `WBP_Job_Guide_Win_Temp` (и такой же `WindowId` в мете).
_COLLIDE = {}          # san(имя окна) → список классов, в которых оно объявлено
_PRIORITY = ["gamemain", "commonwin", "temp", "login"]   # чьё имя остаётся без суффикса


def _wsuffix(base, cls):
    """Суффикс имени ассета/`WindowId` для окна `base` класса `cls` («» — главная сборка)."""
    owners = _COLLIDE.get(base)
    if not owners or len(owners) < 2:
        return ""
    main_cls = sorted(owners, key=lambda c: _PRIORITY.index(c) if c in _PRIORITY else 99)[0]
    return "" if cls == main_cls else "_" + CLASSES[cls]


def _walk_nodes(n):
    yield n
    for c in n.children:
        yield from _walk_nodes(c)


# ═══════════ ★★★ ВАРИАНТЫ ОКОН — производные ассеты из ТОГО ЖЕ плана .JUI (2026-09-03) ═══════════
# ЗАЧЕМ. Базовое окно (напр. `WBP_MESSAGE2_WIN`) делят ~40 подтверждений и удаление персонажа.
#   Когда ОДНОМУ сценарию нужен другой ВИД того же окна (не другая разметка), базовый ассет
#   трогать нельзя (закон [[no-ui-regeneration-without-permission]]), а подменять вид кодом при
#   показе — тоже (закон [[no-runtime-widget-fixes]]). Выход — ОТДЕЛЬНЫЙ ассет, собранный ТЕМ ЖЕ
#   генератором из ТОГО ЖЕ дерева .JUI с точечными подменами В ДАННЫХ (скин узла, видимость,
#   подпись), а не копия ассета руками и не «новое» окно (закон [[no-invented-ui]]: скины —
#   только из JuiSkin.ini, узлы — только из плана). Имена узлов, геометрия и порядок детей у
#   варианта ровно те же, что у базового окна, поэтому C++-бинды по именам не меняются.
# КАК. `WINDOW_VARIANTS[(класс .JUI, ID окна)] = [(ID варианта, мутатор), …]`. Мутатор получает
#   ГЛУБОКУЮ КОПИЮ дерева окна (`copy.deepcopy`, база в том же прогоне не задевается) и меняет
#   ТОЛЬКО атрибуты узлов (`name` = скин, `visible`, `cap_*`). Ассет = `WBP_<ID варианта>` в папке
#   класса, `WindowId` меты = ID варианта. Сборка идёт СРАЗУ ПОСЛЕ базового окна (тот же прогон,
#   те же индексы класса для целей ручек). Фильтр `ASDA_ONLY_WIN`: имя БАЗОВОГО окна собирает базу
#   И её варианты (вариант не может отстать от базы); имя ВАРИАНТА собирает только вариант — база
#   не пересобирается (см. `build_window`: в наборе фильтра у варианта есть и `base`, и `win_id`).
def _node_by_idc(root, idc):
    """Узел дерева .JUI с данным `ID` (без учёта регистра) либо None."""
    key = (idc or "").upper()
    for n in _walk_nodes(root):
        if (n.idc or "").upper() == key:
            return n
    return None


def _variant_message2_neutral(root):
    """`MESSAGE2_WIN_NEUTRAL` — НЕЙТРАЛЬНАЯ модалка выбора из двух равных исходов.

    Решение владельца 2026-09-03 (модалка режима входа «Обычный» / «Душа» на выборе персонажа):
    у базового `MESSAGE2_WIN` кнопки стилизованы как «Да / Нет» — BUT01 красная с красным крестом
    и читается как «Отмена», плюс полоса таймера и значок-предупреждение. Для вопроса с двумя
    равноправными ответами нужен нейтральный вид. Отличий от базового окна ровно ТРИ, все — данные:
      1. `MESSAGE2_BUT01` (скин `Log_SysBtnRd71`: NE_skillbar2 UV 0.719…0.996 × 0.105…0.195,
         подпись FFE9D6BC/FFFFFFFF/FFCDCDCD) → скин и подпись `MESSAGE2_BUT00` (`Log_SysBtnWk72`:
         кадры SS0/SS1/SS2 NE_skillbar2 UV 0.715…0.996 × 0.004…0.094 (SS2 0.000…0.090), Diffuse
         F0F0F0/FFFFFF/C0C0C0, подпись FF323232/FF808080/FF000000, CaptionPoint SS2 = «0 1»).
         Значок `MESSAGE2_BUT01_ICON` (`Log_SysBtn_Icon2` = красный крест, NE_skillbar1 UV
         0.117…0.180 × 0.699…0.762) → значок `MESSAGE2_BUT00_ICON` (`Log_SysBtn_Icon1` = серый
         кружок, UV × 0.543…0.605). Размер BUT01 (71×23 против 72×23) и все позиции НЕ трогаем
         (закон [[jui-flattened-hierarchy-geometry]]) — рамки L/R/CBase у обеих кнопок и так одни.
      2. `MESSAGE2_WARNING` (`Log_Exclamation`, треугольник 39×39) → `Visible=0` ⇒ Collapsed.
      3. `MESSAGE2_GAUGE_BASE` (рамка `Log_White1Stroke_Ot30`) вместе с дочерней полосой
         `MESSAGE2_GAUGE` (`Log_WhiteBar`) → `Visible=0` ⇒ всё поддерево Collapsed (импортёр гасит
         КАЖДЫЙ виджет скрытого поддерева — [[jui-importer-node-names]] п.4). Гасим именно рамку:
         одна полоса без рамки оставила бы на окне пустой белый прямоугольник 230×6.
    Рантайм (`UAsdaWindowSubsystem::ShowConfirm`) находит те же имена `MESSAGE2_TXT00`,
    `MESSAGE2_BUT00/_Text`, `MESSAGE2_BUT01/_Text`; полосу `MESSAGE2_GAUGE*` он трогает только при
    `TimeoutSec > 0`, а нейтральный диалог показывается без таймера.
    """
    need = ("MESSAGE2_BUT00", "MESSAGE2_BUT01", "MESSAGE2_BUT00_ICON", "MESSAGE2_BUT01_ICON",
            "MESSAGE2_WARNING", "MESSAGE2_GAUGE_BASE")
    nodes = {k: _node_by_idc(root, k) for k in need}
    missing = [k for k in need if nodes[k] is None]
    if missing:
        raise RuntimeError("MESSAGE2_WIN_NEUTRAL: в плане .JUI нет узлов %s" % ", ".join(missing))
    but0, but1 = nodes["MESSAGE2_BUT00"], nodes["MESSAGE2_BUT01"]
    but1.name = but0.name                 # скин кнопки: кадры картинки И кадры подписи (SS0..SS2)
    but1.cap_color = but0.cap_color       # цвет подписи в покое — из `CaptionInfo` контрола, не скина
    but1.cap_off = but0.cap_off           # смещение подписи (у обеих «0 0» — на всякий случай тоже)
    but1.cap_align = but0.cap_align       # маска выравнивания (у обеих 5 = центр/центр)
    nodes["MESSAGE2_BUT01_ICON"].name = nodes["MESSAGE2_BUT00_ICON"].name   # крест → кружок
    nodes["MESSAGE2_WARNING"].visible = 0
    nodes["MESSAGE2_GAUGE_BASE"].visible = 0
    return root


# (класс .JUI, ID базового окна) → [(ID варианта = имя ассета без `WBP_`, мутатор копии дерева), …]
WINDOW_VARIANTS = {
    ("login", "MESSAGE2_WIN"): [("MESSAGE2_WIN_NEUTRAL", _variant_message2_neutral)],
}


def _index_nodes(n, r, p, byid, rootof, parentof):
    """Индексы узлов дерева: IDNum → узел (первый выигрывает), узел → корневое окно, узел → родитель."""
    rootof[id(n)] = r; parentof[id(n)] = p
    if n.idnum >= 0 and n.idnum not in byid: byid[n.idnum] = n
    for c in n.children: _index_nodes(c, r, n, byid, rootof, parentof)


def build_window(pkg, root, byid=None, rootof=None, parentof=None, cls="", variant_id=None):
    """byid/rootof/parentof — индексы ВСЕХ узлов класса .JUI (нужны для резолва цели ручки: цель
    может лежать в другом корневом окне, см. Gear_book_movetext → Inven_Strength_BOX3).
    variant_id — ID варианта окна (см. `WINDOW_VARIANTS`): имя ассета `WBP_<variant_id>` и
    `WindowId` меты берутся из него, а имена узлов остаются от базового окна."""
    global _CTX
    _wnames.clear()   # уникальность имён интерактив-виджетов — в пределах окна
    J.compute_anchors(root)   # node.abs (база 1024×768) + node.anchor{xspan,fx,yspan,fy} на каждый узел
    # ★ Пре-проход: собрать rect'ы всех чекбоксов окна (idc→(t,h)) — метка {idc}_TXT центрируется по БОКСУ.
    # ★ Пре-проход S1a: имена узлов-подписей для контролов с пустым `ID`, у которых текст лежит
    #   только в `EXS TextContent` (общее правило — см. `_TXTNAME` и `J.missing_text_nodes`).
    _TXTNAME.clear()
    for _n, _p, _nm in J.missing_text_nodes(root):
        _TXTNAME[id(_n)] = _nm
    # ★★★ Пре-проход S2: имена интерактивных узлов для контролов с пустым `ID` (кнопки/чекбоксы/
    #   радио/комбо). Общее правило — `J.missing_interactive_nodes`; отсеянные контролы в словарь
    #   НЕ попадают и потому остаются без кликабельного виджета, как и раньше.
    _IACTNAME.clear()
    _s2ok, _s2skip = J.missing_interactive_nodes(root, SKINS)
    for _n, _p, _nm, _st, _pl in _s2ok:
        _IACTNAME[id(_n)] = _nm
    for _n, _nm, _st, _why in _s2skip:
        unreal.log("[S2] %s: контрол %s (IDNum %d) БЕЗ кликабельного узла — %s"
                   % (san(root.idc or root.name), _n.name, _n.idnum, _why))
    # ★★★ Пре-проход «слот подписи уступает имя» (см. `_CAPYIELD`): у контрола без `IDNum` пустая подпись
    #   не создаётся, если её имя `{idc}_Text` — это `ID` настоящего контрола окна.
    _CAPYIELD.clear()
    for _n, _nm in J.caption_slot_yields(root, lambda n: (SKINS.get(skin_key_for(n, quiet=True)) or {}).get("type", "")):
        _CAPYIELD.add(id(_n))
        unreal.log("[подпись] %s: пустой слот подписи %s уступает имя настоящему контролу (IDNum нет)"
                   % (san(root.idc or root.name), _nm))
    _check_rects.clear()
    def _collect_checks(n):
        st = (SKINS.get((n.name or "").lower()) or {}).get("type", "")
        if n.idc and st == "CheckButton":
            _cl, _ct, _cw, _ch = n.abs
            _check_rects[n.idc.upper()] = (_ct, _ch)
        for c in n.children: _collect_checks(c)
    _collect_checks(root)
    base = san(root.idc or root.name)
    sfx = _wsuffix(base, cls)          # «» либо `_CommonWin` / `_Temp` у неглавной сборки (см. _COLLIDE)
    # ★ Вариант окна (WINDOW_VARIANTS): своё имя ассета и меты, узлы — с именами базового окна.
    win_id = variant_id or (base + sfx)
    wname = "WBP_" + win_id
    # ★ Фильтр ASDA_ONLY_WIN: регенерить ТОЛЬКО указанное окно (напр. CARAMAKE_WIN), не трогая остальные готовые.
    #   ★ 31.08.2026: допускается СПИСОК окон через запятую (`ASDA_ONLY_WIN=A,B`) — чтобы проверочный
    #     прогон на нескольких окнах не гонял тяжёлый пост-импорт по разу на окно. Одиночное имя
    #     работает ровно как прежде.
    #   ★ 2026-09-03: у ВАРИАНТА в наборе есть и `base` (имя базового окна), и `win_id` (имя
    #     варианта) — поэтому `ASDA_ONLY_WIN=MESSAGE2_WIN` пересобирает базу вместе с вариантами,
    #     а `ASDA_ONLY_WIN=MESSAGE2_WIN_NEUTRAL` — только вариант, базу не трогая.
    only = os.environ.get("ASDA_ONLY_WIN", "")
    onlyset = set(x.strip().lower() for x in only.split(",") if x.strip())
    if onlyset and not (onlyset & {base.lower(), win_id.lower(), wname.lower()}):
        return None, 0, None
    # ── ★★★ ПРЕ-ПРОХОД: ручки перетаскивания окна и их ЦЕЛИ (данные .JUI) ──
    #   Цель ручки резолвится по IDNum среди ВСЕХ узлов класса (у одной ручки цель лежит в другом
    #   корневом окне: Gear_book_movetext 80302 → Inven_Strength_BOX3 80200 в INVEN_WIN — это данные
    #   оригинала, не наша ошибка). Сломанные цели чиним по правилу «цель = родитель ручки» (см. ниже).
    byid = byid or {}
    rootof = rootof or {}
    parentof = parentof or {}
    _CTX = {"handles": [], "nohit": [], "targets": {}, "target_widgets": {},
            # ★★★ 26.09.2026 (ревизия СРЕДН-1/2): правила текста (д)/(б) — только окна ремесла (закрытый список
            #   `asda_ui_text_block_align.IMPORTER_SCOPE_WIN_IDS`); при сбое импорта модуля — прежнее поведение.
            "align_scope": _align_scope(root),
            # имена TextBlock с `Alignment` от `wrap_block_slot` — `_apply_anchor` его не сбрасывает
            "keep_align": set(),
            # ★ 07.10.2026: низ рамки окна (корневой контрол .JUI, база) — порог обрезки узла-подписи (`J.label_slot`)
            "root_bottom": float(root.abs[1] + root.abs[3])}
    for n in _walk_nodes(root):
        ev = _is_drag_handle(n)
        if ev is None:
            continue
        tgt = byid.get(ev["ctrl_id"])
        if tgt is None:
            # ★ БАГ ОРИГИНАЛА (5 ручек в gamemain + 1 в login): CtrlID = -1 либо несуществующий IDNum
            #   (System_3d_setupText_Move: CtrlID="5520" при реальном 55220 — потерянная цифра).
            #   ПОЧИНЕНО (решение юзера 2026-08-06) по доминирующему правилу данных: цель = РОДИТЕЛЬ
            #   ручки (95 из 138 рабочих ручек), а если родителя нет (ручка сама корень) — она сама.
            tgt = parentof.get(id(n)) or n
            ev = dict(ev); ev["_fixed"] = True
            ev["ctrl_id"] = tgt.idnum
        if tgt.idnum >= 0:
            _CTX["targets"][tgt.idnum] = tgt
        n_ev = ev
        # сам факт ручки фиксируется при создании её зоны в add_control; здесь только цели
        _CTX.setdefault("resolved", {})[id(n)] = (tgt, n_ev)
    # ★ РАСКЛАДКА Player_UI: если ассет с таким именем уже разложен по тематической подпапке
    #   (/Game/AsdaAssets/UI/Player_UI/<Группа>/), пересобираем ЕГО ТАМ ЖЕ — иначе повторный импорт
    #   создаст дубль в UI/GameMain и порвёт ссылки C++. Карта строится один раз (см. _layout_map).
    pkg = _layout_pkg(wname, pkg)
    # ★★★ ПРОБНЫЙ ИМПОРТ В ПЕСОЧНИЦУ (`ASDA_JUI_PKG_OVERRIDE`, 17.09.2026). Закон
    #   [[owner-asset-edits-never-lose]] требует ДОКАЗАТЬ, что переимпорт не теряет ручную правку
    #   владельца, а доказать это можно только СРАВНЕНИЕМ боевого ассета со свежесобранным. Собирать
    #   «свежий» поверх боевого — значит сперва его уничтожить, поэтому здесь есть законный способ
    #   отправить сборку в ОТДЕЛЬНУЮ папку (напр. `/Game/_JuiDiff`), не трогая боевые ассеты вовсе.
    #   Ключ НЕ участвует в штатном прогоне: пусто ⇒ поведение ровно прежнее.
    _ovr = os.environ.get("ASDA_JUI_PKG_OVERRIDE", "").rstrip("/")
    # ★★★ ГЕЙТ ЛОГИНА СУДИТ ИСХОДНЫЙ ПУТЬ (исправлено 17.09.2026 по находке ревизии).
    #   Раньше подмена пакета делалась ДО проверки, а `_login_blocked` матчит ПУТЬ: при песочнице
    #   `/Game/_JuiDiff/...` окно сцены логина переставало опознаваться, и защита молча отключалась.
    #   Теперь решение принимается по тому пути, куда окно село бы в ШТАТНОМ прогоне, поэтому ключ
    #   песочницы не может служить обходом охраны логина — ни в боевой пакет, ни в песочницу.
    _login_probe = "%s/%s" % (pkg, wname)
    if _login_blocked(_login_probe):
        return None, 0, None
    if _ovr:
        pkg = _ovr
    full = "%s/%s" % (pkg, wname)
    # ★ 15.09.2026 (Reports/dedup_audit/tex_rebind_2026-09-15.md, раздел 13): окно сцены логина (UI/Login, кроме LOGIN_SHARED_EXCEPTIONS) без
    #   ASDA_ALLOW_LOGIN_WRITE=1 не пересобираем — ни удаления, ни создания, ни текстур, ни сохранения; его запись в
    #   DA_JuiWindowMeta сохраняется слиянием в main() («окна, которых в этом прогоне не было»). Прочие окна — как раньше.
    if _login_blocked(full):
        return None, 0, None
    if EAL.does_asset_exist(full):      # пересборка: снести старый (CreateWidgetBP не перезаписывает в unattended)
        EAL.delete_asset(full)
    bp = B.create_widget_bp(pkg, wname)
    if not bp: return None, 0
    canvas = B.get_root_canvas(bp)
    cnt = [0]; i = [0]
    COLLAPSED = unreal.SlateVisibility.COLLAPSED
    def jlog(n, d=0):
        st = (SKINS.get((n.name or "").lower()) or {}).get("type", "")
        unreal.log("[JTREE] %s%s idc=%s stype=%s abs=%s kids=%d" % (
            "  "*d, n.name, n.idc, st, tuple(int(x) for x in n.abs), len(n.children)))
        for c in n.children: jlog(c, d+1)
    if os.environ.get("ASDA_JTREE"): jlog(root)
    def build_tree(n, parent_panel, ox, oy, is_top, hidden=False, is_root=False):
        # ★ ИЕРАРХИЯ JUI → UMG: узел С ДЕТЬМИ становится CanvasPanel-ГРУППОЙ (двигается целиком, дочерние
        #   доступны в дереве). Дети кладутся ВНУТРЬ группы в ЛОКАЛЬНЫХ координатах (origin = rect узла).
        #   Анкор (доля экрана, респонсив) ставим ТОЛЬКО верхнему уровню — вложенные фикс к своей группе,
        #   поэтому вся группа держит пиксельный размер/позицию как оригинал (jui_align_layout.md).
        # ★ Visible==0 (альт-состояния/попапы) НЕ выкидываем — Collapsed по умолчанию (редактируемы, видны в иерархии).
        #
        # ★★★ 17.09.2026 — ПОЧИНЕНО НАСЛЕДОВАНИЕ СКРЫТОСТИ (класс дефектов S6, разрешение владельца).
        #   БЫЛО:  eff_hidden = hidden or (not is_root and n.visible == 0)
        #          — скрытость родителя ПРОШИВАЛАСЬ вниз по всему поддереву, и собственный
        #          `Visible = "1"` каждого потомка терялся В АССЕТЕ НАВСЕГДА. Показать панель кодом
        #          становилось нечем: контейнер разворачивали, а внутри всё оставалось `Collapsed`
        #          (симптом «открылось ПУСТОЕ окно» — аукцион 13000, окно настроек 69500).
        #   СТАЛО: узел получает СВОЙ флаг из данных и только его.
        #   ПОЧЕМУ ЭТО ВЕРНО ПО ОРИГИНАЛУ. Клиент хранит у контрола ЕГО СОБСТВЕННЫЙ бит видимости,
        #   а при отрисовке проверяет ЦЕПОЧКУ предков (`CControl_IsVisibleChain`): невидимый предок
        #   прячет поддерево ВРЕМЕННО, флаг потомка при этом цел, и показ предка возвращает на экран
        #   ровно тех детей, у кого свой флаг = 1. В UMG цепочку даёт сам Slate: `Collapsed`-родитель
        #   не рисует и не хит-тестит детей независимо от их собственной видимости. То есть
        #   наследование НЕ НУЖНО — оно только разрушало данные.
        #   ПАРАМЕТР `hidden` ОСТАВЛЕН: его передаёт пост-шаг LOGIN_WIN (`build_tree(nb, ip_grp, ...)`)
        #   позиционно; вниз он больше НЕ ПРОТАСКИВАЕТСЯ — потомкам уходит False.
        #   КОРЕНЬ ОКНА (`is_root`) по-прежнему не гасим: «открыто ли окно» — это СОСТОЯНИЕ,
        #   им распоряжается код подсистемы, а не ассет.
        eff_hidden = (not is_root) and getattr(n, "visible", 1) == 0
        l, t, w, h = n.abs
        if is_root:
            # ★ Корневое окно НЕ группируем: его прямые дети раскиданы по всему экрану (кнопки по краям, поля в
            #   центре) и КАЖДЫЙ держит СВОЙ экранный анкор (респонсив). Группа образуется на вложенных поддеревьях.
            ws = add_control(bp, parent_panel, n, i[0], ox=0.0, oy=0.0); i[0] += 1
            for wgt in ws: _apply_anchor(wgt, n)
            if eff_hidden:
                for wgt in ws:
                    try: wgt.set_editor_property("visibility", COLLAPSED)
                    except Exception: pass
            cnt[0] += len(ws)
            # ★ 17.09.2026: вниз уходит False — скрытость НЕ наследуется (см. заголовок build_tree).
            for c in n.children: build_tree(c, parent_panel, 0.0, 0.0, True, False)
            return
        if n.children:
            gname = uniq_name(san(n.idc or n.name) + "_grp")
            # ★ Контейнер = UNION-габариты поддерева (не rect узла): иначе дети, торчащие за rect узла, выпадают
            #   за границу CanvasPanel и НЕ ловят клик (hit-test обрезается границей группы — симптом «кнопка не
            #   реагирует»). origin = левый-верх union; визуал узла и дети — локаль относительно union-origin.
            gx0, gy0, gx1, gy1 = _subtree_bounds(n)
            cont = B.add_canvas(bp, parent_panel, gname, V2(gx0 - ox, gy0 - oy), V2(max(gx1 - gx0, 1), max(gy1 - gy0, 1)))
            if is_top: _apply_anchor(cont, n)          # анкор по УЗЛУ (окно держит позицию/размер как оригинал)
            if _is_scroll_viewport(n):
                # ★ 19.09.2026: зона прокрутки обрезает своё содержимое (см. `_subtree_bounds`).
                try: cont.set_editor_property("clipping", unreal.WidgetClipping.CLIP_TO_BOUNDS)
                except Exception: pass
            # ★ Узел — ЦЕЛЬ ручки перетаскивания: двигать надо ЕГО КОНТЕЙНЕР (вместе со всем поддеревом).
            if _CTX is not None and n.idnum >= 0 and n.idnum in _CTX["targets"]:
                _CTX["target_widgets"].setdefault(n.idnum, []).append(cont.get_name())
            ws = add_control(bp, cont, n, i[0], ox=gx0, oy=gy0); i[0] += 1
            cnt[0] += len(ws)
            # ★ 17.09.2026: дети — внутрь группы, но БЕЗ унаследованной скрытости (вниз идёт False):
            #   свой `Visible` каждого потомка обязан доехать в ассет целым.
            for c in n.children: build_tree(c, cont, gx0, gy0, False, False)
            if eff_hidden:
                try: cont.set_editor_property("visibility", COLLAPSED)
                except Exception: pass
        else:
            ws = add_control(bp, parent_panel, n, i[0], ox=ox, oy=oy); i[0] += 1
            for wgt in ws:
                if is_top: _apply_anchor(wgt, n)       # лист верхнего уровня — экранный анкор; вложенный — фикс
            if eff_hidden:
                for wgt in ws:
                    try: wgt.set_editor_property("visibility", COLLAPSED)
                    except Exception: pass
            cnt[0] += len(ws)
    build_tree(root, canvas, 0.0, 0.0, True, is_root=True)
    # ★ ПОСТ-ШАГ LOGIN_WIN: бейк ручной фичи «server IP из ввода» (SERVER_IP_INPUT) — поле добавлялось в редакторе
    #   и СНОСИЛОСЬ каждой регенерацией (грабли 2026-07-23). Параметры сняты с ручного варианта юзера:
    #   группа SERVER_IP (316×72) в LOGIN_CONFORM_BASE_grp @ (0,-74.2) = рамка-копия поля LOGIN_EDIT00_BASE
    #   (9-slice+болты) + EditableText SERVER_IP_INPUT @ (43,39) 230×19 шрифт 8. C++ ищет ИМЯ SERVER_IP_INPUT.
    if wname == "WBP_LOGIN_WIN":
        def _find_widget(w, name):
            if w.get_name() == name: return w
            if isinstance(w, unreal.PanelWidget):
                for k in range(w.get_children_count()):
                    r = _find_widget(w.get_child_at(k), name)
                    if r: return r
            return None
        def _find_node(n, idc):
            if (n.idc or "").upper() == idc: return n
            for c in n.children:
                r = _find_node(c, idc)
                if r: return r
            return None
        host = _find_widget(canvas, "LOGIN_CONFORM_BASE_grp") or canvas
        ip_grp = B.add_canvas(bp, host, "SERVER_IP", V2(0.0, -74.2), V2(316.0, 72.0))
        nb = _find_node(root, "LOGIN_EDIT00_BASE")
        if nb is not None:   # рамка = точная копия поддерева BASE поля ID (планы/UV/капы/болты)
            bx0, by0, _bx1, _by1 = _subtree_bounds(nb)
            build_tree(nb, ip_grp, bx0, by0, False)
        else:
            unreal.log_warning("[LOGIN_WIN] узел LOGIN_EDIT00_BASE не найден — SERVER_IP без рамки")
        B.add_editable_text(bp, ip_grp, "SERVER_IP_INPUT", V2(43.0, 39.0), V2(230.0, 19.0),
                            "", 8, unreal.LinearColor(1, 1, 1, 1), False)
        cnt[0] += 2
    B.compile_and_save(bp); EAL.save_loaded_asset(bp)
    # ── ★★★ МЕТА ОКНА (ручки перетаскивания + контролы NoSelect) → DA_JuiWindowMeta ──
    meta = unreal.AsdaJuiWindowMeta()
    meta.set_editor_property("window_id", win_id)     # с суффиксом класса у неглавной сборки
    meta.set_editor_property("id_num", int(root.idnum))
    meta.set_editor_property("base_size", V2(root.abs[2], root.abs[3]))
    meta.set_editor_property("always_top", bool(getattr(root, "alwaystop", 0)))
    hs = unreal.Array(unreal.AsdaJuiDragHandle)
    for hrec in _CTX["handles"]:
        node = hrec["node"]
        tgt, ev = _CTX.get("resolved", {}).get(id(node), (None, hrec["ev"]))
        if tgt is None:
            continue
        dh = unreal.AsdaJuiDragHandle()
        dh.set_editor_property("handle_widget", hrec["widget"])
        dh.set_editor_property("target_id_num", int(tgt.idnum))
        dh.set_editor_property("target_control", san(tgt.idc or tgt.name))
        is_root_tgt = (tgt is root)
        dh.set_editor_property("target_is_window_root", is_root_tgt)
        tw_arr = unreal.Array(unreal.Name)
        if not is_root_tgt:
            for nm in _CTX["target_widgets"].get(tgt.idnum, []):
                tw_arr.append(unreal.Name(nm))
        dh.set_editor_property("target_widgets", tw_arr)
        # ЦЕЛЬ В ДРУГОМ КОРНЕВОМ ОКНЕ — данные оригинала (Gear_book_movetext → INVEN_WIN), не наша ошибка.
        troot = rootof.get(id(tgt), root)
        tbase = san(troot.idc or troot.name)
        dh.set_editor_property("target_window_id", tbase + _wsuffix(tbase, cls))
        dh.set_editor_property("original_bug_fixed", bool(ev.get("_fixed")))
        hs.append(dh)
    meta.set_editor_property("drag_handles", hs)
    nh = unreal.Array(unreal.Name)
    for nm in _CTX["nohit"]:
        nh.append(unreal.Name(nm))
    meta.set_editor_property("no_hit_widgets", nh)
    return wname, cnt[0], meta

META_PKG = "/Game/AsdaAssets/UI"
META_NAME = "DA_JuiWindowMeta"

def _ini_of(cls):
    """Путь .ini класса: приоритет ЛОКАЛИЗОВАННОГО (RU/EN), иначе базовый (корейский)."""
    p = os.path.join(INI_DIR, "%s_loc.ini" % cls)
    return p if os.path.isfile(p) else os.path.join(INI_DIR, "%s_jui.ini" % cls)


def _scan_collisions():
    """Заполнить `_COLLIDE`: какие имена окон объявлены в НЕСКОЛЬКИХ классах .JUI.

    Сканируются ВСЕ ЧЕТЫРЕ класса независимо от `sel` — иначе прогон одного класса не знал бы,
    что имя занято другим, и суффикс то появлялся бы, то исчезал.
    """
    _COLLIDE.clear()
    for cls in CLASSES:
        ini = _ini_of(cls)
        if not os.path.isfile(ini):
            continue
        try:
            for r in J.parse_jui(ini):
                _COLLIDE.setdefault(san(r.idc or r.name), []).append(cls)
        except Exception as e:
            unreal.log_warning("[коллизии] %s: %s" % (ini, e))
    dupes = {k: v for k, v in _COLLIDE.items() if len(v) > 1}
    for k, v in sorted(dupes.items()):
        keep = sorted(v, key=lambda c: _PRIORITY.index(c) if c in _PRIORITY else 99)[0]
        unreal.log("[коллизии] окно %s объявлено в классах %s → главный %s, остальные с суффиксом: %s"
                   % (k, "+".join(v), keep,
                      ", ".join("WBP_%s%s" % (k, _wsuffix(k, c)) for c in v if c != keep)))
    return dupes


def _drop_stale_collision_assets(dupes):
    """Снести ассеты неглавных сборок, лежащие под СТАРЫМ (безсуффиксным) именем.

    Иначе после переименования в папке класса остался бы осиротевший `WBP_<ID>` рядом с новым
    `WBP_<ID>_<Класс>`, и `_layout_pkg` мог бы снова увести главную сборку в чужую папку.
    """
    global _LAYOUT
    killed = 0
    for base, owners in dupes.items():
        for cls in owners:
            if not _wsuffix(base, cls):
                continue                                  # это главная сборка — её имя не меняется
            stale = "%s/WBP_%s" % (_CLASS_HOME[cls], base)
            if _login_blocked(stale):   # ★ 15.09.2026: устаревший ассет в UI/Login — сносить только с разрешением
                continue
            if EAL.does_asset_exist(stale):
                EAL.delete_asset(stale)
                killed += 1
                unreal.log("[коллизии] удалён устаревший ассет %s (пересоберётся как WBP_%s%s)"
                           % (stale, base, _wsuffix(base, cls)))
    if killed:
        _LAYOUT = None                                     # карта раскладки устарела
    return killed


def main():
    tw = 0; tc = 0
    metas = []
    only = os.environ.get("ASDA_ONLY_WIN", "")
    # ★★★ ПЕСОЧНИЦА (`ASDA_JUI_PKG_OVERRIDE`): сборка уходит в отдельную папку и НИ ОДИН боевой
    #   ассет не должен пострадать. Значит выключаем всё, что пишет мимо этой папки: снос
    #   устаревших ассетов коллизий, перезапись реестра `DA_JuiWindowMeta` и пост-импорт (его шаги
    #   адресуют боевые пути по именам и в песочнице просто не нужны).
    sandbox = bool(os.environ.get("ASDA_JUI_PKG_OVERRIDE", "").strip())
    dupes = _scan_collisions()
    if not sandbox:
        _drop_stale_collision_assets(dupes)
    for cls in sel:
        folder = CLASSES[cls]
        # ★ приоритет ЛОКАЛИЗОВАННОГО JUI (LanguageUpdate: RU/EN), иначе базовый (корейский)
        ini = _ini_of(cls)
        if not os.path.isfile(ini): unreal.log_warning("нет INI %s" % ini); continue
        roots = J.parse_jui(ini); pkg = "/Game/AsdaAssets/UI/%s" % folder
        if not EAL.does_directory_exist(pkg): EAL.make_directory(pkg)
        # ★ Индексы ПО ВСЕМУ КЛАССУ: цель ручки ищется по IDNum глобально (см. build_window).
        byid = {}; rootof = {}; parentof = {}
        for r in roots: _index_nodes(r, r, None, byid, rootof, parentof)
        unreal.log("=== %s: топ-окон=%d → %s ===" % (cls, len(roots), pkg))
        for r in roots:
            try:
                wn, cc, mt = build_window(pkg, r, byid, rootof, parentof, cls)
                if wn: tw += 1; tc += cc
                if mt: metas.append(mt)
            except Exception as e: unreal.log_warning("окно %s: %s" % (getattr(r, "idc", "?"), e))
            # ★★★ ВАРИАНТЫ окна (см. WINDOW_VARIANTS): глубокая копия ТОГО ЖЕ дерева + мутатор данных,
            #   затем штатный build_window. Индексы копии — свои (id() узлов копии другие), поверх
            #   них — индексы класса, чтобы цель ручки в ЧУЖОМ окне по-прежнему резолвилась.
            for vid, mut in WINDOW_VARIANTS.get((cls, san(r.idc or r.name)), []):
                try:
                    vr = mut(copy.deepcopy(r))
                    vbyid = {}; vrootof = {}; vparentof = {}
                    _index_nodes(vr, vr, None, vbyid, vrootof, vparentof)
                    for k, v in byid.items(): vbyid.setdefault(k, v)
                    for k, v in rootof.items(): vrootof.setdefault(k, v)
                    for k, v in parentof.items(): vparentof.setdefault(k, v)
                    wn, cc, mt = build_window(pkg, vr, vbyid, vrootof, vparentof, cls, variant_id=vid)
                    if wn:
                        tw += 1; tc += cc
                        unreal.log("[вариант] %s ← %s: контролов %d" % (wn, san(r.idc or r.name), cc))
                    if mt: metas.append(mt)
                except Exception as e:
                    # Ошибка — не предупреждение: вариант заказан явно, молча потерять его нельзя.
                    unreal.log_error("[вариант] %s (от %s): НЕ СОБРАН — %s" % (vid, getattr(r, "idc", "?"), e))
        unreal.log("  %s готово" % cls)
    # ── Сохранение реестра меты. При ASDA_ONLY_WIN (регенерация одного окна) реестр НЕ трогаем:
    #    иначе он схлопнется до одного окна и все остальные потеряют ручки/NoSelect.
    if sandbox:
        unreal.log("[мета] ПЕСОЧНИЦА ASDA_JUI_PKG_OVERRIDE — реестр DA_JuiWindowMeta не перезаписан")
    elif only:
        unreal.log("[мета] ASDA_ONLY_WIN=%s — реестр DA_JuiWindowMeta не перезаписан" % only)
    elif metas:
        # ★★★ КОЛЛИЗИЯ ИМЁН МЕЖДУ КЛАССАМИ .JUI — РАЗВЕДЕНА (2026-08-06). Два окна объявлены ДВАЖДЫ:
        #   `COMMONWIN_MSGBOX` (8000) — gamemain (290×170, ручка есть) + commonwin (280×114, без ручки);
        #   `Job_Guide_Win`   (31000) — gamemain (ручка есть) + temp (без ручки).
        #   В оригинале это РАЗНЫЕ менеджеры окон (`g_pWindowMgr` / `g_pGameWindowMgr`, ветка
        #   `g_GameState != 1` в game_main.c), т.е. ДВА РАЗНЫХ окна. Раньше обе сборки писались в
        #   ОДИН ассет `WBP_<ID>` (побеждала последняя) и одна из них теряла ручку — отсюда 142
        #   ручки вместо 144. Теперь неглавная сборка получает суффикс класса (`_wsuffix`), поэтому
        #   `window_id` уникален и dedup ниже — только страховка от неожиданного дубля.
        dedup = {}
        for m in metas:
            dedup[str(m.get_editor_property("window_id"))] = m     # last-wins, как и сам ассет
        if len(dedup) != len(metas):
            unreal.log_warning("[мета] НЕОЖИДАННЫЙ дубль window_id, оставлена последняя сборка: %s"
                               % ", ".join(sorted(k for k in dedup
                                                  if sum(1 for m in metas
                                                         if str(m.get_editor_property("window_id")) == k) > 1)))
        metas = list(dedup.values())
        asset = B.create_window_meta_asset(META_PKG, META_NAME)
        if asset:
            fresh = set(str(m.get_editor_property("window_id")) for m in metas)
            arr = unreal.Array(unreal.AsdaJuiWindowMeta)
            # ★ СЛИЯНИЕ: записи окон, которых в этом прогоне не было (напр. класс login при прогоне
            #   gamemain), сохраняем как есть — иначе один прогон стирал бы мету остальных классов.
            kept = 0
            for old in asset.get_editor_property("windows"):
                if str(old.get_editor_property("window_id")) not in fresh:
                    arr.append(old); kept += 1
            for m in metas: arr.append(m)
            asset.set_editor_property("windows", arr)
            unreal.log("[мета] обновлено окон: %d, сохранено прежних: %d" % (len(metas), kept))
            EAL.save_loaded_asset(asset)
            nh_total = sum(len(m.get_editor_property("no_hit_widgets")) for m in metas)
            h_total = sum(len(m.get_editor_property("drag_handles")) for m in metas)
            w_with = sum(1 for m in metas if len(m.get_editor_property("drag_handles")) > 0)
            unreal.log("=== МЕТА JUI: окон=%d, ручек=%d, окон С ручкой=%d, окон БЕЗ=%d, NoSelect-виджетов=%d ==="
                       % (len(metas), h_total, w_with, len(metas) - w_with, nh_total))
        else:
            unreal.log_warning("[мета] не удалось создать %s/%s" % (META_PKG, META_NAME))
    unreal.log("=== ГЕНЕРИК UMG v2: окон=%d контролов=%d ===" % (tw, tc))
    # ★★★ ОБЯЗАТЕЛЬНЫЙ ПОСТ-ИМПОРТ (регресс 2026-08-06). Перегенерация собирает окно С НУЛЯ и стирает
    #   всё, что доложили в ассет дельта-скрипты. Так исчезли поле карты/маркеры/клик-зоны у окон
    #   1702 и 1710, и мини-карта с картой мира перестали работать ЦЕЛИКОМ. Раннер возвращает их
    #   сразу же, в том же прогоне — забыть его теперь нельзя.
    #   ★★★ И ОН ЖЕ ВОЗВРАЩАЕТ РУЧНЫЕ ПРАВКИ ЮЗЕРА (2026-08-06, второй регресс): не только
    #   дорисованные узлы `RESIZE/SCROLL_*`, но и СДВИНУТЫЕ им узлы из .JUI (заголовок «Система»
    #   окна 2570). Если хоть одна правка не вернулась — `pi.main()` бросает исключение, и оно
    #   ПРОБРАСЫВАЕТСЯ НАРУЖУ: прогон обязан упасть, а не отрапортовать «готово» с потерянными
    #   правками. Молчаливое проглатывание этой ошибки дважды стоило пользователю его работы.
    if sandbox:
        # Песочница: пост-импорт правит БОЕВЫЕ ассеты по именам путей и к сборке в чужой папке
        # отношения не имеет. Пропускаем осознанно и громко — чтобы прогон в песочнице нельзя
        # было спутать со штатным.
        unreal.log("[пост-импорт] ПРОПУЩЕН: прогон в песочнице ASDA_JUI_PKG_OVERRIDE=%s"
                   % os.environ.get("ASDA_JUI_PKG_OVERRIDE"))
        unreal.log("=== ГЕНЕРИК UMG v2 (ПЕСОЧНИЦА): готово ===")
        return
    try:
        spec2 = importlib.util.spec_from_file_location("asda_ui_post_import",
                                                       os.path.join(HERE, "asda_ui_post_import.py"))
        pi = importlib.util.module_from_spec(spec2); sys.modules["asda_ui_post_import"] = pi
        spec2.loader.exec_module(pi)
        pi.main()
    except Exception as e:
        unreal.log_error("[пост-импорт] НЕ ВЫПОЛНЕН: %s — окна карты останутся без поля карты/маркеров, "
                         "а ручные правки пользователя — потерянными! "
                         "Запусти вручную: Tools\\run_headless.bat Tools\\asda_ui_post_import.py" % e)
        raise


def _script_main():
    """Работа скрипта. Вынесена в функцию, чтобы импорт модуля НИЧЕГО не делал
    (закон [[tools-no-import-side-effects]])."""
    global NO_M, SKINS, _DBGF, _TRANS
    _DBGF = open(_DBG, "w", encoding="utf-8") if _DBG else None
    _TRANS = {}
    if os.path.isfile(TRANS_JSON):
        try: _TRANS = json.load(open(TRANS_JSON, encoding="utf-8"))
        except Exception: _TRANS = {}
    NO_UV = unreal.Box2D(unreal.Vector2D(0, 0), unreal.Vector2D(0, 0))
    NO_M = unreal.Margin(0, 0, 0, 0)
    SKINS = parse_skins_full(SKIN_INI)
    main()


# ★★★ ЗАКОН tools-no-import-side-effects: НИКАКИХ действий при импорте модуля.
# ★ ЗАКОН [[tools-no-import-side-effects]]: при импорте модуль НИЧЕГО не делает.
#   `__spec__` выставляет ЛЮБАЯ машинерия импорта (import / importlib / runpy) и НЕ
#   выставляет прямое исполнение файла (python file.py, UE -ExecutePythonScript).
#   `python -m` даёт `__name__ == "__main__"` и покрыт первым условием.
if __name__ == "__main__" or globals().get("__spec__") is None:
    _script_main()
