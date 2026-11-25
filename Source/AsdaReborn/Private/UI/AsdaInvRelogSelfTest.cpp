// Copyright (c) 2026 @sitxovski (Discord)
// ★ САМОТЕСТ «ПЕРЕЗАХОД ДРУГОЙ УЧЁТКОЙ НЕ ОСТАВЛЯЕТ ВЕЩЕЙ ПРОШЛОЙ» (25.10.2026): `asda.SelfTest.InvRelog [quit]`.
//   Жалоба игроков: «Inventory UI caches and retains items from previously logged-in account upon relogging with a different
//   account» — после выхода из учётки А и входа учёткой Б без перезапуска клиента окно инвентаря показывает вещи А.
//   Причина и исправление — шапка ключа `asda.Session.RelogClearItems` (`AsdaInventorySubsystem.cpp`), отчёт
//   `Reports/inventory_relog_leak/REPORT.md`.
//   Путь (закон diagnostic-must-use-real-path), без сети:
//     · содержимое обеих учёток подаётся ПАКЕТАМИ в штатный `HandlePacket` (`UAsdaNetSubsystem::DebugFeedPacket`):
//       1027 (надетое), 4045/4046 (сумка), 4048/4049 (расходники), 4051/4052 (склад, страница без запроса), 5059 (панель);
//       раскладка в модель — Tick подсистем, как в игре;
//     · «выход на логин» и «вход в мир» — штатная рассылка `UAsdaSessionResetSubsystem::Broadcast` с причинами `LeftWorld`
//       и `EnteringWorld`, между ними — 1027 учётки Б (он приходит РАНЬШЕ 1021). Травела нет: окно инвентаря остаётся
//       ТЕМ ЖЕ виджетом — это строже реальной игры (там травел пересоздаёт окно), иконки-призраки видны сразу.
//   Разделы:
//     A. учётка А: модель наполнена, окно рисует её вещи (проверка, что самотест вообще видит иконки);
//     B. сразу после сброса (до данных Б): контейнеры пусты, раскладка панели пуста, вкладки склада «не загружены»,
//        списки аукциона (строки выставления / мои лоты / выдача) пусты при прежних длинах;
//     C. учётка Б: в модели и в окне ТОЛЬКО вещи Б, иконок-призраков 0 (обе вкладки сетки и надетое), панель и склад — Б;
//     D. ключ `asda.Session.RelogClearItems 0` — прежнее поведение: вещи А переживают перезаход (самотест ловит дефект).
//   ★ 25.10.2026 (ревизия `Reports/inventory_relog_leak/REVIEW.md`, Н1–Н4) — ещё:
//     · чат: входящий шёпот 5089 (штатный приём) запоминает шептавшего, «R» (`AnswerLastWhisper`) ставит режим 7 ему (A9);
//       после перезахода режим 4, адресата нет, «R» без шептавшего → loc 6942 в ленту (B7/B8); ключи `asda.Chat.WhisperAnswer`
//       и `asda.Chat.RelogResetWhisper`;
//     · звания: маркеры «новое»/«потеряно» и надетая приставка сняты (B9), ключ `asda.Title.RelogClearFlags`;
//     · навыки: очки Б из 1017, разобранного В ТОМ ЖЕ кадре, что и 1021, — не очки А (B10), ключ `asda.SkillWin.RelogResetPoints`;
//     E. обратная проверка: `MapChanged` и `Disconnected` НЕ трогают вещи, панель, склад, аукцион, режим/адресата чата,
//        маркеры званий и очки навыков учётки Б;
//     D дополнительно: с ключами 0 режим/адресат чата, маркеры званий и очки А переживают перезаход.
//   Запускать на карте мира (`--map world` пакета), только офлайн (-AsdaNoNet). Сброс сессии задевает все подсистемы —
//   в пакете ставить отдельно или последним. Итог: «ПЕРЕЗАХОД ИТОГ: ok=N fail=M».
#include "CoreMinimal.h"
#include "AsdaDevConsole.h"

#if ASDA_DEV_CONSOLE && !UE_BUILD_SHIPPING

#include "AsdaBytes.h"
#include "AsdaCVarSave.h"
#include "AsdaNetSubsystem.h"
#include "AsdaSessionReset.h"
#include "Test/AsdaSelfTestKit.h"
#include "UI/AsdaChatWindowSubsystem.h"    // ★ 25.10.2026 (Н1): режим ввода / адресат шёпота / «R» — публичный путь окна чата
#include "UI/AsdaExchangeSubsystem.h"
#include "UI/AsdaHotbarSubsystem.h"
#include "UI/AsdaInventorySubsystem.h"
#include "UI/AsdaSkillWindowSubsystem.h"   // ★ 25.10.2026 (Н3): свободные очки навыков (`friend`)
#include "UI/AsdaTitleSubsystem.h"         // ★ 25.10.2026 (Н2): маркеры званий и надетая приставка (`friend`)
#include "Text/AsdaTextSubsystem.h"        // ★ 25.10.2026: текст loc 6942 для проверки ленты
#include "UI/AsdaWarehouseSubsystem.h"
#include "World/AsdaLoadManagerSubsystem.h"
#include "World/AsdaWorldKind.h"
#include "World/AsdaWorldSubsystem.h"
#include "Components/Image.h"
#include "Containers/Ticker.h"
#include "Engine/Engine.h"
#include "Engine/GameInstance.h"
#include "Engine/World.h"
#include "HAL/IConsoleManager.h"
#include "HAL/PlatformTime.h"

DEFINE_LOG_CATEGORY_STATIC(LogAsdaInvRelogTest, Log, All);

