// HistoryTrafficRealtimeDlg.cpp : 實現檔案
//

#include "stdafx.h"
#include "TrafficMonitor.h"
#include "HistoryTrafficRealtimeDlg.h"
#include "afxdialogex.h"
#include "DrawCommonHelper.h"
#include "Common.h"

namespace
{
    struct RangeItem
    {
        int seconds;                // 時間區間長度（秒）
        const wchar_t* text_id;     // 顯示文字在字串表中的鍵
    };

    const RangeItem RANGE_ITEMS[]{
        { 60,       L"TXT_SEC_RANGE_1MIN" },
        { 300,      L"TXT_SEC_RANGE_5MIN" },
        { 1800,     L"TXT_SEC_RANGE_30MIN" },
        { 3600,     L"TXT_SEC_RANGE_1HOUR" },
        { 21600,    L"TXT_SEC_RANGE_6HOUR" },
        { 86400,    L"TXT_SEC_RANGE_24HOUR" },
    };

    // 縱軸下限，避免資料為空或極小時比例失真
    constexpr unsigned __int64 MIN_AXIS_MAX_SPEED = 1024;

    const COLORREF CHART_BACK_COLOR = RGB(255, 255, 255);
    const COLORREF CHART_BORDER_COLOR = RGB(200, 200, 200);
    const COLORREF CHART_GRID_COLOR = RGB(224, 224, 224);
    const COLORREF CHART_TEXT_COLOR = RGB(96, 96, 96);
}

// CHistoryTrafficRealtimeDlg 對話框

IMPLEMENT_DYNAMIC(CHistoryTrafficRealtimeDlg, CTabDlg)

CHistoryTrafficRealtimeDlg::CHistoryTrafficRealtimeDlg(CWnd* pParent /*=nullptr*/)
    : CTabDlg(IDD_HISTORY_TRAFFIC_REALTIME_DIALOG, pParent)
{
}

CHistoryTrafficRealtimeDlg::~CHistoryTrafficRealtimeDlg()
{
}

void CHistoryTrafficRealtimeDlg::DoDataExchange(CDataExchange* pDX)
{
    CTabDlg::DoDataExchange(pDX);
    DDX_Control(pDX, IDC_SEC_RANGE_COMBO, m_range_combo);
}

BEGIN_MESSAGE_MAP(CHistoryTrafficRealtimeDlg, CTabDlg)
    ON_WM_PAINT()
    ON_WM_TIMER()
    ON_WM_SIZE()
    ON_WM_DESTROY()
    ON_CBN_SELCHANGE(IDC_SEC_RANGE_COMBO, &CHistoryTrafficRealtimeDlg::OnCbnSelchangeSecRangeCombo)
END_MESSAGE_MAP()

BOOL CHistoryTrafficRealtimeDlg::OnInitDialog()
{
    CTabDlg::OnInitDialog();

    InitRangeCombo();

    // 座標軸標籤使用比對話框字體稍小的字體
    CFont* p_font = GetFont();
    if (p_font != nullptr)
    {
        LOGFONT log_font{};
        p_font->GetLogFont(&log_font);
        log_font.lfHeight = static_cast<LONG>(log_font.lfHeight * 0.85);
        m_axis_font.CreateFontIndirect(&log_font);
    }

    CalculateChartRect();
    UpdateRecords();
    UpdateCurrentText();

    SetTimer(SEC_TRAFFIC_TIMER, 1000, nullptr);

    return TRUE;
}

void CHistoryTrafficRealtimeDlg::OnTabEntered()
{
    UpdateRecords();
    UpdateCurrentText();
    Invalidate(FALSE);
}

void CHistoryTrafficRealtimeDlg::InitRangeCombo()
{
    m_range_combo.ResetContent();
    const int item_count = static_cast<int>(_countof(RANGE_ITEMS));
    int select = 0;
    for (int i{}; i < item_count; i++)
    {
        m_range_combo.AddString(CCommon::LoadText(RANGE_ITEMS[i].text_id));
        if (RANGE_ITEMS[i].seconds == m_range_seconds)
            select = i;
    }
    m_range_combo.SetCurSel(select);
    m_range_seconds = RANGE_ITEMS[select].seconds;
}

