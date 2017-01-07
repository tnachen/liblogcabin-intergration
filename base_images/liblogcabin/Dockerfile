FROM tnachen/proxygen

ADD update_compiler.sh /liblogcabin/update_compiler.sh

RUN /liblogcabin/update_compiler.sh

RUN apt-get -y install git protobuf-compiler libprotobuf-dev scons

WORKDIR /cryptopp-src
RUN git clone https://github.com/tnachen/cryptopp && cd cryptopp && git checkout 5_6_1_fixes && make && make install && cd .. && rm -rf cryptopp

WORKDIR /liblogcabin-src
RUN git clone https://github.com/logcabin/liblogcabin && cd liblogcabin && git submodule init && git submodule update && scons && scons install && cd .. && rm -rf liblogcabin
