// Copyright (c) 2026 @sitxovski (Discord)
// ★ САМОТЕСТ ВЗЯТИЯ КВЕСТА И ВЫБОРА НАГРАДЫ (29.10.2026, баги игроков 2 и 3): `asda.SelfTest.QuestDialogReward [quit]`.
//   Офлайн (`-AsdaNoNet`), реальными путями (закон diagnostic-must-use-real-path):
//     · разговор — `OpenForNpcId` (тот же вход, что клик по NPC без актора), страница — `RenderPage`, кнопки шага и награды —
//       НАСТОЯЩИЙ клик Slate в центр живого прямоугольника кнопки (`UAsdaWindowSubsystem::SimulateClickAt`);
//     · попап наград 540 — штатный `ShowRewardPopupForCheck` (тело ветки «прогресс 2»);
//     · отправки 5043/5041 — БОЕВЫЕ под перехватом (`AsdaSelfTestKit::ArmCapture`), разбор тела побайтно;
//     · состояние персонажа и журнала — офлайн-эмуляция `asda.Quest.FakeSelf` / `asda.Quest.FakeState` (штатный `ApplyQuestStateBlob`).
//   Ветки бага 2 («квест не берётся — диалог обрывается кнопкой „Завершить разговор“»):
//     D1 класс 99 (Рейчел, «Золотая копилка», стр. 27) — 5043 npc 48 ячейка 1; D2 ключи 0 — как оригинал: 5043 нет, страница 28;
//     D3 новичок (класс 0) — 5043 нет, страница остаётся 27; D4 то же с `asda.Quest.AcceptFailStay 0` — страница 28 (оригинал);
//     D5…D9 ячейки выдачи, дописанные по серверу (NPC 127/119/128/130/126) — 5043 с серверным номером ячейки.
//   Ветки бага 3 (зоны клика выбора награды, окно 540): для 1/2/3 строк выбора при 1/2/3 базовых наградах (Δ 100/135):
//     зона каждой строки = прямоугольник строки (±1.5 px), зоны пустых строк погашены; «Готово» без выбора — 5041 нет;
//     клик по ПОСЛЕДНЕЙ строке выделяет её; «Готово» → 5041 с этим индексом награды; после перетаскивания окна — то же;
//     другое разрешение (1600×900) — то же; `asda.UI.CellGridSyncOverlay 0` — зоны на старом месте (дефект воспроизводится).
//   Метки «!» (29.10.2026, решение владельца «выровняй»): по КАЖДОЙ правке ведомостей `Tools/data/nsr_script_fixes.json` и
//     `nsr_step_cond_fixes.json` — правка доехала в ассет; при выполненном серверном условии запись срабатывает, метка над NPC
//     есть и «Принять» шлёт 5043 с серверной ячейкой; при каждом нарушении (уровень/класс/цепочка) запись не срабатывает;
//     убранные записи (квесты 570/622/625) в ассете отсутствуют.
//   Итог: «КВЕСТ-ДИАЛОГ ИТОГ: ok=N fail=M» (команда `asda.SelfTest.WorldMapMarks` — «КАРТА-МИРА-МЕТКИ ИТОГ: …»).
#include "CoreMinimal.h"
#include "AsdaDevConsole.h"

#if ASDA_DEV_CONSOLE && !UE_BUILD_SHIPPING

#include "Npc/AsdaNpcDialogSubsystem.h"
#include "UI/AsdaMinimapSubsystem.h"
#include "Quest/AsdaQuestLogSubsystem.h"
#include "Npc/AsdaNpcDialogTypes.h"
#include "Quest/AsdaQuestTypes.h"
#include "UI/AsdaItemTooltipSubsystem.h"
#include "AsdaDataTableCache.h"
#include "World/AsdaLoadManagerSubsystem.h"
#include "Quest/AsdaBoardQuestSubsystem.h"
#include "Components/Image.h"
#include "Engine/Texture2D.h"
#include "Quest/AsdaQuestMarkerSubsystem.h"
#include "Quest/AsdaNsrScriptPage.h"   // ★ 06.11.2026: `StartClosed` — закрытая выдача заданий 156/157
#include "UI/AsdaWindowSubsystem.h"
#include "UI/AsdaUiHelpers.h"
#include "Test/AsdaSelfTestNet.h"   // ★ R1: тонкий доступ самотестов к сети (вместо AsdaNetSubsystem.h)
#include "Test/AsdaSelfTestTree.h"   // папка проекта в дереве (ASDA_ROOT) — ведомости-оракулы и для собранной игры
#include "Test/AsdaSelfTestKit.h"
#include "Blueprint/UserWidget.h"
#include "Components/Button.h"
#include "Containers/Ticker.h"
#include "Engine/Engine.h"
#include "Engine/GameInstance.h"
#include "Engine/GameViewportClient.h"
#include "Engine/World.h"
#include "HAL/PlatformTime.h"
#include "Dom/JsonObject.h"
#include "Serialization/JsonReader.h"
#include "Serialization/JsonSerializer.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"

DEFINE_LOG_CATEGORY_STATIC(LogAsdaQuestDlgTest, Log, All);

struct FAsdaQuestDialogRewardSelfTest
{
	// Имя команды прогона: одна машина шагов на две команды (`QuestDialogReward` и `WorldMapMarks`).
	static const TCHAR*& CmdRef() { static const TCHAR* C = TEXT("asda.SelfTest.QuestDialogReward"); return C; }
	static const TCHAR* Cmd() { return CmdRef(); }
	// ★ R5-fix2-c (13.11.2026): метка строки итога — своя у каждой из двух команд. Раньше обе печатали «КВЕСТ-ДИАЛОГ ИТОГ», и в сводке
	//   пакета у `asda.SelfTest.WorldMapMarks` стояла строка, по виду принадлежащая другому самотесту. Строки отдельных
	//   проверок («КВЕСТ-ДИАЛОГ ok/FAIL …») общие для обеих команд и не менялись.
	static const TCHAR*& TagRef() { static const TCHAR* T = TEXT("КВЕСТ-ДИАЛОГ"); return T; }

	// Отправка, снятая перехватчиком.
	struct FCap { uint16 Op = 0; TArray<uint8> P; };

	// Шаг сценария: возвращает паузу до следующего шага, с (отрицательное — ждать доставки синтетического клика).
	using FStep = TFunction<double()>;

	// Состояние прогона.
	struct FRun
	{
		TWeakObjectPtr<UGameInstance> GI;
		bool bQuit = false;
		int32 Ok = 0, Fail = 0;
		TArray<FStep> Steps;
		int32 Next = 0;
		double WakeAt = 0.0;
		bool bWaitClick = false;
		uint32 Gen = 0;
		TSharedPtr<TArray<FCap>> Caps;
		TUniquePtr<FAsdaCVarScope> Keys;
		FVector2D RowBeforeDrag = FVector2D(-1.0, -1.0);   // R6: левый верх строки 0 до перетаскивания окна
		int32 SampleMax = 0;                                 // карта мира: максимум нарисованных меток за окно выборки (метки мигают)
		int32 SampleYellow = 0, SampleRed = 0;               // то же по цветам «!»: жёлтые (метка 1) и красные (метка 2)
		TArray<FAsdaWorldMapArea> SavedAreas;                // карта мира: список областей до подмешивания турнирных карт
		int32 BestArea = -1;                                 // карта мира: чужая полевая область с метками (для ключа отката)
	};
	static TSharedPtr<FRun>& Run() { static TSharedPtr<FRun> R; return R; }

	static void Expect(FRun& R, bool b, const FString& What)
	{
		if (b) { ++R.Ok; UE_LOG(LogAsdaQuestDlgTest, Log, TEXT("КВЕСТ-ДИАЛОГ ok   %s"), *What); }
		else { ++R.Fail; UE_LOG(LogAsdaQuestDlgTest, Error, TEXT("КВЕСТ-ДИАЛОГ FAIL %s"), *What); }
	}

	static UAsdaNpcDialogSubsystem* Dlg(FRun& R) { return R.GI.IsValid() ? R.GI->GetSubsystem<UAsdaNpcDialogSubsystem>() : nullptr; }
	static UAsdaWindowSubsystem* WM(FRun& R) { return R.GI.IsValid() ? R.GI->GetSubsystem<UAsdaWindowSubsystem>() : nullptr; }
	static UWorld* World(FRun& R) { return R.GI.IsValid() ? R.GI->GetWorld() : nullptr; }
	static void Exec(FRun& R, const FString& C) { if (GEngine && World(R)) { GEngine->Exec(World(R), *C); } }

	// Виджет окна по имени (nullptr — окна/виджета нет).
	static UWidget* Find(FRun& R, const TCHAR* WinId, const FString& Name)
	{
		UAsdaWindowSubsystem* W = WM(R);
		UUserWidget* Win = W ? W->FindWindow(FName(WinId)) : nullptr;
		return Win ? Win->GetWidgetFromName(FName(*Name)) : nullptr;
	}

	// Настоящий клик Slate в центр живого прямоугольника виджета; false — виджет не отрисован.
	static bool ClickCenter(FRun& R, UWidget* W, const FString& What)
	{
		FVector2D A, B;
		if (!W || !AsdaUi::WidgetAbsRect(W, A, B) || (B - A).X < 2.0)
		{
			Expect(R, false, FString::Printf(TEXT("клик «%s»: виджет не найден или не отрисован"), *What));
			return false;
		}
		WM(R)->SimulateClickAt((A + B) * 0.5, /*HoldFrames*/6);
		R.bWaitClick = true;
		return true;
	}

	// Отправки опкода Op, снятые с начала шага.
	static TArray<FCap> Sent(FRun& R, uint16 Op)
	{
		TArray<FCap> Out;
		for (const FCap& C : *R.Caps) { if (C.Op == Op) { Out.Add(C); } }
		return Out;
	}
	static int32 U16(const TArray<uint8>& P, int32 O) { return P.IsValidIndex(O + 1) ? (int32)(P[O] | (P[O + 1] << 8)) : -1; }