void CHistoryTrafficRealtimeDlg::CalculateChartRect()
{
    CRect client_rect;
    GetClientRect(client_rect);

    // 圖表位於下拉式選單下方，四周留出邊距
    const int margin = theApp.DPI(7);
    int top = margin;
    if (m_range_combo.GetSafeHwnd() != nullptr)
    {
        // 依控制項的實際位置決定圖表上邊界，避免字體大小改變時重疊
        CRect combo_rect;
        m_range_combo.GetWindowRect(combo_rect);
        ScreenToClient(combo_rect);
        top = combo_rect.bottom + margin;
    }

    m_chart_rect = client_rect;
    m_chart_rect.left += margin;
    m_chart_rect.right -= margin;
    m_chart_rect.top = top;
    m_chart_rect.bottom -= margin;
    if (m_chart_rect.right < m_chart_rect.left)
        m_chart_rect.right = m_chart_rect.left;
    if (m_chart_rect.bottom < m_chart_rect.top)
        m_chart_rect.bottom = m_chart_rect.top;
}

void CHistoryTrafficRealtimeDlg::UpdateRecords()
{
    const unsigned int range = static_cast<unsigned int>(m_range_seconds);
    unsigned int now = static_cast<unsigned int>(::time(nullptr)) + 1;      // 加 1 秒以包含當前正在累加的那一秒
    unsigned int from = now > range ? now - range : 0;
    theApp.m_sec_traffic.LoadRange(from, now, m_records);
}

void CHistoryTrafficRealtimeDlg::UpdateCurrentText()
{
    CString str = CCommon::LoadTextFormat(L"TXT_SEC_CURRENT",
        { CCommon::DataSizeToString(theApp.m_in_speed, false) + _T("/s"),
          CCommon::DataSizeToString(theApp.m_out_speed, false) + _T("/s"),
          CCommon::DataSizeToString(m_records.empty() ? 0 : m_records.back().down_speed, false) + _T("/s") });
    SetDlgItemText(IDC_SEC_CURRENT_STATIC, str);
}

void CHistoryTrafficRealtimeDlg::OnTimer(UINT_PTR nIDEvent)
{
    if (nIDEvent == SEC_TRAFFIC_TIMER)
    {
        // 分頁未顯示時不需要重繪
        if (IsWindowVisible())
        {
            UpdateRecords();
            UpdateCurrentText();
            Invalidate(FALSE);
        }
    }

    CTabDlg::OnTimer(nIDEvent);
}

void CHistoryTrafficRealtimeDlg::OnSize(UINT nType, int cx, int cy)
{
    CTabDlg::OnSize(nType, cx, cy);

    if (nType != SIZE_MINIMIZED)
        CalculateChartRect();
}

void CHistoryTrafficRealtimeDlg::OnDestroy()
{
    KillTimer(SEC_TRAFFIC_TIMER);
    CTabDlg::OnDestroy();
}

void CHistoryTrafficRealtimeDlg::OnCbnSelchangeSecRangeCombo()
{
    int select = m_range_combo.GetCurSel();
    if (select < 0 || select >= static_cast<int>(_countof(RANGE_ITEMS)))
        return;

    m_range_seconds = RANGE_ITEMS[select].seconds;
    UpdateRecords();
    UpdateCurrentText();
    Invalidate(FALSE);
}

void CHistoryTrafficRealtimeDlg::OnPaint()
{
    CPaintDC dc(this);

    if (m_chart_rect.IsRectEmpty())
        return;

    // 使用雙緩衝繪圖，避免每秒重繪時閃爍
    CDrawDoubleBuffer draw_double_buffer(&dc, m_chart_rect);
    CDrawCommon drawer;
    drawer.Create(draw_double_buffer.GetMemDC(), this);

    DrawChart(drawer);
}

