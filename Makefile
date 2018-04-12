CC=clang

all:
	$(CC) main.c sleep.c process.c sa_util.c -g -o sleep-app -lX11 -lXss -lm -pthread -Wall -Wextra -Wno-unused-parameter -Wno-unused-variable -Wno-unused-function
	$(CC) sound.c audio_transcode.c ringbuf.c sa_util.c -g -o sound -lpulse -pthread -lavformat -lavcodec -lavutil -Wall -Wextra -Wno-unused-parameter -Wno-unused-variable -Wno-unused-function
	$(CC) sa-control.c 2048.c sa_util.c -g -o sa-control -Wall -Wextra -Wno-unused-parameter -Wno-unused-variable -Wno-unused-function
clean:
	rm sleep-app sound sa-control

install:
	sudo cp -a sleep-app sound sa-control /usr/local/bin
	sudo cp -a sleep-app.service /etc/systemd/system

uninstall:
	sudo systemctl stop sleep-app
	sudo systemctl disable sleep-app
	sudo rm /usr/local/bin/sound /usr/local/bin/sa-control /usr/local/bin/sleep-app
	sudo rm /etc/systemd/system/sleep-app.service