// Copyright (c) 2026 @sitxovski (Discord)
// ★★★ САМОТЕСТ «ВКЛАДКА СУМКИ» (07.11.2026, пачка 12, баг 7 владельца): `asda.SelfTest.InvTab [quit]`.
//   Жалоба: снятие снаряжения не открывает вкладку «снаряжение/премиум». Просьба: помнить последнюю выбранную вкладку.
//   Эталон: нажатие (0xB) по непустому надетому слоту → `uiEquipSlotSelect@0xC6A860`: 5/6/7/10 → вкладка 0, иначе → 1; пустой слот вкладку
//   не меняет; 5001 вкладку не трогает. Снятие: двойной щелчок (0xD) и бросок захвата на сетку (`itemUiHandleControlEvent@0xC6BCA0`).
//   Путь игрока: настоящие щелчки и перетаскивание Slate по живой геометрии, ответы 5001 — штатный приём `DebugFeedPacket`, офлайн.
//   Разделы: K ключ 0 → вкладка не меняется; S таблица слотов; D снятие двойным щелчком (броня, оружие, кольцо); F полная сумка (5001 код 0);
//   G снятие броском на сетку, бросок мимо и на манекен ничего не делают, ключ 0 → захвата нет; M память вкладки: окно, ключ 0, автопереход
//   важнее памяти и её не переписывает, сброс сессии, запись на диск. Кадры — `Reports/inv_tab/2026-11-07/frames/<язык>_<шаг>.png`.
//   Итог: «ВКЛАДКА-СУМКИ ИТОГ: ok=N fail=M». Сумка, надетое, ключи и запись настроек возвращаются в конце.
#include "CoreMinimal.h"
#include "AsdaDevConsole.h"
#include "Test/AsdaSelfTestInput.h"   // ★ R5-c (12.11.2026): общая точка самотестов вместо своих копий помощников

#if ASDA_DEV_CONSOLE && !UE_BUILD_SHIPPING

#include "AsdaNetSubsystem.h"
#include "AsdaCVarSave.h"
#include "Test/AsdaSelfTestKit.h"
#include "Text/AsdaLang.h"
#include "UI/AsdaInventorySubsystem.h"
#include "UI/AsdaShopDblClick.h"
#include "UI/AsdaUiHelpers.h"
#include "UI/AsdaWindowSubsystem.h"
#include "Blueprint/UserWidget.h"
#include "Components/Widget.h"
#include "Engine/Engine.h"
#include "Engine/GameInstance.h"
#include "Engine/GameViewportClient.h"
#include "Engine/World.h"
#include "Framework/Application/SlateApplication.h"
#include "Misc/ConfigCacheIni.h"
#include "Misc/FileHelper.h"
#include "HAL/FileManager.h"
#include "Misc/Paths.h"
#include "TimerManager.h"
#include "UnrealClient.h"

DEFINE_LOG_CATEGORY_STATIC(LogAsdaInvTabTest, Log, All);

namespace AsdaInvTabTest
{
	static const TCHAR* kCmd = TEXT("asda.SelfTest.InvTab");      // имя команды
	static const TCHAR* kPfx = TEXT("ВКЛАДКА-СУМКИ");             // префикс строк и итога
	static const FName kWinId(TEXT("INVEN_WIN"));                 // окно сумки
	static const TCHAR* kSect = TEXT("AsdaInventory");            // секция настроек
	static const TCHAR* kKey  = TEXT("LastTab");                  // ключ вкладки
	static constexpr float kStep = 0.5f;                          // шаг сценария, с

	// Надетые вещи: слот → код (слот контейнера 3 = itemType; 5/6 — кольца).
	struct FWorn { int32 Slot; int32 Code; };
	static const FWorn kWorn[] = { {0, 20556}, {1, 20552}, {2, 20554}, {3, 20555}, {5, 20679}, {6, 20679}, {7, 24781}, {9, 22818}, {10, 69671} };

