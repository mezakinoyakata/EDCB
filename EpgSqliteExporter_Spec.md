# EDCB EPG SQLite書き出し仕様書

## 概要

EpgTimerSrv がEPGデータをロードするたびに、SQLiteデータベースへ自動的に蓄積書き出しを行う機能。

---

## 追加・変更ファイル

| ファイル | 場所 | 種別 |
|---|---|---|
| `sqlite3.h` / `sqlite3.c` | `EpgTimerSrv/EpgTimerSrv/` | SQLite 3.53.1 アマルガメーション |
| `EpgSqliteExporter.h` | `EpgTimerSrv/EpgTimerSrv/` | 新規：エクスポーター宣言 |
| `EpgSqliteExporter.cpp` | `EpgTimerSrv/EpgTimerSrv/` | 新規：エクスポーター実装 |
| `EpgDBManager.cpp` | `EpgTimerSrv/EpgTimerSrv/` | 変更：呼び出し追加 |
| `EpgTimerSrv.vcxproj` | `EpgTimerSrv/EpgTimerSrv/` | 変更：新ファイルをプロジェクトに追加 |

---

## 呼び出しタイミング

`CEpgDBManager::LoadThread()` 内、`epgUtil.UnInitialize()` の直後。  
EPGデータが `nextMap`（`map<LONGLONG, EPGDB_SERVICE_EVENT_INFO>`）に揃った段階で実行される。  
ロードがキャンセルされた場合（`loadStop == true`）はスキップする。

---

## DB出力先

```
{SettingPath}\EpgData.db
```

`SettingPath` は `GetSettingPath()` が返すEDCB設定フォルダ（通常は EpgTimerSrv.exe と同じフォルダ）。

---

## 書き込み方式

**蓄積（UPSERT）** — `INSERT OR REPLACE INTO`

- 主キーが一致するレコードは上書き更新される
- 過去に取得した番組情報（現在のEPGに含まれないもの）はDBに残り続ける
- 番組名・時間等の変更があった場合は同一主キーで上書きされる

---

## DBスキーマ

### services テーブル

| カラム | 型 | 説明 |
|---|---|---|
| onid | INTEGER | original_network_id（PK） |
| tsid | INTEGER | transport_stream_id（PK） |
| sid | INTEGER | service_id（PK） |
| service_type | INTEGER | サービス種別 |
| partial_reception | INTEGER | 部分受信フラグ |
| provider_name | TEXT | プロバイダ名 |
| service_name | TEXT | サービス名 |
| network_name | TEXT | ネットワーク名 |
| ts_name | TEXT | TS名 |
| remote_control_key | INTEGER | リモコンキーID |
| updated_at | TEXT | 書き出し日時（ISO-8601） |

### events テーブル

| カラム | 型 | 説明 |
|---|---|---|
| onid | INTEGER | PK |
| tsid | INTEGER | PK |
| sid | INTEGER | PK |
| event_id | INTEGER | PK |
| start_time | TEXT | 開始時刻（ISO-8601）、NULL可 |
| duration_sec | INTEGER | 長さ（秒）、NULL可 |
| event_name | TEXT | 番組名 |
| short_text | TEXT | 短い説明 |
| ext_text | TEXT | 詳細説明 |
| component_stream_content | INTEGER | 映像stream_content、NULL可 |
| component_type | INTEGER | 映像component_type、NULL可 |
| component_tag | INTEGER | 映像component_tag、NULL可 |
| component_text | TEXT | 映像説明文 |
| free_ca_flag | INTEGER | ノンスクランブルフラグ |
| updated_at | TEXT | 書き出し日時（ISO-8601） |

### event_genres テーブル

PK: (onid, tsid, sid, event_id, seq)

| カラム | 型 | 説明 |
|---|---|---|
| seq | INTEGER | ジャンル順序 |
| nibble_l1 | INTEGER | content_nibble_level_1 |
| nibble_l2 | INTEGER | content_nibble_level_2 |
| user_nibble_1 | INTEGER | user_nibble_1 |
| user_nibble_2 | INTEGER | user_nibble_2 |

### event_audio テーブル

PK: (onid, tsid, sid, event_id, component_tag)

| カラム | 型 | 説明 |
|---|---|---|
| component_tag | INTEGER | PK |
| stream_content | INTEGER | |
| component_type | INTEGER | |
| stream_type | INTEGER | |
| simulcast_group_tag | INTEGER | |
| multi_lingual | INTEGER | ES_multi_lingual_flag |
| main_component | INTEGER | main_component_flag |
| quality_indicator | INTEGER | |
| sampling_rate | INTEGER | |
| text_char | TEXT | 音声説明文 |

### event_groups テーブル

PK: (onid, tsid, sid, event_id, group_type, seq)

| カラム | 型 | 説明 |
|---|---|---|
| group_type | INTEGER | 1=eventGroupInfo, 2=eventRelayInfo |
| seq | INTEGER | 順序 |
| ref_onid | INTEGER | 参照先ONID |
| ref_tsid | INTEGER | 参照先TSID |
| ref_sid | INTEGER | 参照先SID |
| ref_event_id | INTEGER | 参照先EventID |

---

## SQLite設定

- journal_mode = WAL
- synchronous = NORMAL
- ロード完了後に1トランザクションでまとめてコミット

---

## 既存の EDCBEpgImporter との関係

`C:\work\CC\EDCBEpgImporter` は別途作成したC#スタンドアロンツール。  
`*_epg.dat`（TSパケット形式）を直接読んでSQLiteに書き出す外部ツール。  
本修正はEDCB本体（C++）に同機能を組み込んだもので、スキーマは共通。
