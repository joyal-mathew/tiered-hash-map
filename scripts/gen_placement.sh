#!/usr/bin/env bash

set -xe

scripts/placement.sh 4000 > data/results/placement_4000.dat
scripts/placement.sh 6000 > data/results/placement_6000.dat
scripts/placement.sh 15000 > data/results/placement_15000.dat
scripts/placement.sh 60000 > data/results/placement_60000.dat

gnuplot scripts/plotting/placement_4000.gp &
gnuplot scripts/plotting/placement_6000.gp &
gnuplot scripts/plotting/placement_15000.gp &
gnuplot scripts/plotting/placement_60000.gp &
gnuplot scripts/plotting/placement_mem_4000.gp &
gnuplot scripts/plotting/placement_mem_6000.gp &
gnuplot scripts/plotting/placement_mem_15000.gp &
gnuplot scripts/plotting/placement_mem_60000.gp &

wait
