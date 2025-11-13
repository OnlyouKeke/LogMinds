#include "pch.h"
#include "MainWindow.xaml.h"
#include "LogEntry.h"
#if __has_include("MainWindow.g.cpp")
#include "MainWindow.g.cpp"
#endif
#include <microsoft.ui.xaml.window.h>

using namespace winrt;
using namespace Microsoft::UI::Xaml;
using namespace Microsoft::UI::Xaml::Controls;
using namespace Microsoft::UI::Xaml::Controls::Primitives;
using namespace Windows::Data::Json;
using namespace Windows::Foundation;
using namespace Windows::Storage;
using namespace Windows::Storage::Pickers;

namespace
{
    constexpr int64_t c_ticksPerSecond = 10'000'000;
    constexpr int64_t c_ticksPerDay = 24 * 60 * 60 * c_ticksPerSecond;

    bool IsWordChar(wchar_t ch)
    {
        return std::iswalnum(ch) != 0 || ch == L'_';
    }
}

namespace winrt::LogMinds::implementation
{
    MainWindow::MainWindow()
    {
        InitializeComponent();

        m_filteredEntries = single_threaded_observable_vector<LogEntry>();
        LogListView().ItemsSource(m_filteredEntries);
        UpdateUiState();
        RefreshStats();
    }

    int32_t MainWindow::MyProperty()
    {
        return m_myProperty;
    }

    void MainWindow::MyProperty(int32_t value)
    {
        m_myProperty = value;
    }

    void MainWindow::OnOpenLogClicked(IInspectable const&, RoutedEventArgs const&)
    {
        LoadLogsAsync();
    }

    void MainWindow::OnInterpretClicked(IInspectable const&, RoutedEventArgs const&)
    {
        InterpretAsync();
    }

    void MainWindow::OnSearchTextChanged(IInspectable const& sender, TextChangedEventArgs const&)
    {
        auto textBox = sender.as<TextBox>();
        m_searchTerm = ToLower(std::wstring(textBox.Text().c_str()));
        ApplyFilters();
    }

    void MainWindow::OnSeverityChanged(IInspectable const& sender, SelectionChangedEventArgs const&)
    {
        auto combo = sender.as<ComboBox>();
        hstring label;
        if (auto selected = combo.SelectedItem())
        {
            if (auto item = selected.try_as<ComboBoxItem>())
            {
                label = item.Content().try_as<hstring>();
            }
        }

        auto upper = ToUpper(std::wstring(label.c_str()));
        if (upper == L"全部" || upper == L"ALL" || upper.empty())
        {
            m_selectedLevel.clear();
        }
        else
        {
            m_selectedLevel = upper;
        }

        ApplyFilters();
    }

    void MainWindow::OnStartDateChanged(IInspectable const&, DatePickerValueChangedEventArgs const& args)
    {
        if (auto date = args.NewDate())
        {
            m_startTimeFilter = date.Value();
        }
        else
        {
            m_startTimeFilter.reset();
        }
        ApplyFilters();
    }

    void MainWindow::OnEndDateChanged(IInspectable const&, DatePickerValueChangedEventArgs const& args)
    {
        if (auto date = args.NewDate())
        {
            auto value = date.Value();
            value.Duration += c_ticksPerDay - 1;
            m_endTimeFilter = value;
        }
        else
        {
            m_endTimeFilter.reset();
        }
        ApplyFilters();
    }

    void MainWindow::OnStartTimeChanged(IInspectable const&, TimePickerValueChangedEventArgs const&)
    {
        if (m_startTimeFilter)
        {
            auto date = *m_startTimeFilter;
            auto time = StartTimePicker().Time();
            date.Duration = (date.Duration / c_ticksPerDay) * c_ticksPerDay + time.Duration;
            m_startTimeFilter = date;
            ApplyFilters();
        }
    }

