// Copyright (c) 2026 @sitxovski (Discord)
// ★★★ САМОТЕСТ «СМЕНА КАНАЛА В ИГРЕ ПУТЁМ ИГРОКА» (26.10.2026, жалоба игроков «канал в игре не меняется, только перезаходом»). [ПОРТ]
//
// ★ ЗАЧЕМ. Поток эталона: кнопка 17500 над мини-картой → панель 17501 → строка 17503+i → гейты (`MiniMap_BigMapWindowProc@0xD65210`,
//   world_map.c:7491-7585) → 6084 (`netSec_SendChallenge6084@0xF46A90`, 1 б — канал 0-based) → сервер `Recv_UserChannelMove@0x54FF80`
//   (GameServer.c:128733) → 6016/6017 через RelayServer → `sub_550560` → 6015 (26 б) → `netHandleChannelTransfer@0xF49660`
//   (network.c:43886) — состояние 11, как у портала: реконнект → 20000 → 6004 → загрузка ТОЙ ЖЕ карты → 5058 с клеткой из 6015
//   (`net_sendSpawnFieldSelect@0xF489E0`, game_main.c:3607). Сервер на 5058 не присылает навыки/питомцев (5251/6106) — окна обязаны
//   пережить переход сами.
//   Исправления 26.10: гейты и нотисы (ключ asda.Minimap.ChannelGates), 6015 → 5058 (asda.Net.ChannelTransferPortalLogon).
//
// ★ КАК ПРОВЕРЯЕТ (реальный путь, без сети — закон diagnostic-must-use-real-path):
//   A. Клик по кнопке 17500 (`Change_Channel_Btn`, OnClicked) — панель видна, строк ровно 3 («1 CH».«3 CH»).
//   B. Гейты кликом по строке: тот же канал → 98003 (чат + окно-сообщение, 6084 нет); особая карта 4 → 98001 (строка) и карта 8 →
//      98001 (кнопка, панель не раскрылась); подземелье (байт +68 записи карты, поднят на время на карте 6) → 98002; контроль ключом 0 — 6084 уходит, нотиса нет.
//   C. Клик «2 CH» → перехвачен 6084 (`SendSecured`): нагрузка 1 б — канал 1; панель закрылась.
//   D. Ответ 6015 (адрес @@ASTORY_SERVER_WAN_IP@@:15601, та же карта, клетка (393,397), канал 1) через штатный приёмник: канал 2 в подписи,
//      гейт перехода и реконнект взведены, признак 5058 и клетка; на следующем тике — реконнект (офлайн — без сокета).
//   E. 6004 (успех входа) → запрошена загрузка ТОЙ ЖЕ карты (сама загрузка не выполняется — прогон остаётся на карте);
//      сброс «смена карты» (как после загрузки уровня) → решение Load Manager: 5058 с клеткой (393,397), а навык 701 и питомец «Dogi»
//      в моделях окон сохранены (окна не пустые). Контроль: ключ asda.Net.ChannelTransferPortalLogon 0 — после 6015 вход 4001.
//   Состояние подсистемы сети, ключи и перехват возвращаются в конце.
//
// Консоль: `asda.SelfTest.ChannelMove [delay=N] [quit]`. Только офлайн (-AsdaNoNet), карта мира. Строки — «КАНАЛ-ИГРА»,
//   итог — «КАНАЛ-ИГРА ИТОГ: ok=… fail=…».
#include "CoreMinimal.h"
#include "AsdaDevConsole.h"

#if ASDA_DEV_CONSOLE && !UE_BUILD_SHIPPING

#include "AsdaNetSubsystem.h"
#include "AsdaSessionReset.h"
#include "Test/AsdaSelfTestKit.h"
#include "UI/AsdaMinimapSubsystem.h"
#include "UI/AsdaWindowSubsystem.h"
#include "UI/AsdaSkillWindowSubsystem.h"
#include "UI/AsdaNotice.h"
#include "Pet/AsdaPetSubsystem.h"
#include "Test/AsdaSelfTestWorldScope.h"   // ★ 31.10.2026: свой павн и питомцы мира — вернуть в конце (сбросы сессии обнуляют ссылку, 6106 призывает «Dogi»)
#include "World/AsdaLoadManagerSubsystem.h"
#include "World/AsdaMapTable.h"
#include "Blueprint/UserWidget.h"
#include "Blueprint/WidgetTree.h"
#include "Components/Button.h"
#include "Components/TextBlock.h"
#include "Engine/Engine.h"
#include "Engine/GameInstance.h"
#include "Engine/World.h"
#include "HAL/IConsoleManager.h"
#include "Misc/OutputDevice.h"
#include "Misc/OutputDeviceRedirector.h"
#include "TimerManager.h"
#include "UObject/UObjectIterator.h"