	// ── Баг 2: шаг «Принять задание» ──────────────────────────────────────────────────────────────────
	// Открыть разговор с NPC, показать страницу, кликнуть первую кнопку шага (578 `NPC_QA_BUT00`) настоящим кликом.
	static void AddAcceptCase(FRun& R, const FString& Label, int32 NpcId, int32 Page, const FString& SelfCmd, const FString& StateCmd,
		int32 KeyClass99, int32 KeyFailStay, int32 WantSlot, int32 WantPage)
	{
		R.Steps.Add([&R, Label, NpcId, Page, SelfCmd, StateCmd, KeyClass99, KeyFailStay]() -> double
		{
			R.Keys->Set(TEXT("asda.Quest.Class99AnyJob"), KeyClass99);
			R.Keys->Set(TEXT("asda.Quest.AcceptFailStay"), KeyFailStay);
			Exec(R, SelfCmd);
			if (!StateCmd.IsEmpty()) { Exec(R, StateCmd); }
			UAsdaNpcDialogSubsystem* D = Dlg(R);
			if (D) { D->ResetSessionState(EAsdaSessionResetReason::LeftWorld); }   // снять «заявку в полёте» прошлого случая
			const bool bOpen = D && D->OpenForNpcId(NpcId);
			if (D) { D->RenderPage(Page); }
			Expect(R, bOpen && D && D->GetCurrentTalkId() == Page,
				FString::Printf(TEXT("%s: разговор с NPC %d открыт, страница %d"), *Label, NpcId, Page));
			return 0.6;
		});
		R.Steps.Add([&R, Label]() -> double
		{
			R.Caps->Reset();
			return ClickCenter(R, Find(R, TEXT("NPC_LINE_NPC"), TEXT("NPC_QA_BUT00")), Label + TEXT(" кнопка шага 578")) ? -1.0 : 0.1;
		});
		R.Steps.Add([&R, Label, NpcId, WantSlot, WantPage]() -> double
		{
			const TArray<FCap> S = Sent(R, 5043);
			if (WantSlot >= 0)
			{
				const bool bOne = S.Num() == 1;
				const int32 Npc = bOne ? U16(S[0].P, 0) : -1, Slot = bOne ? U16(S[0].P, 2) : -1;
				Expect(R, bOne && Npc == NpcId && Slot == WantSlot,
					FString::Printf(TEXT("%s: 5043 ушёл ×%d, npc %d ячейка %d (ждали ×1, npc %d ячейка %d)"),
						*Label, S.Num(), Npc, Slot, NpcId, WantSlot));
			}
			else
			{
				Expect(R, S.Num() == 0, FString::Printf(TEXT("%s: 5043 НЕ ушёл (ушло ×%d)"), *Label, S.Num()));
			}
			const int32 Cur = Dlg(R) ? Dlg(R)->GetCurrentTalkId() : -1;
			Expect(R, Cur == WantPage, FString::Printf(TEXT("%s: страница после клика %d (ждали %d)"), *Label, Cur, WantPage));
			return 0.1;
		});
	}

	// ── Баг 3: зоны клика выбора награды ─────────────────────────────────────────────────────────────
	// Зона строки k = прямоугольник строки (живая геометрия Slate); зоны строк без награды погашены.
	//   bWantAligned = false — обратная проверка для ключа отката: зона ДОЛЖНА стоять не на строке.
	static void CheckZones(FRun& R, const FString& Label, int32 NumSel, bool bWantAligned)
	{
		static const TCHAR* Idx[6] = { TEXT("06"), TEXT("07"), TEXT("08"), TEXT("09"), TEXT("10"), TEXT("11") };
		for (int32 k = 0; k < 6; ++k)
		{
			UWidget* Back = Find(R, TEXT("EPISODEPOP_WIN"), FString::Printf(TEXT("EPISODEPOP_ITEMBACK%s_grp"), Idx[k]));
			UWidget* Hit = Find(R, TEXT("EPISODEPOP_WIN"), FString::Printf(TEXT("CELL_HIT%02d"), k));
			if (k >= NumSel)
			{
				if (!bWantAligned) { continue; }
				const bool bOff = !Hit || Hit->GetVisibility() == ESlateVisibility::Collapsed;
				Expect(R, bOff, FString::Printf(TEXT("%s: зона пустой строки %d погашена"), *Label, k));
				continue;
			}
			FVector2D Ba(0.0), Bb(0.0), Ha(0.0), Hb(0.0);
			const bool bRects = Back && Hit && AsdaUi::WidgetAbsRect(Back, Ba, Bb) && AsdaUi::WidgetAbsRect(Hit, Ha, Hb);
			const double Dy = bRects ? (Ha.Y - Ba.Y) : 9999.0, Dx = bRects ? (Ha.X - Ba.X) : 9999.0;
			const double DyB = bRects ? (Hb.Y - Bb.Y) : 9999.0, DxB = bRects ? (Hb.X - Bb.X) : 9999.0;
			const bool bAligned = bRects && FMath::Abs(Dy) <= 1.5 && FMath::Abs(Dx) <= 1.5 && FMath::Abs(DyB) <= 1.5 && FMath::Abs(DxB) <= 1.5;
			if (bWantAligned)
			{
				Expect(R, bAligned, FString::Printf(TEXT("%s: зона строки %d = строке (сдвиг зоны x%+.1f y%+.1f, низ/право %+.1f/%+.1f)"),
					*Label, k, Dx, Dy, DxB, DyB));
			}
			else
			{
				Expect(R, bRects && FMath::Abs(Dy) > 20.0, FString::Printf(TEXT("%s: ключ 0 — зона строки %d уехала (y%+.1f), дефект воспроизведён"),
					*Label, k, Dy));
			}
		}
	}

	// Случай попапа ПУТЁМ ИГРОКА: квест в журнале на нужной стадии (`FakeState`), разговор с NPC, страница сдачи, настоящий
	//   клик по кнопке шага → `RunStepAction` (прогресс 2) → попап 540. Затем зоны, «Готово» без выбора, выбор, «Готово».
	static void AddRewardCase(FRun& R, const FString& Label, int32 NpcId, int32 Talk, const FString& SelfCmd, const FString& StateCmd,
		int32 NumSel, bool bFullPath)
	{
		R.Steps.Add([&R, Label, NpcId, Talk, SelfCmd, StateCmd]() -> double
		{
			UAsdaNpcDialogSubsystem* D = Dlg(R);
			if (D) { D->DebugHideRewardPopup(); D->ResetSessionState(EAsdaSessionResetReason::LeftWorld); }
			Exec(R, SelfCmd);
			Exec(R, StateCmd);   // квест в журнале — условие шага сдачи и слот журнала для 5041
			const bool bOpen = D && D->OpenForNpcId(NpcId);
			if (D) { D->RenderPage(Talk); }
			Expect(R, bOpen && D && D->GetCurrentTalkId() == Talk,
				FString::Printf(TEXT("%s: разговор с NPC %d, страница сдачи %d"), *Label, NpcId, Talk));
			return 0.6;
		});
		R.Steps.Add([&R, Label]() -> double
		{
			return ClickCenter(R, Find(R, TEXT("NPC_LINE_NPC"), TEXT("NPC_QA_BUT00")), Label + TEXT(" кнопка шага сдачи")) ? -1.0 : 0.1;
		});
		R.Steps.Add([&R, Label]() -> double
		{
			Expect(R, Dlg(R) && Dlg(R)->DebugRewardPopupVisible(), FString::Printf(TEXT("%s: попап 540 показан кликом по шагу"), *Label));
			return 0.4;
		});
		R.Steps.Add([&R, Label, NumSel]() -> double
		{
			CheckZones(R, Label, NumSel, /*bWantAligned*/true);
			return 0.05;
		});
		if (!bFullPath) { return; }
		// «Готово» без выбора: эталон — нотис 6173, сдачи нет.
		R.Steps.Add([&R, Label]() -> double
		{
			R.Caps->Reset();
			return ClickCenter(R, Find(R, TEXT("EPISODEPOP_WIN"), TEXT("EPISODEPOP_ENDBUT")), Label + TEXT(" «Готово» без выбора")) ? -1.0 : 0.1;
		});
		R.Steps.Add([&R, Label]() -> double
		{
			Expect(R, Sent(R, 5041).Num() == 0 && Dlg(R) && Dlg(R)->DebugRewardPopupVisible(),
				FString::Printf(TEXT("%s: без выбора «Готово» сдачу НЕ шлёт, попап остаётся"), *Label));
			return 0.1;
		});
		// Клик по ПОСЛЕДНЕЙ строке — у неё сдвиг зоны был больше всего.
		R.Steps.Add([&R, Label, NumSel]() -> double
		{
			static const TCHAR* Idx[6] = { TEXT("06"), TEXT("07"), TEXT("08"), TEXT("09"), TEXT("10"), TEXT("11") };
			return ClickCenter(R, Find(R, TEXT("EPISODEPOP_WIN"), FString::Printf(TEXT("EPISODEPOP_ITEMBACK%s_grp"), Idx[NumSel - 1])),
				Label + TEXT(" строка выбора")) ? -1.0 : 0.1;
		});
		R.Steps.Add([&R, Label, NumSel]() -> double
		{
			const int32 Sel = Dlg(R) ? Dlg(R)->DebugRewardSelIndex() : -9;
			Expect(R, Sel == NumSel - 1, FString::Printf(TEXT("%s: клик в центр строки %d выделил строку %d"), *Label, NumSel - 1, Sel));
			R.Caps->Reset();
			return ClickCenter(R, Find(R, TEXT("EPISODEPOP_WIN"), TEXT("EPISODEPOP_ENDBUT")), Label + TEXT(" «Готово»")) ? -1.0 : 0.1;
		});
		R.Steps.Add([&R, Label, NpcId, NumSel]() -> double
		{
			const TArray<FCap> S = Sent(R, 5041);
			const bool bOne = S.Num() == 1 && S[0].P.Num() >= 7;
			const int32 Npc = bOne ? U16(S[0].P, 0) : -1, Pick = bOne ? (int32)S[0].P[4] : -1;
			Expect(R, bOne && Npc == NpcId && Pick == NumSel - 1,
				FString::Printf(TEXT("%s: 5041 ушёл ×%d, npc %d, индекс награды %d (ждали ×1, npc %d, индекс %d)"),
					*Label, S.Num(), Npc, Pick, NpcId, NumSel - 1));
			return 0.1;
		});
	}

	// ── Метки «!» и вход по выровненным записям сценария (29.10.2026, ведомости `Tools/data/nsr_*_fixes.json`) ──────────
	// Состояние `check.pos` / `check.neg` из ведомости → консоль офлайн-подмены (тот же путь, что `asda.Quest.FakeSelf/FakeState`).
	static void ApplyState(FRun& R, const TSharedPtr<FJsonObject>& St)
	{
		const int32 Lv = (int32)St->GetNumberField(TEXT("level")), Cls = (int32)St->GetNumberField(TEXT("cls"));
		const TArray<TSharedPtr<FJsonValue>>& Act = St->GetArrayField(TEXT("active"));
		TArray<FString> Done;
		for (const TSharedPtr<FJsonValue>& V : St->GetArrayField(TEXT("done"))) { Done.Add(FString::FromInt((int32)V->AsNumber())); }
		const int32 ActQ = Act.Num() > 0 ? (int32)Act[0]->AsNumber() : -1, ActS = Act.Num() > 1 ? (int32)Act[1]->AsNumber() : 0;
		Exec(R, FString::Printf(TEXT("asda.Quest.FakeSelf %d %d 0"), Lv, Cls));
		// Активного квеста нет — слот 0 занимает служебный номер 999 (в данных квестов с ним нет).
		Exec(R, FString::Printf(TEXT("asda.Quest.FakeState %d %d %s"), ActQ >= 0 ? ActQ : 999, ActS, *FString::Join(Done, TEXT(","))));
	}

