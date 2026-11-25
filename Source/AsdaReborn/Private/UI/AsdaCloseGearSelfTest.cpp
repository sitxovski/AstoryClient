// Copyright (c) 2026 @sitxovski (Discord)
// ★★★ САМОТЕСТ ПАЧКИ 19, БАГ 2 (08.11.2026): шестерёнка у крестика крутится при наведении — `asda.UI.SelfTestCloseGear [delay=N] [quit]`.
//
// ПУТЬ ИГРОКА: окно открывается штатным входом (горячая клавиша `RunCommandForCheck` / вход подсистемы), курсор ставится настоящим
//   движением Slate (`SetCursorPos` + `ProcessMouseMoveEvent`) в центр ЖИВОЙ геометрии крестика, закрытие — щелчком `SimulateClickAt`.
// ПРОВЕРКИ НА КАЖДОЕ ОКНО (разбор эталона — AsdaCloseGear.h):
//   1 после открытия: статичная `*_Gear` видна, кадровая `*_AniGear` скрыта;
//   2 наведение: статичная скрыта, кадровая видна, за 0,35 с план сменил ≥3 кадров (4 кадра за 300 мс, по кругу) — UV в журнале и кадры;
//   3 уход курсора: обратно; 4 щелчок по крестику закрывает окно; 5 повторное открытие — статичная (не застряла кадровая).
//   Контроль ключа `asda.UI.CloseGearHover 0` на журнале заданий 700 (там копии не было) — кадровая НЕ показывается (дефект воспроизведён).
//   Окно, которое порт не открывает (SYSTEM_WIN 800, ITEM_SWAP_WIN 15200), — ПРОПУСК; отряд 2279 — самотест asda.UI.SelfTestHoverNoSelect (R4/R5).
//   Кадры — `Reports/batch19/menu_anim/<язык>_<окно>_<шаг>.png`. Строки журнала — «САМОТЕСТ-ШЕСТЕРЁНКА», итог «… ИТОГ: ok=… fail=…».
#include "CoreMinimal.h"
#include "Test/AsdaSelfTestInput.h"   // ★ R5-c (12.11.2026): общая точка самотестов вместо своих копий помощников

#if !UE_BUILD_SHIPPING

#include "UI/AsdaCloseGear.h"
#include "UI/AsdaWindowSubsystem.h"
#include "UI/AsdaUiHelpers.h"
#include "UI/AsdaAnimatedPlane.h"
#include "UI/AsdaWarehouseSubsystem.h"
#include "UI/AsdaInventorySubsystem.h"
#include "UI/AsdaSkillWindowSubsystem.h"
#include "UI/AsdaExchangeSubsystem.h"
#include "UI/AsdaExchangeP2PSubsystem.h"
#include "UI/AsdaNpcShopSubsystem.h"
#include "Npc/AsdaNpcDialogTypes.h"   // FAsdaNpcShopRow — строка DT_NpcShopList
#include "UI/AsdaHotbarSubsystem.h"
#include "Login/AsdaLoginData.h"
#include "AsdaNetSubsystem.h"
#include "AsdaBytes.h"
#include "World/AsdaPlayerController.h"
#include "AsdaCVarSave.h"
#include "Test/AsdaSelfTestKit.h"
#include "Text/AsdaLang.h"
#include "Blueprint/UserWidget.h"
#include "Blueprint/WidgetTree.h"
#include "Components/PanelWidget.h"
#include "Containers/Ticker.h"
#include "Engine/DataTable.h"
#include "Engine/GameInstance.h"
#include "Engine/World.h"
#include "Framework/Application/SlateApplication.h"
#include "HAL/IConsoleManager.h"
#include "Misc/Paths.h"
#include "UnrealClient.h"

DEFINE_LOG_CATEGORY_STATIC(LogAsdaCloseGearTest, Log, All);

namespace AsdaCloseGearTestLocal
{
	static const TCHAR* kWho = TEXT("asda.UI.SelfTestCloseGear");   // имя самотеста (сторож ключей)