DEFINE_LOG_CATEGORY_STATIC(LogAsdaChannelMoveTest, Log, All);

namespace AsdaChannelMoveTestLocal
{
	// Имя команды (строка итога, отказ, возврат ключей).
	static const TCHAR* kCmd = TEXT("asda.SelfTest.ChannelMove");

	// Перехватчик строк журнала: системный чат маршрутизатора (LogAsdaMsg) — для проверки нотисов отказа.
	class FSink : public FOutputDevice
	{
	public:
		TArray<FString> Msg;   // строки маршрутизатора сообщений
		virtual void Serialize(const TCHAR* V, ELogVerbosity::Type, const FName& Category) override
		{
			if (!IsInGameThread()) { return; }   // ★ 05.11.2026: устройство синхронное (CanBeUsedOnMultipleThreads) — ловим только строки игрового потока: их пишет проверяемый код, читает самотест, замка у полей нет
			static const FName kMsg(TEXT("LogAsdaMsg"));
			if (V && Category == kMsg) { Msg.Add(FString(V)); }
		}
		// Пишут и с чужих потоков — приёмник только копит строки.
		virtual bool CanBeUsedOnAnyThread() const override { return true; }
		virtual bool CanBeUsedOnMultipleThreads() const override { return true; }   // ★ 05.11.2026: НЕбуферизованный список UE 5.8 — строка доходит в момент записи (иначе гонка с потоком логов: строка после RemoveOutputDevice/проверки)
	};

	// Байты из журнала Player52 25.10.2026 16:52:00 (вход 4001): первая запись 5251 (навык 701) и 6106 (питомец Dogi, 51 б) —
	//   те же, что у asda.SelfTest.PortalPending.
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
	// 6015 (26 б, сервер `sub_550560`): IP[16], порт +16, клетка +18/+20, карта +22, канал +24, Threadidx +25.
	static TArray<uint8> Body6015(int32 Map, uint8 Channel)
	{
		TArray<uint8> B; B.SetNumZeroed(26);
		const char* Ip = "@@ASTORY_SERVER_WAN_IP@@";
		for (int32 i = 0; Ip[i]; ++i) { B[i] = (uint8)Ip[i]; }
		B[16] = 0xF1; B[17] = 0x3C;                       // 15601
		B[18] = 0x89; B[19] = 0x01; B[20] = 0x8D; B[21] = 0x01;   // (393, 397)
		B[22] = (uint8)(Map & 0xFF); B[23] = (uint8)(Map >> 8);
		B[24] = Channel; B[25] = 0;
		return B;
	}
}

// Прогон самотеста (живёт на таймерах мира).
struct FAsdaChannelMoveSelfTest
{
	// Состояние прогона.
	struct FRun
	{
		TWeakObjectPtr<UWorld> W;                // мир прогона
		bool bQuit = false;                      // выйти после итога
		int32 Ok = 0, Fail = 0;                  // счёт проверок
		AsdaChannelMoveTestLocal::FSink Sink;    // перехват строк журнала
		TUniquePtr<FAsdaNetSelfScope> SelfScope; // свой персонаж (charId) — вернуть в конце
		TUniquePtr<FAsdaWorldSelfScope> WorldScope;   // ★ 31.10.2026: свой павн и питомцы мира — вернуть в конце (сбросы сессии обнуляют ссылку, 6106 призывает «Dogi»)
		TUniquePtr<FAsdaCVarScope> Keys;         // ключи — вернуть в конце
		uint32 CapGen = 0;                       // поколение перехвата отправок (0 — не взведён)
		TArray<TArray<uint8>> Sent6084;          // перехваченные тела 6084
		int32 WaitTries = 0;                     // опросов «окно мини-карты уже создано» (павн и сетка карты появляются не сразу)
		// Снимок полей сети (вернуть в конце).
		int32 Map = -1, TX = -1, TZ = -1, PTX = -1, PTZ = -1, Port = 0, ChCount = 0, Ch = 0;
		FString Ip;
		bool bTransfer = false, bPending = false, bPortal = false, bInGame = false;
		uint8 Param = 0xFF;
		EAsdaNetState State = EAsdaNetState::Idle;
		~FRun() { if (GLog) { GLog->RemoveOutputDevice(&Sink); } }
		void Expect(bool b, const FString& What)
		{
			if (b) { ++Ok; UE_LOG(LogAsdaChannelMoveTest, Log, TEXT("КАНАЛ-ИГРА ok   %s"), *What); }
			else   { ++Fail; UE_LOG(LogAsdaChannelMoveTest, Error, TEXT("КАНАЛ-ИГРА FAIL %s"), *What); }
		}
	};

