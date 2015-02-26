#!/bin/bash

MYDIR=`dirname $0`
NUM_NODES=4 #including coordinator
DB_NAME="mydb"
	
check_exit_status()
{
	if [ $1 -ne 0 ]; then 
		echo "Error above. Exiting. Peace."
		exit 1;
	fi
}

launch_db()
{
	pushd ../basic > /dev/null
	check_exit_status $?
	./runN.py $NUM_NODES $DB_NAME init,start > /dev/null
	check_exit_status $?
	popd > /dev/null
}

pushd $MYDIR > /dev/null

#make sure db is up
iquery -a -q "list()" > /dev/null
if [ $? -ne 0 ]; then
	echo "Can't access db... trying to restart"
	killall scidb > /dev/null 2>&1
	launch_db
fi

rm -f distrotest.out

iquery -a -q "apply(tbt,nodeid,nodeid())" >> distrotest.out
check_exit_status $?

iquery -a -q "apply(sg(tbt,1,-1,foo,0,1,0,2,2),nodeid,nodeid())" >> distrotest.out
check_exit_status $?

iquery -a -q "apply(sg(tbt,1,-1,foo,0,0,1,2,2),nodeid,nodeid())" >> distrotest.out
check_exit_status $?

iquery -a -q "apply(sg(tbt,1,-1,foo,0,1,1,2,2),nodeid,nodeid())" >> distrotest.out
check_exit_status $?

iquery -a -q "apply(sg(fbf,1,-1,foo,0,0,0,4,4),nodeid,nodeid())" >> distrotest.out
check_exit_status $?
#does nothing (vector smaller than chunk size)
iquery -a -q "apply(sg(fbf,1,-1,foo,0,1,1,4,4),nodeid,nodeid())" >> distrotest.out
check_exit_status $?

#same as vector (2,0)
iquery -a -q "apply(sg(fbf,1,-1,foo,0,2,1,4,4),nodeid,nodeid())" >> distrotest.out
check_exit_status $?
iquery -a -q "apply(sg(fbf,1,-1,foo,0,2,0,4,4),nodeid,nodeid())" >> distrotest.out
check_exit_status $?

#equiv
iquery -a -q "apply(sg(fbf,1,-1,foo,0,-2,-2,4,4),nodeid,nodeid())" >> distrotest.out
check_exit_status $?
iquery -a -q "apply(sg(fbf,1,-1,foo,0,2, 2,4,4),nodeid,nodeid())" >> distrotest.out
check_exit_status $?

#proof that join only requires colocation, but not correct distribution
iquery -a -q "apply(join( sg(fbf,1,-1,foo1,0,2,2,4,4), sg(fbf,1,-2,foo2,0,2,2,4,4)), abc, foo1.val+foo2.val)" >> distrotest.out

diff distrotest.out distrotest.exp
check_exit_status $?
echo "All set"
