// Copyright (c) 2026 @sitxovski (Discord)
// ★ 23.10.2026 РАЗНОС ФАЙЛА (слово владельца «огромные файлы разделить … с гарантией 100 %»): методы UAsdaWindowSubsystem
//   разнесены по группам в AsdaWindowSubsystem_{Close,Dialogs,Debug}.cpp; общее (LogAsdaWindow, чистые константы и хелперы) —
//   в AsdaWindowSubsystemInternal.h. Строки перенесены БАЙТ В БАЙТ:
//   `python Tools/asda_split_cpp.py verify --plan Reports/split_2026-10-23/AsdaWindowSubsystem.plan.json`.
//   Здесь осталось ядро: жизненный цикл, синяя обводка фокуса, подъём окон и «всегда сверху», раскладка окон, удержание в экране, зоны, слой «поверх всего», перетаскивание, синтетический клик, колесо, открытие окон NPC (их ключи читает подъём окон).
#include "UI/AsdaWindowSubsystem.h"
#include "UI/AsdaUiVisibilitySubsystem.h"   // ★ 21.10.2026: Alt+Z — флаг «интерфейс скрыт» (владелец)
#include "AsdaDevConsole.h"   // ★ античит А2: ASDA_DEV_CVAR / ASDA_DEV_CONSOLE — дев-ключи и команды вне Shipping
#include "Text/AsdaLang.h"   // ★ N9 (дедуп №2, В2-A): FAsdaLang::MissingTag — единая заглушка «[#id]»
#include "UI/AsdaLocIds.h"   // Дд2/Дд3: нотисы кнопок ОБЩИХ окон (ONEBUT/TWOBUT/SHOUTING) — одно место
#include "Resources/AsdaAssetPaths.h"   // ★ волна 2 п. 7 (14.09.2026): реестр путей и правил имён ассетов (генерируемый)
#include "Engine/GameInstance.h"
#include "Engine/World.h"
#include "TimerManager.h"
#include "GameFramework/PlayerController.h"
#include "Blueprint/WidgetBlueprintLibrary.h"
#include "Components/TextBlock.h"
#include "Components/Button.h"
#include "Components/EditableText.h"   // поле ввода 1583 диалога SHOUTING_WIN
#include "Components/CheckBox.h"         // ★ приёмка: галки под ручкой (INVEN_NEW_WIN 83503)
#include "Components/EditableTextBox.h"  // ★ приёмка: поля ввода под ручкой
#include "Components/MultiLineEditableText.h"  // ★ приёмка: многострочный ввод
#include "Components/ComboBoxString.h"   // ★ приёмка: выпадающие списки
#include "Components/Slider.h"           // ★ приёмка: ползунки тянут мышь сами
#include "Components/ScrollBox.h"        // ★ приёмка: прокрутка тянет мышь сама
#include "Components/Border.h"
#include "Components/ProgressBar.h"
#include "Blueprint/WidgetTree.h"
#include "Blueprint/WidgetLayoutLibrary.h"   // DPI-скейл вьюпорта: абсолютная дельта курсора → юниты канвы
#include "Components/CanvasPanel.h"
#include "Components/CanvasPanelSlot.h"
#include "Components/PanelWidget.h"
#include "Components/Image.h"
#include "Engine/Engine.h"
#include "Engine/GameViewportClient.h"
#include "Framework/Application/SlateApplication.h"
#if !UE_BUILD_SHIPPING   // ★ 31.10.2026: журнал синтетического клика — дев-диагностика, в Shipping не входит
#include "Widgets/Input/SButton.h"         // ★ 31.10.2026: состояние кнопки под синтетическим кликом (журнал `[симклик]`)
#include "Types/ReflectionMetadata.h"   // ★ 31.10.2026: имя виджета под синтетическим кликом (журнал `[симклик]`)
#endif
#include "Fonts/FontMeasure.h"   // метрики строки для asda.UI.DumpTextMetrics   // фокус на игровой вьюпорт при базовом GameAndUI
#include "Misc/ConfigCacheIni.h"   // раскладка окон между сессиями: секция [AsdaWindowLayout] в ini игры
#include "Login/AsdaLoginData.h"   // FAsdaLoginData::Loc — подписи кнопок ТОЛЬКО из таблицы DT_GameText
// игровые loc-подписи (2288 «ОК» / 2289 «Отмена») для TWOBUT_MSGBOX   ★ R3: комментарий снятого включения "Text/AsdaTextSubsystem.h" (файл им не пользовался) — сохранён
#include "UI/AsdaUiHelpers.h"         // D1b: общая `AsdaUi::WidgetAbsRect` (хит-тест окна и рамки)
// D2: фолбэк однокнопочного msgbox, когда WBP не загрузился   ★ R3: комментарий снятого включения "UI/AsdaMessageSubsystem.h" (файл им не пользовался) — сохранён
#include "UI/AsdaHotbarSubsystem.h"      // шаг 13 `uiCloseAllWindows`: блокировка/разблокировка хотбара
#include "UI/AsdaSystemMenuSubsystem.h"  // шаги 23-24: сворачивание трея и запрет его кнопок
#include "Resources/AsdaSoundSubsystem.h" // звуки открытия/закрытия окна количества (5 / 6)
#include "UI/AsdaItemTooltipSubsystem.h"  // ★ 27.09.2026: панель 1530 для `DropTopMsgBoxBeforeNotice`
#include "UI/AsdaPurchaseBoxSubsystem.h"  // ★ 27.09.2026: панель 1559 для `DropTopMsgBoxBeforeNotice`
#include "Styling/SlateStyleRegistry.h"   // ★ гашение синей рамки фокуса: подмена кисти FocusRectangle
#include "Styling/SlateStyle.h"
#include "Styling/AppStyle.h"
#include "Brushes/SlateNoResource.h"
// ★ С-Р1: общий офлайн-гейт отладочных команд (RefuseDebugWhenOnline)   ★ R3: комментарий снятого включения "AsdaOutgoingGate.h" (файл им не пользовался) — сохранён
#include "Login/AsdaText.h"                 // FAsdaText::Loc — строки диалога `asda.UI.ConfirmPreview` (07.10.2026)
#include "Login/AsdaLoginGameMode.h"          // ★ 08.10.2026: подъём при показе не трогает экран логина
#include "World/AsdaWorldKind.h"               // ★ F7 (дедуп №2, В3): общий признак «сцена логина»
#include "GameFramework/GameModeBase.h"     // GetAuthGameMode — проверка экрана логина
#include "UnrealClient.h"                   // С-1 (07.10.2026): FViewport::ViewportResizedEvent — смена размера вьюпорта
#include "Blueprint/GameViewportSubsystem.h" // М-1 (07.10.2026): слот окна во вьюпорте — полноэкранный ли он
#include "Widgets/SOverlay.h"               // ★ 08.10.2026: подъём окна БЕЗ снятия с вьюпорта (слот оверлея вьюпорта)
#include "UI/AsdaNotice.h"                  // ★ 08.10.2026 (ревизия В3 рыбалки): нотис 6835 строгого нумпада — LocRaw / ChatColored
#include "UI/AsdaChatSubsystem.h"           // ★ 08.10.2026: ColGold — цвет −11514 нотиса 6835
// ★ 09.10.2026: UAsdaRichText::ApplyFormatted — теги <FC=…> в теле окон-сообщений   ★ R3: комментарий снятого включения "UI/AsdaMarkup.h" (файл им не пользовался) — сохранён
#include "Components/RichTextBlock.h"      // ★ 09.10.2026: тело окна-сообщения после первой подмены — URichTextBlock
#include "AsdaWindowSubsystemInternal.h"   // ★ 23.10.2026 разнос: общее для частей AsdaWindowSubsystem*.cpp (LogAsdaWindow, чистые хелперы)
#include "AsdaWindowSubsystemInternal.h"   // ★ R2 10.11.2026: общее для частей AsdaWindowSubsystem*.cpp

