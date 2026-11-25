// Copyright (c) 2026 @sitxovski (Discord)
// Тонкий доступ самотестов к сетевой подсистеме — реализация переходников (разбор и правила — в `Test/AsdaSelfTestNet.h`).
//   ★ Единственный файл, которому ради этих переходников нужен полный класс `UAsdaNetSubsystem`.
//   Вне дев-гвардов намеренно: все члены сети, к которым ведут переходники, объявлены без гвардов (иначе выпуск упал бы на C3861).
#include "Test/AsdaSelfTestNet.h"   // объявления переходников
#include "AsdaNetSubsystem.h"       // полный класс сети — ради него этот файл и существует
#include "Engine/GameInstance.h"    // UGameInstance::GetSubsystem

namespace AsdaTestNet
{
	// Подсистема сети экземпляра игры.
	UAsdaNetSubsystem* Get(const UGameInstance* GI) { return GI->GetSubsystem<UAsdaNetSubsystem>(); }

	// Методы: вызов одноимённого метода сети с теми же аргументами.
	void DebugFeedPacket(UAsdaNetSubsystem* Net, int32 Opcode, const TArray<uint8>& Body, uint16 HdrA) { Net->DebugFeedPacket(Opcode, Body, HdrA); }
	void SetSelfUserNumForDebug(UAsdaNetSubsystem* Net, uint16 Num) { Net->SetSelfUserNumForDebug(Num); }
	bool IsSelfUserIdx(const UAsdaNetSubsystem* Net, int32 Uidx) { return Net->IsSelfUserIdx(Uidx); }
	void SetSelfSoulState(UAsdaNetSubsystem* Net, uint8 InState) { Net->SetSelfSoulState(InState); }

	// Поля: ссылка на одноимённое поле сети (неконстантная и константная пара на каждое).
	uint16& SelfUserNum(UAsdaNetSubsystem* Net) { return Net->SelfUserNum; }
	const uint16& SelfUserNum(const UAsdaNetSubsystem* Net) { return Net->SelfUserNum; }
	bool& bSelfUserNumKnown(UAsdaNetSubsystem* Net) { return Net->bSelfUserNumKnown; }
	const bool& bSelfUserNumKnown(const UAsdaNetSubsystem* Net) { return Net->bSelfUserNumKnown; }
	int32& SelectedCharId(UAsdaNetSubsystem* Net) { return Net->SelectedCharId; }
	const int32& SelectedCharId(const UAsdaNetSubsystem* Net) { return Net->SelectedCharId; }
	bool& bInGameConnected(UAsdaNetSubsystem* Net) { return Net->bInGameConnected; }
	const bool& bInGameConnected(const UAsdaNetSubsystem* Net) { return Net->bInGameConnected; }
	bool& bSelfDead(UAsdaNetSubsystem* Net) { return Net->bSelfDead; }
	const bool& bSelfDead(const UAsdaNetSubsystem* Net) { return Net->bSelfDead; }
	uint8& SelfLevel(UAsdaNetSubsystem* Net) { return Net->SelfLevel; }
	const uint8& SelfLevel(const UAsdaNetSubsystem* Net) { return Net->SelfLevel; }
	uint8& SelfSoulState(UAsdaNetSubsystem* Net) { return Net->SelfSoulState; }
	const uint8& SelfSoulState(const UAsdaNetSubsystem* Net) { return Net->SelfSoulState; }
	int32& EnterMapId(UAsdaNetSubsystem* Net) { return Net->EnterMapId; }
	const int32& EnterMapId(const UAsdaNetSubsystem* Net) { return Net->EnterMapId; }
	int32& SelfWeightCur(UAsdaNetSubsystem* Net) { return Net->SelfWeightCur; }
	const int32& SelfWeightCur(const UAsdaNetSubsystem* Net) { return Net->SelfWeightCur; }
}