	static TArray<TSharedPtr<FJsonValue>> LoadRows(const TCHAR* File)
	{
		FString Text;
		TSharedPtr<FJsonObject> Root;
		const FString Path = FPaths::ConvertRelativePathToFull(AsdaSelfTestTree::ProjectDir() / TEXT("Tools/data") / File);
		if (!FFileHelper::LoadFileToString(Text, *Path)) { return {}; }
		const TSharedRef<TJsonReader<>> Rd = TJsonReaderFactory<>::Create(Text);
		if (!FJsonSerializer::Deserialize(Rd, Root) || !Root.IsValid()) { return {}; }
		return Root->GetArrayField(TEXT("rows"));
	}

	static const FAsdaNpcQuestScriptRow* FindScript(UAsdaQuestMarkerSubsystem* QM, const FString& Nsr, int32 Rec)
	{
		QM->EnsureTables();
		if (const TArray<const FAsdaNpcQuestScriptRow*>* L = QM->ScriptByNsr.Find(Nsr.ToLower()))
		{
			for (const FAsdaNpcQuestScriptRow* Rp : *L) { if (Rp && Rp->RecIndex == Rec) { return Rp; } }
		}
		return nullptr;
	}

	// Поля записи в ассете = поля `set` ведомости (правка доехала через импортёр).
	static bool RowMatchesSet(const FAsdaNpcQuestScriptRow& Row, const TSharedPtr<FJsonObject>& Set, FString& Bad)
	{
		const TPair<const TCHAR*, int32> Ints[] = {
			{ TEXT("LevelMin"), Row.LevelMin }, { TEXT("LevelMax"), Row.LevelMax }, { TEXT("ClassId"), Row.ClassId },
			{ TEXT("ClassMode"), Row.ClassMode }, { TEXT("QuestDone"), Row.QuestDone }, { TEXT("QuestFreeA"), Row.QuestFreeA },
			{ TEXT("QuestFreeB"), Row.QuestFreeB }, { TEXT("QuestActive"), Row.QuestActive }, { TEXT("QuestStage"), Row.QuestStage }, { TEXT("Mark"), Row.Mark } };
		for (const TPair<const TCHAR*, int32>& F : Ints)
		{
			double V = 0.0;
			if (Set->TryGetNumberField(F.Key, V) && (int32)V != F.Value) { Bad += FString::Printf(TEXT(" %s=%d(ждали %d)"), F.Key, F.Value, (int32)V); }
		}
		return Bad.IsEmpty();
	}

	// Разговор → страница шага «Принять» → настоящий клик по его кнопке → 5043 с ячейкой `WantSlot` (−1 — не должен уйти).
	static void AddAcceptClick(FRun& R, const FString& Label, int32 NpcId, int32 Page, int32 StepIdx, int32 WantSlot)
	{
		R.Steps.Add([&R, Label, NpcId, Page]() -> double
		{
			UAsdaNpcDialogSubsystem* D = Dlg(R);
			if (D) { D->ResetSessionState(EAsdaSessionResetReason::LeftWorld); }
			const bool bOpen = D && D->OpenForNpcId(NpcId);
			if (D) { D->RenderPage(Page); }
			Expect(R, bOpen && D && D->GetCurrentTalkId() == Page, FString::Printf(TEXT("%s: разговор с NPC %d, страница %d"), *Label, NpcId, Page));
			return 0.5;
		});
		R.Steps.Add([&R, Label, StepIdx]() -> double
		{
			R.Caps->Reset();
			return ClickCenter(R, Find(R, TEXT("NPC_LINE_NPC"), FString::Printf(TEXT("NPC_QA_BUT%02d"), StepIdx)), Label + TEXT(" «Принять»")) ? -1.0 : 0.1;
		});
		R.Steps.Add([&R, Label, NpcId, WantSlot]() -> double
		{
			const TArray<FCap> S = Sent(R, 5043);
			if (WantSlot >= 0)
			{
				const bool bOne = S.Num() == 1;
				Expect(R, bOne && U16(S[0].P, 0) == NpcId && U16(S[0].P, 2) == WantSlot,
					FString::Printf(TEXT("%s: квест берётся — 5043 ×%d npc %d ячейка %d (ждали npc %d ячейка %d)"), *Label, S.Num(),
						bOne ? U16(S[0].P, 0) : -1, bOne ? U16(S[0].P, 2) : -1, NpcId, WantSlot));
			}
			else
			{
				Expect(R, S.Num() == 0, FString::Printf(TEXT("%s: квест НЕ берётся — 5043 не ушёл (×%d)"), *Label, S.Num()));
			}
			return 0.1;
		});
	}

	static void BuildMarks(FRun& R)
	{
		const TArray<TSharedPtr<FJsonValue>> Script = LoadRows(TEXT("nsr_script_fixes.json"));
		const TArray<TSharedPtr<FJsonValue>> Steps = LoadRows(TEXT("nsr_step_cond_fixes.json"));
		R.Steps.Add([&R, NS = Script.Num(), NT = Steps.Num()]() -> double
		{
			Expect(R, NS > 0 && NT > 0, FString::Printf(TEXT("ведомости выравнивания прочитаны: записей сценария %d, шагов %d"), NS, NT));
			return 0.05;
		});
		for (const TSharedPtr<FJsonValue>& V : Script)
		{
			const TSharedPtr<FJsonObject> F = V->AsObject();
			const FString Nsr = F->GetStringField(TEXT("nsr"));
			const int32 Rec = (int32)F->GetNumberField(TEXT("rec"));
			const FString Lbl = FString::Printf(TEXT("M %s#%d кв.%d"), *Nsr, Rec, (int32)F->GetNumberField(TEXT("quest")));
			if (F->HasField(TEXT("disable")))
			{
				R.Steps.Add([&R, Nsr, Rec, Lbl]() -> double
				{
					UAsdaQuestMarkerSubsystem* QM = R.GI->GetSubsystem<UAsdaQuestMarkerSubsystem>();
					Expect(R, QM && !FindScript(QM, Nsr, Rec), Lbl + TEXT(": запись убрана (квеста нет ни у клиента, ни у сервера) — метки и входа нет"));
					return 0.02;
				});
				continue;
			}
			const TSharedPtr<FJsonObject> Check = F->GetObjectField(TEXT("check"));
			const TSharedPtr<FJsonObject> Set = F->GetObjectField(TEXT("set"));
			const TSharedPtr<FJsonObject> Pos = Check->GetObjectField(TEXT("pos"));
			const int32 Quest = (int32)F->GetNumberField(TEXT("quest"));
			R.Steps.Add([&R, Nsr, Rec, Lbl, Set, Pos, Quest]() -> double
			{
				UAsdaQuestMarkerSubsystem* QM = R.GI->GetSubsystem<UAsdaQuestMarkerSubsystem>();
				const FAsdaNpcQuestScriptRow* Row = QM ? FindScript(QM, Nsr, Rec) : nullptr;
				FString Bad;
				Expect(R, Row && RowMatchesSet(*Row, Set, Bad), Lbl + TEXT(": правка в ассете DT_NpcQuestScript") + Bad);
				ApplyState(R, Pos);
				const bool bRec = Row && QM->CheckRecord(*Row);
				const int32 Mark = QM ? QM->EvalMarkForNsr(Nsr) : 0;
				// ★ 06.11.2026: у закрытых заданий 156/157 условие записи остаётся, но выдача и метка от неё сняты намеренно (`StartClosed`):
				//   ожидаем только сработавшее условие. Метку NPC могут давать другие его задания, поэтому её здесь не сверяем.
				if (AsdaNsrScript::StartClosed(Quest))
				{
					Expect(R, bRec, FString::Printf(TEXT("%s: выдача закрыта (задание без цели), условие записи цело, сработала=%d"), *Lbl, bRec ? 1 : 0));
					return 0.05;
				}
				Expect(R, bRec && Mark >= 1, FString::Printf(TEXT("%s: условие выполнено → запись сработала=%d, метка над NPC %d"), *Lbl, bRec ? 1 : 0, Mark));
				return 0.05;
			});
			AddAcceptClick(R, Lbl, (int32)F->GetNumberField(TEXT("npc")), (int32)F->GetNumberField(TEXT("accept_page")),
				(int32)F->GetNumberField(TEXT("accept_step")), (int32)F->GetNumberField(TEXT("give_slot")));
			for (const TSharedPtr<FJsonValue>& NV : Check->GetArrayField(TEXT("neg")))
			{
				const TSharedPtr<FJsonObject> Neg = NV->AsObject();
				R.Steps.Add([&R, Nsr, Rec, Lbl, Neg]() -> double
				{
					UAsdaQuestMarkerSubsystem* QM = R.GI->GetSubsystem<UAsdaQuestMarkerSubsystem>();
					const FAsdaNpcQuestScriptRow* Row = QM ? FindScript(QM, Nsr, Rec) : nullptr;
					ApplyState(R, Neg);
					Expect(R, Row && !QM->CheckRecord(*Row),
						FString::Printf(TEXT("%s: %s → запись НЕ срабатывает (метки и входа от неё нет)"), *Lbl, *Neg->GetStringField(TEXT("what"))));
					return 0.02;
				});
			}
		}
		for (const TSharedPtr<FJsonValue>& V : Steps)
		{
			const TSharedPtr<FJsonObject> F = V->AsObject();
			const FString Lbl = FString::Printf(TEXT("S %s стр.%d кв.%d"), *F->GetStringField(TEXT("nsr")), (int32)F->GetNumberField(TEXT("talk")),
				(int32)F->GetNumberField(TEXT("quest")));
			const TSharedPtr<FJsonObject> Check = F->GetObjectField(TEXT("check"));
			const int32 Npc = (int32)F->GetNumberField(TEXT("npc")), Talk = (int32)F->GetNumberField(TEXT("talk"));
			const int32 StepIdx = (int32)F->GetNumberField(TEXT("step")), Slot = (int32)F->GetNumberField(TEXT("give_slot"));
			const TSharedPtr<FJsonObject> Pos = Check->GetObjectField(TEXT("pos"));
			R.Steps.Add([&R, Pos]() -> double { ApplyState(R, Pos); return 0.05; });
			AddAcceptClick(R, Lbl + TEXT(" (условие сервера выполнено)"), Npc, Talk, StepIdx, Slot);
			for (const TSharedPtr<FJsonValue>& NV : Check->GetArrayField(TEXT("neg")))
			{
				const TSharedPtr<FJsonObject> Neg = NV->AsObject();
				R.Steps.Add([&R, Neg]() -> double { ApplyState(R, Neg); return 0.05; });
				AddAcceptClick(R, Lbl + TEXT(" (") + Neg->GetStringField(TEXT("what")) + TEXT(")"), Npc, Talk, StepIdx, -1);
			}
		}
	}

