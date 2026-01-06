#include "ESP_I2S.h"
#include "FS.h"
#include "SD.h"

// Hardware Instances
I2SClass i2s;

// Constants for Intelligence
const int SAMPLE_RATE = 16000;
const size_t MAX_FILE_SIZE = 5 * 1024 * 1024; // 5MB limit
const int SILENCE_TIMEOUT_MS = 2000;          // Keep recording for 2s of silence
const float NOISE_SMOOTHING = 0.05;           // How fast it learns background noise

// Global State
File audioFile;
bool isRecording = false;
int fileIndex = 0;
uint32_t currentFileBytes = 0;
uint32_t lastSpeechTime = 0;
float noiseFloor = 500; // Initial guess, will self-adjust

void startNewFile() {
    if (audioFile) audioFile.close();
    String fileName = "/conv_" + String(fileIndex++) + ".pcm";
    audioFile = SD.open(fileName, FILE_WRITE);
    currentFileBytes = 0;
    Serial.println(">>> Starting conversation record: " + fileName);
}

void setup() {
    Serial.begin(115200);
    while (!Serial) delay(10); [cite: 3]

    // Initialize PDM Internal Mic [cite: 4, 5]
    i2s.setPinsPdmRx(42, 41); 
    if (!i2s.begin(I2S_MODE_PDM_RX, SAMPLE_RATE, I2S_DATA_BIT_WIDTH_16BIT, I2S_SLOT_MODE_MONO)) {
        Serial.println("Failed to initialize I2S!");
        while (1); [cite: 6]
    }

    // Initialize SD Card [cite: 7]
    if (!SD.begin(21)) {
        Serial.println("Failed to mount SD Card!");
        while (1); [cite: 8]
    }
    Serial.println("System Ready. Listening...");
}

void loop() {
    int16_t buffer[512];
    size_t bytesRead = i2s.readBytes((char*)buffer, sizeof(buffer));
    int samplesCount = bytesRead / 2;

    if (samplesCount == 0) return;

    // Calculate Energy (Volume) of this chunk
    float currentEnergy = 0;
    for (int i = 0; i < samplesCount; i++) {
        currentEnergy += abs(buffer[i]);
    }
    currentEnergy /= samplesCount;

    // Intelligence: Differentiate speech from noise
    // We trigger if current energy is 2.5x higher than the learned noise floor
    bool speechDetected = (currentEnergy > (noiseFloor * 2.5) && currentEnergy > 200);

    // Update the noise floor (learning the background noise)
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

    // Handle ongoing recording
    if (isRecording) {
        audioFile.write((uint8_t*)buffer, bytesRead);
        currentFileBytes += bytesRead;

        // Condition 1: File size limit reached
        if (currentFileBytes >= MAX_FILE_SIZE) {
            startNewFile();
        }

        // Condition 2: Conversation ended (Silence timeout)
        if (millis() - lastSpeechTime > SILENCE_TIMEOUT_MS) {
            Serial.println("<<< Conversation ended. Saving file.");
            audioFile.close();
            isRecording = false;
        }
    }
}
