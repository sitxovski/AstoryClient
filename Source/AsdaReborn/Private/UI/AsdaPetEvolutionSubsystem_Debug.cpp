// Copyright (c) 2026 @sitxovski (Discord)
// AsdaPetEvolutionSubsystem_Debug.cpp — ★ R3-c 12.11.2026 (разрез AsdaPetEvolutionSubsystem.cpp, дословный перенос): стенд отправки, дампы, дев-команды.
//   Общее для частей — в AsdaPetEvolutionSubsystem_Private.h; шапка и реверс — в AsdaPetEvolutionSubsystem.cpp.
#include "UI/AsdaPetEvolutionSubsystem.h"
#include "UI/AsdaMessageSubsystem.h"   // ★ R1: прямое включение (раньше приходило транзитивно)
#include "AsdaDevConsole.h"   // ★ античит А2: ASDA_DEV_CVAR / ASDA_DEV_CONSOLE — дев-ключи и команды вне Shipping

#include "UI/AsdaJuiWindowHandle.h"
#include "UI/AsdaNotice.h"
#include "UI/AsdaWindowSubsystem.h"
#include "UI/AsdaUiHelpers.h"
#include "UI/AsdaPetCageSubsystem.h"
#include "Items/AsdaItemDbRows.h"
#include "Pet/AsdaPetSubsystem.h"
#include "Pet/AsdaPetTableRows.h"
#include "Net/NetOpcodes.h"
#include "AsdaBytes.h"
#include "Login/AsdaLoginData.h"   // FAsdaLoginData::EncodeCp1251 — имя питомца в синтезе 6106/6113

#include "Blueprint/UserWidget.h"
#include "Components/Image.h"
#include "Components/Button.h"
#include "Components/TextBlock.h"
#include "Materials/MaterialInterface.h"
#include "Materials/MaterialInstanceDynamic.h"   // ★ волна 9: кисть кадра превью для 43576
#include "Engine/DataTable.h"
#include "Engine/GameInstance.h"
#include "AsdaOutgoingGate.h"               // ★ С-Р1: общий офлайн-гейт отладочных команд (RefuseDebugWhenOnline)
#include "AsdaPetEvolutionSubsystem_Private.h"   // ★ R3-c 12.11.2026: общее для частей AsdaPetEvolutionSubsystem*.cpp

// ══════════════════════════════ СТЕНД ОТПРАВКИ 6112 ═══════════════════════════════════════════
void UAsdaPetEvolutionSubsystem::StartSendStand(int32 TestCharId)
{
	UAsdaPetSubsystem* P = Pets();
	if (!P) { return; }
	if (bStandArmed)
	{
		UE_LOG(LogAsdaPetEvo, Warning, TEXT("[питомец-развитие] СТЕНД 6112 уже взведён"));
		return;
	}
	// ★★★ ВОЛНА 8 (пункт 9 ревизии): ЧУЖОЙ СТЕНД УЖЕ ДЕРЖИТ ПЕРЕХВАТ.
	//   Перехватчик исходящих ОДИН на весь клиент. Без этой проверки второе окно
	//   взводило свой стенд, не получало тела (`Begin` второй раз игнорируется),
	//   печатало ЛОЖНЫЙ вердикт «тело НЕ СНЯТО» и по таймауту звало `EndSendCapture`,
	//   снимая перехват ПЕРВОГО — то есть ломало и свой вердикт, и чужой.
	if (P->IsSendCaptureArmed())
	{
		UE_LOG(LogAsdaPetEvo, Error,
			TEXT("[питомец-развитие] СТЕНД 6112 НЕ ЗАПУЩЕН: перехват исходящих уже держит ДРУГОЙ стенд. ")
			TEXT("Дождитесь его завершения либо снимите его — два стенда разом дают ложный вердикт."));
		return;
	}
	bStandDone = false;
	StandLeft = 8.f;
	bStandArmed = true;
	StandExpectCharId = TestCharId;   // ★ его и ждём в `+0` тела (см. `CheckStandBody`)
	P->BeginSendCapture([this](int32 Op, const TArray<uint8>& Body, bool /*bPad*/, TCHAR Env)
	{
		// ⚠ Здесь сеть трогать нельзя — только пометить; перехват снимет `Tick`.
		if (CheckStandBody(Op, Body, Env)) { bStandDone = true; }
		else { bStandDone = true; }
	}, TestCharId);
	UE_LOG(LogAsdaPetEvo, Log,
		TEXT("[питомец-развитие] СТЕНД 6112 взведён (charId=%d). Дальше — БОЕВОЙ путь: подтверждение → ")
		TEXT("таймер %.0f с → очередь волны 1 → сборщик тела."), TestCharId, kSendDelaySec);
	// Боевой путь: как будто игрок нажал «ОК» в подтверждении 70.
	ArmUnionSend();
}

