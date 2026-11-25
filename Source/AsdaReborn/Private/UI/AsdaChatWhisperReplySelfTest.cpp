// Copyright (c) 2026 @sitxovski (Discord)
// ★★★ САМОТЕСТ «R — ОТВЕТ ПОСЛЕДНЕМУ ШЕПТАВШЕМУ» ПУТЁМ ИГРОКА (05.11.2026, баг игроков). [ОРИГИНАЛ]/[ИСПРАВЛЕНИЕ]
//
// ЭТАЛОН. Приём `Recv_ChatWhisper@0xBA7530` (network.c:10494-10660): входящий шёпот (msgType 1, flag 0, отправитель не сам игрок)
//   кладёт имя в `chat+2552` (`memcpy(pThis + 2552, &pkt[2], 0x14)`, network.c:10646); эхо своего шёпота (flag ≠ 0) и служебные
//   msgType 0/2/3 поле не трогают. Клавиша команды 69 «CHAT ANSWER» (по умолчанию R, `system/Shortcutkey.ini`) — диспетч
//   `input.c:4407-4409` → `ChatInput_ParseWhisper(chat, 1)` (chat.c:2873-2900): шептавшего нет → loc 6942 строкой ленты кат. 5 и
//   очистка поля; есть → в ПОЛЕ ВВОДА пишется приглашение loc 5323 «[Шепчемся с персонажем %s]», режим 7, адресат = шептавший,
//   фокус в поле. Буфер один (20 байт) — перебора нескольких шептавших нет, R всегда отвечает ПОСЛЕДНЕМУ. Смена карты буфер не
//   трогает, выход на выбор персонажа чистит (`playerStateReset`, entity_char.c:44525).
//
// ЧТО ПРОВЕРЯЕТ (настоящий путь: приём `DebugFeedPacket(5089)` → `OnChatLine` → окно чата; клавиша — ввод контроллера `InputKey`
//   → `PollKeys` (фронт отпускания) → `ExecuteCommand(69)` → `AnswerLastWhisper`; отправка — `SubmitInput` под перехватом):
//   N0 шёпота не было, `asda.Chat.WhisperAnswerNoTargetSilent` 0 (оригинал): R → ввод НЕ открыт, режим не тронут, loc 6942 в ленте +1.
//   N  то же при ключе 1 (исправление 08.11): строки 6942 нет, поле без «/w». NM — то же после смены карты без шёпота.
//   A  входящий 5089 от Alice → R: ввод открыт, режим 7, адресат Alice; поле: «/w Alice » (ключ `asda.Chat.WhisperAnswerPrefill` 1)
//      или пусто (ключ 0, прежнее); Enter с текстом → 5088 вида 0 на Alice.
//   B  два шептавших подряд (Alice, затем Bob) → R отвечает Bob (последнему, без перебора).
//   E  эхо своего шёпота (flag 1) от «Carl» → последний шептавший остаётся Bob.
//   U  5089 с WORD +6 (userNum получателя) ≠ свой `SelfUserNum`: ключ `asda.Chat.WhisperUserNumCheck` 1 — строки нет, R → Bob;
//      ключ 0 — прежнее: строка есть, R → Dan.
//   F  чат с Другом (5089 flag 1): эхо своего (charId = свой, имя = партнёр) → «Игроку Pal: hi» (`asda.Chat.SoulmateToFormat` 1),
//      «[своё имя]: hi» (0 + `asda.Chat.SoulmateSelfName` 1, эталон), «[Pal]: hi» (0/0, прежнее); входящее от Друга — «От Pal: hi»
//      (loc 991200, ключ формата 1) или «[Pal]: hi» (ключ 0).
//   S  входящий 5089 с charId = свой (`SelectedCharId`): ключ `asda.Chat.WhisperSelfBranch` 1 — строка из loc 5431, R → Bob
//      (эталон network.c:10596-10631 не делает memcpy адресата); ключ 0 — прежнее «[Me]: …», R → Me.
//   T  строка ввода уже открыта (Enter, режим 4) → R не исполняется как команда: режим 4.
//   M  смена карты (`MapChanged`) → R снова отвечает Bob.
//   L  выход на логин (`LeftWorld`) → R молчит (ключ 1): ни 6942, ни ввода.
//   K  ключ `asda.Chat.WhisperAnswer 0` — прежнее: шептавший не запоминается, R открывает ввод в текущем режиме.
// Консоль: `asda.SelfTest.WhisperReply [delay=N] [quit]`. Только офлайн. Строки — «САМОТЕСТ-ОТВЕТ-R», итог — «… ИТОГ: ok=… fail=…».
#include "CoreMinimal.h"
#include "AsdaDevConsole.h"

#if ASDA_DEV_CONSOLE && !UE_BUILD_SHIPPING

#include "AsdaBytes.h"
#include "AsdaNetSubsystem.h"
#include "AsdaSessionReset.h"
#include "Test/AsdaSelfTestKit.h"
#include "UI/AsdaChatWindowSubsystem.h"
#include "UI/AsdaNotice.h"
#include "World/AsdaPlayerController.h"
#include "World/AsdaWorldSubsystem.h"   // своё имя (GetSelfCharName) — раздел F
#include "World/AsdaKeybindConfig.h"
#include "Containers/Ticker.h"
#include "Engine/Engine.h"
#include "Engine/GameInstance.h"
#include "Engine/World.h"
#include "HAL/IConsoleManager.h"
#include "HAL/PlatformTime.h"
#include "InputCoreTypes.h"
#include "UnrealClient.h"   // FScreenshotRequest: кадры N0/N
#include "Misc/Paths.h"
#include "Misc/Parse.h"         // FParse::Value: язык прогона для имени кадра
#include "Misc/CommandLine.h"   // FCommandLine::Get
#include "HAL/FileManager.h"    // IFileManager: папка кадров

