// Copyright (c) 2026 @sitxovski (Discord)
// AsdaSystemOptionSubsystemInternal.h — ★ R2 10.11.2026 (разрез AsdaSystemOptionSubsystem.cpp): ЗАКРЫТЫЙ заголовок частей AsdaSystemOptionSubsystem*.cpp.
//   Только нужное нескольким частям: категория журнала (определение — AsdaSystemOptionSubsystem.cpp), static-помощники и константы
//   без состояния (текст байт в байт, у каждой части своя копия), extern-объявления ключей/состояния
//   (одно определение — в домашней части). Подключать ПОСЛЕ включений пролога.
#pragma once

#include "UI/AsdaSystemOptionSubsystem.h"
#include "AsdaDevConsole.h"   // ★ античит А2: ASDA_DEV_CVAR / ASDA_DEV_CONSOLE — дев-ключи и команды вне Shipping

#include "UI/AsdaWindowSubsystem.h"
#include "UI/AsdaEscChain.h"   // ★ 25.10.2026: шаг «ESC снимает выбранную цель» эталонной цепочки (между проходом окон и меню 69400)
#include "UI/AsdaUiHelpers.h"
#include "UI/AsdaNotice.h"
#include "UI/AsdaLocIds.h"
#include "UI/AsdaJuiComboBox.h"
#include "UI/AsdaShowOffSubsystem.h"
#include "UI/AsdaChatBalloonSubsystem.h"
#include "UI/AsdaAfterDeathSubsystem.h"   // ★ гейт ESC, ветка 1: принудительный экран смерти
#include "Resources/AsdaVideoSettingsSubsystem.h"
#include "Resources/AsdaBenchSubsystem.h"   // ★ 12.10.2026: «Автоподбор» = замер настоящего кадра на месте
#include "Resources/AsdaGameUserSettings.h"   // ★ 19.09.2026: профиль игрока — штатное хранилище настроек окна
#include "Resources/AsdaSoundSubsystem.h"      // ★ 19.09.2026: громкость — через профиль, мимо приоритетов ключей консоли
#include "World/AsdaKeybindConfig.h"
#include "World/AsdaKeybindProfile.h"

#include "Blueprint/WidgetTree.h"
#include "Components/Button.h"
#include "Components/CanvasPanel.h"
#include "Components/CanvasPanelSlot.h"
#include "Components/CheckBox.h"
#include "Components/Image.h"
#include "Components/PanelWidget.h"
#include "Components/TextBlock.h"
#include "Engine/Engine.h"
#include "Engine/GameInstance.h"
#include "GameFramework/PlayerController.h"
#include "HAL/IConsoleManager.h"
#include "Kismet/GameplayStatics.h"
#include "Kismet/KismetSystemLibrary.h"
#include "Misc/App.h"
#include "Misc/ConfigCacheIni.h"
#include "Text/AsdaLang.h"   // ★ 09.10.2026: строка «Язык» (мультиязычность)
#include "Scalability.h"   // ★ фаза 2: уровни групп качества при открытии окна
#include "Test/AsdaSelfTestKit.h"   // ★ 12.10.2026: самотест asda.SysOpt.SelfTest (аргументы, отказ онлайн, итог)
// ★ 18.10.2026: самотест строки «Цифры урона» — GetDamageTextMode/ShouldShowDamageText   ★ R3: комментарий снятого включения "World/AsdaWorldSubsystem.h" (файл им не пользовался) — сохранён
#include "UnrealClient.h"                // ★ 18.10.2026: FScreenshotRequest — кадры окна в самотесте «Цифры урона»
#include "Misc/FileHelper.h"             // ★ 18.10.2026: чтение GameUserSettings.ini в самотесте «Цифры урона»
#include "Containers/Ticker.h"           // ★ 18.10.2026: пошаговый самотест «Цифры урона»
#include "UI/AsdaJuiWindowHandle.h"        // ★ Дедуп №2, В1-B (U22): каркас `EnsureLazy` окон 69400/69500
#include "World/AsdaCursorClip.h"           // ★ 01.11.2026: курсор не покидает окно игры
#include "World/AsdaOrbitCursor.h"          // ★ 07.11.2026: курсор при зажатой ПКМ скрыт и стоит на месте
#include "World/AsdaCursorSpeed.h"          // ★ 19.10.2026: «Чувствительность мыши» (строка I2) — скорость курсора

