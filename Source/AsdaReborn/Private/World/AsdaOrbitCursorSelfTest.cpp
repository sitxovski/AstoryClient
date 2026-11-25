// Copyright (c) 2026 @sitxovski (Discord)
// ★★★ САМОТЕСТ «КУРСОР ПРИ ЗАЖАТОЙ ПКМ» (07.11.2026, разбор — `Public/World/AsdaOrbitCursor.h`). [ИСПРАВЛЕНИЕ]
//
// ★ ЧТО ПРОВЕРЯЕТ (путь игрока: настоящие события мыши Slate → вьюпорт → `OnRightPressed` → тик контроллера → сторож):
//   A. Решение `Decide` по всем входам.
//   B. Контроль, ключ 0: курсор у края окна, дельты нет — поворот стоит (жалоба игроков), захвата нет.
//   C. Ключ 1: нажатие там же, захват, сырые отсчёты втрое шире экрана — камера повернулась, курсор в точке нажатия; скорость поворота
//      на пиксель равна прежней с учётом скорости указателя; отпускание — захват снят, курсор на месте.
//   D. Быстрый щелчок — захвата нет. E. Нажатие над окном интерфейса — захвата нет.
//   F. Alt+Tab при зажатой кнопке: захват снят, курсор не двигался; возврат фокуса — захват сам не встаёт.
//   G. Модальное окно при зажатой кнопке: сброс клавиш снимает поворот и захват. H. Режим Alt+Z: поворот с захватом, после отпускания
//      курсор остаётся скрытым своим видом. I. Смерть: камера живая, захват работает.
//   J. ★ 09.11.2026 Окно скрыто и показано посреди поворота (`RefreshModalInput` → `SetInputMode` → `ReleaseMouseCapture`): контроль ключом
//      `asda.Camera.OrbitHoldThrough 0` — захват снят, поворот стоит (жалоба владельца); ключ 1 — захват возвращён, камера крутится, курсор
//      скрыт в точке нажатия.
//   K. ★ 09.11.2026 Строка чата открыта и закрыта посреди поворота (фокус полю → сброс клавиш; закрытие → `SetInputMode`): контроль ключом 0
//      — поворот снят; ключ 1 — поворот и захват держатся, поворот возвращён после сброса клавиш (`GetRearmCount`).
//   L. ★ 09.11.2026 (ревизия п. 1) Строка чата, затем модальное окно в том же зажиме — поворот снят; строка чата, затем Alt+Tab — поворот
//      снят; после отпускания тик поворот не докладывает (`bRMBDown` не завис).
//   Окна ОС в прогоне нет (`-RenderOffscreen`): признаки окна подменяет `DebugOverrideWindow`, сырой режим и `ICursor::Show` — заглушки
//   платформы, настоящий курсор Windows здесь не проверяется.
// Консоль: `asda.Camera.SelfTestOrbitCursor [delay=N] [quit]`. Только офлайн. Строки — «САМОТЕСТ-КУРСОР-ПКМ», итог — «… ИТОГ: ok=… fail=…».
#include "CoreMinimal.h"

#if !UE_BUILD_SHIPPING

#include "AsdaCVarSave.h"
#include "AsdaNetSubsystem.h"
#include "AsdaOutgoingGate.h"                  // FAsdaSelfTestRunGate
#include "Test/AsdaSelfTestKit.h"
#include "Test/AsdaSelfTestSelfReady.h"
#include "UI/AsdaUiVisibilitySubsystem.h"      // Alt+Z
#include "UI/AsdaChatWindowSubsystem.h"        // ★ 09.11: строка чата посреди поворота
#include "UI/AsdaWindowSubsystem.h"            // модалка, хит-тест окон
#include "World/AsdaOrbitCursor.h"
#include "World/AsdaPlayerController.h"
#include "Engine/Engine.h"
#include "Engine/GameInstance.h"
#include "Engine/GameViewportClient.h"
#include "Slate/SceneViewport.h"
#include "Engine/World.h"
#include "Framework/Application/SlateApplication.h"
#include "GameFramework/InputSettings.h"
#include "HAL/IConsoleManager.h"
#include "TimerManager.h"
#include "Widgets/SWindow.h"

DEFINE_LOG_CATEGORY_STATIC(LogAsdaOrbitCursorTest, Log, All);

namespace AsdaOrbitCursorTest
{
	static const TCHAR* const kCmd = TEXT("asda.Camera.SelfTestOrbitCursor");   // имя самотеста
	static const TCHAR* const kTag = TEXT("САМОТЕСТ-КУРСОР-ПКМ");               // префикс строк журнала
	static const TCHAR* const kKey = TEXT("asda.Camera.OrbitCursorLock");       // ключ правки
	static const TCHAR* const kHoldKey = TEXT("asda.Camera.OrbitHoldThrough");  // ★ 09.11: ключ «поворот держится сквозь окна и фокус»
	constexpr int32 kRawSteps = 30;       // пачек сырых отсчётов
	constexpr int32 kRawStepPx = 100;     // отсчётов в пачке
	constexpr float kHold = 0.25f;        // ожидание захвата после нажатия, с
	constexpr float kPosTol = 1.5f;       // допуск положения курсора, px

