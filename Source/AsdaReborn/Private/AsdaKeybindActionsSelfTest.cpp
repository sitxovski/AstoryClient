// Copyright (c) 2026 @sitxovski (Discord)
// ★★★ САМОТЕСТ КЛАВИШ «ПОДОБРАТЬ / АТАКОВАТЬ ЦЕЛЬ / БЛИЖАЙШИЙ ВРАГ» И ПРЫЖКА (25.10.2026, баг игроков). [ОРИГИНАЛ]/[ИСПРАВЛЕНИЕ]
//
// ЧТО ПРОВЕРЯЕТ — путём игрока: ввод контроллера `InputKey` → `PollKeys` (фронт отпускания) → `ExecuteCommand` → эталонные тела
//   (`AsdaTargetHotkeys`, `PlayEmotion` 2/1) → исходящие пакеты под перехватом; переназначение — окном настроек 69500
//   (`ShowOptions` → `BeginKeyCapture` → нажатие → «ОК»), «перезапуск» — новый объект раскладки читает профиль заново.
//   D. Раскладка по умолчанию: 56 = F, 59 = `, 60 = TAB, 95 (прыжок) = ПРОБЕЛ.
//   J. Пробел по умолчанию — прыжок; подбора (5012) нет, даже если предмет лежит у ног.
//   P. F — подбор: 5012 с клеткой предмета.
//   N. TAB — ближайший живой моб; повтор — второй ближайший (текущий пропускается), третье — снова первый; дальний (за порогом
//      2 300 000) не выбирается; мёртвый пропускается.
//   A. ` — атака выбранной цели: автоатака включена, ушёл подход 4006 или 4026.
//   R. Окно настроек: 60 → X, 59 → O, 56 → ПРОБЕЛ (прыжок теряет Пробел), 60 → G (G снимается с 42 «INTERFACE UPGRADE», как
//      `Keybind_RemoveConflicts`), снова 60 → X; во время захвата файл раскладки не пишется (эталон пишет только по «ОК»), клавиша
//      захвата игровую команду не исполняет; «ОК» — ini записан.
//   S. «Перезапуск»: новый объект раскладки (`LoadLayout`), второй метод — только файл (`LoadShortcutkeyIni`), и `ReloadLayout`
//      живого объекта — везде новые клавиши (и PrtSc у «SCREEN SHOT» не теряется через ActionMappings).
//   K. Новые клавиши работают (X — ближайший, O — атака, ПРОБЕЛ — подбор без прыжка), старые (TAB, `, F) — нет.
//   C. «Отмена» после захвата — назначение не меняется ни в памяти, ни в файле.
//   L. Старый файл игрока (подбор и прыжок оба на ПРОБЕЛЕ) — подбор уходит на F, прыжок остаётся на ПРОБЕЛЕ.
// Раскладка пишется ТОЛЬКО в песочницу `Saved/AsdaSelfTest/Shortcutkey_keybind_actions.ini` (`SetTestIniOverride`); прогон —
//   только с `-saveddirsuffix` (ActionMappings «ОК» пишет в Saved). Консоль: `asda.SelfTest.KeybindActions [delay=N] [quit]`.
//   Только офлайн. Строки — «САМОТЕСТ-КЛАВИШИ-ДЕЙСТВИЙ», итог — «… ИТОГ: ok=… fail=…».
#include "CoreMinimal.h"
#include "AsdaDevConsole.h"

#if ASDA_DEV_CONSOLE && !UE_BUILD_SHIPPING

#include "AsdaBytes.h"
#include "AsdaCVarSave.h"
#include "AsdaNetSubsystem.h"
#include "AsdaTestPackets.h"                 // запись 4013 (спаун моба)
#include "Test/AsdaSelfTestKit.h"
#include "World/AsdaKeybindConfig.h"
#include "World/AsdaPlayerCharacter.h"
#include "World/AsdaPlayerController.h"
#include "World/AsdaWorldSubsystem.h"
#include "World/AsdaCoord.h"
#include "World/AsdaLootActor.h"
#include "Npc/AsdaMobActor.h"
#include "Items/AsdaItemTableSubsystem.h"
#include "UI/AsdaSystemOptionSubsystem.h"
#include "UI/AsdaWindowSubsystem.h"
#include "UI/AsdaChatWindowSubsystem.h"
#include "Framework/Application/SlateApplication.h"
#include "UI/AsdaPartySubsystem.h"         // ★ 25.10.2026: клавиши отряда (48–50, 62–64)
#include "Login/AsdaLoginData.h"           // имена тела 5107 (cp1251)
#include "GameFramework/InputSettings.h"
#include "GameFramework/PlayerInput.h"     // ★ 26.10.2026: движковые DebugExecBindings — сверка с клавишами игры
#include "Engine/Engine.h"
#include "Engine/GameInstance.h"
#include "Engine/World.h"
#include "InputCoreTypes.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "Misc/CommandLine.h"
#include "HAL/PlatformFileManager.h"
#include "HAL/FileManager.h"
#include "Containers/Ticker.h"

DEFINE_LOG_CATEGORY_STATIC(LogAsdaKeyActTest, Log, All);

namespace AsdaKeyActTest
{
	static const TCHAR* const kCmd = TEXT("asda.SelfTest.KeybindActions");
	static const TCHAR* const kPrefix = TEXT("САМОТЕСТ-КЛАВИШИ-ДЕЙСТВИЙ");
	constexpr int32 kMobType = 1;      // тип моба (Mob list: волк alpen) — как у прочих самотестов боя
	constexpr int32 kSpdDenom = 250;   // знаменатель скорости 4013
	// Коды действий раскладки (эталон `UI_BuildKeybindLabels`).
	constexpr int32 kGetItem = 56, kAttack = 59, kNearEnemy = 60, kUpgrade = 42, kScreenShot = 44, kJump = 95;
	// VK клавиш теста.
	constexpr int32 kVkF = 70, kVkTilde = 192, kVkTab = 9, kVkSpace = 32, kVkX = 88, kVkO = 79, kVkG = 71, kVkPrtSc = 44;

	// Шаг прогона: ждать `Delay` с, затем `Fn` каждый тик, пока не вернёт true (не дольше `Timeout`).
	struct FStep
	{
		FString Name;            // имя шага
		double Delay = 0.0;      // пауза до первого вызова, с
		double Timeout = 5.0;    // потолок ожидания, с
		TFunction<bool()> Fn;    // тело; true — сделано
	};

	// Исходящий пакет под перехватом.
	struct FOut { uint16 Op = 0; TArray<uint8> Body; };

	// Состояние прогона.
	struct FState
	{
		TWeakObjectPtr<UGameInstance> GI;          // экземпляр игры
		bool bQuit = false;                        // `quit` в конце
		int32 Ok = 0, Fail = 0;                    // счёт
		uint32 CapGen = 0;                         // поколение перехвата
		TArray<FOut> Outs;                         // перехваченные отправки
		int32 Mark = 0;                            // начало замера в `Outs`
		bool bWasInGame = false;                   // `bInGameConnected` до прогона
		int32 SavedWeapon = 0;                     // `SelfWeaponItemId` до прогона
		TUniquePtr<FAsdaNetSelfScope> SelfScope;   // свой номер/смерть — вернуть
		TMap<int32, FAsdaKeyBind> SavedBinds;      // раскладка живого объекта до прогона
		EAsdaKeybindSource SavedSource = EAsdaKeybindSource::Defaults;
		FString SavedSourcePath;
		TArray<FInputActionKeyMapping> SavedMappings;   // ActionMappings движка (ASDA_*) до прогона
		FString IniPath;                           // песочница раскладки
		FIntPoint Home = FIntPoint(0, 0);          // своя клетка
		FIntPoint LootTile = FIntPoint(0, 0);      // клетка предмета
		int32 Weapon = 0;                          // предмет-оружие из таблицы (лут и оружие в руке)
		int32 RidA = -1, RidB = -1, RidFar = -1;   // мобы: ближний, второй, дальний
		bool bJumpSeen = false;                    // прыжок был в окне замера
		int32 PartyStop = 0;                       // аргумент `partystop=N` — раздел «Т» только до группы N (диагностика)
		bool bSkipParty = false;                   // аргумент `noparty` — без раздела «Т» (диагностика порядка пакета)
		bool bPeers = false;                       // ★ раздел «Т»: соседи и отряд заведены (убрать в итоге)
		TArray<FStep> Steps;                       // шаги
		int32 Next = 0;                            // текущий шаг
		double StepT = 0.0;                        // время входа в шаг

		UWorld* W() const { return GI.IsValid() ? GI->GetWorld() : nullptr; }
		AAsdaPlayerController* PC() const { UWorld* Wd = W(); return Wd ? Cast<AAsdaPlayerController>(Wd->GetFirstPlayerController()) : nullptr; }
		AAsdaPlayerCharacter* Me() const { APlayerController* P = PC(); return P ? Cast<AAsdaPlayerCharacter>(P->GetPawn()) : nullptr; }
		UAsdaNetSubsystem* Net() const { return GI.IsValid() ? GI->GetSubsystem<UAsdaNetSubsystem>() : nullptr; }
		UAsdaWorldSubsystem* WS() const { return GI.IsValid() ? GI->GetSubsystem<UAsdaWorldSubsystem>() : nullptr; }
		UAsdaSystemOptionSubsystem* Opt() const { return GI.IsValid() ? GI->GetSubsystem<UAsdaSystemOptionSubsystem>() : nullptr; }
		UAsdaKeybindConfig* Keys() const { AAsdaPlayerController* P = PC(); return P ? P->Keys.Get() : nullptr; }
		AAsdaMobActor* Mob(int32 R) const { const UAsdaWorldSubsystem* Ws = WS(); return (Ws && R >= 0) ? Ws->FindNetMobByRid(R) : nullptr; }

