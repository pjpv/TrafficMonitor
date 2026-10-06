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
    CComboBox m_range_combo;
    int m_range_seconds{ 60 };                  // 當前選取的時間區間（秒）
    std::vector<SecTrafficRecord> m_records;    // 當前時間區間內的記錄
    CRect m_chart_rect;                         // 圖表的繪製區域
    CFont m_axis_font;                          // 座標軸標籤使用的字體

    void InitRangeCombo();
    void UpdateRecords();                       // 重新載入當前時間區間內的記錄
    void UpdateCurrentText();                   // 更新當前網速的文字
    void CalculateChartRect();
    void DrawChart(CDrawCommon& drawer);
    void DrawSeries(CDrawCommon& drawer, const CRect& plot_rect, const std::vector<int>& values, const std::vector<bool>& valid, COLORREF color);

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
};
