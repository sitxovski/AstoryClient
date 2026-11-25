// Copyright (c) 2026 @sitxovski (Discord)
// ★★★ САМОТЕСТ ВОЛНЫ 1-А: СОУЛМЕЙТ — NPC 30023/30024/30025, ОКНО 9100, ПУНКТ 2002, ДОП-МЕНЮ, ОКНО ПОИСКА 8200 (06.10.2026). [ПОРТ]
//   Отчёт: `Reports/owner_1006/wave1A_soulmate.md`.
//
// ★ ЧТО ПРОВЕРЯЕТ (ожидания заданы ЯВНО по эталону, а не вызовом проверяемого кода).
//   A. NPC 106 «Статуя души» — кнопка меню 0 (`OnDialogButton(581)`, тот же вход, что у мыши) → код 30023:
//      Друга нет (слот −1) → разговор закрыт, окно 9100 видно, режим 2, подписи 12018/12019/2288/2289, поле пустое.
//      Контроль: слот 0 (граница `>= 0`) → 8505 в журнал и плашку 221 (белым, опкод-метка 30023), разговор открыт, 9100 нет.
//   B. Окно 9100 (кнопки — их собственный `OnClicked`, Enter — `OnTextCommitted(OnEnter)`), перехват исходящих:
//      рядом (≤ 10 тайлов) → 5200 ×1, payload[0] = 2, имя; окно скрыто, режим 1. Контроли: пусто / нет в мире / 11 тайлов /
//      регистр имени / своё имя — 5200 ×0 и окно остаётся; ровно 10 тайлов — уходит; «Отмена» — без отправки;
//      потеря фокуса поля — не подтверждение; повтор без ответа 5201 — уходит (защёлки у эталона нет).
//   C. Меню игрока 2002 (ПКМ по соседу → пункт своим `OnClicked`): 5200 ×1, режим 1, имя цели; при открытом 9100 —
//      режим 2 и 9100 закрывается (`editBoxClearAndHide`).
//   D. NPC 40 — кнопка 0 → 30024: слот −1 → ONEBUT (0, 24) loc 6035, разговор открыт; слот 5 → TWOBUT (1, 8036) loc 8036,
//      «Да» → 5204 ×1 (payload [0]=0, [1..4]=5) и защёлка; повтор → (0, 24) (гейт `sub_938F2C`); «Нет» → 5204 ×0.
//   E. 30025 (кнопки в данных нет — штатный диспетчер `RunCommandForCheck`): (1, 29) loc 8030 → «Да» → 5204; слот 0 → 6035.
//   F. Поиск 8200: доп-меню (строка «Меню Друга», `SimulateClick` сетки) → 5483 ×1 ([0] фильтр, [1] страница), окно НЕ
//      показано; два пакета 5484 (штатный `HandlePacket`) → 2 записи, окно всё ещё скрыто; 5485 (тело 0 б) → окно видно,
//      строки «Кандидат1/Кандидат2», страницы 1/2; доп-меню при видимом окне → скрыть, 5483 ×0; 5483 очищает список;
//      8260 «вперёд» → [1] = 2, 8258 → [1] = 1; 6588 вкладки Друга → 5483 ×1, окно не показано.
//   G. Гейт «души» (SelfSoulState 1): 5200 и 5204 режутся, 5483 — нет (у сервера `Send_SoulSearch` без `sub_52EEA0`).
//   H. ПУТЬ ИГРОКА ПО ЖИВОЙ ГЕОМЕТРИИ: через кадры — Slate-клик (`SimulateClickAt`) по прямоугольнику кнопки NPC 106 →
//      окно 9100; затем Slate-клик по «ОК» 9104 → 5200 ×1.
//   N. ★ 11.10.2026 (В0 аудита дублей №2): F14 — сердце соул-линка заводит ОДИН таймер полёта и снимает его, долетев и
//      при уходе цели из мира (счётчик `ActiveSoulHeartTimers` возвращается к исходному); N3 — разрыв пары 5207 при
//      СВОЁМ userNum 0 снимает своё кольцо и очищает свою запись (раньше условие `!= 0` это пропускало).
//   Всё состояние (userNum, соседи, карточка Друга, очередь окон) восстанавливается в конце.
//
// Консоль: `asda.Soul.SelfTestWave1A [delay=N] [quit]`. Только офлайн (`FAsdaOutgoingGate::RefuseDebugWhenOnline`).
//   Строки проверок — «САМОТЕСТ-1А», итог — «САМОТЕСТ-1А ИТОГ: ok=… fail=…».
#include "CoreMinimal.h"
#include "Test/AsdaSelfTestCtx.h"   // ★ R5-c (12.11.2026): общая точка самотестов вместо своих копий помощников
#include "Test/AsdaSelfTestInput.h"   // ★ R5-c (12.11.2026): общая точка самотестов вместо своих копий помощников

#if !UE_BUILD_SHIPPING
#include "AsdaBytes.h"   // ★ N6 (В1 дедупа №2): единые читалки little-endian
#include "AsdaFxNames.h"   // ★ дедуп №2 В1-C (F6): тег кольца соулмейта

#include "Test/AsdaSelfTestNet.h"   // ★ R1: тонкий доступ самотестов к сети (вместо AsdaNetSubsystem.h)
#include "AsdaOutgoingGate.h"                  // RefuseDebugWhenOnline — офлайн-гейт самотестов
#include "Test/AsdaSelfTestKit.h"   // ★ 11.10.2026 (В3 S1): общий каркас самотестов (аргументы, отказ, перехват, итог, выход)
#include "AsdaCVarSave.h"                     // SetForTest / RestoreAll — подмена ключа с возвратом значения и приоритета
#include "Login/AsdaLoginData.h"               // DecodeCp1251 / EncodeCp1251 — имя в payload 5200 и в записях 5484
#include "Npc/AsdaNpcDialogSubsystem.h"        // окно разговора 570: OpenForNpcId / OnDialogButton / DumpWindow / RunCommandForCheck
#include "UI/AsdaSoulmateSubsystem.h"          // проверяемая подсистема
#include "UI/AsdaPlayerMenuSubsystem.h"        // меню игрока 2000 — пункт 2002
#include "UI/AsdaExtraMenuSubsystem.h"         // доп-меню 74600 — строка «Меню Друга»
#include "UI/AsdaCommunitySubsystem.h"         // окно сообщества 6400 — кнопка 6588 вкладки Друга
#include "UI/AsdaWindowSubsystem.h"            // очередь окон-сообщений, SimulateClickAt, FindWindow
#include "UI/AsdaUiHelpers.h"                  // WidgetAbsRect — живая геометрия кнопки «ОК» 9104
#include "UI/AsdaNotice.h"                     // AsdaNotice::Loc — ожидаемые тексты
#include "World/AsdaWorldSubsystem.h"          // свой павн и фейковые соседи
#include "UI/AsdaInventorySubsystem.h"         // ★ R5-fix8-c (14.11.2026): сумка, которую возвращает закрытие окна 32000
#include "UI/AsdaDisassemblySubsystem.h"       // ★ R5-fix6-c (13.11.2026): закрыть окно 32000, оставленное соседом пачки
#include "World/AsdaPossessGate.h"             // AsdaSoul::BlockOutgoing — гейт «души»
#include "World/AsdaCoord.h"                   // мир ↔ тайл — соседи ставятся от своего тайла
#include "Pet/AsdaPetSubsystem.h"              // BeginSendCapture / EndSendCapture — перехват исходящих
#include "Data/AsdaExtraMenuRow.h"             // FAsdaExtraMenuRow::Cmd — поиск строки команды 1
#include "Components/Button.h"
#include "Components/EditableText.h"
#include "Components/TextBlock.h"
#include "Blueprint/UserWidget.h"
#include "Framework/Application/SlateApplication.h"   // хит-тест пути под курсором (этап H2)
#include "Layout/WidgetPath.h"                     // FWidgetPath
#include "UObject/UObjectIterator.h"                // TObjectIterator — владелец узла пути (этап H2)
#include "Engine/Engine.h"
#include "Engine/GameInstance.h"
#include "Engine/World.h"
#include "HAL/IConsoleManager.h"
#include "Math/Box2D.h"
#include "Misc/OutputDevice.h"
#include "Misc/OutputDeviceRedirector.h"
#include "TimerManager.h"

DEFINE_LOG_CATEGORY_STATIC(LogAsdaSoulWave1ATest, Log, All);

// ★ 11.10.2026 (В0 аудита дублей №2): друг `UAsdaWorldSubsystem` — доступ этапа N к закрытой части соул-линка
//   (сердца F14 и разрыв пары при userNum 0 — N3). Только чтение/подача штатных точек, своих копий логики нет.
struct FAsdaSoulWave1ATestAccess
{
	// Закрытый тип записи соулмейта мира — открытым псевдонимом для этапа N.
	using FSoul = UAsdaWorldSubsystem::FAsdaSoulState;
	// Запись соулмейт-состояния по userIdx (кольцо/партнёр/время следующего сердца).
	static TMap<int32, FSoul>& Souls(UAsdaWorldSubsystem* WS) { return WS->SoulByUserIdx; }
	// Штатный тик сердец (тот же, что зовёт мир каждый кадр).
	static void TickHearts(UAsdaWorldSubsystem* WS) { WS->TickSoulHearts(); }
	// Число живых таймеров полёта сердец.
	static int32 HeartTimers(const UAsdaWorldSubsystem* WS) { return WS->ActiveSoulHeartTimers; }
	// Штатный приёмник разрыва пары 5207.
	static void Break(UAsdaWorldSubsystem* WS, int32 Sub, const FString& Name) { WS->OnSoulmateBreak(Sub, Name); }
	// Штатное включение/выключение кольца на акторе.
	static void Ring(UAsdaWorldSubsystem* WS, AActor* A, bool bOn) { WS->ApplySoulRingToActor(A, bOn); }
	// ★ 29.10.2026 (баг «кольцо после разрыва»): кэш номера пары 6152 по charId — его читает свой спавн.
	static TMap<int32, int32>& SoulByChar(UAsdaWorldSubsystem* WS) { return WS->SoulIdxByCharId; }
};

namespace AsdaSoulWave1ATestLocal
{
	// Журнал строк лога (маршрутизатор сообщений пишет «[msg] категория → приёмники | цвет | «текст»»).
	class FSink : public FOutputDevice
	{
	public:
		TArray<FString> All;    // все строки
		FCriticalSection Lock;  // лог может прийти с любого потока
		virtual void Serialize(const TCHAR* V, ELogVerbosity::Type, const FName&) override
		{
			if (!V) { return; }
			FScopeLock L(&Lock);
			All.Add(V);
		}
		virtual bool CanBeUsedOnAnyThread() const override { return true; }
		virtual bool CanBeUsedOnMultipleThreads() const override { return true; }   // ★ 05.11.2026: НЕбуферизованный список UE 5.8 — строка доходит в момент записи (иначе гонка с потоком логов: строка после RemoveOutputDevice/проверки)
		// Очистить (с досылкой отложенных строк).
		void Reset() { if (GLog) { GLog->Flush(); } FScopeLock L(&Lock); All.Reset(); }
		// Есть ли строка, содержащая ВСЕ подстроки.
		bool Has(std::initializer_list<const TCHAR*> Needles)
		{
			if (GLog) { GLog->Flush(); }
			FScopeLock L(&Lock);
			for (const FString& S : All)
			{
				bool bAll = true;
				for (const TCHAR* N : Needles) { if (!S.Contains(N)) { bAll = false; break; } }
				if (bAll) { return true; }
			}
			return false;
		}
	};

	// Одна перехваченная отправка.
	struct FCap
	{
		int32 Op = 0;          // опкод
		TArray<uint8> P;       // payload (без конверта)
	};

