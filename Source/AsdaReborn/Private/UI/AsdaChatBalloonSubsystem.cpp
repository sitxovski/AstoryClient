// Copyright (c) 2026 @sitxovski (Discord)
// UAsdaChatBalloonSubsystem — реализация облачка чата над головой. Полный разбор эталона,
//   адреса функций и все константы — в шапке Public/UI/AsdaChatBalloonSubsystem.h.
#include "UI/AsdaChatBalloonSubsystem.h"
#include "AsdaDevConsole.h"   // ★ античит А2: ASDA_DEV_CVAR / ASDA_DEV_CONSOLE — дев-ключи и команды вне Shipping
#include "UI/AsdaUiHelpers.h"   // ★ D3: единая проекция над-головного UI (дедуп 2026-08-08)
#include "World/AsdaOverheadLate.h"   // ★ 30.10.2026: облачко — по камере этого кадра (подписка «после камеры»)

#include "AsdaNetSubsystem.h"
#include "Blueprint/UserWidget.h"
#include "Blueprint/WidgetLayoutLibrary.h"
#include "Components/CanvasPanel.h"
#include "Components/CanvasPanelSlot.h"
#include "Components/Image.h"
#include "Components/TextBlock.h"
#include "Engine/Engine.h"
#include "Engine/GameInstance.h"
#include "Engine/World.h"
#include "Fonts/FontMeasure.h"
#include "Framework/Application/SlateApplication.h"
#include "Rendering/SlateRenderer.h"   // ★ 26.10.2026: GetFontMeasureService — высота строки шрифта облачка
#include "HAL/IConsoleManager.h"
#include "GameFramework/Actor.h"
#include "GameFramework/PlayerController.h"
#include "Kismet/GameplayStatics.h"
#include "UI/AsdaChatWindowSubsystem.h"   // игнор-лист (аналог CChat+0x738)
#include "Npc/AsdaMobActor.h"
#include "Npc/AsdaNpcActor.h"
#include "World/AsdaNameplateComponent.h"
#include "World/AsdaPlayerCharacter.h"   // ★ 24.10.2026: подъём тела клипом прыжка (GetOverheadLiftZ)
#include "World/AsdaWorldSubsystem.h"

DEFINE_LOG_CATEGORY_STATIC(LogAsdaTalkBox, Log, All);

// ★★★ ОБВОДКА (двойная прорисовка глифа) В ОБЛАЧКЕ — ВЫКЛЮЧЕНА ПО ТРЕБОВАНИЮ ЮЗЕРА (2026-08-06).
//   Эталон при a6 == 1 ставит стиль шрифта 0x200000, и рендер выводит каждый глиф ДВАЖДЫ —
//   на penX+1 и penX−1 (font_text.c:6832-6886). В порте это читалось как ненужная обводка.
//   Значение 1 возвращает эталонное поведение прямо в игре, без пересборки:
//     asda.Chat.BalloonBold 1     (постоянно — DefaultEngine.ini, [SystemSettings])
// ★★★ ОБВОДКА ШРИФТА В ОБЛАЧКЕ — ВЫКЛЮЧЕНА ПО ТРЕБОВАНИЮ ЮЗЕРА (2026-08-06, вторая жалоба:
//   «обводка ВСЁ ЕЩЁ видна»). Она приходит НЕ из стиля 0x200000, а из САМОГО АССЕТА: редакторный
//   хелпер `UAsdaUmgBuilder::ApplyTextOutline` запёк `FontOutlineSettings` во все текстовые
//   контролы всех трёх окон-облачек. Снимается в рантайме (см. ShowBalloon), поэтому переживает
//   переимпорт JUI. 1 — вернуть обводку (OutlineSize = 1).
static ASDA_DEV_CVAR(int32, GAsdaBalloonOutline,
	TEXT("asda.Chat.BalloonOutline"), 0,
	TEXT("Облачко чата: 0 = без обводки шрифта (умолчание, требование юзера); ")
	TEXT("1 = чёрная обводка OutlineSize=1, как запечено в WBP импортёром."),
	ECVF_Default);

static ASDA_DEV_CVAR(int32, GAsdaBalloonBold,
	TEXT("asda.Chat.BalloonBold"), 0,
	TEXT("Облачко чата: 1 = эталонная двойная прорисовка глифа (стиль 0x200000, «обводка»), ")
	TEXT("0 = выключена по требованию юзера (умолчание)."),
	ECVF_Default);

// ★★★ 26.10.2026 [ОРИГИНАЛ] СВОЁ ОБЛАЧКО ОТРЯДА/АРМИИ (баг игроков «party chat bubbles fail to display above your own
//   character»). 5106 и 6719 адресуют персонажа ПО ИМЕНИ (`CharManager_FindByName@0x9D13F0`); свой персонаж в корзинах
//   CharManager есть (`questCreateAndRegister@0x9D1490`), сервер шлёт 5106 и отправителю (`sub_451210`, GameServer.c:32675).
//   Порт искал по имени только среди соседей — над собой облачка не было. 1 — как оригинал (своё имя → свой павн).
static ASDA_DEV_CVAR(int32, GAsdaBalloonSelfByName,
	TEXT("asda.Chat.BalloonSelfByName"), 1,
	TEXT("Облачко по имени (5106 отряд, 6719 армия): 1 = как оригинал, своё имя находит своего персонажа (облачко над собой); ")
	TEXT("0 = прежний порт (только соседи — над собой облачка нет)."),
	ECVF_Default);

// ★★★ 26.10.2026 [ОРИГИНАЛ] ВЕРТИКАЛЬНАЯ ЦЕНТРОВКА БЛОКА СТРОК (жалоба владельца «первая строка пустая, текст ниже»).
//   `textFont_DrawRichText@0x12C9590`: при бите 0x4 top = (верх+низ)>>1 − высота_шрифта·число_строк/2, при 0x8
//   top = низ − высота_шрифта·число_строк — центруется ВЕСЬ блок. Порт брал одну строку: 2 строки уезжали на полстроки,
//   3 — на целую. 1 — как оригинал (высота строки — реальная у шрифта контрола в UE); 0 — прежний сдвиг.
static ASDA_DEV_CVAR(int32, GAsdaBalloonVCenterBlock,
	TEXT("asda.Chat.BalloonVCenterBlock"), 1,
	TEXT("Облачко: 1 = вертикальная центровка всего блока строк, как оригинал (textFont_DrawRichText); ")
	TEXT("0 = прежний порт (центровка одной строки — многострочный текст уезжает вниз)."),
	ECVF_Default);

// ★★★ 26.10.2026 [ОРИГИНАЛ] ОБРЕЗКА С «...» ПО 580 px (`textTruncateWithEllipsis@0xBC6E60`, вызов в
//   `entityName_DrawOverhead@0xBAD1B0`, entity_char.c:44425) — только облачка ПЕРСОНАЖЕЙ (у моба `uiDrawEntityNamePlate`
//   обрезки нет). 1 — как оригинал; 0 — без обрезки (прежний порт: окно растёт без предела).
static ASDA_DEV_CVAR(int32, GAsdaBalloonTruncate,
	TEXT("asda.Chat.BalloonTruncate"), 1,
	TEXT("Облачко персонажа: 1 = обрезка текста шире 580 px с «...», как оригинал (textTruncateWithEllipsis); 0 = без обрезки."),
	ECVF_Default);

