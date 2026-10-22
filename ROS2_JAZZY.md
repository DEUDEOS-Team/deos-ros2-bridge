# ROS 2 Jazzy ile Çalıştırma

`deos_ros_bridge`, ROS 2 komutlarını UDP üzerinden STM32'ye ileten ve STM32 telemetrisini ROS topic'lerinde yayınlayan bir C++ node paketidir.

## 1. Paketi bilgisayara alın

ROS 2 ekibi, Jazzy ve geliştirme araçları kurulu bilgisayarda bu repoyu aşağıdaki dizine yerleştirecek:

```text
~/ros2_ws/src/deos_ros_bridge/
```

`package.xml` ve `CMakeLists.txt` bu dizinin doğrudan içinde olmalı.

## 2. Araç ve ağ ayarlarını yapın

`config/params.yaml` dosyasında şu değerleri gerçek araca göre düzenleyin:

- `udp_remote_ip`: STM32'nin IP adresi.
- `udp_remote_port`: STM32'nin dinlediği UDP portu.
- `wheelbase`: Ön ve arka aks arası mesafe, metre.
- `max_steering_angle_rad`: Maksimum ön tekerlek açısı, radyan.

Bilgisayar ile STM32 arasında ağ erişimi bulunmalı. Diğer ROS 2 node'larıyla iletişim için `ROS_DOMAIN_ID` ve ağ ayarları uyumlu olmalı.

## 3. Paketi derleyin

```bash
source /opt/ros/jazzy/setup.bash
cd ~/ros2_ws
rosdep install --from-paths src --ignore-src -r -y
colcon build --packages-select deos_ros_bridge
```

## 4. Bridge'i başlatın

```bash
source /opt/ros/jazzy/setup.bash
source ~/ros2_ws/install/setup.bash
ros2 launch deos_ros_bridge bridge.launch.py
```

Bu komut çalıştığı sürece bridge `/deos_ros_bridge` adıyla ayakta kalır. Terminalde `Ctrl+C` ile durdurulur.

## 5. Çalıştığını kontrol edin

Başka bir terminalde:

```bash
source /opt/ros/jazzy/setup.bash
source ~/ros2_ws/install/setup.bash
ros2 node info /deos_ros_bridge
ros2 topic list
ros2 topic echo /sensors/imu/data
```

IMU verisi görmek için STM32'nin bağlı ve veri gönderiyor olması gerekir.

## 6. Otonom yazılıma bağlayın

Karşı ekibin yazılımı `/cmd_vel`, `/vehicle/autonomy_enable` ve `/vehicle/motion_enable` topic'lerine mesaj gönderir; bridge bunları STM32'ye iletir. STM32 telemetrisi `/sensors/imu/data`, `/vehicle/state/speed` gibi topic'lerde yayınlanır. Bridge'i başlatmak tek başına hareket izni vermez.

Her seferinde ayrı terminal açmamak için `bridge.launch.py` aracın ana launch dosyasına dahil edilir. Cihaz açılışında otomatik başlatma istenirse ayrıca bir `systemd` servisi tanımlanır. Sadece derlemek node'u başlatmaz.

Mevcut doğrulama: bağımsız birim testleri ve UDP benchmark geçti; Jazzy üzerinde derleme, topic testleri ve gerçek STM32 entegrasyonu henüz doğrulanmadı.
