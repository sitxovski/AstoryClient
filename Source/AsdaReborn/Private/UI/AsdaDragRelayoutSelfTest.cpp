// Copyright (c) 2026 @sitxovski (Discord)
// ★ ОБЩИЙ САМОТЕСТ «ПЕРЕТАСКИВАНИЕ + СМЕНА РЕЖИМА» (25.10.2026, волна hair_buttons2): `asda.SelfTest.DragRelayout`.
//   ЗАЧЕМ. Класс дефекта, найденный на окне покупки 1559 (кнопки отрывались от рамки): код окна запоминает ИСХОДНЫЕ
//   смещения слотов и при смене режима ставит `смещение = исходное ± сдвиг`, а постановщик окна и перетаскивание игроком
//   (`ShiftTargetByKey`, `PlaceWindowKeepingSavedOffset`) двигают те же слоты корневого канваса. Стоит игроку перетащить
//   окно (или окну встать кодом) и сменить режим — узлы, переставленные «от базы», остаются на старом месте.
//   Самотест ловит это для ЛЮБОГО окна путём игрока, без знания его кода:
//     1) открыть окно, режимы A → B → A; снимок A0, затем B → снимок B0, затем A;
//     2) НАСТОЯЩЕЕ перетаскивание за ручку окна (`SimulateDrag`, −120/+150 px) → снимок AD; узлы, сдвинутые ровно на
//        вектор ручки, — «тянутые» (ручка может тянуть не всё окно, а свою панель: так у магазина бустеров);
//     3) B → снимок B1, A → снимок A1; закрыть, открыть, B → A → снимок A2 (сдвиг игрока применён при открытии);
//     4) A1 и A2 обязаны совпасть с AD (абсолютно, ±1,5 px); в B1 каждый узел — на месте из B0, сдвинутом на вектор
//        ручки, если он сам или его ближайший классифицированный предок «тянутый», иначе — без сдвига.
//   Расхождение = узел «забыл» перетаскивание. Печатаются первые расхождения с именами узлов.
//   АРГУМЕНТЫ (пробел внутри команды — `+`, несколько команд в шаге — через `~`):
//     win=<ид окна в реестре>  open=<команды>  a=<команды>  b=<команды>  [close=<команды>]  [handle=<N ручки>]
//     [settle=<с>]  [label=<метка>]  [grab=<доля ширины ручки, 0..1; умолч. 0.5>]
//     [dismiss=<окно>] — после открытия нажать (настоящим кликом) первую видимую кнопку этого окна-сообщения: модальное
//     сообщение (например, ONEBUT_MSGBOX у поиска отряда) запирает ввод, и перетаскивание иначе не дойдёт до окна.  `quit`
//   Пример: `asda.SelfTest.DragRelayout win=WARE_WIN open=asda.Warehouse.Open+1 a=asda.Warehouse.Button+keep
//            b=asda.Warehouse.Button+take close=asda.Warehouse.Close label=склад quit`
//   ★ 26.10.2026 (волна quest_close_btn): `a=-` / `b=-` — режимов у окна нет (проверяются только перетаскивание и
//     переоткрытие: ловит «открытие ставит узлы от авторской базы», как крестик окна заданий 700);
//     `drag=<dx>,<dy>` — свой вектор перетаскивания; без него вектор (−120, 150) РАЗВОРАЧИВАЕТСЯ по оси, если окно
//     ушло бы за край экрана (постановщик вернул бы его в экран — сдвиг 0, как у окна 700 у левого края на 1024×768);
//     `shot=<папка в Reports>` — снимки кадра после перетаскивания, в режиме B после него и после переоткрытия;
//     `reopen=place` — окно при КАЖДОМ открытии ставит своя формула [ОРИГИНАЛ] (панель пакетов 26500 центрируется —
//     `item_enhance.c:3571-3598`): после переоткрытия сверяется только «все узлы сдвинуты одной дельтой» (ни один не оторван);
//     `win=auto` — окно = то, что стало видно после `open=` (видимое новое окно с ручкой; для обхода ВСЕХ окон по командам
//     открытия, без знания их ид); без `handle=` берётся первая ЖИВАЯ ручка (её узел виден в снимке: ручка внутри
//     свёрнутой группы держит устаревший прямоугольник — так у окна 700 ручка 0 `Quest_Base2_111_DRAG` правой страницы).
//   Итог: «ПЕРЕТАСК-РЕЖИМ ИТОГ: ok=N fail=M [метка, экран]».
#include "CoreMinimal.h"
#include "AsdaDevConsole.h"

#if ASDA_DEV_CONSOLE && !UE_BUILD_SHIPPING

#include "UI/AsdaWindowSubsystem.h"
#include "UI/AsdaUiHelpers.h"
#include "Test/AsdaSelfTestNet.h"   // ★ R1: тонкий доступ самотестов к сети (вместо AsdaNetSubsystem.h)
#include "Test/AsdaSelfTestKit.h"
#include "Blueprint/UserWidget.h"
#include "Blueprint/WidgetTree.h"
#include "Components/PanelWidget.h"
#include "Components/Button.h"         // `dismiss=`: кнопка окна-сообщения
#include "Components/Widget.h"
#include "Containers/Ticker.h"
#include "Engine/Engine.h"
#include "Engine/GameInstance.h"
#include "Engine/GameViewportClient.h"
#include "Engine/World.h"
#include "HAL/IConsoleManager.h"
#include "HAL/PlatformTime.h"
#include "Misc/Paths.h"          // `shot=`: путь снимка
#include "UnrealClient.h"        // `shot=`: FScreenshotRequest

DEFINE_LOG_CATEGORY_STATIC(LogAsdaDragRelayoutTest, Log, All);

namespace AsdaDragRelayoutSelfTest
{
	// Имя команды.
	static const TCHAR* kCmd = TEXT("asda.SelfTest.DragRelayout");
	// Допуск совпадения прямоугольника, px Slate.
	static constexpr float kTol = 1.5f;
	// Вектор перетаскивания по умолчанию, px Slate (ось разворачивается, если окно ушло бы за край — `PickDrag`).
	static const FVector2D kDrag(-120.0, 150.0);

	// Снимок: имя узла → абсолютный прямоугольник (мин, макс) и цепочка имён предков (ближайший первым).
	struct FSnap
	{
		TMap<FString, FBox2D> Rect;            // имя → прямоугольник
		TMap<FString, TArray<FString>> Chain;  // имя → предки
		int32 Num() const { return Rect.Num(); }
	};

	// Шаги сценария.
	enum class EStep : uint8 { Open, Dismiss, A1, SnapFirst, B1, A2, SnapA0, B2, SnapB0, A3, Drag, DragWait, DragSettle, SnapAD, B3, SnapB1, A4, SnapA1, Close, Reopen, A5, SnapA2, Done };

