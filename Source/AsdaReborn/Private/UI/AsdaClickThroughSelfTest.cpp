// Copyright (c) 2026 @sitxovski (Discord)
// ★ САМОТЕСТ «КЛИК СКВОЗЬ ОКНО» (31.10.2026, волна hud_click, жалоба владельца: «Купить» в окне покупки магазина бустеров
//   ушло сквозь окно; «у нас много окон, сквозь которые проходят клики»): `asda.UI.SelfTestClickThrough [cat] [quit]`.
//   Офлайн (`-AsdaNoNet`), путём игрока: решение «клик в окно или в мир» — тот же шлюз, что у контроллера
//   (`IsScreenPointOverAnyUiWindowEx`), «какое окно его получает» — тот же хит-тест, что у подъёма окон (`FindWindowCatchingAt`),
//   клики — настоящими событиями Slate (`SimulateClickAt`), геометрия — `GetCachedGeometry`.
//   A. Магазин бустеров 15300 (NPC Пре-Пре): ячейка 0 → панель покупки 15515 → настоящий клик по «Купить» 15527 → окно
//      подтверждения 1559 (SHELLITEM_PURCHASE_BOX) → сетка по окну 1559 (каждая нарисованная точка ловится им) → настоящий клик
//      по полю магазина ПОД 1559 (модалка обязана его съесть: страница магазина не меняется, мир не получает) → настоящий клик
//      по «OK» 1561: уходит 5070 (перехват, в сеть не уходит), окно закрыто, мир не тронут (павн на месте, маршрута нет).
//      Порядок: «OK» 1561 сначала проверяется шлюзом, затем 1559 закрывается «Отменой» 1562 (покупки нет — иначе магазин ждал бы
//      ответа 5071, которого офлайн нет). A9–A11 — сценарий жалобы: настоящий клик «Вид» 15547 (окно 15600 создаётся ПОСЛЕ 1559),
//      снова «Купить» → 1559 обязано встать над всеми окнами (z выше «Вид» и магазина) и само ловить каждую свою нарисованную точку;
//      настоящий клик «OK» — покупка 5070.
//   B. Каталог (аргумент `cat`): каждое окно реестра и каждое окно .JUI с ассетом WBP открывается (как `asda.UI.SelfTestRaiseClick`)
//      и по сетке 8 px над тем, что окно РИСУЕТ (видимая цепочка, картинка/рамка/кнопка с ненулевой альфой, непустой текст),
//      проверяется: точку ловит само окно или окно выше. Иначе — «сквозь»: в мир (шлюз «мир») или в окно ниже. Причина — по
//      верхнему нарисованному узлу точки: NoSelect (`NoHitWidgets`), только текст (подпись без плана), вне клипа, прочее.
//      Плюс окна вне реестра: верхние `UUserWidget` во вьюпорте, которых реестр не знает (шлюз их не видит вовсе).
//   Итог: «СКВОЗЬ-ОКНО ИТОГ: ok=N fail=M».
#include "CoreMinimal.h"
#include "AsdaDevConsole.h"
#include "Test/AsdaSelfTestCtx.h"   // ★ R5-c (12.11.2026): общая точка самотестов вместо своих копий помощников

#if ASDA_DEV_CONSOLE && !UE_BUILD_SHIPPING

#include "AsdaNetSubsystem.h"
#include "Test/AsdaSelfTestKit.h"
#include "UI/AsdaWindowSubsystem.h"
#include "UI/AsdaJuiWindowMeta.h"
#include "UI/AsdaUiHelpers.h"
#include "AsdaWindowSubsystemInternal.h"   // AsdaIsNoSelectRootWindow — окна-оверлеи (общий список реестра)
#include "UI/AsdaBoosterShopSubsystem.h"
#include "UI/AsdaPurchaseBoxSubsystem.h"
#include "UI/AsdaItemTooltipSubsystem.h"
#include "World/AsdaPlayerController.h"
#include "AssetRegistry/AssetRegistryModule.h"
#include "AsdaUiWbpCatalog.h"   // ★ R5-fix-b 13.11.2026 (третий заход): каталог WBP окон (общая точка)
#include "Blueprint/UserWidget.h"
#include "Blueprint/WidgetBlueprintLibrary.h"
#include "Blueprint/WidgetTree.h"
#include "Components/Border.h"
#include "Components/Button.h"
#include "Components/CanvasPanel.h"
#include "Components/Image.h"
#include "Components/PanelWidget.h"
#include "Components/RichTextBlock.h"
#include "Components/ScrollBox.h"
#include "Components/TextBlock.h"
#include "Engine/Engine.h"
#include "Engine/GameInstance.h"
#include "Engine/World.h"
#include "Framework/Application/SlateApplication.h"
#include "GameFramework/Pawn.h"
#include "GameFramework/PlayerController.h"
#include "HAL/IConsoleManager.h"
#include "TimerManager.h"

DEFINE_LOG_CATEGORY_STATIC(LogAsdaClickThroughTest, Log, All);

namespace AsdaClickThroughTest
{
	static const TCHAR* kCmd = TEXT("asda.UI.SelfTestClickThrough");   // имя команды
	static const FName kShop(TEXT("BOOSTER_SHOP_WIN"));                // магазин бустеров 15300
	static const FName kBox(TEXT("SHELLITEM_PURCHASE_BOX"));           // окно подтверждения покупки 1559

	// Нарисованный узел окна: прямоугольник, имя, причина «почему не ловит» заранее (NoSelect / текст / клип).
	struct FDrawn
	{
		FVector2D Mn, Mx;   // абсолютный прямоугольник (обрезан клипом предков)
		FName Name;         // имя узла
		FString Kind;       // класс узла
		bool bNoHit = false;   // в списке NoSelect окна
		bool bText = false;    // текстовый узел
		bool bClipped = false; // узел частично вне клипующего предка (прямоугольник уже обрезан)
	};