	// ── Карта мира 1710: метки квестов и подложка ПОКАЗАННОЙ карты (30.10.2026, выпуск 5.0.2) ─────────────────────────
	static UAsdaMinimapSubsystem* MM(FRun& R) { return R.GI.IsValid() ? R.GI->GetSubsystem<UAsdaMinimapSubsystem>() : nullptr; }

	// Сколько квест-меток ДОЛЖНО быть у NPC карты: тот же отбор и та же метка, что строит эталон для показанной карты
	//   (`npcUi_BuildNpcActionList(…, 1)`: типы 0/4/5, метка по сценарию NPC, у доски — свой счёт), на карту идут только 1 и 2.
	static int32 ExpectedMapMarks(FRun& R, int32 MapId, int32 OnlyMark = 0)   // OnlyMark: 0 — обе, 1 — жёлтые «!», 2 — красные
	{
		UAsdaQuestMarkerSubsystem* QM = R.GI->GetSubsystem<UAsdaQuestMarkerSubsystem>();
		const UAsdaBoardQuestSubsystem* Board = R.GI->GetSubsystem<UAsdaBoardQuestSubsystem>();
		if (!QM) { return -1; }
		QM->EnsureTables();
		int32 N = 0;
		for (const FAsdaNpcQuestNpcRow* Row : QM->NpcRows)
		{
			if (!Row || Row->MapId != MapId) { continue; }
			if (Row->NpcType != 0 && Row->NpcType != 4 && Row->NpcType != 5) { continue; }
			const int32 Mk = (Row->NpcType == 5 && Board && UAsdaBoardQuestSubsystem::IsEnabled())
				? Board->MinimapMark(Row->NpcId) : QM->EvalMarkForNsr(Row->NsrName);
			if ((Mk == 1 || Mk == 2) && (OnlyMark == 0 || Mk == OnlyMark)) { ++N; }
		}
		return N;
	}

	// Видимые квадраты квест-меток на большой карте — то, что видит игрок (`CountDrawnMarks`, пул маркеров).
	static int32 DrawnQuestMarks(FRun& R)
	{
		UAsdaMinimapSubsystem* M = MM(R);
		return M ? M->CountDrawnMarks(EAsdaMapMark::Quest, true) + M->CountDrawnMarks(EAsdaMapMark::Quest2, true) : -1;
	}

	// Текстура подложки в окне 1710 (`MaxMap_MapImage`); nullptr — поле пустое (белое).
	static UTexture2D* ShownImage(FRun& R)
	{
		const UImage* Img = Cast<UImage>(Find(R, TEXT("MAXMAP_WIN"), TEXT("MaxMap_MapImage")));
		return Img ? Cast<UTexture2D>(Img->GetBrush().GetResourceObject()) : nullptr;
	}

	// Выбор области тем же вызовом, что строка списка «Область» (`MiniMap_BigMapWindowProc` ctrl 1723 → `MiniMap_OpenBigMap`),
	//   затем выборка нарисованных квест-меток ~1.5 с (они мигают 1000/200 мс) и сверка.
	//   WantMode: 1 — ровно по данным NPC карты; 0 — ключ отката, меток быть не должно.
	static void AddAreaCase(FRun& R, int32 AreaIdx, int32 WantMode)
	{
		R.Steps.Add([&R, AreaIdx0 = AreaIdx]() -> double
		{
			const int32 AreaIdx = (AreaIdx0 == -2) ? R.BestArea : AreaIdx0;   // −2 — область, выбранная в прогоне
			UAsdaMinimapSubsystem* M = MM(R);
			const TArray<FAsdaWorldMapArea>& Areas = M->GetWorldMapAreas();
			if (!Areas.IsValidIndex(AreaIdx)) { return 0.01; }
			const FAsdaWorldMapArea& A = Areas[AreaIdx];
			int32 InCont = 0;
			for (int32 i = 0; i < AreaIdx; ++i) { if (Areas[i].Continent == A.Continent) { ++InCont; } }
			M->SelectContinent(A.Continent);
			M->SelectArea(InCont);
			R.SampleMax = 0; R.SampleYellow = 0; R.SampleRed = 0;
			return 0.3;
		});
		for (int32 k = 0; k < 10; ++k)
		{
			R.Steps.Add([&R]() -> double
			{
				R.SampleMax = FMath::Max(R.SampleMax, DrawnQuestMarks(R));
				// Цвета отдельно: жёлтый «!» (`miniQuest`, метка 1 — можно взять), красный (`miniQuest2`, метка 2 — можно сдать).
				R.SampleYellow = FMath::Max(R.SampleYellow, MM(R)->CountDrawnMarks(EAsdaMapMark::Quest, true));
				R.SampleRed = FMath::Max(R.SampleRed, MM(R)->CountDrawnMarks(EAsdaMapMark::Quest2, true));
				return 0.15;
			});
		}
		R.Steps.Add([&R, AreaIdx0 = AreaIdx, WantMode]() -> double
		{
			const int32 AreaIdx = (AreaIdx0 == -2) ? R.BestArea : AreaIdx0;
			UAsdaMinimapSubsystem* M = MM(R);
			const TArray<FAsdaWorldMapArea>& Areas = M->GetWorldMapAreas();
			if (!Areas.IsValidIndex(AreaIdx)) { return 0.01; }
			const FAsdaWorldMapArea& A = Areas[AreaIdx];
			const int32 Shown = M->GetDisplayedMapId();
			const int32 Want = ExpectedMapMarks(R, A.MapId);
			const FString Lbl = FString::Printf(TEXT("W карта %d%s (область %d)"), A.MapId, A.bTown ? TEXT(" город") : TEXT(""), AreaIdx);
			UTexture2D* Tex = ShownImage(R);
			Expect(R, Shown == A.MapId && Tex != nullptr, FString::Printf(TEXT("%s: показана карта %d, подложка %s"), *Lbl, Shown,
				Tex ? *Tex->GetName() : TEXT("НЕТ (белое поле)")));
			if (WantMode == 0)
			{
				Expect(R, R.SampleMax == 0, FString::Printf(TEXT("%s: ключ asda.Map.OtherMapQuestMarks 0 — меток %d (дефект воспроизведён: ждали 0, по данным %d)"),
					*Lbl, R.SampleMax, Want));
				return 0.05;
			}
			// Городской вариант — увеличенный кусок карты: часть NPC за краем, поэтому «не больше» и «есть, если есть что показать».
			const bool bOk = A.bTown ? (R.SampleMax <= Want && (Want == 0 || R.SampleMax > 0)) : (R.SampleMax == Want);
			Expect(R, bOk, FString::Printf(TEXT("%s: квест-меток нарисовано %d, по данным NPC карты %d"), *Lbl, R.SampleMax, Want));
			if (!A.bTown)
			{
				const int32 W1 = ExpectedMapMarks(R, A.MapId, 1), W2 = ExpectedMapMarks(R, A.MapId, 2);
				Expect(R, R.SampleYellow == W1 && R.SampleRed == W2,
					FString::Printf(TEXT("%s: жёлтых «!» (взять) %d из %d, красных (сдать) %d из %d"), *Lbl, R.SampleYellow, W1, R.SampleRed, W2));
			}
			return 0.05;
		});
	}

	// ── ★ [ПОРТ] 31.10.2026 значки ЦЕЛЕЙ взятых заданий на карте мира ──────────────────────────────────────────────────────
	// Выбор области с этой картой тем же вызовом, что строка списка «Область».
	static bool SelectMapArea(FRun& R, int32 MapId)
	{
		UAsdaMinimapSubsystem* M = MM(R);
		const TArray<FAsdaWorldMapArea>& Areas = M->GetWorldMapAreas();
		for (int32 i = 0; i < Areas.Num(); ++i)
		{
			if (Areas[i].MapId != MapId || Areas[i].bTown) { continue; }
			int32 InCont = 0;
			for (int32 k = 0; k < i; ++k) { if (Areas[k].Continent == Areas[i].Continent) { ++InCont; } }
			M->SelectContinent(Areas[i].Continent);
			M->SelectArea(InCont);
			return true;
		}
		return false;
	}

	// Видимые значки целей (живые квадраты пула большой карты).
	static TArray<UImage*> DrawnTargets(FRun& R)
	{
		TArray<UImage*> Out;
		if (UAsdaMinimapSubsystem* M = MM(R)) { M->CountDrawnMarks(EAsdaMapMark::QuestTarget, true, &Out); }
		return Out;
	}

	// Случай значков целей: задание (номер, шаг), карта и ожидаемое число различных точек по данным.
	struct FTargetCase { FString Label; int32 Quest = -1, Stage = 0, Map = -1, Want = 0; };
	// Код эпизода задания (+0 ячейки журнала — по нему журнал находит книгу задания); у доски и без строки — 2001 (слот занят).
	static int32 EpisodeIdOf(int32 QuestNo)
	{
		if (const UDataTable* Ep = AsdaDt::Cached(TEXT("/Game/AsdaAssets/Data/NPC/DT_EpisodeTable.DT_EpisodeTable")))
		{
			for (const TPair<FName, uint8*>& It : Ep->GetRowMap())
			{
				const FAsdaEpisodeRow* R = reinterpret_cast<const FAsdaEpisodeRow*>(It.Value);
				if (R && R->QuestKey == QuestNo) { return R->EpisodeId; }
			}
		}
		return 2001;
	}

