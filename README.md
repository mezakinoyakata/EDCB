EpgDataCap_Bon
==============
**BonDriver based multifunctional EPG software**

Documents are stored in the 'Document' directory.  
Configuration files are stored in the 'ini' directory.

---

## このforkについて

[xtne6f/EDCB](https://github.com/xtne6f/EDCB) からforkし、EPGデータをMySQLデータベースへ書き出す機能を追加しています。

### 追加機能（`for-deploy` ブランチ）

- `EpgSqliteExporter`: EpgTimerSrvがEPGデータをロードするたびにMySQLへUPSERTで蓄積書き出し
  - 接続設定: `{SettingPath}\EpgMysqlConn.ini`
  - 実行時依存: `libmysql.dll`（MySQL Connector/C）を EpgTimerSrv.exe と同じフォルダに配置
- [EDCBViewer](https://github.com/mezakinoyakata/EDCBViewer) と連携して録画済み一覧・番組情報の参照に使用
- 仕様書: `EpgSqliteExporter_Spec.md`
