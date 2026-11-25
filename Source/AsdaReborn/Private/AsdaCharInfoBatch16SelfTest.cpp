// Copyright (c) 2026 @sitxovski (Discord)
// ★★★ САМОТЕСТ ПАЧКИ 16, БАГИ 4 И 5 (08.11.2026): окно персонажа 900 (клавиша C).
//
//   БАГ 4 — функция «Информация о персонаже» (код 205, вкладка «Функции» окна C = `PlayEmotion(205)`, эталон кат. 2 case 5):
//   I1. Цель — я сам: окно 4200 открывается из своих данных ([ИСПРАВЛЕНИЕ] `asda.OtherChara.SelfLocal`), 5478 не уходит; имя —
//       своё, надетое (11×57 б) — из контейнера 3. I2. Повторный клик — окно снова открыто, 5478 нет.
//   I3. Цель — сосед: 5478 с его userNum; ответ 5479+7006 открывает окно о нём. I4. Повторный клик по соседу сразу — второй
//       5478 (эталон обнуляет `win[40]`, 9419 нет; `asda.PlayerMenu.InfoNoLatch`). Контроль: ключ 0 → второго 5478 нет (прежний
//       порт — 9419); `asda.OtherChara.SelfLocal 0` → 5478 о себе уходит, окно не открывается (как оригинал).
//
//   БАГ 5 — под-вкладки страницы «Статус» (981):
//   E1. Подписи трёх под-вкладок (loc 8050/8051/8052, клоны `CHARAINFO_STMENU_Cap<N>`) влезают в элемент переключателя
//       при НОРМАЛЬНОМ кегле образца `CHARAINFO_STMENU_Text` — без автоужатия `BuildStatTabCaptions`.
//   E2. Замер вариантов подписи третьей вкладки (стихии атаки/защиты 12049…12061) при нормальном кегле — в журнал.
//
//   Кадры — `Reports/batch16/charinfo/<язык>_<кадр>.png`.
// Консоль: `asda.CharInfo.Batch16SelfTest [delay=N] [quit]`. Только офлайн. Строки — «САМОТЕСТ-П16», итог — «… ИТОГ: ok=… fail=…».
#include "CoreMinimal.h"

#if !UE_BUILD_SHIPPING

#include "UI/AsdaCharaInfoSubsystem.h"
#include "UI/AsdaOtherCharaSubsystem.h"
#include "UI/AsdaInventorySubsystem.h"
#include "UI/AsdaUiHelpers.h"                // IsShown / SlotRect / MeasureTextWidth
#include "World/AsdaWorldSubsystem.h"
#include "World/AsdaPlayerCharacter.h"
#include "World/AsdaPlayerController.h"
#include "AsdaNetSubsystem.h"
#include "AsdaBytes.h"
#include "AsdaSelf.h"
#include "AsdaCVarSave.h"
#include "Test/AsdaSelfTestKit.h"
#include "Text/AsdaLang.h"
#include "Components/PanelWidget.h"
#include "Components/TextBlock.h"
#include "Containers/Ticker.h"
#include "Engine/Engine.h"
#include "Engine/GameInstance.h"
#include "Engine/World.h"
#include "HAL/IConsoleManager.h"
#include "Misc/Paths.h"
#include "UObject/UObjectIterator.h"
#include "UnrealClient.h"

DEFINE_LOG_CATEGORY_STATIC(LogAsdaP16CharInfoTest, Log, All);

namespace AsdaP16CharInfoTestLocal
{
	static const TCHAR* kWho = TEXT("asda.CharInfo.Batch16SelfTest");   // имя самотеста (сторож ключей)
	constexpr int32 kPeer = 611;                                         // userNum соседа
	constexpr int32 kPeerCid = 990611;                                   // его charId
	// Тело 4005 (151 б): сосед (charId, имя, пол/класс/уровень), игровая позиция — как `AsdaKeybindActionsSelfTest`.
	static TArray<uint8> Body4005Peer(const FVector2D& G)
	{
		TArray<uint8> B; B.SetNumZeroed(151);
		AsdaBytes::WrI16(B.GetData() + 0, (int16)kPeer);
		B[6] = 1;
		AsdaBytes::WrI32(B.GetData() + 11, kPeerCid);
		const char* Name = "P16Peer";
		FMemory::Memcpy(B.GetData() + 15, Name, FCStringAnsi::Strlen(Name));
		B[37] = 1; B[38] = 1; B[40] = 12;   // пол, класс, уровень
		const float X = (float)G.X, Z = (float)G.Y;
		FMemory::Memcpy(B.GetData() + 56, &X, 4);
		FMemory::Memcpy(B.GetData() + 60, &Z, 4);
		return B;
	}
}

