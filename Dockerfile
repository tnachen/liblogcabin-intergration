FROM tnachen/liblogcabin

ADD . /liblogcabin-integration

WORKDIR /liblogcabin-integration

RUN make