    void MainWindow::OnEndTimeChanged(IInspectable const&, TimePickerValueChangedEventArgs const&)
    {
        if (m_endTimeFilter)
        {
            auto date = *m_endTimeFilter;
            auto time = EndTimePicker().Time();
            date.Duration = (date.Duration / c_ticksPerDay) * c_ticksPerDay + time.Duration;
            m_endTimeFilter = date;
            ApplyFilters();
        }
    }

    void MainWindow::OnClearFilters(IInspectable const&, RoutedEventArgs const&)
    {
        SearchBox().Text(L"");
        SeverityCombo().SelectedIndex(0);
        StartDatePicker().Date(nullptr);
        EndDatePicker().Date(nullptr);
        StartTimePicker().Time(TimeSpan{});
        EndTimePicker().Time(TimeSpan{});

        m_searchTerm.clear();
        m_selectedLevel.clear();
        m_startTimeFilter.reset();
        m_endTimeFilter.reset();
        ApplyFilters();
    }

    winrt::fire_and_forget MainWindow::LoadLogsAsync()
    {
        auto lifetime = get_strong();

        m_isLoading = true;
        UpdateUiState();
        UpdateSummary(L"");

        FileOpenPicker picker;
        picker.FileTypeFilter().Append(L".log");
        picker.FileTypeFilter().Append(L".txt");
        picker.FileTypeFilter().Append(L".json");
        picker.FileTypeFilter().Append(L".csv");
        picker.FileTypeFilter().Append(L".*");

        auto hwnd = GetWindowHandle();
        if (hwnd != nullptr)
        {
            Microsoft::UI::Win32Interop::InitializeWithWindow(picker, hwnd);
        }

        StorageFile file{ nullptr };

        try
        {
            file = co_await picker.PickSingleFileAsync();
        }
        catch (...)
        {
        }

        if (!file)
        {
            m_isLoading = false;
            UpdateUiState();
            co_return;
        }

        m_currentFileName = file.DisplayName();

        hstring text;
        try
        {
            text = co_await FileIO::ReadTextAsync(file);
        }
        catch (hresult_error const& ex)
        {
            m_isLoading = false;
            UpdateUiState();

            ContentDialog dialog;
            dialog.XamlRoot(Content().XamlRoot());
            dialog.Title(box_value(L"读取失败"));
            dialog.Content(box_value(ex.message()));
            dialog.CloseButtonText(L"关闭");
            co_await dialog.ShowAsync();
            co_return;
        }

        std::vector<ParsedEntry> parsedEntries;
        bool parsedFromJson = false;

        try
        {
            auto json = JsonValue::Parse(text);
            if (json.ValueType() == JsonValueType::Array)
            {
                parsedFromJson = true;
                for (auto const& item : json.GetArray())
                {
                    if (item.ValueType() == JsonValueType::Object)
                    {
                        parsedEntries.emplace_back(ParseJsonObject(item.GetObject(), std::wstring(item.Stringify().c_str())));
                    }
                    else
                    {
                        ParsedEntry fallback;
                        fallback.Entry = CreateEntry();
                        auto raw = std::wstring(item.Stringify().c_str());
                        fallback.Entry.Message(hstring(raw));
                        fallback.Entry.Raw(hstring(raw));
                        parsedEntries.emplace_back(std::move(fallback));
                    }
                }
            }
            else if (json.ValueType() == JsonValueType::Object)
            {
                parsedFromJson = true;
                parsedEntries.emplace_back(ParseJsonObject(json.GetObject(), std::wstring(text.c_str())));
            }
        }
        catch (...)
        {
            parsedFromJson = false;
        }

        if (!parsedFromJson)
        {
            std::wistringstream stream(text.c_str());
            std::wstring line;
            while (std::getline(stream, line))
            {
                auto parsed = ParseLine(line);
                if (parsed.Entry)
                {
                    parsedEntries.emplace_back(std::move(parsed));
                }
            }
        }

        m_allEntries.clear();
        m_allEntries.reserve(parsedEntries.size());
        for (auto& entry : parsedEntries)
        {
            if (entry.Entry)
            {
                m_allEntries.emplace_back(std::move(entry));
            }
        }

        ApplyFilters();
        RefreshStats();

        m_isLoading = false;
        UpdateUiState();

        if (!m_currentFileName.empty())
        {
            FileNameText().Text(L"文件: " + m_currentFileName);
        }
        else
        {
            FileNameText().Text(L"");
        }

        if (!m_allEntries.empty())
        {
            SummaryBlock().Text(L"点击“LLM解读”获取智能摘要");
        }
        else
        {
            SummaryBlock().Text(L"未解析到有效日志条目");
        }
    }