struct FAsdaP16CharInfoSelfTest
{
	int32 Ok = 0;     // пройдено
	int32 Fail = 0;   // провалено
	TArray<TFunction<void(UWorld*)>> Steps;   // шаги, по одному за тик
	bool bWinWas = false;                     // окно 900 было открыто
	bool bSetupFailed = false;                // подсистемы нет — шаги не выполняются
	bool bAbort = false;                      // окно не открылось
	FString Lang;                             // ru / en — префикс кадров
	FString ShotDir;                          // папка кадров
	TArray<int32> Sent5478;                   // перехваченные 5478 (userNum цели)
	uint32 CapGen = 0;                        // поколение перехвата отправок
	TUniquePtr<FAsdaNetSelfScope> SelfScope;  // свой номер/charId вернутся в конце
	bool bWasInGame = false;                  // `bInGameConnected` до прогона
	FString ClanWas;                          // `SelfClanName` до прогона

	// Одна проверка.
	void Expect(bool b, const FString& Name, const FString& Detail = FString())
	{
		if (b) { ++Ok; } else { ++Fail; }
		UE_LOG(LogAsdaP16CharInfoTest, Log, TEXT("САМОТЕСТ-П16 %s: %s%s%s"), b ? TEXT("ok") : TEXT("ПРОВАЛ"), *Name,
			Detail.IsEmpty() ? TEXT("") : TEXT(" — "), *Detail);
	}
	static UAsdaCharaInfoSubsystem* CI(UWorld* W)
	{
		UGameInstance* GI = W ? W->GetGameInstance() : nullptr;
		return GI ? GI->GetSubsystem<UAsdaCharaInfoSubsystem>() : nullptr;
	}
	static UAsdaOtherCharaSubsystem* OC(UWorld* W)
	{
		UGameInstance* GI = W ? W->GetGameInstance() : nullptr;
		return GI ? GI->GetSubsystem<UAsdaOtherCharaSubsystem>() : nullptr;
	}
	static AAsdaPlayerController* PCOf(UWorld* W) { return W ? Cast<AAsdaPlayerController>(W->GetFirstPlayerController()) : nullptr; }
	static AActor* PeerActor(UWorld* W)
	{
		UGameInstance* GI = W ? W->GetGameInstance() : nullptr;
		const UAsdaWorldSubsystem* Ws = GI ? GI->GetSubsystem<UAsdaWorldSubsystem>() : nullptr;
		return Ws ? Ws->GetPlayerActorByCharId(AsdaP16CharInfoTestLocal::kPeerCid) : nullptr;
	}
	// Клик по функции 205 вкладки «Функции» окна C.
	static bool ClickInfo(UWorld* W) { UAsdaCharaInfoSubsystem* C = CI(W); return C && C->PlayEmotion(205); }
	// Ответ сервера о соседе: 5479 (121 б, клан @3, ключ пары @116) + 7006 (631 б, ключ @0).
	static void FeedPeerReply(UWorld* W, int32 Key)
	{
		UGameInstance* GI = W ? W->GetGameInstance() : nullptr;
		UAsdaNetSubsystem* Net = GI ? GI->GetSubsystem<UAsdaNetSubsystem>() : nullptr;
		if (!Net) { return; }
		TArray<uint8> A; A.SetNumZeroed(121); FMemory::Memcpy(A.GetData() + 3, "P16Clan", 7); AsdaBytes::WrI32(A.GetData() + 116, Key);
		for (int32 i = 0; i < 12; ++i)   // пустые слоты заданий — −1, как сервер (`sub_558830`)
		{
			AsdaBytes::WrI32(A.GetData() + 20 + 4 * i, -1); AsdaBytes::WrI16(A.GetData() + 68 + 2 * i, -1); AsdaBytes::WrI16(A.GetData() + 92 + 2 * i, -1);
		}
		TArray<uint8> B; B.SetNumZeroed(631); AsdaBytes::WrI32(B.GetData(), Key);
		Net->DebugFeedPacket(5479, A);
		Net->DebugFeedPacket(7006, B);
	}
	// Надетое контейнера 3 = то, что легло в окно о себе.
	static bool GearMatchesSelf(UWorld* W, int32& OutWorn)
	{
		OutWorn = 0;
		UGameInstance* GI = W ? W->GetGameInstance() : nullptr;
		const UAsdaInventorySubsystem* Inv = GI ? GI->GetSubsystem<UAsdaInventorySubsystem>() : nullptr;
		const UAsdaOtherCharaSubsystem* O = OC(W);
		if (!Inv || !O || O->DebugShown().Gear.Num() != 627) { return false; }
		for (int32 S = 0; S < 11; ++S)
		{
			const FAsdaItemInstance* It = Inv->GetCell(3, S);
			TArray<uint8> Want; Want.SetNumZeroed(57);
			if (It && It->Raw.Num() >= 57)
			{
				FMemory::Memcpy(Want.GetData(), It->Raw.GetData(), 57);
				if (It->ItemCode > 0) { ++OutWorn; }
			}
			if (FMemory::Memcmp(Want.GetData(), O->DebugShown().Gear.GetData() + 57 * S, 57) != 0) { return false; }
		}
		return true;
	}
	// Текст видимого узла по имени (окно 4200).
	static FString NodeText(UWorld* W, const TCHAR* Name)
	{
		for (TObjectIterator<UTextBlock> It; It; ++It)
		{
			if (It->GetWorld() == W && It->GetName() == Name && It->GetCachedWidget().IsValid() && AsdaUi::IsShown(*It)) { return It->GetText().ToString(); }
		}
		return FString(TEXT("<нет>"));
	}
	// Живой клон подписи под-вкладки N.
	static UTextBlock* Cap(UWorld* W, int32 N)
	{
		const FString Want = FString::Printf(TEXT("CHARAINFO_STMENU_Cap%d"), N);
		for (TObjectIterator<UTextBlock> It; It; ++It)
		{
			UTextBlock* T = *It;
			if (T && T->GetWorld() == W && T->GetName() == Want && T->GetParent()) { return T; }
		}
		return nullptr;
	}
	// Образец подписи (с него клонируются подписи) — сосед клона по родителю.
	static UTextBlock* SourceText(UTextBlock* AnyCap)
	{
		UPanelWidget* P = AnyCap ? AnyCap->GetParent() : nullptr;
		if (!P) { return nullptr; }
		for (int32 i = 0; i < P->GetChildrenCount(); ++i)
		{
			if (UTextBlock* T = Cast<UTextBlock>(P->GetChildAt(i)))
			{
				if (T->GetName() == TEXT("CHARAINFO_STMENU_Text")) { return T; }
			}
		}
		return nullptr;
	}
	// Кадр окна в файл.
	void Shot(const TCHAR* Name)
	{
		const FString File = FPaths::Combine(ShotDir, FString::Printf(TEXT("%s_%s.png"), *Lang, Name));
		FScreenshotRequest::RequestScreenshot(File, /*bShowUI=*/true, /*bAddFilenameSuffix=*/false);
		UE_LOG(LogAsdaP16CharInfoTest, Log, TEXT("САМОТЕСТ-П16 кадр: %s"), *File);
	}

