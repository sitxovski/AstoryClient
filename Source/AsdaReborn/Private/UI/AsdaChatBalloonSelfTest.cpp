// Copyright (c) 2026 @sitxovski (Discord)
// ★★★ САМОТЕСТ ОБЛАЧКОВ ЧАТА НАД ГОЛОВОЙ (26.10.2026, `asda.Chat.BalloonSelfTest`). [ОРИГИНАЛ]
//   Основание: баг игроков «party chat bubbles fail to display above your own character» и жалоба владельца «в обычных
//   пузырях первая строка пустая, текст начинается ниже». Разбор эталона — шапка `Public/UI/AsdaChatBalloonSubsystem.h`.
//
// ★ ПУТЬ ИГРОКА: каждое сообщение приходит СИНТЕТИЧЕСКИМ пакетом через штатный `HandlePacket` (`DebugFeedPacket`) —
//   разбор опкода → `OnChatLine` → очередь подсистемы облачков → её тик → поиск персонажа → виджет. Свой персонаж — штатный
//   павн (офлайн-спавн той же точкой, что 4003), сосед — штатный путь снапшота 4005 (`DebugSpawnFakePeer`).
//   Ожидания (вариант подложки, цвет ТЕКСТА) — константы эталона, явно (network.c: 5085 −16777216, 5087 CChat+623/624,
//   5089 −14540254 вариант 1, 4121 −16748079, 5106 −15829504, 6719 канал 9 = 0xFFA03C00), а не чтение боевого кода.
//
//   R0  повтор бага: ключ asda.Chat.BalloonSelfByName 0 — 5106 от СВОЕГО имени облачка над собой НЕ даёт (прежний порт);
//       ключ asda.Chat.BalloonVCenterBlock 0 — многострочный текст уезжает вниз (верхний зазор больше нижнего на ≥ полстроки).
//   Cn  по каждому каналу (общий 5085, крик 5087 охват 0 и 1, шёпот 5089 флаг 0, Друг 5089 флаг 1, клан 4121, отряд 5106,
//       армия 6719): облачко над СОБОЙ и над СОСЕДОМ; вариант подложки и цвет текста как в эталоне; текст = сообщение без
//       имени; первая строка не пустая; блок строк центрован (зазоры сверху/снизу равны ±2 px) и помещается в окно.
//   L   длинный текст (RU и EN): обрезка по 580 px с «...» (`textTruncateWithEllipsis`), перенос, окно растёт по строкам.
//   Кадры — `Reports/chat_bubbles/bubble_<NN>_<канал>_<язык>.png` (аргумент `noshot` — без кадров).
//
// Консоль: `asda.Chat.BalloonSelfTest [delay=N] [quit] [noshot]`. Только офлайн (каркас `AsdaSelfTestKit`). Строки —
//   «САМОТЕСТ-ОБЛАЧКО», итог — «САМОТЕСТ-ОБЛАЧКО ИТОГ: ok=… fail=…».
#include "CoreMinimal.h"

#if !UE_BUILD_SHIPPING

#include "AsdaNetSubsystem.h"
#include "AsdaBytes.h"                       // WrI16 / WrI32 — тела пакетов
#include "Test/AsdaSelfTestKit.h"            // каркас: отказ, свой персонаж, ключи, итог, выход
#include "Login/AsdaLoginData.h"             // EncodeCp1251 (имя) / EncodeWireText (текст) — как на проводе
#include "UI/AsdaChatWindowSubsystem.h"      // ранг в окне чата (DebugCountLogLines)
#include "UI/AsdaChatBalloonSubsystem.h"     // проверяемая подсистема (DebugGetBalloon, TruncateLikeOriginal)
#include "World/AsdaWorldSubsystem.h"        // свой павн, сосед, своё имя
#include "World/AsdaCoord.h"                 // тайл своего павна → позиция соседа
#include "Text/AsdaLang.h"                   // язык сессии — в имени кадра
#include "HighResScreenshot.h"               // кадр (FScreenshotRequest)
#include "Engine/Engine.h"
#include "Engine/GameInstance.h"
#include "Engine/World.h"
#include "HAL/IConsoleManager.h"
#include "Misc/Paths.h"
#include "TimerManager.h"

DEFINE_LOG_CATEGORY_STATIC(LogAsdaBalloonTest, Log, All);   // канал строк самотеста

