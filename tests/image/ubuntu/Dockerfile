FROM ubuntu:24.04

ENV DEBIAN_FRONTEND="noninteractive"
RUN apt-get update && \
    apt-get install -y autotools-dev autoconf build-essential \
    	automake pkg-config libtool libnuma-dev

COPY . /librseq

WORKDIR /librseq

RUN ./bootstrap && \
    ./configure && \
    make && \
    make install && \
    ldconfig
