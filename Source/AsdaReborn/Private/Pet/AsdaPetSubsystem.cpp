// Copyright (c) 2026 @sitxovski (Discord)
// ═══════════════════════════════════════════════════════════════════════════════════════════════
// Порт клиентской модели данных питомцев (`CPetData`) и всей их сети — волна 1.
// Что откуда взято и чем доказано — в шапке `Public/Pet/AsdaPetSubsystem.h`; здесь не повторяется.
//
// ══════════════════ РАСКЛАДКА ВСЕХ ЧЕТЫРНАДЦАТИ ПАКЕТОВ ОДНОЙ ТАБЛИЦЕЙ ═════════════════════════
//  C→S (конверт «с секретом», длина тела — со стороны сервера, `sub_523F00(pkt, N, userNum, 1)`):
//   6100   8 б  charId i32@0 · petUid i32@4
//   6102  13 б  charId@0 · bag1 u8@4 · slot1 i16@5 (катализатор 107) · bag2@7 · slot2@8 (яйцо 109)
//                        · bag3@10 · slot3@11 (бустер, −1 = нет)
//   6108  11 б  charId@0 · petUid@4 · bag u8@8 · slot i16@9
//   6110   8 б  charId@0 · petUid@4
//   6112  18 б  charId@0 · unionIndex i16@4 · petUid i32@6/@10/@14
//   6118  24 б  charId@0 · petUid@4 · name char[16]@8
//   6121   7 б  charId@0 · bag u8@4 · slot i16@5
//   6148   9 б  petUid i32@0 · itemSlot i16@4 · itemBag u8@6 · premiumSlot i16@7  ★ charId НЕТ
//  S→C (тело после 9-байтовой шапки кадра; у всех, кроме 6106, возможен хвостовой паддинг 0..19 б):
//   6101   9 б  charId@0 · result u8@4 · petUid i32@5
//   6103  18 б  charId@0 · result u8@4 · bag1@5 · slot1 i16@6 · bag2@8 · slot2@9 · bag3@11
//                        · slot3@12 · weight i32@14
//   6104  29 б  userNum u16@0 · charId i32@2 · petUid i32@6 · fullness u8@10 · 3×{u16,i32}@11/@17/@23
//   6105  15 б  userNum@0 · charId@2 · petUid@6 · exp u16@10 · bLevelUp u8@12 · newLevel u16@13
//   6106  5 + 46·N  charId@0 · flag u8@4 · N записей по 46
//   6107   8 б  charId@0 · petUid@4
//   6109  89 б  charId@0 · petUid@4 · Item(57)@8 · result u8@65 · fullness u8@66 · weight i32@67
//                        · 3×{u16,i32}@71/@77/@83
//   6111   9 б  charId@0 · petUid@4 · result u8@8
//   6113  56 б  charId@0 · result u8@4 · newPetUid i32@5 · PetRecord(41)@9 · u16@50/@52/@54 —
//                        ★ МЛАДШИЕ 16 БИТ uid трёх съеденных материалов (их клиент УДАЛЯЕТ сам)
//   6119  25 б  charId@0 · petUid@4 · name char[16]@8 · result u8@24
//   6120  22 б  charId@0 · petTypeId u16@4 (0xFFFF = убрать) · name char[16]@6  ← принимается СЕТЬЮ
//   6122  13 б  charId@0 · result u8@4 · newCount u8@5 · bag u8@6 · slot i16@7 · weight i32@9
//   6123   5 б  charId@0 · count u8@4
//   6149 110 б  ★ petUid i32@0 (НЕ charId!) · PetRecord(41)@4 · itemSlot u16@45 · itemBag u8@47
//                        · result u8@48 · weight i32@49 · Item(57)@53
//  ЗАПИСЬ СПИСКА (46 б) = petUid i32@0 · PetRecord(41)@4 · bIsActive u8@45. Свободного байта НЕТ:
//   `v12[44] = v22` в сборщике (GameServer.c:35452) — это 41-й байт САМОГО PetRecord, который
//   Hex-Rays отрезал от `__int16 a3[20]` (a3 на [ebp−0x30], v22 на [ebp−0x8] = a3 + 0x28).
//   Клиент это подтверждает: третий бонус он читает как dword@rec+39 плюс word@rec+43
//   (network.c:25653-25655), то есть запись кончается на +44, а +45 — признак «призван».
//   ⇒ формулировка «+44 спарка» из `pet_windows_re_report.md` §4.3 ОШИБОЧНА: по ней терялся бы
//     старший байт третьего бонуса.
//
// ★ ПОЛЕ «ДЕНЬГИ» В ОТЧЁТЕ — НА САМОМ ДЕЛЕ ВЕС. i32 в 6103@14, 6109@67, 6122@9, 6149@49 клиент
//   отдаёт в `HUD_UpdateMiniWeightBar(g_pHUD, …)` (item_enhance.c:5037, network.c:25934,
//   item_enhance.c:5036), а сервер получает его из `sub_43FA00` (текущий вес) и вычитает вес
//   потраченных предметов (GameServer.c:34744-34769). Это ТЕКУЩИЙ ВЕС ПЕРСОНАЖА, не деньги.
//
// ══════════════════════════ СЕМАНТИКА ФЛАГА +4 В СПИСКЕ 6106 ══════════════════════════════════
//   Сервер режет список на пачки: добавляет запись, пока `v13 + 46 < 0x28A` (650 б), затем шлёт
//   накопленное и начинает новую пачку с той же 5-байтовой шапкой (GameServer.c:35454-35470).
//   Максимум 14 записей в пачке (5 + 14·46 = 649), 30 слотов — до трёх пачек.
//   Байт +4 приходит из ответа БД и работает КАК ЗАПРЕТ ОТПРАВКИ: и накопление, и финальный слив
//   идут только при `LOBYTE(Src[1]) == 0` (:35453, :35469); в пути после вылупления он ставится
//   нулём явно (:34785). То есть до клиента пакет с ненулевым флагом не доходит В ПРИНЦИПЕ.
//   КЛИЕНТ ЭТОТ БАЙТ ИГНОРИРУЕТ: `netRecvPetListPacket` читает 5 байт шапки, сверяет charId и
//   больше к пятому байту не обращается (network.c:25617-25634). Склейка пачек держится не на
//   флаге, а на том, что записи ДОБАВЛЯЮТСЯ ПО КЛЮЧУ `petUid` (`petDataSelectInvItemById`) —
//   список не чистится перед заливкой. Порт повторяет это `AddOrUpdate`.
//   ⇒ Окно 43000 увидит все 30 слотов после прихода всех пачек, без счётчика и без ожидания.
//
// ══════════════════════════════ ДЕФЕКТЫ ОРИГИНАЛА, ЗАМЕЧЕННЫЕ ЗДЕСЬ ═══════════════════════════
//   ПД1. `Recv_IncubatorReq` при расхождении charId отвечает ОПКОДОМ 6101 с телом 18 б
//       (GameServer.c:34566) — то есть телом 6103 под чужим опкодом. Клиентский приёмник 6101
//       ждёт 9 байт и прочтёт мусор как `result`/`petUid`.
//   ПД2. `Recv_PetConditionUp` в одной ветке отвечает ОПКОДОМ 6101 с телом 89 б
//       (GameServer.c:35532) — телом 6109.
//       Обе ветки — «сервер сам себя перепутал»; лечится на сервере, не на клиенте. Порт от них
//       защищён конструктивно: длина тела 6101 проверяется, лишний хвост игнорируется, а поля
//       читаются по своим смещениям. В лог пишется предупреждение о подозрительной длине.
//   ПД3. Приёмник 6119 у КЛИЕНТА проваливается по switch: `case 2` → 101120 → `goto LABEL_10` →
//       101118 → `goto LABEL_11` → 101121 (item_enhance.c:4956-4974). Игрок видит три нотиса
//       вместо одного. Волне 2 решать с владельцем, воспроизводить ли 1:1; здесь состояние
//       обновляется одинаково в обоих случаях, поведение нотисов сюда не входит.
//   ПД4. `Petlimitbreaktable.txt` читается загрузчиком со сдвигом — см. `AsdaPetTableRows.h`.
// ═══════════════════════════════════════════════════════════════════════════════════════════════
#include "Pet/AsdaPetSubsystem.h"
#include "AsdaPerf.h"   // ★ R4-c (14.11.2026): области замера времени игрового потока
#include "AsdaDevConsole.h"   // ★ античит А2: ASDA_DEV_CVAR / ASDA_DEV_CONSOLE — дев-ключи и команды вне Shipping
#include "AsdaSelf.h"   // ★ В2-A дедупа №2: общая точка «это я?» (класс/уровень/charId/userNum)