	// Журнал из списка (задание, шаг) — штатный `ApplyQuestStateBlob` (тот же путь, что ответ сервера 5047 / `asda.Quest.FakeState`).
	static void ApplyJournal(FRun& R, const TArray<TPair<int32, int32>>& Quests)
	{
		TArray<uint8> Blob; Blob.SetNumZeroed(688);
		auto W16 = [&Blob](int32 O, int32 V) { Blob[O] = V & 0xFF; Blob[O + 1] = (V >> 8) & 0xFF; };
		auto W32 = [&Blob](int32 O, int32 V) { for (int32 k = 0; k < 4; ++k) { Blob[O + k] = (V >> (8 * k)) & 0xFF; } };
		for (int32 i = 0; i < Quests.Num() && i < 12; ++i)
		{
			const int32 O = 44 * i;
			W32(O, EpisodeIdOf(Quests[i].Key)); W16(O + 5, i); W16(O + 8, Quests[i].Key); W16(O + 10, Quests[i].Value);
		}
		if (UAsdaQuestMarkerSubsystem* Q = R.GI->GetSubsystem<UAsdaQuestMarkerSubsystem>()) { Q->ApplyQuestStateBlob(Blob); }
	}

	static void BuildTargets(FRun& R)
	{
		// Значки целей — только у заданий в экранном трекере (решение владельца 31.10). Два задания в журнале: шаг «добыть с
		//   мобов» без координат гида (места мобов, Гора Пламень 12) и шаг с точкой гида (Флабис 13); отслеживание — тем же
		//   путём, что флажок 743 журнала (`UAsdaQuestLogSubsystem::SetSlotTracked`).
		R.Steps.Add([&R]() -> double { R.Keys->Set(TEXT("asda.Map.QuestTargets"), 1); return 0.05; });
		UAsdaQuestMarkerSubsystem* QM = R.GI->GetSubsystem<UAsdaQuestMarkerSubsystem>();
		const UDataTable* Guide = AsdaDt::Cached(TEXT("/Game/AsdaAssets/Data/Quest/DT_QuestGuide.DT_QuestGuide"));
		const UDataTable* Area = AsdaDt::Cached(TEXT("/Game/AsdaAssets/Data/Quest/DT_QuestTargetArea.DT_QuestTargetArea"));
		auto GuideTiles = [Guide](int32 Q, int32 S, int32 Map)
		{
			TSet<FIntPoint> T;
			if (!Guide) { return T; }
			for (const TPair<FName, uint8*>& It : Guide->GetRowMap())
			{
				const FAsdaQuestGuideRow* G = reinterpret_cast<const FAsdaQuestGuideRow*>(It.Value);
				if (G && G->QuestNo == Q && G->Stage == S && G->X != -1 && G->Y != -1 && (Map < 0 || G->MapId == Map)) { T.Add(FIntPoint(G->X, G->Y)); }
			}
			return T;
		};
		// A — места мобов: кандидаты (шаг без координат гида, задание существует у клиента); в прогоне берётся первый, который
		//   журнал даёт поставить в трекер (`CanTrackSlot`) — так же решает флажок 743.
		TSharedRef<FTargetCase> A = MakeShared<FTargetCase>();
		TSharedRef<TArray<FIntVector>> ACands = MakeShared<TArray<FIntVector>>();   // (задание, шаг, карта)
		for (int32 WantMap : { 12, 14, 13, 17 })
		{
			if (!Area) { break; }
			for (const TPair<FName, uint8*>& It : Area->GetRowMap())
			{
				const FAsdaQuestTargetAreaRow* Ar = reinterpret_cast<const FAsdaQuestTargetAreaRow*>(It.Value);
				if (!Ar || Ar->MapId != WantMap || GuideTiles(Ar->QuestNo, Ar->Stage, -1).Num() > 0) { continue; }
				const FString T = (Ar->QuestNo >= UAsdaBoardQuestSubsystem::kQuestBase) ? UAsdaBoardQuestSubsystem::QuestName(Ar->QuestNo)
					: (QM ? QM->EpisodeTitleByQuestKey(Ar->QuestNo) : FString());
				if (T.IsEmpty() || T.StartsWith(TEXT("#"))) { continue; }
				ACands->AddUnique(FIntVector(Ar->QuestNo, Ar->Stage, WantMap));
			}
		}
		R.Steps.Add([&R, A, ACands, Area]() -> double
		{
			UAsdaQuestMarkerSubsystem* Q = R.GI->GetSubsystem<UAsdaQuestMarkerSubsystem>();
			UAsdaQuestLogSubsystem* L = R.GI->GetSubsystem<UAsdaQuestLogSubsystem>();
			Exec(R, TEXT("asda.Quest.FakeSelf 150 1 0"));
			for (const FIntVector& C : *ACands)
			{
				ApplyJournal(R, { { C.X, C.Y } });
				if (Q && L && L->CanTrackSlot(0) && !Q->IsQuestObjectiveDone(C.X)) { A->Quest = C.X; A->Stage = C.Y; A->Map = C.Z; break; }
			}
			TSet<FIntPoint> Tiles;
			if (Area && A->Quest >= 0)
			{
				for (const TPair<FName, uint8*>& It : Area->GetRowMap())
				{
					const FAsdaQuestTargetAreaRow* Ar = reinterpret_cast<const FAsdaQuestTargetAreaRow*>(It.Value);
					if (Ar && Ar->QuestNo == A->Quest && Ar->Stage == A->Stage && Ar->MapId == A->Map) { Tiles.Add(FIntPoint(Ar->TileX, Ar->TileY)); }
				}
			}
			A->Want = Tiles.Num();
			A->Label = FString::Printf(TEXT("места мобов (задание %d, карта %d)"), A->Quest, A->Map);
			return 0.05;
		});
		// B — точка гида на Флабисе (13).
		TSharedRef<FTargetCase> B = MakeShared<FTargetCase>();
		if (Guide)
		{
			for (const TPair<FName, uint8*>& It : Guide->GetRowMap())
			{
				const FAsdaQuestGuideRow* G = reinterpret_cast<const FAsdaQuestGuideRow*>(It.Value);
				if (G && G->MapId == 13 && G->X != -1 && G->Y != -1 && G->QuestNo != A->Quest) { B->Quest = G->QuestNo; B->Stage = G->Stage; B->Map = 13; break; }
			}
			B->Want = GuideTiles(B->Quest, B->Stage, 13).Num();
		}
		B->Label = FString::Printf(TEXT("точка гида (задание %d, карта 13)"), B->Quest);

		// Журнал: два задания (5047 штатным `ApplyQuestStateBlob`, как `asda.Quest.FakeState`, но в двух ячейках), трекер пуст.
		R.Steps.Add([&R, A, B]() -> double
		{
			Exec(R, TEXT("asda.Quest.FakeSelf 150 1 0"));
			ApplyJournal(R, { { A->Quest, A->Stage }, { B->Quest, B->Stage } });
			if (UAsdaQuestMarkerSubsystem* Q = R.GI->GetSubsystem<UAsdaQuestMarkerSubsystem>()) { Q->TrackerClear(); }
			Expect(R, A->Quest >= 0 && B->Quest >= 0 && A->Want > 0 && B->Want > 0,
				FString::Printf(TEXT("T задания для проверки: %s — %d точек; %s — %d точек"), *A->Label, A->Want, *B->Label, B->Want));
			UAsdaMinimapSubsystem* M = MM(R);
			if (M && !M->IsBigMapOpen()) { M->ToggleBigMap(); }
			SelectMapArea(R, A->Map);
			return 0.8;
		});
		auto Track = [&R](int32 Slot, bool bOn) -> bool
		{
			UAsdaQuestLogSubsystem* L = R.GI->GetSubsystem<UAsdaQuestLogSubsystem>();
			return L && L->SetSlotTracked(Slot, bOn);
		};
		auto Count = [&R](const FString& What, int32 Want) -> double
		{
			const int32 N = DrawnTargets(R).Num();
			Expect(R, N == Want, FString::Printf(TEXT("T %s: значков целей %d (ждали %d)"), *What, N, Want));
			return 0.05;
		};
		R.Steps.Add([&R, A, Count]() -> double { return Count(FString::Printf(TEXT("в трекере пусто, карта %d"), A->Map), 0); });
		// Добавили в трекер — точки появились на ОТКРЫТОЙ карте, без повторного выбора области.
		R.Steps.Add([&R, Track]() -> double { Expect(R, Track(0, true), TEXT("T задание 1 поставлено в трекер (флажок журнала)")); return 0.4; });
		R.Steps.Add([&R, A, Count]() -> double { return Count(FString::Printf(TEXT("задание 1 в трекере, карта %d открыта"), A->Map), A->Want); });
		AddHoverCheck(R, A);
		// Второе задание: на его карте точек нет, пока не в трекере; оба в трекере — точки обоих.
		R.Steps.Add([&R, B]() -> double { SelectMapArea(R, B->Map); return 0.6; });
		R.Steps.Add([&R, B, Count]() -> double { return Count(FString::Printf(TEXT("задание 2 НЕ в трекере, карта %d"), B->Map), 0); });
		R.Steps.Add([&R, Track]() -> double { Expect(R, Track(1, true), TEXT("T задание 2 поставлено в трекер")); return 0.4; });
		R.Steps.Add([&R, B, Count]() -> double { return Count(FString::Printf(TEXT("оба в трекере, карта %d"), B->Map), B->Want); });
		AddHoverCheck(R, B);
		R.Steps.Add([&R, A]() -> double { SelectMapArea(R, A->Map); return 0.6; });
		R.Steps.Add([&R, A, Count]() -> double { return Count(FString::Printf(TEXT("оба в трекере, карта %d"), A->Map), A->Want); });
		// Убрали из трекера — точки пропали сразу.
		R.Steps.Add([&R, Track]() -> double { Expect(R, Track(0, false), TEXT("T задание 1 снято с трекера")); return 0.4; });
		R.Steps.Add([&R, A, Count]() -> double { return Count(FString::Printf(TEXT("задание 1 снято, карта %d открыта"), A->Map), 0); });
		// Ключ отката: значков целей нет и у отслеживаемого.
		R.Steps.Add([&R, B, Track]() -> double { Track(1, true); R.Keys->Set(TEXT("asda.Map.QuestTargets"), 0); SelectMapArea(R, B->Map); return 0.6; });
		R.Steps.Add([&R, Count]() -> double { const double P = Count(TEXT("ключ asda.Map.QuestTargets 0"), 0); R.Keys->Set(TEXT("asda.Map.QuestTargets"), 1); return P; });
		// ── Смена набора отслеживаемых (другой набор — очистка трекера и новый список, как при смене персонажа) ──
		R.Steps.Add([&R, B, Track]() -> double
		{
			if (UAsdaQuestMarkerSubsystem* Q = R.GI->GetSubsystem<UAsdaQuestMarkerSubsystem>()) { Q->TrackerClear(); }
			Expect(R, Track(1, true), TEXT("T другой набор: трекер очищен, поставлено только задание 2"));
			SelectMapArea(R, B->Map);
			return 0.6;
		});
		R.Steps.Add([&R, B, Count]() -> double { return Count(FString::Printf(TEXT("другой набор — задание 2 на карте %d"), B->Map), B->Want); });
		R.Steps.Add([&R, A]() -> double { SelectMapArea(R, A->Map); return 0.6; });
		R.Steps.Add([&R, A, Count]() -> double { return Count(FString::Printf(TEXT("другой набор — задания 1 нет, карта %d"), A->Map), 0); });
		// ── Сдано / брошено: журнал без задания (состояние, которое присылает сервер после 5042 / 5006) — точки уходят сразу ──
		R.Steps.Add([&R, A, B, Track]() -> double
		{
			Expect(R, Track(0, true), TEXT("T оба задания снова в трекере"));
			SelectMapArea(R, A->Map);
			return 0.6;
		});
		R.Steps.Add([&R, A, Count]() -> double { return Count(TEXT("перед сдачей задания 1"), A->Want); });
		R.Steps.Add([&R, B]() -> double { ApplyJournal(R, { { B->Quest, B->Stage } }); return 0.4; });
		R.Steps.Add([&R, A, Count]() -> double { return Count(FString::Printf(TEXT("задание 1 сдано (ячейка пуста), карта %d открыта"), A->Map), 0); });
		R.Steps.Add([&R, B]() -> double { SelectMapArea(R, B->Map); return 0.6; });
		R.Steps.Add([&R, B, Count]() -> double { return Count(TEXT("задание 2 ещё в журнале"), B->Want); });
		R.Steps.Add([&R]() -> double { ApplyJournal(R, {}); return 0.4; });
		R.Steps.Add([&R, B, Count]() -> double { return Count(FString::Printf(TEXT("задание 2 брошено (журнал пуст), карта %d открыта"), B->Map), 0); });

		// ── Прогресс: цель выполнена → журнал сам снимает задание с трекера (эталон), точка переходит на NPC сдачи ──
		TSharedRef<FTargetCase> D = MakeShared<FTargetCase>();
		R.Steps.Add([&R, D]() -> double
		{
			// Кандидат по данным в прогоне: задание, которое принимает NPC карты из «Области», на одном шаге ещё НЕ выполнено и
			//   отслеживается (`CanTrackSlot`), а на следующем — выполнено (`IsQuestObjectiveDone`).
			UAsdaQuestMarkerSubsystem* Q = R.GI->GetSubsystem<UAsdaQuestMarkerSubsystem>();
			UAsdaQuestLogSubsystem* L = R.GI->GetSubsystem<UAsdaQuestLogSubsystem>();
			const UDataTable* Snq = AsdaDt::Cached(TEXT("/Game/AsdaAssets/Data/NPC/DT_NpcSnq.DT_NpcSnq"));
			if (!Q || !L || !Snq) { return 0.01; }
			Q->EnsureTables();
			for (const TPair<FName, uint8*>& It : Snq->GetRowMap())
			{
				const FAsdaNpcSnqRow* Row = reinterpret_cast<const FAsdaNpcSnqRow*>(It.Value);
				if (!Row || D->Quest >= 0) { continue; }
				const FAsdaNpcQuestNpcRow* Npc = nullptr;
				for (const FAsdaNpcQuestNpcRow* N : Q->NpcRows)
				{
					if (N && N->NpcId == Row->NpcId && MM(R)->GetWorldMapAreas().ContainsByPredicate([N](const FAsdaWorldMapArea& A) { return A.MapId == N->MapId && !A.bTown; })) { Npc = N; break; }
				}
				if (!Npc) { continue; }
				for (const FAsdaSnqEntry& E : Row->Take)
				{
					if (E.QuestId <= 0 || !E.Keys.ContainsByPredicate([](int32 K) { return K > 0; })) { continue; }
					for (int32 St = 0; St < 4 && D->Quest < 0; ++St)
					{
						ApplyJournal(R, { { E.QuestId, St } });
						if (Q->IsQuestObjectiveDone(E.QuestId) || !L->CanTrackSlot(0)) { continue; }
						ApplyJournal(R, { { E.QuestId, St + 1 } });
						if (!Q->IsQuestObjectiveDone(E.QuestId)) { continue; }
						D->Quest = E.QuestId; D->Stage = St; D->Map = Npc->MapId;
					}
					if (D->Quest >= 0) { break; }
				}
			}
			Expect(R, D->Quest >= 0, FString::Printf(TEXT("T прогресс: найдено задание %d (шаг %d не выполнен, шаг %d выполнен), NPC сдачи на карте %d"),
				D->Quest, D->Stage, D->Stage + 1, D->Map));
			if (D->Quest < 0) { return 0.01; }
			ApplyJournal(R, { { D->Quest, D->Stage } });
			if (UAsdaQuestLogSubsystem* L2 = R.GI->GetSubsystem<UAsdaQuestLogSubsystem>()) { L2->SetSlotTracked(0, true); }
			SelectMapArea(R, D->Map);
			return 0.6;
		});
		R.Steps.Add([&R, D]() -> double
		{
			// Цель выполнена — новое состояние журнала (как 5052/5047 сервера); журнал по `OnQuestStateChanged` сам снимает
			//   задание с трекера, точка остаётся — на NPC, который его принимает.
			const bool bTracked = R.GI->GetSubsystem<UAsdaQuestMarkerSubsystem>()->IsTrackedQuest(D->Quest);
			Expect(R, bTracked, FString::Printf(TEXT("T прогресс: задание %d в трекере до выполнения"), D->Quest));
			ApplyJournal(R, { { D->Quest, D->Stage + 1 } });
			return 0.5;
		});
		R.Steps.Add([&R, D]() -> double
		{
			UAsdaQuestMarkerSubsystem* Q = R.GI->GetSubsystem<UAsdaQuestMarkerSubsystem>();
			const bool bAuto = !Q->IsTrackedQuest(D->Quest);
			bool bNpcTip = false;
			if (const TArray<FAsdaMapMark>* L = MM(R)->Marks.Find((uint8)EAsdaMapMark::QuestTarget))
			{
				for (const FAsdaMapMark& Mk : *L) { if (Mk.MapId == D->Map) { bNpcTip = true; } }
			}
			Expect(R, bAuto && bNpcTip && DrawnTargets(R).Num() > 0,
				FString::Printf(TEXT("T прогресс: цель выполнена — трекер снял задание сам=%d, точка NPC сдачи на открытой карте %d есть=%d (нарисовано %d)"),
					bAuto ? 1 : 0, D->Map, bNpcTip ? 1 : 0, DrawnTargets(R).Num()));
			ApplyJournal(R, {});   // сдали — точка уходит
			return 0.4;
		});
		R.Steps.Add([&R, D, Count]() -> double { return Count(TEXT("прогресс: задание сдано — точки NPC сдачи нет"), 0); });

		R.Steps.Add([&R]() -> double { if (UAsdaQuestMarkerSubsystem* Q = R.GI->GetSubsystem<UAsdaQuestMarkerSubsystem>()) { Q->TrackerClear(); } return 0.05; });
	}

