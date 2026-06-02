#pragma once
#include "../../Common/StructDef.h"
#include <map>

// EPGデータを MySQL に書き出す。
// configPath: EpgMysqlConn.ini のフルパス（ワイド文字列、ファイルが無ければスキップ）
// epgMap: LoadThread で構築した nextMap
void ExportEpgToMysql(const wchar_t* configPath, const std::map<LONGLONG, EPGDB_SERVICE_EVENT_INFO>& epgMap);
