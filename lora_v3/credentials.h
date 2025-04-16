// This EUI must be in little-endian format, so least-significant-byte
// first.
static const u1_t PROGMEM APPEUI[8]={ 0x88, 0x99, 0x99, 0x11, 0x66, 0x00, 0x55, 0x00 };
void os_getArtEui (u1_t* buf) { memcpy_P(buf, APPEUI, 8);}

// This should also be in little endian format, see above.
static const u1_t PROGMEM DEVEUI[8]={ 0x41, 0x29, 0x06, 0xD0, 0x7E, 0xD5, 0xB3, 0x70 };
void os_getDevEui (u1_t* buf) { memcpy_P(buf, DEVEUI, 8);}

// This key should be in big endian format (or, since it is not really a
// number but a block of memory, endianness does not really apply). In
// practice, a key taken from ttnctl can be copied as-is.
static const u1_t PROGMEM APPKEY[16] = { 0x0C, 0x80, 0x56, 0x82, 0xEF, 0xC9, 0x8E, 0x9F, 0x1B, 0x18, 0x85, 0xA4, 0xBD, 0x28, 0x54, 0xA7 };
void os_getDevKey (u1_t* buf) {  memcpy_P(buf, APPKEY, 16);}
