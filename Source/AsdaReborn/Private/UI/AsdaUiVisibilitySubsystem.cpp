// Copyright (c) 2026 @sitxovski (Discord)
// Реализация флага «интерфейс скрыт» (Alt+Z). Устройство и исправления — в шапке `Public/UI/AsdaUiVisibilitySubsystem.h`,
//   разбор эталона — `Reports/hide_ui_altz/01_etalon.md`, ревизия — `review_1.md`.
#include "UI/AsdaUiVisibilitySubsystem.h"

#include "AsdaDevConsole.h"
#include "AsdaNetSubsystem.h"
#include "UI/AsdaWindowSubsystem.h"
#include "UI/AsdaAfterDeathSubsystem.h"
#include "UI/AsdaUiHelpers.h"
#include "World/AsdaWorldKind.h"
#include "Blueprint/UserWidget.h"
#include "Engine/GameInstance.h"
#include "Engine/GameViewportClient.h"
#include "Engine/World.h"
#include "Framework/Application/IInputProcessor.h"
#include "Framework/Application/SlateApplication.h"
#include "GameFramework/PlayerController.h"
#include "HAL/IConsoleManager.h"
#include "Widgets/SWindow.h"

DEFINE_LOG_CATEGORY_STATIC(LogAsdaUiHide, Log, All);

// ★ Откат исправлений дефектов оригинала (шапка, пункты 1–5): 0 — поведение 1:1 (мигание на автоповторе, невидимые модалки,
//   окна ловят мышь, скрытие переживает выход в выбор персонажа). Ключ разработчика — в Shipping его нет (там всегда 1).
static ASDA_DEV_CVAR(int32, CVarAsdaUiHideFixes,
	TEXT("asda.UI.HideFixes"), 1,
	TEXT("Alt+Z: 1 — исправления дефектов оригинала (без мигания на автоповторе, модалка/меню смерти показывают интерфейс, "
	     "скрытые окна не ловят мышь, выход в выбор персонажа показывает интерфейс); 0 — поведение оригинала 1:1."),
	ECVF_Default);

// ★ [ИСПРАВЛЕНИЕ] по решению владельца (01.11.2026): курсор мыши прячется вместе с интерфейсом. 0 — прежнее (курсор виден,
//   как у оригинала). Ключ разработчика — в Shipping его нет (там всегда 1).
static ASDA_DEV_CVAR(int32, CVarAsdaUiHideCursor,
	TEXT("asda.UI.HideCursor"), 1,
	TEXT("Alt+Z: 1 — при скрытом интерфейсе курсор мыши тоже скрыт (возвращается прежнего вида при показе); 0 — курсор виден, "
	     "как у оригинала."),
	ECVF_Default);

namespace AsdaUiHideLocal
{
	// Исправления включены.
	static bool Fixes() { return CVarAsdaUiHideFixes.GetValueOnGameThread() != 0; }
	// Курсор прячется вместе с интерфейсом.
	static bool HideCursor() { return CVarAsdaUiHideCursor.GetValueOnGameThread() != 0; }

	// Препроцессор ввода Slate: видит нажатие раньше фокуса (поле чата, окна), как WM_SYSKEYDOWN у оригинала.
	class FKeyProc : public IInputProcessor
	{
	public:
		// Владелец флага (слабо: подсистема может уйти раньше препроцессора).
		TWeakObjectPtr<UAsdaUiVisibilitySubsystem> Owner;

		// Кадровый тик препроцессора — не нужен.
		virtual void Tick(const float, FSlateApplication&, TSharedRef<ICursor>) override {}
		// Нажатие: Z при зажатом Alt (без Ctrl — Ctrl+Alt+Z у оригинала приходит как WM_KEYDOWN, отчёт §1.2). true — съедено.
		virtual bool HandleKeyDownEvent(FSlateApplication& App, const FKeyEvent& E) override
		{
			if (E.GetKey() != EKeys::Z || !E.IsAltDown() || E.IsControlDown()) { return false; }
			UAsdaUiVisibilitySubsystem* S = Owner.Get();
			return S && S->HandleAltZ(E.IsRepeat());
		}
		// Имя для отладки цепочки препроцессоров Slate.
		virtual const TCHAR* GetDebugName() const override { return TEXT("AsdaUiHideAltZ"); }
	};

