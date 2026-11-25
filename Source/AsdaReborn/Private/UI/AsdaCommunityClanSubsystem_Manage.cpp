// Copyright (c) 2026 @sitxovski (Discord)
// AsdaCommunityClanSubsystem_Manage.cpp — ★ R3-c 12.11.2026 (разрез AsdaCommunityClanSubsystem.cpp, дословный перенос): панель «Предводитель»: страницы управления, права рангов, звания, передача главы, эмблема.
//   Общее для частей — в AsdaCommunityClanSubsystem_Private.h; шапка и реверс — в AsdaCommunityClanSubsystem.cpp.
#include "UI/AsdaCommunityClanSubsystem.h"

#include "AsdaDevConsole.h"   // ★ античит А2: ASDA_DEV_CVAR / ASDA_DEV_CONSOLE — дев-ключи и команды вне Shipping
#include "UI/AsdaNotice.h"   // ★ волна 1: фасад нотисов (Loc / Chat / MsgBox)

#include "UI/AsdaWindowSubsystem.h"
#include "Blueprint/WidgetTree.h"           // обход дерева окна: границы страниц панели управления
#include "Components/CheckBox.h"            // галочки прав звания 35512…35556 (страница 1 панели)
#include "Components/EditableText.h"        // поля ввода 14021 «заметка» и 14029 «объявление»
#include "UI/AsdaUiHelpers.h"   // ★ 31.10.2026: AsdaUi::AnchorRelay — общий GC-якорь реле
#include "UI/AsdaJuiComboBox.h"  // ★ 01.11.2026: списки званий страницы «Ранги»
#include "UI/AsdaHotbarSubsystem.h"   // ★ 01.11.2026: значок 13 (предложение стать главой)
#include "AsdaCommunityClanSubsystem_Private.h"   // ★ R3-c 12.11.2026: общее для частей AsdaCommunityClanSubsystem*.cpp

// ★ R3-c чистка 12.11.2026: помощники, нужные только этой части (дословно из AsdaCommunityClanSubsystem_Private.h).
namespace {
	// ★★★ ПОДДЕРЕВЬЯ ШЕСТИ СТРАНИЦ ПАНЕЛИ УПРАВЛЕНИЯ — ПРЕФИКСЫ ИМЁН УЗЛОВ.
	//   Составлены НЕ на глаз: сняты полным структурным дампом разметки окна 6400
	//   (`Reports/soulmate_win6400_jui_dump.md`, ветка `CmtyClan_Manage_Win` 35000) — для
	//   каждой страницы взят список ИМЁН её потомков и свёрнут до непересекающихся префиксов.
	//   Импортёр иерархию расплющивает (закон `jui-uneven-node-layout`), поэтому гасить панель
	//   целиком нельзя — только по префиксам её содержимого.
	//   ⚠ ДО ЭТОЙ ПРАВКИ порт показывал ВСЕ ШЕСТЬ страниц разом (общий префикс `CmtyClan_Sub`
	//     в списке подрежима 2) — ровно то, на что жаловался владелец.
	static const TCHAR* kMgrPage0[] = {   // 35100 «Кандидаты»
		TEXT("CmtyClan_SubMenu1"), TEXT("CmtyClan_Applicant"), TEXT("CmtyClan_Sub1_"),
	};
	static const TCHAR* kMgrPage1[] = {   // 35500 «Права членов клана»
		TEXT("CmtyClan_SubMenu2"), TEXT("CmtyClan_Position"), TEXT("CmtyClan_PosFunc"),
		TEXT("CmtyClan_Sub2_"),
	};
	static const TCHAR* kMgrPage2[] = {   // 35600 «Ранги членов клана»
		TEXT("CmtyClan_SubMenu3"), TEXT("CmtyClan_TitleBase"), TEXT("CmtyClan_Sub3_"),
	};
	static const TCHAR* kMgrPage3[] = {   // 35800 «Объявления» (единственная страница vis=1 в разметке)
		TEXT("CmtyClan_SubMenu4"), TEXT("CmtyClan_Sub4_"), TEXT("Cmty_Clan_NoticeWt_Base"),
		TEXT("CMTY_CLAN_NOITICE"),
	};
	static const TCHAR* kMgrPage4[] = {   // 35900 «Передача полномочий главы клана»
		TEXT("CmtyClan_SubMenu5"), TEXT("CmtyClan_Sub5_"),
	};
	static const TCHAR* kMgrPage5[] = {   // 36100 «Герб клана»
		TEXT("CmtyClan_SubMenu6"), TEXT("CmtyClan_Sub6_"), TEXT("CmtyClan_BG"),
		TEXT("CmtyClan_Bor"), TEXT("CmtyClan_Flag"), TEXT("CmtyClan_Ins"),
	};
	struct FMgrPagePrefixes { const TCHAR* const* Items; int32 Num; };
	static const FMgrPagePrefixes kMgrPages[kManagePages] = {
		{ kMgrPage0, UE_ARRAY_COUNT(kMgrPage0) }, { kMgrPage1, UE_ARRAY_COUNT(kMgrPage1) },
		{ kMgrPage2, UE_ARRAY_COUNT(kMgrPage2) }, { kMgrPage3, UE_ARRAY_COUNT(kMgrPage3) },
		{ kMgrPage4, UE_ARRAY_COUNT(kMgrPage4) }, { kMgrPage5, UE_ARRAY_COUNT(kMgrPage5) },
	};
}

// ★ 01.11.2026: ключи панели «Предводитель» живут в `AsdaCommunityClanManage.cpp` — читаются аксессорами (имя ключа в exe вне журнала не попадает).
extern bool AsdaClanGradeRowMapOn();

namespace {
	// ★ 04.11.2026 [ИСПРАВЛЕНИЕ] (жалоба игрока «в клане 3 человека, в правке рангов только 2»): эталон `listUI_RefreshPage7Rows@0xBC3910`
	//   берёт записи `база + строка + 1` (1-based) и страниц `(N − 2) / 7` — то есть ВСЕГДА пропускает запись 0 списка, считая её главой.
	//   Но сервер отдаёт состав без сортировки (`UPClanMemberinfo` — SELECT без ORDER BY, на живой БД глава клана 1 стоит вторым), и
	//   член клана, оказавшийся первым в списке, в «Рангах» не показывается вовсе (а значит, его нельзя ни повысить, ни изгнать).
	//   1 — в «Рангах» все члены, кроме главы (звание 1), по 7 на страницу; 0 — как оригинал.
	ASDA_DEV_CVAR(int32, CVarGradeListAllMembers,
		TEXT("asda.Clan.GradeListAllMembers"), 1,
		TEXT("[ИСПРАВЛЕНИЕ] 1 (по умолчанию) = в «Рангах членов клана» все члены, кроме главы, при любом порядке списка сервера; ")
		TEXT("0 = как оригинал (запись 0 списка пропускается всегда — пропадал член, стоящий первым)."),
		ECVF_Default);

