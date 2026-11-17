#pragma once

// VORTHANIA: the data of the event panel (key H).
//
// The server sends the whole panel in one C2 F5 49 message: when the panel is
// opened, and again whenever something changes (an invasion monster died, an
// event started or ended). In between, the countdowns run on the client.
//
// Opening and closing goes through the chat command "/eventos abrir" and
// "/eventos cerrar", so no new client-to-server packet was needed. While the
// panel is open the subscription is renewed every minute; the server drops it
// on its own after three minutes without renewal.

#include "Core/Platform/WinCompat.h"

#include <chrono>
#include <cstdint>
#include <string>
#include <vector>

namespace GameLogic::Events
{
enum class EventPanelKind : BYTE
{
    Invasion = 0,
    MiniGame = 1,
    HappyHour = 2,
    Other = 3,
};

enum class EventPanelEventState : BYTE
{
    Scheduled = 0,
    Starting = 1,
    Running = 2,
    NoSchedule = 3,
};

struct EventPanelMonsterGroup
{
    std::wstring MonsterName;
    std::wstring MapName;
    int Alive = 0;
    int Total = 0;
};

struct EventPanelEntry
{
    std::wstring Name;
    EventPanelKind Kind = EventPanelKind::Other;
    EventPanelEventState State = EventPanelEventState::NoSchedule;
    BYTE StartHour = 0;
    BYTE StartMinute = 0;
    // Until the start, or until the end while running, when the message arrived.
    uint32_t Seconds = 0;
    std::vector<EventPanelMonsterGroup> Monsters;
};

class EventPanelState
{
public:
    static constexpr BYTE SubCode = 0x49;

    static EventPanelState& Instance();

    // Takes the C2 F5 49 message. Returns false when it isn't plausible.
    bool ReadPacket(const BYTE* data, int32_t size);

    bool HasData() const
    {
        return m_hasData;
    }

    const std::vector<EventPanelEntry>& GetEntries() const
    {
        return m_entries;
    }

    // Seconds left until the start (or the end) of the entry, counted down
    // since the message arrived.
    int64_t GetRemainingSeconds(const EventPanelEntry& entry) const;

    // The server time now, as seconds since midnight.
    int32_t GetServerSecondsOfDay() const;

    // Subscribes to the updates; call when the panel opens.
    void Open();

    // Unsubscribes; call when the panel closes.
    void Close();

    // Renews the subscription while the panel is open; call every frame.
    void Update();

    // Forgets the data, e.g. when the character changed. An open panel asks again.
    void Reset();

private:
    static void SendCommand(const wchar_t* commandLine);
    int64_t GetSecondsSinceReceive() const;

    std::vector<EventPanelEntry> m_entries;
    int32_t m_serverSecondsOfDay = 0;
    std::chrono::steady_clock::time_point m_receivedAt{};
    std::chrono::steady_clock::time_point m_lastRenewal{};
    bool m_hasData = false;
    bool m_isOpen = false;
};

inline EventPanelState& EventPanel()
{
    return EventPanelState::Instance();
}
} // namespace GameLogic::Events