#include "AsdaNetSubsystem.h"
#include "AsdaBytes.h"
#include "AsdaDataTableCache.h"
#include "Net/NetOpcodes.h"
#include "Engine/GameInstance.h"
#include "Engine/World.h"
#include "EngineUtils.h"                      // TActorIterator — поиск модели своего питомца (баг 6 пачки 20)
#include "World/AsdaWorldSubsystem.h"         // LocalPlayerActor — свой персонаж
#include "Items/AsdaItemTableSubsystem.h"   // ★ волна 8: общие точки AsdaPetUi::PetRow / ExpRow
#include "Items/AsdaItemDbRows.h"           // ★ волна 8: FAsdaPetRow / FAsdaPetexptableRow
#include "Text/AsdaLangRows.h"               // [ПОРТ] 09.10.2026 (мультиязычность): имя стадии питомца на языке сессии
#include "Engine/DataTable.h"
#include "UI/AsdaNotice.h"            // ★ волна 8: общая точка нотиса AsdaPetUi::NoticeMsgBox               // ★ волна 8: обход `petexptable` в AsdaPetUi::ExpRow
#include "AsdaOutgoingGate.h"               // ★ С-Р1: общий офлайн-гейт отладочных команд (RefuseDebugWhenOnline)
#include "Test/AsdaSelfTestKit.h"   // ★ 11.10.2026 (В3 S1): общий каркас самотестов (аргументы, отказ, перехват, итог, выход)
#include "AsdaPetSubsystem_Private.h"   // ★ R3-c 12.11.2026: общее для частей AsdaPetSubsystem*.cpp