	// Контекст прогона (живёт между таймерами).
	struct FCtx
	{
		int32 Ok = 0, Fail = 0;                    // пройдено / провалено
		TWeakObjectPtr<UWorld> W;                  // мир прогона
		bool bQuit = false;                        // выйти после итога
		FString FrameDir;                          // папка кадров
		float T = 0.f;                             // время следующего шага
		TArray<TArray<uint8>> SavedBag, SavedWorn, SavedSpend;   // сырые записи контейнеров 1, 3, 2 до прогона (пусто — ячейка пуста)
		TSharedPtr<FAsdaNetSelfScope> Self;        // свой персонаж — вернуть в конце
		uint32 CapGen = 0;                         // поколение перехватчика исходящих
		TSharedRef<TArray<uint16>> Sent = MakeShared<TArray<uint16>>();   // перехваченные опкоды
		bool bKeep = false;                        // аргумент `keep`: оставить на диске LastTab=1 для второго процесса
		bool bHadIni = false;                      // была ли запись вкладки до прогона
		int32 IniWas = 0;                          // её значение
		int32 Unequip0 = 0;                        // счётчик снятий на начало шага

		void Expect(bool b, const FString& What)
		{
			if (b) { ++Ok;   UE_LOG(LogAsdaInvTabTest, Log,   TEXT("%s ok   %s"), kPfx, *What); }
			else   { ++Fail; UE_LOG(LogAsdaInvTabTest, Error, TEXT("%s FAIL %s"), kPfx, *What); }
		}
		UGameInstance* GI() const { return W.IsValid() ? W->GetGameInstance() : nullptr; }
		template <class T2> T2* Sub() const { UGameInstance* G = GI(); return G ? G->GetSubsystem<T2>() : nullptr; }
		UAsdaInventorySubsystem* Inv() const { return Sub<UAsdaInventorySubsystem>(); }
		int32 SentCount(uint16 Op) const { int32 N = 0; for (uint16 O : *Sent) { if (O == Op) { ++N; } } return N; }
	};
	using FCtxRef = TSharedRef<FCtx>;

