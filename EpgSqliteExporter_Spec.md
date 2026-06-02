# EDCB EPG MySQL書き出し仕様書

## 概要

EpgTimerSrv がEPGデータをロードするたびに、MySQLデータベースへ自動的に書き出しを行う機能。

---

## 追加・変更ファイル

| ファイル | 場所 | 種別 |
|---|---|---|
| `EpgSqliteExporter.h` | `EpgTimerSrv/EpgTimerSrv/` | 新規：エクスポーター宣言 |
| `EpgSqliteExporter.cpp` | `EpgTimerSrv/EpgTimerSrv/` | 新規：エクスポーター実装（MySQL） |
| `EpgDBManager.cpp` | `EpgTimerSrv/EpgTimerSrv/` | 変更：呼び出し追加 |
| `EpgTimerSrv.vcxproj` | `EpgTimerSrv/EpgTimerSrv/` | 変更：新ファイルをプロジェクトに追加 |

> `sqlite3.h` / `sqlite3.c` はプロジェクトに含まれているが、現在の実装では使用していない。

---

## 呼び出しタイミング

`CEpgDBManager::LoadThread()` 内、`epgUtil.UnInitialize()` の直後。  
EPGデータが `nextMap`（`map<LONGLONG, EPGDB_SERVICE_EVENT_INFO>`）に揃った段階で実行される。  
ロードがキャンセルされた場合（`loadStop == true`）はスキップする。

---

## 接続設定ファイル

```
{SettingPath}\EpgMysqlConn.ini
```

`SettingPath` は `GetSettingPath()` が返すEDCB設定フォルダ（通常は EpgTimerSrv.exe と同じフォルダ）。  
ファイルが存在しない、または `user` が空の場合は書き出しをスキップする（エラーにしない）。

### INIファイル形式

BOM なし UTF-8 または ANSI で保存すること（BOM付き UTF-8 は先頭キーが読み取れないため不可）。

```ini
host=5600X
port=3306
database=edcbviewer
user=edcb
password=（パスワード）
```

| キー | デフォルト | 説明 |
|---|---|---|
| host | localhost | MySQLサーバーのホスト名またはIPアドレス |
| port | 3306 | ポート番号 |
| database | edcbviewer | データベース名 |
| user | （空） | ユーザー名（必須：空の場合スキップ） |
| password | （空） | パスワード |

---

## 書き込み方式

**蓄積（UPSERT）** — `REPLACE INTO`

- 主キーが一致するレコードは上書き更新される
- 過去に取得した番組情報（現在のEPGに含まれないもの）はDBに残り続ける
- 番組名・時間等の変更があった場合は同一主キーで上書きされる
- 1ロードにつき1トランザクションでコミット

---

## DBスキーマ

文字セット: `utf8mb4`、ストレージエンジン: `InnoDB`

### services テーブル

| カラム | 型 | 説明 |
|---|---|---|
| onid | INT | original_network_id（PK） |
| tsid | INT | transport_stream_id（PK） |
| sid | INT | service_id（PK） |
| service_type | INT | サービス種別 |
| partial_reception | INT | 部分受信フラグ |
| provider_name | TEXT | プロバイダ名 |
| service_name | TEXT | サービス名 |
| network_name | TEXT | ネットワーク名 |
| ts_name | TEXT | TS名 |
| remote_control_key | INT | リモコンキーID |
| updated_at | VARCHAR(30) | 書き出し日時（ISO-8601） |

### events テーブル

| カラム | 型 | 説明 |
|---|---|---|
| onid | INT | PK |
| tsid | INT | PK |
| sid | INT | PK |
| event_id | INT | PK |
| start_time | VARCHAR(30) | 開始時刻（ISO-8601）、NULL可 |
| duration_sec | INT | 長さ（秒）、NULL可 |
| event_name | TEXT | 番組名 |
| short_text | TEXT | 短い説明 |
| ext_text | MEDIUMTEXT | 詳細説明 |
| component_stream_content | INT | 映像stream_content、NULL可 |
| component_type | INT | 映像component_type、NULL可 |
| component_tag | INT | 映像component_tag、NULL可 |
| component_text | TEXT | 映像説明文 |
| free_ca_flag | INT | ノンスクランブルフラグ |
| updated_at | VARCHAR(30) | 書き出し日時（ISO-8601） |
| year_week | INT | 開始日の年週（YYYYWW形式、例:202622）NULL不可 DEFAULT 0 |
| reserve_status | TINYINT | 予約・録画状態（下表） NULL不可 DEFAULT 0 |

**reserve_status 値の定義：**

| 値 | 意味 |
|---|---|
| 0 | 未来・未予約 |
| 1 | 未来・予約あり |
| 2 | 録画終了 |
| 3 | 未録画で終了 |

インデックス: `idx_start (start_time)`、`idx_service (onid, tsid, sid)`、`idx_status (year_week, reserve_status)`

### event_genres テーブル

PK: (onid, tsid, sid, event_id, seq)

| カラム | 型 | 説明 |
|---|---|---|
| seq | INT | ジャンル順序 |
| nibble_l1 | INT | content_nibble_level_1 |
| nibble_l2 | INT | content_nibble_level_2 |
| user_nibble_1 | INT | user_nibble_1 |
| user_nibble_2 | INT | user_nibble_2 |

### event_audio テーブル

PK: (onid, tsid, sid, event_id, component_tag)

| カラム | 型 | 説明 |
|---|---|---|
| component_tag | INT | PK |
| stream_content | INT | |
| component_type | INT | |
| stream_type | INT | |
| simulcast_group_tag | INT | |
| multi_lingual | INT | ES_multi_lingual_flag |
| main_component | INT | main_component_flag |
| quality_indicator | INT | |
| sampling_rate | INT | |
| text_char | TEXT | 音声説明文 |

### event_groups テーブル

PK: (onid, tsid, sid, event_id, group_type, seq)

| カラム | 型 | 説明 |
|---|---|---|
| group_type | INT | 1=eventGroupInfo, 2=eventRelayInfo |
| seq | INT | 順序 |
| ref_onid | INT | 参照先ONID |
| ref_tsid | INT | 参照先TSID |
| ref_sid | INT | 参照先SID |
| ref_event_id | INT | 参照先EventID |

---

## 実行時依存

- `libmysql.dll`（MySQL Connector/C）を EpgTimerSrv.exe と同じフォルダに配置する

---

## 既存の EDCBEpgImporter との関係

`C:\work\CC\EDCBEpgImporter` は別途作成したC#スタンドアロンツール。  
`*_epg.dat`（TSパケット形式）を直接読んでSQLiteに書き出す外部ツール。  
本修正はEDCB本体（C++）に同機能をMySQL版として組み込んだもの。  
EDCBEpgImporterのスキーマはSQLite向けのため、カラム型定義が異なる点に注意。
