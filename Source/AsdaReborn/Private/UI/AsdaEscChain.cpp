// Copyright (c) 2026 @sitxovski (Discord)
// AsdaEscChain.cpp — шаг «ESC снимает выбранную цель» эталонной цепочки ESC. [ОРИГИНАЛ] 25.10.2026. Разбор — в шапке
//   `Public/UI/AsdaEscChain.h`.
#include "UI/AsdaEscChain.h"
#include "UI/AsdaWindowSubsystem.h"           // список порта «ESC нажимает крестик»
#include "AsdaDevConsole.h"                 // ASDA_DEV_CVAR — дев-ключи вне Shipping
#include "World/AsdaPlayerController.h"     // выбранная цель (`TargetActor`) и её снятие (`ClearTarget`)
#include "World/AsdaPlayerCharacter.h"      // вид цели 0 эталона: персонаж (свой или чужой)
#include "Npc/AsdaMobActor.h"               // вид цели 1: моб
#include "Npc/AsdaNpcActor.h"               // вид цели 3: NPC
#include "Npc/AsdaNpcDialogSubsystem.h"     // ★ разговор NPC 570: открыт ли, штатное закрытие кнопкой 575
#include "UI/AsdaChatWindowSubsystem.h"     // ветка (в): строка чата открыта → закрыть
#include "World/AsdaWorldSubsystem.h"       // моб «netWord54» ещё в мире (`FindNetMobByRid`)
#include "HAL/PlatformTime.h"
#include "Engine/GameInstance.h"
#include "Engine/World.h"

DEFINE_LOG_CATEGORY_STATIC(LogAsdaEscChain, Log, All);

// Ключ отката шага цели: 0 — как до 25.10.2026 (ESC цель не снимает, без окон сразу открывает ESC-меню 69400).
static ASDA_DEV_CVAR(int32, CVarEscClearTarget,
	TEXT("asda.UI.EscClearTarget"), 1,
	TEXT("1 — [ОРИГИНАЛ] ESC снимает выбранную цель (моб, игрок, сам игрок, NPC) и прячет её плашку, если окно, закрытое этим же ")
	TEXT("нажатием, не потратило его целиком (флаг CWindowMgr+0x215 эталона, GameInput_DispatchEvent@0xC6DCA0 case 2); ")
	TEXT("снятие цели тратит нажатие — ESC-меню 69400 не открывается. 0 — как до 25.10.2026: ESC цель не снимает."),
	ECVF_Default);

// Ключ отката ESC из поля ввода: 0 — как до 25.10.2026 (ESC из поля вне модалки получает только само поле: строка чата закрывается).
static ASDA_DEV_CVAR(int32, CVarEscFromTextField,
	TEXT("asda.UI.EscFromTextField"), 1,
	TEXT("1 — [ОРИГИНАЛ] ESC из поля ввода вне модалки (строка чата и поля окон) идёт по цепочке эталона: верхнее окно с признаком ")
	TEXT("ESC, затем выбранная цель (Win32-поле эталона пересылает отпускание менеджеру окон, ui_core.c:116210); строка чата ")
	TEXT("закрывается, только если тратить нажатие было не на что. 0 — как до 25.10.2026: ESC получает только поле."),
	ECVF_Default);

// ★ 25.10.2026 [ИСПРАВЛЕНИЕ] (решение владельца): ESC закрывает разговор NPC. Ключ 0 — как у оригинала.
static ASDA_DEV_CVAR(int32, CVarEscCloseNpcDialog,
	TEXT("asda.UI.EscCloseNpcDialog"), 1,
	TEXT("1 — [ИСПРАВЛЕНИЕ] ESC закрывает разговор с NPC (окно 570) тем же закрытием, что кнопка 575 (окна торговца уходят вместе с ")
	TEXT("разговором, 5045 «первой встречи» — как у кнопки); порядок: верхнее окно → разговор → цель → меню. 0 — как у оригинала: ")
	TEXT("у окна 570 признака ESC нет, ESC снимает только выделение NPC."),
	ECVF_Default);

// ★ 25.10.2026 [ОРИГИНАЛ]: ветки (в) цепочки ESC по состоянию своего персонажа. Ключ 0 — как до 25.10.2026.
static ASDA_DEV_CVAR(int32, CVarEscStateBranches,
	TEXT("asda.UI.EscStateBranches"), 1,
	TEXT("1 — [ОРИГИНАЛ] ESC без окон и цели смотрит состояние своего персонажа (GameInput_DispatchEvent@0xC6DCA0 case 2): в бою и на ")
	TEXT("бегу меню 69400 не открывается, бег с заявкой удара останавливается (4008 в своей точке). 0 — как до 25.10.2026."),
	ECVF_Default);

