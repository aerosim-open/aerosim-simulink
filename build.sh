#!/bin/bash

if [ -z "$AEROSIM_SIMULINK_ROOT" ]; then
    echo "AEROSIM_SIMULINK_ROOT is not set. Please set it to the root directory of the Aerosim Simulink project."
    exit 1
fi

MATLAB_KAFKA_APP_PATH=$AEROSIM_SIMULINK_ROOT/matlab-apache-kafka/Software/MATLAB/app/sfun

echo
echo "Building dependencies for Aerosim Simulink S-functions..."
RDKAFKA_PATH=$AEROSIM_SIMULINK_ROOT/matlab-apache-kafka/Software/CPP/librdkafka
if [ -f "$MATLAB_KAFKA_APP_PATH/librdkafka.so" ] && [ -f "$MATLAB_KAFKA_APP_PATH/lib/librdkafka.a" ]; then
    echo "librdkafka is already built. Skipping..."
else
    echo
    echo "----- Building librdkafka -----"
    pushd $RDKAFKA_PATH > /dev/null
    ./configure
    make
    cp -f $RDKAFKA_PATH/src/librdkafka.so $MATLAB_KAFKA_APP_PATH
    cp -f $RDKAFKA_PATH/src/librdkafka.a $MATLAB_KAFKA_APP_PATH/lib
    popd > /dev/null
fi


# Build the jansson dependency
JANSSON_PATH=$AEROSIM_SIMULINK_ROOT/matlab-apache-kafka/Software/CPP/jansson
if [ -f "$MATLAB_KAFKA_APP_PATH/lib/libjansson.a" ]; then
    echo "jansson is already built. Skipping..."
else
    echo
    echo "----- Building jansson -----"
    pushd $JANSSON_PATH > /dev/null
    mkdir -p build
    pushd build > /dev/null
    cmake ..
    make
    cp -f $JANSSON_PATH/build/lib/libjansson.a $MATLAB_KAFKA_APP_PATH/lib
    cp -f $JANSSON_PATH/build/include/* $MATLAB_KAFKA_APP_PATH/inc/
    popd > /dev/null
    popd > /dev/null
fi

# Build the AeroSim Simulink S-function MEX files
echo
echo "----- Building AeroSim Simulink S-function MEX files -----"
MATLAB_PATH=$(which "matlab")
if [ "$MATLAB_PATH" == "" ]; then
    echo "MATLAB is not installed or not in the PATH. Please install MATLAB and add it to the PATH."
    exit 1
fi

echo "Running MATLAB in batch mode (no GUI) to build the S-functions..."
matlab -wait -batch "run('$AEROSIM_SIMULINK_ROOT/matlab-apache-kafka/Software/MATLAB/startup.m');run('$AEROSIM_SIMULINK_ROOT/aerosim-sfunctions/build_aerosim_sfuns.m');exit;"