	constexpr int32 kApplicantRows = 8;   // строк-кандидатов страницы 0 (35120 + 20*k, k = 0…7)
	constexpr int32 kRankRows      = 2;   // званий в таблице прав страницы 1 (35510 и 35550)
	constexpr int32 kRankPerms     = 3;   // прав на звание (галочки 35512/35514/35516 и 35552/…)
	constexpr int32 kGradeRows     = 7;   // строк списка званий страницы 2 (35620 + 20*f, f = 0…6)
	constexpr int32 kMasterRows    = 5;   // строк-заместителей страницы 4 (35920 + 20*m, m = 0…4)
	constexpr int32 kGradeCombo    = 4;   // пунктов в комбо звания (loc 17041…17044)

	// ── Подписи страниц панели управления — ВСЕ ВЗЯТЫ ИЗ `CmtyClan_Manage_BuildUI@0xBBE320` ──
	//   (`AsdaStory_game.c:232762-233318`; ни одна не выдумана, у каждой в разборе есть строка).
	constexpr int32 kLocMgrDenied   = 17107;  // «Временно недоступно» — отказ страницы 0 (ветка v6)
	constexpr int32 kLocApplAccept  = 17123;  // подпись кнопки 35124+20k «Принять»
	constexpr int32 kLocApplReject  = 17124;  // подпись кнопки 35125+20k «Отказать»
	constexpr int32 kLocRankHdr     = 17141;  // 35501 — заголовок страницы «Права членов клана»
	constexpr int32 kLocRankPerm0   = 17142;  // 17142…17144 — подписи трёх прав (35513/35515/35517)
	constexpr int32 kLocRankName0   = 17044;  // 35511 «Заместитель главы» (звание 2)
	constexpr int32 kLocRankName1   = 17043;  // 35551 «Ценный сотрудник»  (звание 3)
	constexpr int32 kLocBtnOk       = 2288;   // 35590 «ОК»      (`CWidget_SetTextAll4`, :233867)
	constexpr int32 kLocBtnCancel   = 2289;   // 35591 «Отмена»
	constexpr int32 kLocGradeHdr    = 17161;  // 35601 — заголовок страницы «Ранги членов клана»
	constexpr int32 kLocColName     = 17162;  // 35610 «Имя персонажа»
	constexpr int32 kLocColLevel    = 17163;  // 35611 и 35912 «Уровень»
	constexpr int32 kLocColPoints   = 17164;  // 35612 и 35913 «Передача ОД»
	constexpr int32 kLocExpel       = 17165;  // 35624+20f «Изгнание»
	constexpr int32 kLocGradeItem0  = 17041;  // 17041…17044 — 4 пункта комбо звания (35630+20f)
	constexpr int32 kLocMasterHdr   = 17201;  // 35901 «[Внимание] Передача полномочий главы клана»
	constexpr int32 kLocMasterTxt   = 17202;  // 35902 — пояснение
	constexpr int32 kLocMasterBtn   = 17203;  // 36021 — кнопка «Передача полномочий главы клана»
	constexpr int32 kLocEmblemHdr   = 17236;  // 36101 «Сначала выбери элемент…»
	constexpr int32 kLocEmblemBg    = 17237;  // 36121 «Цвет фона»
	constexpr int32 kLocColorR      = 17238;  // 36123 «Красный»
	constexpr int32 kLocColorG      = 17239;  // 36125 «Зеленый»
	constexpr int32 kLocColorB      = 17240;  // 36127 «Синий»
	constexpr int32 kLocEmblemSign  = 17241;  // 36141 «Герб клана»
	constexpr int32 kLocEmblemBord  = 17242;  // 36301 «Выбор рамки»
	constexpr int32 kLocColorAdjust = 17243;  // 36150 и 36320 «Изменение цвета»
	constexpr int32 kLocEmblemSave  = 17244;  // 36310 — кнопка сохранения герба
	constexpr int32 kLocEmblemUndo  = 17245;  // 36311 — кнопка отката герба
}

// ═════════════ ПАНЕЛЬ УПРАВЛЕНИЯ 35000: ШЕСТЬ СТРАНИЦ (порт `uiSelectPanelTab@0xBC5C20`) ═══════
//   ★★★ ЭТО БЫЛА ИЗВЕСТНАЯ ДЫРА ПОРТА: подрежим «Предводитель» показывал все шесть подстраниц
//   разом, потому что переключателя страниц не было вовсе. Разобрано и реализовано 02.09.2026.
// Индекс обхода JUI из имени узла ассета. Импортёр даёт БЕЗЫМЯННЫМ (без `ID`) контролам имена
//   вида `<имя скина>_<индекс обхода>` и `<имя скина>_<индекс обхода>_<номер плана>`, а также
//   `..._HIT` / `..._DRAG` (`Tools/asda_jui_generic_umg.py`). Индекс обхода — ПЕРВЫЙ чисто
//   числовой сегмент имени: у `BoxIcon_Level_268_0` это 268, у `CmtyClan_SubMenu1_260_HIT` — 260.
//   `INDEX_NONE`, если числового сегмента нет (узел назван по `ID` разметки).
static int32 AsdaJuiTraversalIndex(const FString& Name)
{
	TArray<FString> Parts;
	Name.ParseIntoArray(Parts, TEXT("_"), /*bCullEmpty*/ false);
	for (int32 i = 1; i < Parts.Num(); ++i)
	{
		const FString& P = Parts[i];
		if (P.IsEmpty()) { continue; }
		bool bDigits = true;
		for (const TCHAR C : P) { if (!FChar::IsDigit(C)) { bDigits = false; break; } }
		if (bDigits) { return FCString::Atoi(*P); }
	}
	return INDEX_NONE;
}