// ★ 26.10.2026 (ревизия wave_1025d 3.1) [ОРИГИНАЛ]: «кто меня ударил» (`[1].netWord54`) сбрасывается при смене карты, перелогине и
//   сбросе сессии (у эталона это поле персонажа, персонаж пересоздаётся). Ключ 0 — как 25.10 (поле переживало смену карты).
static ASDA_DEV_CVAR(int32, CVarEscAttackerReset,
	TEXT("asda.UI.EscAttackerReset"), 1,
	TEXT("1 — [ОРИГИНАЛ] «кто ударил своего» (netWord54 веток ESC) и прошлое состояние сбрасываются при любом сбросе сессии, включая ")
	TEXT("смену карты (у эталона поле персонажа, персонаж пересоздаётся); 0 — как 25.10.2026: значение переживало смену карты и перелогин."),
	ECVF_Default);

// Доступ к закрытому эталонному состоянию действия контроллера (друг `AAsdaPlayerController`).
struct FAsdaEscChainAccess
{
	static int32 State(const AAsdaPlayerController* PC) { return PC ? PC->KbEtalonActionState() : 0; }
};

namespace AsdaEscChain
{
	// ★★★ ДАННЫЕ ОРИГИНАЛА: окна из таблицы ESC (`kEscCloseWindows`), чья оконная процедура в ветке события 21 ставит
	//   `CWindowMgr+0x215 = 1` (`sub_92CA8D(g_pWindowMgr, 1)` → `set_o215@0xBD4450`). Снято двумя методами: декомпил
	//   (`AsdaStory_game.c` и `AsdaStory_ida.c` — по 19 вызовов) и бинарь (`RE/ida/work/AsdaStory.exe`, 23 ссылки на
	//   0x92CA8D/0xBD4450: 19 + 2 сброса в `GameInput_DispatchEvent` + переход thunk + 0xDB91EB, которого в декомпиле нет).
	//   Вызовы 0xD42D3C (`uiWndCloseOnMsg13000`, 13000) и 0xE0BFF6 (`uiWindowMsgProc_BE10`, 2100) — окна без признака ESC,
	//   проход им событие 21 не шлёт; в таблицу не входят. Имя окна — ключ реестра окон порта (= ID корня .JUI).
	struct FFullRow
	{
		const TCHAR* WindowId;   // ключ реестра окон
		int32 IdNum;             // IDNum корня .JUI
		const TCHAR* Origin;     // эталонная процедура и адрес вызова set_o215(1)
	};
	static const FFullRow kEscFullyConsumed[] =
	{
		{ TEXT("INVEN_WIN"),          400, TEXT("CGameWindow_OnCommand@0xD1B140 (0xD1B4C3)") },
		{ TEXT("QUEST_WIN"),          700, TEXT("CQuest_Episode_OnEvent@0xC5BB30 (0xC5BE31)") },
		// В эталоне — кроме режима назначения клавиши (`wndThis_2[86] == 1` → `keybindClearActive` без флага); окна 800 в порту
		//   нет (его роль у 69500), строка — ради полноты данных.
		{ TEXT("SYSTEM_WIN"),         800, TEXT("SystemMenu_OnUICommand@0xE93E80 (0xE941B6)") },
		{ TEXT("CHARAINFO_WIN"),      900, TEXT("UI_Window_OnControlMessage@0xE765D0 (0xE76861)") },
		{ TEXT("NPCSHOP_WIN"),       1300, TEXT("uiHandleWindowCommand@0xE24DE0 (0xE24FDD)") },
		{ TEXT("NUMPAD_WIN"),        1410, TEXT("uiQuantityNumPadDialogProc@0xDA6300 (0xDA63B8)") },
		{ TEXT("TWOBUT_MSGBOX"),     1520, TEXT("uiItemUpgradeWnd_OnCommand@0xD79260 (0xD79638)") },
		{ TEXT("SOWELL_SELBOX"),     1530, TEXT("uiItemUpgradeWnd_OnCommand@0xD79260 (0xD7965E)") },
		{ TEXT("MAXMAP_WIN"),        1710, TEXT("MiniMap_BigMapWindowProc@0xD65210 (0xD65BF5)") },
		{ TEXT("OTHERCHARA_DATA"),   4200, TEXT("npcTalkWnd_HandleMessage@0xDA9B70 (0xDA9BFC)") },
		{ TEXT("COMMUNITY_WIN"),     6400, TEXT("uiDlg_WindowProc@0xBD4290 (0xBD434D)") },
		{ TEXT("Find_Soulmate_Win"), 8200, TEXT("uiUserListWndProc@0xE72460 (0xE72592)") },
		{ TEXT("Find_Party_WIn"),    8500, TEXT("процедура без функции в IDB, ветка 0x15 @0xDB8AB8 (0xDB91EB)") },
		{ TEXT("SKILL_WIN"),        12000, TEXT("skillWndOnUICommand@0xE4A8E0 (0xE4ABB6)") },
		{ TEXT("Title_Win"),        18700, TEXT("questLog_OnUiEvent@0xE97B00 (0xE97FE2)") },
		{ TEXT("Get_UpgradeOtion"), 19100, TEXT("CGameWindow_OnCommand@0xD1B140 (0xD1B4E4)") },
	};

