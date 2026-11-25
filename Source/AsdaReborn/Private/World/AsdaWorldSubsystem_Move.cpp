// Copyright (c) 2026 @sitxovski (Discord)
// AsdaWorldSubsystem_Move.cpp — ★ R3-c 12.11.2026 (разрез AsdaWorldSubsystem.cpp, дословный перенос): ход сущностей: 4007/4062/6071, грант своего хода, пересинхронизация после смены скорости.
//   Общее для частей — в AsdaWorldSubsystem_Private.h; шапка и реверс — в AsdaWorldSubsystem.cpp.
#include "World/AsdaWorldSubsystem.h"
#include "AsdaDevConsole.h"   // ★ античит А2: ASDA_DEV_CVAR / ASDA_DEV_CONSOLE — дев-ключи и команды вне Shipping
#include "UI/AsdaNoticeColors.h"   // ★ U12 (В1 дедупа №2): эталонные цвета нотисов одной точкой
#include "UI/AsdaNotice.h"              // ★ 27.09.2026: плашка 221 для отказов 4007 (канал обработчика `Recv_EntityMove`)
#include "HAL/IConsoleManager.h"        // ключ `asda.Move.RefusePlank`
#include "Resources/AsdaModelAnimSubsystem.h"   // Д2/Д8/Д9: ресурс только через менеджер

#include "AsdaNetSubsystem.h"
#include "AsdaSelf.h"   // ★ «это я?» — единая точка своего номера (E2, 14.09.2026)
#include "World/AsdaPlayerCharacter.h"
#include "UI/AsdaWindowSubsystem.h"
#include "UI/AsdaChatSubsystem.h"            // ★ системный чат CHAT_NEW_SYSTEM + оверлеи A/B — все лут-строки
#include "UI/AsdaMessageSubsystem.h"         // ★★★ ЕДИНЫЙ маршрутизатор сообщений (категория → окно по таблице)
#include "Framework/Application/SlateApplication.h"
#include "Npc/AsdaMobActor.h"
#include "World/AsdaCoord.h"                 // ★ Аудит №3 (B1/B2): единый перевод координат и тайловая сетка
#include "World/AsdaPlayerController.h"      // снятие таргета при смерти цели-моба (эталон SetActionState(10))
#include "Resources/AsdaGameUserSettings.h"  // ★ 17.10.2026 (Ф5): режим показа цифр урона `DamageTextMode`
#include "World/AsdaFxLightSettings.h"         // ★ волна 3 п.16 (15.09.2026): AsdaWeaponGlow — имя свечения заточки (одна таблица баз)
// тип оружия атакующего (импакт battk01..04)   ★ R3: комментарий снятого включения "Items/AsdaItemTableSubsystem.h" (файл им не пользовался) — сохранён
#include "UI/AsdaSkillDatabase.h"            // ★ Аудит №3: поля записи навыка — только отсюда
#include "UI/AsdaEffectsSubsystem.h"         // ★ состояния мобов: 4017 бафф-стеки и бафф отражения
#include "ProceduralMeshComponent.h"               // ★ 09.11: площадка города по рельефу
#include "Engine/StaticMesh.h"
#include "AsdaDataTableCache.h"             // AsdaDt::Cached — объекты таблиц имён для сильного удержания (HoldEntityNameTables)
#include "Misc/ConfigCacheIni.h"             // GConfig — опции показа ангела (FxConfig)
#include "AsdaEffectActor.h"                 // эффекты персонажей: крылья/кольца soulmate (follow-кость)
#include "ProceduralMeshComponent.h"         // asda.VerifyWeaponGlow: чтение мировых вершин секций эффекта
// loc-нотисы подбора (1011/1012/1030)   ★ R3: комментарий снятого включения "Text/AsdaTextSubsystem.h" (файл им не пользовался) — сохранён
// UAsdaRichText::ApplyFormatted (единая точка текста)   ★ R3: комментарий снятого включения "UI/AsdaMarkup.h" (файл им не пользовался) — сохранён
#include "Login/AsdaLoginData.h"             // DecodeCp1251 (имена монстров в cp1251)
#include "Engine/World.h"
#include "Engine/GameInstance.h"
#include "Kismet/GameplayStatics.h"
#include "UObject/UObjectGlobals.h"
#include "World/AsdaCombatTrace.h"           // ★ 18.10.2026: журнал боя (ASDA_CTRACE) — полный цикл действий клиента в бою
#include "AsdaWorldSubsystemInternal.h"   // ★ 23.10.2026 разнос: общее для частей AsdaWorldSubsystem*.cpp (LogAsdaWorld, журнал боя, чистые хелперы)
#include "AsdaWorldSubsystem_Private.h"   // ★ R3-c 12.11.2026: общее для частей AsdaWorldSubsystem*.cpp

