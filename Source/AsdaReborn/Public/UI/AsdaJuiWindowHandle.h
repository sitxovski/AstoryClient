// Copyright (c) 2026 @sitxovski (Discord)
// AsdaJuiWindowHandle — КАРКАСЫ ОКОН JUI (волна 3 аудита дублей, п. 13 плана
//   `Reports/dedup_audit/00_PLAN.md`; отчёт `Reports/dedup_audit/wave3_jui_frameworks.md`).
//
// ★★★ ЧТО ЭТО И ЧЕГО ЗДЕСЬ НЕТ.
//   Только общий КОД, который был выписан открытым текстом в десятках подсистем окон:
//     • `FAsdaJuiWindowHandle::Ensure` — «кэш окна годен? иначе снести старое → открыть WBP →
//       привязать контролы → скрыть» (≈30 копий `Ensure*`);
//     • `FAsdaJuiWindowHandle::RegisterFrameZone` — зона окна по планам рамки 3×3;
//     • `FAsdaPopupMenu` — прямоугольник, угол и перенос всплывающего меню (меню 2000 / 28000);
//     • `FAsdaGaugeFill` — обрезка заливки полосы (ES1) по доле;
//     • `AsdaShopUi::` — общие куски витрин магазинов.
//   Разметку виджетов этот код НЕ правит (закон `no-runtime-widget-fixes`): он делает ровно то же,
//   что делали копии, в том же порядке. Отличия копий НЕ сведены — они параметризуются вызывающим
//   (перечень — в отчёте волны).
//
// ★ ПОЧЕМУ НЕ БАЗОВЫЙ КЛАСС. У подсистем окон разные базы (`UGameInstanceSubsystem`,
//   `FTickableGameObject`, интерфейсы), а поля окна (`Widget`, `bReady`) — UPROPERTY конкретного
//   класса. Поэтому каркас — композиция: он работает над ссылками на поля окна и не владеет ими.
#pragma once

#include "CoreMinimal.h"
#include "Templates/Function.h"
#include "UObject/ObjectPtr.h"

class UGameInstance;
class UUserWidget;
class UWidget;
class UImage;
class UTextBlock;
class UAsdaWindowSubsystem;
enum class EAsdaTipOwner : uint8;   // `UI/AsdaTipPanel.h`
enum class ESlateVisibility : uint8;   // `Components/SlateWrapperTypes.h` — видимость корня у `EnsureLazy`

// Итог `FAsdaJuiWindowHandle::Ensure` — какой путь прошёл каркас.
enum class EAsdaJuiEnsure : uint8
{
	NoWindowManager,   // нет `UAsdaWindowSubsystem` — ничего не делали (копии: `return false`)
	AlreadyLive,       // кэш годен: `IsWindowLive(Widget) && bReady` — ничего не делали (копии: `return true`)
	LoadFailed,        // `OpenWindowByPath` вернул nullptr, `OnLoadFailed` уже вызван (копии: `return false`)
	Created,           // окно открыто, `Setup` выполнен, окно скрыто `SetWindowVisible(false)`
};

// Описание окна JUI: id в реестре окон, путь WBP, z-порядок.
struct ASDAREBORN_API FAsdaJuiWindowHandle
{
	FName WindowId;                  // id в реестре `UAsdaWindowSubsystem` (он же id зоны у большинства окон)
	const TCHAR* WbpPath = nullptr;  // путь WBP (`/Game/.../WBP_X`), как у `OpenWindowByPath`
	int32 ZOrder = 0;                // z-порядок окна во вьюпорте

	// Конструктор описания окна.
	FAsdaJuiWindowHandle(FName InWindowId, const TCHAR* InWbpPath, int32 InZOrder)
		: WindowId(InWindowId), WbpPath(InWbpPath), ZOrder(InZOrder) {}

