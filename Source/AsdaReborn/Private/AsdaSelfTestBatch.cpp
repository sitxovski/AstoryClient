// Copyright (c) 2026 @sitxovski (Discord)
// Пакетный прогон самотестов в одной игре — разбор у объявления (Public/AsdaSelfTestBatch.h).
//
// КОМАНДА. `asda.SelfTest.Batch file=<путь> [quit] [timeout=<с>] [settle=<с>] [start=<с>]`
//            `asda.SelfTest.Batch list=<команда1>|<команда2>|… [quit] …`   (команды без аргументов)
//   Файл списка (UTF-8): одна строка — один самотест: `<команда> [аргументы…] [@timeout=<с>]`; `#` — комментарий.
//   `quit` у самотеста добавляется сам (по нему пакет узнаёт конец). `timeout` — потолок на самотест по умолчанию (600 с),
//   `settle` — пауза после конца самотеста (1.5 с: отложенные на следующий кадр шаги самотеста), `start` — пауза до первого.
//   `quit` у пакета — выйти из игры после общего итога.
// СТРОКИ ЛОГА (их разбирает Tools/asda_selftest_batch.py — менять согласованно):
//   SELFTEST_BATCH НАЧАЛО <i>/<N> <имя>: <командная строка>
//   SELFTEST_BATCH КОНЕЦ <i>/<N> <имя>: <ok|fail|timeout> (<с> с) итог=«…» fail-строк=<n>[ утечка ключей=<n>]
//   SELFTEST_BATCH РЕЗУЛЬТАТ <имя> <ok|fail|timeout|skipped>
//   SELFTEST_BATCH ИТОГ: самотестов <N>, ok <a>, fail <b>, timeout <c>, skipped <d>
// ★ 11.10.2026 (В3 S1): `between=к1;к2` — команды между самотестами; в строке КОНЕЦ — « утечка перехвата=1» (перехват отправок
//   снят пакетом) и « свой изменён: …» (что из своего персонажа самотест оставил другим — только запись).
// ВЕРДИКТ самотеста: fail — в последней строке «ИТОГ» число провалов > 0 (fail=N / провалов N / FAIL N), или в ней ОТКАЗ/ПРЕРВАН,
//   или были строки уровня Error со словом FAIL; ok — `quit` пришёл и ничего из этого; timeout — потолок (пакет останавливается,
//   остальные skipped: поздний `quit` зависшего самотеста нельзя отличить от `quit` следующего).
// Только офлайн: как у самих самотестов (их офлайн-гейт `FAsdaOutgoingGate` работает и внутри пакета).
#include "AsdaSelfTestBatch.h"

#include "AsdaCVarSave.h"
#include "Test/AsdaSelfTestNet.h"   // ★ R1: тонкий доступ самотестов к сети (вместо AsdaNetSubsystem.h)
#include "Engine/GameInstance.h"
#include "Test/AsdaSelfTestKit.h"   // ★ 11.10.2026 (В3 S1): проверка утечки перехвата отправок между самотестами
#include "Containers/Ticker.h"
#include "Engine/Engine.h"
#include "Engine/World.h"
#include "HAL/IConsoleManager.h"
#include "Misc/FileHelper.h"
#include "Misc/OutputDeviceRedirector.h"
#include "Misc/Paths.h"

DEFINE_LOG_CATEGORY_STATIC(LogAsdaSelfTestBatch, Log, All);

#if !UE_BUILD_SHIPPING

namespace AsdaSelfTestBatch
{
	// Один самотест списка.
	struct FItem
	{
		FString Cmd;              // командная строка (с добавленным `quit`)
		FString Name;             // имя команды (первое слово)
		double TimeoutSec = 0.0;  // потолок времени
		FString Status;           // ok / fail / timeout / skipped (не запускался: предыдущий упёрся в потолок)
		FString Itog;             // последняя строка «ИТОГ» самотеста
		int32 FailLines = 0;      // строк уровня Error со словом FAIL
		int32 LeakedKeys = 0;     // ключей, не возвращённых самим самотестом
		bool bLeakedCapture = false;   // ★ В3 S1: самотест оставил взведённым перехват отправок (снят пакетом)
		FString SelfDiff;         // ★ В3 S1: что из «своего» персонажа самотест оставил изменённым (только запись в КОНЕЦ)
		double Sec = 0.0;         // длительность
	};

