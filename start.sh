#!/bin/bash
cd "$(dirname "$0")/backend"
sudo python3 server.py &
sleep 2
cd "$(dirname "$0")/frontend"
xinit ../run.sh