// 4007/4009: движение сущностей. Свой userIdx → павн игрока; чужие (мобы/игроки) → по NetRenderId.
void UAsdaWorldSubsystem::OnNetEntityMove(int32 UserIdx, int32 Result, FVector2D CurGame, FVector2D DstGame, float Speed, bool bStop,
	int32 SubType, int32 MoveTgtRid)
{
	UAsdaNetSubsystem* Net = GetGameInstance()->GetSubsystem<UAsdaNetSubsystem>();
	UWorld* W = GetGameInstance()->GetWorld();
	if (!Net || !W) { return; }

	// ★ Self = МИР-СЛОТ SelfUserNum из 4003 (эталон CharManager_FindByUserIdx: единое пространство userIdx
	//   сущностей карты; живой лог: гранты self приходят с uidx=0=SelfUserNum). НЕ UserIndex (аккаунт-индекс
	//   логина, 75180) — с ним ветка мертва: гранты result=1 и античит-снапы result=5 игрока не доходили.
	if (AsdaSelf::IsSelfUserIdx(Net, UserIdx))   // E2: до 4003 «своим» не считается никто
	{
		AAsdaPlayerCharacter* Ch = Cast<AAsdaPlayerCharacter>(LocalPlayerActor);
		if (!Ch) { return; }
		// ★ 4009 (`Recv_EntityStop@0xF3AB50`): снап в cur. Поля +3/+25 и бит 2 эталон здесь НЕ трогает.
		if (bStop)
		{
			// ★ 22.10.2026 (локальный старт, ревизия ВАЖНО-2): 4009 — ответ на 4008, ушедший до нового 4006; ход уже начат локально из
			//   этой самой точки (сервер на 4008 встал туда же) — снап назад не нужен, грант нового 4006 поведёт дальше.
			const AAsdaPlayerController* StopPC = Cast<AAsdaPlayerController>(GetGameInstance()->GetFirstLocalPlayerController());
			if (StopPC && StopPC->ShouldSkipStopSnap(CurGame))
			{
				UE_LOG(LogAsdaWorld, Log, TEXT("[ход] свой 4009 в (%.0f,%.0f) = точка локального старта — без снапа (ход уже идёт)"), CurGame.X, CurGame.Y);
				return;
			}
			Ch->NetSnap(CurGame);
			return;
		}
		AAsdaPlayerController* SelfPC = Cast<AAsdaPlayerController>(GetGameInstance()->GetFirstLocalPlayerController());
		const bool bEtalon = AAsdaPlayerController::IsEtalonSpeedSync();
		// ★★★ 27.09.2026, Р2: эталон ПЕРВОЙ строкой после поиска сущности (до всех гейтов, 0xF3C2F1) пишет
		//   renderId цели хода из пакета +25 в старшее слово `targetRenderId`. Ниже, в своей ветке case 0 — −1.
		Ch->MoveTargetRenderId = MoveTgtRid;
		// ★ 27.09.2026 (эталонная схема): гейт `Recv_EntityMove` — `field_8 != 6 && (field_C & 1) == 0`
		//   (0xF3C2FF…0xF3C314). `field_8` = состояние действия, 6 = МЁРТВ; мёртвому эталон пакет не исполняет
		//   ВОВСЕ — даже бит 2 не снимает (снимет сторож). Бит занятости `field_C & 1` в порте не заведён —
		//   реестр отложенного, «Биты занятости» №2.
		if (bEtalon && SelfPC && SelfPC->IsSelfDead())
		{
			UE_LOG(LogAsdaWorld, Log, TEXT("[ход] свой 4007 result=%d пришёл мёртвому — эталон его не исполняет"), Result);
			return;
		}
		// ★ Эталон 4007[0]: 1=иди (грант); 5=АНТИ-ЧИТ → СТОП + телепорт в последнюю разрешённую серверную
		//   точку (cur); 0=отказ хода (сервер: гейт состояния / та же клетка / чужой номер — `CharMove@0x544EB0`);
		//   прочее=нотис. НЕ снапать на 0 (иначе движение вообще запрещено).
		if (Result == 0) { Ch->MoveTargetRenderId = -1; }   // эталон case 0: `HIWORD(targetRenderId) = -1`
		// ★ 22.10.2026: локальный старт (`asda.Move.LocalStart`, разбор — AsdaPlayerKeyboardEtalon.cpp): 1/5 — ответ принят/снап ниже;
		//   отказ — плавный возврат в точку сервера (до прочей обработки отказа: маршрут закроет `OnSelfMoveRefused`).
		if (SelfPC)
		{
			if (Result == 1 || Result == 5) { SelfPC->OnLocalStartAnswered(Result); }
			else                            { SelfPC->OnLocalStartRefused(Result); }
		}
		// ★ 25.10.2026 (ревизия sit108, М-2) [ОРИГИНАЛ]: `Recv_EntityMove` case 5 (network.c:~40145) — `player_setPositionAndTarget` +
		//   `CharFx_AutoSabzil_State(0)`: сидящий сразу в 0, без «встаёт» и без 4024 0 (`DropSitForEtalonState`, ключ `asda.Char.SitExitEtalon`).
		if (Result == 5) { Ch->DropSitForEtalonState(/*bIdleClip*/ true, TEXT("4007 код 5: поправка позиции")); Ch->NetSnap(CurGame); }
		// ★ 22.10.2026 [ИСПРАВЛЕНИЕ] грант под «станом» не исполняется (`asda.Move.StunHardStop`). Правка ревизии стана (мелочь 5): пакеты
		//   идут одним сокетом по порядку, поэтому грант запроса, принятого ДО наложения, приходит раньше пакета состояния (5272) — его
		//   исполнит обычный путь, а остановка на фронте стана погасит. Грант ПОД станом возможен только там, где сервер ход под
		//   состоянием не отклоняет: 12 (оглушение хранителя) и 14 (абсолютное оглушение) нет в `sub_484D20` (GameServer.c:52910),
		//   а 12 при наложении ещё и снимает со стека хода (:54591) — такой грант уводил бы стоящего под станом персонажа.
		// ★ 22.10.2026 (локальный старт, ревизия ВАЖНО-2): грант запроса, после которого уже ушёл 4008 (`bActionPending` снят — тап,
		//   остановка без 4006), устарел: сервер обработал 4006, затем 4008 и стоит в точке 4008 (ответ — 4009 следом). Исполнять его —
		//   шаг вперёд и снап назад на 4009. При `asda.Move.LocalStart 0` — прежнее.
		else if (Result == 1 && AAsdaPlayerController::IsLocalStart() && !Net->bActionPending)
		{
			UE_LOG(LogAsdaWorld, Log, TEXT("[ход] грант к (%.0f,%.0f) устарел: после запроса ушёл 4008 — сервер стоит в его точке, не исполняется"), DstGame.X, DstGame.Y);
		}
		else if (Result == 1 && SelfPC && SelfPC->IsStunHardStopped())
		{
			UE_LOG(LogAsdaWorld, Log, TEXT("[ход] грант к (%.0f,%.0f) пришёл под станом — не исполняется [ИСПРАВЛЕНИЕ 22.10]"), DstGame.X, DstGame.Y);
		}
		else if (Result == 1)
		{
			if (bEtalon) { ApplySelfMoveGrantEtalon(Ch, SelfPC, Net, CurGame, DstGame, Speed, SubType); }
			// ★ Ревизия WASD, МЕЛК-5 [ПОРТ]: нештатная пара `EtalonSpeedSync 0` + `KeyboardEtalon 1`. Клавиши при этом не ходят
			//   локально, а прежний путь `NetMove` исполняет ЛЮБОЙ грант — опоздавший грант клавиш после отпускания уводил
			//   бы стоящего персонажа до снапа 4009. Грант, которого никто не ждёт (клавиатура неактивна, маршрута нет,
			//   персонаж стоит), игнорируется — как `bWanted` эталонной ветки. При паре 0/0 — прежнее поведение дословно.
			else if (AAsdaPlayerController::IsKeyboardEtalon() && SelfPC && !SelfPC->IsKeyboardEtalonMoveActive()
				&& !SelfPC->HasRoute() && !Ch->IsMoving())
			{
				UE_LOG(LogAsdaWorld, Log, TEXT("[ход] грант к (%.0f,%.0f) никто не ждёт (клавиатура неактивна, маршрута нет) — игнор [ПОРТ]"),
					DstGame.X, DstGame.Y);
			}
			else         { Ch->NetMove(CurGame, DstGame, Speed); }   // прежний порт (ключ 0)
		}
		else if (bEtalon && SelfPC) { SelfPC->OnSelfMoveRefused(Result); }
		// ★ Отказ движения от СЕРВЕРА (эталон Recv_EntityMove network.c:40128-40158, SOLO в 221,
		//   золотой −11514, 10 с): 2→1013 «Недоступная область», 3→1016 «слишком далеко»,
		//   6→1019 «персонаж не может двигаться в таком состоянии», 7→2319 «перегруз ≥90 %»,
		//   8→10183 «до начала битвы перемещения запрещены», 9→10184 «зона возрождения противника».
		//   ★ РЕШЕНИЕ ЮЗЕРА: категория MoveRefused → СИСТЕМНЫЙ ЧАТ.
		//   ★★★ ВАЖНО (диагноз «Недоступная область не видна»): наш сервер result=2 НЕ ШЛЁТ —
		//   в 312 живых пакетах 4007 встречались только result 0/1/5. Основной источник 1013 в
		//   оригинале — КЛИЕНТСКИЙ (клик по непроходимой клетке, input.c:823-834 / world_map.c /
		//   ui_core.c:5328). Он реализован в AAsdaPlayerController::OnLeftClick и в миникарте.
		if (!bStop)
		{
			int32 Loc = 0;
			switch (Result)
			{
			case 2: Loc = 1013;  break;
			case 3: Loc = 1016;  break;
			case 6: Loc = 1019;  break;
			case 7: Loc = 2319;  break;
			case 8: Loc = 10183; break;
			case 9: Loc = 10184; break;
			default: break;              // 0/1/4/5 — без сообщения (эталон)
			}
			// ★★ 27.09.2026 (ревизия WASD, МЕЛК-2): канал ПО ОБРАБОТЧИКУ. `Recv_EntityMove` все шесть строк выводит
			//   `CControl_SetTooltipText(g_pChatWnd, текст, −11514, 10000)` — перезаписываемая плашка 221, без журнала и без
			//   звука (game.c, case 2/3/6/7/8/9). Журнал + error.wav — канал КЛИЕНТСКОГО 1013 (клик в непроходимую клетку,
			//   `OnLeftClick`, решение юзера про категорию MoveRefused касается его и остаётся). Серверный result 2 у клавиш
			//   возможен у края карты (`sub_4221D0` — проверка границ карты), и на каждый ответ (≈ RTT) порт писал строку
			//   журнала со звуком. `asda.Move.RefusePlank 0` — прежний маршрут (журнал + звук).
			static IConsoleVariable* const PlankVar = ASDA_FIND_DEV_CVAR(TEXT("asda.Move.RefusePlank"));
			const bool bPlank = !PlankVar || PlankVar->GetInt() != 0;
			if (Loc > 0 && bPlank)
			{
				AsdaNotice::PlankLoc(GetGameInstance(), EAsdaMsgCategory::System, Loc, AsdaNoticeColor::Gold, 10000.f, 4007);
			}
			else if (Loc > 0)
			{
				if (UAsdaMessageSubsystem* Msg = GetGameInstance()->GetSubsystem<UAsdaMessageSubsystem>())
				{
					FAsdaMsgParams P;
					P.Category = EAsdaMsgCategory::MoveRefused;
					P.LocId = Loc;
					P.SrcOpcode = 4007;
					Msg->Post(P);
				}
			}
		}
		// ★★★ 27.09.2026, Р3: после ЛЮБОГО своего 4007 (все result, включая неизвестные) эталон делает
		//   `gameFlagsClear(&game, 2)` (0xF3C722) — снимает бит «запрос хода в пути». Ведём всегда, при обоих
		//   значениях ключа; читатели бита работают только при эталонной схеме.
		Net->ClearMoveRequestInFlight(TEXT("пришёл свой 4007"));
		return;
	}
	// ★ ЭТАЛОН (Recv_EntityMove@0xF3C2C0 → CharManager_FindByUserIdx): 4007/4009 двигают ТОЛЬКО ИГРОКОВ.
	//   Мобов НЕ искать: rid-пространство мобов пересекается с userIdx игроков (живой лог: моб rid=4 И
	//   игрок Mikey uidx=4) — прежний матч NetMobs первым съедал движение игроков И глушил 5416-запрос.
	//   Движение мобов = 4019 (Recv_MobSpawnLoop) + 4015 kind=2 (Recv_CombatHit case 2).
	// Чужой ИГРОК по userIdx: 4009/анти-чит → снап, иначе интерполяция к dst со скоростью сервера.
	if (FAsdaNetPlayerEntry* PE = NetPlayers.Find(UserIdx))
	{
		if (AAsdaPlayerCharacter* A = PE->Actor.Get())
		{
			// ★ 27.09.2026, Р2: renderId цели хода соседа (эталон пишет его ДО гейтов, только из 4007). Это и есть
			//   поле, которое читает горячая клавиша отряда «взять цель помеченного соотрядника».
			if (!bStop) { A->MoveTargetRenderId = MoveTgtRid; }
			// ★ 27.09.2026: чужая ветка эталона двигает ТОЛЬКО по result 1 (`charMoveStepToCell(cur→dst, speed)`),
			//   снапит по 5 и больше ничего не делает. Сервер соседям шлёт только 1 (`CharMove` → `sub_52E6D0`
			//   с `moveBody[0] = 1`), поэтому на живом разницы нет.
			//   ★ Ревизия С3: закон ключа — «0 = прежнее ДОСЛОВНО». Эталонная ветка — под ключом 1; при ключе 0 —
			//   прежнее «любой не-5 = ход» без изменений.
			// ★ 24.10.2026 [ПОРТ] присед соседа: сервер эхом шлёт тело его 4006 — статус хода @+3 = 4 у приседа (донор/порт).
			//   Стоп (4009) несёт 0 и присед не снимает: следующий ход сам скажет, как идёт сосед.
			if (!bStop && Result == 1) { A->SetNetCrouch(SubType == AAsdaPlayerCharacter::kCrouchMoveStatus); }
			if (bStop || Result == 5) { A->NetSnap(CurGame); }
			else if (!AAsdaPlayerController::IsEtalonSpeedSync() || Result == 1) { A->NetMove(CurGame, DstGame, Speed); }
		}
		return;
	}
	// ★ НЕЗНАКОМЕЦ движется (эталон Recv_EntityMove, ветка FindByUserIdx=NULL → netSendMaskedHandshake):
	//   запросить его снапшот 5416 (ответ 5417/5418 заспавнит актора). Один запрос на userIdx.
	if (UAsdaNetSubsystem* Net2 = GetGameInstance()->GetSubsystem<UAsdaNetSubsystem>())
	{
		if (UserIdx > 0 && !Net2->RequestedUserInfo.Contains(UserIdx))
		{
			if (Net2->SendAgainUserInfo((uint16)UserIdx)) { Net2->RequestedUserInfo.Add(UserIdx); }   // ★ ревизия волны 1: в кэш — только отправленный (гейт перехода/обрыв не должны «залипать» номер)
		}
	}
}

