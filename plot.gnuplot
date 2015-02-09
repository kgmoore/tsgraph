set datafile separator "\t"
set terminal pngcairo size 350,262 enhanced font 'Verdana,10'
set output 'output.png'
plot "1000_pcrs.txt" using 1:2 title 'Error'