// ★★ 26.10.2026 [ОРИГИНАЛ] ПРОБЕЛ В НАЧАЛЕ СТРОКИ-ПРОДОЛЖЕНИЯ: перенос оставляет его в данных, рендер оригинала
//   (`textFont_DrawRichText`, ветка '\n') один пробел после перевода строки пропускает. 1 — как оригинал; 0 — прежний отступ.
static ASDA_DEV_CVAR(int32, GAsdaBalloonSkipWrapSpace,
	TEXT("asda.Chat.BalloonSkipWrapSpace"), 1,
	TEXT("Облачко: 1 = после переноса строки один ведущий пробел не рисуется, как оригинал (textFont_DrawRichText); ")
	TEXT("0 = прежний порт (вторая строка с отступом в пробел)."),
	ECVF_Default);

// Защищённые поля UTextLayoutWidget, читаемые отражением (тела — у LineHeightOf).
static float AsdaTextLineHeightPct(const UTextBlock* T);
static FMargin AsdaTextMargin(const UTextBlock* T);

// ★ ЗАКОН ui-srgb-gamma-rule: dword'ы эталона и Diffuse из JuiSkin заданы в sRGB; UE трактует
//   FLinearColor как линейный и сам гамма-корректирует ⇒ без конверсии чёрный текст станет серым.
static FLinearColor AsdaTalkBoxColor(const FColor& C)
{
	return FLinearColor::FromSRGBColor(C);
}

UAsdaChatBalloonSubsystem::UAsdaChatBalloonSubsystem()
{
	// Три импортированных 1:1 из JUI виджета (Tools/asda_jui_generic_umg.py). Имена контролов
	//   генератор строит из idc: девять планов «<idc>_0_<i>», хвостик «<idc хвоста>_1_0», текст «<idc текста>».
	Templates.SetNum(3);
	Templates[0].WidgetClassPath = TEXT("/Game/AsdaAssets/UI/Player_UI/Quest/WBP_TALKBOXW_WIN.WBP_TALKBOXW_WIN_C");
	Templates[0].PanelPrefix     = TEXT("TALKBOXW_WIN");
	Templates[0].TailName        = FName(TEXT("TALKBOXW_TAB_1_0"));
	Templates[0].TextName        = FName(TEXT("TALKBOXW_TXT"));

	Templates[1].WidgetClassPath = TEXT("/Game/AsdaAssets/UI/Player_UI/Quest/WBP_TALKBOXB_WIN.WBP_TALKBOXB_WIN_C");
	Templates[1].PanelPrefix     = TEXT("TALKBOXB_WIN");
	Templates[1].TailName        = FName(TEXT("TALKBOXB_TAB_1_0"));
	Templates[1].TextName        = FName(TEXT("TALKBOXB_TXT"));

	Templates[2].WidgetClassPath = TEXT("/Game/AsdaAssets/UI/Player_UI/Quest/WBP_TALKBOX_Mon_WIN.WBP_TALKBOX_Mon_WIN_C");
	Templates[2].PanelPrefix     = TEXT("TALKBOX_Mon_WIN");
	Templates[2].TailName        = FName(TEXT("TALKBOX_Mon_Tag_1_0"));
	Templates[2].TextName        = FName(TEXT("TALKBOX_Mon_Txt"));
}

bool UAsdaChatBalloonSubsystem::ShouldCreateSubsystem(UObject* Outer) const
{
	const UWorld* W = Cast<UWorld>(Outer);
	return W && W->IsGameWorld();
}

void UAsdaChatBalloonSubsystem::Initialize(FSubsystemCollectionBase& Collection)
{
	Super::Initialize(Collection);
	// ★★★ [ОРИГИНАЛ] 30.10.2026 (asda.Overhead.LateProject): облачка — после обновления камеры этого кадра (только свой мир).
	HPostCamera = AsdaOverheadLate::AddPostCamera([this](UWorld* W, float /*Dt*/)
	{
		if (W == GetWorld() && AsdaOverheadLate::IsOn()) { TickBalloons(); }
	});
	UGameInstance* GI = GetWorld() ? GetWorld()->GetGameInstance() : nullptr;
	UAsdaNetSubsystem* Net = GI ? GI->GetSubsystem<UAsdaNetSubsystem>() : nullptr;
	if (!Net) { return; }

	// ★ ЗАКОН no-reentrant-net-callbacks: колбэк вызывается ИЗ FAsdaConnection::DrainFrames —
	//   здесь только кладём заявку; поиск актора и создание виджетов идут в Tick.
	HChatLine = Net->OnChatLine.AddLambda([this](const UAsdaNetSubsystem::FAsdaChatLineIn& L)
	{
		if (L.BalloonVariant < 0 || L.BalloonVariant > 2) { return; }
		// ★★★ ИГНОР-ЛИСТ (эталон `Recv_ChatNormal`, network.c:10404-10409): при попадании
		//   отправителя в игнор ВЕСЬ блок обработки лежит в ветке `else`, поэтому пропадает не
		//   только строка в ленте, но и ОБЛАЧКО НАД ГОЛОВОЙ (`entityName_DrawOverhead`).
		//   Список живёт в UAsdaChatWindowSubsystem (наш аналог CChat+0x738); фильтруются
		//   ровно те же опкоды, что и в чате.
		//   ⚠⚠⚠ ОТСТУПЛЕНИЕ (Д27 п.2, требование юзера): игнор распространён на ВСЕ входящие
		//   чат-опкоды, поэтому облачко заблокированного игрока не появляется НИ ОТ ОДНОГО
		//   канала (в эталоне гасилось только у 5085/6561). Условие авторства — то же
		//   `bSenderAuthored`, что и у строки в ленте: облачко от эха своего шёпота остаётся.
		if (L.bSenderAuthored && UAsdaChatWindowSubsystem::IgnoreFiltersOpcode(L.SrcOpcode)
			&& !L.SenderName.IsEmpty())
		{
			UGameInstance* GIn = GetWorld() ? GetWorld()->GetGameInstance() : nullptr;
			const UAsdaChatWindowSubsystem* CW = GIn ? GIn->GetSubsystem<UAsdaChatWindowSubsystem>() : nullptr;
			if (CW && CW->IsChatIgnored(L.SenderName)) { return; }
		}
		FAsdaPendingBalloon P;
		P.Kind        = (EAsdaTalkBox)(uint8)L.BalloonVariant;
		P.Text        = L.BalloonText;
		P.Color       = L.BalloonColor;
		P.bBold       = (L.BalloonFontFlag == 1);
		P.UserIdx     = L.SenderUserIdx;
		P.CharId      = L.SenderCharId;
		// 6719/5106 адресуют персонажа ПО ИМЕНИ (CharManager_FindByName), а не по индексу.
		if (L.SrcOpcode == 6719 || L.SrcOpcode == 5106) { P.ByName = L.SenderName; P.UserIdx = -1; }
		Pending.Push(MoveTemp(P));
	});
	HMobBubble = Net->OnMobChatBubble.AddLambda([this](int32 Rid, const FString& Text)
	{
		FAsdaPendingBalloon P;
		P.Kind   = EAsdaTalkBox::Mon;
		P.MobRid = Rid;
		P.Text   = Text;
		P.Color  = FColor(0xFF, 0x37, 0x28, 0xFF);   // −51416, эталон recvCharChatBubble
		P.bBold  = false;                            // uiDrawEntityNamePlate передаёт a6 = 0
		Pending.Push(MoveTemp(P));
	});
}

