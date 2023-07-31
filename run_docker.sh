#!/bin/bash
export MY_CONTAINER="pgsysml_wd"
export IMAGE="pgsys:v0"
export WORKDIR="/home/woodybryant.wd/pgsysml"

docker run -it  --name $MY_CONTAINER \
         --shm-size 5gb \
         --gpus all \
         -v $WORKDIR:/pgsysml/ \
         --privileged \
         --cap-add sys_ptrace \
         --security-opt seccomp=unconfined \
         -p 54321:5432 \
         -p 54322:5433 \
         -p 54323:5434 \
         -p 54324:5435 \
         -p 54325:5436 \
         -p 54387:22\
	     -d $IMAGE
         /bin/bash