// Copyright (c) 2026 @sitxovski (Discord)
// ★★★ САМОТЕСТ «СОСТАВ БУСТЕРОВ/КОРОБОК И РАЗБОРКА» (05.11.2026, баг игроков, волна booster-loot): `asda.SelfTest.BoosterLoot [quit]`.
//   [ИСПРАВЛЕНИЕ ДЕФЕКТА ОРИГИНАЛА, данные] Разбор и план — `Reports/booster_bundles/2026-11-05/` (скрипт, план, список).
//   Баг 1: «Booster-Demon Ballista» 75834 показывает и выдаёт наряды. Клиент == сервер байт в байт; строка гачи 3946 составлена под
//     линию Interlude (там призы — баллисты), а шаблоны предметов клиента и сервера под теми же кодами — другие вещи. Решение
//     владельца 26.10 (вариант Б) — имя коробки по фактическому составу; здесь — остаток диапазона 755xx–76xxx.
//   Баг 2: окно разборки V (32000) предлагало 2652 предмета с кодами > 75797 (костюмы оленя/снеговика/индейца/пирата…), а серверная
//     `DisassembleTable.txt` кончается на 75797 → ответ 6503 код 0, эталон и порт его молча глотают. Авторитет — сервер (25.09):
//     флаг «разбирается» снят ведомостью `_value_overlay/decompositiontable.csv`.
//   Путь игрока (закон `ui-check-game-path-geometry`): предметы кладутся в ячейки сумки боевой точкой `ApplyExternalRecord`,
//   двойной щелчок и щелчки — настоящими событиями мыши Slate по живой геометрии, офлайн (-AsdaNoNet). Разделы:
//     D. данные: у 75825–75851 нет «демонского» имени; флаг разборки 1 только у кодов серверной таблицы (20551…75797);
//     B. бустер 75834 в сумке → двойной щелчок → окно барабана 10050 → кнопка «Вид» → окно 15600: имя = имени предмета на
//        языке сессии и не прежнее, ячейки = строке `booster_list` (особая 0 = 75796);
//     P. коробка 75844 в сумке → двойной щелчок → окно состава 26500 (вскрытие): имя на экране = новому имени, в составе 76338;
//     C. камера (баг «во время открытия бустера ПКМ не вращает камеру», 05.11): при открытом барабане 10050 (CB) и окне состава
//        26500 (CP) ПКМ-драг в точке мира вне окна поворачивает камеру, зона окна — рамка (< 50 % экрана); пара — ключи
//        `asda.Booster.ZoneFrame 0` / `asda.Package.ZoneFrame 0`: зона во весь экран, точка мира «над окном», поворота нет;
//     X. окно разборки: «Шляпа элитного оленя» 76264 (код > 75797) в списке НЕТ, «Шлем ловкача» 75676 (приз 75834) — ЕСТЬ.
//   Кадры — `Reports/booster_bundles/2026-11-05/frames/<язык>_<раздел>.png`. Сумка, уровень и окна возвращаются в конце.
//   Итог: «СОСТАВ-БУСТЕРОВ ИТОГ: ok=N fail=M». Карта мира (`--map world`), язык — `--lang ru|en`.
#include "CoreMinimal.h"
#include "AsdaDevConsole.h"
#include "Test/AsdaSelfTestCtx.h"   // ★ R5-c (12.11.2026): общая точка самотестов вместо своих копий помощников
#include "Test/AsdaSelfTestInput.h"   // ★ R5-c (12.11.2026): общая точка самотестов вместо своих копий помощников

#if ASDA_DEV_CONSOLE && !UE_BUILD_SHIPPING

#include "Test/AsdaSelfTestNet.h"   // ★ R1: тонкий доступ самотестов к сети (вместо AsdaNetSubsystem.h)
#include "AsdaCVarSave.h"
#include "GameFramework/InputSettings.h"
#include "World/AsdaPlayerController.h"
#include "Test/AsdaSelfTestKit.h"
#include "Items/AsdaItemTableSubsystem.h"
#include "Items/AsdaItemDbRows.h"
#include "Text/AsdaLang.h"
#include "UI/AsdaBoosterPanelSubsystem.h"
#include "UI/AsdaDisassemblySubsystem.h"
#include "UI/AsdaInventorySubsystem.h"
#include "UI/AsdaItemRecordWriter.h"
#include "UI/AsdaItemTooltipSubsystem.h"
#include "UI/AsdaPackagePanelSubsystem.h"
#include "UI/AsdaShopDblClick.h"
#include "UI/AsdaUiHelpers.h"
#include "UI/AsdaWindowSubsystem.h"
#include "Blueprint/UserWidget.h"
#include "Blueprint/WidgetTree.h"
#include "Components/Image.h"
#include "Components/TextBlock.h"
#include "Components/Widget.h"
#include "Engine/DataTable.h"
#include "Engine/Engine.h"
#include "Engine/GameViewportClient.h"
#include "Engine/GameInstance.h"
#include "Engine/World.h"
#include "Framework/Application/SlateApplication.h"
#include "Misc/Paths.h"
#include "TimerManager.h"
#include "UnrealClient.h"