DEFINE_LOG_CATEGORY(LogAsdaWindow);   // ★ 23.10.2026 разнос: было DEFINE_LOG_CATEGORY_STATIC; объявление — AsdaWindowSubsystemInternal.h (один экземпляр на все части)

// ★ Штатная синяя рамка фокуса UE. 0 (по умолчанию) — погашена: в оригинале её нет.
static ASDA_DEV_CVAR(int32, CVarUiFocusRect,
	TEXT("asda.UI.FocusRect"), 0,
	TEXT("Синяя обводка фокуса UE у кнопок и ячеек: 0 — погасить (как в оригинале), 1 — оставить ")
	TEXT("штатную рамку. Читается при старте."),
	ECVF_Default);

// ★ Волна 1-А (повторная ревизия 1А-5 п.3): сжатие полосы подъёма у потолка (`CompactRaiseBand`).
//   0 — прежнее поведение (окна сходятся на kPriorityZ − 1 и перестают подниматься) — только для контрольной пары самотеста.
ASDA_DEV_CVAR(int32, CVarZCompact,
	TEXT("asda.UI.ZCompact"), 1,
	TEXT("1 — у потолка обычного слоя (8799) полоса 700…8799 сжимается с сохранением порядка; 0 — без сжатия (прежнее)."),
	ECVF_Default);

