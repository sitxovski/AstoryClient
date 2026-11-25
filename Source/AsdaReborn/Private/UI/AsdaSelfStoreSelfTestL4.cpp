// Copyright (c) 2026 @sitxovski (Discord)
// ★★★ САМОТЕСТ ВОЛНЫ Л4 ТОРГОВЛИ — ЧАТ ЛИЧНОЙ ЛАВКИ (30.09.2026, `Reports/trade/wave_L45.md`). [ПОРТ]
//
// ЧТО ПРОВЕРЯЕТ. Путь через НАСТОЯЩИЕ функции: витрина посетителя открыта ШТАТНЫМ приёмом 6308 (`DebugFeedPacket` → `HandlePacket` →
//   делегат → очередь → `PumpPending`); поле 23403 (`Stall_Chat_Input`) — Enter → 6313 БОЕВЫМ отправителем под перехватом
//   (`ArmSendCapture`, офлайн сокета нет): тело 106 б ([ИСПРАВЛЕНИЕ] К-0), @0/@4 хозяин, @6 имя, @26 текст ≤ 79 байт + ноль; мат
//   (9005) → журнал 23402 без имени цветом 0xFFFF9600, 6313 нет; приём 6314 (штатный приём) → строки «имя : текст» шестью цветами
//   по номеру отправителя, номер вне 0..5 — строки нет; сброс сессии чистит поле.
//   Фаза «путём игрока»: настоящий клик `SimulateClickAt` по живой геометрии поля 23403 (поле ЦЕЛИКОМ под ручкой блока чата — реестр
//   П8-12): поле получает фокус, окно не сдвинуто; ввод настоящими событиями клавиатуры Slate (`ProcessKeyCharEvent`), лимит 60
//   (`.JUI` `LimitText`), Enter (`ProcessKeyDownEvent`) → 6313 с набранным текстом; горячая клавиша рюкзака при фокусе в поле съедена
//   полем (буква легла в поле, гейт рюкзака не сработал) ↔ фокус у вьюпорта — та же клавиша доходит до гейта рюкзака (К-17).
//   ★ Контрольные пары (закон pixel-metric-needs-control-pairs): каждое действие проверяется и в противоположном условии.
//
// Консоль: `asda.Trade.SelfTestStoreL4 [delay=N] [quit]`. Только офлайн (`FAsdaOutgoingGate::RefuseDebugWhenOnline`).
//   Строки — «САМОТЕСТ-ЛАВКА-Л4», итог — «САМОТЕСТ-ЛАВКА-Л4 ИТОГ: ok=… fail=…».
#include "CoreMinimal.h"

#if !UE_BUILD_SHIPPING

#include "UI/AsdaSelfStoreSubsystem.h"
#include "UI/AsdaTradeStateSubsystem.h"
#include "UI/AsdaInventorySubsystem.h"
#include "UI/AsdaWindowSubsystem.h"
#include "UI/AsdaNotice.h"
#include "World/AsdaPlayerController.h"
#include "World/AsdaWorldSubsystem.h"
#include "World/AsdaKeybindConfig.h"
#include "AsdaNetSubsystem.h"
#include "Test/AsdaSelfTestKit.h"   // ★ 11.10.2026 (В3 S1): общий каркас самотестов (аргументы, отказ, перехват, итог, выход)
#include "AsdaBytes.h"
#include "Login/AsdaLoginData.h"
#include "Blueprint/UserWidget.h"
#include "Components/Button.h"
#include "Components/EditableText.h"
#include "Components/ScrollBox.h"
#include "Components/TextBlock.h"
#include "Framework/Application/SlateApplication.h"
#include "Containers/Ticker.h"
#include "Engine/Engine.h"
#include "Engine/GameInstance.h"
#include "Engine/World.h"
#include "HAL/IConsoleManager.h"

DEFINE_LOG_CATEGORY_STATIC(LogAsdaStoreTestL4, Log, All);

namespace AsdaStoreL4Local
{
	constexpr int32 kSelfCharId = 424242;   // свой charId
	constexpr int32 kSelfUserNum = 17;      // свой userNum
	constexpr int32 kHostUserNum = 55;      // фейковый сосед-продавец (charId = 900000 + userNum)
	constexpr int32 kHostCharId = 900055;   // charId продавца