	// Контекст прогона (живёт между таймерами).
	struct FCtx
	{
		int32 Ok = 0, Fail = 0;                 // пройдено / провалено
		TWeakObjectPtr<UWorld> W;               // мир прогона
		bool bQuit = false;                     // выйти после итога
		bool bCatalog = false;                  // раздел B
		uint32 CapGen = 0;                      // поколение перехвата исходящих
		TSharedPtr<TArray<int32>> Sent;         // перехваченные опкоды
		FVector PawnAt = FVector::ZeroVector;   // позиция павна до кликов
		FString ShopPage;                       // строка страницы магазина до клика под модалкой
		// Каталог.
		TArray<TPair<FName, FString>> Items;    // окно → путь WBP (пусто — уже в реестре)
		int32 Idx = -1;                         // текущее окно каталога
		bool bOpenedByUs = false;               // текущее окно открыто самотестом
		ESlateVisibility VisWas = ESlateVisibility::Collapsed;   // прежняя видимость (окно реестра, скрытое подсистемой)
		bool bVisForced = false;                // видимость включена самотестом
		int32 ZWas = 0;                         // прежний z окна реестра
		int32 WinChecked = 0, WinBad = 0, WinSkipped = 0;        // счётчики каталога
		int32 WinOverlay = 0;                   // окон-оверлеев (NoSelect у корня) — сквозные по эталону
		TMap<FString, int32> ByReason;          // причина → окон
		void Expect(bool b, const FString& What)
		{
			if (b) { ++Ok;   UE_LOG(LogAsdaClickThroughTest, Log,   TEXT("СКВОЗЬ-ОКНО ok   %s"), *What); }
			else   { ++Fail; UE_LOG(LogAsdaClickThroughTest, Error, TEXT("СКВОЗЬ-ОКНО FAIL %s"), *What); }
		}
	};

	using AsdaSelfTestCtx::GIOf;   // ★ R5-c (12.11.2026): общая точка (`Test/AsdaSelfTestCtx.h`) — копия была дословной.
	using AsdaSelfTestCtx::Sub;   // ★ R5-c (12.11.2026): общая точка (`Test/AsdaSelfTestCtx.h`) — копия была дословной.

	// Через Sec секунд от текущего момента.
	static void After(const TSharedRef<FCtx>& C, float Sec, TFunction<void()> Fn)
	{
		UWorld* W = C->W.Get();
		if (!W) { return; }
		FTimerHandle H;
		W->GetTimerManager().SetTimer(H, FTimerDelegate::CreateLambda(MoveTemp(Fn)), FMath::Max(0.01f, Sec), false);
	}

	// Видна ли вся цепочка узла до корня окна.
	static bool ChainVisible(const UWidget* W)
	{
		for (const UWidget* P = W; P; P = P->GetParent())
		{
			const ESlateVisibility V = P->GetVisibility();
			if (V == ESlateVisibility::Collapsed || V == ESlateVisibility::Hidden) { return false; }
			if (P->GetRenderOpacity() < 0.02f) { return false; }
		}
		return true;
	}

	// Рисует ли узел что-нибудь видимое (картинка/рамка/кнопка с ненулевой альфой, непустой текст).
	static bool DrawsSomething(const UWidget* W, bool& bText)
	{
		bText = false;
		if (const UImage* Im = Cast<UImage>(W))
		{
			const FSlateBrush& B = Im->GetBrush();
			if (B.DrawAs == ESlateBrushDrawType::NoDrawType) { return false; }
			return B.GetResourceObject() && B.TintColor.GetSpecifiedColor().A > 0.02f && Im->GetColorAndOpacity().A > 0.02f;
		}
		if (const UBorder* Bd = Cast<UBorder>(W))
		{
			return Bd->Background.DrawAs != ESlateBrushDrawType::NoDrawType && Bd->Background.GetResourceObject()
				&& Bd->GetBrushColor().A > 0.02f;
		}
		if (const UButton* Bt = Cast<UButton>(W))
		{
			const FSlateBrush& N = Bt->GetStyle().Normal;
			return N.DrawAs != ESlateBrushDrawType::NoDrawType && N.GetResourceObject() && N.TintColor.GetSpecifiedColor().A > 0.02f;
		}
		if (const UTextBlock* Tb = Cast<UTextBlock>(W)) { bText = true; return !Tb->GetText().IsEmpty(); }
		if (const URichTextBlock* Rb = Cast<URichTextBlock>(W)) { bText = true; return !Rb->GetText().IsEmpty(); }
		return false;
	}

