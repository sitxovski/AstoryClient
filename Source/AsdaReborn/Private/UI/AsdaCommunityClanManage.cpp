// Copyright (c) 2026 @sitxovski (Discord)
// ★★★ 01.11.2026 (сессия clan-window) — ПАНЕЛЬ «ПРЕДВОДИТЕЛЬ» ВКЛАДКИ «КЛАН» (35000) И ПРИЁМ КЛАНОВЫХ ПАКЕТОВ. [ОРИГИНАЛ]
//   До этой правки на страницах панели стояли ТОЛЬКО ПОДПИСИ: «Сохранить» прав, списки званий, «Изгнать», выбор заместителя
//   и «Передать», ползунки и сетки герба, «Сохранить»/«Вернуть» герба не делали ничего, а ответы сервера 4115/6509/6517/6520/
//   6522/6525/6527/6528/6529 клиент не принимал. Владелец: «окно управления кланом и все вкладки/функции сломаны».
//   Эталон — `uiPanel_HandleCommand@0xBC16D0`, `uiWndDispatchCommand@0xBC2240`, `settings_DispatchFlagById@0xBC1FF0`,
//   модальный диспетчер (`AsdaStory_game.c:380585-380602`, «нет» — :380850), приёмники `network.c` case 0x196D…0x1981.
//   Сервер — `GameServer.c` (6508 :3329, 6521 :4058, 6524 :4383, 6526 :3583, 6530 :3658, 4114 :1057).
//   Разбор и таблица «событие → loc → канал» — в объявлениях методов (`AsdaCommunityClanSubsystem.h`).
#include "UI/AsdaCommunityClanSubsystem.h"
#include "UI/AsdaUiHelpers.h"   // ★ R1: прямое включение (раньше приходило транзитивно)

#include "AsdaBytes.h"
#include "AsdaDataTableCache.h"            // AsdaDt::RowByInt — таблица Clan_BorderIcon
#include "AsdaDevConsole.h"
#include "AsdaNetSubsystem.h"
#include "AsdaPacketWriter.h"
#include "Items/AsdaItemTableSubsystem.h"   // linkCode / подтип 62 — сертификат герба клана
#include "Login/AsdaGameText.h"            // FAsdaTextRow (id + tex)
#include "Login/AsdaLoginData.h"           // DecodeCp1251
#include "Text/AsdaTextSubsystem.h"        // слоты нотисов [ss1]…[ss3]
#include "UI/AsdaCommunitySubsystem.h"
#include "UI/AsdaEnterGuard.h"             // вопросы клана Enter не подтверждает (только щелчок)
#include "UI/AsdaHotbarSubsystem.h"        // значок 13 «предложение стать главой»
#include "UI/AsdaInventorySubsystem.h"     // поиск сертификата и списание (6525)
#include "UI/AsdaJuiComboBox.h"
#include "UI/AsdaNotice.h"
#include "UI/AsdaWindowSubsystem.h"
#include "World/AsdaClanFlag.h"            // текстуры символов/рамок герба
#include "World/AsdaWorldSubsystem.h"
#include "Blueprint/UserWidget.h"
#include "Blueprint/WidgetTree.h"
#include "Components/CheckBox.h"
#include "Components/Image.h"
#include "Components/TextBlock.h"
#include "Engine/Texture2D.h"
#include "HAL/IConsoleManager.h"
#include "Framework/Application/SlateApplication.h"   // курсор для колеса над сетками герба

DEFINE_LOG_CATEGORY_STATIC(LogAsdaClanManage, Log, All);

namespace
{
	// ── Ключи (0 = прежнее поведение порта) ──
	static ASDA_DEV_CVAR(int32, CVarManageActions,
		TEXT("asda.Clan.ManageActions"), 1,
		TEXT("[ОРИГИНАЛ] 1 (по умолчанию) = действия панели «Предводитель» вкладки «Клан» (права 6508, звания 6521, изгнание 4114, ")
		TEXT("передача главы 6526/6530, герб 6524) и приём 4115/4133/6509/6517/6520/6522/6523/6525/6527/6528/6529; 0 = прежнее (только подписи)."),
		ECVF_Default);
	static ASDA_DEV_CVAR(int32, CVarGradeRowMap,
		TEXT("asda.Clan.GradeRowMap"), 1,
		TEXT("[ИСПРАВЛЕНИЕ] 1 (по умолчанию) = строка страницы «Ранги» ведёт в ТУ запись, что в ней нарисована, и лишние строки чистятся; ")
		TEXT("0 = как оригинал (`база + строка + 1`: при главе не первым в списке щелчок по строке бьёт в чужую запись)."),
		ECVF_Default);
	static ASDA_DEV_CVAR(int32, CVarGradeRevertOnNo,
		TEXT("asda.Clan.GradeRevertOnNo"), 1,
		TEXT("[ИСПРАВЛЕНИЕ] 1 (по умолчанию) = «Нет» на вопрос о смене звания возвращает список звания строки к прежнему; ")
		TEXT("0 = как оригинал (список остаётся на выбранном пункте, хотя звание не менялось)."),
		ECVF_Default);
	static ASDA_DEV_CVAR(int32, CVarEmblemIndexClamp,
		TEXT("asda.Clan.EmblemIndexClamp"), 1,
		TEXT("[ИСПРАВЛЕНИЕ] 1 (по умолчанию) = индекс символа/рамки герба вне таблицы (≥ числа записей) сбрасывается в 0; ")
		TEXT("0 = как оригинал (`> числа`: индекс, равный числу записей, уходит на сервер и герб не рисуется)."),
		ECVF_Default);
	static ASDA_DEV_CVAR(int32, CVarEnterLeaderPage,
		TEXT("asda.Clan.EnterLeaderSelectsPage"), 1,
		TEXT("[ОРИГИНАЛ] 1 (по умолчанию) = вход в подрежим «Предводитель» открывает текущую страницу так же, как щелчок по ней ")
		TEXT("(`uiSwitchPanelMode` → `uiSelectPanelTab`: перерисовка страницы, на «Кандидатах» — нотис 17107); 0 = прежнее (только видимость)."),
		ECVF_Default);

	static ASDA_DEV_CVAR(int32, CVarNoticeGoneSelect,
		TEXT("asda.Clan.NoticeGoneSelect"), 1,
		TEXT("[ИСПРАВЛЕНИЕ] 1 (по умолчанию) = после удаления раскрытого объявления (6517) раскрытым становится соседнее (перед ним, ")
		TEXT("а если удалено первое — новое первое); 0 = как оригинал (удалено первое — раскрытым остаётся ключ удалённого: панель пуста)."),
		ECVF_Default);
	static ASDA_DEV_CVAR(int32, CVarEmblemCellSplit,
		TEXT("asda.Clan.EmblemCellSplit"), 1,
		TEXT("[ИСПРАВЛЕНИЕ] 1 (по умолчанию) = герб из блока раскладывается в ячейку 0…14 и ряд для всех индексов; 0 = как оригинал ")
		TEXT("(индекс 15 даёт «ячейку 15» вне сетки 5×3 — курсор и превью не ставятся)."),
		ECVF_Default);

	constexpr int32 kGradeRowsM = 7;      // строк страницы «Ранги» (35620 + 20i)
	constexpr int32 kMasterRowsM = 5;     // строк страницы «Передача полномочий» (35920 + 20i)
	constexpr int32 kGradeItems = 4;      // пунктов списка звания (loc 17041…17044 → звание 5 − пункт)
	constexpr int32 kEmCells = 15;        // ячеек сетки (5 × 3)
	constexpr int32 kEmCols = 5;          // столбцов сетки
	constexpr int32 kEmVisRows = 3;       // видимых рядов (`uiControlSetScrollRange(…, 3, рядов)`)
	constexpr int32 kItemAttrEmblem = 62; // `slotTableFindByItemAttr(&g_InventoryBag, 62, …)` — сертификат герба

	// Смещения подсказки 14900 от ЛОКАЛЬНОЙ позиции строки-источника (эталон: `CControl_SetField74_75(bubble, pos + d)`).
	const FVector2D kTipMemberOff(422.f, 150.f);   // `uiSlotTooltipFromList@0xBC67E0` (заметка члена), 120×80
	const FVector2D kTipGradeOff(110.f, 250.f);    // `uiSlotShowLocalizedNotice@0xBC6920` (17169), ширина 270
	// Локальные позиции строк в их JUI-родителе (структурный дамп окна 6400, `gamemain_jui.ini`).
	FVector2D MemberRowLocal(int32 R) { return FVector2D(9.f, 52.f + 57.f * R); }    // CMTY_CLAN_LIST1…6
	FVector2D GradeRowLocal(int32 R)  { return FVector2D(0.f, 19.f + 43.f * R); }    // CmtyClan_Sub3_Mem0…6
	const FVector2D kBubbleJuiOrig(-35.f, 91.f);   // 14900 `CmtyClan_IntroBubble` — позиция в окне 6400 (JUI)

	// ★ Координаты вырезки записей таблиц герба (`Clan_Sign.bin` / `Clan_BorderIcon.bin` клиента-эталона, запись 76 б:
	//   +4 tex, +68 srcX, +72 srcY). В DataTable порта колонок srcX/srcY нет, а у пяти символов (33…37) и одной рамки (29)
	//   они расходятся с формулой сетки — поэтому берутся из данных, не формулой. Выпечка флага (`UAsdaClanFlag`)
	//   использует формулу, как эталонный `CMiniMap_BakeIconToRenderTarget` — это расхождение самого оригинала.
	const int16 kSignSrc[38][2] = {
		{0,0},{64,0},{128,0},{192,0},{256,0},{320,0},{384,0},{448,0},
		{0,64},{64,64},{128,64},{192,64},{256,64},{320,64},{384,64},{448,64},
		{0,128},{64,128},{128,128},{192,128},{256,128},{320,128},{384,128},{448,128},
		{0,192},{64,192},{128,192},{192,192},{256,192},{320,192},{384,192},{448,192},
		{0,256},{128,256},{192,256},{256,256},{384,256},{448,256} };
	const int16 kBorderIconSrc[30][2] = {
		{0,0},{32,0},{64,0},{96,0},{128,0},{160,0},{192,0},{224,0},
		{0,20},{32,20},{64,20},{96,20},{128,20},{160,20},{192,20},{224,20},
		{0,40},{32,40},{64,40},{96,40},{128,40},{160,40},{192,40},{224,40},
		{0,60},{32,60},{64,60},{96,60},{128,60},{128,60} };
	FName CommWindowId() { return FName(TEXT("COMMUNITY_WIN")); }   // окно 6400 в `UAsdaWindowSubsystem` (`UAsdaCommunitySubsystem::kWindowId`)
	const TCHAR* kBorderIconTablePath = TEXT("/Game/AsdaAssets/Text/Clan/DT_Clan_BorderIcon.DT_Clan_BorderIcon");

