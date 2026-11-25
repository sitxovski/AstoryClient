// Copyright (c) 2026 @sitxovski (Discord)
// ★★★ САМОТЕСТ ОТКАЗОВ ПЕРЕХОДА 5055 / 6201 И ФЛАГА HUD КАМПВАРА (27.09.2026, отчёт `Reports/owner_0927/port_5055.md`). [ПОРТ]
//
// ★ ЗАЧЕМ. Ревизия P40 (`Reports/owner_0927/p40_REVIEW.md`, «Дефект порта») нашла: обработчик 5055 молчал на кодах 8,
//   12 и 13…20 (стоковый отказ «Недостаточно золота» телепорта кристаллом проходил без сообщения), а 0xFF в байтах +51/+52,
//   которые сервер кладёт в каждый стоковый отказ, поднимал флаг подавления боевого HUD кампвара. Исправлено: общая точка
//   отказов `PostPortalRefusalNotice` (5055 и 6201), флаг HUD — только на переходе (код 1), как у эталона.
//
// ★ КАК ПРОВЕРЯЕТ. Пакеты подаются через ШТАТНЫЙ приёмник (`UAsdaNetSubsystem::DebugFeedPacket` → `HandlePacket`), а
//   вывод ловится на стоках окон — по строкам `LogAsdaChat` «[warmsg] …» (плашка WARMESSAGE_WIN 221, эталонный
//   `CControl_SetTooltipText`) и «[сисчат A] …» (журнал, эталонный `CChat_AddTokenizedLines`). Так проверяется весь путь
//   маршрутизатора, а не копия таблицы кодов.
//   Ожидания заданы ЯВНО по эталону (`network.c:42996-43081`, `world_map.c:8473-8499`), а не вызовом проверяемой функции.
//   ★ Контрольные пары (закон pixel-metric-needs-control-pairs):
//     • код 13 даёт нотис у 5055 и ОБЯЗАН молчать у 6201 (у 6201 нет диапазона битвы армий);
//     • коды 0/2 — только плашка, 8/12 — плашка + журнал (проверка различает оба вида);
//     • флаг HUD: отказ с 0xFF не меняет ни false, ни true; переход с 0xFF поднимает, с 0x7F — опускает, карта 23 — поднимает;
//     • токены +51/+52 сохраняются и на отказе (эталон зовёт сеттеры до разбора кода), порядок байтов не перепутан.
//   Состояние подсистемы (токены, флаг, карта, тайлы, флаги перехода) и позиция персонажа восстанавливаются в конце.
//
// Консоль: `asda.Net.SelfTest5055 [delay=N] [quit]`. Только офлайн (`FAsdaOutgoingGate::RefuseDebugWhenOnline`).
//   Строки проверок — «САМОТЕСТ-5055», итог — «САМОТЕСТ-5055 ИТОГ: ok=… fail=…».
#include "CoreMinimal.h"

#if !UE_BUILD_SHIPPING

#include "AsdaNetSubsystem.h"
#include "AsdaOutgoingGate.h"                  // RefuseDebugWhenOnline — офлайн-гейт самотестов
#include "Test/AsdaSelfTestKit.h"   // ★ 11.10.2026 (В3 S1): общий каркас самотестов (аргументы, отказ, перехват, итог, выход)
#include "UI/AsdaNotice.h"                     // AsdaNotice::Loc — ожидаемый текст нотиса
#include "UI/AsdaMessageSubsystem.h"           // маршрут категории System (звука нет)
#include "Engine/Engine.h"
#include "Engine/GameInstance.h"
#include "Engine/World.h"
#include "GameFramework/PlayerController.h"
#include "GameFramework/Pawn.h"
#include "HAL/IConsoleManager.h"
#include "Misc/OutputDevice.h"
#include "Misc/OutputDeviceRedirector.h"
#include "TimerManager.h"

DEFINE_LOG_CATEGORY_STATIC(LogAsdaPortal5055Test, Log, All);