DEFINE_LOG_CATEGORY(LogAsdaPet);   // ★ R2: было DEFINE_LOG_CATEGORY_STATIC — категория общая для частей

const TCHAR* UAsdaPetSubsystem::kPetInfoTablePath      = TEXT("/Game/AsdaAssets/Data/Pet/DT_PetInfo.DT_PetInfo");
const TCHAR* UAsdaPetSubsystem::kPetExpTablePath       = TEXT("/Game/AsdaAssets/Data/Pet/DT_PetExpServer.DT_PetExpServer");
const TCHAR* UAsdaPetSubsystem::kIncubateRateTablePath = TEXT("/Game/AsdaAssets/Data/Pet/DT_PetIncubateRate.DT_PetIncubateRate");
const TCHAR* UAsdaPetSubsystem::kUnionRecipeTablePath  = TEXT("/Game/AsdaAssets/Data/Pet/DT_PetUnionRecipe.DT_PetUnionRecipe");
const TCHAR* UAsdaPetSubsystem::kLimitBreakTablePath   = TEXT("/Game/AsdaAssets/Data/Pet/DT_PetLimitBreak.DT_PetLimitBreak");
const TCHAR* UAsdaPetSubsystem::kEvoStageTablePath     = TEXT("/Game/AsdaAssets/Data/Pet/DT_PetEvoStage.DT_PetEvoStage");

	// ★ Лечение дефекта ПД4 (сдвиг загрузчика `Petlimitbreaktable.txt`). Дефолт 0 = ВОСПРОИЗВОДИМ
	//   сервер 1:1 (закон `never-invent-only-original`); 1 = читать колонки файла, как их задумывал
	//   издатель. Решение о лечении — за владельцем (закон `fix-original-defects`).
	ASDA_DEV_CVAR(int32, CVarPetLimitBreakFixTable,
		TEXT("asda.Pet.LimitBreakFixTable"), 0,
		TEXT("Пороги лимит-брейка: 0 — как в ПАМЯТИ сервера (загрузчик читает 4 колонки из 5, ")
		TEXT("значения сдвинуты — дефект оригинала); 1 — как в файле (Level/Thr1/Thr2/Thr3)."));

namespace {
	// ★★★ ВОЛНА 8 (пункт 1 сквозной ревизии): СКОЛЬКО ЖДАТЬ ОТВЕТА СЕРВЕРА.
	//   У оригинала таймаута нет и быть не может: при разрыве он ЗАКРЫВАЕТ КЛИЕНТ (память
	//   `original-quits-client-on-disconnect`). У порта клиент живёт дальше, а отправка вдобавок
	//   отложена на кадр — и три окна питомцев навсегда залипали в «жду ответа», если пакет не
	//   ушёл. 20 с выбраны с запасом: живой 6109/6113/6149 приходит за десятки миллисекунд
	//   (лог `Reports/pet/v6/`), а лаг сервера в разы больше 20 с означает, что связи уже нет.
	//   0 — таймаут выключен (поведение до волны 8).
	static ASDA_DEV_CVAR(int32, CVarPetSendTimeoutSec,
		TEXT("asda.Pet.SendTimeoutSec"), 20,
		TEXT("Сколько секунд окно питомцев ждёт ответа сервера, прежде чем снять засов «запрос в ")
		TEXT("полёте» и оживить свою кнопку. 0 — не снимать (как было до волны 8)."));
}

namespace AsdaPetUi {
// ═══════════════════ ★★★ ОБЩИЕ ТОЧКИ ПЯТИ ОКОН ПИТОМЦЕВ (ВОЛНА 8) ═════════════════════════════
//   Обоснование каждой — у объявления в `Pet/AsdaPetSubsystem.h`.
	bool PetRow(const UGameInstance* GI, int32 PetIndex, FAsdaPetRow& Out)
	{
		const UAsdaItemTableSubsystem* IT =
			GI ? const_cast<UGameInstance*>(GI)->GetSubsystem<UAsdaItemTableSubsystem>() : nullptr;
		if (!IT || PetIndex < 0) { return false; }
		return IT->GetPet(PetIndex, Out);
	}

