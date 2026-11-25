// Copyright (c) 2026 @sitxovski (Discord)

#include "UI/AsdaPremiumGachaSubsystem.h"
#include "AsdaDevConsole.h"   // ★ античит А2: ASDA_DEV_CVAR / ASDA_DEV_CONSOLE — дев-ключи и команды вне Shipping
#include "UI/AsdaPremiumPostSubsystem.h"   // отложенный показ приза: сообщаем ящику итог выбора
#include "UI/AsdaWindowSubsystem.h"
#include "UI/AsdaSystemMenuSubsystem.h"
#include "UI/AsdaGachaSceneActor.h"
#include "Engine/TextureRenderTarget2D.h"
#include "EngineUtils.h"                          // TActorIterator — поиск актора сцены для диагностики
#include "Kismet/KismetRenderingLibrary.h"   // ★ 24.09: выгрузка буфера сцены гачи в PNG (asda.Gacha.DumpRt)
#include "Components/Image.h"
#include "Engine/World.h"
#include "AsdaNetSubsystem.h"
#include "Blueprint/UserWidget.h"
#include "Blueprint/WidgetTree.h"
#include "Components/TextBlock.h"
#include "Components/Button.h"
#include "Components/ComboBoxString.h"
#include "Components/CanvasPanel.h"
#include "Components/CanvasPanelSlot.h"
#include "Components/PanelWidget.h"
#include "Engine/DataTable.h"
#include "Items/AsdaItemDbRows.h"
#include "Items/AsdaItemTableSubsystem.h"
#include "Resources/AsdaTextureSubsystem.h"
#include "Resources/AsdaSoundSubsystem.h"
#include "UI/AsdaIconCellGrid.h"
#include "UI/AsdaAnimatedPlane.h"      // 68058 `Apple_Make3` — кадровый анимплан (RestartAnim = `CControl_Show`)
#include "UI/AsdaItemTooltipSubsystem.h" // общая плашка подсказки + владелец показа
#include "UI/AsdaInventorySubsystem.h"   // ★ общая точка модели контейнеров: ApplyExternalRecord (6582)
#include "UI/AsdaNotice.h"          // ★ 07.11.2026: окно-сообщение ONEBUT_MSGBOX (`CItem_BuildTooltipText(…, 0, -1, …)`)
#include "UI/AsdaUiHelpers.h"
#include "Components/AudioComponent.h"
#include "HAL/IConsoleManager.h"   // ★ 08.11.2026: ключ asda.Gacha.IdleLoop2D
#include "UI/AsdaJuiComboBox.h"   // ★ ОБЩАЯ точка: выпадающий список скина Bk_ComboBox
#include "UI/AsdaJuiWindowMeta.h"   // мета окон: ручка перетаскивания живёт в данных
#include "Framework/Application/SlateApplication.h"   // курсор для зоны колеса над окном 68310
#include "GameFramework/PlayerController.h"
#include "World/AsdaPlayerController.h"
#include "World/AsdaPlayerCharacter.h"
#include "World/AsdaCoord.h"   // ★ единый перевод UE→игра: тайл входа в сцену (шаг 9)
#include "UI/AsdaCraftSubsystem.h"         // ★ гейт входа: открыты ли окна ремесла 76000/77500 (волна В2)
#include "AsdaOutgoingGate.h"               // ★ С-Р1: общий офлайн-гейт отладочных команд (RefuseDebugWhenOnline)
#include "UI/AsdaItemRecordWriter.h"   // ★ U17 (В1 дедупа №2): синтетическая запись предмета 57 б
#include "AsdaPremiumGachaSubsystemInternal.h"   // ★ R2 10.11.2026: общее для частей AsdaPremiumGachaSubsystem*.cpp

DEFINE_LOG_CATEGORY(LogAsdaGacha);   // ★ R2: было DEFINE_LOG_CATEGORY_STATIC — категория общая для частей

namespace {
	const TCHAR* kUiRoot = TEXT("/Game/AsdaAssets/UI/Player_UI/Premium/");

	constexpr float kRollLen    = 2.000f;   // anim_02 = 2000.0 мс
}

// ★★★ 07.11.2026 решение владельца: «запретить закрывать окно, пока приз не забран; модальное окно на обоих языках». [ИСПРАВЛЕНИЕ]
//   Баг: закрыли сцену после розыгрыша, не вскрыв шар, — при новом открытии шара нет. Эталон так и устроен: кнопки 68076/68209/79109
//   закрывают сцену без проверок (ui_core.c:7624-7631), а `premiumGacha_BeginRoll` возвращает автоматы в покой; гейт клипа есть только у
//   ESC (ui_core.c:7666). Сам приз не пропадает: при выключенном P16 он лежит в элитном ящике уже к приходу 6578 (`sub_4F7D10`).
//   1 — пока идёт ожидание 6578, розыгрыш, шар, вскрытие или показано окно приза 68450, ЗАКРЫТИЕ ИГРОКОМ запрещено (кнопки, меню, ESC),
//       автозакрытие из-за боя отложено; показывается модальное окно 991202. То, что запретить нельзя (смерть, смена карты), закрывает
//       сцену, но приз запоминается и при новом открытии автомат снова выдаёт шар. 0 — прежнее поведение (как эталон).
ASDA_DEV_CVAR(int32, CVarGachaPrizeCloseLock,
	TEXT("asda.Gacha.PrizeCloseLock"), 1,
	TEXT("[ИСПРАВЛЕНИЕ] Премиум-гача: сцену нельзя закрыть, пока приз не забран (модальное окно 991202); при вынужденном закрытии шар ")
	TEXT("возвращается при новом открытии. 0 — прежнее поведение, как в оригинале."),
	ECVF_Default);

