// Copyright (c) 2026 @sitxovski (Discord)
// Реализация баннера смены профессии 18500 и ветки смены профессии 5042 (шапка — в .h).
//   Волна 2-В (06.10.2026), отчёт `Reports/owner_1006/wave2C_duel_mark_banner.md` §3.
#include "UI/AsdaChangeJobBannerSubsystem.h"
#include "AsdaPerf.h"   // ★ R4-b (14.11.2026): области замера времени игрового потока
#include "UI/AsdaUiVisibilitySubsystem.h"   // ★ 21.10.2026: Alt+Z — флаг «интерфейс скрыт» (владелец)
#include "AsdaNetSubsystem.h"
#include "AsdaBytes.h"                      // RdI16 — очки умений body[13]
#include "UI/AsdaHudSubsystem.h"            // признак `showActive` баннера 18550 (взаимоисключение)
#include "UI/AsdaNotice.h"                  // окно-сообщение 7006 (`CItem_BuildTooltipText(…, 0, -1, …)` = ONEBUT_MSGBOX)
#include "UI/AsdaUiHelpers.h"               // слоты канваса: исходные смещения и перестановка надписей
#include "Resources/AsdaSoundSubsystem.h"   // звук 15 (`csoundPlayName2DStore(g_pSoundMgr, 15, 0)`)
#include "Blueprint/UserWidget.h"
#include "Blueprint/WidgetTree.h"
#include "Components/Image.h"
#include "Components/TextBlock.h"
#include "Engine/Engine.h"
#include "Engine/GameViewportClient.h"
#include "Engine/GameInstance.h"
#include "Engine/World.h"

DEFINE_LOG_CATEGORY_STATIC(LogAsdaJobBanner, Log, All);

namespace AsdaJobBanner
{
	// Путь WBP окна 18500 (сгенерирован 1:1 из `.JUI`, корень `ChangeJob_Banner`).
	static const TCHAR* kWbp = TEXT("/Game/AsdaAssets/UI/Player_UI/Dungeon/WBP_ChangeJob_Banner.WBP_ChangeJob_Banner_C");
	// Z окна: над HUD, рядом с баннером 18550 (`WBP_Con_Banner2`, 900).
	static constexpr int32 kZOrder = 901;
	// Надписи профессий 18502+i в порядке IDNum (`CChangeJobBanner_InitUI`, ui_core.c:17815-17820).
	static const TCHAR* kJobNames[9] = {
		TEXT("Fighter"), TEXT("Knight"), TEXT("WeaponMaster"),      // 18502..18504 — воин: 1-я/2-я/3-я профессия
		TEXT("Archer"), TEXT("Ranger"), TEXT("MasterArcher"),       // 18505..18507 — лучник
		TEXT("Wizard"), TEXT("Sorcerer"), TEXT("Arcmage") };        // 18508..18510 — маг
	// JUI Origin.X надписей (gamemain_jui.ini) — от них считается сдвиг слота.
	static const float kJobOriginX[9] = { 65.f, 77.f, 1.f, 73.f, 64.f, 24.f, 68.f, 55.f, 41.f };
	// JUI Size.X надписей — фолбэк ширины, если слот не прочитался.
	static const float kJobWidthJui[9] = { 86.f, 87.f, 150.f, 88.f, 99.f, 139.f, 95.f, 103.f, 122.f };
	static constexpr float kBecomeOriginX = 151.f;   // JUI Origin.X «Con_Banner_Become» (18501)
	static constexpr float kFrameS = 1.f / 60.f;     // [ПОРТ] шаг кадра въезда: эталон двигает ПО КАДРУ, у нас — 60 Гц

	// Узел-картинка надписи: имя `Con_Banner_<Имя>_<индекс JUI>_0` (UImage).
	static UWidget* FindLabelImage(UUserWidget* Host, const FString& Base)
	{
		if (!Host || !Host->WidgetTree) { return nullptr; }
		TArray<UWidget*> All;
		Host->WidgetTree->GetAllWidgets(All);
		const FString Prefix = Base + TEXT("_");
		for (UWidget* W : All)
		{
			if (!W || !W->IsA<UImage>()) { continue; }
			const FString N = W->GetName();
			if (!N.StartsWith(Prefix, ESearchCase::CaseSensitive) || !N.EndsWith(TEXT("_0"))) { continue; }
			// Между «Base_» и «_0» — только цифры (иначе «Con_Banner_Archer» поймал бы «Con_Banner_ArcherX_…»).
			const FString Mid = N.Mid(Prefix.Len(), N.Len() - Prefix.Len() - 2);
			if (!Mid.IsEmpty() && Mid.IsNumeric()) { return W; }
		}
		return nullptr;
	}
	// Видимость узла: видим (без хит-теста) или свёрнут.
	static void SetShown(UWidget* W, bool bShown)
	{
		if (W) { W->SetVisibility(bShown ? ESlateVisibility::HitTestInvisible : ESlateVisibility::Collapsed); }
	}
}

