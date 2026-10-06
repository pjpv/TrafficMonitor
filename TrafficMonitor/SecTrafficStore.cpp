#include "stdafx.h"
#include "SecTrafficStore.h"
#include "Common.h"
#include <ctime>

namespace
{
    constexpr char SEC_TRAFFIC_MAGIC[8]{ 'T', 'M', 'S', 'E', 'C', '0', '1', '\0' };

    // 網速以位元組/秒為單位儲存，超過 4GB/s 的極端值截斷，避免溢位
    unsigned int ClampToUint32(unsigned __int64 value)
    {
        return value > 0xFFFFFFFFu ? 0xFFFFFFFFu : static_cast<unsigned int>(value);
    }

    // 讀取一個分段檔案中落在 [from, to) 區間內的記錄
    void ReadSegmentFile(const std::wstring& path, unsigned int from, unsigned int to, std::vector<SecTrafficRecord>& records)
    {
        std::ifstream file(path.c_str(), std::ios::binary);
        if (!file.is_open())
            return;

        SecTrafficFileHeader header{};
        if (!file.read(reinterpret_cast<char*>(&header), sizeof(header)))
            return;
        if (memcmp(header.magic, SEC_TRAFFIC_MAGIC, sizeof(SEC_TRAFFIC_MAGIC)) != 0
            || header.record_size != sizeof(SecTrafficRecord))
            return;

        SecTrafficRecord record{};
        while (file.read(reinterpret_cast<char*>(&record), sizeof(record)))
        {
            // 記錄按時間遞增排列，超出上界即可結束
            if (record.time >= to)
                break;
            if (record.time >= from)
                records.push_back(record);
        }
    }
}

void CSecTrafficStore::Init(const std::wstring& dir_path)
{
    std::lock_guard<std::mutex> lock(m_mutex);

    m_dir = dir_path;
    if (!m_dir.empty() && m_dir.back() != L'\\')
        m_dir += L'\\';
    if (m_dir.empty())
        return;

    ::CreateDirectory(m_dir.c_str(), nullptr);

    CleanUpInternal();
    LoadRecentRecords();
}

void CSecTrafficStore::SetKeepDays(int keep_days)
{
    std::lock_guard<std::mutex> lock(m_mutex);
    m_keep_days = keep_days;
}

int CSecTrafficStore::GetKeepDays() const
{
    std::lock_guard<std::mutex> lock(m_mutex);
    return m_keep_days;
}

void CSecTrafficStore::SetEnabled(bool enabled)
{
    std::lock_guard<std::mutex> lock(m_mutex);
    m_enabled = enabled;
}

bool CSecTrafficStore::IsEnabled() const
{
    std::lock_guard<std::mutex> lock(m_mutex);
    return m_enabled;
}

void CSecTrafficStore::AddSample(unsigned __int64 down_speed, unsigned __int64 up_speed, int elapsed_ms)
{
    std::lock_guard<std::mutex> lock(m_mutex);

    if (!m_enabled || m_dir.empty() || elapsed_ms <= 0)
        return;

    unsigned int now_second = static_cast<unsigned int>(::time(nullptr));
    if (m_bucket_second == 0)
        m_bucket_second = now_second;

    // 以「當前整秒」為桶累加，桶滿一秒後才產生一條記錄，因此落盤頻率固定為每秒一條
    m_bucket_down += down_speed * static_cast<unsigned __int64>(elapsed_ms) / 1000u;
    m_bucket_up += up_speed * static_cast<unsigned __int64>(elapsed_ms) / 1000u;
    m_bucket_ms += elapsed_ms;

    if (now_second > m_bucket_second)
    {
        EmitBucket();
        m_bucket_second = now_second;
        m_bucket_down = 0;
        m_bucket_up = 0;
        m_bucket_ms = 0;
    }
}