// ★★★ ГРАНИЦЫ ПОДДЕРЕВЬЕВ ШЕСТИ СТРАНИЦ — СНИМАЮТСЯ С ЖИВОГО ДЕРЕВА, БЕЗ МАГИЧЕСКИХ ЧИСЕЛ.
//   ЗАЧЕМ. Префиксы имён (`kMgrPages`) закрывают все узлы, у которых в разметке есть `ID`, но
//   внутри страниц лежат и БЕЗЫМЯННЫЕ украшения — `BoxIcon_Level` (рамка уровня, страницы 0/2/4),
//   `BtnBase`, `WkBtn_IconCheck`, `RdBtn_IconCancel` (подложки кнопок, страницы 1/4/5),
//   `Popup_TalkBox_Red` (красная плашка, страница 4). Имена у них ОДИНАКОВЫЕ на разных страницах,
//   различает их только индекс обхода. Обход JUI — в глубину, поэтому поддерево каждой страницы
//   занимает НЕПРЕРЫВНЫЙ диапазон индексов: [индекс `CmtyClan_SubMenuN`, индекс следующей).
//   Верхнюю границу последней страницы даёт первый узел ПОСЛЕ неё — фон панели `BG`
//   (в разметке он брат шести страниц; дамп окна 6400, ветка `CmtyClan_Manage_Win`).
//   ⚠ Сам `BG` ни в один диапазон не попадает и гасится только вместе с подрежимом (`kMgrChrome`)
//     — ровно как в эталоне, где `uiSelectPanelTab` трогает исключительно шесть контролов-страниц.
void UAsdaCommunityClanSubsystem::CollectManagePageAnchors()
{
	ManagePageAnchor.Init(INDEX_NONE, kManagePages + 1);
	if (!CommWnd || !CommWnd->WidgetTree) { return; }

	// Якоря шести страниц + верхняя граница последней.
	static const TCHAR* kAnchorIdc[kManagePages + 1] = {
		TEXT("CmtyClan_SubMenu1"), TEXT("CmtyClan_SubMenu2"), TEXT("CmtyClan_SubMenu3"),
		TEXT("CmtyClan_SubMenu4"), TEXT("CmtyClan_SubMenu5"), TEXT("CmtyClan_SubMenu6"),
		TEXT("BG"),
	};
	for (int32 A = 0; A <= kManagePages; ++A)
	{
		const FString Pref = FString(kAnchorIdc[A]) + TEXT("_");
		int32 Best = INDEX_NONE;
		CommWnd->WidgetTree->ForEachWidget([&Pref, &Best](UWidget* W)
		{
			if (!W) { return; }
			const FString N = W->GetName();
			if (!N.StartsWith(Pref, ESearchCase::CaseSensitive)) { return; }
			const int32 Idx = AsdaJuiTraversalIndex(N);
			if (Idx == INDEX_NONE) { return; }
			if (Best == INDEX_NONE || Idx < Best) { Best = Idx; }
		});
		ManagePageAnchor[A] = Best;
	}
	UE_LOG(LogAsdaClanTab, Log,
		TEXT("[клан-вкладка] границы страниц панели управления: %d/%d/%d/%d/%d/%d, конец %d"),
		ManagePageAnchor[0], ManagePageAnchor[1], ManagePageAnchor[2], ManagePageAnchor[3],
		ManagePageAnchor[4], ManagePageAnchor[5], ManagePageAnchor[6]);
}

// Показать/скрыть ОДНУ страницу панели управления целиком: и узлы с `ID` (по префиксам имён),
//   и безымянные украшения внутри её диапазона индексов обхода.
void UAsdaCommunityClanSubsystem::SetManagePageVisible(int32 PageIdx, bool bVisible)
{
	if (!CommWnd || PageIdx < 0 || PageIdx >= kManagePages) { return; }
	using namespace AsdaCommunityUi;

	const FMgrPagePrefixes& Pfx = kMgrPages[PageIdx];
	for (int32 i = 0; i < Pfx.Num; ++i) { SetVisByPrefix(CommWnd, Pfx.Items[i], bVisible); }
	// ★ 01.11.2026: префиксы зажгли и узлы, скрытые в разметке (рамка выбора, подсветки, списки, курсоры) — вернуть их состояние.
	if (bVisible) { ReapplyPageRuntimeState(PageIdx); }

	// Безымянные узлы страницы — по диапазону индексов обхода (см. `CollectManagePageAnchors`).
	if (!CommWnd->WidgetTree || ManagePageAnchor.Num() <= kManagePages) { return; }
	const int32 Lo = ManagePageAnchor[PageIdx];
	const int32 Hi = ManagePageAnchor[PageIdx + 1];
	if (Lo == INDEX_NONE || Hi == INDEX_NONE || Hi <= Lo) { return; }
	CommWnd->WidgetTree->ForEachWidget([Lo, Hi, bVisible](UWidget* W)
	{
		if (!W) { return; }
		const FString N = W->GetName();
		// Узлы, названные по `ID` разметки, уже обработаны префиксами — второй раз не трогаем
		//   (иначе диапазон забрал бы себе чужой `CmtyClan_*` по случайному совпадению цифр).
		if (N.StartsWith(TEXT("CmtyClan"), ESearchCase::IgnoreCase)
			|| N.StartsWith(TEXT("CMTY_CLAN"), ESearchCase::IgnoreCase)
			|| N.StartsWith(TEXT("Cmty_Clan"), ESearchCase::IgnoreCase)
			|| N.StartsWith(TEXT("Clan_"), ESearchCase::IgnoreCase))
		{
			return;
		}
		const int32 Idx = AsdaJuiTraversalIndex(N);
		if (Idx < Lo || Idx >= Hi) { return; }
		W->SetVisibility(bVisible ? ShownVisibilityFor(W) : ESlateVisibility::Collapsed);
	});
}

