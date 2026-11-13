CC      ?= gcc
CFLAGS  ?= -O2 -Wall -Wextra
BOSCH   := external/BMI08x_SensorAPI
SRC     := src/main.c src/rpi_platform.c src/udp_stream.c $(BOSCH)/bmi08a.c $(BOSCH)/bmi08g.c $(BOSCH)/bmi08xa.c
INC     := -Isrc -I$(BOSCH)
LIBS    := -lgpiod -lm

bmi088_test: $(SRC)
	$(CC) $(CFLAGS) $(INC) -o $@ $(SRC) $(LIBS)

clean:
	rm -f bmi088_test

.PHONY: clean