// ★★★ ПОДЪЁМ ОКНА КЛИКОМ ПО ТЕЛУ ДЛЯ ВСЕХ ОБЫЧНЫХ ОКОН (решение владельца 06.10.2026; разбор — owner_1006/raise_on_body_click.md).
//   1 (по умолчанию) — правила эталона (`IsBodyRaiseEligible`): нажатие ЛКМ/ПКМ/СКМ по телу поднимает окно, кроме модальных,
//   AlwaysTop, NoSelect-корней, «неподнимаемых» (+0x24D = 0: чат/журнал, превью бустера, состав пакета) и мини-карты (+0x244);
//   фокус поля ввода — по правилу эталона. 0 — как было до 06.10: поднимаются только перетаскиваемые окна и окна с явным признаком.
ASDA_DEV_CVAR(int32, CVarRaiseOnBodyClickAll,
	TEXT("asda.UI.RaiseOnBodyClickAll"), 1,
	TEXT("1 — подъём окна нажатием по телу у всех обычных окон, как в оригинале (исключения эталона); ")
	TEXT("0 — прежнее правило порта (только перетаскиваемые окна и явный SetRaiseOnBodyClick)."),
	ECVF_Default);

// ★★★ Ревизия подъёма (СРЕДН-1, 07.10.2026): полоса AlwaysTop. 1 — окна с рабочим AlwaysTop живут в [8600…8699] над обычным
//   слоем, обычный подъём — не выше 8599 (как `uiControl_AddChildOrdered`: перед первым AlwaysTop); 0 — прежняя единая полоса
//   700…8799 (AlwaysTop — неподвижные якоря).
ASDA_DEV_CVAR(int32, CVarAlwaysTopBand,
	TEXT("asda.UI.AlwaysTopBand"), 1,
	TEXT("1 — окна AlwaysTop в своей полосе 8600…8699 над обычными (как оригинал); 0 — прежняя единая полоса подъёма."),
	ECVF_Default);

// ★★★ ОТКРЫТИЕ ПОВЕРХ ВСЕХ (правило владельца 08.10.2026; разбор — `Reports/owner_1008/ui_bugs.md`, объявление
//   `IsShowRaiseEligible`). 1 — показ окна поднимает его наверх своей полосы (очередь в порядке показов, разбор в начале тика
//   Slate), окна, показанные щелчком, остаются выше окна, по которому щёлкнули; 0 — прежнее поведение: подъём только там, где
//   подсистема сама зовёт `BringWindowToFront`, тело окна, поднятое на отпускании, накрывает открытое им окно.
ASDA_DEV_CVAR(int32, CVarOpenOnTop,
	TEXT("asda.UI.OpenOnTop"), 1,
	TEXT("1 — каждое открываемое окно встаёт поверх всех текущих (правило владельца 08.10.2026); 0 — прежнее поведение порта."),
	ECVF_Default);

// ★★★ КЛЮЧ ОТКАТА ДЛЯ РУЧЕК-ДОБАВОК (`FAsdaJuiDragHandle::bAddedByPort`).
//   В `.JUI` перетаскивание объявлено ДАННЫМИ (EVS{EVT=6, RET=0x20003}), и у части окон его нет
//   вовсе — оригинал их двигать не давал. Владелец потребовал сделать такие окна перетаскиваемыми
//   (2026-08-26, военный магазин 67200 — дословно «военный магазин хочу чтобы двигался»).
//   Эти ручки помечены в мете флагом `bAddedByPort`; здесь их можно отключить целиком, не трогая
//   «родные» ручки из данных.
ASDA_DEV_CVAR(int32, CVarAddedDragHandles,
	TEXT("asda.UI.AddedDragHandles"), 1,
	TEXT("Ручки перетаскивания, которых в оригинале не было (bAddedByPort): 1 — работают (наша ")
	TEXT("добавка), 0 — выключены (как в оригинале — такие окна не двигаются)."),
	ECVF_Default);