// ★★★ ПОРТ `uiSelectPanelTab@0xBC5C20` — ДОСЛОВНО.
//   Эталон:
//     for (i=0;i<6;++i) SetVisible(page[i], 0);          // гасим ВСЕ шесть страниц
//     sub_91F5D6(this);                                  // = sub_BC8150: снять фокус с полей
//                                                        //   14029 (wnd[173]) и 14021 (wnd[14])
//     switch (a2) {
//       case 0: v6 = 1; break;                           // ← ВСЕГДА отказ, нотис 17107
//       case 1: master? cUI_RefreshTabControlsVisibility : v7 = 1;
//       case 2: master? (uiSetSixControlsFieldAndShow + listUI_RefreshPage7Rows) : v7 = 1;
//       case 3: (RankPerm(звание,2) || master)? sub_922772 : v7 = 1;
//       case 4: master? uiTradeListPopulate5Rows : v7 = 1;
//       case 5: master? uiInvFilterPanelRefresh : v7 = 1;
//     }
//     if (v6) { нотис 17107; SetVisible(page[текущая], 1); return 0; }
//     if (v7) { нотис 17006; SetSelectedIndex(bar35001, текущая); SetVisible(page[текущая],1); return 0; }
//     this[386] = a2; SetVisible(page[a2], 1); return 1;
//   `sub_92CE3E` = `IsClanMaster@0xBC7090` (звание == 1), `sub_92E478` = `ClanRankPermission`.
void UAsdaCommunityClanSubsystem::SelectManagePage(int32 PageIdx)
{
	if (!IsBoundTo(CommWnd)) { return; }
	if (PageIdx < 0 || PageIdx >= kManagePages) { return; }
	// ★ 01.11.2026 [ИСПРАВЛЕНИЕ] (`asda.Clan.HideApplicantsPage`): страницы «Кандидаты» нет — запрос на неё не исполняется.
	if (PageIdx == 0 && CVarHideApplicantsPage.GetValueOnGameThread() != 0) { return; }

	// 1:1: гасим все шесть страниц ДО разбора гейта.
	for (int32 P = 0; P < kManagePages; ++P) { SetManagePageVisible(P, false); }
	ClearEditFocus();   // `sub_91F5D6` → `sub_BC8150`

	const bool bMaster = (SelfGrade() == 1);      // `IsClanMaster@0xBC7090`: char+0x97B == 1
	bool bDeniedAlways = false;                   // ветка `v6` — нотис 17107
	bool bDeniedRights = false;                   // ветка `v7` — нотис 17006
	switch (PageIdx)
	{
	case 0:
		// ★ ОСОБЕННОСТЬ ОРИГИНАЛА, ВОСПРОИЗВОДИМ 1:1: страница «Кандидаты» НЕ ОТКРЫВАЕТСЯ
		//   НИКОГДА — эталон безусловно ставит `v6 = 1` и показывает нотис 17107 «Временно
		//   недоступно». Восемь строк-кандидатов 35120…35260 в клиенте никто не заполняет вовсе
		//   (`CmtyClan_Manage_BuildUI` только чистит их и ставит подписи кнопок 17123/17124),
		//   опкода «список заявок в клан» в срезе нет. Придумывать его нельзя.
		bDeniedAlways = true;
		break;
	case 1:
		if (bMaster) { RefreshRankPermPage(); } else { bDeniedRights = true; }
		break;
	case 2:
		if (bMaster)
		{
			// `uiSetSixControlsFieldAndShow@0xBC6140`: очистить 6 строк и ПОКАЗАТЬ полосу 14101
			//   (пагинация страницы званий — та же, что у состава клана).
			for (int32 R = 0; R < kGradeRows - 1; ++R) { ClearGradeRow(R); }
			SetBottomBarVisible(true);
			RefreshGradePage();
		}
		else { bDeniedRights = true; }
		break;
	case 3:
		// Гейт эталона у этой страницы ДРУГОЙ: маска права 2 ЛИБО «я глава».
		if (SelfHasPermission(kPermManage))
		{
			// `sub_922772` → `uiClanDelete_FocusInput@0xBC61B0`: очистить поле 14029, вернуть в
			//   него каретку и СКРЫТЬ полосу 14101.
			if (UEditableText* E = FindEdit(TEXT("CMTY_CLAN_NOITICE_WRITE")))
			{
				E->SetText(FText::GetEmpty());
				E->SetKeyboardFocus();
			}
			SetBottomBarVisible(false);
		}
		else { bDeniedRights = true; }
		break;
	case 4:
		if (bMaster) { RefreshMasterTransferPage(); } else { bDeniedRights = true; }
		break;
	case 5:
		if (bMaster) { RefreshEmblemPage(); } else { bDeniedRights = true; }
		break;
	default:
		break;
	}

	if (bDeniedAlways || bDeniedRights)
	{
		NoticeBox(Loc(bDeniedAlways ? kLocMgrDenied : kLocForbidden));   // 17107 либо 17006
		// ★ ИСПРАВЛЕНИЕ ДЕФЕКТА ОРИГИНАЛА (помечено, закон `fix-original-defects`).
		//   ОРИГИНАЛ: `SetSelectedIndex(bar, текущая)` эталон делает ТОЛЬКО в ветке `v7` (отказ по
		//     правам). В ветке `v6` (страница «Кандидаты») он этого не делает — полоса 35001
		//     остаётся подсвеченной на пункте «Кандидаты», хотя показана прежняя страница.
		//   МЫ: возвращаем подсветку в обеих ветках. ПОЧЕМУ: в UMG кнопка-опция состояния не
		//     держит, подсветку целиком ведёт код, и «залипший» пункт был бы НАШИМ собственным
		//     дефектом, а не воспроизведением оригинала. Ключ отката не заводим: наблюдаемого
		//     поведения оригинала (подсветка следует за контролом) правка не меняет.
		AsdaCommunityUi::SetOptSelected(CommWnd, TEXT("CmtyClan_SubMenuBtn"), ManageSubPage);
		SetManagePageVisible(ManageSubPage, true);
		UE_LOG(LogAsdaClanTab, Log,
			TEXT("[клан-вкладка] страница панели управления %d отклонена (%s), остаёмся на %d"),
			PageIdx, bDeniedAlways ? TEXT("нотис 17107 «Временно недоступно»")
			                       : TEXT("нотис 17006 «Запрещено»"), ManageSubPage);
		return;
	}

	ManageSubPage = PageIdx;                     // 1:1 `this[386] = a2`
	SetManagePageVisible(PageIdx, true);
	// В оригинале подсветку пункта переносит САМ контрол-список; в UMG это делаем мы (тот же
	//   примитив, что и у полосы подрежимов 14000).
	AsdaCommunityUi::SetOptSelected(CommWnd, TEXT("CmtyClan_SubMenuBtn"), PageIdx);
	UE_LOG(LogAsdaClanTab, Log, TEXT("[клан-вкладка] страница панели управления → %d"), PageIdx);
}