	// Шаг сценария.
	//   ★ R5-fix6 (13.11.2026): шаг исполняется не раньше `Delay` секунд И не раньше `MinFrames` кадров игры после предыдущего шага;
	//   если задано `Until` — ещё и не раньше, чем оно станет истинным (потолок `kUntilCapSec`). Прежде шаги шли только по времени
	//   (0,03…0,25 с): при кадре 100 мс (три игры разом) между шагами не проходило ни одного тика контроллера и сторожа — нажатие,
	//   потерю фокуса и отпускание игра «видела» слитно, и тест краснел без дефекта игры. Смысл и число проверок прежние.
	//   ★ R5-fix7 (14.11.2026): по умолчанию 4 кадра, а не 2. Сторож игры считает доклад поворота живым ещё 2 кадра после
	//   последнего (`kReportFreshFrames`, AsdaOrbitCursor.cpp): отпускание кнопки контроллер видит кадром позже, «поворот снят» —
	//   ещё через два. При 60 кадрах/с прежние 0,15 с давали 9 кадров; при кадре 100 мс двух кадров не хватало — «отпускание:
	//   поворот снят» и точка нового нажатия (якорь) проверялись раньше, чем игра сняла прошлый поворот.
	struct FStep { float Delay; TFunction<void()> Fn; int32 MinFrames = 4; TFunction<bool()> Until; };
	constexpr int32 kUntilCapFrames = 8;  // потолок ожидания события шага: срок шага И столько кадров игры (ревизия: не секунды —
	                                      //   «захват встал за 0,25 с» остаётся проверкой срока, запас — только на длину кадра)

	// Состояние прогона.
	struct FState
	{
		TWeakObjectPtr<UWorld> World;                 // мир прогона
		FAsdaSelfTestRunGate Gate{ kCmd, false };     // офлайн-сторож
		bool bQuit = false;                           // выйти по итогу
		int32 Ok = 0, Fail = 0;                       // счётчики
		TArray<FStep> Steps;                          // сценарий
		int32 Idx = 0;                                // текущий шаг
		bool bSmoothWas = false;                      // сглаживание мыши до прогона
		float Yaw0 = 0.f;                             // угол камеры на начало замера
		FVector2D P = FVector2D::ZeroVector;          // точка нажатия случая
		int32 Count0 = 0;                             // счётчик захватов на начало случая
		float DegPerPx = 0.f;                         // поворот на пиксель курсора (контроль)
		FName ToggleId;                               // ★ 09.11: видимое окно, которое прячем и показываем посреди поворота
		int32 Recap0 = 0;                             // ★ 09.11: счётчик возвратов захвата на начало случая
		int32 Rearm0 = 0;                             // ★ 09.11: счётчик возвратов поворота на начало случая
		uint64 LastStepFrame = 0;                     // ★ R5-fix6: кадр игры, в котором исполнен предыдущий шаг
		double StepArmS = 0.0;                        // ★ R5-fix6: когда взведён текущий шаг (отсчёт потолка события)
		uint64 StepArmFrame = 0;                      // ★ R5-fix6: кадр игры, в котором взведён текущий шаг
		double ClickDownS = 0.0;                      // ★ R5-fix6: раздел D — когда нажата кнопка быстрого щелчка
		int32 ClickTry = 0;                           // ★ R5-fix6: раздел D — номер попытки быстрого щелчка
		bool bClickDone = false;                      // ★ R5-fix6: раздел D — быстрый щелчок получился (или попытки кончились)

		AAsdaPlayerController* Pc() const { return World.IsValid() ? Cast<AAsdaPlayerController>(World->GetFirstPlayerController()) : nullptr; }   // свой контроллер
		UGameInstance* GI() const { return World.IsValid() ? World->GetGameInstance() : nullptr; }   // экземпляр игры
		// Отметка проверки.
		void Expect(bool b, const FString& What)
		{
			if (b) { ++Ok; UE_LOG(LogAsdaOrbitCursorTest, Log, TEXT("%s [ok]   %s"), kTag, *What); }
			else   { ++Fail; UE_LOG(LogAsdaOrbitCursorTest, Error, TEXT("%s [FAIL] %s"), kTag, *What); }
		}
		// Насколько камера повернулась от `Yaw0`, градусы (0…180).
		float Turned() const { const AAsdaPlayerController* C = Pc(); return C ? FMath::Abs(FMath::FindDeltaAngleDegrees(Yaw0, C->GetCameraYawUE())) : -1.f; }
	};
	using FRef = TSharedRef<FState>;

