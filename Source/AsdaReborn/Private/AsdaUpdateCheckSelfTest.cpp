// Copyright (c) 2026 @sitxovski (Discord)
// ★ САМОТЕСТ ПРОВЕРКИ НОВОЙ ВЕРСИИ (18.10.2026): `asda.SelfTest.UpdateCheck [quit]`. Подсистема — `UAsdaUpdateCheckSubsystem`
//   (шапка `Public/UI/AsdaUpdateCheckSubsystem.h`). Только офлайн (-AsdaNoNet); язык — `-AsdaLang=ru|en`.
//   Образцы ответа сервера — НАСТОЯЩИЕ подписанные `keys/keyring.json` и `version.json` с @@ASTORY_UPDATE_HOST@@
//   (скачаны 18.10.2026: alpha-1.0.5-10.2026, seq 6) в `<проект>/tests/update_check/`: подпись проверяется боевым корнем.
//   1) чистые функции: вшитый корень == `Releases/keys/root.pub`, id корня; та же версия → Same, установка старее → Newer,
//      новее → Older; откат keyring, подделанная нагрузка, чужой канал, конверт не того вида → Invalid; разбор state.json;
//   2) подмена ответа «новая версия» → штатное окно ONEBUT_MSGBOX 1500: текст 991100, ОДНА видимая кнопка с подписью 991101,
//      окно модальное (ввод игрока снят); повторный ответ — окно не плодится (показ один, запись очереди одна);
//   3) «та же версия» → окна нет, следующая проверка через интервал; «сбой сети» и «подпись не сошлась» → окна нет, повтор;
//   4) (через кадры) запись окна снесена (как сменой карты) → секундный тик ставит её снова; подпись кнопки помещается
//      в кнопку (геометрия после отрисовки); НАСТОЯЩИЙ запрос FHttpModule на закрытый порт 127.0.0.1:9 → сбой молча, повтор.
//   Кнопку самотест НЕ нажимает (она закрывает игру); проверяется, что она привязана к действию окна.
//   Итог: «ОБНОВЛЕНИЯ ИТОГ: ok=N fail=M».
#include "CoreMinimal.h"
#include "AsdaDevConsole.h"
#include "Test/AsdaSelfTestInput.h"   // ★ R5-c (12.11.2026): общая точка самотестов вместо своих копий помощников

#if ASDA_DEV_CONSOLE && !UE_BUILD_SHIPPING

#include "UI/AsdaUpdateCheckSubsystem.h"
#include "UI/AsdaWindowSubsystem.h"
#include "UI/AsdaNotice.h"
#include "Text/AsdaLang.h"
#include "Test/AsdaSelfTestTree.h"   // папка проекта в дереве (ASDA_ROOT) — ведомости-оракулы и для собранной игры
#include "Test/AsdaSelfTestKit.h"
#include "Blueprint/UserWidget.h"
#include "Blueprint/WidgetTree.h"
#include "Components/Button.h"
#include "Components/TextBlock.h"
#include "Containers/Ticker.h"
#include "Engine/GameInstance.h"
#include "Engine/World.h"
#include "HAL/IConsoleManager.h"
#include "HAL/PlatformTime.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "UnrealClient.h"   // FScreenshotRequest — снимок кадра с интерфейсом (`shot`)

DEFINE_LOG_CATEGORY_STATIC(LogAsdaUpdateTest, Log, All);

namespace AsdaUpdateCheckSelfTest
{
	// Имя команды (строка итога, отказ, возврат ключей).
	static const TCHAR* kCmd = TEXT("asda.SelfTest.UpdateCheck");
	// Версия и seq образца version.json.
	static const TCHAR* kFixtureVersion = TEXT("alpha-1.0.5-10.2026");
	static constexpr int64 kFixtureSeq = 6;
	// Открытый корневой ключ, которым подписаны образцы tests/update_check (id 9df15db74947e0a0). Не корень вашей сборки:
	// образцы проверяются этим ключом, а вшитый корень сборки сверяется только с Releases/keys/root.pub.
	static const TCHAR* kFixtureRootHex = TEXT("97f882383978cb4b6ccee58a08bf22d050e09da1e0bff4ff179ccf9e1347db26");
	// Корень образцов байтами (32; пусто — строка испорчена).
	static TArray<uint8> FixtureRoot()
	{
		TArray<uint8> K;
		K.SetNumZeroed(32);
		if (HexToBytes(FString(kFixtureRootHex), K.GetData()) != 32) { K.Reset(); }
		return K;
	}
	// Закрытый порт для настоящего сбоя сети (discard; на машине разработчика не слушается).
	static const TCHAR* kDeadUrl = TEXT("http://127.0.0.1:9");