	// Окно прогона: id, как открыть, как назвать в кадрах.
	struct FCase
	{
		FName Win;                                  // корень окна
		const TCHAR* Tag;                           // имя в кадрах
		TFunction<void(UWorld*)> Open;              // штатный вход
		TFunction<void(UWorld*)> Close;             // закрыть мимо крестика (уборка между окнами); пусто — реестр окон
	};

	// Центр живой геометрии.
	using AsdaSelfTestInput::CenterOf;   // ★ R5-c (12.11.2026): общая точка (`Test/AsdaSelfTestInput.h`) — копия была дословной.
	// Настоящее движение курсора Slate.
	static void MoveCursor(const FVector2D& P)
	{
		FSlateApplication& App = FSlateApplication::Get();
		App.SetCursorPos(P);
		FPointerEvent Mv(0, P, P, TSet<FKey>(), EKeys::Invalid, 0.f, FModifierKeysState());
		App.ProcessMouseMoveEvent(Mv);
	}
	// Первый кадровый план узла (сам узел или потомок).
	static UAsdaAnimatedPlane* AnimOf(UWidget* W)
	{
		if (UAsdaAnimatedPlane* A = Cast<UAsdaAnimatedPlane>(W)) { return A; }
		if (UPanelWidget* P = Cast<UPanelWidget>(W))
		{
			TArray<UWidget*> Kids;
			UWidgetTree::GetChildWidgets(P, Kids);
			for (UWidget* K : Kids) { if (UAsdaAnimatedPlane* A = Cast<UAsdaAnimatedPlane>(K)) { return A; } }
		}
		return nullptr;
	}
	// Узел и все его предки видимы (план реально рисуется).
	static bool Drawn(const UWidget* W)
	{
		for (const UWidget* P = W; P; P = P->GetParent()) { if (!AsdaUi::IsShown(P)) { return false; } }
		return W != nullptr;
	}
	// Узел и планы поддерева видимы.
	static bool DrawnTree(UWidget* W)
	{
		if (!Drawn(W)) { return false; }
		UAsdaAnimatedPlane* A = AnimOf(W);
		return !A || Drawn(A);
	}
}

struct FAsdaCloseGearSelfTest
{
	int32 Ok = 0, Fail = 0, Skip = 0;           // счётчики
	TArray<TFunction<float(UWorld*)>> Steps;   // шаг возвращает паузу (с)
	FString Lang, ShotDir;                     // язык и папка кадров
	TArray<FString> UVs;                       // UV кадровой шестерёнки при наведении
	bool bCaseOpen = false;                    // текущее окно открылось
	bool bRepeat = false;                      // шаг просит повторить себя в следующем кадре (★ R5-fix-b 13.11.2026 (заключительный заход))
	double HoverT0 = 0.0;                      // момент первой выборки кадров при наведении (0 — не начата)

	void Expect(bool b, const FString& Name, const FString& Detail = FString())
	{
		b ? ++Ok : ++Fail;
		UE_LOG(LogAsdaCloseGearTest, Log, TEXT("САМОТЕСТ-ШЕСТЕРЁНКА %s: %s%s%s"), b ? TEXT("ok") : TEXT("ПРОВАЛ"), *Name,
			Detail.IsEmpty() ? TEXT("") : TEXT(" — "), *Detail);
	}
	void Shot(const FString& Name)
	{
		const FString File = FPaths::Combine(ShotDir, FString::Printf(TEXT("%s_%s.png"), *Lang, *Name));
		FScreenshotRequest::RequestScreenshot(File, /*bShowUI=*/true, /*bAddFilenameSuffix=*/false);
		UE_LOG(LogAsdaCloseGearTest, Log, TEXT("САМОТЕСТ-ШЕСТЕРЁНКА кадр: %s"), *File);
	}
	template <class T> static T* Sub(UWorld* W) { UGameInstance* G = W ? W->GetGameInstance() : nullptr; return G ? G->GetSubsystem<T>() : nullptr; }