	void Setup(UWorld* W)
	{
		UAsdaCharaInfoSubsystem* C = CI(W);
		UGameInstance* GI = W ? W->GetGameInstance() : nullptr;
		UAsdaNetSubsystem* Net = GI ? GI->GetSubsystem<UAsdaNetSubsystem>() : nullptr;
		if (!C || !Net || !OC(W)) { Expect(false, TEXT("нет подсистем")); bSetupFailed = true; return; }
		Lang = FAsdaLang::IsRu() ? TEXT("ru") : TEXT("en");
		ShotDir = FPaths::ConvertRelativePathToFull(FPaths::Combine(FPaths::ProjectDir(), TEXT("../Reports/batch16/charinfo")));
		bWinWas = C->IsWindowOpen();
		if (bWinWas) { C->CloseWindow(); }
		CapGen = AsdaSelfTestKit::ArmCapture(Net, [this](uint16 Op, const uint8* P, int32 Len, bool, TCHAR)
		{
			if (Op == 5478 && P && Len >= 2) { Sent5478.Add((int32)AsdaBytes::RdI16(P)); }
		}, 0, AsdaP16CharInfoTestLocal::kWho);
		if (!CapGen) { Expect(false, TEXT("перехват отправок занят")); bSetupFailed = true; return; }
		SelfScope = MakeUnique<FAsdaNetSelfScope>(Net, AsdaP16CharInfoTestLocal::kWho);
		if (!Net->bSelfUserNumKnown) { Net->SelfUserNum = 4321; Net->bSelfUserNumKnown = true; }
		if (Net->SelectedCharId == 0) { Net->SelectedCharId = 990400; }
		bWasInGame = Net->bInGameConnected;
		Net->bInGameConnected = true;
		APlayerController* PC = W->GetFirstPlayerController();
		AAsdaPlayerCharacter* Me = PC ? Cast<AAsdaPlayerCharacter>(PC->GetPawn()) : nullptr;
		if (!Me) { Expect(false, TEXT("нет своего павна")); bSetupFailed = true; return; }
		Net->DebugFeedPacket(4005, AsdaP16CharInfoTestLocal::Body4005Peer(Me->GetGamePosXZ() + FVector2D(250.f, 0.f)));
	}

