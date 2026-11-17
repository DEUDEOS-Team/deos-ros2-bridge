# Raspberry Pi'de DEOS ROS 2 Bridge

Bu imaj 64 bit Raspberry Pi OS veya 64 bit Ubuntu çalıştıran Raspberry Pi'ler için
`linux/arm64` olarak derlenir. ROS 2 Jazzy imajın içinde gelir.

## 1. Raspberry Pi'ye Docker kur

`dpkg --print-architecture` komutu `arm64` döndürmeli. Docker kurulu değilse
[Raspberry Pi OS 64 bit için Docker'ın Debian kurulumunu](https://docs.docker.com/engine/install/debian/)
veya [Ubuntu için Docker kurulumunu](https://docs.docker.com/engine/install/ubuntu/)
takip et. Ardından Pi'de kontrol et:

```bash
sudo systemctl enable --now docker
sudo docker run --rm hello-world
```

## 2. İmajı yükle

Hazır `deos-ros-bridge-rpi-arm64.tar.gz` arşivini ve bu repodaki
`config/params.yaml` dosyasını Raspberry Pi'ye kopyala. Arşivin bulunduğu
dizinden yükle:

```bash
sudo docker load -i deos-ros-bridge-rpi-arm64.tar.gz
sudo docker image inspect deos-ros-bridge:rpi-arm64 --format '{{.Os}}/{{.Architecture}}'
```

Çıktı `linux/arm64` olmalı. Alternatif olarak güncel kaynak dosyalarının
tamamını Pi'ye kopyalayıp repo kökünden imajı doğrudan derleyebilirsin:

```bash
sudo docker buildx build --platform linux/arm64 --load -t deos-ros-bridge:rpi-arm64 .
```

## 3. STM32 ve ROS 2 ayarlarını yap

Pi'de `params.yaml` içindeki `udp_remote_ip` ve `udp_remote_port` değerlerini
STM32'ye göre ayarla. Araç için `wheelbase` ve `max_steering_angle_rad`
değerlerini kontrol et. Pi'den STM32'nin IP adresine ağ rotası olmalı.

Aşağıdaki komutu `params.yaml` dosyasının bulunduğu dizinden çalıştır.
`ROS_DOMAIN_ID` değerini diğer ROS 2 düğümleriyle aynı seç. Host ağı,
Pi'nin ağ arabirimlerini UDP ve ROS 2 keşfi için doğrudan kullanır.

```bash
sudo docker run -d --name deos-ros-bridge \
  --restart unless-stopped \
  --network host \
  -e ROS_DOMAIN_ID=0 \
  --mount "type=bind,source=$PWD/params.yaml,target=/config/params.yaml,readonly" \
  deos-ros-bridge:rpi-arm64 \
  ros2 launch deos_ros_bridge bridge.launch.py params_file:=/config/params.yaml
```

## 4. Çalıştığını kontrol et

```bash
sudo docker logs --tail 50 deos-ros-bridge
sudo docker exec deos-ros-bridge bash /deos_entrypoint.sh ros2 node list
sudo docker exec deos-ros-bridge bash /deos_entrypoint.sh ros2 topic list
```

Düğüm listesinde `/deos_ros_bridge` görünmeli. STM32 telemetri gönderiyorsa
IMU verisini aşağıdaki komutla kontrol edebilirsin:

```bash
sudo docker exec deos-ros-bridge bash /deos_entrypoint.sh ros2 topic echo /sensors/imu/data
```

YAML dosyasında değişiklik yaparsan konteyneri durdurup kaldır:

```bash
sudo docker stop deos-ros-bridge
sudo docker rm deos-ros-bridge
```

Ardından 3. adımdaki çalıştırma komutunu aynı YAML yoluyla tekrar uygula.
