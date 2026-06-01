#include "stdafx.h"
#include "EpgSqliteExporter.h"
#include "../../Common/StringUtil.h"
#include "sqlite3.h"

namespace
{

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

const char* DDL_CREATE_TABLES = R"sql(
CREATE TABLE IF NOT EXISTS services (
    onid               INTEGER NOT NULL,
    tsid               INTEGER NOT NULL,
    sid                INTEGER NOT NULL,
    service_type       INTEGER NOT NULL DEFAULT 0,
    partial_reception  INTEGER NOT NULL DEFAULT 0,
    provider_name      TEXT NOT NULL DEFAULT '',
    service_name       TEXT NOT NULL DEFAULT '',
    network_name       TEXT NOT NULL DEFAULT '',
    ts_name            TEXT NOT NULL DEFAULT '',
    remote_control_key INTEGER NOT NULL DEFAULT 0,
    updated_at         TEXT NOT NULL DEFAULT '',
    PRIMARY KEY (onid, tsid, sid)
);
CREATE TABLE IF NOT EXISTS events (
    onid                     INTEGER NOT NULL,
    tsid                     INTEGER NOT NULL,
    sid                      INTEGER NOT NULL,
    event_id                 INTEGER NOT NULL,
    start_time               TEXT,
    duration_sec             INTEGER,
    event_name               TEXT NOT NULL DEFAULT '',
    short_text               TEXT NOT NULL DEFAULT '',
    ext_text                 TEXT NOT NULL DEFAULT '',
    component_stream_content INTEGER,
    component_type           INTEGER,
    component_tag            INTEGER,
    component_text           TEXT NOT NULL DEFAULT '',
    free_ca_flag             INTEGER NOT NULL DEFAULT 0,
    updated_at               TEXT NOT NULL DEFAULT '',
    PRIMARY KEY (onid, tsid, sid, event_id)
);
CREATE TABLE IF NOT EXISTS event_genres (
    onid          INTEGER NOT NULL,
    tsid          INTEGER NOT NULL,
    sid           INTEGER NOT NULL,
    event_id      INTEGER NOT NULL,
    seq           INTEGER NOT NULL,
    nibble_l1     INTEGER NOT NULL,
    nibble_l2     INTEGER NOT NULL,
    user_nibble_1 INTEGER NOT NULL,
    user_nibble_2 INTEGER NOT NULL,
    PRIMARY KEY (onid, tsid, sid, event_id, seq)
);
CREATE TABLE IF NOT EXISTS event_audio (
    onid                INTEGER NOT NULL,
    tsid                INTEGER NOT NULL,
    sid                 INTEGER NOT NULL,
    event_id            INTEGER NOT NULL,
    component_tag       INTEGER NOT NULL,
    stream_content      INTEGER NOT NULL DEFAULT 0,
    component_type      INTEGER NOT NULL DEFAULT 0,
    stream_type         INTEGER NOT NULL DEFAULT 0,
    simulcast_group_tag INTEGER NOT NULL DEFAULT 0,
    multi_lingual       INTEGER NOT NULL DEFAULT 0,
    main_component      INTEGER NOT NULL DEFAULT 0,
    quality_indicator   INTEGER NOT NULL DEFAULT 0,
    sampling_rate       INTEGER NOT NULL DEFAULT 0,
    text_char           TEXT NOT NULL DEFAULT '',
    PRIMARY KEY (onid, tsid, sid, event_id, component_tag)
);
CREATE TABLE IF NOT EXISTS event_groups (
    onid         INTEGER NOT NULL,
    tsid         INTEGER NOT NULL,
    sid          INTEGER NOT NULL,
    event_id     INTEGER NOT NULL,
    group_type   INTEGER NOT NULL,
    seq          INTEGER NOT NULL,
    ref_onid     INTEGER NOT NULL,
    ref_tsid     INTEGER NOT NULL,
    ref_sid      INTEGER NOT NULL,
    ref_event_id INTEGER NOT NULL,
    PRIMARY KEY (onid, tsid, sid, event_id, group_type, seq)
);
)sql";

