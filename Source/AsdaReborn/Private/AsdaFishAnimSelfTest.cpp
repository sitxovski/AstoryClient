// Copyright (c) 2026 @sitxovski (Discord)
// ★★★ САМОТЕСТ РЫБАЛКИ, ВОЛНА В5 (08.10.2026): действия 60…67, удочка, рыба, эффекты — свои и чужих. [ПОРТ]
//
// ★ ЗАЧЕМ. Сервера офлайн нет; путь игрока проверяется так:
//   • ПРИЁМЫ (6168/6170/6172/6173/4025) — штатный `UAsdaNetSubsystem::DebugFeedPacket` → `HandlePacket` → делегаты →
//     очереди подсистем → их Tick (второй копии логики нет); ОТПРАВКИ — под перехватом `ArmSendCapture` (в сеть не уходят);
//   • ВРЕМЯ — НАСТОЯЩЕЕ: клипы играют на скелете, конец клипа — таймер персонажа, мост В1 ВЫКЛЮЧЕН (`asda.Fish.ClipBridge 0`),
//     т.е. автомат двигает только конец реального клипа; мини-игра — встроенный эмулятор (раунд «отвечается» `EmuQueueRound`);
//   • «ЧТО РЕАЛЬНО ИГРАЕТ СКЕЛЕТ» — имя ассета у single-node инстанса (как сторож эмоций `TickEmotionWatch`) у персонажа и удочки;
//   • ПОСАДКА — мировые точки костей: `8a1_dummy` удочки = `8_a1` персонажа; `fs_Bone01` рыбы = `Bone24` удочки (62) / `_8_a1` (65).
//   Контрольные пары: сосед без удочки — удочка не появляется; 4025 с stateId вне таблицы (120) рыбалку не трогает
//   (★ В45fix2: 130 — состояние 68, ПЕРЕБИВАЕТ 60…67, B24d…B24g; 109 — состояние 9, удочка до конца вставания, B24…B24c;
//   «залипание» скрытого оружия при флаге 0 в «занят», B24h…B24j);
//   повторный вход в 65 без смены действия — второго эффекта нет ([15100]); на 60/61/63 рыба не видна.
// ★ В5fix (09.10.2026, ревизия В5 ВАЖН-3): клип РЕАЛЬНО идёт (`IsPlaying`, рост `GetCurrentTime` у персонажа и удочки,
//   позиция клипа 60 к переходу дошла до конца); РЕАЛЬНАЯ скрытость частей (компонент `WeaponMeshTag`, акторы
//   `WeaponGlowFx`/`ShieldGlowFx` = аура щита — ВАЖН-1); экип соседа — пакетом 4021; движение/покой/встать/GM — пакетом 4025
//   (1 / 0 / 400 / 109 / 300 / 301 — ВАЖН-2); регресс-пары: эмоция (своя после рыбалки и отказ во время, соседа — 4025 100),
//   навык и атака соседа (общие точки показа — пакетам нужен живой бой), эталонное состояние для короны/клавиатуры,
//   смена карты при живом действии; ключи `asda.Fish.LegacyRodClip0`, `asda.Fish.LegacyHookNoRod`; фолбэк удочки только
//   без img-записи (МЕЛК-3); без перезагрузки по смене оружия (МЕЛК-1); страховка моста при сорванном клипе (МЕЛК-5).
//
// ★ fishfix2 (09.10.2026, ревизия fishing_fixes_REVIEW СРЕДН-1): раздел D — НАСТОЯЩАЯ смерть пакетом 4053 (поза трупа у актора,
//   серверное «мёртв») на клипах заброса (60), вытаскивания (62) и итога (65): клип рыбалки на труп не ставится, 4024 соседям
//   не уходит, автомат доходит до 0 страховкой по таймеру .jbb, поза трупа (anim_18) остаётся; воскрешение — пакетом 4056.
//   Контрольные пары ключом `asda.Fish.LegacyDeadClip 1` (дефект оригинала воспроизводится) и труп соседа по 4025 122.
// ★ six_items fix (09.10.2026, ревизия СРЕДН-1/СРЕДН-2): B45 — лопата соседа 40/43 снимается САМА концом клипа (6.000 / 9.034 с),
//   контроль `asda.Shovel.ClipEnd 0`; раздел E — свой: код 11 пакета 5029 → 4008 + 4024 110/114 + 40/43, конец клипа → 5428,
//   смена состояния снимает таймер, гейт «состояние ≠ 0» (сидение / рыбалка / ход) — копание не начинается; контроль `asda.Shovel.NetSync 0`.
// ★ autoshovel (09.10.2026, ревизия six_fixes п. 6 / СРЕДН-1 / МЕЛК-1): раздел F — цикл автолопаты путём игрока: двойной клик
//   по ячейке → 5028 (слот запомнен) → 5029 код 11 → 43 → конец копки → 13 с → 5012 к добыче 5005 → 5013/5015 → повтор 5028 —
//   три цикла (третий без добычи); 9002 при повторном применении во время копки; остановки: ход, смерть, 7505, ручная лопата,
//   9001, смена карты, разрыв (МЕЛК-1: 43 и таймер клипа сняты, 5428 нет); [ПОРТ] 2040 / 9006 / чужой код в слоте / отказ 5029
//   с контрольными парами ключом `asda.Shovel.AutoLoopFix 0`; контроль `asda.Shovel.AutoLoop 0`.
// ★ копание (10.10.2026, `Reports/owner_1010/digging.md`): раздел G — клип копания (anim_233/234), удары по порогам combo time
//   (40: 0.6/2.066/3.566/5.1 с; 43: + перемотка → 5.1/6.6/8.134 с), эффект удара c-shovel-a_00, инструмент на 8_a1 (ручной/авто),
//   стартовые эффекты (свой a_01, сосед 4025 a_01/b_01, появление в 110/114 с привязкой 6), итог 5430 штатным разбором —
//   нотис 9003/9004 только своему, эффекты c-suc/c-fail и звук всем; контроли `asda.Shovel.ToolMeshFix 0` и `asda.Shovel.Clip 0`.
//   Ключ `shot` — кадры копания своего (FScreenshotRequest, Saved*/Screenshots).
// Консоль: `asda.Fish.AnimSelfTest [delay=N] [quit] [shot]`. Только офлайн. Строки «САМОТЕСТ-РЫБА-АНИМ ok/FAIL …», итог — «… ИТОГ: ok=… fail=…».
#include "CoreMinimal.h"

#if !UE_BUILD_SHIPPING

#include "World/AsdaFishingSubsystem.h"
#include "UI/AsdaNotice.h"                 // ★ 05.11.2026: AsdaNotice::Loc — ожидаемый текст нотисов 9003/9004 на языке сессии
#include "AsdaCVarSave.h"                   // ★ ИСПРАВЛЕНИЕ 11.10.2026 (В0, N1(д)/U8): ключи консоли самотеста — запомнить и вернуть (значение + SetBy)
#include "World/AsdaFishAnimSubsystem.h"
#include "World/AsdaPlayerCharacter.h"
#include "World/AsdaWorldSubsystem.h"
#include "World/AsdaLoadManagerSubsystem.h"
#include "AsdaNetSubsystem.h"
#include "Test/AsdaSelfTestKit.h"   // ★ 11.10.2026 (В3 S1): общий каркас самотестов (аргументы, отказ, перехват, итог, выход)
#include "AsdaBytes.h"
#include "AsdaSessionReset.h"
#include "AsdaEffectActor.h"
#include "AsdaFxNames.h"   // ★ дедуп №2 В1-C (F6): теги свечения заточки
#include "UI/AsdaWindowSubsystem.h"
#include "World/AsdaKingCrownSubsystem.h"
#include "World/AsdaPlayerController.h"
#include "Skill/AsdaSkillFxSubsystem.h"
#include "Engine/StaticMesh.h"
#include "Engine/SkeletalMesh.h"
#include "UI/AsdaInventorySubsystem.h"
#include "Items/AsdaItemTableSubsystem.h"
#include "Characters/AsdaCharAssembly.h"
#include "Components/SkeletalMeshComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Animation/AnimSingleNodeInstance.h"
#include "Animation/AnimSequence.h"
#include "Containers/Ticker.h"
#include "Engine/Engine.h"
#include "Engine/GameInstance.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "GameFramework/PlayerController.h"
#include "HAL/IConsoleManager.h"
#include "TimerManager.h"
#include "World/AsdaCoord.h"               // ★ autoshovel: свой тайл (раздел F)
#include "World/AsdaLootActor.h"           // ★ autoshovel: наземная добыча 5005 (раздел F)
#include "UI/AsdaEffectsSubsystem.h"       // ★ autoshovel: состояние (15,2) «на транспорте» — гейт 2040
#include "Items/AsdaItemUseStateGate.h"     // ★ 05.11.2026: гейт 7007 — раздел Z (оглушение во время автолопаты)
#include "UI/AsdaPremiumShopSubsystem.h"   // ★ autoshovel: признак «элитный магазин открыт» — гейт 9001
#include "AsdaSelf.h"                      // ★ autoshovel: свой номер для состояния (15,2)
#include "Misc/OutputDevice.h"             // ★ autoshovel: перехват журнала (нотисы)
#include "Misc/ScopeLock.h"
#include "UnrealClient.h"                   // ★ копание: FScreenshotRequest — кадры раздела G (ключ shot)
#include "Materials/MaterialInterface.h"      // ★ яма копания: режим смешивания материала ямы (раздел G)
#include "World/AsdaWalkGrid.h"             // ★ копание: суша для кадров (ключ shot)
#include "World/AsdaGroundTrace.h"          // ★ копание: сетка карты (AsdaGround::GetGrid)
#include "Engine/StaticMesh.h"              // ★ копание: имя меша инструмента

DEFINE_LOG_CATEGORY_STATIC(LogAsdaFishAnimTest, Log, All);

// Друг `UAsdaNetSubsystem` и `UAsdaFishingSubsystem` (объявлено в их заголовках).
struct FAsdaFishAnimSelfTest
{
	// Перехваченная отправка.
	struct FSent
	{
		int32 Op = 0;          // опкод
		TArray<uint8> P;       // тело (без конверта)
	};
	// Шаг сценария: true — шаг завершён (переход к следующему). Аргумент — секунды с начала шага.
	using FStep = TFunction<bool(float)>;

	int32 Ok = 0, Fail = 0;              // пройдено / провалено
	TArray<FSent> Sent;                  // всё перехваченное
	uint32 CaptureGen = 0;               // поколение перехватчика
	TWeakObjectPtr<UWorld> World;        // мир прогона
	UGameInstance* GI = nullptr;         // экземпляр игры
	UAsdaNetSubsystem* Net = nullptr;    // сеть
	UAsdaFishingSubsystem* F = nullptr;  // автомат рыбалки
	UAsdaFishAnimSubsystem* FA = nullptr; // показ рыбалки
	UAsdaWorldSubsystem* WS = nullptr;   // мир
	UAsdaWindowSubsystem* WM = nullptr;  // окна-сообщения
	UAsdaInventorySubsystem* Inv = nullptr; // инвентарь
	UAsdaItemTableSubsystem* Items = nullptr; // таблица предметов
	UAsdaLoadManagerSubsystem* LM = nullptr;  // карта
	TWeakObjectPtr<AAsdaPlayerCharacter> Me;  // свой персонаж
	TWeakObjectPtr<AAsdaPlayerCharacter> Peer;   // сосед с удочкой
	TWeakObjectPtr<AAsdaPlayerCharacter> Peer2;  // сосед БЕЗ удочки (контроль)
	TArray<FStep> Steps;                 // сценарий
	int32 StepIdx = 0;                   // текущий шаг
	double StepStartS = 0.0;             // начало шага (время мира)
	bool bQuit = false;                  // выйти после прогона
	FTSTicker::FDelegateHandle TickH;    // тикер сценария
	// Снимок для восстановления.
	int32 BridgeWas = 0, MapWas = 0, CharIdWas = 0;
	IAsdaFishMiniGame* MgWas = nullptr;
	TWeakObjectPtr<UObject> MgOwnerWas;
	FVector2D PosWas = FVector2D::ZeroVector;
	int32 Act63Seen = 0;                 // какое действие подсечки выпало (63/64)
	int32 ShieldId = 0;                  // щит со свечением-аурой (таблица предметов, заточка 7) — для ВАЖН-1
	int32 SwordId = 0;                   // оружие НЕ удочка с мешом и свечением — сосед без удочки, своя удочка «img без меша»
	float MaxT60 = -1.f;                 // наибольшая позиция клипа 60 у своего, пока действие 60 (клип реально доигран)
	float Len60 = 0.f;                   // длина клипа 60 (ассет)
	float MarkCharT = 0.f;               // позиция клипа персонажа на отметке (проверка «время идёт»)
	float MarkRodT = 0.f;                // позиция клипа удочки на отметке
	int32 DeathMark = 0;                 // ★ fishfix2: индекс в `Sent` на момент смерти (что ушло ПОСЛЕ неё)
	// ★ six_items (09.10.2026, ревизия СРЕДН-1/СРЕДН-2): лопата 40/43 — конец по клипу, гейт состояния, сеть.
	int32 ShovelId = 0;                  // предмет-лопата (тип 11, подтип 33) из таблицы предметов
	int32 AutoShovelId = 0;              // предмет-авто-лопата (тип 11, подтип 34)
	int32 ShovelMark = 0;                // индекс в `Sent` на момент подачи кода 11 (что ушло ПОСЛЕ неё)
	double ShovelT0 = 0.0;               // время мира, когда состояние 40/43 встало (замер длительности)
	int32 WeightCurWas = -1;             // вес персонажа до раздела E (гейт веса 90 % не должен мешать)
	int32 WeightMaxWas = -1;             // максимальный вес до раздела E
	bool bWeightSaved = false;           // вес сохранён разделом E (восстановить в Finish)
	// ★ autoshovel (09.10.2026, ревизия six_fixes п. 6): раздел F — цикл автолопаты.
	// Перехватчик журнала: нотисы инвентаря видны строкой «[инвентарь] нотис N: …» (тот же приём, что у самотеста О2).
	class FLogSink : public FOutputDevice
	{
	public:
		TArray<FString> All;    // все строки
		FCriticalSection Lock;  // журнал может прийти с любого потока
		virtual void Serialize(const TCHAR* V, ELogVerbosity::Type, const FName&) override
		{
			if (!V) { return; }
			FScopeLock L(&Lock);
			All.Add(V);
		}
		virtual bool CanBeUsedOnAnyThread() const override { return true; }
		virtual bool CanBeUsedOnMultipleThreads() const override { return true; }   // ★ 05.11.2026: НЕбуферизованный список UE 5.8 — строка доходит в момент записи (иначе гонка с потоком логов: строка после RemoveOutputDevice/проверки)
		// Очистка (перед ней — доставка фонового журнала).
		void Reset() { if (GLog) { GLog->Flush(); } FScopeLock L(&Lock); All.Reset(); }
		// Сколько строк содержат подстроку.
		int32 Hits(const TCHAR* Needle)
		{
			if (GLog) { GLog->Flush(); }
			FScopeLock L(&Lock);
			int32 N = 0;
			for (const FString& S : All) { if (S.Contains(Needle)) { ++N; } }
			return N;
		}
	};
	FLogSink* Sink = nullptr;            // перехват журнала раздела F (снимается в Finish)
	int32 DigCell = -1;                  // ячейка расходников (контейнер 2) с авто-лопатой
	int32 DigMark = 0;                   // индекс в `Sent` на начало проверки раздела F
	int32 StunRefusals0 = 0;             // ★ 05.11.2026: отказов гейта 7007 до раздела Z (оглушение во время автолопаты)
	FIntPoint DigLootTile = FIntPoint(0, 0);   // тайл добычи текущего цикла
	TArray<int32> DigFilledCells;        // ячейки расходников, забитые для 7505 (очистить)
	TArray<int32> DigBagCells;           // ячейки сумки, куда лёг подбор 5013 (очистить)
	bool bShopFlagSet = false;           // поднят признак «элитный магазин открыт» (снять в Finish)
	// ★ копание (10.10.2026): раздел G.
	bool bShot = false;                  // снимать кадры копания (аргумент `shot`)
	TArray<double> StrikeAt;             // моменты ударов от входа в состояние, с
	TArray<float> StrikeDt;              // длина кадра, на котором замечен удар (наибольшая из двух последних), с (★ R5-fix6-c (13.11.2026))
	float PrevFrameDt = 0.f;             // длина прошлого кадра наблюдателя, с
	int32 StrikeSeen = 0;                // счётчик ударов персонажа, уже учтённый
	double GT0 = 0.0;                    // время мира на входе в состояние копания
	float TrackAfter4 = -1.f;            // время трека 43 сразу после 4-го удара, мс (перемотка на порог [1])
	bool bHitFxOk = true;                // после каждого удара жив эффект удара c-shovel-a_00
	int32 FxSuc0 = 0;                    // эффектов c-suc-a_00 в мире до подачи 5430
	int32 FxFail0 = 0;                   // эффектов c-fail-a_00 в мире до подачи 5430
	static constexpr int32 kPeer3Num = 4403;   // сосед, появившийся копающим авто-лопатой (4065@102 = 114)
	static constexpr int32 kPeer4Num = 4404;   // сосед, появившийся копающим лопатой (4065@102 = 110)
	static constexpr int32 kPeer5Num = 4405;   // сосед раздела G (прежних соседей снял сброс сессии S12)
	TWeakObjectPtr<AAsdaPlayerCharacter> GPeer;      // сосед раздела G
	TWeakObjectPtr<AAsdaPlayerCharacter> StrikeWho;  // чьи удары записывает наблюдатель `OnTick` (каждый кадр, мимо очереди шагов)
	TArray<TPair<float, FString>> ShotPlan;          // кадры по времени от входа в состояние: (с, имя файла) — снимает наблюдатель
	// ★ яма копания (10.10.2026, `digging_hole.md`): наблюдатель `OnTick` за меш-эмиттером 0x1F стартового эффекта (`badakmeshnoalpha`).
	double HoleLastT = -1.0;             // последний момент от входа в состояние, когда секция ямы построена, с (−1 — не видели)
	bool bHoleSeen = false;              // секция ямы хоть раз построена
	bool bHoleOpaque = true;             // у ямы материал без бленда (Opaque/Masked) и нет парной ModAdd-секции (эталон: ALPHABLENDENABLE=0, ZWRITE=1)
	// Кадр с UI (ключ `shot`): имя файла без расширения.
	void Shot(const TCHAR* Name) { FScreenshotRequest::RequestScreenshot(Name, /*bShowUI=*/true, /*bAddFilenameSuffix=*/false); }
	// ★ копание: 5430 — итог копки (3 б): итог u8 (1 — удачно), userNum i16.
	static TArray<uint8> B5430(uint8 Result, int32 UserIdx)
	{
		TArray<uint8> B; B.SetNumZeroed(3); B[0] = Result; AsdaBytes::WrI16(B.GetData() + 1, (int16)UserIdx); return B;
	}

	static constexpr int32 kRod = 69657;         // удочка 10/1, img 49106 → ch1_n_00_fird_00_00_0
	static constexpr int32 kRod2 = 69658;        // удочка, img 49107 → fird_01 (смена удочки)
	static constexpr int32 kBait = 69667;        // наживка 204
	static constexpr int32 kFish = 69577;        // карась, тип 1, модель fs_fsm_crucian_00
	static constexpr int32 kJunk = 69649;        // мусор, тип 4, модель fs_stuff_00 (масштаб 2.0)
	static constexpr int32 kPeerNum = 4401;      // userNum соседа с удочкой (charId = 900000 + userNum)
	static constexpr int32 kPeer2Num = 4402;     // userNum соседа без удочки
	static constexpr int32 kSkillId = 1228;      // навык с клипом у обоих полов (`DT_SkillClips`, anim_161/162) — регресс-пара навыка