	// Состояние прогона (живёт до итога; вторая фаза — на тиках).
	struct FRun
	{
		TWeakObjectPtr<UWorld> World;          // мир самотеста
		TWeakObjectPtr<UGameInstance> GI;      // экземпляр игры
		bool bQuit = false;                    // выйти после итога
		FString ShotPath;                      // `shot` — снимок кадра с окном (путь PNG), пусто — без снимка
		int32 Ok = 0, Fail = 0;                // счёт проверок
		int32 Phase = 0;                       // фаза второй части
		double PhaseStart = 0.0;               // начало фазы (FPlatformTime::Seconds)
		FString Keyring, Version;              // образцы ответа сервера
		FTSTicker::FDelegateHandle Tick;       // тик второй части
	};
	static TSharedPtr<FRun> GRun;

	// Записать результат проверки.
	static void Expect(FRun& R, bool b, const FString& What)
	{
		if (b) { ++R.Ok; UE_LOG(LogAsdaUpdateTest, Log, TEXT("ОБНОВЛЕНИЯ ok   %s"), *What); }
		else { ++R.Fail; UE_LOG(LogAsdaUpdateTest, Error, TEXT("ОБНОВЛЕНИЯ FAIL %s"), *What); }
	}

	// Есть ли в строке кириллица (признак RU-текста).
	using AsdaSelfTestInput::HasCyrillic;   // ★ R5-c (12.11.2026): общая точка (`Test/AsdaSelfTestInput.h`) — копия была дословной.

	// Текст без переносов и повторных пробелов (окно раскладывает строку по ширине 270).
	static FString Flat(const FString& S)
	{
		TArray<FString> W;
		S.Replace(TEXT("\\n"), TEXT(" ")).ParseIntoArrayWS(W);
		return FString::Join(W, TEXT(" "));
	}

	// Установленная версия для проверок.
	static FAsdaInstalledVersion Inst(int64 Seq, int64 KeyringSeq = 1, const TCHAR* Channel = TEXT("alpha"))
	{
		FAsdaInstalledVersion I;
		I.Channel = Channel; I.Version = TEXT("тест"); I.Seq = Seq; I.KeyringSeq = KeyringSeq; I.bValid = true;
		return I;
	}

	// Видимые кнопки окна (вся иерархия виджета).
	static int32 CountVisibleButtons(UUserWidget* W)
	{
		int32 N = 0;
		if (W && W->WidgetTree)
		{
			W->WidgetTree->ForEachWidget([&N](UWidget* X)
			{
				if (Cast<UButton>(X) && X->IsVisible())
				{
					bool bShown = true;   // видима вся цепочка родителей
					for (UWidget* P = X->GetParent(); P; P = P->GetParent()) { if (!P->IsVisible()) { bShown = false; break; } }
					if (bShown) { ++N; }
				}
			});
		}
		return N;
	}

