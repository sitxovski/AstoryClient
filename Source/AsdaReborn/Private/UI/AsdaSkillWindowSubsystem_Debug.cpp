// Copyright (c) 2026 @sitxovski (Discord)
// AsdaSkillWindowSubsystem_Debug.cpp — ★ R3-c 12.11.2026 (разрез AsdaSkillWindowSubsystem.cpp, дословный перенос): тексты/нотисы, тестовые подстановки, симуляции, дампы, дев-команды, команды меню.
//   Общее для частей — в AsdaSkillWindowSubsystem_Private.h; шапка и реверс — в AsdaSkillWindowSubsystem.cpp.
#include "UI/AsdaSkillWindowSubsystem.h"


#if PLATFORM_WINDOWS
// ★ Системный интервал двойного клика: оригинал получает событие 0xD от самой Windows
//   (`WM_LBUTTONDBLCLK`), то есть по значению «Панель управления → Мышь». В Slate такого запроса нет
//   (`FSlateApplication` умеет только маршрутизировать уже готовое событие), поэтому берём его у ОС
//   тем же вызовом, каким его берёт оконная процедура оригинала.
#include "Windows/AllowWindowsPlatformTypes.h"
#include "Windows/MinWindows.h"
#include "Windows/HideWindowsPlatformTypes.h"
#endif
#include "AsdaSkillWindowSubsystem_Private.h"   // ★ R3-c 12.11.2026: общее для частей AsdaSkillWindowSubsystem*.cpp

// ★ Гейты клика по узлу: эталон при отказе МОЛЧИТ (`skillWndSelectSlot@0xE53C80`). 0 = как в эталоне.
static ASDA_DEV_CVAR(int32, CVarSkillExplainRefusal,
	TEXT("asda.Skill.ExplainRefusal"), 0,
	TEXT("1 = писать в чат причину, почему клик по узлу не привёл к изучению (эталон молчит)."),
	ECVF_Default);

// ══════════════════════════════ ЗОНА ИНТЕРФЕЙСА ════════════════════════════════════════════════
bool UAsdaSkillWindowSubsystem::GetWindowRect(FVector2D& OutMin, FVector2D& OutMax) const
{
	if (!IsWindowOpen() || !Win) { return false; }
	// Корпус окна: берём геометрию контейнера сетки и шапки — не «объединение всех узлов»
	//   (закон ui-hierarchy-helper: rect строится по узлам корпуса).
	const UWidget* Body = AsdaUi::FindNodeByName(Win, TEXT("SkillTree_Wheel_grp"), TEXT("SkillTree_Wheel"));
	const UWidget* Head = AsdaUi::FindNodeByName(Win, TEXT("Skill_Base_grp"), TEXT("Skill_Base"));
	bool bAny = false;
	FVector2D Mn(FLT_MAX, FLT_MAX), Mx(-FLT_MAX, -FLT_MAX);
	auto Acc = [&](const UWidget* W)
	{
		if (!W) { return; }
		// ★ Аудит №4 (A9/B1): живой абсолютный прямоугольник — общая точка `AsdaUi::WidgetAbsRect`
		//   (она же отсеивает нулевую геометрию). Накопитель min/max остаётся здесь.
		FVector2D P, PMax;
		if (!AsdaUi::WidgetAbsRect(W, P, PMax)) { return; }
		Mn.X = FMath::Min(Mn.X, P.X); Mn.Y = FMath::Min(Mn.Y, P.Y);
		Mx.X = FMath::Max(Mx.X, PMax.X); Mx.Y = FMath::Max(Mx.Y, PMax.Y);
		bAny = true;
	};
	Acc(Body); Acc(Head);
	if (!bAny) { return false; }
	OutMin = Mn; OutMax = Mx;
	return true;
}

// ══════════════════════════════ УТИЛИТЫ ════════════════════════════════════════════════════════
// ★ Аудит №3 (A1): тело переехало в общую точку `AsdaUi::LocRaw` (D10 аудита №2) — здесь остался
//   вызов с ТЕМ ЖЕ поведением: сырая строка (закон `ui-text-color-exs`: `GetTextByIdRaw` сохраняет
//   inline-теги цвета и кегля, `GetTextById` их режет), а если строки нет — диагностическое
//   «[#N]» (до 11.10.2026 — «[loc N]», N9) (`bDebugFallback = true`, вид, который выбрали окно навыков и самоцветы).
FString UAsdaSkillWindowSubsystem::LocRaw(int32 Id) const
{
	// ★ Волна 1 (14.09.2026, В2): общая точка `AsdaNotice::LocRaw` (поведение то же).
	return AsdaNotice::LocRaw(GetGameInstance(), Id, /*bDebugFallback*/ true);
}