	// Одна проверка.
	void Expect(bool b, const FString& What)
	{
		if (b) { ++Ok; UE_LOG(LogAsdaFishAnimTest, Log, TEXT("САМОТЕСТ-РЫБА-АНИМ ok   %s"), *What); }
		else   { ++Fail; UE_LOG(LogAsdaFishAnimTest, Error, TEXT("САМОТЕСТ-РЫБА-АНИМ FAIL %s"), *What); }
	}
	// Подать пакет штатным разбором (очереди разберёт Tick подсистем).
	void Feed(int32 Op, const TArray<uint8>& Body) { Net->DebugFeedPacket(Op, Body); }
	// Сколько раз ушёл опкод (с телом stateId для 4024 — первым u16 после userNum).
	int32 Count(int32 Op) const { int32 N = 0; for (const FSent& S : Sent) { N += (S.Op == Op) ? 1 : 0; } return N; }
	// Ушёл ли 4024 со stateId: тело 29 б = MD5(16) + {u16 userNum, u16 stateId, u8 шлем, f32 X, f32 Z}, конверт 'R'.
	bool Sent4024(int32 StateId) const
	{
		for (const FSent& S : Sent)
		{
			if (S.Op == 4024 && S.P.Num() == 29 && AsdaBytes::RdU16(S.P.GetData() + 18) == StateId
				&& AsdaBytes::RdU16(S.P.GetData() + 16) == Net->SelfUserNum) { return true; }
		}
		return false;
	}
	// Имя анимации single-node инстанса компонента.
	static FString ClipOf(const USkeletalMeshComponent* C)
	{
		const UAnimSingleNodeInstance* SN = C ? C->GetSingleNodeInstance() : nullptr;
		return SN ? GetNameSafe(SN->GetAnimationAsset()) : FString();
	}
	// Клип персонажа = anim_(225 + действие − 60)?
	static bool CharClipIs(const AAsdaPlayerCharacter* C, int32 Action)
	{
		return C && C->GetPlayingClipName().EndsWith(FString::Printf(TEXT("anim_%d"), AAsdaPlayerCharacter::kFishClipBase + Action - 60));
	}
	// Клип удочки = anim_0N, N = действие − 60?
	static bool RodClipIs(const AAsdaPlayerCharacter* C, int32 Action)
	{
		return C && C->GetFishRodComp() && C->GetFishRodClip() == Action - 60
			&& ClipOf(C->GetFishRodComp()).EndsWith(FString::Printf(TEXT("anim_%02d"), Action - 60));
	}
	// Расстояние между двумя костями в мире.
	static float BoneDist(const USkeletalMeshComponent* A, FName BA, const USkeletalMeshComponent* B, FName BB)
	{
		if (!A || !B || A->GetBoneIndex(BA) == INDEX_NONE || B->GetBoneIndex(BB) == INDEX_NONE) { return 1.e9f; }
		return (float)FVector::Dist(A->GetSocketLocation(BA), B->GetSocketLocation(BB));
	}
	// Удочка сидит в руке: `8a1_dummy` = `8_a1` и прикреплена к кости 8_a1.
	static float RodHandDist(const AAsdaPlayerCharacter* C)
	{
		return C ? BoneDist(C->GetFishRodComp(), FName(TEXT("8a1_dummy")), C->BaseSkel, FName(TEXT("8_a1"))) : 1.e9f;
	}
	// Эффектов fish_end_0N в мире.
	int32 FxCount(const TCHAR* Jeb) const
	{
		int32 N = 0;
		if (UWorld* W = World.Get())
		{
			for (TActorIterator<AAsdaEffectActor> It(W); It; ++It)
			{
				if (IsValid(*It) && It->GetClass()->GetName().Contains(Jeb)) { ++N; }
			}
		}
		return N;
	}
	// Масштаб рыбы относительно масштаба тела.
	static float FishScale(const AAsdaPlayerCharacter* C)
	{
		if (!C || !C->GetFishHookComp() || !C->BaseSkel) { return -1.f; }
		return (float)(C->GetFishHookComp()->GetComponentScale().GetAbs().X / FMath::Max(1e-6, C->BaseSkel->GetComponentScale().GetAbs().X));
	}
	// Тела пакетов.
	static TArray<uint8> B6168(uint8 Code, int32 V1, int32 Bonus)
	{
		TArray<uint8> B; B.SetNumZeroed(7); B[0] = Code; AsdaBytes::WrI32(B.GetData() + 1, V1); AsdaBytes::WrI16(B.GetData() + 5, Bonus); return B;
	}
	static TArray<uint8> B6170(uint8 Code, int32 Phase, int32 Gauge)
	{
		TArray<uint8> B; B.SetNumZeroed(5); B[0] = Code; AsdaBytes::WrI16(B.GetData() + 1, Phase); AsdaBytes::WrI16(B.GetData() + 3, Gauge); return B;
	}
	static void Rec57(uint8* R, int32 Code, int32 Cont, int32 Slot, int32 Amount)
	{
		FMemory::Memzero(R, 57); AsdaBytes::WrI32(R + 0, Code); R[4] = (uint8)Cont; AsdaBytes::WrI16(R + 5, Slot); AsdaBytes::WrI32(R + 9, Amount);
	}
	static TArray<uint8> B6172(uint8 Code, int32 Size, int32 FishCode, int32 FishSlot)
	{
		TArray<uint8> B; B.SetNumZeroed(121); B[0] = Code; AsdaBytes::WrI16(B.GetData() + 1, 10); AsdaBytes::WrI16(B.GetData() + 5, Size);
		Rec57(B.GetData() + 7, kBait, 3, 10, 4);
		if (FishCode > 0) { Rec57(B.GetData() + 64, FishCode, 1, FishSlot, 1); }
		return B;
	}
	static TArray<uint8> B6173(uint8 Phase, int32 CharId, int32 Fish, int32 Size)
	{
		TArray<uint8> B; B.SetNumZeroed(11); B[0] = Phase; AsdaBytes::WrI32(B.GetData() + 1, CharId); AsdaBytes::WrI32(B.GetData() + 5, Fish); AsdaBytes::WrI16(B.GetData() + 9, Size); return B;
	}
	// 4025: userIdx u16, actionType u16, шлем u8, направление X f32, Z f32 (13 б).
	static TArray<uint8> B4025(int32 UserIdx, int32 Act, float DX, float DZ)
	{
		TArray<uint8> B; B.SetNumZeroed(13); AsdaBytes::WrI16(B.GetData(), (int16)UserIdx); AsdaBytes::WrI16(B.GetData() + 2, (int16)Act);
		FMemory::Memcpy(B.GetData() + 5, &DX, 4); FMemory::Memcpy(B.GetData() + 9, &DZ, 4); return B;
	}
	// 4021: userIdx u16, экип-слот u16, itemId i32, заточка u8 (9 б, `Recv_CharFullSpawn@0xF3E970`).
	static TArray<uint8> B4021(int32 UserIdx, int32 Slot, int32 ItemId, uint8 Grade)
	{
		TArray<uint8> B; B.SetNumZeroed(9); AsdaBytes::WrI16(B.GetData(), (int16)UserIdx); AsdaBytes::WrI16(B.GetData() + 2, (int16)Slot);
		AsdaBytes::WrI32(B.GetData() + 4, ItemId); B[8] = Grade; return B;
	}
	// ★ fishfix2: 4053 — смерть: i16 userIdx, i16 killerIdx (4 б, `Recv_CharDeath_Apply@0xF42E50`).
	static TArray<uint8> B4053(int32 UserIdx)
	{
		TArray<uint8> B; B.SetNumZeroed(4); AsdaBytes::WrI16(B.GetData(), (int16)UserIdx); return B;
	}
	// ★ fishfix2: 4056 — воскрешение на месте: uidx@0, тайл @2/@4, HP i32@6, MP i16@10, опыт @12, state @20 (≠200 — живым), 22 б.
	static TArray<uint8> B4056(int32 UserIdx, int32 TileX, int32 TileZ)
	{
		TArray<uint8> B; B.SetNumZeroed(22); AsdaBytes::WrI16(B.GetData(), (int16)UserIdx); AsdaBytes::WrI16(B.GetData() + 2, (int16)TileX);
		AsdaBytes::WrI16(B.GetData() + 4, (int16)TileZ); AsdaBytes::WrI32(B.GetData() + 6, 300); AsdaBytes::WrI16(B.GetData() + 10, 50); return B;
	}
	// ★ six_items: 5029 (24 б, `OnRecv_UseItemResult@0xD0B1E0`): код результата @0, userNum @1, itemCode @7. Код 11 — «копать».
	static TArray<uint8> B5029(int32 Result, int32 UserIdx, int32 ItemCode)
	{
		TArray<uint8> B; B.SetNumZeroed(24); B[0] = (uint8)Result; AsdaBytes::WrI16(B.GetData() + 1, (int16)UserIdx);
		AsdaBytes::WrI32(B.GetData() + 7, ItemCode); return B;
	}
	// ★ six_items: ушёл ли после From 4024 со stateId (тело 29 б: MD5 16 + {u16 userNum, u16 stateId, …}).
	bool Sent4024From(int32 StateId, int32 From) const
	{
		for (int32 i = FMath::Max(0, From); i < Sent.Num(); ++i)
		{
			const FSent& S = Sent[i];
			if (S.Op == 4024 && S.P.Num() == 29 && AsdaBytes::RdU16(S.P.GetData() + 18) == StateId) { return true; }
		}
		return false;
	}
	// ★ six_items: ушёл ли после From 5428 с телом эталона {u16 свой userNum, u8 1} (3 б, конверт 'S').
	bool Sent5428From(int32 From) const
	{
		for (int32 i = FMath::Max(0, From); i < Sent.Num(); ++i)
		{
			const FSent& S = Sent[i];
			if (S.Op == 5428 && S.P.Num() == 3 && AsdaBytes::RdU16(S.P.GetData()) == Net->SelfUserNum && S.P[2] == 1) { return true; }
		}
		return false;
	}
	// ★ autoshovel: 5005 — одна запись наземного лута (15 б): код i32, тайл X/Z i16, количество i32, дропнувший i16 (−1), байт.
	static TArray<uint8> B5005(int32 Code, const FIntPoint& Tile)
	{
		TArray<uint8> B; B.SetNumZeroed(15); AsdaBytes::WrI32(B.GetData(), Code); AsdaBytes::WrI16(B.GetData() + 4, (int16)Tile.X);
		AsdaBytes::WrI16(B.GetData() + 6, (int16)Tile.Y); AsdaBytes::WrI32(B.GetData() + 8, 1); AsdaBytes::WrI16(B.GetData() + 12, -1); return B;
	}
	// ★ autoshovel: 5013 — свой подбор (62 б): код 1, подобравший −1 (= свой), запись 57 б с +3, вес i16 @60.
	static TArray<uint8> B5013(int32 Code, int32 BagCell)
	{
		TArray<uint8> B; B.SetNumZeroed(62); B[0] = 1; AsdaBytes::WrI16(B.GetData() + 1, -1); Rec57(B.GetData() + 3, Code, 1, BagCell, 1); return B;
	}
	// ★ autoshovel: 5015 — снять предмет с тайла (4 б).
	static TArray<uint8> B5015(const FIntPoint& Tile)
	{
		TArray<uint8> B; B.SetNumZeroed(4); AsdaBytes::WrI16(B.GetData(), (int16)Tile.X); AsdaBytes::WrI16(B.GetData() + 2, (int16)Tile.Y); return B;
	}
	// ★ autoshovel: первая перехваченная отправка опкода начиная с From (nullptr — нет).
	const FSent* FirstFrom(int32 Op, int32 From) const
	{
		for (int32 i = FMath::Max(0, From); i < Sent.Num(); ++i) { if (Sent[i].Op == Op) { return &Sent[i]; } }
		return nullptr;
	}
	// ★ autoshovel: свой тайл.
	FIntPoint MyTile() const { return Me.IsValid() ? AsdaCoord::GameToTile(Me->GetGamePosXZ()) : FIntPoint(0, 0); }
	// ★ autoshovel: секунд с последнего старта таймера цикла.
	double DigAge() const { return FPlatformTime::Seconds() - Inv->GetAutoDigTimer().StartS; }
	// ★ fishfix2: сколько раз ушёл опкод начиная с индекса From перехваченного.
	int32 CountFrom(int32 Op, int32 From) const
	{
		int32 N = 0; for (int32 i = FMath::Max(0, From); i < Sent.Num(); ++i) { N += (Sent[i].Op == Op) ? 1 : 0; } return N;
	}
	// ★ fishfix2: персонаж в позе трупа — клип death01 (ассет 18).
	static bool DeathClipIs(const AAsdaPlayerCharacter* C)
	{
		return C && C->AnimState == EAsdaPlayerState::Dead && C->GetPlayingClipName().EndsWith(TEXT("anim_18"));
	}
	// Позиция клипа single-node компонента (−1 — нет).
	static float ClipTime(const USkeletalMeshComponent* C)
	{
		const UAnimSingleNodeInstance* SN = C ? C->GetSingleNodeInstance() : nullptr;
		return SN ? SN->GetCurrentTime() : -1.f;
	}
	// Играет ли клип single-node компонента.
	static bool ClipPlaying(const USkeletalMeshComponent* C)
	{
		const UAnimSingleNodeInstance* SN = C ? C->GetSingleNodeInstance() : nullptr;
		return SN && SN->IsPlaying() && SN->GetAnimationAsset() != nullptr;
	}
	// РЕАЛЬНАЯ видимость частей «оружие/щит»: меши с `WeaponMeshTag`, акторы-эффекты WeaponGlowFx / ShieldGlowFx.
	struct FParts
	{
		int32 MeshVis = 0, MeshHid = 0;   // меш оружия видим/скрыт
		int32 WFxVis = 0, WFxHid = 0;     // свечение оружия видимо/скрыто
		int32 SFxVis = 0, SFxHid = 0;     // аура щита видима/скрыта
	};
	// Снять видимость частей персонажа.
	static FParts PartsOf(AAsdaPlayerCharacter* C)
	{
		FParts R;
		if (!C) { return R; }
		TArray<UStaticMeshComponent*> SMs;
		C->GetComponents<UStaticMeshComponent>(SMs);
		for (const UStaticMeshComponent* M : SMs)
		{
			if (M && M->ComponentHasTag(UAsdaCharAssembly::WeaponMeshTag)) { (M->bHiddenInGame ? R.MeshHid : R.MeshVis) += 1; }
		}
		TArray<AActor*> Att;
		C->GetAttachedActors(Att);
		for (const AActor* A : Att)
		{
			if (!A) { continue; }
			if (A->ActorHasTag(FName(AsdaCharFxTags::kWeaponGlow))) { (A->IsHidden() ? R.WFxHid : R.WFxVis) += 1; }
			if (A->ActorHasTag(FName(AsdaCharFxTags::kShieldGlow))) { (A->IsHidden() ? R.SFxHid : R.SFxVis) += 1; }
		}
		return R;
	}
	// Строка для журнала.
	static FString PartsStr(const FParts& P)
	{
		return FString::Printf(TEXT("меш %d/%d, свечение оружия %d/%d, аура щита %d/%d (видно/скрыто)"),
			P.MeshVis, P.MeshHid, P.WFxVis, P.WFxHid, P.SFxVis, P.SFxHid);
	}
	// Клип покоя персонажа = anim_NN от `IdleClipIdx()` (ревизия В5 ВАЖН-3 п.5 — не «что угодно, кроме рыбалки»).
	static bool IdleClipIs(const AAsdaPlayerCharacter* C)
	{
		return C && C->GetPlayingClipName().EndsWith(FString::Printf(TEXT("anim_%02d"), C->IdleClipIdx()));
	}
	// Эталонное состояние своего для клавиатуры (`KbEtalonActionState`), −1 — нет контроллера.
	int32 KbState() const
	{
		const AAsdaPlayerController* PC = World.IsValid() ? Cast<AAsdaPlayerController>(World->GetFirstPlayerController()) : nullptr;
		return PC ? PC->KbEtalonActionState() : -1;
	}
	// Выставить ключ консоли (int).
	static void SetCv(const TCHAR* Name, int32 V)
	{
		AsdaCVarSave::SetForTest(Name, V);
	}
	// Найти щит и НЕ-удочку-оружие, у которых при заточке 7 собран BP свечения (у оружия — и меш).
	void FindGlowItems()
	{
		for (const FAsdaItemRecord& Rec : Items->GetAllItems())
		{
			if (ShieldId == 0 && Rec.ItemType == 8)
			{
				const FString J = WS->BuildGlowJebName(Rec.ItemCode, 7, /*bShield*/true);
				if (!J.IsEmpty() && AAsdaEffectActor::ResolveJebClass(J)) { ShieldId = Rec.ItemCode; }
			}
			if (SwordId == 0 && Rec.ItemType == 9 && Rec.ItemSubType >= 0 && Rec.ItemSubType <= 5)
			{
				const FString J = WS->BuildGlowJebName(Rec.ItemCode, 7, /*bShield*/false);
				const FString M = Items->GetItemMeshName(Rec.ItemCode, true);
				if (!J.IsEmpty() && !M.IsEmpty() && AAsdaEffectActor::ResolveJebClass(J)
					&& LoadObject<UStaticMesh>(nullptr, *UAsdaCharAssembly::CharStaticMeshPath(M)))
				{
					SwordId = Rec.ItemCode;
				}
			}
			if (ShieldId && SwordId) { break; }
		}
	}
	// Свободная ячейка сумки (контейнер 1) для улова.
	int32 FreeBagCell() const
	{
		for (int32 c = 0; c < Inv->GetContainerSize(1); ++c) { if (Inv->GetCellItemCode(1, c) == 0) { return c; } }
		return 0;
	}

	void Build();
	bool Start(UWorld* W);
	void Finish();
	bool OnTick(float Dt);
};

bool FAsdaFishAnimSelfTest::Start(UWorld* W)
{
	World = W;
	GI = W ? W->GetGameInstance() : nullptr;
	Net = GI ? GI->GetSubsystem<UAsdaNetSubsystem>() : nullptr;
	F = GI ? GI->GetSubsystem<UAsdaFishingSubsystem>() : nullptr;
	FA = GI ? GI->GetSubsystem<UAsdaFishAnimSubsystem>() : nullptr;
	WS = GI ? GI->GetSubsystem<UAsdaWorldSubsystem>() : nullptr;
	WM = GI ? GI->GetSubsystem<UAsdaWindowSubsystem>() : nullptr;
	Inv = GI ? GI->GetSubsystem<UAsdaInventorySubsystem>() : nullptr;
	Items = GI ? GI->GetSubsystem<UAsdaItemTableSubsystem>() : nullptr;
	LM = GI ? GI->GetSubsystem<UAsdaLoadManagerSubsystem>() : nullptr;
	if (!Net || !F || !FA || !WS || !WM || !Inv || !Items || !LM) { Expect(false, TEXT("нет подсистем")); return false; }
	Me = F->GetSelfCharacter();
	if (!Me.IsValid()) { Expect(false, TEXT("нет своего персонажа")); return false; }
	// ── снимок и подготовка ──
	IConsoleVariable* CvBridge = IConsoleManager::Get().FindConsoleVariable(TEXT("asda.Fish.ClipBridge"));
	BridgeWas = CvBridge ? CvBridge->GetInt() : 0;
	if (CvBridge) { AsdaCVarSave::SetForTest(TEXT("asda.Fish.ClipBridge"), 0); }
	Expect(BridgeWas == 0, FString::Printf(TEXT("0 мост клипов В1 по умолчанию выключен (asda.Fish.ClipBridge = %d)"), BridgeWas));
	MapWas = LM->CurrentMapId;
	CharIdWas = Net->SelectedCharId;
	if (Net->SelectedCharId == 0) { Net->SelectedCharId = 0x00ABCDEF; }
	PosWas = Me->GetGamePosXZ();
	MgWas = F->GetRegisteredMiniGame();
	MgOwnerWas = F->GetRegisteredMiniGameOwner();
	F->SetMiniGame(nullptr);             // эмулятор гайда (окно В2 возвращается в конце)
	F->ResetSessionState(EAsdaSessionResetReason::Disconnected);
	F->SetAutoRepeat(false);             // после улова: 120 → 121 → 128 → 0 (без перезаброса)
	WM->ClearMsgBoxQueue(TEXT("самотест анимаций рыбалки: старт"));
	Inv->DebugPutItem(3, UAsdaFishingSubsystem::kEquipSlotRod, kRod, 1);
	Inv->DebugPutItem(3, UAsdaFishingSubsystem::kEquipSlotBait, kBait, 5);
	Me->SetWeaponFromItem(kRod, Items);  // модель оружия своего = удочка (4021/5001 своему офлайн нет)
	SetCv(TEXT("asda.Fish.LegacyRodClip0"), 0);
	SetCv(TEXT("asda.Fish.LegacyHookNoRod"), 0);
	SetCv(TEXT("asda.Fish.DebugFailClip"), 0);
	FindGlowItems();
	Expect(ShieldId > 0 && SwordId > 0, FString::Printf(TEXT("0 найдены щит %d и оружие %d со свечением заточки 7 (BP собран)"), ShieldId, SwordId));
	CaptureGen = AsdaSelfTestKit::ArmCapture(Net, [this](uint16 Op, const uint8* P, int32 Len, bool bPad, TCHAR Env)
	{
		FSent S; S.Op = Op; if (P && Len > 0) { S.P.Append(P, Len); } Sent.Add(MoveTemp(S));
		// ★ 12.10.2026 (дедуп №2, В3 S7): тело 4024/5428 целиком — сверка «байт в байт» до/после единого состояния действия
		//   (только строка журнала, не проверка; номер — порядковый номер отправки в прогоне).
		if (Op == 4024 || Op == 5428)
		{
			UE_LOG(LogAsdaFishAnimTest, Log, TEXT("САМОТЕСТ-РЫБА-АНИМ ПАКЕТ #%d op=%u len=%d pad=%d env=%c hex=%s"),
				Sent.Num(), Op, Len, bPad ? 1 : 0, Env ? Env : TEXT('?'), *BytesToHex(P, Len));
		}
	}, 0, TEXT("asda.Fish.AnimSelfTest"));
	Build();
	StepIdx = 0;
	StepStartS = W->GetTimeSeconds();
	TickH = FTSTicker::GetCoreTicker().AddTicker(FTickerDelegate::CreateLambda([this](float Dt) { return OnTick(Dt); }));
	return true;
}

void FAsdaFishAnimSelfTest::Finish()
{
	if (Net) { AsdaSelfTestKit::DisarmCapture(Net, CaptureGen); }
	if (F)
	{
		F->ResetSessionState(EAsdaSessionResetReason::Disconnected);
		F->SetMiniGame(MgWas, MgOwnerWas.Get());
	}
	// ★ ИСПРАВЛЕНИЕ 11.10.2026 (В0, N1(д)/U8): все ключи, тронутые прогоном (Fish.ClipBridge, Fish.LegacyRodClip0/LegacyHookNoRod/DebugFailClip/
	//   LegacyDeadClip, Shovel.ClipEnd/NetSync/AutoLoop/AutoLoopFix/Clip/ToolMeshFix/StartFxFix, Loot.PickPendingGuardSec), —
	//   к исходному значению И приоритету SetBy. Прежде мост возвращался прежним значением с SetByConsole, остальные —
	//   КОНСТАНТАМИ «умолчаний» (0/1, PickPendingGuardSec — 5) с SetByConsole, без учёта значения до прогона.
	AsdaCVarSave::RestoreAll(TEXT("asda.Fish.AnimSelfTest"));
	// ★ autoshovel: раздел F — снять перехват журнала, признак магазина, предметы, цикл.
	if (Sink && GLog) { GLog->RemoveOutputDevice(Sink); }
	delete Sink; Sink = nullptr;
	if (GI && bShopFlagSet)
	{
		if (UAsdaPremiumShopSubsystem* Shop = GI->GetSubsystem<UAsdaPremiumShopSubsystem>()) { Shop->DebugSetOpenFlag(false); }
	}
	if (Inv)
	{
		Inv->AutoDigReset(TEXT("самотест: конец"));
		if (DigCell >= 0) { Inv->DebugPutItem(2, DigCell, 0, 0); }
		for (const int32 Cc : DigFilledCells) { Inv->DebugPutItem(2, Cc, 0, 0); }
		for (const int32 Cc : DigBagCells) { Inv->DebugPutItem(1, Cc, 0, 0); }
		Inv->bActionPending = false;
	}
	if (Net && bWeightSaved) { Net->SelfWeightCur = WeightCurWas; Net->SelfWeightMax = WeightMaxWas; }
	// ★ fishfix2: раздел D оборвался мёртвым — воскресить штатным 4056 (иначе игра офлайн осталась бы трупом).
	if (Net && Net->bSelfDead) { Feed(4056, B4056(Net->SelfUserNum, Net->SelfTileX, Net->SelfTileZ)); }
	if (Me.IsValid() && Items) { Me->SetWeaponFromItem(kRod, Items); }
	if (Inv)
	{
		Inv->DebugPutItem(3, UAsdaFishingSubsystem::kEquipSlotRod, 0, 0);
		Inv->DebugPutItem(3, UAsdaFishingSubsystem::kEquipSlotBait, 0, 0);
	}
	if (Net) { Net->SelectedCharId = CharIdWas; }
	if (LM) { LM->CurrentMapId = MapWas; }
	if (WM) { WM->ClearMsgBoxQueue(TEXT("самотест анимаций рыбалки: конец")); }
	UE_LOG(LogAsdaFishAnimTest, Log, TEXT("САМОТЕСТ-РЫБА-АНИМ ИТОГ: ok=%d fail=%d"), Ok, Fail);
	AsdaSelfTestKit::Quit(World.Get(), bQuit);
}

bool FAsdaFishAnimSelfTest::OnTick(float /*Dt*/)
{
	UWorld* W = World.Get();
	if (!W || !Me.IsValid()) { Expect(false, TEXT("мир/персонаж пропали посреди прогона")); Finish(); delete this; return false; }
	const float T = (float)(W->GetTimeSeconds() - StepStartS);
	// Наблюдатель: позиция клипа своего действия 60 (доказательство, что переход дал конец РЕАЛЬНОГО клипа, а не таймер).
	if (Me->GetFishAction() == 60 && Me->GetPlayingClipName().EndsWith(TEXT("anim_225")))
	{
		MaxT60 = FMath::Max(MaxT60, ClipTime(Me->BaseSkel));
	}
	// ★ копание (раздел G): удары и кадры записываются КАЖДЫЙ КАДР — шаги очереди идут по одному и опоздали бы.
	if (AAsdaPlayerCharacter* X = StrikeWho.Get())
	{
		// Удар мог случиться на этом кадре или на прошлом (наблюдатель идёт до или после тика персонажа) — берём больший.
		const float FrameDt = W->GetDeltaSeconds();
		const float FrameBound = FMath::Max(PrevFrameDt, FrameDt);
		PrevFrameDt = FrameDt;
		const int32 Tot = X->GetShovelStrikeTotal();
		while (StrikeSeen < Tot)
		{
			++StrikeSeen;
			StrikeAt.Add(W->GetTimeSeconds() - GT0);
			StrikeDt.Add(FrameBound);
			const AActor* H = X->GetShovelHitFx();
			if (!H || !H->GetClass()->GetName().Contains(TEXT("c_shovel_a_00"))) { bHitFxOk = false; }
			if (X->GetEtalonShovelState() == 43 && StrikeAt.Num() == 4) { TrackAfter4 = X->GetShovelTrackMs(); }
		}
		for (int32 i = ShotPlan.Num() - 1; i >= 0; --i)
		{
			if (W->GetTimeSeconds() - GT0 >= ShotPlan[i].Key) { Shot(*ShotPlan[i].Value); ShotPlan.RemoveAt(i); }
		}
		// ★ яма копания: до какого момента построена секция меш-эмиттера 0x1F стартового эффекта и чем она рисуется.
		if (const AAsdaEffectActor* SFx = Cast<AAsdaEffectActor>(X->GetShovelStartFx()))
		{
			for (int32 e = 0; e < SFx->Emitters.Num(); ++e)
			{
				const FAsdaFxEmitter& Em = SFx->Emitters[e];   // эмиттер стартового эффекта
				if (Em.JebType != 0x1F) { continue; }
				FVector Cc; float Rr = 0.f; int32 Nv = 0;     // центроид, радиус, число вершин секции
				if (SFx->EmitterSectionStats(e, Cc, Rr, Nv) && Nv > 0) { HoleLastT = W->GetTimeSeconds() - GT0; bHoleSeen = true; }
				// ★ 11.10.2026 (fx_alphatest.md): мастер 0x1F — Masked (альфа-тест эталона ALPHAREF 8 при пункте свечения 4),
				//   бленда у него по-прежнему нет: Opaque и Masked оба «непрозрачные».
				const EBlendMode Bm = Em.Material ? Em.Material->GetBlendMode() : BLEND_Translucent;   // режим бленда ямы
				if (!Em.Material || (Bm != BLEND_Opaque && Bm != BLEND_Masked) || Em.MaterialB) { bHoleOpaque = false; }
			}
		}
	}
	while (Steps.IsValidIndex(StepIdx))
	{
		if (!Steps[StepIdx](T)) { return true; }   // шаг ждёт
		++StepIdx;
		StepStartS = W->GetTimeSeconds();
		return true;                               // следующий шаг — со следующего кадра
	}
	Finish();
	delete this;
	return false;
}