		void Expect(bool b, const FString& What)
		{
			if (b) { ++Ok; UE_LOG(LogAsdaKeyActTest, Display, TEXT("%s ok   %s"), kPrefix, *What); }
			else   { ++Fail; UE_LOG(LogAsdaKeyActTest, Error, TEXT("%s FAIL %s"), kPrefix, *What); }
		}
		void Add(const TCHAR* Name, double Delay, TFunction<bool()> Fn, double Timeout = 5.0)
		{
			FStep St; St.Name = Name; St.Delay = Delay; St.Timeout = Timeout; St.Fn = MoveTemp(Fn);
			Steps.Add(MoveTemp(St));
		}
		void Do(const TCHAR* Name, double Delay, TFunction<void()> Fn) { Add(Name, Delay, [F = MoveTemp(Fn)]() { F(); return true; }); }
		// Число перехваченных пакетов `Op` с начала замера.
		int32 Count(uint16 Op) const { int32 N = 0; for (int32 i = Mark; i < Outs.Num(); ++i) { N += (Outs[i].Op == Op) ? 1 : 0; } return N; }
		const FOut* First(uint16 Op) const { for (int32 i = Mark; i < Outs.Num(); ++i) { if (Outs[i].Op == Op) { return &Outs[i]; } } return nullptr; }
		// Бинд действия в живом объекте.
		FAsdaKeyBind Bind(int32 Cmd) const { FAsdaKeyBind B; if (UAsdaKeybindConfig* K = Keys()) { K->GetBind(Cmd, B); } return B; }
	};

	// Нажать/отпустить клавишу штатным вводом контроллера.
	static void KeyEv(const FState& S, const FKey& K, bool bDown)
	{
		if (APlayerController* P = S.PC()) { P->InputKey(FInputKeyEventArgs::CreateSimulated(K, bDown ? IE_Pressed : IE_Released, bDown ? 1.f : 0.f)); }
	}
	// Первое оружие (тип 9, не удочка) из таблицы предметов. 0 — нет.
	static int32 FindWeapon(const UAsdaItemTableSubsystem* IT)
	{
		if (!IT) { return 0; }
		for (int32 Code = 20000; Code < 80000; ++Code)
		{
			const FAsdaItemRecord* R = IT->GetItem(Code);
			if (R && R->ItemType == 9 && R->ItemVariant != 1 && R->AttackRangeTiles > 0) { return Code; }
		}
		return 0;
	}
	// Запись 5005 (15 б): @0 предмет, @4/@6 клетка, @8 кол-во, @12 rid дропнувшего (−1).
	static TArray<uint8> Body5005(int32 Item, const FIntPoint& Tile)
	{
		TArray<uint8> B; B.SetNumZeroed(15);
		AsdaBytes::WrI32(&B[0], Item);
		AsdaBytes::WrI16(&B[4], (int16)Tile.X); AsdaBytes::WrI16(&B[6], (int16)Tile.Y);
		AsdaBytes::WrI32(&B[8], 1);
		AsdaBytes::WrI16(&B[12], -1);
		return B;
	}
	// Тело 4017 (100 б): @0 тип, @2 rid, @4 флаги (0 — мёртв), @8 28×i16 −1, @92 HP, @96/@98 клетка.
	static TArray<uint8> Body4017Dead(int32 Rid, const FIntPoint& Tile)
	{
		TArray<uint8> B; B.SetNumZeroed(100);
		AsdaBytes::WrI16(&B[0], (int16)kMobType);
		AsdaBytes::WrI16(&B[2], (int16)Rid);
		AsdaBytes::WrI32(&B[4], 0);
		for (int32 i = 0; i < 28; ++i) { AsdaBytes::WrI16(&B[8 + 2 * i], -1); }
		AsdaBytes::WrI32(&B[92], 0);
		AsdaBytes::WrI16(&B[96], (int16)Tile.X); AsdaBytes::WrI16(&B[98], (int16)Tile.Y);
		return B;
	}
	// ActionMappings движка с именем ASDA_* (снимок/возврат).
	static TArray<FInputActionKeyMapping> AsdaMappings()
	{
		TArray<FInputActionKeyMapping> Out;
		if (const UInputSettings* IS = UInputSettings::GetInputSettings())
		{
			for (const FInputActionKeyMapping& M : IS->GetActionMappings()) { if (UAsdaKeybindConfig::CmdFromInputActionName(M.ActionName) > 0) { Out.Add(M); } }
		}
		return Out;
	}
	static void RestoreMappings(const TArray<FInputActionKeyMapping>& Saved)
	{
		UInputSettings* IS = UInputSettings::GetInputSettings();
		if (!IS) { return; }
		for (const FInputActionKeyMapping& M : AsdaMappings()) { IS->RemoveActionMapping(M, false); }
		for (const FInputActionKeyMapping& M : Saved) { IS->AddActionMapping(M, false); }
		IS->ForceRebuildKeymaps();
	}
	// Сброс боя (как у самотестов боя): автоатака, ожидания, маршрут; цель снимается.
	static void StopCombat(FState& S)
	{
		if (AAsdaPlayerController* P = S.PC())
		{
			P->bAutoAttack = false; P->bCombatStarted = false; P->bInCombatStance = false; P->bWaitingCombatAck = false;
			P->NextAttackTickTime = 0.0;
			P->GetWorldTimerManager().ClearTimer(P->DeferredAttackSendTimer);
			P->CancelRoute();
			P->ClearTarget();
		}
		if (UAsdaNetSubsystem* N = S.Net()) { N->ClearMoveRequestInFlight(TEXT("самотест клавиш действий: сброс боя")); N->bItemPickPending = false; }
		if (AAsdaPlayerCharacter* C = S.Me()) { C->StopAttackAnim(); C->AttackFaceTarget = nullptr; C->StopMove(); }
	}
	// Шаги «нажать клавишу K → отпустить» (фронт отпускания у `PollKeys` — в следующем кадре после отпускания).
	static void AddTap(TSharedRef<FState> S, const TCHAR* Name, FKey K)
	{
		S->Do(Name, 0.12, [S, K]() { S->Mark = S->Outs.Num(); S->bJumpSeen = false; KeyEv(*S, K, true); });
		S->Do(Name, 0.15, [S, K]()
		{
			if (AAsdaPlayerCharacter* C = S->Me()) { S->bJumpSeen |= C->IsJumpActive(); }
			KeyEv(*S, K, false);
		});
	}
	// Захват клавиши окном настроек: строка действия → нажатие → отпускание (захват — по нажатию, `PollHotkeys`).
	static void AddCapture(TSharedRef<FState> S, const TCHAR* Name, int32 Cmd, FKey K)
	{
		S->Do(Name, 0.12, [S, Cmd]() { if (UAsdaSystemOptionSubsystem* O = S->Opt()) { O->BeginKeyCapture(Cmd); } });
		S->Do(Name, 0.12, [S, K]() { S->Mark = S->Outs.Num(); S->bJumpSeen = false; KeyEv(*S, K, true); });
		S->Do(Name, 0.15, [S, K]() { if (AAsdaPlayerCharacter* C = S->Me()) { S->bJumpSeen |= C->IsJumpActive(); } KeyEv(*S, K, false); });
	}

