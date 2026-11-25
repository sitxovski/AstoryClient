// Copyright (c) 2026 @sitxovski (Discord)
// AsdaTutorialSubsystem.cpp — ОБУЧЕНИЕ: общее (флаги событий, сеть, окна) + КНИГА ОБУЧЕНИЯ и значок «?».
//   Гид 1950 — в AsdaTutorialGuide.cpp. Разбор — `Docs/Port/systems/TUTORIAL.md`, шапка — AsdaTutorialSubsystem.h.
#include "UI/AsdaTutorialSubsystem.h"
#include "AsdaPerf.h"   // ★ R4-c (14.11.2026): области замера времени игрового потока
#include "UI/AsdaUiHelpers.h"   // ★ R1: прямое включение (раньше приходило транзитивно)

#include "AsdaDevConsole.h"
#include "AsdaNetSubsystem.h"
#include "AsdaSelf.h"
#include "AsdaPacketWriter.h"   // ★ R5-fix-b 13.11.2026: тело 6184 — общий писатель
#include "Net/NetOpcodes.h"
#include "Login/AsdaText.h"
#include "Text/AsdaLang.h"
#include "Resources/AsdaAssetPaths.h"
#include "Resources/AsdaTextureSubsystem.h"
#include "UI/AsdaInventorySubsystem.h"
#include "Quest/AsdaQuestMarkerSubsystem.h"
#include "Quest/AsdaBoardQuestSubsystem.h"
#include "UI/AsdaWindowSubsystem.h"
#include "UI/AsdaUiVisibilitySubsystem.h"
#include "Blueprint/UserWidget.h"
#include "Components/Button.h"
#include "Components/CanvasPanelSlot.h"
#include "Components/Image.h"
#include "Components/TextBlock.h"
#include "Engine/DataTable.h"
#include "Engine/GameInstance.h"
#include "Engine/Texture2D.h"
#include "HAL/PlatformTime.h"
#include "Kismet/GameplayStatics.h"
#include "World/AsdaPlayerCharacter.h"
#include "World/AsdaPlayerController.h"   // AAsdaWorldGameMode — игровой мир
#include "GameFramework/GameModeBase.h"

DEFINE_LOG_CATEGORY_STATIC(LogAsdaTutorial, Log, All);

// ── Дефект оригинала «?» горит снова после перезахода (TUTORIAL.md §5) ──
//   «Закрыть» 69299 6184 НЕ шлёт (как оригинал; решение владельца 10.11.2026: опыт за раздел — только за «КОНЕЦ»).
//   Ключ `asda.Tutorial.BookCloseSends` (отправка при «Закрыть») снят тем же решением. Дефект закрывает локальный
//   прогресс персонажа (AsdaTutorialStore.cpp, ключ `asda.Tutorial.LocalProgress`): отметка «Закрыть» живёт в файле.

// ── [ИСПРАВЛЕНИЕ] дефекта оригинала: разговор с NPC при открытой книге (`regionWindow_OpenAndPathMove`) ──
//   Эталон первой строкой зовёт `uiSelectFirstValidTab` и при ОТКРЫТОЙ книге: меняет текущий раздел и страницу на 0 БЕЗ
//   перерисовки и поднимает «?» поверх книги — ответы дальше читаются с невидимой страницы. 1 — при открытой книге раздел
//   и страница не трогаются (гейт «книга открыта → разговора нет» остаётся); 0 — как оригинал.
static ASDA_DEV_CVAR(int32, CVarTutorialNpcKeepsBook, TEXT("asda.Tutorial.NpcKeepsBook"), 1,
	TEXT("1 — разговор с NPC при открытой книге обучения не сбрасывает её раздел/страницу и не поднимает «?» [ИСПРАВЛЕНИЕ]; 0 — как оригинал."),
	ECVF_Default);

namespace AsdaTutorialBook
{
	// Окна и их ассеты (`Content/AsdaAssets/UI/Player_UI/Tutorial`).
	static const FName kNoticeId(TEXT("Tutorial_Notice_WIN"));   // 69800
	static const FName kBookId(TEXT("Tutorial_WIN"));            // 69200
	static const TCHAR* kNoticeWbp = TEXT("/Game/AsdaAssets/UI/Player_UI/Tutorial/WBP_Tutorial_Notice_WIN.WBP_Tutorial_Notice_WIN_C");
	static const TCHAR* kBookWbp   = TEXT("/Game/AsdaAssets/UI/Player_UI/Tutorial/WBP_Tutorial_WIN.WBP_Tutorial_WIN_C");
	static const TCHAR* kTable     = TEXT("/Game/AsdaAssets/Data/DT_Tutorial.DT_Tutorial");
	constexpr int32 kNoticeZ = 700;      // значок поверх HUD, под обычными окнами
	constexpr int32 kBookZ = 730;        // полоса книги — уровень обычных окон
	constexpr double kCycleMs = 3000.0;  // `uiCycleToNextValidTab`: `timeGetTime() - this[52] > 0xBB8`
	constexpr int32 kPages = 20;         // `this[50] < 0x14`
	constexpr int32 kLocClose = 10035;   // подпись 69299 «Закрыть» (`tutorialUiInitControls`)
	constexpr int32 kMaskBits = 256;     // 32 байта маски
}

