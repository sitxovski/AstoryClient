// Copyright (c) 2026 @sitxovski (Discord)
// ★★★ САМОТЕСТ «ЗАБОР ИЗ ЭЛИТНОГО ЯЩИКА — ПРИЗРАКИ В СУМКЕ» (05.11.2026, баг игрока): `asda.SelfTest.PostWithdraw [quit]`. [ПОРТ]
//   Жалоба: «после забора из премиум-ящика в сумке остаются невидимые предметы-призраки, ячейки заняты, дальше не переносится».
//   Что проверяет — ПУТЬ ИГРОКА целиком, без сети (-AsdaNoNet), с настоящими пакетами:
//     • окно ящика открывается штатно (`OpenAndSync`) → настоящий 11004 перехватывается → эмулятор сервера отвечает 11005
//       через штатный приёмник (`DebugFeedPacket` → `HandlePacket` → делегат → Tick подсистемы);
//     • «Положить в рюкзак» (`TakeSelected`) → настоящий 11006 перехватывается → эмулятор повторяет сервер
//       (`Recv_PremiumItemToInven_Req@0x4F8340` + `sub_4F89D0@0x4F89D0`, GameServer.c:95835/96012): строку ящика ищет по
//       (Order, Code, Amount == Count) как `UPCashitemToinven{Gear|Functional}`, ячейки — первая свободная (код ≤ 0) своего
//       контейнера (`sub_4D6740@0x4D6740`), контейнер — по правилу `sub_5340B0@0x5340B0` (item_list: класс +36 == 3 или тип
//       0…4/8/9/12…15 → 1, прочее → 2; самоцветы → 2; спецтаблица → 1); ответ 11007 (409 б: статус, CharIndex, 7 × 57 б
//       записей, вес @404, BuyType @408) — через штатный приёмник;
//     • после каждого шага: модель сумки клиента == «память сервера» эмулятора во ВСЕХ 60 ячейках контейнеров 1 и 2 (лишнее =
//       призрак, недостающее = потеря), иконка ячейки окна инвентаря нарисована ⇔ ячейка занята (обе вкладки), строки ящика
//       = оставшиеся заказы.
//   Случаи: W1 одна штука; W2 заказ из 3 разных; W3 9 заказов (окно показывает 7) — забор всех по одному; W4 сумка почти
//   полна (свободны 30, 58, 59): заказ из 4 — клиент не шлёт 11006, заказ из 3 — ложится в 30/58/59; W5 отказы 3/0/2 —
//   сумка не меняется, следующий забор проходит; W6 ответ кладёт НЕ в ту ячейку, что выбрал запрос (как P100 на 2604 для
//   нескладываемых при резерве коробки) — клиент обязан класть по 11007; W7 расходник (контейнер 2); W8 строка ящика с
//   Amount 10 > 7 (живая строка Player17) — отказ 0, призрака нет; I сверка иконок ВСЕХ кодов, замеченных в ящике
//   (Joy1.dbo.ItemPostbox 05.11 + журналы MoveToInven 18.10–05.11 трёх GS, 539 кодов): у каждого иконка рисуется.
//   Состояние (сумка, расходники, «свой персонаж», ключи, перехват, окна) возвращается. Итог: «ПОЧТА-ЗАБОР ИТОГ: ok=N fail=M».
#include "CoreMinimal.h"
#include "AsdaDevConsole.h"

#if ASDA_DEV_CONSOLE && !UE_BUILD_SHIPPING

#include "AsdaBytes.h"
#include "AsdaCVarSave.h"
#include "AsdaNetSubsystem.h"
#include "Test/AsdaSelfTestKit.h"
#include "Items/AsdaItemTableSubsystem.h"
#include "UI/AsdaInventorySubsystem.h"
#include "UI/AsdaItemRecordWriter.h"
#include "UI/AsdaPremiumGachaSubsystem.h"
#include "UI/AsdaPremiumPostSubsystem.h"
#include "UI/AsdaUiHelpers.h"
#include "Components/Image.h"
#include "Engine/Engine.h"
#include "Engine/GameInstance.h"
#include "Engine/World.h"
#include "HAL/IConsoleManager.h"
#include "Text/AsdaLang.h"
#include "Misc/Paths.h"
#include "UnrealClient.h"
#include "TimerManager.h"

DEFINE_LOG_CATEGORY_STATIC(LogAsdaPostWithdrawTest, Log, All);

namespace AsdaPostWithdrawTest
{
	static const TCHAR* kCmd = TEXT("asda.SelfTest.PostWithdraw");   // имя команды
	static const TCHAR* kTag = TEXT("ПОЧТА-ЗАБОР");                   // префикс строк лога и итога
	static constexpr int32 kBag = 60;                                 // ёмкость контейнеров 1/2 (инвентарь ×2)
	static constexpr int32 kCharId = 0x00ABCDEF;                      // charId «своего персонажа» прогона (сверка CharIndex 11005/11007)
	static constexpr int32 kBuyType = 1;                              // BuyType записей (не 3 — ветка окна гачи не нужна)

	// Коды, замеченные в элитном ящике живого dev-сервера (Joy1.dbo.ItemPostbox на 05.11.2026 + все ItemIndex журналов
	//   `Log_MGamePremium/MoveToInven_*.txt` GameServer/Ch2/Indun за 18.10–05.11.2026), 539 кодов по возрастанию.
	static const int32 kSeenCodes[] = {
		30, 31, 32, 33, 36, 37, 40, 43, 49, 50, 55, 56, 57, 196, 197, 208,
		209, 211, 212, 262, 295, 298, 322, 323, 330, 333, 334, 343, 357, 362, 365, 366,
		370, 373, 382, 387, 388, 400, 403, 405, 461, 466, 467, 468, 469, 470, 471, 476,
		489, 491, 492, 493, 494, 495, 496, 497, 498, 499, 500, 514, 558, 7003, 7007, 7119,
		20613, 20807, 20816, 20822, 20830, 20936, 20946, 20952, 21094, 21520, 22817, 23129, 23185, 23190, 23191, 23224,
		23229, 23230, 23231, 23232, 23234, 23532, 23636, 23654, 23762, 27466, 27966, 29168, 30530, 30531, 30532, 31277,
		32414, 32418, 33954, 35676, 35680, 36426, 36710, 38986, 41182, 42077, 42084, 47599, 51035, 52181, 53660, 53669,
		53672, 53673, 53685, 56046, 59047, 61632, 61891, 61892, 61896, 61897, 61898, 61899, 61905, 61961, 61962, 61965,
		61966, 62023, 62024, 62121, 62122, 62123, 62124, 62125, 62126, 62127, 62128, 62129, 62130, 62131, 62132, 62133,
		62134, 62135, 62271, 62272, 62273, 62277, 62278, 62279, 62280, 62284, 62286, 62311, 62340, 62341, 62342, 62343,
		62344, 62345, 62346, 62347, 62348, 62378, 62403, 62404, 62405, 62502, 62503, 62504, 62505, 62506, 62652, 62654,
		62658, 62659, 62660, 62661, 62665, 62666, 62667, 62724, 62725, 62727, 62728, 62729, 62784, 62785, 62786, 62893,
		63033, 63035, 63039, 63040, 63041, 63042, 63103, 63104, 63105, 63108, 63109, 63165, 63167, 63414, 63416, 63420,
		63421, 63422, 63423, 63427, 63483, 63486, 63487, 63488, 63489, 63490, 63491, 63546, 63548, 63646, 63647, 63648,
		63649, 63655, 63796, 63797, 63801, 63802, 63803, 63804, 63808, 63809, 63864, 63866, 63867, 63868, 63870, 63871,
		63872, 63927, 63928, 63929, 64026, 64161, 64395, 64396, 64397, 66616, 67634, 67635, 67636, 67637, 67638, 67639,
		67640, 67641, 67642, 67643, 67644, 67645, 67646, 67647, 67648, 67786, 67791, 67792, 67797, 67798, 67799, 67853,
		67854, 67855, 67856, 67858, 67859, 67860, 67861, 67916, 67917, 67918, 68407, 68408, 68409, 68410, 68411, 68412,
		68413, 68414, 68415, 68416, 68418, 68419, 68420, 68421, 68422, 68423, 68424, 68425, 68426, 68427, 68428, 68429,
		68430, 68431, 68432, 68433, 68434, 68435, 68436, 68437, 68484, 68494, 68495, 68496, 68497, 68498, 68538, 68656,
		68700, 69244, 69253, 69254, 69256, 69257, 69298, 69352, 69361, 69363, 69365, 69641, 69642, 69648, 69659, 69676,
		69752, 69860, 69914, 69977, 70026, 70080, 70134, 70188, 70625, 70679, 70733, 70787, 70850, 70904, 70958, 70967,
		70968, 70969, 70970, 70971, 70973, 70974, 70975, 70976, 70977, 70978, 70979, 70980, 70981, 71120, 71174, 71184,
		71185, 71187, 71188, 71193, 71194, 71195, 71196, 71197, 72634, 72745, 72849, 72903, 72957, 73011, 73065, 73119,
		73173, 73335, 73875, 74091, 74145, 74424, 74478, 74532, 75198, 75503, 75506, 75510, 75511, 75512, 75513, 75514,
		75516, 75521, 75539, 75647, 75662, 75700, 75701, 75702, 75703, 75704, 75816, 75817, 75818, 75820, 75821, 75822,
		75823, 75824, 75978, 75979, 75980, 76338, 76358, 76359, 76360, 76366, 76367, 76368, 76369, 76373, 76374, 76384,
		76387, 76392, 76394, 76399, 76402, 76405, 76408, 76412, 76416, 76417, 76424, 76425, 76427, 76429, 76432, 76435,
		76442, 76443, 76447, 76451, 76453, 76454, 76455, 76456, 76458, 76460, 76461, 76463, 76464, 76467, 76468, 76469,
		76470, 78988, 79204, 79276, 79348, 79374, 79376, 79378, 79429, 79431, 79437, 79440, 79443, 79446, 79449, 79452,
		79455, 79457, 79460, 79461, 79464, 79467, 79470, 79473, 79475, 79476, 79477, 79479, 79480, 79482, 79483, 79485,
		79486, 79488, 79491, 79494, 79497, 79498, 79499, 79500, 79501, 79503, 79504, 79506, 79507, 79552, 79577, 79578,
		79631, 79653, 79673, 79674, 79675, 79692, 79693, 79695, 79696, 79698, 79699, 79700, 79701, 79702, 79703, 79708,
		79715, 79716, 79717, 79718, 79720, 79721, 79722, 79723, 79733, 79755, 79935
	};

