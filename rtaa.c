/* NOTES */
// Make sigint handler
// Remove for loops init their own index var's


#include <stdio.h>
#include <errno.h>
#include <unistd.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include <time.h>
#include "mailbox.h" // for gpu batches
#include "gpu_fft.h"
#include <jack/jack.h>
#include "led-matrix-c.h"

#define PI 3.14159265358979323846

jack_port_t *input_port;
jack_client_t *client;

int i, j, k, loops, freq, log2_N, jobs, N, mb;

// wrapper struct for a pair of floats that make a complex #
// The real part is "re" and the imaginary part is "im"
struct GPU_FFT_COMPLEX *base;
// struct that holds everything for the transform setup. 3 members of importance:
// 1) "in" is a pointer to the start of the memory where the gpu expects to find FFT input data
// 2) "out" is a pointer to the start of the memory where the gpu will put the FFT results
// 3) "step" is used in "batch mode" and gives the offset between the in and out arrays for each successive FFT in the batch
struct GPU_FFT *fft;

struct RGBLedMatrix *matrix;
struct LedCanvas *offscreen_canvas;
struct RGBLedMatrixOptions ledOptions;
float val1=0, val2=0, val3=0, val4=0;


// The process callback for this JACK application is called in a
// special realtime thread once for each audio cycle.
int process (jack_nframes_t nframes, void *arg) {

	jack_default_audio_sample_t *in;
	in = jack_port_get_buffer (input_port, nframes);

    for (j=0; j<jobs; j++) {
        base = fft->in + j*fft->step; // input buffer of structs with 2 floats

        for (i=0; i<N; i++) {
            // hann window & transform data range from -1<->=1 to 0<->65535
            base[i].re = ((((0.5 * (1 - cosf(2 * PI * i/ (nframes - 1))) * in[i]) + 1.0) * 32768) - 1);
            base[i].im = 0;
        }

    }

    usleep(1); // Yield to OS
    gpu_fft_execute(fft); // call one or many times

    for (j=0; j<jobs; j++) {
        base = fft->out + j*fft->step; // output buffer of structs with 2 floats

        for (i=0; i<128; i++) {
            k = i * 4;
            val1 = fabs(5*log10(sqrt(pow(base[k].re,2)+pow(base[k].im,2))));
            val2 = fabs(5*log10(sqrt(pow(base[k+1].re,2)+pow(base[k+1].im,2))));
            val3 = fabs(5*log10(sqrt(pow(base[k+2].re,2)+pow(base[k+2].im,2))));
            val4 = fabs(5*log10(sqrt(pow(base[k+3].re,2)+pow(base[k+3].im,2))));
            // average 4 values for the column height of that freq
            led_canvas_set_pixel(offscreen_canvas, i, 32-(int)((val1+val2+val3+val4)/4), 128, 128, 128);
        }

    }

    // Now, we swap the canvas. We give swap_on_vsync the buffer we
    // just have drawn into, and wait until the next vsync happens.
    // we get back the unused buffer to which we'll draw in the next iteration.
    offscreen_canvas = led_matrix_swap_on_vsync(matrix, offscreen_canvas);
    led_canvas_clear(offscreen_canvas);

	return 0;
}


// JACK calls this shutdown_callback if the server ever shuts down or
// decides to disconnect the client.
void jack_shutdown (void *arg) {
	gpu_fft_release(fft); // Videocore memory lost if not freed !
	led_matrix_delete(matrix); // resets the display
	exit (1);
}

void fft_init() {
	log2_N = 10; 	// 8 <= log2_N <= 22
    jobs   = 1;  	// transforms per batch
    loops  = 1;  	// test repetitions

    N = 1<<log2_N; // N is equal to 1, left shifted by log2_N 0 bits. 8 is 256. 22 is ~4.2e6.

	// mailbox, fft length: 2**(8 <-> 22), REV/FWD time <-> freq domain, # of transforms in each batch, and the fft struct
    int ret = gpu_fft_prepare(mb, log2_N, GPU_FFT_FWD, jobs, &fft); // call once for setup

    switch(ret) {
        case -1: printf("Unable to enable V3D. Please check your firmware is up to date.\n"); exit(1);
        case -2: printf("log2_N=%d not supported.  Try between 8 and 22.\n", log2_N);         exit(1);
        case -3: printf("Out of memory.  Try a smaller batch or increase GPU memory.\n");     exit(1);
        case -4: printf("Unable to map Videocore peripherals into ARM memory space.\n");      exit(1);
        case -5: printf("Can't open libbcm_host.\n");                                         exit(1);
    }

	return;
}

void led_init(int argc, char *argv[]) {
  memset(&ledOptions, 0, sizeof(ledOptions));
  ledOptions.rows = 32;
  ledOptions.cols = 128;
  ledOptions.chain_length = 1;
  ledOptions.hardware_mapping = "adafruit-hat";

  matrix = led_matrix_create_from_options(&ledOptions, &argc, &argv);
  if (matrix == NULL) {
    fprintf(stderr, "Error creating matrix from options");
    return;
  }

  // Create one extra buffer to draw on, which is then swapped on each refresh.
  offscreen_canvas = led_matrix_create_offscreen_canvas(matrix);

  return;
}

int main (int argc, char *argv[]) {
	const char **ports;
	const char *client_name = "ledPi";
	const char *server_name = NULL;
	jack_options_t options = JackNullOption;
	jack_status_t status;

	mb = mbox_open();

	/* open a client connection to the JACK server */
	client = jack_client_open (client_name, options, &status, server_name);
	if (client == NULL) {
		fprintf (stderr, "jack_client_open() failed, "
			 "status = 0x%2.0x\n", status);
		if (status & JackServerFailed) {
			fprintf (stderr, "Unable to connect to JACK server\n");
		}
		exit (1);
	}
	if (status & JackServerStarted) {
		fprintf (stderr, "JACK server started\n");
	}

	// tell the JACK server to call `process()' whenever
	// there is work to be done.
	jack_set_process_callback (client, process, 0);

	// tell the JACK server to call `jack_shutdown()' if
	// it ever shuts down, either entirely, or if it
	// just decides to stop calling us.
	jack_on_shutdown (client, jack_shutdown, 0);

	// display the current sample rate.
	printf ("engine sample rate: %" PRIu32 "\n", jack_get_sample_rate (client));

	/* create a port */
	input_port = jack_port_register (client, "input",
					 JACK_DEFAULT_AUDIO_TYPE,
					 JackPortIsInput, 0);

	if ((input_port == NULL)) {
		fprintf(stderr, "no more JACK ports available\n");
		exit (1);
	}

	fft_init();
	led_init(argc, argv);

	// Tell the JACK server that we are ready to roll.  Our
	// process() callback will start running now.
	if (jack_activate (client)) {
		fprintf (stderr, "cannot activate client");
		exit (1);
	}

	// Connect the ports.  You can't do this before the client is
	// activated, because we can't make connections to clients
	// that aren't running.
	ports = jack_get_ports (client, NULL, NULL, JackPortIsPhysical|JackPortIsOutput);
	if (ports == NULL) {
		fprintf(stderr, "no physical capture ports\n");
		exit (1);
	}

	if (jack_connect (client, ports[0], jack_port_name (input_port))) {
		fprintf (stderr, "cannot connect input ports\n");
	}

	free (ports);

	/* keep running until stopped by the user */
	sleep (-1);

	// this is never reached but if the program
	// had some other way to exit besides being killed,
	// they would be important to call.

	jack_client_close(client);
	exit(0);
}
