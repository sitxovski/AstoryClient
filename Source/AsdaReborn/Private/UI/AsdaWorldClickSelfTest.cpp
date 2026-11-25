// Copyright (c) 2026 @sitxovski (Discord)
// ★ САМОТЕСТ «КЛИК В МИР СКВОЗЬ HUD» (31.10.2026, волна hud_click): `asda.UI.SelfTestWorldClick [<карта>] [quit]`.
//   Жалоба игроков «над персонажем верх экрана не кликается». Офлайн (`-AsdaNoNet`), путём игрока (законы
//   ui-check-game-path-geometry, diagnostic-must-use-real-path): решение «клик в мир или в окно» спрашивается у ТОГО ЖЕ шлюза,
//   которым его спрашивает контроллер (`IsScreenPointOverAnyUiWindowEx` ≡ `IsCursorOverAnyUiWindow`), по живой геометрии
//   `GetCachedGeometry`; лист Slate под точкой — `LocateWindowUnderMouse` (кнопка Slate съела бы клик сама).
//   Сетка 64×48 по вьюпорту (16 px при 1024×768) в каждом состоянии — дважды: прежние правила (`asda.UI.ZoneRectInit 0`,
//   `asda.UI.HitTestPlanesOnly 0`) и новые. По каждой паре «окно/виджет», поймавшей ячейку, — класс, видимость, прямоугольник,
//   прозрачность, кисть/текст, цепочка видимости предков, «в вьюпорте».
//   Состояния: S1 типовой HUD карты (на 19 — HUD боя через 5055); S2 журналы чата полны длинных строк; S3 открыты инвентарь,
//   инфо персонажа, навыки, журнал квестов; S4 они закрыты; S5 Alt+Z (интерфейс скрыт); S6 Alt+Z обратно.
//   Проверки (новые правила):
//     A  полоса мира над персонажем (x 224…704, y 128…480 при 1024×768 — вне угловых окон HUD и карты боя 49900) в S1/S2/S4/S6
//        свободна;
//     B  окна HUD по-прежнему ловят свои клики (MINISTATE, MINIMAP, CHAT_NEW, CHAT_NEW_SYSTEM, QUICK, SYSTEMMAIN);
//     C  строки чата не расширяют зону окон чата (S2 ≤ S1 по ячейкам CHAT_*);
//     D  открытые окна ловят (S3: у инвентаря, инфо персонажа, журнала квестов есть ячейки; окно навыков офлайн отвечает
//        окном-сообщением «не меняешь профессию» — его снимаем), закрытые — нет (S4 = S2, лишних ячеек 0);
//     E  Alt+Z: 0 ячеек; после показа — как S2;
//     F  зона-накопитель (делегат расширяет переданные Mn/Mx и не сбрасывает их) — шлюз даёт ровно её прямоугольник;
//     G  настоящий клик Slate в полосу мира доходит до мира: шлюз «мир», и контроллер разобрал мировой клик — построил маршрут,
//        персонаж сдвинулся (короткий ход успевает закончиться) или ответ — нотис 1013 «Недоступная область» (офлайн-спавн стоит где придётся; клик сквозь окно не дал бы ни того,
//        ни другого). Точка — земля у ног персонажа; без маршрута — вторая точка с другой стороны.
//   Итог: «КЛИК-В-МИР ИТОГ: ok=N fail=M».
#include "CoreMinimal.h"
#include "AsdaDevConsole.h"
#include "Test/AsdaSelfTestCtx.h"   // ★ R5-c (12.11.2026): общая точка самотестов вместо своих копий помощников

#if ASDA_DEV_CONSOLE && !UE_BUILD_SHIPPING

#include "Test/AsdaSelfTestKit.h"
#include "UI/AsdaWindowSubsystem.h"
#include "UI/AsdaUiVisibilitySubsystem.h"
#include "UI/AsdaUiHelpers.h"
#include "UI/AsdaChatSubsystem.h"
#include "UI/AsdaChatWindowSubsystem.h"
#include "UI/AsdaNotice.h"
#include "UI/AsdaInventorySubsystem.h"
#include "UI/AsdaCharaInfoSubsystem.h"
#include "UI/AsdaSkillWindowSubsystem.h"
#include "UI/AsdaCampWarSubsystem.h"
#include "UI/AsdaCampWarBattleSubsystem.h"
#include "Quest/AsdaQuestLogSubsystem.h"
#include "World/AsdaLoadManagerSubsystem.h"
#include "World/AsdaPlayerController.h"
#include "AsdaSessionReset.h"
#include "Blueprint/UserWidget.h"
#include "Components/Image.h"
#include "Components/PanelWidget.h"
#include "Components/TextBlock.h"
#include "Components/RichTextBlock.h"
#include "Engine/Engine.h"
#include "Engine/GameInstance.h"
#include "Engine/GameViewportClient.h"
#include "Engine/World.h"
#include "GameFramework/Pawn.h"
#include "GameFramework/PlayerController.h"
#include "Framework/Application/SlateApplication.h"
#include "HAL/IConsoleManager.h"
#include "TimerManager.h"
#include "Types/ReflectionMetadata.h"
#include "Widgets/SViewport.h"
#include "Widgets/SWindow.h"

DEFINE_LOG_CATEGORY_STATIC(LogAsdaWorldClickTest, Log, All);