	template<class T> static T* Sub(const FRun& R)
	{
		UWorld* W = R.W.Get();
		UGameInstance* GI = W ? W->GetGameInstance() : nullptr;
		return GI ? GI->GetSubsystem<T>() : nullptr;
	}
	static UAsdaNetSubsystem* NetOf(const FRun& R) { return Sub<UAsdaNetSubsystem>(R); }

	// Шаг по таймеру мира.
	static void After(const TSharedRef<FRun>& R, float Sec, TFunction<void(const TSharedRef<FRun>&)> Fn)
	{
		UWorld* W = R->W.Get();
		if (!W) { return; }
		FTimerHandle H;
		W->GetTimerManager().SetTimer(H, FTimerDelegate::CreateLambda([R, Fn = MoveTemp(Fn)]() { Fn(R); }), Sec, false);
	}

	// Живой виджет мини-карты WBP_MINIMAP_WIN (1702) этого мира — по кнопке 17500 в дереве (подсистема виджет наружу не отдаёт;
	//   сбросы сессии пересоздают окно, поэтому ищем на каждый вызов, а не кэшируем).
	static UUserWidget* FindMini(const UWorld* W)
	{
		for (TObjectIterator<UUserWidget> It; It; ++It)
		{
			UUserWidget* U = *It;
			if (U && U->GetWorld() == W && U->IsInViewport() && U->WidgetTree && U->WidgetTree->FindWidget(FName(TEXT("Change_Channel_Btn")))) { return U; }
		}
		return nullptr;
	}
	// Контрол мини-карты по имени (nullptr — виджета или контрола нет).
	template<class T> static T* Ctl(const FRun& R, const TCHAR* Name)
	{
		UUserWidget* M = FindMini(R.W.Get());
		return (M && M->WidgetTree) ? Cast<T>(M->WidgetTree->FindWidget(FName(Name))) : nullptr;
	}
	static bool Shown(const UWidget* Wd) { return Wd && Wd->GetVisibility() != ESlateVisibility::Collapsed && Wd->GetVisibility() != ESlateVisibility::Hidden; }
	// Клик игрока: OnClicked кнопки (тот же делегат, что зовёт Slate).
	static bool Click(const FRun& R, const TCHAR* Name)
	{
		UButton* B = Ctl<UButton>(R, Name);
		if (!B) { return false; }
		B->OnClicked.Broadcast();
		return true;
	}
	// Текст верхнего окна-сообщения и строки чата после действия; очередь окон после проверки снимается.
	static bool NoticeShown(const FRun& R, int32 LocId, FString& OutWhy)
	{
		UWorld* W = R.W.Get();
		UGameInstance* GI = W ? W->GetGameInstance() : nullptr;
		UAsdaWindowSubsystem* WM = GI ? GI->GetSubsystem<UAsdaWindowSubsystem>() : nullptr;
		const FString Want = GI ? AsdaNotice::Loc(GI, LocId) : FString();
		const FString Key = Want.Left(10);
		GLog->Flush();
		const bool bChat = !Key.IsEmpty() && R.Sink.Msg.ContainsByPredicate([&Key](const FString& S) { return S.Contains(Key); });
		const FString Top = WM ? WM->GetMsgBoxQueueTopText() : FString();
		const bool bBox = !Key.IsEmpty() && Top.Contains(Key);
		OutWhy = FString::Printf(TEXT("«%s»: чат=%d окно=%d"), *Want, (int32)bChat, (int32)bBox);
		if (WM) { WM->ClearMsgBoxQueue(TEXT("самотест ChannelMove")); }
		return bChat && bBox;
	}

	static void Start(UWorld* W, bool bQuit);
	static void WaitMini(const TSharedRef<FRun>& R);
	static void Begin(const TSharedRef<FRun>& R);
	static void StepA(const TSharedRef<FRun>& R);
	static void StepB(const TSharedRef<FRun>& R);
	static void StepC(const TSharedRef<FRun>& R);
	static void StepD(const TSharedRef<FRun>& R);
	static void StepDCheck(const TSharedRef<FRun>& R);
	static void StepE(const TSharedRef<FRun>& R);
	static void Finish(const TSharedRef<FRun>& R);
};