	// Контекст многоэтапного прогона (живёт между таймерами).
	struct FCtx
	{
		int32 Ok = 0, Fail = 0;                 // пройдено / провалено
		TWeakObjectPtr<UWorld> W;               // мир прогона
		bool bQuit = false;                     // выйти после последнего этапа
		uint16 UserNumWas = 0;                  // свой userNum до прогона
		bool bKnownWas = false;                 // «userNum пришёл» до прогона
		uint8 SoulStateWas = 0;                 // SelfSoulState до прогона
		FString SelfNameWas;                    // своё имя в мире до прогона (★ R5-fix6-c (13.11.2026))
		bool bNameSet = false;                  // имя подменено тестом — вернуть в конце
		uint32 CapGen = 0;                      // 1 — свой перехват взведён, 0 — не стоит
		TArray<FCap> Caps;                      // перехваченные отправки
		TSharedPtr<FSink> Sink;                 // журнал строк лога
		int32 ZLinkBefore = 0;                  // z окна 9100 до клика по его телу (этап H2)
		bool bOkCovered = false;                // «ОК» был перекрыт чатом 2570 (этап H2)
		int32 HeartTimersBase = 0;              // ★ 11.10.2026: таймеров полёта сердец до этапа N (F14)
		int32 CharIdWas = 0;                    // ★ 29.10.2026: свой charId (`SelectedCharId`) до этапа N4
		bool bRingReset = false;                // ★ 29.10.2026: аргумент `ringreset` — этап N5 с полным сбросом мира (разрушает сессию)

		// Одна проверка: печать и счёт.
		void Expect(bool b, const FString& What)
		{
			if (b) { ++Ok; UE_LOG(LogAsdaSoulWave1ATest, Log, TEXT("САМОТЕСТ-1А ok   %s"), *What); }
			else   { ++Fail; UE_LOG(LogAsdaSoulWave1ATest, Error, TEXT("САМОТЕСТ-1А FAIL %s"), *What); }
		}
		// Сколько отправок с опкодом Op.
		int32 Count(int32 Op) const { int32 N = 0; for (const FCap& C : Caps) { if (C.Op == Op) { ++N; } } return N; }
		// Последняя отправка с опкодом Op (nullptr — не было).
		const FCap* Last(int32 Op) const { for (int32 i = Caps.Num() - 1; i >= 0; --i) { if (Caps[i].Op == Op) { return &Caps[i]; } } return nullptr; }
		// Байт I payload последней отправки Op (−1 — отправки нет или payload короче).
		int32 Byte(int32 Op, int32 I) const { const FCap* P = Last(Op); return (P && P->P.IsValidIndex(I)) ? (int32)P->P[I] : -1; }
	};

	// Тестовые константы.
	static constexpr uint16 kMe = 500;            // свой userNum
	static constexpr int32 kPeerNear  = 901;      // сосед в соседнем тайле
	static constexpr int32 kPeerFar   = 902;      // сосед в 11 тайлах по X (вне радиуса)
	static constexpr int32 kPeerEdge  = 903;      // сосед ровно в 10 тайлах по X (граница радиуса)
	static const TCHAR* kNearName = TEXT("ДругТест");      // имя ближнего соседа
	static const TCHAR* kFarName  = TEXT("ДалёкийДруг");   // имя дальнего соседа
	static const TCHAR* kEdgeName = TEXT("ГраницаДруг");   // имя соседа на границе

	// Поставить перехват ВСЕХ исходящих (в сеть не уходит ничего) — публичная точка `UAsdaPetSubsystem::BeginSendCapture`
	//   (тот же `SendCaptureForStand`, что у самотеста 6099); charId не подменяется (0).
	static void Arm(const TSharedRef<FCtx>& C, UGameInstance* GI)
	{
		C->Caps.Reset();
		UAsdaPetSubsystem* Pet = GI ? GI->GetSubsystem<UAsdaPetSubsystem>() : nullptr;
		TWeakPtr<FCtx> WC = C;
		C->CapGen = (Pet && Pet->BeginSendCapture([WC](int32 Op, const TArray<uint8>& Body, bool, TCHAR)
		{
			if (TSharedPtr<FCtx> S = WC.Pin())
			{
				FCap& Cp = S->Caps.AddDefaulted_GetRef();
				Cp.Op = Op;
				Cp.P = Body;
			}
		}, 0)) ? 1u : 0u;
	}
	// Снять свой перехват.
	static void Disarm(const TSharedRef<FCtx>& C, UGameInstance* GI)
	{
		UAsdaPetSubsystem* Pet = GI ? GI->GetSubsystem<UAsdaPetSubsystem>() : nullptr;
		if (Pet && C->CapGen) { Pet->EndSendCapture(); }
		C->CapGen = 0;
	}

	// Запись соулмейта 5110 (251 б): слот [0..3], имя партнёра с [63]. Подаётся ШТАТНЫМ приёмом.
	static void FeedSlot(UAsdaNetSubsystem* Net, int32 Slot)
	{
		TArray<uint8> B; B.SetNumZeroed(251);
		B[0] = (uint8)(Slot & 0xFF); B[1] = (uint8)((Slot >> 8) & 0xFF); B[2] = (uint8)((Slot >> 16) & 0xFF); B[3] = (uint8)((Slot >> 24) & 0xFF);
		FAsdaLoginData::EncodeCp1251(TEXT("Партнёр"), B.GetData() + 63, 19);
		AsdaTestNet::DebugFeedPacket(Net, 5110, B);
	}

	// Запись кандидата 5484 (33 б + «о себе»): уровень, профессия, имя, пол, userNum, charId, хвост.
	static void AppendCandidate(TArray<uint8>& B, const TCHAR* Name, int32 UserNum, int32 CharId, const char* Intro)
	{
		const int32 At = B.Num();
		const int32 IntroLen = (int32)FCStringAnsi::Strlen(Intro);
		B.AddZeroed(33 + IntroLen);
		uint8* R = B.GetData() + At;
		R[0] = 20;                                                     // уровень
		R[1] = 1;                                                      // профессия
		FAsdaLoginData::EncodeCp1251(Name, R + 3, 19);                 // имя
		R[23] = 1;                                                     // пол
		R[24] = (uint8)(UserNum & 0xFF); R[25] = (uint8)(UserNum >> 8); // userNum
		R[26] = (uint8)(CharId & 0xFF); R[27] = (uint8)(CharId >> 8); R[28] = (uint8)(CharId >> 16); R[29] = (uint8)(CharId >> 24);
		R[30] = 1;                                                     // участников
		R[31] = (uint8)IntroLen;                                       // длина «о себе»
		FMemory::Memcpy(R + 33, Intro, IntroLen);
	}

	// Текст живого TextBlock окна по имени узла (пусто — узла нет).
	static FString TextOf(const UUserWidget* Wnd, const TCHAR* Name)
	{
		const UTextBlock* T = Wnd ? Cast<UTextBlock>(Wnd->GetWidgetFromName(FName(Name))) : nullptr;
		return T ? T->GetText().ToString() : FString(TEXT("<нет узла>"));
	}

	// Нажать кнопку окна её СОБСТВЕННЫМ `OnClicked` (тот же реле-путь, что у мыши после Slate). false — кнопки нет.
	static bool Press(const UUserWidget* Wnd, const TCHAR* Name)
	{
		UButton* B = Wnd ? Cast<UButton>(Wnd->GetWidgetFromName(FName(Name))) : nullptr;
		if (!B) { return false; }
		B->OnClicked.Broadcast();
		return true;
	}

	// Поле имени 9101.
	static UEditableText* NameEdit(const UUserWidget* Wnd)
	{
		return Wnd ? Cast<UEditableText>(Wnd->GetWidgetFromName(TEXT("SOULMATE_NAME_INPUT"))) : nullptr;
	}

	// Имя из payload 5200 ([1..20] cp1251).
	static FString NameOf5200(const FCap* C)
	{
		return (C && C->P.Num() >= 21) ? FAsdaLoginData::DecodeCp1251(C->P.GetData() + 1, 20) : FString();
	}

	// Строка разбора окна 570 по тегу (`DumpWindow`), пусто — нет строки.
	using AsdaSelfTestInput::DumpLine;   // ★ R5-c (12.11.2026): общая точка (`Test/AsdaSelfTestInput.h`) — копия была дословной.

	// Прямоугольник из строки дампа «… rect=(x0,y0)-(x1,y1) …»; false — нет или нулевой.
	static bool ParseRect(const FString& Line, FBox2D& Out)
	{
		const int32 At = Line.Find(TEXT("rect=("));
		if (At == INDEX_NONE) { return false; }
		const FString Rest = Line.Mid(At + 6);
		FString A, B;
		if (!Rest.Split(TEXT(")-("), &A, &B)) { return false; }
		int32 CloseAt = INDEX_NONE;
		if (!B.FindChar(TEXT(')'), CloseAt)) { return false; }
		B = B.Left(CloseAt);
		FString Ax, Ay, Bx, By;
		if (!A.Split(TEXT(","), &Ax, &Ay) || !B.Split(TEXT(","), &Bx, &By)) { return false; }
		Out = FBox2D(FVector2D(FCString::Atod(*Ax), FCString::Atod(*Ay)), FVector2D(FCString::Atod(*Bx), FCString::Atod(*By)));
		return Out.GetSize().X > 0.0 && Out.GetSize().Y > 0.0;
	}

	static void StageGeoNpc(TSharedRef<FCtx> C);
	static void StageGeoLink(TSharedRef<FCtx> C);
	static void StageGeoRaise(TSharedRef<FCtx> C);
	static void StageGeoAfterRaise(TSharedRef<FCtx> C);
	static void StageGeoOk(TSharedRef<FCtx> C);
	static void StageSoulN1(TSharedRef<FCtx> C);
	static void StageSoulN2(TSharedRef<FCtx> C);
	static void StageSoulN3(TSharedRef<FCtx> C);
	static void StageSoulN4(TSharedRef<FCtx> C);
	static void StageSoulN5(TSharedRef<FCtx> C);
	static void Finish(TSharedRef<FCtx> C);

	// Запустить этап через Sec секунд мирового таймера (кадры идут, геометрия успевает отрисоваться).
	using AsdaSelfTestCtx::ByStage::After;   // ★ R5-c (12.11.2026): общая точка (`Test/AsdaSelfTestCtx.h`) — копия была дословной.