	// Состояние прогона.
	struct FRun
	{
		TWeakObjectPtr<UWorld> World;          // мир
		TWeakObjectPtr<UGameInstance> GI;      // экземпляр игры
		bool bQuit = false;                    // выход после итога
		FName Win;                             // окно
		TArray<FString> Open, A, B, Close;     // команды шагов
		int32 Handle = 0;                      // номер ручки перетаскивания
		bool bHandleAuto = true;               // ★ 26.10.2026: `handle=` не задан — первая живая ручка (`PickHandle`)
		bool bAutoWin = false;                 // ★ 26.10.2026: `win=auto` — окно определяется после `open=`
		TSet<FName> VisibleBefore;             // ★ 26.10.2026: окна, видимые до `open=` (для `win=auto`)
		bool bReopenPlace = false;             // ★ 26.10.2026: `reopen=place` — при открытии окно ставит своя формула (сверка одной дельтой)
		double Settle = 0.6;                   // пауза после команд шага, с
		FString Label, Res;                    // метка и экран
		int32 Ok = 0, Fail = 0;                // счёт
		EStep Step = EStep::Open;              // текущий шаг
		double StepAt = 0.0;                   // когда можно делать следующий шаг
		FVector2D HandleBefore = FVector2D(-1.0, -1.0);   // ручка до перетаскивания
		bool bCaptureBusyAtStart = false;      // перехват отправок взведён до прогона (не наш)
		FString CaptureArmedAt;                // шаг, на котором перехват впервые оказался взведён (диагностика утечек)
		double GrabFx = 0.5;                   // точка хвата по ширине ручки (доля)
		FVector2D D = FVector2D::ZeroVector;   // фактический сдвиг ручки перетаскиванием
		FSnap First, A0, B0, AD;               // снимки: первый показ A, эталонные A/B (до перетаскивания), сразу после него
		FName Dismiss;                         // окно-сообщение, которое закрыть кликом после открытия (`dismiss=`)
		bool bReopened = false;                // окно уже переоткрыто (после `Dismiss` — в A5, а не в A1)
		bool bNoHandle = false;                // у окна нет ручки — перетаскивание не проверяется (только смена режима и переоткрытие)
		TSet<FString> Moved, Still;            // узлы, сдвинутые перетаскиванием на D / оставшиеся на месте (по A0 → AD)
		bool bDragSet = false;                 // вектор задан аргументом `drag=`
		FVector2D DragVec = kDrag;             // вектор перетаскивания (`drag=` или `PickDrag`)
		FString ShotDir;                       // папка снимков в Reports (`shot=`), пусто — без снимков
	};
	static TSharedPtr<FRun> GRun;

	// Проверка с записью в журнал.
	static void Expect(FRun& R, bool bOk, const FString& What, const FString& Detail = FString())
	{
		bOk ? ++R.Ok : ++R.Fail;
		if (bOk) { UE_LOG(LogAsdaDragRelayoutTest, Log, TEXT("ПЕРЕТАСК-РЕЖИМ ok   [%s %s] %s %s"), *R.Label, *R.Res, *What, *Detail); }
		else     { UE_LOG(LogAsdaDragRelayoutTest, Warning, TEXT("ПЕРЕТАСК-РЕЖИМ FAIL [%s %s] %s %s"), *R.Label, *R.Res, *What, *Detail); }
	}

	// Аргумент `key=` (пусто — нет).
	static FString Arg(const TArray<FString>& A, const TCHAR* Key)
	{
		const FString P = FString(Key) + TEXT("=");
		for (const FString& S : A) { if (S.StartsWith(P, ESearchCase::IgnoreCase)) { return S.Mid(P.Len()); } }
		return FString();
	}

	// Команды шага: `~` — разделитель команд, `+` — пробел.
	static TArray<FString> Cmds(const FString& S)
	{
		TArray<FString> Out;
		S.ParseIntoArray(Out, TEXT("~"), true);
		for (FString& C : Out) { C.ReplaceInline(TEXT("+"), TEXT(" ")); }
		Out.RemoveAll([](const FString& C) { return C.TrimStartAndEnd() == TEXT("-"); });   // `-` — шаг без команд (26.10.2026)
		return Out;
	}

	// Выполнить команды шага в мире прогона.
	static void Exec(FRun& R, const TArray<FString>& C)
	{
		UWorld* W = R.World.Get();
		for (const FString& S : C)
		{
			UE_LOG(LogAsdaDragRelayoutTest, Log, TEXT("ПЕРЕТАСК-РЕЖИМ [%s] команда: %s"), *R.Label, *S);
			if (GEngine && W) { GEngine->Exec(W, *S); }
		}
	}

	// Узел виден целиком по цепочке родителей.
	static bool ChainVisible(const UWidget* W)
	{
		for (const UWidget* P = W; P; P = P->GetParent()) { if (!P->IsVisible()) { return false; } }
		return true;
	}

	// Снимок видимых узлов окна (абсолютно) с цепочками предков. false — окна/ручки нет.
	static bool Snap(FRun& R, FSnap& Out)
	{
		Out = FSnap();
		UGameInstance* GI = R.GI.Get();
		UAsdaWindowSubsystem* WM = GI ? GI->GetSubsystem<UAsdaWindowSubsystem>() : nullptr;
		UUserWidget* Win = WM ? WM->FindWindow(R.Win) : nullptr;
		if (!Win || !Win->WidgetTree) { return false; }
		TArray<UWidget*> All;
		Win->WidgetTree->GetAllWidgets(All);
		for (const UWidget* W : All)
		{
			if (!W || !ChainVisible(W)) { continue; }
			FVector2D Mn, Mx;
			if (!AsdaUi::WidgetAbsRect(W, Mn, Mx) || (Mx - Mn).X < 1.0 || (Mx - Mn).Y < 1.0) { continue; }
			if (W == Win->WidgetTree->RootWidget) { continue; }   // корневой полноэкранный канвас не двигается никогда
			Out.Rect.Add(W->GetName(), FBox2D(Mn, Mx));
			TArray<FString>& C = Out.Chain.Add(W->GetName());
			for (const UWidget* P = W->GetParent(); P; P = P->GetParent()) { C.Add(P->GetName()); }
		}
		return Out.Num() > 0;
	}