// Разбор снятого тела по СЕРВЕРНЫМ смещениям (GameServer.c:35955-35975), мимо нашего сборщика.
bool UAsdaPetEvolutionSubsystem::CheckStandBody(int32 Opcode, const TArray<uint8>& Body, TCHAR Envelope) const
{
	if (Opcode != OP_PET_UNION_REQ)
	{
		UE_LOG(LogAsdaPetEvo, Error, TEXT("[питомец-развитие] СТЕНД: ждали опкод %d, пришёл %d"),
			(int32)OP_PET_UNION_REQ, Opcode);
		return false;
	}
	if (Body.Num() != 18)
	{
		UE_LOG(LogAsdaPetEvo, Error, TEXT("[питомец-развитие] СТЕНД: тело %d б, а сервер читает 18"), Body.Num());
		return false;
	}
	const int32 CharId = AsdaBytes::RdI32(&Body[0]);
	const int32 Union  = (int32)AsdaBytes::RdU16(&Body[4]);
	const int32 M0 = AsdaBytes::RdI32(&Body[6]);
	const int32 M1 = AsdaBytes::RdI32(&Body[10]);
	const int32 M2 = AsdaBytes::RdI32(&Body[14]);
	// ★ CharId ВХОДИТ В ВЕРДИКТ (ревизия волны 5, СРЕДН-3). Раньше он только печатался — а это
	//   ровно то поле, ради которого существует закон `charid-vs-usernum`: при расхождении сервер
	//   МОЛЧА игнорирует пакет, и «проверка глазами» тут не годится.
	// ★ ОГРАНИЧЕНИЕ СТЕНДА, которое он поймать НЕ МОЖЕТ: мы читаем материалы как i32, а сервер
	//   берёт из них только младший WORD (`v13[2*i + 3]`, GameServer.c:35970). Значит обрезание
	//   `uid > 65535` стенд не увидит — это дефект проводного формата, а не сборщика, и лечится
	//   он только на сервере (тот же корень, что у 16-битных полей `+50/+52/+54` ответа 6113).
	const bool bOk = (CharId == StandExpectCharId) && (Union == StandExpectUnion)
		&& (M0 == StandExpectMat[0]) && (M1 == StandExpectMat[1]) && (M2 == StandExpectMat[2])
		&& (Envelope == TEXT('S'));
	UE_LOG(LogAsdaPetEvo, Log,
		TEXT("[питомец-развитие] СТЕНД 6112: конверт=%c длина=18 charId=%d(ждали %d) рецепт=%d(ждали %d) ")
		TEXT("материалы %d/%d/%d (ждали %d/%d/%d) ⇒ %s"),
		Envelope, CharId, StandExpectCharId, Union, StandExpectUnion, M0, M1, M2,
		StandExpectMat[0], StandExpectMat[1], StandExpectMat[2], bOk ? TEXT("СОШЛОСЬ") : TEXT("РАСХОЖДЕНИЕ"));
	return bOk;
}

// ════════════════════════════════════ ДИАГНОСТИКА ═════════════════════════════════════════════
FString UAsdaPetEvolutionSubsystem::DumpControls() const
{
	FString S = TEXT("=== окна развития питомца: разобранные контролы ===\n");
	auto Line = [&S](const TCHAR* Id, const UObject* W) { S += FString::Printf(TEXT("  %-46s %s\n"), Id, W ? TEXT("есть") : TEXT("НЕТ")); };
	S += TEXT("-- 43500 Pet_Evolution_Win --\n");
	Line(TEXT("43501 Pet_Evolution_Top_TextMove"), TxtTitle);
	Line(TEXT("43889 Pet_Evolution_X"),            BtnX);
	Line(TEXT("43876 Pet_Evolution_OK_2"),         BtnOk);
	Line(TEXT("43879 Pet_Evolution_End"),          BtnEnd);
	Line(TEXT("43877 Pet_Evolution_Tooltip_Text"), TxtInfo);
	Line(TEXT("43582 SIDEBUT"),                    BtnExpand);
	Line(TEXT("43583 CLOSEBUT"),                   BtnCollapse);
	Line(TEXT("43580 Pet_Evolution_R_grp"),        PanelRight);
	Line(TEXT("43883 R_Mark_Ani1"),                ResultAni);
	Line(TEXT("43576 R_Mark_3d (превью, волна 6-7)"), ImgResult3d);
	Line(TEXT("43742 R_Text1"),                    TxtResult);
	Line(TEXT("43740 Gfont_3"),                    GfontResult);
	Line(TEXT("43725 (читателей в срезе нет)"),    Ctrl43725);
	Line(TEXT("43728 (читателей в срезе нет)"),    Ctrl43728);
	Line(TEXT("43888 Btn_CBase_grp"),              BtnBaseGrp);
	Line(TEXT("43520 шаблон Count_1_grp"),         TplCountGrp);
	Line(TEXT("43529 шаблон ведущей «1»"),         TplLead1);
	for (int32 m = 0; m < TplGlyphs.Num(); ++m)
	{
		Line(*FString::Printf(TEXT("%d шаблон цифры (скин Count_%d)"), 43532 + m, 9 - m), TplGlyphs[m]);
	}
	for (int32 j = 0; j < Stages.Num(); ++j)
	{
		const FAsdaPetEvoRow& R = Stages[j];
		int32 G = 0; for (const TObjectPtr<UImage>& X : R.Glyphs) { G += X ? 1 : 0; }
		S += FString::Printf(TEXT("  стадия %d: иконка %d=%s подпись %d=%s Main %d=%s Evo %d=%s стрелка %d=%s клонов %d/10\n"),
			j, kStageIconId + j, R.Icon ? TEXT("есть") : TEXT("НЕТ"),
			kStageLabelId + j, R.Label ? TEXT("есть") : TEXT("НЕТ"),
			kStageMainId + j, R.GfontMain ? TEXT("есть") : TEXT("НЕТ"),
			kStageEvoId + j, R.GfontEvo ? TEXT("есть") : TEXT("НЕТ"),
			kStageArrowId + j, R.SlideAni ? TEXT("есть") : TEXT("НЕТ (у строк 4 и 5 номера в .JUI нет — дефект ПД6)"),
			G);
	}
	for (int32 m = 0; m < Mats.Num(); ++m)
	{
		const FAsdaPetUnionRow& R = Mats[m];
		int32 G = 0; for (const TObjectPtr<UImage>& X : R.Glyphs) { G += X ? 1 : 0; }
		S += FString::Printf(TEXT("  материал %d: иконка %d=%s заголовок %d=%s имя %d=%s готовность %d=%s глиф %d=%s клонов %d/10\n"),
			m, kMatIconId + m, R.Icon ? TEXT("есть") : TEXT("НЕТ"),
			kMatTitleId + m, R.Title ? TEXT("есть") : TEXT("НЕТ"),
			kMatNameId + m, R.Name ? TEXT("есть") : TEXT("НЕТ"),
			kMatReadyId + m, R.Ready ? TEXT("есть") : TEXT("НЕТ"),
			kMatGfontId + m, R.GfontReady ? TEXT("есть") : TEXT("НЕТ"), G);
	}
	S += TEXT("-- 43400 Pet_Inven_Win --\n");
	int32 CellsFound = 0; for (const FAsdaPetGridCell& C : Cells) { CellsFound += C.Icon ? 1 : 0; }
	S += FString::Printf(TEXT("  ячеек 43401…43430: %d/30\n"), CellsFound);
	Line(TEXT("43431 Pet_Inven_Top_Text"), TxtGridTitle);
	Line(TEXT("43439 Pet_Inven_Win_X (СБРОС, не закрытие диалога)"), BtnGridX);
	S += TEXT("-- 43450 Pet_Inven_Tooltip --\n");
	Line(TEXT("43452 T1 имя"),      TxtTipName);
	Line(TEXT("43453 T2 «Рост»"),   TxtTipLabel);
	Line(TEXT("43454 T3 стадия"),   TxtTipStage);
	Line(TEXT("43451 Green_Bar"),   TipBarFill);
	Line(TEXT("43455 проценты"),    TxtTipPct);
	return S;
}

