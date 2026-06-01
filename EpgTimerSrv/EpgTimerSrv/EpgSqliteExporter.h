#pragma once
#include "../../Common/StructDef.h"
#include <map>

// EPGデータ(nextMap)をSQLiteに書き出す。
// dbPath: 書き出し先DBファイルのフルパス（ワイド文字列）
// epgMap: LoadThread で構築した nextMap
void ExportEpgToSqlite(const wchar_t* dbPath, const std::map<LONGLONG, EPGDB_SERVICE_EVENT_INFO>& epgMap);