DEFINE_LOG_CATEGORY_STATIC(LogAsdaWhisperReplyTest, Log, All);   // канал строк самотеста

namespace AsdaWhisperReplyTest
{
	static const TCHAR* kCmd = TEXT("asda.SelfTest.WhisperReply");   // имя команды (каркас, ключи, итог)
	static const TCHAR* kPrefix = TEXT("САМОТЕСТ-ОТВЕТ-R");          // префикс строк и итога
	static const TCHAR* kSelfName = TEXT("WrTester");                 // имя своего персонажа при офлайн-спавне (раздел F)

	// Один шаг сценария: задержка от начала шага, тело (true — шаг завершён).
	struct FStep { FString Name; double Delay = 0.0; TFunction<bool()> Fn; };

	// Состояние прогона.
	struct FState
	{
		TWeakObjectPtr<UGameInstance> GI;          // игра прогона
		bool bQuit = false;                        // выйти после прогона
		int32 Ok = 0, Fail = 0;                    // пройдено / провалено
		TArray<FStep> Steps;                       // шаги
		int32 Next = 0;                            // текущий шаг
		double StepT = 0.0;                        // время входа в шаг
		int32 Base6942 = 0;                        // строк 6942 в слоте 7 до нажатия
		FString L6942;                             // текст loc 6942 на языке сессии
		TArray<TPair<uint16, TArray<uint8>>> Outs; // перехваченные исходящие
		uint32 CapGen = 0;                         // поколение перехвата
		TUniquePtr<FAsdaCVarScope> Keys;           // ключи прогона (возврат на выходе)

		UWorld* W() const { return GI.IsValid() ? GI->GetWorld() : nullptr; }
		APlayerController* PC() const { UWorld* Wd = W(); return Wd ? Wd->GetFirstPlayerController() : nullptr; }
		UAsdaNetSubsystem* Net() const { return GI.IsValid() ? GI->GetSubsystem<UAsdaNetSubsystem>() : nullptr; }
		UAsdaChatWindowSubsystem* Chat() const { return GI.IsValid() ? GI->GetSubsystem<UAsdaChatWindowSubsystem>() : nullptr; }
		UAsdaSessionResetSubsystem* Reset() const { return GI.IsValid() ? GI->GetSubsystem<UAsdaSessionResetSubsystem>() : nullptr; }

		// Одна проверка: строка в лог и счёт.
		void Expect(bool b, const FString& What)
		{
			if (b) { ++Ok; UE_LOG(LogAsdaWhisperReplyTest, Display, TEXT("%s ok   %s"), kPrefix, *What); }
			else   { ++Fail; UE_LOG(LogAsdaWhisperReplyTest, Error, TEXT("%s FAIL %s"), kPrefix, *What); }
		}
		// Шаг-действие (всегда завершается сразу).
		void Do(const TCHAR* Name, double Delay, TFunction<void()> Fn)
		{
			FStep St; St.Name = Name; St.Delay = Delay; St.Fn = [F = MoveTemp(Fn)]() { F(); return true; };
			Steps.Add(MoveTemp(St));
		}
	};

	// Клавиша команды 69 «CHAT ANSWER» в живой раскладке (по умолчанию R).
	static FKey AnswerKey(const FState& S)
	{
		const AAsdaPlayerController* P = Cast<AAsdaPlayerController>(S.PC());
		FAsdaKeyBind B;
		if (P && P->Keys && P->Keys->GetBind(69, B) && B.Vk != 0) { return UAsdaKeybindConfig::VkToFKey(B.Vk); }
		return EKeys::R;
	}
	// Нажать/отпустить клавишу штатным вводом контроллера.
	static void KeyEv(const FState& S, const FKey& K, bool bDown)
	{
		if (APlayerController* P = S.PC()) { P->InputKey(FInputKeyEventArgs::CreateSimulated(K, bDown ? IE_Pressed : IE_Released, bDown ? 1.f : 0.f)); }
	}
	// Входящий 5089 (эталон Recv_ChatWhisper): флаг @0, msgType @1 = 1, charId @2, имя cp1251 @8 (20 б), SoulState @28 = 0, текст @29.
	//   WORD @6 = userNum получателя: по умолчанию свой `SelfUserNum` (так пишет сервер), `ToUserNum` ≥ 0 — подменить (раздел U).
	static void Feed5089(const FState& S, const char* From, uint8 Flag, int32 CharId, int32 ToUserNum = -1)
	{
		TArray<uint8> B; B.SetNumZeroed(29 + 2);
		B[0] = Flag; B[1] = 1;
		AsdaBytes::WrI32(B.GetData() + 2, CharId);
		const UAsdaNetSubsystem* NS = S.Net();   // свой userNum — для WORD @6
		AsdaBytes::WrI16(B.GetData() + 6, ToUserNum >= 0 ? ToUserNum : (NS ? (int32)NS->SelfUserNum : 0));
		for (int32 i = 0; From[i] && i < 19; ++i) { B[8 + i] = (uint8)From[i]; }
		B[29] = 'h'; B[30] = 'i';
		if (UAsdaNetSubsystem* N = S.Net()) { N->DebugFeedPacket(5089, B); }
	}
	// Текст поля ввода и фокус — из дампа окна (`DebugInputState`: «фокус=N текст«…»»).
	static void FieldOf(const UAsdaChatWindowSubsystem* C, FString& OutText, bool& bOutFocus)
	{
		const FString D = C ? C->DebugInputState() : FString();
		bOutFocus = D.Contains(TEXT("фокус=1"));
		OutText.Reset();
		const int32 A = D.Find(TEXT("текст«"));
		if (A == INDEX_NONE) { return; }
		const int32 From = A + 6;
		const int32 E = D.Find(TEXT("»"), ESearchCase::CaseSensitive, ESearchDir::FromStart, From);
		if (E != INDEX_NONE) { OutText = D.Mid(From, E - From); }
	}
	// Действует ли подстановка «/w имя » (новый ключ; нет ключа — прежнее поведение, поле пустое).
	static bool PrefillOn()
	{
		const IConsoleVariable* V = IConsoleManager::Get().FindConsoleVariable(TEXT("asda.Chat.WhisperAnswerPrefill"));
		return V && V->GetInt() != 0;
	}