	// Строка ящика на «сервере» (одна строка dbo.ItemPostbox).
	struct FDbRow { int32 Order = 0; int32 Code = 0; int32 Amount = 0; int32 BuyType = kBuyType; };

	// Контекст прогона.
	struct FCtx
	{
		int32 Ok = 0, Fail = 0;                         // пройдено / провалено
		TWeakObjectPtr<UWorld> W;                       // мир прогона
		bool bQuit = false;                             // выйти после итога
		TArray<TArray<uint8>> SavedBag, SavedSpend;     // контейнеры 1/2 до прогона (пусто — ячейка была пустой)
		TUniquePtr<FAsdaNetSelfScope> Self;             // «свой персонаж» сети — вернуть в конце
		uint32 CapGen = 0;                              // поколение перехвата отправок
		TArray<TPair<int32, TArray<uint8>>> Out;        // перехваченные отправки (опкод, тело) — очередь эмулятора
		int32 Served = 0;                               // сколько отправок эмулятор уже обработал
		int32 Sent11006 = 0;                            // всего перехвачено 11006
		TArray<FDbRow> Db;                              // «dbo.ItemPostbox» персонажа
		TArray<int32> Mem1, Mem2;                       // «память сервера»: коды ячеек контейнеров 1 и 2 (0 — пусто)
		int32 ForcedStatus = 1;                         // статус следующего ответа 11007 (1 — эмулировать успех)
		int32 DriftSkip = 0;                            // ответ пропускает столько свободных ячеек (сценарий W6)
		int32 RefuseNth = 0;                            // какой по счёту 11006 получит отказ (0 — никакой)
		int32 RefuseStatus = 0;                         // код этого отказа
		int32 Prize = 0;                                // код коробки с окном выбора варианта (сценарий M7)
		int32 Sent6581 = 0;                             // всего перехвачено 6581 (обмен варианта)
		int32 LastVariant = -1;                         // код варианта в последнем 6581
		int32 Chosen = -1;                              // вариант, выбранный в окне 68500 (сценарий M9)
		TArray<int32> Gear;                             // коды экипировки (контейнер 1) с иконкой
		int32 Spend = 0;                                // код расходника (контейнер 2) с иконкой
		TArray<TFunction<void()>> Steps;                // шаги
		int32 Step = 0;                                 // следующий шаг
		int32 IdleTicks = 0;                            // тиков простоя подряд (шаг идёт после 2)
		FTimerHandle H;                                 // таймер
		bool bDone = false;                             // итог подведён

		void Expect(bool b, const FString& What)
		{
			if (b) { ++Ok;   UE_LOG(LogAsdaPostWithdrawTest, Log,   TEXT("%s ok   %s"), kTag, *What); }
			else   { ++Fail; UE_LOG(LogAsdaPostWithdrawTest, Error, TEXT("%s FAIL %s"), kTag, *What); }
		}
		UGameInstance* GI() const { return W.IsValid() ? W->GetGameInstance() : nullptr; }
		template <class T> T* Sub() const { UGameInstance* G = GI(); return G ? G->GetSubsystem<T>() : nullptr; }
		TArray<int32>& Mem(int32 Cont) { return Cont == 2 ? Mem2 : Mem1; }
	};
	using FCtxRef = TSharedRef<FCtx>;

	// Контейнер выдачи по правилу сервера `sub_5340B0@0x5340B0` (GameServer.c; независимо от кода порта).
	static int32 ServerContainer(const UAsdaItemTableSubsystem* IT, int32 Code)
	{
		if (IT->IsSowelCode(Code)) { return 2; }
		if (IT->IsSpecialItemCode(Code)) { return 1; }
		const FAsdaItemRecord* R = IT->GetItem(Code);
		if (!R) { return 1; }
		const int32 T = R->ItemType;
		const bool bGear = R->CategoryFlag == 3 || (T >= 0 && T <= 4) || T == 8 || T == 9 || (T >= 12 && T <= 15);
		return bGear ? 1 : 2;
	}

	// Тело 11005: CharIndex + записи (Order, Code, Count, BuyType) — строки «БД» в её порядке (как UPCashitemList).
	static TArray<uint8> List11005(const TArray<FDbRow>& Db)
	{
		TArray<uint8> B;
		B.SetNumZeroed(4 + 10 * Db.Num());
		AsdaBytes::WrI32(&B[0], kCharId);
		for (int32 i = 0; i < Db.Num(); ++i)
		{
			AsdaBytes::WrI32(&B[4 + 10 * i], Db[i].Order);
			AsdaBytes::WrI32(&B[8 + 10 * i], Db[i].Code);
			B[12 + 10 * i] = (uint8)FMath::Clamp(Db[i].Amount, 0, 255);
			B[13 + 10 * i] = (uint8)Db[i].BuyType;
		}
		return B;
	}

	// Иконка ячейки окна инвентаря нарисована (кисть с ресурсом и рисуется).
	static bool CellIconDrawn(const UAsdaInventorySubsystem* Inv, int32 Cell)
	{
		const UImage* Img = Cast<UImage>(Inv->DebugGetGridIcon(Cell));
		if (!Img) { return false; }
		const FSlateBrush& B = Img->GetBrush();
		return B.GetResourceObject() != nullptr && B.DrawAs != ESlateBrushDrawType::NoDrawType && Img->IsVisible();
	}

	// Сверка модели и картинки сумки с «памятью сервера» + строк ящика с «БД».
	static void CheckAll(const FCtxRef& C, const FString& Tag)
	{
		UAsdaInventorySubsystem* Inv = C->Sub<UAsdaInventorySubsystem>();
		UAsdaPremiumPostSubsystem* P = C->Sub<UAsdaPremiumPostSubsystem>();
		if (!Inv || !P) { C->Expect(false, Tag + TEXT(": подсистемы есть")); return; }
		for (int32 Cont = 1; Cont <= 2; ++Cont)
		{
			int32 Ghost = 0, Lost = 0, Wrong = 0, VisBad = 0;
			FString Where, VisWhere;
			Inv->SetTab(Cont == 1 ? 1 : 0);
			for (int32 i = 0; i < kBag; ++i)
			{
				const int32 Have = FMath::Max(0, Inv->GetCellItemCode(Cont, i));
				const int32 Want = C->Mem(Cont)[i];
				if (Have > 0 && Want <= 0) { ++Ghost; Where += FString::Printf(TEXT(" призрак[%d]=%d"), i, Have); }
				else if (Have <= 0 && Want > 0) { ++Lost; Where += FString::Printf(TEXT(" нет[%d]=%d"), i, Want); }
				else if (Have != Want) { ++Wrong; Where += FString::Printf(TEXT(" не тот[%d]=%d/%d"), i, Have, Want); }
				if (CellIconDrawn(Inv, i) != (Have > 0)) { ++VisBad; VisWhere += FString::Printf(TEXT(" [%d]код=%d нарисована=%d"), i, Have, CellIconDrawn(Inv, i) ? 1 : 0); }
			}
			C->Expect(Ghost == 0 && Lost == 0 && Wrong == 0,
				FString::Printf(TEXT("%s: контейнер %d — модель клиента == память сервера (призраков %d, потерь %d, не тот код %d)%s"),
					*Tag, Cont, Ghost, Lost, Wrong, *Where.Left(300)));
			C->Expect(VisBad == 0, FString::Printf(TEXT("%s: контейнер %d — иконка нарисована ⇔ ячейка занята (расхождений %d)%s"), *Tag, Cont, VisBad, *VisWhere.Left(400)));
		}
		Inv->SetTab(1);
		// Строки ящика: столько, сколько разных заказов (не больше 7), и ни одной уже забранной.
		TArray<int32> Orders;
		for (const FDbRow& R : C->Db) { Orders.AddUnique(R.Order); }
		const int32 WantRows = FMath::Min(Orders.Num(), (int32)UAsdaPremiumPostSubsystem::kRows);
		int32 Stale = 0;
		for (int32 r = 0; r < P->GetFilledRowCount(); ++r) { if (!Orders.Contains(P->GetRow(r).Order)) { ++Stale; } }
		C->Expect(P->GetFilledRowCount() == WantRows && Stale == 0,
			FString::Printf(TEXT("%s: строк ящика %d (ждём %d), строк уже забранных заказов %d"), *Tag, P->GetFilledRowCount(), WantRows, Stale));
	}