void UAsdaPremiumGachaSubsystem::Initialize(FSubsystemCollectionBase& Collection)
{
	Super::Initialize(Collection);
	Collection.InitializeDependency(UAsdaWindowSubsystem::StaticClass());   // роспись ESC окна 68000 (ниже)
	if (UAsdaNetSubsystem* Net = GetGameInstance() ? GetGameInstance()->GetSubsystem<UAsdaNetSubsystem>() : nullptr)
	{
		HBalance = Net->OnPremiumBalance.AddUObject(this, &UAsdaPremiumGachaSubsystem::OnBalance);
		HRoll    = Net->OnPremiumGachaResult.AddUObject(this, &UAsdaPremiumGachaSubsystem::OnRollResult);
		HApply   = Net->OnPremiumGachaApply.AddUObject(this, &UAsdaPremiumGachaSubsystem::OnPrizeApplied);
	}
	// ★ Вход в сцену — кнопка HUD 73410 `NEW_Primiumpick` («Лотобокс», подсказка loc 9801).
	//   Маршрут оригинала: `uiHotkey_DispatchCommand@0xE8F670` case 73410 → `premiumGacha_BeginRoll`;
	//   отжатие → `uiGuildPanelClose`. Кнопка — CheckButton, поэтому идём по её состоянию.
	//   ⚠ В таблице меню у 73410 указано целевое окно 68100 (диалог подтверждения) — это неточность
	//     записи: кнопка открывает ВСЮ СЦЕНУ, а 68100 показывается уже внутри неё, по «крутить».
	if (UAsdaSystemMenuSubsystem* Menu = GetGameInstance() ? GetGameInstance()->GetSubsystem<UAsdaSystemMenuSubsystem>() : nullptr)
	{
		Menu->OnMenuCommand.AddDynamic(this, &UAsdaPremiumGachaSubsystem::OnMenuButton);
		// ★★★ ПОДСВЕТКА КНОПКИ 73410 = `uiPanel_SetTabVisible(g_pSystemMainWnd, 10, 1)` в конце
		//   `premiumGacha_BeginRoll` (world_map.c:4754) и `(…, 10, 0)` в `uiGuildPanelClose`
		//   (ui_core.c:7202). У нас подсветку раздаёт ЕДИНЫЙ механизм меню «кнопка = её окно
		//   открыто», поэтому вместо двух ручных вызовов даём ему предикат состояния сцены.
		//   ⚠ Через `BoundWindowId` (в таблице у 73410 стоит 68100) это работать не может: 68100 —
		//     диалог подтверждения ВНУТРИ сцены, он виден лишь в момент «крутить», а таб 10 в
		//     оригинале горит всё время, пока открыта сцена.
		Menu->RegisterWindowPredicate(73410, [this]() { return bSceneOpen; });
	}
	// ★ 10.10.2026 [ОРИГИНАЛ]: ESC по корню сцены 68000 — эталонная ветка события 21 (разбор у `EscClose`).
	if (UAsdaWindowSubsystem* WM = GetGameInstance() ? GetGameInstance()->GetSubsystem<UAsdaWindowSubsystem>() : nullptr)
	{
		WM->RegisterEscCloser(FName(kRoot.Id), FSimpleDelegate::CreateUObject(this, &UAsdaPremiumGachaSubsystem::EscClose));
	}
	UE_LOG(LogAsdaGacha, Log, TEXT("[премиум-гача] подсистема поднята: сцена из 7 окон, вход — кнопка 73410"));
}

void UAsdaPremiumGachaSubsystem::EscClose()
{
	if (!bSceneOpen) { return; }
	if (RefuseCloseForPrize()) { return; }   // ★ 07.11.2026: приз не забран — объясняем окном, а не молчим
	// `sub_92839D(i)` → `sub_B6DDA0` → `sub_93DCCA(g_pGachaSceneCtrl, i)` — номер клипа автомата i; клип 1 = ПОКОЙ
	//   (его ставят 68452 «Нажми» и `premiumGacha_BeginRoll`). В порту клип выбранного автомата — фаза `Idle`.
	if (Phase != EAsdaGachaPhase::Idle)
	{
		UE_LOG(LogAsdaGacha, Log, TEXT("[премиум-гача] ESC съеден: автомат не в покое (фаза %d) — эталон сцену не закрывает"),
			(int32)Phase);
		return;
	}
	UE_LOG(LogAsdaGacha, Log, TEXT("[премиум-гача] ESC: сцена закрыта (uiGuildPanelClose + inventoryBag_Close)"));
	CloseScene();   // `inventoryBag_Close` = `uiGuildPanelClose` + скрыть 68200 — обе половины уже внутри `CloseScene`
}

void UAsdaPremiumGachaSubsystem::Deinitialize()
{
	if (UAsdaWindowSubsystem* EscWM = GetGameInstance() ? GetGameInstance()->GetSubsystem<UAsdaWindowSubsystem>() : nullptr)
	{
		EscWM->UnregisterEscCloser(FName(kRoot.Id));
	}
	if (UAsdaNetSubsystem* Net = GetGameInstance() ? GetGameInstance()->GetSubsystem<UAsdaNetSubsystem>() : nullptr)
	{
		Net->OnPremiumBalance.Remove(HBalance);
		Net->OnPremiumGachaResult.Remove(HRoll);
		Net->OnPremiumGachaApply.Remove(HApply);
	}
	if (bWheelBound)
	{
		if (UAsdaWindowSubsystem* WM = GetGameInstance() ? GetGameInstance()->GetSubsystem<UAsdaWindowSubsystem>() : nullptr)
		{
			WM->UnregisterWheelHandler(FName(kView.Id));
		}
		bWheelBound = false;
	}
	Super::Deinitialize();
}

// ★★★ К-3: СБРОС СОСТОЯНИЯ СЕССИИ (шапка `AsdaSessionReset.h`). Подсистема живёт на
//   `GameInstance` и переживает разрыв связи, выход в выбор персонажа и травел; всё, что
//   ниже, — данные и защёлки ОДНОЙ сессии. Отправок здесь нет (закон `no-reentrant-net-callbacks`).
void UAsdaPremiumGachaSubsystem::ResetSessionState(EAsdaSessionResetReason Reason)
{
	// [ИСПРАВЛЕНИЕ] 10.11.2026 «выбор до получения»: выбор, заряд и ожидание 6582 принадлежат сессии — после разрыва
	//   или смены карты старый вариант не должен уйти в 6581 по чужой записи.
	CancelPreTakeChoice();
	DisarmPostVariant();
	bPostSwapPending = false;
	if (bSceneOpen) { CloseScene(); }
	// ★ 07.11.2026: смена карты шар не отменяет (данные персонажа целы); разрыв и смена персонажа — отменяют, приз ждёт в элитном ящике.
	if (AsdaIsFullSessionReset(Reason)) { bResumePrize = false; ResumePrizeRecord.Reset(); }
	// `bRequestInFlight` — эталонный гейт от двойной отправки (`gap_10[628]`): снимается ТОЛЬКО
	// ответом сервера, поэтому после разрыва автомат больше не принял бы ни одного вращения.
	bRequestInFlight = false;
	bApplyPending = false;
	PendingApplyBody.Reset();
	Phase = EAsdaGachaPhase::Closed;
	PhaseTime = 0.f;
	Balance = 0;
	SelectedSlot = 0;
	GridScrollRow = 0; GridSelectedCell = -1; TipHoverCell = -1;
	GridMouse.Reset(); VarMouse.Reset();
}