	// Вход своего персонажа в мир, как после 4003 (офлайн-спавн): без него окно чата не строится.
	static void Respawn(const FState& S)
	{
		if (UWorld* Wd = S.W()) { if (GEngine && AsdaSelfTestKit::NeedSelfSpawn(Wd, kSelfName)) { GEngine->Exec(Wd, *FString::Printf(TEXT("Asda.World.FakeSelfSpawn 256 256 %s"), kSelfName)); } }
	}

	// Добавить «нажать R» (зажать 0.15 с, отпустить, подождать 0.25 с) перед проверкой `Check`.
	static void AddPressR(const TSharedRef<FState>& S, const TCHAR* Tag, TFunction<void()> Check)
	{
		S->Do(*FString::Printf(TEXT("%s: R вниз"), Tag), 0.10, [S]()
		{
			if (UAsdaChatWindowSubsystem* C = S->Chat()) { S->Base6942 = C->DebugCountLogLines(S->L6942, 7); }
			KeyEv(*S, AnswerKey(*S), true);
		});
		S->Do(*FString::Printf(TEXT("%s: R вверх"), Tag), 0.15, [S]() { KeyEv(*S, AnswerKey(*S), false); });
		S->Do(*FString::Printf(TEXT("%s: проверка"), Tag), 0.25, MoveTemp(Check));
	}

	// Кадр окна с интерфейсом в Reports/batch15/r_notarget_frames/<имя>_<язык>.png (не в Saved: пакет удаляет свою песочницу Saved_<s>).
	static void Shot(const TCHAR* Name)
	{
		FString Lang = TEXT("ru");   // язык прогона из -AsdaLang=, для имени файла
		FParse::Value(FCommandLine::Get(), TEXT("AsdaLang="), Lang);
		const FString Dir = FPaths::ConvertRelativePathToFull(FPaths::Combine(FPaths::ProjectDir(), TEXT("../Reports/batch15/r_notarget_frames")));   // папка кадров пачки 15
		IFileManager::Get().MakeDirectory(*Dir, true);
		FScreenshotRequest::RequestScreenshot(FPaths::Combine(Dir, FString::Printf(TEXT("%s_%s.png"), Name, *Lang)), /*bShowUI=*/true, /*bAddFilenameSuffix=*/false);
	}

	// R без шептавшего при ключе asda.Chat.WhisperAnswerNoTargetSilent = 1: ввод закрыт, режим 4, строки 6942 нет, поле без «/w».
	static void ExpectSilentR(FState& S, const TCHAR* Tag)
	{
		UAsdaChatWindowSubsystem* C = S.Chat();
		if (!C) { S.Expect(false, FString::Printf(TEXT("%s: нет подсистемы чата"), Tag)); return; }
		const int32 N = C->DebugCountLogLines(S.L6942, 7);
		FString Txt; bool bFocus = false; FieldOf(C, Txt, bFocus);
		S.Expect(!C->IsInputActive() && C->GetInputState() == 4 && N == S.Base6942 && !Txt.Contains(TEXT("/w")),
			FString::Printf(TEXT("%s R → ввод открыт=%d (ждём 0), режим %d (ждём 4), «%s» +%d (ждём +0), поле «%s» (без /w)"),
				Tag, C->IsInputActive() ? 1 : 0, C->GetInputState(), *S.L6942, N - S.Base6942, *Txt));
	}

	// Полный сброс окна чата перед разделом: ввод закрыт, режим 4.
	static void CloseAll(const FState& S)
	{
		if (UAsdaChatWindowSubsystem* C = S.Chat()) { C->CloseInput(); C->SetInputState(4); C->DebugSetWhisperTarget(FString()); }
	}