DEFINE_LOG_CATEGORY_STATIC(LogAsdaBoosterLootTest, Log, All);

namespace AsdaBoosterLootTest
{
	static const TCHAR* kCmd = TEXT("asda.SelfTest.BoosterLoot");   // имя команды
	static const TCHAR* kPfx = TEXT("СОСТАВ-БУСТЕРОВ");             // префикс строк и итога
	static const FName kPrevId(TEXT("BOOSTER_PREV_WIN"));            // окно «Вид» 15600
	static const FName kPkgId(TEXT("Package_Contents_Win"));         // окно состава набора 26500
	static const FName kDrumId(TEXT("BOOSTING_WIN"));                // окно барабана 10050

	static constexpr int32 kBooster   = 75834;   // «Booster-Demon Ballista» из отчёта игрока (строка гачи 3946)
	static constexpr int32 kBoostSp0  = 75796;   // особая позиция 0 этой строки (Gold Magical Plus Package)
	static constexpr int32 kBox       = 75844;   // коробка «Booster-Demon Bow» (пакет 4546) — теперь по составу
	static constexpr int32 kBoxItem   = 76338;   // «Яйцо снежного льва» в её составе
	static constexpr int32 kCostume   = 76264;   // «Шляпа элитного оленя» — код вне серверной таблицы разборки
	static constexpr int32 kGearOk    = 75676;   // «Шлем ловкача» — приз 75834, в серверной таблице разборки есть
	static constexpr int32 kSrvFirst  = 20551;   // первый код серверной DisassembleTable.txt
	static constexpr int32 kSrvLast   = 75797;   // последний (55 247 строк)
	static constexpr int32 kCellUse   = 0;       // ячейка сумки для бустера/коробки
	static constexpr int32 kCellCos   = 5;       // ячейка сумки для костюма
	static constexpr int32 kCellGear  = 6;       // ячейка сумки для шлема

	// Прежние имена (до правки) — на экране их быть не должно.
	static const TCHAR* kOldNames[] = { TEXT("Booster-Demon"), TEXT("демона") };

	// Контекст прогона (живёт между таймерами).
	struct FCtx
	{
		int32 Ok = 0, Fail = 0;                    // пройдено / провалено
		TWeakObjectPtr<UWorld> W;                  // мир прогона
		bool bQuit = false;                        // выйти после итога
		FString FrameDir;                          // папка кадров
		TMap<int32, TArray<uint8>> Saved;          // ячейка сумки → сырая запись до прогона (пусто — была пуста)
		TSharedPtr<FAsdaNetSelfScope> Self;        // свой персонаж (уровень) — вернуть в конце

		void Expect(bool b, const FString& What)
		{
			if (b) { ++Ok;   UE_LOG(LogAsdaBoosterLootTest, Log,   TEXT("%s ok   %s"), kPfx, *What); }
			else   { ++Fail; UE_LOG(LogAsdaBoosterLootTest, Error, TEXT("%s FAIL %s"), kPfx, *What); }
		}
		UGameInstance* GI() const { return W.IsValid() ? W->GetGameInstance() : nullptr; }
		template <class T> T* Sub() const { UGameInstance* G = GI(); return G ? G->GetSubsystem<T>() : nullptr; }
	};
	using FCtxRef = TSharedRef<FCtx>;