	static void Finish(TSharedRef<FState> S)
	{
		UWorld* W = S->W();
		StopCombat(*S);
		if (UAsdaSystemOptionSubsystem* O = S->Opt()) { if (O->IsCapturingKey()) { O->CancelKeyCapture(); } if (O->IsOptionsShown()) { O->CloseOptions(false); } }
		if (S->bPeers)
		{
			// Раздел «Т»: состав отряда — пустой 5107, соседи — штатный уход из видимости 4011.
			if (UAsdaPartySubsystem* P = S->GI.IsValid() ? S->GI->GetSubsystem<UAsdaPartySubsystem>() : nullptr)
			{
				TArray<uint8> B; B.SetNumZeroed(144);
				for (int32 i = 0; i < 6; ++i) { AsdaBytes::WrI32(&B[4 * i], -1); }
				P->InjectPacket(5107, B);
				P->DebugSetPartyState(false, false, -1, 0);
			}
			if (UAsdaNetSubsystem* N = S->Net())
			{
				for (const int32 U : { 501, 502, 503 }) { TArray<uint8> B; B.SetNumZeroed(2); AsdaBytes::WrI16(B.GetData(), (int16)U); N->DebugFeedPacket(4011, B); }
			}
		}
		UAsdaKeybindConfig::SetTestIniOverride(FString());
		RestoreMappings(S->SavedMappings);
		if (UAsdaKeybindConfig* K = S->Keys()) { K->Binds = S->SavedBinds; K->Source = S->SavedSource; K->SourcePath = S->SavedSourcePath; }
		for (const int32 R : { S->RidA, S->RidB, S->RidFar }) { if (AAsdaMobActor* M = S->Mob(R)) { M->Destroy(); } }
		if (UAsdaNetSubsystem* N = S->Net())
		{
			if (S->CapGen) { AsdaSelfTestKit::DisarmCapture(N, S->CapGen); S->CapGen = 0; }
			N->bInGameConnected = S->bWasInGame;
			N->SelfWeaponItemId = S->SavedWeapon;
			N->bItemPickPending = false;
			N->ClearMoveRequestInFlight(TEXT("итог самотеста клавиш действий"));
		}
		S->SelfScope.Reset();
		IFileManager::Get().Delete(*S->IniPath, false, true, true);
		// Состояние ввода после уборки (следующему самотесту пакета достаётся ровно оно).
		{
			APlayerController* P = S->PC();
			const UAsdaWindowSubsystem* Wm = S->GI.IsValid() ? S->GI->GetSubsystem<UAsdaWindowSubsystem>() : nullptr;
			const UAsdaSystemOptionSubsystem* O = S->Opt();
			UE_LOG(LogAsdaKeyActTest, Display, TEXT("%s уборка: модалка %d, захват %d, Shift %d, Ctrl %d, Alt %d; Enter → cmd %d, Enter съеден захватом %d, свой павн %d"), kPrefix,
				Wm && Wm->IsAnyModalOpen() ? 1 : 0, O && O->IsGameInputCaptured() ? 1 : 0,
				P && P->IsInputKeyDown(EKeys::LeftShift) ? 1 : 0, P && P->IsInputKeyDown(EKeys::LeftControl) ? 1 : 0, P && P->IsInputKeyDown(EKeys::LeftAlt) ? 1 : 0,
				S->Keys() ? S->Keys()->LookupCmd(13, EAsdaKeyMod::None) : -1, O && O->IsVkHeldFromCapture(13) ? 1 : 0, (P && P->GetPawn()) ? 1 : 0);
		}
		AsdaSelfTestKit::Finish(W, kCmd, S->bQuit, LogAsdaKeyActTest, kPrefix, S->Ok, S->Fail,
			*FString::Printf(TEXT(" (исходящих: %d)"), S->Outs.Num()));
	}

	// ══════ ★ 25.10.2026 (решение владельца): клавиши отряда/цели 48–50, 61–64 ══════
	//   Т0 дефолты порта (48–50, 61, 63, 64 — «не назначено», 62 — SHIFT+TAB); Т1 Shift+Tab по умолчанию: соотрядник A → B → я → A;
	//   Т2 окно: 48 → ], 49 → [, 50 → ,, 61 → ., 63 → /, 64 → L, 62 → SHIFT+O, «ОК»; Т3 L — пометка общей цели по кругу A → B → A;
	//   Т4 / — цель помеченного (его цель атаки, затем цель хода), без цели — цель не меняется; Т5 SHIFT+O — Shift+Tab-круг,
	//   прежний SHIFT+TAB — ничего; Т6 ] — окно приглашения 2250 для чужого игрока; Т7 . — 5478 по цели;
	//   ★ 26.10.2026 (ревизия wave_1025d п. 6): 48 было на `;` — это движковый DebugExecBinding `ToggleDebugCamera` (BaseInput.ini):
	//   локальным игроком становился DebugCameraController, и до конца игры AsdaPlayerController клавиш не получал (EnterConfirm
	//   после пакета падал 42/2). Теперь `]`; привязки движка, совпадающие с клавишами игры, сняты в Config/DefaultInput.ini.
	//   Т8 , — без выбранной строки молча, строка B — 5100 c его charId, не глава — 5100 нет (1089); Т9 [ — 5098 режим 0.
	constexpr int32 kPeerA = 501, kPeerB = 502, kPeerC = 503;   // userNum соседей
	constexpr int32 kSelfCid = 990400;                         // свой charId, если сессии нет
	static int32 PeerCid(int32 U) { return 990000 + U; }
	// Тело 4005 (151 б): сосед `U` (charId 990000+U, имя), игровая позиция.
	static TArray<uint8> Body4005Peer(int32 U, const char* Name, const FVector2D& G)
	{
		TArray<uint8> B; B.SetNumZeroed(151);
		AsdaBytes::WrI16(B.GetData() + 0, (int16)U);
		B[6] = 1;
		AsdaBytes::WrI32(B.GetData() + 11, PeerCid(U));
		FMemory::Memcpy(B.GetData() + 15, Name, FCStringAnsi::Strlen(Name));
		B[37] = 1; B[38] = 1; B[40] = 10;   // пол, класс, уровень
		const float X = (float)G.X, Z = (float)G.Y;
		FMemory::Memcpy(B.GetData() + 56, &X, 4);
		FMemory::Memcpy(B.GetData() + 60, &Z, 4);
		return B;
	}
	// Тело 5107 (144 б): шесть charId (−1 пусто) + шесть имён по 20 б. Слот 0 — глава.
	static TArray<uint8> Body5107(const TArray<TPair<int32, FString>>& Rows)
	{
		TArray<uint8> B; B.SetNumZeroed(144);
		for (int32 i = 0; i < 6; ++i) { AsdaBytes::WrI32(&B[4 * i], -1); }
		for (int32 i = 0; i < Rows.Num() && i < 6; ++i)
		{
			AsdaBytes::WrI32(&B[4 * i], Rows[i].Key);
			FAsdaLoginData::EncodeCp1251(Rows[i].Value, &B[24 + 20 * i], 19);
		}
		return B;
	}
	// Нажать сочетание (модификатор держится вокруг нажатия клавиши).
	static void AddTapMod(TSharedRef<FState> S, const TCHAR* Name, FKey Mod, FKey K)
	{
		S->Do(Name, 0.12, [S, Mod]() { KeyEv(*S, Mod, true); });
		AddTap(S, Name, K);
		S->Do(Name, 0.12, [S, Mod]() { KeyEv(*S, Mod, false); });
	}
	static UAsdaPartySubsystem* Party(const FState& S) { return S.GI.IsValid() ? S.GI->GetSubsystem<UAsdaPartySubsystem>() : nullptr; }
	static AAsdaPlayerCharacter* Peer(const FState& S, int32 U)
	{
		const UAsdaWorldSubsystem* Ws = S.WS();
		return Ws ? Cast<AAsdaPlayerCharacter>(Ws->GetPlayerActorByCharId(PeerCid(U))) : nullptr;
	}

