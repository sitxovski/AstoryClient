// Copyright (c) 2026 @sitxovski (Discord)
// ★★★ САМОТЕСТ «ПОРТАЛЬНЫЙ ВХОД НЕ ПЕРЕЖИВАЕТ РАЗРЫВ» (25.10.2026, жалоба Player52; тот же сбой у Player17 21.10). [ПОРТ]
//
// ★ ЗАЧЕМ. `bPortalPending` снимался только отправкой 5058: платный телепорт Player52 25.10 16:53:21 не подключился к целевой
//   карте (предел 3 с, «таймаут подключения … сервер не отвечает»), игрок перелогинился — и Load Manager после загрузки
//   карты послал портальный 5058 вместо 4001. Сервер на 5058 не шлёт 5251/6057/6106 — окна навыков и питомцев пустые.
//   Исправления (шапки — AsdaNetSubsystem.h/.cpp): `ClearPortalTransferState` (разрыв, полный сброс сессии, вход из выбора
//   персонажа, страховка 30 с), неблокирующее подключение со сроком 20 с, `HandleGameConnectFailed` (нотис 1001, разрыв).
//   Попутно: подсказка карточки премиум-магазина для кодов второй таблицы (свитки 196/197) и подстановка ветки в 12854.
//
// ★ КАК ПРОВЕРЯЕТ (реальный путь, без сети — закон diagnostic-must-use-real-path):
//   A. 5055 через штатный приёмник (`DebugFeedPacket`) взводит переход; смена карты (MapChanged) признак НЕ снимает (контроль);
//      разрыв `HandleConnectionClosed` снимает всё — следующий вход 4001; вход из выбора персонажа (EnteringWorld) — тоже.
//   B. Страховка 30 с: соединения нет → признак снят; идёт подключение (bAwaitGameConnect) → ★ 26.10.2026 страховка не считает
//      (гейт и признак держатся, отсчёт 0 — ключ asda.Net.TransferGuardAfterConnect); контроль ключом 0 — прежнее: гейт снят, признак остаётся.
//   C. Неудачное подключение: 5055 → «подключение начато» → `FAsdaConnection::DebugBeginStalledConnect` (никто не отвечает,
//      сокета нет) → срок истёк → OnClosed → `HandleConnectionClosed` → `HandleGameConnectFailed`: попап 8000 с нотисом 1001,
//      причина 4, признаки перехода сняты. Контроль — ключ asda.Net.GameConnectFailDisconnect 0: попапа нет (прежнее).
//   D. После разрыва «перелогин»: 6057 + 5251 + 6106 (байты из журнала Player52 25.10 16:52:00) — навык 701 и питомец Dogi в
//      моделях окон (сервер их шлёт только на 4001 — путь окон цел).
//   E. 5464 с веткой 1 — 12854 с названием ветки 9400 (RU/EN — по языку прогона); с веткой 0 — «[]», как у оригинала
//      (`skillUiOnLearnSkill_6590`: при grade вне 1..9 слот пустой).
//   F. Подсказка карточки премиум-магазина для 196 (вторая таблица) показана; контроль — витрина NPC (EquipShop) и ключ 0.
//   Состояние подсистемы сети и ключи возвращаются в конце; ★ 31.10.2026 — и мир своего персонажа (`FAsdaWorldSelfScope`: ссылка
//   на павн, которую обнуляют сбросы сессии, и питомец «Dogi» из 6106), иначе следующие самотесты пакета шли без персонажа.
//
// Консоль: `asda.SelfTest.PortalPending [delay=N] [quit]`. Только офлайн (-AsdaNoNet), карта мира. Строки — «ПОРТАЛ-ВХОД»,
//   итог — «ПОРТАЛ-ВХОД ИТОГ: ok=… fail=…». Прогон делает настоящий сброс сессии (разрыв) — запускать отдельным пакетом.
#include "CoreMinimal.h"
#include "AsdaDevConsole.h"

#if ASDA_DEV_CONSOLE && !UE_BUILD_SHIPPING

#include "AsdaNetSubsystem.h"
#include "AsdaConnection.h"
#include "AsdaSessionReset.h"
#include "Test/AsdaSelfTestKit.h"
#include "UI/AsdaDisconnectPopupSubsystem.h"
#include "UI/AsdaWindowSubsystem.h"
#include "UI/AsdaSkillWindowSubsystem.h"
#include "UI/AsdaItemTooltipSubsystem.h"
#include "UI/AsdaTipPanel.h"
#include "UI/AsdaNotice.h"
#include "Text/AsdaTextSubsystem.h"
#include "Pet/AsdaPetSubsystem.h"
#include "Test/AsdaSelfTestWorldScope.h"   // ★ 31.10.2026: свой павн и питомцы мира — вернуть в конце (сбросы сессии обнуляют ссылку, 6106 призывает «Dogi»)
#include "Items/AsdaItemTableSubsystem.h"
#include "Blueprint/UserWidget.h"
#include "Components/TextBlock.h"
#include "Engine/GameInstance.h"
#include "Engine/World.h"
#include "HAL/IConsoleManager.h"
#include "Misc/OutputDevice.h"
#include "Misc/OutputDeviceRedirector.h"
#include "TimerManager.h"

