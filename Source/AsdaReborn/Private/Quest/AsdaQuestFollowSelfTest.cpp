// Copyright (c) 2026 @sitxovski (Discord)
// ★★★ САМОТЕСТ «СЛЕДОВАТЬ» ЖУРНАЛА ЗАДАНИЙ ПУТЁМ ИГРОКА (31.10.2026, баг 6 владельца: у части заданий цель — точка, область
//   или моб — в одном месте, а кнопка маршрута ведёт в другое место или к другому мобу).
//
//   КОРЕНЬ (аудит всех 803 строк гида — `Tools/asda_quest_follow_audit.py`, `Reports/bug_quest_follow/`):
//     1) данные гида оригинала `QDT.bin`: строки боссов перепутаны (464/467 — Виглаф ↔ Огненный владыка), точка в 48–131
//        клетке от спавна цели (328, 336, 20002, 20039, 20045, 20056), ID тёзки вместо NPC шага (52/250: 82→83, 55: 68→70) —
//        ведомость `Tools/data/quest_guide_fixes.json` [ИСПРАВЛЕНИЕ], ключа нет (откат — переимпорт без ведомости);
//     2) 35 строк без координат (мобы и NPC дворца королевы, замка шахмат, гор Пламении, Флэммио 566/568): кнопка не вела
//        никуда, а карта мира показывала цель — теперь место цели из общей точки `AsdaQuestGuideTarget` (ключ
//        `asda.Quest.FollowNoCoordTarget`, 0 — прежнее «идти некуда»).
//
//   ПУТЬ ИГРОКА: журнал — штатным приёмом состояния заданий (`ApplyQuestStateBlob`, как 5047); окно 700 открыто; выбор ячейки
//   и кнопка маршрута 705…708 — НАСТОЯЩИЕ щелчки Slate (`SimulateClickAt`) по живой геометрии. Результат читается там, где его
//   оставляет боевой код: своя карта — флажок цели мини-карты (`GetDestinationTile`), чужая — заявка межкартового автопути
//   (`DescribeMapTravel`). Оракул — клетки, выписанные здесь из серверных данных (спавн `<карта>.svr`, NPC `NpcInfo/*.npc`),
//   НЕ из таблиц, которые читает боевой код.
//   Разделы: A контроль строки гида без правки (своя карта); B перепутанные боссы; C точка вдали от спавна; D ID тёзки NPC;
//   E моб без координат (одна зона / самая населённая зона); F NPC без координат (значка на карте мира у такого шага нет:
//   значки — только у отслеживаемых заданий, а разговорный шаг без строк целей журнал в трекер не ставит);
//   G ключ `asda.Quest.FollowNoCoordTarget 0` — прежнее поведение; H цель шага выполнена (решение владельца) — кнопка
//   показывает NPC сдачи (имя, карта, клетка) и ведёт к нему: переход «не выполнено → выполнено» пакетом прогресса 5052 при
//   ОТКРЫТОМ журнале (подпись меняется без переоткрытия), гид называет другого NPC (394: гид 181, шаг принимает 180 —
//   сервер), ключ `asda.Quest.FollowDoneToNpc 0` — как в оригинале; S отбор NPC шага (тот же вычислитель, что у облачка
//   сдачи) на 17 парах «задание/стадия» против серверной `ConditionTable.Bin` (+57 задание, +65 стадия ячейки).
//
// Консоль: `asda.SelfTest.QuestFollow [delay=N] [quit]`. Только офлайн (`-AsdaNoNet`), на карте мира.
//   Строки проверок — «СЛЕДОВАТЬ-ТЕСТ ok/FAIL», итог — «СЛЕДОВАТЬ-ТЕСТ ИТОГ: ok=… fail=…».
#include "CoreMinimal.h"
#include "AsdaDevConsole.h"
#include "Test/AsdaSelfTestCtx.h"   // ★ R5-c (12.11.2026): общая точка самотестов вместо своих копий помощников
#include "Test/AsdaSelfTestInput.h"   // ★ R5-c (12.11.2026): общая точка самотестов вместо своих копий помощников

#if ASDA_DEV_CONSOLE && !UE_BUILD_SHIPPING

#include "AsdaBytes.h"
#include "AsdaCVarSave.h"
#include "AsdaDataTableCache.h"
#include "Test/AsdaSelfTestTree.h"   // папка проекта в дереве (ASDA_ROOT) — ведомости-оракулы и для собранной игры
#include "Test/AsdaSelfTestKit.h"
#include "Npc/AsdaNpcDialogTypes.h"
#include "Quest/AsdaQuestLogSubsystem.h"
#include "Quest/AsdaQuestMarkerSubsystem.h"
#include "UI/AsdaMinimapSubsystem.h"
#include "UI/AsdaUiHelpers.h"
#include "UI/AsdaWindowSubsystem.h"
#include "World/AsdaLoadManagerSubsystem.h"
#include "World/AsdaPlayerCharacter.h"
#include "World/AsdaCoord.h"
#include "Quest/AsdaQuestGuideTarget.h"
#include "Npc/AsdaNpcDialogSubsystem.h"
#include "Text/AsdaLang.h"
#include "Text/AsdaLangRows.h"
#include "AsdaNetSubsystem.h"
#include "GameFramework/PlayerController.h"
#include "Dom/JsonObject.h"
#include "Serialization/JsonReader.h"
#include "Serialization/JsonSerializer.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "UnrealClient.h"   // FScreenshotRequest — кадр журнала раздела P
#include "Blueprint/UserWidget.h"
#include "Components/TextBlock.h"
#include "Components/ScrollBox.h"
#include "Components/Widget.h"
#include "Engine/DataTable.h"
#include "Engine/Engine.h"
#include "Engine/GameInstance.h"
#include "Engine/World.h"
#include "HAL/IConsoleManager.h"
#include "Internationalization/Regex.h"
#include "TimerManager.h"

DEFINE_LOG_CATEGORY_STATIC(LogAsdaQuestFollowTest, Log, All);

// Доступ самотеста к закрытому вычислителю «!» над NPC (`CollectDeliverableForNsr`) — друг подсистемы меток.
struct FAsdaQuestFollowSelfTestAccess
{
	static int32 Deliverable(const UAsdaQuestMarkerSubsystem* QM, const FString& Nsr) { return QM ? QM->CollectDeliverableForNsr(Nsr) : -1; }
};

namespace AsdaQuestFollowTest
{
	static const TCHAR* kCmd   = TEXT("asda.SelfTest.QuestFollow");    // имя команды
	static const TCHAR* kKey   = TEXT("asda.Quest.FollowNoCoordTarget"); // ключ исправления строк без координат
	static const TCHAR* kQuest = TEXT("QUEST_WIN");                      // журнал заданий 700

	// Случай: ячейка журнала, задание, шаг, кнопка маршрута (0…3), ожидаемая карта и клетка, допуск (клеток), подпись.
	struct FCase { int32 Cell, Quest, Stage, Line, Map, X, Y, Tol; const TCHAR* Tag; };

	// Оракул — серверные данные dev (31.10.2026): спавн `<карта>.svr`, NPC `NpcInfo/*.npc`; карта 3 — Alpen (своя карта прогона).
	static const FCase kCases[] = {
		{ 0,   2,  0, 0,  3, 115, 128, 0, TEXT("A контроль: «Злобный зонтик», Alpen (строка гида без правки, своя карта)") },
		{ 1, 464,  0, 0, 14, 222, 136, 0, TEXT("B «Латунный рыцарь Виглаф» → его спавн StagnantDesert (было (240,390) — место Огненного владыки)") },
		{ 1, 464,  0, 1, 14, 209, 394, 0, TEXT("B «Огненный владыка» → его спавн (было (236,142) — место Виглафа)") },
		{ 1, 464,  0, 2, 14, 374, 244, 0, TEXT("B «Гильгамеш» — строка без правки") },
		{ 2, 328,  0, 0,  2, 116, 267, 0, TEXT("C «Вор Элик» → спавн Conquester (было (70,282), 48 клеток)") },
		{ 3, 20039, 0, 2, 14, 374, 251, 0, TEXT("C «Гильгамеш» доски → спавн (было (321,261), 54 клетки)") },
		{ 4,  52,  0, 0,  1, 150, 283, 0, TEXT("D «Древние руины» NPC 83 (ID тёзки 82 исправлен, точка прежняя)") },
		{ 5, 215,  0, 0,  8, 120, 211, 0, TEXT("E моб без координат: зона спавна дворца королевы (одна зона)") },
		{ 6, 217,  0, 0,  8, 393, 406, 0, TEXT("E моб без координат: самая населённая зона (4 точки) на чужой карте") },
		{ 7, 225,  0, 0,  9,  71, 476, 2, TEXT("F NPC 157 без координат → место NPC в замке шахмат") },
		// ★ 08.11.2026 (пачка 15, баг 4 «Golden Piggy Bank» = задание 206 «Золотая копилка», эпизод 2207): в QDT оригинала у
		//   стадии 0 нет строк гида — в журнале был только текст. Строки добавлены ведомостью `Tools/data/quest_guide_fixes.json`
		//   06.11 (цель — мобы, за которых засчитывает сервер, точка — их зона .svr). Индексы kCases[7]/[9] выше не сдвигаются.
		{ 10, 206, 0, 0, 0, 208, 101, 0, TEXT("P задание 206 «Золотая копилка», строка 1 → зона мобов Silaris (ведомость гида 06.11)") },
		{ 10, 206, 0, 1, 1, 262, 336, 0, TEXT("P задание 206 «Золотая копилка», строка 2 → зона мобов Rein (ведомость гида 06.11)") },
	};
	// Раздел H: задание 48 (сдача Турану, NPC 103: сервер Rein (296,254); строка цели из ведомости гида 06.11.2026) на шаге 0 → 5052 шаг 1 = выполнено;
	//   задание 394 на шаге 1 (выполнено; гид шага называет NPC 181, а ячейку сдачи на стадии 1 держит NPC 180: сервер
	//   QuestNpcTable/ConditionTable ключ 899 — задание 394, стадия 1; спавн 180 — Alpen (85,439), гид — (85,436)).
	static const FCase kDone[] = {
		{ 8,  48, 0, 0, 1, 296, 254, 0, TEXT("H задание 48 → «выполнено» пакетом 5052: NPC сдачи Туран, серверный спавн Rein") },
		{ 9, 394, 1, 0, 3,  85, 439, 0, TEXT("H задание 394 выполнено: сдачу примет NPC 180 (гид называл 181), серверный спавн") },
	};
	// Раздел S: (задание, стадия) → NPC, чья ячейка «ПРИНИМАЕТ» открыта на этой стадии. Оракул — сервер dev 31.10:
	//   QuestNpcTable.Bin (ячейки сдачи) + ConditionTable.Bin (задание +57, стадия +65 записи ключа); −1 — никто.
	struct FStep { int32 Quest, Stage, Npc; };
	static const FStep kSteps[] = {
		{56,1,-1}, {57,1,-1}, {103,0,-1}, {104,0,-1}, {121,1,-1}, {215,0,-1}, {221,0,-1}, {223,0,-1}, {248,0,162},
		{263,0,84}, {263,1,-1}, {310,1,-1}, {215,1,151}, {215,2,100}, {226,0,157}, {226,1,-1}, {394,1,180},
	};
	static constexpr int32 kCells = 11;   // занятых ячеек журнала прогона (10 — задание 206, раздел P)