// ══════════════════════════════ Общая точка флагов (AsdaTutorialEvents.h) ══════════════════════════════
// Подсистема обучения по любому объекту с доступом к GameInstance (сам GameInstance, актор, подсистема, виджет).
static UAsdaTutorialSubsystem* AsdaTutorialOf(const UObject* Ctx)
{
	if (!Ctx) { return nullptr; }
	const UGameInstance* GI = Cast<UGameInstance>(Ctx);
	if (!GI)
	{
		const UWorld* W = Ctx->GetWorld();
		GI = W ? W->GetGameInstance() : nullptr;
	}
	return GI ? GI->GetSubsystem<UAsdaTutorialSubsystem>() : nullptr;
}

void AsdaTutorial::Mark(const UObject* Ctx, EAsdaTutorialFlag Flag)
{
	if (UAsdaTutorialSubsystem* T = AsdaTutorialOf(Ctx)) { T->MarkFlag(Flag); }
}

bool AsdaTutorial::OnNpcTalkOpening(const UObject* Ctx)
{
	UAsdaTutorialSubsystem* T = AsdaTutorialOf(Ctx);
	return T ? T->NpcTalkOpening() : true;
}

void AsdaTutorial::OnMaskPacket(const UObject* Ctx, const uint8* Data, int32 Len)
{
	if (UAsdaTutorialSubsystem* T = AsdaTutorialOf(Ctx)) { T->ApplyMask(Data, Len); }
}

void UAsdaTutorialSubsystem::MarkFlag(EAsdaTutorialFlag Flag)
{
	const int32 I = (int32)Flag;
	if (I >= 0 && I < (int32)UE_ARRAY_COUNT(GuideFlags)) { GuideFlags[I] = 1; }
}

// ══════════════════════════════ Жизненный цикл ══════════════════════════════
void UAsdaTutorialSubsystem::Initialize(FSubsystemCollectionBase& Collection)
{
	Collection.InitializeDependency<UAsdaNetSubsystem>();
	Collection.InitializeDependency<UAsdaWindowSubsystem>();
	Super::Initialize(Collection);
	UGameInstance* GI = GetGameInstance();
	if (UAsdaNetSubsystem* Net = GI ? GI->GetSubsystem<UAsdaNetSubsystem>() : nullptr)
	{
		HWorldReady  = Net->OnWorldReady.AddUObject(this, &UAsdaTutorialSubsystem::OnWorldReady);
		HSelfExp     = Net->OnSelfExp.AddUObject(this, &UAsdaTutorialSubsystem::OnSelfExp);
		HQuestStep   = Net->OnQuestStepResult.AddUObject(this, &UAsdaTutorialSubsystem::OnQuestStepResult);
		HQuestTurnIn = Net->OnQuestTurnInResult.AddUObject(this, &UAsdaTutorialSubsystem::OnQuestTurnInResult);
		HQuestSync   = Net->OnQuestStateSync.AddUObject(this, &UAsdaTutorialSubsystem::OnQuestPacket);
		HQuestProg   = Net->OnQuestProgress.AddUObject(this, &UAsdaTutorialSubsystem::OnQuestPacket);
	}
	// ESC по открытой полосе книги: эталонный проход шлёт ей событие 21, а `uiSkillTutorialOnCommand@0xB79980`
	//   ветки 21 не имеет ⇒ нажатие СЪЕДЕНО и окно не закрывается (69200 стоит в таблице ESC — `kEscCloseWindows`).
	if (UAsdaWindowSubsystem* WM = GI ? GI->GetSubsystem<UAsdaWindowSubsystem>() : nullptr)
	{
		WM->RegisterEscSwallower(AsdaTutorialBook::kBookId);
	}
}

void UAsdaTutorialSubsystem::Deinitialize()
{
	UGameInstance* GI = GetGameInstance();
	if (UAsdaNetSubsystem* Net = GI ? GI->GetSubsystem<UAsdaNetSubsystem>() : nullptr)
	{
		Net->OnWorldReady.Remove(HWorldReady);
		Net->OnSelfExp.Remove(HSelfExp);
		Net->OnQuestStepResult.Remove(HQuestStep);
		Net->OnQuestTurnInResult.Remove(HQuestTurnIn);
		Net->OnQuestStateSync.Remove(HQuestSync);
		Net->OnQuestProgress.Remove(HQuestProg);
	}
	for (TObjectPtr<UUserWidget>* W : { &GuideWin, &NoticeWin, &BookWin })
	{
		if (*W) { (*W)->RemoveFromParent(); *W = nullptr; }
	}
	Super::Deinitialize();
}