void UAsdaChatBalloonSubsystem::Deinitialize()
{
	UGameInstance* GI = GetWorld() ? GetWorld()->GetGameInstance() : nullptr;
	if (UAsdaNetSubsystem* Net = GI ? GI->GetSubsystem<UAsdaNetSubsystem>() : nullptr)
	{
		if (HChatLine.IsValid())  { Net->OnChatLine.Remove(HChatLine); }
		if (HMobBubble.IsValid()) { Net->OnMobChatBubble.Remove(HMobBubble); }
	}
	HChatLine.Reset();
	HMobBubble.Reset();
	AsdaOverheadLate::RemovePostCamera(HPostCamera);
	for (FAsdaBalloonInst& B : Balloons)
	{
		if (B.Widget) { B.Widget->RemoveFromParent(); }
	}
	Balloons.Reset();
	Pending.Clear();
	Super::Deinitialize();
}

// ── ПОИСК ВЛАДЕЛЬЦА ЗАЯВКИ (эталонные CharManager_FindByUserIdx / ByCharId / ByName / ByRenderId) ──
void UAsdaChatBalloonSubsystem::ResolveAndShow(const FAsdaPendingBalloon& P)
{
	UWorld* W = GetWorld();
	UGameInstance* GI = W ? W->GetGameInstance() : nullptr;
	UAsdaWorldSubsystem* Wld = GI ? GI->GetSubsystem<UAsdaWorldSubsystem>() : nullptr;
	if (!Wld) { return; }

	AActor* Owner = nullptr;
	if (P.MobRid >= 0)
	{
		AAsdaMobActor* M = Wld->FindNetMobByRid(P.MobRid);
		// Эталон uiDrawEntityNamePlate: гейт `*(ent+480) != 10` — над МЁРТВЫМ мобом облачка нет.
		if (M && !M->IsDead()) { Owner = M; }
	}
	else if (!P.ByName.IsEmpty())
	{
		// ★ 26.10.2026 [ОРИГИНАЛ]: `CharManager_FindByName` обходит корзины, где лежит и свой персонаж (`strcmp` —
		//   с учётом регистра) ⇒ своё имя даёт своего павна. Разбор — у ключа asda.Chat.BalloonSelfByName.
		AActor* Self = Wld->LocalPlayerActor.Get();
		if (GAsdaBalloonSelfByName.GetValueOnGameThread() != 0 && ::IsValid(Self)
			&& Wld->GetSelfCharName().Equals(P.ByName, ESearchCase::CaseSensitive))
		{
			Owner = Self;
		}
		else
		{
			Owner = Wld->FindPlayerActorByName(P.ByName);
		}
	}
	else if (P.CharId > 0)
	{
		Owner = Wld->FindPlayerActorByCharId(P.CharId);
	}
	else if (P.UserIdx >= 0)
	{
		Owner = Wld->ActorByUserIdx(P.UserIdx);
	}
	if (!Owner)
	{
		// 1:1: entityName_DrawOverhead начинается с `if (g_bShowEntityNames && a3)` — нет сущности,
		//   нет и облачка (эталон при этом НЕ падает и ничего не делает).
		UE_LOG(LogAsdaTalkBox, Verbose,
			TEXT("[облачко] владелец не найден (uidx=%d charId=%d имя=«%s» rid=%d) — эталон облачко не рисует"),
			P.UserIdx, P.CharId, *P.ByName, P.MobRid);
		return;
	}
	// ★ 26.10.2026 [ОРИГИНАЛ]: облачко ПЕРСОНАЖА (`entityName_DrawOverhead`) режет текст по 580 px с «...»; у моба
	//   (`uiDrawEntityNamePlate`, заявка 8008) обрезки нет. Шрифт для замера — из экземпляра облачка нужного варианта.
	FString Shown = P.Text;
	if (P.MobRid < 0 && GAsdaBalloonTruncate.GetValueOnGameThread() != 0)
	{
		if (FAsdaBalloonInst* B = AcquireBalloon(Owner, P.Kind))
		{
			Shown = TruncateWithFont(*B, P.Text);
		}
	}
	ShowBalloon(Owner, P.Kind, Shown, P.Color, P.bBold);
}

// ── ОБРЕЗКА С «...» (эталонный textTruncateWithEllipsis @0xBC6E60, посимвольно) ─────────────────────────────────
//   if (ширина(Str) < a3) return Str;  иначе копить ширины по символу, пока сумма+следующий не превысит a3;
//   копируется Size−1 символов (при DBCS — Size−2; у нас однобайтовый режим) и дописывается «...».
//   Если перебор так и не случился — строка возвращается как есть.
FString UAsdaChatBalloonSubsystem::TruncateWithFont(const FAsdaBalloonInst& B, const FString& In) const
{
	const float Limit = (float)TruncateWidthPx;
	if (In.IsEmpty() || MeasureWidth(B, In) < Limit) { return In; }
	float Acc = 0.f;
	for (int32 Size = 0; Size < In.Len(); ++Size)
	{
		const float Cw = MeasureWidth(B, In.Mid(Size, 1));
		if (Cw + Acc > Limit)
		{
			const int32 Keep = FMath::Max(Size - 1, 0);
			return In.Left(Keep) + TEXT("...");
		}
		Acc += Cw;
	}
	return In;
}

FString UAsdaChatBalloonSubsystem::TruncateLikeOriginal(EAsdaTalkBox Kind, const FString& In) const
{
	for (const FAsdaBalloonInst& B : Balloons)
	{
		if (B.Kind == Kind && B.Text) { return TruncateWithFont(B, In); }
	}
	return In;
}

bool UAsdaChatBalloonSubsystem::DebugGetBalloon(const AActor* Owner, EAsdaTalkBox Kind, FAsdaBalloonDebugInfo& Out) const
{
	Out = FAsdaBalloonDebugInfo();
	for (const FAsdaBalloonInst& B : Balloons)
	{
		if (B.Owner.Get() != Owner || B.Kind != Kind) { continue; }
		Out.bFound = true;
		Out.bActive = B.bActive;
		Out.bOnScreen = B.Widget && B.Widget->GetVisibility() != ESlateVisibility::Collapsed
			&& B.Widget->GetVisibility() != ESlateVisibility::Hidden;
		Out.Opacity = B.Widget ? B.Widget->GetRenderOpacity() : 0.f;
		Out.WorldPt = B.LastWorldPt;
		Out.Widget = B.Widget;
		Out.Kind = B.Kind;
		Out.Text = B.ShownText;
		Out.Color = B.TextColorSrgb;
		Out.PanelSize = B.PanelSize;
		Out.TextAlign = B.TextAlign;
		Out.LineCount = B.LineCount;
		if (B.Text)
		{
			if (const UCanvasPanelSlot* S = AsdaUi::SlotOf(B.Text)) { Out.TextPos = S->GetPosition(); Out.TextSlotSize = S->GetSize(); }
			Out.TextDesired = B.Text->GetDesiredSize();
			Out.UeLineHeight = LineHeightOf(B);
			Out.TextMargin = AsdaTextMargin(B.Text);
			Out.LineHeightPct = AsdaTextLineHeightPct(B.Text);
		}
		return true;
	}
	return false;
}

// Защищённое поле UTextLayoutWidget (геттера у движка нет) — читаем отражением: LineHeightPercentage.
static float AsdaTextLineHeightPct(const UTextBlock* T)
{
	static const FFloatProperty* P = FindFProperty<FFloatProperty>(UTextLayoutWidget::StaticClass(), TEXT("LineHeightPercentage"));
	return (T && P) ? P->GetPropertyValue_InContainer(T) : 1.f;
}

// Защищённое поле UTextLayoutWidget — Margin (поля вокруг текста) отражением.
static FMargin AsdaTextMargin(const UTextBlock* T)
{
	static const FStructProperty* P = FindFProperty<FStructProperty>(UTextLayoutWidget::StaticClass(), TEXT("Margin"));
	if (!T || !P || P->Struct != TBaseStructure<FMargin>::Get()) { return FMargin(); }
	return *P->ContainerPtrToValuePtr<FMargin>(T);
}

