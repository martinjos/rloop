rloop: rloop.cc
	gcc -Wall -o $@ $^ `pkg-config fuse --cflags --libs`
