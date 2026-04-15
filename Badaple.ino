#include <SPI.h>
#include <Adafruit_GFX.h>
#include <Adafruit_ST7735.h>
#include <Feather.hpp>
#include <LittleFS.h>
#include <cstring>
#include <stdint.h>

#define FRAME_SIZE 2560
#define DWidth 160
#define DHeight 128
#define TILE_SIZE 16

#define TFT_SCLK D13
#define TFT_MOSI D11
#define TFT_CS   D10
#define TFT_DC    D9
#define TFT_RST   D8

Feather feather([]() { return uint64_t(millis()); });
Adafruit_ST7735 tft(TFT_CS, TFT_DC, TFT_RST);

File videoFile;

uint8_t framebuffer[FRAME_SIZE];
uint8_t tileBuf[(TILE_SIZE * TILE_SIZE) / 8];
uint16_t lineBuf[DWidth];

uint32_t currentFrame = 0;
uint32_t totalFrames = 0;
uint16_t videoFPS = 12;
bool videoReady = false;

volatile bool frameDecoded = false;
volatile bool frameDrawing = false;

struct BLVHeader {
  char magic[4];
  uint16_t width;
  uint16_t height;
  uint16_t fps;
  uint32_t frame_count;
  uint16_t keyframe_interval;
  uint16_t tile_size;
  uint16_t flags;
};

static bool file_read(void* dst, size_t n) {
  return videoFile.read((uint8_t*)dst, n) == (int)n;
}

static uint16_t read_u16() {
  uint8_t b[2];
  if (!file_read(b, 2)) return 0;
  return (uint16_t)b[0] | ((uint16_t)b[1] << 8);
}

static uint32_t read_u32() {
  uint8_t b[4];
  if (!file_read(b, 4)) return 0;
  return (uint32_t)b[0]
       | ((uint32_t)b[1] << 8)
       | ((uint32_t)b[2] << 16)
       | ((uint32_t)b[3] << 24);
}

static bool readHeader(BLVHeader& h) {
  if (!file_read(h.magic, 4)) return false;
  h.width = read_u16();
  h.height = read_u16();
  h.fps = read_u16();
  h.frame_count = read_u32();
  h.keyframe_interval = read_u16();
  h.tile_size = read_u16();
  h.flags = read_u16();

  if (memcmp(h.magic, "BLV1", 4) != 0) return false;
  if (h.width != DWidth || h.height != DHeight) return false;
  if (h.tile_size != TILE_SIZE) return false;
  return true;
}

static bool rleDecode(uint8_t* out, uint32_t outSize, uint32_t payloadSize) {
  uint32_t written = 0;
  uint32_t consumed = 0;

  while (consumed < payloadSize && written < outSize) {
    uint8_t tag = 0;
    uint8_t count = 0;

    if (!file_read(&tag, 1)) return false;
    if (!file_read(&count, 1)) return false;
    consumed += 2;

    if (count == 0) return false;

    if (tag == 0) {
      if (written + count > outSize) return false;
      if (consumed + count > payloadSize) return false;
      if (!file_read(out + written, count)) return false;
      written += count;
      consumed += count;
    } else if (tag == 1) {
      uint8_t v = 0;
      if (!file_read(&v, 1)) return false;
      consumed += 1;
      if (written + count > outSize) return false;
      memset(out + written, v, count);
      written += count;
    } else {
      return false;
    }
  }

  return written == outSize && consumed == payloadSize;
}

static inline uint8_t getBit(const uint8_t* buf, int x, int y) {
  uint32_t idx = (uint32_t)y * DWidth + x;
  return (buf[idx >> 3] >> (7 - (idx & 7))) & 1;
}

static inline void xorBit(uint8_t* buf, int x, int y, uint8_t v) {
  if (!v) return;
  uint32_t idx = (uint32_t)y * DWidth + x;
  buf[idx >> 3] ^= (1 << (7 - (idx & 7)));
}

static void applyTile(uint8_t tx, uint8_t ty) {
  const int x0 = tx * TILE_SIZE;
  const int y0 = ty * TILE_SIZE;

  int p = 0;
  for (int y = 0; y < TILE_SIZE; y++) {
    for (int x = 0; x < TILE_SIZE; x++) {
      uint8_t bit = (tileBuf[p >> 3] >> (7 - (p & 7))) & 1;
      xorBit(framebuffer, x0 + x, y0 + y, bit);
      p++;
    }
  }
}

static bool decodeFrame() {
  uint8_t type = 0;
  if (!file_read(&type, 1)) return false;

  uint32_t size = read_u32();

  if (type == 0) {
    return rleDecode(framebuffer, FRAME_SIZE, size);
  }

  if (type != 1) return false;

  uint16_t count = read_u16();

  for (uint16_t i = 0; i < count; i++) {
    uint8_t tx = 0;
    uint8_t ty = 0;

    if (!file_read(&tx, 1)) return false;
    if (!file_read(&ty, 1)) return false;

    uint16_t ts = read_u16();
    memset(tileBuf, 0, sizeof(tileBuf));

    if (!rleDecode(tileBuf, sizeof(tileBuf), ts)) return false;
    applyTile(tx, ty);
  }

  currentFrame++;
  return true;
}

static void drawFrame() {
  tft.startWrite();

  for (int y = 0; y < DHeight; y++) {
    for (int x = 0; x < DWidth; x++) {
      lineBuf[x] = getBit(framebuffer, x, y) ? ST77XX_WHITE : ST77XX_BLACK;
    }
    tft.setAddrWindow(0, y, DWidth, 1);
    tft.writePixels(lineBuf, DWidth);
  }

  tft.endWrite();
}

static void restartVideo() {
  videoFile.seek(sizeof(BLVHeader));
  currentFrame = 0;
  memset(framebuffer, 0, sizeof(framebuffer));
  frameDecoded = false;
  frameDrawing = false;
}

static void decodeTask() {
  if (!videoReady) return;
  if (frameDecoded) return;
  if (frameDrawing) return;

  if (!decodeFrame()) {
    restartVideo();
    if (!decodeFrame()) return;
  }

  frameDecoded = true;
}

static void drawTask() {
  if (!videoReady) return;
  if (!frameDecoded) return;
  if (frameDrawing) return;

  frameDrawing = true;
  drawFrame();
  frameDrawing = false;
  frameDecoded = false;
}

void setup() {
  SPI.begin(TFT_SCLK, -1, TFT_MOSI, TFT_CS);

  tft.initR(INITR_BLACKTAB);
  tft.setRotation(3);
  tft.fillScreen(ST77XX_BLACK);
  tft.setTextColor(ST77XX_WHITE);
  tft.setTextSize(1);

  if (!LittleFS.begin()) {
    tft.setCursor(0, 0);
    tft.println("FS fail");
    return;
  }

  videoFile = LittleFS.open("/video.blv", "r");
  if (!videoFile) {
    tft.setCursor(0, 0);
    tft.println("open fail");
    return;
  }

  BLVHeader h;
  if (!readHeader(h)) {
    tft.setCursor(0, 0);
    tft.println("header fail");
    return;
  }

  videoFPS = h.fps;
  totalFrames = h.frame_count;
  memset(framebuffer, 0, sizeof(framebuffer));
  videoReady = true;

  const uint32_t interval = 1000 / videoFPS;
  const uint64_t now = feather.now_ms();

  feather.PeriodicTask(
    []() { decodeTask(); },
    now,
    interval,
    1
  );

  feather.PeriodicTask(
    []() { drawTask(); },
    now,
    interval,
    2
  );
}

void loop() {
  feather.step();
}