// Высота строки шрифта контрола текста в UE (то, на что реально растёт блок строк): FontMeasure × LineHeightPercentage.
float UAsdaChatBalloonSubsystem::LineHeightOf(const FAsdaBalloonInst& B) const
{
	if (!B.Text || !FSlateApplication::IsInitialized()) { return (float)FontLineHeight; }
	const FSlateFontInfo Font = B.Text->GetFont();
	if (!Font.HasValidFont()) { return (float)FontLineHeight; }
	const TSharedRef<FSlateFontMeasure> FM = FSlateApplication::Get().GetRenderer()->GetFontMeasureService();
	// Высота ОДНОЙ строки так, как её раскладывает текстовый слой Slate (на неё растёт TextBlock): замер «Ag».
	const float H = (float)FM->Measure(TEXT("Ag"), Font, 1.f).Y * AsdaTextLineHeightPct(B.Text);
	return H > 0.f ? H : (float)FontLineHeight;
}

// ── ЕДИНАЯ ТОЧКА ПОКАЗА (эталонный uiShowTooltipPlate @0xF228E0) ───────────────────────────────
void UAsdaChatBalloonSubsystem::ShowBalloon(AActor* Owner, EAsdaTalkBox Kind, const FString& Text,
	FColor Color, bool bBold)
{
	if (!bShowBalloons || !::IsValid(Owner) || Text.IsEmpty()) { return; }
	FAsdaBalloonInst* B = AcquireBalloon(Owner, Kind);
	if (!B || !B->Widget || !B->Text) { return; }

	// a7 → CClass_SetField926: 0 ⇒ 0x100000 (в рендере глифа никак не проверяется = обычная прорисовка),
	//   1 ⇒ 0x200000 — КАЖДЫЙ глиф выводится ДВАЖДЫ: сначала на penX+1, потом на penX−1
	//   (font_text.c:6832-6886). Воспроизводим буквально: вторая копия строки, сдвинутая на +2 px,
	//   при этом основная уезжает на −1 px (см. LayoutBalloon).
	// ★★★ ОТСТУПЛЕНИЕ ОТ ОРИГИНАЛА (прямое требование юзера 2026-08-06) — НЕ «ЧИНИТЬ» ОБРАТНО.
	//   Юзер: «отображается заебись, только обводка там не нужна». Речь о стиле 0x200000: эталон
	//   рисует КАЖДЫЙ глиф ДВАЖДЫ (penX+1 и penX−1, font_text.c:6832-6886), что на тонком шрифте
	//   порта читается как обводка/двоение. Механизм воспроизведения ОСТАВЛЕН целиком (вторая
	//   копия строки TextBold и её раскладка) и включается одной консольной переменной —
	//   `asda.Chat.BalloonBold 1` возвращает эталонное поведение без пересборки.
	B->bBold = bBold && (GAsdaBalloonBold.GetValueOnGameThread() != 0);

	// ★★★ ЖИВОЙ ТЕСТ 2026-08-06: «обводка ВСЁ ЕЩЁ видна» — хотя двойная прорисовка (0x200000)
	//   уже была выключена. РАЗБОР ПО ФАКТУ (не по коду): обводка идёт НЕ отсюда, а ЗАПЕЧЕНА
	//   В САМОМ АССЕТЕ. В `WBP_TALKBOXW_WIN` / `WBP_TALKBOXB_WIN` / `WBP_TALKBOX_Mon_WIN` у
	//   текстовых блоков лежит непустая `FSlateFontInfo::OutlineSettings` — её массово проставил
	//   редакторный хелпер `UAsdaUmgBuilder::ApplyTextOutline` («чёрная обводка текста, как в
	//   оригинале у над-головных плашек», AsdaUmgBuilder.cpp:505). Проверено по строкам ассетов:
	//   во всех трёх присутствуют `FontOutlineSettings`/`OutlineSize`.
	//   ⇒ Снимаем обводку В РАНТАЙМЕ, у ОБЕИХ копий строки и на КАЖДЫЙ показ: так это работает
	//   независимо от того, что лежит в ассете, и переживает переимпорт JUI (иначе обводка
	//   вернулась бы при следующем прогоне импортёра — грабля tools-no-import-side-effects).
	//   ★ Сделано ДО WrapAndResize: перенос и размеры окна меряются тем же шрифтом.
	//   Вернуть эталонную обводку без пересборки: `asda.Chat.BalloonOutline 1`.
	//   ★ 2026-08-20: своя копия снятия обводки убрана — теперь это ОБЩАЯ ТОЧКА
	//   `AsdaUi::SetTextOutline` (D4b), её же зовёт окно разговора с NPC. Поведение прежнее.
	const int32 WantOutlinePx = (GAsdaBalloonOutline.GetValueOnGameThread() != 0) ? 1 : 0;
	AsdaUi::SetTextOutline(B->Text,     WantOutlinePx);
	AsdaUi::SetTextOutline(B->TextBold, WantOutlinePx);
	// Цвет ТЕКСТА — CControl_SetField923(pText, a4).
	B->TextColorSrgb = Color;   // ★ 26.10.2026: для дампа/самотеста — как передал вызывающий
	const FLinearColor TextCol = AsdaTalkBoxColor(Color);
	B->Text->SetColorAndOpacity(FSlateColor(TextCol));
	if (B->TextBold) { B->TextBold->SetColorAndOpacity(FSlateColor(TextCol)); }

	// Перенос + пересчёт размеров + постановка строки (uiWrapTooltipText) и раскладка (uiTargetPlateLayoutCenter).
	WrapAndResize(*B, Text);
	LayoutBalloon(*B);

	// t0 = now; bActive = 1; bSuppressed = 0; SetVisible(окно, 1).
	B->ShowTimeS   = GetWorld() ? GetWorld()->GetTimeSeconds() : 0.0;
	B->bActive     = true;
	B->bSuppressed = false;
	B->Widget->SetVisibility(ESlateVisibility::HitTestInvisible);
	UE_LOG(LogAsdaTalkBox, Log, TEXT("[облачко] %s вариант=%d «%s» окно=%.0f×%.0f выр.=%d"),
		*Owner->GetName(), (int32)Kind, *Text, B->PanelSize.X, B->PanelSize.Y, B->TextAlign);
}

void UAsdaChatBalloonSubsystem::HideBalloonsFor(AActor* Owner)
{
	for (FAsdaBalloonInst& B : Balloons)
	{
		if (B.Owner.Get() != Owner) { continue; }
		// uiTooltipPlateHide @0xF22800: bActive=0, bSuppressed=0, t0=0, всё скрыто.
		B.bActive = false;
		B.bSuppressed = false;
		B.ShowTimeS = -1.0;
		if (B.Widget) { B.Widget->SetVisibility(ESlateVisibility::Collapsed); }
	}
}