DEFINE_LOG_CATEGORY_STATIC(LogAsdaPortalPendingTest, Log, All);

namespace AsdaPortalPendingTestLocal
{
	// Имя команды (строка итога, отказ, возврат ключей).
	static const TCHAR* kCmd = TEXT("asda.SelfTest.PortalPending");

	// Перехватчик строк журнала: сообщения окна навыков («[навыки] msgbox:») и попапа обрыва («[обрыв]»).
	class FSink : public FOutputDevice
	{
	public:
		TArray<FString> SkillMsg;   // тексты однокнопочных сообщений окна навыков
		TArray<FString> Disc;       // строки попапа обрыва
		virtual void Serialize(const TCHAR* V, ELogVerbosity::Type, const FName& Category) override
		{
			if (!IsInGameThread()) { return; }   // ★ 05.11.2026: устройство синхронное (CanBeUsedOnMultipleThreads) — ловим только строки игрового потока: их пишет проверяемый код, читает самотест, замка у полей нет
			static const FName kSkill(TEXT("LogAsdaSkillWin"));
			static const FName kDisc(TEXT("LogAsdaDisc"));
			if (!V) { return; }
			const FString S(V);
			if (Category == kSkill && S.StartsWith(TEXT("[навыки] msgbox: "))) { SkillMsg.Add(S.Mid(17)); }
			else if (Category == kDisc) { Disc.Add(S); }
		}
		// Пишут и с чужих потоков — приёмник только копит строки.
		virtual bool CanBeUsedOnAnyThread() const override { return true; }
		virtual bool CanBeUsedOnMultipleThreads() const override { return true; }   // ★ 05.11.2026: НЕбуферизованный список UE 5.8 — строка доходит в момент записи (иначе гонка с потоком логов: строка после RemoveOutputDevice/проверки)
	};

	// Байты из журнала Player52 25.10.2026 16:52:00 (вход 4001): первая запись 5251 (навык 701) и 6106 (питомец Dogi, 51 б).
	static TArray<uint8> Body5251()
	{
		TArray<uint8> B; B.SetNumZeroed(34);
		B[0] = 0xBD; B[1] = 0x02; B[2] = 0x00; B[3] = 0x01;   // код 701, уровень 0, «можно вкачивать»
		return B;
	}
	static TArray<uint8> Body6106()
	{
		static const uint8 K[51] = {
			0xDE,0x25,0x01,0x00, 0x00, 0x4A,0xB2,0x02,0x00, 0x14,0x00, 0x44,0x6F,0x67,0x69,0x00,0x00,0x00,0x00,0x00,0x00,0x00,
			0x00,0x00,0x00,0x00,0x00, 0x3B,0x01,0x04,0x1A,0x00,0x07,0x00,0x01,0x00,0x00,0x00,0x17,0x00,0x01,0x00,0x00,0x00,
			0x00,0x00,0x00,0x00,0x00,0x00,0x01 };
		return TArray<uint8>(K, 51);
	}
	// 5055 код 1: другая карта, адрес и тайл как у телепорта Player52 (@@ASTORY_SERVER_WAN_IP@@:15601, (393,397)).
	static TArray<uint8> Body5055(int32 Map)
	{
		TArray<uint8> B; B.SetNumZeroed(53);
		B[0] = 1;
		const char* Ip = "@@ASTORY_SERVER_WAN_IP@@";
		for (int32 i = 0; Ip[i]; ++i) { B[1 + i] = (uint8)Ip[i]; }
		B[21] = 0xF1; B[22] = 0x3C;                                   // 15601
		B[23] = (uint8)(Map & 0xFF); B[24] = (uint8)(Map >> 8);
		B[25] = 0x89; B[26] = 0x01; B[27] = 0x8D; B[28] = 0x01;       // (393, 397)
		B[51] = 0xFF; B[52] = 0xFF;
		return B;
	}
}