	// ★ 26.10.2026: вектор перетаскивания, при котором окно (объединение видимых узлов снимка `S`) остаётся в экране
	//   (прямоугольник корневого полноэкранного канваса окна). Предпочтение — `kDrag`; ось без места в эту сторону
	//   разворачивается, без места в обе — берётся сторона с большим запасом (не больше `kDrag` по модулю).
	static FVector2D PickDrag(const FRun& R, const FSnap& S)
	{
		UGameInstance* GI = R.GI.Get();
		UAsdaWindowSubsystem* WM = GI ? GI->GetSubsystem<UAsdaWindowSubsystem>() : nullptr;
		UUserWidget* Win = WM ? WM->FindWindow(R.Win) : nullptr;
		FVector2D VMn, VMx;
		if (!Win || !Win->WidgetTree || !AsdaUi::WidgetAbsRect(Win->WidgetTree->RootWidget, VMn, VMx) || S.Num() == 0) { return kDrag; }
		FBox2D Foot(ForceInit);
		for (const TPair<FString, FBox2D>& Kv : S.Rect) { Foot += Kv.Value; }
		auto Axis = [](double Pref, double Lo, double Hi, double VLo, double VHi)
		{
			const double Room[2] = { (Lo - VLo) - 2.0, (VHi - Hi) - 2.0 };   // запас влево/вверх и вправо/вниз
			const int32 Want = Pref < 0.0 ? 0 : 1;
			const double Mag = FMath::Abs(Pref);
			if (Room[Want] >= Mag) { return Pref; }
			if (Room[1 - Want] >= Mag) { return -Pref; }
			const int32 Best = Room[0] >= Room[1] ? 0 : 1;
			const double M = FMath::Clamp(Room[Best], 0.0, Mag);
			return Best == 0 ? -M : M;
		};
		return FVector2D(Axis(kDrag.X, Foot.Min.X, Foot.Max.X, VMn.X, VMx.X), Axis(kDrag.Y, Foot.Min.Y, Foot.Max.Y, VMn.Y, VMx.Y));
	}

	// ★ 26.10.2026: `win=auto` — новое видимое окно с ручкой после `open=`. false — такого нет (окно не открылось офлайн).
	static bool ResolveAutoWin(FRun& R, UAsdaWindowSubsystem& WM)
	{
		TArray<FName> Cand;
		for (const FName& Id : WM.GetRegisteredWindowIds())
		{
			if (R.VisibleBefore.Contains(Id) || !WM.IsWindowVisible(Id)) { continue; }
			FVector2D Mn, Mx;
			bool bHandle = false;
			for (int32 i = 0; i < 8 && !bHandle; ++i) { bHandle = WM.DebugGetDragHandleRect(Id, i, Mn, Mx); }
			if (bHandle) { Cand.Add(Id); }
		}
		FString List;
		for (const FName& Id : Cand) { List += TEXT(" ") + Id.ToString(); }
		UE_LOG(LogAsdaDragRelayoutTest, Log, TEXT("ПЕРЕТАСК-РЕЖИМ [%s] win=auto: новых окон с ручкой %d:%s"), *R.Label, Cand.Num(), *List);
		if (Cand.Num() == 0) { return false; }
		R.Win = Cand[0];
		if (R.Label.IsEmpty() || R.Label == TEXT("auto")) { R.Label = R.Win.ToString(); }
		return true;
	}

	// ★ 26.10.2026: первая ЖИВАЯ ручка — её прямоугольник совпадает с видимым узлом снимка `S` (±0,5 px). Ручка в свёрнутой
	//   группе держит устаревший прямоугольник Slate, тянуть за неё нельзя. Нет живой — первая с прямоугольником.
	static int32 PickHandle(const FRun& R, UAsdaWindowSubsystem& WM, const FSnap& S)
	{
		int32 First = INDEX_NONE;
		for (int32 i = 0; i < 8; ++i)
		{
			FVector2D Mn, Mx;
			if (!WM.DebugGetDragHandleRect(R.Win, i, Mn, Mx)) { continue; }
			if (First == INDEX_NONE) { First = i; }
			for (const TPair<FString, FBox2D>& Kv : S.Rect)
			{
				if ((Kv.Value.Min - Mn).GetAbsMax() <= 0.5 && (Kv.Value.Max - Mx).GetAbsMax() <= 0.5) { return i; }
			}
		}
		return First == INDEX_NONE ? 0 : First;
	}

	// ★ 26.10.2026: снимок кадра `shot=` — Reports/<папка>/<метка>_<экран>_<этап>.png.
	static void Shot(const FRun& R, const TCHAR* Stage)
	{
		if (R.ShotDir.IsEmpty()) { return; }
		FString Name = FString::Printf(TEXT("%s_%s_%s"), *R.Label, *R.Res, Stage);
		Name.ReplaceCharInline(TEXT(' '), TEXT('_'));
		Name.ReplaceCharInline(TEXT('/'), TEXT('_'));
		Name.ReplaceCharInline(TEXT(':'), TEXT('_'));
		const FString Path = FPaths::ConvertRelativePathToFull(FPaths::ProjectDir() / TEXT("../Reports") / R.ShotDir / (Name + TEXT(".png")));
		FScreenshotRequest::RequestScreenshot(Path, /*bShowUI*/true, /*bAddFilenameSuffix*/false);
		UE_LOG(LogAsdaDragRelayoutTest, Log, TEXT("ПЕРЕТАСК-РЕЖИМ [%s] снимок: %s"), *R.Label, *Path);
	}

	// Ожидаемый сдвиг узла относительно снимка до перетаскивания: D — «тянутый» (сам или ближайший классифицированный
	//   предок), 0 — «на месте». false — не классифицирован (узел и его предки в режиме A не видны).
	static bool ExpectedShift(const FRun& R, const FSnap& S, const FString& Name, FVector2D& Out)
	{
		if (R.Moved.Contains(Name)) { Out = R.D; return true; }
		if (R.Still.Contains(Name)) { Out = FVector2D::ZeroVector; return true; }
		if (const TArray<FString>* C = S.Chain.Find(Name))
		{
			for (const FString& P : *C)
			{
				if (R.Moved.Contains(P)) { Out = R.D; return true; }
				if (R.Still.Contains(P)) { Out = FVector2D::ZeroVector; return true; }
			}
		}
		return false;
	}

	// Сверить снимок с эталоном: каждый общий узел = эталон + ожидаемый сдвиг (`bShifted` — по классу «тянутый»).
	static void Compare(FRun& R, const FSnap& Ref, const FSnap& Got, bool bShifted, const TCHAR* What)
	{
		int32 Common = 0, Bad = 0, Unknown = 0;
		FString Det;
		for (const TPair<FString, FBox2D>& Kv : Ref.Rect)
		{
			const FBox2D* G = Got.Rect.Find(Kv.Key);
			if (!G) { continue; }
			FVector2D Sh = FVector2D::ZeroVector;
			if (bShifted && !ExpectedShift(R, Got, Kv.Key, Sh)) { ++Unknown; continue; }
			++Common;
			const FVector2D DMn = G->Min - Kv.Value.Min - Sh, DMx = G->Max - Kv.Value.Max - Sh;
			if (DMn.GetAbsMax() > kTol || DMx.GetAbsMax() > kTol)
			{
				if (++Bad <= 8) { Det += FString::Printf(TEXT(" %s сдвиг (%.0f,%.0f) размер (%+.0f,%+.0f);"), *Kv.Key, DMn.X, DMn.Y, DMx.X - DMn.X, DMx.Y - DMn.Y); }
			}
		}
		Expect(R, Common > 0 && Bad == 0, What,
			FString::Printf(TEXT("(сверено узлов %d из %d/%d, без класса %d, расходятся %d%s)"), Common, Ref.Num(), Got.Num(), Unknown, Bad, *Det));
	}

