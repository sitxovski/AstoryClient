// Copyright (c) 2026 @sitxovski (Discord)
// ★★★ САМОТЕСТ «СТРЕЛКА СУМКИ 408» (08.11.2026, пачка 17, баг 5 владельца): `asda.SelfTest.InvArrow [quit]`.
//   Жалоба игрока: стрелка, раскрывающая правую панель сумки (408 `INVEN_OPENBUT`), «капризная» — срабатывает не всегда,
//   «надо кликнуть мимо, а потом снова на предмет».
//   Путь игрока: настоящие щелчки Slate (`SimulateClickAt`) по живой геометрии стрелок 408/412, вкладок 402, ячеек сетки,
//   надетых слотов и точки мира; офлайн (-AsdaNoNet). На каждом щелчке по стрелке журнал: какое окно ловит точку
//   (`FindWindowCatchingAt`), чей лист Slate под ней, съел ли нажатие гейт верхнего окна, состояние панели до и после.
//   Разделы — в строках ниже (префикс «СТРЕЛКА-СУМКИ»). Кадры — `Reports/batch17/inv_arrow/<язык>_<шаг>.png`.
//   Итог: «СТРЕЛКА-СУМКИ ИТОГ: ok=N fail=M». Сумка, надетое, ключи и запись вкладки возвращаются в конце.
#include "CoreMinimal.h"
#include "AsdaDevConsole.h"
#include "Test/AsdaSelfTestInput.h"   // ★ R5-c (12.11.2026): общая точка самотестов вместо своих копий помощников

#if ASDA_DEV_CONSOLE && !UE_BUILD_SHIPPING

#include "AsdaNetSubsystem.h"
#include "AsdaCVarSave.h"
#include "Test/AsdaSelfTestKit.h"
#include "Text/AsdaLang.h"
#include "UI/AsdaInventorySubsystem.h"
#include "UI/AsdaUiHelpers.h"
#include "UI/AsdaWindowSubsystem.h"
#include "Blueprint/UserWidget.h"
#include "Blueprint/WidgetTree.h"
#include "Components/Widget.h"
#include "Engine/Engine.h"
#include "Engine/GameInstance.h"
#include "Engine/GameViewportClient.h"
#include "Engine/World.h"
#include "Framework/Application/SlateApplication.h"
#include "Layout/WidgetPath.h"
#include "Types/ReflectionMetadata.h"
#include "Misc/ConfigCacheIni.h"
#include "Misc/Paths.h"
#include "TimerManager.h"
#include "UnrealClient.h"

DEFINE_LOG_CATEGORY_STATIC(LogAsdaInvArrowTest, Log, All);

namespace AsdaInvArrowTest
{
	static const TCHAR* kCmd = TEXT("asda.SelfTest.InvArrow");    // имя команды
	static const TCHAR* kPfx = TEXT("СТРЕЛКА-СУМКИ");             // префикс строк и итога
	static const FName kWinId(TEXT("INVEN_WIN"));                 // окно сумки
	static const TCHAR* kSect = TEXT("AsdaInventory");            // секция настроек вкладки
	static const TCHAR* kKey  = TEXT("LastTab");                  // ключ вкладки
	static constexpr float kStep = 0.5f;                          // шаг сценария, с

	// Надетые вещи: слот → код (как у `asda.SelfTest.InvTab`).
	struct FWorn { int32 Slot; int32 Code; };
	static const FWorn kWorn[] = { {0, 20556}, {1, 20552}, {2, 20554}, {3, 20555}, {5, 20679}, {6, 20679}, {9, 22818} };