// ═══════════ ★★★ ЕДИНСТВЕННЫЙ ДОПУСТИМЫЙ СПИСОК-ИСКЛЮЧЕНИЕ ПЕРЕТАСКИВАНИЯ ═══════════════════════
// Всё остальное решают ДАННЫЕ .JUI (есть ли у окна ручка EVS{EVT=6,RET=0x20003}); модальные `*_MSGBOX`
// исключаются сами — ручек у них нет. Здесь только прямые требования пользователя:
//   • `CHAT_NEW` (2500) — «убери возможность перетаскивать чат, только растягивание» (2026-08-05);
//   • `CHAT_NEW_SYSTEM` (2570) — тот же запрет распространён на журнал системного чата (2026-08-06).
// В .JUI ручки у обоих ЕСТЬ (2534→2500 и 2576→2570) — это осознанное отступление от оригинала.
const TSet<FName>& GDragDisabledWindows()
{
	static const TSet<FName> S = { FName(TEXT("CHAT_NEW")), FName(TEXT("CHAT_NEW_SYSTEM")) };
	return S;
}

// ── ★★★ ХИТ-ТЕСТ ОКНА 1:1 С ОРИГИНАЛОМ (`uiControlHitTest @0x12DB470`, ui_core.c:107127):
//    1) сначала РЕКУРСИЯ ПО ДЕТЯМ — дети «выше» родителя, побеждает первый попавший;
//    2) прямоугольник контрола (АЛЬФА НЕ УЧИТЫВАЕТСЯ — прозрачные пиксели окна тоже ловят клик);
//    3) скрытые контролы и вся скрытая цепочка предков не ловят;
//    4) `NoSelect="1"` — клик проходит НАСКВОЗЬ (список виджетов приходит из DA_JuiWindowMeta).
//    ★ `UCanvasPanel` пропускается всегда: контейнер-группа — НАША выдумка для UMG, в .JUI её нет,
//      и её union-габариты перекрыли бы дыры между реальными контролами.
// ★★★ 31.10.2026 [ИСПРАВЛЕНИЕ ПОРТА] (волна hud_click, жалоба «над персонажем клик не идёт в мир»): ДВА ПРАВИЛА ХИТ-ТЕСТА,
//   которых у порта не было, хотя у эталона они есть. Проверка — самотест `asda.UI.SelfTestWorldClick` (сетка кликов).
//   (а) ТЕКСТ НЕ ЛОВИТ. Эталон перебирает только ПЛАНЫ контрола (`uiControlHitTest`, ui_core.c:107169-107174: вектор `this+67`
//       → `CPlane_GetRect` → слот 39 `uiPlanePixelHitTest`; его «пиксельная» проверка `sub_12E3470` — лишь «точка в кадре текстуры»,
//       альфу не смотрит). Подпись (`CaptionText`) планом не является. У порта подпись — отдельный `UTextBlock`/`URichTextBlock`,
//       и он ловил клик своим прямоугольником: подписи шире контрола, центровка вниз, строки чата длиннее окна. План контрола
//       у порта всё равно есть — картинка или невидимая зона `<base>_HIT` импортёра (`asda_jui_generic_umg.py`, «needs_hit»).
//   (б) ОБРЕЗКА ПО КЛИПУ. Узел вне прямоугольника клипующего предка (`Clipping != Inherit`; у `UScrollBox` — всегда) не ловит:
//       так хит-тестит и Slate, и эталонный список (ловят только видимые строки). Иначе строки журнала, уехавшие прокруткой ВЫШЕ
//       окна чата, держали свои прямоугольники над миром.
//   1 (по умолчанию) — оба правила; 0 — прежнее: любой не-канвас ловит своим прямоугольником, без обрезки.
ASDA_DEV_CVAR(int32, CVarHitTestPlanesOnly,
	TEXT("asda.UI.HitTestPlanesOnly"), 1,
	TEXT("1 (по умолчанию) — хит-тест окна как эталон uiControlHitTest: текст (TextBlock/RichTextBlock) клик не ловит, узлы вне ")
	TEXT("клипующего предка (ScrollBox, Clipping≠Inherit) не ловят; 0 — прежнее: любой узел ловит своим прямоугольником ")
	TEXT("(подписи шире контрола и строки чата, уехавшие за окно, глотали клик в мир)."),
	ECVF_Default);