	// Следующий шаг сценария (через kStep после предыдущего).
	static void Step(const FCtxRef& C, TFunction<void()> Fn, float Wait = kStep)
	{
		UWorld* W = C->W.Get();
		if (!W) { return; }
		C->T += Wait;
		FTimerHandle H;
		W->GetTimerManager().SetTimer(H, FTimerDelegate::CreateLambda(MoveTemp(Fn)), C->T, false);
	}
	// Центр живой геометрии узла (false — не отрисован / скрыт).
	static bool Center(const UWidget* Wd, FVector2D& Out)
	{
		FVector2D Mn, Mx;
		if (!Wd || !AsdaUi::WidgetAbsRect(Wd, Mn, Mx) || (Mx - Mn).X < 2.0 || (Mx - Mn).Y < 2.0) { return false; }
		Out = (Mn + Mx) * 0.5f;
		return true;
	}
	// Курсор в точку + событие движения.
	using AsdaSelfTestInput::Hover;   // ★ R5-c (12.11.2026): общая точка (`Test/AsdaSelfTestInput.h`) — копия была дословной.
	// Абсолютная точка внутри вьюпорта (доли ширины и высоты).
	static FVector2D ViewPoint(float Fx, float Fy)
	{
		if (!GEngine || !GEngine->GameViewport || !GEngine->GameViewport->GetGameViewportWidget().IsValid()) { return FVector2D::ZeroVector; }
		const FGeometry G = GEngine->GameViewport->GetGameViewportWidget()->GetCachedGeometry();
		return G.LocalToAbsolute(G.GetLocalSize() * FVector2D(Fx, Fy));
	}
	// Настоящий щелчок Slate.
	static void Click(const FCtxRef& C, const FVector2D& P) { if (UAsdaWindowSubsystem* WM = C->Sub<UAsdaWindowSubsystem>()) { Hover(P); WM->SimulateClickAt(P, /*HoldFrames*/ 2); } }
	// Щелчок по кнопке вкладки 402 (`INVEN_CHOICEBUT_OPT0/1`).
	static bool ClickTab(const FCtxRef& C, int32 Tab)
	{
		UAsdaWindowSubsystem* WM = C->Sub<UAsdaWindowSubsystem>();
		UUserWidget* Win = WM ? WM->FindWindow(kWinId) : nullptr;
		FVector2D P;
		if (!Win || !Center(AsdaUi::FindNodeByPrefix(Win, Tab ? TEXT("INVEN_CHOICEBUT_OPT1") : TEXT("INVEN_CHOICEBUT_OPT0")), P)) { return false; }
		Click(C, P);
		return true;
	}
	// Щелчок по надетому слоту.
	static bool ClickSlot(const FCtxRef& C, int32 Slot)
	{
		FVector2D P;
		if (!Center(C->Inv()->DebugGetEquipSlotIcon(Slot), P)) { return false; }
		Click(C, P);
		return true;
	}
	// Двойной щелчок по надетому слоту.
	static bool DblSlot(const FCtxRef& C, int32 Slot)
	{
		FVector2D P;
		if (!Center(C->Inv()->DebugGetEquipSlotIcon(Slot), P)) { return false; }
		Hover(P);
		AsdaSimDoubleClickAt(C->Sub<UAsdaWindowSubsystem>(), P, false);
		return true;
	}
	// Перетащить надетый слот в точку.
	static bool DragSlot(const FCtxRef& C, int32 Slot, const FVector2D& To)
	{
		FVector2D A;
		if (!Center(C->Inv()->DebugGetEquipSlotIcon(Slot), A)) { return false; }
		Hover(A);
		C->Sub<UAsdaWindowSubsystem>()->SimulateDrag(A, To, /*HoldFrames*/ 6);
		return true;
	}
	// Кадр (шаг без других действий: снимок берётся в конце кадра).
	static void Shot(const FCtxRef& C, const TCHAR* Tag)
	{
		const FString File = C->FrameDir / FString::Printf(TEXT("%s_%s.png"), FAsdaLang::Code(FAsdaLang::Current()), Tag);
		UE_LOG(LogAsdaInvTabTest, Log, TEXT("%s: КАДР → %s"), kPfx, *File);
		FScreenshotRequest::RequestScreenshot(File, /*bShowUI*/ true, /*bAddFilenameSuffix*/ false);
	}
	// Ответ сервера 5001: Result, источник надетый слот, приёмник DstType:DstSlot.
	static void Feed5001(const FCtxRef& C, int32 Result, int32 Slot, int32 DstType, int32 DstSlot)
	{
		TArray<uint8> B; B.SetNumZeroed(24);
		B[0] = (uint8)Result; B[1] = 2;
		B[2] = (uint8)(Slot & 0xFF); B[3] = (uint8)((Slot >> 8) & 0xFF); B[6] = 3;
		B[13] = (uint8)(DstSlot & 0xFF); B[14] = (uint8)((DstSlot >> 8) & 0xFF); B[17] = (uint8)DstType;
		if (UAsdaNetSubsystem* Net = C->Sub<UAsdaNetSubsystem>()) { Net->DebugFeedPacket(5001, B); }
	}
	// Значение вкладки в настройках (−1 — записи нет).
	static int32 IniTab()
	{
		int32 V = -1;
		if (GConfig && !GConfig->GetInt(kSect, kKey, V, GGameUserSettingsIni)) { V = -1; }
		return V;
	}
	// Сохранить контейнер.
	static void SaveCont(const FCtxRef& C, int32 Cont, int32 N, TArray<TArray<uint8>>& Out)
	{
		Out.SetNum(N);
		for (int32 i = 0; i < N; ++i)
		{
			const FAsdaItemInstance* It = C->Inv()->GetCell(Cont, i);
			Out[i] = (It && !It->IsEmpty()) ? It->Raw : TArray<uint8>();
		}
	}
	// Вернуть контейнер.
	static void RestoreCont(const FCtxRef& C, int32 Cont, const TArray<TArray<uint8>>& In)
	{
		for (int32 i = 0; i < In.Num(); ++i)
		{
			C->Inv()->DebugPutItem(Cont, i, 0, 0);
			if (In[i].Num() > 0) { C->Inv()->ApplyExternalRecord(In[i], UAsdaInventorySubsystem::EExternalRecordMode::AsIs); }
		}
	}
	// Надеть набор и положить предмет в сумку (ячейка 0 — чтобы сетка была не пуста).
	static void Dress(const FCtxRef& C)
	{
		UAsdaInventorySubsystem* I = C->Inv();
		for (int32 i = 0; i < 60; ++i) { I->DebugPutItem(1, i, 0, 0); I->DebugPutItem(2, i, 0, 0); }
		for (int32 i = 0; i < 11; ++i) { I->DebugPutItem(3, i, 0, 0); }
		for (const FWorn& X : kWorn) { I->DebugPutItem(3, X.Slot, X.Code, 1); }
		I->DebugPutItem(1, 0, 20553, 1);
		I->DebugPutItem(1, 1, 20553, 1);
		I->DebugPutItem(2, 0, 20592, 3);
	}