	// Нарисованные узлы окна (обход дерева; прямоугольник обрезан клипующими предками).
	static void CollectDrawn(UWidget* W, const TSet<FName>& NoHit, FVector2D ClipMn, FVector2D ClipMx, TArray<FDrawn>& Out)
	{
		if (!W) { return; }
		const ESlateVisibility V = W->GetVisibility();
		if (V == ESlateVisibility::Collapsed || V == ESlateVisibility::Hidden || W->GetRenderOpacity() < 0.02f) { return; }
		FVector2D Mn, Mx;
		const bool bR = AsdaUi::WidgetAbsRect(W, Mn, Mx);
		if (UPanelWidget* P = Cast<UPanelWidget>(W))
		{
			FVector2D CMn = ClipMn, CMx = ClipMx;
			if (bR && (W->GetClipping() != EWidgetClipping::Inherit || W->IsA<UScrollBox>()))
			{
				CMn = FVector2D::Max(CMn, Mn); CMx = FVector2D::Min(CMx, Mx);
			}
			for (int32 i = 0; i < P->GetChildrenCount(); ++i) { CollectDrawn(P->GetChildAt(i), NoHit, CMn, CMx, Out); }
		}
		bool bText = false;
		if (!bR || !DrawsSomething(W, bText)) { return; }
		FDrawn D;
		D.Mn = FVector2D::Max(Mn, ClipMn); D.Mx = FVector2D::Min(Mx, ClipMx);
		if (D.Mn.X >= D.Mx.X || D.Mn.Y >= D.Mx.Y) { return; }
		D.bClipped = !(D.Mn.Equals(Mn) && D.Mx.Equals(Mx));
		D.Name = W->GetFName(); D.Kind = W->GetClass()->GetName(); D.bNoHit = NoHit.Contains(D.Name); D.bText = bText;
		Out.Add(D);
	}

	// Итог сетки одного окна: нарисованных точек, сквозь (в мир / в окно ниже), причины по узлам.
	struct FGridRes
	{
		int32 Drawn = 0, ToWorld = 0, ToLower = 0;   // нарисованных точек / ушло в мир / ушло в окно ниже
		TMap<FString, int32> Why;                    // «узел (класс): причина» → точек
		FString Sample;                              // первая точка «сквозь»
	};
	static FGridRes GridWindow(UAsdaWindowSubsystem* WM, FName Id, float Step, bool bStrict = false)
	{
		FGridRes R;
		UUserWidget* Wd = WM->FindWindow(Id);
		if (!Wd || !Wd->GetRootWidget()) { return R; }
		TSet<FName> NoHit;   // NoSelect окна — тот же список, что реестр берёт из меты .JUI (`AttachJuiMeta`)
		if (const UAsdaJuiWindowMetaAsset* Meta = WM->GetJuiMeta())
		{
			if (const FAsdaJuiWindowMeta* M = Meta->FindWindow(Id)) { NoHit.Append(M->NoHitWidgets); }
		}
		TArray<FDrawn> Dr;
		const double Big = 1.0e7;
		CollectDrawn(Wd->GetRootWidget(), NoHit, FVector2D(-Big, -Big), FVector2D(Big, Big), Dr);
		if (Dr.Num() == 0) { return R; }
		FBox2D Box(ForceInit);
		for (const FDrawn& D : Dr) { Box += D.Mn; Box += D.Mx; }
		const int32 MyZ = WM->GetWindowZOrder(Id);
		for (double Y = Box.Min.Y + Step * 0.5; Y < Box.Max.Y; Y += Step)
		{
			for (double X = Box.Min.X + Step * 0.5; X < Box.Max.X; X += Step)
			{
				const FVector2D P(X, Y);
				const FDrawn* Top = nullptr;   // последний в обходе — верхний по отрисовке
				for (const FDrawn& D : Dr) { if (P.X >= D.Mn.X && P.X <= D.Mx.X && P.Y >= D.Mn.Y && P.Y <= D.Mx.Y) { Top = &D; } }
				if (!Top) { continue; }
				++R.Drawn;
				const FName By = WM->FindWindowCatchingAt(P);
				// Окно выше по z закрывает точку законно; строгий режим (окно подтверждения) — точку обязано ловить само окно.
				if (By == Id || (!bStrict && !By.IsNone() && WM->GetWindowZOrder(By) >= MyZ)) { continue; }
				FName Z, Wg;
				const bool bUi = WM->IsScreenPointOverAnyUiWindowEx(P, Z, Wg);
				if (!bUi) { ++R.ToWorld; } else { ++R.ToLower; }
				// Причина по нарисованным узлам точки (любой из них ловит — значит, дело не в нём).
				FString Why;
				bool bAllNoHit = true, bAllText = true, bAnyClip = false;
				for (const FDrawn& D : Dr)
				{
					if (!(P.X >= D.Mn.X && P.X <= D.Mx.X && P.Y >= D.Mn.Y && P.Y <= D.Mx.Y)) { continue; }
					bAllNoHit &= D.bNoHit; bAllText &= (D.bText || D.bNoHit); bAnyClip |= D.bClipped;
				}
				if (bAllNoHit) { Why = TEXT("NoSelect (NoHitWidgets окна)"); }
				else if (bAllText) { Why = TEXT("только текст — подпись без плана"); }
				else if (!ChainVisible(Wd->GetWidgetFromName(Top->Name))) { Why = TEXT("скрытый предок"); }
				else { Why = FString::Printf(TEXT("прочее (поймал %s)"), By.IsNone() ? (bUi ? *Z.ToString() : TEXT("мир")) : *By.ToString()); }
				if (bAnyClip) { Why += TEXT(", узел обрезан клипом"); }
				R.Why.FindOrAdd(FString::Printf(TEXT("%s (%s): %s → %s"), *Top->Name.ToString(), *Top->Kind, *Why,
					bUi ? *FString::Printf(TEXT("окно ниже «%s»"), *Z.ToString()) : TEXT("мир"))) += 1;
				if (R.Sample.IsEmpty()) { R.Sample = FString::Printf(TEXT("(%.0f,%.0f)"), P.X, P.Y); }
			}
		}
		return R;
	}

	static FString WhyText(const FGridRes& R)
	{
		FString S;
		for (const TPair<FString, int32>& K : R.Why) { S += FString::Printf(TEXT("\n      %4d × %s"), K.Value, *K.Key); }
		return S;
	}