namespace AsdaBalloonTest
{
	static const TCHAR* kCmd = TEXT("asda.Chat.BalloonSelfTest");   // имя команды (каркас, ключи, итог)
	static constexpr int32 kMeUser = 4242;                         // свой userNum на время прогона
	static constexpr int32 kMeChar = 777001;                       // свой charId на время прогона
	static constexpr int32 kPeerUser = 4301;                       // userNum соседа (charId = 900000 + userNum)
	static constexpr int32 kPeerChar = 900000 + kPeerUser;         // charId соседа (`DebugSpawnFakePeerState`)
	static const TCHAR* kSelfName = TEXT("Тестер");                // своё имя (5106/6719 адресуют по имени)
	static const TCHAR* kPeerName = TEXT("Соседка");               // имя соседа
	static constexpr float kStepSec = 1.2f;                        // шаг между каналами (очередь + кадр)
	static constexpr float kCheckSec = 0.5f;                       // от подачи пакета до проверки (тики очереди)

	// Один случай: опкод + вариант подложки + цвет текста эталона + тексты своего и соседского сообщения.
	struct FCase
	{
		FString Tag;            // метка канала (в лог и имя кадра)
		int32 Op = 0;           // опкод приёма
		uint8 Sub = 0;          // охват 5087 / флаг 5089 / онлайн 6719
		EAsdaTalkBox Kind = EAsdaTalkBox::White;   // ожидаемый вариант (эталон)
		FColor Color;           // ожидаемый цвет текста (dword эталона)
		FString SelfText;       // своё сообщение
		FString PeerText;       // сообщение соседа
		bool bLong = false;     // длинный текст — ждём обрезку «...»
		int32 Rank = 0;         // ранг отправителя в пакете (5085/5087); 0 — без ранга
		bool bRankInBalloon = false;   // ключ `asda.Chat.BalloonRankHide` 0 — ранг в облачке, как оригинал
	};

	// Состояние прогона.
	struct FRun
	{
		int32 Ok = 0, Fail = 0;                    // пройдено / провалено
		bool bQuit = false;                        // выйти после прогона
		bool bShot = true;                         // снимать кадры
		int32 RankLinesBase = 0;                   // строк с рангом в ленте до подачи случая
		FString ShotDir;                           // папка кадров
		FString Lang;                              // ru / en
		TWeakObjectPtr<UWorld> W;                  // мир прогона
		TUniquePtr<FAsdaNetSelfScope> Self;        // свой номер/charId — вернуть
		TUniquePtr<FAsdaCVarScope> Keys;           // ключи — вернуть
		TArray<FCase> Cases;                       // каналы
		int32 ShotNo = 0;                          // номер кадра
		float OneLineH = 0.f;                      // высота ОДНОСТРОЧНОГО блока TextBlock (первый однострочный случай C1) — эталон «без лишней строки»

		// Одна проверка: строка в лог и счёт.
		void Expect(bool b, const FString& What)
		{
			if (b) { ++Ok; UE_LOG(LogAsdaBalloonTest, Log, TEXT("САМОТЕСТ-ОБЛАЧКО [OK]   %s"), *What); }
			else   { ++Fail; UE_LOG(LogAsdaBalloonTest, Error, TEXT("САМОТЕСТ-ОБЛАЧКО [FAIL] %s"), *What); }
		}
		// Кадр с интерфейсом (облачка — во вьюпорте).
		void Shot(const FString& Name)
		{
			if (!bShot) { return; }
			const FString Path = FPaths::Combine(ShotDir, FString::Printf(TEXT("bubble_%02d_%s_%s.png"), ++ShotNo, *Name, *Lang));
			UE_LOG(LogAsdaBalloonTest, Log, TEXT("САМОТЕСТ-ОБЛАЧКО КАДР → %s"), *Path);
			FScreenshotRequest::RequestScreenshot(Path, /*bShowUI=*/true, /*bAddFilenameSuffix=*/false);
		}
	};

	// Подсистемы прогона.
	struct FSubs
	{
		UAsdaNetSubsystem* Net = nullptr;            // сеть (приём)
		UAsdaWorldSubsystem* WS = nullptr;           // мир (павн, сосед)
		UAsdaChatBalloonSubsystem* Bal = nullptr;    // облачка
		bool Valid() const { return Net && WS && Bal; }
	};
	static FSubs Get(UWorld* W)
	{
		FSubs S;
		UGameInstance* GI = W ? W->GetGameInstance() : nullptr;
		if (!GI) { return S; }
		S.Net = GI->GetSubsystem<UAsdaNetSubsystem>();
		S.WS = GI->GetSubsystem<UAsdaWorldSubsystem>();
		S.Bal = W->GetSubsystem<UAsdaChatBalloonSubsystem>();
		return S;
	}