	// Вьюпорт в абсолютных координатах.
	static bool ViewportAbs(FVector2D& Mn, FVector2D& Mx)
	{
		const UGameViewportClient* VP = GEngine ? GEngine->GameViewport.Get() : nullptr;
		const TSharedPtr<SWindow> Win = VP ? VP->GetWindow() : nullptr;
		if (!Win.IsValid()) { return false; }
		const FGeometry G = Win->GetWindowGeometryInScreen();
		Mn = FVector2D(G.GetAbsolutePosition());
		Mx = Mn + FVector2D(G.GetAbsoluteSize());
		return true;
	}
	// Точка по долям вьюпорта.
	static FVector2D Point(float Fx, float Fy)
	{
		FVector2D Mn, Mx;
		if (!ViewportAbs(Mn, Mx)) { return FVector2D::ZeroVector; }
		return FVector2D(Mn.X + (Mx.X - Mn.X) * Fx, Mn.Y + (Mx.Y - Mn.Y) * Fy);
	}
	// Правая точка сетки вне окон интерфейса (bOverUi false) или над окном (true). (−1,−1) — нет.
	static FVector2D FindPoint(const FState& S, bool bOverUi)
	{
		const UAsdaWindowSubsystem* WM = S.GI() ? S.GI()->GetSubsystem<UAsdaWindowSubsystem>() : nullptr;
		for (int32 Y = 2; Y < 30 && WM; ++Y)
		{
			for (int32 X = 39; X > 12; --X)
			{
				const FVector2D P = Point(X / 40.f, Y / 32.f);
				FName Hit;
				if (WM->IsScreenPointOverAnyUiWindow(P, Hit) == bOverUi) { return P; }
			}
		}
		return FVector2D(-1.0, -1.0);
	}
	// ★ 09.11.2026: события мыши — с индексом указателя мыши (`CursorPointerIndex`), как у настоящей мыши: захват вьюпорта и его снятие
	//   режимом ввода (`ProcessReply`, указатель 10) идут по этому индексу; прежде самотест слал указатель 0 и снятия захвата не видел.
	// Курсор в точку без кнопок.
	static void Hover(const FVector2D& P)
	{
		FSlateApplication& App = FSlateApplication::Get();
		App.SetCursorPos(P);
		App.ProcessMouseMoveEvent(FPointerEvent(ETouchIndex::CursorPointerIndex, P, P, TSet<FKey>(), EKeys::Invalid, 0.f, FModifierKeysState()));
	}
	// ПКМ вниз в точке P.
	static void Down(const FVector2D& P)
	{
		Hover(P);
		TSet<FKey> B; B.Add(EKeys::RightMouseButton);
		FSlateApplication::Get().ProcessMouseButtonDownEvent(nullptr, FPointerEvent(ETouchIndex::CursorPointerIndex, P, P, B, EKeys::RightMouseButton, 0.f, FModifierKeysState()));
	}
	// ПКМ вверх в текущей точке курсора.
	static void Up()
	{
		FSlateApplication& App = FSlateApplication::Get();
		const FVector2D P = App.GetCursorPos();
		App.ProcessMouseButtonUpEvent(FPointerEvent(ETouchIndex::CursorPointerIndex, P, P, TSet<FKey>(), EKeys::RightMouseButton, 0.f, FModifierKeysState()));
	}
	// Движение курсора при зажатой ПКМ из `From` в `To` (упёршийся курсор — одна и та же точка).
	static void Move(const FVector2D& From, const FVector2D& To)
	{
		TSet<FKey> B; B.Add(EKeys::RightMouseButton);
		FSlateApplication& App = FSlateApplication::Get();
		App.SetCursorPos(To);
		App.ProcessMouseMoveEvent(FPointerEvent(ETouchIndex::CursorPointerIndex, To, From, B, EKeys::Invalid, 0.f, FModifierKeysState()));
	}
	// Стоит ли курсор в точке P.
	static bool CursorAt(const FVector2D& P) { return FVector2D::Distance(FSlateApplication::Get().GetCursorPos(), P) <= kPosTol; }

	// Раздел A: таблица решений.
	static void CheckDecide(FState& S)
	{
		using FC = FAsdaOrbitCursor;
		FC::FInputs Good;
		Good.bOrbitReported = Good.bButtonDown = Good.bHasWindow = Good.bAppActive = Good.bForeground = Good.bViewportCaptured = true;
		Good.MovedPx = 10.f; Good.HeldSec = 0.01f;
		FString Why;
		S.Expect(FC::Decide(Good, false, Why), TEXT("A1 всё в порядке, курсор сдвинулся — захват"));
		auto Off = [&S, &Good](const TCHAR* Name, TFunction<void(FC::FInputs&)> Fn)
		{
			FC::FInputs In = Good; Fn(In);
			FString W;
			const bool b0 = FC::Decide(In, false, W), b1 = FC::Decide(In, true, W);
			S.Expect(!b0 && !b1, FString::Printf(TEXT("A2 %s — захвата нет и стоящий снимается (%s)"), Name, *W));
		};
		Off(TEXT("ключ 0"), [](FC::FInputs& I) { I.bKeyOn = false; });
		Off(TEXT("нет доклада тика"), [](FC::FInputs& I) { I.bOrbitReported = false; });
		Off(TEXT("кнопка отпущена"), [](FC::FInputs& I) { I.bButtonDown = false; });
		Off(TEXT("нет окна"), [](FC::FInputs& I) { I.bHasWindow = false; });
		Off(TEXT("приложение неактивно"), [](FC::FInputs& I) { I.bAppActive = false; });
		Off(TEXT("окно не на переднем плане"), [](FC::FInputs& I) { I.bForeground = false; });
		Off(TEXT("окно свёрнуто"), [](FC::FInputs& I) { I.bMinimized = true; });
		Off(TEXT("вьюпорт не держит мышь"), [](FC::FInputs& I) { I.bViewportCaptured = false; });
		FC::FInputs Wait = Good; Wait.MovedPx = 0.f; Wait.HeldSec = 0.f;
		S.Expect(!FC::Decide(Wait, false, Why), TEXT("A3 только что нажата, курсор на месте — ждём (щелчок не мигает)"));
		S.Expect(FC::Decide(Wait, true, Why), TEXT("A4 стоящий захват не зависит от сдвига (курсор стоит в точке нажатия)"));
		Wait.HeldSec = FC::kEngageHoldSec;
		S.Expect(FC::Decide(Wait, false, Why), TEXT("A5 удержание без сдвига — захват (курсор у края, сдвигаться некуда)"));
	}

	// ★ 09.11: видимое окно мира (не модальное — модалок в прогоне нет), чтобы спрятать и показать его настоящим путём.
	static FName FindToggleWindow(const FState& S)
	{
		const UAsdaWindowSubsystem* WM = S.GI() ? S.GI()->GetSubsystem<UAsdaWindowSubsystem>() : nullptr;
		if (!WM || WM->IsWorldInputCapturedByModal()) { return NAME_None; }
		for (const FName& Id : WM->GetRegisteredWindowIds()) { if (WM->IsWindowVisible(Id)) { return Id; } }
		return NAME_None;
	}
	// ★ 09.11: спрятать (bVisible false) или показать окно случая J.
	static void ToggleWindow(FState& S, bool bVisible)
	{
		if (UAsdaWindowSubsystem* WM = S.GI() ? S.GI()->GetSubsystem<UAsdaWindowSubsystem>() : nullptr)
		{
			if (!S.ToggleId.IsNone()) { WM->SetWindowVisible(S.ToggleId, bVisible); }
		}
	}
	// ★ 09.11: подсистема строки чата.
	static UAsdaChatWindowSubsystem* Chat(const FState& S) { return S.GI() ? S.GI()->GetSubsystem<UAsdaChatWindowSubsystem>() : nullptr; }