	// Сбор строк лога текущего самотеста (поток любой — под замком).
	class FCapture : public FOutputDevice
	{
	public:
		FCriticalSection Lock;    // защита полей ниже
		bool bOn = false;         // идёт самотест
		FString LastItog;         // последняя строка со словом «ИТОГ» (кроме строк самого пакета)
		int32 FailLines = 0;      // строк Error/Fatal со словом FAIL

		virtual bool CanBeUsedOnAnyThread() const override { return true; }   // строки любого потока (вместе со строкой ниже)
		virtual bool CanBeUsedOnMultipleThreads() const override { return true; }   // ★ 05.11.2026: НЕбуферизованный список UE 5.8 — строка доходит в момент записи (иначе гонка с потоком логов: строка после RemoveOutputDevice/проверки)
		// Приём строки лога.
		virtual void Serialize(const TCHAR* V, ELogVerbosity::Type Verbosity, const FName& Category) override
		{
			if (!V) { return; }
			FScopeLock L(&Lock);
			if (!bOn) { return; }
			if (Category == FName(TEXT("LogAsdaSelfTestBatch"))) { return; }
			const FString S(V);
			if (S.Contains(TEXT("ИТОГ"))) { LastItog = S.Left(300); }
			const ELogVerbosity::Type Vb = (ELogVerbosity::Type)(Verbosity & ELogVerbosity::VerbosityMask);
			if ((Vb == ELogVerbosity::Error || Vb == ELogVerbosity::Fatal) && S.Contains(TEXT("FAIL"), ESearchCase::CaseSensitive)) { ++FailLines; }
		}
		// Начать сбор для нового самотеста.
		void Begin() { FScopeLock L(&Lock); bOn = true; LastItog.Reset(); FailLines = 0; }
		// Закончить сбор и вернуть собранное.
		void End(FString& OutItog, int32& OutFail) { FScopeLock L(&Lock); bOn = false; OutItog = LastItog; OutFail = FailLines; }
	};

	// Состояние пакета.
	struct FBatch
	{
		TArray<FItem> Items;          // список
		int32 Cur = -1;               // текущий самотест
		enum class EState : uint8 { Start, Running, Settle, Done } State = EState::Start;   // фаза
		double PhaseT0 = 0.0;         // начало фазы
		double TestT0 = 0.0;          // начало самотеста
		double StartSec = 2.0;        // пауза до первого
		double SettleSec = 1.5;       // пауза после конца самотеста
		bool bQuitAtEnd = false;      // выйти после итога
		bool bQuitSeen = false;       // текущий самотест вызвал `quit`
		TArray<FString> Between;      // ★ В3 S1: команды между самотестами (`between=к1;к2`), по умолчанию нет
		// ★ В3 S1: «свой» персонаж на старте самотеста — номер, известность, charId, уровень, смерть (для записи утечки в КОНЕЦ).
		struct FSelf { int32 UserNum = 0; bool bKnown = false; int32 CharId = 0; int32 Level = 0; bool bDead = false; } SelfAtStart;
		FCapture Capture;             // сбор строк лога
		FTSTicker::FDelegateHandle Tick;   // тикер пакета (ядро — переживает смену мира)
	};

	// Единственный пакет процесса (null — пакета нет).
	static TUniquePtr<FBatch>& Batch()
	{
		static TUniquePtr<FBatch> GBatch;
		return GBatch;
	}

	// Игровой мир (для Exec команд самотестов).
	static UWorld* GameWorld()
	{
		if (!GEngine) { return nullptr; }
		for (const FWorldContext& C : GEngine->GetWorldContexts())
		{
			if ((C.WorldType == EWorldType::Game || C.WorldType == EWorldType::PIE) && C.World()) { return C.World(); }
		}
		return nullptr;
	}

	// ★ В3 S1: снимок «своего» сетевой подсистемы (для сравнения начала и конца самотеста).
	static bool SnapSelf(FBatch::FSelf& Out)
	{
		UWorld* W = GameWorld();
		const UAsdaNetSubsystem* Net = W && W->GetGameInstance() ? AsdaTestNet::Get(W->GetGameInstance()) : nullptr;
		if (!Net) { return false; }
		Out.UserNum = AsdaTestNet::SelfUserNum(Net); Out.bKnown = AsdaTestNet::bSelfUserNumKnown(Net); Out.CharId = AsdaTestNet::SelectedCharId(Net);
		Out.Level = AsdaTestNet::SelfLevel(Net); Out.bDead = AsdaTestNet::bSelfDead(Net);
		return true;
	}