	// ★ 26.10.2026 (`reopen=place`): все общие узлы `Got` сдвинуты относительно `Ref` ОДНОЙ дельтой (окно переставлено целиком,
	//   ни один узел не остался на старом месте и не уехал отдельно). Дельта — по первому общему узлу.
	static void CompareUniform(FRun& R, const FSnap& Ref, const FSnap& Got, const TCHAR* What)
	{
		bool bHave = false;
		FVector2D Dl = FVector2D::ZeroVector;
		int32 Common = 0, Bad = 0;
		FString Det;
		for (const TPair<FString, FBox2D>& Kv : Ref.Rect)
		{
			const FBox2D* G = Got.Rect.Find(Kv.Key);
			if (!G) { continue; }
			if (!bHave) { Dl = G->Min - Kv.Value.Min; bHave = true; }
			++Common;
			const FVector2D DMn = G->Min - Kv.Value.Min - Dl, DMx = G->Max - Kv.Value.Max - Dl;
			if (DMn.GetAbsMax() > kTol || DMx.GetAbsMax() > kTol)
			{
				if (++Bad <= 8) { Det += FString::Printf(TEXT(" %s отрыв (%.0f,%.0f);"), *Kv.Key, DMn.X, DMn.Y); }
			}
		}
		Expect(R, Common > 0 && Bad == 0, What, FString::Printf(TEXT("(узлов %d, общая дельта (%.0f,%.0f), оторваны %d%s)"), Common, Dl.X, Dl.Y, Bad, *Det));
	}

	// Конец: итог, выход.
	static bool Done()
	{
		FRun& R = *GRun;
		Exec(R, R.Close);
		AsdaSelfTestKit::Finish(R.World.Get(), kCmd, R.bQuit, LogAsdaDragRelayoutTest, TEXT("ПЕРЕТАСК-РЕЖИМ"), R.Ok, R.Fail,
			*FString::Printf(TEXT(" [%s, %s]"), *R.Label, *R.Res));
		GRun.Reset();
		return false;
	}