	// Шаг через Sec секунд от старта.
	using AsdaSelfTestCtx::At;   // ★ R5-c (12.11.2026): общая точка (`Test/AsdaSelfTestCtx.h`) — копия была дословной.
	// Центр живой геометрии узла (false — не отрисован / скрыт).
	using AsdaSelfTestInput::Center;   // ★ R5-c (12.11.2026): общая точка (`Test/AsdaSelfTestInput.h`) — копия отличалась только тем, что `Mn`/`Mx` объявлялись без начального значения (на результат не влияет).
	// Курсор в точку + событие движения.
	using AsdaSelfTestInput::Hover;   // ★ R5-c (12.11.2026): общая точка (`Test/AsdaSelfTestInput.h`) — копия была дословной.
	// Настоящий щелчок Slate.
	static void Click(const FCtxRef& C, const FVector2D& P) { if (UAsdaWindowSubsystem* WM = C->Sub<UAsdaWindowSubsystem>()) { Hover(P); WM->SimulateClickAt(P, /*HoldFrames*/ 2); } }
	// Узел окна по началу имени.
	static UWidget* Node(const FCtxRef& C, FName WinId, const TCHAR* Pfx)
	{
		UAsdaWindowSubsystem* WM = C->Sub<UAsdaWindowSubsystem>();
		UUserWidget* Win = WM ? WM->FindWindow(WinId) : nullptr;
		return Win ? AsdaUi::FindNodeByPrefix(Win, Pfx) : nullptr;
	}
	// Есть ли видимый текстовый узел окна с ровно этим текстом.
	static bool WinShowsText(const FCtxRef& C, FName WinId, const FString& Text)
	{
		UAsdaWindowSubsystem* WM = C->Sub<UAsdaWindowSubsystem>();
		UUserWidget* Win = WM ? WM->FindWindow(WinId) : nullptr;
		if (!Win || !Win->WidgetTree || Text.IsEmpty()) { return false; }
		TArray<UWidget*> All;
		Win->WidgetTree->GetAllWidgets(All);
		for (UWidget* X : All)
		{
			const UTextBlock* T = Cast<UTextBlock>(X);
			if (T && AsdaUi::IsShown(T) && T->GetText().ToString().TrimStartAndEnd() == Text) { return true; }
		}
		return false;
	}
	// Есть ли в строке прежнее «демонское» имя.
	static bool HasOldName(const FString& S)
	{
		for (const TCHAR* O : kOldNames) { if (S.Contains(O)) { return true; } }
		return false;
	}
	// Кадр окна.
	static void Shot(const FCtxRef& C, const TCHAR* Tag)
	{
		const FString File = C->FrameDir / FString::Printf(TEXT("%s_%s.png"), FAsdaLang::Code(FAsdaLang::Current()), Tag);
		UE_LOG(LogAsdaBoosterLootTest, Log, TEXT("%s: КАДР → %s"), kPfx, *File);
		FScreenshotRequest::RequestScreenshot(File, /*bShowUI*/ true, /*bAddFilenameSuffix*/ false);
	}
	// Положить предмет в ячейку сумки (боевая точка приёма записи).
	static void Put(const FCtxRef& C, int32 Cell, int32 Code)
	{
		UAsdaInventorySubsystem* I = C->Sub<UAsdaInventorySubsystem>();
		if (!I) { return; }
		if (!C->Saved.Contains(Cell))
		{
			const FAsdaItemInstance* It = I->GetCell(1, Cell);
			C->Saved.Add(Cell, (It && !It->IsEmpty()) ? It->Raw : TArray<uint8>());
		}
		I->ApplyExternalRecord(FAsdaItemRecordWriter::Cell(Code, 1, Cell, 1), UAsdaInventorySubsystem::EExternalRecordMode::AsIs);
	}
	// Двойной щелчок по иконке ячейки сумки (вкладка «Снаряжение»).
	static bool DblClickBag(const FCtxRef& C, int32 Cell)
	{
		UAsdaInventorySubsystem* I = C->Sub<UAsdaInventorySubsystem>();
		FVector2D P;
		if (!I || !Center(I->DebugGetGridIcon(Cell), P)) { return false; }
		Hover(P);
		AsdaSimDoubleClickAt(C->Sub<UAsdaWindowSubsystem>(), P, false);
		return true;
	}

	// Абсолютный прямоугольник вьюпорта игры (Slate).
	static bool ViewportAbs(FVector2D& Mn, FVector2D& Mx)
	{
		if (!GEngine || !GEngine->GameViewport || !GEngine->GameViewport->GetGameViewportWidget().IsValid()) { return false; }
		const FGeometry G = GEngine->GameViewport->GetGameViewportWidget()->GetCachedGeometry();
		Mn = G.LocalToAbsolute(FVector2D::ZeroVector);
		Mx = G.LocalToAbsolute(G.GetLocalSize());
		return (Mx - Mn).X > 10.0;
	}
	// Точка мира для ПКМ-драга: левый край вьюпорта по середине высоты (окна сумки/барабана/состава — правее).
	static FVector2D WorldPoint()
	{
		FVector2D Mn, Mx;
		if (!ViewportAbs(Mn, Mx)) { return FVector2D::ZeroVector; }
		return FVector2D(Mn.X + (Mx.X - Mn.X) * 0.08f, Mn.Y + (Mx.Y - Mn.Y) * 0.55f);
	}
	// Доля площади вьюпорта, которую занимает зона окна `Zone` (−1 — зоны нет / не отрисована).
	static float ZoneShare(const FCtxRef& C, FName Zone)
	{
		const UAsdaWindowSubsystem* WM = C->Sub<UAsdaWindowSubsystem>();
		FVector2D Mn, Mx, VMn, VMx;
		if (!WM || !WM->GetUiZoneRect(Zone, Mn, Mx) || !ViewportAbs(VMn, VMx)) { return -1.f; }
		const FVector2D V = VMx - VMn, Z = Mx - Mn;
		return (V.X * V.Y > 0.0) ? float((Z.X * Z.Y) / (V.X * V.Y)) : -1.f;
	}

	static constexpr int32 kOrbitSteps = 8;      // шагов ПКМ-драга
	static constexpr float kOrbitDx    = 20.f;   // пикселей вправо за шаг