	// Число «провалов» из строки ИТОГ: fail=N / провалов N / FAIL N (максимум найденного); -1 — чисел нет.
	static int32 ParseFailCount(const FString& Itog)
	{
		int32 Best = -1;
		const TCHAR* Keys[] = { TEXT("fail="), TEXT("провалов"), TEXT("FAIL") };
		for (const TCHAR* K : Keys)
		{
			int32 From = 0;
			while (true)
			{
				const int32 P = Itog.Find(K, ESearchCase::CaseSensitive, ESearchDir::FromStart, From);
				if (P == INDEX_NONE) { break; }
				int32 I = P + FCString::Strlen(K);
				while (I < Itog.Len() && (Itog[I] == TEXT(' ') || Itog[I] == TEXT(':'))) { ++I; }
				int32 J = I;
				while (J < Itog.Len() && FChar::IsDigit(Itog[J])) { ++J; }
				if (J > I) { Best = FMath::Max(Best, FCString::Atoi(*Itog.Mid(I, J - I))); }
				From = P + 1;
			}
		}
		return Best;
	}

	// Завершить текущий самотест: вердикт, возврат забытых ключей, строка КОНЕЦ.
	static void FinishCurrent(FBatch& B, bool bTimeout)
	{
		FItem& It = B.Items[B.Cur];
		B.Capture.End(It.Itog, It.FailLines);
		It.Sec = FPlatformTime::Seconds() - B.TestT0;
		if (bTimeout) { It.Status = TEXT("timeout"); }
		else
		{
			const int32 FailN = ParseFailCount(It.Itog);
			const bool bRefused = It.Itog.Contains(TEXT("ОТКАЗ")) || It.Itog.Contains(TEXT("ПРЕРВАН"));
			It.Status = (FailN > 0 || It.FailLines > 0 || bRefused) ? TEXT("fail") : TEXT("ok");
		}
		// Независимость: ключи, которые самотест не вернул сам, возвращаются здесь — следующий самотест стартует с исходными.
		It.LeakedKeys = AsdaCVarSave::Registry().Num();
		if (It.LeakedKeys > 0)
		{
			UE_LOG(LogAsdaSelfTestBatch, Warning, TEXT("SELFTEST_BATCH %s: самотест оставил подменённые ключи (%d) — возврат пакетом"), *It.Name, It.LeakedKeys);
			AsdaCVarSave::RestoreAll(*FString::Printf(TEXT("asda.SelfTest.Batch/%s"), *It.Name));
		}
		AsdaCVarSave::DisarmWorldGuard(*It.Name);
		// ★ 11.10.2026 (В3 S1, N1(в)): перехват отправок, оставленный самотестом, следующий самотест каркаса получил бы как
		//   «занят» (отказ). Пакет снимает его и отмечает утечку. Снятие — штатным `DisarmSendCapture` текущего поколения.
		{
			UWorld* W = GameWorld();
			UAsdaNetSubsystem* Net = W && W->GetGameInstance() ? AsdaTestNet::Get(W->GetGameInstance()) : nullptr;
			if (Net && AsdaSelfTestKit::IsCaptureBusy(Net))
			{
				It.bLeakedCapture = true;
				UE_LOG(LogAsdaSelfTestBatch, Warning, TEXT("SELFTEST_BATCH %s: самотест оставил перехват отправок взведённым — снят пакетом"), *It.Name);
				AsdaSelfTestKit::DisarmLeakedCapture(Net);
			}
		}
		// ★ В3 S1: «свой» персонаж — только запись (возврат — дело самого самотеста, `FAsdaNetSelfScope`): одиночный прогон стартует
		//   с другим «своим», чем пакетный, и молча подменять его пакетом нельзя.
		{
			FBatch::FSelf N;
			if (SnapSelf(N))
			{
				const FBatch::FSelf& O = B.SelfAtStart;
				if (N.UserNum != O.UserNum) { It.SelfDiff += FString::Printf(TEXT(" userNum %d→%d"), O.UserNum, N.UserNum); }
				if (N.bKnown != O.bKnown) { It.SelfDiff += FString::Printf(TEXT(" известен %d→%d"), O.bKnown ? 1 : 0, N.bKnown ? 1 : 0); }
				if (N.CharId != O.CharId) { It.SelfDiff += FString::Printf(TEXT(" charId %d→%d"), O.CharId, N.CharId); }
				if (N.Level != O.Level) { It.SelfDiff += FString::Printf(TEXT(" уровень %d→%d"), O.Level, N.Level); }
				if (N.bDead != O.bDead) { It.SelfDiff += FString::Printf(TEXT(" смерть %d→%d"), O.bDead ? 1 : 0, N.bDead ? 1 : 0); }
			}
		}
		UE_LOG(LogAsdaSelfTestBatch, Display, TEXT("SELFTEST_BATCH КОНЕЦ %d/%d %s: %s (%.1f с) итог=«%s» fail-строк=%d%s%s%s"),
			B.Cur + 1, B.Items.Num(), *It.Name, *It.Status, It.Sec, It.Itog.IsEmpty() ? TEXT("нет строки ИТОГ") : *It.Itog, It.FailLines,
			It.LeakedKeys > 0 ? *FString::Printf(TEXT(" утечка ключей=%d"), It.LeakedKeys) : TEXT(""),
			It.bLeakedCapture ? TEXT(" утечка перехвата=1") : TEXT(""),
			It.SelfDiff.IsEmpty() ? TEXT("") : *FString::Printf(TEXT(" свой изменён:%s"), *It.SelfDiff));
	}

