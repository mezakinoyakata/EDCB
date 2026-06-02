#include "stdafx.h"
#include "EpgSqliteExporter.h"
#include "../../Common/StringUtil.h"
#include <mysql.h>
#include <fstream>

namespace {

inline std::string W2U8(const wstring& w)
{
    string s;
    WtoUTF8(w, s);
    return s;
}

std::string SystemTimeToStr(const SYSTEMTIME& st)
{
    char buf[32];
    snprintf(buf, sizeof(buf), "%04d-%02d-%02dT%02d:%02d:%02d",
             st.wYear, st.wMonth, st.wDay, st.wHour, st.wMinute, st.wSecond);
    return buf;
}

// MySQL 接続設定（EpgMysqlConn.ini から読み込む）
struct ConnInfo {
    std::string host     = "localhost";
    int         port     = 3306;
    std::string database = "edcbviewer";
    std::string user;
    std::string password;
};

bool LoadConnInfo(const wchar_t* iniPath, ConnInfo& out)
{
    std::string pathU8;
    WtoUTF8(wstring(iniPath), pathU8);
    std::ifstream f(pathU8);
    if (!f) return false;

    std::string line;
    while (std::getline(f, line)) {
        auto eq = line.find('=');
        if (eq == std::string::npos) continue;
        std::string key = line.substr(0, eq);
        std::string val = line.substr(eq + 1);
        if      (key == "host")     out.host     = val;
        else if (key == "port")     out.port     = std::stoi(val);
        else if (key == "database") out.database = val;
        else if (key == "user")     out.user     = val;
        else if (key == "password") out.password = val;
    }
    return !out.user.empty();
}

// 文字列を MySQL のエスケープ付きシングルクォートで囲む
std::string Q(MYSQL* db, const std::string& s)
{
    std::string buf(s.size() * 2 + 1, '\0');
    unsigned long len = mysql_real_escape_string(db, &buf[0], s.c_str(), (unsigned long)s.size());
    buf.resize(len);
    return "'" + buf + "'";
}

// NULL または整数値
std::string N(bool hasValue, int val)
{
    return hasValue ? std::to_string(val) : "NULL";
}

// NULL または文字列
std::string NQ(MYSQL* db, bool hasValue, const std::string& val)
{
    return hasValue ? Q(db, val) : "NULL";
}

void Exec(MYSQL* db, const std::string& sql)
{
    if (mysql_query(db, sql.c_str()) != 0)
        AddDebugLogFormat(L"EpgMysql query error: %S\n%S", mysql_error(db), sql.substr(0, 120).c_str());
}

// CREATE TABLE が存在しない場合のみ作成。
// インデックスは TABLE 定義内の KEY 句として作成するため IF NOT EXISTS 不要。
static const char* DDL_SERVICES = R"sql(
CREATE TABLE IF NOT EXISTS services (
    onid               INT NOT NULL,
    tsid               INT NOT NULL,
    sid                INT NOT NULL,
    service_type       INT NOT NULL DEFAULT 0,
    partial_reception  INT NOT NULL DEFAULT 0,
    provider_name      TEXT NOT NULL DEFAULT '',
    service_name       TEXT NOT NULL DEFAULT '',
    network_name       TEXT NOT NULL DEFAULT '',
    ts_name            TEXT NOT NULL DEFAULT '',
    remote_control_key INT NOT NULL DEFAULT 0,
    updated_at         VARCHAR(30) NOT NULL DEFAULT '',
    PRIMARY KEY (onid, tsid, sid)
) ENGINE=InnoDB CHARACTER SET utf8mb4
)sql";

static const char* DDL_EVENTS = R"sql(
CREATE TABLE IF NOT EXISTS events (
    onid                     INT NOT NULL,
    tsid                     INT NOT NULL,
    sid                      INT NOT NULL,
    event_id                 INT NOT NULL,
    start_time               VARCHAR(30),
    duration_sec             INT,
    event_name               TEXT NOT NULL DEFAULT '',
    short_text               TEXT NOT NULL DEFAULT '',
    ext_text                 MEDIUMTEXT NOT NULL DEFAULT '',
    component_stream_content INT,
    component_type           INT,
    component_tag            INT,
    component_text           TEXT NOT NULL DEFAULT '',
    free_ca_flag             INT NOT NULL DEFAULT 0,
    updated_at               VARCHAR(30) NOT NULL DEFAULT '',
    PRIMARY KEY (onid, tsid, sid, event_id),
    KEY idx_start   (start_time),
    KEY idx_service (onid, tsid, sid)
) ENGINE=InnoDB CHARACTER SET utf8mb4
)sql";