	// Контекст прогона (живёт между таймерами).
	struct FCtx
	{
		int32 Ok = 0, Fail = 0;           // пройдено / провалено
		TWeakObjectPtr<UWorld> W;         // мир прогона
		bool bQuit = false;               // выйти после итога
		bool bHaveStart = false;          // исходная позиция персонажа снята
		uint32 CapGen = 0;                // поколение перехвата отправок (раздел T: 5041)
		struct FSent { int32 Op = 0; TArray<uint8> Body; };
		TArray<FSent> Sent;               // перехваченные отправки
		int32 Mark = 0;                   // число отправок на начало шага
		FVector2D StartGame = FVector2D::ZeroVector;   // исходная позиция (игровые координаты) — вернуть в финале

		void Expect(bool b, const FString& What)
		{
			if (b) { ++Ok;   UE_LOG(LogAsdaQuestFollowTest, Log,   TEXT("СЛЕДОВАТЬ-ТЕСТ ok   %s"), *What); }
			else   { ++Fail; UE_LOG(LogAsdaQuestFollowTest, Error, TEXT("СЛЕДОВАТЬ-ТЕСТ FAIL %s"), *What); }
		}
		UGameInstance* GI() const { return W.IsValid() ? W->GetGameInstance() : nullptr; }
		template <class T> T* Sub() const { UGameInstance* G = GI(); return G ? G->GetSubsystem<T>() : nullptr; }
	};
	using FCtxRef = TSharedRef<FCtx>;

	// Шаг через Sec секунд от вызова.
	using AsdaSelfTestCtx::At;   // ★ R5-c (12.11.2026): общая точка (`Test/AsdaSelfTestCtx.h`) — копия была дословной.
	// Центр живой геометрии узла (false — не отрисован / скрыт / нулевой размер).
	using AsdaSelfTestInput::Center;   // ★ R5-c (12.11.2026): общая точка (`Test/AsdaSelfTestInput.h`) — копия отличалась только тем, что `Mn`/`Mx` объявлялись без начального значения (на результат не влияет).
	// Узел окна журнала по имени.
	static UWidget* Node(const FCtxRef& C, const TCHAR* Name)
	{
		UAsdaWindowSubsystem* WM = C->Sub<UAsdaWindowSubsystem>();
		UUserWidget* Wd = WM ? WM->FindWindow(FName(kQuest)) : nullptr;
		return Wd ? AsdaUi::FindNodeByName(Wd, Name, nullptr) : nullptr;
	}
	// Настоящий щелчок по узлу журнала.
	static void Click(const FCtxRef& C, const FString& Name, const FString& Tag)
	{
		FVector2D P = FVector2D::ZeroVector;
		const bool bGeo = Center(Node(C, *Name), P);
		C->Expect(bGeo, FString::Printf(TEXT("%s: узел %s журнала на экране"), *Tag, *Name));
		if (bGeo) { C->Sub<UAsdaWindowSubsystem>()->SimulateClickAt(P, /*HoldFrames*/ 2); }
	}
	// Код эпизода задания (+0 ячейки журнала: по нему журнал находит книгу); без строки эпизода — 2001 (слот занят).
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
	// Журнал прогона штатным приёмом состояния (раскладка 5047: 12 × 44 б + карта завершения). bEmpty — пустой журнал.
	static void ApplyJournal(const FCtxRef& C, bool bEmpty)
	{
		TArray<uint8> B; B.SetNumZeroed(688);
		if (!bEmpty)
		{
			TSet<int32> Done;
			TArray<FCase> All(kCases, UE_ARRAY_COUNT(kCases));
			All.Append(kDone, UE_ARRAY_COUNT(kDone));
			for (const FCase& K : All)
			{
				if (Done.Contains(K.Cell)) { continue; }
				Done.Add(K.Cell);
				uint8* R = B.GetData() + 44 * K.Cell;
				AsdaBytes::WrI32(R + 0, EpisodeIdOf(K.Quest));   // +0 эпизод
				R[4] = 4;                                         // +4 тип записи = ячейка журнала квестов
				AsdaBytes::WrI16(R + 5, K.Cell);                  // +5 номер ячейки сервера
				AsdaBytes::WrI16(R + 8, K.Quest);                 // +8 номер задания
				AsdaBytes::WrI16(R + 10, K.Stage);                // +10 стадия
			}
		}
		if (UAsdaQuestMarkerSubsystem* QM = C->Sub<UAsdaQuestMarkerSubsystem>()) { QM->ApplyQuestStateBlob(B); }
	}
	// Куда повела кнопка: своя карта — флажок цели мини-карты, чужая — заявка автопути. false — никуда.
	static bool Destination(const FCtxRef& C, int32& OutMap, FIntPoint& OutTile)
	{
		UAsdaMinimapSubsystem* MM = C->Sub<UAsdaMinimapSubsystem>();
		UAsdaLoadManagerSubsystem* LM = C->Sub<UAsdaLoadManagerSubsystem>();
		if (!MM) { return false; }
		if (MM->IsTravelRequested())
		{
			// «Межкартовый автопуть: цель карта=%d клетка=(%d,%d); …» — строка диагностики заявки (`DescribeMapTravel`).
			const FRegexPattern Pat(TEXT("карта=(-?\\d+) клетка=\\((-?\\d+),(-?\\d+)\\)"));
			FRegexMatcher M(Pat, MM->DescribeMapTravel());
			if (!M.FindNext()) { return false; }
			OutMap = FCString::Atoi(*M.GetCaptureGroup(1));
			OutTile = FIntPoint(FCString::Atoi(*M.GetCaptureGroup(2)), FCString::Atoi(*M.GetCaptureGroup(3)));
			return true;
		}
		if (MM->IsDestinationActive())
		{
			OutMap = LM ? LM->CurrentMapId : -1;
			OutTile = MM->GetDestinationTile();
			return true;
		}
		return false;
	}
	// Сбросить следы прошлого щелчка: заявку автопути и флажок цели.
	static void ResetDest(const FCtxRef& C)
	{
		if (UAsdaMinimapSubsystem* MM = C->Sub<UAsdaMinimapSubsystem>())
		{
			if (MM->IsTravelRequested()) { MM->CancelTravelRequest(); }
			MM->SetDestinationTile(0, 0, false);
		}
	}
	// Подпись кнопки маршрута Line выбранной ячейки (как её видит игрок).
	static FString RouteText(const FCtxRef& C, int32 Line)
	{
		const UTextBlock* T = Cast<UTextBlock>(Node(C, *FString::Printf(TEXT("QUEST_btn_b%d_Text"), Line + 1)));
		return T ? T->GetText().ToString() : FString();
	}

	// Один случай: щелчок по ячейке → щелчок по кнопке → проверка места. Next — продолжение цепочки.
	static bool RouteShown(const FCtxRef& C, int32 Line);   // ниже: видна ли кнопка маршрута Line (раздел P)

