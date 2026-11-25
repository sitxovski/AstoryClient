# -*- coding: utf-8 -*-
# Copyright (c) 2026 @sitxovski (Discord)
# asda_convert_map_lit.py — перевод карты на РЕАЛЬНЫЙ UE-свет и ПЕРЕСТАНОВКА света по данным карты.
#   1) (режим all) репарент MIC объектов/террейна на LIT-мастера (M_AsdaObjectLit/M_AsdaTerrainLit) — текстуры по имени переносятся;
#   2) РЕДАКТИРУЕМЫЕ актёры: DirectionalLight (per-map цвет+направление из .mapraw) + SkyLight (ambient per-map);
#   3) PostProcessVolume (unbound) с ФИКС-экспозицией (min=max) — чтобы яркость не «уплывала», полный контроль в редакторе.
#   Всё — обычные актёры/ассеты, редактируются в UE.
#
# Запуск (СКРЫТО, редактор закрыт):
#   ASDA_MAP=alpen  python Tools/run_headless_safe.py Tools/asda_convert_map_lit.py --marker MAPLIT-FINISHED
#   ASDA_MAP=ALL [ASDA_MAPS="alpen||rein"] ...  — все карты с .mapraw (или подмножество через '||')
#   ASDA_LIT_MODE=light (умолч.) — только свет (Sun/Sky/PP); ASDA_LIT_MODE=all — плюс репарент MIC (долго).
#
# ★★★ НАПРАВЛЕНИЕ СОЛНЦА (правка Д-1 аудита света 2026-09-03, fix_lighting_sun_and_ambient.md).
#   Источник — ключ 'sunDir' сцены .mapraw ver>=3: ГОТОВЫЙ единичный вектор из `<map>.SCE @0xB4`,
#   игровые оси (Y вверх), направлен ОТ ПОВЕРХНОСТИ К СОЛНЦУ. Это ровно то, чем светит клиент:
#   live.dmp alpen g_vLightDir = (-0.4924, 0.8660, 0.0868) == файл @0xB4; CRenderer_SetDirLight
#   (render_core.c:41396) негатит переданный вектор и кладёт в g_vLightDir, а HLSL светит
#   `max(0, dot(Normal, lhtDir))` (EmptyProject.fx:329) ⇒ lhtDir = «к солнцу».
#   До правки сюда шёл 'lightDir' — пересчёт из углов НЕВЕРНОЙ формулой (ошибка 30..90°, 9 карт —
#   солнце на горизонте вместо зенита). Файлы ver<=2 теперь отвергаются (require_sun_dir), а не читаются молча.
#   Оси: та же C(x,y,z)=(-z,x,y), что у геометрии (asda_coords, memory map-ue-export-pipeline);
#   DirectionalLight в UE светит ВДОЛЬ своего forward ⇒ forward = -C(sunDir) (распространение света).
#
# ★ КАЛИБРОВКА ЯРКОСТИ (якорь = формула оригинала `lite = LightColor·(MatAmbient 0.6 + N·L)`, дамп alpen:
#   MatAmbient=0.6, LightColor=поле ambient .SCE; unlit-порт давал на свету ≈0.98·tex, в тени 0.37·tex).
#   UE (Lambert): на свету tex·(E·N·L/π + S), в тени tex·S, где E = Sun (лк), S = SkyLight.
#   E = mx·SUN_LUX (2.6 → E/π ≈ 0.83·mx). Чтобы тень/свет = 0.6/1.6 = 0.375 как в оригинале:
#   S = 0.5·mx  ⇒ SKY_K = 0.5 (на alpen mx=0.61 → 0.31 — совпало с прежней ручной калибровкой 0.30).
#   [ПОРТ] Данных для неба в .SCE нет — Sky = функция от яркости солнца карты с одним коэффициентом
#   SKY_K; цвет Sky = цвет солнца (в оригинале ambient = MatAmbient·LightColor, т.е. тот же тинт).
#   ★ AmbientFloor (Emissive-«пол» 0.25 в Lit-мастерах) СНЯТ той же правкой (Д-3): тень теперь честно
#   темнее, тёмные карты (nightvalley mx=0.19, castle_of_chess 0.21) — тёмные.
#
# КЛЮЧИ ПОДКРУТКИ (перезапуск этого скрипта, без пересборки карт и мастеров):
#   ASDA_SUNLUX  — сила солнца = mx·SUN_LUX (умолч. 2.6);
#   ASDA_SKYK    — SkyLight = mx·SKY_K (умолч. 0.5);   ASDA_SKYLUX — АБСОЛЮТНАЯ сила Sky (перекрывает SKY_K);
#   ASDA_EXP     — фикс-экспозиция PPV (умолч. 1.0; применяется только при СОЗДАНИИ PPV).
#   Всё то же правится руками на актёрах Sun_<map>/Sky_<map>/PP_<map> в редакторе.
#
# ★★★ СЕТ A «ЦВЕТ СВЕТА» (05.09.2026, план roundtable_2026-09-04/FINAL_synthesis.md §4, спецификация
#   Reports/render_sets_2026-09-05/setA_offline_fix_report.md §3). Данные — export/maps/ground_colors.json
#   (`_schema` 2, Tools/asda_ground_color.py): линейный RGB, нормирован по max-каналу.
#   ASDA_LIT_MODE=color — режим «ТОЛЬКО ЦВЕТ»: меняет ровно три вещи — Sky_<map>.light_color,
#       Sky_<map>.lower_hemisphere_color и (по ключу ASDA_FOGDENSITY) MapFog.fog_density. Солнце (intensity/
#       rotation/color), сила Sky, PPV, мобильность локальных светов, BuiltData — НЕ ТРОГАЮТСЯ (владелец тюнил
#       руками). Сила Sky в этом режиме пишется ТОЛЬКО если ASDA_SKYK/ASDA_SKYLUX задан явно в окружении.
#       Актёры не создаются: нет Sky_<map>/MapFog — предупреждение и пропуск.
#   ASDA_HEMI=legacy|desat50|tinted_desat50 (умолч. legacy = прежнее 0.6·ambient):
#       lower_hemisphere_color = JSON[{desat50|ground_tinted_desat50}] · LOWER_K · mx (ASDA_LOWERK, умолч. 0.6);
#       tinted_desat50 = цвет detail-тайлов × вершинный тинт террейна, десатурация 50 % — то, что видит камера.
#   ASDA_SKYTINT=off|<t 0..1>|white (умолч. off = цвет солнца, как прежде): Sky.light_color =
#       norm_max(lerp(sun, fog_x, t)); white = (1,1,1) («пусть говорит капчер»). Считается НА МЕСТЕ по sun/fog из JSON;
#       при t=0.5/0.75 сверяется бит в бит с готовыми sky_tint[…]/sky_tint_linear[…] JSON (расхождение → в лог).
#   ASDA_SKYTINT_SPACE=srgb|linear (умолч. srgb): туман = байты D3DCOLOR как есть (`fog`, пол 0.15) либо
#       переведён sRGB→linear ДО lerp (`fog_norm_linear`) — ревизия: пространство lerp весит больше, чем t.
#   ASDA_FOGDENSITY=off|fogend|fogspan (умолч. off): MapFog.fog_density. Калибровка ключа
#       ASDA_FOGK (умолч. 2200): UE делит density на 1000 (SceneCore.cpp:404); в шейдере LineIntegral при нулевом falloff
#       = density·ln2 (HeightFogCommon.ush:207-213 — Тейлор log(2)), а фактор = exp2(−L·LineIntegral) (:313, :394), т.е.
#       ★ T = 2^(−ln2 · d/1000 · L) = e^(−(ln2)²·d/1000·L), непрозрачность = 1 − T (ревизия review_setA_fix.md ВАЖНО №1 —
#       прежняя запись «2^(−d·L)» завышала все проценты). nightvalley при 2200/fogEnd: ≈26 % на 2000 юн, ≈65 % на 7000
#       (ГД просил 30 %/85 % — одной экспонентой обе точки не взять; fog_max_opacity 0.9 не трогаем).
#       fogend  = ASDA_FOGK / fogEnd — игнорирует fogStart (ревизия review_setA_rollout.md ВАЖНО №2: на картах с коротким
#                 ramp-ом sunnycoast 11 %, flammio 18 %, conquester 34 % на fogEnd, в оригинале там 100 %).
#       ★ fogspan = ASDA_FOGK / (fogEnd − fogStart) (доводка 05.09): StartDistance уже = fogStart, поэтому непрозрачность
#                 на fogEnd у ВСЕХ карт одинакова = 1 − 2^(−ln2·FOGK/1000). Оригинал — линейный range-fog 0 %@fogStart →
#                 100 %@fogEnd (render_core.c:953-954, 41349-41350); экспонента «стену» не даёт. Текущий K=2737 (=1000·log2(1/0.15),
#                 выбран по формуле без ln2) даёт на fogEnd 1 − 2^(−1.897) = **73 %** (не 85 %), на nightvalley 31 %@2000 (точка ГД);
#                 порог ≥85 % на fogEnd требует FOGK = 1000·log2(1/0.15)/ln2 = **3949** (тогда nightvalley 41 %@2000). Одной
#                 экспонентой с StartDistance=fogStart обе точки (30 %@2000 и 85 %@fogEnd) не взять — выбор K за владельцем
#                 (сет C-1, долг (б): 2737 = точка ГД у ног, 3949 = порог брифа на горизонте).
#                 Δ ≤ 0 (нет данных/fogEnd ≤ fogStart) → предупреждение, плотность не трогаю. Короткий ramp
#                 (sunnycoast Δ=1000 → 2.737, flammio Δ=1500 → 1.825) даёт «стену», как в оригинале; кап по желанию
#                 владельца — ASDA_FOGDENSITY_MAX=<density> (умолч. без капа).
#       Прежняя плотность владельца 1e-6 давала 0.0005 % на 7000 юн (ревизия review_setA.md 4.3) — тумана не было.
#       ASDA_FOG_MAPS="a||b" | "ALL" — к каким картам применять fogend. ★ Раскатка 05.09 (ревизия, мелочь 9): пустой
#       ключ = НИ ОДНОЙ карты (предупреждение), раньше пустой означал «все» — опасное умолчание.
#   ★ ASDA_FOGCUTOFF=off|dome|<юн> (умолч. off; раскатка 05.09, ревизия review_setA.md ВАЖНО №1): MapFog.fog_cutoff_distance.
#       В UE 5.8 материал с IsSky НЕ защищён от ExponentialHeightFog (Material.h:1088) — при fogend купол неба и облака
#       тонут в тумане (кап 0.9). Cutoff в шейдере — по ДЛИНЕ ЛУЧА камера→пиксель (HeightFogCommon.ush:397:
#       `CameraToReceiverLength > FogCutoffDistance` → ExpFogFactor=1, тумана нет). `dome` = МИНИМАЛЬНОЕ расстояние от пивота
#       купола до его поверхности × масштаб актора × (1 − ASDA_FOGCUTOFF_MARGIN, умолч. 0.05). Купол сплюснут (SKY_DOME.JBM
#       R=1000, H=285; актор SkyDome scale 100, следует за камерой) ⇒ ближайшая точка — ЗЕНИТ 28 500 юн, а не радиус 100 000:
#       cutoff по радиусу очистил бы только ленту горизонта (<5°), зенит остался бы в тумане. Расстояние берётся из bbox
#       меша актора (AsdaSkyDomeActor.Dome или StaticMeshActor c SM_sky*/SM_skydome), без купола — не трогаю (предупреждение).
#       Оговорка (в отчёт): всё дальше cutoff тоже без тумана — мир за 27 000 юн получает резкий край 0.9→0.
#       ★ Доводка 05.09 (review_setA_rollout.md ВАЖНО №1): cutoff красил ЧЁРНЫМ пустоту за краем карты ниже уровня глаз
#       (far plane > cutoff → ExpFogFactor=1 → clear-цвет). Решение (д): купол — translucent с Apply Fogging=off
#       (Tools/asda_build_sky_dome.py), а cutoff снимается явным ASDA_FOGCUTOFF=0 (0 = выкл в шейдере; `off` = не трогать).
#   ★ ГЕЙТ ОКРУЖЕНИЯ (ревизия ВАЖНО №3): в режиме color заданные ASDA_SKYK / ASDA_SKYLUX / ASDA_SUNLUX = ОТКАЗ всего прогона
#       (log_error, ничего не пишется), кроме явного ASDA_COLOR_ALLOW_INTENSITY=1 (тогда сила Sky пишется, как раньше).
#   ★ Login (без .mapraw, уровень /Game/Map/Login) — ТОЛЬКО с ASDA_ALLOW_LOGIN_WRITE=1 И ASDA_ALLOW_LOGIN_MAP_WRITE=1
#       (★ 16.09.2026, ревизия раздела 22, СРЕДН-2: в этом уровне стоит свет владельца, а откат «нет /Game/Map/<A>/<A>
#       → LVL=/Game/Map/<A>» ниже подставлял его при ASDA_MAP=Login. Без второй переменной main() выходит сразу,
#       не загрузив и не сохранив уровень). С обоими разрешениями — как раньше: режим color по правилам с amb=None — white применим,
#       off/<t>/полусфера/fogend требуют данных → «нет данных, не трогаю»; cutoff — из актора.
#   Фолбэки (без исключений): нет JSON / `_schema`<2 / карты нет в JSON / `mx` JSON ≠ max(ambient) → предупреждение и
#       прежнее поведение; `ground` null или diag.patches_used==0 (flamio) → полусфера legacy, но Sky-тинт применим;
#       ASDA_FOGK / ASDA_LOWERK / ASDA_FOGCUTOFF_MARGIN не-число → предупреждение и умолчание (ревизия, мелочь 8).
#   Самопроверка после save: цвета/плотность/cutoff читаются ИЗ АКТЁРА и сверяются с ожидаемыми (расхождение → log_error);
#       слепок нетронутых полей (солнце, сила Sky, MapFog start/falloff/max_opacity/цвет) до/после — в лог.
#
# ★★★ СЕТ B «ТЕНИ + ГИГИЕНА» (05.09.2026, план FINAL_synthesis.md §4 Сет B, адреса developer_round2.md §2.2).
#   ASDA_CONTACT=<длина>   — contact_shadow_length на СУЩЕСТВУЮЩЕМ Sun_<map> (None = не трогать). Раньше 0.06 писался
#                            только при СОЗДАНИИ солнца (`if created_sun`) — на 22 картах с готовым солнцем ключа не было.
#   ASDA_SUNANGLE=<град>   — light_source_angle на Sun_<map> (None = не трогать). Оба ключа действуют в режимах light/all
#                            И в режиме shadow; при отсутствии ключа новое солнце получает прежние 2.0 / 0.06.
#   ASDA_LIT_MODE=shadow   — режим «ТОЛЬКО ТЕНИ»: меняет ровно contact/angle на Sun_<map> (по ключам) и флаг
#                            cast_contact_shadow у ISM-компонентов ЛИСТВЫ (по ключу ниже). Цвет/сила/направление солнца,
#                            Sky, MapFog, PPV, BuiltData — НЕ ТРОГАЮТСЯ (владелец тюнил руками); актёры не создаются;
#                            .mapraw не нужен (Login/L_Loading тоже проходят: уровень /Game/Map/<name> без подпапки;
#                            ★ 16.09.2026: Login — только с ASDA_ALLOW_LOGIN_MAP_WRITE=1, см. гейт в main()).
#   ASDA_ISM_CONTACT=off|on|keep (умолч. keep) — cast_contact_shadow=False|True у ISM `I_<model>` актёра AsdaMapObjects_<map>,
#                            если <model> (без регистра) начинается с префикса из Tools/setB_foliage_prefixes.json
#                            (ASDA_FOLIAGE_JSON). Листва — карточки/кресты (tree_/flower_/glass_…, e5b_billboards.md):
#                            контакт-тень на них даёт полосы; ip_/shop_ в список НЕ входят (developer_round2 §2.2).
#   Самопроверка из актёра: contact/angle и флаги ISM читаются обратно; слепок «нетронутых» полей до/после — в лог.
#   ★ Сет E-1 (06.09.2026): ASDA_LIT_MODE=df — режим «ТОЛЬКО DISTANCE FIELDS»: меняет ровно три вещи и ничего больше
#     (сила/цвет/поворот солнца, Sky, MapFog, PPV — не трогаются; актёры не создаются; .mapraw не нужен; L_Loading проходит,
#      Login — только с ASDA_ALLOW_LOGIN_MAP_WRITE=1, ★ 16.09.2026):
#     ASDA_DFSHADOW=off|on|keep (умолч. keep) — use_ray_traced_distance_field_shadows у Sun_<map>: у UDirectionalLightComponent
#         по умолчанию TRUE (DirectionalLightComponent.cpp:1044-1048) ⇒ после r.GenerateMeshDistanceFields=1 DF-тени солнца включились
#         бы САМИ (r.DistanceFieldShadowing=1 на High/Epic) за DynamicShadowDistance 20 000. «off» до решения владельца (план §3.6).
#     ASDA_ISM_DF=off|on|keep (умолч. keep) — affect_distance_field_lighting у ISM-компонентов ЛИСТВЫ `I_<model>` (asda_foliage):
#         кроны-карточки в DF = сплошные коробки → пятна DFAO/DF-тени (план §3.4); «off» = листва вне DF-сцены.
#     ASDA_LOCAL_DFSHADOW=off|keep (умолч. keep) — use_ray_traced_distance_field_shadows у локальных светов (Point/Spot/Rect;
#         дополнение §2.3: DF-тени фонарей грубые — off у всех; сейчас на картах их 0, ключ на будущее).
#     ★ Сет E-2 (06.09.2026, DFAO): ручки DFAO на Sky_<map> (SkyLightComponent.h:152-175, действуют при Movable+cast_shadows+DF):
#       ASDA_DFAO=on|off|keep (умолч. keep) — cast_shadows у Sky (условие DFAO в движке, DistanceFieldAmbientOcclusion.cpp:734-737);
#       ASDA_DFAO_DIST=<юн> — occlusion_max_distance (дефолт движка 1000; UI 200–1500; цена растёт с дальностью);
#       ASDA_DFAO_CONTRAST=<0..1> — contrast (S-кривая AO); ASDA_DFAO_MIN=<0..1> — min_occlusion (пол затенения; ломает контакт).
#       Незаданный ключ = поле не трогается. Значения выбраны A/B в setE2_report.md; самопроверка из актёра, как у остальных.
#     Самопроверка из актёра + слепок «нетронутых» полей солнца до/после; дифф-гейт — дамп set0_dump_light_actors.py до/после.
#   ★ Сет E-3 (07.09.2026, Lumen/небо): ASDA_LIT_MODE=sky — режим «ТОЛЬКО ТОЧКА И ПОРОГ ЗАХВАТА НЕБА» у Sky_<map> (captured-scene):
#     ДЕФЕКТ: Sky_<map> стоял в (0,0,30000) с sky_distance_threshold=150000 (дефолт движка). Захват кубмапа идёт ИЗ ПОЗИЦИИ компонента
#     с near-plane = SkyDistanceThreshold (ReflectionEnvironmentCapture.cpp:2416,2856: «anything further than SkyDistanceThreshold …
#     will be included»), а купол SkyDome — плоская шапка R=100 000 / H=28 532 (SM_skydome ×100), следующая за камерой: точка 30 000 ВЫШЕ
#     купола, и всё ближе 150 000 отсечено ⇒ верхняя полусфера кубмапа ЧЁРНАЯ, «небо» = только lower_hemisphere_color снизу.
#     Классический SkyLight/DFAO это скрывали (SKY_K откалиброван при чёрном небе), Lumen — нет: промахи лучей = кубмап (Lumen.cpp:228-235,
#     LumenSceneRendering.cpp:2515-2526) ⇒ GI без неба. Доказано визуализатором r.Lumen.Visualize 3 (setE3_report.md §2).
#     ASDA_SKYCAP_POS=dome|auto|x,y,z|keep (умолч. keep) — позиция актора Sky_<map>:
#         ★ dome (ДОВОДКА 07.09.2026, РЕКОМЕНДУЕМОЕ) = позиция САМОГО КУПОЛА (AsdaSkyDomeActor, авторская (0,0,0) —
#           asda_build_sky_dome.py:443) + ASDA_SKYCAP_ZOFF_DOME (умолч. 0). Обоснование: купол центрируется на КАМЕРЕ каждый тик
#           (AsdaSkyDomeActor.cpp:42-62), а захват кубмапа идёт при загрузке уровня, когда камера ещё в (0,0,0) ⇒ купол тоже в (0,0,0).
#           Точка захвата обязана совпасть с ЦЕНТРОМ купола, иначе зенит грани +Z смотрит в купол НАКЛОННО и near-plane
#           (= sky_distance_threshold) срезает шапку: при auto (центр террейна −25600,25600) расстояние до купола 36 204 юн,
#           высота шапки над точкой падает с 28 533 до ~13 000 < порога 20 000 ⇒ ЧЁРНЫЙ КЛИН в зените ВСЕГДА (не «когда игрок у края»).
#         auto = центр XY bounds террейна (SM_terrain), z = середина z-диапазона террейна + ASDA_SKYCAP_ZOFF (умолч. 300) —
#           режим E-3, ОСТАВЛЕН только для A/B: даёт клин (см. sky_thr_and_redirectors_report.md §A).
#     ASDA_SKYCAP_THR=<юн>|keep (умолч. keep) — sky_distance_threshold (E-3: 20000 — ближняя геометрия отсечена, дальний рельеф за 20 000
#         образует горизонт, купол 27 000…100 000 в кадре захвата); ASDA_SKYCAP_RES=<n>|keep — cubemap_resolution (умолч. keep; 128).
#     Карты БЕЗ купола (castle_of_chaos / palace_of_queen / t_*, Login, L_Loading — INDOOR по замыслу) — пропуск с логом (ASDA_SKYCAP_NODOME=1
#     заставляет). Остальное (сила/цвет Sky, полусфера, солнце, туман, PPV) — НЕ ТРОГАЕТСЯ; актёры не создаются; самопроверка из актёра.
import os, math, unreal
import os as _os, sys as _sys
_sys.path.insert(0, _os.path.dirname(_os.path.abspath(__file__)))
import asda_coords as _C
import asda_mapraw as _MR                                  # ★ единый ридер .mapraw (был свой read_scene)
import asda_maps as _MAPS                                  # ★ единый санитайзер имени карты
import asda_foliage as _FOL                                # ★ Сет E-1: единый признак листвы (был здесь; нужен и билдеру уровней)
EAL=unreal.EditorAssetLibrary; MEL=unreal.MaterialEditingLibrary; ML=unreal.MathLibrary
ar=unreal.AssetRegistryHelpers.get_asset_registry()