	// Свой павн (публичное поле подсистемы мира; nullptr — нет живого).
	static AActor* SelfOf(const UAsdaWorldSubsystem* WS)
	{
		AActor* A = WS ? WS->LocalPlayerActor.Get() : nullptr;
		return ::IsValid(A) ? A : nullptr;
	}

	// Байты имени (cp1251, 20 б) и текста (провод) — как кладёт сервер.
	static void PutName(TArray<uint8>& B, int32 Off, const FString& Name)
	{
		uint8 Nm[20] = {};
		FAsdaLoginData::EncodeCp1251(Name, Nm, 19);
		FMemory::Memcpy(B.GetData() + Off, Nm, 20);
	}
	static void PutText(TArray<uint8>& B, const FString& Text)
	{
		TArray<uint8> T; T.SetNumZeroed(400);
		const int32 N = FAsdaLoginData::EncodeWireText(Text, T.GetData(), 400);
		T.SetNum(N);
		B.Append(T); B.Add(0);
	}

	// Тело пакета канала от персонажа (userNum, charId, имя). Раскладки — эталонные обработчики (network.c).
	static TArray<uint8> Body(const FCase& C, int32 UserNum, int32 CharId, const FString& Name, const FString& Text)
	{
		TArray<uint8> B;
		switch (C.Op)
		{
		case 5085:   // WORD userIdx@0, char[20] имя@2, DWORD rank@22, текст@26
			B.SetNumZeroed(26); AsdaBytes::WrI16(&B[0], (int16)UserNum); PutName(B, 2, Name); AsdaBytes::WrI32(&B[22], C.Rank); break;
		case 5087:   // WORD userIdx@0, BYTE охват@2, char[20] имя@3, DWORD rank@23, текст@27
			B.SetNumZeroed(27); AsdaBytes::WrI16(&B[0], (int16)UserNum); B[2] = C.Sub; PutName(B, 3, Name); AsdaBytes::WrI32(&B[23], C.Rank); break;
		case 5089:   // BYTE флаг@0, BYTE тип@1 (1 = сообщение), DWORD charId@2, WORD@6, char[20] имя@8, BYTE душа@28, текст@29
			// ★ 05.11.2026: WORD@6 = userNum ПОЛУЧАТЕЛЯ (сервер `sub_547EB0` пишет туда номер адресата) — свой `kMeUser`;
			//   нулевой номер ключ `asda.Chat.WhisperUserNumCheck` (эталон) отбрасывает как «шёпот не мне».
			B.SetNumZeroed(29); B[0] = C.Sub; B[1] = 1; AsdaBytes::WrI32(&B[2], CharId); AsdaBytes::WrI16(&B[6], kMeUser); PutName(B, 8, Name); break;
		case 4121:   // WORD@0, DWORD charId@2, WORD@6, char[20] имя@8, текст@28
			B.SetNumZeroed(28); AsdaBytes::WrI32(&B[2], CharId); PutName(B, 8, Name); break;
		case 5106:   // WORD userNum@0, char[20] имя@2, …, текст@25 (сервер `sub_451210`)
			B.SetNumZeroed(25); AsdaBytes::WrI16(&B[0], (int16)UserNum); PutName(B, 2, Name); break;
		case 6719:   // BYTE онлайн@0, char[20] имя@1, текст@21
			B.SetNumZeroed(21); B[0] = C.Sub; PutName(B, 1, Name); break;
		default: break;
		}
		PutText(B, Text);
		return B;
	}

	// Подать сообщение канала от себя и от соседа.
	static void FeedBoth(UAsdaNetSubsystem* Net, const FCase& C)
	{
		Net->DebugFeedPacket(C.Op, Body(C, kMeUser, kMeChar, kSelfName, C.SelfText));
		Net->DebugFeedPacket(C.Op, Body(C, kPeerUser, kPeerChar, kPeerName, C.PeerText));
	}