	static void RunCase(const FCtxRef& C, const FCase& K, bool bExpectNone, TFunction<void()> Next)
	{
		const FString Tag = FString(K.Tag) + (bExpectNone ? TEXT(" [ключ 0]") : TEXT(""));
		ResetDest(C);
		Click(C, FString::Printf(TEXT("QUEST_INVEN%d_HIT"), K.Cell), Tag + TEXT(" ячейка"));
		At(C, 0.35f, [C, K, Tag]()
		{
			UAsdaQuestLogSubsystem* QL = C->Sub<UAsdaQuestLogSubsystem>();
			C->Expect(QL && QL->GetSelectedSlot() == K.Cell, FString::Printf(TEXT("%s: выбрана ячейка %d"), *Tag, K.Cell));
			const FString Txt = RouteText(C, K.Line);
			C->Expect(!Txt.IsEmpty(), FString::Printf(TEXT("%s: кнопка %d — «%s»"), *Tag, 705 + K.Line, *Txt));
			// Раздел P: кадр журнала с выбранной ячейкой (кнопки маршрута и указатель «Click» видны игроку).
			if (K.Tag[0] == TEXT('P') && K.Line == 0)
			{
				const FString Dir = FPaths::ConvertRelativePathToFull(FPaths::ProjectDir() / TEXT("../Reports/batch15/piggy_frames"));
				FScreenshotRequest::RequestScreenshot(Dir / FString::Printf(TEXT("journal_206_%s.png"), FAsdaLang::IsRu() ? TEXT("ru") : TEXT("en")),
					/*bShowUI*/ true, /*bAddFilenameSuffix*/ false);
				C->Expect(RouteShown(C, 0) && RouteShown(C, 1) && !RouteShown(C, 2),
					FString::Printf(TEXT("%s: в журнале видны ровно 2 кнопки маршрута (строки гида стадии 0)"), *Tag));
			}
			Click(C, FString::Printf(TEXT("QUEST_btn_b%d"), K.Line + 1), Tag + TEXT(" кнопка маршрута"));
		});
		At(C, 0.8f, [C, K, Tag, bExpectNone, Next]()
		{
			int32 Map = -1; FIntPoint T(-1, -1);
			const bool bHas = Destination(C, Map, T);
			if (bExpectNone)
			{
				C->Expect(!bHas, FString::Printf(TEXT("%s: кнопка не ведёт никуда (прежнее поведение), заявка %s карта %d (%d,%d)"),
					*Tag, bHas ? TEXT("ЕСТЬ") : TEXT("нет"), Map, T.X, T.Y));
			}
			else
			{
				const bool bOk = bHas && Map == K.Map && FMath::Abs(T.X - K.X) <= K.Tol && FMath::Abs(T.Y - K.Y) <= K.Tol;
				C->Expect(bOk, FString::Printf(TEXT("%s: ведёт на карту %d клетку (%d,%d) — ждали карту %d (%d,%d)±%d"),
					*Tag, Map, T.X, T.Y, K.Map, K.X, K.Y, K.Tol));
			}
			ResetDest(C);
			At(C, 0.2f, Next);
		});
	}

	// Пакет прогресса 5052 штатным приёмом: запись ячейки (раскладка 5047) с новой стадией — ветка ОБНОВЛЕНИЯ слота.
	static void Progress5052(const FCtxRef& C, const FCase& K, int32 NewStage, const TArray<int32>& ObjIds = {}, const TArray<int32>& ObjCounts = {})
	{
		TArray<uint8> B; B.SetNumZeroed(508);
		const int32 Ep = EpisodeIdOf(K.Quest);
		AsdaBytes::WrI16(B.GetData() + 0, Ep);        // +0  эпизод
		B[4] = 2;                                      // +4  вид != 1 ⇒ обновление слота
		AsdaBytes::WrI32(B.GetData() + 13, 0);         // +13 <= 0 ⇒ не завершение
		uint8* R = B.GetData() + 22;                   // +22 — запись ячейки
		AsdaBytes::WrI32(R + 0, Ep);
		R[4] = 4;
		AsdaBytes::WrI16(R + 5, K.Cell);
		R[7] = 2;
		AsdaBytes::WrI16(R + 8, K.Quest);
		AsdaBytes::WrI16(R + 10, NewStage);
		for (int32 j = 0; j < 5; ++j)                  // +14+6j — цели слота (i32 код, i16 счётчик)
		{
			AsdaBytes::WrI32(R + 14 + 6 * j, ObjIds.IsValidIndex(j) ? ObjIds[j] : 0);
			AsdaBytes::WrI16(R + 18 + 6 * j, ObjCounts.IsValidIndex(j) ? ObjCounts[j] : 0);
		}
		if (UAsdaQuestMarkerSubsystem* QM = C->Sub<UAsdaQuestMarkerSubsystem>()) { QM->ApplyQuestProgressBlob(B); }
	}
	// Видна ли кнопка маршрута Line выбранной ячейки.
	static bool RouteShown(const FCtxRef& C, int32 Line)
	{
		const UWidget* Wd = Node(C, *FString::Printf(TEXT("QUEST_btn_b%d"), Line + 1));
		return Wd && AsdaUi::IsShown(Wd);
	}

	static void Finish(const FCtxRef& C);
	static void RunSteps(const FCtxRef& C);
	static void RunKeys(const FCtxRef& C);
	static void RunAll(const FCtxRef& C);
	static void RunTurnIn(const FCtxRef& C);

	// Значки целей на карте мира для карты Map (живые квадраты пула большой карты).
	static int32 DrawnTargets(const FCtxRef& C, int32 Map)
	{
		UAsdaMinimapSubsystem* MM = C->Sub<UAsdaMinimapSubsystem>();
		if (!MM) { return -1; }
		if (!MM->IsBigMapOpen()) { MM->ToggleBigMap(); }
		const TArray<FAsdaWorldMapArea>& Areas = MM->GetWorldMapAreas();
		for (int32 i = 0; i < Areas.Num(); ++i)
		{
			if (Areas[i].MapId != Map || Areas[i].bTown) { continue; }
			int32 InCont = 0;
			for (int32 k = 0; k < i; ++k) { if (Areas[k].Continent == Areas[i].Continent) { ++InCont; } }
			MM->SelectContinent(Areas[i].Continent);
			MM->SelectArea(InCont);
			break;
		}
		return MM->CountDrawnMarks(EAsdaMapMark::QuestTarget, true);
	}
	// Число видимых кнопок маршрута выбранной ячейки.
	static int32 RoutesShown(const FCtxRef& C)
	{
		int32 N = 0;
		for (int32 i = 0; i < 4; ++i) { if (RouteShown(C, i)) { ++N; } }
		return N;
	}

	// ═══ H. Цель шага выполнена → NPC сдачи ═══
	static void RunDone(const FCtxRef& C)
	{
		const FCase& K48 = kDone[0];
		AsdaCVarSave::SetForTest(TEXT("asda.Quest.FollowDoneToNpc"), 1);
		ResetDest(C);
		Click(C, FString::Printf(TEXT("QUEST_INVEN%d_HIT"), K48.Cell), TEXT("H задание 48 ячейка"));
		At(C, 0.4f, [C, K48]()
		{
			UAsdaQuestMarkerSubsystem* QM = C->Sub<UAsdaQuestMarkerSubsystem>();
			C->Expect(QM && !QM->IsQuestObjectiveDone(K48.Quest), TEXT("H задание 48 шаг 0: цель НЕ выполнена"));
			C->Expect(RouteShown(C, 0), TEXT("H задание 48 шаг 0: строка цели есть (★ 06.11.2026 ведомость гида: моб и карта; в оригинале строки не было)"));
			Progress5052(C, K48, 1);   // сервер: предметы собраны → шаг 1
		});
		At(C, 1.0f, [C, K48]()
		{
			UAsdaQuestMarkerSubsystem* QM = C->Sub<UAsdaQuestMarkerSubsystem>();
			C->Expect(QM && QM->IsQuestObjectiveDone(K48.Quest), TEXT("H 5052 шаг 1: цель выполнена"));
			const FString Txt = RouteText(C, 0);
			const FString Want = FString::Printf(TEXT(", x:%d, y: %d"), 296, 254);   // серверный спавн Турана (Rein.npc)
			C->Expect(RouteShown(C, 0) && Txt.StartsWith(TEXT("=> ")) && Txt.Contains(TEXT(", x:")),
				FString::Printf(TEXT("H без переоткрытия окна: кнопка 705 — «%s» (NPC сдачи, карта, клетка)"), *Txt));
			C->Expect(Txt.EndsWith(Want), FString::Printf(TEXT("H подпись кончается «%s»"), *Want));
			Click(C, TEXT("QUEST_btn_b1"), TEXT("H задание 48 кнопка маршрута"));
		});
		At(C, 1.5f, [C, K48]()
		{
			int32 Map = -1; FIntPoint T(-1, -1);
			const bool bHas = Destination(C, Map, T);
			C->Expect(bHas && Map == K48.Map && FMath::Abs(T.X - K48.X) <= K48.Tol && FMath::Abs(T.Y - K48.Y) <= K48.Tol,
				FString::Printf(TEXT("%s: ведёт на карту %d (%d,%d) — ждали %d (%d,%d)±%d"), K48.Tag, Map, T.X, T.Y, K48.Map, K48.X, K48.Y, K48.Tol));
			ResetDest(C);
			// Ключ 0: как в оригинале — у выполненного 48 строк маршрута нет (гида нет).
			AsdaCVarSave::SetForTest(TEXT("asda.Quest.FollowDoneToNpc"), 0);
			Click(C, FString::Printf(TEXT("QUEST_INVEN%d_HIT"), kDone[1].Cell), TEXT("H ключ 0 смена ячейки"));
		});
		At(C, 1.9f, [C, K48]() { Click(C, FString::Printf(TEXT("QUEST_INVEN%d_HIT"), K48.Cell), TEXT("H ключ 0 ячейка 48")); });
		At(C, 2.3f, [C]()
		{
			C->Expect(!RouteShown(C, 0), TEXT("H ключ 0: у выполненного задания 48 строк маршрута нет (оригинал)"));
			// Ключ 0, задание 394: строка гида шага — NPC 181 (оригинал).
			Click(C, FString::Printf(TEXT("QUEST_INVEN%d_HIT"), kDone[1].Cell), TEXT("H ключ 0 ячейка 394"));
		});
		At(C, 2.7f, [C]()
		{
			const FString Txt = RouteText(C, 0);
			C->Expect(RouteShown(C, 0) && Txt.Contains(TEXT(", x:118")), FString::Printf(TEXT("H ключ 0: задание 394 — строка гида NPC 181 «%s» (оригинал)"), *Txt));
			AsdaCVarSave::SetForTest(TEXT("asda.Quest.FollowDoneToNpc"), 1);
			Click(C, FString::Printf(TEXT("QUEST_INVEN%d_HIT"), kDone[0].Cell), TEXT("H ключ 1 смена ячейки"));
		});
		At(C, 3.1f, [C]()
		{
			RunCase(C, kDone[1], false, [C]() { RunKeys(C); });
		});
	}