// Доступ самотеста к закрытым спискам аукциона (`friend` в `UAsdaExchangeSubsystem`): строки выставления — клиентские
//   копии вещей сумки, их сервер не присылает; лоты и выдачу кладём так же, как их кладут приёмники 9908/9904.
struct FAsdaInvRelogSelfTest
{
	// Положить по одной записи вещи `Code` в строки выставления, «мои лоты» и выдачу поиска.
	static void Fill(UAsdaExchangeSubsystem* X, int32 Code)
	{
		if (!X || X->RegRows.Num() == 0 || X->MyLots.Num() == 0 || X->Listings.Num() == 0) { return; }
		X->RegRows[0].SrcContainer = 1; X->RegRows[0].SrcCell = 0;
		X->RegRows[0].Item.ItemCode = Code; X->RegRows[0].Item.Amount = 1;
		X->MyLots[0].Serial = 1; X->MyLots[0].ItemCode = Code;
		X->Listings[0].Serial = 2; X->Listings[0].ItemCode = Code;
	}
	// Непустых записей в трёх списках и их длины (длина фиксированная — после сброса обязана сохраниться).
	static int32 Filled(const UAsdaExchangeSubsystem* X, FString& OutLens)
	{
		if (!X) { OutLens = TEXT("нет подсистемы"); return -1; }
		int32 N = 0;
		for (const FAsdaExchangeRegRow& R : X->RegRows) { if (!R.IsEmpty()) { ++N; } }
		for (const FAsdaExchangeMyLot& L : X->MyLots) { if (!L.IsEmpty()) { ++N; } }
		for (const FAsdaExchangeListing& L : X->Listings) { if (!L.IsEmpty()) { ++N; } }
		OutLens = FString::Printf(TEXT("длины %d/%d/%d"), X->RegRows.Num(), X->MyLots.Num(), X->Listings.Num());
		return N;
	}
	// ★ 25.10.2026 (Н2): звания учётки — мигающие маркеры «новое»/«потеряно» и надетая приставка (их кладут 6065/6066/6068).
	static void SeedTitle(UAsdaTitleSubsystem* T, int32 NewId, int32 LostId, int32 Prefix)
	{
		if (!T) { return; }
		T->FlagNew.Add(NewId); T->FlagLost.Add(LostId); T->WornPrefix = Prefix;
	}
	// Снимок званий: маркеров «новое», «потеряно», надетая приставка; true — `NewId` среди «новых».
	static bool TitleState(const UAsdaTitleSubsystem* T, int32 NewId, int32& OutNew, int32& OutLost, int32& OutPrefix)
	{
		OutNew = T ? T->FlagNew.Num() : -1; OutLost = T ? T->FlagLost.Num() : -1; OutPrefix = T ? T->WornPrefix : -2;
		return T && T->FlagNew.Contains(NewId);
	}
	// ★ 25.10.2026 (Н3): очки навыков учётки в копии окна (так их кладёт тик из очереди `PendingPoints`).
	static void SetSkillPoints(UAsdaSkillWindowSubsystem* S, int32 P) { if (S) { S->SkillPoints = P; } }
};

namespace AsdaInvRelogSelfTest
{
	static const TCHAR* kCmd = TEXT("asda.SelfTest.InvRelog");   // имя команды (итог, отказ, возврат ключей)
	static const TCHAR* kTag = TEXT("ПЕРЕЗАХОД");                  // префикс строк журнала и итога
	static const TCHAR* kKey = TEXT("asda.Session.RelogClearItems");   // ключ исправления (этот файл в Shipping не собирается)
	static const TCHAR* kKeyChat = TEXT("asda.Chat.RelogResetWhisper");      // Н1: режим/адресат шёпота
	static const TCHAR* kKeyTitle = TEXT("asda.Title.RelogClearFlags");      // Н2: маркеры званий и приставка
	static const TCHAR* kKeySkill = TEXT("asda.SkillWin.RelogResetPoints");  // Н3: очки навыков
	static const TCHAR* kKeyAnswer = TEXT("asda.Chat.WhisperAnswer");        // «R»: запоминание шептавшего (в обоих проходах 1)

	// Ключи сброса сессии этой волны (кроме «R»): все — 1 или все — 0.
	static void SetRelogKeys(int32 V)
	{
		AsdaCVarSave::SetForTest(kKey, V);
		AsdaCVarSave::SetForTest(kKeyChat, V);
		AsdaCVarSave::SetForTest(kKeyTitle, V);
		AsdaCVarSave::SetForTest(kKeySkill, V);
	}

	// Запись предмета для батча: (ячейка, код, количество).
	struct FRec { int32 Cell; int32 Code; int32 Amount; };

	// Набор учётки: сумка (контейнер 1), расходники (2), склад (5, вкладка 0), надетое (1027: слот → код), панель (5059).
	struct FAccount
	{
		const TCHAR* Name;            // «А» / «Б» — для журнала
		TArray<FRec> Shell;           // 4045
		TArray<FRec> Spend;           // 4048
		TArray<FRec> Store;           // 4051
		TArray<FRec> Equip;           // 1027: Cell = экип-слот 0..10
		TArray<int32> HotbarPages;    // страницы 5059, которые сервер шлёт этой учётке
		int32 HotbarCol = 0;          // колонка эмоции на каждой присланной странице
		const TCHAR* Whisperer = TEXT("");   // кто шепчет этой учётке (5089)
		int32 SkillPts = 0;           // свободные очки навыков (блок 1017 +39)
		int32 TitleNew = 0, TitleLost = 0, TitlePrefix = -1;   // маркеры званий и надетая приставка
	};

	// Учётка А: вещи во всех контейнерах + раскладка панели на страницах 0 и 3.
	static FAccount AccountA()
	{
		FAccount A; A.Name = TEXT("А");
		A.Shell = { {0, 70871, 1}, {5, 62277, 1}, {17, 41782, 1}, {29, 63033, 1} };
		A.Spend = { {3, 461, 7}, {40, 20593, 3} };
		A.Store = { {2, 20566, 50}, {12, 70871, 1} };
		A.Equip = { {0, 41782, 1}, {9, 70871, 1} };
		A.HotbarPages = { 0, 3 };
		A.HotbarCol = 0;
		A.Whisperer = TEXT("WhisperA"); A.SkillPts = 7; A.TitleNew = 5; A.TitleLost = 6; A.TitlePrefix = 12;
		return A;
	}