	bool ExpRow(const UGameInstance* GI, int32 Pos, FAsdaPetexptableRow& Out)
	{
		const UAsdaItemTableSubsystem* IT =
			GI ? const_cast<UGameInstance*>(GI)->GetSubsystem<UAsdaItemTableSubsystem>() : nullptr;
		const UDataTable* DT = IT ? IT->GetDataTable(TEXT("petexptable")) : nullptr;
		if (!DT || Pos < 0) { return false; }
		TArray<FAsdaPetexptableRow*> Rows;
		DT->GetAllRows<FAsdaPetexptableRow>(TEXT("petexptable"), Rows);
		for (const FAsdaPetexptableRow* R : Rows)
		{
			// ★ [ПОРТ] 09.10.2026 (мультиязычность): копия с `StageName` на языке сессии (оверлей `petexptable`).
			if (R && R->Level == Pos + 1) { Out = AsdaLangRows::CopyLocalized(DT, *R); return true; }
		}
		if (const FAsdaPetexptableRow* ByName =
			DT->FindRow<FAsdaPetexptableRow>(FName(*FString::FromInt(Pos)), TEXT("petexptable"), /*bWarn*/ false))
		{
			Out = AsdaLangRows::CopyLocalized(DT, *ByName); return true;   // [ПОРТ] 09.10.2026 (мультиязычность)
		}
		if (Rows.IsValidIndex(Pos) && Rows[Pos]) { Out = AsdaLangRows::CopyLocalized(DT, *Rows[Pos]); return true; }   // [ПОРТ] 09.10.2026 (мультиязычность)
		return false;
	}

	bool ExpFraction(const UGameInstance* GI, const FAsdaPetSlot& S, float& OutFrac)
	{
		OutFrac = 0.f;
		FAsdaPetRow Pr;
		if (!PetRow(GI, S.PetIndex, Pr) || S.Level < 1) { return false; }
		FAsdaPetexptableRow Lo, Hi;
		if (!ExpRow(GI, S.Level - 1, Lo) || !ExpRow(GI, S.Level, Hi)) { return false; }
		// ★ М7: поле `DurabilityFactor` (+0x50) генерируемого заголовка — это ExpRateX10.
		const double Rate = (double)Pr.DurabilityFactor / 10.0;
		const double Min  = (double)Lo.Exp * Rate;
		const double Max  = (double)Hi.Exp * Rate;
		if (Max - Min <= 0.0) { return false; }
		// ⚠ БЕЗ КЛАМПА — дословно эталону (AsdaStory_game.c:411572). См. объявление.
		OutFrac = (float)(((double)S.Exp - Min) / (Max - Min));
		return true;
	}

	void DrainEvents(TArray<TTuple<int32, int32, int32>>& Queue,
		TFunctionRef<void(int32, int32, int32)> Handler)
	{
		if (Queue.Num() == 0) { return; }
		TArray<TTuple<int32, int32, int32>> Batch = MoveTemp(Queue);
		Queue.Reset();
		for (const TTuple<int32, int32, int32>& E : Batch)
		{
			Handler(E.Get<0>(), E.Get<1>(), E.Get<2>());
		}
	}

	bool IsSendLinkUp(const UGameInstance* GI)
	{
		UGameInstance* G = const_cast<UGameInstance*>(GI);
		const UAsdaNetSubsystem* N = G ? G->GetSubsystem<UAsdaNetSubsystem>() : nullptr;
		const UAsdaPetSubsystem* P = G ? G->GetSubsystem<UAsdaPetSubsystem>() : nullptr;
		if (!N) { return false; }
		return N->IsConnected() || (P != nullptr && P->IsSendCaptureArmed());
	}

	float SendTimeoutSec()
	{
		return (float)FMath::Max(0, CVarPetSendTimeoutSec.GetValueOnGameThread());
	}

	void NoticeMsgBox(const UGameInstance* GI, int32 LocId, int32 EtalonNoticeId)
	{
		AsdaNotice::MsgBoxLoc(GI, EAsdaMsgCategory::ItemRefused, LocId, kMsgBoxOkLoc, EtalonNoticeId);
	}

	int32 StandI32(const TArray<uint8>& B, int32 Ofs)
	{
		return (Ofs >= 0 && B.Num() >= Ofs + 4) ? AsdaBytes::RdI32(&B[Ofs]) : kStandOutOfRange;
	}
	int32 StandI16(const TArray<uint8>& B, int32 Ofs)
	{
		return (Ofs >= 0 && B.Num() >= Ofs + 2) ? AsdaBytes::RdI16(&B[Ofs]) : kStandOutOfRange;
	}
	int32 StandU8(const TArray<uint8>& B, int32 Ofs)
	{
		return (Ofs >= 0 && B.Num() >= Ofs + 1) ? (int32)B[Ofs] : -1;
	}
}

// ═══════════════════════════════════ ЖИЗНЕННЫЙ ЦИКЛ ════════════════════════════════════════════
void UAsdaPetSubsystem::Initialize(FSubsystemCollectionBase& Collection)
{
	Super::Initialize(Collection);
	LoadTables();

	if (UAsdaNetSubsystem* N = Net())
	{
		HPetPacket = N->OnPetPacket.AddUObject(this, &UAsdaPetSubsystem::OnPetPacket);
		// 6120 УЖЕ принимается сетью и потребляется миром — второй приёмник не заводим
		//   (закон `check-existing-before-creating`). Подписываемся, чтобы знать своего питомца
		//   в мире и сверять его со списком.
		HPeerPet = N->OnPeerPet.AddUObject(this, &UAsdaPetSubsystem::OnPeerPet6120);
		bNetBound = true;
	}
	UE_LOG(LogAsdaPet, Log,
		TEXT("[питомцы] подсистема поднята: породы=%d уровни=%d шансы=%d рецепты=%d лимит-брейк=%d карты=%d, сеть %s"),
		PetInfo.Num(), PetExp.Num(), IncubateRate.Num(), UnionRecipe.Num(), LimitBreak.Num(),
		EvoStage.Num(), bNetBound ? TEXT("привязана") : TEXT("НЕ привязана"));
}

