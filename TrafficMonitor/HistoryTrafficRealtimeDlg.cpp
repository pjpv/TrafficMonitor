// HistoryTrafficRealtimeDlg.cpp : 實現檔案
//

#include "stdafx.h"
#include "TrafficMonitor.h"
#include "HistoryTrafficRealtimeDlg.h"
#include "afxdialogex.h"
#include "DrawCommonHelper.h"
#include "Common.h"
#include <cmath>

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

    const COLORREF CHART_BACK_COLOR = RGB(255, 255, 255);
    const COLORREF CHART_BORDER_COLOR = RGB(200, 200, 200);
    const COLORREF CHART_GRID_COLOR = RGB(224, 224, 224);
    const COLORREF CHART_TEXT_COLOR = RGB(96, 96, 96);

    const int Y_GRID_COUNT = 4;     // 縱軸網格線數量
    const int X_GRID_COUNT = 4;     // 橫軸網格線數量

    // 縱軸對數比例的基準值（1 KB/s），低於此值的速度一律繪於底線
    constexpr unsigned __int64 SEC_AXIS_MIN_SPEED = 1024;

    // 相鄰記錄的時間間隔超過此秒數時，視為程式未執行造成的資料中斷
    constexpr unsigned int SEC_CHART_GAP_SECONDS = 3;
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
    UpdateInfoText();

    SetTimer(SEC_TRAFFIC_TIMER, 1000, nullptr);

    return TRUE;
}

