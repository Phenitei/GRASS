# Dirs
SRCDIR   = ./src
BINDIR   = ./bin
INCLUDES = ./include
OBJDIR   = ./objects

# Compiler
CC=gcc

# Stuff to enable stack smashing and such
CFLAGS+=-fno-stack-protector -z execstack -I $(INCLUDES)/

# Libraries to link against
LDLIBS += -lpthread

all: $(BINDIR)/client $(BINDIR)/server

$(OBJDIR)/grass.o: $(SRCDIR)/grass.c
	$(CC) -c -o $@ $< $(CFLAGS) $(LDLIBS)

$(BINDIR)/client: $(SRCDIR)/client.c $(OBJDIR)/grass.o
	$(CC) -o $@ $? $(CFLAGS) $(LDLIBS)

$(BINDIR)/server: $(SRCDIR)/server.c $(OBJDIR)/grass.o
	$(CC) -o $@ $? $(CFLAGS) $(LDLIBS)

.PHONY: clean
clean:
	rm -f $(BINDIR)/* $(OBJDIR)/*
