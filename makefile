TARGET = sensorsim.out

OBJS = basestation.o \
	network.o \
	satellite.o \
	sensor.o \
	utilities.o \
	main.o

REBUILDABLES = $(OBJS) $(TARGET)

clean:
	rm -f $(REBUILDABLES)

all: $(TARGET)

$(TARGET): $(OBJS)
	mpicc -g -o $@ $^ -lm -lpthread
	rm -f $(OBJS)

%.o: %.c
	mpicc -g -o $@ -c $< -lm -lpthread

main.o: basestation.h sharedstruct.h utilities.h sensor.h satellite.h
sensor.o: sensor.h
basestation.o: basestation.h
satellite.o: satellite.h
network.o: network.h
utilities.o: utilities.h
