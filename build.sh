#!/bin/sh

CFLAGS="-O0 -g -Wall -Wextra -Wpedantic -std=c11 -pthread -Iwren/src/include -L." 
OPTS=""

CODE_HOME=`pwd`

if [ -e wren ];
then
    if [ ! -e wren/lib/libwren.a ];
    then
        cd wren && make -j 4;
        cp lib/libwren.a $CODE_HOME/libwren.a
        cd $CODE_HOME
    fi
else
    echo "Make sure that you have the wren submodule!.";
fi

cc -o p2pjs p2pjs.c sha-256.c $CFLAGS $OPTS -lwren -lm