// ── СОЗДАНИЕ/ПОИСК ЭКЗЕМПЛЯРА (эталонный uiWnd_BuildTripleChildWindow @0xF22A50) ───────────────
FAsdaBalloonInst* UAsdaChatBalloonSubsystem::AcquireBalloon(AActor* Owner, EAsdaTalkBox Kind)
{
	for (FAsdaBalloonInst& B : Balloons)
	{
		if (B.Owner.Get() == Owner && B.Kind == Kind) { return &B; }
	}
	const int32 Idx = (int32)Kind;
	if (!Templates.IsValidIndex(Idx)) { return nullptr; }
	const FAsdaTalkBoxTemplate& T = Templates[Idx];

	UWorld* W = GetWorld();
	APlayerController* PC = W ? UGameplayStatics::GetPlayerController(W, 0) : nullptr;
	UClass* Cls = LoadClass<UUserWidget>(nullptr, *T.WidgetClassPath);
	if (!PC || !Cls)
	{
		UE_LOG(LogAsdaTalkBox, Warning, TEXT("[облачко] нет виджета «%s» или контроллера"), *T.WidgetClassPath);
		return nullptr;
	}
	UUserWidget* Wdg = CreateWidget<UUserWidget>(PC, Cls);
	if (!Wdg) { return nullptr; }
	Wdg->AddToViewport(ViewportZOrder);
	Wdg->SetVisibility(ESlateVisibility::Collapsed);

	FAsdaBalloonInst New;
	New.Owner  = Owner;
	New.Kind   = Kind;
	New.Widget = Wdg;
	New.Slices.SetNum(9);
	for (int32 i = 0; i < 9; ++i)
	{
		const FName N(*FString::Printf(TEXT("%s_0_%d"), *T.PanelPrefix, i));
		New.Slices[i] = Cast<UImage>(Wdg->GetWidgetFromName(N));
		if (!New.Slices[i])
		{
			UE_LOG(LogAsdaTalkBox, Warning, TEXT("[облачко] в «%s» нет плана «%s»"), *T.WidgetClassPath, *N.ToString());
		}
	}
	New.Tail = Cast<UImage>(Wdg->GetWidgetFromName(T.TailName));
	New.Text = Cast<UTextBlock>(Wdg->GetWidgetFromName(T.TextName));
	// Вторая копия строки для стиля 0x200000 (двойная прорисовка глифа со сдвигом ±1 px).
	if (New.Text)
	{
		if (UCanvasPanelSlot* TS = AsdaUi::SlotOf(New.Text))
		{
			if (UCanvasPanel* Canvas = Cast<UCanvasPanel>(New.Text->GetParent()))
			{
				UTextBlock* Copy = NewObject<UTextBlock>(Wdg, UTextBlock::StaticClass(),
					FName(*(T.TextName.ToString() + TEXT("_Bold2"))));
				Copy->SetFont(New.Text->GetFont());
				Copy->SetAutoWrapText(false);
				Copy->SetVisibility(ESlateVisibility::Collapsed);
				if (UCanvasPanelSlot* CS = Canvas->AddChildToCanvas(Copy))
				{
					CS->SetAnchors(TS->GetAnchors());
					CS->SetAlignment(TS->GetAlignment());
					CS->SetZOrder(TS->GetZOrder());
				}
				New.TextBold = Copy;
			}
		}
	}

	// Снимаем «шаблонные» размеры прямо из WBP (аналог полей +16/+20/+24/+28 объекта эталона):
	//   угол = размер плана 0 (левый-верхний) и плана 8 (правый-нижний), хвостик — размер своего слота.
	auto SlotSize = [](UWidget* Wg, const FVector2D& Def) -> FVector2D
	{
		if (Wg)
		{
			if (const UCanvasPanelSlot* S = AsdaUi::SlotOf(Wg)) { return S->GetSize(); }
		}
		return Def;
	};
	New.CornerTL = SlotSize(New.Slices[0], FVector2D(9.f, 9.f));
	New.CornerBR = SlotSize(New.Slices[8], FVector2D(9.f, 9.f));
	New.TailSize = SlotSize(New.Tail, FVector2D(23.f, 19.f));

	// Все контролы облачка — мышь не ловят (JUI: NoSelect = 1 у всех трёх окон).
	for (UImage* I : New.Slices) { if (I) { I->SetVisibility(ESlateVisibility::HitTestInvisible); } }
	if (New.Tail) { New.Tail->SetVisibility(ESlateVisibility::HitTestInvisible); }
	if (New.Text) { New.Text->SetVisibility(ESlateVisibility::HitTestInvisible); New.Text->SetAutoWrapText(false); }

	Balloons.Add(MoveTemp(New));
	return &Balloons.Last();
}

float UAsdaChatBalloonSubsystem::MeasureWidth(const FAsdaBalloonInst& B, const FString& Line) const
{
	if (Line.IsEmpty() || !B.Text || !FSlateApplication::IsInitialized()) { return 0.f; }
	const FSlateFontInfo Font = B.Text->GetFont();
	if (!Font.HasValidFont()) { return 0.f; }
	// ★ СВЕДЕНО (`fix_helpers_dedup.md`): общий замер `AsdaUi::MeasureTextWidth` (гварды выше уже
	//   гарантируют инициализацию Slate и валидный шрифт, поэтому Fallback недостижим).
	return AsdaUi::MeasureTextWidth(Line, Font, 0.f);
}

