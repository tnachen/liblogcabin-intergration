all:
	g++ -std=c++14 -o raft-example-server Server.cpp -I/usr/local/include -lproxygenhttpserver -lfolly -lglog -pthread -lliblogcabin