// ★★★ 31.10.2026 [ИСПРАВЛЕНИЕ] (волна hud_click, требование владельца «клик по любой видимой точке окна ловится окном, а не
//   уходит в мир или в окно ниже»): ВИДИМОЕ ТЕЛО ОКНА ЛОВИТ КЛИК. Сетка по каталогу окон (`asda.UI.SelfTestClickThrough cat`,
//   226 окон) нашла «сквозь» у 61: почти всё — НАРИСОВАННЫЕ планы с `NoSelect="1"` (шестерёнки над рамками инвентаря, навыков,
//   склада, лавки; подложки окна разбора `Disassembly_BG`, ящика `Package_ItemName*`, смены имени `CharNameChange_BG`), реже —
//   непустые заголовки-тексты вне планов. У эталона такие планы клик пропускают (`uiControlHitTest`: `NoSelect` — байт +0x127),
//   но игрок видит под курсором окно. Теперь нарисованный план (картинка/рамка с текстурой и ненулевой альфой) и непустой текст
//   окна ловят клик, даже если помечены `NoSelect`; невидимые NoSelect-зоны и пустые подписи по-прежнему пропускают.
//   Окна с `NoSelect` у КОРНЯ (`GNoSelectRootWindows`: нотисы, баннеры, метки цели, облачка речи, подсказки, полоса опыта) —
//   оверлеи, они остаются сквозными [ОРИГИНАЛ]. 1 (по умолчанию) — так; 0 — прежнее: NoSelect и любой текст клик пропускают.
ASDA_DEV_CVAR(int32, CVarHitTestDrawnBody,
	TEXT("asda.UI.HitTestDrawnBody"), 1,
	TEXT("1 (по умолчанию) — нарисованный план окна (картинка/рамка с текстурой и альфой) и непустой текст ловят клик, даже с ")
	TEXT("NoSelect (кроме окон-оверлеев с NoSelect у корня: нотисы, баннеры, метки, подсказки); 0 — прежнее: NoSelect и текст ")
	TEXT("пропускают клик сквозь видимое окно в мир или в окно ниже."),
	ECVF_Default);

// ★★★ 31.10.2026 [ИСПРАВЛЕНИЕ ПОРТА] (волна hud_click): ПРЯМОУГОЛЬНИК РУЧНОЙ ЗОНЫ — С ЧИСТЫМ НАКОПИТЕЛЕМ.
//   ПРИЧИНА БАГА «верх экрана над персонажем не кликается» (карта 19, 31.10): шлюз отдавал делегату зоны НЕИНИЦИАЛИЗИРОВАННЫЕ
//   локальные Mn/Mx, а делегат-накопитель (`FAsdaPopupMenu::AccumRect` — min/max к переданным) расширял мусор стека: табло 49600
//   давало (0,−1)–(885,168), карта 49900 — (0,−1)–(885,522). Те зоны починены у себя (`asda.CampWar.ZoneUnion`), но шлюз оставался
//   ловушкой для любой будущей зоны-накопителя. Теперь до вызова — пустой прямоугольник (+∞…−∞), после — проверка: числа
//   конечны и Mn ≤ Mx (пустой или вывернутый прямоугольник зоной не считается).
//   1 (по умолчанию) — так; 0 — прежнее (Mn/Mx не заданы, ответ делегата берётся как есть).
ASDA_DEV_CVAR(int32, CVarZoneRectInit,
	TEXT("asda.UI.ZoneRectInit"), 1,
	TEXT("1 (по умолчанию) — шлюз «курсор над окном» отдаёт делегату зоны пустой прямоугольник и отбрасывает пустой/вывернутый/")
	TEXT("бесконечный ответ; 0 — прежнее: неинициализированные Mn/Mx (делегат-накопитель расширял мусор стека)."),
	ECVF_Default);

