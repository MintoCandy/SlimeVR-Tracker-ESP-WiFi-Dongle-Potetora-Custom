/*
	Official-compatible UDP proxy for stock SlimeVR ESP trackers.
*/

#include "SlimeServerEmu.h"
#include "packetHandling.h"
#include "esp_wifi.h"
#include "Preferences.h" // フラッシュメモリ保存用のライブラリ

SlimeServerEmu SlimeServerEmu::instance;

static const char kHandshakeMagic[] = "Hey OVR =D 5";
static constexpr size_t kHandshakeMagicLen = 12;

SlimeServerEmu &SlimeServerEmu::getInstance() { return instance; }

ErrorCodes SlimeServerEmu::begin(
	const char *ssid, const char *password,
	uint8_t channel, uint8_t maxConn, bool hidden
) {
	m_maxConn = maxConn;

	uint8_t useChannel = channel;
	if (channel == 0) {
		useChannel = pickBestChannel();
	}

	WiFi.mode(WIFI_AP);
	WiFi.softAP(ssid, password, useChannel, hidden ? 1 : 0, maxConn);

	wifi_config_t cfg;
	esp_wifi_get_config(WIFI_IF_AP, &cfg);
	cfg.ap.beacon_interval = kBeaconIntervalMs;
	cfg.ap.dtim_period     = kDtimPeriod;
	esp_wifi_set_config(WIFI_IF_AP, &cfg);
	esp_wifi_set_ps(WIFI_PS_NONE);

	if (m_udp.listen(kPort)) {
		m_udp.onPacket([this](AsyncUDPPacket pkt) { onPacket(pkt); });
	}
	Serial.printf("[Emu] SlimeVR server emulator on ch %u, listening on :%u\n", useChannel, kPort);
	return ErrorCodes::NO_ERROR;
}

uint8_t SlimeServerEmu::pickBestChannel() {
	WiFi.mode(WIFI_AP_STA);
	int n = WiFi.scanNetworks(false, false);
	if (n <= 0) {
		WiFi.scanDelete();
		Serial.println("[Emu] channel scan: no APs found, default ch 1");
		return 1;
	}

	const uint8_t cands[3] = {1, 6, 11};
	long score[3] = {0, 0, 0};

	for (int i = 0; i < n; i++) {
		int ch = WiFi.channel(i);
		int rssi = WiFi.RSSI(i);
		long w = rssi + 100;
		if (w < 1) w = 1;
		for (int k = 0; k < 3; k++) {
			int dist = abs(ch - static_cast<int>(cands[k]));
			if (dist == 0)      score[k] += w * 4;
			else if (dist <= 2) score[k] += w * 2;
			else if (dist <= 4) score[k] += w * 1;
		}
	}
	WiFi.scanDelete();

	int best = 0;
	for (int k = 1; k < 3; k++) if (score[k] < score[best]) best = k;

	Serial.printf("[Emu] channel scan: ch1=%ld ch6=%ld ch11=%ld -> pick ch %u\n",
	              score[0], score[1], score[2], cands[best]);
	return cands[best];
}

uint8_t SlimeServerEmu::connectedCount() const {
	uint8_t n = 0;
	for (size_t i = 0; i < kMaxTrackers; i++) if (m_peers[i].used) n++;
	return n;
}

// discovery handshake 佈局(tracker -> server):
//   [0..2]0 [3]3 [4..11]packetNum
//   body@12: board(BE u32) imu(4) mcu(BE u32) imuInfo*3(12) protocol(4)
//   @40: fwVersion shortstring(1+N)   @41+N: MAC(6)
bool SlimeServerEmu::parseHandshake(const uint8_t *data, size_t len, uint8_t outMac[6],
                                    uint32_t &boardType, uint32_t &mcuType, uint32_t &imuType) {
	constexpr size_t kBodyOffset = 12;
	constexpr size_t kFwLenOffset = 40;
	if (len <= kFwLenOffset) return false;

	boardType = readBeU32(&data[kBodyOffset]);       // @12
	imuType   = readBeU32(&data[kBodyOffset + 4]);    // @16
	mcuType   = readBeU32(&data[kBodyOffset + 8]);    // @20

	uint8_t fwLen = data[kFwLenOffset];
	size_t macOffset = kFwLenOffset + 1 + fwLen;
	if (len < macOffset + 6) return false;
	memcpy(outMac, &data[macOffset], 6);
	return true;
}