void UAsdaTutorialSubsystem::ResetSessionState(EAsdaSessionResetReason Reason)
{
	// Смена карты: эталон на загрузке гасит гид (`[0] = 0`, game_main.c:3542) и поднимает снова на этапе 11 при
	//   уровне 1 — шаг ПРОДОЛЖАЕТСЯ (счётчик +4 живёт до новой сборки интерфейса). Виджеты старого мира сносит UE.
	CloseGuide(TEXT("смена карты/сброс сессии"));
	// Разрыв связи мир не выгружает — окна живы на экране: свернуть, прежде чем забыть (иначе «сироты» до перехода).
	for (TObjectPtr<UUserWidget>* W : { &GuideWin, &NoticeWin, &BookWin })
	{
		if (*W) { (*W)->SetVisibility(ESlateVisibility::Collapsed); *W = nullptr; }
	}
	GuideMouse.Reset(); BookMouse.Reset();
	BookHoverCtl = BookPressCtl = -1;
	// Полоса книги на загрузке остаётся «открытой» у эталона (объект живёт), но её окно — нет: закрываем как «Закрыть»
	//   без отметки, чтобы гейт разговора с NPC не держался на невидимом окне.
	bBookOpen = false;
	bNoticeShown = false;
	if (!AsdaIsFullSessionReset(Reason)) { return; }
	// Локальный прогресс (AsdaTutorialStore.cpp): привязка сессии забыта — следующий вход загрузит файл СВОЕГО персонажа.
	StoreBinding.Empty();
	FMemory::Memzero(LocalMask, sizeof(LocalMask));
	bGuideDismissed = false;
	bSavedSnapshotValid = false;
	// Новый персонаж / выход: прогресс гида с начала (эталон: `uiTradeWindowInit` при сборке интерфейса ставит шаг 1),
	//   маска чужого персонажа не должна протечь. ★ 6185 приходит от игрового сервера ПОСЛЕ 1021, поэтому чистка при
	//   `EnteringWorld` его не теряет (закон `session-reset-drops-early-data`): маска того же входа ещё не пришла.
	GuideStep = 1;
	GuideShownStep = 0;
	FMemory::Memzero(GuideFlags, sizeof(GuideFlags));
	FMemory::Memzero(Mask, sizeof(Mask));
	bMaskKnown = false;
	BookCategory = -1;
	BookPage = 0;
	BookHover = -1;
	BookTimerMs = 0.0;
}

void UAsdaTutorialSubsystem::Tick(float DeltaTime)
{
	ASDA_PERF_SCOPE("Tick.Tutorial");   // ★ R4-c (14.11.2026): область замера (AsdaPerf.h; в Shipping пусто)
	GuideTick(DeltaTime);
	QuestIconCheckTick();
	CycleTick();
	BookMouseTick();
}

// ══════════════════════════════ Сеть ══════════════════════════════
void UAsdaTutorialSubsystem::OnSelfExp(int32 Level, int64 Exp, int64 Gained, bool bFullExp, int32 SrcRid)
{
	// `netHandleHpExpUpdate@0xF427A0` (network.c:42570): флаг ставится только в третьей ветке — не «полный опыт»
	//   (`Flag != 1`), источник известен (`SrcRid != -1`), нотис 100900 «получено опыта». Опыт без источника — без флага.
	if (!bFullExp && SrcRid != -1 && Gained > 0) { MarkFlag(EAsdaTutorialFlag::ExpGained); }
}

void UAsdaTutorialSubsystem::OnQuestStepResult(int32 Result, int32 TalkId, const TArray<uint8>& Body)
{
	MarkFlag(EAsdaTutorialFlag::QuestPacket);   // первая строка `CQuest_RecvPacket@0xD9D260`, до разбора кода
	bQuestIconCheck = true;
}

void UAsdaTutorialSubsystem::OnQuestTurnInResult(int32 StepCode, int32 QuestNo, int32 Stage, int32 FinishType,
	int32 TalkId, const TArray<uint8>& Body)
{
	// `net_ApplyPlayerFullUpdate@0xD828A0` (network.c:21783-21786): байт 81 — только при виде завершения 2 (задание сдано целиком).
	if (FinishType == 2) { MarkFlag(EAsdaTutorialFlag::PlayerFull); }
	bQuestIconCheck = true;
}

void UAsdaTutorialSubsystem::OnQuestPacket(const TArray<uint8>& Body)
{
	bQuestIconCheck = true;   // пакеты журнала эталон доводит до `CQuestLog_RefreshSlotIcons` и при закрытом окне
}

void UAsdaTutorialSubsystem::QuestIconCheckTick()
{
	// `g_bQuestIconUpdated` (0x2C715F8, байт 96) ставит `CQuestLog_RefreshSlotIcons@0xC5D500` в ветках «цель выполнена»
	//   (эпизод: `charStateFindAndBuildRequest`; задание доски: готово к сдаче или вид слота 1). Эталон зовёт её из
	//   обработчиков пакетов журнала (quest.c:2711/3079/3458/3605) независимо от окна — порт проверяет тот же итог
	//   кадром позже (данные журнала к этому моменту разобраны своими подписчиками). Открытое окно отмечает и само.
	if (!bQuestIconCheck) { return; }
	bQuestIconCheck = false;
	UGameInstance* GI = GetGameInstance();
	const UAsdaQuestMarkerSubsystem* QM = GI ? GI->GetSubsystem<UAsdaQuestMarkerSubsystem>() : nullptr;
	const UAsdaBoardQuestSubsystem* Board = GI ? GI->GetSubsystem<UAsdaBoardQuestSubsystem>() : nullptr;
	if (!QM) { return; }
	for (const FAsdaQuestSlot& S : QM->GetSlots())
	{
		if (S.QuestId < 2001) { continue; }   // ячейка пуста
		const bool bBoard = Board && Board->FindQuest(S.QuestNo) != nullptr;
		const bool bDone = bBoard ? (Board->IsQuestReady(S.QuestNo) || S.ObjType == 1) : QM->IsQuestObjectiveDone(S.QuestNo);
		if (bDone) { MarkFlag(EAsdaTutorialFlag::QuestLogIcons); return; }
	}
}