void UAsdaWindowSubsystem::Initialize(FSubsystemCollectionBase& Collection)
{
	Super::Initialize(Collection);
	// ★ Тик перетаскивания. Эталон применяет дельту курсора КАЖДЫЙ КАДР, пока над ручкой держат ЛКМ
	//   (`CControl_OnLButtonDown` перевзводит событие 6 каждый кадр, ui_core.c:107263) — значит и
	//   нам нужен именно покадровый опрос, а не разовый обработчик клика.
	DragTickHandle = FTSTicker::GetCoreTicker().AddTicker(
		FTickerDelegate::CreateUObject(this, &UAsdaWindowSubsystem::TickDrag), 0.f);
	// ★ С-1 (07.10.2026): смена размера вьюпорта во время игры (`r.SetRes`, развёртывание окна) — см. `OnViewportResized`.
	ViewportResizedHandle = FViewport::ViewportResizedEvent.AddUObject(this, &UAsdaWindowSubsystem::OnViewportResized);
	// ★ М-2 (07.10.2026): разбор показанных скрытых кусков — в начале тика Slate, до отрисовки кадра показа.
	if (FSlateApplication::IsInitialized())
	{
		SlatePreTickHandle = FSlateApplication::Get().OnPreTick().AddUObject(this, &UAsdaWindowSubsystem::OnSlatePreTick);
	}

	DisableGlobalFocusRectangle();
	RegisterTopPressGate();   // ★ 06.11.2026: нажатие получает только верхнее окно (`asda.UI.TopWindowPress`)
	// ★ 22.10.2026: окна, которые эталон держит при смене карты, запоминаются перед травелом (`CaptureTravelKeptWindows`).
	PreLoadMapHandle = FCoreUObjectDelegates::PreLoadMap.AddUObject(this, &UAsdaWindowSubsystem::OnPreLoadMapForTravelKeep);

	// ══ ★★★ РОСПИСИ ESC ОКОН, ВЛАДЕЛЕЦ КОТОРЫХ — САМА ЭТА ПОДСИСТЕМА (пункт С10-б/1) ══════════
	//   Поведение каждого окна ДОКАЗАНО по эталону — разбор цепочки и ветки события 21 в шапке
	//   `CloseTopmostOnEsc` в заголовке. Закрытие идёт ШТАТНЫМ путём окна (тот же метод, что
	//   зовёт реле его кнопки «Отмена»), а не снятием виджета: иначе у подсистемы остался бы
	//   висеть колбэк ожидающего ответа вызывающего.
	//
	//   1500 `ONEBUT_MSGBOX` — ESC СЪЕДАЕТСЯ, но окно НЕ закрывается (`if (a3 != 1500)` в
	//        `uiItemUpgradeWnd_OnCommand@0xD79260`). Единственный выход — кнопка `ONEBUT_BUT00`.
	RegisterEscSwallower(FName(TEXT("ONEBUT_MSGBOX")));
	//   1520 `TWOBUT_MSGBOX` — ESC = кнопка `TWOBUT_BUT01` «Отмена» (эталон шлёт 1524).
	//        ★ Сюда же попадают подтверждения ESC-меню «к выбору персонажа» (contentKind 3) и
	//        «выйти из игры» (contentKind 4): в ветке кнопки 1524 этих значений НЕТ, то есть
	//        ESC на них ТОЛЬКО закрывает окно. Из игры ESC не выходит — проверено по эталону.
	RegisterEscCloser(FName(TEXT("TWOBUT_MSGBOX")),
		FSimpleDelegate::CreateUObject(this, &UAsdaWindowSubsystem::EscCloseGameConfirm));
	//   1410 `NUMPAD_WIN` — ESC = отмена ввода, тот же `pfnOnCancel`, что у кнопки
	//        `NUMPAD_CLOSED` 1416 (`uiQuantityNumPadDialogProc@0xDA6300`, ветка `a2 == 1410`).
	RegisterEscCloser(FName(TEXT("NUMPAD_WIN")),
		FSimpleDelegate::CreateUObject(this, &UAsdaWindowSubsystem::CloseNumPad, /*bAccepted*/false));
}

