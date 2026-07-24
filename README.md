# SlimeVR ESP Tracker WiFi Dongle ぽてとらカスタムファーム / Potetora Custom Firmware

**2026年7月24日 追記 / Added on July 24, 2026**

### 🇯🇵 日本語 (Japanese)
このファームウェアは、白猫（Xユーザー名：[@NekodaKohaku](https://x.com/NekodaKohaku)）様が公開されている「[SlimeVR-Tracker-ESP-WiFi-Dongle](https://github.com/NekodaKohaku/SlimeVR-Tracker-ESP-WiFi-Dongle)」をベースに、日本国内のBOOTHで販売されているVRトラッカー「[ぽてとら2(強化版)](https://potesuto.booth.pm/items/5945821)」向けにカスタムを行ったハードフォーク版です。

動作確認はぽてとら2(強化版)で行っています。ぽてとら（無印）含むほかのラインナップでは確認していないため動作するかは不明です。
使用した開発ボードはAmazonなどで販売されている「Freenove ESP32-S3 ESP32 S3 Board Lite」になります。

基本的な使い方や環境構築などについては、本家様の説明文（当README下部）をご確認の上、ご自身の環境に合わせて調整してください。

> **謝辞**
> 素晴らしいオープンソースプロジェクトを公開してくださっている白猫様、ならびに各種ライブラリの開発者様方に心より感謝いたします。
> （※本カスタムファームウェアの開発・改修にはAIアシスタント「Gemini3.1Pro」を使用しています）

#### 🛠️ 主なカスタム内容
*   **拡張センサー（AUX）への対応**
    *   センサーIDから拡張センサーのデータを分離・抽出。
    *   PC側（SlimeVR Server）に対して、独立した別の仮想トラッカーとして自動登録させる処理を追加。
    *   メインセンサーと拡張センサーの連動データ（バッテリー、通信状態など）の同期およびID/MACアドレスの偽装処理を実装。

*   ~~通信不安定時のオンライン復帰機能~~
    ~~* トラッカーからデータが正常に送られてきているにもかかわらず、サーバー側で「オフライン」と判定されてしまう症状を改善。~~
    ~~* データの受信が確認できた時点で、強制的にオンライン状態へ復帰させるロジックを追加。~~

*   **通信の安定性向上と誤切断バグの修正**

    * 上記の「強制復帰ロジック」は、切断と復帰を繰り返す現象を誘発する要因となっていたため削除しました。

    * ESP32のマルチコア（非同期処理）によるレースコンディションが原因で、パケット受信時刻と判定時刻が競合して「時間のアンダーフロー（巻き戻り）」が発 生し、超巨大な経過時間として誤検知されて強制切断されるバグを特定・修正しました。

*   **フラッシュメモリ 8MB 開発ボードへの対応**
    *   8MBのフラッシュを搭載したボードで書き込むための設定を追加。詳細は `platformio.ini` 内のコメントアウトをご参照ください。

*   **ドングルとトラッカーのペアリング記憶機能**
    *   一度接続を確立したトラッカーと、PC側に送信する仮想IDの紐付けを記憶。
    *   再起動時や再接続時にトラッカーの割り当て（センサー番号）がズレてしまう問題を防止。

---

### 🇬🇧 English
This firmware is a customized hard fork of the "[SlimeVR-Tracker-ESP-WiFi-Dongle](https://github.com/NekodaKohaku/SlimeVR-Tracker-ESP-WiFi-Dongle)" originally published by Shironeko (X/Twitter: [@NekodaKohaku](https://x.com/NekodaKohaku)). It has been specifically modified for use with the VR tracker "[Potetora 2 (Enhanced Version)](https://potesuto.booth.pm/items/5945821)" sold on BOOTH in Japan.

Operation has been verified specifically with Potetora 2 (Enhanced Version). It has not been tested with other models, including the original Potetora (standard version), so compatibility with them is unknown.

For basic usage and setup instructions, please refer to the original author's description at the bottom of this README and adjust according to your environment.

> **Acknowledgements**
> I would like to express my sincere gratitude to Shironeko for publishing such an excellent open-source project, as well as to the developers of the various libraries used.
> (*Note: The AI assistant "Gemini" was used in the development and modification of this custom firmware.*)

#### 🛠️ Key Customizations (Changelog)
*   **Support for Auxiliary (Extended) Sensors**
    *   Extracts auxiliary sensor data based on the Sensor ID.
    *   Automatically registers the auxiliary sensor as an independent, virtual tracker on the PC side (SlimeVR Server).
    *   Implements separation and spoofing of linked data (battery level, signal strength, MAC address, etc.) for both main and auxiliary sensors.
*   ~~Forced Online Recovery for Unstable Connections~~
    * ~~* Resolves an issue where a tracker is recognized as "Offline" by the server despite successfully sending data.~~
    *  ~~* Added logic to forcefully restore the "Online" state as soon as incoming data is detected.~~
    Improved Connection Stability and Fix for False Disconnection Bug

    The "forced recovery logic" mentioned above was removed as it induced a UI flickering issue (repeated disconnects and reconnects).

    Identified and fixed a bug caused by a race condition due to the ESP32's multi-core (asynchronous) processing. The conflict between the packet reception time and the evaluation time caused a "time underflow," resulting in a massive elapsed time value that triggered false disconnections.

*   **Support for 8MB Flash Development Boards**
    *   Added build configurations for boards equipped with 8MB flash memory. Please refer to the commented-out sections in `platformio.ini` for details.
*   **Dongle-to-Tracker Pairing Memory**
    *   Memorizes the binding between a successfully connected tracker and the virtual ID sent to the PC.
    *   Prevents tracker assignment (sensor numbering) mismatches during reboots or reconnections.
---

（以下、本家様の説明文 / Original description follows below）

[日本語](#日本語) | [English](#english) | [中文](#中文)

---

## 日本語

本プロジェクトは、ESP32-S3 を SlimeVR ESP トラッカー専用の WiFi レシーバーにするファームウェアです。

ESP32-S3 Dongle を PC に接続すると、Dongle が独立した SoftAP ホットスポットを作成し、トラッカーから受信した UDP データを USB HID 経由で PC 側の SlimeVR Server へ転送します。

これにより、家庭用 WiFi の距離、混雑、2.4GHz 干渉などによる遅延や切断の問題を軽減できます。

また、一般的なルーターでは設定できない特殊な設定を使用しているため、通常のルーターと比べてトラッカーの電力を約 10% 節約できます。

🖼️ 実測データ（投稿）: <https://x.com/NekodaKohaku/status/2067842378573254682>

### ⚠️ ハードウェア互換性

現在、このファームウェアは **ESP32-S3 Dongle でのみ完全にテストされています**。

- ✅ **テスト済み機材:** ESP32-S3-WROOM-1

ESP32-S2 でも理論上は動作する可能性がありますが、最大接続数と安定性は未確認です。

### ⚙️ デフォルト WiFi 設定

| 設定項目 | デフォルト値 | 備考 |
| :--- | :--- | :--- |
| **SSID** | `SlimeDongle` | トラッカーが接続する WiFi 名 |
| **パスワード** | `slimedongle12345` | 接続パスワード |
| **UDP ポート** | `6969` | SlimeVR のデフォルトポート |
| **最大接続数** | `10` | ESP32 SoftAP の最大制限 |

### 🛠️ 設定変更

WiFi 名、パスワード、チャンネルなどを変更する場合は、以下のファイルを編集してください。

```text
src/WifiDongleConfig.h
```

主な設定項目:

- SoftAP の SSID / パスワード
- WiFi チャンネル
- トラッカーのタイムアウト
- ハートビート間隔
- 最大接続数

### 🚀 クイックスタート

1. ファームウェアを ESP32-S3 Dongle に書き込みます。
2. Dongle を PC の USB ポートに接続します。
3. PC 上で SlimeVR Server を起動します。
4. SlimeVR トラッカーの WiFi 設定を以下に変更します。

   ```text
   SSID: SlimeDongle
   Password: slimedongle12345
   ```

5. トラッカーが接続されると、SlimeVR Server に表示されます。

> 💡 **備考:**
> パソコン背面の USB ポートへの接続を推奨します。
> パソコンの設置位置が低い場合は、USB 延長ケーブルを使用してより高い位置に設置することをお勧めします。

### 📚 参考

- <https://github.com/SlimeVR/SlimeVR-Tracker-ESP-Receiver>
- <https://github.com/SlimeVR/SlimeVR-Tracker-ESP/tree/espnow>
- <https://github.com/mitzey234/SlimeVR-Tracker-ESP/tree/esp-now>
- <https://github.com/Kirisame-Nanoha/mocopi-slimevr-hid-receiver/tree/main>

---

## English

This project turns an ESP32-S3 into a dedicated WiFi receiver for SlimeVR ESP Trackers.

By plugging the ESP32-S3 Dongle into your PC, the dongle creates its own independent SoftAP hotspot, receives UDP data from Trackers, and forwards the data to the PC-side SlimeVR Server through USB HID.

This can help reduce latency and disconnection issues caused by home WiFi distance, congestion, or 2.4GHz interference.

Because it uses special settings that ordinary routers cannot adjust, it can save about 10% of the tracker's power compared to a regular router.

🖼️ Measurement (post): <https://x.com/NekodaKohaku/status/2067842378573254682>

### ⚠️ Hardware Compatibility

This firmware has currently been fully tested only on the **ESP32-S3 Dongle**.

- ✅ **Tested device:** ESP32-S3-WROOM-1

ESP32-S2 may theoretically work, but the maximum connection count and stability are not yet confirmed.

### ⚙️ Default WiFi Settings

| Setting | Default Value | Note |
| :--- | :--- | :--- |
| **SSID** | `SlimeDongle` | WiFi name Trackers should connect to |
| **Password** | `slimedongle12345` | Connection password |
| **UDP Port** | `6969` | SlimeVR default port |
| **Maximum Connections** | `10` | Maximum limit of ESP32 SoftAP |

### 🛠️ Configuration

To change the WiFi name, password, channel, or other options, edit:

```text
src/WifiDongleConfig.h
```

Main options:

- SoftAP SSID / password
- WiFi channel
- Tracker timeout
- Heartbeat interval
- Maximum connection count

### 🚀 Quick Start

1. Flash this firmware to your ESP32-S3 Dongle.
2. Plug the dongle into a USB port on your PC.
3. Start SlimeVR Server on your PC.
4. Change your SlimeVR Tracker WiFi settings to:

   ```text
   SSID: SlimeDongle
   Password: slimedongle12345
   ```

5. Once connected, the Trackers should appear in SlimeVR Server.

> 💡 **Note:**
> It is recommended to connect the dongle to the USB port on the back of your PC.
> If your PC is placed at a lower level, it is advised to use a USB extension cable to position the dongle at a higher location.

### 📚 References

- <https://github.com/SlimeVR/SlimeVR-Tracker-ESP-Receiver>
- <https://github.com/SlimeVR/SlimeVR-Tracker-ESP/tree/espnow>
- <https://github.com/mitzey234/SlimeVR-Tracker-ESP/tree/esp-now>
- <https://github.com/Kirisame-Nanoha/mocopi-slimevr-hid-receiver/tree/main>

---

## 中文

本專案是將 ESP32-S3 變成 SlimeVR ESP 追蹤器專用的 WiFi 接收器的韌體。

將 ESP32-S3 Dongle 插在電腦上後，Dongle 會建立獨立的 SoftAP 熱點，接收追蹤器傳來的 UDP 資料，並透過 USB HID 將資料轉送到 PC 端的 SlimeVR Server。

這樣可以減少因家用 WiFi 距離、壅塞或 2.4GHz 干擾造成的延遲與斷線問題。

由於使用了一般路由器無法調整的特殊設定，與一般路由器相比可節省約 10% 追蹤器的電力。

🖼️ 實測數據（貼文）: <https://x.com/NekodaKohaku/status/2067842378573254682>

### ⚠️ 硬體相容性

目前此韌體已完整測試於 **ESP32-S3 Dongle**。

- ✅ **測試設備:** ESP32-S3-WROOM-1

ESP32-S2 理論上可能可行，但最大連線數與穩定性尚未確認。

### ⚙️ 預設 WiFi 設定

| 設定項目 | 預設值 | 備註 |
| :--- | :--- | :--- |
| **SSID** | `SlimeDongle` | 追蹤器要連線的 WiFi 名稱 |
| **密碼** | `slimedongle12345` | 連線密碼 |
| **UDP 埠** | `6969` | SlimeVR 預設埠 |
| **最大連線數** | `10` | ESP32 SoftAP 的最大限制 |

### 🛠️ 設定修改

若要修改 WiFi 名稱、密碼、頻道或其他設定，請編輯：

```text
src/WifiDongleConfig.h
```

主要可調整項目：

- SoftAP 的 SSID / 密碼
- WiFi 頻道
- 追蹤器逾時
- 心跳間隔
- 最大連線數

### 🚀 快速上手

1. 將韌體燒錄到 ESP32-S3 Dongle。
2. 將 Dongle 插到 PC 的 USB 埠。
3. 啟動 PC 上的 SlimeVR Server。
4. 將 SlimeVR 追蹤器的 WiFi 設定改成：

   ```text
   SSID: SlimeDongle
   Password: slimedongle12345
   ```

5. 追蹤器連線成功後，會顯示在 SlimeVR Server 中。

> 💡 **備註：**
> 建議將 Dongle 接到電腦後面的 USB 插孔。
> 如果電腦位置較低，建議使用 USB 延長線將其放到較高的位置。

### 📚 參考

- <https://github.com/SlimeVR/SlimeVR-Tracker-ESP-Receiver>
- <https://github.com/SlimeVR/SlimeVR-Tracker-ESP/tree/espnow>
- <https://github.com/mitzey234/SlimeVR-Tracker-ESP/tree/esp-now>
- <https://github.com/Kirisame-Nanoha/mocopi-slimevr-hid-receiver/tree/main>