namespace AsdaPortal5055TestLocal
{
	// Перехватчик строк стоков чата: копит «[warmsg]» (плашка 221) и «[сисчат» (журнал) категории LogAsdaChat.
	class FChatSink : public FOutputDevice
	{
	public:
		TArray<FString> War;    // строки плашки 221
		TArray<FString> Sys;    // строки журнала
		virtual void Serialize(const TCHAR* V, ELogVerbosity::Type Verbosity, const FName& Category) override
		{
			if (!IsInGameThread()) { return; }   // ★ 05.11.2026: устройство синхронное (CanBeUsedOnMultipleThreads) — ловим только строки игрового потока: их пишет проверяемый код, читает самотест, замка у полей нет
			static const FName kChat(TEXT("LogAsdaChat"));
			if (Category != kChat || !V) { return; }
			const FString S(V);
			if (S.StartsWith(TEXT("[warmsg] строка"))) { War.Add(S); }   // «окно создано» и прочие служебные — мимо
			else if (S.StartsWith(TEXT("[сисчат"))) { Sys.Add(S); }
		}
		// Пишем и с чужих потоков — приёмник только копит строки под замком редиректора.
		virtual bool CanBeUsedOnAnyThread() const override { return true; }
		virtual bool CanBeUsedOnMultipleThreads() const override { return true; }   // ★ 05.11.2026: НЕбуферизованный список UE 5.8 — строка доходит в момент записи (иначе гонка с потоком логов: строка после RemoveOutputDevice/проверки)
		void Reset() { War.Reset(); Sys.Reset(); }
	};

	// Счётчики прогона.
	struct FRun
	{
		int32 Ok = 0, Fail = 0;
		void Expect(bool b, const FString& What)
		{
			if (b) { ++Ok; UE_LOG(LogAsdaPortal5055Test, Log, TEXT("САМОТЕСТ-5055 ok   %s"), *What); }
			else   { ++Fail; UE_LOG(LogAsdaPortal5055Test, Error, TEXT("САМОТЕСТ-5055 FAIL %s"), *What); }
		}
	};

	// Эталонная таблица 5055 (`Recv_PortalChannelChange@0xF45BD0`): loc-id и «есть ли строка журнала». 0 — нотиса нет.
	static int32 Expected5055(uint8 Code, bool& bChat)
	{
		bChat = true;
		switch (Code)
		{
		case 0x00: bChat = false; return 1096;
		case 0x02: bChat = false; return 1097;
		case 0x08: return 13916;
		case 0x0C: return 2231;
		default: break;
		}
		if (Code >= 0x0D && Code <= 0x14) { return 7101 + (Code - 0x0D); }
		bChat = false;
		return 0;   // 1 — переход; 3…7, 9…11 — окна подземелий (в чат ничего); 21+ — молча
	}

	// Эталонная таблица 6201 (`portalHandleChannelWarp@0xF464C0`): то же без диапазона 13…20.
	static int32 Expected6201(uint8 Code, bool& bChat)
	{
		if (Code >= 0x0D && Code <= 0x14) { bChat = false; return 0; }
		return Expected5055(Code, bChat);
	}
}

// Друг не нужен: поля состояния перехода и токены у `UAsdaNetSubsystem` публичные.
struct FAsdaPortal5055SelfTest
{
	// Весь прогон (синхронный — приёмник 5055/6201 отвечает сразу, без сети и таймеров).
	static void RunChecks(UWorld* W, bool bQuit);
	// Точка входа консольной команды.
	static void Run(const TArray<FString>& Args, UWorld* W);
};

