#pragma once

#include "LogEntry.g.h"

namespace winrt::LogMinds::implementation
{
    struct LogEntry : LogEntryT<LogEntry>
    {
        LogEntry() = default;

        winrt::hstring Timestamp() const;
        void Timestamp(winrt::hstring const& value);

        winrt::hstring Level() const;
        void Level(winrt::hstring const& value);

        winrt::hstring Source() const;
        void Source(winrt::hstring const& value);

        winrt::hstring Message() const;
        void Message(winrt::hstring const& value);

        winrt::hstring Context() const;
        void Context(winrt::hstring const& value);

        winrt::hstring Raw() const;
        void Raw(winrt::hstring const& value);

        winrt::Windows::Foundation::IReference<winrt::Windows::Foundation::DateTime> OccurredOn() const;
        void OccurredOn(winrt::Windows::Foundation::IReference<winrt::Windows::Foundation::DateTime> const& value);

    private:
        winrt::hstring m_timestamp{};
        winrt::hstring m_level{};
        winrt::hstring m_source{};
        winrt::hstring m_message{};
        winrt::hstring m_context{};
        winrt::hstring m_raw{};
        winrt::Windows::Foundation::IReference<winrt::Windows::Foundation::DateTime> m_occurredOn{ nullptr };
    };
}

namespace winrt::LogMinds::factory_implementation
{
    struct LogEntry : LogEntryT<LogEntry, implementation::LogEntry>
    {
    };
}
