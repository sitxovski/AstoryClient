// Copyright (c) 2026 @sitxovski (Discord)
// UAsdaZoneSubsystem — ЗОНА «ГОРОД / НЕ ГОРОД» и всё, что оригинал на неё вешает:
//   (1) СМЕНА BGM при входе/выходе из города, (2) БАННЕР НАЗВАНИЯ ЛОКАЦИИ вверху экрана.
//
// ЭТАЛОН (адреса runtime VA, ASLR off):
//   • Признак зоны — controlBar_SlotIsActive@0x95BE40 → sub_95BE80 = БАЙТ1 4-байтного флага тайла .TER
//     (CMap: tileArray + 92*idx + 16). Бит 0x80 = город/безопасная зона. Проходимые тайлы имеют
//     ровно 0x00 (поле) или 0x80 (город) — остальные значения байта1 непроходимы, поэтому сравнение
//     «целого байта» эталона тождественно сравнению флага города.
//   • КОНЕЦ ЗАГРУЗКИ КАРТЫ (game_main.c:3390-3420, две одинаковые ветки 3805-3840):
//       g_CachedZoneAttribFlags = 100;                       // заведомо невозможное значение
//       регистрация двух BGM-слотов карты (g_bMapAmbientSound0/1 + getRecord32ByIndex@0x965FF0);
//       soundEmitterStopStream(dev, 0, 16);                  // МГНОВЕННЫЙ стоп (a2=0)
//       CSoundStream_PlayById(dev, (flags&0x80) ? 0 : 1, 0, 16);  // старт БЕЗ фейда (a3=0)
//       npcwin_DrawMapNpcImage(g_pNpcWnd);                   // баннер названия (вариант по зоне)
//   • НА ХОДУ, при смене клетки (entity_char.c:20978-21002, в апдейте позиции локального игрока):
//       flags = controlBar_SlotIsActive(...);
//       if (flags != g_CachedZoneAttribFlags && фейд не идёт)
//           cSoundSet_QueueOrSetByIndex(dev, (flags&0x80)?0:1, 16, 16);   // ФЕЙД-ОЧЕРЕДЬ (16 кадров)
//   • БАННЕР НА ХОДУ (mapUI_DrawMapEnterBanner@0xDA2EF0, world_map.c:7736) — ТОЛЬКО при входе В ГОРОД
//     (0x80 не было → стало), и не чаще, чем раз в 8000 мс (0x1F40).
//     ★★ ОТСТУПЛЕНИЕ ОТ ЭТАЛОНА (по просьбе пользователя, 2026-08-01): порт показывает баннер И при
//        ВЫХОДЕ из города в поле (обычный вариант текстуры), гард общий на оба направления.
//        Выключается CVar `asda.Zone.BannerOnLeaveTown 0` → поведение оригинала. Подробности и
//        дизасм-доказательство отсутствия показа в оригинале — в шапке AsdaZoneSubsystem.cpp.
//   • Отрисовка баннера (npcwin_DrawMapNpcImage@0xDA3330, npc_mob_pet.c:3696):
//       имя текстуры = Map_SetNameTextureByID@0xDA3700(mapId, вГороде ? 0 : 1)  →  Data/NPC/Image/<n>.TEX
//       x = ScreenW*0.5 − w/2;  y = ScreenH*0.25 − h/2;   w,h = натуральный размер текстуры
//       objStartTimedAction(ctrl, 0, 255, 2000)      → проявление 2.0 с
//       по завершении (uiWindowMsgHandler@0xD91EC0, msg 24, id 9000, arg 3):
//       objStartTimedAction(ctrl, 255, 0, 7000)      → затухание 7.0 с
//   • Контрол баннера — ШТАТНЫЙ JUI-контрол ID 9000 «VILLAGE_NAME» (gamemain, Name="NoImage_icon",
//     AlwaysTop=1); в порте это сгенерированный /Game/AsdaAssets/UI/Player_UI/HUD/WBP_VILLAGE_NAME.
//     Своего UI не изобретаем — берём этот виджет и наполняем из кода (как WBP_LoadingScreen).
//
// Данные карты (BGM обоих слотов + имена текстур) лежат в DT_MapTable (FAsdaMapRow), см.
//   Tools/asda_export_map_zone_data.py — выгрузка из заголовка .TER и свитча Map_SetNameTextureByID.
#pragma once

#include "CoreMinimal.h"
#include "Subsystems/GameInstanceSubsystem.h"
#include "Tickable.h"
#include "AsdaSessionReset.h"   // IAsdaSessionResettable — сброс состояния сессии
#include "AsdaTickGate.h"   // ★ R4, доля a: шаблон класса не тикает (общая точка)
#include "AsdaZoneSubsystem.generated.h"

class UUserWidget;
class UImage;
class UTexture2D;
class UAsdaWalkGrid;