#   ★ Сет D (05.09.2026): ASDA_LIT_MODE=rim — режим «ТОЛЬКО РИМ»: актор AAsdaRimLightActor «Rim_<map>» (C++ AsdaReborn) с пер-карточными
#       RimColor/RimStrength/RimPower для мастеров персонажей (asda_build_materials_lit.wire_rim, коллекция MPC_AsdaToon). Ключ ASDA_RIM:
#       off (умолч.) — ничего; auto — цвет = fog_norm_linear карты из ground_colors.json, сила = RIM_MAX·clamp((RIM_MX_DAY − mx)/(RIM_MX_DAY − RIM_MX_NIGHT),0,1)
#       (mx = max(ambient) карты: nightvalley 0.19 → 0.35, castle_of_chess 0.21 → 0.30, castle_of_chaos 0.24 → 0.21, дневные ≥ 0.32 → 0 — актор
#       НЕ создаётся, существующий выключается), power 3 (ГД §4 «Слой 3»); "r,g,b,strength,power" — явные числа. В режимах light/all/color ключ
#       тоже действует (после основной работы). Уровень read-only (P4) → снимается, путь в _backup/2026-09-05_setD/_readonly_list.txt.
MAP=os.environ.get("ASDA_MAP","alpen")
RAW=os.environ.get("ASDA_RAW",r"@@ASTORY_DEV_ROOT_FWD@@/Data/Export/maps")
MODE=os.environ.get("ASDA_LIT_MODE","light").lower()       # light | all | color | shadow | rim | df | sky
RIM=os.environ.get("ASDA_RIM","off").lower()                      # ★ сет D: off | auto | "r,g,b,strength,power"
RIM_MAX,RIM_MX_NIGHT,RIM_MX_DAY,RIM_POWER=0.35,0.19,0.32,3.0       # ГД §4: nightvalley 0.35 при mx 0.19; день (mx ≥ 0.32) → 0
RIM_READONLY_LIST=r"@@ASTORY_DEV_ROOT_FWD@@/AstoryClient/_backup/2026-09-05_setD/_readonly_list.txt"
# ★ Сет B «тени» (см. шапку)
CONTACT=os.environ.get("ASDA_CONTACT")                            # None = не трогать; "0.02" = переписать на Sun_<map>
SUNANGLE=os.environ.get("ASDA_SUNANGLE")                          # None = не трогать; "1.5"
ISM_CONTACT=os.environ.get("ASDA_ISM_CONTACT","keep").lower()     # off | on | keep — cast_contact_shadow у ISM листвы
DFSHADOW=os.environ.get("ASDA_DFSHADOW","keep").lower()           # ★ Сет E-1: off | on | keep — DF-тени солнца (use_ray_traced_distance_field_shadows)
ISM_DF=os.environ.get("ASDA_ISM_DF","keep").lower()               # ★ Сет E-1: off | on | keep — affect_distance_field_lighting у ISM листвы
LOCAL_DFSHADOW=os.environ.get("ASDA_LOCAL_DFSHADOW","keep").lower()  # ★ Сет E-1: off | keep — DF-тени локальных светов
DFAO=os.environ.get("ASDA_DFAO","keep").lower()                   # ★ Сет E-2: on | off | keep — cast_shadows у Sky_<map> (условие DFAO)
SKYCAP_POS=os.environ.get("ASDA_SKYCAP_POS","keep").lower()       # ★ Сет E-3 + доводка 07.09: dome | auto | x,y,z | keep — позиция Sky_<map> (точка захвата кубмапа)
SKYCAP_THR=os.environ.get("ASDA_SKYCAP_THR","keep").lower()       # ★ Сет E-3: <юн> | keep — sky_distance_threshold (near-plane захвата)
SKYCAP_ZOFF=float(os.environ.get("ASDA_SKYCAP_ZOFF","300") or 300)  # ★ Сет E-3: подъём точки захвата над серединой z-диапазона террейна
# ★ Доводка 07.09.2026: подъём точки захвата над пивотом КУПОЛА при ASDA_SKYCAP_POS=dome. Умолчание 0 — точка ровно в центре купола
#   (симметрия граней кубмапа: зенит 28 533 юн, кольца 26 361/20 176/10 919 — все выше порога 20 000, клина нет).
SKYCAP_ZOFF_DOME=float(os.environ.get("ASDA_SKYCAP_ZOFF_DOME","0") or 0)
SKYCAP_RES=os.environ.get("ASDA_SKYCAP_RES","keep").lower()       # ★ Сет E-3: <n> | keep — cubemap_resolution
SKYCAP_NODOME=os.environ.get("ASDA_SKYCAP_NODOME","0")=="1"       # ★ Сет E-3: править и карты без купола
DFAO_DIST=os.environ.get("ASDA_DFAO_DIST")                        # ★ Сет E-2: occlusion_max_distance у Sky (None = не трогать)
DFAO_CONTRAST=os.environ.get("ASDA_DFAO_CONTRAST")                # ★ Сет E-2: contrast у Sky (None = не трогать)
DFAO_MIN=os.environ.get("ASDA_DFAO_MIN")                          # ★ Сет E-2: min_occlusion у Sky (None = не трогать)
FOLIAGE_JSON=os.environ.get("ASDA_FOLIAGE_JSON", os.path.join(os.path.dirname(os.path.abspath(__file__)),"setB_foliage_prefixes.json"))
SCALE=100.0
# ★ Сет A «цвет света» (см. шапку)
HEMI=os.environ.get("ASDA_HEMI","legacy").lower()                 # legacy | desat50 | tinted_desat50
SKYTINT=os.environ.get("ASDA_SKYTINT","off").lower()              # off | <t> | white
SKYTINT_SPACE=os.environ.get("ASDA_SKYTINT_SPACE","srgb").lower() # srgb | linear
FOGDENS=os.environ.get("ASDA_FOGDENSITY","off").lower()           # off | fogend | fogspan
def _env_num(name, default):
    """float из окружения; не-число → предупреждение и умолчание (ревизия setA, мелочь 8: раньше float() валил скрипт до логов)."""
    v=os.environ.get(name)
    if v is None or v=="": return float(default)
    try: return float(v)
    except ValueError: unreal.log_warning("SETA: %s=%r не число — беру умолчание %s"%(name,v,default)); return float(default)