	// Раздел S: шёпот от самого себя (charId тела = `SelectedCharId`) при ключе `asda.Chat.WhisperSelfBranch` = `Branch`.
	//   До раздела последний шептавший — Bob. Эталон (1): строка из loc 5431 с именем, «R» → Bob. Прежнее (0): «[Me]: …», «R» → Me.
	static void AddSelfWhisper(const TSharedRef<FState>& S, int32 Branch)
	{
		static constexpr int32 kSelfCharId = 777099;   // свой charId на время раздела (офлайн-стенд его не задаёт)
		const FString Tag = FString::Printf(TEXT("S%d"), Branch);   // метка раздела в строках
		// Свой charId прогона до раздела (возвращается после проверки).
		TSharedRef<int32> SavedId = MakeShared<int32>(0);
		S->Do(*FString::Printf(TEXT("%s: приём 5089 от себя"), *Tag), 0.05, [S, Branch, SavedId]()
		{
			S->Keys->Set(TEXT("asda.Chat.WhisperSelfBranch"), Branch);
			if (UAsdaNetSubsystem* N = S->Net()) { *SavedId = N->SelectedCharId; N->SelectedCharId = kSelfCharId; }
			Feed5089(*S, "Me", 0, kSelfCharId);
		});
		AddPressR(S, *Tag, [S, Branch, SavedId, Tag]()
		{
			UAsdaChatWindowSubsystem* C = S->Chat();
			FString Raw, Nick; FColor Col; int32 Slot = -1;
			const bool bLine = C->DebugLastLogLine(Raw, Col, Slot, Nick) && Raw.Contains(TEXT("hi"));
			// Начало строки по эталону: loc 5431 с подставленным именем (`sprintf(lineBuf, loc5431, имя)`).
			FString Head = AsdaNotice::Loc(S->GI.Get(), 5431);
			const int32 PctS = Head.Find(TEXT("%s"), ESearchCase::CaseSensitive);
			if (PctS != INDEX_NONE) { Head = Head.Left(PctS) + TEXT("Me") + Head.Mid(PctS + 2); }
			UE_LOG(LogAsdaWhisperReplyTest, Display, TEXT("%s %s: loc 5431 = «%s»; последняя строка ленты (слот %d) «%s»"),
				kPrefix, *Tag, *AsdaNotice::Loc(S->GI.Get(), 5431), Slot, *Raw);
			const FString WantTarget = Branch ? TEXT("Bob") : TEXT("Me");
			const bool bRightLine = Branch ? (!Head.IsEmpty() && Raw.StartsWith(Head) && !Raw.StartsWith(TEXT("[Me]: ")))
			                               : Raw.StartsWith(TEXT("[Me]: "));
			S->Expect(bLine && bRightLine,
				FString::Printf(TEXT("%s: шёпот от себя → строка «%s» (ждём %s)"), *Tag, *Raw,
					Branch ? TEXT("начало loc 5431 с именем, не «[Me]: »") : TEXT("«[Me]: …», прежнее")));
			S->Expect(C->GetInputState() == 7 && C->DebugWhisperTarget() == WantTarget,
				FString::Printf(TEXT("%s: R после шёпота от себя → режим %d, адресат «%s» (ждём 7, %s)"),
					*Tag, C->GetInputState(), *C->DebugWhisperTarget(), *WantTarget));
			if (UAsdaNetSubsystem* N = S->Net()) { N->SelectedCharId = *SavedId; }
			S->Keys->Set(TEXT("asda.Chat.WhisperSelfBranch"), 1);
			CloseAll(*S);
		});
	}

	// Раздел U: 5089 с WORD +6 ≠ свой `SelfUserNum` при ключе `asda.Chat.WhisperUserNumCheck` = `Check`. До раздела последний
	//   шептавший — Bob. Эталон (1): ни строки (входящий Dan и эхо Eve), «R» → Bob. Прежнее (0): строки есть, «R» → Dan.
	static void AddForeignUserNum(const TSharedRef<FState>& S, int32 Check)
	{
		const FString Tag = FString::Printf(TEXT("U%d"), Check);   // метка раздела в строках
		TSharedRef<TPair<uint16, bool>> Saved = MakeShared<TPair<uint16, bool>>(0, false);   // свой userNum и признак до раздела
		TSharedRef<TPair<int32, int32>> Base = MakeShared<TPair<int32, int32>>(0, 0);        // строк Dan / Eve в ленте до приёма
		S->Do(*FString::Printf(TEXT("%s: приём 5089 на чужой userNum"), *Tag), 0.05, [S, Check, Saved, Base]()
		{
			S->Keys->Set(TEXT("asda.Chat.WhisperUserNumCheck"), Check);
			UAsdaNetSubsystem* N = S->Net();
			if (N)
			{
				*Saved = TPair<uint16, bool>(N->SelfUserNum, N->bSelfUserNumKnown);
				if (!N->bSelfUserNumKnown || N->SelfUserNum == 0) { N->SetSelfUserNumForDebug(4321); }   // свой номер известен, как после 4003
			}
			const int32 Mine = N ? (int32)N->SelfUserNum : 0;
			const int32 Foreign = (Mine + 1) & 0x7FFF;   // заведомо чужой userNum
			if (UAsdaChatWindowSubsystem* C = S->Chat()) { *Base = TPair<int32, int32>(C->DebugCountLogLines(TEXT("Dan")), C->DebugCountLogLines(TEXT("Eve"))); }
			Feed5089(*S, "Dan", 0, 777004, Foreign);   // входящий от Dan, адресован не мне
			Feed5089(*S, "Eve", 1, 777005, Foreign);   // эхо «своего» шёпота Eve, адресовано не мне
		});
		AddPressR(S, *Tag, [S, Check, Saved, Base, Tag]()
		{
			UAsdaChatWindowSubsystem* C = S->Chat();
			const int32 DDan = C->DebugCountLogLines(TEXT("Dan")) - Base->Key;
			const int32 DEve = C->DebugCountLogLines(TEXT("Eve")) - Base->Value;
			const FString WantTarget = Check ? TEXT("Bob") : TEXT("Dan");
			const int32 WantLines = Check ? 0 : 1;
			S->Expect(DDan == WantLines && DEve == WantLines,
				FString::Printf(TEXT("%s: 5089 на чужой userNum → строк Dan +%d, Eve +%d (ждём +%d: %s)"), *Tag, DDan, DEve, WantLines,
					Check ? TEXT("эталон отбрасывает") : TEXT("прежнее — показывает")));
			S->Expect(C->GetInputState() == 7 && C->DebugWhisperTarget() == WantTarget,
				FString::Printf(TEXT("%s: R → режим %d, адресат «%s» (ждём 7, %s)"), *Tag, C->GetInputState(), *C->DebugWhisperTarget(), *WantTarget));
			if (UAsdaNetSubsystem* N = S->Net()) { N->SelfUserNum = Saved->Key; N->bSelfUserNumKnown = Saved->Value; }
			S->Keys->Set(TEXT("asda.Chat.WhisperUserNumCheck"), 1);
			CloseAll(*S);
		});
	}

