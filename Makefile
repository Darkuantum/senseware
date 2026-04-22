FQBN   := esp32:esp32:esp32
CLI    := $(shell which arduino-cli 2>/dev/null || echo "./bin/arduino-cli")
SKETCH := senseware_code/senseware_code.ino
PORT   := $(shell ls /dev/ttyUSB* /dev/ttyACM* 2>/dev/null | head -1)

# huge_app partition (3MB, no OTA) for WiFi+SSE firmware
PART_FLAGS := --build-property build.partitions=huge_app --build-property upload.maximum_size=3145728

.PHONY: compile upload monitor clean

compile:
	$(CLI) compile --fqbn $(FQBN) $(PART_FLAGS) $(SKETCH)

upload: compile
	$(CLI) upload -p $(PORT) --fqbn $(FQBN) $(SKETCH)

monitor:
	$(CLI) monitor -p $(PORT) -c 115200

clean:
	rm -rf senseware_code/build/
