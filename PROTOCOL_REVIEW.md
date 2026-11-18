# DEOS protokol ve firmware incelemesi

İncelenen firmware `5cb57e7`, taşınabilir protokol `70d846f`.
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

## Taşınabilir kütüphane bulguları

- `src/deos_frame.c`: decoder, bildirilen payload eksikse hata veriyor
  ancak payload sonundaki fazladan baytı kabul ediyor. Masaüstünde sıfır
  payload ve bir fazla bayt ile dönüş değeri 0 olarak doğrulandı.
  Bridge hem UDP uzunluk önekini hem tam Universal Header uzunluğunu denetler.
- `src/deos_registry.c`: wildcard önce kaydedilmişse eşleşen bir unicast
  mesajı wildcard ve düğüme özel callback'leri birlikte tetikleyebiliyor.
  İki callback ile masaüstünde doğrulandı. Bridge yalnızca düğüme özel
  kayıtlar kullanır.
- `python/deos.py`: registry tamponu sabit 1300 bayt. C kütüphanesi
  kapasiteyle birlikte yapıyı büyütüyor. Örneğin masaüstünde 64 girişlik
  yapı 2568 bayt. Ayrıca Python little-endian getter'ları kısa girişin
  uzunluğunu doğrulamadan sabit genişlik okuyan C fonksiyonunu çağırır.
  Bridge C registry ve codec'i kullanır. Sahte STM32, Python registry
  kullanmaz ve payload genişliğini okumadan önce denetler.
- `include/deos/deos_types.h`: C99 esnek dizi alanı, Jazzy'nin C++
  derlemesinde pedantic uyarısı üretiyor. Derleme tamamlanıyor.