// Друг `UAsdaNetSubsystem` (AsdaNetSubsystem.h): нужны `Conn` и `HandleConnectionClosed`.
struct FAsdaPortalPendingSelfTest
{
	// Состояние прогона (живёт на таймерах мира).
	struct FRun
	{
		TWeakObjectPtr<UWorld> W;               // мир прогона
		bool bQuit = false;                     // выйти после итога
		int32 Ok = 0, Fail = 0;                 // счёт проверок
		AsdaPortalPendingTestLocal::FSink Sink; // перехват строк журнала
		TUniquePtr<FAsdaNetSelfScope> SelfScope;// свой персонаж (charId) — вернуть в конце
		TUniquePtr<FAsdaWorldSelfScope> WorldScope;   // ★ 31.10.2026: свой павн и питомцы мира — вернуть в конце (сбросы сессии обнуляют ссылку, 6106 призывает «Dogi»)
		TUniquePtr<FAsdaCVarScope> Keys;        // ключи — вернуть в конце
		// Снимок полей сети (вернуть в конце).
		int32 Map = -1, TX = -1, TZ = -1, PTX = -1, PTZ = -1, Port = 0, ClassId = -1;
		FString Ip;
		bool bTransfer = false, bPending = false, bPortal = false, bInGame = false, bHud = false;
		uint8 T1 = 0xFF, T2 = 0xFF;
		// Перехватчик снимается и при досрочном уничтожении прогона (мир ушёл с таймерами) — висячего устройства в GLog нет.
		~FRun() { if (GLog) { GLog->RemoveOutputDevice(&Sink); } }
		void Expect(bool b, const FString& What)
		{
			if (b) { ++Ok; UE_LOG(LogAsdaPortalPendingTest, Log, TEXT("ПОРТАЛ-ВХОД ok   %s"), *What); }
			else   { ++Fail; UE_LOG(LogAsdaPortalPendingTest, Error, TEXT("ПОРТАЛ-ВХОД FAIL %s"), *What); }
		}
	};

	static UAsdaNetSubsystem* NetOf(const FRun& R)
	{
		UWorld* W = R.W.Get();
		UGameInstance* GI = W ? W->GetGameInstance() : nullptr;
		return GI ? GI->GetSubsystem<UAsdaNetSubsystem>() : nullptr;
	}
	template<class T> static T* Sub(const FRun& R)
	{
		UWorld* W = R.W.Get();
		UGameInstance* GI = W ? W->GetGameInstance() : nullptr;
		return GI ? GI->GetSubsystem<T>() : nullptr;
	}
	// Другая карта, чем текущая (портал обязан пойти реконнектом).
	static int32 OtherMap(const UAsdaNetSubsystem* Net) { return (Net && Net->EnterMapId == 0) ? 3 : 0; }

	// Шаг по таймеру мира.
	static void After(const TSharedRef<FRun>& R, float Sec, TFunction<void(const TSharedRef<FRun>&)> Fn)
	{
		UWorld* W = R->W.Get();
		if (!W) { return; }
		FTimerHandle H;
		W->GetTimerManager().SetTimer(H, FTimerDelegate::CreateLambda([R, Fn = MoveTemp(Fn)]() { Fn(R); }), Sec, false);
	}

	static void Start(UWorld* W, bool bQuit);
	static void StepA(const TSharedRef<FRun>& R);
	static void StepB(const TSharedRef<FRun>& R);
	static void StepBCheck(const TSharedRef<FRun>& R);
	static void StepBControl(const TSharedRef<FRun>& R);
	static void StepC(const TSharedRef<FRun>& R, bool bControl);
	static void StepCCheck(const TSharedRef<FRun>& R, bool bControl);
	static void StepD(const TSharedRef<FRun>& R);
	static void StepDCheck(const TSharedRef<FRun>& R);
	static void StepEF(const TSharedRef<FRun>& R);
	static void Finish(const TSharedRef<FRun>& R);
};

void FAsdaPortalPendingSelfTest::Start(UWorld* W, bool bQuit)
{
	TSharedRef<FRun> R = MakeShared<FRun>();
	R->W = W;
	R->bQuit = bQuit;
	UAsdaNetSubsystem* Net = NetOf(*R);
	if (!Net || !Net->Conn) { AsdaSelfTestKit::Refused(W, AsdaPortalPendingTestLocal::kCmd, bQuit, TEXT("нет сетевой подсистемы")); return; }
	if (Net->IsConnected()) { AsdaSelfTestKit::Refused(W, AsdaPortalPendingTestLocal::kCmd, bQuit, TEXT("живое соединение")); return; }
	GLog->AddOutputDevice(&R->Sink);
	R->SelfScope = MakeUnique<FAsdaNetSelfScope>(Net, AsdaPortalPendingTestLocal::kCmd);
	R->WorldScope = MakeUnique<FAsdaWorldSelfScope>(W, AsdaPortalPendingTestLocal::kCmd);
	R->Keys = MakeUnique<FAsdaCVarScope>(W, AsdaPortalPendingTestLocal::kCmd);
	R->Keys->Set(TEXT("asda.Net.PortalPendingReset"), 1);
	R->Keys->Set(TEXT("asda.Net.GameConnectFailDisconnect"), 1);
	R->Keys->Set(TEXT("asda.ItemTip.ShopCardTable2"), 1);
	R->Keys->Set(TEXT("asda.Net.ChannelTransferPortalLogon"), 1);   // ★ 26.10.2026: A6 — 6015 ведёт к 5058, как у оригинала
	R->Map = Net->EnterMapId; R->TX = Net->SelfTileX; R->TZ = Net->SelfTileZ;
	R->PTX = Net->PortalTileX; R->PTZ = Net->PortalTileZ; R->Ip = Net->GameServerIP; R->Port = Net->GameServerPort;
	R->bTransfer = Net->bTransferInProgress; R->bPending = Net->bPendingEnterWorld; R->bPortal = Net->bPortalPending;
	R->bInGame = Net->bInGameConnected; R->bHud = Net->bCampWarHudSuppressed; R->T1 = Net->CampWarToken1; R->T2 = Net->CampWarToken2;
	R->ClassId = Net->SelfClassId;
	Net->SelectedCharId = 75230;   // charId из журнала Player52 — адресат 6106 (вернёт FAsdaNetSelfScope)
	StepA(R);
}