	// Проверки одного облачка (владелец Who) против ожиданий случая.
	static void CheckOne(FRun& R, UAsdaChatBalloonSubsystem* Bal, AActor* Owner, const FCase& C, const TCHAR* Who, const FString& Msg)
	{
		FAsdaBalloonDebugInfo D;
		const bool bHas = Owner && Bal->DebugGetBalloon(Owner, C.Kind, D);
		R.Expect(bHas && D.bActive && D.bOnScreen,
			FString::Printf(TEXT("%s %s: облачко варианта %d есть=%d активно=%d на экране=%d"), *C.Tag, Who, (int32)C.Kind,
				bHas ? 1 : 0, D.bActive ? 1 : 0, D.bOnScreen ? 1 : 0));
		if (!bHas) { return; }
		R.Expect(D.Color == C.Color, FString::Printf(TEXT("%s %s: цвет текста %s (эталон %s)"), *C.Tag, Who, *D.Color.ToHex(), *C.Color.ToHex()));
		// Текст сверяется без пробелов: перенос стоит на пробеле, и рендер оригинала пробел после '\n' не рисует.
		const FString Flat = D.Text.Replace(TEXT("\n"), TEXT(""));
		const FString FlatNoSp = Flat.Replace(TEXT(" "), TEXT(""));
		if (C.bLong)
		{
			const FString Head = FlatNoSp.LeftChop(3);
			R.Expect(Flat.EndsWith(TEXT("...")) && Msg.Replace(TEXT(" "), TEXT("")).StartsWith(Head) && Head.Len() < Msg.Replace(TEXT(" "), TEXT("")).Len()
					&& D.LineCount >= 3,
				FString::Printf(TEXT("%s %s: длинный текст обрезан с «...» (580 px): %d из %d символов, строк %d — «%s»"),
					*C.Tag, Who, Flat.Len(), Msg.Len(), D.LineCount, *Flat));
		}
		else
		{
			R.Expect(FlatNoSp == Msg.Replace(TEXT(" "), TEXT("")),
				FString::Printf(TEXT("%s %s: текст облачка «%s» = сообщение без имени «%s»"), *C.Tag, Who, *D.Text.Replace(TEXT("\n"), TEXT("⏎")), *Msg));
		}
		TArray<FString> Lines; D.Text.ParseIntoArray(Lines, TEXT("\n"), false);
		const bool bFirst = Lines.Num() > 0 && !Lines[0].TrimStartAndEnd().IsEmpty();
		bool bNoIndent = true;   // строка-продолжение не начинается с пробела (рендер оригинала его пропускает)
		for (int32 i = 1; i < Lines.Num(); ++i) { if (Lines[i].StartsWith(TEXT(" "))) { bNoIndent = false; } }
		// Верх блока строк — эталон: 7 (TextInset) + центровка блока (TextH − 12·строк)/2 = 11 при выравнивании 4/5, 7 при 0.
		if (D.LineCount == 1 && R.OneLineH <= 0.f) { R.OneLineH = D.TextDesired.Y; }   // C1: настоящая высота одной строки TextBlock
		const float L = R.OneLineH > 0.f ? R.OneLineH : D.UeLineHeight + 1.f;   // высота одной строки (TextBlock)
		const float Top = D.TextPos.Y;
		const float WantTop = (D.TextAlign & 0x4) ? 11.f : 7.f;
		const float Bottom = D.PanelSize.Y - (Top + D.TextDesired.Y);
		R.Expect(bFirst && bNoIndent && FMath::IsNearlyEqual(Top, WantTop, 0.5f) && D.TextDesired.Y <= L * (float)D.LineCount + 1.f,
			FString::Printf(TEXT("%s %s: первая строка «%s» не пустая, отступов нет=%d; строк %d, выр. %d; верх текста %.1f (эталон %.0f); ")
				TEXT("блок TextBlock %.1f (ждём ≤ %d×%.1f — без лишней строки); зазор снизу %.1f; окно %.0f×%.0f; поля ассета (%.0f,%.0f,%.0f,%.0f), высота строк %.2f"),
				*C.Tag, Who, Lines.Num() ? *Lines[0] : TEXT(""), bNoIndent ? 1 : 0, D.LineCount, D.TextAlign, Top, WantTop, D.TextDesired.Y,
				D.LineCount, L, Bottom, D.PanelSize.X, D.PanelSize.Y, D.TextMargin.Left, D.TextMargin.Top, D.TextMargin.Right,
				D.TextMargin.Bottom, D.LineHeightPct));
	}

	// Конец: соседа убрать штатным 4011, облачка погасить, свой номер и ключи вернуть; итог и выход.
	static void Finish(const TSharedRef<FRun>& R)
	{
		UWorld* W = R->W.Get();
		const FSubs S = Get(W);
		if (S.Net)
		{
			TArray<uint8> B; B.SetNumZeroed(2); AsdaBytes::WrI16(&B[0], (int16)kPeerUser);
			S.Net->DebugFeedPacket(4011, B);
		}
		if (S.Bal && S.WS) { S.Bal->HideBalloonsFor(SelfOf(S.WS)); }
		R->Self.Reset();
		R->Keys.Reset();
		AsdaSelfTestKit::Finish(W, kCmd, R->bQuit, LogAsdaBalloonTest, TEXT("САМОТЕСТ-ОБЛАЧКО"), R->Ok, R->Fail);
	}