	// Правый щелчок по первому значку цели (курсор встаёт на значок, действий на карте нет) → подсказка с названием задания.
	static void AddHoverCheck(FRun& R, TSharedRef<FTargetCase> C)
	{
		R.Steps.Add([&R]() -> double
		{
			const TArray<UImage*> Img = DrawnTargets(R);
			FVector2D A, B;
			if (Img.Num() > 0 && AsdaUi::WidgetAbsRect(Img[0], A, B))
			{
				WM(R)->SimulateClickAt((A + B) * 0.5, /*HoldFrames*/30, /*bRightButton*/true);
				R.bWaitClick = true;
				return -1.0;
			}
			return 0.1;
		});
		R.Steps.Add([&R, C]() -> double
		{
			const UAsdaItemTooltipSubsystem* Tip = R.GI->GetSubsystem<UAsdaItemTooltipSubsystem>();
			const UAsdaQuestMarkerSubsystem* QM = R.GI->GetSubsystem<UAsdaQuestMarkerSubsystem>();
			const FString Title = !QM ? FString() : (C->Quest >= UAsdaBoardQuestSubsystem::kQuestBase
				? UAsdaBoardQuestSubsystem::QuestName(C->Quest) : QM->EpisodeTitleByQuestKey(C->Quest));
			const bool bShown = Tip && Tip->IsTooltipShown() && Tip->GetTipOwner() == EAsdaTipOwner::Minimap;
			FString Got;
			if (const TArray<FAsdaMapMark>* L = MM(R)->Marks.Find((uint8)EAsdaMapMark::QuestTarget))
			{
				for (const FAsdaMapMark& Mk : *L) { if (Mk.MapId == C->Map) { Got = Mk.TipTitle + TEXT(" | ") + Mk.TipBody; break; } }
			}
			Expect(R, bShown && !Title.IsEmpty() && Got.Contains(Title) && Got.Contains(TEXT("=> ")),
				FString::Printf(TEXT("T %s: наведение — подсказка показана=%d, текст «%s»"), *C->Label, bShown ? 1 : 0, *Got));
			return 0.1;
		});
	}