void UAsdaChangeJobBannerSubsystem::Initialize(FSubsystemCollectionBase& Collection)
{
	Super::Initialize(Collection);
	Collection.InitializeDependency(UAsdaNetSubsystem::StaticClass());
	if (UAsdaNetSubsystem* Net = GetGameInstance() ? GetGameInstance()->GetSubsystem<UAsdaNetSubsystem>() : nullptr)
	{
		QuestTurnInHandle = Net->OnQuestTurnInResult.AddUObject(this, &UAsdaChangeJobBannerSubsystem::OnQuestTurnInResult);
		JobChangedHandle  = Net->OnSelfJobChanged.AddUObject(this, &UAsdaChangeJobBannerSubsystem::OnSelfJobChanged);
	}
}

void UAsdaChangeJobBannerSubsystem::Deinitialize()
{
	if (UAsdaNetSubsystem* Net = GetGameInstance() ? GetGameInstance()->GetSubsystem<UAsdaNetSubsystem>() : nullptr)
	{
		Net->OnQuestTurnInResult.Remove(QuestTurnInHandle);
		Net->OnSelfJobChanged.Remove(JobChangedHandle);
	}
	StopAll(TEXT("деинициализация"));
	if (Banner) { Banner->RemoveFromParent(); Banner = nullptr; }
	Super::Deinitialize();
}

void UAsdaChangeJobBannerSubsystem::ResetSessionState(EAsdaSessionResetReason Reason)
{
	// Эталон баннер при смене карты не трогает (`CChangeJobBanner_Reset` — только этап инициализации UI, ui_core.c:54076);
	//   у нас полный сброс сессии (разрыв, выход в выбор) обязан снять его — оригинал при разрыве закрывает клиент.
	if (!AsdaIsFullSessionReset(Reason)) { return; }
	StopAll(TEXT("сброс сессии"));
	SelfJobGroup = 0;
}

// ════════════════════════ 5042: ВЕТКА СМЕНЫ ПРОФЕССИИ (network.c:21835-21889) ════════════════════════
void UAsdaChangeJobBannerSubsystem::OnQuestTurnInResult(int32 StepCode, int32 QuestNo, int32 /*Stage*/,
	int32 /*FinishType*/, int32 /*TalkId*/, const TArray<uint8>& Body)
{
	// `switch (resultCode)` case 0 (resultCode = body[0], `savedThis + 60364`) и `if (jobChangeFlag > 0)`,
	//   где `jobChangeFlag = 1` при `body[10] != 0` (`savedThis + 60374`, network.c:21796-21800).
	if (StepCode != 0 || Body.Num() < 15 || Body[10] == 0) { return; }
	// ★ 18.10.2026: при ключе `asda.Quest.ApplyJobChange 1` поля уже записала сеть (до этой рассылки) и прислала
	//   `OnSelfJobChanged` — ветка отработала там. Ниже — ПРЕЖНИЙ путь (ключ 0), без изменений поведения.
	if (UAsdaNetSubsystem::IsQuestJobChangeApplyOn()) { return; }
	UGameInstance* GI = GetGameInstance();
	UAsdaNetSubsystem* Net = GI ? GI->GetSubsystem<UAsdaNetSubsystem>() : nullptr;
	if (!Net) { return; }
	const int32 OldClass = Net->SelfClassId;
	// network.c:21845-21853: `gap_43B[10] = body[10]` (профессия, +0x445), `gap_438[0] = body[11]` (класс, +0x438),
	//   `gap_438[1] = body[12]` (подкласс), `*(WORD*)&gap_43B[8] = body[13]` (свободные очки умений).
	Net->SelfJob         = (int32)Body[10];
	Net->SelfClassId     = (int32)Body[11];
	Net->SelfSubJob      = (int32)Body[12];
	Net->SelfSkillPoints = AsdaBytes::RdI16(&Body[13]);
	// `skillUiUpdatePointText(g_pSkillWnd, body[13])` — окно навыков слушает этот делегат.
	Net->OnSelfSkillPoints.Broadcast(Net->SelfSkillPoints);
	UE_LOG(LogAsdaJobBanner, Log,
		TEXT("[смена профессии] 5042 квест %d (прежний путь, ключ 0): профессия %d, класс %d→%d, подкласс %d, очки умений %d"),
		QuestNo, (int32)Body[10], OldClass, (int32)Body[11], (int32)Body[12], Net->SelfSkillPoints);
	RunJobChangeUi((int32)Body[11], (int32)Body[12], /*bNotice=*/false);
}

