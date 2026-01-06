#include "ESP_I2S.h" // Library for the PDM Microphone
#include "FS.h"      // File System library
#include "SD.h"      // SD Card library

I2SClass i2s; // Initialize the I2S hardware instance

// --- Hardware Pin Definitions ---
const int SD_CS = 21;           
const int PIN_CLK = 42;         
const int PIN_DATA = 41;        
const int SAMPLE_RATE = 16000; // Standard 16kHz for voice recording

// --- Logic Settings ---
const uint32_t MAX_RECORD_TIME_MS = 30000; // Force new file every 30 seconds
const int SILENCE_TIMEOUT_MS = 5000;       // Wait 5s of silence before saving

// --- AI VAD Thresholds (Tuned to your specific environment) ---
const int ENERGY_START = 85;   // Volume needed to wake up
const int ENERGY_HOLD  = 50;   // Volume needed to stay awake
const int FLUX_START   = 55;   // Texture needed to wake up
const int FLUX_HOLD    = 35;   // Texture needed to stay awake

File audioFile;
bool isRecording = false;
bool isSystemActive = true; 
int fileIndex = 0; 
uint32_t lastSpeechTime = 0;   // Tracks the last time voice was detected
uint32_t recordStartTime = 0;  // Tracks when the current file started
uint32_t lastDebugTime = 0;    // Tracks Serial Monitor updates
float last_sample = 0;         // Used for the DC-Offset filter

// Function to create the WAV container header
void writeWavHeader(File file) {
    byte header[44];
    memset(header, 0, 44);
    memcpy(&header[0], "RIFF", 4);
    memcpy(&header[8], "WAVEfmt ", 8);
    header[16] = 16; header[20] = 1; header[22] = 1;
    uint32_t sRate = SAMPLE_RATE;
    memcpy(&header[24], &sRate, 4);
    uint32_t bRate = sRate * 2;
    memcpy(&header[28], &bRate, 4);
    header[32] = 2; header[34] = 16;
    memcpy(&header[36], "data", 4);
    file.write(header, 44);
}

// Function to update the header with correct file sizes after recording ends
void finalizeWavFile(File file) {
    uint32_t fileSize = file.size();
    uint32_t riffSize = fileSize - 8;
    uint32_t dataSize = fileSize - 44;
    file.seek(4); file.write((byte*)&riffSize, 4);
    file.seek(40); file.write((byte*)&dataSize, 4);
}

void setup() {
    Serial.begin(115200);
    delay(2000); 
    if (!SD.begin(SD_CS)) while(1); // Stop if SD card fails
    
    // Find the next available filename on the SD card
    File root = SD.open("/");
    int maxFound = -1;
    while (File entry = root.openNextFile()) {
        String fileName = String(entry.name());
        if (fileName.indexOf("conv_") != -1 && fileName.endsWith(".wav")) {
            int num = fileName.substring(fileName.indexOf('_') + 1, fileName.lastIndexOf('.')).toInt();
            if (num > maxFound) maxFound = num;
        }
        entry.close();
    }
    fileIndex = maxFound + 1;

    // Start Microphone
    i2s.setPinsPdmRx(PIN_CLK, PIN_DATA);
    i2s.begin(I2S_MODE_PDM_RX, SAMPLE_RATE, I2S_DATA_BIT_WIDTH_16BIT, I2S_SLOT_MODE_MONO);
    Serial.println("--- 30s ROTATION VAD READY ---");
}

void loop() {
    // Listen for Serial commands (stop, start, r for reset)
    if (Serial.available() > 0) {
        String cmd = Serial.readStringUntil('\n'); cmd.trim();
        if (cmd == "stop") { if(isRecording){finalizeWavFile(audioFile); audioFile.close(); isRecording=false;} isSystemActive=false; Serial.println(">>> STANDBY"); }
        else if (cmd == "start") { isSystemActive = true; Serial.println(">>> LISTENING"); }
        else if (cmd == "r") ESP.restart();
    }
    if (!isSystemActive) return;

    // Read audio data from microphone
    int16_t buffer[512];
    size_t bytesRead = i2s.readBytes((char*)buffer, sizeof(buffer));
    if (bytesRead == 0) return;

    float current_energy = 0;
    float spectral_flux = 0; 
    
    // --- DSP Processing Loop ---
    for (int i = 0; i < (bytesRead / 2); i++) {
        float sample = (float)buffer[i];
        // High-pass filter to remove DC Offset "clicks"
        float filtered = sample - (0.98 * last_sample);
        last_sample = sample;
        buffer[i] = (int16_t)filtered;

        current_energy += abs(filtered); // Calculate volume
        // Calculate "Flux" (Change in texture)
        if (i > 0) spectral_flux += abs(buffer[i] - buffer[i-1]);
    }
    current_energy /= (bytesRead / 2);
    spectral_flux /= (bytesRead / 2);

    // Apply Hysteresis logic (Switch thresholds based on recording state)
    int eThresh = isRecording ? ENERGY_HOLD : ENERGY_START;
    int fThresh = isRecording ? FLUX_HOLD : FLUX_START;
    bool isSpeech = (current_energy > eThresh) && (spectral_flux > fThresh);

    // If speech is detected, start recording or refresh silence timer
    if (isSpeech) {
        lastSpeechTime = millis(); 
        if (!isRecording) {
            isRecording = true;
            recordStartTime = millis();
            String fileName = "/conv_" + String(fileIndex++) + ".wav";
            audioFile = SD.open(fileName, FILE_WRITE);
            if (audioFile) { writeWavHeader(audioFile); Serial.println(">>> START RECORDING: " + fileName); }
        }
    }

    // Print debug scores every 1 second
    if (millis() - lastDebugTime > 1000) {
        if (isRecording) {
            float sLeft = (float)(SILENCE_TIMEOUT_MS - (millis() - lastSpeechTime)) / 1000.0;
            Serial.printf("DEBUG [Energy: %.0f | Flux: %.0f] Recording: YES | Silence in: %.1fs\n", current_energy, spectral_flux, sLeft < 0 ? 0 : sLeft);
        } else {
            Serial.printf("DEBUG [Energy: %.0f | Flux: %.0f] Recording: NO\n", current_energy, spectral_flux);
        }
        lastDebugTime = millis();
    }

    // Manage the active recording file
    if (isRecording && audioFile) {
        audioFile.write((uint8_t*)buffer, bytesRead);
        
        uint32_t duration = millis() - recordStartTime;
        bool isSilent = (millis() - lastSpeechTime > SILENCE_TIMEOUT_MS);
        bool timeUp = (duration >= MAX_RECORD_TIME_MS);

        // Close file if silent OR if 30s limit is reached
        if (isSilent || timeUp) {
            finalizeWavFile(audioFile);
            audioFile.close();
            isRecording = false;
            
            if (timeUp) {
                Serial.println(">>> FILE SAVED: 30s LIMIT REACHED");
            } else {
                Serial.println(">>> FILE SAVED BECAUSE OF SILENCE");
            }
        }
    }
}
