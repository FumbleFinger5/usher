TARGET = usher
COMP = g++

# Use 'make r=1' for release version
ifdef r
BUG =
else
BUG = -ggdb -DBUG=YES
endif

CFLAGS = `pkg-config --cflags gtk+-3.0` \
 -I/usr/include/gtkmm-3.0 -I/usr/include/glibmm-2.4 -I/usr/lib/x86_64-linux-gnu/glibmm-2.4/include \
 -I/usr/include/sigc++-2.0 -I/usr/lib/x86_64-linux-gnu/sigc++-2.0/include \
 -I../plib -I. $(BUG) -fPIC
LFLAGS = -L../plib `pkg-config --libs gtk+-3.0` -rdynamic -lplib -lcurl -ljson-c -licuuc -licudata -licui18n -llz4

OBJFILES = $(TARGET).o $(TARGET)c.o

$(TARGET): $(OBJFILES)
	@$(COMP) -o $@ $(OBJFILES) $(LFLAGS)
	@echo $@ Linked OK

%.o: %.cpp
	@$(COMP) $(CFLAGS) -c -o $@ $<
	@echo $@ Compiled OK

clean:
	rm -f *.o *.so *~ main $(TARGET)

