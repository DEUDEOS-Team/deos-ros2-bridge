# DEOS ROS 2 Bridge (`deos_ros_bridge`)

C++ tabanlı, yüksek performanslı **ROS 2** ve **DEOS Gömülü Araç Kontrol Ağı (STM32 VCU)** köprü düğümü (Bridge Node).

---

## 1. Genel Bakış ve Mimari

Bu paket, Raspberry Pi (veya x86 IPC) üzerinde çalışan otonom sürüş yazılımları (Nav2, SLAM, yörünge planlayıcı) ile araç üzerindeki merkezi mikrodenetleyici (**Main STM32 / VCU**) ve CAN-FD aktüatörleri (Traction, Steering, Brake, BMS, IMU) arasındaki çift yönlü veri akışını ve protokol çevrimini yönetir.

Köprü, DEOS ağı üzerinde **`0x04` (`DEOS_NODE_MICRO_ROS`)** sanal düğüm kimliği ile işlem yapar.

```text
┌────────────────────────────────────────────────────────┐
│             OTONOM BİLGİSAYAR (ROS 2)                  │
│       Nav2, SLAM, Planlama, Durum Yöneticisi           │
└───────────────────────────┬────────────────────────────┘
                            │ ROS 2 Topics (DDS / SHM)
                            ▼
┌────────────────────────────────────────────────────────┐
│             AĞ KÖPRÜSÜ (deos_ros_bridge)               │
│  - Ackermann Kinematik Dönüşümü                        │
│  - Birim Dönüşümü (SI Float <-> Fixed-Point Integer)   │
│  - 200 ms Komut Bekçisi (Watchdog Safety)              │
│  - 1 Hz Heartbeat ve İstemci Tanıma (Handshake)        │
└───────────────────────────┬────────────────────────────┘
                            │ Raw UDP (Port 5001)
                            ▼
┌────────────────────────────────────────────────────────┐
│             ANA ARAÇ KONTROLCÜSÜ (STM32 VCU)           │
│           DEOS Yönlendirici, State Machine, LEDs       │
└───────────────────────────┬────────────────────────────┘
                            │ CAN-FD Omurgası
             ┌──────────────┼──────────────┐
             ▼              ▼              ▼
     [ Traction ECU ] [ Steering ECU ] [ Brake / BMS ]
```

---

## 2. Ağ ve Çerçeve (Framing) Formatı

* **Protokol:** Bağlantısız UDP soketi (`SOCK_DGRAM`)
* **Hedef STM32 Adresi:** `192.168.1.10:5001` (Varsayılan)
* **İstemci Tanıma (Client Discovery):** UDP bağlantısız olduğu için STM32 ilk açıldığında Pi'nin IP ve portunu bilmez. Köprü düğümü her 1 saniyede bir `DEOS_CMD_SYSTEM_HEARTBEAT` paketi yollar; STM32 bu paketi yakalayarak Pi'nin adresini kaydeder ve telemetri yayınına başlar.

### UDP Tel Çerçevesi (Wire Frame)
Her UDP paketi **1 Bayt Uzunluk**, **8 Bayt Başlık** ve **$N$ Bayt Yükten (Payload)** oluşur (Tüm tamsayılar Little-Endian):

```text
[ Byte 0 ] : Toplam Çerçeve Uzunluğu (8 + Payload Uzunluğu)
[ Byte 1 ] : Priority       (Örn: 1: Critical Control, 2: Control, 5: Network)
[ Byte 2 ] : Message Class  (1: Command, 2: Status, 7: Network)
[ Byte 3 ] : Service ID     (0: System, 1: Traction, 2: Steering, 7: Sensors, 8: Lighting)
[ Byte 4 ] : Destination    (3: Main STM32, 0x10: Traction, 0x11: Steering)
[ Byte 5 ] : Source         (0x04: DEOS_NODE_MICRO_ROS)
[ Byte 6 ] : Version        (0x11: DEOS_PROTOCOL_VERSION)
[ Byte 7 ] : Sequence       (0..255 artan sayaç)
[ Byte 8 ] : Command ID     (Servise özel komut)
[ Byte 9+] : Payload        (0..60 Bayt veriler)
```

---

## 3. ROS 2 Arayüzü (Topic Listesi)

### Abonelikler (Subscribers — ROS 2 $\rightarrow$ STM32)
| Topic | Mesaj Tipi | Açıklama |
| :--- | :--- | :--- |
| `/cmd_vel` | `geometry_msgs/msg/Twist` | Doğrusal hız ($v$) ve açısal hız ($\omega$). Ackermann açısına çevrilip Traction ve Steering ECU'larına yollanır. |
| `/vehicle/autonomy_enable` | `std_msgs/msg/Bool` | Otonom sürüş izin bayrağı (Watchdog süresi: 500 ms). |
| `/vehicle/motion_enable` | `std_msgs/msg/Bool` | Fiziksel hareket izin bayrağı (Watchdog süresi: 250 ms). |
| `/vehicle/set_state` | `std_msgs/msg/UInt8` | Araç durum değiştirme talebi (`1: STANDBY`, `2: READY`, `3: ACTIVE`, `5: SAFE`). |
| `/vehicle/lights/headlight` | `std_msgs/msg/Bool` | Ön far kontrolü (`TURN_ON: 0x01` / `TURN_OFF: 0x02`, 1-bayt `light_id`). |
| `/vehicle/lights/brake` | `std_msgs/msg/Bool` | Fren lambası kontrolü. |
| `/vehicle/signals/left` | `std_msgs/msg/Bool` | Sol sinyal kontrolü. |
| `/vehicle/signals/right` | `std_msgs/msg/Bool` | Sağ sinyal kontrolü. |
| `/vehicle/signals/hazard` | `std_msgs/msg/Bool` | Dörtlü flaşör kontrolü. |