// Категория журнала частей AsdaSystemOptionSubsystem*.cpp: определение — AsdaSystemOptionSubsystem.cpp.
DECLARE_LOG_CATEGORY_EXTERN( LogAsdaSysOpt, Log, All );

static const FName kOptionWindowId = FName(TEXT("SYSTEM_Option_NEW"));

namespace SysOptLoc {
	// Кнопки окна (`uiNoticeDialogInitControls` ui_core.c:88093-88107, `uiDlgInitControlsByID` :88320/:88327).
	static constexpr int32 kBtnOk      = 2288;    // 70442 «ОК»

	static constexpr int32 kBtnCancel  = 2289;    // 70443 «Отмена»

	static constexpr int32 kPageEffects   = 990301;   // «Эффекты»            (5-я ячейка; она же подпись группы эффектов)
	static constexpr int32 kWinMode       = 990302;   // «Режим экрана»

	static constexpr int32 kRenderScale   = 990305;   // «Масштаб рендера»
	static constexpr int32 kFpsLimit      = 990306;   // «Ограничение кадров»

	static constexpr int32 kVSync         = 990308;   // «Вертикальная синхронизация»
	static constexpr int32 kGfxApi        = 990309;   // «Графический режим»

	static constexpr int32 kApiNote       = 990312;   // «Графический режим сменится после перезапуска игры.»

	static constexpr int32 kLang          = 990354;   // «Язык»
	static constexpr int32 kLangNote      = 990355;   // «Язык сменится после перезапуска игры.»

	// ★ [ПОРТ] 18.10.2026 (явное разрешение владельца): строка «Цифры урона» на странице «Отображение имён» — режим показа
	//   всплывающих цифр урона, поле профиля `UAsdaGameUserSettings::DamageTextMode` (синхронизация боя Ф5). Пункты по значению 0/1/2.
	static constexpr int32 kDmgText       = 990357;   // «Цифры урона»   / «Damage numbers»
	static constexpr int32 kDmgMine       = 990358;   // «Только мои»    / «Mine only»   — 0: касающиеся меня (я бью / бьют меня)
	static constexpr int32 kDmgAll        = 990359;   // «Все»           / «All»         — 1: все (умолчание порта)
	static constexpr int32 kDmgOff        = 990360;   // «Не показывать» / «Off»         — 2: всплывающих цифр нет
	// ★ 19.10.2026 [ПОРТ] (запрос владельца): строки I2/I3 страницы «Разное» — полосы 10…360 % (★ v2, было 20…180), поля профиля
	//   `CursorSpeedPercent` / `CameraTurnPercent`.
	static constexpr int32 kCursorSpeed   = 990361;   // «Чувствительность мыши»            / «Mouse sensitivity»
	static constexpr int32 kCameraTurn    = 990362;   // «Чувствительность поворота камеры» / «Camera rotation sensitivity»
	// ★ 25.10.2026 [ПОРТ] (запрос владельца): строка I4 страницы «Разное» — режим Ctrl ходьбы вприсядку, поле профиля `CrouchMode`.
	static constexpr int32 kCrouchMode    = 990364;   // «Присед»  / «Crouch»
	static constexpr int32 kCrouchToggle  = 990365;   // «Нажатие» / «Toggle» — 0: одиночное нажатие Ctrl включает/выключает
	static constexpr int32 kCrouchHold    = 990366;   // «Зажатие» / «Hold»   — 1: присед, пока Ctrl удерживается
	static constexpr int32 kPreset        = 990313;   // «Общее качество»

	static constexpr int32 kAutoDetect    = 990320;   // «Автоподбор»
	static constexpr int32 kGrpTex        = 990321;   // «Текстуры»
	static constexpr int32 kGrpShadow     = 990322;   // «Тени»
	static constexpr int32 kGrpPP         = 990323;   // «Постобработка»

	static constexpr int32 kGrpRefl       = 990326;   // «Отражения»