	// ═══ S. Отбор NPC шага: задание ставится в ячейку 7 штатным приёмом прогресса, NPC — общей точкой кнопок и карты мира ═══
	static void RunSteps(const FCtxRef& C)
	{
		UAsdaQuestMarkerSubsystem* QM = C->Sub<UAsdaQuestMarkerSubsystem>();
		for (const FStep& St : kSteps)
		{
			FCase K = kCases[9]; K.Quest = St.Quest; K.Stage = St.Stage;
			Progress5052(C, K, St.Stage);
			TArray<AsdaQuestGuideTarget::FTurnIn> Npcs;
			AsdaQuestGuideTarget::TurnInNpcs(QM, St.Quest, St.Stage, /*bByStep*/ true, Npcs);
			FString Got;
			for (const AsdaQuestGuideTarget::FTurnIn& T : Npcs) { Got += FString::Printf(TEXT("%s%d"), Got.IsEmpty() ? TEXT("") : TEXT(","), T.NpcId); }
			const bool bOk = (St.Npc < 0) ? Npcs.Num() == 0 : (Npcs.Num() == 1 && Npcs[0].NpcId == St.Npc);
			C->Expect(bOk, FString::Printf(TEXT("S задание %d стадия %d: принимает [%s] — сервер: %s"), St.Quest, St.Stage,
				*Got, St.Npc < 0 ? TEXT("никто") : *FString::FromInt(St.Npc)));
		}
		At(C, 0.2f, [C]() { RunAll(C); });
	}

	// ═══ K. Ключи независимы: все 4 сочетания (FollowNoCoordTarget, FollowDoneToNpc). На каждом — (1) моб без координат 215
	//   (кнопка ведёт к зоне ⇔ первый ключ), (2) выполненное задание 15 «Материалы для ремонта» в ячейке 7: одна кнопка —
	//   NPC сдачи 43 с серверной клеткой (163,458) ⇔ второй ключ, иначе 4 строки гида оригинала; (3) значки карты мира (Alpen, 3)
	//   у отслеживаемого выполненного 15: 1 — NPC 43 ⇔ второй ключ, иначе 4 — все NPC с ячейкой SNQ (1, 40, 43, 48). ═══
	static void RunKeys(const FCtxRef& C)
	{
		static const int32 kCell15 = 7;
		FCase K15 = kCases[9]; K15.Cell = kCell15; K15.Quest = 15; K15.Stage = 0;
		AsdaCVarSave::SetForTest(kKey, 1);
		AsdaCVarSave::SetForTest(TEXT("asda.Quest.FollowDoneToNpc"), 1);
		Progress5052(C, K15, 0);   // не выполнено — его можно поставить в трекер
		UAsdaQuestLogSubsystem* QL = C->Sub<UAsdaQuestLogSubsystem>();
		const bool bTracked = QL->SetSlotTracked(kCell15, true);
		C->Expect(bTracked, TEXT("K задание 15 поставлено в трекер (флажок журнала)"));
		Progress5052(C, K15, 0, { 21205, 21206, 21207 }, { 1, 1, 1 });   // предметы собраны — цель выполнена
		UAsdaQuestMarkerSubsystem* QM = C->Sub<UAsdaQuestMarkerSubsystem>();
		C->Expect(QM && QM->IsQuestObjectiveDone(15), TEXT("K задание 15: цель выполнена (QCT)"));
		TSharedRef<int32> Combo = MakeShared<int32>(0);
		TSharedRef<TFunction<void()>> Next = MakeShared<TFunction<void()>>();
		*Next = [C, Combo, Next]()
		{
			if (*Combo >= 4)
			{
				AsdaCVarSave::SetForTest(kKey, 1);
				AsdaCVarSave::SetForTest(TEXT("asda.Quest.FollowDoneToNpc"), 1);
				C->Sub<UAsdaQuestLogSubsystem>()->SetSlotTracked(kCell15, false);
				UAsdaMinimapSubsystem* MM = C->Sub<UAsdaMinimapSubsystem>();
				if (MM && MM->IsBigMapOpen()) { MM->ToggleBigMap(); }
				At(C, 0.3f, [C]() { RunSteps(C); });
				return;
			}
			const int32 NoCoord = (*Combo) & 1, Done = ((*Combo) >> 1) & 1;
			++(*Combo);
			const FString Tag = FString::Printf(TEXT("K ключи NoCoord=%d DoneToNpc=%d"), NoCoord, Done);
			AsdaCVarSave::SetForTest(kKey, NoCoord);
			AsdaCVarSave::SetForTest(TEXT("asda.Quest.FollowDoneToNpc"), Done);
			UE_LOG(LogAsdaQuestFollowTest, Log, TEXT("СЛЕДОВАТЬ-ТЕСТ раздел %s"), *Tag);
			// (1) моб без координат — тем же путём игрока, что раздел E (при первом ключе 0 — «не ведёт никуда»).
			RunCase(C, kCases[7], NoCoord == 0, [C, Done, Tag, Next]()
			{
				// (2) кнопки журнала у выполненного задания 15.
				Click(C, FString::Printf(TEXT("QUEST_INVEN%d_HIT"), kCell15), Tag + TEXT(" ячейка 15"));
				At(C, 0.4f, [C, Done, Tag, Next]()
				{
					const FString Txt = RouteText(C, 0);
					const int32 N = RoutesShown(C);
					if (Done)
					{
						C->Expect(N == 1 && Txt.EndsWith(TEXT(", x:163, y: 458")),
							FString::Printf(TEXT("%s: одна кнопка — NPC сдачи 43, серверная клетка (163,458): %d кнопок, «%s»"), *Tag, N, *Txt));
					}
					else
					{
						C->Expect(N == 4, FString::Printf(TEXT("%s: 4 строки гида оригинала — %d кнопок, первая «%s»"), *Tag, N, *Txt));
					}
					// (3) значки карты мира: открыть, показать чужую область и вернуть Alpen (пересчёт значков), счёт через 0.6 с.
					DrawnTargets(C, 1);
					At(C, 0.4f, [C, Done, Tag, Next]()
					{
						DrawnTargets(C, 3);
						At(C, 0.6f, [C, Done, Tag, Next]()
						{
							UAsdaMinimapSubsystem* MM = C->Sub<UAsdaMinimapSubsystem>();
							const int32 M = MM ? MM->CountDrawnMarks(EAsdaMapMark::QuestTarget, true) : -1;
							const int32 Want = Done ? 1 : 4;
							C->Expect(M == Want, FString::Printf(TEXT("%s: значков целей на карте Alpen %d (ждали %d)"), *Tag, M, Want));
							if (MM && MM->IsBigMapOpen()) { MM->ToggleBigMap(); }
							At(C, 0.3f, [Next]() { (*Next)(); });
						});
					});
				});
			});
		};
		At(C, 0.3f, [Next]() { (*Next)(); });
	}

