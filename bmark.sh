#!/bin/sh

CONTROLLER_HOST="localhost"
CONTROLLER_PORT="6633"
LOOP_DURATION="5000" # in ms
LOOPS="11"
MACS="1000"

NTHREADS=$1
SWITCHES=$2

RESULTS_DIR="results/results-t$NTHREADS-s$SWITCHES"

rm -rf $RESULTS_DIR
mkdir -p $RESULTS_DIR

killall raw_controller

echo "Ready to run with $NTHREADS threads and $SWITCHES switches!"

for method in COMPLETION SYNCQUEUE RINGBUFFER; do 

    make CFLAGS="-D$method -DNTHREADS=$NTHREADS" HOME=$HOME 
    
    for measure in THROUGHPUT LATENCY; do
        for i in $(seq 1 1); do
            benchmark_fname="$RESULTS_DIR/benchmark-$measure-$method-$i"
            controller_fname="$RESULTS_DIR/controller-$measure-$method-$i"

            echo "Method: $method, Measure: $measure, run $i"
                    
            echo "Method: $method, Measure: $measure, run $i" > $controller_fname
            ./raw_controller l2 >> $controller_fname 2>&1 &
            sleep 3

            echo "Method: $method, Measure: $measure, run $i" >$benchmark_fname
            
            flag=""
            if [ $measure = THROUGHPUT ]; then
                flag="-t"
            fi;
            
            echo "$ cbench -c $CONTROLLER_HOST -p $CONTROLLER_PORT -m $LOOP_DURATION -l $LOOPS -s $SWITCHES -M $MACS $flag" >> $benchmark_fname
            cbench -c $CONTROLLER_HOST -p $CONTROLLER_PORT -m $LOOP_DURATION -l $LOOPS -s $SWITCHES -M $MACS $flag >> $benchmark_fname 2>&1
            sleep 5
            killall -s SIGINT raw_controller
            sleep 5
        done;
    done;
    
done;