int SlimeServerEmu::findPeerByMac(const uint8_t mac[6]) {
	for (size_t i = 0; i < kMaxTrackers; i++)
		if (m_peers[i].used && memcmp(m_peers[i].mac, mac, 6) == 0)
			return static_cast<int>(i);
	return -1;
}

int SlimeServerEmu::findPeerByIp(const IPAddress &ip) {
	for (size_t i = 0; i < kMaxTrackers; i++)
		if (m_peers[i].used && m_peers[i].ip == ip)
			return static_cast<int>(i);
	return -1;
}

uint8_t SlimeServerEmu::allocTrackerId() {
	for (uint8_t id = 0; id < kMaxTrackers; id++) {
		bool taken = false;
		for (size_t i = 0; i < kMaxTrackers; i++)
			if (m_peers[i].used && m_peers[i].trackerId == id) { taken = true; break; }
		if (!taken) return id;
	}
	return 0;
}

int SlimeServerEmu::findOrAddPeer(const uint8_t mac[6], const IPAddress &ip, uint16_t port, bool &isNew) {
	isNew = false;
	int idx = findPeerByMac(mac);
	if (idx >= 0) {
		m_peers[idx].ip = ip;
		m_peers[idx].port = port;
		return idx;
	}
	for (size_t i = 0; i < kMaxTrackers; i++) {
		if (!m_peers[i].used) {
			m_peers[i].used = true;

			Preferences prefs;
			prefs.begin("slime_pairs", false); // "slime_pairs" という名前空間を開く

			int assignedId = -1;

			//過去にこのMACアドレスが登録されたスロット（ID）があるか確認
			for (uint8_t id = 0; id < kMaxTrackers; id++) {
				char key[8];
				snprintf(key, sizeof(key), "tr_%u", id);
				
				//キーが実際に存在する場合のみ読み込みを行う（エラーログ防止）
				if (prefs.isKey(key)) {
					uint8_t savedMac[6] = {0};
					size_t readLen = prefs.getBytes(key, savedMac, 6);

					if (readLen == 6 && memcmp(savedMac, mac, 6) == 0) {
						assignedId = id; // 過去のIDを復元
						Serial.printf("[Emu] Saved pairing found: ID %u for MAC %02x:%02x:%02x:%02x:%02x:%02x\n", 
							id, mac[0], mac[1], mac[2], mac[3], mac[4], mac[5]);
						break;
					}
				}
			}

			//過去の登録がない場合、空いているスロットを探して新規保存
			if (assignedId == -1) {
				for (uint8_t id = 0; id < kMaxTrackers; id++) {
					char key[8];
					snprintf(key, sizeof(key), "tr_%u", id);
					
					bool isEmpty = false;
					
					//キーが存在しなければ空きスロット。存在しても中身が0なら空きスロット。
					if (!prefs.isKey(key)) {
						isEmpty = true;
					} else {
						uint8_t savedMac[6] = {0};
						uint8_t zeroMac[6] = {0};
						if (prefs.getBytes(key, savedMac, 6) != 6 || memcmp(savedMac, zeroMac, 6) == 0) {
							isEmpty = true;
						}
					}

					if (isEmpty) {
						prefs.putBytes(key, mac, 6);
						assignedId = id;
						Serial.printf("[Emu] New pairing saved: ID %u for MAC %02x:%02x:%02x:%02x:%02x:%02x\n", 
							id, mac[0], mac[1], mac[2], mac[3], mac[4], mac[5]);
						break;
					}
				}
			}

			//もし空きスロットがなく登録できなかった場合、全リセットして0番から再登録
			if (assignedId == -1) {
				Serial.println("[Emu] !!! No flash slot available !!! Clearing all saved pairings...");
				
				prefs.clear(); // "slime_pairs" 内のすべてのペアリングデータをフラッシュから消去
				
				// 今回受信したMACアドレスを、初期化したスロット0番（ID: 0）に保存
				prefs.putBytes("tr_0", mac, 6);
				assignedId = 0;
				
				Serial.printf("[Emu] Pairings cleared. New start with: ID 0 for MAC %02x:%02x:%02x...\n", mac[0], mac[1], mac[2]);
			}

			prefs.end();

			m_peers[i].trackerId = static_cast<uint8_t>(assignedId);

			memcpy(m_peers[i].mac, mac, 6);
			m_peers[i].ip = ip;
			m_peers[i].port = port;
			m_peers[i].lastHeartbeatMs = 0;
			isNew = true;
			Serial.printf("[Emu] new tracker id=%u mac=%02x:%02x:%02x:%02x:%02x:%02x ip=%s\n",
				m_peers[i].trackerId, mac[0],mac[1],mac[2],mac[3],mac[4],mac[5],
				ip.toString().c_str());
			return static_cast<int>(i);
		}
	}
	return -1;
}