FOG_K=_env_num("ASDA_FOGK",2200)                                  # density = FOG_K / fogEnd (fogend) | FOG_K / (fogEnd−fogStart) (fogspan)
FOG_DMAX=_env_num("ASDA_FOGDENSITY_MAX",0)                        # кап плотности для fogspan (0 = без капа) — решение владельца по «стене»
FOG_MAPS=os.environ.get("ASDA_FOG_MAPS","")                       # "a||b" | "ALL" — fogend только для этих карт; пусто = ни одной
FOGCUTOFF=os.environ.get("ASDA_FOGCUTOFF","off").lower()          # off | dome | <юн> — MapFog.fog_cutoff_distance (см. шапку)
FOGCUTOFF_MARGIN=_env_num("ASDA_FOGCUTOFF_MARGIN",0.05)           # запас от ближайшей точки купола (доля)
LOWER_K=_env_num("ASDA_LOWERK",0.6)                               # lower_hemisphere = c·LOWER_K·mx
# ★ Гейт окружения режима color (ревизия ВАЖНО №3): ключи силы света в окружении = отказ, если не разрешено явно.
INTENSITY_KEYS=[k for k in ("ASDA_SKYK","ASDA_SKYLUX","ASDA_SUNLUX") if k in os.environ]
COLOR_ALLOW_INTENSITY=os.environ.get("ASDA_COLOR_ALLOW_INTENSITY","")=="1"
SKYK_EXPLICIT=(("ASDA_SKYK" in os.environ) or ("ASDA_SKYLUX" in os.environ)) and COLOR_ALLOW_INTENSITY  # сила Sky в color — только явно разрешённая
GC_PATH=os.environ.get("ASDA_GROUND_COLORS", r"@@ASTORY_DEV_ROOT_FWD@@/Data/Export/maps/ground_colors.json")
GC_SCHEMA_MIN=2
SUN_LUX=float(os.environ.get("ASDA_SUNLUX","2.6"))   # сила направленного (множится на max(ambient) карты)
SKY_K=float(os.environ.get("ASDA_SKYK","0.5"))       # SkyLight = SKY_K · max(ambient) карты (см. шапку)
SKY_ABS=os.environ.get("ASDA_SKYLUX")                # абсолютная сила Sky (если задана — перекрывает SKY_K)
EXPOSURE=float(os.environ.get("ASDA_EXP","1.0"))     # фикс-экспозиция

OBJ_SRC,OBJ_LIT="M_AsdaObject","/Game/Materials/M_AsdaObjectLit"
TER_SRC,TER_LIT="M_AsdaTerrain","/Game/Materials/M_AsdaTerrainLit"

def to_ue(x,y,z): return _C.to_ue(x,y,z,SCALE)   # ★ единая конверсия осей (asda_coords)

def read_scene(path):
    """ambient, diffuse, sunDir (к солнцу, игровые оси). ValueError на .mapraw ver<=2 — перегнать экспорт."""
    sc=_MR.read_scene(path)                                # strict=True — как прежний assert на 'AMRW'
    return sc["ambient"], sc["diffuse"], _MR.require_sun_dir(sc, path)

def sun_travel_ue(sd):
    """Направление РАСПРОСТРАНЕНИЯ света в UE (forward у DirectionalLight) = -C(sunDir), единичный."""
    d=to_ue(sd[0],sd[1],sd[2])
    n=math.sqrt(d.x*d.x+d.y*d.y+d.z*d.z) or 1.0
    return unreal.Vector(-d.x/n,-d.y/n,-d.z/n)

def find_actor(eas, cls, label):
    """Существующий актёр класса cls: сначала по метке, иначе первый попавшийся (или None)."""
    first=None
    for a in eas.get_all_level_actors():
        if isinstance(a,cls):
            if a.get_actor_label()==label: return a
            if first is None: first=a
    return first

# ---------------------------------------------------------------- Сет A: цвета из ground_colors.json
_GC_CACHE={}
def load_ground_colors():
    """dict JSON (или None + предупреждение). Кэш на процесс; читается один раз."""
    if "gc" in _GC_CACHE: return _GC_CACHE["gc"]
    gc=None
    try:
        import json
        with open(GC_PATH,"r",encoding="utf-8") as f: gc=json.load(f)
        sch=gc.get("_schema",0) if isinstance(gc,dict) else 0
        if not isinstance(sch,(int,float)) or sch<GC_SCHEMA_MIN:
            unreal.log_warning("SETA: %s протух (_schema=%r < %d) — фолбэки (как без JSON)"%(GC_PATH,sch,GC_SCHEMA_MIN)); gc=None
        elif not isinstance(gc.get("maps"),dict):
            unreal.log_warning("SETA: %s без 'maps' — фолбэки"%GC_PATH); gc=None
    except Exception as e:
        unreal.log_warning("SETA: нет/битый %s (%s) — фолбэки (как без JSON)"%(GC_PATH,e)); gc=None
    _GC_CACHE["gc"]=gc
    return gc

def _norm_max(c, eps=1e-3):
    m=max(max(c),eps); return [v/m for v in c]          # ровно как asda_ground_color.norm_max
def _lerp3(a,b,t): return [a[i]+(b[i]-a[i])*t for i in range(3)]   # ровно как asda_ground_color.lerp3
def _is_rgb(v): return isinstance(v,(list,tuple)) and len(v)>=3 and all(isinstance(x,(int,float)) for x in v[:3])

def _glow_step4():
    """★★★ Пара «интенсивность, порог» ореола ступени 4 = КАЛИБРОВКА ВЛАДЕЛЬЦА 09.09.2026 (v3_rec).

    Единственный источник чисел — [AsdaGlow] Config/DefaultScalability.ini; те же строки читают C++
    (UAsdaVideoSettingsSubsystem::SetGlowLevel) и Tools/asda_bloom_rollout.py. Числа здесь не дублируем
    (закон port-shared-points): нет строк — падаем, а не подставляем «примерно такое же»."""
    ini=_os.path.join(_os.path.dirname(_os.path.dirname(_os.path.abspath(__file__))),"Config","DefaultScalability.ini")
    want={"intensity@4":None,"threshold@4":None}; sec=False
    with open(ini,"r",encoding="utf-8-sig",errors="replace") as f:
        for line in f:
            s=line.strip()
            if s.startswith("["): sec=(s.lower()=="[asdaglow]"); continue
            if not sec or "=" not in s or s.startswith((";","#")): continue
            k,_,v=s.partition("="); k=k.strip().lower()
            if k in want: want[k]=float(v.strip())
    if None in want.values(): raise ValueError("в [AsdaGlow] %s нет Intensity@4/Threshold@4"%ini)
    return want["intensity@4"],want["threshold@4"]

def map_record(MAP, amb, mx):
    """Запись карты из JSON с проверкой свежести (mx JSON == max(ambient) .mapraw). None → фолбэки."""
    gc=load_ground_colors()
    if gc is None: return None,"нет JSON"
    rec=gc["maps"].get(MAP)
    if rec is None: unreal.log_warning("SETA %s: карты нет в ground_colors.json — фолбэки"%MAP); return None,"карты нет в JSON"
    jmx=rec.get("mx")
    if not isinstance(jmx,(int,float)) or abs(jmx-mx)>1e-6:
        unreal.log_warning("SETA %s: JSON протух — mx %r ≠ max(ambient) %.7f — фолбэки"%(MAP,jmx,mx)); return None,"mx≠ambient (протух)"
    return rec,"ok"

def sky_tint_color(MAP, rec, sun_col):
    """Sky.light_color по ASDA_SKYTINT/ASDA_SKYTINT_SPACE. Возвращает (LinearColor, описание)."""
    if SKYTINT=="off": return sun_col,"off=солнце"
    if SKYTINT=="white": return unreal.LinearColor(1.0,1.0,1.0,1.0),"white"
    try: t=max(0.0,min(1.0,float(SKYTINT)))
    except ValueError:
        unreal.log_warning("SETA %s: ASDA_SKYTINT=%r не число/off/white — солнце"%(MAP,SKYTINT)); return sun_col,"off(битый ключ)"
    if rec is None: return sun_col,"off(нет данных)"
    space=SKYTINT_SPACE
    if space not in ("srgb","linear"): unreal.log_warning("SETA %s: ASDA_SKYTINT_SPACE=%r — беру srgb"%(MAP,space)); space="srgb"
    sun=rec.get("sun"); fog=rec.get("fog") if space=="srgb" else rec.get("fog_norm_linear")
    if not _is_rgb(sun) or not _is_rgb(fog):
        unreal.log_warning("SETA %s: sun/fog в JSON null — Sky = солнце"%MAP); return sun_col,"off(sun/fog null)"
    # sun из JSON обязан совпасть с sun_col (тот же ambient/mx)
    sc3=[sun_col.r,sun_col.g,sun_col.b]
    if max(abs(sun[i]-sc3[i]) for i in range(3))>1e-6:
        unreal.log_warning("SETA %s: sun JSON %s ≠ ambient/mx (%.4f,%.4f,%.4f) — Sky = солнце"%(MAP,sun,sc3[0],sc3[1],sc3[2])); return sun_col,"off(sun≠ambient)"
    c=_norm_max(_lerp3(sun,fog,t))
    # сверка с готовыми значениями JSON при t=0.5/0.75 (должны совпасть бит в бит)
    key=str(t); table=rec.get("sky_tint" if space=="srgb" else "sky_tint_linear") or {}
    ref=table.get(key) if isinstance(table,dict) else None
    chk=""
    if _is_rgb(ref):
        d=max(abs(ref[i]-c[i]) for i in range(3))
        chk=" сверка JSON[%s]: %s"%(key,"OK" if d<=1e-12 else "РАСХОЖДЕНИЕ %.3g"%d)
        if d>1e-12: unreal.log_warning("SETA %s: sky_tint[%s] JSON %s ≠ расчёт %s"%(MAP,key,ref,c))
    return unreal.LinearColor(c[0],c[1],c[2],1.0),"lerp(sun,fog_%s,%.3g)=(%.4f,%.4f,%.4f)%s"%(space,t,c[0],c[1],c[2],chk)

def lower_hemisphere(MAP, rec, amb, mx):
    """lower_hemisphere_color по ASDA_HEMI. legacy = прежнее amb·0.6 (LOWER_K по умолчанию 0.6)."""
    legacy=unreal.LinearColor(amb[0]*LOWER_K,amb[1]*LOWER_K,amb[2]*LOWER_K,1.0)
    if HEMI=="legacy": return legacy,"legacy"
    if HEMI not in ("desat50","tinted_desat50"):
        unreal.log_warning("SETA %s: ASDA_HEMI=%r неизвестен — legacy"%(MAP,HEMI)); return legacy,"legacy(битый ключ)"
    if rec is None: return legacy,"legacy(нет данных)"
    diag=rec.get("diag") or {}
    if rec.get("ground") is None or not diag.get("patches_used"):
        unreal.log_warning("SETA %s: ground null / patches_used=0 (заглушка) — полусфера legacy"%MAP); return legacy,"legacy(ground null)"
    c=None; src=HEMI
    if HEMI=="tinted_desat50":
        c=rec.get("ground_tinted_desat50")
        if not _is_rgb(c): c=rec.get("desat50"); src="desat50(tinted null)"
    else: c=rec.get("desat50")
    if not _is_rgb(c):
        unreal.log_warning("SETA %s: %s null в JSON — полусфера legacy"%(MAP,HEMI)); return legacy,"legacy(%s null)"%HEMI
    v=[c[i]*LOWER_K*mx for i in range(3)]
    return unreal.LinearColor(v[0],v[1],v[2],1.0),"%s·%.2f·mx=(%.4f,%.4f,%.4f)"%(src,LOWER_K,v[0],v[1],v[2])

def fog_density_for(MAP, rec):
    """MapFog.fog_density по ASDA_FOGDENSITY (None = не трогать)."""
    if FOGDENS=="off": return None,"off"
    if FOGDENS not in ("fogend","fogspan"): unreal.log_warning("SETA %s: ASDA_FOGDENSITY=%r неизвестен — не трогаю"%(MAP,FOGDENS)); return None,"off(битый ключ)"
    if not FOG_MAPS:                                       # ★ пустой список = ни одной карты (ревизия, мелочь 9)
        unreal.log_warning("SETA %s: ASDA_FOGDENSITY=%s, но ASDA_FOG_MAPS пуст — плотность не трогаю (нужен список или ALL)"%(MAP,FOGDENS))
        return None,"off(ASDA_FOG_MAPS пуст)"
    if FOG_MAPS.upper()!="ALL" and MAP not in FOG_MAPS.split("||"): return None,"off(не в ASDA_FOG_MAPS)"
    if rec is None: return None,"off(нет данных)"
    fe=rec.get("fogEnd")
    if not isinstance(fe,(int,float)) or fe<=0: unreal.log_warning("SETA %s: fogEnd=%r — плотность не трогаю"%(MAP,fe)); return None,"off(fogEnd null)"
    if FOGDENS=="fogend": return FOG_K/fe,"FOGK %.0f/fogEnd %.0f=%.6g"%(FOG_K,fe,FOG_K/fe)
    # ★ fogspan (доводка 05.09): ramp-длина Δ = fogEnd − fogStart, как у линейного тумана оригинала (см. шапку)
    fs=rec.get("fogStart")
    if not isinstance(fs,(int,float)) or fs<0: unreal.log_warning("SETA %s: fogStart=%r — плотность не трогаю"%(MAP,fs)); return None,"off(fogStart null)"
    span=fe-fs
    if span<=0: unreal.log_warning("SETA %s: fogEnd %.0f ≤ fogStart %.0f — плотность не трогаю"%(MAP,fe,fs)); return None,"off(Δ≤0)"
    d=FOG_K/span; capped=""
    if FOG_DMAX>0 and d>FOG_DMAX: capped=" кап %.4g→%.4g"%(d,FOG_DMAX); d=FOG_DMAX
    return d,"FOGK %.0f/(fogEnd %.0f−fogStart %.0f=%.0f)=%.6g%s"%(FOG_K,fe,fs,span,d,capped)