void UAsdaChangeJobBannerSubsystem::OnSelfJobChanged(int32 OldClassId, int32 Job, int32 ClassId, int32 SubJob)
{
	UE_LOG(LogAsdaJobBanner, Log, TEXT("[смена профессии] событие сети: профессия %d, класс %d→%d, подкласс %d"),
		Job, OldClassId, ClassId, SubJob);
	// ★ 19.10.2026: смена профессии из ЗАВЕРШЕНИЯ 5052 (`charApplyFullStatusPacket`, entity_char.c:46333-46356) — у эталона там
	//   только поля + нотис вида 0 (20200/6177): ни звука 15, ни баннера 18500, ни таймеров. [ПОРТ] группу профессии
	//   (`gap_43B[32]`) пересчитываем и здесь — эталон в этой ветке её не трогает, и она осталась бы от прежнего класса.
	const UAsdaNetSubsystem* Net = GetGameInstance() ? GetGameInstance()->GetSubsystem<UAsdaNetSubsystem>() : nullptr;
	if (Net && Net->bSelfJobChangeFrom5052)
	{
		int32 Group = 0;
		JobBannerIdFromClass(ClassId, Group);
		SelfJobGroup = Group;
		PostJobChangeNotice(ClassId, SubJob);
		return;
	}
	RunJobChangeUi(ClassId, SubJob, /*bNotice=*/true);
}

void UAsdaChangeJobBannerSubsystem::RunJobChangeUi(int32 ClassId, int32 SubJob, bool bNotice)
{
	UGameInstance* GI = GetGameInstance();
	// `csoundPlayName2DStore(g_pSoundMgr, 15, 0)` (network.c:21856).
	if (UAsdaSoundSubsystem* Snd = GI ? GI->GetSubsystem<UAsdaSoundSubsystem>() : nullptr) { Snd->PlaySound2DById(15); }
	int32 Group = 0;
	const int32 BannerId = JobBannerIdFromClass(ClassId, Group);
	SelfJobGroup = Group;   // `pField142C5->gap_43B[32] = jobGroup` (network.c:21878)
	UE_LOG(LogAsdaJobBanner, Log, TEXT("[смена профессии] класс %d, подкласс %d: надпись %d, группа %d"), ClassId, SubJob, BannerId, Group);
	// `changeJobBannerStart(g_pChangeJobBanner, jobBannerId, 500, 2000, 1000)` (network.c:21882).
	StartBanner(BannerId, 500, 2000, 1000);
	// `chatBuildTransactionNotice(savedThis, тело…, 0, 0)` (network.c:21891-22018) — после баннера и таймеров 5/6.
	if (bNotice) { PostJobChangeNotice(ClassId, SubJob); }
}