// ── ПЕРЕНОС СТРОК + РАЗМЕРЫ (uiWrapTooltipText @0xF22D10, посимвольно 1:1) ─────────────────────
void UAsdaChatBalloonSubsystem::WrapAndResize(FAsdaBalloonInst& B, const FString& InText)
{
	// 1) отбросить ведущие пробелы (`for (Str = pInputText; *Str && *Str == 32; ++Str)`).
	int32 Start = 0;
	while (Start < InText.Len() && InText[Start] == TEXT(' ')) { ++Start; }
	const FString Src = InText.Mid(Start);
	const int32 TotalLen = Src.Len();          // totalLen — по нему выбирается выравнивание

	FString OutText;                            // outText — итоговая строка с '\n'
	FString CurLine;                            // curLine  — текущая (ещё не закрытая) строка
	FString LastBreakLine;                      // lastBreakLine — снимок curLine на последнем пробеле
	int32 BreakPos = INDEX_NONE;                // breakPos — индекс символа-пробела
	int32 BreakCol = 0;                         // breakColWidth
	bool  bHasBreak = false;                    // hasBreakPoint
	bool  bCanBreak = true;                     // canBreakHere (сбрасывается двухбайтовым символом)
	int32 LineCol = 0;                          // lineColWidth
	int32 LineCount = 0;                        // lineCount — сколько раз сработал перенос
	float MaxLineWidth = 0.f;                   // maxLineWidth (пиксели)
	int32 FontH = 0;                            // HIDWORD(lineWidthAndFontHeight): 12 при непустой строке

	int32 i = 0;
	while (i < Src.Len())
	{
		const TCHAR Ch = Src[i];
		// Ширина в «колонках»: 1 (ASCII) либо 2 (ведущий байт DBCS). При g_nClientLanguage == 11
		//   DBCS-ветка не включается вовсе ⇒ любая кириллица считается за 1 колонку.
		const bool bWide = (!bSingleByteLanguage && (uint32)Ch >= 0x80u);
		LineCol += bWide ? 2 : 1;
		CurLine.AppendChar(Ch);
		if (bWide) { bCanBreak = false; }
		++i;

		// Точка отката — ПРОБЕЛ, стоящий СЛЕДУЮЩИМ (эталон проверяет *Str уже после сдвига).
		if (bCanBreak && i < Src.Len() && Src[i] == TEXT(' '))
		{
			BreakPos = i;
			BreakCol = LineCol;
			LastBreakLine = CurLine;
			bHasBreak = true;
		}

		if (LineCol > WrapColumns)
		{
			if (bHasBreak)
			{
				// Курсор ставится НА пробел ⇒ следующая строка начинается с пробела (поведение оригинала).
				i = BreakPos;
				LineCol = BreakCol;
				CurLine = LastBreakLine;
			}
			bCanBreak = true;
			bHasBreak = false;
			CurLine.AppendChar(TEXT('\n'));
			LineCol = 0;
			++LineCount;
			const FString Measured = CurLine.Replace(TEXT("\n"), TEXT(""));
			const float LineW = MeasureWidth(B, Measured);
			// 1:1: lineWidthAndFontHeight ПЕРЕПИСЫВАЕТСЯ на каждом переносе целиком, а для пустой
			//   строки textFontMeasureLineWidth возвращает 0 в ОБЕИХ половинах ⇒ высота тоже 0.
			FontH = Measured.IsEmpty() ? 0 : FontLineHeight;
			MaxLineWidth = FMath::Max(MaxLineWidth, LineW);
			OutText += CurLine;
			CurLine.Reset();
		}
	}
	// Хвост после последнего переноса тоже участвует в maxLineWidth.
	if (!CurLine.IsEmpty())
	{
		MaxLineWidth = FMath::Max(MaxLineWidth, MeasureWidth(B, CurLine));
	}

	const int32 FinalTextLen = CurLine.Len();
	bool bNoTail = false;                       // isSingleLine эталона (мисномер: «хвоста нет»)
	float PanelW, PanelH, TextW, TextH;
	if (LineCount <= 0)
	{
		const float Wd = MeasureWidth(B, CurLine);
		if (Wd >= 20.f) { TextW = Wd + 8.f;  TextH = 20.f; PanelW = Wd + 21.f; }
		else            { TextW = 20.f;      TextH = 20.f; PanelW = 36.f;      }
		PanelH = 36.f;
	}
	else if (FinalTextLen > 0)
	{
		TextW  = MaxLineWidth + 8.f;
		TextH  = (float)(FontH * (LineCount + 1) + 8);
		PanelW = MaxLineWidth + 21.f;
		PanelH = (float)(FontH * (LineCount + 1) + 22);
	}
	else if (LineCount == 1)
	{
		TextW = MaxLineWidth + 4.f;  TextH = 20.f;
		PanelW = MaxLineWidth + 14.f; PanelH = 24.f;
		bNoTail = true;
	}
	else
	{
		TextW  = MaxLineWidth + 6.f;
		TextH  = (float)(LineCount * FontH + 8);
		PanelW = MaxLineWidth + 16.f;
		PanelH = (float)(LineCount * FontH + 22);
		bNoTail = true;
	}
	if (!bNoTail) { OutText += CurLine; }

	// Выравнивание текста (CControl_SetField924): 0x1 hCenter, 0x2 hRight, 0x4 vCenter, 0x8 vBottom.
	int32 Align;
	if (bNoTail)                                { Align = 0; }
	else if (TotalLen >= 18 && TotalLen <= 36)  { Align = 4; }
	else if (TotalLen >= 18)                    { Align = 0; }
	else                                        { Align = 5; }

	B.PanelSize = FVector2D(PanelW, PanelH);
	B.TextSize  = FVector2D(TextW, TextH);
	B.TextAlign = Align;
	// ★ 26.10.2026: число строк блока (для центровки всего блока, как `textFont_DrawRichText`): сегменты между '\n';
	//   пустой хвост после последнего '\n' (ветка «хвоста нет») строкой не считается — рендер его не рисует.
	{
		TArray<FString> Parts;
		OutText.ParseIntoArray(Parts, TEXT("\n"), /*InCullEmpty=*/false);
		while (Parts.Num() > 1 && Parts.Last().IsEmpty()) { Parts.Pop(); }
		B.LineCount = FMath::Max(Parts.Num(), 1);
	}
	// ★ 26.10.2026 [ОРИГИНАЛ] рендер `textFont_DrawRichText@0x12C9590` (ветка перевода строки, case 3/4): после '\n'
	//   ОДИН пробел пропускается (`if (Str[i+1] == 32) ++i`). Перенос ставит курсор НА пробел, и строка-продолжение в
	//   данных начинается с пробела — но на экране оригинала отступа нет. Ширина окна уже посчитана с пробелом (как у
	//   эталонного замера), убирается только показ. Ключ asda.Chat.BalloonSkipWrapSpace (0 — прежний отступ).
	if (GAsdaBalloonSkipWrapSpace.GetValueOnGameThread() != 0)
	{
		OutText = OutText.Replace(TEXT("\n "), TEXT("\n"));
	}
	B.ShownText = OutText;
	const ETextJustify::Type J = (Align & 0x1) ? ETextJustify::Center
	                           : (Align & 0x2) ? ETextJustify::Right
	                                           : ETextJustify::Left;
	if (B.Text)
	{
		B.Text->SetText(FText::FromString(OutText));
		B.Text->SetJustification(J);
	}
	if (B.TextBold)
	{
		B.TextBold->SetText(FText::FromString(OutText));
		B.TextBold->SetJustification(J);
	}
}

// ── РАСКЛАДКА: девять планов (uiPanel9SliceLayout @0x13005D0) + хвостик (uiTargetPlateLayoutCenter) ──
void UAsdaChatBalloonSubsystem::LayoutBalloon(FAsdaBalloonInst& B)
{
	const float W = B.PanelSize.X, H = B.PanelSize.Y;
	const float LW = B.CornerTL.X, TH = B.CornerTL.Y;   // левая колонка / верхняя строка
	const float RW = B.CornerBR.X, BH = B.CornerBR.Y;   // правая колонка / нижняя строка
	const float CW = FMath::Max(W - LW - RW, 0.f);      // центральная колонка (растягивается)
	const float CH = FMath::Max(H - TH - BH, 0.f);      // центральная строка

	// Порядок планов ResizeWindow: 0 1 2 / 3 4 5 / 6 7 8 (эталон копирует Rect'ы именно так).
	const FVector2D Pos[9] = {
		{0.f, 0.f},      {LW, 0.f},      {W - RW, 0.f},
		{0.f, TH},       {LW, TH},       {W - RW, TH},
		{0.f, H - BH},   {LW, H - BH},   {W - RW, H - BH},
	};
	const FVector2D Size[9] = {
		{LW, TH}, {CW, TH}, {RW, TH},
		{LW, CH}, {CW, CH}, {RW, CH},
		{LW, BH}, {CW, BH}, {RW, BH},
	};
	for (int32 i = 0; i < 9 && i < B.Slices.Num(); ++i)
	{
		UImage* I = B.Slices[i];
		if (!I) { continue; }
		if (UCanvasPanelSlot* S = AsdaUi::SlotOf(I))
		{
			S->SetPosition(Pos[i]);
			S->SetSize(Size[i]);
		}
		// Кисть тоже должна тянуться: у растягиваемых планов размер картинки = размеру слота.
		FSlateBrush Br = I->GetBrush();
		Br.SetImageSize(Size[i]);
		I->SetBrush(Br);
	}

	// Хвостик: (ширина_окна/2 − ширина_хвостика/2, высота_окна − 3) — uiTargetPlateLayoutCenter.
	if (B.Tail)
	{
		if (UCanvasPanelSlot* S = AsdaUi::SlotOf(B.Tail))
		{
			S->SetPosition(FVector2D(W * 0.5f - B.TailSize.X * 0.5f, H - TailOverlap));
			S->SetSize(B.TailSize);
		}
	}

	// Текст: Origin (7,7) от окна, размер (TextW, TextH). Вертикальная центровка — как в рендере
	//   контрола: при бите 0x4 строка рисуется со сдвигом (h − высота_шрифта)/2, при 0x8 — прижата вниз.
	if (B.Text)
	{
		float VY = 0.f;
		if (GAsdaBalloonVCenterBlock.GetValueOnGameThread() != 0)
		{
			// ★ 26.10.2026 [ОРИГИНАЛ] `textFont_DrawRichText@0x12C9590`: центруется ВЕСЬ блок —
			//   top = (верх+низ)>>1 − v53[612801]·строк/2, где v53[612801] = 12 (высота шрифта, ctor @AsdaStory_game.c:977064 —
			//   та же 12, что у измерителя `textFontMeasureLine`). Буквально: верх текста = 7 + 4 = 11 px при любом числе строк.
			//   (Шаг строк у оригинала 12+3 = 15 px — `v53[612802] = 3`; у шрифта порта шаг 14 px, разница в 1 px.)
			const float BlockH = (float)FontLineHeight * (float)B.LineCount;
			if (B.TextAlign & 0x4)      { VY = (B.TextSize.Y - BlockH) * 0.5f; }
			else if (B.TextAlign & 0x8) { VY = (B.TextSize.Y - BlockH); }
		}
		else
		{
			if (B.TextAlign & 0x4)      { VY = (B.TextSize.Y - (float)FontLineHeight) * 0.5f; }
			else if (B.TextAlign & 0x8) { VY = (B.TextSize.Y - (float)FontLineHeight); }
		}
		VY = FMath::Max(VY, 0.f);
		// Стиль 0x200000: две копии со сдвигом −1 и +1 px по X (эталон penX+1 / penX−1).
		const float DX = B.bBold ? 1.f : 0.f;
		if (UCanvasPanelSlot* S = AsdaUi::SlotOf(B.Text))
		{
			S->SetPosition(FVector2D(TextInset.X - DX, TextInset.Y + VY));
			S->SetSize(FVector2D(B.TextSize.X, B.TextSize.Y));
		}
		if (B.TextBold)
		{
			if (UCanvasPanelSlot* S2 = AsdaUi::SlotOf(B.TextBold))
			{
				S2->SetPosition(FVector2D(TextInset.X + DX, TextInset.Y + VY));
				S2->SetSize(FVector2D(B.TextSize.X, B.TextSize.Y));
			}
			B.TextBold->SetVisibility(B.bBold ? ESlateVisibility::HitTestInvisible : ESlateVisibility::Collapsed);
		}
	}
}