	static constexpr int32 kDrawDistance  = 990328;   // «Дальность прорисовки»
	static constexpr int32 kAAMethod      = 990329;   // «Сглаживание»

	static constexpr int32 kSharpen       = 990334;   // «Резкость»

	static constexpr int32 kMotionBlur    = 990340;   // «Размытие в движении»

	static constexpr int32 kClouds        = 990342;   // «Объёмные облака»
}

namespace SysOptLocFix {
// ═══════════════════════════ ★★★ ИСПРАВЛЕНИЯ ПЕРЕВОДА ОКНА (фаза 2, 19.09.2026) ═══════════════════════════
//   ПРАВИЛО. Оригинальные номера строк GameText НЕ ПЕРЕПИСЫВАЮТСЯ: 11601 нужна ещё и `SystemMenu_OnUICommand`
//   (ui_core.c:80225), 12190…12194 оригинал сравнивает с ТЕКСТОМ `config.txt` (`Config_ParseSettings`). Поэтому
//   исправление — НАША строка в диапазоне порта 990200+, а окно берёт «нашу, если она есть, иначе эталонную»
//   (`LocUi`). Таблица ниже — ЕДИНСТВЕННОЕ место соответствия «эталонный id → наш id». Доказательства смысла —
//   в плане фазы 1 (`opt_plan/A_labels.tsv`, колонка «Смысл по эталону»), ключевые — прямо у строк.
//   [ИСПРАВЛЕНИЕ ПЕРЕВОДА] — всё, что ниже.
	struct FFix { int32 Etalon; int32 Ours; };
	static const FFix kFixes[] = {
		// ── Заголовки групп и строки страниц клавиш (`uiDlgInitControlsByID`, ui_core.c:88359-88480) ──
		{ 30040, 990200 },   // «Быстрый доступ» → «Страницы панели»: действия 1…5 = `Hotbar_SetPage(0…4)`
		{ 30041, 990201 }, { 30042, 990202 }, { 30043, 990203 }, { 30044, 990204 }, { 30045, 990205 },
		                     // ★ «Ячейка быстрого доступа N» — НЕВЕРНЫЙ СМЫСЛ: это СТРАНИЦЫ панели → «Страница панели N»
		{ 30060, 990206 },   // «Горячие клавиши ячеек» → «Панель быстрого доступа» (действия 6…17 — ячейки основной панели)
		{ 30061, 990207 }, { 30062, 990208 }, { 30063, 990209 }, { 30064, 990210 }, { 30065, 990211 }, { 30066, 990212 },
		{ 30067, 990213 }, { 30068, 990214 }, { 30069, 990215 }, { 30070, 990216 }, { 30071, 990217 }, { 30072, 990218 },
		{ 30260, 990219 },   // «Добавленные горячие клавиши» → «Верхняя панель» (кор. 상단퀵슬롯; действия 18…29)
		{ 30261, 990207 }, { 30262, 990208 }, { 30263, 990209 }, { 30264, 990210 }, { 30265, 990211 }, { 30266, 990212 },
		{ 30267, 990213 }, { 30268, 990214 }, { 30269, 990215 }, { 30270, 990216 }, { 30271, 990217 }, { 30272, 990218 },
		                     // «Добавленная клавиша N» (★ у 30272 опечатка «13») → «Ячейка N» под заголовком «Верхняя панель»
		{ 30091, 990220 },   // «Снаряжение» → «Персонаж» (`uiWindowToggleTab(CharaInfo, 0)`)
		{ 30092, 990221 },   // «Действия » (хвостовой пробел) → «Действия»
		{ 30095, 990222 },   // «Задания » → «Задания»
		{ 30097, 990223 },   // «Поиск приятелей» → «Поиск игроков» (WHO; «приятели» в клиенте — друзья)
		{ 30098, 990224 },   // «Общая карта» → «Карта мира» (окно 1710)
		{ 30099, 990225 },   // «Звания » → «Звания»
		{ 30102, 5500 },     // ★ «Улучшение» — НЕВЕРНО: действие 41 (INTERFACE REFINE) открывает окно крафта `CItemMainWnd`
		                     //   (`hotkeyDispatchAction` case 41 → `tileInteractGateAndOpen(g_pItemMainWnd)`), заголовок
		                     //   которого — эталонная loc 5500 «Создание предмета» (ITEM_making_movetext). Своя строка не нужна.
		{ 30103, 990226 },   // «Улучшен. снаряжения » → «Улучшение снаряжения»
		{ 30104, 990227 },   // «Открыть дом питомца» → «Дом питомца» (в ряду существительных)
		{ 30105, 990228 },   // «Сохранить изображение» → «Снимок экрана»
		{ 30122, 990229 },   // «Выход из отряда» → «Выйти из отряда»
		{ 30123, 990230 },   // «Изгнание из отряда» → «Исключить из отряда»
		{ 30140, 990231 },   // «Базовые действия » → «Основные действия»
		{ 30141, 990232 }, { 30142, 990233 }, { 30143, 990234 }, { 30144, 990235 },   // «Вперёд/Назад/Поворот влево/вправо»
		{ 30147, 990236 },   // «Обменять предмет» → «Обмен» (обычный обмен, 2,2)
		{ 30148, 990237 },   // ★ «Обменять предмет» — ДУБЛЬ 30147, а это обмен ЛИЧНЫМИ вещами (2,3) → «Обмен личными вещами»
		{ 30150, 990238 },   // «Выбрать ближайшего врага» → «Ближайший враг»
		{ 30151, 990239 },   // «Сведения о персонаже» → «Осмотреть персонажа» (сведения о ДРУГОМ персонаже, 2,5)
		{ 30152, 990240 },   // «Выделить себя » → «Выбрать себя»
		{ 30153, 990241 },   // «Указать цель» → «Поделиться целью» (TARGET SHARE, 2,6)
		{ 30154, 990242 },   // ★ «Выбрать разделяющего игрока» — бессмыслица: перебор членов отряда + нотис 20307 → «Кто задаёт цель отряда»
		{ 30155, 990243 },   // ★ «Личное хранилище» — НЕВЕРНО: это личная ЛАВКА (2,7), хранилище = склад → «Личная лавка»
		{ 30184, 990244 },   // ★ «Шепот» — ПЕРЕСТАВЛЕНО: действие 69 = `ChatInput_ParseWhisper(,1)` ответ → «Ответить на шёпот»
		{ 30185, 990245 },   // ★ «Ответный шепот» — ПЕРЕСТАВЛЕНО: действие 70 = режим 7 шёпот → «Шёпот»
		{ 30221, 990246 },   // «Приветствие» → «Поприветствовать» (эмоции — глаголами в одном стиле)
		{ 30223, 990247 },   // «Злость» → «Злиться»
		{ 30224, 990248 },   // «Грусть» → «Грустить»
		{ 30225, 990249 },   // «Радость» → «Радоваться»
		{ 30230, 990250 },   // «Ухаживание» → «Ухаживать»
		// ── Списки под-страниц и кнопки ──
		{ 30012, 990251 },   // «Отряды и общее» → «Отряд и действия»
		{ 30401, 990252 },   // «Отображение имен» → «Отображение имён»
		{ 11601, 990253 },   // ★ кнопка 69652 «Отмена» совпадала с «Отмена» окна (2289), а действие иное — «Снять клавишу»
		// ── «Настройки игры» ──
		{ 30421, 990254 },   // «Предложить Дружбу» (повелительное у флага) → «Принимать предложения дружбы»
		{ 30422, 990255 }, { 30423, 990256 }, { 30424, 990257 }, { 30425, 990258 },
		{ 30426, 990259 },   // «Услышанный шепот» → «Принимать шёпот» (кор. 귓속말 받기)
		{ 30427, 990260 },
		{ 30451, 990261 },   // «Показывать ОЗ монстра» → «…монстров»
		{ 30452, 990262 },   // «Показывать мое имя и ОЗ» → «Показывать своё имя и ОЗ»
		{ 30471, 990263 },   // «Выводить подсказки» → «Показывать подсказки»
		{ 30473, 990264 },   // ★ «…окно обработки предметов» — НЕВЕРНО: окно ХВАСТОВСТВА 28100 (кор. 아이템 자랑창)
		{ 30474, 990265 }, { 30475, 990266 },
		{ 30477, 990267 },   // ★ «Горячая клавиша работает только при Shift» — НЕВЕРНО: Shift гейтит ПЕРЕТАСКИВАНИЕ ячеек панели
		{ 30478, 990268 },   // «Больше горячих клавиш» → «Показывать верхнюю панель» (второй ряд панели)
		{ 30479, 990269 },   // ★ «Появится после отката.» — ДОРАЗОБРАНО (фаза 2): флаг `g_bOptMiniMap` (мисномер) гейтит в
		                     //   `pktHandlerCharFxStop@0xF4EDF0` (опкод 5274) вызов `sub_B4F100`: по центру экрана всплывает
		                     //   и сжимается 256→32 px ЗНАЧОК НАВЫКА (`uiDrawTexturedSprite`, текстура записи навыка). Сервер
		                     //   шлёт 5274, когда истёк ОТКАТ навыка (GameServer.c: `sub_47B830` сравнивает прошедшее с
		                     //   полем, которое `sub_47B9C0` заполняет временем отката из таблицы навыков с учётом
		                     //   сокращений отката) → «Показывать значок навыка после отката».
		{ 30480, 990270 },   // ★ «Отображать герб клана» — НЕВЕРНО: `Faction_Grade.tex` у имени = знак РАНГА
		// ── «Настройки системы» ──
		{ 30522, 990271 },   // «Звук» (совпадало с названием под-страницы) → «Звуковые эффекты» (кор. 효과음)
		{ 30562, 990272 },   // ★ «Сглаживание ландшафта» — НЕВЕРНО: бит TERRAIN SHADER, кор. «мягкий горизонт» → «Мягкий горизонт»
		{ 30564, 990273 },   // ★ «Всплывающие подсказки» — НЕВЕРНО: облачка речи (кор. 말풍선) → «Облачка речи»
		{ 30565, 990274 },   // «Шейдеры» → «Шейдерные эффекты» (мастер эффект-пути `g_bUseEffectPath`)
		{ 30566, 990275 },   // « - Отражение в воде» → « - Отражения в воде»
		{ 30568, 990276 },   // «MipFilter  » (англ.) → «Трилинейная фильтрация» (D3DSAMP_MIPFILTER LINEAR/NONE)
		{ 30569, 990277 },   // «Звездное небо» → «Звёздное небо»
		{ 30570, 990278 },   // «Эффекты мерцания » → «Свечение» (кор. 눈부심 효과; LEVEL OF GLOW EFFECT)
	};