	// Этап 1 (синхронный): A…G. В конце — окно NPC 106 открыто и заведён этап геометрии H.
	static void Run(TSharedRef<FCtx> C)
	{
		UWorld* W = C->W.Get();
		UGameInstance* GI = W ? W->GetGameInstance() : nullptr;
		UAsdaNetSubsystem* Net = GI ? AsdaTestNet::Get(GI) : nullptr;
		UAsdaNpcDialogSubsystem* Dlg = GI ? GI->GetSubsystem<UAsdaNpcDialogSubsystem>() : nullptr;
		UAsdaSoulmateSubsystem* Soul = GI ? GI->GetSubsystem<UAsdaSoulmateSubsystem>() : nullptr;
		UAsdaWindowSubsystem* WM = GI ? GI->GetSubsystem<UAsdaWindowSubsystem>() : nullptr;
		UAsdaWorldSubsystem* WS = GI ? GI->GetSubsystem<UAsdaWorldSubsystem>() : nullptr;
		UAsdaPlayerMenuSubsystem* PM = GI ? GI->GetSubsystem<UAsdaPlayerMenuSubsystem>() : nullptr;
		UAsdaExtraMenuSubsystem* Etc = GI ? GI->GetSubsystem<UAsdaExtraMenuSubsystem>() : nullptr;
		if (!Net || !Dlg || !Soul || !WM || !WS || !PM || !Etc)
		{
			C->Expect(false, TEXT("нет подсистем (сеть/диалог/соулмейт/окна/мир/меню/доп-меню)"));
			Finish(C);
			return;
		}
		C->UserNumWas = AsdaTestNet::SelfUserNum(Net);
		C->bKnownWas = AsdaTestNet::bSelfUserNumKnown(Net);
		C->SoulStateWas = AsdaTestNet::SelfSoulState(Net);
		WM->ClearMsgBoxQueue(TEXT("самотест 1А: старт"));
		Soul->ResetSessionState(EAsdaSessionResetReason::LeftWorld);   // чистая карточка: слот −1, окна скрыты
		// ★ R5-fix6-c (13.11.2026): исходное состояние — без окна разборки 32000. При нём разговор с NPC молча не открывается
		//   (`regionWindow_OpenAndPathMove`); окно оставлял `asda.SelfTest.EntryWindows` — после него тест давал 60/25.
		//   ★ R5-fix8-c (14.11.2026): окно 32000 при закрытии возвращает сумку, если она была открыта до него, — закрываем и её (иначе
		//   сумка осталась бы открытой на весь прогон и соседям).
		if (UAsdaDisassemblySubsystem* Ds = GI->GetSubsystem<UAsdaDisassemblySubsystem>())
		{
			if (Ds->IsOpen())
			{
				UAsdaInventorySubsystem* Inv = GI->GetSubsystem<UAsdaInventorySubsystem>();
				const bool bBagWas = Inv && Inv->IsWindowOpen();   // сумка была открыта сама по себе — её не трогаем
				Ds->CloseWindow();
				if (Inv && !bBagWas && Inv->IsWindowOpen()) { Inv->CloseWindow(); }
			}
		}
		// Свой павн (офлайн — штатный крюк) и три соседа на известных тайлах.
		if (!::IsValid(WS->LocalPlayerActor.Get()) && GEngine)
		{
			GEngine->Exec(W, TEXT("Asda.Load.CurrentMap 3"));
			WS->DebugSpawnSelfAt(256, 256, TEXT("СамТест"));
		}
		AsdaTestNet::SelfUserNum(Net) = kMe; AsdaTestNet::bSelfUserNumKnown(Net) = true;
		AsdaTestNet::SetSelfSoulState(Net, 0);
		// Соседи — ОТ СВОЕГО тайла (павн мог стоять где угодно): +1, +11 (вне радиуса) и +10 (граница) тайлов по X.
		FIntPoint MyTile(256, 256);
		if (const AActor* Me = WS->LocalPlayerActor.Get()) { MyTile = AsdaCoord::GameToTile(AsdaCoord::UeToGame2D(Me->GetActorLocation())); }
		auto TileCenter = [&MyTile](int32 Dx) { return AsdaCoord::TileCenterGame(MyTile.X + Dx, MyTile.Y); };
		WS->DebugSpawnFakePeer(kPeerNear, kNearName, TileCenter(1));
		WS->DebugSpawnFakePeer(kPeerFar,  kFarName,  TileCenter(11));
		WS->DebugSpawnFakePeer(kPeerEdge, kEdgeName, TileCenter(10));
		UE_LOG(LogAsdaSoulWave1ATest, Log, TEXT("САМОТЕСТ-1А инфо: свой тайл (%d,%d), соседи +1/+11/+10 по X"), MyTile.X, MyTile.Y);
		C->Expect(::IsValid(WS->LocalPlayerActor.Get()) && WS->FindPlayerActorByName(kNearName) && WS->FindPlayerActorByName(kFarName),
			TEXT("подготовка: свой павн и соседи в мире"));
		// ★ R5-fix6-c (13.11.2026): своё имя задаёт САМ тест (штатный тестовый доступ мира). Прежде имя было только у павна, поднятого
		//   этим же тестом (`DebugSpawnSelfAt … «СамТест»`); в пачке павн поднимал сосед, имя было пустым, и «B5 контроль»
		//   пропускался — 79 проверок против 80 одиночно. Прежнее имя возвращается в `Finish`.
		C->SelfNameWas = WS->GetSelfCharName(); C->bNameSet = true;
		WS->SetSelfCharNameForSelfTest(TEXT("СамТест"));
		const FString SelfName = WS->GetSelfCharName();   // своё имя (для контроля «это я»)
		C->Sink->Reset();
		Arm(C, GI);
		C->Expect(C->CapGen != 0, TEXT("перехват исходящих взведён"));

		// ════════════════ I. «Первая встреча» (ревизия 1-А §7): страница 121 только при первой встрече с NPC типа 0 ════════════════
		//   Ожидания — из данных `NpcQuestScript.csv`: у Эллы (107, тип 0) запись 121 → страница 63, запись 122 → страница 64,
		//   остальные записи требуют квестов/уровня ≥ 4 (офлайн их нет) ⇒ перебор выбирает 64.
		Dlg->DebugResetNpcMet();
		C->Caps.Reset();
		const bool bMet107 = Dlg->IsNpcMet(107);          // до первой встречи
		const bool bOpen1 = Dlg->OpenForNpcId(107);        // первая встреча
		const int32 Talk1 = Dlg->GetCurrentTalkId();       // её страница
		C->Expect(!bMet107 && bOpen1 && Talk1 == 63,
			FString::Printf(TEXT("I1: первая встреча с Эллой (тип 0) → страница 121 = talkId 63 (есть %d)"), Talk1));
		C->Expect(C->Count(5045) == 0, TEXT("I1: при открытии 5045 не шлётся"));
		Dlg->Close();
		{
			const FCap* P = C->Last(5045);
			const int32 Id = (P && P->P.Num() == 2) ? (int32)AsdaBytes::RdU16(P->P.GetData()) : -1;   // ★ N6 (В1 дедупа №2): формула чтения — `AsdaBytes`
			C->Expect(C->Count(5045) == 1 && Id == 107 && Dlg->IsNpcMet(107),
				FString::Printf(TEXT("I2: уход от Эллы → 5045 ×%d, тело %d б, npcId %d, встреча отмечена %d (ждём ×1, 2, 107, 1)"),
					C->Count(5045), P ? P->P.Num() : -1, Id, Dlg->IsNpcMet(107) ? 1 : 0));
		}
		C->Caps.Reset();
		const bool bOpen2 = Dlg->OpenForNpcId(107);        // повторная встреча
		const int32 Talk2 = Dlg->GetCurrentTalkId();       // её страница
		C->Expect(bOpen2 && Talk2 == 64,
			FString::Printf(TEXT("I3: повторная встреча → перебор записей, talkId 64 (запись 122), есть %d"), Talk2));
		Dlg->Close();
		C->Expect(C->Count(5045) == 0, TEXT("I3 контроль: повторный уход → 5045 ×0"));
		const bool bMet106 = Dlg->IsNpcMet(106);          // статуя до встречи
		const bool bOpen3 = Dlg->OpenForNpcId(106);        // первая встреча со статуей
		const int32 Talk3 = Dlg->GetCurrentTalkId();       // её страница
		C->Expect(!bMet106 && bOpen3 && Talk3 == 64,
			FString::Printf(TEXT("I4 контроль: «Статуя души» (тип 3) → перебор с первой встречи, talkId 64 (есть %d)"), Talk3));
		Dlg->Close();
		C->Expect(C->Count(5045) == 0 && !Dlg->IsNpcMet(106), TEXT("I4 контроль: уход от NPC ненулевого типа → 5045 ×0, встреча не отмечается"));
		{
			AsdaTestNet::SetSelfSoulState(Net, 1);
			const bool b5045 = AsdaSoul::BlockOutgoing(Net, 5045);
			AsdaTestNet::SetSelfSoulState(Net, 0);
			C->Expect(b5045 && !AsdaSoul::BlockOutgoing(Net, 5045), TEXT("I5: гейт «души» режет 5045 (сервер `case 0x13B5` под sub_52EEA0), без «души» — нет"));
		}
		C->Caps.Reset();

		// ════════════════ A. NPC 106 → кнопка 0 → 30023 ════════════════
		C->Expect(Soul->GetFriendSlot() == -1, FString::Printf(TEXT("A0: слот Друга до прогона −1 (есть %d)"), Soul->GetFriendSlot()));
		C->Expect(Dlg->OpenForNpcId(106), TEXT("A1: окно разговора с NPC 106 «Статуя души» открылось"));
		{
			const FString B0 = DumpLine(Dlg->DumpWindow(), TEXT("NPC_SHOP_BUT00"));
			C->Expect(B0.Contains(TEXT("видим=1")) && B0.Contains(TEXT("код=30023")), FString::Printf(TEXT("A1: кнопка 0 видна, код 30023: %s"), *B0.TrimStart()));
		}
		Dlg->OnDialogButton(581);
		UUserWidget* Link = Soul->GetLinkWindowForCheck();
		C->Expect(!Dlg->IsDialogOpen(), TEXT("A2: Друга нет → разговор закрыт (UI_resetAllWindowsAndLeave_5045)"));
		C->Expect(Link && WM->IsWindowVisible(TEXT("SOULMATE_LINK_WIN")), TEXT("A2: окно 9100 видно"));
		C->Expect(Soul->GetLinkWinMode() == 2, FString::Printf(TEXT("A2: режим +1900 = 2 (есть %u)"), (uint32)Soul->GetLinkWinMode()));
		C->Expect(TextOf(Link, TEXT("SOULMATE_LINK_TXT0")) == AsdaNotice::Loc(GI, 12018) && TextOf(Link, TEXT("SOULMATE_LINK_TXT1")) == AsdaNotice::Loc(GI, 12019),
			FString::Printf(TEXT("A2: подписи 9129/9130 = loc 12018/12019: «%s» / «%s»"), *TextOf(Link, TEXT("SOULMATE_LINK_TXT0")), *TextOf(Link, TEXT("SOULMATE_LINK_TXT1"))));
		C->Expect(TextOf(Link, TEXT("SOULMATE_LINK_BTN1_Text")) == AsdaNotice::Loc(GI, 2288) && TextOf(Link, TEXT("SOULMATE_LINK_BTN0_Text")) == AsdaNotice::Loc(GI, 2289),
			FString::Printf(TEXT("A2: подписи 9104/9103 = loc 2288/2289: «%s» / «%s»"), *TextOf(Link, TEXT("SOULMATE_LINK_BTN1_Text")), *TextOf(Link, TEXT("SOULMATE_LINK_BTN0_Text"))));
		C->Expect(NameEdit(Link) && NameEdit(Link)->GetText().IsEmpty(), TEXT("A2: поле 9101 пустое"));
		C->Expect(C->Count(5200) == 0, TEXT("A2: открытие окна ничего не шлёт"));
		Soul->CloseLinkWindow();
		// Контроль: слот 0 — граница `>= 0` у 30023 (у 30024/30025 тот же 0 — «Друга нет»).
		FeedSlot(Net, 0);
		C->Expect(Soul->GetFriendSlot() == 0, FString::Printf(TEXT("A3 подготовка: 5110 → слот 0 (есть %d)"), Soul->GetFriendSlot()));
		C->Sink->Reset();
		Dlg->OpenForNpcId(106);
		Dlg->OnDialogButton(581);
		const FString L8505 = AsdaNotice::Loc(GI, 8505);
		C->Expect(Dlg->IsDialogOpen() && !WM->IsWindowVisible(TEXT("SOULMATE_LINK_WIN")),
			TEXT("A3: слот 0 → разговор открыт, 9100 НЕ открыто"));
		C->Expect(C->Sink->Has({ TEXT("[msg] Social"), TEXT("сисчат"), TEXT("#FFFFFF"), *L8505 })
			&& C->Sink->Has({ TEXT("[msg] Social"), TEXT("WARMESSAGE221"), TEXT("#FFFFFF"), TEXT("опкод=30023"), *L8505 }),
			FString::Printf(TEXT("A3: 8505 «%s» — журнал И плашка 221, белым"), *L8505));
		Dlg->Close();

		// ════════════════ B. Окно 9100: «ОК» / Enter / «Отмена» ════════════════
		FeedSlot(Net, -1);
		C->Expect(Soul->GetFriendSlot() == -1, TEXT("B подготовка: слот −1"));
		auto OpenLink = [&]() { Soul->OpenLinkWindow(); Link = Soul->GetLinkWindowForCheck(); };
		auto TypeName = [&](const TCHAR* N) { if (UEditableText* E = NameEdit(Link)) { E->SetText(FText::FromString(N)); } };
		auto LinkShown = [&]() { return WM->IsWindowVisible(TEXT("SOULMATE_LINK_WIN")); };
		OpenLink();
		C->Caps.Reset();
		TypeName(TEXT(""));
		C->Expect(Press(Link, TEXT("SOULMATE_LINK_BTN1")), TEXT("B1: кнопка 9104 найдена"));
		C->Expect(C->Count(5200) == 0 && LinkShown(), TEXT("B1 контроль: пустое имя → 5200 ×0, окно осталось"));
		TypeName(TEXT("НетТакого"));
		Press(Link, TEXT("SOULMATE_LINK_BTN1"));
		C->Expect(C->Count(5200) == 0 && LinkShown(), TEXT("B2 контроль: имени нет в мире → 5200 ×0, окно осталось (молча)"));
		TypeName(kFarName);
		Press(Link, TEXT("SOULMATE_LINK_BTN1"));
		C->Expect(C->Count(5200) == 0 && LinkShown(), TEXT("B3 контроль: сосед в 11 тайлах → 5200 ×0 (|Δ| ≤ 10)"));
		TypeName(TEXT("другтест"));
		Press(Link, TEXT("SOULMATE_LINK_BTN1"));
		C->Expect(C->Count(5200) == 0 && LinkShown(), TEXT("B4 контроль: имя в другом регистре → 5200 ×0 (strcmp эталона)"));
		// ★ R5-fix6-c (13.11.2026): проверка выполняется всегда (имя задано тестом выше) — число проверок не зависит от соседей пачки.
		TypeName(*SelfName);
		Press(Link, TEXT("SOULMATE_LINK_BTN1"));
		C->Expect(!SelfName.IsEmpty() && C->Count(5200) == 0 && LinkShown(), FString::Printf(TEXT("B5 контроль: своё имя «%s» → 5200 ×0"), *SelfName));
		// Потеря фокуса — НЕ подтверждение.
		TypeName(kNearName);
		if (UEditableText* E = NameEdit(Link)) { E->OnTextCommitted.Broadcast(E->GetText(), ETextCommit::OnUserMovedFocus); }
		C->Expect(C->Count(5200) == 0 && LinkShown(), TEXT("B6 контроль: потеря фокуса поля → 5200 ×0"));
		// Рядом — «ОК».
		Press(Link, TEXT("SOULMATE_LINK_BTN1"));
		{
			const FCap* P = C->Last(5200);
			C->Expect(C->Count(5200) == 1 && P && P->P.Num() == 153 && P->P[0] == 2 && NameOf5200(P) == kNearName,
				FString::Printf(TEXT("B7: сосед рядом, «ОК» → 5200 ×%d, payload %d б, режим %d, имя «%s» (ждём ×1, 153, 2, «%s»)"),
					C->Count(5200), P ? P->P.Num() : -1, P && P->P.Num() ? P->P[0] : -1, *NameOf5200(P), kNearName));
			C->Expect(!LinkShown() && Soul->GetLinkWinMode() == 1, TEXT("B7: после отправки окно скрыто, режим 1 (editBoxClearAndHide)"));
		}
		// Повтор без ответа 5201 — уходит (защёлки у эталона нет), через Enter.
		OpenLink();
		C->Caps.Reset();
		TypeName(kNearName);
		if (UEditableText* E = NameEdit(Link)) { E->OnTextCommitted.Broadcast(E->GetText(), ETextCommit::OnEnter); }
		C->Expect(C->Count(5200) == 1 && C->Byte(5200, 0) == 2, TEXT("B8: повтор без 5201, Enter в поле → 5200 ×1 (защёлки нет)"));
		// Граница: ровно 10 тайлов — уходит.
		OpenLink();
		C->Caps.Reset();
		TypeName(kEdgeName);
		Press(Link, TEXT("SOULMATE_LINK_BTN1"));
		C->Expect(C->Count(5200) == 1 && NameOf5200(C->Last(5200)) == kEdgeName, TEXT("B9: сосед ровно в 10 тайлах → 5200 ×1"));
		// «Отмена».
		OpenLink();
		C->Caps.Reset();
		TypeName(kNearName);
		Press(Link, TEXT("SOULMATE_LINK_BTN0"));
		C->Expect(C->Count(5200) == 0 && !LinkShown() && Soul->GetLinkWinMode() == 1, TEXT("B10: «Отмена» 9103 → окно скрыто, режим 1, 5200 ×0"));

		// ════════════════ C. Меню игрока 2002 ════════════════
		C->Caps.Reset();
		PM->NotifyRightPress(kPeerNear);
		const bool bMenu = PM->NotifyRightRelease(kPeerNear, false);
		PM->ClickByNodeName(TEXT("TRANTOOL_BUT02"));
		{
			const FCap* P = C->Last(5200);
			C->Expect(bMenu && C->Count(5200) == 1 && P && P->P[0] == 1 && NameOf5200(P) == kNearName && !PM->IsMenuOpen(),
				FString::Printf(TEXT("C1: меню по соседу, пункт 2002 → 5200 ×%d, режим %d, имя «%s» (ждём ×1, 1, «%s»; прежде режим 0)"),
					C->Count(5200), P ? P->P[0] : -1, *NameOf5200(P), kNearName));
		}
		OpenLink();
		C->Caps.Reset();
		PM->NotifyRightPress(kPeerNear); PM->NotifyRightRelease(kPeerNear, false);
		PM->ClickByNodeName(TEXT("TRANTOOL_BUT02"));
		C->Expect(C->Count(5200) == 1 && C->Byte(5200, 0) == 2 && !LinkShown() && Soul->GetLinkWinMode() == 1,
			TEXT("C2: при открытом 9100 пункт 2002 → режим 2, окно 9100 закрыто (editBoxClearAndHide)"));

		// ════════════════ D. NPC 40 → кнопка 0 → 30024 ════════════════
		WM->ClearMsgBoxQueue(TEXT("самотест 1А: D"));
		C->Caps.Reset();
		C->Expect(Dlg->OpenForNpcId(40), TEXT("D0: окно разговора с NPC 40 открылось"));
		{
			const FString B0 = DumpLine(Dlg->DumpWindow(), TEXT("NPC_SHOP_BUT00"));
			C->Expect(B0.Contains(TEXT("видим=1")) && B0.Contains(TEXT("код=30024")), FString::Printf(TEXT("D0: кнопка 0 видна, код 30024: %s"), *B0.TrimStart()));
		}
		Dlg->OnDialogButton(581);
		C->Expect(WM->IsMsgBoxQueued(0, 24) && WM->GetMsgBoxQueueTopText() == AsdaNotice::Loc(GI, 6035) && !WM->IsMsgBoxQueued(1, 8036) && Dlg->IsDialogOpen(),
			FString::Printf(TEXT("D1: слот −1 → окно (0, 24) 6035 «%s», подтверждения нет, разговор открыт"), *WM->GetMsgBoxQueueTopText()));
		WM->ClearMsgBoxQueue(TEXT("самотест 1А: D1"));
		FeedSlot(Net, 5);
		if (!Dlg->IsDialogOpen()) { Dlg->OpenForNpcId(40); }
		Dlg->OnDialogButton(581);
		C->Expect(WM->IsMsgBoxQueued(1, 8036) && WM->GetMsgBoxQueueTopText() == AsdaNotice::Loc(GI, 8036) && C->Count(5204) == 0,
			FString::Printf(TEXT("D2: слот 5 → подтверждение (1, 8036) «%s», 5204 до ответа ×0"), *WM->GetMsgBoxQueueTopText().Left(40)));
		WM->AnswerMsgBox(true, true);
		{
			const FCap* P = C->Last(5204);
			const int32 SlotSent = (P && P->P.Num() >= 5) ? (P->P[1] | (P->P[2] << 8) | (P->P[3] << 16) | (P->P[4] << 24)) : -999;
			C->Expect(C->Count(5204) == 1 && P && P->P.Num() == 5 && P->P[0] == 0 && SlotSent == 5 && Soul->IsBreakRequestInFlight(),
				FString::Printf(TEXT("D3: «Да» → 5204 ×%d, [0]=%d, слот %d, защёлка %d (ждём ×1, 0, 5, 1)"),
					C->Count(5204), P && P->P.Num() ? P->P[0] : -1, SlotSent, Soul->IsBreakRequestInFlight() ? 1 : 0));
		}
		Dlg->OnDialogButton(581);
		C->Expect(WM->IsMsgBoxQueued(0, 24) && !WM->IsMsgBoxQueued(1, 8036) && C->Count(5204) == 1,
			TEXT("D4 контроль: запрос в полёте → (0, 24) 6035, подтверждения нет (гейт sub_938F2C)"));
		WM->ClearMsgBoxQueue(TEXT("самотест 1А: D4"));
		Soul->ResetSessionState(EAsdaSessionResetReason::MapChanged);   // снимает защёлку, карточку бережёт
		C->Expect(!Soul->IsBreakRequestInFlight() && Soul->GetFriendSlot() == 5, TEXT("D5 подготовка: защёлка снята, слот 5 цел"));
		Dlg->OnDialogButton(581);
		WM->AnswerMsgBox(true, false);
		C->Expect(C->Count(5204) == 1 && !Soul->IsBreakRequestInFlight(), TEXT("D5 контроль: «Нет» → 5204 не ушёл"));
		Dlg->Close();

		// ════════════════ E. 30025 (кнопки в данных нет) ════════════════
		WM->ClearMsgBoxQueue(TEXT("самотест 1А: E"));
		C->Caps.Reset();
		Dlg->RunCommandForCheck(30025, -1);
		C->Expect(WM->IsMsgBoxQueued(1, 29) && WM->GetMsgBoxQueueTopText() == AsdaNotice::Loc(GI, 8030),
			FString::Printf(TEXT("E1: 30025 → подтверждение (1, 29) loc 8030 «%s»"), *WM->GetMsgBoxQueueTopText().Left(40)));
		WM->AnswerMsgBox(true, true);
		C->Expect(C->Count(5204) == 1 && C->Byte(5204, 1) == 5, TEXT("E2: «Да» → 5204 ×1, слот 5"));
		WM->ClearMsgBoxQueue(TEXT("самотест 1А: E2"));
		Soul->ResetSessionState(EAsdaSessionResetReason::MapChanged);
		FeedSlot(Net, 0);
		C->Caps.Reset();
		Dlg->RunCommandForCheck(30025, -1);
		C->Expect(WM->IsMsgBoxQueued(0, 24) && !WM->IsMsgBoxQueued(1, 29) && C->Count(5204) == 0,
			TEXT("E3 контроль: слот 0 → (0, 24) 6035 (граница `<= 0`), 5204 ×0"));
		WM->ClearMsgBoxQueue(TEXT("самотест 1А: E3"));
		FeedSlot(Net, -1);

		// ════════════════ F. Поиск 8200: доп-меню → 5483 → 5484×2 → 5485 ════════════════
		int32 EtcRow = -1;   // строка «Меню Друга» (команда 1)
		for (int32 i = 0; i < Etc->GetRecordCount(); ++i)
		{
			if (const FAsdaExtraMenuRow* R = Etc->FindByIndex(i)) { if (R->Cmd == 1) { EtcRow = i; break; } }
		}
		C->Expect(EtcRow >= 0, FString::Printf(TEXT("F0: строка «Меню Друга» (команда 1) в таблице доп-меню: %d"), EtcRow));
		Soul->CloseSearchWindow();
		C->Caps.Reset();
		Etc->SimulateClick(EtcRow);
		{
			const FCap* P = C->Last(5483);
			C->Expect(C->Count(5483) == 1 && P && P->P.Num() == 2 && P->P[0] == 0 && P->P[1] == 1 && !Soul->IsSearchWindowShown(),
				FString::Printf(TEXT("F1: доп-меню → 5483 ×%d [%d,%d] (ждём ×1 [0 фильтр, 1 страница]), окно скрыто до 5485"),
					C->Count(5483), P && P->P.Num() > 0 ? P->P[0] : -1, P && P->P.Num() > 1 ? P->P[1] : -1));
		}
		{
			TArray<uint8> A; A.Add(2); A.Add(1); AppendCandidate(A, TEXT("Кандидат1"), 701, 7001, "abc");
			TArray<uint8> B; B.Add(2); B.Add(1); AppendCandidate(B, TEXT("Кандидат2"), 702, 7002, "de");
			AsdaTestNet::DebugFeedPacket(Net, 5484, A);
			AsdaTestNet::DebugFeedPacket(Net, 5484, B);
		}
		C->Expect(Soul->GetSearchCandidateNum() == 2 && !Soul->IsSearchWindowShown(),
			FString::Printf(TEXT("F2: два пакета 5484 → записей %d (ждём 2 — дописываются), окно скрыто"), Soul->GetSearchCandidateNum()));
		AsdaTestNet::DebugFeedPacket(Net, 5485, TArray<uint8>());
		{
			UUserWidget* Find = Soul->GetSearchWindowForCheck();
			C->Expect(Soul->IsSearchWindowShown(), TEXT("F3: 5485 → окно 8200 видно"));
			C->Expect(TextOf(Find, TEXT("Find_SMate_ListName01")) == TEXT("Кандидат1") && TextOf(Find, TEXT("Find_SMate_ListName02")) == TEXT("Кандидат2")
				&& TextOf(Find, TEXT("Find_SMate_ListName03")).IsEmpty(),
				FString::Printf(TEXT("F3: строки «%s» / «%s» / «%s»"), *TextOf(Find, TEXT("Find_SMate_ListName01")),
					*TextOf(Find, TEXT("Find_SMate_ListName02")), *TextOf(Find, TEXT("Find_SMate_ListName03"))));
			C->Expect(TextOf(Find, TEXT("Find_SMate_CurrPage")) == TEXT("1") && TextOf(Find, TEXT("Find_SMate_LastPage")) == TEXT("2"),
				TEXT("F3: страницы 1 / 2"));
			// 8260 «вперёд» → [1] = 2; 8258 «в начало» → [1] = 1.
			C->Caps.Reset();
			Press(Find, TEXT("Find_SMate_NextBtn"));
			const FCap* P1 = C->Last(5483);
			C->Expect(P1 && P1->P.Num() == 2 && P1->P[1] == 2 && Soul->GetSearchCandidateNum() == 0,
				FString::Printf(TEXT("F4: 8260 → 5483 страница %d (ждём 2), список очищен отправкой (%d)"), P1 && P1->P.Num() > 1 ? P1->P[1] : -1, Soul->GetSearchCandidateNum()));
			Press(Find, TEXT("Find_SMate_FirstBtn"));
			const FCap* P2 = C->Last(5483);
			C->Expect(P2 && P2->P.Num() == 2 && P2->P[1] == 1, TEXT("F4 контроль: 8258 → 5483 страница 1"));
		}
		C->Caps.Reset();
		Etc->SimulateClick(EtcRow);
		C->Expect(!Soul->IsSearchWindowShown() && C->Count(5483) == 0, TEXT("F5: доп-меню при видимом окне → скрыто, 5483 ×0"));
		AsdaTestNet::DebugFeedPacket(Net, 5485, TArray<uint8>());
		C->Expect(Soul->IsSearchWindowShown() && TextOf(Soul->GetSearchWindowForCheck(), TEXT("Find_SMate_ListName01")).IsEmpty(),
			TEXT("F6: 5485 при пустом списке → окно видно, строки пустые"));
		Soul->CloseSearchWindow();
		// 6588 «Найти Друга» вкладки 0 окна 6400 — его собственным `OnClicked`.
		if (UAsdaCommunitySubsystem* Comm = GI->GetSubsystem<UAsdaCommunitySubsystem>())
		{
			// ★ R5-fix3-c (13.11.2026): вкладку 0 ставит сам тест штатным `OpenOnTab(0)` (хоткей 35 оригинала). Прежний `OpenWindow()`
			//   открывал окно на вкладке, оставшейся от предыдущего самотеста пачки: после клановых оставалась вкладка 2,
			//   «Найти Друга» вкладки 0 не срабатывала (5483 ×0) и F7 падала, хотя одиночно проходила.
			Comm->OpenOnTab(0);
			UUserWidget* CommW = WM->FindWindow(TEXT("COMMUNITY_WIN"));
			C->Caps.Reset();
			const bool bBtn = Press(CommW, TEXT("Soul_Find_Btn"));
			C->Expect(bBtn && C->Count(5483) == 1 && !Soul->IsSearchWindowShown(),
				FString::Printf(TEXT("F7: 6588 → 5483 ×%d, окно до 5485 скрыто (кнопка %s)"), C->Count(5483), bBtn ? TEXT("есть") : TEXT("НЕТ")));
			Comm->CloseWindow();
		}
		else
		{
			C->Expect(false, TEXT("F7: нет подсистемы сообщества"));
		}

		// ════════════════ G. Гейт «души» ════════════════
		AsdaTestNet::SetSelfSoulState(Net, 1);
		const bool b5200 = AsdaSoul::BlockOutgoing(Net, 5200), b5204 = AsdaSoul::BlockOutgoing(Net, 5204), b5483 = AsdaSoul::BlockOutgoing(Net, 5483);
		AsdaTestNet::SetSelfSoulState(Net, 0);
		const bool b5200Off = AsdaSoul::BlockOutgoing(Net, 5200);
		C->Expect(b5200 && b5204 && !b5483 && !b5200Off,
			FString::Printf(TEXT("G: «душа» режет 5200=%d 5204=%d, НЕ режет 5483=%d; без «души» 5200=%d (ждём 1/1/0/0)"),
				b5200 ? 1 : 0, b5204 ? 1 : 0, b5483 ? 1 : 0, b5200Off ? 1 : 0));
		Disarm(C, GI);

		// ════════════════ Z. Сжатие полосы подъёма (повторная ревизия 1А-5 п.3) ════════════════
		//   Два окна (9100 и 8200) поднимаются ПОПЕРЕМЕННО 300 раз, стартуя у потолка (8600 — «долгая игра»). Ожидание:
		//   каждый поднятый — строго выше другого и ниже 8599 (с ревизии подъёма: над ним полоса AlwaysTop); чат 2570 и приоритетное
		//   окно (8300 в полосе 8800) не сдвинулись.
		//   Контрольная пара: тот же прогон без сжатия (`asda.UI.ZCompact 0`) обязан упереться в потолок и дать провалы.
		{
			Soul->OpenLinkWindow(); Soul->OpenSearchWindow(); Soul->OpenIntroWindow();
			const FName kA(TEXT("SOULMATE_LINK_WIN")), kB(TEXT("Find_Soulmate_Win")), kP(TEXT("My_Introduction_Win")), kChat(TEXT("CHAT_NEW_SYSTEM"));
			const int32 ZaWas = WM->GetWindowZOrder(kA), ZbWas = WM->GetWindowZOrder(kB), ZpWas = WM->GetWindowZOrder(kP);
			IConsoleVariable* CvZ = IConsoleManager::Get().FindConsoleVariable(TEXT("asda.UI.ZCompact"));
			const int32 CvZWas = CvZ ? CvZ->GetInt() : 1;
			// Один прогон: возвращает число «подъём не сработал», максимум z и сдвиги чата/приоритетного окна.
			auto RunRaises = [&](int32 bCompact, int32& OutBad, int32& OutMaxZ, bool& bOutChatSame, bool& bOutPrioSame)
			{
				// ★ ИСПРАВЛЕНИЕ 11.10.2026 (В0, N1(д)): подмена с запоминанием исходного значения и приоритета.
				if (CvZ) { AsdaCVarSave::SetForTest(TEXT("asda.UI.ZCompact"), bCompact); }
				WM->SetWindowPriority(kP, true);
				const int32 ZChat0 = WM->GetWindowZOrder(kChat), ZPrio0 = WM->GetWindowZOrder(kP);
				// ★ Ревизия подъёма окон (07.10.2026): потолок обычного слоя — kAlwaysTopZ − 1 (8599), над ним полоса AlwaysTop.
				WM->SetWindowZOrder(kA, UAsdaWindowSubsystem::kAlwaysTopZ - 1); WM->SetWindowZOrder(kB, UAsdaWindowSubsystem::kAlwaysTopZ - 2);
				OutBad = 0; OutMaxZ = 0;
				for (int32 i = 0; i < 300; ++i)
				{
					const FName Up = (i % 2 == 0) ? kB : kA, Other = (i % 2 == 0) ? kA : kB;
					WM->BringWindowToFront(Up);
					const int32 Zu = WM->GetWindowZOrder(Up), Zo = WM->GetWindowZOrder(Other);
					OutMaxZ = FMath::Max(OutMaxZ, Zu);
					if (!(Zu > Zo && Zu < UAsdaWindowSubsystem::kPriorityZ)) { ++OutBad; }
				}
				bOutChatSame = WM->GetWindowZOrder(kChat) == ZChat0;
				bOutPrioSame = WM->GetWindowZOrder(kP) == ZPrio0 && ZPrio0 >= UAsdaWindowSubsystem::kPriorityZ;
				WM->SetWindowPriority(kP, false);
			};
			int32 Bad1 = 0, Max1 = 0, Bad0 = 0, Max0 = 0; bool bChat1 = false, bPrio1 = false, bChat0 = false, bPrio0 = false;
			RunRaises(1, Bad1, Max1, bChat1, bPrio1);
			C->Expect(Bad1 == 0 && Max1 < UAsdaWindowSubsystem::kAlwaysTopZ - 1 && bChat1 && bPrio1,
				FString::Printf(TEXT("Z1: сжатие ВКЛ — 300 подъёмов: провалов порядка %d, макс z %d (< 8599), чат на месте %d, приоритетное на месте %d"),
					Bad1, Max1, bChat1 ? 1 : 0, bPrio1 ? 1 : 0));
			RunRaises(0, Bad0, Max0, bChat0, bPrio0);
			C->Expect(Bad0 > 0 && Max0 == UAsdaWindowSubsystem::kAlwaysTopZ - 1,
				FString::Printf(TEXT("Z2 контроль: сжатие ВЫКЛ — окна упёрлись в потолок %d, провалов порядка %d (ждём > 0)"), Max0, Bad0));
			// ★ ИСПРАВЛЕНИЕ 11.10.2026 (В0, N1(д)): было `Set(CvZWas, ECVF_SetByConsole)` — значение то же, но приоритет
			//   оставался «из консоли»; теперь возвращаются и значение, и исходный SetBy.
			AsdaCVarSave::RestoreAll(TEXT("asda.Soul.SelfTestWave1A"));
			(void)CvZWas;
			WM->SetWindowZOrder(kA, ZaWas); WM->SetWindowZOrder(kB, ZbWas); WM->SetWindowZOrder(kP, ZpWas);
			Soul->CloseLinkWindow(); Soul->CloseSearchWindow(); Soul->CloseIntroWindow();
		}

		// ── к этапу H: окно NPC 106 открыто, геометрию снимаем через кадры ──
		WM->ClearMsgBoxQueue(TEXT("самотест 1А: H"));
		C->Expect(Dlg->OpenForNpcId(106), TEXT("H0: окно разговора с NPC 106 открыто для клика по геометрии"));
		After(C, 0.6f, &StageGeoNpc);
	}