void SlimeServerEmu::sendHandshakeReply(const IPAddress &ip, uint16_t port) {
	uint8_t buf[1 + kHandshakeMagicLen];
	buf[0] = PKT_SRV_HANDSHAKE;
	memcpy(&buf[1], kHandshakeMagic, kHandshakeMagicLen);
	m_udp.writeTo(buf, sizeof(buf), ip, port);
}

void SlimeServerEmu::sendHeartbeat(Peer &p) {
	uint8_t buf[12];
	memset(buf, 0, sizeof(buf));
	buf[3] = PKT_SRV_HEARTBEAT;
	uint64_t pn = m_packetNumber++;
	for (int i = 0; i < 8; i++)
		buf[4 + i] = static_cast<uint8_t>((pn >> (8 * (7 - i))) & 0xFF);
	m_udp.writeTo(buf, sizeof(buf), p.ip, p.port);
}

// ---- Accel(4):body@12 = x,y,z (f32 BE), sensorId(1). len=25 ----
void SlimeServerEmu::handleAccel(Peer &p, const uint8_t *data, size_t len) {
	if (len < 25) return;

	// 24バイト目からセンサーIDを取得 
	uint8_t sensorId = data[24]; 
	// MAX_SENSORS_PER_TRACKER 以上の場合は配列外参照防止のため弾く
	if (sensorId >= MAX_SENSORS_PER_TRACKER) return; 

	// sensorId の位置へ動的に加速度を保存
	p.accelFixed[sensorId][0] = toFixed<7>(readBeFloat(&data[12]));
	p.accelFixed[sensorId][1] = toFixed<7>(readBeFloat(&data[16]));
	p.accelFixed[sensorId][2] = toFixed<7>(readBeFloat(&data[20]));
}

