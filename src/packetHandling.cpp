#include "packetHandling.h"

PacketHandling &PacketHandling::getInstance() {
    return instance;
}

int PacketHandling::findTracker(uint8_t id) {
    for (size_t i = 0; i < MAX_TRACKERS; i++) {
        if (trackers[i].used && trackers[i].id == id) {
            return static_cast<int>(i);
        }
    }
    return -1;
}

bool PacketHandling::registerTracker(uint8_t trackerId, const uint8_t mac[6]) {
    int idx = findTracker(trackerId);
    bool isNew = (idx < 0);
    if (idx < 0) {
        for (size_t i = 0; i < MAX_TRACKERS; i++) {
            if (!trackers[i].used) { idx = static_cast<int>(i); break; }
        }
    }
    if (idx < 0) return false;
    trackers[idx].used = true;
    trackers[idx].id = trackerId;
    memcpy(trackers[idx].mac, mac, 6);
    return isNew;
}

void PacketHandling::updateRssiByMac(const uint8_t mac[6], int8_t rssi) {
    for (size_t i = 0; i < MAX_TRACKERS; i++)
        if (trackers[i].used && memcmp(trackers[i].mac, mac, 6) == 0) {
            trackers[i].rssi = rssi;
            return;
        }
}

// ---- 官方相容 setter:拿到就更新對應欄位 ----
void PacketHandling::setBattery(uint8_t trackerId, uint8_t pct, uint16_t mv) {
    // trackerId に属するすべての仮想IDを一括更新
    for (uint8_t s = 0; s < MAX_SENSORS_PER_TRACKER; s++) {
        uint8_t targetHidId = trackerId + (s * 16);
        int idx = findTracker(targetHidId);
        if (idx >= 0) {
            trackers[idx].batt = pct;
            trackers[idx].battV = static_cast<uint8_t>((mv > 2450) ? (mv - 2450) / 10 : 0);
            trackers[idx].hasBattData = true;
        }
    }
}

void PacketHandling::setTemp(uint8_t trackerId, uint8_t tempEncoded) {
    int idx = findTracker(trackerId);
    if (idx < 0) return;
    trackers[idx].temp = tempEncoded;
}

void PacketHandling::setRssi(uint8_t trackerId, int8_t rssi) {
    int idx = findTracker(trackerId);
    if (idx < 0) return;
    trackers[idx].rssi = rssi;
}

void PacketHandling::setSensorInfo(uint8_t trackerId, uint8_t imuId, uint8_t magId) {
    int idx = findTracker(trackerId);
    if (idx < 0) return;
    trackers[idx].imuId = imuId;
    trackers[idx].magId = magId;
}

void PacketHandling::setFirmware(uint8_t trackerId, uint8_t brdId, uint8_t mcuId,
                                 uint16_t fwDate, uint8_t fwMajor, uint8_t fwMinor, uint8_t fwPatch) {
    int idx = findTracker(trackerId);
    if (idx < 0) return;
    trackers[idx].brdId = brdId;
    trackers[idx].mcuId = mcuId;
    trackers[idx].fwDate = fwDate;
    trackers[idx].fwMajor = fwMajor;
    trackers[idx].fwMinor = fwMinor;
    trackers[idx].fwPatch = fwPatch;
}

void PacketHandling::pushStatus(uint8_t trackerId, uint8_t status) {
    Packet p;
    memset(p.data, 0, HID_PACKET_SIZE);
    p.data[0] = 3;
    p.data[1] = trackerId;
    p.data[2] = status;
    priorityPush(p);
}

void PacketHandling::setTrackerOnline(uint8_t trackerId, bool online) {
    // trackerId に属するすべての仮想IDを一括更新
    for (uint8_t s = 0; s < MAX_SENSORS_PER_TRACKER; s++) {
        uint8_t targetHidId = trackerId + (s * 16);
        int idx = findTracker(targetHidId);
        if (idx >= 0) {
            if (trackers[idx].online != online) {
                trackers[idx].online = online;
                if (!online) {
                    pushStatus(targetHidId, 0);
                    Serial.printf("[HID] tracker %u marked DISCONNECTED\n", targetHidId);
                } else {
                    pushStatus(targetHidId, 1);
                    lastRegSentMs = 0;
                    Serial.printf("[HID] tracker %u back ONLINE\n", targetHidId);
                }
            }
        }
    }
}

