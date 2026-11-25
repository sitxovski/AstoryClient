// Copyright (c) 2026 @sitxovski (Discord)
// UAsdaWindowSubsystem — единый менеджер UI-ОКОН (по образу оригинала CWindowMgr, ui_core.c).
//   Оригинал (ANIWIN_CONDWIN_CLASSIFY.md §3): CWindowMgr — реестр контролов по числовому id + JUI-загрузчик +
//   z-order-рендер + show/hide. Ключевые: CWindowMgr_FindControlByID@0x12B0130, LoadJUI_ParseNode@0x12B07D0,
//   WindowMgr_RenderControls@0x97D8F0, CControl_SetVisible, CClass_MapInsert (регистрация окна).
//   Порт: окна собирались ВРУЧНУЮ в каждом GameMode (AddLoginSceneOverlays хардкодил LoadClass+CreateWidget+
//   AddToViewport). Эта подсистема — ЕДИНЫЙ реестр «открой/найди/покажи/закрой окно по id», z-order, замена
//   ручной сборки. GameInstanceSubsystem — UI живёт в рантайме. Правило classify:B (система, много экранов).
#pragma once

#include "CoreMinimal.h"
#include "Containers/Ticker.h"
#include "Subsystems/GameInstanceSubsystem.h"
#include "Blueprint/UserWidget.h"
#include "UI/AsdaJuiWindowMeta.h"
#include "UI/AsdaMsgCategory.h"   // ★ R1: EAsdaMsgCategory — категория события (без класса подсистемы сообщений)
#include "UI/AsdaUiInputTypes.h"   // ★ R1: FAsdaMouseFrame / FAsdaDragArm / FAsdaDragGhost … — поля ввода (без всего AsdaUiHelpers.h)
#include "Components/SlateWrapperTypes.h"   // ESlateVisibility
#include "Styling/SlateTypes.h"          // ★ 09.10.2026: FTextBlockStyle — снятый стиль тела окна-сообщения
#include "AsdaSessionReset.h"   // IAsdaSessionResettable — сброс состояния сессии
#include "AsdaWindowSubsystem.generated.h"

class FViewport;   // С-1 (07.10.2026): колбэк `FViewport::ViewportResizedEvent` — смена размера вьюпорта
class IInputProcessor;   // ★ 06.11.2026: препроцессор нажатий Slate (`asda.UI.TopWindowPress`)

// ★ [ОРИГИНАЛ] 08.10.2026 (пакет 2): поправка «целые пиксели» одного слота корневого канваса окна (`SnapPercentAnchors`).
//   Не USTRUCT: слабый указатель на слот и два вектора, рефлексия не нужна.
struct FAsdaPctSnapItem
{
	TWeakObjectPtr<class UCanvasPanelSlot> Slot;   // слот, которому добавлена поправка
	FVector2D Anchor = FVector2D::ZeroVector;      // анкор слота на момент поправки (сменился — поправку не снимаем)
	FVector2D Delta = FVector2D::ZeroVector;       // добавленная к смещению поправка, юниты канваса
};

// Запись реестра окна (виджет + z-order + модальность). Аналог слота CWindowMgr (+292 = модальный флаг).
USTRUCT()
struct FAsdaWindowEntry
{
	GENERATED_BODY()

	UPROPERTY()
	TObjectPtr<UUserWidget> Widget = nullptr;

	UPROPERTY()
	int32 ZOrder = 0;

	// Модальное окно (эталон флаг +292): пока открыто и видимо — блокирует ввод/фокус к нижним окнам.
	UPROPERTY()
	bool bModal = false;

	// ══ ★★★ ДАННЫЕ JUI, ПОДТЯНУТЫЕ ИЗ DA_JuiWindowMeta при регистрации окна (см. AsdaJuiWindowMeta.h) ══
	// Ручки перетаскивания окна. ПУСТО = окно не перетаскивается, и это НЕ забывчивость, а данные:
	//   у модальных `*_MSGBOX` ручки в .JUI нет — требование юзера «кроме модальных» выполняется само.
	UPROPERTY()
	TArray<FAsdaJuiDragHandle> DragHandles;

	// Виджеты контролов с `NoSelect="1"` — хит-тест окна их ПРОПУСКАЕТ (клик проходит насквозь).
	UPROPERTY()
	TSet<FName> NoHitWidgets;

	// `AlwaysTop="1"` у корня — окно не поднимать при взаимодействии (живёт в верхнем слое).
	UPROPERTY()
	bool bAlwaysTop = false;

	// Накопленный сдвиг окна перетаскиванием, в слейт-юнитах (диагностика/дампы).
	UPROPERTY()
	FVector2D DragOffset = FVector2D::ZeroVector;

	// Мета найдена (иначе окна нет в .JUI — напр. служебные/самодельные оверлеи).
	UPROPERTY()
	bool bHasMeta = false;

	// ★ Волна 1-А (ревизия §8, 06.10.2026): поднимать окно кликом по ТЕЛУ, хотя ручек перетаскивания у него нет.
	//   [ОРИГИНАЛ] `uiWindowMgrDispatchMouse@0x12AF540` поднимает ЛЮБОЕ окно, поймавшее мышь; порт по умолчанию — только
	//   перетаскиваемые (z HUD и чата ведут их подсистемы). Окна-спутники, которые эталон показывает НЕ поднятыми и которые
	//   может накрыть чат (9100), включают этот признак сами (`SetRaiseOnBodyClick`).
	UPROPERTY()
	bool bRaiseOnBodyClick = false;

	// ★★★ РАБОЧИЙ признак AlwaysTop (ревизия подъёма, КРИТ-1, 07.10.2026). У эталона байт `+0x229` меняется во время работы:
	//   `set_o229@0xB6E310` — 20 вызовов (QUEST_WIN снимает при сборке журнала, BOOSTER_SHOP_WIN/EXTRACT_WIN/… ставят при
	//   сборке/показе). Начальное значение — мета `.JUI` (`bAlwaysTop`), поверх — таблица эталона и `SetWindowAlwaysTop`.
	//   Именно его читают подъём, полоса AlwaysTop и сжатие z.
	UPROPERTY()
	bool bAlwaysTopRuntime = false;

	// ★★★ Мостики состояний ПОДПИСИ кнопок окна (`AsdaUi::BindButtonCaptionStates`). Держим их
	//   здесь, потому что динамический делегат Slate ссылается на приёмник СЛАБО: без `UPROPERTY`
	//   реле собрал бы GC и наведение/нажатие «отвалились» бы через случайное время после открытия.
	//   Умирают вместе с записью окна (`CloseWindow`/`CloseAll`).
	UPROPERTY()
	TArray<TObjectPtr<UObject>> CaptionRelays;

	// ★★★ ПРИМЕНЁННЫЕ К ОКНУ СДВИГИ — состояние механизма СОХРАНЕНИЯ ПОЗИЦИЙ (см. ниже, блок
	//   «РАСКЛАДКА ОКОН МЕЖДУ СЕССИЯМИ»). Ключ = имя контрола-ЦЕЛИ ручки
	//   (`FAsdaJuiDragHandle::TargetControl`); значение = сдвиг, КОТОРЫЙ СЕЙЧАС ФАКТИЧЕСКИ применён
	//   к слотам этого окна, в юнитах канваса. Нужен, чтобы восстановление было ИДЕМПОТЕНТНЫМ:
	//   применяется разница «сохранено − уже применено», а не абсолютное значение.
	//   ★ Отличается от `DragOffset`: тот — суммарный сдвиг ОКНА-ВЛАДЕЛЬЦА ручки (диагностика),
	//   а здесь сдвиги разложены ПО ЦЕЛЯМ и хранятся у ОКНА-ЦЕЛИ (у ручек бывает чужая цель —
	//   `Gear_book_movetext` двигает контрол внутри `INVEN_WIN`).
	UPROPERTY()
	TMap<FName, FVector2D> AppliedOffsets;

	// ★★ КЛЮЧИ СДВИГА, ЕЩЁ НЕ ПРОВЕРЕННЫЕ КЛАМПОМ В ЭКРАН (М-2 ревизии `widen_fixes_REVIEW.md`, 07.10.2026).
	//   Заявка на кламп ведётся ПОКЛЮЧЕВО: ключ снимается, только когда у его цели нашлись габариты
	//   (кламп выполнен или не нужен). Ключ скрытого под-окна (`OldSoulmate_Win`, `Inven_*_Win`) ждёт
	//   ПОКАЗА этого под-окна, а не снимается вместе с окном, как было при заявке на всё окно.
	//   Заполняют `ApplySavedLayout` / `CenterWindowKeepingSavedOffset`, разбирает `ClampWindowIntoViewport`.
	TSet<FName> ClampPendingKeys;
	// ★ 08.10.2026 (ревизия ui_bugs_REVIEW СРЕДН-2): корень окна правилом «за экраном» уже поставлен в центр/дефолт —
	//   под-окно со своим ключом (чат личной лавки `StreetStall_ChatBox`) тогда ставится ОТНОСИТЕЛЬНО корня: его сдвиг
	//   обнуляется (позиция из разметки внутри окна), а не центрируется само поверх окна. Ставит/читает `OffscreenAdjust`.
	bool bRootOffscreenFixed = false;

	// ★★ ПОДПИСКИ «ЖДЁМ ПОКАЗА» (М-2 ревизии `widen_fixes2_REVIEW.md`, 07.10.2026). Когда все оставшиеся ключи окна ждут
	//   ПОКАЗА скрытого куска (под-окно или само окно Collapsed/Hidden), окно уходит из покадрового `PendingClamp`, а на
	//   скрытые виджеты вешается подписка FieldNotify `Visibility` (её шлёт `UWidget::SetVisibility` при смене значения).
	//   Событие показа возвращает окно в `PendingClamp`; подписки снимаются в тике (не из колбэка), при закрытии окна
	//   и при сбросе раскладки. Не UPROPERTY: слабые указатели и ручки делегатов рефлексии не нужны.
	TArray<TPair<TWeakObjectPtr<class UWidget>, FDelegateHandle>> ClampWatch;

	// ★★★ ПОДЪЁМ ПРИ ПОКАЗЕ (правило владельца 08.10.2026: «каждое открываемое окно — поверх всех текущих»).
	//   Подписка FieldNotify `Visibility` на корень окна (`UUserWidget`): переход «скрыто → показано» ставит окно в очередь
	//   подъёма `PendingShowRaise` (порт отложенного списка `CClass_MapInsert` → `CControl_DispatchMessage`, ui_core.c:105802-105812).
	//   Не UPROPERTY: ручка делегата рефлексии не нужна; снимается в `UnwatchShow` (закрытие окна, перерегистрация).
	FDelegateHandle ShowWatch;
	// Окно было показано на момент последнего события видимости (фронт «скрыто → показано» ловится по нему).
	bool bShownForRaise = false;

	// ★ [ОРИГИНАЛ] 08.10.2026 (пакет 2): поправки «целые пиксели» слотов с процентным якорем — см. `SnapPercentAnchors`.
	TArray<FAsdaPctSnapItem> PctSnap;

	// ★ 08.10.2026 (пакет 3, МЕЛК-1 ревизии пакета 2): ПОСТАНОВЩИК окна, поставленного кодом оригинала (1530 — левый-верх 1500,
	//   1559 — то же, 1410 — у курсора / режим 6 — центр). Повторяется при смене размера вьюпорта (`ProcessViewportResize`):
	//   абсолютная постановка (анкор левый-верх экрана) сама за экраном не следует. Пусто — окно постановки не имеет.
	//   Не UPROPERTY: замыкание, рефлексия не нужна; живёт ровно столько, сколько запись окна (закрытие стирает запись).
	TFunction<void()> RePlacer;
	// ★ 08.10.2026 (пакет 5, СРЕДН-1 ревизии пакета 4): экран сменился, пока окно с постановщиком было СКРЫТО, — `RePlacer`
	//   выполнится на фронте ближайшего показа (`OnShowWatchVisibility`), а не при смене размера. Общий механизм для всех окон
	//   с постановщиком (1530 показывается без постановки — `ShowSowelStonesWindow` только меняет видимость). Снимается
	//   показом и новой постановкой (`SetWindowRePlacer`: окно, поставленное своим открытием, уже стоит под текущий экран).
	bool bRePlaceOnShow = false;

	// ★★★ ОКНО-ВЛАДЕЛЕЦ (дочернее окно всегда рисуется ВЫШЕ него).
	//   Эталон: `uiShowChildWindow @0xBA05D0` (ui_core.c:11605) при открытии палитры цветов 2700
	//   делает `CClass_MapInsert(g_pWindowMgr, this[61])` — ту же регистрацию «верхним окном»,
	//   что и `uiWindowShowHideAndRegister` для меню. То есть дочернее окно в оригинале
	//   ГАРАНТИРОВАННО поверх родителя, и это не «модальность», а отношение владения.
	//   ПУСТО = самостоятельное окно.
	UPROPERTY()
	FName OwnerWindowId;
};

// ══════ СОХРАНЁННАЯ РАСКЛАДКА ОДНОГО ОКНА (то, что лежит в ini игры между сессиями) ═══════════
//   Не USTRUCT намеренно: в структуре нет ни одной ссылки на UObject, рефлексия ей не нужна,
//   а в редакторе правится не она, а сам ini (текстовый, человекочитаемый).
struct FAsdaWindowSavedLayout
{
	// Сдвиги по ЦЕЛЯМ ручек (ключ = `FAsdaJuiDragHandle::TargetControl`), в юнитах канваса.
	TMap<FName, FVector2D> Offsets;

	// ★ Ключи, которые ПРИМЕНЯЕТ САМ ВЛАДЕЛЕЦ ОКНА, а не общий механизм (записаны через
	//   `SetSavedWindowOffset`). Так живут окна, которые двигает не `TickDrag`, а код подсистемы:
	//   справка чата 2580, окно хранителя 56000 с авто-якорем. Реестр их хранит и отдаёт, но НЕ
	//   двигает — иначе сдвиг применился бы дважды (реестром и владельцем) либо был бы затёрт
	//   формулой владельца в первом же кадре. В ini у них префикс `PosExt.` вместо `Pos.`.
	TSet<FName> ExternalKeys;

	// Размер окна в БАЗОВЫХ px — только для окон, которые вообще умеют менять размер
	//   (в порту это чат 2500 и системный журнал 2570; у остальных ресайза нет ни в оригинале,
	//   ни у нас). `bHasSize = false` — размер не сохранён, окно берёт свой собственный дефолт.
	FVector2D Size = FVector2D::ZeroVector;
	bool bHasSize = false;

	// Дополнительный булев флаг состояния раскладки, смысл которого задаёт ОКНО-ВЛАДЕЛЕЦ.
	//   Для чата 2500 это `bLayoutToggled` («низ окна уже закреплён на 750, а не 758») — без него
	//   восстановленная высота встала бы на 8 px мимо. Универсальный слот, а не «поле чата»:
	//   любое будущее окно с ресайзом может положить сюда свой одиночный признак.
	bool bSizeFlag = false;
};

// ★★★ ЗОНА ИНТЕРФЕЙСА — провайдер прямоугольника окна в АБСОЛЮТНЫХ экранных координатах.
//   Возвращает false, если окна сейчас нет/оно скрыто/геометрия ещё не посчитана.
//   Считается ПО ЖИВОЙ ГЕОМЕТРИИ (окна двигаются, растягиваются, меняют DPI), поэтому это делегат,
//   а не сохранённый rect: реестр спрашивает владельца в момент запроса.
DECLARE_DELEGATE_RetVal_TwoParams(bool, FAsdaUiZoneRect, FVector2D& /*OutMin*/, FVector2D& /*OutMax*/);

// ★★★ ОБРАБОТЧИК КОЛЕСА МЫШИ НАД ОКНОМ. Возвращает true, если колесо СЪЕДЕНО (список прокручен).
//   Notches — число «щелчков»: +1 = от себя (вверх по списку), −1 = на себя (вниз).
DECLARE_DELEGATE_RetVal_OneParam(bool, FAsdaUiWheelHandler, int32 /*Notches*/);
// Закрыватель окна NPC: зовётся, когда игрок тронулся с места (порт `UI_resetAllWindowsAndLeave_5045`).
DECLARE_DELEGATE(FAsdaNpcWindowCloser);

// ══════ ★★★ ОЧЕРЕДЬ ОКОН-СООБЩЕНИЙ (порт стека `CMsgBoxMgr+0x3340`, 26.09) — константы ══════
namespace AsdaMsgBox
{
	// Ключ «эталонного аналога нет»: запись с ним никогда не считается дублем (окно есть только в порту).
	static constexpr int32 kNoKey = MIN_int32;
	// Ёмкость массива эталона: `recordVec_alloc@0xD7DED0(v3, 10)` в конструкторе менеджера (ui_core.c:55531).
	//   ★ Дефект ОРИГИНАЛА: `vecPushBack40@0xD7E030` границу не проверяет — 11-я запись пишет за массив.
	//   Порт не ограничивает очередь (TArray растёт), на переполнении эталонной ёмкости пишет Warning.
	static constexpr int32 kEtalonCapacity = 10;
}

// Одна запись очереди окон-сообщений — аналог 40-байтовой записи эталона
//   {+0 msgClass, +4 resolvedType, +8 std::string текст, +36 bForceShow} (`sub_D6D000`/`record40_copyAssign`).
//   Всё, что эталон восстанавливал из id в `CMsgBox_SetContent`/`OnSendItemUpgrade`, в порту лежит здесь явно:
//   подписи кнопок, колбэки вызывающего, полоса-гейт.
struct FAsdaMsgBoxEntry
{
	int32 Kind = 0;                 // эталонный msgClass: 0 = ONEBUT 1500, 1/3..8 = TWOBUT 1520
	int32 Id = -1;                  // resolvedType эталона (ключ дедупа вместе с Kind); kNoKey — без дедупа
	FString Text;                   // готовый текст тела окна
	int32 OkLocId = 2288;           // подпись ONEBUT_BUT00 / TWOBUT_BUT00
	int32 CancelLocId = 2289;       // подпись TWOBUT_BUT01
	EAsdaMsgCategory FallbackCategory = EAsdaMsgCategory::ItemRefused;   // канал ленты, если WBP не загрузился
	TFunction<void()> OnOk;         // ONEBUT: действие вызывающего на «ОК»
	TFunction<void(bool)> OnResult; // TWOBUT: ответ «Да»/«Нет»
	// ★ 06.10.2026 (волна 2-Г): GIFT_MSGBOX 1552 (виды 59/60): ответ 1554 «ОК» / 1555 «Отмена» + текст поля 1558
	//   на момент ответа (эталон `GetEditText(pGiftEdit)` в обработчике 1554, ui_core.c:57961/57974).
	TFunction<void(bool, const FString&)> OnTextResult;
	float GaugeGateSec = 0.f;       // TWOBUT: полоса-гейт кнопки согласия (порт-требование, см. ShowGameConfirm)
	// ★ 27.09.2026 (`Reports/owner_0927/friend_gate_12435.md`): TWOBUT — полоса-ТАЙМЕР БЕЗ гейта [ОРИГИНАЛ]:
	//   `uiProgressTickTimedAction@0xD78C20` (ui_core.c:58335-58379) показывает `pTwoBtnGauge` и заливает её
	//   (сейчас − старт)/30000 мс. Исход по таймеру делает ВЛАДЕЛЕЦ записи (у эталона — игровой цикл, не окно),
	//   окно только рисует заливку от его старта (при возврате записи из очереди полоса не начинается заново).
	float GaugeTimerSec = 0.f;          // длительность таймера, с (0 = полосы-таймера нет)
	double GaugeTimerStartReal = -1.0;  // FPlatformTime::Seconds() старта таймера у владельца
	int32 ZOrder = 0;              // запрошенный z (модалка всё равно берёт полосу kModalZ)
	uint32 Serial = 0;              // порядковый номер постановки — для лога и самотеста
	// ★ 25.10.2026 (Enter = кнопка согласия, ревизия `wave_1025c_review` ВАЖНО-3/4): согласие необратимо, тратит деньги/предметы
	//   или вопрос пришёл от сервера — Enter эту запись НЕ подтверждает, только щелчок. Ставит вызывающий: `AsdaEnterGuard::NoEnter(WM)`.
	bool bNoEnter = false;
	// Запись двухкнопочного окна 1520 (виды 1, 3…8 — ветви `CGameUI_CloseWindow`). ★ 06.10.2026 (волна 2-Г): прежде
	//   `Kind != 0` — с приходом видов 59/60 (GIFT_MSGBOX 1552) такое правило отдало бы их окну 1520.
	bool IsTwoBut() const { return Kind == 1 || (Kind >= 3 && Kind <= 8); }
	// Запись окна ввода имени GIFT_MSGBOX 1552 (виды 59 «призыв» / 60 «перемещение», волна 2-Г).
	bool IsGift() const { return Kind == 59 || Kind == 60; }
};