	// Обёртка вьюпорта: `UGameViewportSubsystem::AddToScreen` кладёт каждый виджет в свой `SConstraintCanvas` — родитель корня.
	static TSharedPtr<SWidget> HostOf(const UUserWidget* W)
	{
		if (!W || !W->IsInViewport()) { return nullptr; }
		const TSharedPtr<SWidget> Root = W->GetCachedWidget();
		const TSharedPtr<SWidget> Host = Root.IsValid() ? Root->GetParentWidget() : nullptr;
		static const FName kCanvas(TEXT("SConstraintCanvas"));
		return (Host.IsValid() && Host->GetType() == kCanvas) ? Host : nullptr;   // чужой родитель — не трогаем
	}
}

void UAsdaUiVisibilitySubsystem::Initialize(FSubsystemCollectionBase& Collection)
{
	Super::Initialize(Collection);
	if (FSlateApplication::IsInitialized() && !IsRunningCommandlet())
	{
		TSharedRef<AsdaUiHideLocal::FKeyProc> P = MakeShared<AsdaUiHideLocal::FKeyProc>();
		P->Owner = this;
		KeyProc = P;
		// Первым в цепочке: сторож рыбалки (`AsdaFishZoneSubsystem`) съедает Alt-сочетания, а у оригинала Alt+Z при рыбалке работает.
		FSlateApplication::Get().RegisterInputPreProcessor(P, 0);
	}
}

void UAsdaUiVisibilitySubsystem::Deinitialize()
{
	if (KeyProc.IsValid() && FSlateApplication::IsInitialized()) { FSlateApplication::Get().UnregisterInputPreProcessor(KeyProc); }
	KeyProc.Reset();
	bUiShown = true;   // вернуть обёртки без события: соседние подсистемы уже разбираются
	ApplyAll();
	Tracked.Reset();
	Super::Deinitialize();
}

void UAsdaUiVisibilitySubsystem::ResetSessionState(EAsdaSessionResetReason Reason)
{
	// Эталон не сбрасывает нигде. Порт: разрыв связи — ВСЕГДА показать (оригинал в этот момент закрывает клиент, у порта — экран
	//   входа, закон `disconnect-ok-returns-to-login`); выход в выбор персонажа / новый вход — показать [ИСПРАВЛЕНИЕ 5].
	if (Reason == EAsdaSessionResetReason::Disconnected) { SetUiShown(true, TEXT("разрыв связи")); }
	else if (AsdaIsFullSessionReset(Reason) && AsdaUiHideLocal::Fixes()) { SetUiShown(true, AsdaSessionResetReasonName(Reason)); }
	ApplyAll();   // смена мира: на сцене входа скрытие не действует (см. `ShouldHide`)
}

bool UAsdaUiVisibilitySubsystem::IsUiShownFor(const UObject* Ctx)
{
	const UWorld* W = Ctx ? Ctx->GetWorld() : nullptr;
	const UGameInstance* GI = W ? W->GetGameInstance() : Cast<UGameInstance>(Ctx);
	if (!GI) { if (const UGameInstanceSubsystem* Sub = Cast<UGameInstanceSubsystem>(Ctx)) { GI = Sub->GetGameInstance(); } }
	const UAsdaUiVisibilitySubsystem* S = GI ? GI->GetSubsystem<UAsdaUiVisibilitySubsystem>() : nullptr;
	return !S || !S->ShouldHide();
}

bool UAsdaUiVisibilitySubsystem::HiddenBlocksUiHitTest(const UObject* Ctx)
{
	return AsdaUiHideLocal::Fixes() && !IsUiShownFor(Ctx);
}

bool UAsdaUiVisibilitySubsystem::ShouldHide() const
{
	// Выбор персонажа у оригинала рисует свой путь (`CRenderer_RenderFrame`) без проверки бита — там интерфейс виден всегда.
	const UGameInstance* GI = GetGameInstance();
	const UWorld* W = GI ? GI->GetWorld() : nullptr;
	return !bUiShown && !(W && AsdaWorldKind::IsLoginScene(W));
}

