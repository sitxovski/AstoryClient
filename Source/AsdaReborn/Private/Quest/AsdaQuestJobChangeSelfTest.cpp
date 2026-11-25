// Copyright (c) 2026 @sitxovski (Discord)
// ★ САМОТЕСТ 18.10.2026: `asda.Quest.SelfTestJobChange [delay=N] [quit]` — квесты появляются СРАЗУ после сдачи, смены профессии
//   и нового уровня (жалоба игроков «после 5-го уровня квесты не появляются; после перезапуска — появляются»).
//   Входы ТОЛЬКО штатные: пакеты 5047/5042/4036 — `UAsdaNetSubsystem::DebugFeedPacket` → `HandlePacket` → делегаты → подсистемы.
//   Проверки идут СИНХРОННО сразу после подачи пакета: фоновый пересчёт меток (`Asda.Quest.MarkRebuildPeriod`) на время прогона
//   выключен (0), тика между подачей и проверкой нет — значит, всё видимое пересчитано по событию.
//   Пары квестов — РЕАЛЬНЫЕ, из данных клиента (`RawAssets/Data/NPC`: NpcQuestScript.csv = .NSR, NpcSnq.json = SNQInfo,
//   SnqIct.json = SNQICT, EpisodeTable.json):
//     (а) цепочка: квест 19 → 20 у NPC corin (31, alpen): запись .NSR №12 «завершён 19 ∧ свободен 20, ур. 7», SNQICT 82;
//     (б) смена профессии: квест 21 (gllidio, «Солдат», ур. ≥ 5, класс 0) сдан с body[10..12] = 1/1/0 → квест 54 у joel (18):
//         .NSR №11 и SNQICT 160 «класс 1, ур. ≥ 24»; квест 22 у wendy (13, SNQICT 88 «класс 0») с выдачи уходит;
//     (в) уровень после сдачи: квест 91 → 1 у 181npc (181, alpen): .NSR №15 и SNQICT 38 «завершён 91 ∧ свободен 1, ур. ≥ 3»;
//         сдача при уровне 2 — квеста 1 нет, 4036 (уровень 3) — есть.
//   Контрольные пары: до ↔ после, ключ `asda.Quest.ApplyJobChange` 1 ↔ 0, ветка «только очки» ↔ «ни одной ветки».
//   Сервер не нужен: команда отказывает при живом соединении (`AsdaSelfTestKit::RefuseStart`).
#include "AsdaDevConsole.h"   // ASDA_DEV_CONSOLE — дев-команда вне Shipping

#if !UE_BUILD_SHIPPING   // самотесты в Shipping не входят
#include "AsdaNetSubsystem.h"
#include "AsdaBytes.h"
#include "AsdaSelf.h"
#include "AsdaCVarSave.h"     // ключи консоли самотеста — запомнить и вернуть (значение + SetBy)
#include "Test/AsdaSelfTestKit.h"
#include "Quest/AsdaQuestMarkerSubsystem.h"
#include "Npc/AsdaNpcDialogSubsystem.h"
#include "UI/AsdaChangeJobBannerSubsystem.h"
#include "UI/AsdaWindowSubsystem.h"
#include "UI/AsdaNotice.h"
#include "UI/AsdaInventorySubsystem.h"     // ★ 19.10.2026: самотест наград 5042 — сумка, золото
#include "UI/AsdaChatSubsystem.h"          // ★ журнал 2572 и плашка 221 — пробы строк наград
#include "UI/AsdaUiHelpers.h"              // AsdaUi::FormatThousands — ожидаемая сумма с разрядами
#include "UI/AsdaSkillWindowSubsystem.h"   // ★ 22.10.2026: самотест навыков после смены профессии — модель, сетка, клик узла
#include "UI/AsdaSkillDatabase.h"          // дерево и узел навыка (Grade / AbsSlot)
#include "UI/AsdaSkillUseSubsystem.h"      // ★ 26.10.2026: самотест 4-й профессии — применение навыка (тот же путь, что слот панели)
#include "Skill/AsdaSkillFxSubsystem.h"    // показ навыка: клип + эффекты (`IsSkillVisualActive`)
#include "World/AsdaPlayerCharacter.h"     // свой персонаж — кастер показа
#include "World/AsdaPlayerController.h"    // цель навыка (`TargetActor`) — щелчок по мобу
#include "Npc/AsdaMobActor.h"              // моб-цель навыка (появление 4015 / уход 4011)
#include "World/AsdaCoord.h"               // клетка своего персонажа
#include "AsdaTestPackets.h"               // тело появления моба 4013 (общий конструктор самотестов)
#include "Components/Button.h"             // кнопка 4-й профессии 12401 (`SOUL_SP_W_T`)
#include "Components/TextBlock.h"          // подпись кнопки 12401
#include "Misc/OutputDevice.h"             // перехват строк показа навыка (LogAsdaSkillFx)
#include "Misc/OutputDeviceRedirector.h"
#include "Components/Image.h"              // подсветка «доступен» узла (`Selectable`)
#include "Blueprint/UserWidget.h"          // окно навыков — живая геометрия (`GetCachedGeometry`)
#include "Items/AsdaItemTableSubsystem.h"  // имена наград: предмет / самоцвет / спец-предмет
#include "World/AsdaWorldSubsystem.h"
#include "World/AsdaLoadManagerSubsystem.h"
#include "AsdaDataTableCache.h"   // AsdaDt::RowByInt — строка SNQICT по ключу
#include "Engine/Engine.h"
#include "Engine/GameInstance.h"
#include "Engine/World.h"
#include "TimerManager.h"
#include "GameFramework/PlayerController.h"

DEFINE_LOG_CATEGORY_STATIC(LogAsdaQuestJobTest, Log, All);

struct FAsdaQuestJobChangeSelfTest
{
	static constexpr const TCHAR* kCmd = TEXT("asda.Quest.SelfTestJobChange");   // имя команды (и реестр ключей AsdaCVarSave)
	static int32 Ok;     // пройдено проверок
	static int32 Fail;   // провалено проверок

	// Снимок состояния до прогона — возвращается в `Finish`.
	struct FSaved
	{
		uint8 Level = 0;                                      // `SelfLevel`
		int64 Exp = -1;                                       // `SelfExp` (4036 его перепишет)
		int32 Job = -1, Cls = -1, Sub = -1, Pts = -1;         // `SelfJob/SelfClassId/SelfSubJob/SelfSkillPoints`
		TArray<FAsdaQuestSlot> Slots;                         // журнал квестов (12 слотов)
		TArray<uint8> DoneBits;                               // карта завершения (160 б)
		bool bSynced = false;                                 // пришло ли состояние квестов
		int32 DlgNpcId = -1;                                  // NPC окна диалога
		TArray<UAsdaNpcDialogSubsystem::FAsdaQuestPick> Give; // списки облачка 2406 (их перестроит `BuildQuestLists`)
		TArray<UAsdaNpcDialogSubsystem::FAsdaQuestPick> Take;
	};
	static FSaved Saved;
	static FDelegateHandle ProbeTurnIn;   // проба `OnQuestTurnInResult` (класс в момент рассылки 5042)
	static FDelegateHandle ProbeJob;      // проба `OnSelfJobChanged` (сколько раз пришло событие)
	static int32 ProbeClassAtTurnIn;      // `SelfClassId`, который увидел подписчик 5042
	static int32 JobEvents;               // число событий `OnSelfJobChanged`

	// Учёт проверки и строка лога.
	static void Check(bool bOk, const FString& What)
	{
		if (bOk) { ++Ok; } else { ++Fail; }
		UE_LOG(LogAsdaQuestJobTest, Log, TEXT("[квест-самотест] %s — %s"), bOk ? TEXT("ОК") : TEXT("ПРОВАЛ"), *What);
	}
	// Запись little-endian в тело.
	static void PutI16(TArray<uint8>& B, int32 At, int32 V) { B[At] = (uint8)(V & 0xFF); B[At + 1] = (uint8)((V >> 8) & 0xFF); }
	static void PutI32(TArray<uint8>& B, int32 At, int32 V) { for (int32 i = 0; i < 4; ++i) { B[At + i] = (uint8)(((uint32)V >> (8 * i)) & 0xFF); } }

	// Одна запись журнала для 5047: эпизод (≥ 2001), номер квеста, стадия; цели пустые.
	struct FSlotSpec { int32 Episode = 0; int32 QuestNo = -1; int32 Stage = 0; };
	// 5047 (688 б): 12 слотов по 44 б (`+0` i32 эпизод, `+4` u8 категория, `+5` i16 ячейка, `+7` u8 вид, `+8` i16 квест,
	//   `+10` i16 стадия, `+14` 5 пар целей), затем 160 б карты завершения с `+528` (раскладка `ApplyQuestStateBlob`).
	static TArray<uint8> Make5047(const TArray<FSlotSpec>& Specs, const TArray<int32>& Done)
	{
		TArray<uint8> B; B.SetNumZeroed(688);
		for (int32 i = 0; i < 12; ++i)
		{
			const int32 O = 44 * i;
			PutI16(B, O + 8, -1);
			for (int32 j = 0; j < 5; ++j) { PutI32(B, O + 14 + 6 * j, -1); }
			if (!Specs.IsValidIndex(i)) { continue; }
			PutI32(B, O + 0, Specs[i].Episode);
			B[O + 4] = 4;                         // категория «запись журнала» (как у `sub_45CA70`)
			PutI16(B, O + 5, i);
			B[O + 7] = 2;                         // «в работе»
			PutI16(B, O + 8, Specs[i].QuestNo);
			PutI16(B, O + 10, Specs[i].Stage);
		}
		for (int32 Q : Done) { if (Q >= 0 && Q < 160 * 8) { B[528 + (Q >> 3)] |= (uint8)(1u << (Q & 7)); } }
		return B;
	}
	// 5042 (501 б, раскладка `UAsdaNetSubsystem` case 5042): код шага @0, квест @1, стадия @3, тип завершения @5 (2 = квест
	//   закрыт), страница @6 (−1 — диалог не перерисовывать), профессия/класс/подкласс @10..@12, очки умений i16 @13.
	static TArray<uint8> Make5042(int32 QuestNo, int32 Stage, int32 Finish, int32 Job, int32 Cls, int32 Sub, int32 Pts)
	{
		TArray<uint8> B; B.SetNumZeroed(501);
		B[0] = 0; PutI16(B, 1, QuestNo); PutI16(B, 3, Stage); B[5] = (uint8)Finish; PutI16(B, 6, -1);
		B[10] = (uint8)Job; B[11] = (uint8)Cls; B[12] = (uint8)Sub; PutI16(B, 13, Pts);
		return B;
	}
	// 4036 (уровень): userIdx u16 @0, уровень u8 @2, опыт i64 @3, очки i16 @11. Стат-блок (117 б) не подаём — он не нужен.
	static TArray<uint8> Make4036(int32 UserIdx, int32 Level)
	{
		TArray<uint8> B; B.SetNumZeroed(13);
		PutI16(B, 0, UserIdx); B[2] = (uint8)Level;
		return B;
	}

	// Поля своего персонажа напрямую (так их держит сеть после 1017/4005).
	static void SetSelf(UAsdaNetSubsystem* Net, int32 Level, int32 Job, int32 Cls, int32 Sub, int32 Pts)
	{
		Net->SelfLevel = (uint8)Level; Net->SelfJob = Job; Net->SelfClassId = Cls; Net->SelfSubJob = Sub; Net->SelfSkillPoints = Pts;
	}
	// Квесты, которые NPC выдаёт прямо сейчас — тот же `BuildQuestLists`, что у облачка 2406 живого диалога.
	static TArray<int32> GiveOf(UAsdaNpcDialogSubsystem* Dlg, int32 NpcId)
	{
		Dlg->CurrentNpcId = NpcId;
		// ★ 06.11.2026: с 04.11 `BuildQuestLists` проверяет страницу ячейки в сценарии (`ResolveCellTalkId(CurrentNsr, …)`), а сценарий
		//   ставит открытие диалога (`AsdaNpcDialogSubsystem.cpp`: `CurrentNsr = NpcRec->NsrName`). Здесь окна нет — ставим так же,
		//   иначе пустое имя сценария гасит все строки и список пуст при любых условиях.
		const FString KeepNsr = Dlg->CurrentNsr;
		if (const auto* NpcRec = Dlg->FindNpcRow(NpcId)) { Dlg->CurrentNsr = NpcRec->NsrName; }
		Dlg->BuildQuestLists();
		Dlg->CurrentNsr = KeepNsr;
		TArray<int32> Out;
		for (const UAsdaNpcDialogSubsystem::FAsdaQuestPick& P : Dlg->GiveList) { Out.AddUnique(P.QuestId); }
		return Out;
	}
	// Запись .NSR по имени сценария и её индексу в файле → `CheckRecord` (единый вычислитель условий).
	static int32 RecOk(UAsdaQuestMarkerSubsystem* QM, const TCHAR* Nsr, int32 RecIndex)
	{
		QM->EnsureTables();
		if (const TArray<const FAsdaNpcQuestScriptRow*>* Rows = QM->ScriptByNsr.Find(FString(Nsr).ToLower()))
		{
			for (const FAsdaNpcQuestScriptRow* R : *Rows) { if (R && R->RecIndex == RecIndex) { return QM->CheckRecord(*R) ? 1 : 0; } }
		}
		return -1;   // записи нет — таблица не та
	}
	// Условие строки `SNQICT.Bin` (ключ облачка 2406) — `EvalSnqIctCond`, тот же вызов, что в `BuildQuestLists`, но без гейта
	//   «два сюжетных квеста сразу нельзя» (`HasActiveQuestOrReqByKey`): так видно именно условие класса/уровня.
	static int32 SnqOk(UAsdaQuestMarkerSubsystem* QM, int32 Key)
	{
		const FAsdaSnqIctRow* R = AsdaDt::RowByInt<FAsdaSnqIctRow>(TEXT("/Game/AsdaAssets/Data/NPC/DT_SnqIct.DT_SnqIct"), Key, TEXT("QuestJobTest"));
		return R ? (QM->EvalSnqIctCond(*R) ? 1 : 0) : -1;
	}
	// Строка со списком номеров квестов (для лога).
	static FString Join(const TArray<int32>& A)
	{
		FString S;
		for (int32 V : A) { S += FString::Printf(TEXT("%s%d"), S.IsEmpty() ? TEXT("") : TEXT(","), V); }
		return S.IsEmpty() ? TEXT("—") : S;
	}

	static void RunSync(UWorld* W);
	static void Finish(UWorld* W);

	// ★ 19.10.2026 — `asda.Quest.SelfTestRewardLines`: строки наград 5042 (`chatBuildTransactionNotice` виды 2/3/4/6, отказы 1…13).
	static constexpr const TCHAR* kCmdReward = TEXT("asda.Quest.SelfTestRewardLines");   // имя команды (и реестр ключей AsdaCVarSave)
	// Запись предмета в тело 5042: код, контейнер (+4), слот (+5), количество (+9).
	struct FItemSpec { int32 Code = 0; int32 Cont = 0; int32 Slot = 0; int32 Amount = 0; };
	// Снимок того, что трогает прогон наград (возвращается в `FinishRewardLines`).
	struct FRewardSaved
	{
		int32 GoldDisplay = -1;                               // `UAsdaInventorySubsystem::GoldDisplay` как есть (−1 — «не приходило»)
		int32 GoldCell = -1;                                  // количество в ячейке 20551 контейнера 2 (−1 — ячейки нет)
		int32 WeightCur = -1, WeightMax = -1;                 // `SelfWeightCur/SelfWeightMax`
		int32 Charm = 0; bool bCharmFromBlock = false;        // `SelfCharm/bSelfCharmFromBlock`
		TArray<FItemSpec> Cells;                              // ячейки, куда кладутся предметы (контейнер/слот)
		TArray<FAsdaItemInstance> CellData;                   // их содержимое до прогона
		TArray<FAsdaQuestSlot> Slots; TArray<uint8> DoneBits; bool bSynced = false;   // журнал квестов
		uint8 Level = 0; int32 Job = -1, Cls = -1, Sub = -1, Pts = -1;               // свой персонаж (г7/г8 его меняют)
		int32 DlgNpcId = -1;                                                       // NPC окна диалога (`GiveOf` его ставит)
		TArray<UAsdaNpcDialogSubsystem::FAsdaQuestPick> Give, Take;                // списки облачка 2406
	};
	static FRewardSaved RSaved;
	// 5042 с наградами: код шага @0, квест @1, стадия @3, тип завершения @5, страница −1 @6, эпизод слота i32 @15,
	//   до 6 записей предметов по 57 б с @147, вес i16 @489, золото i32 @491, обаяние i32 @495 (раскладка `AsdaQuestTurnInReward`).
	static TArray<uint8> Make5042R(int32 Step, int32 Finish, int32 SlotEpisode, const TArray<FItemSpec>& Items,
		int32 Weight, int32 Gold, int32 Charm)
	{
		TArray<uint8> B = Make5042(9001, 1, Finish, 0, 0, 0, 0);
		B[0] = (uint8)Step;
		PutI32(B, 15, SlotEpisode);
		for (int32 i = 0; i < Items.Num() && i < 6; ++i)
		{
			const int32 O = 147 + 57 * i;
			PutI32(B, O + 0, Items[i].Code);
			B[O + 4] = (uint8)Items[i].Cont;
			PutI16(B, O + 5, Items[i].Slot);
			PutI32(B, O + 9, Items[i].Amount);
		}
		PutI16(B, 489, Weight); PutI32(B, 491, Gold); PutI32(B, 495, Charm);
		return B;
	}
	static void RunRewardLines(UWorld* W);
	static void FinishRewardLines(UWorld* W);