FString UAsdaPetEvolutionSubsystem::DumpState() const
{
	const FAsdaPetSlot* Pet = Target();
	const FAsdaPetUnionRecipeRow* R = CurrentRecipe();
	FString S = TEXT("=== окно 43500: состояние ===\n");
	S += FString::Printf(TEXT("  открыто=%s страница=%s цель uid=%d (порода %d, ур. %d)\n"),
		bVisible ? TEXT("да") : TEXT("нет"), bWidePage ? TEXT("развёрнута") : TEXT("свёрнута"),
		TargetUid, Pet ? Pet->PetIndex : -1, Pet ? Pet->Level : -1);
	S += FString::Printf(TEXT("  выбранная стадия=%d колонка=%d рецепт=%d\n"),
		CurStage, CurCol, R ? R->UnionIndex : -1);
	for (int32 m = 0; m < Mats.Num(); ++m)
	{
		S += FString::Printf(TEXT("  слот %d: выбран uid=%d, требуется порода %d уровня %d\n"),
			m, Mats[m].PickedUid, Mats[m].ReqPetIndex, Mats[m].ReqLevel);
	}
	S += FString::Printf(TEXT("  подтверждение взведено=%s (до отправки %.1f с), запрос в полёте=%s\n"),
		bConfirmArmed ? TEXT("да") : TEXT("нет"), SendLeft, SendLatch.IsArmed() ? TEXT("да") : TEXT("нет"));
	S += FString::Printf(TEXT("  сетка 43400 показана=%s, подсказка 43450 показана=%s (на uid=%d)\n"),
		bGridShown ? TEXT("да") : TEXT("нет"), bTipShown ? TEXT("да") : TEXT("нет"), TipForUid);
	S += FString::Printf(TEXT("  клоны счётчиков построены=%s\n"), bClonesBuilt ? TEXT("да") : TEXT("нет"));
	if (UAsdaPetSubsystem* P = Pets())
	{
		S += FString::Printf(TEXT("  гейт 109974: призванный питомец=%s\n"),
			P->GetActivePet() ? TEXT("ЕСТЬ (окно не откроется)") : TEXT("нет"));
	}
	return S;
}

FString UAsdaPetEvolutionSubsystem::DumpMap() const
{
	FString S = TEXT("=== карта развития питомца (PetEvolutionTable.bin) ===\n");
	const FAsdaPetSlot* Pet = Target();
	FAsdaPetRow Pr;
	if (!Pet || !PetRow(Pet->PetIndex, Pr)) { return S + TEXT("  цели нет\n"); }
	S += FString::Printf(TEXT("  порода %d «%s», ключ карты (pettable+468) = %d\n"),
		Pet->PetIndex, *Pr.Name, Pr.WeaponClass);
	const FAsdaPetEvoStageRow* Map = CurrentMap();
	if (!Map) { return S + TEXT("  карты нет — у 24 пород из 32 ключ равен −1, это штатно\n"); }
	for (int32 j = 0; j < kStages; ++j)
	{
		const int32 T = Map->PetType.IsValidIndex(j) ? Map->PetType[j] : -1;
		const int32 L = Map->Level.IsValidIndex(j) ? Map->Level[j] : -1;
		const int32 U = Map->StuffIdx.IsValidIndex(j) ? Map->StuffIdx[j] : -1;
		FAsdaPetRow Sr;
		S += FString::Printf(TEXT("  стадия %d: порода %d «%s» уровень %d рецепт %d %s\n"),
			j, T, (T >= 0 && PetRow(T, Sr)) ? *Sr.Name : TEXT("—"), L, U,
			(T < 0) ? TEXT("(ПУСТАЯ — гасится по PetType == −1)") : (HasAllRecipeMatches(j) ? TEXT("[рецепт собран]") : TEXT("")));
	}
	return S;
}

