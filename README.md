# DEOS ROS 2 Bridge

Amaç:
ROS 2 ile DEOS gömülü kontrol ağı arasında bridge görevi gören C++ tabanlı ROS 2 node geliştirmek.

Temel roller:
- ROS 2 topic'lerine subscribe olmak
- Araç durum ve sensör verilerini ROS 2'ye publish etmek
- DEOS ağı üzerinde Virtual Node olarak davranmak
- Node ID: 0x04

İlk aşama:
- Proje iskeleti
- ROS 2 publisher/subscriber yapısı
- Daha sonra UDP ve DEOS protocol entegrasyonu

Not:
Derleme ve gerçek test Raspberry Pi üzerindeki ROS 2 ortamında yapılacaktır.

# deos-ros2-bridge