	// ★★★ КАРКАС `Ensure*`. Порядок шагов ЖЁСТКИЙ и равен порядку копий:
	//   1) `WM = GI->GetSubsystem<UAsdaWindowSubsystem>()`; нет — `NoWindowManager`;
	//   2) `WM->IsWindowLive(Widget) && bReady` — `AlreadyLive` (закон «окно пережило травел»: `IsWindowLive`);
	//   3) если `Widget` не пуст — `DropStale(*WM)` (сброс протухшего окна; у каждого окна свой);
	//   4) `Widget = WM->OpenWindowByPath(WindowId, WbpPath, ZOrder, SelfHitTestInvisible)`;
	//      корень SelfHitTestInvisible: растянутый на вьюпорт UUserWidget с Visible съел бы клики по миру;
	//   5) nullptr — `OnLoadFailed()` (лог окна своей категорией) и `LoadFailed`;
	//   6) `Setup(*WM)` — привязка контролов, зоны, приоритет z, закрыватели (порядок внутри — окна);
	//   7) `WM->SetWindowVisible(WindowId, false)` — в `.JUI` корень окна `Visible="0"`;
	//   8) `AfterHide(*WM)` — шаги окна ПОСЛЕ скрытия (сброс флагов, закрыватель, привязка у окон,
	//      которые прячутся до разбора контролов); 9) `Created`.
	//   `bReady` — значение флага готовности окна на момент вызова (читается только на шаге 2).
	//   Окну без шага 3 (отряд ×4) передаётся пустая лямбда: при пустом `DropStale` путь тот же.
	EAsdaJuiEnsure Ensure(UGameInstance* GI, TObjectPtr<UUserWidget>& Widget, bool bReady,
		TFunctionRef<void(UAsdaWindowSubsystem& WM)> DropStale,
		TFunctionRef<void(UAsdaWindowSubsystem& WM)> Setup,
		TFunctionRef<void()> OnLoadFailed,
		TFunctionRef<void(UAsdaWindowSubsystem& WM)> AfterHide) const;

	// То же без шага 8 (у большинства окон после скрытия ничего нет).
	EAsdaJuiEnsure Ensure(UGameInstance* GI, TObjectPtr<UUserWidget>& Widget, bool bReady,
		TFunctionRef<void(UAsdaWindowSubsystem& WM)> DropStale,
		TFunctionRef<void(UAsdaWindowSubsystem& WM)> Setup,
		TFunctionRef<void()> OnLoadFailed) const;

	// ★★ ВТОРОЙ ВАРИАНТ КАРКАСА — «ОТКРЫТЬ ПРИ ПЕРВОМ ПОКАЗЕ» (дедуп №2, В1-B, U22, 11.10.2026). Сведены копии соулмейта
	//   (6500/8200/8300/8400/9100), окна чужого персонажа 4200 и системного меню 69400/69500. Отличия от `Ensure`: окну
	//   НЕ навязывается скрытие (`SetWindowVisible(false)` не зовётся — видимость корня задаёт `RootVisibility`, показ решает
	//   вызывающий), сброс старого окна `BeforeOpen` зовётся ВСЕГДА, когда кэш негоден (у копий он не зависел от того, пуст ли
	//   указатель), а закрыватель общих путей (Esc / «закрыть всё») регистрируется здесь. Порядок шагов ЖЁСТКИЙ:
	//   1) `WM.IsWindowLive(Widget) && bReady` — `AlreadyLive` (ничего не делали);
	//   2) `BeforeOpen()` — сброс полей прежнего окна (пустая лямбда, если сбрасывать нечего);
	//   3) `Widget = WM.OpenWindowByPath(WindowId, WbpPath, ZOrder, RootVisibility)`;
	//   4) nullptr — `OnLoadFailed()` (лог окна своей категорией) и `LoadFailed`;
	//   5) `Setup(WM)` — привязка контролов, подписи, постановка окна (порядок внутри — окна);
	//   6) `Closer` связан — `WM.RegisterWindowCloser(WindowId, Closer)` (роспись только хранится, поэтому её место после
	//      привязки ничего не меняет); 7) `Created`.
	EAsdaJuiEnsure EnsureLazy(UAsdaWindowSubsystem& WM, TObjectPtr<UUserWidget>& Widget, bool bReady,
		ESlateVisibility RootVisibility,
		TFunctionRef<void()> BeforeOpen,
		TFunctionRef<void()> OnLoadFailed,
		TFunctionRef<void(UAsdaWindowSubsystem& WM)> Setup,
		FSimpleDelegate Closer = FSimpleDelegate()) const;

