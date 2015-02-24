for f in *pcr.txt
do
  echo "Processing $f"
  sort -k 3 $f | tail -n 2
done 
