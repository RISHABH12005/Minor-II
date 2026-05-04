idf.py create-project ids

cd ids

idf.py set-target esp32c5

idf.py menuconfig

idf.py fullclean

idf.py build

idf.py flash monitor