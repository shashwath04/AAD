#include <Arduino.h>        // Core Arduino functionality
#include <SD.h>             // SD card filesystem support
#include <SPI.h>            // SPI interface for SD card
#include "driver/i2s.h"     // ESP32 I2S driver

// ===================== AUDIO SETTINGS =====================
#define SAMPLE_RATE     16000        // 16 kHz sample rate
#define RECORD_SECONDS  10           // Record duration
#define BITS_PER_SAMPLE 16           // 16-bit audio
#define CHANNELS        1            // Mono audio

// Total number of samples to record
#define TOTAL_SAMPLES (SAMPLE_RATE * RECORD_SECONDS)

// ===================== I2S PIN DEFINITIONS =====================
// These pins match the Seeed XIAO ESP32S3 Sense onboard microphone
#define I2S_WS   42   // Word Select (LRCLK)
#define I2S_SD   41   // Serial Data
#define I2S_SCK  40   // Serial Clock (BCLK)

// ===================== SD CARD PIN =====================
#define SD_CS 21      // XIAO ESP32S3 SD card chip select

// File handle for WAV file
File audioFile;

// ===================== WAV HEADER STRUCT =====================
typedef struct {
  char riff[4] = {'R','I','F','F'};      // RIFF header
  uint32_t chunkSize;                   // File size - 8
  char wave[4] = {'W','A','V','E'};      // WAVE format
  char fmt[4]  = {'f','m','t',' '};      // Format chunk
  uint32_t subchunk1Size = 16;           // PCM header size
  uint16_t audioFormat = 1;              // PCM = 1
  uint16_t numChannels = CHANNELS;       // Mono
  uint32_t sampleRate = SAMPLE_RATE;     // Sample rate
  uint32_t byteRate;                     // SampleRate * Channels * Bits/8
  uint16_t blockAlign;                   // Channels * Bits/8
  uint16_t bitsPerSample = BITS_PER_SAMPLE;
  char data[4] = {'d','a','t','a'};      // Data chunk
  uint32_t dataSize;                     // Audio data size
} WAVHeader;

// ===================== SETUP =====================
void setup() {
  Serial.begin(115200);                  // Start serial monitor
  delay(2000);                           // Allow time for USB init

  Serial.println("Initializing SD card...");
  
  // Initialize SD card
  if (!SD.begin(SD_CS)) {
    Serial.println("SD card initialization failed!");
    while (true);
  }
  Serial.println("SD card ready.");

  // Configure I2S peripheral
  i2s_config_t i2s_config = {
    .mode = (i2s_mode_t)(I2S_MODE_MASTER | I2S_MODE_RX), // Master receive
    .sample_rate = SAMPLE_RATE,                          // Sample rate
    .bits_per_sample = I2S_BITS_PER_SAMPLE_16BIT,        // 16-bit
    .channel_format = I2S_CHANNEL_FMT_ONLY_LEFT,         // Mono
    .communication_format = I2S_COMM_FORMAT_I2S,         // I2S standard
    .intr_alloc_flags = ESP_INTR_FLAG_LEVEL1,             // Interrupt level
    .dma_buf_count = 8,                                  // DMA buffers
    .dma_buf_len = 1024,                                 // Buffer length
    .use_apll = false,
    .tx_desc_auto_clear = false,
    .fixed_mclk = 0
  };

  // Assign I2S pins
  i2s_pin_config_t pin_config = {
    .bck_io_num = I2S_SCK,
    .ws_io_num = I2S_WS,
    .data_out_num = -1,      // Not used (RX only)
    .data_in_num = I2S_SD
  };

  // Install and start I2S driver
  i2s_driver_install(I2S_NUM_0, &i2s_config, 0, NULL);
  i2s_set_pin(I2S_NUM_0, &pin_config);
  i2s_zero_dma_buffer(I2S_NUM_0);

  Serial.println("I2S microphone initialized.");

  // Start recording
  recordAudio();
}

// ===================== RECORD FUNCTION =====================
void recordAudio() {
  Serial.println("Recording audio...");

  // Open WAV file on SD card
  audioFile = SD.open("/audio.wav", FILE_WRITE);
  if (!audioFile) {
    Serial.println("Failed to open audio file!");
    return;
  }

  // Prepare WAV header
  WAVHeader header;
  header.byteRate = SAMPLE_RATE * CHANNELS * (BITS_PER_SAMPLE / 8);
  header.blockAlign = CHANNELS * (BITS_PER_SAMPLE / 8);
  header.dataSize = TOTAL_SAMPLES * (BITS_PER_SAMPLE / 8);
  header.chunkSize = 36 + header.dataSize;

  // Write placeholder WAV header
  audioFile.write((uint8_t*)&header, sizeof(WAVHeader));

  // Audio buffer
  int16_t sampleBuffer[1024];
  size_t bytesRead;

  uint32_t samplesRecorded = 0;

  // Record until desired sample count is reached
  while (samplesRecorded < TOTAL_SAMPLES) {
    i2s_read(
      I2S_NUM_0,
      sampleBuffer,
      sizeof(sampleBuffer),
      &bytesRead,
      portMAX_DELAY
    );

    audioFile.write((uint8_t*)sampleBuffer, bytesRead);
    samplesRecorded += bytesRead / 2; // 2 bytes per sample
  }

  // Close file
  audioFile.close();

  Serial.println("Recording complete. Saved as /audio.wav");
}

// ===================== LOOP =====================
void loop() {
  // Nothing to do after recording
}