    winrt::fire_and_forget MainWindow::InterpretAsync()
    {
        auto lifetime = get_strong();
        if (m_allEntries.empty())
        {
            co_return;
        }

        m_isLoading = true;
        UpdateUiState();

        co_await winrt::resume_background();
        auto summary = BuildSummary();
        co_await winrt::resume_foreground(DispatcherQueue());

        UpdateSummary(summary);
        m_isLoading = false;
        UpdateUiState();
    }

    void MainWindow::UpdateFilters()
    {
        ApplyFilters();
    }

    void MainWindow::ApplyFilters()
    {
        if (!m_filteredEntries)
        {
            return;
        }

        m_filteredEntries.Clear();

        for (auto const& item : m_allEntries)
        {
            if (!item.Entry)
            {
                continue;
            }

            if (!m_selectedLevel.empty())
            {
                if (item.NormalizedLevel != m_selectedLevel)
                {
                    continue;
                }
            }

            if (!m_searchTerm.empty())
            {
                std::wstring message = ToLower(std::wstring(item.Entry.Message().c_str()));
                std::wstring context = ToLower(std::wstring(item.Entry.Context().c_str()));
                std::wstring source = ToLower(std::wstring(item.Entry.Source().c_str()));
                std::wstring raw = ToLower(std::wstring(item.Entry.Raw().c_str()));
                if (message.find(m_searchTerm) == std::wstring::npos &&
                    context.find(m_searchTerm) == std::wstring::npos &&
                    source.find(m_searchTerm) == std::wstring::npos &&
                    raw.find(m_searchTerm) == std::wstring::npos)
                {
                    continue;
                }
            }

            if (m_startTimeFilter || m_endTimeFilter)
            {
                if (!item.OccurredOn)
                {
                    continue;
                }

                if (m_startTimeFilter && item.OccurredOn.value() < m_startTimeFilter.value())
                {
                    continue;
                }

                if (m_endTimeFilter && item.OccurredOn.value() > m_endTimeFilter.value())
                {
                    continue;
                }
            }

            m_filteredEntries.Append(item.Entry);
        }

        RefreshStats();
        UpdateUiState();
    }

    void MainWindow::UpdateUiState()
    {
        OpenLogButton().IsEnabled(!m_isLoading);
        InterpretButton().IsEnabled(!m_isLoading && !m_allEntries.empty());
        ClearFiltersButton().IsEnabled(!m_isLoading && !m_allEntries.empty());
        LoadingIndicator().IsActive(m_isLoading);
        LoadingIndicator().Visibility(m_isLoading ? Visibility::Visible : Visibility::Collapsed);
        SearchBox().IsEnabled(!m_isLoading);
        SeverityCombo().IsEnabled(!m_isLoading);
        StartDatePicker().IsEnabled(!m_isLoading);
        EndDatePicker().IsEnabled(!m_isLoading);
    }

    void MainWindow::UpdateSummary(hstring const& summary)
    {
        m_lastSummary = summary;
        SummaryBlock().Text(summary);
    }