// ★★★ 27.09.2026 — ГРАНТ СВОЕГО ХОДА ПО ЭТАЛОНУ (`Recv_EntityMove@0xF3C2C0`, case 1, байт-код 0xF3C362…0xF3C5C1;
//   разбор — `Reports/owner_0927/move_4007.md` §2). Дословно:
//     если подтип @+3 не в 1..2 → ничего (case 1 без действия);
//     t = dist2D(cur, dst) / speed                                   — мс, за которые сервер «проходит» отрезок;
//     если у своего в списке состояний есть «Паника» (id 4, тип 0) → метка = сейчас   (0xF3C3C3: ход ведёт сервер,
//       своего 4006 не было — поправка на путь пакета не нужна; id 4 = FEAR: индекс 4 таблицы
//       `ProcessState_SKILL_STATE_*` сервера и «Паника / fearu01» в таблице эффектов клиента — два метода);
//     t −= |сейчас − метка| / 2.0                                    — половина пути пакета туда-обратно (0x13980F8 = 2.0);
//     метка = сейчас;
//     t > 0 → v = dist2D(позиция актора, dst) / t; `charMoveToWorldPos(dst, v)` — от ТЕКУЩЕЙ позиции, без снапа;
//     t ≤ 0 → `Net_SendActionPacket(dst)` (4008 в точке dst при незакрытом ходе), позиция = dst, покой.
//   Часы — `timeGetTime()` кадра (мс). Метка у эталона — float (кванты до 32 мс на больших аптаймах); у нас double.
//   ОТСТУПЛЕНИЯ [ПОРТ], помечены:
//     · грант во время ходьбы КЛАВИШАМИ — как прежде (только скорость): клавиатура схемой не охвачена;
//     · «опоздавший» грант, когда хода уже не ждут (нет маршрута и не «Паника») — персонажа не трогаем: эталон
//       держит стоп до ответа, поэтому у него такой грант двигать некого, а у нас остановки клавиатуры, NPC, лута
//       стоп не держат — без гейта персонаж убежал бы после 4008;
//     · скорость вне (0.0001; 5) — прежний путь `NetMove` (эталон делил бы на ноль / принял бы мусор).
void UAsdaWorldSubsystem::ApplySelfMoveGrantEtalon(AAsdaPlayerCharacter* Ch, AAsdaPlayerController* PC, UAsdaNetSubsystem* Net,
	const FVector2D& CurGame, const FVector2D& DstGame, float Speed, int32 SubType)
{
	if (!Ch || !Net) { return; }
	// ★ 24.10.2026 [ПОРТ] подтип 4 — наш присед (4006 @+3 = 4, ключ asda.Crouch.NetStatus): грант исполняется, как подтип 2.
	//   Свой грант — эхо НАШЕГО 4006, поэтому 4 принимается и тогда, когда присед уже снят (грант пришёл после отпускания
	//   Ctrl — иначе ход бы застрял). Эталон-клиент такого 4006 не шлёт; иной подтип вне 1..2 — по-прежнему без хода.
	const bool bCrouchGrant = (SubType == AAsdaPlayerCharacter::kCrouchMoveStatus);
	if ((SubType <= 0 || SubType > 2) && !bCrouchGrant)
	{
		UE_LOG(LogAsdaWorld, Log, TEXT("[ход] грант с подтипом %d — эталон исполняет только 1..2, ход не начат"), SubType);
		return;
	}
	// Клавиатура: курсом рулит контроллер, от сервера — только скорость (прежнее поведение, см. `NetMove`).
	if (Ch->IsMoving() && !Ch->IsMovingToPoint()) { Ch->NetMove(CurGame, DstGame, Speed); return; }
	if (!(Speed > 0.0001f && Speed < 5.f)) { Ch->NetMove(CurGame, DstGame, Speed); return; }

	bool bFear = false;
	if (const UAsdaEffectsSubsystem* Fx = GetGameInstance()->GetSubsystem<UAsdaEffectsSubsystem>())
	{
		bFear = (Fx->FindByTypeSub(Fx->SelfUserIdx(), /*Id=*/4, /*Type=*/0) != nullptr);
	}
	const double NowMs = UAsdaNetSubsystem::MoveClockMs();
	const double D1 = FVector2D::Distance(CurGame, DstGame);
	double T = D1 / (double)Speed;
	if (bFear) { Net->MoveRequestStampMs = NowMs; }
	const double HalfRtt = FMath::Abs(NowMs - Net->MoveRequestStampMs) / 2.0;
	T -= HalfRtt;
	Net->MoveRequestStampMs = NowMs;

	// ★ 27.09.2026 (`asda.Move.KeyboardEtalon 1`): ход клавишами идёт тем же грантом — пока клавиатура активна
	//   (эталонный `g_bWorldTickActive == 0`, клавиша держится и её 4006 ушёл), грант клавиш тоже «ждут».
	//   После отпускания клавиатура неактивна, и поздний грант не двигает — у эталона его тут же гасит 4009
	//   ответа на 4008 отпускания (снап в точку отпускания, флаг [228]); итог тот же. [ПОРТ]
	const bool bWanted = (PC && PC->HasRoute()) || bFear || (PC && PC->IsKeyboardEtalonMoveActive());
	if (!bWanted)
	{
		UE_LOG(LogAsdaWorld, Log, TEXT("[ход] грант к (%.0f,%.0f) пришёл, когда хода уже не ждут (нет маршрута) — персонаж не трогается [ПОРТ]"),
			DstGame.X, DstGame.Y);
		return;
	}
	if (T > 0.0)
	{
		const FVector2D NowGame = AsdaCoord::UeToGame2D(Ch->GetActorLocation());
		const double D2 = FVector2D::Distance(NowGame, DstGame);
		float V = (float)(D2 / T);
		// ★ 22.10.2026 (локальный старт, ревизия: «просадка ~8 % после гранта»): клиент тронулся на ½RTT раньше сервера и идёт впереди на
		//   ½RTT·v — подгонка к серверному времени замедляла его на эту долю. На гранте локального старта скорость не ниже серверной
		//   (впереди ≤ ½RTT, запас сервера 1000 мс); если клиент отстал (потолок ВАЖНО-4) — догоняет подгонкой.
		if (PC && PC->bLastGrantWasLocal) { V = FMath::Max(V, Speed); PC->bLastGrantWasLocal = false; }
		UE_LOG(LogAsdaWorld, Log, TEXT("[ход] грант (эталон): путь сервера %.0f / %.4f = %.0f мс − ½RTT %.0f мс → %.0f мс; ")
			TEXT("от актора %.0f → скорость %.4f%s"), D1, Speed, D1 / Speed, HalfRtt, T, D2, V, bFear ? TEXT(" [Паника]") : TEXT(""));
		if (V > 0.f) { Ch->NetMoveFromActor(DstGame, V); }
		else         { Ch->NetSnap(DstGame); }   // актор уже в dst (D2 = 0): эталон идёт с нулевой скоростью — у нас сразу покой
	}
	else
	{
		UE_LOG(LogAsdaWorld, Log, TEXT("[ход] грант опоздал (время пути %.0f мс ≤ 0): 4008 в (%.0f,%.0f), позиция = цель"),
			T, DstGame.X, DstGame.Y);
		Net->SendStop4008((float)DstGame.X, (float)DstGame.Y);   // гейт `bActionPending` — внутри, как у эталона
		Ch->NetSnap(DstGame);
	}
}

