#!/bin/bash

RESULTS_DIR="$1" # Cannot have hyphens
MEASURE_ARG="$2"

MEASURE="THROUGHPUT";
if [ "$MEASURE_ARG" = "l" ]; then
    MEASURE="LATENCY";
fi;

cd $RESULTS_DIR

COMPLETION=0
RINGBUFFER=0
SYNCQUEUE=0

NCOMPLETION=0
NRINGBUFFER=0
NSYNCQUEUE=0

printf "METHOD\tAVG\tSTDEV\n"
for f in `ls benchmark-$MEASURE-*`; do 
    rname=`echo $f | cut -d'-' -f 2-3`
    
    vals=`cat $f | grep "RESULT" | cut -d' ' -f 8`
    avg=`echo $vals | cut -d "/" -f 3`;
    stdev=`echo $vals | cut -d "/" -f 4`
    
    if [[ $rname == *COMPLETION* ]]; then
        NCOMPLETION=`echo $NCOMPLETION + 1 | bc -l`
        COMPLETION=`echo $COMPLETION + $avg | bc -l`
    fi;

    if [[ $rname == *RINGBUFFER* ]]; then
        NRINGBUFFER=`echo $NRINGBUFFER + 1 | bc -l`
        RINGBUFFER=`echo $RINGBUFFER + $avg | bc -l`
    fi;
    
    if [[ $rname == *SYNCQUEUE* ]]; then
        NSYNCQUEUE=`echo $NSYNCQUEUE + 1 | bc -l`
        SYNCQUEUE=`echo $SYNCQUEUE + $avg | bc -l`
    fi;
    
    printf "%s\t%s\t%s\n" $rname $avg $stdev
done;

COMPLETION=`echo $COMPLETION / $NCOMPLETION | bc`
RINGBUFFER=`echo $RINGBUFFER / $NRINGBUFFER | bc`
SYNCQUEUE=`echo $SYNCQUEUE / $NSYNCQUEUE | bc`

echo
echo -e "COMPLETION\t$COMPLETION"
echo -e "RINGBUFFER\t$RINGBUFFER"
echo -e "SYNCQUEUE\t$SYNCQUEUE"

cd - > /dev/null