    MainWindow::ParsedEntry MainWindow::ParseLine(std::wstring const& line)
    {
        ParsedEntry result;
        auto trimmed = Trim(line);
        if (trimmed.empty())
        {
            return result;
        }

        if (trimmed.front() == L'{')
        {
            try
            {
                auto json = JsonObject::Parse(trimmed);
                return ParseJsonObject(json, trimmed);
            }
            catch (...)
            {
            }
        }

        result.Entry = CreateEntry();
        result.Entry.Raw(hstring(trimmed));

        static const std::wregex isoPattern(
            LR"(^\s*(\d{4}-\d{2}-\d{2}[ T]\d{2}:\d{2}:\d{2}(?:[.,]\d+)?)(?:\s*(?:Z|[+-]\d{2}:\d{2})?)?(?:\s*\[([^\]]+)\])?\s*(TRACE|DEBUG|INFO|WARN|WARNING|ERROR|ERR|FATAL|CRITICAL|NOTICE)?\s*[:-]?\s*(.*)$)",
            std::regex_constants::icase);
        std::wsmatch isoMatch;
        if (std::regex_match(trimmed, isoMatch, isoPattern))
        {
            auto timestamp = std::wstring(isoMatch[1].first, isoMatch[1].second);
            auto source = std::wstring(isoMatch[2].first, isoMatch[2].second);
            auto level = std::wstring(isoMatch[3].first, isoMatch[3].second);
            auto message = std::wstring(isoMatch[4].first, isoMatch[4].second);

            result.Entry.Timestamp(hstring(timestamp));
            result.OccurredOn = ParseTimestamp(timestamp);
            result.Entry.Source(hstring(source));
            result.Entry.Message(hstring(Trim(message)));

            if (!level.empty())
            {
                result.NormalizedLevel = ToUpper(level);
                if (result.NormalizedLevel == L"WARNING")
                {
                    result.NormalizedLevel = L"WARN";
                }
                else if (result.NormalizedLevel == L"ERR")
                {
                    result.NormalizedLevel = L"ERROR";
                }
                result.Entry.Level(hstring(result.NormalizedLevel));
            }
            else
            {
                result.Entry.Level(L"");
            }

            return result;
        }

        static const std::wregex syslogPattern(
            LR"(^\s*([A-Za-z]{3}\s+\d{1,2}\s+\d{2}:\d{2}:\d{2})\s+([^\s]+)\s+([^:]+):\s*(.*)$)");
        std::wsmatch syslogMatch;
        if (std::regex_match(trimmed, syslogMatch, syslogPattern))
        {
            auto timestamp = std::wstring(syslogMatch[1].first, syslogMatch[1].second);
            auto host = std::wstring(syslogMatch[2].first, syslogMatch[2].second);
            auto process = std::wstring(syslogMatch[3].first, syslogMatch[3].second);
            auto message = std::wstring(syslogMatch[4].first, syslogMatch[4].second);

            result.Entry.Timestamp(hstring(timestamp));
            result.Entry.Source(hstring(host + L" " + process));
            result.Entry.Message(hstring(Trim(message)));
            result.OccurredOn = ParseSyslogTimestamp(timestamp);
            result.Entry.Level(L"");
            result.NormalizedLevel.clear();
            return result;
        }

        static const std::wregex kvPattern(
            LR"(^\s*\[?([^\]]+)\]?\s*[:|-]\s*(TRACE|DEBUG|INFO|WARN|WARNING|ERROR|ERR|FATAL|CRITICAL)\s*[:-]?\s*(.*)$)",
            std::regex_constants::icase);
        std::wsmatch kvMatch;
        if (std::regex_match(trimmed, kvMatch, kvPattern))
        {
            auto source = std::wstring(kvMatch[1].first, kvMatch[1].second);
            auto level = std::wstring(kvMatch[2].first, kvMatch[2].second);
            auto message = std::wstring(kvMatch[3].first, kvMatch[3].second);

            result.Entry.Source(hstring(Trim(source)));
            result.NormalizedLevel = ToUpper(level);
            if (result.NormalizedLevel == L"WARNING")
            {
                result.NormalizedLevel = L"WARN";
            }
            else if (result.NormalizedLevel == L"ERR")
            {
                result.NormalizedLevel = L"ERROR";
            }
            result.Entry.Level(hstring(result.NormalizedLevel));
            result.Entry.Message(hstring(Trim(message)));
            return result;
        }

        static const std::wregex simpleLevelPattern(
            LR"(^\s*\[?(TRACE|DEBUG|INFO|WARN|WARNING|ERROR|ERR|FATAL|CRITICAL|NOTICE)\]?\s*[:-]?\s*(.*)$)",
            std::regex_constants::icase);
        std::wsmatch simpleMatch;
        if (std::regex_match(trimmed, simpleMatch, simpleLevelPattern))
        {
            auto level = std::wstring(simpleMatch[1].first, simpleMatch[1].second);
            auto message = std::wstring(simpleMatch[2].first, simpleMatch[2].second);
            result.NormalizedLevel = ToUpper(level);
            if (result.NormalizedLevel == L"WARNING")
            {
                result.NormalizedLevel = L"WARN";
            }
            else if (result.NormalizedLevel == L"ERR")
            {
                result.NormalizedLevel = L"ERROR";
            }
            result.Entry.Level(hstring(result.NormalizedLevel));
            result.Entry.Message(hstring(Trim(message)));
            return result;
        }

        result.Entry.Message(hstring(trimmed));
        result.Entry.Level(L"");
        result.NormalizedLevel.clear();
        return result;
    }