// ★★★ ДВА РАЗНЫХ КАНАЛА СООБЩЕНИЙ. Раньше окно сваливало всё в системные строки чата — это не 1:1.
//   Эталон различает их явно:
//     • `CItem_BuildTooltipText(g_pItemTooltipUI, 0, −1, текст, 1)` = ОДНОКНОПОЧНЫЙ MSGBOX
//       (`ONEBUT_MSGBOX` 1500: текст `ONEBUT_TEXT` 1501, кнопка `ONEBUT_BUT00` 1503 с подписью 2288,
//       полоса `ONEBUT_GAUGE` 1502 скрыта). Так показываются 9421, 12854, 12855 и ВСЕ отказы 5254
//       (2200/2201/2202/2206/2207/2208/111700/111701).
//     • `CChat_AddTokenizedLines(g_pChatWnd, текст, −11514, 10000, 0)` = СТРОКА В ЧАТ. Так уходит
//       ровно один текст этого окна — 107022 «окно занято другим экраном» (skill_buff.c:32753).
//   ⚠ Тот же вызов с msgClass = 1 и id = 41 — это ДВУХкнопочное подтверждение (нотис 9415),
//   оно уже реализовано через `ShowGameConfirm`.
//   ★★★ АУДИТ №3 (A1): полотно `ONEBUT_MSGBOX` (открытие, текст 1501 центр/центр по rect 270×100,
//   подпись кнопки 1503 из `DT_GameText`, гашение полосы 1502, реле закрытия) собирает ОКОННАЯ
//   подсистема — `UAsdaWindowSubsystem::ShowGameMessage` (D2 аудита №2, туда же сведены снасть и
//   самоцветы). Здесь остался вызов: у окна навыков своё — только РЕШЕНИЕ, что показать msgbox.
void UAsdaSkillWindowSubsystem::MsgBox1(const FString& Text) const
{
	if (Text.IsEmpty()) { return; }   // пустой текст ⇒ ничего не показываем (как и прежняя копия)
	// ★ Канал фолбэка — `SkillRefused`: если WBP не загрузился, отказ навыка должен уйти в СВОЮ ленту,
	//   как делала прежняя копия окна (общая точка по умолчанию пишет в `ItemRefused`).
	// ★ Волна 1 (14.09.2026, В2): единый маршрут `AsdaNotice::MsgBox` — окно 1500, без оконной подсистемы —
	//   строка в ленту той же категорией. Категория/подпись кнопки — прежние.
	if (!AsdaNotice::MsgBox(GetGameInstance(), EAsdaMsgCategory::SkillRefused, Text, AsdaLoc::kBtnOk))
	{
		UE_LOG(LogAsdaSkillWin, Warning, TEXT("[навыки] нет UAsdaWindowSubsystem; сообщение ушло в ленту: %s"), *Text);
		return;
	}
	UE_LOG(LogAsdaSkillWin, Log, TEXT("[навыки] msgbox: %s"), *Text);
}

void UAsdaSkillWindowSubsystem::Notice(int32 LocId) const
{
	MsgBox1(LocRaw(LocId));
}

// Подсказка при «пустом» клике по узлу. Эталон в этом месте НЕ показывает ничего — по умолчанию молчим
//   и мы, оставляя только лог. Ключ `asda.Skill.ExplainRefusal 1` выводит причину строкой в чат
//   (именно строкой: модальное окно на каждый клик — точно не то, что делает оригинал).
void UAsdaSkillWindowSubsystem::RefusalHint(int32 LocId) const
{
	UE_LOG(LogAsdaSkillWin, Log, TEXT("[изучение] клик отклонён гейтом: нотис %d «%s» (эталон молчит)"),
		LocId, *LocRaw(LocId));
	if (CVarSkillExplainRefusal.GetValueOnGameThread() != 0) { ChatNotice(LocId); }
}

// Единственный текст окна, который эталон отправляет СТРОКОЙ В ЧАТ (107022).
void UAsdaSkillWindowSubsystem::ChatNotice(int32 LocId) const
{
	// ★ Волна 1 (14.09.2026, В2): маршрут — фасад `AsdaNotice` (категория та же, поведение то же).
	if (AsdaNotice::Chat(GetGameInstance(), EAsdaMsgCategory::SkillRefused, LocId)) { return; }
	UE_LOG(LogAsdaSkillWin, Log, TEXT("[навыки] в чат %d: %s"), LocId, *LocRaw(LocId));
}

// ══════════════════════════════ ЭМУЛЯЦИЯ И ДАМПЫ ═══════════════════════════════════════════════
void UAsdaSkillWindowSubsystem::SetPointsForTest(int32 Points)
{
	SkillPoints = Points;
	if (IsWindowOpen()) { RebuildAll(); }
}

void UAsdaSkillWindowSubsystem::SetJobForTest(int32 JobId, int32 SubJobId)
{
	JobOverride = JobId;
	SubJobOverride = SubJobId;
	if (IsWindowOpen()) { RebuildAll(); }
}