	static void BuildParty(TSharedRef<FState> S)
	{
		S->Add(TEXT("Т подготовка"), 0.2, [S]()
		{
			AAsdaPlayerCharacter* Me = S->Me(); UAsdaNetSubsystem* N = S->Net(); UAsdaKeybindConfig* K = S->Keys();
			UAsdaPartySubsystem* P = Party(*S); UAsdaWorldSubsystem* Ws = S->WS();
			if (!Me || !N || !K || !P || !Ws) { return false; }
			StopCombat(*S);
			K->LoadDefaults();
			K->SaveToInputSettings(/*bSaveToDisk*/ false);
			if (N->SelectedCharId == 0) { N->SelectedCharId = kSelfCid; }   // вернёт FAsdaNetSelfScope
			const FVector2D G = Me->GetGamePosXZ();
			N->DebugFeedPacket(4005, Body4005Peer(kPeerA, "KbPeerA", G + FVector2D(200.f, 0.f)));
			N->DebugFeedPacket(4005, Body4005Peer(kPeerB, "KbPeerB", G + FVector2D(-200.f, 0.f)));
			N->DebugFeedPacket(4005, Body4005Peer(kPeerC, "KbPeerC", G + FVector2D(0.f, 300.f)));
			S->bPeers = true;
			P->DebugSetPartyState(true, true, 7, 0);
			P->DebugResetTargetCycles();   // ★ 26.10.2026: повторный прогон в той же игре — круг Shift+Tab и пометка общей цели с начала
			P->InjectPacket(5107, Body5107({ { N->SelectedCharId, Ws->GetSelfCharName() }, { PeerCid(kPeerA), TEXT("KbPeerA") },
				{ PeerCid(kPeerB), TEXT("KbPeerB") } }));
			return true;
		}, 10.0);
		S->Add(TEXT("Т состав"), 0.3, [S]()
		{
			UAsdaPartySubsystem* P = Party(*S);
			if (!P || !Peer(*S, kPeerA) || !Peer(*S, kPeerB) || !Peer(*S, kPeerC)) { return false; }
			int32 Placed = 0;
			for (const FAsdaPartyMember& M : P->GetMembers()) { Placed += (M.SlotIndex >= 0) ? 1 : 0; }
			if (Placed < 2) { return false; }
			S->Expect(P->IsInParty() && P->IsPartyLeader() && P->GetMembers().Num() == 3, TEXT("Т подготовка: отряд из трёх, я глава, соотрядники в строках панели"));
			return true;
		}, 10.0);
		S->Do(TEXT("Т0"), 0.1, [S]()
		{
			bool bNone = true;
			for (const int32 C : { 48, 49, 50, 61, 63, 64 }) { bNone &= !S->Bind(C).IsBound(); }
			S->Expect(bNone, TEXT("Т0 дефолты порта: 48–50, 61, 63, 64 — не назначены (keybindLoadDefaults)"));
			S->Expect(S->Bind(62).Vk == kVkTab && S->Bind(62).Mod == EAsdaKeyMod::Shift, TEXT("Т0 62 «ACTION SELETE MYSELF» = SHIFT+TAB"));
			if (AAsdaPlayerController* P = S->PC()) { P->ClearTarget(); }
		});
		if (S->PartyStop == 1) { return; }
		// Т1 Shift+Tab по умолчанию.
		AddTapMod(S, TEXT("Т1 shift+tab 1"), EKeys::LeftShift, EKeys::Tab);
		S->Do(TEXT("Т1a"), 0.1, [S]() { AAsdaPlayerController* P = S->PC(); S->Expect(P && P->TargetActor.Get() == Peer(*S, kPeerA), TEXT("Т1 Shift+Tab — соотрядник строки 0 (KbPeerA)")); });
		AddTapMod(S, TEXT("Т1 shift+tab 2"), EKeys::LeftShift, EKeys::Tab);
		S->Do(TEXT("Т1b"), 0.1, [S]() { AAsdaPlayerController* P = S->PC(); S->Expect(P && P->TargetActor.Get() == Peer(*S, kPeerB), TEXT("Т1 Shift+Tab ещё — строка 1 (KbPeerB)")); });
		AddTapMod(S, TEXT("Т1 shift+tab 3"), EKeys::LeftShift, EKeys::Tab);
		S->Do(TEXT("Т1c"), 0.1, [S]() { AAsdaPlayerController* P = S->PC(); S->Expect(P && P->TargetActor.Get() == S->Me(), TEXT("Т1 Shift+Tab ещё — строк больше нет: цель = я")); });
		AddTapMod(S, TEXT("Т1 shift+tab 4"), EKeys::LeftShift, EKeys::Tab);
		S->Do(TEXT("Т1d"), 0.1, [S]() { AAsdaPlayerController* P = S->PC(); S->Expect(P && P->TargetActor.Get() == Peer(*S, kPeerA), TEXT("Т1 круг заново — снова KbPeerA")); });
		if (S->PartyStop == 2) { return; }
		// Т2 переназначение окном.
		S->Do(TEXT("Т2 открыть"), 0.2, [S]() { if (UAsdaSystemOptionSubsystem* O = S->Opt()) { O->ShowOptions(); O->SelectTab(2); } });
		AddCapture(S, TEXT("Т2 48→]"), 48, EKeys::RightBracket);   // `;` — движковая ToggleDebugCamera (см. шапку раздела «Т»)
		AddCapture(S, TEXT("Т2 49→["), 49, EKeys::LeftBracket);   // ' в Development — клавиша GameplayDebugger
		AddCapture(S, TEXT("Т2 50→,"), 50, EKeys::Comma);
		AddCapture(S, TEXT("Т2 61→."), 61, EKeys::Period);
		AddCapture(S, TEXT("Т2 63→/"), 63, EKeys::Slash);
		AddCapture(S, TEXT("Т2 64→L"), 64, EKeys::L);
		S->Do(TEXT("Т2 shift"), 0.1, [S]() { KeyEv(*S, EKeys::LeftShift, true); });
		AddCapture(S, TEXT("Т2 62→shift+O"), 62, EKeys::O);
		S->Do(TEXT("Т2 shift up"), 0.12, [S]() { KeyEv(*S, EKeys::LeftShift, false); });
		S->Do(TEXT("Т2 ОК"), 0.2, [S]() { if (UAsdaSystemOptionSubsystem* O = S->Opt()) { O->CloseOptions(true); } });
		S->Do(TEXT("Т2"), 0.2, [S]()
		{
			FString Txt; FFileHelper::LoadFileToString(Txt, *S->IniPath);
			const bool bMem = S->Bind(48).Vk == 221 && S->Bind(49).Vk == 219 && S->Bind(50).Vk == 188 && S->Bind(61).Vk == 190
				&& S->Bind(63).Vk == 191 && S->Bind(64).Vk == 76 && S->Bind(62).Vk == kVkO && S->Bind(62).Mod == EAsdaKeyMod::Shift;
			S->Expect(bMem, TEXT("Т2 окно: 48 ] · 49 [ · 50 , · 61 . · 63 / · 64 L · 62 SHIFT+O"));
			S->Expect(Txt.Contains(TEXT("PARTY INVITE:\t]")) && Txt.Contains(TEXT("ACTION TARGET SHARE NEXT:\tL"))
				&& Txt.Contains(TEXT("ACTION SELETE MYSELF:\tSHIFT+O")), TEXT("Т2 «ОК» — ini записан"));
			UAsdaKeybindConfig* Fresh = NewObject<UAsdaKeybindConfig>(S->PC());
			Fresh->LoadLayout(S->Keys()->Profile);
			FAsdaKeyBind B63, B62; Fresh->GetBind(63, B63); Fresh->GetBind(62, B62);
			S->Expect(B63.Vk == 191 && B62.Vk == kVkO && B62.Mod == EAsdaKeyMod::Shift, TEXT("Т2 «перезапуск» (новый объект раскладки) — те же клавиши"));
			// ★ 26.10.2026: после захватов локальный игрок — по-прежнему наш контроллер (не DebugCameraController движка).
			UWorld* Wd = S->W();
			APlayerController* First = Wd ? Wd->GetFirstPlayerController() : nullptr;
			S->Expect(First && First->IsA<AAsdaPlayerController>(), FString::Printf(TEXT("Т2 локальный игрок — AsdaPlayerController (сейчас %s)"),
				First ? *First->GetClass()->GetName() : TEXT("нет")));
		});
		if (S->PartyStop == 3) { return; }
		// Т3 L — пометка общей цели по кругу.
		AddTap(S, TEXT("Т3 L 1"), EKeys::L);
		S->Do(TEXT("Т3a"), 0.1, [S]() { UAsdaPartySubsystem* P = Party(*S); S->Expect(P && P->GetSharedTargetSlot() == 0, FString::Printf(TEXT("Т3 L — пометка общей цели: строка 0 (сейчас %d)"), P ? P->GetSharedTargetSlot() : -9)); });
		AddTap(S, TEXT("Т3 L 2"), EKeys::L);
		S->Do(TEXT("Т3b"), 0.1, [S]() { UAsdaPartySubsystem* P = Party(*S); S->Expect(P && P->GetSharedTargetSlot() == 1, TEXT("Т3 L ещё — строка 1")); });
		AddTap(S, TEXT("Т3 L 3"), EKeys::L);
		S->Do(TEXT("Т3c"), 0.1, [S]()
		{
			UAsdaPartySubsystem* P = Party(*S);
			S->Expect(P && P->GetSharedTargetSlot() == 0, TEXT("Т3 L ещё — круг по count − 1 = 2 строкам: снова 0"));
			// Т4 подготовка: у помеченного (A) цель атаки — моб B.
			if (AAsdaPlayerCharacter* A = Peer(*S, kPeerA)) { A->AttackFaceTarget = S->Mob(S->RidB); A->MoveTargetRenderId = -1; }
			if (AAsdaPlayerController* Pc = S->PC()) { Pc->ClearTarget(); }
		});
		AddTap(S, TEXT("Т4 / 1"), EKeys::Slash);
		S->Do(TEXT("Т4a"), 0.1, [S]()
		{
			AAsdaPlayerController* P = S->PC();
			S->Expect(P && S->Mob(S->RidB) && P->TargetActor.Get() == S->Mob(S->RidB), TEXT("Т4 / — цель помеченного соотрядника (его цель атаки, моб) стала моей"));
			if (AAsdaPlayerCharacter* A = Peer(*S, kPeerA)) { A->AttackFaceTarget = nullptr; A->MoveTargetRenderId = S->RidB; }
			if (P) { P->ClearTarget(); }
		});
		AddTap(S, TEXT("Т4 / 2"), EKeys::Slash);
		S->Do(TEXT("Т4b"), 0.1, [S]()
		{
			AAsdaPlayerController* P = S->PC();
			S->Expect(P && P->TargetActor.Get() == S->Mob(S->RidB), TEXT("Т4 / — нет цели атаки: берётся цель его хода (HIWORD)"));
			if (AAsdaPlayerCharacter* A = Peer(*S, kPeerA)) { A->AttackFaceTarget = nullptr; A->MoveTargetRenderId = -1; }
			if (P) { P->ClearTarget(); }
		});
		AddTap(S, TEXT("Т4 / 3"), EKeys::Slash);
		S->Do(TEXT("Т4c"), 0.1, [S]() { AAsdaPlayerController* P = S->PC(); S->Expect(P && !P->TargetActor.IsValid(), TEXT("Т4 / — у соотрядника нет цели: 20300, моя цель не меняется")); });
		if (S->PartyStop == 4) { return; }
		// Т5 SHIFT+O — Shift+Tab-круг; прежний SHIFT+TAB больше ничего не делает.
		AddTapMod(S, TEXT("Т5 shift+tab"), EKeys::LeftShift, EKeys::Tab);
		S->Do(TEXT("Т5a"), 0.1, [S]() { AAsdaPlayerController* P = S->PC(); S->Expect(P && !P->TargetActor.IsValid(), TEXT("Т5 прежний SHIFT+TAB после переназначения — ничего")); });
		AddTapMod(S, TEXT("Т5 shift+O"), EKeys::LeftShift, EKeys::O);
		S->Do(TEXT("Т5b"), 0.1, [S]()
		{
			AAsdaPlayerController* P = S->PC();
			S->Expect(P && P->TargetActor.IsValid() && P->TargetActor.Get()->IsA<AAsdaPlayerCharacter>(), TEXT("Т5 SHIFT+O (переназначен) — круг Shift+Tab: цель — персонаж"));
			if (P) { P->SetTarget(Peer(*S, kPeerC)); }   // Т6: цель — чужой игрок вне отряда
		});
		AddTap(S, TEXT("Т6 ]"), EKeys::RightBracket);
		S->Do(TEXT("Т6"), 0.2, [S]()
		{
			UAsdaPartySubsystem* P = Party(*S);
			S->Expect(P && P->IsInviteWindowOpen(), TEXT("Т6 ] — окно приглашения в отряд 2250 для выбранного игрока (вне отряда)"));
			if (P) { P->OnCreateCancel(); }   // кнопка 2253 «Отмена» (флаг заявки снят)
			if (AAsdaPlayerController* Pc = S->PC()) { Pc->SetTarget(Peer(*S, kPeerA)); }
		});
		if (S->PartyStop == 5) { return; }
		AddTap(S, TEXT("Т7 ."), EKeys::Period);
		S->Do(TEXT("Т7"), 0.2, [S]()
		{
			const FOut* O = S->First(5478);
			const bool bBody = O && O->Body.Num() >= 6 && AsdaBytes::RdI16(&O->Body[0]) == kPeerA && AsdaBytes::RdI32(&O->Body[2]) == PeerCid(kPeerA);
			S->Expect(bBody, FString::Printf(TEXT("Т7 . — запрос сведений 5478 по цели (userNum %d, charId %d)"), kPeerA, PeerCid(kPeerA)));
			if (UAsdaPartySubsystem* P = Party(*S)) { P->SelectMemberSlot(-1); }
		});
		AddTap(S, TEXT("Т8 , без строки"), EKeys::Comma);
		S->Do(TEXT("Т8a"), 0.1, [S]()
		{
			S->Expect(S->Count(5100) == 0, TEXT("Т8 , без выбранной строки панели — молча (эталон, в отличие от кнопки 2262)"));
			if (UAsdaPartySubsystem* P = Party(*S)) { P->SelectMemberSlot(1); }
		});
		AddTap(S, TEXT("Т8 , строка B"), EKeys::Comma);
		S->Do(TEXT("Т8b"), 0.1, [S]()
		{
			const FOut* O = S->First(5100);
			S->Expect(O && O->Body.Num() >= 4 && AsdaBytes::RdI32(&O->Body[0]) == PeerCid(kPeerB), TEXT("Т8 , строка 1 выбрана — 5100 с charId KbPeerB"));
			if (UAsdaPartySubsystem* P = Party(*S)) { P->DebugSetPartyState(true, false, 7, 0); }
		});
		AddTap(S, TEXT("Т8 , не глава"), EKeys::Comma);
		S->Do(TEXT("Т8c"), 0.1, [S]()
		{
			S->Expect(S->Count(5100) == 0, TEXT("Т8 , не глава — 5100 нет (нотис 1089)"));
			if (UAsdaPartySubsystem* P = Party(*S)) { P->DebugSetPartyState(true, true, 7, 0); }
		});
		AddTap(S, TEXT("Т9 ["), EKeys::LeftBracket);
		S->Do(TEXT("Т9"), 0.1, [S]()
		{
			const FOut* O = S->First(5098);
			S->Expect(O && O->Body.Num() >= 3 && O->Body[2] == 0, TEXT("Т9 [ — выход из отряда 5098 режим 0 (и главе — без окна роспуска)"));
		});
		S->Do(TEXT("Т итог"), 0.1, [S]()
		{
			UAsdaPartySubsystem* P = Party(*S);
			S->Expect(P && S->Count(5098) <= 1 && !(P->IsDisbandOpen()), TEXT("Т окно роспуска не поднималось"));
		});
		// Т10 ★ 26.10.2026: пометка общей цели снимается при смене карты (эталон `CParty_BuildUI@0xB95FF0`: g_nPartySelIdx = −1).
		//   Сброс сессии вызывается у одной подсистемы отряда (смена карты целиком снесла бы окна и соседей прогона).
		S->Do(TEXT("Т10 подготовка"), 0.1, [S]()
		{
			if (UAsdaPartySubsystem* P = Party(*S)) { P->DebugSetPartyState(true, true, 7, 0); P->DebugResetTargetCycles(); }
		});
		AddTap(S, TEXT("Т10 L"), EKeys::L);
		S->Do(TEXT("Т10"), 0.1, [S]()
		{
			UAsdaPartySubsystem* P = Party(*S);
			const int32 Before = P ? P->GetSharedTargetSlot() : -9;
			if (P) { static_cast<IAsdaSessionResettable*>(P)->ResetSessionState(EAsdaSessionResetReason::MapChanged); }
			const int32 After = P ? P->GetSharedTargetSlot() : -9;
			S->Expect(Before == 0 && After == -1 && P && P->IsInParty(),
				FString::Printf(TEXT("Т10 смена карты: пометка общей цели снята (было %d, стало %d), отряд на месте %d"), Before, After, P && P->IsInParty() ? 1 : 0));
			AsdaCVarSave::SetForTest(TEXT("asda.Party.ShareMarkResetOnMap"), 0);
		});
		AddTap(S, TEXT("Т10к L"), EKeys::L);
		S->Do(TEXT("Т10к"), 0.1, [S]()
		{
			UAsdaPartySubsystem* P = Party(*S);
			const int32 Before = P ? P->GetSharedTargetSlot() : -9;
			if (P) { static_cast<IAsdaSessionResettable*>(P)->ResetSessionState(EAsdaSessionResetReason::MapChanged); }
			const int32 After = P ? P->GetSharedTargetSlot() : -9;
			S->Expect(Before == 0 && After == 0, FString::Printf(TEXT("Т10 контроль (ключ asda.Party.ShareMarkResetOnMap 0): пометка пережила смену карты (было %d, стало %d) — прежнее поведение"), Before, After));
			AsdaCVarSave::SetForTest(TEXT("asda.Party.ShareMarkResetOnMap"), 1);
		});
	}