	// Эмулятор сервера: одна перехваченная отправка за вызов. true — что-то обработано.
	static bool ServeOne(const FCtxRef& C)
	{
		if (C->Served >= C->Out.Num()) { return false; }
		const TPair<int32, TArray<uint8>> Pkt = C->Out[C->Served++];
		UAsdaNetSubsystem* Net = C->Sub<UAsdaNetSubsystem>();
		UAsdaItemTableSubsystem* IT = C->Sub<UAsdaItemTableSubsystem>();
		if (!Net || !IT) { return true; }
		if (Pkt.Key == 11004)
		{
			Net->DebugFeedPacket(11005, List11005(C->Db));   // `RecvDB_PremiumItemList_Res@0x4F8050`: строки ящика как есть
			return true;
		}
		// 6581 (`Recv_UserPrimiumGaChaReward@0x4F72B0`): запись по (контейнер, слот) с тем же кодом — обмен на вариант, ответ 6582.
		if (Pkt.Key == 6581 && Pkt.Value.Num() >= 11)
		{
			const uint8* V = Pkt.Value.GetData();
			const int32 Code = AsdaBytes::RdI32(V + 0);
			const int32 Slot = (int32)(int16)(V[4] | (V[5] << 8));
			const int32 Cont = (int32)V[6];
			const int32 Var  = AsdaBytes::RdI32(V + 7);
			C->LastVariant = Var;
			const bool bFound = (Cont == 1 || Cont == 2) && Slot >= 0 && Slot < kBag && C->Mem(Cont)[Slot] == Code;
			TArray<uint8> B6;
			B6.SetNumZeroed(61);
			B6[0] = bFound ? 1 : 10;
			*reinterpret_cast<int16*>(&B6[2]) = (int16)-1;   // успех: очищаемой ячейки нет (контейнер 0), вариант ложится в ту же
			if (bFound)
			{
				const TArray<uint8> Rec = FAsdaItemRecordWriter().Code(Var).Container(Cont).Slot(Slot).Second(-1).Amount(Cont == 2 ? 1 : 0).ToArray();
				FMemory::Memcpy(&B6[4], Rec.GetData(), 57);
				C->Mem(Cont)[Slot] = Var;
			}
			UE_LOG(LogAsdaPostWithdrawTest, Log, TEXT("%s эмулятор: 6581 код %d ячейка %d/%d вариант %d → 6582 статус %d"), kTag, Code, Cont, Slot, Var, (int32)B6[0]);
			Net->DebugFeedPacket(6582, B6);
			return true;
		}
		if (Pkt.Key != 11006 || Pkt.Value.Num() < 10) { return true; }
		const uint8* R = Pkt.Value.GetData();
		const int32 Order = AsdaBytes::RdI32(R + 0), Code = AsdaBytes::RdI32(R + 4), Count = R[8], BuyType = R[9];
		TArray<uint8> B;
		B.SetNumZeroed(409);
		AsdaBytes::WrI32(&B[1], kCharId);
		AsdaBytes::WrI32(&B[404], 100);
		B[408] = (uint8)BuyType;
		const int32 Cont = ServerContainer(IT, Code);
		int32 Status = C->ForcedStatus;
		if (C->RefuseNth > 0 && --C->RefuseNth == 0) { Status = C->RefuseStatus; }
		// Запрос: ячейки под Count штук (иначе 3 — «нет места», как сток).
		TArray<int32> Req;
		for (int32 i = 0; i < kBag && Req.Num() < Count; ++i) { if (C->Mem(Cont)[i] <= 0) { Req.Add(i); } }
		if (Status == 1 && Req.Num() < Count) { Status = 3; }
		// Процедура: строка (Userindex, Charidx, Orderidx, Code[, Amount = Count]).
		const int32 DbIdx = C->Db.IndexOfByPredicate([&](const FDbRow& D) { return D.Order == Order && D.Code == Code && D.Amount == Count; });
		if (Status == 1 && DbIdx == INDEX_NONE) { Status = 0; }
		B[0] = (uint8)Status;
		if (Status == 1)
		{
			// Ответ 2604 (`sub_4F89D0`): ячейки пересчитываются; W6 — пропуск DriftSkip свободных ячеек (ячейка ответа ≠ запроса).
			int32 Skip = C->DriftSkip;
			int32 j = 0;
			for (int32 i = 0; i < kBag && j < Count; ++i)
			{
				if (C->Mem(Cont)[i] > 0) { continue; }
				if (Skip > 0) { --Skip; continue; }
				const TArray<uint8> Rec = FAsdaItemRecordWriter().Code(Code).Container(Cont).Slot(i).Second(-1).Amount(Cont == 2 ? 1 : 0).ToArray();
				FMemory::Memcpy(&B[5 + 57 * j], Rec.GetData(), 57);
				C->Mem(Cont)[i] = Code;
				++j;
			}
			C->Db.RemoveAt(DbIdx);
		}
		UE_LOG(LogAsdaPostWithdrawTest, Log, TEXT("%s эмулятор: 11006 Order %d код %d ×%d → 11007 статус %d (контейнер %d)"), kTag, Order, Code, Count, Status, Cont);
		Net->DebugFeedPacket(11007, B);
		return true;
	}

	// Вернуть сумку/расходники, окна, сеть, ключи; снять перехват; итог.
	static void Finish(const FCtxRef& C)
	{
		C->bDone = true;
		if (UAsdaPremiumPostSubsystem* P = C->Sub<UAsdaPremiumPostSubsystem>())
		{
			P->CloseWindow();
			P->SimulateList(List11005({}));
		}
		if (UAsdaInventorySubsystem* Inv = C->Sub<UAsdaInventorySubsystem>())
		{
			auto Restore = [Inv](const TArray<TArray<uint8>>& Saved, int32 Cont)
			{
				for (int32 I = 0; I < kBag; ++I)
				{
					if (Saved.IsValidIndex(I) && Saved[I].Num() >= FAsdaItemInstance::kRecordSize)
					{
						Inv->ApplyExternalRecord(Saved[I], UAsdaInventorySubsystem::EExternalRecordMode::AsIs);
					}
					else { Inv->ClearCell(Cont, I); }
				}
			};
			Restore(C->SavedBag, 1);
			Restore(C->SavedSpend, 2);
			Inv->CloseWindow();
		}
		if (UAsdaNetSubsystem* Net = C->Sub<UAsdaNetSubsystem>()) { if (C->CapGen) { AsdaSelfTestKit::DisarmCapture(Net, C->CapGen); C->CapGen = 0; } }
		if (C->Self) { C->Self->Restore(); C->Self.Reset(); }
		if (UWorld* Wd = C->W.Get())
		{
			FTimerHandle H = C->H;
			TWeakObjectPtr<UWorld> W2 = C->W;
			Wd->GetTimerManager().SetTimerForNextTick(FTimerDelegate::CreateLambda([W2, H]() mutable { if (UWorld* W3 = W2.Get()) { W3->GetTimerManager().ClearTimer(H); } }));
		}
		TArray<TFunction<void()>> Done = MoveTemp(C->Steps);   // шаги держат контекст — разорвать цикл
		Done.Reset();
		AsdaSelfTestKit::Finish(C->W.Get(), kCmd, C->bQuit, LogAsdaPostWithdrawTest, kTag, C->Ok, C->Fail);
	}

	// Положить предмет «руками» в обе стороны (сумка клиента и память сервера) — подготовка W4.
	static void PutBoth(const FCtxRef& C, int32 Cont, int32 Cell, int32 Code)
	{
		if (UAsdaInventorySubsystem* Inv = C->Sub<UAsdaInventorySubsystem>()) { Inv->DebugPutItem(Cont, Cell, Code, 1); }
		C->Mem(Cont)[Cell] = Code;
	}
	// Очистить ячейку в обе стороны.
	static void ClearBoth(const FCtxRef& C, int32 Cont, int32 Cell)
	{
		// Очистка — серверной записью «amount −1» (путь 6166/6582/11007), а не голым `ClearCell`: тот не перерисовывает окно.
		if (UAsdaInventorySubsystem* Inv = C->Sub<UAsdaInventorySubsystem>()) { Inv->ApplyExternalRecord(FAsdaItemRecordWriter::ClearCell(Cont, Cell)); }
		C->Mem(Cont)[Cell] = 0;
	}
	// Строка окна ящика с заказом Order (−1 — нет).
	static int32 RowOf(const UAsdaPremiumPostSubsystem* P, int32 Order)
	{
		for (int32 r = 0; r < P->GetFilledRowCount(); ++r) { if (P->GetRow(r).Order == Order) { return r; } }
		return -1;
	}
	// Переоткрыть ящик штатно (11004 → 11005 эмулятора).
	static void Reopen(const FCtxRef& C)
	{
		if (UAsdaPremiumPostSubsystem* P = C->Sub<UAsdaPremiumPostSubsystem>()) { P->CloseWindow(); P->OpenAndSync(true); }
	}
	// Выбрать строку заказа и нажать «Положить в рюкзак».
	static void Take(const FCtxRef& C, int32 Order)
	{
		UAsdaPremiumPostSubsystem* P = C->Sub<UAsdaPremiumPostSubsystem>();
		if (!P) { return; }
		const int32 Row = RowOf(P, Order);
		C->Expect(Row >= 0, FString::Printf(TEXT("заказ %d есть в окне ящика (строка %d)"), Order, Row));
		if (Row < 0) { return; }
		P->SelectRow(Row);
		P->TakeSelected();
	}