// ───────────────────────────── окна ─────────────────────────────
bool UAsdaPremiumGachaSubsystem::EnsureWindows()
{
	UAsdaWindowSubsystem* WM = GetGameInstance() ? GetGameInstance()->GetSubsystem<UAsdaWindowSubsystem>() : nullptr;
	if (!WM) { return false; }
	if (bReady && WM->IsWindowLive(RootWin)) { return true; }

	// ★ Окна пересоздаются (первый вход либо после `CloseAll` на шаге 5) ⇒ ВСЕ подобъекты, живущие
	//   в их дереве, обязаны быть сброшены: иначе полоса прокрутки и сетка иконок останутся с
	//   указателями на снесённые виджеты (та же грабля, что у сумки: `ScrollBar = FAsdaScrollBarNomal()`).
	GridScrollBar = FAsdaScrollBarNomal();
	GridScrollHost = nullptr;
	PrizeGrid = nullptr;
	GridSelectMark = nullptr;
	TipHoverCell = -1;
	// Окна пересоздаются ⇒ прежние узлы 68059/68056 мертвы: растяжку надо применить заново.
	SceneImage = nullptr;
	FullScreenApplied = FVector2D::ZeroVector;
	// Списки 68507/68508 держат указатели на виджеты 68500 — при пересоздании дерева они мертвы.
	if (VarJobCombo)   { VarJobCombo->Reset(); }
	if (VarLevelCombo) { VarLevelCombo->Reset(); }
	VarMouse.Reset();

	auto Open = [&](const FWin& W) -> UUserWidget*
	{
		const FString Path = FString(kUiRoot) + W.Wbp;
		UUserWidget* Widget = WM->OpenWindowByPath(FName(W.Id), Path, W.Z, ESlateVisibility::SelfHitTestInvisible);
		if (!Widget)
		{
			UE_LOG(LogAsdaGacha, Error, TEXT("[премиум-гача] окно %s не загрузилось (%s)"), W.Id, *Path);
		}
		else
		{
			WM->SetWindowVisible(FName(W.Id), false);   // все окна сцены стартуют скрытыми
		}
		return Widget;
	};

	RootWin    = Open(kRoot);
	ExitWin    = Open(kExit);
	SelectWin  = Open(kSelect);
	ClickWin   = Open(kClick);
	ViewWin    = Open(kView);
	MsgWin     = Open(kMsg);
	Select2Win = Open(kSel2);

	if (!RootWin) { return false; }
	SetupControls();
	// ★ Колесо мыши над интерфейсом раздаёт ОБЩИЙ реестр окон (тот же канал, что у сумки, книги
	//   навыков и чата) — регистрируемся один раз на имя окна витрины, чтобы порядок «сверху вниз
	//   по z» работал и для нас (`UAsdaWindowSubsystem::HandleMouseWheel`).
	if (!bWheelBound)
	{
		WM->RegisterWheelHandler(FName(kView.Id),
			FAsdaUiWheelHandler::CreateUObject(this, &UAsdaPremiumGachaSubsystem::OnGridWheel));
		bWheelBound = true;
	}
	bReady = true;
	return true;
}

// ─────────────────────── дамп цепочки виджетов под кистью 3D-сцены ───────────────────────
FString UAsdaPremiumGachaSubsystem::DumpSceneBrushChain() const
{
	// ★★★ Зачем это вообще есть: Slate считает `FinalColor = BaseColor * InVertex.Color`
	//   (`SlateElementPixelShader.usf:318`). Вершинный цвет — это тинт браша × `ColorAndOpacity`
	//   виджета × унаследованные от родителей цвет и `RenderOpacity`. Любое число < 1 на
	//   этой цепочке выцветит сцену, а оффлайн-зонды этого НЕ видят — они смотрят только RT.
	FString Out = TEXT("[премиум-гача] цепочка виджетов под 3D-сценой:\n");
	if (!SceneImage)
	{
		return Out + TEXT("  КИСТЬ НЕ ПОСТАВЛЕНА (SceneImage = null) — сцена закрыта или 68056 не найден\n");
	}

	auto C2S = [](const FLinearColor& C) { return FString::Printf(TEXT("(%.4f %.4f %.4f a=%.4f)"), C.R, C.G, C.B, C.A); };

	// Сама кисть: ресурс, тинт, UV-регион, режим рисования.
	{
		const FSlateBrush& B = SceneImage->GetBrush();
		const FBox2f Uv = B.GetUVRegion();
		Out += FString::Printf(
			TEXT("  браш: ресурс=%s draw_as=%d тинт=%s uv_valid=%d [%.3f %.3f]-[%.3f %.3f] размер=%.0fx%.0f\n"),
			*GetNameSafe(B.GetResourceObject()), (int32)B.DrawAs.GetValue(),
			*C2S(B.TintColor.GetSpecifiedColor()), Uv.bIsValid ? 1 : 0,
			Uv.Min.X, Uv.Min.Y, Uv.Max.X, Uv.Max.Y, B.ImageSize.X, B.ImageSize.Y);
		Out += FString::Printf(TEXT("  UImage 68056: ColorAndOpacity=%s RenderOpacity=%.4f видимость=%d\n"),
			*C2S(SceneImage->GetColorAndOpacity()), SceneImage->GetRenderOpacity(),
			(int32)SceneImage->GetVisibility());
	}

	// Родители до корня окна.
	FLinearColor Acc = SceneImage->GetColorAndOpacity() * SceneImage->GetBrush().TintColor.GetSpecifiedColor();
	Acc.A *= SceneImage->GetRenderOpacity();
	int32 Depth = 0;
	for (UWidget* W = SceneImage->GetParent(); W; W = W->GetParent())
	{
		Out += FString::Printf(TEXT("  родитель[%d] %-38s %-22s RenderOpacity=%.4f видимость=%d обрезка=%d\n"),
			Depth++, *W->GetName(), *W->GetClass()->GetName(),
			W->GetRenderOpacity(), (int32)W->GetVisibility(), (int32)W->GetClipping());
		Acc.A *= W->GetRenderOpacity();
	}
	if (RootWin)
	{
		Out += FString::Printf(TEXT("  окно 68000 %s: ColorAndOpacity=%s RenderOpacity=%.4f видимость=%d\n"),
			*RootWin->GetName(), *C2S(RootWin->GetColorAndOpacity()),
			RootWin->GetRenderOpacity(), (int32)RootWin->GetVisibility());
		Acc *= RootWin->GetColorAndOpacity();
		Acc.A *= RootWin->GetRenderOpacity();
	}
	Out += FString::Printf(TEXT("  ★ ИТОГОВЫЙ вершинный цвет = %s (обязан быть 1 1 1 a=1)\n"), *C2S(Acc));

	// Формат RT — от него зависит, декодирует ли сэмплер гамму сам (см. шапку сборщика материала).
	if (SceneActor)
	{
		if (UTextureRenderTarget2D* RT = SceneActor->GetRenderTarget())
		{
			Out += FString::Printf(
				TEXT("  RT: %dx%d формат=%d override=%d bForceLinearGamma=%d IsSRGB()=%d SRGB(свойство)=%d DisplayGamma=%.3f\n"),
				RT->SizeX, RT->SizeY, (int32)RT->RenderTargetFormat, (int32)RT->OverrideFormat,
				RT->bForceLinearGamma ? 1 : 0, RT->IsSRGB() ? 1 : 0, RT->SRGB ? 1 : 0, RT->GetDisplayGamma());
		}
		Out += FString::Printf(TEXT("  материал кисти: %s\n"), *GetNameSafe(SceneActor->GetSceneBrushMaterial()));
	}
	// Яркость самого Slate: если DisplayGamma не 2.2, компенсация гаммы в материале считает не то.
	Out += FString::Printf(TEXT("  GEngine->DisplayGamma = %.4f (компенсация в материале рассчитана на 2.2)\n"),
		GEngine ? GEngine->GetDisplayGamma() : -1.f);
	return Out;
}