	static void Finish(const FCtxRef& C)
	{
		if (C->Inv()->IsWindowOpen()) { C->Inv()->CloseWindow(); }
		RestoreCont(C, 1, C->SavedBag);
		RestoreCont(C, 2, C->SavedSpend);
		RestoreCont(C, 3, C->SavedWorn);
		if (GConfig)
		{
			if (C->bKeep)        { GConfig->SetInt(kSect, kKey, 1, GGameUserSettingsIni); }
			else if (C->bHadIni) { GConfig->SetInt(kSect, kKey, C->IniWas, GGameUserSettingsIni); }
			else            { GConfig->RemoveKey(kSect, kKey, GGameUserSettingsIni); }
			GConfig->Flush(false, GGameUserSettingsIni);
		}
		AsdaCVarSave::SetForTest(TEXT("asda.Inven.EquipSelectTab"), 1);
		AsdaCVarSave::SetForTest(TEXT("asda.Inven.EquipDragOff"), 1);
		AsdaCVarSave::SetForTest(TEXT("asda.Inven.RememberTab"), 1);
		if (UAsdaNetSubsystem* Net = C->Sub<UAsdaNetSubsystem>()) { AsdaSelfTestKit::DisarmCapture(Net, C->CapGen); }
		if (C->Self.IsValid()) { C->Self->Restore(); }
		AsdaSelfTestKit::Finish(C->W.Get(), kCmd, C->bQuit, LogAsdaInvTabTest, kPfx, C->Ok, C->Fail);
	}

	// Двойной щелчок снятия: начать с вкладки From, снять Slot, ждать вкладку WantTab, ответ 5001 в DstType:DstSlot, проверить, что вещь пришла.
	static void DoubleOff(const FCtxRef& C, const FString& Tag, int32 From, int32 Slot, int32 WantTab, int32 DstType, int32 DstSlot)
	{
		Step(C, [C, From]() { ClickTab(C, From); });
		Step(C, [C, Tag, From, Slot]()
		{
			C->Sent->Reset();
			C->Unequip0 = C->Inv()->DebugGetUnequipRequests();
			C->Expect(C->Inv()->GetTab() == From && DblSlot(C, Slot), FString::Printf(TEXT("%s вкладка %d, двойной щелчок по слоту %d"), *Tag, From, Slot));
		});
		Step(C, [C, Tag, Slot, WantTab, DstType, DstSlot]()
		{
			UAsdaInventorySubsystem* I = C->Inv();
			C->Expect(I->GetTab() == WantTab && I->DebugGetUnequipRequests() == C->Unequip0 + 1 && C->SentCount(5000) == 1,
				FString::Printf(TEXT("%s снятие: вкладка %d (ждём %d), запросов снятия +%d, 5000=%d"), *Tag, I->GetTab(), WantTab, I->DebugGetUnequipRequests() - C->Unequip0, C->SentCount(5000)));
			Feed5001(C, 1, Slot, DstType, DstSlot);
		});
		Step(C, [C, Tag, Slot, WantTab, DstType, DstSlot]()
		{
			UAsdaInventorySubsystem* I = C->Inv();
			const FAsdaItemInstance* Was = I->GetCell(3, Slot);
			const FAsdaItemInstance* Now = I->GetCell(DstType, DstSlot);
			C->Expect((!Was || Was->IsEmpty()) && Now && !Now->IsEmpty() && I->GetTab() == WantTab,
				FString::Printf(TEXT("%s 5001 код 1: вещь в %d:%d, слот пуст, вкладка осталась %d"), *Tag, DstType, DstSlot, I->GetTab()));
		});
	}