	// Тик сценария.
	static bool OnTick(float)
	{
		if (!GRun.IsValid()) { return false; }
		FRun& R = *GRun;
		const double Now = FPlatformTime::Seconds();
		if (Now < R.StepAt) { return true; }
		UGameInstance* GI = R.GI.Get();
		UAsdaWindowSubsystem* WM = GI ? GI->GetSubsystem<UAsdaWindowSubsystem>() : nullptr;
		if (!WM) { ++R.Fail; return Done(); }
		// Диагностика утечки перехвата отправок: на каком шаге он впервые оказался взведён.
		if (!R.bCaptureBusyAtStart && R.CaptureArmedAt.IsEmpty())
		{
			if (const UAsdaNetSubsystem* Net = AsdaTestNet::Get(GI))
			{
				if (AsdaSelfTestKit::IsCaptureBusy(Net)) { R.CaptureArmedAt = FString::Printf(TEXT("шаг %d"), (int32)R.Step); }
			}
		}
		auto Next = [&R, Now](EStep S, double Wait) { R.Step = S; R.StepAt = Now + Wait; return true; };
		switch (R.Step)
		{
			case EStep::Open:   Exec(R, R.Open); return Next(EStep::Dismiss, R.Settle * 2.0);
			case EStep::Dismiss:
			{
				const EStep After = R.bReopened ? EStep::A5 : EStep::A1;   // сообщение показывается и при переоткрытии
				if (R.bAutoWin && R.Win.IsNone() && !ResolveAutoWin(R, *WM))
				{
					Expect(R, false, TEXT("win=auto: после open= видно новое окно с ручкой перетаскивания"), TEXT("(не открылось — офлайн-гейт или нет ручки)"));
					return Next(EStep::Done, 0.0);
				}
				if (R.Dismiss.IsNone()) { return Next(After, 0.0); }
				if (WM->IsSimClickPending()) { return true; }
				UUserWidget* Box = WM->FindWindow(R.Dismiss);
				const UButton* Btn = nullptr;
				if (Box && Box->WidgetTree)
				{
					Box->WidgetTree->ForEachWidget([&Btn](UWidget* X) { if (!Btn && X && X->IsA<UButton>() && ChainVisible(X)) { Btn = Cast<UButton>(X); } });
				}
				FVector2D Mn, Mx;
				if (Btn && AsdaUi::WidgetAbsRect(Btn, Mn, Mx))
				{
					UE_LOG(LogAsdaDragRelayoutTest, Log, TEXT("ПЕРЕТАСК-РЕЖИМ [%s] закрываю сообщение %s кнопкой %s"), *R.Label, *R.Dismiss.ToString(), *Btn->GetName());
					WM->SimulateClickAt((Mn + Mx) * 0.5, /*HoldFrames*/ 2, /*bRight*/ false);
				}
				else { UE_LOG(LogAsdaDragRelayoutTest, Log, TEXT("ПЕРЕТАСК-РЕЖИМ [%s] сообщения %s нет — закрывать нечего"), *R.Label, *R.Dismiss.ToString()); }
				return Next(After, R.Settle);
			}
			case EStep::A1:     Exec(R, R.A);    return Next(EStep::SnapFirst, R.Settle);
			case EStep::SnapFirst: Snap(R, R.First); return Next(EStep::B1, 0.0);
			case EStep::B1:     Exec(R, R.B);    return Next(EStep::A2, R.Settle);
			case EStep::A2:     Exec(R, R.A);    return Next(EStep::SnapA0, R.Settle);
			case EStep::SnapA0:
				if (!Snap(R, R.A0))
				{
					FVector2D HMn, HMx;
					const bool bWin = WM->FindWindow(R.Win) != nullptr;
					const bool bHandle = WM->DebugGetDragHandleRect(R.Win, R.Handle, HMn, HMx);
					Expect(R, false, TEXT("окно и ручка перетаскивания есть, узлы видны"),
						FString::Printf(TEXT("(окно %s, ручка %s)"), bWin ? TEXT("есть") : TEXT("НЕТ"), bHandle ? TEXT("есть") : TEXT("НЕТ")));
					UE_LOG(LogAsdaDragRelayoutTest, Log, TEXT("ПЕРЕТАСК-РЕЖИМ ручки окон: %s"),*WM->DebugDragHandles());
					return Done();
				}
				// Без перетаскивания: A после B → A обязан совпасть с первым показом A (ловит «база снята до постановки окна»).
				if (R.First.Num() > 0) { Compare(R, R.First, R.A0, /*bShifted*/ false, TEXT("режим A после B → A = первый показ A (без перетаскивания)")); }
				return Next(EStep::B2, 0.0);
			case EStep::B2:     Exec(R, R.B);    return Next(EStep::SnapB0, R.Settle);
			case EStep::SnapB0: Snap(R, R.B0);   return Next(EStep::A3, 0.0);
			case EStep::A3:     Exec(R, R.A);    return Next(EStep::Drag, R.Settle);
			case EStep::Drag:
			{
				FVector2D HMn, HMx;
				if (R.bHandleAuto)
				{
					R.Handle = PickHandle(R, *WM, R.A0);   // ★ 26.10.2026: живая ручка (не в свёрнутой группе)
					R.bHandleAuto = false;
				}
				if (!WM->DebugGetDragHandleRect(R.Win, R.Handle, HMn, HMx))
				{
					// Окно без ручки игрок не тянет — проверяем только переоткрытие.
					UE_LOG(LogAsdaDragRelayoutTest, Log, TEXT("ПЕРЕТАСК-РЕЖИМ [%s] у окна нет ручки %d — перетаскивание не проверяется"), *R.Label, R.Handle);
					R.bNoHandle = true;
					return Next(EStep::Close, 0.0);
				}
				R.HandleBefore = HMn;
				// Хват — у ВЕРХНЕГО края ручки (полоса заголовка): середина ручки на всё окно попадает в содержимое,
				//   которое съедает нажатие (так же берётся окно 1559 в `asda.SelfTest.PurchaseBoxButtons`).
				const FVector2D From(HMn.X + (HMx.X - HMn.X) * R.GrabFx, HMn.Y + FMath::Min(8.0, (HMx.Y - HMn.Y) * 0.5));
				if (!R.bDragSet) { R.DragVec = PickDrag(R, R.A0); }   // ★ 26.10.2026: окно не уводится за край экрана
				UE_LOG(LogAsdaDragRelayoutTest, Log, TEXT("ПЕРЕТАСК-РЕЖИМ [%s] ручка %d: (%.0f,%.0f)-(%.0f,%.0f), вектор (%.0f,%.0f)"), *R.Label, R.Handle,
					HMn.X, HMn.Y, HMx.X, HMx.Y, R.DragVec.X, R.DragVec.Y);
				WM->SimulateDrag(From, From + R.DragVec, /*HoldFrames*/ 12);   // курсор ведётся 12 кадров: окна опрашивают мышь в своём Tick
				return Next(EStep::DragWait, 0.1);
			}
			case EStep::DragWait:
			{
				if (WM->IsSimClickPending()) { return true; }
				return Next(EStep::DragSettle, R.Settle);   // окно применяет сдвиг на своём тике после отпускания — выждать отрисовку
			}
			case EStep::DragSettle:
			{
				FVector2D HMn, HMx;
				WM->DebugGetDragHandleRect(R.Win, R.Handle, HMn, HMx);
				R.D = HMn - R.HandleBefore;
				Expect(R, R.D.Size() >= 50.0, TEXT("окно сдвинуто перетаскиванием за ручку"), FString::Printf(TEXT("(сдвиг %.0f,%.0f)"), R.D.X, R.D.Y));
				if (R.D.Size() < 50.0)
				{
					UE_LOG(LogAsdaDragRelayoutTest, Log, TEXT("ПЕРЕТАСК-РЕЖИМ ручки окон: %s"), *WM->DebugDragHandles());
					return Next(EStep::Done, 0.0);   // без сдвига сверять нечего
				}
				return Next(EStep::SnapAD, R.Settle);
			}
			case EStep::SnapAD:
			{
				// Классы узлов по A0 → AD: сдвинут на D — «тянутый», на 0 — «на месте», иначе — странный (клампы и т. п.).
				Snap(R, R.AD);
				Shot(R, TEXT("1_drag"));
				int32 Odd = 0;
				for (const TPair<FString, FBox2D>& Kv : R.A0.Rect)
				{
					const FBox2D* G = R.AD.Rect.Find(Kv.Key);
					if (!G) { continue; }
					const FVector2D Dl = G->Min - Kv.Value.Min;
					if ((Dl - R.D).GetAbsMax() <= kTol) { R.Moved.Add(Kv.Key); }
					else if (Dl.GetAbsMax() <= kTol) { R.Still.Add(Kv.Key); }
					else { ++Odd; }
				}
				Expect(R, R.Moved.Num() > 0 && Odd == 0, TEXT("перетаскивание: узлы сдвинулись ровно на вектор ручки или остались"),
					FString::Printf(TEXT("(тянутых %d, на месте %d, прочих %d)"), R.Moved.Num(), R.Still.Num(), Odd));
				return Next(EStep::B3, 0.0);
			}
			case EStep::B3:     Exec(R, R.B);    return Next(EStep::SnapB1, R.Settle);
			case EStep::SnapB1:
			{
				FSnap B1;
				if (R.B0.Num() > 0 && Snap(R, B1)) { Compare(R, R.B0, B1, /*bShifted*/ true, TEXT("режим B после перетаскивания = режим B до + сдвиг тянутых")); }
				Shot(R, TEXT("2_modeB"));
				return Next(EStep::A4, 0.0);
			}
			case EStep::A4:     Exec(R, R.A);    return Next(EStep::SnapA1, R.Settle);
			case EStep::SnapA1:
			{
				FSnap A1;
				if (Snap(R, A1)) { Compare(R, R.AD, A1, /*bShifted*/ false, TEXT("режим A после перетаскивания и B = сразу после перетаскивания")); }
				else { Expect(R, false, TEXT("снимок A после перетаскивания")); }
				return Next(EStep::Close, 0.0);
			}
			case EStep::Close:
				if (R.Close.Num() == 0) { return Next(EStep::Done, 0.0); }
				Exec(R, R.Close); return Next(EStep::Reopen, R.Settle);
			case EStep::Reopen: Exec(R, R.Open); R.bReopened = true; return Next(EStep::Dismiss, R.Settle * 2.0);
			case EStep::A5:     Exec(R, R.B); Exec(R, R.A); return Next(EStep::SnapA2, R.Settle);
			case EStep::SnapA2:
			{
				FSnap A2;
				if (Snap(R, A2))
				{
					Shot(R, TEXT("3_reopen"));
					if (R.bNoHandle) { Compare(R, R.A0, A2, /*bShifted*/ false, TEXT("переоткрыто → B → A = режим A до")); }
					else if (R.bReopenPlace) { CompareUniform(R, R.AD, A2, TEXT("переоткрыто (окно ставит своя формула [ОРИГИНАЛ]) → узлы одной дельтой, ни один не оторван")); }
					else { Compare(R, R.AD, A2, /*bShifted*/ false, TEXT("переоткрыто (сдвиг игрока из раскладки) → B → A = сразу после перетаскивания")); }
				}
				else { Expect(R, false, TEXT("снимок A после переоткрытия")); }
				return Next(EStep::Done, 0.0);
			}
			default:
				if (!R.bCaptureBusyAtStart)
				{
					Expect(R, R.CaptureArmedAt.IsEmpty(), TEXT("перехват отправок не взведён за прогон"),
						R.CaptureArmedAt.IsEmpty() ? FString() : FString::Printf(TEXT("(впервые взведён: %s)"), *R.CaptureArmedAt));
				}
				return Done();
		}
	}