void UAsdaPetSubsystem::Deinitialize()
{
	if (UAsdaNetSubsystem* N = Net())
	{
		if (HPetPacket.IsValid()) { N->OnPetPacket.Remove(HPetPacket); }
		if (HPeerPet.IsValid())   { N->OnPeerPet.Remove(HPeerPet); }
	}
	HPetPacket.Reset();
	HPeerPet.Reset();
	bNetBound = false;
	Super::Deinitialize();
}

void UAsdaPetSubsystem::ResetSessionState(EAsdaSessionResetReason Reason)
{
	// Сессионное — уходит ВСЕГДА, включая смену карты: сокет пересоздан, «запрос в полёте»
	//   мёртв, очередь отложенных отправок ушла бы в закрытый сокет.
	CallLatch.Disarm();   // ★ S2 (В2-A): засов «6100 в полёте» — общая точка
	bFeedNeedsActivePetNotice = false;
	Pending.Reset();
	// ★ 27.09 (СРЕДН-1 ревизии P31/P32): юнион «в полёте» умирает вместе с сокетом — его 6113 уже
	//   не придёт, а список uid, удалённых 6111, не должен дожить до следующего юниона. Сбрасывается
	//   на ЛЮБОЙ причине, включая смену карты. Массив переменной длины — Reset штатен.
	UnionReleasedUids.Reset();
	// ★ Волна 3, КРИТ-1: перехват исходящих — ИНСТРУМЕНТ ПРОГОНА, он не должен пережить ни
	//   разрыв связи, ни смену карты: при взведённом перехвате в сеть не уходит ничего.
	EndSendCapture();

	if (Reason == EAsdaSessionResetReason::MapChanged)
	{
		// ★ Список и число ячеек ПЕРЕЖИВАЮТ переход. Данные персонажа на сервере целы, и он
		//   присылает их заново и при портале (6123 — GameServer.c:114478; 6106 — через
		//   `UPCharPetList`, вызов из ДВУХ мест `DBManager.exe.c:3622` и `:11412`). Но если
		//   какой-то путь перехода их не пришлёт, стирание оставило бы окно 43000 пустым до
		//   перезахода — а пустой список ничем не отличается от «питомцев нет».
		//   Заново пришедший 6106 всё равно перезапишет записи по ключу `petUid`.
		bFeedNeedsActivePetNotice = false;
		RxSinceReset = 0;   // счётчик диагностики привязан к отрезку между сбросами, а не к сессии
		UE_LOG(LogAsdaPet, Log, TEXT("[питомцы] смена карты: список (%d) и ячейки (%d) сохранены, ")
			TEXT("сброшены только «запрос в полёте» и очередь отправки"), Slots.Num(), PetPossessionCount);
		return;
	}

	// ★ Диагностика закона `session-reset-drops-early-data`: если к моменту входа в мир пет-пакеты
	//   уже приходили, значит данные приходят РАНЬШЕ сброса и зачистка их съедает.
	if (Reason == EAsdaSessionResetReason::EnteringWorld && RxSinceReset > 0)
	{
		UE_LOG(LogAsdaPet, Warning,
			TEXT("[питомцы] ВНИМАНИЕ: до сигнала «вход в мир» уже пришло %d пет-пакетов — ")
			TEXT("они будут стёрты. Проверить порядок (закон session-reset-drops-early-data)."),
			RxSinceReset);
	}

	Slots.Reset();                                  // реестр переменной длины — Reset штатен
	PetPossessionCount = kDefaultPossession;        // сервер тоже начинает с 6 и клампит 6..30
	ActiveUniqueId = -1;
	SelectedUniqueId = -1;
	SelfPeerPetType = -1;
	SelfPeerPetName.Empty();
	LastCallResult = 0;
	LastIncubateResult = 0;
	LastIncubateEcho = FAsdaPetIncubateEcho();   // ★ волна 3: номера ячеек чужой сессии — мусор
	// ★★★ ПРАВИЛО, ВВЕДЁННОЕ РЕВИЗИЕЙ ВОЛНЫ 3: КАЖДЫЙ «снимок тела пакета» и каждое поле,
	//   запомненное из пакета, СБРАСЫВАЕТСЯ НА СЕССИИ. Иначе номер ячейки, имя или вес чужой
	//   сессии переживут переподключение и попадут в нотис уже другого персонажа.
	//   `LastRenamePrevName` — поле волн 1/2 (нотис 101110 «[старое] → [новое]»), сбрасывать его
	//   забыли; ловится тем же правилом, поэтому чиню здесь же и сообщаю автору волны 2.
	LastRenamePrevName.Empty();
	LastFeedResult = 0;
	LastFeedEcho = FAsdaPetFeedEcho();       // ★ волна 6: номер ячейки и вес чужой сессии — мусор
	LastSlotUpEcho = FAsdaPetSlotUpEcho();   // ★ волна 6: то же правило, что у 6103/6149
	LastReleaseResult = 0;
	LastUnionResult = 0;
	LastUnionMissing = 0;   // ★ 27.09: диагностика 6113 чужой сессии — мусор
	LastRenameResult = 0;
	LastSlotUpResult = 0;
	LastLimitBreakResult = 0;
	LastLimitBreakEcho = FAsdaPetLimitBreakEcho();   // ★ волна 4: номера ячеек чужой сессии — мусор
	RxSinceReset = 0;
	UE_LOG(LogAsdaPet, Log, TEXT("[питомцы] полный сброс состояния (причина %d)"), (int32)Reason);
}