// `sub_91F5D6` → `sub_BC8150@0xBC8150`: `SetFocus(wnd[173], 0); SetFocus(wnd[14], 0);`
//   — снять каретку с поля объявления 14029 и с поля заметки 14021.
void UAsdaCommunityClanSubsystem::ClearEditFocus()
{
	if (!CommWnd) { return; }
	static const TCHAR* kEdits[] = { TEXT("CMTY_CLAN_NOITICE_WRITE"), TEXT("Clan_MyIntroTxt") };
	for (const TCHAR* Idc : kEdits)
	{
		UEditableText* E = FindEdit(Idc);
		// В UMG «снять фокус с виджета» = вернуть его окну-владельцу: прямого `SetFocus(0)`,
		//   как у `CControl`, здесь нет.
		if (E && E->HasKeyboardFocus()) { CommWnd->SetKeyboardFocus(); }
	}
}

// ★ Подписи всех шести страниц панели управления. Каждая строка — из `CmtyClan_Manage_BuildUI`
//   (`AsdaStory_game.c:232762-233318`); loc-идентификаторы объявлены в шапке файла.
void UAsdaCommunityClanSubsystem::BindManagePageCaptions()
{
	if (!CommWnd) { return; }
	using namespace AsdaCommunityUi;

	// ── Страница 0 (35100 «Кандидаты») ──
	//   Эталон ставит 35101 и 35102 ПУСТЫМИ строками (`CControl_SetText(…, &Src)`) и никогда
	//   ничего в них не пишет: список заявок клиенту не приходит. Строки-кандидаты тоже чистятся
	//   (`uiClanDeleteClearAllText`), а подписи кнопок берутся из loc 17123 / 17124.
	SetTxt(CommWnd, TEXT("CmtyClan_Sub1_Txt00"), FString());   // 35101 ← "" (1:1)
	SetTxt(CommWnd, TEXT("CmtyClan_Sub1_Txt01"), FString());   // 35102 ← "" (1:1)
	for (int32 k = 0; k < kApplicantRows; ++k)
	{
		SetTxt(CommWnd, FString::Printf(TEXT("CmtyClan_Sub1_Name%d"), k), FString());   // 35121+20k
		SetTxt(CommWnd, FString::Printf(TEXT("CmtyClan_Sub1_Lv%d"),   k), FString());   // 35122+20k
		SetTxt(CommWnd, FString::Printf(TEXT("CmtyClan_Sub1_Job%d"),  k), FString());   // 35123+20k
		SetButtonCaption(CommWnd, FString::Printf(TEXT("CmtyClan_Sub1_OKBtn%d"),  k), Loc(kLocApplAccept));
		SetButtonCaption(CommWnd, FString::Printf(TEXT("CmtyClan_Sub1_CanBtn%d"), k), Loc(kLocApplReject));
	}

	// ── Страница 1 (35500 «Права членов клана») ──
	SetTxt(CommWnd, TEXT("CmtyClan_Sub2_Txt00"), Loc(kLocRankHdr));            // 35501 ← 17141
	static const int32 kRankNameLoc[kRankRows] = { kLocRankName0, kLocRankName1 };
	for (int32 R = 0; R < kRankRows; ++R)
	{
		// 35511 ← loc 17044 «Заместитель главы» (звание 2), 35551 ← loc 17043 «Ценный сотрудник»
		//   (звание 3): массив `posNameStrIds[2] = {17044, 17043}` построителя.
		SetTxt(CommWnd, FString::Printf(TEXT("CmtyClan_PositionName%d"), R + 1), Loc(kRankNameLoc[R]));
		for (int32 M = 0; M < kRankPerms; ++M)
		{
			// Подписи прав 35513/35515/35517 и 35553/35555/35557 ← loc 17142 + m.
			SetTxt(CommWnd, FString::Printf(TEXT("CmtyClan_PosFunc%d_%d"), R + 1, M + 1),
				Loc(kLocRankPerm0 + M));
		}
	}
	SetButtonCaption(CommWnd, TEXT("CmtyClan_Sub2_ChangeBtn"), Loc(kLocBtnOk));       // 35590 ← 2288
	SetButtonCaption(CommWnd, TEXT("CmtyClan_Sub2_CancelBtn"), Loc(kLocBtnCancel));   // 35591 ← 2289

	// ── Страница 2 (35600 «Ранги членов клана») ──
	SetTxt(CommWnd, TEXT("CmtyClan_Sub3_Txt00"), Loc(kLocGradeHdr));     // 35601 ← 17161
	SetTxt(CommWnd, TEXT("CmtyClan_Sub3_Txt01"), Loc(kLocColName));      // 35610 ← 17162
	SetTxt(CommWnd, TEXT("CmtyClan_Sub3_Txt02"), Loc(kLocColLevel));     // 35611 ← 17163
	SetTxt(CommWnd, TEXT("CmtyClan_Sub3_Txt03"), Loc(kLocColPoints));    // 35612 ← 17164
	for (int32 F = 0; F < kGradeRows; ++F)
	{
		SetButtonCaption(CommWnd, FString::Printf(TEXT("CmtyClan_Sub3_Expulsion%d"), F), Loc(kLocExpel));
		ClearGradeRow(F);
	}

	// ── Страница 3 (35800 «Объявления») — её три подписи (35801 ← 17181, 14031 ← 17183,
	//   14030 ← 17184) уже выставлены выше в `BindTabControls`; дублировать не нужно.

	// ── Страница 4 (35900 «Передача полномочий главы клана») ──
	SetTxt(CommWnd, TEXT("CmtyClan_Sub5_Txt00"), Loc(kLocMasterHdr));    // 35901 ← 17201
	SetTxt(CommWnd, TEXT("CmtyClan_Sub5_Txt01"), Loc(kLocMasterTxt));    // 35902 ← 17202
	SetTxt(CommWnd, TEXT("CmtyClan_Sub5_T1"),    Loc(kLocRankName0));    // 35911 ← 17044
	SetTxt(CommWnd, TEXT("CmtyClan_Sub5_T2"),    Loc(kLocColLevel));     // 35912 ← 17163
	SetTxt(CommWnd, TEXT("CmtyClan_Sub5_T3"),    Loc(kLocColPoints));    // 35913 ← 17164
	SetButtonCaption(CommWnd, TEXT("CmtyClan_Sub5_ConBtn"), Loc(kLocMasterBtn));   // 36021 ← 17203
	for (int32 M = 0; M < kMasterRows; ++M) { ClearMasterRow(M); }

	// ── Страница 5 (36100 «Герб клана») ──
	SetTxt(CommWnd, TEXT("CmtyClan_Sub6_Txt00"), Loc(kLocEmblemHdr));      // 36101 ← 17236
	SetTxt(CommWnd, TEXT("CmtyClan_Sub6_Txt01"), Loc(kLocEmblemBg));       // 36121 ← 17237
	SetTxt(CommWnd, TEXT("CmtyClan_BG_Red"),     Loc(kLocColorR));         // 36123 ← 17238
	SetTxt(CommWnd, TEXT("CmtyClan_BG_Green"),   Loc(kLocColorG));         // 36125 ← 17239
	SetTxt(CommWnd, TEXT("CmtyClan_BG_Blue"),    Loc(kLocColorB));         // 36127 ← 17240
	SetTxt(CommWnd, TEXT("CmtyClan_Sub6_Txt02"), Loc(kLocEmblemSign));     // 36141 ← 17241
	SetTxt(CommWnd, TEXT("CmtyClan_Sub6_Txt03"), Loc(kLocColorAdjust));    // 36150 ← 17243
	SetTxt(CommWnd, TEXT("CmtyClan_Sub6_Txt04"), Loc(kLocEmblemBord));     // 36301 ← 17242
	SetTxt(CommWnd, TEXT("CmtyClan_Sub6_Txt05"), Loc(kLocColorAdjust));    // 36320 ← 17243
	SetButtonCaption(CommWnd, TEXT("CmtyClan_Sub6_SaveBtn"),   Loc(kLocEmblemSave));   // 36310 ← 17244
	SetButtonCaption(CommWnd, TEXT("CmtyClan_Sub6_RecallBtn"), Loc(kLocEmblemUndo));   // 36311 ← 17245
}