def _mesh_box(sm):
    """(min, max) bbox статик-меша в его локальных единицах; None, если не достать."""
    try:
        b=sm.get_bounding_box(); return b.min,b.max
    except Exception: pass
    try:
        bs=sm.get_bounds(); o=bs.origin; e=bs.box_extent
        return unreal.Vector(o.x-e.x,o.y-e.y,o.z-e.z),unreal.Vector(o.x+e.x,o.y+e.y,o.z+e.z)
    except Exception: return None

def find_sky_dome(eas):
    """Купол неба уровня: (актор, StaticMeshComponent, имя меша) — AsdaSkyDomeActor.Dome либо StaticMeshActor с SM_sky*/SM_skydome."""
    for a in eas.get_all_level_actors():
        if a.get_class().get_name()=="AsdaSkyDomeActor":
            try:
                c=a.get_editor_property("dome"); sm=c.get_editor_property("static_mesh") if c else None
                return a,c,(sm.get_name() if sm else None)
            except Exception: return a,None,None
    for a in eas.get_all_level_actors():
        if isinstance(a,unreal.StaticMeshActor):
            c=a.static_mesh_component; sm=c.get_editor_property("static_mesh") if c else None
            if sm and sm.get_name().lower() in ("sm_sky","sm_skydome"): return a,c,sm.get_name()
    return None,None,None

def dome_min_distance(MAP, eas):
    """Ближайшая точка поверхности купола к его пивоту (юн UE) по bbox меша × масштаб актора; (dist, описание) или (None, почему).
    Купол сплюснут (R=1000, H=285): грани bbox на плоскости пивота (z=0 у основания) не считаются, берётся min по остальным."""
    a,c,mesh=find_sky_dome(eas)
    if a is None: return None,"купола нет (AsdaSkyDomeActor / SM_sky*)"
    sm=c.get_editor_property("static_mesh") if c is not None else None
    if sm is None: return None,"у купола %s нет меша"%a.get_actor_label()
    box=_mesh_box(sm)
    if box is None: return None,"bbox меша %s недоступен"%mesh
    mn,mx=box; span=max(mx.x-mn.x,mx.y-mn.y,mx.z-mn.z,1e-6)
    faces=[abs(v) for v in (mn.x,mx.x,mn.y,mx.y,mn.z,mx.z) if abs(v)>1e-3*span]   # грани, не проходящие через пивот
    if not faces: return None,"bbox меша %s вырожден"%mesh
    s=a.get_actor_scale3d(); k=min(abs(s.x),abs(s.y),abs(s.z))
    d=min(faces)*k
    return d,"%s (%s) bbox=(%.0f..%.0f, %.0f..%.0f, %.0f..%.0f) scale=%.3g → ближайшая грань %.0f·%.3g=%.0f юн"%(
        a.get_actor_label(),mesh,mn.x,mx.x,mn.y,mx.y,mn.z,mx.z,k,min(faces),k,d)

def fog_cutoff_for(MAP, eas):
    """MapFog.fog_cutoff_distance по ASDA_FOGCUTOFF (None = не трогать): off | dome | <юн>."""
    if FOGCUTOFF=="off": return None,"off"
    if FOGCUTOFF=="dome":
        d,why=dome_min_distance(MAP,eas)
        if d is None: unreal.log_warning("SETA %s: ASDA_FOGCUTOFF=dome — %s — cutoff не трогаю"%(MAP,why)); return None,"off(%s)"%why
        m=max(0.0,min(0.9,FOGCUTOFF_MARGIN)); v=float(int(d*(1.0-m)))
        return v,"dome: %s, запас %.0f%% → %.0f"%(why,m*100,v)
    try: v=float(FOGCUTOFF)
    except ValueError: unreal.log_warning("SETA %s: ASDA_FOGCUTOFF=%r не off/dome/число — не трогаю"%(MAP,FOGCUTOFF)); return None,"off(битый ключ)"
    if v<0: unreal.log_warning("SETA %s: ASDA_FOGCUTOFF=%r <0 — не трогаю"%(MAP,FOGCUTOFF)); return None,"off(<0)"
    return v,"явно %.0f"%v

def _lin_to_fcolor(c):
    """Ожидаемый FColor после SetLightColor(LinearColor, sRGB=True) — для самопроверки из актёра."""
    try: return unreal.MathLibrary.conv_linear_color_to_color(c,True)
    except Exception:
        def f(v):
            v=max(0.0,min(1.0,v)); s_=v*12.92 if v<=0.0031308 else 1.055*v**(1/2.4)-0.055
            return max(0,min(255,int(s_*255.999)))
        return unreal.Color(f(c.r),f(c.g),f(c.b),255)

def verify_sky(MAP, sc, sky_col, lower, fc, dens, cutoff=None):
    """Самопроверка ИЗ АКТЁРА после записи: light_color (8-бит sRGB, допуск ±1), lower_hemisphere (1e-6), density (отн. 1e-6),
    fog_cutoff_distance (0.5 юн). sky_col/lower None = поле не писалось (Login без данных) — не проверяется."""
    errs=[]
    got=sc.get_editor_property("light_color")
    if sky_col is not None:
        exp=_lin_to_fcolor(sky_col)
        if max(abs(int(got.r)-int(exp.r)),abs(int(got.g)-int(exp.g)),abs(int(got.b)-int(exp.b)))>1:
            errs.append("light_color в актёре (%d,%d,%d) ≠ ожидаемого (%d,%d,%d)"%(got.r,got.g,got.b,exp.r,exp.g,exp.b))
    lh=sc.get_editor_property("lower_hemisphere_color")
    if lower is not None and max(abs(lh.r-lower.r),abs(lh.g-lower.g),abs(lh.b-lower.b))>1e-6:
        errs.append("lower_hemisphere в актёре (%.5f,%.5f,%.5f) ≠ (%.5f,%.5f,%.5f)"%(lh.r,lh.g,lh.b,lower.r,lower.g,lower.b))
    if fc is not None and dens is not None:
        gd=float(fc.get_editor_property("fog_density"))
        if abs(gd-dens)>1e-6*max(1.0,abs(dens)): errs.append("fog_density в актёре %.7g ≠ %.7g"%(gd,dens))
    if fc is not None and cutoff is not None:
        gc_=float(fc.get_editor_property("fog_cutoff_distance"))
        if abs(gc_-cutoff)>0.5: errs.append("fog_cutoff_distance в актёре %.1f ≠ %.1f"%(gc_,cutoff))
    for e in errs: unreal.log_error("SETA %s: САМОПРОВЕРКА: %s"%(MAP,e))
    unreal.log("SETA %s: самопроверка из актёра: light_color=(%d,%d,%d) lower=(%.5f,%.5f,%.5f)%s%s → %s"%(
        MAP,got.r,got.g,got.b,lh.r,lh.g,lh.b,
        (" fog_density=%.6g"%float(fc.get_editor_property("fog_density"))) if fc is not None else "",
        (" fog_cutoff=%.0f"%float(fc.get_editor_property("fog_cutoff_distance"))) if fc is not None else "",
        "OK" if not errs else "ПРОВАЛ"))
    return not errs

def _snap_untouched(sun, sky, fc=None):
    """Слепок полей, которые режим color НЕ имеет права менять (сверка до/после в логе):
    солнце (сила/поворот/цвет/угол/контакт), сила Sky, MapFog start/falloff/max_opacity/цвет."""
    d={}
    if sun is not None:
        dc=sun.get_component_by_class(unreal.DirectionalLightComponent); r=sun.get_actor_rotation(); c=dc.get_editor_property("light_color")
        d["sun"]=(round(dc.get_editor_property("intensity"),7),round(r.pitch,4),round(r.yaw,4),round(r.roll,4),int(c.r),int(c.g),int(c.b),
                  round(dc.get_editor_property("light_source_angle"),5),round(dc.get_editor_property("contact_shadow_length"),6))
    if sky is not None:
        sc=sky.get_component_by_class(unreal.SkyLightComponent); d["sky_intensity"]=round(sc.get_editor_property("intensity"),7)
    if fc is not None:
        try:
            col=fc.get_editor_property("fog_inscattering_luminance")
            d["fog"]=(round(float(fc.get_editor_property("start_distance")),3),round(float(fc.get_editor_property("fog_height_falloff")),9),
                      round(float(fc.get_editor_property("fog_max_opacity")),6),round(col.r,6),round(col.g,6),round(col.b,6))
        except Exception as e: d["fog"]="н/д (%s)"%e
    return d

# ---------------------------------------------------------------- Сет B: тени (контакт/угол солнца, ISM листвы)
def _env_float(name, val):
    """float из ключа окружения или None; нечисло → предупреждение и None (ключ игнорируется)."""
    if val is None: return None
    try: return float(val)
    except ValueError: unreal.log_warning("SETB: %s=%r не число — игнорирую"%(name,val)); return None

def apply_sun_shadow_keys(MAP, dc):
    """Записать contact_shadow_length / light_source_angle на компонент солнца по ключам (None = не трогать).
    Возвращает dict ожидаемых значений для самопроверки."""
    exp={}
    a=_env_float("ASDA_SUNANGLE",SUNANGLE); c=_env_float("ASDA_CONTACT",CONTACT)
    if a is not None: dc.set_editor_property("light_source_angle",a); exp["light_source_angle"]=a
    if c is not None: dc.set_editor_property("contact_shadow_length",c); exp["contact_shadow_length"]=c
    if exp: unreal.log("SETB %s: солнце ← %s"%(MAP,", ".join("%s=%g"%kv for kv in sorted(exp.items()))))
    return exp

# ★ Сет E-1 (06.09.2026): загрузчик префиксов и признак листвы переехали в Tools/asda_foliage.py (один источник для map_lit,
#   билдера уровней и прохода настроек DF). Здесь — обёртки с прежними именами (вызовы ниже и у других скриптов не менялись).
#   `exclude` — подстроки имени, при которых модель листвой НЕ считается даже под префиксом (ревизия 05.09 ВАЖНО-2).
def load_foliage_prefixes():
    """Список префиксов моделей листвы (нижний регистр) из setB_foliage_prefixes.json; нет/битый → [] + предупреждение."""
    return _FOL.load_foliage_rules(FOLIAGE_JSON)[0]

def is_foliage_model(model):
    """Модель (имя из .mapraw, без регистра) считается листвой: префикс из JSON и ни одной подстроки `exclude`."""
    return _FOL.is_foliage_model(model, FOLIAGE_JSON)

def ism_contact_pass(MAP, eas, want):
    """cast_contact_shadow=want у ISM `I_<model>` листвы на уровне. Возвращает (список компонентов, изменено, всего ISM)."""
    touched=[]; n_all=0; n_changed=0
    for a in eas.get_all_level_actors():
        for comp in a.get_components_by_class(unreal.InstancedStaticMeshComponent):
            n_all+=1
            nm=comp.get_name()
            if not nm.startswith("I_") or not is_foliage_model(nm[2:]): continue
            if bool(comp.get_editor_property("cast_contact_shadow"))!=want:
                comp.set_editor_property("cast_contact_shadow",want); n_changed+=1
            touched.append(comp)
    return touched,n_changed,n_all

def _snap_sun_untouched(sun):
    """Слепок полей солнца, которые режим shadow НЕ имеет права менять (сила/поворот/цвет/дистанция/мобильность/LF облаков)."""
    if sun is None: return {}
    dc=sun.get_component_by_class(unreal.DirectionalLightComponent); r=sun.get_actor_rotation(); c=dc.get_editor_property("light_color")
    return {"sun":(round(dc.get_editor_property("intensity"),7),round(r.pitch,4),round(r.yaw,4),round(r.roll,4),int(c.r),int(c.g),int(c.b),
                   round(dc.get_editor_property("dynamic_shadow_distance_movable_light"),3),str(dc.get_editor_property("mobility")),
                   str(dc.get_editor_property("light_function_material")))}