void UAsdaUiVisibilitySubsystem::SetUiShown(bool bShown, const TCHAR* Why)
{
	if (bShown == bUiShown) { return; }
	bUiShown = bShown;
	UE_LOG(LogAsdaUiHide, Log, TEXT("[интерфейс] %s (%s)"), bShown ? TEXT("ПОКАЗАН") : TEXT("СКРЫТ (Alt+Z)"), Why ? Why : TEXT(""));
	ApplyAll();
	OnUiShownChanged.Broadcast(bUiShown);
}

FString UAsdaUiVisibilitySubsystem::GetToggleBlockReason() const
{
	// Гейты `0x98548E`: g_GameState == 1, !CUIManager+0x44 (меню смерти / защищённый запрос), !+0x04 (предмет в руке),
	//   !CWindowMgr+0x200 (модальное окно).
	const UGameInstance* GI = GetGameInstance();
	const UWorld* W = GI ? GI->GetWorld() : nullptr;
	if (!W || AsdaWorldKind::IsLoginScene(W)) { return TEXT("не мир"); }
	const APlayerController* PC = GI->GetFirstLocalPlayerController(W);
	if (!PC || !PC->GetPawn()) { return TEXT("нет своего персонажа (загрузка)"); }
	if (const UAsdaNetSubsystem* Net = GI->GetSubsystem<UAsdaNetSubsystem>(); Net && Net->bSelfDead) { return TEXT("персонаж мёртв (состояние 8)"); }
	if (const UAsdaAfterDeathSubsystem* Ad = GI->GetSubsystem<UAsdaAfterDeathSubsystem>(); Ad && Ad->IsDeathMenuOpen()) { return TEXT("меню смерти"); }
	if (FAsdaDragGhost::IsAnyActive()) { return TEXT("предмет в руке"); }
	if (const UAsdaWindowSubsystem* WM = GI->GetSubsystem<UAsdaWindowSubsystem>(); WM && WM->IsAnyModalOpen()) { return TEXT("модальное окно"); }
	return FString();
}

bool UAsdaUiVisibilitySubsystem::ToggleFromHotkey(const TCHAR* Src)
{
	const FString Block = GetToggleBlockReason();
	if (!Block.IsEmpty())
	{
		UE_LOG(LogAsdaUiHide, Verbose, TEXT("[интерфейс] Alt+Z отклонён: %s (%s)"), *Block, Src ? Src : TEXT(""));
		return false;
	}
	SetUiShown(!bUiShown, Src);
	return true;
}

bool UAsdaUiVisibilitySubsystem::HandleAltZ(bool bRepeat)
{
	// В редакторе (PIE) препроцессор глобален — реагируем, только если активно окно ЭТОЙ игры (иначе Alt+Z в окнах редактора
	//   и в соседних клиентах PIE переключал бы всех).
	if (GIsEditor && FSlateApplication::IsInitialized())
	{
		const UGameViewportClient* VP = GetGameInstance() ? GetGameInstance()->GetGameViewportClient() : nullptr;
		const TSharedPtr<SWindow> Mine = VP ? VP->GetWindow() : nullptr;
		if (!Mine.IsValid() || FSlateApplication::Get().GetActiveTopLevelWindow() != Mine) { return false; }
	}
	// [ИСПРАВЛЕНИЕ 1] автоповтор не переключает (у оригинала удержание мигало). Нажатие съедается в любом случае — у эталона
	//   ветка WM_SYSKEYDOWN возвращает 0 и DefWindowProc не зовёт.
	if (bRepeat && AsdaUiHideLocal::Fixes()) { return true; }
	ToggleFromHotkey(bRepeat ? TEXT("Alt+Z (автоповтор)") : TEXT("Alt+Z"));
	return true;
}