	// Окно есть (кэш годен или только что создано).
	static bool Succeeded(EAsdaJuiEnsure R) { return R == EAsdaJuiEnsure::AlreadyLive || R == EAsdaJuiEnsure::Created; }

	// ★ ЗОНА ОКНА ПО ПЛАНАМ РАМКИ: `RegisterUiZone(ZoneId, слабая лямбда Owner)`, лямбда отвечает
	//   `!bOpen || !Widget → false`, иначе `AsdaUi::FramePlanesAbsRect(Widget, PlaneFmt, PlaneCount)`.
	//   `Widget` и `bOpen` — ССЫЛКИ на поля `Owner`: читаются в момент запроса зоны, как `this->` у копий.
	//   `PlaneFmt` обязан жить вечно (литерал `TEXT("…_0_%d")`).
	static void RegisterFrameZone(UAsdaWindowSubsystem& WM, FName ZoneId, UObject* Owner,
		const TObjectPtr<UUserWidget>& Widget, const bool& bOpen, const TCHAR* PlaneFmt, int32 PlaneCount = 9);

	// ★ Ревизия 15.09: `Widget`/`bOpen` живут в лямбде зоны ПО ССЫЛКЕ — временный объект (сырой `UUserWidget*`,
	//   битовое поле `uint8 b:1`, выражение) дал бы висячую ссылку. Такие вызовы запрещены на этапе компиляции.
	static void RegisterFrameZone(UAsdaWindowSubsystem& WM, FName ZoneId, UObject* Owner,
		TObjectPtr<UUserWidget>&& Widget, const bool& bOpen, const TCHAR* PlaneFmt, int32 PlaneCount = 9) = delete;
	static void RegisterFrameZone(UAsdaWindowSubsystem& WM, FName ZoneId, UObject* Owner,
		const TObjectPtr<UUserWidget>& Widget, bool&& bOpen, const TCHAR* PlaneFmt, int32 PlaneCount = 9) = delete;
};

// Всплывающее меню окна (меню игрока 2000 `TRANTOOL_WIN`, меню «Хвастовство» 28000 `Open_Item_Win`).
//   Кнопки, действия и захват верхнего слоя остаются в окне; здесь — только геометрия и скрытие.
struct ASDAREBORN_API FAsdaPopupMenu
{
	// Добавить живой прямоугольник узла к накопителю. Свёрнутый/без геометрии узел пропускается.
	static bool AccumRect(const UWidget* W, FVector2D& Mn, FVector2D& Mx, bool& bAny);

	// Левый верхний угол меню — минимум оффсетов слотов корневой канвы. false — корня/слотов нет.
	static bool GetTopLeft(const UUserWidget* Menu, FVector2D& Out);

	// Сдвинуть ВСЕ дети корневой канвы так, чтобы угол меню встал в `TopLeft` (иерархия JUI расплющена).
	static void PlaceAt(UUserWidget* Menu, const FVector2D& TopLeft);