	static void BuildWorldMap(FRun& R)
	{
		// Большую карту рисует её тик, когда есть номер карты Load Manager, свой павн и сетка карты (`UpdateBigMap`). Офлайн павна
		//   нет — штатный офлайн-спавн (как `asda.Action.SelfTest`): карта 3 (Альпия), клетка (256,256).
		R.Steps.Add([&R]() -> double
		{
			UWorld* W = World(R);
			if (W && AsdaSelfTestKit::NeedSelfSpawn(W))
			{
				Exec(R, TEXT("Asda.Load.CurrentMap 3"));
				Exec(R, TEXT("Asda.World.FakeSelfSpawn 256 256"));
			}
			return 2.0;
		});
		R.Steps.Add([&R]() -> double
		{
			// Персонаж 60-го уровня с пустым журналом: «!» у множества квестодателей на всех картах (состояние 5047 офлайн).
			Exec(R, TEXT("asda.Quest.FakeSelf 60 1 0"));
			Exec(R, TEXT("asda.Quest.FakeState 999 0"));
			R.Keys->Set(TEXT("asda.Map.OtherMapQuestMarks"), 1);
			R.Keys->Set(TEXT("asda.Map.BigImageFromMini"), 1);
			UAsdaMinimapSubsystem* M = MM(R);
			if (M && !M->IsBigMapOpen()) { M->ToggleBigMap(); }   // то же, что клавиша M
			return 0.8;
		});
		R.Steps.Add([&R]() -> double
		{
			UAsdaMinimapSubsystem* M = MM(R);
			Expect(R, M && M->IsBigMapOpen() && M->GetWorldMapAreas().Num() > 0,
				FString::Printf(TEXT("W карта мира открыта, областей в списке %d"), M ? M->GetWorldMapAreas().Num() : 0));
			int32 Total = 0;
			if (M) { for (const FAsdaWorldMapArea& A : M->GetWorldMapAreas()) { Total += FMath::Max(0, ExpectedMapMarks(R, A.MapId)); } }
			Expect(R, Total > 0, FString::Printf(TEXT("W по данным есть что показывать: квест-меток по всем областям %d"), Total));
			return 0.05;
		});
		// Все области обоих континентов (список областей — тот, что видит игрок).
		const int32 NumAreas = MM(R) ? MM(R)->GetWorldMapAreas().Num() : 0;
		for (int32 i = 0; i < NumAreas; ++i) { AddAreaCase(R, i, 1); }
		// Высокоуровневая чужая карта: FirewayForest (16) — квестодатели только для 140+ (на 60-м меток по данным нет).
		// ★ 06.11.2026: у 6 NPC этой карты записи меток 140+ ведут на страницы 10–12, которых нет ни в одной сборке, заданий у них нет
		//   (`DT_NpcSnq` пусто). С 04.11 (`asda.Npc.SkipMissingPage`) таких меток нет — это верно: сверка с данными при ключе 1, затем
		//   контроль при ключе 0 — проверка видит метки высокоуровневой карты.
		R.Steps.Add([&R]() -> double { Exec(R, TEXT("asda.Quest.FakeSelf 145 1 0")); return 0.1; });
		if (UAsdaMinimapSubsystem* M16 = MM(R))
		{
			const TArray<FAsdaWorldMapArea>& Areas = M16->GetWorldMapAreas();
			for (int32 i = 0; i < Areas.Num(); ++i) { if (Areas[i].MapId == 16) { AddAreaCase(R, i, 1); break; } }
		}
		R.Steps.Add([&R]() -> double
		{
			Expect(R, R.SampleMax == 0, FString::Printf(TEXT("W FirewayForest (16) персонажем 145-го уровня: меток без страницы нет (%d)"), R.SampleMax));
			R.Keys->Set(TEXT("asda.Npc.SkipMissingPage"), 0);
			return 0.1;
		});
		if (UAsdaMinimapSubsystem* M16 = MM(R))
		{
			const TArray<FAsdaWorldMapArea>& Areas = M16->GetWorldMapAreas();
			for (int32 i = 0; i < Areas.Num(); ++i) { if (Areas[i].MapId == 16) { AddAreaCase(R, i, 1); break; } }
		}
		R.Steps.Add([&R]() -> double
		{
			UAsdaMinimapSubsystem* M = MM(R);
			Expect(R, M && M->GetDisplayedMapId() == 16 && R.SampleMax > 0,
				FString::Printf(TEXT("W FirewayForest (16) персонажем 145-го уровня, ключ SkipMissingPage 0 (контроль): квест-меток нарисовано %d (> 0)"), R.SampleMax));
			R.Keys->Set(TEXT("asda.Npc.SkipMissingPage"), 1);
			Exec(R, TEXT("asda.Quest.FakeSelf 60 1 0"));
			return 0.1;
		});
		// Ключ отката на чужой карте с метками: дефект воспроизводится (проверка его ловит).
		R.Steps.Add([&R]() -> double { R.Keys->Set(TEXT("asda.Map.OtherMapQuestMarks"), 0); return 0.05; });
		R.Steps.Add([&R]() -> double
		{
			UAsdaMinimapSubsystem* M = MM(R);
			const TArray<FAsdaWorldMapArea>& Areas = M->GetWorldMapAreas();
			const UAsdaLoadManagerSubsystem* LM = R.GI->GetSubsystem<UAsdaLoadManagerSubsystem>();
			const int32 CurMap = LM ? LM->CurrentMapId : -1;
			R.BestArea = -1;
			for (int32 i = 0; i < Areas.Num() && R.BestArea < 0; ++i)
			{
				if (!Areas[i].bTown && Areas[i].MapId != CurMap && ExpectedMapMarks(R, Areas[i].MapId) > 0) { R.BestArea = i; }
			}
			Expect(R, R.BestArea >= 0, FString::Printf(TEXT("W для ключа отката найдена чужая карта с метками (область %d)"), R.BestArea));
			return 0.05;
		});
		AddAreaCase(R, -2, 0);
		R.Steps.Add([&R]() -> double { R.Keys->Set(TEXT("asda.Map.OtherMapQuestMarks"), 1); return 0.05; });
		// Турнирные карты 10/11 (в список областей оригинал их не включает) — подмешиваем строки и выбираем тем же вызовом.
		R.Steps.Add([&R]() -> double
		{
			UAsdaMinimapSubsystem* M = MM(R);
			R.SavedAreas = M->GetWorldMapAreas();
			TArray<FAsdaWorldMapArea> Ext = R.SavedAreas;
			for (int32 Id : { 10, 11 })
			{
				FAsdaWorldMapArea A;
				A.Continent = 0; A.MapId = Id; A.bTown = false; A.TextId = R.SavedAreas.Num() > 0 ? R.SavedAreas[0].TextId : 0;
				Ext.Add(A);
			}
			M->SetWorldMapAreas(Ext);
			return 0.05;
		});
		for (int32 j = 0; j < 2; ++j)
		{
			R.Steps.Add([&R, j]() -> double
			{
				UAsdaMinimapSubsystem* M = MM(R);
				int32 InCont = -1;
				for (const FAsdaWorldMapArea& A : M->GetWorldMapAreas()) { if (A.Continent == 0) { ++InCont; } }
				M->SelectContinent(0);
				M->SelectArea(InCont - 1 + j);   // две последние строки континента 0 — подмешанные 10 и 11
				return 0.4;
			});
			R.Steps.Add([&R, j]() -> double
			{
				const int32 Id = 10 + j;
				UTexture2D* Tex = ShownImage(R);
				const FString Want = (Id == 10) ? TEXT("t_palace_of_queen") : TEXT("t_castle_of_chess");
				Expect(R, MM(R)->GetDisplayedMapId() == Id && Tex && Tex->GetName().Equals(Want, ESearchCase::IgnoreCase),
					FString::Printf(TEXT("W турнирная карта %d: подложка %s (ждали картинку мини-карты %s)"), Id, Tex ? *Tex->GetName() : TEXT("НЕТ"), *Want));
				return 0.05;
			});
		}
		R.Steps.Add([&R]() -> double
		{
			R.Keys->Set(TEXT("asda.Map.BigImageFromMini"), 0);
			UAsdaMinimapSubsystem* M = MM(R);
			int32 InCont = -1;
			for (const FAsdaWorldMapArea& A : M->GetWorldMapAreas()) { if (A.Continent == 0) { ++InCont; } }
			M->SelectArea(InCont);   // карта 11 ещё раз, без запасной подложки
			return 0.4;
		});
		R.Steps.Add([&R]() -> double
		{
			Expect(R, ShownImage(R) == nullptr, TEXT("W ключ asda.Map.BigImageFromMini 0 — у турнирной карты подложки нет (дефект воспроизведён)"));
			R.Keys->Set(TEXT("asda.Map.BigImageFromMini"), 1);
			MM(R)->SetWorldMapAreas(R.SavedAreas);
			return 0.1;
		});
		BuildTargets(R);   // ★ 31.10.2026 значки целей взятых заданий
		R.Steps.Add([&R]() -> double { if (MM(R)->IsBigMapOpen()) { MM(R)->ToggleBigMap(); } return 0.1; });
	}

	static void Finish(FRun& R)
	{
		UGameInstance* GI = R.GI.Get();
		if (GI)
		{
			if (UAsdaNetSubsystem* Net = AsdaTestNet::Get(GI)) { AsdaSelfTestKit::DisarmCapture(Net, R.Gen); }
			if (UAsdaNpcDialogSubsystem* D = GI->GetSubsystem<UAsdaNpcDialogSubsystem>())
			{
				D->DebugHideRewardPopup();
				D->Close();
				D->ResetSessionState(EAsdaSessionResetReason::LeftWorld);
			}
			if (UAsdaQuestMarkerSubsystem* QM = GI->GetSubsystem<UAsdaQuestMarkerSubsystem>()) { QM->ResetSessionState(EAsdaSessionResetReason::LeftWorld); }
		}
		UWorld* W = GI ? GI->GetWorld() : nullptr;
		if (W && GEngine) { GEngine->Exec(W, TEXT("asda.Quest.FakeSelf")); GEngine->Exec(W, TEXT("r.SetRes 1024x768w")); }
		R.Keys.Reset();
		AsdaSelfTestKit::Finish(W, Cmd(), R.bQuit, LogAsdaQuestDlgTest, TagRef(), R.Ok, R.Fail);
		Run().Reset();
	}