    MainWindow::ParsedEntry MainWindow::ParseJsonObject(JsonObject const& object, std::wstring const& rawLine)
    {
        ParsedEntry result;
        result.Entry = CreateEntry();
        result.Entry.Raw(hstring(rawLine));

        auto getValue = [&](std::initializer_list<std::wstring_view> keys) -> std::wstring
        {
            for (auto key : keys)
            {
                hstring hkey(key.data(), key.size());
                if (object.HasKey(hkey))
                {
                    auto value = object.Lookup(hkey);
                    switch (value.ValueType())
                    {
                    case JsonValueType::String:
                        return std::wstring(value.GetString().c_str());
                    case JsonValueType::Number:
                        return std::to_wstring(value.GetNumber());
                    case JsonValueType::Boolean:
                        return value.GetBoolean() ? L"true" : L"false";
                    default:
                        return std::wstring(value.Stringify().c_str());
                    }
                }
            }
            return L"";
        };

        auto timestamp = getValue({ L"timestamp", L"time", L"@timestamp", L"datetime", L"date", L"eventTime" });
        if (!timestamp.empty())
        {
            result.Entry.Timestamp(hstring(timestamp));
            result.OccurredOn = ParseTimestamp(timestamp);
        }

        auto level = getValue({ L"level", L"severity", L"logLevel", L"lvl", L"priority" });
        if (!level.empty())
        {
            result.NormalizedLevel = ToUpper(level);
            if (result.NormalizedLevel == L"WARNING")
            {
                result.NormalizedLevel = L"WARN";
            }
            else if (result.NormalizedLevel == L"ERR")
            {
                result.NormalizedLevel = L"ERROR";
            }
            result.Entry.Level(hstring(result.NormalizedLevel));
        }

        auto source = getValue({ L"logger", L"source", L"module", L"service", L"category", L"name" });
        if (!source.empty())
        {
            result.Entry.Source(hstring(source));
        }

        auto message = getValue({ L"message", L"msg", L"event", L"description", L"detail" });
        if (!message.empty())
        {
            result.Entry.Message(hstring(message));
        }
        else
        {
            result.Entry.Message(hstring(rawLine));
        }

        std::vector<std::wstring> contextPairs;
        for (auto const& pair : object)
        {
            auto key = std::wstring(pair.Key().c_str());
            auto lowerKey = ToLower(key);
            if (lowerKey == L"timestamp" || lowerKey == L"time" || lowerKey == L"@timestamp" || lowerKey == L"datetime" ||
                lowerKey == L"date" || lowerKey == L"eventtime" || lowerKey == L"level" || lowerKey == L"severity" ||
                lowerKey == L"loglevel" || lowerKey == L"lvl" || lowerKey == L"priority" || lowerKey == L"message" ||
                lowerKey == L"msg" || lowerKey == L"event" || lowerKey == L"description" || lowerKey == L"detail" ||
                lowerKey == L"logger" || lowerKey == L"source" || lowerKey == L"module" || lowerKey == L"service" ||
                lowerKey == L"category" || lowerKey == L"name")
            {
                continue;
            }

            auto value = pair.Value();
            std::wstring serialized;
            switch (value.ValueType())
            {
            case JsonValueType::String:
                serialized = std::wstring(value.GetString().c_str());
                break;
            case JsonValueType::Number:
                serialized = std::to_wstring(value.GetNumber());
                break;
            case JsonValueType::Boolean:
                serialized = value.GetBoolean() ? L"true" : L"false";
                break;
            default:
                serialized = std::wstring(value.Stringify().c_str());
                break;
            }
            contextPairs.push_back(key + L"=" + serialized);
        }

        if (!contextPairs.empty())
        {
            std::wstring context;
            for (size_t i = 0; i < contextPairs.size(); ++i)
            {
                context.append(contextPairs[i]);
                if (i + 1 < contextPairs.size())
                {
                    context.append(L" | ");
                }
            }
            result.Entry.Context(hstring(context));
        }

        if (result.NormalizedLevel.empty())
        {
            result.NormalizedLevel.clear();
        }

        return result;
    }

