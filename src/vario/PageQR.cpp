#include "PageQR.h"

#include "display.h"
#include "qrcode.h"

// level of detail https://github.com/ricmoo/qrcode/
// If set to 3, will render bigger
#define QR_ECC ECC_LOW
#define QR_SIZE 2

#define X_OFFSET 3
#define Y_OFFSET 40

void PageQR::draw_extra() {
    auto qr_version = 7;

    QRCode qrcode;
    uint8_t qrcodeData[qrcode_getBufferSize(qr_version)];

    qrcode_initText(&qrcode, qrcodeData, qr_version, QR_ECC, url.c_str());
    for (uint8_t x = 0; x < qrcode.size; x++) {
        for (uint8_t y = 0; y < qrcode.size; y++) {
            if (qrcode_getModule(&qrcode, x, y)) {
                u8g2.drawBox(X_OFFSET + x * QR_SIZE, Y_OFFSET + y * QR_SIZE,
                             QR_SIZE, QR_SIZE);
            }
        }
    }
}