// A. Переход взводится; MapChanged его не трогает; разрыв и вход из выбора персонажа снимают.
void FAsdaPortalPendingSelfTest::StepA(const TSharedRef<FRun>& R)
{
	UAsdaNetSubsystem* Net = NetOf(*R);
	if (!Net) { Finish(R); return; }
	const int32 Dst = OtherMap(Net);
	Net->DebugFeedPacket(5055, AsdaPortalPendingTestLocal::Body5055(Dst));
	R->Expect(Net->bPortalPending && Net->bTransferInProgress && Net->bPendingEnterWorld && Net->PortalTileX == 393 && Net->PortalTileZ == 397,
		FString::Printf(TEXT("A1 5055 на карту %d: переход взведён (5058=%d, гейт=%d, реконнект=%d, тайл %d,%d)"), Dst,
			(int32)Net->bPortalPending, (int32)Net->bTransferInProgress, (int32)Net->bPendingEnterWorld, Net->PortalTileX, Net->PortalTileZ));
	Net->ResetSessionState(EAsdaSessionResetReason::MapChanged);
	R->Expect(Net->bPortalPending && Net->PortalTileX == 393,
		TEXT("A2 контроль: сброс «смена карты» признак портального входа НЕ снимает (5058 уходит после загрузки новой карты)"));
	Net->HandleConnectionClosed();   // разрыв во время перехода (тот же путь, что FIN/RST сервера)
	R->Expect(!Net->bPortalPending && !Net->bPendingEnterWorld && !Net->bTransferInProgress && Net->PortalTileX == -1 && Net->PortalTileZ == -1,
		FString::Printf(TEXT("A3 разрыв во время перехода: признаки сняты (5058=%d, реконнект=%d, гейт=%d, тайл %d,%d) — после перелогина уйдёт 4001"),
			(int32)Net->bPortalPending, (int32)Net->bPendingEnterWorld, (int32)Net->bTransferInProgress, Net->PortalTileX, Net->PortalTileZ));
	// Вход из выбора персонажа (1021 → EnteringWorld) — признак, оставшийся от прошлой сессии, снимается.
	Net->bPortalPending = true; Net->PortalTileX = 10; Net->PortalTileZ = 20;
	Net->ResetSessionState(EAsdaSessionResetReason::EnteringWorld);
	R->Expect(!Net->bPortalPending && Net->PortalTileX == -1, TEXT("A4 вход из выбора персонажа (EnteringWorld): признак 5058 снят — логон 4001"));
	Net->bPortalPending = true; Net->PortalTileX = 10; Net->PortalTileZ = 20;
	Net->ResetSessionState(EAsdaSessionResetReason::LeftWorld);
	R->Expect(!Net->bPortalPending && Net->PortalTileX == -1, TEXT("A5 выход в выбор персонажа (LeftWorld): признак 5058 снят"));
	// Смена канала 6015 (netHandleChannelTransfer): ★ 26.10.2026 — как у оригинала, состояние 11 и портальный вход 5058 с клеткой
	//   из пакета (+18/+20; ключ asda.Net.ChannelTransferPortalLogon); разрыв снимает и его.
	{
		const int32 ChWas = Net->CurrentChannel;
		const uint8 ParWas = Net->PortalChannelParam;
		TArray<uint8> B; B.SetNumZeroed(26);
		const char* Ip = "@@ASTORY_SERVER_WAN_IP@@";
		for (int32 i = 0; Ip[i]; ++i) { B[i] = (uint8)Ip[i]; }
		B[16] = 0xF1; B[17] = 0x3C;                                         // порт 15601
		B[18] = 0xBD; B[19] = 0x01; B[20] = 0x9D; B[21] = 0x00;             // клетка (445,157) — как в журнале Meet 21.10
		B[22] = (uint8)(R->Map >= 0 ? R->Map : 0); B[24] = 1; B[25] = 0xFF;  // та же карта, канал 2
		Net->DebugFeedPacket(6015, B);
		R->Expect(Net->bTransferInProgress && Net->bPendingEnterWorld && Net->bPortalPending && Net->PortalTileX == 445 && Net->PortalTileZ == 157,
			FString::Printf(TEXT("A6 смена канала 6015: реконнект взведён, после загрузки — портальный 5058 с клеткой (445,157) "
				"(гейт=%d, реконнект=%d, 5058=%d, клетка=(%d,%d))"),
				(int32)Net->bTransferInProgress, (int32)Net->bPendingEnterWorld, (int32)Net->bPortalPending, Net->PortalTileX, Net->PortalTileZ));
		Net->HandleConnectionClosed();
		R->Expect(!Net->bTransferInProgress && !Net->bPendingEnterWorld && !Net->bPortalPending && Net->PortalTileX == -1,
			TEXT("A7 разрыв во время смены канала: гейт, отложенный реконнект и признак 5058 сняты (после обрыва вход обычный 4001)"));
		Net->CurrentChannel = ChWas;
		Net->PortalChannelParam = ParWas;
	}
	StepB(R);
}