	static void Run(const FCtxRef& C)
	{
		UAsdaInventorySubsystem* I = C->Inv();
		UAsdaNetSubsystem* Net = C->Sub<UAsdaNetSubsystem>();
		if (!I || !Net || !C->Sub<UAsdaWindowSubsystem>())
		{
			C->Expect(false, TEXT("подсистемы на месте"));
			AsdaSelfTestKit::Finish(C->W.Get(), kCmd, C->bQuit, LogAsdaInvTabTest, kPfx, C->Ok, C->Fail);
			return;
		}
		C->Self = MakeShared<FAsdaNetSelfScope>(Net, kCmd);
		SaveCont(C, 1, 60, C->SavedBag);
		SaveCont(C, 2, 60, C->SavedSpend);
		SaveCont(C, 3, 11, C->SavedWorn);
		C->bHadIni = GConfig && GConfig->GetInt(kSect, kKey, C->IniWas, GGameUserSettingsIni);
		if (GConfig) { GConfig->RemoveKey(kSect, kKey, GGameUserSettingsIni); }
		TSharedRef<TArray<uint16>> Sent = C->Sent;
		C->CapGen = AsdaSelfTestKit::ArmCapture(Net, [Sent](uint16 Op, const uint8*, int32, bool, TCHAR) { Sent->Add(Op); }, 0, kCmd);
		AsdaCVarSave::SetForTest(TEXT("asda.Inven.EquipSelectTab"), 1);
		AsdaCVarSave::SetForTest(TEXT("asda.Inven.EquipDragOff"), 1);
		AsdaCVarSave::SetForTest(TEXT("asda.Inven.RememberTab"), 1);
		if (I->IsWindowOpen()) { I->CloseWindow(); }
		Dress(C);
		I->OpenWindow();
		C->Expect(I->IsWindowOpen() && I->GetTab() == 0, TEXT("A0 первое открытие без записи — вкладка 0, как оригинал"));

		// ═══ K. Ключ 0: вкладка не меняется (шаг 0 — воспроизведение жалобы) ═══
		Step(C, [C]() { AsdaCVarSave::SetForTest(TEXT("asda.Inven.EquipSelectTab"), 0); C->Expect(ClickSlot(C, 1), TEXT("K1 (ключ 0, прежний порт) щелчок по броне")); }, 1.0f);
		Step(C, [C]()
		{
			C->Expect(C->Inv()->GetTab() == 0, FString::Printf(TEXT("K2 (ключ 0) вкладка осталась 0 — так выглядела жалоба (вкладка %d)"), C->Inv()->GetTab()));
			AsdaCVarSave::SetForTest(TEXT("asda.Inven.EquipSelectTab"), 1);
		});

		// ═══ S. Таблица слотов `uiEquipSlotSelect` ═══
		struct FRow { int32 Slot; int32 Tab; };
		static const FRow kRows[] = { {1, 1}, {5, 0}, {9, 1}, {10, 0}, {0, 1}, {6, 0}, {2, 1}, {7, 0}, {3, 1} };
		for (const FRow& R : kRows)
		{
			const int32 Slot = R.Slot, Tab = R.Tab;
			Step(C, [C, Slot]() { ClickSlot(C, Slot); });
			Step(C, [C, Slot, Tab]() { C->Expect(C->Inv()->GetTab() == Tab, FString::Printf(TEXT("S слот %d → вкладка %d (стала %d)"), Slot, Tab, C->Inv()->GetTab())); });
		}
		Step(C, [C]() { ClickSlot(C, 4); });
		Step(C, [C]() { C->Expect(C->Inv()->GetTab() == 1, FString::Printf(TEXT("S пустой слот 4 вкладку не меняет (стала %d)"), C->Inv()->GetTab())); });

		// ═══ D. Снятие двойным щелчком ═══
		DoubleOff(C, TEXT("D1 броня"), 0, 1, 1, 1, 3);
		Step(C, [C]() { Shot(C, TEXT("D1_armor_off")); });
		DoubleOff(C, TEXT("D2 оружие"), 0, 9, 1, 1, 4);
		DoubleOff(C, TEXT("D3 кольцо"), 1, 5, 0, 2, 5);
		Step(C, [C]() { Shot(C, TEXT("D3_ring_off")); });

		// ═══ F. Полная сумка: сервер отвечает 5001 код 0, вещь на месте, вкладка уже переключена ═══
		Step(C, [C]() { ClickTab(C, 0); });
		Step(C, [C]() { C->Sent->Reset(); C->Expect(DblSlot(C, 0), TEXT("F1 двойной щелчок по слоту 0 с вкладки 0")); });
		Step(C, [C]() { C->Expect(C->SentCount(5000) == 1, TEXT("F2 5000 уходит (проверки места у клиента нет, эталон)")); Feed5001(C, 0, 0, 1, 0); });
		Step(C, [C]()
		{
			const FAsdaItemInstance* It = C->Inv()->GetCell(3, 0);
			C->Expect(It && !It->IsEmpty() && C->Inv()->GetTab() == 1, FString::Printf(TEXT("F3 отказ 5001 код 0: вещь надета, вкладка 1 (стала %d)"), C->Inv()->GetTab()));
		});

		// ═══ G. Снятие броском ═══
		Step(C, [C]() { ClickTab(C, 0); });
		Step(C, [C]()
		{
			C->Sent->Reset();
			FVector2D To;
			C->Expect(Center(C->Inv()->DebugGetGridIcon(0), To) && DragSlot(C, 2, To), TEXT("G1 перетаскивание слота 2 на ячейку сетки"));
		});
		Step(C, [C]()
		{
			C->Expect(C->SentCount(5000) == 1 && C->Inv()->GetTab() == 1, FString::Printf(TEXT("G2 бросок на сетку = снять: 5000=%d, вкладка %d"), C->SentCount(5000), C->Inv()->GetTab()));
			Feed5001(C, 1, 2, 1, 7);
		}, 1.2f);
		Step(C, [C]()
		{
			const FAsdaItemInstance* It = C->Inv()->GetCell(3, 2);
			C->Expect(!It || It->IsEmpty(), TEXT("G3 5001 код 1: слот 2 пуст"));
			C->Sent->Reset();
			FVector2D To;
			C->Expect(Center(C->Inv()->DebugGetEquipSlotIcon(0), To) && DragSlot(C, 3, To), TEXT("G4 перетаскивание слота 3 на другой слот"));
		});
		Step(C, [C]()
		{
			C->Expect(C->SentCount(5000) == 0, FString::Printf(TEXT("G5 бросок на манекен ничего не шлёт (5000=%d)"), C->SentCount(5000)));
			DragSlot(C, 3, ViewPoint(0.45f, 0.30f));
		}, 1.2f);
		Step(C, [C]()
		{
			const FAsdaItemInstance* It = C->Inv()->GetCell(3, 3);
			C->Expect(C->SentCount(5000) == 0 && C->SentCount(5002) == 0 && C->SentCount(5006) == 0 && It && !It->IsEmpty(),
				FString::Printf(TEXT("G6 бросок в мир: вещь надета, ни снятия, ни выброса (5000=%d 5002=%d 5006=%d)"), C->SentCount(5000), C->SentCount(5002), C->SentCount(5006)));
			AsdaCVarSave::SetForTest(TEXT("asda.Inven.EquipDragOff"), 0);
			FVector2D To;
			Center(C->Inv()->DebugGetGridIcon(0), To);
			DragSlot(C, 3, To);
		}, 1.2f);
		Step(C, [C]()
		{
			C->Expect(C->SentCount(5000) == 0, FString::Printf(TEXT("G7 (ключ 0) захвата нет, 5000=%d"), C->SentCount(5000)));
			AsdaCVarSave::SetForTest(TEXT("asda.Inven.EquipDragOff"), 1);
		}, 1.2f);

		// ═══ M. Память вкладки ═══
		Step(C, [C]() { C->Expect(ClickTab(C, 1), TEXT("M1 ручной выбор вкладки 1")); });
		Step(C, [C]() { C->Expect(IniTab() == 1, FString::Printf(TEXT("M2 записано LastTab=1 (%d)"), IniTab())); C->Inv()->CloseWindow(); });
		Step(C, [C]() { C->Inv()->OpenWindow(); });
		Step(C, [C]()
		{
			UAsdaInventorySubsystem* Iv = C->Inv();
			C->Expect(Iv->GetTab() == 1 && Iv->GetGridContainer() == 1 && !Iv->IsPanelExpanded(), FString::Printf(TEXT("M3 окно открылось на вкладке 1, панель свёрнута (вкладка %d, панель %d)"), Iv->GetTab(), Iv->IsPanelExpanded() ? 1 : 0));
		});
		Step(C, [C]() { Shot(C, TEXT("M3_reopen_tab1")); });
		Step(C, [C]() { ClickTab(C, 0); });
		Step(C, [C]() { C->Inv()->CloseWindow(); });
		Step(C, [C]() { C->Inv()->OpenWindow(); C->Expect(C->Inv()->GetTab() == 0 && IniTab() == 0, FString::Printf(TEXT("M4 выбор вкладки 0 запомнен (вкладка %d, запись %d)"), C->Inv()->GetTab(), IniTab())); });
		// Автопереход важнее памяти и её не переписывает.
		Step(C, [C]() { ClickSlot(C, 0); });
		Step(C, [C]() { C->Expect(C->Inv()->GetTab() == 1 && IniTab() == 0, FString::Printf(TEXT("M5 память 0, щелчок по броне → вкладка 1, запись осталась 0 (вкладка %d, запись %d)"), C->Inv()->GetTab(), IniTab())); C->Inv()->CloseWindow(); });
		Step(C, [C]() { C->Inv()->OpenWindow(); C->Expect(C->Inv()->GetTab() == 0, FString::Printf(TEXT("M6 после автоперехода окно снова на выборе игрока (вкладка %d)"), C->Inv()->GetTab())); });
		Step(C, [C]() { ClickTab(C, 1); });
		Step(C, [C]() { ClickSlot(C, 6); });
		Step(C, [C]() { C->Expect(C->Inv()->GetTab() == 0 && IniTab() == 1, FString::Printf(TEXT("M7 память 1, щелчок по кольцу → вкладка 0, запись осталась 1 (вкладка %d, запись %d)"), C->Inv()->GetTab(), IniTab())); C->Inv()->CloseWindow(); });
		// Ключ 0: оригинал.
		Step(C, [C]() { AsdaCVarSave::SetForTest(TEXT("asda.Inven.RememberTab"), 0); C->Inv()->OpenWindow(); C->Expect(C->Inv()->GetTab() == 0, TEXT("M8 (ключ 0, оригинал) при записи 1 окно на вкладке 0")); });
		Step(C, [C]() { ClickTab(C, 1); });
		Step(C, [C]() { C->Inv()->CloseWindow(); AsdaCVarSave::SetForTest(TEXT("asda.Inven.RememberTab"), 1); });
		// Между входами: сброс сессии и запись на диске.
		Step(C, [C]()
		{
			// `GGameUserSettingsIni` в UE 5 — имя ветки настроек, не путь: файл ищем в папке настроек.
			TArray<FString> Files;
			IFileManager::Get().FindFilesRecursive(Files, *FPaths::GeneratedConfigDir(), TEXT("GameUserSettings.ini"), true, false);
			bool bOnDisk = false;
			for (const FString& F : Files)
			{
				FString Txt;
				if (FFileHelper::LoadFileToString(Txt, *F) && Txt.Contains(TEXT("[AsdaInventory]")) && Txt.Contains(TEXT("LastTab=1"))) { bOnDisk = true; }
			}
			C->Expect(bOnDisk, FString::Printf(TEXT("M9 запись на диске (файлов настроек %d в %s)"), Files.Num(), *FPaths::GeneratedConfigDir()));
			C->Inv()->ResetSessionState(EAsdaSessionResetReason::LeftWorld);
		});
		Step(C, [C]() { C->Inv()->OpenWindow(); C->Expect(C->Inv()->GetTab() == 1, FString::Printf(TEXT("M10 после сброса сессии окно на вкладке 1 (вкладка %d)"), C->Inv()->GetTab())); });
		Step(C, [C]() { Finish(C); });
	}
}