	// Учётка Б: другой, меньший набор; ячейка 5 сумки совпадает с А (перезапись), расходников нет вовсе,
	//   надето только оружие, панель — только страница 0.
	static FAccount AccountB()
	{
		FAccount B; B.Name = TEXT("Б");
		B.Shell = { {1, 63033, 1}, {5, 70871, 1} };
		B.Spend = {};
		B.Store = { {7, 461, 2} };
		B.Equip = { {9, 63033, 1} };
		B.HotbarPages = { 0 };
		B.HotbarCol = 2;
		B.Whisperer = TEXT("WhisperB"); B.SkillPts = 3; B.TitleNew = 8; B.TitleLost = 9; B.TitlePrefix = 14;
		return B;
	}

	// Состояние прогона.
	struct FRun
	{
		TWeakObjectPtr<UGameInstance> GI;   // экземпляр игры
		TWeakObjectPtr<UWorld> World;       // мир прогона
		bool bQuit = false;                 // выйти после итога
		int32 Ok = 0, Fail = 0;             // счёт проверок
		int32 Step = 0;                     // шаг машины состояний
		double StepAt = 0.0;                // время входа в шаг
		bool bLegacyPass = false;           // идёт проход D (ключ 0)
		TWeakObjectPtr<AActor> LocalWas;    // свой актор мира до прогона (вернуть)
		int32 MapWas = -1;                  // `CurrentMapId` до прогона (вернуть)
	};
	static TSharedPtr<FRun> GRun;

	// Записать результат проверки.
	static void Expect(FRun& R, bool b, const FString& What)
	{
		if (b) { ++R.Ok; UE_LOG(LogAsdaInvRelogTest, Log, TEXT("%s ok   %s"), kTag, *What); }
		else { ++R.Fail; UE_LOG(LogAsdaInvRelogTest, Error, TEXT("%s FAIL %s"), kTag, *What); }
	}

	static UAsdaInventorySubsystem* Inv(const FRun& R) { return R.GI.IsValid() ? R.GI->GetSubsystem<UAsdaInventorySubsystem>() : nullptr; }
	static UAsdaNetSubsystem* Net(const FRun& R) { return R.GI.IsValid() ? R.GI->GetSubsystem<UAsdaNetSubsystem>() : nullptr; }
	static UAsdaHotbarSubsystem* Hot(const FRun& R) { return R.GI.IsValid() ? R.GI->GetSubsystem<UAsdaHotbarSubsystem>() : nullptr; }
	static UAsdaWarehouseSubsystem* Ware(const FRun& R) { return R.GI.IsValid() ? R.GI->GetSubsystem<UAsdaWarehouseSubsystem>() : nullptr; }
	static UAsdaWorldSubsystem* WorldSub(const FRun& R) { return R.GI.IsValid() ? R.GI->GetSubsystem<UAsdaWorldSubsystem>() : nullptr; }
	static UAsdaLoadManagerSubsystem* Load(const FRun& R) { return R.GI.IsValid() ? R.GI->GetSubsystem<UAsdaLoadManagerSubsystem>() : nullptr; }
	static UAsdaExchangeSubsystem* Exch(const FRun& R) { return R.GI.IsValid() ? R.GI->GetSubsystem<UAsdaExchangeSubsystem>() : nullptr; }
	static UAsdaSessionResetSubsystem* Reset(const FRun& R) { return R.GI.IsValid() ? R.GI->GetSubsystem<UAsdaSessionResetSubsystem>() : nullptr; }
	static UAsdaChatWindowSubsystem* Chat(const FRun& R) { return R.GI.IsValid() ? R.GI->GetSubsystem<UAsdaChatWindowSubsystem>() : nullptr; }
	static UAsdaTitleSubsystem* Title(const FRun& R) { return R.GI.IsValid() ? R.GI->GetSubsystem<UAsdaTitleSubsystem>() : nullptr; }
	static UAsdaSkillWindowSubsystem* Skill(const FRun& R) { return R.GI.IsValid() ? R.GI->GetSubsystem<UAsdaSkillWindowSubsystem>() : nullptr; }

	// Инвентарь раскладывает батчи только «в мире» (карта ≥ 0 и свой актор) — офлайн-карте их даём, как самотест текста.
	static void EnsureInWorld(FRun& R)
	{
		UWorld* W = R.World.Get();
		UAsdaWorldSubsystem* WS = WorldSub(R);
		UAsdaLoadManagerSubsystem* LM = Load(R);
		if (!W || !WS || !LM) { return; }
		if (LM->CurrentMapId < 0) { LM->CurrentMapId = 3; }
		if (!::IsValid(WS->LocalPlayerActor.Get()))
		{
			if (R.LocalWas.IsValid()) { WS->LocalPlayerActor = R.LocalWas.Get(); }
			else { WS->DebugSpawnSelfAt(256, 256, TEXT("InvRelog")); }
		}
	}

	// 57-байтовая запись предмета (как у сервера: код @0, контейнер @4, ячейка @5, количество @9, вес стопки @14).
	static void PutRecord(uint8* R57, int32 Cont, const FRec& E)
	{
		FMemory::Memzero(R57, FAsdaItemInstance::kRecordSize);
		AsdaBytes::WrI32(R57 + 0, E.Code);
		R57[4] = (uint8)Cont;
		AsdaBytes::WrI16(R57 + 5, E.Cell);
		AsdaBytes::WrI32(R57 + 9, E.Amount);
		AsdaBytes::WrI16(R57 + 14, FMath::Max(1, E.Amount));
	}

