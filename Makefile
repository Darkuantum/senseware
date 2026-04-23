FQBN   := esp32:esp32:dfrobot_firebeetle2_esp32e
CLI    := bin/arduino-cli
SKETCH := senseware_code/senseware_code.ino
PORT   := $(shell ls /dev/ttyUSB* /dev/ttyACM* 2>/dev/null | head -1)

ARDUINO_DATA_DIR   := $(shell pwd)/.arduino
ARDUINO_CONFIG_FILE := $(shell pwd)/arduino-cli.yaml

export ARDUINO_DATA_DIR
export ARDUINO_CONFIG_FILE

CLI_FLAGS := --config-file $(ARDUINO_CONFIG_FILE)

# huge_app partition (3MB, no OTA) for WiFi+SSE firmware
PART_FLAGS := --build-property build.partitions=huge_app --build-property upload.maximum_size=3145728

.PHONY: compile upload monitor clean

compile:
	$(CLI) $(CLI_FLAGS) compile --fqbn $(FQBN) $(PART_FLAGS) $(SKETCH)

upload: compile
	$(CLI) $(CLI_FLAGS) upload -p $(PORT) --fqbn $(FQBN) $(SKETCH)

monitor:
	$(CLI) $(CLI_FLAGS) monitor -p $(PORT) -c 115200

clean:
	rm -rf senseware_code/build/
