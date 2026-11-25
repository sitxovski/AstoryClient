// Copyright (c) 2026 @sitxovski (Discord)
// UAsdaMiniStateSubsystem — САМОТЕСТ доработок портрета своего персонажа (06.10.2026), `asda.UI.SelfTestMiniState`.
//   Отчёт: `Reports/owner_1006/self_portrait_impl.md`. Проверяет ТОЛЬКО штатными входами:
//     • пакеты — через `UAsdaNetSubsystem::DebugFeedPacket` → `HandlePacket` (тот же разбор, что у живого сервера),
//       затем кадр подсистем (`Tick`) — очереди отряда и эмблемы разбираются так же, как в игре;
//     • вес — через единый источник `SetSelfWeight` (его зовут все 70 каналов веса);
//     • смена имени — офлайн-спавн `Asda.World.FakeSelfSpawn` (эмуляция 4003 с другим именем);
//     • геометрия — `GetCachedGeometry` ПОСЛЕ отрисовки (закон ui-check-game-path-geometry).
//   ★ Контрольные пары (закон pixel-metric-needs-control-pairs) у каждой ветки: свой ↔ чужой, код успеха ↔ отказ,
//     ключ исправления 1 ↔ 0, жив ↔ мёртв, флаг вкл ↔ выкл, карта кампвара ↔ обычная.
//   Сервер не нужен и не трогается: команда отказывает при живом соединении (`RefuseDebugWhenOnline`).
#include "UI/AsdaMiniStateSubsystem.h"
#include "UI/AsdaUiHelpers.h"   // ★ R1: прямое включение (раньше приходило транзитивно)
#include "AsdaDevConsole.h"   // ★ античит А2: ASDA_DEV_CVAR / ASDA_DEV_CONSOLE — дев-ключи и команды вне Shipping
#include "AsdaNetSubsystem.h"
#include "AsdaSelf.h"                        // свой userNum (общая точка ядра)
// офлайн-гейт отладочных команд   ★ R3: комментарий снятого включения "AsdaOutgoingGate.h" (файл им не пользовался) — сохранён
#include "Test/AsdaSelfTestKit.h"   // ★ 11.10.2026 (В3 S1): общий каркас самотестов (аргументы, отказ, перехват, итог, выход)
#include "UI/AsdaChatSubsystem.h"            // журнал 2572: цвет строки нотиса (канал)
#include "UI/AsdaPartySubsystem.h"           // состав отряда (5107) и выход (5103)
#include "UI/AsdaWindowSubsystem.h"          // очистка очереди модальных нотисов после прогона
#include "UI/AsdaTargetFrameSubsystem.h"     // ★ ревизия: эмблема клана ЦЕЛИ (кампвар, гашение по событиям клана)
#include "UI/AsdaClanSubsystem.h"           // ★ ревизия: нотис роспуска 12410 (имя клана из кэша окна)
#include "AsdaSessionReset.h"                // ★ ревизия: настоящий сброс сессии (смена карты / выход в выбор персонажа)
#include "World/AsdaNameplateComponent.h"   // лагерь своего персонажа на плашке (CampId)
#include "Text/AsdaTextSubsystem.h"          // текст loc-строки для поиска в журнале
#include "World/AsdaWorldSubsystem.h"
#include "World/AsdaLoadManagerSubsystem.h"
#include "World/AsdaMapTable.h"
#include "World/AsdaClanFlag.h"              // kPlateUV — ожидаемый вырез эмблемы
#include "World/AsdaPlayerController.h"
#include "Components/Image.h"
#include "Components/TextBlock.h"
#include "Components/CanvasPanelSlot.h"
#include "Components/PanelWidget.h"       // GetParent() → UPanelWidget (обход цепочки родителей)
#include "Engine/Engine.h"
#include "Engine/GameInstance.h"
#include "Engine/Texture2D.h"
#include "Engine/TextureRenderTarget2D.h"
#include "TimerManager.h"
#include "HAL/IConsoleManager.h"
#include "Framework/Application/SlateApplication.h"   // настоящие события мыши (одиночный клик по портрету)
#include "HAL/PlatformProcess.h"                    // пауза дольше окна двойного клика
#include "GameFramework/PlayerController.h"   // GetPawn — «павн уже есть?» перед офлайн-спавном
#include <initializer_list>                   // TraceIs({ … }) — ожидаемый след нотисов
#include "AsdaCVarSave.h"                     // ★ ИСПРАВЛЕНИЕ 11.10.2026 (В0, N1(д)/U8): ключи консоли самотеста — запомнить и вернуть (значение + SetBy)

DEFINE_LOG_CATEGORY_STATIC(LogAsdaMiniTest, Log, All);

namespace AsdaMiniTest
{
	static constexpr int32 kSelfCharId  = 900001;   // свой charId на время прогона (восстанавливается)
	static constexpr int32 kOtherCharId = 900002;   // соотрядник (глава в составе 5107)
	static constexpr int32 kSelfClan    = 7;        // свой клан
	static constexpr int32 kOtherClan   = 8;        // чужой клан — контрольная пара 6542
	static const TCHAR*    kSelfName    = TEXT("Portrait");    // имя своего (офлайн-спавн)
	static const TCHAR*    kSelfName2   = TEXT("Portrait2");   // имя после «смены»

	// Запись little-endian в тело пакета.
	static void PutI16(TArray<uint8>& B, int32 At, int32 V) { B[At] = (uint8)(V & 0xFF); B[At + 1] = (uint8)((V >> 8) & 0xFF); }
	static void PutI32(TArray<uint8>& B, int32 At, int32 V) { for (int32 i = 0; i < 4; ++i) { B[At + i] = (uint8)(((uint32)V >> (8 * i)) & 0xFF); } }
	// ASCII-строка в поле фиксированной длины.
	static void PutStr(TArray<uint8>& B, int32 At, const char* S, int32 Max) { for (int32 i = 0; S[i] && i < Max; ++i) { B[At + i] = (uint8)S[i]; } }
	// 40 байт дизайна флага (поля через 8 байт, как `FAsdaClanFlagData::FromWire`): рамка 0, символ 0.
	static void PutFlag40(TArray<uint8>& B, int32 At)
	{
		PutI32(B, At + 0,  0);                    // BorderIdx
		PutI32(B, At + 8,  (int32)0xFFFFFFFF);    // BorderColor
		PutI32(B, At + 16, (int32)0xFF202020);    // BgColor
		PutI32(B, At + 24, 0);                    // SignIdx
		PutI32(B, At + 32, (int32)0xFFFFFFFF);    // SignColor
	}
	// Узел сам не скрыт (цепочку родителей проверяет геометрическая часть).
	static bool Shown(const UWidget* W)
	{
		if (!W) { return false; }
		const ESlateVisibility V = W->GetVisibility();
		return V != ESlateVisibility::Collapsed && V != ESlateVisibility::Hidden;
	}
	// Узел и ВСЯ цепочка родителей видимы.
	static bool ShownChain(const UWidget* W)
	{
		for (const UWidget* P = W; P; P = P->GetParent()) { if (!Shown(P)) { return false; } }
		return W != nullptr;
	}
	// Ширина слота канваса (клип слоёв 37/89 пишет её в Offsets.Right).
	static float SlotW(const UWidget* W)
	{
		const UCanvasPanelSlot* S = W ? Cast<UCanvasPanelSlot>(W->Slot) : nullptr;
		return S ? S->GetOffsets().Right : -1.f;
	}
	// Подсистема мини-статуса по миру.
	static UAsdaMiniStateSubsystem* MiniOf(UWorld* W)
	{
		return (W && W->GetGameInstance()) ? W->GetGameInstance()->GetSubsystem<UAsdaMiniStateSubsystem>() : nullptr;
	}
}

