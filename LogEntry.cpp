#include "pch.h"
#include "LogEntry.h"
#if __has_include("LogEntry.g.cpp")
#include "LogEntry.g.cpp"
#endif

namespace winrt::LogMinds::implementation
{
    winrt::hstring LogEntry::Timestamp() const
    {
        return m_timestamp;
    }

    void LogEntry::Timestamp(winrt::hstring const& value)
    {
        m_timestamp = value;
    }

    winrt::hstring LogEntry::Level() const
    {
        return m_level;
    }

    void LogEntry::Level(winrt::hstring const& value)
    {
        m_level = value;
    }

    winrt::hstring LogEntry::Source() const
    {
        return m_source;
    }

    void LogEntry::Source(winrt::hstring const& value)
    {
        m_source = value;
    }

    winrt::hstring LogEntry::Message() const
    {
        return m_message;
    }

    void LogEntry::Message(winrt::hstring const& value)
    {
        m_message = value;
    }

    winrt::hstring LogEntry::Context() const
    {
        return m_context;
    }

    void LogEntry::Context(winrt::hstring const& value)
    {
        m_context = value;
    }

    winrt::hstring LogEntry::Raw() const
    {
        return m_raw;
    }

    void LogEntry::Raw(winrt::hstring const& value)
    {
        m_raw = value;
    }

    winrt::Windows::Foundation::IReference<winrt::Windows::Foundation::DateTime> LogEntry::OccurredOn() const
    {
        return m_occurredOn;
    }

    void LogEntry::OccurredOn(winrt::Windows::Foundation::IReference<winrt::Windows::Foundation::DateTime> const& value)
    {
        m_occurredOn = value;
    }
}