void UAsdaPetSubsystem::Tick(float DeltaTime)
{
	ASDA_PERF_SCOPE("Tick.Pet");   // ★ R4-c (14.11.2026): область замера (AsdaPerf.h; в Shipping пусто)
	// ★★★ ВОЛНА 8 (пункт 1 ревизии): ТАЙМАУТ ОЖИДАНИЯ 6101 — разбор у поля `CallLatch`.
	// ★ ИСПРАВЛЕНИЕ 11.10.2026 (дедуп №2, В2-A, S2): срок — стенные часы засова, ключ читается на тике (было — `DeltaTime`).
	if (CallLatch.IsExpired(AsdaPetUi::SendTimeoutSec()))
	{
		CallLatch.bTimedOut = true;   // ★ среднее 7: пометить гонку ДО снятия засова (`Disarm` признак не трогает)
		ClearCallRequestInFlight(ASDA_DEV_TEXT(TEXT("ответа 6101 нет за отведённое время (ключ asda.Pet.SendTimeoutSec)"), TEXT("ответа 6101 нет за отведённое время")));
	}

	if (Pending.Num() == 0) { return; }
	// Слив очереди — ВНЕ разбора кадров (закон `no-reentrant-net-callbacks`).
	TArray<FAsdaPetPendingRequest> Batch = MoveTemp(Pending);
	Pending.Reset();
	for (const FAsdaPetPendingRequest& R : Batch)
	{
		if (!SendRequest(R))
		{
			UE_LOG(LogAsdaPet, Warning, TEXT("[питомцы] запрос %d не ушёл: сеть не готова"), R.Opcode);
			// Признак «в полёте» держать нельзя — иначе кнопка призыва залипнет навсегда.
			if (R.Opcode == OP_PET_CALL_REQ) { ClearCallRequestInFlight(TEXT("запрос 6100 не ушёл: сеть не готова")); }
		}
	}
}