namespace AsdaWorldClickTest
{
	static const TCHAR* kCmd = TEXT("asda.UI.SelfTestWorldClick");   // имя команды
	static constexpr int32 kNX = 64, kNY = 48;                          // сетка ячеек по вьюпорту
	static const FName kAccumZone(TEXT("ASDA_SELFTEST_ACCUM_ZONE"));    // зона-накопитель проверки F

	// Ячейка сетки: окно/зона шлюза и поймавший виджет (None — мир), лист Slate, съест ли его кнопка Slate.
	struct FCell
	{
		FName Zone;          // окно или ручная зона (None — клик в мир)
		FName Widget;        // поймавший виджет (None у ручной зоны)
		bool bSlateEats = false;   // в пути Slate под точкой есть виджет с захватом мыши (кнопка, поле, галка…)
		FString SlateLeaf;   // лист пути Slate
	};
	// Снимок сетки.
	struct FScan
	{
		TArray<FCell> Cells;            // kNX*kNY ячеек
		FVector2D Origin, CellSz;       // абсолютное начало вьюпорта и размер ячейки (px Slate)
		bool bValid = false;            // вьюпорт был
		int32 Count(TFunctionRef<bool(int32 X, int32 Y, const FCell&)> Pred) const
		{
			int32 N = 0;
			for (int32 i = 0; i < Cells.Num(); ++i) { if (Pred(i % kNX, i / kNX, Cells[i])) { ++N; } }
			return N;
		}
		FVector2D Center(int32 X, int32 Y) const { return Origin + FVector2D((X + 0.5f) * CellSz.X, (Y + 0.5f) * CellSz.Y); }
	};

	// Контекст прогона (живёт между таймерами).
	struct FCtx
	{
		int32 Ok = 0, Fail = 0;                 // пройдено / провалено
		TWeakObjectPtr<UWorld> W;               // мир прогона
		bool bQuit = false;                     // выйти после итога
		int32 MapId = 3;                        // карта прогона
		float T = 0.f;                          // время следующего шага
		TSharedPtr<FAsdaCVarScope> Keys;        // ключи на время прогона (RAII)
		bool bCampWarHud = false;               // включён HUD боя (карта 19)
		TMap<FString, FScan> Fixed, Legacy;     // снимки по состояниям: новые правила / прежние
		TArray<FName> OpenedNew;                // окна, появившиеся в S3
		int32 N1013 = -1;                       // строк нотиса 1013 в журнале до клика G
		FVector PawnAt0 = FVector::ZeroVector;  // позиция своего павна до клика G
		void Expect(bool b, const FString& What)
		{
			if (b) { ++Ok;   UE_LOG(LogAsdaWorldClickTest, Log,   TEXT("КЛИК-В-МИР ok   %s"), *What); }
			else   { ++Fail; UE_LOG(LogAsdaWorldClickTest, Error, TEXT("КЛИК-В-МИР FAIL %s"), *What); }
		}
	};

	using AsdaSelfTestCtx::GIOf;   // ★ R5-c (12.11.2026): общая точка (`Test/AsdaSelfTestCtx.h`) — копия была дословной.
	using AsdaSelfTestCtx::Sub;   // ★ R5-c (12.11.2026): общая точка (`Test/AsdaSelfTestCtx.h`) — копия была дословной.

	// Следующий шаг через Step секунд после предыдущего.
	using AsdaSelfTestCtx::Then;   // ★ R5-c (12.11.2026): общая точка (`Test/AsdaSelfTestCtx.h`) — копия была дословной.

	// Полоса мира над персонажем в ячейках (x 224…704, y 128…480 при 1024×768; левее — карта боя 49900 (3,322)-(203,522)).
	static bool InWorldBand(int32 X, int32 Y) { return X >= 14 && X <= 43 && Y >= 8 && Y <= 29; }

	// Тип Slate-виджета с собственным захватом мыши (его нажатие до мира не дойдёт, что бы ни сказал шлюз).
	static bool IsSlateCaptor(const FString& Type)
	{
		return Type == TEXT("SButton") || Type == TEXT("SCheckBox") || Type == TEXT("SEditableText") || Type == TEXT("SMultiLineEditableText")
			|| Type == TEXT("SComboButton") || Type == TEXT("SSlider") || Type == TEXT("SScrollBar") || Type == TEXT("SEditableTextBox");
	}

	// Снять сетку при текущих ключах шлюза.
	static FScan ScanGrid(const TSharedRef<FCtx>& C)
	{
		FScan Out;
		UAsdaWindowSubsystem* WM = Sub<UAsdaWindowSubsystem>(C);
		if (!WM || !GEngine || !GEngine->GameViewport || !GEngine->GameViewport->GetGameViewportWidget().IsValid()) { return Out; }
		const FGeometry G = GEngine->GameViewport->GetGameViewportWidget()->GetCachedGeometry();
		const FVector2D Sz = G.GetLocalSize();
		if (Sz.X < 64.f || Sz.Y < 64.f) { return Out; }
		Out.Origin = G.LocalToAbsolute(FVector2D::ZeroVector);
		Out.CellSz = (G.LocalToAbsolute(Sz) - Out.Origin) / FVector2D(kNX, kNY);
		TArray<TSharedRef<SWindow>> Wins;
		FSlateApplication::Get().GetAllVisibleWindowsOrdered(Wins);
		Out.Cells.SetNum(kNX * kNY);
		for (int32 Y = 0; Y < kNY; ++Y)
		{
			for (int32 X = 0; X < kNX; ++X)
			{
				FCell& Cl = Out.Cells[Y * kNX + X];
				const FVector2D Abs = Out.Center(X, Y);
				WM->IsScreenPointOverAnyUiWindowEx(Abs, Cl.Zone, Cl.Widget);
				const FWidgetPath Path = FSlateApplication::Get().LocateWindowUnderMouse(Abs, Wins, /*bIgnoreEnabledStatus*/ false);
				if (Path.IsValid() && Path.Widgets.Num() > 0)
				{
					const TSharedRef<SWidget> Lw = Path.Widgets.Last().Widget;
					Cl.SlateLeaf = Lw->GetTypeAsString() + TEXT(" ") + FReflectionMetaData::GetWidgetDebugInfo(&Lw.Get());
					for (int32 i = 0; i < Path.Widgets.Num(); ++i)
					{
						const TSharedRef<SWidget> Pw = Path.Widgets[i].Widget;
						if (IsSlateCaptor(Pw->GetTypeAsString()) && Pw->GetVisibility().IsHitTestVisible()) { Cl.bSlateEats = true; }
					}
				}
			}
		}
		Out.bValid = true;
		return Out;
	}