	// ★ 22.10.2026 — `asda.Quest.SelfTestJobChangeSkills`: очко вкладывается в навык НОВОЙ профессии сразу после её смены квестом
	//   (жалоба игроков «после профессии лучника очки не вкладываются, помогает перезапуск»). Разбор — `Reports/
	//   jobchange_skills_2026-10-22/REPORT.md`. Сервер сразу за 5042 / 5052 со сменой профессии шлёт 5252 с базовыми навыками
	//   класса (`recvRewardQuestProcess@0x45CF90` / `recvPickupQuestItemToMonsterOrObject@0x45D580` → `sub_48A8C0@0x48A8C0`,
	//   список — `SkillTable_Info/ChangeJobAddSkillTable.txt`), а при открытии навыка вкачкой — 5252 с открывшимися
	//   (`sub_47E9B0@0x47E9B0`); эталон `Net_RecvLearnedSkillList@0xE56090` ставит каждой записи «можно вкачивать» = 1.
	static constexpr const TCHAR* kCmdSkills = TEXT("asda.Quest.SelfTestJobChangeSkills");   // имя команды (и реестр ключей AsdaCVarSave)
	// Снимок того, что трогает прогон навыков (возвращается в `FinishSkills`).
	struct FSkillSaved
	{
		uint8 Level = 0; int32 Job = -1, Cls = -1, Sub = -1, Pts = -1;               // свой персонаж
		TMap<int32, FAsdaLearnedSkill> Learned;                                    // модель окна навыков
		int32 WinPoints = -1, Tree = 0, Scroll = 0, Selected = -1, JobOv = -1, SubOv = -1;   // счётчик/вкладка/прокрутка/подмены окна
		bool bWinOpen = false;                                                     // было ли окно навыков открыто
		TArray<FAsdaQuestSlot> Slots; TArray<uint8> DoneBits; bool bSynced = false; // журнал квестов (5052 освобождает ячейку)
	};
	static FSkillSaved SSaved;
	static TArray<TArray<uint8>> Sent5253;   // тела перехваченных 5253 «вложить очко» (в сеть не уходят)
	static uint32 CapGen5253;                // поколение перехвата 5253 (0 — не взведён)
	// 5252 (`sub_48A8C0`: `sub_4C28F0(…, 5252, 105, …, 1)`): 35 записей по 3 б — i16 код, u8 уровень; пустые — код −1. Сервер
	//   шлёт его с паддингом конверта (`sub_5643E0`, `a8 = 1`: 0…19 б хвоста), поэтому тело длиннее 105 — хвост `Pad`.
	static TArray<uint8> Make5252(const TArray<int32>& Codes, int32 Level, const TArray<uint8>& Pad)
	{
		TArray<uint8> B; B.SetNumZeroed(105);
		for (int32 i = 0; i < 35; ++i) { PutI16(B, 3 * i, Codes.IsValidIndex(i) ? Codes[i] : -1); B[3 * i + 2] = (uint8)(Codes.IsValidIndex(i) ? Level : 0); }
		B.Append(Pad);
		return B;
	}
	// Запись дерева 34 б для 5251/6056 (`sub_476E10`: @0 i16 код, @2 u8 уровень, @3 u8 «можно вкачивать» = `sub_482130`).
	static TArray<uint8> MakeSkillRec34(int32 Code, int32 Level, int32 bAvail)
	{
		TArray<uint8> B; B.SetNumZeroed(34);
		PutI16(B, 0, Code); B[2] = (uint8)Level; B[3] = (uint8)bAvail;
		return B;
	}
	// 5254 (`Recv_SkillPoint_SkillActive@0x4774C0`, 111 б): @0 код (1 — успех), @1 i16 свободные очки, @3 i16 навык, @5 u8 уровень.
	static TArray<uint8> Make5254(int32 Code, int32 Points, int32 Skill, int32 Level)
	{
		TArray<uint8> B; B.SetNumZeroed(111);
		B[0] = (uint8)Code; PutI16(B, 1, Points); PutI16(B, 3, Skill); B[5] = (uint8)Level;
		return B;
	}
	// Показать узел навыка в открытом окне (вкладка по дереву навыка, прокрутка — чтобы ряд попал в 9 видимых); индекс узла или −1.
	static int32 ShowSkillSlot(UAsdaSkillWindowSubsystem* SW, int32 Code);
	static void RunSkills(UWorld* W);
	// Путь игрока (`ui-check-game-path-geometry`): через кадр после (в) — живая геометрия подсветки узла 932 в окне навыков.
	static void CheckSkillsGeometry(UWorld* W);
	static void FinishSkills(UWorld* W);
	static int32 GeoSlot;   // узел 932 после (в) (−1 — не дошли)

	// ★ 26.10.2026 — `asda.Quest.SelfTestJob4`: 4-я профессия видна и работает (разбор Reports/class_34_39/RESEARCH.md §6;
	//   сервер `sub_461010` пишет ступень в ПОДКЛАСС, основной класс остаётся 3/14/25; P87 выдаёт строку подкласса). Путь игрока:
	//   вход — блок 1017 (класс 14, подкласс 15) + список навыков 5251 (стартовые навыки строки 15 `ChangeJobAddSkillTable`),
	//   окно навыков → кнопка 12401 → дерево 4-й профессии (ветка 11), клик по узлу → 5253, ответ 5254, применение (`DispatchByKind`)
	//   → 5256, ответ сервера 5257 → показ (клип 4-й профессии + эффекты). Контроль: ключ 0, 3-я ступень, ремесленный подкласс,
	//   смена профессии 5042 (14 → подкласс 15) в мире. Перехват отправок — всех опкодов (в сеть ничего не уходит).
	static constexpr const TCHAR* kCmdJob4 = TEXT("asda.Quest.SelfTestJob4");   // имя команды (и реестр ключей AsdaCVarSave)
	// Перехватчик строк показа навыка («[навыки-визуал] навык …»).
	class FFxSink : public FOutputDevice
	{
	public:
		TArray<FString> Lines;   // строки категории LogAsdaSkillFx
		virtual void Serialize(const TCHAR* V, ELogVerbosity::Type, const FName& Category) override
		{
			if (!IsInGameThread()) { return; }   // ★ 05.11.2026: устройство синхронное (CanBeUsedOnMultipleThreads) — ловим только строки игрового потока: их пишет проверяемый код, читает самотест, замка у полей нет
			static const FName kFx(TEXT("LogAsdaSkillFx"));
			if (V && Category == kFx) { Lines.Add(FString(V)); }
		}
		virtual bool CanBeUsedOnAnyThread() const override { return true; }
		virtual bool CanBeUsedOnMultipleThreads() const override { return true; }   // ★ 05.11.2026: НЕбуферизованный список UE 5.8 — строка доходит в момент записи (иначе гонка с потоком логов: строка после RemoveOutputDevice/проверки)
	};
	static FFxSink FxSink;                    // перехват строк показа (на время прогона)
	static TArray<TArray<uint8>> Sent5256;    // перехваченные заявки применения 5256
	static TArray<uint8> SavedBlock1017;      // блок 1017 до прогона (возвращается в FinishJob4)
	static int32 SavedCharId;                 // `SelectedCharId` до прогона
	static uint8 SavedSoul;                   // `SelfSoulState` до прогона (1017 ставит его из `LastSelectMode`)
	static int32 CastCode;                    // навык 4-й профессии, ушедший 5256 (−1 — не применился)
	static int32 SavedWeaponSub;              // подтип оружия персонажа до прогона (−1000 — не трогали)
	static TUniquePtr<FAsdaNetSelfScope> J4Self;   // свой номер/уровень — возврат на любом выходе
	static int32 VitIdx;                        // свой userIdx записи ОМ (`VitalsByUserIdx`), INDEX_NONE — не трогали
	static bool bVitHad;                        // была ли запись до прогона
	static UAsdaNetSubsystem::FAsdaVitals VitWas;   // запись до прогона (офлайн ОМ = 0 — гейт 1018 не пустил бы применение)
	static TWeakObjectPtr<AActor> TargetWas;   // цель контроллера до прогона
	static bool bTargetSet;                     // цель подменялась
	static constexpr int32 kJ4MobRid = 30871;   // renderId моба-цели прогона (убирается штатным 4011)
	// 1017: 4 б префикса + блок 68 б (`Net_RecvCharSelect`): класс +28, подкласс +29, уровень +30, очки i16 +39, профессия +41.
	static TArray<uint8> Make1017(const TArray<uint8>& Block68, int32 Cls, int32 Sub, int32 Level, int32 Pts, int32 Job)
	{
		TArray<uint8> B; B.SetNumZeroed(72);
		if (Block68.Num() == 68) { FMemory::Memcpy(B.GetData() + 4, Block68.GetData(), 68); }
		B[4 + 28] = (uint8)Cls; B[4 + 29] = (uint8)Sub; B[4 + 30] = (uint8)Level; PutI16(B, 4 + 39, Pts); B[4 + 41] = (uint8)Job;
		return B;
	}
	static void RunJob4(UWorld* W);
	static void Job4Cast(UWorld* W);      // через тик после 5256: ответ 5257
	static void Job4Visual(UWorld* W);    // через тик после 5257: показ + живая геометрия кнопки + контроль
	static void FinishJob4(UWorld* W);
};
int32 FAsdaQuestJobChangeSelfTest::Ok = 0;
int32 FAsdaQuestJobChangeSelfTest::Fail = 0;
FAsdaQuestJobChangeSelfTest::FSaved FAsdaQuestJobChangeSelfTest::Saved;
FDelegateHandle FAsdaQuestJobChangeSelfTest::ProbeTurnIn;
FDelegateHandle FAsdaQuestJobChangeSelfTest::ProbeJob;
int32 FAsdaQuestJobChangeSelfTest::ProbeClassAtTurnIn = -100;
int32 FAsdaQuestJobChangeSelfTest::JobEvents = 0;
FAsdaQuestJobChangeSelfTest::FRewardSaved FAsdaQuestJobChangeSelfTest::RSaved;
FAsdaQuestJobChangeSelfTest::FSkillSaved FAsdaQuestJobChangeSelfTest::SSaved;
TArray<TArray<uint8>> FAsdaQuestJobChangeSelfTest::Sent5253;
uint32 FAsdaQuestJobChangeSelfTest::CapGen5253 = 0;
int32 FAsdaQuestJobChangeSelfTest::GeoSlot = -1;
FAsdaQuestJobChangeSelfTest::FFxSink FAsdaQuestJobChangeSelfTest::FxSink;
TArray<TArray<uint8>> FAsdaQuestJobChangeSelfTest::Sent5256;
TArray<uint8> FAsdaQuestJobChangeSelfTest::SavedBlock1017;
int32 FAsdaQuestJobChangeSelfTest::SavedCharId = 0;
uint8 FAsdaQuestJobChangeSelfTest::SavedSoul = 0;
int32 FAsdaQuestJobChangeSelfTest::CastCode = -1;
int32 FAsdaQuestJobChangeSelfTest::SavedWeaponSub = -1000;
TUniquePtr<FAsdaNetSelfScope> FAsdaQuestJobChangeSelfTest::J4Self;
int32 FAsdaQuestJobChangeSelfTest::VitIdx = INDEX_NONE;
bool FAsdaQuestJobChangeSelfTest::bVitHad = false;
UAsdaNetSubsystem::FAsdaVitals FAsdaQuestJobChangeSelfTest::VitWas;
TWeakObjectPtr<AActor> FAsdaQuestJobChangeSelfTest::TargetWas;
bool FAsdaQuestJobChangeSelfTest::bTargetSet = false;

void FAsdaQuestJobChangeSelfTest::RunSync(UWorld* W)
{
	UGameInstance* GI = W ? W->GetGameInstance() : nullptr;
	UAsdaNetSubsystem* Net = GI ? GI->GetSubsystem<UAsdaNetSubsystem>() : nullptr;
	UAsdaQuestMarkerSubsystem* QM = GI ? GI->GetSubsystem<UAsdaQuestMarkerSubsystem>() : nullptr;
	UAsdaNpcDialogSubsystem* Dlg = GI ? GI->GetSubsystem<UAsdaNpcDialogSubsystem>() : nullptr;
	UAsdaChangeJobBannerSubsystem* Job = GI ? GI->GetSubsystem<UAsdaChangeJobBannerSubsystem>() : nullptr;
	UAsdaWindowSubsystem* WM = GI ? GI->GetSubsystem<UAsdaWindowSubsystem>() : nullptr;
	UAsdaLoadManagerSubsystem* LM = GI ? GI->GetSubsystem<UAsdaLoadManagerSubsystem>() : nullptr;
	Ok = 0; Fail = 0;
	if (!Net || !QM || !Dlg || !Job || !WM || !LM)
	{
		Check(false, TEXT("нет одной из подсистем (сеть/метки/диалог/баннер/окна/загрузка)"));
		return;
	}
	auto Feed = [Net](int32 Op, const TArray<uint8>& B) { Net->DebugFeedPacket(Op, B); };   // штатный HandlePacket

	// ── снимок и подготовка ──
	Saved.Level = Net->SelfLevel; Saved.Exp = Net->SelfExp;
	Saved.Job = Net->SelfJob; Saved.Cls = Net->SelfClassId; Saved.Sub = Net->SelfSubJob; Saved.Pts = Net->SelfSkillPoints;
	Saved.Slots = QM->Slots; Saved.DoneBits = QM->DoneBits; Saved.bSynced = QM->bStateSynced;
	Saved.DlgNpcId = Dlg->CurrentNpcId; Saved.Give = Dlg->GiveList; Saved.Take = Dlg->TakeList;
	AsdaCVarSave::SetForTest(TEXT("Asda.Quest.MarkRebuildPeriod"), 0.f);   // фоновый пересчёт выключен: видно только событийный
	AsdaCVarSave::SetForTest(TEXT("asda.Quest.ApplyJobChange"), 1);
	Job->DebugSetManualClock(true);
	Job->DebugReset();
	WM->ClearMsgBoxQueue(TEXT("квест-самотест: старт"));
	ProbeClassAtTurnIn = -100; JobEvents = 0;
	ProbeTurnIn = Net->OnQuestTurnInResult.AddLambda([Net](int32, int32, int32, int32, int32, const TArray<uint8>&)
	{
		ProbeClassAtTurnIn = Net->SelfClassId;
	});
	ProbeJob = Net->OnSelfJobChanged.AddLambda([](int32, int32, int32, int32) { ++JobEvents; });

	const int32 Self = AsdaSelf::GetUserIdxOr(GI, 0);
	const bool bAlpen = (LM->CurrentMapId == 3);
	Check(Net->IsSelfUserIdx(Self) && bAlpen,
		FString::Printf(TEXT("свой userNum %d опознаётся, карта %d (alpen = 3: живые метки NPC corin/181npc проверяются)"), Self, LM->CurrentMapId));
	auto LiveMark = [QM, bAlpen](int32 NpcId) { return bAlpen ? QM->GetMarkForNpcId(NpcId) : -1; };

	// ═══ (а) ЦЕПОЧКА: сдача 19 → у corin сразу выдаётся 20 ═══
	{
		SetSelf(Net, 7, 0, 0, 0, 0);
		Feed(5047, Make5047({ { 2020, 19, 0 } }, { 90 }));   // 19 в работе (эпизод 2020), 90 закрыт — убирает шум записи №13
		const TArray<int32> G0 = GiveOf(Dlg, 31);
		const int32 M0 = QM->EvalMarkForNsr(TEXT("corin"));
		const int32 L0 = LiveMark(31);
		Check(QM->IsQuestStateSynced() && !G0.Contains(20) && M0 != 1 && L0 != 1,
			FString::Printf(TEXT("(а) до сдачи: corin выдаёт [%s] — 20 нет; метка %d (живая %d) ≠ 1"), *Join(G0), M0, L0));
		Feed(5042, Make5042(19, 0, 2, 0, 0, 0, 0));          // квест 19 закрыт целиком
		const TArray<int32> G1 = GiveOf(Dlg, 31);
		const int32 M1 = QM->EvalMarkForNsr(TEXT("corin"));
		const int32 L1 = LiveMark(31);
		Check(QM->IsQuestCompletedPublic(19) && RecOk(QM, TEXT("corin"), 12) == 1,
			TEXT("(а) после 5042: бит завершения 19 взведён, запись .NSR corin №12 «завершён 19 ∧ свободен 20» сошлась"));
		Check(G1.Contains(20), FString::Printf(TEXT("(а) диалог corin СРАЗУ выдаёт 20: [%s]"), *Join(G1)));
		Check(M1 == 1 && L1 == 1, FString::Printf(TEXT("(а) метка corin СРАЗУ = 1 (вычислитель %d, живая над NPC %d), без тика"), M1, L1));
	}

	// ═══ (б) СМЕНА ПРОФЕССИИ в 5042: квест 21 сдан, класс 0 → 1; квест 54 у joel (класс 1) — сразу ═══
	{
		SetSelf(Net, 24, 0, 0, 0, 0);
		Feed(5047, Make5047({ { 2022, 21, 1 } }, {}));       // 21 «Солдат» в работе (эпизод 2022)
		const TArray<int32> J0 = GiveOf(Dlg, 18);
		const int32 MJ0 = QM->EvalMarkForNsr(TEXT("joel"));
		Check(!J0.Contains(54) && MJ0 == 0 && RecOk(QM, TEXT("joel"), 11) == 0 && SnqOk(QM, 160) == 0,
			FString::Printf(TEXT("(б) до: класс 0 — joel выдаёт [%s] без 54, метка %d, .NSR №11 и SNQICT 160 «класс 1» не сходятся"), *Join(J0), MJ0));
		Check(RecOk(QM, TEXT("wendy"), 61) == 1 && SnqOk(QM, 88) == 1,
			TEXT("(б) до: у wendy условие квеста 22 «класс 0» сходится (.NSR №61, SNQICT 88; в облачке его держит гейт «сюжетный 21 в работе»)"));
		JobEvents = 0; ProbeClassAtTurnIn = -100;
		Feed(5042, Make5042(21, 1, 2, 1, 1, 0, 3));          // профессия 1, класс 1 (Fighter), подкласс 0, очки 3
		Check(Net->SelfJob == 1 && Net->SelfClassId == 1 && Net->SelfSubJob == 0 && Net->SelfSkillPoints == 3,
			FString::Printf(TEXT("(б) поля: профессия %d, класс %d, подкласс %d, очки %d (ждём 1/1/0/3)"),
				Net->SelfJob, Net->SelfClassId, Net->SelfSubJob, Net->SelfSkillPoints));
		Check(JobEvents == 1 && ProbeClassAtTurnIn == 1,
			FString::Printf(TEXT("(б) OnSelfJobChanged ×%d; подписчик 5042 уже видит класс %d (сеть применила ДО рассылки)"), JobEvents, ProbeClassAtTurnIn));
		Check(Job->DebugPhase() == 1 && Job->DebugShownJob() == 0 && Job->GetSelfJobGroup() == 1,
			FString::Printf(TEXT("(б) баннер 18500: фаза %d, надпись %d (Fighter), группа %d"), Job->DebugPhase(), Job->DebugShownJob(), Job->GetSelfJobGroup()));
		const FString Soldier = AsdaNotice::LocRaw(GI, 6133);
		Check(Job->DebugJobNoticeCount() == 1 && !Soldier.IsEmpty() && Job->DebugLastJobNoticeText().Contains(Soldier) && !Job->DebugLastJobNoticeMsgBox(),
			FString::Printf(TEXT("(б) нотис 20200 «%s» (имя loc 6133 «%s»), окна-сообщения нет (класс ≤ 25, подкласс 0)"),
				*Job->DebugLastJobNoticeText(), *Soldier));
		const TArray<int32> J1 = GiveOf(Dlg, 18);
		const TArray<int32> W1 = GiveOf(Dlg, 13);
		const int32 MJ1 = QM->EvalMarkForNsr(TEXT("joel"));
		Check(J1.Contains(54) && RecOk(QM, TEXT("joel"), 11) == 1 && SnqOk(QM, 160) == 1,
			FString::Printf(TEXT("(б) диалог joel СРАЗУ выдаёт 54 «класс 1»: [%s]"), *Join(J1)));
		Check(MJ1 == 1, FString::Printf(TEXT("(б) метка joel СРАЗУ = %d (была 0), без тика"), MJ1));
		Check(!W1.Contains(22) && RecOk(QM, TEXT("wendy"), 61) == 0 && SnqOk(QM, 88) == 0,
			FString::Printf(TEXT("(б) wendy больше не выдаёт 22 «класс 0» (условие не сходится): [%s]"), *Join(W1)));

		// Пара: ключ 0 — прежний путь (поля пишет баннер, события и нотиса 20200 нет).
		Job->DebugReset();
		SetSelf(Net, 24, 0, 0, 0, 0);
		Feed(5047, Make5047({ { 2022, 21, 1 } }, {}));
		AsdaCVarSave::SetForTest(TEXT("asda.Quest.ApplyJobChange"), 0);
		JobEvents = 0;
		Feed(5042, Make5042(21, 1, 2, 1, 1, 0, 3));
		Check(Net->SelfClassId == 1 && Net->SelfJob == 1 && JobEvents == 0 && Job->DebugJobNoticeCount() == 0 && Job->DebugPhase() == 1,
			FString::Printf(TEXT("(б) ключ 0: класс %d записан баннером, событий %d, нотисов 20200 %d, баннер фаза %d (пара)"),
				Net->SelfClassId, JobEvents, Job->DebugJobNoticeCount(), Job->DebugPhase()));
		AsdaCVarSave::SetForTest(TEXT("asda.Quest.ApplyJobChange"), 1);
		Job->DebugReset();

		// Ветка «только очки» (`updateFlags[4]`) и контрольная «ни одной ветки» (body[11] ≠ 0 без профессии).
		SetSelf(Net, 24, 1, 1, 0, 3);
		JobEvents = 0;
		Feed(5042, Make5042(9001, 1, 1, 0, 0, 0, 7));
		Check(Net->SelfSkillPoints == 7 && Net->SelfClassId == 1 && JobEvents == 0,
			FString::Printf(TEXT("(б) только очки: body[10..12] = 0, очки 7 → %d, класс не тронут (%d), событий %d"),
				Net->SelfSkillPoints, Net->SelfClassId, JobEvents));
		Feed(5042, Make5042(9001, 1, 1, 0, 1, 0, 9));
		Check(Net->SelfSkillPoints == 7 && JobEvents == 0,
			FString::Printf(TEXT("(б) body[10] = 0, body[11] = 1: ни одна ветка эталона — очки остались %d (пара)"), Net->SelfSkillPoints));
	}

	// ═══ (в) УРОВЕНЬ ПОСЛЕ СДАЧИ: квест 91 сдан на уровне 2 → квест 1 (ур. ≥ 3) появляется по 4036 ═══
	{
		SetSelf(Net, 2, 1, 1, 0, 0);
		Feed(5047, Make5047({ { 2092, 91, 1 } }, { 207 }));  // 91 в работе (эпизод 2092); 207 закрыт — убирает шум записи №14
		Feed(5042, Make5042(91, 1, 2, 0, 0, 0, 0));
		const TArray<int32> G0 = GiveOf(Dlg, 181);
		const int32 M0 = QM->EvalMarkForNsr(TEXT("181npc"));
		const int32 L0 = LiveMark(181);
		Check(QM->IsQuestCompletedPublic(91) && !G0.Contains(1) && RecOk(QM, TEXT("181npc"), 15) == 0 && M0 != 1 && L0 != 1,
			FString::Printf(TEXT("(в) 91 сдан на уровне 2: 181npc выдаёт [%s] без 1, запись №15 «ур. ≥ 3» не сходится, метка %d (живая %d)"),
				*Join(G0), M0, L0));
		Feed(4036, Make4036(Self, 3));
		const TArray<int32> G1 = GiveOf(Dlg, 181);
		const int32 M1 = QM->EvalMarkForNsr(TEXT("181npc"));
		const int32 L1 = LiveMark(181);
		int32 Lv = 0, C = 0, S = 0;
		QM->ReadSelfLevelClass(Lv, C, S);
		Check(Net->SelfLevel == 3 && Lv == 3,
			FString::Printf(TEXT("(в) 4036: SelfLevel %d, вычислитель условий видит уровень %d (без перезахода)"), (int32)Net->SelfLevel, Lv));
		Check(G1.Contains(1) && RecOk(QM, TEXT("181npc"), 15) == 1,
			FString::Printf(TEXT("(в) диалог 181npc СРАЗУ выдаёт 1: [%s]"), *Join(G1)));
		Check(M1 == 1 && L1 == 1, FString::Printf(TEXT("(в) метка 181npc СРАЗУ = 1 (вычислитель %d, живая над NPC %d), без тика"), M1, L1));
	}
}