	// Тело 6308 (628 б): [0] код, @1 charId хозяина, @5 userNum, @7 число лотов, @8 название[50], @58 имя[20], лоты с @78 (55 б).
	TArray<uint8> Make6308(const FString& Title)
	{
		TArray<uint8> B; B.SetNumZeroed(628);
		B[0] = 1;
		AsdaBytes::WrI32(B.GetData() + 1, kHostCharId);
		AsdaBytes::WrI16(B.GetData() + 5, kHostUserNum);
		B[7] = 1;
		FAsdaLoginData::EncodeCp1251(Title, B.GetData() + 8, 49);
		FAsdaLoginData::EncodeCp1251(TEXT("Продавец"), B.GetData() + 58, 19);
		uint8* P = B.GetData() + 78;
		AsdaBytes::WrI32(P + 0, 20592);
		AsdaBytes::WrI32(P + 4, 5);
		AsdaBytes::WrI32(P + 45, 1000);
		P[8] = 30;
		return B;
	}
	// Тело 6314 (104 б): @0 i32 номер отправителя, @4 имя[20], @24 текст[80].
	TArray<uint8> Make6314(int32 Num, const FString& Name, const FString& Text)
	{
		TArray<uint8> B; B.SetNumZeroed(104);
		AsdaBytes::WrI32(B.GetData(), Num);
		FAsdaLoginData::EncodeCp1251(Name, B.GetData() + 4, 19);
		FAsdaLoginData::EncodeCp1251(Text, B.GetData() + 24, 79);
		return B;
	}
	// Цвета строк 6314 эталона по номеру (shop_trade.c:17878-17889).
	const FColor kNumColor[6] = { FColor(0xFF, 0xFF, 0x00), FColor(0xFF, 0xFF, 0xFF), FColor(0xAC, 0xD3, 0x73),
		FColor(0x00, 0xBF, 0xF3), FColor(0x56, 0x74, 0xB9), FColor(0xF6, 0x8E, 0x56) };
}

struct FAsdaSelfStoreL4SelfTest
{
	// ★ 11.10.2026 (В3 S1, N1(г)): «свой» персонаж (charId, userNum, уровень…) подменяется в Setup — возврат RAII каркаса.
	FAsdaNetSelfScope SelfScope;
	int32 Ok = 0;     // пройдено проверок
	int32 Fail = 0;   // провалено проверок
	struct FCap { uint16 Op = 0; TArray<uint8> P; };           // перехваченный исходящий пакет
	TSharedRef<TArray<FCap>> Caps = MakeShared<TArray<FCap>>();
	uint32 Gen = 0;                                              // поколение перехватчика
	TArray<TFunction<void(UWorld*)>> Steps;                      // шаги фазы «путём игрока»
	FVector2D BtnPos0 = FVector2D::ZeroVector;                   // абсолютная позиция крестика 23005 до клика (окно не сдвинуто)
	FVector2D InputAt = FVector2D::ZeroVector;                   // центр поля 23403 (живая геометрия)
	int32 GateSeq0 = 0;                                          // счётчик отказов «лавка открыта» до нажатия клавиши
	FKey InvKey;                                                 // клавиша команды 33 «INTERFACE INVENTORY» раскладки

	void Expect(bool bCond, const FString& Name, const FString& Detail)
	{
		if (bCond) { ++Ok; } else { ++Fail; }
		UE_LOG(LogAsdaStoreTestL4, Log, TEXT("САМОТЕСТ-ЛАВКА-Л4 %s: %s — %s"), bCond ? TEXT("ok") : TEXT("ПРОВАЛ"), *Name, *Detail);
	}
	int32 Count(uint16 Op, TArray<uint8>* Out = nullptr) const
	{
		int32 N = 0;
		for (const FCap& C : *Caps) { if (C.Op == Op) { ++N; if (Out) { *Out = C.P; } } }
		return N;
	}
	// Подать пакет штатным приёмом и разобрать очередь колбэка витрины (её разбирает Tick; здесь — сразу).
	static void Feed(UAsdaNetSubsystem* Net, UAsdaSelfStoreSubsystem* S, int32 Op, const TArray<uint8>& Body)
	{
		Net->DebugFeedPacket(Op, Body);
		S->PumpPending();
	}
	// Строки журнала 23402 с цветами (дерево WBP, как их видит игрок).
	static void LogLines(const UAsdaSelfStoreSubsystem* S, TArray<FString>& OutText, TArray<FColor>& OutColor)
	{
		OutText.Reset(); OutColor.Reset();
		if (!S->LogList) { return; }
		for (UWidget* C : S->LogList->GetAllChildren())
		{
			const UTextBlock* T = Cast<UTextBlock>(C);
			if (!T) { continue; }
			OutText.Add(T->GetText().ToString());
			OutColor.Add(T->GetColorAndOpacity().GetSpecifiedColor().ToFColor(true));
		}
	}
	static FVector2D AbsCenter(const UWidget* W)
	{
		if (!W) { return FVector2D::ZeroVector; }
		const FGeometry G = W->GetCachedGeometry();
		return G.GetAbsolutePosition() + G.GetAbsoluteSize() * 0.5f;
	}