// ═══ ★★★ СИНЯЯ ОБВОДКА ФОКУСА UE — ПОГАСИТЬ ГЛОБАЛЬНО (требование владельца 2026-08-26) ═══════
// ЧТО ЭТО. `SWidget::Paint` (SWidget.cpp:1744, ветка `PLATFORM_UI_NEEDS_FOCUS_OUTLINES`) дорисовывает
//   поверх сфокусированного виджета кисть `GetFocusBrush()`, а та берётся ОДНОЙ строкой:
//   `FAppStyle::Get().GetBrush("FocusRectangle")` (SWidget.cpp:1012-1015). В Starship-стиле это
//   синяя скруглённая рамка. В оригинале игры фокус — понятие ТОЛЬКО поля ввода, никаких обводок
//   у кнопок и ячеек нет.
// ПОЧЕМУ НЕ ХВАТАЛО ТОЧЕЧНОГО СПОСОБА. `AsdaUi::DisableButtonFocusRings` снимает фокусируемость
//   у кнопок ОКНА в момент открытия, но `SButton` читает флаг только в `Construct`, а ячейки сеток,
//   строки списков и иконки предметов создаются КОДОМ позже — за каждой такой кнопкой не уследить.
//   Здесь же гасится сам источник: кисть подменяется пустой, и рамку нечем рисовать НИГДЕ.
// ★ Точечный механизм ОСТАВЛЕН и продолжает работать: он нужен не ради косметики, а чтобы кнопка
//   не забирала фокус у строки чата.
// Откат: `asda.UI.FocusRect 1` (вернуть штатную рамку UE) — читается при следующем старте.
void UAsdaWindowSubsystem::DisableGlobalFocusRectangle()
{
	static bool bDone = false;
	if (bDone) { return; }
	if (CVarUiFocusRect.GetValueOnGameThread() != 0)
	{
		UE_LOG(LogAsdaWindow, Log, TEXT("[фокус] штатная рамка UE оставлена (asda.UI.FocusRect 1)"));
		bDone = true;
		return;
	}
	const ISlateStyle* Found = FSlateStyleRegistry::FindSlateStyle(FAppStyle::GetAppStyleSetName());
	FSlateStyleSet* Style = const_cast<FSlateStyleSet*>(static_cast<const FSlateStyleSet*>(Found));
	if (!Style)
	{
		UE_LOG(LogAsdaWindow, Warning,
			TEXT("[фокус] стиль «%s» не найден — синяя рамка фокуса останется"),
			*FAppStyle::GetAppStyleSetName().ToString());
		return;
	}
	Style->Set("FocusRectangle", new FSlateNoResource());
	bDone = true;
	UE_LOG(LogAsdaWindow, Log,
		TEXT("[фокус] синяя обводка фокуса UE погашена глобально (кисть FocusRectangle → пустая)"));
}

void UAsdaWindowSubsystem::Deinitialize()
{
	UnregisterTopPressGate();
	if (PreLoadMapHandle.IsValid()) { FCoreUObjectDelegates::PreLoadMap.Remove(PreLoadMapHandle); PreLoadMapHandle.Reset(); }
	if (DragTickHandle.IsValid())
	{
		FTSTicker::GetCoreTicker().RemoveTicker(DragTickHandle);
		DragTickHandle.Reset();
	}
	if (ViewportResizedHandle.IsValid())
	{
		FViewport::ViewportResizedEvent.Remove(ViewportResizedHandle);
		ViewportResizedHandle.Reset();
	}
	if (SlatePreTickHandle.IsValid())
	{
		if (FSlateApplication::IsInitialized()) { FSlateApplication::Get().OnPreTick().Remove(SlatePreTickHandle); }
		SlatePreTickHandle.Reset();
	}
	FlushWindowLayout();   // раскладка окон — на диск перед выходом (последний рубеж, обычно уже записана)
	TopmostClaims.Reset();
	UiZones.Reset();
	MsgBoxQueue.Reset();   // очередь окон-сообщений: выход из игры — без возврата записи после CloseAll
	CloseAll();
	Super::Deinitialize();
}

