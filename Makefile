rloop: rloop.cc
	g++ -Wall -o $@ $^ `pkg-config fuse --cflags --libs`
