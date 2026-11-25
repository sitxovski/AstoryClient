// Copyright (c) 2026 @sitxovski (Discord)
// ★ САМОТЕСТ «КУРСОР НАД МИНИ-КАРТОЙ» (06.11.2026): `asda.SelfTest.MinimapCursor [quit]`. Офлайн (`-AsdaNoNet`), карта мира.
//   Путём игрока: настоящее движение мыши Slate в центр поля `MMap_ClickArea`, кадры игры идут между шагами (наведение Slate
//   считается по сетке хит-теста последнего кадра). Вид курсора читается у контроллера (`CurrentCursor`) — то, что уходит в ОС.
//   Шаги: K ключ 0 — прежнее (курсор мира); A над полем — «идти», вне поля — обычный; B Alt+Z — обычный, показ — снова «идти»;
//   C виджет поверх поля — обычный; D модальное окно — обычный; E чужой курсор интерфейса (самоцвет) не перебивается;
//   F настоящая сумка `INVEN_WIN`, перетащенная за ручку на поле, — обычный, после возврата — снова «идти»;
//   G окно пополнения `CashCharge_Win` (настоящий WBP): наведение на поле остаётся, но курсор не «идти» (`!sub_93B9B6(g_pCashChargeDialog)`).
//   Итог: «КУРСОР-МИНИКАРТЫ ИТОГ: ok=N fail=M».
#include "CoreMinimal.h"
#include "AsdaDevConsole.h"
#include "Test/AsdaSelfTestInput.h"   // ★ R5-c (12.11.2026): общая точка самотестов вместо своих копий помощников

#if ASDA_DEV_CONSOLE && !UE_BUILD_SHIPPING

#include "Test/AsdaSelfTestKit.h"
#include "UI/AsdaInventorySubsystem.h"
#include "UI/AsdaMinimapSubsystem.h"
#include "UI/AsdaUiHelpers.h"
#include "UI/AsdaUiVisibilitySubsystem.h"
#include "UI/AsdaWindowSubsystem.h"
#include "World/AsdaPlayerController.h"
#include "Blueprint/UserWidget.h"
#include "Engine/Engine.h"
#include "Engine/GameInstance.h"
#include "Engine/GameViewportClient.h"
#include "Engine/World.h"
#include "Framework/Application/SlateApplication.h"
#include "HAL/IConsoleManager.h"
#include "TimerManager.h"
#include "Widgets/Layout/SBorder.h"

DEFINE_LOG_CATEGORY_STATIC(LogAsdaMinimapCursorTest, Log, All);

namespace AsdaMinimapCursorTest
{
	using ECur = AAsdaPlayerController::EAsdaCursor;

	// Состояние прогона: шаги идут по таймеру, между ними живут кадры игры.
	struct FRun
	{
		TWeakObjectPtr<UWorld> World;                 // мир прогона
		TWeakObjectPtr<AAsdaPlayerController> PC;     // контроллер (вид курсора)
		TWeakObjectPtr<UAsdaMinimapSubsystem> Map;    // мини-карта
		TWeakObjectPtr<UAsdaUiVisibilitySubsystem> Vis;   // Alt+Z
		TWeakObjectPtr<UAsdaWindowSubsystem> WM;      // окна
		TArray<TFunction<void()>> Steps;              // шаги
		int32 Next = 0;                               // следующий шаг
		int32 Ok = 0;                                 // пройдено
		int32 Fail = 0;                               // провалено
		bool bQuit = false;                           // выйти по итогу
		FVector2D Over = FVector2D::ZeroVector;       // точка на поле карты
		FVector2D Away = FVector2D::ZeroVector;       // точка вне окон
		TSharedPtr<SWidget> Cover;                    // виджет поверх поля
		TWeakObjectPtr<UWidget> Area;                 // поле `MMap_ClickArea`
		TWeakObjectPtr<UAsdaInventorySubsystem> Inv;  // сумка — настоящее окно поверх поля
		FVector2D InvHome = FVector2D::ZeroVector;    // центр ручки сумки до перетаскивания
		int32 Clicks = 0;                             // принятые клики поля до шага
		TUniquePtr<FAsdaCVarScope> Keys;              // ключи на время прогона
		FTimerHandle Timer;                           // таймер шагов
	};