> **Not (`FUNCTIONAL_SPLIT.md` Madde 3.1):** ROS 2 otonom tarafı periyodik 1 Hz yanıp sönme (blink) yapmaz. Köprü sadece tek bir `TURN_ON` / `TURN_OFF` komutu yollar; 1 Hz donanımsal yanıp sönme STM32 üzerindeki `LedController` tarafından yürütülür.

### Yayıncılar (Publishers — STM32 $\rightarrow$ ROS 2)
| Topic | Mesaj Tipi | Açıklama |
| :--- | :--- | :--- |
| `/sensors/imu/data` | `sensor_msgs/msg/Imu` | STM32'den 10 Hz yayınlanan ivmeölçer ($\text{m/s}^2$) ve jiroskop ($\text{rad/s}$) verisi. |
| `/vehicle/state/speed` | `std_msgs/msg/Float32` | Araç güncel hızı ($\text{m/s}$). Geri viteste negatif değer üretir. |
| `/vehicle/state/steering_angle` | `std_msgs/msg/Float32` | Güncel ön tekerlek açısı (radyan). |
| `/vehicle/state_machine/state` | `std_msgs/msg/UInt8` | STM32'nin güncel araç durum makinesi durumu (`0..7`). |
| `/vehicle/bms/voltage` | `std_msgs/msg/Float32` | Batarya paket voltajı ($\text{V}$). |
| `/vehicle/bms/soc` | `std_msgs/msg/Float32` | Batarya şarj yüzdesi ($\%$). |

---

## 4. Kinematik ve Güvenlik Bekçisi (Safety Watchdog)

### Ackermann Kinematiği
Köprü, gelen `geometry_msgs/msg/Twist` verisini dingil mesafesi ($L$) parametresiyle ön tekerlek açısına çevirir:
$$\delta = \arctan\left(\frac{L \cdot \omega}{v}\right)$$
Tekerlek açısı `max_steering_angle_rad` (varsayılan $0.60\text{ rad} \approx 34.4^\circ$) ile sınırlandırılır.

### Güvenlik Failsafe Mekanizması
* `/cmd_vel` mesajı **200 ms** boyunca gelmezse, veya
* `/vehicle/autonomy_enable` **500 ms** boyunca yenilenmezse, veya
* `/vehicle/motion_enable` **250 ms** boyunca yenilenmezse;

Köprü, kontrolsüz hareketleri önlemek için derhal Traction ECU'ya **`target_speed = 0`** ve **`target_accel = 2.0 m/s²`** durma komutu fırlatır.

---

## 5. Derleme ve Çalıştırma (Raspberry Pi / ROS 2)

### Bağımlılıklar
* ROS 2 (Humble, Iron veya Jazzy)
* `rclcpp`, `std_msgs`, `geometry_msgs`, `sensor_msgs`

### Derleme
ROS 2 çalışma alanınızda (örn: `~/ros2_ws`):
```bash
cd ~/ros2_ws
colcon build --packages-select deos_ros_bridge
source install/setup.bash
```

### Parametreler ile Başlatma (Tek Dosyadan Yönetim)
Tüm parametreler `config/params.yaml` dosyası üzerinden yönetilir. 

**Önerilen Yöntem (Launch Dosyası ile):**
```bash
ros2 launch deos_ros_bridge bridge.launch.py
```
Farklı bir parametre dosyası belirtmek için:
```bash
ros2 launch deos_ros_bridge bridge.launch.py params_file:=/path/to/custom_params.yaml
```

**Dinamik Parametre Güncelleme (Çalışma Zamanında):**
Düğümü yeniden başlatmadan parametreleri güncelleyebilirsiniz:
```bash
ros2 param set /deos_ros_bridge wheelbase 2.2
ros2 param set /deos_ros_bridge cmd_vel_timeout_ms 300
```

---

## 6. Test ve Doğrulama Araçları

### A. Mock STM32 Sunucusu (`test/mock_stm32.py`)
Fiziksel STM32 kartı bağlı olmadan köprüyü test etmek için Python mock simülatörü çalıştırılabilir:
```bash
python3 test/mock_stm32.py
```
Bu betik UDP port `5001`'i dinler, Pi'den gelen Heartbeat ve `/cmd_vel` paketlerini ekrana basar ve Pi'ye 10 Hz simüle edilmiş IMU verisi basar.

### B. Uçtan Uca Hız ve Gecikme Benchmark'ı
```bash
g++ -O3 -std=c++17 -Iinclude test/benchmark_stress_test.cpp -lpthread -o test/benchmark_stress_test
./test/benchmark_stress_test
```
* **Tipik Gecikme (Round-Trip):** $\approx 14\ \mu\text{s}$ ($0.014\text{ ms}$)
* **Throughput:** $69,000+$ paket/saniye