	// Батч контейнера (тело 4045/4048/4051 = записи подряд).
	static TArray<uint8> Batch(int32 Cont, const TArray<FRec>& Recs)
	{
		TArray<uint8> B; B.SetNumZeroed(FAsdaItemInstance::kRecordSize * Recs.Num());
		for (int32 i = 0; i < Recs.Num(); ++i) { PutRecord(B.GetData() + FAsdaItemInstance::kRecordSize * i, Cont, Recs[i]); }
		return B;
	}

	// Тело 1027 «успех»: [0]=1, с +5 — 11 записей по 57 б, индекс = экип-слот.
	static void Feed1027(FRun& R, const FAccount& A)
	{
		TArray<uint8> B; B.SetNumZeroed(5 + FAsdaItemInstance::kRecordSize * 11);
		B[0] = 1;
		for (const FRec& E : A.Equip) { PutRecord(B.GetData() + 5 + FAsdaItemInstance::kRecordSize * E.Cell, 3, E); }
		Net(R)->DebugFeedPacket(1027, B);
	}

	// Содержимое учётки после входа в мир: сумка, расходники, страница склада без запроса, страницы панели.
	static void FeedWorldData(FRun& R, const FAccount& A)
	{
		UAsdaNetSubsystem* N = Net(R);
		N->DebugFeedPacket(4045, Batch(1, A.Shell));
		N->DebugFeedPacket(4046, TArray<uint8>());
		N->DebugFeedPacket(4048, Batch(2, A.Spend));
		N->DebugFeedPacket(4049, TArray<uint8>());
		N->DebugFeedPacket(4051, Batch(5, A.Store));
		N->DebugFeedPacket(4052, TArray<uint8>());
		for (const int32 Page : A.HotbarPages)
		{
			// 5059: u8 страница + 13 записей по 10 б; пустая запись — тип 0. В колонке HotbarCol — эмоция (тип 2, id 1).
			TArray<uint8> P; P.SetNumZeroed(1 + 13 * 10);
			P[0] = (uint8)Page;
			uint8* S = P.GetData() + 1 + 10 * A.HotbarCol;
			S[0] = 2; S[1] = 0; AsdaBytes::WrI16(S + 2, 0); AsdaBytes::WrI32(S + 4, 1); AsdaBytes::WrI16(S + 8, 1);
			N->DebugFeedPacket(5059, P);
		}
	}

	// Иконка нарисована: видима, есть ресурс и кисть рисуется.
	static bool IconDrawn(const UWidget* Wd)
	{
		const UImage* Img = Cast<UImage>(Wd);
		if (!Img || !Img->IsVisible()) { return false; }
		const FSlateBrush& B = Img->GetBrush();
		return B.GetResourceObject() != nullptr && B.DrawAs != ESlateBrushDrawType::NoDrawType;
	}

	// Множество непустых ячеек контейнера (номера) и их коды — для сравнения с ожидаемым набором.
	static TMap<int32, int32> CellsOf(const UAsdaInventorySubsystem* I, int32 Cont)
	{
		TMap<int32, int32> M;
		const int32 N = I ? I->GetContainerSize(Cont) : 0;
		for (int32 c = 0; c < N; ++c)
		{
			const FAsdaItemInstance* It = I->GetCell(Cont, c);
			if (It && !It->IsEmpty()) { M.Add(c, It->ItemCode); }
		}
		return M;
	}

	// Ожидаемое содержимое контейнера = ровно эти записи.
	static bool SameAs(const TMap<int32, int32>& Got, const TArray<FRec>& Want, FString& OutDiff)
	{
		bool b = Got.Num() == Want.Num();
		for (const FRec& E : Want) { const int32* C = Got.Find(E.Cell); if (!C || *C != E.Code) { b = false; } }
		OutDiff.Reset();
		for (const TPair<int32, int32>& P : Got) { OutDiff += FString::Printf(TEXT("%d:%d "), P.Key, P.Value); }
		if (OutDiff.IsEmpty()) { OutDiff = TEXT("пусто"); }
		return b;
	}

	// Сетка окна по вкладке: (нарисовано иконок, из них «призраков» — нарисовано при пустой ячейке модели).
	static void CountGrid(const FRun& R, int32 Tab, int32& OutDrawn, int32& OutGhosts)
	{
		OutDrawn = 0; OutGhosts = 0;
		UAsdaInventorySubsystem* I = Inv(R);
		if (!I) { return; }
		I->SetTab(Tab);
		const int32 Cont = I->GetGridContainer();
		const int32 First = UAsdaInventorySubsystem::kGridCols * I->GetScrollRow();   // ★ 29.10.2026 (инвентарь ×2): 12 × 5
		for (int32 c = 0; c < UAsdaInventorySubsystem::kGridCells; ++c)
		{
			const bool bDrawn = IconDrawn(I->DebugGetGridIcon(c));
			const FAsdaItemInstance* It = I->GetCell(Cont, First + c);
			if (bDrawn) { ++OutDrawn; }
			if (bDrawn && (!It || It->IsEmpty())) { ++OutGhosts; }
		}
	}

	// Надетое в окне: призраки = нарисованная иконка слота при пустом контейнере 3.
	static int32 CountEquipGhosts(const FRun& R)
	{
		UAsdaInventorySubsystem* I = Inv(R);
		int32 Ghosts = 0;
		for (int32 s = 0; s < 11; ++s)
		{
			const FAsdaItemInstance* It = I ? I->GetCell(3, s) : nullptr;
			if (I && IconDrawn(I->DebugGetEquipSlotIcon(s)) && (!It || It->IsEmpty())) { ++Ghosts; }
		}
		return Ghosts;
	}

	// Вкладка 0 склада загружена? (по журналу состояния окна — поле класса закрытое).
	static bool WareTab0Loaded(const FRun& R)
	{
		const UAsdaWarehouseSubsystem* Wh = Ware(R);
		return Wh && Wh->DumpState().Contains(TEXT(" 0=загр"));
	}

