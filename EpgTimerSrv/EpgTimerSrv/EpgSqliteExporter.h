#pragma once
#include "../../Common/StructDef.h"
#include <map>
#include <unordered_map>

// EPGデータを MySQL に書き出す。
// configPath: EpgMysqlConn.ini のフルパス（ワイド文字列、ファイルが無ければスキップ）
// epgMap: LoadThread で構築した nextMap
// reserveStatusMap: (onid<<48|tsid<<32|sid<<16|event_id) → reserve_status (1=予約あり, 2=録画終了)
void ExportEpgToMysql(const wchar_t* configPath,
                      const std::map<LONGLONG, EPGDB_SERVICE_EVENT_INFO>& epgMap,
                      const std::unordered_map<LONGLONG, int>& reserveStatusMap);