UCLASS()
class ASDAREBORN_API UAsdaWindowSubsystem : public UGameInstanceSubsystem,
	public IAsdaSessionResettable
{
	GENERATED_BODY()

public:
	virtual void Initialize(FSubsystemCollectionBase& Collection) override;
	virtual void Deinitialize() override;

	// ── IAsdaSessionResettable (К-3) ──
	// Реестр окон уже самоочищается: вход в мир зовёт RevealWorld → CloseAll.
	// Но заявки на верхний план, модалка подтверждения и её таймеры CloseAll не трогает —
	// оставшаяся модалка держала бы ввод в режиме UIOnly вечно.
	virtual void ResetSessionState(EAsdaSessionResetReason Reason) override;

	// ═══════ ★★★ ПЕРЕТАСКИВАНИЕ ОКОН ЗА РУЧКУ + ХИТ-ТЕСТ ОКНА (данные .JUI, одна точка на всё) ═══════
	// РЕВЕРС: `Reports/ui_window_drag_and_hittest.md`. Кратко:
	//   • ручка объявлена В ДАННЫХ: `EVS{EVT=6, RET=0x20003, CtrlID=<IDNum цели>}`; движок каждый кадр,
	//     пока над ручкой держат ЛКМ, двигает ЦЕЛЬ на дельту курсора (`uiControl_RepositionByDelta
	//     @0x12F1C30`, ui_core.c:113990). ★ Клампа по экрану в оригинале НЕТ — не добавляем.
	//   • сдвиг = ДЕЛЬТА курсора (не «курсор − точка захвата»), поэтому пропущенный кадр ничего не рвёт.
	//   • подъём окна наверх — `uiWindowMgrDispatchMouse @0x12AF540`: первый корень, обработавший мышь,
	//     переставляется в конец списка, КРОМЕ окон с `AlwaysTop`.
	// Списка перетаскиваемых окон в коде НЕТ и быть не должно: всё берётся из `DA_JuiWindowMeta`,
	//   который генерирует импортёр JUI. Единственные исключения — GDragDisabled в .cpp (чат 2500/2570
	//   по прямому требованию юзера «убери перетаскивание чата, только растягивание»).

	// Курсор над телом КОНКРЕТНОГО окна (прямоугольники его видимых контролов минус NoSelect)?
	bool IsCursorOverWindow(FName WindowId) const;

	// Поднять окно наверх обычного слоя (аналог listMoveToEnd/CClass_MapInsert). AlwaysTop не трогает.
	//   ★ Дочерние окна (см. SetWindowOwner) поднимаются ВМЕСТЕ с родителем и остаются НАД ним.
	UFUNCTION(BlueprintCallable, Category="AsdaWindow")
	void BringWindowToFront(FName WindowId);
	// ★ Волна 1-А: окно без ручек тоже поднимается кликом по телу (на отпускании, как тело перетаскиваемых окон).
	//   Признак живёт в записи окна; окна ещё нет — ничего не делает (звать после открытия).
	void SetRaiseOnBodyClick(FName WindowId, bool bRaise);
	// ★ Подъём кликом по телу ДЛЯ ВСЕХ обычных окон (решение владельца 06.10.2026, отчёт owner_1006/raise_on_body_click.md).
	//   [ОРИГИНАЛ] `uiWindowMgrDispatchMouse@0x12AF540` + `sub_12AFF80` (перестановка в списке). true — окно поднимается
	//   нажатием по телу; OutWhy — причина решения (дамп/самотест). Ключ `asda.UI.RaiseOnBodyClickAll 0` — прежнее правило
	//   порта (только ручки перетаскивания и явный `SetRaiseOnBodyClick`).
	bool IsBodyRaiseEligible(FName WindowId, FString* OutWhy = nullptr) const;
	// Окно, которое поймает нажатие в точке Pt (разбор сверху вниз по z ровно как `TickDrag`, модальный гейт учтён); None — мир.
	FName FindWindowCatchingAt(FVector2D Pt) const;
	// Точка Pt попадает в тело окна (хит-тест 1:1 с `uiControlHitTest`, NoSelect пропущены).
	bool IsWindowHitAt(FName WindowId, FVector2D Pt) const;
	// Под точкой Pt в окне — живой контрол со своим захватом Slate (кнопка, поле ввода, список…).
	bool HasCapturingControlAt(FName WindowId, FVector2D Pt) const;
	// Идентификаторы всех окон реестра (дамп `asda.UI.DumpRaiseList` и самотест `asda.UI.SelfTestRaiseClick`).
	TArray<FName> GetRegisteredWindowIds() const;
	// ★★★ Рабочий AlwaysTop (порт `set_o229@0xB6E310`). Запоминается на весь запуск (окно порта пересоздаётся при открытии,
	//   контрол эталона — нет). Открытому окну с флагом — верх полосы AlwaysTop; снятие флага — наверх обычного слоя.
	void SetWindowAlwaysTop(FName WindowId, bool bAlwaysTop);
	// Рабочий AlwaysTop окна (открытого — из записи; закрытого — таблица/мета).
	bool IsWindowAlwaysTop(FName WindowId) const;

	// ═══════ ★★★ ОТКРЫТИЕ ПОВЕРХ ВСЕХ (правило владельца 08.10.2026, отчёт `Reports/owner_1008/ui_bugs.md`) ═══════
	//   «Каждое открываемое окно открывается поверх всех текущих, каждое следующее — поверх предыдущего». Две части:
	//   1) показ окна (открытие или переход «скрыто → показано») поднимает его СРАЗУ, в том же вызове (`RaiseOnShowNow`);
	//      в очередь (`EnqueueShowRaise`) окно встаёт, только пока зажата кнопка мыши или идёт разбор очереди, — она
	//      разбирается на отпускании кнопки (`TickDrag`) и в `OnSlatePreTick`, в порядке показов (ревизия М-5 08.10.2026:
	//      прежняя фраза «очередь разбирается в начале тика Slate» устарела) — наверх СВОЕЙ полосы (`BringWindowToFront`: обычная,
	//      AlwaysTop; модалки, всплывающие 8700+, приоритетные 8800+, подсказки 9000+ живут в своих полосах и не трогаются);
	//   2) [ОРИГИНАЛ] щелчок по телу поднимает окно на НАЖАТИИ (`sub_12AFF80` из `uiWindowMgrDispatchMouse`), а окна, показанные
	//      этим щелчком, поднимаются ПОЗЖЕ (отложенный список `CClass_MapInsert`) — значит выше. Порт поднимает тело на ОТПУСКАНИИ
	//      (иначе съедается клик), поэтому всё, что показано/поднято между нажатием и отпусканием, после подъёма тела поднимается
	//      заново в том же порядке. Без этого окно, открытое кнопкой окна (демонтаж 32000 из сумки), уходило ПОД сумку.
	//   Ключ отката: `asda.UI.OpenOnTop 0` — прежнее поведение (подъём только там, где подсистема зовёт `BringWindowToFront` сама).
	// Поднимается ли окно при показе; OutWhy — причина решения (дамп/самотест).
	bool IsShowRaiseEligible(FName WindowId, FString* OutWhy = nullptr) const;
	// Разобрать очередь подъёма при показе сейчас (самотест; в игре — `OnSlatePreTick` и отпускание кнопки мыши в `TickDrag`).
	//   Пока зажата кнопка мыши или ждёт подъём тела по щелчку — ждёт (перевешивание рвёт захват Slate). Возвращает число подъёмов.
	int32 FlushShowRaise(const TCHAR* Why);
	// Длина очереди подъёма при показе (самотест).
	int32 GetPendingShowRaiseNum() const { return PendingShowRaise.Num(); }
	// [САМОТЕСТ] Взвести подъём тела по щелчку, как это делает нажатие мыши в `TickDrag` (эмуляция «нажали на окно A»).
	void DebugSetPendingBodyRaise(FName WindowId) { PendingRaiseWindowId = WindowId; }
	// Candidate — дочернее окно Root (на любой глубине, `SetWindowOwner`); для самотеста: потомки законно выше владельца.
	bool IsWindowOwnedBy(FName Candidate, FName Root) const { return IsOwnedBy(Candidate, Root); }
	// [САМОТЕСТ] Записать в раскладку (ini прогона) ОДИН сдвиг Off всем ключам перетаскивания меты .JUI (цель ручки: окно-цель +
	//   контрол-цель) — прогон «все ключи за экраном» (ревизия 07.10: 141 ключ, 5 режимов). Возвращает число ключей.
	int32 DebugFillAllLayouts(FVector2D Off);

	// ★★★ ОБЪЯВИТЬ ОКНО ДОЧЕРНИМ (эталон `uiShowChildWindow` — палитра 2700 над редактором 2590).
	//   Сразу же поднимает дочернее над владельцем и держит его там при любых подъёмах.
	//   OwnerWindowId = NAME_None снимает связь.
	UFUNCTION(BlueprintCallable, Category="AsdaWindow")
	void SetWindowOwner(FName WindowId, FName OwnerWindowId);

	/** ★★★ СИНТЕТИЧЕСКИЙ КЛИК ДЛЯ ЖИВОЙ ПРОВЕРКИ (`asda.UI.SimClick`, реализация — в .cpp).
	 *  Инжектирует НАСТОЯЩИЕ события Slate (нажатие → удержание `HoldFrames` кадров → отпускание),
	 *  поэтому ловит именно то, что ломается у жалоб «нажимать по два раза»: потерю захвата мыши
	 *  между нажатием и отпусканием. Общая точка — пользоваться могут все UI-подсистемы. */
	//  ★ `bRightButton` (24.09.2026): какой кнопкой кликать. Заведено НЕ для удобства — без него
	//    ПРАВУЮ кнопку в Slate послать было НЕЧЕМ, и любая правка ПКМ оставалась непроверяемой
	//    боевым путём (закон `diagnostic-must-use-real-path`). Ровно так и дожил до владельца
	//    дефект «ПКМ по слоту питомца не открывает панель 43058».
	void SimulateClickAt(FVector2D ScreenPos, int32 HoldFrames, bool bRightButton = false);

	/** ★★★ СИНТЕТИЧЕСКОЕ ПЕРЕТАСКИВАНИЕ (`asda.UI.SimDrag`) — расширение той же общей точки.
	 *  ЗАЧЕМ. Клик проверить синтетикой было можно, а ПЕРЕТАСКИВАНИЕ — нет: у него между нажатием
	 *  и отпусканием обязано пройти РЕАЛЬНОЕ ДВИЖЕНИЕ курсора, иначе не сработает ни порог
	 *  `FAsdaDragArm` (он считает пройденные пиксели), ни хит-тесты «что под курсором сейчас»
	 *  (`AsdaUi::IsCursorOverWidget` читает `GetCursorPos()`, а не координату из события).
	 *  Здесь курсор ведётся ПО КАДРАМ от `FromPos` к `ToPos` настоящими `ProcessMouseMoveEvent`,
	 *  поэтому путь игрока воспроизводится целиком: нажал → повёл → отпустил над целью.
	 *  ⚠ Это ЕДИНСТВЕННЫЙ способ доказать перетаскивание в headless-прогоне: мыши там нет, а
	 *  звать `BeginDrag`/`Drop*` напрямую бессмысленно — теряется ровно то, что и ломается. */
	void SimulateDrag(FVector2D FromPos, FVector2D ToPos, int32 HoldFrames, bool bRightButton = false);
	// ★ Волна 11 заточки: заказанный клик/драг ещё не доставлен (нет отпускания). Нужен самотестам, которые
	//   шлют ДВА клика подряд (двойной щелчок): второй заказ, поданный раньше отпускания первого, затёр бы его.
	bool IsSimClickPending() const { return bSimClickArmed; }
	// ★ 06.11.2026 (`asda.UI.TopWindowPress`, разбор в AsdaWindowSubsystem_TopPress.cpp): нажатие в точке `Pt` Slate отдаст контролу
	//   нижнего окна `OutLower`, хотя мышь ловит тело верхнего `OutTop`. Общая проверка гейта и самотеста.
	bool FindLowerWindowUnderTopAt(FVector2D Pt, FName& OutTop, FName& OutLower) const;
	// ★ 06.11.2026 (`asda.UI.TopWindowPoll`): виджет W лежит в окне НИЖЕ окна, которое ловит мышь в точке `Pt`. Вопрос опроса
	//   `AsdaUi::IsCursorOverWidget`: контролы, разбирающие мышь покадрово, Slate не видят и гейта нажатий не проходят.
	bool IsWidgetBelowCatcher(const UWidget* W, FVector2D Pt) const;
	// ★ 08.11.2026 (пачка 18, баг 2; ключ `asda.UI.PanelPress`) ПАНЕЛЬ ВНУТРИ ОКНА ЛОВИТ НАЖАТИЕ СВОИМ ТЕЛОМ. Немодальная панель, которую
	//   эталон держит контролом внутри окна (панель покупки 15515 бустер-магазина, панель 6333 окна 6000): эталон
	//   `uiContainerHandleMouse` отдаёт нажатие верхнему контролу под курсором, и тело панели закрывает карточки под ней. У порта
	//   планы тела `SelfHitTestInvisible` — нажатие уходило в кнопку окна под панелью, а гейт верхнего окна внутри ОДНОГО окна
	//   не работает. `PanelId` — имя контрола (.JUI, для лога), `OwnerWindowId` — окно реестра, `IsShown` — показана ли панель,
	//   `Node` — виджет панели (её прямоугольник и поддерево). То же правило — у модального контрола с узлом (`RegisterOriginalModal`):
	//   там нажатие вне узла глохнет по всему окну-владельцу (эталон `+0x1A0`).
	void RegisterPressPanel(FName PanelId, FName OwnerWindowId, TFunction<bool()> IsShown, TFunction<const UWidget*()> Node);
	void UnregisterPressPanel(FName PanelId);
	// Нажатие в точке `Pt` по Slate-виджету `Leaf` окна `LeafWindow` съедает панель или модальный узел этого же окна (общая проверка гейта,
	//   опроса и самотеста `asda.Shop.ModalProbeSelfTest`).
	bool IsPressCaughtByPanel(FName LeafWindow, const TSharedPtr<SWidget>& Leaf, FVector2D Pt) const;
	// Сколько нажатий съел гейт верхнего окна с начала игры (самотест).
	int32 GetTopPressEatenCount() const { return TopPressEatenCount; }
	// Сколько нажатий ждут своего отпускания у гейта (самотест: застрявших пометок быть не должно).
	int32 GetTopPressPendingUps() const { return TopPressEaten.Num(); }
	// Окно реестра, которому принадлежит Slate-виджет (самотест: кто на самом деле взял нажатие).
	FName DebugWindowOwningSlate(const TSharedPtr<SWidget>& W) const { return FindWindowOwningSlate(W); }

	// ★ Текстовая сводка СОСТОЯНИЙ ПОДПИСИ кнопок (консоль `asda.UI.DumpCaptionStates`).
	//   `WindowId = NAME_None` — по всем открытым окнам. Печатает и то, что положил импортёр
	//   (цвета кадров 0/1/2 + смещения), и ТЕКУЩИЙ цвет/позицию подписи — иначе 1-пиксельный
	//   сдвиг и оттенок на снимке не проверить.
	FString DebugCaptionStates(FName WindowId) const;

	// Текстовая сводка ручек перетаскивания всех открытых окон (консоль asda.UI.DumpDrag).
	FString DebugDragHandles() const;
	// ★ 10.10.2026 (modal_lock2, СРЕДН-2): живой прямоугольник ручки перетаскивания №Index окна (абсолютные координаты Slate) —
	//   цель настоящего `SimulateDrag` в самотесте замка ввода. false — окна/ручки/виджета нет или он не отрисован.
	bool DebugGetDragHandleRect(FName WindowId, int32 Index, FVector2D& OutMin, FVector2D& OutMax) const;

	// Реестр меты JUI (грузится лениво из DA_JuiWindowMeta). nullptr — ассет не сгенерирован.
	const UAsdaJuiWindowMetaAsset* GetJuiMeta() const;

	// ═══════════════════ РЕЕСТР ЗОН ИНТЕРФЕЙСА (перехват мирового клика) ════════════════════════
	// ЗАЧЕМ. Клик по миру в порту идёт НЕ через Slate, а прямым биндом
	//   `InputComponent->BindKey(EKeys::LeftMouseButton, IE_Pressed, …, OnLeftClick)`
	//   (AsdaPlayerController.cpp:70). Слой UMG остановил бы его, только если бы виджет под курсором
	//   СЪЕЛ событие, но узлы импортированных окон — `UImage`, а `SImage` не реализует
	//   `OnMouseButtonDown` и возвращает Unhandled: `Visible` делает картинку хит-таргетом для
	//   наведения, но событие НЕ поглощает. Поэтому блокировка живёт на стороне контроллера.
	// ПОЧЕМУ РЕЕСТР, А НЕ МЕТОД В КАЖДОЙ ПОДСИСТЕМЕ (решение 2026-08-04). Первый вариант —
	//   `UAsdaChatSubsystem::IsCursorOverWindow()` + персональный `if` в контроллере. Окон будет
	//   много (основной чат 2500, вкладки 2590, палитра 2700, контекстные меню 2670/80000, весь HUD),
	//   и «каждый со своим прямоугольником + своя ветка в OnLeftClick» — гарантированный источник
	//   багов: одну ветку забыли — клик проваливается в мир сквозь окно; порядок веток задаёт
	//   приоритет; отладка размазана по подсистемам. Здесь ОДНА точка правды: подсистема регистрирует
	//   зону, контроллер спрашивает `IsCursorOverAnyUiWindow()`.
	// ПРАВИЛО ДЛЯ ВЛАДЕЛЬЦА ЗОНЫ: rect считать ТОЛЬКО по узлам, которые окно и образуют (подложка,
	//   рамка, группа списка) — «объединение всех узлов» опасно, один случайно растянутый пользователем
	//   контрол раздувает зону на весь экран и блокирует игру (это уже случалось: SCROLL_DOWN до
	//   нормализации, UNUSED_REPORT/chat_system_window Д9.2).
	void RegisterUiZone(FName ZoneId, FAsdaUiZoneRect RectProvider);
	void UnregisterUiZone(FName ZoneId);

	// Курсор над ЛЮБОЙ зарегистрированной зоной? OutZone (если задан) получает id первой попавшей.
	UFUNCTION(BlueprintPure, Category="AsdaWindow")
	bool IsCursorOverAnyUiWindow() const;
	bool IsCursorOverAnyUiWindow(FName& OutZone) const;
	// ★ 21.10.2026: то же решение для произвольной экранной точки (Slate, абсолютные пиксели). Курсор читает `IsCursorOverAnyUiWindow`;
	//   самотест подаёт центр окна (при -RenderOffscreen перенос курсора ОС не работает).
	bool IsScreenPointOverAnyUiWindow(const FVector2D& ScreenPoint, FName& OutZone) const;
	// ★ 31.10.2026 (волна hud_click): то же решение, что `IsScreenPointOverAnyUiWindow` (одна реализация — та зовёт эту), плюс
	//   ИМЯ поймавшего виджета `OutWidget` (у ручной зоны шага 1 — NAME_None). Нужен сетке кликов `asda.UI.SelfTestWorldClick`
	//   и дампам: «кто именно перехватил клик в мир».
	bool IsScreenPointOverAnyUiWindowEx(const FVector2D& ScreenPoint, FName& OutZone, FName& OutWidget) const;

	// Прямоугольник конкретной зоны (для дампов/диагностики).
	bool GetUiZoneRect(FName ZoneId, FVector2D& OutMin, FVector2D& OutMax) const;

	// Текстовая сводка всех зон (для asda.Chat.DumpWidgets / asda.UI.DumpZones).
	FString DebugUiZones() const;

	// ═════════════════════ ★★★ КОЛЕСО МЫШИ НАД ИНТЕРФЕЙСОМ (2026-08-06) ═════════════════════════
	// ЭТАЛОН. `WM_MOUSEWHEEL` (game_main.c:5864) НЕ отдаёт колесо только камере: сначала оно
	//   кладётся В ОЧЕРЕДЬ ОБОИХ менеджеров окон — `CWindowMgr_QueueMouseWheel(g_pWindowMgr, wParam)`
	//   и `…(g_pGameWindowMgr, wParam)` (ui_core.c:113636, запись типа 7) — и только потом, ЕСЛИ
	//   курсор НЕ над UI (`!CControl_HitTest`), зовётся `Camera_MouseWheelZoom`. То есть у оригинала
	//   интерфейс имеет ПЕРВЫЙ приоритет на колесо, а мир — второй.
	// ПОРТ. Мир у нас уже защищён: `AAsdaPlayerController::OnWheel` выходит, если курсор над зоной
	//   интерфейса. Здесь — вторая половина: ЕДИНАЯ точка, куда окна вешают свои обработчики.
	//   Порядок опроса — по z СВЕРХУ ВНИЗ (как хит-тест), первый вернувший true забирает колесо;
	//   при открытой модалке отвечают только окна её полосы (z ≥ kModalZ).
	// ПОЧЕМУ РЕЕСТР, А НЕ `if` В КОНТРОЛЛЕРЕ — ровно та же причина, что у реестра зон выше.
	void RegisterWheelHandler(FName WindowId, FAsdaUiWheelHandler Handler);
	void UnregisterWheelHandler(FName WindowId);

	// Разослать колесо окнам. true — интерфейс колесо съел (миру оно уже не нужно).
	bool HandleMouseWheel(int32 Notches);

	// Сводка обработчиков колеса (asda.UI.DumpZones).
	FString DebugWheelHandlers() const;

	// ═══════════ ★★★ ОКНА NPC: ЗАКРЫТИЕ ПРИ УХОДЕ ИГРОКА ══════════════════════════════════════
	//
	// ЭТАЛОН. Окна, открытые разговором с NPC, закрывает ОДНА функция —
	//   `UI_resetAllWindowsAndLeave_5045@0xD995C0`. Она гасит подряд ВСЕ такие окна: нумпад,
	//   обмен предметами, **магазин бустеров** (`boosterShopCloseWindow`), окно извлечения,
	//   посредничество, слоты экипировки и т. д. Зовут её переходы состояния локального игрока —
	//   в частности «пошёл» (`ida.c:278273`, ветка начала движения) и «умер/телепорт»
	//   (`ida.c:278552`, `ida.c:224453`). То есть правило оригинала — НЕ дистанция до NPC,
	//   а САМ ФАКТ, что игрок тронулся с места.
	//
	// ПОЧЕМУ РЕЕСТР, А НЕ `if` В КОНТРОЛЛЕРЕ. Такое поведение обязано быть у КАЖДОГО окна NPC
	//   (магазин снаряжения, склад, кузнец, парикмахерская, магазин бустеров…). Если писать его
	//   в контроллере, он обрастёт цепочкой `if` по всем подсистемам, и каждое новое окно NPC
	//   придётся туда дописывать — ровно то, от чего уводят реестры зон и колеса выше.
	//   Владелец окна регистрирует свой ЗАКРЫВАТЕЛЬ (обычно `CloseWindow()` подсистемы, он
	//   идемпотентен), а контроллер зовёт `CloseNpcWindowsOnPlayerMove` одним вызовом.
	void RegisterNpcWindowCloser(FName WindowId, FAsdaNpcWindowCloser Closer);
	void UnregisterNpcWindowCloser(FName WindowId);

	// Игрок тронулся с места / умер / телепортировался — закрыть все окна NPC.
	//   Возвращает, сколько закрывателей было вызвано (для лога).
	int32 CloseNpcWindowsOnPlayerMove(const TCHAR* Why);

	// ═══════════ СЛОЙ «ПОВЕРХ ВСЕГО» — всплывающие списки, справки, контекстные меню ═══════════
	// ★★★ ЗАЧЕМ (живой тест 2026-08-05). Выпадающий список каналов чата (контрол 2608) раскрывается
	//   ВВЕРХ и геометрически залезает на окно системного журнала 2570. Список — ребёнок КОРНЕВОГО
	//   виджета чата, журнал — ДРУГОЙ корневой виджет; `UCanvasPanelSlot::SetZOrder` внутри чата
	//   на них НЕ влияет вообще: порядок между разными корневыми виджетами задаётся ТОЛЬКО
	//   z-order'ом слота `AddToViewport`. Прошлая правка ставила списку z = 9000 ВНУТРИ окна чата
	//   и поэтому не могла сработать в принципе.
	// ★ МЕХАНИЗМ (один на все всплывающие слои, чтобы не чинить это по второму разу для справки «?»
	//   и контекстных меню 2670 / 80000):
	//     • `PushTopmost(ClaimId, OwnerWindowId, RectProvider)` — поднимает КОРНЕВОЙ виджет-владелец
	//       в вьюпорте на зарезервированный z (kTopmostZ), запоминает прежний z и регистрирует
	//       ЗОНУ ИНТЕРФЕЙСА по прямоугольнику самого всплывающего слоя (иначе клик над списком
	//       уходил бы в мир — список не входит в корпус окна и в его зону);
	//     • `PopTopmost(ClaimId)` — возвращает прежний z и снимает зону;
	//     • `IsPointerBlockedByTopmost(MyWindowId)` — «курсор сейчас над ЧУЖИМ всплывающим слоем?».
	//       Любая подсистема, которая ловит мышь опросом (а таких у нас все — `UImage` клик не
	//       поглощает, грабля №2), обязана спросить это ПЕРВОЙ строкой своего TickPointer и выйти,
	//       иначе журнал перехватит клик, предназначенный списку каналов поверх него.
	void PushTopmost(FName ClaimId, FName OwnerWindowId, FAsdaUiZoneRect RectProvider);
	void PopTopmost(FName ClaimId);
	bool IsTopmostActive(FName ClaimId) const;
	// true, если курсор над активным всплывающим слоем, которым владеет ДРУГОЕ окно (не MyWindowId).
	bool IsPointerBlockedByTopmost(FName MyWindowId) const;
	// ★ ВОЛНА 9 ЗАТОЧКИ (25.09.2026): «курсор над ДРУГИМ ОБЫЧНЫМ окном, которое лежит ВЫШЕ моего?».
	//   Эталон `uiWindowMgrDispatchMouse@0x12AF540` отдаёт мышь ПЕРВОМУ корню сверху вниз, под которым
	//   курсор, — окну ниже событие не приходит вовсе. Наш покадровый опрос спрашивает только СВОИ
	//   виджеты (`AsdaUi::IsCursorOverWidget`) и видит ячейки сквозь окно поверх (пример: 20000 x 200…648
	//   и 78200 x 592…864 перекрываются полосой 56 px). Эта проверка — вторая строка опроса после
	//   `IsPointerBlockedByTopmost`. Учитываются окна реестра с z выше моего и НИЖЕ `kTopmostZ`
	//   (всплывашки — у `IsPointerBlockedByTopmost`, модалки — у `AsdaUi::IsModalDispatchBlocked`,
	//   тултипы 9099 мышь у оригинала не ловят); корень `HitTestInvisible` не считается.
	bool IsCursorOverHigherWindow(FName MyWindowId) const;
	// Сводка активных слоёв для дампов.
	FString DebugTopmost() const;

	// Зарезервированный z всплывающих слоёв: выше игрового HUD и всех окон, ниже экрана загрузки
	//   (30000, AsdaLoadManagerSubsystem) и дев-меню (5000 → перекрывать его нельзя, поэтому 9000
	//   стоит выше: дев-меню открывают поверх игры осознанно, а всплывашка живёт долю секунды).
	static constexpr int32 kTopmostZ = 9000;

	// ═════════ ★★★ ПОЛОСА «ПРИОРИТЕТНЫХ» ОКОН (требование владельца 2026-08-26) ═════════════════
	// ТРЕБОВАНИЕ (дословно): «у окон коробок/бустеров нужно приоритет поставить — самый верх над
	//   другими окнами, чтобы они не открывались позади других окон, а всегда были впереди».
	// ⚠ ЭТО НАША ДОБАВКА ПОВЕРХ ОРИГИНАЛА. В данных .JUI признака «приоритетное окно» нет:
	//   атрибут `AlwaysTop="1"` есть у 29 контролов, но он значит «не переупорядочивать подъёмом»
	//   (`BringWindowToFront` его уважает), а вовсе не «рисовать выше всех» — и стоит он у совсем
	//   других окон (`MAXMAP_WIN`, `EXPBar_WIn`, `QUEST_WIN`).
	// УСТРОЙСТВО (по образцу уже существующей полосы модалок): приоритетные окна живут в
	//   [kPriorityZ … kPriorityZ+99], а ПОТОЛОК подъёма обычных окон опущен до kPriorityZ−1.
	//   Поэтому обычное окно не может перекрыть приоритетное, сколько по нему ни кликай.
	//   Порядок слоёв снизу вверх: обычные окна < приоритетные < всплывашки (9000) <
	//   тултипы (9099) < модалки (20000) < загрузочный экран (30000).
	//   ★ Тултип и модалки НАМЕРЕННО остаются выше: подсказка о предмете внутри коробки и
	//   подтверждение покупки обязаны быть видны поверх неё.
	static constexpr int32 kPriorityZ    = 8800;
	static constexpr int32 kPriorityZMax = 8899;
	// ★ Волна 1-А (повторная ревизия, 1А-5 п.3): НИЗ полосы, которую сжимает `CompactRaiseBand`. Ниже лежат слои
	//   с собственным порядком (HUD, чат 689 / журнал 690 и т.п.) — их сжатие не трогает.
	static constexpr int32 kRaiseBandFloor = 700;
	// ★★★ Ревизия подъёма (СРЕДН-1, 07.10.2026): ПОЛОСА AlwaysTop [kAlwaysTopZ … kAlwaysTopZMax] над обычным слоем и ПОД
	//   приоритетными 8800. Эталон собирает AlwaysTop в конец списка (`mapClearMatchingEntries`, ui_core.c:105126), а подъём
	//   ставит обычное окно ПЕРЕД первым AlwaysTop (`uiControl_AddChildOrdered`) ⇒ потолок обычного подъёма — kAlwaysTopZ − 1.
	//   8700…8799 оставлены всплывающим окнам порта (меню по игроку 8700). Ключ отката — `asda.UI.AlwaysTopBand 0`.
	static constexpr int32 kAlwaysTopZ    = 8600;
	static constexpr int32 kAlwaysTopZMax = 8699;

	/** ★ Волна 1-А: СЖАТИЕ полосы подъёма [kRaiseBandFloor, kPriorityZ). `BringWindowToFront` ставит окно на «верх + 1»
	 *  с потолком kPriorityZ − 1 без перенумерации — после ~сотни подъёмов окна упирались в потолок, получали равный z
	 *  и переставали подниматься. Сжатие переносит различные значения z полосы на подряд идущие (от kRaiseBandFloor),
	 *  СОХРАНЯЯ относительный порядок и равенства; окна AlwaysTop — якоря (их z не меняется, порядок вокруг них
	 *  сохраняется); сохранённые z слоёв «поверх всего» пересчитываются той же картой. Возврат — сколько окон сдвинуто.
	 *  Ключ отката `asda.UI.ZCompact 0`. */
	int32 CompactRaiseBand(const TCHAR* Why);

	/** Перевести окно в полосу «всегда поверх обычных окон» (или вернуть обратно).
	 *  Зовётся владельцем окна сразу после открытия. Повторный вызов идемпотентен. */
	UFUNCTION(BlueprintCallable, Category="AsdaWindow")
	void SetWindowPriority(FName WindowId, bool bPriority);

	// ═════════ ★★★ ЗАРЕЗЕРВИРОВАННЫЙ СЛОЙ МОДАЛЬНЫХ ОКОН (живой тест 2026-08-06) ═════════════════
	// ДЕФЕКТ: окно «Настройки вкладки» (`CHAT_TAB_WIN`, z = 820) НАКРЫВАЛО собственный диалог
	//   подтверждения «…вернутся к настройкам по умолчанию. Продолжить?» (`TWOBUT_MSGBOX`, z = 810).
	//   Z-order модалок задавался вызывающим «на глаз» и мог оказаться НИЖЕ уже открытого окна.
	// ПОЧЕМУ НЕ ДАННЫЕ. Проверено по всем четырём .JUI: атрибут `AlwaysTop="1"` стоит у 29 контролов
	//   `gamemain` (+1 `commonwin`, +3 `login`), и это НЕ признак модальности — там `MAXMAP_WIN`
	//   (1710), `EXPBar_WIn` (310), `QUEST_WIN` (700), а вот `TWOBUT_MSGBOX` (1520), `ONEBUT_MSGBOX`
	//   (1500) и `SHOUTING_WIN` (1580) — БЕЗ него. Признака «модальное» в данных .JUI нет вообще:
	//   в оригинале модальность — РАНТАЙМНОЕ состояние менеджера (`pManager+104`, §B.4 отчёта
	//   ui_window_drag_and_hittest.md), которое ставит тот, кто окно открыл.
	// ПРАВИЛО ПОРТА (явное, вместо списка окон в коде): модальность = флаг `bModal` записи реестра,
	//   а КАЖДОЕ модальное окно живёт в зарезервированной полосе z [kModalZ … kModalZ+999], выше
	//   любых обычных окон, всплывашек (kTopmostZ 9000) и меню чата (9500), ниже экрана загрузки
	//   (30000). Запрошенный вызывающим z у модалки ИГНОРИРУЕТСЯ — иначе дефект вернётся с любым
	//   новым окном, у которого z окажется больше.
	static constexpr int32 kModalZ = 20000;

	// ══════════ ★★★ РАСКЛАДКА ОКОН МЕЖДУ СЕССИЯМИ: ini игры, ОДИН механизм на ВСЕ окна ══════════
	// ★ ОТСТУПЛЕНИЕ ОТ ОРИГИНАЛА — РЕШЕНИЕ ЮЗЕРА (2026-08-07, дословно): «реализуй сохранение.
	//   И вообще все окна пусть сохраняют в Ini игры полноценно свои позиции и оттуда каждый раз
	//   достают их». В ЭТАЛОНЕ сохранения позиций окон НЕТ НИ У ОДНОГО ОКНА: `uiControl_Reposition
	//   ByDelta @0x12F1C30` пишет только в рантайм-поля контрола (+0x128/+0x12C), а единственный
	//   файл настроек интерфейса `ChatOpt.bin` (chat.c:1397) хранит вкладки/цвета/фильтры и НИ
	//   ОДНОЙ координаты. Позиция окна хранителя 56000 в эталоне тоже не сохраняется (см.
	//   `Reports/ministate_win_port.md` §11.4). ⇒ Это ОСОЗНАННОЕ УЛУЧШЕНИЕ по закону
	//   `fix-original-defects`, НЕ «возвращать к эталону».
	//
	// ГДЕ ЛЕЖИТ. `GGameUserSettingsIni` (`Saved/Config/<Платформа>/GameUserSettings.ini`), секция
	//   `[AsdaWindowLayout]`. ★ Тот же файл, в который порт уже пишет `[AsdaLogin]`, `[AsdaVideo]`,
	//   `[AsdaKeybinds]` — второй механизм настроек не заводится. В `Content/` писать нельзя
	//   (в упакованной игре это пак, а не папка).
	//
	// ЧТО ИМЕННО ХРАНИТСЯ И ПОЧЕМУ ИМЕННО ЭТО (вопрос разрешений — закон `jui-align-anchors`).
	//   Хранится НЕ абсолютная позиция окна, а **СДВИГ ОТНОСИТЕЛЬНО РАСКЛАДКИ .JUI**, в юнитах
	//   канваса. Причина: раскладка импортёра — это АНКОРЫ (доля экрана + пиксельный офсет от
	//   своего угла), и она сама по себе правильно переезжает на любое разрешение. Если сохранить
	//   абсолютную точку, мы ПОДМЕНИМ схему анкора собственной — ровно тот класс ошибок, который
	//   закон `jui-align-anchors` запрещает (три случая подряд: заливка EXP-бара, плита
	//   `CHAT_NEW_SERV`, список журнала; расхождение до 220 px на 1440). Сдвиг же — это ровно то,
	//   что делал пользователь мышью, он складывается с анкором и схему не ломает.
	//   ★ Юниты: DPI-кривая проекта — константа 1.0 (`DefaultEngine.ini`, `UIScaleCurve`), поэтому
	//   юнит канваса = экранный пиксель; дельта курсора делится на `GetViewportScale` ровно так же,
	//   как в `TickDrag`, ⇒ сохранённое число и сдвиг слота — одна и та же величина.
	//   ★ При ЗАГРУЗКЕ сдвиг КЛАМПИТСЯ в экран (см. ниже) — иначе после смены разрешения или
	//   монитора окно оказалось бы за краем. Сам эталон клампа не делает, но там и сохранения нет.
	//
	// КАКИЕ ОКНА СОХРАНЯЮТСЯ — ПРИЗНАК ИЗ ДАННЫХ, А НЕ СПИСОК В КОДЕ.
	//   Сохраняется окно, у которого в `.JUI` ОБЪЯВЛЕНА РУЧКА ПЕРЕТАСКИВАНИЯ
	//   (`EVS{EVT=6, RET=0x20003}` ⇒ непустой `DragHandles` записи реестра) и которое не модально.
	//   Это ровно те 125 окон из 224, которые в оригинале вообще можно двигать. Всё остальное —
	//   мини-карта, полоса опыта 310, HUD с фиксированной раскладкой — ручки не имеет, двигаться
	//   не может, и сохранять у него нечего. Модальные окна (`*_MSGBOX`, `SHOUTING_WIN`) исключены
	//   отдельно: они появляются по месту (центр экрана, `CenterModalContent`).
	//   ★ Чат 2500 и журнал 2570 стоят в `GDragDisabledWindows` (требование юзера «только
	//   растягивание») ⇒ ПОЗИЦИЯ у них не сохраняется по тому же признаку, а РАЗМЕР сохраняется
	//   через `SaveWindowSize` ниже.

	// Окно «сохраняемое» по признаку выше (ручка в данных .JUI + не модальное)?
	UFUNCTION(BlueprintPure, Category="AsdaWindow")
	bool IsWindowLayoutSaveable(FName WindowId) const;

	// Есть ли сохранённая позиция у окна (хоть по одной цели)?
	//   ★ ЭТО ВОПРОС ДЛЯ ПОДСИСТЕМ, КОТОРЫЕ СЧИТАЮТ ПОЗИЦИЮ ОКНА САМИ (авто-якорь). Такая
	//   подсистема обязана спросить это ПЕРЕД тем, как перебить позицию своей формулой, иначе
	//   восстановленная из ini позиция будет затёрта в первом же кадре.
	UFUNCTION(BlueprintPure, Category="AsdaWindow")
	bool HasSavedWindowOffset(FName WindowId) const;

	// Сохранённый сдвиг конкретной цели. TargetKey = NAME_None ⇒ первая (обычно единственная) цель.
	bool GetSavedWindowOffset(FName WindowId, FName TargetKey, FVector2D& OutOffset) const;

	// ФАКТИЧЕСКИ применённый к живому окну сдвиг цели (ini-восстановление + кламп в экран + то,
	//   что пользователь натянул мышью в этой сессии).
	//   Нужен владельцам, считающим «дизайн-позицию» цели: дизайн = текущая позиция − применённое.
	//   ★ `TargetKey = NAME_None` ⇒ первая (обычно единственная) цель — как у `GetSavedWindowOffset`.
	bool GetAppliedWindowOffset(FName WindowId, FName TargetKey, FVector2D& OutOffset) const;

	// ★ СИНХРОНИЗАЦИЯ БАЗЫ НАКОПЛЕНИЯ. Владелец, который двигает цель СВОИМ кодом (авто-якорь окна
	//   хранителя 56000), обязан в момент, когда цель становится перетаскиваемой, привести учёт
	//   общего механизма к ФАКТУ: Total = текущая позиция − дизайн-позиция .JUI. Иначе драг-дельты
	//   `AccumulateOffset` лягут на неверную базу, и при следующем запуске окно восстановится
	//   «дизайн + дельты» = мимо того места, где его оставили (ровно так в ini попал мусор
	//   `Pos.MINISTATE_WIN.Soul_Boost_Win=2592,1783`). Ключ остаётся ОБЫЧНЫМ («Pos.»): восстановление
	//   при запуске делает общий механизм, как у всех окон.
	void SyncSavedWindowOffset(FName WindowId, FName TargetKey, FVector2D TotalOffset);

	// Записать сдвиг ИЗВНЕ — для окон, которые двигает не общий `TickDrag`, а собственный код
	//   подсистемы-владельца (окно хранителя 56000 с авто-якорем, справка чата 2580 и т.п.).
	//   Пишется в ту же секцию ini тем же форматом, что и обычные окна.
	UFUNCTION(BlueprintCallable, Category="AsdaWindow")
	void SetSavedWindowOffset(FName WindowId, FName TargetKey, FVector2D Offset);

	// ── РАЗМЕР (только окна с ресайзом: чат 2500, журнал 2570) ──
	//   ⚠ Память размера у кнопки «свернуть/развернуть» 2605 живёт ТОЛЬКО В СЕССИИ (требование
	//   юзера) и к этим функциям отношения не имеет: сюда кладётся ТЕКУЩИЙ размер окна, а не
	//   «запомненный кнопкой». Одно другому не мешает.
	UFUNCTION(BlueprintCallable, Category="AsdaWindow")
	void SaveWindowSize(FName WindowId, FVector2D BaseSize, bool bFlag = false);
	bool GetSavedWindowSize(FName WindowId, FVector2D& OutBaseSize, bool* OutFlag = nullptr) const;

	// Сбросить сохранённую раскладку к умолчанию .JUI. NAME_None = ВСЕ окна.
	//   Открытые окна тут же возвращаются на свои места (сдвиг снимается), ini переписывается.
	UFUNCTION(BlueprintCallable, Category="AsdaWindow")
	void ResetWindowLayout(FName WindowId);

	// Записать накопленное на диск (зовётся на отпускании мыши, закрытии окна и выходе).
	void FlushWindowLayout();

	// Текстовая сводка сохранённой раскладки (консоль `asda.UI.DumpWindowPos`).
	FString DebugWindowLayout() const;

	// ═════════════════ СЦЕПКА ОКОН ПО ВЕРХНЕЙ КРОМКЕ («приклеить окно к окну») ═════════════════
	// Зачем. Системный журнал 2570 стоит ВПЛОТНУЮ НАД основным чатом 2500 (JUI: журнал 1,508,474×70
	//   ⇒ низ 578; чат 1,580 ⇒ верх 580; зазор 2 px). Когда чат растёт вверх, журнал обязан ехать
	//   вверх ровно на ту же величину, оставаясь приклеенным. Подсистемы разные
	//   (UAsdaChatWindowSubsystem и UAsdaChatSubsystem), и знать друг о друге они не должны —
	//   поэтому связь идёт через ЭТОТ реестр: окно-якорь публикует свою верхнюю кромку в БАЗОВЫХ
	//   пикселях (1024×768), приклеенное окно её читает в своём тике и позиционируется от неё.
	// ★ Публикуется именно БАЗОВАЯ координата, а не экранная: у всех окон порта POINT-анкор в
	//   нижний-левый угол экрана, поэтому базовая кромка не зависит ни от разрешения, ни от DPI —
	//   сцепка одинаково держится на 768, 1080 и 1440.
	void SetWindowAnchorTop(FName WindowId, float BaseTopPx);
	bool GetWindowAnchorTop(FName WindowId, float& OutBaseTopPx) const;

	// Открыть окно по id из класса виджета. Уже открыто → вернуть существующее (не плодить). Иначе создать,
	//   добавить в вьюпорт с z-order, зарегистрировать (аналог create+MapInsert+SetVisible оригинала).
	//   bModal=true → модальное (блокирует ввод/фокус к нижним окнам, пока открыто).
	UFUNCTION(BlueprintCallable, Category="AsdaWindow")
	UUserWidget* OpenWindow(FName WindowId, TSubclassOf<UUserWidget> WidgetClass, int32 ZOrder = 0,
		ESlateVisibility Visibility = ESlateVisibility::SelfHitTestInvisible, bool bModal = false);

	// Удобный вариант: класс WBP по объектному пути ("/Game/.../WBP_X.WBP_X_C" — суффикс _C добавится, если нет).
	UFUNCTION(BlueprintCallable, Category="AsdaWindow")
	UUserWidget* OpenWindowByPath(FName WindowId, const FString& WidgetClassPath, int32 ZOrder = 0,
		ESlateVisibility Visibility = ESlateVisibility::SelfHitTestInvisible, bool bModal = false);

	// Зарегистрировать УЖЕ созданное окно (напр. LOGIN_WIN с биндингами) под id — чтобы находить/скрывать через мгр.
	UFUNCTION(BlueprintCallable, Category="AsdaWindow")
	void RegisterWindow(FName WindowId, UUserWidget* Widget, int32 ZOrder = 0, bool bModal = false);

	// Найти открытое окно по id (аналог FindControlByID). nullptr если нет.
	UFUNCTION(BlueprintCallable, Category="AsdaWindow")
	UUserWidget* FindWindow(FName WindowId) const;

	// ★★★ ОБЩАЯ ТОЧКА «окно ровно по центру экрана». Эталон считает это одной и той же формулой
	//   для каждого всплывающего окна: `posX = ширина/2 − w/2`, `posY = высота/2 − h/2`
	//   (напр. облачко 2406, `ui_core.c:60338…60342`). Здесь то же самое, но анкером (0.5,0.5),
	//   чтобы центр держался на ЛЮБОМ разрешении (DPI проекта = 1.0, px-размеры сохраняются).
	//   RefWidget — контрол, ПО КОТОРОМУ центрируем (рамка окна). nullptr ⇒ по общему bbox всех
	//   point-анкор детей корневой канвы (так живут модальные диалоги).
	//   Взаимная раскладка детей не меняется: все сдвигаются ОДНИМ общим смещением. Идемпотентно.
	void CenterWindowOnScreen(UUserWidget* W, UWidget* RefWidget = nullptr);

	// ★★★ ЦЕНТР ЭКРАНА КАК ДИЗАЙН-ПОЗИЦИЯ, БЕЗ ПОТЕРИ СОХРАНЁННОГО СДВИГА (24.09.2026).
	//
	//   ЗАЧЕМ. Требование владельца к окну «Дом для питомца» 43000, дословно: «По-умолчанию
	//   по-середине экрана, потом по сохраненному положению окна после перетаскивания
	//   пользователем». Голый `CenterWindowOnScreen` для этого НЕ ГОДИТСЯ: он считает позицию по
	//   ТЕКУЩИМ смещениям слотов, а в них уже лежит восстановленный из ini сдвиг (`ApplySavedLayout`
	//   отрабатывает внутри `OpenWindow`, то есть РАНЬШЕ) — и центрирование его затирает. Об этой
	//   грабле прямо предупреждает шапка `HasSavedWindowOffset` выше; трое вызывающих
	//   (`AsdaCharaInfoSubsystem`, `AsdaSystemOptionSubsystem`, `AsdaPurchaseBoxSubsystem`) зовут
	//   центрирование безусловно и этим болеют.
	//
	//   ЧТО ДЕЛАЕТ: снимает применённые сдвиги → центрирует → возвращает сдвиги ПОВЕРХ центра.
	//   ⇒ «дизайн-позиция» окна становится центром экрана, а перетаскивание пользователя
	//   отсчитывается от него и живёт в ini обычным ключом `Pos.<Окно>.<Цель>` — без отдельного
	//   хранилища и без правки ассета.
	//   ИДЕМПОТЕНТНО: повторный вызов даёт тот же результат (обе части идемпотентны).
	//   ⚠ Это ОТСТУПЛЕНИЕ ОТ ОРИГИНАЛА и помечено как таковое: оригинал ставит окно по координатам
	//   .JUI и позиций между сессиями не хранит вовсе (`UI_SaveWindowState@0xD61D40`,
	//   `ui_core.c:54410-54467`, пишет в `WinState.bin` только четыре тега — `QP`/`CO`/`MO`/`SMO`,
	//   ни одной координаты). Откат — у ключа вызывающей подсистемы.
	UFUNCTION(BlueprintCallable, Category="AsdaWindow")
	void CenterWindowKeepingSavedOffset(FName WindowId, UWidget* RefWidget = nullptr);

	// ★★★ [ОРИГИНАЛ] 08.10.2026 (уточнение владельца «дефолт окна», отчёт `Reports/owner_1008/window_default_pos.md` §1.4/§6):
	//   ПОСТАНОВКА ОКНА КОДОМ, как `CControl_SetField74_75@0x97D500` оригинала у отдельных окон (9100 по центру −105 по Y,
	//   16000 центр+якорь, 1300 в 16 %/23 % экрана, 750 в точке 42 %/50 %). Левый-верх РАМКИ окна (планы корневого контрола
	//   FrameIdc; без них — RefWidget; без него — общий габарит детей корня) ставится в точку, которую считает TopLeftOf по
	//   размеру экрана (юниты канваса) и размеру рамки. Сохранённые сдвиги игрока снимаются до постановки и возвращаются
	//   поверх — дизайн-позиция окна становится постановкой оригинала (её и возвращает правило «за экраном → дефолт»).
	//   Анкор детей корня — левый-верх экрана: позиция абсолютна, как Origin у оригинала. Ключ отката `asda.UI.JuiDefaultPos 0`
	//   проверяют вызывающие (прежнее поведение порта).
	void PlaceWindowKeepingSavedOffset(FName WindowId, FName FrameIdc, UWidget* RefWidget,
		TFunctionRef<FVector2D(const FVector2D& ViewSize, const FVector2D& FrameSize)> TopLeftOf);
	// Размер экрана в юнитах канваса окон (вьюпорт / DPI); нет вьюпорта — 1024×768.
	FVector2D GetViewportCanvasSize() const;
	// ★ 08.10.2026: ключ `asda.UI.JuiDefaultPos` — 1 (умолчание): окна стоят в дефолте ОРИГИНАЛА (разметка .JUI и постановки
	//   кодом оригинала, отчёт window_default_pos.md §6); 0 — прежние постановки порта (центр экрана у 900/8300/69400/69500/750,
	//   без постановки у 9100/16000/1300, у курсора у 8100).
	static bool UseOriginalDefaultPos();

	// ── ★★★ Аудит №4 (B4): ГВАРД «ОКНО ПЕРЕЖИЛО ТРАВЕЛ» — ОДНА ТОЧКА ────────────────────────────
	//
	// ЧТО ЭТО. Кэшированный указатель на окно у подсистемы `UGameInstance` переживает смену уровня,
	//   а САМ виджет — нет: при травеле он остаётся привязан к СТАРОМУ миру и вылетает из вьюпорта.
	//   Поэтому каждое `EnsureWidgets()` начинается с вопроса «мой кэш ещё годен?», и ответ на него
	//   складывается из ТРЁХ условий, каждое из которых обязательно:
	//     1) указатель жив;
	//     2) мир виджета == текущий мир GameInstance  — иначе это окно прошлого уровня;
	//     3) виджет В ВЫВОДЕ (`IsInViewport`)         — иначе он жив, но его никто не увидит.
	//
	// ★ ЗАЧЕМ ОБЩАЯ ТОЧКА. Условие было выписано открытым кодом в 15 подсистемах, и в шестнадцатой
	//   (`UAsdaMinimapSubsystem::EnsureWidgets`) третью часть ЗАБЫЛИ. Цена ошибки — не «лишние
	//   строки», а мёртвая функция: если виджет миникарты вылетел из вьюпорта, оставшись в том же
	//   мире, гвард возвращал «кэш годен», окно НИКОГДА не переоткрывалось, и карта пропадала до
	//   перезахода. Такой вопрос обязан иметь одно имя и один ответ.
	//
	// ⚠ Это НЕ `IsWindowVisible`: окно может быть намеренно спрятано (`Collapsed`), оставаясь
	//   совершенно живым — здесь спрашивают «кэш ещё мой?», а не «видно ли сейчас».
	UFUNCTION(BlueprintCallable, Category="AsdaWindow")
	bool IsWindowLive(const UUserWidget* Widget) const;

	// ЗАРЕГИСТРИРОВАНО ли окно (виджет создан). ⚠ НЕ то же, что «видно»: окна порта прячутся
	//   `SetWindowVisible(false)`, оставаясь зарегистрированными, поэтому для вопроса «окно сейчас
	//   на экране?» есть `IsWindowVisible` ниже — путать их нельзя.
	UFUNCTION(BlueprintCallable, Category="AsdaWindow")
	bool IsOpen(FName WindowId) const;

	// ★ Окно ФАКТИЧЕСКИ видно: виджет есть И его видимость не Collapsed/Hidden.
	//   Единственный источник истины «окно открыто» для всех, кто выводит из этого своё состояние
	//   (подсветка кнопок главного меню, гейты модальности, тоглы по клавише).
	UFUNCTION(BlueprintCallable, Category="AsdaWindow")
	bool IsWindowVisible(FName WindowId) const;

	// Показать/скрыть окно (Visibility = SelfHitTestInvisible при показе визуала; Collapsed при скрытии).
	UFUNCTION(BlueprintCallable, Category="AsdaWindow")
	void SetWindowVisible(FName WindowId, bool bVisible,
		ESlateVisibility VisibleMode = ESlateVisibility::SelfHitTestInvisible);

	// Закрыть окно: убрать из вьюпорта + снять с регистрации.
	UFUNCTION(BlueprintCallable, Category="AsdaWindow")
	void CloseWindow(FName WindowId);

	// Закрыть ВСЕ окна: снять с вьюпорта и очистить реестр. ⚠ ЭТО СНОС, А НЕ «ЗАКРЫТИЕ ПО ИГРЕ».
	//   Подсистемы-владельцы при этом НЕ узнают, что их окна не стало, и их собственные флаги
	//   состояния («окно открыто») остаются взведёнными — окно воскресает в ближайшем `Tick`
	//   через `EnsureWidget`. Поэтому применять только на РАЗБОРКЕ (`Deinitialize`).
	//   Игровое «закрыть открытые окна» — `CloseAllPlayerWindows` ниже (порт `uiCloseAllWindows`).
	UFUNCTION(BlueprintCallable, Category="AsdaWindow")
	void CloseAll();

	// ══════ ★★★ ЭТАЛОННОЕ «ЗАКРЫТЬ ОТКРЫТЫЕ ОКНА» (`uiCloseAllWindows@0xD61960`, ui_core.c:54350) ══════
	//
	// РЕВЕРС. В оригинале это НЕ обход реестра `CWindowMgr`, а ЖЁСТКИЙ ЯВНЫЙ СПИСОК: функция зовёт
	//   у каждого окна-участника его СОБСТВЕННЫЙ метод закрытия (`CInventoryWindow::Close` для
	//   инвентаря, `uiPanel_Hide` для книги навыков, `miniMapWndClose` для мини-карты …), и
	//   каждый такой метод сбрасывает СВОЙ флаг «я открыт». Именно поэтому окно потом не
	//   всплывает обратно. Плюс три действия по HUD, окна не закрывающие:
	//     • `uiShowPanelControls(g_pQuickSlotBar)` — ЗАБЛОКИРОВАТЬ хотбар (мисномер: показываются
	//       затемняющие оверлеи, `bPanelHidden = 0` ⇒ `hotbarOnUiEvent` игнорирует ввод);
	//     • `CUIPanel_SetExpanded(g_pSystemMainWnd, 0, 1)` — свернуть трей главного меню;
	//     • `uiPanel_DestroyChildControls(g_pSystemMainWnd)` — мисномер: ЗАПРЕТИТЬ кнопки трея
	//       (кроме 73410 «гача» — её в списке нет, ею и выходят из сцены).
	//   Чат, мини-статус, полоса опыта, нотисы, подсказка и прочий HUD в списке НЕ значатся и
	//   остаются на экране.
	//
	// ПОЧЕМУ НЕ `CloseAll`. Прежний порт звал `CloseAll()`, и это дало ДВЕ регрессии:
	//   1) подсистемы с собственным кэшем виджетов переставали рисовать (тултипы во всей игре;
	//      список каналов мини-карты вместе с картой мира, редактором вкладок чата, палитрой,
	//      окном гнезда самоцвета и GM-панелью) — разбор `Reports/minimap_channels_fix.md`;
	//   2) инвентарь, наоборот, воскресал через кадр: `CloseAll` не трогает его `bWindowOpen`,
	//      и `Tick` → `EnsureWidget` создавал окно заново — разбор `Reports/gacha_close_windows.md`.
	//   Обе — одна и та же ошибка: сносить виджет мимо владельца.
	//
	// Роспись участников — `RegisterWindowCloser` (окно САМО объявляет, как его закрывать).
	// Окно БЕЗ закрывателя не трогается вовсе: это ровно те окна, которых нет в списке эталона.
	UFUNCTION(BlueprintCallable, Category="AsdaWindow")
	void CloseAllPlayerWindows();

	// ══════ ★★★ ПАРНОЕ ВОССТАНОВЛЕНИЕ (`uiExpandPartyPanel@0xD61C30`, ui_core.c:54401) ══════
	//   Зовётся при ВЫХОДЕ из полноэкранной сцены (`uiGuildPanelClose@0xB6E920` — закрытие сцены
	//   премиум-гачи, ui_core.c:7195). Возвращает ровно ТРИ вещи HUD:
	//     `*(g_pUIManager+17) = 0`, `uiHidePanelControls(g_pQuickSlotBar)` (разблокировать хотбар),
	//     `CUIPanel_SetExpanded(g_pSystemMainWnd, 1, 1)` (развернуть трей),
	//     `uiPanelClearAllTabFlags(g_pSystemMainWnd)` (разрешить кнопки трея).
	// ★★★ ЗАКРЫТЫЕ ОКНА НЕ ВОССТАНАВЛИВАЮТСЯ, и это НЕ упущение. Снимок «что было открыто»
	//   делает отдельная функция `uiCaptureOpenWindowsState(g_pNpcWnd)`, и вход в сцену гачи
	//   (`premiumGacha_BeginRoll@0xB6DF80`, world_map.c:4700-4702) её НЕ зовёт — в отличие от
	//   входа в магазин NPC (ui_core.c:61117) и продажи (item_inventory.c:19254). То есть после
	//   выхода из гачи инвентарь в оригинале остаётся ЗАКРЫТЫМ.
	UFUNCTION(BlueprintCallable, Category="AsdaWindow")
	void RestoreAfterSceneExit();

	// Порт флага `*(g_pUIManager + 17)`: «игровые окна сейчас убраны полноэкранной сценой».
	//   Ставится в `CloseAllPlayerWindows`, снимается в `RestoreAfterSceneExit`.
	UFUNCTION(BlueprintPure, Category="AsdaWindow")
	bool ArePlayerWindowsHidden() const { return bPlayerWindowsHidden; }

	// ══════ ★★★ СНИМОК ОТКРЫТЫХ ОКОН И ИХ ВОССТАНОВЛЕНИЕ (волна В3 ремесла, 25.09.2026) ══════
	//   Порт пары `uiCaptureOpenWindowsState@0xD96FD0` (ui_core.c:60910) / `uiCloseAllToggleWindows@0xD97280`
	//   (:60958) → `uiDispatchWindowToggle@0xD97300` (:60972). Реализация — `Private/UI/AsdaWindowSnapshot.cpp`,
	//   разбор слотов — `Reports/craft/wave3.md` §3.
	//   ЭТАЛОН: 15 флагов `g_pNpcWnd + 0xEDF4 + 4·слот` (`gap_5A08 + 37868…37924`, dword [15229..15243]; ревизия В3
	//   МЕЛК-5: +0xEE08 — это слот 5, а не начало массива). Снимок ТОЛЬКО ставит флаги
	//   видимых окон (ничего не закрывает — закрывает затем `uiCloseAllWindows` вызывающего); восстановление
	//   для каждого поднятого флага зовёт ИГРОВУЮ функцию открытия окна (со своим звуком) и обнуляет все 15.
	//   Слот 5 (окно ремесла 76000) в снимок не пишется никогда (байты `0xD96FD0`), слот 2 пишется, но в
	//   восстановлении ветки нет — оба сохранены 1:1.
	//   Кто снимает: вход в диалог NPC (`regionWindow_OpenAndPathMove`, ui_core.c:61117), открытие окна
	//   состава 77000 (`uiCraftWnd_OpenForItem`, ui_core.c:101175), открытие элитного магазина 6000
	//   (`uiInventoryOpenWindow` → `charBeginAutoMove@0xC3E420`, entity_char.c:45057 — волна В4; второй путь
	//   `invShopSellConfirm@0xC279E0` недостижим: у его обёртки 0xC276A0 вызывающих в бинаре нет). Кто восстанавливает:
	//   уход от NPC (`UI_resetAllWindowsAndLeave_5045`, :61245 — его зовут и закрытия магазинов: `shopWin_Close`
	//   shop_trade.c:3918, `uiInventoryResetAndLeave` ui_core.c:28553, `extractShop_Close` item_enhance.c:1003) и сброс
	//   окна изготовления 77500 (`itemQtyDlgReset`, item_inventory.c:69590 — волна В4).
	//   ★ ВЛАДЕЛЬЦЫ ФЛАГОВ — только для исправления К-7 (`asda.Craft.FixStaleSnapshot`): у эталона флаг один на
	//   слот, и снимок окна 77000, закрытого без изготовления, живёт до чужого восстановления (переоткрывает
	//   давно закрытые окна). Порт помнит, КТО поднял флаг, и ремесло снимает только СВОИ — снимок диалога NPC,
	//   снятый в ту же минуту, не страдает. Восстановление, как и у эталона, открывает слот при любом владельце.
	enum class EWindowSnapshotOwner : uint8
	{
		NpcDialog = 1 << 0,   // `regionWindow_OpenAndPathMove@0xD99290` — вход в диалог NPC
		Craft     = 1 << 1,   // `uiCraftWnd_OpenForItem@0xFE20D0` — окно состава 77000
		Shop      = 1 << 2,   // `charBeginAutoMove@0xC3E420` — открытие элитного магазина 6000 (волна В4)
	};
	// Защиты порта снимка (ревизия В3 МЕЛК-2, ключ `asda.UI.WindowSnapshotGuards`, умолчание 1): сброс сессии снимает
	//   флаги без открытия окон, а закрытие диалога NPC / окна 77500 внутри чужого «закрыть всё» окна не переоткрывает.
	//   0 = как оригинал. Читают `ResetSessionState` этой подсистемы, диалог NPC и окно 77500.
	static bool AreSnapshotGuardsEnabled();
	// Включена ли общая точка снимка (`asda.UI.WindowSnapshot`, умолчание 1 = как оригинал). Читают пути, у которых
	//   снимок идёт в паре с «закрыть всё» (элитный магазин 6000): при 0 они ведут себя как порт до 25.09.
	static bool IsWindowSnapshotEnabled();
	static constexpr int32 kWindowSnapshotSlots = 15;   // `memset(this + 15229, 0, 0x3C)` = 15 dword

	// `uiCaptureOpenWindowsState`: поднять флаги слотов, чьи окна сейчас открыты. Возвращает число флагов,
	//   поднятых ЭТИМ вызовом. При `asda.UI.WindowSnapshot 0` ничего не делает (поведение порта до 25.09).
	int32 CaptureOpenWindows(EWindowSnapshotOwner Owner, const TCHAR* Why);
	// `uiCloseAllToggleWindows`: для каждого поднятого флага — игровое «открыть» окна слота, затем все
	//   флаги в 0. Возвращает число окон, которым отдано «открыть».
	int32 ReopenCapturedWindows(const TCHAR* Why);
	// Исправление К-7: снять флаги только этого владельца (без открытия окон).
	void DropCapturedWindows(EWindowSnapshotOwner Owner, const TCHAR* Why);
	// Снять ВСЕ флаги без открытия окон (сброс сессии: переоткрывать окна посреди травела/разрыва нельзя).
	void ClearWindowSnapshot(const TCHAR* Why);

	// ══════ ★★★ 22.10.2026: ОКНА, КОТОРЫЕ ЭТАЛОН ДЕРЖИТ ПРИ СМЕНЕ КАРТЫ (травел UE их сносит) ══════
	//   ЭТАЛОН. Загрузка карты (`g_LoadProgress` 0…11, game_main.c:3540-3725) закрывает ТОЛЬКО свой список: автоотказ
	//   заявки в отряд, значки уведомлений хотбара, элитный ящик 69000 (`sub_92548B(g_pInventoryBagWindow)` →
	//   `SetVisible(0)`), окна-сообщения 5001/5002/0, большую карту (`miniMapWndClose`), вопрос зова друга (38),
	//   окно квест-обмена; на шаге 11 — магазин бустеров 15300 (`uiStorageWindow_Close` + `uiCloseItemComposeDialog`),
	//   парикмахерскую, элитный магазин 6000 (`uiInventoryResetAndLeave`), панель состава пакета 26500. Инвентарь,
	//   окно персонажа, журнал заданий, разборка, клетка питомца, улучшение снаряжения, книга навыков, сообщество —
	//   у оригинала ОСТАЮТСЯ открытыми (это постоянные контролы `CWindowMgr`, загрузка их не трогает).
	//   ПОРТ. Виджеты умирают вместе с миром UE, а владельцы при «смене карты» флаг «открыто» либо снимают (персонаж,
	//   сообщество), либо держат при мёртвом виджете (журнал заданий) — окно пропадало (самотест
	//   `asda.SelfTest.EntryWindowsTravel`, режим `legacy`). [ИСПРАВЛЕНИЕ ПОРТА] перед травелом из игрового мира
	//   (`PreLoadMap`) запоминаются открытые окна из этого списка (слоты снимка 0/1/4/6/9/12/13/14 — те же предикаты, что у
	//   `uiCaptureOpenWindowsState`), а после входа на новую карту (`UAsdaWorldSubsystem::RevealWorld` по 4003, после
	//   `CloseAll`) они пересобираются ИГРОВЫМИ функциями открытия владельцев молча (`FUiSoundMute` — у оригинала окна
	//   не закрывались, звуков нет). Окно с «открыто» при мёртвом виджете сперва закрывается владельцем (тоже молча).
	//   Полный сброс сессии запомненное снимает. Откат: `asda.UI.EntryWindowsEtalon 0`.
	void CaptureTravelKeptWindows(const TCHAR* Why);
	int32 ReopenTravelKeptWindows(const TCHAR* Why);
	bool HasTravelKeptWindows() const;
	// Порт `setField_p17(g_pUIManager, 0)` (4055/4056 своего, game_main.c:13027, network.c:42683): ТОЛЬКО флаг
	//   `*(g_pUIManager+17) = 0`, без хотбара и трея (их возвращает `uiPanelClearAllTabFlags` / `RestoreAfterSceneExit`).
	void SetPlayerWindowsHiddenFlag(bool bHidden) { bPlayerWindowsHidden = bHidden; }
	// Поднят ли флаг слота (любым владельцем) / маска владельцев слота (диагностика и самотест).
	bool IsWindowSlotCaptured(int32 Slot) const { return Slot >= 0 && Slot < kWindowSnapshotSlots && WindowSnapshot[Slot] != 0; }
	uint8 GetWindowSlotOwners(int32 Slot) const { return (Slot >= 0 && Slot < kWindowSnapshotSlots) ? WindowSnapshot[Slot] : 0; }
	// Текстовый дамп 15 слотов: имя окна эталона, есть ли окно в порту, владельцы флага.
	FString DumpWindowSnapshot() const;
	// Идёт ли сейчас цикл закрывателей `CloseAllPlayerWindows`. Закрыватель диалога NPC (он в росписи порта) по нему
	//   понимает, что разговор кончается НЕ уходом игрока, а чужим «закрыть всё», — и окна из снимка не переоткрывает
	//   (иначе окна, чьи закрыватели уже отработали, остались бы открытыми посреди полноэкранной сцены).
	bool IsClosingAllPlayerWindows() const { return bClosingAllPlayerWindows; }

	// ── ★★★ РОСПИСЬ УЧАСТНИКОВ `CloseAllPlayerWindows` ──
	// Владелец окна объявляет, КАК его окно закрывается «по-игровому» (со сбросом своего флага).
	//   Порт эталонной строки списка вида `if (открыт) ЗакрытьСебя()`: условие «открыт» живёт
	//   внутри самого закрывателя (в порту это ранний выход `if (!bWindowOpen) return;`).
	//   Делегат биндить ТОЛЬКО через `CreateUObject`/`CreateWeakLambda` — тогда он сам протухает
	//   вместе с подсистемой и мёртвых вызовов не бывает.
	// Порядок вызова закрывателей = порядок регистрации (эталонный список тоже упорядочен;
	//   у нас порядок не значим — окна независимы, но детерминизм важен для повторяемости логов).
	void RegisterWindowCloser(FName WindowId, FSimpleDelegate Closer);

	// Снять роспись (окно больше не участвует в `CloseAllPlayerWindows`).
	void UnregisterWindowCloser(FName WindowId);

	// Сменить z-order окна (перекрытие/подъём наверх).
	UFUNCTION(BlueprintCallable, Category="AsdaWindow")
	void SetWindowZOrder(FName WindowId, int32 ZOrder);

	/** Текущий z-порядок окна (−1 — окна нет в реестре). Парный геттер к `SetWindowZOrder`:
	 *  нужен дампам «кто над кем» — без него порядок окон проверялся только глазами по снимку. */
	UFUNCTION(BlueprintPure, Category="AsdaWindow")
	int32 GetWindowZOrder(FName WindowId) const;

	UFUNCTION(BlueprintCallable, Category="AsdaWindow")
	int32 GetOpenCount() const { return Windows.Num(); }

	// ── Модальность (аналог uiIsAnyModalWindowOpen@0xB79520) ──
	// Пометить окно модальным/немодальным (пересчитывает gate ввода).
	UFUNCTION(BlueprintCallable, Category="AsdaWindow")
	void SetWindowModal(FName WindowId, bool bModal);

	// Открыто ли ХОТЯ БЫ ОДНО модальное окно (модальное + видимое)? Ввод к нижним окнам заблокирован.
	//   ★ 10.10.2026: это порт эталонного флага `CWindowMgr+0x200` (`get_o200@0x9864A0`): кроме окон `bModal` учитываются
	//   модальные контролы эталона без `bModal` (росписи `RegisterEscModal` / `RegisterOriginalModal`), ключ `asda.UI.ModalLock`.
	UFUNCTION(BlueprintCallable, Category="AsdaWindow")
	bool IsAnyModalOpen() const;

	// Верхнее (максимальный z) модальное видимое окно. nullptr если модальных нет.
	UFUNCTION(BlueprintCallable, Category="AsdaWindow")
	UUserWidget* GetTopModalWindow() const;

	// ★ 10.10.2026 (modal_lock2, СРЕДН-1 ревизии): ввод мира сейчас отобран модалкой — `RefreshModalInput` поставил
	//   `FInputModeUIOnly` поверх БАЗОВОГО режима мира (GameAndUI), и вьюпорт игнорирует мышь (`SetIgnoreInput`). Тогда колесо
	//   до `AAsdaPlayerController::OnWheel` осью не доходит — его пересылает препроцессор Slate контроллера (колесо как у
	//   эталона game_main.c:5864: зум камеры, если курсор не над окном). На экранах логина (базовый UIOnly) — false.
	// ★ 10.10.2026 (modal_lock3, МЕЛК-3 ревизии `modal_lock2_REVIEW.md`): считается ПО ФАКТУ, без запомненного флага — есть
	//   видимая модалка (`GetTopModalWindow`), базовый режим мира GameAndUI и игровой вьюпорт действительно игнорирует ввод
	//   (`UGameViewportClient::IgnoreInput`). Прежний флаг `bModalInputCaptured` мог «залипнуть»: при закрытии модалки без
	//   контроллера (смена карты) `RefreshModalInput` выходил до его снятия, а новый контроллер получал режим мира от
	//   `RevealWorld` — колесо шло и вьюпортом, и препроцессором (двойной зум). Реализация — .cpp.
	bool IsWorldInputCapturedByModal() const;

	// ★★★ 10.10.2026 [ИСПРАВЛЕНИЕ] ПКМ ПОД МОДАЛКОЙ — МИР (modal_lock2, решение владельца; ★ уточнено modal_lock3, МЕЛК-4).
	//   ОКНАМ под модалкой эталон ПКМ НЕ отдаёт — как и порт при любом значении ключа. Слоты vtable менеджера окон `off_14FE8DC`
	//   (прочитаны из файла сборки IDB статически) — по видам очереди `input_ProcessMoveCommand@0x12F0290`: 3 «кнопка нажата» →
	//   `uiWindowMgrDispatchMouse@0x12AF540`, 5 «перемещение» → `uiWindowMgrDispatchToAllControls@0x12AF6E0`, 4 «двойной» →
	//   `uiControl_UpdateInputChildren@0x12AF640` — все три при `+0x1A0` отдают событие ТОЛЬКО модалке и выходят (`return 1`).
	//   ПКМ-нажатие (событие 0xE, `CControl_OnRButtonDblClick@0x12DC410` — миснейм; меню 28000 сумки) рождается только там.
	//   `uiControlDispatchMouseRToChildren@0x12AF8D0` — МИСНЕЙМ: это слот 6 «кнопка ОТПУЩЕНА» (любой кнопки); он идёт по всем
	//   окнам, но наведение 8/9/10 зовёт с индексом −1 (сразу 0), а 12/15/18 требуют флагов плана, которые ставит только
	//   нажатие (под модалкой до окон не дошедшее). Итог: под модалкой окна не получают ни ПКМ, ни наведения, ни подсказок.
	//   МИРУ эталон ПКМ отдаёт без гейта `get_o200`: WM_RBUTTONDOWN (game_main.c:5822) — `Camera_BeginRotateAnchor`,
	//   WM_RBUTTONUP (5834) — `Input_PickEntityClick`/`…Hover`. Порт это под модалкой запирает — [ИСПРАВЛЕНИЕ]. Ключ отката
	//   `asda.UI.ModalLockRightClick 0` — как оригинал: нажатие/отпускание ПКМ и вращение камеры доходят до мира (препроцессор
	//   `FAsdaModalMouseRelay` контроллера), окнам — ничего. true — ключ 0 (ПКМ миру под модалкой отдаётся).
	bool ModalRightClickReachesWorld() const;

	// ★ 10.10.2026 (modal_lock3, самотест МЕЛК-3): имитация потери контроллера для `RefreshModalInput` (true — контроллера
	//   «нет», как при смене карты; false — снова есть). Только для `asda.UI.SelfTestModalLock`.
	void DebugSimulateNoController(bool bNoController) { bDebugNoController = bNoController; }

	// ═══════ ★★★ ESC ЗАКРЫВАЕТ ВЕРХНЕЕ ОТКРЫТОЕ ОКНО (порт `uiContainerHandleKey@0x12AF390`) ═══════
	//
	// ЭТАЛОН (разобран целиком 17.09.2026, пункт С10-б реестра отложенного). `uiContainerHandleKey`
	//   — обработчик клавиши у САМОГО менеджера окон `CWindowMgr`, а не у диспетчера раскладки:
	//   `MainWndProc@0x9841A0` на WM_KEYDOWN кладёт клавишу в очередь менеджера
	//   (`CWindowMgr_QueueKeyDown@0x12EFE60`) БЕЗУСЛОВНО, а тройной гейт ввода
	//   (`!sub_9386BC && !sub_9323E3 && !helper_getField1 && !sub_930E71`) стоит строкой НИЖЕ и
	//   закрывает только `Input_OnKeyDown_Dispatch`. ⇒ проход закрытия идёт ДО всех гейтов и
	//   работает даже при открытом модальном окне.
	//
	// ЧТО ДЕЛАЕТ ЭТАЛОН — по телу функции, а не по именам:
	//   • ЕСТЬ МОДАЛЬНЫЙ КОНТРОЛ (`*(this + 104)`, байт-смещение `CWindowMgr+0x1A0`) — смотрится
	//     ТОЛЬКО ОН: `если (клавиша == 27 && видим && get_o228) { OnEvent21; *(this+520) = 1; }`.
	//     Дальше клавиша уходит детям одной лишь модалки, а список окон не обходится ВООБЩЕ:
	//     при открытой модалке нижние окна по ESC не закрываются.
	//     ★ В этой ветке `0x208` перед проверкой НЕ обнуляется — единица от прошлого нажатия
	//     доживает до следующего кадра. Дефект оригинала; у нас «съедено» — возвращаемое значение,
	//     а не поле с временем жизни, поэтому дефект не переносится.
	//   • МОДАЛЬНОГО НЕТ — при ESC сначала `*(this + 520) = 0` («не съедено»), затем обход СПИСКА
	//     ОКОН менеджера (`this + 98`, байт-смещение `CWindowMgr+0x188`) ОБРАТНЫМ ИТЕРАТОРОМ:
	//     начало `sub_931876` → `sub_12B5330` заворачивает `end()`, предел `sub_91FFB3` →
	//     `sub_12B5370` заворачивает `begin()`, шаг `sub_91FE28` идёт по `_Prev`
	//     (`sub_12B88B0`: `*this = *(узел + 4)`), а разыменование `stl_listGetNextValue`
	//     (`sub_12B5AB0`) копирует итератор, отступает на `_Prev` и берёт значение — каноническая
	//     `std::reverse_iterator`. ⇒ ОБХОД ИДЁТ С ХВОСТА СПИСКА.
	//     Хвост = ВЕРХНЕЕ окно: подъём окна кликом (`uiWindowMgrDispatchMouse@0x12AF540` →
	//     `uiControl_RemoveChild@0x12AFF80` — мисномер, функция стирает окно из списка и добавляет
	//     заново через `uiControl_AddChildOrdered@0x12B2B60`, а тот кладёт обычное окно `push_back`),
	//     и тем же обратным итератором идёт хит-тест мыши, где первым обязано отвечать верхнее.
	//   • ПЕРВОЕ ВИДИМОЕ окно с признаком `CControl+0x228` (`get_o228@0x12B3DE0`) получает
	//     `CControl_OnEvent21` и `*(this + 520) = 1`, после чего `break` — ЗА ОДНО НАЖАТИЕ
	//     ЗАКРЫВАЕТСЯ РОВНО ОДНО ОКНО.
	//   • `CControl_OnEvent21@0x12DCC30` = `CControl_RaiseEvent(this, -1, 21, 0)`: событие 21 уходит
	//     в ОКОННУЮ ПРОЦЕДУРУ владельца, и там это ШТАТНОЕ ЗАКРЫТИЕ — та же функция, что и у
	//     «крестика». Доказано тремя обработчиками: `upgradeWndProc@0xDC3690` → `uiCloseWindowReset`,
	//     `shopSellWndProc@0xEDC090` → `uiEquipUpgradePanelHide` (её же зовёт ветка нажатия кнопки
	//     57369), `itemUpgradeWndProc@0xB639B0` → `sub_93548F`. Это НЕ «спрятать виджет»: окно
	//     сбрасывает СВОЁ состояние.
	//   • Вложенность: обходятся только ПРЯМЫЕ дети менеджера, то есть КОРНЕВЫЕ окна .JUI.
	//
	// ПРИЗНАК «ЗАКРЫВАЮСЬ ПО ESC» — ЭТО ДАННЫЕ ОРИГИНАЛА, А НЕ НАШ ВЫБОР. Поле `CControl+0x228`
	//   пишет единственная обёртка `helper_setByteField552@0x91F8CE` → `set_o228@0xB61180`; вызовов
	//   61, все со значением 1, снимающих вызовов НЕТ ⇒ свойство СТАТИЧЕСКОЕ, ставится при
	//   инициализации окна. Оба среза декомпила (`AsdaStory_game.c` и `AsdaStory_ida.c`) дают
	//   список строка в строку одинаковый. Полный перечень (IDNum + имя + эталонная функция) — в
	//   таблице `kEscCloseWindows` в .cpp; назначать окна «по смыслу» ЗАПРЕЩЕНО.
	//   ★ Место таблицы — КОД, а не `DA_JuiWindowMeta`: мета генерируется импортёром из .JUI, а в
	//   .JUI этого признака НЕТ (он живёт в коде клиента), и дописывание его в ассет означало бы
	//   перегенерацию UI — запрещено законом проекта.
	//
	// ПОРТ. Закрывать окно обязан ЕГО ВЛАДЕЛЕЦ (это и есть событие 21), поэтому проход ищет роспись
	//   в трёх реестрах по порядку: 1) `EscClosers` (заведён специально под ESC), 2) `WindowClosers`
	//   (роспись `CloseAllPlayerWindows` — та же семантика «закрой себя»), 3) `NpcWindowClosers`
	//   (роспись «игрок тронулся» — тоже собственный `CloseWindow` окна). Окно из таблицы БЕЗ
	//   росписи проход ПРОПУСКАЕТ и смотрит окно ниже: слепое `CloseWindow` сняло бы виджет, но
	//   оставило бы `bOpen = true` у подсистемы-владельца, и окно больше не открылось бы.
	//   ⚠ Это ОСОЗНАННОЕ отступление от эталона (он `break`-ает на первом же окне с признаком,
	//   что бы ни сделала процедура) — компромисс ради 30 окон таблицы, которых в порту пока нет.
	//   Там, где поведение эталона ДОКАЗАНО как «съесть и ничего не делать», отступление снято
	//   реестром `EscSwallowers` (см. `RegisterEscSwallower`).
	//
	// ★★★ СЕМЕЙСТВО MSGBOX И НУМПАД (пункт С10-б/1 реестра отложенного, закрыт 17.09.2026).
	//   КАК НАЙДЕНА ПРОЦЕДУРА. Прежняя гипотеза «оконная процедура менеджера msgbox виртуальна,
	//   vtable+56 у `off_13FB5AC`» ОПРОВЕРГНУТА по БИНАРЮ (`RE/ida/work/AsdaStory.exe`,
	//   13 344 768 б; .rdata RVA 0xF91000 → файловый сдвиг 0xA74000, VA 0x13FB5AC → offset
	//   0xADE5AC): по этому адресу лежит РОВНО ОДИН указатель — 0x93B060 (`jmp 0xD6AB80`,
	//   удаляющий деструктор), сразу за ним идут строковые литералы `CMsgBoxMgr_InitControls`
	//   («After_Death_Txt», «RESU_Normal», «m_pMsgBox_Resu_Base»). То есть у класса ровно одна
	//   виртуальная функция и слота +56 в этой таблице НЕТ ВООБЩЕ.
	//   Настоящая цепочка события 21 (вся — из декомпила + бинаря):
	//     `CControl_RaiseEvent@0x12F19E0` зовёт ОБЩИЙ обработчик по `CWindowMgr+0x218`
	//       (записывает `sub_12B0530@0x12B0530`: `mov [eax+0x218], ecx` — снято с бинаря);
	//     ставит его `sub_93CD25(g_pWindowMgr, j_ui_DispatchCommandEvent)` ⇒ это
	//       `ui_DispatchCommandEvent@0xC6C650`;
	//     а тот роутит по `IDNum / 100`: ветка 14 → `uiQuantityNumPadDialogProc@0xDA6300`
	//       (окна 1400…1499 = НУМПАД), ветка 15 → `uiItemUpgradeWnd_OnCommand@0xD79260`
	//       (окна 1500…1599 = MSGBOX; имя в IDA ВРЁТ).
	//   ЧТО ДОКАЗАНО ДЛЯ КАЖДОГО ОКНА (ветка `case 0x15u` каждой процедуры):
	//     • `ONEBUT_MSGBOX` 1500 — `if (a3 != 1500)`: ESC НЕ ДЕЛАЕТ НИЧЕГО (нажатие съедено);
	//     • `TWOBUT_MSGBOX` 1520 — `OnSendItemUpgrade(mgr, 1524)` = кнопка `TWOBUT_BUT01`
	//       «Отмена» (loc 2289). ★ Аргумент `a4` в теле `OnSendItemUpgrade@0xD74180` НЕ
	//       ИСПОЛЬЗУЕТСЯ (проверено по всему телу) ⇒ исход в точности как у нажатия кнопки;
	//     • `SOWELL_SELBOX` 1530 — `OnSendItemUpgrade(mgr, 1533)` = кнопка `SOWELL_BUT01`
	//       «Отмена» (loc 2378);
	//     • `NUMPAD_WIN` 1410 — `(*this->pfnOnCancel)(this)`, ТОТ ЖЕ колбэк, что у кнопки
	//       `NUMPAD_CLOSED` 1416 (`uiNumPadDialogProc@0xDA6040`, `case 0x588u`) ⇒ ОТМЕНА ввода.
	//   ★ ОПАСНЫЙ СЛУЧАЙ ПРОВЕРЕН И БЕЗОПАСЕН: подтверждения ESC-меню «вернуться к выбору
	//   персонажа» (`SystemMenu_OnUICommand@0xE93E80`, cmdId 802 → contentKind 3) и «выйти из
	//   игры» (cmdId 803 → contentKind 4) показываются именно в `TWOBUT_MSGBOX`, а в ветке
	//   кнопки 1524 значений 3 и 4 НЕТ ⇒ ESC на них = просто закрыть окно. Действие живёт
	//   только в ветке кнопки 1523 (`case 3: gameReturnToCharSelectTimer()`,
	//   `case 4: gameLeaveToCharSelect()`). ESC из игры НЕ ВЫХОДИТ.
	//
	// Возвращает true, если окно закрыто — это порт `CWindowMgr+0x208 = 1` («нажатие съедено»):
	//   вызывающий обязан на этом остановиться и ESC-меню 69400 НЕ трогать.
	// Ключ отката: `asda.UI.EscCloseTopWindow 0` — прохода нет, ESC ведёт себя как до 17.09.2026.
	bool CloseTopmostOnEsc();

	// Стоит ли у окна эталонный признак «закрываюсь по ESC» (таблица `kEscCloseWindows`).
	UFUNCTION(BlueprintPure, Category="AsdaWindow")
	bool IsEscCloseWindow(FName WindowId) const;

	// ★ 07.11.2026 [ИСПРАВЛЕНИЕ] Окно из списка порта «ESC нажимает крестик» (`kEscExtraWindows`, ключ `asda.UI.EscExtraWindows`). Признак оригинала не меняет.
	bool IsEscExtraWindow(FName WindowId) const;
	// Нажать крестик такого окна. false — крестик недоступен.
	bool PressEscExtraClose(FName WindowId);

	// Роспись «как закрыть себя по ESC». Отдельная от `RegisterWindowCloser` НАМЕРЕННО: та
	//   записывает окно в эталонный список `uiCloseAllWindows@0xD61960`, и дописывать в него окна
	//   ради ESC значило бы менять ЧУЖОЕ поведение (полноэкранная сцена закрывала бы лишнее).
	//   Биндить только через `CreateUObject`/`CreateWeakLambda` — тогда роспись протухает вместе
	//   с владельцем и мёртвых вызовов не бывает.
	void RegisterEscCloser(FName WindowId, FSimpleDelegate Closer);
	void UnregisterEscCloser(FName WindowId);

	// ★ 10.10.2026 [ОРИГИНАЛ] ВСПЛЫВАЮЩИЙ СЛОЙ, КОТОРЫЙ ЭТАЛОН ДЕЛАЕТ КОРНЕВЫМ ОКНОМ. Пример — меню вкладок чата
	//   `CHAT_SETUP_Win` 2670: эталон показывает его и регистрирует ВЕРХНИМ корневым окном
	//   (`uiWindowShowHideAndRegister@0xBA0360` → `CClass_MapInsert(g_pWindowMgr, this[56])`), признак 0x228 ставит
	//   `CChat_BuildTabUI` (chat.c:863) ⇒ первый ESC закрывает его (`uiChatWindowMsgProc`, событие 21, `ctrlId == 2670`).
	//   В порту такой слой живёт ВНУТРИ виджета владельца (в реестре окон его нет), поэтому проход ESC видит его через
	//   эту роспись: видимость спрашивается у владельца (`IsShown`), z = z корневого виджета-владельца (его поднимает
	//   `PushTopmost`). Закрыватель — обычная `RegisterEscCloser(PopupId, …)`. Слой без строки в `kEscCloseWindows` не
	//   принимается (та же защита, что у `RegisterEscCloser`).
	void RegisterEscPopup(FName PopupId, FName OwnerWindowId, TFunction<bool()> IsShown);

	// ★ 07.11.2026 [ИСПРАВЛЕНИЕ] ЩЕЛЧОК МИМО ВСПЛЫВАЮЩЕГО МЕНЮ (`asda.UI.ClickAwayMenu`). Меню, у которого есть зона в реестре (`PushTopmost`
	//   или `RegisterFrameZone`), гаснет от нажатия ЛКМ/ПКМ/СКМ вне зоны и от потери фокуса приложения. Показанным считается меню, чья зона
	//   отдаёт прямоугольник. Вызов повторяемый: можно регистрировать при каждом показе. Шапка — `AsdaWindowSubsystem_TopPress.cpp`.
	void RegisterClickAwayMenu(FName ZoneId, FSimpleDelegate Closer);
	// Самотест: прямоугольник показанного меню; false — меню не показано.
	bool DebugGetClickAwayRect(FName ZoneId, FVector2D& OutMin, FVector2D& OutMax) const;
	void UnregisterEscPopup(FName PopupId);

	// ★ 10.10.2026 [ОРИГИНАЛ] ОКНО, КОТОРОЕ ЭТАЛОН ПОКАЗЫВАЕТ МОДАЛЬНО, А ПОРТ — БЕЗ ФЛАГА `bModal`. Пример — общая модалка
	//   покупки `SHELLITEM_PURCHASE_BOX` 1559: эталон `upgradeUiFillConfirmPanel@0xD7BD90` (item_enhance.c:2768) показывает
	//   её через `helper_setupFields(g_pWindowMgr, окно, 1)` — модальный контрол менеджера (`CWindowMgr+0x1A0`), а порт
	//   держит её обычным окном полосы 8850 (своё размещение и ввод). Для прохода ESC такое окно — МОДАЛКА: эталонный
	//   `uiContainerHandleKey@0x12AF390` при модальном контроле смотрит только его; признака 0x228 у 1559 нет ⇒ ESC не
	//   закрывает ни её, ни окна под ней (магазин 15000/15300 остаётся открытым). z окна роспись НЕ меняет (ввод — см. ниже).
	//   ★ 10.10.2026 (решение владельца, `owner_1010/modal_lock.md`): роспись — ОБЩИЙ механизм «модальный контрол эталона без
	//   флага `bModal` порта». Пока окно из росписи показано, оно запирает ввод так же, как `bModal`: `IsAnyModalOpen` = true
	//   (хотбар, сумка, ESC-меню, кнопки системного меню, раскладка клавиш, чат), мышь отдаётся только ему (блокер под ним).
	//   Полоса z окна и его размещение НЕ меняются. Ключ диагностики — `asda.UI.ModalLock 0` (запирается только ESC).
	void RegisterEscModal(FName WindowId);
	void UnregisterEscModal(FName WindowId);
	// ★ 10.10.2026 [ОРИГИНАЛ] Тот же механизм для модального контрола, который у порта живёт ВНУТРИ корневого окна, а не
	//   отдельным окном реестра. Пример — окно регистрации рыбы 74200 внутри книги 74000: эталон `fishCollectionOpenWindow
	//   @0xFD6720` (entity_char.c:64419) зовёт `helper_setupFields(g_pWindowMgr, 74200, 1)`. `ModalId` — имя контрола .JUI
	//   (для лога), `OwnerWindowId` — корень в реестре окон (мышь отдаётся ему), `IsShown` — показан ли контрол (лямбда со
	//   слабой ссылкой на владельца). Владелец обязан звать `NotifyModalStateChanged` при показе и скрытии контрола.
	//   ★ 10.10.2026 (modal_lock2, МЕЛК-2 ревизии): `Node` — виджет самого контрола внутри корня. Эталон отдаёт мышь только
	//   поддереву `+0x1A0` (74200: Origin 172 198, 222×492), поэтому щелчок, перетаскивание и колесо проходят модальный гейт лишь
	//   в прямоугольнике узла (`PassesModalMouseGateAt`) — заголовок книги 74000 при открытом 74200 мышь не видит. Пусто — мышь
	//   получает весь корень (прежнее правило).
	void RegisterOriginalModal(FName ModalId, FName OwnerWindowId, TFunction<bool()> IsShown, TFunction<const UWidget*()> Node = nullptr);
	void UnregisterOriginalModal(FName ModalId);
	// Показ/скрытие модального контрола из росписи `RegisterOriginalModal` — пересчитать замок ввода (блокер, фокус, подъём).
	void NotifyModalStateChanged();
	// Показано хоть одно окно из росписей `RegisterEscModal`/`RegisterOriginalModal` (эталонный флаг `CWindowMgr+0x200`, его
	//   ставит тот же `helper_setupFields`). При `asda.UI.ModalLock 0` учитываются только корневые окна (`RegisterEscModal`) —
	//   так было до 10.10.2026 (вечер): роспись запирала ESC-проход и гейт ESC-меню, но не прочий ввод.
	bool IsAnyEscModalShown() const;
	// Корень, которому сейчас отдан ввод модальным контролом эталона без `bModal` (NAME_None — такого нет или ключ снят).
	//   Нужен покадровым опросам ВНУТРИ этого окна: их собственный модальный контрол ввод не запирает
	//   (`AsdaUi::IsModalDispatchBlockedFor`).
	FName GetOriginalModalOwner() const;

	// ★★★ «ОКНО СЪЕДАЕТ ESC, НО НЕ ЗАКРЫВАЕТСЯ» — доказанное поведение однокнопочного msgbox.
	//   Не выдумка и не заглушка: оконная процедура семейства msgbox — `uiItemUpgradeWnd_OnCommand
	//   @0xD79260` (имя в IDA ВРЁТ, это процедура менеджера окон сообщений; как она найдена —
	//   в шапке `CloseTopmostOnEsc`). Её ветка события 21 начинается с ЯВНОГО исключения:
	//     `case 0x15u: if (a3 != 1500) { if (a3 == 1520) …; else if (a3 == 1530) …; } break;`
	//   ⇒ у `ONEBUT_MSGBOX` (1500) событие 21 не делает НИЧЕГО. При этом нажатие всё равно
	//   СЪЕДЕНО: `uiContainerHandleKey@0x12AF390` ставит `CWindowMgr+0x208 = 1` и делает `break`
	//   СРАЗУ ПОСЛЕ вызова `CControl_OnEvent21`, не глядя на результат ⇒ пока на экране висит
	//   однокнопочное сообщение, ESC в оригинале не закрывает ни его, ни окно под ним и не
	//   открывает меню 69400. Роспись-закрыватель тут была бы ВЫДУМКОЙ поведения, поэтому
	//   заведён отдельный реестр «поглотителей».
	void RegisterEscSwallower(FName WindowId);
	void UnregisterEscSwallower(FName WindowId);

	// Сводка для дампа `asda.UI.DumpEscWindows`: какие окна таблицы есть в порту, видимы ли сейчас
	//   и есть ли у них роспись закрытия.
	FString DebugEscWindows() const;

	// ── СИСТЕМА диалогов подтверждения (эталон MESSAGE2_WIN, IDNum 40) ──
	// Показать модальный Yes/No диалог. Message — готовый текст (вызывающий резолвит loc-строку сам).
	//   OnResult(true)=Yes(BUT00), OnResult(false)=No(BUT01). TimeoutSec>0 → авто-исход по таймауту.
	//   bTimeoutConfirms: исход таймаута (эталон onUiEvent a4==24: delete→send(0)=ОТМЕНА [false];
	//   back/login→send(1)=ПОДТВЕРЖДЕНИЕ [true]). Единый путь для ВСЕХ подтверждений; раньше собирался вручную.
	//   Возвращает виджет диалога (или nullptr).
	//   YesLocId/NoLocId: loc-ID подписей кнопок (текст ТОЛЬКО из таблицы DT_GameText, не литерал); -1 = не менять.
	//   bGateYesOnGauge: кнопка Yes НЕАКТИВНА (и визуально) пока гадж не заполнится за TimeoutSec (анти-мисклик удаления);
	//     при этом таймаут НЕ авто-закрывает — гадж лишь разблокирует Yes.
	//   ★ bNeutral (2026-09-03, решение владельца): НЕЙТРАЛЬНЫЙ вид — ассет-вариант
	//     `WBP_MESSAGE2_WIN_NEUTRAL` (тот же план .JUI, `Tools/asda_jui_generic_umg.py::WINDOW_VARIANTS`):
	//     обе кнопки в скине BUT00 (белая плашка + серый кружок, подпись FF323232), полоса GAUGE и значок
	//     WARNING свёрнуты В АССЕТЕ (закон no-runtime-widget-fixes). Для вопроса с двумя РАВНОПРАВНЫМИ
	//     ответами (выбор режима входа), а не «сделать / отменить». Имена узлов и WindowId (`MESSAGE2_WIN`)
	//     те же — реле, бинды, IsOpen/CloseConfirm не меняются. Полосы в варианте нет ⇒ с TimeoutSec > 0
	//     или bGateYesOnGauge не сочетается: показывается базовое окно, в лог Warning.
	UUserWidget* ShowConfirm(const FString& Message, TFunction<void(bool)> OnResult, float TimeoutSec = 0.0f,
		bool bTimeoutConfirms = false, int32 YesLocId = -1, int32 NoLocId = -1, bool bGateYesOnGauge = false,
		bool bNeutral = false);

	// ★★ ИГРОВОЙ двухкнопочный msgbox — эталон TWOBUT_MSGBOX 1520 (CMsgBox_SetContent contentKind 1,
	//   ui_core.c:56402): текст TWOBUT_TEXT (TextAlign 5 = центр/центр), кнопки TWOBUT_BUT00 = loc 2288 «ОК»
	//   (WkBtn, галочка) / TWOBUT_BUT01 = loc 2289 «Отмена» (RdBtn, крестик); окно центрируется по экрану
	//   (CMsgBoxMgr_InitControls ui_core.c:55953-55958). MESSAGE2_WIN — ЛОГИН-окно, в мире не используется.
	//   GaugeGateSec > 0 → полоса TWOBUT_GAUGE (клип ширины заливки, закон ProgressBar ES1) заполняется за
	//   N секунд, и ДО заполнения кнопка ОК НЕАКТИВНА (требование юзера-анти-мисклик; сам эталон в этом окне
	//   полосу для смерти скрывает — InitControls ui_core.c:55952, ветка 0x4000 её не показывает).
	//   ★★★ ОЧЕРЕДЬ (26.09, см. раздел «ОЧЕРЕДЬ ОКОН-СООБЩЕНИЙ» ниже): `EtalonNoticeId`/`EtalonKind` — пара
	//   (msgClass, noticeId) эталонного `CItem_BuildTooltipText`; по ней повторный показ, пока такая пара уже в
	//   очереди, ОТБРАСЫВАЕТСЯ молча (как `objList_ContainsPair`). `AsdaMsgBox::kNoKey` — у окна порта нет
	//   эталонного аналога: дубли не отбрасываются. Отброшенный показ возвращает nullptr, колбэк не зовётся.
	//   ★ 27.09.2026: GaugeTimerSec > 0 → полоса TWOBUT_GAUGE — ТАЙМЕР без гейта (кнопки активны), заливка от
	//   GaugeTimerStartReal (FPlatformTime::Seconds() владельца); исход по таймеру делает владелец (см. FAsdaMsgBoxEntry).
	UUserWidget* ShowGameConfirm(const FString& Message, TFunction<void(bool)> OnResult, float GaugeGateSec = 0.f,
		int32 OkLocId = 2288, int32 CancelLocId = 2289, int32 EtalonNoticeId = AsdaMsgBox::kNoKey, int32 EtalonKind = 1,
		float GaugeTimerSec = 0.f, double GaugeTimerStartReal = -1.0);

	// ★★ ИГРОВОЙ ОДНОКНОПОЧНЫЙ msgbox — эталон `ONEBUT_MSGBOX` 1500 (`CMsgBox_SetContent` contentKind 0,
	//   ui_core.c:55951): текст `ONEBUT_TEXT` 1501 (rect 270×100, TextAlign 5 = центр/центр), кнопка
	//   `ONEBUT_BUT00` 1503 с подписью loc 2288 «ОК», полоса `ONEBUT_GAUGE*` 1502 скрыта при init
	//   (ветка kind 0 её не показывает).
	//   ★ D2 (аудит `code_dedup_audit_2.md`): раньше это полотно собиралось ТРИЖДЫ (снасть, книга
	//   навыков, самоцветы) почти одинаковыми ~50 строками, и копия самоцветов при этом ЗАБЫВАЛА
	//   и подпись кнопки, и обработчик закрытия — окно нечем было закрыть. Теперь точка одна,
	//   рядом с двухкнопочным `ShowGameConfirm`, и реле закрытия держит сама оконная подсистема.
	//   Пустой текст ⇒ ничего не показываем (как и все три прежние копии). WBP не загрузился ⇒
	//   текст уходит в ленту сообщений, чтобы игрок отказ всё-таки увидел.
	//   Возвращает виджет окна (или nullptr).
	//   ★ Аудит №3: `FallbackCategory` — канал ленты для случая «WBP не загрузился». Прежние копии
	//   писали отказ в СВОЙ канал (книга навыков — в `SkillRefused`), и единая точка не должна была
	//   этот смысл терять. По умолчанию `ItemRefused` — как у первых двух сведённых копий.
	//   ★★★ `OnOk` (опц.) — ДЕЙСТВИЕ на кнопку ОК ПОМИМО закрытия окна. Нужен 4-й сведённой копии —
	//   модалке «вошёл в мир мёртвым» (`AsdaAfterDeathSubsystem`), у которой ОК запускает воскрешение
	//   4054. Раньше та копия собирала окно САМА и звала `OnClicked.Clear()` на ОБЩЕМ `ONEBUT_MSGBOX`,
	//   СТИРАЯ реле-владельца этой точки (порча чужого состояния, `fix_helpers_dedup.md`). Теперь
	//   действие приходит колбэком, а реле держит подсистема окон (как для всех остальных вызовов).
	//   `ZOrder` (опц., по умолчанию 0 — как у трёх прежних копий) — модалке смерти нужен 800
	//   (поверх HUD и нотисов).
	//   ★★★ ОЧЕРЕДЬ (26.09): `EtalonNoticeId` — noticeId эталонного `CItem_BuildTooltipText(ui, 0, ID, …)`;
	//   по умолчанию −1 (так зовут 556 из 674 эталонных показов ONEBUT). Пока в очереди есть (0, ID), новый
	//   показ с той же парой отбрасывается молча (`objList_ContainsPair@0xD6E1D0`) и возвращает nullptr.
	UUserWidget* ShowGameMessage(const FString& Message, int32 OkLocId = 2288 /*AsdaLoc::kBtnOk*/,
		EAsdaMsgCategory FallbackCategory = EAsdaMsgCategory::ItemRefused,
		TFunction<void()> OnOk = nullptr, int32 ZOrder = 0, int32 EtalonNoticeId = -1);

	// ★ Раскладка текста 1:1 по эталону TextAlign 5 = hCENTER+vCENTER (font_caption_layout.md §3):
	//   x = центр rect − textW/2 (по строкам, = Justification Center), y = центр rect − lineH·N/2
	//   (= autosize-слот + alignment 0.5/0.5 в центре rect). Импортёр кладёт Text-контрол на ty=top+ (h−14)/2
	//   (одна строка 14 px) → истинный центр rect = (slotL + slotW/2, slotT + 7). WrapW>0 → перенос по
	//   авторской ширине (WrapTextAt; AutoWrap в CanvasPanel — закон ui-autowrap-canvas-gotcha), 0 → одна строка.
	//   Идемпотентно (повторный вызов геометрию не трогает — слот уже autosize).
	static void CenterTextBlockInSlot(class UTextBlock* Tb, float WrapW = 0.f, FVector2D ExtraOffset = FVector2D::ZeroVector);

	// ★ 09.10.2026 (СРЕДН-1 ревизии autoshovel_fishbooks): тело окна-сообщения (TWOBUT_TEXT / ONEBUT_TEXT /
	//   GIFT_TEXT00) с тегами `<FC=…>` — через единую точку `UAsdaRichText::ApplyFormatted`, с раскладкой
	//   `CenterTextBlockInSlot` (центр/центр, перенос по WrapW). Возвращает живой узел (nullptr — узла нет).
	UWidget* SetMsgBoxBodyFormatted(UUserWidget* Box, const TCHAR* NodeName, const FString& Raw, float WrapW, bool bForceWhite);
	// ★ 09.10.2026: видимый текст узла тела (UTextBlock или URichTextBlock без разметки UE) — для самотестов.
	static FString ReadMsgBoxShownText(const UWidget* W);

	// Закрыть текущий диалог подтверждения и вызвать OnResult(bConfirmed) один раз.
	UFUNCTION(BlueprintCallable, Category="AsdaWindow")
	void CloseConfirm(bool bConfirmed);

	// ══════ ★★★ ЗВУКИ ОКОН-СООБЩЕНИЙ ONEBUT 1500 / TWOBUT 1520 — ОБЩАЯ ТОЧКА (волна В5 ремесла, W4-1: 1:1 как оригинал) ═══
	//   Эталон играет их в ОБЩИХ функциях менеджера окон-сообщений, то есть у ВСЕХ систем (ревизия В4, второй метод — байты:
	//   в образе ровно три `push 19h` + вызов звука, все внутри окна-сообщения):
	//     • 25 — показ: `CItem_BuildTooltipText@0xD6E790` (имя IDA врёт; item_inventory.c:51589), показ из очереди
	//       `CMsgBox_SetContent` (0xD73645) и `uiWindow_Open` (0xD73DA8) → порт: `ShowGameMessage` / `ShowGameConfirm`;
	//     • 4 — ответ кнопкой: `OnSendItemUpgrade@0xD74180` (имя врёт) на ОК 1503, «Да» 1523, «Нет» 1524 (ui_core.c:56957,
	//       57052, 57682); ESC на TWOBUT = «Нет» 1524 → порт: реле ONEBUT_BUT00 и `CloseConfirm` (им же зовёт ESC);
	//     • 5 — закрытие: `CGameUI_CloseWindow@0xD73DD0` играет его ВСЕГДА (game_main.c:9860/9888), кто бы ни закрывал →
	//       порт: `CloseWindow` для ONEBUT_MSGBOX / TWOBUT_MSGBOX (любой вызывающий, включая программное закрытие).
	//   Своих вызовов этих звуков у систем быть не должно (у ремесла они удалены в В5). Полосы-таймера (1502/1522 → ОК/«Нет»)
	//   у игровых окон порта нет (`ShowGameConfirm` исхода по таймеру не делает) — ветка таймера здесь не нужна.
	//   Молча (без 4/5): замена окна новым сообщением и сброс сессии — у эталона таких закрытий нет (очередь / перезагрузка).
	//   ★ Волна В6 ремесла (ревизия В5 СРЕДН-2): семейство менеджера `CMsgBoxMgr` шире 1500/1520 — ещё 1530 `SOWELL_SELBOX`,
	//   1545 `THREEBUT_MSGBOX`, 1552 `GIFT_MSGBOX`, 1559 `SHELLITEM_PURCHASE_BOX` (`CMsgBoxMgr_InitControls`, ui_core.c:55692-55781).
	//   Их кнопки в `OnSendItemUpgrade` играют 4 (1532/1533 ui_core.c:57887/57892, 1549-1551 :57897-57951, 1554/1555
	//   :57958-57984, 1561/1562 :57991/58037). Показ 1530 (`sowelSlot_DrawStones`, item_inventory.c:50283) и 1559
	//   (`upgradeUiFillConfirmPanel`, item_enhance.c:2612) идёт МИМО `CMsgBox_SetContent` — без 25; закрытие
	//   (`ui_HideControlsAndRefresh` ui_core.c:56279, `uiPopupClose` :58661, `uiWindowMgrCloseAndHide` :58669) — без 5.
	//   Порт: 1530 — `UAsdaItemTooltipSubsystem`, 1559 — `UAsdaPurchaseBoxSubsystem`; их кнопки зовут `PlayMsgBoxSfx(4)`.
	//   ★ 06.10.2026 (волна 2-Г): 1552 перенесено (виды 59/60 очереди, `ShowGiftMsgBox`, кнопки 1554/1555 → звук 4);
	//   1545 — мёртвое в оригинале (вид 9 никто не ставит: скан 858 вызовов + `ConfirmDialog_Setup` без вызывающих).
	// ══ ★ ОТСТУПЛЕНИЯ И ПРИВЕДЕНИЕ К ЭТАЛОНУ (волна В6, ревизия В5 МЕЛК-2/МЕЛК-3) ══
	//   • Двойной 5: ветки «Да» 1523 с кодами 0x4000 (подтверждение меню смерти) и 0x97B (удаление звания, 6069) зовут
	//     `CGameUI_CloseWindow` сами и ЕЩЁ РАЗ через `LABEL_300` (ui_core.c:57068-57075, :57675) — 5 звучит дважды.
	//     Порт повторяет: первое закрытие вида делают колбэки этих окон (`AsdaAfterDeathSubsystem`, `AsdaTitleSubsystem`)
	//     ДО своего действия (редакция 1), второе — общая точка после ответа — `RepeatMsgBoxClose` (с очередью это ещё и повторный возврат записи, 25, как у эталона). Третья такая ветка 0x42 (`itemTooltipBuildOrNotice` → 6140, item_inventory.c:67092) в порту
	//     отсутствует вместе с системой.
	//   • Звук без окна: при загрузке мира эталон зовёт `CGameUI_CloseWindow(5001/5002/0)` (game_main.c:3547-3549) — 5
	//     трижды при любом состоянии окон. Порт: `UAsdaLoadManagerSubsystem::StartTargetLoad` (начало загрузки) → `MapLoadCloseMsgBoxes`.
	//     `Recv_ExchangeRequestResult` (network.c:14338) — окна обмена в порту нет.
	//   • Выход из игры: ветки ОК 0x3717/0x3746 (ui_core.c:57028) закрытие не зовут — без 5. В порту эти нотисы
	//     (14103/14150) показывает своё окно `AsdaDisconnectPopupSubsystem`, не ONEBUT — звуков менеджера у него нет.
	//   • ОЧЕРЕДЬ сообщений с 26.09 ЕСТЬ (раздел «ОЧЕРЕДЬ ОКОН-СООБЩЕНИЙ» ниже): новый показ ложится сверху, прежний
	//     остаётся в очереди и возвращается после ответа (25 на каждый показ и возврат, 5 на каждое закрытие).
	int32 GetMsgBoxSfxCount(int32 SoundId) const { const int32* N = MsgBoxSfxCounts.Find(SoundId); return N ? *N : 0; }   // самотест: сколько раз сыграно
	void ResetMsgBoxSfxCounts() { MsgBoxSfxCounts.Reset(); }                                                                 // самотест: обнулить счётчики
	// Звук окна-сообщения ОБЩЕЙ точкой: играет id и считает его для самотеста. Публичен с волны В6: им звучат кнопки
	//   1530/1559 (4) и повторный `CGameUI_CloseWindow` веток 0x4000/0x97B (5). Свои вызовы 25/4/5 мимо него запрещены.
	void PlayMsgBoxSfx(int32 SoundId, const TCHAR* Why);
	// Закрыть окно БЕЗ звука 5 менеджера сообщений (сброс сессии: у эталона такого закрытия нет — клиент перезагружается).
	//   Для остальных окон — то же, что `CloseWindow`.
	void CloseWindowSilently(FName WindowId);

	// ══════ ★★★ ОЧЕРЕДЬ ОКОН-СООБЩЕНИЙ ONEBUT 1500 / TWOBUT 1520 (26.09, решение владельца «завести» → 1:1) ═══
	//   РЕВЕРС: `Reports/msgqueue/msgqueue.md`. Эталон — менеджер `CMsgBoxMgr` (`g_pItemTooltipUI`):
	//   • Хранилище: указатель `+0x3340` на массив 40-байтовых записей ёмкостью 10 (`recordVec_alloc@0xD7DED0`),
	//     это СТЕК: добавление в конец (`vecPushBack40@0xD7E030`), снятие С КОНЦА (`recordVec_popBack@0xD7E0C0`).
	//   • Показ `CItem_BuildTooltipText@0xD6E790` (item_inventory.c:50510): (1) `uiExchangeListRemoveItem` (обмен, id 2372);
	//     (2) пара (msgClass, noticeId) уже в стеке → показ ОТБРОШЕН целиком, без звука (`objList_ContainsPair@0xD6E1D0`);
	//     (3) запись в конец стека; (4) СРАЗУ рисует её в окне своего вида поверх прежнего содержимого, модально
	//     (`helper_setupFields`), (5) звук 25. Прежняя запись остаётся в стеке под новой.
	//   • Ответ кнопкой `OnSendItemUpgrade@0xD74180` (ui_core.c:56946-56952): ПЕРВЫМ делом снимает запись с конца
	//     стека и делает её текущей (вид/id), звук 4, действие по id, затем `CGameUI_CloseWindow(вид)`.
	//   • `CGameUI_CloseWindow@0xD73DD0` (game_main.c:9788-9889): прячет окно своего вида; текущий id 5001/5002
	//     (обмен) → снять ещё одну запись и снять модальность; иначе стек пуст → снять модальность, не пуст →
	//     показать запись из стека `CMsgBox_SetContent` (звук 25; вид 2 — `sowelSlot_DrawStones`); звук 5 ВСЕГДА.
	//   • ★ ДЕФЕКТ ОРИГИНАЛА: для повторного показа берётся ПЕРВАЯ запись (`sub_D7E120` = начало массива), а кнопка
	//     снимает ПОСЛЕДНЮЮ. При 3+ записях окно показывает не ту, что будет отвечена (A,B,C → видно C, A, A;
	//     B не видно никогда, «ОК» по A исполняет B). Порт показывает ПОСЛЕДНЮЮ (то, что снимет кнопка) — при
	//     1-2 записях поведение совпадает с эталоном байт в байт. Откат: `asda.MsgBox.QueueFixes 0`.
	//   • Сброс: выход в выбор персонажа (TWOBUT id 4, ui_core.c:57195-57203) выгребает стек целиком; при разрыве
	//     оригинал закрывает клиент. Порт: полный сброс сессии чистит очередь молча, без колбэков. Смена карты
	//     очередь СОХРАНЯЕТ: `CGameUI_CloseWindow(5001/5002/0)` загрузки (game_main.c:3547-3549) показывает запись
	//     из стека снова — порт возвращает её после `CloseAll` входа в мир.
	//   • Смерть: вход своего персонажа на карту (`char_onLocalPlayerMapEntry`, entity_char.c:53192) зовёт
	//     `uiCloseOnExchangeItemType@0xD79D30` (ui_core.c:58576, имя врёт) — см. `CloseDeathMsgBoxOnMapEntry`.
	//   Молча (без 4/5) очередь снимает только полный сброс сессии.

	// Ответ на верхнее окно-сообщение (реле кнопок, ESC на TWOBUT, программное «нажатие» вызывающего):
	//   снять запись, 4, действие вызывающего, закрытие вида (5 + возврат следующей записи с 25).
	//   bTwoBut — какое окно отвечает; bYes — «ОК»/«Да» (true) или «Нет» (false; для ONEBUT не бывает).
	void AnswerMsgBox(bool bTwoBut, bool bYes);
	// ══════ ★ 06.10.2026 (волна 2-Г): ОКНО ВВОДА ИМЕНИ `GIFT_MSGBOX` 1552 — виды 59/60 той же очереди ══════
	//   ★ ИМЯ ОКНА ВРЁТ: это не «подарок». Его ставит ТОЛЬКО `CItemManager_UseInventoryItem@0xA0FDF0`
	//   (item_inventory.c:3043/3225; второй метод — скан всех 858 вызовов `CItem_BuildTooltipText` с константным видом:
	//   59/60 толкают ровно два места 0xA0FF61/0xA0FF26): свиток ПРИЗЫВА (`special_itemlist.SubType` 64 → вид 59, loc
	//   104000) и свиток ПЕРЕМЕЩЕНИЯ к игроку (SubType 38 → вид 60, loc 12832). Показ — `CItem_BuildTooltipText` /
	//   `CMsgBox_SetContent` (ui_core.c:56596-56607): текст 1553, поле 1558 очищено, окно модально; при ПЕРВОМ показе ещё
	//   фокус ввода в 1558 (`uiPanel_SetFocusChildren(g_pChatWnd, 0)` + `CControl_SetFocus(1558, 1)`). Кнопки (init
	//   ui_core.c:55976-55979): 1554 = loc 2288 «ОК», 1555 = loc 2289 «Отмена»; Enter в 1558 (67587) = 1554
	//   (`uiItemUpgradeWnd_OnCommand`, ui_core.c:58419). Ответ (`OnSendItemUpgrade` case 1554/1555, :57955-57989): снять
	//   запись, звук 4, действие (у порта — `OnTextResult(bOk, текст 1558)`), снять фокус 1558, закрыть вид (5 + возврат).
	//   ESC окно НЕ закрывает (флага +552 у 1552 эталон не ставит). Kind — 59 или 60; Text — тело 1553.
	UUserWidget* ShowGiftMsgBox(int32 Kind, const FString& Text, TFunction<void(bool, const FString&)> OnTextResult);
	// Ответ на окно 1552 (реле 1554/1555, Enter в 1558, программное «нажатие» самотеста).
	void AnswerGiftMsgBox(bool bOk);
	// Текст поля 1558 (пусто — окна нет). Самотест и лог.
	FString GetGiftMsgBoxEditText() const;
	// Порт `uiCloseOnExchangeItemType@0xD79D30`: если открыто ONEBUT или TWOBUT и в очереди есть запись с id
	//   2370/2371/2376 (модалки смерти) — снять её, спрятать ОБА окна, снять модальность; молча (без 4/5).
	//   Возвращает true, если запись нашлась.
	//   bRequireVisible=false — сброс сессии при смене карты (виджеты уже снесены травелом, гейт видимости неприменим).
	bool CloseDeathMsgBoxOnMapEntry(bool bRequireVisible = true);
	// ★ 22.10.2026: порт `uiComboListCloseTradePanel@0xD79F10` (ui_core.c:58614; имя IDA ВРЁТ — к торговле отношения не
	//   имеет). Зовёт ТОЛЬКО своё 4056 (`GameNet_OnLocalPlayerMapEntry`, game_main.c:12923): если TWOBUT 1520 виден и в
	//   очереди есть запись с id 31 — снять её, спрятать TWOBUT, снять модальность; молча. Id 31 у TWOBUT — вопрос
	//   «воскреснуть?»: «Да» шлёт 4054 `OP_RECV_CHAR_REGEN` (OnSendItemUpgrade case 0x1F, ui_core.c:57448-57551). Флаги
	//   персонажа (`helper_clearFlagBits(0x4000)`, `+0x32DE4`, `+0x48`) у порта живут в подсистеме смерти и снимаются её
	//   `OnCharRevived`. Возвращает true, если запись нашлась.
	bool CloseMsgBoxIdOnMapEntry(int32 Id, bool bTwoButOnly, const TCHAR* Why);
	// Показана ли сейчас запись с этим id (любого вида) — эталонное `displayNoticeId == Id` (`+0x32FC`).
	bool IsMsgBoxDisplayedId(int32 Id) const { return MsgBoxQueue.Num() > 0 && MsgBoxCurId == Id; }
	// Есть ли в очереди запись (Kind, Id) — порт `objList_ContainsPair@0xD6E1D0` (для гейтов вызывающих).
	bool IsMsgBoxQueued(int32 Kind, int32 Id) const;
	// Есть ли в очереди запись вида Kind (id не сравнивается) — порт `listContainsId@0xD6E3C0` (entity_char.c:46296).
	bool IsMsgBoxKindQueued(int32 Kind) const;
	// Сколько записей в очереди (самотест, дамп).
	int32 GetMsgBoxQueueNum() const { return MsgBoxQueue.Num(); }
	// ★ 25.10.2026 (Enter = кнопка согласия): номер показанной записи (верх стека; 0 — очереди нет) — Enter сверяет его на отпускании,
	//   чтобы подмена содержимого того же окна во время удержания не подтвердила чужой вопрос.
	uint32 GetDisplayedMsgBoxSerial() const { return MsgBoxQueue.Num() ? MsgBoxQueue.Last().Serial : 0u; }
	// Показанная запись помечена «без Enter» (`FAsdaMsgBoxEntry::bNoEnter`).
	bool IsDisplayedMsgBoxNoEnter() const { return MsgBoxQueue.Num() > 0 && MsgBoxQueue.Last().bNoEnter; }
	// Текст верхней записи очереди (самотест): пусто, если очередь пуста.
	FString GetMsgBoxQueueTopText() const { return MsgBoxQueue.Num() ? MsgBoxQueue.Last().Text : FString(); }
	// ★ 29.09.2026 (волна О2 торговли, ревизия О1 МЕЛК-3): верхняя запись очереди — (Kind, Id), то есть она СЕЙЧАС на экране
	//   (эталонный `displayNoticeId`, по которому `uiProgressTickBuffAction` решает, чьё окно закрывать).
	bool IsMsgBoxTop(int32 Kind, int32 Id) const { return MsgBoxQueue.Num() > 0 && MsgBoxQueue.Last().Kind == Kind && MsgBoxQueue.Last().Id == Id; }
	// ★ 30.09.2026 (волна О3 торговли, ревизия О2 МЕЛК-9): (Kind, Id) — ТЕКУЩЕЕ окно менеджера, то есть эталонная пара
	//   `+0x32F8/+0x32FC` (`displayNoticeId`). При `asda.MsgBox.QueueFixes 0` на экране ПЕРВАЯ запись очереди, а не верхняя —
	//   `IsMsgBoxTop` тогда отвечает не про показанное окно; эта проверка верна при обоих значениях ключа.
	bool IsMsgBoxDisplayed(int32 Kind, int32 Id) const { return MsgBoxQueue.Num() > 0 && MsgBoxCurKind == Kind && MsgBoxCurId == Id; }
	// Снять молча (без колбэков и звуков) все записи (Kind, Id) — для состояния, которое подсистема сама сбрасывает
	//   (зов друга (1, 38) не переживает смену карты — решение порта Р5). Если верхняя запись ушла, окно
	//   показывает следующую без звука. Возвращает число снятых записей.
	//   bRedisplayNow=false — смена карты: следующую запись не рисовать посреди травела (её вернёт `CloseAll`).
	int32 RemoveMsgBoxEntries(int32 Kind, int32 Id, const TCHAR* Why, bool bRedisplayNow = true);
	// Снять ВСЮ очередь молча, без колбэков и звуков, закрыть оба окна (полный сброс сессии; самотесты между шагами).
	void ClearMsgBoxQueue(const TCHAR* Why);
	// ★ 27.09.2026 (`Reports/owner_0927/who_clan_fix.md`): порт `itemBuyPanelRefreshOrHide@0xD7A110` (имя IDA ВРЁТ —
	//   это метод менеджера сообщений `CMsgBoxMgr`, зовётся на `g_pItemTooltipUI`; item_inventory.c:51715-51742, дизасм
	//   0xD7A110…0xD7A221). Эталон зовёт его ПЕРЕД нотисом отказа «Друг» 12435 (network.c:11775 — 4136, :11984 — 4106,
	//   social.c:3434 — 4108, item_inventory.c:7360 — 4111) и перед 12433 «глава не может выйти» (network.c:12240 — 4116).
	//   ★ Поправка 27.09.2026 (`friend_gate_12435.md`): прежний список «:12240 — 4110; social.c:3434; item_inventory.c:7360»
	//   путал опкоды и нотисы — у 4116 (не 4110) нотис 12433, а не 12435; social.c/item_inventory.c — клан 4108/4111
	//   (имена IDA `partySendInviteAuth`/`itemSendSplitOrMoveRequest` ВРУТ: это не отряд и не разделение предмета):
	//     • стек `+0x3340` не пуст → снять ВЕРХНЮЮ запись (`recordVec_popBack`, без действия и звуков) и снять
	//       модальность (`sub_12B2900` на `g_pWindowMgr`); окно не прячет — следующий нотис рисуется в него же;
	//     • стек пуст → спрятать шесть панелей менеджера 1500/1520/1530/1545/1552/1559 (слоты +4/+0x18/+0x4C/+0x30/
	//       +0x80/+0x9C, сверено по `CMsgBoxMgr_InitControls@0xD6AD20`).
	//   [ПОРТ] Снятая запись уносит и свой виджет (у порта модальность = видимое модальное окно; без записи окно стало бы
	//   «сиротой»). 1530 — `UAsdaItemTooltipSubsystem`, 1559 — `UAsdaPurchaseBoxSubsystem`; 1552 — очередь (волна 2-Г); 1545 мёртвое.
	//   Вызывать непосредственно перед показом нотиса. Why — для лога.
	void DropTopMsgBoxBeforeNotice(const TCHAR* Why);
	// Шаг загрузки мира `g_LoadProgress == 0`: `CGameUI_CloseWindow(5001)`, `(5002)`, `(0)` (game_main.c:3547-3549) —
	//   три закрытия менеджера: у каждого 5, а при непустой очереди — ещё и возврат записи (25). Заменяет прежний
	//   `PlayMapLoadCloseSfx` (три голых 5).
	//   ★ 22.10.2026: bDeferDisplay=false — следующая запись показывается сразу (порядок эталона шага 0: после закрытия вида 0
	//   виден вопрос 38 — его снимает `AsdaEntryWindows::ApplyMapLoadWindowCloses`); true — как прежде, вернётся после входа.
	void MapLoadCloseMsgBoxes(bool bDeferDisplay = true);
	// ★ 22.10.2026: окна-сообщения шага 0 этой загрузки уже закрыты в момент приёма пакета перехода — `StartTargetLoad`
	//   (`ConsumeMapLoadMsgBoxesClosed`) их второй раз не закрывает (у эталона шаг 0 один на загрузку).
	void MarkMapLoadMsgBoxesClosed() { bMapLoadMsgBoxesClosed = true; }
	bool ConsumeMapLoadMsgBoxesClosed() { const bool b = bMapLoadMsgBoxesClosed; bMapLoadMsgBoxesClosed = false; return b; }
	// ★ 22.10.2026: пересчёт режима ввода после своего воскрешения (мир уже показан) — без смены фокуса. Нажатые клавиши
	//   сбрасываются, только если пересчёт — переход модалка ↔ мир (`asda.UI.ModalFlushKeys`, ревизия WASD М-5).
	void RefreshModalInputNow() { RefreshModalInput(); }
	// ★ 22.10.2026: идёт служебное переоткрытие окон после травела (`ReopenTravelKeptWindows`) — владельцы не шлют запросов
	//   «первого открытия» (окно персонажа — 5466): у оригинала окно не закрывалось и запроса не было.
	bool IsReopeningTravelKept() const { return bReopeningTravelKept; }
	// ЛИШНИЙ `CGameUI_CloseWindow(Kind)` веток «Да» 0x4000/0x97B (ui_core.c:57068/57071, до действия; второй — `LABEL_300`
	//   :57675 — делает общая точка): колбэк ответа зовёт его ДО своего действия — спрятать окно вида, вернуть запись
	//   очереди (25) и сыграть 5. Why — для лога.
	void RepeatMsgBoxClose(int32 Kind, const TCHAR* Why);
	// Дамп очереди в лог (консоль `asda.MsgBox.DumpQueue`).
	void DumpMsgBoxQueue() const;

	// ══════ ★★★ МОДАЛЬНЫЙ ДИАЛОГ ВВОДА ТЕКСТА — эталонный `SHOUTING_WIN` (IDNum 1580) ═══════════
	// РЕВЕРС: `Reports/chat_tab_editor_2590_re.md` §5.1. Своего окна ввода у подсистем НЕТ —
	//   игра переиспользует окно «крика»: `uiConfirmDialog_Setup @0xD7D7D0` кладёт режим в
	//   `CItemTooltipUI+0x3490`, показывает 1580, даёт фокус полю 1583 и заполняет:
	//     1581 `Shouting_TitleTxt` — ЗАГОЛОВОК, 1582 `Shouting_GuideTxt` — ПОДСКАЗКА,
	//     1583 `Shouting_InputBox` — поле (лимит поля 66, `ctrlEditSetTextLength`, ui_core.c:55877).
	//   Кнопки: 1584 OK (loc 2288 «ОК»), 1585 Отмена (loc **2378** «Отмена» — НЕ 2289!).
	//   Текст нигде не хранится: читается из 1583 в момент нажатия ОК.
	//   Режимы `nConfirmMode`: 0 = ввод для использования предмета, 1 = переименовать вкладку чата,
	//   2 = добавить вкладку чата. Режим — это ДИСПЕТЧЕР эталона; в порте его роль играет колбэк.
	//   OnResult(bAccepted, Text) вызывается РОВНО ОДИН раз. MaxBytes > 0 — жёсткий стоп набора
	//   в БАЙТАХ CP1251 (эталон меряет `strlen`, а не символы, см. лимит имени вкладки = 10 байт).
	UUserWidget* ShowInputDialog(const FString& Title, const FString& Guide, const FString& InitialText,
		int32 MaxBytes, TFunction<void(bool, const FString&)> OnResult);
	// Закрыть диалог ввода и отдать результат (аналог `uiWnd_CloseAndReleaseSlot`, режим → −1).
	UFUNCTION(BlueprintCallable, Category="AsdaWindow")
	void CloseInputDialog(bool bAccepted);

	// ═══════ ★★★ ОКНО ВВОДА КОЛИЧЕСТВА — эталонный `NUMPAD_WIN` (IDNum 1410, `CNumPadDialog`) ═══════
	// РЕВЕРС: `Docs/UI/Common/NUMPAD_WIN.md` (все контролы CONFIRMED) + `uiOpenContextPopupWithHandlers
	//   @0xDA55A0` (ui_core.c:61746). Эталон держит ОДИН экземпляр окна (`g_pNumPadDialog`) и раздаёт ему
	//   пару колбэков OK/Cancel по режиму `a3` (0..12) — покупка, продажа, обмен, склад, корзина,
	//   квик-слот, апгрейд, использование предмета. В порте роль диспетчера режимов играет колбэк,
	//   ровно как это уже сделано для `ShowInputDialog` (там режимы 0/1/2 эталона свёрнуты в OnResult).
	//
	// Контролы: панель 1410, подпись 1411 `NUMPAD_MESSAGE`, поле 1412 `NUMPAD_NUMBOX`, MAX 1413,
	//   DELETE 1414, OK 1415, CLOSE 1416, цифры 1400..1409.
	//   ⚠ Имена цифровых кнопок СМЕЩЕНЫ относительно самих цифр: `NUMPAD_NO09` = «0»,
	//   `NUMPAD_NO00` = «1», … `NUMPAD_NO08` = «9» (idnum − 1400 = цифра).
	//
	// Правила эталона, воспроизводимые здесь 1:1:
	//   • подпись окна — локализованный нотис **12124** (`UI_FormatLocalizedNotice`, ui_core.c:61774);
	//   • не больше **10 знаков** в поле (`uiNumPadDialogProc`, ui_core.c:62062-62074);
	//   • после ЛЮБОГО изменения — обрезка до `MaxQuantity` (`uiClampQuantityToMax@0xDA6410`);
	//   • MAX подставляет `MaxQuantity`, DELETE снимает один символ с конца;
	//   • Enter в поле = ОК, ESC/CLOSE = отмена (`uiQuantityNumPadDialogProc@0xDA6300`);
	//   • при закрытии поле очищается, звук 6; при открытии звук 5 (`uiDialogClose@0xDA5A30`);
	//   • окно встаёт **у курсора**; исключение — режим 6 эталона, он центрируется по экрану
	//     (ui_core.c:61836-61841) ⇒ параметр `bCenterOnScreen`;
	//   • [ИСПРАВЛЕНИЕ] 08.10.2026 (пакет 3, СРЕДН-1): у курсора окно прижимается к краю экрана — у оригинала клампа нет, окно
	//     у нижнего ряда/правого края инвентаря уходило за экран; откат `asda.UI.NumPadClampToScreen 0`.
	//
	// OnResult(bAccepted, Qty) вызывается РОВНО ОДИН раз. При отмене Qty = 0.
	// ★ Эталонные OK-хендлеры проверяют введённое на диапазон `1..maxQuantity`: значение вне диапазона
	//   операцию не запускает. Эту проверку делаем ЗДЕСЬ, чтобы её не дублировал каждый вызывающий.
	// ★★ 08.10.2026 (ревизия В3 рыбалки, ВАЖН-1) — `bStrictMax`: СТРОГИЙ максимум эталона `uiClampQuantityToMax@0xDA6410`
	//   (g:400586-400594): поле режется при `введённое > максимум` ДАЖЕ при максимуме ≤ 0 (тогда в «0»/«максимум»), и каждый
	//   такой срез пишет в чат нотис 6835 (цвет −11514, 10 с). ОК со значением вне `1..максимум` (при максимуме ≤ 0 — любой)
	//   операцию НЕ запускает. Без флага — прежнее поведение общей точки (максимум ≤ 0 = «без предела», 6835 не пишется):
	//   прочие режимы нумпада (NUMPAD_FLOW.md §2) им не задеты. Сейчас флаг ставит режим 10 (книга рыбака, окно 74200).
	UUserWidget* ShowNumPad(int32 MaxQuantity, TFunction<void(bool, int32)> OnResult,
		bool bCenterOnScreen = false, bool bStrictMax = false);

	// Закрыть окно количества и отдать результат (эталон `uiDialogClose@0xDA5A30` + вызов колбэка режима).
	UFUNCTION(BlueprintCallable, Category="AsdaWindow")
	void CloseNumPad(bool bAccepted);

	// Открыто ли окно ввода количества (эталонный признак `pDialog[17]`).
	UFUNCTION(BlueprintCallable, Category="AsdaWindow")
	bool IsNumPadOpen() const;

	// ★ Волна 2-Б (06.10.2026, ревизия М2): закрыть окно количества БЕЗ колбэка режима — эталонный `uiDialogClose(g_pNumPadDialog)`
	//   из головы `UI_resetAllWindowsAndLeave_5045` (ui_core.c:61170): ни OK-, ни Cancel-обработчик режима не зовётся. Who — для лога.
	void DismissNumPad(const TCHAR* Who);

	// ★★★ БАЗОВЫЙ режим ввода экрана — тот, что восстанавливается, когда модальных окон НЕТ.
	//   Меню-экраны (логин/сервер/char-select) = чистый UI → UIOnly. МИР = GameAndUI: иначе PlayerController
	//   вообще не получает клавиши (IsInputKeyDown в AAsdaPlayerController::Tick) и клики (BindKey LMB),
	//   т.е. отваливаются и WASD, и click-to-move. РЕГРЕССИЯ 2026-08-01: любое ОТКРЫТИЕ окна в мире
	//   (мини-карта 1702) звало RefreshModalInput → ветка «модальных нет» ставила UIOnly поверх мира.
	//   Ставится один раз владельцем фазы: AAsdaLoginGameMode::BeginPlay → UIOnly,
	//   UAsdaWorldSubsystem::RevealWorld → GameAndUI.
	UFUNCTION(BlueprintCallable, Category="AsdaWindow")
	void SetBaseInputMode(bool bWorldGameAndUI);

private:
	// Зоны интерфейса: id → провайдер прямоугольника (живая геометрия владельца).
	//   НЕ UPROPERTY: делегаты держат слабые ссылки на UObject-владельцев; владелец обязан
	//   снять регистрацию в ResetWidgets/Deinitialize.
	TMap<FName, FAsdaUiZoneRect> UiZones;
	// Обработчики колеса по id окна (см. RegisterWheelHandler). Не UPROPERTY по той же причине,
	//   что и UiZones: делегат держит слабую ссылку, владелец обязан сняться сам.
	TMap<FName, FAsdaUiWheelHandler> WheelHandlers;
	// Закрыватели окон NPC (порт `UI_resetAllWindowsAndLeave_5045`): игрок пошёл ⇒ окна закрылись.
	TMap<FName, FAsdaNpcWindowCloser> NpcWindowClosers;
	// Верхние кромки окон-якорей в базовых px (см. SetWindowAnchorTop).
	TMap<FName, float> WindowAnchorTops;

	// Активные всплывающие слои «поверх всего» (см. PushTopmost). НЕ UPROPERTY по той же причине,
	//   что и UiZones: делегат держит слабую ссылку на владельца, владелец обязан снять слой сам.
	struct FTopmostClaim
	{
		FName Owner;                 // id окна-владельца в реестре Windows
		FAsdaUiZoneRect Rect;        // прямоугольник самого всплывающего слоя (живая геометрия)
		int32 SavedZ = 0;            // z владельца до подъёма — вернём на PopTopmost
	};
	TMap<FName, FTopmostClaim> TopmostClaims;

	// false = UIOnly (меню-экраны), true = GameAndUI (мир). См. SetBaseInputMode.
	bool bBaseInputGameAndUI = false;
	// ★ 10.10.2026 (modal_lock3, самотест МЕЛК-3): имитация потери контроллера — `RefreshModalInput` ведёт себя так, будто
	//   локального контроллера нет (смена карты). Ставит только `DebugSimulateNoController`; в игре всегда false.
	bool bDebugNoController = false;

	// Реестр id→(виджет,z,модаль). UPROPERTY = GC-рут открытых окон.
	UPROPERTY(Transient)
	TMap<FName, FAsdaWindowEntry> Windows;

	// ★★★ Росписи «как закрыть себя по-игровому» (см. `RegisterWindowCloser`). НЕ UPROPERTY: делегат
	//   рефлексии не подлежит, а от висячих указателей защищает сам бинд через `CreateUObject`
	//   (`FSimpleDelegate::ExecuteIfBound` у мёртвого владельца молча ничего не делает).
	//   Порядок вызова = порядок регистрации, поэтому список, а не TMap (ключ в паре — для снятия).
	TArray<TPair<FName, FSimpleDelegate>> WindowClosers;

	// ★★★ Росписи «как закрыть себя по ESC» (см. `RegisterEscCloser`). Отдельный реестр от
	//   `WindowClosers`: у того своя эталонная роль — список `uiCloseAllWindows@0xD61960`.
	//   Порядок здесь не значим (закрывается ровно одно окно, выбранное по z), поэтому TMap.
	//   Не UPROPERTY: делегат рефлексии не подлежит, от висячих указателей защищает сам бинд.
	TMap<FName, FSimpleDelegate> EscClosers;

	// ★ Всплывающие слои-«корневые окна» для прохода ESC (см. `RegisterEscPopup`).
	struct FEscPopup
	{
		FName Owner;                   // id корневого окна-владельца в реестре `Windows` (его z = z слоя)
		TFunction<bool()> IsShown;     // слой сейчас показан (спрашивается у владельца; лямбда со слабой ссылкой)
	};
	TMap<FName, FEscPopup> EscPopups;  // id слоя (имя контрола .JUI) → владелец и видимость

	// ★ 10.10.2026: модальный контрол эталона без флага `bModal` порта (см. `RegisterEscModal` / `RegisterOriginalModal`).
	struct FOrigModal
	{
		FName Owner;                   // корень в реестре `Windows`, которому отдаётся ввод (у корневой росписи — само окно)
		TFunction<bool()> IsShown;     // показан ли контрол внутри владельца (пусто — корневая роспись, смотрится окно)
		TFunction<const UWidget*()> Node;   // ★ modal_lock2: виджет узла (мышь — только в его прямоугольнике); пусто — весь корень
	};
	TMap<FName, FOrigModal> EscModals; // id контрола (.JUI) → владелец и видимость

	// Показан ли модальный контрол росписи (владелец видим и, для вложенного, `IsShown`).
	bool IsOrigModalShown(const FOrigModal& M) const;
	// Верхний показанный модальный контрол без `bModal` (с учётом ключа `asda.UI.ModalLock`); false — нет.
	//   OutOwner — его корень в реестре, OutZ — z корня.
	bool FindTopOrigModal(FName& OutOwner, int32& OutZ) const;
	// Проходит ли окно модальный гейт мыши (порт `+0x1A0`: при модальном контроле мышь получает только он).
	//   `bModal` — как прежде: окна полосы kModalZ; модалка без `bModal` — только её корень и окна полосы kModalZ.
	bool PassesModalMouseGate(FName WindowId, const FAsdaWindowEntry& E) const;
	// ★ 10.10.2026 (modal_lock2, МЕЛК-2 ревизии): то же с точкой мыши. Корень вложенного модального контрола (74000 при 74200)
	//   проходит, только если точка внутри узла (`FOrigModal::Node`) — эталон отдаёт мышь поддереву `+0x1A0`, а не всему окну.
	//   Хит-тест окон (`FindWindowCatchingAt`), захват перетаскивания (`TickDrag`) и колесо (`HandleMouseWheel`).
	bool PassesModalMouseGateAt(FName WindowId, const FAsdaWindowEntry& E, FVector2D Pt) const;
	// Корень, поднятый в полосу приоритетных на время модального контрола без `bModal` (чтобы окна выше него не ловили
	//   клики Slate); NAME_None — подъёма нет. Возвращается на место при снятии модалки (`RefreshModalInput`).
	FName ModalLiftedOwner;

	// ★★★ Окна, которые по эталону ESC ПОГЛОЩАЮТ, но при этом НЕ закрываются
	//   (см. `RegisterEscSwallower`). Сейчас здесь ровно одна запись — `ONEBUT_MSGBOX` 1500.
	TSet<FName> EscSwallowers;

	// Закрыть окно ЕГО СОБСТВЕННЫМ закрывателем (порт события 21). Возвращает имя сработавшего
	//   реестра росписей или nullptr, если росписи нет ни в одном из трёх.
	const TCHAR* ExecuteOwnClose(FName WindowId);

	// ★ Закрыватель ESC для ИГРОВОГО двухкнопочного msgbox `TWOBUT_MSGBOX` 1520 — порт ветки
	//   `uiItemUpgradeWnd_OnCommand@0xD79260`, событие 21, `a3 == 1520`:
	//   `OnSendItemUpgrade(mgr, 1524)` = ТА ЖЕ функция и ТОТ ЖЕ номер кнопки, что у нажатия
	//   `TWOBUT_BUT01` «Отмена» (loc 2289) ⇒ у нас это `CloseConfirm(false)`, то есть ровно то,
	//   что делает реле кнопки «Отмена» в `ShowGameConfirm`.
	void EscCloseGameConfirm();

	// Порт `*(g_pUIManager + 17)`: окна убраны полноэкранной сценой (сейчас это сцена премиум-гачи).
	bool bPlayerWindowsHidden = false;

	// Снимок открытых окон (`g_pNpcWnd` dword [15229..15243]): маска владельцев `EWindowSnapshotOwner` на слот
	//   (0 = флаг не поднят). Реализация — `AsdaWindowSnapshot.cpp`.
	uint8 WindowSnapshot[kWindowSnapshotSlots] = {};
	// ★ 22.10.2026: окна, открытые перед травелом из игрового мира (слоты снимка), — пересобрать после входа на новую карту.
	bool TravelKept[kWindowSnapshotSlots] = {};
	bool bReopeningTravelKept = false;             // идёт `ReopenTravelKeptWindows`
	bool bMapLoadMsgBoxesClosed = false;           // шаг 0 этой загрузки уже закрыл окна-сообщения
	FDelegateHandle PreLoadMapHandle;              // подписка на `FCoreUObjectDelegates::PreLoadMap`
	void OnPreLoadMapForTravelKeep(const FString& MapName);
	// Цикл закрывателей `CloseAllPlayerWindows` идёт (см. `IsClosingAllPlayerWindows`).
	bool bClosingAllPlayerWindows = false;

	// Полноэкранный прозрачный hit-test блокер под модалкой (эталон: диспетчер WindowMgr захватывает ввод
	//   mgr[104] → мышь+клавиатура к нижним окнам заблокированы; SPEC_GAUGE_MODAL.md B3). В UE воспроизводим
	//   блокер на z=(z модалки−1): топовый hit-test-виджет ест клики, не пуская их в нижние окна.
	UPROPERTY(Transient)
	TObjectPtr<class UBorder> ModalBlocker;

	// Реле кнопок Yes/No текущего диалога подтверждения (GC-рут). Колбэк результата + таймер авто-отмены.
	// ── Диалог ввода SHOUTING_WIN 1580 (см. ShowInputDialog) ──
	static const FName kInputWindowId;
	UPROPERTY(Transient)
	TArray<TObjectPtr<class UAsdaButtonRelay>> InputRelays;
	TFunction<void(bool, const FString&)> InputCallback;
	TWeakObjectPtr<class UEditableText> InputField;
	TWeakObjectPtr<class UAsdaTextLimiter> InputLimiter;   // жёсткий стоп набора по байтам CP1251
	// ★ Реле подтверждения поля (Enter = ОК, Esc = отмена; потеря фокуса — НЕ подтверждение).
	//   Держим ссылкой из UPROPERTY-массива реле, чтобы GC не собрал объект с активным делегатом.
	UPROPERTY(Transient)
	TObjectPtr<class UAsdaTextLimiter> InputCommitRelay;

	// ── Окно ввода количества NUMPAD_WIN 1410 (см. ShowNumPad) ──
	static const FName kNumPadWindowId;
	UPROPERTY(Transient)
	TArray<TObjectPtr<class UAsdaButtonRelay>> NumPadRelays;   // цифры, MAX, DELETE, OK, CLOSE
	TFunction<void(bool, int32)> NumPadCallback;
	TWeakObjectPtr<class UEditableText> NumPadField;           // 1412 NUMPAD_NUMBOX
	int32 NumPadMaxQuantity = 0;                               // эталонный `pDialog[8]`
	bool bNumPadStrictMax = false;                             // строгий максимум эталона (см. `ShowNumPad`, параметр `bStrictMax`)
	// ★ W3fix2 (ревизия `fishing_impl_W3fix_W6_REVIEW.md`, МЕЛК-1): ведущий «−» в поле строгого режима появился от кнопки
	//   MAX (эталон пишет `sprintf("%d", max)` и при max < 0). Только тогда «−» сохраняется; с клавиатуры его ввести нельзя.
	bool bNumPadMinusFromMax = false;
	// ★ Реле изменения/подтверждения поля: клампинг до максимума (`uiClampQuantityToMax`),
	//   Enter = ОК, Esc = отмена. Держим UPROPERTY — иначе GC соберёт объект с живым делегатом
	//   (та же грабля, что уже описана у InputCommitRelay).
	UPROPERTY(Transient)
	TObjectPtr<class UAsdaTextLimiter> NumPadClampRelay;
	UPROPERTY(Transient)
	TObjectPtr<class UAsdaTextLimiter> NumPadCommitRelay;
	// Защита от повторного входа: CloseNumPad зовётся и из кнопки, и из commit-реле поля.
	bool bNumPadClosing = false;
	// Пере-вход клампинга: SetText внутри обработчика OnTextChanged снова его вызывает.
	bool bNumPadClamping = false;

	// Обрезка содержимого поля до допустимого — эталон `uiClampQuantityToMax@0xDA6410`:
	//   оставляем только цифры, не больше 10 знаков, значение не выше `NumPadMaxQuantity`
	//   (в строгом режиме — и при максимуме ≤ 0, с нотисом 6835 в чат).
	void ClampNumPadField();
	// Дописать цифру в конец поля — эталонный ПАТТЕРН цифровой кнопки (ui_core.c:62062-62074):
	//   дописывает, только если длина < 10, затем клампит.
	void AppendNumPadDigit(int32 Digit);

	UPROPERTY(Transient)
	TArray<TObjectPtr<class UAsdaButtonRelay>> ConfirmRelays;
	// ★ 31.10.2026 [ИСПРАВЛЕНИЕ] (`asda.UI.RelayAnchorSplit`): GC-якорь кнопок «Да/Нет» игрового `TWOBUT_MSGBOX` (очередь окон-сообщений).
	//   Раньше они лежали в `ConfirmRelays` вместе с кнопками логин-диалога `MESSAGE2_WIN` (`ShowConfirm`): показ/ответ одного окна
	//   сбрасывал якоря другого, и при обоих окнах на экране после сборки мусора кнопки второго молча не работали.
	UPROPERTY(Transient)
	TArray<TObjectPtr<class UAsdaButtonRelay>> TwoButRelays;
	// Якорь кнопок `TWOBUT_MSGBOX`: `TwoButRelays` (ключ 1) или прежний общий `ConfirmRelays` (ключ 0).
	TArray<TObjectPtr<class UAsdaButtonRelay>>& TwoButRelayAnchor();
	// ★ D2: реле кнопки ОК однокнопочного `ONEBUT_MSGBOX`. Держим ЗДЕСЬ (а не у вызывающего окна),
	//   иначе GC соберёт объект с активным делегатом. Список чистится при каждом новом показе.
	UPROPERTY(Transient)
	TArray<TObjectPtr<class UAsdaButtonRelay>> MessageRelays;
	// Закрыть ЛОГИН-диалог MESSAGE2_WIN: `bAnswer` — ответ игрока, иначе молча (замена, сброс сессии).
	//   С 26.09 игровой TWOBUT_MSGBOX сюда не ходит — его ведёт очередь окон-сообщений (`AnswerMsgBox`).
	void CloseConfirmImpl(bool bConfirmed, bool bAnswer);

	// ── ★★★ ОЧЕРЕДЬ ОКОН-СООБЩЕНИЙ (порт стека `CMsgBoxMgr+0x3340`; разбор — в публичном разделе) ──
	TArray<FAsdaMsgBoxEntry> MsgBoxQueue;   // стек: конец массива = верх
	// ★ 09.10.2026: стиль тела окна-сообщения из JUI, снятый до подмены на rich (ключ — имя узла).
	UPROPERTY(Transient)
	TMap<FName, FTextBlockStyle> MsgBodyStyles;
	uint32 MsgBoxSerial = 0;                // счётчик постановок (лог/самотест)
	int32 MsgBoxCurKind = -1;               // «текущий вид» менеджера (+0x32F8 = dword 3262)
	int32 MsgBoxCurId = -1;                 // «текущий id» (+0x32FC = dword 3263, displayNoticeId)
	bool bMsgBoxRawClose = false;           // `CloseWindow` окна-сообщения = голое снятие виджета (внутренний путь очереди)
	bool bMsgBoxRedisplayPending = false;   // загрузка мира вернула запись (25 сыгран) — окно поставит `CloseAll`
	// Поставить запись = `CItem_BuildTooltipText`: дедуп (вид, ВХОДНОЙ id) → в конец → показ (25).
	//   nullptr — отброшено дублем или WBP не загрузился (тогда запись снята, TWOBUT получает «нет»).
	UUserWidget* PushMsgBoxChecked(FAsdaMsgBoxEntry&& Entry, int32 RawNoticeId);
	// Нарисовать запись в окне её вида (ONEBUT/TWOBUT), поднять окно наверх модалок, звук 25 (если bSound).
	UUserWidget* DisplayMsgBoxEntry(const FAsdaMsgBoxEntry& E, bool bSound);
	// Порт `CGameUI_CloseWindow(вид)`: спрятать окно вида (bHide); для текущего id 38/5001/5002 снять ещё запись
	//   (после ответа кнопкой — только при `QueueFixes 0`); 5001/5002 — без возврата; иначе вернуть запись очереди (25);
	//   звук 5 (если не bSilent). bDeferDisplay — только звук возврата, окно поставит `CloseAll` (загрузка мира).
	void CloseMsgBoxKind(int32 Kind, bool bHide, bool bSilent, bool bAfterAnswer, bool bDeferDisplay);
	// Снять виджет окна-сообщения из реестра без звука и без логики очереди.
	void RemoveMsgBoxWidget(bool bTwoBut);
	// ★ 06.10.2026 (волна 2-Г): то же по виду записи — 0 → 1500, 1/3…8 → 1520, 59/60 → 1552; прочие — ничего.
	void RemoveMsgBoxWidgetOfKind(int32 Kind);
	// ★ 06.10.2026 (волна 2-Г): нарисовать запись вида 59/60 в окне 1552 (тело `DisplayMsgBoxEntry` для этого окна).
	UUserWidget* DisplayGiftMsgBox(const FAsdaMsgBoxEntry& E, bool bSound);
	// ★ 06.10.2026 (волна 2-Г): программное закрытие окна 1552 чужим кодом — как `DismissMsgBoxWindow` для 1500/1520.
	void DismissGiftMsgBox(bool bSilent);
	// ★ 06.10.2026 (волна 2-Г): запись, которую ответит окно WinId (обобщение `FindMsgBoxAnswerIndex` на 1552).
	int32 FindMsgBoxAnswerIndexFor(FName WinId) const;
	// Реле кнопок 1554/1555 и поля 1558 окна 1552 (под защитой GC, чистятся при каждом показе).
	UPROPERTY(Transient)
	TArray<TObjectPtr<UObject>> GiftRelays;
	// Программное закрытие окна-сообщения чужим кодом (`CloseWindow`/`CloseWindowSilently`): снять его запись без
	//   действия вызывающего, закрыть вид (5, если не bSilent) и вернуть следующую запись.
	void DismissMsgBoxWindow(bool bTwoBut, bool bSilent);
	// После `CloseAll` (вход в мир) вернуть верхнюю запись очереди следующим тиком, без звука.
	void RedisplayMsgBoxAfterCloseAll();
	// `miniMapWndClose(g_pMiniMapWnd)` показа окна-сообщения: закрыть карту мира 1710 (роспись `MAXMAP_WIN`), если открыта.
	void CloseWorldMapForMsgBox();
	// Запись, которую ответит кнопка окна: верхняя, если её окно совпадает; иначе последняя запись этого окна
	//   (защита порта: модальность эталона не дала бы нажать нижнее окно). INDEX_NONE — записи нет.
	int32 FindMsgBoxAnswerIndex(bool bTwoBut) const;
	// Индекс записи, которую показывать при возврате: верх (исправление) или начало (эталон), см. `QueueFixes`.
	int32 GetMsgBoxRedisplayIndex() const;
	TMap<int32, int32> MsgBoxSfxCounts;   // id звука → сколько раз сыграно (самотест «ровно по одному»)
	bool bMsgBoxSilentClose = false;      // закрытие окна-сообщения без звука 5 (замена окна, сброс сессии)
	TFunction<void(bool)> ConfirmCallback;
	FTimerHandle ConfirmTimeoutTimer;

	// Анимация гейджа отсчёта MESSAGE2_GAUGE (эталон uiProgressBar_AnimStart: CProgressBar 0→100 линейно за
	//   TimeoutSec, заливка L→R; SPEC_GAUGE_MODAL.md B2). Тикаем frac=elapsed/dur, ставим заливку.
	// ★ 31.10.2026 [ИСПРАВЛЕНИЕ] (`asda.UI.ConfirmGaugeSplit`): состояние полосы и гейта «Да» — СВОЁ у каждого окна подтверждения.
	//   Раньше это был один набор полей на логин-диалог MESSAGE2_WIN и игровой TWOBUT_MSGBOX: показ одного перехватывал полосу,
	//   таймер и кнопку «Да» другого (гейт разблокировал не ту кнопку), а закрытие/снятие TWOBUT обнуляло окно и таймер полосы
	//   логин-диалога. Ключ 0 — прежнее: оба окна ведут один экземпляр (`LoginGauge`).
	struct FAsdaConfirmGauge
	{
		FTimerHandle AnimTimer;                     // тик заливки (0,033 с)
		double StartTime = 0.0;                     // старт заливки по миру
		float Duration = 0.0f;                      // длительность заливки, с
		double RealStart = -1.0;                    // ★ 27.09: старт полосы-таймера TWOBUT (FPlatformTime владельца); −1 — по миру
		FString Prefix;                             // префикс узлов полосы (MESSAGE2_GAUGE / TWOBUT_GAUGE)
		int32 LastLoggedQuarter = -1;               // лог заливки 0/25/50/75/100 % (рантайм-доказательство)
		TWeakObjectPtr<UUserWidget> Dlg;            // окно, чью полосу ведём
		bool bGateYes = false;                      // «Да» разблокируется только по заполнению полосы
		float FullW = 0.f;                          // полная ширина заливки (клип по Frac, как Rect оригинала)
		TWeakObjectPtr<class UButton> YesButton;    // кнопка «Да» (enable/disable по полосе)
	};
	FAsdaConfirmGauge LoginGauge;    // логин-диалог MESSAGE2_WIN (`ShowConfirm`); при ключе 0 — и TWOBUT
	FAsdaConfirmGauge TwoButGauge;   // игровой TWOBUT_MSGBOX (очередь окон-сообщений)
	// ★ (ревизия 31.10) Какой экземпляр ведёт полосу TWOBUT — решается ОДИН РАЗ при показе окна (`DisplayMsgBoxEntry`) и
	//   запоминается: переключение ключа, пока окно на экране, не теряет его таймер и кнопку.
	bool bTwoButOwnGauge = true;
	// Полоса окна: TWOBUT (`bTwoBut`) — запомненный при показе экземпляр (`TwoButGauge` при ключе 1), иначе `LoginGauge`.
	FAsdaConfirmGauge& GaugeOf(bool bTwoBut);
	// Полоса TWOBUT больше не нужна (окно снято программно/ответом): снять её таймер (свой экземпляр; при общем — как прежде,
	//   таймер не трогаем) и забыть окно.
	void ReleaseTwoButGauge();
	// Логин-диалог подтверждения MESSAGE2_WIN (игровой TWOBUT_MSGBOX с 26.09 ведёт очередь окон-сообщений).
	FName ConfirmWindowId = TEXT("MESSAGE2_WIN");
	bool bLoggedGaugeType = false;                   // одноразовый лог класса виджета гаджа (диагностика)
	// Тик заливки полосы окна (`bTwoBut` — TWOBUT, иначе логин-диалог).
	void TickConfirmGaugeOf(bool bTwoBut);

	// Пересчитать gate ввода: если есть модальное — сфокусировать ввод на верхнем модальном (блок нижних).
	void RefreshModalInput();

	// Выдать z для НОВОГО модального окна: kModalZ, а при уже открытых модалках — на 1 выше верхней
	//   (вложенные подтверждения складываются стопкой). Верхняя граница полосы — kModalZ+999.
	int32 AllocateModalZ() const;

public:
	// ★ Поднять ВСЕ дочерние окна владельца ВЫШЕ него (рекурсивно, для внуков). Каждому потомку —
	//   СВОЯ ступень (`OwnerZ + 1`, `+2`, …) в порядке их текущего z; потомка, который уже выше,
	//   функция НЕ ОПУСКАЕТ. Модальные потомки не трогаем: они живут в полосе kModalZ и так выше.
	// ★ ПУБЛИЧНАЯ (ревизия 25.09.2026, СРЕДНЕЕ-6): подсистеме-владельцу бывает нужно ВОССТАНОВИТЬ
	//   порядок своей стопки, НЕ поднимая её саму наверх слоя. Раньше для этого звали
	//   `BringWindowToFront`, и это молча меняло поведение: открытие сетки 43400 выносило весь
	//   диалог 43500 на верх обычного слоя, чего до правки не происходило.
	void RaiseOwnedAbove(FName OwnerId);

private:
	// Является ли Candidate потомком Root по цепочке OwnerWindowId (защита от циклов — лимит глубины).
	bool IsOwnedBy(FName Candidate, FName Root) const;

	// ══════════════ ПЕРЕТАСКИВАНИЕ: состояние и внутренняя кухня (реализация — .cpp) ═══════════════
	// Реестр меты JUI. Транзиентный кэш загруженного DataAsset.
	UPROPERTY(Transient)
	TObjectPtr<const UAsdaJuiWindowMetaAsset> JuiMeta;
	mutable bool bJuiMetaLoadTried = false;

	// Тик подсистемы (FTSTicker): опрос ЛКМ и перенос окна на дельту курсора.
	FTSTicker::FDelegateHandle DragTickHandle;
	bool TickDrag(float DeltaTime);

	FName DragWindowId;                    // окно, которое сейчас тянут (None = не тянем)
	int32 DragHandleIndex = INDEX_NONE;    // индекс ручки в DragHandles этого окна
	FVector2D DragPrevMouse = FVector2D::ZeroVector;   // предыдущая позиция курсора (дельта = cur − prev)
	bool bDragMoved = false;               // в текущем захвате окно реально сдвигали (пакет 4: кламп на отпускании только тогда)
	/** ★★★ ОКНО, КОТОРОЕ НАДО ПОДНЯТЬ НА ОТПУСКАНИИ ЛКМ (None = нечего поднимать).
	 *  Живой дефект 2026-08-19 «иногда приходится нажимать по два раза»: подъём окна идёт через
	 *  `SetWindowZOrder`, а тот снимает окно с вьюпорта и вешает заново. Между нажатием и
	 *  отпусканием это рвёт путь захвата мыши, Slate снимает захват (`FSlateUser::GetCaptorPath`
	 *  → `ReleaseCapture` → `SButton::OnMouseCaptureLost` → `Release()`), и `OnMouseButtonUp`
	 *  при методе `DownAndUp` уже не зовёт `ExecuteOnClick` — клик исчезает без следа в логе.
	 *  Поэтому щелчок по ТЕЛУ окна подъём откладывает сюда, а выполняет его на отпускании.
	 *  Щелчок по РУЧКЕ поднимает сразу (кликабельных контролов под ручкой нет). */
	FName PendingRaiseWindowId;
	// ★ Подъём по телу (06.10.2026): латч СКМ для фронта нажатия — эталон поднимает окно нажатием ЛКМ, ПКМ и СКМ
	//   (`uiContainerHandleMouse@0x12DCC50`: флаги 0x400/0x2000/0x10000 = события 11/14/17).
	bool bPrevMmbRaise = false;
	// ★ Клавиатурный фокус ДО нажатия (снимок прошлого кадра): Slate разбирает нажатие раньше `TickDrag` и успевает
	//   увести фокус на вьюпорт. Нужен правилу эталона «поле окна НИЖЕ поймавшего нажатие фокус не теряет».
	TWeakPtr<SWidget> FocusBeforePress;
	// Разбор фокуса на фронте нажатия (правило эталона — у определения). Catcher — окно, поймавшее нажатие.
	void ApplyPressFocusRule(FName Catcher);
	// Вернуть фокус полю, если перевешивание окна во вьюпорте его сняло (эталонная перестановка фокус не трогает).
	void RestoreFocusAfterRaise(const TSharedPtr<SWidget>& Was);
	// Окно реестра, в поддереве которого лежит Slate-виджет W (None — не в окне).
	FName FindWindowOwningSlate(const TSharedPtr<SWidget>& W) const;
	// ★ 06.11.2026 (`asda.UI.TopWindowPress`): препроцессор нажатий Slate. Решение по нажатию: true — съесть (`Press`), по отпусканию
	//   съеденного — тоже съесть (`Release`). Тело — AsdaWindowSubsystem_TopPress.cpp.
	friend class FAsdaTopPressGate;
	bool IsBelowCatcher(FName Top, FName Low) const;   // окно Low ниже поймавшего мышь окна Top
	bool HandleTopWindowPress(const FPointerEvent& Ev);
	bool HandleTopWindowRelease(const FPointerEvent& Ev);
	void RegisterTopPressGate();     // поставить препроцессор (Initialize)
	void UnregisterTopPressGate();   // снять препроцессор (Deinitialize)
	void CloseClickAwayMenus(const FVector2D* PressPt);   // погасить меню; PressPt — точка нажатия, nullptr — все показанные
	TMap<FName, FSimpleDelegate> ClickAwayMenus;          // зона меню → закрыватель
	FDelegateHandle ClickAwayActivationHandle;            // подписка на потерю фокуса приложения
	TSharedPtr<IInputProcessor> TopPressGate;   // препроцессор
	TSet<FKey> TopPressEaten;                   // кнопки, чьё нажатие съедено и ждёт отпускания
	int32 TopPressEatenCount = 0;               // съедено нажатий всего
	// ★ 08.11.2026 (пачка 18, баг 2): панели внутри окон, тело которых ловит нажатие (`RegisterPressPanel`).
	struct FPressPanel
	{
		FName Owner;                          // окно реестра
		TFunction<bool()> IsShown;            // панель показана
		TFunction<const UWidget*()> Node;     // виджет панели
	};
	TMap<FName, FPressPanel> PressPanels;   // id контрола (.JUI) → панель
	// ★ Ревизия (СРЕДН-2): фокус строки, который вернуть НА ОТПУСКАНИИ (возврат на нажатии рвал клик фокусируемой кнопки).
	TWeakPtr<SWidget> PendingFocusRestore;
	// ★ Ревизия (КРИТ-1): рабочий AlwaysTop, выставленный кодом (`SetWindowAlwaysTop`) — переживает пересоздание окна.
	TMap<FName, bool> AlwaysTopOverride;
	// Рабочий AlwaysTop по id: выставленный кодом → таблица эталона → мета `.JUI`.
	bool ResolveAlwaysTop(FName WindowId) const;
	// z для окна наверху полосы AlwaysTop (сжатие полосы, если она упёрлась в kAlwaysTopZMax).
	int32 NextAlwaysTopZ(FName WindowId);
	// Есть ли у окна предок-владелец с рабочим AlwaysTop (дочернее окно живёт в полосе владельца).
	bool HasAlwaysTopOwner(FName WindowId) const;
	// ── синтетический клик `asda.UI.SimClick` (см. `SimulateClickAt`) ──
	FVector2D SimClickPos = FVector2D::ZeroVector;
	int32 SimClickHold = 0;         // сколько кадров держать кнопку
	int32 SimClickFramesLeft = 0;   // сколько кадров удержания осталось
	bool bSimClickDown = false;     // нажатие уже отправлено
	bool bSimClickArmed = false;    // клик заказан и ещё не завершён
	// ── синтетическое перетаскивание `asda.UI.SimDrag` (см. `SimulateDrag`) ──
	FVector2D SimClickEndPos = FVector2D::ZeroVector;   // куда ведём курсор к моменту отпускания
	bool bSimClickIsDrag = false;                       // false ⇒ обычный клик (конец = начало)
	// ★ Кнопка текущего заказа: ЛКМ или ПКМ. Одно поле на клик и на драг — путь инъекции общий.
	bool bSimClickRight = false;
	/** Кадровый разбор заказанного клика. Зовётся в самом начале `TickDrag`. */
	void TickSimulatedClick();
	// ★ Аудит №3 (B3): прежний латч `bPrevLmbDown` — общая точка `FAsdaMouseFrame`.
	//   Захват окна начинается ТОЛЬКО по фронту нажатия (`Mouse.bLmbDown`).
	FAsdaMouseFrame Mouse;

	// Подтянуть мету окна из DA_JuiWindowMeta в запись реестра (зовётся из RegisterWindow/OpenWindow).
	void AttachJuiMeta(FName WindowId, FAsdaWindowEntry& Entry);

	// ★★★ Доукомплектовать КНОПКИ окна из данных — зовётся оттуда же, где и `AttachJuiMeta`,
	//   чтобы у окна не было двух разных моментов «доукомплектования». Два действия:
	//     1) снять синее кольцо фокуса UE со всех кнопок (в оригинале его нет, и оно крадёт
	//        фокус у поля ввода) — `AsdaUi::DisableButtonFocusRings`;
	//     2) привязать состояния ПОДПИСИ (наведение/нажатие) — `AsdaUi::BindButtonCaptionStates`;
	//        данные состояний лежат в `FButtonStyle` самих кнопок (их кладёт импортёр JUI).
	//   Обоснование и разбор эталона — в `AsdaUiHelpers.h`.
	void ApplyButtonStateData(FName WindowId, class UUserWidget* Widget, FAsdaWindowEntry& Entry);

	// ══════════ РАСКЛАДКА МЕЖДУ СЕССИЯМИ: внутренняя кухня (обоснование — в public-блоке) ═══════
	// Секция ini, в которую пишется вся раскладка окон.
	static const TCHAR* kLayoutSection;

	// Сохранённая раскладка по окнам — ЕДИНСТВЕННЫЙ источник правды в рантайме; ini лишь его слепок.
	TMap<FName, FAsdaWindowSavedLayout> SavedLayouts;
	bool bLayoutsLoaded = false;         // ini уже прочитан (ленивая загрузка при первом обращении)
	bool bLayoutDirty = false;           // есть несохранённые изменения (сброс — FlushWindowLayout)

	// Окна, которые ждут КЛАМПА В ЭКРАН: восстановление сдвига делается сразу при регистрации
	//   (слоты доступны всегда), а вот проверить «не уехало ли окно за край» можно только по ЖИВОЙ
	//   геометрии, которой в кадре создания ещё нет. Разбирается в `TickLayoutRestore` каждый кадр,
	//   запись снимается, как только кламп удался (или окно закрылось).
	TSet<FName> PendingClamp;

	void LoadAllLayouts() const;                                  // ленивое чтение секции ini
	void ApplySavedLayout(FName WindowId);                        // применить сдвиги (идемпотентно)
	void TickLayoutRestore();

	/** Погасить штатную синюю рамку фокуса UE ГЛОБАЛЬНО (подменой кисти `FocusRectangle` на пустую).
	 *  В оригинале фокус — понятие только поля ввода; обводок у кнопок и ячеек нет.
	 *  Зовётся один раз при инициализации. Откат — `asda.UI.FocusRect 1`. */
	static void DisableGlobalFocusRectangle();
	bool ClampWindowIntoViewport(FName WindowId);                 // true — заявку снимаем (ключи разобраны или окно ждёт показа по подписке)

	// ══ ★★★ КЛАМП ДО ПЕРВОЙ ОТРИСОВКИ (М-1 ревизии `widen_fixes2_REVIEW.md`, 07.10.2026) ══════════════════════════
	//   Тот же кламп, но без живой геометрии: прямоугольники кусков считаются по ЦЕПОЧКЕ СЛОТОВ канваса от корня окна
	//   (анкоры, alignment — формула `SConstraintCanvas`), экран = размер вьюпорта / DPI в юнитах канваса. Зовётся там,
	//   где сдвиг только что применён (`ApplySavedLayout`, `CenterWindowKeepingSavedOffset`, смена размера вьюпорта),
	//   поэтому первый кадр окна уже в экране. Ключи остаются в заявке: живой кламп по геометрии — проверка следом.
	//   Окно не во вьюпорте целиком (не полноэкранный слот) или кусок вне цепочки слотов канваса — кусок пропускается.
	//   ★ 08.10.2026 (пакет 4): DragReleaseKey ≠ None — режим «отпустили перетаскивание» (`ClampDragOnRelease`): только этот
	//   ключ (без заявки `ClampPendingKeys`), всегда ПРИЖАТИЕ К КРАЮ (не правило «за экраном → дефолт»), модальное окно тоже,
	//   и поправка идёт и в `SavedLayouts` — в ini попадёт позиция, где окно осталось после возврата в экран.
	//   ★ 08.10.2026 (пакет 5): в этом режиме (а) сдвиг ключа округляется до целого пикселя ВНУТРЬ экрана (СРЕДН-2: рамка у
	//   края не должна на перезапуске мериться «за экраном» из-за дробей), (б) отпущен корневой ключ — следом проверяются
	//   под-окна этого окна со своей позицией (МЕЛК-1: чат личного магазина, уехавший вместе с корнем), (в) поправка
	//   отпущенного ключа входит в `DragOffset` окна-владельца ручки (МЕЛК-2: `asda.UI.DumpDrag`).
	void ClampWindowBySlotsNow(FName WindowId, FName DragReleaseKey = NAME_None);
	// ★ 08.10.2026 (пакет 4): [ПОРТ] требование владельца — окно, отпущенное игроком частично за краем экрана (инвентарь при
	//   1024×768 оставался на 12 px за нижним краем), возвращается в экран прижатием к краю. Зовётся на отпускании ЛКМ перед
	//   записью ini (`FlushWindowLayout`). Только если окно в этом захвате реально сдвигали. Откат — `asda.UI.DragReleaseClamp 0`.
	void ClampDragOnRelease();
	// Виджеты под-окон, которые двигает СОБСТВЕННЫЙ (не корневой) ключ окна: в габариты корня не входят (С-1 прошлой ревизии).
	TSet<FName> CollectOwnKeyWidgets(FName WindowId, const FAsdaWindowEntry& E) const;

	// ══ ★★★ СМЕНА РАЗМЕРА ВЬЮПОРТА ВО ВРЕМЯ ИГРЫ (С-1 ревизии `widen_fixes2_REVIEW.md`, 07.10.2026) ══════════════
	//   БЫЛО: кламп запускало только открытие окна; `r.SetRes`/развёртывание окна игры оставляли открытые окна (и HUD —
	//   чат, MINISTATE, их никто не переоткрывает) за краем до перезапуска.
	//   СТАЛО: подписка на `FViewport::ViewportResizedEvent` (её же слушает `UGameEngine::OnViewportResized`). Колбэк
	//   только ставит флаг (закон `no-reentrant-net-callbacks`: он приходит изнутри обновления RHI вьюпорта); в тике
	//   у каждого открытого окна применённые сдвиги возвращаются к сохранённым в ini, затем кламп по слотам и
	//   заявка на живую проверку. В ini ничего не пишется: на большом экране окно снова там, где его ставил игрок.
	void OnViewportResized(FViewport* Viewport, uint32 Unused);   // колбэк движка: только флаг
	void ProcessViewportResize();                                 // разбор флага в тике (см. выше)
	FDelegateHandle ViewportResizedHandle;                        // подписка на `FViewport::ViewportResizedEvent`
	bool bViewportResizePending = false;                          // пришло событие, ещё не разобрано
	uint64 ViewportResizeFrame = 0;                               // `GFrameCounter` события (разбор — через 2 кадра, после раскладки Slate)
	FIntPoint LastViewportPx = FIntPoint::ZeroValue;              // размер вьюпорта, под который клампились окна (0 — ещё не мерили)
	uint64 LiveClampNotBeforeFrame = 0;                           // живой кламп не раньше этого кадра: геометрия после сдвига устарела

	// ══ М-2: событие показа вместо покадрового перебора (подписки — `FAsdaWindowEntry::ClampWatch`) ══════════════
	void OnClampWatchVisibility(UObject* Obj, UE::FieldNotification::FFieldId Field, FName WindowId);   // показ → в заявку
	void ClearClampWatch(FAsdaWindowEntry& E);                    // снять все подписки окна
	TSet<FName> ClampWokenWindows;                                // окна, разбуженные событием показа (подписки снимет тик)
	// Разбор разбуженных окон: снять подписки, кламп по слотам показанного куска, заявка на живую проверку.
	//   Зовётся из `OnSlatePreTick` (до раскладки и отрисовки кадра показа — кусок сразу рисуется в экране) и из тика.
	void ProcessClampWoken();
	// `FSlateApplication::OnPreTick`: начало тика Slate, ПОСЛЕ игрового кода кадра и ДО отрисовки — точка, где показ,
	//   сделанный в этом кадре, ещё можно поправить без видимого кадра за краем. Разбирает разбуженные окна и делает
	//   кламп по слотам окнам из `PendingClamp` (показ, пришедший, пока окно ещё стояло в опросе).
	void OnSlatePreTick(float DeltaTime);
	FDelegateHandle SlatePreTickHandle;                           // подписка на `FSlateApplication::OnPreTick`

	// ══ ★★★ Подъём при показе (правило владельца 08.10.2026; обоснование — у `IsShowRaiseEligible`) ══════════════
	TArray<FName> PendingShowRaise;                               // очередь подъёма в порядке показов (окно — один раз, в конце)
	bool bFlushingShowRaise = false;                              // идёт разбор очереди: подъёмы из неё в очередь не возвращаются
	void WatchShow(FName WindowId, FAsdaWindowEntry& E);          // подписаться на видимость корня окна (открытие/регистрация)
	void UnwatchShow(FAsdaWindowEntry& E);                        // снять подписку (закрытие/перерегистрация)
	void OnShowWatchVisibility(UObject* Obj, UE::FieldNotification::FFieldId Field, FName WindowId);   // фронт показа → очередь
	void EnqueueShowRaise(FName WindowId, const TCHAR* Why);      // поставить окно в конец очереди
	// Показ при отпущенных кнопках мыши — подъём СРАЗУ (в кадре показа, до фокуса строки ввода, который окно ставит после показа:
	//   перевешивание окна с фокусом в поле ломает последующий `SetText` — замер `asda.UI.ProbeEditRaise`). Иначе — очередь.
	void RaiseOnShowNow(FName WindowId, const TCHAR* Why);
	// Окна, поднятые (показом или явным подъёмом) МЕЖДУ нажатием по телу другого окна и отпусканием: тело на отпускании встаёт
	//   ПОД самое нижнее из них (эталон: тело поднимается на нажатии, показанные щелчком — позже, выше).
	TArray<FName> RaisedDuringPress;
	// Рецепт сдвига цели: что именно двигать (весь корень окна или перечисленные виджеты).
	//   Берётся ИЗ МЕТЫ .JUI, а не из кода: цель могла прийти от ручки ЧУЖОГО окна.
	bool FindDragRecipe(FName TargetWindowId, FName TargetKey, bool& bOutRoot, TArray<FName>& OutWidgets) const;
	// Сдвинуть цель окна на Delta по рецепту выше (общая часть drag'а и восстановления).
	void ShiftTargetByKey(FName WindowId, FName TargetKey, FVector2D Delta);
	// Учесть сдвиг цели в сохраняемой раскладке (зовётся из TickDrag и из SetSavedWindowOffset).
	void AccumulateOffset(FName TargetWindowId, FName TargetKey, FVector2D Delta);
	// Сдвинуть цель ручки на Delta (слейт-юниты): либо всё окно, либо перечисленные виджеты цели.
	void ShiftDragTarget(const FAsdaJuiDragHandle& Handle, const FAsdaWindowEntry& Owner, FVector2D Delta);

	// ★ ГЛОБАЛЬНО: модальный диалог — ПО ЦЕНТРУ экрана. JUI кладёт диалоги фикс-Origin'ом по центру базы
	//   1024×768 (MESSAGE2_WIN: Origin 377,299 = (1024−270)/2,(768−170)/2, align-mode 0) — на других
	//   разрешениях фикс-координата уезжает в угол. Центр-анкор (0.5,0.5) сохраняет px-размер (DPI=1.0).
	//   ★ 08.10.2026 (пакет 2): для окон таблицы `PlaceModalByOriginal` это правило больше не действует — см. ниже.
	void CenterModalContent(UUserWidget* W);

	// ★★★ [ОРИГИНАЛ] 08.10.2026, РЕШЕНИЕ ВЛАДЕЛЬЦА (пакет 2): модальное окно стоит ТАМ, ГДЕ ЕГО СТАВИТ ОРИГИНАЛ, а не в центре порта.
	//   Таблица (реверс `Reports/owner_1008/window_default_pos.md` §1.4 + `gamemain_jui.ini`, сводка — `batch2_fixes.md`):
	//     • 1580 SHOUTING_WIN, 8000 COMMONWIN_MSGBOX — позиция = разметка .JUI (якорь «ц50/ц70», «ц50/ц50»), её уже несёт WBP;
	//     • 1530 SOWELL_SELBOX — левый-верх окна 1500 (`CMsgBoxMgr_InitControls@0xD6AD20`, ui_core.c:55959), см. `MsgBox1500TopLeft`;
	//     • 1410 NUMPAD_WIN — ставит вызывающий (`ShowNumPad`: у курсора / режим 6 — центр, ui_core.c:61835-61843);
	//     • прочие модальные (1500/1520/1552 — центр и у оригинала; окна порта без оригинала; экран входа) — прежний центр.
	//   Зовётся после регистрации окна (запись реестра нужна `PlaceWindowKeepingSavedOffset`). Экран входа не трогается
	//   (закон «Login не трогать»). Откат: `asda.UI.ModalOriginalPos 0` — все модальные снова по центру.
	void PlaceModalByOriginal(FName WindowId);
public:
	// Ключ `asda.UI.ModalOriginalPos` ≠ 0 — модальные окна в позиции оригинала (умолчание 1); 0 — прежний центр порта.
	static bool UseOriginalModalPos();
	// ★ [ОРИГИНАЛ] Левый-верх окна 1500 ONEBUT_MSGBOX по центру экрана — точка, куда `CMsgBoxMgr_InitControls@0xD6AD20`
	//   (ui_core.c:55953-55961) ставит ВСЕ окна семейства (1500/1520/1530/1559/1545): ((W>>1) − (360>>1), (H>>1) − (140>>1)),
	//   целочисленно, по размеру 1500 из разметки (360×140), а не по своему. View — экран в юнитах канваса.
	static FVector2D MsgBox1500TopLeft(const FVector2D& View);
	// ★ 08.10.2026 (пакет 3, МЕЛК-1): запомнить постановщик окна (`FAsdaWindowEntry::RePlacer`) — его повторит смена размера
	//   вьюпорта. nullptr — снять (окно стоит якорем, например центр D5 у 1559 при `asda.PremiumMall.LegacyBoxCenter 0`).
	//   Окна нет в реестре — ничего не делает. ★ Пакет 5: снимает `bRePlaceOnShow` — новая постановка уже под текущий экран.
	void SetWindowRePlacer(FName WindowId, TFunction<void()> Placer);
private:

	// ══ ★★★ [ОРИГИНАЛ] ЦЕЛЫЕ КООРДИНАТЫ ОКОН С ПРОЦЕНТНЫМ ЯКОРЕМ (08.10.2026, пакет 2; МЕЛК-1 `batch_fixes_REVIEW.md`) ══
	//   Оригинал (`uiCalcAnchoredScreenPos@0x12DFBB0`, ui_core.c:109345-109372) ставит корень «ц%» в ЦЕЛЫЕ пиксели:
	//   X = ftol(a · float(W · 0.0099999998) + Origin.x) − (w >> 1) (Y так же). FPU в одинарной точности: устройство D3D9
	//   создаётся без D3DCREATE_FPU_PRESERVE (`CD3DManager_GetBehaviorFlags@0x11245D0` даёт только 0x20/0x40), поэтому
	//   произведение округляется до float. WBP несёт ту же формулу в точной арифметике (анкор a/100, смещение Origin − w/2.0),
	//   отсюда дробные позиции (900 → 145.2, 44.6; 8300 → y 465.5; 69400 → y 259 вместо 258).
	//   Здесь каждому точечному слоту корневого канваса с анкором строго внутри (0;1) по оси добавляется поправка
	//   «формула оригинала − формула WBP» для ТЕКУЩЕГО размера экрана. Поправка запоминается (`FAsdaWindowEntry::PctSnap`) и
	//   снимается перед пересчётом (смена размера вьюпорта) — шаг идемпотентен; слот, которому чужой код сменил анкор
	//   (центрирование, постановка), выпадает из учёта. Экран входа не трогается. Откат: `asda.UI.PctAnchorTrunc 0`.
	void SnapPercentAnchors(FName WindowId);
};