	// Девять ползунков: фон (36122/36124/36126), символ (36151/36153/36155), рамка (36321/36322/36323).
	const TCHAR* kSliderIdc[9] = {
		TEXT("CmtyClan_BG_Cr"), TEXT("CmtyClan_BG_Cg"), TEXT("CmtyClan_BG_Cb"),
		TEXT("CmtyClan_Insg_Cr"), TEXT("CmtyClan_Insg_Cg"), TEXT("CmtyClan_Insg_Cb"),
		TEXT("CmtyClan_Border_Cr"), TEXT("CmtyClan_Border_Cg"), TEXT("CmtyClan_Border_Cb") };

	FLinearColor Rgb(const uint8 C[3]) { return FLinearColor(FColor(C[0], C[1], C[2], 255)); }
	uint32 Argb(const uint8 C[3]) { return 0xFF000000u | ((uint32)C[0] << 16) | ((uint32)C[1] << 8) | (uint32)C[2]; }

	// Длина имени записи (+5, char[20]) до нуля — то, что копирует `sprintf("%s")` эталона.
	int32 RawNameLen(const FAsdaClanMemberRecord& R) { int32 L = 0; while (L < 19 && R.B[5 + L] != 0) { ++L; } return L; }
	UWidget* NodeOf(UUserWidget* Host, const FString& Idc) { return Host ? AsdaCommunityUi::Find(Host, Idc) : nullptr; }
	// Кисть-вырезка: текстура `Tex`, прямоугольник (Src, Size) в пикселях текстуры.
	void SetCut(UImage* Img, UTexture2D* Tex, const FVector2D& Src, const FVector2D& Size)
	{
		if (!Img) { return; }
		if (!Tex) { Img->SetVisibility(ESlateVisibility::Hidden); return; }
		FSlateBrush B = Img->GetBrush();
		B.SetResourceObject(Tex);
		const float TW = FMath::Max((float)Tex->GetSizeX(), 1.f), TH = FMath::Max((float)Tex->GetSizeY(), 1.f);
		B.SetUVRegion(FBox2D(FVector2D(Src.X / TW, Src.Y / TH), FVector2D((Src.X + Size.X) / TW, (Src.Y + Size.Y) / TH)));
		B.DrawAs = ESlateBrushDrawType::Image;
		B.TintColor = FSlateColor(FLinearColor::White);
		Img->SetBrush(B);
		Img->SetVisibility(ESlateVisibility::HitTestInvisible);
	}
}

// Аксессоры ключей для основного файла вкладки (имя ключа в exe вне журнала не попадает — правило Shipping).
bool AsdaClanGradeRowMapOn() { return CVarGradeRowMap.GetValueOnGameThread() != 0; }
bool AsdaClanEnterLeaderSelectsPageOn() { return CVarEnterLeaderPage.GetValueOnGameThread() != 0; }

namespace
{
	// Поставить виджет `W` левым верхом в левый верх виджета `Target` (+ `Nudge` в пикселях разметки). Родители у строки и у
	//   рамки/курсора разные (импортёр расплющивает иерархию в разные группы), поэтому сравниваются позиции В КОРНЕ окна
	//   (`CanvasRootPlacement`: сумма слотов цепочки — от видимости не зависит, у свёрнутого виджета тоже есть). Слоты импортёра
	//   точечные, поэтому сдвиг в корне равен сдвигу собственного слота.
	bool MoveOnto(UWidget* W, const UWidget* Target, const FVector2D& Nudge)
	{
		FAnchors A1, A2; FVector2D WRoot, TRoot, P, Sz;
		if (!W || !Target || !AsdaUi::CanvasRootPlacement(W, A1, WRoot) || !AsdaUi::CanvasRootPlacement(Target, A2, TRoot) || !AsdaUi::SlotRect(W, P, Sz)) { return false; }
		AsdaUi::SetSlotPos(W, P + (TRoot - WRoot) + Nudge);
		return true;
	}
}

// ═══════════════════════════ ОБЩЕЕ ═══════════════════════════════════════════════════════════

FString UAsdaCommunityClanSubsystem::LocSlots(int32 Id, const FString& S1, const FString& S2, const FString& S3) const
{
	UGameInstance* GI = GetGameInstance();
	UAsdaTextSubsystem* T = GI ? GI->GetSubsystem<UAsdaTextSubsystem>() : nullptr;
	if (!T) { return Loc(Id); }
	T->NoticeClearSlots();
	T->NoticeSetString(0, S1);
	if (!S2.IsEmpty()) { T->NoticeSetString(1, S2); }
	if (!S3.IsEmpty()) { T->NoticeSetString(2, S3); }
	return T->FormatNotice(Id);
}

void UAsdaCommunityClanSubsystem::AskConfirm(const FString& Text, int32 Key, TFunction<void(bool)> OnResult)
{
	UGameInstance* GI = GetGameInstance();
	UAsdaWindowSubsystem* WM = GI ? GI->GetSubsystem<UAsdaWindowSubsystem>() : nullptr;
	if (!WM) { return; }
	// 1:1 `CItem_BuildTooltipText(g_pItemTooltipUI, 1, Key, текст, 1)` — окно-вопрос TWOBUT с ключом очереди Key.
	AsdaEnterGuard::NoEnter(WM)->ShowGameConfirm(Text, MoveTemp(OnResult), 0.f, 2288, 2289, Key, 1);
}

void UAsdaCommunityClanSubsystem::BindManageActions()
{
	if (CVarManageActions.GetValueOnGameThread() == 0 || !CommWnd) { return; }
	int32 N = 0;
	auto B = [this, &N](const FString& Idc, int32 Tag, TFunction<void(int32)> Fn)
	{
		if (AsdaCommunityUi::BindButton(this, CommWnd, Idc, Tag, MoveTemp(Fn), Relays)) { ++N; }
		else { UE_LOG(LogAsdaClanManage, Warning, TEXT("[клан-панель] кнопка «%s» не найдена"), *Idc); }
	};
	B(TEXT("CmtyClan_Sub2_ChangeBtn"), 35590, [this](int32) { ActionSaveRankPerms(); });
	B(TEXT("CmtyClan_Sub2_CancelBtn"), 35591, [this](int32) { RefreshRankPermPage(); });   // `cUI_RefreshTabControlsVisibility`
	for (int32 R = 0; R < kGradeRowsM; ++R)
	{
		B(FString::Printf(TEXT("CmtyClan_Sub3_Expulsion%d"), R), 35624 + 20 * R, [this, R](int32) { ActionExpel(R); });
	}
	B(TEXT("CmtyClan_Sub5_ConBtn"), 36021, [this](int32) { ActionMasterTransfer(); });
	B(TEXT("CmtyClan_Sub6_SaveBtn"), 36310, [this](int32) { ActionEmblemSave(); });
	B(TEXT("CmtyClan_Sub6_RecallBtn"), 36311, [this](int32)   // 0x8DD7: герб есть — из блока, нет — сброс
	{
		if (Info.bValid && Info.HasEmblem() == 1) { EmblemLoadFromBlock(); } else { EmblemReset(); }
	});

	// Списки званий 35630+20i: `UAsdaJuiComboBox` (игровой вид `Bk_ComboBox` + `DarkGrey_List`), пункты loc 17041…17044.
	GradeCombos.Reset();
	TArray<FString> Items;
	for (int32 I = 0; I < kGradeItems; ++I) { Items.Add(Loc(17041 + I)); }
	for (int32 R = 0; R < kGradeRowsM; ++R)
	{
		UAsdaJuiComboBox* C = NewObject<UAsdaJuiComboBox>(this);
		if (!C->Setup(CommWnd, FString::Printf(TEXT("CmtyClan_Sub3_Combo%d"), R), CommWindowId(), /*bOpenUp*/ false))
		{
			UE_LOG(LogAsdaClanManage, Warning, TEXT("[клан-панель] список звания строки %d не привязан"), R);
		}
		C->SetItems(Items);
		C->SetSelectedIndex(-1);
		C->OnPicked.BindWeakLambda(this, [this, R](int32 Idx) { OnGradePicked(R, Idx); });
		GradeCombos.Add(C);
	}
	BuildPlaneCache();
	EmblemBindWidgets();
	// Подсказка 14900: девять планов и углы — с раскладки импортёра (до первого показа).
	AsdaJuiNine::Collect(CommWnd, TEXT("CmtyClan_IntroBubble"), BubblePlanes);
	AsdaJuiNine::MeasureCorners(BubblePlanes, BubbleCornerTL, BubbleCornerBR);
	{
		FVector2D P, Sz;
		WindowJuiOrigin = (BubblePlanes.Num() == 9 && AsdaUi::SlotRect(BubblePlanes[0].Get(), P, Sz)) ? (P - kBubbleJuiOrig) : FVector2D::ZeroVector;
	}
	HideBubble();
	MasterSelRow = -1; MasterHoverRow = -1;
	if (UAsdaWindowSubsystem* WM = GetGameInstance() ? GetGameInstance()->GetSubsystem<UAsdaWindowSubsystem>() : nullptr)
	{
		WM->RegisterWheelHandler(CommWindowId(), FAsdaUiWheelHandler::CreateUObject(this, &UAsdaCommunityClanSubsystem::OnWheel));
	}
	UE_LOG(LogAsdaClanManage, Log, TEXT("[клан-панель] действия привязаны: кнопок %d, списков званий %d, ползунков %d"),
		N, GradeCombos.Num(), EmSliders.Num());
}