	// Запуск.
	static void Run(const TArray<FString>& A, UWorld* W)
	{
		const FAsdaSelfTestArgs Args = FAsdaSelfTestArgs::Parse(A);
		if (AsdaSelfTestKit::RefuseStart(W, kCmd, Args)) { return; }
		UGameInstance* GI = W ? W->GetGameInstance() : nullptr;
		if (!GI || !GI->GetSubsystem<UAsdaWindowSubsystem>()) { AsdaSelfTestKit::Refused(W, kCmd, Args.bQuit, TEXT("нет подсистемы окон")); return; }
		if (GRun.IsValid()) { AsdaSelfTestKit::Refused(W, kCmd, Args.bQuit, TEXT("самотест уже идёт")); return; }
		const FString WinS = Arg(A, TEXT("win"));
		if (WinS.IsEmpty() || Arg(A, TEXT("open")).IsEmpty() || Arg(A, TEXT("a")).IsEmpty() || Arg(A, TEXT("b")).IsEmpty())
		{
			AsdaSelfTestKit::Refused(W, kCmd, Args.bQuit, TEXT("нужны win= open= a= b="));
			return;
		}
		GRun = MakeShared<FRun>();
		FRun& R = *GRun;
		R.World = W; R.GI = GI; R.bQuit = Args.bQuit;
		R.Win = FName(*WinS);
		R.Open = Cmds(Arg(A, TEXT("open"))); R.A = Cmds(Arg(A, TEXT("a"))); R.B = Cmds(Arg(A, TEXT("b"))); R.Close = Cmds(Arg(A, TEXT("close")));
		R.Handle = FCString::Atoi(*Arg(A, TEXT("handle")));
		R.bHandleAuto = Arg(A, TEXT("handle")).IsEmpty() || Arg(A, TEXT("handle")) == TEXT("auto");
		if (WinS.Equals(TEXT("auto"), ESearchCase::IgnoreCase))
		{
			// ★ 26.10.2026: `win=auto` — запомнить видимые окна до `open=`.
			R.bAutoWin = true; R.Win = NAME_None;
			UAsdaWindowSubsystem* WM0 = GI->GetSubsystem<UAsdaWindowSubsystem>();
			for (const FName& Id : WM0->GetRegisteredWindowIds()) { if (WM0->IsWindowVisible(Id)) { R.VisibleBefore.Add(Id); } }
		}
		if (!Arg(A, TEXT("dismiss")).IsEmpty()) { R.Dismiss = FName(*Arg(A, TEXT("dismiss"))); }
		if (!Arg(A, TEXT("grab")).IsEmpty()) { R.GrabFx = FMath::Clamp(FCString::Atod(*Arg(A, TEXT("grab"))), 0.0, 1.0); }
		if (!Arg(A, TEXT("drag")).IsEmpty())
		{
			TArray<FString> P; Arg(A, TEXT("drag")).ParseIntoArray(P, TEXT(","), true);
			if (P.Num() == 2) { R.DragVec = FVector2D(FCString::Atod(*P[0]), FCString::Atod(*P[1])); R.bDragSet = true; }
		}
		R.ShotDir = Arg(A, TEXT("shot"));
		R.bReopenPlace = Arg(A, TEXT("reopen")).Equals(TEXT("place"), ESearchCase::IgnoreCase);
		if (!Arg(A, TEXT("settle")).IsEmpty()) { R.Settle = FMath::Max(0.2, FCString::Atod(*Arg(A, TEXT("settle")))); }
		R.Label = Arg(A, TEXT("label")); if (R.Label.IsEmpty()) { R.Label = WinS; }
		FVector2D V(0.0, 0.0);
		if (UGameViewportClient* VC = W->GetGameViewport()) { VC->GetViewportSize(V); }
		R.Res = FString::Printf(TEXT("%.0fx%.0f"), V.X, V.Y);
		if (const UAsdaNetSubsystem* Net = AsdaTestNet::Get(GI)) { R.bCaptureBusyAtStart = AsdaSelfTestKit::IsCaptureBusy(Net); }
		UE_LOG(LogAsdaDragRelayoutTest, Log, TEXT("ПЕРЕТАСК-РЕЖИМ старт [%s %s]: окно %s, перехват до прогона %s"), *R.Label, *R.Res, *WinS,
			R.bCaptureBusyAtStart ? TEXT("ВЗВЕДЁН (чужой)") : TEXT("свободен"));
		R.StepAt = FPlatformTime::Seconds();
		FTSTicker::GetCoreTicker().AddTicker(FTickerDelegate::CreateStatic(&OnTick), 0.05f);
	}
}

// ★ ПРОБА «КТО ЛЕЖИТ В КОРНЕВОМ КАНВАСЕ» (25.10.2026): `asda.SelfTest.ProbeRootParents win=<окно> prefix=<префикс> [soulsearch]`.
//   Узел, который код окна ставит «от исходного смещения», уязвим к перетаскиванию, только если он — прямой ребёнок
//   корневого канваса (его двигают `ShiftTargetByKey`/постановщик). Проба печатает для узлов с префиксом их родителя
//   и пометку КОРЕНЬ/группа. `soulsearch` — сначала построить окно поиска Друга 8200 (офлайн его поднимает только 5485).
#include "UI/AsdaSoulmateSubsystem.h"
static FAutoConsoleCommandWithWorldAndArgs GAsdaSelfTestProbeRootParents(
	TEXT("asda.SelfTest.ProbeRootParents"),
	TEXT("Проба: родители узлов окна с префиксом (КОРЕНЬ — узел двигается перетаскиванием окна). win=<окно> prefix=<префикс> [soulsearch]."),
	FConsoleCommandWithWorldAndArgsDelegate::CreateLambda([](const TArray<FString>& A, UWorld* W)
	{
		UGameInstance* GI = W ? W->GetGameInstance() : nullptr;
		UAsdaWindowSubsystem* WM = GI ? GI->GetSubsystem<UAsdaWindowSubsystem>() : nullptr;
		if (!WM) { return; }
		if (A.Contains(TEXT("soulsearch")))
		{
			if (UAsdaSoulmateSubsystem* S = GI->GetSubsystem<UAsdaSoulmateSubsystem>()) { S->OpenSearchWindow(); }
		}
		const FString WinS = AsdaDragRelayoutSelfTest::Arg(A, TEXT("win")), Pfx = AsdaDragRelayoutSelfTest::Arg(A, TEXT("prefix"));
		UUserWidget* Win = WM->FindWindow(FName(*WinS));
		if (!Win || !Win->WidgetTree) { UE_LOG(LogAsdaDragRelayoutTest, Warning, TEXT("ПРОБА-КОРЕНЬ: окна %s нет"), *WinS); return; }
		const UWidget* Root = Win->WidgetTree->RootWidget;
		int32 N = 0, InRoot = 0;
		Win->WidgetTree->ForEachWidget([&](UWidget* X)
		{
			if (!X || !X->GetName().StartsWith(Pfx)) { return; }
			const UWidget* P = X->GetParent();
			const bool bRoot = (P == Root);
			++N; InRoot += bRoot ? 1 : 0;
			UE_LOG(LogAsdaDragRelayoutTest, Log, TEXT("ПРОБА-КОРЕНЬ %s: %s ← %s%s"), *WinS, *X->GetName(), P ? *P->GetName() : TEXT("-"), bRoot ? TEXT(" [КОРЕНЬ]") : TEXT(""));
		});
		FVector2D HMn, HMx;
		UE_LOG(LogAsdaDragRelayoutTest, Log, TEXT("ПРОБА-КОРЕНЬ %s итог: узлов «%s*» %d, из них в корне %d; ручка перетаскивания %s"), *WinS, *Pfx, N, InRoot,
			WM->DebugGetDragHandleRect(FName(*WinS), 0, HMn, HMx) ? TEXT("есть") : TEXT("нет (или окно не отрисовано)"));
		AsdaSelfTestKit::Finish(W, TEXT("asda.SelfTest.ProbeRootParents"), A.Contains(TEXT("quit")), LogAsdaDragRelayoutTest,
			TEXT("ПРОБА-КОРЕНЬ"), N > 0 ? 1 : 0, N > 0 ? 0 : 1);
	}));