void UAsdaSkillWindowSubsystem::SimulateCompactList(bool bAllAvailable)
{
	// Строим тело 5252 из навыков ВСЕХ деревьев своей полосы (первые 35 — как у сервера).
	if (!Db) { return; }
	TArray<int32> Codes;
	for (int32 T = 0; T < 4; ++T)
	{
		TArray<int32> Part; Db->GetTreeSkills(GetSelfBand(), T, Part);
		Codes.Append(Part);
	}
	TArray<uint8> Body;
	const int32 N = FMath::Min(35, Codes.Num());
	Body.SetNumZeroed(105);
	for (int32 i = 0; i < N; ++i)
	{
		const uint16 C = (uint16)(int16)Codes[i];
		Body[3 * i]     = (uint8)(C & 0xFF);
		Body[3 * i + 1] = (uint8)(C >> 8);
		Body[3 * i + 2] = 1;   // уровень 1
	}
	for (int32 i = N; i < 35; ++i) { Body[3 * i] = 0xFF; Body[3 * i + 1] = 0xFF; }
	ApplyCompactList(Body);
	if (bAllAvailable) { for (TPair<int32, FAsdaLearnedSkill>& It : Learned) { It.Value.bAvailable = true; } }
	if (IsWindowOpen()) { RebuildAll(); }
}

void UAsdaSkillWindowSubsystem::SimulateFullList(bool bAllAvailable)
{
	if (!Db) { return; }
	TArray<int32> Codes;
	for (int32 T = 0; T < 4; ++T)
	{
		TArray<int32> Part; Db->GetTreeSkills(GetSelfBand(), T, Part);
		Codes.Append(Part);
	}
	TArray<uint8> Body;
	Body.SetNumZeroed(Codes.Num() * 34);
	for (int32 i = 0; i < Codes.Num(); ++i)
	{
		const int32 Off = i * 34;
		const uint16 C = (uint16)(int16)Codes[i];
		Body[Off]     = (uint8)(C & 0xFF);
		Body[Off + 1] = (uint8)(C >> 8);
		Body[Off + 2] = 0;                                  // уровень 0 = не выучен
		Body[Off + 3] = bAllAvailable ? 1 : 0;              // «можно вкачивать»
	}
	ApplyListRecords(Body, /*bReplace*/ true);
	if (IsWindowOpen()) { RebuildAll(); }
}

void UAsdaSkillWindowSubsystem::SimulateUpgradeResult(int32 SkillCode, int32 NewLevel, int32 NewPoints)
{
	TArray<uint8> B; B.SetNumZeroed(111);
	B[0] = 1;
	B[1] = (uint8)((uint16)(int16)NewPoints & 0xFF);
	B[2] = (uint8)(((uint16)(int16)NewPoints >> 8) & 0xFF);
	B[3] = (uint8)((uint16)(int16)SkillCode & 0xFF);
	B[4] = (uint8)(((uint16)(int16)SkillCode >> 8) & 0xFF);
	B[5] = (uint8)FMath::Clamp(NewLevel, 0, 255);
	ApplyUpgradeResult(B);
	if (IsWindowOpen()) { RebuildAll(); }
}

void UAsdaSkillWindowSubsystem::SimulateUpgradeFail(int32 Code)
{
	TArray<uint8> B; B.SetNumZeroed(111);
	B[0] = (uint8)FMath::Clamp(Code, 0, 255);
	ApplyUpgradeResult(B);
}

FString UAsdaSkillWindowSubsystem::DumpCooldownCurtains() const
{
	const double Now = AsdaCooldownNowS(GetGameInstance());   // ★ 25.10.2026: часы реестра откатов
	int32 Planes = 0;
	for (const FAsdaSkillSlotWidgets& S : Slots) { Planes += S.Curtain.Plane ? 1 : 0; }
	FString R = FString::Printf(
		TEXT("=== ШТОРКИ: КНИГА НАВЫКОВ (показ узла — ЭТАЛОННЫЙ, skillAddAndRefreshBars@0xE49A20; ")
		TEXT("наше — живой прогон вместо кадра 120) ===\n")
		TEXT("окно %s, планов шторки найдено %d/%d, откатов в реестре окна %d\n"),
		IsWindowOpen() ? TEXT("открыто") : TEXT("закрыто"), Planes, Slots.Num(), Cooldowns.Num());

	int32 Lit = 0;
	for (int32 i = 0; i < Slots.Num(); ++i)
	{
		const FAsdaSkillSlotWidgets& S = Slots[i];
		if (S.Curtain.ShownState == 0) { continue; }
		++Lit;
		R += FString::Printf(TEXT("  узел %-2d навык %-6d состояние %d осталось %.2f с\n"),
			i, S.Curtain.ShownKey, S.Curtain.ShownState, FMath::Max(0.0, S.Curtain.ShownEndS - Now));
	}
	if (Lit == 0) { R += TEXT("  горящих шторок нет\n"); }
	// Реестр окна печатаем всегда: он и есть источник, который читает панель через мост.
	for (const TPair<int32, FSkillCooldown>& It : Cooldowns)
	{
		R += FString::Printf(TEXT("  реестр: навык %d · осталось %.2f из %.2f с\n"),
			It.Key, FMath::Max(0.f, CooldownRemainLive(It.Value)), It.Value.TotalSec);
	}
	return R;
}