void PacketHandling::fifoPush(const Packet &p, uint8_t trackerId) {
    portENTER_CRITICAL(&m_mux);
    // 去重:同一 tracker 的資料封包(data[0]==1)已在佇列就原地更新
    if (!fifoEmpty()) {
        size_t idx = fifoTail;
        while (idx != fifoHead) {
            if (fifo[idx].data[1] == trackerId && fifo[idx].data[0] == 1) {
                fifo[idx] = p;
                portEXIT_CRITICAL(&m_mux);
                return;
            }
            idx = (idx + 1) % FIFO_SIZE;
        }
    }
    if (fifoFull) {
        portEXIT_CRITICAL(&m_mux);
        return;
    }
    fifo[fifoHead] = p;
    fifoHead = (fifoHead + 1) % FIFO_SIZE;
    if (fifoHead == fifoTail) fifoFull = true;
    portEXIT_CRITICAL(&m_mux);
}

bool PacketHandling::fifoPop(Packet &out) {
    portENTER_CRITICAL(&m_mux);
    if (fifoEmpty()) {
        portEXIT_CRITICAL(&m_mux);
        return false;
    }
    out = fifo[fifoTail];
    fifoTail = (fifoTail + 1) % FIFO_SIZE;
    fifoFull = false;
    portEXIT_CRITICAL(&m_mux);
    return true;
}

void PacketHandling::priorityPush(const Packet &p) {
    portENTER_CRITICAL(&m_mux);
    if (priorityFull) {
        portEXIT_CRITICAL(&m_mux);
        return;   // 滿了就丟(極少發生;16 格對 status 綽綽有餘)
    }
    priorityFifo[priorityHead] = p;
    priorityHead = (priorityHead + 1) % PRIORITY_FIFO_SIZE;
    if (priorityHead == priorityTail) priorityFull = true;
    portEXIT_CRITICAL(&m_mux);
}

bool PacketHandling::priorityPop(Packet &out) {
    portENTER_CRITICAL(&m_mux);
    if (priorityEmpty()) {
        portEXIT_CRITICAL(&m_mux);
        return false;
    }
    out = priorityFifo[priorityTail];
    priorityTail = (priorityTail + 1) % PRIORITY_FIFO_SIZE;
    priorityFull = false;
    portEXIT_CRITICAL(&m_mux);
    return true;
}

void PacketHandling::insert(const uint8_t *payload) {
    uint8_t trackerId = payload[0] >> 4;
    uint8_t sensorId = payload[0] & 0x0F;

    // 16刻みで動的に仮想HID IDを生成 (sensor 0: ID+0, sensor 1: ID+16, sensor 2: ID+32...)
    uint8_t hidId = trackerId + (sensorId * 16);

    int idx = findTracker(hidId);

    // 未登録の仮想トラッカー（拡張センサー）の初回データ受信時
    if (idx < 0) {
        if (sensorId > 0) {
            int mainIdx = findTracker(trackerId);
            if (mainIdx >= 0) {
                uint8_t extMac[6];
                memcpy(extMac, trackers[mainIdx].mac, 6);

                // 拡張センサーごとに被らないMACアドレスを作る
                extMac[5] ^= (0xA0 + sensorId);

                // 仮想トラッカーの登録
                if (registerTracker(hidId, extMac)) {
                    idx = findTracker(hidId);

                    if (idx >= 0) {
                        trackers[idx].online = true;

                        pushStatus(hidId, 1);

                        // メインセンサーの情報をすべてコピーする
                        trackers[idx].hasBattData = trackers[mainIdx].hasBattData;
                        trackers[idx].batt        = trackers[mainIdx].batt;
                        trackers[idx].battV       = trackers[mainIdx].battV;
                        trackers[idx].temp        = trackers[mainIdx].temp;
                        trackers[idx].brdId       = trackers[mainIdx].brdId;
                        trackers[idx].mcuId       = trackers[mainIdx].mcuId;
                        trackers[idx].imuId       = trackers[mainIdx].imuId;
                        trackers[idx].magId       = trackers[mainIdx].magId;
                        trackers[idx].fwDate      = trackers[mainIdx].fwDate;
                        trackers[idx].fwMajor     = trackers[mainIdx].fwMajor;
                        trackers[idx].fwMinor     = trackers[mainIdx].fwMinor;
                        trackers[idx].fwPatch     = trackers[mainIdx].fwPatch;
                    }
                }
            } else {
                return; // メインセンサーが未登録なら破棄
            }
        } else {
            return; // メインセンサーが未登録なら破棄
        }
    }

    Packet p;
    memset(p.data, 0, HID_PACKET_SIZE);
    p.data[0] = 1;
    p.data[1] = hidId; // HIDパケットには仮想IDをセット
    memcpy(&p.data[2], &payload[1], 8);
    memcpy(&p.data[10], &payload[9], 6);

    fifoPush(p, hidId);
}