void UAsdaTutorialSubsystem::ApplyMask(const uint8* Data, int32 Len)
{
	// `skillTutorial_RecvBlob32@0xB7ADC0`: memcpy(this+216, пакет+9, 32) — тело целиком, без проверок.
	if (!Data || Len < (int32)sizeof(Mask))
	{
		UE_LOG(LogAsdaTutorial, Warning, TEXT("[обучение] 6185: тело %d б < 32 — пропуск"), Len);
		return;
	}
	FMemory::Memcpy(Mask, Data, sizeof(Mask));
	bMaskKnown = true;
	EnsureProgressLoaded();
	int32 Server = 0, Done = 0;
	for (int32 I = 0; I < AsdaTutorialBook::kMaskBits; ++I)
	{
		Server += (Mask[I >> 3] >> (I & 7)) & 1;
		Done += IsSectionDone(I) ? 1 : 0;
	}
	UE_LOG(LogAsdaTutorial, Log, TEXT("[обучение] ←6185 маска разделов книги: на сервере %d, с локальными %d"), Server, Done);
}

bool UAsdaTutorialSubsystem::IsSectionDone(int32 Index) const
{
	if (Index < 0 || Index >= AsdaTutorialBook::kMaskBits) { return false; }
	// `bitmask_testBit`: qword[I/64] бит I%64 (LE). [ИСПРАВЛЕНИЕ] Бит сервера ИЛИ локальный (AsdaTutorialStore.cpp).
	return ((Mask[Index >> 3] | LocalMask[Index >> 3]) & (uint8)(1u << (Index & 7))) != 0;
}

void UAsdaTutorialSubsystem::SetSectionBit(int32 Index, bool bOn)
{
	if (Index < 0 || Index >= AsdaTutorialBook::kMaskBits) { return; }
	const uint8 Bit = (uint8)(1u << (Index & 7));
	if (bOn) { Mask[Index >> 3] |= Bit; LocalMask[Index >> 3] |= Bit; }   // локальная копия — в файл персонажа
	else { Mask[Index >> 3] &= (uint8)~Bit; LocalMask[Index >> 3] &= (uint8)~Bit; }
}

bool UAsdaTutorialSubsystem::SendSectionDone(int32 Index, const TCHAR* Why)
{
	// `netSendSlotCmd_6184@0xB7AE80`: конверт «секрет 8 + MD5(8) + 8», полезная нагрузка 8 байт:
	//   @0 i32 charId (`g_LocalPlayer+1052`), @4 u16 userNum (`g_pLocalSessionCtx+190188`), @6 u16 id раздела (+0 записи).
	//   Сервер `Recv_UserTutorialInfoUpdate@0x55FA30` сверяет charId, ставит бит, начисляет опыт и пишет в БД.
	UGameInstance* GI = GetGameInstance();
	UAsdaNetSubsystem* Net = GI ? GI->GetSubsystem<UAsdaNetSubsystem>() : nullptr;
	if (!Net || !Sections.IsValidIndex(Index)) { return false; }
	// ★ R5-fix-b 13.11.2026: сверено с эталоном (network.c:9713-9842): тело собирается в `authHashInput[2]` — dword charId,
	//   LOWORD userNum, HIWORD id раздела (`*pSlotEntry`, +0 записи 4672 б); MD5 по этим 8 байтам, буфер 32 б = секрет 8 +
	//   MD5 16 + тело 8, `CNetwork_Send(…, 32, 6184, 1, 1, 1, 0, 0)` — ровно то, что собирает `SendMenuPacket` (пад 1).
	//   Расхождения нет; ручные `Memcpy` заменены общим писателем — байты те же (LE, младшие 16 бит userNum).
	const int32 CharId = AsdaSelf::GetCharId(Net);
	const int32 UserNum = AsdaSelf::GetUserIdxOr(Net, 0);
	const uint16 Id = (uint16)Sections[Index].Id;
	FAsdaPacketWriter P(8);                                   // тело 6184 (8 б)
	P.I32(0, CharId).U16(4, (uint32)UserNum).U16(6, Id);     // @0 charId, @4 userNum (u16), @6 id раздела
	const bool bOk = Net->SendMenuPacket((uint16)OP_RECV_USER_TUTORIAL_INFO_UPDATE, P.Data(), P.Num(), TEXT("6184 раздел книги пройден"));
	UE_LOG(LogAsdaTutorial, Log, TEXT("[обучение] →6184 раздел %d (id %d) пройден, charId=%d userNum=%d (%s) -> %s"),
		Index, (int32)Id, CharId, UserNum, Why, bOk ? TEXT("OK") : TEXT("FAIL"));
	return bOk;
}

// ══════════════════════════════ Общие помощники ══════════════════════════════
bool UAsdaTutorialSubsystem::IsInGameWorld() const
{
	// `g_GameState == 1` — игровой мир (режим `AAsdaWorldGameMode`; карта логина идёт другим режимом). Павн не спрашиваем:
	//   свой персонаж появляется по 4003 позже, а опрос «?» и тик гида эталона от него не зависят.
	const UGameInstance* GI = GetGameInstance();
	const UWorld* W = GI ? GI->GetWorld() : nullptr;
	return W && W->IsGameWorld() && W->GetAuthGameMode() && W->GetAuthGameMode()->IsA<AAsdaWorldGameMode>();
}