void UAsdaCommunityClanSubsystem::BuildPlaneCache()
{
	PlaneCache.Reset();
	if (!CommWnd || !CommWnd->WidgetTree) { return; }
	CommWnd->WidgetTree->ForEachWidget([this](UWidget* W)
	{
		FString N; int32 J = 0, P = 0;
		if (W && AsdaUi::ParsePlaneName(W->GetName(), N, J, P)) { PlaneCache.Add(FString::Printf(TEXT("%s#%d"), *N, P), W); }
	});
}

UWidget* UAsdaCommunityClanSubsystem::PlaneAny(const FString& Idc, int32 Plane) const
{
	const TWeakObjectPtr<UWidget>* W = PlaneCache.Find(FString::Printf(TEXT("%s#%d"), *Idc, Plane));
	return W ? W->Get() : nullptr;
}

UImage* UAsdaCommunityClanSubsystem::PlaneImg(const FString& Idc, int32 Plane) const { return Cast<UImage>(PlaneAny(Idc, Plane)); }

void UAsdaCommunityClanSubsystem::EnsureNoticeIconHook()
{
	if (HNoticeIcon.IsValid()) { return; }
	if (UAsdaHotbarSubsystem* Hb = GetGameInstance() ? GetGameInstance()->GetSubsystem<UAsdaHotbarSubsystem>() : nullptr)
	{
		HNoticeIcon = Hb->OnNoticeIconClicked.AddUObject(this, &UAsdaCommunityClanSubsystem::OnNoticeIcon);
	}
}

void UAsdaCommunityClanSubsystem::ReapplyPageRuntimeState(int32 PageIdx)
{
	if (!CommWnd) { return; }
	if (PageIdx == 2)
	{
		for (UAsdaJuiComboBox* C : GradeCombos) { if (C && C->IsReady()) { C->SetOpen(C->IsOpen()); } }   // список виден только раскрытым
	}
	else if (PageIdx == 4)
	{
		for (int32 R = 0; R < kMasterRowsM; ++R)
		{
			if (UImage* Ov = PlaneImg(FString::Printf(TEXT("CmtyClan_Sub5_COver%d"), R), 0))
			{
				Ov->SetVisibility(R == MasterHoverRow ? ESlateVisibility::HitTestInvisible : ESlateVisibility::Collapsed);
			}
		}
		for (int32 I = 0; I < 8; ++I)
		{
			if (UImage* S = PlaneImg(TEXT("CmtyClan_Sub5_Sel"), I))
			{
				S->SetVisibility(MasterSelRow >= 0 ? ESlateVisibility::HitTestInvisible : ESlateVisibility::Collapsed);
			}
		}
	}
	else if (PageIdx == 5)
	{
		EmblemDrawPreview();   // курсоры 36200/36398 стоят на выбранных ячейках (виден, как у эталона после отрисовки)
	}
}

// ═══════════════════════════ ПОКАДРОВЫЙ ОПРОС ════════════════════════════════════════════════

bool UAsdaCommunityClanSubsystem::TickManage(float Dt)
{
	// Отложенные запросы — вне сетевого колбэка (закон `no-reentrant-net-callbacks`).
	EnsureNoticeIconHook();
	if (bWant4124 || bWant4138 || bWant4122)
	{
		UAsdaNetSubsystem* N = GetGameInstance() ? GetGameInstance()->GetSubsystem<UAsdaNetSubsystem>() : nullptr;
		if (N)
		{
			if (bWant4124 && SelfClanId() >= 0) { N->SendClanMemberListReq4124(SelfClanId()); }
			if (bWant4138) { N->SendClanRosterRefresh4138(); }
			if (bWant4122 && SelfClanId() >= 0) { N->SendClanBaseInfoReq4122(SelfClanId()); }
		}
		bWant4124 = bWant4138 = bWant4122 = false;
	}
	if (EmCertRec.Num() == FAsdaItemInstance::kRecordSize + 1)   // 6525: запись слота ждёт применения (последний байт — признак)
	{
		TArray<uint8> Rec(EmCertRec.GetData(), FAsdaItemInstance::kRecordSize);
		EmCertRec.Reset();
		if (UAsdaInventorySubsystem* Inv = GetGameInstance() ? GetGameInstance()->GetSubsystem<UAsdaInventorySubsystem>() : nullptr)
		{
			// `netOnRefineSlotUpdate`: количество (+9) ≠ 0 — запись ставится как есть, иначе слот очищается.
			Inv->ApplyExternalRecord(Rec, AsdaBytes::RdI32(Rec.GetData() + 9) != 0
				? UAsdaInventorySubsystem::EExternalRecordMode::AsIs : UAsdaInventorySubsystem::EExternalRecordMode::CountOrClear);
		}
	}
	if (!IsBoundTo(CommWnd) || !bTabVisible || !AsdaUi::IsShown(CommWnd) || CVarManageActions.GetValueOnGameThread() == 0)
	{
		ManageMouse.Reset();
		for (UAsdaJuiComboBox* C : GradeCombos) { if (C && C->IsOpen()) { C->SetOpen(false); } }
		if (BubbleKey != INDEX_NONE) { HideBubble(); }
		return true;
	}
	ManageMouse.Update();
	if (AsdaUi::IsModalDispatchBlocked(GetGameInstance())) { ManageMouse.Reset(); return true; }
	const FAsdaMouseFrame& M = ManageMouse;

	if (Mode == 2 && ManageSubPage == 2)
	{
		// Раскрытый список модальнее соседей — пока он открыт, остальные мышь не разбирают.
		UAsdaJuiComboBox* Open = nullptr;
		for (UAsdaJuiComboBox* C : GradeCombos) { if (C && C->IsOpen()) { Open = C; } }
		if (Open) { Open->Tick(M); }
		else { for (UAsdaJuiComboBox* C : GradeCombos) { if (C && C->IsReady()) { C->Tick(M); } } }
	}
	else
	{
		for (UAsdaJuiComboBox* C : GradeCombos) { if (C && C->IsOpen()) { C->SetOpen(false); } }
	}

	if (Mode == 2 && ManageSubPage == 4)
	{
		// Строки 35920+20i кнопок не имеют: наведение (событие 8/0xA → подсветка COver) и щелчок (0xB → выбор).
		int32 Hover = -1;
		for (int32 R = 0; R < kMasterRowsM; ++R)
		{
			UImage* Row = PlaneImg(FString::Printf(TEXT("CmtyClan_Sub5_Mem%d"), R), 0);
			if (Row && AsdaUi::IsCursorOverWidget(Row)) { Hover = R; break; }
		}
		if (Hover != MasterHoverRow)
		{
			for (int32 R = 0; R < kMasterRowsM; ++R)   // `uiTabSelectPageByOffset`: все пять гасятся, наведённая — видна
			{
				if (UImage* Ov = PlaneImg(FString::Printf(TEXT("CmtyClan_Sub5_COver%d"), R), 0))
				{
					Ov->SetVisibility(R == Hover ? ESlateVisibility::HitTestInvisible : ESlateVisibility::Collapsed);
				}
			}
			MasterHoverRow = Hover;
		}
		if (M.bLmbDown && Hover >= 0) { MasterRowClick(Hover); }
	}

	if (Mode == 2 && ManageSubPage == 5)
	{
		bool bChanged = false;
		for (int32 S = 0; S < EmSliders.Num(); ++S)
		{
			const int32 Was = EmSliders[S].Value;
			if (EmSliders[S].Tick(M) && EmSliders[S].Value != Was)
			{
				uint8* Dst = (S < 3) ? EmBg : ((S < 6) ? EmSign : EmBorder);   // `settings_DispatchFlagById`
				Dst[S % 3] = (uint8)EmSliders[S].Value;
				bChanged = true;
			}
		}
		if (bChanged) { EmblemApplyColors(); }
		if (M.bLmbDown)
		{
			for (int32 Cell = 0; Cell < kEmCells; ++Cell)
			{
				if (UImage* I = PlaneImg(FString::Printf(TEXT("CmtyClan_InsgIcon%02d"), Cell), 0))
				{
					if (AsdaUi::IsShown(I) && AsdaUi::IsCursorOverWidget(I)) { EmblemCellClick(true, Cell); break; }
				}
				if (UImage* I = PlaneImg(FString::Printf(TEXT("CmtyClan_BorderIcon%02d"), Cell), 0))
				{
					if (AsdaUi::IsShown(I) && AsdaUi::IsCursorOverWidget(I)) { EmblemCellClick(false, Cell); break; }
				}
			}
		}
	}
	TickHover(M);
	return true;
}

// ═══════════════════════════ ПОДСКАЗКА 14900 ═════════════════════════════════════════════════

void UAsdaCommunityClanSubsystem::ShowBubble(int32 Key, const FString& Text, float W, float H, const FVector2D& LocalPos)
{
	if (BubblePlanes.Num() != 9 || !CommWnd) { return; }
	const FVector2D Pos = WindowJuiOrigin + LocalPos;
	AsdaJuiNine::Layout(BubblePlanes, BubbleCornerTL, BubbleCornerBR, Pos, FVector2D(W, H));
	// Группа 14900 в разметке скрыта (Visible = 0) — без неё не видны ни планы, ни текст 14901 (ребёнок).
	if (UWidget* G = CommWnd->GetWidgetFromName(TEXT("CmtyClan_IntroBubble_grp"))) { G->SetVisibility(ESlateVisibility::HitTestInvisible); }
	for (const TWeakObjectPtr<UImage>& P : BubblePlanes) { if (UImage* I = P.Get()) { I->SetVisibility(ESlateVisibility::HitTestInvisible); } }
	if (UTextBlock* T = AsdaCommunityUi::FindLabel(CommWnd, TEXT("CmtyClan_IntroTip")))
	{
		T->SetText(FText::FromString(Text));
		T->SetAutoWrapText(false);
		T->SetWrapTextAt(W - 12.f);
		AsdaUi::SetSlotRect(T, Pos + FVector2D(6.f, 6.f), FVector2D(W - 12.f, H - 12.f));
		T->SetVisibility(ESlateVisibility::HitTestInvisible);
	}
	BubbleKey = Key;
}