	// Контекст прогона (живёт между таймерами).
	struct FCtx
	{
		int32 Ok = 0, Fail = 0;                    // пройдено / провалено
		TWeakObjectPtr<UWorld> W;                  // мир прогона
		bool bQuit = false;                        // выйти после итога
		FString FrameDir;                          // папка кадров
		float T = 0.f;                             // время следующего шага
		TArray<TArray<uint8>> SavedBag, SavedWorn, SavedSpend;   // сырые записи контейнеров 1, 3, 2 до прогона
		TSharedPtr<FAsdaNetSelfScope> Self;        // свой персонаж — вернуть в конце
		bool bHadIni = false;                      // была ли запись вкладки до прогона
		int32 IniWas = 0;                          // её значение
		int32 Eaten0 = 0;                          // счётчик съеденных гейтом нажатий перед щелчком по стрелке
		bool bWas = false;                         // панель раскрыта перед щелчком по стрелке

		void Expect(bool b, const FString& What)
		{
			if (b) { ++Ok;   UE_LOG(LogAsdaInvArrowTest, Log,   TEXT("%s ok   %s"), kPfx, *What); }
			else   { ++Fail; UE_LOG(LogAsdaInvArrowTest, Error, TEXT("%s FAIL %s"), kPfx, *What); }
		}
		UGameInstance* GI() const { return W.IsValid() ? W->GetGameInstance() : nullptr; }
		template <class T2> T2* Sub() const { UGameInstance* G = GI(); return G ? G->GetSubsystem<T2>() : nullptr; }
		UAsdaInventorySubsystem* Inv() const { return Sub<UAsdaInventorySubsystem>(); }
		UAsdaWindowSubsystem* WM() const { return Sub<UAsdaWindowSubsystem>(); }
	};
	using FCtxRef = TSharedRef<FCtx>;

