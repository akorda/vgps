# Virtual GPS device

Based on [virtualgps](https://github.com/rkaczorek/virtualgps) project, but written in `C`.

```bash
# number of pseudo terminals
cat /proc/sys/kernel/pty/nr
# view the virtual device:
cat /dev/pts/3
# add pty to gpsd
sudo gpsdctl add /dev/pts/3
# remove pty from gpsd
sudo gpsdctl remove /dev/pts/3
# view gpsd logs
sudo journalctl -f -u gpsd
# run a simple X11 client, e.g. xgps
# check gpsd status
telnet localhost 2947
```

## GPS clients

## gpsdclient

```bash
pipx install gpsdclient
pipx run gpsdclient
```