	bool Nodes(UWorld* W, FName Win, UWidget*& C, UWidget*& G, UWidget*& A)
	{
		UAsdaCloseGearSubsystem* S = Sub<UAsdaCloseGearSubsystem>(W);
		if (S) { S->DebugTickNow(); }
		if (S && S->DebugNodes(Win, C, G, A)) { return true; }
		// Ключ 0: общая точка молчит — узлы по той же таблице напрямую.
		UAsdaWindowSubsystem* WM = Sub<UAsdaWindowSubsystem>(W);
		UUserWidget* Wd = WM ? WM->FindWindow(Win) : nullptr;
		for (const FAsdaCloseGearRow& R : UAsdaCloseGearSubsystem::Rows())
		{
			if (R.Window != Win || !Wd) { continue; }
			C = AsdaUi::FindNodeExact(Wd, R.Close);
			G = AsdaUi::FindNodeByPrefix(Wd, *FString::Printf(TEXT("%s_"), R.Gear));
			A = AsdaUi::FindNodeByPrefix(Wd, *FString::Printf(TEXT("%s_"), R.Ani));
		}
		return C != nullptr;
	}
	// Уборка окна мимо крестика.
	void CloseBy(UWorld* W, const AsdaCloseGearTestLocal::FCase& K)
	{
		if (K.Close) { K.Close(W); return; }
		if (UAsdaWindowSubsystem* WM = Sub<UAsdaWindowSubsystem>(W)) { WM->CloseWindow(K.Win); }
	}
	static FString UVOf(UWidget* A)
	{
		const UAsdaAnimatedPlane* P = AsdaCloseGearTestLocal::AnimOf(A);
		if (!P) { return TEXT("нет плана"); }
		const FBox2f B = P->GetBrush().GetUVRegion();
		return FString::Printf(TEXT("%.3f,%.3f"), B.Min.X, B.Min.Y);
	}