	void Run(UWorld* W)
	{
		using namespace AsdaStoreL4Local;
		UGameInstance* GI = W ? W->GetGameInstance() : nullptr;
		UAsdaSelfStoreSubsystem* S = GI ? GI->GetSubsystem<UAsdaSelfStoreSubsystem>() : nullptr;
		UAsdaNetSubsystem* Net = GI ? GI->GetSubsystem<UAsdaNetSubsystem>() : nullptr;
		UAsdaWindowSubsystem* WM = GI ? GI->GetSubsystem<UAsdaWindowSubsystem>() : nullptr;
		UAsdaWorldSubsystem* WS = GI ? GI->GetSubsystem<UAsdaWorldSubsystem>() : nullptr;
		if (!S || !Net || !WM || !WS)
		{
			UE_LOG(LogAsdaStoreTestL4, Error, TEXT("САМОТЕСТ-ЛАВКА-Л4 ИТОГ: ok=0 fail=1 (нет подсистем)"));
			return;
		}
		// ── Подготовка: свой персонаж, продавец 55, витрина посетителя штатным 6308, перехват всего трафика ──
		SelfScope.Capture(Net, TEXT("asda.Trade.SelfTestStoreL4"));   // ★ В3 S1: снимок «своего» до подмены
		Net->SelectedCharId = kSelfCharId;
		Net->SetSelfUserNumForDebug(kSelfUserNum);
		Net->SelfLevel = 30;
		WS->DebugSpawnFakePeer(kHostUserNum, TEXT("Продавец"), FVector2D(258.f, 256.f));
		S->ResetSessionState(EAsdaSessionResetReason::MapChanged);
		Caps->Reset();
		TSharedRef<TArray<FCap>> C = Caps;
		Gen = AsdaSelfTestKit::ArmCapture(Net, [C](uint16 Op, const uint8* P, int32 Len, bool, TCHAR)
		{
			FCap X; X.Op = Op; if (P && Len > 0) { X.P.Append(P, Len); } C->Add(MoveTemp(X));
		}, 0, TEXT("asda.Trade.SelfTestStoreL4"));
		WM->ClearMsgBoxQueue(TEXT("самотест лавки Л4"));
		Feed(Net, S, 6308, Make6308(TEXT("Лавка Л4")));
		const FString SelfName = WS->GetSelfCharName();

		// ═══ 1. Поле 23403 разобрано (`Stall_Chat_Input`), витрина посетителя открыта ═══
		Expect(S->IsVisitorShowcaseOpen() && S->EdChat != nullptr && S->EdChat->GetName() == TEXT("Stall_Chat_Input"),
			TEXT("витрина посетителя открыта штатным 6308; поле чата 23403 `Stall_Chat_Input` найдено"),
			FString::Printf(TEXT("витрина=%d поле=%s"), S->IsVisitorShowcaseOpen() ? 1 : 0, S->EdChat ? *S->EdChat->GetName() : TEXT("нет")));

		// ═══ 2. [ПРОТОКОЛ] раскладка 6313: тело 106 б (К-0), текст ≤ 79 байт + ноль ↔ короткий текст — целиком ═══
		{
			const FString Long = FString::ChrN(100, TEXT('ж'));
			const TArray<uint8> B = UAsdaNetSubsystem::BuildSelfStoreChat6313Body(kHostCharId, kHostUserNum, TEXT("Покупатель"), Long);
			const TArray<uint8> B2 = UAsdaNetSubsystem::BuildSelfStoreChat6313Body(kHostCharId, kHostUserNum, TEXT("Покупатель"), TEXT("Привет"));
			const FString T = B.Num() == 106 ? FAsdaLoginData::DecodeCp1251(B.GetData() + 26, 80) : FString();
			const FString T2 = B2.Num() == 106 ? FAsdaLoginData::DecodeCp1251(B2.GetData() + 26, 80) : FString();
			const bool bLay = B.Num() == 106 && AsdaBytes::RdI32(B.GetData()) == kHostCharId && AsdaBytes::RdI16(B.GetData() + 4) == kHostUserNum
				&& FAsdaLoginData::DecodeCp1251(B.GetData() + 6, 20) == TEXT("Покупатель") && T.Len() == 79 && B[105] == 0;
			Expect(bLay && T2 == TEXT("Привет"),
				TEXT("[ИСПРАВЛЕНИЕ] К-0: тело 6313 = 106 б (сервер S:101545), @0 charId хозяина, @4 userNum, @6 имя, @26 текст — 100 символов → 79 + ноль ↔ короткий целиком"),
				FString::Printf(TEXT("тело %d б, текст %d символов, [105]=%d; короткий «%s»"), B.Num(), T.Len(), B.Num() == 106 ? (int32)B[105] : -1, *T2));
		}

		// ═══ 3. Enter по 23403 → 6313 боевым отправителем: хозяин, своё имя, текст; поле очищено ↔ пустое поле — ничего ═══
		if (S->EdChat)
		{
			Caps->Reset();
			S->EdChat->SetText(FText::GetEmpty());
			S->OnChatInputEnter();
			const int32 NEmpty = Count(6313);
			S->EdChat->SetText(FText::FromString(TEXT("Почём зелье?")));
			S->OnChatInputEnter();
			TArray<uint8> P; const int32 N = Count(6313, &P);
			const bool bBody = P.Num() == 106 && AsdaBytes::RdI32(P.GetData()) == kHostCharId && AsdaBytes::RdI16(P.GetData() + 4) == kHostUserNum
				&& FAsdaLoginData::DecodeCp1251(P.GetData() + 6, 20) == SelfName && FAsdaLoginData::DecodeCp1251(P.GetData() + 26, 80) == TEXT("Почём зелье?");
			Expect(NEmpty == 0 && N == 1 && bBody && !SelfName.IsEmpty() && S->EdChat->GetText().IsEmpty(),
				TEXT("Enter в 23403 → 6313 (106 б: хозяин 900055/55, своё НЕПУСТОЕ имя, текст), поле очищено ↔ пустое поле — 6313 нет"),
				FString::Printf(TEXT("пусто: 6313=%d; с текстом: 6313=%d тело %d б имя «%s» поле «%s»"), NEmpty, N, P.Num(),
					P.Num() >= 26 ? *FAsdaLoginData::DecodeCp1251(P.GetData() + 6, 20) : TEXT(""), *S->EdChat->GetText().ToString()));
		}

		// ═══ 4. Мат (`Chat_MatchBannedWord`) → 9005 в журнал 23402 без имени, 0xFFFF9600, поле очищено, 6313 нет ↔ обычный текст — 6313 (п. 3) ═══
		if (S->EdChat)
		{
			Caps->Reset();
			TArray<FString> T0; TArray<FColor> C0; LogLines(S, T0, C0);
			S->bDebugCurseAll = true;
			S->EdChat->SetText(FText::FromString(TEXT("брань")));
			S->OnChatInputEnter();
			S->bDebugCurseAll = false;
			TArray<FString> T1; TArray<FColor> C1; LogLines(S, T1, C1);
			const FString Want = AsdaNotice::Loc(GI, 9005);
			const bool bLine = T1.Num() == T0.Num() + 1 && T1.Last() == Want && C1.Last() == FColor(0xFF, 0x96, 0x00);
			Expect(Count(6313) == 0 && bLine && S->EdChat->GetText().IsEmpty() && !Want.IsEmpty(),
				TEXT("мат в 23403 → строка 9005 в журнал 23402 без имени, цвет 0xFFFF9600, поле очищено, 6313 нет"),
				FString::Printf(TEXT("6313=%d строк %d→%d «%s» цвет %s (ждём «%s» #FF9600)"), Count(6313), T0.Num(), T1.Num(),
					T1.Num() ? *T1.Last() : TEXT(""), C1.Num() ? *C1.Last().ToHex() : TEXT("-"), *Want));
		}

		// ═══ 5. [ПОРТ] Enter без витрины — ничего (окно эталона не шлёт событий, пока скрыто) ↔ витрина открыта — 6313 (п. 3) ═══
		if (S->EdChat)
		{
			Caps->Reset();
			S->bShopOpen = false;
			S->EdChat->SetText(FText::FromString(TEXT("эхо")));
			S->OnChatInputEnter();
			S->bShopOpen = true;
			const int32 NClosed = Count(6313);
			S->OnChatInputEnter();
			Expect(NClosed == 0 && Count(6313) == 1, TEXT("[ПОРТ] Enter при закрытой витрине — 6313 нет ↔ витрина открыта — 6313"),
				FString::Printf(TEXT("закрыта: %d; открыта: %d"), NClosed, Count(6313)));
		}

		// ═══ 6. Приём 6314 (штатный приём): номера 0..5 — «имя : текст» шестью цветами эталона ↔ номер 6 — строки нет ═══
		{
			TArray<FString> T0; TArray<FColor> C0; LogLines(S, T0, C0);
			for (int32 Num = 0; Num <= 6; ++Num)
			{
				Feed(Net, S, 6314, Make6314(Num, FString::Printf(TEXT("Гость%d"), Num), FString::Printf(TEXT("строка %d"), Num)));
			}
			TArray<FString> T1; TArray<FColor> C1; LogLines(S, T1, C1);
			bool bAll = T1.Num() == T0.Num() + 6;
			FString Det;
			for (int32 Num = 0; Num < 6 && bAll; ++Num)
			{
				const int32 I = T0.Num() + Num;
				const FString Want = FString::Printf(TEXT("Гость%d : строка %d"), Num, Num);
				const bool bOne = T1[I] == Want && C1[I] == kNumColor[Num];
				bAll &= bOne;
				Det += FString::Printf(TEXT("[%d «%s» %s] "), Num, *T1[I], *C1[I].ToHex());
			}
			Expect(bAll, TEXT("6314: номер 0 хозяин #FFFF00, 1 белый, 2 #ACD373, 3 #00BFF3, 4 #5674B9, 5 #F68E56 — «имя : текст» ↔ номер 6 — строки нет"),
				FString::Printf(TEXT("строк %d→%d; %s"), T0.Num(), T1.Num(), *Det));
		}

		// ═══ 7. Сброс сессии: недописанная строка поля очищена, пакетов нет ═══
		if (S->EdChat)
		{
			Caps->Reset();
			S->EdChat->SetText(FText::FromString(TEXT("недописано")));
			S->ResetSessionState(EAsdaSessionResetReason::MapChanged);
			Expect(S->EdChat->GetText().IsEmpty() && Caps->Num() == 0 && !S->IsShowcaseOpen(),
				TEXT("сброс сессии: витрина скрыта, поле 23403 очищено, пакетов нет"),
				FString::Printf(TEXT("поле «%s» пакетов %d"), *S->EdChat->GetText().ToString(), Caps->Num()));
		}
	}