	void BuildSteps()
	{
		// ════════ БАГ 4 ════════
		Steps.Add([this](UWorld* W)
		{
			UAsdaCharaInfoSubsystem* C = CI(W); if (!C) { return; }
			C->ToggleTab(0);   // цель команды 30 (клавиша C)
			if (!C->IsWindowOpen()) { Expect(false, TEXT("S1 окно 900 открыто")); bAbort = true; return; }
			C->SwitchTabMode(1);   // вкладка «Функции»
			// Свои данные окна: клан (char+2410) и уровень (char+1082) — подставные, чтобы проверка видела именно их.
			if (UAsdaNetSubsystem* Net = W->GetGameInstance()->GetSubsystem<UAsdaNetSubsystem>())
			{
				ClanWas = Net->SelfClanName; Net->SelfClanName = TEXT("P16Mine"); Net->SelfLevel = 33;
			}
			AAsdaPlayerController* PC = PCOf(W);
			if (PC) { PC->SetTarget(PC->GetPawn()); }   // цель — я сам
			Sent5478.Reset();
			const bool bRet = ClickInfo(W);
			UAsdaOtherCharaSubsystem* O = OC(W);
			const UAsdaWorldSubsystem* Ws = W->GetGameInstance()->GetSubsystem<UAsdaWorldSubsystem>();
			int32 Worn = 0;
			const bool bGear = GearMatchesSelf(W, Worn);
			Expect(bRet && O && O->IsOpen() && O->DebugIsSelfTarget() && Sent5478.Num() == 0,
				TEXT("I1 «Информация о персонаже» по себе → окно 4200 из своих данных, 5478 не ушёл"),
				FString::Printf(TEXT("возврат %d, окно %d, о себе %d, 5478 ушло %d"), bRet ? 1 : 0, O && O->IsOpen() ? 1 : 0,
					O && O->DebugIsSelfTarget() ? 1 : 0, Sent5478.Num()));
			const FString Nm = NodeText(W, TEXT("OTHERCHARA_DATATXT00"));
			const FString Lv = NodeText(W, TEXT("OTHERCHARA_DATATXT04"));
			Expect(Ws && Nm == Ws->GetSelfCharName() && bGear && NodeText(W, TEXT("OTHERCHARA_DATATXT08")) == TEXT("P16Mine") && Lv.StartsWith(TEXT("33 ")),
				TEXT("I1b имя 4202, уровень 4207, клан 4211 — свои; надетое 11×57 б — байт в байт контейнер 3"),
				FString::Printf(TEXT("имя «%s» / своё «%s», уровень «%s», класс «%s», клан «%s», знак «%s», надето %d"), *Nm,
					Ws ? *Ws->GetSelfCharName() : TEXT(""), *NodeText(W, TEXT("OTHERCHARA_DATATXT04")),
					*NodeText(W, TEXT("OTHERCHARA_DATATXT06")), *NodeText(W, TEXT("OTHERCHARA_DATATXT08")),
					*NodeText(W, TEXT("OTHERCHARA_DATATXT11")), Worn));
			// Пустые слоты журнала не дают строк заданий (у сервера в них −1, ключ 0 нашёл бы эпизод).
			int32 Rows = 0, Busy = 0;
			for (int32 k = 0; k < 12; ++k)
			{
				const FString Q = NodeText(W, *FString::Printf(TEXT("OtherChara_Qst%02d"), k + 1));
				if (!Q.IsEmpty() && Q != TEXT("<нет>")) { ++Rows; }
				if (O && O->DebugShown().QuestId.IsValidIndex(k) && O->DebugShown().QuestId[k] > 0) { ++Busy; }
			}
			Expect(O && Rows == Busy, TEXT("I1c строки заданий 4251… — только занятые слоты журнала"),
				FString::Printf(TEXT("строк %d, занятых слотов %d"), Rows, Busy));
			Shot(TEXT("self_info"));
		});
		Steps.Add([this](UWorld* W)
		{
			Sent5478.Reset();
			const bool bRet = ClickInfo(W);
			UAsdaOtherCharaSubsystem* O = OC(W);
			Expect(bRet && O && O->IsOpen() && O->DebugIsSelfTarget() && Sent5478.Num() == 0,
				TEXT("I2 повторный клик по себе → окно снова открыто, 5478 нет, 9419 нет"),
				FString::Printf(TEXT("возврат %d, окно %d, 5478 ушло %d"), bRet ? 1 : 0, O && O->IsOpen() ? 1 : 0, Sent5478.Num()));
			Shot(TEXT("self_info_again"));
		});
		Steps.Add([this](UWorld* W)
		{
			AAsdaPlayerController* PC = PCOf(W);
			AActor* Peer = PeerActor(W);
			if (!PC || !Peer) { Expect(false, TEXT("I3 сосед в мире")); return; }
			PC->SetTarget(Peer);
			Sent5478.Reset();
			ClickInfo(W);
			UAsdaOtherCharaSubsystem* O = OC(W);
			Expect(Sent5478.Num() == 1 && Sent5478[0] == AsdaP16CharInfoTestLocal::kPeer && O && !O->DebugIsSelfTarget()
				&& O->DebugTargetUserNum() == AsdaP16CharInfoTestLocal::kPeer,
				TEXT("I3 клик по соседу → 5478 с его userNum, окно ждёт ответ о нём"),
				FString::Printf(TEXT("5478 ушло %d (цель %d)"), Sent5478.Num(), Sent5478.Num() ? Sent5478[0] : -1));
			ClickInfo(W);
			Expect(Sent5478.Num() == 2, TEXT("I4 повторный клик по соседу сразу → второй 5478, без 9419 (эталон обнуляет win[40])"),
				FString::Printf(TEXT("5478 ушло %d"), Sent5478.Num()));
			FeedPeerReply(W, 4242);
		});
		Steps.Add([this](UWorld* W)
		{
			UAsdaOtherCharaSubsystem* O = OC(W);
			Expect(O && O->IsOpen() && !O->DebugIsSelfTarget() && NodeText(W, TEXT("OTHERCHARA_DATATXT00")) == TEXT("P16Peer"),
				TEXT("I3b ответ 5479+7006 о соседе → окно о нём"), FString::Printf(TEXT("имя «%s», клан «%s»"),
					*NodeText(W, TEXT("OTHERCHARA_DATATXT00")), *NodeText(W, TEXT("OTHERCHARA_DATATXT08"))));
			Shot(TEXT("peer_info"));
			// Контроль 1: прежняя защёлка порта.
			AsdaCVarSave::SetForTest(TEXT("asda.PlayerMenu.InfoNoLatch"), 0);
			Sent5478.Reset();
			ClickInfo(W);
			ClickInfo(W);
			Expect(Sent5478.Num() == 1, TEXT("I5 контроль asda.PlayerMenu.InfoNoLatch 0 → второй клик 5478 не шлёт (прежний порт — 9419)"),
				FString::Printf(TEXT("5478 ушло %d"), Sent5478.Num()));
			AsdaCVarSave::SetForTest(TEXT("asda.PlayerMenu.InfoNoLatch"), 1);
			// Контроль 2: о себе как оригинал.
			AsdaCVarSave::SetForTest(TEXT("asda.OtherChara.SelfLocal"), 0);
			if (O) { O->CloseWindow(); }
			AAsdaPlayerController* PC = PCOf(W);
			if (PC) { PC->SetTarget(PC->GetPawn()); }
			Sent5478.Reset();
			ClickInfo(W);
			const int32 Self = AsdaSelf::GetUserIdxOr(W->GetGameInstance(), -1);
			Expect(Sent5478.Num() == 1 && Sent5478[0] == (int32)(int16)Self && O && !O->IsOpen(),
				TEXT("I6 контроль asda.OtherChara.SelfLocal 0 → 5478 о себе уходит, окно не открыто (оригинал: сервер молчит)"),
				FString::Printf(TEXT("5478 ушло %d, окно %d"), Sent5478.Num(), O && O->IsOpen() ? 1 : 0));
			AsdaCVarSave::SetForTest(TEXT("asda.OtherChara.SelfLocal"), 1);
			if (O) { O->CloseWindow(); }
			if (PC) { PC->SetTarget(nullptr); }
		});

		// ════════ БАГ 5 ════════
		Steps.Add([this](UWorld* W)
		{
			UAsdaCharaInfoSubsystem* C = CI(W); if (!C) { return; }
			if (!C->IsWindowOpen()) { C->ToggleTab(0); }
			if (!C->IsWindowOpen()) { Expect(false, TEXT("S2 окно 900 открыто")); bAbort = true; return; }
			C->SwitchTabMode(0);
			C->SelectStatPage(2);
		});
		Steps.Add([this](UWorld* W)
		{
			UTextBlock* C0 = Cap(W, 0);
			UTextBlock* Src = SourceText(C0);
			if (!C0 || !Src) { Expect(false, TEXT("E0 подписи под-вкладок и образец найдены")); return; }
			const FSlateFontInfo Base = Src->GetFont();
			UE_LOG(LogAsdaP16CharInfoTest, Log, TEXT("САМОТЕСТ-П16 образец CHARAINFO_STMENU_Text: кегль %d, шрифт %s"),
				(int32)Base.Size, *GetNameSafe(Base.FontObject));
			for (int32 N = 0; N < 3; ++N)
			{
				UTextBlock* T = Cap(W, N);
				FVector2D P, S;
				if (!T || !AsdaUi::SlotRect(T, P, S)) { Expect(false, FString::Printf(TEXT("E1.%d подпись найдена"), N)); continue; }
				const FString Txt = T->GetText().ToString();
				const float Limit = FMath::Max(8.f, (float)S.X - 4.f);
				const float AtBase = AsdaUi::MeasureTextWidth(Txt, Base, -1.f);
				const int32 Px = (int32)T->GetFont().Size;
				Expect(Px == (int32)Base.Size && AtBase <= Limit,
					FString::Printf(TEXT("E1.%d подпись «%s» — нормальный кегль, влезает"), N, *Txt),
					FString::Printf(TEXT("кегль %d (норма %d), ширина при норме %.1f, место %.1f (элемент %.0f)"),
						Px, (int32)Base.Size, AtBase, Limit, S.X));
			}
			FVector2D P2, S2;
			UTextBlock* C2 = Cap(W, 2);
			const float Limit2 = (C2 && AsdaUi::SlotRect(C2, P2, S2)) ? FMath::Max(8.f, (float)S2.X - 4.f) : 0.f;
			static const TCHAR* kVariants[] = {
				TEXT("Characteristic Info"), TEXT("Elements"), TEXT("Elemental"), TEXT("Element Info"), TEXT("Element"), TEXT("Attributes"),
				TEXT("Характеристики"), TEXT("Стихии"), TEXT("Стихийные"), TEXT("Стихия"),
				TEXT("Информация "), TEXT("Информация"), TEXT("Сведения"), TEXT("Основное"), TEXT("Общее") };
			for (const TCHAR* V : kVariants)
			{
				const float Wd = AsdaUi::MeasureTextWidth(V, Base, -1.f);
				UE_LOG(LogAsdaP16CharInfoTest, Log, TEXT("САМОТЕСТ-П16 замер «%s»: %.1f px при кегле %d (место %.1f) — %s"),
					V, Wd, (int32)Base.Size, Limit2, Wd <= Limit2 ? TEXT("влезает") : TEXT("НЕ влезает"));
			}
			Shot(TEXT("stat_tab_elements"));
		});
		Steps.Add([this](UWorld* W)
		{
			UAsdaCharaInfoSubsystem* C = CI(W); if (!C) { return; }
			C->SelectStatPage(0);
			Shot(TEXT("stat_tab_basic"));
			UE_LOG(LogAsdaP16CharInfoTest, Log, TEXT("САМОТЕСТ-П16 дамп окна 900:\n%s"), *C->DumpWidgets());
		});
	}

