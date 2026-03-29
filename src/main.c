/*
 * LTE Uplink Monitor for HackRF One - WITH OPENGL WATERFALL (macOS fixed)
 * =============================================================
 * Fixed for AppleClang: added <unistd.h> for usleep()
 * All other functionality unchanged.
 */

#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <string.h>
#include <time.h>
#include <math.h>
#include <signal.h>
#include <stdbool.h>
#include <unistd.h>          /* <<< ADDED for usleep() on macOS */
#include <libhackrf/hackrf.h>

#include <GL/glew.h>
#include <GLFW/glfw3.h>
#include <fftw3.h>

#define SAMPLE_RATE_HZ          16000000ULL
#define SYNC_DURATION_MS        10
#define DWELL_TIME_SEC          3
#define HANG_TIME_SEC           8
#define POWER_THRESHOLD         0.08

#define FFT_SIZE                1024
#define WATERFALL_HEIGHT        512
#define WATERFALL_WIDTH         FFT_SIZE

static hackrf_device *device = NULL;
static FILE *record_file = NULL;
static bool recording = false;
static time_t last_strong_signal_time = 0;
static uint64_t current_center_freq = 0;

static const uint64_t chunk_centers[] = {
    1718000000ULL, 1730000000ULL, 1742000000ULL, 1754000000ULL, 1766000000ULL, 1778000000ULL,
    1928000000ULL, 1940000000ULL, 1952000000ULL, 1964000000ULL, 1976000000ULL,
    2508000000ULL, 2520000000ULL, 2532000000ULL, 2544000000ULL, 2556000000ULL, 2568000000ULL
};
static const int NUM_CHUNKS = sizeof(chunk_centers) / sizeof(chunk_centers[0]);

static int *shuffled_order = NULL;
static int current_pos = 0;

// Waterfall state
static GLFWwindow *window = NULL;
static GLuint texture_id = 0;
static float *waterfall_data = NULL;
static int waterfall_row = 0;

static fftwf_plan fft_plan;
static fftwf_complex *fft_in = NULL;
static fftwf_complex *fft_out = NULL;
static float *fft_buffer = NULL;

void shuffle_order(void) {
    for (int i = 0; i < NUM_CHUNKS; i++) shuffled_order[i] = i;
    for (int i = NUM_CHUNKS - 1; i > 0; i--) {
        int j = rand() % (i + 1);
        int tmp = shuffled_order[i];
        shuffled_order[i] = shuffled_order[j];
        shuffled_order[j] = tmp;
    }
    printf("=== NEW RANDOM SCAN CYCLE STARTED (%d unique 16 MHz chunks) ===\n", NUM_CHUNKS);
}

void print_status(void) {
    const char *band = (current_center_freq > 2400000000ULL) ? "7" :
                       (current_center_freq > 1900000000ULL) ? "1" : "3";
    printf("Random hop → Band %s @ %.3f MHz (chunk %d/%d)\n",
           band, current_center_freq / 1e6, current_pos + 1, NUM_CHUNKS);
}

void beep(void) {
    if (system("osascript -e 'beep 2'") != 0) {   // macOS-friendly beep fallback
        printf("\a"); fflush(stdout);
    }
}

void update_waterfall(float *magnitudes) {
    memmove(waterfall_data + WATERFALL_WIDTH, waterfall_data,
            (WATERFALL_HEIGHT - 1) * WATERFALL_WIDTH * sizeof(float));
    memcpy(waterfall_data, magnitudes, WATERFALL_WIDTH * sizeof(float));

    glBindTexture(GL_TEXTURE_2D, texture_id);
    glTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, WATERFALL_WIDTH, WATERFALL_HEIGHT,
                    GL_RED, GL_FLOAT, waterfall_data);
    glBindTexture(GL_TEXTURE_2D, 0);
}

void render_waterfall(void) {
    glClear(GL_COLOR_BUFFER_BIT);
    glBindTexture(GL_TEXTURE_2D, texture_id);
    glBegin(GL_QUADS);
        glTexCoord2f(0.0f, 0.0f); glVertex2f(-1.0f, -1.0f);
        glTexCoord2f(1.0f, 0.0f); glVertex2f( 1.0f, -1.0f);
        glTexCoord2f(1.0f, 1.0f); glVertex2f( 1.0f,  1.0f);
        glTexCoord2f(0.0f, 1.0f); glVertex2f(-1.0f,  1.0f);
    glEnd();
    glBindTexture(GL_TEXTURE_2D, 0);
    glfwSwapBuffers(window);
}