void FAsdaPortal5055SelfTest::RunChecks(UWorld* W, bool bQuit)
{
	using namespace AsdaPortal5055TestLocal;
	UGameInstance* GI = W ? W->GetGameInstance() : nullptr;
	UAsdaNetSubsystem* Net = GI ? GI->GetSubsystem<UAsdaNetSubsystem>() : nullptr;
	if (!Net)
	{
		UE_LOG(LogAsdaPortal5055Test, Error, TEXT("САМОТЕСТ-5055 ИТОГ: ok=0 fail=1 (нет сетевой подсистемы)"));
		AsdaSelfTestKit::Quit(W, bQuit);
		return;
	}
	// Повторная проверка офлайна перед подачей: соединение могло появиться за время задержки.
	if (Net->IsConnected())
	{
		UE_LOG(LogAsdaPortal5055Test, Error, TEXT("САМОТЕСТ-5055 ИТОГ: ok=0 fail=1 (появилось живое соединение — прогон отменён)"));
		return;
	}

	FRun R;
	FChatSink Sink;
	GLog->AddOutputDevice(&Sink);

	// ── Снимок состояния (вернём в конце) ──
	const uint8 SavedT1 = Net->CampWarToken1, SavedT2 = Net->CampWarToken2;
	const bool  SavedHud = Net->bCampWarHudSuppressed;
	const int32 SavedMap = Net->EnterMapId, SavedTX = Net->SelfTileX, SavedTZ = Net->SelfTileZ;
	const int32 SavedPTX = Net->PortalTileX, SavedPTZ = Net->PortalTileZ;
	const bool  SavedTransfer = Net->bTransferInProgress, SavedPending = Net->bPendingEnterWorld, SavedPortal = Net->bPortalPending;
	APlayerController* PC = W->GetFirstPlayerController();
	APawn* Pawn = PC ? PC->GetPawn() : nullptr;
	const FVector SavedLoc = Pawn ? Pawn->GetActorLocation() : FVector::ZeroVector;

	// Маршрут категории System — без звука (эталон отказов перехода звука не играет).
	if (UAsdaMessageSubsystem* Msg = GI->GetSubsystem<UAsdaMessageSubsystem>())
	{
		R.Expect(Msg->GetRoute(EAsdaMsgCategory::System).Sound == NAME_None, TEXT("маршрут System без звука (error.wav у отказов нет)"));
	}

	// Подача и сверка одного отказа. `Op` — 5055/6201; ожидания — по эталонной таблице соответствующего обработчика.
	auto CheckRefusal = [&](int32 Op, uint8 Code)
	{
		bool bChat = false;
		const int32 LocId = (Op == 5055) ? Expected5055(Code, bChat) : Expected6201(Code, bChat);
		TArray<uint8> Body;
		if (Op == 5055)
		{
			Body.SetNumZeroed(53);
			Body[51] = 0xFF; Body[52] = 0xFF;                 // как в стоковом буфере сервера (`sub_40DE70`)
		}
		else
		{
			Body.SetNumZeroed(7);
		}
		Body[0] = Code;
		Net->CampWarToken1 = 0x11; Net->CampWarToken2 = 0x22;
		Net->bCampWarHudSuppressed = false;
		Net->bTransferInProgress = false; Net->bPendingEnterWorld = false; Net->bPortalPending = false;
		GLog->Flush();   // строки прошлых подач доходят до перехватчика отложенно — дослать ДО сброса
		Sink.Reset();
		Net->DebugFeedPacket(Op, Body);
		GLog->Flush();

		const FString Tag = FString::Printf(TEXT("%d код %u"), Op, (uint32)Code);
		const FString Text = LocId ? AsdaNotice::Loc(GI, LocId) : FString();
		if (LocId)
		{
			R.Expect(!Text.IsEmpty(), FString::Printf(TEXT("%s: строка loc %d есть в DT_GameText"), *Tag, LocId));
			const bool bWar = Sink.War.Num() == 1 && Sink.War[0].Contains(FString::Printf(TEXT("'%s'"), *Text))
				&& Sink.War[0].Contains(TEXT("цвет=#FFD306")) && Sink.War[0].Contains(TEXT("TTL=10000"));
			R.Expect(bWar, FString::Printf(TEXT("%s: плашка 221 «%s» −11514/10 с (строк %d)"), *Tag, *Text, Sink.War.Num()));
			if (bChat)
			{
				R.Expect(Sink.Sys.Num() == 1 && Sink.Sys[0].Contains(Text),
					FString::Printf(TEXT("%s: строка журнала «%s» (строк %d)"), *Tag, *Text, Sink.Sys.Num()));
			}
			else
			{
				R.Expect(Sink.Sys.Num() == 0, FString::Printf(TEXT("%s: журнала нет (строк %d)"), *Tag, Sink.Sys.Num()));
			}
		}
		else
		{
			R.Expect(Sink.War.Num() == 0 && Sink.Sys.Num() == 0,
				FString::Printf(TEXT("%s: нотиса нет (плашка %d, журнал %d)"), *Tag, Sink.War.Num(), Sink.Sys.Num()));
		}
		R.Expect(!Net->bTransferInProgress && !Net->bPendingEnterWorld && !Net->bPortalPending,
			FString::Printf(TEXT("%s: перехода нет"), *Tag));
		if (Op == 5055)
		{
			R.Expect(!Net->bCampWarHudSuppressed, FString::Printf(TEXT("%s: 0xFF в +51 НЕ поднимает флаг HUD"), *Tag));
			R.Expect(Net->CampWarToken1 == 0xFF && Net->CampWarToken2 == 0xFF,
				FString::Printf(TEXT("%s: токены защёлкнуты и на отказе (%02X %02X)"), *Tag, Net->CampWarToken1, Net->CampWarToken2));
		}
	};

	// ── 1. 5055: все коды отказа 0, 2…21 и крайние 0x3F/0xFF ──
	for (int32 C = 0; C <= 21; ++C) { if (C != 1) { CheckRefusal(5055, (uint8)C); } }
	CheckRefusal(5055, 0x3F);
	CheckRefusal(5055, 0xFF);

	// ── 2. 6201: те же коды — контрольная пара для 13…20 ──
	for (int32 C = 0; C <= 21; ++C) { if (C != 1) { CheckRefusal(6201, (uint8)C); } }

	// ── 3. Отказ не опускает уже поднятый флаг (эталон на отказе HUD не трогает вовсе) ──
	{
		TArray<uint8> B; B.SetNumZeroed(53); B[0] = 0x0C; B[51] = 0xFF; B[52] = 0xFF;
		Net->bCampWarHudSuppressed = true;
		Net->DebugFeedPacket(5055, B);
		R.Expect(Net->bCampWarHudSuppressed, TEXT("5055 код 12 при поднятом флаге: флаг остался поднят"));
		// Порядок токенов: +51 → Token1 (payload[25]), +52 → Token2 (payload[26]).
		B[0] = 0x00; B[51] = 0xAB; B[52] = 0xCD;
		Net->DebugFeedPacket(5055, B);
		R.Expect(Net->CampWarToken1 == 0xAB && Net->CampWarToken2 == 0xCD,
			FString::Printf(TEXT("токены: +51→Token1=%02X, +52→Token2=%02X (ожидание AB CD)"), Net->CampWarToken1, Net->CampWarToken2));
	}

	// ── 4. 5055 код 1 (переход в пределах той же карты — без реконнекта): бит 0x80 / карта 23 ──
	auto Transfer = [&](int32 Map, uint8 T1, bool bPreset, bool bExpect, const TCHAR* What)
	{
		TArray<uint8> B; B.SetNumZeroed(53);
		B[0] = 1;
		B[23] = (uint8)(Map & 0xFF); B[24] = (uint8)((Map >> 8) & 0xFF);
		const int32 TX = SavedTX >= 0 ? SavedTX : 256, TZ = SavedTZ >= 0 ? SavedTZ : 256;
		B[25] = (uint8)(TX & 0xFF); B[26] = (uint8)((TX >> 8) & 0xFF);
		B[27] = (uint8)(TZ & 0xFF); B[28] = (uint8)((TZ >> 8) & 0xFF);
		B[51] = T1; B[52] = 0x5A;
		Net->EnterMapId = Map;                       // та же карта ⇒ ветка «только позиция», соединение не трогается
		Net->bCampWarHudSuppressed = bPreset;
		Net->bTransferInProgress = false; Net->bPendingEnterWorld = false; Net->bPortalPending = false;
		GLog->Flush();   // строки прошлых подач доходят до перехватчика отложенно — дослать ДО сброса
		Sink.Reset();
		Net->DebugFeedPacket(5055, B);
		GLog->Flush();
		R.Expect(Net->bCampWarHudSuppressed == bExpect,
			FString::Printf(TEXT("5055 код 1, карта %d, +51=%02X: флаг HUD %s (%s)"), Map, T1,
				Net->bCampWarHudSuppressed ? TEXT("поднят") : TEXT("опущен"), What));
		R.Expect(Sink.War.Num() == 0 && Sink.Sys.Num() == 0, FString::Printf(TEXT("5055 код 1, карта %d: нотиса нет"), Map));
		R.Expect(!Net->bPendingEnterWorld, FString::Printf(TEXT("5055 код 1, карта %d: та же карта — без реконнекта"), Map));
	};
	Transfer(7, 0xFF, false, true,  TEXT("бит 0x80 ⇒ подавлен"));
	Transfer(7, 0x7F, true,  false, TEXT("бит 0x80 снят ⇒ разрешён"));
	Transfer(23, 0x00, false, true, TEXT("карта 23 ⇒ подавлен"));

	// ── Восстановление ──
	Net->CampWarToken1 = SavedT1; Net->CampWarToken2 = SavedT2;
	Net->bCampWarHudSuppressed = SavedHud;
	Net->EnterMapId = SavedMap; Net->SelfTileX = SavedTX; Net->SelfTileZ = SavedTZ;
	Net->PortalTileX = SavedPTX; Net->PortalTileZ = SavedPTZ;
	Net->bTransferInProgress = SavedTransfer; Net->bPendingEnterWorld = SavedPending; Net->bPortalPending = SavedPortal;
	if (Pawn && IsValid(Pawn)) { Pawn->SetActorLocation(SavedLoc, false, nullptr, ETeleportType::TeleportPhysics); }

	GLog->Flush();
	GLog->RemoveOutputDevice(&Sink);
	UE_LOG(LogAsdaPortal5055Test, Log, TEXT("САМОТЕСТ-5055 ИТОГ: ok=%d fail=%d"), R.Ok, R.Fail);
	AsdaSelfTestKit::Quit(W, bQuit);
}