void CHistoryTrafficRealtimeDlg::DrawChart(CDrawCommon& drawer)
{
    drawer.FillRect(CRect(0, 0, m_chart_rect.Width(), m_chart_rect.Height()), CHART_BACK_COLOR);
    drawer.DrawRectOutLine(CRect(0, 0, m_chart_rect.Width(), m_chart_rect.Height()), CHART_BORDER_COLOR);

    // 繪圖區四周為座標軸標籤預留的邊距
    CRect plot_rect(0, 0, m_chart_rect.Width(), m_chart_rect.Height());
    plot_rect.left += theApp.DPI(62);
    plot_rect.bottom -= theApp.DPI(18);
    plot_rect.top += theApp.DPI(4);
    plot_rect.right -= theApp.DPI(4);
    if (plot_rect.Width() <= 1 || plot_rect.Height() <= 1)
        return;

    const unsigned int range = static_cast<unsigned int>(m_range_seconds);
    unsigned int now = static_cast<unsigned int>(::time(nullptr)) + 1;
    unsigned int from = now > range ? now - range : 0;

    // 縱軸上限取區間內的最大值
    unsigned __int64 max_speed{};
    for (const SecTrafficRecord& record : m_records)
    {
        if (record.down_speed > max_speed)
            max_speed = record.down_speed;
        if (record.up_speed > max_speed)
            max_speed = record.up_speed;
    }
    if (max_speed < MIN_AXIS_MAX_SPEED)
        max_speed = MIN_AXIS_MAX_SPEED;

    if (m_axis_font.GetSafeHandle() != nullptr)
        drawer.SetFont(&m_axis_font);

    // 橫向網格線與縱軸標籤
    const int y_grid_count = 4;
    for (int i{}; i <= y_grid_count; i++)
    {
        int y = plot_rect.bottom - plot_rect.Height() * i / y_grid_count;
        drawer.DrawLine(CPoint(plot_rect.left, y), CPoint(plot_rect.right, y), CHART_GRID_COLOR);

        unsigned __int64 value = max_speed * i / y_grid_count;
        CRect label_rect(0, y - theApp.DPI(8), plot_rect.left - theApp.DPI(4), y + theApp.DPI(8));
        drawer.DrawWindowText(label_rect, CCommon::DataSizeToString(value, false), CHART_TEXT_COLOR, IDrawCommon::Alignment::RIGHT);
    }

    // 縱向網格線與橫軸時間標籤
    const int x_grid_count = 4;
    for (int i{}; i <= x_grid_count; i++)
    {
        int x = plot_rect.left + plot_rect.Width() * i / x_grid_count;
        drawer.DrawLine(CPoint(x, plot_rect.top), CPoint(x, plot_rect.bottom), CHART_GRID_COLOR);

        unsigned int label_time = from + static_cast<unsigned int>(static_cast<unsigned __int64>(range) * i / x_grid_count);
        time_t t = static_cast<time_t>(label_time);
        tm local_tm{};
        localtime_s(&local_tm, &t);
        CString str;
        str.Format(_T("%02d:%02d:%02d"), local_tm.tm_hour, local_tm.tm_min, local_tm.tm_sec);

        IDrawCommon::Alignment align = IDrawCommon::Alignment::CENTER;
        if (i == 0)
            align = IDrawCommon::Alignment::LEFT;
        else if (i == x_grid_count)
            align = IDrawCommon::Alignment::RIGHT;
        CRect label_rect(x - theApp.DPI(32), plot_rect.bottom + theApp.DPI(2), x + theApp.DPI(32), plot_rect.bottom + theApp.DPI(16));
        drawer.DrawWindowText(label_rect, str, CHART_TEXT_COLOR, align);
    }

    // 每個像素列對應一個時間桶，桶內取平均值
    const int width = plot_rect.Width();
    std::vector<unsigned __int64> down_sum(width, 0);
    std::vector<unsigned __int64> up_sum(width, 0);
    std::vector<int> count(width, 0);
    for (const SecTrafficRecord& record : m_records)
    {
        if (record.time < from)
            continue;
        unsigned int offset = record.time - from;
        if (offset >= range)
            continue;
        int x = static_cast<int>(static_cast<unsigned __int64>(offset) * width / range);
        if (x < 0 || x >= width)
            continue;
        down_sum[x] += record.down_speed;
        up_sum[x] += record.up_speed;
        count[x]++;
    }

    std::vector<int> down_values(width, 0);
    std::vector<int> up_values(width, 0);
    std::vector<bool> valid(width, false);
    for (int x{}; x < width; x++)
    {
        if (count[x] == 0)
            continue;
        valid[x] = true;
        down_values[x] = static_cast<int>((down_sum[x] / count[x]) * plot_rect.Height() / max_speed);
        up_values[x] = static_cast<int>((up_sum[x] / count[x]) * plot_rect.Height() / max_speed);
    }

    DrawSeries(drawer, plot_rect, down_values, valid, TRAFFIC_COLOR_BLUE);
    DrawSeries(drawer, plot_rect, up_values, valid, TRAFFIC_COLOR_RED);

    if (m_records.empty())
    {
        CRect text_rect = plot_rect;
        drawer.DrawWindowText(text_rect, CCommon::LoadText(L"TXT_SEC_NO_DATA"), CHART_TEXT_COLOR, IDrawCommon::Alignment::CENTER);
    }
}

void CHistoryTrafficRealtimeDlg::DrawSeries(CDrawCommon& drawer, const CRect& plot_rect, const std::vector<int>& values, const std::vector<bool>& valid, COLORREF color)
{
    CPoint prev_point{};
    bool has_prev{ false };
    for (int x{}; x < static_cast<int>(values.size()); x++)
    {
        if (!valid[x])
        {
            // 資料中斷（例如程式未執行），重新起一條折線
            has_prev = false;
            continue;
        }

        CPoint point(plot_rect.left + x, plot_rect.bottom - values[x]);
        if (has_prev)
            drawer.DrawLine(prev_point, point, color);
        prev_point = point;
        has_prev = true;
    }
}