void CSecTrafficStore::EmitBucket()
{
    if (m_bucket_ms <= 0)
        return;

    SecTrafficRecord record;
    record.time = m_bucket_second;
    record.down_speed = ClampToUint32(m_bucket_down * 1000u / static_cast<unsigned __int64>(m_bucket_ms));
    record.up_speed = ClampToUint32(m_bucket_up * 1000u / static_cast<unsigned __int64>(m_bucket_ms));

    m_recent.push_back(record);
    while (m_recent.size() > SEC_TRAFFIC_RECENT_CAPACITY)
        m_recent.pop_front();

    m_pending.push_back(record);
}

void CSecTrafficStore::Flush()
{
    std::lock_guard<std::mutex> lock(m_mutex);
    FlushInternal();
}

void CSecTrafficStore::FlushInternal()
{
    if (!m_pending.empty())
    {
        for (const SecTrafficRecord& record : m_pending)
            WriteRecord(record);
        m_pending.clear();
    }

    if (m_file.is_open())
        m_file.flush();
}

void CSecTrafficStore::WriteRecord(const SecTrafficRecord& record)
{
    std::wstring segment = SegmentPath(record.time);
    if (segment != m_current_segment)
    {
        if (m_file.is_open())
        {
            m_file.flush();
            m_file.close();
        }
        m_current_segment = segment;

        // 新檔案需要先寫入檔頭
        bool need_header = !CCommon::FileExist(segment.c_str());
        m_file.open(segment.c_str(), std::ios::binary | std::ios::app);
        if (!m_file.is_open())
        {
            m_current_segment.clear();
            return;
        }
        if (need_header)
        {
            SecTrafficFileHeader header{};
            memcpy(header.magic, SEC_TRAFFIC_MAGIC, sizeof(header.magic));
            header.version = 1;
            header.record_size = sizeof(SecTrafficRecord);
            m_file.write(reinterpret_cast<const char*>(&header), sizeof(header));
        }
    }

    m_file.write(reinterpret_cast<const char*>(&record), sizeof(record));
}

void CSecTrafficStore::LoadRecentRecords()
{
    m_recent.clear();
    if (m_dir.empty())
        return;

    // 先讀當前分段，不足記憶體窗口時再往前讀一天
    time_t now = ::time(nullptr);
    for (int day_offset = 0; day_offset <= 1 && m_recent.size() < SEC_TRAFFIC_RECENT_CAPACITY; day_offset++)
    {
        time_t t = now - static_cast<time_t>(day_offset) * 86400;
        std::vector<SecTrafficRecord> records;
        ReadSegmentFile(SegmentPath(static_cast<unsigned int>(t)), 0, 0xFFFFFFFFu, records);
        if (day_offset == 0)
            m_recent.assign(records.begin(), records.end());
        else
            m_recent.insert(m_recent.begin(), records.begin(), records.end());
    }

    while (m_recent.size() > SEC_TRAFFIC_RECENT_CAPACITY)
        m_recent.pop_front();
}

void CSecTrafficStore::LoadRange(unsigned int from, unsigned int to, std::vector<SecTrafficRecord>& records)
{
    std::lock_guard<std::mutex> lock(m_mutex);

    records.clear();
    if (to <= from)
        return;

    // 先落盤，保證檔案內容與記憶體窗口不重疊、不留缺口
    FlushInternal();

    if (m_recent.empty())
    {
        LoadRangeFromFiles(from, to, records);
        return;
    }

    // 記憶體窗口之前的部分從檔案讀取
    unsigned int mem_begin = m_recent.front().time;
    if (from < mem_begin)
    {
        unsigned int file_to = mem_begin < to ? mem_begin : to;
        if (file_to > from)
            LoadRangeFromFiles(from, file_to, records);
    }

    // 記憶體窗口內的部分直接取用
    for (const SecTrafficRecord& record : m_recent)
    {
        if (record.time < from)
            continue;
        if (record.time >= to)
            break;
        records.push_back(record);
    }
}

