# Copyright (c) 2026 @sitxovski (Discord)
# asda_export_backdrop_anim.py — АНИМАЦИЯ login-задника: клип `cre_creback-a_00_0.jbb` → пер-элементные
# дельта-треки для 697 акторов `BackdropElem_*` (декомпозиция asda_export_backdrop_elements.py).
#
# ★ ЧТО ЭТО И ПОЧЕМУ ИМЕННО ТАК (реверс, `Reports/login_backdrop_anim.md`):
#   * Задник — ОДНА модель `cre_creback-a_00.jbm` из 697 РИГИДНЫХ под-мешей (техника tNoSkin 0x200/0x201,
#     палитра скиннинга пуста, M=0). У ригидного под-меша кость одна и ищется ПО ИМЕНИ УЗЛА меша
#     (`asda_skinned_to_gltf.resolve_skin_by_name`, ветка «РИГИДНЫЙ узел (0x200, без палитры)»).
#   * Клип сцены — `cre_creback-a_00_0.jbb` (эталон `LoginCharMake_LoadBackdrop@0xF21150`: грузит
#     ИМЕННО его + два крошечных `-b_00_*.jbb` = дорожки якоря камеры). Файл `cre_creback-a_00.jbb`
#     (6.8 МБ, 1000 костей) движком НЕ грузится и к именам узлов задника отношения не имеет
#     (совпадений имён 0 из 697) — см. отчёт, раздел «ложный след».
#   * Проигрывание: каждый кадр `animSetTrackValueByIndex(&renderCtxA, 0, dt)`; когда
#     `Game_IsPausedOrMenu` (== `time >= duration`) — `CObject_SetField13(...,0)` сбрасывает время
#     в 0. То есть клип крутится В ЦИКЛЕ. Данные это подтверждают: ключ 0 == ключ 200 байт-в-байт.
#   * Скелет клипа: 692 кости, ВСЕ — дети единственного корня `cre_creback-a_00_all link`, у которого
#     все 201 ключ тождественны (pos=0, quat const, scale const, conv=1). Глубина 1 + неподвижный
#     корень ⇒ «плоское» и «иерархическое» чтение ключей дают ОДНО И ТО ЖЕ (проверено численно),
#     поэтому вопрос флага 0x2 (`CClass_DispatchByFlag@0x99EDC0`) здесь не влияет ни на что.
#
# ★ ЧТО ПИШЕТСЯ: RawAssets/backdrop/backdrop_anim.json — на каждый элемент трек ЛОКАЛЬНОЙ дельты в
#   UE-пространстве (см), готовый к применению как `NewRelative = LocalDelta * BaseRelative`.
#   Именно ЛОКАЛЬНАЯ (сопряжённая базовым трансформом элемента) дельта, а не глобальная: тогда правка
#   дизайнера (элемент подвинули/повернули) НЕ ломает качание — оно едет вместе с элементом
#   (закон [[ue-editor-friendly]]). Для НЕтронутых элементов результат тождественно равен оригиналу.
#
# Запуск: python asda_export_backdrop_anim.py      (системный python с numpy)
import os as _os, sys as _sys
_sys.path.insert(0, _os.path.dirname(_os.path.abspath(__file__)))
import os, sys, json, re, zipfile, importlib.util
import numpy as np
import asda_paths as _P

HERE = os.path.dirname(os.path.abspath(__file__))
OUT = os.path.join(HERE, "..", "RawAssets", "backdrop")
ELEMENTS_JSON = os.path.join(OUT, "backdrop_elements.json")

MODEL_ENTRY = "cre_creback-a_00.jbm"          # модель задника внутри CharModel.bnz
CLIP_ENTRY = "cre_creback-a_00_0.jbb"         # клип сцены внутри CharMotion.baz (эталон: LoadBackdrop)
CHAR_MOTION_BAZ = os.path.join(_P.DATA, "Character", "Motion", "CharMotion.baz")

# ★★★ 12.09.2026: масштаб сцены логина приведён к ×1 (как у всех игровых карт) — причины и откат
#   описаны в asda_build_backdrop_elements.py. Число ДОЛЖНО совпадать с тамошним SCALE, иначе
#   дельта-треки уедут относительно элементов: берём его из той же переменной окружения.
SCALE = float(os.environ.get("ASDA_LOGIN_SCALE", "1.0"))   # было 100.0 («×100-сцена», метры→см)
# ★ ОСЬ jbm→UE: `to_ue = (x, -z, y) × 100` (выведена сверкой glb↔_OLD, [[login-scene-decomposition]];
#   МЕНЯТЬ НЕЛЬЗЯ). В row-vector-виде это p_ue = p_jbm · S, где S — матрица ниже.
S3 = np.array([[1.0, 0.0, 0.0],
               [0.0, 0.0, 1.0],
               [0.0, -1.0, 0.0]]) * SCALE      # строка i = образ базисного вектора jbm в UE