	// Подробность о поймавшем виджете (класс, видимость, прямоугольник, прозрачность, кисть/текст, предки, «в вьюпорте»).
	static FString DescribeWidget(const TSharedRef<FCtx>& C, FName Zone, FName Widget)
	{
		UAsdaWindowSubsystem* WM = Sub<UAsdaWindowSubsystem>(C);
		UUserWidget* Win = WM ? WM->FindWindow(Zone) : nullptr;
		if (Widget.IsNone()) { return Win ? TEXT("ручная зона окна") : TEXT("ручная зона"); }
		UWidget* Wg = Win ? Win->GetWidgetFromName(Widget) : nullptr;
		if (!Wg) { return TEXT("виджет не найден по имени"); }
		FVector2D Mn, Mx;
		const bool bR = AsdaUi::WidgetAbsRect(Wg, Mn, Mx);
		FString S = FString::Printf(TEXT("%s vis=%s rect=%s(%.0f,%.0f)-(%.0f,%.0f) opacity=%.2f inViewport=%d"),
			*Wg->GetClass()->GetName(), *StaticEnum<ESlateVisibility>()->GetNameStringByValue((int64)Wg->GetVisibility()),
			bR ? TEXT("") : TEXT("НЕТ "), Mn.X, Mn.Y, Mx.X, Mx.Y, Wg->GetRenderOpacity(), Win->IsInViewport() ? 1 : 0);
		if (const UImage* Im = Cast<UImage>(Wg))
		{
			const FSlateBrush& Br = Im->GetBrush();
			const UObject* Res = Br.GetResourceObject();
			S += FString::Printf(TEXT(" кисть=%s tintA=%.2f colorA=%.2f"), Res ? *Res->GetName() : TEXT("НЕТ"),
				Br.TintColor.GetSpecifiedColor().A, Im->GetColorAndOpacity().A);
		}
		if (const UTextBlock* Tb = Cast<UTextBlock>(Wg)) { S += FString::Printf(TEXT(" текст=«%s»"), *Tb->GetText().ToString().Left(40)); }
		if (const URichTextBlock* Rb = Cast<URichTextBlock>(Wg)) { S += FString::Printf(TEXT(" текст=«%s»"), *Rb->GetText().ToString().Left(40)); }
		FString Chain;
		for (const UWidget* P = Wg->GetParent(); P; P = P->GetParent())
		{
			Chain += FString::Printf(TEXT(" ← %s[%s]"), *P->GetName(), *StaticEnum<ESlateVisibility>()->GetNameStringByValue((int64)P->GetVisibility()));
		}
		return S + Chain;
	}