	// ═══ V. ПОЛНЫЙ СПИСОК: все 491 состояние «цель выполнена» (QCT) против оракула `Tools/data/quest_turnin_expected.json`
	//   (его строит `Tools/asda_quest_turnin_table.py` из серверных QuestNpcTable/ConditionTable/NpcInfo своей моделью условий —
	//   независимо от клиентского вычислителя). Состояние ставится штатным приёмом 5052 (стадия + цели слота), класс персонажа —
	//   тот, что требует условие ячейки; живой выбор `TurnInNpcs` обязан дать ровно тех же NPC и их серверные клетки. ═══
	static void RunAll(const FCtxRef& C)
	{
		FString Text;
		const FString Path = FPaths::ConvertRelativePathToFull(AsdaSelfTestTree::ProjectDir() / TEXT("Tools/data/quest_turnin_expected.json"));
		TArray<TSharedPtr<FJsonValue>> Arr;
		if (!FFileHelper::LoadFileToString(Text, *Path) || !FJsonSerializer::Deserialize(TJsonReaderFactory<>::Create(Text), Arr) || Arr.Num() == 0)
		{
			C->Expect(false, FString::Printf(TEXT("V оракул %s прочитан и разобран"), *Path));
			Finish(C);
			return;
		}
		UAsdaQuestMarkerSubsystem* QM = C->Sub<UAsdaQuestMarkerSubsystem>();
		int32 Ok = 0, Bad = 0, Multi = 0, None = 0;
		for (const TSharedPtr<FJsonValue>& V : Arr)
		{
			const TSharedPtr<FJsonObject> O = V.IsValid() ? V->AsObject() : nullptr;
			if (!O) { continue; }
			FCase K = kCases[9];
			K.Quest = (int32)O->GetNumberField(TEXT("QuestNo"));
			K.Stage = (int32)O->GetNumberField(TEXT("Stage"));
			TArray<int32> Ids, Cnts, Want;
			for (const TSharedPtr<FJsonValue>& X : O->GetArrayField(TEXT("ObjIds"))) { Ids.Add((int32)X->AsNumber()); }
			for (const TSharedPtr<FJsonValue>& X : O->GetArrayField(TEXT("ObjCounts"))) { Cnts.Add((int32)X->AsNumber()); }
			for (const TSharedPtr<FJsonValue>& X : O->GetArrayField(TEXT("Npcs"))) { Want.Add((int32)X->AsNumber()); }
			const int32 Cls = (int32)O->GetNumberField(TEXT("ClassId"));
			GEngine->Exec(C->W.Get(), *FString::Printf(TEXT("asda.Quest.FakeSelf 150 %d 0"), Cls >= 0 ? Cls : 1));
			Progress5052(C, K, K.Stage, Ids, Cnts);
			TArray<AsdaQuestGuideTarget::FTurnIn> Npcs;
			AsdaQuestGuideTarget::TurnInNpcs(QM, K.Quest, K.Stage, /*bByStep*/ true, Npcs);
			TArray<int32> Got;
			bool bPlaces = true;
			for (const AsdaQuestGuideTarget::FTurnIn& T : Npcs)
			{
				Got.AddUnique(T.NpcId);
				for (const TSharedPtr<FJsonValue>& P : O->GetArrayField(TEXT("Places")))   // клетка — серверный спавн из оракула
				{
					const TArray<TSharedPtr<FJsonValue>>& A = P->AsArray();
					if (A.Num() == 4 && (int32)A[0]->AsNumber() == T.NpcId
						&& ((int32)A[1]->AsNumber() != T.Place.MapId || (int32)A[2]->AsNumber() != T.Place.TileX || (int32)A[3]->AsNumber() != T.Place.TileY))
					{
						bPlaces = false;
					}
				}
			}
			Got.Sort(); Want.Sort();
			if (Got == Want && bPlaces) { ++Ok; if (Want.Num() > 1) { ++Multi; } if (Want.Num() == 0) { ++None; } continue; }
			++Bad;
			FString G, W;
			for (int32 X : Got) { G += FString::Printf(TEXT(" %d"), X); }
			for (int32 X : Want) { W += FString::Printf(TEXT(" %d"), X); }
			C->Expect(false, FString::Printf(TEXT("V задание %d шаг %d: выбрано [%s ] — сервер [%s ]%s"), K.Quest, K.Stage, *G, *W,
				bPlaces ? TEXT("") : TEXT(", клетка не серверная")));
		}
		GEngine->Exec(C->W.Get(), TEXT("asda.Quest.FakeSelf 150 1 0"));
		C->Expect(Bad == 0 && Ok == Arr.Num(), FString::Printf(TEXT("V все состояния «цель выполнена»: %d, совпало %d, расхождений %d; из них законных пар (ближайший) %d, «сервер не примет ни у кого» %d"),
			Arr.Num(), Ok, Bad, Multi, None));
		RunTurnIn(C);
	}



	// ═══ T. СДАЧА ВОССТАНОВЛЕННЫХ ЗАДАНИЙ ПУТЁМ ИГРОКА ([ИСПРАВЛЕНИЕ] 31.10.2026: у 15 заданий в данных оригинала не было ячейки
	//   сдачи и страницы диалога). Оракул — `AstoryServer/Patches/AsdaPatch/tools/data_quest_turnin/spec.json`, построенный из
	//   СЕРВЕРНЫХ таблиц (NPC, слот ячейки, ключ, страница, реплики). На каждом: задание в журнале на нужной стадии
	//   (`asda.Quest.FakeState`, как ответ 5047) → разговор с NPC → облачко MULTYQUEST (`ShowMultiQuest`, кнопка «Задание» корня)
	//   → НАСТОЯЩИЙ клик по строке «принимает» → страница сдачи (реплика RU/EN — утверждённая) → клик «Получение награды» →
	//   попап 540 («Готово», у наград на выбор — первая строка) → ровно одна 5041 = [npc u16][слот ячейки u16][индекс награды u8].
	//   ★ Задание 96 — ячейка Эллосы со слотом 10 (таблица расширения `QuestNpcTableExt.Bin`, патч NpcServer N22): 5041 уходит
	//   со слотом 10. Плюс совместный прогон Эллосы: в журнале 125, 126 и 96 СРАЗУ — три строки «принимает» в порядке слотов
	//   (1, 9, 10), каждая сдаётся своей 5041; «10 + 1» — все задания ячеек Эллосы в журнале, строка 96 последней (прокрутка);
	//   «!» над Эллосой при одном 96; NPC сдачи 96 («Следовать», карта мира) — Эллоса. ═══
	struct FTurnCase
	{
		int32 Quest = 0, Stage = 0, Npc = 0, Talk = 0, Slot = 0, Level = 1, NumRewards = 1;
		int32 Key = 0;                 // ключ условия ячейки (кнопка шага страницы, State == ключ)
		bool bRegular = false;         // обычная (оригинальная) сдача: реплика не сверяется, выбор награды — по факту попапа
		int32 Row = 0;                 // строка облачка «принимает» (−1 — последняя)
		int32 Rows = 1;                // строк «принимает» в облачке (−1 — сколько пропустят условия, не меньше 2)
		TArray<FIntPoint> Journal;     // журнал (задание, стадия); пусто — одно это задание (`asda.Quest.FakeState`)
		FString Ru, En;
	};

	static TArray<FTurnCase> LoadTurnCases()
	{
		TArray<FTurnCase> Out;
		FString Text;
		const FString Path = FPaths::ConvertRelativePathToFull(AsdaSelfTestTree::ProjectDir() / TEXT("../AstoryServer/Patches/AsdaPatch/tools/data_quest_turnin/spec.json"));
		TSharedPtr<FJsonObject> Root;
		if (!FFileHelper::LoadFileToString(Text, *Path) || !FJsonSerializer::Deserialize(TJsonReaderFactory<>::Create(Text), Root) || !Root) { return Out; }
		for (const TSharedPtr<FJsonValue>& V : Root->GetArrayField(TEXT("cells")))
		{
			const TSharedPtr<FJsonObject> O = V->AsObject();
			FTurnCase K;
			K.Quest = (int32)O->GetNumberField(TEXT("quest"));  K.Stage = (int32)O->GetNumberField(TEXT("stage"));
			K.Npc = (int32)O->GetNumberField(TEXT("npc"));      K.Talk = (int32)O->GetNumberField(TEXT("talk"));
			K.Slot = (int32)O->GetNumberField(TEXT("slot"));    K.Level = (int32)O->GetNumberField(TEXT("level"));
			K.NumRewards = O->GetArrayField(TEXT("rewards")).Num();
			K.Ru = O->GetStringField(TEXT("ru"));               K.En = O->GetStringField(TEXT("en"));
			K.Key = (int32)O->GetNumberField(TEXT("key"));
			Out.Add(K);
		}
		// Совместный прогон Эллосы: все её восстановленные ячейки в одном журнале. Порядок строк облачка — как у
		//   `BuildQuestLists`: по номеру ячейки (слоты записи 0…9, затем таблица расширения 10+).
		TArray<FTurnCase> Ella;
		for (const FTurnCase& K : Out) { if (K.Npc == 56) { Ella.Add(K); } }
		Ella.Sort([](const FTurnCase& A, const FTurnCase& B) { return A.Slot < B.Slot; });
		TArray<FIntPoint> J;
		int32 Lvl = 1;
		for (const FTurnCase& K : Ella) { J.Add(FIntPoint(K.Quest, K.Stage)); Lvl = FMath::Max(Lvl, K.Level); }
		for (int32 i = 0; i < Ella.Num(); ++i)
		{
			FTurnCase K = Ella[i];
			K.Row = i; K.Rows = Ella.Num(); K.Journal = J; K.Level = Lvl;
			Out.Add(K);
		}
		// «10 + 1»: в журнале ВСЕ задания ячеек Эллосы 0…9 (стадия — из её ключа `DT_SnqIct`) и 96 (слот 10) — строка 96 идёт
		//   последней, до неё облачко прокручивается; сдача — 5041 со слотом 10.
		const FTurnCase* K96 = Ella.FindByPredicate([](const FTurnCase& K) { return K.Slot >= 10; });
		const FAsdaNpcSnqRow* Own = AsdaDt::RowByInt<FAsdaNpcSnqRow>(TEXT("/Game/AsdaAssets/Data/NPC/DT_NpcSnq.DT_NpcSnq"), 56, TEXT("QuestFollowTest"));
		if (K96 && Own)
		{
			FTurnCase K = *K96;
			K.Journal.Reset();
			for (const FAsdaSnqEntry& E : Own->Take)
			{
				if (E.QuestId <= 0 || E.QuestId == K.Quest) { continue; }
				int32 Stage = 0;
				for (int32 Key : E.Keys)
				{
					const FAsdaSnqIctRow* R = Key > 0 ? AsdaDt::RowByInt<FAsdaSnqIctRow>(TEXT("/Game/AsdaAssets/Data/NPC/DT_SnqIct.DT_SnqIct"), Key, TEXT("QuestFollowTest")) : nullptr;
					if (R && R->QuestActive == E.QuestId) { Stage = R->QuestStage; break; }
				}
				K.Journal.Add(FIntPoint(E.QuestId, Stage));
			}
			K.Journal.Add(FIntPoint(K.Quest, K.Stage));
			K.Row = -1; K.Rows = -1; K.Level = 150;
			Out.Add(K);
		}
		// Обычные (оригинальные) сдачи со слотами 0…9 у разных NPC — регресс общего пути (облачко, страница, 5041 [npc][слот]).
		//   Берутся из серверной таблицы сдачи (`DT_QuestTurnIn`): действие 2, условие ключа без целей/предметов/класса/экипировки
		//   (состояние задаётся одним 5047), страница сдачи с кнопкой этого ключа есть; по одной ячейке на NPC, разные слоты.
		{
			TSet<int32> Restored, UsedNpc, UsedSlot;
			for (const FTurnCase& K : Out) { Restored.Add(K.Quest); }
			const UDataTable* Ti = AsdaDt::Cached(TEXT("/Game/AsdaAssets/Data/Quest/DT_QuestTurnIn.DT_QuestTurnIn"));
			const UDataTable* Pg = AsdaDt::Cached(TEXT("/Game/AsdaAssets/Data/NPC/DT_NpcNsrPage.DT_NpcNsrPage"));
			int32 Added = 0;
			for (const TPair<FName, uint8*>& It : Ti ? Ti->GetRowMap() : TMap<FName, uint8*>())
			{
				const FAsdaQuestTurnInRow* T = reinterpret_cast<const FAsdaQuestTurnInRow*>(It.Value);
				if (!T || T->Action != 2 || Restored.Contains(T->QuestNo) || UsedNpc.Contains(T->NpcId) || Added >= 8) { continue; }
				const FAsdaNpcSnqRow* Snq = AsdaDt::RowByInt<FAsdaNpcSnqRow>(TEXT("/Game/AsdaAssets/Data/NPC/DT_NpcSnq.DT_NpcSnq"), T->NpcId, TEXT("QuestFollowTest"));
				for (int32 Key : T->Keys)
				{
					const FAsdaSnqIctRow* R = Key > 0 ? AsdaDt::RowByInt<FAsdaSnqIctRow>(TEXT("/Game/AsdaAssets/Data/NPC/DT_SnqIct.DT_SnqIct"), Key, TEXT("QuestFollowTest")) : nullptr;
					if (!R || R->QuestActive != T->QuestNo || R->QuestStage != T->Stage || R->HasClass || R->HasReqItems || R->HasEquipped
						|| R->HasBagTab || R->ObjFlagNone || R->ObjIds.ContainsByPredicate([](int32 Id) { return Id > 0; })) { continue; }
					int32 Slot = -1;
					for (int32 i = 0; Snq && i < Snq->Take.Num() && i < 10; ++i)
					{
						if (Snq->Take[i].QuestId == T->QuestNo && Snq->Take[i].Keys.Contains(Key)) { Slot = i; }
					}
					bool bPage = false;
					for (const TPair<FName, uint8*>& Pi : Pg ? Pg->GetRowMap() : TMap<FName, uint8*>())
					{
						const FAsdaNsrPageRow* P = reinterpret_cast<const FAsdaNsrPageRow*>(Pi.Value);
						if (!P || P->TalkId != R->TalkId) { continue; }
						for (const FAsdaNsrStep& St : P->Steps) { for (const FAsdaNsrStepSub& Sb : St.Subs) { bPage |= Sb.State == Key; } }
					}
					if (Slot < 0 || UsedSlot.Contains(Slot) || !bPage) { continue; }
					FTurnCase K;
					K.Quest = T->QuestNo; K.Stage = T->Stage; K.Npc = T->NpcId; K.Talk = R->TalkId; K.Slot = Slot; K.Key = Key;
					K.Level = FMath::Clamp(R->HasLevel ? R->LevelMin : 1, 1, 150); K.NumRewards = -1; K.bRegular = true;
					Out.Add(K);
					UsedNpc.Add(T->NpcId); UsedSlot.Add(Slot); ++Added;
					break;
				}
			}
		}
		return Out;
	}