def main_shadow(MAP, LVL):
    """★ Режим ASDA_LIT_MODE=shadow: ТОЛЬКО contact/angle на Sun_<map> (по ключам) и cast_contact_shadow у ISM листвы."""
    les=unreal.get_editor_subsystem(unreal.LevelEditorSubsystem)
    eas=unreal.get_editor_subsystem(unreal.EditorActorSubsystem)
    les.load_level(LVL)
    sun=find_actor(eas,unreal.DirectionalLight,"Sun_%s"%MAP)
    exp={}; errs=[]
    if sun is None: unreal.log_warning("SETB %s: нет DirectionalLight — солнце не трогаю (актёры не создаются)"%MAP)
    elif sun.get_actor_label()!="Sun_%s"%MAP: unreal.log_warning("SETB %s: DirectionalLight без метки Sun_%s (%s) — правлю его"%(MAP,MAP,sun.get_actor_label()))
    before=_snap_sun_untouched(sun)
    dc=sun.get_component_by_class(unreal.DirectionalLightComponent) if sun is not None else None
    if dc is not None: exp=apply_sun_shadow_keys(MAP,dc)
    want=None
    if ISM_CONTACT in ("off","on"): want=(ISM_CONTACT=="on")
    elif ISM_CONTACT!="keep": unreal.log_warning("SETB %s: ASDA_ISM_CONTACT=%r неизвестен — ISM не трогаю"%(MAP,ISM_CONTACT))
    touched=[]; n_changed=0; n_all=0
    if want is not None:
        if not load_foliage_prefixes(): unreal.log_error("SETB %s: пустой список префиксов листвы — ISM не трогаю"%MAP)
        else: touched,n_changed,n_all=ism_contact_pass(MAP,eas,want)
    if not exp and want is None: unreal.log_warning("SETB %s: ни одного ключа (ASDA_CONTACT/ASDA_SUNANGLE/ASDA_ISM_CONTACT) — нечего делать"%MAP); return "nodata"
    les.save_current_level()
    # самопроверка ИЗ АКТЁРА
    for k,v in exp.items():
        got=float(dc.get_editor_property(k))
        if abs(got-v)>1e-5: errs.append("%s в актёре %.6g ≠ %.6g"%(k,got,v))
    bad=[c.get_name() for c in touched if bool(c.get_editor_property("cast_contact_shadow"))!=want]
    if bad: errs.append("cast_contact_shadow не записался у %d ISM: %s"%(len(bad),", ".join(bad[:5])))
    after=_snap_sun_untouched(sun)
    if before!=after: errs.append("НЕТРОНУТЫЕ ПОЛЯ СОЛНЦА ИЗМЕНИЛИСЬ: %s → %s"%(before,after))
    for e in errs: unreal.log_error("SETB %s: САМОПРОВЕРКА: %s"%(MAP,e))
    if errs: return "error"      # ★ ревизия savedir11, СРЕДН-2: провал самопроверки — не «сделано»
    sun_now=("contact=%.4g angle=%.4g"%(float(dc.get_editor_property("contact_shadow_length")),float(dc.get_editor_property("light_source_angle")))) if dc is not None else "—"
    unreal.log("SETB-SHADOW %s: солнце %s (%s) | ISM листвы: %d из %d, cast_contact_shadow→%s изменено %d | нетронутые поля=%s | самопроверка=%s"
               %(MAP,sun.get_actor_label() if sun else "нет",sun_now,len(touched),n_all,
                 ("keep" if want is None else str(want)),n_changed,"да" if before==after else "НЕТ","OK" if not errs else "ПРОВАЛ"))
    if touched: unreal.log("SETB %s: листва ISM: %s%s"%(MAP,", ".join(c.get_name() for c in touched[:40])," …" if len(touched)>40 else ""))

def ism_df_pass(MAP, eas, want):
    """★ Сет E-1: affect_distance_field_lighting=want у ISM `I_<model>` ЛИСТВЫ на уровне. Возвращает (компоненты, изменено, всего ISM)."""
    touched=[]; n_all=0; n_changed=0
    for a in eas.get_all_level_actors():
        for comp in a.get_components_by_class(unreal.InstancedStaticMeshComponent):
            n_all+=1
            nm=comp.get_name()
            if not nm.startswith("I_") or not is_foliage_model(nm[2:]): continue
            if bool(comp.get_editor_property("affect_distance_field_lighting"))!=want:
                comp.set_editor_property("affect_distance_field_lighting",want); n_changed+=1
            touched.append(comp)
    return touched,n_changed,n_all

def main_df(MAP, LVL):
    """★ Сет E-1 (06.09.2026), режим ASDA_LIT_MODE=df: ТОЛЬКО use_ray_traced_distance_field_shadows у Sun_<map> (ASDA_DFSHADOW),
    affect_distance_field_lighting у ISM листвы (ASDA_ISM_DF), use_ray_traced_distance_field_shadows у локальных светов (ASDA_LOCAL_DFSHADOW).
    Всё остальное (сила/цвет/поворот солнца, Sky, MapFog, PPV, контакт/угол) — НЕ ТРОГАЕТСЯ; актёры не создаются."""
    les=unreal.get_editor_subsystem(unreal.LevelEditorSubsystem)
    eas=unreal.get_editor_subsystem(unreal.EditorActorSubsystem)
    les.load_level(LVL)
    sun=find_actor(eas,unreal.DirectionalLight,"Sun_%s"%MAP)
    errs=[]; did=[]
    if sun is None: unreal.log_warning("SETE %s: нет DirectionalLight — солнце не трогаю (актёры не создаются)"%MAP)
    elif sun.get_actor_label()!="Sun_%s"%MAP: unreal.log_warning("SETE %s: DirectionalLight без метки Sun_%s (%s) — правлю его"%(MAP,MAP,sun.get_actor_label()))
    before=_snap_sun_untouched(sun)
    dc=sun.get_component_by_class(unreal.DirectionalLightComponent) if sun is not None else None
    # слепок и контакт/угол солнца — тоже «нетронутые» в этом режиме
    before_ca=(round(float(dc.get_editor_property("contact_shadow_length")),6),round(float(dc.get_editor_property("light_source_angle")),6)) if dc else None
    want_sun=None
    if DFSHADOW in ("off","on"): want_sun=(DFSHADOW=="on")
    elif DFSHADOW!="keep": unreal.log_warning("SETE %s: ASDA_DFSHADOW=%r неизвестен — солнце не трогаю"%(MAP,DFSHADOW))
    sun_changed=False
    if dc is not None and want_sun is not None:
        cur=bool(dc.get_editor_property("use_ray_traced_distance_field_shadows"))
        if cur!=want_sun: dc.set_editor_property("use_ray_traced_distance_field_shadows",want_sun); sun_changed=True
        did.append("sun.use_ray_traced_distance_field_shadows %s→%s"%(cur,want_sun))
    want_ism=None
    if ISM_DF in ("off","on"): want_ism=(ISM_DF=="on")
    elif ISM_DF!="keep": unreal.log_warning("SETE %s: ASDA_ISM_DF=%r неизвестен — ISM не трогаю"%(MAP,ISM_DF))
    touched=[]; n_changed=0; n_all=0
    if want_ism is not None:
        if not load_foliage_prefixes(): unreal.log_error("SETE %s: пустой список префиксов листвы — ISM не трогаю"%MAP)
        else:
            touched,n_changed,n_all=ism_df_pass(MAP,eas,want_ism); did.append("ism.affect_distance_field_lighting→%s (%d из %d, изменено %d)"%(want_ism,len(touched),n_all,n_changed))
    locals_touched=[]; locals_changed=0
    if LOCAL_DFSHADOW=="off":
        for a in eas.get_all_level_actors():
            if isinstance(a,unreal.Light) and not isinstance(a,(unreal.DirectionalLight,unreal.SkyLight)):
                for lc in a.get_components_by_class(unreal.LightComponent):
                    try:
                        if bool(lc.get_editor_property("use_ray_traced_distance_field_shadows")):
                            lc.set_editor_property("use_ray_traced_distance_field_shadows",False); locals_changed+=1
                        locals_touched.append(lc)
                    except Exception as e: unreal.log_warning("SETE %s: локальный свет %s: %s"%(MAP,a.get_actor_label(),e))
        did.append("local.use_ray_traced_distance_field_shadows→False (%d светов, изменено %d)"%(len(locals_touched),locals_changed))
    elif LOCAL_DFSHADOW!="keep": unreal.log_warning("SETE %s: ASDA_LOCAL_DFSHADOW=%r неизвестен — локальные света не трогаю"%(MAP,LOCAL_DFSHADOW))
    # ★ Сет E-2: ручки DFAO на Sky_<map> — cast_shadows / occlusion_max_distance / contrast / min_occlusion (только заданные ключи)
    sky=find_actor(eas,unreal.SkyLight,"Sky_%s"%MAP)
    skc=sky.get_component_by_class(unreal.SkyLightComponent) if sky is not None else None
    sky_want={}
    if DFAO in ("on","off"): sky_want["cast_shadows"]=(DFAO=="on")
    elif DFAO!="keep": unreal.log_warning("SETE %s: ASDA_DFAO=%r неизвестен — Sky не трогаю"%(MAP,DFAO))
    for key,raw in (("occlusion_max_distance",DFAO_DIST),("contrast",DFAO_CONTRAST),("min_occlusion",DFAO_MIN)):
        if raw is None: continue
        try: sky_want[key]=float(raw)
        except ValueError: unreal.log_warning("SETE %s: %s=%r не число — поле не трогаю"%(MAP,key,raw))
    sky_changed=0
    if sky_want and skc is None: unreal.log_warning("SETE %s: нет SkyLight — ручки DFAO не применить (актёры не создаются)"%MAP)
    elif sky_want:
        for key,val in sky_want.items():
            cur=skc.get_editor_property(key)
            same=(bool(cur)==bool(val)) if isinstance(val,bool) else abs(float(cur)-val)<1e-6
            if not same: skc.set_editor_property(key,val); sky_changed+=1
            did.append("sky.%s %s→%s"%(key,cur,val))
    if not did: unreal.log_warning("SETE %s: ни одного ключа (ASDA_DFSHADOW/ASDA_ISM_DF/ASDA_LOCAL_DFSHADOW/ASDA_DFAO*) — нечего делать"%MAP); return "nodata"
    if sun_changed or n_changed or locals_changed or sky_changed:
        _level_writable(LVL)
        les.save_current_level()
    else: unreal.log("SETE %s: значения уже целевые — уровень не сохраняю"%MAP)
    # самопроверка ИЗ АКТЁРА
    if dc is not None and want_sun is not None and bool(dc.get_editor_property("use_ray_traced_distance_field_shadows"))!=want_sun:
        errs.append("use_ray_traced_distance_field_shadows в актёре ≠ %s"%want_sun)
    bad=[c.get_name() for c in touched if bool(c.get_editor_property("affect_distance_field_lighting"))!=want_ism]
    if bad: errs.append("affect_distance_field_lighting не записался у %d ISM: %s"%(len(bad),", ".join(bad[:5])))
    badl=[c.get_name() for c in locals_touched if bool(c.get_editor_property("use_ray_traced_distance_field_shadows"))]
    if badl: errs.append("DF-тени не сняты у локальных светов: %s"%", ".join(badl[:5]))
    if skc is not None:   # ★ Сет E-2: самопроверка ручек DFAO из актёра
        for key,val in sky_want.items():
            cur=skc.get_editor_property(key)
            same=(bool(cur)==bool(val)) if isinstance(val,bool) else abs(float(cur)-val)<1e-6
            if not same: errs.append("Sky.%s в актёре = %s ≠ %s"%(key,cur,val))
    after=_snap_sun_untouched(sun)
    after_ca=(round(float(dc.get_editor_property("contact_shadow_length")),6),round(float(dc.get_editor_property("light_source_angle")),6)) if dc else None
    if before!=after or before_ca!=after_ca: errs.append("НЕТРОНУТЫЕ ПОЛЯ СОЛНЦА ИЗМЕНИЛИСЬ: %s/%s → %s/%s"%(before,before_ca,after,after_ca))
    for e in errs: unreal.log_error("SETE %s: САМОПРОВЕРКА: %s"%(MAP,e))
    if errs: return "error"      # ★ ревизия savedir11, СРЕДН-2: провал самопроверки — не «сделано»
    unreal.log("SETE-DF %s: %s | нетронутые поля=%s | самопроверка=%s"%(MAP,"; ".join(did),"да" if (before==after and before_ca==after_ca) else "НЕТ","OK" if not errs else "ПРОВАЛ"))
    if touched: unreal.log("SETE %s: листва ISM: %s%s"%(MAP,", ".join(c.get_name() for c in touched[:40])," …" if len(touched)>40 else ""))

def terrain_bounds(eas):
    """(origin, extent, актор) мировых bounds террейна (StaticMeshActor с мешем SM_terrain) или (None, None, None)."""
    for a in eas.get_all_level_actors():
        if isinstance(a,unreal.StaticMeshActor):
            c=a.static_mesh_component; sm=c.get_editor_property("static_mesh") if c else None
            if sm and sm.get_name().lower()=="sm_terrain":
                o,e=a.get_actor_bounds(False); return o,e,a
    return None,None,None