    LogEntry MainWindow::CreateEntry()
    {
        return LogEntry();
    }

    std::wstring MainWindow::Trim(std::wstring_view text)
    {
        size_t start = 0;
        size_t end = text.size();
        while (start < end && std::iswspace(text[start]))
        {
            ++start;
        }
        while (end > start && std::iswspace(text[end - 1]))
        {
            --end;
        }
        return std::wstring(text.substr(start, end - start));
    }

    std::wstring MainWindow::ToLower(std::wstring value)
    {
        std::transform(value.begin(), value.end(), value.begin(), [](wchar_t ch)
        {
            return static_cast<wchar_t>(std::towlower(ch));
        });
        return value;
    }

    std::wstring MainWindow::ToUpper(std::wstring value)
    {
        std::transform(value.begin(), value.end(), value.begin(), [](wchar_t ch)
        {
            return static_cast<wchar_t>(std::towupper(ch));
        });
        return value;
    }

    std::optional<DateTime> MainWindow::ParseTimestamp(std::wstring_view text)
    {
        std::wstring normalized(text);
        for (auto& ch : normalized)
        {
            if (ch == L'T')
            {
                ch = L' ';
            }
        }

        auto dot = normalized.find_first_of(L".,");
        int fractionalMilliseconds = 0;
        if (dot != std::wstring::npos)
        {
            auto fraction = normalized.substr(dot + 1);
            if (!fraction.empty())
            {
                int digits = static_cast<int>(std::min<size_t>(3, fraction.size()));
                fractionalMilliseconds = std::stoi(fraction.substr(0, digits));
            }
            normalized = normalized.substr(0, dot);
        }

        std::wistringstream stream(normalized);
        std::tm tm{};
        stream >> std::get_time(&tm, L"%Y-%m-%d %H:%M:%S");
        if (stream.fail())
        {
            return std::nullopt;
        }

#if defined(_WIN32)
        auto seconds = _mkgmtime(&tm);
#else
        auto seconds = timegm(&tm);
#endif
        if (seconds == -1)
        {
            return std::nullopt;
        }

        auto timePoint = std::chrono::system_clock::from_time_t(seconds) + std::chrono::milliseconds(fractionalMilliseconds);
        DateTime dateTime{};
        dateTime.Duration = winrt::clock::from_sys(timePoint).time_since_epoch().count();
        return dateTime;
    }