	static void Build(TSharedRef<FState> S)
	{
		// ── Подготовка ──
		S->Add(TEXT("подготовка"), 0.0, [S]()
		{
			AAsdaPlayerCharacter* Me = S->Me(); AAsdaPlayerController* PC = S->PC(); UAsdaNetSubsystem* Net = S->Net();
			UAsdaKeybindConfig* K = S->Keys();
			if (!Me || !PC || !Net || !K || !S->WS() || !S->Opt()) { return false; }
			TWeakPtr<FState> WSp = S;
			S->CapGen = AsdaSelfTestKit::ArmCapture(Net, [WSp](uint16 Op, const uint8* P, int32 Len, bool, TCHAR)
			{
				if (TSharedPtr<FState> Sp = WSp.Pin()) { FOut O; O.Op = Op; if (P && Len > 0) { O.Body.Append(P, Len); } Sp->Outs.Add(MoveTemp(O)); }
			}, 0, kCmd);
			if (!S->CapGen) { S->Expect(false, TEXT("перехват отправок занят")); S->Next = S->Steps.Num(); return true; }
			S->SelfScope = MakeUnique<FAsdaNetSelfScope>(Net, kCmd);
			if (!Net->bSelfUserNumKnown) { Net->SelfUserNum = 4321; Net->bSelfUserNumKnown = true; }
			Net->bSelfDead = false;
			S->bWasInGame = Net->bInGameConnected;
			Net->bInGameConnected = true;
			S->SavedWeapon = Net->SelfWeaponItemId;
			S->Weapon = FindWeapon(S->GI->GetSubsystem<UAsdaItemTableSubsystem>());
			Net->SelfWeaponItemId = S->Weapon;
			for (const TCHAR* Key : { TEXT("asda.Keys.TargetActions"), TEXT("asda.Keys.JumpPickupSplit"), TEXT("asda.Keys.SaveOnOkOnly"),
			                          TEXT("asda.Keys.CaptureSwallow"), TEXT("asda.Keys.KeepUnmappableOnLoad"), TEXT("asda.Jump.Enable") })
			{
				AsdaCVarSave::SetForTest(Key, 1);
			}
			// Раскладка: снимок живого объекта и ActionMappings, песочница файла, раскладка по умолчанию (как «По умолчанию»).
			S->SavedBinds = K->Binds; S->SavedSource = K->Source; S->SavedSourcePath = K->SourcePath;
			S->SavedMappings = AsdaMappings();
			S->IniPath = FPaths::ConvertRelativePathToFull(FPaths::Combine(FPaths::ProjectSavedDir(), TEXT("AsdaSelfTest"), TEXT("Shortcutkey_keybind_actions.ini")));
			IFileManager::Get().Delete(*S->IniPath, false, true, true);
			UAsdaKeybindConfig::SetTestIniOverride(S->IniPath);
			K->LoadDefaults();
			K->SaveToInputSettings(/*bSaveToDisk*/ false);   // слой ActionMappings — та же раскладка (как у первого запуска)
			StopCombat(*S);
			Me->SetAnimState(EAsdaPlayerState::Idle);
			S->Home = AsdaCoord::GameToTile(Me->GetGamePosXZ());
			Me->NetSnap(AsdaCoord::TileCenterGame(S->Home));
			S->LootTile = S->Home;
			UE_LOG(LogAsdaKeyActTest, Display, TEXT("%s подготовка: клетка (%d,%d), оружие %d, песочница «%s»"), kPrefix,
				S->Home.X, S->Home.Y, S->Weapon, *S->IniPath);
			// ★ 26.10.2026 (ревизия wave_1025d): круг Shift+Tab и пометка общей цели отряда — с начала (повторный прогон в той же игре
			//   начинал круг со строки 1 — Т1 падал 4/4, пометку — с прошлой строки — Т3/Т4).
			if (UAsdaPartySubsystem* Pt = S->GI->GetSubsystem<UAsdaPartySubsystem>()) { Pt->DebugResetTargetCycles(); }
			// ★ 26.10.2026 (п. 6): движковые отладочные привязки (DebugExecBindings) не пересекаются с клавишами игры — иначе в
			//   Development-сборке клавиша игрока параллельно исполняла бы команду движка (`;` → ToggleDebugCamera).
			{
				FString Clash;
				if (const UPlayerInput* PIn = PC->PlayerInput)
				{
					for (const FKeyBind& KB : PIn->DebugExecBindings)
					{
						const int32 Vk = UAsdaKeybindConfig::FKeyToVk(KB.Key);
						const bool bGameKey = (Vk != 0 && UAsdaKeybindConfig::VkToKeyName(Vk) != nullptr)
							|| KB.Key == EKeys::LeftShift || KB.Key == EKeys::RightShift || KB.Key == EKeys::PageDown;
						if (bGameKey) { Clash += FString::Printf(TEXT("%s=«%s» "), *KB.Key.ToString(), *KB.Command); }
					}
				}
				S->Expect(Clash.IsEmpty(), Clash.IsEmpty() ? FString(TEXT("D0 движковые DebugExecBindings не задевают клавиши игры (Config/DefaultInput.ini)"))
					: FString::Printf(TEXT("D0 движковые DebugExecBindings задевают клавиши игры: %s"), *Clash.TrimEnd()));
			}
			return true;
		}, 30.0);

		// ── D: раскладка по умолчанию ──
		S->Do(TEXT("D"), 0.1, [S]()
		{
			S->Expect(S->Bind(kGetItem).Vk == kVkF && S->Bind(kGetItem).Mod == EAsdaKeyMod::None,
				FString::Printf(TEXT("D1 по умолчанию «ACTION GET ITEM» = F (сейчас %s)"), *UAsdaKeybindConfig::FormatBind(S->Bind(kGetItem))));
			S->Expect(S->Bind(kAttack).Vk == kVkTilde, FString::Printf(TEXT("D2 «ACTION ATTACK» = ` (эталон keybindLoadDefaults; сейчас %s)"), *UAsdaKeybindConfig::FormatBind(S->Bind(kAttack))));
			S->Expect(S->Bind(kNearEnemy).Vk == kVkTab, FString::Printf(TEXT("D3 «ACTION TARGET NEAR_ENEMY» = TAB (сейчас %s)"), *UAsdaKeybindConfig::FormatBind(S->Bind(kNearEnemy))));
			S->Expect(S->Bind(kJump).Vk == kVkSpace, FString::Printf(TEXT("D4 «EXT JUMP» (прыжок) = ПРОБЕЛ (сейчас %s)"), *UAsdaKeybindConfig::FormatBind(S->Bind(kJump))));
			if (UAsdaNetSubsystem* N = S->Net()) { N->DebugFeedPacket(5005, Body5005(S->Weapon, S->LootTile)); }
		});
		S->Add(TEXT("лут у ног"), 0.2, [S]()
		{
			const UAsdaWorldSubsystem* Ws = S->WS(); AAsdaPlayerCharacter* Me = S->Me();
			return Ws && Me && Ws->FindFirstLootWithin(Me->GetActorLocation(), 300.f) != nullptr;
		});

		// ── J: Пробел по умолчанию — прыжок, без подбора ──
		AddTap(S, TEXT("J пробел"), EKeys::SpaceBar);
		S->Do(TEXT("J итог"), 0.3, [S]()
		{
			S->Expect(S->bJumpSeen, TEXT("J1 Пробел по умолчанию — прыжок"));
			S->Expect(S->Count(5012) == 0, FString::Printf(TEXT("J2 Пробел по умолчанию — подбора нет (5012: %d)"), S->Count(5012)));
		});
		// ── P: F — подбор ──
		S->Do(TEXT("P сброс"), 1.2, [S]() { if (UAsdaNetSubsystem* N = S->Net()) { N->bItemPickPending = false; } });
		AddTap(S, TEXT("P F"), EKeys::F);
		S->Do(TEXT("P итог"), 0.3, [S]()
		{
			const FOut* O = S->First(5012);
			const bool bTile = O && O->Body.Num() >= 8 && AsdaBytes::RdI16(&O->Body[O->Body.Num() - 4]) == S->LootTile.X
				&& AsdaBytes::RdI16(&O->Body[O->Body.Num() - 2]) == S->LootTile.Y;
			S->Expect(S->Count(5012) == 1, FString::Printf(TEXT("P1 F — подбор: 5012 ушёл (%d)"), S->Count(5012)));
			S->Expect(bTile, TEXT("P2 5012 несёт клетку предмета"));
			S->Expect(!S->bJumpSeen, TEXT("P3 F — без прыжка"));
			if (UAsdaNetSubsystem* N = S->Net())
			{
				N->bItemPickPending = false;
				S->RidA = 31001; S->RidB = 31002; S->RidFar = 31003;
				N->DebugFeedPacket(4013, AsdaTestPackets::MobSpawn4013(S->RidA, kMobType, S->Home + FIntPoint(3, 0), 1, kSpdDenom));
				N->DebugFeedPacket(4013, AsdaTestPackets::MobSpawn4013(S->RidB, kMobType, S->Home + FIntPoint(0, 6), 1, kSpdDenom));
				N->DebugFeedPacket(4013, AsdaTestPackets::MobSpawn4013(S->RidFar, kMobType, S->Home + FIntPoint(-18, 0), 1, kSpdDenom));
			}
		});
		S->Add(TEXT("мобы"), 0.2, [S]() { return S->Mob(S->RidA) && S->Mob(S->RidB) && S->Mob(S->RidFar); });

		// ── N: TAB — ближайший враг, чередование, дальний не берётся ──
		S->Do(TEXT("N цель снята"), 0.3, [S]() { if (AAsdaPlayerController* P = S->PC()) { P->ClearTarget(); } });
		AddTap(S, TEXT("N1 tab"), EKeys::Tab);
		S->Do(TEXT("N1"), 0.25, [S]()
		{
			AAsdaPlayerController* P = S->PC();
			S->Expect(P && P->TargetActor.Get() == S->Mob(S->RidA), TEXT("N1 TAB без цели — ближайший моб (3 клетки)"));
		});
		AddTap(S, TEXT("N2 tab"), EKeys::Tab);
		S->Do(TEXT("N2"), 0.25, [S]()
		{
			AAsdaPlayerController* P = S->PC();
			S->Expect(P && P->TargetActor.Get() == S->Mob(S->RidB), TEXT("N2 TAB повторно — второй ближайший (текущая цель пропущена)"));
		});
		AddTap(S, TEXT("N3 tab"), EKeys::Tab);
		S->Do(TEXT("N3"), 0.25, [S]()
		{
			AAsdaPlayerController* P = S->PC();
			S->Expect(P && P->TargetActor.Get() == S->Mob(S->RidA), TEXT("N3 TAB третий раз — снова первый; дальний (18 клеток, за порогом 2 300 000) не выбран"));
		});

		// ── A: ` — атака выбранной цели ──
		AddTap(S, TEXT("A tilde"), EKeys::Tilde);
		S->Do(TEXT("A"), 0.4, [S]()
		{
			AAsdaPlayerController* P = S->PC();
			const int32 N06 = S->Count(4006), N26 = S->Count(4026);
			S->Expect(P && P->bAutoAttack && P->TargetActor.Get() == S->Mob(S->RidA),
				FString::Printf(TEXT("A1 ` — атака выбранного моба: автоатака %d"), P && P->bAutoAttack ? 1 : 0));
			S->Expect(N06 + N26 > 0, FString::Printf(TEXT("A2 ушёл подход 4006 / удар 4026 (4006: %d, 4026: %d)"), N06, N26));
			StopCombat(*S);
		});
		// Без цели — ничего.
		AddTap(S, TEXT("A0 tilde"), EKeys::Tilde);
		S->Do(TEXT("A0"), 0.3, [S]()
		{
			AAsdaPlayerController* P = S->PC();
			S->Expect(P && !P->bAutoAttack && S->Count(4026) == 0 && S->Count(4006) == 0, TEXT("A3 ` без цели — ничего (эталон: ветки нет)"));
		});

		// ── R: переназначение окном настроек ──
		S->Do(TEXT("R открыть"), 0.2, [S]()
		{
			if (UAsdaSystemOptionSubsystem* O = S->Opt()) { O->ShowOptions(); O->SelectTab(2); }
			if (UAsdaKeybindConfig* K = S->Keys()) { K->SetBind(kScreenShot, kVkPrtSc, EAsdaKeyMod::None); }   // PrtSc — проверка слоя ActionMappings
		});
		AddCapture(S, TEXT("R 60→X"), kNearEnemy, EKeys::X);
		AddCapture(S, TEXT("R 59→O"), kAttack, EKeys::O);
		AddCapture(S, TEXT("R 56→пробел"), kGetItem, EKeys::SpaceBar);
		S->Do(TEXT("R1"), 0.3, [S]()
		{
			S->Expect(S->Bind(kNearEnemy).Vk == kVkX && S->Bind(kAttack).Vk == kVkO && S->Bind(kGetItem).Vk == kVkSpace,
				FString::Printf(TEXT("R1 окно: 60 = %s, 59 = %s, 56 = %s"), *UAsdaKeybindConfig::FormatBind(S->Bind(kNearEnemy)),
					*UAsdaKeybindConfig::FormatBind(S->Bind(kAttack)), *UAsdaKeybindConfig::FormatBind(S->Bind(kGetItem))));
			S->Expect(!S->Bind(kJump).IsBound(), FString::Printf(TEXT("R2 Пробел ушёл к подбору — у прыжка снят (сейчас %s)"), *UAsdaKeybindConfig::FormatBind(S->Bind(kJump))));
			S->Expect(!S->bJumpSeen && S->Count(5012) == 0, FString::Printf(TEXT("R3 клавиша захвата игровую команду не исполнила (прыжок %d, 5012: %d)"),
				S->bJumpSeen ? 1 : 0, S->Count(5012)));
			S->Expect(!IFileManager::Get().FileExists(*S->IniPath), TEXT("R4 во время захвата файл раскладки не пишется (эталон — только «ОК»)"));
		});
		AddCapture(S, TEXT("R 60→G"), kNearEnemy, EKeys::G);
		S->Do(TEXT("R5"), 0.3, [S]()
		{
			S->Expect(S->Bind(kNearEnemy).Vk == kVkG && !S->Bind(kUpgrade).IsBound(),
				FString::Printf(TEXT("R5 60 → G: G снят с 42 «INTERFACE UPGRADE» (Keybind_RemoveConflicts; у 42 теперь %s)"), *UAsdaKeybindConfig::FormatBind(S->Bind(kUpgrade))));
		});
		AddCapture(S, TEXT("R 60→X снова"), kNearEnemy, EKeys::X);
		S->Do(TEXT("R ОК"), 0.2, [S]() { if (UAsdaSystemOptionSubsystem* O = S->Opt()) { O->CloseOptions(true); } });
		S->Do(TEXT("R6"), 0.2, [S]()
		{
			FString Txt; FFileHelper::LoadFileToString(Txt, *S->IniPath);
			S->Expect(Txt.Contains(TEXT("ACTION TARGET NEAR_ENEMY:\tX")) && Txt.Contains(TEXT("ACTION ATTACK:\tO")) && Txt.Contains(TEXT("ACTION GET ITEM:\tSPACE"))
				&& Txt.Contains(TEXT("EXT JUMP:\tNONE")), TEXT("R6 «ОК» — ini записан (X / O / SPACE, прыжок NONE)"));
		});

		// ── S: «перезапуск» ──
		S->Do(TEXT("S"), 0.1, [S]()
		{
			AAsdaPlayerController* P = S->PC(); UAsdaKeybindConfig* K = S->Keys();
			if (!P || !K) { S->Expect(false, TEXT("S нет контроллера")); return; }
			UAsdaKeybindConfig* Fresh = NewObject<UAsdaKeybindConfig>(P);
			Fresh->LoadLayout(K->Profile);
			FAsdaKeyBind B60, B59, B56, B95, B42, B44;
			Fresh->GetBind(kNearEnemy, B60); Fresh->GetBind(kAttack, B59); Fresh->GetBind(kGetItem, B56); Fresh->GetBind(kJump, B95);
			Fresh->GetBind(kUpgrade, B42); Fresh->GetBind(kScreenShot, B44);
			S->Expect(B60.Vk == kVkX && B59.Vk == kVkO && B56.Vk == kVkSpace && !B95.IsBound() && !B42.IsBound(),
				FString::Printf(TEXT("S1 новый объект раскладки (LoadLayout): 60 %s, 59 %s, 56 %s, 95 %s, 42 %s"),
					*UAsdaKeybindConfig::FormatBind(B60), *UAsdaKeybindConfig::FormatBind(B59), *UAsdaKeybindConfig::FormatBind(B56),
					*UAsdaKeybindConfig::FormatBind(B95), *UAsdaKeybindConfig::FormatBind(B42)));
			S->Expect(B44.Vk == kVkPrtSc, FString::Printf(TEXT("S2 «SCREEN SHOT» = PrtSc пережил слой ActionMappings (сейчас %s)"), *UAsdaKeybindConfig::FormatBind(B44)));
			UAsdaKeybindConfig* FileOnly = NewObject<UAsdaKeybindConfig>(P);
			FileOnly->LoadDefaults();
			const bool bRead = FileOnly->LoadShortcutkeyIni(S->IniPath);
			FAsdaKeyBind F60, F59, F56, F95;
			FileOnly->GetBind(kNearEnemy, F60); FileOnly->GetBind(kAttack, F59); FileOnly->GetBind(kGetItem, F56); FileOnly->GetBind(kJump, F95);
			S->Expect(bRead && F60.Vk == kVkX && F59.Vk == kVkO && F56.Vk == kVkSpace && !F95.IsBound(), TEXT("S3 второй метод — только файл (LoadShortcutkeyIni): те же клавиши"));
			K->ReloadLayout();
			S->Expect(S->Bind(kNearEnemy).Vk == kVkX && S->Bind(kGetItem).Vk == kVkSpace && !S->Bind(kJump).IsBound(), TEXT("S4 ReloadLayout живого объекта — те же клавиши"));
		});

		// ── K: новые клавиши работают, старые — нет ──
		S->Do(TEXT("K цель снята"), 0.2, [S]() { StopCombat(*S); });
		AddTap(S, TEXT("K1 X"), EKeys::X);
		S->Do(TEXT("K1"), 0.25, [S]() { AAsdaPlayerController* P = S->PC(); S->Expect(P && P->TargetActor.Get() == S->Mob(S->RidA), TEXT("K1 X (переназначен) — ближайший моб")); });
		AddTap(S, TEXT("K2 O"), EKeys::O);
		S->Do(TEXT("K2"), 0.4, [S]()
		{
			AAsdaPlayerController* P = S->PC();
			S->Expect(P && P->bAutoAttack, TEXT("K2 O (переназначен) — атака цели"));
			StopCombat(*S);
			if (UAsdaNetSubsystem* N = S->Net()) { N->bItemPickPending = false; }
		});
		AddTap(S, TEXT("K3 пробел"), EKeys::SpaceBar);
		S->Do(TEXT("K3"), 0.3, [S]()
		{
			S->Expect(S->Count(5012) == 1, FString::Printf(TEXT("K3 Пробел, назначенный игроком на подбор, — подбирает (5012: %d)"), S->Count(5012)));
			S->Expect(!S->bJumpSeen, TEXT("K4 …и не прыгает"));
			if (UAsdaNetSubsystem* N = S->Net()) { N->bItemPickPending = false; }
		});
		AddTap(S, TEXT("K5 tab"), EKeys::Tab);
		AddTap(S, TEXT("K5 tilde"), EKeys::Tilde);
		AddTap(S, TEXT("K5 F"), EKeys::F);
		S->Do(TEXT("K5"), 0.3, [S]()
		{
			AAsdaPlayerController* P = S->PC();
			S->Expect(P && !P->TargetActor.IsValid() && !P->bAutoAttack && S->Count(5012) == 0,
				TEXT("K5 прежние TAB / ` / F больше ничего не делают (цели нет, атаки нет, 5012 нет)"));
		});

		// ── N4: мёртвый пропускается ──
		S->Do(TEXT("N4 смерть"), 0.1, [S]() { if (UAsdaNetSubsystem* N = S->Net()) { N->DebugFeedPacket(4017, Body4017Dead(S->RidA, S->Home + FIntPoint(3, 0))); } });
		S->Add(TEXT("N4 ждём смерть"), 0.1, [S]() { AAsdaMobActor* M = S->Mob(S->RidA); return !M || M->IsDead(); }, 8.0);
		AddTap(S, TEXT("N4 X"), EKeys::X);
		S->Do(TEXT("N4"), 0.25, [S]() { AAsdaPlayerController* P = S->PC(); S->Expect(P && P->TargetActor.Get() == S->Mob(S->RidB), TEXT("N4 мёртвый моб (состояние 10) пропущен — выбран второй")); });

		// ── C: «Отмена» не меняет назначение ──
		S->Do(TEXT("C открыть"), 0.2, [S]() { if (UAsdaSystemOptionSubsystem* O = S->Opt()) { O->ShowOptions(); O->SelectTab(2); } });
		AddCapture(S, TEXT("C 60→TAB"), kNearEnemy, EKeys::Tab);
		S->Do(TEXT("C отмена"), 0.2, [S]() { if (UAsdaSystemOptionSubsystem* O = S->Opt()) { O->CloseOptions(false); } });
		S->Do(TEXT("C"), 0.2, [S]()
		{
			FString Txt; FFileHelper::LoadFileToString(Txt, *S->IniPath);
			S->Expect(S->Bind(kNearEnemy).Vk == kVkX && Txt.Contains(TEXT("ACTION TARGET NEAR_ENEMY:\tX")),
				FString::Printf(TEXT("C1 «Отмена» — 60 осталось X в памяти (%s) и в файле"), *UAsdaKeybindConfig::FormatBind(S->Bind(kNearEnemy))));
		});

		// ── L: старый файл (подбор и прыжок оба на Пробеле) ──
		S->Do(TEXT("L"), 0.1, [S]()
		{
			AAsdaPlayerController* P = S->PC(); UAsdaKeybindConfig* K = S->Keys();
			if (!P || !K) { return; }
			K->LoadDefaults();
			K->SetBind(kGetItem, kVkSpace, EAsdaKeyMod::None);   // загрузочный путь (без правила прыжка) — как старый файл
			K->SetBind(kJump, kVkSpace, EAsdaKeyMod::None);
			K->SaveShortcutkeyIni(S->IniPath);
			K->SaveToInputSettings(/*bSaveToDisk*/ false);
			UAsdaKeybindConfig* Fresh = NewObject<UAsdaKeybindConfig>(P);
			Fresh->LoadLayout(K->Profile);
			FAsdaKeyBind B56, B95;
			Fresh->GetBind(kGetItem, B56); Fresh->GetBind(kJump, B95);
			S->Expect(B56.Vk == kVkF && B95.Vk == kVkSpace, FString::Printf(TEXT("L1 старый файл: подбор → %s, прыжок → %s (ждём F / SPACE)"),
				*UAsdaKeybindConfig::FormatBind(B56), *UAsdaKeybindConfig::FormatBind(B95)));
			AsdaCVarSave::SetForTest(TEXT("asda.Keys.JumpPickupSplit"), 0);
			UAsdaKeybindConfig* Old = NewObject<UAsdaKeybindConfig>(P);
			Old->LoadLayout(K->Profile);
			FAsdaKeyBind O56;
			Old->GetBind(kGetItem, O56);
			S->Expect(O56.Vk == kVkSpace, TEXT("L2 ключ asda.Keys.JumpPickupSplit 0 — прежнее: подбор остаётся на ПРОБЕЛЕ"));
			AsdaCVarSave::SetForTest(TEXT("asda.Keys.JumpPickupSplit"), 1);
			// ★ 26.10.2026 (ревизия wave_1025d 2.2): старый файл, где F уже занята («CHARACTER INFO» 30 = F), — подбор без клавиши, но
			//   не молча: взведён нотис (команда-владелец F = 30), окно настроек покажет его один раз за процесс.
			UAsdaKeybindConfig::ConsumePickupUnboundNotice();   // прежняя отметка (если была) — не в счёт
			K->LoadDefaults();
			K->SetBind(kGetItem, kVkSpace, EAsdaKeyMod::None);
			K->SetBind(kJump, kVkSpace, EAsdaKeyMod::None);
			K->SetBind(30, kVkF, EAsdaKeyMod::None);
			K->SaveShortcutkeyIni(S->IniPath);
			K->SaveToInputSettings(/*bSaveToDisk*/ false);
			UAsdaKeybindConfig* Busy = NewObject<UAsdaKeybindConfig>(P);
			Busy->LoadLayout(K->Profile);
			FAsdaKeyBind U56;
			Busy->GetBind(kGetItem, U56);
			const int32 FOwner = UAsdaKeybindConfig::ConsumePickupUnboundNotice();
			S->Expect(!U56.IsBound() && FOwner == 30, FString::Printf(TEXT("L3 F занята командой 30: подбор без клавиши (%s), нотис взведён (владелец F = %d)"),
				*UAsdaKeybindConfig::FormatBind(U56), FOwner));
		});
		if (!S->bSkipParty) { BuildParty(S); }
		// ── E: Enter после всех разделов — строка чата открывается (путь Slate, как у `asda.SelfTest.EnterConfirm`) ──
		S->Do(TEXT("E подготовка"), 0.3, [S]()
		{
			if (UAsdaChatWindowSubsystem* C = S->GI.IsValid() ? S->GI->GetSubsystem<UAsdaChatWindowSubsystem>() : nullptr) { if (C->IsInputActive()) { C->CloseInput(); } }
			if (FSlateApplication::IsInitialized()) { FSlateApplication::Get().SetAllUserFocusToGameViewport(); }
		});
		S->Do(TEXT("E вниз"), 0.15, [S]()
		{
			if (!FSlateApplication::IsInitialized()) { return; }
			FSlateApplication::Get().ProcessKeyDownEvent(FKeyEvent(EKeys::Enter, FModifierKeysState(), 0, false, 13, 13));
			FSlateApplication::Get().ProcessKeyCharEvent(FCharacterEvent(TEXT('\r'), FModifierKeysState(), 0, false));
		});
		S->Do(TEXT("E вверх"), 0.1, [S]()
		{
			APlayerController* P = S->PC();
			UE_LOG(LogAsdaKeyActTest, Display, TEXT("%s E: Enter зажат у контроллера %d"), kPrefix, P && P->IsInputKeyDown(EKeys::Enter) ? 1 : 0);
			if (FSlateApplication::IsInitialized()) { FSlateApplication::Get().ProcessKeyUpEvent(FKeyEvent(EKeys::Enter, FModifierKeysState(), 0, false, 13, 13)); }
		});
		S->Do(TEXT("E"), 0.2, [S]()
		{
			UAsdaChatWindowSubsystem* C = S->GI.IsValid() ? S->GI->GetSubsystem<UAsdaChatWindowSubsystem>() : nullptr;
			// НАБЛЮДЕНИЕ (не проверка): после раздела «Т» Enter через Slate иногда не доходит до контроллера (IsInputKeyDown = 0) —
			//   ниже `PollKeys`, в маршруте Slate → вьюпорт; плавает от прогона к прогону. Разбор — Reports/keybinds/AUDIT.md §8.
			UE_LOG(LogAsdaKeyActTest, Warning, TEXT("%s E (наблюдение): Enter после всех разделов — строка чата %s"), kPrefix,
				(C && C->IsInputActive()) ? TEXT("открыта") : TEXT("НЕ открыта (Enter не дошёл до контроллера)"));
			if (C && C->IsInputActive()) { C->CloseInput(); }
		});
	}