FString UAsdaPetEvolutionSubsystem::DumpGrid() const
{
	FString S = TEXT("=== сетка 43400: кандидаты ===\n");
	const FAsdaPetUnionRecipeRow* R = CurrentRecipe();
	S += FString::Printf(TEXT("  колонка %d, фильтр: порода %d, уровень >= %d, uid не занят в других слотах\n"),
		CurCol,
		(R && R->ReqPetIndex.IsValidIndex(CurCol)) ? R->ReqPetIndex[CurCol] : -1,
		(R && R->ReqLevel.IsValidIndex(CurCol)) ? R->ReqLevel[CurCol] : -1);
	for (int32 i = 0; i < Cells.Num(); ++i)
	{
		if (Cells[i].PetUid < 0) { continue; }
		S += FString::Printf(TEXT("  ячейка %d (id %d): pet uid=%d\n"), i, kGridFirstId + i, Cells[i].PetUid);
	}
	return S;
}

FString UAsdaPetEvolutionSubsystem::DumpNotices() const
{
	FString S = TEXT("=== окна 43500/43400/43450: событие → loc → канал ===\n");
	auto Row = [this, &S](const TCHAR* Event, int32 LocId, const TCHAR* Channel, const TCHAR* Where)
	{
		S += FString::Printf(TEXT("  %-52s %6d  %-26s %s  «%s»\n"),
			Event, LocId, Channel, Where, *Loc(LocId).Left(60));
	};
	Row(TEXT("init 43501 заголовок"),                109960, TEXT("C (подпись)"),        TEXT("ui_core.c:89813"));
	Row(TEXT("init 43743 «Основной питомец»"),       109964, TEXT("C (подпись)"),        TEXT("ui_core.c:89851"));
	Row(TEXT("init 43744 «Материал 1»"),             109965, TEXT("C (подпись)"),        TEXT("ui_core.c:89851"));
	Row(TEXT("init 43745 «Материал 2»"),             109966, TEXT("C (подпись)"),        TEXT("ui_core.c:89851"));
	Row(TEXT("init 43876 «Развитие»"),               109933, TEXT("C (подпись кнопки)"), TEXT("ui_core.c:89869"));
	Row(TEXT("init 43879 «Закрыть»"),                109934, TEXT("C (подпись кнопки)"), TEXT("ui_core.c:89872"));
	Row(TEXT("init 43431 «Вещи питомца»"),           109962, TEXT("C (подпись)"),        TEXT("ui_core.c:89880"));
	Row(TEXT("init 43453 «Рост»"),                   109963, TEXT("C (подпись)"),        TEXT("ui_core.c:89891"));
	Row(TEXT("ГЕЙТ: питомец призван → окно не откр."),109974, TEXT("A (модальный нотис)"),TEXT("ui_core.c:89964"));
	Row(TEXT("окно открылось → 43877"),              109967, TEXT("C (подсказка 43877)"),TEXT("ui_core.c:89979"));
	Row(TEXT("выбрана стадия → 43877"),              109968, TEXT("C (подсказка 43877)"),TEXT("skill_buff.c:37965"));
	Row(TEXT("43876, слоты НЕ полны → 43877"),       109971, TEXT("C (подсказка 43877)"),TEXT("ui_core.c:90322"));
	Row(TEXT("43876, слоты полны → msgbox 70"),      109972, TEXT("B (подтверждение)"),  TEXT("ui_core.c:90318"));
	Row(TEXT("  кнопка «ОК» msgbox 70"),             108510, TEXT("B (кнопка)"),         TEXT("item_inventory.c:51110"));
	Row(TEXT("  кнопка «Отмена» msgbox 70"),         108511, TEXT("B (кнопка)"),         TEXT("item_inventory.c:51113"));
	Row(TEXT("слот материала ЗАНЯТ → 43749+m"),      109970, TEXT("C (подпись)"),        TEXT("item_inventory.c:65165"));
	Row(TEXT("слот материала ПУСТ → 43749+m"),       109969, TEXT("C (подпись)"),        TEXT("item_inventory.c:65177"));
	Row(TEXT("6113 код 1 (успех)"),                  109973, TEXT("A (модальный нотис)"),TEXT("npc_mob_pet.c:7684"));
	S += TEXT("  6113 коды 3 / 4 / 7 (отказ)          —       —                          npc_mob_pet.c:7654  ");
	S += TEXT("★ нотиса у оригинала НЕТ (весь разбор внутри `if (result == 1)`)\n");
	S += TEXT("  ПАРНЫЕ ВЕТКИ (реестр их теряет, закон notices-registry-lossy-on-ternary):\n");
	S += TEXT("    109970 / 109969 — «Питомец готов» / «Не выбрано»,  item_inventory.c:65165 / :65177\n");
	S += TEXT("    msgbox 70 / 109971 — подтверждение / «не выбраны материалы», ui_core.c:90318 / :90322\n");
	S += TEXT("  ★ Чат (EAsdaMsgTarget) у питомцев НЕ используется — доказано двумя методами (отчёт §6.1).\n");
	return S;
}

namespace {
// ═══════════════════════════════ КОНСОЛЬНЫЕ КОМАНДЫ ═══════════════════════════════════════════
	// Подсистема текущего игрового экземпляра (у консольных команд `this` нет).
	UAsdaPetEvolutionSubsystem* EvoSubsystem(UWorld* W)
	{
		UGameInstance* GI = W ? W->GetGameInstance() : nullptr;
		return GI ? GI->GetSubsystem<UAsdaPetEvolutionSubsystem>() : nullptr;
	}
}

#if !UE_BUILD_SHIPPING
bool UAsdaPetEvolutionSubsystem::DebugShowPage(bool bWide)
{
	if (!EnsureEvo()) { return false; }
	if (UAsdaWindowSubsystem* WM = Windows()) { WM->SetWindowVisible(kEvoWindowId, true); }
	ApplyPage(bWide);
	return true;
}
#endif

