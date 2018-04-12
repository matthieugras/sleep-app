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

# Usage
sudo systemctl start sleep-app  
example: sudo sa-control -a -h 7 -m 30 -f /home/grasm/Music/bla.mp3