void FAsdaChannelMoveSelfTest::Start(UWorld* W, bool bQuit)
{
	TSharedRef<FRun> R = MakeShared<FRun>();
	R->W = W;
	R->bQuit = bQuit;
	UAsdaNetSubsystem* Net = NetOf(*R);
	if (!Net) { AsdaSelfTestKit::Refused(W, AsdaChannelMoveTestLocal::kCmd, bQuit, TEXT("нет сетевой подсистемы")); return; }
	if (Net->IsConnected()) { AsdaSelfTestKit::Refused(W, AsdaChannelMoveTestLocal::kCmd, bQuit, TEXT("живое соединение")); return; }
	// Офлайн-прогон: окно мини-карты заводит её тик, когда есть номер карты Load Manager, свой павн и сетка карты. Нет павна —
	//   офлайн-спавн штатными крюками (как у asda.Action.SelfTest): карта 3 (Альпия), клетка (256,256).
	if (AsdaSelfTestKit::NeedSelfSpawn(W) && GEngine)
	{
		GEngine->Exec(W, TEXT("Asda.Load.CurrentMap 3"));
		GEngine->Exec(W, TEXT("Asda.World.FakeSelfSpawn 256 256"));
	}
	WaitMini(R);
}

// Окно мини-карты создаёт её тик, когда есть павн и сетка карты, — ждём до 20 с.
void FAsdaChannelMoveSelfTest::WaitMini(const TSharedRef<FRun>& R)
{
	UWorld* W = R->W.Get();
	if (!W) { return; }
	if (!FindMini(W))
	{
		if (++R->WaitTries > 40)
		{
			AsdaSelfTestKit::Refused(W, AsdaChannelMoveTestLocal::kCmd, R->bQuit, TEXT("нет окна мини-карты (Change_Channel_Btn) за 20 с"));
			return;
		}
		After(R, 0.5f, [](const TSharedRef<FRun>& R) { WaitMini(R); });
		return;
	}
	Begin(R);
}

// Снимок состояния, ключи, перехват, наполнение окон — и шаг A.
void FAsdaChannelMoveSelfTest::Begin(const TSharedRef<FRun>& R)
{
	UWorld* W = R->W.Get();
	UAsdaNetSubsystem* Net = NetOf(*R);
	if (!W || !Net) { return; }
	GLog->AddOutputDevice(&R->Sink);
	R->SelfScope = MakeUnique<FAsdaNetSelfScope>(Net, AsdaChannelMoveTestLocal::kCmd);
	R->WorldScope = MakeUnique<FAsdaWorldSelfScope>(W, AsdaChannelMoveTestLocal::kCmd);
	R->Keys = MakeUnique<FAsdaCVarScope>(W, AsdaChannelMoveTestLocal::kCmd);
	R->Keys->Set(TEXT("asda.Minimap.ChannelGates"), 1);
	R->Keys->Set(TEXT("asda.Net.ChannelTransferPortalLogon"), 1);
	R->Keys->Set(TEXT("asda.Net.PortalPendingReset"), 1);
	R->Map = Net->EnterMapId; R->TX = Net->SelfTileX; R->TZ = Net->SelfTileZ;
	R->PTX = Net->PortalTileX; R->PTZ = Net->PortalTileZ; R->Ip = Net->GameServerIP; R->Port = Net->GameServerPort;
	R->ChCount = Net->ChannelCount; R->Ch = Net->CurrentChannel; R->Param = Net->PortalChannelParam;
	R->bTransfer = Net->bTransferInProgress; R->bPending = Net->bPendingEnterWorld; R->bPortal = Net->bPortalPending;
	R->bInGame = Net->bInGameConnected; R->State = Net->State;
	// Перехват ВСЕХ отправок (узкий перехват связь не подменяет — `IsSendLinkUp`); копим только 6084.
	TWeakPtr<FRun> WR = R;
	R->CapGen = AsdaSelfTestKit::ArmCapture(Net, [WR](uint16 Op, const uint8* Body, int32 Len, bool, TCHAR)
	{
		TSharedPtr<FRun> P = WR.Pin();
		if (P && Op == 6084 && Body && Len > 0) { P->Sent6084.Add(TArray<uint8>(Body, Len)); }
	}, 0, AsdaChannelMoveTestLocal::kCmd);
	if (R->CapGen == 0) { R->Expect(false, TEXT("перехват отправок занят другим прогоном")); Finish(R); return; }
	Net->SelectedCharId = 75230;   // charId из журнала Player52 — адресат 6106 (вернёт FAsdaNetSelfScope)
	Net->ChannelCount = 3;         // RU-клиент: три слота каналов (ComputeRealChannels)
	Net->CurrentChannel = 0;
	Net->EnterMapId = 3;           // Альпия — полевая карта, гейтов нет
	// Окна навыков и питомцев наполнены, как после входа 4001 (на 5058 сервер их не присылает).
	Net->DebugFeedPacket(6057, TArray<uint8>());
	Net->DebugFeedPacket(5251, AsdaChannelMoveTestLocal::Body5251());
	Net->DebugFeedPacket(6106, AsdaChannelMoveTestLocal::Body6106());
	After(R, 0.3f, [](const TSharedRef<FRun>& R) { StepA(R); });
}