// ★ ПОРТ `cUI_RefreshTabControlsVisibility@0xBC5F80` — страница 1 «Права членов клана».
//   Эталон ДОСЛОВНО:
//     for (i=0;i<2;++i) {
//        uiHideThreeControls_60E0(this + 7*i + 110);           // снять три галочки звания
//        for (j=0;j<3;++j)
//           SetVisibleState(chk[i][j], sub_92E478(this, i+2, j+1) ? 1 : 0);
//     }
//     SetVisibleState(chk[0][0], 1); sub_9350A2(chk[0][0]);    // ← ВСЕГДА взведена и заперта
//     SetVisible(this[265], 0);                                // скрыть полосу 14101
//   ⚠ `SetVisibleState` у контрола-`CheckButton` — это состояние ГАЛОЧКИ, а не видимость
//     (закон «имена врут»). `sub_9350A2` = `set_o261@0xBC60B0`: ставит контролу байт +609 = 1,
//     то есть запирает его от ввода.
//   Звания в таблице — 2 (`CmtyClan_Position1`) и 3 (`CmtyClan_Position2`); права 1, 2, 3
//   (по подписям 17142 «Управление кандидатами», 17143 «Написать объявление», 17144
//   «Использовать ОД»). Маски берутся из блока 4123 (`FAsdaClanBaseInfo::RankPermission`).
void UAsdaCommunityClanSubsystem::RefreshRankPermPage()
{
	if (!CommWnd) { return; }
	for (int32 R = 0; R < kRankRows; ++R)
	{
		for (int32 M = 0; M < kRankPerms; ++M)
		{
			const bool bOn = Info.RankPermission(R + 2, M + 1);
			SetCheck(FString::Printf(TEXT("CmtyClan_PosFunc_Btn%d_%d"), R + 1, M + 1), bOn, true);
		}
	}
	// 1:1 хвост эталона: первая галочка (звание 2, право 1 «Управление кандидатами») ВСЕГДА
	//   взведена и заперта — заместитель главы это право имеет всегда и снять его нельзя.
	SetCheck(TEXT("CmtyClan_PosFunc_Btn1_1"), true, false);
	SetBottomBarVisible(false);   // `CControl_SetVisible(this[265], 0)`
	UE_LOG(LogAsdaClanTab, Log,
		TEXT("[клан-вкладка] страница «Права членов клана»: маски блока 4123 %s"),
		Info.bValid ? TEXT("применены") : TEXT("НЕ получены — все галочки сняты"));
}

// Галочка страницы прав: `bEnabled == false` — контрол заперт (`set_o261` эталона).
void UAsdaCommunityClanSubsystem::SetCheck(const FString& Idc, bool bChecked, bool bEnabled)
{
	if (!CommWnd) { return; }
	if (UCheckBox* C = Cast<UCheckBox>(AsdaCommunityUi::Find(CommWnd, Idc)))
	{
		C->SetIsChecked(bChecked);
		C->SetIsEnabled(bEnabled);
		return;
	}
	UE_LOG(LogAsdaClanTab, Warning, TEXT("[клан-вкладка] галочка «%s» в разметке не найдена"), *Idc);
}

