#include "stdafx.h"
#include "GameLogic/Events/EventPanelState.h"

#include "Data/Translation/MultiLanguage.h"
#include "Engine/Object/ZzzCharacter.h"
#include "Network/Server/WSclient.h"

namespace GameLogic::Events
{
namespace
{
// C2 header: C2, size (2 bytes), F5, 49.
constexpr int32_t HeaderSize = 5;
constexpr auto RenewalInterval = std::chrono::seconds(60);

class Reader
{
public:
    Reader(const BYTE* data, int32_t size) : m_data(data), m_size(size), m_position(HeaderSize)
    {
    }

    bool Ok() const
    {
        return m_ok;
    }

    BYTE Byte()
    {
        if (!Require(1))
        {
            return 0;
        }

        return m_data[m_position++];
    }

    uint16_t UInt16()
    {
        if (!Require(2))
        {
            return 0;
        }

        const auto value = static_cast<uint16_t>(m_data[m_position] | (m_data[m_position + 1] << 8));
        m_position += 2;
        return value;
    }

    uint32_t UInt32()
    {
        if (!Require(4))
        {
            return 0;
        }

        const auto value = static_cast<uint32_t>(m_data[m_position]) | (static_cast<uint32_t>(m_data[m_position + 1]) << 8) |
                           (static_cast<uint32_t>(m_data[m_position + 2]) << 16) |
                           (static_cast<uint32_t>(m_data[m_position + 3]) << 24);
        m_position += 4;
        return value;
    }

    // A byte with the length, then that many bytes of UTF-8.
    std::wstring Text()
    {
        const auto length = Byte();
        if (length == 0 || !Require(length))
        {
            return {};
        }

        std::vector<wchar_t> buffer(static_cast<size_t>(length) + 1, L'\0');
        CMultiLanguage::ConvertFromUtf8(buffer.data(), reinterpret_cast<const char*>(m_data + m_position), length);
        buffer[length] = L'\0';
        m_position += length;
        return std::wstring(buffer.data());
    }

private:
    bool Require(int32_t count)
    {
        if (!m_ok || m_position + count > m_size)
        {
            m_ok = false;
            return false;
        }

        return true;
    }

    const BYTE* m_data;
    int32_t m_size;
    int32_t m_position;
    bool m_ok = true;
};
} // namespace

EventPanelState& EventPanelState::Instance()
{
    static EventPanelState instance;
    return instance;
}

bool EventPanelState::ReadPacket(const BYTE* data, int32_t size)
{
    if (data == nullptr || size < HeaderSize + 4)
    {
        return false;
    }

    Reader reader(data, size);
    const auto hour = reader.Byte();
    const auto minute = reader.Byte();
    const auto second = reader.Byte();
    const auto eventCount = reader.Byte();

    std::vector<EventPanelEntry> entries;
    entries.reserve(eventCount);
    for (int i = 0; i < eventCount && reader.Ok(); ++i)
    {
        EventPanelEntry entry;
        entry.Kind = static_cast<EventPanelKind>(reader.Byte());
        entry.State = static_cast<EventPanelEventState>(reader.Byte());
        entry.StartHour = reader.Byte();
        entry.StartMinute = reader.Byte();
        entry.Seconds = reader.UInt32();
        entry.Name = reader.Text();

        const auto groupCount = reader.Byte();
        for (int g = 0; g < groupCount && reader.Ok(); ++g)
        {
            EventPanelMonsterGroup group;
            group.Alive = reader.UInt16();
            group.Total = reader.UInt16();
            group.MonsterName = reader.Text();
            group.MapName = reader.Text();
            entry.Monsters.push_back(std::move(group));
        }

        entries.push_back(std::move(entry));
    }

    if (!reader.Ok() || hour > 23 || minute > 59 || second > 59)
    {
        return false;
    }

    m_entries = std::move(entries);
    m_serverSecondsOfDay = hour * 3600 + minute * 60 + second;
    m_receivedAt = std::chrono::steady_clock::now();
    m_hasData = true;
    return true;
}

int64_t EventPanelState::GetSecondsSinceReceive() const
{
    if (!m_hasData)
    {
        return 0;
    }

    return std::chrono::duration_cast<std::chrono::seconds>(std::chrono::steady_clock::now() - m_receivedAt).count();
}

int64_t EventPanelState::GetRemainingSeconds(const EventPanelEntry& entry) const
{
    const auto remaining = static_cast<int64_t>(entry.Seconds) - GetSecondsSinceReceive();
    return remaining < 0 ? 0 : remaining;
}

int32_t EventPanelState::GetServerSecondsOfDay() const
{
    return static_cast<int32_t>((m_serverSecondsOfDay + GetSecondsSinceReceive()) % 86400);
}

void EventPanelState::SendCommand(const wchar_t* commandLine)
{
    if (SocketClient == nullptr || Hero == nullptr)
    {
        return;
    }

    SocketClient->ToGameServer()->SendPublicChatMessage(MU_C16(Hero->ID), MU_C16(commandLine));
}

void EventPanelState::Open()
{
    m_isOpen = true;
    m_lastRenewal = std::chrono::steady_clock::now();
    SendCommand(L"/eventos abrir");
}

void EventPanelState::Close()
{
    if (!m_isOpen)
    {
        return;
    }

    m_isOpen = false;
    SendCommand(L"/eventos cerrar");
}

void EventPanelState::Update()
{
    if (!m_isOpen)
    {
        return;
    }

    const auto now = std::chrono::steady_clock::now();
    if (now - m_lastRenewal >= RenewalInterval)
    {
        m_lastRenewal = now;
        SendCommand(L"/eventos abrir");
    }
}

void EventPanelState::Reset()
{
    m_entries.clear();
    m_hasData = false;

    // Entering the world again (other character, reconnect) starts a new
    // session on the server, which doesn't know about the panel anymore.
    if (m_isOpen)
    {
        Open();
    }
}
} // namespace GameLogic::Events
