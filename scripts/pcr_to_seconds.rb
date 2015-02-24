# ruby sample code.
# # process every line in a text file with ruby (version 1).
file = ARGV[0]
first_pcr = nil
first_timestamp = nil

File.readlines(file).each do |line|
  values = line.split("\t");
  
  if (first_pcr == nil) then
    first_pcr = values[0].to_i;
    first_timestamp = values[1].to_i;
  end

  pcr = (values[0].to_i - first_pcr).to_f / 90000;
  timestamp = (values[1].to_i - first_timestamp).to_f / 1000000000;
  
  puts pcr.to_s + "\t" + timestamp.to_s + "\t" + (pcr-timestamp).to_s;
end