	// ═══ ФАЗА «ПУТЁМ ИГРОКА» — шаги раз в 0,4 с, настоящие события Slate по живой геометрии ═══
	void BuildInputPhase()
	{
		using namespace AsdaStoreL4Local;
		auto Ctx = [](UWorld* W, UGameInstance*& GI, UAsdaSelfStoreSubsystem*& S, UAsdaNetSubsystem*& Net, UAsdaWindowSubsystem*& WM)
		{
			GI = W ? W->GetGameInstance() : nullptr;
			S = GI ? GI->GetSubsystem<UAsdaSelfStoreSubsystem>() : nullptr;
			Net = GI ? GI->GetSubsystem<UAsdaNetSubsystem>() : nullptr;
			WM = GI ? GI->GetSubsystem<UAsdaWindowSubsystem>() : nullptr;
			return S && Net && WM;
		};
		auto KeyEv = [](const FKey& K, uint32 Ch) { return FKeyEvent(K, FModifierKeysState(), 0, false, Ch, 0); };
		// 0. Витрина посетителя снова открыта штатным 6308.
		Steps.Add([this, Ctx](UWorld* W)
		{
			UGameInstance* GI; UAsdaSelfStoreSubsystem* S; UAsdaNetSubsystem* Net; UAsdaWindowSubsystem* WM;
			if (!Ctx(W, GI, S, Net, WM)) { return; }
			Net->SelectedCharId = kSelfCharId; Net->SetSelfUserNumForDebug(kSelfUserNum);
			Feed(Net, S, 6308, Make6308(TEXT("Лавка Л4")));
		});
		Steps.Add([](UWorld*) {});   // кадр на раскладку (живая геометрия)
		// 1. Клик по центру поля 23403 (поле целиком под ручкой блока чата — реестр П8-12).
		Steps.Add([this, Ctx](UWorld* W)
		{
			UGameInstance* GI; UAsdaSelfStoreSubsystem* S; UAsdaNetSubsystem* Net; UAsdaWindowSubsystem* WM;
			if (!Ctx(W, GI, S, Net, WM)) { return; }
			InputAt = AbsCenter(S->EdChat);
			BtnPos0 = S->BtnShopX ? S->BtnShopX->GetCachedGeometry().GetAbsolutePosition() : FVector2D::ZeroVector;
			if (!InputAt.IsZero()) { WM->SimulateClickAt(InputAt, /*HoldFrames*/ 3); }
		});
		Steps.Add([](UWorld*) {});   // запас: удержание 3 кадра при низкой частоте кадров -game
		Steps.Add([this, Ctx](UWorld* W)
		{
			UGameInstance* GI; UAsdaSelfStoreSubsystem* S; UAsdaNetSubsystem* Net; UAsdaWindowSubsystem* WM;
			if (!Ctx(W, GI, S, Net, WM)) { return; }
			const FVector2D Pos1 = S->BtnShopX ? S->BtnShopX->GetCachedGeometry().GetAbsolutePosition() : FVector2D(-1.f, -1.f);
			Expect(!InputAt.IsZero() && S->IsChatInputFocused() && S->IsShowcaseOpen() && Pos1.Equals(BtnPos0, 0.5f),
				TEXT("путь игрока (П8-12): клик в поле 23403 под ручкой блока чата — фокус в поле, окно не сдвинуто и открыто"),
				FString::Printf(TEXT("точка (%.0f,%.0f) фокус=%d витрина=%d крестик (%.1f,%.1f)→(%.1f,%.1f)"), InputAt.X, InputAt.Y,
					S->IsChatInputFocused() ? 1 : 0, S->IsShowcaseOpen() ? 1 : 0, BtnPos0.X, BtnPos0.Y, Pos1.X, Pos1.Y));
			// 2. Ввод символами клавиатуры Slate (как WM_CHAR у эталона) — 65 символов, лимит поля 60 (`.JUI` LimitText).
			for (int32 i = 0; i < 65; ++i) { FSlateApplication::Get().ProcessKeyCharEvent(FCharacterEvent(TEXT('я'), FModifierKeysState(), 0, false)); }
		});
		Steps.Add([this, Ctx, KeyEv](UWorld* W)
		{
			UGameInstance* GI; UAsdaSelfStoreSubsystem* S; UAsdaNetSubsystem* Net; UAsdaWindowSubsystem* WM;
			if (!Ctx(W, GI, S, Net, WM)) { return; }
			const int32 Len = S->EdChat ? S->EdChat->GetText().ToString().Len() : -1;
			Expect(Len == UAsdaSelfStoreSubsystem::kChatInputLimit, TEXT("путь игрока: 65 символов с клавиатуры → в поле 60 (`.JUI` LimitText 60)"),
				FString::Printf(TEXT("длина %d"), Len));
			// 3. Очистить и набрать «Торг?», Enter клавишей → 6313.
			if (S->EdChat) { S->EdChat->SetText(FText::GetEmpty()); }
			for (const TCHAR Ch : FString(TEXT("Торг?"))) { FSlateApplication::Get().ProcessKeyCharEvent(FCharacterEvent(Ch, FModifierKeysState(), 0, false)); }
			Caps->Reset();
			FSlateApplication::Get().ProcessKeyDownEvent(KeyEv(EKeys::Enter, 13));
			FSlateApplication::Get().ProcessKeyUpEvent(KeyEv(EKeys::Enter, 13));
		});
		Steps.Add([this, Ctx](UWorld* W)
		{
			UGameInstance* GI; UAsdaSelfStoreSubsystem* S; UAsdaNetSubsystem* Net; UAsdaWindowSubsystem* WM;
			if (!Ctx(W, GI, S, Net, WM)) { return; }
			TArray<uint8> P; const int32 N = Count(6313, &P);
			const FString Txt = P.Num() == 106 ? FAsdaLoginData::DecodeCp1251(P.GetData() + 26, 80) : FString();
			Expect(N == 1 && Txt == TEXT("Торг?") && S->EdChat && S->EdChat->GetText().IsEmpty(),
				TEXT("путь игрока: набор «Торг?» клавиатурой + Enter → 6313 (106 б) с этим текстом, поле очищено"),
				FString::Printf(TEXT("6313=%d тело %d б текст «%s»"), N, P.Num(), *Txt));
			// Сведение (не проверка): держит ли поле фокус после Enter (у Win32-поля эталона фокус остаётся; у UE решает свойство
			//   виджета `ClearKeyboardFocusOnCommit` ассета).
			// ★ Ревизия Л4/Л5 (Л4-1): после точечной правки ассета (`Tools/asda_ui_stall_chat_focus.py`) — проверка, а не сведение:
			//   фокус остаётся в поле, как у Win32-поля эталона.
			Expect(S->IsChatInputFocused() && S->EdChat && !S->EdChat->GetClearKeyboardFocusOnCommit(),
				TEXT("Л4-1: после Enter фокус остаётся в поле 23403 (ассет: ClearKeyboardFocusOnCommit = 0), как у поля эталона"),
				FString::Printf(TEXT("фокус=%d ClearKeyboardFocusOnCommit=%d"), S->IsChatInputFocused() ? 1 : 0,
					(S->EdChat && S->EdChat->GetClearKeyboardFocusOnCommit()) ? 1 : 0));
			// 4. Горячая клавиша рюкзака при фокусе в поле: снова клик в поле, нажатие клавиши команды 33.
			if (!InputAt.IsZero()) { WM->SimulateClickAt(InputAt, 3); }
		});
		Steps.Add([](UWorld*) {});   // запас на клик
		Steps.Add([this, Ctx, KeyEv](UWorld* W)
		{
			UGameInstance* GI; UAsdaSelfStoreSubsystem* S; UAsdaNetSubsystem* Net; UAsdaWindowSubsystem* WM;
			if (!Ctx(W, GI, S, Net, WM)) { return; }
			const AAsdaPlayerController* PC = Cast<AAsdaPlayerController>(W->GetFirstPlayerController());
			FAsdaKeyBind B;
			InvKey = (PC && PC->Keys && PC->Keys->GetBind(33, B) && B.Vk != 0) ? UAsdaKeybindConfig::VkToFKey(B.Vk) : EKeys::I;
			const UAsdaTradeStateSubsystem* TS = GI->GetSubsystem<UAsdaTradeStateSubsystem>();
			GateSeq0 = TS ? TS->GetStoreGateSeq() : 0;
			if (S->EdChat) { S->EdChat->SetText(FText::GetEmpty()); }
			const uint32 Ch = InvKey.GetFName().ToString().Len() == 1 ? (uint32)FChar::ToLower(InvKey.GetFName().ToString()[0]) : 0;
			FSlateApplication::Get().ProcessKeyDownEvent(KeyEv(InvKey, Ch));
			if (Ch) { FSlateApplication::Get().ProcessKeyCharEvent(FCharacterEvent((TCHAR)Ch, FModifierKeysState(), 0, false)); }
		});
		Steps.Add([this, KeyEv](UWorld*) { FSlateApplication::Get().ProcessKeyUpEvent(KeyEv(InvKey, 0)); });
		Steps.Add([](UWorld*) {});   // кадр опроса клавиш (фронт отпускания)
		Steps.Add([this, Ctx, KeyEv](UWorld* W)
		{
			UGameInstance* GI; UAsdaSelfStoreSubsystem* S; UAsdaNetSubsystem* Net; UAsdaWindowSubsystem* WM;
			if (!Ctx(W, GI, S, Net, WM)) { return; }
			const UAsdaTradeStateSubsystem* TS = GI->GetSubsystem<UAsdaTradeStateSubsystem>();
			const int32 Seq = TS ? TS->GetStoreGateSeq() : -1;
			const FString Txt = S->EdChat ? S->EdChat->GetText().ToString() : FString();
			Expect(Seq == GateSeq0 && !Txt.IsEmpty(),
				TEXT("путь игрока: клавиша рюкзака при фокусе в поле 23403 — буква легла в поле, команда рюкзака не дошла (поле съедает клавиши)"),
				FString::Printf(TEXT("клавиша %s, отказов «лавка» %d→%d, поле «%s»"), *InvKey.ToString(), GateSeq0, Seq, *Txt));
			// 5. Контроль: фокус у вьюпорта — та же клавиша доходит до диспетча, рюкзак упирается в гейт витрины (К-17).
			if (S->EdChat) { S->EdChat->SetText(FText::GetEmpty()); }
			FSlateApplication::Get().SetAllUserFocusToGameViewport();
			GateSeq0 = Seq;
			FSlateApplication::Get().ProcessKeyDownEvent(KeyEv(InvKey, 0));
		});
		Steps.Add([this, KeyEv](UWorld*) { FSlateApplication::Get().ProcessKeyUpEvent(KeyEv(InvKey, 0)); });
		Steps.Add([](UWorld*) {});   // кадр опроса клавиш (фронт отпускания)
		Steps.Add([this, Ctx](UWorld* W)
		{
			UGameInstance* GI; UAsdaSelfStoreSubsystem* S; UAsdaNetSubsystem* Net; UAsdaWindowSubsystem* WM;
			if (!Ctx(W, GI, S, Net, WM)) { return; }
			const UAsdaTradeStateSubsystem* TS = GI->GetSubsystem<UAsdaTradeStateSubsystem>();
			const int32 Seq = TS ? TS->GetStoreGateSeq() : -1;
			Expect(Seq == GateSeq0 + 1 && TS && TS->GetLastStoreGateWho().StartsWith(TEXT("uiOpenInventoryWindow")) && !S->IsChatInputFocused(),
				TEXT("путь игрока (контроль): фокус у вьюпорта — та же клавиша доходит до рюкзака, гейт витрины 23000 (К-17) срабатывает"),
				FString::Printf(TEXT("отказов %d→%d, читатель «%s» loc %d"), GateSeq0, Seq, TS ? *TS->GetLastStoreGateWho() : TEXT("-"),
					TS ? TS->GetLastStoreGateLoc() : -1));
		});
	}