FString UAsdaSkillWindowSubsystem::DumpControls() const
{
	FString S = TEXT("=== КОНТРОЛЫ ОКНА НАВЫКОВ (SKILL_WIN 12000) ===\n");
	auto Line = [&S](const TCHAR* Name, int32 IdNum, const UWidget* W)
	{
		S += FString::Printf(TEXT("  %-22s idnum=%-6d %s\n"), Name, IdNum, W ? TEXT("НАЙДЕН") : TEXT("нет"));
	};
	Line(TEXT("SKILL_WIN_CLOSEBTN"), 12009, CloseButton);
	Line(TEXT("Skill_Gear"),         12002, GearStatic);
	Line(TEXT("Skill_AniGear"),      12003, GearAnimated);
	Line(TEXT("SKILL_POINTTXT"),     1092,  TxtSkillPoint);
	Line(TEXT("SKILL_POINTBACK"),    12262, TxtPointBack);
	Line(TEXT("SKILL_TXT00"),        12261, TxtTreeCaption);
	Line(TEXT("SKILL_POINT_TXT"),    12007, TxtTreeSpent);
	Line(TEXT("SKILL_BLINE_2"),      12410, Job4PointFrame);
	Line(TEXT("SKILL_POINTBACK2"),   12411, TxtJob4Back);
	Line(TEXT("SKILL_POINTTXT2"),    12413, TxtJob4Point);
	Line(TEXT("SOUL_SP_W"),          12400, Job4Frame);
	Line(TEXT("SOUL_SP_W_T"),        12401, Job4Button);
	Line(TEXT("SKILL_SOUL_MAIN"),    56300, PreviewPanel);
	Line(TEXT("SKILL_SOUL_TEXT_p1"), 56301, PreviewTitle);
	for (int32 k = 0; k < SoulRows.Num(); ++k)
	{
		const FAsdaSoulPreviewRow& R = SoulRows[k];
		S += FString::Printf(
			TEXT("  хранитель строка %d: портрет=%s имя=%s иконка=%s плашек=%d  запись=%d навык=%d бафф=%d ступень=%d\n"),
			k, R.Illust ? TEXT("+") : TEXT("-"), R.Name ? TEXT("+") : TEXT("-"), R.NextIcon ? TEXT("+") : TEXT("-"),
			R.Tiers.Num(), R.GuardIndex, R.GuardianSkillId, R.BuffSkillId, R.Tier);
	}
	for (int32 k = 0; k < TreeArt.Num(); ++k)
	{
		S += FString::Printf(TEXT("  подложка дерева %2d: %s\n"), k, TreeArt[k] ? TEXT("НАЙДЕНА") : TEXT("нет"));
	}
	for (int32 i = 0; i < Tab3Buttons.Num(); ++i) { S += FString::Printf(TEXT("  SKILL_CHARA_BTN1_OPT%d  idnum=12005  %s\n"), i, Tab3Buttons[i] ? TEXT("НАЙДЕН") : TEXT("нет")); }
	for (int32 i = 0; i < Tab4Buttons.Num(); ++i) { S += FString::Printf(TEXT("  SKILL_CHARA_BTN2_OPT%d  idnum=12006  %s\n"), i, Tab4Buttons[i] ? TEXT("НАЙДЕН") : TEXT("нет")); }
	S += TEXT("--- 45 узлов сетки ---\n");
	for (int32 i = 0; i < Slots.Num(); ++i)
	{
		const FAsdaSkillSlotWidgets& W = Slots[i];
		S += FString::Printf(TEXT("  узел %02d: grp=%s рамка=%s иконка=%s уровень=%s доступ=%s выбран=%s кд=%s ced=%s монет=%d  навык=%d\n"),
			i, W.Group ? TEXT("+") : TEXT("-"), W.Frame ? TEXT("+") : TEXT("-"), W.Icon ? TEXT("+") : TEXT("-"),
			W.Level ? TEXT("+") : TEXT("-"), W.Selectable ? TEXT("+") : TEXT("-"), W.Selected ? TEXT("+") : TEXT("-"),
			W.Cool ? TEXT("+") : TEXT("-"), W.Ced ? TEXT("+") : TEXT("-"), W.CoinIcons.Num(), W.SkillCode);
	}
	return S;
}

FString UAsdaSkillWindowSubsystem::DumpState() const
{
	const int32 Job = GetSelfJob(), Band = GetSelfBand();
	FString S = FString::Printf(
		TEXT("=== СОСТОЯНИЕ ОКНА НАВЫКОВ ===\n")
		TEXT("  класс=%d (%s) подкласс=%d полоса=%d\n")
		TEXT("  окно %s, вкладка=%d (grade %d), прокрутка=%d/%d, выбран узел=%d\n")
		TEXT("  очков=%d, записей в модели=%d, активных кулдаунов=%d\n"),
		Job, Db ? *Db->GetJobName(Job) : TEXT("?"), GetSelfSubJob(), Band,
		IsWindowOpen() ? TEXT("открыто") : TEXT("закрыто"),
		CurTree, UAsdaSkillDatabase::GradeForBandTree(Band, CurTree), ScrollRow, MaxScrollRow(), SelectedSlot,
		SkillPoints, Learned.Num(), Cooldowns.Num());
	if (const UAsdaNetSubsystem* Net = GetGameInstance() ? GetGameInstance()->GetSubsystem<UAsdaNetSubsystem>() : nullptr)
	{
		S += FString::Printf(TEXT("  сеть: очки=%d (1017 блок+39), подкласс=%d (блок+29), профессия=%d (блок+41)\n"),
			Net->SelfSkillPoints, Net->SelfSubJob, Net->SelfJob);
	}
	if (Db) { S += Db->DumpSummary(); }
	return S;
}