    std::optional<DateTime> MainWindow::ParseSyslogTimestamp(std::wstring_view text)
    {
        std::wstring value(text);
        std::wistringstream stream(value);
        std::tm tm{};
        stream >> std::get_time(&tm, L"%b %d %H:%M:%S");
        if (stream.fail())
        {
            return std::nullopt;
        }

        auto now = std::chrono::system_clock::now();
        auto nowTime = std::chrono::system_clock::to_time_t(now);
        std::tm current{};
#if defined(_WIN32)
        gmtime_s(&current, &nowTime);
#else
        gmtime_r(&nowTime, &current);
#endif
        tm.tm_year = current.tm_year;

#if defined(_WIN32)
        auto seconds = _mkgmtime(&tm);
#else
        auto seconds = timegm(&tm);
#endif
        if (seconds == -1)
        {
            return std::nullopt;
        }

        auto timePoint = std::chrono::system_clock::from_time_t(seconds);
        DateTime dateTime{};
        dateTime.Duration = winrt::clock::from_sys(timePoint).time_since_epoch().count();
        return dateTime;
    }

    std::wstring MainWindow::FormatDateRange(std::optional<DateTime> const& start, std::optional<DateTime> const& end)
    {
        auto formatSingle = [](DateTime const& value)
        {
            auto sys = winrt::clock::to_sys(value);
            auto tt = std::chrono::system_clock::to_time_t(sys);
            std::tm tm{};
#if defined(_WIN32)
            gmtime_s(&tm, &tt);
#else
            gmtime_r(&tt, &tm);
#endif
            std::wstringstream ss;
            ss << std::put_time(&tm, L"%Y-%m-%d %H:%M:%S");
            return ss.str();
        };

        if (start && end)
        {
            return formatSingle(*start) + L" 至 " + formatSingle(*end);
        }
        if (start)
        {
            return L"自 " + formatSingle(*start);
        }
        if (end)
        {
            return L"截至 " + formatSingle(*end);
        }
        return L"";
    }