// ★★★ 4062 `netRecvEntityPosSync@0xF3C150` — позиционный синк сущности (хвост P4, COMBAT_TODO §3).
//   ★ 16.09.2026, ПЕРЕПИСАНО ПО РЕВИЗИИ. Первая версия расходилась с эталоном по всем трём решениям, и в игре
//   это было ВРЕДНЕЕ отсутствия обработчика (соседи уносились за карту, свой персонаж терял управление).
//
//   ЧТО ДЕЛАЕТ ЭТАЛОН (три ветки, game.c:551884-551928):
//     1) СВОЙ персонаж (`effectSlots[112].field_40` — это флаг «локальный игрок», оффсет 0x2E734,
//        `client/logic/entity_character.md:416`, а вовсе не «слот эффектов», как я сперва прочёл):
//        тип-байт, цель и скорость кладутся в поля ОТЛОЖЕННОГО ХОДА (52127…52130) — ход исполняет покадровый
//        тик (game.c:83907), а не сам обработчик пакета;
//     2) ЧУЖОЙ и тип-байт != 0 → `charMoveStepToCell(cur → dest, speed)` — интерполяция;
//     3) ЧУЖОЙ и тип-байт == 0 → НИЧЕГО (это «стоп», как case 0 у 4007).
//
//   ПОЧЕМУ У НАС ТАК (уточнено 16.09.2026, третья ревизия). Для СВОЕГО мы КОРРЕКТИРУЕМ уже идущий клик-мув:
//   правим цель текущего узла маршрута и зовём `NetMove` с серверной скоростью. Нового движения не начинаем —
//   если клик-мува нет, обработчик молчит (у эталона так же: без взведённого хода читателям полей нечего
//   исполнять). Маршрут НЕ отменяем: отмена рвёт остаток пути и глушит обязательный 4008 на финише.
//   Мобов 4062 не касается: эталон ищет `CharManager_FindByUserIdx`, а rid-пространство мобов пересекается
//   с userIdx игроков (разбор в OnNetEntityMove). Движение мобов — 4019 и 4015 kind=2.
//   ★★★ 27.09.2026: ВЕТКА СЕРВЕРОМ НЕ ИСПОЛЬЗУЕТСЯ — наш старый сервер 4062 не шлёт никогда (три метода,
//   все живые серверные образы; `Reports/owner_0927/position_correction.md`). Код оставлен как эталон
//   на случай серверного патча. Настоящая коррекция позиции своего персонажа — 4007 result=5 и 4009,
//   их обрабатывает OnNetEntityMove (ветка «свой»).
void UAsdaWorldSubsystem::OnNetEntityPosSync(int32 UserIdx, uint8 Kind, FVector2D DstGame, float Speed)
{
	UAsdaNetSubsystem* Net = GetGameInstance() ? GetGameInstance()->GetSubsystem<UAsdaNetSubsystem>() : nullptr;
	if (!Net) { return; }

	if (AsdaSelf::IsSelfUserIdx(Net, UserIdx))
	{
		// ★★★ СВОЙ — КОРРЕКЦИЯ УЖЕ ИДУЩЕГО ХОДА, а не новый ход (уточнено ревизией 16.09.2026).
		//   Эталон кладёт цель и скорость в поля 52127…52130, но их читатели сидят в `switch(*(this+52121))`,
		//   а ключ 52121 обработчик 4062 НЕ ПИШЕТ — его ставит клик-мув. То есть сервер перебивает цель и
		//   скорость того хода, который клиент и так собирается исполнить; если хода нет — эталон не двигается.
		//   Поэтому: правим только идущий клик-мув — ПЕРЕНАЦЕЛИВАЯ текущий узел маршрута.
		//   ★★★ 16.09.2026 (третья ревизия, КРИТ): здесь стоял `CancelRoute()`, и это было ошибкой —
		//   отмена теряла остаток маршрута, `RouteRenderId` (подбег к NPC/луту) и, главное, глушила
		//   финальный 4008: `StopMoveAndNotify` живёт внутри `RouteTick`, который при пустом маршруте
		//   выходит на первой строке. Сервер остаётся в состоянии «игрок идёт» → «Сначала нужно
		//   остановиться» на любом навыке (дефект, вылеченный 10.08). Эталон маршрутов не отменяет.
		// ★ 16.09.2026 (четвёртая ревизия, СРЕДН-2): тип-байт 0 — «стоп», а не ход. Эталон его СОХРАНЯЕТ
		//   (`animParam9C = *a2`) для покадрового тика; у нас тика с этими полями нет, а трактовать «стоп»
		//   как обычный ход нельзя — персонаж уехал бы по пакету, который движением не является.
		//   Полное поведение ждёт системы состояний (записано в PORT_PENDING_ON_SYSTEMS.md).
		if (Kind == 0) { return; }
		AAsdaPlayerCharacter* Self = Cast<AAsdaPlayerCharacter>(LocalPlayerActor);
		AAsdaPlayerController* PC = Self ? Cast<AAsdaPlayerController>(GetGameInstance()->GetFirstLocalPlayerController()) : nullptr;
		// ★ 16.09.2026 (четвёртая ревизия, СРЕДН-1): движемся ТОЛЬКО если узел реально переназначен.
		//   Признак «идёт клик-мув» у персонажа для этого не годится: после запоздавшего гранта 4007 флаги
		//   снова взведены, а маршрута уже нет — и ход «в точку» без маршрута не закрылся бы финальным 4008.
		if (PC && PC->RetargetCurrentNode(DstGame))
		{
			Self->NetMove(AsdaCoord::UeToGame2D(Self->GetActorLocation()), DstGame, Speed);
		}
		return;
	}
	FAsdaNetPlayerEntry* PE = NetPlayers.Find(UserIdx);
	AAsdaPlayerCharacter* Ch = PE ? PE->Actor.Get() : nullptr;
	if (!Ch) { return; }
	if (Kind == 0) { return; }                 // «стоп» — эталон в этой ветке не делает НИЧЕГО
	// ★ 16.09.2026 (ревизия): гейта состояния здесь НЕТ и у эталона — `else if (*a2)` идёт без проверок.
	//   Прежний `IsIncapacitated()` был моей отсебятиной: мёртвый сосед переставал двигаться и после
	//   воскрешения прыгал из устаревшей точки. Единственный гейт 4062 стоит в self-ветке (`field_C & 6`),
	//   и он про ЗАНЯТОСТЬ ДЕЙСТВИЕМ, а не про смерть; в порт эти биты пока не перенесены.
	const FVector2D NowGame = AsdaCoord::UeToGame2D(Ch->GetActorLocation());
	Ch->NetMove(NowGame, DstGame, Speed);      // скорость приходит В ПАКЕТЕ, пересчёт не нужен
}