	static int32 Find(int32 EtalonId)
	{
		for (const FFix& F : kFixes) { if (F.Etalon == EtalonId) { return F.Ours; } }
		return EtalonId;
	}
}

namespace SysOptExtra {
// ═══════════════════════════ ★★★ НОВЫЕ СТРОКИ ОКНА (фаза 2, 19.09.2026) ═══════════════════════════════
//   ВСЕ узлы — КЛОНЫ существующих узлов ЭТОГО окна в АССЕТЕ (шаг `Tools/asda_ui_option_extra_rows.py`):
//   список — клон строки «Разрешение» 70051–70053, флажок — строки «Полный экран» 70054/70055 (на «Экране»)
//   или 3D-строки (на «Эффектах»), полоса — «Яркости» 70057/70058, кнопка — «По умолчанию» 69651.
//   Имена детерминированы: `SYSTEM_OX_<тег>_grp` (строка), `_TXT` (подпись), `_CMB` (список), `_CHK`
//   (флажок), `_BTN` / `_BTN_Text` (кнопка); планы списков — `Bk_ComboBox_<n>_0/_1/_txt`, полосы — `SideBar_Scr_<n>_0…3`.
//   Код здесь ставит ТОЛЬКО подписи (из loc) и состояние — разметки не создаёт (закон `no-runtime-widget-fixes`).
//   ★★★ 12.10.2026 СОКРАЩЕНИЕ СОСТАВА 23 → 16 (вердикт дизайнера `Reports/owner_1012/presets/01_designer.md` §1 «Итог по
//   составу» + владелец: ползунок дальности Q9 оставить, DX12/DX11 оставить, блики убрать совсем). Сняты строки Q5 «Растительность»
//   (sg.FoliageQuality на ISM-мир не действует), Q6 «Детализация вдали» (ступени; одна ручка дальности — ползунок Q9), Q8 «Глобальное
//   освещение» (ступень неразличима, цена неустойчива — ставит пресет), E3 «Анизотропия» (слита в «Текстуры»), E4 «Затенение
//   окружения» (на High/Max при Lumen флажок ничего не делает — ставит уровень), E6 «Блики объектива» (решение владельца 10.09:
//   выключены совсем; поле профиля `LensFlare` и его миграция ОСТАЮТСЯ). Узлы снимает тот же шаг ассета (`REMOVED_ROWS` генератора),
//   оставшиеся строки он сдвигает вверх без дыр. Loc-строки снятых пунктов (9903xx) не удалены. Ключи перечисления сняты НЕ были:
//   индекс `State.Groups[Key − Q1_Tex]` и «наши» idnum 71000 + ключ держатся на них.
	enum EKey : int32
	{
		S1_Mode = 1, S2_Scale, S3_Fps, S4_VSync, S5_Api, S6_Note, S7_Lang, S8_LangNote,   // S7/S8 — ★ 09.10.2026 язык
		Q0_Preset = 10, QA_Auto, Q1_Tex, Q2_Shadow, Q3_Fx, Q4_PP, Q5_Foliage, Q6_View, Q7_Refl, Q8_GI, Q9_Draw,   // Q5/Q6/Q8 — без строки с 12.10.2026
		E1_AA = 30, E2_Sharpen, E3_Aniso, E4_AO, E5_MB, E6_LF, E7_Clouds,                                        // E3/E4/E6 — без строки с 12.10.2026
		I1_DmgText = 50,   // ★ 18.10.2026: вкладка «Настройки игры», страница «Отображение имён» — «Цифры урона» (шаг ассета asda_ui_option_dmgtext_row)
		I2_Cursor = 51, I3_Camera,   // ★ 19.10.2026: страница «Разное» — чувствительность мыши и поворота камеры (шаг ассета asda_ui_option_mouse_rows)
		I4_Crouch = 53,              // ★ 25.10.2026: страница «Разное» — «Присед: Нажатие/Зажатие» (шаг ассета asda_ui_option_crouch_row)
	};
	// «Наши» idnum для общих таблиц чекбоксов и полос: 71000 + ключ. В .JUI окна таких номеров нет.
	static constexpr int32 kIdBase = 71000;
	enum EKind : int32 { Combo, Check, Slider, Button, Note };
	struct FRowDef { int32 Key; const TCHAR* Tag; EKind Kind; int32 Tab; int32 Page; int32 LabelLoc; };
	static const FRowDef kRows[] = {
		{ S1_Mode,    TEXT("S1"), Combo,  1, 1, SysOptLoc::kWinMode },
		{ S2_Scale,   TEXT("S2"), Combo,  1, 1, SysOptLoc::kRenderScale },
		{ S3_Fps,     TEXT("S3"), Combo,  1, 1, SysOptLoc::kFpsLimit },
		{ S4_VSync,   TEXT("S4"), Check,  1, 1, SysOptLoc::kVSync },
		{ S5_Api,     TEXT("S5"), Combo,  1, 1, SysOptLoc::kGfxApi },
		{ S6_Note,    TEXT("S6"), Note,   1, 1, SysOptLoc::kApiNote },
		{ S7_Lang,    TEXT("S7"), Combo,  1, 1, SysOptLoc::kLang },       // ★ 09.10.2026: шаг ассета asda_ui_option_lang_row
		{ S8_LangNote,TEXT("S8"), Note,   1, 1, SysOptLoc::kLangNote },
		{ Q0_Preset,  TEXT("Q0"), Combo,  1, 3, SysOptLoc::kPreset },
		{ QA_Auto,    TEXT("QA"), Button, 1, 3, SysOptLoc::kAutoDetect },
		{ Q1_Tex,     TEXT("Q1"), Combo,  1, 3, SysOptLoc::kGrpTex },
		{ Q2_Shadow,  TEXT("Q2"), Combo,  1, 3, SysOptLoc::kGrpShadow },
		{ Q3_Fx,      TEXT("Q3"), Combo,  1, 3, SysOptLoc::kPageEffects },
		{ Q4_PP,      TEXT("Q4"), Combo,  1, 3, SysOptLoc::kGrpPP },
		// ★ 12.10.2026: Q5 «Растительность», Q6 «Детализация вдали», Q8 «Глобальное освещение» — сняты (см. шапку блока).
		{ Q7_Refl,    TEXT("Q7"), Combo,  1, 3, SysOptLoc::kGrpRefl },
		{ Q9_Draw,    TEXT("Q9"), Slider, 1, 3, SysOptLoc::kDrawDistance },
		{ E1_AA,      TEXT("E1"), Combo,  1, 4, SysOptLoc::kAAMethod },
		{ E2_Sharpen, TEXT("E2"), Combo,  1, 4, SysOptLoc::kSharpen },
		// ★ 12.10.2026: E3 «Анизотропия», E4 «Затенение окружения», E6 «Блики объектива» — сняты (см. шапку блока).
		{ E5_MB,      TEXT("E5"), Check,  1, 4, SysOptLoc::kMotionBlur },
		{ E7_Clouds,  TEXT("E7"), Check,  1, 4, SysOptLoc::kClouds },
		// ★ 18.10.2026: вкладка 0 «Настройки игры», под-страница 1 «Отображение имён» (70250). Клон строки «Свечение» 3D в ассете.
		{ I1_DmgText, TEXT("I1"), Combo,  0, 1, SysOptLoc::kDmgText },
		// ★ 19.10.2026: вкладка 0 «Настройки игры», под-страница 2 «Разное» (70350). Клоны полосы «Яркость» в ассете.
		{ I2_Cursor,  TEXT("I2"), Slider, 0, 2, SysOptLoc::kCursorSpeed },
		{ I3_Camera,  TEXT("I3"), Slider, 0, 2, SysOptLoc::kCameraTurn },
		// ★ 25.10.2026: та же страница «Разное» — клон строки «Свечение» 3D (список слева, подпись справа), под I3.
		{ I4_Crouch,  TEXT("I4"), Combo,  0, 2, SysOptLoc::kCrouchMode },
	};
	// ★ 12.10.2026: теги строк, СНЯТЫХ из окна. Узлов `SYSTEM_OX_<тег>_*` в ассете быть не должно — это проверяют сводка
	//   `asda.SysOpt.Dump` и самотест `asda.SysOpt.SelfTest` (их наличие = шаг ассета `asda_ui_option_extra_rows` не прогнан).
	static const TCHAR* const kRemovedTags[] = { TEXT("Q5"), TEXT("Q6"), TEXT("Q8"), TEXT("E3"), TEXT("E4"), TEXT("E6") };
	// Суффиксы имён узлов строки, по которым ищется «остаток» снятой строки (корень, подпись, список, флажок).
	static const TCHAR* const kRowSuffixes[] = { TEXT("grp"), TEXT("TXT"), TEXT("CMB"), TEXT("CHK") };
	static FString NodeName(const TCHAR* Tag, const TCHAR* Suffix) { return FString::Printf(TEXT("SYSTEM_OX_%s_%s"), Tag, Suffix); }