// ★★★ К-3: СБРОС СОСТОЯНИЯ СЕССИИ (шапка `AsdaSessionReset.h`).
//   ⚠ САМ РЕЕСТР ОКОН ЗДЕСЬ НЕ ТРОГАЕМ: он уже самоочищается — вход в мир зовёт
//   `UAsdaWorldSubsystem::RevealWorld`, а тот `CloseAll()` (порт `uiCloseAllWindows@0xD61960`).
//   Второй снос ломал бы этот порядок. Чистим ровно то, чего `CloseAll` не касается.
void UAsdaWindowSubsystem::ResetSessionState(EAsdaSessionResetReason Reason)
{
	// Модалка подтверждения: без сброса её `Yes/No` остались бы висеть, а базовый режим ввода —
	//   UIOnly (`RefreshModalInput`), то есть игрок не смог бы ни ходить, ни закрыть окно.
	//   ⚠ Колбэк ОБНУЛЯЕМ ДО закрытия: он мог бы отправить пакет в мёртвый сокет
	//   (закон `no-reentrant-net-callbacks`), а «ответа» игрок не давал.
	ConfirmCallback = nullptr;
	if (IsOpen(ConfirmWindowId)) { CloseConfirmImpl(false, /*bAnswer*/false); }   // W4-1: без звуков (не ответ игрока)
	// ★★★ Очередь окон-сообщений (26.09): полный сброс снимает её целиком молча и без колбэков — у эталона выход в
	//   выбор персонажа выгребает стек (TWOBUT id 4, ui_core.c:57195-57203), а разрыв закрывает клиент. Смена карты
	//   очередь сохраняет (записи вернутся после `CloseAll` входа в мир, как у эталона после загрузки).
	if (AsdaIsFullSessionReset(Reason)) { ClearMsgBoxQueue(AsdaSessionResetReasonName(Reason)); }

	// Заявки на верхний план принадлежат окнам, которые уже снесены травелом.
	TopmostClaims.Reset();
	PendingRaiseWindowId = NAME_None;
	PendingClamp.Reset();

	// «Все игровые окна спрятаны» — состояние гачи/диалога NPC прошлой сессии.
	bPlayerWindowsHidden = false;

	// Снимок открытых окон (`uiCaptureOpenWindowsState`) — окна прошлой сцены: восстанавливать их после
	//   травела/разрыва нельзя (окна уже снесены), поэтому флаги снимаются БЕЗ открытия.
	//   ★ Защита порта (ревизия В3 МЕЛК-2): у эталона флаги обнуляют только конструктор и восстановление —
	//   при `asda.UI.WindowSnapshotGuards 0` флаги переживают сброс, как в оригинале.
	if (AreSnapshotGuardsEnabled()) { ClearWindowSnapshot(AsdaSessionResetReasonName(Reason)); }
	// ★ 22.10.2026: окна, запомненные перед травелом, переживают ТОЛЬКО смену карты (их вернёт `RevealWorld`).
	if (AsdaIsFullSessionReset(Reason)) { FMemory::Memzero(TravelKept, sizeof(TravelKept)); bMapLoadMsgBoxesClosed = false; }

	// Заказанный программный клик (диагностика) — к новому миру отношения не имеет.
	bSimClickArmed = false; bSimClickDown = false;
	SimClickHold = 0; SimClickFramesLeft = 0;
	Mouse.Reset();

	if (AsdaIsFullSessionReset(Reason))
	{
		FlushWindowLayout();   // раскладка окон персонажа — на диск, пока она ещё его
		UE_LOG(LogAsdaWindow, Log, TEXT("[окна] сброс сессии (%s): модалка и заявки верхнего плана сняты"),
			AsdaSessionResetReasonName(Reason));
	}
}

const UAsdaJuiWindowMetaAsset* UAsdaWindowSubsystem::GetJuiMeta() const
{
	if (!JuiMeta && !bJuiMetaLoadTried)
	{
		bJuiMetaLoadTried = true;
		UAsdaWindowSubsystem* Self = const_cast<UAsdaWindowSubsystem*>(this);
		Self->JuiMeta = LoadObject<UAsdaJuiWindowMetaAsset>(nullptr, UAsdaJuiWindowMetaAsset::kAssetPath);
		if (!Self->JuiMeta)
		{
			UE_LOG(LogAsdaWindow, Warning,
				TEXT("[JUI-мета] ассет «%s» не найден — окна будут без ручек перетаскивания и без вырезов NoSelect. ")
				TEXT("Перегенерируй UI: Tools\\run_headless.bat Tools\\asda_jui_generic_umg.py"),
				UAsdaJuiWindowMetaAsset::kAssetPath);
		}
		else
		{
			UE_LOG(LogAsdaWindow, Log, TEXT("[JUI-мета] загружена: окон %d"), Self->JuiMeta->Windows.Num());
		}
	}
	return JuiMeta;
}