	// Раздел F: чат с Другом (соулмейт) — 5089 flag 1. Эхо СВОЕГО сообщения (charId = свой, имя = партнёр «Pal») и входящее
	//   от партнёра (charId чужой, имя «Pal»). Ключи `asda.Chat.SoulmateToFormat` = `ToFmt`, `asda.Chat.SoulmateSelfName` = `SelfNm`:
	//   1/x — своё «loc 5431(Pal): hi»; 0/1 — «[своё имя]: hi» (эталон); 0/0 — прежнее «[Pal]: hi». Входящее всегда «[Pal]: hi».
	static void AddSoulEcho(const TSharedRef<FState>& S, int32 ToFmt, int32 SelfNm)
	{
		static constexpr int32 kSelfCharId = 777099;   // свой charId на время раздела
		const FString Tag = FString::Printf(TEXT("F%d%d"), ToFmt, SelfNm);   // метка раздела
		TSharedRef<int32> SavedId = MakeShared<int32>(0);   // свой charId до раздела
		S->Do(*FString::Printf(TEXT("%s: эхо своего сообщения Другу"), *Tag), 0.05, [S, ToFmt, SelfNm, SavedId, Tag]()
		{
			S->Keys->Set(TEXT("asda.Chat.SoulmateToFormat"), ToFmt);
			S->Keys->Set(TEXT("asda.Chat.SoulmateSelfName"), SelfNm);
			UAsdaNetSubsystem* N = S->Net();
			if (N) { *SavedId = N->SelectedCharId; N->SelectedCharId = kSelfCharId; }
			Feed5089(*S, "Pal", 1, kSelfCharId);
			UAsdaChatWindowSubsystem* C = S->Chat();
			const UAsdaWorldSubsystem* WS = S->GI.IsValid() ? S->GI->GetSubsystem<UAsdaWorldSubsystem>() : nullptr;
			const FString Me = WS ? WS->GetSelfCharName() : FString();   // своё имя (общая точка)
			FString Raw, Nick; FColor Col; int32 Slot = -1;
			C->DebugLastLogLine(Raw, Col, Slot, Nick);
			FString Want;
			if (ToFmt)
			{
				FString Head = AsdaNotice::Loc(S->GI.Get(), 5431);
				const int32 PctS = Head.Find(TEXT("%s"), ESearchCase::CaseSensitive);
				if (PctS != INDEX_NONE) { Head = Head.Left(PctS) + TEXT("Pal") + Head.Mid(PctS + 2); }
				Want = Head + TEXT(": hi");
			}
			else { Want = FString::Printf(TEXT("[%s]: hi"), SelfNm ? *Me : TEXT("Pal")); }
			S->Expect(!Me.IsEmpty() && Raw == Want && Slot == 8,
				FString::Printf(TEXT("%s: своё сообщение Другу → «%s», слот %d (ждём «%s», слот 8; своё имя «%s»)"), *Tag, *Raw, Slot, *Want, *Me));
			// Входящее от партнёра: ключ формата 1 — «От Pal: hi» / «From [Pal]: hi» (loc 991200), 0 — «[Pal]: hi».
			Feed5089(*S, "Pal", 1, 777010);
			C->DebugLastLogLine(Raw, Col, Slot, Nick);
			FString WantIn = TEXT("[Pal]: hi");
			if (ToFmt)
			{
				FString Head = AsdaNotice::Loc(S->GI.Get(), 991200);
				const int32 PctS = Head.Find(TEXT("%s"), ESearchCase::CaseSensitive);
				if (PctS != INDEX_NONE) { Head = Head.Left(PctS) + TEXT("Pal") + Head.Mid(PctS + 2); }
				WantIn = Head + TEXT(": hi");
			}
			S->Expect(Raw == WantIn && Slot == 8 && (!ToFmt || AsdaNotice::Loc(S->GI.Get(), 991200).Contains(TEXT("%s"))),
				FString::Printf(TEXT("%s: сообщение от Друга → «%s», слот %d (ждём «%s», слот 8; loc 991200 = «%s»)"), *Tag, *Raw, Slot, *WantIn,
					*AsdaNotice::Loc(S->GI.Get(), 991200)));
			if (N) { N->SelectedCharId = *SavedId; }
			S->Keys->Set(TEXT("asda.Chat.SoulmateToFormat"), 1);
			S->Keys->Set(TEXT("asda.Chat.SoulmateSelfName"), 1);
		});
	}