void UAsdaUiVisibilitySubsystem::Tick(float DeltaTime)
{
	// Курсор: новый контроллер после смены карты приходит с видимым курсором, ключ могли переключить — держим каждый кадр.
	ApplyCursor();
	// [ИСПРАВЛЕНИЕ 2–3] модальное окно и меню смерти у оригинала появлялись невидимыми и запирали Alt+Z — показываем интерфейс.
	if (bUiShown || !AsdaUiHideLocal::Fixes()) { return; }
	const UGameInstance* GI = GetGameInstance();
	const UAsdaWindowSubsystem* WM = GI ? GI->GetSubsystem<UAsdaWindowSubsystem>() : nullptr;
	const UAsdaAfterDeathSubsystem* Ad = GI ? GI->GetSubsystem<UAsdaAfterDeathSubsystem>() : nullptr;
	if (WM && WM->IsAnyModalOpen()) { SetUiShown(true, TEXT("открыто модальное окно")); return; }
	if (Ad && Ad->IsDeathMenuOpen()) { SetUiShown(true, TEXT("меню смерти")); }
}

void UAsdaUiVisibilitySubsystem::Track(UUserWidget* Widget)
{
	if (!Widget) { return; }
	Tracked.RemoveAll([](const TWeakObjectPtr<UUserWidget>& P) { return !P.IsValid(); });
	Tracked.AddUnique(Widget);
	ApplyTo(Widget);
}

void UAsdaUiVisibilitySubsystem::TrackFor(const UObject* Ctx, UUserWidget* Widget)
{
	const UWorld* W = Ctx ? Ctx->GetWorld() : nullptr;
	const UGameInstance* GI = W ? W->GetGameInstance() : Cast<UGameInstance>(Ctx);
	if (!GI) { if (const UGameInstanceSubsystem* Sub = Cast<UGameInstanceSubsystem>(Ctx)) { GI = Sub->GetGameInstance(); } }
	if (UAsdaUiVisibilitySubsystem* S = GI ? GI->GetSubsystem<UAsdaUiVisibilitySubsystem>() : nullptr) { S->Track(Widget); }
}

bool UAsdaUiVisibilitySubsystem::IsViewportHostVisible(const UUserWidget* Widget)
{
	const TSharedPtr<SWidget> Host = AsdaUiHideLocal::HostOf(Widget);
	return Host.IsValid() && Host->GetVisibility().IsVisible() && Host->GetRenderOpacity() > 0.f;
}

bool UAsdaUiVisibilitySubsystem::IsViewportHostFocusable(const UUserWidget* Widget)
{
	// Фокус клавиатуры Slate отбрасывает только невидимые (`Collapsed`/`Hidden`) ветки пути — `HitTestInvisible` его пропускает.
	const TSharedPtr<SWidget> Host = AsdaUiHideLocal::HostOf(Widget);
	return Host.IsValid() && Host->GetVisibility().IsVisible();
}

void UAsdaUiVisibilitySubsystem::ApplyTo(UUserWidget* Widget)
{
	const TSharedPtr<SWidget> Host = AsdaUiHideLocal::HostOf(Widget);
	if (!Host.IsValid()) { return; }
	FHiddenHost* Rec = HiddenHosts.FindByPredicate([&Host](const FHiddenHost& H) { return H.Host.Pin() == Host; });
	if (ShouldHide())
	{
		if (Rec) { return; }   // уже спрятана
		FHiddenHost H;
		H.Host = Host;
		H.Was = Host->GetVisibility();
		H.WasOpacity = Host->GetRenderOpacity();
		HiddenHosts.Add(H);
		// Не рисуется (прозрачность 0) и не ловит мышь Slate (`HitTestInvisible`), но ОСТАЁТСЯ на пути фокуса: набор в чате и
		//   ввод в модалке при скрытом интерфейсе работают, как у оригинала (ревизия С-2: `Collapsed` рвал путь фокуса).
		Host->SetVisibility(EVisibility::HitTestInvisible);
		Host->SetRenderOpacity(0.f);
	}
	else if (Rec)
	{
		Host->SetVisibility(Rec->Was);
		Host->SetRenderOpacity(Rec->WasOpacity);
		HiddenHosts.RemoveAll([&Host](const FHiddenHost& H) { return !H.Host.IsValid() || H.Host.Pin() == Host; });
	}
}

