GCC = gcc -pthread -Wall -Wextra -Wpedantic -Wshadow -O3
EXECBIN = httpserver
OBJECTS = httpserver.o

all: ${EXECBIN}
${EXECBIN}: ${OBJECTS}
	${GCC} -o $@ $^





%.o: %.c




	${GCC} -c $<

clean:
	rm -f ${EXECBIN} ${OBJECTS}