void UAsdaCommunityClanSubsystem::HideBubble()
{
	for (const TWeakObjectPtr<UImage>& P : BubblePlanes) { if (UImage* I = P.Get()) { I->SetVisibility(ESlateVisibility::Collapsed); } }
	if (CommWnd)
	{
		if (UTextBlock* T = AsdaCommunityUi::FindLabel(CommWnd, TEXT("CmtyClan_IntroTip"))) { T->SetVisibility(ESlateVisibility::Collapsed); }
		if (UWidget* G = CommWnd->GetWidgetFromName(TEXT("CmtyClan_IntroBubble_grp"))) { G->SetVisibility(ESlateVisibility::Collapsed); }
	}
	BubbleKey = INDEX_NONE;
}

void UAsdaCommunityClanSubsystem::TickHover(const FAsdaMouseFrame& M)
{
	int32 Key = INDEX_NONE;
	FString Text; float W = 0.f, H = 0.f; FVector2D Local = FVector2D::ZeroVector;
	if (Mode == 0)
	{
		// Строки состава 14120+20i: событие 8 → `uiSlotTooltipFromList` — заметка члена (`запись+47`), панель 120×80.
		for (int32 R = 0; R < 6 && Key == INDEX_NONE; ++R)
		{
			UWidget* Row = NodeOf(CommWnd, FString::Printf(TEXT("CMTY_CLAN_LIST%d"), R + 1));
			if (!Row || !AsdaUi::IsShown(Row) || !AsdaUi::IsCursorOverWidget(Row)) { continue; }
			const int32 Idx = R + 6 * (Page - 1);
			if (!Members.IsValidIndex(Idx)) { break; }
			const FString Memo = FAsdaLoginData::DecodeCp1251(Members[Idx].B + 47, 64);
			if (Memo.IsEmpty()) { break; }
			Key = 1000 + Idx; Text = Memo; W = 120.f; H = 80.f; Local = MemberRowLocal(R) + kTipMemberOff;
		}
	}
	else if (Mode == 2 && ManageSubPage == 2)
	{
		// Строки «Рангов» 35620+20i: событие 8 → `uiSlotShowLocalizedNotice` — loc 17169 (имя, время последнего выхода).
		for (int32 R = 0; R < kGradeRowsM && Key == INDEX_NONE; ++R)
		{
			UImage* Row = PlaneImg(FString::Printf(TEXT("CmtyClan_Sub3_Mem%d"), R), 0);
			if (!Row || !AsdaUi::IsShown(Row) || !AsdaUi::IsCursorOverWidget(Row)) { continue; }
			const int32 Idx = GradeRowToMember(R);
			if (!Members.IsValidIndex(Idx)) { break; }
			const FAsdaClanMemberRecord& Rec = Members[Idx];
			Key = 2000 + Idx;
			Text = LocSlots(17169, Rec.Name(), FAsdaLoginData::DecodeCp1251(Rec.B + 115, 16));
			W = 270.f; H = 12.f + 14.f * FMath::Max(1, FMath::DivideAndRoundUp(Text.Len(), 40)); Local = GradeRowLocal(R) + kTipGradeOff;
		}
	}
	if (Key == INDEX_NONE) { if (BubbleKey != INDEX_NONE) { HideBubble(); } return; }
	if (Key != BubbleKey) { ShowBubble(Key, Text, W, H, Local); }
}

// ═══════════════════════════ СТРАНИЦА 1 «ПРАВА» ══════════════════════════════════════════════

// 35590 — `antiCheatValidateEquipSums@0xBC4330` (★ МИСНЕЙМ, это «сохранить права званий»):
//   for (i < 2: звания 2 и 3) for (j < 3) if (галочка[i][j]) mask[i] += 2^j;  mask[2] = mask[3] = 0 (звания 4/5 обнуляются!)
//   if (число званий, чья маска совпала с блоком @57+2i, < 2) → 6508 (`netSendAntiCheatMd5Token@0xBCCBE0`):
//   тело 10 б: @0 i16 clanIndex (блок @0) · @2/@4/@6/@8 u16 маски званий 2/3/4/5. Ничего не изменилось — молча.
void UAsdaCommunityClanSubsystem::ActionSaveRankPerms()
{
	if (!CommWnd) { return; }
	uint16 Mask[4] = { 0, 0, 0, 0 };
	int32 Same = 0;
	for (int32 R = 0; R < 2; ++R)
	{
		for (int32 P = 0; P < 3; ++P)
		{
			const UCheckBox* C = Cast<UCheckBox>(AsdaCommunityUi::Find(CommWnd, FString::Printf(TEXT("CmtyClan_PosFunc_Btn%d_%d"), R + 1, P + 1)));
			if (C && C->IsChecked()) { Mask[R] = (uint16)(Mask[R] + (1 << P)); }
		}
		if (Info.bValid && Mask[R] == AsdaBytes::RdU16(Info.B + 57 + 2 * R)) { ++Same; }
	}
	if (Same >= 2)
	{
		UE_LOG(LogAsdaClanManage, Log, TEXT("[клан-панель] права: маски %u/%u не изменились — 6508 не нужен (1:1)"), Mask[0], Mask[1]);
		return;
	}
	UAsdaNetSubsystem* N = GetGameInstance() ? GetGameInstance()->GetSubsystem<UAsdaNetSubsystem>() : nullptr;
	if (!N) { return; }
	FAsdaPacketWriter W(10);
	W.I16(0, Info.bValid ? Info.ClanIndex() : SelfClanId());
	for (int32 R = 0; R < 4; ++R) { W.I16(2 + 2 * R, (int16)Mask[R]); }
	const bool bOk = N->SendMenuPacket(6508, W.Data(), W.Num(), TEXT("права званий клана 6508"));
	UE_LOG(LogAsdaClanManage, Log, TEXT("[клан-панель] →6508 права званий: 2=%u 3=%u (4/5 = 0, 1:1) -> %s"), Mask[0], Mask[1], bOk ? TEXT("OK") : TEXT("ОТКАЗ"));
}

// ═══════════════════════════ СТРАНИЦА 2 «РАНГИ» ══════════════════════════════════════════════

int32 UAsdaCommunityClanSubsystem::GradeRowToMember(int32 Row) const
{
	if (Row < 0 || Row >= kGradeRowsM) { return -1; }
	if (CVarGradeRowMap.GetValueOnGameThread() != 0) { return GradeRowMember[Row]; }
	return 7 * (Page - 1) + Row + 1;   // 1:1 эталон `rowBase + rowIndex + 1`
}

// Событие 70145 по 35630+20i → `uiBuildItemUpgradeNotice@0xBC4080`: Item — пункт списка (новое звание = 5 − Item).
//   `5 − Item <= текущее звание` (повышение или то же) → вопрос 17167 «Дать игроку [имя] звание '[новое]'?»,
//   иначе (понижение) → 17168 «'[имя]' имеет ранг [текущее]. Понизить его до ранга '[новое]'?». «Да» (0x430F/0x4310) → 6521.
void UAsdaCommunityClanSubsystem::OnGradePicked(int32 Row, int32 Item)
{
	const int32 Idx = GradeRowToMember(Row);
	if (!Members.IsValidIndex(Idx) || Item < 0 || Item >= kGradeItems) { return; }
	FAsdaClanMemberRecord Rec = Members[Idx];
	const int32 Cur = Rec.Grade();
	const int32 NewGrade = 5 - Item;
	const FString CurName = Loc(17046 - Cur);
	const FString NewName = Loc(17041 + Item);
	const bool bUp = (NewGrade <= Cur);
	const int32 Key = bUp ? 17167 : 17168;
	const FString Text = bUp ? LocSlots(17167, Rec.Name(), NewName) : LocSlots(17168, Rec.Name(), CurName, NewName);
	Rec.B[33] = (uint8)NewGrade;   // 1:1: `itemEntry[33] = 5 − upgradeLevel` и копия в отложенную запись
	PendingRec = Rec; bPendingRec = true;
	AskConfirm(Text, Key, [this](bool bYes)
	{
		if (bYes) { SendGrade6521(); return; }
		if (CVarGradeRevertOnNo.GetValueOnGameThread() != 0) { RefreshGradePage(); }
	});
}

// 6521 (`netSendAntiCheatReport@0xBCCE40`), тело 31 б: @0 i32 свой charId (`g_LocalPlayer+1052`; сервер не читает) ·
//   @4 i16 новое звание · @6 i32 userIndex цели · @10 u8 charIndex · @11 char[20] имя. Ответ — 6522 (код), всем — 6523.
void UAsdaCommunityClanSubsystem::SendGrade6521()
{
	UAsdaNetSubsystem* N = GetGameInstance() ? GetGameInstance()->GetSubsystem<UAsdaNetSubsystem>() : nullptr;
	if (!N || !bPendingRec) { return; }
	FAsdaPacketWriter W(31);
	W.I32(0, N->SelectedCharId);
	W.I16(4, (int16)PendingRec.Grade());
	W.I32(6, PendingRec.UserIndex());
	W.U8(10, PendingRec.CharIndex());
	W.Bytes(11, PendingRec.B + 5, 20);   // 1:1 `memcpy(&v8[2] + 3, запись + 5, 0x14)` — сырые байты имени записи
	const bool bOk = N->SendMenuPacket(6521, W.Data(), W.Num(), TEXT("звание члена клана 6521"));
	UE_LOG(LogAsdaClanManage, Log, TEXT("[клан-панель] →6521 звание: «%s» → %d -> %s"), *PendingRec.Name(), (int32)PendingRec.Grade(), bOk ? TEXT("OK") : TEXT("ОТКАЗ"));
}