static FAutoConsoleCommandWithWorldAndArgs GAsdaSelfTestDragRelayout(
	TEXT("asda.SelfTest.DragRelayout"),
	TEXT("Самотест: окно не «забывает» перетаскивание игроком при смене режима. win=<окно> open=<команды> a=<команды> b=<команды> "
	     "[close=<команды>] [handle=N] [settle=с] [label=метка] [drag=dx,dy] [shot=папка] [quit]; пробел в команде — `+`, "
		     "несколько команд — через `~`; a=- / b=- — режимов нет."),
	FConsoleCommandWithWorldAndArgsDelegate::CreateLambda([](const TArray<FString>& A, UWorld* W)
	{
		AsdaDragRelayoutSelfTest::Run(A, W);
	}));

// ★ САМОТЕСТ «СКРЫТАЯ РУЧКА НЕ ТЯНЕТ ОКНО» (26.10.2026, волна quest_close_btn, доделка): `asda.SelfTest.HiddenHandleDrag [quit]`.
//   Ключ `asda.UI.DragHandleChainVisible` (AsdaWindowSubsystem.cpp): ручка ловит нажатие, только если видна вся цепочка
//   родителей — эталон `uiControlHitTest@0x12DB470` (ui_core.c:107180-107193). Сценарий путём игрока на окне заданий 700:
//     1) открыть, развернуть страницу 732 (ручка 0 `Quest_Base2_111_DRAG` получает прямоугольник), свернуть — ручка 0 скрыта
//        родителем, но её прямоугольник Slate остаётся прежним;
//     2) перетащить окно за ВИДИМУЮ ручку (вверх) — скрытая ручка остаётся на старом месте, её полоса ложится на тело окна;
//     3) найти в полосе скрытой ручки точку, где нажатие ловит окно 700, нет контрола с захватом и нет видимой ручки;
//     4) настоящим перетаскиванием из этой точки — окно НЕ сдвигается; 5) за видимую ручку — сдвигается на вектор.
//   С ключом 0 шаг 4 обязан дать FAIL (так ловится регресс). Итог: «СКРЫТАЯ-РУЧКА ИТОГ: ok=N fail=M [экран]».
namespace AsdaHiddenHandleTest
{
	static const TCHAR* kCmd = TEXT("asda.SelfTest.HiddenHandleDrag");   // имя самотеста
	static const FName kWin(TEXT("QUEST_WIN"));                          // окно заданий 700
	static constexpr int32 kHidden = 0;                                  // ручка правой страницы (в свёрнутом виде скрыта)
	static constexpr int32 kVisible = 1;                                 // ручка заголовка `QUEST_WIN_MOVE_118_DRAG`

	// Состояние прогона.
	struct FRun
	{
		TWeakObjectPtr<UWorld> World;           // мир
		TWeakObjectPtr<UGameInstance> GI;       // экземпляр игры
		bool bQuit = false;                     // выход после итога
		int32 Ok = 0, Fail = 0;                 // счёт
		int32 Step = 0;                         // шаг сценария
		double StepAt = 0.0;                    // когда можно делать шаг
		FString Res;                            // экран
		FVector2D VisBefore = FVector2D::ZeroVector;   // видимая ручка до перетаскивания
		FVector2D Probe = FVector2D(-1.0, -1.0);       // точка в полосе скрытой ручки
		FVector2D Vec = FVector2D::ZeroVector;         // вектор контрольного перетаскивания
	};
	static TSharedPtr<FRun> GRun;

	// Проверка с записью в журнал.
	static void Expect(FRun& R, bool bOk, const FString& What, const FString& Detail = FString())
	{
		bOk ? ++R.Ok : ++R.Fail;
		if (bOk) { UE_LOG(LogAsdaDragRelayoutTest, Log, TEXT("СКРЫТАЯ-РУЧКА ok   [%s] %s %s"), *R.Res, *What, *Detail); }
		else     { UE_LOG(LogAsdaDragRelayoutTest, Warning, TEXT("СКРЫТАЯ-РУЧКА FAIL [%s] %s %s"), *R.Res, *What, *Detail); }
	}

	// Консольная команда в мире прогона.
	static void Exec(FRun& R, const TCHAR* C)
	{
		UE_LOG(LogAsdaDragRelayoutTest, Log, TEXT("СКРЫТАЯ-РУЧКА команда: %s"), C);
		if (GEngine && R.World.IsValid()) { GEngine->Exec(R.World.Get(), C); }
	}

	// Конец: закрыть окно, итог, выход.
	static bool Done()
	{
		FRun& R = *GRun;
		Exec(R, TEXT("Asda.QuestLog.Open 0"));
		AsdaSelfTestKit::Finish(R.World.Get(), kCmd, R.bQuit, LogAsdaDragRelayoutTest, TEXT("СКРЫТАЯ-РУЧКА"), R.Ok, R.Fail,
			*FString::Printf(TEXT(" [%s]"), *R.Res));
		GRun.Reset();
		return false;
	}

