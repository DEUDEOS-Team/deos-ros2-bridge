# Raspberry Pi modları ve bridge güncelleyicisi

## Developer ve prod yönetimi

Pi'deki geliştirme otomasyonları `/etc/deos/system.yaml` dosyasından yönetilir.
`mode: developer` seçildiğinde bridge kontrolü ve Wi-Fi görevi kendi `enabled`
ve `interval_seconds` ayarlarını kullanır. Periyot, görev tamamlandıktan sonra
başlar. İlk görevler servis açıldığında çalışır. `mode: prod` otomasyonları
durdurur ve kayıtlı Wi-Fi profillerinin otomatik bağlantısını kapatır. Aktif
bağlantı korunur. Developer'a dönüşte özgün autoconnect değerleri geri yüklenir.

Mevcut güncelleyici kurulu Pi'de kaynak repo kökünden kurulum yapılır.
Python 3 ve `python3-yaml` gerekir.

```bash
sudo bash ops/install-mode.sh
sudo nano /etc/deos/system.yaml
sudo deos-mode apply
deos-mode status
journalctl -u deos-developer.service -f
```

`apply` tüm dosyayı doğrular ve çalışan servise uygular. Hatalı ayar durumunda
önceki ayarlar çalışır. Etkin ayarlar `/var/lib/deos/applied.json` içinde
saklanır ve açılışta yüklenir. Dosyada yapılmış fakat uygulanmamış değişiklikler
yeniden başlatmada etkinleşmez. Mod geçişi devam eden Wi-Fi işleminin bitmesini
bekleyebilir. Başlamış bridge güncellemesi prod'a geçildiğinde tamamlanır.

`deos-developer.service` görevleri bağımsız iş parçacıklarında yürütür.
Bridge görevi mevcut `deos-bridge-update.service` birimini tetikler. Kurulum
eski `deos-bridge-update.timer` birimini kapatır. Güncelleme kaydı bu nedenle
hem yönetici servisinde hem güncelleyici servisinde bulunur.

Wi-Fi bağlıysa veya bağlanıyorsa ağ taraması ve değiştirme yapılmaz. Bağlantı
yoksa kayıtlı ve görünür profiller öncelik, sinyal gücü, UUID sırasıyla denenir.
Üç başarısız döngüden sonra yalnız seçili arayüzün NetworkManager yönetimi
kapatılıp yeniden açılır. Kurtarma arası varsayılan olarak 300 saniyedir.
Wi-Fi görevini `enabled: false` yapmak ek kurtarma/tarama görevini durdurur.
NetworkManager'ın profil autoconnect ayarlarını prod modu yönetir.

Kontrollü testler aşağıdaki komutla çalıştırılır.

```bash
python3 -m unittest discover -s ops/tests -v
```

Yeni bir otomasyon eklenirken YAML doğrulamasına ve görev listesine eklenir.
Görevlerin etkinliği ve periyotları aynı dosyada tutulur.

## Bridge dağıtımı

Bridge kodu, Dockerfile, `deploy/compose.yaml` ve `config/params.yaml` aynı
main commit'inden dağıtılır. STM32 adresi, port, kalibrasyonlar ve tüm bridge
parametreleri repoda tutulur. Konteyner ayarları Compose üzerinden yönetilir.

Pi'deki sabit güncelleyici `/opt/deos/bridge-updater` altındadır. Yeni main
commit'i için GitHub'daki `ci.yml` workflow'unun aynı commit'e ait main push
çalışmasının başarıyla tamamlanması gerekir. CI bekliyorsa sonraki kontrolde
tekrar bakılır. Başarısız CI veya API erişim hatası dağıtımı engeller.

Onaylanan kaynaklar ve alt modüller `releases/COMMIT` altında ayrı bir kopyaya
çıkarılır. Derleme ve konteyner başlatma bu sürümün Compose tanımından yapılır.
Parametre dosyası aynı sürüm dizininden salt okunur bağlanır. Böylece ileride
source klasörü güncellense de çalışan konteynerin parametreleri korunur.

Yeni konteyner sağlıklı başlatılamazsa önceki konteyner, kendi imajı ve sürüm
parametreleriyle geri getirilir. Başarılı commit `deployed-revision` dosyasına
yazılır. Önceki konteyner `deos-ros-bridge-previous` adıyla tutulur.

İlk kurulum veya güncelleyici kodu değişikliğinde Pi'de repo kökünden çalıştır.

```bash
sudo bash ops/install-updater.sh
```

Bu kurulum, bridge güncelleme servisini kurar. Tetikleme mevcut sistem
zamanlayıcısı tarafından yapılır. Developer/prod yöneticisinin ayarları ayrı
`/etc/deos/system.yaml` dosyasından yönetilir.

Ubuntu'da aynı konteyner tanımını kullanmak için repo kökünden çalıştır.

```bash
docker compose --env-file /dev/null -f deploy/compose.yaml up -d --build
```

Pi otomasyonu imajı `main-COMMIT` etiketiyle derler. ROS_DOMAIN_ID, DDS, ağ,
restart politikası ve başlatma komutu Compose dosyasından gelir. Parametreleri
repodaki config dosyasında değiştirip main'e gönder. Pi'deki eski
`/home/deos/deos_ros_bridge/params.yaml` aktif dağıtımda kullanılmaz.

```bash
journalctl -u deos-bridge-update.service -f
docker inspect deos-ros-bridge --format '{{.State.Health.Status}}'
```

Healthcheck bridge sürecini denetler. STM32 hattını PING/PONG veya telemetriyle
ayrıca kontrol et. Yeni sürüm ve kalibrasyon dosyasını dağıtımdan önce birlikte
doğrula. Eski sürüm dizinleri geri dönüş için saklanır.