void UAsdaUiVisibilitySubsystem::ApplyAll()
{
	// Опрос мыши окон по геометрии (`FAsdaMouseFrame`, `IsCursorOverWidget`) слепнет вместе с интерфейсом [ИСПРАВЛЕНИЕ 4].
	AsdaUi::GUiInputHidden = ShouldHide() && AsdaUiHideLocal::Fixes();
	Tracked.RemoveAll([](const TWeakObjectPtr<UUserWidget>& P) { return !P.IsValid(); });
	for (const TWeakObjectPtr<UUserWidget>& P : Tracked) { ApplyTo(P.Get()); }
	if (!ShouldHide())
	{
		// Обёртки, которые после скрытия ушли из вьюпорта или сменились, — вернуть по записи.
		for (const FHiddenHost& H : HiddenHosts)
		{
			if (TSharedPtr<SWidget> Host = H.Host.Pin()) { Host->SetVisibility(H.Was); Host->SetRenderOpacity(H.WasOpacity); }
		}
		HiddenHosts.Reset();
	}
	ApplyCursor();
}

void UAsdaUiVisibilitySubsystem::ApplyCursor()
{
	const UGameInstance* GI = GetGameInstance();
	const UWorld* W = GI ? GI->GetWorld() : nullptr;
	APlayerController* PC = (GI && W) ? GI->GetFirstLocalPlayerController(W) : nullptr;
	bool bChanged = false;
	if (ShouldHide() && AsdaUiHideLocal::HideCursor())
	{
		if (PC && (CursorPC.Get() != PC || PC->CurrentMouseCursor != EMouseCursor::None))
		{
			// Чужой (прежний) контроллер уже ушёл со сменой карты; запоминаем, что держал новый, и прячем.
			if (PC->CurrentMouseCursor != EMouseCursor::None) { CursorWas = PC->CurrentMouseCursor; }
			CursorPC = PC;
			PC->CurrentMouseCursor = EMouseCursor::None;
			bChanged = true;
		}
		bCursorHidden = PC != nullptr;
	}
	else if (bCursorHidden)
	{
		// Вернуть тот курсор вьюпорта, что был до скрытия; новый контроллер — его умолчание. Вид (файл .cur/.ani) держит игра.
		if (APlayerController* Old = CursorPC.Get(); Old && Old != PC && Old->CurrentMouseCursor == EMouseCursor::None)
		{
			Old->CurrentMouseCursor = CursorWas;
		}
		if (PC && PC->CurrentMouseCursor == EMouseCursor::None)
		{
			PC->CurrentMouseCursor = (CursorPC.Get() == PC) ? CursorWas : PC->DefaultMouseCursor;
		}
		bCursorHidden = false;
		CursorPC.Reset();
		bChanged = true;
	}
	if (bChanged)
	{
		UE_LOG(LogAsdaUiHide, Log, TEXT("[интерфейс] курсор мыши %s"), bCursorHidden ? TEXT("СКРЫТ") : TEXT("ПОКАЗАН"));
		// Slate спрашивает курсор у вьюпорта по движению мыши — спросить сразу, иначе неподвижная мышь осталась бы прежней.
		if (FSlateApplication::IsInitialized() && !IsRunningCommandlet()) { FSlateApplication::Get().QueryCursor(); }
	}
}

#if ASDA_DEV_CONSOLE
// Диагностика: `asda.UI.Hide 1` — скрыть, `0` — показать (без гейтов Alt+Z).
static FAutoConsoleCommandWithWorldAndArgs GAsdaUiHideCmd(
	TEXT("asda.UI.Hide"),
	TEXT("Скрыть (1) или показать (0) интерфейс, как Alt+Z, но без гейтов. Без аргумента — печать состояния."),
	FConsoleCommandWithWorldAndArgsDelegate::CreateLambda([](const TArray<FString>& A, UWorld* W)
	{
		UAsdaUiVisibilitySubsystem* S = (W && W->GetGameInstance()) ? W->GetGameInstance()->GetSubsystem<UAsdaUiVisibilitySubsystem>() : nullptr;
		if (!S) { return; }
		if (A.Num() > 0) { S->SetUiShown(FCString::Atoi(*A[0]) == 0, TEXT("консоль asda.UI.Hide")); }
		UE_LOG(LogAsdaUiHide, Display, TEXT("[интерфейс] %s; Alt+Z сейчас: %s"), S->IsUiShown() ? TEXT("показан") : TEXT("скрыт"),
			S->GetToggleBlockReason().IsEmpty() ? TEXT("разрешён") : *S->GetToggleBlockReason());
	}));
#endif