	// Запустить самотест с номером B.Cur.
	static void StartCurrent(FBatch& B)
	{
		FItem& It = B.Items[B.Cur];
		B.bQuitSeen = false;
		B.TestT0 = FPlatformTime::Seconds();
		UE_LOG(LogAsdaSelfTestBatch, Display, TEXT("SELFTEST_BATCH НАЧАЛО %d/%d %s: %s"), B.Cur + 1, B.Items.Num(), *It.Name, *It.Cmd);
		B.Capture.Begin();
		SnapSelf(B.SelfAtStart);   // ★ В3 S1: «свой» на старте — для записи утечки в КОНЕЦ
		if (GEngine) { GEngine->Exec(GameWorld(), *It.Cmd); }
	}

	// Общий итог и (с `quit`) выход. Пакет снимается ДО выхода — `quit` уже не перехватывается.
	static void FinishAll()
	{
		TUniquePtr<FBatch>& P = Batch();
		if (!P) { return; }
		FBatch& B = *P;
		int32 NOk = 0, NFail = 0, NTo = 0, NSkip = 0;
		for (FItem& It : B.Items)
		{
			if (It.Status.IsEmpty()) { It.Status = TEXT("skipped"); }
			UE_LOG(LogAsdaSelfTestBatch, Display, TEXT("SELFTEST_BATCH РЕЗУЛЬТАТ %s %s"), *It.Name, *It.Status);
			NOk += It.Status == TEXT("ok"); NFail += It.Status == TEXT("fail"); NTo += It.Status == TEXT("timeout"); NSkip += It.Status == TEXT("skipped");
		}
		UE_LOG(LogAsdaSelfTestBatch, Display, TEXT("SELFTEST_BATCH ИТОГ: самотестов %d, ok %d, fail %d, timeout %d, skipped %d"), B.Items.Num(), NOk, NFail, NTo, NSkip);
		if (GLog) { GLog->RemoveOutputDevice(&B.Capture); }
		const bool bQuit = B.bQuitAtEnd;
		P.Reset();   // тикер снимается возвратом false из TickBatch (FinishAll зовётся только оттуда)
		if (bQuit && GEngine) { GEngine->Exec(GameWorld(), TEXT("quit")); }
	}