	static void Build(const TSharedRef<FState>& S)
	{
		// ── Подготовка: ключи, перехват, чистое состояние (как после входа: шептавшего нет). ──
		S->Do(TEXT("подготовка"), 0.0, [S]()
		{
			S->Keys = MakeUnique<FAsdaCVarScope>(S->W(), kCmd);
			S->Keys->Set(TEXT("asda.Chat.WhisperAnswer"), 1);
			S->L6942 = AsdaNotice::Loc(S->GI.Get(), 6942);
			S->CapGen = AsdaSelfTestKit::ArmCapture(S->Net(), [S](uint16 O, const uint8* P, int32 Len, bool, TCHAR)
			{
				TArray<uint8> B; if (P && Len > 0) { B.Append(P, Len); }
				S->Outs.Add(TPair<uint16, TArray<uint8>>(O, MoveTemp(B)));
			}, 0, kCmd);
			S->Expect(S->CapGen != 0, TEXT("0: перехват исходящих взведён"));
			// Чистое состояние «после входа»: шептавшего нет. Сброс `LeftWorld` здесь НЕ шлём — он снимает своего персонажа, а без
			//   него окно чата не строится (тик чата ждёт `LocalPlayerActor`), и строка ввода теряла бы поле.
			if (UAsdaChatWindowSubsystem* C = S->Chat()) { C->NoteWhisperFrom(FString()); }
			CloseAll(*S);
			UE_LOG(LogAsdaWhisperReplyTest, Display, TEXT("%s клавиша команды 69 «CHAT ANSWER» = %s; подстановка «/w имя » = %d"),
				kPrefix, *AnswerKey(*S).ToString(), PrefillOn() ? 1 : 0);
		});

		// ── N0: шёпота не было, ключ asda.Chat.WhisperAnswerNoTargetSilent = 0 — как оригинал, строка 6942 (дефект воспроизведён). ──
		S->Do(TEXT("N0: ключ 0"), 0.05, [S]() { S->Keys->Set(TEXT("asda.Chat.WhisperAnswerNoTargetSilent"), 0); });
		AddPressR(S, TEXT("N0"), [S]()
		{
			UAsdaChatWindowSubsystem* C = S->Chat();
			const int32 N = C->DebugCountLogLines(S->L6942, 7);
			S->Expect(!C->IsInputActive() && C->GetInputState() == 4 && N == S->Base6942 + 1,
				FString::Printf(TEXT("N0: ключ 0, без шёпота R → ввод открыт=%d (ждём 0), режим %d (ждём 4), «%s» в ленте слот 7 +%d (ждём +1, дефект)"),
					C->IsInputActive() ? 1 : 0, C->GetInputState(), *S->L6942, N - S->Base6942));
		});

		S->Do(TEXT("N0: кадр"), 0.05, [S]() { Shot(TEXT("whisper_r_notarget_key0")); });

		// ── N: шёпота не было, ключ 1 — R молчит: ни строки, ни ввода, поле не заполнено. ──
		S->Do(TEXT("N: ключ 1"), 0.05, [S]() { S->Keys->Set(TEXT("asda.Chat.WhisperAnswerNoTargetSilent"), 1); });
		AddPressR(S, TEXT("N"), [S]() { ExpectSilentR(*S, TEXT("N: без шёпота")); });
		S->Do(TEXT("N: кадр"), 0.05, [S]() { Shot(TEXT("whisper_r_notarget_key1")); });

		// ── NM: смена карты без шёпота — R по-прежнему молчит. ──
		S->Do(TEXT("NM: смена карты"), 0.05, [S]() { if (UAsdaSessionResetSubsystem* R = S->Reset()) { R->Broadcast(EAsdaSessionResetReason::MapChanged); } CloseAll(*S); });
		S->Do(TEXT("NM: вход на карту (4003)"), 0.05, [S]() { Respawn(*S); });
		AddPressR(S, TEXT("NM"), [S]() { ExpectSilentR(*S, TEXT("NM: после смены карты без шёпота")); });

		// ── A: входящий от Alice → R. ──
		S->Do(TEXT("A: приём 5089 от Alice"), 0.05, [S]() { Feed5089(*S, "Alice", 0, 777001); });
		AddPressR(S, TEXT("A"), [S]()
		{
			UAsdaChatWindowSubsystem* C = S->Chat();
			FString Txt; bool bFocus = false; FieldOf(C, Txt, bFocus);
			S->Expect(C->IsInputActive() && C->GetInputState() == 7 && C->DebugWhisperTarget() == TEXT("Alice"),
				FString::Printf(TEXT("A1: R после шёпота Alice → ввод открыт=%d, режим %d (ждём 7), адресат «%s» (ждём Alice)"),
					C->IsInputActive() ? 1 : 0, C->GetInputState(), *C->DebugWhisperTarget()));
			const FString Want = PrefillOn() ? FString(TEXT("/w Alice ")) : FString();
			S->Expect(Txt == Want, FString::Printf(TEXT("A2: поле ввода «%s» (ждём «%s»); фокус в поле=%d"), *Txt, *Want, bFocus ? 1 : 0));
			// Enter с текстом: тем же путём, что поле 2609. С подстановкой игрок дописывает текст после «/w Alice ».
			S->Outs.Reset();
			C->DebugSubmit(Txt + TEXT("yo"));
			int32 N5088 = 0; FString Hex;
			for (const TPair<uint16, TArray<uint8>>& O : S->Outs)
			{
				if (O.Key != 5088) { continue; }
				++N5088;
				Hex.Reset();
				for (const uint8 Bt : O.Value) { Hex += FString::Printf(TEXT("%02X "), Bt); }
			}
			const TPair<uint16, TArray<uint8>>* Last = S->Outs.FindByPredicate([](const TPair<uint16, TArray<uint8>>& O) { return O.Key == 5088; });
			const bool bBody = Last && Last->Value.Num() == 23 && Last->Value[0] == 0 && FMemory::Memcmp(Last->Value.GetData() + 1, "Alice", 5) == 0
				&& Last->Value[6] == 0 && Last->Value[21] == 'y' && Last->Value[22] == 'o';
			S->Expect(N5088 == 1 && bBody, FString::Printf(TEXT("A3: Enter «%syo» → 5088 ×%d тело [%s] (ждём ×1: вид 0, «Alice», «yo»)"), *Txt, N5088, *Hex.TrimEnd()));
			CloseAll(*S);
		});

		// ── B: два шептавших подряд → R отвечает последнему. ──
		S->Do(TEXT("B: Alice, затем Bob"), 0.05, [S]() { Feed5089(*S, "Alice", 0, 777001); Feed5089(*S, "Bob", 0, 777002); });
		AddPressR(S, TEXT("B"), [S]()
		{
			UAsdaChatWindowSubsystem* C = S->Chat();
			S->Expect(C->GetInputState() == 7 && C->DebugWhisperTarget() == TEXT("Bob"),
				FString::Printf(TEXT("B: после Alice и Bob R → режим %d, адресат «%s» (ждём 7, Bob — последний; перебора нет, как буфер chat+2552)"),
					C->GetInputState(), *C->DebugWhisperTarget()));
			CloseAll(*S);
		});

		// ── E: эхо своего шёпота не меняет последнего шептавшего. ──
		S->Do(TEXT("E: эхо своего шёпота Carl"), 0.05, [S]() { Feed5089(*S, "Carl", 1, 777003); });
		AddPressR(S, TEXT("E"), [S]()
		{
			UAsdaChatWindowSubsystem* C = S->Chat();
			S->Expect(C->DebugWhisperTarget() == TEXT("Bob"),
				FString::Printf(TEXT("E: эхо своего шёпота (flag 1) «Carl» → R адресат «%s» (ждём Bob)"), *C->DebugWhisperTarget()));
			CloseAll(*S);
		});

		// ── S: входящий 5089, где charId тела = СВОЙ (`SelectedCharId`) → эталонная ветка loc 5431, адресат «R» не меняется. ──
		//   S1 — ключ `asda.Chat.WhisperSelfBranch` 1 (эталон): строка из loc 5431, «R» → Bob. S0 — ключ 0 (прежнее): «[Me]: …», «R» → Me.
		AddSelfWhisper(S, 1);
		AddSelfWhisper(S, 0);
		S->Do(TEXT("S: вернуть Bob последним шептавшим"), 0.05, [S]() { Feed5089(*S, "Bob", 0, 777002); });

		// ── U: WORD тела +6 = userNum получателя ≠ свой → шёпот не мне: эталон отбрасывает (flag 0 — молча; flag 1 — у эталона
		//   пустая строка, у нас [ИСПРАВЛЕНИЕ] — тоже отброс). U1 — ключ `asda.Chat.WhisperUserNumCheck` 1; U0 — ключ 0, прежнее. ──
		AddForeignUserNum(S, 1);
		AddForeignUserNum(S, 0);
		S->Do(TEXT("U: вернуть Bob последним шептавшим"), 0.05, [S]() { Feed5089(*S, "Bob", 0, 777002); });


		// ── T: строка ввода уже открыта (набор текста) → R — буква, а не команда. ──
		S->Do(TEXT("T: открыть строку (Enter)"), 0.05, [S]() { if (UAsdaChatWindowSubsystem* C = S->Chat()) { C->OpenInput(4); } });
		AddPressR(S, TEXT("T"), [S]()
		{
			UAsdaChatWindowSubsystem* C = S->Chat();
			S->Expect(C->GetInputState() == 4 && C->DebugWhisperTarget().IsEmpty(),
				FString::Printf(TEXT("T: R во время набора → режим %d (ждём 4), адресат «%s» (ждём пусто) — команда 69 не исполнена"),
					C->GetInputState(), *C->DebugWhisperTarget()));
			CloseAll(*S);
		});

		// ── K: ключ 0 — прежнее поведение порта. ──
		S->Do(TEXT("K: ключ 0, приём Alice"), 0.05, [S]()
		{
			// Как сессия с ключом 0 с самого входа: прошлых шептавших нет (иначе остался бы Bob из раздела M).
			if (UAsdaChatWindowSubsystem* C = S->Chat()) { C->NoteWhisperFrom(FString()); }
			S->Keys->Set(TEXT("asda.Chat.WhisperAnswer"), 0);
			Feed5089(*S, "Alice", 0, 777001);
		});
		AddPressR(S, TEXT("K"), [S]()
		{
			UAsdaChatWindowSubsystem* C = S->Chat();
			S->Expect(C->IsInputActive() && C->GetInputState() == 4 && C->DebugWhisperTarget().IsEmpty(),
				FString::Printf(TEXT("K: ключ 0 → R открывает ввод %d в текущем режиме %d, адресат «%s» (ждём 1, 4, пусто)"),
					C->IsInputActive() ? 1 : 0, C->GetInputState(), *C->DebugWhisperTarget()));
			CloseAll(*S);
			S->Keys->Set(TEXT("asda.Chat.WhisperAnswer"), 1);
		});

		// ── M: смена карты буфер не трогает. ──
		// ── P: ключ `asda.Chat.WhisperAnswerPrefill 0` — прежнее: режим 7 адресату, поле пустое. ──
		S->Do(TEXT("P: ключ подстановки 0, приём Alice"), 0.05, [S]()
		{
			S->Keys->Set(TEXT("asda.Chat.WhisperAnswerPrefill"), 0);
			Feed5089(*S, "Alice", 0, 777001);
		});
		AddPressR(S, TEXT("P"), [S]()
		{
			UAsdaChatWindowSubsystem* C = S->Chat();
			FString Txt; bool bFocus = false; FieldOf(C, Txt, bFocus);
			S->Expect(C->IsInputActive() && C->GetInputState() == 7 && C->DebugWhisperTarget() == TEXT("Alice") && Txt.IsEmpty(),
				FString::Printf(TEXT("P: подстановка 0 → ввод %d, режим %d, адресат «%s», поле «%s» (ждём 1, 7, Alice, пусто — прежнее)"),
					C->IsInputActive() ? 1 : 0, C->GetInputState(), *C->DebugWhisperTarget(), *Txt));
			CloseAll(*S);
			S->Keys->Set(TEXT("asda.Chat.WhisperAnswerPrefill"), 1);
		});

		S->Do(TEXT("M: шёпот Bob"), 0.05, [S]() { Feed5089(*S, "Bob", 0, 777002); });
		S->Do(TEXT("M: смена карты"), 0.05, [S]() { if (UAsdaSessionResetSubsystem* R = S->Reset()) { R->Broadcast(EAsdaSessionResetReason::MapChanged); } CloseAll(*S); });
		S->Do(TEXT("M: вход на карту (4003)"), 0.05, [S]() { Respawn(*S); });
		AddPressR(S, TEXT("M"), [S]()
		{
			UAsdaChatWindowSubsystem* C = S->Chat();
			// Проверяем буфер шептавшего (эталон: смена карты chat+2552 не трогает). Флаг набора здесь НЕ сверяем: синтетический
			//   `MapChanged` без настоящей загрузки уровня пересобирает окно чата так, что поле теряет фокус у ЛЮБОГО пути открытия
			//   (контроль Enter ниже) — это свойство стенда, не клавиши R; на живой смене карты окно строится заново с уровнем.
			FString Txt; bool bFocus = false; FieldOf(C, Txt, bFocus);
			const FString Want = PrefillOn() ? FString(TEXT("/w Bob ")) : FString();
			S->Expect(C->GetInputState() == 7 && C->DebugWhisperTarget() == TEXT("Bob") && Txt == Want,
				FString::Printf(TEXT("M: после смены карты R → режим %d, адресат «%s», поле «%s» (ждём 7, Bob, «%s»)"),
					C->GetInputState(), *C->DebugWhisperTarget(), *Txt, *Want));
			CloseAll(*S);
			C->OpenInput(4);   // контроль: Enter (команда 66) на том же стенде
		});
		S->Do(TEXT("M: контроль Enter"), 0.25, [S]()
		{
			UAsdaChatWindowSubsystem* C = S->Chat();
			UE_LOG(LogAsdaWhisperReplyTest, Display, TEXT("%s НАБЛЮДЕНИЕ M: после синтетической смены карты строка, открытая Enter, держит фокус=%d (0 — свойство стенда)"),
				kPrefix, C && C->IsInputActive() ? 1 : 0);
			CloseAll(*S);
		});

		// ── L: выход на логин чистит. ──
		// ── F: чат с Другом — своё эхо подписано «кому» (исправление) / своим именем (эталон) / как раньше; входящее — именем партнёра.
		//   Стоит ПОСЛЕ M: вход на карту (Respawn) даёт своему персонажу имя `kSelfName` — офлайн-стенд иначе без имени. ──
		AddSoulEcho(S, 1, 1);
		AddSoulEcho(S, 0, 1);
		AddSoulEcho(S, 0, 0);
		S->Do(TEXT("L: выход на логин"), 0.05, [S]() { if (UAsdaSessionResetSubsystem* R = S->Reset()) { R->Broadcast(EAsdaSessionResetReason::LeftWorld); } CloseAll(*S); });
		AddPressR(S, TEXT("L"), [S]()
		{
			UAsdaChatWindowSubsystem* C = S->Chat();
			ExpectSilentR(*S, TEXT("L: после выхода на логин"));
			CloseAll(*S);
		});
	}

