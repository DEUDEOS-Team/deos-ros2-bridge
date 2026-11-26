# DEOS protokol ve firmware incelemesi

İncelenen firmware `5cb57e7`, taşınabilir protokol `0ebda91` (v1.2, `0x12`; önceki inceleme: `70d846f`).
Bridge, bu protokol revizyonunu `vendor/deos_protocol` alt modülünden derler.

## Ethernet ve IMU

- `ICD_Ethernet_Transport.pdf` bölüm 3 ve firmware
  `apps/main_node/src/transport/eth_ros_bridge.cpp` UDP/5001, bir bayt frame
  uzunluğu ve dokuz baytlık Universal Header tanımlar.
- `ICD_Network_Management.pdf` bölüm 2 ve `deos_core/src/deos_network.c`
  PING/PONG'u SYSTEM/NETWORK sınıfında tanımlar. PONG aynı dört baytlık
  Ping ID'yi ve gönderenin kendi sıra sayacını taşır.
- Aynı ICD'nin abonelik bölümünde `SUBSCRIBE` isteği altı bayttır. Servis ve
  komut, iki bayt little-endian aralık ve iki bayt kira süresi taşır.
  Kira süresinin sıfır olması süresiz abonelik anlamına gelir. Bridge
  açılışta IMU düğümüne (0x13), SENSORS/IMU_DATA için tek istek gönderir.
  Varsayılan 100 ms, firmware IMU thread'indeki 10 Hz nominal hızdan gelir.
  ROS tarafı depth 20 ve best-effort kullanır. DDS QoS, DEOS abonelik
  payload'ının bir alanı değildir.
- Firmware engeli: `apps/main_node/src/io/ImuSensor.cpp` örnek oluşturup
  bekliyor. IMU_DATA için REQUEST handler kaydı ve bir yayın çağrısı yok.
  `lib/deos_core/src/deos_subscription.c` handler yoksa
  `DEOS_RESULT_UNSUPPORTED` döndürüyor. Sahte STM32'nin kabul modu
  bridge'in veri yolunu sınar. Gerçek firmware'in bu revizyonunda beklenen
  sonuç aboneliğin reddedilmesidir.
- Abonelik ICD'si beş baytlık yanıt tanımlar. Firmware'deki
  `deos_send_response` bunun başına ayrıca genel sonuç kodunu ekler.
  Bridge firmware'in ilk sonuç baytını kullanır.

## Taşınabilir kütüphane bulguları (`vendor/deos_protocol`)

Aşağıdaki 6 bulgu ilk olarak `70d846f` revizyonunda tespit edilmiş, **`0ebda91` (v1.2 / `0x12`) güncellemesi ile tümü üst akışta (`deos-protocol`) çözülmüştür**:

- **#1 Python Traction komut ID'leri (`0x04`, `0x05`):** Çözüldü — `python/deos.py` artık tüm `DEOS_*` sabitlerini doğrudan C başlıklarından (`deos_icd.h`, `deos_frame.h`, `deos_types.h`) dinamik olarak okur.
- **#2 Python `DeosRegistry` tampon taşması ve `ctypes` enum ABI:** Çözüldü — C tarafına `deos_registry_sizeof()` eklendi, Python tamponu çalışma zamanı boyutuna göre ayırıyor, enum argümanları `ctypes.c_int` yapıldı ve `DeosRegistry(local_node)` zorunlu kılındı.
- **#3 Python LE getter uzunluk kontrolü:** Çözüldü — `get_u16_le`/`get_i16_le`/`get_u32_le`/`get_i32_le` eksik girişte `ValueError` fırlatıyor.
- **#4 `deos_frame_decode` fazla bayt ve versiyon doğrulaması:** Çözüldü — artık `version != DEOS_PROTOCOL_VERSION` (`-EPROTONOSUPPORT`) ve `9 + payload_len != frame_len` (`-EBADMSG`) kontrol ediliyor.
- **#5 `deos_registry_dispatch` unicast + wildcard çift tetikleme:** Çözüldü — unicast mesajlarda tam düğüm eşleşmesi wildcard (`0xFF`) yedeğine öncelikli, implicit `deos_registry_register()` sarmalayıcısı kaldırıldı (`deos_registry_register_for_node` kullanılıyor).
- **#6 `deos_types.h` C++ `-Wpedantic` uyarısı:** Çözüldü — `deos_parameter_payload::value[]` için `__extension__` eklendi; `-Wall -Wextra -Wpedantic` ile sıfır uyarı ile derleniyor.

> **Sürüm uyumluluğu notu:** `DEOS_PROTOCOL_VERSION` `0x11`'den `0x12`'ye yükseltildi ve `deos_frame_decode` artık versiyon uyuşmazlığını reddediyor. Gerçek STM32 firmware ve `deos_stm32_emulator` düğümlerinin de v1.2 (`0x12`) protokolüne güncellenmesi gerekir.