// B. Страховка 30 с: без соединения признак снимается; контроль — идёт подключение.
void FAsdaPortalPendingSelfTest::StepB(const TSharedRef<FRun>& R)
{
	UAsdaNetSubsystem* Net = NetOf(*R);
	if (!Net) { Finish(R); return; }
	Net->bPortalPending = true; Net->PortalTileX = 393; Net->PortalTileZ = 397;
	Net->bTransferInProgress = true; Net->TransferGuardSec = 29.99f; Net->bAwaitGameConnect = false;
	After(R, 0.3f, [](const TSharedRef<FRun>& R)
	{
		UAsdaNetSubsystem* Net = NetOf(*R);
		if (!Net) { Finish(R); return; }
		R->Expect(!Net->bTransferInProgress && !Net->bPortalPending && Net->PortalTileX == -1,
			FString::Printf(TEXT("B1 страховка 30 с без соединения: гейт и признак 5058 сняты (гейт=%d, 5058=%d)"), (int32)Net->bTransferInProgress, (int32)Net->bPortalPending));
		// Подключение ещё идёт — ★ 26.10.2026: страховка не считает (её 30 с — от исхода подключения), признак остаётся.
		R->Keys->Set(TEXT("asda.Net.TransferGuardAfterConnect"), 1);
		Net->bPortalPending = true; Net->PortalTileX = 393; Net->PortalTileZ = 397;
		Net->bTransferInProgress = true; Net->TransferGuardSec = 29.99f;
		Net->bAwaitGameConnect = true; Net->AwaitGameConnectSec = 0.f;
		After(R, 0.3f, [](const TSharedRef<FRun>& R) { StepBCheck(R); });
	});
}

void FAsdaPortalPendingSelfTest::StepBCheck(const TSharedRef<FRun>& R)
{
	UAsdaNetSubsystem* Net = NetOf(*R);
	if (!Net) { Finish(R); return; }
	R->Expect(Net->bTransferInProgress && Net->bPortalPending && Net->PortalTileX == 393 && Net->TransferGuardSec < 1.f,
		FString::Printf(TEXT("B2 подключение идёт (до 20 с): страховка не считает — гейт держится, признак 5058 сохранён, отсчёт %.2f с (гейт=%d, 5058=%d)"),
			Net->TransferGuardSec, (int32)Net->bTransferInProgress, (int32)Net->bPortalPending));
	// Контроль: ключ 0 — прежнее (30 с от взвода, при идущем подключении гейт снимается, признак остаётся).
	R->Keys->Set(TEXT("asda.Net.TransferGuardAfterConnect"), 0);
	Net->bTransferInProgress = true; Net->TransferGuardSec = 29.99f;
	After(R, 0.3f, [](const TSharedRef<FRun>& R) { StepBControl(R); });
}

void FAsdaPortalPendingSelfTest::StepBControl(const TSharedRef<FRun>& R)
{
	UAsdaNetSubsystem* Net = NetOf(*R);
	if (!Net) { Finish(R); return; }
	R->Expect(!Net->bTransferInProgress && Net->bPortalPending && Net->PortalTileX == 393,
		FString::Printf(TEXT("B3 контроль (ключ asda.Net.TransferGuardAfterConnect 0): страховка 30 с при идущем подключении — гейт снят, признак 5058 сохранён (5058=%d)"),
			(int32)Net->bPortalPending));
	R->Keys->Set(TEXT("asda.Net.TransferGuardAfterConnect"), 1);
	Net->bTransferInProgress = false; Net->TransferGuardSec = 0.f;
	Net->bAwaitGameConnect = false; Net->AwaitGameConnectSec = 0.f;
	Net->bPortalPending = false; Net->PortalTileX = -1; Net->PortalTileZ = -1;
	StepC(R, /*bControl*/ true);
}