// ---- RotationData(17):[12]sensorId [13]dataType [14..29]quat xyzw(f32 BE) [30]acc ----
void SlimeServerEmu::handleRotation(Peer &p, const uint8_t *data, size_t len) {
    if (len < 31) return;
    uint8_t sensorId = data[12] & 0x0F;

    // 定義した最大センサー数以上の ID が来た場合は破棄
    if (sensorId >= MAX_SENSORS_PER_TRACKER) return;

    int16_t qxF = toFixed<15>(readBeFloat(&data[14]));
    int16_t qyF = toFixed<15>(readBeFloat(&data[18]));
    int16_t qzF = toFixed<15>(readBeFloat(&data[22]));
    int16_t qwF = toFixed<15>(readBeFloat(&data[26]));

    uint8_t payload[15];
    // パケット先頭に (物理トラッカーID << 4 | センサーID) をセット
    payload[0] = static_cast<uint8_t>((p.trackerId << 4) | sensorId);
    memcpy(&payload[1], &qxF, 2);
    memcpy(&payload[3], &qyF, 2);
    memcpy(&payload[5], &qzF, 2);
    memcpy(&payload[7], &qwF, 2);
    
    // sensorId の加速度バッファから直接取り出す
    memcpy(&payload[9],  &p.accelFixed[sensorId][0], 2);
    memcpy(&payload[11], &p.accelFixed[sensorId][1], 2);
    memcpy(&payload[13], &p.accelFixed[sensorId][2], 2);

    PacketHandling::getInstance().insert(payload);
}

// ---- Battery(12):body@12 = voltage(f32 BE), percentage(f32 BE). len=20 ----
void SlimeServerEmu::handleBattery(Peer &p, const uint8_t *data, size_t len) {
	if (len < 20) return;
	float voltage = readBeFloat(&data[12]);     // volts
	float pct     = readBeFloat(&data[16]);     // 0..1 或 0..100(見下方註)
	// 官方 percentage 是 0..1;轉成 0..100
	uint8_t pctU = static_cast<uint8_t>(std::clamp(pct <= 1.0f ? pct * 100.0f : pct, 0.0f, 100.0f));
	uint16_t mv  = static_cast<uint16_t>(std::clamp(voltage, 0.0f, 6.5f) * 1000.0f);
	PacketHandling::getInstance().setBattery(p.trackerId, pctU, mv);
}

// ---- Temperature(20):body@12 = sensorId(1), temp(f32 BE). len=17 ----
void SlimeServerEmu::handleTemperature(Peer &p, const uint8_t *data, size_t len) {
    if (len < 17) return;
    
    // 12バイト目からセンサーIDを取得する
    uint8_t sensorId = data[12]; 

    // 最大センサー数以上のIDが来た場合は弾く（安全対策）
    if (sensorId >= MAX_SENSORS_PER_TRACKER) return;

    float tempC = readBeFloat(&data[13]);
    
    // 16刻みの動的HID ID計算 (sensor 0: ID+0, sensor 1: ID+16, sensor 2: ID+32...)
    uint8_t hidId = p.trackerId + (sensorId * 16);
    
    PacketHandling::getInstance().setTemp(hidId, encodeTemp(tempC));
}

// ---- SignalStrength(19):body@12 = sensorId(1), strength(int8). len=14 ----
void SlimeServerEmu::handleSignal(Peer &p, const uint8_t *data, size_t len) {
	if (len < 14) return;
	
	int8_t rssi = static_cast<int8_t>(data[13]);
	
	// メインに電波強度をセットする
	PacketHandling::getInstance().setRssi(p.trackerId, rssi);
}

void SlimeServerEmu::handleSensorInfo(Peer &p, const uint8_t *data, size_t len) {
    if (len < 15) return;
    
    // 12バイト目からセンサーIDとステータスを取得する
    uint8_t sensorId = data[12];

    // 最大センサー数以上のIDが来た場合は弾く（配列外参照・不正データ対策）
    if (sensorId >= MAX_SENSORS_PER_TRACKER) return;

    uint8_t sensorStatus = data[13];
    uint8_t imuId = data[14];

    // 16刻みの動的HID ID計算 (sensor 0: ID+0, sensor 1: ID+16, sensor 2: ID+32...)
    uint8_t hidId = p.trackerId + (sensorId * 16);
    
    // 割り出した正しいID（hidId）でPC側へ通知する
    PacketHandling::getInstance().setSensorInfo(hidId, imuId, 0);

    // もし物理的に本当に通信エラー(statusが正常を示す1以外)が起きている場合はログを出す
    if (sensorStatus != 1) {
        Serial.printf("[Emu] Tracker %d Sensor %d reported STATUS ERROR: %d\n", p.trackerId, sensorId, sensorStatus);
    }
}