void FAsdaPortal5055SelfTest::Run(const TArray<FString>& Args, UWorld* W)
{
	// ★ ПЕРВЫМ ОПЕРАТОРОМ — офлайн-гейт: в живой сессии поддельные 5055/6201 перевели бы игрока или испортили бы
	//   токены кампвара, которые уходят эхом в хендшейк 20000. Самотест ничего не отправляет — окно защиты не нужно.
	const FAsdaSelfTestArgs KitArgs = FAsdaSelfTestArgs::Parse(Args);   // ★ 11.10.2026 (В3 S1): каркас — отказ с ИТОГ и quit, второй барьер на все опкоды
	if (AsdaSelfTestKit::RefuseStart(W, TEXT("asda.Net.SelfTest5055"), KitArgs)) { return; }

	float Delay = 0.f;
	bool bQuit = false;
	for (const FString& A : Args)
	{
		if (A.Equals(TEXT("quit"), ESearchCase::IgnoreCase)) { bQuit = true; }
	}
	Delay = AsdaSelfTestKit::DelayArgOr(Args, Delay);   // ★ R5-fix2-c (13.11.2026): `delay=N` — общая точка (была ветка цикла аргументов)
	if (!W) { return; }
	if (Delay <= 0.f) { RunChecks(W, bQuit); return; }
	TWeakObjectPtr<UWorld> WW = W;
	FTimerHandle H;
	W->GetTimerManager().SetTimer(H, FTimerDelegate::CreateLambda([WW, bQuit]()
	{
		if (WW.Get() && AsdaSelfTestKit::RefuseDeferred(WW.Get(), TEXT("asda.Net.SelfTest5055"), bQuit)) { return; }   // Р-2: офлайн повторно — на отложенном старте
		if (UWorld* Wd = WW.Get()) { RunChecks(Wd, bQuit); }
	}), Delay, false);
}

static FAutoConsoleCommandWithWorldAndArgs GAsdaSelfTestPortal5055(
	TEXT("asda.Net.SelfTest5055"),
	TEXT("Самотест отказов перехода 5055/6201 (нотисы 1096/1097/13916/2231/7101…7108, плашка 221 и журнал по эталону) и "
	     "флага боевого HUD кампвара (только на переходе, бит 0x80 у +51 или карта 23). Только офлайн. `delay=N` `quit`."),
	FConsoleCommandWithWorldAndArgsDelegate::CreateLambda([](const TArray<FString>& A, UWorld* W)
	{
		FAsdaPortal5055SelfTest::Run(A, W);
	}));

#endif // !UE_BUILD_SHIPPING