	// Сколько строк окна отмечено.
	static int32 CheckedRows(const UAsdaPremiumPostSubsystem* P)
	{
		int32 N = 0;
		for (int32 r = 0; r < UAsdaPremiumPostSubsystem::kRows; ++r) { if (P->IsRowChecked(r)) { ++N; } }
		return N;
	}
	// Отметить галочками все заполненные строки: нажимаем только те, что ещё не отмечены.
	static void CheckAllRows(const FCtxRef& C)
	{
		UAsdaPremiumPostSubsystem* P = C->Sub<UAsdaPremiumPostSubsystem>();
		if (!P) { return; }
		for (int32 r = 0; r < P->GetFilledRowCount(); ++r) { if (!P->IsRowChecked(r)) { P->SimulateCheckboxClick(r); } }
	}
	// Нажать «Положить в рюкзак» с обнулённым счётчиком 11006.
	static void Press(const FCtxRef& C)
	{
		C->Sent11006 = 0;
		if (UAsdaPremiumPostSubsystem* P = C->Sub<UAsdaPremiumPostSubsystem>()) { P->TakeSelected(); }
	}
	// Опустошить оба контейнера в обе стороны.
	static void ClearBags(const FCtxRef& C)
	{
		for (int32 i = 0; i < kBag; ++i)
		{
			if (C->Mem1[i] > 0) { ClearBoth(C, 1, i); }
			if (C->Mem2[i] > 0) { ClearBoth(C, 2, i); }
		}
	}
	// Кадр окна в файл: Saved/PostMultiFrames/<язык>_<метка>.png.
	static void Shot(const TCHAR* Tag)
	{
		// Кадры — вне Saved: пакет самотестов стирает свою папку Saved_<суффикс> после прогона.
		const FString File = FPaths::ConvertRelativePathToFull(FPaths::ProjectDir() / TEXT("../Reports/batch14/postbox/frames")
			/ FString::Printf(TEXT("%s_%s.png"), FAsdaLang::Code(FAsdaLang::Current()), Tag));
		UE_LOG(LogAsdaPostWithdrawTest, Log, TEXT("%s КАДР → %s"), kTag, *File);
		FScreenshotRequest::RequestScreenshot(File, /*bShowUI*/ true, /*bAddFilenameSuffix*/ false);
	}
	// Коробка, у которой мост 11007 открывает окно 68500: проверяем настоящим вызовом моста.
	static int32 PickPrize(const FCtxRef& C, const UAsdaItemTableSubsystem* IT)
	{
		UAsdaPremiumGachaSubsystem* Gacha = C->Sub<UAsdaPremiumGachaSubsystem>();
		UImage* Probe = NewObject<UImage>(GetTransientPackage());
		if (!Gacha || !Probe) { return 0; }
		TMap<int32, int32> Group;   // LinkWordCode → сколько коробок в группе
		for (const FAsdaItemRecord& R : IT->GetAllItems())
		{
			if ((R.ItemSubType == 200 || R.ItemSubType == 201) && (uint16)R.LinkWordCode > 0x63) { Group.FindOrAdd(R.LinkWordCode) += 1; }
		}
		int32 Tries = 0;
		for (const FAsdaItemRecord& R : IT->GetAllItems())
		{
			if (R.ItemSubType != 200 && R.ItemSubType != 201) { continue; }
			if ((uint16)R.LinkWordCode <= 0x63 || Group.FindRef(R.LinkWordCode) < 2) { continue; }
			if (!AsdaUi::ApplyItemIcon(C->GI(), Probe, R.ItemCode, FVector2D(32.f, 32.f), true, true)) { continue; }
			if (++Tries > 40) { break; }
			const TArray<uint8> Rec = FAsdaItemRecordWriter().Code(R.ItemCode).Container(ServerContainer(IT, R.ItemCode)).Slot(0).Second(-1).Amount(0).ToArray();
			if (Gacha->OnPrizeTakenFromPost(Rec)) { Gacha->CloseVariantWindow(); return R.ItemCode; }
		}
		return 0;
	}

	// Подбор кодов: 20 видов экипировки и один расходник — с иконкой, контейнер по правилу сервера.
	static bool PickCodes(const FCtxRef& C, const UAsdaItemTableSubsystem* IT)
	{
		UImage* Probe = NewObject<UImage>(GetTransientPackage());   // пробная картинка: код с рисуемой иконкой (атлас «0»/«−1» у 25 кодов таблицы — мимо)
		for (const FAsdaItemRecord& R : IT->GetAllItems())
		{
			if (R.ItemCode < 20551 || !Probe || !AsdaUi::ApplyItemIcon(C->GI(), Probe, R.ItemCode, FVector2D(32.f, 32.f), true, true)) { continue; }
			const int32 Cont = ServerContainer(IT, R.ItemCode);
			if (Cont == 1 && R.ItemType >= 0 && R.ItemType <= 4 && C->Gear.Num() < 20) { C->Gear.Add(R.ItemCode); }
			else if (Cont == 2 && R.ItemType == 11 && !C->Spend) { C->Spend = R.ItemCode; }
			if (C->Gear.Num() >= 20 && C->Spend) { break; }
		}
		C->Prize = PickPrize(C, IT);
		return C->Gear.Num() >= 20 && C->Spend > 0;
	}