def main_sky(MAP, LVL):
    """★ Сет E-3 (07.09.2026), режим ASDA_LIT_MODE=sky: ТОЛЬКО позиция Sky_<map> (точка захвата) и sky_distance_threshold / cubemap_resolution.
    Всё остальное (сила/цвет Sky, полусфера, cast_shadows/occlusion, солнце, туман, PPV) — НЕ ТРОГАЕТСЯ; актёры не создаются."""
    les=unreal.get_editor_subsystem(unreal.LevelEditorSubsystem)
    eas=unreal.get_editor_subsystem(unreal.EditorActorSubsystem)
    les.load_level(LVL)
    sky=find_actor(eas,unreal.SkyLight,"Sky_%s"%MAP)
    if sky is None: unreal.log_warning("SETE3 %s: нет SkyLight — пропуск (актёры не создаются)"%MAP); return "nodata"
    skc=sky.get_component_by_class(unreal.SkyLightComponent)
    dome,_dc,dmesh=find_sky_dome(eas)
    if dome is None and not SKYCAP_NODOME:
        unreal.log("SETE3 %s: купола нет (INDOOR по замыслу) — Sky не трогаю (ASDA_SKYCAP_NODOME=1 заставит)"%MAP); return "nodata"
    sun=find_actor(eas,unreal.DirectionalLight,"Sun_%s"%MAP)
    before=_snap_sun_untouched(sun)
    # слепок «нетронутых» полей Sky: сила/цвет/полусфера/DFAO-ручки
    def _snap_sky():
        c=skc.get_editor_property("light_color"); lh=skc.get_editor_property("lower_hemisphere_color")
        return (round(float(skc.get_editor_property("intensity")),7),int(c.r),int(c.g),int(c.b),round(lh.r,6),round(lh.g,6),round(lh.b,6),
                bool(skc.get_editor_property("cast_shadows")),round(float(skc.get_editor_property("occlusion_max_distance")),3),
                str(skc.get_editor_property("source_type")),bool(skc.get_editor_property("real_time_capture")),bool(skc.get_editor_property("lower_hemisphere_is_black")))
    sky_before=_snap_sky()
    did=[]; errs=[]; changed=0
    was_loc=sky.get_actor_location()
    want_loc=None
    if SKYCAP_POS=="dome":
        # ★ Доводка 07.09.2026: точка захвата = пивот купола (он же позиция камеры в момент захвата) + ZOFF_DOME
        if dome is None: errs.append("купола нет — позицию dome не вычислить (ASDA_SKYCAP_POS=dome)")
        else:
            _d=dome.get_actor_location()
            want_loc=unreal.Vector(_d.x,_d.y,_d.z+SKYCAP_ZOFF_DOME)
            did.append("купол-пивот (%.0f,%.0f,%.0f) + zoff %.0f"%(_d.x,_d.y,_d.z,SKYCAP_ZOFF_DOME))
    elif SKYCAP_POS=="auto":
        o,e,ta=terrain_bounds(eas)
        if o is None: errs.append("террейн SM_terrain не найден — позицию auto не вычислить")
        else:
            want_loc=unreal.Vector(o.x,o.y,o.z+SKYCAP_ZOFF)     # origin bounds = середина z-диапазона
            did.append("terrain %s bounds origin=(%.0f,%.0f,%.0f) extent=(%.0f,%.0f,%.0f)"%(ta.get_actor_label(),o.x,o.y,o.z,e.x,e.y,e.z))
    elif SKYCAP_POS!="keep":
        try:
            x,y,z=[float(v) for v in SKYCAP_POS.split(",")]; want_loc=unreal.Vector(x,y,z)
        except ValueError: errs.append("ASDA_SKYCAP_POS=%r не dome/auto/keep/x,y,z"%SKYCAP_POS)
    if want_loc is not None:
        d=math.sqrt((was_loc.x-want_loc.x)**2+(was_loc.y-want_loc.y)**2+(was_loc.z-want_loc.z)**2)
        if d>1e-3: sky.set_actor_location(want_loc,False,False); changed+=1
        did.append("location (%.0f,%.0f,%.0f)→(%.0f,%.0f,%.0f)"%(was_loc.x,was_loc.y,was_loc.z,want_loc.x,want_loc.y,want_loc.z))
    want_thr=None
    if SKYCAP_THR!="keep":
        try: want_thr=float(SKYCAP_THR)
        except ValueError: errs.append("ASDA_SKYCAP_THR=%r не число"%SKYCAP_THR)
    if want_thr is not None:
        cur=float(skc.get_editor_property("sky_distance_threshold"))
        if abs(cur-want_thr)>1e-6: skc.set_editor_property("sky_distance_threshold",want_thr); changed+=1
        did.append("sky_distance_threshold %.0f→%.0f"%(cur,want_thr))
    want_res=None
    if SKYCAP_RES!="keep":
        try: want_res=int(SKYCAP_RES)
        except ValueError: errs.append("ASDA_SKYCAP_RES=%r не целое"%SKYCAP_RES)
    if want_res is not None:
        cur=int(skc.get_editor_property("cubemap_resolution"))
        if cur!=want_res: skc.set_editor_property("cubemap_resolution",want_res); changed+=1
        did.append("cubemap_resolution %d→%d"%(cur,want_res))
    if dome is not None:
        dl=dome.get_actor_location(); sc=dome.get_actor_scale3d(); box=_mesh_box(_dc.get_editor_property("static_mesh")) if _dc else None
        top=(box[1].z*sc.z) if box else None
        did.append("купол %s (%s) pos=(%.0f,%.0f,%.0f) scale=%.0f верх=%s"%(dome.get_actor_label(),dmesh,dl.x,dl.y,dl.z,sc.z,("%.0f"%top) if top else "?"))
    if not did: unreal.log_warning("SETE3 %s: ни одного ключа (ASDA_SKYCAP_POS/THR/RES) — нечего делать"%MAP); return "nodata"
    if changed and not errs:
        try: skc.recapture_sky()
        except Exception: pass
        _level_writable(LVL)
        les.save_current_level()
    else: unreal.log("SETE3 %s: значения уже целевые или ошибки ключей — уровень не сохраняю"%MAP)
    # самопроверка ИЗ АКТЁРА
    if want_loc is not None:
        L=sky.get_actor_location()
        if max(abs(L.x-want_loc.x),abs(L.y-want_loc.y),abs(L.z-want_loc.z))>1e-2: errs.append("location в актёре (%.1f,%.1f,%.1f) ≠ цели"%(L.x,L.y,L.z))
    if want_thr is not None and abs(float(skc.get_editor_property("sky_distance_threshold"))-want_thr)>1e-3: errs.append("sky_distance_threshold в актёре ≠ %.0f"%want_thr)
    if want_res is not None and int(skc.get_editor_property("cubemap_resolution"))!=want_res: errs.append("cubemap_resolution в актёре ≠ %d"%want_res)
    if _snap_sky()!=sky_before: errs.append("НЕТРОНУТЫЕ ПОЛЯ SKY ИЗМЕНИЛИСЬ: %s → %s"%(sky_before,_snap_sky()))
    if _snap_sun_untouched(sun)!=before: errs.append("НЕТРОНУТЫЕ ПОЛЯ СОЛНЦА ИЗМЕНИЛИСЬ")
    for e in errs: unreal.log_error("SETE3 %s: САМОПРОВЕРКА: %s"%(MAP,e))
    if errs: return "error"      # ★ ревизия savedir11, СРЕДН-2: провал самопроверки — не «сделано»
    unreal.log("SETE-SKY %s: %s | изменено полей=%d | самопроверка=%s"%(MAP,"; ".join(did),changed,"OK" if not errs else "ПРОВАЛ"))

def rim_values(MAP, rec, mx):
    """(color[3], strength, power, описание) по ASDA_RIM; None — ключ off/битый."""
    if RIM=="off": return None
    if RIM=="auto":
        if rec is None or not _is_rgb(rec.get("fog_norm_linear")): unreal.log_warning("SETD %s: ASDA_RIM=auto — нет fog_norm_linear в JSON — рим не трогаю"%MAP); return None
        col=[float(v) for v in rec["fog_norm_linear"][:3]]
        t=(RIM_MX_DAY-float(mx))/(RIM_MX_DAY-RIM_MX_NIGHT); t=max(0.0,min(1.0,t))
        return col,RIM_MAX*t,RIM_POWER,"auto(fog_norm_linear, mx=%.3f → t=%.2f)"%(mx,t)
    try:
        v=[float(x) for x in RIM.split(",")]
        if len(v)!=5: raise ValueError
        return v[:3],v[3],v[4],"явно"
    except ValueError:
        unreal.log_warning("SETD %s: ASDA_RIM=%r не off/auto/«r,g,b,strength,power» — рим не трогаю"%(MAP,RIM)); return None

def _level_writable(LVL):
    """Файл уровня read-only от Perforce → снять и записать путь для reconcile (память p4v-revert-wiped-assets)."""
    f=r"@@ASTORY_DEV_ROOT_FWD@@/AstoryClient/Content/"+LVL[len("/Game/"):]+".umap"
    if _os.path.isfile(f) and not _os.access(f,_os.W_OK):
        import stat as _st
        _os.chmod(f,_st.S_IWRITE|_st.S_IREAD)
        _os.makedirs(_os.path.dirname(RIM_READONLY_LIST),exist_ok=True)
        with open(RIM_READONLY_LIST,"a",encoding="utf-8") as fh: fh.write("Content/%s.umap\n"%LVL[len("/Game/"):])
        unreal.log("SETD: снят read-only: %s"%f)

def apply_rim(MAP, LVL, eas, rec, mx, save=True):
    """★ Сет D: актор Rim_<map> (AAsdaRimLightActor) со значениями из rim_values(). Возвращает (актор|None, описание)."""
    vals=rim_values(MAP,rec,mx)
    if vals is None: return None,"off"
    col,strength,power,why=vals
    cls=getattr(unreal,"AsdaRimLightActor",None)
    if cls is None: unreal.log_error("SETD %s: класса AsdaRimLightActor нет — модуль AsdaReborn не собран с сетом D"%MAP); return None,"нет класса"
    rim=find_actor(eas,cls,"Rim_%s"%MAP)
    if strength<=0.0:
        if rim is None: unreal.log("SETD %s: рим %s → сила 0 — актор не создаётся"%(MAP,why)); return None,"сила 0, актора нет"
        rim.set_editor_property("enable",False); rim.set_editor_property("rim_strength",0.0)
        unreal.log("SETD %s: рим %s → сила 0 — существующий %s выключен"%(MAP,why,rim.get_actor_label()))
    else:
        if rim is None:
            _level_writable(LVL)
            rim=eas.spawn_actor_from_class(cls,unreal.Vector(0,0,20000)); rim.set_actor_label("Rim_%s"%MAP)
        rim.set_editor_property("enable",True)
        rim.set_editor_property("rim_color",unreal.LinearColor(col[0],col[1],col[2],1.0))
        rim.set_editor_property("rim_strength",float(strength)); rim.set_editor_property("rim_power",float(power))
    if save:
        _level_writable(LVL)
        unreal.get_editor_subsystem(unreal.LevelEditorSubsystem).save_current_level()
    # самопроверка ИЗ АКТЁРА
    c=rim.get_editor_property("rim_color")
    unreal.log("SETD-RIM %s: %s enable=%s color=(%.3f,%.3f,%.3f) strength=%.3f power=%.2f | %s"%(MAP,rim.get_actor_label(),rim.get_editor_property("enable"),
               c.r,c.g,c.b,float(rim.get_editor_property("rim_strength")),float(rim.get_editor_property("rim_power")),why))
    return rim,why

def main_rim(MAP, LVL, amb, mx):
    """★ Режим ASDA_LIT_MODE=rim: ТОЛЬКО актор рима (солнце/небо/туман не трогаются)."""
    les=unreal.get_editor_subsystem(unreal.LevelEditorSubsystem)
    eas=unreal.get_editor_subsystem(unreal.EditorActorSubsystem)
    les.load_level(LVL)
    rec,why=map_record(MAP,amb,mx)
    if RIM=="off": unreal.log_warning("SETD %s: режим rim, но ASDA_RIM=off — нечего делать"%MAP); return "nodata"
    # ★ 16.09.2026 (ревизия savedir11, СРЕДН-1). Статус берём ИЗ apply_rim: её ранние выходы терялись, и
    #   «класса AsdaRimLightActor нет» (модуль собран без сета D) шло в «обработано» как успешная работа.
    _rim, _why = apply_rim(MAP,LVL,eas,rec,mx,save=True)
    if _rim is None:
        return "error" if _why == "нет класса" else "nodata"

def main_color(MAP, ASSET, LVL, amb, mx):
    """★ Режим ASDA_LIT_MODE=color: ТОЛЬКО Sky.light_color / lower_hemisphere_color / MapFog.fog_density / fog_cutoff_distance.
    amb=None (Login без .mapraw): применяется только то, что не требует данных карты (white, cutoff); остальное не трогается."""
    les=unreal.get_editor_subsystem(unreal.LevelEditorSubsystem)
    eas=unreal.get_editor_subsystem(unreal.EditorActorSubsystem)
    les.load_level(LVL)
    if amb is not None:
        sun_col=unreal.LinearColor(amb[0]/mx,amb[1]/mx,amb[2]/mx,1.0)
        rec,why=map_record(MAP,amb,mx)
        sky_col,d_sky=sky_tint_color(MAP,rec,sun_col)
        lower,d_low=lower_hemisphere(MAP,rec,amb,mx)
        dens,d_fog=fog_density_for(MAP,rec)
    else:                                                # ★ Login: данных карты нет
        rec,why=None,"нет .mapraw (Login)"
        sky_col,d_sky=(unreal.LinearColor(1.0,1.0,1.0,1.0),"white") if SKYTINT=="white" else (None,"не трогаю (нет данных для %s)"%SKYTINT)
        lower,d_low=None,"не трогаю (нет ambient для полусферы)"
        dens,d_fog=None,"off(нет fogEnd)"
    sun=find_actor(eas,unreal.DirectionalLight,"Sun_%s"%MAP)
    sky=find_actor(eas,unreal.SkyLight,"Sky_%s"%MAP)
    if sky is None:
        unreal.log_warning("SETA %s: нет Sky_%s — в режиме color актёры не создаются, пропуск"%(MAP,MAP)); return "nodata"
    if sky.get_actor_label()!="Sky_%s"%MAP: unreal.log_warning("SETA %s: SkyLight без метки Sky_%s (%s) — правлю его"%(MAP,MAP,sky.get_actor_label()))
    fc=None; fog_lbl="нет актёра тумана"; cutoff=None; d_cut="off"
    fogs=[a for a in eas.get_all_level_actors() if isinstance(a,unreal.ExponentialHeightFog)]
    if fogs:
        fog_a=next((a for a in fogs if a.get_actor_label()=="MapFog"),fogs[0]); fog_lbl=fog_a.get_actor_label()
        fc=fog_a.get_component_by_class(unreal.ExponentialHeightFogComponent)
        cutoff,d_cut=fog_cutoff_for(MAP,eas)
    before=_snap_untouched(sun,sky,fc)
    sc=sky.get_component_by_class(unreal.SkyLightComponent)
    if sky_col is not None: sc.set_light_color(sky_col)
    if lower is not None: sc.set_editor_property("lower_hemisphere_color",lower)
    sky_lux=None
    if SKYK_EXPLICIT and mx:
        sky_lux=float(SKY_ABS) if SKY_ABS else SKY_K*mx; sc.set_intensity(sky_lux)
    if fc is not None:
        if dens is not None: fc.set_editor_property("fog_density",dens)
        if cutoff is not None: fc.set_editor_property("fog_cutoff_distance",cutoff)
    else:
        if dens is not None: unreal.log_warning("SETA %s: ASDA_FOGDENSITY=%s, но актёра ExponentialHeightFog нет — не создаю"%(MAP,FOGDENS))
        if FOGCUTOFF!="off": unreal.log_warning("SETA %s: ASDA_FOGCUTOFF=%s, но актёра ExponentialHeightFog нет — не создаю"%(MAP,FOGCUTOFF)); d_cut="off(нет MapFog)"
    les.save_current_level()
    ok=verify_sky(MAP,sc,sky_col,lower,fc,dens,cutoff)
    after=_snap_untouched(sun,sky,fc)
    if SKYK_EXPLICIT: before.pop("sky_intensity",None); after.pop("sky_intensity",None)
    if before!=after: unreal.log_error("SETA %s: НЕТРОНУТЫЕ ПОЛЯ ИЗМЕНИЛИСЬ: %s → %s"%(MAP,before,after)); ok=False
    fdens=("%.6g"%float(fc.get_editor_property("fog_density"))) if fc is not None else "—"
    fcut=("%.0f"%float(fc.get_editor_property("fog_cutoff_distance"))) if fc is not None else "—"
    unreal.log("SETA-COLOR %s: json=%s | Sky.light_color: %s | lower: %s | Sky сила: %s | MapFog(%s) density=%s (%s) cutoff=%s (%s) | "
               "солнце/сила Sky/MapFog-прочее не тронуты=%s | самопроверка=%s"
               %(MAP,why,d_sky,d_low,("явно %.4f"%sky_lux) if sky_lux is not None else "не трогал",fog_lbl,fdens,d_fog,fcut,d_cut,
                 "да" if before==after else "НЕТ","OK" if ok else "ПРОВАЛ"))
    if rec is not None and rec.get("fog_floor_applied"):
        unreal.log("SETA %s: пол 0.15 у тумана: %s / linear %s"%(MAP,rec.get("fog_floor_channels"),rec.get("fog_floor_channels_linear")))