// ── ГЕОМЕТРИЯ ВЛАДЕЛЬЦА: мировая точка + экранный сдвиг по типу сущности ──────────────────────
bool UAsdaChatBalloonSubsystem::ResolveAnchor(const AActor* Owner, FVector& OutWorld, float& OutScreenY) const
{
	if (!::IsValid(Owner)) { return false; }
	// «Верх AABB» — из ЕДИНОГО источника с плашкой имени (данные сущности, не габариты меша).
	float TopZ = 0.f;
	if (const UAsdaNameplateComponent* NP = Owner->FindComponentByClass<UAsdaNameplateComponent>())
	{
		TopZ = NP->GetEntityTopZ();
	}
	else
	{
		FVector Origin, Extent;
		Owner->GetActorBounds(true, Origin, Extent);
		TopZ = (Origin.Z + Extent.Z) - Owner->GetActorLocation().Z;
	}

	float K;
	if (Owner->IsA(AAsdaMobActor::StaticClass()))      { K = MobWorldZ;    OutScreenY = MobScreenY; }
	else if (Owner->IsA(AAsdaNpcActor::StaticClass())) { K = NpcWorldZ;    OutScreenY = NpcScreenY; }
	else                                               { K = PlayerWorldZ; OutScreenY = PlayerScreenY; }

	OutWorld = Owner->GetActorLocation();
	OutWorld.Z += TopZ + K;
	// ★ 24.10.2026 [ПОРТ]: в прыжке облачко поднимается вместе с телом (подъём кости корня клипа; ключ `asda.Jump.OverheadFollow`).
	if (const AAsdaPlayerCharacter* JumpCh = Cast<AAsdaPlayerCharacter>(Owner)) { OutWorld.Z += JumpCh->GetOverheadLiftZ(); }
	// ★★★ [ОРИГИНАЛ] 06.11.2026 (asda.Overhead.MountLift): высота транспорта — game_main.c:4478-4491, chat.c:6543-6552.
	OutWorld.Z += AsdaOverheadOcclusion::MountLiftZ(Owner);
	return true;
}

// ── ПОКАДРОВОЕ ОБНОВЛЕНИЕ (эталонные циклы 0x9DAE40 / 0xF77A60 / 0xF92EA0) ────────────────────
void UAsdaChatBalloonSubsystem::UpdateBalloon(FAsdaBalloonInst& B, double NowS)
{
	UUserWidget* Wdg = B.Widget;
	if (!Wdg) { return; }
	AActor* Owner = B.Owner.Get();
	if (!::IsValid(Owner) || !B.bActive)
	{
		Wdg->SetVisibility(ESlateVisibility::Collapsed);
		return;
	}
	// Таймер uiControlAutoHideTimed @0xF22C60: now − t0 >= 5000 мс ⇒ bActive=0, bSuppressed=1, скрыть.
	if (B.ShowTimeS >= 0.0 && (NowS - B.ShowTimeS) * 1000.0 >= (double)LifetimeMs)
	{
		B.bActive = false;
		B.bSuppressed = true;
		Wdg->SetVisibility(ESlateVisibility::Collapsed);
		return;
	}
	// Владелец скрыт (шаблон/труп до деспавна) или моб умер — облачка нет.
	if (Owner->IsHidden())
	{
		Wdg->SetVisibility(ESlateVisibility::Collapsed);
		return;
	}
	if (const AAsdaMobActor* M = Cast<AAsdaMobActor>(Owner))
	{
		if (M->IsDead())   // гейт `*(ent+480) != 10` в uiDrawEntityNamePlate
		{
			Wdg->SetVisibility(ESlateVisibility::Collapsed);
			return;
		}
	}

	UWorld* W = GetWorld();
	APlayerController* PC = W ? UGameplayStatics::GetPlayerController(W, 0) : nullptr;
	if (!PC) { return; }

	FVector WorldPt; float ScreenYOff = 0.f;
	if (!ResolveAnchor(Owner, WorldPt, ScreenYOff))
	{
		Wdg->SetVisibility(ESlateVisibility::Collapsed);
		return;
	}
	B.LastWorldPt = WorldPt;   // ★ 30.10.2026: диагностика (самотест отставания от камеры)
	// ★ D3 (дедуп UI 2026-08-08): проекция мир→экран + фрустум-отсечка вынесены в
	//   AsdaUi::WorldToViewportFollow (та же точка, что у плашки AsdaNameplateComponent).
	FVector2D Screen;
	const float Margin = B.PanelSize.X + 64.f;
	if (!AsdaUi::WorldToViewportFollow(PC, W, WorldPt, Margin, Screen))
	{
		Wdg->SetVisibility(ESlateVisibility::Collapsed);   // за камерой или вне кадра
		return;
	}
	// ★★★ [ОРИГИНАЛ] 29.10.2026 (баг игроков 3, ключ asda.Overhead.Occlude): облачко перекрывается геометрией мира на глубине
	//   своей точки (у оригинала — z-тест планов, game_main.c:4523). Таймер жизни при этом идёт как прежде.
	// ★★★ [ОРИГИНАЛ] 05.11.2026 (asda.Overhead.OccludeAtText): z-тест — в пикселях окна облачка (оно стоит НАД проекцией точки, со
	//   сдвигом ScreenYOff), а не в самой точке: решение по строке центра окна (разбор — `AsdaOverheadOcclusion::EvaluateText`).
	const float DPIo = FMath::Max(UWidgetLayoutLibrary::GetViewportScale(W), 0.01f);
	const float OccA = AsdaOverheadOcclusion::EvaluateText(B.OccState, W, WorldPt,
		FVector2D(0.f, (ScreenYOff + B.PanelSize.Y * 0.5f) * DPIo), B.PanelSize.X * 0.5f * DPIo, Owner);
	if (OccA <= 0.f)
	{
		Wdg->SetVisibility(ESlateVisibility::Collapsed);
		return;
	}
	Wdg->SetVisibility(ESlateVisibility::HitTestInvisible);
	if (OccA != B.OccOpacity)
	{
		B.OccOpacity = OccA;
		Wdg->SetRenderOpacity(OccA);
	}

	const float DPI = FMath::Max(UWidgetLayoutLibrary::GetViewportScale(W), 0.01f);
	// Эталон: X −= смещение, затем uiTargetPlateLayoutCenter прибавляет его обратно и вычитает
	//   половину ширины окна ⇒ окно центрируется по X на проекции; Y = проекция + сдвиг по типу.
	const float PosX = Screen.X / DPI - B.PanelSize.X * 0.5f;
	const float PosY = Screen.Y / DPI + ScreenYOff;
	Wdg->SetPositionInViewport(FVector2D(PosX, PosY), /*bRemoveDPIScale=*/false);
	// Размер = окно + выступающий хвостик (хвостик садится на высоту_окна − 3).
	Wdg->SetDesiredSizeInViewport(FVector2D(B.PanelSize.X, B.PanelSize.Y - TailOverlap + B.TailSize.Y));
}