	// Кнопка шага страницы, у которого под-шаг несёт ключ сдачи (`State` == ключ): NPC_QA_BUT0<номер шага>. −1 — нет.
	static int32 StepButtonOf(int32 TalkId, int32 Key)
	{
		const UDataTable* Pg = AsdaDt::Cached(TEXT("/Game/AsdaAssets/Data/NPC/DT_NpcNsrPage.DT_NpcNsrPage"));
		for (const TPair<FName, uint8*>& Pi : Pg ? Pg->GetRowMap() : TMap<FName, uint8*>())
		{
			const FAsdaNsrPageRow* P = reinterpret_cast<const FAsdaNsrPageRow*>(Pi.Value);
			if (!P || P->TalkId != TalkId) { continue; }
			for (int32 i = 0; i < P->Steps.Num(); ++i)
			{
				for (const FAsdaNsrStepSub& Sb : P->Steps[i].Subs) { if (Sb.State == Key) { return i; } }
			}
		}
		return -1;
	}

	// Журнал из нескольких заданий — штатный приём состояния (раскладка 5047), ячейки 0…N−1.
	static void ApplyJournalList(const FCtxRef& C, const TArray<FIntPoint>& J)
	{
		TArray<uint8> B; B.SetNumZeroed(688);
		for (int32 i = 0; i < J.Num() && 44 * (i + 1) <= 688 - 160; ++i)   // 12 ячеек журнала
		{
			uint8* R = B.GetData() + 44 * i;
			AsdaBytes::WrI32(R + 0, EpisodeIdOf(J[i].X));
			R[4] = 4;
			AsdaBytes::WrI16(R + 5, i);
			AsdaBytes::WrI16(R + 8, J[i].X);
			AsdaBytes::WrI16(R + 10, J[i].Y);
		}
		if (UAsdaQuestMarkerSubsystem* QM = C->Sub<UAsdaQuestMarkerSubsystem>()) { QM->ApplyQuestStateBlob(B); }
	}

	// Узел окна по имени (любого окна WM).
	static UWidget* WinNode(const FCtxRef& C, const TCHAR* Win, const FString& Name)
	{
		UAsdaWindowSubsystem* WM = C->Sub<UAsdaWindowSubsystem>();
		UUserWidget* W = WM ? WM->FindWindow(FName(Win)) : nullptr;
		return W ? W->GetWidgetFromName(FName(*Name)) : nullptr;
	}
	static bool ClickNode(const FCtxRef& C, UWidget* Wd, const FString& Tag)
	{
		FVector2D P;
		const bool bGeo = Center(Wd, P);
		C->Expect(bGeo, FString::Printf(TEXT("%s: на экране"), *Tag));
		if (bGeo) { C->Sub<UAsdaWindowSubsystem>()->SimulateClickAt(P, /*HoldFrames*/6); }
		return bGeo;
	}

