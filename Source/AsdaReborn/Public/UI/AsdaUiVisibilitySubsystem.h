// Copyright (c) 2026 @sitxovski (Discord)
// UAsdaUiVisibilitySubsystem — ВЛАДЕЛЕЦ флага «интерфейс показан/скрыт» (Alt+Z). [ОРИГИНАЛ]+[ПОРТ]
//
// ЧТО ЭТО У ОРИГИНАЛА (`Reports/hide_ui_altz/01_etalon.md`). WM_SYSKEYDOWN 'Z' (`MainWndProc` `0x98548E`, game_main.c:6299)
//   переключает бит 128 маски проходов рендера `CConfig+0x38` (`g_hMainWnd@0x15CB460` — миснейм). Читатель бита один —
//   `CRenderer_RenderWorldFrame` (render_core.c:1482-1570): при сброшенном бите НЕ РИСУЮТСЯ все окна JUI (HUD, хотбар, чат,
//   мини-карта со значками 5274, большая карта, панель цели, плашки WARMESSAGE 221, баннеры, модальные, подсказки, призрак
//   перетаскивания). ОСТАЮТСЯ «мировые» окна и проходы до блока: таблички имён/HP над головами, облачка чата, вывески лавок,
//   цифры урона, мир и эффекты, курсор ОС. Логику бит не меняет: хоткеи, клики, чат работают. Гейты переключения:
//   мир (`g_GameState == 1`), нет меню смерти/защищённого запроса (`CUIManager+0x44`), нет предмета в руке (`+0x04`), нет
//   модального окна (`CWindowMgr+0x200`). Фокус чата НЕ гейт, буква «z» в чат не печатается (WM_SYSCHAR съеден). Звука, нотиса,
//   записи в конфиг нет; вернуть — только повторным Alt+Z.
//
// УСТРОЙСТВО ПОРТА. Один флаг здесь, все потребители берут его отсюда:
//   • окна реестра `UAsdaWindowSubsystem` и баннеры (уровень, профессия, зона, дуэль) регистрируются `Track`: подсистема прячет
//     ОБЁРТКУ вьюпорта виджета (`SConstraintCanvas` от `AddToViewport`) — прозрачность 0 + `HitTestInvisible`: собственную
//     видимость окна не трогает (окна сами её меняют), мышь Slate не ловит, путь фокуса не рвёт (набор в чате работает);
//   • опрос мыши окон по геометрии — общий кадр `FAsdaMouseFrame` и `AsdaUi::IsCursorOverWidget` слепнут (`AsdaUi::GUiInputHidden`),
//     вывеска лавки (видна у оригинала) — исключение `FAsdaMouseFrame::bIgnoreUiHidden`;
//   • хит-тест окон реестра (`IsScreenPointOverAnyUiWindow`, `FindWindowCatchingAt`, `IsCursorOverWindow`, колесо) и значок 5274
//     спрашивают `IsUiShownFor` / `HiddenBlocksUiHitTest`; на сцене входа скрытие не действует;
//   • таблички, облачка, вывески, цифры урона, экран загрузки — не трогаются (как у оригинала).
//   Ловля — препроцессор ввода Slate: срабатывает и при фокусе в поле чата, как у оригинала.
//
// ★ [ИСПРАВЛЕНИЯ] дефектов оригинала (ключ отката `asda.UI.HideFixes 0`):
//   1) удержание Alt+Z мигало интерфейсом на автоповторе — здесь только фронт нажатия;
//   2–3) модальное окно / меню смерти при скрытом интерфейсе были невидимы и запирали Alt+Z — здесь их появление сразу
//        показывает интерфейс;
//   4) невидимые окна ловили клики и подсказки — здесь при скрытом интерфейсе окна мышь не ловят, клик идёт в мир;
//   5) скрытие переживало выход в выбор персонажа и новый вход — здесь полный сброс сессии показывает интерфейс
//      (смена карты — нет, как у оригинала). Разрыв связи показывает интерфейс всегда, и при откате исправлений.
//
// ★ [ИСПРАВЛЕНИЕ] по решению владельца (01.11.2026, ключ `asda.UI.HideCursor 0` — прежнее): при скрытом интерфейсе прячется и
//   курсор мыши. У оригинала курсор ОС при Alt+Z остаётся (`01_etalon.md` §1: `SetCursor` `game_main.c:6106` бит 128 не читает).
//   Прячется только ПОКАЗ: курсор вьюпорта, который UE спрашивает у контроллера (`APlayerController::GetMouseCursor` →
//   `CurrentMouseCursor`), на время скрытия = `EMouseCursor::None`. Вид курсора порта (`AAsdaPlayerController::SetGameCursor`:
//   обычный/бой/разговор/Pick камня, файлы через `SetHardwareCursor`) и режим ввода (`bShowMouseCursor`) не трогаются — клики по
//   миру идут как прежде, а при показе возвращается тот вид, что держит игра в эту секунду. Новый контроллер после смены карты
//   (`InitInputSystem` ставит `DefaultMouseCursor`) прячется тиком; на сцене входа, при разрыве связи и при самовозврате
//   интерфейса (модалка, меню смерти) курсор возвращается вместе с ним.
#pragma once

#include "CoreMinimal.h"
#include "Subsystems/GameInstanceSubsystem.h"
#include "Tickable.h"
#include "AsdaSessionReset.h"
#include "AsdaUiVisibilitySubsystem.generated.h"