// ★ 18.10.2026 — нотис смены профессии, порт `chatBuildTransactionNotice@0xD9FC70` вид 0 (chat.c:6094-6125).
//   Аргументы — тело 5042 по значению: `HIBYTE(a4)` = body[11] (класс), `directItemFlag` = `BYTE4(chatArgs[1])` = body[12] (подкласс).
//     подкласс ≠ 0 → имя = `ui_GetLocalizedNotice(подкласс)` = loc `12300 + подкласс` (ui_core.c:15942), ОКНО-сообщение есть;
//     подкласс = 0 → класс > 25 ? loc `класс + 6875` (окно есть) : loc `класс + 6132` (`useShortNameTable = 1`, окна нет);
//     текст = 20200 «Поздравляем! Теперь ты [ss1]!» (слот 0 = имя);
//     `if (!useShortNameTable) CItem_BuildTooltipText(g_pItemTooltipUI, 0, 24, текст, 1)` — окно ONEBUT_MSGBOX, ключ (0, 24);
//     `CChat_AddTokenizedLines(чат, текст, −21248, 10000, 0)`, затем 6177 «Доступны новые вещи и навыки.» тем же цветом,
//     затем `CControl_SetTooltipText(чат, 6177, −21248, 10000)` — плашка 221. Цвет −21248 = 0xFFFFAD00.
void UAsdaChangeJobBannerSubsystem::PostJobChangeNotice(int32 ClassId, int32 SubJob)
{
	UGameInstance* GI = GetGameInstance();
	if (!GI) { return; }
	static constexpr int32 kLocBecame       = 20200;   // «Поздравляем! Теперь ты [ss1]!»
	static constexpr int32 kLocNewThings    = 6177;    // «Доступны новые вещи и навыки.»
	static constexpr int32 kLocSubName      = 12300;   // `ui_GetLocalizedNotice(a1)` = loc a1 + 12300
	static constexpr int32 kLocClassShort   = 6132;    // имена классов 0..25
	static constexpr int32 kLocClassLong    = 6875;    // имена классов > 25
	static constexpr int32 kEtalonNoticeId  = 24;      // `CItem_BuildTooltipText(…, 0, 24, …)` — ключ очереди окон
	const FColor kColor((uint32)0xFFFFAD00);            // −21248
	bool bShort = false;
	int32 NameLoc = 0;
	if (SubJob != 0)          { NameLoc = kLocSubName + SubJob; }
	else if (ClassId > 0x19)  { NameLoc = ClassId + kLocClassLong; }
	else                      { NameLoc = ClassId + kLocClassShort; bShort = true; }
	const FString Name = AsdaNotice::LocRaw(GI, NameLoc);
	const FString Text = AsdaNotice::LocSlot(GI, kLocBecame, Name);
	if (!bShort) { AsdaNotice::MsgBox(GI, EAsdaMsgCategory::System, Text, AsdaLoc::kBtnOk, kEtalonNoticeId); }
	AsdaNotice::ChatColored(GI, EAsdaMsgCategory::System, Text, kColor, 10000.f);
	const FString More = AsdaNotice::LocRaw(GI, kLocNewThings);
	AsdaNotice::ChatColored(GI, EAsdaMsgCategory::System, More, kColor, 10000.f);
	AsdaNotice::Plank(GI, EAsdaMsgCategory::System, More, kColor, 10000.f, 5042);
	++JobNoticeCount;
	LastJobNoticeText = Text;
	bLastJobNoticeMsgBox = !bShort;
	UE_LOG(LogAsdaJobBanner, Log, TEXT("[смена профессии] нотис 20200 «%s» (имя loc %d%s) + 6177 в чат и плашку 221"),
		*Text, NameLoc, bShort ? TEXT("") : TEXT(", окно-сообщение (0, 24)"));
}

int32 UAsdaChangeJobBannerSubsystem::JobBannerIdFromClass(int32 ClassByte, int32& OutJobGroup)
{
	// network.c:21858-21876: `switch (--class)`: 0..2 → id = class−1, group = id+1; 11..13 → id = class−9, group = id−2;
	//   22..24 → id = class−17, group = id−5; прочее → id −1, group 0 (4-я профессия баннера не даёт — так в оригинале).
	OutJobGroup = 0;
	const int32 C = ClassByte - 1;
	if (C >= 0 && C <= 2)   { const int32 Id = ClassByte - 1;  OutJobGroup = Id + 1; return Id; }
	if (C >= 11 && C <= 13) { const int32 Id = ClassByte - 9;  OutJobGroup = Id - 2; return Id; }
	if (C >= 22 && C <= 24) { const int32 Id = ClassByte - 17; OutJobGroup = Id - 5; return Id; }
	return -1;
}