void init_waterfall(void) {
    waterfall_data = calloc(WATERFALL_HEIGHT * WATERFALL_WIDTH, sizeof(float));

    glGenTextures(1, &texture_id);
    glBindTexture(GL_TEXTURE_2D, texture_id);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_R32F, WATERFALL_WIDTH, WATERFALL_HEIGHT, 0,
                 GL_RED, GL_FLOAT, NULL);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    glBindTexture(GL_TEXTURE_2D, 0);

    fft_in = fftwf_malloc(sizeof(fftwf_complex) * FFT_SIZE);
    fft_out = fftwf_malloc(sizeof(fftwf_complex) * FFT_SIZE);
    fft_buffer = malloc(FFT_SIZE * sizeof(float));
    fft_plan = fftwf_plan_dft_1d(FFT_SIZE, fft_in, fft_out, FFTW_FORWARD, FFTW_ESTIMATE);
}

void process_iq_for_waterfall(uint8_t *data, uint32_t len) {
    uint32_t num_samples = len / 2;
    if (num_samples < FFT_SIZE) return;

    for (int i = 0; i < FFT_SIZE; i++) {
        int8_t re = (int8_t)data[i*2];
        int8_t im = (int8_t)data[i*2 + 1];
        fft_in[i][0] = re / 127.0f;
        fft_in[i][1] = im / 127.0f;
    }

    fftwf_execute(fft_plan);

    float max_mag = 0.0f;
    for (int i = 0; i < FFT_SIZE; i++) {
        float mag = sqrtf(fft_out[i][0]*fft_out[i][0] + fft_out[i][1]*fft_out[i][1]);
        fft_buffer[i] = mag;
        if (mag > max_mag) max_mag = mag;
    }

    if (max_mag > 0.0f) {
        for (int i = 0; i < FFT_SIZE; i++) {
            fft_buffer[i] = fminf(fft_buffer[i] / max_mag * 2.0f, 1.0f);  // slightly boosted contrast
        }
    }

    int half = FFT_SIZE / 2;
    for (int i = 0; i < half; i++) {
        float tmp = fft_buffer[i];
        fft_buffer[i] = fft_buffer[i + half];
        fft_buffer[i + half] = tmp;
    }

    update_waterfall(fft_buffer);
}

void insert_sync_marker(FILE *f) {
    uint64_t sync_samples = SAMPLE_RATE_HZ * SYNC_DURATION_MS / 1000;
    uint8_t *sync_buf = malloc(sync_samples * 2);
    if (sync_buf) {
        for (uint64_t j = 0; j < sync_samples * 2; j += 2) {
            sync_buf[j]     = 127;
            sync_buf[j + 1] = 0;
        }
        fwrite(sync_buf, 1, sync_samples * 2, f);
        free(sync_buf);
    }
}

int rx_callback(hackrf_transfer *transfer) {
    if (transfer->valid_length == 0) return 0;

    uint8_t *data = transfer->buffer;
    uint32_t len = transfer->valid_length;
    uint32_t num_samples = len / 2;

    double sum_sq = 0.0;
    for (uint32_t i = 0; i < len; i += 2) {
        int8_t i_val = (int8_t)data[i];
        int8_t q_val = (int8_t)data[i + 1];
        double i_norm = i_val / 127.0;
        double q_norm = q_val / 127.0;
        sum_sq += (i_norm * i_norm) + (q_norm * q_norm);
    }
    double rms_power = sqrt(sum_sq / num_samples);

    time_t now = time(NULL);

    if (rms_power > POWER_THRESHOLD) {
        last_strong_signal_time = now;

        if (!recording) {
            char filename[128];
            strftime(filename, sizeof(filename), "lte_uplink_violation_%Y%m%d_%H%M%S.iq", localtime(&now));

            record_file = fopen(filename, "wb");
            if (record_file) {
                recording = true;
                printf("\n=== VIOLATION DETECTED! RMS=%.4f @ %.3f MHz ===\n", rms_power, current_center_freq / 1e6);
                printf("Recording to %s\n", filename);
                beep();
                printf("Inserted START sync marker\n");
                insert_sync_marker(record_file);
                fwrite(data, 1, len, record_file);
            }
            return 0;
        }
    }

    if (recording) {
        fwrite(data, 1, len, record_file);

        if (now - last_strong_signal_time >= HANG_TIME_SEC) {
            printf("=== END OF VIOLATION – inserting END sync marker and beep ===\n");
            beep();
            insert_sync_marker(record_file);
            printf("Inserted END sync marker\n");

            fclose(record_file);
            record_file = NULL;
            recording = false;
            printf("Recording stopped. Resuming scan...\n");
        }
    }

    process_iq_for_waterfall(data, len);
    return 0;
}