static const char* DDL_GENRES = R"sql(
CREATE TABLE IF NOT EXISTS event_genres (
    onid          INT NOT NULL,
    tsid          INT NOT NULL,
    sid           INT NOT NULL,
    event_id      INT NOT NULL,
    seq           INT NOT NULL,
    nibble_l1     INT NOT NULL,
    nibble_l2     INT NOT NULL,
    user_nibble_1 INT NOT NULL,
    user_nibble_2 INT NOT NULL,
    PRIMARY KEY (onid, tsid, sid, event_id, seq)
) ENGINE=InnoDB CHARACTER SET utf8mb4
)sql";

static const char* DDL_AUDIO = R"sql(
CREATE TABLE IF NOT EXISTS event_audio (
    onid                INT NOT NULL,
    tsid                INT NOT NULL,
    sid                 INT NOT NULL,
    event_id            INT NOT NULL,
    component_tag       INT NOT NULL,
    stream_content      INT NOT NULL DEFAULT 0,
    component_type      INT NOT NULL DEFAULT 0,
    stream_type         INT NOT NULL DEFAULT 0,
    simulcast_group_tag INT NOT NULL DEFAULT 0,
    multi_lingual       INT NOT NULL DEFAULT 0,
    main_component      INT NOT NULL DEFAULT 0,
    quality_indicator   INT NOT NULL DEFAULT 0,
    sampling_rate       INT NOT NULL DEFAULT 0,
    text_char           TEXT NOT NULL DEFAULT '',
    PRIMARY KEY (onid, tsid, sid, event_id, component_tag)
) ENGINE=InnoDB CHARACTER SET utf8mb4
)sql";

static const char* DDL_GROUPS = R"sql(
CREATE TABLE IF NOT EXISTS event_groups (
    onid         INT NOT NULL,
    tsid         INT NOT NULL,
    sid          INT NOT NULL,
    event_id     INT NOT NULL,
    group_type   INT NOT NULL,
    seq          INT NOT NULL,
    ref_onid     INT NOT NULL,
    ref_tsid     INT NOT NULL,
    ref_sid      INT NOT NULL,
    ref_event_id INT NOT NULL,
    PRIMARY KEY (onid, tsid, sid, event_id, group_type, seq)
) ENGINE=InnoDB CHARACTER SET utf8mb4
)sql";

} // namespace