// «Изгнать» 35624+20i → `uiCacheItemUpgradeRecord@0xBC3F50`: вопрос 17166 «[звание] '[имя]' - выгнать из клана?» (ключ 17166)
//   и копия записи в отложенную; «Да» (0x430E) → `netSendItemInscriptionPacket` = 4114.
void UAsdaCommunityClanSubsystem::ActionExpel(int32 Row)
{
	const int32 Idx = GradeRowToMember(Row);
	if (!Members.IsValidIndex(Idx)) { return; }
	PendingRec = Members[Idx]; bPendingRec = true;
	AskConfirm(LocSlots(17166, Loc(17046 - PendingRec.Grade()), PendingRec.Name()), 17166, [this](bool bYes)
	{
		if (bYes) { SendExpel4114(); }
	});
}

// 4114, тело 25 б: @0 i32 userIndex цели · @4 u8 charIndex · @5 char[20] имя. Гейт эталона — «я в клане» (`gap_96A[3] == 1`),
//   иначе нотис 12428. Сервер проверяет право `& 0x10`; ответ 4115 (0 — отказ 12422, 1 — исключён 12423).
void UAsdaCommunityClanSubsystem::SendExpel4114()
{
	// 1:1 гейт эталона `CClass_GetField142C(&g_InventoryBag)->gap_96A[3] == 1` — это своё ЗВАНИЕ (char+0x97B, тот же байт, что 4133
	//   обнуляет при исключении): только глава, иначе нотис 12428 «Исключать игроков из клана может только его глава».
	if (SelfGrade() != 1)
	{
		NoticeBox(Loc(12428));
		return;
	}
	UAsdaNetSubsystem* N = GetGameInstance() ? GetGameInstance()->GetSubsystem<UAsdaNetSubsystem>() : nullptr;
	if (!N || !bPendingRec) { return; }
	FAsdaPacketWriter W(25);
	W.I32(0, PendingRec.UserIndex());
	W.U8(4, PendingRec.CharIndex());
	W.Bytes(5, PendingRec.B + 5, RawNameLen(PendingRec));   // 1:1 `sprintf(…, "%s", запись + 5)` — сырые байты до нуля
	const bool bOk = N->SendMenuPacket(4114, W.Data(), W.Num(), TEXT("изгнание из клана 4114"));
	UE_LOG(LogAsdaClanManage, Log, TEXT("[клан-панель] →4114 изгнать «%s» -> %s"), *PendingRec.Name(), bOk ? TEXT("OK") : TEXT("ОТКАЗ"));
}

// ═══════════════════════════ СТРАНИЦА 4 «ПЕРЕДАЧА ПОЛНОМОЧИЙ» ════════════════════════════════

// `uiHighlightRowByCoord@0xBC28B0`: щелчок по выбранной строке снимает выбор (`sub_931E2A`), по другой — рамка 36020 на неё.
void UAsdaCommunityClanSubsystem::MasterRowClick(int32 Row)
{
	if (!CommWnd) { return; }
	const bool bDeselect = (MasterSelRow == Row);
	MasterSelRow = bDeselect ? -1 : Row;
	UImage* RowImg = PlaneImg(FString::Printf(TEXT("CmtyClan_Sub5_Mem%d"), Row), 0);
	UImage* First = PlaneImg(TEXT("CmtyClan_Sub5_Sel"), 0);
	// Рамка 307×21 — восемь планов; встаёт на строку 305×19 со сдвигом (−1, −1), как в разметке (36020 у строки 0).
	FVector2D DeltaLocal = FVector2D::ZeroVector;
	FAnchors A1, A2; FVector2D RRoot, FRoot;
	if (!bDeselect && RowImg && First && AsdaUi::CanvasRootPlacement(RowImg, A1, RRoot) && AsdaUi::CanvasRootPlacement(First, A2, FRoot))
	{
		DeltaLocal = (RRoot - FRoot) + FVector2D(-1.f, -1.f);
	}
	for (int32 I = 0; I < 8; ++I)
	{
		if (UImage* S = PlaneImg(TEXT("CmtyClan_Sub5_Sel"), I))
		{
			FVector2D P, Sz;
			if (!bDeselect && AsdaUi::SlotRect(S, P, Sz)) { AsdaUi::SetSlotPos(S, P + DeltaLocal); }
			S->SetVisibility(bDeselect ? ESlateVisibility::Collapsed : ESlateVisibility::HitTestInvisible);
		}
	}
}

// 36021 → `mailSelectEntryShowTooltip@0xBC45D0`: обход списка, v-й заместитель (звание 2) с v == выбранной строке;
//   [113] «в игре» ≠ 0 → вопрос 17204 (ключ 17204), иначе нотис 17209. Выбора нет — молча (1:1).
void UAsdaCommunityClanSubsystem::ActionMasterTransfer()
{
	if (MasterSelRow < 0) { return; }
	int32 V = -1;
	for (const FAsdaClanMemberRecord& R : Members)
	{
		if (R.Grade() == 2) { ++V; }
		if (V == MasterSelRow)
		{
			PendingRec = R; bPendingRec = true;
			if (R.InGame())
			{
				AskConfirm(LocSlots(17204, R.Name()), 17204, [this](bool bYes)
				{
					if (!bYes) { return; }
					// 0x4334 → `itemBuildLocalizedTooltip@0xBC4790`: второй вопрос 17205 (ключ 17205); «Да» (0x4335) → 6526.
					AskConfirm(LocSlots(17205, PendingRec.Name()), 17205, [this](bool bYes2) { if (bYes2) { SendMasterTrans6526(); } });
				});
			}
			else
			{
				NoticeBox(LocSlots(17209, R.Name()));
			}
			return;
		}
		if (V >= 5) { return; }
	}
}

// 6526 (`netSendNameRequest@0xBCD300`), тело 31 б: @0 i32 свой charId · @4 i16 clanIndex (блок @0) · @6 i32 userIndex нового ·
//   @10 u8 charIndex · @11 char[20] имя. Затем нотис 17208. Адресату приходит 6529, ответ — 6530; итог — 6528.
void UAsdaCommunityClanSubsystem::SendMasterTrans6526()
{
	UAsdaNetSubsystem* N = GetGameInstance() ? GetGameInstance()->GetSubsystem<UAsdaNetSubsystem>() : nullptr;
	if (!N || !bPendingRec) { return; }
	FAsdaPacketWriter W(31);
	W.I32(0, N->SelectedCharId);
	W.I16(4, Info.bValid ? Info.ClanIndex() : SelfClanId());
	W.I32(6, PendingRec.UserIndex());
	W.U8(10, PendingRec.CharIndex());
	W.Bytes(11, PendingRec.B + 5, RawNameLen(PendingRec));   // 1:1 `sprintf(…, "%s", запись + 5)`
	const bool bOk = N->SendMenuPacket(6526, W.Data(), W.Num(), TEXT("передача главы клана 6526"));
	UE_LOG(LogAsdaClanManage, Log, TEXT("[клан-панель] →6526 передать главу «%s» -> %s"), *PendingRec.Name(), bOk ? TEXT("OK") : TEXT("ОТКАЗ"));
	NoticeBox(LocSlots(17208, PendingRec.Name()));
}

// Значок 13 (`uiNoticeButtonHandler` case 13): вопрос 17207 с [ss1] = ИМЯ КЛАНА из блока 191 б (`byte_2BE5106` = блок+2;
//   ★ так в оригинале — текст говорит «Игрок [ss1]», а подставляется клан: имени предлагающего в 6529 нет вовсе).
//   «Да» (0x4337) → 6530 [0] = 1, «Нет» (ветка «нет» того же кода, :380850) → 6530 [0] = 2.
void UAsdaCommunityClanSubsystem::OnNoticeIcon(int32 Type)
{
	if (Type != 13 || CVarManageActions.GetValueOnGameThread() == 0) { return; }
	FString ClanName = Info.bValid ? Info.ClanName() : FString();
	if (ClanName.IsEmpty())   // блока ещё нет (4123 в пути) — имя своего клана из зеркала персонажа (`char+0x96A`)
	{
		if (const UAsdaNetSubsystem* N0 = GetGameInstance() ? GetGameInstance()->GetSubsystem<UAsdaNetSubsystem>() : nullptr) { ClanName = N0->SelfClanName; }
	}
	AskConfirm(LocSlots(17207, ClanName), 17207, [this](bool bYes)
	{
		UAsdaNetSubsystem* N = GetGameInstance() ? GetGameInstance()->GetSubsystem<UAsdaNetSubsystem>() : nullptr;
		if (!N) { return; }
		// 6530 (`sendItemPickupReq@0xBCD650`), тело 3 б: @0 u8 ответ · @1 i16 свой clanId (`char+2408`).
		FAsdaPacketWriter W(3);
		W.U8(0, bYes ? 1 : 2);
		W.I16(1, (int16)SelfClanId());
		const bool bOk = N->SendMenuPacket(6530, W.Data(), W.Num(), TEXT("ответ на передачу главы 6530"));
		UE_LOG(LogAsdaClanManage, Log, TEXT("[клан-панель] →6530 ответ %d -> %s"), bYes ? 1 : 2, bOk ? TEXT("OK") : TEXT("ОТКАЗ"));
	});
}

// ═══════════════════════════ СТРАНИЦА 5 «ГЕРБ» ═══════════════════════════════════════════════

int32 UAsdaCommunityClanSubsystem::EmSignCount() const { return UE_ARRAY_COUNT(kSignSrc); }
int32 UAsdaCommunityClanSubsystem::EmBorderCount() const { return UE_ARRAY_COUNT(kBorderIconSrc); }

void UAsdaCommunityClanSubsystem::EmblemBindWidgets()
{
	EmSliders.Reset();
	for (int32 S = 0; S < 9; ++S)
	{
		FAsdaJuiHSlider Sl;
		if (!Sl.Bind(CommWnd, kSliderIdc[S], 0, 255)) { UE_LOG(LogAsdaClanManage, Warning, TEXT("[клан-панель] ползунок «%s» не найден"), kSliderIdc[S]); }
		EmSliders.Add(Sl);
	}
}