	// Центр узла окна.
	static bool NodeCenter(UAsdaWindowSubsystem* WM, FName Win, const TCHAR* Node, FVector2D& Out)
	{
		UUserWidget* W = WM ? WM->FindWindow(Win) : nullptr;
		UWidget* N = W ? W->GetWidgetFromName(FName(Node)) : nullptr;
		FVector2D Mn, Mx;
		if (!N || !AsdaUi::WidgetAbsRect(N, Mn, Mx)) { return false; }
		Out = (Mn + Mx) * 0.5;
		return true;
	}

	// Мир не тронут: павн на месте и маршрута нет.
	static bool WorldUntouched(const TSharedRef<FCtx>& C, FString& Out)
	{
		AAsdaPlayerController* PC = C->W.IsValid() ? Cast<AAsdaPlayerController>(C->W->GetFirstPlayerController()) : nullptr;
		const bool bMoved = PC && PC->GetPawn() && FVector::Dist2D(PC->GetPawn()->GetActorLocation(), C->PawnAt) > 1.0;
		const bool bRoute = PC && PC->HasRoute();
		Out = FString::Printf(TEXT("павн сдвинулся=%d, маршрут=%d"), bMoved ? 1 : 0, bRoute ? 1 : 0);
		return !bMoved && !bRoute;
	}

	static void StartCatalog(const TSharedRef<FCtx>& C);
	static void Finish(const TSharedRef<FCtx>& C);
	static void RunBooster(const TSharedRef<FCtx>& C);

	// ★ R5-fix8-b 14.11.2026 (третья запись, добавка координатора: ClickThrough): исходное состояние ставит сам тест — павн стоит. Ход, оставленный прежним тестом пачки (щелчок по миру), гасится,
	//   и снимок «позиция до кликов» берётся только после СОБЫТИЯ «не движется» (`IsMovingNow`: нет маршрута и локального хода);
	//   опрос по кадру, потолок 3 с (не дождались — идём дальше, проверки «мир не тронут» тогда честно красные).
	static void WaitStill(const TSharedRef<FCtx>& C, double Until)
	{
		UWorld* W = C->W.Get();
		if (!W) { return; }
		AAsdaPlayerController* PC = Cast<AAsdaPlayerController>(W->GetFirstPlayerController());
		const double Now = FPlatformTime::Seconds();
		if (PC && PC->IsMovingNow() && Now < Until)
		{
			W->GetTimerManager().SetTimerForNextTick(FTimerDelegate::CreateLambda([C, Until]() { WaitStill(C, Until); }));
			return;
		}
		if (PC && PC->IsMovingNow()) { UE_LOG(LogAsdaClickThroughTest, Warning, TEXT("[сквозь-окно] павн не остановился за 3 с — снимок на бегу")); }
		RunBooster(C);
	}
	static void StartStill(const TSharedRef<FCtx>& C)
	{
		AAsdaPlayerController* PC = C->W.IsValid() ? Cast<AAsdaPlayerController>(C->W->GetFirstPlayerController()) : nullptr;
		if (PC && PC->IsMovingNow())
		{
			UE_LOG(LogAsdaClickThroughTest, Log, TEXT("[сквозь-окно] павн в движении на старте (маршрут=%d) — остановка до снимка позиции"), PC->HasRoute() ? 1 : 0);
			PC->StopMoveAndNotify(TEXT("самотест сквозь-окно: исходное состояние"));
		}
		WaitStill(C, FPlatformTime::Seconds() + 3.0);
	}