def main(MAP=MAP):
    ASSET=_MAPS.map_asset_name(MAP); LVL="/Game/Map/%s/%s"%(ASSET,ASSET)
    # ★★★ 16.09.2026 (ревизия раздела 22, СРЕДН-2). ASDA_MAP ни с чем не сверяется, а у режимов shadow/df/sky ниже
    #   есть ОТКАТ на уровень без подпапки (`LVL="/Game/Map/%s"%ASSET`): при ASDA_MAP=Login это САМ уровень владельца —
    #   load_level, save_current_level и удаление <уровень>_BuiltData. Уровень логина открывает только
    #   ASDA_ALLOW_LOGIN_MAP_WRITE=1 (ASDA_ALLOW_LOGIN_WRITE открывает АССЕТЫ сцены логина, но не уровень).
    #   Проверяем ОБА пути — и /Game/Map/<A>/<A>, и корень /Game/Map/<A>, на который идёт откат; выход именно
    #   отсюда, а не из _script_main: при ASDA_MAP=ALL main() зовётся на каждую карту отдельно.
    import asda_login_guard as _LG
    # ★ 16.09.2026 (ревизия §7, п. 5): плюс сверка имени карты с реестром (Tools/data/asda_map_registry.json) —
    #   опечатка в ASDA_MAP иначе молча даёт произвольный корень /Game/Map/<мусор>.
    if _LG.login_map_write_blocked("/Game/Map/%s"%ASSET, "asda_convert_map_lit.py") or \
       _LG.login_map_write_blocked(LVL, "asda_convert_map_lit.py", quiet=True) or \
       _LG.map_name_blocked("/Game/Map/%s"%ASSET, "asda_convert_map_lit.py"): return "blocked"
    if MODE=="shadow":                                       # ★ Сет B: только тени, .mapraw не нужен (Login/L_Loading без подпапки)
        if not EAL.does_asset_exist(LVL) and EAL.does_asset_exist("/Game/Map/%s"%ASSET): LVL="/Game/Map/%s"%ASSET
        if not EAL.does_asset_exist(LVL): unreal.log_warning("нет уровня %s — пропуск"%LVL); return "nodata"
        return (main_shadow(MAP,LVL)) or "ok"
    if MODE=="df":                                           # ★ Сет E-1: только DF-флаги, .mapraw не нужен (Login/L_Loading без подпапки)
        if not EAL.does_asset_exist(LVL) and EAL.does_asset_exist("/Game/Map/%s"%ASSET): LVL="/Game/Map/%s"%ASSET
        if not EAL.does_asset_exist(LVL): unreal.log_warning("нет уровня %s — пропуск"%LVL); return "nodata"
        return (main_df(MAP,LVL)) or "ok"
    if MODE=="sky":                                          # ★ Сет E-3: только точка/порог захвата Sky_<map>, .mapraw не нужен
        if not EAL.does_asset_exist(LVL) and EAL.does_asset_exist("/Game/Map/%s"%ASSET): LVL="/Game/Map/%s"%ASSET
        if not EAL.does_asset_exist(LVL): unreal.log_warning("нет уровня %s — пропуск"%LVL); return "nodata"
        return (main_sky(MAP,LVL)) or "ok"
    raw=os.path.join(RAW,MAP+".mapraw")
    if MODE=="rim":                                          # ★ Сет D: только актор рима
        if not os.path.exists(raw): unreal.log_error("нет .mapraw: %s"%MAP); return "nodata"
        if not EAL.does_asset_exist(LVL): unreal.log_warning("нет уровня %s — пропуск"%LVL); return "nodata"
        amb,dif,sd=read_scene(raw); mx=max(amb) or 1.0; return (main_rim(MAP,LVL,amb,mx)) or "ok"
    if MODE=="color":                                        # ★ Сет A: только цвет (см. шапку), без солнца/PPV/репарента
        if INTENSITY_KEYS and not COLOR_ALLOW_INTENSITY:     # ★ гейт окружения (ревизия ВАЖНО №3)
            unreal.log_error("SETA %s: ОТКАЗ — в режиме color заданы ключи силы света %s; сними их (env -u) или ASDA_COLOR_ALLOW_INTENSITY=1"%(MAP,INTENSITY_KEYS)); return "refused"
        if not os.path.exists(raw) and EAL.does_asset_exist("/Game/Map/%s"%ASSET) and not EAL.does_asset_exist(LVL):
            return (main_color(MAP,ASSET,"/Game/Map/%s"%ASSET,None,None)) or "ok"     # ★ Login: уровень без подпапки и без .mapraw
        if not os.path.exists(raw): unreal.log_error("нет .mapraw: %s"%MAP); return "nodata"
        if not EAL.does_asset_exist(LVL): unreal.log_warning("нет уровня %s — пропуск"%LVL); return "nodata"
        amb,dif,sd=read_scene(raw)
        mx=max(amb) or 1.0; return (main_color(MAP,ASSET,LVL,amb,mx)) or "ok"
    if not os.path.exists(raw): unreal.log_error("нет .mapraw: %s"%MAP); return "nodata"
    if not EAL.does_asset_exist(LVL): unreal.log_warning("нет уровня %s — пропуск"%LVL); return "nodata"
    amb,dif,sd=read_scene(raw)
    lit_obj=EAL.load_asset(OBJ_LIT); lit_ter=EAL.load_asset(TER_LIT)
    if not lit_obj or not lit_ter: unreal.log_error("нет LIT-мастеров — сначала asda_build_materials_lit.py"); return "error"   # ★ ревизия savedir10, СРЕДН-2: НЕ «нет данных» — мастера не собраны, это настройка

    # 1) РЕПАРЕНТ MIC (все папки карты) — только в режиме all (на 22 картах давно сделано; долго)
    n_obj=n_ter=0
    if MODE=="all":
        for ad in ar.get_assets_by_path("/Game/Map/%s"%ASSET, recursive=True):
            if str(ad.asset_class_path.asset_name)!="MaterialInstanceConstant": continue
            mic=EAL.load_asset(str(ad.package_name)); par=mic.get_editor_property("parent")
            if not par: continue
            if par.get_name()==OBJ_SRC: mic.set_editor_property("parent",lit_obj); EAL.save_loaded_asset(mic); n_obj+=1
            elif par.get_name()==TER_SRC: mic.set_editor_property("parent",lit_ter); EAL.save_loaded_asset(mic); n_ter+=1

    # 2) СВЕТ на уровне
    les=unreal.get_editor_subsystem(unreal.LevelEditorSubsystem)
    eas=unreal.get_editor_subsystem(unreal.EditorActorSubsystem)
    les.load_level(LVL)
    for a in list(eas.get_all_level_actors()):
        # ★ Юзерские локальные света (PointLight/SpotLight/RectLight) НЕ сносим, но переводим в MOVABLE:
        #   Static/Stationary требуют «Собрать свет» (Lightmass), а он у нас ЗАПРЕЩЁН (меши без lightmap-UV,
        #   печёт мусор) → static-точка «не светит». Movable светит сразу, динамически.
        if isinstance(a,(unreal.PointLight,unreal.SpotLight,unreal.RectLight)):
            lc_=a.get_component_by_class(unreal.LightComponent)
            if lc_ and lc_.get_editor_property("mobility")!=unreal.ComponentMobility.MOVABLE:
                lc_.set_editor_property("mobility",unreal.ComponentMobility.MOVABLE)
                unreal.log("  локальный свет %s → MOVABLE"%a.get_actor_label())
    # ★ Снести запечённый Lightmass-мусор, если юзер жал «Собрать свет» (BuiltData бесполезен и вреден).
    bd=LVL+"_BuiltData"
    if EAL.does_asset_exist(bd): EAL.delete_asset(bd); unreal.log("  BuiltData удалён")
    mx=max(amb) or 1.0
    sun_col=unreal.LinearColor(amb[0]/mx,amb[1]/mx,amb[2]/mx,1.0)
    travel=sun_travel_ue(sd)
    rot=ML.find_look_at_rotation(unreal.Vector(0,0,0),travel)
    # ★ Существующий Sun_<map> ОБНОВЛЯЕМ НА МЕСТЕ (не сносим): на нём висит light_function_material
    #   теней облаков (asda_add_cloud_shadows.py) и правки владельца; пересоздание всё это теряло.
    sun=find_actor(eas,unreal.DirectionalLight,"Sun_%s"%MAP); created_sun=False
    if sun is None:
        sun=eas.spawn_actor_from_class(unreal.DirectionalLight,unreal.Vector(0,0,30000),rot); created_sun=True
    else:
        sun.set_actor_rotation(rot,False)
    dc=sun.get_component_by_class(unreal.DirectionalLightComponent)
    # ★ MOVABLE: полностью динамический свет — НЕ требует BuiltData/«Собрать свет». Static/Stationary в рантайме
    #   (загрузка мира через логин-флоу) без BuiltData давал ЧЁРНЫЕ объекты; напрямую в редакторе выглядел норм
    #   (editor-превью). Movable ведёт себя одинаково в редакторе и рантайме.
    dc.set_editor_property("mobility",unreal.ComponentMobility.MOVABLE)
    dc.set_light_color(sun_col); dc.set_intensity(mx*SUN_LUX)
    # ★ Сет B: угол/контакт — по ключам ASDA_SUNANGLE/ASDA_CONTACT на ЛЮБОМ солнце; без ключей новое солнце
    #   получает прежние 2.0/0.06, существующее — не трогается (как раньше).
    if created_sun:
        dc.set_editor_property("dynamic_shadow_distance_movable_light",20000.0)
        if SUNANGLE is None: dc.set_editor_property("light_source_angle",2.0)      # мягкий край тени (Genshin-стиль, не бритвенный)
        if CONTACT is None:  dc.set_editor_property("contact_shadow_length",0.06)  # контакт-тени: тёмный стык у подножий (не «плоско»)
    apply_sun_shadow_keys(MAP,dc)
    # shadow_amount НЕ трогаем (1.0): светлоту тени задаёт SkyLight
    sun.set_actor_label("Sun_%s"%MAP)
    sky_lux=float(SKY_ABS) if SKY_ABS else SKY_K*mx
    sky=find_actor(eas,unreal.SkyLight,"Sky_%s"%MAP)
    if sky is None: sky=eas.spawn_actor_from_class(unreal.SkyLight,unreal.Vector(0,0,30000))
    sc=sky.get_component_by_class(unreal.SkyLightComponent)
    # ★ НЕ real_time_capture (без SkyAtmosphere RTC=чёрный ambient + fps-жор). Movable+captured-scene:
    #   капчер сцены (купол неба) на загрузке уровня — работает и в рантайме.
    sc.set_editor_property("mobility",unreal.ComponentMobility.MOVABLE)
    sc.set_editor_property("real_time_capture",False)
    sc.set_editor_property("source_type",unreal.SkyLightSourceType.SLS_CAPTURED_SCENE)
    sc.set_intensity(sky_lux)
    # ★ Сет A: цвет Sky и полусферы по ключам ASDA_SKYTINT/ASDA_HEMI (умолчания off/legacy = прежнее поведение:
    #   [ПОРТ] ambient = MatAmbient·LightColor — тот же тинт, что у солнца; полусфера 0.6·ambient).
    rec,why=map_record(MAP,amb,mx)
    sky_col,d_sky=sky_tint_color(MAP,rec,sun_col)
    lower,d_low=lower_hemisphere(MAP,rec,amb,mx)
    sc.set_light_color(sky_col)
    # ★ Нижняя полусфера НЕ чёрная (дефолт UE = чёрный солид → низ/наклонённые поверхности без ambient
    #   «скатываются в полную тьму»). Тёплый средний тон от цвета карты.
    sc.set_editor_property("lower_hemisphere_color",lower)
    sky.set_actor_label("Sky_%s"%MAP)
    dens,d_fog=fog_density_for(MAP,rec); fc=None
    if dens is not None:
        fogs=[a for a in eas.get_all_level_actors() if isinstance(a,unreal.ExponentialHeightFog)]
        if fogs:
            fc=next((a for a in fogs if a.get_actor_label()=="MapFog"),fogs[0]).get_component_by_class(unreal.ExponentialHeightFogComponent)
            fc.set_editor_property("fog_density",dens)
        else: unreal.log_warning("SETA %s: ASDA_FOGDENSITY=%s, но актёра ExponentialHeightFog нет — не создаю"%(MAP,FOGDENS))
    unreal.log("SETA %s: json=%s | Sky.light_color: %s | lower: %s | fog: %s"%(MAP,why,d_sky,d_low,d_fog))
    try: sc.recapture_sky()
    except Exception: pass
    # 3) PostProcess (фикс-экспозиция — полный контроль яркости); существующий PP_<map> не трогаем
    ppv=find_actor(eas,unreal.PostProcessVolume,"PP_%s"%MAP)
    if ppv is None:
        try:
            ppv=eas.spawn_actor_from_class(unreal.PostProcessVolume,unreal.Vector(0,0,0))
            ppv.set_editor_property("unbound",True)
            s=ppv.get_editor_property("settings")
            s.set_editor_property("override_auto_exposure_min_brightness",True); s.set_editor_property("auto_exposure_min_brightness",EXPOSURE)
            s.set_editor_property("override_auto_exposure_max_brightness",True); s.set_editor_property("auto_exposure_max_brightness",EXPOSURE)
            # ★ AO (GTAO из конфига): умеренный — «приземляет» объекты, не грязнит плоские аниме-поверхности.
            s.set_editor_property("override_ambient_occlusion_intensity",True); s.set_editor_property("ambient_occlusion_intensity",0.4)
            s.set_editor_property("override_ambient_occlusion_radius",True);    s.set_editor_property("ambient_occlusion_radius",100.0)
            # ★★★ Ореол (у оригинала — LEVEL OF GLOW). ЧИСЛА ВЫБРАНЫ ВЛАДЕЛЬЦЕМ 09.09.2026 (вариант `v3_rec`
            #   калибровочного ряда, Reports/bloom_calibration_2026-09-09.md §3): интенсивность 4.0, порог 2.5.
            #   Берём их из [AsdaGlow] Config/DefaultScalability.ini (ступень 4) — единственный источник, из
            #   которого те же числа читают C++ и Tools/asda_bloom_rollout.py (закон port-shared-points).
            #   БЫЛО: интенсивность 0.35 без порога (−1 = отсечки нет) — bloom брал ВЕСЬ кадр, поэтому и был
            #   прижат вдвое; отсечка 2.5 убирает из буфера сцену и оставляет ореол у ярких ядер.
            #   Радиус (bloom_size_scale) и блики (lens_flare_*) НЕ трогаем — владелец решил «как есть».
            #   ★ Этот блок работает ТОЛЬКО для НОВОГО волюма (`if ppv is None`). Существующие PP_<карта>
            #   обновляет отдельный проход Tools/asda_bloom_rollout.py.
            _bi,_bt=_glow_step4()
            s.set_editor_property("override_bloom_intensity",True); s.set_editor_property("bloom_intensity",_bi)
            s.set_editor_property("override_bloom_threshold",True); s.set_editor_property("bloom_threshold",_bt)
            ppv.set_editor_property("settings",s); ppv.set_actor_label("PP_%s"%MAP)
        except Exception as e:
            unreal.log_warning("PostProcess exposure: %s (пропускаю — экспозицию выставишь в редакторе)"%e)
    if RIM!="off": apply_rim(MAP,LVL,eas,rec,mx,save=False)   # ★ сет D: ключ действует и в light/all
    _level_writable(LVL)
    les.save_current_level()
    # ★ Самопроверка ИЗ АКТЁРА: forward солнца после записи == -C(sunDir) (угол в градусах, ожидается 0).
    fwd=sun.get_actor_forward_vector()
    dot=max(-1.0,min(1.0,fwd.x*travel.x+fwd.y*travel.y+fwd.z*travel.z))
    ang=math.degrees(math.acos(dot))
    unreal.log("LIT %s: репарент obj=%d ter=%d | sunDir(игр)=(%.4f,%.4f,%.4f) travel(UE)=(%.4f,%.4f,%.4f) rot=(p%.2f y%.2f r%.2f) угол=%.3f° | "
               "Sun цвет=(%.2f,%.2f,%.2f) сила=%.3f | Sky=%.3f | exp=%.2f"
               %(MAP,n_obj,n_ter,sd[0],sd[1],sd[2],travel.x,travel.y,travel.z,rot.pitch,rot.yaw,rot.roll,ang,
                 amb[0],amb[1],amb[2],mx*SUN_LUX,sky_lux,EXPOSURE))
    if ang>0.05: unreal.log_error("LIT %s: солнце НЕ совпало с sunDir (угол %.3f°)"%(MAP,ang))
    if ang>0.05:
        return "error"          # ★ ревизия savedir11, СРЕДН-2: солнце не совпало с эталоном — это провал
    if verify_sky(MAP,sc,sky_col,lower,fc,dens) is False:   # ★ Сет A: цвета/плотность из актёра
        return "error"
    # ★ 16.09.2026 (ревизия savedir9, СРЕДН-2): статус нужен, чтобы итог и маркер отличали настоящую работу
    #   от раннего выхода («нет .mapraw», «нет уровня», «нет LIT-мастеров», отказ по ключам окружения).
    return "ok"