#if ASDA_DEV_CONSOLE   // ★ античит А2: дев-команда, в Shipping не регистрируется
static FAutoConsoleCommandWithWorldAndArgs GCmdPetEvoOpen(
	TEXT("asda.Pet.EvoOpen"),
	TEXT("asda.Pet.EvoOpen [uid] — открыть окно 43500 на питомце (без аргумента — на выбранном в 43000)."),
	FConsoleCommandWithWorldAndArgsDelegate::CreateStatic([](const TArray<FString>& Args, UWorld* W)
	{
		UAsdaPetEvolutionSubsystem* S = EvoSubsystem(W);
		if (!S) { return; }
		int32 Uid = -1;
		if (Args.Num() > 0) { Uid = FCString::Atoi(*Args[0]); }
		else if (UAsdaPetSubsystem* P = W && W->GetGameInstance() ? W->GetGameInstance()->GetSubsystem<UAsdaPetSubsystem>() : nullptr)
		{
			Uid = P->GetSelectedUniqueId();
		}
		UE_LOG(LogAsdaPetEvo, Log, TEXT("[питомец-развитие] asda.Pet.EvoOpen %d → %s"),
			Uid, S->OpenEvolution(Uid) ? TEXT("открыто") : TEXT("гейт/отказ"));
	}));
#endif // ASDA_DEV_CONSOLE

#if ASDA_DEV_CONSOLE   // ★ античит А2: дев-команда, в Shipping не регистрируется
static FAutoConsoleCommandWithWorld GCmdPetEvoClose(
	TEXT("asda.Pet.EvoClose"), TEXT("Закрыть окно 43500."),
	FConsoleCommandWithWorldDelegate::CreateStatic([](UWorld* W)
	{
		if (UAsdaPetEvolutionSubsystem* S = EvoSubsystem(W)) { S->CloseEvolution(); }
	}));
#endif // ASDA_DEV_CONSOLE

#if ASDA_DEV_CONSOLE   // ★ античит А2: дев-команда, в Shipping не регистрируется
static FAutoConsoleCommandWithWorld GCmdPetEvoControls(
	TEXT("asda.Pet.EvoControls"), TEXT("Дамп разобранных контролов окон 43500/43400/43450."),
	FConsoleCommandWithWorldDelegate::CreateStatic([](UWorld* W)
	{
		if (UAsdaPetEvolutionSubsystem* S = EvoSubsystem(W)) { UE_LOG(LogAsdaPetEvo, Log, TEXT("\n%s"), *S->DumpControls()); }
	}));
#endif // ASDA_DEV_CONSOLE

#if ASDA_DEV_CONSOLE   // ★ античит А2: дев-команда, в Shipping не регистрируется
static FAutoConsoleCommandWithWorld GCmdPetEvoState(
	TEXT("asda.Pet.EvoState"), TEXT("Состояние окна 43500: цель, стадия, слоты, гейты."),
	FConsoleCommandWithWorldDelegate::CreateStatic([](UWorld* W)
	{
		if (UAsdaPetEvolutionSubsystem* S = EvoSubsystem(W)) { UE_LOG(LogAsdaPetEvo, Log, TEXT("\n%s"), *S->DumpState()); }
	}));
#endif // ASDA_DEV_CONSOLE

#if ASDA_DEV_CONSOLE   // ★ античит А2: дев-команда, в Shipping не регистрируется
static FAutoConsoleCommandWithWorld GCmdPetEvoMap(
	TEXT("asda.Pet.EvoMap"), TEXT("Карта развития питомца: 7 стадий, породы, уровни, рецепты."),
	FConsoleCommandWithWorldDelegate::CreateStatic([](UWorld* W)
	{
		if (UAsdaPetEvolutionSubsystem* S = EvoSubsystem(W)) { UE_LOG(LogAsdaPetEvo, Log, TEXT("\n%s"), *S->DumpMap()); }
	}));
#endif // ASDA_DEV_CONSOLE

// ★★★ ДИАГНОСТИКА ПОКАЗА 43450 (ревизия 25.09.2026, СРЕДНЕЕ-9).
//   ЗАЧЕМ ОТДЕЛЬНАЯ КОМАНДА. Показ подсказки 43450 идёт ТОЛЬКО по наведению на ячейку сетки
//   (`TickMouse` → `IsCursorOverWidget` → `ShowTip`), а в офскрин-прогоне курсора нет: живой
//   прогон 25.09 напечатал «ЗОНЫ ИНТЕРФЕЙСА, курсор (139195488,0)», то есть мусор, и три
//   наведения подряд на заведомо верные точки ВНУТРИ сетки (420,289)-(627,460) не дали ничего.
//   Проверить окно было НЕЧЕМ — и это предел офскрин-стенда, а не дефект кода.
//   ⇒ Заводим ту же пару, что уже есть у окна 43300 (`asda.Pet.EvoTipHover`): команда идёт
//   БОЕВЫМ путём `ShowTip`/`HideTip`, а не пишет в поля (закон `diagnostic-must-use-real-path`).
#if ASDA_DEV_CONSOLE   // ★ античит А2: дев-команда, в Shipping не регистрируется
static FAutoConsoleCommandWithWorldAndArgs GCmdPetEvoTipGrid(
	TEXT("asda.Pet.EvoTipGrid"),
	TEXT("asda.Pet.EvoTipGrid <uid|-1> — БОЕВОЙ путь показа подсказки 43450 по ячейке сетки 43400: ")
	TEXT("−1 = погасить (`sub_92BA11`), иначе показать на этом питомце (`uiShowItemInfoWindow`). ")
	TEXT("Штатный вход — наведение мышью, а в офскрин-прогоне курсора нет."),
	FConsoleCommandWithWorldAndArgsDelegate::CreateStatic([](const TArray<FString>& Args, UWorld* W)
	{
		UAsdaPetEvolutionSubsystem* S = EvoSubsystem(W);
		if (!S || Args.Num() < 1) { return; }
		const int32 Uid = FCString::Atoi(*Args[0]);
		if (Uid < 0) { S->HideTip(); } else { S->ShowTip(Uid); }
		UE_LOG(LogAsdaPetEvo, Log, TEXT("[питомец-развитие] asda.Pet.EvoTipGrid %d"), Uid);
	}));
