#pragma once

#include "MainWindow.g.h"

namespace winrt::LogMinds::implementation
{
    struct MainWindow : MainWindowT<MainWindow>
    {
        MainWindow();

        int32_t MyProperty();
        void MyProperty(int32_t value);

        void OnOpenLogClicked(winrt::Windows::Foundation::IInspectable const& sender, winrt::Microsoft::UI::Xaml::RoutedEventArgs const& args);
        void OnInterpretClicked(winrt::Windows::Foundation::IInspectable const& sender, winrt::Microsoft::UI::Xaml::RoutedEventArgs const& args);
        void OnSearchTextChanged(winrt::Windows::Foundation::IInspectable const& sender, winrt::Microsoft::UI::Xaml::Controls::TextChangedEventArgs const& args);
        void OnSeverityChanged(winrt::Windows::Foundation::IInspectable const& sender, winrt::Microsoft::UI::Xaml::Controls::SelectionChangedEventArgs const& args);
        void OnStartDateChanged(winrt::Windows::Foundation::IInspectable const& sender, winrt::Microsoft::UI::Xaml::Controls::DatePickerValueChangedEventArgs const& args);
        void OnEndDateChanged(winrt::Windows::Foundation::IInspectable const& sender, winrt::Microsoft::UI::Xaml::Controls::DatePickerValueChangedEventArgs const& args);
        void OnStartTimeChanged(winrt::Windows::Foundation::IInspectable const& sender, winrt::Microsoft::UI::Xaml::Controls::TimePickerValueChangedEventArgs const& args);
        void OnEndTimeChanged(winrt::Windows::Foundation::IInspectable const& sender, winrt::Microsoft::UI::Xaml::Controls::TimePickerValueChangedEventArgs const& args);
        void OnClearFilters(winrt::Windows::Foundation::IInspectable const& sender, winrt::Microsoft::UI::Xaml::RoutedEventArgs const& args);

    private:
        struct ParsedEntry
        {
            winrt::LogMinds::LogEntry Entry{ nullptr };
            std::optional<winrt::Windows::Foundation::DateTime> OccurredOn{};
            std::wstring NormalizedLevel;
        };

        winrt::Windows::Foundation::Collections::IObservableVector<winrt::LogMinds::LogEntry> m_filteredEntries{ nullptr };
        std::vector<ParsedEntry> m_allEntries;
        int32_t m_myProperty{ 0 };
        bool m_isLoading{ false };
        std::wstring m_searchTerm;
        std::wstring m_selectedLevel;
        std::optional<winrt::Windows::Foundation::DateTime> m_startTimeFilter;
        std::optional<winrt::Windows::Foundation::DateTime> m_endTimeFilter;
        winrt::hstring m_lastSummary;
        winrt::hstring m_currentFileName;

        winrt::fire_and_forget LoadLogsAsync();
        winrt::fire_and_forget InterpretAsync();
        void UpdateFilters();
        void ApplyFilters();
        void UpdateUiState();
        void UpdateSummary(winrt::hstring const& summary);
        ParsedEntry ParseLine(std::wstring const& line);
        ParsedEntry ParseJsonObject(winrt::Windows::Data::Json::JsonObject const& object, std::wstring const& rawLine);
        winrt::LogMinds::LogEntry CreateEntry();
        static std::wstring Trim(std::wstring_view text);
        static std::wstring ToLower(std::wstring value);
        static std::wstring ToUpper(std::wstring value);
        static std::optional<winrt::Windows::Foundation::DateTime> ParseTimestamp(std::wstring_view text);
        static std::optional<winrt::Windows::Foundation::DateTime> ParseSyslogTimestamp(std::wstring_view text);
        static std::wstring FormatDateRange(std::optional<winrt::Windows::Foundation::DateTime> const& start, std::optional<winrt::Windows::Foundation::DateTime> const& end);
        winrt::hstring BuildSummary();
        void RefreshStats();
        HWND GetWindowHandle() const;
    };
}

namespace winrt::LogMinds::factory_implementation
{
    struct MainWindow : MainWindowT<MainWindow, implementation::MainWindow>
    {
    };
}