// ★ ПОРТ `listUI_RefreshPage7Rows@0xBC3910` — страница 2 «Ранги членов клана».
//   Эталон ДОСЛОВНО:
//     v14 = size(список членов) − 2;                 // ★ именно −2, а не −1
//     totalPages = v14 / 7;  if (>0) ++totalPages; else totalPages = 1;
//     v13 = 7 * (page − 1);  v12 = 0;
//     for (i = 0; i < 7; ++i) {
//        ok = listGetElementByIndex(список, v13 + i + 1, rec);   // ★ индекс 1-BASED
//        if (ok) { if (rec[33] == 1) continue;                   // ★ главу клана пропускаем
//                  name[v12] = rec+5;  lv[v12] = rec[26];
//                  combo[v12].selected = 5 − rec[33];  points[v12] = rec[34..37]; }
//        else      uiComboBox_ClearTextsAndSelect(row v12);
//        ++v12; }
//     page1 = page; page2 = totalPages;              // ТЕ ЖЕ контролы 14234/14235 и те же поля
//                                                    // wnd[493]/wnd[494], что у состава клана
//   ⚠ ДЕФЕКТ ОРИГИНАЛА, ВОСПРОИЗВОДИМ 1:1: `continue` пропускает и `++v12`, поэтому строка главы
//     клана слот не занимает, а последняя строка страницы остаётся от прошлой отрисовки.
//     «Починка» сдвинула бы нумерацию строк относительно кнопок «Изгнание» 35624+20f, у которых
//     номер жёстко связан с номером строки, — поэтому не чиним.
void UAsdaCommunityClanSubsystem::RefreshGradePage()
{
	if (!IsBoundTo(CommWnd)) { return; }
	using namespace AsdaCommunityUi;

	int32 Row = 0;
	for (int32 F = 0; F < kGradeRows; ++F) { GradeRowMember[F] = -1; }   // ★ 01.11.2026: карта «строка → запись»
	if (CVarGradeListAllMembers.GetValueOnGameThread() != 0)
	{
		// ★ 04.11.2026: все не-главы в порядке списка, по 7 на страницу (разбор — у ключа).
		TArray<int32> Eligible;
		for (int32 I = 0; I < Members.Num(); ++I) { if (Members[I].Grade() != 1) { Eligible.Add(I); } }
		TotalPages = FMath::Max(1, FMath::DivideAndRoundUp(Eligible.Num(), kGradeRows));
		Page = FMath::Clamp(Page, 1, TotalPages);
		for (int32 R = 0; R < kGradeRows; ++R)
		{
			const int32 K = kGradeRows * (Page - 1) + R;
			if (!Eligible.IsValidIndex(K)) { ClearGradeRow(R); continue; }
			const FAsdaClanMemberRecord& Rec = Members[Eligible[K]];
			GradeRowMember[R] = Eligible[K];
			SetTxt(CommWnd, FString::Printf(TEXT("CmtyClan_Sub3_Name%d"), R), Rec.Name());
			SetTxt(CommWnd, FString::Printf(TEXT("CmtyClan_Sub3_Lv%d"), R), FString::FromInt((int32)Rec.Level()));
			SetTxt(CommWnd, FString::Printf(TEXT("CmtyClan_Sub3_Points%d"), R), FString::FromInt(Rec.Points()));
			const int32 Item = 5 - (int32)Rec.Grade();
			if (GradeCombos.IsValidIndex(R) && GradeCombos[R] && GradeCombos[R]->IsReady())
			{
				GradeCombos[R]->SetSelectedIndex((Item >= 0 && Item < kGradeCombo) ? Item : -1);
			}
			else
			{
				SetTxt(CommWnd, FString::Printf(TEXT("CmtyClan_Sub3_Combo%d"), R), (Item >= 0 && Item < kGradeCombo) ? Loc(kLocGradeItem0 + Item) : FString());
			}
			++Row;
		}
		SetTxt(CommWnd, TEXT("CMTY_CLAN_PAGE1"), FString::FromInt(Page));        // 14234
		SetTxt(CommWnd, TEXT("CMTY_CLAN_PAGE2"), FString::FromInt(TotalPages));  // 14235
		UE_LOG(LogAsdaClanTab, Log, TEXT("[клан-вкладка] страница «Ранги членов клана»: страница %d/%d, строк %d (не-глав %d из %d)"),
			Page, TotalPages, Row, Eligible.Num(), Members.Num());
		return;
	}

	const int32 LastIdx = Members.Num() - 2;
	TotalPages = LastIdx / kGradeRows;
	if (TotalPages > 0) { ++TotalPages; } else { TotalPages = 1; }
	Page = FMath::Clamp(Page, 1, TotalPages);

	const int32 Base = kGradeRows * (Page - 1);
	for (int32 i = 0; i < kGradeRows; ++i)
	{
		const int32 Abs = Base + i + 1;   // 1-BASED, как в эталоне
		if (Members.IsValidIndex(Abs))
		{
			const FAsdaClanMemberRecord& R = Members[Abs];
			if (R.Grade() == 1) { continue; }   // 1:1: главу клана в список званий не кладём
			GradeRowMember[Row] = Abs;
			SetTxt(CommWnd, FString::Printf(TEXT("CmtyClan_Sub3_Name%d"), Row), R.Name());
			SetTxt(CommWnd, FString::Printf(TEXT("CmtyClan_Sub3_Lv%d"), Row),
				FString::FromInt((int32)R.Level()));
			SetTxt(CommWnd, FString::Printf(TEXT("CmtyClan_Sub3_Points%d"), Row),
				FString::FromInt(R.Points()));
			// Комбо звания: эталон выбирает пункт `5 − звание` из списка loc 17041…17044
			//   (`CClass_DrawText(combo, loc(n + 17041))` в построителе), то есть подпись равна
			//   loc(17041 + 5 − звание) = loc(17046 − звание) — та же формула, что у колонки
			//   «Звание» в составе клана. Движковый `UComboBoxString` порт не показывает
			//   (`ShownVisibilityFor`), видимую подпись несёт узел `<idc>_Text`, куда пишет `SetTxt`.
			const int32 Item = 5 - (int32)R.Grade();
			// ★ 01.11.2026: живой список звания (`UAsdaJuiComboBox`) ставит подпись сам; без него — прежняя подпись.
			if (GradeCombos.IsValidIndex(Row) && GradeCombos[Row] && GradeCombos[Row]->IsReady())
			{
				GradeCombos[Row]->SetSelectedIndex((Item >= 0 && Item < kGradeCombo) ? Item : -1);
			}
			else
			{
				SetTxt(CommWnd, FString::Printf(TEXT("CmtyClan_Sub3_Combo%d"), Row),
					(Item >= 0 && Item < kGradeCombo) ? Loc(kLocGradeItem0 + Item) : FString());
			}
		}
		else
		{
			ClearGradeRow(Row);
		}
		++Row;
		if (Row >= kGradeRows) { break; }
	}
	// ★ 01.11.2026 [ИСПРАВЛЕНИЕ] (`asda.Clan.GradeRowMap`): эталон при пропуске главы строку не сдвигает, и последние строки
	//   остаются с текстом прошлой страницы (`uiSetSixControlsFieldAndShow` чистит только шесть). Чистим недозаполненные.
	if (AsdaClanGradeRowMapOn()) { for (int32 F = Row; F < kGradeRows; ++F) { ClearGradeRow(F); } }

	SetTxt(CommWnd, TEXT("CMTY_CLAN_PAGE1"), FString::FromInt(Page));        // 14234
	SetTxt(CommWnd, TEXT("CMTY_CLAN_PAGE2"), FString::FromInt(TotalPages));  // 14235
	UE_LOG(LogAsdaClanTab, Log,
		TEXT("[клан-вкладка] страница «Ранги членов клана»: страница %d/%d, строк заполнено %d"),
		Page, TotalPages, Row);
}