#endif // ASDA_DEV_CONSOLE

#if ASDA_DEV_CONSOLE   // ★ античит А2: дев-команда, в Shipping не регистрируется
static FAutoConsoleCommandWithWorld GCmdPetEvoGrid(
	TEXT("asda.Pet.EvoGrid"), TEXT("Что сейчас в тридцати ячейках сетки 43400 и по какому фильтру."),
	FConsoleCommandWithWorldDelegate::CreateStatic([](UWorld* W)
	{
		if (UAsdaPetEvolutionSubsystem* S = EvoSubsystem(W)) { UE_LOG(LogAsdaPetEvo, Log, TEXT("\n%s"), *S->DumpGrid()); }
	}));
#endif // ASDA_DEV_CONSOLE

#if ASDA_DEV_CONSOLE   // ★ античит А2: дев-команда, в Shipping не регистрируется
static FAutoConsoleCommandWithWorld GCmdPetEvoNotices(
	TEXT("asda.Pet.EvoNotices"), TEXT("Таблица «событие → loc → канал» окон развития."),
	FConsoleCommandWithWorldDelegate::CreateStatic([](UWorld* W)
	{
		if (UAsdaPetEvolutionSubsystem* S = EvoSubsystem(W)) { UE_LOG(LogAsdaPetEvo, Log, TEXT("\n%s"), *S->DumpNotices()); }
	}));
#endif // ASDA_DEV_CONSOLE

#if ASDA_DEV_CONSOLE   // ★ античит А2: дев-команда, в Shipping не регистрируется
static FAutoConsoleCommandWithWorldAndArgs GCmdPetEvoStage(
	TEXT("asda.Pet.EvoStage"),
	TEXT("asda.Pet.EvoStage <0..6> — выбрать стадию ТЕМ ЖЕ путём, каким это делает клик игрока."),
	FConsoleCommandWithWorldAndArgsDelegate::CreateStatic([](const TArray<FString>& Args, UWorld* W)
	{
		UAsdaPetEvolutionSubsystem* S = EvoSubsystem(W);
		if (!S || Args.Num() < 1) { return; }
		S->OnStageClicked(FCString::Atoi(*Args[0]));
	}));
#endif // ASDA_DEV_CONSOLE

#if ASDA_DEV_CONSOLE   // ★ античит А2: дев-команда, в Shipping не регистрируется
static FAutoConsoleCommandWithWorldAndArgs GCmdPetEvoSlot(
	TEXT("asda.Pet.EvoSlot"),
	TEXT("asda.Pet.EvoSlot <0..2> — открыть сетку 43400 на колонке (путь игрока)."),
	FConsoleCommandWithWorldAndArgsDelegate::CreateStatic([](const TArray<FString>& Args, UWorld* W)
	{
		UAsdaPetEvolutionSubsystem* S = EvoSubsystem(W);
		if (!S || Args.Num() < 1) { return; }
		S->OnMaterialSlotClicked(FCString::Atoi(*Args[0]));
	}));
#endif // ASDA_DEV_CONSOLE

#if ASDA_DEV_CONSOLE   // ★ античит А2: дев-команда, в Shipping не регистрируется
static FAutoConsoleCommandWithWorldAndArgs GCmdPetEvoPick(
	TEXT("asda.Pet.EvoPick"),
	TEXT("asda.Pet.EvoPick <0..29> — выбрать кандидата из сетки (эквивалент двойного клика)."),
	FConsoleCommandWithWorldAndArgsDelegate::CreateStatic([](const TArray<FString>& Args, UWorld* W)
	{
		UAsdaPetEvolutionSubsystem* S = EvoSubsystem(W);
		if (!S || Args.Num() < 1) { return; }
		S->OnGridCellPicked(FCString::Atoi(*Args[0]));
	}));
#endif // ASDA_DEV_CONSOLE

#if ASDA_DEV_CONSOLE   // ★ античит А2: дев-команда, в Shipping не регистрируется
static FAutoConsoleCommandWithWorld GCmdPetEvoOk(
	TEXT("asda.Pet.EvoOk"), TEXT("Нажать кнопку 43876 «Развитие» (путь игрока)."),
	FConsoleCommandWithWorldDelegate::CreateStatic([](UWorld* W)
	{
		if (UAsdaPetEvolutionSubsystem* S = EvoSubsystem(W)) { S->OnOkClicked(); }
	}));
#endif // ASDA_DEV_CONSOLE