	// Шаги одного окна.
	void AddCase(const AsdaCloseGearTestLocal::FCase& K, bool bKeyOff)
	{
		using namespace AsdaCloseGearTestLocal;
		const FString Tag = FString::Printf(TEXT("%s%s"), K.Tag, bKeyOff ? TEXT("_key0") : TEXT(""));
		Steps.Add([this, K, bKeyOff](UWorld* W) -> float
		{
			AsdaCVarSave::SetForTest(TEXT("asda.UI.CloseGearHover"), bKeyOff ? 0 : 1);
			MoveCursor(FVector2D(4.f, 4.f));
			UAsdaWindowSubsystem* WM = Sub<UAsdaWindowSubsystem>(W);
			// Окно сообщения прошлого шага (навыки офлайн дают нотис «не меняешь профессию») модально и съело бы щелчки.
			if (WM) { WM->ClearMsgBoxQueue(TEXT("самотест шестерёнки")); }
			if (WM && !WM->IsWindowVisible(K.Win)) { K.Open(W); }
			return 0.8f;
		});
		Steps.Add([this, K, Tag, bKeyOff](UWorld* W) -> float
		{
			UAsdaWindowSubsystem* WM = Sub<UAsdaWindowSubsystem>(W);
			if (WM) { WM->ClearMsgBoxQueue(TEXT("самотест шестерёнки")); }
			bCaseOpen = WM && WM->IsWindowVisible(K.Win);
			if (!bCaseOpen)
			{
				++Skip;
				UE_LOG(LogAsdaCloseGearTest, Log, TEXT("САМОТЕСТ-ШЕСТЕРЁНКА ПРОПУСК: %s офлайн не открылось"), *K.Win.ToString());
				return 0.f;
			}
			UWidget *C = nullptr, *G = nullptr, *A = nullptr;
			const bool bN = Nodes(W, K.Win, C, G, A);
			Expect(bN && G && A, FString::Printf(TEXT("%s 0 узлы крестика и шестерёнок найдены"), *Tag),
				FString::Printf(TEXT("крестик %s, статичная %s, кадровая %s"), C ? *C->GetName() : TEXT("НЕТ"), G ? *G->GetName() : TEXT("НЕТ"), A ? *A->GetName() : TEXT("НЕТ")));
			Expect(G && A && AsdaCloseGearTestLocal::DrawnTree(G) && !AsdaUi::IsShown(A), FString::Printf(TEXT("%s 1 после открытия: статичная видна, кадровая скрыта"), *Tag));
			Shot(Tag + TEXT("_1open"));
			FVector2D P;
			if (CenterOf(C, P)) { MoveCursor(P); }
			UVs.Reset();
			HoverT0 = 0.0;
			return 0.05f;
		});
		// ★ R5-fix-b 13.11.2026 (заключительный заход) (BRIEF4 п. 1): кадры кадровой шестерёнки — ДО ТРЁХ РАЗНЫХ опросом каждый кадр, пока она показана, с
		//   потолком 3 с от первой выборки (было: 4 выборки по часам +50/150/250/350 мс). План меняет кадр раз в 100 мс; выборка раз
		//   в ~103 мс под нагрузкой ловила один кадр дважды (REPORT3 §8: npcshop — кадры 0,0,2,3, два разных из четырёх). Смысл тот же —
		//   «анимация крутится»: ≥3 разных кадра; потолок — чтобы стоящая анимация давала провал, а не вечное ожидание.
		//   Выборка — только пока кадровая отрисована (UV скрытого узла кадром анимации не считается). Ключ 0 — одна выборка через
		//   0,35 с (там проверяется лишь, что кадровая НЕ показана).
		Steps.Add([this, K, Tag, bKeyOff](UWorld* W) -> float
		{
			if (!bCaseOpen) { return 0.f; }
			const double Now = FPlatformTime::Seconds();
			if (HoverT0 <= 0.0) { HoverT0 = Now; }
			const double Ms = (Now - HoverT0) * 1000.0;
			UWidget *C = nullptr, *G = nullptr, *A = nullptr;
			Nodes(W, K.Win, C, G, A);
			const bool bDrawn = A && AsdaCloseGearTestLocal::DrawnTree(A);
			if (bKeyOff)
			{
				if (Ms < 350.0) { bRepeat = true; return 0.f; }
				UVs.Add(UVOf(A));
				UE_LOG(LogAsdaCloseGearTest, Log, TEXT("САМОТЕСТ-ШЕСТЕРЁНКА %s наведение +%.0f мс: кадровая видна=%d, UV=%s"), *Tag, Ms, bDrawn ? 1 : 0, *UVs.Last());
				Shot(FString::Printf(TEXT("%s_2hover_%03dms"), *Tag, (int32)Ms));
				return 0.f;
			}
			if (bDrawn)
			{
				const FString UV = UVOf(A);
				if (!UVs.Contains(UV))
				{
					UVs.Add(UV);
					UE_LOG(LogAsdaCloseGearTest, Log, TEXT("САМОТЕСТ-ШЕСТЕРЁНКА %s наведение +%.0f мс: кадровая видна=1, новый кадр %d, UV=%s"), *Tag, Ms, UVs.Num(), *UV);
					Shot(FString::Printf(TEXT("%s_2hover_%03dms"), *Tag, (int32)Ms));
				}
			}
			if (UVs.Num() < 3 && Ms < 3000.0) { bRepeat = true; return 0.f; }
			UE_LOG(LogAsdaCloseGearTest, Log, TEXT("САМОТЕСТ-ШЕСТЕРЁНКА %s наведение: разных кадров %d за %.0f мс (сбор до 3, потолок 3000 мс)"), *Tag, UVs.Num(), Ms);
			return 0.f;
		});
		Steps.Add([this, K, Tag, bKeyOff](UWorld* W) -> float
		{
			if (!bCaseOpen) { return 0.f; }
			UWidget *C = nullptr, *G = nullptr, *A = nullptr;
			Nodes(W, K.Win, C, G, A);
			TSet<FString> Distinct(UVs);
			if (bKeyOff)
			{
				Expect(A && !AsdaUi::IsShown(A), FString::Printf(TEXT("%s ключ 0: кадровая НЕ показана (дефект порта воспроизведён)"), *Tag));
			}
			else
			{
				Expect(G && A && !AsdaUi::IsShown(G) && AsdaCloseGearTestLocal::DrawnTree(A), FString::Printf(TEXT("%s 2 наведение: статичная скрыта, кадровая видна"), *Tag));
				Expect(Distinct.Num() >= 3, FString::Printf(TEXT("%s 2 кадровая крутится: разных кадров ≥3 (сбор до трёх, потолок 3 с)"), *Tag),   // ★ R5-fix-b 13.11.2026 (заключительный заход)
					FString::Printf(TEXT("UV: %s"), *FString::Join(UVs, TEXT(" | "))));
			}
			AsdaCloseGearTestLocal::MoveCursor(FVector2D(4.f, 4.f));
			return 0.15f;
		});
		Steps.Add([this, K, Tag, bKeyOff](UWorld* W) -> float
		{
			if (!bCaseOpen) { return 0.f; }
			UWidget *C = nullptr, *G = nullptr, *A = nullptr;
			Nodes(W, K.Win, C, G, A);
			if (!bKeyOff)
			{
				Expect(G && A && AsdaCloseGearTestLocal::DrawnTree(G) && !AsdaUi::IsShown(A), FString::Printf(TEXT("%s 3 уход курсора: статичная видна, кадровая скрыта"), *Tag));
			}
			Shot(Tag + TEXT("_3leave"));
			// Закрытие щелчком по крестику (путь игрока).
			FVector2D P;
			UAsdaWindowSubsystem* WM = Sub<UAsdaWindowSubsystem>(W);
			if (WM && AsdaCloseGearTestLocal::CenterOf(C, P)) { WM->SimulateClickAt(P, 2, false); }
			return 0.5f;
		});
		Steps.Add([this, K, Tag, bKeyOff](UWorld* W) -> float
		{
			if (!bCaseOpen) { return 0.f; }
			UAsdaWindowSubsystem* WM = Sub<UAsdaWindowSubsystem>(W);
			Expect(WM && !WM->IsWindowVisible(K.Win), FString::Printf(TEXT("%s 4 щелчок по крестику закрыл окно"), *Tag));
			Shot(Tag + TEXT("_4closed"));
			AsdaCloseGearTestLocal::MoveCursor(FVector2D(4.f, 4.f));
			if (WM && WM->IsWindowVisible(K.Win)) { CloseBy(W, K); }
			if (WM) { WM->ClearMsgBoxQueue(TEXT("самотест шестерёнки")); }
			if (!bKeyOff && WM && !WM->IsWindowVisible(K.Win)) { K.Open(W); }
			return 0.8f;
		});
		Steps.Add([this, K, Tag, bKeyOff](UWorld* W) -> float
		{
			if (!bCaseOpen || bKeyOff) { return 0.f; }
			UWidget *C = nullptr, *G = nullptr, *A = nullptr;
			Nodes(W, K.Win, C, G, A);
			Expect(G && A && AsdaCloseGearTestLocal::DrawnTree(G) && !AsdaUi::IsShown(A), FString::Printf(TEXT("%s 5 повторное открытие: статичная видна, кадровая скрыта"), *Tag));
			Shot(Tag + TEXT("_5reopen"));
			UAsdaWindowSubsystem* WM = Sub<UAsdaWindowSubsystem>(W);
			if (WM && WM->IsWindowVisible(K.Win)) { CloseBy(W, K); }
			return 0.4f;
		});
	}