// ══════════════════════════════ КОНСОЛЬНЫЕ КОМАНДЫ ═════════════════════════════════════════════
static UAsdaSkillWindowSubsystem* SkGet(UWorld* W)
{
	UGameInstance* GI = W ? W->GetGameInstance() : nullptr;
	return GI ? GI->GetSubsystem<UAsdaSkillWindowSubsystem>() : nullptr;
}

#if ASDA_DEV_CONSOLE   // ★ античит А2: дев-команда, в Shipping не регистрируется
static FAutoConsoleCommandWithWorldAndArgs GSkOpen(
	TEXT("asda.Skill.Open"), TEXT("Открыть окно навыков (SKILL_WIN 12000)."),
	FConsoleCommandWithWorldAndArgsDelegate::CreateLambda([](const TArray<FString>&, UWorld* W)
	{ if (UAsdaSkillWindowSubsystem* S = SkGet(W)) { S->OpenWindow(); } }));
#endif // ASDA_DEV_CONSOLE

#if ASDA_DEV_CONSOLE   // ★ античит А2: дев-команда, в Shipping не регистрируется
static FAutoConsoleCommandWithWorldAndArgs GSkClose(
	TEXT("asda.Skill.Close"), TEXT("Закрыть окно навыков."),
	FConsoleCommandWithWorldAndArgsDelegate::CreateLambda([](const TArray<FString>&, UWorld* W)
	{ if (UAsdaSkillWindowSubsystem* S = SkGet(W)) { S->CloseWindow(); } }));
#endif // ASDA_DEV_CONSOLE

#if ASDA_DEV_CONSOLE   // ★ античит А2: дев-команда, в Shipping не регистрируется
static FAutoConsoleCommandWithWorldAndArgs GSkToggle(
	TEXT("asda.Skill.Toggle"), TEXT("Открыть/закрыть окно навыков."),
	FConsoleCommandWithWorldAndArgsDelegate::CreateLambda([](const TArray<FString>&, UWorld* W)
	{ if (UAsdaSkillWindowSubsystem* S = SkGet(W)) { S->ToggleWindow(); } }));
#endif // ASDA_DEV_CONSOLE

#if ASDA_DEV_CONSOLE   // ★ античит А2: дев-команда, в Shipping не регистрируется
static FAutoConsoleCommandWithWorldAndArgs GSkDump(
	TEXT("asda.Skill.Dump"), TEXT("Дамп КАЖДОГО разобранного контрола окна навыков."),
	FConsoleCommandWithWorldAndArgsDelegate::CreateLambda([](const TArray<FString>&, UWorld* W)
	{ if (UAsdaSkillWindowSubsystem* S = SkGet(W)) { UE_LOG(LogAsdaSkillWin, Log, TEXT("\n%s"), *S->DumpControls()); } }));
#endif // ASDA_DEV_CONSOLE

#if ASDA_DEV_CONSOLE   // ★ античит А2: дев-команда, в Shipping не регистрируется
static FAutoConsoleCommandWithWorldAndArgs GSkState(
	TEXT("asda.Skill.State"), TEXT("Состояние окна навыков: класс, полоса, вкладка, очки, число записей."),
	FConsoleCommandWithWorldAndArgsDelegate::CreateLambda([](const TArray<FString>&, UWorld* W)
	{ if (UAsdaSkillWindowSubsystem* S = SkGet(W)) { UE_LOG(LogAsdaSkillWin, Log, TEXT("\n%s"), *S->DumpState()); } }));
#endif // ASDA_DEV_CONSOLE

#if ASDA_DEV_CONSOLE   // ★ античит А2: дев-команда, в Shipping не регистрируется
static FAutoConsoleCommandWithWorldAndArgs GSkTree(
	TEXT("asda.Skill.Tree"), TEXT("asda.Skill.Tree <0..3> — переключить вкладку дерева."),
	FConsoleCommandWithWorldAndArgsDelegate::CreateLambda([](const TArray<FString>& A, UWorld* W)
	{ if (UAsdaSkillWindowSubsystem* S = SkGet(W)) { S->SetTree(A.Num() > 0 ? FCString::Atoi(*A[0]) : 0); } }));
#endif // ASDA_DEV_CONSOLE