	static void RunCase(const TSharedRef<FRun>& R, int32 I);

	// Проверка случая I после разбора очереди; затем кадр и следующий случай.
	static void CheckCase(const TSharedRef<FRun>& R, int32 I)
	{
		UWorld* W = R->W.Get();
		const FSubs S = Get(W);
		if (!S.Valid()) { R->Expect(false, TEXT("подсистемы пропали")); Finish(R); return; }
		const FCase& C = R->Cases[I];
		AActor* Me = SelfOf(S.WS);
		AActor* Peer = S.WS->FindPlayerActorByName(kPeerName);
		// Ранг: в облачке только при ключе 0 (как оригинал); в окне чата — всегда, у своей и соседской строки.
		const FString RankTag = C.Rank ? FString::Printf(TEXT("[#%d]"), C.Rank) : FString();
		const FString BalloonPrefix = C.bRankInBalloon ? RankTag + TEXT(" ") : FString();
		const FString SelfMsg = BalloonPrefix + C.SelfText, PeerMsg = BalloonPrefix + C.PeerText;
		if (C.Rank)
		{
			UAsdaChatWindowSubsystem* Chat = W->GetGameInstance()->GetSubsystem<UAsdaChatWindowSubsystem>();
			const int32 Lines = Chat ? Chat->DebugCountLogLines(RankTag) - R->RankLinesBase : -1;
			FString Raw, Nick; FColor Col; int32 Slot = -1;
			if (Chat) { Chat->DebugLastLogLine(Raw, Col, Slot, Nick); }
			R->Expect(Lines == 2 && Raw.Contains(RankTag) && Raw.Contains(C.PeerText),
				FString::Printf(TEXT("%s: ранг в окне чата — строк с «%s» +%d (ждём +2: своя и соседа), последняя «%s», слот %d"), *C.Tag, *RankTag, Lines, *Raw, Slot));
		}
		CheckOne(*R, S.Bal, Me, C, TEXT("СВОЁ"), SelfMsg);
		CheckOne(*R, S.Bal, Peer, C, TEXT("СОСЕД"), PeerMsg);
		R->Shot(C.Tag);
		FTimerHandle H;
		W->GetTimerManager().SetTimer(H, FTimerDelegate::CreateLambda([R, I]() { RunCase(R, I + 1); }), kStepSec - kCheckSec, false);
	}

	// Случай I: погасить прошлые облачка (новый вариант не должен смешаться в кадре), подать оба сообщения.
	static void RunCase(const TSharedRef<FRun>& R, int32 I)
	{
		UWorld* W = R->W.Get();
		const FSubs S = Get(W);
		if (!S.Valid()) { R->Expect(false, TEXT("подсистемы пропали")); Finish(R); return; }
		if (I >= R->Cases.Num()) { Finish(R); return; }
		S.Bal->HideBalloonsFor(SelfOf(S.WS));
		S.Bal->HideBalloonsFor(S.WS->FindPlayerActorByName(kPeerName));
		const FCase& Cs = R->Cases[I];
		if (Cs.Rank)
		{
			R->Keys->Set(TEXT("asda.Chat.BalloonRankHide"), Cs.bRankInBalloon ? 0 : 1);
			const UAsdaChatWindowSubsystem* Chat = W->GetGameInstance()->GetSubsystem<UAsdaChatWindowSubsystem>();
			R->RankLinesBase = Chat ? Chat->DebugCountLogLines(FString::Printf(TEXT("[#%d]"), Cs.Rank)) : 0;
		}
		FeedBoth(S.Net, Cs);
		FTimerHandle H;
		W->GetTimerManager().SetTimer(H, FTimerDelegate::CreateLambda([R, I]() { CheckCase(R, I); }), kCheckSec, false);
	}