	// ПКМ-драг путём игрока: курсор в точку мира P, ПКМ вниз (событие Slate), 8 движений по 20 px (по шагу в кадр), ПКМ вверх.
	//   Сглаживание мыши на время драга выключено (как в `asda.UI.SelfTestModalLock`). OnDone(поворот камеры, °).
	static void Orbit(const FCtxRef& C, const FVector2D& P, TFunction<void(float)> OnDone)
	{
		AAsdaPlayerController* PC = C->W.IsValid() ? Cast<AAsdaPlayerController>(C->W->GetFirstPlayerController()) : nullptr;
		if (!PC || !FSlateApplication::IsInitialized()) { OnDone(NAN); return; }
		const bool bSmoothWas = GetDefault<UInputSettings>()->bEnableMouseSmoothing;   // настройка до драга
		GetMutableDefault<UInputSettings>()->bEnableMouseSmoothing = false;
		const float Yaw0 = PC->GetCameraYawUE();                                       // угол до драга
		FSlateApplication& App = FSlateApplication::Get();
		Hover(P);
		TSet<FKey> Btn;   // зажатые кнопки события
		Btn.Add(EKeys::RightMouseButton);
		App.ProcessMouseButtonDownEvent(nullptr, FPointerEvent(0, P, P, Btn, EKeys::RightMouseButton, 0.f, FModifierKeysState()));
		for (int32 k = 0; k <= kOrbitSteps; ++k)
		{
			At(C, 0.05f * (k + 1), [C, P, k, Yaw0, bSmoothWas, OnDone]()
			{
				FSlateApplication& A = FSlateApplication::Get();
				if (k < kOrbitSteps)
				{
					const FVector2D Prev = P + FVector2D(kOrbitDx * k, 0.f), Cur = Prev + FVector2D(kOrbitDx, 0.f);
					TSet<FKey> B; B.Add(EKeys::RightMouseButton);
					A.SetCursorPos(Cur);
					A.ProcessMouseMoveEvent(FPointerEvent(0, Cur, Prev, B, EKeys::Invalid, 0.f, FModifierKeysState()));
					return;
				}
				const FVector2D End = P + FVector2D(kOrbitDx * kOrbitSteps, 0.f);
				AAsdaPlayerController* Pc = C->W.IsValid() ? Cast<AAsdaPlayerController>(C->W->GetFirstPlayerController()) : nullptr;
				const float D = Pc ? FMath::FindDeltaAngleDegrees(Yaw0, Pc->GetCameraYawUE()) : NAN;
				A.ProcessMouseButtonUpEvent(FPointerEvent(0, End, End, TSet<FKey>(), EKeys::RightMouseButton, 0.f, FModifierKeysState()));
				GetMutableDefault<UInputSettings>()->bEnableMouseSmoothing = bSmoothWas;
				OnDone(D);
			});
		}
	}
	// Раздел камеры для открытого окна (зона `Zone`, ключ исправления `Key`): пара «ключ 1 — камера вращается, точка мира вне
	//   зоны» / «ключ 0 — зона во весь экран, точка мира «над окном», поворота нет». Затем ключ 1 и `Then`.
	static void CameraPair(const FCtxRef& C, const TCHAR* Tag, FName Zone, const TCHAR* Key, TFunction<void()> Then)
	{
		const FVector2D P = WorldPoint();
		const UAsdaWindowSubsystem* WM = C->Sub<UAsdaWindowSubsystem>();
		FName Hit;
		const bool bOver1 = WM && WM->IsScreenPointOverAnyUiWindow(P, Hit);
		const float Share1 = ZoneShare(C, Zone);
		C->Expect(Share1 > 0.f && Share1 < 0.5f && !bOver1, FString::Printf(
			TEXT("%s1 ключ %s 1: зона %s = %.0f%% экрана (рамка окна), точка мира (%.0f,%.0f) не над окном (попадание «%s»)"),
			Tag, Key, *Zone.ToString(), Share1 * 100.f, P.X, P.Y, bOver1 ? *Hit.ToString() : TEXT("-")));
		Orbit(C, P, [C, Tag, Zone, Key, P, Then](float Yaw1)
		{
			C->Expect(FMath::IsFinite(Yaw1) && FMath::Abs(Yaw1) > 1.f,
				FString::Printf(TEXT("%s2 ключ 1: ПКМ-драг при открытом окне поворачивает камеру на %.2f°"), Tag, Yaw1));
			AsdaCVarSave::SetForTest(Key, TEXT("0"));
			const UAsdaWindowSubsystem* WM2 = C->Sub<UAsdaWindowSubsystem>();
			FName Hit0;
			const bool bOver0 = WM2 && WM2->IsScreenPointOverAnyUiWindow(P, Hit0);
			const float Share0 = ZoneShare(C, Zone);
			C->Expect(bOver0 && Hit0 == Zone && Share0 > 0.9f, FString::Printf(
				TEXT("%s3 ключ 0 (прежний порт): зона %s = %.0f%% экрана, точка мира «над окном» (%s) — баг воспроизведён"),
				Tag, *Zone.ToString(), Share0 * 100.f, *Hit0.ToString()));
			Orbit(C, P, [C, Tag, Key, Then](float Yaw0)
			{
				C->Expect(FMath::IsFinite(Yaw0) && FMath::Abs(Yaw0) < 0.01f,
					FString::Printf(TEXT("%s4 ключ 0: камера НЕ поворачивается (%.3f°) — как в отчёте игрока"), Tag, Yaw0));
				AsdaCVarSave::SetForTest(Key, TEXT("1"));
				Then();
			});
		});
	}