	// Проверка окна, показанного на подмену «новая версия».
	static void CheckPopup(FRun& R, UAsdaUpdateCheckSubsystem* S, UAsdaWindowSubsystem* WM, const TCHAR* Tag)
	{
		UGameInstance* GI = R.GI.Get();
		const bool bEn = FAsdaLang::Current() == EAsdaLang::En;
		UUserWidget* Box = S->DebugGetPopup();
		Expect(R, Box != nullptr && S->IsPopupDisplayed(), FString::Printf(TEXT("%s: окно ONEBUT_MSGBOX на экране"), Tag));
		if (!Box) { return; }
		const FString Want = AsdaNotice::Loc(GI, UAsdaUpdateCheckSubsystem::kLocText);
		const FString Shown = UAsdaWindowSubsystem::ReadMsgBoxShownText(Box->GetWidgetFromName(TEXT("ONEBUT_TEXT")));
		Expect(R, !Want.IsEmpty() && Flat(Shown) == Flat(Want) && (bEn ? !HasCyrillic(Shown) : HasCyrillic(Shown)),
			FString::Printf(TEXT("%s: текст окна = loc %d [%s] «%s»"), Tag, UAsdaUpdateCheckSubsystem::kLocText,
				FAsdaLang::Code(FAsdaLang::Current()), *Shown.Replace(TEXT("\n"), TEXT(" / "))));
		const UTextBlock* BT = Cast<UTextBlock>(Box->GetWidgetFromName(TEXT("ONEBUT_BUT00_Text")));
		const FString WantBtn = AsdaNotice::Loc(GI, UAsdaUpdateCheckSubsystem::kLocButton);
		Expect(R, BT && !WantBtn.IsEmpty() && BT->GetText().ToString() == WantBtn && (bEn ? !HasCyrillic(WantBtn) : HasCyrillic(WantBtn)),
			FString::Printf(TEXT("%s: подпись кнопки = loc %d «%s»"), Tag, UAsdaUpdateCheckSubsystem::kLocButton, BT ? *BT->GetText().ToString() : TEXT("—")));
		const UButton* B = Cast<UButton>(Box->GetWidgetFromName(TEXT("ONEBUT_BUT00")));
		const int32 NBtn = CountVisibleButtons(Box);
		Expect(R, B && B->IsVisible() && B->OnClicked.IsBound() && NBtn == 1,
			FString::Printf(TEXT("%s: одна видимая кнопка (видимых %d), привязана к действию"), Tag, NBtn));
		Expect(R, WM->IsAnyModalOpen(), FString::Printf(TEXT("%s: окно модальное — ввод игрока снят"), Tag));
	}