def _script_main():
    """Работа скрипта. Вынесена в функцию, чтобы импорт модуля НИЧЕГО не делал
    (закон [[tools-no-import-side-effects]])."""
    # ★ ASDA_MAP=ALL — раскатка на ВСЕ карты, у которых есть .mapraw (login/L_Loading не карты — скипаются
    #   отсутствием уровня/равки); ASDA_MAPS="a||b" — подмножество (одна сессия на 22 уровня тяжёлая).
    # ★★★ ЗАЩИТА СВЕТА ВЛАДЕЛЬЦА. Гейт стоит в ДВУХ местах, и оба нужны: в main() — для одиночной карты и как
    #   последний рубеж, и в ALL-цикле ниже — чтобы заблокированная карта доехала до итоговой строки (без этого
    #   прогон печатал маркер успеха даже когда не обработал ничего; ревизия savedir8, СРЕДН-3). Оба спрашивают
    #   ASDA_ALLOW_LOGIN_MAP_WRITE — ключ САМОГО уровня; ASDA_ALLOW_LOGIN_WRITE открывает только АССЕТЫ сцены.
    #   ★ 16.09.2026 (ревизия savedir3, СРЕДН-1): прежняя проверка сравнивала имя с литералом "Login" и спрашивала
    #   не тот ключ, из-за чего запуск с ВЕРНЫМ ключом падал SystemExit(3) ещё до гейта, а литерал не узнавал иных
    #   форм имени (урок [[false-negatives-method]]). Её здесь больше нет.
    import sys as _sys
    _sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
    _miss_names = []
    if MAP.upper()=="ALL":
        names=[fn[:-7] for fn in sorted(os.listdir(RAW)) if fn.lower().endswith(".mapraw")]
        if MODE in ("shadow","df","sky"): names+=["Login","L_Loading"]     # ★ Сет B/E-1/E-3: без .mapraw, но со своим Sun_*/Sky_* (asda_convert_chars_lit.py)
        if MODE=="color": names+=["Login"]                  # ★ Сет A раскатка 05.09: Login по правилам color без данных карты
        only=os.environ.get("ASDA_MAPS","")
        if only:
            # ★ 16.09.2026 (ревизия savedir10, СРЕДН-3). ASDA_MAPS ни с чем не сверялась: опечатка («alpne») или
            #   неверный разделитель («,», одиночный «|») давали ПУСТОЙ список — и прогон печатал маркер успеха,
            #   не сделав ничего. Называем непопавшие имена поимённо.
            _want = [s.strip() for s in only.split("||") if s.strip()]
            names = [n for n in names if n in _want]
            _miss = _miss_names = [s for s in _want if s not in names]
            if _miss:
                unreal.log_error("LIT: имена из ASDA_MAPS не найдены среди карт: %s (разделитель — «||»; "
                                 "список карт берётся из .mapraw в %s)" % (", ".join(_miss), RAW))
        # ★ 16.09.2026 (ревизия savedir3, СРЕДН-1): отбор «выкинуть Login по ASDA_ALLOW_LOGIN_WRITE» убран —
        #   Login остаётся в списке, а решение принимает гейт (login_map_write_blocked): без разрешения он
        #   печатает свою строку и карта считается заблокированной, с ASDA_ALLOW_LOGIN_MAP_WRITE=1 — работает.
        # ★★★ 16.09.2026 (ревизия savedir8, СРЕДН-3). Гейт спрашивается ЗДЕСЬ, а не только внутри main():
        #   main() на блокировке просто возвращает управление, цикл молча шёл дальше, а в конце БЕЗУСЛОВНО
        #   печатался маркер успеха MAPLIT-FINISHED. Прогон, в котором заблокированы ВСЕ карты, отчитывался
        #   успешным. Путь строится тем же законом, что в main(): /Game/Map/<asset>.
        import asda_login_guard as _LG_all
        _done = 0; _blocked = 0; _errors = 0; _skipped = 0; _nodata = 0; _protected = 0
        for n in names:
            _root = "/Game/Map/%s" % _MAPS.map_asset_name(n)
            # ★ 16.09.2026 (ревизия savedir9, МЕЛК-1 + доводка). Порядок проверок важен и выверен симуляцией:
            #   1) СЦЕНА ЛОГИНА — первой и безусловно, чтобы подмена списка NON_MAPS не могла увести уровень
            #      владельца из графы «заблокировано» (quiet=True: ту же строку печатает гейт внутри main(),
            #      иначе она шла бы дважды — МЕЛК-2);
            #   2) известные «не карты» — тихо, отдельной графой: это состав папки export/maps, а не проблема;
            #   3) сверка имени с реестром — для всего остального.
            #   ★ Если поставить сверку имени ПЕРЕД пунктом 2, заглушка flamio попадает в «заблокировано»,
            #     и ШТАТНЫЙ прогон ASDA_MAP=ALL всегда получает неуспешный вердикт. Проверено симуляцией.
            if _LG_all.login_map_write_blocked(_root, "asda_convert_map_lit.py", quiet=True):
                # ★ 16.09.2026 (ревизия savedir10, СРЕДН-1). Строку печатаем САМИ: `continue` до main() не
                #   доходит, а гейт зван тихо (иначе при разрешённом прогоне строка шла бы дважды). Без неё
                #   в журнале оставалось «заблокировано=1» без указания карты и причины.
                unreal.log_warning("[LOGIN-GUARD] asda_convert_map_lit.py: карта %s ПРОПУЩЕНА — это сцена логина "
                                   "со светом владельца; её уровень открывает только ASDA_ALLOW_LOGIN_MAP_WRITE=1"
                                   % n)
                # ★ 16.09.2026 (ревизия savedir11, СРЕДН-4): это ОЖИДАЕМЫЙ пропуск, а не проблема прогона.
                #   Своя графа: иначе штатный ASDA_MAP=ALL в color/shadow/df/sky всегда неуспешен — Login
                #   остаётся в списке карт и по построению блокируется.
                _protected += 1; continue
            if n in getattr(_MAPS, "NON_MAPS", {}):
                # ★ 16.09.2026 (замечание ревизии savedir8). Известные «не карты» (flamio — стаб 114 байт без
                #   .TER, load — экраны загрузки) лежат в export/maps рядом с картами. Это ДАННЫЕ, трогать их
                #   нельзя; но и «заблокировано» о них писать незачем — иначе штатный прогон каждый раз
                #   отчитывается о проблеме, которой нет. Считаем отдельно.
                _skipped += 1
                continue
            if _LG_all.map_name_blocked(_root, "asda_convert_map_lit.py"):
                _blocked += 1; continue
            try:
                _st = main(n)
                if _st == "ok":        _done += 1
                elif _st == "blocked": _blocked += 1
                elif _st == "nodata":  _nodata += 1
                else:                  _errors += 1      # "refused" и любой неожиданный ответ — это не работа
            except ValueError as e:
                unreal.log_error("LIT %s: %s" % (n, e)); _errors += 1
        unreal.log("=== LIT ИТОГ: обработано=%d, заблокировано=%d, ошибок=%d, без данных=%d, "
                   "пропущено (не карты)=%d, пропущено (защита логина)=%d, всего=%d ==="
                   % (_done, _blocked, _errors, _nodata, _skipped, _protected, len(names)))
        if not names:
            # ★ ревизия savedir10, СРЕДН-3: «нечего делать» — это не успех.
            unreal.log_error("LIT: список карт ПУСТ (ASDA_MAPS=%r отфильтровала всё либо в %s нет .mapraw) — "
                             "прогон не сделал ничего" % (os.environ.get("ASDA_MAPS", ""), RAW))
        # ★ ревизия savedir11, СРЕДН-3: непопавшие имена ASDA_MAPS — тоже неуспех: просили N карт, сделали меньше.
        # ★ 16.09.2026 (ревизия savedir12, СРЕДН-3 + МЕЛК-1). Успех — это СДЕЛАННАЯ работа. Прежнее условие
        #   считало успехом прогон, где весь список ушёл в пропуски: «ASDA_MAPS=Login» в режимах sky/shadow/df/
        #   color давало «пропущено (защита логина)=1, всего=1» и зелёный вердикт — владелец считал бы свет
        #   раскатанным. Заодно снят разнобой: одиночная карта с «нет данных» уже была неуспехом.
        _ok = (_blocked == 0 and _errors == 0 and _done > 0 and not _miss_names)
    else:
        _ok = (main() == "ok")     # ★ ревизия savedir10, МЕЛК-2: успех — это сделанная работа, а не «не блок»
    # ★★★ 16.09.2026 (ревизия savedir9, СРЕДН-1). Маркер УСПЕХА печатается, только когда успех и есть.
    #   run_headless_safe.py выносит вердикт «прогон удался» по одному факту наличия маркера (verdict(): ok=True),
    #   поэтому безусловная печать означала: гейт заблокировал все карты — а обёртка отчиталась успехом.
    #   При блокировке печатается MAPLIT-BLOCKED: маркера успеха обёртка не находит, вердикт честный.
    # ★ 16.09.2026 (ревизия savedir11, МЕЛК-2). У строк РАЗНЫЕ начала: run_headless_safe.py по умолчанию ищет
    #   маркер «ЗАВЕРШЕНО», и прежняя пара («ЗАВЕРШЕНО (MAPLIT-FINISHED)» / «ЗАВЕРШЕНО (MAPLIT-BLOCKED)») давала
    #   ему успех в обоих случаях — защита держалась только на дисциплине вызывающего.
    if _ok:
        unreal.log("ЗАВЕРШЕНО (MAPLIT-FINISHED)")
    else:
        # ★ МЕЛК-3: батч-драйверы ловят (Exception, SystemExit) и считают провал — иначе asda_build_all_maps
        #   писал «КАРТА ГОТОВА» на каждом неуспешном прогоне конвертера.
        unreal.log_error("LIT ПРЕРВАНО (MAPLIT-BLOCKED): прогон не выполнил работу — см. строку ИТОГ выше")
        raise SystemExit(3)


# ★ ЗАКОН [[tools-no-import-side-effects]]: при импорте модуль НИЧЕГО не делает.
#   `__spec__` выставляет ЛЮБАЯ машинерия импорта (import / importlib / runpy) и НЕ
#   выставляет прямое исполнение файла (python file.py, UE -ExecutePythonScript).
#   `python -m` даёт `__name__ == "__main__"` и покрыт первым условием.
if __name__ == "__main__" or globals().get("__spec__") is None:
    _script_main()