	// Следующий шаг сценария (через `Wait` после предыдущего).
	static void Step(const FCtxRef& C, TFunction<void()> Fn, float Wait = kStep)
	{
		UWorld* W = C->W.Get();
		if (!W) { return; }
		C->T += Wait;
		FTimerHandle H;
		W->GetTimerManager().SetTimer(H, FTimerDelegate::CreateLambda(MoveTemp(Fn)), C->T, false);
	}
	// Центр живой геометрии узла (false — не отрисован / скрыт).
	static bool Center(const UWidget* Wd, FVector2D& Out)
	{
		FVector2D Mn, Mx;
		if (!Wd || !Wd->IsVisible() || !AsdaUi::WidgetAbsRect(Wd, Mn, Mx) || (Mx - Mn).X < 2.0 || (Mx - Mn).Y < 2.0) { return false; }
		Out = (Mn + Mx) * 0.5f;
		return true;
	}
	// Узел окна сумки по точному имени.
	static UWidget* Node(const FCtxRef& C, const TCHAR* Name)
	{
		UUserWidget* Win = C->WM() ? C->WM()->FindWindow(kWinId) : nullptr;
		return Win ? Win->GetWidgetFromName(FName(Name)) : nullptr;
	}
	// Ключ правки.
	static void Key(int32 V) { AsdaCVarSave::SetForTest(TEXT("asda.Inven.ArrowOpensEquipTab"), V); }
	// Курсор в точку + событие движения Slate.
	using AsdaSelfTestInput::Hover;   // ★ R5-c (12.11.2026): общая точка (`Test/AsdaSelfTestInput.h`) — копия была дословной.
	// Настоящий щелчок Slate.
	static void Click(const FCtxRef& C, const FVector2D& P) { if (UAsdaWindowSubsystem* WM = C->WM()) { Hover(P); WM->SimulateClickAt(P, /*HoldFrames*/ 2); } }
	// Щелчок по узлу окна сумки (false — узла не видно).
	static bool ClickNode(const FCtxRef& C, const TCHAR* Name)
	{
		FVector2D P;
		if (!Center(Node(C, Name), P)) { return false; }
		Click(C, P);
		return true;
	}
	// Абсолютная точка внутри вьюпорта (доли ширины и высоты).
	static FVector2D ViewPoint(float Fx, float Fy)
	{
		if (!GEngine || !GEngine->GameViewport || !GEngine->GameViewport->GetGameViewportWidget().IsValid()) { return FVector2D::ZeroVector; }
		const FGeometry G = GEngine->GameViewport->GetGameViewportWidget()->GetCachedGeometry();
		return G.LocalToAbsolute(G.GetLocalSize() * FVector2D(Fx, Fy));
	}
	// Лист Slate под точкой.
	static TSharedPtr<SWidget> LeafAt(const FVector2D& P)
	{
		FSlateApplication& App = FSlateApplication::Get();
		const FWidgetPath Path = App.LocateWindowUnderMouse(P, App.GetInteractiveTopLevelWindows());
		return Path.IsValid() ? TSharedPtr<SWidget>(Path.Widgets.Last().Widget) : nullptr;
	}
	// Кадр (снимок берётся в конце кадра).
	static void Shot(const FCtxRef& C, const TCHAR* Tag)
	{
		const FString File = C->FrameDir / FString::Printf(TEXT("%s_%s.png"), FAsdaLang::Code(FAsdaLang::Current()), Tag);
		UE_LOG(LogAsdaInvArrowTest, Log, TEXT("%s: КАДР → %s"), kPfx, *File);
		FScreenshotRequest::RequestScreenshot(File, /*bShowUI*/ true, /*bAddFilenameSuffix*/ false);
	}
	// Щелчок по стрелке 408 с журналом: кто ловит точку, чей лист, состояние до. false — стрелки не видно.
	static bool ClickArrow(const FCtxRef& C, const FString& Tag)
	{
		UAsdaWindowSubsystem* WM = C->WM();
		FVector2D P;
		const bool bSeen = Center(Node(C, TEXT("INVEN_OPENBUT")), P);
		C->bWas = C->Inv()->IsPanelExpanded();
		C->Eaten0 = WM ? WM->GetTopPressEatenCount() : 0;
		if (!bSeen)
		{
			UE_LOG(LogAsdaInvArrowTest, Log, TEXT("%s %s: стрелка 408 не видна (панель %s, вкладка %d)"), kPfx, *Tag,
				C->bWas ? TEXT("раскрыта") : TEXT("свёрнута"), C->Inv()->GetTab());
			return false;
		}
		Hover(P);
		const FName Catch = WM->FindWindowCatchingAt(P);
		const FName Leaf = WM->DebugWindowOwningSlate(LeafAt(P));
		UE_LOG(LogAsdaInvArrowTest, Log, TEXT("%s %s: щелчок по 408 в (%.0f,%.0f): ловит «%s», лист Slate окна «%s», вкладка %d, панель %s"),
			kPfx, *Tag, P.X, P.Y, *Catch.ToString(), *Leaf.ToString(), C->Inv()->GetTab(), C->bWas ? TEXT("раскрыта") : TEXT("свёрнута"));
		WM->SimulateClickAt(P, /*HoldFrames*/ 2);
		return true;
	}
	// Полный разбор узлов стрелок 408/412 (имя, тип, видимость, прямоугольник) и обход видимой картинки 408 сеткой 3 px:
	//   сколько точек картинки ведут в саму кнопку (лист пути Slate внутри кнопки).
	static void SweepArrow(const FCtxRef& C, const FString& Tag)
	{
		UUserWidget* Win = C->WM() ? C->WM()->FindWindow(kWinId) : nullptr;
		if (!Win || !Win->WidgetTree) { C->Expect(false, Tag + TEXT(": окно")); return; }
		TArray<UWidget*> All;
		Win->WidgetTree->GetAllWidgets(All);
		FVector2D UMn(1e9, 1e9), UMx(-1e9, -1e9);
		for (UWidget* Wd : All)
		{
			const FString N = Wd ? Wd->GetName() : FString();
			if (!N.Contains(TEXT("OPENBUT")) && !N.Contains(TEXT("CLOSEBUT"))) { continue; }
			FVector2D Mn, Mx;
			const bool bR = AsdaUi::WidgetAbsRect(Wd, Mn, Mx);
			UE_LOG(LogAsdaInvArrowTest, Log, TEXT("%s %s: узел %s (%s) вид=%d отрисован=%d [%.0f,%.0f — %.0f,%.0f]"), kPfx, *Tag, *N,
				*Wd->GetClass()->GetName(), (int32)Wd->GetVisibility(), bR ? 1 : 0, Mn.X, Mn.Y, Mx.X, Mx.Y);
			if (bR && N.Contains(TEXT("OPENBUT")) && Wd->IsVisible()) { UMn = FVector2D::Min(UMn, Mn); UMx = FVector2D::Max(UMx, Mx); }
		}
		UWidget* Btn = Node(C, TEXT("INVEN_OPENBUT"));
		const TSharedPtr<SWidget> BtnS = Btn ? Btn->GetCachedWidget() : nullptr;
		if (!BtnS.IsValid() || UMx.X <= UMn.X) { C->Expect(false, Tag + TEXT(": кнопка 408 отрисована")); return; }
		FSlateApplication& App = FSlateApplication::Get();
		int32 Total = 0, InBtn = 0;
		TMap<FString, int32> Other;   // кто ловит точки мимо кнопки
		for (double Y = UMn.Y + 1; Y < UMx.Y - 1; Y += 3)
		{
			for (double X = UMn.X + 1; X < UMx.X - 1; X += 3)
			{
				++Total;
				const FWidgetPath Path = App.LocateWindowUnderMouse(FVector2D(X, Y), App.GetInteractiveTopLevelWindows());
				bool bIn = false;
				for (int32 i = 0; Path.IsValid() && i < Path.Widgets.Num(); ++i) { if (Path.Widgets[i].Widget == BtnS.ToSharedRef()) { bIn = true; break; } }
				if (bIn) { ++InBtn; continue; }
				const FString Who = Path.IsValid() ? FReflectionMetaData::GetWidgetDebugInfo(&Path.Widgets.Last().Widget.Get()) : TEXT("—");
				Other.FindOrAdd(Who)++;
			}
		}
		for (const TPair<FString, int32>& Kv : Other) { UE_LOG(LogAsdaInvArrowTest, Log, TEXT("%s %s: мимо кнопки %d точек → %s"), kPfx, *Tag, Kv.Value, *Kv.Key); }
		C->Expect(Total > 0 && InBtn == Total, FString::Printf(TEXT("%s: точек картинки 408 в кнопке %d из %d"), *Tag, InBtn, Total));
	}
	// Итог щелчка по стрелке: панель должна стать `bWant`, вкладка — `WantTab` (−1 — не проверять); нажатие гейтом не съедено.
	static void ExpectArrow(const FCtxRef& C, const FString& Tag, bool bWant, int32 WantTab)
	{
		UAsdaWindowSubsystem* WM = C->WM();
		const int32 Eaten = WM ? WM->GetTopPressEatenCount() - C->Eaten0 : -1;
		const bool bNow = C->Inv()->IsPanelExpanded();
		const int32 Tab = C->Inv()->GetTab();
		C->Expect(bNow == bWant && Eaten == 0 && (WantTab < 0 || Tab == WantTab),
			FString::Printf(TEXT("%s: панель %s → %s (ждём %s), вкладка %d (ждём %d), съедено гейтом %d"), *Tag,
			C->bWas ? TEXT("раскрыта") : TEXT("свёрнута"), bNow ? TEXT("раскрыта") : TEXT("свёрнута"), bWant ? TEXT("раскрыта") : TEXT("свёрнута"),
			Tab, WantTab, Eaten));
	}
	// Значение вкладки в настройках (−1 — записи нет).
	static int32 IniTab()
	{
		int32 V = -1;
		if (GConfig && !GConfig->GetInt(kSect, kKey, V, GGameUserSettingsIni)) { V = -1; }
		return V;
	}
	// Сохранить контейнер.
	static void SaveCont(const FCtxRef& C, int32 Cont, int32 N, TArray<TArray<uint8>>& Out)
	{
		Out.SetNum(N);
		for (int32 i = 0; i < N; ++i)
		{
			const FAsdaItemInstance* It = C->Inv()->GetCell(Cont, i);
			Out[i] = (It && !It->IsEmpty()) ? It->Raw : TArray<uint8>();
		}
	}
	// Вернуть контейнер.
	static void RestoreCont(const FCtxRef& C, int32 Cont, const TArray<TArray<uint8>>& In)
	{
		for (int32 i = 0; i < In.Num(); ++i)
		{
			C->Inv()->DebugPutItem(Cont, i, 0, 0);
			if (In[i].Num() > 0) { C->Inv()->ApplyExternalRecord(In[i], UAsdaInventorySubsystem::EExternalRecordMode::AsIs); }
		}
	}
	// Надеть набор, положить снаряжение в сумку снаряжения (1:0, 1:1) и расходник (2:0).
	static void Dress(const FCtxRef& C)
	{
		UAsdaInventorySubsystem* I = C->Inv();
		for (int32 i = 0; i < 60; ++i) { I->DebugPutItem(1, i, 0, 0); I->DebugPutItem(2, i, 0, 0); }
		for (int32 i = 0; i < 11; ++i) { I->DebugPutItem(3, i, 0, 0); }
		for (const FWorn& X : kWorn) { I->DebugPutItem(3, X.Slot, X.Code, 1); }
		I->DebugPutItem(1, 0, 20553, 1);
		I->DebugPutItem(1, 1, 20553, 1);
		I->DebugPutItem(2, 0, 20592, 3);
	}

