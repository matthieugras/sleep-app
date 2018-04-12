# sleep-app
Advanced alarm clock daemon for Linux (BSD style license)

# Dependencies
X11  
Xlib  
XScreensaver extension  
FFMPEG  
Pulseaudio  


# Installation
make  
make install  
Temporary hack for Xauthority: Change the putenv line of sa_main_get_x11_inactive_time in main.c

# Usage
sudo systemctl start sleep-app  
example: sudo sa-control -a -h 7 -m 30 -f /home/grasm/Music/bla.mp3