FString UAsdaTutorialSubsystem::LocText(int32 Id) const
{
	return AsdaUi::LocRaw(GetGameInstance(), Id, /*bDebugFallback=*/false);
}

UUserWidget* UAsdaTutorialSubsystem::EnsureWindow(FName WindowId, const TCHAR* Wbp, int32 Z, TObjectPtr<UUserWidget>& Slot)
{
	UGameInstance* GI = GetGameInstance();
	UWorld* W = GI ? GI->GetWorld() : nullptr;
	if (Slot && Slot->IsInViewport() && Slot->GetWorld() == W) { return Slot; }
	Slot = nullptr;   // окно прежнего мира (травел сносит виджеты) — поднять заново
	UAsdaWindowSubsystem* WM = GI ? GI->GetSubsystem<UAsdaWindowSubsystem>() : nullptr;
	if (!WM || !W) { return nullptr; }
	Slot = WM->OpenWindowByPath(WindowId, Wbp, Z, ESlateVisibility::SelfHitTestInvisible);
	if (Slot) { UAsdaUiVisibilitySubsystem::TrackFor(this, Slot); }   // Alt+Z гасит окно вместе с интерфейсом
	return Slot;
}

void UAsdaTutorialSubsystem::SetImageTexture(UImage* Img, const FString& Name, bool bNpcImage)
{
	// `CControl_DrawTextureRect`: сначала «пустая» текстура (`CClass_GetSubObjectAtFixedOff`), затем `<имя>.tex`.
	//   Файла нет — контрол остаётся пустым. Порт: пустой = скрыт.
	if (!Img) { return; }
	UTexture2D* Tex = nullptr;
	if (!Name.IsEmpty())
	{
		if (UAsdaTextureSubsystem* TM = AsdaTexMgr())
		{
			Tex = TM->LoadUITexture(FName(*Name.ToLower()));
			if (!Tex && bNpcImage)
			{
				Tex = TM->LoadTextureCached(FString(AsdaAssetPaths::kNpcImageRoot) / Name);
			}
		}
	}
	if (!Tex)
	{
		Img->SetVisibility(ESlateVisibility::Hidden);
		return;
	}
	FVector2D Pos, Size;
	if (!AsdaUi::SlotRect(Img, Pos, Size)) { Size = FVector2D(Tex->GetSizeX(), Tex->GetSizeY()); }
	FSlateBrush B;
	B.SetResourceObject(Tex);
	B.ImageSize = Size;
	Img->SetBrush(B);
	Img->SetVisibility(ESlateVisibility::SelfHitTestInvisible);
}

// ══════════════════════════════ Книга: данные и условия ══════════════════════════════
bool UAsdaTutorialSubsystem::LoadBook()
{
	if (Sections.Num() > 0) { return true; }
	if (bBookTableMissing) { return false; }
	BookTable = LoadObject<UDataTable>(nullptr, AsdaTutorialBook::kTable);
	if (!BookTable)
	{
		bBookTableMissing = true;   // предупреждение — один раз за жизнь подсистемы, опрос раз в 3 с не спамит
		UE_LOG(LogAsdaTutorial, Warning, TEXT("[обучение] нет таблицы %s (импорт Tools/asda_import_tutorial.py)"), AsdaTutorialBook::kTable);
		return false;
	}
	TArray<FAsdaTutorialRow*> Rows;
	BookTable->GetAllRows<FAsdaTutorialRow>(TEXT("Tutorial"), Rows);
	Sections.Reset();
	Sections.SetNum(Rows.Num());
	for (const FAsdaTutorialRow* R : Rows)
	{
		if (R && Sections.IsValidIndex(R->Index)) { Sections[R->Index] = *R; }
	}
	UE_LOG(LogAsdaTutorial, Log, TEXT("[обучение] книга: разделов %d"), Sections.Num());
	return Sections.Num() > 0;
}

bool UAsdaTutorialSubsystem::EvalPredicate(int32 Type, int32 A, int32 B) const
{
	// Таблица `m_pPredicateFns` (`CTutorial_Init@0xB77B90`), вызов `(this, A, B)`; вид > 16 — условие пропускается
	//   (`entry_CheckRequirements`: `if (type <= 0x10 && !pred(...)) return 0`).
	UGameInstance* GI = GetGameInstance();
	switch (Type)
	{
	case 0:   // `sub_B79DF0`: уровень своего персонажа ≥ A
		return AsdaSelf::GetLevel(GI) >= A;
	case 1:   // `sub_B79E40`: класс (`gap_438[0]`) == A
		return AsdaSelf::GetClassId(GI) == A;
	case 9:   // `inventoryHasItemCount@0xB7A1E0`: предмет A в количестве ≥ B — надетые 9, сумка, расходники
	{
		const UAsdaInventorySubsystem* Inv = GI ? GI->GetSubsystem<UAsdaInventorySubsystem>() : nullptr;
		if (!Inv) { return false; }
		struct FScan { int32 Cont; int32 N; };
		static const FScan kScan[] = { { 3, 9 }, { 1, 60 }, { 2, 60 } };   // эталон: 9 / 30 (у порта сумка 60, P96) / 60
		for (const FScan& S : kScan)
		{
			for (int32 I = 0; I < S.N; ++I)
			{
				const FAsdaItemInstance* C = Inv->GetCell(S.Cont, I);
				if (C && C->ItemCode == A && C->Amount >= B) { return true; }
			}
		}
		return false;
	}
	default:
		// Остальные 14 видов (ветка навыков, значения магазина, бафф, окна, звук, цель…) в таблице рабочей сборки НЕ
		//   встречаются ни разу (все 27 разделов — виды 0 и 9, `Reports/tutorial/import_check.txt`). Порт не
		//   выдумывает их смысл: такой раздел не предлагается, а лог называет вид для разбора.
		UE_LOG(LogAsdaTutorial, Warning, TEXT("[обучение] условие вида %d (A=%d B=%d) в порте не разобрано — раздел не предлагается"), Type, A, B);
		return false;
	}
}