	// ═══ A. Магазин бустеров → «Купить» → 1559 ═══
	static void RunBooster(const TSharedRef<FCtx>& C)
	{
		UAsdaWindowSubsystem* WM = Sub<UAsdaWindowSubsystem>(C);
		UAsdaBoosterShopSubsystem* Bs = Sub<UAsdaBoosterShopSubsystem>(C);
		UAsdaNetSubsystem* Net = Sub<UAsdaNetSubsystem>(C);
		if (!WM || !Bs || !Net) { C->Expect(false, TEXT("A: подсистемы окон/магазина бустеров/сети есть")); StartCatalog(C); return; }
		C->Sent = MakeShared<TArray<int32>>();
		TSharedPtr<TArray<int32>> S = C->Sent;
		C->CapGen = AsdaSelfTestKit::ArmCapture(Net, [S](uint16 Op, const uint8*, int32, bool, TCHAR) { S->Add((int32)Op); }, 0, kCmd);
		if (!C->CapGen) { C->Expect(false, TEXT("A: перехват исходящих взведён")); StartCatalog(C); return; }
		if (const APlayerController* PC = C->W->GetFirstPlayerController()) { if (PC->GetPawn()) { C->PawnAt = PC->GetPawn()->GetActorLocation(); } }
		Bs->OpenWindow();
		After(C, 1.0f, [C]()
		{
			UAsdaBoosterShopSubsystem* Bs = Sub<UAsdaBoosterShopSubsystem>(C);
			UAsdaWindowSubsystem* WM = Sub<UAsdaWindowSubsystem>(C);
			C->Expect(Bs->IsOpen(), TEXT("A1: магазин бустеров 15300 открыт"));
			Bs->OnCellBuyClicked(0);
			After(C, 0.5f, [C]()
			{
				UAsdaBoosterShopSubsystem* Bs = Sub<UAsdaBoosterShopSubsystem>(C);
				UAsdaWindowSubsystem* WM = Sub<UAsdaWindowSubsystem>(C);
				C->Expect(Bs->IsPurPanelShown(), TEXT("A2: панель покупки 15515 показана (кнопка «Купить» ячейки 0)"));
				const FGridRes G = GridWindow(WM, kShop, 8.f);
				UE_LOG(LogAsdaClickThroughTest, Log, TEXT("[сквозь-окно] A2 сетка магазина 15300 с панелью: нарисовано %d, в мир %d, в окно ниже %d%s"),
					G.Drawn, G.ToWorld, G.ToLower, *WhyText(G));
				C->Expect(G.Drawn > 0 && G.ToWorld + G.ToLower == 0, FString::Printf(TEXT("A2: магазин с панелью ловит каждую нарисованную точку — сквозь %d из %d %s"),
					G.ToWorld + G.ToLower, G.Drawn, *G.Sample));
				FVector2D P;
				if (!NodeCenter(WM, kShop, TEXT("BOOSTER_PUR_OKBTN"), P)) { C->Expect(false, TEXT("A3: у «Купить» 15527 есть геометрия")); StartCatalog(C); return; }
				FName Z, Wg;
				WM->IsScreenPointOverAnyUiWindowEx(P, Z, Wg);
				C->Expect(Z == kShop && WM->FindWindowCatchingAt(P) == kShop, FString::Printf(TEXT("A3: «Купить» (%.0f,%.0f) — шлюз «%s/%s», ловит «%s»"),
					P.X, P.Y, *Z.ToString(), *Wg.ToString(), *WM->FindWindowCatchingAt(P).ToString()));
				WM->SimulateClickAt(P, /*HoldFrames*/ 3);
				After(C, 0.6f, [C]()
				{
					UAsdaWindowSubsystem* WM = Sub<UAsdaWindowSubsystem>(C);
					UAsdaPurchaseBoxSubsystem* Box = Sub<UAsdaPurchaseBoxSubsystem>(C);
					UAsdaBoosterShopSubsystem* Bs = Sub<UAsdaBoosterShopSubsystem>(C);
					FString Wt;
					const bool bWorld = WorldUntouched(C, Wt);
					C->Expect(Box && Box->IsOpen() && bWorld, FString::Printf(TEXT("A4: настоящий клик «Купить» → окно подтверждения 1559 открыто=%d; мир не тронут (%s)"),
						(Box && Box->IsOpen()) ? 1 : 0, *Wt));
					const FGridRes G = GridWindow(WM, kBox, 6.f);
					UE_LOG(LogAsdaClickThroughTest, Log, TEXT("[сквозь-окно] A5 сетка 1559: z=%d (магазин %d), нарисовано %d, в мир %d, в окно ниже %d%s"),
						WM->GetWindowZOrder(kBox), WM->GetWindowZOrder(kShop), G.Drawn, G.ToWorld, G.ToLower, *WhyText(G));
					C->Expect(G.Drawn > 0 && G.ToWorld + G.ToLower == 0 && WM->GetWindowZOrder(kBox) > WM->GetWindowZOrder(kShop),
						FString::Printf(TEXT("A5: окно 1559 выше магазина (z %d > %d) и ловит каждую нарисованную точку — сквозь %d из %d %s"),
							WM->GetWindowZOrder(kBox), WM->GetWindowZOrder(kShop), G.ToWorld + G.ToLower, G.Drawn, *G.Sample));
					// A6: клик по кнопке магазина ПОД модалкой («следующая страница» вне 1559) — модалка обязана его съесть.
					C->ShopPage = Bs->DumpState().Left(80);
					FVector2D Q;
					const bool bQ = NodeCenter(WM, kShop, TEXT("BOOSTER_SHOP_NEXTP"), Q) || NodeCenter(WM, kShop, TEXT("BOOSTER_SHOP_CLOSE"), Q);
					if (bQ) { WM->SimulateClickAt(Q, 3); }
					After(C, 0.5f, [C, bQ, Q]()
					{
						UAsdaWindowSubsystem* WM = Sub<UAsdaWindowSubsystem>(C);
						UAsdaPurchaseBoxSubsystem* Box = Sub<UAsdaPurchaseBoxSubsystem>(C);
						UAsdaBoosterShopSubsystem* Bs = Sub<UAsdaBoosterShopSubsystem>(C);
						FString Wt;
						const bool bWorld = WorldUntouched(C, Wt);
						C->Expect(!bQ || (Bs->IsOpen() && Bs->DumpState().Left(80) == C->ShopPage && Box->IsOpen() && bWorld),
							FString::Printf(TEXT("A6: клик по кнопке магазина под модалкой (%.0f,%.0f) съеден: магазин открыт=%d, состояние то же=%d, 1559 открыто=%d, мир (%s)%s"),
								Q.X, Q.Y, Bs->IsOpen() ? 1 : 0, Bs->DumpState().Left(80) == C->ShopPage ? 1 : 0, Box->IsOpen() ? 1 : 0, *Wt,
								bQ ? TEXT("") : TEXT(" — кнопки страницы нет, шаг пропущен")));
						FVector2D P;
						if (!NodeCenter(WM, kBox, TEXT("PURCHASE_BTN01"), P)) { C->Expect(false, TEXT("A7: у «OK» 1561 есть геометрия")); StartCatalog(C); return; }
						FName Z, Wg;
						WM->IsScreenPointOverAnyUiWindowEx(P, Z, Wg);
						C->Expect(Z == kBox && WM->FindWindowCatchingAt(P) == kBox, FString::Printf(TEXT("A7: «OK» 1561 (%.0f,%.0f) — шлюз «%s/%s», ловит «%s»"),
							P.X, P.Y, *Z.ToString(), *Wg.ToString(), *WM->FindWindowCatchingAt(P).ToString()));
						FVector2D Pc;
						if (!NodeCenter(WM, kBox, TEXT("PURCHASE_BTN02"), Pc)) { C->Expect(false, TEXT("A8: у «Отмена» 1562 есть геометрия")); StartCatalog(C); return; }
						C->Sent->Reset();
						WM->SimulateClickAt(Pc, 3);
						After(C, 0.6f, [C]()
						{
							UAsdaPurchaseBoxSubsystem* Box = Sub<UAsdaPurchaseBoxSubsystem>(C);
							UAsdaBoosterShopSubsystem* Bs = Sub<UAsdaBoosterShopSubsystem>(C);
							FString Wt;
							const bool bWorld = WorldUntouched(C, Wt);
							C->Expect(!C->Sent->Contains(5070) && !Box->IsOpen() && Bs->IsOpen() && Bs->IsPurPanelShown() && bWorld,
								FString::Printf(TEXT("A8: настоящий клик «Отмена» 1562 → 1559 закрыто=%d, покупки нет=%d, магазин и панель на месте=%d, мир не тронут (%s)"),
									Box->IsOpen() ? 0 : 1, C->Sent->Contains(5070) ? 0 : 1, (Bs->IsOpen() && Bs->IsPurPanelShown()) ? 1 : 0, *Wt));
							// A9: «Вид» 15547 настоящим кликом — окно 15600 создаётся после 1559.
							UAsdaWindowSubsystem* WM = Sub<UAsdaWindowSubsystem>(C);
							FVector2D V;
							if (!NodeCenter(WM, kShop, TEXT("BOOSTER_PUR_PREV"), V)) { C->Expect(false, TEXT("A9: у «Вид» 15547 есть геометрия")); Bs->CloseWindow(); StartCatalog(C); return; }
							WM->SimulateClickAt(V, 3);
							After(C, 0.6f, [C]()
							{
								UAsdaWindowSubsystem* WM = Sub<UAsdaWindowSubsystem>(C);
								const FName kPrev(TEXT("BOOSTER_PREV_WIN"));
								C->Expect(WM->IsWindowVisible(kPrev), FString::Printf(TEXT("A9: «Вид» → окно 15600 показано (z=%d, 1559 z=%d)"),
									WM->GetWindowZOrder(kPrev), WM->GetWindowZOrder(kBox)));
								FVector2D P;
								if (!NodeCenter(WM, kShop, TEXT("BOOSTER_PUR_OKBTN"), P)) { C->Expect(false, TEXT("A10: у «Купить» есть геометрия")); StartCatalog(C); return; }
								WM->SimulateClickAt(P, 3);
								After(C, 0.6f, [C]()
								{
									UAsdaWindowSubsystem* WM = Sub<UAsdaWindowSubsystem>(C);
									UAsdaPurchaseBoxSubsystem* Box = Sub<UAsdaPurchaseBoxSubsystem>(C);
									const FName kPrev(TEXT("BOOSTER_PREV_WIN"));
									const int32 Zb = WM->GetWindowZOrder(kBox);
									int32 ZTop = -1; FName TopId;
									for (const FName& Id : WM->GetRegisteredWindowIds())
									{
										if (Id == kBox || !WM->IsWindowVisible(Id)) { continue; }
										const int32 Z = WM->GetWindowZOrder(Id);
										if (Z < UAsdaWindowSubsystem::kTopmostZ && Z > ZTop) { ZTop = Z; TopId = Id; }
									}
									const FGridRes G = GridWindow(WM, kBox, 6.f, /*bStrict*/ true);
									UE_LOG(LogAsdaClickThroughTest, Log, TEXT("[сквозь-окно] A10 сетка 1559 после «Вид»: z=%d («Вид» %d, магазин %d), нарисовано %d, ловит не само окно %d%s"),
										Zb, WM->GetWindowZOrder(kPrev), WM->GetWindowZOrder(kShop), G.Drawn, G.ToWorld + G.ToLower, *WhyText(G));
									C->Expect(Box && Box->IsOpen() && Zb > ZTop && G.Drawn > 0 && G.ToWorld + G.ToLower == 0,
										FString::Printf(TEXT("A10: снова «Купить» после «Вид» → 1559 открыто=%d, z %d выше всех окон (верхнее прочее «%s» z %d), "
											"каждую свою точку ловит само — чужих %d из %d %s [ИСПРАВЛЕНИЕ asda.UI.PriorityBandRaise]"),
											(Box && Box->IsOpen()) ? 1 : 0, Zb, *TopId.ToString(), ZTop, G.ToWorld + G.ToLower, G.Drawn, *G.Sample));
									FVector2D P;
									if (!NodeCenter(WM, kBox, TEXT("PURCHASE_BTN01"), P)) { C->Expect(false, TEXT("A11: у «OK» 1561 есть геометрия")); StartCatalog(C); return; }
									const FName By = WM->FindWindowCatchingAt(P);
									C->Sent->Reset();
									WM->SimulateClickAt(P, 3);
									After(C, 0.6f, [C, By]()
									{
										UAsdaPurchaseBoxSubsystem* Box = Sub<UAsdaPurchaseBoxSubsystem>(C);
										UAsdaBoosterShopSubsystem* Bs = Sub<UAsdaBoosterShopSubsystem>(C);
										FString Wt;
										const bool bWorld = WorldUntouched(C, Wt);
										C->Expect(By == kBox && C->Sent->Contains(5070) && !Box->IsOpen() && bWorld,
											FString::Printf(TEXT("A11: «OK» 1561 ловит «%s»; настоящий клик → покупка 5070 ушла=%d, 1559 закрыто=%d, мир не тронут (%s)"),
												*By.ToString(), C->Sent->Contains(5070) ? 1 : 0, Box->IsOpen() ? 0 : 1, *Wt));
										if (UAsdaWindowSubsystem* WM = Sub<UAsdaWindowSubsystem>(C)) { WM->SetWindowVisible(FName(TEXT("BOOSTER_PREV_WIN")), false); }
										Bs->CloseWindow();
										StartCatalog(C);
									});
								});
							});
						});
					});
				});
			});
		});
	}