void FAsdaQuestJobChangeSelfTest::Finish(UWorld* W)
{
	UGameInstance* GI = W ? W->GetGameInstance() : nullptr;
	UAsdaNetSubsystem* Net = GI ? GI->GetSubsystem<UAsdaNetSubsystem>() : nullptr;
	UAsdaQuestMarkerSubsystem* QM = GI ? GI->GetSubsystem<UAsdaQuestMarkerSubsystem>() : nullptr;
	UAsdaNpcDialogSubsystem* Dlg = GI ? GI->GetSubsystem<UAsdaNpcDialogSubsystem>() : nullptr;
	UAsdaChangeJobBannerSubsystem* Job = GI ? GI->GetSubsystem<UAsdaChangeJobBannerSubsystem>() : nullptr;
	UAsdaWindowSubsystem* WM = GI ? GI->GetSubsystem<UAsdaWindowSubsystem>() : nullptr;
	if (Net)
	{
		Net->OnQuestTurnInResult.Remove(ProbeTurnIn);
		Net->OnSelfJobChanged.Remove(ProbeJob);
		Net->SelfLevel = Saved.Level; Net->SelfExp = Saved.Exp;
		Net->SelfJob = Saved.Job; Net->SelfClassId = Saved.Cls; Net->SelfSubJob = Saved.Sub; Net->SelfSkillPoints = Saved.Pts;
	}
	if (QM)
	{
		QM->Slots = Saved.Slots; QM->DoneBits = Saved.DoneBits; QM->bStateSynced = Saved.bSynced;
		QM->bDirty = true;
		QM->RebuildMarks();
	}
	if (Dlg) { Dlg->CurrentNpcId = Saved.DlgNpcId; Dlg->GiveList = Saved.Give; Dlg->TakeList = Saved.Take; }
	if (Job) { Job->DebugReset(); Job->DebugSetManualClock(false); }
	if (WM) { WM->ClearMsgBoxQueue(TEXT("квест-самотест: конец")); }
	AsdaCVarSave::RestoreAll(kCmd);
	AsdaSelfTestKit::Summary(LogAsdaQuestJobTest, TEXT("[квест-самотест]"), Ok, Fail, TEXT(""), false);
}