	// Собрать сценарий.
	static void Build(FRef S)
	{
		auto Add = [S](float Delay, TFunction<void()> Fn) { S->Steps.Add({ Delay, MoveTemp(Fn) }); };
		// ★ R5-fix6: шаг «после нажатия ждём захват» — событие (`IsEngaged`), а не только время `kHold`.
		auto AddEngaged = [S](float Delay, TFunction<void()> Fn) { S->Steps.Add({ Delay, MoveTemp(Fn), 4, []() { return FAsdaOrbitCursor::IsEngaged(); } }); };
		auto Key = [](int32 V) { AsdaCVarSave::SetForTest(kKey, V); };
		// Пачки сырых отсчётов мыши: от тиков игры не зависят — по одному кадру на пачку (R5-fix7).
		auto Raw = [S](int32 N) { for (int32 i = 0; i < N; ++i) { S->Steps.Add({ 0.03f, []() { FSlateApplication::Get().OnRawMouseMove(kRawStepPx, 0); }, /*MinFrames=*/1, nullptr }); } };
		Add(0.f, [S]() { CheckDecide(*S); });

		// B. Контроль: ключ 0, сначала замер скорости, затем упор в край.
		Add(0.1f, [S, Key]()
		{
			Key(0);
			S->P = FindPoint(*S, false);
			S->Expect(S->P.X >= 0.0, FString::Printf(TEXT("B0 точка мира у правого края (%.0f,%.0f)"), S->P.X, S->P.Y));
			S->Yaw0 = S->Pc()->GetCameraYawUE();
			S->Count0 = FAsdaOrbitCursor::GetEngageCount();
			Down(S->P - FVector2D(300.0, 0.0));
		});
		for (int32 i = 0; i < 10; ++i)
		{
			Add(0.03f, [S, i]() { const FVector2D From = S->P - FVector2D(300.0 - 30.0 * i, 0.0); Move(From, From + FVector2D(30.0, 0.0)); });
		}
		Add(0.15f, [S]()
		{
			const float T = S->Turned();
			S->DegPerPx = T / 300.f;
			S->Expect(T > 0.5f, FString::Printf(TEXT("B1 ключ 0: свободный ход курсора на 300 px повернул камеру на %.2f°"), T));
			S->Yaw0 = S->Pc()->GetCameraYawUE();
		});
		for (int32 i = 0; i < 10; ++i) { Add(0.03f, [S]() { Move(S->P, S->P); }); }   // курсор упёрся: положение не меняется
		Add(0.15f, [S]()
		{
			S->Expect(S->Turned() < 0.05f, FString::Printf(TEXT("B2 ключ 0: у края курсор стоит — поворот стоит (%.3f°), жалоба воспроизведена"), S->Turned()));
			S->Expect(FAsdaOrbitCursor::GetEngageCount() == S->Count0 && !FAsdaOrbitCursor::IsEngaged(), TEXT("B3 ключ 0: захвата нет"));
			Up();
		});

		// C. Ключ 1: нажатие там же.
		Add(0.2f, [S, Key]() { Key(1); S->Count0 = FAsdaOrbitCursor::GetEngageCount(); Down(S->P); });
		AddEngaged(kHold, [S]()
		{
			S->Expect(FAsdaOrbitCursor::IsEngaged() && FAsdaOrbitCursor::GetEngageCount() == S->Count0 + 1, TEXT("C1 ключ 1: удержание у края — захват стоит"));
			S->Expect(FVector2D::Distance(FAsdaOrbitCursor::GetAnchor(), S->P) <= kPosTol, TEXT("C2 якорь — точка нажатия"));
			S->Yaw0 = S->Pc()->GetCameraYawUE();
		});
		Raw(kRawSteps);
		Add(0.15f, [S]()
		{
			FVector2D Mn, Mx; ViewportAbs(Mn, Mx);
			const float Px = (float)(kRawSteps * kRawStepPx);
			const float T = S->Turned();
			const float Want = FMath::Abs(FMath::UnwindDegrees(S->DegPerPx * Px * FAsdaOrbitCursor::DeltaScale()));
			S->Expect(T > 5.f, FString::Printf(TEXT("C3 сырые %.0f px при ширине окна %.0f: камера повернулась (%.1f° по кругу), края нет"), Px, Mx.X - Mn.X, T));
			S->Expect(FMath::Abs(Want - T) < 15.f, FString::Printf(TEXT("C4 скорость прежняя: ждали %.1f° по кругу, есть %.1f°, множитель указателя %.2f"), Want, T, FAsdaOrbitCursor::DeltaScale()));
			S->Expect(CursorAt(S->P), TEXT("C5 курсор стоит в точке нажатия"));
			Up();
		});
		Add(0.15f, [S]()
		{
			S->Expect(!FAsdaOrbitCursor::IsEngaged(), TEXT("C6 отпускание — захват снят"));
			S->Expect(CursorAt(S->P), TEXT("C7 курсор на прежнем месте"));
		});

		// D. Быстрый щелчок.
		//   ★ R5-fix6 (13.11.2026): «быстрый» — кнопка отпущена раньше порога захвата `kEngageHoldSec` (0,12 с). Отпускание идёт на
		//   следующем кадре после нажатия; если кадр вышел длиннее порога (нагрузка), щелчок быстрым не был — проверять нечего, попытка
		//   повторяется (до `kClickTries`). Проверка та же и одна: после быстрого щелчка захватов не прибавилось.
		constexpr int32 kClickTries = 8;
		for (int32 Try = 0; Try < kClickTries; ++Try)
		{
			Add(0.1f, [S]()
			{
				if (S->bClickDone) { return; }
				++S->ClickTry;
				S->Count0 = FAsdaOrbitCursor::GetEngageCount();
				Down(S->P);
				S->ClickDownS = FPlatformTime::Seconds();
			});
			S->Steps.Add({ 0.04f, [S, Try]()
			{
				if (S->bClickDone) { return; }
				Up();
				const double Held = FPlatformTime::Seconds() - S->ClickDownS;
				if (Held < (double)FAsdaOrbitCursor::kEngageHoldSec) { S->bClickDone = true; return; }
				UE_LOG(LogAsdaOrbitCursorTest, Log, TEXT("%s D: попытка %d — кнопка держалась %.3f с (кадр длиннее порога захвата %.2f с), щелчок не быстрый — повтор"),
					kTag, S->ClickTry, Held, FAsdaOrbitCursor::kEngageHoldSec);
				if (Try + 1 == kClickTries) { S->bClickDone = true; S->Count0 = -1; }   // попытки кончились — проверка ниже упадёт с объяснением
			}, /*MinFrames=*/1, nullptr });
		}
		Add(0.2f, [S]()
		{
			S->Expect(S->Count0 >= 0 && FAsdaOrbitCursor::GetEngageCount() == S->Count0, S->Count0 >= 0
				? FString::Printf(TEXT("D1 быстрый щелчок ПКМ — захвата нет, курсор не мигает (попытка %d)"), S->ClickTry)
				: FString(TEXT("D1 быстрый щелчок ПКМ — ни одна попытка не уложилась в порог захвата: кадры длиннее порога")));
		});

		// E. Над окном интерфейса.
		Add(0.1f, [S]()
		{
			const FVector2D Ui = FindPoint(*S, true);
			S->Expect(Ui.X >= 0.0, FString::Printf(TEXT("E0 точка над окном найдена (%.0f,%.0f)"), Ui.X, Ui.Y));
			S->Count0 = FAsdaOrbitCursor::GetEngageCount();
			Down(Ui);
		});
		Add(kHold, [S]() { S->Expect(FAsdaOrbitCursor::GetEngageCount() == S->Count0, TEXT("E1 нажатие над окном — поворота и захвата нет")); Up(); });

		// F. Alt+Tab при зажатой кнопке.
		Add(0.2f, [S]() { S->Count0 = FAsdaOrbitCursor::GetEngageCount(); Down(S->P); });
		AddEngaged(kHold, [S]()
		{
			S->Expect(FAsdaOrbitCursor::IsEngaged(), TEXT("F1 захват стоит"));
			FAsdaOrbitCursor::DebugOverrideWindow(true, false);
			if (AAsdaPlayerController* C = S->Pc()) { C->FlushPressedKeys(); }   // потеря фокуса: вьюпорт сбрасывает клавиши
		});
		Add(0.1f, [S]()
		{
			S->Expect(!FAsdaOrbitCursor::IsEngaged(), TEXT("F2 потеря фокуса — захват снят"));
			S->Expect(CursorAt(S->P), TEXT("F3 курсор на месте"));
			FAsdaOrbitCursor::DebugOverrideWindow(true, true);
		});
		Add(kHold, [S]()
		{
			S->Expect(!FAsdaOrbitCursor::IsEngaged() && FAsdaOrbitCursor::GetEngageCount() == S->Count0 + 1, TEXT("F4 возврат фокуса с зажатой кнопкой — захват сам не встаёт"));
			Up();
		});

		// G. Модальное окно при зажатой кнопке.
		Add(0.2f, [S]() { Down(S->P); });
		AddEngaged(kHold, [S]()
		{
			S->Expect(FAsdaOrbitCursor::IsEngaged(), TEXT("G1 захват стоит"));
			if (UAsdaWindowSubsystem* WM = S->GI()->GetSubsystem<UAsdaWindowSubsystem>()) { WM->ShowGameMessage(TEXT("самотест курсора ПКМ")); }
		});
		Add(0.3f, [S]()
		{
			S->Expect(!FAsdaOrbitCursor::IsEngaged(), TEXT("G2 модальное окно — поворот снят сбросом клавиш, захват снят"));
			S->Expect(CursorAt(S->P), TEXT("G3 курсор на месте, окно можно нажимать"));
			if (UAsdaWindowSubsystem* WM = S->GI()->GetSubsystem<UAsdaWindowSubsystem>()) { WM->CloseWindow(FName(TEXT("ONEBUT_MSGBOX"))); }
			Up();
		});

		// H. Режим Alt+Z.
		Add(0.3f, [S]()
		{
			UAsdaUiVisibilitySubsystem* Vis = S->GI()->GetSubsystem<UAsdaUiVisibilitySubsystem>();
			S->Expect(Vis && Vis->ToggleFromHotkey(TEXT("самотест курсора ПКМ")), TEXT("H0 Alt+Z скрыл интерфейс"));
			Down(S->P);
		});
		AddEngaged(kHold, [S]() { S->Expect(FAsdaOrbitCursor::IsEngaged(), TEXT("H1 Alt+Z: захват стоит")); S->Yaw0 = S->Pc()->GetCameraYawUE(); });
		Raw(5);
		Add(0.15f, [S]() { S->Expect(S->Turned() > 1.f, FString::Printf(TEXT("H2 Alt+Z: камера крутится (%.1f°)"), S->Turned())); Up(); });
		Add(0.15f, [S]()
		{
			const AAsdaPlayerController* C = S->Pc();
			S->Expect(!FAsdaOrbitCursor::IsEngaged() && C && C->CurrentMouseCursor == EMouseCursor::None, TEXT("H3 отпускание: захват снят, курсор остаётся скрытым режимом Alt+Z"));
			if (UAsdaUiVisibilitySubsystem* Vis = S->GI()->GetSubsystem<UAsdaUiVisibilitySubsystem>()) { Vis->ToggleFromHotkey(TEXT("самотест курсора ПКМ")); }
		});
		Add(0.15f, [S]() { const AAsdaPlayerController* C = S->Pc(); S->Expect(C && C->CurrentMouseCursor != EMouseCursor::None, TEXT("H4 Alt+Z обратно — курсор виден")); });

		// I. Смерть: камера живая.
		Add(0.1f, [S]() { if (UAsdaNetSubsystem* N = AsdaSelfTestKit::NetOf(S->World.Get())) { N->bSelfDead = true; } Down(S->P); });
		AddEngaged(kHold, [S]() { S->Expect(FAsdaOrbitCursor::IsEngaged(), TEXT("I1 мёртв: захват стоит")); S->Yaw0 = S->Pc()->GetCameraYawUE(); });
		Raw(5);
		Add(0.15f, [S]()
		{
			S->Expect(S->Turned() > 1.f, FString::Printf(TEXT("I2 мёртв: камера крутится (%.1f°)"), S->Turned()));
			if (UAsdaNetSubsystem* N = AsdaSelfTestKit::NetOf(S->World.Get())) { N->bSelfDead = false; }
			Up();
		});
		Add(0.15f, [S]() { S->Expect(!FAsdaOrbitCursor::IsEngaged() && CursorAt(S->P), TEXT("I3 отпускание: захват снят, курсор на месте")); });

		// ★ 09.11.2026 J. Окно прячется и показывается посреди поворота: контроль ключом 0, затем ключ 1.
		auto Hold = [](int32 V) { AsdaCVarSave::SetForTest(kHoldKey, V); };
		for (int32 Pass = 0; Pass < 2; ++Pass)
		{
			const bool bFix = Pass == 1;
			Add(0.2f, [S, Hold, bFix]()
			{
				Hold(bFix ? 1 : 0);
				S->ToggleId = FindToggleWindow(*S);
				S->Expect(!S->ToggleId.IsNone(), FString::Printf(TEXT("J0 ключ %d: видимое окно мира для показа/скрытия — «%s»"), bFix ? 1 : 0, *S->ToggleId.ToString()));
				S->Recap0 = FAsdaOrbitCursor::GetRecaptureCount();
				Down(S->P);
			});
			AddEngaged(kHold, [S, bFix]()
			{
				S->Expect(FAsdaOrbitCursor::IsEngaged(), FString::Printf(TEXT("J1 ключ %d: захват стоит до окна"), bFix ? 1 : 0));
				ToggleWindow(*S, false);
			});
			Add(0.1f, [S]()
			{
				const UGameViewportClient* VP = GEngine ? GEngine->GameViewport.Get() : nullptr;
				const FSceneViewport* Sc = VP ? VP->GetGameViewport() : nullptr;
				UE_LOG(LogAsdaOrbitCursorTest, Log, TEXT("%s окно «%s» скрыто: захват вьюпорта %d, режим ввода «%s»"), kTag, *S->ToggleId.ToString(),
					Sc && Sc->HasMouseCapture() ? 1 : 0, S->Pc() ? *S->Pc()->GetCurrentInputModeDebugString() : TEXT("?"));
				ToggleWindow(*S, true);
			});
			Add(0.1f, [S]() { S->Yaw0 = S->Pc()->GetCameraYawUE(); });
			Raw(8);
			Add(0.15f, [S, bFix]()
			{
				const float T = S->Turned();
				if (bFix)
				{
					S->Expect(FAsdaOrbitCursor::IsEngaged(), TEXT("J2 ключ 1: окно скрыто и показано — захват стоит, курсор скрыт"));
					S->Expect(FAsdaOrbitCursor::GetRecaptureCount() > S->Recap0, FString::Printf(TEXT("J3 ключ 1: отнятый режимом ввода захват вьюпорт получил обратно (%d раз)"), FAsdaOrbitCursor::GetRecaptureCount() - S->Recap0));
					S->Expect(T > 2.f, FString::Printf(TEXT("J4 ключ 1: камера крутится после окна (%.1f°)"), T));
					S->Expect(CursorAt(S->P), TEXT("J5 ключ 1: курсор в точке нажатия"));
				}
				else
				{
					S->Expect(!FAsdaOrbitCursor::IsEngaged(), TEXT("J2 ключ 0: окно сняло захват — курсор показан (жалоба владельца воспроизведена)"));
					S->Expect(T < 0.05f, FString::Printf(TEXT("J3 ключ 0: поворот после окна стоит (%.3f°)"), T));
				}
				Up();
			});
			Add(0.15f, [S, bFix]() { S->Expect(!FAsdaOrbitCursor::IsEngaged() && !FAsdaOrbitCursor::IsOrbitReported(), FString::Printf(TEXT("J6 ключ %d: отпускание — захвата нет, поворот снят"), bFix ? 1 : 0)); });
		}

		// ★ 09.11.2026 K. Строка чата открыта (фокус полю) и закрыта (`SetInputMode`) посреди поворота: контроль ключом 0, затем ключ 1.
		for (int32 Pass = 0; Pass < 2; ++Pass)
		{
			const bool bFix = Pass == 1;
			Add(0.2f, [S, Hold, bFix]() { Hold(bFix ? 1 : 0); S->Rearm0 = FAsdaOrbitCursor::GetRearmCount(); Down(S->P); });
			AddEngaged(kHold, [S, bFix]()
			{
				S->Expect(FAsdaOrbitCursor::IsEngaged(), FString::Printf(TEXT("K1 ключ %d: захват стоит до чата"), bFix ? 1 : 0));
				UAsdaChatWindowSubsystem* C = Chat(*S);
				S->Expect(C != nullptr, TEXT("K0 подсистема чата есть"));
				if (C) { C->OpenInput(3); }   // команда 67 CHAT NORMAL — как клавиша игрока
			});
			Add(0.2f, [S, bFix]()
			{
				const UAsdaChatWindowSubsystem* C = Chat(*S);
				S->Expect(C && C->IsInputActive(), FString::Printf(TEXT("K2 ключ %d: строка чата открыта"), bFix ? 1 : 0));
				const int32 Re = FAsdaOrbitCursor::GetRearmCount() - S->Rearm0;
				S->Expect(bFix ? Re == 1 : Re == 0, FString::Printf(TEXT("K2b ключ %d: поворот после сброса клавиш возвращён %d раз (ждали %d)"), bFix ? 1 : 0, Re, bFix ? 1 : 0));
				S->Yaw0 = S->Pc()->GetCameraYawUE();
			});
			Raw(6);
			Add(0.15f, [S, bFix]()
			{
				const float T = S->Turned();
				if (bFix)
				{
					S->Expect(FAsdaOrbitCursor::IsEngaged() && T > 1.f, FString::Printf(TEXT("K3 ключ 1: строка чата открыта — захват стоит, камера крутится (%.1f°)"), T));
				}
				else
				{
					S->Expect(!FAsdaOrbitCursor::IsEngaged() && T < 0.05f, FString::Printf(TEXT("K3 ключ 0: строка чата погасила поворот (%.3f°), курсор показан (контроль)"), T));
				}
				if (UAsdaChatWindowSubsystem* C = Chat(*S)) { C->CloseInput(); }
			});
			Add(0.2f, [S]() { S->Yaw0 = S->Pc()->GetCameraYawUE(); });
			Raw(6);
			Add(0.15f, [S, bFix]()
			{
				if (bFix)
				{
					const float T = S->Turned();
					S->Expect(FAsdaOrbitCursor::IsEngaged() && T > 1.f, FString::Printf(TEXT("K4 ключ 1: строка чата закрыта — захват стоит, камера крутится (%.1f°)"), T));
					S->Expect(CursorAt(S->P), TEXT("K5 ключ 1: курсор в точке нажатия"));
				}
				Up();
			});
			Add(0.15f, [S, bFix]() { S->Expect(!FAsdaOrbitCursor::IsEngaged() && !FAsdaOrbitCursor::IsOrbitReported(), FString::Printf(TEXT("K6 ключ %d: отпускание — захвата нет, поворот снят"), bFix ? 1 : 0)); });
		}

		// ★ 09.11.2026 L (ревизия п. 1). Ключ 1: строка чата (поворот возвращён), затем второй сброс в том же зажиме.
		//   L1: модальное окно — поворот снят, захвата нет. L2: Alt+Tab — поворот снят. После отпускания поворот не докладывается.
		for (int32 Pass = 0; Pass < 2; ++Pass)
		{
			const bool bModal = Pass == 0;
			const TCHAR* Name = bModal ? TEXT("модальное окно") : TEXT("Alt+Tab");
			Add(0.2f, [S, Hold]() { Hold(1); S->Rearm0 = FAsdaOrbitCursor::GetRearmCount(); Down(S->P); });
			AddEngaged(kHold, [S]() { if (UAsdaChatWindowSubsystem* C = Chat(*S)) { C->OpenInput(3); } });
			Add(0.2f, [S, Name]()
			{
				S->Expect(FAsdaOrbitCursor::GetRearmCount() == S->Rearm0 + 1 && FAsdaOrbitCursor::IsEngaged(), FString::Printf(TEXT("L0 %s: строка чата открыта, поворот возвращён"), Name));
			});
			Add(0.05f, [S, bModal]()
			{
				if (bModal) { if (UAsdaWindowSubsystem* WM = S->GI()->GetSubsystem<UAsdaWindowSubsystem>()) { WM->ShowGameMessage(TEXT("самотест курсора ПКМ")); } }
				else
				{
					FAsdaOrbitCursor::DebugOverrideWindow(true, false);
					if (AAsdaPlayerController* C = S->Pc()) { C->FlushPressedKeys(); }   // потеря фокуса: вьюпорт сбрасывает клавиши
				}
			});
			Add(0.3f, [S, Name]()
			{
				S->Expect(!FAsdaOrbitCursor::IsOrbitReported() && !FAsdaOrbitCursor::IsEngaged(), FString::Printf(TEXT("L1 %s после чата: поворот снят, захвата нет (поворот не завис)"), Name));
				S->Expect(FAsdaOrbitCursor::GetRearmCount() == S->Rearm0 + 1, FString::Printf(TEXT("L2 %s: второй раз поворот не возвращён"), Name));
			});
			Add(0.1f, [S, bModal]()
			{
				if (bModal) { if (UAsdaWindowSubsystem* WM = S->GI()->GetSubsystem<UAsdaWindowSubsystem>()) { WM->CloseWindow(FName(TEXT("ONEBUT_MSGBOX"))); } }
				else { FAsdaOrbitCursor::DebugOverrideWindow(true, true); }
				if (UAsdaChatWindowSubsystem* C = Chat(*S)) { C->CloseInput(); }
			});
			Add(0.2f, [S, Name]()
			{
				S->Expect(!FAsdaOrbitCursor::IsOrbitReported(), FString::Printf(TEXT("L3 %s снято, кнопка ещё зажата: поворот сам не вернулся"), Name));
				Up();
			});
			Add(0.15f, [S, Name]() { S->Expect(!FAsdaOrbitCursor::IsEngaged() && !FAsdaOrbitCursor::IsOrbitReported(), FString::Printf(TEXT("L4 %s: отпускание — захвата и поворота нет"), Name)); });
		}
	}