	// Проверка с записью в журнал.
	static void Check(FRun& R, bool b, const FString& What)
	{
		if (b) { ++R.Ok;   UE_LOG(LogAsdaMinimapCursorTest, Log,   TEXT("КУРСОР-МИНИКАРТЫ ok   %s"), *What); }
		else   { ++R.Fail; UE_LOG(LogAsdaMinimapCursorTest, Error, TEXT("КУРСОР-МИНИКАРТЫ FAIL %s"), *What); }
	}

	// Настоящее движение мыши Slate в точку.
	using AsdaSelfTestInput::Move;   // ★ R5-c (12.11.2026): общая точка (`Test/AsdaSelfTestInput.h`) — копия была дословной.

	// Центр самой крупной ручки окна (ручка корня). false — окна или ручки нет.
	static bool GrabPoint(const FRun& R, FName Id, FVector2D& Out)
	{
		double Best = -1.0;
		for (int32 i = 0; i < 16; ++i)
		{
			FVector2D Mn, Mx;
			if (!R.WM.IsValid() || !R.WM->DebugGetDragHandleRect(Id, i, Mn, Mx)) { continue; }
			const double A = (Mx.X - Mn.X) * (Mx.Y - Mn.Y);
			if (A > Best) { Best = A; Out = (Mn + Mx) * 0.5; }
		}
		return Best > 0.0;
	}

	// Вид курсора игры сейчас.
	static int32 Cur(const FRun& R) { return R.PC.IsValid() ? (int32)R.PC->CurrentCursor : -1; }

	// Следующий шаг или итог.
	static void Pump(TSharedRef<FRun> R)
	{
		UWorld* W = R->World.Get();
		if (!W) { return; }
		if (R->Next >= R->Steps.Num())
		{
			R->Keys.Reset();
			AsdaSelfTestKit::Finish(W, TEXT("asda.SelfTest.MinimapCursor"), R->bQuit, LogAsdaMinimapCursorTest, TEXT("КУРСОР-МИНИКАРТЫ"), R->Ok, R->Fail);
			return;
		}
		R->Steps[R->Next++]();
		W->GetTimerManager().SetTimer(R->Timer, FTimerDelegate::CreateLambda([R]() { Pump(R); }), 0.3f, false);
	}
}

