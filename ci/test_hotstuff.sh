#!/bin/bash

sh -x ci/deploy.sh Release
cd cbuild_Release && make hotstuff
sleep 3
nohup ./hotstuff 2>&1 &