// A. Кнопка 17500 раскрывает панель 17501: ровно 3 строки «N CH».
void FAsdaChannelMoveSelfTest::StepA(const TSharedRef<FRun>& R)
{
	UAsdaMinimapSubsystem* Mini = Sub<UAsdaMinimapSubsystem>(*R);
	if (!Mini) { R->Expect(false, TEXT("A нет подсистемы мини-карты")); Finish(R); return; }
	Mini->SetChannelPanelOpen(false);
	const bool bClick = Click(*R, TEXT("Change_Channel_Btn"));
	const bool bPanel = Shown(Ctl<UWidget>(*R, TEXT("Change_Channel_Win_grp")));
	R->Expect(bClick && bPanel, FString::Printf(TEXT("A1 клик по 17500: панель каналов 17501 раскрыта (клик=%d, панель=%d)"), (int32)bClick, (int32)bPanel));
	int32 Rows = 0; FString Labels;
	for (int32 i = 0; i < 15; ++i)
	{
		UTextBlock* T = Ctl<UTextBlock>(*R, *FString::Printf(TEXT("Change_Channel%02d"), i));
		if (Shown(T)) { ++Rows; Labels += T->GetText().ToString() + TEXT("|"); }
	}
	R->Expect(Rows == 3 && Labels == TEXT("1 CH|2 CH|3 CH|") && Shown(Ctl<UWidget>(*R, TEXT("Change_Channel01_Click"))),
		FString::Printf(TEXT("A2 строк каналов 3 («%s»), кнопка строки «2 CH» видна"), *Labels));
	StepB(R);
}

