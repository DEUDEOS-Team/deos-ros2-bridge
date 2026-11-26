# Raspberry Pi'de DEOS ROS 2 Bridge

Bu imaj 64 bit Raspberry Pi OS veya 64 bit Ubuntu çalıştıran Raspberry Pi'ler için
`linux/arm64` olarak derlenir. ROS 2 Jazzy imajın içinde gelir.

## Git üzerinden otomatik dağıtım

Güncel dağıtım `deploy/compose.yaml` ve `config/params.yaml` dosyalarını main
commit'iyle birlikte kullanır. STM32 adresi, port ve kalibrasyonları repoda
düzenle. Pi'de her sürümün kendi parametre kopyası bağlanır.
GitHub CI başarılı olduğunda Pi imajı derleyip konteyneri Compose ile yeniler.
Kurulum ve geri dönüş ayrıntıları [ops rehberinde](ops/README.md).

Aşağıdaki arşiv ve docker run adımları elle kurulum içindir.

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

Hazır `deos-ros-bridge-universal-arm64.tar.gz` arşivini ve bu repodaki
`config/params.yaml` dosyasını Raspberry Pi'ye kopyala. Arşivin bulunduğu
dizinden yükle:

```bash
sudo docker load -i deos-ros-bridge-universal-arm64.tar.gz
sudo docker image inspect deos-ros-bridge:universal-arm64 --format '{{.Os}}/{{.Architecture}}'
```

Çıktı `linux/arm64` olmalı. Alternatif olarak güncel kaynak dosyalarının
tamamını Pi'ye kopyalayıp repo kökünden imajı doğrudan derleyebilirsin:

```bash
git submodule update --init vendor/deos_protocol
sudo docker buildx build --platform linux/arm64 --load -t deos-ros-bridge:universal-arm64 .
```

## 3. STM32 ve ROS 2 ayarlarını yap

Pi'nin STM32'ye bağlı Ethernet arayüzüne `192.168.1.20/24` sabit adresini ver.
Main STM32'nin adresi `192.168.1.10` ve `params.yaml` içindeki
`udp_remote_ip` değeri bu adresle eşleşmeli. `udp_remote_port: 5001` değeri
hem Pi'nin yerel UDP portunu hem de STM32'nin hedef UDP portunu belirler.
Main STM32 firmware'i uzak uç olarak `192.168.1.20` adresini ve UDP `5001`
portunu kullanır. Araç için `wheelbase` ve `max_steering_angle_rad` değerlerini
kontrol et.

Textual, Main STM32'nin `192.168.1.10:5000` adresine TCP ile bağlanır. Pi
tarafında da kaynak port `5000` olacaksa Textual istemcisi TCP soketini
bağlantıyı açmadan önce `192.168.1.20:5000` adresine bağlamalıdır.

Aşağıdaki komutu `params.yaml` dosyasının bulunduğu dizinden çalıştır.
`ROS_DOMAIN_ID` değerini diğer ROS 2 düğümleriyle aynı seç. Host ağı,
Pi'nin ağ arabirimlerini UDP ve ROS 2 keşfi için doğrudan kullanır.

```bash
sudo docker run -d --name deos-ros-bridge \
  --restart unless-stopped \
  --network host \
  -e ROS_DOMAIN_ID=0 \
  --mount "type=bind,source=$PWD/params.yaml,target=/config/params.yaml,readonly" \
  deos-ros-bridge:universal-arm64 \
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

## 5. IMU aboneliğini doğrula

Bridge başlangıçta IMU'ya bir SUBSCRIBE isteği gönderir. `imu_interval_ms`
varsayılan olarak 100 ms'dir. Kira süresi sıfırdır ve bridge aboneliği
periyodik olarak yenilemez. ROS 2 IMU topic'i best-effort QoS kullanır.

Güncel Main STM32 test firmware'i IMU aboneliğine örnek veri döndürür.
Kartı bridge'den önce çalıştır ki açılıştaki tek seferlik abonelik isteğini
alabilsin. UDP soketi açıldıktan sonra STM32 kendi durum mesajlarını
önceden bir paket almadan Pi'ye gönderebilir.

ROS 2 tarafında Main STM32 bağlantısını elle sınamak için tek bir PING
gönderilebilir. İlk komutu bir terminalde, ikinciyi başka bir terminalde
çalıştır. PONG, aynı 32 bit kimliği döndürür.

```bash
sudo docker exec -it deos-ros-bridge bash /deos_entrypoint.sh ros2 topic echo /deos/network/pong
sudo docker exec deos-ros-bridge bash /deos_entrypoint.sh ros2 topic pub --once /deos/network/ping std_msgs/msg/UInt32 '{data: 42}'
```

## 6. İsteğe bağlı ROS 2 Mock panelini çalıştır

`deos-ros-mock-jazzy-arm64.tar.gz` dosyasını Pi'ye kopyalayıp ayrı bir
konteyner olarak başlat. `ROS_DOMAIN_ID` değeri bridge ile aynı olmalı.

```bash
sudo docker load -i deos-ros-mock-jazzy-arm64.tar.gz
sudo docker run -d --rm --name deos-ros-mock --network host --init \
  -e ROS_DOMAIN_ID=0 -e FASTDDS_BUILTIN_TRANSPORTS=UDPv4 \
  deos-ros-mock:jazzy-arm64
```

Panel Pi'de `http://127.0.0.1:8765` adresinde açılır. Başka bir bilgisayardan
bakmak için `ssh -L 8765:127.0.0.1:8765 kullanici@192.168.1.20` ile bağlan ve
kendi tarayıcında aynı adresi aç. Test komutlarını kullanmak istersen mock
başlatma komutunun sonuna `--enable-commands` ekle.