	// Этап H1: Slate-клик по живому прямоугольнику кнопки 0 окна NPC 106.
	static void StageGeoNpc(TSharedRef<FCtx> C)
	{
		UWorld* W = C->W.Get();
		UGameInstance* GI = W ? W->GetGameInstance() : nullptr;
		UAsdaNpcDialogSubsystem* Dlg = GI ? GI->GetSubsystem<UAsdaNpcDialogSubsystem>() : nullptr;
		UAsdaWindowSubsystem* WM = GI ? GI->GetSubsystem<UAsdaWindowSubsystem>() : nullptr;
		if (!Dlg || !WM) { C->Expect(false, TEXT("H1: нет подсистем")); Finish(C); return; }
		const FString L = DumpLine(Dlg->DumpWindow(), TEXT("NPC_SHOP_BUT00"));
		FBox2D R(ForceInit);
		const bool bR = ParseRect(L, R);
		C->Expect(bR && L.Contains(TEXT("видим=1")), FString::Printf(TEXT("H1: кнопка 0 NPC 106 отрисована: %s"), *L.TrimStart()));
		if (bR) { WM->SimulateClickAt(R.GetCenter(), /*HoldFrames*/ 3); }
		After(C, 0.6f, &StageGeoLink);
	}

	// Хит-тест Slate в точке At: путь под курсором. Возвращает: лежит ли в пути `Want` (если задан), владельцев узлов
	//   хвоста пути (для журнала), попал ли путь в окно 9100 / в системный чат 2570.
	struct FProbe { bool bWant = false; bool bLink = false; bool bChat = false; FString Tail; };
	static FProbe ProbeAt(UWorld* W, FVector2D At, const UWidget* Want)
	{
		FProbe R;
		if (!FSlateApplication::IsInitialized()) { return R; }
		FSlateApplication& App = FSlateApplication::Get();
		const FWidgetPath Path = App.LocateWindowUnderMouse(At, App.GetInteractiveTopLevelWindows());
		const TSharedPtr<SWidget> Self = Want ? Want->GetCachedWidget() : nullptr;
		for (int32 k = 0; k < Path.Widgets.Num(); ++k)
		{
			const TSharedRef<SWidget> Sw = Path.Widgets[k].Widget;
			if (Self.IsValid() && Sw == Self) { R.bWant = true; }
			FString Owner;
			for (TObjectIterator<UWidget> It; It; ++It)
			{
				if (It->GetWorld() == W && It->GetCachedWidget() == Sw)
				{
					const UUserWidget* Uw = It->GetTypedOuter<UUserWidget>();
					const FString Cls = Uw ? Uw->GetClass()->GetName() : It->GetClass()->GetName();
					const FString Nm = It->GetName();
					if (Cls.Contains(TEXT("SOULMATE_LINK_WIN")) || Nm.Contains(TEXT("SOULMATE_LINK_WIN"))) { R.bLink = true; }
					if (Cls.Contains(TEXT("CHAT_NEW_SYSTEM")) || Nm.Contains(TEXT("CHAT_NEW_SYSTEM"))) { R.bChat = true; }
					Owner = FString::Printf(TEXT("[%s/%s]"), *Cls, *Nm);
					break;
				}
			}
			if (k >= Path.Widgets.Num() - 5) { R.Tail += TEXT(" › ") + Sw->GetType().ToString() + Owner; }
		}
		return R;
	}

