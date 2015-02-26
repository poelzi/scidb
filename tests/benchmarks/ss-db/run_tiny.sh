#!/bin/bash

### Input parameters
XSTARTS=(503000 504000 500000 504000 504000)
YSTARTS=(503000 491000 504000 501000 493000)
size=5000
window=25
count=1

### SSDB directory
THIS=`pwd`
while [ -h "$THIS" ]; do
  ls=`ls -ld "$THIS"`
  link=`expr "$ls" : '.*-> \(.*\)$'`
  if expr "$link" : '.*/.*' > /dev/null; then
    THIS="$link"
  else
    THIS=`dirname "$THIS"`/"$link"
  fi
done
SSDB="$THIS"
###

gen(){
 # Data Generation
 ssdbgen -o -s -c tiny $SSDB/bin/tileData
 mv bench bench.pos $SSDB/data/tiny
}
init(){
 # Load the Findstars library
 echo "Loading Findstars"
 iquery -r /dev/null -aq "load_library('findstars')"
 
 # Create Data Set tiny
 echo "Create Tiny Array"
 iquery -r /dev/null -aq "CREATE IMMUTABLE ARRAY tiny <a:int32, b:int32, c:int32, d:int32, e:int32,f:int32,g:int32,h:int32, i:int32, j:int32, k:int32>[Z=0:19,20,0 ,J=0:9,10,0, I=0:9,10,0]"
 
 # Load Data tiny
 echo "Loading Tiny data .."
 START=$(date +%s)
 iquery -r /dev/null -aq "load(tiny, '$SSDB/data/tiny/bench')"
 END=$(date +%s)
 DIFF=$(( $END - $START ))
 echo "Loading Time: $DIFF seconds"

 # Cook Data tiny
 echo "Cooking Tiny data into tiny_obs array .."
 START=$(date +%s)
 iquery -r /dev/null -aq "store(findstars(tiny,a,450),tiny_obs)"
 END=$(date +%s)
 DIFF=$(( $END - $START ))
 echo "Cooking Time: $DIFF seconds"

 #  Pre-Reparting the array
 echo "Pre-Reparting the Array"
 iquery -r /dev/null -aq "store(repart(tiny,<a:int32, b:int32, c:int32, d:int32, e:int32,f:int32,g:int32,h:int32, i:int32, j:int32, k:int32>[Z=0:19,20,0 ,J=0:9,12,0, I=0:9,12,0]),tiny_reparted)"
 
 # Split the observation
 echo "Pre-Observation spliting"
 python scripts/split_tiny.py
}

q1(){
 START=$(date +%s)
 iquery -r /dev/null -aq "avg(subarray(tiny,0,0,0,19,9,9),a)"
 END=$(date +%s)
 DIFF=$(( $END - $START ))
 echo "Q1: $DIFF seconds"
}

q2(){
 START=$(date +%s)
 iquery -r /dev/null -aq "findstars(subarray(tiny,0,0,0,0,9,9),a,450)"
 END=$(date +%s)
 DIFF=$(( $END - $START ))
 echo "Q2: $DIFF seconds"
}

q3(){
 START=$(date +%s)
 iquery -r /dev/null -aq "thin(window(subarray(tiny_reparted,0,0,0,19,9,9),1,4,4,avg(a)),0,1,2,3,2,3)"
 END=$(date +%s)
 DIFF=$(( $END - $START ))
 echo "Q3: $DIFF seconds"
}

q4(){
 START=$(date +%s)
 for (( i=0; i < 20 ; i++ )) do
   iquery -r /dev/null -aq  "avg(filter(subarray(tiny_obs_`printf $i`,${XSTARTS[$ind]},${YSTARTS[$ind]},${XSTARTS[$ind]}+$size,${YSTARTS[$ind]}+$size),center is not null),sumPixel)" &
 done
 wait
 END=$(date +%s)
 DIFF=$(( $END - $START ))
 echo "Q4: $DIFF seconds"
}

q5(){
  START=$(date +%s)
  for (( i=0; i < 20 ; i++ )) do
    iquery -r /dev/null -aq  "filter(subarray(tiny_obs_`printf $i`,${XSTARTS[$ind]},${YSTARTS[$ind]},${XSTARTS[$ind]}+$size,${YSTARTS[$ind]}+$size),polygon is not null)" &
  done
  wait
  END=$(date +%s)
  DIFF=$(( $END - $START ))
  echo "Q5: $DIFF seconds"
}

q6(){
  START=$(date +%s)
  for (( i=0; i < 20 ; i++ )) do
    iquery -o csv+ -r /dev/null -aq  "filter(window(filter(subarray(tiny_obs_`printf $i`,${XSTARTS[$ind]},${YSTARTS[$ind]},${XSTARTS[$ind]}+$size,${YSTARTS[$ind]}+$size),center is not null),$window,$window,count(center)),center_count>$count)"
  done
  wait
  END=$(date +%s)
  DIFF=$(( $END - $START ))
  echo "Q6: $DIFF seconds"
}

echo "SSDB:"
if [[ $1 = "-g" ]]; then
 echo "[.... Data Generation ....]"
 gen
 echo "[..... Initialization ....]"
 init
fi
if [[ $1 = "-i" ]]; then
 echo "[.... Initialization ....]"
 init
fi

echo "[Begin]"
## ADD more repetitions here
for rep in 0 #1 2
do
 for ind in 0 1 2 3 4
 do
  echo "Run [$(($rep*5+$ind))]:"
  q1 
  q2
  q3
  q4
  q5
  q6
 done
done

echo "[End]"