	static void Finish(const FCtxRef& C)
	{
		if (C->Inv()->IsWindowOpen()) { C->Inv()->CloseWindow(); }
		RestoreCont(C, 1, C->SavedBag);
		RestoreCont(C, 2, C->SavedSpend);
		RestoreCont(C, 3, C->SavedWorn);
		if (GConfig)
		{
			if (C->bHadIni) { GConfig->SetInt(kSect, kKey, C->IniWas, GGameUserSettingsIni); }
			else            { GConfig->RemoveKey(kSect, kKey, GGameUserSettingsIni); }
			GConfig->Flush(false, GGameUserSettingsIni);
		}
		Key(1);
		if (C->Self.IsValid()) { C->Self->Restore(); }
		AsdaSelfTestKit::Finish(C->W.Get(), kCmd, C->bQuit, LogAsdaInvArrowTest, kPfx, C->Ok, C->Fail);
	}

	// Один щелчок по стрелке 408 и проверка итога.
	static void Arrow(const FCtxRef& C, const FString& Tag, bool bWant, const TCHAR* Frame = nullptr, int32 WantTab = 1)
	{
		Step(C, [C, Tag]() { C->Expect(ClickArrow(C, Tag), Tag + TEXT(": стрелка 408 видна")); });
		Step(C, [C, Tag, bWant, Frame, WantTab]() { ExpectArrow(C, Tag, bWant, WantTab); if (Frame) { Shot(C, Frame); } });
	}