// B. Гейты кликом по строке/кнопке и контроль ключом 0.
void FAsdaChannelMoveSelfTest::StepB(const TSharedRef<FRun>& R)
{
	UAsdaNetSubsystem* Net = NetOf(*R);
	UAsdaMinimapSubsystem* Mini = Sub<UAsdaMinimapSubsystem>(*R);
	UAsdaLoadManagerSubsystem* LM = Sub<UAsdaLoadManagerSubsystem>(*R);
	if (!Net || !Mini) { Finish(R); return; }
	FString Why;
	// B1 тот же канал (строка «1 CH» при текущем 0).
	R->Sent6084.Reset(); R->Sink.Msg.Reset();
	Click(*R, TEXT("Change_Channel00_Click"));
	const bool b98003 = NoticeShown(*R, 98003, Why);
	R->Expect(b98003 && R->Sent6084.Num() == 0 && !Shown(Ctl<UWidget>(*R, TEXT("Change_Channel_Win_grp"))),
		FString::Printf(TEXT("B1 «1 CH» при текущем 1: нотис 98003 %s, 6084 нет (%d), панель закрыта"), *Why, R->Sent6084.Num()));
	// B2 особая карта 4 (строка): 98001.
	Net->EnterMapId = 4;
	Mini->SetChannelPanelOpen(true);
	R->Sent6084.Reset(); R->Sink.Msg.Reset();
	Click(*R, TEXT("Change_Channel01_Click"));
	const bool b98001 = NoticeShown(*R, 98001, Why);
	R->Expect(b98001 && R->Sent6084.Num() == 0, FString::Printf(TEXT("B2 карта 4, «2 CH»: нотис 98001 %s, 6084 нет (%d)"), *Why, R->Sent6084.Num()));
	// B3 особая карта 8 (только список кнопки 17500): панель не раскрывается, 98001.
	Net->EnterMapId = 8;
	Mini->SetChannelPanelOpen(false);
	R->Sink.Msg.Reset();
	Click(*R, TEXT("Change_Channel_Btn"));
	const bool b98001b = NoticeShown(*R, 98001, Why);
	R->Expect(b98001b && !Shown(Ctl<UWidget>(*R, TEXT("Change_Channel_Win_grp"))),
		FString::Printf(TEXT("B3 карта 8, кнопка 17500: нотис 98001 %s, панель не раскрыта"), *Why));
	// B4 подземелье (байт +68 записи карты, `FAsdaMapRow::ItemRestrict`). В статической таблице клиента байт +68 = 0 у всех
	//   22 карт (живой дамп, Reports/minimap_re.md §22) — по данным гейт недостижим, как и у оригинала; проверяем код гейта
	//   на записи карты 6 (Солнечный Берег, не из списка особых) с временно поднятым байтом, затем возвращаем.
	FAsdaMapRow* Row6 = LM ? const_cast<FAsdaMapRow*>(LM->GetMapRow(6)) : nullptr;
	if (Row6)
	{
		const int32 Was = Row6->ItemRestrict;
		Row6->ItemRestrict = 1;
		Net->EnterMapId = 6;
		Mini->SetChannelPanelOpen(true);
		R->Sent6084.Reset(); R->Sink.Msg.Reset();
		Click(*R, TEXT("Change_Channel01_Click"));
		const bool b98002 = NoticeShown(*R, 98002, Why);
		Row6->ItemRestrict = Was;
		R->Expect(b98002 && R->Sent6084.Num() == 0, FString::Printf(TEXT("B4 подземелье (карта 6 с байтом +68 = 1): нотис 98002 %s, 6084 нет (%d)"), *Why, R->Sent6084.Num()));
	}
	else
	{
		R->Expect(false, TEXT("B4 нет записи карты 6 в таблице Load Manager"));
	}
	// B5 контроль: ключ 0 — прежнее поведение (на особой карте 6084 уходит, нотиса нет).
	R->Keys->Set(TEXT("asda.Minimap.ChannelGates"), 0);
	Net->EnterMapId = 4;
	Mini->SetChannelPanelOpen(true);
	R->Sent6084.Reset(); R->Sink.Msg.Reset();
	Click(*R, TEXT("Change_Channel01_Click"));
	FString WhyC;
	const bool bNoticeC = NoticeShown(*R, 98001, WhyC);
	R->Expect(!bNoticeC && R->Sent6084.Num() == 1, FString::Printf(TEXT("B5 контроль ключом asda.Minimap.ChannelGates 0: 6084 ушёл (%d), нотиса нет"), R->Sent6084.Num()));
	R->Keys->Set(TEXT("asda.Minimap.ChannelGates"), 1);
	Net->EnterMapId = 3;
	StepC(R);
}

// C. «2 CH» на полевой карте → 6084 с каналом 1.
void FAsdaChannelMoveSelfTest::StepC(const TSharedRef<FRun>& R)
{
	UAsdaMinimapSubsystem* Mini = Sub<UAsdaMinimapSubsystem>(*R);
	if (!Mini) { Finish(R); return; }
	Mini->SetChannelPanelOpen(false);
	Click(*R, TEXT("Change_Channel_Btn"));
	R->Sent6084.Reset();
	Click(*R, TEXT("Change_Channel01_Click"));
	// Перехватчик `SendSecured` получает полезную нагрузку до сборки тела [секрет 8][MD5 16][нагрузка] (общая точка всех защищённых).
	const bool bBody = R->Sent6084.Num() == 1 && R->Sent6084[0].Num() == 1 && R->Sent6084[0][0] == 1;
	R->Expect(bBody, FString::Printf(TEXT("C1 клик «2 CH»: 6084 ушёл через SendSecured (%d), нагрузка 1 б = канал 1 (0-based)"), R->Sent6084.Num()));
	R->Expect(!Shown(Ctl<UWidget>(*R, TEXT("Change_Channel_Win_grp"))), TEXT("C2 панель каналов закрыта сразу после запроса (эталон: переход — по ответу 6015)"));
	StepD(R);
}