// ─────────────────────── дамп пяти полос слотов (команда `asda.Gacha.BarsDump`) ───────────────────────
FString UAsdaPremiumGachaSubsystem::DumpSlotBars() const
{
	// ★★★ ПОЧЕМУ «ЭФФЕКТИВНАЯ» ВИДИМОСТЬ, А НЕ СВОЯ. У свёрнутого родителя ребёнок не рисуется и
	//   не ловит мышь, каким бы `Visible` он сам себя ни считал. Именно из-за этого проверка
	//   «виджет есть и он Visible» дала ложное «всё готово» при расширении до пяти автоматов.
	auto Chain = [](const UWidget* W) -> const UWidget*
	{
		for (const UWidget* Cur = W; Cur; Cur = Cur->GetParent())
		{
			const ESlateVisibility V = Cur->GetVisibility();
			if (V == ESlateVisibility::Collapsed || V == ESlateVisibility::Hidden) { return Cur; }
		}
		return nullptr;   // никто в цепочке не прячет — виден
	};
	auto Vis = [&Chain](const UWidget* W) -> FString
	{
		if (!W) { return TEXT("НЕТ ВИДЖЕТА"); }
		const UWidget* Bad = Chain(W);
		return Bad ? FString::Printf(TEXT("СКРЫТ (прячет %s)"), *Bad->GetName()) : FString(TEXT("виден"));
	};

	FString Out = TEXT("[премиум-гача] полосы слотов 68010+10i:\n");
	Out += FString::Printf(TEXT("  автоматов в 3D: %d, выбран слот %d, сдвиг полос по Y %.0f\n"),
		SceneActor ? SceneActor->GetMachineCount() : -1, SelectedSlot,
		SceneActor ? SceneActor->GetSlotScreenYOffset() : 0.f);
	for (int32 i = 0; i < SlotBars.Num(); ++i)
	{
		FVector2D P(-1.f, -1.f);
		const bool bProj = SceneActor && SceneActor->GetSlotScreenPos(i, P);
		FString Uv = TEXT("—");
		if (const UImage* D = SlotDigits.IsValidIndex(i) ? SlotDigits[i].Get() : nullptr)
		{
			const FBox2f R = D->GetBrush().GetUVRegion();
			Uv = FString::Printf(TEXT("%s uv[%.4f %.4f]-[%.4f %.4f]"),
				*GetNameSafe(D->GetBrush().GetResourceObject()), R.Min.X, R.Min.Y, R.Max.X, R.Max.Y);
		}
		Out += FString::Printf(
			TEXT("  полоса %d: группа=%s свёрнутых потомков=%d проекция=%s(%.0f,%.0f)\n")
			TEXT("            цифра: %-14s %s\n")
			TEXT("            «Основные предметы» (68011+10i): %-14s  «Играть» (68061+i): %s\n"),
			i + 1, *Vis(SlotBars[i]), CountCollapsed(SlotBars[i]),
			bProj ? TEXT("") : TEXT("ПРОМАХ "), P.X, P.Y,
			*Vis(SlotDigits.IsValidIndex(i) ? SlotDigits[i].Get() : nullptr), *Uv,
			*Vis(ViewButtons.IsValidIndex(i) ? ViewButtons[i].Get() : nullptr),
			*Vis(SpinButtons.IsValidIndex(i) ? SpinButtons[i].Get() : nullptr));
	}
	// Витрина каждого автомата: сколько позиций даст фильтр `ViewSlot == i` (кнопка 68011+10i
	//   именно это и открывает) и цена, которую пошлёт «Играть» (68061+i → пакет 6577 со слотом i).
	if (const UDataTable* T = PrizeTable.Get())
	{
		int32 PerSlot[5] = { 0, 0, 0, 0, 0 };
		for (const TPair<FName, uint8*>& It : T->GetRowMap())
		{
			const FAsdaPremiumGatyaRow* Row = reinterpret_cast<const FAsdaPremiumGatyaRow*>(It.Value);
			if (Row && Row->ItemCode > 0 && Row->ViewSlot >= 0 && Row->ViewSlot < 5) { ++PerSlot[Row->ViewSlot]; }
		}
		for (int32 i = 0; i < 5; ++i)
		{
			Out += FString::Printf(TEXT("  автомат %d: витрина %d позиций, цена %d\n"),
				i + 1, PerSlot[i], GetSlotPrice(i));
		}
	}
	return Out;
}