// ════════════════════════ ОКНО 18500 ════════════════════════
bool UAsdaChangeJobBannerSubsystem::EnsureWidget()
{
	using namespace AsdaJobBanner;
	UGameInstance* GI = GetGameInstance();
	UWorld* W = GI ? GI->GetWorld() : nullptr;
	// ★ Ревизия 2-В К-1: после `OpenLevel` виджет прежнего мира снят движком — создаём заново.
	if (Banner && Banner->IsInViewport() && Banner->GetWorld() == W) { return true; }
	if (Banner)
	{
		UE_LOG(LogAsdaJobBanner, Log, TEXT("[смена профессии] окно 18500 осталось от прежнего мира — создаю заново"));
		Banner->RemoveFromParent();
		Banner = nullptr;
		JobImg.Reset(); JobTxt.Reset(); JobWidth.Reset(); BecomeImg = nullptr; BecomeTxt = nullptr; BaseOffsets.Reset();
	}
	if (!W) { return false; }
	UClass* Cls = LoadClass<UUserWidget>(nullptr, kWbp);
	if (!Cls)
	{
		UE_LOG(LogAsdaJobBanner, Warning, TEXT("[смена профессии] окно 18500 не загрузилось: %s"), kWbp);
		return false;
	}
	Banner = CreateWidget<UUserWidget>(W, Cls);
	if (!Banner) { return false; }
	// ★ Дедуп №2, В2-B (U11, решение): окно 18500 — ЭКРАННЫЙ СЛОЙ мимо реестра окон (как и баннер уровня HUD, z 900), и это
	//   оставлено. У эталона 18500 — контрол менеджера окон (`CWindowMgr_FindControlByID`, ui_core.c:17772), но интерактива
	//   у него нет: перевод в реестр добавил бы ему хит-тест, ESC-закрытие и полосу z реестра — поведение, которого у баннера
	//   нет. Жизненный цикл свой: «остался от прежнего мира → создать заново» (выше), сброс сессии баннер не трогает.
	Banner->AddToViewport(kZOrder);
	// ★ 21.10.2026 (Alt+Z): прячется вместе с интерфейсом — у оригинала это окно JUI блока бита 128 (render_core.c:1482).
	UAsdaUiVisibilitySubsystem::TrackFor(this, Banner);
	Banner->SetVisibility(ESlateVisibility::Collapsed);   // корень 18500 в `.JUI` объявлен `Visible="0"`
	BaseOffsets.Reset();
	JobImg.SetNum(9); JobTxt.SetNum(9); JobWidth.SetNum(9);
	int32 Found = 0;
	for (int32 I = 0; I < 9; ++I)
	{
		const FString Base = FString::Printf(TEXT("Con_Banner_%s"), kJobNames[I]);
		JobImg[I] = FindLabelImage(Banner, Base);
		JobTxt[I] = AsdaUi::FindNodeExact(Banner, *Base);
		FVector2D P, S;
		JobWidth[I] = (JobImg[I] && AsdaUi::SlotRect(JobImg[I], P, S) && S.X > 0.f) ? (float)S.X : kJobWidthJui[I];
		if (JobImg[I]) { ++Found; }
	}
	BecomeImg = FindLabelImage(Banner, TEXT("Con_Banner_Become"));
	BecomeTxt = AsdaUi::FindNodeExact(Banner, TEXT("Con_Banner_Become"));
	{
		FVector2D P, S;
		if (BecomeImg && AsdaUi::SlotRect(BecomeImg, P, S) && S.X > 0.f) { BecomeWidth = (float)S.X; }
		UWidget* Root = AsdaJobBanner::FindLabelImage(Banner, TEXT("ChangeJob_Banner"));   // `ChangeJob_Banner_0_0` — сама полоса
		if (Root && AsdaUi::SlotRect(Root, P, S) && S.X > 0.f) { BannerWidth = (float)S.X; }
	}
	UE_LOG(LogAsdaJobBanner, Log,
		TEXT("[смена профессии] окно 18500 поднято: надписей %d/9, «Become» %s (ширина %.0f), полоса %.0f"),
		Found, BecomeImg ? TEXT("есть") : TEXT("НЕТ"), BecomeWidth, BannerWidth);
	return true;
}