int32 UAsdaMiniStateSubsystem::RunSelfTestSync()
{
	using namespace AsdaMiniTest;
	UGameInstance* GI = GetGameInstance();
	UAsdaNetSubsystem* Net = GI ? GI->GetSubsystem<UAsdaNetSubsystem>() : nullptr;
	UAsdaWorldSubsystem* WS = GI ? GI->GetSubsystem<UAsdaWorldSubsystem>() : nullptr;
	UAsdaLoadManagerSubsystem* LM = GI ? GI->GetSubsystem<UAsdaLoadManagerSubsystem>() : nullptr;
	UAsdaPartySubsystem* Party = GI ? GI->GetSubsystem<UAsdaPartySubsystem>() : nullptr;
	UAsdaTextSubsystem* Txt = GI ? GI->GetSubsystem<UAsdaTextSubsystem>() : nullptr;
	UAsdaChatSubsystem* Chat = GI ? GI->GetSubsystem<UAsdaChatSubsystem>() : nullptr;
	UWorld* W = GI ? GI->GetWorld() : nullptr;
	AAsdaPlayerController* PC = W ? Cast<AAsdaPlayerController>(W->GetFirstPlayerController()) : nullptr;
	StOk = 0; StFail = 0;
	auto Check = [this](bool bOk, const FString& What)   // учёт и строка лога проверки
	{
		if (bOk) { ++StOk; } else { ++StFail; }
		UE_LOG(LogAsdaMiniTest, Log, TEXT("[портрет-самотест] %s — %s"), bOk ? TEXT("ОК") : TEXT("ПРОВАЛ"), *What);
	};
	if (!Net || !WS || !LM || !Party || !PC || !W)
	{
		Check(false, TEXT("нет сети/мира/загрузчика/отряда/контроллера"));
		return StFail;
	}
	auto Feed = [Net](int32 Op, const TArray<uint8>& B) { Net->DebugFeedPacket(Op, B); };   // штатный HandlePacket
	UAsdaTargetFrameSubsystem* TF = GI->GetSubsystem<UAsdaTargetFrameSubsystem>();   // панель цели
	UAsdaClanSubsystem* ClanS = GI->GetSubsystem<UAsdaClanSubsystem>();             // окна клана (нотис 12410)
	UAsdaSessionResetSubsystem* SR = GI->GetSubsystem<UAsdaSessionResetSubsystem>(); // рассылка сброса сессии
	UAsdaWindowSubsystem* WM = GI->GetSubsystem<UAsdaWindowSubsystem>();            // очередь модальных нотисов
	auto Pump = [this, Party, TF, ClanS]()   // один кадр подсистем (очереди отряда/клана → портрет → панель цели)
	{
		Party->Tick(0.f);
		if (ClanS) { ClanS->Tick(0.f); }
		Tick(0.f);
		if (TF) { TF->Tick(0.f); }
	};
	auto TraceStr = [this]()   // след нотисов строкой
	{
		FString S;
		for (int32 Id : NoticeTrace) { S += FString::Printf(TEXT("%d "), Id); }
		return S.IsEmpty() ? FString(TEXT("—")) : S;
	};
	auto TraceIs = [this](std::initializer_list<int32> Want)   // след равен списку
	{
		if (NoticeTrace.Num() != (int32)Want.size()) { return false; }
		int32 i = 0;
		for (int32 Id : Want) { if (NoticeTrace[i++] != Id) { return false; } }
		return true;
	};
	// Цвет последней строки журнала с текстом loc-строки (канал + цвет обработчика).
	auto JournalColor = [Chat, Txt](int32 LocId, FColor& Out)
	{
#if !UE_BUILD_SHIPPING
		if (!Chat || !Txt) { return false; }
		const FString Needle = Txt->GetTextById(LocId).Left(16);
		return !Needle.IsEmpty() && Chat->DebugFindLastSystemLineColor(Needle, Out);
#else
		return false;
#endif
	};

	// ── Подготовка: своя идентичность и сохранение того, что вернём ──
	StSavedCharId = Net->SelectedCharId;
	StSavedMapId = LM->CurrentMapId;
	StSavedWeightCur = Net->SelfWeightCur; StSavedWeightMax = Net->SelfWeightMax;
	IConsoleVariable* CvLeave = IConsoleManager::Get().FindConsoleVariable(TEXT("asda.Mini.PartyerHideOnLeave"));
	IConsoleVariable* CvFlagOff = IConsoleManager::Get().FindConsoleVariable(TEXT("asda.Mini.ClanMarkHideOnFlagOff"));
	StSavedHideOnLeave = CvLeave ? CvLeave->GetInt() : 1;
	StSavedHideOnFlagOff = CvFlagOff ? CvFlagOff->GetInt() : 1;
	Net->SelectedCharId = kSelfCharId;
	const int32 SelfIdx = AsdaSelf::GetUserIdxOr(GI, 0);   // свой userNum
	Pump();
	Check(bControlsReady, TEXT("окно MINISTATE_WIN поднято и разобрано"));
	Check(Net->IsSelfUserIdx(SelfIdx) && !Net->IsSelfUserIdx(SelfIdx + 1),
		FString::Printf(TEXT("свой userNum %d опознаётся, %d — чужой"), SelfIdx, SelfIdx + 1));
	if (!bControlsReady) { return StFail; }

	// ═══ 0. ИМЯ: смена имени без смены класса (раньше имя менялось только вместе с классом) ═══
	{
		const FString Before = CharNameText ? CharNameText->GetText().ToString() : FString();
		GEngine->Exec(W, *FString::Printf(TEXT("Asda.World.FakeSelfSpawn 256 256 %s"), kSelfName2));
		Pump();
		const FString After = CharNameText ? CharNameText->GetText().ToString() : FString();
		Check(Before == kSelfName && After == kSelfName2,
			FString::Printf(TEXT("имя: до «%s» (ждём %s) → после спавна с другим именем «%s» (ждём %s), класс %d не менялся"),
				*Before, kSelfName, *After, kSelfName2, LastClassApplied));
		GEngine->Exec(W, *FString::Printf(TEXT("Asda.World.FakeSelfSpawn 256 256 %s"), kSelfName));
		Pump();
		Check(CharNameText && CharNameText->GetText().ToString() == kSelfName, TEXT("имя вернулось «Portrait»"));
	}

	// ═══ 1. ВЕС: один нотис за пакет (goto LABEL_19), 2359 — со следующим пакетом ═══
	{
		auto WeightTo = [Net](int32 Cur) { Net->SetSelfWeight(Cur, 15000, TEXT("самотест портрета")); };
		WeightTo(1000); Pump(); NoticeTrace.Reset();
		WeightTo(14000);   // 93 % первым пакетом
		Check(TraceIs({ 2358 }), FString::Printf(TEXT("вес 93 %% первым пакетом → только 2358 (след: %s)"), *TraceStr()));
		Pump(); Pump();
		Check(TraceIs({ 2358 }), FString::Printf(TEXT("кадры после пакета — тот же пакет второй раз не применяется (след: %s)"), *TraceStr()));
		WeightTo(14100);
		Check(TraceIs({ 2358, 2359 }), FString::Printf(TEXT("следующий пакет ≥ 90 %% → 2359 (след: %s)"), *TraceStr()));
		WeightTo(14200);
		Check(TraceIs({ 2358, 2359 }), FString::Printf(TEXT("третий пакет ≥ 90 %% — нотисов нет (след: %s)"), *TraceStr()));
		// Контрольная пара: 70..89 % — 2359 не приходит.
		WeightTo(1000); NoticeTrace.Reset();
		WeightTo(11000);   // 73 %
		WeightTo(12000);   // 80 %
		Check(TraceIs({ 2358 }), FString::Printf(TEXT("73 %% → 80 %%: только 2358 (след: %s)"), *TraceStr()));
		WeightTo(13600);   // 90,6 %
		Check(TraceIs({ 2358, 2359 }), FString::Printf(TEXT("затем 90,6 %% → 2359 (след: %s)"), *TraceStr()));
		FColor C;
		const bool bLine = JournalColor(2358, C);
		Check(bLine && C.R == 0xFF && C.G == 0xAD && C.B == 0x00,
			FString::Printf(TEXT("2358 в журнале цветом эталона FFAD00 (канал — решение владельца 04.08: журнал + error.wav): %s %02X%02X%02X"),
				bLine ? TEXT("строка есть") : TEXT("СТРОКИ НЕТ"), C.R, C.G, C.B));
#if !UE_BUILD_SHIPPING
		// Контрольная пара дорожек к 4206: 2358 тоже в буфере A (буфер держит 5 строк — проверяем сразу).
		const FString NW = Txt ? Txt->GetTextById(2358).Left(16) : FString();
		Check(Chat && Chat->DebugBufferHas(EAsdaSysTrack::A, NW) && !Chat->DebugBufferHas(EAsdaSysTrack::B, NW),
			TEXT("дорожка 2358 (перегруз): буфер A, не B"));
#endif
		WeightTo(1000);
	}

	// ═══ 2. 4206: нотисы режима боя по обработчику ═══
	{
		auto Make4206 = [](int32 Uidx, int32 Sub, int32 Cmd)   // тело 13 б: @0 i16 userIdx, @7 подтип, @12 команда
		{
			TArray<uint8> B; B.SetNumZeroed(13);
			PutI16(B, 0, Uidx); B[7] = (uint8)Sub; B[12] = (uint8)Cmd;
			return B;
		};
		NoticeTrace.Reset();
		Feed(4206, Make4206(SelfIdx, 1, 1));
		Check(TraceIs({ 108114 }) && Shown(PvpModeIcon), FString::Printf(TEXT("4206 свой, команда 1, подтип 1 → 108114 + иконка 5300 (след: %s)"), *TraceStr()));
		Feed(4206, Make4206(SelfIdx, 2, 1));
		Check(TraceIs({ 108114, 108131 }) && Shown(PvpModeIcon), FString::Printf(TEXT("подтип 2 → 108131 (след: %s)"), *TraceStr()));
		Feed(4206, Make4206(SelfIdx, 0, 1));
		Check(TraceIs({ 108114, 108131, 108132 }) && !Shown(PvpModeIcon), FString::Printf(TEXT("подтип 0 → 108132, иконка скрыта (след: %s)"), *TraceStr()));
		Feed(4206, Make4206(SelfIdx, 0, 2));
		Feed(4206, Make4206(SelfIdx, 0, 0));
		Check(TraceIs({ 108114, 108131, 108132, 108119, 108120 }), FString::Printf(TEXT("команда 2 → 108119, команда 0 → 108120 (след: %s)"), *TraceStr()));
		// Контрольная пара: чужой userIdx — ни нотиса, ни иконки.
		Feed(4206, Make4206(SelfIdx + 1, 1, 1));
		Feed(4206, Make4206(SelfIdx + 1, 0, 2));
		Feed(4206, Make4206(SelfIdx + 1, 0, 0));
		Check(NoticeTrace.Num() == 5 && !Shown(PvpModeIcon), FString::Printf(TEXT("4206 чужого (1/2/0) — нотисов и иконки нет (след: %s)"), *TraceStr()));
		FColor C;
		const bool bLine = JournalColor(108114, C);
		Check(bLine && C == FColor::White, FString::Printf(TEXT("108114 в журнале белым (эталон −1): %s %02X%02X%02X"),
			bLine ? TEXT("строка есть") : TEXT("СТРОКИ НЕТ"), C.R, C.G, C.B));
#if !UE_BUILD_SHIPPING
		// ★ Ревизия МЕЛК-1: дорожка эталона track = 0 → буфер A (2557), не B (2558).
		const FString N1 = Txt ? Txt->GetTextById(108114).Left(16) : FString();
		const FString N2 = Txt ? Txt->GetTextById(108120).Left(16) : FString();
		Check(Chat && !N1.IsEmpty() && Chat->DebugBufferHas(EAsdaSysTrack::A, N1) && !Chat->DebugBufferHas(EAsdaSysTrack::B, N1)
			&& Chat->DebugBufferHas(EAsdaSysTrack::A, N2) && !Chat->DebugBufferHas(EAsdaSysTrack::B, N2),
			TEXT("4206: 108114 и 108120 в буфере A, в буфере B их нет (эталон track = 0)"));
#endif
	}

	// ═══ 3. ЗНАЧОК ГЛАВЫ ОТРЯДА (9) по 5102 ═══
	auto Make5107 = [](int32 Leader, int32 Second)   // состав: @0..23 шесть i32 charId (−1 пусто), @24.. имена по 20
	{
		TArray<uint8> B; B.SetNumZeroed(144);
		for (int32 i = 0; i < 6; ++i) { PutI32(B, 4 * i, -1); }
		PutI32(B, 0, Leader); PutI32(B, 4, Second);
		PutStr(B, 24, "Other", 20); PutStr(B, 44, "Portrait", 20);
		return B;
	};
	auto Make5102 = [](int32 CharId) { TArray<uint8> B; B.SetNumZeroed(4); PutI32(B, 0, CharId); return B; };
	{
		Feed(5107, Make5107(kOtherCharId, kSelfCharId)); Pump();
		Check(Party->IsInParty() && Party->GetMembers().Num() == 2 && !Shown(PartyerIcon),
			TEXT("5107 (глава — другой, я второй): в отряде, значок НЕ трогается (эталон зажигает только 5102)"));
		Feed(5102, Make5102(kSelfCharId)); Pump();
		Check(Shown(PartyerIcon), TEXT("5102 «глава — я» → значок показан"));
		Feed(5102, Make5102(kOtherCharId)); Pump();
		Check(!Shown(PartyerIcon), TEXT("5102 «глава — другой» → значок скрыт (контрольная пара)"));
		Feed(5102, Make5102(kSelfCharId)); Pump();
		Feed(5102, Make5102(123456)); Pump();
		Check(Shown(PartyerIcon), TEXT("5102 с charId вне состава → значок не меняется (эталон: член не найден)"));
		// Дефект оригинала: значок висит после роспуска. Ключ 0 = как оригинал, 1 = исправление.
		if (CvLeave) { AsdaCVarSave::SetForTest(TEXT("asda.Mini.PartyerHideOnLeave"), 0); }
		TArray<uint8> D; D.SetNumZeroed(2); PutI16(D, 0, Party->GetPartyIndex());
		Feed(5103, D); Pump();
		Check(!Party->IsInParty() && Shown(PartyerIcon), TEXT("[ОРИГИНАЛ] ключ 0: 5103 роспуск — значок остаётся (дефект оригинала воспроизводится)"));
		if (CvLeave) { AsdaCVarSave::SetForTest(TEXT("asda.Mini.PartyerHideOnLeave"), 1); }
		Pump();
		Check(!Shown(PartyerIcon), TEXT("[ПОРТ] ключ 1: отряда нет → значок погашен"));
	}

	// ═══ 4. ЭМБЛЕМА КЛАНА (270/271) ═══
	auto Make4135 = [](int32 CharId, int32 Clan, bool bFlag)   // запись 142 б: @0 charId, @4 i16 clanId, @6 имя, @101 флаг, @102 дизайн
	{
		TArray<uint8> B; B.SetNumZeroed(142);
		PutI32(B, 0, CharId); PutI16(B, 4, Clan); PutStr(B, 6, "SelfTest", 17);
		B[101] = bFlag ? 1 : 0;
		if (bFlag) { PutFlag40(B, 102); }
		return B;
	};
	auto Make6542 = [](int32 Clan, bool bOn)   // 43 б: @0 i16 clanId, @2 флаг, @3 дизайн 40 б
	{
		TArray<uint8> B; B.SetNumZeroed(43);
		PutI16(B, 0, Clan); B[2] = bOn ? 1 : 0; PutFlag40(B, 3);
		return B;
	};
	auto Make4133 = [](int32 CharId, const char* Name)   // код 2: @3 charId, @8 имя
	{
		TArray<uint8> B; B.SetNumZeroed(28);
		B[0] = 2; PutI32(B, 3, CharId); PutStr(B, 8, Name, 20);
		return B;
	};
	auto One = [](uint8 V) { TArray<uint8> B; B.Add(V); return B; };
	auto MarkRT = [this]()   // кисть эмблемы — испечённый RT с вырезом kPlateUV
	{
		if (!ClanMarkImg) { return false; }
		const FSlateBrush& B = ClanMarkImg->GetBrush();
		const FBox2D UV = B.GetUVRegion();
		return Cast<UTextureRenderTarget2D>(B.GetResourceObject()) != nullptr
			&& UV.Min.Equals(UAsdaClanFlag::kPlateUV.Min, 1e-4) && UV.Max.Equals(UAsdaClanFlag::kPlateUV.Max, 1e-4);
	};
	// ★ Ревизия МЕЛК-4: ВХОД НА КАРТУ ШТАТНЫМ ПУТЁМ. Настоящий сброс сессии «смена карты» (его же рассылает
	//   UAsdaSessionResetSubsystem на каждом портале), номер карты, снимок клана своего и спавн своего павна;
	//   затем кадры — окно портрета пересоздаётся через EnsureWidget, где и стоит эталонный вход на карту.
	// ★ Повторная ревизия (R2-СРЕДН-1): ПОРЯДОК СЕРВЕРА — `CharMake@0x543A10` кладёт 4135 своему (GameServer.c:124111)
	//   РАНЬШЕ 4003 (своего спавна, :124121) в одну пачку упаковки. Поэтому 4135 подаём ДО спавна, а старый павн
	//   заранее НЕ уничтожаем (офлайн травела нет — это и есть худший случай «ссылка на старый актор ещё жива»).
	//   До правки мира (зеркало клана своего + сброс ссылки при смене карты) эмблема после такого портала пропадала.
	auto ReEnterMap = [&](int32 MapId, int32 Clan, bool bFlag)
	{
		if (SR) { SR->Broadcast(EAsdaSessionResetReason::MapChanged); }
		GEngine->Exec(W, *FString::Printf(TEXT("Asda.Load.CurrentMap %d"), MapId));
		Feed(4135, Make4135(kSelfCharId, Clan, bFlag));                                   // CharMake :124111
		GEngine->Exec(W, *FString::Printf(TEXT("Asda.World.FakeSelfSpawn 256 256 %s"), kSelfName));   // 4003 :124121
		Pump(); Pump();
	};
	{
		Feed(4135, Make4135(kSelfCharId, kSelfClan, true)); Pump();
		Check(WS->GetSelfClanId() == kSelfClan && Shown(ClanMarkBase) && MarkRT(),
			TEXT("4135 свой + флаг → база 270 видна, 271 = испечённый флаг, вырез (23,12)+64×42"));
		Feed(4135, Make4135(kSelfCharId, kSelfClan, false)); Pump();
		Check(!Shown(ClanMarkBase), TEXT("4135 свой БЕЗ флага → база скрыта (контрольная пара; entity_char.c:44695)"));
		Feed(4135, Make4135(kOtherCharId, kSelfClan, true)); Pump();
		Check(!Shown(ClanMarkBase), TEXT("4135 ЧУЖОГО с флагом — портрет не трогается"));
		Feed(4135, Make4135(kSelfCharId, kSelfClan, true)); Pump();
		Check(Shown(ClanMarkBase), TEXT("4135 свой + флаг → снова видна"));

		// 4132: код 1 — блок клана (не гасит), код 2 — CHUD_SetVisible(0).
		TArray<uint8> C1; C1.SetNumZeroed(192); C1[0] = 1; PutI16(C1, 1, kSelfClan);
		Feed(4132, C1); Pump();
		Check(Shown(ClanMarkBase), TEXT("4132 код 1 — эмблема не гаснет (контрольная пара)"));
		Feed(4132, One(2)); Pump();
		Check(!Shown(ClanMarkBase), TEXT("4132 код 2 → эмблема погашена"));
		ReEnterMap(StSavedMapId, kSelfClan, true);
		Check(bControlsReady && Shown(ClanMarkBase) && MarkRT(), TEXT("портал (сброс «смена карты» + 4135 с флагом + спавн) → окно пересоздано, эмблема видна"));
		ReEnterMap(StSavedMapId, kSelfClan, false);
		Check(bControlsReady && !Shown(ClanMarkBase), TEXT("портал с 4135 БЕЗ флага → эмблема скрыта (контрольная пара)"));
		ReEnterMap(StSavedMapId, kSelfClan, true);

		// 6542 своего клана: ключ исправления 1 ↔ 0.
		if (CvFlagOff) { AsdaCVarSave::SetForTest(TEXT("asda.Mini.ClanMarkHideOnFlagOff"), 1); }
		Feed(6542, Make6542(kSelfClan, false)); Pump();
		Check(!Shown(ClanMarkBase), TEXT("[ПОРТ] 6542 своего «флаг выкл» → погашена"));
		Feed(6542, Make6542(kSelfClan, true)); Pump();
		Check(Shown(ClanMarkBase) && MarkRT(), TEXT("6542 своего «флаг вкл» → видна с испечённым флагом"));
		if (CvFlagOff) { AsdaCVarSave::SetForTest(TEXT("asda.Mini.ClanMarkHideOnFlagOff"), 0); }
		Feed(6542, Make6542(kSelfClan, false)); Pump();
		Check(Shown(ClanMarkBase), TEXT("[ОРИГИНАЛ] ключ 0: 6542 «выкл» после «вкл» — база остаётся (social.c:3640)"));
		Feed(4132, One(2)); Pump();
		Feed(6542, Make6542(kSelfClan, false)); Pump();
		Check(Shown(ClanMarkBase), TEXT("[ОРИГИНАЛ] ключ 0: 6542 «выкл» при прежнем «выкл» — база ЗАЖИГАЕТСЯ (дефект оригинала)"));
		if (CvFlagOff) { AsdaCVarSave::SetForTest(TEXT("asda.Mini.ClanMarkHideOnFlagOff"), 1); }
		Feed(6542, Make6542(kSelfClan, true)); Pump();
		Feed(6542, Make6542(kOtherClan, false)); Pump();
		Check(Shown(ClanMarkBase), TEXT("6542 ЧУЖОГО клана «выкл» — портрет не трогается (контрольная пара)"));

		// 4117 код 1 — выход из клана; затем вход на карту не возвращает эмблему.
		Feed(4117, One(0)); Pump();
		Check(Shown(ClanMarkBase), TEXT("4117 код 0 (отказ) — эмблема не гаснет (контрольная пара)"));
		Feed(4117, One(1)); Pump();
		Check(!Shown(ClanMarkBase) && WS->GetSelfClanId() == -1, TEXT("4117 код 1 → погашена, свой клан сброшен"));
		ReEnterMap(StSavedMapId, -1, false);
		Check(!Shown(ClanMarkBase), TEXT("портал после выхода из клана (сервер шлёт clanId −1) → скрыта"));

		// 4107 код 1 — роспуск своего клана.
		Feed(4135, Make4135(kSelfCharId, kSelfClan, true)); Pump();
		Feed(4107, One(2)); Pump();
		Check(Shown(ClanMarkBase), TEXT("4107 код 2 (отказ) — эмблема не гаснет (контрольная пара)"));
		Feed(4107, One(1)); Pump();
		Check(!Shown(ClanMarkBase) && WS->GetSelfClanId() == -1, TEXT("4107 код 1 → погашена, свой клан сброшен (пустой клановый блок, ui_core.c:17112-17121)"));
		Pump();
		Check(!Shown(ClanMarkBase) && !IsSelfClanFlagOn(), TEXT("после роспуска флаг клана своего снят (зеркало мира сброшено) — вход на карту эмблему не вернёт"));

		// 4133 код 2 — свой ↔ чужой.
		Feed(4135, Make4135(kSelfCharId, kSelfClan, true)); Pump();
		Feed(4133, Make4133(kOtherCharId, "Other")); Pump();
		Check(Shown(ClanMarkBase), TEXT("4133 код 2 ЧУЖОГО — эмблема не гаснет (контрольная пара)"));
		Feed(4133, Make4133(kSelfCharId, "Portrait")); Pump();
		Check(!Shown(ClanMarkBase), TEXT("4133 код 2 СВОЙ → погашена"));

		// Кампвар: карта с байтом +69 = 1 → ячейка 64×64 атласа лагерей по своему лагерю.
		const FAsdaMapRow* R19 = LM->GetMapRow(19);
		Check(R19 && R19->CampwarPkFlag == 1, FString::Printf(TEXT("DT_MapTable: карта 19 campwar = %d (ждём 1)"), R19 ? R19->CampwarPkFlag : -1));
		auto Make6713 = [](int32 Camp) { TArray<uint8> B; B.SetNumZeroed(7); PutI16(B, 0, Camp); return B; };
		auto CampUV = [this](int32 U, int32 V)   // кисть = CampWar_MarkImage, клетка (U,V) 64×64
		{
			if (!ClanMarkImg) { return false; }
			const FSlateBrush& B = ClanMarkImg->GetBrush();
			const UTexture2D* T = Cast<UTexture2D>(B.GetResourceObject());
			if (!T || !T->GetName().Contains(TEXT("campwar_markimage"), ESearchCase::IgnoreCase)) { return false; }
			const FIntPoint Sz = AsdaUi::LogicalAtlasSize(T);
			const FBox2D UV = B.GetUVRegion();
			const FVector2D Min((float)U / Sz.X, (float)V / Sz.Y), Max((float)(U + 64) / Sz.X, (float)(V + 64) / Sz.Y);
			UE_LOG(LogAsdaMiniTest, Log, TEXT("[портрет-самотест]   атлас %s %dx%d, UV (%.4f,%.4f)-(%.4f,%.4f), ждём (%.4f,%.4f)-(%.4f,%.4f)"),
				*T->GetName(), Sz.X, Sz.Y, UV.Min.X, UV.Min.Y, UV.Max.X, UV.Max.Y, Min.X, Min.Y, Max.X, Max.Y);
			return UV.Min.Equals(Min, 1e-3) && UV.Max.Equals(Max, 1e-3);
		};
		Feed(6713, Make6713(1));
		ReEnterMap(19, -1, false);
		Check(Shown(ClanMarkBase) && CampUV(64, 0), TEXT("кампвар, лагерь 1 (без клана) → база видна, клетка (64,0)"));
		Feed(6713, Make6713(2)); DrawPlayerPortraitMark();
		Check(Shown(ClanMarkBase) && CampUV(0, 64), TEXT("лагерь 2 → клетка (0,64)"));
		Feed(6713, Make6713(0)); DrawPlayerPortraitMark();
		Check(CampUV(0, 0), TEXT("лагерь 0 → клетка (0,0)"));
		Feed(4135, Make4135(kSelfCharId, kSelfClan, true)); Pump();
		Check(Shown(ClanMarkBase) && CampUV(0, 0), TEXT("кампвар + свой флаг: рисуется эмблема ЛАГЕРЯ, не флаг (ветка +69 первой)"));

#if !UE_BUILD_SHIPPING   // отладочные методы панели цели есть только вне shipping
		// ★ Ревизия (вне задачи): ПАНЕЛЬ ЦЕЛИ — та же общая точка лагеря. Цель = свой павн (лагерь 0, флаг вкл).
		AActor* Me = WS->LocalPlayerActor.Get();
		auto TfMarkIsCamp = [TF]()   // кисть эмблемы цели — атлас лагерей
		{
			const UImage* I = TF ? TF->DebugClanMark() : nullptr;
			const UObject* R = I ? I->GetBrush().GetResourceObject() : nullptr;
			return R && R->GetName().Contains(TEXT("campwar_markimage"), ESearchCase::IgnoreCase);
		};
		auto TfMarkIsRT = [TF]()     // кисть эмблемы цели — испечённый флаг
		{
			const UImage* I = TF ? TF->DebugClanMark() : nullptr;
			return I && Cast<UTextureRenderTarget2D>(I->GetBrush().GetResourceObject()) != nullptr;
		};
		if (TF) { TF->DebugSetClanMark(Me); }
		Check(TF && Shown(TF->DebugClanGroup()) && TfMarkIsCamp(), TEXT("панель цели на кампваре: эмблема цели = ячейка лагеря (ui_core.c:75970-75976)"));

		ReEnterMap(StSavedMapId, kSelfClan, true);
		Check(Shown(ClanMarkBase) && MarkRT(), TEXT("обычная карта + флаг → снова испечённый флаг (контрольная пара к кампвару)"));
		Me = WS->LocalPlayerActor.Get();
		if (TF) { TF->DebugSetClanMark(Me); }
		Check(TF && Shown(TF->DebugClanGroup()) && TfMarkIsRT(), TEXT("панель цели на обычной карте: испечённый флаг (контрольная пара)"));

		// Панель цели гаснет по событиям клана (`sub_92132C(g_pHUD, 0)`), чужой 4133 — нет.
		Feed(4133, Make4133(kOtherCharId, "Other")); Pump();
		Check(TF && Shown(TF->DebugClanGroup()), TEXT("панель цели: 4133 чужого — эмблема цели не гаснет (контрольная пара)"));
		Feed(4132, One(2)); Pump();
		Check(TF && !Shown(TF->DebugClanGroup()), TEXT("панель цели: 4132 код 2 → эмблема цели погашена"));
		if (TF) { TF->DebugSetClanMark(Me); }
		Feed(4117, One(1)); Pump();
		Check(TF && !Shown(TF->DebugClanGroup()), TEXT("панель цели: 4117 код 1 → эмблема цели погашена"));
#endif
		Feed(6713, Make6713(-1));

		// ★ Ревизия (вне задачи): нотис роспуска 12410 — имя клана из кэша окна сообщества (gap_616), не из
		//   зеркала персонажа, которое сеть чистит до рассылки 4107.
		if (WM && ClanS)
		{
			Feed(4135, Make4135(kSelfCharId, kSelfClan, true)); Pump();
			TArray<uint8> Blk; Blk.SetNumZeroed(192); Blk[0] = 1; PutI16(Blk, 1, kSelfClan); PutStr(Blk, 3, "SelfTest", 18);
			Feed(4132, Blk); Pump();
			WM->ClearMsgBoxQueue(TEXT("самотест портрета: перед 4107"));
			Feed(4107, One(1)); Pump(); Pump();
			const FString Top = WM->GetMsgBoxQueueTopText();
			Check(Top.StartsWith(TEXT("SelfTest")), FString::Printf(TEXT("4107 код 1: нотис 12410 начинается с имени клана из кэша окна: «%s»"), *Top));
			Feed(4135, Make4135(kSelfCharId, kSelfClan, true)); Pump();
			Feed(4132, One(2)); Pump();   // 4132 код 2 обнуляет кэш окна
			WM->ClearMsgBoxQueue(TEXT("самотест портрета: перед 4107 (контроль)"));
			Feed(4107, One(1)); Pump(); Pump();
			const FString Top2 = WM->GetMsgBoxQueueTopText();
			Check(!Top2.Contains(TEXT("SelfTest")), FString::Printf(TEXT("контроль: кэш окна обнулён 4132 код 2 → имени нет: «%s»"), *Top2));
			WM->ClearMsgBoxQueue(TEXT("самотест портрета: после 12410"));
		}
	}

	// ═══ 5. СМЕРТЬ — штатными пакетами: 4058 («второе» HP) → 4053 (смерть) → 4058 при смерти → 4056 (воскрешение) ═══
	{
		const bool bPotionWas = bResetVitalSecondWithoutPotion;
		bResetVitalSecondWithoutPotion = false;   // иначе обнуление «без зелья» спутало бы контрольную пару
		auto Make4058 = [](int32 Uidx, int32 Max, int32 Cur, int32 Sec)   // 17 б: @0 uidx, @2 maxHp, @6 curHp, @10 тип, @11 skill, @13 второе HP
		{
			TArray<uint8> B; B.SetNumZeroed(17);
			PutI16(B, 0, Uidx); PutI32(B, 2, Max); PutI32(B, 6, Cur); PutI32(B, 13, Sec);
			return B;
		};
		auto Make4053 = [](int32 Uidx) { TArray<uint8> B; B.SetNumZeroed(4); PutI16(B, 0, Uidx); PutI16(B, 2, Uidx + 100); return B; };
		auto Make4056 = [](int32 Uidx)   // 22 б: @0 uidx, @2/@4 тайл, @6 HP, @10 MP, @12 опыт, @20 state (не 200 = живым)
		{
			TArray<uint8> B; B.SetNumZeroed(22);
			PutI16(B, 0, Uidx); PutI16(B, 2, 256); PutI16(B, 4, 256); PutI32(B, 6, 300); PutI16(B, 10, 50);
			return B;
		};
		UAsdaNetSubsystem::FAsdaVitals* V = nullptr;   // витальные своего (ищем после пакета)
		Feed(4058, Make4058(SelfIdx, 696, 300, 500)); Pump();
		const float H0 = SlotW(HpGhost);
		Pump();
		Check(H0 > 1.f && FMath::IsNearlyEqual(H0, SlotW(HpGhost)) && !IsSelfDeadExact(),
			FString::Printf(TEXT("жив: 4058 «второе» HP 500 → слой HPBuff %.1f px держится (контрольная пара)"), SlotW(HpGhost)));
		Feed(4053, Make4053(SelfIdx)); Pump();
		V = Net->VitalsByUserIdx.Find(SelfIdx);
		Check(IsSelfDeadExact() && SlotW(HpGhost) == 0.f && SlotW(MpGhost) == 0.f && V && V->HpSecond == 0 && V->MpSecond == 0,
			FString::Printf(TEXT("4053 смерть: слои %.1f / %.1f px, «второе» HP/MP %d / %d (ждём 0)"),
				SlotW(HpGhost), SlotW(MpGhost), V ? V->HpSecond : -9, V ? V->MpSecond : -9));
		Feed(4058, Make4058(SelfIdx, 696, 300, 500));   // без кадра — пакет пришёл ПОСЛЕ тика портрета
		Check(SlotW(HpGhost) == 0.f, FString::Printf(TEXT("мёртв: 4058 со «вторым» HP 500 между кадрами — слой не мелькает (%.1f px)"), SlotW(HpGhost)));
		Pump();
		Feed(4056, Make4056(SelfIdx)); Pump(); Pump();
		Check(!IsSelfDeadExact() && SlotW(HpGhost) == 0.f, FString::Printf(TEXT("4056 воскрешение: жив, слой %.1f px до нового «второго» HP (как эталон)"), SlotW(HpGhost)));
		Feed(4058, Make4058(SelfIdx, 696, 300, 500)); Pump();
		Check(SlotW(HpGhost) > 1.f, FString::Printf(TEXT("воскрешён: 4058 «второе» HP 500 → слой снова %.1f px (пара «мёртв → воскрешён»)"), SlotW(HpGhost)));
		bResetVitalSecondWithoutPotion = bPotionWas;
	}

	// ═══ 6. ДВОЙНОЙ ЛКМ по 97: полный сброс и цель = я (повторно — при цели «уже я») ═══
	{
		PC->ClearTarget();
		OnSelfTargetDoubleClick();
		Check(PC->TargetActor.Get() == WS->LocalPlayerActor.Get(), TEXT("двойной ЛКМ: цель = свой персонаж"));
		OnSelfTargetDoubleClick();
		Check(PC->TargetActor.Get() == WS->LocalPlayerActor.Get(), TEXT("повторный двойной ЛКМ при цели «я»: сброс + снова я (панель цели пересобрана ForceRefresh)"));
		PC->ClearTarget();
	}

	// ═══ 7. ПОРТАЛ И ВЫХОД (ревизия СРЕДН-1 + флаги перегруза): настоящий сброс сессии ═══
	{
		Feed(5107, Make5107(kSelfCharId, kOtherCharId)); Pump();
		Feed(5102, Make5102(kSelfCharId)); Pump();
		Net->SetSelfWeight(1000, 15000, TEXT("самотест портрета")); Net->SetSelfWeight(11000, 15000, TEXT("самотест портрета"));   // 73 % → 2358
		NoticeTrace.Reset();
		ReEnterMap(StSavedMapId, kSelfClan, true);
		Net->SetSelfWeight(11000, 15000, TEXT("самотест портрета: вес после портала"));
		Check(Party->IsInParty() && Party->IsPartyLeader() && Shown(PartyerIcon),
			TEXT("портал (сброс «смена карты», окно пересоздано): я глава → значок главы ВИДЕН (живой CHUD оригинала)"));
		Check(NoticeTrace.Num() == 0, FString::Printf(TEXT("портал при перегрузе 73 %%: 2358 НЕ повторяется (след: %s)"), *TraceStr()));
		// Контрольная пара: полный сброс (выход в выбор персонажа) — значок и флаги перегруза уходят.
		if (SR) { SR->Broadcast(EAsdaSessionResetReason::LeftWorld); }
		Check(!Party->IsInParty() && !Shown(PartyerIcon) && !bPartyerOn, TEXT("выход в выбор персонажа (полный сброс) → отряда нет, значок погашен"));
		Net->SelectedCharId = kSelfCharId;
		GEngine->Exec(W, *FString::Printf(TEXT("Asda.Load.CurrentMap %d"), StSavedMapId));
		GEngine->Exec(W, *FString::Printf(TEXT("Asda.World.FakeSelfSpawn 256 256 %s"), kSelfName));
		Pump(); Pump();
		NoticeTrace.Reset();
		Net->SetSelfWeight(11000, 15000, TEXT("самотест портрета: вес после нового входа"));
		Check(TraceIs({ 2358 }), FString::Printf(TEXT("новый вход после полного сброса: 73 %% → 2358 снова (контрольная пара; след: %s)"), *TraceStr()));
		Net->SetSelfWeight(1000, 15000, TEXT("самотест портрета"));

		// ★ Ревизия R3-МЕЛК-1: флаг клана (6542) пришёл РАНЬШЕ первого снимка 4135 сессии. Клан своего известен из
		//   4105 (создание клана, без флага); затем 6542 своего клана; затем спавн — зеркало мира применяет флаг.
		auto Make4105 = [](int32 Clan)   // код 1: @1 u16 clanId, @3 имя 20 б, @173 звание 9 б (тело ≥ 182)
		{
			TArray<uint8> B; B.SetNumZeroed(196);
			B[0] = 1; PutI16(B, 1, Clan); PutStr(B, 3, "SelfTest", 20); PutStr(B, 173, "Master", 9);
			return B;
		};
		auto ReSpawnFull = [&]()   // полный сброс (выход в выбор персонажа) и новый вход без 4135
		{
			if (SR) { SR->Broadcast(EAsdaSessionResetReason::LeftWorld); }
			Net->SelectedCharId = kSelfCharId;
			GEngine->Exec(W, *FString::Printf(TEXT("Asda.Load.CurrentMap %d"), StSavedMapId));
		};
		ReSpawnFull();
		Feed(4105, Make4105(kSelfClan));
		Feed(6542, Make6542(kSelfClan, true));
		GEngine->Exec(W, *FString::Printf(TEXT("Asda.World.FakeSelfSpawn 256 256 %s"), kSelfName));
		Pump(); Pump();
		Check(Shown(ClanMarkBase) && MarkRT(), TEXT("6542 «флаг вкл» раньше первого 4135 сессии (после 4105) → после спавна эмблема видна"));
		ReSpawnFull();
		Feed(4105, Make4105(kSelfClan));
		Feed(6542, Make6542(kSelfClan, false));
		GEngine->Exec(W, *FString::Printf(TEXT("Asda.World.FakeSelfSpawn 256 256 %s"), kSelfName));
		Pump(); Pump();
		Check(!Shown(ClanMarkBase), TEXT("контрольная пара: 6542 «флаг выкл» раньше 4135 → после спавна эмблемы нет"));
		if (WM) { WM->ClearMsgBoxQueue(TEXT("самотест портрета: после 4105")); }
	}

	// ── Состояние для геометрической части: значок главы и эмблема видимы ──
	Feed(5107, Make5107(kSelfCharId, kOtherCharId)); Pump();
	Feed(5102, Make5102(kSelfCharId)); Pump();
	Feed(4135, Make4135(kSelfCharId, kSelfClan, true)); Pump();
	UE_LOG(LogAsdaMiniTest, Log, TEXT("[портрет-самотест] синхронная часть: проверок %d, провалов %d"), StOk + StFail, StFail);
	UE_LOG(LogAsdaMiniTest, Log, TEXT("%s"), *DumpControls());
	return StFail;
}