// ★★★ 04.11.2026 [ИСПРАВЛЕНИЕ ДЕФЕКТА ОРИГИНАЛА] (баг владельца «верхом ударили — транспорт пропал, через миг сервер дёргает назад»).
//   РАЗБОР (сервер `GameServer.c`, клиент `RE/etalon/network.c:40228`, самотест `asda.SelfTest.MountHitSpeed`):
//   • Удар по всаднику (урон ≥ 1 % макс. HP: PvP `sub_426B00` :17050, навык `sub_555800` :131364, моб `sub_4CA330` :80514) —
//     `sub_4F2170` (ссадить: слот 15 = −1, bcast 5454) и сразу `RecalcMoveSpeed@0x5588F0(…, 1)`: `CUser+21758` = новая скорость,
//     bcast 6071 всем в секторе, включая самого. Добровольная высадка (5455 → `PopFunctionItemState_Toggle@0x4F1F60`) — так же.
//   • Сервер ведёт ход ОТРЕЗКАМИ: длительность отрезка `CUser+21766` = путь / скорость считается ОДИН раз — при приёме 4006
//     (`CharMove@0x544EB0`, перекат в `MoveSyncProcess@0x5427D0`); тик двигает точку на Δt·(ТЕКУЩАЯ скорость) и останавливает
//     отрезок, когда прошло 21766 мс. 6071 меняет скорость, но НЕ длительность — после замедления посреди отрезка сервер
//     встаёт НЕ ДОЙДЯ до цели на (v_старая/v_новая − 1)·остаток мс, после ускорения — проскакивает её.
//   • Клиент (эталон `Recv_CharMoveToCell`: шаг пересчитывается к СОХРАНЁННОЙ цели с новой скоростью; порт — то же) доходит до
//     цели. Следующий 4006/4008 из цели: клиент впереди сервера > 1000 мс → 4007 result 5 «CharReMove Hack Position» или 4009
//     «CharStop Revision Pos» — рывок назад. Конь +60 % (0.56 → 0.35): рывок при остатке отрезка > 1.7 с — то есть на любом длинном
//     ходе кликом (живой журнал GameLog 04.11 15:31:13: «CharStop Revision Pos» + «CharMove Hack Position» у GM_sitxovski).
//   ИСПРАВЛЕНИЕ: свой 6071 посреди хода «в точку» → 4006 из текущей точки к той же цели (штатный пакет, тот же путь, что у досылки
//   статуса приседа). Сервер в ветке «идёт» сверяет точку (клиент ≈ сервер) и перекатывает отрезок с НОВОЙ скоростью — длительность
//   снова верная, грант 4007 подгоняет шаг. Не шлём: 4006 уже в пути (сервер обработал удар раньше него — отрезок и так новый),
//   после 4008 (сервер стоит — точка должна совпасть побайтно), мёртвому, под станом, цель в той же клетке (сервер ответит 0).
//   Плюс скорость прежнего гранта локального старта (`LocalPrevSpeed`, им добегают отрезок при отказе/потолке) — новая.
//   Ключ `asda.Move.SpeedChangeResync`: 1 — так; 0 — прежнее поведение (как оригинал).
static ASDA_DEV_CVAR(int32, CVarAsdaMoveSpeedChangeResync,
	TEXT("asda.Move.SpeedChangeResync"), 1,
	TEXT("[ИСПРАВЛЕНИЕ] 1 (по умолчанию) — своя новая скорость (6071: высадка с транспорта, в т.ч. от удара, бафф/дебафф скорости) посреди ")
	TEXT("хода «в точку» сразу сообщается серверу 4006 из текущей точки к той же цели: сервер пересчитывает отрезок с новой скоростью и не ")
	TEXT("дёргает назад. 0 — как оригинал (сервер доводит отрезок со старой длительностью — рывок назад после высадки на ходу)."),
	ECVF_Default);