// `changeJobBannerStart@0xBD4F40` (ui_core.c:17907-17982).
bool UAsdaChangeJobBannerSubsystem::StartBanner(int32 JobBannerId, int32 EndMs, int32 HoldMs, int32 FadeMs)
{
	using namespace AsdaJobBanner;
	UGameInstance* GI = GetGameInstance();
	// `if (a2 != -1)`. [ПОРТ] ещё и `> 8`: эталон индексировал бы массив надписей за его концом.
	if (JobBannerId < 0 || JobBannerId > 8)
	{
		UE_LOG(LogAsdaJobBanner, Log, TEXT("[смена профессии] баннера нет: номер надписи %d (эталон: −1 → без баннера)"), JobBannerId);
		return false;
	}
	// `if (startActive != 1 && showActive != 1)` — ни свой баннер, ни баннер 18550 сейчас не идут.
	if (bStartActive)
	{
		UE_LOG(LogAsdaJobBanner, Log, TEXT("[смена профессии] баннер %d пропущен: предыдущий ещё идёт (startActive)"), JobBannerId);
		return false;
	}
	if (const UAsdaHudSubsystem* Hud = GI ? GI->GetSubsystem<UAsdaHudSubsystem>() : nullptr)
	{
		if (Hud->IsConBanner2Active())
		{
			UE_LOG(LogAsdaJobBanner, Log, TEXT("[смена профессии] баннер %d пропущен: идёт баннер 18550 (showActive)"), JobBannerId);
			return false;
		}
	}
	if (!EnsureWidget()) { return false; }
	bStartActive = true;
	// `bannerTargetY = anchorY − (H >> 2)`; `moveXStep = (H >> 2) / 10 + 8` — H = высота окна игры в пикселях.
	//   ★ Дедуп №2, В2-B (U11, проверено): пиксели здесь ВЕРНЫ — эталон считает шаги и высоту в пикселях экрана, а перевод в
	//   единицы UI делает `ApplyOffset` (`PosYPx / ViewportDpiScale`). Менять на единицы нельзя: целочисленные `>> 2` и шаг
	//   `/10 + 8` эталона считаются от пикселей.
	int32 H = 720;
	if (GEngine && GEngine->GameViewport)
	{
		FVector2D Sz; GEngine->GameViewport->GetViewportSize(Sz);
		if (Sz.Y > 1.f) { H = (int32)Sz.Y; }
	}
	MoveStep = (H >> 2) / 10 + 8;
	PosYPx = -(float)(H >> 2);
	FadeDurS = FMath::Max(FadeMs, 0) / 1000.f;   // `timerIntervalB = a5`
	HoldLeftS = -1.f;                            // WM_TIMER 11 взводится по прибытии (`changeJobBannerAnimTick`)
	FadeElapsedS = -1.f;                         // [ПОРТ] прежнее угасание снимаем: оно гасило бы новую альфу 255
	bSliding = true;                             // `started = 1`, `animPhase = 1`
	SlideAccumS = 0.f;
	HoldLeftS = -1.f;
	// `moveTimerInterval = a4` — сколько держать после прибытия. Запоминаем в HoldLeftS при прибытии.
	const float HoldS = FMath::Max(HoldMs, 0) / 1000.f;
	PendingHoldS = HoldS;
	// `endTime = a3 + now` (a3 = 500) в фазе 1 никто не читает (его читает только `uiSlidePanel_TickMoveLeft` баннера 18550).
	(void)EndMs;
	if (JobBannerId % 3 == 0) { bCenterFlag = true; }   // `if (!(a2 % 3)) centerFlag = 1;` — сброса в 0 здесь нет
	// Альфа 255 у корня и всех контролов (`CControl_SetColor4(…, 0xFF, 0)` ×22).
	Banner->SetRenderOpacity(1.f);
	SetShown(Banner, true);
	// Центровка «Become» + надпись: `freeW = bannerRectX − (jobW + becomeRectX)`.
	const float JobW = JobWidth.IsValidIndex(JobBannerId) ? JobWidth[JobBannerId] : kJobWidthJui[JobBannerId];
	const float FreeW = BannerWidth - (JobW + BecomeWidth);
	const float HalfGap = (float)((int32)FreeW / 2);   // целочисленное деление эталона
	const float JobX = BecomeWidth + HalfGap;
	for (int32 I = 0; I < 9; ++I) { SetShown(JobImg[I], false); SetShown(JobTxt[I], false); }
	auto Place = [this](UWidget* W, float OriginX, float NewX)
	{
		FMargin B;
		if (W && AsdaUi::BaseOffsetsOf(BaseOffsets, W, B)) { AsdaUi::SetSlotPos(W, FVector2D(B.Left + (NewX - OriginX), B.Top)); }
	};
	Place(BecomeImg, kBecomeOriginX, HalfGap);
	Place(BecomeTxt, kBecomeOriginX, HalfGap);
	Place(JobImg[JobBannerId], kJobOriginX[JobBannerId], JobX);
	Place(JobTxt[JobBannerId], kJobOriginX[JobBannerId], JobX);
	SetShown(BecomeImg, true); SetShown(BecomeTxt, true);
	SetShown(JobImg[JobBannerId], true); SetShown(JobTxt[JobBannerId], true);
	ShownJob = JobBannerId;
	ApplyOffset();
	UE_LOG(LogAsdaJobBanner, Log,
		TEXT("[смена профессии] баннер 18500: надпись %d (%s), H=%d, старт %.0f px, шаг %d, Become.x=%.0f, Job.x=%.0f, нотис 7006 %s"),
		JobBannerId, kJobNames[JobBannerId], H, PosYPx, MoveStep, HalfGap, JobX, bCenterFlag ? TEXT("будет") : TEXT("нет"));
	return true;
}