// C. Неудачное подключение после 5055: срок истёк → разрыв с нотисом 1001 (контроль: ключ 0 — прежнее молчание).
void FAsdaPortalPendingSelfTest::StepC(const TSharedRef<FRun>& R, bool bControl)
{
	UAsdaNetSubsystem* Net = NetOf(*R);
	UAsdaDisconnectPopupSubsystem* Pop = Sub<UAsdaDisconnectPopupSubsystem>(*R);
	if (!Net || !Pop) { R->Expect(false, TEXT("C нет подсистемы попапа")); Finish(R); return; }
	R->Keys->Set(TEXT("asda.Net.GameConnectFailDisconnect"), bControl ? 0 : 1);
	if (Pop->IsOpen()) { Pop->ClosePopup(); }
	Net->DebugFeedPacket(5055, AsdaPortalPendingTestLocal::Body5055(OtherMap(Net)));
	// То, что сделал бы EnterGameWorld следующим кадром: старое соединение закрыто, подключение начато и ждёт ответа.
	Net->bPendingEnterWorld = false;
	Net->bAwaitGameConnect = true; Net->AwaitGameConnectSec = 0.f;
	Net->Conn->DebugBeginStalledConnect(0.25f);   // «сервер не отвечает» — без сокета, по сроку OnClosed
	R->Sink.Disc.Reset();
	After(R, 0.9f, [bControl](const TSharedRef<FRun>& R) { StepCCheck(R, bControl); });
}

void FAsdaPortalPendingSelfTest::StepCCheck(const TSharedRef<FRun>& R, bool bControl)
{
	UAsdaNetSubsystem* Net = NetOf(*R);
	UAsdaDisconnectPopupSubsystem* Pop = Sub<UAsdaDisconnectPopupSubsystem>(*R);
	UAsdaWindowSubsystem* WM = Sub<UAsdaWindowSubsystem>(*R);
	if (!Net || !Pop) { Finish(R); return; }
	GLog->Flush();
	if (bControl)
	{
		R->Expect(!Pop->IsOpen() && !Net->bAwaitGameConnect,
			FString::Printf(TEXT("C0 контроль (ключ asda.Net.GameConnectFailDisconnect 0): неудача подключения без попапа — прежнее поведение (попап=%d)"), (int32)Pop->IsOpen()));
		// Прежнее поведение оставляло признаки — вернуть «чистое» состояние для основной проверки.
		Net->bTransferInProgress = false; Net->bPortalPending = false; Net->PortalTileX = -1; Net->PortalTileZ = -1;
		StepC(R, /*bControl*/ false);
		return;
	}
	FString Text;
	if (UUserWidget* Box = WM ? WM->FindWindow(UAsdaDisconnectPopupSubsystem::kWindowId) : nullptr)
	{
		if (UTextBlock* T = Cast<UTextBlock>(Box->GetWidgetFromName(TEXT("COMMONWIN_TEXT")))) { Text = T->GetText().ToString(); }
	}
	UGameInstance* GI = R->W.IsValid() ? R->W->GetGameInstance() : nullptr;
	const FString Want = AsdaNotice::Loc(GI, 1001);
	const bool bNotice = R->Sink.Disc.ContainsByPredicate([](const FString& S) { return S.Contains(TEXT("нотис 1001")); });
	R->Expect(Pop->IsOpen() && bNotice && !Want.IsEmpty() && Text.Contains(Want.Left(12)),
		FString::Printf(TEXT("C1 неудача подключения в мире: попап 8000 с нотисом 1001 «%s» (открыт=%d, строка журнала=%d, текст «%s»)"),
			*Want, (int32)Pop->IsOpen(), (int32)bNotice, *Text.Replace(TEXT("\n"), TEXT(" / "))));
	R->Expect(Net->GetLastDisconnectReason() == 4 && Pop->GetNextNotice() == 0,
		FString::Printf(TEXT("C2 причина разрыва 4 (эталон g_nDisconnectReason = 4), заданный текст израсходован (причина=%d)"), Net->GetLastDisconnectReason()));
	R->Expect(!Net->bPortalPending && !Net->bTransferInProgress && !Net->bAwaitGameConnect && !Net->bPendingEnterWorld && Net->PortalTileX == -1,
		FString::Printf(TEXT("C3 после неудачи признаки перехода сняты (5058=%d, гейт=%d, ожидание=%d) — перелогин пойдёт 4001"),
			(int32)Net->bPortalPending, (int32)Net->bTransferInProgress, (int32)Net->bAwaitGameConnect));
	Pop->ClosePopup();   // ОК не жмём — он увёл бы на экран входа (это проверяет asda.SelfTest.DisconnectOk)
	StepD(R);
}