	static void Run(const FCtxRef& C)
	{
		UAsdaInventorySubsystem* I = C->Inv();
		UAsdaNetSubsystem* Net = C->Sub<UAsdaNetSubsystem>();
		if (!I || !Net || !C->WM())
		{
			C->Expect(false, TEXT("подсистемы на месте"));
			AsdaSelfTestKit::Finish(C->W.Get(), kCmd, C->bQuit, LogAsdaInvArrowTest, kPfx, C->Ok, C->Fail);
			return;
		}
		C->Self = MakeShared<FAsdaNetSelfScope>(Net, kCmd);
		SaveCont(C, 1, 60, C->SavedBag);
		SaveCont(C, 2, 60, C->SavedSpend);
		SaveCont(C, 3, 11, C->SavedWorn);
		C->bHadIni = GConfig && GConfig->GetInt(kSect, kKey, C->IniWas, GGameUserSettingsIni);
		if (GConfig) { GConfig->RemoveKey(kSect, kKey, GGameUserSettingsIni); }
		if (I->IsWindowOpen()) { I->CloseWindow(); }
		Dress(C);
		I->OpenWindow();
		C->Expect(I->IsWindowOpen() && I->GetTab() == 0, TEXT("A0 окно открыто на вкладке 0"));

		// ═══ K. Ключ 0 = оригинал (шаг 0 — воспроизведение жалобы): на «Расходниках» видимая стрелка мертва ═══
		Step(C, [C]() { Key(0); Shot(C, TEXT("A0_open")); }, 1.0f);
		Arrow(C, TEXT("K1 (ключ 0, как оригинал) вкладка 0, стрелка 408 не срабатывает"), false, TEXT("K1_tab0_key0_dead"), 0);
		Arrow(C, TEXT("K2 (ключ 0) повторное нажатие — снова ничего"), false, nullptr, 0);

		// ═══ A. Ключ 1: стрелка на «Расходниках» переводит на «Снаряжение» и раскрывает панель ═══
		Step(C, [C]() { Key(1); });
		Arrow(C, TEXT("A1 вкладка 0, стрелка 408"), true, TEXT("A1_tab0_arrow"));
		Step(C, [C]() { C->Expect(IniTab() == -1, FString::Printf(TEXT("A2 память вкладки не записана стрелкой (запись %d)"), IniTab())); });
		Step(C, [C]() { C->Expect(ClickNode(C, TEXT("INVEN_CLOSEBUT")), TEXT("A3 стрелка 412 свернуть")); });
		Step(C, [C]() { C->Expect(!C->Inv()->IsPanelExpanded() && C->Inv()->GetTab() == 1, TEXT("A3 панель свёрнута, вкладка 1")); });
		Step(C, [C]() { C->Expect(ClickNode(C, TEXT("INVEN_CHOICEBUT_OPT0")), TEXT("A4 щелчок по вкладке 0")); });

		// ═══ B. Вкладка «Снаряжение», без выбора ═══
		Step(C, [C]() { C->Expect(ClickNode(C, TEXT("INVEN_CHOICEBUT_OPT1")), TEXT("B0 щелчок по вкладке 1")); });
		Step(C, [C]() { C->Expect(C->Inv()->GetTab() == 1, TEXT("B0 вкладка 1")); });
		Step(C, [C]() { SweepArrow(C, TEXT("Z1 зона 408")); });
		Arrow(C, TEXT("B1 вкладка 1 без выбора, стрелка 408"), true, TEXT("B1_tab1_arrow"));
		Step(C, [C]() { C->Expect(ClickNode(C, TEXT("INVEN_CLOSEBUT")), TEXT("B2 стрелка 412 свернуть")); });
		Step(C, [C]() { C->Expect(!C->Inv()->IsPanelExpanded(), TEXT("B2 панель свёрнута")); });
		Arrow(C, TEXT("B3 повторное нажатие 408 после 412"), true);
		Step(C, [C]() { ClickNode(C, TEXT("INVEN_CLOSEBUT")); });
		Arrow(C, TEXT("B4 третье нажатие 408"), true);

		// ═══ C. Выбор предмета → свёртка → стрелка ═══
		Step(C, [C]() { ClickNode(C, TEXT("INVEN_CLOSEBUT")); });
		Step(C, [C]() { FVector2D P; C->Expect(Center(C->Inv()->DebugGetGridIcon(0), P), TEXT("C0 ячейка 0 видна")); Click(C, P); });
		Step(C, [C]() { UE_LOG(LogAsdaInvArrowTest, Log, TEXT("%s C1 после щелчка по предмету: панель %s"), kPfx, C->Inv()->IsPanelExpanded() ? TEXT("раскрыта") : TEXT("свёрнута")); Shot(C, TEXT("C1_item")); });
		Step(C, [C]() { ClickNode(C, TEXT("INVEN_CLOSEBUT")); });
		Arrow(C, TEXT("C2 предмет выбран, панель свёрнута 412, стрелка 408"), true, TEXT("C2_sel_arrow"));
		// Наведение на предмет (подсказка) и сразу стрелка.
		Step(C, [C]() { ClickNode(C, TEXT("INVEN_CLOSEBUT")); });
		Step(C, [C]() { FVector2D P; if (Center(C->Inv()->DebugGetGridIcon(1), P)) { Hover(P); } C->Inv()->DebugTickPointer(); });
		Arrow(C, TEXT("C3 после наведения на предмет (подсказка), стрелка 408"), true);
		// Щелчок по предмету после ручной свёртки — затем стрелка.
		Step(C, [C]() { ClickNode(C, TEXT("INVEN_CLOSEBUT")); });
		Step(C, [C]() { FVector2D P; if (Center(C->Inv()->DebugGetGridIcon(1), P)) { Click(C, P); } });
		Step(C, [C]() { UE_LOG(LogAsdaInvArrowTest, Log, TEXT("%s C4 щелчок по предмету после ручной свёртки: панель %s"), kPfx, C->Inv()->IsPanelExpanded() ? TEXT("раскрыта") : TEXT("свёрнута")); });
		Arrow(C, TEXT("C5 после щелчка по предмету, стрелка 408"), true);
		// Щелчок по пустой ячейке (мимо предмета) — затем стрелка.
		Step(C, [C]() { ClickNode(C, TEXT("INVEN_CLOSEBUT")); });
		Step(C, [C]() { FVector2D P; if (Center(C->Inv()->DebugGetGridIcon(10), P)) { Click(C, P); } });
		Arrow(C, TEXT("C6 после щелчка по пустой ячейке, стрелка 408"), true);

		// ═══ D. Щелчок мимо окна (мир) → стрелка ═══
		Step(C, [C]() { ClickNode(C, TEXT("INVEN_CLOSEBUT")); });
		Step(C, [C]()
		{
			FVector2D P = ViewPoint(0.75f, 0.45f);
			for (const FVector2D& F : { FVector2D(0.75f, 0.45f), FVector2D(0.8f, 0.35f), FVector2D(0.7f, 0.5f), FVector2D(0.85f, 0.4f) })
			{
				P = ViewPoint(F.X, F.Y);
				if (C->WM()->FindWindowCatchingAt(P).IsNone()) { break; }
			}
			UE_LOG(LogAsdaInvArrowTest, Log, TEXT("%s D0 щелчок мимо в (%.0f,%.0f), ловит «%s»"), kPfx, P.X, P.Y, *C->WM()->FindWindowCatchingAt(P).ToString());
			Click(C, P);
		});
		Arrow(C, TEXT("D1 после щелчка мимо окна, стрелка 408"), true);

		// ═══ E. Надетый слот → свёртка → стрелка ═══
		Step(C, [C]() { ClickNode(C, TEXT("INVEN_CLOSEBUT")); });
		Step(C, [C]() { FVector2D P; C->Expect(Center(C->Inv()->DebugGetEquipSlotIcon(1), P), TEXT("E0 надетый слот 1 виден")); Click(C, P); });
		Step(C, [C]() { ClickNode(C, TEXT("INVEN_CLOSEBUT")); });
		Arrow(C, TEXT("E1 после выбора надетого и свёртки, стрелка 408"), true);

		// ═══ F. Закрыть и открыть окно → стрелка ═══
		Step(C, [C]() { C->Inv()->CloseWindow(); });
		Step(C, [C]() { C->Inv()->OpenWindow(); });
		Step(C, [C]() { UE_LOG(LogAsdaInvArrowTest, Log, TEXT("%s F0 переоткрыто: вкладка %d, панель %s"), kPfx, C->Inv()->GetTab(), C->Inv()->IsPanelExpanded() ? TEXT("раскрыта") : TEXT("свёрнута")); });
		Arrow(C, TEXT("F1 после переоткрытия, стрелка 408"), true, TEXT("F1_reopen_arrow"));

		// ═══ G. Вкладка 0 с выбранным расходником → стрелка ═══
		Step(C, [C]() { ClickNode(C, TEXT("INVEN_CHOICEBUT_OPT0")); });
		Step(C, [C]() { FVector2D P; if (Center(C->Inv()->DebugGetGridIcon(0), P)) { Click(C, P); } });
		Arrow(C, TEXT("G1 вкладка 0, выбран расходник, стрелка 408"), true, TEXT("G1_tab0_sel_arrow"));

		// ═══ H. Надетое кольцо (слот 5 → вкладка 0) → свёртка → стрелка ═══
		Step(C, [C]() { FVector2D P; C->Expect(Center(C->Inv()->DebugGetEquipSlotIcon(5), P), TEXT("H0 кольцо (слот 5) видно")); Click(C, P); });
		Step(C, [C]() { UE_LOG(LogAsdaInvArrowTest, Log, TEXT("%s H1 щелчок по кольцу: вкладка %d, панель %s"), kPfx, C->Inv()->GetTab(), C->Inv()->IsPanelExpanded() ? TEXT("раскрыта") : TEXT("свёрнута")); Shot(C, TEXT("H1_ring")); });
		Step(C, [C]() { if (C->Inv()->IsPanelExpanded()) { ClickNode(C, TEXT("INVEN_CLOSEBUT")); } });
		Step(C, [C]() { C->Expect(C->Inv()->GetTab() == 0 && !C->Inv()->IsPanelExpanded(), FString::Printf(TEXT("H2 вкладка 0, панель свёрнута (вкладка %d)"), C->Inv()->GetTab())); });
		Arrow(C, TEXT("H3 после кольца, вкладка 0, стрелка 408"), true, TEXT("H3_ring_arrow"));

		// ═══ R. Переоткрытие на «Расходниках» (память вкладки 0) → стрелка сразу ═══
		Step(C, [C]() { ClickNode(C, TEXT("INVEN_CHOICEBUT_OPT0")); });
		Step(C, [C]() { C->Inv()->CloseWindow(); });
		Step(C, [C]() { C->Inv()->OpenWindow(); });
		Step(C, [C]() { C->Expect(C->Inv()->GetTab() == 0, FString::Printf(TEXT("R0 переоткрыто на вкладке 0 (вкладка %d)"), C->Inv()->GetTab())); });
		Arrow(C, TEXT("R1 первое нажатие после открытия на «Расходниках»"), true, TEXT("R1_reopen_tab0_arrow"));

		Step(C, [C]() { Finish(C); });
	}
}

static FAutoConsoleCommandWithWorldAndArgs GAsdaSelfTestInvArrow(
	TEXT("asda.SelfTest.InvArrow"),
	TEXT("Самотест пачки 17 (08.11.2026): стрелка 408/412 правой панели сумки во всех порядках настоящими щелчками. "
	     "Путь игрока, офлайн. Кадры — Reports/batch17/inv_arrow. `delay=N` `quit`."),
	FConsoleCommandWithWorldAndArgsDelegate::CreateLambda([](const TArray<FString>& Args, UWorld* W)
	{
		using namespace AsdaInvArrowTest;
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
		C->FrameDir = FPaths::ConvertRelativePathToFull(FPaths::ProjectDir() / TEXT("../Reports/batch17/inv_arrow"));
		FTimerHandle H;
		W->GetTimerManager().SetTimer(H, FTimerDelegate::CreateLambda([C]()
		{
			if (C->W.IsValid() && AsdaSelfTestKit::RefuseDeferred(C->W.Get(), kCmd, C->bQuit)) { return; }
			Run(C);
		}), FMath::Max(0.01f, KA.Delay), false);
	}));

#endif // ASDA_DEV_CONSOLE && !UE_BUILD_SHIPPING
