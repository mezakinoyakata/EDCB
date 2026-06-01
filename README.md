EpgDataCap_Bon
==============
**BonDriver based multifunctional EPG software**

Documents are stored in the 'Document' directory.  
Configuration files are stored in the 'ini' directory.

---

## このforkについて

[xtne6f/EDCB](https://github.com/xtne6f/EDCB) からforkし、EPGデータをSQLiteデータベースに書き出す機能を追加することを目指しています。

### 追加機能（`for-deploy` ブランチ）

- `EpgSqliteExporter`: EpgTimerSrvがEPGデータをロードするたびに `{SettingPath}\EpgData.db` へUPSERTで蓄積書き出し
- SQLite 3.53.1 アマルガメーション同梱
- 仕様書: `EpgSqliteExporter_Spec.md`
