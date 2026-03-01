#本机开发
cd /home/lty/esp/esp-idf && . ./export.sh && cd /home/lty/projects/hidcon-device
idf.py build && idf.py flash

#或者直接github 的 devcontainer 开发
idf.py fullclean
idf.py set-target esp32-s3
idf.py build && idf.py flash