#if ASDA_DEV_CONSOLE   // ★ античит А2: дев-команда, в Shipping не регистрируется
static FAutoConsoleCommandWithWorldAndArgs GSkScroll(
	TEXT("asda.Skill.Scroll"), TEXT("asda.Skill.Scroll <ряд> — прокрутка сетки."),
	FConsoleCommandWithWorldAndArgsDelegate::CreateLambda([](const TArray<FString>& A, UWorld* W)
	{ if (UAsdaSkillWindowSubsystem* S = SkGet(W)) { S->SetScrollRow(A.Num() > 0 ? FCString::Atoi(*A[0]) : 0); } }));
#endif // ASDA_DEV_CONSOLE

#if ASDA_DEV_CONSOLE   // ★ античит А2: дев-команда, в Shipping не регистрируется
static FAutoConsoleCommandWithWorldAndArgs GSkPoints(
	TEXT("asda.Skill.Points"), TEXT("asda.Skill.Points <N> — подставить число свободных очков (проверка без сервера)."),
	FConsoleCommandWithWorldAndArgsDelegate::CreateLambda([](const TArray<FString>& A, UWorld* W)
	{ if (UAsdaSkillWindowSubsystem* S = SkGet(W)) { S->SetPointsForTest(A.Num() > 0 ? FCString::Atoi(*A[0]) : 0); } }));
#endif // ASDA_DEV_CONSOLE

#if ASDA_DEV_CONSOLE   // ★ античит А2: дев-команда, в Shipping не регистрируется
static FAutoConsoleCommandWithWorldAndArgs GSkClass(
	TEXT("asda.Skill.Class"), TEXT("asda.Skill.Class <jobId> [subJobId] — подменить класс (проверка всех наборов навыков без сервера)."),
	FConsoleCommandWithWorldAndArgsDelegate::CreateLambda([](const TArray<FString>& A, UWorld* W)
	{
		if (UAsdaSkillWindowSubsystem* S = SkGet(W))
		{
			const int32 Job = A.Num() > 0 ? FCString::Atoi(*A[0]) : -1;
			const int32 Sub = A.Num() > 1 ? FCString::Atoi(*A[1]) : -1;
			S->SetJobForTest(Job, Sub);
			UE_LOG(LogAsdaSkillWin, Log, TEXT("\n%s"), *S->DumpState());
		}
	}));
#endif // ASDA_DEV_CONSOLE

#if ASDA_DEV_CONSOLE   // ★ античит А2: дев-команда, в Shipping не регистрируется
static FAutoConsoleCommandWithWorldAndArgs GSkSimList(
	TEXT("asda.Skill.Sim.List"), TEXT("Эмуляция 5252 (компактный список выученного) по классу игрока."),
	FConsoleCommandWithWorldAndArgsDelegate::CreateLambda([](const TArray<FString>&, UWorld* W)
	{
		// ★ С-Р1 (26.09.2026), офлайн-гейт (редакция 1, СРЕДН-3): поддельный список выученного 5252 — Skill.Use/Learn пошли бы по поддельному набору.
		if (FAsdaOutgoingGate::RefuseDebugWhenOnline(W, TEXT("asda.Skill.Sim.List"))) { return; }
		if (UAsdaSkillWindowSubsystem* S = SkGet(W)) { S->SimulateCompactList(true); }
	}));
#endif // ASDA_DEV_CONSOLE

#if ASDA_DEV_CONSOLE   // ★ античит А2: дев-команда, в Shipping не регистрируется
static FAutoConsoleCommandWithWorldAndArgs GSkSimFull(
	TEXT("asda.Skill.Sim.Full"), TEXT("Эмуляция 5251 (полный 34-б список, всё доступно к изучению)."),
	FConsoleCommandWithWorldAndArgsDelegate::CreateLambda([](const TArray<FString>&, UWorld* W)
	{
		// ★ С-Р1 (26.09.2026), офлайн-гейт (редакция 1, СРЕДН-3): поддельный полный список 5251 (всё доступно) — 5253/5256 по поддельному набору.
		if (FAsdaOutgoingGate::RefuseDebugWhenOnline(W, TEXT("asda.Skill.Sim.Full"))) { return; }
		if (UAsdaSkillWindowSubsystem* S = SkGet(W)) { S->SimulateFullList(true); }
	}));
#endif // ASDA_DEV_CONSOLE

#if ASDA_DEV_CONSOLE   // ★ античит А2: дев-команда, в Shipping не регистрируется
static FAutoConsoleCommandWithWorldAndArgs GSkSimResult(
	TEXT("asda.Skill.Sim.Result"), TEXT("asda.Skill.Sim.Result <код> <уровень> <очки> — эмуляция успешного ответа 5254."),
	FConsoleCommandWithWorldAndArgsDelegate::CreateLambda([](const TArray<FString>& A, UWorld* W)
	{
		// ★ С-Р1 (26.09.2026), офлайн-гейт: поддельный результат изучения 5254: уровень и очки навыков — дальнейшие 5253/5256 по поддельному набору.
		if (FAsdaOutgoingGate::RefuseDebugWhenOnline(W, TEXT("asda.Skill.Sim.Result"))) { return; }
		if (UAsdaSkillWindowSubsystem* S = SkGet(W) )
		{
			S->SimulateUpgradeResult(A.Num() > 0 ? FCString::Atoi(*A[0]) : 501,
			                         A.Num() > 1 ? FCString::Atoi(*A[1]) : 1,
			                         A.Num() > 2 ? FCString::Atoi(*A[2]) : 0);
		}
	}));