void sigint_handler(int s) {
    (void)s;
    printf("\nShutting down...\n");
    if (device) {
        hackrf_stop_rx(device);
        hackrf_close(device);
        hackrf_exit();
    }
    if (record_file) fclose(record_file);
    if (window) glfwDestroyWindow(window);
    glfwTerminate();
    free(shuffled_order);
    free(waterfall_data);
    if (fft_plan) fftwf_destroy_plan(fft_plan);
    if (fft_in) fftwf_free(fft_in);
    if (fft_out) fftwf_free(fft_out);
    free(fft_buffer);
    exit(0);
}

int main(void) {
    signal(SIGINT, sigint_handler);
    srand(time(NULL));

    shuffled_order = malloc(NUM_CHUNKS * sizeof(int));
    shuffle_order();

    if (!glfwInit()) {
        fprintf(stderr, "Failed to initialize GLFW\n");
        return 1;
    }

    glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 2);
    glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 1);
    window = glfwCreateWindow(1024, 600, "LTE Uplink Monitor - OpenGL Waterfall", NULL, NULL);
    if (!window) {
        fprintf(stderr, "Failed to create GLFW window\n");
        glfwTerminate();
        return 1;
    }

    glfwMakeContextCurrent(window);
    if (glewInit() != GLEW_OK) {
        fprintf(stderr, "Failed to initialize GLEW\n");
        return 1;
    }

    glClearColor(0.0f, 0.0f, 0.0f, 1.0f);
    init_waterfall();

    printf("OpenGL Waterfall initialized.\n");

    if (hackrf_init() != HACKRF_SUCCESS || hackrf_open(&device) != HACKRF_SUCCESS) {
        fprintf(stderr, "HackRF init failed\n");
        return 1;
    }

    current_center_freq = chunk_centers[shuffled_order[0]];
    current_pos = 0;

    hackrf_set_freq(device, current_center_freq);
    hackrf_set_sample_rate(device, SAMPLE_RATE_HZ);
    hackrf_set_lna_gain(device, 32);
    hackrf_set_vga_gain(device, 40);
    hackrf_set_amp_enable(device, 0);

    printf("HackRF LTE Uplink Monitor started with OpenGL Waterfall (macOS)\n");
    printf("Point your directional antenna at celltowers.\n\n");

    if (hackrf_start_rx(device, rx_callback, NULL) != HACKRF_SUCCESS) {
        fprintf(stderr, "Failed to start RX\n");
        goto cleanup;
    }

    time_t last_switch_time = time(NULL);

    while (!glfwWindowShouldClose(window)) {
        time_t now = time(NULL);

        if (!recording && (now - last_switch_time >= DWELL_TIME_SEC)) {
            hackrf_stop_rx(device);

            current_pos++;
            if (current_pos >= NUM_CHUNKS) {
                shuffle_order();
                current_pos = 0;
            }

            current_center_freq = chunk_centers[shuffled_order[current_pos]];
            hackrf_set_freq(device, current_center_freq);
            hackrf_start_rx(device, rx_callback, NULL);

            last_switch_time = now;
            print_status();
        }

        render_waterfall();
        glfwPollEvents();
        usleep(10000);   // now properly declared
    }

cleanup:
    if (device) {
        hackrf_stop_rx(device);
        hackrf_close(device);
        hackrf_exit();
    }
    free(shuffled_order);
    return 0;
}
