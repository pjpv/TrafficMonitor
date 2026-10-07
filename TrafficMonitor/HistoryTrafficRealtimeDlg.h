#pragma once
#include "TabDlg.h"
#include "DrawCommon.h"
#include "SecTrafficStore.h"

// 秒級實時流量圖表對話框

class CHistoryTrafficRealtimeDlg : public CTabDlg
{
    DECLARE_DYNAMIC(CHistoryTrafficRealtimeDlg)

public:
    CHistoryTrafficRealtimeDlg(CWnd* pParent = nullptr);
    virtual ~CHistoryTrafficRealtimeDlg();

    // 對話框資料
#ifdef AFX_DESIGN_TIME
    enum { IDD = IDD_HISTORY_TRAFFIC_REALTIME_DIALOG };
#endif

protected:
    // 圖表上的一個取樣點
    struct SecChartPoint
    {
        int x{};            // 繪圖區內的橫座標
        int down{};         // 下行速度對應的像素高度
        int up{};           // 上行速度對應的像素高度
        bool connect{ false };  // 是否與前一個取樣點相連（用於標示資料中斷）
    };

    CComboBox m_range_combo;
    CComboBox m_scale_combo;
    int m_range_seconds{ 60 };                  // 當前選取的時間區間（秒）
    bool m_log_scale{ false };                  // 縱軸是否採用對數比例（預設線性）
    std::vector<SecTrafficRecord> m_records;    // 當前時間區間內的記錄
    CString m_info_text;                        // 頂部顯示的當前網速文字
    CRect m_chart_rect;                         // 圖表的繪製區域
    CFont m_axis_font;                          // 座標軸標籤使用的字體

    void InitRangeCombo();
    void InitScaleCombo();
    void UpdateRecords();                       // 重新載入當前時間區間內的記錄
    void UpdateInfoText();                      // 更新頂部顯示的當前網速文字
    void CalculateChartRect();
    void DrawChart(CDrawCommon& drawer);
    void DrawSeries(CDrawCommon& drawer, const CRect& plot_rect, const std::vector<SecChartPoint>& points, bool draw_down, COLORREF color);

protected:
    virtual void DoDataExchange(CDataExchange* pDX);    // DDX/DDV 支援

    DECLARE_MESSAGE_MAP()
public:
    virtual BOOL OnInitDialog();
    virtual void OnTabEntered() override;
    afx_msg void OnPaint();
    afx_msg void OnTimer(UINT_PTR nIDEvent);
    afx_msg void OnSize(UINT nType, int cx, int cy);
    afx_msg void OnDestroy();
    afx_msg void OnCbnSelchangeSecRangeCombo();
    afx_msg void OnCbnSelchangeSecScaleCombo();
};
