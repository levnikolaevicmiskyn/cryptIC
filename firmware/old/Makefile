
exe: test_sha256.c sha256.o
	gcc -Wall test_sha256.c -o test_sha256 sha256.o 

sha256.o: ./sha256/src/sha256.c ./sha256/src/sha256.h
	gcc -Wall ./sha256/src/sha256.c -c

clean:
	rm -r *.o