	// Центр «ОК» 9104 и живые прямоугольники окна 9100 (девять планов рамки) и кнопок. false — не отрисовано.
	static bool LinkGeometry(UUserWidget* Link, FVector2D& OkC, FBox2D& Win, FBox2D& OkBox, FBox2D& CanBox)
	{
		UButton* Ok = Link ? Cast<UButton>(Link->GetWidgetFromName(TEXT("SOULMATE_LINK_BTN1"))) : nullptr;
		UButton* Can = Link ? Cast<UButton>(Link->GetWidgetFromName(TEXT("SOULMATE_LINK_BTN0"))) : nullptr;
		FVector2D A(0.f, 0.f), B(0.f, 0.f), Wa(0.f, 0.f), Wb(0.f, 0.f), Ca(0.f, 0.f), Cb(0.f, 0.f);   // углы прямоугольников
		const bool bOk = Ok && AsdaUi::WidgetAbsRect(Ok, A, B);
		const bool bWin = AsdaUi::FramePlanesAbsRect(Link, TEXT("SOULMATE_LINK_WIN_0_%d"), 9, Wa, Wb);
		if (Can) { AsdaUi::WidgetAbsRect(Can, Ca, Cb); }
		OkC = (A + B) * 0.5f; OkBox = FBox2D(A, B); Win = FBox2D(Wa, Wb); CanBox = FBox2D(Ca, Cb);
		return bOk && bWin;
	}