	// Сценарий.
	static void BuildSteps(const FCtxRef& C)
	{
		TWeakPtr<FCtx> WC(C);
		auto Add = [C](TFunction<void(FCtx&)> F) { TWeakPtr<FCtx> W2(C); C->Steps.Add([W2, F]() { if (TSharedPtr<FCtx> X = W2.Pin()) { F(*X); } }); };
		auto Ref = [WC]() { return WC.Pin().ToSharedRef(); };
		const TArray<int32> G = C->Gear;

		// ── W1: одна штука ──
		Add([G](FCtx& X) { X.Db = { { 10, G[0], 1 } }; });
		Add([Ref](FCtx& X) { Reopen(Ref()); });
		Add([Ref](FCtx& X) { CheckAll(Ref(), TEXT("W0 ящик открыт, 1 заказ")); Take(Ref(), 10); });
		Add([Ref](FCtx& X) { CheckAll(Ref(), TEXT("W1 забор одной штуки")); });
		// ── W2: заказ из трёх разных ──
		Add([G, Ref](FCtx& X) { X.Db = { { 20, G[1], 1 }, { 20, G[2], 1 }, { 20, G[3], 1 } }; Reopen(Ref()); });
		Add([Ref](FCtx& X) { Take(Ref(), 20); });
		Add([Ref](FCtx& X) { CheckAll(Ref(), TEXT("W2 заказ из 3 разных (3 × 11006 подряд)")); });
		// ── W3: 9 заказов, окно показывает 7; забор всех по одному ──
		Add([G, Ref](FCtx& X) { X.Db.Reset(); for (int32 k = 0; k < 9; ++k) { X.Db.Add({ 30 + k, G[4 + k], 1 }); } Reopen(Ref()); });
		Add([Ref](FCtx& X) { CheckAll(Ref(), TEXT("W3 9 заказов — в окне 7")); });
		for (int32 k = 0; k < 9; ++k)
		{
			Add([Ref, k](FCtx& X) { Take(Ref(), 30 + k); });
			Add([Ref, k](FCtx& X) { CheckAll(Ref(), FString::Printf(TEXT("W3 забран заказ %d из 9"), k + 1)); });
		}
		// ── W4: сумка почти полна — свободны 30, 58, 59 ──
		Add([G, Ref](FCtx& X)
		{
			for (int32 i = 0; i < kBag; ++i) { if (X.Mem1[i] <= 0 && i != 30 && i != 58 && i != 59) { PutBoth(Ref(), 1, i, G[19]); } }
			X.Db = { { 40, G[13], 1 }, { 40, G[14], 1 }, { 40, G[15], 1 }, { 40, G[16], 1 }, { 41, G[17], 1 }, { 41, G[18], 1 }, { 41, G[17], 1 } };
			Reopen(Ref());
		});
		Add([Ref](FCtx& X) { X.Sent11006 = 0; Take(Ref(), 40); });
		Add([Ref](FCtx& X)
		{
			X.Expect(X.Sent11006 == 0, FString::Printf(TEXT("W4 заказ из 4 при 3 свободных: 11006 не ушёл (%d)"), X.Sent11006));
			CheckAll(Ref(), TEXT("W4 отказ клиента «нет места» — сумка не тронута"));
			Take(Ref(), 41);
		});
		Add([Ref](FCtx& X)
		{
			CheckAll(Ref(), TEXT("W4 заказ из 3 лёг в 30/58/59"));
			X.Expect(X.Mem1[30] > 0 && X.Mem1[58] > 0 && X.Mem1[59] > 0, TEXT("W4 ячейки 30, 58, 59 заняты (сумка 60)"));
		});
		// ── W5: отказы 3 / 0 / 2, затем успех ──
		Add([G, Ref](FCtx& X)
		{
			for (int32 i = 50; i < 58; ++i) { ClearBoth(Ref(), 1, i); }
			X.Db = { { 50, G[5], 1 } };
			Reopen(Ref());
		});
		for (const int32 St : { 3, 0, 2 })
		{
			Add([Ref, St](FCtx& X) { X.ForcedStatus = St; Take(Ref(), 50); });
			Add([Ref, St](FCtx& X)
			{
				const UAsdaPremiumPostSubsystem* P = X.Sub<UAsdaPremiumPostSubsystem>();
				X.Expect(P && !P->DebugTakeRunning() && !P->DebugInFlight(), FString::Printf(TEXT("W5 отказ %d: забор окончен, защёлка снята"), St));
				CheckAll(Ref(), FString::Printf(TEXT("W5 отказ %d — сумка не тронута, строка осталась"), St));
			});
		}
		Add([Ref](FCtx& X) { X.ForcedStatus = 1; Take(Ref(), 50); });
		Add([Ref](FCtx& X) { CheckAll(Ref(), TEXT("W5 после отказов забор проходит (ячейка 50)")); });
		// ── W6: ответ кладёт не в ту ячейку, что выбрал запрос ──
		Add([G, Ref](FCtx& X) { X.Db = { { 60, G[6], 1 }, { 60, G[7], 1 } }; X.DriftSkip = 2; Reopen(Ref()); });
		Add([Ref](FCtx& X) { Take(Ref(), 60); });
		Add([Ref](FCtx& X) { X.DriftSkip = 0; CheckAll(Ref(), TEXT("W6 ячейки ответа ≠ запроса — клиент кладёт по 11007")); });
		// ── W7: расходник (контейнер 2) ──
		Add([Ref](FCtx& X) { X.Db = { { 70, X.Spend, 2 } }; Reopen(Ref()); });
		Add([Ref](FCtx& X) { Take(Ref(), 70); });
		Add([Ref](FCtx& X) { CheckAll(Ref(), TEXT("W7 расходник ×2 в контейнер 2")); });
		// ── W8: строка ящика с Amount 10 > 7 ──
		Add([G, Ref](FCtx& X) { for (int32 i = 40; i < 50; ++i) { ClearBoth(Ref(), 1, i); } X.Db = { { 80, G[8], 10 } }; Reopen(Ref()); });
		Add([Ref](FCtx& X) { X.Sent11006 = 0; Take(Ref(), 80); });
		Add([Ref](FCtx& X)
		{
			X.Expect(X.Sent11006 == 1, FString::Printf(TEXT("W8 Amount 10 > 7: ушёл один 11006 (%d) с Count 7"), X.Sent11006));
			CheckAll(Ref(), TEXT("W8 Amount 10 > 7: процедура отказывает 0 — призрака нет, строка осталась"));
		});
		// ── M1: 9 заказов, галочками отмечены 4 (пачка 14) ──
		Add([G, Ref](FCtx& X) { ClearBags(Ref()); X.Db.Reset(); for (int32 k = 0; k < 9; ++k) { X.Db.Add({ 110 + k, G[k], 1 }); } Reopen(Ref()); });
		Add([Ref](FCtx& X)
		{
			UAsdaPremiumPostSubsystem* P = X.Sub<UAsdaPremiumPostSubsystem>();
			X.Expect(CheckedRows(P) == 1 && P->IsRowChecked(6), FString::Printf(TEXT("M1 при открытии отмечена нижняя строка, как в эталоне (отмечено %d)"), CheckedRows(P)));
			P->SimulateCheckboxClick(0); P->SimulateCheckboxClick(1); P->SimulateCheckboxClick(2);
			X.Expect(CheckedRows(P) == 4 && P->IsRowChecked(6), FString::Printf(TEXT("M1 три нажатия галочек добавили строки (отмечено %d)"), CheckedRows(P)));
			P->SimulateCheckboxClick(2);
			X.Expect(CheckedRows(P) == 3 && !P->IsRowChecked(2), TEXT("M1 повторное нажатие снимает отметку только этой строки"));
			P->SimulateIconClick(2 * UAsdaPremiumPostSubsystem::kCols);
			X.Expect(CheckedRows(P) == 4 && P->IsRowChecked(2), TEXT("M1 щелчок по иконке строки ставит отметку"));
			Shot(TEXT("M1_four_checked"));
		});
		Add([Ref](FCtx& X) { Press(Ref()); });
		Add([Ref](FCtx& X)
		{
			UAsdaPremiumPostSubsystem* P = X.Sub<UAsdaPremiumPostSubsystem>();
			X.Expect(X.Sent11006 == 4 && X.Db.Num() == 5, FString::Printf(TEXT("M1 одно нажатие: 11006 ×%d (4), в базе осталось %d (5)"), X.Sent11006, X.Db.Num()));
			CheckAll(Ref(), TEXT("M1 четыре заказа забраны"));
			X.Expect(CheckedRows(P) == 1, FString::Printf(TEXT("M1 после забора отмечена одна строка (%d)"), CheckedRows(P)));
			Shot(TEXT("M1_after_take"));
			// ── M2: ничего не отмечено ──
			for (int32 r = 0; r < UAsdaPremiumPostSubsystem::kRows; ++r) { if (P->IsRowChecked(r)) { P->SimulateCheckboxClick(r); } }
			X.Expect(CheckedRows(P) == 0, TEXT("M2 все отметки сняты"));
			Press(Ref());
		});
		Add([Ref](FCtx& X)
		{
			X.Expect(X.Sent11006 == 0, FString::Printf(TEXT("M2 без отметок 11006 не ушёл (%d)"), X.Sent11006));
			CheckAll(Ref(), TEXT("M2 ящик и сумка не тронуты"));
		});
		// ── M3: семь заказов разом, среди них многозаписные и расходник; ещё два ждут за пределом семи ──
		Add([G, Ref](FCtx& X)
		{
			X.Db = { { 120, G[0], 1 }, { 120, G[1], 1 }, { 120, G[2], 1 }, { 121, G[3], 1 }, { 121, G[4], 1 }, { 122, X.Spend, 2 },
				{ 123, G[5], 1 }, { 124, G[6], 1 }, { 125, G[7], 1 }, { 126, G[8], 1 }, { 127, G[9], 1 }, { 128, G[10], 1 } };
			Reopen(Ref());
		});
		Add([Ref](FCtx& X) { CheckAllRows(Ref()); X.Expect(CheckedRows(X.Sub<UAsdaPremiumPostSubsystem>()) == 7, TEXT("M3 отмечены все 7 строк")); Shot(TEXT("M3_seven_checked")); Press(Ref()); });
		Add([Ref](FCtx& X)
		{
			X.Expect(X.Sent11006 == 10, FString::Printf(TEXT("M3 7 заказов = 11006 ×%d (10)"), X.Sent11006));
			CheckAll(Ref(), TEXT("M3 семь заказов забраны, в окне следующие два"));
			const UAsdaPremiumPostSubsystem* P = X.Sub<UAsdaPremiumPostSubsystem>();
			X.Expect(RowOf(P, 127) == 0 && RowOf(P, 128) == 1, TEXT("M3 заказы за пределом семи появились в окне"));
			Shot(TEXT("M3_after_take"));
		});
		// ── M4: места на все отмеченные нет ──
		Add([G, Ref](FCtx& X)
		{
			ClearBags(Ref());
			for (int32 i = 0; i < kBag; ++i) { if (i != 30 && i != 58 && i != 59) { PutBoth(Ref(), 1, i, G[19]); } }
			X.Db = { { 130, G[0], 1 }, { 131, G[1], 1 }, { 131, G[2], 1 }, { 132, G[3], 1 } };
			Reopen(Ref());
		});
		Add([Ref](FCtx& X) { CheckAllRows(Ref()); Press(Ref()); });
		Add([Ref](FCtx& X)
		{
			UAsdaPremiumPostSubsystem* P = X.Sub<UAsdaPremiumPostSubsystem>();
			X.Expect(X.Sent11006 == 0 && CheckedRows(P) == 3, FString::Printf(TEXT("M4 4 единицы при 3 ячейках: 11006 ×%d (0), отметки целы (%d)"), X.Sent11006, CheckedRows(P)));
			CheckAll(Ref(), TEXT("M4 отказ клиента — ничего не забрано"));
			Shot(TEXT("M4_no_room"));
			P->SimulateCheckboxClick(RowOf(P, 130));
			Press(Ref());
		});
		Add([Ref](FCtx& X)
		{
			X.Expect(X.Sent11006 == 3 && X.Mem1[30] > 0 && X.Mem1[58] > 0 && X.Mem1[59] > 0, FString::Printf(TEXT("M4 после снятия галочки 11006 ×%d (3), ячейки 30/58/59 заняты"), X.Sent11006));
			CheckAll(Ref(), TEXT("M4 заказы легли в последние ячейки"));
		});
		// ── M4b: контейнеры считаются отдельно ──
		Add([Ref](FCtx& X) { X.Db = { { 130, X.Gear[0], 1 }, { 133, X.Spend, 2 } }; Reopen(Ref()); });
		Add([Ref](FCtx& X) { CheckAllRows(Ref()); Press(Ref()); });
		Add([Ref](FCtx& X)
		{
			UAsdaPremiumPostSubsystem* P = X.Sub<UAsdaPremiumPostSubsystem>();
			X.Expect(X.Sent11006 == 0, TEXT("M4b сумка полна: снаряжение и расходник вместе не уходят"));
			P->SimulateCheckboxClick(RowOf(P, 130));
			Press(Ref());
		});
		Add([Ref](FCtx& X)
		{
			X.Expect(X.Sent11006 == 1, FString::Printf(TEXT("M4b один расходник уходит: у него свой контейнер (%d)"), X.Sent11006));
			CheckAll(Ref(), TEXT("M4b расходник взят, снаряжение осталось"));
		});
		// ── M5: отказ сервера посреди очереди ──
		Add([G, Ref](FCtx& X)
		{
			ClearBags(Ref());
			X.Db = { { 140, G[0], 1 }, { 141, G[1], 1 }, { 142, G[2], 1 }, { 143, G[3], 1 } };
			Reopen(Ref());
		});
		Add([Ref](FCtx& X) { CheckAllRows(Ref()); X.RefuseNth = 2; X.RefuseStatus = 0; Press(Ref()); });
		Add([Ref](FCtx& X)
		{
			const UAsdaPremiumPostSubsystem* P = X.Sub<UAsdaPremiumPostSubsystem>();
			X.Expect(X.Sent11006 == 2 && !P->DebugTakeRunning() && !P->DebugInFlight(), FString::Printf(TEXT("M5 отказ на втором заказе останавливает очередь (11006 ×%d)"), X.Sent11006));
			CheckAll(Ref(), TEXT("M5 первый заказ взят, список обновлён"));
			X.Expect(RowOf(P, 140) < 0 && CheckedRows(P) == 3, FString::Printf(TEXT("M5 невзятые остались отмеченными (%d)"), CheckedRows(P)));
			Press(Ref());
		});
		Add([Ref](FCtx& X)
		{
			X.Expect(X.Sent11006 == 3 && X.Db.Num() == 0, FString::Printf(TEXT("M5 повторное нажатие взяло остаток (11006 ×%d, в базе %d)"), X.Sent11006, X.Db.Num()));
			CheckAll(Ref(), TEXT("M5 ящик пуст"));
		});
		// ── M5b: отказ на первом заказе ──
		Add([G, Ref](FCtx& X) { X.Db = { { 145, G[4], 1 }, { 146, G[5], 1 } }; Reopen(Ref()); });
		Add([Ref](FCtx& X) { CheckAllRows(Ref()); X.RefuseNth = 1; X.RefuseStatus = 3; Press(Ref()); });
		Add([Ref](FCtx& X)
		{
			const UAsdaPremiumPostSubsystem* P = X.Sub<UAsdaPremiumPostSubsystem>();
			X.Expect(X.Sent11006 == 1 && CheckedRows(P) == 2 && !P->DebugTakeRunning(), FString::Printf(TEXT("M5b отказ 3 на первом: 11006 ×%d, отметки целы (%d)"), X.Sent11006, CheckedRows(P)));
			CheckAll(Ref(), TEXT("M5b сумка не тронута"));
			X.Db.Reset();
		});
		// ── M6: окно закрыто посреди очереди ──
		Add([G, Ref](FCtx& X) { X.Db = { { 150, G[6], 1 }, { 151, G[7], 1 }, { 152, G[8], 1 } }; Reopen(Ref()); });
		Add([Ref](FCtx& X) { CheckAllRows(Ref()); Press(Ref()); X.Sub<UAsdaPremiumPostSubsystem>()->CloseWindow(); });
		Add([Ref](FCtx& X)
		{
			X.Expect(X.Sent11006 == 1 && X.Db.Num() == 2, FString::Printf(TEXT("M6 закрытие окна: ушедший заказ взят, остальные нет (11006 ×%d, в базе %d)"), X.Sent11006, X.Db.Num()));
			Reopen(Ref());
		});
		Add([Ref](FCtx& X) { CheckAll(Ref(), TEXT("M6 окно открыто снова, очередь забыта")); X.Expect(CheckedRows(X.Sub<UAsdaPremiumPostSubsystem>()) == 1, TEXT("M6 отметки открыты заново")); });
		// ── M7: приз с окном выбора останавливает очередь ──
		Add([G, Ref](FCtx& X)
		{
			X.Expect(X.Prize > 0, FString::Printf(TEXT("M7 коробка с выбором варианта найдена (%d)"), X.Prize));
			AsdaCVarSave::SetForTest(TEXT("asda.Post.ChooseBeforeTake"), TEXT("0"));   // M7 — выбор после получения, как оригинал
			X.Db.Reset();
			if (X.Prize > 0) { X.Db = { { 160, X.Prize, 1, 3 }, { 161, G[9], 1 }, { 162, G[10], 1 } }; }
			Reopen(Ref());
		});
		Add([Ref](FCtx& X) { if (X.Prize > 0) { CheckAllRows(Ref()); Press(Ref()); } });
		Add([Ref](FCtx& X)
		{
			if (X.Prize <= 0) { return; }
			const UAsdaPremiumPostSubsystem* P = X.Sub<UAsdaPremiumPostSubsystem>();
			X.Expect(X.Sent11006 == 1 && !P->DebugTakeRunning(), FString::Printf(TEXT("M7 окно выбора приза: очередь стоит (11006 ×%d)"), X.Sent11006));
			X.Expect(CheckedRows(P) == 2, FString::Printf(TEXT("M7 невзятые заказы остались отмеченными (%d)"), CheckedRows(P)));
			Shot(TEXT("M7_choice_stop"));   // кадр с открытым окном выбора; приз пока скрыт ключом asda.Post.HideUntilChoice
		});
		Add([Ref](FCtx& X)
		{
			if (X.Prize <= 0) { return; }
			// Крестик окна 68500 — путь игрока: окно закрыто без выбора, отложенный приз уходит в сумку.
			if (UAsdaPremiumGachaSubsystem* Gc = X.Sub<UAsdaPremiumGachaSubsystem>()) { Gc->AbortVariantChoice(); }
		});
		Add([Ref](FCtx& X)
		{
			if (X.Prize <= 0) { return; }
			CheckAll(Ref(), TEXT("M7 приз взят, остальное в ящике"));
			Press(Ref());
		});
		Add([Ref](FCtx& X)
		{
			if (X.Prize <= 0) { return; }
			X.Expect(X.Sent11006 == 2 && X.Db.Num() == 0, FString::Printf(TEXT("M7 второе нажатие взяло остаток (11006 ×%d)"), X.Sent11006));
			CheckAll(Ref(), TEXT("M7 ящик пуст"));
		});
		Add([](FCtx& X) { AsdaCVarSave::SetForTest(TEXT("asda.Post.ChooseBeforeTake"), TEXT("1")); });
		// ── M8: ключ 0 = как оригинал ──
		Add([G, Ref](FCtx& X)
		{
			AsdaCVarSave::SetForTest(TEXT("asda.EliteMail.MultiSelect"), TEXT("0"));
			X.Db = { { 170, G[11], 1 }, { 171, G[12], 1 }, { 172, G[13], 1 } };
			Reopen(Ref());
		});
		Add([Ref](FCtx& X)
		{
			UAsdaPremiumPostSubsystem* P = X.Sub<UAsdaPremiumPostSubsystem>();
			P->SimulateCheckboxClick(0);
			P->SimulateCheckboxClick(1);
			X.Expect(CheckedRows(P) == 1 && P->IsRowChecked(1) && P->GetSelectedRow() == 1, TEXT("M8 ключ 0: галочки работают как радиогруппа"));
			Press(Ref());
		});
		Add([Ref](FCtx& X)
		{
			X.Expect(X.Sent11006 == 1 && X.Db.Num() == 2, FString::Printf(TEXT("M8 ключ 0: взят один заказ (11006 ×%d)"), X.Sent11006));
			CheckAll(Ref(), TEXT("M8 ключ 0: ящик и сумка сходятся"));
			AsdaCVarSave::SetForTest(TEXT("asda.EliteMail.MultiSelect"), TEXT("1"));
			X.Db.Reset();
			Reopen(Ref());   // ящик перечитан: остаток M8 снят из «базы», окно должно это показать
		});
		// ── M9: [ИСПРАВЛЕНИЕ] 10.11.2026 выбор варианта ДО получения: крестик оставляет заказ в ящике, ОК берёт выбранный ──
		Add([G, Ref](FCtx& X)
		{
			if (X.Prize <= 0) { return; }
			X.Db = { { 180, X.Prize, 1, 3 }, { 181, G[14], 1 }, { 182, G[15], 1 } };
			X.Sent6581 = 0;
			Reopen(Ref());
		});
		Add([Ref](FCtx& X) { if (X.Prize > 0) { CheckAllRows(Ref()); Press(Ref()); } });
		Add([Ref](FCtx& X)
		{
			if (X.Prize <= 0) { return; }
			const UAsdaPremiumPostSubsystem* P = X.Sub<UAsdaPremiumPostSubsystem>();
			const UAsdaPremiumGachaSubsystem* Gc = X.Sub<UAsdaPremiumGachaSubsystem>();
			X.Expect(X.Sent11006 == 0 && P->IsWaitingPreTakeChoice() && Gc && Gc->IsPreTakeChoiceOpen(),
				FString::Printf(TEXT("M9 окно выбора открыто ДО получения (11006 ×%d)"), X.Sent11006));
			X.Expect(X.Db.ContainsByPredicate([](const FDbRow& D) { return D.Order == 180; }), TEXT("M9 коробка ещё в ящике, пока выбирают"));
			Shot(TEXT("M9_choice_before_take"));
		});
		Add([Ref](FCtx& X)
		{
			if (X.Prize <= 0) { return; }
			// Крестик 68519 — путь игрока.
			if (UAsdaPremiumGachaSubsystem* Gc = X.Sub<UAsdaPremiumGachaSubsystem>()) { Gc->AbortVariantChoice(); }
		});
		Add([Ref](FCtx& X)
		{
			if (X.Prize <= 0) { return; }
			const UAsdaPremiumPostSubsystem* P = X.Sub<UAsdaPremiumPostSubsystem>();
			const int32 Row = RowOf(P, 180);
			X.Expect(X.Sent6581 == 0 && X.Db.Num() == 1 && X.Db[0].Order == 180,
				FString::Printf(TEXT("M9 крестик: коробка осталась в ящике, 6581 ×%d, в базе %d"), X.Sent6581, X.Db.Num()));
			X.Expect(X.Sent11006 == 2, FString::Printf(TEXT("M9 крестик: остальные отмеченные взяты (11006 ×%d)"), X.Sent11006));
			// Отметку отмена снимает, но после перечитывания ящика действует правило эталона: без отметок отмечается нижняя
			//   непустая строка — здесь она единственная, поэтому снова отмечена.
			X.Expect(Row >= 0 && P->IsRowChecked(Row) == (P->GetFilledRowCount() == 1),
				FString::Printf(TEXT("M9 крестик: строка коробки в окне (%d), отметка по правилу эталона"), Row));
			CheckAll(Ref(), TEXT("M9 крестик"));
			Shot(TEXT("M9_after_cancel"));
		});
		Add([Ref](FCtx& X)
		{
			if (X.Prize <= 0) { return; }
			UAsdaPremiumPostSubsystem* P = X.Sub<UAsdaPremiumPostSubsystem>();
			const int32 Row = RowOf(P, 180);
			if (Row >= 0 && !P->IsRowChecked(Row)) { P->SimulateCheckboxClick(Row); }
			Press(Ref());
		});
		Add([Ref](FCtx& X)
		{
			if (X.Prize <= 0) { return; }
			UAsdaPremiumGachaSubsystem* Gc = X.Sub<UAsdaPremiumGachaSubsystem>();
			X.Expect(X.Sent11006 == 0 && Gc && Gc->IsPreTakeChoiceOpen(), TEXT("M9 второе нажатие: снова выбор до получения"));
			if (!Gc) { return; }
			// Профессия и уровень, у которых вариант есть; предпочтительно не сам приз — тогда обмен виден в сумке.
			X.Chosen = -1;
			int32 BestJ = -1, BestL = -1;   // профессия и уровень найденного варианта
			for (int32 J = 0; J < 10 && (X.Chosen < 0 || X.Chosen == X.Prize); ++J)
			{
				for (int32 L = 0; L < 10; ++L)
				{
					Gc->SimulateVariantCombo(true, false, J);
					Gc->SimulateVariantCombo(false, false, L);
					const int32 V = Gc->ResolveCurrentVariant();
					if (V >= 0 && (X.Chosen < 0 || V != X.Prize)) { X.Chosen = V; BestJ = J; BestL = L; }
					if (X.Chosen >= 0 && X.Chosen != X.Prize) { break; }
				}
			}
			// В списках оставляем найденное сочетание (перебор ушёл дальше).
			if (BestJ >= 0) { Gc->SimulateVariantCombo(true, false, BestJ); Gc->SimulateVariantCombo(false, false, BestL); }
			X.Expect(X.Chosen >= 0 && Gc->ResolveCurrentVariant() == X.Chosen,
				FString::Printf(TEXT("M9 в списках есть вариант (%d, профессия %d, уровень %d)"), X.Chosen, BestJ, BestL));
			Gc->ConfirmVariant();   // ОК 68511
		});
		Add([Ref](FCtx& X)
		{
			if (X.Prize <= 0 || X.Chosen < 0) { return; }
			const UAsdaPremiumGachaSubsystem* Gc = X.Sub<UAsdaPremiumGachaSubsystem>();
			const UAsdaInventorySubsystem* Inv = X.Sub<UAsdaInventorySubsystem>();
			X.Expect(X.Sent11006 == 1 && X.Sent6581 == 1 && X.LastVariant == X.Chosen && Gc->GetLastPostSwapVariant() == X.Chosen,
				FString::Printf(TEXT("M9 ОК: 11006 ×%d, затем 6581 ×%d с вариантом %d (выбран %d)"), X.Sent11006, X.Sent6581, X.LastVariant, X.Chosen));
			int32 Have = 0;
			for (int32 I = 0; Inv && I < kBag; ++I) { const FAsdaItemInstance* Cl = Inv->GetCell(1, I); if (Cl && !Cl->IsEmpty() && Cl->ItemCode == X.Chosen) { ++Have; } }
			X.Expect(X.Db.Num() == 0 && Have >= 1, FString::Printf(TEXT("M9 ОК: ящик пуст, в сумке выбранный вариант %d (×%d)"), X.Chosen, Have));
			X.Expect(!Gc->IsPostSwapPending(), TEXT("M9 ОК: обмен завершён"));
			CheckAll(Ref(), TEXT("M9 ОК"));
			Shot(TEXT("M9_after_ok"));
		});
		// ── M10: окно выбора закрыто НЕ крестиком (ESC, закрытие сцены гачи, общий сброс окон) — та же отмена ──
		Add([Ref](FCtx& X)
		{
			if (X.Prize <= 0) { return; }
			X.Db = { { 190, X.Prize, 1, 3 } };
			Reopen(Ref());
		});
		Add([Ref](FCtx& X)
		{
			if (X.Prize <= 0) { return; }
			UAsdaPremiumPostSubsystem* P = X.Sub<UAsdaPremiumPostSubsystem>();
			const int32 Row = RowOf(P, 190);
			if (Row >= 0 && !P->IsRowChecked(Row)) { P->SimulateCheckboxClick(Row); }
			Press(Ref());
		});
		Add([Ref](FCtx& X)
		{
			if (X.Prize <= 0) { return; }
			UAsdaPremiumGachaSubsystem* Gc = X.Sub<UAsdaPremiumGachaSubsystem>();
			X.Expect(Gc && Gc->IsPreTakeChoiceOpen(), TEXT("M10 выбор до получения открыт"));
			if (Gc) { Gc->CloseVariantWindow(); }   // окно прячет чужой путь, не крестик
		});
		Add([Ref](FCtx& X)
		{
			if (X.Prize <= 0) { return; }
			const UAsdaPremiumPostSubsystem* P = X.Sub<UAsdaPremiumPostSubsystem>();
			X.Expect(X.Sent11006 == 0 && !P->IsWaitingPreTakeChoice() && !P->DebugTakeRunning() && X.Db.Num() == 1,
				FString::Printf(TEXT("M10 окно закрыто не крестиком: 11006 ×%d, ожидание снято, коробка в ящике (%d)"), X.Sent11006, X.Db.Num()));
			CheckAll(Ref(), TEXT("M10"));
			X.Db.Reset();
			Reopen(Ref());
		});
		// ── I: иконки всех замеченных кодов ──
		Add([Ref](FCtx& X)
		{
			UAsdaInventorySubsystem* Inv = X.Sub<UAsdaInventorySubsystem>();
			UAsdaItemTableSubsystem* IT = X.Sub<UAsdaItemTableSubsystem>();
			if (!Inv || !IT) { X.Expect(false, TEXT("I подсистемы есть")); return; }
			const int32 Cell = 57;
			int32 Bad = 0, Unknown = 0;
			FString List;
			for (const int32 Code : kSeenCodes)
			{
				const int32 Cont = ServerContainer(IT, Code);
				const bool bKnown = IT->GetItem(Code) != nullptr || IT->IsSpecialItemCode(Code) || IT->IsSowelCode(Code);
				Inv->SetTab(Cont == 1 ? 1 : 0);
				const int32 Was = Inv->GetCellItemCode(Cont, Cell);
				TArray<uint8> Keep;
				if (Was > 0) { if (const FAsdaItemInstance* I = Inv->GetCell(Cont, Cell)) { Keep = I->Raw; } }
				Inv->ApplyExternalRecord(FAsdaItemRecordWriter::Cell(Code, Cont, Cell, 1));
				const bool bDrawn = CellIconDrawn(Inv, Cell);
				if (Keep.Num() >= FAsdaItemInstance::kRecordSize) { Inv->ApplyExternalRecord(Keep, UAsdaInventorySubsystem::EExternalRecordMode::AsIs); }
				else { Inv->ApplyExternalRecord(FAsdaItemRecordWriter::ClearCell(Cont, Cell)); }
				if (!bDrawn) { ++Bad; if (!bKnown) { ++Unknown; } List += FString::Printf(TEXT(" %d(к%d%s)"), Code, Cont, bKnown ? TEXT("") : TEXT(",нет в таблицах")); }
			}
			Inv->SetTab(1);
			X.Expect(Bad == 0, FString::Printf(TEXT("I иконки %d кодов из ящика: не рисуются %d (из них нет в таблицах клиента %d)%s"),
				(int32)UE_ARRAY_COUNT(kSeenCodes), Bad, Unknown, *List.Left(600)));
		});
		// ── A: все коды таблиц клиента — у какого атласа нет текстуры (такой предмет в сумке рисуется пустой ячейкой) ──
		Add([Ref](FCtx& X)
		{
			UAsdaItemTableSubsystem* IT = X.Sub<UAsdaItemTableSubsystem>();
			UImage* Img = NewObject<UImage>(GetTransientPackage());
			if (!IT || !Img) { X.Expect(false, TEXT("A таблицы и пробная картинка есть")); return; }
			TMap<FString, int32> BadAtlas;   // атлас без текстуры → число кодов
			TMap<FString, int32> FirstCode;  // атлас → первый код
			int32 Total = 0, NoIcon = 0, Bad = 0;
			for (const FAsdaItemRecord& R : IT->GetAllItems())
			{
				++Total;
				const FAsdaItemIcon Ic = IT->GetItemIcon(R.ItemCode, true);
				if (!Ic.IsValid()) { ++NoIcon; continue; }
				if (AsdaUi::ApplyItemIcon(X.GI(), Img, R.ItemCode, FVector2D(32.f, 32.f), true, true)) { continue; }
				++Bad;
				BadAtlas.FindOrAdd(Ic.Atlas) += 1;
				if (!FirstCode.Contains(Ic.Atlas)) { FirstCode.Add(Ic.Atlas, R.ItemCode); }
			}
			FString List;
			for (const TPair<FString, int32>& P : BadAtlas) { List += FString::Printf(TEXT(" %s×%d(напр. %d)"), *P.Key, P.Value, FirstCode[P.Key]); }
			UE_LOG(LogAsdaPostWithdrawTest, Warning, TEXT("%s A кодов %d, без иконки в таблице %d, атлас не загружается %d: %s"), kTag, Total, NoIcon, Bad, *List);
			X.Expect(true, FString::Printf(TEXT("A аудит атласов выполнен: кодов %d, атлас не загружается у %d (%d атласов)"), Total, Bad, BadAtlas.Num()));
		});
		Add([Ref](FCtx& X) { CheckAll(Ref(), TEXT("F финал")); });
	}