void ExportEpgToMysql(const wchar_t* configPath, const std::map<LONGLONG, EPGDB_SERVICE_EVENT_INFO>& epgMap)
{
    ConnInfo ci;
    if (!LoadConnInfo(configPath, ci)) {
        AddDebugLogFormat(L"EpgMysql: config not found or invalid, skipping: %ls", configPath);
        return;
    }

    MYSQL* db = mysql_init(nullptr);
    if (!mysql_real_connect(db, ci.host.c_str(), ci.user.c_str(), ci.password.c_str(),
                            ci.database.c_str(), ci.port, nullptr, 0)) {
        AddDebugLogFormat(L"EpgMysql: connect failed: %S", mysql_error(db));
        mysql_close(db);
        return;
    }

    mysql_set_character_set(db, "utf8mb4");

    Exec(db, DDL_SERVICES);
    Exec(db, DDL_EVENTS);
    Exec(db, DDL_GENRES);
    Exec(db, DDL_AUDIO);
    Exec(db, DDL_GROUPS);

    Exec(db, "START TRANSACTION");

    std::string nowStr;
    {
        SYSTEMTIME now;
        GetLocalTime(&now);
        nowStr = SystemTimeToStr(now);
    }

    int svcCount = 0, evtCount = 0;

    for (const auto& kv : epgMap) {
        const EPGDB_SERVICE_INFO& svc = kv.second.serviceInfo;
        ++svcCount;
        evtCount += (int)kv.second.eventList.size();

        std::string sql =
            "REPLACE INTO services(onid,tsid,sid,service_type,partial_reception,"
            "provider_name,service_name,network_name,ts_name,remote_control_key,updated_at) VALUES("
            + std::to_string(svc.ONID) + ","
            + std::to_string(svc.TSID) + ","
            + std::to_string(svc.SID)  + ","
            + std::to_string(svc.service_type) + ","
            + std::to_string(svc.partialReceptionFlag) + ","
            + Q(db, W2U8(svc.service_provider_name)) + ","
            + Q(db, W2U8(svc.service_name))          + ","
            + Q(db, W2U8(svc.network_name))           + ","
            + Q(db, W2U8(svc.ts_name))                + ","
            + std::to_string(svc.remote_control_key_id) + ","
            + Q(db, nowStr) + ")";
        Exec(db, sql);

        for (const EPGDB_EVENT_INFO& evt : kv.second.eventList) {
            sql =
                "REPLACE INTO events(onid,tsid,sid,event_id,start_time,duration_sec,"
                "event_name,short_text,ext_text,"
                "component_stream_content,component_type,component_tag,component_text,"
                "free_ca_flag,updated_at) VALUES("
                + std::to_string(evt.original_network_id)  + ","
                + std::to_string(evt.transport_stream_id)  + ","
                + std::to_string(evt.service_id)           + ","
                + std::to_string(evt.event_id)             + ","
                + NQ(db, evt.StartTimeFlag != 0, SystemTimeToStr(evt.start_time)) + ","
                + N(evt.DurationFlag != 0, evt.durationSec) + ","
                + Q(db, evt.hasShortInfo ? W2U8(evt.shortInfo.event_name) : "") + ","
                + Q(db, evt.hasShortInfo ? W2U8(evt.shortInfo.text_char)  : "") + ","
                + Q(db, evt.hasExtInfo   ? W2U8(evt.extInfo.text_char)    : "") + ","
                + N(evt.hasComponentInfo, evt.componentInfo.stream_content)  + ","
                + N(evt.hasComponentInfo, evt.componentInfo.component_type)  + ","
                + N(evt.hasComponentInfo, evt.componentInfo.component_tag)   + ","
                + Q(db, evt.hasComponentInfo ? W2U8(evt.componentInfo.text_char) : "") + ","
                + std::to_string(evt.freeCAFlag) + ","
                + Q(db, nowStr) + ")";
            Exec(db, sql);

            if (evt.hasContentInfo) {
                int seq = 0;
                for (const auto& g : evt.contentInfo.nibbleList) {
                    sql =
                        "REPLACE INTO event_genres(onid,tsid,sid,event_id,seq,"
                        "nibble_l1,nibble_l2,user_nibble_1,user_nibble_2) VALUES("
                        + std::to_string(evt.original_network_id) + ","
                        + std::to_string(evt.transport_stream_id) + ","
                        + std::to_string(evt.service_id)          + ","
                        + std::to_string(evt.event_id)            + ","
                        + std::to_string(seq++)                   + ","
                        + std::to_string(g.content_nibble_level_1) + ","
                        + std::to_string(g.content_nibble_level_2) + ","
                        + std::to_string(g.user_nibble_1)           + ","
                        + std::to_string(g.user_nibble_2)           + ")";
                    Exec(db, sql);
                }
            }

            if (evt.hasAudioInfo) {
                for (const auto& a : evt.audioInfo.componentList) {
                    sql =
                        "REPLACE INTO event_audio(onid,tsid,sid,event_id,component_tag,"
                        "stream_content,component_type,stream_type,simulcast_group_tag,"
                        "multi_lingual,main_component,quality_indicator,sampling_rate,text_char) VALUES("
                        + std::to_string(evt.original_network_id) + ","
                        + std::to_string(evt.transport_stream_id) + ","
                        + std::to_string(evt.service_id)          + ","
                        + std::to_string(evt.event_id)            + ","
                        + std::to_string(a.component_tag)         + ","
                        + std::to_string(a.stream_content)        + ","
                        + std::to_string(a.component_type)        + ","
                        + std::to_string(a.stream_type)           + ","
                        + std::to_string(a.simulcast_group_tag)   + ","
                        + std::to_string(a.ES_multi_lingual_flag) + ","
                        + std::to_string(a.main_component_flag)   + ","
                        + std::to_string(a.quality_indicator)     + ","
                        + std::to_string(a.sampling_rate)         + ","
                        + Q(db, W2U8(a.text_char)) + ")";
                    Exec(db, sql);
                }
            }

            auto writeGroups = [&](const EPGDB_EVENTGROUP_INFO& info, int groupType) {
                int seq = 0;
                for (const auto& r : info.eventDataList) {
                    std::string gsql =
                        "REPLACE INTO event_groups(onid,tsid,sid,event_id,group_type,seq,"
                        "ref_onid,ref_tsid,ref_sid,ref_event_id) VALUES("
                        + std::to_string(evt.original_network_id) + ","
                        + std::to_string(evt.transport_stream_id) + ","
                        + std::to_string(evt.service_id)          + ","
                        + std::to_string(evt.event_id)            + ","
                        + std::to_string(groupType)               + ","
                        + std::to_string(seq++)                   + ","
                        + std::to_string(r.original_network_id)   + ","
                        + std::to_string(r.transport_stream_id)   + ","
                        + std::to_string(r.service_id)            + ","
                        + std::to_string(r.event_id)              + ")";
                    Exec(db, gsql);
                }
            };
            writeGroups(evt.eventGroupInfo, 1);
            writeGroups(evt.eventRelayInfo,  2);
        }
    }

    Exec(db, "COMMIT");
    mysql_close(db);

    AddDebugLogFormat(L"EpgMysql: done svc=%d evt=%d", svcCount, evtCount);
}