void UAsdaChatBalloonSubsystem::Tick(float /*DeltaTime*/)
{
	// ★ 30.10.2026 (asda.Overhead.LateProject 1): облачка считаются ПОСЛЕ обновления камеры кадра (подписка из Initialize).
	if (AsdaOverheadLate::IsOn()) { return; }
	TickBalloons();
}

void UAsdaChatBalloonSubsystem::TickBalloons()
{
	UWorld* W = GetWorld();
	if (!W || !W->IsGameWorld()) { return; }

	// Заявки, накопленные обработчиками пакетов (никакой работы со сценой из колбэка сети).
	if (TArray<FAsdaPendingBalloon> Batch; Pending.Drain(Batch))   // ★ U7 (В1 дедупа №2): общая очередь `TAsdaNetQueue` (замок + Drain) вместо своего массива
	{
		for (const FAsdaPendingBalloon& P : Batch) { ResolveAndShow(P); }
	}

	const double NowS = W->GetTimeSeconds();
	for (int32 i = Balloons.Num() - 1; i >= 0; --i)
	{
		FAsdaBalloonInst& B = Balloons[i];
		// Владелец исчез (деспавн/смена карты) — снимаем виджет, как uiTooltipPlateHide при удалении.
		if (!B.Owner.IsValid() || !B.Widget)
		{
			if (B.Widget) { B.Widget->RemoveFromParent(); }
			Balloons.RemoveAt(i);
			continue;
		}
		UpdateBalloon(B, NowS);
	}
}

FString UAsdaChatBalloonSubsystem::DebugState() const
{
	FString S = FString::Printf(TEXT("облачки: %d (показ %s, жизнь %d мс, перенос > %d колонок)\n"),
		Balloons.Num(), bShowBalloons ? TEXT("вкл") : TEXT("выкл"), LifetimeMs, WrapColumns);
	const double NowS = GetWorld() ? GetWorld()->GetTimeSeconds() : 0.0;
	for (const FAsdaBalloonInst& B : Balloons)
	{
		S += FString::Printf(TEXT("  %-28s вариант=%d активно=%d погашено=%d осталось=%.2f с окно=%.0f×%.0f текст=%.0f×%.0f выр.=%d\n"),
			B.Owner.IsValid() ? *B.Owner->GetName() : TEXT("<нет>"), (int32)B.Kind,
			B.bActive ? 1 : 0, B.bSuppressed ? 1 : 0,
			B.ShowTimeS >= 0.0 ? FMath::Max(0.0, (double)LifetimeMs / 1000.0 - (NowS - B.ShowTimeS)) : 0.0,
			B.PanelSize.X, B.PanelSize.Y, B.TextSize.X, B.TextSize.Y, B.TextAlign);
	}
	return S;
}

// ═══════════════════════════════ КОНСОЛЬНЫЕ КОМАНДЫ ═══════════════════════════════════════════
static UAsdaChatBalloonSubsystem* AsdaTalkBox(UWorld* W)
{
	return W ? W->GetSubsystem<UAsdaChatBalloonSubsystem>() : nullptr;
}

#if ASDA_DEV_CONSOLE   // ★ античит А2: дев-команда, в Shipping не регистрируется
static FAutoConsoleCommandWithWorldAndArgs GAsdaBalloon(
	TEXT("asda.Chat.Balloon"),
	TEXT("Облачко над СВОИМ персонажем локально (без сервера): asda.Chat.Balloon [вариант 0|1|2] <текст>. ")
	TEXT("0 — белое (обычный чат), 1 — золотое (шёпот), 2 — чёрное (моб/NPC). Цвет текста берётся эталонный."),
	FConsoleCommandWithWorldAndArgsDelegate::CreateStatic([](const TArray<FString>& Args, UWorld* W)
	{
		UAsdaChatBalloonSubsystem* S = AsdaTalkBox(W);
		APlayerController* PC = W ? UGameplayStatics::GetPlayerController(W, 0) : nullptr;
		APawn* Self = PC ? PC->GetPawn() : nullptr;
		if (!S || !Self)
		{
			UE_LOG(LogAsdaTalkBox, Warning, TEXT("asda.Chat.Balloon: нет подсистемы или павна (нужно быть в мире)"));
			return;
		}
		int32 First = 0;
		int32 Variant = 0;
		if (Args.Num() >= 2 && Args[0].IsNumeric()) { Variant = FMath::Clamp(FCString::Atoi(*Args[0]), 0, 2); First = 1; }
		FString Text;
		for (int32 i = First; i < Args.Num(); ++i) { Text += (i > First ? TEXT(" ") : TEXT("")) + Args[i]; }
		if (Text.IsEmpty()) { Text = TEXT("Проверка облачка чата"); }
		// Цвета/стиль — ровно те, что эталон передаёт для соответствующего варианта:
		//   0 → 5085 Recv_ChatNormal (FF000000, обычный), 1 → 5089 Recv_ChatWhisper (FF222222, обычный),
		//   2 → 8008 recvCharChatBubble (FFFF3728, обычный).
		static const FColor kColors[3] = { FColor(0,0,0,255), FColor(0x22,0x22,0x22,255), FColor(0xFF,0x37,0x28,255) };
		S->ShowBalloon(Self, (EAsdaTalkBox)(uint8)Variant, Text, kColors[Variant], /*bBold=*/false);
	}));
#endif // ASDA_DEV_CONSOLE

#if ASDA_DEV_CONSOLE   // ★ античит А2: дев-команда, в Shipping не регистрируется
static FAutoConsoleCommandWithWorldAndArgs GAsdaBalloonDump(
	TEXT("asda.Chat.BalloonDump"),
	TEXT("Состояние облачек чата: владелец, вариант, остаток таймера, размеры окна/текста, выравнивание"),
	FConsoleCommandWithWorldAndArgsDelegate::CreateStatic([](const TArray<FString>&, UWorld* W)
	{
		if (UAsdaChatBalloonSubsystem* S = AsdaTalkBox(W))
		{
			UE_LOG(LogAsdaTalkBox, Log, TEXT("%s"), *S->DebugState());
		}
	}));
#endif // ASDA_DEV_CONSOLE