int32 UAsdaMiniStateSubsystem::RunSelfTestGeometry()
{
	using namespace AsdaMiniTest;
	const int32 FailWas = StFail;
	auto Check = [this](bool bOk, const FString& What)
	{
		if (bOk) { ++StOk; } else { ++StFail; }
		UE_LOG(LogAsdaMiniTest, Log, TEXT("[портрет-самотест] %s — %s"), bOk ? TEXT("ОК") : TEXT("ПРОВАЛ"), *What);
	};
	// Экранный прямоугольник после отрисовки.
	auto Rect = [](const UWidget* W, FVector2D& P, FVector2D& S)
	{
		if (!W) { return false; }
		const FGeometry& G = W->GetCachedGeometry();
		P = FVector2D(G.GetAbsolutePosition());
		S = FVector2D(G.GetAbsoluteSize());
		return S.X > 0.f && S.Y > 0.f;
	};
	FVector2D CP, CS, PP, PS, MP, MS;
	const bool bC = Rect(ClickZone, CP, CS);
	const bool bP = Rect(PartyerIcon, PP, PS);
	const bool bM = Rect(ClanMarkImg, MP, MS);
	Check(bC, FString::Printf(TEXT("зона клика 97 отрисована: (%.1f,%.1f) %.1f×%.1f"), CP.X, CP.Y, CS.X, CS.Y));
	if (!bC) { return StFail - FailWas; }
	// Масштаб UI: зона 97 в JUI 227×58 @ (10,14) — опорный узел, не меняется кодом.
	const float K = CS.X / 227.f;
	auto Near = [K](const FVector2D& Got, const FVector2D& Want) { return Got.Equals(Want, 1.5f * FMath::Max(K, 1.f)); };
	const FVector2D WantPP = CP + FVector2D(10.f - 10.f, 45.f - 14.f) * K, WantPS = FVector2D(17.f, 18.f) * K;
	const FVector2D WantMP = CP + FVector2D(241.f - 10.f, 16.f - 14.f) * K, WantMS = FVector2D(32.f, 32.f) * K;
	Check(bP && ShownChain(PartyerIcon) && Near(PP, WantPP) && Near(PS, WantPS),
		FString::Printf(TEXT("значок главы 9 на экране (%.1f,%.1f) %.1f×%.1f, ждём (%.1f,%.1f) %.1f×%.1f [JUI abs (10,45) 17×18]"),
			PP.X, PP.Y, PS.X, PS.Y, WantPP.X, WantPP.Y, WantPS.X, WantPS.Y));
	Check(bM && ShownChain(ClanMarkImg) && Near(MP, WantMP) && Near(MS, WantMS),
		FString::Printf(TEXT("эмблема 271 на экране (%.1f,%.1f) %.1f×%.1f, ждём (%.1f,%.1f) %.1f×%.1f [JUI abs (241,16) 32×32]"),
			MP.X, MP.Y, MS.X, MS.Y, WantMP.X, WantMP.Y, WantMS.X, WantMS.Y));

	// ═══ ОДИН КЛИК ПО ПОРТРЕТУ (05.11.2026, [ИСПРАВЛЕНИЕ] владельца, ключ asda.UI.SelfTargetSingleClick) ═══
	//   Путём игрока: настоящие движение/нажатие/отпускание мыши Slate в точку зоны 97, затем опрос `TickPointer` (как кадр).
	UGameInstance* GI = GetGameInstance();
	UAsdaWorldSubsystem* WS = GI ? GI->GetSubsystem<UAsdaWorldSubsystem>() : nullptr;
	UWorld* World = GI ? GI->GetWorld() : nullptr;
	AAsdaPlayerController* PC = World ? Cast<AAsdaPlayerController>(World->GetFirstPlayerController()) : nullptr;
	if (WS && PC && IsValid(WS->LocalPlayerActor) && FSlateApplication::IsInitialized())
	{
		FSlateApplication& App = FSlateApplication::Get();
		auto Move = [&App](const FVector2D& P)
		{
			App.SetCursorPos(P);
			App.ProcessMouseMoveEvent(FPointerEvent(0, P, P, TSet<FKey>(), EKeys::Invalid, 0.f, FModifierKeysState()));
		};
		auto Btn = [&App, this](const FVector2D& P, bool bDown)
		{
			const FPointerEvent E(0, P, P, TSet<FKey>(), EKeys::LeftMouseButton, 0.f, FModifierKeysState());
			if (bDown) { App.ProcessMouseButtonDownEvent(nullptr, E); } else { App.ProcessMouseButtonUpEvent(E); }
			TickPointer();
		};
		auto Click = [&](const FVector2D& P) { Move(P); TickPointer(); Btn(P, true); Btn(P, false); };
		auto IsSelf = [&]() { return PC->TargetActor.Get() == WS->LocalPlayerActor.Get(); };
		// Точка зоны 97 вне своих контролов (иконки эффектов, плюс-пак) — сетка по зоне.
		FVector2D Pt = CP + CS * 0.5f;
		bool bPt = false;
		for (int32 Iy = 1; Iy < 8 && !bPt; ++Iy)
		{
			for (int32 Ix = 1; Ix < 12 && !bPt; ++Ix)
			{
				const FVector2D Q = CP + FVector2D(CS.X * Ix / 12.f, CS.Y * Iy / 8.f);
				Move(Q);
				if (IsCursorOver(ClickZone) && !IsCursorOverOwnControlInClickZone()) { Pt = Q; bPt = true; }
			}
		}
		Check(bPt, FString::Printf(TEXT("К0 точка зоны 97 вне иконок эффектов/плюс-пака: (%.0f,%.0f)"), Pt.X, Pt.Y));
		const FVector2D Out = CP + FVector2D(CS.X + 200.f, CS.Y + 200.f);   // вне зоны (мир)
		Move(Out); TickPointer();
		{
			FAsdaCVarScope Keys(World, TEXT("asda.UI.SelfTestMiniState"));
			Keys.Set(TEXT("asda.UI.SelfTargetSingleClick"), 1);
			PC->ClearTarget();
			Move(Pt); TickPointer(); Btn(Pt, true);
			Check(!IsSelf(), TEXT("К1 нажатие ЛКМ по портрету — выбор ещё не сделан (ждём отпускания: перетаскивание не выбирает)"));
			Btn(Pt, false);
			Check(IsSelf(), TEXT("К2 ОДИН клик по портрету — цель = свой персонаж [ИСПРАВЛЕНИЕ]"));
			FPlatformProcess::Sleep(FAsdaDblClick::WindowSec() + 0.05f);   // К2 не должен стать первым кликом пары К3
			// Двойной клик: первый клик выбрал; цель снимаем между кликами — второй клик пары НЕ должен выбрать повторно.
			PC->ClearTarget();
			Click(Pt);
			const bool bFirst = IsSelf();
			PC->ClearTarget();
			Click(Pt);
			Check(bFirst && !IsSelf(), TEXT("К3 двойной клик — выбор ровно один раз (второе нажатие пары не повторяет)"));
			// Третий клик после пары — новое одиночное нажатие (тройной не считается вторым двойным).
			Click(Pt);
			Check(IsSelf(), TEXT("К4 клик после двойного — снова одиночный, выбирает"));
			// Перетаскивание: нажатие в зоне, сдвиг дальше порога, отпускание — не клик.
			PC->ClearTarget();
			Move(Out); TickPointer();   // разорвать пару двойного
			FPlatformProcess::Sleep(FAsdaDblClick::WindowSec() + 0.05f);
			Move(Pt); TickPointer(); Btn(Pt, true);
			const FVector2D Dragged = Pt + FVector2D(12.f, 0.f);
			Move(Dragged); TickPointer(); Btn(Dragged, false);
			Check(!IsSelf(), TEXT("К5 нажатие и сдвиг на 12 px (перетаскивание) — не выбирает"));
			// Нажатие в мире, отпускание над портретом — не клик по портрету.
			FPlatformProcess::Sleep(FAsdaDblClick::WindowSec() + 0.05f);
			Move(Out); TickPointer(); Btn(Out, true);
			Move(Pt); TickPointer(); Btn(Pt, false);
			Check(!IsSelf(), TEXT("К6 нажатие вне зоны, отпускание над портретом — не выбирает"));
			// Свой контрол внутри зоны (плюс-пак) — клик по нему себя не выбирает.
			if (PlusPackIcon && ShownChain(PlusPackIcon))
			{
				FVector2D PkP, PkS;
				if (Rect(PlusPackIcon, PkP, PkS))
				{
					FPlatformProcess::Sleep(FAsdaDblClick::WindowSec() + 0.05f);
					const FVector2D Pk = PkP + PkS * 0.5f;
					Click(Pk);
					Check(!IsSelf() || !IsCursorOver(ClickZone), TEXT("К7 клик по плюс-паку внутри зоны — себя не выбирает"));
				}
			}
			else { UE_LOG(LogAsdaMiniTest, Log, TEXT("[портрет-самотест] К7 плюс-пак скрыт — проверка клика по нему пропущена")); }
			// ПКМ по портрету — выбор не делает (ПКМ в зоне — только подсказка плюс-пака).
			PC->ClearTarget();
			Move(Pt); TickPointer();
			{
				const FPointerEvent R(0, Pt, Pt, TSet<FKey>(), EKeys::RightMouseButton, 0.f, FModifierKeysState());
				App.ProcessMouseButtonDownEvent(nullptr, R); TickPointer();
				App.ProcessMouseButtonUpEvent(R); TickPointer();
			}
			Check(!IsSelf(), TEXT("К8 ПКМ по портрету — цель не меняется"));
			// Ключ 0 — прежнее: одиночный не выбирает, двойной выбирает.
			Keys.Set(TEXT("asda.UI.SelfTargetSingleClick"), 0);
			FPlatformProcess::Sleep(FAsdaDblClick::WindowSec() + 0.05f);
			Click(Pt);
			Check(!IsSelf(), TEXT("К9 ключ 0: один клик — не выбирает (как оригинал)"));
			Click(Pt);
			Check(IsSelf(), TEXT("К10 ключ 0: двойной клик — выбирает (как оригинал)"));
		}
		PC->ClearTarget();
		Move(Out); TickPointer();
	}
	else { Check(false, TEXT("К0 нет своего персонажа / контроллера / Slate")); }
	return StFail - FailWas;
}

