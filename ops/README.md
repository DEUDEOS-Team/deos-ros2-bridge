# Raspberry Pi bridge güncelleyicisi

`deos-bridge-update.timer`, açılıştan 90 saniye sonra ve her kontrol
tamamlandıktan bir dakika sonra GitHub `DEUDEOS-Team/deos-ros2-bridge`
reposunun `main` dalını kontrol eder. Kod ve alt modüller Pi'deki
`/home/deos/deos-bridge-updater/source` dizinine çekilir. Yeni commit için
C++ birim testleri çalıştırılır ve Docker imajı Pi üzerinde derlenir. Başarılı başlangıç kontrolünden sonra
commit `/home/deos/deos-bridge-updater/deployed-revision` dosyasına yazılır.

Çalışma parametreleri `/home/deos/deos_ros_bridge/params.yaml` dosyasından
okunur. Konteyner host ağı, `ROS_DOMAIN_ID=0` ve `unless-stopped` yeniden
başlatma politikası ile çalışır. Önceki konteyner durdurulmuş halde
`deos-ros-bridge-previous` adıyla tutulur. Yeni sürüm başlatılamazsa önceki
konteyner geri getirilir. Kontrol ROS bridge sürecinin açıldığını doğrular.
STM32'nin erişilebilir olması dağıtım kontrolünün kapsamı dışındadır.

Pi'ye üç dosyayı `/home/deos/deos-bridge-updater` altına kopyaladıktan sonra
unit dosyalarını kur.

```bash
sudo install -m 644 /home/deos/deos-bridge-updater/deos-bridge-update.service /etc/systemd/system/
sudo install -m 644 /home/deos/deos-bridge-updater/deos-bridge-update.timer /etc/systemd/system/
sudo systemctl daemon-reload
sudo systemctl enable --now deos-bridge-update.timer
```

İlk kontrol mevcut `main` sürümünü derler ve kurar. Etkinleştirmeden önce
`main` dalının dağıtmak istediğin kodu içerdiğini doğrula.

```bash
sudo systemctl start deos-bridge-update.service
journalctl -u deos-bridge-update.service -f
systemctl list-timers deos-bridge-update.timer
```

Otomatik kontrolleri durdurmak için aşağıdaki komutu kullan.

```bash
sudo systemctl disable --now deos-bridge-update.timer
```

İmajlar `deos-ros-bridge:main-COMMIT` etiketiyle saklanır. Otomatik imaj
silme yapılmaz. Başarısız güncelleme sonraki kontrolde yeniden denenir.

GitHub Actions, `main` push ve pull request'lerinde birim testlerini ve
Docker derlemesini çalıştırır. Pi güncelleyicisi aynı birim testlerini
dağıtımdan önce bağımsız olarak tekrar çalıştırır.