	static void Build(FRun& R)
	{
		// ═══ Баг 2 ═══
		const FString Lv20 = TEXT("asda.Quest.FakeSelf 20 1 0"), Novice = TEXT("asda.Quest.FakeSelf 20 0 0");
		AddAcceptCase(R, TEXT("D1 класс 99, исправление"), 48, 27, Lv20, FString(), 1, 1, /*ячейка*/1, /*стр.*/27);
		AddAcceptCase(R, TEXT("D2 класс 99, ключи 0 (оригинал)"), 48, 27, Lv20, FString(), 0, 0, -1, 28);
		AddAcceptCase(R, TEXT("D3 новичок, исправление"), 48, 27, Novice, FString(), 1, 1, -1, 27);
		AddAcceptCase(R, TEXT("D4 новичок, AcceptFailStay 0"), 48, 27, Novice, FString(), 1, 0, -1, 28);
		const FString Lv40 = TEXT("asda.Quest.FakeSelf 40 1 0");
		AddAcceptCase(R, TEXT("D5 NPC 127 «Поединок 1»"), 127, 8, Lv40, FString(), 1, 1, 1, 8);
		AddAcceptCase(R, TEXT("D6 NPC 119 «Любовь и бумажные самолётики 1»"), 119, 5, Lv40, FString(), 1, 1, 0, 5);
		AddAcceptCase(R, TEXT("D7 NPC 128 «Новый поединок 1»"), 128, 15, Lv40, TEXT("asda.Quest.FakeState 1 0 430"), 1, 1, 1, 15);
		AddAcceptCase(R, TEXT("D8 NPC 130 «Любовь и бумажные самолётики 5»"), 130, 45, Lv40, TEXT("asda.Quest.FakeState 1 0 439"), 1, 1, 5, 45);
		AddAcceptCase(R, TEXT("D9 NPC 126 «После поединка 1»"), 126, 29, Lv40, TEXT("asda.Quest.FakeState 1 0 433"), 1, 1, 2, 29);

		// ═══ Баг 3 ═══ (Δ панели выбора: 1–2 базовых → 100, 3–4 → 135)
		R.Steps.Add([&R]() -> double { R.Keys->Set(TEXT("asda.UI.CellGridSyncOverlay"), 1); return 0.05; });
		const FString Ella = TEXT("asda.Quest.FakeSelf 20 1 0"), EllaQ = TEXT("asda.Quest.FakeState 130 0");
		AddRewardCase(R, TEXT("R1 Элла «A Right To Create Clan» (2 базовых, 3 на выбор)"), 107, 48, Ella, EllaQ, 3, true);
		AddRewardCase(R, TEXT("R2 Куриан «Faith Follower» (3 на выбор)"), 19, 26, TEXT("asda.Quest.FakeSelf 30 12 0"),
			TEXT("asda.Quest.FakeState 56 2"), 3, true);
		AddRewardCase(R, TEXT("R3 Эко (2 на выбор)"), 101, 31, TEXT("asda.Quest.FakeSelf 30 1 0"), TEXT("asda.Quest.FakeState 227 1"), 2, true);
		AddRewardCase(R, TEXT("R4 Зебиан (1 на выбор)"), 42, 5, TEXT("asda.Quest.FakeSelf 40 1 0"), TEXT("asda.Quest.FakeState 356 1"), 1, true);
		AddRewardCase(R, TEXT("R5 Чмелла (3 базовых, 2 на выбор)"), 20, 25, TEXT("asda.Quest.FakeSelf 30 23 0"),
			TEXT("asda.Quest.FakeState 57 2"), 2, true);
		// После перетаскивания окна за ручку — зоны едут вместе со строками.
		AddRewardCase(R, TEXT("R6 Элла перед перетаскиванием"), 107, 48, Ella, EllaQ, 3, false);
		R.Steps.Add([&R]() -> double
		{
			FVector2D A, B;
			if (!WM(R)->DebugGetDragHandleRect(FName(TEXT("EPISODEPOP_WIN")), 0, A, B))
			{
				Expect(R, false, TEXT("R6 перетаскивание: ручки окна 540 нет или она не отрисована"));
				return 0.1;
			}
			const FVector2D From((A.X + B.X) * 0.5, A.Y + 6.0);   // верхняя кромка ручки — над ней нет кнопок
			FVector2D Ra, Rb;
			R.RowBeforeDrag = AsdaUi::WidgetAbsRect(Find(R, TEXT("EPISODEPOP_WIN"), TEXT("EPISODEPOP_ITEMBACK06_grp")), Ra, Rb) ? Ra : FVector2D(-1.0, -1.0);
			WM(R)->SimulateDrag(From, From + FVector2D(-120.0, 70.0), /*HoldFrames*/12);
			R.bWaitClick = true;
			return -1.0;
		});
		R.Steps.Add([&R]() -> double
		{
			// Окно действительно уехало (иначе проверка зон ниже прошла бы вхолостую).
			FVector2D Ra, Rb;
			const bool bRow = AsdaUi::WidgetAbsRect(Find(R, TEXT("EPISODEPOP_WIN"), TEXT("EPISODEPOP_ITEMBACK06_grp")), Ra, Rb);
			const FVector2D Moved = bRow ? Ra - R.RowBeforeDrag : FVector2D::ZeroVector;
			Expect(R, bRow && R.RowBeforeDrag.X >= 0.0 && Moved.Size() > 30.0,
				FString::Printf(TEXT("R6 перетаскивание сдвинуло окно: строка 0 уехала на (%+.0f,%+.0f)"), Moved.X, Moved.Y));
			CheckZones(R, TEXT("R6 после перетаскивания"), 3, true);
			return 0.05;
		});
		// Другое разрешение.
		R.Steps.Add([&R]() -> double { Exec(R, TEXT("r.SetRes 1600x900w")); return 1.5; });
		AddRewardCase(R, TEXT("R7 Элла при 1600×900"), 107, 48, Ella, EllaQ, 3, true);
		R.Steps.Add([&R]() -> double { Exec(R, TEXT("r.SetRes 1024x768w")); return 1.5; });
		// Ключ отката на СВЕЖЕМ окне (зоны строятся заново в `EnsureRewardWindow`): зоны остаются на запечённом месте —
		//   дефект воспроизводится, то есть проверка его действительно ловит.
		R.Steps.Add([&R]() -> double
		{
			R.Keys->Set(TEXT("asda.UI.CellGridSyncOverlay"), 0);
			if (UAsdaNpcDialogSubsystem* D = Dlg(R)) { D->DebugHideRewardPopup(); }
			if (UAsdaWindowSubsystem* W = WM(R)) { W->CloseWindow(FName(TEXT("EPISODEPOP_WIN"))); }
			return 0.3;
		});
		AddRewardCase(R, TEXT("R8 ключ 0"), 107, 48, Ella, EllaQ, 3, false);
		R.Steps.RemoveAt(R.Steps.Num() - 1);   // вместо проверки «зоны на месте» — проверка «зоны уехали»
		R.Steps.Add([&R]() -> double { CheckZones(R, TEXT("R8 ключ 0"), 3, /*bWantAligned*/false); R.Keys->Set(TEXT("asda.UI.CellGridSyncOverlay"), 1); return 0.05; });

		// ═══ Метки «!» и вход по записям сценария, выровненным по серверу (ведомости Tools/data/nsr_*_fixes.json) ═══
		R.Steps.Add([&R]() -> double
		{
			R.Keys->Set(TEXT("asda.Quest.Class99AnyJob"), 1);
			R.Keys->Set(TEXT("asda.Quest.AcceptFailStay"), 1);
			if (UAsdaNpcDialogSubsystem* D = Dlg(R)) { D->DebugHideRewardPopup(); }
			return 0.05;
		});
		BuildMarks(R);
	}

	static bool OnTick(float)
	{
		if (!Run().IsValid()) { return false; }
		FRun& R = *Run();
		if (!R.GI.IsValid()) { Run().Reset(); return false; }
		const double Now = FPlatformTime::Seconds();
		if (R.bWaitClick)
		{
			UAsdaWindowSubsystem* W = WM(R);
			if (W && W->IsSimClickPending()) { return true; }
			R.bWaitClick = false;
			R.WakeAt = Now + 0.25;   // кадры на обработку клика
			return true;
		}
		if (Now < R.WakeAt) { return true; }
		if (!R.Steps.IsValidIndex(R.Next)) { Finish(R); return false; }
		const double Pause = R.Steps[R.Next++]();
		if (Pause >= 0.0) { R.WakeAt = Now + Pause; }
		return true;
	}

	static void Start(const TArray<FString>& A, UWorld* W, bool bWorldMap)
	{
		if (!Run().IsValid())
		{
			CmdRef() = bWorldMap ? TEXT("asda.SelfTest.WorldMapMarks") : TEXT("asda.SelfTest.QuestDialogReward");
			TagRef() = bWorldMap ? TEXT("КАРТА-МИРА-МЕТКИ") : TEXT("КВЕСТ-ДИАЛОГ");
		}
		const FAsdaSelfTestArgs Args = FAsdaSelfTestArgs::Parse(A);
		if (AsdaSelfTestKit::RefuseStart(W, Cmd(), Args)) { return; }
		UGameInstance* GI = W ? W->GetGameInstance() : nullptr;
		if (!GI || !GI->GetSubsystem<UAsdaNpcDialogSubsystem>()) { AsdaSelfTestKit::Refused(W, Cmd(), Args.bQuit, TEXT("нет подсистемы диалога NPC")); return; }
		if (Run().IsValid()) { AsdaSelfTestKit::Refused(W, Cmd(), Args.bQuit, TEXT("самотест уже идёт")); return; }
		Run() = MakeShared<FRun>();
		FRun& R = *Run();
		R.GI = GI; R.bQuit = Args.bQuit;
		R.Caps = MakeShared<TArray<FCap>>();
		R.Keys = MakeUnique<FAsdaCVarScope>(W, Cmd());
		R.Keys->Set(TEXT("asda.Quest.InFlightTimeoutSec"), 0.05f);
		TSharedPtr<TArray<FCap>> C = R.Caps;
		R.Gen = AsdaSelfTestKit::ArmCapture(AsdaTestNet::Get(GI), [C](uint16 Op, const uint8* P, int32 Len, bool, TCHAR)
		{
			FCap X; X.Op = Op; if (P && Len > 0) { X.P.Append(P, Len); } C->Add(MoveTemp(X));
		}, 0, Cmd());
		if (R.Gen == 0) { Run().Reset(); AsdaSelfTestKit::Refused(W, Cmd(), Args.bQuit, TEXT("перехват отправок занят")); return; }
		if (bWorldMap) { BuildWorldMap(R); } else { Build(R); }
		FTSTicker::GetCoreTicker().AddTicker(FTickerDelegate::CreateStatic(&FAsdaQuestDialogRewardSelfTest::OnTick), 0.05f);
	}
};

static FAutoConsoleCommandWithWorldAndArgs GAsdaSelfTestQuestDialogReward(
	TEXT("asda.SelfTest.QuestDialogReward"),
	TEXT("Самотест взятия квеста (класс 99, ячейки выдачи по серверу, шаг «Принять» с невыполненным условием) и зон клика выбора "
	     "награды окна 540 (1–3 строки, перетаскивание, 1600×900, ключ отката). Только офлайн. `quit`."),
	FConsoleCommandWithWorldAndArgsDelegate::CreateLambda([](const TArray<FString>& A, UWorld* W)
	{
		FAsdaQuestDialogRewardSelfTest::Start(A, W, /*bWorldMap*/false);
	}));

static FAutoConsoleCommandWithWorldAndArgs GAsdaSelfTestWorldMapMarks(
	TEXT("asda.SelfTest.WorldMapMarks"),
	TEXT("Самотест карты мира 1710: по ВСЕМ областям обоих континентов (выбор — тем же вызовом, что список «Область») метки квестов "
	     "NPC показанной карты нарисованы, подложка есть; турнирные карты 10/11 — подложка из мини-карты; ключи отката. Только офлайн. `quit`."),
	FConsoleCommandWithWorldAndArgsDelegate::CreateLambda([](const TArray<FString>& A, UWorld* W)
	{
		FAsdaQuestDialogRewardSelfTest::Start(A, W, /*bWorldMap*/true);
	}));

#endif // ASDA_DEV_CONSOLE && !UE_BUILD_SHIPPING