#if !UE_BUILD_SHIPPING   // ★ античит А2 (12.10.2026): отладка самотестов/дев-команд — только вне Shipping
void UAsdaMiniStateSubsystem::FinishSelfTest()
{
	using namespace AsdaMiniTest;
	UGameInstance* GI = GetGameInstance();
	UAsdaNetSubsystem* Net = GI ? GI->GetSubsystem<UAsdaNetSubsystem>() : nullptr;
	UAsdaLoadManagerSubsystem* LM = GI ? GI->GetSubsystem<UAsdaLoadManagerSubsystem>() : nullptr;
	UAsdaPartySubsystem* Party = GI ? GI->GetSubsystem<UAsdaPartySubsystem>() : nullptr;
	UAsdaWindowSubsystem* WM = GI ? GI->GetSubsystem<UAsdaWindowSubsystem>() : nullptr;
	if (Net)
	{
		// Клан и отряд — теми же штатными пакетами: клан −1 без флага, роспуск отряда.
		TArray<uint8> C; C.SetNumZeroed(142);
		PutI32(C, 0, kSelfCharId); PutI16(C, 4, -1);
		Net->DebugFeedPacket(4135, C);
		if (Party && Party->IsInParty())
		{
			TArray<uint8> D; D.SetNumZeroed(2); PutI16(D, 0, Party->GetPartyIndex());
			Net->DebugFeedPacket(5103, D);
			Party->Tick(0.f);
		}
		Net->SetSelfWeight(StSavedWeightCur, StSavedWeightMax, TEXT("самотест портрета: возврат"));
		Net->SelectedCharId = StSavedCharId;
	}
	if (LM && StSavedMapId >= 0) { LM->CurrentMapId = StSavedMapId; }
	// ★ ИСПРАВЛЕНИЕ 11.10.2026 (В0, N1(д)/U8): ключи Mini.PartyerHideOnLeave / Mini.ClanMarkHideOnFlagOff — к исходному значению И приоритету
	//   SetBy (прежде — прежние значения, но с SetByCode).
	AsdaCVarSave::RestoreAll(TEXT("asda.UI.SelfTestMiniState"));
	Tick(0.f);
	if (WM) { WM->ClearMsgBoxQueue(TEXT("самотест портрета: конец")); }   // нотисы клана 12425/12410/12411
	NoticeTrace.Reset();
	AsdaSelfTestKit::Summary(LogAsdaMiniTest, TEXT("[портрет-самотест]"), StOk, StFail, TEXT(""), false);   // ★ 11.10.2026 (В3 S1): единый итог (было «проверок N, провалов M»)
}
#endif // !UE_BUILD_SHIPPING