	// Этап H2: окно 9100 открыто Slate-кликом по NPC. [ОРИГИНАЛ] Окно показывается НЕ поднятым (`editBoxShowAndFocus` не зовёт
	//   `CClass_MapInsert`), а системный чат 2570 эталон держит в КОНЦЕ списка окон (сверху) — ревизия 1-А §8. Поэтому:
	//   если «ОК» перекрыт чатом — контроль «клик по ОК уходит в чат, 5200 ×0», затем клик по СВОБОДНОЙ части окна
	//   поднимает его наверх и уже потом «ОК». Чат НЕ прячем — это путь игрока.
	static void StageGeoLink(TSharedRef<FCtx> C)
	{
		UWorld* W = C->W.Get();
		UGameInstance* GI = W ? W->GetGameInstance() : nullptr;
		UAsdaNpcDialogSubsystem* Dlg = GI ? GI->GetSubsystem<UAsdaNpcDialogSubsystem>() : nullptr;
		UAsdaSoulmateSubsystem* Soul = GI ? GI->GetSubsystem<UAsdaSoulmateSubsystem>() : nullptr;
		UAsdaWindowSubsystem* WM = GI ? GI->GetSubsystem<UAsdaWindowSubsystem>() : nullptr;
		if (!Dlg || !Soul || !WM) { C->Expect(false, TEXT("H2: нет подсистем")); Finish(C); return; }
		C->Expect(!Dlg->IsDialogOpen() && WM->IsWindowVisible(TEXT("SOULMATE_LINK_WIN")) && Soul->GetLinkWinMode() == 2,
			TEXT("H2: Slate-клик по кнопке NPC 106 → разговор закрыт, окно 9100 видно, режим 2"));
		UUserWidget* Link = Soul->GetLinkWindowForCheck();
		if (UEditableText* E = NameEdit(Link)) { E->SetText(FText::FromString(kNearName)); }
		FVector2D OkC(0.f, 0.f); FBox2D Win(ForceInit), OkBox(ForceInit), CanBox(ForceInit);
		const bool bGeo = LinkGeometry(Link, OkC, Win, OkBox, CanBox);
		C->Expect(bGeo, FString::Printf(TEXT("H2: «ОК» 9104 и рамка окна отрисованы: «ОК» (%.0f,%.0f), окно (%.0f,%.0f)-(%.0f,%.0f)"),
			OkC.X, OkC.Y, Win.Min.X, Win.Min.Y, Win.Max.X, Win.Max.Y));
		if (!bGeo) { Finish(C); return; }
		UButton* Ok = Cast<UButton>(Link->GetWidgetFromName(TEXT("SOULMATE_LINK_BTN1")));
		const FProbe P = ProbeAt(W, OkC, Ok);
		C->ZLinkBefore = WM->GetWindowZOrder(TEXT("SOULMATE_LINK_WIN"));
		UE_LOG(LogAsdaSoulWave1ATest, Log, TEXT("САМОТЕСТ-1А инфо H2: z окна 9100 = %d, чата 2570 = %d; под центром «ОК»:%s"),
			C->ZLinkBefore, WM->GetWindowZOrder(TEXT("CHAT_NEW_SYSTEM")), *P.Tail);
		if (P.bWant)
		{
			// Перекрытия нет (зависит от сохранённого размера чата) — прямой путь: клик по «ОК».
			UE_LOG(LogAsdaSoulWave1ATest, Log, TEXT("САМОТЕСТ-1А инфо H2: «ОК» не перекрыт — клик сразу по «ОК»"));
			C->bOkCovered = false;
			Arm(C, GI);
			WM->SimulateClickAt(OkC, /*HoldFrames*/ 3);
			After(C, 0.6f, &StageGeoOk);
			return;
		}
		C->bOkCovered = true;
		C->Expect(P.bChat, TEXT("H2: «ОК» перекрыт именно системным чатом 2570 (эталон: чат в конце списка окон, 9100 при показе не поднимается)"));
		// Контроль: клик по перекрытой «ОК» уходит в чат — 5200 ×0.
		Arm(C, GI);
		WM->SimulateClickAt(OkC, /*HoldFrames*/ 3);
		After(C, 0.6f, &StageGeoRaise);
	}

	// Этап H2б: контроль «клик по перекрытой ОК → 5200 ×0», затем клик по свободной части окна 9100 (подъём).
	static void StageGeoRaise(TSharedRef<FCtx> C)
	{
		UWorld* W = C->W.Get();
		UGameInstance* GI = W ? W->GetGameInstance() : nullptr;
		UAsdaSoulmateSubsystem* Soul = GI ? GI->GetSubsystem<UAsdaSoulmateSubsystem>() : nullptr;
		UAsdaWindowSubsystem* WM = GI ? GI->GetSubsystem<UAsdaWindowSubsystem>() : nullptr;
		if (!Soul || !WM) { C->Expect(false, TEXT("H2б: нет подсистем")); Finish(C); return; }
		C->Expect(C->Count(5200) == 0 && WM->IsWindowVisible(TEXT("SOULMATE_LINK_WIN")),
			FString::Printf(TEXT("H2б контроль: клик по перекрытой «ОК» ушёл в чат — 5200 ×%d, окно 9100 на месте"), C->Count(5200)));
		Disarm(C, GI);
		UUserWidget* Link = Soul->GetLinkWindowForCheck();
		FVector2D OkC(0.f, 0.f); FBox2D Win(ForceInit), OkBox(ForceInit), CanBox(ForceInit);
		LinkGeometry(Link, OkC, Win, OkBox, CanBox);
		// Свободная точка окна: сетка 6×6 внутри рамки, мимо кнопок и поля ввода; в пути — окно 9100, не чат.
		UWidget* Edit = NameEdit(Link);
		FVector2D Ea(0.f, 0.f), Eb(0.f, 0.f);   // поле 9101
		if (Edit) { AsdaUi::WidgetAbsRect(Edit, Ea, Eb); }
		const FBox2D EditBox(Ea, Eb);
		bool bFound = false; FVector2D Body(0.f, 0.f);
		for (int32 iy = 1; iy <= 6 && !bFound; ++iy)
		{
			for (int32 ix = 1; ix <= 6 && !bFound; ++ix)
			{
				const FVector2D Pt(Win.Min.X + (Win.Max.X - Win.Min.X) * ix / 7.f, Win.Min.Y + (Win.Max.Y - Win.Min.Y) * iy / 7.f);
				if (OkBox.ExpandBy(4.f).IsInside(Pt) || CanBox.ExpandBy(4.f).IsInside(Pt) || EditBox.ExpandBy(4.f).IsInside(Pt)) { continue; }
				const FProbe Pb = ProbeAt(W, Pt, nullptr);
				// Точка тела окна, не накрытая чатом: подъём по телу ведёт оконный менеджер по зонам окон (планы рамки
				//   9100 — некликабельные картинки, в пути Slate их нет), поэтому критерий — «внутри рамки и НЕ под чатом».
				if (!Pb.bChat) { Body = Pt; bFound = true; UE_LOG(LogAsdaSoulWave1ATest, Log, TEXT("САМОТЕСТ-1А инфо H2б: тело окна (%.0f,%.0f), путь:%s"), Pt.X, Pt.Y, *Pb.Tail); }
			}
		}
		C->Expect(bFound, FString::Printf(TEXT("H2б: свободная часть окна 9100 под курсором найдена (%.0f,%.0f)"), Body.X, Body.Y));
		if (!bFound) { Finish(C); return; }
		WM->SimulateClickAt(Body, /*HoldFrames*/ 3);
		After(C, 0.6f, &StageGeoAfterRaise);
	}

	// Этап H2в: окно поднято над чатом, «ОК» теперь сверху — клик по «ОК».
	static void StageGeoAfterRaise(TSharedRef<FCtx> C)
	{
		UWorld* W = C->W.Get();
		UGameInstance* GI = W ? W->GetGameInstance() : nullptr;
		UAsdaSoulmateSubsystem* Soul = GI ? GI->GetSubsystem<UAsdaSoulmateSubsystem>() : nullptr;
		UAsdaWindowSubsystem* WM = GI ? GI->GetSubsystem<UAsdaWindowSubsystem>() : nullptr;
		if (!Soul || !WM) { C->Expect(false, TEXT("H2в: нет подсистем")); Finish(C); return; }
		const int32 ZLink = WM->GetWindowZOrder(TEXT("SOULMATE_LINK_WIN"));
		const int32 ZChat = WM->GetWindowZOrder(TEXT("CHAT_NEW_SYSTEM"));
		C->Expect(ZLink > ZChat && ZLink > C->ZLinkBefore,
			FString::Printf(TEXT("H2в: клик по телу окна поднял 9100: z %d → %d, чат %d"), C->ZLinkBefore, ZLink, ZChat));
		UUserWidget* Link = Soul->GetLinkWindowForCheck();
		FVector2D OkC(0.f, 0.f); FBox2D Win(ForceInit), OkBox(ForceInit), CanBox(ForceInit);
		LinkGeometry(Link, OkC, Win, OkBox, CanBox);
		UButton* Ok = Link ? Cast<UButton>(Link->GetWidgetFromName(TEXT("SOULMATE_LINK_BTN1"))) : nullptr;
		const FProbe P = ProbeAt(W, OkC, Ok);
		C->Expect(P.bWant, FString::Printf(TEXT("H2в: «ОК» теперь в пути хит-теста (хвост:%s)"), *P.Tail));
		Arm(C, GI);
		WM->SimulateClickAt(OkC, /*HoldFrames*/ 3);
		After(C, 0.6f, &StageGeoOk);
	}