	// Печать снимка: карта ячеек буквами по окнам, затем пары «окно/виджет» с числом ячеек и подробностью.
	static void LogScan(const TSharedRef<FCtx>& C, const TCHAR* State, const TCHAR* Mode, const FScan& S)
	{
		if (!S.bValid) { UE_LOG(LogAsdaWorldClickTest, Warning, TEXT("[клик-в-мир] СЕТКА «%s/%s»: вьюпорта нет"), State, Mode); return; }
		TMap<FName, TCHAR> Letter;
		TMap<FString, int32> ByPair, ByPairBand;
		TMap<FString, TPair<FName, FName>> PairKey;
		FString Map;
		int32 Ui = 0, Band = 0, SlateEatWorld = 0;
		for (int32 Y = 0; Y < kNY; ++Y)
		{
			FString Row;
			for (int32 X = 0; X < kNX; ++X)
			{
				const FCell& Cl = S.Cells[Y * kNX + X];
				if (Cl.Zone.IsNone()) { Row += Cl.bSlateEats ? TEXT('#') : (InWorldBand(X, Y) ? TEXT(',') : TEXT('.')); if (Cl.bSlateEats) { ++SlateEatWorld; } continue; }
				++Ui;
				if (!Letter.Contains(Cl.Zone)) { Letter.Add(Cl.Zone, (TCHAR)(Letter.Num() < 26 ? TEXT('A') + Letter.Num() : TEXT('a') + (Letter.Num() - 26) % 26)); }
				Row += Letter[Cl.Zone];
				const FString K = Cl.Zone.ToString() + TEXT("/") + Cl.Widget.ToString();
				ByPair.FindOrAdd(K) += 1;
				PairKey.Add(K, TPair<FName, FName>(Cl.Zone, Cl.Widget));
				if (InWorldBand(X, Y)) { ++Band; ByPairBand.FindOrAdd(K) += 1; }
			}
			Map += TEXT("\n    ") + Row;
		}
		FString Legend;
		for (const TPair<FName, TCHAR>& L : Letter) { Legend += FString::Printf(TEXT(" %c=%s"), L.Value, *L.Key.ToString()); }
		UE_LOG(LogAsdaWorldClickTest, Log, TEXT("[клик-в-мир] СЕТКА «%s/%s» карта %d (%dx%d, ячейка %.0fx%.0f): окно в %d из %d, в полосе мира %d, "
			"мир по шлюзу, но кнопка Slate съест — %d (#);%s (, — полоса мира)%s"),
			State, Mode, C->MapId, kNX, kNY, S.CellSz.X, S.CellSz.Y, Ui, S.Cells.Num(), Band, SlateEatWorld, *Legend, *Map);
		ByPair.ValueSort([](int32 A, int32 B) { return A > B; });
		for (const TPair<FString, int32>& P : ByPair)
		{
			const TPair<FName, FName>& Key = PairKey[P.Key];
			const int32* InBand = ByPairBand.Find(P.Key);
			UE_LOG(LogAsdaWorldClickTest, Log, TEXT("[клик-в-мир]   «%s/%s» %-48s ячеек %4d (в полосе %d): %s"),
				State, Mode, *P.Key, P.Value, InBand ? *InBand : 0, *DescribeWidget(C, Key.Key, Key.Value));
		}
		// Ячейки «мир», где Slate-кнопка съест клик: лист и место.
		TMap<FString, int32> Eaten;
		for (const FCell& Cl : S.Cells) { if (Cl.Zone.IsNone() && Cl.bSlateEats) { Eaten.FindOrAdd(Cl.SlateLeaf) += 1; } }
		for (const TPair<FString, int32>& E : Eaten)
		{
			UE_LOG(LogAsdaWorldClickTest, Log, TEXT("[клик-в-мир]   «%s/%s» мир по шлюзу, Slate съест: %4d × %s"), State, Mode, E.Value, *E.Key);
		}
	}

	// Снять сетку в обоих режимах (прежние правила / новые), напечатать и запомнить.
	static void ScanBoth(const TSharedRef<FCtx>& C, const TCHAR* State)
	{
		C->Keys->Set(TEXT("asda.UI.ZoneRectInit"), 0);
		C->Keys->Set(TEXT("asda.UI.HitTestPlanesOnly"), 0);
		const FScan L = ScanGrid(C);
		C->Keys->Set(TEXT("asda.UI.ZoneRectInit"), 1);
		C->Keys->Set(TEXT("asda.UI.HitTestPlanesOnly"), 1);
		const FScan F = ScanGrid(C);
		LogScan(C, State, TEXT("прежде"), L);
		LogScan(C, State, TEXT("теперь"), F);
		// Что сняли новые правила: пары, ловившие прежде и не ловящие теперь (ячейки, где ответ сменился «окно» → «мир»).
		if (L.bValid && F.bValid)
		{
			TMap<FString, int32> Freed;
			for (int32 i = 0; i < L.Cells.Num(); ++i)
			{
				if (!L.Cells[i].Zone.IsNone() && F.Cells[i].Zone.IsNone())
				{
					Freed.FindOrAdd(L.Cells[i].Zone.ToString() + TEXT("/") + L.Cells[i].Widget.ToString()) += 1;
				}
			}
			for (const TPair<FString, int32>& P : Freed)
			{
				UE_LOG(LogAsdaWorldClickTest, Log, TEXT("[клик-в-мир]   «%s» ОСВОБОЖДЕНО новыми правилами: %-48s ячеек %d"), State, *P.Key, P.Value);
			}
		}
		C->Legacy.Add(State, L);
		C->Fixed.Add(State, F);
	}

	// Ячеек окна (или окон с префиксом) в снимке.
	static int32 CellsOf(const FScan& S, const TCHAR* Prefix)
	{
		return S.Count([Prefix](int32, int32, const FCell& Cl) { return !Cl.Zone.IsNone() && Cl.Zone.ToString().StartsWith(Prefix); });
	}
	// Ячеек в полосе мира (окно по шлюзу или кнопка Slate).
	static int32 BandBlocked(const FScan& S)
	{
		return S.Count([](int32 X, int32 Y, const FCell& Cl) { return InWorldBand(X, Y) && (!Cl.Zone.IsNone() || Cl.bSlateEats); });
	}
	// Ячейки, где снимок B «окно», а A — «мир» (лишние ячейки B против A), с перечнем пар.
	static int32 ExtraCells(const FScan& A, const FScan& B, FString& OutWho)
	{
		int32 N = 0;
		TMap<FString, int32> Who;
		for (int32 i = 0; i < A.Cells.Num() && i < B.Cells.Num(); ++i)
		{
			if (A.Cells[i].Zone.IsNone() && !B.Cells[i].Zone.IsNone()) { ++N; Who.FindOrAdd(B.Cells[i].Zone.ToString() + TEXT("/") + B.Cells[i].Widget.ToString()) += 1; }
		}
		for (const TPair<FString, int32>& P : Who) { OutWho += FString::Printf(TEXT(" %s×%d"), *P.Key, P.Value); }
		return N;
	}
	// Окна, ловящие хоть одну ячейку.
	static TSet<FName> ZonesOf(const FScan& S)
	{
		TSet<FName> Z;
		for (const FCell& Cl : S.Cells) { if (!Cl.Zone.IsNone()) { Z.Add(Cl.Zone); } }
		return Z;
	}