void PacketHandling::insertInfo(const uint8_t *info) {
    uint8_t trackerId = info[0];
    int idx = findTracker(trackerId);
    if (idx < 0) return;

    trackers[idx].batt = info[1];
    uint16_t mv = info[2] | (info[3] << 8);
    trackers[idx].battV = static_cast<uint8_t>((mv > 2450) ? (mv - 2450) / 10 : 0);
    trackers[idx].temp = info[4];
    trackers[idx].brdId = info[5];
    trackers[idx].mcuId = info[6];
    trackers[idx].imuId = info[7];
    trackers[idx].magId = info[8];
    trackers[idx].fwDate = info[9] | (info[10] << 8);
    trackers[idx].fwMajor = info[11];
    trackers[idx].fwMinor = info[12];
    trackers[idx].fwPatch = info[13];
    trackers[idx].hasBattData = true;
}

void PacketHandling::tick(HIDDevice &hidDevice) {
    if (!hidDevice.ready()) return;

    uint32_t now = millis();

    // === 分散送信によるUSBバッファ溢れ対策 ===
    // 従来の「100msごとに全員分を一度に送る」処理を廃止し、
    // 「15msごとに1台ずつ順番に送る」ようにしてUSBパケットの消失を防ぎます。
    
    static size_t currentRegIdx = 0; // 現在処理中のトラッカー番号

    if (now - lastRegSentMs >= 15) {
        lastRegSentMs = now;

        // 次に送信すべきオンラインのトラッカーを探す
        size_t startIndex = currentRegIdx;
        bool found = false;
        do {
            if (trackers[currentRegIdx].used && trackers[currentRegIdx].online) {
                found = true;
                break;
            }
            currentRegIdx = (currentRegIdx + 1) % MAX_TRACKERS;
        } while (currentRegIdx != startIndex);

        if (found) {
            uint8_t report[HID_REPORT_SIZE];
            memset(report, 254, sizeof(report)); // 余ったスロットはダミー(254)で埋める

            TrackerInfo &ti = trackers[currentRegIdx];

            // 枠1: Register (IDとMACアドレスの通知)
            uint8_t *r = &report[0];
            r[0] = 255; 
            r[1] = ti.id;
            for (int b = 0; b < 6; b++) r[2 + b] = ti.mac[5 - b];

            // 枠2: Device Info (バッテリーやシステム情報 兼 PC側の生存確認)
            uint8_t *d = &report[HID_PACKET_SIZE];
            d[0] = 0;
            d[1] = ti.id;
            d[2] = ti.batt;
            d[3] = ti.battV;
            d[4] = ti.temp;
            d[5] = ti.brdId;
            d[6] = ti.mcuId;
            d[7] = ti.online ? 1 : 0;
            d[8] = ti.imuId;
            d[9] = ti.magId;
            d[10] = ti.fwDate & 0xFF;
            d[11] = (ti.fwDate >> 8) & 0xFF;
            d[12] = ti.fwMajor;
            d[13] = ti.fwMinor;
            d[14] = ti.fwPatch;
            d[15] = static_cast<uint8_t>(ti.rssi < 0 ? -ti.rssi : ti.rssi);

            // 1台分(レポート1つ分)だけUSBに送信
            hidDevice.send(report, HID_REPORT_SIZE);

            // 次の送信タイミングのためにインデックスを進める
            currentRegIdx = (currentRegIdx + 1) % MAX_TRACKERS;
        }
    }

    // === 第二部分:送高優先封包(status)+ 資料封包 ===
    static uint32_t lastDataSentMs = 0;
    if (now - lastDataSentMs < 5) {
        return;
    }
    lastDataSentMs = now;

    for (int rep = 0; rep < 8; rep++) {
        uint8_t report[HID_REPORT_SIZE];
        memset(report, 0, sizeof(report));
        int slot = 0;

        // 先填高優先封包(斷線 status 等),確保它們永不被資料壅塞延遲/丟棄
        while (slot < (int)PACKETS_PER_REPORT) {
            Packet p;
            if (!priorityPop(p)) break;
            memcpy(&report[slot * HID_PACKET_SIZE], p.data, HID_PACKET_SIZE);
            slot++;
        }

        // 再用一般資料封包填滿剩餘 slot
        while (slot < (int)PACKETS_PER_REPORT) {
            Packet p;
            if (!fifoPop(p)) break;   // fifoPop 內部有鎖;空了會回 false
            memcpy(&report[slot * HID_PACKET_SIZE], p.data, HID_PACKET_SIZE);
            slot++;
        }

        if (slot == 0) return;   // priority 和 data 都空了,結束
        for (int s = slot; s < (int)PACKETS_PER_REPORT; s++) {
            report[s * HID_PACKET_SIZE] = 254;
        }

        if (!hidDevice.send(report, HID_REPORT_SIZE)) {
            return;
        }
    }
}

PacketHandling PacketHandling::instance;
