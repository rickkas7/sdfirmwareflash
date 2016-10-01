#include "Particle.h"
#include "system_update.h"

#include "SdFat/SdFat.h"

STARTUP(System.enableFeature(FEATURE_RETAINED_MEMORY));

// This is set up to use primary SPI with DMA
// SD Adapter   Electron
// SS (CS)      A2
// SCK          A3
// MISO (DO)    A4
// MOSI (DI)    A5
SdFat sd;
const uint8_t chipSelect = SS;

void checkCardForUpdates();
void updateFromFile(File &firmwareFile);

const unsigned long CARD_CHECK_PERIOD = 30000;
unsigned long lastCardCheck = 5000 - CARD_CHECK_PERIOD;
retained size_t loadStage = 0;

const size_t LOAD_STAGES = 4;
const char *loadFilenames[LOAD_STAGES] = {
	"system1.bin",
	"system2.bin",
	"system3.bin",
	"firmware.bin"
};

void setup() {
	Serial.begin(9600);
}

void loop() {
	if (millis() - lastCardCheck >= CARD_CHECK_PERIOD) {
		lastCardCheck = millis();
		checkCardForUpdates();
	}
}


void checkCardForUpdates() {
	if (!sd.begin(chipSelect, SPI_HALF_SPEED)) {
		Serial.println("no card");
		loadStage = 0;
		return;
	}

	if (loadStage >= LOAD_STAGES) {
		Serial.println("skipped (already loaded)");
		return;
	}

	while(loadStage < LOAD_STAGES) {
		File firmwareFile;

		if (firmwareFile.open(loadFilenames[loadStage], O_READ)) {
			Serial.printlnf("loading %s", loadFilenames[loadStage]);
			updateFromFile(firmwareFile);
			firmwareFile.close();
			return;
		}

		Serial.printlnf("no file %s", loadFilenames[loadStage]);
		loadStage++;
	}

}

void updateFromFile(File &firmwareFile) {
	size_t fileSize = firmwareFile.fileSize();

	Serial.printlnf("has an image file length=%lu", fileSize);

	FileTransfer::Descriptor file;

	file.file_length = fileSize;
	file.file_address = 0; // Automatically set to HAL_OTA_FlashAddress if store is FIRMWARE
	file.chunk_address = 0;
	file.chunk_size = 0; // use default
	file.store = FileTransfer::Store::FIRMWARE;


	int result = Spark_Prepare_For_Firmware_Update(file, 0, NULL);
	if (result != 0) {
		Serial.printlnf("prepare failed %d", result);
		return;
	}

	Serial.printlnf("chunk_size=%d file_address=0x%x", file.chunk_size, file.file_address);

	// Typically 512 bytes
	uint8_t *buf = (uint8_t *) malloc(file.chunk_size);
	if (result != 0) {
		Serial.println("failed to allocate buffer");
		return;
	}

	// Note that Spark_Prepare_For_Firmware_Update sets file.file_address so it's not really zero here
	// even though it's what we initialize it to above!
	file.chunk_address = file.file_address;

	size_t offset = 0;
	bool succeeded = true;

	while(offset < fileSize) {
		if (file.chunk_size > (fileSize - offset)) {
			file.chunk_size = (fileSize - offset);
		}
		if (firmwareFile.read(buf, file.chunk_size) < 0) {
			Serial.println("read failed");
			succeeded = false;
			break;
		}

		Serial.printlnf("chunk_address=0x%x chunk_size=%d", file.chunk_address, file.chunk_size);
		result = Spark_Save_Firmware_Chunk(file, buf, NULL);
		if (result != 0) {
			Serial.printlnf("save chunk failed %d", result);
			succeeded = false;
			break;
		}

		file.chunk_address += file.chunk_size;
		offset += file.chunk_size;
	}

	free(buf);

	result = Spark_Finish_Firmware_Update(file, succeeded, NULL);
	if (result != 0) {
		Serial.printlnf("finish failed %d", result);
		return;
	}

	if (succeeded) {
		loadStage++;
	}

	Serial.printlnf("update complete");
}
