S = hex/shader_256.hex \
    hex/shader_512.hex \
    hex/shader_1k.hex \
    hex/shader_2k.hex \
    hex/shader_4k.hex \
    hex/shader_8k.hex \
    hex/shader_16k.hex \
    hex/shader_32k.hex \
    hex/shader_64k.hex \
    hex/shader_128k.hex \
    hex/shader_256k.hex \
    hex/shader_512k.hex \
    hex/shader_1024k.hex \
    hex/shader_2048k.hex \
    hex/shader_4096k.hex

C = mailbox.c gpu_fft.c gpu_fft_base.c gpu_fft_twiddles.c gpu_fft_shaders.c

C1D = $(C) real_time_audio_analyzer.c
C2D = $(C) hello_fft_2d.c gpu_fft_trans.c

H1D = gpu_fft.h mailbox.h
H2D = gpu_fft.h mailbox.h gpu_fft_trans.h hello_fft_2d_bitmap.h

F = -I/home/pi/rpi_led/rpi-rgb-led-matrix/include -L/home/pi/rpi_led/rpi-rgb-led-matrix/lib -lrgbmatrix -ljack -lpthread -lstdc++ -lrt -lm -ldl

# -I..rpi..include is for a header file
# -Xlinker --verbose
# Might need -lpthred???

all:    rtaa hello_fft_2d.bin

rtaa:   $(S) $(C1D) $(H1D)
    gcc -o rtaa /home/pi/rpi_led/rpi-rgb-led-matrix/lib/led-matrix-c.o $(F) $(C1D)

hello_fft_2d.bin:   $(S) hex/shader_trans.hex $(C2D) $(H2D)
    gcc -o hello_fft_2d.bin $(F) $(C2D)

clean:
    rm -f *.bin