void UAsdaChangeJobBannerSubsystem::Tick(float DeltaTime)
{
	ASDA_PERF_SCOPE("Tick.ChangeJobBanner");   // ★ R4-b (14.11.2026): область замера (AsdaPerf.h; в Shipping пусто)
	if (bManualClock) { return; }
	Advance(DeltaTime);
}

void UAsdaChangeJobBannerSubsystem::Advance(float Seconds)
{
	using namespace AsdaJobBanner;
	if (Seconds <= 0.f) { return; }
	// Въезд — `changeJobBannerAnimTick` каждый кадр, пока `started`.
	bool bArrivedNow = false;   // прибытие в этом шаге: таймер 11 считается от кадра прибытия, а не от начала шага
	if (bSliding)
	{
		SlideAccumS += Seconds;
		while (bSliding && SlideAccumS >= kFrameS)
		{
			SlideAccumS -= kFrameS;
			SlideFrame();
		}
		if (!bSliding)
		{
			bArrivedNow = true;
			HoldLeftS -= SlideAccumS;   // остаток шага после кадра прибытия уже идёт в счёт показа
			SlideAccumS = 0.f;
		}
	}
	// WM_TIMER 11 → `uiPanel_fadeInControls(18500)`: угасание + таймер 12 + `startActive = 0`.
	if (HoldLeftS >= 0.f || bArrivedNow)
	{
		if (!bArrivedNow) { HoldLeftS -= Seconds; }
		if (HoldLeftS <= 0.f)
		{
			const float Over = -HoldLeftS;
			HoldLeftS = -1.f;
			FadeElapsedS = Over;
			Timer12LeftS = FadeDurS - Over;
			bStartActive = false;
			UE_LOG(LogAsdaJobBanner, Log, TEXT("[смена профессии] таймер 11: угасание %.2f с, startActive = 0"), FadeDurS);
			Seconds = 0.f;   // остаток уже учтён в FadeElapsedS/Timer12LeftS
		}
	}
	if (FadeElapsedS >= 0.f)
	{
		FadeElapsedS += Seconds;
		const float A = (FadeDurS > 0.f) ? FMath::Clamp(1.f - FadeElapsedS / FadeDurS, 0.f, 1.f) : 0.f;
		if (Banner) { Banner->SetRenderOpacity(A); }
		if (FadeElapsedS >= FadeDurS)
		{
			FadeElapsedS = -1.f;
			// Эталон оставляет контролы видимыми с альфой 0; у нас окно сворачиваем — разницы на экране нет.
			SetShown(Banner, false);
		}
	}
	// WM_TIMER 12: `if (centerFlag) { 7006 окном-сообщением; centerFlag = 0 }`.
	if (Timer12LeftS >= 0.f)
	{
		Timer12LeftS -= Seconds;
		if (Timer12LeftS <= 0.f)
		{
			Timer12LeftS = -1.f;
			if (bCenterFlag)
			{
				bCenterFlag = false;
				++Notice7006Count;
				// `UI_FormatLocalizedNotice(7006)` + `CItem_BuildTooltipText(g_pItemTooltipUI, 0, -1, текст, 1)` — ONEBUT_MSGBOX 1500.
				AsdaNotice::MsgBoxLoc(GetGameInstance(), EAsdaMsgCategory::Progress, 7006);
				UE_LOG(LogAsdaJobBanner, Log, TEXT("[смена профессии] таймер 12: первая профессия — нотис 7006 (глобальный чат)"));
			}
		}
	}
}

