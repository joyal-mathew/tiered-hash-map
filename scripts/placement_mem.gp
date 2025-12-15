set term svg
set output "plots/placement_mem.svg"

plot "data/placement.dat" using 1:3 with linespoints