void UAsdaPremiumGachaSubsystem::ClickAllSlotButtons()
{
	const int32 N = SceneActor ? SceneActor->GetMachineCount() : 0;
	for (int32 i = 0; i < SlotBars.Num(); ++i)
	{
		// ── кнопка витрины 68011+10i: должна открыть сетку 68300 ИМЕННО своего автомата ──
		if (UButton* B = ViewButtons.IsValidIndex(i) ? ViewButtons[i].Get() : nullptr)
		{
			B->OnClicked.Broadcast();
			UE_LOG(LogAsdaGacha, Log,
				TEXT("[премиум-гача] проверка: «Основные предметы» полосы %d → слот %d, витрина %d позиций"),
				i + 1, SelectedSlot, Prizes.Num());
			CloseGridWindow();
		}
		else if (i < N)
		{
			UE_LOG(LogAsdaGacha, Error, TEXT("[премиум-гача] проверка: у полосы %d НЕТ кнопки витрины"), i + 1);
		}
		// ── кнопка «крутить» 68061+i: должна дойти до диалога оплаты 68100 со СВОЕЙ ценой ──
		if (UButton* B = SpinButtons.IsValidIndex(i) ? SpinButtons[i].Get() : nullptr)
		{
			B->OnClicked.Broadcast();
			UE_LOG(LogAsdaGacha, Log,
				TEXT("[премиум-гача] проверка: «Играть» полосы %d → слот %d, цена %d, фаза %d"),
				i + 1, SelectedSlot, GetSlotPrice(SelectedSlot), (int32)Phase);
			CancelRoll();
		}
		else if (i < N)
		{
			UE_LOG(LogAsdaGacha, Error, TEXT("[премиум-гача] проверка: у полосы %d НЕТ кнопки «крутить»"), i + 1);
		}
	}
	SelectSlot(0);   // вернуть выбор в исходное состояние, как после открытия сцены
}

// ★ 05.11.2026: настоящая кнопка полосы — тот же мультикаст `OnClicked`, что у мыши (описание — у объявления).
bool UAsdaPremiumGachaSubsystem::PressSlotButton(int32 Slot, bool bSpin)
{
	const TArray<TObjectPtr<UButton>>& Arr = bSpin ? SpinButtons : ViewButtons;
	UButton* B = Arr.IsValidIndex(Slot) ? Arr[Slot].Get() : nullptr;
	if (!B) { return false; }
	B->OnClicked.Broadcast();
	return true;
}

// ★ 07.11.2026: настоящая кнопка закрытия — 68076 (`bExitBar`) или крестик 68209, тот же мультикаст `OnClicked`, что у мыши.
bool UAsdaPremiumGachaSubsystem::PressCloseButton(bool bExitBar)
{
	UButton* B = bExitBar
		? (ExitWin ? Cast<UButton>(ExitWin->GetWidgetFromName(TEXT("Premium_PickBAR_daybtn_X_btn"))) : nullptr)
		: (ClickWin ? Cast<UButton>(ClickWin->GetWidgetFromName(TEXT("Premium_Pick_Click_X"))) : nullptr);
	if (!B) { return false; }
	B->OnClicked.Broadcast();
	return true;
}

// ★ 05.11.2026: кнопка 68108 «Играть» диалога 68100 — тот же мультикаст `OnClicked`, что у мыши.
bool UAsdaPremiumGachaSubsystem::PressConfirmPlay()
{
	UButton* Ok = SelectWin ? Cast<UButton>(SelectWin->GetWidgetFromName(TEXT("Premium_Pick_select_ok"))) : nullptr;
	if (!Ok) { return false; }
	Ok->OnClicked.Broadcast();
	return true;
}

// ★ 05.11.2026: видимость диалога оплаты 68100 по менеджеру окон (та же точка, что его показывает).
bool UAsdaPremiumGachaSubsystem::IsConfirmDialogShown() const
{
	const UAsdaWindowSubsystem* WM = GetGameInstance() ? GetGameInstance()->GetSubsystem<UAsdaWindowSubsystem>() : nullptr;
	return WM && WM->IsWindowVisible(FName(kSelect.Id));
}

// ★ 05.11.2026: видимость витрины 68300 по менеджеру окон.
bool UAsdaPremiumGachaSubsystem::IsPrizeGridShown() const
{
	const UAsdaWindowSubsystem* WM = GetGameInstance() ? GetGameInstance()->GetSubsystem<UAsdaWindowSubsystem>() : nullptr;
	return WM && WM->IsWindowVisible(FName(kView.Id));
}

// ★ 05.11.2026: клип автомата в 3D-сцене (порт `sub_B6ACB0`: поле +620 ребёнка сцены).
int32 UAsdaPremiumGachaSubsystem::GetMachineClip(int32 Slot) const
{
	return SceneActor ? SceneActor->GetPlayingClip(Slot) : -1;
}

namespace {
// ─────────────────────── консольные команды (только для зондов и ручной проверки) ───────────────────────
// ★ Паттерн взят из `AsdaBoosterPanelSubsystem.cpp:1440-1490` — так же зовутся окна бустера.
//   Нужны потому, что из UE-Python до подсистемы игрового экземпляра дотянуться НЕЧЕМ:
//   `unreal.SubsystemBlueprintLibrary` в обёртке отсутствует (проверено запуском).
	UAsdaPremiumGachaSubsystem* GachaSub(UWorld* W)
	{
		UGameInstance* GI = W ? W->GetGameInstance() : nullptr;
		return GI ? GI->GetSubsystem<UAsdaPremiumGachaSubsystem>() : nullptr;
	}
}

