
PROGRAMS = prodcons \
			monitor

LDLIBS = -lpthread

all: $(PROGRAMS)

debug: LDLIBS += -DDEBUG -g
debug: all

clean:
	rm -f $(PROGRAMS)