// ★ 19.10.2026 — СТРОКИ НАГРАД 5042 (`chatBuildTransactionNotice@0xD9FC70`, разбор у `AsdaQuestTurnInReward` в диалоге NPC).
//   Ожидания — по эталону: вид 2 «Получено [ss1] золота.» (разность С разрядами), вид 3 6019, вид 4 «Получена награда: [ss1].»
//   (имя: предмет → самоцвет → спец-предмет), вид 6 «%d Повышено обаяние.»; всё — журнал 2572 + плашка 221 цветом −256;
//   порядок 2 → 3 → 4 → 6; отказ 1…13 — окно ONEBUT (0, 24); ключ `asda.Quest.TurnInRewardLines 0` — ничего из этого.
void FAsdaQuestJobChangeSelfTest::RunRewardLines(UWorld* W)
{
	UGameInstance* GI = W ? W->GetGameInstance() : nullptr;
	UAsdaNetSubsystem* Net = GI ? GI->GetSubsystem<UAsdaNetSubsystem>() : nullptr;
	UAsdaQuestMarkerSubsystem* QM = GI ? GI->GetSubsystem<UAsdaQuestMarkerSubsystem>() : nullptr;
	UAsdaInventorySubsystem* Inv = GI ? GI->GetSubsystem<UAsdaInventorySubsystem>() : nullptr;
	UAsdaItemTableSubsystem* IT = GI ? GI->GetSubsystem<UAsdaItemTableSubsystem>() : nullptr;
	UAsdaChatSubsystem* Chat = GI ? GI->GetSubsystem<UAsdaChatSubsystem>() : nullptr;
	UAsdaWindowSubsystem* WM = GI ? GI->GetSubsystem<UAsdaWindowSubsystem>() : nullptr;
	Ok = 0; Fail = 0;
	if (!Net || !QM || !Inv || !IT || !Chat || !WM)
	{
		Check(false, TEXT("нет одной из подсистем (сеть/метки/сумка/таблица предметов/чат/окна)"));
		return;
	}
	auto Feed = [Net](const TArray<uint8>& B) { Net->DebugFeedPacket(5042, B); };   // штатный HandlePacket
	const FColor kYellow((uint32)0xFFFFFF00);   // −256

	// ── снимок ──
	RSaved = FRewardSaved();
	RSaved.GoldDisplay = Inv->GoldDisplay;
	for (int32 S = 0; S < 60; ++S) { const FAsdaItemInstance* I = Inv->GetCell(2, S); if (I && I->ItemCode == UAsdaInventorySubsystem::kGoldItemCode) { RSaved.GoldCell = I->Amount; break; } }
	RSaved.WeightCur = Net->SelfWeightCur; RSaved.WeightMax = Net->SelfWeightMax;
	RSaved.Charm = Net->SelfCharm; RSaved.bCharmFromBlock = Net->bSelfCharmFromBlock;
	RSaved.Slots = QM->Slots; RSaved.DoneBits = QM->DoneBits; RSaved.bSynced = QM->bStateSynced;
	RSaved.Level = Net->SelfLevel; RSaved.Job = Net->SelfJob; RSaved.Cls = Net->SelfClassId; RSaved.Sub = Net->SelfSubJob; RSaved.Pts = Net->SelfSkillPoints;
	if (UAsdaNpcDialogSubsystem* D0 = GI->GetSubsystem<UAsdaNpcDialogSubsystem>()) { RSaved.DlgNpcId = D0->CurrentNpcId; RSaved.Give = D0->GiveList; RSaved.Take = D0->TakeList; }
	AsdaCVarSave::SetForTest(TEXT("asda.Quest.TurnInRewardLines"), 1);
	AsdaCVarSave::SetForTest(TEXT("asda.Quest.ApplyJobChange"), 1);
	AsdaCVarSave::SetForTest(TEXT("Asda.Quest.MarkRebuildPeriod"), 0.f);
	WM->ClearMsgBoxQueue(TEXT("самотест наград: старт"));

	// ── коды наград из данных клиента: предмет, самоцвет без записи предмета, спец-предмет без двух первых ──
	const int32 kItem = 51784;   // «Нагрудник элитного оленя» (item_list)
	int32 kSowel = -1, kSpecial = -1;
	for (int32 C = 7001; C < 7400 && kSowel < 0; ++C)
	{
		FAsdaSowellRow Sw;
		if (!IT->GetItem(C) && IT->GetSowell(C, Sw) && !Sw.Name.IsEmpty()) { kSowel = C; }
	}
	for (int32 C = 1; C < 3000 && kSpecial < 0; ++C)
	{
		FAsdaSowellRow Sw; FAsdaSpecialItemlistRow Sp; FAsdaSpecialitemTooltiplistRow Tip;
		if (!IT->GetItem(C) && !IT->GetSowell(C, Sw) && IT->GetSpecialItem(C, Sp) && IT->GetSpecialItemTooltip(C, Tip) && !Tip.Name.IsEmpty()) { kSpecial = C; }
	}
	FString NameItem = IT->GetItemName(kItem), NameSowel, NameSpecial;
	{
		FAsdaSowellRow Sw; FAsdaSpecialitemTooltiplistRow Tip;
		if (kSowel > 0 && IT->GetSowell(kSowel, Sw)) { NameSowel = Sw.Name; }
		if (kSpecial > 0 && IT->GetSpecialItemTooltip(kSpecial, Tip)) { NameSpecial = Tip.Name; }
	}
	Check(IT->GetItem(kItem) && !NameItem.IsEmpty() && kSowel > 0 && kSpecial > 0,
		FString::Printf(TEXT("данные: предмет %d «%s», самоцвет %d «%s», спец-предмет %d «%s»"),
			kItem, *NameItem, kSowel, *NameSowel, kSpecial, *NameSpecial));

	// ── свободные ячейки под предметы (контейнер 1 — предмет, 2 — самоцвет и спец-предмет), снимок их содержимого ──
	auto FreeCell = [Inv](int32 Cont, int32 Num, int32 Skip) -> int32
	{
		for (int32 S = Num - 1; S >= 0; --S)
		{
			const FAsdaItemInstance* I = Inv->GetCell(Cont, S);
			if (I && I->ItemCode <= 0 && S != Skip) { return S; }
		}
		return -1;
	};
	const int32 S1 = FreeCell(1, 30, -1), S2 = FreeCell(2, 60, -1), S3 = FreeCell(2, 60, S2);
	RSaved.Cells = { { 0, 1, S1, 0 }, { 0, 2, S2, 0 }, { 0, 2, S3, 0 } };
	for (const FItemSpec& C : RSaved.Cells) { const FAsdaItemInstance* I = Inv->GetCell(C.Cont, C.Slot); RSaved.CellData.Add(I ? *I : FAsdaItemInstance()); }
	Check(S1 >= 0 && S2 >= 0 && S3 >= 0, FString::Printf(TEXT("свободные ячейки: 1/%d, 2/%d, 2/%d"), S1, S2, S3));
	if (S1 < 0 || S2 < 0 || S3 < 0 || kSowel < 0 || kSpecial < 0) { return; }

	const FString GoldLine  = AsdaNotice::LocSlot(GI, 20198, AsdaUi::FormatThousands(12345));
	const FString SlotLine  = AsdaNotice::LocRaw(GI, 6019);
	const FString ItemLine  = AsdaNotice::LocSlot(GI, 20199, NameItem);
	const FString SowelLine = AsdaNotice::LocSlot(GI, 20199, NameSowel);
	const FString SpecLine  = AsdaNotice::LocSlot(GI, 20199, NameSpecial);
	const FString CharmText = AsdaNotice::LocRaw(GI, 6025);
	const FString CharmLine = FString::Printf(TEXT("5 %s"), *CharmText);
	Check(GoldLine.Contains(TEXT("12,345")) && !GoldLine.Contains(TEXT("[ss1]")) && ItemLine.Contains(NameItem) && !SlotLine.IsEmpty() && !CharmText.IsEmpty(),
		FString::Printf(TEXT("тексты: «%s» / «%s» / «%s» / «%s»"), *GoldLine, *SlotLine, *ItemLine, *CharmLine));
	auto Cnt = [Chat](const FString& S) { return Chat->DebugCountSystemLines(S); };
	auto Yel = [Chat, kYellow](const FString& S) { FColor C(0, 0, 0, 0); return Chat->DebugFindLastSystemLineColor(S, C) && C == kYellow; };
	auto War = [Chat, kYellow](const FString& S) { FColor C(0, 0, 0, 0); return Chat->DebugCountWarLines(S, &C) > 0 && C == kYellow; };

	// ═══ (г1) полная сдача: золото 1 000 → 13 345, слот, 3 предмета, обаяние 10 → 15, вес 777 ═══
	{
		Inv->SetGoldDisplay(1000);
		Net->SelfCharm = 10; Net->bSelfCharmFromBlock = true;
		const int32 G0 = Cnt(GoldLine), L0 = Cnt(SlotLine), I0 = Cnt(ItemLine), W0 = Cnt(SowelLine), P0 = Cnt(SpecLine), C0 = Cnt(CharmLine);
		Feed(Make5042R(0, 1, 2020, { { kItem, 1, S1, 1 }, { kSowel, 2, S2, 1 }, { kSpecial, 2, S3, 3 } }, 777, 13345, 15));
		Check(Cnt(GoldLine) == G0 + 1 && Yel(GoldLine), FString::Printf(TEXT("(г1) вид 2: «%s» ×1 в журнале, цвет −256"), *GoldLine));
		Check(Cnt(SlotLine) == L0 + 1 && Yel(SlotLine), FString::Printf(TEXT("(г1) вид 3: «%s» ×1, −256"), *SlotLine));
		Check(Cnt(ItemLine) == I0 + 1 && Cnt(SowelLine) == W0 + 1 && Cnt(SpecLine) == P0 + 1 && Yel(ItemLine) && Yel(SowelLine) && Yel(SpecLine),
			FString::Printf(TEXT("(г1) вид 4: «%s», «%s», «%s» — по одной, −256"), *ItemLine, *SowelLine, *SpecLine));
		Check(Cnt(CharmLine) == C0 + 1 && Yel(CharmLine), FString::Printf(TEXT("(г1) вид 6: «%s» ×1, −256"), *CharmLine));
		const int32 iG = Chat->DebugLastSystemLineIndex(GoldLine), iL = Chat->DebugLastSystemLineIndex(SlotLine);
		const int32 iI = Chat->DebugLastSystemLineIndex(ItemLine), iW = Chat->DebugLastSystemLineIndex(SowelLine);
		const int32 iP = Chat->DebugLastSystemLineIndex(SpecLine), iC = Chat->DebugLastSystemLineIndex(CharmLine);
		Check(iG >= 0 && iG < iL && iL < iI && iI < iW && iW < iP && iP < iC,
			FString::Printf(TEXT("(г1) порядок эталона 2 → 3 → 4(×3) → 6: индексы %d < %d < %d < %d < %d < %d"), iG, iL, iI, iW, iP, iC));
		Check(War(CharmLine) && War(SpecLine), TEXT("(г1) плашка 221: последние строки (спец-предмет, обаяние) — там же, −256"));
		const FAsdaItemInstance* A = Inv->GetCell(1, S1); const FAsdaItemInstance* B = Inv->GetCell(2, S2); const FAsdaItemInstance* C = Inv->GetCell(2, S3);
		Check(A && A->ItemCode == kItem && B && B->ItemCode == kSowel && C && C->ItemCode == kSpecial && C->Amount == 3,
			TEXT("(г1) предметы легли в сумку по полям записи (`OnRecv_ItemUpdate`), количество спец-предмета 3"));
		Check(Inv->GetGold() == 13345 && Net->SelfWeightCur == 777 && Net->SelfCharm == 15 && WM->GetMsgBoxQueueNum() == 0,
			FString::Printf(TEXT("(г1) золото %d (13 345), вес %d (777), обаяние %d (15), окон-сообщений %d (0)"),
				Inv->GetGold(), Net->SelfWeightCur, Net->SelfCharm, WM->GetMsgBoxQueueNum()));
	}

	// ═══ (г2) только золото: плашка 221; (г3) золото не выросло — строки нет; обаяние без роста — текст без числа ═══
	{
		const FString Gold2 = AsdaNotice::LocSlot(GI, 20198, AsdaUi::FormatThousands(1000000));
		Inv->SetGoldDisplay(5);
		const int32 G0 = Cnt(Gold2);
		Feed(Make5042R(0, 1, 0, {}, 0, 1000005, 0));
		Check(Cnt(Gold2) == G0 + 1 && War(Gold2) && Inv->GetGold() == 1000005,
			FString::Printf(TEXT("(г2) «%s» в журнале и в плашке 221 цветом −256, золото %d"), *Gold2, Inv->GetGold()));
		const int32 N0 = Cnt(TEXT("золота"));
		Feed(Make5042R(0, 1, 0, {}, 0, 400, 0));
		Check(Cnt(TEXT("золота")) == N0 && Inv->GetGold() == 1000005,
			FString::Printf(TEXT("(г3) золото 400 < %d: разность ≤ 0 — строки нет, поле не тронуто"), Inv->GetGold()));
		Net->SelfCharm = 15;
		const int32 A0 = Cnt(CharmText), B0 = Cnt(CharmLine);
		Feed(Make5042R(0, 1, 0, {}, 0, 0, 15));
		Check(Cnt(CharmText) == A0 + 1 && Cnt(CharmLine) == B0 && Net->SelfCharm == 15,
			FString::Printf(TEXT("(г3) обаяние 15 → 15: строка «%s» без числа (`sprintf(\"%%s\")`)"), *CharmText));
		const int32 S0 = Cnt(SlotLine);
		Feed(Make5042R(0, 2, 2020, {}, 0, 0, 0));
		Check(Cnt(SlotLine) == S0, TEXT("(г3) тип завершения 2 (квест закрыт) при body[15] > 0 — строки 6019 нет (`body[5] != 2`)"));
	}

	// ═══ (г4) отказ сервера: код шага 3 → окно 6176 (0, 24), наград нет ═══
	{
		WM->ClearMsgBoxQueue(TEXT("самотест наград: г4"));
		Inv->SetGoldDisplay(10);
		const int32 N0 = Cnt(TEXT("золота"));
		Feed(Make5042R(3, 1, 2020, {}, 0, 999, 0));
		const FString Want = AsdaNotice::LocRaw(GI, 6176);
		Check(WM->IsMsgBoxQueued(0, 24) && WM->GetMsgBoxQueueTopText().Contains(Want) && Cnt(TEXT("золота")) == N0 && Inv->GetGold() == 10,
			FString::Printf(TEXT("(г4) код 3 → окно (0, 24) «%s», строк наград нет, золото %d"), *WM->GetMsgBoxQueueTopText(), Inv->GetGold()));
		WM->ClearMsgBoxQueue(TEXT("самотест наград: г4 конец"));
		Feed(Make5042R(14, 1, 0, {}, 0, 0, 0));
		Check(WM->GetMsgBoxQueueNum() == 0, TEXT("(г4) код 14 — `default` эталона: молча"));
	}

	// 5052 «квест завершён» (508 б): objType @4 = 1, признак завершения i32 @13 > 0, запись слота @22 (эпизод), блок наград с @17:
	//   очки умений i16 @20, предметы @154+57i, вес i16 @496, золото i32 @498, обаяние i32 @502 (`charApplyFullStatusPacket`).
	auto Make5052R = [](int32 Points, const TArray<FItemSpec>& Items, int32 Weight, int32 Gold, int32 Charm)
	{
		TArray<uint8> B; B.SetNumZeroed(508);
		PutI16(B, 0, 9001);   // номер квеста (`iQuestIndex`) — вне карты завершения 160 б, бит не ставится
		B[4] = 1; PutI32(B, 13, 1); PutI32(B, 22, 2020); PutI16(B, 27, 0); PutI16(B, 30, 9001); PutI16(B, 32, 1);
		PutI16(B, 20, Points);
		for (int32 i = 0; i < Items.Num() && i < 6; ++i)
		{
			const int32 O = 154 + 57 * i;
			PutI32(B, O + 0, Items[i].Code); B[O + 4] = (uint8)Items[i].Cont; PutI16(B, O + 5, Items[i].Slot); PutI32(B, O + 9, Items[i].Amount);
		}
		PutI16(B, 496, Weight); PutI32(B, 498, Gold); PutI32(B, 502, Charm);
		return B;
	};

	// ═══ (г6) ЗАВЕРШЕНИЕ по 5052: золото 1 000 → 3 000, предмет, очки 4, обаяние 10 → 12, вес 555; строки 6019 нет ═══
	{
		Inv->ClearCell(1, S1);
		Inv->SetGoldDisplay(1000);
		Net->SelfCharm = 10; Net->SelfSkillPoints = 0;
		const FString Gold6  = AsdaNotice::LocSlot(GI, 20198, AsdaUi::FormatThousands(2000));
		const FString Pts6   = AsdaNotice::LocRaw(GI, 6024);
		const FString Charm6 = FString::Printf(TEXT("2 %s"), *CharmText);
		const int32 G0 = Cnt(Gold6), I0 = Cnt(ItemLine), P0 = Cnt(Pts6), C0 = Cnt(Charm6), L0 = Cnt(SlotLine);
		Net->DebugFeedPacket(5052, Make5052R(4, { { kItem, 1, S1, 1 } }, 555, 3000, 12));
		Check(Cnt(Gold6) == G0 + 1 && Yel(Gold6),   // плашку 221 золото уже покинуло: 4 строки при лимите 3
			FString::Printf(TEXT("(г6) 5052 вид 2: «%s» ×1, −256"), *Gold6));
		Check(Cnt(ItemLine) == I0 + 1 && Cnt(Pts6) == P0 + 1 && Cnt(Charm6) == C0 + 1 && Yel(ItemLine) && Yel(Pts6) && Yel(Charm6),
			FString::Printf(TEXT("(г6) 5052 виды 4/5/6: «%s», «%s», «%s» — по одной, −256"), *ItemLine, *Pts6, *Charm6));
		Check(Cnt(SlotLine) == L0, TEXT("(г6) 5052: строки 6019 нет (в `charApplyFullStatusPacket` вида 3 нет, ветка завершения её не шлёт)"));
		const int32 iG = Chat->DebugLastSystemLineIndex(Gold6), iI = Chat->DebugLastSystemLineIndex(ItemLine);
		const int32 iP = Chat->DebugLastSystemLineIndex(Pts6), iC = Chat->DebugLastSystemLineIndex(Charm6);
		Check(iG >= 0 && iG < iI && iI < iP && iP < iC,
			FString::Printf(TEXT("(г6) порядок эталона 2 → 4 → 5 → 6: индексы %d < %d < %d < %d"), iG, iI, iP, iC));
		Check(War(Charm6), TEXT("(г6) плашка 221: обаяние там же, −256"));
		const FAsdaItemInstance* A = Inv->GetCell(1, S1);
		Check(A && A->ItemCode == kItem && Inv->GetGold() == 3000 && Net->SelfWeightCur == 555 && Net->SelfCharm == 12 && Net->SelfSkillPoints == 4,
			FString::Printf(TEXT("(г6) 5052: предмет в 1/%d, золото %d (3 000), вес %d (555), обаяние %d (12), очки %d (4)"),
				S1, Inv->GetGold(), Net->SelfWeightCur, Net->SelfCharm, Net->SelfSkillPoints));
	}

	// ═══ (г7) ЗАВЕРШЕНИЕ 19 по 5052 → у corin (31) СРАЗУ выдаётся 20 (.NSR №12 «завершён 19 ∧ свободен 20, ур. 7») ═══
	UAsdaNpcDialogSubsystem* Dlg = GI->GetSubsystem<UAsdaNpcDialogSubsystem>();
	UAsdaChangeJobBannerSubsystem* JobB = GI->GetSubsystem<UAsdaChangeJobBannerSubsystem>();
	if (Dlg && JobB)
	{
		JobB->DebugSetManualClock(true);
		JobB->DebugReset();
		SetSelf(Net, 7, 0, 0, 0, 0);
		Net->DebugFeedPacket(5047, Make5047({ { 2020, 19, 0 } }, { 90 }));   // 19 в работе (эпизод 2020, ячейка 0)
		const TArray<int32> G0 = GiveOf(Dlg, 31);
		TArray<uint8> B = Make5052R(0, {}, 0, 0, 0);
		PutI16(B, 0, 19); PutI32(B, 22, 2020); PutI16(B, 27, 0); PutI16(B, 30, 19); PutI16(B, 32, 0);
		Net->DebugFeedPacket(5052, B);
		const TArray<int32> G1 = GiveOf(Dlg, 31);
		Check(!G0.Contains(20) && QM->IsQuestCompletedPublic(19) && QM->Slots.IsValidIndex(0) && QM->Slots[0].QuestNo != 19 && G1.Contains(20),
			FString::Printf(TEXT("(г7) 5052 завершил 19: бит взведён, ячейка 0 освобождена (квест %d), corin выдаёт [%s] (до — [%s])"),
				QM->Slots.IsValidIndex(0) ? QM->Slots[0].QuestNo : -1, *Join(G1), *Join(G0)));
		Check(QM->EvalMarkForNsr(TEXT("corin")) == 1, TEXT("(г7) метка corin СРАЗУ = 1 (без тика)"));
	}
	// ═══ (г8) ЗАВЕРШЕНИЕ 21 по 5052 со сменой профессии 1/1/0/3 и новым квестом (@66) → класс 1, joel выдаёт 54 ═══
	if (Dlg && JobB)
	{
		JobB->DebugReset();
		SetSelf(Net, 24, 0, 0, 0, 0);
		Net->DebugFeedPacket(5047, Make5047({ { 2022, 21, 1 } }, {}));        // 21 в работе (эпизод 2022, ячейка 0)
		TArray<uint8> B = Make5052R(0, {}, 0, 0, 0);
		PutI16(B, 0, 21); PutI32(B, 22, 2022); PutI16(B, 27, 0); PutI16(B, 30, 21); PutI16(B, 32, 1);
		B[17] = 1; B[18] = 1; B[19] = 0; PutI16(B, 20, 3);                   // профессия/класс/подкласс/очки (`a2+17..+20`)
		// Новый квест @66 (запись 44 б, как +59 у 5042): эпизод 2901, ячейка 3 (поле +5), квест 9002, стадия 0 — вне сценариев joel/wendy,
		//   чтобы его появление в журнале не меняло проверяемые ниже условия «класс 1» / «класс 0».
		PutI32(B, 66, 2901); B[66 + 4] = 4; PutI16(B, 66 + 5, 3); B[66 + 7] = 2; PutI16(B, 66 + 8, 9002); PutI16(B, 66 + 10, 0);
		const int32 NJ0 = JobB->DebugJobNoticeCount();
		// 20197 «Получено задание [ss1].» — оракул по тексту ТЕКУЩЕГО языка (до «[ss1]»): ★ 22.10.2026 прогон EN искал русский литерал.
		FString QGot = AsdaNotice::LocRaw(GI, 20197);
		{ int32 P = QGot.Find(TEXT("[ss1]")); if (P != INDEX_NONE) { QGot = QGot.Left(P); } QGot.TrimStartAndEndInline(); }
		const int32 NQ0 = Chat->DebugCountSystemLines(QGot);
		Net->DebugFeedPacket(5052, B);
		Check(Net->SelfJob == 1 && Net->SelfClassId == 1 && Net->SelfSubJob == 0 && Net->SelfSkillPoints == 3,
			FString::Printf(TEXT("(г8) 5052: профессия %d, класс %d, подкласс %d, очки %d (ждём 1/1/0/3)"),
				Net->SelfJob, Net->SelfClassId, Net->SelfSubJob, Net->SelfSkillPoints));
		Check(JobB->DebugJobNoticeCount() == NJ0 + 1 && JobB->DebugPhase() == 0 && JobB->GetSelfJobGroup() == 1,
			FString::Printf(TEXT("(г8) нотис 20200 «%s» есть, баннера 18500 нет (фаза %d) — как `charApplyFullStatusPacket`; группа %d"),
				*JobB->DebugLastJobNoticeText(), JobB->DebugPhase(), JobB->GetSelfJobGroup()));
		Check(QM->IsQuestCompletedPublic(21) && QM->Slots.IsValidIndex(3) && QM->Slots[3].QuestNo == 9002 && QM->Slots[3].QuestId == 2901,
			FString::Printf(TEXT("(г8) журнал: 21 завершён, квест 9002 (эпизод 2901, тело @66) лёг в ячейку 3 (там %d)"),
				QM->Slots.IsValidIndex(3) ? QM->Slots[3].QuestNo : -1));
		Check(!QGot.IsEmpty() && Chat->DebugCountSystemLines(QGot) == NQ0 + 1,
			FString::Printf(TEXT("(г8) вид 1: нотис 20197 «%s …» о квесте из @66 — ровно один"), *QGot));
		const TArray<int32> J1 = GiveOf(Dlg, 18);
		Check(SnqOk(QM, 160) == 1 && RecOk(QM, TEXT("joel"), 11) == 1 && J1.Contains(54),
			FString::Printf(TEXT("(г8) условие joel «класс 1» (.NSR №11, SNQICT 160) сошлось сразу; облачко joel [%s]"), *Join(J1)));
		Check(RecOk(QM, TEXT("wendy"), 61) == 0, TEXT("(г8) wendy: условие квеста 22 «класс 0» больше не сходится"));
		JobB->DebugReset();
	}

	// ═══ (г5) ключ 0 — прежний путь: ни строк, ни золота/предметов/обаяния, ни окна отказа ═══
	{
		AsdaCVarSave::SetForTest(TEXT("asda.Quest.TurnInRewardLines"), 0);
		for (const FItemSpec& C : RSaved.Cells) { Inv->ClearCell(C.Cont, C.Slot); }
		Inv->SetGoldDisplay(1000);
		Net->SelfCharm = 10;
		const int32 G0 = Cnt(GoldLine), L0 = Cnt(SlotLine), I0 = Cnt(ItemLine), C0 = Cnt(CharmLine);
		Feed(Make5042R(0, 1, 2020, { { kItem, 1, S1, 1 } }, 777, 13345, 15));
		const FAsdaItemInstance* A = Inv->GetCell(1, S1);
		Check(Cnt(GoldLine) == G0 && Cnt(SlotLine) == L0 && Cnt(ItemLine) == I0 && Cnt(CharmLine) == C0
			&& Inv->GetGold() == 1000 && Net->SelfCharm == 10 && A && A->ItemCode <= 0,
			TEXT("(г5) ключ 0: строк 20198/6019/20199/6025 нет, золото/обаяние/сумка не тронуты (пара)"));
		{
			const FString Gold6 = AsdaNotice::LocSlot(GI, 20198, AsdaUi::FormatThousands(2000));
			const int32 N6 = Cnt(Gold6);
			Inv->ClearCell(1, S1);
			Net->DebugFeedPacket(5052, Make5052R(0, { { kItem, 1, S1, 1 } }, 555, 3000, 12));
			const FAsdaItemInstance* A6 = Inv->GetCell(1, S1);
			Check(Cnt(Gold6) == N6 && Inv->GetGold() == 1000 && Net->SelfCharm == 10 && A6 && A6->ItemCode <= 0,
				TEXT("(г5) ключ 0: завершение 5052 — строк нет, золото/обаяние/сумка не тронуты (пара к г6)"));
		}
		WM->ClearMsgBoxQueue(TEXT("самотест наград: г5"));
		Feed(Make5042R(3, 1, 0, {}, 0, 0, 0));
		Check(WM->GetMsgBoxQueueNum() == 0, TEXT("(г5) ключ 0: отказ 3 — окна нет (прежний путь: только лог)"));
		AsdaCVarSave::SetForTest(TEXT("asda.Quest.TurnInRewardLines"), 1);
	}
}