	void Finish(UWorld* W)
	{
		AsdaCVarSave::RestoreAll(AsdaP16CharInfoTestLocal::kWho);
		UGameInstance* GI = W ? W->GetGameInstance() : nullptr;
		if (UAsdaNetSubsystem* Net = GI ? GI->GetSubsystem<UAsdaNetSubsystem>() : nullptr)
		{
			if (CapGen) { AsdaSelfTestKit::DisarmCapture(Net, CapGen); CapGen = 0; }
			Net->SelfClanName = ClanWas;
			Net->bInGameConnected = bWasInGame;
		}
		if (UAsdaOtherCharaSubsystem* O = OC(W)) { O->CloseWindow(); }
		SelfScope.Reset();
		UAsdaCharaInfoSubsystem* C = CI(W);
		if (!C) { return; }
		if (!bWinWas) { C->CloseWindow(); } else { C->RefreshAll(); }
	}
};

namespace
{
	FAutoConsoleCommandWithWorldAndArgs GAsdaP16CharInfoSelfTest(
		TEXT("asda.CharInfo.Batch16SelfTest"),
		TEXT("asda.CharInfo.Batch16SelfTest [delay=N] [quit] — ОФЛАЙН-самотест пачки 16: «Информация о персонаже» по себе и соседу ")
		TEXT("(баг 4), подписи под-вкладок окна персонажа нормальным кеглем (баг 5), кадры RU/EN."),
		FConsoleCommandWithWorldAndArgsDelegate::CreateLambda([](const TArray<FString>& Args, UWorld* W)
		{
			const TCHAR* Who = AsdaP16CharInfoTestLocal::kWho;
			AsdaCVarSave::ArmWorldGuard(W, Who);
			const FAsdaSelfTestArgs KitArgs = FAsdaSelfTestArgs::Parse(Args);
			if (AsdaSelfTestKit::RefuseStart(W, Who, KitArgs)) { return; }
			float Delay = 0.f;
			Delay = AsdaSelfTestKit::DelayArgOr(Args, Delay);   // ★ R5-fix2-c (13.11.2026): `delay=N` — общая точка (был отдельный цикл по аргументам с одной этой веткой)
			const bool bQuit = Args.Contains(TEXT("quit"));
			// Офлайн своего павна нет — карта 3 и подставной свой персонаж (как `AsdaKeybindActionsSelfTest`), затем пауза на загрузку.
			if (AsdaSelfTestKit::NeedSelfSpawn(W) && GEngine)
			{
				GEngine->Exec(W, TEXT("Asda.Load.CurrentMap 3"));
				GEngine->Exec(W, TEXT("Asda.World.FakeSelfSpawn 256 256"));
				Delay = FMath::Max(Delay, 4.f);
			}
			TWeakObjectPtr<UWorld> WW(W);
			FTSTicker::GetCoreTicker().AddTicker(FTickerDelegate::CreateLambda([WW, bQuit, Who](float) -> bool
			{
				UWorld* W2 = WW.Get();
				if (!W2) { return false; }
				if (AsdaSelfTestKit::RefuseDeferred(W2, Who, bQuit)) { return false; }
				TSharedRef<FAsdaP16CharInfoSelfTest> T = MakeShared<FAsdaP16CharInfoSelfTest>();
				T->Setup(W2);
				T->BuildSteps();
				if (T->bSetupFailed) { T->Steps.Reset(); }
				TSharedRef<int32> Idx = MakeShared<int32>(0);
				FTSTicker::GetCoreTicker().AddTicker(FTickerDelegate::CreateLambda([WW, T, Idx, bQuit, Who](float) -> bool
				{
					UWorld* W3 = WW.Get();
					if (!W3) { AsdaCVarSave::RestoreAll(Who); return false; }
					if (*Idx < T->Steps.Num() && !T->bAbort) { T->Steps[(*Idx)++](W3); return true; }
					T->Finish(W3);
					UE_LOG(LogAsdaP16CharInfoTest, Log, TEXT("САМОТЕСТ-П16 ИТОГ: ok=%d fail=%d"), T->Ok, T->Fail);
					AsdaSelfTestKit::Quit(W3, bQuit);
					return false;
				}), 0.6f);
				return false;
			}), Delay);
			UE_LOG(LogAsdaP16CharInfoTest, Log, TEXT("САМОТЕСТ-П16: запуск через %.1f с"), Delay);
		}));
}

#endif // !UE_BUILD_SHIPPING