	// Имя узла, по которому строка считается «есть в ассете»: список — `_CMB`, флажок — `_CHK`, полоса — `_grp`,
	//   кнопка — `_BTN`, примечание — `_TXT` (у примечания корня-группы нет).
	static FString KeyNode(const FRowDef& R)
	{
		switch (R.Kind)
		{
		case Combo:  return NodeName(R.Tag, TEXT("CMB"));
		case Check:  return NodeName(R.Tag, TEXT("CHK"));
		case Slider: return NodeName(R.Tag, TEXT("grp"));
		case Button: return NodeName(R.Tag, TEXT("BTN"));
		default:     return NodeName(R.Tag, TEXT("TXT"));
		}
	}
}

// ★ 12.10.2026: аудит состава строк фазы 2 в ассете окна (для сводки `asda.SysOpt.Dump` и самотеста).
//   OutMissing — строк из `kRows`, чьего ключевого узла в ассете нет; OutStale — снятых строк (`kRemovedTags`), чей узел
//   в ассете ещё остался. Строка результата — для лога.
static FString SysOptRowsAudit(UUserWidget* W, int32& OutMissing, int32& OutStale)
{
	OutMissing = 0; OutStale = 0;
	FString Have, Miss, Stale;
	if (!W) { return TEXT("виджета нет"); }
	for (const SysOptExtra::FRowDef& R : SysOptExtra::kRows)
	{
		const bool bHas = W->GetWidgetFromName(FName(*SysOptExtra::KeyNode(R))) != nullptr;
		(bHas ? Have : Miss) += FString::Printf(TEXT("%s "), R.Tag);
		if (!bHas) { ++OutMissing; }
	}
	for (const TCHAR* Tag : SysOptExtra::kRemovedTags)
	{
		for (const TCHAR* Suf : SysOptExtra::kRowSuffixes)
		{
			if (W->GetWidgetFromName(FName(*SysOptExtra::NodeName(Tag, Suf))))
			{
				Stale += FString::Printf(TEXT("%s_%s "), Tag, Suf);
				++OutStale;
			}
		}
	}
	return FString::Printf(TEXT("строк %d: есть [%s]%s%s"), (int32)UE_ARRAY_COUNT(SysOptExtra::kRows), *Have.TrimEnd(),
		Miss.IsEmpty() ? TEXT("") : *FString::Printf(TEXT(", НЕТ [%s]"), *Miss.TrimEnd()),
		Stale.IsEmpty() ? TEXT(", снятых 12.10.2026 в ассете нет") : *FString::Printf(TEXT(", ★ ОСТАТКИ СНЯТЫХ СТРОК [%s] — шаг ассета asda_ui_option_extra_rows не прогнан"), *Stale.TrimEnd()));
}