// Счётчик отправленных пересинхронизаций (журнал, самотест `asda.SelfTest.MountHitSpeed`).
int32 GAsdaSpeedResyncCount = 0;

static void AsdaResyncSelfSegmentAfterSpeed(UGameInstance* GI, AAsdaPlayerCharacter* Ch, UAsdaNetSubsystem* Net, float OldSpeed, float NewSpeed,
	bool bApplied)
{
	if (!GI || !Ch || !Net) { return; }
	const bool bOn = CVarAsdaMoveSpeedChangeResync.GetValueOnGameThread() != 0;
	AAsdaPlayerController* PC = Cast<AAsdaPlayerController>(GI->GetFirstLocalPlayerController());
	const bool bValid = NewSpeed > 0.0001f && NewSpeed < 5.f;   // тот же фильтр, что у сетевого слоя 4007/4062/6071
	// Прежний грант, который добегают при отказе/потолке локального старта, сервер теперь ведёт с новой скоростью.
	if (bOn && bValid && PC && PC->LocalPrevSpeed > 0.f) { PC->LocalPrevSpeed = NewSpeed; }
	if (!bOn || !bValid || !bApplied) { return; }
	const TCHAR* Skip = nullptr;
	if (!Net->bInGameConnected)                         { Skip = TEXT("нет игрового соединения"); }
	else if (!Ch->IsMovingToPoint())                    { Skip = TEXT("ход не «в точку» (курсом рулит клавиатура прежней схемы)"); }
	else if (FMath::Abs(OldSpeed - NewSpeed) < 0.0005f) { Skip = TEXT("скорость не изменилась"); }
	else if (Net->IsMoveRequestInFlight())              { Skip = TEXT("4006 в пути — сервер примет его уже с новой скоростью"); }
	else if (!Net->bActionPending)                      { Skip = TEXT("после 4008 сервер стоит — отрезка нет"); }
	else if (PC && PC->IsSelfDead())                    { Skip = TEXT("мёртв"); }
	else if (PC && PC->IsStunHardStopped())             { Skip = TEXT("под станом"); }
	// Состояния, под которыми сервер отбивает 4006 кодом 6 (`sub_484D20`: лёд/камень/паутина/оглушение/сон/страх…) — отказ закрыл бы
	//   маршрут (`OnSelfMoveRefused`), а ход под ними ведёт сервер (страх) или его нет вовсе. Набор эталона `listHasAnyOfSlotTypes@0xA38970`.
	else if (PC && PC->IsMoveInputLockedByState())      { Skip = TEXT("ход запрещён состоянием (сервер ответил бы 6)"); }
	const FVector2D Cur = Ch->GetGamePosXZ();
	const FVector2D Dst = Ch->GetMoveDstGame();
	if (!Skip && (FVector2D::Distance(Cur, Dst) <= 1.0 || AsdaCoord::GameToTile(Cur) == AsdaCoord::GameToTile(Dst)))
	{
		Skip = TEXT("цель в своей клетке — отрезок кончается сам");
	}
	// Остаток пути короче 200 ед. — недобег сервера заведомо меньше порога «Hack Position» (1000 мс) при любых скоростях сервера
	//   (кламп 0…0.66, оглушение /2 ⇒ не ниже 0.175): 200·(1/0.175 − 1/0.66) ≈ 840 мс. 4006 вблизи цели рискует отказом 0 («та же
	//   клетка» по ТОЧКЕ СЕРВЕРА, `CharMove` :124532), а отказ закрыл бы маршрут.
	static constexpr double kResyncMinRemaining = 200.0;
	if (!Skip && FVector2D::Distance(Cur, Dst) < kResyncMinRemaining)
	{
		Skip = TEXT("до цели меньше 200 ед. — недобег сервера ниже порога 1000 мс");
	}
	if (Skip)
	{
		UE_LOG(LogAsdaWorld, Log, TEXT("[ход] своя скорость %.4f → %.4f: отрезок у сервера не пересчитываем (%s)"), OldSpeed, NewSpeed, Skip);
		return;
	}
	if (PC) { PC->NoteMove4006WithoutLocalStart(); }
	const bool bSent = Net->SendMove4006((float)Cur.X, (float)Cur.Y, (float)Dst.X, (float)Dst.Y, Ch->LastSent4006TargetRid);
	++GAsdaSpeedResyncCount;
	UE_LOG(LogAsdaWorld, Log, TEXT("[ход] своя скорость %.4f → %.4f посреди хода: 4006 (%.0f,%.0f)→(%.0f,%.0f), цель rid %d — сервер пересчитает ")
		TEXT("отрезок с новой скоростью [ИСПРАВЛЕНИЕ] %s"), OldSpeed, NewSpeed, Cur.X, Cur.Y, Dst.X, Dst.Y, Ch->LastSent4006TargetRid,
		bSent ? TEXT("OK") : TEXT("НЕ ОТПРАВЛЕН"));
	ASDA_CTRACE(EAsdaCT::Send, TEXT("скорость %.4f → %.4f посреди хода (6071): 4006 к той же цели (%.0f,%.0f) — пересчёт отрезка сервера"),
		OldSpeed, NewSpeed, Dst.X, Dst.Y);
}