static FAutoConsoleCommandWithWorldAndArgs GAsdaSelfTestInvTab(
	TEXT("asda.SelfTest.InvTab"),
	TEXT("Самотест пачки 12 (07.11.2026): вкладка сумки при выборе и снятии надетого (uiEquipSlotSelect), снятие броском, память вкладки. "
	     "Путь игрока, офлайн. Кадры — Reports/inv_tab/2026-11-07/frames. `delay=N` `quit`."),
	FConsoleCommandWithWorldAndArgsDelegate::CreateLambda([](const TArray<FString>& Args, UWorld* W)
	{
		using namespace AsdaInvTabTest;
		const FAsdaSelfTestArgs KA = FAsdaSelfTestArgs::Parse(Args);
		if (!KA.Has(TEXT("selfready")) && W && GEngine && AsdaSelfTestKit::NeedSelfSpawn(W))
		{
			GEngine->Exec(W, TEXT("Asda.Load.CurrentMap 3"));
			GEngine->Exec(W, TEXT("Asda.World.FakeSelfSpawn 256 256"));
			const FString Again = FString::Printf(TEXT("%s selfready%s%s%s"), kCmd, KA.bQuit ? TEXT(" quit") : TEXT(""), KA.Has(TEXT("keep")) ? TEXT(" keep") : TEXT(""), KA.Has(TEXT("after")) ? TEXT(" after") : TEXT(""));
			TWeakObjectPtr<UWorld> WW(W);
			FTimerHandle H;
			W->GetTimerManager().SetTimer(H, FTimerDelegate::CreateLambda([WW, Again]() { if (WW.IsValid() && GEngine) { GEngine->Exec(WW.Get(), *Again); } }), 2.f, false);
			return;
		}
		AsdaCVarSave::ArmWorldGuard(W, kCmd);
		if (AsdaSelfTestKit::RefuseStart(W, kCmd, KA)) { return; }
		if (!W) { return; }
		FCtxRef C = MakeShared<FCtx>();
		C->W = W;
		C->bQuit = KA.bQuit;
		C->bKeep = KA.Has(TEXT("keep"));
		// Аргумент `after`: второй процесс той же папки настроек — первое открытие сумки берёт вкладку с диска (M11), запись убирается.
		if (KA.Has(TEXT("after")))
		{
			UAsdaInventorySubsystem* I = C->Inv();
			if (I) { if (I->IsWindowOpen()) { I->CloseWindow(); } I->OpenWindow(); }
			C->Expect(I && IniTab() == 1 && I->GetTab() == 1, FString::Printf(TEXT("M11 новый запуск игры: окно на вкладке 1 (вкладка %d, запись %d)"), I ? I->GetTab() : -1, IniTab()));
			if (I) { I->CloseWindow(); }
			if (GConfig) { GConfig->RemoveKey(kSect, kKey, GGameUserSettingsIni); GConfig->Flush(false, GGameUserSettingsIni); }
			AsdaSelfTestKit::Finish(W, kCmd, KA.bQuit, LogAsdaInvTabTest, kPfx, C->Ok, C->Fail);
			return;
		}
		C->FrameDir = FPaths::ConvertRelativePathToFull(FPaths::ProjectDir() / TEXT("../Reports/inv_tab/2026-11-07/frames"));
		FTimerHandle H;
		W->GetTimerManager().SetTimer(H, FTimerDelegate::CreateLambda([C]()
		{
			if (C->W.IsValid() && AsdaSelfTestKit::RefuseDeferred(C->W.Get(), kCmd, C->bQuit)) { return; }
			Run(C);
		}), FMath::Max(0.01f, KA.Delay), false);
	}));

#endif // ASDA_DEV_CONSOLE && !UE_BUILD_SHIPPING
