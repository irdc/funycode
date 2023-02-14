CFLAGS	= -Wall
LDFLAGS	= -g -ggdb
SRCS	= funycode.c funyfilt.c
OBJS	= $(SRCS:.c=.o)

funyfilt: $(OBJS)
	$(CC) $(LDFLAGS) -o $@ $(OBJS)

test: funyfilt
	./funyfilt -e < test.txt | diff -q test.enc -
	./funyfilt < test.enc | diff -q test.txt -
	./funyfilt -e < test.txt | ./funyfilt | diff -q test.txt -

clean:
	rm -f funyfilt $(OBJS)