// ★★★ 6071 `Recv_CharMoveToCell@0xF3CC90` — НОВАЯ СКОРОСТЬ движения сущности.
//   Приходит при посадке на транспорт и слезании с него (5450/5455 → сервер пересчитывает скорость
//   в `CUserMoveManager::RecalcMoveSpeed@0x5588F0` и рассылает результат) и при смене состояний скорости.
//
//   ЭТАЛОН (game.c:552167-552178) целиком: `CharManager_FindByUserIdx(userIdx)`, и если найденный
//   персонаж идёт (`getFlag_47551 == 1`) — `charMoveStepToCell(его позиция → его СОХРАНЁННАЯ цель,
//   новая скорость)`. Своего и чужого он НЕ РАЗЛИЧАЕТ: одна ветка на всех. Цели в пакете нет — только
//   чья скорость и какая.
//
//   ★ Авторитет скорости — ЦЕЛИКОМ СЕРВЕР (реверс 16.09.2026): клиент не считает её вообще, своё поле
//   скорости в 4006 оставляет нулём, а сервер перетирает его перед рассылкой. Поэтому здесь мы ничего
//   не пересчитываем — принимаем готовое число.
void UAsdaWorldSubsystem::OnNetEntitySpeed(int32 UserIdx, float Speed)
{
	UAsdaNetSubsystem* Net = GetGameInstance() ? GetGameInstance()->GetSubsystem<UAsdaNetSubsystem>() : nullptr;
	if (!Net) { return; }

	// Своего и чужого различаем ТОЛЬКО чтобы взять нужного актора: поведение у эталона одно на обоих.
	AAsdaPlayerCharacter* Ch = nullptr;
	if (AsdaSelf::IsSelfUserIdx(Net, UserIdx))
	{
		Ch = Cast<AAsdaPlayerCharacter>(LocalPlayerActor);
	}
	else if (FAsdaNetPlayerEntry* PE = NetPlayers.Find(UserIdx))
	{
		Ch = PE->Actor.Get();
	}
	if (!Ch) { return; }
	const float OldSpeed = Ch->GetMoveSpeed();   // скорость идущего шага ДО пакета (для пересинхронизации отрезка ниже)
	// Гейт «идёт» живёт внутри `SetServerMoveSpeed` — как у эталона, стоящего персонажа пакет не трогает.
	const bool bApplied = Ch->SetServerMoveSpeed(Speed);
	UE_LOG(LogAsdaWorld, Verbose, TEXT("6071 → uidx=%d скорость=%.4f %s"), UserIdx, Speed,
		bApplied ? TEXT("применена к идущему шагу") : TEXT("не применена (персонаж стоит)"));
	if (AsdaSelf::IsSelfUserIdx(Net, UserIdx))
	{
		AsdaResyncSelfSegmentAfterSpeed(GetGameInstance(), Ch, Net, OldSpeed, Speed, bApplied);
	}
}