// D. «Перелогин»: на 4001 сервер шлёт 6057 + 5251 + 6106 — навыки и питомец попадают в модели окон.
void FAsdaPortalPendingSelfTest::StepD(const TSharedRef<FRun>& R)
{
	UAsdaNetSubsystem* Net = NetOf(*R);
	if (!Net) { Finish(R); return; }
	R->Expect(!Net->bPortalPending, TEXT("D0 решение Load Manager после загрузки: bPortalPending=0 → SendCharLogonReq (4001), не 5058"));
	Net->DebugFeedPacket(6057, TArray<uint8>());
	Net->DebugFeedPacket(5251, AsdaPortalPendingTestLocal::Body5251());
	Net->DebugFeedPacket(6106, AsdaPortalPendingTestLocal::Body6106());
	After(R, 0.4f, [](const TSharedRef<FRun>& R) { StepDCheck(R); });
}

void FAsdaPortalPendingSelfTest::StepDCheck(const TSharedRef<FRun>& R)
{
	const UAsdaSkillWindowSubsystem* Sk = Sub<UAsdaSkillWindowSubsystem>(*R);
	const UAsdaPetSubsystem* Pet = Sub<UAsdaPetSubsystem>(*R);
	R->Expect(Sk && Sk->FindLearned(701) != nullptr, TEXT("D1 5251 (ответ на 4001): навык 701 в модели окна навыков"));
	const bool bDogi = Pet && Pet->GetSlots().ContainsByPredicate([](const FAsdaPetSlot& S) { return S.UniqueId == 176714 && S.Name == TEXT("Dogi"); });
	R->Expect(bDogi, FString::Printf(TEXT("D2 6106 (ответ на 4001): питомец 176714 «Dogi» в списке (записей %d)"), Pet ? Pet->GetSlots().Num() : -1));
	StepEF(R);
}

// E. 12854 с названием ветки; F. подсказка карточки премиум-магазина для 196.
void FAsdaPortalPendingSelfTest::StepEF(const TSharedRef<FRun>& R)
{
	UAsdaNetSubsystem* Net = NetOf(*R);
	UGameInstance* GI = R->W.IsValid() ? R->W->GetGameInstance() : nullptr;
	if (!Net || !GI) { Finish(R); return; }
	Net->SelfClassId = 1;   // полоса 1 (класс 1..11) — названия веток 9400..9402
	// Оракул — СЫРЫЕ строки таблицы (маркер [ss1] на месте), а не проверяемая подстановка.
	const UAsdaTextSubsystem* TS = GI->GetSubsystem<UAsdaTextSubsystem>();
	const FString Tpl = TS ? TS->GetTextById(12854) : FString();
	const FString Tree = TS ? TS->GetTextById(9400) : FString();
	auto Feed5464 = [&](uint8 Grade) -> FString
	{
		GLog->Flush();
		R->Sink.SkillMsg.Reset();
		TArray<uint8> B; B.SetNumZeroed(3); B[0] = 2; B[2] = Grade;   // очки 2, ветка Grade (как в журнале Player52 22.10)
		Net->DebugFeedPacket(5464, B);
		GLog->Flush();
		return R->Sink.SkillMsg.Num() > 0 ? R->Sink.SkillMsg.Last() : FString();
	};
	const FString M1 = Feed5464(1);
	const FString Want1 = Tpl.Replace(TEXT("[ss1]"), *Tree, ESearchCase::CaseSensitive);
	R->Expect(!Tree.IsEmpty() && Tpl.Contains(TEXT("[ss1]")) && M1 == Want1 && !M1.Contains(TEXT("[]")),
		FString::Printf(TEXT("E1 5464 ветка 1: «%s» (ожидание «%s»)"), *M1, *Want1));
	const FString M0 = Feed5464(0);
	R->Expect(M0.Contains(TEXT("[]")),
		FString::Printf(TEXT("E2 контроль: 5464 ветка 0 — пустой слот «[]», как у оригинала (skillUiOnLearnSkill_6590): «%s»"), *M0));
	if (UAsdaWindowSubsystem* WM = GI->GetSubsystem<UAsdaWindowSubsystem>()) { WM->ClearMsgBoxQueue(TEXT("самотест PortalPending")); }

	UAsdaItemTooltipSubsystem* Tip = GI->GetSubsystem<UAsdaItemTooltipSubsystem>();
	UAsdaItemTableSubsystem* IT = GI->GetSubsystem<UAsdaItemTableSubsystem>();
	if (Tip && IT)
	{
		R->Expect(IT->IsSpecialItemCode(196) && IT->IsSpecialItemCode(197) && IT->IsSpecialItemCode(470),
			TEXT("F0 196/197/470 есть в клиентской второй таблице (special_itemlist)"));
		Tip->HideTooltip();
		Tip->ShowShopSlotTooltip(196, 1, /*bSellMode*/ false, EAsdaTipOwner::PremiumShop);
		R->Expect(Tip->IsTooltipShown() && Tip->GetTipOwner() == EAsdaTipOwner::PremiumShop,
			FString::Printf(TEXT("F1 подсказка карточки премиум-магазина: свиток 196 «%s» показан"), *IT->GetItemName(196)));
		Tip->HideTooltip();
		Tip->ShowShopSlotTooltip(196, 1, false, EAsdaTipOwner::EquipShop);
		R->Expect(!Tip->IsTooltipShown(), TEXT("F2 контроль: витрина NPC для кода второй таблицы — подсказки нет, как прежде"));
		Tip->HideTooltip();
		R->Keys->Set(TEXT("asda.ItemTip.ShopCardTable2"), 0);
		Tip->ShowShopSlotTooltip(196, 1, false, EAsdaTipOwner::PremiumShop);
		R->Expect(!Tip->IsTooltipShown(), TEXT("F3 контроль: ключ asda.ItemTip.ShopCardTable2 0 — подсказки нет (прежнее поведение)"));
		Tip->HideTooltip();
	}
	else
	{
		R->Expect(false, TEXT("F нет подсистемы подсказок или таблиц"));
	}
	Finish(R);
}