	static void ArmStep(FRef S, int32 I, float Delay);   // ★ R5-fix6: взвод шага (определение ниже `Next`)

	// Итог.
	static void Finish(FRef S)
	{
		UWorld* W = S->World.Get();
		if (FSlateApplication::IsInitialized() && FSlateApplication::Get().GetPressedMouseButtons().Contains(EKeys::RightMouseButton)) { Up(); }
		FAsdaOrbitCursor::DebugOverrideWindow(false, false);
		GetMutableDefault<UInputSettings>()->bEnableMouseSmoothing = S->bSmoothWas;
		AsdaCVarSave::RestoreAll(kCmd);
		AsdaSelfTestKit::Finish(W, kCmd, S->bQuit, LogAsdaOrbitCursorTest, kTag, S->Ok, S->Fail);
	}

	// Следующий шаг.
	static void Next(FRef S)
	{
		UWorld* W = S->World.Get();
		if (!W || !S->Gate.Pass(W)) { return; }
		if (!S->Steps.IsValidIndex(S->Idx)) { Finish(S); return; }
		const int32 I = S->Idx++;
		S->StepArmS = FPlatformTime::Seconds();
		S->StepArmFrame = GFrameCounter;
		ArmStep(S, I, FMath::Max(0.01f, S->Steps[I].Delay));
	}