void SlimeServerEmu::onPacket(AsyncUDPPacket &pkt) {
	const uint8_t *data = pkt.data();
	size_t len = pkt.length();
	if (len < 4) return;

	uint8_t type = data[3];

	if (type == PKT_SEND_HANDSHAKE) {
		uint8_t mac[6];
		uint32_t boardType = 0, mcuType = 0, imuType = 0;
		if (!parseHandshake(data, len, mac, boardType, mcuType, imuType)) {
			Serial.println("[Emu] handshake parse failed");
			return;
		}
		bool isNew = false;
		int idx = findOrAddPeer(mac, pkt.remoteIP(), pkt.remotePort(), isNew);
		if (idx < 0) { Serial.println("[Emu] tracker table full"); return; }

		sendHandshakeReply(pkt.remoteIP(), pkt.remotePort());
		sendHeartbeat(m_peers[idx]);
		m_peers[idx].lastHeartbeatMs = millis();
		m_peers[idx].lastSeenMs = millis();
		m_peers[idx].connected = true;

		if (isNew && m_onConnected)
			m_onConnected(m_peers[idx].trackerId, m_peers[idx].mac);

		PacketHandling::getInstance().setTrackerOnline(m_peers[idx].trackerId, true);

		PacketHandling::getInstance().setFirmware(
			m_peers[idx].trackerId,
			static_cast<uint8_t>(boardType),
			static_cast<uint8_t>(mcuType),
			0, 0, 0, 0
		);
		PacketHandling::getInstance().setSensorInfo(
			m_peers[idx].trackerId,
			static_cast<uint8_t>(imuType),   // IMU 類型(=15)
			0                                 // mag 先 0
		);
		return;
	}

	int idx = findPeerByIp(pkt.remoteIP());
	if (idx < 0) return;
	Peer &p = m_peers[idx];

	p.lastSeenMs = millis();

	if (p.port != pkt.remotePort()) {
        Serial.printf("[Emu] Tracker ID %d (IP: %s) ポート変更を検知: %d -> %d\n", 
                      p.trackerId, 
                      pkt.remoteIP().toString().c_str(), 
                      p.port, 
                      pkt.remotePort());
                      
        p.port = pkt.remotePort(); // 新しいポートで上書き
    }
	
	if (!p.connected) {
		p.connected = true;
		PacketHandling::getInstance().setTrackerOnline(p.trackerId, true);
	}

	switch (type) {
		case PKT_SEND_ACCEL:      handleAccel(p, data, len);       break;
		case PKT_SEND_ROTATION:   handleRotation(p, data, len);    break;
		case PKT_SEND_BATTERY:    handleBattery(p, data, len);     break;
		case PKT_SEND_TEMP:       handleTemperature(p, data, len); break;
		case PKT_SEND_SIGNAL:     handleSignal(p, data, len);      break;
		case PKT_SEND_SENSORINFO: handleSensorInfo(p, data, len);  break;
		default: break;
	}
}

void SlimeServerEmu::update() {
	uint32_t now = millis();
	for (size_t i = 0; i < kMaxTrackers; i++) {
		Peer &p = m_peers[i];
		if (!p.used) continue;

		if (kEnableHeartbeat && now - p.lastHeartbeatMs >= kHeartbeatIntervalMs) {
			p.lastHeartbeatMs = now;
			sendHeartbeat(p);
		}

		if (p.connected && (now - p.lastSeenMs >= kTrackerTimeoutMs)) {
			p.connected = false;
			Serial.printf("[Emu] tracker id=%u timed out -> disconnected\n", p.trackerId);
			PacketHandling::getInstance().setTrackerOnline(p.trackerId, false);
		}
	}
}