	// Проверка «полоса мира свободна» в состоянии.
	static void ExpectBandFree(const TSharedRef<FCtx>& C, const TCHAR* State)
	{
		const FScan* F = C->Fixed.Find(State);
		const FScan* L = C->Legacy.Find(State);
		if (!F || !F->bValid) { C->Expect(false, FString::Printf(TEXT("A %s: сетка снята"), State)); return; }
		C->Expect(BandBlocked(*F) == 0, FString::Printf(TEXT("A %s: полоса мира над персонажем свободна — ячеек «окно» %d (прежде %d) [ИСПРАВЛЕНИЕ]"),
			State, BandBlocked(*F), L ? BandBlocked(*L) : -1));
	}

	// Завершение: снять зону F, вернуть HUD боя, ключи, интерфейс.
	static void Finish(const TSharedRef<FCtx>& C)
	{
		if (UAsdaWindowSubsystem* WM = Sub<UAsdaWindowSubsystem>(C)) { WM->UnregisterUiZone(kAccumZone); }
		if (UAsdaUiVisibilitySubsystem* Vis = Sub<UAsdaUiVisibilitySubsystem>(C)) { Vis->SetUiShown(true, TEXT("самотест клика в мир: конец")); }
		if (C->bCampWarHud)
		{
			if (UAsdaCampWarSubsystem* CW = Sub<UAsdaCampWarSubsystem>(C)) { CW->OnPortalTransfer5055(false, 3); CW->ResetSessionState(EAsdaSessionResetReason::EnteringWorld); }
			if (UAsdaCampWarBattleSubsystem* Bt = Sub<UAsdaCampWarBattleSubsystem>(C)) { Bt->ResetSessionState(EAsdaSessionResetReason::EnteringWorld); }
		}
		if (C->Keys) { C->Keys->Restore(); }
		AsdaSelfTestKit::Finish(C->W.Get(), kCmd, C->bQuit, LogAsdaWorldClickTest, TEXT("КЛИК-В-МИР"), C->Ok, C->Fail);
	}

	static void ClickGround(const TSharedRef<FCtx>& C, int32 Try);   // G — ниже

	// Через Sec секунд ОТ ТЕКУЩЕГО момента (для шагов, заказанных уже изнутри таймера; `Then` считает от старта прогона).
	static void After(const TSharedRef<FCtx>& C, float Sec, TFunction<void()> Fn)
	{
		UWorld* W = C->W.Get();
		if (!W) { return; }
		FTimerHandle H;
		W->GetTimerManager().SetTimer(H, FTimerDelegate::CreateLambda(MoveTemp(Fn)), FMath::Max(0.01f, Sec), false);
	}