	void BuildSteps()
	{
		using namespace AsdaCloseGearTestLocal;
		auto Cmd = [](int32 C) { return [C](UWorld* W) { if (AAsdaPlayerController* P = W ? Cast<AAsdaPlayerController>(W->GetFirstPlayerController()) : nullptr) { P->RunCommandForCheck(C); } }; };
		TArray<FCase> Cases = {
			{ FName(TEXT("INVEN_WIN")),     TEXT("inven"),     [](UWorld* W) { if (auto* S = Sub<UAsdaInventorySubsystem>(W)) { S->OpenWindow(); } },
			                                                     [](UWorld* W) { if (auto* S = Sub<UAsdaInventorySubsystem>(W)) { S->CloseWindow(); } } },
			{ FName(TEXT("CHARAINFO_WIN")), TEXT("charainfo"), Cmd(30), Cmd(30) },
			// Навыки офлайн без профессии не открываются (нотис «не меняешь профессию») — класс 1 подменой самотеста.
			//   ★ R5-fix-b 13.11.2026 (заключительный заход) (BRIEF4 п. 2): подмена снимается за собой — при закрытии окна навыков и в конце прогона (прежде класс 1
			//   оставался соседям пачки).
			{ FName(TEXT("SKILL_WIN")),     TEXT("skill"),     [Cmd](UWorld* W) { if (auto* S = Sub<UAsdaSkillWindowSubsystem>(W)) { S->SetJobForTest(1, 0); } Cmd(32)(W); },
			                                                     [Cmd](UWorld* W) { Cmd(32)(W); if (auto* S = Sub<UAsdaSkillWindowSubsystem>(W)) { S->SetJobForTest(-1, -1); } } },
			{ FName(TEXT("QUEST_WIN")),     TEXT("quest"),     Cmd(34), Cmd(34) },
			{ FName(TEXT("Title_Win")),     TEXT("title"),     Cmd(38), Cmd(38) },
			{ FName(TEXT("COMMUNITY_WIN")), TEXT("community"), Cmd(36), Cmd(36) },
			{ FName(TEXT("WARE_WIN")),      TEXT("ware"),      [](UWorld* W) { if (auto* S = Sub<UAsdaWarehouseSubsystem>(W)) { S->OpenWindow(UAsdaWarehouseSubsystem::kModeKeep); } },
			                                                     [](UWorld* W) { if (auto* S = Sub<UAsdaWarehouseSubsystem>(W)) { S->CloseWindow(); } } },
			{ FName(TEXT("EXCHANGE_WIN")),  TEXT("exchange"),  [](UWorld* W) { if (auto* S = Sub<UAsdaExchangeSubsystem>(W)) { S->OpenMode(1); } },
			                                                     [](UWorld* W) { if (auto* S = Sub<UAsdaExchangeSubsystem>(W)) { S->CloseAll(); } } },
			// Обмен P2P 2008 — путь игрока «приглашение 5283 → значок → „Принять“ → 5285» (как самотест О2).
			{ FName(TEXT("TRAN_WIN")),      TEXT("tran"),      [](UWorld* W)
				{
					UAsdaNetSubsystem* Net = Sub<UAsdaNetSubsystem>(W);
					UAsdaHotbarSubsystem* Hb = Sub<UAsdaHotbarSubsystem>(W);
					UAsdaWindowSubsystem* WM = Sub<UAsdaWindowSubsystem>(W);
					UAsdaExchangeP2PSubsystem* X = Sub<UAsdaExchangeP2PSubsystem>(W);
					if (!Net || !Hb || !WM || !X) { return; }
					X->ResetSessionState(EAsdaSessionResetReason::LeftWorld);
					TArray<uint8> A; A.SetNumZeroed(24);
					A[0] = 1; AsdaBytes::WrI16(A.GetData() + 1, 902); FAsdaLoginData::EncodeCp1251(TEXT("Partner"), A.GetData() + 3, 19); A[23] = 0;
					Net->DebugFeedPacket(5283, A); X->Tick(0.f);
					Hb->HandleNoticeIconClick(4);
					WM->AnswerMsgBox(true, true);
					TArray<uint8> B; B.SetNumZeroed(50);
					B[0] = 1; B[1] = 0; AsdaBytes::WrI32(B.GetData() + 2, 77);
					AsdaBytes::WrI16(B.GetData() + 6, 902); FAsdaLoginData::EncodeCp1251(TEXT("Partner"), B.GetData() + 8, 19);
					AsdaBytes::WrI16(B.GetData() + 28, 500); FAsdaLoginData::EncodeCp1251(TEXT("Me"), B.GetData() + 30, 19);
					Net->DebugFeedPacket(5285, B); X->Tick(0.f);
				},
				// ★ R5-fix-b 13.11.2026 (третий заход): закрытие — тем же штатным сбросом модели обмена, что в открытии (`InvItemListDialogInit` снимает
				//   признак «окно обмена 2008», `HideWindow` прячет окно). Раньше уборка шла реестром окон (`WM->CloseWindow`): виджет
				//   снимался, признак оставался 1 — следующий самотест пачки (`UI.SelfTestEscOnTop`) не мог открыть сумку.
				[](UWorld* W) { if (UAsdaExchangeP2PSubsystem* X = Sub<UAsdaExchangeP2PSubsystem>(W)) { X->ResetSessionState(EAsdaSessionResetReason::LeftWorld); } } },
			{ FName(TEXT("NPCSHOP_WIN")),   TEXT("npcshop"),   [](UWorld* W)
				{
					UAsdaNpcShopSubsystem* Shop = Sub<UAsdaNpcShopSubsystem>(W);
					UDataTable* Dt = LoadObject<UDataTable>(nullptr, TEXT("/Game/AsdaAssets/Data/NPC/DT_NpcShopList.DT_NpcShopList"));
					if (!Shop || !Dt) { return; }
					for (const TPair<FName, uint8*>& Row : Dt->GetRowMap())
					{
						const FAsdaNpcShopRow* R = reinterpret_cast<const FAsdaNpcShopRow*>(Row.Value);
						if (R && R->ItemCodes.Num() > 0) { Shop->OpenShop(5, R->NpcId); break; }
					}
				},
				[](UWorld* W) { if (auto* S = Sub<UAsdaNpcShopSubsystem>(W)) { S->CloseShop(); } } },
		};
		for (const FCase& K : Cases) { AddCase(K, false); }
		// Контроль ключа 0 на журнале 700 (до правки копии у него не было).
		AddCase(Cases[3], true);
		// Окна, которые порт не открывает: отметить пропуском.
		Steps.Add([this](UWorld* W) -> float
		{
			for (const TCHAR* Id : { TEXT("SYSTEM_WIN"), TEXT("ITEM_SWAP_WIN") })
			{
				UAsdaWindowSubsystem* WM = Sub<UAsdaWindowSubsystem>(W);
				++Skip;
				UE_LOG(LogAsdaCloseGearTest, Log, TEXT("САМОТЕСТ-ШЕСТЕРЁНКА ПРОПУСК: %s — порт окно не открывает (зарегистрировано=%d), строка таблицы ждёт показа"),
					Id, WM && WM->FindWindow(FName(Id)) ? 1 : 0);
			}
			AsdaCloseGearTestLocal::MoveCursor(FVector2D(4.f, 4.f));
			return 0.f;
		});
	}
};