// Один кадр `changeJobBannerAnimTick@0xBD5FB0` (ui_core.c:18324-18389), ветка `animPhase == 1`.
void UAsdaChangeJobBannerSubsystem::SlideFrame()
{
	if (MoveStep > 1) { --MoveStep; }
	const float NewY = PosYPx + (float)MoveStep;
	if (NewY >= 0.f)
	{
		PosYPx = 0.f;            // `SetField74_75(conBanner, bannerAnchorX, bannerAnchorY)`
		bSliding = false;        // `started = 0`
		HoldLeftS = PendingHoldS;   // `SetTimer(hWnd, 11, moveTimerInterval)` — показ 2000 мс
		UE_LOG(LogAsdaJobBanner, Log, TEXT("[смена профессии] баннер на месте, таймер 11 через %.2f с"), HoldLeftS);
	}
	else
	{
		PosYPx = NewY;
	}
	ApplyOffset();
}

void UAsdaChangeJobBannerSubsystem::ApplyOffset()
{
	if (!Banner) { return; }
	// Пиксели окна игры → единицы UI (DPI-кривая проекта): эталон двигает контрол в пикселях экрана.
	const UGameInstance* GI = GetGameInstance();
	const float Dpi = AsdaUi::ViewportDpiScale(GI ? GI->GetWorld() : nullptr);
	FWidgetTransform Tr;
	Tr.Translation = FVector2D(0.f, PosYPx / FMath::Max(Dpi, 0.01f));
	Banner->SetRenderTransform(Tr);
}

void UAsdaChangeJobBannerSubsystem::StopAll(const TCHAR* Why)
{
	const bool bWas = bStartActive || bSliding || HoldLeftS >= 0.f || FadeElapsedS >= 0.f || Timer12LeftS >= 0.f;
	bStartActive = false; bSliding = false; bCenterFlag = false;
	HoldLeftS = -1.f; FadeElapsedS = -1.f; Timer12LeftS = -1.f;
	SlideAccumS = 0.f; PosYPx = 0.f; ShownJob = -1;
	if (Banner) { AsdaJobBanner::SetShown(Banner, false); Banner->SetRenderOpacity(1.f); ApplyOffset(); }
	if (bWas) { UE_LOG(LogAsdaJobBanner, Log, TEXT("[смена профессии] баннер снят: %s"), Why); }
}

int32 UAsdaChangeJobBannerSubsystem::DebugPhase() const
{
	if (bSliding) { return 1; }
	if (HoldLeftS >= 0.f) { return 2; }
	if (FadeElapsedS >= 0.f) { return 3; }
	return 0;
}

float UAsdaChangeJobBannerSubsystem::DebugOpacity() const
{
	return Banner ? Banner->GetRenderOpacity() : 0.f;
}

int32 UAsdaChangeJobBannerSubsystem::DebugShownJob() const
{
	if (!Banner || Banner->GetVisibility() == ESlateVisibility::Collapsed) { return -1; }
	for (int32 I = 0; I < JobImg.Num(); ++I)
	{
		if (JobImg[I] && JobImg[I]->GetVisibility() != ESlateVisibility::Collapsed) { return I; }
	}
	return -1;
}

bool UAsdaChangeJobBannerSubsystem::DebugLabelX(float& OutBecomeX, float& OutJobX) const
{
	using namespace AsdaJobBanner;
	if (ShownJob < 0 || !JobImg.IsValidIndex(ShownJob) || !JobImg[ShownJob] || !BecomeImg) { return false; }
	// X в координатах JUI-окна = текущий Left слота − исходный Left + JUI Origin.X.
	auto JuiX = [this](UWidget* W, float OriginX, float& Out)
	{
		FVector2D P, S;
		const FMargin* B = BaseOffsets.Find(TWeakObjectPtr<UWidget>(W));
		if (!W || !B || !AsdaUi::SlotRect(W, P, S)) { return false; }
		Out = (float)P.X - B->Left + OriginX;
		return true;
	};
	return JuiX(BecomeImg, kBecomeOriginX, OutBecomeX) && JuiX(JobImg[ShownJob], kJobOriginX[ShownJob], OutJobX);
}