#endif // ASDA_DEV_CONSOLE

#if ASDA_DEV_CONSOLE   // ★ античит А2: дев-команда, в Shipping не регистрируется
static FAutoConsoleCommandWithWorldAndArgs GSkSimFail(
	TEXT("asda.Skill.Sim.Fail"), TEXT("asda.Skill.Sim.Fail <код 2..8> — эмуляция отказа 5254 (нотисы 2201..2208/111700/111701)."),
	FConsoleCommandWithWorldAndArgsDelegate::CreateLambda([](const TArray<FString>& A, UWorld* W)
	{
		// ★ С-Р1 (26.09.2026), офлайн-гейт (редакция 1, СРЕДН-3): поддельный отказ 5254 — снимает ожидание ответа изучения.
		if (FAsdaOutgoingGate::RefuseDebugWhenOnline(W, TEXT("asda.Skill.Sim.Fail"))) { return; }
		if (UAsdaSkillWindowSubsystem* S = SkGet(W)) { S->SimulateUpgradeFail(A.Num() > 0 ? FCString::Atoi(*A[0]) : 2); }
	}));
#endif // ASDA_DEV_CONSOLE

#if ASDA_DEV_CONSOLE   // ★ античит А2: дев-команда, в Shipping не регистрируется
static FAutoConsoleCommandWithWorldAndArgs GSkSimCool(
	TEXT("asda.Skill.Sim.Cool"), TEXT("asda.Skill.Sim.Cool <код> <сек> — эмуляция кулдауна навыка."),
	FConsoleCommandWithWorldAndArgsDelegate::CreateLambda([](const TArray<FString>& A, UWorld* W)
	{
		if (UAsdaSkillWindowSubsystem* S = SkGet(W))
		{
			S->PushCooldownFromServer(A.Num() > 0 ? FCString::Atoi(*A[0]) : 501,
			                          A.Num() > 1 ? FCString::Atoi(*A[1]) : 10);
		}
	}));
#endif // ASDA_DEV_CONSOLE

#if ASDA_DEV_CONSOLE   // ★ античит А2: дев-команда, в Shipping не регистрируется
static FAutoConsoleCommandWithWorldAndArgs GSkLearn(
	TEXT("asda.Skill.Learn"), TEXT("asda.Skill.Learn <код> [online] — РЕАЛЬНАЯ отправка 5253 «вложить очко». Офлайн без кода — 501; "
	                               "на живом сервере — только с явным кодом и аргументом `online`."),
	FConsoleCommandWithWorldAndArgsDelegate::CreateLambda([](const TArray<FString>& A, UWorld* W)
	{
		// ★ С-Р1, редакция 1 (СРЕДН-4): очко навыка вкладывается НЕОБРАТИМО, гейты окна обходятся, а без аргумента уходил
		//   навык 501. Онлайн — только с явным `online` и явным числовым кодом.
		if (FAsdaOutgoingGate::RefuseRiskyWithoutOnlineOptIn(W, TEXT("asda.Skill.Learn <код>"), A, TEXT("необратимое вложение очка навыка 5253 в обход окна"))) { return; }
		const bool bOnline = FAsdaOutgoingGate::HasOnlineOptIn(A);
		const bool bFirstIsOptIn = A.Num() > 0 && A[0].Equals(TEXT("online"), ESearchCase::IgnoreCase);
		if (bOnline && !(A.Num() > 0 && A[0].IsNumeric()))
		{
			UE_LOG(LogAsdaSkillWin, Warning, TEXT("[офлайн-гейт] asda.Skill.Learn: на живом сервере код навыка обязателен (asda.Skill.Learn <код> online) — ничего не отправлено"));
			return;
		}
		// Офлайн — как раньше: первый аргумент как код, без него 501.
		if (UAsdaSkillWindowSubsystem* S = SkGet(W)) { S->RequestLearn((A.Num() > 0 && !bFirstIsOptIn) ? FCString::Atoi(*A[0]) : 501); }
	}));
#endif // ASDA_DEV_CONSOLE