	static FName GLastAnswered;      // окно, ответившее последнему проходу ESC (NAME_None — никто)
	static int32 GClearCount = 0;    // сколько раз шаг снял цель (самотест)
	static int32 GAttackerRid = -1;  // эталонный `[1].netWord54`: renderId моба, ударившего своего (−1 — нет)
	static int32 GPrevState = 0;     // состояние действия своего в прошлом кадре (вход в 0 стирает GAttackerRid)
	static int32 GStopCount = 0;     // ветки (в): остановок 4008 + покой
	static int32 GConsumeCount = 0;  // ветки (в): нажатий, потраченных без меню

	bool ClearTargetOn() { return CVarEscClearTarget.GetValueOnGameThread() != 0; }
	bool FromTextFieldOn() { return CVarEscFromTextField.GetValueOnGameThread() != 0; }
	FName LastAnsweredWindow() { return GLastAnswered; }
	void NoteAnsweredWindow(FName WindowId) { GLastAnswered = WindowId; }
	int32 GetClearCount() { return GClearCount; }

	bool IsFullyConsumedWindow(FName WindowId)
	{
		if (WindowId.IsNone()) { return false; }
		for (const FFullRow& R : kEscFullyConsumed)
		{
			if (WindowId == FName(R.WindowId)) { return true; }
		}
		return false;
	}

	// Вид выбранной цели по эталону: 0 — персонаж (и сам игрок), 1 — моб, 3 — NPC; −1 — иной актор (лут и пр.), его шаг не трогает.
	static int32 TargetKind(const AActor* A)
	{
		if (!A) { return -1; }
		if (A->IsA<AAsdaMobActor>()) { return 1; }
		if (A->IsA<AAsdaNpcActor>()) { return 3; }
		if (A->IsA<AAsdaPlayerCharacter>()) { return 0; }
		return -1;
	}

	bool CloseNpcDialogOn() { return CVarEscCloseNpcDialog.GetValueOnGameThread() != 0; }
	bool StateBranchesOn() { return CVarEscStateBranches.GetValueOnGameThread() != 0; }
	int32 GetSelfAttackerRid() { return GAttackerRid; }
	int32 GetStateStopCount() { return GStopCount; }
	int32 GetStateConsumeCount() { return GConsumeCount; }