void FAsdaQuestJobChangeSelfTest::FinishRewardLines(UWorld* W)
{
	UGameInstance* GI = W ? W->GetGameInstance() : nullptr;
	UAsdaNetSubsystem* Net = GI ? GI->GetSubsystem<UAsdaNetSubsystem>() : nullptr;
	UAsdaQuestMarkerSubsystem* QM = GI ? GI->GetSubsystem<UAsdaQuestMarkerSubsystem>() : nullptr;
	UAsdaInventorySubsystem* Inv = GI ? GI->GetSubsystem<UAsdaInventorySubsystem>() : nullptr;
	UAsdaWindowSubsystem* WM = GI ? GI->GetSubsystem<UAsdaWindowSubsystem>() : nullptr;
	if (Inv)
	{
		for (int32 i = 0; i < RSaved.Cells.Num() && i < RSaved.CellData.Num(); ++i)
		{
			// Штатные пути сумки (с перерисовкой и кэшем хотбара): пустая — `ClearCell`, иначе запись как есть.
			const FAsdaItemInstance& D = RSaved.CellData[i];
			if (D.ItemCode <= 0 || D.Raw.Num() < FAsdaItemInstance::kRecordSize) { Inv->ClearCell(RSaved.Cells[i].Cont, RSaved.Cells[i].Slot); }
			else { Inv->ApplyExternalRecord(D.Raw, UAsdaInventorySubsystem::EExternalRecordMode::AsIs); }
		}
		if (RSaved.GoldDisplay >= 0) { Inv->SetGoldDisplay(RSaved.GoldDisplay); }
		else { Inv->GoldDisplay = -1; Inv->RefreshMoney(); }   // исходное «не приходило» — сеттер −1 не принимает
		if (RSaved.GoldCell > 0) { Inv->SetGoldCellAmount(RSaved.GoldCell); }   // `SetGoldDisplay` трогал и ячейку 20551
	}
	if (Net)
	{
		Net->SelfWeightCur = RSaved.WeightCur; Net->SelfWeightMax = RSaved.WeightMax;
		Net->SelfCharm = RSaved.Charm; Net->bSelfCharmFromBlock = RSaved.bCharmFromBlock;
		Net->SelfLevel = RSaved.Level; Net->SelfJob = RSaved.Job; Net->SelfClassId = RSaved.Cls; Net->SelfSubJob = RSaved.Sub; Net->SelfSkillPoints = RSaved.Pts;
	}
	if (QM)
	{
		QM->Slots = RSaved.Slots; QM->DoneBits = RSaved.DoneBits; QM->bStateSynced = RSaved.bSynced;
		QM->bDirty = true;
		QM->RebuildMarks();
	}
	if (UAsdaNpcDialogSubsystem* D0 = GI ? GI->GetSubsystem<UAsdaNpcDialogSubsystem>() : nullptr) { D0->CurrentNpcId = RSaved.DlgNpcId; D0->GiveList = RSaved.Give; D0->TakeList = RSaved.Take; }
	if (UAsdaChangeJobBannerSubsystem* Job = GI ? GI->GetSubsystem<UAsdaChangeJobBannerSubsystem>() : nullptr) { Job->DebugReset(); Job->DebugSetManualClock(false); }
	if (WM) { WM->ClearMsgBoxQueue(TEXT("самотест наград: конец")); }
	AsdaCVarSave::RestoreAll(kCmdReward);
	AsdaSelfTestKit::Summary(LogAsdaQuestJobTest, TEXT("[квест-самотест наград]"), Ok, Fail, TEXT(""), false);
}

int32 FAsdaQuestJobChangeSelfTest::ShowSkillSlot(UAsdaSkillWindowSubsystem* SW, int32 Code)
{
	const UAsdaSkillDatabase* Db = SW ? SW->GetDatabase() : nullptr;
	const FAsdaSkillDef* D = Db ? Db->Get(Code) : nullptr;
	if (!D || !SW->IsWindowOpen()) { return -1; }
	SW->SetTree(UAsdaSkillDatabase::TreeFromGrade(D->Grade));   // вкладка дерева навыка (как клик игрока по вкладке)
	SW->SetScrollRow(FMath::Max(0, D->AbsSlot() / SW->GridColumns - (SW->GridRows - 1)));   // колесо: ряд узла — в видимых 9
	for (int32 i = 0; i < SW->Slots.Num(); ++i) { if (SW->Slots[i].SkillCode == Code) { return i; } }
	return -1;
}

// ★ 22.10.2026 — НАВЫКИ ПОСЛЕ СМЕНЫ ПРОФЕССИИ. Пакеты — РЕАЛЬНЫЕ тела по серверу (`RE/ida/gs_migrate/out/GameServer.c`):
//   5042 блок @10..@14 = `sub_460CE0` (профессия/класс/подкласс/очки `sub_52E900`: уровень 5 → 2), 5052 @17..@20 — тот же блок;
//   5252 — базовые навыки класса из `Donors/GameServer_stock/SkillTable_Info/ChangeJobAddSkillTable.txt` (строки 12 и 1; фильтр
//   `sub_48A8C0` по `SkillInfo.sif` +189/+190/+200..208 их все пропускает у свежего персонажа) с хвостом паддинга 15 и 19 б
//   (тела 120 и 124 б — журнал игрока Player5 19.10.2026 20:12:51 и 21:22:01); 931 → 932 — тот же журнал, 21:22:01–02:
//   «Howl of Stamina» 5/5 открыла «Party Howl of Stamina», клик по ней — «доступен»=0. Свой персонаж до смены — новичок
//   (класс 0, списка 5251 у него нет: в журналах игроков у профессии 0 строк «список (полный)» нет ни одной).
//   Вложение — клик игрока по узлу (`OnSlotClicked` → гейты `skillWndSelectSlot@0xE53C80`) с ключом `asda.Skill.NoConfirm 1`
//   (подтверждение 9415 зовёт тот же `RequestLearn`); 5253 — БОЕВОЙ `SendSkillLearn5253` под узким перехватом.
void FAsdaQuestJobChangeSelfTest::RunSkills(UWorld* W)
{
	UGameInstance* GI = W ? W->GetGameInstance() : nullptr;
	UAsdaNetSubsystem* Net = GI ? GI->GetSubsystem<UAsdaNetSubsystem>() : nullptr;
	UAsdaQuestMarkerSubsystem* QM = GI ? GI->GetSubsystem<UAsdaQuestMarkerSubsystem>() : nullptr;
	UAsdaSkillWindowSubsystem* SW = GI ? GI->GetSubsystem<UAsdaSkillWindowSubsystem>() : nullptr;
	UAsdaChangeJobBannerSubsystem* JobB = GI ? GI->GetSubsystem<UAsdaChangeJobBannerSubsystem>() : nullptr;
	UAsdaWindowSubsystem* WM = GI ? GI->GetSubsystem<UAsdaWindowSubsystem>() : nullptr;
	Ok = 0; Fail = 0;
	GeoSlot = -1;
	if (!Net || !QM || !SW || !JobB || !WM || !SW->GetDatabase())
	{
		Check(false, TEXT("нет одной из подсистем (сеть/метки/окно навыков с базой/баннер/окна)"));
		return;
	}
	// ── снимок ──
	SSaved = FSkillSaved();
	SSaved.Level = Net->SelfLevel; SSaved.Job = Net->SelfJob; SSaved.Cls = Net->SelfClassId; SSaved.Sub = Net->SelfSubJob; SSaved.Pts = Net->SelfSkillPoints;
	SSaved.Learned = SW->Learned; SSaved.WinPoints = SW->SkillPoints; SSaved.Tree = SW->CurTree; SSaved.Scroll = SW->ScrollRow;
	SSaved.Selected = SW->SelectedSlot; SSaved.JobOv = SW->JobOverride; SSaved.SubOv = SW->SubJobOverride; SSaved.bWinOpen = SW->IsWindowOpen();
	SSaved.Slots = QM->Slots; SSaved.DoneBits = QM->DoneBits; SSaved.bSynced = QM->bStateSynced;
	AsdaCVarSave::SetForTest(TEXT("asda.Quest.ApplyJobChange"), 1);
	AsdaCVarSave::SetForTest(TEXT("asda.Quest.TurnInRewardLines"), 1);
	AsdaCVarSave::SetForTest(TEXT("asda.Skill.NoConfirm"), 1);
	AsdaCVarSave::SetForTest(TEXT("Asda.Quest.MarkRebuildPeriod"), 0.f);
	JobB->DebugSetManualClock(true);
	JobB->DebugReset();
	WM->ClearMsgBoxQueue(TEXT("самотест навыков: старт"));
	SW->JobOverride = -1; SW->SubJobOverride = -1;   // класс — только из сети (как у игрока)
	if (SW->IsWindowOpen()) { SW->CloseWindow(); }
	Sent5253.Reset();
	CapGen5253 = AsdaSelfTestKit::ArmCapture(Net, [](uint16 Op, const uint8* P, int32 Len, bool, TCHAR)
	{
		if (Op == 5253) { Sent5253.Add(TArray<uint8>(P, FMath::Max(0, Len))); }
	}, 5253, kCmdSkills);
	Check(CapGen5253 != 0, TEXT("узкий перехват 5253 взведён (в сеть ничего не уходит)"));
	if (CapGen5253 == 0) { return; }

	auto Feed  = [Net](int32 Op, const TArray<uint8>& B) { Net->DebugFeedPacket(Op, B); };   // штатный HandlePacket
	auto Drain = [SW]() { SW->DrainPending(); };                                            // то, что делает Tick окна
	auto AvailOf = [SW](int32 C) { const FAsdaLearnedSkill* L = SW->Learned.Find(C); return L ? (int32)L->bAvailable : -1; };
	// Клик игрока по узлу навыка: вкладка + прокрутка + `OnSlotClicked`. Возвращает число ушедших 5253 и тело последнего.
	auto Learn = [SW](int32 Code, int32& OutSlot) -> int32
	{
		Sent5253.Reset();
		OutSlot = ShowSkillSlot(SW, Code);
		if (OutSlot >= 0) { SW->OnSlotClicked(OutSlot); }
		return Sent5253.Num();
	};
	// Тело 5253 эталона `net_sendRequest_5253@0xE54DE0`: i16 код @0, флаг 1 @2, 1 @3 (ровно 4 б — MD5 сервера `sub_523F00(…, 4u)`).
	auto Body5253Ok = [](int32 Code)
	{
		if (Sent5253.Num() != 1) { return false; }
		const TArray<uint8>& B = Sent5253[0];
		return B.Num() == 4 && AsdaBytes::RdI16(B.GetData()) == Code && B[2] == 1 && B[3] == 1;
	};
	auto Dump5253 = []()
	{
		if (Sent5253.Num() == 0) { return FString(TEXT("—")); }
		const TArray<uint8>& B = Sent5253.Last();
		FString S;
		for (uint8 V : B) { S += FString::Printf(TEXT("%02X "), V); }
		return FString::Printf(TEXT("×%d, последнее %d б: %s"), Sent5253.Num(), B.Num(), *S.TrimEnd());
	};
	auto SelectableShown = [SW](int32 Slot)
	{
		return SW->Slots.IsValidIndex(Slot) && SW->Slots[Slot].Selectable
			&& SW->Slots[Slot].Selectable->GetVisibility() == ESlateVisibility::SelfHitTestInvisible;
	};
	// Хвост паддинга: первые байты дали бы «запись №36» = навык 706 уровня 3, если бы его разобрали как данные.
	TArray<uint8> Pad15; Pad15.SetNumZeroed(15); Pad15[0] = 0xC2; Pad15[1] = 0x02; Pad15[2] = 0x03; Pad15[3] = 0x2A;
	TArray<uint8> Pad19; Pad19.SetNumZeroed(19); Pad19[0] = 0xC2; Pad19[1] = 0x02; Pad19[2] = 0x03; Pad19[6] = 0x5B;
	const TArray<int32> kArcher  = { 701, 702, 723, 704, 705, 818, 774, 707, 716, 787, 773 };   // ChangeJobAddSkillTable, класс 12
	const TArray<int32> kFighter = { 501, 502, 520, 504, 505, 527, 555, 507, 508, 590 };        // ChangeJobAddSkillTable, класс 1
	auto CountAvail0 = [SW, &AvailOf](const TArray<int32>& Codes)
	{
		int32 N = 0;
		for (int32 C : Codes) { if (AvailOf(C) == 1 && SW->GetLearnedLevel(C) == 0) { ++N; } }
		return N;
	};

	// ═══ (а) 5042: новичок (ур. 5) → «Лучник» (профессия 2, класс 12, подкласс 0, очки 2) → 5252 базовых навыков лучника ═══
	{
		SetSelf(Net, 5, 0, 0, 0, 0);
		SW->Learned.Reset(); SW->SkillPoints = 0; SW->CurTree = 2; SW->ScrollRow = 0;
		Feed(5042, Make5042(9001, 1, 2, 2, 12, 0, 2));   // квест 9001 — вне карты завершения: журнал не меняется; блок персонажа — как у сервера
		Feed(5252, Make5252(kArcher, 0, Pad15));        // сервер сразу за 5042: `sub_48A8C0(класс − 1)` (GameServer.c:37955-37962)
		Drain();
		const FString JobName = SW->GetDatabase()->GetJobName(Net->SelfClassId);
		Check(Net->SelfJob == 2 && Net->SelfClassId == 12 && SW->GetSelfJob() == 12 && SW->GetSkillPoints() == 2,
			FString::Printf(TEXT("(а) 5042: профессия %d, класс %d «%s» (окно видит %d), очков в окне %d (ждём 2/12/2)"),
				Net->SelfJob, Net->SelfClassId, *JobName, SW->GetSelfJob(), SW->GetSkillPoints()));
		Check(SW->CurTree == 0, FString::Printf(TEXT("(а) защёлка 5042: вкладка %d (эталон `skillWnd_msgProc(12005, 0)` → 0)"), SW->CurTree));
		Check(SW->Learned.Num() == kArcher.Num() && !SW->Learned.Contains(706),
			FString::Printf(TEXT("(а) 5252 (тело %d б): в модели %d записей (ждём %d); хвост паддинга 15 б НЕ разобран — навыка 706 нет"),
				105 + Pad15.Num(), SW->Learned.Num(), kArcher.Num()));
		const int32 NA = CountAvail0(kArcher);
		Check(NA == kArcher.Num(),
			FString::Printf(TEXT("(а) «можно вкачивать» у %d из %d базовых навыков лучника, уровень 0 (эталон `Net_RecvLearnedSkillList` v6 = 1)"), NA, kArcher.Num()));
		SW->OpenWindow();
		Check(SW->IsWindowOpen(), TEXT("(а) окно навыков открывается (класс 12 ≠ 0 — гейт 9421 пройден)"));
		int32 Slot = -1;
		const int32 N1 = Learn(701, Slot);
		Check(Slot >= 0 && SelectableShown(Slot) && N1 == 1 && Body5253Ok(701),
			FString::Printf(TEXT("(а) узел %d навыка 701: подсветка «доступен» %s; клик → 5253 %s (ждём «BD 02 01 01»)"),
				Slot, SelectableShown(Slot) ? TEXT("есть") : TEXT("НЕТ"), *Dump5253()));
		Feed(5254, Make5254(1, 1, 701, 1));
		Drain();
		Check(SW->GetLearnedLevel(701) == 1 && SW->GetSkillPoints() == 1,
			FString::Printf(TEXT("(а) 5254 успех: 701 → уровень %d, очков %d"), SW->GetLearnedLevel(701), SW->GetSkillPoints()));
		const int32 N2 = Learn(704, Slot);
		Check(N2 == 1 && Body5253Ok(704),
			FString::Printf(TEXT("(а) второе очко — навык 704 (другое дерево): 5253 %s"), *Dump5253()));
		Feed(5254, Make5254(1, 0, 704, 1));
		Drain();
		const int32 N3 = Learn(702, Slot);
		Check(N3 == 0 && SW->GetSkillPoints() == 0,
			FString::Printf(TEXT("(а) контроль: очков 0 — клик по 702 молчит (гейт очков эталона), 5253 %s"), *Dump5253()));
		// Контроль «доступен = 0»: запись 6056 с байтом +3 = 0 и очко из ветки «только очки» 5042 — клик молчит, гейт цел.
		Feed(6056, MakeSkillRec34(702, 0, 0));
		Feed(5042, Make5042(9001, 1, 1, 0, 0, 0, 1));
		Drain();
		const int32 N4 = Learn(702, Slot);
		Check(N4 == 0 && AvailOf(702) == 0 && SW->GetSkillPoints() == 1,
			FString::Printf(TEXT("(а) контроль: 702 с «доступен» = 0 (6056), очков %d — 5253 %s"), SW->GetSkillPoints(), *Dump5253()));
		SW->CloseWindow();
	}

	// ═══ (б) 5052 (квест закрыт сбором предметов): новичок → «Воин» (1/1/0, очки 2) → 5252 базовых навыков воина ═══
	{
		JobB->DebugReset();
		SetSelf(Net, 5, 0, 0, 0, 0);
		SW->Learned.Reset(); SW->SkillPoints = 0; SW->CurTree = 2; SW->ScrollRow = 0;
		// 5052 «квест завершён» (508 б, раскладка (г6)/(г8) самотеста наград): objType @4 = 1, признак завершения i32 @13 > 0,
		//   запись слота @22 (эпизод 2020, ячейка 0, квест 9001), блок персонажа @17..@20.
		TArray<uint8> B; B.SetNumZeroed(508);
		PutI16(B, 0, 9001); B[4] = 1; PutI32(B, 13, 1); PutI32(B, 22, 2020); PutI16(B, 27, 0); PutI16(B, 30, 9001); PutI16(B, 32, 1);
		B[17] = 1; B[18] = 1; B[19] = 0; PutI16(B, 20, 2);
		Feed(5052, B);
		Feed(5252, Make5252(kFighter, 0, Pad19));   // сервер сразу за 5052: `sub_48A8C0` (GameServer.c:38182-38188)
		Drain();
		Check(Net->SelfJob == 1 && Net->SelfClassId == 1 && SW->GetSelfJob() == 1 && SW->GetSkillPoints() == 2,
			FString::Printf(TEXT("(б) 5052: профессия %d, класс %d «%s», очков в окне %d (ждём 1/1/2)"),
				Net->SelfJob, Net->SelfClassId, *SW->GetDatabase()->GetJobName(Net->SelfClassId), SW->GetSkillPoints()));
		Check(SW->CurTree == 2, FString::Printf(TEXT("(б) 5052 защёлку не ставит: вкладка %d осталась (эталон `charApplyFullStatusPacket` дерево не трогает)"), SW->CurTree));
		const int32 NA = CountAvail0(kFighter);
		Check(NA == kFighter.Num() && SW->Learned.Num() == kFighter.Num() && !SW->Learned.Contains(706),
			FString::Printf(TEXT("(б) 5252 (тело %d б): «можно вкачивать» у %d из %d, записей %d, хвост 19 б не разобран"),
				105 + Pad19.Num(), NA, kFighter.Num(), SW->Learned.Num()));
		SW->OpenWindow();
		int32 Slot = -1;
		const int32 N1 = Learn(501, Slot);
		Check(Slot >= 0 && SelectableShown(Slot) && N1 == 1 && Body5253Ok(501),
			FString::Printf(TEXT("(б) узел %d навыка 501: подсветка %s; клик → 5253 %s (ждём «F5 01 01 01»)"),
				Slot, SelectableShown(Slot) ? TEXT("есть") : TEXT("НЕТ"), *Dump5253()));
		Feed(5254, Make5254(1, 1, 501, 1));
		Drain();
		Check(SW->GetLearnedLevel(501) == 1 && SW->GetSkillPoints() == 1,
			FString::Printf(TEXT("(б) 5254 успех: 501 → уровень %d, очков %d"), SW->GetLearnedLevel(501), SW->GetSkillPoints()));
	}

	// ═══ (в) вкачка открывает навык: 931 «Howl of Stamina» 4 → 5 открывает 932 (`sub_47E9B0`: 5252, затем 5254) ═══
	{
		Feed(5251, MakeSkillRec34(931, 4, 1));          // 931 выучен до 4, «доступен» (полный список при входе)
		Feed(5042, Make5042(9001, 1, 1, 0, 0, 0, 5));    // очки 5 (ветка «только очки»)
		Drain();
		int32 Slot = -1;
		const int32 N1 = Learn(931, Slot);
		Check(N1 == 1 && Body5253Ok(931), FString::Printf(TEXT("(в) 931 (4/5): клик → 5253 %s"), *Dump5253()));
		Feed(5252, Make5252({ 932 }, 0, Pad15));         // `sub_47E9B0` шлёт 5252 ДО ответа 5254 (GameServer.c:48351 → :48373)
		Feed(5254, Make5254(1, 4, 931, 5));
		Drain();
		Check(SW->GetLearnedLevel(931) == 5 && AvailOf(932) == 1 && SW->GetLearnedLevel(932) == 0,
			FString::Printf(TEXT("(в) 931 → уровень %d; 932 пришёл в 5252: «доступен» %d, уровень %d"),
				SW->GetLearnedLevel(931), AvailOf(932), SW->GetLearnedLevel(932)));
		const int32 N2 = Learn(932, Slot);
		Check(Slot >= 0 && SelectableShown(Slot) && N2 == 1 && Body5253Ok(932),
			FString::Printf(TEXT("(в) узел %d навыка 932 «Party Howl of Stamina»: подсветка %s; клик → 5253 %s (журнал игрока: «доступен»=0)"),
				Slot, SelectableShown(Slot) ? TEXT("есть") : TEXT("НЕТ"), *Dump5253()));
		Feed(5254, Make5254(1, 3, 932, 1));   // ответ сервера на 932: очков 4 − 1
		Drain();
		Check(SW->GetLearnedLevel(932) == 1 && SW->GetSkillPoints() == 3,
			FString::Printf(TEXT("(в) 5254 успех: 932 → уровень %d, очков %d"), SW->GetLearnedLevel(932), SW->GetSkillPoints()));
	}
	// ═══ ★ 29.10.2026 (в2) портал сразу после вложенного очка (баг игроков «очки навыков не открывают навыки, помогает
	//   перезапуск»; ключ asda.SkillWin.KeepQueuesOnMapChange): ответ 5254 и очки приняты по прежнему соединению, но тик окна
	//   их ещё не разобрал — в тот же кадр смена карты. Персонаж тот же: ответ обязан примениться (сервер на портале список не
	//   повторяет). Контроль ключом 0 — прежний порт выбрасывал ответ: уровень и очки оставались старыми до перезахода. ═══
	{
		const TMap<int32, FAsdaLearnedSkill> LearnedAfterV = SW->Learned;   // модель после (в) — вернуть для (г)
		AsdaCVarSave::SetForTest(TEXT("asda.SkillWin.KeepQueuesOnMapChange"), 1);
		Feed(5254, Make5254(1, 2, 932, 2));   // 932: 1 → 2, очков 3 − 1
		SW->ResetSessionState(EAsdaSessionResetReason::MapChanged);
		Drain();
		Check(SW->GetLearnedLevel(932) == 2 && SW->GetSkillPoints() == 2,
			FString::Printf(TEXT("(в2) 5254 и портал в одном кадре: 932 → уровень %d, очков %d (ждём 2/2) [ИСПРАВЛЕНИЕ]"),
				SW->GetLearnedLevel(932), SW->GetSkillPoints()));
		AsdaCVarSave::SetForTest(TEXT("asda.SkillWin.KeepQueuesOnMapChange"), 0);
		Feed(5254, Make5254(1, 1, 932, 3));   // 932: 2 → 3, очков 2 − 1 — при ключе 0 ответ выбрасывается сменой карты
		SW->ResetSessionState(EAsdaSessionResetReason::MapChanged);
		Drain();
		Check(SW->GetLearnedLevel(932) == 2 && SW->GetSkillPoints() == 2,
			FString::Printf(TEXT("(в2) контроль, ключ 0: ответ потерян — 932 уровень %d, очков %d (прежний порт: 2/2)"),
				SW->GetLearnedLevel(932), SW->GetSkillPoints()));
		AsdaCVarSave::SetForTest(TEXT("asda.SkillWin.KeepQueuesOnMapChange"), 1);
		// Полный сброс по-прежнему чистит очереди: ответ, пришедший до выхода в выбор персонажа, к следующему входу не доживает.
		const int32 TreeWas = SW->CurTree, RowWas = SW->ScrollRow, JobOvWas = SW->JobOverride, SubOvWas = SW->SubJobOverride;
		Feed(5254, Make5254(1, 0, 932, 3));
		SW->ResetSessionState(EAsdaSessionResetReason::Disconnected);
		Check(SW->PendingResult.Num() == 0, FString::Printf(TEXT("(в2) полный сброс (разрыв связи) чистит очередь ответов, как прежде (в очереди %d)"),
			SW->PendingResult.Num()));
		// Состояние для (г): модель, очки, вкладка и окно — как после (в).
		SW->Learned = LearnedAfterV; SW->SkillPoints = 3;
		SW->CurTree = TreeWas; SW->ScrollRow = RowWas; SW->JobOverride = JobOvWas; SW->SubJobOverride = SubOvWas;
		if (!SW->IsWindowOpen()) { SW->OpenWindow(); }
	}
	// (г) подсветку «доступен» навыка новой профессии проверяем по ЖИВОЙ геометрии (через кадр, `CheckSkillsGeometry`): базовый
	//   навык воина без уровня — 502 (пришёл в 5252 из (б)), очков 3; окно остаётся открытым до проверки.
	GeoSlot = ShowSkillSlot(SW, 502);
	SW->SelectedSlot = -1;
}