// ═══════════════════════════ КЛЮЧИ ОТКАТА К ПОВЕДЕНИЮ ОРИГИНАЛА ════════════════════════════════
// ★ 25.10.2026 [ОРИГИНАЛ] Блок опций персонажа с сервера (1027 → `optionsApplyServerFlags@0xED1B40`): 1 = как эталон —
//   «Интерфейс-1» №0…№6, «Интерфейс-2» №0/№1 и «Показывать шлем» берутся из БД (`Charoption`); 0 = прежний порт (только профиль).
// ★ 25.10.2026 (ревизия 3.1): ключ `asda.SoulFx.SingleSource` живёт в AsdaWorldSubsystem_Peers.cpp — здесь аксессор.
bool AsdaSoulFxSingleSourceOn();
// GSysOptWorldApply: определение — в домашней части (R2).
extern bool GSysOptWorldApply;

// Ключ CVarEscGate: определение — в домашней части (R2, без static).
#if ASDA_DEV_CONSOLE
extern TAutoConsoleVariable<int32> CVarEscGate;
#else
extern TAsdaConstCVar<int32> CVarEscGate;
#endif

// Ключ CVarAsdaKeysCaptureHeldCleanup: определение — в домашней части (R2, без static).
#if ASDA_DEV_CONSOLE
extern TAutoConsoleVariable<int32> CVarAsdaKeysCaptureHeldCleanup;
#else
extern TAsdaConstCVar<int32> CVarAsdaKeysCaptureHeldCleanup;
#endif

// Имя, под которым импортёр (и шаг клонов фазы 2) назвал планы списка: ближайший сосед со скином
//   `Bk_ComboBox` в той же панели (у контролов с пустым `ID` в .JUI планы названы по скину, `Bk_ComboBox_<n>`).
static FString SysOptPlaneIdcOf(UWidget* Combo)
{
	UPanelWidget* P = Combo ? Combo->GetParent() : nullptr;
	if (!P) { return FString(); }
	for (int32 c = 0; c < P->GetChildrenCount(); ++c)
	{
		UWidget* K = P->GetChildAt(c);
		if (!K) { continue; }
		FString NodeIdc; int32 Jui = 0, Plane = 0;
		if (AsdaUi::ParsePlaneName(K->GetName(), NodeIdc, Jui, Plane) && NodeIdc == TEXT("Bk_ComboBox"))
		{
			return FString::Printf(TEXT("Bk_ComboBox_%d"), Jui);
		}
	}
	return FString();
}
