rloop: rloop.cc
	g++ -Wall -o $@ $^ `pkg-config fuse --cflags --libs` -lboost_filesystem -lboost_system