	// Весь прогон.
	static void Start(const FCtxRef& C)
	{
		UAsdaInventorySubsystem* Inv = C->Sub<UAsdaInventorySubsystem>();
		UAsdaItemTableSubsystem* IT = C->Sub<UAsdaItemTableSubsystem>();
		UAsdaNetSubsystem* Net = C->Sub<UAsdaNetSubsystem>();
		UAsdaPremiumPostSubsystem* P = C->Sub<UAsdaPremiumPostSubsystem>();
		if (!Inv || !IT || !Net || !P) { C->Expect(false, TEXT("подсистемы инвентаря/таблиц/сети/ящика есть")); Finish(C); return; }
		if (P->IsWindowVisible()) { C->Expect(false, TEXT("окно ящика не открыто игроком")); Finish(C); return; }
		if (!PickCodes(C, IT)) { C->Expect(false, FString::Printf(TEXT("коды подобраны: экипировка %d/20, расходник %d"), C->Gear.Num(), C->Spend)); Finish(C); return; }
		AsdaCVarSave::SetForTest(TEXT("asda.Inven.ShellX2"), 1);
		AsdaCVarSave::SetForTest(TEXT("asda.EliteMail.MultiItemRows"), 1);
		AsdaCVarSave::SetForTest(TEXT("asda.EliteMail.SortNewestFirst"), 0);
		auto Snap = [Inv](int32 Cont, TArray<TArray<uint8>>& Out)
		{
			for (int32 I = 0; I < kBag; ++I) { const FAsdaItemInstance* X = Inv->GetCell(Cont, I); Out.Add(X && !X->IsEmpty() ? X->Raw : TArray<uint8>()); }
		};
		Snap(1, C->SavedBag); Snap(2, C->SavedSpend);
		for (int32 I = 0; I < kBag; ++I) { Inv->ClearCell(1, I); Inv->ClearCell(2, I); }
		C->Mem1.Init(0, kBag); C->Mem2.Init(0, kBag);
		C->Self = MakeUnique<FAsdaNetSelfScope>(Net, kCmd);
		Net->SelectedCharId = kCharId;
		TWeakPtr<FCtx> WC(C);
		C->CapGen = AsdaSelfTestKit::ArmCapture(Net, [WC](uint16 Op, const uint8* Pp, int32 Len, bool, TCHAR)
		{
			if (TSharedPtr<FCtx> X = WC.Pin())
			{
				if (Op != 11004 && Op != 11006 && Op != 6581) { return; }
				if (Op == 6581) { ++X->Sent6581; }
				TArray<uint8> Body;
				if (Pp && Len > 0) { Body.Append(Pp, Len); }
				// В перехват приходит сам payload до защиты (`SendSecured`): 11006 — запись 10 б, 11004 — 4 нулевых байта.
				if (Op == 11006) { ++X->Sent11006; }
				X->Out.Add(TPair<int32, TArray<uint8>>((int32)Op, MoveTemp(Body)));
			}
		}, 0, kCmd);
		C->Expect(C->CapGen != 0 && Inv->GetContainerSize(1) == kBag, FString::Printf(TEXT("перехват взведён, ёмкость сумки %d"), Inv->GetContainerSize(1)));
		if (!C->CapGen) { Finish(C); return; }
		BuildSteps(C);
		UWorld* W = C->W.Get();
		W->GetTimerManager().SetTimer(C->H, FTimerDelegate::CreateLambda([C]()   // контекст держит таймер
		{
			if (C->bDone) { return; }
			UAsdaPremiumPostSubsystem* Pp = C->Sub<UAsdaPremiumPostSubsystem>();
			if (ServeOne(C)) { C->IdleTicks = 0; return; }
			// Ожидание выбора варианта (окно 68500 до 11006) — ход игрока, а не работа ящика: следующий шаг его и делает.
			const bool bBusy = Pp && ((Pp->DebugTakeRunning() && !Pp->IsWaitingPreTakeChoice()) || Pp->DebugInFlight());
			if (bBusy) { C->IdleTicks = 0; return; }
			if (++C->IdleTicks < 2) { return; }
			C->IdleTicks = 0;
			if (C->Step < C->Steps.Num()) { C->Steps[C->Step++](); return; }
			Finish(C);
		}), 0.15f, true);
	}
}