    hstring MainWindow::BuildSummary()
    {
        if (m_allEntries.empty())
        {
            return L"尚未加载日志数据。";
        }

        std::map<std::wstring, int> levelCount;
        std::map<std::wstring, int> sourceCount;
        std::unordered_map<std::wstring, int> keywordFrequency;
        std::vector<std::wstring> criticalMessages;
        std::optional<DateTime> firstTimestamp;
        std::optional<DateTime> lastTimestamp;

        for (auto const& entry : m_allEntries)
        {
            if (!entry.Entry)
            {
                continue;
            }

            auto level = entry.NormalizedLevel;
            if (level.empty())
            {
                level = L"未标记";
            }
            levelCount[level]++;

            auto source = std::wstring(entry.Entry.Source().c_str());
            if (!source.empty())
            {
                sourceCount[source]++;
            }

            if (entry.OccurredOn)
            {
                if (!firstTimestamp || entry.OccurredOn < firstTimestamp)
                {
                    firstTimestamp = entry.OccurredOn;
                }
                if (!lastTimestamp || entry.OccurredOn > lastTimestamp)
                {
                    lastTimestamp = entry.OccurredOn;
                }
            }

            auto message = std::wstring(entry.Entry.Message().c_str());
            if (!entry.NormalizedLevel.empty())
            {
                auto upper = entry.NormalizedLevel;
                if (upper == L"ERROR" || upper == L"FATAL" || upper == L"CRITICAL")
                {
                    criticalMessages.push_back(message);
                }
            }

            std::wstring lowerMessage = ToLower(message);
            std::wstring word;
            for (wchar_t ch : lowerMessage)
            {
                if (IsWordChar(ch))
                {
                    word.push_back(ch);
                }
                else
                {
                    if (word.size() > 3)
                    {
                        keywordFrequency[word]++;
                    }
                    word.clear();
                }
            }
            if (word.size() > 3)
            {
                keywordFrequency[word]++;
            }
        }

        std::vector<std::pair<std::wstring, int>> keywords(keywordFrequency.begin(), keywordFrequency.end());
        std::sort(keywords.begin(), keywords.end(), [](auto const& left, auto const& right)
        {
            if (left.second == right.second)
            {
                return left.first < right.first;
            }
            return left.second > right.second;
        });

        size_t maxKeywords = std::min<size_t>(5, keywords.size());

        std::wstringstream summary;
        summary << L"📊 日志总览" << std::endl;
        summary << L"  • 共解析 " << m_allEntries.size() << L" 条记录";
        if (!levelCount.empty())
        {
            summary << L"，级别分布：";
            bool first = true;
            for (auto const& pair : levelCount)
            {
                if (!first)
                {
                    summary << L"，";
                }
                summary << pair.first << L"=" << pair.second;
                first = false;
            }
        }
        summary << std::endl;

        if (firstTimestamp || lastTimestamp)
        {
            summary << L"  • 时间范围：" << FormatDateRange(firstTimestamp, lastTimestamp) << std::endl;
        }

        if (!sourceCount.empty())
        {
            std::vector<std::pair<std::wstring, int>> sortedSources(sourceCount.begin(), sourceCount.end());
            std::sort(sortedSources.begin(), sortedSources.end(), [](auto const& left, auto const& right)
            {
                if (left.second == right.second)
                {
                    return left.first < right.first;
                }
                return left.second > right.second;
            });
            summary << L"  • 主要来源：";
            size_t count = std::min<size_t>(3, sortedSources.size());
            for (size_t i = 0; i < count; ++i)
            {
                if (i > 0)
                {
                    summary << L"，";
                }
                summary << sortedSources[i].first << L"(" << sortedSources[i].second << L")";
            }
            summary << std::endl;
        }

        if (!criticalMessages.empty())
        {
            summary << L"⚠️ 关键异常" << std::endl;
            size_t count = std::min<size_t>(3, criticalMessages.size());
            for (size_t i = 0; i < count; ++i)
            {
                summary << L"  • " << criticalMessages[i] << std::endl;
            }
            if (criticalMessages.size() > count)
            {
                summary << L"  • 其余 " << (criticalMessages.size() - count) << L" 条错误已省略" << std::endl;
            }
        }

        if (maxKeywords > 0)
        {
            summary << L"🧠 主题洞察" << std::endl;
            summary << L"  • 高频关键词：";
            for (size_t i = 0; i < maxKeywords; ++i)
            {
                if (i > 0)
                {
                    summary << L"，";
                }
                summary << keywords[i].first << L"(" << keywords[i].second << L")";
            }
            summary << std::endl;
        }

        summary << L"✅ 建议操作" << std::endl;
        int warnCount = 0;
        if (auto it = levelCount.find(L"WARN"); it != levelCount.end())
        {
            warnCount = it->second;
        }

        if (!criticalMessages.empty())
        {
            summary << L"  • 优先处理上述关键异常，必要时增加告警阈值监控" << std::endl;
        }
        else if (warnCount == 0)
        {
            summary << L"  • 当前日志未发现严重异常，可继续监控趋势" << std::endl;
        }
        else
        {
            summary << L"  • 聚焦 WARN 级别日志，确认潜在风险是否可复现" << std::endl;
        }

        return hstring(summary.str());
    }

    void MainWindow::RefreshStats()
    {
        std::wstringstream stats;
        stats << L"共 " << m_allEntries.size() << L" 条记录，当前显示 " << static_cast<uint32_t>(m_filteredEntries.Size()) << L" 条。";

        if (!m_selectedLevel.empty())
        {
            stats << L" 筛选级别：" << m_selectedLevel;
        }

        auto searchText = std::wstring(SearchBox().Text().c_str());
        if (!searchText.empty())
        {
            stats << L" 关键词：" << searchText;
        }

        if (m_startTimeFilter || m_endTimeFilter)
        {
            auto rangeText = FormatDateRange(m_startTimeFilter, m_endTimeFilter);
            if (!rangeText.empty())
            {
                stats << L" 时间：" << rangeText;
            }
        }

        StatsText().Text(hstring(stats.str()));
    }

    HWND MainWindow::GetWindowHandle() const
    {
        HWND hwnd{};
        if (auto native = this->try_as<::IWindowNative>())
        {
            winrt::check_hresult(native->get_WindowHandle(&hwnd));
        }
        return hwnd;
    }
}