	// ★ R5-fix6: взвести шаг I через Delay секунд; по сроку — проверить кадры и событие шага, не готово — перевзвести на короткий срок.
	static void ArmStep(FRef S, int32 I, float Delay)
	{
		UWorld* W = S->World.Get();
		if (!W) { return; }
		FTimerHandle H;
		W->GetTimerManager().SetTimer(H, FTimerDelegate::CreateLambda([S, I]()
		{
			const FStep& St = S->Steps[I];
			const bool bFrames = GFrameCounter >= S->LastStepFrame + (uint64)FMath::Max(0, St.MinFrames);
			const bool bCap = FPlatformTime::Seconds() - S->StepArmS >= (double)St.Delay && GFrameCounter >= S->StepArmFrame + (uint64)kUntilCapFrames;
			if (!bFrames || (St.Until && !St.Until() && !bCap)) { ArmStep(S, I, 0.01f); return; }
			S->LastStepFrame = GFrameCounter;
			St.Fn();
			Next(S);
		}), Delay, false);
	}

	// Вход команды.
	static void Run(const TArray<FString>& Args, UWorld* W)
	{
		const FAsdaSelfTestArgs A = FAsdaSelfTestArgs::Parse(Args);
		if (!A.Has(TEXT("selfready")) && W && GEngine && !AsdaSelfTestSelfReady::IsReady(W))
		{
			const APlayerController* P = W->GetFirstPlayerController();
			if (!P || !P->GetPawn()) { GEngine->Exec(W, TEXT("Asda.Load.CurrentMap 3")); }
			GEngine->Exec(W, TEXT("Asda.World.FakeSelfSpawn 256 256"));
			const FString Again = FString::Printf(TEXT("%s selfready%s"), kCmd, A.bQuit ? TEXT(" quit") : TEXT(""));
			TWeakObjectPtr<UWorld> WW(W);
			FTimerHandle H;
			W->GetTimerManager().SetTimer(H, FTimerDelegate::CreateLambda([WW, Again]() { if (WW.IsValid() && GEngine) { GEngine->Exec(WW.Get(), *Again); } }), 3.f, false);
			return;
		}
		if (AsdaSelfTestKit::RefuseStart(W, kCmd, A)) { return; }
		if (!FSlateApplication::IsInitialized()) { AsdaSelfTestKit::Refused(W, kCmd, A.bQuit, TEXT("нет Slate")); return; }
		AsdaCVarSave::ArmWorldGuard(W, kCmd);
		FRef S = MakeShared<FState>();
		S->World = W; S->bQuit = A.bQuit; S->Gate = FAsdaSelfTestRunGate{ kCmd, A.bQuit };
		if (!S->Pc()) { AsdaSelfTestKit::Refused(W, kCmd, A.bQuit, TEXT("нет своего контроллера")); return; }
		S->bSmoothWas = GetDefault<UInputSettings>()->bEnableMouseSmoothing;
		GetMutableDefault<UInputSettings>()->bEnableMouseSmoothing = false;
		FAsdaOrbitCursor::DebugOverrideWindow(true, true);
		Build(S);
		Next(S);
	}
}

static FAutoConsoleCommandWithWorldAndArgs GAsdaSelfTestOrbitCursor(
	TEXT("asda.Camera.SelfTestOrbitCursor"),
	TEXT("Самотест курсора при зажатой ПКМ: решение сторожа, контроль ключом 0 (у края поворот стоит), захват и поворот мимо края, возврат курсора, "
	     "быстрый щелчок, нажатие над окном, потеря фокуса, модальное окно, Alt+Z, смерть, окно и строка чата посреди поворота. Только офлайн. `delay=N` `quit`."),
	FConsoleCommandWithWorldAndArgsDelegate::CreateLambda([](const TArray<FString>& A, UWorld* W)
	{
		AsdaOrbitCursorTest::Run(A, W);
	}));

#endif // !UE_BUILD_SHIPPING