	// «Выход на логин → выбор Б → 1017 Б (очки навыков) → 1027 Б → 1021 (вход в мир)» — ВСЁ в одном кадре: тик между пакетами
	//   не проходит, поэтому очки 1017 лежат в очереди окна навыков в момент сброса 1021 (худший случай Н3).
	static void RelogTo(FRun& R, const FAccount& B)
	{
		UAsdaSessionResetSubsystem* S = Reset(R);
		S->Broadcast(EAsdaSessionResetReason::LeftWorld);
		UAsdaNetSubsystem* N = Net(R);
		N->SelfSkillPoints = B.SkillPts;               // как приёмник 1017 (`AsdaNetSubsystem.cpp`, блок +39)
		N->OnSelfSkillPoints.Broadcast(B.SkillPts);
		Feed1027(R, B);
		S->Broadcast(EAsdaSessionResetReason::EnteringWorld);
	}

	// Входящий шёпот 5089 (flag 0, msgType 1): charId @2, имя cp1251 @8 (20 б), SoulState @28 = 0, текст @29 — штатный приём.
	static void Feed5089(FRun& R, const TCHAR* From)
	{
		TArray<uint8> B; B.SetNumZeroed(29 + 3);
		B[0] = 0; B[1] = 1;
		AsdaBytes::WrI32(B.GetData() + 2, 777001);
		AsdaBytes::WrI16(B.GetData() + 6, Net(R)->SelfUserNum);   // ★ 05.11.2026: WORD@6 = userNum получателя (свой) — иначе `asda.Chat.WhisperUserNumCheck` отбросит как «шёпот не мне»
		const FTCHARToUTF8 Name(From);   // имена самотеста — латиница (cp1251 = ASCII)
		FMemory::Memcpy(B.GetData() + 8, Name.Get(), FMath::Min(Name.Length(), 19));
		B[29] = 'h'; B[30] = 'i';
		Net(R)->DebugFeedPacket(5089, B);
	}

	// Сессия учётки вне вещей: шёпот ей (и «R» в ответ), маркеры званий и надетая приставка, свободные очки навыков.
	static void SeedSession(FRun& R, const FAccount& A, const TCHAR* P)
	{
		UAsdaChatWindowSubsystem* C = Chat(R);
		if (!C) { Expect(R, false, TEXT("нет подсистемы окна чата")); return; }
		Feed5089(R, A.Whisperer);
		C->AnswerLastWhisper();   // клавиша 69 «R»
		const int32 St = C->GetInputState();
		const FString To = C->DebugWhisperTarget();
		Expect(R, St == 7 && To == A.Whisperer,
			FString::Printf(TEXT("%sA9: «R» после входящего шёпота 5089 → режим %d (ждём 7), адресат «%s» (ждём «%s»)"), P, St, *To, A.Whisperer));
		C->CloseInput();          // режим 7 и адресат остаются — как у игрока, который ответил и закрыл строку
		FAsdaInvRelogSelfTest::SeedTitle(Title(R), A.TitleNew, A.TitleLost, A.TitlePrefix);
		FAsdaInvRelogSelfTest::SetSkillPoints(Skill(R), A.SkillPts);
	}

	// Сессия учётки `A` на месте (обратная проверка и проход D): режим 7 с адресатом, маркер «новое», приставка, очки.
	static void ExpectSessionKept(FRun& R, const FAccount& A, const FString& Tag)
	{
		UAsdaChatWindowSubsystem* C = Chat(R);
		const int32 St = C ? C->GetInputState() : -1;
		const FString To = C ? C->DebugWhisperTarget() : FString();
		Expect(R, St == 7 && To == A.Whisperer, FString::Printf(TEXT("%s: чат — режим %d, адресат «%s» (ждём 7 / «%s»)"), *Tag, St, *To, A.Whisperer));
		int32 NNew = 0, NLost = 0, Prefix = 0;
		const bool bNew = FAsdaInvRelogSelfTest::TitleState(Title(R), A.TitleNew, NNew, NLost, Prefix);
		Expect(R, bNew && NLost >= 1 && Prefix == A.TitlePrefix,
			FString::Printf(TEXT("%s: звания — «новое» %d (есть id %d: %d), «потеряно» %d, приставка %d (ждём %d)"),
				*Tag, NNew, A.TitleNew, bNew ? 1 : 0, NLost, Prefix, A.TitlePrefix));
		const int32 Pts = Skill(R) ? Skill(R)->GetSkillPoints() : -100;
		Expect(R, Pts == A.SkillPts, FString::Printf(TEXT("%s: навыки — свободных очков %d (ждём %d)"), *Tag, Pts, A.SkillPts));
	}

	static void Done(FRun& R)
	{
		// Уборка: штатный сброс новой сессии с ключом 1 снимает всё, что положил прогон; карта и свой актор — как были.
		SetRelogKeys(1);
		AsdaCVarSave::SetForTest(kKeyAnswer, 1);
		if (UAsdaSessionResetSubsystem* S = Reset(R)) { S->Broadcast(EAsdaSessionResetReason::LeftWorld); }
		if (UAsdaLoadManagerSubsystem* LM = Load(R)) { LM->CurrentMapId = R.MapWas; }
		if (UAsdaWorldSubsystem* WS = WorldSub(R)) { if (R.LocalWas.IsValid()) { WS->LocalPlayerActor = R.LocalWas.Get(); } }
		AsdaSelfTestKit::Finish(R.World.Get(), kCmd, R.bQuit, LogAsdaInvRelogTest, kTag, R.Ok, R.Fail);
		GRun.Reset();
	}