	bool RunAfterWindowPass(UGameInstance* GI, bool bWindowAnswered, const TCHAR* Path, bool bAllowDialog)
	{
		// (а) окно, закрытое этим нажатием, поставило `CWindowMgr+0x215` — эталон сбрасывает флаг и выходит.
		const UAsdaWindowSubsystem* EscWm = GI ? GI->GetSubsystem<UAsdaWindowSubsystem>() : nullptr;
		if (ClearTargetOn() && bWindowAnswered && (IsFullyConsumedWindow(GLastAnswered) || (EscWm && EscWm->IsEscExtraWindow(GLastAnswered))))
		{
			UE_LOG(LogAsdaEscChain, Log, TEXT("[ESC/%s] окно «%s» потратило нажатие целиком (эталон CWindowMgr+0x215) — цель не трогаем"),
				Path, *GLastAnswered.ToString());
			return true;
		}
		// ★ [ИСПРАВЛЕНИЕ] разговор NPC 570: окно этим нажатием не закрывалось — закрыть разговор той же функцией, что кнопка 575
		//   (`OnDialogButton(575)` → `Close()` = порт `UI_resetAllWindowsAndLeave_5045`: окна торговца по правилу 26.08, 5045
		//   «первой встречи», флаг 0x1000 собеседника). Выделение NPC не снимается — это следующий ESC (как у кнопки).
		if (CloseNpcDialogOn() && bAllowDialog && !bWindowAnswered)
		{
			UAsdaNpcDialogSubsystem* Dlg = GI ? GI->GetSubsystem<UAsdaNpcDialogSubsystem>() : nullptr;
			if (Dlg && Dlg->IsDialogOpen())
			{
				const int32 NpcId = Dlg->GetCurrentNpcId();
				Dlg->OnDialogButton(575);
				if (!Dlg->IsDialogOpen())
				{
					UE_LOG(LogAsdaEscChain, Log, TEXT("[ESC/%s] закрыт разговор с NPC %d (кнопка 575) [ИСПРАВЛЕНИЕ] — нажатие потрачено"), Path, NpcId);
					return true;
				}
				UE_LOG(LogAsdaEscChain, Log, TEXT("[ESC/%s] разговор с NPC %d не закрылся (окно наград ждёт «ОК» 567 — как у кнопки 575)"), Path, NpcId);
			}
		}
		if (!ClearTargetOn()) { return false; }
		// (б) выбранная цель своего вида — снять (плашку, подсветку и кольцо гасит `ClearTarget`).
		UWorld* W = GI ? GI->GetWorld() : nullptr;
		AAsdaPlayerController* PC = W ? Cast<AAsdaPlayerController>(W->GetFirstPlayerController()) : nullptr;
		AActor* Target = PC ? PC->TargetActor.Get() : nullptr;
		const int32 Kind = TargetKind(Target);
		if (Kind < 0) { return false; }
		const FString Who = Target->GetName();
		PC->ClearTarget();
		++GClearCount;
		UE_LOG(LogAsdaEscChain, Log, TEXT("[ESC/%s] снята цель «%s» (вид %d эталона: 0 персонаж, 1 моб, 3 NPC)%s — нажатие потрачено"),
			Path, *Who, Kind, bWindowAnswered ? *FString::Printf(TEXT(", окно «%s» закрыто тем же нажатием"), *GLastAnswered.ToString()) : TEXT(""));
		return true;
	}

	void ResetSelfHitState(const TCHAR* Why)
	{
		if (CVarEscAttackerReset.GetValueOnGameThread() == 0) { return; }
		if (GAttackerRid >= 0)
		{
			UE_LOG(LogAsdaEscChain, Log, TEXT("[ESC] «кто меня ударил» (renderId %d) сброшен: %s"), GAttackerRid, Why ? Why : TEXT("?"));
		}
		GAttackerRid = -1;
		GPrevState = 0;
	}

	void NoteSelfHitByMob(int32 MobRid, int32 HitValue)
	{
		// `Combat_ApplyDamage` (combat_pvp.c:7756/7770): −1 уклонение, −2 и 0 промах — поле не пишется; прочее (урон, крит) — пишется.
		if (MobRid < 0 || HitValue == -1 || HitValue == -2 || HitValue == 0) { return; }
		GAttackerRid = MobRid;
	}

	void TickSelfState(UGameInstance* GI)
	{
		UWorld* W = GI ? GI->GetWorld() : nullptr;
		const AAsdaPlayerController* PC = W ? Cast<AAsdaPlayerController>(W->GetFirstPlayerController()) : nullptr;
		if (!PC) { return; }
		const int32 St = FAsdaEscChainAccess::State(PC);
		// `CCharacter_SetAction` case 0 под гейтом `field_8 != newAction`: только переход В покой стирает netWord54.
		if (St == 0 && GPrevState != 0) { GAttackerRid = -1; }
		GPrevState = St;
	}