void FAsdaQuestJobChangeSelfTest::CheckSkillsGeometry(UWorld* W)
{
	UGameInstance* GI = W ? W->GetGameInstance() : nullptr;
	UAsdaSkillWindowSubsystem* SW = GI ? GI->GetSubsystem<UAsdaSkillWindowSubsystem>() : nullptr;
	UUserWidget* Win = SW ? SW->GetWindowWidget() : nullptr;
	if (!SW || !Win || !SW->IsWindowOpen() || !SW->Slots.IsValidIndex(GeoSlot) || !SW->Slots[GeoSlot].Selectable)
	{
		Check(false, FString::Printf(TEXT("(г) геометрия: окно навыков не открыто или узла %d нет"), GeoSlot));
		return;
	}
	const FAsdaSkillSlotWidgets& S = SW->Slots[GeoSlot];
	const FGeometry GW = Win->GetCachedGeometry();
	const FGeometry GS = S.Selectable->GetCachedGeometry();
	const FGeometry GI2 = S.Icon ? S.Icon->GetCachedGeometry() : FGeometry();
	const FVector2D WinMin = GW.GetAbsolutePosition(), WinMax = WinMin + GW.GetAbsoluteSize();
	const FVector2D SelMin = GS.GetAbsolutePosition(), SelMax = SelMin + GS.GetAbsoluteSize();
	const FVector2D Ctr = (SelMin + SelMax) * 0.5;
	const bool bInside = SelMin.X >= WinMin.X - 0.5 && SelMin.Y >= WinMin.Y - 0.5 && SelMax.X <= WinMax.X + 0.5 && SelMax.Y <= WinMax.Y + 0.5;
	Check(S.SkillCode == 502 && S.Selectable->GetVisibility() == ESlateVisibility::SelfHitTestInvisible
		&& GS.GetLocalSize().X > 0.f && GS.GetLocalSize().Y > 0.f && bInside && (!S.Icon || GI2.IsUnderLocation(Ctr)),
		FString::Printf(TEXT("(г) путь игрока, живая геометрия: узел %d навыка %d — подсветка «доступен» %.0f×%.0f в (%.0f,%.0f), ")
			TEXT("внутри окна (%.0f,%.0f)–(%.0f,%.0f), центр над иконкой узла"),
			GeoSlot, S.SkillCode, GS.GetLocalSize().X, GS.GetLocalSize().Y, SelMin.X, SelMin.Y, WinMin.X, WinMin.Y, WinMax.X, WinMax.Y));
	SW->CloseWindow();
}

void FAsdaQuestJobChangeSelfTest::FinishSkills(UWorld* W)
{
	UGameInstance* GI = W ? W->GetGameInstance() : nullptr;
	UAsdaNetSubsystem* Net = GI ? GI->GetSubsystem<UAsdaNetSubsystem>() : nullptr;
	UAsdaQuestMarkerSubsystem* QM = GI ? GI->GetSubsystem<UAsdaQuestMarkerSubsystem>() : nullptr;
	UAsdaSkillWindowSubsystem* SW = GI ? GI->GetSubsystem<UAsdaSkillWindowSubsystem>() : nullptr;
	UAsdaWindowSubsystem* WM = GI ? GI->GetSubsystem<UAsdaWindowSubsystem>() : nullptr;
	if (Net)
	{
		AsdaSelfTestKit::DisarmCapture(Net, CapGen5253);
		Net->SelfLevel = SSaved.Level; Net->SelfJob = SSaved.Job; Net->SelfClassId = SSaved.Cls; Net->SelfSubJob = SSaved.Sub; Net->SelfSkillPoints = SSaved.Pts;
	}
	CapGen5253 = 0;
	Sent5253.Reset();
	if (SW)
	{
		SW->DrainPending();   // хвост очередей прогона — до возврата модели
		if (SW->IsWindowOpen()) { SW->CloseWindow(); }
		SW->Learned = SSaved.Learned; SW->SkillPoints = SSaved.WinPoints; SW->CurTree = SSaved.Tree; SW->ScrollRow = SSaved.Scroll;
		SW->SelectedSlot = SSaved.Selected; SW->JobOverride = SSaved.JobOv; SW->SubJobOverride = SSaved.SubOv;
		if (SSaved.bWinOpen) { SW->OpenWindow(); }
	}
	if (QM)
	{
		QM->Slots = SSaved.Slots; QM->DoneBits = SSaved.DoneBits; QM->bStateSynced = SSaved.bSynced;
		QM->bDirty = true;
		QM->RebuildMarks();
	}
	if (UAsdaChangeJobBannerSubsystem* Job = GI ? GI->GetSubsystem<UAsdaChangeJobBannerSubsystem>() : nullptr) { Job->DebugReset(); Job->DebugSetManualClock(false); }
	if (WM) { WM->ClearMsgBoxQueue(TEXT("самотест навыков: конец")); }
	AsdaCVarSave::RestoreAll(kCmdSkills);
	AsdaSelfTestKit::Summary(LogAsdaQuestJobTest, TEXT("[квест-самотест навыков]"), Ok, Fail, TEXT(""), false);
}