bool UAsdaCommunityClanSubsystem::OnWheel(int32 Notches)
{
	if (CVarManageActions.GetValueOnGameThread() == 0 || !IsBoundTo(CommWnd) || Mode != 2 || ManageSubPage != 5 || Notches == 0) { return false; }
	// Окна прокрутки 36201/36399 (WheelWindow) виджета не имеют — область = прямоугольник сетки (ячейки 0 и 14).
	auto Over = [this](const TCHAR* Prefix) -> bool
	{
		UImage* A = PlaneImg(FString::Printf(TEXT("%s%02d"), Prefix, 0), 0);
		UImage* Z = PlaneImg(FString::Printf(TEXT("%s%02d"), Prefix, kEmCells - 1), 0);
		FVector2D AMn, AMx, ZMn, ZMx;
		if (!A || !Z || !AsdaUi::WidgetAbsRect(A, AMn, AMx) || !AsdaUi::WidgetAbsRect(Z, ZMn, ZMx)) { return false; }
		FVector2D C = FVector2D::ZeroVector;
		if (FSlateApplication::IsInitialized()) { C = FSlateApplication::Get().GetCursorPos(); }
		return C.X >= AMn.X && C.X <= ZMx.X && C.Y >= AMn.Y && C.Y <= ZMx.Y;
	};
	const bool bSign = Over(TEXT("CmtyClan_InsgIcon"));
	const bool bBorder = !bSign && Over(TEXT("CmtyClan_BorderIcon"));
	if (!bSign && !bBorder) { return false; }
	const int32 Rows = FMath::DivideAndRoundUp(bSign ? EmSignCount() : EmBorderCount(), kEmCols);
	int32& Pg = bSign ? EmSignPage : EmBorderPage;
	Pg = FMath::Clamp(Pg - Notches, 0, FMath::Max(Rows - kEmVisRows, 0));   // событие 0x17: позиция прокрутки → верхний ряд
	EmblemDrawGrids();
	EmblemDrawPreview();
	return true;
}

// `uiSlotGridDrawIcons@0xBC7140` / `uiInvSlotDrawIcons@0xBC7280`: 15 ячеек чистятся, затем `запись[j + 5·ряд]` рисуется
//   вырезкой (символ 64×64 в ячейку 32×32, рамка 32×20).
void UAsdaCommunityClanSubsystem::EmblemDrawGrids()
{
	if (!CommWnd) { return; }
	FString SignTex, BorderTex;
	UAsdaClanFlag::LookupSignTex(0, SignTex);
	if (const FAsdaTextRow* R = AsdaDt::RowByInt<FAsdaTextRow>(kBorderIconTablePath, 1, TEXT("AsdaClanManage"))) { BorderTex = R->tex; }
	UTexture2D* ST = UAsdaClanFlag::LoadJuiTexture(SignTex);
	UTexture2D* BT = UAsdaClanFlag::LoadJuiTexture(BorderTex);
	for (int32 J = 0; J < kEmCells; ++J)
	{
		const int32 SI = J + kEmCols * EmSignPage;
		UImage* SImg = PlaneImg(FString::Printf(TEXT("CmtyClan_InsgIcon%02d"), J), 0);
		if (SI < EmSignCount()) { SetCut(SImg, ST, FVector2D(kSignSrc[SI][0], kSignSrc[SI][1]), FVector2D(64.f, 64.f)); }
		else if (SImg) { SImg->SetVisibility(ESlateVisibility::Hidden); }
		const int32 BI = J + kEmCols * EmBorderPage;
		UImage* BImg = PlaneImg(FString::Printf(TEXT("CmtyClan_BorderIcon%02d"), J), 0);
		if (BI < EmBorderCount()) { SetCut(BImg, BT, FVector2D(kBorderIconSrc[BI][0], kBorderIconSrc[BI][1]), FVector2D(32.f, 20.f)); }
		else if (BImg) { BImg->SetVisibility(ESlateVisibility::Hidden); }
	}
}

// `uiSlotDrawIconAndTooltip(…, −1, 1)` + `uiCursorDrawIconSlot(…, −1, 1)`: символ `ячейка + 5·ряд` → превью 36112 (64×64),
//   рамка → превью 36111 (`Clan_BorderImage`, 144×96); курсоры 36200/36398 встают на ячейку.
void UAsdaCommunityClanSubsystem::EmblemDrawPreview()
{
	if (!CommWnd) { return; }
	auto PlaceSel = [this](const TCHAR* SelIdc, const FString& CellIdc, const FVector2D& Nudge)
	{
		UWidget* Sel = PlaneAny(SelIdc, 0);   // `CmtyClan_InsgSel_485_0` / `CmtyClan_BorderSel_511_0` (AsdaAnimatedPlane) — из кэша планов
		UImage* Cell = PlaneImg(CellIdc, 0);
		if (!Sel || !Cell || !MoveOnto(Sel, Cell, Nudge)) { return; }
		Sel->SetVisibility(ESlateVisibility::HitTestInvisible);
	};
	const int32 SI = EmSignCell + kEmCols * EmSignPage;
	if (SI < EmSignCount())
	{
		FString Tex; UAsdaClanFlag::LookupSignTex(SI, Tex);
		SetCut(PlaneImg(TEXT("CmtyClan_Insignia"), 0), UAsdaClanFlag::LoadJuiTexture(Tex),
			FVector2D(kSignSrc[SI][0], kSignSrc[SI][1]), FVector2D(64.f, 64.f));
		// Курсор: эталон кладёт его в (позиция ячейки + 10, +15) в координатах страницы; у ячейки 0 это на 1 px правее её.
		PlaceSel(TEXT("CmtyClan_InsgSel"), FString::Printf(TEXT("CmtyClan_InsgIcon%02d"), EmSignCell), FVector2D(1.f, 0.f));
	}
	const int32 BI = EmBorderCell + kEmCols * EmBorderPage;
	if (BI < EmBorderCount())
	{
		FString Tex; UAsdaClanFlag::LookupBorderTex(BI, Tex);
		// `Clan_BorderImage`: сетка 144×96, по 15 на текстуру, 3 столбца (совпадает с записями файла — проверено).
		const int32 K = BI % 15;
		SetCut(PlaneImg(TEXT("CmtyClan_FlagBorder"), 0), UAsdaClanFlag::LoadJuiTexture(Tex),
			FVector2D((K % 3) * 144.f, (K / 3) * 96.f), FVector2D(144.f, 96.f));
		PlaceSel(TEXT("CmtyClan_BorderSel"), FString::Printf(TEXT("CmtyClan_BorderIcon%02d"), EmBorderCell), FVector2D::ZeroVector);
	}
	EmblemApplyColors();
}

// `uiColorApplyRGBToControls@0xBC7EA0`: 36110 ← фон, 36112 ← символ, 36111 ← рамка (`CControl_SetValueAll4` — цвет всех планов).
void UAsdaCommunityClanSubsystem::EmblemApplyColors()
{
	if (!CommWnd) { return; }
	if (UImage* I = PlaneImg(TEXT("CmtyClan_FlagBG"), 0))     { I->SetColorAndOpacity(Rgb(EmBg)); }
	if (UImage* I = PlaneImg(TEXT("CmtyClan_Insignia"), 0))   { I->SetColorAndOpacity(Rgb(EmSign)); }
	if (UImage* I = PlaneImg(TEXT("CmtyClan_FlagBorder"), 0)) { I->SetColorAndOpacity(Rgb(EmBorder)); }
}

void UAsdaCommunityClanSubsystem::EmblemSyncSliders()
{
	const uint8* Src[3] = { EmBg, EmSign, EmBorder };
	for (int32 S = 0; S < EmSliders.Num(); ++S) { EmSliders[S].SetValue(Src[S / 3][S % 3]); }
}

// `uiNumberDisplay_Refresh@0xBC7AA0`: дизайн блока [66..105] (Border@0, BorderColor@8, BgColor@16, Sign@24, SignColor@32) →
//   цвета; индекс ≤ 15 — ячейка = индекс, ряд 0; иначе ячейка = индекс % 15, ряд = (индекс − ячейка) / 5.
void UAsdaCommunityClanSubsystem::EmblemLoadFromBlock()
{
	if (!Info.bValid) { EmblemReset(); return; }
	const uint8* D = Info.B + 66;
	auto Col = [](const uint8* P, uint8 Out[3]) { const uint32 V = AsdaBytes::RdU32(P); Out[0] = (V >> 16) & 0xFF; Out[1] = (V >> 8) & 0xFF; Out[2] = V & 0xFF; };
	Col(D + 16, EmBg); Col(D + 32, EmSign); Col(D + 8, EmBorder);
	// ★ [ИСПРАВЛЕНИЕ] (`asda.Clan.EmblemCellSplit`): эталон при индексе ≤ 15 ставит ячейку = индекс — у 15 это ячейка вне сетки 0…14.
	const bool bFix = CVarEmblemCellSplit.GetValueOnGameThread() != 0;
	auto Split = [bFix](int32 Idx, int32& Cell, int32& Pg)
	{
		Idx = FMath::Max(Idx, 0);
		if (bFix ? (Idx < 15) : (Idx <= 15)) { Cell = Idx; Pg = 0; return; }
		Cell = Idx % 15; Pg = (Idx - Cell) / 5;
	};
	Split(AsdaBytes::RdI32(D + 24), EmSignCell, EmSignPage);
	Split(AsdaBytes::RdI32(D + 0), EmBorderCell, EmBorderPage);
	EmblemSyncSliders();
	EmblemDrawGrids();
	EmblemDrawPreview();
}

// `uiNumberDisplay_Reset@0xBC79C0`: все цвета 255, ячейки и ряды 0.
void UAsdaCommunityClanSubsystem::EmblemReset()
{
	for (int32 I = 0; I < 3; ++I) { EmBg[I] = EmSign[I] = EmBorder[I] = 255; }
	EmSignCell = EmSignPage = EmBorderCell = EmBorderPage = 0;
	EmblemSyncSliders();
	EmblemDrawGrids();
	EmblemDrawPreview();
}