	// Тик сценария.
	static bool OnTick(float)
	{
		if (!GRun.IsValid()) { return false; }
		FRun& R = *GRun;
		const double Now = FPlatformTime::Seconds();
		if (Now < R.StepAt) { return true; }
		UGameInstance* GI = R.GI.Get();
		UAsdaWindowSubsystem* WM = GI ? GI->GetSubsystem<UAsdaWindowSubsystem>() : nullptr;
		if (!WM) { ++R.Fail; return Done(); }
		if (WM->IsSimClickPending()) { return true; }
		auto Next = [&R, Now](double Wait) { ++R.Step; R.StepAt = Now + Wait; return true; };
		FVector2D HMn, HMx, VMn, VMx;
		switch (R.Step)
		{
			case 0: Exec(R, TEXT("Asda.QuestLog.FakeJournal 4")); Exec(R, TEXT("Asda.QuestLog.Open 1")); return Next(1.2);
			case 1: Exec(R, TEXT("Asda.QuestLog.Page 1")); return Next(0.6);   // правая страница видна — ручка 0 размечена
			case 2: Exec(R, TEXT("Asda.QuestLog.Page 0")); return Next(0.6);   // свёрнута — ручка 0 скрыта родителем
			case 3:
			{
				const bool bH = WM->DebugGetDragHandleRect(kWin, kHidden, HMn, HMx);
				const bool bV = WM->DebugGetDragHandleRect(kWin, kVisible, VMn, VMx);
				Expect(R, bH && bV, TEXT("у окна 700 есть прямоугольники скрытой (0) и видимой (1) ручки"),
					FString::Printf(TEXT("(0: (%.0f,%.0f)-(%.0f,%.0f); 1: (%.0f,%.0f)-(%.0f,%.0f))"), HMn.X, HMn.Y, HMx.X, HMx.Y, VMn.X, VMn.Y, VMx.X, VMx.Y));
				if (!bH || !bV) { return Done(); }
				// Окно — вверх за видимую ручку, чтобы полоса скрытой ручки легла на тело окна.
				const double Up = FMath::Min(40.0, VMn.Y - 4.0);
				const FVector2D From(VMn.X + (VMx.X - VMn.X) * 0.3, VMn.Y + 8.0);
				R.VisBefore = VMn;
				WM->SimulateDrag(From, From + FVector2D(0.0, -Up), /*HoldFrames*/ 12);
				return Next(0.8);
			}
			case 4:
			{
				WM->DebugGetDragHandleRect(kWin, kVisible, VMn, VMx);
				const FVector2D D = VMn - R.VisBefore;
				Expect(R, D.Y < -10.0, TEXT("окно поднято за видимую ручку"), FString::Printf(TEXT("(сдвиг %.0f,%.0f)"), D.X, D.Y));
				WM->DebugGetDragHandleRect(kWin, kHidden, HMn, HMx);
				// Точка в полосе скрытой ручки: нажатие ловит окно 700, под ней нет контрола с захватом и нет видимой ручки.
				for (double Y = HMn.Y + 3.0; Y < HMx.Y - 2.0 && R.Probe.X < 0.0; Y += 4.0)
				{
					for (double X = HMn.X + 4.0; X < HMx.X - 4.0; X += 6.0)
					{
						const FVector2D P(X, Y);
						const bool bInVis = P.X >= VMn.X && P.X <= VMx.X && P.Y >= VMn.Y && P.Y <= VMx.Y;
						if (bInVis || WM->FindWindowCatchingAt(P) != kWin || WM->HasCapturingControlAt(kWin, P)) { continue; }
						R.Probe = P;
						break;
					}
				}
				Expect(R, R.Probe.X >= 0.0, TEXT("в полосе скрытой ручки есть точка тела окна без контрола и без видимой ручки"),
					FString::Printf(TEXT("(полоса (%.0f,%.0f)-(%.0f,%.0f), точка (%.0f,%.0f))"), HMn.X, HMn.Y, HMx.X, HMx.Y, R.Probe.X, R.Probe.Y));
				if (R.Probe.X < 0.0) { return Done(); }
				R.VisBefore = VMn;
				WM->SimulateDrag(R.Probe, R.Probe + FVector2D(60.0, 40.0), /*HoldFrames*/ 12);
				return Next(0.8);
			}
			case 5:
			{
				WM->DebugGetDragHandleRect(kWin, kVisible, VMn, VMx);
				const FVector2D D = VMn - R.VisBefore;
				Expect(R, D.GetAbsMax() <= 1.5, TEXT("нажатие в полосе СКРЫТОЙ ручки окно не двигает (эталон uiControlHitTest)"),
					FString::Printf(TEXT("(сдвиг %.0f,%.0f)"), D.X, D.Y));
				// Контроль: видимая ручка тянет окно на вектор.
				R.VisBefore = VMn;
				R.Vec = FVector2D(50.0, 30.0);
				const FVector2D From(VMn.X + (VMx.X - VMn.X) * 0.3, VMn.Y + 8.0);
				WM->SimulateDrag(From, From + R.Vec, /*HoldFrames*/ 12);
				return Next(0.8);
			}
			case 6:
			{
				WM->DebugGetDragHandleRect(kWin, kVisible, VMn, VMx);
				const FVector2D D = VMn - R.VisBefore;
				Expect(R, (D - R.Vec).GetAbsMax() <= 1.5, TEXT("видимая ручка тянет окно на вектор"),
					FString::Printf(TEXT("(сдвиг %.0f,%.0f, ждём %.0f,%.0f)"), D.X, D.Y, R.Vec.X, R.Vec.Y));
				return Done();
			}
			default: return Done();
		}
	}

	// Запуск.
	static void Run(const TArray<FString>& A, UWorld* W)
	{
		const FAsdaSelfTestArgs Args = FAsdaSelfTestArgs::Parse(A);
		if (AsdaSelfTestKit::RefuseStart(W, kCmd, Args)) { return; }
		UGameInstance* GI = W ? W->GetGameInstance() : nullptr;
		if (!GI || !GI->GetSubsystem<UAsdaWindowSubsystem>()) { AsdaSelfTestKit::Refused(W, kCmd, Args.bQuit, TEXT("нет подсистемы окон")); return; }
		if (GRun.IsValid()) { AsdaSelfTestKit::Refused(W, kCmd, Args.bQuit, TEXT("самотест уже идёт")); return; }
		GRun = MakeShared<FRun>();
		FRun& R = *GRun;
		R.World = W; R.GI = GI; R.bQuit = Args.bQuit;
		FVector2D V(0.0, 0.0);
		if (UGameViewportClient* VC = W->GetGameViewport()) { VC->GetViewportSize(V); }
		R.Res = FString::Printf(TEXT("%.0fx%.0f"), V.X, V.Y);
		R.StepAt = FPlatformTime::Seconds();
		FTSTicker::GetCoreTicker().AddTicker(FTickerDelegate::CreateStatic(&OnTick), 0.05f);
	}
}

static FAutoConsoleCommandWithWorldAndArgs GAsdaSelfTestHiddenHandleDrag(
	TEXT("asda.SelfTest.HiddenHandleDrag"),
	TEXT("Самотест: нажатие в полосе СКРЫТОЙ ручки окна 700 (правая страница свёрнута) окно не двигает, видимая ручка тянет. [quit]"),
	FConsoleCommandWithWorldAndArgsDelegate::CreateLambda([](const TArray<FString>& A, UWorld* W)
	{
		AsdaHiddenHandleTest::Run(A, W);
	}));

#endif // ASDA_DEV_CONSOLE && !UE_BUILD_SHIPPING