class UUserWidget;
class SWidget;
class IInputProcessor;
class APlayerController;

UCLASS()
class ASDAREBORN_API UAsdaUiVisibilitySubsystem : public UGameInstanceSubsystem, public FTickableGameObject,
	public IAsdaSessionResettable
{
	GENERATED_BODY()

public:
	virtual void Initialize(FSubsystemCollectionBase& Collection) override;
	virtual void Deinitialize() override;

	// Полный сброс сессии показывает интерфейс [ИСПРАВЛЕНИЕ 5]; смена карты — нет.
	virtual void ResetSessionState(EAsdaSessionResetReason Reason) override;

	// ── FTickableGameObject: модалка/меню смерти при скрытом интерфейсе показывают его [ИСПРАВЛЕНИЕ 2–3] ──
	virtual void Tick(float DeltaTime) override;
	virtual TStatId GetStatId() const override { RETURN_QUICK_DECLARE_CYCLE_STAT(UAsdaUiVisibilitySubsystem, STATGROUP_Tickables); }
	virtual bool IsTickable() const override { return !bUiShown; }

	// Порт бита 128 `CConfig+0x38`: true — интерфейс рисуется (умолчание, `|= 0x9FB` при чтении config.txt).
	bool IsUiShown() const { return bUiShown; }
	// То же для рисующих без указателя на подсистему (нет подсистемы — «показан»).
	static bool IsUiShownFor(const UObject* Ctx);

	// Интерфейс скрыт и исправления включены: окна по геометрии мышь НЕ ловят [ИСПРАВЛЕНИЕ 4] (`IsCursorOverAnyUiWindow`).
	static bool HiddenBlocksUiHitTest(const UObject* Ctx);

	// Единственный писатель флага. Событие — только при смене. Why — причина для журнала.
	void SetUiShown(bool bShown, const TCHAR* Why);

	// Alt+Z: гейты эталона и переключение. true — переключено; false — отклонено гейтом (молча, как у оригинала).
	bool ToggleFromHotkey(const TCHAR* Src);

	// Почему Alt+Z сейчас отклонён (пусто — разрешён). Самотест и журнал.
	FString GetToggleBlockReason() const;

	// Зарегистрировать виджет вьюпорта, который прячется вместе с интерфейсом (окна реестра, баннеры). Повторная регистрация
	//   того же виджета безопасна (после повторного `AddToViewport` у него новая обёртка — состояние применится к ней).
	void Track(UUserWidget* Widget);
	// То же без указателя на подсистему.
	static void TrackFor(const UObject* Ctx, UUserWidget* Widget);

	// Видна ли обёртка вьюпорта виджета (самотест: проверка путём игрока по живому Slate-дереву).
	static bool IsViewportHostVisible(const UUserWidget* Widget);
	// Остаётся ли обёртка на пути фокуса клавиатуры (набор в чате при скрытом интерфейсе).
	static bool IsViewportHostFocusable(const UUserWidget* Widget);

	// Событие смены флага (true — показан).
	DECLARE_MULTICAST_DELEGATE_OneParam(FOnAsdaUiShownChanged, bool /*bShown*/);
	FOnAsdaUiShownChanged OnUiShownChanged;

	// Нажатие из препроцессора ввода: Z при зажатом Alt. bRepeat — автоповтор. true — нажатие съедено.
	bool HandleAltZ(bool bRepeat);

	// Курсор мыши сейчас спрятан этой подсистемой (вместе с интерфейсом, ключ `asda.UI.HideCursor`). Самотест и журнал.
	bool IsCursorHidden() const { return bCursorHidden; }

private:
	// Прятать сейчас: флаг скрыт И это не сцена входа (выбор персонажа у оригинала рисуется своим путём, всегда виден).
	bool ShouldHide() const;
	// Применить текущее состояние к обёртке одного виджета.
	void ApplyTo(UUserWidget* Widget);
	// Применить ко всем зарегистрированным (и вычистить умершие).
	void ApplyAll();
	// Спрятать/вернуть курсор вьюпорта по `ShouldHide` и ключу `asda.UI.HideCursor` (зовут `ApplyAll` и тик скрытого режима).
	void ApplyCursor();

	bool bUiShown = true;                                   // бит 128
	TArray<TWeakObjectPtr<UUserWidget>> Tracked;            // прячущиеся виджеты вьюпорта
	// Свёрнутая обёртка и её видимость до скрытия (возвращается при показе).
	struct FHiddenHost
	{
		TWeakPtr<SWidget> Host;      // обёртка вьюпорта (`SConstraintCanvas` подсистемы вьюпорта)
		EVisibility Was;             // видимость до скрытия
		float WasOpacity = 1.f;      // прозрачность до скрытия
	};
	TArray<FHiddenHost> HiddenHosts;
	TSharedPtr<IInputProcessor> KeyProc;                    // препроцессор Alt+Z
	bool bCursorHidden = false;                             // курсор вьюпорта спрятан нами
	TWeakObjectPtr<APlayerController> CursorPC;             // контроллер, чей курсор спрятан
	TEnumAsByte<EMouseCursor::Type> CursorWas = EMouseCursor::Default;   // его `CurrentMouseCursor` до скрытия
};