	// Шаг пакета (тикер ядра, раз в 0.1 с).
	static bool TickBatch(float)
	{
		TUniquePtr<FBatch>& P = Batch();
		if (!P) { return false; }
		FBatch& B = *P;
		const double Now = FPlatformTime::Seconds();
		switch (B.State)
		{
		case FBatch::EState::Start:
			if (Now - B.PhaseT0 < B.StartSec) { return true; }
			B.Cur = 0;
			B.State = FBatch::EState::Running;
			StartCurrent(B);
			return true;
		case FBatch::EState::Running:
			if (B.bQuitSeen) { B.State = FBatch::EState::Settle; B.PhaseT0 = Now; return true; }
			if (Now - B.TestT0 > B.Items[B.Cur].TimeoutSec)
			{
				// Потолок: самотест, возможно, ещё идёт на своих таймерах, и его поздний `quit` нельзя отличить от `quit`
				//   следующего. Чтобы не приписать чужой вердикт, пакет ОСТАНАВЛИВАЕТСЯ: остальные — skipped.
				FinishCurrent(B, /*bTimeout*/ true);
				UE_LOG(LogAsdaSelfTestBatch, Error, TEXT("SELFTEST_BATCH %s: потолок %.0f с — пакет остановлен, остальные не запускаются"),
					*B.Items[B.Cur].Name, B.Items[B.Cur].TimeoutSec);
				B.State = FBatch::EState::Done; FinishAll(); return false;
			}
			return true;
		case FBatch::EState::Settle:
			if (Now - B.PhaseT0 < B.SettleSec) { return true; }
			FinishCurrent(B, /*bTimeout*/ false);
			if (++B.Cur >= B.Items.Num()) { B.State = FBatch::EState::Done; FinishAll(); return false; }
			// ★ В3 S1: команды между самотестами (`between=`), если заданы — до старта следующего.
			for (const FString& C : B.Between)
			{
				UE_LOG(LogAsdaSelfTestBatch, Display, TEXT("SELFTEST_BATCH между самотестами: %s"), *C);
				if (GEngine) { GEngine->Exec(GameWorld(), *C); }
			}
			B.State = FBatch::EState::Running;
			StartCurrent(B);
			return true;
		default:
			return false;
		}
	}

	// Разобрать строку списка в элемент (false — пустая/комментарий).
	static bool ParseLine(const FString& Raw, double DefTimeout, FItem& Out)
	{
		FString L = Raw.TrimStartAndEnd();
		if (L.IsEmpty() || L.StartsWith(TEXT("#"))) { return false; }
		TArray<FString> Tok;
		L.ParseIntoArrayWS(Tok);
		if (Tok.Num() == 0) { return false; }
		Out.Name = Tok[0];
		Out.TimeoutSec = DefTimeout;
		TArray<FString> Keep;
		bool bHasQuit = false;
		for (const FString& T : Tok)
		{
			if (T.StartsWith(TEXT("@timeout="))) { Out.TimeoutSec = FMath::Max(1.0, (double)FCString::Atof(*T.Mid(9))); continue; }
			if (T.Equals(TEXT("quit"), ESearchCase::IgnoreCase)) { bHasQuit = true; }
			Keep.Add(T);
		}
		if (!bHasQuit) { Keep.Add(TEXT("quit")); }   // по `quit` пакет узнаёт конец самотеста
		Out.Cmd = FString::Join(Keep, TEXT(" "));
		return true;
	}

	bool IsActive() { return Batch().IsValid(); }

	bool InterceptQuit(const TCHAR* Cmd)
	{
		TUniquePtr<FBatch>& P = Batch();
		if (!P || !Cmd) { return false; }
		const TCHAR* T = Cmd;
		if (!FParse::Command(&T, TEXT("QUIT")) && !FParse::Command(&T, TEXT("EXIT"))) { return false; }
		if (P->State == FBatch::EState::Running && !P->bQuitSeen)
		{
			P->bQuitSeen = true;
			UE_LOG(LogAsdaSelfTestBatch, Display, TEXT("SELFTEST_BATCH %s: самотест вызвал quit — конец, выход перехвачен"),
				P->Items.IsValidIndex(P->Cur) ? *P->Items[P->Cur].Name : TEXT("?"));
		}
		else
		{
			UE_LOG(LogAsdaSelfTestBatch, Warning, TEXT("SELFTEST_BATCH: quit вне хода самотеста (фаза %d) — перехвачен, игра продолжает пакет"), (int32)P->State);
		}
		return true;
	}

	void Quit(UWorld* W)
	{
		if (GEngine) { GEngine->Exec(W, TEXT("quit")); }
	}
}