struct Stmt {
    sqlite3_stmt* s = nullptr;
    ~Stmt() { if (s) sqlite3_finalize(s); }
    bool prepare(sqlite3* db, const char* sql) {
        return sqlite3_prepare_v2(db, sql, -1, &s, nullptr) == SQLITE_OK;
    }
    void reset() { sqlite3_reset(s); }
    void bind_int(int col, sqlite3_int64 v) { sqlite3_bind_int64(s, col, v); }
    void bind_text(int col, const std::string& v) {
        sqlite3_bind_text(s, col, v.c_str(), -1, SQLITE_TRANSIENT);
    }
    void bind_null(int col) { sqlite3_bind_null(s, col); }
    void step() { sqlite3_step(s); }
};

void exec_sql(sqlite3* db, const char* sql)
{
    char* err = nullptr;
    sqlite3_exec(db, sql, nullptr, nullptr, &err);
    if (err) sqlite3_free(err);
}

} // namespace

void ExportEpgToSqlite(const wchar_t* dbPath, const std::map<LONGLONG, EPGDB_SERVICE_EVENT_INFO>& epgMap)
{
    string dbPathU8;
    WtoUTF8(wstring(dbPath), dbPathU8);

    sqlite3* db = nullptr;
    if (sqlite3_open_v2(dbPathU8.c_str(), &db,
                        SQLITE_OPEN_READWRITE | SQLITE_OPEN_CREATE, nullptr) != SQLITE_OK) {
        AddDebugLogFormat(L"EpgSqliteExporter: DB open failed: %ls", dbPath);
        if (db) sqlite3_close(db);
        return;
    }

    exec_sql(db, "PRAGMA journal_mode=WAL;");
    exec_sql(db, "PRAGMA synchronous=NORMAL;");
    exec_sql(db, DDL_CREATE_TABLES);
    exec_sql(db, "BEGIN;");

    Stmt svcStmt, evtStmt, gnrStmt, audStmt, grpStmt;
    svcStmt.prepare(db,
        "INSERT OR REPLACE INTO services(onid,tsid,sid,service_type,partial_reception,provider_name,"
        "service_name,network_name,ts_name,remote_control_key,updated_at)"
        " VALUES(?,?,?,?,?,?,?,?,?,?,?)");
    evtStmt.prepare(db,
        "INSERT OR REPLACE INTO events(onid,tsid,sid,event_id,start_time,duration_sec,event_name,"
        "short_text,ext_text,component_stream_content,component_type,component_tag,"
        "component_text,free_ca_flag,updated_at)"
        " VALUES(?,?,?,?,?,?,?,?,?,?,?,?,?,?,?)");
    gnrStmt.prepare(db,
        "INSERT OR REPLACE INTO event_genres(onid,tsid,sid,event_id,seq,nibble_l1,nibble_l2,user_nibble_1,user_nibble_2)"
        " VALUES(?,?,?,?,?,?,?,?,?)");
    audStmt.prepare(db,
        "INSERT OR REPLACE INTO event_audio(onid,tsid,sid,event_id,component_tag,stream_content,component_type,"
        "stream_type,simulcast_group_tag,multi_lingual,main_component,quality_indicator,sampling_rate,text_char)"
        " VALUES(?,?,?,?,?,?,?,?,?,?,?,?,?,?)");
    grpStmt.prepare(db,
        "INSERT OR REPLACE INTO event_groups(onid,tsid,sid,event_id,group_type,seq,ref_onid,ref_tsid,ref_sid,ref_event_id)"
        " VALUES(?,?,?,?,?,?,?,?,?,?)");

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

        svcStmt.bind_int (1, svc.ONID);
        svcStmt.bind_int (2, svc.TSID);
        svcStmt.bind_int (3, svc.SID);
        svcStmt.bind_int (4, svc.service_type);
        svcStmt.bind_int (5, svc.partialReceptionFlag);
        svcStmt.bind_text(6, W2U8(svc.service_provider_name));
        svcStmt.bind_text(7, W2U8(svc.service_name));
        svcStmt.bind_text(8, W2U8(svc.network_name));
        svcStmt.bind_text(9, W2U8(svc.ts_name));
        svcStmt.bind_int (10, svc.remote_control_key_id);
        svcStmt.bind_text(11, nowStr);
        svcStmt.step();
        svcStmt.reset();

        for (const EPGDB_EVENT_INFO& evt : kv.second.eventList) {
            evtStmt.bind_int (1, evt.original_network_id);
            evtStmt.bind_int (2, evt.transport_stream_id);
            evtStmt.bind_int (3, evt.service_id);
            evtStmt.bind_int (4, evt.event_id);
            if (evt.StartTimeFlag)
                evtStmt.bind_text(5, SystemTimeToStr(evt.start_time));
            else
                evtStmt.bind_null(5);
            if (evt.DurationFlag)
                evtStmt.bind_int (6, evt.durationSec);
            else
                evtStmt.bind_null(6);
            evtStmt.bind_text(7,  evt.hasShortInfo ? W2U8(evt.shortInfo.event_name) : "");
            evtStmt.bind_text(8,  evt.hasShortInfo ? W2U8(evt.shortInfo.text_char)  : "");
            evtStmt.bind_text(9,  evt.hasExtInfo   ? W2U8(evt.extInfo.text_char)    : "");
            if (evt.hasComponentInfo) {
                evtStmt.bind_int (10, evt.componentInfo.stream_content);
                evtStmt.bind_int (11, evt.componentInfo.component_type);
                evtStmt.bind_int (12, evt.componentInfo.component_tag);
                evtStmt.bind_text(13, W2U8(evt.componentInfo.text_char));
            } else {
                evtStmt.bind_null(10);
                evtStmt.bind_null(11);
                evtStmt.bind_null(12);
                evtStmt.bind_text(13, "");
            }
            evtStmt.bind_int (14, evt.freeCAFlag);
            evtStmt.bind_text(15, nowStr);
            evtStmt.step();
            evtStmt.reset();

            if (evt.hasContentInfo) {
                int seq = 0;
                for (const auto& g : evt.contentInfo.nibbleList) {
                    gnrStmt.bind_int(1, evt.original_network_id);
                    gnrStmt.bind_int(2, evt.transport_stream_id);
                    gnrStmt.bind_int(3, evt.service_id);
                    gnrStmt.bind_int(4, evt.event_id);
                    gnrStmt.bind_int(5, seq++);
                    gnrStmt.bind_int(6, g.content_nibble_level_1);
                    gnrStmt.bind_int(7, g.content_nibble_level_2);
                    gnrStmt.bind_int(8, g.user_nibble_1);
                    gnrStmt.bind_int(9, g.user_nibble_2);
                    gnrStmt.step();
                    gnrStmt.reset();
                }
            }

            if (evt.hasAudioInfo) {
                for (const auto& a : evt.audioInfo.componentList) {
                    audStmt.bind_int (1, evt.original_network_id);
                    audStmt.bind_int (2, evt.transport_stream_id);
                    audStmt.bind_int (3, evt.service_id);
                    audStmt.bind_int (4, evt.event_id);
                    audStmt.bind_int (5, a.component_tag);
                    audStmt.bind_int (6, a.stream_content);
                    audStmt.bind_int (7, a.component_type);
                    audStmt.bind_int (8, a.stream_type);
                    audStmt.bind_int (9, a.simulcast_group_tag);
                    audStmt.bind_int (10, a.ES_multi_lingual_flag);
                    audStmt.bind_int (11, a.main_component_flag);
                    audStmt.bind_int (12, a.quality_indicator);
                    audStmt.bind_int (13, a.sampling_rate);
                    audStmt.bind_text(14, W2U8(a.text_char));
                    audStmt.step();
                    audStmt.reset();
                }
            }

            auto writeGroup = [&](const EPGDB_EVENTGROUP_INFO& info, int groupType) {
                int seq = 0;
                for (const auto& r : info.eventDataList) {
                    grpStmt.bind_int(1, evt.original_network_id);
                    grpStmt.bind_int(2, evt.transport_stream_id);
                    grpStmt.bind_int(3, evt.service_id);
                    grpStmt.bind_int(4, evt.event_id);
                    grpStmt.bind_int(5, groupType);
                    grpStmt.bind_int(6, seq++);
                    grpStmt.bind_int(7, r.original_network_id);
                    grpStmt.bind_int(8, r.transport_stream_id);
                    grpStmt.bind_int(9, r.service_id);
                    grpStmt.bind_int(10, r.event_id);
                    grpStmt.step();
                    grpStmt.reset();
                }
            };
            writeGroup(evt.eventGroupInfo, 1);
            writeGroup(evt.eventRelayInfo,  2);
        }
    }

    exec_sql(db, "COMMIT;");
    sqlite3_close(db);

    AddDebugLogFormat(L"EpgSqliteExporter: done svc=%d evt=%d path=%ls", svcCount, evtCount, dbPath);
}