	// Ход прогона после того, как свой персонаж есть.
	static void Run(const TSharedRef<FCtx>& C)
	{
		UAsdaWindowSubsystem* WM = Sub<UAsdaWindowSubsystem>(C);
		UAsdaUiVisibilitySubsystem* Vis = Sub<UAsdaUiVisibilitySubsystem>(C);
		UAsdaLoadManagerSubsystem* LM = Sub<UAsdaLoadManagerSubsystem>(C);
		if (!WM || !Vis || !LM || !FSlateApplication::IsInitialized())
		{
			C->Expect(false, TEXT("подсистемы окон/видимости/загрузки и Slate есть"));
			Finish(C);
			return;
		}
		C->Keys = MakeShared<FAsdaCVarScope>(C->W.Get(), kCmd);
		Vis->SetUiShown(true, TEXT("самотест клика в мир: старт"));
		// Карта 19 — HUD боя армий (как вход 5055 с города).
		if (C->MapId == 19)
		{
			C->Keys->Set(TEXT("asda.CampWar.Battle"), 1);
			if (UAsdaCampWarSubsystem* CW = Sub<UAsdaCampWarSubsystem>(C))
			{
				LM->CurrentMapId = 3;
				CW->OnPortalTransfer5055(/*bHudOn*/ true, /*TargetMapId*/ 19);
				C->bCampWarHud = true;
			}
			LM->CurrentMapId = 19;
		}
		// S1 — типовой HUD.
		Then(C, 1.5f, [C]()
		{
			ScanBoth(C, TEXT("S1_hud"));
			ExpectBandFree(C, TEXT("S1_hud"));
			const FScan& F = C->Fixed[TEXT("S1_hud")];
			const FScan& L = C->Legacy[TEXT("S1_hud")];
			for (const TCHAR* Win : { TEXT("MINISTATE_WIN"), TEXT("MINIMAP_WIN"), TEXT("CHAT_NEW"), TEXT("QUICK_NEW_WIN"), TEXT("SYSTEMMAIN_NEW_WIN") })
			{
				const int32 Nf = F.Count([Win](int32, int32, const FCell& Cl) { return Cl.Zone == FName(Win); });
				const int32 Nl = L.Count([Win](int32, int32, const FCell& Cl) { return Cl.Zone == FName(Win); });
				C->Expect(Nf > 0, FString::Printf(TEXT("B S1: окно HUD «%s» ловит свои клики — ячеек %d (прежде %d)"), Win, Nf, Nl));
			}
			if (C->MapId == 19)
			{
				C->Expect(CellsOf(F, TEXT("CAMPWAR_")) > 0, FString::Printf(TEXT("B S1: окна боя 49600/49900/49940 ловят свои клики — ячеек %d"), CellsOf(F, TEXT("CAMPWAR_"))));
			}
			// F — зона-накопитель: делегат только расширяет переданные Mn/Mx (как `FAsdaPopupMenu::AccumRect` без сброса).
			UAsdaWindowSubsystem* WM = Sub<UAsdaWindowSubsystem>(C);
			const FVector2D A = F.Origin + FVector2D(300.0, 300.0), B = F.Origin + FVector2D(316.0, 316.0);
			WM->RegisterUiZone(kAccumZone, FAsdaUiZoneRect::CreateLambda([A, B](FVector2D& Mn, FVector2D& Mx) -> bool
			{
				Mn = FVector2D(FMath::Min(Mn.X, A.X), FMath::Min(Mn.Y, A.Y));
				Mx = FVector2D(FMath::Max(Mx.X, B.X), FMath::Max(Mx.Y, B.Y));
				return true;
			}));
			FName Z, Wd;
			const bool bIn = WM->IsScreenPointOverAnyUiWindowEx((A + B) * 0.5, Z, Wd) && Z == kAccumZone;
			FName Z2, Wd2;
			const bool bOut = WM->IsScreenPointOverAnyUiWindowEx(F.Origin + FVector2D(296.0, 296.0), Z2, Wd2) && Z2 == kAccumZone;
			FName Z3, Wd3;
			const bool bFar = WM->IsScreenPointOverAnyUiWindowEx(F.Origin + FVector2D(2.0, 2.0), Z3, Wd3) && Z3 == kAccumZone;
			FVector2D Mn, Mx;
			const bool bRect = WM->GetUiZoneRect(kAccumZone, Mn, Mx);
			C->Expect(bIn && !bOut && !bFar && bRect && Mn.Equals(A, 0.01) && Mx.Equals(B, 0.01),
				FString::Printf(TEXT("F: зона-накопитель без сброса — шлюз даёт ровно её прямоугольник (%.0f,%.0f)-(%.0f,%.0f): внутри=%d, рядом=%d, "
					"угол экрана=%d [ИСПРАВЛЕНИЕ asda.UI.ZoneRectInit]"), Mn.X, Mn.Y, Mx.X, Mx.Y, bIn ? 1 : 0, bOut ? 1 : 0, bFar ? 1 : 0));
			WM->UnregisterUiZone(kAccumZone);
			// S2 — журналы чата полны длинных строк (прокрутка уводит ранние строки выше окна).
			const FString Long = TEXT("Проверка клика в мир: очень длинная строка журнала, чтобы перенос и прокрутка увели ранние строки выше окна чата ")
				TEXT("— The quick brown fox jumps over the lazy dog 0123456789");
			if (UAsdaChatSubsystem* Sys = Sub<UAsdaChatSubsystem>(C))
			{
				for (int32 i = 0; i < 40; ++i) { Sys->AddSystemMessage(FString::Printf(TEXT("%d %s"), i, *Long), FColor(255, 220, 120), EAsdaSysTrack::A, 10000.f); }
			}
			if (UAsdaChatWindowSubsystem* Ch = Sub<UAsdaChatWindowSubsystem>(C))
			{
				for (int32 i = 0; i < 40; ++i) { Ch->AddChatLine(EAsdaChatCat::Common, FString::Printf(TEXT("[Тест] %d %s"), i, *Long)); }
			}
		});
		Then(C, 1.0f, [C]()
		{
			ScanBoth(C, TEXT("S2_chat"));
			ExpectBandFree(C, TEXT("S2_chat"));
			const FScan& F1 = C->Fixed[TEXT("S1_hud")];
			const FScan& F2 = C->Fixed[TEXT("S2_chat")];
			FString Who;
			const int32 Extra = ExtraCells(F1, F2, Who);
			C->Expect(Extra == 0, FString::Printf(TEXT("C S2: строки чата не расширяют зону окон — лишних ячеек %d (окна чата: было %d, стало %d)%s"),
				Extra, CellsOf(F1, TEXT("CHAT_")), CellsOf(F2, TEXT("CHAT_")), *Who));
			// S3 — открыть типовые окна.
			if (UAsdaInventorySubsystem* Inv = Sub<UAsdaInventorySubsystem>(C)) { Inv->OpenWindow(); }
			if (UAsdaCharaInfoSubsystem* Ci = Sub<UAsdaCharaInfoSubsystem>(C)) { Ci->OpenWindow(); }
			if (UAsdaSkillWindowSubsystem* Sk = Sub<UAsdaSkillWindowSubsystem>(C)) { Sk->OpenWindow(); }
			if (UAsdaQuestLogSubsystem* Q = Sub<UAsdaQuestLogSubsystem>(C)) { Q->Open(); }
		});
		Then(C, 1.0f, [C]()
		{
			ScanBoth(C, TEXT("S3_open"));
			const TSet<FName> Before = ZonesOf(C->Fixed[TEXT("S2_chat")]);
			const TSet<FName> Now = ZonesOf(C->Fixed[TEXT("S3_open")]);
			C->OpenedNew.Reset();
			for (const FName& Z : Now) { if (!Before.Contains(Z)) { C->OpenedNew.Add(Z); } }
			FString Names;
			for (const FName& Z : C->OpenedNew) { Names += TEXT(" ") + Z.ToString(); }
			const bool bInv = Now.Contains(FName(TEXT("INVEN_WIN")));
			const bool bCi = Now.Contains(FName(TEXT("CHARAINFO_WIN")));
			const bool bQ = Now.Contains(FName(TEXT("QUEST_WIN")));
			C->Expect(bInv && bCi && bQ,
				FString::Printf(TEXT("D S3: открытые окна ловят свои клики — новых окон %d:%s (инвентарь=%d, инфо=%d, квесты=%d)"),
					C->OpenedNew.Num(), *Names, bInv ? 1 : 0, bCi ? 1 : 0, bQ ? 1 : 0));
			if (UAsdaInventorySubsystem* Inv = Sub<UAsdaInventorySubsystem>(C)) { Inv->CloseWindow(); }
			if (UAsdaCharaInfoSubsystem* Ci = Sub<UAsdaCharaInfoSubsystem>(C)) { Ci->CloseWindow(); }
			if (UAsdaSkillWindowSubsystem* Sk = Sub<UAsdaSkillWindowSubsystem>(C)) { Sk->CloseWindow(); }
			if (UAsdaQuestLogSubsystem* Q = Sub<UAsdaQuestLogSubsystem>(C)) { Q->Close(); }
			// Окно навыков офлайн отвечает окном-сообщением (loc «не меняешь профессию») — настоящее окно, снимаем его до S4.
			if (UAsdaWindowSubsystem* WM = Sub<UAsdaWindowSubsystem>(C)) { WM->ClearMsgBoxQueue(TEXT("самотест клика в мир: после S3")); }
		});
		Then(C, 1.0f, [C]()
		{
			ScanBoth(C, TEXT("S4_closed"));
			ExpectBandFree(C, TEXT("S4_closed"));
			FString Who;
			const int32 Extra = ExtraCells(C->Fixed[TEXT("S2_chat")], C->Fixed[TEXT("S4_closed")], Who);
			C->Expect(Extra == 0, FString::Printf(TEXT("D S4: закрытые окна клик не ловят — лишних ячеек против S2: %d%s"), Extra, *Who));
			FString WhoL;
			const int32 ExtraL = ExtraCells(C->Legacy[TEXT("S2_chat")], C->Legacy[TEXT("S4_closed")], WhoL);
			UE_LOG(LogAsdaWorldClickTest, Log, TEXT("[клик-в-мир] S4 прежде: лишних ячеек против S2 — %d%s"), ExtraL, *WhoL);
			// S5 — Alt+Z.
			if (UAsdaUiVisibilitySubsystem* Vis = Sub<UAsdaUiVisibilitySubsystem>(C)) { Vis->SetUiShown(false, TEXT("самотест клика в мир: Alt+Z")); }
		});
		Then(C, 0.5f, [C]()
		{
			ScanBoth(C, TEXT("S5_hidden"));
			const FScan& F = C->Fixed[TEXT("S5_hidden")];
			const int32 N = F.Count([](int32, int32, const FCell& Cl) { return !Cl.Zone.IsNone(); });
			const int32 Ns = F.Count([](int32, int32, const FCell& Cl) { return Cl.bSlateEats; });
			C->Expect(N == 0 && Ns == 0, FString::Printf(TEXT("E S5: интерфейс скрыт (Alt+Z) — ячеек «окно» %d, кнопок Slate %d"), N, Ns));
			if (UAsdaUiVisibilitySubsystem* Vis = Sub<UAsdaUiVisibilitySubsystem>(C)) { Vis->SetUiShown(true, TEXT("самотест клика в мир: Alt+Z обратно")); }
		});
		Then(C, 0.7f, [C]()
		{
			ScanBoth(C, TEXT("S6_shown"));
			ExpectBandFree(C, TEXT("S6_shown"));
			FString Who;
			const int32 Extra = ExtraCells(C->Fixed[TEXT("S2_chat")], C->Fixed[TEXT("S6_shown")], Who);
			FString Who2;
			const int32 Missing = ExtraCells(C->Fixed[TEXT("S6_shown")], C->Fixed[TEXT("S2_chat")], Who2);
			C->Expect(Extra == 0 && Missing == 0, FString::Printf(TEXT("E S6: после показа — как S2: лишних %d%s, пропавших %d%s"), Extra, *Who, Missing, *Who2));
			// G — настоящий клик Slate в полосу мира: до мира доходит (контроллер строит маршрут). Точка — земля у ног персонажа.
			ClickGround(C, 0);
		});
	}