// Сценарий. Шаг возвращает true, когда условие выполнено ИЛИ вышел срок (тогда — FAIL с описанием).
void FAsdaFishAnimSelfTest::Build()
{
	AAsdaPlayerCharacter* C = Me.Get();
	// Ждать условия до Timeout секунд; по успеху — Then(); по сроку — FAIL What.
	auto WaitFor = [this](TFunction<bool()> Cond, float Timeout, FString What, TFunction<void()> Then)
	{
		return FStep([this, Cond, Timeout, What, Then](float T)
		{
			if (Cond()) { if (!What.IsEmpty()) { Expect(true, What); } if (Then) { Then(); } return true; }
			if (T > Timeout) { Expect(false, What + FString::Printf(TEXT(" — не дождались за %.1f с"), Timeout)); return true; }
			return false;
		});
	};
	// Сделать действие сразу (шаг на один кадр).
	auto Do = [](TFunction<void()> Fn) { return FStep([Fn](float) { Fn(); return true; }); };

	// ═══════════════ A. СВОЙ ЦИКЛ ═══════════════
	Steps.Add(Do([this, C]()
	{
		LM->CurrentMapId = 3;
		const bool bFound = F->StartAtNearestSpot();
		Expect(bFound && F->GetFishState() == 109, FString::Printf(TEXT("A0 зона рядом найдена, автомат 109 (сейчас %d)"), F->GetFishState()));
	}));
	Steps.Add(WaitFor([this]() { return WM->IsMsgBoxQueued(1, 81); }, 3.f, TEXT("A1 диалог 81"), [this]() { WM->CloseConfirm(true); }));
	Steps.Add(WaitFor([this]() { return F->GetFishState() == 107; }, 3.f, TEXT("A2 автомат 107 (6167 ушёл)"),
		[this]() { Expect(Count(6167) == 1, FString::Printf(TEXT("A2 6167 ушёл ровно 1 раз (%d)"), Count(6167))); Feed(6168, B6168(1, kFish, 100)); }));
	Steps.Add(WaitFor([C]() { return C->GetFishAction() == 60; }, 3.f, TEXT("A3 6168 код 1 → действие 60"), [this, C]()
	{
		Expect(CharClipIs(C, 60), FString::Printf(TEXT("A3 скелет играет клип заброса anim_225: '%s'"), *C->GetPlayingClipName()));
		Expect(ClipPlaying(C->BaseSkel), TEXT("A3 клип персонажа ИГРАЕТ (IsPlaying)"));
		const UAnimSingleNodeInstance* SN = C->BaseSkel ? C->BaseSkel->GetSingleNodeInstance() : nullptr;
		const UAnimSequence* Seq = SN ? Cast<UAnimSequence>(SN->GetAnimationAsset()) : nullptr;
		Len60 = Seq ? Seq->GetPlayLength() : 0.f;
		Expect(Sent4024(122), TEXT("A3 4024 stateId 122 соседям"));
		Expect(C->GetFishFlag() && F->GetSelfFishingFlag(), TEXT("A3 флаг [15036] = 1 (FSM 108) — персонажу передан"));
		Expect(C->bWeaponMotionHidden, TEXT("A3 оружие за спину (bActionBusy рыбалки)"));
		Expect(UAsdaKingCrownSubsystem::EtalonActionStateOf(C) == 60, FString::Printf(TEXT("A3 корона: эталонное состояние = 60 (%d), чётное — спаун не блокирует"),
			UAsdaKingCrownSubsystem::EtalonActionStateOf(C)));
		Expect(KbState() == 60, FString::Printf(TEXT("A3 клавиатура: KbEtalonActionState = 60 (%d) — ведёт себя как прежний покой"), KbState()));
	}));
	Steps.Add(WaitFor([C]() { return C->IsFishWeaponHidden(); }, 1.f, TEXT("A3a признак «оружие и щит скрыты» (флаг && занят: char[52136] = 0)"), nullptr));
	Steps.Add(WaitFor([]() { return true; }, 0.f, TEXT(""), [this, C]()
	{
		const FParts P = PartsOf(C);
		Expect(P.MeshVis == 0 && P.WFxVis == 0 && P.SFxVis == 0, FString::Printf(TEXT("A3b РЕАЛЬНО: видимых частей оружия/щита у своего нет — %s"), *PartsStr(P)));
	}));
	Steps.Add(WaitFor([C]() { return C->GetFishRodComp() != nullptr; }, 1.f, TEXT("A4 удочка вставлена"), [C, this]()
	{
		USkeletalMeshComponent* R = C->GetFishRodComp();
		Expect(C->GetFishRodMeshName() == TEXT("ch1_n_00_fird_00_00_0_00"), FString::Printf(TEXT("A4 меш удочки по img 49106 (%s)"), *C->GetFishRodMeshName()));
		Expect(R && R->GetAttachParent() == C->BaseSkel && R->GetAttachSocketName() == FName(TEXT("8_a1")), TEXT("A4 удочка на кости 8_a1 персонажа"));
		Expect(RodClipIs(C, 60), FString::Printf(TEXT("A4 удочка играет клип 0 (заброс): '%s'"), *ClipOf(R)));
		Expect(ClipPlaying(R), TEXT("A4 клип удочки ИГРАЕТ (IsPlaying)"));
		Expect(!C->IsFishHookVisible(), TEXT("A4 контроль: на 60 рыба не видна"));
		MarkCharT = ClipTime(C->BaseSkel);
		MarkRodT = ClipTime(R);
	}));
	Steps.Add(WaitFor([this, C]() { return ClipTime(C->BaseSkel) > MarkCharT + 0.15f && ClipTime(C->GetFishRodComp()) > MarkRodT + 0.15f; }, 1.f,
		TEXT("A4a время клипов персонажа и удочки РЕАЛЬНО идёт (позиция растёт)"), nullptr));
	Steps.Add(WaitFor([]() { return true; }, 0.f, TEXT(""), [this, C]()
	{
		const float D = RodHandDist(C);
		Expect(D < 3.f, FString::Printf(TEXT("A5 посадка: кость 8a1_dummy удочки = кость 8_a1 персонажа (%.2f см)"), D));
	}));
	// конец клипа 60 (3.67 с) двигает автомат: 110 → 61
	Steps.Add(WaitFor([C]() { return C->GetFishAction() == 61; }, 5.f, TEXT("A6 конец клипа 60 → действие 61"), [this, C]()
	{
		Expect(F->GetFishState() >= 110 && F->GetFishState() <= 115, FString::Printf(TEXT("A6 автомат ушёл из 112 концом клипа (мост 0): %d"), F->GetFishState()));
		Expect(Len60 > 0.f && MaxT60 >= Len60 - 0.2f, FString::Printf(TEXT("A6 клип 60 РЕАЛЬНО доигран до конца: позиция анимации дошла до %.2f из %.2f с"), MaxT60, Len60));
		Expect(CharClipIs(C, 61) && RodClipIs(C, 61), FString::Printf(TEXT("A6 клип ожидания anim_226 / удочка anim_01: '%s' / '%s'"), *C->GetPlayingClipName(), *ClipOf(C->GetFishRodComp())));
		Expect(!Sent4024(123), TEXT("A6 контроль: 61 соседям не шлётся"));
		// регресс-пара эмоции: своему во время рыбалки эмоция отклоняется (признак «занят»), действие не сбито
		Expect(C->PlayEmotionClip(33) <= 0.f && C->GetFishAction() == 61 && CharClipIs(C, 61), TEXT("A6 КОНТРОЛЬ: своя эмоция во время рыбалки отклонена, клип 61 не сбит"));
		Expect(UAsdaKingCrownSubsystem::EtalonActionStateOf(C) == 61, TEXT("A6 корона: состояние 61 (нечётное — спаун короны блокируется, как эталон +47551)"));
		Expect(KbState() == 61, FString::Printf(TEXT("A6 клавиатура: KbEtalonActionState = 61 (%d)"), KbState()));
	}));
	Steps.Add(WaitFor([this]() { return F->GetFishState() == 115; }, 3.f, TEXT("A7 автомат 115 (мини-игра)"), [this]() { F->EmuQueueRound(4, 4); }));
	Steps.Add(WaitFor([C]() { return C->GetFishAction() == 63 || C->GetFishAction() == 64; }, 3.f, TEXT("A8 раунд → подсечка 63/64"), [this, C]()
	{
		Act63Seen = C->GetFishAction();
		Expect(CharClipIs(C, Act63Seen) && RodClipIs(C, Act63Seen), FString::Printf(TEXT("A8 подсечка %d: клип '%s', удочка '%s'"), Act63Seen, *C->GetPlayingClipName(), *ClipOf(C->GetFishRodComp())));
		Expect(Count(6169) == 1, TEXT("A8 6169 ушёл"));
		Expect(!C->IsFishHookVisible(), TEXT("A8 контроль: на 63/64 рыба не видна"));
	}));
	Steps.Add(WaitFor([C]() { return C->GetFishAction() == 61; }, 3.f, TEXT("A9 конец 63/64 → 61"), [this]()
	{
		Feed(6170, B6170(1, 1, 500));   // шкала полна → 116 → 117 → 6171(1) → 118
	}));
	Steps.Add(WaitFor([this]() { return F->GetFishState() == 118; }, 3.f, TEXT("A10 6170 полна → 118"), [this]()
	{
		Expect(Count(6171) == 1, TEXT("A10 6171(1) ушёл"));
		Feed(6172, B6172(1, 120, kFish, FreeBagCell()));
	}));
	Steps.Add(WaitFor([C]() { return C->GetFishAction() == 62; }, 3.f, TEXT("A11 6172 код 1 → 119 → 113 → действие 62"), [this, C]()
	{
		Expect(CharClipIs(C, 62) && RodClipIs(C, 62), FString::Printf(TEXT("A11 вытаскивание anim_227 / удочка anim_02: '%s' / '%s'"), *C->GetPlayingClipName(), *ClipOf(C->GetFishRodComp())));
		Expect(Sent4024(124), TEXT("A11 4024 stateId 124"));
		Expect(C->GetFishHasFish() && C->GetFishCode() == kFish && C->GetFishSizePct() == 120, TEXT("A11 поля рыбы: есть, 69577, 120%"));
	}));
	Steps.Add(WaitFor([C]() { return C->IsFishHookVisible(); }, 1.f, TEXT("A12 рыба видна на 62"), [this, C]()
	{
		USkeletalMeshComponent* H = C->GetFishHookComp();
		Expect(H && H->GetAttachParent() == C->GetFishRodComp() && H->GetAttachSocketName() == FName(TEXT("Bone24")), TEXT("A12 рыба на кости Bone24 удочки"));
		Expect(H && GetNameSafe(H->GetSkeletalMeshAsset()) == TEXT("fs_fsm_crucian_00"), FString::Printf(TEXT("A12 модель рыбы по Fish_data+120: %s"), H ? *GetNameSafe(H->GetSkeletalMeshAsset()) : TEXT("нет")));
		Expect(FMath::IsNearlyEqual(FishScale(C), 1.2f, 0.01f), FString::Printf(TEXT("A12 масштаб рыбы = размер/100 = 1.2 (%.3f)"), FishScale(C)));
		Expect(ClipOf(H).EndsWith(TEXT("anim_00")) && ClipPlaying(H), FString::Printf(TEXT("A12 рыба играет свой клип: '%s'"), *ClipOf(H)));
	}));
	Steps.Add(WaitFor([]() { return true; }, 0.f, TEXT(""), [this, C]()
	{
		const float D = BoneDist(C->GetFishHookComp(), FName(TEXT("fs_Bone01")), C->GetFishRodComp(), FName(TEXT("Bone24")));
		Expect(D < 3.f, FString::Printf(TEXT("A13 посадка: fs_Bone01 рыбы = Bone24 удочки (%.2f см)"), D));
	}));
	// конец 62 → fish_end_01 + 126 → действие 65
	Steps.Add(WaitFor([C]() { return C->GetFishAction() == 65; }, 4.f, TEXT("A14 конец клипа 62 → 126 → действие 65"), [this, C]()
	{
		Expect(FxCount(TEXT("fish_end_01")) >= 1, TEXT("A14 эффект fish_end_01 заспавнен (свой, из конца клипа 62)"));
		Expect(CharClipIs(C, 65) && RodClipIs(C, 65), FString::Printf(TEXT("A14 удача anim_230 / удочка anim_05: '%s'"), *C->GetPlayingClipName()));
		Expect(Sent4024(127), TEXT("A14 4024 stateId 127"));
	}));
	Steps.Add(WaitFor([C]() { return C->IsFishHookVisible() && C->GetFishHookComp()->GetAttachSocketName() == FName(TEXT("_8_a1")); }, 1.f,
		TEXT("A15 на 65 рыба в руке (кость _8_a1)"), [this, C]()
	{
		const float D = BoneDist(C->GetFishHookComp(), FName(TEXT("fs_Bone01")), C->BaseSkel, FName(TEXT("_8_a1")));
		Expect(D < 3.f, FString::Printf(TEXT("A15 посадка: fs_Bone01 рыбы = _8_a1 персонажа (%.2f см)"), D));
	}));
	Steps.Add(WaitFor([C]() { return C->GetFishAction() == 0; }, 5.f, TEXT("A16 конец 65 → действие 0"), [this, C]()
	{
		Expect(F->GetFishState() == 120, FString::Printf(TEXT("A16 автомат 120 (%d)"), F->GetFishState()));
		Expect(IdleClipIs(C), FString::Printf(TEXT("A16 клип покоя anim_%02d (IdleClipIdx): '%s'"), C->IdleClipIdx(), *C->GetPlayingClipName()));
	}));
	Steps.Add(WaitFor([C]() { return !C->IsFishHookVisible() && !C->IsFishWeaponHidden(); }, 1.f, TEXT("A16a рыба не видна, признак скрытия оружия снят"), [this, C]()
	{
		const FParts P = PartsOf(C);
		Expect(P.MeshHid == 0 && P.WFxHid == 0 && P.SFxHid == 0, FString::Printf(TEXT("A16b РЕАЛЬНО: скрытых частей оружия/щита нет — %s"), *PartsStr(P)));
	}));
	Steps.Add(WaitFor([C]() { return C->GetFishRodComp() == nullptr && C->GetFishHookComp() == nullptr; }, 1.f, TEXT("A17 флаг 0 → удочка и рыба сняты"), nullptr));
	Steps.Add(WaitFor([this]() { return F->GetFishState() == 0; }, 12.f, TEXT("A18 120 → 121 → 128 → 0 (автоповтор выкл.)"), [this, C]()
	{
		// регресс-пара эмоции: после рыбалки своя эмоция снова играет и не перебивается
		const float L = C->PlayEmotionClip(33);
		Expect(L > 0.f && C->IsEmotionAnimActive(), FString::Printf(TEXT("A19 эмоция своего после рыбалки играет (%.2f с, клип '%s')"), L, *C->GetPlayingClipName()));
	}));
	Steps.Add(WaitFor([]() { return true; }, 0.f, TEXT(""), [C]() { C->StopEmotionClip(); }));

	// ═══════════════ B. ЧУЖИЕ ═══════════════
	Steps.Add(Do([this, C]()
	{
		const FVector2D Near = C->GetGamePosXZ();
		WS->DebugSpawnFakePeer(kPeerNum, TEXT("Рыбак"), Near + FVector2D(0.f, 300.f));
		WS->DebugSpawnFakePeer(kPeer2Num, TEXT("БезУдочки"), Near + FVector2D(300.f, 300.f));
	}));
	Steps.Add(WaitFor([this]() { return WS->FindPlayerActorByCharId(900000 + kPeerNum) && WS->FindPlayerActorByCharId(900000 + kPeer2Num); }, 5.f,
		TEXT("B0 соседи заспавнены"), [this]()
	{
		Peer = Cast<AAsdaPlayerCharacter>(WS->FindPlayerActorByCharId(900000 + kPeerNum));
		Peer2 = Cast<AAsdaPlayerCharacter>(WS->FindPlayerActorByCharId(900000 + kPeer2Num));
		// Экип пакетом 4021 (путь игрока): рыбаку — удочка (слот 9) и щит с заточкой 7 (слот 8, аура);
		//   второму — оружие со свечением (слот 9, заточка 7).
		Feed(4021, B4021(kPeerNum, 9, kRod, 7));
		if (ShieldId > 0) { Feed(4021, B4021(kPeerNum, 8, ShieldId, 7)); }
		if (SwordId > 0) { Feed(4021, B4021(kPeer2Num, 9, SwordId, 7)); }
	}));
	Steps.Add(WaitFor([this]()
	{
		const FParts P1 = PartsOf(Peer.Get()), P2 = PartsOf(Peer2.Get());
		return Peer.IsValid() && Peer2.IsValid() && Peer->GetEquippedWeaponItemId() == kRod && P1.MeshVis > 0 && P1.SFxVis > 0
			&& Peer2->GetEquippedWeaponItemId() == SwordId && P2.MeshVis > 0 && P2.WFxVis > 0;
	}, 3.f, TEXT("B0a 4021 → рыбак: удочка в руке + аура щита; второй: оружие + свечение (всё видно)"), [this]()
	{
		UE_LOG(LogAsdaFishAnimTest, Log, TEXT("САМОТЕСТ-РЫБА-АНИМ инфо: рыбак %s; второй %s"), *PartsStr(PartsOf(Peer.Get())), *PartsStr(PartsOf(Peer2.Get())));
		// Появление рыбака в видимости: 6173 {1, charId, 0, 0} (Send_MoveSectorInClippingUserInfo).
		Feed(6173, B6173(1, 900000 + kPeerNum, 0, 0));
		Feed(6173, B6173(1, 900000 + kPeer2Num, 0, 0));
	}));
	Steps.Add(WaitFor([this]() { return Peer.IsValid() && Peer->GetFishAction() == 61 && Peer->GetFishRodComp(); }, 3.f,
		TEXT("B1 6173 фаза 1 → сосед: действие 61 + удочка"), [this]()
	{
		AAsdaPlayerCharacter* P = Peer.Get();
		Expect(CharClipIs(P, 61) && ClipPlaying(P->BaseSkel), FString::Printf(TEXT("B1 клип соседа anim_226 играет: '%s'"), *P->GetPlayingClipName()));
		Expect(RodClipIs(P, 61), FString::Printf(TEXT("B1 удочка соседа — клип текущего действия (01) [ИСПРАВЛЕНИЕ]: '%s'"), *ClipOf(P->GetFishRodComp())));
		Expect(P->GetFishFlag() && !P->GetFishHasFish(), TEXT("B1 флаг 1, рыбы нет"));
		Expect(Peer2.IsValid() && Peer2->GetFishAction() == 61 && Peer2->GetFishRodComp() == nullptr,
			TEXT("B1 КОНТРОЛЬ: сосед без удочки — действие 61, удочка НЕ появилась"));
		MarkCharT = ClipTime(P->BaseSkel);
		MarkRodT = ClipTime(P->GetFishRodComp());
	}));
	Steps.Add(WaitFor([this]() { return ClipTime(Peer->BaseSkel) > MarkCharT + 0.15f && ClipTime(Peer->GetFishRodComp()) > MarkRodT + 0.15f; }, 1.f,
		TEXT("B1a время клипов соседа и его удочки РЕАЛЬНО идёт"), [this]()
	{
		const FParts P1 = PartsOf(Peer.Get()), P2 = PartsOf(Peer2.Get());
		Expect(P1.MeshVis == 0 && P1.MeshHid > 0 && P1.SFxVis == 0 && P1.SFxHid > 0 && P1.WFxVis == 0,
			FString::Printf(TEXT("B1b ВАЖН-1: у рыбака скрыты меш оружия И аура щита — %s"), *PartsStr(P1)));
		Expect(P2.MeshVis == 0 && P2.MeshHid > 0 && P2.WFxVis == 0 && P2.WFxHid > 0,
			FString::Printf(TEXT("B1c второй (флаг 1, занят, без удочки): меш и свечение оружия скрыты, как эталон busy && [15036] — %s"), *PartsStr(P2)));
		Expect(RodHandDist(Peer.Get()) < 3.f, FString::Printf(TEXT("B2 посадка удочки соседа на 8_a1 (%.2f см)"), RodHandDist(Peer.Get())));
		Feed(4025, B4025(kPeer2Num, 0, 0.f, 0.f));   // второму — покой пакетом (ВАЖН-2: Idle → Idle снимает 61)
		Feed(4025, B4025(kPeerNum, 122, 1.f, 0.f));
	}));
	Steps.Add(WaitFor([this]() { return Peer->GetFishAction() == 60 && Peer2->GetFishAction() == 0; }, 2.f,
		TEXT("B3 4025 stateId 122 → действие 60; второй 4025 0 при покое → действие 61 снято (ВАЖН-2)"), [this]()
	{
		Expect(Peer2->GetFishFlag(), TEXT("B3 КОНТРОЛЬ: у второго удочки нет — флаг эталон не снимает (сброс game.c:607922 идёт по объектам удочек)"));
		AAsdaPlayerCharacter* P = Peer.Get();
		Expect(CharClipIs(P, 60) && RodClipIs(P, 60), FString::Printf(TEXT("B3 заброс у соседа: '%s' / '%s'"), *P->GetPlayingClipName(), *ClipOf(P->GetFishRodComp())));
		const FVector2D Dir = P->GetFacingDirGame();
		Expect(Dir.X > 0.99f, FString::Printf(TEXT("B3 поворот к воде по направлению 4025 (1,0): (%.2f, %.2f)"), Dir.X, Dir.Y));
		Expect(IdleClipIs(Peer2.Get()), FString::Printf(TEXT("B3 второй в клипе покоя: '%s'"), *Peer2->GetPlayingClipName()));
		// ★ 30.10.2026: контроль — 120. Прежний 121 В ТАБЛИЦЕ `Recv_CharSpawnFx` есть (network.c:39819, case 9 → состояние 51, жест
		//   поражения) и по эталону ПЕРЕБИВАЕТ рыбалку; вне таблицы — 118…120 (cases 6…8 нет).
		Feed(4025, B4025(kPeerNum, 120, 0.f, 1.f));   // контроль: вне таблицы
		// ★ В45fix2 (МЕЛК-5): 130 здесь больше НЕ контроль — это `CChar_SetState(…, 68)` (g:551554-551555), он ПЕРЕБИВАЕТ
		//   60…67 (запись безусловна, g:76456). Его пара — B24b…B24e ниже.
	}));
	Steps.Add(WaitFor([]() { return true; }, 0.f, TEXT(""), [this]()
	{
		Expect(Peer->GetFishAction() == 60 && CharClipIs(Peer.Get(), 60), TEXT("B4 КОНТРОЛЬ: 4025 stateId 120 (нет в таблице Recv_CharSpawnFx) рыбалку не трогает"));
		const FParts P2 = PartsOf(Peer2.Get());
		Expect(P2.MeshHid == 0 && P2.WFxHid == 0 && P2.MeshVis > 0 && P2.WFxVis > 0, FString::Printf(TEXT("B4a второй: оружие и свечение снова видны — %s"), *PartsStr(P2)));
	}));
	Steps.Add(WaitFor([this]() { return Peer->GetFishAction() == 61; }, 5.f, TEXT("B5 конец 60 у соседа → 61 (таблица эталона)"), [this]()
	{
		Feed(6173, B6173(1, 900000 + kPeerNum, kJunk, 50));   // итог: мусор, размер 50 → масштаб 2.0 (тип 4)
	}));
	Steps.Add(WaitFor([this]() { return Peer->GetFishCode() == kJunk; }, 2.f, TEXT("B6 6173 итог → поля соседа"), [this]()
	{
		Expect(Peer->GetFishHasFish() && Peer->GetFishSizePct() == 50, TEXT("B6 рыба есть, размер 50"));
		Feed(4025, B4025(kPeerNum, 124, 1.f, 0.f));
	}));
	Steps.Add(WaitFor([this]() { return Peer->GetFishAction() == 62 && Peer->IsFishHookVisible(); }, 2.f, TEXT("B7 4025 124 → 62, рыба видна"), [this]()
	{
		AAsdaPlayerCharacter* P = Peer.Get();
		Expect(GetNameSafe(P->GetFishHookComp()->GetSkeletalMeshAsset()) == TEXT("fs_stuff_00"), TEXT("B7 модель мусора fs_stuff_00"));
		Expect(FMath::IsNearlyEqual(FishScale(P), 2.f, 0.01f), FString::Printf(TEXT("B7 тип 4 → масштаб 2.0 (%.3f), а не размер/100"), FishScale(P)));
		Expect(FxCount(TEXT("fish_end_02")) == 0, TEXT("B7 контроль: на 62 эффектов нет"));
	}));
	Steps.Add(WaitFor([this]() { return BoneDist(Peer->GetFishHookComp(), FName(TEXT("fs_Bone01")), Peer->GetFishRodComp(), FName(TEXT("Bone24"))) < 3.f; }, 1.f,
		TEXT("B8 рыба соседа на Bone24"), [this]()
	{
		Expect(true, TEXT("B8 посадка рыбы соседа на кончик лески"));
		Feed(4025, B4025(kPeerNum, 127, 1.f, 0.f));
	}));
	Steps.Add(WaitFor([this]() { return Peer->GetFishAction() == 65; }, 2.f, TEXT("B9 4025 127 → 65"), [this]()
	{
		Expect(Peer->GetFishPeerFxCount() == 1, FString::Printf(TEXT("B9 эффект fish_end_01 у соседа при ВХОДЕ в 65 (%d)"), Peer->GetFishPeerFxCount()));
		Expect(CharClipIs(Peer.Get(), 65), TEXT("B9 клип anim_230"));
		Feed(4025, B4025(kPeerNum, 127, 1.f, 0.f));   // контроль: повтор 65 — [15100] не даёт второго эффекта
	}));
	Steps.Add(WaitFor([]() { return true; }, 0.f, TEXT(""), [this]()
	{
		Expect(Peer->GetFishPeerFxCount() == 1, FString::Printf(TEXT("B10 КОНТРОЛЬ: повтор 65 — второго эффекта нет ([15100]) (%d)"), Peer->GetFishPeerFxCount()));
		Expect(Peer->IsFishHookVisible() && Peer->GetFishHookComp()->GetAttachSocketName() == FName(TEXT("_8_a1")), TEXT("B10 рыба в руке соседа (_8_a1)"));
	}));
	Steps.Add(WaitFor([this]() { return Peer->GetFishAction() == 0; }, 5.f, TEXT("B11 конец 65 у соседа → 0 и флаг 0"), [this]()
	{
		Expect(!Peer->GetFishFlag(), TEXT("B11 флаг [15036] снят концом клипа"));
	}));
	Steps.Add(WaitFor([this]() { return !Peer->GetFishRodComp() && !Peer->GetFishHookComp(); }, 1.f, TEXT("B12 удочка и рыба соседа сняты"), [this]()
	{
		const FParts P1 = PartsOf(Peer.Get());
		Expect(P1.MeshHid == 0 && P1.SFxHid == 0 && P1.MeshVis > 0 && P1.SFxVis > 0, FString::Printf(TEXT("B12a у рыбака снова видны меш и аура щита — %s"), *PartsStr(P1)));
		Feed(4025, B4025(kPeerNum, 128, 1.f, 0.f));
	}));
	Steps.Add(WaitFor([this]() { return Peer->GetFishAction() == 66; }, 2.f, TEXT("B13 4025 128 → 66"), [this]()
	{
		Expect(Peer->GetFishPeerFxCount() == 2 && FxCount(TEXT("fish_end_02")) >= 1, TEXT("B13 эффект fish_end_02 у соседа"));
	}));
	Steps.Add(WaitFor([]() { return true; }, 0.f, TEXT(""), [this]()
	{
		Expect(Peer->GetFishRodComp() == nullptr, TEXT("B14 КОНТРОЛЬ: флаг 0 — удочка на 66 без 6173 не вставляется (эталон снимает тем же кадром)"));
	}));
	Steps.Add(WaitFor([this]() { return Peer->GetFishAction() == 0; }, 4.f, TEXT("B15 конец 66 → 0"), [this]()
	{
		Feed(6173, B6173(1, 900000 + kPeerNum, 0, 0));
	}));
	// ── МЕЛК-1: смена удочки пакетом 4021 при висящей удочке — модель НЕ перезагружается (у эталона [15084] не пишется) ──
	Steps.Add(WaitFor([this]() { return Peer->GetFishAction() == 61 && Peer->GetFishRodComp(); }, 2.f, TEXT("B16 снова 61 с удочкой"), [this]()
	{
		Feed(4021, B4021(kPeerNum, 9, kRod2, 0));
	}));
	Steps.Add(WaitFor([this]() { return Peer->GetEquippedWeaponItemId() == kRod2 && PartsOf(Peer.Get()).MeshHid > 0; }, 2.f,
		TEXT("B17 4021 → в руке удочка 69658, новый меш оружия пересобран"), [this]()
	{
		const FParts P1 = PartsOf(Peer.Get());
		Expect(P1.MeshVis == 0, FString::Printf(TEXT("B17a МЕЛК-9: пересобранный меш оружия скрыт повторным проходом по событию — %s"), *PartsStr(P1)));
		Expect(Peer->GetFishRodComp() && Peer->GetFishRodMeshName() == TEXT("ch1_n_00_fird_00_00_0_00"),
			FString::Printf(TEXT("B17b КОНТРОЛЬ МЕЛК-1: модель удочки НЕ перезагружена (как оригинал): %s"), *Peer->GetFishRodMeshName()));
		Feed(4025, B4025(kPeerNum, 400, 0.f, 0.f));   // ВАЖН-2: покой (400) при AnimState уже Idle
	}));
	Steps.Add(WaitFor([this]() { return Peer->GetFishAction() == 0; }, 1.f, TEXT("B18 ВАЖН-2: 4025 400 при покое → действие 61 снято"), [this]()
	{
		Expect(IdleClipIs(Peer.Get()), FString::Printf(TEXT("B18 клип покоя у соседа: '%s'"), *Peer->GetPlayingClipName()));
	}));
	Steps.Add(WaitFor([this]() { return !Peer->GetFishFlag() && !Peer->GetFishRodComp(); }, 1.f, TEXT("B18a через кадр флаг 0 и удочка снята (game.c:607922)"), [this]()
	{
		Feed(6173, B6173(1, 900000 + kPeerNum, 0, 0));
	}));
	Steps.Add(WaitFor([this]() { return Peer->GetFishAction() == 61 && Peer->GetFishRodComp(); }, 2.f, TEXT("B19 6173 фаза 1 → новая вставка"), [this]()
	{
		Expect(Peer->GetFishRodMeshName() == TEXT("ch1_n_00_fird_01_00_0_00"), FString::Printf(TEXT("B19 новая вставка берёт новую удочку → fird_01 (%s)"), *Peer->GetFishRodMeshName()));
		Feed(6173, B6173(3, 900000 + kPeerNum, 0, 0));
	}));
	Steps.Add(WaitFor([this]() { return Peer->GetFishAction() == 63 || Peer->GetFishAction() == 64; }, 2.f, TEXT("B20 6173 фаза 3 → 63/64"), [this]()
	{
		Expect(Peer->GetFishFlag(), TEXT("B20 фаза 3 поля не трогает (флаг остался 1)"));
		Feed(4025, B4025(kPeerNum, 1, 0.f, 1.f));   // движение соседа ПАКЕТОМ перебивает действие
	}));
	Steps.Add(WaitFor([this]() { return Peer->GetFishAction() == 0; }, 1.f, TEXT("B21 4025 1 (движение) снимает действие рыбалки"), [this]()
	{
		Feed(4025, B4025(kPeerNum, 0, 0.f, 0.f));
	}));
	Steps.Add(WaitFor([this]() { return !Peer->GetFishFlag() && !Peer->GetFishRodComp(); }, 1.f,
		TEXT("B22 сосед с удочкой в движении/покое → флаг 0, удочка снята (game.c:607922)"), [this]()
	{
		Feed(6173, B6173(1, 900000 + kPeerNum, 0, 0));
	}));
	Steps.Add(WaitFor([this]() { return Peer->GetFishAction() == 61 && Peer->GetFishRodComp(); }, 2.f, TEXT("B23 снова 61"), [this]()
	{
		Feed(4025, B4025(kPeerNum, 109, 0.f, 0.f));   // «встать» (109) при покое
	}));
	// ★ В45fix2 (ревизия В4/В5, МЕЛК-3): 109 = `CChar_SetState(…, 9)` «встаёт» (g:551808-551809), а не 0. Действие снято СРАЗУ,
	//   идёт хвост клипа sitdown01 (состояние 9), удочка висит — флаг эталон снимает только в ≤ 2/5/6 (g:607922), т.е. по
	//   концу вставания. Прежде порт (109 → покой) снимал удочку тем же кадром.
	Steps.Add(WaitFor([this]() { return Peer->GetFishAction() == 0; }, 1.f, TEXT("B24 МЕЛК-3: 4025 109 → действие 61 снято"), [this]()
	{
		AAsdaPlayerCharacter* P = Peer.Get();
		Expect(P->GetSitPhase() == 9 && GetNameSafe(P->BaseSkel->GetSingleNodeInstance() ? P->BaseSkel->GetSingleNodeInstance()->GetAnimationAsset() : nullptr).EndsWith(TEXT("anim_15")),
			FString::Printf(TEXT("B24a состояние 9 «встаёт»: фаза %d, клип '%s' (хвост sitdown01 = …anim_15, слот 3)"), P->GetSitPhase(), *P->GetPlayingClipName()));
		Expect(UAsdaKingCrownSubsystem::EtalonActionStateOf(P) == 9, FString::Printf(TEXT("B24a корона: эталонное состояние 9 (%d)"), UAsdaKingCrownSubsystem::EtalonActionStateOf(P)));
	}));
	Steps.Add(WaitFor([]() { return true; }, 0.f, TEXT(""), [this]()
	{
		Expect(Peer->GetSitPhase() == 9 && Peer->GetFishFlag() && Peer->GetFishRodComp() != nullptr,
			TEXT("B24b КОНТРОЛЬ МЕЛК-3: пока идёт вставание (9), флаг 1 и удочка висит (эталон: снятие только в ≤ 2/5/6)"));
	}));
	Steps.Add(WaitFor([this]() { return Peer->GetSitPhase() == 0 && !Peer->GetFishFlag() && !Peer->GetFishRodComp(); }, 4.f,
		TEXT("B24c конец вставания → состояние 0 → флаг 0, удочка снята (game.c:607922)"), [this]()
	{
		Expect(IdleClipIs(Peer.Get()), FString::Printf(TEXT("B24c клип покоя после вставания: '%s'"), *Peer->GetPlayingClipName()));
		Feed(6173, B6173(1, 900000 + kPeerNum, 0, 0));
	}));
	// ★ В45fix2 (ревизия В4/В5, МЕЛК-5): 4025 130 = `CChar_SetState(…, 68)` «добыча» — переписывает 60…67, `bActionBusy = 1`,
	//   флаг и удочку не снимает (68 ∉ ≤ 2/5/6). Снимает 68 следующая смена состояния (4025 0 → флаг 0, удочка снята).
	Steps.Add(WaitFor([this]() { return Peer->GetFishAction() == 61 && Peer->GetFishRodComp(); }, 2.f, TEXT("B24d снова 61 с удочкой"), [this]()
	{
		Feed(4025, B4025(kPeerNum, 130, 0.f, 0.f));
	}));
	Steps.Add(WaitFor([this]() { return Peer->GetFishAction() == 0; }, 1.f, TEXT("B24e МЕЛК-5: 4025 130 (состояние 68) перебил действие 61"), [this]()
	{
		AAsdaPlayerCharacter* P = Peer.Get();
		Expect(P->IsEtalonState68() && UAsdaKingCrownSubsystem::EtalonActionStateOf(P) == 68,
			FString::Printf(TEXT("B24e состояние 68 «добыча» стоит (корона: %d)"), UAsdaKingCrownSubsystem::EtalonActionStateOf(P)));
		MarkCharT = (float)World->GetTimeSeconds();
	}));
	Steps.Add(WaitFor([this]() { return World->GetTimeSeconds() > MarkCharT + 0.4f; }, 1.f, TEXT(""), [this]()
	{
		AAsdaPlayerCharacter* P = Peer.Get();
		const FParts P1 = PartsOf(P);
		Expect(P->IsEtalonState68() && P->GetFishFlag() && P->GetFishRodComp() != nullptr,
			TEXT("B24f КОНТРОЛЬ МЕЛК-5: через 0.4 с в 68 флаг 1 и удочка висят (68 ∉ ≤ 2/5/6 — эталон флаг не снимает)"));
		Expect(P1.MeshVis == 0 && P1.MeshHid > 0, FString::Printf(TEXT("B24f 68 = «занят» (bActionBusy 1) && флаг → оружие скрыто — %s"), *PartsStr(P1)));
		Feed(4025, B4025(kPeerNum, 0, 0.f, 0.f));
	}));
	Steps.Add(WaitFor([this]() { return !Peer->IsEtalonState68() && !Peer->GetFishFlag() && !Peer->GetFishRodComp(); }, 1.f,
		TEXT("B24g 4025 0 снял состояние 68 → флаг 0, удочка снята"), [this]()
	{
		Feed(6173, B6173(1, 900000 + kPeerNum, 0, 0));
	}));
	// ★ В45fix2 (ревизия В4/В5, МЕЛК-6): «залипание» `char[52136]` — сосед в 61 получил 6173 с фазой 0 (флаг 0, смены состояния
	//   нет): удочка снята, а оружие у эталона ОСТАЁТСЯ скрытым до конца «занят» (g:80805-80813: при флаге 0 поле не
	//   возвращается в 1). Показ — только когда «занят» снят (4025 0).
	Steps.Add(WaitFor([this]() { return Peer->GetFishAction() == 61 && Peer->GetFishRodComp() && PartsOf(Peer.Get()).MeshHid > 0; }, 2.f,
		TEXT("B24h снова 61 с удочкой, оружие скрыто"), [this]()
	{
		Feed(6173, B6173(0, 900000 + kPeerNum, 0, 0));
	}));
	Steps.Add(WaitFor([this]() { return !Peer->GetFishFlag() && !Peer->GetFishRodComp(); }, 1.f, TEXT("B24i 6173 фаза 0 → флаг 0, удочка снята"), [this]()
	{
		const FParts P1 = PartsOf(Peer.Get());
		Expect(Peer->GetFishAction() == 61 && P1.MeshVis == 0 && P1.MeshHid > 0,
			FString::Printf(TEXT("B24i МЕЛК-6: «занят» (61) при флаге 0 — оружие ОСТАЁТСЯ скрытым (залипание 52136 эталона) — действие %d, %s"), Peer->GetFishAction(), *PartsStr(P1)));
		Feed(4025, B4025(kPeerNum, 0, 0.f, 0.f));
	}));
	Steps.Add(WaitFor([this]() { return Peer->GetFishAction() == 0 && PartsOf(Peer.Get()).MeshHid == 0; }, 1.f,
		TEXT("B24j КОНТРОЛЬ МЕЛК-6: «занят» снят (4025 0) — оружие снова видно"), [this]()
	{
		Feed(6173, B6173(1, 900000 + kPeerNum, 0, 0));
	}));
	// ── регресс: эмоция соседа во время рыбалки (4025 100 → состояние 33) ──
	Steps.Add(WaitFor([this]() { return Peer->GetFishAction() == 61 && Peer->GetFishRodComp(); }, 2.f, TEXT("B25 снова 61"), [this]()
	{
		Feed(4025, B4025(kPeerNum, 100, 0.f, 0.f));
	}));
	Steps.Add(WaitFor([this]() { return Peer->IsEmotionAnimActive(); }, 1.f, TEXT("B26 4025 100 → эмоция соседа перебила 61"), [this]()
	{
		Expect(Peer->GetFishAction() == 0, TEXT("B26 действие рыбалки снято эмоцией"));
		Expect(UAsdaKingCrownSubsystem::EtalonActionStateOf(Peer.Get()) == 33, TEXT("B26 корона: состояние 33 (эмоция)"));
	}));
	Steps.Add(WaitFor([this]() { return true; }, 0.f, TEXT(""), [this]()
	{
		Expect(Peer->GetFishFlag() && Peer->GetFishRodComp(), TEXT("B27 МЕЛК-2: эмоция флаг не снимает — удочка остаётся (эталон: только ≤2/5/6)"));
		const FParts P1 = PartsOf(Peer.Get());
		Expect(P1.MeshVis == 0 && P1.SFxVis == 0, FString::Printf(TEXT("B27a занят (эмоция) && флаг → оружие и щит скрыты — %s"), *PartsStr(P1)));
		Feed(4025, B4025(kPeerNum, 0, 0.f, 0.f));
	}));
	Steps.Add(WaitFor([this]() { return !Peer->IsEmotionAnimActive() && !Peer->GetFishFlag() && !Peer->GetFishRodComp(); }, 1.f,
		TEXT("B28 4025 0 → эмоция прервана, покой с удочкой → флаг 0, удочка снята"), [this]()
	{
		Feed(6173, B6173(1, 900000 + kPeerNum, 0, 0));
	}));
	// ── регресс: навык соседа во время рыбалки (общая точка показа навыка; пакет 5257 требует живого боя) ──
	Steps.Add(WaitFor([this]() { return Peer->GetFishAction() == 61 && Peer->GetFishRodComp(); }, 2.f, TEXT("B29 снова 61"), [this]()
	{
		UAsdaSkillFxSubsystem* Sfx = GI->GetSubsystem<UAsdaSkillFxSubsystem>();
		const bool bOk = Sfx && Sfx->PlaySkillVisual(Peer.Get(), kSkillId, kPeerNum);
		Expect(bOk, FString::Printf(TEXT("B29 показ навыка %d соседу запущен"), kSkillId));
	}));
	Steps.Add(WaitFor([this]() { return Peer->IsSkillAnimActive(); }, 1.f, TEXT("B30 навык (24) перебил 61"), [this]()
	{
		Expect(Peer->GetFishAction() == 0, TEXT("B30 действие рыбалки снято навыком"));
	}));
	Steps.Add(WaitFor([]() { return true; }, 0.f, TEXT(""), [this]()
	{
		Expect(Peer->GetFishFlag() && Peer->GetFishRodComp(), TEXT("B31 МЕЛК-2: во время навыка флаг и удочка остаются (эталон: 24 ∉ {≤2, 5, 6})"));
	}));
	Steps.Add(WaitFor([this]() { return !Peer->IsSkillAnimActive() && !Peer->GetFishFlag() && !Peer->GetFishRodComp(); }, 4.f,
		TEXT("B32 навык кончился → покой с удочкой → флаг 0, удочка снята"), [this]()
	{
		Feed(6173, B6173(1, 900000 + kPeerNum, 0, 0));
	}));
	// ── регресс: атака соседа во время рыбалки (общая точка замаха; пакету 4029 нужна живая цель) ──
	Steps.Add(WaitFor([this]() { return Peer->GetFishAction() == 61 && Peer->GetFishRodComp(); }, 2.f, TEXT("B33 снова 61"), [this]()
	{
		const float L = Peer->PlayAttackClip(Peer->EquippedWeaponSubType);
		Expect(L > 0.f && Peer->IsAttackAnimActive(), FString::Printf(TEXT("B33 замах соседа запущен (%.2f с)"), L));
		Expect(Peer->GetFishAction() == 0, TEXT("B33 действие рыбалки снято атакой (5)"));
	}));
	Steps.Add(WaitFor([this]() { return !Peer->GetFishFlag() && !Peer->GetFishRodComp(); }, 1.f, TEXT("B34 атака (5) → флаг 0, удочка снята (game.c:607922)"), [this]()
	{
		Feed(6173, B6173(1, 900000 + kPeerNum, 0, 0));
	}));
	// ── регресс: GM-скрытие во время рыбалки (4025 300/301) ──
	Steps.Add(WaitFor([this]() { return Peer->GetFishAction() == 61 && Peer->GetFishRodComp() && !Peer->IsAttackAnimActive(); }, 4.f, TEXT("B35 снова 61"), [this]()
	{
		Feed(4025, B4025(kPeerNum, 300, 0.f, 0.f));
	}));
	Steps.Add(WaitFor([this]() { return Peer->GetFishAction() == 0 && Peer->IsHidden(); }, 1.f, TEXT("B36 ВАЖН-2: 4025 300 → действие снято (состояние 0), сосед скрыт"), nullptr));
	Steps.Add(WaitFor([this]() { return !Peer->GetFishFlag() && !Peer->GetFishRodComp(); }, 1.f, TEXT("B37 флаг 0, удочка снята"), [this]()
	{
		const FParts P1 = PartsOf(Peer.Get());
		Expect(P1.SFxVis == 0, FString::Printf(TEXT("B37a МЕЛК-9: конец рыбалки под GM-скрытием аурy щита НЕ проявил — %s"), *PartsStr(P1)));
		Feed(4025, B4025(kPeerNum, 301, 0.f, 0.f));
	}));
	Steps.Add(WaitFor([this]() { const FParts P1 = PartsOf(Peer.Get()); return !Peer->IsHidden() && P1.SFxVis > 0 && P1.SFxHid == 0; }, 1.f,
		TEXT("B38 4025 301 → сосед показан, аура щита вернулась (передана системе GM)"), [this]()
	{
		SetCv(TEXT("asda.Fish.LegacyRodClip0"), 1);
		Feed(6173, B6173(1, 900000 + kPeerNum, 0, 0));
	}));
	// ── ключ отката [ИСПРАВЛЕНИЕ] клипа новой удочки ──
	Steps.Add(WaitFor([this]() { return Peer->GetFishAction() == 61 && Peer->GetFishRodComp(); }, 2.f, TEXT("B39 asda.Fish.LegacyRodClip0 1: 6173 фаза 1 → 61 + удочка"), [this]()
	{
		Expect(Peer->GetFishRodClip() == 0 && ClipOf(Peer->GetFishRodComp()).EndsWith(TEXT("anim_00")),
			FString::Printf(TEXT("B39 как оригинал: новая удочка играет клип 00 (заброс) при действии 61: '%s'"), *ClipOf(Peer->GetFishRodComp())));
		SetCv(TEXT("asda.Fish.LegacyRodClip0"), 0);
		Feed(6173, B6173(2, 900000 + kPeerNum, 0, 0));
	}));
	Steps.Add(WaitFor([this]() { return Peer->GetFishAction() == 0 && !Peer->GetFishFlag(); }, 2.f, TEXT("B40 6173 фаза 2 (61 → 0, порядок эталона) → действие 0, флаг 0"), nullptr));
	Steps.Add(WaitFor([this]() { return !Peer->GetFishRodComp(); }, 1.f, TEXT("B41 удочка снята"), nullptr));
	// ★ Решение владельца 09.10.2026 (п. 5): «эмоции» 40/43 — состояния лопаты. 4025 110 → `CChar_SetState(…, 40)` (network.c:39958),
	//   114 → 43 (network.c:39716): `bActionBusy = 1` → оружие ЗА СПИНУ (флаг набора/кости), снимает следующая смена состояния.
	//   Контроль: до пакета сосед не «занят» и оружие в руке (иначе проверка ничего не доказывает).
	{
		TSharedRef<bool> bHiddenBefore = MakeShared<bool>(true);   // оружие соседа за спиной до пакета 110 (контроль)
		Steps.Add(WaitFor([]() { return true; }, 0.f, TEXT(""), [this, bHiddenBefore]()
		{
			*bHiddenBefore = Peer->bWeaponMotionHidden;
			Expect(!*bHiddenBefore && Peer->GetEtalonShovelState() == 0,
				FString::Printf(TEXT("B42 контроль: до 4025 110 оружие соседа в руке (за спиной %d), лопаты нет (кость %s)"),
					*bHiddenBefore ? 1 : 0, *Peer->CurrentWeaponBone().ToString()));
			Feed(4025, B4025(kPeerNum, 110, 0.f, 1.f));
		}));
		for (const int32 Code : { 110, 114 })
		{
			const int32 St = (Code == 110) ? 40 : 43;
			Steps.Add(WaitFor([this, St]() { return Peer->GetEtalonShovelState() == St; }, 1.f,
				FString::Printf(TEXT("B43 4025 %d → состояние %d"), Code, St), [this, St, Code]()
			{
				Expect(Peer->bWeaponMotionHidden && Peer->CurMotionSet() == 0 && UAsdaKingCrownSubsystem::EtalonActionStateOf(Peer.Get()) == St,
					FString::Printf(TEXT("B43 4025 %d → состояние %d «занят»: оружие ЗА СПИНОЙ (флаг %d, набор _%02d, кость %s), корона %d — как эталон (bActionBusy → char+226184)"),
						Code, St, Peer->bWeaponMotionHidden ? 1 : 0, Peer->CurMotionSet(), *Peer->CurrentWeaponBone().ToString(),
						UAsdaKingCrownSubsystem::EtalonActionStateOf(Peer.Get())));
				Feed(4025, B4025(kPeerNum, 0, 0.f, 0.f));
			}));
			Steps.Add(WaitFor([this]() { return Peer->GetEtalonShovelState() == 0 && !Peer->bWeaponMotionHidden; }, 1.f,
				FString::Printf(TEXT("B44 4025 0 после %d → состояние снято, оружие снова в руке"), St), [this, Code]()
			{
				if (Code == 110) { Feed(4025, B4025(kPeerNum, 114, 0.f, 1.f)); }
			}));
		}
	}
	// ★ six_items СРЕДН-1 (09.10.2026): БЕЗ 4025 0 состояние лопаты снимается САМО концом клипа копания
	//   (`CCharacter_UpdateActionState` case 7/10 → у соседа `CChar_SetState(…, 0)`): 40 — 6.000 с, 43 — 9.034 с (±0,2 с).
	//   Сосед ничего не шлёт (5428 — только ветка своего). Контроль: `asda.Shovel.ClipEnd 0` — прежний порт, конца нет.
	for (const int32 Code : { 110, 114 })
	{
		const int32 St = (Code == 110) ? 40 : 43;
		const float Want = (Code == 110) ? 6.0f : 9.034f;
		Steps.Add(Do([this, Code]() { ShovelMark = Sent.Num(); Feed(4025, B4025(kPeerNum, Code, 0.f, 1.f)); }));
		Steps.Add(WaitFor([this, St]() { return Peer->GetEtalonShovelState() == St; }, 1.f,
			FString::Printf(TEXT("B45 4025 %d → состояние %d (замер конца клипа)"), Code, St), [this, St, Want]()
		{
			ShovelT0 = World->GetTimeSeconds();
			Expect(FMath::IsNearlyEqual(Peer->ShovelStateSeconds(St), Want, 0.01f),
				FString::Printf(TEXT("B45 длительность состояния %d по эталону: %.3f с (ждём %.3f: клип 6000 мс из CharMotion.baz, пороги combo_time)"),
					St, Peer->ShovelStateSeconds(St), Want));
		}));
		Steps.Add(WaitFor([this]() { return Peer->GetEtalonShovelState() == 0; }, Want + 1.f,
			FString::Printf(TEXT("B45 без 4025 0 состояние %d снялось само (конец клипа)"), St), [this, St, Want]()
		{
			const double Dt = World->GetTimeSeconds() - ShovelT0;
			Expect(FMath::Abs(Dt - Want) <= 0.2,
				FString::Printf(TEXT("B45 состояние %d снято через %.3f с (эталон %.3f ± 0,2)"), St, Dt, Want));
			Expect(!Peer->bWeaponMotionHidden && UAsdaKingCrownSubsystem::EtalonActionStateOf(Peer.Get()) == 0,
				FString::Printf(TEXT("B45 после конца клипа %d: оружие в руке (флаг %d), корона %d"), St, Peer->bWeaponMotionHidden ? 1 : 0,
					UAsdaKingCrownSubsystem::EtalonActionStateOf(Peer.Get())));
			Expect(CountFrom(5428, ShovelMark) == 0 && CountFrom(4024, ShovelMark) == 0,
				FString::Printf(TEXT("B45 сосед по концу клипа %d ничего не шлёт (5428: %d, 4024: %d)"), St, CountFrom(5428, ShovelMark), CountFrom(4024, ShovelMark)));
		}));
	}
	Steps.Add(Do([this]() { SetCv(TEXT("asda.Shovel.ClipEnd"), 0); Feed(4025, B4025(kPeerNum, 110, 0.f, 1.f)); }));
	Steps.Add(WaitFor([this]() { return Peer->GetEtalonShovelState() == 40; }, 1.f, TEXT("B45 КОНТРОЛЬ asda.Shovel.ClipEnd 0: 4025 110 → 40"), nullptr));
	Steps.Add(FStep([this](float T)
	{
		if (T < 6.6f) { return false; }
		Expect(Peer->GetEtalonShovelState() == 40 && Peer->bWeaponMotionHidden,
			FString::Printf(TEXT("B45 КОНТРОЛЬ ключ 0 (прежний порт): через %.1f с состояние %d держится, оружие за спиной %d — проверка различает"),
				T, Peer->GetEtalonShovelState(), Peer->bWeaponMotionHidden ? 1 : 0));
		SetCv(TEXT("asda.Shovel.ClipEnd"), 1);
		Feed(4025, B4025(kPeerNum, 0, 0.f, 0.f));
		return true;
	}));
	Steps.Add(WaitFor([this]() { return Peer->GetEtalonShovelState() == 0 && !Peer->bWeaponMotionHidden; }, 1.f, TEXT("B45 КОНТРОЛЬ: 4025 0 снял 40"), nullptr));

	// ═══════════════ C. СВОЙ: ФОЛБЭК УДОЧКИ, «IMG БЕЗ МЕША», РЫБА БЕЗ УДОЧКИ, СТРАХОВКА МОСТА, СМЕНА КАРТЫ ═══════════════
	Steps.Add(Do([this, C]()
	{
		F->ResetSessionState(EAsdaSessionResetReason::Disconnected);
		C->SetWeaponFromItem(0, Items);                   // нет записи предмета → [ПОРТ] фолбэк fird_00
		LM->CurrentMapId = 3;
		Expect(F->StartAtNearestSpot(), TEXT("C0 старт рыбалки (без оружия в модели)"));
	}));
	Steps.Add(WaitFor([this]() { return WM->IsMsgBoxQueued(1, 81); }, 3.f, TEXT("C1 диалог 81"), [this]() { WM->CloseConfirm(true); }));
	Steps.Add(WaitFor([this]() { return F->GetFishState() == 107; }, 3.f, TEXT("C2 автомат 107"), [this]() { Feed(6168, B6168(1, kFish, 100)); }));
	Steps.Add(WaitFor([C]() { return C->GetFishAction() == 60 && C->GetFishRodComp(); }, 3.f, TEXT("C3 действие 60, удочка вставлена"), [this, C]()
	{
		Expect(C->GetFishRodMeshName() == TEXT("ch1_n_00_fird_00_00_0_00"), FString::Printf(TEXT("C3 МЕЛК-3: нет записи предмета → [ПОРТ] фолбэк fird_00 (%s)"), *C->GetFishRodMeshName()));
		C->SetWeaponFromItem(SwordId, Items);             // смена оружия при висящей удочке
	}));
	Steps.Add(WaitFor([]() { return true; }, 0.f, TEXT(""), [this, C]()
	{
		Expect(C->GetFishRodComp() && C->GetFishRodMeshName() == TEXT("ch1_n_00_fird_00_00_0_00"), TEXT("C4 КОНТРОЛЬ МЕЛК-1: своя смена оружия удочку не перезагружает"));
		F->ResetSessionState(EAsdaSessionResetReason::MapChanged);   // смена карты при живом действии
	}));
	Steps.Add(WaitFor([C]() { return C->GetFishAction() == 0 && !C->GetFishRodComp() && !C->GetFishHookComp() && !C->IsFishWeaponHidden(); }, 1.f,
		TEXT("C5 смена карты при действии 60: действие 0, удочка и рыба сняты, оружие не скрыто"), [this, C]()
	{
		Expect(!C->IsFishRodInserted(), TEXT("C5 признак вставки [15037] снят"));
		// Вторая попытка: в модели оружие НЕ удочка (img-запись есть, удочкиного меша нет) + клип 60 «сорван» ключом диагностики.
		SetCv(TEXT("asda.Fish.DebugFailClip"), 60);
		LM->CurrentMapId = 3;
		Expect(F->StartAtNearestSpot(), TEXT("C6 старт рыбалки (в модели — оружие не удочка)"));
	}));
	Steps.Add(WaitFor([this]() { return WM->IsMsgBoxQueued(1, 81); }, 3.f, TEXT("C7 диалог 81"), [this]() { WM->CloseConfirm(true); }));
	Steps.Add(WaitFor([this]() { return F->GetFishState() == 107; }, 3.f, TEXT("C8 автомат 107"), [this]() { Feed(6168, B6168(1, kFish, 100)); }));
	Steps.Add(WaitFor([this, C]() { return F->GetSelfFishingFlag() && C->IsFishRodInserted(); }, 3.f, TEXT("C9 6168 код 1: флаг 1, удочка «вставлена»"), [this, C]()
	{
		Expect(C->GetFishAction() == 0, TEXT("C9 МЕЛК-5: клип 60 сорван (ключ диагностики) — действие у персонажа не стоит"));
		Expect(C->GetFishRodComp() == nullptr, TEXT("C9 МЕЛК-3: img-запись есть, меша удочки нет → удочки нет, фолбэк НЕ берётся (как эталон)"));
		SetCv(TEXT("asda.Fish.DebugFailClip"), 0);
	}));
	Steps.Add(WaitFor([C]() { return C->IsFishRodInserted() && C->GetFishRodComp() == nullptr; }, 0.f, TEXT("C10 защёлка: повторной вставки (и предупреждения каждый кадр) нет"), nullptr));
	Steps.Add(WaitFor([this, C]() { return C->GetFishAction() == 61; }, 5.f, TEXT("C11 МЕЛК-5: страховка моста довела проход 60 по таймеру .jbb → 110 → действие 61"), [this, C]()
	{
		Expect(F->GetFishState() >= 110 && F->GetFishState() <= 115, FString::Printf(TEXT("C11 автомат не застрял в 112 (%d)"), F->GetFishState()));
		Expect(C->GetFishHookComp() != nullptr && !C->IsFishHookVisible(), TEXT("C12 МЕЛК-4: рыба вставлена, без удочки — скрыта [ИСПРАВЛЕНИЕ]"));
		SetCv(TEXT("asda.Fish.LegacyHookNoRod"), 1);
	}));
	Steps.Add(WaitFor([C]() { return C->IsFishHookVisible(); }, 1.f, TEXT("C13 asda.Fish.LegacyHookNoRod 1: без удочки рыба видна (как оригинал)"), [this]()
	{
		SetCv(TEXT("asda.Fish.LegacyHookNoRod"), 0);
	}));
	Steps.Add(WaitFor([C]() { return !C->IsFishHookVisible(); }, 1.f, TEXT("C14 ключ 0 → снова скрыта"), [this, C]()
	{
		F->ResetSessionState(EAsdaSessionResetReason::Disconnected);
		C->SetWeaponFromItem(kRod, Items);
	}));
	Steps.Add(WaitFor([C]() { return C->GetFishAction() == 0 && !C->IsFishRodInserted() && !C->GetFishHookComp(); }, 1.f, TEXT("C15 сброс сессии: всё снято"), nullptr));

	// ═══════════════ D. НАСТОЯЩАЯ СМЕРТЬ ВО ВРЕМЯ КЛИПА (fishfix2 09.10, ревизия fishing_fixes_REVIEW СРЕДН-1) ═══════════════
	//   Смерть — штатным пакетом 4053 (поза трупа у актора, `bSelfDead`), не подменой `TestDeadOverride`. Клип действия идёт по-
	//   настоящему (мост В1 выключен), смерть приходит посреди него. Ожидание [ПОРТ]: клип рыбалки на труп больше не ставится,
	//   4024 после смерти не уходит, автомат доходит до 0 страховкой по таймеру .jbb, поза трупа (anim_18) сохраняется до конца.
	//   Контроль: `asda.Fish.LegacyDeadClip 1` — клип на трупе снова ставится и 4024 уходит (дефект оригинала). Воскрешение — 4056.
	auto DeathEpisode = [this, C, WaitFor, Do](const FString& Tag, int32 State, int32 Action, bool bLegacy)
	{
		Steps.Add(Do([this, C, State, Action, bLegacy]()
		{
			SetCv(TEXT("asda.Fish.LegacyDeadClip"), bLegacy ? 1 : 0);
			F->ResetSessionState(EAsdaSessionResetReason::Disconnected);
			F->SetAutoRepeat(false);
			C->SetWeaponFromItem(kRod, Items);
			F->bSelfFishingFlag = true;
			F->CaughtItemId = (Action == 65) ? kFish : 0;
			F->b6171InFlight = false; F->bDeathCancelCalled = false;
			F->FishState = State;      // 112 → 60, 113 → 62, 126 → 65 (вход автомата в клип)
			Sent.Reset();
		}));
		Steps.Add(WaitFor([C, Action]() { return C->GetFishAction() == Action && CharClipIs(C, Action); }, 3.f,
			Tag + FString::Printf(TEXT(" живой: клип действия %d (anim_%d) играет"), Action, AAsdaPlayerCharacter::kFishClipBase + Action - 60),
			[this, C, Tag, Action]()
		{
			Expect(Sent4024(Action + 62), Tag + FString::Printf(TEXT(" живой: 4024 stateId %d соседям ушёл (контроль канала)"), Action + 62));
			DeathMark = Sent.Num();
			Feed(4053, B4053(Net->SelfUserNum));   // НАСТОЯЩАЯ смерть посреди клипа
		}));
		Steps.Add(WaitFor([this, C]() { return C->AnimState == EAsdaPlayerState::Dead && Net->bSelfDead; }, 1.f,
			Tag + TEXT(" 4053 → свой мёртв (поза трупа у актора, серверное «мёртв»)"), [this, C, Tag, bLegacy]()
		{
			// При ключе 1 автомат в том же кадре снова ставит клип на труп — эту проверку делает только ветка [ПОРТ].
			if (!bLegacy)
			{
				Expect(C->GetFishAction() == 0 && DeathClipIs(C), Tag + FString::Printf(TEXT(" смерть сняла действие, клип '%s' (ждём anim_18)"), *C->GetPlayingClipName()));
			}
		}));
		Steps.Add(FStep([this, C, Tag, Action, bLegacy](float T)
		{
			if (T < 0.6f) { return false; }    // 0.6 с автомат зовёт повтор действия каждый кадр
			const int32 N4024 = CountFrom(4024, DeathMark);
			if (!bLegacy)
			{
				Expect(C->GetFishAction() == 0 && DeathClipIs(C) && N4024 == 0,
					Tag + FString::Printf(TEXT(" [ПОРТ] повтор действия у трупа клип не ставит: действие %d, клип '%s', 4024 после смерти ×%d (ждём 0, 0, anim_18)"),
						C->GetFishAction(), *C->GetPlayingClipName(), N4024));
				Expect(F->GetSelfAction() == Action || F->GetFishState() != (Action == 60 ? 112 : Action == 62 ? 113 : 126),
					Tag + FString::Printf(TEXT(" действие ведёт страховка (своё %d, автомат %d)"), F->GetSelfAction(), F->GetFishState()));
			}
			else
			{
				Expect(C->GetFishAction() == Action && CharClipIs(C, Action) && N4024 >= 1,
					Tag + FString::Printf(TEXT(" КОНТРОЛЬ asda.Fish.LegacyDeadClip 1: клип %d на трупе снова (действие %d, '%s'), 4024 ×%d (дефект оригинала воспроизведён)"),
						Action, C->GetFishAction(), *C->GetPlayingClipName(), N4024));
			}
			return true;
		}));
		if (Action == 60 && !bLegacy)
		{
			// Заброс: страховка доводит проход 60 → 110 → 61 (мини-игра у трупа); сервер на 6171(2) отвечает 6172 {2} → 128.
			Steps.Add(WaitFor([this]() { return F->GetFishState() >= 110 && F->GetFishState() <= 115; }, 6.f,
				Tag + TEXT(" страховка по таймеру .jbb довела проход 60 → 110 (клипа нет)"), [this]()
			{
				Feed(6172, B6172(2, 0, 0, 0));
			}));
		}
		Steps.Add(WaitFor([this]() { return F->GetFishState() == 0; }, 12.f, Tag + TEXT(" автомат дошёл до 0"), [this, C, Tag, bLegacy]()
		{
			const int32 N4024 = CountFrom(4024, DeathMark);
			const int32 N6171 = CountFrom(6171, DeathMark);
			if (!bLegacy)
			{
				Expect(DeathClipIs(C) && C->GetFishAction() == 0 && N4024 == 0 && N6171 == 1,
					Tag + FString::Printf(TEXT(" итог: поза трупа '%s' (ждём anim_18), действие %d, 4024 после смерти ×%d (ждём 0 — соседи рыбачащий труп не видят), 6171(2) ×%d (ждём 1)"),
						*C->GetPlayingClipName(), C->GetFishAction(), N4024, N6171));
			}
			else
			{
				Expect(!DeathClipIs(C) && C->AnimState == EAsdaPlayerState::Dead,
					Tag + FString::Printf(TEXT(" КОНТРОЛЬ ключ 1: тело застыло в клипе рыбалки '%s', не в позе трупа (дефект оригинала)"), *C->GetPlayingClipName()));
			}
			SetCv(TEXT("asda.Fish.LegacyDeadClip"), 0);
			Feed(4056, B4056(Net->SelfUserNum, Net->SelfTileX, Net->SelfTileZ));   // воскрешение на месте
		}));
		Steps.Add(WaitFor([this, C]() { return C->AnimState != EAsdaPlayerState::Dead && !Net->bSelfDead; }, 2.f, Tag + TEXT(" 4056 → воскрешён"), nullptr));
	};
	DeathEpisode(TEXT("D1 заброс"), 112, 60, false);
	DeathEpisode(TEXT("D2 вытаскивание"), 113, 62, false);
	DeathEpisode(TEXT("D3 итог ловли (удача)"), 126, 65, false);
	DeathEpisode(TEXT("D4 вытаскивание"), 113, 62, true);
	// Труп соседа: 4025 stateId 122 у мёртвого соседа клип не ставит (общая точка `PlayFishAction`), контроль — ключ 1.
	Steps.Add(Do([this]()
	{
		if (Peer2.IsValid()) { Peer2->StopFishAction(TEXT("самотест D5")); }
		Feed(4053, B4053(kPeer2Num));
	}));
	Steps.Add(WaitFor([this]() { return Peer2.IsValid() && Peer2->AnimState == EAsdaPlayerState::Dead; }, 1.f, TEXT("D5 4053 → сосед мёртв"), [this]()
	{
		Feed(4025, B4025(kPeer2Num, 122, 1.f, 0.f));
	}));
	Steps.Add(FStep([this](float T)
	{
		if (T < 0.3f) { return false; }
		Expect(Peer2.IsValid() && Peer2->GetFishAction() == 0 && DeathClipIs(Peer2.Get()),
			FString::Printf(TEXT("D5 [ПОРТ] 4025 122 у трупа соседа клип заброса не ставит: действие %d, клип '%s' (ждём 0, anim_18)"),
				Peer2.IsValid() ? Peer2->GetFishAction() : -1, Peer2.IsValid() ? *Peer2->GetPlayingClipName() : TEXT("нет")));
		SetCv(TEXT("asda.Fish.LegacyDeadClip"), 1);
		Feed(4025, B4025(kPeer2Num, 122, 1.f, 0.f));
		return true;
	}));
	Steps.Add(WaitFor([this]() { return Peer2.IsValid() && Peer2->GetFishAction() == 60; }, 1.f,
		TEXT("D6 КОНТРОЛЬ asda.Fish.LegacyDeadClip 1: у трупа соседа клип 60 снова ставится (как оригинал)"), [this]()
	{
		SetCv(TEXT("asda.Fish.LegacyDeadClip"), 0);
		Peer2->StopFishAction(TEXT("самотест D6"));
	}));

	// ═══════════════ E. СВОЙ: ЛОПАТА 40/43 — СЕТЬ, КОНЕЦ ПО КЛИПУ, ГЕЙТ СОСТОЯНИЯ (six_items 09.10.2026, СРЕДН-1/СРЕДН-2) ═══════════════
	//   Путь игрока: код результата 11 пакета 5029 штатным разбором (`ApplyUseResult` → `ApplyUseRefusal` case 11 →
	//   `ApplyShovelingTool`), отправки под перехватом. Эталон `Item_UseShovelingTool` (item_inventory.c:39344-39430):
	//   при состоянии 0 — 4008, 4024 110|114, состояние 40|43; конец клипа (case 7/10) — свой шлёт 5428 {userNum, 1}.
	//   При состоянии ≠ 0 (ход, сидение, рыбалка …) — молча ничего.
	Steps.Add(Do([this, C]()
	{
		if (Net->bSelfDead) { Feed(4056, B4056(Net->SelfUserNum, Net->SelfTileX, Net->SelfTileZ)); }
		F->ResetSessionState(EAsdaSessionResetReason::Disconnected);
		C->StopFishAction(TEXT("самотест E0"));
		C->StopMove();
		for (const FAsdaItemRecord& Rec : Items->GetAllItems())
		{
			if (ShovelId == 0 && Rec.ItemType == 11 && Rec.ItemSubType == 33) { ShovelId = Rec.ItemCode; }
			if (AutoShovelId == 0 && Rec.ItemType == 11 && Rec.ItemSubType == 34) { AutoShovelId = Rec.ItemCode; }
		}
		WeightCurWas = Net->SelfWeightCur; WeightMaxWas = Net->SelfWeightMax; bWeightSaved = true;
		Net->SelfWeightCur = 0; Net->SelfWeightMax = 1000;   // гейт веса 90 % (эталон :39330) пройден
		Expect(ShovelId > 0 && AutoShovelId > 0, FString::Printf(TEXT("E0 найдены лопата %d (11/33) и авто-лопата %d (11/34)"), ShovelId, AutoShovelId));
	}));
	Steps.Add(WaitFor([this, C]() { return !Net->bSelfDead && KbState() == 0 && C->GetEtalonShovelState() == 0; }, 6.f,
		TEXT("E1 свой в покое: состояние действия 0"), [this]()
	{
		ShovelMark = Sent.Num();
		Net->bActionPending = true;   // незакрытый 4006 (сервер считает нас идущими) — эталонный гейт 4008 открыт
		Feed(5029, B5029(11, Net->SelfUserNum, ShovelId));
	}));
	for (int32 Pass = 0; Pass < 2; ++Pass)
	{
		const int32 St = Pass == 0 ? 40 : 43;
		const int32 Code = Pass == 0 ? 110 : 114;
		const float Want = Pass == 0 ? 6.0f : 9.034f;
		Steps.Add(WaitFor([C, St]() { return C->GetEtalonShovelState() == St; }, 1.f,
			FString::Printf(TEXT("E2 код 11 (%s) из покоя → состояние %d"), Pass == 0 ? TEXT("лопата") : TEXT("авто-лопата"), St), [this, C, St, Code]()
		{
			ShovelT0 = World->GetTimeSeconds();
			Expect(CountFrom(4008, ShovelMark) == 1 && Sent4024From(Code, ShovelMark),
				FString::Printf(TEXT("E2 ушли 4008 (%d) и 4024 %d (%d) — как эталон item_inventory.c:39356/39363"),
					CountFrom(4008, ShovelMark), Code, Sent4024From(Code, ShovelMark) ? 1 : 0));
			Expect(C->bWeaponMotionHidden && KbState() == St,
				FString::Printf(TEXT("E2 «занят»: оружие за спиной %d, состояние клавиатуры %d"), C->bWeaponMotionHidden ? 1 : 0, KbState()));
		}));
		Steps.Add(WaitFor([C]() { return C->GetEtalonShovelState() == 0; }, Want + 1.f,
			FString::Printf(TEXT("E3 свой: состояние %d снялось само (конец клипа)"), St), [this, C, St, Want, Pass]()
		{
			const double Dt = World->GetTimeSeconds() - ShovelT0;
			Expect(FMath::Abs(Dt - Want) <= 0.2, FString::Printf(TEXT("E3 состояние %d снято через %.3f с (эталон %.3f ± 0,2)"), St, Dt, Want));
			Expect(Sent5428From(ShovelMark) && CountFrom(5428, ShovelMark) == 1,
				FString::Printf(TEXT("E3 конец клипа своего: 5428 {свой userNum, 1} ушёл ровно раз (%d) — `Combat_SendSelectTarget(1)` эталона"),
					CountFrom(5428, ShovelMark)));
			Expect(!C->bWeaponMotionHidden && KbState() == 0 && IdleClipIs(C),
				FString::Printf(TEXT("E3 после конца: оружие в руке (флаг %d), состояние %d, клип покоя '%s'"),
					C->bWeaponMotionHidden ? 1 : 0, KbState(), *C->GetPlayingClipName()));
			ShovelMark = Sent.Num();
			if (Pass == 0) { Net->bActionPending = true; Feed(5029, B5029(11, Net->SelfUserNum, AutoShovelId)); }
		}));
	}
	// Смена состояния снимает и таймер конца: после хода 5428 не уходит.
	Steps.Add(Do([this]() { ShovelMark = Sent.Num(); Feed(5029, B5029(11, Net->SelfUserNum, ShovelId)); }));
	Steps.Add(WaitFor([C]() { return C->GetEtalonShovelState() == 40; }, 1.f, TEXT("E4 снова 40"), [C]()
	{
		C->SetAnimState(EAsdaPlayerState::Idle);   // любая смена состояния (`CChar_SetState`) — снимает 40
	}));
	Steps.Add(FStep([this, C](float T)
	{
		if (T < 6.5f) { return false; }
		Expect(C->GetEtalonShovelState() == 0 && CountFrom(5428, ShovelMark) == 0,
			FString::Printf(TEXT("E4 смена состояния сняла 40 вместе с таймером: через %.1f с 5428 не ушёл (%d)"), T, CountFrom(5428, ShovelMark)));
		return true;
	}));
	// ── СРЕДН-2: гейт «состояние ≠ 0 → молча ничего» — сидение, рыбалка, ход ──
	Steps.Add(Do([C]() { C->BeginSitDown(); }));
	Steps.Add(WaitFor([this]() { return KbState() == 7 || KbState() == 8; }, 2.f, TEXT("E5 свой садится (состояние 7/8)"), [this]()
	{
		ShovelMark = Sent.Num();
		Net->bActionPending = true;   // гейт 4008 открыт — проверка «4008 не ушёл» различает
		Feed(5029, B5029(11, Net->SelfUserNum, ShovelId));
	}));
	Steps.Add(FStep([this, C](float T)
	{
		if (T < 0.5f) { return false; }
		Expect(C->GetEtalonShovelState() == 0 && CountFrom(4008, ShovelMark) == 0 && CountFrom(4024, ShovelMark) == 0 && C->GetSitPhase() != 0,
			FString::Printf(TEXT("E5 СРЕДН-2: копание сидя не начинается — состояние %d, 4008 %d, 4024 %d, сидение %d сохранено"),
				C->GetEtalonShovelState(), CountFrom(4008, ShovelMark), CountFrom(4024, ShovelMark), C->GetSitPhase()));
		C->SetAnimState(EAsdaPlayerState::Idle);
		C->PlayFishAction(61, false);   // действие рыбалки 61 (эталонное +47551 = 61)
		return true;
	}));
	Steps.Add(WaitFor([this, C]() { return C->GetFishAction() == 61 && KbState() == 61; }, 2.f, TEXT("E6 своё действие рыбалки 61"), [this]()
	{
		ShovelMark = Sent.Num();
		Net->bActionPending = true;   // гейт 4008 открыт — проверка «4008 не ушёл» различает
		Feed(5029, B5029(11, Net->SelfUserNum, AutoShovelId));
	}));
	Steps.Add(FStep([this, C](float T)
	{
		if (T < 0.5f) { return false; }
		Expect(C->GetEtalonShovelState() == 0 && C->GetFishAction() == 61 && CountFrom(4008, ShovelMark) == 0 && CountFrom(4024, ShovelMark) == 0,
			FString::Printf(TEXT("E6 СРЕДН-2: копание во время рыбалки не начинается — состояние %d, рыбалка %d не сбита, 4008 %d, 4024 %d"),
				C->GetEtalonShovelState(), C->GetFishAction(), CountFrom(4008, ShovelMark), CountFrom(4024, ShovelMark)));
		C->StopFishAction(TEXT("самотест E6"));
		C->NetMoveFromActor(C->GetGamePosXZ() + FVector2D(600.f, 0.f), 0.35f);   // ход (состояние 1)
		return true;
	}));
	Steps.Add(WaitFor([this, C]() { return C->IsMoving() && KbState() == 1; }, 2.f, TEXT("E7 свой идёт (состояние 1)"), [this]()
	{
		ShovelMark = Sent.Num();
		Net->bActionPending = true;   // гейт 4008 открыт — проверка «4008 не ушёл» различает
		Feed(5029, B5029(11, Net->SelfUserNum, ShovelId));
	}));
	Steps.Add(FStep([this, C](float T)
	{
		if (T < 0.3f) { return false; }
		Expect(C->GetEtalonShovelState() == 0 && C->IsMoving() && CountFrom(4008, ShovelMark) == 0 && CountFrom(4024, ShovelMark) == 0,
			FString::Printf(TEXT("E7 СРЕДН-2: копание на ходу не начинается — состояние %d, ход не остановлен %d, 4008 %d, 4024 %d"),
				C->GetEtalonShovelState(), C->IsMoving() ? 1 : 0, CountFrom(4008, ShovelMark), CountFrom(4024, ShovelMark)));
		C->StopMove();
		return true;
	}));
	// Контроль ключа сети: `asda.Shovel.NetSync 0` — состояние ставится, но ни 4024, ни 5428 (прежний порт).
	Steps.Add(WaitFor([this, C]() { return KbState() == 0 && !C->IsMoving(); }, 3.f, TEXT("E8 снова покой"), [this]()
	{
		SetCv(TEXT("asda.Shovel.NetSync"), 0);
		ShovelMark = Sent.Num();
		Net->bActionPending = true;
		Feed(5029, B5029(11, Net->SelfUserNum, ShovelId));
	}));
	Steps.Add(WaitFor([C]() { return C->GetEtalonShovelState() == 40; }, 1.f, TEXT("E8 КОНТРОЛЬ asda.Shovel.NetSync 0: состояние 40 ставится"), nullptr));
	Steps.Add(WaitFor([C]() { return C->GetEtalonShovelState() == 0; }, 7.f, TEXT("E8 КОНТРОЛЬ: конец клипа снял 40"), [this]()
	{
		Expect(CountFrom(4008, ShovelMark) == 1 && CountFrom(4024, ShovelMark) == 0 && CountFrom(5428, ShovelMark) == 0,
			FString::Printf(TEXT("E8 КОНТРОЛЬ ключ 0: 4008 %d, 4024 %d, 5428 %d (прежний порт — сети копания нет; проверка различает)"),
				CountFrom(4008, ShovelMark), CountFrom(4024, ShovelMark), CountFrom(5428, ShovelMark)));
		SetCv(TEXT("asda.Shovel.NetSync"), 1);
	}));

	// ═══════════════ F. ЦИКЛ АВТОЛОПАТЫ (autoshovel 09.10.2026, ревизия six_fixes п. 6 / СРЕДН-1 / МЕЛК-1) ═══════════════
	//   Путь игрока: применение — двойной клик по ячейке расходников (`UseByDoubleClick` → `UseConsumable`, 5028 под перехватом);
	//   ответы — штатный `HandlePacket` (5029 код 11, 5005 добыча на земле, 5013 подбор, 5015 снять с тайла, 4053/4056).
	//   Время цикла — НАСТОЯЩЕЕ (13 с, три цикла). В проверках условий остановки, где нужен лишь «срок вышел», отсчёт таймера
	//   сдвигается на 13 с назад (`StartS`) — код кадра цикла тот же, ждать не нужно.
	auto Shift13 = [this]() { Inv->AutoDig.StartS -= 13.0; };       // «срок вышел» без ожидания
	auto Idle0 = [C]() { C->SetAnimState(EAsdaPlayerState::Idle); };  // любая смена состояния — снимает 40/43 (покой 0)
	Steps.Add(Do([this, C]()
	{
		if (Net->bSelfDead) { Feed(4056, B4056(Net->SelfUserNum, Net->SelfTileX, Net->SelfTileZ)); }
		C->StopMove();
		SetCv(TEXT("asda.Shovel.AutoLoop"), 1); SetCv(TEXT("asda.Shovel.AutoLoopFix"), 1);
		SetCv(TEXT("asda.Shovel.ClipEnd"), 1); SetCv(TEXT("asda.Shovel.NetSync"), 1);
		if (!bWeightSaved) { WeightCurWas = Net->SelfWeightCur; WeightMaxWas = Net->SelfWeightMax; bWeightSaved = true; }
		Net->SelfWeightCur = 0; Net->SelfWeightMax = 1000;
		for (int32 c = 0; c < Inv->GetContainerSize(2); ++c) { if (Inv->GetCellItemCode(2, c) == 0) { DigCell = c; break; } }
		if (DigCell >= 0) { Inv->DebugPutItem(2, DigCell, AutoShovelId, 1); }
		Inv->bActionPending = false; Net->bItemPickPending = false;
		Inv->AutoDigReset(TEXT("самотест F0")); Inv->AutoDig = FAsdaAutoDigTimer();
		Sink = new FLogSink(); GLog->AddOutputDevice(Sink);
		Expect(DigCell >= 0 && AutoShovelId > 0 && Inv->GetCellItemCode(2, DigCell) == AutoShovelId,
			FString::Printf(TEXT("F0 авто-лопата %d положена в расходники, ячейка %d"), AutoShovelId, DigCell));
	}));
	Steps.Add(WaitFor([this, C]() { return !Net->bSelfDead && KbState() == 0 && C->GetEtalonShovelState() == 0 && !C->IsMoving(); }, 6.f,
		TEXT("F1 свой в покое"), [this]()
	{
		DigMark = Sent.Num(); Sink->Reset();
		const bool bUsed = Inv->UseByDoubleClick(2, DigCell);   // путь игрока: двойной клик по ячейке
		const FSent* S = FirstFrom(5028, DigMark);
		const FAsdaAutoDigTimer& T = Inv->GetAutoDigTimer();
		Expect(bUsed && CountFrom(5028, DigMark) == 1 && S && S->P.Num() == 5 && S->P[0] == 2 && AsdaBytes::RdI16(S->P.GetData() + 1) == DigCell,
			FString::Printf(TEXT("F1 двойной клик: 5028 ушёл ровно раз (%d), тело {контейнер 2, слот %d}"), CountFrom(5028, DigMark), DigCell));
		Expect(T.Slot == DigCell && T.Cont == 2 && !T.bActive,
			FString::Printf(TEXT("F1 гейт 11/34 запомнил слот %d и контейнер %d (эталон set_o9/set_oA), таймер ещё не идёт (%d)"), T.Slot, T.Cont, T.bActive ? 1 : 0));
		Feed(5029, B5029(11, Net->SelfUserNum, AutoShovelId));
	}));
	// Три цикла: 1 и 2 — с добычей на земле (5005 → 5012 → 5013/5015), 3 — без добычи (повтор без подбора).
	for (int32 Cy = 1; Cy <= 3; ++Cy)
	{
		Steps.Add(WaitFor([this, C]() { return C->GetEtalonShovelState() == 43 && Inv->GetAutoDigTimer().bActive && Inv->GetAutoDigTimer().Level == 0; }, 2.f,
			FString::Printf(TEXT("F2.%d код 11 → копка 43, таймер 13 с запущен (уровень 0)"), Cy), [this, Cy]()
		{
			const FAsdaAutoDigTimer& T = Inv->GetAutoDigTimer();
			Expect(T.ItemCode == AutoShovelId && T.DurationMs == 13000 && T.Mode == 34,
				FString::Printf(TEXT("F2.%d таймер: предмет %d, %d мс, режим %d (эталон timerStart(13000) + set_o8(34))"), Cy, T.ItemCode, T.DurationMs, T.Mode));
			if (Cy == 1)
			{
				// СРЕДН-1: повторное применение во время копки — 9002, 5028 не уходит, цикл не трогается.
				const int32 M = Sent.Num(); Sink->Reset();
				Inv->UseByDoubleClick(2, DigCell);
				Expect(CountFrom(5028, M) == 0 && Sink->Hits(TEXT("нотис 9002:")) == 1 && Inv->GetAutoDigTimer().bActive,
					FString::Printf(TEXT("F2.1 СРЕДН-1: двойной клик во время 43 — нотис 9002 (%d), 5028 не ушёл (%d), цикл идёт"),
						Sink->Hits(TEXT("нотис 9002:")), CountFrom(5028, M)));
			}
			if (Cy <= 2)
			{
				const FIntPoint My = MyTile();
				DigLootTile = My + (Cy == 1 ? FIntPoint(1, 0) : FIntPoint(0, 1));
				Feed(5005, B5005(kFish, DigLootTile));   // добыча рядом (≤ 300 юнитов)
			}
			DigMark = Sent.Num();
		}));
		if (Cy <= 2)
		{
			Steps.Add(WaitFor([this]() { return WS->FindLootAtTile(DigLootTile) != nullptr; }, 1.f,
				FString::Printf(TEXT("F3.%d добыча 5005 лежит на соседнем тайле"), Cy), nullptr));
		}
		Steps.Add(WaitFor([C]() { return C->GetEtalonShovelState() == 0; }, 10.f,
			FString::Printf(TEXT("F4.%d копка закончилась (конец клипа 43)"), Cy), [this, Cy]()
		{
			Expect(Inv->GetAutoDigTimer().bActive && CountFrom(5012, DigMark) == 0 && CountFrom(5028, DigMark) == 0 && DigAge() < 12.5,
				FString::Printf(TEXT("F4.%d после копки цикл ждёт срока: таймер идёт, %.2f с, 5012 %d, 5028 %d"),
					Cy, DigAge(), CountFrom(5012, DigMark), CountFrom(5028, DigMark)));
		}));
		if (Cy <= 2)
		{
			Steps.Add(WaitFor([this]() { return CountFrom(5012, DigMark) >= 1; }, 6.f,
				FString::Printf(TEXT("F5.%d срок 13 с — подбор 5012"), Cy), [this, Cy]()
			{
				const FSent* S = FirstFrom(5012, DigMark);
				const FIntPoint My = MyTile();
				Expect(FMath::Abs(DigAge() - 13.0) <= 0.35, FString::Printf(TEXT("F5.%d 5012 через %.3f с от старта (эталон 13,000 ± 0,35)"), Cy, DigAge()));
				Expect(S && S->P.Num() == 8 && AsdaBytes::RdI16(S->P.GetData()) == My.X && AsdaBytes::RdI16(S->P.GetData() + 2) == My.Y
					&& AsdaBytes::RdI16(S->P.GetData() + 4) == DigLootTile.X && AsdaBytes::RdI16(S->P.GetData() + 6) == DigLootTile.Y,
					FString::Printf(TEXT("F5.%d 5012 {свой тайл (%d,%d), тайл добычи (%d,%d)} — без хода к предмету"), Cy, My.X, My.Y, DigLootTile.X, DigLootTile.Y));
				Expect(CountFrom(5028, DigMark) == 0 && Net->bItemPickPending,
					FString::Printf(TEXT("F5.%d подбор в пути — повтора ещё нет (5028 %d)"), Cy, CountFrom(5028, DigMark)));
				// ★ 09.10.2026 (МЕЛК-1 ревизии autoshovel_fishbooks): гейт эталона `sub_92101B` в общей точке 5012 — второй подбор,
				//   пока первый без ответа, не уходит ни из какого источника; [ПОРТ] сторож снимает флаг через N с молчания.
				if (Cy == 1)
				{
					const bool bSecond = Net->SendItemPick5012(MyTile(), DigLootTile);   // «Поднять» из окна действий / клик по луту
					Expect(!bSecond && CountFrom(5012, DigMark) == 1 && Net->bItemPickPending,
						FString::Printf(TEXT("F5.1г второй 5012 при подборе в пути не ушёл (вернул %d, 5012 всего %d) — эталон sub_92101B"), bSecond ? 1 : 0, CountFrom(5012, DigMark)));
					IConsoleVariable* Guard = IConsoleManager::Get().FindConsoleVariable(TEXT("asda.Loot.PickPendingGuardSec"));
					const double Stamp = Net->ItemPickStampMs;   // метка отправки — вернём после проверки
					Net->ItemPickStampMs = Stamp - 6000.0;        // «молчание сервера» 6 с
					if (Guard) { AsdaCVarSave::SetForTest(TEXT("asda.Loot.PickPendingGuardSec"), 0.f); }
					const bool bEtalonKeeps = Net->IsItemPickPending();   // ключ 0 — как эталон, без сторожа
					if (Guard) { AsdaCVarSave::SetForTest(TEXT("asda.Loot.PickPendingGuardSec"), 5.f); }
					const bool bGuardKeeps = Net->IsItemPickPending();    // ключ 5 — сторож снимает
					Expect(bEtalonKeeps && !bGuardKeeps && !Net->bItemPickPending,
						FString::Printf(TEXT("F5.1г сторож [ПОРТ]: 6 с без ответа — при ключе 0 флаг держится (%d), при 5 с снят (%d)"), bEtalonKeeps ? 1 : 0, bGuardKeeps ? 0 : 1));
					Net->bItemPickPending = true;                 // вернуть «в пути»: ответ 5013/5015 ниже снимет его штатно
					Net->ItemPickStampMs = Stamp;
				}
				// Ответ сервера на подбор: 5013 (свой, предмет в сумку) и 5015 (снять с тайла).
				int32 Bag = -1;
				for (int32 c = 0; c < Inv->GetContainerSize(1); ++c) { if (Inv->GetCellItemCode(1, c) == 0 && !DigBagCells.Contains(c)) { Bag = c; break; } }
				if (Bag >= 0) { DigBagCells.Add(Bag); }
				Feed(5013, B5013(kFish, FMath::Max(0, Bag)));
				Feed(5015, B5015(DigLootTile));
			}));
		}
		Steps.Add(WaitFor([this]() { return CountFrom(5028, DigMark) >= 1; }, Cy <= 2 ? 3.f : 15.f,
			FString::Printf(TEXT("F6.%d повтор применения 5028"), Cy), [this, Cy]()
		{
			const FSent* S = FirstFrom(5028, DigMark);
			Expect(S && S->P.Num() == 5 && S->P[0] == 2 && AsdaBytes::RdI16(S->P.GetData() + 1) == DigCell && CountFrom(5028, DigMark) == 1,
				FString::Printf(TEXT("F6.%d повтор 5028 {контейнер 2, слот %d} ушёл ровно раз (%d) — inventory_SendUseItemRequest"), Cy, DigCell, CountFrom(5028, DigMark)));
			if (Cy <= 2)
			{
				Expect(WS->FindLootAtTile(DigLootTile) == nullptr && DigAge() <= 13.0 + 1.6,
					FString::Printf(TEXT("F6.%d добыча поднята (тайл пуст), повтор через %.3f с (≤ 13 + 1,5 с просрочки)"), Cy, DigAge()));
			}
			else
			{
				Expect(CountFrom(5012, DigMark) == 0 && FMath::Abs(DigAge() - 13.0) <= 0.35,
					FString::Printf(TEXT("F6.3 без добычи: 5012 нет (%d), повтор через %.3f с (три кадра попыток подбора)"), CountFrom(5012, DigMark), DigAge()));
			}
			Expect(Inv->GetAutoDigTimer().Repeats == Cy, FString::Printf(TEXT("F6.%d повторов в цикле %d"), Cy, Inv->GetAutoDigTimer().Repeats));
			Feed(5029, B5029(11, Net->SelfUserNum, AutoShovelId));   // ответ сервера на повтор — снова «копать»
		}));
	}
	// ── ★ 05.11.2026 (bug-sowel-level): Z — ОГЛУШЕНИЕ ВО ВРЕМЯ АВТОЛОПАТЫ. Эталон `inventory_SendUseItemRequest@0xD13EA0`
	//   (item_inventory.c:41046-41050): срок вышел, но персонаж в ограничивающем состоянии (`listProbeEntryTypesSetState`) →
	//   плашка 7007, 5028 НЕ уходит, таймер НЕ сбрасывается; снятие состояния — повтор уходит сам. Состояние — настоящий 5272/5268.
	Steps.Add(WaitFor([this, C]() { return C->GetEtalonShovelState() == 43 && Inv->GetAutoDigTimer().bActive && Inv->GetAutoDigTimer().Level == 0; }, 2.f,
		TEXT("Z0 цикл перезапущен ответом на повтор (43, таймер)"), [this]()
	{
		// Срок НЕ сдвигаем: гейт 9002 (копка 40/43 ещё идёт) у эталона стоит РАНЬШЕ 7007 — ждём настоящие 13 с цикла.
		const UAsdaEffectsSubsystem* Fx = Inv->GetGameInstance() ? Inv->GetGameInstance()->GetSubsystem<UAsdaEffectsSubsystem>() : nullptr;
		const int32 Self = Fx ? Fx->SelfUserIdx() : 0;
		TArray<uint8> B; B.SetNumZeroed(16);
		B[0] = (uint8)(Self & 0xFF); B[1] = (uint8)((Self >> 8) & 0xFF); B[2] = 3; B[9] = 1; B[11] = 0x58; B[12] = 0x02;   // состояние 3, 600 с
		Feed(5272, B);
		StunRefusals0 = AsdaItemUseStateGate::GetRefusalCount();
		DigMark = Sent.Num();
	}));
	Steps.Add(WaitFor([this]() { return AsdaItemUseStateGate::GetRefusalCount() > StunRefusals0; }, 15.f, TEXT("Z1 под оглушением повтор отклонён гейтом"), [this]()
	{
		Expect(CountFrom(5028, DigMark) == 0 && Inv->GetAutoDigTimer().bActive && AsdaItemUseStateGate::IsSelfStateLocked(Inv->GetGameInstance()),
			FString::Printf(TEXT("Z1 оглушение (5272 состояние 3): повтор 5028 НЕ ушёл (%d), плашка 7007 (отказов %d→%d), таймер цикла цел — как эталон"),
				CountFrom(5028, DigMark), StunRefusals0, AsdaItemUseStateGate::GetRefusalCount()));
		const UAsdaEffectsSubsystem* Fx = Inv->GetGameInstance() ? Inv->GetGameInstance()->GetSubsystem<UAsdaEffectsSubsystem>() : nullptr;
		const int32 Self = Fx ? Fx->SelfUserIdx() : 0;
		TArray<uint8> R; R.SetNumZeroed(4);
		R[0] = (uint8)(Self & 0xFF); R[1] = (uint8)((Self >> 8) & 0xFF); R[2] = 3;
		Feed(5268, R);   // оглушение снято
	}));
	Steps.Add(WaitFor([this]() { return CountFrom(5028, DigMark) >= 1; }, 3.f, TEXT("Z2 после снятия оглушения повтор ушёл"), [this]()
	{
		Expect(CountFrom(5028, DigMark) == 1 && !AsdaItemUseStateGate::IsSelfStateLocked(Inv->GetGameInstance()),
			FString::Printf(TEXT("Z2 5268 сняло оглушение — повтор 5028 ушёл ровно раз (%d), цикл продолжился сам"), CountFrom(5028, DigMark)));
		Feed(5029, B5029(11, Net->SelfUserNum, AutoShovelId));   // ответ сервера — снова «копать»
	}));
	// ── Условия остановки. S1 — ход (состояние 1) во время копки 43 ──
	Steps.Add(WaitFor([this, C]() { return C->GetEtalonShovelState() == 43 && Inv->GetAutoDigTimer().bActive && Inv->GetAutoDigTimer().Level == 0; }, 2.f,
		TEXT("S1 цикл перезапущен ответом на повтор (43, таймер)"), [C]()
	{
		C->NetMoveFromActor(C->GetGamePosXZ() + FVector2D(600.f, 0.f), 0.35f);   // ход
	}));
	Steps.Add(WaitFor([this]() { return !Inv->GetAutoDigTimer().bActive; }, 1.5f, TEXT("S1 ход сбросил цикл (покадровая проверка состояния)"), [this, C]()
	{
		C->StopMove();
		DigMark = Sent.Num();
	}));
	Steps.Add(FStep([this](float T)
	{
		if (T < 14.f) { return false; }
		Expect(CountFrom(5028, DigMark) == 0 && CountFrom(5012, DigMark) == 0 && !Inv->GetAutoDigTimer().bActive,
			FString::Printf(TEXT("S1 через %.1f с после хода ни подбора (%d), ни повтора (%d)"), T, CountFrom(5012, DigMark), CountFrom(5028, DigMark)));
		return true;
	}));
	// S2 — смерть (4053) во время копки.
	Steps.Add(WaitFor([this, C]() { return KbState() == 0 && !C->IsMoving(); }, 3.f, TEXT("S2 покой"), [this]()
	{
		Feed(5029, B5029(11, Net->SelfUserNum, AutoShovelId));
	}));
	Steps.Add(WaitFor([this, C]() { return C->GetEtalonShovelState() == 43 && Inv->GetAutoDigTimer().bActive; }, 2.f, TEXT("S2 копка 43, цикл идёт"), [this]()
	{
		Feed(4053, B4053(Net->SelfUserNum));
	}));
	Steps.Add(WaitFor([this]() { return !Inv->GetAutoDigTimer().bActive; }, 1.f, TEXT("S2 смерть сбросила цикл"), [this]()
	{
		Feed(4056, B4056(Net->SelfUserNum, Net->SelfTileX, Net->SelfTileZ));
	}));
	// S3 — 7505: ответ «копать» при полном контейнере расходников.
	Steps.Add(WaitFor([this, C]() { return !Net->bSelfDead && KbState() == 0 && C->GetEtalonShovelState() == 0; }, 6.f, TEXT("S3 ожил, покой"), [this]()
	{
		Feed(5029, B5029(11, Net->SelfUserNum, AutoShovelId));
	}));
	Steps.Add(WaitFor([this, C]() { return C->GetEtalonShovelState() == 43 && Inv->GetAutoDigTimer().bActive; }, 2.f, TEXT("S3 цикл идёт"), [this]()
	{
		for (int32 c = 0; c < Inv->GetContainerSize(2); ++c)
		{
			if (Inv->GetCellItemCode(2, c) == 0) { Inv->DebugPutItem(2, c, kBait, 1); DigFilledCells.Add(c); }
		}
		Sink->Reset();
		Feed(5029, B5029(11, Net->SelfUserNum, AutoShovelId));
	}));
	Steps.Add(WaitFor([this]() { return !Inv->GetAutoDigTimer().bActive; }, 1.f, TEXT("S3 7505 (нет места) сбросил цикл"), [this, Idle0]()
	{
		Expect(Sink->Hits(TEXT("нотис 7505:")) == 1, FString::Printf(TEXT("S3 нотис 7505 (%d)"), Sink->Hits(TEXT("нотис 7505:"))));
		for (const int32 Cc : DigFilledCells) { Inv->DebugPutItem(2, Cc, 0, 0); }
		DigFilledCells.Reset();
		Idle0();
	}));
	// S4 — ручная лопата 33 гасит цикл (эталон :39397), покой 0 сам по себе цикл НЕ гасит.
	Steps.Add(WaitFor([this, C]() { return KbState() == 0 && C->GetEtalonShovelState() == 0; }, 2.f, TEXT("S4 покой"), [this]()
	{
		Feed(5029, B5029(11, Net->SelfUserNum, AutoShovelId));
	}));
	Steps.Add(WaitFor([this, C]() { return C->GetEtalonShovelState() == 43 && Inv->GetAutoDigTimer().bActive; }, 2.f, TEXT("S4 цикл идёт"), [Idle0]() { Idle0(); }));
	Steps.Add(FStep([this](float T)
	{
		if (T < 0.3f) { return false; }
		Expect(Inv->GetAutoDigTimer().bActive && KbState() == 0, TEXT("S4 состояние 0 цикл не гасит (эталон: сброс только при ≠ 0, 40, 43)"));
		Feed(5029, B5029(11, Net->SelfUserNum, ShovelId));
		return true;
	}));
	Steps.Add(WaitFor([this, C]() { return C->GetEtalonShovelState() == 40; }, 2.f, TEXT("S4 ручная лопата 33 → 40"), [this, Idle0]()
	{
		Expect(!Inv->GetAutoDigTimer().bActive && Inv->GetAutoDigTimer().Mode == 33,
			FString::Printf(TEXT("S4 ручная лопата сбросила цикл, режим 33 (%d)"), Inv->GetAutoDigTimer().Mode));
		Idle0();
	}));
	// S5 — 9001 (элитный магазин открыт) в повторе: нотис + сброс, 5028 нет.
	Steps.Add(WaitFor([this, C]() { return KbState() == 0 && C->GetEtalonShovelState() == 0; }, 2.f, TEXT("S5 покой"), [this]()
	{
		Feed(5029, B5029(11, Net->SelfUserNum, AutoShovelId));
	}));
	Steps.Add(WaitFor([this, C]() { return C->GetEtalonShovelState() == 43 && Inv->GetAutoDigTimer().bActive; }, 2.f, TEXT("S5 цикл идёт"), [this, Idle0, Shift13]()
	{
		Idle0();
		if (UAsdaPremiumShopSubsystem* Shop = GI->GetSubsystem<UAsdaPremiumShopSubsystem>()) { Shop->DebugSetOpenFlag(true); bShopFlagSet = true; }
		DigMark = Sent.Num(); Sink->Reset();
		Shift13();
	}));
	Steps.Add(WaitFor([this]() { return !Inv->GetAutoDigTimer().bActive; }, 2.f, TEXT("S5 срок вышел при открытом элитном магазине — цикл сброшен"), [this]()
	{
		Expect(Sink->Hits(TEXT("нотис 9001:")) == 1 && CountFrom(5028, DigMark) == 0,
			FString::Printf(TEXT("S5 нотис 9001 (%d), 5028 не ушёл (%d) — эталон :41034-41042"), Sink->Hits(TEXT("нотис 9001:")), CountFrom(5028, DigMark)));
		if (UAsdaPremiumShopSubsystem* Shop = GI->GetSubsystem<UAsdaPremiumShopSubsystem>()) { Shop->DebugSetOpenFlag(false); bShopFlagSet = false; }
	}));
	// S6 — [ПОРТ] 2040 (транспорт) в повторе: сброс; контроль `asda.Shovel.AutoLoopFix 0` — нотис каждый кадр, цикл висит.
	for (int32 Fix = 1; Fix >= 0; --Fix)
	{
		Steps.Add(WaitFor([this, C]() { return KbState() == 0 && C->GetEtalonShovelState() == 0; }, 2.f,
			FString::Printf(TEXT("S6 покой (ключ исправлений %d)"), Fix), [this, Fix]()
		{
			SetCv(TEXT("asda.Shovel.AutoLoopFix"), Fix);
			Feed(5029, B5029(11, Net->SelfUserNum, AutoShovelId));
		}));
		Steps.Add(WaitFor([this, C]() { return C->GetEtalonShovelState() == 43 && Inv->GetAutoDigTimer().bActive; }, 2.f,
			FString::Printf(TEXT("S6 цикл идёт (ключ %d)"), Fix), [this, Idle0, Shift13]()
		{
			Idle0();
			if (UAsdaEffectsSubsystem* Eff = GI->GetSubsystem<UAsdaEffectsSubsystem>()) { Eff->AddType2(AsdaSelf::GetUserIdxOr(GI, INDEX_NONE), 15, 600, 0); }
			DigMark = Sent.Num(); Sink->Reset();
			Shift13();
		}));
		Steps.Add(FStep([this, Fix](float T)
		{
			if (T < 0.6f) { return false; }
			const int32 H = Sink->Hits(TEXT("нотис 2040:"));   // сколько раз вывелся 2040
			if (Fix == 1)
			{
				Expect(!Inv->GetAutoDigTimer().bActive && H == 1 && CountFrom(5028, DigMark) == 0,
					FString::Printf(TEXT("S6 [ПОРТ] транспорт в повторе: нотис 2040 один (%d), цикл сброшен, 5028 нет (%d)"), H, CountFrom(5028, DigMark)));
			}
			else
			{
				Expect(Inv->GetAutoDigTimer().bActive && H >= 2 && CountFrom(5028, DigMark) == 0,
					FString::Printf(TEXT("S6 КОНТРОЛЬ ключ 0 (оригинал): цикл висит, нотис 2040 повторяется (%d раз за 0,6 с), 5028 нет"), H));
				Inv->AutoDigReset(TEXT("самотест S6"));
				SetCv(TEXT("asda.Shovel.AutoLoopFix"), 1);
			}
			if (UAsdaEffectsSubsystem* Eff = GI->GetSubsystem<UAsdaEffectsSubsystem>()) { Eff->ResetSessionState(EAsdaSessionResetReason::LeftWorld); }
			return true;
		}));
	}
	// S7 — [ПОРТ] 9006 (вес ≥ 90 %) на ответ «копать»: сброс; контроль ключом 0 — цикл висит (оригинал).
	for (int32 Fix = 1; Fix >= 0; --Fix)
	{
		Steps.Add(WaitFor([this, C]() { return KbState() == 0 && C->GetEtalonShovelState() == 0; }, 2.f,
			FString::Printf(TEXT("S7 покой (ключ исправлений %d)"), Fix), [this, Fix]()
		{
			SetCv(TEXT("asda.Shovel.AutoLoopFix"), Fix);
			Net->SelfWeightCur = 0;
			Feed(5029, B5029(11, Net->SelfUserNum, AutoShovelId));
		}));
		Steps.Add(WaitFor([this, C]() { return C->GetEtalonShovelState() == 43 && Inv->GetAutoDigTimer().bActive; }, 2.f,
			FString::Printf(TEXT("S7 цикл идёт (ключ %d)"), Fix), [this]()
		{
			Net->SelfWeightCur = 950; Sink->Reset();
			Feed(5029, B5029(11, Net->SelfUserNum, AutoShovelId));   // ответ на повтор при весе 95 %
		}));
		Steps.Add(FStep([this, Fix, Idle0](float T)
		{
			if (T < 0.4f) { return false; }
			const int32 H = Sink->Hits(TEXT("нотис 9006:"));   // нотис веса
			if (Fix == 1)
			{
				Expect(H == 1 && !Inv->GetAutoDigTimer().bActive, FString::Printf(TEXT("S7 [ПОРТ] 9006 (%d) сбросил цикл"), H));
			}
			else
			{
				Expect(H == 1 && Inv->GetAutoDigTimer().bActive,
					FString::Printf(TEXT("S7 КОНТРОЛЬ ключ 0 (оригинал): 9006 (%d), цикл НЕ сброшен — бесконечный повтор"), H));
				Inv->AutoDigReset(TEXT("самотест S7"));
				SetCv(TEXT("asda.Shovel.AutoLoopFix"), 1);
			}
			Net->SelfWeightCur = 0;
			Idle0();
			return true;
		}));
	}
	// S8 — [ПОРТ] в слоте инструмента чужой предмет (ручная лопата 11/33): повтора нет, цикл сброшен.
	Steps.Add(WaitFor([this, C]() { return KbState() == 0 && C->GetEtalonShovelState() == 0; }, 2.f, TEXT("S8 покой"), [this]()
	{
		Feed(5029, B5029(11, Net->SelfUserNum, AutoShovelId));
	}));
	Steps.Add(WaitFor([this, C]() { return C->GetEtalonShovelState() == 43 && Inv->GetAutoDigTimer().bActive; }, 2.f, TEXT("S8 цикл идёт"), [this, Idle0, Shift13]()
	{
		Idle0();
		Inv->DebugPutItem(2, DigCell, ShovelId, 1);   // инструмент заменили другим 11-м предметом
		DigMark = Sent.Num();
		Shift13();
	}));
	Steps.Add(WaitFor([this]() { return !Inv->GetAutoDigTimer().bActive; }, 2.f, TEXT("S8 [ПОРТ] чужой код в слоте — цикл сброшен"), [this]()
	{
		Expect(CountFrom(5028, DigMark) == 0, FString::Printf(TEXT("S8 чужой предмет не применён (5028 %d)"), CountFrom(5028, DigMark)));
		Inv->DebugPutItem(2, DigCell, AutoShovelId, 1);
	}));
	// S9 — [ПОРТ] отказ 5029 на повтор (код 9 «в городе»): сброс.
	Steps.Add(WaitFor([this, C]() { return KbState() == 0 && C->GetEtalonShovelState() == 0; }, 2.f, TEXT("S9 покой"), [this]()
	{
		Feed(5029, B5029(11, Net->SelfUserNum, AutoShovelId));
	}));
	Steps.Add(WaitFor([this, C]() { return C->GetEtalonShovelState() == 43 && Inv->GetAutoDigTimer().bActive; }, 2.f, TEXT("S9 цикл идёт"), [this]()
	{
		Feed(5029, B5029(9, Net->SelfUserNum, AutoShovelId));
	}));
	Steps.Add(WaitFor([this]() { return !Inv->GetAutoDigTimer().bActive; }, 1.f, TEXT("S9 [ПОРТ] отказ 5029 код 9 на повтор сбросил цикл"), [Idle0]() { Idle0(); }));
	// S10 — контроль ключа `asda.Shovel.AutoLoop 0`: копка 43 есть, таймера цикла нет (прежний порт).
	Steps.Add(WaitFor([this, C]() { return KbState() == 0 && C->GetEtalonShovelState() == 0; }, 2.f, TEXT("S10 покой"), [this]()
	{
		SetCv(TEXT("asda.Shovel.AutoLoop"), 0);
		Feed(5029, B5029(11, Net->SelfUserNum, AutoShovelId));
	}));
	Steps.Add(WaitFor([C]() { return C->GetEtalonShovelState() == 43; }, 2.f, TEXT("S10 КОНТРОЛЬ ключ 0: копка 43 ставится"), [this, Idle0]()
	{
		Expect(!Inv->GetAutoDigTimer().bActive, TEXT("S10 КОНТРОЛЬ asda.Shovel.AutoLoop 0: таймера цикла нет (проверка различает)"));
		SetCv(TEXT("asda.Shovel.AutoLoop"), 1);
		Idle0();
	}));
	// S11 — смена карты (сброс сессии MapChanged): «AUTOSABZIL : END : RESET».
	Steps.Add(WaitFor([this, C]() { return KbState() == 0 && C->GetEtalonShovelState() == 0; }, 2.f, TEXT("S11 покой"), [this]()
	{
		Feed(5029, B5029(11, Net->SelfUserNum, AutoShovelId));
	}));
	Steps.Add(WaitFor([this, C]() { return C->GetEtalonShovelState() == 43 && Inv->GetAutoDigTimer().bActive; }, 2.f, TEXT("S11 цикл идёт"), [this]()
	{
		Inv->ResetSessionState(EAsdaSessionResetReason::MapChanged);
		Expect(!Inv->GetAutoDigTimer().bActive, TEXT("S11 смена карты сбросила цикл (game_main.c:3361)"));
	}));
	// S12 — МЕЛК-1: разрыв связи при живом акторе снимает 43 и таймер конца клипа (5428 в новую сессию не уходит) и цикл.
	Steps.Add(Do([Idle0]() { Idle0(); }));
	Steps.Add(WaitFor([this, C]() { return KbState() == 0 && C->GetEtalonShovelState() == 0; }, 2.f, TEXT("S12 покой"), [this]()
	{
		Feed(5029, B5029(11, Net->SelfUserNum, AutoShovelId));
	}));
	Steps.Add(WaitFor([this, C]() { return C->GetEtalonShovelState() == 43 && Inv->GetAutoDigTimer().bActive; }, 2.f, TEXT("S12 копка 43, цикл идёт"), [this, C]()
	{
		DigMark = Sent.Num();
		Inv->ResetSessionState(EAsdaSessionResetReason::Disconnected);
		WS->ResetSessionState(EAsdaSessionResetReason::Disconnected);
		Expect(!Inv->GetAutoDigTimer().bActive && C->GetEtalonShovelState() == 0 && !C->bWeaponMotionHidden,
			FString::Printf(TEXT("S12 МЕЛК-1: сброс сессии снял цикл, состояние %d и «занят» (оружие за спиной %d)"),
				C->GetEtalonShovelState(), C->bWeaponMotionHidden ? 1 : 0));
	}));
	Steps.Add(FStep([this](float T)
	{
		if (T < 9.6f) { return false; }
		Expect(CountFrom(5428, DigMark) == 0 && CountFrom(5028, DigMark) == 0,
			FString::Printf(TEXT("S12 через %.1f с после сброса ни 5428 (%d), ни 5028 (%d)"), T, CountFrom(5428, DigMark), CountFrom(5028, DigMark)));
		return true;
	}));

	// ═══════════════ G. КОПАНИЕ: КЛИП, УДАРЫ, ИНСТРУМЕНТ, ЭФФЕКТЫ, ИТОГ 5430 (копание 10.10.2026) ═══════════════
	//   Путь игрока: соседи — штатный разбор 4025 110/114 и снапшот появления (4065@102 = 110/114, `DebugSpawnFakePeer` идёт
	//   тем же `SpawnRemotePlayer`); свой — код 11 пакета 5029; итог — 5430 штатным `HandlePacket`. Время настоящее.
	//   Эталон: `CCharacter_UpdateActionState` case 7/10 (удары по порогам 600/2066/3566/5100 мс, у 43 перемотка на 2066 после
	//   4-го удара), инструмент на `8_a1` ровно в 40/43, стартовый эффект (свой — a_01 / [ИСПР] b_01, сосед — a_01/b_01; яма 0x1F непрозрачная, видна до конца контейнера), итог —
	//   network.c:1530-1592 (нотис 9003/9004 только своему, эффект и звук итога всем). Контроль `asda.Shovel.ToolMeshFix 0`.
	{
		// Записывать удары персонажа Who до конца состояния (State → 0); по концу — Then().
		//   Who — АДРЕС поля (сосед раздела G появляется уже во время прогона: копия слабой ссылки при сборке сценария была бы пустой).
		auto TrackStrikes = [this](const TWeakObjectPtr<AAsdaPlayerCharacter>* Who, float Timeout, FString What, TFunction<void()> Then)
		{
			return FStep([this, Who, Timeout, What, Then](float T)
			{
				AAsdaPlayerCharacter* X = Who->Get();
				if (!X) { Expect(false, What + TEXT(" — актор пропал")); return true; }
				if (X->GetEtalonShovelState() == 0) { Expect(true, What); if (Then) { Then(); } return true; }
				if (T > Timeout) { Expect(false, What + FString::Printf(TEXT(" — не дождались за %.1f с"), Timeout)); return true; }
				return false;
			});
		};
		// Сверить моменты ударов с эталонными (с): 0 ≤ опоздание от порога ≤ длина кадра; `Tol` — прежний допуск, только в строку.
		//   Моменты — по часам персонажа (`GetShovelStrikeLog`: сумма его DeltaSeconds с входа, как растёт трек): кадр-рывок от
		//   снимка экрана сдвигает настенные часы, но не порядок «трек дошёл до порога → удар». Настенные — в журнал для сведения.
		auto CheckTimes = [this](const TCHAR* Tag, const TArray<float>& Want, float Tol)
		{
			const AAsdaPlayerCharacter* X = StrikeWho.Get();
			const TArray<float> Got = X ? X->GetShovelStrikeLog() : TArray<float>();
			bool bOkT = (Got.Num() == Want.Num()) && (StrikeAt.Num() == Want.Num());
			// ★ R5-fix6-c (13.11.2026): удар случается на ПЕРВОМ кадре, где трек дошёл до порога (`TickShovel`), поэтому честная граница —
			//   «не раньше порога и не позже одного кадра после него»: 0 ≤ опоздание ≤ длина кадра (+5 мс на округление порогов
			//   до мс и сумму float). Прежний неподвижный допуск ±0,06/±0,08 с один кадр длиннее допуска делал красным (G4/G8
			//   в `rx_r5fix2`), а при обычном кадре был в 3–4 раза шире нужного. У состояния 43 после 4-го удара трек
			//   перематывается на порог [1] (как у эталона), поэтому удары 5–7 отсчитываются от момента 4-го удара.
			constexpr float kStrikeEps = 0.005f;
			FString L, Lw, Ld;
			for (int32 i = 0; i < Got.Num(); ++i)
			{
				L += FString::Printf(TEXT("%s%.3f"), i ? TEXT(" / ") : TEXT(""), Got[i]);
				if (Want.IsValidIndex(i))
				{
					const float Ref = (Want.Num() == 7 && i >= 4 && Got.IsValidIndex(3)) ? Got[3] + (Want[i] - Want[3]) : Want[i];   // порог удара, с
					const float FrameS = StrikeDt.IsValidIndex(i) ? StrikeDt[i] : 0.f;   // длина кадра удара, с
					const float Late = Got[i] - Ref;                                      // опоздание от порога, с
					if (Late < -kStrikeEps || Late > FrameS + kStrikeEps) { bOkT = false; }
					Ld += FString::Printf(TEXT("%s%+.3f/%.3f"), i ? TEXT(" ") : TEXT(""), Late, FrameS);
				}
			}
			for (int32 i = 0; i < StrikeAt.Num(); ++i) { Lw += FString::Printf(TEXT("%s%.3f"), i ? TEXT(" / ") : TEXT(""), StrikeAt[i]); }
			FString W;
			for (int32 i = 0; i < Want.Num(); ++i) { W += FString::Printf(TEXT("%s%.3f"), i ? TEXT(" / ") : TEXT(""), Want[i]); }
			Expect(bOkT, FString::Printf(TEXT("%s ударов %d (ждём %d), моменты по часам персонажа %s с (эталон %s; опоздание от порога/кадр, с: %s — ждём 0 ≤ опоздание ≤ кадр; прежний допуск ± %.2f); по настенным %s"),
				Tag, Got.Num(), Want.Num(), *L, *W, *Ld, Tol, *Lw));
			Expect(bHitFxOk, FString::Printf(TEXT("%s после каждого удара жив эффект удара c-shovel-a_00 (прежний погашен)"), Tag));
		};
		// Имя меша инструмента и его видимость.
		auto ToolStr = [](const AAsdaPlayerCharacter* X)
		{
			const UStaticMeshComponent* Tc = X ? X->GetShovelToolComp() : nullptr;
			return FString::Printf(TEXT("%s, %s"), (Tc && Tc->GetStaticMesh()) ? *Tc->GetStaticMesh()->GetName() : TEXT("нет"),
				(Tc && Tc->IsVisible()) ? TEXT("виден") : TEXT("скрыт"));
		};
		auto ToolIs = [](const AAsdaPlayerCharacter* X, const TCHAR* Mesh, bool bVisible)
		{
			const UStaticMeshComponent* Tc = X ? X->GetShovelToolComp() : nullptr;
			const bool bVis = Tc && Tc->IsVisible();
			if (!bVisible) { return !bVis; }
			return bVis && Tc->GetStaticMesh() && Tc->GetStaticMesh()->GetName().Equals(Mesh, ESearchCase::IgnoreCase)
				&& Tc->GetAttachSocketName() == FName(TEXT("8_a1"));
		};
		auto StartFxIs = [](const AAsdaPlayerCharacter* X, const TCHAR* Jeb, bool bFollow)
		{
			const AAsdaEffectActor* E = X ? Cast<AAsdaEffectActor>(X->GetShovelStartFx()) : nullptr;
			return E && E->GetClass()->GetName().Contains(Jeb) && E->bFollowPosition == bFollow;
		};
		auto ResetStrikes = [this](AAsdaPlayerCharacter* X)
		{
			StrikeAt.Reset(); StrikeDt.Reset(); bHitFxOk = true; TrackAfter4 = -1.f; ShotPlan.Reset();
			HoleLastT = -1.0; bHoleSeen = false; bHoleOpaque = true;   // ★ яма копания: наблюдатель — заново
			StrikeSeen = X ? X->GetShovelStrikeTotal() : 0;
			GT0 = World->GetTimeSeconds();
			StrikeWho = X;
		};
		const TArray<float> Want40 = { 0.6f, 2.066f, 3.566f, 5.1f };
		const TArray<float> Want43 = { 0.6f, 2.066f, 3.566f, 5.1f, 5.1f, 6.6f, 8.134f };

		// G1 — сосед, 4025 110 → 40: клип anim_233, лопата в руке на 8_a1, стартовый a_01 без привязки, 4 удара.
		//   Соседей разделов B/F снял сброс сессии S12 — сосед раздела G ставится заново тем же путём снапшота.
		Steps.Add(Do([this, Idle0]()
		{
			Idle0(); Inv->AutoDigReset(TEXT("самотест G0")); Inv->AutoDig = FAsdaAutoDigTimer();
			SetCv(TEXT("asda.Shovel.ClipEnd"), 1); SetCv(TEXT("asda.Shovel.Clip"), 1); SetCv(TEXT("asda.Shovel.ToolMeshFix"), 1);
			WS->DebugSpawnFakePeer(kPeer5Num, TEXT("Копщик"), Me->GetGamePosXZ() + FVector2D(300.f, -300.f));
		}));
		Steps.Add(WaitFor([this]() { GPeer = Cast<AAsdaPlayerCharacter>(WS->ActorByUserIdx(kPeer5Num)); return GPeer.IsValid() && GPeer->GetEtalonShovelState() == 0; }, 5.f,
			TEXT("G0 сосед раздела G появился, в покое"), [this]()
		{
			Feed(4025, B4025(kPeer5Num, 110, 0.f, 1.f));
		}));
		Steps.Add(WaitFor([this]() { return GPeer->GetEtalonShovelState() == 40; }, 1.f, TEXT("G1 4025 110 → 40"), [this, ResetStrikes, ToolIs, ToolStr, StartFxIs]()
		{
			ResetStrikes(GPeer.Get());
			Expect(GPeer->GetPlayingClipName().EndsWith(TEXT("anim_233")) && ClipPlaying(GPeer->BaseSkel),
				FString::Printf(TEXT("G1 играет клип копания anim_233 (ch4_98_Shoveling, трек 17): '%s'"), *GPeer->GetPlayingClipName()));
			Expect(GPeer->GetShovelThresholdMs(0) == 600 && GPeer->GetShovelThresholdMs(1) == 2066 && GPeer->GetShovelThresholdMs(2) == 3566
				&& GPeer->GetShovelThresholdMs(3) == 5100,
				FString::Printf(TEXT("G1 пороги ударов из combo time: %d/%d/%d/%d мс (эталон 600/2066/3566/5100)"),
					GPeer->GetShovelThresholdMs(0), GPeer->GetShovelThresholdMs(1), GPeer->GetShovelThresholdMs(2), GPeer->GetShovelThresholdMs(3)));
			Expect(ToolIs(GPeer.Get(), TEXT("ch4_98_shoveling"), true),
				FString::Printf(TEXT("G1 в руке лопата ch4_98_Shoveling на кости 8_a1: %s"), *ToolStr(GPeer.Get())));
			Expect(StartFxIs(GPeer.Get(), TEXT("c_shovel_a_01"), false),
				TEXT("G1 стартовый эффект c-shovel-a_01 (4025 110), матрица без привязки — network.c:39960"));
			Expect(GPeer->bWeaponMotionHidden, TEXT("G1 оружие за спиной (bActionBusy)"));
		}));
		Steps.Add(TrackStrikes(&GPeer, 8.f, TEXT("G2 сосед: 40 снялось концом клипа"), [this, CheckTimes, ToolIs, Want40]()
		{
			CheckTimes(TEXT("G2 лопата 40:"), Want40, 0.06f);
			Expect(ToolIs(GPeer.Get(), TEXT(""), false), TEXT("G2 конец клипа: лопата убрана из руки (sub_93362B(…, 0))"));
		}));
		Steps.Add(WaitFor([this]() { return !GPeer->GetShovelStartFx() && GPeer->GetShovelStrokeCount() == 0; }, 1.f,
			TEXT("G2 вне 40/43 стартовый эффект погашен, счётчики ударов обнулены (entity_char.c:8766-8771)"), [this]()
		{
			Feed(4025, B4025(kPeer5Num, 114, 0.f, 1.f));
		}));
		// G3 — сосед, 4025 114 → 43: клип anim_234, авто-лопата (исправление), стартовый b_01, 7 ударов с перемоткой.
		Steps.Add(WaitFor([this]() { return GPeer->GetEtalonShovelState() == 43; }, 1.f, TEXT("G3 4025 114 → 43"), [this, ResetStrikes, ToolIs, ToolStr, StartFxIs]()
		{
			ResetStrikes(GPeer.Get());
			Expect(GPeer->GetPlayingClipName().EndsWith(TEXT("anim_234")), FString::Printf(TEXT("G3 играет клип anim_234 (…_01, трек 20): '%s'"), *GPeer->GetPlayingClipName()));
			Expect(ToolIs(GPeer.Get(), TEXT("ch4_98_autoshovel_01"), true) && GPeer->IsShovelToolAuto(),
				FString::Printf(TEXT("G3 [ИСПРАВЛЕНИЕ] в руке авто-лопата ch4_98_AutoShovel_01: %s"), *ToolStr(GPeer.Get())));
			Expect(StartFxIs(GPeer.Get(), TEXT("c_shovel_b_01"), false), TEXT("G3 стартовый эффект c-shovel-b_01 (4025 114) — network.c:39718"));
		}));
		Steps.Add(TrackStrikes(&GPeer, 11.f, TEXT("G4 сосед: 43 снялось концом клипа"), [this, CheckTimes, Want43]()
		{
			CheckTimes(TEXT("G4 авто-лопата 43:"), Want43, 0.08f);
			Expect(TrackAfter4 >= 2066.f && TrackAfter4 <= 2066.f + 150.f,
				FString::Printf(TEXT("G4 после 4-го удара трек перемотан на порог [1]: %.0f мс (эталон 2066)"), TrackAfter4));
			const double Dt = World->GetTimeSeconds() - GT0;
			Expect(FMath::Abs(Dt - 9.034) <= 0.2, FString::Printf(TEXT("G4 конец 43 через %.3f с (эталон 9.034 ± 0,2)"), Dt));
		}));
		// G5 — КОНТРОЛЬ `asda.Shovel.ToolMeshFix 0` (дефект оригинала): 110 сменил авто на ручную, 114 соседу авто не вернул.
		Steps.Add(WaitFor([this]() { return GPeer->GetEtalonShovelState() == 0; }, 1.f, TEXT("G5 покой"), [this]()
		{
			SetCv(TEXT("asda.Shovel.ToolMeshFix"), 0);
			Feed(4025, B4025(kPeer5Num, 110, 0.f, 1.f));
		}));
		Steps.Add(WaitFor([this]() { return GPeer->GetEtalonShovelState() == 40; }, 1.f, TEXT("G5 КОНТРОЛЬ: 110 → 40"), [this, ToolIs, ToolStr]()
		{
			Expect(ToolIs(GPeer.Get(), TEXT("ch4_98_shoveling"), true), FString::Printf(TEXT("G5 КОНТРОЛЬ: 110 сменил авто-меш на ручной (network.c:39966): %s"), *ToolStr(GPeer.Get())));
			Feed(4025, B4025(kPeer5Num, 0, 0.f, 0.f));
		}));
		Steps.Add(WaitFor([this]() { return GPeer->GetEtalonShovelState() == 0; }, 1.f, TEXT("G5 КОНТРОЛЬ: 4025 0"), [this]()
		{
			Feed(4025, B4025(kPeer5Num, 114, 0.f, 1.f));
		}));
		Steps.Add(WaitFor([this]() { return GPeer->GetEtalonShovelState() == 43; }, 1.f, TEXT("G5 КОНТРОЛЬ: 114 → 43"), [this, ToolIs, ToolStr]()
		{
			Expect(ToolIs(GPeer.Get(), TEXT("ch4_98_shoveling"), true) && !GPeer->IsShovelToolAuto(),
				FString::Printf(TEXT("G5 КОНТРОЛЬ ключ 0 (оригинал): у соседа на 114 РУЧНАЯ лопата — перевёрнутое условие network.c:39724 воспроизведено: %s"),
					*ToolStr(GPeer.Get())));
			SetCv(TEXT("asda.Shovel.ToolMeshFix"), 1);
			Feed(4025, B4025(kPeer5Num, 0, 0.f, 0.f));
		}));
		// G6 — 5430 соседу: эффект и звук итога у соседа, нотиса нет; неизвестный userNum — ничего.
		Steps.Add(WaitFor([this]() { return GPeer->GetEtalonShovelState() == 0; }, 1.f, TEXT("G6 покой"), [this]()
		{
			FxSuc0 = FxCount(TEXT("c_suc_a_00")); FxFail0 = FxCount(TEXT("c_fail_a_00"));
			Sink->Reset();
			Feed(5430, B5430(1, kPeer5Num));
			Feed(5430, B5430(0, kPeer5Num));
			Feed(5430, B5430(1, 31999));
		}));
		Steps.Add(WaitFor([this]() { return FxCount(TEXT("c_suc_a_00")) > FxSuc0 && FxCount(TEXT("c_fail_a_00")) > FxFail0; }, 1.f,
			TEXT("G6 5430 соседу: эффекты c-suc-a_00 и c-fail-a_00 у соседа"), [this]()
		{
			Expect(FxCount(TEXT("c_suc_a_00")) == FxSuc0 + 1 && FxCount(TEXT("c_fail_a_00")) == FxFail0 + 1,
				FString::Printf(TEXT("G6 ровно по одному эффекту итога (успех %d→%d, провал %d→%d): неизвестный userNum ничего не дал"),
					FxSuc0, FxCount(TEXT("c_suc_a_00")), FxFail0, FxCount(TEXT("c_fail_a_00"))));
			Expect(Sink->Hits(TEXT("[копание] нотис")) == 0, TEXT("G6 5430 соседа: нотиса 9003/9004 своему нет (эталон: только `pUpgradeTargetChar == свой`)"));
			Expect(Sink->Hits(TEXT("итог 5430: удачно у соседа")) == 1 && Sink->Hits(TEXT("итог 5430: не удалось у соседа")) == 1,
				TEXT("G6 итог разобран у соседа: «удачно» и «не удалось» по разу (звук S_e_shovelling02/03)"));
		}));
		// G7 — свой: код 11 лопаты → 40 (клип, лопата, стартовый a_01), конец → 5428, итог 5430 → нотис 9003 + эффект; 9004.
		//   Ключ `shot`: для кадров свой персонаж переносится на ближайшую сушу (у спота рыбалки он по пояс в воде) и без оружия
		//   (в руке у самотеста удочка — на кадре её не спутать с лопатой). Проверки от этого не зависят.
		if (bShot)
		{
			Steps.Add(Do([this]()
			{
				const UAsdaWalkGrid* Gd = AsdaGround::GetGrid();
				const FIntPoint T0 = MyTile();
				FIntPoint Best(-1, -1);
				for (int32 R = 2; R <= 40 && Best.X < 0; ++R)
				{
					for (int32 Dx = -R; Dx <= R && Best.X < 0; ++Dx)
					{
						for (int32 Dz = -R; Dz <= R; ++Dz)
						{
							if (FMath::Max(FMath::Abs(Dx), FMath::Abs(Dz)) != R) { continue; }
							const int32 X = T0.X + Dx, Z = T0.Y + Dz;
							if (Gd && Gd->IsTileWalkable(X, Z) && !Gd->IsTileOverWater(X, Z) && !Gd->IsTilePeaceZone(X, Z)) { Best = FIntPoint(X, Z); break; }
						}
					}
				}
				if (Best.X >= 0) { Me->NetSnap(AsdaCoord::TileCenterGame(Best)); }
				Me->SetWeaponFromItem(0, Items);   // без оружия
				UE_LOG(LogAsdaFishAnimTest, Log, TEXT("САМОТЕСТ-РЫБА-АНИМ кадры: свой перенесён на сушу %d,%d (было %d,%d), без оружия"), Best.X, Best.Y, T0.X, T0.Y);
			}));
		}
		Steps.Add(WaitFor([this, C]() { return KbState() == 0 && C->GetEtalonShovelState() == 0; }, 2.f, TEXT("G7 свой в покое"), [this]()
		{
			ShovelMark = Sent.Num();
			Feed(5029, B5029(11, Net->SelfUserNum, ShovelId));
		}));
		Steps.Add(WaitFor([C]() { return C->GetEtalonShovelState() == 40; }, 1.f, TEXT("G7 свой: код 11 → 40"), [this, C, ResetStrikes, ToolIs, ToolStr, StartFxIs]()
		{
			ResetStrikes(C);
			Expect(C->GetPlayingClipName().EndsWith(TEXT("anim_233")), FString::Printf(TEXT("G7 свой: клип anim_233 '%s'"), *C->GetPlayingClipName()));
			Expect(ToolIs(C, TEXT("ch4_98_shoveling"), true), FString::Printf(TEXT("G7 свой: лопата в руке: %s"), *ToolStr(C)));
			Expect(StartFxIs(C, TEXT("c_shovel_a_01"), false), TEXT("G7 свой: стартовый эффект c-shovel-a_01 (CCharacter_SetAction case 40)"));
			if (bShot)
			{
				ShotPlan.Add(TPair<float, FString>(0.15f, TEXT("asda_dig_40_start")));
				ShotPlan.Add(TPair<float, FString>(0.65f, TEXT("asda_dig_40_strike1")));
				ShotPlan.Add(TPair<float, FString>(2.9f, TEXT("asda_dig_40_mid")));
				ShotPlan.Add(TPair<float, FString>(4.3f, TEXT("asda_dig_40_hole_4s3")));   // ★ яма: перед концом жизни эмиттера (4.5 с)
				ShotPlan.Add(TPair<float, FString>(5.7f, TEXT("asda_dig_40_end_5s7")));    // ★ яма застыла видимой до конца контейнера (6.0 с)
			}
		}));
		Steps.Add(TrackStrikes(&Me, 8.f, TEXT("G8 свой: 40 снялось концом клипа"), [this, CheckTimes, Want40, C, ToolIs]()
		{
			CheckTimes(TEXT("G8 свой, лопата 40:"), Want40, 0.06f);
			// ★ яма копания (digging_hole.md): `c-shovel-a_01` / `badakmeshnoalpha` (0x1F) — непрозрачная; живёт 0.016 + 4.5 с и
			//   застывает видимой до конца контейнера 6.0 с (endMode 0, `asda.Fx.End0Freeze`) = конец клипа 40.
			Expect(bHoleSeen && bHoleOpaque && HoleLastT >= 5.8 && HoleLastT <= 6.2,
				FString::Printf(TEXT("G8 яма лопаты: видна до %.2f с (контейнер a_01 6.0 с = конец клипа), материал непрозрачный: %s"),
					HoleLastT, bHoleOpaque ? TEXT("да") : TEXT("НЕТ")));
			Expect(Sent5428From(ShovelMark) && CountFrom(5428, ShovelMark) == 1, TEXT("G8 конец клипа своего: 5428 ушёл ровно раз"));
			Expect(ToolIs(C, TEXT(""), false) && IdleClipIs(C), FString::Printf(TEXT("G8 лопата убрана, клип покоя '%s'"), *C->GetPlayingClipName()));
			FxSuc0 = FxCount(TEXT("c_suc_a_00")); FxFail0 = FxCount(TEXT("c_fail_a_00"));
			Sink->Reset();
			Feed(5430, B5430(1, Net->SelfUserNum));   // ответ сервера на 5428 — удачно
		}));
		Steps.Add(WaitFor([this]() { return FxCount(TEXT("c_suc_a_00")) > FxSuc0; }, 1.f, TEXT("G9 свой 5430 код 1: эффект c-suc-a_00"), [this, C]()
		{
			// ★ 05.11.2026: текст нотиса — из таблицы языка сессии (был зашит русский — в EN ложный FAIL при верной строке).
			Expect(Sink->Hits(*FString::Printf(TEXT("[копание] нотис 9003: %s ("), *AsdaNotice::Loc(GI, 9003))) == 1 && Sink->Hits(TEXT("(в системный чат: да)")) == 1,
				TEXT("G9 нотис 9003 «Раскопка проведена удачно» ушёл в системный чат (−11514, 10 с)"));
			Expect(!C->GetShovelHitFx(), TEXT("G9 эффект последнего удара погашен итогом (worldMatrix[1])"));
			if (bShot) { Shot(TEXT("asda_dig_result_success")); }
			Sink->Reset();
			Feed(5430, B5430(0, Net->SelfUserNum));
		}));
		Steps.Add(WaitFor([this]() { return FxCount(TEXT("c_fail_a_00")) > FxFail0; }, 1.f, TEXT("G10 свой 5430 код 0: эффект c-fail-a_00"), [this]()
		{
			Expect(Sink->Hits(*FString::Printf(TEXT("[копание] нотис 9004: %s ("), *AsdaNotice::Loc(GI, 9004))) == 1 && Sink->Hits(TEXT("(в системный чат: да)")) == 1,   // ★ 05.11.2026: текст из таблицы языка
				TEXT("G10 нотис 9004 «Раскопка не удалась» ушёл в системный чат"));
			ShovelMark = Sent.Num();
			Net->bActionPending = false;
			Feed(5029, B5029(11, Net->SelfUserNum, AutoShovelId));
		}));
		// G11 — свой авто-лопата: эталон ставит своему a_01 и для 43 (entity_char.c:17367) — копипаст имени; [ИСПРАВЛЕНИЕ]
		//   `asda.Shovel.StartFxFix 1` (по умолчанию) — b_01, как соседу и при появлении в 114. Авто-лопата в руке.
		Steps.Add(WaitFor([C]() { return C->GetEtalonShovelState() == 43; }, 1.f, TEXT("G11 свой: код 11 авто-лопаты → 43"), [this, C, ToolIs, ToolStr, StartFxIs, ResetStrikes]()
		{
			ResetStrikes(C);
			Expect(C->GetPlayingClipName().EndsWith(TEXT("anim_234")), FString::Printf(TEXT("G11 свой: клип anim_234 '%s'"), *C->GetPlayingClipName()));
			Expect(StartFxIs(C, TEXT("c_shovel_b_01"), false), TEXT("G11 свой 43: [ИСПРАВЛЕНИЕ] стартовый эффект b_01 (эталон a_01 — копипаст, ключ asda.Shovel.StartFxFix)"));
			Expect(ToolIs(C, TEXT("ch4_98_autoshovel_01"), true), FString::Printf(TEXT("G11 свой: авто-лопата в руке: %s"), *ToolStr(C)));
			if (bShot)
			{
				ShotPlan.Add(TPair<float, FString>(1.2f, TEXT("asda_dig_43_swing")));
				ShotPlan.Add(TPair<float, FString>(3.0f, TEXT("asda_dig_43_3s0")));
				ShotPlan.Add(TPair<float, FString>(5.15f, TEXT("asda_dig_43_strike4")));
				ShotPlan.Add(TPair<float, FString>(7.0f, TEXT("asda_dig_43_7s0")));      // ★ яма: с a_01 её уже не было бы (контейнер 6.0 с)
				ShotPlan.Add(TPair<float, FString>(8.3f, TEXT("asda_dig_43_8s3")));      // ★ яма b_01 ещё видна (жизнь 8.5 с)
				ShotPlan.Add(TPair<float, FString>(8.9f, TEXT("asda_dig_43_end_8s9")));  // ★ после конца контейнера b_01 (8.5 с), клип до 9.03 с
			}
		}));
		Steps.Add(TrackStrikes(&Me, 11.f, TEXT("G12 свой: 43 снялось концом клипа"), [this, CheckTimes, Want43]()
		{
			CheckTimes(TEXT("G12 свой, авто-лопата 43:"), Want43, 0.08f);
			Expect(Sent5428From(ShovelMark), TEXT("G12 конец клипа 43: 5428 ушёл"));
			// ★ яма копания: `c-shovel-b_01` / `badakmeshnoalpha` — 0.016 + 8.5 с, контейнер 8.5 с (эталон шапки/эмиттера).
			Expect(bHoleSeen && bHoleOpaque && HoleLastT >= 8.3 && HoleLastT <= 8.9,
				FString::Printf(TEXT("G12 яма авто-лопаты: видна до %.2f с (b_01: 8.5 с при клипе 9.03), материал непрозрачный: %s"),
					HoleLastT, bHoleOpaque ? TEXT("да") : TEXT("НЕТ")));
			Inv->AutoDigReset(TEXT("самотест G12")); Inv->AutoDig = FAsdaAutoDigTimer();   // цикл в этом разделе не нужен
		}));
		// G12К — КОНТРОЛЬ `asda.Shovel.StartFxFix 0` (как оригинал): своему 43 — a_01, яма гаснет с контейнером на 6.0 с — замер различает.
		Steps.Add(WaitFor([this, C]() { return KbState() == 0 && C->GetEtalonShovelState() == 0; }, 2.f, TEXT("G12К свой в покое"), [this]()
		{
			SetCv(TEXT("asda.Shovel.StartFxFix"), 0);
			ShovelMark = Sent.Num();
			Net->bActionPending = false;
			Feed(5029, B5029(11, Net->SelfUserNum, AutoShovelId));
		}));
		Steps.Add(WaitFor([C]() { return C->GetEtalonShovelState() == 43; }, 1.f, TEXT("G12К свой: авто-лопата → 43"), [this, C, StartFxIs, ResetStrikes]()
		{
			ResetStrikes(C);
			Expect(StartFxIs(C, TEXT("c_shovel_a_01"), false), TEXT("G12К КОНТРОЛЬ ключ 0: стартовый эффект a_01, как CCharacter_SetAction case 43"));
		}));
		Steps.Add(TrackStrikes(&Me, 11.f, TEXT("G12К свой: 43 снялось концом клипа"), [this]()
		{
			Expect(bHoleSeen && HoleLastT >= 5.8 && HoleLastT <= 6.2,
				FString::Printf(TEXT("G12К КОНТРОЛЬ ключ 0: яма a_01 видна до %.2f с (контейнер 6.0) при клипе 9.03 — замер различает"), HoleLastT));
			SetCv(TEXT("asda.Shovel.StartFxFix"), 1);
			Inv->AutoDigReset(TEXT("самотест G12К")); Inv->AutoDig = FAsdaAutoDigTimer();
		}));
		// G13 — сосед появился УЖЕ копающим (снапшот 4065@102 = 114 / 110): состояние, меш, стартовый эффект с привязкой 6.
		Steps.Add(Do([this]()
		{
			const FVector2D Near = Me->GetGamePosXZ();
			WS->DebugSpawnFakePeerState(kPeer3Num, TEXT("Копатель"), Near + FVector2D(-300.f, 300.f), 114);
			WS->DebugSpawnFakePeerState(kPeer4Num, TEXT("Землекоп"), Near + FVector2D(-300.f, -300.f), 110);
		}));
		Steps.Add(WaitFor([this]() { return Cast<AAsdaPlayerCharacter>(WS->ActorByUserIdx(kPeer3Num)) && Cast<AAsdaPlayerCharacter>(WS->ActorByUserIdx(kPeer4Num)); }, 5.f,
			TEXT("G13 соседи появились"), [this, ToolIs, ToolStr, StartFxIs]()
		{
			AAsdaPlayerCharacter* P3 = Cast<AAsdaPlayerCharacter>(WS->ActorByUserIdx(kPeer3Num));
			AAsdaPlayerCharacter* P4 = Cast<AAsdaPlayerCharacter>(WS->ActorByUserIdx(kPeer4Num));
			Expect(P3->GetEtalonShovelState() == 43 && ToolIs(P3, TEXT("ch4_98_autoshovel_01"), true) && StartFxIs(P3, TEXT("c_shovel_b_01"), true),
				FString::Printf(TEXT("G13 появление в 114: состояние %d, %s, стартовый c-shovel-b_01 с привязкой 6 (game_main.c:7308)"),
					P3->GetEtalonShovelState(), *ToolStr(P3)));
			Expect(P4->GetEtalonShovelState() == 40 && ToolIs(P4, TEXT("ch4_98_shoveling"), true) && StartFxIs(P4, TEXT("c_shovel_a_01"), true),
				FString::Printf(TEXT("G13 появление в 110: состояние %d, %s, стартовый c-shovel-a_01 с привязкой 6 (game_main.c:7429)"),
					P4->GetEtalonShovelState(), *ToolStr(P4)));
			Expect(P4->GetPlayingClipName().EndsWith(TEXT("anim_233")) && P3->GetPlayingClipName().EndsWith(TEXT("anim_234")),
				TEXT("G13 у появившихся играют клипы копания"));
		}));
		// G14 — КОНТРОЛЬ `asda.Shovel.Clip 0`: клипа нет (покой), удары по треку идут — проверка клипа различает.
		Steps.Add(WaitFor([this]() { return GPeer->GetEtalonShovelState() == 0; }, 1.f, TEXT("G14 покой соседа"), [this]()
		{
			SetCv(TEXT("asda.Shovel.Clip"), 0);
			Feed(4025, B4025(kPeer5Num, 110, 0.f, 1.f));
		}));
		Steps.Add(WaitFor([this]() { return GPeer->GetEtalonShovelState() == 40; }, 1.f, TEXT("G14 КОНТРОЛЬ: 110 → 40"), [this]()
		{
			Expect(!GPeer->GetPlayingClipName().EndsWith(TEXT("anim_233")) && IdleClipIs(GPeer.Get()),
				FString::Printf(TEXT("G14 КОНТРОЛЬ ключ 0: клип покоя '%s' вместо anim_233 — проверка различает"), *GPeer->GetPlayingClipName()));
			SetCv(TEXT("asda.Shovel.Clip"), 1);
			Feed(4025, B4025(kPeer5Num, 0, 0.f, 0.f));
		}));
	}
}