bool UAsdaTutorialSubsystem::CheckRequirements(int32 Index) const
{
	if (!Sections.IsValidIndex(Index)) { return false; }
	const FAsdaTutorialRow& R = Sections[Index];
	for (int32 I = 0; I < 3; ++I)
	{
		const int32 T = R.CondType.IsValidIndex(I) ? R.CondType[I] : -1;
		// `*(DWORD*)(v4 + 4*i + 76) <= 0x10u` — беззнаковое сравнение: −1 и > 16 пропускаются.
		if ((uint32)T > 16u) { continue; }
		const int32 A = R.CondA.IsValidIndex(I) ? R.CondA[I] : -1;
		const int32 B = R.CondB.IsValidIndex(I) ? R.CondB[I] : -1;
		if (!EvalPredicate(T, A, B)) { return false; }
	}
	return true;
}

bool UAsdaTutorialSubsystem::SelectFirstValidSection()
{
	// `uiSelectFirstValidTab@0xB79370`: первый раздел без бита и с выполненными условиями → текущий, стр. 0, показ «?».
	if (!LoadBook()) { return false; }
	EnsureProgressLoaded();   // локальные отметки персонажа — до выбора раздела
	for (int32 I = 0; I < Sections.Num(); ++I)
	{
		if (!IsSectionDone(I) && CheckRequirements(I))
		{
			BookCategory = I;
			BookPage = 0;
			ShowNotice(true);
			return true;
		}
	}
	return false;
}

void UAsdaTutorialSubsystem::CycleTick()
{
	// `uiCycleToNextValidTab@0xB79420` (главный цикл, только в мире — `g_GameState == 1`): пока книга закрыта,
	//   раз в 3 с ищется раздел. Найденный НЕ гасит прежний «?» и не гасится, если раздела больше нет — как эталон.
	if (bBookOpen) { return; }
	if (!IsInGameWorld()) { return; }   // `g_GameState == 1`
	const double Now = FPlatformTime::Seconds() * 1000.0;
	if (Now - BookTimerMs <= AsdaTutorialBook::kCycleMs) { return; }
	BookTimerMs = Now;
	SelectFirstValidSection();
}

bool UAsdaTutorialSubsystem::NpcTalkOpening()
{
	if (!(bBookOpen && CVarTutorialNpcKeepsBook.GetValueOnGameThread() != 0))
	{
		SelectFirstValidSection();   // `uiSelectFirstValidTab(g_pSkillTutorialWnd)`
	}
	if (bBookOpen)               // `if (sub_B79200(book)) return` — разговор не открывается
	{
		UE_LOG(LogAsdaTutorial, Log, TEXT("[обучение] открыта книга обучения — разговор с NPC не открыт (regionWindow_OpenAndPathMove)"));
		return false;
	}
	return true;
}

// ══════════════════════════════ Книга: окна ══════════════════════════════
bool UAsdaTutorialSubsystem::EnsureBookWidgets()
{
	using namespace AsdaTutorialBook;
	UUserWidget* N = EnsureWindow(kNoticeId, kNoticeWbp, kNoticeZ, NoticeWin);
	UUserWidget* B = EnsureWindow(kBookId, kBookWbp, kBookZ, BookWin);
	if (!N || !B) { return false; }
	if (NoticeBound.Get() != N)
	{
		NoticeBound = N;
		NoticeClick = AsdaUi::FindNodeExact(N, TEXT("Tutorial_Notice_click_4_0"));
		N->SetVisibility(bNoticeShown ? ESlateVisibility::SelfHitTestInvisible : ESlateVisibility::Collapsed);
	}
	if (BookBound.Get() != B)
	{
		BookBound = B;
		UAsdaRichText::CaptureFmt(BookTitle, Cast<UTextBlock>(AsdaUi::FindNodeExact(B, TEXT("Tutorial_T1"))));
		UAsdaRichText::CaptureFmt(BookDesc, Cast<UTextBlock>(AsdaUi::FindNodeExact(B, TEXT("Tutorial_T2"))));
		for (int32 I = 0; I < 3; ++I)
		{
			const FString Nm = FString::Printf(TEXT("Tutorial_TT%d"), I + 1);
			UAsdaRichText::CaptureFmt(BookAnswer[I], Cast<UTextBlock>(AsdaUi::FindNodeExact(B, *Nm)));
			BookAnswerHit[I] = AsdaUi::FindNodeByPrefix(B, *(Nm + TEXT("_")));   // `Tutorial_TT<n>_<k>_HIT` — прямоугольник 69206+i
		}
		BookHighlight = AsdaUi::FindNodeByPrefix(B, TEXT("Tutorial_LIST_T_Check"));
		BookImgLeft  = Cast<UImage>(AsdaUi::FindNodeExact(B, TEXT("Tutorial_img_left_11_0")));
		BookImgMid   = Cast<UImage>(AsdaUi::FindNodeExact(B, TEXT("Tutorial_img_midle_13_0")));
		BookImgRight = Cast<UImage>(AsdaUi::FindNodeExact(B, TEXT("Tutorial_img_right_12_0")));
		BookCloseBtn = Cast<UButton>(AsdaUi::FindNodeExact(B, TEXT("Tutorial_XX")));
		BookCloseTxt = Cast<UTextBlock>(AsdaUi::FindNodeExact(B, TEXT("Tutorial_XX_Text")));
		if (BookCloseBtn.IsValid()) { BookCloseBtn->OnClicked.AddUniqueDynamic(this, &UAsdaTutorialSubsystem::OnBookCloseClicked); }
		// `tutorialUiInitControls`: подпись 69299 = loc 10035, 69311 и прочие — меню 69300 (не показывается).
		if (BookCloseTxt.IsValid()) { BookCloseTxt->SetText(FText::FromString(FAsdaMarkup::StripTags(LocText(kLocClose)))); }
		B->SetVisibility(bBookOpen ? ESlateVisibility::SelfHitTestInvisible : ESlateVisibility::Collapsed);
		if (bBookOpen) { RefreshPage(); }
	}
	return true;
}

