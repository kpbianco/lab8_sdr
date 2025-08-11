# lab8_sdr

flash .bit.bin file as normal
./config_codec.sh

gcc udp_sdr.c -O2 -o udp_sdr
./udp_sdr [HOST_IP] --fake [DDS_FREQ] --tune [TUNE_FREQ] --start
example:  ./udp_sdr 192.168.0.160 --fake 5000 --tune 12000 --start

you can then interact with it using:
Runtime commands on stdin:
  f <Hz>    set fake DDS
  t <Hz>    set tune DDS
  d <ip>    change destination IP
  s on|off  start/stop streaming
  q         quit


the port is 25344 by default as a variable in the udp_sdr file