static FAutoConsoleCommandWithWorldAndArgs GAsdaFishAnimSelfTest(
	TEXT("asda.Fish.AnimSelfTest"),
	TEXT("asda.Fish.AnimSelfTest [delay=N] [quit] — ОФЛАЙН-самотест рыбалки В5: свой цикл 60→61→63/64→61→62→65→0 на реальных клипах ")
	TEXT("(мост В1 выкл.), удочка/рыба на костях, fish_end, соседи по 4021/4025/6173, скрытие оружия и щита, регресс-пары, контрольные пары"),
	FConsoleCommandWithWorldAndArgsDelegate::CreateLambda([](const TArray<FString>& A, UWorld* W)
	{
		// ★ 11.10.2026 (В3 S1): аргументы, офлайн-гейт со вторым барьером и итог отказа с `quit` — общий каркас.
		const FAsdaSelfTestArgs Args = FAsdaSelfTestArgs::Parse(A);
		const bool bQuit = Args.bQuit;               // выйти после прогона
		const bool bShotArg = Args.Has(TEXT("shot"));   // ★ копание: снимать кадры раздела G
		const float Delay = Args.Delay;              // задержка запуска, с (из -ExecCmds мир ещё грузится)
		if (AsdaSelfTestKit::RefuseStart(W, TEXT("asda.Fish.AnimSelfTest"), Args)) { return; }
		if (!W) { AsdaSelfTestKit::Refused(nullptr, TEXT("asda.Fish.AnimSelfTest"), bQuit, TEXT("нет мира")); return; }
		TWeakObjectPtr<UWorld> WW = W;
		auto Go = [WW, bQuit, bShotArg]()
		{
			UWorld* Wd = WW.Get();
			if (!Wd) { return; }
			FAsdaFishAnimSelfTest* T = new FAsdaFishAnimSelfTest();   // живёт до конца сценария (тикер удаляет)
			T->bQuit = bQuit;
			T->bShot = bShotArg;
			if (!T->Start(Wd))
			{
				UE_LOG(LogAsdaFishAnimTest, Log, TEXT("САМОТЕСТ-РЫБА-АНИМ ИТОГ: ok=%d fail=%d"), T->Ok, T->Fail);
				AsdaSelfTestKit::Quit(Wd, bQuit);
				delete T;
			}
		};
		// Свой персонаж офлайн: штатные крюки (как самотесты В1/В4) у первого спота Альпии (тайлы 139, 77).
		auto SpawnThenGo = [WW, Go, bQuit]()
		{
			UWorld* Wd = WW.Get();
			if (!Wd) { return; }
			if (AsdaSelfTestKit::RefuseDeferred(Wd, TEXT("asda.Fish.AnimSelfTest"), bQuit)) { return; }   // Р-2: офлайн повторно — на отложенном старте
			// ★ 12.10.2026 (ревизия В3 дедупа №2): спавн у воды — ВСЕГДА, а не только без павна. Прежде павн, оставленный
			//   предыдущим самотестом пакета в другом месте (например, 256 256), не переставлялся: A0 «зона рядом не найдена» и
			//   38 провалов (`wave3_S7.md` §4) — самотест зависел от места в пакете. Первым в пакете (павна нет) — как было.
			if (GEngine)
			{
				GEngine->Exec(Wd, TEXT("Asda.Load.CurrentMap 3"));
				GEngine->Exec(Wd, TEXT("Asda.World.FakeSelfSpawn 139 77"));
				FTimerHandle H;
				Wd->GetTimerManager().SetTimer(H, FTimerDelegate::CreateLambda(Go), 3.f, false);
				return;
			}
			Go();
		};
		if (Delay <= 0.f) { SpawnThenGo(); return; }
		FTimerHandle H0;
		W->GetTimerManager().SetTimer(H0, FTimerDelegate::CreateLambda(SpawnThenGo), Delay, false);
	}));

#endif // !UE_BUILD_SHIPPING
