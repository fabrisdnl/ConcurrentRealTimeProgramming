
PROGRAMS = client \
			server

LDLIBS = -lpthread

all: $(PROGRAMS)

clean:
	rm -f $(PROGRAMS)