	// Этап H3: 5200 ушёл по Slate-клику «ОК».
	static void StageGeoOk(TSharedRef<FCtx> C)
	{
		UWorld* W = C->W.Get();
		UGameInstance* GI = W ? W->GetGameInstance() : nullptr;
		UAsdaWindowSubsystem* WM = GI ? GI->GetSubsystem<UAsdaWindowSubsystem>() : nullptr;
		const FCap* P = C->Last(5200);
		C->Expect(C->Count(5200) == 1 && P && P->P[0] == 2 && NameOf5200(P) == kNearName && WM && !WM->IsWindowVisible(TEXT("SOULMATE_LINK_WIN")),
			FString::Printf(TEXT("H3: Slate-клик «ОК» → 5200 ×%d режим %d «%s», окно скрыто"), C->Count(5200), P ? P->P[0] : -1, *NameOf5200(P)));
		StageSoulN1(C);   // ★ 11.10.2026: этап N (сердца F14, разрыв при userNum 0 — N3), затем итог
	}

	// ★ 11.10.2026 (В0 аудита дублей №2). Этап N1 (F14): сердце к ближнему соседу — один таймер полёта; долетев, снимается.
	//   Естественные сердца тика мира отключены сдвигом `NextHeartS` в далёкое будущее (проверка детерминирована).
	static void StageSoulN1(TSharedRef<FCtx> C)
	{
		UWorld* W = C->W.Get();
		UGameInstance* GI = W ? W->GetGameInstance() : nullptr;
		UAsdaWorldSubsystem* WS = GI ? GI->GetSubsystem<UAsdaWorldSubsystem>() : nullptr;
		if (!WS) { C->Expect(false, TEXT("N1: нет мира")); Finish(C); return; }
		C->HeartTimersBase = FAsdaSoulWave1ATestAccess::HeartTimers(WS);
		FAsdaSoulWave1ATestAccess::FSoul St; St.SoulIdx = 1; St.PartnerUserIdx = kPeerNear; St.NextHeartS = 0.0;
		FAsdaSoulWave1ATestAccess::Souls(WS).Add((int32)kMe, St);
		FAsdaSoulWave1ATestAccess::TickHearts(WS);
		FAsdaSoulWave1ATestAccess::Souls(WS).FindChecked((int32)kMe).NextHeartS = 1.0e12;
		const int32 N = FAsdaSoulWave1ATestAccess::HeartTimers(WS);
		C->Expect(N == C->HeartTimersBase + 1, FString::Printf(TEXT("N1: сердце к соседу — таймеров полёта %d → %d (+1)"), C->HeartTimersBase, N));
		After(C, 2.0f, &StageSoulN2);
	}

	// Этап N2 (F14): долетевшее сердце сняло таймер; второе сердце — к ДАЛЬНЕМУ соседу (11 тайлов: долететь не успеет), он
	//   уходит из мира (4011: фейд ~1 с, затем уничтожение актора) → сердце теряет цель, таймер снимается.
	static void StageSoulN2(TSharedRef<FCtx> C)
	{
		UWorld* W = C->W.Get();
		UGameInstance* GI = W ? W->GetGameInstance() : nullptr;
		UAsdaWorldSubsystem* WS = GI ? GI->GetSubsystem<UAsdaWorldSubsystem>() : nullptr;
		UAsdaNetSubsystem* Net = GI ? AsdaTestNet::Get(GI) : nullptr;
		if (!WS || !Net) { C->Expect(false, TEXT("N2: нет подсистем")); Finish(C); return; }
		const int32 N0 = FAsdaSoulWave1ATestAccess::HeartTimers(WS);
		C->Expect(N0 == C->HeartTimersBase, FString::Printf(TEXT("N2: сердце долетело — таймер снят (%d, ждали %d)"), N0, C->HeartTimersBase));
		FAsdaSoulWave1ATestAccess::Souls(WS).FindChecked((int32)kMe).NextHeartS = 0.0;
		FAsdaSoulWave1ATestAccess::Souls(WS).FindChecked((int32)kMe).PartnerUserIdx = kPeerFar;
		FAsdaSoulWave1ATestAccess::TickHearts(WS);
		FAsdaSoulWave1ATestAccess::Souls(WS).FindChecked((int32)kMe).NextHeartS = 1.0e12;
		const int32 N1 = FAsdaSoulWave1ATestAccess::HeartTimers(WS);
		C->Expect(N1 == C->HeartTimersBase + 1, FString::Printf(TEXT("N2: второе сердце (к дальнему соседу) — таймеров %d (+1)"), N1));
		TArray<uint8> B; B.SetNumZeroed(2); B[0] = (uint8)(kPeerFar & 0xFF); B[1] = (uint8)(kPeerFar >> 8);
		AsdaTestNet::DebugFeedPacket(Net, 4011, B);   // сосед-цель уходит из мира (штатный приём: фейд ~1 с → уничтожение)
		After(C, 1.6f, &StageSoulN3);
	}

	// Этап N3: (F14) цель исчезла — таймер снят; (N3) разрыв пары 5207 при СВОЁМ userNum 0 снимает своё кольцо и запись.
	static void StageSoulN3(TSharedRef<FCtx> C)
	{
		UWorld* W = C->W.Get();
		UGameInstance* GI = W ? W->GetGameInstance() : nullptr;
		UAsdaWorldSubsystem* WS = GI ? GI->GetSubsystem<UAsdaWorldSubsystem>() : nullptr;
		UAsdaNetSubsystem* Net = GI ? AsdaTestNet::Get(GI) : nullptr;
		if (!WS || !Net) { C->Expect(false, TEXT("N3: нет подсистем")); Finish(C); return; }
		const int32 N0 = FAsdaSoulWave1ATestAccess::HeartTimers(WS);
		C->Expect(N0 == C->HeartTimersBase, FString::Printf(TEXT("N3: цель ушла из мира — таймер снят (%d, ждали %d)"), N0, C->HeartTimersBase));
		FAsdaSoulWave1ATestAccess::Souls(WS).Remove((int32)kMe);
		// Кольцо: есть ли на акторе прикреплённый эффект с тегом кольца.
		auto HasRing = [](AActor* A)
		{
			if (!A) { return false; }
			TArray<AActor*> Att; A->GetAttachedActors(Att);
			for (const AActor* X : Att) { if (X && X->ActorHasTag(FName(AsdaCharFxTags::kSoulRing))) { return true; } }
			return false;
		};
		AActor* Me = WS->LocalPlayerActor.Get();
		AsdaTestNet::SelfUserNum(Net) = 0; AsdaTestNet::bSelfUserNumKnown(Net) = true;   // свой номер 0 — штатный для нашего сервера
		FAsdaSoulWave1ATestAccess::FSoul St; St.SoulIdx = 1; St.PartnerUserIdx = kPeerFar; St.NextHeartS = 1.0e12;
		FAsdaSoulWave1ATestAccess::Souls(WS).Add(0, St);
		FAsdaSoulWave1ATestAccess::Ring(WS, Me, true);
		C->Expect(HasRing(Me), TEXT("N3 подготовка: своё кольцо надето (userNum 0)"));
		FAsdaSoulWave1ATestAccess::Break(WS, 1, FString());
		const FAsdaSoulWave1ATestAccess::FSoul* After0 = FAsdaSoulWave1ATestAccess::Souls(WS).Find(0);
		const bool bRingLeft = HasRing(Me);
		C->Expect(!bRingLeft && After0 && After0->SoulIdx == -1 && After0->PartnerUserIdx == -1,
			FString::Printf(TEXT("N3: 5207 при своём userNum 0 — кольцо снято (%s), запись очищена (soul=%d партнёр=%d)"),
				bRingLeft ? TEXT("НЕТ") : TEXT("да"), After0 ? After0->SoulIdx : 999, After0 ? After0->PartnerUserIdx : 999));
		FAsdaSoulWave1ATestAccess::Souls(WS).Remove(0);
		AsdaTestNet::SelfUserNum(Net) = kMe;   // исходный номер вернёт Finish
		StageSoulN4(C);   // ★ 29.10.2026: кольцо после разрыва пары (кэш 6152 по charId)
	}

	// ★ 29.10.2026 Этап N4 (баг игроков «кольцо соулмейта остаётся после разрыва, Друг был вне игры»; ключ
	//   asda.Soulmate.RingCacheSync). Ожидания — по эталону: поле +2215 одно на персонажа, его пишут 6152
	//   (`PartyMark_Helper2`, social.c:7081) и 5220, а 5207 ставит −1 (social.c:7760). Пакеты — штатным приёмом
	//   (`DebugFeedPacket`), разрыв — штатный приёмник мира (как в N3: без побочных действий вкладки над хотбаром).
	static void StageSoulN4(TSharedRef<FCtx> C)
	{
		UWorld* W = C->W.Get();
		UGameInstance* GI = W ? W->GetGameInstance() : nullptr;
		UAsdaWorldSubsystem* WS = GI ? GI->GetSubsystem<UAsdaWorldSubsystem>() : nullptr;
		UAsdaNetSubsystem* Net = GI ? AsdaTestNet::Get(GI) : nullptr;
		if (!WS || !Net) { C->Expect(false, TEXT("N4: нет подсистем")); Finish(C); return; }
		auto HasRing = [](AActor* A)
		{
			if (!A) { return false; }
			TArray<AActor*> Att; A->GetAttachedActors(Att);
			for (const AActor* X : Att) { if (X && X->ActorHasTag(FName(AsdaCharFxTags::kSoulRing))) { return true; } }
			return false;
		};
		C->CharIdWas = AsdaTestNet::SelectedCharId(Net);
		if (AsdaTestNet::SelectedCharId(Net) == 0) { AsdaTestNet::SelectedCharId(Net) = 777001; }   // офлайн charId нет — свой на время этапа
		const int32 MyChar = AsdaTestNet::SelectedCharId(Net);
		AActor* Me = WS->LocalPlayerActor.Get();
		TMap<int32, int32>& ByChar = FAsdaSoulWave1ATestAccess::SoulByChar(WS);
		// 6152 своего персонажа: charId, номер пары, charId партнёра, SoulState (13 б).
		auto Feed6152 = [Net, MyChar](int32 SoulIdx)
		{
			TArray<uint8> B; B.SetNumZeroed(13);
			FMemory::Memcpy(&B[0], &MyChar, 4); FMemory::Memcpy(&B[4], &SoulIdx, 4);
			const int32 Partner = 777002; FMemory::Memcpy(&B[8], &Partner, 4);
			AsdaTestNet::DebugFeedPacket(Net, 6152, B);
		};
		// 5220 своей записи (101 б): @0 charId, @4 userIdx, @6 номер пары, @23 userIdx партнёра; @25/@33 = 0 (панель не нужна).
		auto Feed5220 = [Net, MyChar](int32 SoulIdx)
		{
			TArray<uint8> B; B.SetNumZeroed(101);
			FMemory::Memcpy(&B[0], &MyChar, 4);
			const uint16 U = kMe; FMemory::Memcpy(&B[4], &U, 2);
			FMemory::Memcpy(&B[6], &SoulIdx, 4);
			const int16 P = -1; FMemory::Memcpy(&B[23], &P, 2);
			AsdaTestNet::DebugFeedPacket(Net, 5220, B);
		};
		// «Спавн зажёг бы кольцо»: ровно условия своего спавна (AsdaWorldSubsystem.cpp — SoulByUserIdx / SoulIdxByCharId > 0).
		auto SpawnWouldLight = [WS, &ByChar, MyChar]()
		{
			const FAsdaSoulWave1ATestAccess::FSoul* S = FAsdaSoulWave1ATestAccess::Souls(WS).Find((int32)kMe);
			const int32* I = ByChar.Find(MyChar);
			return (S && S->SoulIdx > 0) || (I && *I > 0);
		};
		AsdaCVarSave::SetForTest(TEXT("asda.Soulmate.RingCacheSync"), 1);
		// N4a: 6152 своего с парой 7 → кэш 7, кольцо на себе.
		Feed6152(7);
		C->Expect(ByChar.FindRef(MyChar) == 7 && HasRing(Me),
			FString::Printf(TEXT("N4a: 6152 свой, пара 7 — кэш %d, кольцо %s"), ByChar.FindRef(MyChar), HasRing(Me) ? TEXT("да") : TEXT("НЕТ")));
		// N4b: 5207 код 3 (принудительный разрыв) → кэш −1, кольца нет, спавн его не вернёт (портал после разрыва).
		FAsdaSoulWave1ATestAccess::Break(WS, 3, FString());
		C->Expect(ByChar.FindRef(MyChar) == -1 && !HasRing(Me) && !SpawnWouldLight(),
			FString::Printf(TEXT("N4b: 5207 код 3 — кэш %d (ждём −1), кольцо %s, спавн зажёг бы: %s"), ByChar.FindRef(MyChar),
				HasRing(Me) ? TEXT("ОСТАЛОСЬ") : TEXT("снято"), SpawnWouldLight() ? TEXT("ДА") : TEXT("нет")));
		// N4c: вход после разрыва вне игры — устаревший 6152 (пара 7), затем 5220 со Smidx −1 (`UPCharSoulMate`, State=0).
		Feed6152(7);
		Feed5220(-1);
		C->Expect(ByChar.FindRef(MyChar) == -1 && !HasRing(Me) && !SpawnWouldLight(),
			FString::Printf(TEXT("N4c: 6152 пара 7 → 5220 Smidx −1 — кэш %d (ждём −1), кольцо %s, спавн зажёг бы: %s"),
				ByChar.FindRef(MyChar), HasRing(Me) ? TEXT("ОСТАЛОСЬ") : TEXT("снято"), SpawnWouldLight() ? TEXT("ДА") : TEXT("нет")));
		// N4d: обратный порядок — 5220 Smidx 9 после 6152 −1: последний пакет решает (поле одно), кольцо есть.
		Feed6152(-1);
		Feed5220(9);
		C->Expect(ByChar.FindRef(MyChar) == 9 && HasRing(Me) && SpawnWouldLight(),
			FString::Printf(TEXT("N4d: 6152 −1 → 5220 пара 9 — кэш %d (ждём 9), кольцо %s"), ByChar.FindRef(MyChar), HasRing(Me) ? TEXT("да") : TEXT("НЕТ")));
		// N4e контроль (ключ 0 = прежний порт): 5220 −1 не перебивает кэш 6152 → спавн вернул бы кольцо (дефект воспроизводится).
		AsdaCVarSave::SetForTest(TEXT("asda.Soulmate.RingCacheSync"), 0);
		Feed6152(7);
		Feed5220(-1);
		C->Expect(ByChar.FindRef(MyChar) == 7 && SpawnWouldLight(),
			FString::Printf(TEXT("N4e контроль, ключ 0: 6152 пара 7 → 5220 −1 — кэш %d (прежний порт: 7), спавн зажёг бы: %s"),
				ByChar.FindRef(MyChar), SpawnWouldLight() ? TEXT("да (дефект)") : TEXT("нет")));
		AsdaCVarSave::SetForTest(TEXT("asda.Soulmate.RingCacheSync"), 1);
		// Подготовка N5: кэш снова «пара 7», соседи уходят из мира (их акторы не должны пережить сброс реестра).
		Feed6152(7);
		for (const int32 U : { kPeerNear, kPeerFar, kPeerEdge })
		{
			TArray<uint8> B; B.SetNumZeroed(2); B[0] = (uint8)(U & 0xFF); B[1] = (uint8)(U >> 8);
			AsdaTestNet::DebugFeedPacket(Net, 4011, B);
		}
		After(C, 1.6f, &StageSoulN5);
	}