	// Шаги (на тиках ядра: Tick подсистем раскладывает пакеты между ними).
	static bool OnTick(float)
	{
		if (!GRun.IsValid()) { return false; }
		FRun& R = *GRun;
		const double Now = FPlatformTime::Seconds();
		const double InStep = Now - R.StepAt;
		auto Next = [&R, Now]() { ++R.Step; R.StepAt = Now; };
		const FAccount A = AccountA(), B = AccountB();
		const TCHAR* P = R.bLegacyPass ? TEXT("D") : TEXT("");   // префикс строк прохода D
		UAsdaInventorySubsystem* I = Inv(R);
		if (!I || !Net(R) || !Hot(R) || !Ware(R) || !Reset(R) || !Chat(R) || !Title(R) || !Skill(R)) { Expect(R, false, TEXT("подсистемы пропали")); Done(R); return false; }
		EnsureInWorld(R);

		switch (R.Step)
		{
		case 0:   // учётка А: вход (1027 до 1021) и данные мира
		{
			if (!R.bLegacyPass) { SetRelogKeys(1); }
			AsdaCVarSave::SetForTest(kKeyAnswer, 1);
			RelogTo(R, A);
			FeedWorldData(R, A);
			Next();
			return true;
		}
		case 1:   // ждём раскладку А
		{
			const FAsdaItemInstance* It = I->GetCell(1, 0);
			const bool bReady = It && It->ItemCode == 70871 && I->IsContainerReceived(1) && I->IsContainerReceived(2);
			if (!bReady)
			{
				if (InStep > 15.0) { Expect(R, false, FString::Printf(TEXT("%sA: за 15 с батчи учётки А не разложены (нет «в мире»?)"), P)); Done(R); return false; }
				return true;
			}
			// ⚠ Строку сравнения считаем ДО вызова `Expect`: порядок вычисления аргументов в MSVC — справа налево.
			// В проходе D (ключ 0) модель уже несёт остатки прошлого прохода — точные наборы А там не сверяются.
			if (!R.bLegacyPass)
			{
				FString D1, D2, D3, D4;
				const bool b1 = SameAs(CellsOf(I, 1), A.Shell, D1), b2 = SameAs(CellsOf(I, 2), A.Spend, D2);
				const bool b3 = SameAs(CellsOf(I, 3), A.Equip, D3), b4 = SameAs(CellsOf(I, 5), A.Store, D4);
				Expect(R, b1, FString::Printf(TEXT("A1: сумка = набор А (%s)"), *D1));
				Expect(R, b2, FString::Printf(TEXT("A2: расходники = набор А (%s)"), *D2));
				Expect(R, b3, FString::Printf(TEXT("A3: надетое = набор А (%s)"), *D3));
				Expect(R, b4, FString::Printf(TEXT("A4: склад = набор А (%s)"), *D4));
			}
			Expect(R, !Hot(R)->GetSlot(13 * 3 + A.HotbarCol).IsEmpty(), FString::Printf(TEXT("%sA5: панель, страница 3 — слот А занят"), P));
			Expect(R, WareTab0Loaded(R), FString::Printf(TEXT("%sA6: склад: вкладка 0 «загружена» (страница без запроса)"), P));
			{
				FAsdaInvRelogSelfTest::Fill(Exch(R), 70871);   // аукцион А: строка выставления, мой лот, выдача
				FString L;
				const int32 N = FAsdaInvRelogSelfTest::Filled(Exch(R), L);
				Expect(R, N == 3, FString::Printf(TEXT("%sA8: аукцион А: записей %d из 3 (%s)"), P, N, *L));
			}
			SeedSession(R, A, P);   // A9: шёпот и «R»; звания и очки А
			I->OpenWindow();
			Next();
			return true;
		}
		case 2:   // окно А нарисовано
		{
			if (InStep < 1.0) { return true; }
			int32 Drawn = 0, Ghosts = 0;
			CountGrid(R, 1, Drawn, Ghosts);
			Expect(R, I->IsWindowOpen() && Drawn >= 1 && Ghosts == 0,
				FString::Printf(TEXT("%sA7: окно открыто, вкладка «Снаряжение»: иконок %d, призраков %d"), P, Drawn, Ghosts));
			// Выход на логин и вход учёткой Б (до данных мира Б).
			RelogTo(R, B);
			const bool bClear = !R.bLegacyPass;
			FString D;
			const bool bShellEmpty = CellsOf(I, 1).Num() == 0, bSpendEmpty = CellsOf(I, 2).Num() == 0, bStoreEmpty = CellsOf(I, 5).Num() == 0;
			const bool bHotEmpty = Hot(R)->GetSlot(13 * 3 + A.HotbarCol).IsEmpty() && Hot(R)->GetSlot(A.HotbarCol).IsEmpty();
			if (bClear)
			{
				Expect(R, bShellEmpty && bSpendEmpty && bStoreEmpty, TEXT("B1: после сброса контейнеры 1/2/5 пусты"));
				const bool bEquipEmpty = SameAs(CellsOf(I, 3), TArray<FRec>(), D);
				Expect(R, bEquipEmpty, FString::Printf(TEXT("B2: надетое пусто, 1027 Б ждёт в очереди (%s)"), *D));
				Expect(R, bHotEmpty, TEXT("B3: раскладка панели прошлой сессии снята"));
				Expect(R, !WareTab0Loaded(R) && !Ware(R)->IsOpen(), TEXT("B4: склад: вкладки «не загружены», окно закрыто"));
				Expect(R, !I->IsWindowOpen(), TEXT("B5: окно инвентаря закрыто сбросом"));
				FString L;
				const int32 N = FAsdaInvRelogSelfTest::Filled(Exch(R), L);
				Expect(R, N == 0 && L == TEXT("длины 5/10/8"),
					FString::Printf(TEXT("B6: аукцион: записей прошлой сессии %d, %s (фиксированные длины сохранены)"), N, *L));
				// Н1: режим и адресат шёпота А не дожили до Б; «R» без шептавшего — loc 6942 строкой ленты, режим прежний.
				UAsdaChatWindowSubsystem* C = Chat(R);
				const int32 St = C->GetInputState();
				const FString To = C->DebugWhisperTarget();
				Expect(R, St == 4 && To.IsEmpty(), FString::Printf(TEXT("B7: чат — режим %d (ждём 4 «Общий»), адресат «%s» (ждём пусто)"), St, *To));
				UAsdaTextSubsystem* Txt = R.GI->GetSubsystem<UAsdaTextSubsystem>();
				const FString Loc6942 = Txt ? Txt->GetTextById(6942) : FString();
				const int32 Before = Loc6942.IsEmpty() ? -1 : C->DebugCountLogLines(Loc6942);
				C->AnswerLastWhisper();
				const int32 After = Loc6942.IsEmpty() ? -1 : C->DebugCountLogLines(Loc6942);
				const int32 St2 = C->GetInputState();
				const FString To2 = C->DebugWhisperTarget();
				// ★ 08.11.2026: при asda.Chat.WhisperAnswerNoTargetSilent = 1 (исправление) R без шептавшего строки 6942 не пишет.
				const IConsoleVariable* Silent = IConsoleManager::Get().FindConsoleVariable(TEXT("asda.Chat.WhisperAnswerNoTargetSilent"));
				const int32 Want = (Silent && Silent->GetInt() != 0) ? 0 : 1;   // ждём строк 6942 сверх прежних
				Expect(R, St2 == 4 && To2.IsEmpty() && Before >= 0 && After == Before + Want,
					FString::Printf(TEXT("B8: «R» после перезахода — шептавшего нет: режим %d, адресат «%s», строк loc 6942 %d → %d (ждём +%d)"),
						St2, *To2, Before, After, Want));
				C->CloseInput();
				// Н2: маркеры званий и приставка А сняты.
				int32 NNew = 0, NLost = 0, Prefix = 0;
				FAsdaInvRelogSelfTest::TitleState(Title(R), A.TitleNew, NNew, NLost, Prefix);
				Expect(R, NNew == 0 && NLost == 0 && Prefix == -1,
					FString::Printf(TEXT("B9: звания — «новое» %d, «потеряно» %d, приставка %d (ждём 0/0/−1)"), NNew, NLost, Prefix));
				// Н3: очки Б из 1017, разобранного в одном кадре с 1021 (очередь окна выброшена сбросом).
				const int32 Pts = Skill(R)->GetSkillPoints();
				Expect(R, Pts == B.SkillPts, FString::Printf(TEXT("B10: навыки — свободных очков %d (ждём %d учётки Б, у А было %d)"), Pts, B.SkillPts, A.SkillPts));
			}
			else
			{
				FString L;
				const int32 N = FAsdaInvRelogSelfTest::Filled(Exch(R), L);
				Expect(R, !bShellEmpty && !bHotEmpty && WareTab0Loaded(R) && N == 3,
					FString::Printf(TEXT("D-B: ключ 0 — прежнее: вещи А, панель, флаг склада и аукцион (%d) пережили сброс"), N));
				ExpectSessionKept(R, A, TEXT("D-B2 ключи 0 — прежнее, сессия А пережила перезаход"));
			}
			FeedWorldData(R, B);
			Next();
			return true;
		}
		case 3:   // ждём раскладку Б
		{
			const FAsdaItemInstance* It = I->GetCell(1, 1);
			const bool bReady = It && It->ItemCode == 63033 && I->IsContainerReceived(1) && I->IsContainerReceived(2);
			if (!bReady)
			{
				if (InStep > 15.0) { Expect(R, false, FString::Printf(TEXT("%sC: за 15 с батчи учётки Б не разложены"), P)); Done(R); return false; }
				return true;
			}
			I->OpenWindow();
			Next();
			return true;
		}
		case 4:   // модель и окно Б
		{
			if (InStep < 1.0) { return true; }
			FString D1, D2, D3, D4;
			const bool bShell = SameAs(CellsOf(I, 1), B.Shell, D1);
			const bool bSpend = SameAs(CellsOf(I, 2), B.Spend, D2);
			const bool bEquip = SameAs(CellsOf(I, 3), B.Equip, D3);
			const bool bStore = SameAs(CellsOf(I, 5), B.Store, D4);
			const bool bHotOld = !Hot(R)->GetSlot(13 * 3 + A.HotbarCol).IsEmpty() || !Hot(R)->GetSlot(A.HotbarCol).IsEmpty();
			const bool bHotNew = !Hot(R)->GetSlot(B.HotbarCol).IsEmpty();
			int32 DrawnShell = 0, GhostShell = 0, DrawnSpend = 0, GhostSpend = 0;
			CountGrid(R, 1, DrawnShell, GhostShell);
			CountGrid(R, 0, DrawnSpend, GhostSpend);
			const int32 GhostEquip = CountEquipGhosts(R);
			if (!R.bLegacyPass)
			{
				Expect(R, bShell, FString::Printf(TEXT("C1: сумка = только набор Б (%s)"), *D1));
				Expect(R, bSpend, FString::Printf(TEXT("C2: расходники = только набор Б (%s)"), *D2));
				Expect(R, bEquip, FString::Printf(TEXT("C3: надетое = только набор Б, 1027 пережил сброс 1021 (%s)"), *D3));
				Expect(R, bStore, FString::Printf(TEXT("C4: склад = только набор Б (%s)"), *D4));
				Expect(R, !bHotOld && bHotNew, TEXT("C5: панель — только раскладка Б (страница 3 пуста, слот Б занят)"));
				Expect(R, I->IsWindowOpen() && GhostShell == 0 && DrawnShell <= B.Shell.Num(),
					FString::Printf(TEXT("C6: окно, «Снаряжение»: иконок %d (вещей Б %d), призраков %d"), DrawnShell, B.Shell.Num(), GhostShell));
				Expect(R, GhostSpend == 0 && DrawnSpend == 0,
					FString::Printf(TEXT("C7: окно, «Расходники»: иконок %d, призраков %d (у Б расходников нет)"), DrawnSpend, GhostSpend));
				Expect(R, GhostEquip == 0, FString::Printf(TEXT("C8: окно, надетое: призраков %d"), GhostEquip));
				Expect(R, WareTab0Loaded(R), TEXT("C9: склад: вкладка 0 снова «загружена» — страницей Б"));
				// ★ E (Н4) — обратная проверка: смена карты и обрыв связи сессию Б НЕ трогают.
				I->CloseWindow();
				FAsdaInvRelogSelfTest::Fill(Exch(R), 63033);
				SeedSession(R, B, TEXT("E-"));
				const EAsdaSessionResetReason kKeep[2] = { EAsdaSessionResetReason::MapChanged, EAsdaSessionResetReason::Disconnected };
				for (const EAsdaSessionResetReason Why : kKeep)
				{
					Reset(R)->Broadcast(Why);
					const FString W = AsdaSessionResetReasonName(Why);
					FString E1, E2, E3, E4;
					const bool e1 = SameAs(CellsOf(I, 1), B.Shell, E1), e2 = SameAs(CellsOf(I, 2), B.Spend, E2);
					const bool e3 = SameAs(CellsOf(I, 3), B.Equip, E3), e4 = SameAs(CellsOf(I, 5), B.Store, E4);
					Expect(R, e1 && e2 && e3 && e4,
						FString::Printf(TEXT("E1 %s: вещи Б на месте — сумка (%s), расходники (%s), надетое (%s), склад (%s)"), *W, *E1, *E2, *E3, *E4));
					Expect(R, !Hot(R)->GetSlot(B.HotbarCol).IsEmpty(), FString::Printf(TEXT("E2 %s: панель — слот Б занят"), *W));
					Expect(R, WareTab0Loaded(R), FString::Printf(TEXT("E3 %s: склад — вкладка 0 «загружена»"), *W));
					FString L;
					const int32 N = FAsdaInvRelogSelfTest::Filled(Exch(R), L);
					Expect(R, N == 3, FString::Printf(TEXT("E4 %s: аукцион Б — записей %d из 3 (%s)"), *W, N, *L));
					ExpectSessionKept(R, B, FString::Printf(TEXT("E5 %s"), *W));
				}
				// Проход D — тот же сценарий с ключом 0.
				R.bLegacyPass = true;
				SetRelogKeys(0);
				R.Step = 0; R.StepAt = Now;
				return true;
			}
			// Ключ 0: вещи А остаются В МОДЕЛИ (не «иконка над пустой ячейкой», а сама ячейка занята чужим), поэтому
			//   признак дефекта — лишние ячейки в модели и лишние иконки в окне сверх набора Б.
			Expect(R, !bShell && !bSpend && DrawnShell > B.Shell.Num() && DrawnSpend > 0,
				FString::Printf(TEXT("D-C: ключ 0 — дефект воспроизводится: сумка (%s), расходники (%s), иконок «Снаряжение» %d "
					"при вещах Б %d, «Расходники» %d при 0"), *D1, *D2, DrawnShell, B.Shell.Num(), DrawnSpend));
			I->CloseWindow();
			Done(R);
			return false;
		}
		default:
			Done(R);
			return false;
		}
	}

