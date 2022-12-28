
PROGRAMS = client \
           server

LDLIBS = -lpthread

all: $(PROGRAMS)

debug: LDLIBS += -DDEBUG -g
debug: all

clean:
	rm -f $(PROGRAMS)