	// ★ 29.10.2026 Этап N5: полный сброс сессии (разрыв связи — актор своего жив) чистит кэш 6152; контроль ключом 0.
	static void StageSoulN5(TSharedRef<FCtx> C)
	{
		UWorld* W = C->W.Get();
		UGameInstance* GI = W ? W->GetGameInstance() : nullptr;
		UAsdaWorldSubsystem* WS = GI ? GI->GetSubsystem<UAsdaWorldSubsystem>() : nullptr;
		UAsdaNetSubsystem* Net = GI ? AsdaTestNet::Get(GI) : nullptr;
		if (!WS || !Net) { C->Expect(false, TEXT("N5: нет подсистем")); Finish(C); return; }
		TMap<int32, int32>& ByChar = FAsdaSoulWave1ATestAccess::SoulByChar(WS);
		const int32 MyChar = AsdaTestNet::SelectedCharId(Net);
		C->Expect(ByChar.FindRef(MyChar) == 7, FString::Printf(TEXT("N5 подготовка: кэш 6152 = %d (ждём 7)"), ByChar.FindRef(MyChar)));
		// Уборка за N4/N5: 6152 «пары нет» своему (снимает своё кольцо и `SelfSoulIdx` подсистемы навыков), запись кэша — долой.
		auto CleanupRing = [Net, WS, &ByChar, MyChar]()
		{
			TArray<uint8> B; B.SetNumZeroed(13);
			const int32 None = -1;
			FMemory::Memcpy(&B[0], &MyChar, 4); FMemory::Memcpy(&B[4], &None, 4);
			AsdaTestNet::DebugFeedPacket(Net, 6152, B);
			FAsdaSoulWave1ATestAccess::Ring(WS, WS->LocalPlayerActor.Get(), false);
			ByChar.Remove(MyChar);
		};
		// Полный сброс мира оставляет сессию в виде «связь разорвана» — следующий самотест пакета на нём не идёт. Поэтому только по
		//   явному аргументу `ringreset` (прогонять последним/отдельно); без него — кэш возвращается к виду до N4.
		if (!C->bRingReset)
		{
			CleanupRing();
			UE_LOG(LogAsdaSoulWave1ATest, Log, TEXT("САМОТЕСТ-1А инфо: N5 (полный сброс мира) пропущен — нужен аргумент ringreset"));
			AsdaCVarSave::RestoreAll(TEXT("asda.Soul.SelfTestWave1A"));
			AsdaTestNet::SelectedCharId(Net) = C->CharIdWas;
			Finish(C);
			return;
		}
		AsdaCVarSave::SetForTest(TEXT("asda.Soulmate.RingCacheSync"), 0);
		WS->ResetSessionState(EAsdaSessionResetReason::Disconnected);
		C->Expect(ByChar.Contains(MyChar), TEXT("N5 контроль, ключ 0: полный сброс кэш 6152 НЕ чистит (прежний порт)"));
		AsdaCVarSave::SetForTest(TEXT("asda.Soulmate.RingCacheSync"), 1);
		WS->ResetSessionState(EAsdaSessionResetReason::Disconnected);
		C->Expect(ByChar.Num() == 0, FString::Printf(TEXT("N5: полный сброс сессии — кэш 6152 пуст (%d записей)"), ByChar.Num()));
		CleanupRing();
		AsdaCVarSave::RestoreAll(TEXT("asda.Soul.SelfTestWave1A"));
		AsdaTestNet::SelectedCharId(Net) = C->CharIdWas;
		Finish(C);
	}


	// Восстановление состояния, итог, выход.
	static void Finish(TSharedRef<FCtx> C)
	{
		UWorld* W = C->W.Get();
		UGameInstance* GI = W ? W->GetGameInstance() : nullptr;
		UAsdaNetSubsystem* Net = GI ? AsdaTestNet::Get(GI) : nullptr;
		UAsdaNpcDialogSubsystem* Dlg = GI ? GI->GetSubsystem<UAsdaNpcDialogSubsystem>() : nullptr;
		UAsdaSoulmateSubsystem* Soul = GI ? GI->GetSubsystem<UAsdaSoulmateSubsystem>() : nullptr;
		UAsdaWindowSubsystem* WM = GI ? GI->GetSubsystem<UAsdaWindowSubsystem>() : nullptr;
		Disarm(C, GI);
		if (Dlg && Dlg->IsDialogOpen()) { Dlg->Close(); }
		if (Soul) { Soul->ResetSessionState(EAsdaSessionResetReason::LeftWorld); }
		// ★ R5-fix6-c (13.11.2026): своё имя — как было до прогона.
		if (UAsdaWorldSubsystem* WSn = (GI && C->bNameSet) ? GI->GetSubsystem<UAsdaWorldSubsystem>() : nullptr) { WSn->SetSelfCharNameForSelfTest(C->SelfNameWas); C->bNameSet = false; }
		if (Net)
		{
			for (const int32 U : { kPeerNear, kPeerFar, kPeerEdge })
			{
				TArray<uint8> B; B.SetNumZeroed(2); B[0] = (uint8)(U & 0xFF); B[1] = (uint8)(U >> 8);
				AsdaTestNet::DebugFeedPacket(Net, 4011, B);   // уход соседа из мира (штатный приём)
			}
			AsdaTestNet::SelfUserNum(Net) = C->UserNumWas; AsdaTestNet::bSelfUserNumKnown(Net) = C->bKnownWas;
			AsdaTestNet::SetSelfSoulState(Net, C->SoulStateWas);
		}
		if (WM) { WM->ClearMsgBoxQueue(TEXT("самотест 1А: конец")); }
		if (C->Sink.IsValid() && GLog) { GLog->RemoveOutputDevice(C->Sink.Get()); }
		UE_LOG(LogAsdaSoulWave1ATest, Log, TEXT("САМОТЕСТ-1А ИТОГ: ok=%d fail=%d"), C->Ok, C->Fail);
		if (C->bQuit && GEngine && W) { GEngine->Exec(W, TEXT("quit")); }
	}
}

static FAutoConsoleCommandWithWorldAndArgs GAsdaSoulWave1ASelfTest(
	TEXT("asda.Soul.SelfTestWave1A"),
	TEXT("asda.Soul.SelfTestWave1A [delay=N] [quit] [ringreset] — ОФЛАЙН-самотест волны 1-А: NPC 30023/30024/30025, окно 9100, пункт 2002, доп-меню, поиск 8200 (5483/5484/5485), гейт «души», клики по геометрии"),
	FConsoleCommandWithWorldAndArgsDelegate::CreateLambda([](const TArray<FString>& A, UWorld* W)
	{
		const FAsdaSelfTestArgs KitArgs = FAsdaSelfTestArgs::Parse(A);   // ★ 11.10.2026 (В3 S1): каркас — отказ с ИТОГ и quit, второй барьер на все опкоды
		if (AsdaSelfTestKit::RefuseStart(W, TEXT("asda.Soul.SelfTestWave1A"), KitArgs)) { return; }
		if (!W) { return; }
		float Delay = 0.f;   // задержка запуска, с
		bool bQuit = false;  // выйти после прогона
		bool bRingReset = false;   // ★ 29.10.2026: этап N5 — полный сброс мира (разрушает сессию; прогонять последним)
		for (const FString& Arg : A)
		{
			if (Arg.Equals(TEXT("quit"), ESearchCase::IgnoreCase)) { bQuit = true; }
			else if (Arg.Equals(TEXT("ringreset"), ESearchCase::IgnoreCase)) { bRingReset = true; }
		}
		Delay = AsdaSelfTestKit::DelayArgOr(A, Delay);   // ★ R5-fix2-c (13.11.2026): `delay=N` — общая точка (была ветка цикла аргументов)
		TWeakObjectPtr<UWorld> WW = W;
		auto Go = [WW, bQuit, bRingReset]()   // прогон (сразу или по таймеру); выход — в последнем этапе
		{
			if (!WW.Get()) { return; }
			if (AsdaSelfTestKit::RefuseDeferred(WW.Get(), TEXT("asda.Soul.SelfTestWave1A"), bQuit)) { return; }   // Р-2: офлайн повторно — на отложенном старте
			TSharedRef<AsdaSoulWave1ATestLocal::FCtx> C = MakeShared<AsdaSoulWave1ATestLocal::FCtx>();
			C->W = WW; C->bQuit = bQuit; C->bRingReset = bRingReset;
			C->Sink = MakeShared<AsdaSoulWave1ATestLocal::FSink>();
			if (GLog) { GLog->AddOutputDevice(C->Sink.Get()); }
			AsdaSoulWave1ATestLocal::Run(C);
		};
		if (Delay <= 0.f) { Go(); return; }
		FTimerHandle H;
		W->GetTimerManager().SetTimer(H, FTimerDelegate::CreateLambda(Go), Delay, false);
	}));

#endif // !UE_BUILD_SHIPPING