	// Вторая часть (на тиках): возврат снесённой записи, геометрия кнопки, настоящий сбой сети.
	static bool OnTick(float)
	{
		if (!GRun.IsValid()) { return false; }
		FRun& R = *GRun;
		UGameInstance* GI = R.GI.Get();
		UAsdaUpdateCheckSubsystem* S = GI ? GI->GetSubsystem<UAsdaUpdateCheckSubsystem>() : nullptr;
		UAsdaWindowSubsystem* WM = GI ? GI->GetSubsystem<UAsdaWindowSubsystem>() : nullptr;
		const double Now = FPlatformTime::Seconds();
		auto Done = [&R]()
		{
			if (UGameInstance* G = R.GI.Get()) { if (UAsdaUpdateCheckSubsystem* X = G->GetSubsystem<UAsdaUpdateCheckSubsystem>()) { X->DebugEndForTest(); } }
			AsdaSelfTestKit::Finish(R.World.Get(), kCmd, R.bQuit, LogAsdaUpdateTest, TEXT("ОБНОВЛЕНИЯ"), R.Ok, R.Fail);
			GRun.Reset();
			return false;
		};
		if (!S || !WM) { Expect(R, false, TEXT("4: подсистемы пропали")); return Done(); }

		switch (R.Phase)
		{
			case 0:   // запись окна снесена, как сменой карты; ждём, что тик поставит её снова
				if (S->IsPopupQueued() && S->IsPopupDisplayed())
				{
					Expect(R, S->GetPopupShowCount() == 2, FString::Printf(TEXT("4: снесённое окно вернулось само за %.1f с (показов %d)"),
						Now - R.PhaseStart, S->GetPopupShowCount()));
					CheckPopup(R, S, WM, TEXT("4"));
					R.Phase = 1; R.PhaseStart = Now;
				}
				else if (Now - R.PhaseStart > 5.0)
				{
					Expect(R, false, TEXT("4: снесённое окно не вернулось за 5 с"));
					R.Phase = 1; R.PhaseStart = Now;
				}
				return true;
			case 1:   // кадр отрисован — геометрия: подпись помещается в кнопку
				if (Now - R.PhaseStart < 0.5) { return true; }
				{
					UUserWidget* Box = S->DebugGetPopup();
					UTextBlock* BT = Box ? Cast<UTextBlock>(Box->GetWidgetFromName(TEXT("ONEBUT_BUT00_Text"))) : nullptr;
					UButton* B = Box ? Cast<UButton>(Box->GetWidgetFromName(TEXT("ONEBUT_BUT00"))) : nullptr;
					const float TextW = BT ? (float)BT->GetCachedGeometry().GetLocalSize().X : 0.f;
					const float BtnW = B ? (float)B->GetCachedGeometry().GetLocalSize().X : 0.f;
					const float DesW = BT ? (float)BT->GetDesiredSize().X : 0.f;
					Expect(R, BtnW > 1.f && DesW > 1.f && DesW <= BtnW,
						FString::Printf(TEXT("4: подпись кнопки помещается: текст %.0f (слот %.0f) ≤ кнопка %.0f"), DesW, TextW, BtnW));
				}
				if (!R.ShotPath.IsEmpty() && R.Phase == 1)
				{
					FScreenshotRequest::RequestScreenshot(R.ShotPath, /*bShowUI*/true, /*bAddFilenameSuffix*/false);
					UE_LOG(LogAsdaUpdateTest, Log, TEXT("ОБНОВЛЕНИЯ снимок кадра с окном: %s"), *R.ShotPath);
					R.Phase = 10; R.PhaseStart = Now;   // дать кадру записаться
					return true;
				}
				[[fallthrough]];
			case 10:
				if (R.Phase == 10 && Now - R.PhaseStart < 1.5) { return true; }
				S->DebugEndForTest();
				Expect(R, !S->IsPopupQueued(), TEXT("4: окно снято самотестом"));
				// Настоящий запрос на закрытый порт.
				S->DebugBeginForTest(Inst(kFixtureSeq - 1), kDeadUrl, FixtureRoot());
				Expect(R, S->CheckNow() && S->IsRequestInFlight(), TEXT("5: запрос FHttpModule ушёл (асинхронно, игровой поток не ждёт)"));
				R.Phase = 2; R.PhaseStart = Now;
				return true;
			case 2:   // ждём завершения запроса
				if (S->IsRequestInFlight())
				{
					if (Now - R.PhaseStart > 30.0) { Expect(R, false, TEXT("5: запрос не завершился за 30 с")); return Done(); }
					return true;
				}
				{
					const double Next = S->GetSecondsToNextCheck();
					Expect(R, S->GetLastResult() == EAsdaUpdateCheckResult::NetFail && !S->IsPopupQueued()
						&& Next > UAsdaUpdateCheckSubsystem::kDefaultIntervalSec - 10.0 && Next <= UAsdaUpdateCheckSubsystem::kDefaultIntervalSec + 1.0,
						FString::Printf(TEXT("5: настоящий сбой сети за %.1f с — молча, окна нет, повтор через %.0f с"), Now - R.PhaseStart, Next));
				}
				return Done();
			default:
				return Done();
		}
	}