// ═══ Живой разбор окна 68500 (замечания владельца №2…№5) ═══
//   Печатается построчно: одна строка — один проверяемый факт, чтобы её можно было выдернуть из лога grep’ом.
FString UAsdaPremiumGachaSubsystem::DumpVariantWindow() const
{
	UAsdaWindowSubsystem* WM = GetGameInstance() ? GetGameInstance()->GetSubsystem<UAsdaWindowSubsystem>() : nullptr;
	TArray<FString> L;
	L.Add(TEXT("=== ОКНО 68500 `Premium_Pick_select_2` ==="));
	if (!WM) { L.Add(TEXT("реестра окон нет")); return FString::Join(L, LINE_TERMINATOR); }

	const FName Sel2Id(kSel2.Id);
	const FName PostId(TEXT("Primium_Item_post_WIN"));
	const int32 ZSel = WM->GetWindowZOrder(Sel2Id);
	const int32 ZPost = WM->GetWindowZOrder(PostId);
	L.Add(FString::Printf(TEXT("видно: %s | z 68500 = %d, z ящика 69000 = %d => %s"),
		WM->IsWindowVisible(Sel2Id) ? TEXT("да") : TEXT("нет"), ZSel, ZPost,
		(ZSel > ZPost) ? TEXT("ПОВЕРХ ящика") : TEXT("★ ПОД ящиком")));

	// Ручка перетаскивания — из `DA_JuiWindowMeta` (данные, а не список в коде).
	if (const UAsdaJuiWindowMetaAsset* Meta = WM->GetJuiMeta())
	{
		auto Handles = [Meta](FName Id) -> FString
		{
			const FAsdaJuiWindowMeta* M = Meta->FindWindow(Id);
			if (!M) { return TEXT("записи в мете НЕТ"); }
			if (M->DragHandles.Num() == 0) { return TEXT("★ ручек НЕТ — окно не таскается"); }
			FString R;
			for (const FAsdaJuiDragHandle& H : M->DragHandles)
			{
				R += FString::Printf(TEXT("%s -> %d%s "), *H.HandleWidget.ToString(), H.TargetIdNum,
					H.bOriginalBugFixed ? TEXT(" (восстановлена)") : TEXT(""));
			}
			return R;
		};
		L.Add(FString::Printf(TEXT("ручка 68500: %s"), *Handles(Sel2Id)));
		L.Add(FString::Printf(TEXT("ручка 69000: %s"), *Handles(PostId)));
	}

	auto Combo = [](const UAsdaJuiComboBox* C, const TCHAR* Name) -> FString
	{
		if (!C || !C->IsReady()) { return FString::Printf(TEXT("%s: НЕ собран"), Name); }
		FString R = FString::Printf(TEXT("%s: пунктов %d, выбран %d ["), Name, C->NumItems(), C->GetSelectedIndex());
		for (int32 i = 0; i < C->NumItems(); ++i) { R += (i ? TEXT(", ") : TEXT("")) + C->GetItemLabel(i); }
		return R + TEXT("]");
	};
	L.Add(Combo(VarJobCombo,   TEXT("68507 «Выбрана профессия»")));
	L.Add(Combo(VarLevelCombo, TEXT("68508 «Выбран уровень»")));
	L.Add(FString::Printf(TEXT("заголовок 68501: «%s»"),
		VarTitle ? *VarTitle->GetText().ToString() : TEXT("—")));
	L.Add(FString::Printf(TEXT("описание 68502: «%s»"),
		VarDesc ? *VarDesc->GetText().ToString().Left(60) : TEXT("—")));
	return FString::Join(L, LINE_TERMINATOR);
}

FString UAsdaPremiumGachaSubsystem::SimulateVariantCombo(bool bJob, bool bOpen, int32 PickIdx)
{
	UAsdaJuiComboBox* C = bJob ? VarJobCombo : VarLevelCombo;
	if (!C || !C->IsReady()) { return TEXT("\u0441\u043f\u0438\u0441\u043e\u043a \u041d\u0415 \u0441\u043e\u0431\u0440\u0430\u043d"); }
	C->SetOpen(bOpen);
	if (PickIdx >= 0) { C->SetSelectedIndex(PickIdx); }
	return FString::Printf(TEXT("%s: \u0440\u0430\u0441\u043a\u0440\u044b\u0442 %s, \u043f\u0443\u043d\u043a\u0442\u043e\u0432 %d, \u0432\u044b\u0431\u0440\u0430\u043d %d \u00ab%s\u00bb"),
		bJob ? TEXT("68507 \u043f\u0440\u043e\u0444\u0435\u0441\u0441\u0438\u044f") : TEXT("68508 \u0443\u0440\u043e\u0432\u0435\u043d\u044c"),
		C->IsOpen() ? TEXT("\u0434\u0430") : TEXT("\u043d\u0435\u0442"), C->NumItems(), C->GetSelectedIndex(),
		*C->GetItemLabel(C->GetSelectedIndex()));
}

#if ASDA_DEV_CONSOLE   // ★ античит А2: дев-команда, в Shipping не регистрируется
static FAutoConsoleCommandWithWorldAndArgs GGachaVarCombo(
	TEXT("asda.Gacha.VarCombo"),
	TEXT("asda.Gacha.VarCombo <job|level> <0|1 \u0440\u0430\u0441\u043a\u0440\u044b\u0442\u044c> [\u0438\u043d\u0434\u0435\u043a\u0441 \u043f\u0443\u043d\u043a\u0442\u0430] \u2014 \u0436\u0438\u0432\u0430\u044f \u043f\u0440\u043e\u0432\u0435\u0440\u043a\u0430 "
	     "\u0432\u044b\u043f\u0430\u0434\u0430\u044e\u0449\u0438\u0445 \u0441\u043f\u0438\u0441\u043a\u043e\u0432 \u043e\u043a\u043d\u0430 68500"),
	FConsoleCommandWithWorldAndArgsDelegate::CreateStatic([](const TArray<FString>& A, UWorld* W)
	{
		UAsdaPremiumGachaSubsystem* S = GachaSub(W);
		if (!S || A.Num() < 2) { return; }
		const bool bJob = A[0].Equals(TEXT("job"), ESearchCase::IgnoreCase);
		const bool bOpen = FCString::Atoi(*A[1]) != 0;
		const int32 Pick = A.Num() > 2 ? FCString::Atoi(*A[2]) : -1;
		UE_LOG(LogAsdaGacha, Log, TEXT("%s"), *S->SimulateVariantCombo(bJob, bOpen, Pick));
	}));
#endif // ASDA_DEV_CONSOLE