void FAsdaPortalPendingSelfTest::Finish(const TSharedRef<FRun>& R)
{
	if (UAsdaNetSubsystem* Net = NetOf(*R))
	{
		Net->EnterMapId = R->Map; Net->SelfTileX = R->TX; Net->SelfTileZ = R->TZ;
		Net->PortalTileX = R->PTX; Net->PortalTileZ = R->PTZ; Net->GameServerIP = R->Ip; Net->GameServerPort = R->Port;
		Net->bTransferInProgress = R->bTransfer; Net->bPendingEnterWorld = R->bPending; Net->bPortalPending = R->bPortal;
		Net->bInGameConnected = R->bInGame; Net->bCampWarHudSuppressed = R->bHud; Net->CampWarToken1 = R->T1; Net->CampWarToken2 = R->T2;
		Net->SelfClassId = R->ClassId;
		Net->bAwaitGameConnect = false; Net->AwaitGameConnectSec = 0.f;
	}
	if (R->SelfScope) { R->SelfScope->Restore(); }
	if (R->WorldScope) { R->WorldScope->Restore(); }
	if (R->Keys) { R->Keys->Restore(); }
	GLog->Flush();
	GLog->RemoveOutputDevice(&R->Sink);
	AsdaSelfTestKit::Finish(R->W.Get(), AsdaPortalPendingTestLocal::kCmd, R->bQuit, LogAsdaPortalPendingTest, TEXT("ПОРТАЛ-ВХОД"), R->Ok, R->Fail);
}

static FAutoConsoleCommandWithWorldAndArgs GAsdaSelfTestPortalPending(
	TEXT("asda.SelfTest.PortalPending"),
	TEXT("Самотест: портальный вход 5058 не переживает разрыв/выход/вход из выбора персонажа/страховку 30 с; неудача подключения "
	     "к game-серверу = попап 1001; навыки и питомец после перелогина; 12854 с веткой; подсказка свитка 196. Офлайн. `delay=N` `quit`."),
	FConsoleCommandWithWorldAndArgsDelegate::CreateLambda([](const TArray<FString>& Args, UWorld* W)
	{
		const FAsdaSelfTestArgs A = FAsdaSelfTestArgs::Parse(Args);
		if (AsdaSelfTestKit::RefuseStart(W, AsdaPortalPendingTestLocal::kCmd, A)) { return; }
		if (!W) { return; }
		if (A.Delay <= 0.f) { FAsdaPortalPendingSelfTest::Start(W, A.bQuit); return; }
		TWeakObjectPtr<UWorld> WW = W;
		const bool bQuit = A.bQuit;
		FTimerHandle H;
		W->GetTimerManager().SetTimer(H, FTimerDelegate::CreateLambda([WW, bQuit]()
		{
			if (WW.Get() && AsdaSelfTestKit::RefuseDeferred(WW.Get(), AsdaPortalPendingTestLocal::kCmd, bQuit)) { return; }
			if (UWorld* Wd = WW.Get()) { FAsdaPortalPendingSelfTest::Start(Wd, bQuit); }
		}), A.Delay, false);
	}));

#endif // ASDA_DEV_CONSOLE && !UE_BUILD_SHIPPING