// ★ 26.10.2026 — 4-Я ПРОФЕССИЯ ВИДНА И РАБОТАЕТ (`asda.Quest.SelfTestJob4`, объявление и разбор — в теле структуры).
//   Пакеты — РЕАЛЬНЫЕ раскладки: 1017 (`Net_RecvCharSelect@0xF03910`, блок `g_aCharSelectData`), 5251 (записи 34 б `sub_476E10`),
//   5253/5254 (`Recv_SkillPoint_SkillActive@0x4774C0`), 5256 (`skillCastSelfSendPacket@0x9FE210`), 5257 (`Recv_SkillCast@0xF4E370`),
//   5042 (`sub_461010`: {профессия, класс прежний, подкласс новый, очки}). Стартовые навыки 4-й профессии лучника — строка 15
//   `SkillTable_Info/ChangeJobAddSkillTable.txt` (живой = сток): 826 828 841 842 843 (выдаёт P87, `sub_481CE0` → `sub_48A8C0`).
void FAsdaQuestJobChangeSelfTest::RunJob4(UWorld* W)
{
	UGameInstance* GI = W ? W->GetGameInstance() : nullptr;
	UAsdaNetSubsystem* Net = GI ? GI->GetSubsystem<UAsdaNetSubsystem>() : nullptr;
	UAsdaQuestMarkerSubsystem* QM = GI ? GI->GetSubsystem<UAsdaQuestMarkerSubsystem>() : nullptr;
	UAsdaSkillWindowSubsystem* SW = GI ? GI->GetSubsystem<UAsdaSkillWindowSubsystem>() : nullptr;
	UAsdaSkillUseSubsystem* Use = GI ? GI->GetSubsystem<UAsdaSkillUseSubsystem>() : nullptr;
	UAsdaChangeJobBannerSubsystem* JobB = GI ? GI->GetSubsystem<UAsdaChangeJobBannerSubsystem>() : nullptr;
	UAsdaWindowSubsystem* WM = GI ? GI->GetSubsystem<UAsdaWindowSubsystem>() : nullptr;
	Ok = 0; Fail = 0;
	CastCode = -1; SavedWeaponSub = -1000;
	if (!Net || !QM || !SW || !Use || !JobB || !WM || !SW->GetDatabase())
	{
		Check(false, TEXT("нет одной из подсистем (сеть/метки/окно навыков с базой/применение/баннер/окна)"));
		return;
	}
	// ── снимок ──
	SSaved = FSkillSaved();
	SSaved.Level = Net->SelfLevel; SSaved.Job = Net->SelfJob; SSaved.Cls = Net->SelfClassId; SSaved.Sub = Net->SelfSubJob; SSaved.Pts = Net->SelfSkillPoints;
	SSaved.Learned = SW->Learned; SSaved.WinPoints = SW->SkillPoints; SSaved.Tree = SW->CurTree; SSaved.Scroll = SW->ScrollRow;
	SSaved.Selected = SW->SelectedSlot; SSaved.JobOv = SW->JobOverride; SSaved.SubOv = SW->SubJobOverride; SSaved.bWinOpen = SW->IsWindowOpen();
	SSaved.Slots = QM->Slots; SSaved.DoneBits = QM->DoneBits; SSaved.bSynced = QM->bStateSynced;
	SavedBlock1017 = Net->CharSelectData; SavedCharId = Net->SelectedCharId; SavedSoul = Net->SelfSoulState;
	J4Self = MakeUnique<FAsdaNetSelfScope>(Net, kCmdJob4);
	if (!Net->bSelfUserNumKnown) { Net->SelfUserNum = 1; Net->bSelfUserNumKnown = true; }   // адресат 5257 — «я»
	AsdaCVarSave::SetForTest(TEXT("asda.Quest.ApplyJobChange"), 1);
	AsdaCVarSave::SetForTest(TEXT("asda.Skill.NoConfirm"), 1);
	AsdaCVarSave::SetForTest(TEXT("asda.Job4.StageFromSubJob"), 1);
	// ★ 11.11.2026 (D4, разбор красных эталона): с 05.11.2026 (`asda.Skill.Job4Pools`, AsdaSkillWindowSubsystem.cpp) навык 4-й
	//   профессии не берёт очки основных деревьев: у персонажа самотеста (ур. 60, свободных 5) пул 4-й профессии пуст, клик
	//   по узлу 841 давал msgbox 2202 вместо 5253 (эталон 7.6.0: ok=21 fail=1). Здесь проверяется путь «клик → 5253 → 5254 →
	//   применение», а не раздел очков по пулам, поэтому на время прогона пулы выключены; возврат — RestoreAll(kCmdJob4).
	AsdaCVarSave::SetForTest(TEXT("asda.Skill.Job4Pools"), 0);
	AsdaCVarSave::SetForTest(TEXT("Asda.Quest.MarkRebuildPeriod"), 0.f);
	JobB->DebugSetManualClock(true);
	JobB->DebugReset();
	WM->ClearMsgBoxQueue(TEXT("самотест 4-й профессии: старт"));
	SW->JobOverride = -1; SW->SubJobOverride = -1;   // класс — только из сети (как у игрока)
	if (SW->IsWindowOpen()) { SW->CloseWindow(); }
	Sent5253.Reset(); Sent5256.Reset();
	FxSink.Lines.Reset();
	GLog->AddOutputDevice(&FxSink);
	Net->bStandCaptureSkillUse = true;   // 5256 офлайн собирается штатным отправителем и уходит в перехват
	CapGen5253 = AsdaSelfTestKit::ArmCapture(Net, [](uint16 Op, const uint8* P, int32 Len, bool, TCHAR)
	{
		if (Op == 5253) { Sent5253.Add(TArray<uint8>(P, FMath::Max(0, Len))); }
		if (Op == 5256) { Sent5256.Add(TArray<uint8>(P, FMath::Max(0, Len))); }
	}, 0, kCmdJob4);
	Check(CapGen5253 != 0, TEXT("перехват отправок взведён (в сеть ничего не уходит)"));
	if (CapGen5253 == 0) { return; }

	auto Feed  = [Net](int32 Op, const TArray<uint8>& B) { Net->DebugFeedPacket(Op, B); };   // штатный HandlePacket
	auto Drain = [SW]() { SW->DrainPending(); };
	auto Learn = [SW](int32 Code, int32& OutSlot) -> int32
	{
		Sent5253.Reset();
		OutSlot = ShowSkillSlot(SW, Code);
		if (OutSlot >= 0) { SW->OnSlotClicked(OutSlot); }
		return Sent5253.Num();
	};
	auto Body5253Ok = [](int32 Code)
	{
		if (Sent5253.Num() != 1) { return false; }
		const TArray<uint8>& B = Sent5253[0];
		return B.Num() == 4 && AsdaBytes::RdI16(B.GetData()) == Code && B[2] == 1 && B[3] == 1;
	};
	auto BtnShown = [SW]() { return SW->Job4Button && SW->Job4Button->GetVisibility() == ESlateVisibility::Visible; };
	const TArray<int32> kRow15 = { 826, 828, 841, 842, 843 };   // ChangeJobAddSkillTable, строка 15 (Мастер лука)

	// ═══ (а) вход персонажем 4-й профессии: 1017 (класс 14 «Опытный лучник», подкласс 15, ур. 60, очки 5, профессия 2) + 5251 ═══
	{
		Feed(1017, Make1017(SavedBlock1017, 14, 15, 60, 5, 2));
		TArray<uint8> L;
		for (int32 C : kRow15) { L.Append(MakeSkillRec34(C, 0, 1)); }
		SW->Learned.Reset();
		Feed(5251, L);
		Drain();
		Check(Net->SelfClassId == 14 && Net->SelfSubJob == 15 && AsdaSelf::GetSubJob(GI) == 15 && AsdaSelf::GetJob4StageClass(GI) == 15,
			FString::Printf(TEXT("(а) 1017: класс %d, подкласс %d → ступень 4-й профессии %d (ждём 14/15/15)"),
				Net->SelfClassId, Net->SelfSubJob, AsdaSelf::GetJob4StageClass(GI)));
		int32 NRow = 0;
		for (int32 C : kRow15) { if (SW->Learned.Contains(C)) { ++NRow; } }
		Check(NRow == kRow15.Num(), FString::Printf(TEXT("(а) 5251: в модели окна %d из %d стартовых навыков строки 15"), NRow, kRow15.Num()));
		SW->OpenWindow();
		Check(SW->IsWindowOpen(), TEXT("(а) окно навыков открывается (класс 14, полоса 2)"));
		Check(BtnShown() && SW->Job4ButtonText && SW->Job4ButtonText->GetText().ToString() == AsdaNotice::Loc(GI, 111500),
			FString::Printf(TEXT("(а) кнопка 4-й профессии 12401 видна (подпись «%s», ждём loc 111500 «%s»)"),
				SW->Job4ButtonText ? *SW->Job4ButtonText->GetText().ToString() : TEXT("<нет>"), *AsdaNotice::Loc(GI, 111500)));
		Check(UAsdaSkillDatabase::Job4TabLocId(SW->GetSelfBand(), SW->GetSelfSubJob()) == 9424,
			FString::Printf(TEXT("(а) подпись вкладки 4-й профессии подкласса 15 — loc 9424 «%s»"), *AsdaNotice::Loc(GI, 9424)));
		// Щелчок игрока по кнопке 12401 → дерево 4-й профессии.
		if (SW->Job4Button) { SW->Job4Button->OnClicked.Broadcast(); }
		TArray<int32> Tree3;
		SW->GetDatabase()->GetTreeSkills(SW->GetSelfBand(), 3, Tree3);
		bool bAllGrade11 = Tree3.Num() > 0;
		for (int32 C : Tree3) { const FAsdaSkillDef* D = SW->GetDatabase()->Get(C); if (!D || D->Grade != 11) { bAllGrade11 = false; } }
		Check(SW->CurTree == 3 && bAllGrade11 && Tree3.Contains(841)
			&& SW->Job4PointFrame && SW->Job4PointFrame->GetVisibility() != ESlateVisibility::Collapsed,
			FString::Printf(TEXT("(а) щелчок 12401: вкладка %d (ждём 3), в дереве %d навыков ветки 11 (с 841), строка очков 4-й профессии %s"),
				SW->CurTree, Tree3.Num(), (SW->Job4PointFrame && SW->Job4PointFrame->GetVisibility() != ESlateVisibility::Collapsed) ? TEXT("видна") : TEXT("СКРЫТА")));
	}

	// ═══ (б) изучение: клик по узлу навыка 4-й профессии → 5253, ответ 5254 ═══
	{
		int32 Slot = -1;
		const int32 N1 = Learn(841, Slot);
		Check(Slot >= 0 && N1 == 1 && Body5253Ok(841) && SW->CurTree == 3,
			FString::Printf(TEXT("(б) узел %d навыка 841 на вкладке %d: клик → 5253 ×%d (ждём 1 тело «49 03 01 01»)"), Slot, SW->CurTree, N1));
		Feed(5254, Make5254(1, 4, 841, 1));
		Drain();
		Check(SW->GetLearnedLevel(841) == 1 && SW->GetSkillPoints() == 4,
			FString::Printf(TEXT("(б) 5254 успех: 841 → уровень %d, очков %d (ждём 1/4)"), SW->GetLearnedLevel(841), SW->GetSkillPoints()));
	}

	// ═══ (в) применение: путь клика по слоту (`DispatchByKind`) → действие 102 → 5256 на следующем тике ═══
	{
		// ОМ своего персонажа (офлайн — 0; на живом сервере её дают 4005/4038): 1000 на время прогона, гейт 1018 проверяет стоимость.
		VitIdx = AsdaSelf::GetUserIdxOr(GI, INDEX_NONE);
		bVitHad = Net->VitalsByUserIdx.Contains(VitIdx);
		if (bVitHad) { VitWas = Net->VitalsByUserIdx[VitIdx]; }
		UAsdaNetSubsystem::FAsdaVitals& V = Net->VitalsByUserIdx.FindOrAdd(VitIdx);
		V.MpCur = 1000; V.MpMax = FMath::Max(V.MpMax, 1000);
		// Цель — моб рядом (щелчок по мобу): навыки строки 15 — «на моба или игрока» (castType 3) и по площади; без цели и без целей
		//   в боксе эталон заявку не шлёт. Моб появляется штатным 4013 (тип 1 — волк alpen)
		//   на соседней клетке, уходит штатным 4011 в FinishJob4.
		if (AAsdaPlayerController* PC = Cast<AAsdaPlayerController>(W->GetFirstPlayerController()))
		{
			const AAsdaPlayerCharacter* Me = Cast<AAsdaPlayerCharacter>(PC->GetPawn());
			const FIntPoint Tile = Me ? AsdaCoord::GameToTile(AsdaCoord::UeToGame2D(Me->GetActorLocation())) + FIntPoint(1, 0) : FIntPoint(257, 256);
			Feed(4013, AsdaTestPackets::MobSpawn4013(kJ4MobRid, /*Type*/1, Tile, /*State*/1, /*Speed*/250));
			UAsdaWorldSubsystem* WSs = GI->GetSubsystem<UAsdaWorldSubsystem>();
			AAsdaMobActor* Mob = WSs ? WSs->FindNetMobByRid(kJ4MobRid) : nullptr;
			Check(Mob != nullptr, FString::Printf(TEXT("(в) моб-цель rid %d появился пакетом 4013 на клетке (%d,%d)"), kJ4MobRid, Tile.X, Tile.Y));
			TargetWas = PC->TargetActor; bTargetSet = true;
			PC->TargetActor = Mob;
		}
		const UAsdaSkillDatabase* Db = SW->GetDatabase();
		TArray<int32> Tried;
		// Сначала навыки без цели (castType ≠ 3 «по цели»): офлайн выбранной цели нет, а заявку «по цели» эталон без цели не шлёт.
		TArray<int32> Order = kRow15;
		Order.StableSort([Db](int32 A, int32 B)
		{
			const FAsdaSkillDef* DA = Db->Get(A); const FAsdaSkillDef* DB = Db->Get(B);
			return (DA && DA->CastType != 3) && !(DB && DB->CastType != 3);
		});
		for (int32 C : Order)
		{
			const FAsdaSkillDef* D = Db->Get(C);
			if (!D || (D->Kind != 1 && D->Kind != 2) || D->SubType == 3 || D->SubType == 4) { continue; }   // пассивы/авто — не применяются
			if (SW->GetLearnedLevel(C) <= 0)
			{
				int32 Slot = -1;
				Learn(C, Slot);
				Feed(5254, Make5254(1, FMath::Max(0, SW->GetSkillPoints() - 1), C, 1));
				Drain();
			}
			Tried.Add(C);
			if (Use->DispatchByKind(C, -1)) { CastCode = C; break; }
		}
		FString TriedS; for (int32 C : Tried) { TriedS += FString::Printf(TEXT("%d "), C); }
		Check(CastCode > 0, FString::Printf(TEXT("(в) применение навыка 4-й профессии: гейты пройдены у %d (пробовали %s)"), CastCode, *TriedS.TrimEnd()));
	}
	SW->CloseWindow();
}

void FAsdaQuestJobChangeSelfTest::Job4Cast(UWorld* W)
{
	UGameInstance* GI = W ? W->GetGameInstance() : nullptr;
	UAsdaNetSubsystem* Net = GI ? GI->GetSubsystem<UAsdaNetSubsystem>() : nullptr;
	if (!Net || CastCode <= 0) { return; }
	const TArray<uint8>* Req = nullptr;
	for (const TArray<uint8>& B : Sent5256) { if (B.Num() >= 31 && AsdaBytes::RdI16(B.GetData() + 24) == CastCode) { Req = &B; } }
	Check(Req != nullptr, FString::Printf(TEXT("(в) заявка 5256 ушла (перехвачено %d), код навыка @24 = %d"), Sent5256.Num(), CastCode));
	// Ответ сервера «принято» 5257: @0 код 1, @1 i16 кастер (я), @3 i16 навык, @5 уровень, @6 i16 записей 0.
	TArray<uint8> B; B.SetNumZeroed(8);
	B[0] = 1; PutI16(B, 1, Net->SelfUserNum); PutI16(B, 3, CastCode); B[5] = 1; PutI16(B, 6, 0);
	FxSink.Lines.Reset();
	Net->DebugFeedPacket(5257, B);
}

void FAsdaQuestJobChangeSelfTest::Job4Visual(UWorld* W)
{
	UGameInstance* GI = W ? W->GetGameInstance() : nullptr;
	UAsdaNetSubsystem* Net = GI ? GI->GetSubsystem<UAsdaNetSubsystem>() : nullptr;
	UAsdaSkillWindowSubsystem* SW = GI ? GI->GetSubsystem<UAsdaSkillWindowSubsystem>() : nullptr;
	UAsdaSkillFxSubsystem* Fx = GI ? GI->GetSubsystem<UAsdaSkillFxSubsystem>() : nullptr;
	APlayerController* PC = W ? W->GetFirstPlayerController() : nullptr;
	AAsdaPlayerCharacter* Me = PC ? Cast<AAsdaPlayerCharacter>(PC->GetPawn()) : nullptr;
	if (!Net || !SW) { return; }
	// ═══ (г) показ: строка «[навыки-визуал] навык N … (мой) … клип … (anim_K …) … каст: эффектов E … ударов a/b/c» ═══
	if (CastCode > 0)
	{
		GLog->Flush();
		const FString Tag = FString::Printf(TEXT("[навыки-визуал] навык %d «"), CastCode);
		FString Line;
		for (const FString& S : FxSink.Lines) { if (S.Contains(Tag)) { Line = S; } }
		int32 Anim = -1, NCast = 0, H0 = 0, H1 = 0, H2 = 0;
		{
			const int32 A = Line.Find(TEXT("(anim_"));
			if (A >= 0) { Anim = FCString::Atoi(*Line.Mid(A + 6, 4)); }
			const int32 E = Line.Find(TEXT("каст: эффектов "));
			if (E >= 0) { NCast = FCString::Atoi(*Line.Mid(E + 15, 4)); }
			const int32 U = Line.Find(TEXT("ударов "));
			if (U >= 0)
			{
				TArray<FString> P; Line.Mid(U + 7, 16).ParseIntoArray(P, TEXT("/"));
				if (P.Num() >= 3) { H0 = FCString::Atoi(*P[0]); H1 = FCString::Atoi(*P[1]); H2 = FCString::Atoi(*P[2]); }
			}
		}
		Check(!Line.IsEmpty() && Line.Contains(TEXT("(мой)")) && Anim >= 188 && Anim <= 201 && (NCast + H0 + H1 + H2) > 0,
			FString::Printf(TEXT("(г) 5257 → показ навыка %d: клип anim_%d (клипы 4-й профессии 188…201), эффектов каста %d, ударов %d/%d/%d"),
				CastCode, Anim, NCast, H0, H1, H2));
		Check(Fx && Me && Fx->IsSkillVisualActive(Me), FString::Printf(TEXT("(г) показ навыка %d идёт на своём персонаже"), CastCode));
		if (Fx && Me) { Fx->StopSkillVisual(Me); }
	}
	// ═══ (д) путь игрока, живая геометрия: кнопка 12401 в окне — размер и место внутри окна ═══
	{
		UUserWidget* Win = SW->GetWindowWidget();
		const FGeometry GW = Win ? Win->GetCachedGeometry() : FGeometry();
		const FGeometry GB = SW->Job4Button ? SW->Job4Button->GetCachedGeometry() : FGeometry();
		const FVector2D WMin = GW.GetAbsolutePosition(), WMax = WMin + GW.GetAbsoluteSize();
		const FVector2D BMin = GB.GetAbsolutePosition(), BMax = BMin + GB.GetAbsoluteSize();
		const bool bIn = BMin.X >= WMin.X - 0.5 && BMin.Y >= WMin.Y - 0.5 && BMax.X <= WMax.X + 0.5 && BMax.Y <= WMax.Y + 0.5;
		Check(SW->IsWindowOpen() && SW->Job4Button && SW->Job4Button->GetVisibility() == ESlateVisibility::Visible
			&& GB.GetLocalSize().X > 0.f && GB.GetLocalSize().Y > 0.f && bIn,
			FString::Printf(TEXT("(д) живая геометрия: кнопка 12401 %.0f×%.0f в (%.0f,%.0f), внутри окна (%.0f,%.0f)–(%.0f,%.0f)"),
				GB.GetLocalSize().X, GB.GetLocalSize().Y, BMin.X, BMin.Y, WMin.X, WMin.Y, WMax.X, WMax.Y));
	}
	auto BtnShown = [SW]() { SW->RebuildAll(); return SW->Job4Button && SW->Job4Button->GetVisibility() == ESlateVisibility::Visible; };
	// ═══ (е) контроль ═══
	{
		AsdaCVarSave::SetForTest(TEXT("asda.Job4.StageFromSubJob"), 0);
		Check(!BtnShown(), TEXT("(е) ключ asda.Job4.StageFromSubJob 0 — как эталон: у класса 14 с подклассом 15 кнопки нет"));
		AsdaCVarSave::SetForTest(TEXT("asda.Job4.StageFromSubJob"), 1);
		Net->SelfSubJob = 0;
		Check(!BtnShown(), TEXT("(е) 3-я ступень (класс 14, подкласс 0): кнопки 4-й профессии нет"));
		Net->SelfClassId = 3; Net->SelfSubJob = 34;
		Check(!BtnShown() && AsdaSelf::GetJob4StageClass(GI) == 3,
			TEXT("(е) ремесленный подкласс 34 у класса 3: ступень остаётся 3, кнопки нет (боевую вкладку не открывает)"));
		Net->SelfClassId = 14; Net->SelfSubJob = 26;
		Check(AsdaSelf::GetJob4StageClass(GI) == 14, TEXT("(е) подкласс чужой полосы (26 у класса 14) ступень не меняет"));
		Net->SelfClassId = 15; Net->SelfSubJob = 0;
		Check(BtnShown(), TEXT("(е) 4-я ступень прямо в основном классе (15, подкласс 0 — персонаж из БД): кнопка есть, как у эталона"));
	}
	// ═══ (ж) смена профессии в мире: 5042 {профессия 2, класс 14 прежний, подкласс 15, очки} — кнопка появляется сразу ═══
	{
		SetSelf(Net, 60, 2, 14, 0, 3);
		Check(!BtnShown(), TEXT("(ж) до 5042: класс 14, подкласс 0 — кнопки нет"));
		Net->DebugFeedPacket(5042, Make5042(9001, 1, 2, 2, 14, 15, 3));
		SW->DrainPending();
		Check(Net->SelfClassId == 14 && Net->SelfSubJob == 15 && BtnShown(),
			FString::Printf(TEXT("(ж) 5042 (сервер `sub_461010`): класс %d, подкласс %d — кнопка 4-й профессии видна без перезахода"),
				Net->SelfClassId, Net->SelfSubJob));
	}
	SW->CloseWindow();
}