	static bool Tick(TSharedRef<FState> S)
	{
		if (!S->GI.IsValid()) { UAsdaKeybindConfig::SetTestIniOverride(FString()); AsdaCVarSave::RestoreAll(kCmd); return false; }
		if (S->Next >= S->Steps.Num()) { Finish(S); return false; }
		const double Now = FPlatformTime::Seconds();
		FStep& St = S->Steps[S->Next];
		const double In = Now - S->StepT;
		if (In < St.Delay) { return true; }
		const int32 Was = S->Next;
		const bool bDone = St.Fn();
		if (S->Next != Was) { return true; }
		if (bDone) { ++S->Next; S->StepT = FPlatformTime::Seconds(); return true; }
		if (In - St.Delay > St.Timeout)
		{
			S->Expect(false, FString::Printf(TEXT("шаг «%s» не дождался за %.0f с"), *St.Name, St.Timeout));
			Finish(S);
			return false;
		}
		return true;
	}
}

static FAutoConsoleCommandWithWorldAndArgs GAsdaSelfTestKeybindActions(
	TEXT("asda.SelfTest.KeybindActions"),
	TEXT("[ОРИГИНАЛ] Самотест клавиш «подобрать» (56), «атаковать цель» (59), «ближайший враг» (60) и прыжка (95): ввод контроллера, ")
	TEXT("переназначение окном настроек, «перезапуск» раскладки, «Отмена», старый файл. Офлайн, на карте мира, только с -saveddirsuffix. [delay=N] [quit]"),
	FConsoleCommandWithWorldAndArgsDelegate::CreateLambda([](const TArray<FString>& Args, UWorld* W)
	{
		using namespace AsdaKeyActTest;
		const FAsdaSelfTestArgs KA = FAsdaSelfTestArgs::Parse(Args);
		static const uint16 kGuardOps[] = { 4006, 4008, 4024, 4026, 4028, 4201, 5012, 5098, 5100, 5478 };
		if (AsdaSelfTestKit::RefuseStart(W, kCmd, KA, 0.f, MakeArrayView(kGuardOps))) { return; }
		// ActionMappings «ОК» пишет в Saved/Config — без отдельной папки Saved прогон испортил бы Input.ini владельца.
		if (!FString(FCommandLine::Get()).Contains(TEXT("-saveddirsuffix=")))
		{
			AsdaSelfTestKit::Refused(W, kCmd, KA.bQuit, TEXT("нужен -saveddirsuffix (иначе «ОК» окна запишет Input.ini владельца)"));
			return;
		}
		UGameInstance* GI = W ? W->GetGameInstance() : nullptr;
		if (!GI) { AsdaSelfTestKit::Refused(W, kCmd, KA.bQuit, TEXT("нет GameInstance")); return; }
		if (AsdaSelfTestKit::NeedSelfSpawn(W) && GEngine)
		{
			GEngine->Exec(W, TEXT("Asda.Load.CurrentMap 3"));
			GEngine->Exec(W, TEXT("Asda.World.FakeSelfSpawn 256 256"));
		}
		TSharedRef<FState> S = MakeShared<FState>();
		S->GI = GI;
		S->bQuit = KA.bQuit;
		S->bSkipParty = KA.Has(TEXT("noparty"));
		for (const FString& A : Args) { if (A.StartsWith(TEXT("partystop="))) { S->PartyStop = FCString::Atoi(*A.Mid(10)); } }
		Build(S);
		S->StepT = FPlatformTime::Seconds() + KA.Delay;
		FTSTicker::GetCoreTicker().AddTicker(FTickerDelegate::CreateLambda([S](float) -> bool { return Tick(S); }), 0.0f);
		UE_LOG(LogAsdaKeyActTest, Display, TEXT("%s запуск: шагов %d"), kPrefix, S->Steps.Num());
	}));

#endif // ASDA_DEV_CONSOLE && !UE_BUILD_SHIPPING