	static void Finish(const TSharedRef<FState>& S)
	{
		if (S->CapGen != 0) { AsdaSelfTestKit::DisarmCapture(S->Net(), S->CapGen); S->CapGen = 0; }
		CloseAll(*S);
		Respawn(*S);       // раздел L снял своего персонажа — вернуть, чтобы следующий самотест пакета шёл в мире
		S->Keys.Reset();   // ключи вернуть ДО строки итога
		AsdaSelfTestKit::Finish(S->W(), kCmd, S->bQuit, LogAsdaWhisperReplyTest, kPrefix, S->Ok, S->Fail);
	}

	static bool Tick(TSharedRef<FState> S)
	{
		if (!S->GI.IsValid()) { return false; }
		if (S->Next >= S->Steps.Num()) { Finish(S); return false; }
		FStep& St = S->Steps[S->Next];
		if (FPlatformTime::Seconds() - S->StepT < St.Delay) { return true; }
		if (St.Fn()) { ++S->Next; S->StepT = FPlatformTime::Seconds(); }
		return true;
	}
}

static FAutoConsoleCommandWithWorldAndArgs GAsdaSelfTestWhisperReply(
	TEXT("asda.SelfTest.WhisperReply"),
	TEXT("[ОРИГИНАЛ] Самотест клавиши R «ответ последнему шептавшему» путём игрока: приём 5089, ввод контроллера, 5088 под перехватом, ")
	TEXT("смена карты, выход на логин, ключи asda.Chat.WhisperAnswer / asda.Chat.WhisperAnswerPrefill. Офлайн. [delay=N] [quit]"),
	FConsoleCommandWithWorldAndArgsDelegate::CreateLambda([](const TArray<FString>& Args, UWorld* W)
	{
		using namespace AsdaWhisperReplyTest;
		const FAsdaSelfTestArgs KA = FAsdaSelfTestArgs::Parse(Args);
		static const uint16 kGuardOps[] = { 5088 };
		if (AsdaSelfTestKit::RefuseStart(W, kCmd, KA, 0.f, MakeArrayView(kGuardOps))) { return; }
		UGameInstance* GI = W ? W->GetGameInstance() : nullptr;
		if (!GI) { AsdaSelfTestKit::Refused(W, kCmd, KA.bQuit, TEXT("нет GameInstance")); return; }
		if (AsdaSelfTestKit::NeedSelfSpawn(W, kSelfName) && GEngine)
		{
			GEngine->Exec(W, TEXT("Asda.Load.CurrentMap 3"));
			GEngine->Exec(W, *FString::Printf(TEXT("Asda.World.FakeSelfSpawn 256 256 %s"), kSelfName));
		}
		TSharedRef<FState> S = MakeShared<FState>();
		S->GI = GI;
		S->bQuit = KA.bQuit;
		Build(S);
		S->StepT = FPlatformTime::Seconds() + KA.Delay + 1.0;
		FTSTicker::GetCoreTicker().AddTicker(FTickerDelegate::CreateLambda([S](float) -> bool { return Tick(S); }), 0.0f);
		UE_LOG(LogAsdaWhisperReplyTest, Display, TEXT("%s запуск: шагов %d"), kPrefix, S->Steps.Num());
	}));

#endif // ASDA_DEV_CONSOLE && !UE_BUILD_SHIPPING