UCLASS()
class ASDAREBORN_API UAsdaZoneSubsystem : public UGameInstanceSubsystem, public FTickableGameObject,
	public IAsdaSessionResettable
{
	GENERATED_BODY()

public:
	virtual void Initialize(FSubsystemCollectionBase& Collection) override;
	virtual void Deinitialize() override;

	// ── IAsdaSessionResettable (К-3) ──
	// Кэш зоны уже пересчитывается по смене CurrentMapId; здесь снимается баннер.
	virtual void ResetSessionState(EAsdaSessionResetReason Reason) override;

	virtual void Tick(float DeltaTime) override;
	virtual TStatId GetStatId() const override { RETURN_QUICK_DECLARE_CYCLE_STAT(UAsdaZoneSubsystem, STATGROUP_Tickables); }
	// ★ R4, доля a (14.11.2026): шаблон класса (CDO) не тикает — `AsdaTickGate.h`, ключ `asda.Perf.NoCdoTick`; рабочий объект тикает как раньше.
	virtual bool IsTickable() const override { return AsdaTickGate::NotCdo(this); }
	virtual bool IsTickableInEditor() const override { return false; }
	virtual bool IsTickableWhenPaused() const override { return false; }

	// Текущее состояние зоны (для консоли/логов/BP). true = город (бит 0x80).
	UFUNCTION(BlueprintCallable, Category="Asda|Зона")
	bool IsInPeaceZone() const { return (CachedZoneFlags & 0x80) != 0; }

	// ★ 30.10.2026 [ОРИГИНАЛ]: «город» по клетке, где локальный игрок стоит В ЭТОТ МОМЕНТ. Эталон в точках решения
	//   (`uiInventoryConfirmAction@0xC4AD60`, ui_core.c:30542) зовёт `controlBar_SlotIsActive(клетка) & 0x80` напрямую,
	//   а не кэш `g_CachedZoneAttribFlags`, — кэш до первого тика на карте равен 100 (не город). Нет павна/сетки → кэш.
	bool IsLocalPlayerInPeaceZoneNow() const;

	// Показать баннер названия локации (штатный путь: вход на карту / вход в город).
	UFUNCTION(BlueprintCallable, Category="Asda|Зона")
	void ShowLocationBanner(bool bTownVariant);

	// Принудительный показ немедленно: сбрасывает гард 8 с и отложенный показ (консоль Asda.Zone.Banner).
	UFUNCTION(BlueprintCallable, Category="Asda|Зона")
	void ForceShowBanner(bool bTownVariant);

	// Диагностика: карта / клетка / зона / трек / текстура — одной строкой.
	FString DescribeState() const;
	// Диагностика баннера: создан / в вьюпорте / фаза / фактические размер-позиция / альфа.
	FString DescribeBanner() const;

private:
	// ── Состояние зоны (эталонный g_CachedZoneAttribFlags) ──
	static constexpr int32 kZoneUnset = 100;   // эталон инициализирует именно 100 (невозможный байт1)
	int32 CachedZoneFlags = kZoneUnset;
	int32 ZoneMapId       = -1;                // карта, к которой относится состояние
	int32 LastTileX       = INT32_MIN;         // эталон пересчитывает зону только при СМЕНЕ клетки
	int32 LastTileZ       = INT32_MIN;

	// ── Баннер названия локации ──
	UPROPERTY(Transient) TObjectPtr<UUserWidget> BannerWidget;
	UPROPERTY(Transient) TObjectPtr<UImage>      BannerImage;
	// 0 = скрыт, 1 = проявление (2.0 с), 2 = затухание (7.0 с) — ровно две objStartTimedAction эталона.
	int32 BannerPhase   = 0;
	float BannerPhaseT  = 0.f;
	float BannerGuard   = 0.f;   // остаток 8.0 с «повторно не показывать» (эталонный 0x1F40)
	// ── Пиксель-в-пиксель и ожидание валидной компоновки ──
	FVector2D BannerTexSize = FVector2D::ZeroVector;   // натуральный размер текстуры названия (эталон: размер контрола)
	FVector2D BannerVpSize  = FVector2D::ZeroVector;   // вьюпорт, под который посчитаны целые пиксели
	float     BannerDpi     = 1.f;                     // масштаб UI на момент раскладки (у проекта кривая = 1.0)
	bool      bBannerAwaitingLayout = false;           // ждём совпадения геометрии с размером текстуры
	int32     BannerLayoutTries = 0;
	int32     BannerShowCount   = 0;                   // номер показа (для сверки 1-го и 2-го в логе)
	static constexpr int32 kBannerLayoutMaxTries = 10; // предохранитель: не ждать вечно
	// Баннер «вход на карту» показывается по снятии загрузочного экрана (он полноэкранный, ZOrder 30000).
	bool  bPendingFirstBanner = false;
	bool  bPendingFirstTown   = false;

	static constexpr int32 kBannerZOrder = 2000;   // поверх игрового HUD, ниже загрузочного экрана

	static constexpr float kBannerFadeInSec  = 2.0f;   // objStartTimedAction(ctrl, 0, 255, 2000)
	static constexpr float kBannerFadeOutSec = 7.0f;   // objStartTimedAction(ctrl, 255, 0, 7000)
	static constexpr float kBannerGuardSec   = 8.0f;   // 0x1F40 мс

	// Фейд смены BGM: 16 шагов громкости эталона (CSound_SetupFade volume/16) при 30 к/с = 0.533 с.
	static constexpr float kBgmFadeSec = 0.533f;

	void  TickZone(float DeltaTime);
	void  TickBanner(float DeltaTime);
	void  ResetForMap(int32 MapId);
	// Имя трека для зоны по строке DT_MapTable (пусто = слот карты не заполнен → тишина, как в оригинале).
	FName ResolveBgm(int32 MapId, bool bTown) const;
	// Имя текстуры названия локации (Map_SetNameTextureByID). bTown=false → обычная.
	FString ResolveNameTexture(int32 MapId, bool bTown) const;
	UTexture2D* LoadNameTexture(const FString& BaseName) const;
	bool  EnsureBannerWidget();
	// Раскладка баннера в ЦЕЛЫХ ПИКСЕЛЯХ экрана (эталон рисует UI 1:1, без масштабирования).
	void  ApplyBannerLayout();
	void  HideBanner();
	// ★ Один поток BGM: снести запечённый в уровень авто-BGM (легаси-актор «MapBGM»), если он там есть.
	void  SilenceLegacyLevelBgm();
	const UAsdaWalkGrid* GetPlayerGrid(class AAsdaPlayerCharacter*& OutPlayer) const;
};