// Событие 0xB по ячейке: `uiSlotDrawIconAndTooltip(a3, 0)` / `uiCursorDrawIconSlot(a3, 0)` — ячейка = a3 − 36170 (36340).
void UAsdaCommunityClanSubsystem::EmblemCellClick(bool bSign, int32 Cell)
{
	if (bSign) { EmSignCell = Cell; } else { EmBorderCell = Cell; }
	EmblemDrawPreview();
}

// 36310 → `uiNumberDisplay_RecalcColors@0xBC76A0`: сборка 40 б дизайна (индексы `ячейка + 5·ряд`, вне таблицы → 0) и вопрос
//   17232 «Заменить существующий герб?» (блок [65] == 1) либо 17231 «Сделать этот символ гербом?» (ключи те же).
void UAsdaCommunityClanSubsystem::ActionEmblemSave()
{
	const bool bClamp = CVarEmblemIndexClamp.GetValueOnGameThread() != 0;
	int32 Sign = EmSignCell + kEmCols * EmSignPage;
	if (bClamp ? (Sign >= EmSignCount()) : (Sign > EmSignCount())) { Sign = 0; }
	int32 Border = EmBorderCell + kEmCols * EmBorderPage;
	if (bClamp ? (Border >= EmBorderCount()) : (Border > EmBorderCount())) { Border = 0; }
	FMemory::Memzero(EmDesign, sizeof(EmDesign));
	AsdaBytes::WrI32(EmDesign + 0, Border);
	AsdaBytes::WrI32(EmDesign + 8, (int32)Argb(EmBorder));
	AsdaBytes::WrI32(EmDesign + 16, (int32)Argb(EmBg));
	AsdaBytes::WrI32(EmDesign + 24, Sign);
	AsdaBytes::WrI32(EmDesign + 32, (int32)Argb(EmSign));
	const bool bReplace = Info.bValid && Info.HasEmblem() == 1;
	const int32 Key = bReplace ? 17232 : 17231;
	AskConfirm(Loc(Key), Key, [this](bool bYes) { if (bYes) { EmblemCheckCertificate(); } });
}

// «Да» 17231/17232 (0x434F/0x4350) → `invTryAddItemWithNotice@0xBC7910`: `slotTableFindByItemAttr(сумка, 62)` — первая вещь сумки
//   (затем расходников), у которой `linkCode` записи item_list == 62 ЛИБО подтип special_itemlist == 62. Есть → вопрос 17233
//   (ключ 17233), нет → нотис 17234 (ключ −9).
void UAsdaCommunityClanSubsystem::EmblemCheckCertificate()
{
	UGameInstance* GI = GetGameInstance();
	UAsdaInventorySubsystem* Inv = GI ? GI->GetSubsystem<UAsdaInventorySubsystem>() : nullptr;
	UAsdaItemTableSubsystem* Tab = GI ? GI->GetSubsystem<UAsdaItemTableSubsystem>() : nullptr;
	EmCertRec.Reset();
	if (Inv && Tab)
	{
		for (const int32 Cont : { 1, 2 })
		{
			for (int32 I = 0; I < Inv->GetContainerSize(Cont) && EmCertRec.Num() == 0; ++I)
			{
				const FAsdaItemInstance* C = Inv->GetCell(Cont, I);
				if (!C || C->ItemCode <= 0) { continue; }
				const FAsdaItemRecord* R = Tab->GetItem(C->ItemCode);
				FAsdaSpecialItemlistRow Sp;
				if ((R && R->LinkCode == kItemAttrEmblem) || (Tab->GetSpecialItem(C->ItemCode, Sp) && Sp.SubType == kItemAttrEmblem))
				{
					EmCertRec = C->Raw;
					if (EmCertRec.Num() < FAsdaItemInstance::kRecordSize) { EmCertRec.SetNumZeroed(FAsdaItemInstance::kRecordSize); }
				}
			}
			if (EmCertRec.Num() > 0) { break; }
		}
	}
	if (EmCertRec.Num() == 0)
	{
		AsdaNotice::MsgBox(GI, EAsdaMsgCategory::Social, Loc(17234), 2288, -9);
		return;
	}
	AskConfirm(Loc(17233), 17233, [this](bool bYes) { if (bYes) { SendFlag6524(); } });
}

// 6524 (`netSendAntiCheatToken_D8B0@0xBCD8B0`), тело 53 б: @0 i16 clanIndex · @2 i32 свой charId · @6 i32 код предмета ·
//   @10 i16 слот · @12 u8 контейнер · @13 40 б дизайна. Ответ 6525 (сертификат списан, нотис 17235), всем — 6527 и 6542.
void UAsdaCommunityClanSubsystem::SendFlag6524()
{
	UAsdaNetSubsystem* N = GetGameInstance() ? GetGameInstance()->GetSubsystem<UAsdaNetSubsystem>() : nullptr;
	if (!N || EmCertRec.Num() < FAsdaItemInstance::kRecordSize) { return; }
	FAsdaPacketWriter W(53);
	W.I16(0, Info.bValid ? Info.ClanIndex() : SelfClanId());
	W.I32(2, N->SelectedCharId);
	W.I32(6, AsdaBytes::RdI32(EmCertRec.GetData() + 0));
	W.I16(10, AsdaBytes::RdI16(EmCertRec.GetData() + 5));
	W.U8(12, EmCertRec[4]);
	for (int32 I = 0; I < 40; ++I) { W.U8(13 + I, EmDesign[I]); }
	const bool bOk = N->SendMenuPacket(6524, W.Data(), W.Num(), TEXT("герб клана 6524"));
	UE_LOG(LogAsdaClanManage, Log, TEXT("[клан-панель] →6524 герб: символ %d рамка %d, сертификат %d (конт. %d слот %d) -> %s"),
		AsdaBytes::RdI32(EmDesign + 24), AsdaBytes::RdI32(EmDesign + 0), AsdaBytes::RdI32(EmCertRec.GetData()),
		(int32)EmCertRec[4], (int32)AsdaBytes::RdI16(EmCertRec.GetData() + 5), bOk ? TEXT("OK") : TEXT("ОТКАЗ"));
	EmCertRec.Reset();
}

FString UAsdaCommunityClanSubsystem::DebugEmblemState() const
{
	return FString::Printf(TEXT("фон %d,%d,%d символ %d,%d,%d рамка %d,%d,%d · символ ячейка %d ряд %d · рамка ячейка %d ряд %d"),
		EmBg[0], EmBg[1], EmBg[2], EmSign[0], EmSign[1], EmSign[2], EmBorder[0], EmBorder[1], EmBorder[2],
		EmSignCell, EmSignPage, EmBorderCell, EmBorderPage);
}

// ═══════════════════════════ ПРИЁМ ═══════════════════════════════════════════════════════════

void UAsdaCommunityClanSubsystem::OnAdminPacket(int32 Opcode, const TArray<uint8>& B)
{
	if (CVarManageActions.GetValueOnGameThread() == 0) { return; }
	UGameInstance* GI = GetGameInstance();
	switch (Opcode)
	{
	case 4115: OnRecvExpel4115(B); break;
	case 4133: OnRecvMember4133(B); break;
	case 6509:   // [0] код · [1..2] i16 опкод-источник: (1, 6508) → нотис 17145 (ключ 17145)
		if (B.Num() >= 3 && B[0] == 1 && AsdaBytes::RdU16(B.GetData() + 1) == 6508) { AsdaNotice::MsgBox(GI, EAsdaMsgCategory::Social, Loc(17145), 2288, 17145); }
		break;
	case 6517: OnRecvNoticeGone6517(B); break;
	case 6520: OnRecvMemo6520(B); break;
	case 6522:   // код 2 → нотис 17172 (ключ 17172) + перерисовка «Рангов»; прочие коды эталон молча пропускает
		if (B.Num() >= 1 && B[0] == 2) { AsdaNotice::MsgBox(GI, EAsdaMsgCategory::Social, Loc(17172), 2288, 17172); RefreshGradePage(); }
		break;
	case 6523: OnRecvGrade6523(B); break;
	case 6525: OnRecvFlag6525(B); break;
	case 6527:   // блок 191 б целиком (+ i16 источник) → `uiTradeWindowSetData`: копия, перерисовка инфо-панели и герба
		if (B.Num() >= FAsdaClanBaseInfo::kSize) { OnBaseInfo(TArray<uint8>(B.GetData(), FAsdaClanBaseInfo::kSize)); }
		break;
	case 6528: OnRecvMaster6528(B); break;
	case 6529:   // мне предлагают стать главой: значок 13 на панели уведомлений (`statusBarAddIcon(…, 13)`)
		if (UAsdaHotbarSubsystem* Hb = GI ? GI->GetSubsystem<UAsdaHotbarSubsystem>() : nullptr) { Hb->AddNoticeIcon(13); }
		// [ПОРТ] имя клана для [ss1] вопроса 17207 эталон берёт из блока клана (`byte_2BE5106`); у порта блок есть, только если
		//   приходил 4123. Не приходил (вкладку не открывали) — просим его сейчас (отложенно, из тикера).
		if (!Info.bValid) { bWant4122 = true; }
		break;
	default: break;
	}
}

// 4115 `channelListRespHandler@0xBCA410`: [0] код · [1..4] userIndex · [5] charIndex · [6..25] имя.
//   0 → нотис 12422; 1 → нотис 12423, запись удаляется, перерисовка состава и «Рангов».
void UAsdaCommunityClanSubsystem::OnRecvExpel4115(const TArray<uint8>& B)
{
	if (B.Num() < 1) { return; }
	if (B[0] == 0) { NoticeBox(Loc(12422)); return; }
	if (B[0] != 1) { return; }
	NoticeBox(Loc(12423));
	if (B.Num() >= 6)
	{
		const int32 Idx = IndexOf(AsdaBytes::RdI32(B.GetData() + 1), B[5]);
		if (Idx != INDEX_NONE) { Members.RemoveAt(Idx); }
	}
	RefreshMemberList();
	RefreshGradePage();
	// 1:1 хвост `channelListRespHandler`: `uiInfoPanelSetTextFields` + `uiItemIconLayoutAndDraw` — своя плашка (имя и клан)
	//   перерисовывается. У порта это делает мир по делегату клана своего персонажа (тот же путь, что 4105).
	if (UAsdaNetSubsystem* N = GetGameInstance() ? GetGameInstance()->GetSubsystem<UAsdaNetSubsystem>() : nullptr)
	{
		N->OnClanInfo.Broadcast(-1, SelfClanId(), N->SelfClanName, FString());
	}
}