	// G: клик Slate в ячейку полосы мира у ног персонажа (попытка Try: 0 — справа, 1 — слева), через 0.6 с — проверка маршрута.
	static void ClickGround(const TSharedRef<FCtx>& C, int32 Try)
	{
		UAsdaWindowSubsystem* WM = Sub<UAsdaWindowSubsystem>(C);
		const FScan* F = C->Fixed.Find(TEXT("S6_shown"));
		if (!WM || !F || !F->bValid) { C->Expect(false, TEXT("G: окна и сетка есть")); Finish(C); return; }
		const int32 X = (Try == 0) ? kNX / 2 + 8 : kNX / 2 - 8, Y = 26;
		UAsdaChatSubsystem* Sys = Sub<UAsdaChatSubsystem>(C);
		if (Try == 0)
		{
			C->N1013 = Sys ? Sys->DebugCountSystemLines(AsdaNotice::Loc(GIOf(C), 1013)) : -1;
			const APlayerController* PC0 = C->W.IsValid() ? C->W->GetFirstPlayerController() : nullptr;
			C->PawnAt0 = (PC0 && PC0->GetPawn()) ? PC0->GetPawn()->GetActorLocation() : FVector::ZeroVector;
		}
		const FVector2D P = F->Center(X, Y);
		FName Z, Wd;
		const bool bUi = WM->IsScreenPointOverAnyUiWindowEx(P, Z, Wd);
		WM->SimulateClickAt(P, /*HoldFrames*/ 3);
		After(C, 0.6f, [C, Try, P, bUi, Z]()
		{
			AAsdaPlayerController* PC = C->W.IsValid() ? Cast<AAsdaPlayerController>(C->W->GetFirstPlayerController()) : nullptr;
			const bool bMoved = PC && PC->GetPawn() && FVector::Dist2D(PC->GetPawn()->GetActorLocation(), C->PawnAt0) > 1.0;
			const bool bRoute = PC && (PC->HasRoute() || bMoved);
			if (!bRoute && Try == 0 && !bUi) { ClickGround(C, 1); return; }
			UAsdaChatSubsystem* Sys = Sub<UAsdaChatSubsystem>(C);
			const int32 N = Sys ? Sys->DebugCountSystemLines(AsdaNotice::Loc(GIOf(C), 1013)) : -1;
			const bool bRefused = C->N1013 >= 0 && N > C->N1013;
			C->Expect(!bUi && (bRoute || bRefused), FString::Printf(TEXT("G: настоящий клик Slate в полосу мира (%.0f,%.0f) — шлюз: %s; контроллер разобрал "
				"мировой клик: маршрут или ход=%d, нотис 1013 «недоступная область»=%d (попытка %d)"),
				P.X, P.Y, bUi ? *Z.ToString() : TEXT("мир"), bRoute ? 1 : 0, bRefused ? 1 : 0, Try + 1));
			Finish(C);
		});
	}
}