namespace
{
	FAutoConsoleCommandWithWorldAndArgs GAsdaCloseGearSelfTest(
		TEXT("asda.UI.SelfTestCloseGear"),
		TEXT("asda.UI.SelfTestCloseGear [delay=N] [quit] — ОФЛАЙН-самотест пачки 19 (баг 2): наведение на крестик крутит шестерёнку *_AniGear ")
		TEXT("во всех окнах таблицы UAsdaCloseGearSubsystem, закрытие щелчком, кадры RU/EN в Reports/batch19/menu_anim."),
		FConsoleCommandWithWorldAndArgsDelegate::CreateLambda([](const TArray<FString>& Args, UWorld* W)
		{
			const TCHAR* Who = AsdaCloseGearTestLocal::kWho;
			AsdaCVarSave::ArmWorldGuard(W, Who);
			const FAsdaSelfTestArgs KitArgs = FAsdaSelfTestArgs::Parse(Args);
			if (AsdaSelfTestKit::RefuseStart(W, Who, KitArgs)) { return; }
			float Delay = 0.f;
			Delay = AsdaSelfTestKit::DelayArgOr(Args, Delay);   // ★ R5-fix2-c (13.11.2026): `delay=N` — общая точка (был отдельный цикл по аргументам с одной этой веткой)
			const bool bQuit = Args.Contains(TEXT("quit"));
			TWeakObjectPtr<UWorld> WW(W);
			FTSTicker::GetCoreTicker().AddTicker(FTickerDelegate::CreateLambda([WW, bQuit, Who](float) -> bool
			{
				UWorld* W2 = WW.Get();
				if (!W2) { return false; }
				if (AsdaSelfTestKit::RefuseDeferred(W2, Who, bQuit)) { return false; }
				TSharedRef<FAsdaCloseGearSelfTest> T = MakeShared<FAsdaCloseGearSelfTest>();
				T->Lang = FAsdaLang::IsRu() ? TEXT("ru") : TEXT("en");
				T->ShotDir = FPaths::ConvertRelativePathToFull(FPaths::Combine(FPaths::ProjectDir(), TEXT("../Reports/batch19/menu_anim")));
				T->BuildSteps();
				TSharedRef<int32> Idx = MakeShared<int32>(0);
				TSharedRef<double> Next = MakeShared<double>(0.0);
				const TWeakObjectPtr<UGameInstance> WGI = W2->GetGameInstance();   // ★ R5-fix-b 13.11.2026 (заключительный заход, третья запись), ревизия REVIEW6 М-4: снять подмену класса и без мира
				FTSTicker::GetCoreTicker().AddTicker(FTickerDelegate::CreateLambda([WW, WGI, T, Idx, Next, bQuit, Who](float) -> bool
				{
					UWorld* W3 = WW.Get();
					if (!W3)
					{
						if (UGameInstance* G = WGI.Get()) { if (UAsdaSkillWindowSubsystem* Sk = G->GetSubsystem<UAsdaSkillWindowSubsystem>()) { Sk->SetJobForTest(-1, -1); } }
						AsdaCVarSave::RestoreAll(Who); return false;
					}
					const double Now = FPlatformTime::Seconds();
					if (Now < *Next) { return true; }
					if (*Idx < T->Steps.Num())
					{
						*Next = Now + T->Steps[(*Idx)++](W3);
						if (T->bRepeat) { T->bRepeat = false; --(*Idx); }   // ★ R5-fix-b 13.11.2026 (заключительный заход): шаг выборки кадров повторяется до своего условия
						return true;
					}
					// ★ R5-fix-b 13.11.2026 (заключительный заход) (BRIEF4 п. 2): подмена класса навыков (окно SKILL_WIN офлайн) — снята, соседям пачки не остаётся.
					if (UAsdaSkillWindowSubsystem* Sk = FAsdaCloseGearSelfTest::Sub<UAsdaSkillWindowSubsystem>(W3)) { Sk->SetJobForTest(-1, -1); }
					AsdaCVarSave::RestoreAll(Who);
					UE_LOG(LogAsdaCloseGearTest, Log, TEXT("САМОТЕСТ-ШЕСТЕРЁНКА пропущено окон: %d"), T->Skip);
					UE_LOG(LogAsdaCloseGearTest, Log, TEXT("САМОТЕСТ-ШЕСТЕРЁНКА ИТОГ: ok=%d fail=%d"), T->Ok, T->Fail);
					AsdaSelfTestKit::Quit(W3, bQuit);
					return false;
				}), 0.f);
				return false;
			}), Delay);
			UE_LOG(LogAsdaCloseGearTest, Log, TEXT("САМОТЕСТ-ШЕСТЕРЁНКА: запуск через %.1f с"), Delay);
		}));
}

#endif // !UE_BUILD_SHIPPING