// ═════════════════════════════════════ ТАБЛИЦЫ ═════════════════════════════════════════════════
void UAsdaPetSubsystem::LoadTables()
{
	PetInfo.Reset();
	PetExp.Reset();
	IncubateRate.Reset();
	UnionRecipe.Reset();
	LimitBreak.Reset();
	EvoStage.Reset();

	if (const UDataTable* Dt = AsdaDt::Cached(kPetInfoTablePath))
	{
		for (const TPair<FName, uint8*>& It : Dt->GetRowMap())
		{
			const FAsdaPetInfoRow* R = reinterpret_cast<const FAsdaPetInfoRow*>(It.Value);
			if (R && R->PetIndex >= 0) { PetInfo.Add(R->PetIndex, *R); }
		}
	}
	else
	{
		UE_LOG(LogAsdaPet, Error, TEXT("[питомцы] нет таблицы %s (источник GameServer\\PetData_Info\\PetInfo_Table.txt). ")
			TEXT("Собрать: Tools\\asda_import_pet_server_tables.py"), kPetInfoTablePath);
	}

	if (const UDataTable* Dt = AsdaDt::Cached(kPetExpTablePath))
	{
		for (const TPair<FName, uint8*>& It : Dt->GetRowMap())
		{
			const FAsdaPetExpRow* R = reinterpret_cast<const FAsdaPetExpRow*>(It.Value);
			if (R && R->Level >= 0) { PetExp.Add(R->Level, *R); }
		}
	}
	else
	{
		UE_LOG(LogAsdaPet, Error, TEXT("[питомцы] нет таблицы %s (PetExp_Table.txt)"), kPetExpTablePath);
	}

	if (const UDataTable* Dt = AsdaDt::Cached(kIncubateRateTablePath))
	{
		for (const TPair<FName, uint8*>& It : Dt->GetRowMap())
		{
			const FAsdaPetIncubateRateRow* R = reinterpret_cast<const FAsdaPetIncubateRateRow*>(It.Value);
			if (R && R->Grade >= 0) { IncubateRate.Add(R->Grade, *R); }
		}
	}
	else
	{
		UE_LOG(LogAsdaPet, Warning, TEXT("[питомцы] нет таблицы %s (PetIncubateRate_Table.txt)"), kIncubateRateTablePath);
	}

	if (const UDataTable* Dt = AsdaDt::Cached(kUnionRecipeTablePath))
	{
		for (const TPair<FName, uint8*>& It : Dt->GetRowMap())
		{
			const FAsdaPetUnionRecipeRow* R = reinterpret_cast<const FAsdaPetUnionRecipeRow*>(It.Value);
			if (R && R->UnionIndex >= 0) { UnionRecipe.Add(R->UnionIndex, *R); }
		}
	}
	else
	{
		UE_LOG(LogAsdaPet, Warning, TEXT("[питомцы] нет таблицы %s (Petdetailevotable.txt)"), kUnionRecipeTablePath);
	}

	if (const UDataTable* Dt = AsdaDt::Cached(kLimitBreakTablePath))
	{
		for (const TPair<FName, uint8*>& It : Dt->GetRowMap())
		{
			// ★ КЛЮЧ — `RowIndex` (индекс массива): им сервер адресует таблицу через `MaxLevel`
			//   (`base + 16 * MaxLevel`). Колонка `Level` файла для поиска НЕ годится — она идёт
			//   1..10 и дала бы сдвиг ещё на строку (см. `AsdaPetTableRows.h`).
			const FAsdaPetLimitBreakRow* R = reinterpret_cast<const FAsdaPetLimitBreakRow*>(It.Value);
			if (R && R->RowIndex >= 0) { LimitBreak.Add(R->RowIndex, *R); }
		}
	}
	else
	{
		UE_LOG(LogAsdaPet, Warning, TEXT("[питомцы] нет таблицы %s (Petlimitbreaktable.txt)"), kLimitBreakTablePath);
	}

	if (const UDataTable* Dt = AsdaDt::Cached(kEvoStageTablePath))
	{
		for (const TPair<FName, uint8*>& It : Dt->GetRowMap())
		{
			const FAsdaPetEvoStageRow* R = reinterpret_cast<const FAsdaPetEvoStageRow*>(It.Value);
			if (R && R->EvoIndex >= 0) { EvoStage.Add(R->EvoIndex, *R); }
		}
	}
	else
	{
		UE_LOG(LogAsdaPet, Warning, TEXT("[питомцы] нет таблицы %s (клиентский Data\\PetEvolutionTable.bin)"), kEvoStageTablePath);
	}

	bTablesLoaded = (PetInfo.Num() > 0 && PetExp.Num() > 0);
}

const FAsdaPetInfoRow*         UAsdaPetSubsystem::GetPetInfo(int32 PetIndex) const       { return PetInfo.Find(PetIndex); }
const FAsdaPetExpRow*          UAsdaPetSubsystem::GetPetExp(int32 Level) const           { return PetExp.Find(Level); }
const FAsdaPetIncubateRateRow* UAsdaPetSubsystem::GetIncubateRate(int32 Grade) const     { return IncubateRate.Find(Grade); }
const FAsdaPetUnionRecipeRow*  UAsdaPetSubsystem::GetUnionRecipe(int32 UnionIndex) const { return UnionRecipe.Find(UnionIndex); }
const FAsdaPetLimitBreakRow*   UAsdaPetSubsystem::GetLimitBreak(int32 PetMaxLevel) const  { return LimitBreak.Find(PetMaxLevel); }
const FAsdaPetEvoStageRow*     UAsdaPetSubsystem::GetEvoStage(int32 EvoIndex) const      { return EvoStage.Find(EvoIndex); }

bool UAsdaPetSubsystem::GetLimitBreakThresholds(int32 PetMaxLevel, int32& Thr1, int32& Thr2, int32& Thr3) const
{
	Thr1 = Thr2 = Thr3 = 0;
	const FAsdaPetLimitBreakRow* R = GetLimitBreak(PetMaxLevel);
	if (!R) { return false; }
	// Дефолт — память сервера (дефект ПД4 воспроизводится 1:1, закон `never-invent-only-original`).
	//   Ключ `asda.Pet.LimitBreakFixTable 1` переключает на колонки файла; лечение — за владельцем.
	// ★ ВОЛНА 4, замечание ревизии (МЕЛК-6): при `…FixTable 1` строка берётся ТА ЖЕ, что и при 0 —
	//   по `RowIndex == PetMaxLevel`, потому что сервер адресует таблицу порядковым индексом
	//   (`base + 16 * MaxLevel`, GameServer.c:36725). У этой строки КОЛОНКА `Level` файла равна
	//   `MaxLevel + 1` (издатель нумеровал строки с единицы). Это НЕ сдвиг и не ошибка: ключом
	//   остаётся индекс, колонка `Level` справочная и в поиске не участвует ни в одном из режимов.
	if (CVarPetLimitBreakFixTable.GetValueOnGameThread() != 0)
	{
		Thr1 = R->Thr1; Thr2 = R->Thr2; Thr3 = R->Thr3;
	}
	else
	{
		Thr1 = R->ServerD0; Thr2 = R->ServerD1; Thr3 = R->ServerD2;
	}
	return true;
}