	// ═══ B. Каталог окон ═══
	static void CatalogStep(const TSharedRef<FCtx>& C);
	static void StartCatalog(const TSharedRef<FCtx>& C)
	{
		if (!C->bCatalog) { Finish(C); return; }
		UAsdaWindowSubsystem* WM = Sub<UAsdaWindowSubsystem>(C);
		if (!WM) { Finish(C); return; }
		// Окна вне реестра: верхние виджеты вьюпорта, которых реестр не знает.
		{
			TArray<UUserWidget*> Top;
			UWidgetBlueprintLibrary::GetAllWidgetsOfClass(C->W.Get(), Top, UUserWidget::StaticClass(), /*TopLevelOnly*/ true);
			TSet<const UUserWidget*> Reg;
			for (const FName& Id : WM->GetRegisteredWindowIds()) { if (UUserWidget* Wd = WM->FindWindow(Id)) { Reg.Add(Wd); } }
			int32 N = 0;
			for (UUserWidget* Wd : Top)
			{
				if (!Wd || Reg.Contains(Wd) || !Wd->IsInViewport() || !ChainVisible(Wd)) { continue; }
				TArray<FDrawn> Dr;
				const double Big = 1.0e7;
				CollectDrawn(Wd->GetRootWidget(), TSet<FName>(), FVector2D(-Big, -Big), FVector2D(Big, Big), Dr);
				FBox2D Box(ForceInit);
				for (const FDrawn& D : Dr) { Box += D.Mn; Box += D.Mx; }
				UE_LOG(LogAsdaClickThroughTest, Log, TEXT("[сквозь-окно] ВНЕ РЕЕСТРА: %s (%s) нарисованных узлов %d, габарит (%.0f,%.0f)-(%.0f,%.0f), hit-test корня %s"),
					*Wd->GetName(), *Wd->GetClass()->GetName(), Dr.Num(), Box.Min.X, Box.Min.Y, Box.Max.X, Box.Max.Y,
					*StaticEnum<ESlateVisibility>()->GetNameStringByValue((int64)Wd->GetVisibility()));
				if (Dr.Num() > 0) { ++N; }
			}
			UE_LOG(LogAsdaClickThroughTest, Log, TEXT("[сквозь-окно] окон вне реестра с видимым рисунком: %d"), N);
		}
		// Каталог: окна реестра + окна .JUI с WBP.
		TMap<FName, FString> Wbp;
		AsdaUiWbpCatalog::Collect(Wbp);   // ★ R5-fix-b 13.11.2026 (третий заход): общая точка трёх копий; класс `WBP_<id>_C` собранной игры — под id `<id>`
		TSet<FName> Seen;
		for (const FName& Id : WM->GetRegisteredWindowIds()) { Seen.Add(Id); C->Items.Emplace(Id, FString()); }
		if (const UAsdaJuiWindowMetaAsset* Meta = WM->GetJuiMeta())
		{
			for (const FAsdaJuiWindowMeta& M : Meta->Windows)
			{
				const FString* P = Wbp.Find(M.WindowId);
				if (!P || Seen.Contains(M.WindowId)) { continue; }
				Seen.Add(M.WindowId);
				C->Items.Emplace(M.WindowId, *P);
			}
		}
		UE_LOG(LogAsdaClickThroughTest, Log, TEXT("[сквозь-окно] каталог: окон %d"), C->Items.Num());
		C->Idx = -1;
		After(C, 0.1f, [C]() { CatalogStep(C); });
	}