	// Прямоугольник корпуса меню по списку узлов-планов (объединять ВСЕ узлы нельзя — зону раздувают
	//   скрытые кнопки). Ни один узел не дал геометрии: `bFallbackToWidgetRect` ? прямоугольник всего
	//   виджета : false. ⚠ Флаг НЕ унифицирован: меню 2000 — с фолбэком, меню 28000 — без (E8, отчёт).
	static bool BodyRect(const UUserWidget* Menu, TArrayView<const TCHAR*> BodyNodes,
		bool bFallbackToWidgetRect, FVector2D& OutMin, FVector2D& OutMax);

	// Скрыть окно меню и вернуть захваченный верхний слой: `SetWindowVisible(false)` → `PopTopmost`.
	static void HideAndPopTopmost(UGameInstance* GI, FName WindowId, FName ClaimId);
};

// Заливка полосы (план ES1 скина `*_Bar`): ширина = полная × доля, левый край — базовый.
struct ASDAREBORN_API FAsdaGaugeFill
{
	// `Left = BaseLeft`, `Right = FullW × clamp(Frac, 0, 1)`. Нет слота или `FullW <= 0` — ничего.
	//   ⚠ Полоса отряда (`UAsdaPartySubsystem::ClipBar`) сюда НЕ сведена: она не пишет `Left` и
	//   пропускает неизменную ширину — это другое поведение (отчёт волны).
	static void Clip(UImage* Fill, float BaseLeft, float FullW, float Frac);
};

// Общие куски витрин магазинов. Пакеты, валюты и loc — в окнах (1:1 с сервером).
namespace AsdaShopUi
{
	// Надпись-клон по геометрии узла `Ref` в том же родителе окна `Host`: без переноса строк,
	//   мышь не ловит, шрифт донора, по желанию выравнивание вправо (витрины 6983 и 67200).
	ASDAREBORN_API UTextBlock* MakeLabelLike(UUserWidget* Host, UWidget* Ref, const TCHAR* NewName,
		const UTextBlock* FontDonor, bool bRightAlign);

	// Метки страниц «номер + 1» форматом `%d` (порт `tradeWinUpdatePageLabels@0xC37BC0`).
	ASDAREBORN_API void SetPageLabels(UTextBlock* TxtPage, UTextBlock* TxtPages, int32 Page, int32 MaxPage);

	// Плашка предмета карточки под курсором: показывается, только когда карточка сменилась.
	//   `TipIdx` — поле окна «что показано»; `ItemCodeAt(Over)` зовётся только для `Over >= 0`.
	ASDAREBORN_API void TickCardTooltip(UGameInstance* GI, int32& TipIdx, int32 OverIdx, EAsdaTipOwner Owner,
		TFunctionRef<int32(int32)> ItemCodeAt);

	// Кнопка «проверить баланс»: пакет 5466 с выбранным персонажем.
	ASDAREBORN_API void QueryPremiumBalance(UGameInstance* GI);

	// Результат раскладки рамки витрины (для лога окна).
	struct FShowcaseHeight
	{
		int32 ByRes = 0;   // рядов по таблице разрешений
		int32 Shown = 0;   // показано карточек
		int32 Rows  = 0;   // итоговое число мест (кратно 3)
		int32 NewH  = 0;   // высота рамки, px
		float MidH  = 0.f; // высота среднего ряда 9-slice
		float BotY  = 0.f; // верх нижнего ряда 9-slice
	};

	// Высота рамки витрины `98 × (ряды/3) + 48` на 9 планах 3×3. `ByResFn` зовётся после проверки
	//   планов (как в копиях). true — рамка переложена (`InOutApplied = NewH`), вызывающий логирует.
	ASDAREBORN_API bool ApplyShowcaseHeight(const TArray<TObjectPtr<UWidget>>& PanelPlanes,
		TFunctionRef<int32()> ByResFn, int32 VisibleCards, int32& InOutApplied, FShowcaseHeight& Out);

	// Рамка выбора карточки: все планы `HitTestInvisible` или `Collapsed`.
	ASDAREBORN_API void SetSelectionVisible(const TArray<TObjectPtr<UWidget>>& Parts, bool bVisible);
}