// D. Ответ 6015 через штатный приёмник; реконнект на следующем тике.
void FAsdaChannelMoveSelfTest::StepD(const TSharedRef<FRun>& R)
{
	UAsdaNetSubsystem* Net = NetOf(*R);
	if (!Net) { Finish(R); return; }
	Net->DebugFeedPacket(6015, AsdaChannelMoveTestLocal::Body6015(3, 1));
	R->Expect(Net->CurrentChannel == 1 && Net->EnterMapId == 3 && Net->GameServerPort == 15601,
		FString::Printf(TEXT("D1 6015: текущий канал 2, карта прежняя 3, порт 15601 (канал=%d, карта=%d, сервер %s:%d)"),
			Net->CurrentChannel + 1, Net->EnterMapId, *Net->GameServerIP, Net->GameServerPort));
	R->Expect(Net->bTransferInProgress && Net->bPendingEnterWorld && Net->bPortalPending && Net->PortalTileX == 393 && Net->PortalTileZ == 397,
		FString::Printf(TEXT("D2 гейт перехода и реконнект взведены, после загрузки — 5058 с клеткой (393,397) (гейт=%d, реконнект=%d, 5058=%d, клетка=(%d,%d))"),
			(int32)Net->bTransferInProgress, (int32)Net->bPendingEnterWorld, (int32)Net->bPortalPending, Net->PortalTileX, Net->PortalTileZ));
	After(R, 0.6f, [](const TSharedRef<FRun>& R) { StepDCheck(R); });
}

void FAsdaChannelMoveSelfTest::StepDCheck(const TSharedRef<FRun>& R)
{
	UAsdaNetSubsystem* Net = NetOf(*R);
	if (!Net) { Finish(R); return; }
	R->Expect(!Net->bPendingEnterWorld && Net->bPortalPending, TEXT("D3 тик выполнил реконнект (офлайн — без сокета), признак 5058 цел"));
	const UTextBlock* Lbl = Ctl<UTextBlock>(*R, TEXT("MMap_SvrCh"));
	const FString L = Lbl ? Lbl->GetText().ToString() : FString();
	R->Expect(L.Contains(TEXT("2")), FString::Printf(TEXT("D4 подпись 1717 над мини-картой показывает канал 2: «%s»"), *L));
	StepE(R);
}

// E. 6004 → загрузка той же карты; сброс «смена карты»; решение 5058; окна не пустые; контроль ключом 0.
void FAsdaChannelMoveSelfTest::StepE(const TSharedRef<FRun>& R)
{
	UAsdaNetSubsystem* Net = NetOf(*R);
	UAsdaSessionResetSubsystem* SR = Sub<UAsdaSessionResetSubsystem>(*R);
	if (!Net || !SR) { Finish(R); return; }
	TArray<uint8> Ok6004; Ok6004.SetNumZeroed(9); Ok6004[0] = 2; Ok6004[1] = Ok6004[2] = Ok6004[3] = Ok6004[4] = 0xFF;
	Net->DebugFeedPacket(6004, Ok6004);
	const bool bLoad = Net->bPendingMapLoad && Net->PendingMapLoadId == 3;
	Net->bPendingMapLoad = false; Net->PendingMapLoadId = -1;   // саму загрузку не выполняем — прогон остаётся на этой карте
	R->Expect(bLoad && Net->bInGameConnected, TEXT("E1 6004 (успех входа): запрошена загрузка той же карты 3 (выполнение отменено самотестом)"));
	SR->Broadcast(EAsdaSessionResetReason::MapChanged);   // то, что рассылает приезд на уровень
	R->Expect(Net->bPortalPending && Net->PortalTileX == 393 && Net->PortalTileZ == 397,
		TEXT("E2 решение Load Manager после загрузки: bPortalPending=1 → SendCharPortalLogonReq (5058) с клеткой (393,397), не 4001"));
	const UAsdaSkillWindowSubsystem* Sk = Sub<UAsdaSkillWindowSubsystem>(*R);
	const UAsdaPetSubsystem* Pet = Sub<UAsdaPetSubsystem>(*R);
	R->Expect(Sk && Sk->FindLearned(701) != nullptr, TEXT("E3 окно навыков не пустое: навык 701 пережил смену канала (на 5058 сервер 5251 не шлёт)"));
	const bool bDogi = Pet && Pet->GetSlots().ContainsByPredicate([](const FAsdaPetSlot& S) { return S.UniqueId == 176714 && S.Name == TEXT("Dogi"); });
	R->Expect(bDogi, FString::Printf(TEXT("E4 окно питомцев не пустое: «Dogi» пережил смену канала (записей %d; на 5058 сервер 6106 не шлёт)"),
		Pet ? Pet->GetSlots().Num() : -1));
	// Контроль: ключ 0 — прежний порт, после 6015 вход 4001.
	Net->bPortalPending = false; Net->PortalTileX = Net->PortalTileZ = -1;
	R->Keys->Set(TEXT("asda.Net.ChannelTransferPortalLogon"), 0);
	Net->DebugFeedPacket(6015, AsdaChannelMoveTestLocal::Body6015(3, 0));
	R->Expect(!Net->bPortalPending && Net->bPendingEnterWorld && Net->CurrentChannel == 0,
		TEXT("E5 контроль ключом asda.Net.ChannelTransferPortalLogon 0: после 6015 признака 5058 нет — вход 4001 (прежнее поведение)"));
	Net->bPendingEnterWorld = false;
	Finish(R);
}