	static void RunTurnIn(const FCtxRef& C)
	{
		const TArray<FTurnCase> Cases = LoadTurnCases();
		const int32 NSpec = Cases.FilterByPredicate([](const FTurnCase& K) { return K.Journal.Num() == 0 && !K.bRegular; }).Num();
		const int32 NRegular = Cases.FilterByPredicate([](const FTurnCase& K) { return K.bRegular; }).Num();
		const int32 NJoint = Cases.Num() - NSpec - NRegular;
		C->Expect(NRegular >= 5, FString::Printf(TEXT("T обычные сдачи (оригинальные ячейки, слоты 0…9, разные NPC): %d (ждали ≥ 5)"), NRegular));
		C->Expect(NSpec == 17, FString::Printf(TEXT("T оракул spec.json: ячеек сдачи %d (ждали 17 — 16 заданий, у 72 две)"), NSpec));
		C->Expect(NJoint == 4, FString::Printf(TEXT("T совместные прогоны Эллосы: %d (ждали 4 — 125, 126, 96 вместе и «10 + 1»)"), NJoint));
		C->Expect(Cases.ContainsByPredicate([](const FTurnCase& K) { return K.Quest == 96 && K.Npc == 56 && K.Slot == 10; }),
			TEXT("T оракул: 96 у Эллосы (56), слот 10 (таблица расширения)"));
		UAsdaQuestLogSubsystem* QL = C->Sub<UAsdaQuestLogSubsystem>();
		if (QL && QL->IsOpen()) { QL->Close(); }
		// «!» над Эллосой: в журнале одно 96 (его ячейка — слот 10, таблица расширения).
		{
			UAsdaQuestMarkerSubsystem* QM = C->Sub<UAsdaQuestMarkerSubsystem>();
			GEngine->Exec(C->W.Get(), TEXT("asda.Quest.FakeSelf 42 1 0"));
			GEngine->Exec(C->W.Get(), TEXT("asda.Quest.FakeState 96 0"));
			const int32 On = FAsdaQuestFollowSelfTestAccess::Deliverable(QM, TEXT("ellosa"));
			C->Expect(On == 99, FString::Printf(TEXT("T «!» над Эллосой при задании 96 (слот 10): %d (ждали 99 — «можно сдать»)"), On));
			// Ключ `asda.Npc.CellsBeyond10 0` — прежнее поведение: ячейки сверх 10 не учитываются, «!» за 96 нет.
			GEngine->Exec(C->W.Get(), TEXT("asda.Npc.CellsBeyond10 0"));
			const int32 Off = FAsdaQuestFollowSelfTestAccess::Deliverable(QM, TEXT("ellosa"));
			GEngine->Exec(C->W.Get(), TEXT("asda.Npc.CellsBeyond10 1"));
			C->Expect(Off != 99, FString::Printf(TEXT("T ключ asda.Npc.CellsBeyond10 0: «!» над Эллосой за 96 нет (%d, как без исправления)"), Off));
			// NPC сдачи 96 («Следовать» журнала и значок карты мира — общая точка `TurnInNpcs`) — Эллоса: и по правилу сервера
			//   (ключ FollowDoneToNpc 1), и прежним отбором по клиентскому SNQ (ключ 0).
			for (int32 ByStep = 0; ByStep < 2; ++ByStep)
			{
				TArray<AsdaQuestGuideTarget::FTurnIn> Npcs;
				AsdaQuestGuideTarget::TurnInNpcs(QM, 96, 0, ByStep != 0, Npcs);
				FString Got;
				bool bOnly56 = Npcs.Num() > 0;
				for (const AsdaQuestGuideTarget::FTurnIn& T : Npcs)
				{
					Got += FString::Printf(TEXT(" %d@%d(%d,%d)"), T.NpcId, T.Place.MapId, T.Place.TileX, T.Place.TileY);
					bOnly56 &= T.NpcId == 56 && T.Place.MapId >= 0;
				}
				C->Expect(bOnly56, FString::Printf(TEXT("T NPC сдачи 96 (%s) — только Эллоса 56 на карте:%s"),
					ByStep ? TEXT("правило сервера") : TEXT("прежний отбор SNQ"), *Got));
			}
		}
		TSharedRef<int32> Idx = MakeShared<int32>(0);
		TSharedRef<TFunction<void()>> Next = MakeShared<TFunction<void()>>();
		*Next = [C, Cases, Idx, Next]()
		{
			if (*Idx >= Cases.Num()) { Finish(C); return; }
			const FTurnCase K = Cases[(*Idx)++];
			const FString Tag = K.Journal.Num()
				? FString::Printf(TEXT("T Эллоса совместно (%d заданий), строка %s: задание %d → 5041 слот %d"), K.Journal.Num(),
					K.Row >= 0 ? *FString::FromInt(K.Row) : TEXT("последняя"), K.Quest, K.Slot)
				: FString::Printf(TEXT("T %s задание %d шаг %d → NPC %d слот %d"), K.bRegular ? TEXT("обычная сдача:") : TEXT(""),
					K.Quest, K.Stage, K.Npc, K.Slot);
			UAsdaNpcDialogSubsystem* D = C->Sub<UAsdaNpcDialogSubsystem>();
			if (!D) { C->Expect(false, TEXT("T подсистема диалога есть")); Finish(C); return; }
			D->DebugHideRewardPopup();
			D->ResetSessionState(EAsdaSessionResetReason::LeftWorld);
			GEngine->Exec(C->W.Get(), *FString::Printf(TEXT("asda.Quest.FakeSelf %d 1 0"), FMath::Max(K.Level, 1)));
			if (K.Journal.Num()) { ApplyJournalList(C, K.Journal); }
			else { GEngine->Exec(C->W.Get(), *FString::Printf(TEXT("asda.Quest.FakeState %d %d"), K.Quest, K.Stage)); }
			const bool bOpen = D->OpenForNpcId(K.Npc);
			C->Expect(bOpen, Tag + TEXT(": разговор открыт"));
			D->ShowMultiQuest();
			// Строки облачка строятся сразу; нужная — в видимую часть списка (прокрутка `ScrollBox`, как колесом у игрока).
			int32 NRows = 0;
			while (WinNode(C, TEXT("MULTYQUEST_WIN"), FString::Printf(TEXT("MULTYQUEST_ROW_2410_%d"), NRows))) { ++NRows; }
			const int32 RowIdx = K.Row >= 0 ? K.Row : NRows - 1;
			if (UWidget* Rw = WinNode(C, TEXT("MULTYQUEST_WIN"), FString::Printf(TEXT("MULTYQUEST_ROW_2410_%d"), RowIdx)))
			{
				UWidget* Holder = Rw->GetParent();
				if (UScrollBox* Sb = Holder ? Cast<UScrollBox>(Holder->GetParent()) : nullptr) { Sb->ScrollWidgetIntoView(Holder, false); }
			}
			At(C, 0.6f, [C, K, Tag, Next, NRows, RowIdx]()
			{
				// Строк «принимает» ровно столько, сколько заданий этого NPC в журнале («10 + 1» — сколько пропустят условия).
				if (K.Rows > 0)
				{
					C->Expect(NRows == K.Rows, FString::Printf(TEXT("%s: в облачке строк «принимает» %d (ждали %d)"), *Tag, NRows, K.Rows));
				}
				else
				{
					C->Expect(NRows >= 2, FString::Printf(TEXT("%s: в облачке строк «принимает» %d (журнал %d заданий; 96 — последняя)"), *Tag, NRows, K.Journal.Num()));
				}
				C->Mark = C->Sent.Num();
				ClickNode(C, WinNode(C, TEXT("MULTYQUEST_WIN"), FString::Printf(TEXT("MULTYQUEST_ROW_2410_%d"), RowIdx)), Tag + TEXT(" строка облачка"));
				At(C, 0.6f, [C, K, Tag, Next]()
				{
					UAsdaNpcDialogSubsystem* D = C->Sub<UAsdaNpcDialogSubsystem>();
					C->Expect(D && D->GetCurrentTalkId() == K.Talk,
						FString::Printf(TEXT("%s: страница сдачи %d (сейчас %d)"), *Tag, K.Talk, D ? D->GetCurrentTalkId() : -1));
					// Реплика страницы на языке сессии — утверждённая (RU — строка DT, EN — оверлей).
					const UDataTable* Dt = AsdaDt::Cached(TEXT("/Game/AsdaAssets/Data/NPC/DT_NpcNsrPage.DT_NpcNsrPage"));
					FString Nsr;
					for (const TPair<FName, uint8*>& It : Dt ? Dt->GetRowMap() : TMap<FName, uint8*>())
					{
						const FAsdaNsrPageRow* R = reinterpret_cast<const FAsdaNsrPageRow*>(It.Value);
						if (R && R->TalkId == K.Talk && It.Key.ToString().EndsWith(FString::Printf(TEXT("_%d"), K.Talk)) && R->Text.Replace(TEXT("\r"), TEXT("")) == K.Ru.Replace(TEXT("\r"), TEXT("")))
						{
							const FString Shown = AsdaLangRows::Text(Dt, R, TEXT("Text"), R->Text);
							const FString& Want = FAsdaLang::IsRu() ? K.Ru : K.En;
							C->Expect(Shown.Replace(TEXT("\r"), TEXT("")) == Want.Replace(TEXT("\r"), TEXT("")),
								FString::Printf(TEXT("%s: реплика (%s) «%s»"), *Tag, FAsdaLang::IsRu() ? TEXT("RU") : TEXT("EN"), *Shown.Replace(TEXT("\n"), TEXT(" / "))));
							Nsr = It.Key.ToString();
						}
					}
					if (!K.bRegular) { C->Expect(!Nsr.IsEmpty(), Tag + TEXT(": строка страницы в DT_NpcNsrPage найдена")); }
					const int32 Btn = StepButtonOf(K.Talk, K.Key);
					C->Expect(Btn >= 0, FString::Printf(TEXT("%s: кнопка шага с ключом %d на странице %d — NPC_QA_BUT0%d"), *Tag, K.Key, K.Talk, Btn));
					ClickNode(C, WinNode(C, TEXT("NPC_LINE_NPC"), FString::Printf(TEXT("NPC_QA_BUT0%d"), FMath::Max(Btn, 0))), Tag + TEXT(" кнопка сдачи"));
					At(C, 0.7f, [C, K, Tag, Next]()
					{
						UAsdaNpcDialogSubsystem* D = C->Sub<UAsdaNpcDialogSubsystem>();
						auto Count5041 = [C]() { int32 N = 0; for (int32 i = C->Mark; i < C->Sent.Num(); ++i) { N += C->Sent[i].Op == 5041; } return N; };
						auto Check = [C, K, Tag, Next, Count5041]()
						{
							int32 N = 0; TArray<uint8> B;
							for (int32 i = C->Mark; i < C->Sent.Num(); ++i) { if (C->Sent[i].Op == 5041) { ++N; B = C->Sent[i].Body; } }
							const bool bShape = N == 1 && B.Num() >= 5;
							const int32 Npc = bShape ? AsdaBytes::RdI16(B.GetData()) : -1;
							const int32 Slot = bShape ? AsdaBytes::RdI16(B.GetData() + 2) : -1;
							C->Expect(bShape && Npc == K.Npc && Slot == K.Slot,
								FString::Printf(TEXT("%s: ушла 5041 ×%d — NPC %d, слот ячейки %d (сервер: NPC %d, слот %d)"), *Tag, N, Npc, Slot, K.Npc, K.Slot));
							C->Sub<UAsdaNpcDialogSubsystem>()->DebugHideRewardPopup();
							At(C, 0.2f, [Next]() { (*Next)(); });
						};
						if (Count5041() > 0 || !D || !D->DebugRewardPopupVisible()) { Check(); return; }
						// Попап наград: у наград на выбор — сначала первая строка выбора.
						if (K.NumRewards > 1)
						{
							ClickNode(C, WinNode(C, TEXT("EPISODEPOP_WIN"), TEXT("EPISODEPOP_ITEMBACK06_grp")), Tag + TEXT(" строка выбора награды"));
						}
						else if (K.NumRewards < 0)
						{
							// Обычная сдача: число наград заранее не известно — строку выбора жмём, только если она на экране.
							FVector2D Pt;
							UWidget* Row = WinNode(C, TEXT("EPISODEPOP_WIN"), TEXT("EPISODEPOP_ITEMBACK06_grp"));
							if (Row && Row->IsVisible() && Center(Row, Pt)) { C->Sub<UAsdaWindowSubsystem>()->SimulateClickAt(Pt, 6); }
						}
						At(C, 0.4f, [C, Tag, Check]()
						{
							ClickNode(C, WinNode(C, TEXT("EPISODEPOP_WIN"), TEXT("EPISODEPOP_ENDBUT")), Tag + TEXT(" «Готово»"));
							At(C, 0.6f, [Check]() { Check(); });
						});
					});
				});
			});
		};
		// Сначала ключ 0: облачко Эллосы при одном 96 — строки «принимает» нет (как было); затем ключ 1 и прогон случаев.
		if (UAsdaNpcDialogSubsystem* D0 = C->Sub<UAsdaNpcDialogSubsystem>())
		{
			D0->ResetSessionState(EAsdaSessionResetReason::LeftWorld);
			GEngine->Exec(C->W.Get(), TEXT("asda.Quest.FakeSelf 42 1 0"));
			GEngine->Exec(C->W.Get(), TEXT("asda.Quest.FakeState 96 0"));
			GEngine->Exec(C->W.Get(), TEXT("asda.Npc.CellsBeyond10 0"));
			C->Expect(D0->OpenForNpcId(56), TEXT("T ключ asda.Npc.CellsBeyond10 0: разговор с Эллосой открыт"));
			D0->ShowMultiQuest();
		}
		At(C, 0.6f, [C, Next]()
		{
			C->Expect(WinNode(C, TEXT("MULTYQUEST_WIN"), TEXT("MULTYQUEST_ROW_2410_0")) == nullptr,
				TEXT("T ключ asda.Npc.CellsBeyond10 0: у Эллосы при задании 96 строки «принимает» нет (прежний предел 10 ячеек)"));
			GEngine->Exec(C->W.Get(), TEXT("asda.Npc.CellsBeyond10 1"));
			At(C, 0.3f, [Next]() { (*Next)(); });
		});
	}