	// R0 (после разбора очереди): повтор обоих багов на ключах отката, затем ключи как в оригинале и каналы.
	static void CheckRepro(const TSharedRef<FRun>& R)
	{
		UWorld* W = R->W.Get();
		const FSubs S = Get(W);
		if (!S.Valid()) { R->Expect(false, TEXT("R0 подсистемы пропали")); Finish(R); return; }
		AActor* Me = SelfOf(S.WS);
		FAsdaBalloonDebugInfo D;
		const bool bSelf = Me && S.Bal->DebugGetBalloon(Me, EAsdaTalkBox::White, D) && D.bActive;
		R->Expect(!bSelf, FString::Printf(TEXT("R0a повтор бага (asda.Chat.BalloonSelfByName 0): 5106 от своего имени «%s» → облачко над собой есть=%d (ждём 0 — прежний порт)"),
			kSelfName, bSelf ? 1 : 0));
		// Многострочное облачко соседа при старой центровке: верхний зазор больше нижнего.
		AActor* Peer = S.WS->FindPlayerActorByName(kPeerName);
		FAsdaBalloonDebugInfo P;
		const bool bPeer = Peer && S.Bal->DebugGetBalloon(Peer, EAsdaTalkBox::White, P);
		const float L = P.UeLineHeight;
		const float Top = P.TextPos.Y, Bottom = P.PanelSize.Y - (P.TextPos.Y + P.TextDesired.Y);
		TArray<FString> PL; P.Text.ParseIntoArray(PL, TEXT("\n"), false);
		const bool bIndent = PL.Num() > 1 && PL[1].StartsWith(TEXT(" "));
		R->Expect(bPeer && P.LineCount >= 2 && (P.TextAlign & 0x4) && Top - Bottom >= L * 0.45f && bIndent,
			FString::Printf(TEXT("R0b повтор бага (asda.Chat.BalloonVCenterBlock 0, BalloonSkipWrapSpace 0): %d строк, выр. %d — зазор сверху %.1f, снизу %.1f ")
				TEXT("(ждём перекос ≥ полстроки %.1f), вторая строка с отступом=%d"), P.LineCount, P.TextAlign, Top, Bottom, L * 0.5f, bIndent ? 1 : 0));
		R->Shot(TEXT("R0_old_party_self_and_vcenter"));
		R->Keys->Set(TEXT("asda.Chat.BalloonSelfByName"), 1);
		R->Keys->Set(TEXT("asda.Chat.BalloonVCenterBlock"), 1);
		R->Keys->Set(TEXT("asda.Chat.BalloonSkipWrapSpace"), 1);
		FTimerHandle H;
		W->GetTimerManager().SetTimer(H, FTimerDelegate::CreateLambda([R]() { RunCase(R, 0); }), kStepSec - kCheckSec, false);
	}

	// Шаг 1: свой персонаж, сосед, номера; R0 — повтор бага на ключах отката.
	static void Step1(const TSharedRef<FRun>& R)
	{
		UWorld* W = R->W.Get();
		const FSubs S = Get(W);
		if (!S.Valid()) { AsdaSelfTestKit::Refused(W, kCmd, R->bQuit, TEXT("нет подсистем сети/мира/облачков")); return; }
		R->Self = MakeUnique<FAsdaNetSelfScope>(S.Net, kCmd);
		R->Keys = MakeUnique<FAsdaCVarScope>(W, kCmd);
		R->Keys->Set(TEXT("asda.Chat.BalloonTruncate"), 1);
		S.Net->SetSelfUserNumForDebug((uint16)kMeUser);
		S.Net->SelectedCharId = kMeChar;
		AActor* Me = SelfOf(S.WS);
		R->Expect(Me != nullptr && S.WS->GetSelfCharName() == kSelfName,
			FString::Printf(TEXT("старт: свой павн %s, имя «%s» (ждём «%s»)"), Me ? *Me->GetName() : TEXT("<нет>"), *S.WS->GetSelfCharName(), kSelfName));
		if (!Me) { Finish(R); return; }
		const FIntPoint MyTile = AsdaCoord::GameToTile(AsdaCoord::UeToGame2D(Me->GetActorLocation()));
		S.WS->DebugSpawnFakePeer(kPeerUser, kPeerName, AsdaCoord::TileCenterGame(MyTile.X + 2, MyTile.Y + 1));

		// R0: прежнее поведение порта (ключи отката) — свой 5106 и многострочный 5106 соседа.
		R->Keys->Set(TEXT("asda.Chat.BalloonSelfByName"), 0);
		R->Keys->Set(TEXT("asda.Chat.BalloonVCenterBlock"), 0);
		R->Keys->Set(TEXT("asda.Chat.BalloonSkipWrapSpace"), 0);
		FCase Party;
		Party.Op = 5106; Party.SelfText = TEXT("Я тут, жду отряд!"); Party.PeerText = TEXT("Кто со мной в данж у ворот?");
		FeedBoth(S.Net, Party);
		FTimerHandle H;
		W->GetTimerManager().SetTimer(H, FTimerDelegate::CreateLambda([R]() { CheckRepro(R); }), kCheckSec + 0.5f, false);
	}