	static void Finish(const FCtxRef& C)
	{
		UWorld* W = C->W.Get();
		if (UAsdaDisassemblySubsystem* D = C->Sub<UAsdaDisassemblySubsystem>()) { if (D->IsOpen()) { D->CloseWindow(); } }
		if (UAsdaPackagePanelSubsystem* P = C->Sub<UAsdaPackagePanelSubsystem>()) { P->ClosePanel(); }
		if (UAsdaBoosterPanelSubsystem* B = C->Sub<UAsdaBoosterPanelSubsystem>()) { B->ClosePrevWindow(); if (B->IsWindowOpen()) { B->OnCloseButton(); } }
		if (UAsdaInventorySubsystem* I = C->Sub<UAsdaInventorySubsystem>())
		{
			for (const TPair<int32, TArray<uint8>>& S : C->Saved)
			{
				I->ApplyExternalRecord(S.Value.Num() == FAsdaItemRecordWriter::kSize ? S.Value : FAsdaItemRecordWriter::ClearCell(1, S.Key),
					UAsdaInventorySubsystem::EExternalRecordMode::AsIs);
			}
			if (I->IsWindowOpen()) { I->CloseWindow(); }
		}
		if (UAsdaItemTooltipSubsystem* T = C->Sub<UAsdaItemTooltipSubsystem>()) { T->HideTooltip(); }
		if (C->Self) { C->Self->Restore(); }
		AsdaSelfTestKit::Finish(W, kCmd, C->bQuit, LogAsdaBoosterLootTest, kPfx, C->Ok, C->Fail);
	}

