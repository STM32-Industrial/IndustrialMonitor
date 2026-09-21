#ifndef CRC32_H
#define CRC32_H

#include <QByteArray>

// 与设备端(BOOT/crc32.c, USER/ota.c)完全一致的 CRC-32 (IEEE 802.3, 多项式0xEDB88320)
quint32 crc32(const QByteArray &data);

#endif // CRC32_H