// asda.UI.SelfTestMiniState [delay=N] [quit] — офлайн: спавн своего павна (если его нет), через 4 с синхронная часть,
//   ещё через 1,5 с (кадры отрисованы) геометрическая часть, возврат состояния, выход по `quit`.
#if ASDA_DEV_CONSOLE   // ★ античит А2: дев-команда, в Shipping не регистрируется
static FAutoConsoleCommandWithWorldAndArgs GAsdaMiniSelfTest(
	TEXT("asda.UI.SelfTestMiniState"),
	TEXT("asda.UI.SelfTestMiniState [delay=N] [quit] — ОФЛАЙН-самотест портрета своего персонажа (MINISTATE_WIN): "
	     "вес 2358/2359, нотисы 4206, значок главы 5102, эмблема клана 4135/6542/4107/4117/4132/4133 и кампвар, "
	     "смерть, имя, двойной клик; пакеты штатным HandlePacket, геометрия после отрисовки"),
	FConsoleCommandWithWorldAndArgsDelegate::CreateLambda([](const TArray<FString>& A, UWorld* W)
	{
		AsdaCVarSave::ArmWorldGuard(W, TEXT("asda.UI.SelfTestMiniState"));   // ★ С1 (ревизия wave0_REVIEW.md, 11.10.2026): мир ушёл до итога — ключи вернёт сторож
		const FAsdaSelfTestArgs KitArgs = FAsdaSelfTestArgs::Parse(A);   // ★ 11.10.2026 (В3 S1): каркас — отказ с ИТОГ и quit, второй барьер на все опкоды
		if (AsdaSelfTestKit::RefuseStart(W, TEXT("asda.UI.SelfTestMiniState"), KitArgs)) { return; }
		if (!W) { return; }
		float Delay = 0.f;   // задержка запуска, с
		bool bQuit = false;  // выйти после прогона
		for (const FString& Arg : A)
		{
			if (Arg.Equals(TEXT("quit"), ESearchCase::IgnoreCase)) { bQuit = true; }
		}
		Delay = AsdaSelfTestKit::DelayArgOr(A, Delay);   // ★ R5-fix2-c (13.11.2026): `delay=N` — общая точка (была ветка цикла аргументов)
		TWeakObjectPtr<UWorld> WW = W;
		auto Start = [WW, bQuit]()   // спавн и цепочка этапов
		{
			UWorld* Wd = WW.Get();
			if (!Wd || !GEngine) { return; }
			if (AsdaSelfTestKit::RefuseDeferred(Wd, TEXT("asda.UI.SelfTestMiniState"), bQuit)) { return; }   // Р-2: офлайн повторно — на отложенном старте
			// ★ 11.10.2026 (В3 S1, загрязнение пакета): павн прошлого самотеста с другим именем («СамТест») — тоже спавн с «Portrait»
			//   (проверка имени портрета ждёт его; в пакете прежде провал «имя: до «СамТест»»).
			if (AsdaSelfTestKit::NeedSelfSpawn(Wd, AsdaMiniTest::kSelfName))
			{
				GEngine->Exec(Wd, TEXT("Asda.Load.CurrentMap 3"));
				GEngine->Exec(Wd, *FString::Printf(TEXT("Asda.World.FakeSelfSpawn 256 256 %s"), AsdaMiniTest::kSelfName));
			}
			FTimerHandle H1;
			Wd->GetTimerManager().SetTimer(H1, FTimerDelegate::CreateLambda([WW, bQuit]()
			{
				UWorld* W2 = WW.Get();
				UAsdaMiniStateSubsystem* M = AsdaMiniTest::MiniOf(W2);
				if (!M) { return; }
				M->RunSelfTestSync();
				FTimerHandle H2;
				W2->GetTimerManager().SetTimer(H2, FTimerDelegate::CreateLambda([WW, bQuit]()
				{
					UWorld* W3 = WW.Get();
					UAsdaMiniStateSubsystem* M3 = AsdaMiniTest::MiniOf(W3);
					if (!M3) { AsdaCVarSave::RestoreAll(TEXT("asda.UI.SelfTestMiniState")); return; }   // ★ С1 (ревизия wave0_REVIEW.md, 11.10.2026): мир ушёл до итога — ключи вернуть и на этом выходе
					M3->RunSelfTestGeometry();
					M3->FinishSelfTest();
					AsdaSelfTestKit::Quit(W3, bQuit);
				}), 1.5f, false);
			}), 4.f, false);
		};
		if (Delay <= 0.f) { Start(); return; }
		FTimerHandle H;
		W->GetTimerManager().SetTimer(H, FTimerDelegate::CreateLambda(Start), Delay, false);
	}));
#endif // ASDA_DEV_CONSOLE