#if ASDA_DEV_CONSOLE   // ★ античит А2: дев-команда, в Shipping не регистрируется
static FAutoConsoleCommandWithWorldAndArgs GCmdPetEvoStand(
	TEXT("asda.Pet.EvoCapture"),
	TEXT("asda.Pet.EvoCapture <charId> — стенд отправки 6112: перехват тела + боевой путь подтверждения."),
	FConsoleCommandWithWorldAndArgsDelegate::CreateStatic([](const TArray<FString>& Args, UWorld* W)
	{
		// ★ С-Р1 (26.09.2026), офлайн-гейт: стенд шлёт 6112 без модалки через 4 с; при сбое перехвата ушло бы настоящее слияние (питомцы-материалы теряются).
		static const uint16 kGuardOps[] = { 6112 };
		if (FAsdaOutgoingGate::RefuseDebugWhenOnline(W, TEXT("asda.Pet.EvoCapture"), 15.f, MakeArrayView(kGuardOps))) { return; }
		UAsdaPetEvolutionSubsystem* S = EvoSubsystem(W);
		if (!S) { return; }
		S->StartSendStand(Args.Num() > 0 ? FCString::Atoi(*Args[0]) : 12345);
	}));
#endif // ASDA_DEV_CONSOLE

// ── Синтез записи `PetRecord` (41 б) для тел 6106 и 6113. Раскладка — ctor `sub_45B710`
//   (GameServer.c:36961): `+0 u16 petIndex`, `+2 char[16] name`, `+18 u8 fullness`, `+19 u8 level`,
//   `+20 u8 maxLevel`, `+21 u16 exp`, `+23` 3×`{u16 abilType, i32 value}`.
//   ★ Своего энкодера имени не заводим: cp1251 в порту умеет одна точка — `EncodeCp1251`.
static void EvoWritePetRecord(TArray<uint8>& B, int32 Off, int32 PetIndex, const FString& Name,
	int32 Fullness, int32 Level, int32 MaxLevel, int32 Exp)
{
	if (!B.IsValidIndex(Off + 40)) { return; }
	AsdaBytes::WrI16(B.GetData() + Off + 0, (int16)PetIndex);
	FAsdaLoginData::EncodeCp1251(Name, B.GetData() + Off + 2, 16);
	B[Off + 18] = (uint8)FMath::Clamp(Fullness, 0, 255);
	B[Off + 19] = (uint8)FMath::Clamp(Level, 0, 255);
	B[Off + 20] = (uint8)FMath::Clamp(MaxLevel, 0, 255);
	AsdaBytes::WrI16(B.GetData() + Off + 21, (int16)Exp);
	for (int32 j = 0; j < 3; ++j)
	{
		AsdaBytes::WrI16(B.GetData() + Off + 23 + 6 * j, (int16)(j + 1));
		AsdaBytes::WrI32(B.GetData() + Off + 25 + 6 * j, 10 * (j + 1));
	}
}

// ★ Набор питомцев ПОД РЕЦЕПТ. `asda.Pet.Test` ставит породу `i % 31` и уровень `1 + i % 9`,
//   и рецепт при таком наборе НЕ СОБИРАЕТСЯ (рецепту 0 нужны породы 13 ур.≥4, 1 ур.≥3, 10 ур.≥3).
//   Здесь список синтезируется НАСТОЯЩИМ телом 6106 и идёт через БОЕВОЙ приёмник волны 1
//   (закон `diagnostic-must-use-real-path`), просто породы и уровни заданы осмысленно.
#if ASDA_DEV_CONSOLE   // ★ античит А2: дев-команда, в Shipping не регистрируется
static FAutoConsoleCommandWithWorldAndArgs GCmdPetEvoPets(
	TEXT("asda.Pet.EvoPets"),
	TEXT("asda.Pet.EvoPets [рецепт 0..8] — подставить набор питомцев под рецепт СИНТЕЗОМ настоящего ")
	TEXT("6106: цель (порода базовой стадии карты) + три материала рецепта. Никто не призван."),
	FConsoleCommandWithWorldAndArgsDelegate::CreateStatic([](const TArray<FString>& Args, UWorld* W)
	{
		// ★ С-Р1 (26.09.2026), офлайн-гейт: поддельный 6106: питомцы uid 2000–2004 — Pet.EvoOk/клик отправил бы 6112 с поддельными uid.
		if (FAsdaOutgoingGate::RefuseDebugWhenOnline(W, TEXT("asda.Pet.EvoPets"))) { return; }
		UGameInstance* GI = W ? W->GetGameInstance() : nullptr;
		UAsdaPetSubsystem* P = GI ? GI->GetSubsystem<UAsdaPetSubsystem>() : nullptr;
		if (!P) { return; }
		const int32 RecipeId = (Args.Num() > 0) ? FCString::Atoi(*Args[0]) : 0;
		const FAsdaPetUnionRecipeRow* R = P->GetUnionRecipe(RecipeId);
		if (!R)
		{
			UE_LOG(LogAsdaPetEvo, Error, TEXT("[питомец-развитие] рецепта %d в DT_PetUnionRecipe нет"), RecipeId);
			return;
		}
		// Цель — питомец ПОРОДЫ ПЕРВОГО МАТЕРИАЛА: у рецепта 0 это порода 13, а у неё ключ карты 0,
		//   то есть карта развития есть и стадия 0 — она сама.
		struct FSeed { int32 Uid; int32 Pet; int32 Level; };
		TArray<FSeed> Seeds;
		Seeds.Add({ 2000, R->ReqPetIndex.IsValidIndex(0) ? R->ReqPetIndex[0] : 0,
		            FMath::Max(1, R->ReqLevel.IsValidIndex(0) ? R->ReqLevel[0] : 1) });
		for (int32 i = 0; i < 3; ++i)
		{
			Seeds.Add({ 2001 + i, R->ReqPetIndex.IsValidIndex(i) ? R->ReqPetIndex[i] : 0,
			            FMath::Max(1, R->ReqLevel.IsValidIndex(i) ? R->ReqLevel[i] : 1) });
		}
		// Плюс один заведомо НЕподходящий — чтобы фильтр сетки было видно.
		Seeds.Add({ 2004, (R->ReqPetIndex.IsValidIndex(0) ? R->ReqPetIndex[0] : 0), 1 });

		TArray<uint8> Body;
		Body.SetNumZeroed(5 + 46 * Seeds.Num());
		AsdaBytes::WrI32(Body.GetData() + 0, P->GetSlots().Num() >= 0 ? 0 : 0);  // charId допишем ниже
		Body[4] = 0;                                                             // bLast
		for (int32 i = 0; i < Seeds.Num(); ++i)
		{
			const int32 O = 5 + 46 * i;
			AsdaBytes::WrI32(Body.GetData() + O, Seeds[i].Uid);
			EvoWritePetRecord(Body, O + 4, Seeds[i].Pet,
				FString::Printf(TEXT("Эво%d"), i), 100, Seeds[i].Level, FMath::Max(Seeds[i].Level, 5), 0);
			Body[O + 45] = 0;                                                    // никто НЕ призван
		}
		// charId приёмник сверяет с `SelectedCharId`; без логина он 0 — и 0 в теле его устроит.
		P->OnPetPacket(OP_PET_LIST, Body);
		UE_LOG(LogAsdaPetEvo, Log,
			TEXT("[питомец-развитие] EvoPets: рецепт %d ⇒ результат порода %d ур.%d; нужны %d(ур.%d) %d(ур.%d) %d(ур.%d); ")
			TEXT("подставлено %d питомцев (цель uid 2000, материалы 2001-2003, лишний 2004)"),
			RecipeId, R->ResultPetIndex, R->ResultPetLevel,
			R->ReqPetIndex[0], R->ReqLevel[0], R->ReqPetIndex[1], R->ReqLevel[1],
			R->ReqPetIndex[2], R->ReqLevel[2], Seeds.Num());
		UE_LOG(LogAsdaPetEvo, Log, TEXT("%s"), *P->DumpState());
	}));