	// Открыть следующее окно каталога, через кадры — сетка, затем вернуть.
	static void CatalogStep(const TSharedRef<FCtx>& C)
	{
		UAsdaWindowSubsystem* WM = Sub<UAsdaWindowSubsystem>(C);
		if (!WM) { Finish(C); return; }
		if (++C->Idx >= C->Items.Num())
		{
			FString R;
			for (const TPair<FString, int32>& K : C->ByReason) { R += FString::Printf(TEXT("\n    %4d × %s"), K.Value, *K.Key); }
			UE_LOG(LogAsdaClickThroughTest, Log, TEXT("[сквозь-окно] КАТАЛОГ: проверено %d (оверлеев %d), сквозь у %d, пропущено %d; причины по окнам:%s"),
				C->WinChecked, C->WinOverlay, C->WinBad, C->WinSkipped, *R);
			C->Expect(C->WinBad == 0, FString::Printf(TEXT("B: каждое окно ловит каждую свою нарисованную точку — окон со «сквозь» %d из %d"), C->WinBad, C->WinChecked));
			Finish(C);
			return;
		}
		WM->ClearMsgBoxQueue(TEXT("самотест сквозь-окно: следующее окно"));
		if (UAsdaItemTooltipSubsystem* Tip = Sub<UAsdaItemTooltipSubsystem>(C)) { Tip->HideTooltip(); }
		const FName Id = C->Items[C->Idx].Key;
		const FString Path = C->Items[C->Idx].Value;
		C->bOpenedByUs = false; C->bVisForced = false;
		if (UUserWidget* Wd = WM->FindWindow(Id))
		{
			C->ZWas = WM->GetWindowZOrder(Id);
			const ESlateVisibility V = Wd->GetVisibility();
			if (V == ESlateVisibility::Collapsed || V == ESlateVisibility::Hidden)
			{
				C->VisWas = V; C->bVisForced = true;
				Wd->SetVisibility(ESlateVisibility::SelfHitTestInvisible);
			}
		}
		else if (Path.IsEmpty() || !WM->OpenWindowByPath(Id, Path, 750, ESlateVisibility::SelfHitTestInvisible, false))
		{
			++C->WinSkipped;
			After(C, 0.02f, [C]() { CatalogStep(C); });
			return;
		}
		else { C->bOpenedByUs = true; C->ZWas = WM->GetWindowZOrder(Id); }
		After(C, 0.15f, [C, Id]()
		{
			UAsdaWindowSubsystem* WM = Sub<UAsdaWindowSubsystem>(C);
			if (!WM) { Finish(C); return; }
			const FGridRes G = GridWindow(WM, Id, 8.f);
			// Окно-оверлей с NoSelect у корня (нотис, баннер, метка, подсказка) — сквозное по эталону: не считаем.
			const bool bOverlay = AsdaIsNoSelectRootWindow(Id);
			++C->WinChecked;
			if (bOverlay)
			{
				++C->WinOverlay;
				UE_LOG(LogAsdaClickThroughTest, Log, TEXT("[сквозь-окно] окно «%s»: оверлей (NoSelect у корня) — сквозное [ОРИГИНАЛ], нарисовано %d, сквозь %d"),
					*Id.ToString(), G.Drawn, G.ToWorld + G.ToLower);
			}
			else if (G.ToWorld + G.ToLower > 0)
			{
				++C->WinBad;
				for (const TPair<FString, int32>& K : G.Why)
				{
					int32 Colon = INDEX_NONE;
					K.Key.FindChar(TEXT(':'), Colon);
					const FString Reason = Colon != INDEX_NONE ? K.Key.Mid(Colon + 2) : K.Key;
					int32 Arrow = Reason.Find(TEXT(" → "));
					C->ByReason.FindOrAdd(Arrow != INDEX_NONE ? Reason.Left(Arrow) : Reason) += 1;
				}
				UE_LOG(LogAsdaClickThroughTest, Warning, TEXT("[сквозь-окно] ОКНО «%s» z=%d: нарисовано %d, в мир %d, в окно ниже %d, пример %s%s"),
					*Id.ToString(), WM->GetWindowZOrder(Id), G.Drawn, G.ToWorld, G.ToLower, *G.Sample, *WhyText(G));
			}
			else
			{
				UE_LOG(LogAsdaClickThroughTest, Log, TEXT("[сквозь-окно] окно «%s» z=%d: нарисовано %d — все ловит"), *Id.ToString(), WM->GetWindowZOrder(Id), G.Drawn);
			}
			if (C->bOpenedByUs) { WM->CloseWindowSilently(Id); }
			else if (UUserWidget* Wd = WM->FindWindow(Id))
			{
				if (C->bVisForced) { Wd->SetVisibility(C->VisWas); }
				if (WM->GetWindowZOrder(Id) != C->ZWas) { WM->SetWindowZOrder(Id, C->ZWas); }
			}
			After(C, 0.03f, [C]() { CatalogStep(C); });
		});
	}