static FAutoConsoleCommandWithWorldAndArgs GAsdaSelfTestPostWithdraw(
	TEXT("asda.SelfTest.PostWithdraw"),
	TEXT("[ПОРТ] 05.11.2026 (баг «призраки после забора из премиум-ящика»): путь игрока 11004/11005/11006/11007 с эмулятором "
	     "сервера — модель сумки == память сервера, иконки ⇔ занятость, строки ящика; одна/несколько/9 заказов, почти полная "
	     "сумка, отказы 3/0/2, ячейка ответа ≠ запроса, расходник, Amount > 7; иконки всех кодов ящика. Офлайн. `delay=N` `quit`."),
	FConsoleCommandWithWorldAndArgsDelegate::CreateLambda([](const TArray<FString>& Args, UWorld* W)
	{
		using namespace AsdaPostWithdrawTest;
		const FAsdaSelfTestArgs KA = FAsdaSelfTestArgs::Parse(Args);
		// Окно инвентаря и очередь его пакетов живут при своём персонаже в мире — офлайн его нет: штатный фейковый спавн
		//   и повторный запуск через 2 с (как `asda.SelfTest.ChargeStackReserve`).
		if (!KA.Has(TEXT("selfready")) && W && GEngine && AsdaSelfTestKit::NeedSelfSpawn(W))
		{
			GEngine->Exec(W, TEXT("Asda.Load.CurrentMap 3"));
			GEngine->Exec(W, TEXT("Asda.World.FakeSelfSpawn 256 256"));
			const FString Again = FString::Printf(TEXT("%s selfready%s"), kCmd, KA.bQuit ? TEXT(" quit") : TEXT(""));   // повторная команда
			TWeakObjectPtr<UWorld> WW(W);
			FTimerHandle H;
			W->GetTimerManager().SetTimer(H, FTimerDelegate::CreateLambda([WW, Again]() { if (WW.IsValid() && GEngine) { GEngine->Exec(WW.Get(), *Again); } }), 2.f, false);
			return;
		}
		AsdaCVarSave::ArmWorldGuard(W, kCmd);
		static const uint16 kGuardOps[] = { 11004, 11006 };   // опкоды самотеста в окне отказа «онлайн»
		if (AsdaSelfTestKit::RefuseStart(W, kCmd, KA, 0.f, MakeArrayView(kGuardOps))) { return; }
		if (!W) { return; }
		FCtxRef C = MakeShared<FCtx>();
		C->W = W;
		C->bQuit = KA.bQuit;
		if (KA.Delay > 0.f)
		{
			FTimerHandle H;
			W->GetTimerManager().SetTimer(H, FTimerDelegate::CreateLambda([C]() { Start(C); }), KA.Delay, false);
		}
		else
		{
			Start(C);
		}
	}));

#endif // ASDA_DEV_CONSOLE && !UE_BUILD_SHIPPING