void UAsdaTutorialSubsystem::ShowNotice(bool bOn)
{
	bNoticeShown = bOn;
	if (!EnsureBookWidgets()) { return; }
	NoticeWin->SetVisibility(bOn ? ESlateVisibility::SelfHitTestInvisible : ESlateVisibility::Collapsed);
}

void UAsdaTutorialSubsystem::OpenBook()
{
	// Клик по 69801: `tutorialPanelShowAndRefresh@0xB792A0` (показ 69200, `*this = 1`, MapInsert, стр. 0, обновить)
	//   и `CControl_SetVisible(69800, 0)`.
	if (!Sections.IsValidIndex(BookCategory) || !EnsureBookWidgets()) { return; }
	bBookOpen = true;
	BookPage = 0;
	BookWin->SetVisibility(ESlateVisibility::SelfHitTestInvisible);
	if (UAsdaWindowSubsystem* WM = GetGameInstance()->GetSubsystem<UAsdaWindowSubsystem>()) { WM->BringWindowToFront(AsdaTutorialBook::kBookId); }
	RefreshPage();
	ShowNotice(false);
	UE_LOG(LogAsdaTutorial, Log, TEXT("[обучение] открыта книга: раздел %d «%s»"), BookCategory, *Sections[BookCategory].Caption);
}

void UAsdaTutorialSubsystem::RefreshPage()
{
	// `TutorialUI_RefreshPage@0xB78460`.
	if (!EnsureBookWidgets()) { return; }
	UAsdaRichText::SetFmt(BookTitle, FString());
	UAsdaRichText::SetFmt(BookDesc, FString());
	for (FAsdaFmtText& A : BookAnswer) { UAsdaRichText::SetFmt(A, FString()); }
	if (!(Sections.IsValidIndex(BookCategory) && BookPage >= 0 && BookPage < AsdaTutorialBook::kPages))
	{
		// Страница вне 0…19 (ответ «КОНЕЦ» → переход 100): спрятать, бит, 6184.
		const int32 Cat = BookCategory;
		HideBookAndMark(TEXT("страница вне диапазона (ответ «конец»)"), /*bSave=*/false);
		SetSectionBit(Cat, true);
		// Локальная отметка «КОНЕЦ» — только если 6184 ушёл: иначе (обрыв связи) раздел навсегда скрылся бы без опыта.
		//   Бит в памяти остаётся до перезахода, как у оригинала; после входа раздел предложится снова.
		if (!SendSectionDone(Cat, TEXT("ответ «конец»")) && Cat >= 0 && Cat < AsdaTutorialBook::kMaskBits)
		{
			LocalMask[Cat >> 3] &= (uint8)~(1u << (Cat & 7));
		}
		SaveProgress(TEXT("ответ «конец»"));
		return;
	}
	const FAsdaTutorialRow& R = Sections[BookCategory];
	const FAsdaTutorialPage* P = R.Pages.IsValidIndex(BookPage) ? &R.Pages[BookPage] : nullptr;
	if (!P) { return; }
	const FString Key = FString::Printf(TEXT("%d.p%d"), BookCategory, BookPage);
	UAsdaRichText::SetFmt(BookTitle, FAsdaLang::Resolve(TEXT("Tutorial"), Key, P->Title));
	if (P->DescId >= 0) { UAsdaRichText::SetFmt(BookDesc, FAsdaText::Get(TEXT("Tutorial_text"), P->DescId, TEXT("text"))); }
	for (int32 J = 0; J < 3; ++J)
	{
		const int32 AnsId = P->AnswerId.IsValidIndex(J) ? P->AnswerId[J] : -1;
		if (AnsId >= 0) { UAsdaRichText::SetFmt(BookAnswer[J], FAsdaText::Get(TEXT("Tutorial_text_answer"), AnsId, TEXT("text"))); }
		// Подсветка (`this[43]` = 69216) — на строку выбранного ответа, со сдвигом (−2, −2).
		if (BookHover == J && BookHighlight.IsValid())
		{
			FVector2D Pos, Size;
			UWidget* Line = BookAnswer[J].Node ? BookAnswer[J].Node.Get() : nullptr;
			if (Line && AsdaUi::SlotRect(Line, Pos, Size)) { AsdaUi::SetSlotPos(BookHighlight.Get(), Pos - FVector2D(2.f, 2.f)); }
		}
	}
	SetImageTexture(BookImgLeft.Get(), FString(), false);
	SetImageTexture(BookImgMid.Get(), FString(), false);
	SetImageTexture(BookImgRight.Get(), FString(), false);
	if (P->RobotImage.Len() > 1)
	{
		if (P->RobotImageType == 1) { SetImageTexture(BookImgLeft.Get(), P->RobotImage, true); }
		if (P->RobotImageType == 2) { SetImageTexture(BookImgRight.Get(), P->RobotImage, true); }
	}
	if (P->Image.Len() > 1) { SetImageTexture(BookImgMid.Get(), P->Image, false); }
}