	static void Run(const FCtxRef& C)
	{
		UAsdaItemTableSubsystem* IT = C->Sub<UAsdaItemTableSubsystem>();
		UAsdaInventorySubsystem* Inv = C->Sub<UAsdaInventorySubsystem>();
		if (!IT || !Inv || !C->Sub<UAsdaWindowSubsystem>() || !C->Sub<UAsdaBoosterPanelSubsystem>() || !C->Sub<UAsdaPackagePanelSubsystem>()
			|| !C->Sub<UAsdaDisassemblySubsystem>())
		{
			C->Expect(false, TEXT("подсистемы таблиц/инвентаря/окон/бустера/окна 26500/разборки есть"));
			Finish(C);
			return;
		}
		// Уровень 70: гейт уровня предмета не должен мешать пути (75834 — с 60-го).
		if (UAsdaNetSubsystem* Net = AsdaSelfTestKit::NetOf(C->GI()))
		{
			C->Self = MakeShared<FAsdaNetSelfScope>(Net, kCmd);
			AsdaTestNet::SelfLevel(Net) = 70;
		}

		// ═══ D. Данные ═══
		{
			int32 Demon = 0;   // бустеров/коробок 75825–75851 с прежним «демонским» именем
			for (int32 Code = 75825; Code <= 75851; ++Code)
			{
				const FString N = IT->GetItemName(Code);
				if (HasOldName(N)) { ++Demon; UE_LOG(LogAsdaBoosterLootTest, Warning, TEXT("%s: %d «%s»"), kPfx, Code, *N); }
			}
			C->Expect(Demon == 0, FString::Printf(TEXT("D1 у 75825–75851 нет прежнего «демонского» имени (осталось %d); 75834 = «%s»"),
				Demon, *IT->GetItemName(kBooster)));
			const UDataTable* Dt = IT->GetDataTable(FName(TEXT("decompositiontable")));
			int32 InRange = 0, Beyond = 0;   // флаг 1 внутри серверной таблицы / за её пределом
			if (Dt)
			{
				for (const TPair<FName, uint8*>& It : Dt->GetRowMap())
				{
					const FAsdaDecompositiontableRow* R = reinterpret_cast<const FAsdaDecompositiontableRow*>(It.Value);
					if (!R || R->ResultDrawCount != 1) { continue; }
					((R->ItemCode < kSrvFirst || R->ItemCode > kSrvLast) ? Beyond : InRange)++;
				}
			}
			C->Expect(Dt && Beyond == 0 && InRange >= 41705, FString::Printf(
				TEXT("D2 флаг «разбирается» только у кодов серверной таблицы %d…%d: внутри %d (было 41 705), за пределом %d (было 2 652)"),
				kSrvFirst, kSrvLast, InRange, Beyond));
		}

		// ═══ G. Снаряжение (пачка 11, 07.11.2026): клиентские группы опций = новым значениям сервера, предел pricetable = 8 ═══
		//   [шлем, рубаха, грейд Hero] Pirate Captain, Pirate Lieutenant, Vampire Lord, High Vampire: группы шлема 5983/5984 и кирасы воина 5975/5976 (эталон).
		{
			const int32 Heads[] = { 73143, 73089, 74448, 74502 };   // шлемы ур.40, воин
			for (int32 Code : Heads)
			{
				const FAsdaItemRecord* R = IT->GetItem(Code);
				FAsdaPriceRow Pr;
				const bool bPrice = IT->GetPrice(Code, Pr);
				C->Expect(R && R->OptionGroup0 == 5983 && R->OptionGroup1 == 5984 && bPrice && Pr.dw03 == 8, FString::Printf(
					TEXT("G1 %d «%s»: группы %d/%d (ждём 5983/5984), предел pricetable %d (ждём 8)"),
					Code, *IT->GetItemName(Code), R ? R->OptionGroup0 : -1, R ? R->OptionGroup1 : -1, bPrice ? Pr.dw03 : -1));
			}
			const FAsdaItemRecord* Sh = IT->GetItem(73144);
			C->Expect(Sh && Sh->OptionGroup0 == 5975 && Sh->OptionGroup1 == 5976, FString::Printf(
				TEXT("G2 рубаха 73144: группы %d/%d (ждём 5975/5976 — кираса воина, образец 61801)"), Sh ? Sh->OptionGroup0 : -1, Sh ? Sh->OptionGroup1 : -1));

			// Класс B (в стоке предел 0): по одной шапке семейства, значения — цели Reports/gear_fix_20261107/target_B.json.
			struct FClassB { int32 Code; int32 G0; int32 G1; int32 Limit; };   // код, группы опций, предел pricetable
			const FClassB ClassB[] = { {73197, 5983, 5984, 8}, {73236, 5983, 5984, 8}, {73305, 5933, 5934, 7}, {73560, 5933, 5934, 7} };
			for (const FClassB& B : ClassB)
			{
				const FAsdaItemRecord* R = IT->GetItem(B.Code);
				FAsdaPriceRow Pr;
				const bool bPrice = IT->GetPrice(B.Code, Pr);
				C->Expect(R && R->OptionGroup0 == B.G0 && R->OptionGroup1 == B.G1 && bPrice && Pr.dw03 == B.Limit, FString::Printf(
					TEXT("G3 класс B %d «%s»: группы %d/%d (ждём %d/%d), предел pricetable %d (ждём %d)"),
					B.Code, *IT->GetItemName(B.Code), R ? R->OptionGroup0 : -1, R ? R->OptionGroup1 : -1, B.G0, B.G1, bPrice ? Pr.dw03 : -1, B.Limit));
			}
			// Удочки исключены из правки (данных нет): групп и предела у них быть не должно.
			const int32 Rods[] = { 69658, 69659, 69661, 69662, 69663, 69665, 69666 };   // коды target_B_rods_excluded.json
			for (int32 Code : Rods)
			{
				const FAsdaItemRecord* R = IT->GetItem(Code);
				FAsdaPriceRow Pr;
				const int32 Limit = IT->GetPrice(Code, Pr) ? Pr.dw03 : 0;   // нет строки цены = предела нет
				C->Expect(R && R->OptionGroup0 == 0 && R->OptionGroup1 == 0 && Limit == 0, FString::Printf(
					TEXT("G4 удочка %d: группы %d/%d, предел %d (ждём 0/0, 0 — не менялась)"), Code, R ? R->OptionGroup0 : -1, R ? R->OptionGroup1 : -1, Limit));
			}
		}

		// ═══ B. Бустер 75834 → барабан → «Вид» ═══
		At(C, 0.5f, [C]()
		{
			Put(C, kCellUse, kBooster);
			UAsdaInventorySubsystem* I = C->Sub<UAsdaInventorySubsystem>();
			if (I && !I->IsWindowOpen()) { I->OpenWindow(); }
			if (I) { I->SetTab(1); }
		});
		At(C, 1.4f, [C]()
		{
			C->Expect(DblClickBag(C, kCellUse), TEXT("B0 иконка бустера 75834 в сумке на экране — двойной щелчок"));
		});
		At(C, 2.4f, [C]()
		{
			UAsdaBoosterPanelSubsystem* Bp = C->Sub<UAsdaBoosterPanelSubsystem>();
			C->Expect(Bp && Bp->IsWindowOpen(), TEXT("B1 двойной щелчок по бустеру → окно барабана 10050"));
			FVector2D P;
			UWidget* Btn = Node(C, kDrumId, TEXT("BOOSTER_PreviewBtn"));
			const bool bGeo = Center(Btn, P);
			C->Expect(bGeo, TEXT("B2 кнопка «Вид» (10072) на экране"));
			// ★ C. Камера при открытом барабане (баг «во время открытия бустера ПКМ не вращает камеру»), потом щелчок «Вид».
			CameraPair(C, TEXT("CB"), FName(TEXT("BOOSTING_WIN")), TEXT("asda.Booster.ZoneFrame"), [C, bGeo, P]()
			{
				Shot(C, TEXT("CB_camera_drum"));
				if (bGeo) { Click(C, P); }
			});
		});
		At(C, 4.9f, [C]()
		{
			UAsdaItemTableSubsystem* IT = C->Sub<UAsdaItemTableSubsystem>();
			const UAsdaBoosterPanelSubsystem* Bp = C->Sub<UAsdaBoosterPanelSubsystem>();
			const UAsdaWindowSubsystem* WM = C->Sub<UAsdaWindowSubsystem>();
			const bool bShown = Bp && Bp->IsPrevVisible() && WM && WM->IsWindowVisible(kPrevId) && Bp->GetPrevItemCode() == kBooster;
			C->Expect(bShown, FString::Printf(TEXT("B3 «Вид» открыт для 75834 (код=%d)"), Bp ? Bp->GetPrevItemCode() : -1));
			const FString Want = IT ? IT->GetItemName(kBooster) : FString();
			const UTextBlock* Nm = Cast<UTextBlock>(Node(C, kPrevId, TEXT("BOOSTER_PREV_NAME")));
			const FString Got = Nm ? Nm->GetText().ToString() : FString();
			C->Expect(!Want.IsEmpty() && Got == Want && !HasOldName(Got), FString::Printf(TEXT("B4 имя в «Виде» = «%s» (ждём «%s», не прежнее)"), *Got, *Want));
			TArray<int32> S35;
			const bool bRow = IT && IT->GetBoosterSlotsRaw(kBooster, S35) && S35.Num() == 35 && S35[0] == kBoostSp0;
			C->Expect(bRow, FString::Printf(TEXT("B5 строка booster_list 75834: особая 0 = %d (ждём %d)"), S35.Num() ? S35[0] : -1, kBoostSp0));
			UImage* Sp0 = Cast<UImage>(Node(C, kPrevId, TEXT("BOOSTER_PREV_SITEM00_")));
			FVector2D P;
			C->Expect(Center(Sp0, P), TEXT("B6 иконка особой позиции 0 «Вида» видна на экране"));
			Shot(C, TEXT("B_booster_prev"));
		});
		At(C, 5.9f, [C]()
		{
			if (UAsdaBoosterPanelSubsystem* B = C->Sub<UAsdaBoosterPanelSubsystem>()) { B->ClosePrevWindow(); if (B->IsWindowOpen()) { B->OnCloseButton(); } }
		});

		// ═══ P. Коробка 75844 → окно состава 26500 ═══
		At(C, 6.4f, [C]()
		{
			Put(C, kCellUse, kBox);
			// ★ Барабан при открытии закрывает сумку (`asda.Booster.OpenClosesBag`, эталон `sowelWindowClose`) — открыть снова.
			UAsdaInventorySubsystem* I = C->Sub<UAsdaInventorySubsystem>();
			if (I && !I->IsWindowOpen()) { I->OpenWindow(); }
			if (I) { I->SetTab(1); }
		});
		At(C, 7.2f, [C]() { C->Expect(DblClickBag(C, kCellUse), TEXT("P0 иконка коробки 75844 в сумке на экране — двойной щелчок")); });
		At(C, 8.2f, [C]()
		{
			UAsdaItemTableSubsystem* IT = C->Sub<UAsdaItemTableSubsystem>();
			const UAsdaPackagePanelSubsystem* Pk = C->Sub<UAsdaPackagePanelSubsystem>();
			C->Expect(Pk && Pk->IsPanelOpen() && Pk->GetStampMode() == 1,
				FString::Printf(TEXT("P1 двойной щелчок по коробке → окно 26500 (вскрытие) (панель=%d режим=%d)"),
					Pk && Pk->IsPanelOpen() ? 1 : 0, Pk ? Pk->GetStampMode() : -1));
			const FString Want = IT ? IT->GetItemName(kBox) : FString();
			C->Expect(!Want.IsEmpty() && !HasOldName(Want) && WinShowsText(C, kPkgId, Want),
				FString::Printf(TEXT("P2 окно состава показывает новое имя «%s»"), *Want));
			bool bEgg = false;
			for (int32 r = 0; Pk && r < UAsdaPackagePanelSubsystem::kBlocks && !bEgg; ++r)
			{
				for (int32 k = 0; k < UAsdaPackagePanelSubsystem::kBigCells && !bEgg; ++k) { bEgg = Pk->DebugCellCode(r, true, k) == kBoxItem; }
			}
			C->Expect(bEgg, TEXT("P3 в составе на экране — «Яйцо снежного льва» 76338"));
			Shot(C, TEXT("P_package"));
			// ★ C. Камера при открытом окне состава (тот же дефект зоны во весь экран).
			CameraPair(C, TEXT("CP"), FName(TEXT("PACKAGE_CONTENTS")), TEXT("asda.Package.ZoneFrame"), []() {});
		});
		At(C, 10.6f, [C]() { if (UAsdaPackagePanelSubsystem* P = C->Sub<UAsdaPackagePanelSubsystem>()) { P->ClosePanel(); } });

		// ═══ X. Окно разборки ═══
		At(C, 11.1f, [C]()
		{
			Put(C, kCellUse, 0);
			Put(C, kCellCos, kCostume);
			Put(C, kCellGear, kGearOk);
			if (UAsdaDisassemblySubsystem* D = C->Sub<UAsdaDisassemblySubsystem>()) { if (!D->IsOpen()) { D->Toggle(TEXT("самотест BoosterLoot")); } }
		});
		At(C, 12.2f, [C]()
		{
			const UAsdaDisassemblySubsystem* D = C->Sub<UAsdaDisassemblySubsystem>();
			const TArray<int32> L = D ? D->GetListCells() : TArray<int32>();
			C->Expect(D && D->IsOpen(), TEXT("X0 окно разборки 32000 открыто"));
			C->Expect(!L.Contains(kCellCos), FString::Printf(TEXT("X1 «Шляпа элитного оленя» 76264 (сервер её не разбирает) в списке НЕТ (позиций %d)"), L.Num()));
			C->Expect(L.Contains(kCellGear), TEXT("X2 «Шлем ловкача» 75676 (в серверной таблице) в списке ЕСТЬ"));
			FName Hit;
			const FVector2D Wp = WorldPoint();
			const UAsdaWindowSubsystem* WM = C->Sub<UAsdaWindowSubsystem>();
			C->Expect(WM && !WM->IsScreenPointOverAnyUiWindow(Wp, Hit), FString::Printf(
				TEXT("X3 при открытом окне разборки точка мира (%.0f,%.0f) не над окном (камера ПКМ свободна; попадание «%s»)"), Wp.X, Wp.Y, *Hit.ToString()));
			Shot(C, TEXT("X_disassembly"));
		});
		At(C, 13.2f, [C]() { Finish(C); });
	}
}