static FAutoConsoleCommandWithWorldAndArgs GAsdaSelfTestMinimapCursor(
	TEXT("asda.SelfTest.MinimapCursor"),
	TEXT("Самотест курсора над мини-картой: настоящее движение мыши Slate по полю, Alt+Z, виджет и сумка поверх, модальное окно, "
	     "окно пополнения, чужой курсор интерфейса, ключ 0. Офлайн, на карте мира. `quit`."),
	FConsoleCommandWithWorldAndArgsDelegate::CreateLambda([](const TArray<FString>& Args, UWorld* W)
	{
		using namespace AsdaMinimapCursorTest;
		static const TCHAR* kCmd = TEXT("asda.SelfTest.MinimapCursor");
		const FAsdaSelfTestArgs KA = FAsdaSelfTestArgs::Parse(Args);
		// Своему персонажу нужна карта мира: офлайн его нет — штатный фейковый спавн и повторный запуск через 3 с.
		if (!KA.Has(TEXT("selfready")) && W && GEngine && AsdaSelfTestKit::NeedSelfSpawn(W))
		{
			GEngine->Exec(W, TEXT("Asda.Load.CurrentMap 3"));
			GEngine->Exec(W, TEXT("Asda.World.FakeSelfSpawn 256 256"));
			const FString Again = FString::Printf(TEXT("%s selfready%s"), kCmd, KA.bQuit ? TEXT(" quit") : TEXT(""));
			TWeakObjectPtr<UWorld> WW(W);
			FTimerHandle H;
			W->GetTimerManager().SetTimer(H, FTimerDelegate::CreateLambda([WW, Again]() { if (WW.IsValid() && GEngine) { GEngine->Exec(WW.Get(), *Again); } }), 3.f, false);
			return;
		}
		if (AsdaSelfTestKit::RefuseStart(W, kCmd, KA)) { return; }
		UGameInstance* GI = W ? W->GetGameInstance() : nullptr;
		UAsdaWindowSubsystem* WM = GI ? GI->GetSubsystem<UAsdaWindowSubsystem>() : nullptr;
		UAsdaMinimapSubsystem* Map = GI ? GI->GetSubsystem<UAsdaMinimapSubsystem>() : nullptr;
		UAsdaUiVisibilitySubsystem* Vis = GI ? GI->GetSubsystem<UAsdaUiVisibilitySubsystem>() : nullptr;
		AAsdaPlayerController* PC = W ? Cast<AAsdaPlayerController>(W->GetFirstPlayerController()) : nullptr;
		UUserWidget* Win = WM ? WM->FindWindow(FName(TEXT("MINIMAP_WIN"))) : nullptr;
		UWidget* Area = Win ? Win->GetWidgetFromName(TEXT("MMap_ClickArea")) : nullptr;
		FVector2D Mn, Mx;
		if (!Map || !Vis || !PC || !Area || !AsdaUi::WidgetAbsRect(Area, Mn, Mx) || !FSlateApplication::IsInitialized())
		{
			AsdaSelfTestKit::Refused(W, kCmd, KA.bQuit, TEXT("нет окна мини-карты / поля MMap_ClickArea / контроллера"));
			return;
		}
		TSharedRef<FRun> R = MakeShared<FRun>();
		R->World = W; R->PC = PC; R->Map = Map; R->Vis = Vis; R->WM = WM; R->bQuit = KA.bQuit;
		R->Area = Area; R->Inv = GI->GetSubsystem<UAsdaInventorySubsystem>();
		R->Over = (Mn + Mx) * 0.5f;
		R->Away = FVector2D(Mn.X - 300.f, Mx.Y + 200.f);   // мир: левее и ниже окна карты
		R->Keys = MakeUnique<FAsdaCVarScope>(W, kCmd);
		Vis->SetUiShown(true, TEXT("самотест курсора мини-карты"));
		TWeakPtr<FRun> Wk = R;
		auto Add = [&R, Wk](TFunction<void(FRun&)> F) { R->Steps.Add([Wk, F]() { if (TSharedPtr<FRun> P = Wk.Pin()) { F(*P); } }); };
		const int32 kMove = (int32)ECur::Move;
		const int32 kNormal = (int32)ECur::Normal;

		// ═══ K. Ключ 0 — воспроизведение жалобы ═══
		Add([](FRun& X) { X.Keys->Set(TEXT("asda.Minimap.HoverCursor"), 0); Move(X.Over); });
		Add([=](FRun& X) { Check(X, Cur(X) != kMove && !X.Map->IsMiniCursorOverridden(), FString::Printf(TEXT("K1 ключ 0: над полем курсор не «идти» (%d) — жалоба воспроизведена"), Cur(X))); Move(X.Away); });
		// ═══ A. Наведение ═══
		Add([](FRun& X) { X.Keys->Set(TEXT("asda.Minimap.HoverCursor"), 1); Move(X.Over); });
		Add([=](FRun& X) { Check(X, Cur(X) == kMove && X.PC->bUiCursorOverride && X.Map->IsMiniCursorOverridden(), FString::Printf(TEXT("A1 над полем мини-карты — курсор «идти» (%d)"), Cur(X))); Move(X.Away); });
		Add([=](FRun& X) { Check(X, Cur(X) != kMove && !X.PC->bUiCursorOverride, FString::Printf(TEXT("A2 ушли с поля — курсор вернулся (%d), мир снова его ведёт"), Cur(X))); Move(X.Over); });
		// ═══ B. Alt+Z ═══
		Add([](FRun& X) { X.Vis->SetUiShown(false, TEXT("самотест")); Move(X.Over + FVector2D(1.f, 0.f)); });
		Add([=](FRun& X) { Check(X, Cur(X) != kMove && !X.Map->IsMiniCursorOverridden(), TEXT("B1 Alt+Z: скрытая мини-карта курсор не меняет")); X.Vis->SetUiShown(true, TEXT("самотест")); Move(X.Over); });
		Add([=](FRun& X) { Check(X, Cur(X) == kMove, TEXT("B2 интерфейс показан — над полем снова «идти»")); });
		// ═══ C. Виджет поверх поля ═══
		Add([](FRun& X)
		{
			X.Cover = SNew(SBorder).Visibility(EVisibility::Visible);
			GEngine->GameViewport->AddViewportWidgetContent(X.Cover.ToSharedRef(), 5000);
			Move(X.Over + FVector2D(0.f, 1.f));
		});
		Add([](FRun& X) { Move(X.Over); });
		Add([=](FRun& X)
		{
			Check(X, Cur(X) != kMove, FString::Printf(TEXT("C1 окно поверх мини-карты — курсор не «идти» (%d)"), Cur(X)));
			GEngine->GameViewport->RemoveViewportWidgetContent(X.Cover.ToSharedRef());
			X.Cover.Reset();
			Move(X.Over + FVector2D(1.f, 1.f));
		});
		Add([](FRun& X) { Move(X.Over); });
		Add([=](FRun& X) { Check(X, Cur(X) == kMove, TEXT("C2 окно убрано — снова «идти»")); });
		// ═══ D. Модальное окно ═══
		Add([](FRun& X) { X.WM->ShowGameConfirm(TEXT("самотест курсора мини-карты"), [](bool) {}); });
		Add([=](FRun& X) { Check(X, Cur(X) != kMove, TEXT("D1 модальное окно — курсор не «идти»")); X.WM->AnswerMsgBox(true, false); });
		Add([](FRun& X) { Move(X.Away); });
		// ═══ E. Чужой курсор интерфейса ═══
		Add([](FRun& X) { X.PC->bUiCursorOverride = true; X.PC->SetGameCursor(ECur::Hammer); Move(X.Over); });
		Add([](FRun& X) { Check(X, X.PC->CurrentCursor == ECur::Hammer && !X.Map->IsMiniCursorOverridden(), TEXT("E1 взведённый курсор (молот) над мини-картой не перебивается")); Move(X.Away); });
		Add([](FRun& X) { Check(X, X.PC->CurrentCursor == ECur::Hammer && X.PC->bUiCursorOverride, TEXT("E2 уход с поля чужой курсор не снимает")); X.PC->bUiCursorOverride = false; X.PC->SetGameCursor(ECur::Normal); });
		// ═══ F. Настоящее окно поверх поля: сумка, перетащенная за ручку (`SimulateDrag`, 20 кадров; шаг = 0,3 с) ═══
		static const FName kInv(TEXT("INVEN_WIN"));
		Add([](FRun& X) { if (X.Inv.IsValid() && !X.Inv->IsWindowOpen()) { X.Inv->OpenWindow(); } });
		Add([](FRun&) {});
		Add([](FRun& X)
		{
			const bool bGrab = GrabPoint(X, kInv, X.InvHome);
			Check(X, bGrab, TEXT("F0 сумка открыта, ручка есть"));
			if (bGrab) { X.WM->SimulateDrag(X.InvHome, X.Over + FVector2D(0.f, -30.f), 20); }
		});
		Add([](FRun&) {});
		Add([](FRun&) {});
		Add([](FRun&) {});
		Add([](FRun& X) { Move(X.Over + FVector2D(1.f, 0.f)); });
		Add([](FRun& X) { Move(X.Over); });
		Add([=](FRun& X)
		{
			const FName Top = X.WM->FindWindowCatchingAt(X.Over);
			UE_LOG(LogAsdaMinimapCursorTest, Log, TEXT("КУРСОР-МИНИКАРТЫ F: окно под точкой %s, наведение поля %d, курсор %d"), *Top.ToString(), X.Area.IsValid() && X.Area->IsHovered() ? 1 : 0, Cur(X));
			Check(X, Top == kInv, FString::Printf(TEXT("F1 сумка лежит поверх поля (под точкой %s)"), *Top.ToString()));
			Check(X, Cur(X) != kMove && !X.Map->IsMiniCursorOverridden(), FString::Printf(TEXT("F2 настоящее окно поверх мини-карты — курсор не «идти» (%d)"), Cur(X)));
			X.Keys->Set(TEXT("asda.UI.TopWindowPress"), 0);   // общий гейт нажатий тоже снят: иначе дефект не виден
			X.Keys->Set(TEXT("asda.Minimap.ClickTopWindowOnly"), 0);
			X.Clicks = X.Map->GetMapClickCount();
			X.WM->SimulateClickAt(X.Over, 2);
		});
		Add([](FRun& X)
		{
			Check(X, X.Map->GetMapClickCount() == X.Clicks + 1, TEXT("F4 ключи 0: клик по сумке проходит на поле — дефект воспроизведён"));
			X.Keys->Set(TEXT("asda.UI.TopWindowPress"), 1);
			X.Clicks = X.Map->GetMapClickCount();
			X.WM->SimulateClickAt(X.Over, 2);
		});
		Add([](FRun& X)
		{
			Check(X, X.Map->GetMapClickCount() == X.Clicks, TEXT("F4б общий гейт `asda.UI.TopWindowPress` без частного ключа держит клик сам"));
			X.Keys->Set(TEXT("asda.UI.TopWindowPress"), 0);
			X.Keys->Set(TEXT("asda.Minimap.ClickTopWindowOnly"), 1);
			X.Clicks = X.Map->GetMapClickCount();
			X.WM->SimulateClickAt(X.Over, 2);
		});
		Add([](FRun& X)
		{
			Check(X, X.Map->GetMapClickCount() == X.Clicks, TEXT("F5 клик по сумке над полем — поле его не получает"));
			X.Keys->Set(TEXT("asda.UI.TopWindowPress"), 1);
			FVector2D Grab;
			if (GrabPoint(X, kInv, Grab)) { X.WM->SimulateDrag(Grab, X.InvHome, 20); }
		});
		Add([](FRun&) {});
		Add([](FRun&) {});
		Add([](FRun&) {});
		Add([](FRun& X) { if (X.Inv.IsValid()) { X.Inv->CloseWindow(); } Move(X.Over + FVector2D(1.f, 0.f)); });
		Add([](FRun& X) { Move(X.Over); });
		Add([=](FRun& X)
		{
			Check(X, Cur(X) == kMove, TEXT("F3 сумка убрана — снова «идти»"));
			X.Clicks = X.Map->GetMapClickCount();
			X.WM->SimulateClickAt(X.Over, 2);
		});
		Add([](FRun& X) { Check(X, X.Map->GetMapClickCount() == X.Clicks + 1, TEXT("F6 сумка убрана — клик по полю принят")); Move(X.Away); });
		// ═══ G. Окно пополнения: порт его нигде не открывает, поэтому настоящий WBP открывается здесь; поле остаётся под мышью ═══
		static const FName kCash(TEXT("CashCharge_Win"));
		Add([](FRun& X)
		{
			X.WM->OpenWindowByPath(kCash, TEXT("/Game/AsdaAssets/UI/Player_UI/Premium/WBP_CashCharge_Win"), 0, ESlateVisibility::Collapsed);
			Move(X.Over);
		});
		Add([=](FRun& X)
		{
			Check(X, X.WM->IsOpen(kCash) && !X.WM->IsWindowVisible(kCash) && Cur(X) == kMove, FString::Printf(TEXT("G0 окно пополнения создано и скрыто — над полем «идти» (%d)"), Cur(X)));
			X.WM->SetWindowVisible(kCash, true);
			Move(X.Over + FVector2D(1.f, 0.f));
		});
		Add([](FRun& X) { Move(X.Over); });
		Add([=](FRun& X)
		{
			const UGameInstance* G = X.World.IsValid() ? X.World->GetGameInstance() : nullptr;
			Check(X, X.WM->IsWindowVisible(kCash) && X.Area.IsValid() && X.Area->IsHovered() && !AsdaUi::IsModalDispatchBlocked(G),
				TEXT("G1 окно пополнения показано, поле под мышью, модальности нет (условие проверяется отдельно от перекрытия)"));
			Check(X, Cur(X) != kMove && !X.Map->IsMiniCursorOverridden(), FString::Printf(TEXT("G2 открыто окно пополнения — курсор не «идти» (%d)"), Cur(X)));
			X.WM->CloseWindow(kCash);
		});
		Add([=](FRun& X) { Check(X, Cur(X) == kMove, TEXT("G3 окно пополнения закрыто — снова «идти»")); Move(X.Away); });
		Add([=](FRun& X) { Check(X, Cur(X) == kNormal || Cur(X) != kMove, TEXT("конец: курсор не остался «идти»")); });
		Pump(R);
	}));

#endif // ASDA_DEV_CONSOLE && !UE_BUILD_SHIPPING