void FAsdaChannelMoveSelfTest::Finish(const TSharedRef<FRun>& R)
{
	if (UAsdaNetSubsystem* Net = NetOf(*R))
	{
		if (R->CapGen) { AsdaSelfTestKit::DisarmCapture(Net, R->CapGen); R->CapGen = 0; }
		Net->EnterMapId = R->Map; Net->SelfTileX = R->TX; Net->SelfTileZ = R->TZ;
		Net->PortalTileX = R->PTX; Net->PortalTileZ = R->PTZ; Net->GameServerIP = R->Ip; Net->GameServerPort = R->Port;
		Net->ChannelCount = R->ChCount; Net->CurrentChannel = R->Ch; Net->PortalChannelParam = R->Param;
		Net->bTransferInProgress = R->bTransfer; Net->bPendingEnterWorld = R->bPending; Net->bPortalPending = R->bPortal;
		Net->bInGameConnected = R->bInGame; Net->State = R->State;
		Net->bPendingMapLoad = false; Net->PendingMapLoadId = -1;
	}
	if (UAsdaMinimapSubsystem* Mini = Sub<UAsdaMinimapSubsystem>(*R)) { Mini->SetChannelPanelOpen(false); }
	if (UAsdaWindowSubsystem* WM = Sub<UAsdaWindowSubsystem>(*R)) { WM->ClearMsgBoxQueue(TEXT("самотест ChannelMove, итог")); }
	if (R->SelfScope) { R->SelfScope->Restore(); }
	if (R->WorldScope) { R->WorldScope->Restore(); }
	if (R->Keys) { R->Keys->Restore(); }
	GLog->Flush();
	GLog->RemoveOutputDevice(&R->Sink);
	AsdaSelfTestKit::Finish(R->W.Get(), AsdaChannelMoveTestLocal::kCmd, R->bQuit, LogAsdaChannelMoveTest, TEXT("КАНАЛ-ИГРА"), R->Ok, R->Fail);
}

static FAutoConsoleCommandWithWorldAndArgs GAsdaSelfTestChannelMove(
	TEXT("asda.SelfTest.ChannelMove"),
	TEXT("Самотест: смена канала в игре путём игрока — кнопка 17500, строки каналов, гейты 98001/98002/98003, 6084, ответ 6015, "
	     "реконнект, 6004, вход 5058 с клеткой из 6015, окна навыков и питомцев не пустые. Офлайн. `delay=N` `quit`."),
	FConsoleCommandWithWorldAndArgsDelegate::CreateLambda([](const TArray<FString>& Args, UWorld* W)
	{
		const FAsdaSelfTestArgs A = FAsdaSelfTestArgs::Parse(Args);
		if (AsdaSelfTestKit::RefuseStart(W, AsdaChannelMoveTestLocal::kCmd, A)) { return; }
		if (!W) { return; }
		if (A.Delay <= 0.f) { FAsdaChannelMoveSelfTest::Start(W, A.bQuit); return; }
		TWeakObjectPtr<UWorld> WW = W;
		const bool bQuit = A.bQuit;
		FTimerHandle H;
		W->GetTimerManager().SetTimer(H, FTimerDelegate::CreateLambda([WW, bQuit]()
		{
			if (WW.Get() && AsdaSelfTestKit::RefuseDeferred(WW.Get(), AsdaChannelMoveTestLocal::kCmd, bQuit)) { return; }
			if (UWorld* Wd = WW.Get()) { FAsdaChannelMoveSelfTest::Start(Wd, bQuit); }
		}), A.Delay, false);
	}));

#endif // ASDA_DEV_CONSOLE && !UE_BUILD_SHIPPING