	// Порт `objStateAllowsRender@0xA2A2F0` для своего персонажа (по смыслу «в бою»), карта полей — в шапке заголовка.
	static bool InFight(const AAsdaPlayerController* PC, UGameInstance* GI, int32 St)
	{
		// Моб `netWord54` ещё в мире (`Entity_FindByRenderId != 0` — и мёртвый до деспавна тоже).
		auto AttackerAlive = [GI]() -> bool
		{
			const UAsdaWorldSubsystem* WS = GI ? GI->GetSubsystem<UAsdaWorldSubsystem>() : nullptr;
			return GAttackerRid >= 0 && WS && WS->FindNetMobByRid(GAttackerRid) != nullptr;
		};
		if (St == 0)
		{
			// Подрежим 10/20 «боевой покой» — [ПОРТ] стойка боя (`bInCombatStance`) и её хвост после боя (`CombatStanceUntil`,
			//   10 с эталонного `CGameUI_UpdateFadeAnim` до подрежима 15).
			const bool bCombatIdle = PC->bInCombatStance || PC->CombatStanceUntil > FPlatformTime::Seconds();
			return bCombatIdle || AttackerAlive();
		}
		if (St == 1 || (St >= 7 && St <= 9) || (St >= 33 && St <= 47) || St == 51 || (St >= 60 && St <= 67)) { return AttackerAlive(); }
		return true;   // прочие состояния (смерть 6, удар 5, навык 24, каст 13/14/27, добыча 68 …) — «в бою»
	}

	bool RunStateBranches(UGameInstance* GI, const TCHAR* Path)
	{
		UWorld* W = GI ? GI->GetWorld() : nullptr;
		AAsdaPlayerController* PC = W ? Cast<AAsdaPlayerController>(W->GetFirstPlayerController()) : nullptr;
		AAsdaPlayerCharacter* Ch = PC ? Cast<AAsdaPlayerCharacter>(PC->GetPawn()) : nullptr;
		if (StateBranchesOn() && PC && Ch)
		{
			const int32 St = FAsdaEscChainAccess::State(PC);
			// [1].field118 «заявка удара»: [ПОРТ] 4026 ушла (`bCombatStarted`) или идёт PvP-атака (`bPvpAttack`, клик input.c:1624).
			const bool bIntent = PC->bCombatStarted || PC->bPvpAttack;
			const bool bMobMask = (Ch->TargetModeMask & 1) != 0;   // [1].gap_0[2] бит 0
			auto Stop = [PC, Path](const TCHAR* Why)
			{
				// `Net_SendActionPacket` (4008 в своей точке, гейт «есть незакрытый ход») + `CCharacter_SetAction(0)`.
				PC->StopMoveAndNotify(Why);
				++GStopCount;
				UE_LOG(LogAsdaEscChain, Log, TEXT("[ESC/%s] %s: стоп 4008 в своей точке и покой (эталон game_main.c:9318/9342)"), Path, Why);
			};
			if (InFight(PC, GI, St))
			{
				if (St == 5 || St == 24)
				{
					// `*(LP + 208484) = 0` — отложенное действие 52121 снято. [ПОРТ] отдельного поля нет: удержание автоатаки
					//   (52121 = 100) у порта — `bAutoAttack`, а он гаснет вместе с выбранной целью (шаг (б) выше); отложенное
					//   102 навыка исполняется следующим тиком (`UAsdaSkillUseSubsystem::FlushPendingAction`) — снимать нечего.
					++GConsumeCount;
					UE_LOG(LogAsdaEscChain, Log, TEXT("[ESC/%s] состояние %d (удар/навык) без цели: 52121 = 0, меню не открывается"), Path, St);
					return true;
				}
				if ((St == 1 || St == 2) && (bIntent || bMobMask))
				{
					Stop(bIntent ? TEXT("бег в бою с заявкой удара") : TEXT("бег в бою, маска цели-моба"));
				}
				++GConsumeCount;
				UE_LOG(LogAsdaEscChain, Log, TEXT("[ESC/%s] персонаж в бою (состояние %d, моб-обидчик %d) — меню не открывается"), Path, St, GAttackerRid);
				return true;
			}
			if (St == 1 || St == 2)
			{
				if (bIntent) { Stop(TEXT("бег с заявкой удара")); }
				++GConsumeCount;
				UE_LOG(LogAsdaEscChain, Log, TEXT("[ESC/%s] персонаж бежит (состояние %d) — меню не открывается"), Path, St);
				return true;
			}
		}
		// Строка чата открыта (`sub_9250EE(g_pChatWnd)`) — `uiNameInputConfirmShow`: текст стёрт, фокус снят, строка закрыта.
		if (UAsdaChatWindowSubsystem* Chat = GI ? GI->GetSubsystem<UAsdaChatWindowSubsystem>() : nullptr; Chat && Chat->IsInputActive())
		{
			Chat->CloseInput();
			UE_LOG(LogAsdaEscChain, Log, TEXT("[ESC/%s] строка чата закрыта (uiNameInputConfirmShow) — меню не открывается"), Path);
			return true;
		}
		return false;
	}
}