// Консольная команда пакета.
static FAutoConsoleCommandWithWorldAndArgs GAsdaSelfTestBatch(
	TEXT("asda.SelfTest.Batch"),
	TEXT("asda.SelfTest.Batch file=<путь>|list=<к1>|<к2>… [quit] [timeout=600] [settle=1.5] [start=2] — в ОДНОЙ игре по очереди ")
	TEXT("запускает самотесты (каждому добавляется `quit`, его перехват = конец самотеста), пишет ok/fail/timeout по каждому и ")
	TEXT("общий итог `SELFTEST_BATCH ИТОГ`; с `quit` — выход. Обёртка: Tools/asda_selftest_batch.py."),
	FConsoleCommandWithWorldAndArgsDelegate::CreateLambda([](const TArray<FString>& A, UWorld* W)
	{
		using namespace AsdaSelfTestBatch;
		if (Batch()) { UE_LOG(LogAsdaSelfTestBatch, Error, TEXT("SELFTEST_BATCH: пакет уже идёт — повторный запуск отклонён")); return; }
		TUniquePtr<FBatch> B = MakeUnique<FBatch>();
		double DefTimeout = 600.0;
		FString File, List;
		for (const FString& Arg : A)
		{
			if (Arg.StartsWith(TEXT("file="))) { File = Arg.Mid(5).TrimQuotes(); }
			else if (Arg.StartsWith(TEXT("list="))) { List = Arg.Mid(5); }
			else if (Arg.StartsWith(TEXT("timeout="))) { DefTimeout = FMath::Max(1.0, (double)FCString::Atof(*Arg.Mid(8))); }
			else if (Arg.StartsWith(TEXT("settle="))) { B->SettleSec = FMath::Max(0.0, (double)FCString::Atof(*Arg.Mid(7))); }
			else if (Arg.StartsWith(TEXT("start="))) { B->StartSec = FMath::Max(0.0, (double)FCString::Atof(*Arg.Mid(6))); }
			else if (Arg.Equals(TEXT("quit"), ESearchCase::IgnoreCase)) { B->bQuitAtEnd = true; }
			// ★ 11.10.2026 (В3 S1): `between=к1;к2` — консольные команды между самотестами (без аргументов).
			else if (Arg.StartsWith(TEXT("between=")))
			{
				Arg.Mid(8).ParseIntoArray(B->Between, TEXT(";"), true);
			}
		}
		TArray<FString> Lines;
		if (!File.IsEmpty())
		{
			if (!FFileHelper::LoadFileToStringArray(Lines, *File))
			{
				UE_LOG(LogAsdaSelfTestBatch, Error, TEXT("SELFTEST_BATCH ИТОГ: ОТКАЗ — не прочитан файл списка %s"), *File);
				if (B->bQuitAtEnd && GEngine) { GEngine->Exec(W, TEXT("quit")); }
				return;
			}
		}
		if (!List.IsEmpty()) { List.ParseIntoArray(Lines, TEXT("|"), true); }
		for (const FString& L : Lines) { FItem It; if (ParseLine(L, DefTimeout, It)) { B->Items.Add(MoveTemp(It)); } }
		if (B->Items.Num() == 0)
		{
			UE_LOG(LogAsdaSelfTestBatch, Error, TEXT("SELFTEST_BATCH ИТОГ: ОТКАЗ — пустой список самотестов"));
			if (B->bQuitAtEnd && GEngine) { GEngine->Exec(W, TEXT("quit")); }
			return;
		}
		UE_LOG(LogAsdaSelfTestBatch, Display, TEXT("SELFTEST_BATCH: самотестов %d, потолок %.0f с, оседание %.1f с, выход в конце=%d"),
			B->Items.Num(), DefTimeout, B->SettleSec, B->bQuitAtEnd ? 1 : 0);
		B->PhaseT0 = FPlatformTime::Seconds();
		if (GLog) { GLog->AddOutputDevice(&B->Capture); }
		Batch() = MoveTemp(B);
		Batch()->Tick = FTSTicker::GetCoreTicker().AddTicker(FTickerDelegate::CreateStatic(&TickBatch), 0.1f);
	}));

#else // UE_BUILD_SHIPPING — пакета нет, `quit` не перехватывается

namespace AsdaSelfTestBatch
{
	bool IsActive() { return false; }
	bool InterceptQuit(const TCHAR*) { return false; }
	void Quit(UWorld* W) { if (GEngine) { GEngine->Exec(W, TEXT("quit")); } }
}

#endif // !UE_BUILD_SHIPPING