#if ASDA_DEV_CONSOLE   // ★ античит А2: дев-команда, в Shipping не регистрируется
static FAutoConsoleCommandWithWorld GGachaVarDump(
	TEXT("asda.Gacha.VarDump"),
	TEXT("asda.Gacha.VarDump — разбор окна 68500: видимость, z над ящиком, ручка перетаскивания, оба списка"),
	FConsoleCommandWithWorldDelegate::CreateStatic([](UWorld* W)
	{
		if (UAsdaPremiumGachaSubsystem* S = GachaSub(W))
		{
			UE_LOG(LogAsdaGacha, Log, TEXT("%s"), *S->DumpVariantWindow());
		}
	}));
#endif // ASDA_DEV_CONSOLE

#if ASDA_DEV_CONSOLE   // ★ античит А2: дев-команда, в Shipping не регистрируется
// ★★★ ЖИВАЯ ПРОВЕРКА БАГА ВЛАДЕЛЬЦА «выбор не несёт смысла» — ДВЕ команды.
//   Первая нажимает ОК окна 68500 ТЕМ ЖЕ КОДОМ, что и клик (`ConfirmVariant` — порт связки
//   `GachaScene_OnControlMessage` 68511 → `sowelWndConfirmAction` → 6581).
static FAutoConsoleCommandWithWorld GGachaVarOk(
	TEXT("asda.Gacha.VarOk"),
	TEXT("asda.Gacha.VarOk — нажать кнопку ОК (68511) окна выбора варианта тем же кодом, что клик мышью"),
	FConsoleCommandWithWorldDelegate::CreateStatic([](UWorld* W)
	{
		if (UAsdaPremiumGachaSubsystem* S = GachaSub(W)) { S->ConfirmVariant(); }
	}));
#endif // ASDA_DEV_CONSOLE

#if ASDA_DEV_CONSOLE   // ★ античит А2: дев-команда, в Shipping не регистрируется
static FAutoConsoleCommandWithWorld GGachaVarClose(
	TEXT("asda.Gacha.VarClose"),
	TEXT("asda.Gacha.VarClose — закрыть окно 68500 БЕЗ выбора тем же кодом, что крестик 68519. "
	     "Проверка ветки «отмена»: 6581 не уходит, предмет остаётся вариантом по умолчанию."),
	FConsoleCommandWithWorldDelegate::CreateStatic([](UWorld* W)
	{
		if (UAsdaPremiumGachaSubsystem* S = GachaSub(W)) { S->AbortVariantChoice(); }
	}));
#endif // ASDA_DEV_CONSOLE

#if ASDA_DEV_CONSOLE   // ★ античит А2: дев-команда, в Shipping не регистрируется
//   Вторая эмулирует ОТВЕТ СЕРВЕРА 6582 — тем же путём, что сетевой колбэк (`OnPrizeApplied` →
//   отложенное применение в `Tick`). Именно здесь и жил баг: `Tick` выходил по гейту `bSceneOpen`
//   раньше применения, и на маршруте «из элитного ящика» (сцена гачи закрыта) обмен пропадал.
//   Тело собирается НАСТОЯЩЕЕ, 61 байт: @0 статус, @1 контейнер очищаемой ячейки, @2 i16 её слот,
//   @4 запись 57 б (код/контейнер/слот/количество), @60 хвост.
static FAutoConsoleCommandWithWorldAndArgs GGachaSimPrizeApply(
	TEXT("asda.Gacha.SimPrizeApply"),
	TEXT("asda.Gacha.SimPrizeApply <код варианта> [контейнер=1] [слот=0] [кол-во=1] — эмуляция ответа 6582 "
	     "(проверка, что обмен применяется и при ЗАКРЫТОЙ сцене гачи — маршрут из элитного ящика)"),
	FConsoleCommandWithWorldAndArgsDelegate::CreateStatic([](const TArray<FString>& A, UWorld* W)
	{
		// ★ С-Р1 (26.09.2026), офлайн-гейт: поддельная запись приза в сумке — дальнейшие действия с ней ушли бы на сервер.
		if (FAsdaOutgoingGate::RefuseDebugWhenOnline(W, TEXT("asda.Gacha.SimPrizeApply"))) { return; }
		UAsdaPremiumGachaSubsystem* S = GachaSub(W);
		if (!S || A.Num() < 1)
		{
			UE_LOG(LogAsdaGacha, Warning,
				TEXT("использование: asda.Gacha.SimPrizeApply <код варианта> [контейнер] [слот] [кол-во]"));
			return;
		}
		const int32 Code = FCString::Atoi(*A[0]);
		const int32 Cont = (A.Num() > 1) ? FCString::Atoi(*A[1]) : 1;
		const int32 Slot = (A.Num() > 2) ? FCString::Atoi(*A[2]) : 0;
		const int32 Amt  = (A.Num() > 3) ? FCString::Atoi(*A[3]) : 1;
		S->SimulatePrizeApply(Code, Cont, Slot, Amt);
	}));
#endif // ASDA_DEV_CONSOLE

#if ASDA_DEV_CONSOLE   // ★ античит А2: дев-команда, в Shipping не регистрируется
static FAutoConsoleCommandWithWorld GGachaOpen(
	TEXT("asda.Gacha.Open"),
	TEXT("asda.Gacha.Open — открыть сцену премиум-гачи тем же путём, что кнопка 73410"),
	FConsoleCommandWithWorldDelegate::CreateStatic([](UWorld* W)
	{
		UAsdaPremiumGachaSubsystem* S = GachaSub(W);
		if (!S) { UE_LOG(LogAsdaGacha, Warning, TEXT("[премиум-гача] подсистемы нет в этом мире")); return; }
		UE_LOG(LogAsdaGacha, Log, TEXT("[премиум-гача] asda.Gacha.Open: %s"),
			S->OpenScene() ? TEXT("открыта") : TEXT("НЕ открыта"));
	}));
#endif // ASDA_DEV_CONSOLE

#if ASDA_DEV_CONSOLE   // ★ античит А2: дев-команда, в Shipping не регистрируется
static FAutoConsoleCommandWithWorld GGachaClose(
	TEXT("asda.Gacha.Close"),
	TEXT("asda.Gacha.Close — закрыть сцену премиум-гачи"),
	FConsoleCommandWithWorldDelegate::CreateStatic([](UWorld* W)
	{
		if (UAsdaPremiumGachaSubsystem* S = GachaSub(W)) { S->CloseScene(); }
	}));
#endif // ASDA_DEV_CONSOLE