	static void Run(const TArray<FString>& Args0, UWorld* W)
	{
		const FAsdaSelfTestArgs Args = FAsdaSelfTestArgs::Parse(Args0);
		if (AsdaSelfTestKit::RefuseStart(W, kCmd, Args)) { return; }
		UGameInstance* GI = W ? W->GetGameInstance() : nullptr;
		if (!GI) { AsdaSelfTestKit::Refused(W, kCmd, Args.bQuit, TEXT("нет GameInstance")); return; }
		if (AsdaWorldKind::IsLoginScene(W)) { AsdaSelfTestKit::Refused(W, kCmd, Args.bQuit, TEXT("запускать в мире, не на сцене входа")); return; }
		if (GRun.IsValid()) { AsdaSelfTestKit::Refused(W, kCmd, Args.bQuit, TEXT("самотест уже идёт")); return; }
		if (!IConsoleManager::Get().FindConsoleVariable(kKey) || !IConsoleManager::Get().FindConsoleVariable(kKeyChat)
			|| !IConsoleManager::Get().FindConsoleVariable(kKeyTitle) || !IConsoleManager::Get().FindConsoleVariable(kKeySkill)
			|| !IConsoleManager::Get().FindConsoleVariable(kKeyAnswer)) { AsdaSelfTestKit::Refused(W, kCmd, Args.bQuit, TEXT("нет ключа исправления")); return; }
		GRun = MakeShared<FRun>();
		FRun& R = *GRun;
		R.GI = GI; R.World = W; R.bQuit = Args.bQuit; R.StepAt = FPlatformTime::Seconds();
		if (UAsdaWorldSubsystem* WS = WorldSub(R)) { R.LocalWas = WS->LocalPlayerActor.Get(); }
		if (UAsdaLoadManagerSubsystem* LM = Load(R)) { R.MapWas = LM->CurrentMapId; }
		FTSTicker::GetCoreTicker().AddTicker(FTickerDelegate::CreateStatic(&OnTick), 0.1f);
	}
}

static FAutoConsoleCommandWithWorldAndArgs GAsdaSelfTestInvRelog(
	TEXT("asda.SelfTest.InvRelog"),
	TEXT("Самотест: перезаход другой учёткой без перезапуска — в инвентаре, на складе и в панели только вещи новой учётки, "
	     "иконок-призраков 0; с ключом исправления 0 дефект воспроизводится. Запускать в мире, только офлайн. `quit`."),
	FConsoleCommandWithWorldAndArgsDelegate::CreateLambda([](const TArray<FString>& A, UWorld* W)
	{
		AsdaInvRelogSelfTest::Run(A, W);
	}));

#endif // ASDA_DEV_CONSOLE && !UE_BUILD_SHIPPING