void CSecTrafficStore::LoadRangeFromFiles(unsigned int from, unsigned int to, std::vector<SecTrafficRecord>& records) const
{
    if (m_dir.empty())
        return;

    const time_t end = static_cast<time_t>(to);

    // 以本地時間的「當日中午」為游標逐日前進，可避免日光節約時間造成的日期跳變
    tm local_tm{};
    time_t cursor = static_cast<time_t>(from);
    localtime_s(&local_tm, &cursor);
    local_tm.tm_hour = 12;
    local_tm.tm_min = 0;
    local_tm.tm_sec = 0;
    local_tm.tm_isdst = -1;
    cursor = mktime(&local_tm);

    while (cursor < end)
    {
        localtime_s(&local_tm, &cursor);
        ReadSegmentFile(m_dir + SegmentFileName(local_tm), from, to, records);

        local_tm.tm_mday += 1;      // mktime 會自動正規化跨月、跨年
        local_tm.tm_hour = 12;
        local_tm.tm_min = 0;
        local_tm.tm_sec = 0;
        local_tm.tm_isdst = -1;
        cursor = mktime(&local_tm);
    }
}

void CSecTrafficStore::CleanUp()
{
    std::lock_guard<std::mutex> lock(m_mutex);
    CleanUpInternal();
}

void CSecTrafficStore::CleanUpInternal()
{
    if (m_keep_days <= 0 || m_dir.empty())
        return;

    time_t cutoff = ::time(nullptr) - static_cast<time_t>(m_keep_days) * 86400;
    tm cutoff_tm{};
    localtime_s(&cutoff_tm, &cutoff);
    const int cutoff_date = (cutoff_tm.tm_year + 1900) * 10000 + (cutoff_tm.tm_mon + 1) * 100 + cutoff_tm.tm_mday;

    std::wstring pattern = m_dir + L"*.dat";
    WIN32_FIND_DATA find_data{};
    HANDLE h_find = ::FindFirstFile(pattern.c_str(), &find_data);
    if (h_find == INVALID_HANDLE_VALUE)
        return;

    do
    {
        int year{}, month{}, day{};
        if (!ParseSegmentDate(find_data.cFileName, year, month, day))
            continue;
        if (year * 10000 + month * 100 + day >= cutoff_date)
            continue;

        std::wstring path = m_dir + find_data.cFileName;
        if (path != m_current_segment)
            ::DeleteFile(path.c_str());
    } while (::FindNextFile(h_find, &find_data));

    ::FindClose(h_find);
}

std::wstring CSecTrafficStore::SegmentPath(unsigned int unix_second) const
{
    time_t t = static_cast<time_t>(unix_second);
    tm local_tm{};
    localtime_s(&local_tm, &t);
    return m_dir + SegmentFileName(local_tm);
}

std::wstring CSecTrafficStore::SegmentFileName(const struct tm& local_tm)
{
    wchar_t name[32]{};
    swprintf_s(name, L"%04d%02d%02d.dat", local_tm.tm_year + 1900, local_tm.tm_mon + 1, local_tm.tm_mday);
    return name;
}

bool CSecTrafficStore::ParseSegmentDate(const std::wstring& file_name, int& year, int& month, int& day)
{
    // 分段檔案名固定為 YYYYMMDD.dat
    if (file_name.size() != 12 || file_name.compare(8, 4, L".dat") != 0)
        return false;
    for (int i = 0; i < 8; i++)
    {
        if (file_name[i] < L'0' || file_name[i] > L'9')
            return false;
    }

    year = (file_name[0] - L'0') * 1000 + (file_name[1] - L'0') * 100 + (file_name[2] - L'0') * 10 + (file_name[3] - L'0');
    month = (file_name[4] - L'0') * 10 + (file_name[5] - L'0');
    day = (file_name[6] - L'0') * 10 + (file_name[7] - L'0');
    return true;
}
