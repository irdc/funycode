CFLAGS	= -Wall -g -ggdb
LDFLAGS	= 
SRCS	= funycode.c funyfilt.c
OBJS	= $(SRCS:.c=.o)

all: funyfilt funycode.so

funycode.so: funycode.o
	$(CC) $(LDFLAGS) -shared -o $@ $^

funyfilt: $(OBJS)
	$(CC) $(LDFLAGS) -o $@ $(OBJS)

test: funyfilt
	./funyfilt -e < test.txt | diff -q test.enc -
	./funyfilt < test.enc | diff -q test.txt -
	./funyfilt -e < test.txt | ./funyfilt | diff -q test.txt -

clean:
	rm -f funyfilt funycode.so $(OBJS)
