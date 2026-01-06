#include "ESP_I2S.h"
#include "FS.h"
#include "SD.h"

// Hardware Instances
I2SClass i2s;

// --- Configuration ---
const int SAMPLE_RATE = 16000;
const size_t MAX_FILE_SIZE = 5 * 1024 * 1024; // 5MB Limit
const int SILENCE_TIMEOUT_MS = 2000;          // Wait 2s before stopping
const float NOISE_SMOOTHING = 0.05;           // Speed of noise adaptation

// --- State Variables ---
File audioFile;
bool isRecording = false;
int fileIndex = 0;
uint32_t currentFileBytes = 0;
uint32_t lastSpeechTime = 0;
float noiseFloor = 500.0; // Dynamic baseline

void startNewFile() {
    if (audioFile) audioFile.close();
    String fileName = "/conv_" + String(fileIndex++) + ".pcm";
    audioFile = SD.open(fileName, FILE_WRITE);
    currentFileBytes = 0;
    Serial.println(">>> STARTING RECORD: " + fileName);
}

void setup() {
    Serial.begin(115200);
    while (!Serial) delay(10); // Wait for Serial [cite: 3]

    // Initialize I2S for the Internal PDM Mic [cite: 4, 5]
    i2s.setPinsPdmRx(42, 41); 
    if (!i2s.begin(I2S_MODE_PDM_RX, SAMPLE_RATE, I2S_DATA_BIT_WIDTH_16BIT, I2S_SLOT_MODE_MONO)) {
        Serial.println("I2S Init Failed!");
        while (1); 
    }

    // Initialize SD Card [cite: 7]
    if (!SD.begin(21)) {
        Serial.println("SD Card Failed!");
        while (1); 
    }
    Serial.println("System Active: Listening for conversation...");
}

void loop() {
    int16_t buffer[512];
    // Constant Listening: Read current audio chunk
    size_t bytesRead = i2s.readBytes((char*)buffer, sizeof(buffer));
    int samplesCount = bytesRead / 2;

    if (samplesCount == 0) return;

    // Intelligence: Calculate Volume Energy
    float currentEnergy = 0;
    for (int i = 0; i < samplesCount; i++) {
        currentEnergy += abs(buffer[i]);
    }
    currentEnergy /= samplesCount;

    // Voice Activity Detection (VAD) logic
    // Triggers if volume is 2.5x higher than the "learned" background noise
    bool speechDetected = (currentEnergy > (noiseFloor * 2.5) && currentEnergy > 200);

    // Update noise floor if it's just background hum
    if (!speechDetected) {
        noiseFloor = (noiseFloor * (1.0 - NOISE_SMOOTHING)) + (currentEnergy * NOISE_SMOOTHING);
    }

    if (speechDetected) {
        lastSpeechTime = millis();
        if (!isRecording) {
            isRecording = true;
            startNewFile();
        }
    }

    // Recording Logic
    if (isRecording) {
        // Write audio data to SD [cite: 12]
        audioFile.write((uint8_t*)buffer, bytesRead);
        currentFileBytes += bytesRead;

        // Management: Limit file size to 5MB
        if (currentFileBytes >= MAX_FILE_SIZE) {
            startNewFile();
        }

        // Management: Stop if silence persists for 2 seconds
        if (millis() - lastSpeechTime > SILENCE_TIMEOUT_MS) {
            Serial.println("<<< SILENCE DETECTED: Saving file.");
            audioFile.close();
            isRecording = false;
        }
    }
}
