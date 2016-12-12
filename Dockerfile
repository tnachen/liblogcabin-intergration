FROM mercury/proxygen

RUN g++ -std=c++11 -o raft-example-server Server.cpp -lproxygenhttpserver -lfolly -lglog -pthread