// `uiComboBox_ClearTextsAndSelect@0xBC0530`: три текста строки в "" и сброс выбора комбо.
void UAsdaCommunityClanSubsystem::ClearGradeRow(int32 Row)
{
	if (!CommWnd) { return; }
	using namespace AsdaCommunityUi;
	SetTxt(CommWnd, FString::Printf(TEXT("CmtyClan_Sub3_Name%d"),   Row), FString());
	SetTxt(CommWnd, FString::Printf(TEXT("CmtyClan_Sub3_Lv%d"),     Row), FString());
	SetTxt(CommWnd, FString::Printf(TEXT("CmtyClan_Sub3_Points%d"), Row), FString());
	SetTxt(CommWnd, FString::Printf(TEXT("CmtyClan_Sub3_Combo%d"),  Row), FString());
	// ★ 01.11.2026: живой список звания — без выбора (`CControl_SetSelectedItem` эталона).
	if (GradeCombos.IsValidIndex(Row) && GradeCombos[Row] && GradeCombos[Row]->IsReady()) { GradeCombos[Row]->SetSelectedIndex(-1); }
}

// ★ ПОРТ `uiTradeListPopulate5Rows@0xBC6210` — страница 4 «Передача полномочий главы клана».
//   Эталон ДОСЛОВНО:
//     for (c=0;c<5;++c) wnd_ClearThreeTexts(row[c]);
//     for (srcIdx = 0; srcIdx < size; ++srcIdx) {
//        ok = listGetElementByIndex(список, srcIdx, rec);      // ★ здесь индекс 0-BASED
//        if (ok == 1 && rec[33] == 2) {                        // ★ только звание 2 (заместители)
//            row[vis] = { rec+5, "%d" от rec[26], "%d" от rec[34..37] };
//            if (vis >= 4) { SetVisible(this[265], 0); return; }
//            ++vis; } }
//     SetVisible(this[265], 0);
void UAsdaCommunityClanSubsystem::RefreshMasterTransferPage()
{
	if (!IsBoundTo(CommWnd)) { return; }
	using namespace AsdaCommunityUi;

	for (int32 R = 0; R < kMasterRows; ++R) { ClearMasterRow(R); }

	int32 Vis = 0, Shown = 0;
	for (int32 i = 0; i < Members.Num(); ++i)
	{
		const FAsdaClanMemberRecord& R = Members[i];
		if (R.Grade() != 2) { continue; }   // 1:1: в списке только заместители главы
		SetTxt(CommWnd, FString::Printf(TEXT("CmtyClan_Sub5_Name%d"), Vis), R.Name());
		SetTxt(CommWnd, FString::Printf(TEXT("CmtyClan_Sub5_Lv%d"), Vis),
			FString::FromInt((int32)R.Level()));
		SetTxt(CommWnd, FString::Printf(TEXT("CmtyClan_Sub5_Points%d"), Vis),
			FString::FromInt(R.Points()));
		++Shown;
		if (Vis >= kMasterRows - 1) { break; }   // 1:1 `if (visibleRow >= 4) return`
		++Vis;
	}
	SetBottomBarVisible(false);   // 1:1: обе ветки выхода гасят полосу 14101
	UE_LOG(LogAsdaClanTab, Log,
		TEXT("[клан-вкладка] страница «Передача полномочий»: заместителей показано %d из %d членов"),
		Shown, Members.Num());
}

// `wnd_ClearThreeTexts@0xBC05D0` для строки 35920 + 20*m.
void UAsdaCommunityClanSubsystem::ClearMasterRow(int32 Row)
{
	if (!CommWnd) { return; }
	using namespace AsdaCommunityUi;
	SetTxt(CommWnd, FString::Printf(TEXT("CmtyClan_Sub5_Name%d"),   Row), FString());
	SetTxt(CommWnd, FString::Printf(TEXT("CmtyClan_Sub5_Lv%d"),     Row), FString());
	SetTxt(CommWnd, FString::Printf(TEXT("CmtyClan_Sub5_Points%d"), Row), FString());
}

// ★ ПОРТ `uiInvFilterPanelRefresh@0xBC6410` — страница 5 «Герб клана».
//   Эталон: скрыть полосу 14101 → `uiSlotGridDrawIcons` (сетка 15 гербов 36170…36184) →
//   `uiInvSlotDrawIcons` (сетка 15 рамок 36340…36354) → при флаге [65] блока 4123 == 1
//   `uiNumberDisplay_Refresh`, иначе `uiNumberDisplay_Reset` (сам флаг клана).
//   ⚠ ЧТО ПЕРЕНЕСЕНО: гашение полосы 14101 и все подписи страницы (`BindManagePageCaptions`).
//   ⚠ ЧТО НЕ ПЕРЕНЕСЕНО И ПОЧЕМУ: рисование герба. Картинки лежат в `./Data/Clan_Sign.bin`
//     (`CClanFlag_LoadSignTable@0xBC8190`), а сам герб — 40 байт блока 4123 ([66..105]), формат
//     которых реверсом НЕ РАЗОБРАН (`Reports/audit_clan.md` §4 п.5). Выдумывать раскладку
//     нельзя (закон `never-invent-only-original`), поэтому иконки, три пары ползунков RGB и
//     кнопки 36310/36311 остаются без поведения — перечислено в отчёте
//     `Reports/audit_2026-09-02/fix_clan_tab_rework.md`.
void UAsdaCommunityClanSubsystem::RefreshEmblemPage()
{
	SetBottomBarVisible(false);
	// ★ 01.11.2026 (clan-window) [ОРИГИНАЛ]: формат 40 байт разобран (`UAsdaClanFlag`, пакет 6542 / блок [66..105]) — теперь
	//   страница работает целиком: сетки символов и рамок, затем герб клана из блока (флаг [65] == 1) либо сброс.
	if (Info.bValid && Info.HasEmblem() == 1) { EmblemLoadFromBlock(); } else { EmblemReset(); }   // обе рисуют сетки и превью
	UE_LOG(LogAsdaClanTab, Log, TEXT("[клан-вкладка] страница «Герб клана»: флаг [65] = %d, %s"),
		Info.bValid ? (int32)Info.HasEmblem() : -1, *DebugEmblemState());
}