void CHistoryTrafficRealtimeDlg::OnTabEntered()
{
    UpdateRecords();
    UpdateInfoText();
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

void CHistoryTrafficRealtimeDlg::UpdateInfoText()
{
    // 區間峰值取當前區間內的最大值
    unsigned __int64 peak{};
    for (const SecTrafficRecord& record : m_records)
    {
        if (record.down_speed > peak)
            peak = record.down_speed;
        if (record.up_speed > peak)
            peak = record.up_speed;
    }

    m_info_text = CCommon::LoadTextFormat(L"TXT_SEC_CURRENT",
        { CCommon::DataSizeToString(theApp.m_in_speed, false) + _T("/s"),
          CCommon::DataSizeToString(theApp.m_out_speed, false) + _T("/s"),
          CCommon::DataSizeToString(peak, false) + _T("/s") });
}

void CHistoryTrafficRealtimeDlg::OnTimer(UINT_PTR nIDEvent)
{
    if (nIDEvent == SEC_TRAFFIC_TIMER)
    {
        // 分頁未顯示時不需要重繪
        if (IsWindowVisible())
        {
            UpdateRecords();
            UpdateInfoText();
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
    UpdateInfoText();
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
    const int chart_width = m_chart_rect.Width();
    const int chart_height = m_chart_rect.Height();

    drawer.FillRect(CRect(0, 0, chart_width, chart_height), CHART_BACK_COLOR);
    drawer.DrawRectOutLine(CRect(0, 0, chart_width, chart_height), CHART_BORDER_COLOR);

    // 繪圖區四周為座標軸標籤預留邊距，頂部另留一列顯示當前數值
    CRect plot_rect(0, 0, chart_width, chart_height);
    plot_rect.left += theApp.DPI(62);
    plot_rect.top += theApp.DPI(20);
    plot_rect.right -= theApp.DPI(4);
    plot_rect.bottom -= theApp.DPI(18);
    if (plot_rect.Width() <= 1 || plot_rect.Height() <= 1)
        return;

    const unsigned int range = static_cast<unsigned int>(m_range_seconds);
    unsigned int now = static_cast<unsigned int>(::time(nullptr)) + 1;
    unsigned int from = now > range ? now - range : 0;

    // 縱軸採用對數比例：網速的動態範圍可達數百倍，線性比例會令低流量永遠貼近底線而無法辨識
    unsigned __int64 max_speed{};
    for (const SecTrafficRecord& record : m_records)
    {
        if (record.down_speed > max_speed)
            max_speed = record.down_speed;
        if (record.up_speed > max_speed)
            max_speed = record.up_speed;
    }
    // 保證至少有 4 倍的動態範圍，避免資料平坦時比例失真
    if (max_speed < SEC_AXIS_MIN_SPEED * 4)
        max_speed = SEC_AXIS_MIN_SPEED * 4;

    const double log_min = std::log(static_cast<double>(SEC_AXIS_MIN_SPEED));
    const double log_span = std::log(static_cast<double>(max_speed)) - log_min;

    // 將速度換算成由繪圖區底部起算的像素高度
    auto to_pixels = [&](unsigned __int64 speed) -> int
    {
        double value = static_cast<double>(speed);
        if (value < static_cast<double>(SEC_AXIS_MIN_SPEED))
            value = static_cast<double>(SEC_AXIS_MIN_SPEED);
        if (value > static_cast<double>(max_speed))
            value = static_cast<double>(max_speed);
        return static_cast<int>((std::log(value) - log_min) / log_span * plot_rect.Height() + 0.5);
    };

    if (m_axis_font.GetSafeHandle() != nullptr)
        drawer.SetFont(&m_axis_font);

    // 橫向網格線與縱軸標籤
    for (int i{}; i <= Y_GRID_COUNT; i++)
    {
        int y = plot_rect.bottom - plot_rect.Height() * i / Y_GRID_COUNT;
        drawer.DrawLine(CPoint(plot_rect.left, y), CPoint(plot_rect.right, y), CHART_GRID_COLOR);

        unsigned __int64 value = static_cast<unsigned __int64>(std::exp(log_min + log_span * i / Y_GRID_COUNT) + 0.5);
        CRect label_rect(0, y - theApp.DPI(8), plot_rect.left - theApp.DPI(4), y + theApp.DPI(8));
        drawer.DrawWindowText(label_rect, CCommon::DataSizeToString(value, false), CHART_TEXT_COLOR, IDrawCommon::Alignment::RIGHT);
    }

    // 縱向網格線與橫軸時間標籤
    const CRect chart_rect(0, 0, chart_width, chart_height);
    for (int i{}; i <= X_GRID_COUNT; i++)
    {
        int x = plot_rect.left + plot_rect.Width() * i / X_GRID_COUNT;
        drawer.DrawLine(CPoint(x, plot_rect.top), CPoint(x, plot_rect.bottom), CHART_GRID_COLOR);

        unsigned int label_time = from + static_cast<unsigned int>(static_cast<unsigned __int64>(range) * i / X_GRID_COUNT);
        time_t t = static_cast<time_t>(label_time);
        tm local_tm{};
        localtime_s(&local_tm, &t);
        CString str;
        str.Format(_T("%02d:%02d:%02d"), local_tm.tm_hour, local_tm.tm_min, local_tm.tm_sec);

        IDrawCommon::Alignment align = IDrawCommon::Alignment::CENTER;
        if (i == 0)
            align = IDrawCommon::Alignment::LEFT;
        else if (i == X_GRID_COUNT)
            align = IDrawCommon::Alignment::RIGHT;
        CRect label_rect(x - theApp.DPI(32), plot_rect.bottom + theApp.DPI(2), x + theApp.DPI(32), plot_rect.bottom + theApp.DPI(16));
        CRect clipped;
        clipped.IntersectRect(label_rect, chart_rect);
        drawer.DrawWindowText(clipped, str, CHART_TEXT_COLOR, align);
    }

    // 建立繪圖用的取樣點列
    const int width = plot_rect.Width();
    std::vector<SecChartPoint> points;

    if (m_records.size() <= static_cast<size_t>(width) * 2)
    {
        // 記錄數量不多時直接以每條記錄為一個取樣點，保留完整的時間細節
        unsigned int prev_time{};
        bool has_prev{ false };
        for (const SecTrafficRecord& record : m_records)
        {
            if (record.time < from)
                continue;
            unsigned int offset = record.time - from;
            if (offset >= range)
                continue;

            SecChartPoint point;
            point.x = plot_rect.left + static_cast<int>(static_cast<unsigned __int64>(offset) * width / range);
            point.down = to_pixels(record.down_speed);
            point.up = to_pixels(record.up_speed);
            // 相鄰記錄的時間間隔過大，說明程式當時並未執行，曲線應該斷開
            point.connect = has_prev && (record.time - prev_time <= SEC_CHART_GAP_SECONDS);
            points.push_back(point);

            prev_time = record.time;
            has_prev = true;
        }
    }
    else
    {
        // 記錄數量過多時按像素列聚合，取列內「最大值」而非平均值：
        // 一秒的突發流量被數十秒稀釋後會從圖上消失，只有最大值能保留突發
        std::vector<unsigned __int64> down_max(width, 0);
        std::vector<unsigned __int64> up_max(width, 0);
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
            if (record.down_speed > down_max[x])
                down_max[x] = record.down_speed;
            if (record.up_speed > up_max[x])
                up_max[x] = record.up_speed;
            count[x]++;
        }

        int prev_x{};
        bool has_prev{ false };
        for (int x{}; x < width; x++)
        {
            if (count[x] == 0)
                continue;

            SecChartPoint point;
            point.x = plot_rect.left + x;
            point.down = to_pixels(down_max[x]);
            point.up = to_pixels(up_max[x]);
            point.connect = has_prev && (x - prev_x <= 2);
            points.push_back(point);

            prev_x = x;
            has_prev = true;
        }
    }

    // 由下而上疊放：先畫上行再畫下行，令流量較大的下行曲線顯示在上層
    DrawSeries(drawer, plot_rect, points, false, TRAFFIC_COLOR_RED);
    DrawSeries(drawer, plot_rect, points, true, TRAFFIC_COLOR_BLUE);

    // 頂部數值文字
    drawer.SetFont(GetFont());
    CRect info_rect(plot_rect.left, theApp.DPI(2), chart_width - theApp.DPI(4), plot_rect.top);
    drawer.DrawWindowText(info_rect, m_info_text, CHART_TEXT_COLOR, IDrawCommon::Alignment::LEFT);

    if (m_records.empty())
    {
        CRect text_rect = plot_rect;
        drawer.DrawWindowText(text_rect, CCommon::LoadText(L"TXT_SEC_NO_DATA"), CHART_TEXT_COLOR, IDrawCommon::Alignment::CENTER);
    }
}

void CHistoryTrafficRealtimeDlg::DrawSeries(CDrawCommon& drawer, const CRect& plot_rect, const std::vector<SecChartPoint>& points, bool draw_down, COLORREF color)
{
    CPoint prev{};
    for (size_t i{}; i < points.size(); i++)
    {
        CPoint point(points[i].x, plot_rect.bottom - (draw_down ? points[i].down : points[i].up));
        bool linked_prev = (i > 0) && points[i].connect;
        bool linked_next = (i + 1 < points.size()) && points[i + 1].connect;

        if (linked_prev)
        {
            // 以階梯線連接：相鄰取樣點常落在同一像素列，直接連線會退化為零長度而不被 GDI 繪製
            drawer.DrawLine(prev, CPoint(point.x, prev.y), color);
            drawer.DrawLine(CPoint(point.x, prev.y), point, color);
        }
        else if (!linked_next)
        {
            // 孤立點沒有可連接的鄰居，補一個短豎線使其可見
            drawer.DrawLine(point, CPoint(point.x, point.y - 1), color);
        }

        prev = point;
    }
}