static FAutoConsoleCommandWithWorldAndArgs GAsdaSelfTestBoosterLoot(
	TEXT("asda.SelfTest.BoosterLoot"),
	TEXT("Самотест волны booster-loot (05.11.2026): имена бустеров/коробок по составу (75834 → «Вид», 75844 → окно 26500) и окно разборки "
	     "без предметов, которых нет в серверной таблице. Путь игрока, офлайн, карта мира. Кадры — Reports/booster_bundles/2026-11-05/frames. "
	     "`delay=N` `quit`."),
	FConsoleCommandWithWorldAndArgsDelegate::CreateLambda([](const TArray<FString>& Args, UWorld* W)
	{
		using namespace AsdaBoosterLootTest;
		const FAsdaSelfTestArgs KA = FAsdaSelfTestArgs::Parse(Args);
		if (!KA.Has(TEXT("selfready")) && W && GEngine && AsdaSelfTestKit::NeedSelfSpawn(W))
		{
			GEngine->Exec(W, TEXT("Asda.Load.CurrentMap 3"));
			GEngine->Exec(W, TEXT("Asda.World.FakeSelfSpawn 256 256"));
			const FString Again = FString::Printf(TEXT("%s selfready%s"), kCmd, KA.bQuit ? TEXT(" quit") : TEXT(""));
			TWeakObjectPtr<UWorld> WW(W);
			FTimerHandle H;
			W->GetTimerManager().SetTimer(H, FTimerDelegate::CreateLambda([WW, Again]() { if (WW.IsValid() && GEngine) { GEngine->Exec(WW.Get(), *Again); } }), 2.f, false);
			return;
		}
		AsdaCVarSave::ArmWorldGuard(W, kCmd);
		if (AsdaSelfTestKit::RefuseStart(W, kCmd, KA)) { return; }
		if (!W) { return; }
		FCtxRef C = MakeShared<FCtx>();
		C->W = W;
		C->bQuit = KA.bQuit;
		C->FrameDir = FPaths::ConvertRelativePathToFull(FPaths::ProjectDir() / TEXT("../Reports/booster_bundles/2026-11-05/frames"));
		FTimerHandle H;
		W->GetTimerManager().SetTimer(H, FTimerDelegate::CreateLambda([C]()
		{
			if (C->W.IsValid() && AsdaSelfTestKit::RefuseDeferred(C->W.Get(), kCmd, C->bQuit)) { return; }
			Run(C);
		}), FMath::Max(0.01f, KA.Delay), false);
	}));

#endif // ASDA_DEV_CONSOLE && !UE_BUILD_SHIPPING