// 4133 `Recv_AuthRelogin@0xBCB760`: [0] вид · [1..2] clan · [3..133] запись 131 б.
//   1 — обновить запись · 2 — исключён/вышел: я → нотис 20052 (имя клана), сброс панели, окно 6400 закрыть, 4138;
//   другой → строка чата 20053 (цвет −11514, 10 с) и запись долой · 3 — новая запись. Затем перерисовка состава.
void UAsdaCommunityClanSubsystem::OnRecvMember4133(const TArray<uint8>& B)
{
	if (B.Num() < 3 + FAsdaClanMemberRecord::kSize) { return; }
	FAsdaClanMemberRecord R;
	FMemory::Memcpy(R.B, B.GetData() + 3, FAsdaClanMemberRecord::kSize);
	const int32 Idx = IndexOf(R.UserIndex(), R.CharIndex());
	UGameInstance* GI = GetGameInstance();
	switch (B[0])
	{
	case 1: if (Idx != INDEX_NONE) { Members[Idx] = R; } break;   // `party_UpdateMemberRecord` — только существующая
	case 3: if (Idx != INDEX_NONE) { Members[Idx] = R; } else { Members.Add(R); } break;   // `sub_925C5B`
	case 2:
	{
		const UAsdaNetSubsystem* N = GI ? GI->GetSubsystem<UAsdaNetSubsystem>() : nullptr;
		const UAsdaWorldSubsystem* WS = GI ? GI->GetSubsystem<UAsdaWorldSubsystem>() : nullptr;
		const bool bMe = N && WS && R.UserIndex() == N->SelectedCharId && R.Name().Equals(WS->GetSelfCharName(), ESearchCase::CaseSensitive);
		if (bMe)
		{
			NoticeBox(LocSlots(20052, Info.bValid ? Info.ClanName() : FString()));
			Members.Reset();   // `uiMemberPanel_Reset`: списки и блок клана
			Info = FAsdaClanBaseInfo(); ++InfoRevision;
			if (UAsdaCommunitySubsystem* Cm = GI ? GI->GetSubsystem<UAsdaCommunitySubsystem>() : nullptr) { Cm->CloseWindow(); }   // `communityUI_Close`
			bWant4138 = true;
		}
		else
		{
			AsdaNotice::ChatColored(GI, EAsdaMsgCategory::System, LocSlots(20053, R.Name()), FColor(0xFFFFD306u), 10000.f);
			if (Idx != INDEX_NONE) { Members.RemoveAt(Idx); }
		}
		break;
	}
	default: break;
	}
	RefreshMemberList();
}

// 6517 `memberListSelectById@0xBCC680`: [0..3] ключ удалённого объявления. Запись — долой; «раскрытым» становится объявление,
//   стоявшее перед удалённым (было раскрыто оно же) — или пусто, если их не осталось. Перерисовка, `wnd[487] = −1`.
void UAsdaCommunityClanSubsystem::OnRecvNoticeGone6517(const TArray<uint8>& B)
{
	if (B.Num() < 4) { return; }
	const int32 Key = AsdaBytes::RdI32(B.GetData());
	// 1:1: `v9 = (u16)wnd[483]`; обход до удалённой записи, и если удалённая — раскрытая, v9 = ключ каждой ПРЕДЫДУЩЕЙ (в итоге —
	//   стоявшей перед удалённой). Запись долой; список не пуст → `wnd[483] = v9`, иначе −1 (у порта 0 — «ничего не раскрыто»).
	int32 NewSel = (int32)(uint16)NoticeCursor;
	int32 Removed = INDEX_NONE;
	for (int32 I = 0; I < Notices.Num(); ++I)
	{
		const int32 K = Notices[I].Num() >= 4 ? AsdaBytes::RdI32(Notices[I].GetData()) : INDEX_NONE;
		if (K == Key) { Notices.RemoveAt(I); Removed = I; break; }
		if (Key == NoticeCursor) { NewSel = (int32)(uint16)K; }
	}
	// ★ [ИСПРАВЛЕНИЕ] (`asda.Clan.NoticeGoneSelect`): удалено раскрытое — раскрытым становится стоявшее перед ним, а если удалено
	//   первое — новое первое (у эталона v9 тогда остаётся ключом удалённого, панель пустеет). Ключи — полные i32 (эталон режет до u16).
	if (CVarNoticeGoneSelect.GetValueOnGameThread() != 0 && Key == NoticeCursor && Removed != INDEX_NONE)
	{
		if (Removed > 0 && Notices.IsValidIndex(Removed - 1)) { NewSel = AsdaBytes::RdI32(Notices[Removed - 1].GetData()); }
		else if (Notices.Num() > 0 && Notices[0].Num() >= 4) { NewSel = AsdaBytes::RdI32(Notices[0].GetData()); }
	}
	NoticeCursor = Notices.Num() > 0 ? NewSel : 0;
	NoticeDetailKey = INDEX_NONE;   // `wnd[487] = −1`
	RefreshNoticePanel();
}

// 6520 `partyUpdateMemberFromItem@0xBCCB20`: [0..3] userIndex · [4] charIndex · [7..70] заметка → запись [47..110]. Без перерисовки.
void UAsdaCommunityClanSubsystem::OnRecvMemo6520(const TArray<uint8>& B)
{
	if (B.Num() < 7) { return; }
	const int32 Idx = IndexOf(AsdaBytes::RdI32(B.GetData()), B[4]);
	if (Idx == INDEX_NONE) { return; }
	FMemory::Memzero(Members[Idx].B + 47, 64);
	FMemory::Memcpy(Members[Idx].B + 47, B.GetData() + 7, FMath::Min(63, B.Num() - 7));
}

// 6523 `uiHandleSlotResultPacket@0xBCD0E0`: [0..3] userIndex · [4] charIndex · [7..8] u16 звание.
//   Запись найдена → звание [33]; звание 1 → нотис 17171 «[имя] становится главой» (ключ 17171), иначе 17170
//   «[имя]: ранг меняется на '[звание]'» (ключ 17170); перерисовка «Рангов». Затем 4124 заново (отложенно) и при «это я» —
//   инфо-панель (своё звание в 14015 ставит сеть: `charSetFlagAndRefreshUi`).
void UAsdaCommunityClanSubsystem::OnRecvGrade6523(const TArray<uint8>& B)
{
	if (B.Num() < 9) { return; }
	const int32 Grade = AsdaBytes::RdU16(B.GetData() + 7);
	const int32 Idx = IndexOf(AsdaBytes::RdI32(B.GetData()), B[4]);
	UGameInstance* GI = GetGameInstance();
	if (Idx != INDEX_NONE)
	{
		Members[Idx].B[33] = (uint8)Grade;
		const FString Name = Members[Idx].Name();
		if (Grade == 1) { AsdaNotice::MsgBox(GI, EAsdaMsgCategory::Social, LocSlots(17171, Name), 2288, 17171); }
		else { AsdaNotice::MsgBox(GI, EAsdaMsgCategory::Social, LocSlots(17170, Name, Loc(17046 - Grade)), 2288, 17170); }
		RefreshGradePage();
	}
	bWant4124 = true;   // `netSendAntiCheatToken(v18)` = 4124
	const UAsdaNetSubsystem* N = GI ? GI->GetSubsystem<UAsdaNetSubsystem>() : nullptr;
	if (N && AsdaBytes::RdI32(B.GetData()) == N->SelectedCharId) { RefreshClanInfoPanel(); }
}

// 6525 `netOnRefineSlotUpdate@0xBCDB20`: [0] код · [1..4] userIndex · [5..61] запись слота 57 б. (1, свой) → запись слота
//   (количество ≠ 0 — ставится, иначе слот очищается; применяет инвентарь в `TickManage`) и нотис 17235 (ключ 17235).
void UAsdaCommunityClanSubsystem::OnRecvFlag6525(const TArray<uint8>& B)
{
	UGameInstance* GI = GetGameInstance();
	const UAsdaNetSubsystem* N = GI ? GI->GetSubsystem<UAsdaNetSubsystem>() : nullptr;
	if (B.Num() < 5 + FAsdaItemInstance::kRecordSize || B[0] != 1 || !N || AsdaBytes::RdI32(B.GetData() + 1) != N->SelectedCharId) { return; }
	EmCertRec = TArray<uint8>(B.GetData() + 5, FAsdaItemInstance::kRecordSize);
	EmCertRec.Add(1);   // признак «ждёт применения» (см. `TickManage`)
	AsdaNotice::MsgBox(GI, EAsdaMsgCategory::Social, Loc(17235), 2288, 17235);
}

// 6528 `uiPanelSelectModeNotify@0xBCD570`: [0] код. 1 → полоса 14000 на «Состав», страница панели = 3 («Объявления»), подрежим 0,
//   нотис 17210 (ключ 17210); 5 → нотис 17211 (ключ 17211) «предложение отвергнуто».
void UAsdaCommunityClanSubsystem::OnRecvMaster6528(const TArray<uint8>& B)
{
	if (B.Num() < 1) { return; }
	UGameInstance* GI = GetGameInstance();
	if (B[0] == 1)
	{
		ManageSubPage = 3;
		SwitchMode(-1);
		AsdaNotice::MsgBox(GI, EAsdaMsgCategory::Social, Loc(17210), 2288, 17210);
	}
	else if (B[0] == 5)
	{
		AsdaNotice::MsgBox(GI, EAsdaMsgCategory::Social, Loc(17211), 2288, 17211);
	}
}