void FAsdaQuestJobChangeSelfTest::FinishJob4(UWorld* W)
{
	UGameInstance* GI = W ? W->GetGameInstance() : nullptr;
	UAsdaNetSubsystem* Net = GI ? GI->GetSubsystem<UAsdaNetSubsystem>() : nullptr;
	UAsdaQuestMarkerSubsystem* QM = GI ? GI->GetSubsystem<UAsdaQuestMarkerSubsystem>() : nullptr;
	UAsdaSkillWindowSubsystem* SW = GI ? GI->GetSubsystem<UAsdaSkillWindowSubsystem>() : nullptr;
	UAsdaWindowSubsystem* WM = GI ? GI->GetSubsystem<UAsdaWindowSubsystem>() : nullptr;
	GLog->Flush();
	GLog->RemoveOutputDevice(&FxSink);
	if (Net)
	{
		AsdaSelfTestKit::DisarmCapture(Net, CapGen5253);
		Net->bStandCaptureSkillUse = false;
		Net->CharSelectData = SavedBlock1017; Net->SelectedCharId = SavedCharId; Net->SetSelfSoulState(SavedSoul);
		if (bTargetSet)
		{
			if (AAsdaPlayerController* PC = W ? Cast<AAsdaPlayerController>(W->GetFirstPlayerController()) : nullptr) { PC->TargetActor = TargetWas; }
			bTargetSet = false; TargetWas.Reset();
			TArray<uint8> B; B.SetNumZeroed(2); PutI16(B, 0, kJ4MobRid);
			Net->DebugFeedPacket(4011, B);   // моб-цель уходит штатным путём (`OnEntityDespawn`)
		}
		if (VitIdx != INDEX_NONE)
		{
			if (bVitHad) { Net->VitalsByUserIdx.Add(VitIdx, VitWas); } else { Net->VitalsByUserIdx.Remove(VitIdx); }
			VitIdx = INDEX_NONE;
		}
		Net->SelfLevel = SSaved.Level; Net->SelfJob = SSaved.Job; Net->SelfClassId = SSaved.Cls; Net->SelfSubJob = SSaved.Sub; Net->SelfSkillPoints = SSaved.Pts;
	}
	if (J4Self) { J4Self->Restore(); J4Self.Reset(); }
	CapGen5253 = 0;
	Sent5253.Reset(); Sent5256.Reset();
	if (SW)
	{
		SW->DrainPending();
		if (SW->IsWindowOpen()) { SW->CloseWindow(); }
		SW->Learned = SSaved.Learned; SW->SkillPoints = SSaved.WinPoints; SW->CurTree = SSaved.Tree; SW->ScrollRow = SSaved.Scroll;
		SW->SelectedSlot = SSaved.Selected; SW->JobOverride = SSaved.JobOv; SW->SubJobOverride = SSaved.SubOv;
		if (SSaved.bWinOpen) { SW->OpenWindow(); }
	}
	if (QM)
	{
		QM->Slots = SSaved.Slots; QM->DoneBits = SSaved.DoneBits; QM->bStateSynced = SSaved.bSynced;
		QM->bDirty = true;
		QM->RebuildMarks();
	}
	if (UAsdaChangeJobBannerSubsystem* Job = GI ? GI->GetSubsystem<UAsdaChangeJobBannerSubsystem>() : nullptr) { Job->DebugReset(); Job->DebugSetManualClock(false); }
	if (WM) { WM->ClearMsgBoxQueue(TEXT("самотест 4-й профессии: конец")); }
	AsdaCVarSave::RestoreAll(kCmdJob4);
	AsdaSelfTestKit::Summary(LogAsdaQuestJobTest, TEXT("[квест-самотест 4-й профессии]"), Ok, Fail, TEXT(""), false);
}

// asda.Quest.SelfTestJobChange [delay=N] [quit] — офлайн: спавн своего павна на alpen (если его нет), через 4 с — прогон, выход по `quit`.
#if ASDA_DEV_CONSOLE
static FAutoConsoleCommandWithWorldAndArgs GAsdaQuestJobChangeSelfTest(
	TEXT("asda.Quest.SelfTestJobChange"),
	TEXT("asda.Quest.SelfTestJobChange [delay=N] [quit] — ОФЛАЙН-самотест: квесты сразу после сдачи (цепочка 19→20), смены профессии ")
	TEXT("в 5042 (21 → 54 «класс 1», 22 «класс 0» уходит) и нового уровня 4036 (91 → 1); пакеты штатным HandlePacket"),
	FConsoleCommandWithWorldAndArgsDelegate::CreateLambda([](const TArray<FString>& A, UWorld* W)
	{
		const FAsdaSelfTestArgs KitArgs = FAsdaSelfTestArgs::Parse(A);
		if (AsdaSelfTestKit::RefuseStart(W, FAsdaQuestJobChangeSelfTest::kCmd, KitArgs)) { return; }
		if (!W) { return; }
		float Delay = 0.f;
		bool bQuit = false;
		for (const FString& Arg : A)
		{
			if (Arg.Equals(TEXT("quit"), ESearchCase::IgnoreCase)) { bQuit = true; }
		}
		Delay = AsdaSelfTestKit::DelayArgOr(A, Delay);   // ★ R5-fix2-c (13.11.2026): `delay=N` — общая точка (была ветка цикла аргументов)
		TWeakObjectPtr<UWorld> WW = W;
		auto Start = [WW, bQuit]()
		{
			UWorld* Wd = WW.Get();
			if (!Wd || !GEngine) { return; }
			if (AsdaSelfTestKit::RefuseDeferred(Wd, FAsdaQuestJobChangeSelfTest::kCmd, bQuit)) { return; }
			APlayerController* P = Wd->GetFirstPlayerController();
			// ★ 12.11.2026 [ИСПРАВЛЕНИЕ теста]: номер карты выставляется ВСЕГДА (тем же крюком, что и раньше), а не только когда
			//   своего персонажа ещё нет: в пачке после другого самотеста персонаж уже есть, карта оставалась чужой (0), и проверки
			//   «карта = 3» и живых меток NPC падали в зависимости от соседа по пачке. Сами проверки не ослаблены.
			GEngine->Exec(Wd, TEXT("Asda.Load.CurrentMap 3"));
			if (!P || !P->GetPawn())
			{
				GEngine->Exec(Wd, TEXT("Asda.World.FakeSelfSpawn 256 256 QuestJob"));
			}
			FTimerHandle H1;
			Wd->GetTimerManager().SetTimer(H1, FTimerDelegate::CreateLambda([WW, bQuit]()
			{
				UWorld* W2 = WW.Get();
				if (!W2) { AsdaCVarSave::RestoreAll(FAsdaQuestJobChangeSelfTest::kCmd); return; }
				FAsdaQuestJobChangeSelfTest::RunSync(W2);
				FAsdaQuestJobChangeSelfTest::Finish(W2);
				AsdaSelfTestKit::Quit(W2, bQuit);
			}), 4.f, false);
		};
		if (Delay <= 0.f) { Start(); return; }
		FTimerHandle H;
		W->GetTimerManager().SetTimer(H, FTimerDelegate::CreateLambda(Start), Delay, false);
	}));

// ★ 19.10.2026 — asda.Quest.SelfTestRewardLines [delay=N] [quit] — офлайн, тот же запуск (alpen + свой павн), прогон через 4 с.
static FAutoConsoleCommandWithWorldAndArgs GAsdaQuestRewardLinesSelfTest(
	TEXT("asda.Quest.SelfTestRewardLines"),
	TEXT("asda.Quest.SelfTestRewardLines [delay=N] [quit] — ОФЛАЙН-самотест строк наград 5042 (`chatBuildTransactionNotice` виды 2/3/4/6: ")
	TEXT("золото 20198, слот 6019, предметы 20199, обаяние 6025; отказ 1…13 окном (0, 24); ключ asda.Quest.TurnInRewardLines 0 — ничего)"),
	FConsoleCommandWithWorldAndArgsDelegate::CreateLambda([](const TArray<FString>& A, UWorld* W)
	{
		const FAsdaSelfTestArgs KitArgs = FAsdaSelfTestArgs::Parse(A);
		if (AsdaSelfTestKit::RefuseStart(W, FAsdaQuestJobChangeSelfTest::kCmdReward, KitArgs)) { return; }
		if (!W) { return; }
		float Delay = 0.f;
		bool bQuit = false;
		for (const FString& Arg : A)
		{
			if (Arg.Equals(TEXT("quit"), ESearchCase::IgnoreCase)) { bQuit = true; }
		}
		Delay = AsdaSelfTestKit::DelayArgOr(A, Delay);   // ★ R5-fix2-c (13.11.2026): `delay=N` — общая точка (была ветка цикла аргументов)
		TWeakObjectPtr<UWorld> WW = W;
		auto Start = [WW, bQuit]()
		{
			UWorld* Wd = WW.Get();
			if (!Wd || !GEngine) { return; }
			if (AsdaSelfTestKit::RefuseDeferred(Wd, FAsdaQuestJobChangeSelfTest::kCmdReward, bQuit)) { return; }
			APlayerController* P = Wd->GetFirstPlayerController();
			// ★ 12.11.2026 [ИСПРАВЛЕНИЕ теста]: номер карты выставляется ВСЕГДА (тем же крюком, что и раньше), а не только когда
			//   своего персонажа ещё нет: в пачке после другого самотеста персонаж уже есть, карта оставалась чужой (0), и проверки
			//   «карта = 3» и живых меток NPC падали в зависимости от соседа по пачке. Сами проверки не ослаблены.
			GEngine->Exec(Wd, TEXT("Asda.Load.CurrentMap 3"));
			if (!P || !P->GetPawn())
			{
				GEngine->Exec(Wd, TEXT("Asda.World.FakeSelfSpawn 256 256 QuestReward"));
			}
			FTimerHandle H1;
			Wd->GetTimerManager().SetTimer(H1, FTimerDelegate::CreateLambda([WW, bQuit]()
			{
				UWorld* W2 = WW.Get();
				if (!W2) { AsdaCVarSave::RestoreAll(FAsdaQuestJobChangeSelfTest::kCmdReward); return; }
				FAsdaQuestJobChangeSelfTest::RunRewardLines(W2);
				FAsdaQuestJobChangeSelfTest::FinishRewardLines(W2);
				AsdaSelfTestKit::Quit(W2, bQuit);
			}), 4.f, false);
		};
		if (Delay <= 0.f) { Start(); return; }
		FTimerHandle H;
		W->GetTimerManager().SetTimer(H, FTimerDelegate::CreateLambda(Start), Delay, false);
	}));

// ★ 22.10.2026 — asda.Quest.SelfTestJobChangeSkills [delay=N] [quit] — офлайн, тот же запуск (alpen + свой павн), прогон через 4 с.
static FAutoConsoleCommandWithWorldAndArgs GAsdaQuestJobChangeSkillsSelfTest(
	TEXT("asda.Quest.SelfTestJobChangeSkills"),
	TEXT("asda.Quest.SelfTestJobChangeSkills [delay=N] [quit] — ОФЛАЙН-самотест: очко вкладывается в навык новой профессии без ")
	TEXT("перезахода — 5042 «Лучник» и 5052 «Воин» + 5252 базовых навыков (с паддингом), 931→932 по 5252; 5253 под перехватом"),
	FConsoleCommandWithWorldAndArgsDelegate::CreateLambda([](const TArray<FString>& A, UWorld* W)
	{
		const FAsdaSelfTestArgs KitArgs = FAsdaSelfTestArgs::Parse(A);
		if (AsdaSelfTestKit::RefuseStart(W, FAsdaQuestJobChangeSelfTest::kCmdSkills, KitArgs)) { return; }
		if (!W) { return; }
		const bool bQuit = KitArgs.bQuit;
		TWeakObjectPtr<UWorld> WW = W;
		auto Start = [WW, bQuit]()
		{
			UWorld* Wd = WW.Get();
			if (!Wd || !GEngine) { return; }
			if (AsdaSelfTestKit::RefuseDeferred(Wd, FAsdaQuestJobChangeSelfTest::kCmdSkills, bQuit)) { return; }
			APlayerController* P = Wd->GetFirstPlayerController();
			// ★ 12.11.2026 [ИСПРАВЛЕНИЕ теста]: номер карты выставляется ВСЕГДА (тем же крюком, что и раньше), а не только когда
			//   своего персонажа ещё нет: в пачке после другого самотеста персонаж уже есть, карта оставалась чужой (0), и проверки
			//   «карта = 3» и живых меток NPC падали в зависимости от соседа по пачке. Сами проверки не ослаблены.
			GEngine->Exec(Wd, TEXT("Asda.Load.CurrentMap 3"));
			if (!P || !P->GetPawn())
			{
				GEngine->Exec(Wd, TEXT("Asda.World.FakeSelfSpawn 256 256 QuestSkills"));
			}
			FTimerHandle H1;
			Wd->GetTimerManager().SetTimer(H1, FTimerDelegate::CreateLambda([WW, bQuit]()
			{
				UWorld* W2 = WW.Get();
				if (!W2) { AsdaCVarSave::RestoreAll(FAsdaQuestJobChangeSelfTest::kCmdSkills); return; }
				FAsdaQuestJobChangeSelfTest::RunSkills(W2);
				// Кадр Slate между (в) и проверкой геометрии: `GetCachedGeometry` обновляется только отрисовкой.
				FTimerHandle H2;
				W2->GetTimerManager().SetTimer(H2, FTimerDelegate::CreateLambda([WW, bQuit]()
				{
					UWorld* W3 = WW.Get();
					if (!W3) { AsdaCVarSave::RestoreAll(FAsdaQuestJobChangeSelfTest::kCmdSkills); return; }
					FAsdaQuestJobChangeSelfTest::CheckSkillsGeometry(W3);
					FAsdaQuestJobChangeSelfTest::FinishSkills(W3);
					AsdaSelfTestKit::Quit(W3, bQuit);
				}), 0.5f, false);
			}), 4.f, false);
		};
		if (KitArgs.Delay <= 0.f) { Start(); return; }
		FTimerHandle H;
		W->GetTimerManager().SetTimer(H, FTimerDelegate::CreateLambda(Start), KitArgs.Delay, false);
	}));

// ★ 26.10.2026 — asda.Quest.SelfTestJob4 [delay=N] [quit] — офлайн, тот же запуск (alpen + свой павн), прогон через 4 с,
//   затем тик на 5256, тик на показ после 5257 (кадры Slate — для живой геометрии кнопки 12401).
static FAutoConsoleCommandWithWorldAndArgs GAsdaQuestJob4SelfTest(
	TEXT("asda.Quest.SelfTestJob4"),
	TEXT("asda.Quest.SelfTestJob4 [delay=N] [quit] — ОФЛАЙН-самотест 4-й профессии путём игрока: вход 1017 (класс 14, подкласс 15) + 5251, ")
	TEXT("кнопка 12401 и дерево ветки 11, клик по узлу → 5253/5254, применение → 5256, 5257 → показ (клип 188…201 + эффекты); контроль: ")
	TEXT("ключ asda.Job4.StageFromSubJob 0, 3-я ступень, ремесленный подкласс, 5042 смены профессии"),
	FConsoleCommandWithWorldAndArgsDelegate::CreateLambda([](const TArray<FString>& A, UWorld* W)
	{
		const FAsdaSelfTestArgs KitArgs = FAsdaSelfTestArgs::Parse(A);
		if (AsdaSelfTestKit::RefuseStart(W, FAsdaQuestJobChangeSelfTest::kCmdJob4, KitArgs)) { return; }
		if (!W) { return; }
		const bool bQuit = KitArgs.bQuit;
		TWeakObjectPtr<UWorld> WW = W;
		auto Start = [WW, bQuit]()
		{
			UWorld* Wd = WW.Get();
			if (!Wd || !GEngine) { return; }
			if (AsdaSelfTestKit::RefuseDeferred(Wd, FAsdaQuestJobChangeSelfTest::kCmdJob4, bQuit)) { return; }
			APlayerController* P = Wd->GetFirstPlayerController();
			// ★ 12.11.2026 [ИСПРАВЛЕНИЕ теста]: номер карты выставляется ВСЕГДА (тем же крюком, что и раньше), а не только когда
			//   своего персонажа ещё нет: в пачке после другого самотеста персонаж уже есть, карта оставалась чужой (0), и проверки
			//   «карта = 3» и живых меток NPC падали в зависимости от соседа по пачке. Сами проверки не ослаблены.
			GEngine->Exec(Wd, TEXT("Asda.Load.CurrentMap 3"));
			if (!P || !P->GetPawn())
			{
				GEngine->Exec(Wd, TEXT("Asda.World.FakeSelfSpawn 256 256 QuestJob4"));
			}
			FTimerHandle H1;
			Wd->GetTimerManager().SetTimer(H1, FTimerDelegate::CreateLambda([WW, bQuit]()
			{
				UWorld* W2 = WW.Get();
				if (!W2) { AsdaCVarSave::RestoreAll(FAsdaQuestJobChangeSelfTest::kCmdJob4); return; }
				FAsdaQuestJobChangeSelfTest::RunJob4(W2);
				FTimerHandle H2;
				W2->GetTimerManager().SetTimer(H2, FTimerDelegate::CreateLambda([WW, bQuit]()
				{
					UWorld* W3 = WW.Get();
					if (!W3) { AsdaCVarSave::RestoreAll(FAsdaQuestJobChangeSelfTest::kCmdJob4); return; }
					FAsdaQuestJobChangeSelfTest::Job4Cast(W3);
					// Окно навыков снова открыто — к (д) отрисуется кадр.
					if (UAsdaSkillWindowSubsystem* SW = W3->GetGameInstance()->GetSubsystem<UAsdaSkillWindowSubsystem>()) { SW->OpenWindow(); }
					FTimerHandle H3;
					W3->GetTimerManager().SetTimer(H3, FTimerDelegate::CreateLambda([WW, bQuit]()
					{
						UWorld* W4 = WW.Get();
						if (!W4) { AsdaCVarSave::RestoreAll(FAsdaQuestJobChangeSelfTest::kCmdJob4); return; }
						FAsdaQuestJobChangeSelfTest::Job4Visual(W4);
						FAsdaQuestJobChangeSelfTest::FinishJob4(W4);
						AsdaSelfTestKit::Quit(W4, bQuit);
					}), 0.25f, false);
				}), 0.5f, false);
			}), 4.f, false);
		};
		if (KitArgs.Delay <= 0.f) { Start(); return; }
		FTimerHandle H;
		W->GetTimerManager().SetTimer(H, FTimerDelegate::CreateLambda(Start), KitArgs.Delay, false);
	}));
#endif // ASDA_DEV_CONSOLE

#endif // !UE_BUILD_SHIPPING