#if ASDA_DEV_CONSOLE   // ★ античит А2: дев-команда, в Shipping не регистрируется
// ★ Аудит 2026-09-02: имя `asda.Skill.Info` закреплено ЗА ЭТОЙ командой (сырая строка таблицы).
//   Однофамилец из `AsdaSkillUseSubsystem.cpp`, который молча затирался при старте, переименован
//   в `asda.Skill.Use.Info` — там разбор гейтов применения, а не содержимое таблицы.
static FAutoConsoleCommandWithWorldAndArgs GSkInfo(
	TEXT("asda.Skill.Info"),
	TEXT("asda.Skill.Info <код> — СЫРАЯ строка навыка из таблицы skill_info.sif: имя, иконка, вид, ")
	TEXT("макс. уровень, подтип, grade/вкладка, ряд-колонка-слот, кулдаун, MP, дальность, AoE, цель, ")
	TEXT("каст, эффект S1, доп. навыки, цены в монетах, описание. Гейты применения (пройдёт ли навык ")
	TEXT("здесь и сейчас) показывает asda.Skill.Use.Info"),
	FConsoleCommandWithWorldAndArgsDelegate::CreateLambda([](const TArray<FString>& A, UWorld* W)
	{
		if (UAsdaSkillWindowSubsystem* S = SkGet(W))
		{
			if (const UAsdaSkillDatabase* Db = S->GetDatabase())
			{
				UE_LOG(LogAsdaSkillWin, Log, TEXT("\n%s"), *Db->DumpSkill(A.Num() > 0 ? FCString::Atoi(*A[0]) : 501));
			}
		}
	}));
#endif // ASDA_DEV_CONSOLE

#if ASDA_DEV_CONSOLE   // ★ античит А2: дев-команда, в Shipping не регистрируется
// ── Перетаскивание навыка в панель: проверка БЕЗ МЫШИ ──
static FAutoConsoleCommandWithWorldAndArgs GSkToHotbar(
	TEXT("asda.Skill.ToHotbar"),
	TEXT("asda.Skill.ToHotbar <код> <слот 0..77> — уложить навык в слот панели со всеми гейтами эталона ")
	TEXT("(подтип не 3/4, уровень > 0, колонка 12 = слот оружия не принимает) + пакет 5067."),
	FConsoleCommandWithWorldAndArgsDelegate::CreateLambda([](const TArray<FString>& A, UWorld* W)
	{
		UAsdaSkillWindowSubsystem* S = SkGet(W);
		if (!S) { return; }
		const int32 Code = A.Num() > 0 ? FCString::Atoi(*A[0]) : -1;
		const int32 Abs  = A.Num() > 1 ? FCString::Atoi(*A[1]) : 0;
		S->DropSkillToHotbarSlot(Code, Abs);
	}));
#endif // ASDA_DEV_CONSOLE

#if ASDA_DEV_CONSOLE   // ★ античит А2: дев-команда, в Shipping не регистрируется
static FAutoConsoleCommandWithWorldAndArgs GSkDragCheck(
	TEXT("asda.Skill.DragCheck"),
	TEXT("asda.Skill.DragCheck <код> — можно ли перетащить навык (гейты `skillCheckResetEligible`)."),
	FConsoleCommandWithWorldAndArgsDelegate::CreateLambda([](const TArray<FString>& A, UWorld* W)
	{
		UAsdaSkillWindowSubsystem* S = SkGet(W);
		if (!S) { return; }
		const int32 Code = A.Num() > 0 ? FCString::Atoi(*A[0]) : -1;
		const UAsdaSkillDatabase* Db = S->GetDatabase();
		const FAsdaSkillDef* D = Db ? Db->Get(Code) : nullptr;
		UE_LOG(LogAsdaSkillWin, Log,
			TEXT("[навык→панель] проверка захвата: навык %d «%s», подтип %d (3/4 нельзя), уровень %d (нужен > 0) ⇒ %s"),
			Code, D ? *D->Name : TEXT("?"), D ? D->SubType : -1, S->GetLearnedLevel(Code),
			S->CanDragSkill(Code) ? TEXT("МОЖНО") : TEXT("НЕЛЬЗЯ"));
	}));
#endif // ASDA_DEV_CONSOLE

// ══════════════════════════ ОТКРЫТИЕ СНАРУЖИ: МЕНЮ И КЛАВИША ═══════════════════════════════════
// Кнопка 73403 главного меню. ⚠ Как и у инвентаря, колбэк прилетает ИЗНУТРИ обработчика чекбокса
//   меню, поэтому действие откладывать нельзя только если оно не трогает сам чекбокс — здесь
//   `ToggleWindow()` лишь показывает/прячет своё окно, реентрантности нет.
void UAsdaSkillWindowSubsystem::OnMenuCommand(int32 ButtonIdNum, int32 /*TargetWindowId*/, bool bOpen)
{
	if (ButtonIdNum != kMenuButtonIdNum) { return; }
	UE_LOG(LogAsdaSkillWin, Log, TEXT("[навыки] кнопка меню %d → %s"),
		ButtonIdNum, bOpen ? TEXT("открыть") : TEXT("закрыть"));
	if (bOpen) { OpenWindow(); } else { CloseWindow(); }
}

// Клавиша «K» (`INTERFACE SKILL`, команда 32). Эталон `hotkeyDispatchAction@0xE3CD90`.
void UAsdaSkillWindowSubsystem::OnGameCommand(int32 Cmd)
{
	if (Cmd != kCmdInterfaceSkill) { return; }
	ToggleWindow();
}