	void Disarm(UAsdaNetSubsystem* Net) { if (Net) { AsdaSelfTestKit::DisarmCapture(Net, Gen); } }
};

namespace
{
	// ★ R5-fix-c (13.11.2026): своя `ParseStoreL4Delay` (дословная копия у двенадцати самотестов) убрана — разбор `delay=N` в общей точке
	//   `AsdaSelfTestKit::DelayArg` (`Test/AsdaSelfTestKit.h`), вызовы ниже идут в неё.

	FAutoConsoleCommandWithWorldAndArgs GAsdaTradeSelfTestStoreL4(
		TEXT("asda.Trade.SelfTestStoreL4"),
		TEXT("asda.Trade.SelfTestStoreL4 [delay=N] [quit] — самотест чата личной лавки (волна Л4): поле 23403 → 6313 (106 б, К-0), мат 9005, "
		     "приём 6314 (6 цветов), сброс; путь игрока — клик в поле под ручкой, ввод клавиатурой (лимит 60), Enter → 6313, клавиша рюкзака "
		     "съедена полем ↔ доходит без фокуса. Только офлайн."),
		FConsoleCommandWithWorldAndArgsDelegate::CreateLambda([](const TArray<FString>& Args, UWorld* W)
		{
			const FAsdaSelfTestArgs KitArgs = FAsdaSelfTestArgs::Parse(Args);   // ★ 11.10.2026 (В3 S1): каркас — отказ с ИТОГ и quit, второй барьер на все опкоды
			if (AsdaSelfTestKit::RefuseStart(W, TEXT("asda.Trade.SelfTestStoreL4"), KitArgs)) { return; }
			const float Delay = AsdaSelfTestKit::DelayArg(Args);
			const bool bQuit = Args.Contains(TEXT("quit"));
			TWeakObjectPtr<UWorld> WW(W);
			FTSTicker::GetCoreTicker().AddTicker(FTickerDelegate::CreateLambda([WW, bQuit](float) -> bool
			{
				UWorld* Wl = WW.Get();
				if (!Wl) { return false; }
				if (AsdaSelfTestKit::RefuseDeferred(Wl, TEXT("asda.Trade.SelfTestStoreL4"), bQuit)) { return false; }   // Р-2: офлайн повторно — на отложенном старте
				// ★ 11.10.2026 (В3 S1, загрязнение пакета): павн от прошлого самотеста с другим (пустым) именем — тоже спавн с известным.
				const bool bNeedSpawn = AsdaSelfTestKit::NeedSelfSpawn(Wl, TEXT("ГостьЛавки"));
				if (bNeedSpawn && GEngine)
				{
					GEngine->Exec(Wl, TEXT("Asda.Load.CurrentMap 3"));
					// ★ Ревизия Л4/Л5 (МЕЛК-2): своему персонажу — НЕПУСТОЕ имя (штатный крюк офлайн-спавна), иначе сверка имени
					//   отправителя в 6313 вырождена («пусто == пусто»).
					GEngine->Exec(Wl, TEXT("Asda.World.FakeSelfSpawn 256 256 ГостьЛавки"));
				}
				FTSTicker::GetCoreTicker().AddTicker(FTickerDelegate::CreateLambda([WW, bQuit](float) -> bool
				{
					UWorld* W2 = WW.Get();
					if (!W2) { return false; }
					TSharedRef<FAsdaSelfStoreL4SelfTest> T = MakeShared<FAsdaSelfStoreL4SelfTest>();
					T->Run(W2);
					T->BuildInputPhase();
					TSharedRef<int32> Idx = MakeShared<int32>(0);
					FTSTicker::GetCoreTicker().AddTicker(FTickerDelegate::CreateLambda([WW, T, Idx, bQuit](float) -> bool
					{
						UWorld* W3 = WW.Get();
						if (!W3) { return false; }
						if (*Idx < T->Steps.Num()) { T->Steps[(*Idx)++](W3); return true; }
						UGameInstance* GI3 = W3->GetGameInstance();
						if (UAsdaNetSubsystem* Net3 = GI3 ? GI3->GetSubsystem<UAsdaNetSubsystem>() : nullptr) { T->Disarm(Net3); }
						if (UAsdaSelfStoreSubsystem* S3 = GI3 ? GI3->GetSubsystem<UAsdaSelfStoreSubsystem>() : nullptr) { S3->ResetSessionState(EAsdaSessionResetReason::MapChanged); }
						if (UAsdaWindowSubsystem* WM3 = GI3 ? GI3->GetSubsystem<UAsdaWindowSubsystem>() : nullptr) { WM3->ClearMsgBoxQueue(TEXT("самотест Л4: итог")); }
						UE_LOG(LogAsdaStoreTestL4, Log, TEXT("САМОТЕСТ-ЛАВКА-Л4 ИТОГ: ok=%d fail=%d"), T->Ok, T->Fail);
						T->SelfScope.Restore();   // ★ В3 S1 (N1(г)): «свой» — как до прогона
						AsdaSelfTestKit::Quit(W3, bQuit);
						return false;
					}), 0.4f);
					return false;
				}), bNeedSpawn ? 2.f : 0.f);
				return false;
			}), Delay);
			UE_LOG(LogAsdaStoreTestL4, Log, TEXT("САМОТЕСТ-ЛАВКА-Л4: запуск через %.1f с"), Delay);
		}));
}

#endif // !UE_BUILD_SHIPPING