void UAsdaTutorialSubsystem::HideBookAndMark(const TCHAR* Why, bool bSave)
{
	// `uiPanelHideAndResetTimer@0xB79310`: скрыть 69200, `this[52] = timeGetTime()`, бит раздела, `*this = 0`.
	if (BookWin) { BookWin->SetVisibility(ESlateVisibility::Collapsed); }
	BookTimerMs = FPlatformTime::Seconds() * 1000.0;
	SetSectionBit(BookCategory, true);
	bBookOpen = false;
	if (BookHighlight.IsValid()) { BookHighlight->SetVisibility(ESlateVisibility::Collapsed); }
	UE_LOG(LogAsdaTutorial, Log, TEXT("[обучение] книга закрыта: раздел %d отмечен (%s)"), BookCategory, Why);
	if (bSave) { SaveProgress(Why); }   // [ИСПРАВЛЕНИЕ] отметка переживает перезаход (оригинал держал её только в памяти)
}

void UAsdaTutorialSubsystem::OnBookCloseClicked()
{
	// 69299 «Закрыть» (`uiSkillTutorialOnCommand`, событие 66561) → `uiPanelHideAndResetTimer`. 6184 не уходит, опыта
	//   за раздел нет (как оригинал, решение владельца 10.11); отметка — локально в файл персонажа (HideBookAndMark).
	HideBookAndMark(TEXT("кнопка «Закрыть» 69299"));
}

void UAsdaTutorialSubsystem::BookMouseTick()
{
	// Мышь книги и «?» — опросом (узлы импортёра — `UImage`, события 8/0xB считаем сами, `FAsdaMouseFrame`).
	if (!bNoticeShown && !bBookOpen) { BookMouse.Reset(); BookHoverCtl = BookPressCtl = -1; return; }
	if (!EnsureBookWidgets()) { return; }
	BookMouse.Update();
	// Контрол под курсором: 0 — «?» 69801, 1…3 — ответы 69206…69208.
	int32 Ctl = -1;
	if (bNoticeShown && NoticeClick.IsValid() && AsdaUi::IsCursorOverWidget(NoticeClick.Get())) { Ctl = 0; }
	if (bBookOpen)
	{
		for (int32 I = 0; I < 3 && Ctl < 0; ++I)
		{
			if (BookAnswerHit[I].IsValid() && AsdaUi::IsCursorOverWidget(BookAnswerHit[I].Get())) { Ctl = I + 1; }
		}
	}
	// Событие 8 (курсор вошёл на контрол окна): подсветка гаснет на ЛЮБОМ контроле; ответ с переходом — подсветить.
	if (Ctl != BookHoverCtl)
	{
		BookHoverCtl = Ctl;
		if (bBookOpen && BookHighlight.IsValid()) { BookHighlight->SetVisibility(ESlateVisibility::Collapsed); }
		if (bBookOpen && Ctl >= 1)
		{
			const FAsdaTutorialPage* P = (Sections.IsValidIndex(BookCategory) && Sections[BookCategory].Pages.IsValidIndex(BookPage))
				? &Sections[BookCategory].Pages[BookPage] : nullptr;
			const int32 J = Ctl - 1;
			if (P && P->NextPage.IsValidIndex(J) && P->NextPage[J] != -1)
			{
				BookHover = J;
				if (BookHighlight.IsValid()) { BookHighlight->SetVisibility(ESlateVisibility::SelfHitTestInvisible); }
				RefreshPage();
			}
		}
	}
	if (BookMouse.bLmbDown) { BookPressCtl = Ctl; }
	if (!BookMouse.bLmbUp) { return; }
	const int32 Pressed = BookPressCtl;
	BookPressCtl = -1;
	if (Pressed < 0 || Pressed != Ctl) { return; }   // событие 0xB — отпускание на том же контроле
	if (Pressed == 0) { OpenBook(); return; }
	// Клик по ответу: `this[50] = переход`, обновить страницу (переход −1 — ответа нет, ничего).
	const FAsdaTutorialPage* P = (Sections.IsValidIndex(BookCategory) && Sections[BookCategory].Pages.IsValidIndex(BookPage))
		? &Sections[BookCategory].Pages[BookPage] : nullptr;
	const int32 J = Pressed - 1;
	if (bBookOpen && P && P->NextPage.IsValidIndex(J) && P->NextPage[J] != -1)
	{
		BookPage = P->NextPage[J];
		RefreshPage();
	}
}