	static void Run(const TArray<FString>& A, UWorld* W)
	{
		const FAsdaSelfTestArgs Args = FAsdaSelfTestArgs::Parse(A);
		if (AsdaSelfTestKit::RefuseStart(W, kCmd, Args)) { return; }
		UGameInstance* GI = W ? W->GetGameInstance() : nullptr;
		UAsdaUpdateCheckSubsystem* S = GI ? GI->GetSubsystem<UAsdaUpdateCheckSubsystem>() : nullptr;
		UAsdaWindowSubsystem* WM = GI ? GI->GetSubsystem<UAsdaWindowSubsystem>() : nullptr;
		if (!S || !WM) { AsdaSelfTestKit::Refused(W, kCmd, Args.bQuit, TEXT("нет подсистемы проверки обновлений или окон")); return; }
		if (GRun.IsValid()) { AsdaSelfTestKit::Refused(W, kCmd, Args.bQuit, TEXT("самотест уже идёт")); return; }
		GRun = MakeShared<FRun>();
		FRun& R = *GRun;
		R.World = W; R.GI = GI; R.bQuit = Args.bQuit;
		if (Args.Has(TEXT("shot")))
		{
			R.ShotPath = FPaths::ConvertRelativePathToFull(FPaths::ProjectDir() / FString::Printf(TEXT("../Reports/update_check_logs/popup_%s.png"),
				FAsdaLang::Code(FAsdaLang::Current())));
		}
		UE_LOG(LogAsdaUpdateTest, Log, TEXT("ОБНОВЛЕНИЯ язык сессии: %s; проверка в этом процессе %s"),
			FAsdaLang::Code(FAsdaLang::Current()), S->IsEnabled() ? TEXT("включена (есть state.json)") : TEXT("выключена (нет state.json — как и должно быть вне установки)"));

		// ── 1. Чистые функции ──
		const TArray<uint8> Root = AsdaUpdateVerify::RootPublicKey();
		TArray<uint8> RootFile;
		const FString RootPath = FPaths::ConvertRelativePathToFull(AsdaSelfTestTree::ProjectDir() / TEXT("../Releases/keys/root.pub"));
		Expect(R, FFileHelper::LoadFileToArray(RootFile, *RootPath) && RootFile == Root && Root.Num() == 32,
			FString::Printf(TEXT("1: вшитый корень == %s"), *RootPath));
		Expect(R, AsdaUpdateVerify::KeyId(Root).Len() == 16, FString::Printf(TEXT("1: id корня %s"), *AsdaUpdateVerify::KeyId(Root)));
		const TArray<uint8> FixRoot = FixtureRoot();
		Expect(R, FixRoot.Num() == 32 && AsdaUpdateVerify::KeyId(FixRoot) == TEXT("9df15db74947e0a0"), TEXT("1: корень образцов прочитан"));
		const FString Dir = AsdaSelfTestTree::ProjectDir() / TEXT("tests/update_check");
		const bool bFix = FFileHelper::LoadFileToString(R.Keyring, *(Dir / TEXT("keyring.json")))
			&& FFileHelper::LoadFileToString(R.Version, *(Dir / TEXT("version.json")));
		Expect(R, bFix, FString::Printf(TEXT("1: образцы ответа сервера прочитаны (%s)"), *Dir));

		// Вердикт → строка журнала с причиной ЭТОЙ проверки (вычисление до формирования текста).
		auto Check = [&](const FString& K, const FString& V, const TArray<uint8>& RootKey, const FAsdaInstalledVersion& I,
			EAsdaUpdateVerdict Want, const TCHAR* What)
		{
			FString Ver, Why;
			int64 Seq = -1;
			const EAsdaUpdateVerdict Got = AsdaUpdateVerify::Evaluate(K, V, RootKey, I, Ver, Seq, Why);
			static const TCHAR* Names[] = { TEXT("Newer"), TEXT("Same"), TEXT("Older"), TEXT("Invalid") };
			const bool bVer = (Want == EAsdaUpdateVerdict::Invalid) || (Ver == kFixtureVersion && Seq == kFixtureSeq);
			Expect(R, Got == Want && bVer, FString::Printf(TEXT("1: %s → %s (сервер «%s» seq %lld)%s%s"), What, Names[(int32)Got],
				*Ver, (long long)Seq, Why.IsEmpty() ? TEXT("") : TEXT(" — "), *Why));
		};
		{
			Check(R.Keyring, R.Version, FixRoot, Inst(kFixtureSeq), EAsdaUpdateVerdict::Same, TEXT("боевая подпись, установлена seq 6"));
			Check(R.Keyring, R.Version, FixRoot, Inst(kFixtureSeq - 1), EAsdaUpdateVerdict::Newer, TEXT("установлена seq 5"));
			Check(R.Keyring, R.Version, FixRoot, Inst(kFixtureSeq + 1), EAsdaUpdateVerdict::Older, TEXT("установлена seq 7 (сервер отстал)"));
			Check(R.Keyring, R.Version, FixRoot, Inst(kFixtureSeq - 1, /*KeyringSeq*/2), EAsdaUpdateVerdict::Invalid,
				TEXT("лаунчер видел keyring seq 2, сервер даёт 1"));
			Check(R.Keyring, R.Version, FixRoot, Inst(kFixtureSeq - 1, 1, TEXT("beta")), EAsdaUpdateVerdict::Invalid, TEXT("установлен канал beta"));
			// Подделанная нагрузка: один символ base64 заменён — подпись не сходится.
			FString Bad = R.Version;
			const int32 P = Bad.Find(TEXT("\"payload\": \"")) + 20;
			if (P > 20 && P < Bad.Len()) { Bad[P] = (Bad[P] == TCHAR('A')) ? TCHAR('B') : TCHAR('A'); }
			Check(R.Keyring, Bad, FixRoot, Inst(kFixtureSeq - 1), EAsdaUpdateVerdict::Invalid, TEXT("подделанный version.json"));
			Check(R.Keyring, R.Keyring, FixRoot, Inst(kFixtureSeq - 1), EAsdaUpdateVerdict::Invalid, TEXT("keyring вместо version.json"));
			TArray<uint8> OtherRoot = FixRoot; OtherRoot[0] ^= 0x01;
			Check(R.Keyring, R.Version, OtherRoot, Inst(kFixtureSeq - 1), EAsdaUpdateVerdict::Invalid, TEXT("чужой корень"));
			Check(TEXT("<html>502</html>"), R.Version, FixRoot, Inst(kFixtureSeq - 1), EAsdaUpdateVerdict::Invalid, TEXT("мусор вместо keyring"));
		}
		{
			FAsdaInstalledVersion I;
			Expect(R, AsdaUpdateVerify::ParseInstalledState(TEXT("{\"channel\":\"alpha\",\"version\":\"alpha-1.0.4-10.2026\",\"manifest\":\"x\",\"seq\":5,\"keyring_seq\":1,\"seq_max\":{\"alpha\":5},\"seen_manifest\":{}}"), I)
				&& I.Channel == TEXT("alpha") && I.Seq == 5 && I.KeyringSeq == 1 && I.Version == TEXT("alpha-1.0.4-10.2026"),
				TEXT("1: state.json лаунчера разобран (канал, версия, seq, keyring_seq)"));
			Expect(R, AsdaUpdateVerify::ParseInstalledState(TEXT("{\"version\":\"alpha-1.0.4-10.2026\",\"seq\":5}"), I) && I.Channel == TEXT("alpha"),
				TEXT("1: state.json без канала → канал игры alpha"));
			Expect(R, !AsdaUpdateVerify::ParseInstalledState(TEXT("{\"seq\":0,\"keyring_seq\":1}"), I),
				TEXT("1: state.json без версии (игра не установлена) → проверки нет"));
			Expect(R, !AsdaUpdateVerify::ParseInstalledState(TEXT("не json"), I), TEXT("1: повреждённый state.json → проверки нет"));
		}

		// ── 2. Подмена ответа «новая версия» → штатное окно, один раз ──
		S->DebugBeginForTest(Inst(kFixtureSeq - 1), kDeadUrl, FixtureRoot());
		const int32 QueueBefore = WM->GetMsgBoxQueueNum();
		S->DebugInjectResponses(true, R.Keyring, R.Version);
		Expect(R, S->GetLastResult() == EAsdaUpdateCheckResult::NewVersion && S->IsUpdateFound() && S->GetPopupShowCount() == 1
			&& S->GetSecondsToNextCheck() < 0.0, TEXT("2: новая версия найдена, окно поставлено один раз, проверки остановлены"));
		CheckPopup(R, S, WM, TEXT("2"));
		S->DebugInjectResponses(true, R.Keyring, R.Version);
		S->ShowUpdatePopup(false);
		Expect(R, S->GetPopupShowCount() == 1 && WM->GetMsgBoxQueueNum() == QueueBefore + 1,
			FString::Printf(TEXT("2: повторный ответ и повторный показ — окно не плодится (показов %d, записей очереди +%d)"),
				S->GetPopupShowCount(), WM->GetMsgBoxQueueNum() - QueueBefore));
		S->DebugEndForTest();
		Expect(R, !S->IsPopupQueued() && WM->GetMsgBoxQueueNum() == QueueBefore, TEXT("2: окно снято самотестом без нажатия кнопки"));

		// ── 3. Та же версия / сбой сети / подпись не сошлась → окна нет, повтор через интервал ──
		auto NextOk = [S]()
		{
			const double N = S->GetSecondsToNextCheck();
			return N > UAsdaUpdateCheckSubsystem::kDefaultIntervalSec - 5.0 && N <= UAsdaUpdateCheckSubsystem::kDefaultIntervalSec + 1.0;
		};
		S->DebugBeginForTest(Inst(kFixtureSeq), kDeadUrl, FixtureRoot());
		S->DebugInjectResponses(true, R.Keyring, R.Version);
		Expect(R, S->GetLastResult() == EAsdaUpdateCheckResult::UpToDate && !S->IsPopupQueued() && S->GetPopupShowCount() == 0 && NextOk(),
			FString::Printf(TEXT("3: та же версия → окна нет, следующая проверка через %.0f с"), S->GetSecondsToNextCheck()));
		S->DebugBeginForTest(Inst(kFixtureSeq - 1), kDeadUrl, FixtureRoot());
		S->DebugInjectResponses(false, FString(), FString());
		Expect(R, S->GetLastResult() == EAsdaUpdateCheckResult::NetFail && !S->IsPopupQueued() && NextOk(),
			FString::Printf(TEXT("3: сбой сети → молча, окна нет, повтор через %.0f с"), S->GetSecondsToNextCheck()));
		{
			FString Bad = R.Version;
			const int32 P = Bad.Find(TEXT("\"payload\": \"")) + 20;
			if (P > 20 && P < Bad.Len()) { Bad[P] = (Bad[P] == TCHAR('A')) ? TCHAR('B') : TCHAR('A'); }
			S->DebugBeginForTest(Inst(kFixtureSeq - 1), kDeadUrl, FixtureRoot());
			S->DebugInjectResponses(true, R.Keyring, Bad);
			Expect(R, S->GetLastResult() == EAsdaUpdateCheckResult::Invalid && !S->IsPopupQueued() && NextOk(),
				TEXT("3: подпись не сошлась → окна нет (закрыть игру подделкой нельзя), повтор через интервал"));
		}

		// ── 4–5. На тиках: снесённая запись возвращается, геометрия, настоящий сбой сети ──
		S->DebugBeginForTest(Inst(kFixtureSeq - 1), kDeadUrl, FixtureRoot());
		S->DebugInjectResponses(true, R.Keyring, R.Version);
		WM->RemoveMsgBoxEntries(0, UAsdaUpdateCheckSubsystem::kLocText, TEXT("самотест: как сброс очереди сменой карты"), /*bRedisplayNow*/false);
		Expect(R, !S->IsPopupQueued() && S->IsUpdateFound(), TEXT("4: запись окна снесена, защёлка «есть обновление» осталась"));
		R.Phase = 0;
		R.PhaseStart = FPlatformTime::Seconds();
		R.Tick = FTSTicker::GetCoreTicker().AddTicker(FTickerDelegate::CreateStatic(&OnTick), 0.1f);
	}
}

static FAutoConsoleCommandWithWorldAndArgs GAsdaSelfTestUpdateCheck(
	TEXT("asda.SelfTest.UpdateCheck"),
	TEXT("Самотест проверки новой версии: подпись боевым корнем на образцах сервера, сравнение seq, подмена ответа (новая версия → "
	     "окно ONEBUT 1500 один раз, одна кнопка «Закрыть игру», модально; та же версия / сбой / подделка → окна нет, повтор), "
	     "возврат окна после сноса, настоящий сбой сети. Только офлайн. `shot` — снимок кадра с окном в Reports/update_check_logs. `quit`."),
	FConsoleCommandWithWorldAndArgsDelegate::CreateLambda([](const TArray<FString>& A, UWorld* W)
	{
		AsdaUpdateCheckSelfTest::Run(A, W);
	}));

#endif // ASDA_DEV_CONSOLE && !UE_BUILD_SHIPPING