S4 = np.eye(4); S4[:3, :3] = S3                # однородная (без переноса — конверсия линейна)
S4I = np.linalg.inv(S4)

# Порог «кость реально движется»: разброс трансляции > 0.01 модельной ед. либо кватерниона > 1e-4.
MOVE_EPS_POS = 0.01
MOVE_EPS_QUAT = 1e-4


def load_conv():
    """Загрузить asda_skinned_to_gltf как модуль (parse_jbm / parse_jbb / eng_row_matrix / mat_to_quat)."""
    p = os.path.join(HERE, "asda_skinned_to_gltf.py")
    spec = importlib.util.spec_from_file_location("skconv", p)
    m = importlib.util.module_from_spec(spec)
    sys.modules["skconv"] = m
    spec.loader.exec_module(m)
    return m


def _norm(s):
    """Нормализация имени узла/кости для сопоставления (движок сравнивает без учёта регистра/пробелов)."""
    return re.sub(r"\s+", " ", s or "").strip().lower()


def main():
    """Работа скрипта. Вынесена в функцию, чтобы импорт модуля НИЧЕГО не делал
    (закон [[tools-no-import-side-effects]])."""
    conv = load_conv()

    # ---- модель задника (тот же источник, что у asda_export_backdrop_elements: CharModel.bnz) ----
    zm = zipfile.ZipFile(_P.CHAR_MODEL_BNZ)
    jbm = conv.parse_jbm(zm.read(MODEL_ENTRY))
    meshes = jbm["meshes"]

    # ---- клип сцены ----
    zb = zipfile.ZipFile(CHAR_MOTION_BAZ)
    jbb = conv.parse_jbb(zb.read(CLIP_ENTRY))
    bones = jbb["bones"]
    B = jbb["B"]
    K = bones[0]["keyCount"]
    fps = float(jbb["fps"])

    # ---- проверки-инварианты (если данные не такие — лучше упасть, чем тихо выдать мусор) ----
    assert all(b["keyCount"] == K for b in bones), "у костей клипа РАЗНОЕ число ключей — формула ниже неверна"
    t0 = bones[0]["t"]
    assert all(np.allclose(b["t"], t0, atol=1e-4) for b in bones), "у костей клипа РАЗНЫЕ времена ключей"
    step = np.diff(t0)
    assert np.allclose(step, 1.0 / fps, atol=1e-4), "шаг ключей НЕ равномерный 1/fps"
    root = [i for i, b in enumerate(bones) if b["parent"] < 0]
    assert len(root) == 1 and root[0] == 0, "ожидался ЕДИНСТВЕННЫЙ корень скелета (индекс 0)"
    assert all(b["parent"] in (-1, 0) for b in bones), "скелет глубже 1 уровня — плоское чтение неверно"
    r = bones[0]
    assert float(np.ptp(r["pos"], 0).max()) < 1e-6 and float(np.ptp(r["quat"], 0).max()) < 1e-6, \
        "корень скелета ДВИЖЕТСЯ — нужен иерархический пересчёт"
    duration = float(t0[-1])

    # ---- размещения элементов (имена/центры) — из готового JSON, чтобы 1:1 совпасть с акторами ----
    with open(ELEMENTS_JSON, "r", encoding="utf-8") as f:
        elements = json.load(f)["elements"]
    assert len(elements) == len(meshes), "backdrop_elements.json не соответствует модели (число элементов)"

    # ---- индекс костей по имени ----
    bidx = {}
    for i, b in enumerate(bones):
        bidx.setdefault(_norm(b["name"]), i)

    def W(i, k):
        """Мировая матрица кости i на ключе k, ДОСЛОВНО как движок (row-major, v·M). Скелет плоский
        (глубина 1, корень тождественен), поэтому ключ = сразу world."""
        b = bones[i]
        kk = min(k, b["keyCount"] - 1)
        return conv.eng_row_matrix(b["quat"][kk], b["pos"][kk], b["scale"][kk], b["conv"])

    tracks = []
    static_names = []      # кость есть, но она неподвижна → элемент остаётся статикой
    unbound_names = []     # кости с таким именем в клипе НЕТ вообще
    max_scale_dev = 0.0    # контроль: дельта должна быть ЖЁСТКОЙ (без масштаба)

    for ei, e in enumerate(elements):
        name = e["name"]                                  # bg_<idx>_<node>
        node = name.split("_", 2)[2] if name.count("_") >= 2 else ""
        # сверка с моделью: имя узла под-меша должно совпасть с хвостом имени элемента
        assert _norm(node) == _norm(meshes[ei].get("node", "")), \
            "элемент %s не соответствует под-мешу %d (%s)" % (name, ei, meshes[ei].get("node", ""))
        bi = bidx.get(_norm(node))
        if bi is None:
            unbound_names.append(name)
            continue
        b = bones[bi]
        moving = (float(np.ptp(b["pos"], 0).max()) > MOVE_EPS_POS
                  or float(np.ptp(b["quat"], 0).max()) > MOVE_EPS_QUAT)
        if not moving:
            static_names.append(name)
            continue

        # Базовый трансформ элемента в UE = ЧИСТЫЙ перенос в to_ue(center) (билдер ставит поворот 0,
        # масштаб 1). Он же — пивот, вокруг которого сопрягаем дельту.
        c = e["center"]
        t0_ue = np.array([c[0], c[1], c[2]], np.float64) @ S3      # to_ue(center), см
        T0 = np.eye(4); T0[3, :3] = t0_ue
        T0I = np.eye(4); T0I[3, :3] = -t0_ue

        W0I = np.linalg.inv(W(bi, 0))
        keys = []
        for k in range(K):
            D_eng = W0I @ W(bi, k)              # дельта в модельном пространстве (row-vector)
            D_ue = S4I @ D_eng @ S4             # та же дельта в UE-пространстве (см)
            L = T0 @ D_ue @ T0I                 # ЛОКАЛЬНАЯ дельта: NewRel = L · BaseRel
            R_row = L[:3, :3]
            sc = np.array([np.linalg.norm(R_row[i]) for i in range(3)])
            max_scale_dev = max(max_scale_dev, float(np.abs(sc - 1.0).max()))
            q = conv.mat_to_quat((R_row / sc[:, None]).T)   # column-конвенция → компоненты FQuat (x,y,z,w)
            keys.extend([round(float(L[3, 0]), 3), round(float(L[3, 1]), 3), round(float(L[3, 2]), 3),
                         round(float(q[0]), 7), round(float(q[1]), 7), round(float(q[2]), 7), round(float(q[3]), 7)])
        # НЕПРЕРЫВНОСТЬ ЗНАКА кватерниона: q и −q — один поворот, но скачок знака даёт «длинный» slerp.
        for k in range(1, K):
            a = keys[(k - 1) * 7 + 3: (k - 1) * 7 + 7]
            bq = keys[k * 7 + 3: k * 7 + 7]
            if sum(x * y for x, y in zip(a, bq)) < 0.0:
                for j in range(3, 7):
                    keys[k * 7 + j] = -keys[k * 7 + j]
        tracks.append(dict(element=name, bone=b["name"], keys=keys))

    data = dict(clip=os.path.splitext(CLIP_ENTRY)[0], model=os.path.splitext(MODEL_ENTRY)[0],
                fps=fps, duration=duration, numKeys=K, stride=7,
                tracks=tracks, static=static_names, unbound=unbound_names)
    os.makedirs(OUT, exist_ok=True)
    with open(os.path.join(OUT, "backdrop_anim.json"), "w", encoding="utf-8") as f:
        json.dump(data, f)

    print("клип %s: костей=%d ключей=%d fps=%.0f длительность=%.4f с (луп: ключ0==ключ%d)"
          % (CLIP_ENTRY, B, K, fps, duration, K - 1))
    print("элементов всего      : %d" % len(elements))
    print("  с ДВИЖУЩЕЙСЯ костью: %d  ← треки записаны" % len(tracks))
    print("  с неподвижной костью: %d  ← остаются статикой" % len(static_names))
    print("  без кости в клипе   : %d  ← остаются статикой: %s" % (len(unbound_names), unbound_names))
    print("макс. отклонение масштаба дельты от 1.0: %.2e (дельта жёсткая)" % max_scale_dev)
    print("записано: %s (%.1f МБ)" % (os.path.join(OUT, "backdrop_anim.json"),
                                      os.path.getsize(os.path.join(OUT, "backdrop_anim.json")) / 1048576.0))


# ★ ЗАКОН [[tools-no-import-side-effects]]: при импорте модуль НИЧЕГО не делает.
if __name__ == "__main__" or globals().get("__spec__") is None:
    main()
