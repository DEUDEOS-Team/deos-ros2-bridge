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

## Mevcut bridge dağıtım servisi

`deos-bridge-update.service`, GitHub `DEUDEOS-Team/deos-ros2-bridge` reposunun
`main` dalını `/opt/deos/bridge-updater/source` altına çeker. Yeni commit
için C++ testlerini çalıştırır ve Docker imajını Pi üzerinde derler. Parametreler
`/home/deos/deos_ros_bridge/params.yaml` dosyasından alınır. Başarılı başlangıç
sonrası commit `deployed-revision` dosyasına kaydedilir. Önceki konteyner
`deos-ros-bridge-previous` adıyla saklanır ve başlangıç hatasında geri getirilir.

Dağıtım kontrolü ROS bridge sürecinin açıldığını doğrular. STM32 erişimi için
ayrıca PING/PONG veya telemetri kontrolü gerekir. İmajlar
`deos-ros-bridge:main-COMMIT` etiketiyle saklanır.

```bash
journalctl -u deos-bridge-update.service -f
```

Eski bağımsız timer, mod yöneticisi kullanıldığında kapalı tutulmalıdır.
GitHub Actions `main` push ve pull request'lerinde C++ testlerini, mod
yöneticisi testlerini ve Docker derlemesini çalıştırır.