#endif // ASDA_DEV_CONSOLE

// ★ Синтез НАСТОЯЩЕГО тела 6113 (56 б) и прогон через БОЕВОЙ приёмник волны 1.
//   Раскладка — сборщик сервера `sub_45B9D0` (GameServer.c:37077) и заполнение `sub_4590E0`
//   (:36085-36100): `+0 i32 charId`, `+4 u8 result`, `+5 i32 newPetUid`, `+9` PetRecord(41),
//   `+50/+52/+54 u16` — МЛАДШИЕ 16 БИТ uid трёх съеденных материалов.
#if ASDA_DEV_CONSOLE   // ★ античит А2: дев-команда, в Shipping не регистрируется
static FAutoConsoleCommandWithWorldAndArgs GCmdPetEvoResult(
	TEXT("asda.Pet.EvoResult"),
	TEXT("asda.Pet.EvoResult <код 1|3|4|7> [newUid] [порода] [уровень] [mat0] [mat1] [mat2] — ")
	TEXT("синтезировать настоящее тело 6113 (56 б) и скормить БОЕВОМУ приёмнику OnPetPacket."),
	FConsoleCommandWithWorldAndArgsDelegate::CreateStatic([](const TArray<FString>& Args, UWorld* W)
	{
		// ★ С-Р1 (26.09.2026), офлайн-гейт: поддельный 6113 (результат слияния).
		if (FAsdaOutgoingGate::RefuseDebugWhenOnline(W, TEXT("asda.Pet.EvoResult"))) { return; }
		UGameInstance* GI = W ? W->GetGameInstance() : nullptr;
		UAsdaPetSubsystem* P = GI ? GI->GetSubsystem<UAsdaPetSubsystem>() : nullptr;
		if (!P || Args.Num() < 1) { return; }
		const int32 Code   = FCString::Atoi(*Args[0]);
		const int32 NewUid = (Args.Num() > 1) ? FCString::Atoi(*Args[1]) : 3000;
		const int32 Pet    = (Args.Num() > 2) ? FCString::Atoi(*Args[2]) : 21;
		const int32 Level  = (Args.Num() > 3) ? FCString::Atoi(*Args[3]) : 4;
		const int32 M0     = (Args.Num() > 4) ? FCString::Atoi(*Args[4]) : 2001;
		const int32 M1     = (Args.Num() > 5) ? FCString::Atoi(*Args[5]) : 2002;
		const int32 M2     = (Args.Num() > 6) ? FCString::Atoi(*Args[6]) : 2003;

		TArray<uint8> Body;
		Body.SetNumZeroed(56);
		AsdaBytes::WrI32(Body.GetData() + 0, 0);                 // charId (без логина он 0)
		Body[4] = (uint8)FMath::Clamp(Code, 0, 255);
		AsdaBytes::WrI32(Body.GetData() + 5, NewUid);
		EvoWritePetRecord(Body, 9, Pet, TEXT("Эволюция"), 100, Level, FMath::Max(Level, 5), 0);
		// ★ 16-битное усечение — ДЕФЕКТ ОРИГИНАЛА, воспроизводим его же (GameServer.c:36090).
		AsdaBytes::WrI16(Body.GetData() + 50, (int16)(M0 & 0xFFFF));
		AsdaBytes::WrI16(Body.GetData() + 52, (int16)(M1 & 0xFFFF));
		AsdaBytes::WrI16(Body.GetData() + 54, (int16)(M2 & 0xFFFF));
		P->OnPetPacket(OP_PET_UNION_RESULT, Body);
		UE_LOG(LogAsdaPetEvo, Log,
			TEXT("[питомец-развитие] EvoResult: код=%d новый uid=%d порода=%d ур.=%d, съедены %d/%d/%d"),
			Code, NewUid, Pet, Level, M0, M1, M2);
	}));
#endif // ASDA_DEV_CONSOLE
