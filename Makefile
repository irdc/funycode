CFLAGS	= -Wall
LDFLAGS	= -g -ggdb
SRCS	= funycode.c funyfilt.c
OBJS	= $(SRCS:.c=.o)

funyfilt: $(OBJS)
	$(CC) $(LDFLAGS) -o $@ $(OBJS)

test: funyfilt
	./funyfilt -e < test.txt | ./funyfilt | diff -q test.txt -

clean:
	rm -f funyfilt $(OBJS)