static FAutoConsoleCommandWithWorldAndArgs GAsdaSelfTestWorldClick(
	TEXT("asda.UI.SelfTestWorldClick"),
	TEXT("Самотест «клик в мир сквозь HUD»: сетка 64×48 шлюзом «курсор над окном» (прежние/новые правила), кто перехватил каждую ячейку; "
	     "состояния HUD/чат/окна открыты/закрыты/Alt+Z. Аргумент — номер карты (3 по умолчанию; 19 — с HUD боя). Офлайн. `quit`."),
	FConsoleCommandWithWorldAndArgsDelegate::CreateLambda([](const TArray<FString>& Args, UWorld* W)
	{
		using namespace AsdaWorldClickTest;
		const FAsdaSelfTestArgs KA = FAsdaSelfTestArgs::Parse(Args);
		const int32 MapId = KA.Int(0, 3);
		// ★ 11.11.2026 (D4b): строки военного сообщения (WARMESSAGE_WIN, живут 10 с) от прошлого самотеста той же игры стоят над
		//   персонажем и попадают в сетку как «окно» — на EN после UI.SelfTestWave1B четыре провала «полоса мира свободна». Старт
		//   откладывается, пока строки не погаснут сами (повтор команды раз в секунду, не дольше 15 с).
		{
			static int32 GWarWaitTries = 0;
			UAsdaChatSubsystem* WarChat = (W && W->GetGameInstance()) ? W->GetGameInstance()->GetSubsystem<UAsdaChatSubsystem>() : nullptr;
			if (WarChat) { WarChat->DebugTickWarExpiry(); }
			if (WarChat && WarChat->DebugWarLineCount() > 0 && GWarWaitTries < 15 && GEngine)
			{
				++GWarWaitTries;
				const FString Again = FString(kCmd) + TEXT(" ") + FString::Join(Args, TEXT(" "));
				TWeakObjectPtr<UWorld> WarW(W);
				FTimerHandle WarH;
				W->GetTimerManager().SetTimer(WarH, FTimerDelegate::CreateLambda([WarW, Again]() { if (WarW.IsValid() && GEngine) { GEngine->Exec(WarW.Get(), *Again); } }), 1.f, false);
				return;
			}
			GWarWaitTries = 0;
		}
		// Свой персонаж нужен контроллеру мира — офлайн его нет: штатный фейковый спавн и повторный запуск через 2 с.
		if (!KA.Has(TEXT("selfready")) && W && GEngine && AsdaSelfTestKit::NeedSelfSpawn(W))
		{
			GEngine->Exec(W, *FString::Printf(TEXT("Asda.Load.CurrentMap %d"), MapId));
			GEngine->Exec(W, TEXT("Asda.World.FakeSelfSpawn 256 256"));
			const FString Again = FString::Printf(TEXT("%s %d selfready%s"), kCmd, MapId, KA.bQuit ? TEXT(" quit") : TEXT(""));
			TWeakObjectPtr<UWorld> WW(W);
			FTimerHandle H;
			W->GetTimerManager().SetTimer(H, FTimerDelegate::CreateLambda([WW, Again]() { if (WW.IsValid() && GEngine) { GEngine->Exec(WW.Get(), *Again); } }), 2.f, false);
			return;
		}
		if (AsdaSelfTestKit::RefuseStart(W, kCmd, KA)) { return; }
		TSharedRef<FCtx> C = MakeShared<FCtx>();
		C->W = W;
		C->bQuit = KA.bQuit;
		C->MapId = MapId;
		if (UGameInstance* GI = W ? W->GetGameInstance() : nullptr)
		{
			if (UAsdaLoadManagerSubsystem* LM = GI->GetSubsystem<UAsdaLoadManagerSubsystem>()) { LM->CurrentMapId = MapId; }
		}
		Run(C);
	}));

#endif // ASDA_DEV_CONSOLE && !UE_BUILD_SHIPPING