	// Каналы и тексты (RU — свои, EN — соседа; в длинном случае наоборот). Средние тексты 19..36 символов — ровно тот
	//   диапазон, где эталон центрует блок (выравнивание 4) и где у порта съезжала первая строка.
	static void FillCases(FRun& R)
	{
		auto Add = [&R](const TCHAR* Tag, int32 Op, uint8 Sub, EAsdaTalkBox K, FColor Col, const TCHAR* Me, const TCHAR* Peer, bool bLong = false)
		{
			FCase C; C.Tag = Tag; C.Op = Op; C.Sub = Sub; C.Kind = K; C.Color = Col; C.SelfText = Me; C.PeerText = Peer; C.bLong = bLong;
			R.Cases.Add(C);
		};
		// Цвета — dword'ы эталона (network.c), записаны явно.
		Add(TEXT("C1_obshiy_short_5085"), 5085, 0, EAsdaTalkBox::White, FColor(0x00, 0x00, 0x00, 0xFF), TEXT("Привет!"), TEXT("Hi there!"));
		Add(TEXT("C2_obshiy_5085"), 5085, 0, EAsdaTalkBox::White, FColor(0x00, 0x00, 0x00, 0xFF),
			TEXT("Всем привет, кто идёт со мной?"), TEXT("Hello everyone, who goes with me"));
		Add(TEXT("C3_krik_ohvat0_5087"), 5087, 0, EAsdaTalkBox::White, FColor(0xFF, 0x00, 0x00, 0xFF),
			TEXT("Продаю меч, пишите в шёпот!"), TEXT("Selling a sword, whisper me now!"));
		Add(TEXT("C4_krik_ohvat1_5087"), 5087, 1, EAsdaTalkBox::White, FColor(0xF1, 0xEF, 0x07, 0xFF),
			TEXT("Ищу отряд на босса у моста"), TEXT("Looking for a party near bridge"));
		Add(TEXT("C5_shyopot_5089"), 5089, 0, EAsdaTalkBox::Gold, FColor(0x22, 0x22, 0x22, 0xFF),
			TEXT("Тихо, встречаемся у фонтана"), TEXT("Psst, meet me by the fountain"));
		Add(TEXT("C6_drug_5089"), 5089, 1, EAsdaTalkBox::Gold, FColor(0x22, 0x22, 0x22, 0xFF),
			TEXT("Друг, я уже рядом с тобой!"), TEXT("Friend, I am right next to you!"));
		Add(TEXT("C7_klan_4121"), 4121, 0, EAsdaTalkBox::White, FColor(0x00, 0x71, 0xD1, 0xFF),
			TEXT("Клан, сбор в замке в девять"), TEXT("Clan, gather at the castle at 9"));
		Add(TEXT("C8_otryad_5106"), 5106, 0, EAsdaTalkBox::White, FColor(0x0E, 0x76, 0x00, 0xFF),
			TEXT("Отряд, держимся вместе тут"), TEXT("Party, stay close to me here ok"));
		Add(TEXT("C9_armiya_6719"), 6719, 1, EAsdaTalkBox::White, FColor(0xA0, 0x3C, 0x00, 0xFF),
			TEXT("Армия, все к западным воротам"), TEXT("Army, everyone to the west gate"));
		// Ранг: только в окне чата (`asda.Chat.BalloonRankHide` 1); R0 — повтор бага (ключ 0, ранг в облачке, как оригинал).
		auto AddRank = [&](const TCHAR* Tag, int32 Op, uint8 Sub, const FColor& Col, int32 Rank, bool bInBalloon)
		{
			Add(Tag, Op, Sub, EAsdaTalkBox::White, Col, TEXT("Ранг только в чате"), TEXT("Rank in chat only"));
			R.Cases.Last().Rank = Rank;
			R.Cases.Last().bRankInBalloon = bInBalloon;
		};
		AddRank(TEXT("K0_rang_5085_klyuch0"), 5085, 0, FColor(0x00, 0x00, 0x00, 0xFF), 7, true);
		AddRank(TEXT("K0_rang_5087_klyuch0"), 5087, 0, FColor(0xFF, 0x00, 0x00, 0xFF), 8, true);
		AddRank(TEXT("K1_rang_5085"), 5085, 0, FColor(0x00, 0x00, 0x00, 0xFF), 7, false);
		AddRank(TEXT("K2_rang_5085_100"), 5085, 0, FColor(0x00, 0x00, 0x00, 0xFF), 100, false);
		AddRank(TEXT("K3_rang_5087_ohvat0"), 5087, 0, FColor(0xFF, 0x00, 0x00, 0xFF), 8, false);
		AddRank(TEXT("K4_rang_5087_ohvat1"), 5087, 1, FColor(0xF1, 0xEF, 0x07, 0xFF), 9, false);
		AddRank(TEXT("K5_rang_5087_ohvat2"), 5087, 2, FColor(0xF1, 0xEF, 0x07, 0xFF), 10, false);
		Add(TEXT("L1_dlinny_5085"), 5085, 0, EAsdaTalkBox::White, FColor(0x00, 0x00, 0x00, 0xFF),
			TEXT("Очень длинное сообщение в общий чат, чтобы проверить перенос строк, рост окна по высоте и обрезку текста с многоточием, как в оригинале игры, ну и ещё немного слов сверху для верности"),
			TEXT("This is a very long message in the general chat to check line wrapping, the window growing in height and the text being cut with an ellipsis, as in the original game, plus a few extra words"),
			/*bLong=*/true);
		Add(TEXT("L2_dlinny_otryad_5106"), 5106, 0, EAsdaTalkBox::White, FColor(0x0E, 0x76, 0x00, 0xFF),
			TEXT("Длинное сообщение отряда: идём через лес к северному мосту, там босс, у кого есть зелья — делитесь, хил стоит сзади, танк впереди, все слушают лидера и не убегают"),
			TEXT("Long party message: we go through the forest to the north bridge, the boss is there, share potions, healer stays back, tank in front, everyone listens to the leader"),
			/*bLong=*/true);
	}
}

