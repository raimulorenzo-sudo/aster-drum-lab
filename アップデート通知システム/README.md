# ASTER Drum Lab アップデート通知システム

GitHub にはインストーラーを置かず、最新バージョン番号だけを公開します。
インストーラーの配布は BOOTH で行います。

プラグインの画面を開くと2秒後に自動確認し、新版がある場合だけ通知を表示します。
確認結果は24時間保存し、複数のプラグインインスタンスから同じ通知が連続して出ないようにしています。
通信に失敗した場合は起動時にエラーを表示せず、プラグインの動作を妨げません。

## 新しいバージョンを配布する手順

1. BOOTH の商品ファイルを新しいインストーラーに更新する。
2. `CMakeLists.txt` の `project(AsterDrumLab VERSION ...)` を新しいバージョンに変更する。
3. `ui-prototype/package.json` の `version` も同じ番号に変更してプラグインをビルドする。
4. BOOTH で最新版を配布した後、この `latest-version.txt` を同じ番号に変更する。
5. GitHub の公開リポジトリ `raimulorenzo-sudo/aster-drum-lab-update` に `latest-version.txt` を反映する。

`latest-version.txt` には、次のようにバージョン番号だけを書きます。

```text
1.0.1
```

BOOTH の更新前にこの番号を上げると、まだダウンロードできない更新をユーザーに通知してしまうため、必ず BOOTH の更新を先に行います。