int32 UAsdaPetSubsystem::GetExpThreshold(int32 PetIdx, int32 Level) const
{
	// 1:1 формула сервера: `PetExpTable[Level + 1].Exp * PetInfo[PetIndex].ExpRateX10 / 10`
	//   (GameServer.c:35304). Целочисленное деление — как там (double только в лимит-брейке).
	const FAsdaPetExpRow*  E = GetPetExp(Level + 1);
	const FAsdaPetInfoRow* I = GetPetInfo(PetIdx);
	if (!E || !I) { return 0; }
	// ★ 1:1 С СЕРВЕРОМ, ВКЛЮЧАЯ ПЕРЕПОЛНЕНИЕ. Сервер множит в 32 битах и только потом переводит в
	//   double: `(int)((double)(Exp * ExpRateX10) / 10.0)` (GameServer.c:35304). Счёт в int64 дал
	//   бы на запирающей строке 11 (999999999 × 155) ДРУГОЕ число, чем у сервера. Умножение идёт
	//   через uint32: переполнение знакового типа в C++ не определено, а нужна именно обёртка по
	//   модулю 2^32, как на x86.
	const int32 Prod = (int32)((uint32)E->Exp * (uint32)I->ExpRateX10);
	return (int32)((double)Prod / 10.0);
}

// ═══════════════════════════════════ ДОСТУП К СЛОТАМ ═══════════════════════════════════════════
const FAsdaPetSlot* UAsdaPetSubsystem::FindByUniqueId(int32 UniqueId) const
{
	if (UniqueId == -1) { return nullptr; }
	for (const FAsdaPetSlot& S : Slots) { if (S.UniqueId == UniqueId) { return &S; } }
	return nullptr;
}

FAsdaPetSlot* UAsdaPetSubsystem::FindMutable(int32 UniqueId)
{
	if (UniqueId == -1) { return nullptr; }
	for (FAsdaPetSlot& S : Slots) { if (S.UniqueId == UniqueId) { return &S; } }
	return nullptr;
}

FAsdaPetSlot& UAsdaPetSubsystem::AddOrUpdate(const FAsdaPetSlot& InSlot)
{
	FAsdaPetSlot In = InSlot;
	// ★ ПРИЧУДА ЭТАЛОНА, ВОСПРОИЗВОДИТСЯ 1:1: порода 0 подменяется на 15.
	//   `petDataSelectInvItemById` делает `if (!a4) a4 = 15;` (npc_mob_pet.c:5510), где `a4` —
	//   третий аргумент, то есть `tableIndex` (порода). Через эту функцию у оригинала проходят ВСЕ
	//   добавления записи: список 6106 (network.c:25661), эволюция 6113 и лимит-брейк 6149
	//   (npc_mob_pet.c:7745). Почему так — видно в данных: строка 0 `PetInfo_Table` называется
	//   «none», то есть порода 0 у издателя нерабочая, и клиент затыкает её пятнадцатой.
	if (In.PetIndex == 0) { In.PetIndex = 15; }
	if (FAsdaPetSlot* Ex = FindMutable(In.UniqueId))
	{
		*Ex = In;
		return *Ex;
	}
	const int32 Idx = Slots.Add(In);
	return Slots[Idx];
}

int32 UAsdaPetSubsystem::GetOccupiedCount() const
{
	int32 N = 0;
	for (const FAsdaPetSlot& S : Slots) { if (S.IsFilled()) { ++N; } }
	return N;
}

void UAsdaPetSubsystem::SetSelectedUniqueId(int32 UniqueId)
{
	SelectedUniqueId = (UniqueId != -1 && FindByUniqueId(UniqueId)) ? UniqueId : -1;
}

UAsdaNetSubsystem* UAsdaPetSubsystem::Net() const
{
	UGameInstance* GI = GetGameInstance();
	return GI ? GI->GetSubsystem<UAsdaNetSubsystem>() : nullptr;
}

int32 UAsdaPetSubsystem::SelfCharId() const
{
	const UAsdaNetSubsystem* N = Net();
	return N ? N->SelectedCharId : 0;
}

bool UAsdaPetSubsystem::IsForSelf(int32 CharIdFromBody, int32 Opcode) const
{
	const int32 Mine = SelfCharId();
	// ★ До входа в мир своего charId ещё нет (0). Оригинал в этот момент пакетов не получает,
	//   но самопроверка гоняет приёмники без сессии — там сверять не с чем, принимаем.
	// ★ Дедуп №2, В2-A (N2): общая точка `AsdaSelf::IsSelfCharId` с ЯВНОЙ политикой «принять всё» (поведение то же).
	if (AsdaSelf::IsSelfCharId(Net(), CharIdFromBody, AsdaSelf::EAsdaUnknownSelf::AcceptAll)) { return true; }
	UE_LOG(LogAsdaPet, Warning,
		TEXT("[питомцы] пакет %d адресован charId=%d, а мой charId=%d — игнорирую (так же делает эталон)"),
		Opcode, CharIdFromBody, Mine);
	return false;
}