	static void Finish(const TSharedRef<FCtx>& C)
	{
		if (C->CapGen) { AsdaSelfTestKit::DisarmCapture(Sub<UAsdaNetSubsystem>(C), C->CapGen); C->CapGen = 0; }
		AsdaSelfTestKit::Finish(C->W.Get(), kCmd, C->bQuit, LogAsdaClickThroughTest, TEXT("СКВОЗЬ-ОКНО"), C->Ok, C->Fail);
	}
}

static FAutoConsoleCommandWithWorldAndArgs GAsdaSelfTestClickThrough(
	TEXT("asda.UI.SelfTestClickThrough"),
	TEXT("Самотест «клик сквозь окно»: магазин бустеров → «Купить» → окно 1559 → «OK» настоящими кликами Slate (покупка уходит, "
	     "клик не проходит в магазин и мир); `cat` — сетка по каждому окну каталога (нарисованная точка обязана ловиться окном). "
	     "Офлайн. `quit`."),
	FConsoleCommandWithWorldAndArgsDelegate::CreateLambda([](const TArray<FString>& Args, UWorld* W)
	{
		using namespace AsdaClickThroughTest;
		const FAsdaSelfTestArgs KA = FAsdaSelfTestArgs::Parse(Args);
		if (!KA.Has(TEXT("selfready")) && W && GEngine && AsdaSelfTestKit::NeedSelfSpawn(W))
		{
			GEngine->Exec(W, TEXT("Asda.Load.CurrentMap 3"));
			GEngine->Exec(W, TEXT("Asda.World.FakeSelfSpawn 256 256"));
			const FString Again = FString::Printf(TEXT("%s selfready%s%s"), kCmd, KA.Has(TEXT("cat")) ? TEXT(" cat") : TEXT(""), KA.bQuit ? TEXT(" quit") : TEXT(""));
			TWeakObjectPtr<UWorld> WW(W);
			FTimerHandle H;
			W->GetTimerManager().SetTimer(H, FTimerDelegate::CreateLambda([WW, Again]() { if (WW.IsValid() && GEngine) { GEngine->Exec(WW.Get(), *Again); } }), 2.f, false);
			return;
		}
		if (AsdaSelfTestKit::RefuseStart(W, kCmd, KA)) { return; }
		TSharedRef<FCtx> C = MakeShared<FCtx>();
		C->W = W;
		C->bQuit = KA.bQuit;
		C->bCatalog = KA.Has(TEXT("cat"));
		After(C, 1.0f, [C]() { StartStill(C); });   // ★ R5-fix8-b 14.11.2026 (третья запись, добавка координатора: ClickThrough): сначала павн стоит
	}));

#endif // ASDA_DEV_CONSOLE && !UE_BUILD_SHIPPING
