#include "crc32.h"

static quint32 s_table[256];
static bool    s_ready = false;

static void crc32_init_table()
{
    for (quint32 i = 0; i < 256; i++) {
        quint32 c = i;
        for (int k = 0; k < 8; k++) {
            c = (c & 1) ? (0xEDB88320UL ^ (c >> 1)) : (c >> 1);
        }
        s_table[i] = c;
    }
    s_ready = true;
}

quint32 crc32(const QByteArray &data)
{
    if (!s_ready)
        crc32_init_table();

    quint32 crc = 0xFFFFFFFFUL;
    for (int i = 0; i < data.size(); i++) {
        crc = s_table[(crc ^ static_cast<quint8>(data.at(i))) & 0xFF] ^ (crc >> 8);
    }
    return crc ^ 0xFFFFFFFFUL;
}