	static void Finish(const FCtxRef& C)
	{
		ResetDest(C);
		UAsdaQuestLogSubsystem* QL = C->Sub<UAsdaQuestLogSubsystem>();
		UAsdaMinimapSubsystem* MM = C->Sub<UAsdaMinimapSubsystem>();
		if (QL) { for (int32 i = 0; i < kCells; ++i) { if (QL->IsSlotTracked(i)) { QL->SetSlotTracked(i, false); } } }
		if (MM && MM->IsBigMapOpen()) { MM->ToggleBigMap(); }
		if (QL && QL->IsOpen()) { QL->Close(); }
		if (UAsdaNpcDialogSubsystem* D = C->Sub<UAsdaNpcDialogSubsystem>()) { D->DebugHideRewardPopup(); D->ResetSessionState(EAsdaSessionResetReason::LeftWorld); }
		if (C->CapGen) { AsdaSelfTestKit::DisarmCapture(C->Sub<UAsdaNetSubsystem>(), C->CapGen); C->CapGen = 0; }
		ApplyJournal(C, true);   // журнал прогона — пустой (офлайн своего журнала нет)
		// Персонаж — стоп и на исходную клетку: маршруты своей карты двигали его, а следующие самотесты пакета (NavAttack)
		//   считают от своей позиции.
		UWorld* W = C->W.Get();
		if (AAsdaPlayerCharacter* Me = W && W->GetFirstPlayerController() ? Cast<AAsdaPlayerCharacter>(W->GetFirstPlayerController()->GetPawn()) : nullptr)
		{
			Me->StopMove();
			if (C->bHaveStart) { Me->NetSnap(C->StartGame); }
		}
		if (GEngine && C->W.IsValid()) { GEngine->Exec(C->W.Get(), TEXT("asda.Quest.FakeSelf")); }   // подмена уровня снята
		if (GEngine && C->W.IsValid()) { GEngine->Exec(C->W.Get(), TEXT("asda.Npc.CellsBeyond10 1")); }   // ключ — по умолчанию
		AsdaSelfTestKit::Finish(C->W.Get(), kCmd, C->bQuit, LogAsdaQuestFollowTest, TEXT("СЛЕДОВАТЬ-ТЕСТ"), C->Ok, C->Fail);
	}

	static void Run(const FCtxRef& C)
	{
		UAsdaWindowSubsystem* WM = C->Sub<UAsdaWindowSubsystem>();
		UAsdaQuestLogSubsystem* QL = C->Sub<UAsdaQuestLogSubsystem>();
		UAsdaLoadManagerSubsystem* LM = C->Sub<UAsdaLoadManagerSubsystem>();
		if (!WM || !QL || !LM || !C->Sub<UAsdaQuestMarkerSubsystem>() || !C->Sub<UAsdaMinimapSubsystem>())
		{
			C->Expect(false, TEXT("подсистемы окон/журнала/меток/мини-карты/загрузки есть"));
			Finish(C);
			return;
		}
		C->Expect(LM->CurrentMapId == 3, FString::Printf(TEXT("своя карта прогона — 3 (Alpen), сейчас %d"), LM->CurrentMapId));
		// Перехват отправок (раздел T: 5041 сдачи) — боевые отправители под перехватом, в сеть ничего не уходит.
		TWeakPtr<FCtx> WC(C);
		C->CapGen = AsdaSelfTestKit::ArmCapture(C->Sub<UAsdaNetSubsystem>(), [WC](uint16 Op, const uint8* P, int32 Len, bool, TCHAR)
		{
			if (TSharedPtr<FCtx> X = WC.Pin()) { FCtx::FSent S; S.Op = (int32)Op; if (P && Len > 0) { S.Body.Append(P, Len); } X->Sent.Add(MoveTemp(S)); }
		}, 0, kCmd);
		C->Expect(C->CapGen != 0, TEXT("перехват отправок свободен"));
		AsdaCVarSave::SetForTest(kKey, 1);
		AsdaCVarSave::SetForTest(TEXT("asda.Map.QuestTargets"), 1);
		AsdaCVarSave::SetForTest(TEXT("asda.Quest.FollowDoneToNpc"), 1);
		// Уровень своего персонажа — выше порогов QCT заданий прогона (19–60), как у самотеста значков карты мира.
		if (GEngine) { GEngine->Exec(C->W.Get(), TEXT("asda.Quest.FakeSelf 150 1 0")); }
		if (const APawn* Me = C->W->GetFirstPlayerController() ? C->W->GetFirstPlayerController()->GetPawn() : nullptr)
		{
			C->StartGame = AsdaCoord::UeToGame2D(Me->GetActorLocation());
			C->bHaveStart = true;
		}
		ApplyJournal(C, false);
		if (!QL->IsOpen()) { QL->Open(); }
		WM->BringWindowToFront(FName(kQuest));

		// Цепочка случаев A…F, затем G (ключ 0) и H (цель выполнена → NPC сдачи).
		TSharedRef<int32> Idx = MakeShared<int32>(0);
		TSharedRef<TFunction<void()>> Step = MakeShared<TFunction<void()>>();
		*Step = [C, Idx, Step]()
		{
			if (*Idx < (int32)UE_ARRAY_COUNT(kCases))
			{
				const FCase& K = kCases[(*Idx)++];
				RunCase(C, K, false, *Step);
				return;
			}
			// ═══ G. Ключ 0: строки без координат снова не ведут никуда (моб E и NPC F) ═══
			AsdaCVarSave::SetForTest(kKey, 0);
			RunCase(C, kCases[7], true, [C]()
			{
				RunCase(C, kCases[9], true, [C]()
				{
					At(C, 0.2f, [C]() { RunDone(C); });
				});
			});
		};
		At(C, 1.0f, [Step]() { (*Step)(); });
	}
}

static FAutoConsoleCommandWithWorldAndArgs GAsdaSelfTestQuestFollow(
	TEXT("asda.SelfTest.QuestFollow"),
	TEXT("Самотест кнопки «Следовать» (705…708) журнала заданий путём игрока: строки гида, исправленные ведомостью (перепутанные боссы, "
	     "точка вдали от спавна, ID тёзки NPC), строки без координат (моб / NPC) → место цели, ключ asda.Quest.FollowNoCoordTarget 0, "
	     "цель выполнена → NPC сдачи (5052 при открытом журнале, ключ asda.Quest.FollowDoneToNpc 0). Офлайн, карта мира. `delay=N` `quit`."),
	FConsoleCommandWithWorldAndArgsDelegate::CreateLambda([](const TArray<FString>& Args, UWorld* W)
	{
		using namespace AsdaQuestFollowTest;
		const FAsdaSelfTestArgs KA = FAsdaSelfTestArgs::Parse(Args);
		// Свой персонаж нужен гейтам мира — офлайн его нет: штатный фейковый спавн и повторный запуск через 2 с.
		if (!KA.Has(TEXT("selfready")) && W && GEngine && AsdaSelfTestKit::NeedSelfSpawn(W))
		{
			GEngine->Exec(W, TEXT("Asda.Load.CurrentMap 3"));
			GEngine->Exec(W, TEXT("Asda.World.FakeSelfSpawn 256 256"));
			const FString Again = FString::Printf(TEXT("%s selfready%s"), kCmd, KA.bQuit ? TEXT(" quit") : TEXT(""));
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
		FTimerHandle H;
		W->GetTimerManager().SetTimer(H, FTimerDelegate::CreateLambda([C]()
		{
			if (C->W.IsValid() && AsdaSelfTestKit::RefuseDeferred(C->W.Get(), kCmd, C->bQuit)) { return; }
			Run(C);
		}), FMath::Max(0.01f, KA.Delay), false);
	}));

#endif // ASDA_DEV_CONSOLE && !UE_BUILD_SHIPPING
