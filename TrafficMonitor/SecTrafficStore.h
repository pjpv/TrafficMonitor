#pragma once
#include <ctime>
#include <deque>
#include <fstream>
#include <mutex>
#include <string>
#include <vector>

// 秒級流量記錄，每條記錄代表 1 秒內的網速
#pragma pack(push, 1)
struct SecTrafficRecord
{
    unsigned int time{};        // Unix 時間戳（秒）
    unsigned int down_speed{};  // 下行速度（位元組/秒）
    unsigned int up_speed{};    // 上行速度（位元組/秒）
};
#pragma pack(pop)

// 分段檔案的檔頭
#pragma pack(push, 1)
struct SecTrafficFileHeader
{
    char magic[8]{};                // 固定為 "TMSEC01"
    unsigned int version{};         // 檔案格式版本
    unsigned int record_size{};     // 單條記錄的位元組數
};
#pragma pack(pop)

// 記憶體中保留的記錄數量（2 小時）
constexpr size_t SEC_TRAFFIC_RECENT_CAPACITY = 7200;

// 秒級歷史流量儲存：按日分段存檔，並在記憶體中保留最近一段時間的記錄。
// 採樣以「整秒」為桶累加，因此無論監控間隔設定為多少，落盤的記錄固定為每秒一條。
// 採樣在監控執行緒中進行、讀取在介面執行緒中進行，所有公開介面均以互斥鎖保護。
class CSecTrafficStore
{
public:
    // 設定存檔目錄，載入最近的記錄並清理過期的分段檔案
    void Init(const std::wstring& dir_path);

    // 設定分段檔案保留天數，0 表示永久保留
    void SetKeepDays(int keep_days);
    int GetKeepDays() const;

    void SetEnabled(bool enabled);
    bool IsEnabled() const;

    // 累加一次採樣。elapsed_ms 為本次採樣覆蓋的時間長度，down_speed/up_speed 為該段時間內的平均速度（位元組/秒）
    void AddSample(unsigned __int64 down_speed, unsigned __int64 up_speed, int elapsed_ms);

    // 將尚未寫入檔案的記錄寫入磁碟
    void Flush();

    // 依保留天數刪除過期的分段檔案
    void CleanUp();

    // 讀取 [from, to) 區間內的記錄，結果按時間遞增排列
    void LoadRange(unsigned int from, unsigned int to, std::vector<SecTrafficRecord>& records);

private:
    void FlushInternal();                               // 已持有鎖時使用
    void CleanUpInternal();                             // 已持有鎖時使用
    void EmitBucket();                                  // 結束當前整秒的累加，產生一條記錄
    void WriteRecord(const SecTrafficRecord& record);   // 將一條記錄寫入對應的分段檔案
    void LoadRecentRecords();                           // 從最新的分段檔案載入最近的記錄到記憶體
    void LoadRangeFromFiles(unsigned int from, unsigned int to, std::vector<SecTrafficRecord>& records) const;

    std::wstring SegmentPath(unsigned int unix_second) const;   // 取得指定時間所屬的分段檔案路徑
    static std::wstring SegmentFileName(const struct tm& local_tm);
    static bool ParseSegmentDate(const std::wstring& file_name, int& year, int& month, int& day);

private:
    mutable std::mutex m_mutex;

    std::wstring m_dir;                     // 分段檔案所在目錄（以反斜線結尾）
    std::wstring m_current_segment;         // 當前開啟的分段檔案路徑
    std::ofstream m_file;

    std::deque<SecTrafficRecord> m_recent;      // 最近若干秒的記錄，含尚未寫入檔案的部分
    std::vector<SecTrafficRecord> m_pending;    // 尚未寫入檔案的記錄

    unsigned int m_bucket_second{};         // 當前正在累加的整秒（0 表示尚未開始）
    unsigned __int64 m_bucket_down{};       // 當前整秒內累加的下行位元組數
    unsigned __int64 m_bucket_up{};         // 當前整秒內累加的上行位元組數
    int m_bucket_ms{};                      // 當前整秒內累加的毫秒數

    int m_keep_days{ 90 };
    bool m_enabled{ true };
};