#if ASDA_DEV_CONSOLE   // ★ античит А2: дев-команда, в Shipping не регистрируется
static FAutoConsoleCommandWithWorld GGachaBarsDump(
	TEXT("asda.Gacha.BarsDump"),
	TEXT("asda.Gacha.BarsDump — пять полос слотов: эффективная видимость, UV цифры, обе кнопки, витрина и цена"),
	FConsoleCommandWithWorldDelegate::CreateStatic([](UWorld* W)
	{
		if (UAsdaPremiumGachaSubsystem* S = GachaSub(W))
		{
			UE_LOG(LogAsdaGacha, Log, TEXT("%s"), *S->DumpSlotBars());
		}
	}));
#endif // ASDA_DEV_CONSOLE

#if ASDA_DEV_CONSOLE   // ★ античит А2: дев-команда, в Shipping не регистрируется
// ★★★ ПРОВЕРКА ОБЕИХ КНОПОК У ВСЕХ ПЯТИ ПОЛОС — НАЖАТИЕМ, А НЕ «НА ГЛАЗ».
//   Дамп выше доказывает, что кнопка ВИДНА и достижима мышью; эта команда доказывает, что за ней
//   стоит РАБОЧИЙ обработчик именно своего слота: у каждой полосы «дёргается» `OnClicked` — тот
//   самый мультикаст, который вызывает мышь, — и в лог печатается результат (число позиций витрины
//   после кнопки 68011+10i и номер слота с ценой после кнопки «крутить» 68061+i).
//   Сеть не трогается: «крутить» доводится только до диалога 68100, который тут же отменяется.
static FAutoConsoleCommandWithWorld GGachaBarsClick(
	TEXT("asda.Gacha.BarsClick"),
	TEXT("asda.Gacha.BarsClick — нажать у всех пяти полос обе кнопки (витрина и «крутить») и напечатать результат"),
	FConsoleCommandWithWorldDelegate::CreateStatic([](UWorld* W)
	{
		UAsdaPremiumGachaSubsystem* S = GachaSub(W);
		if (!S) { UE_LOG(LogAsdaGacha, Warning, TEXT("[премиум-гача] подсистемы нет в этом мире")); return; }
		S->ClickAllSlotButtons();
	}));
#endif // ASDA_DEV_CONSOLE

#if ASDA_DEV_CONSOLE   // ★ античит А2: дев-команда, в Shipping не регистрируется
// ★★★ ДИАГНОСТИКА 24.09.2026 — САМ БУФЕР СЦЕНЫ ГАЧИ В PNG И ДОЛЯ НЕПУСТЫХ ПИКСЕЛЕЙ.
//   ЗАЧЕМ. Проверка «видна ли 3D-сцена» по логу НЕ ОТВЕЧАЕТ на вопрос: строки «автоматов 5 из 5»,
//   «RT 1600x900», «кисть MID_M_AsdaGachaScene3D_0» доказывают лишь, что буфер СОЗДАН и кисть
//   ПОДСТАВЛЕНА, а не что кадр ДОШЁЛ ДО ЭКРАНА (закон `false-negatives-method`). Команда режет
//   цепочку «капчер → RT → кисть → контрол 68056» ровно пополам, как это сделала для окна
//   питомца `asda.Pet.PreviewDumpRt`: если в PNG буфера автоматы ЕСТЬ, а на кадре экрана их нет —
//   виноват участок «кисть/контрол»; если и в буфере пусто — капчер или состав сцены.
//   ⚠ ТОЛЬКО ЧТЕНИЕ: ни один ассет не трогается, RT выгружается в <Saved>/Screenshots.
static FAutoConsoleCommandWithWorldAndArgs GGachaDumpRt(
	TEXT("asda.Gacha.DumpRt"),
	TEXT("asda.Gacha.DumpRt <имя> — выгрузить САМ буфер сцены гачи в <Saved>/Screenshots/<имя>.png + доля непустых пикселей."),
	FConsoleCommandWithWorldAndArgsDelegate::CreateStatic([](const TArray<FString>& Args, UWorld* W)
	{
		// Актор берём ОБХОДОМ МИРА, а не полем подсистемы: поле приватное, а заводить ради
		// одной диагностики публичный доступ в шапку — лишняя правка чужого интерфейса.
		AAsdaGachaSceneActor* A = nullptr;
		for (TActorIterator<AAsdaGachaSceneActor> It(W); It; ++It) { A = *It; break; }
		if (!A) { UE_LOG(LogAsdaGacha, Warning, TEXT("[премиум-гача] актора сцены нет — сцена не создана")); return; }
		// Сперва ПИКСЕЛЬНАЯ МЕТРИКА (её видно прямо в логе), затем файл — чтобы вердикт был и без PNG.
		UE_LOG(LogAsdaGacha, Display, TEXT("[премиум-гача] охват кадра в буфере: %s"), *A->DescribeFrameCoverage());
		UTextureRenderTarget2D* Rt = A->GetRenderTarget();
		if (!Rt) { UE_LOG(LogAsdaGacha, Warning, TEXT("[премиум-гача] буфера сцены нет")); return; }
		const FString Name = Args.Num() > 0 ? Args[0] : TEXT("gacha_rt");
		const FString Dir = FPaths::ProjectSavedDir() / TEXT("Screenshots");
		UKismetRenderingLibrary::ExportRenderTarget(W, Rt, Dir, Name + TEXT(".png"));
		UE_LOG(LogAsdaGacha, Display, TEXT("[премиум-гача] буфер сцены выгружен: %s/%s.png"), *Dir, *Name);
	}));
#endif // ASDA_DEV_CONSOLE

#if ASDA_DEV_CONSOLE   // ★ античит А2: дев-команда, в Shipping не регистрируется
static FAutoConsoleCommandWithWorld GGachaBrushDump(
	TEXT("asda.Gacha.BrushDump"),
	TEXT("asda.Gacha.BrushDump — тинт/прозрачность контейнера 68056 и всех его родителей + формат RT"),
	FConsoleCommandWithWorldDelegate::CreateStatic([](UWorld* W)
	{
		if (UAsdaPremiumGachaSubsystem* S = GachaSub(W))
		{
			UE_LOG(LogAsdaGacha, Log, TEXT("%s"), *S->DumpSceneBrushChain());
		}
	}));
#endif // ASDA_DEV_CONSOLE