static FAutoConsoleCommandWithWorldAndArgs GAsdaChatBalloonSelfTest(
	TEXT("asda.Chat.BalloonSelfTest"),
	TEXT("asda.Chat.BalloonSelfTest [delay=N] [quit] [noshot] — ОФЛАЙН-самотест облачков чата путём игрока: общий/крик/шёпот/Друг/клан/")
	TEXT("отряд/армия над собой и соседом, цвет и вариант по эталону, первая строка, центровка, длинный текст с «...»; кадры в Reports/chat_bubbles"),
	FConsoleCommandWithWorldAndArgsDelegate::CreateLambda([](const TArray<FString>& A, UWorld* W)
	{
		using namespace AsdaBalloonTest;
		const FAsdaSelfTestArgs Args = FAsdaSelfTestArgs::Parse(A);
		if (AsdaSelfTestKit::RefuseStart(W, kCmd, Args)) { return; }
		if (!W) { return; }
		TSharedRef<FRun> R = MakeShared<FRun>();
		R->bQuit = Args.bQuit;
		R->bShot = !Args.Has(TEXT("noshot"));
		R->W = W;
		R->Lang = FAsdaLang::Code(FAsdaLang::Current());
		R->ShotDir = FPaths::ConvertRelativePathToFull(FPaths::Combine(FPaths::ProjectDir(), TEXT("../Reports/chat_bubbles")));
		FillCases(*R);
		// Свой персонаж с известным именем (5106/6719 адресуют по имени): офлайн-спавн той же точкой, что 4003.
		if (AsdaSelfTestKit::NeedSelfSpawn(W, kSelfName))
		{
			UGameInstance* GI = W->GetGameInstance();
			UAsdaWorldSubsystem* WS = GI ? GI->GetSubsystem<UAsdaWorldSubsystem>() : nullptr;
			if (!::IsValid(WS ? WS->LocalPlayerActor.Get() : nullptr) && GEngine) { GEngine->Exec(W, TEXT("Asda.Load.CurrentMap 3")); }
			if (WS) { WS->DebugSpawnSelfAt(256, 256, kSelfName); }
		}
		auto Go = [R]()
		{
			UWorld* Wd = R->W.Get();
			if (!Wd) { return; }
			if (AsdaSelfTestKit::RefuseDeferred(Wd, kCmd, R->bQuit)) { return; }
			Step1(R);
		};
		const float Delay = FMath::Max(Args.Delay, 2.f);   // павну и соседу — кадры на появление
		FTimerHandle H;
		W->GetTimerManager().SetTimer(H, FTimerDelegate::CreateLambda(Go), Delay, false);
	}));

#endif // !UE_BUILD_SHIPPING
