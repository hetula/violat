/*
 * MIT License
 *
 * Copyright (c) 2017 Tuomo Heino
 *
 * Permission is hereby granted, free of charge, to any person obtaining a copy
 * of this software and associated documentation files (the "Software"), to deal
 * in the Software without restriction, including without limitation the rights
 * to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
 * copies of the Software, and to permit persons to whom the Software is
 * furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in all
 * copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
 * AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 * OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
 * SOFTWARE.
 */
#define _XOPEN_SOURCE 700
#define _LARGEFILE64_SOURCE
#define _FILE_OFFSET_BITS 64
#define _GNU_SOURCE

#include <stdlib.h>
#include <unistd.h>
#include <ftw.h>
#include <stdio.h>
#include <string.h>
#include <errno.h>
#include <fnmatch.h>
#include <gst/gst.h>
#include <assert.h>

// Use 15 File Descriptors
#ifndef USE_FDS
#define USE_FDS 15
#endif

#ifndef MATCHING
#define MATCHING 0
#endif

#ifndef NO_SONG
#define NO_SONG -1
#endif

// Console colors
#define CLR_BLACK   "\x1b[30m"
#define CLR_RED     "\x1b[31m"
#define CLR_GREEN   "\x1b[32m"
#define CLR_YELLOW  "\x1b[33m"
#define CLR_BLUE    "\x1b[34m"
#define CLR_MAGENTA "\x1b[35m"
#define CLR_CYAN    "\x1b[36m"
#define CLR_WHITE   "\x1b[37m"
#define CLR_CLEAR   "\x1b[0m"

#define PLAY_MODE_NORMAL 0
#define PLAY_MODE_RANDOM 1
#define PLAY_MODE_SINGLE 2

const char *HELP =
        "\x1b[32;1mCommands:\x1b[0m\n"
                "\x1b[34;1mhelp\x1b[0m   - Prints this help\n"
                "\x1b[34;1mscan\x1b[0m   - Scan new songs in to library\n"
                "\x1b[34;1mvol\x1b[0m    - Change volume\n"
                "\n"
                "\x1b[34;1mplay\x1b[0m   - Play song\n"
                "\x1b[34;1mstop\x1b[0m   - Stop playback\n"
                "\x1b[34;1mpause\x1b[0m  - Pause playback\n"
                "\x1b[34;1mresume\x1b[0m - Resume playback\n"
                "\x1b[34;1mnext\x1b[0m   - Play next song\n"
                "\x1b[34;1mprev\x1b[0m   - Play previous song\n"
                "\x1b[34;1mnow\x1b[0m    - Show currently playing song\n"
                "\n"
                "\x1b[34;1mmode\x1b[0m   - Show current playback mode\n"
                "\x1b[34;1mnormal\x1b[0m - Change to normal playback mode\n"
                "\x1b[34;1mrandom\x1b[0m - Change to random playback mode\n"
                "\x1b[34;1msingle\x1b[0m - Change to single playback mode";

const char *logo =
        " _   _  _         _         ___  ___             _               _                           \n"
                "| | | |(_)       | |        |  \\/  |            (_)             | |                          \n"
                "| | | | _   ___  | |  __ _  | .  . | _   _  ___  _   ___  _ __  | |  __ _  _   _   ___  _ __ \n"
                "| | | || | / _ \\ | | / _` | | |\\/| || | | |/ __|| | / __|| '_ \\ | | / _` || | | | / _ \\| '__|\n"
                "\\ \\_/ /| || (_) || || (_| | | |  | || |_| |\\__ \\| || (__ | |_) || || (_| || |_| ||  __/| |   \n"
                " \\___/ |_| \\___/ |_| \\__,_| \\_|  |_/ \\__,_||___/|_| \\___|| .__/ |_| \\__,_| \\__, | \\___||_|   \n"
                "                                                         | |                __/ |            \n"
                "                                                         |_|               |___/             ";

char **song_library;
int play_mode = PLAY_MODE_NORMAL;
int file_count = 0;
int new_files = 0;
int volume = 75;
int last_sel = 0;
GstElement *pipeline = NULL;
GMainLoop *g_main_loop = NULL;
pthread_t gobj_thread;
guint bus_watch_id;
char *now_playing = NULL;

// Callback functions
int walk_dir_tree(const char *const dirpath);

gboolean bus_callback(GstBus *bus, GstMessage *message, gpointer data);

// Non externed
void free_pipeline_if_needed();

void play_file(char *play, int print_info);

void set_volume();

void set_next_song();

void set_prev_song();

void play_last_sel(int print_info);

int get_music_files(const char *filepath, const struct stat *info, int typeflag, struct FTW *pathinfo);

int read_int();

char *read_in();

// Externed
extern inline void parse_args(int argc, char **argv);

extern inline void pause_playback();

extern inline void resume_playback();

extern inline int add_entry(const char *filepath);

extern inline int select_file();

extern inline void player_interface();

/**
 * Checks if params contain only -v or --version
 * @param argc argument count
 * @param argv arguments
 * @return if was only -v or --version
 */
extern inline int only_version(int argc, char **argv) {
    return argc == 2 && (strcmp(argv[1], "-v") == 0 || strcmp(argv[1], "--version") == 0);
}

/**
 * Print gstreamers linked info
 */
extern inline void print_gstream_info() {
    const gchar *nano_str;
    guint major, minor, micro, nano;
    gst_version(&major, &minor, &micro, &nano);
    switch (nano) {
        case 1:
            nano_str = "(CVS)";
            break;
        case 2:
            nano_str = "(Prerelease)";
            break;
        default:
            nano_str = "";
            break;
    }
    printf("GStreamer %d.%d.%d %s\n", major, minor, micro, nano_str);
}

void *gobj_main_loop_run(void *ptr) {
    g_main_loop = g_main_loop_new(NULL, FALSE);
    g_main_loop_run(g_main_loop);
    printf("Closing down...");
    g_main_loop_unref(g_main_loop);
    return 0;
}

/**
 * GObject main loop to get bus events to run
 */
extern inline int start_gobj_thread() {
    return pthread_create(&gobj_thread, NULL, gobj_main_loop_run, NULL);
}

int main(int argc, char **argv) {
    if (only_version(argc, argv)) {
        printf("Viola Musicplayer\n"
                       "Music player from Terminal using GStreamer\n"
                       "Version 1.0\n"
                       "Copyright (c) Tuomo Heino\n"
                       "Under MIT License\n");
        return 0;
    }
    printf("%s%s%s\nCopyright (c) Tuomo Heino\nVersion 1.0\n", CLR_CYAN, logo, CLR_CLEAR);
    time_t t;
    srand((unsigned) time(&t));
    gst_init(NULL, NULL);
    print_gstream_info();
    printf("\n");

    song_library = malloc((file_count + 1) * sizeof(char *));
    if (song_library == NULL) {
        printf("Malloc failed exiting!");
        return EXIT_FAILURE;
    }

    parse_args(argc, argv);

    if (argc == 1) {
        printf("Use scan command to add songs!\nType help for more commands\n");
    }
    if (start_gobj_thread() != 0) {
        printf("Unable to start gobject main loop.");
        return EXIT_FAILURE;
    }
    player_interface();
    free_pipeline_if_needed();
    if (g_main_loop != NULL) {
        // Quit main loop
        g_main_loop_quit(g_main_loop);
    }
    pthread_join(gobj_thread, NULL);
    // BB library
    free(song_library);
    return EXIT_SUCCESS;
}

extern inline void parse_args(int argc, char **argv) {
    if (argc >= 3) {
        char *arg = argv[1];
        if (strcmp(arg, "-l") == 0 || strcmp(arg, "--library") == 0) {
            printf("Initial library scan!\n");
            for (int i = 2; i < argc; i++) {
                char *param = argv[i];
                if (strlen(param) == 0 || param[0] == '-') {
                    break;
                }
                printf("%s\n", param);
                new_files = 0;
                if (walk_dir_tree(param)) {
                    fprintf(stderr, "%s.\n", strerror(errno));
                } else if (new_files > 0) {
                    printf("%d new songs added!\n", new_files);
                }
            }
        }
    }
}


void free_pipeline_if_needed() {
    if (pipeline != NULL) {
        now_playing = NULL;
        gst_element_set_state(pipeline, GST_STATE_NULL);
        gst_object_unref(pipeline);
        g_source_remove(bus_watch_id);
        pipeline = NULL;
    }
}

void set_volume() {
    if (pipeline != NULL) {
        double set_vol = volume / 100.0;
        g_object_set(pipeline, "volume", set_vol, NULL);
    }
}

int get_rnd_int(int max) {
    return rand() % max;
}

void set_next_song() {
    switch (play_mode) {
        case PLAY_MODE_RANDOM:
            last_sel = get_rnd_int(file_count);
            break;
        case PLAY_MODE_SINGLE:
            // Nothing to do
            break;
        case PLAY_MODE_NORMAL:
        default:
            last_sel++;
            break;
    }
}

void set_prev_song() {
    switch (play_mode) {
        case PLAY_MODE_RANDOM:
            last_sel = get_rnd_int(file_count);
            break;
        case PLAY_MODE_SINGLE:
            // Nothing to do
            break;
        case PLAY_MODE_NORMAL:
        default:
            last_sel--;
            break;
    }
}

extern inline void pause_playback() {
    if (pipeline != NULL) {
        gst_element_set_state(pipeline, GST_STATE_PAUSED);
    }
}

extern inline void resume_playback() {
    if (pipeline != NULL) {
        gst_element_set_state(pipeline, GST_STATE_PLAYING);
    }
}

void play_last_sel(int print_info) {
    if (last_sel >= file_count) {
        last_sel = 0;
    }
    if (last_sel < 0) {
        last_sel = file_count - 1;
    }
    char *resolved = 0;
    char *play = realpath(song_library[last_sel], resolved);
    play_file(play, print_info);
}

extern inline int add_entry(const char *filepath) {
    // 0 results are matching so flip
    if (!fnmatch("*.+(mp3|flac|wav|wma|m4u|aac|ogg)", filepath, FNM_EXTMATCH)) {
        char **temp = realloc(song_library, (file_count + 1) * sizeof(char *));
        if (temp == NULL) {
            printf("Memory allocation failed!\n");
            free(song_library);
            return 1;
        }
        song_library = temp;

        char *file = malloc(strlen(filepath) + 1);
        strcpy(file, filepath);

        song_library[file_count] = file;
        file_count++;
        new_files++;
    }
    return 0;
}

int get_music_files(const char *filepath, const struct stat *info, int typeflag, struct FTW *pathinfo) {
    switch (typeflag) {
        case FTW_F: // File
        case FTW_D: // Directory
        case FTW_DP: // Directory, all subs visited
            return add_entry(filepath);
        default:
            return 0;
    }
}


int walk_dir_tree(const char *const dirpath) {
    int result;
    /* Invalid directory path? */
    if (dirpath == NULL || *dirpath == '\0')
        return errno = EINVAL;

    result = nftw(dirpath, get_music_files, USE_FDS, FTW_PHYS);
    if (result >= 0)
        errno = result;

    return errno;
}

gboolean bus_callback(GstBus *bus, GstMessage *message, gpointer data) {
    if (GST_MESSAGE_TYPE(message) == GST_MESSAGE_EOS) {
        set_next_song();
        play_last_sel(FALSE);
    }
    return TRUE;
}

void play_file(char *play, int print_info) {
    assert(play != NULL);
    char *base = basename(play);
    if (print_info) {
        printf("%sPlaying: %s%s\n", CLR_GREEN, base, CLR_CLEAR);
    }
    char *play_bin = "playbin uri=\"file://";
    char *esc = "\"";
    char *play_file = malloc(strlen(play_bin) + strlen(play) + strlen(esc) + 1);

    strcpy(play_file, play_bin);
    strcat(play_file, play);
    strcat(play_file, esc);

    free_pipeline_if_needed();
    now_playing = base;
    pipeline = gst_parse_launch(play_file, NULL);
    free(play_file);

    GstBus *bus = gst_pipeline_get_bus(GST_PIPELINE (pipeline));
    bus_watch_id = gst_bus_add_watch(bus, bus_callback, NULL);
    gst_object_unref(bus);

    /* Start playing */
    gst_element_set_state(pipeline, GST_STATE_PLAYING);
    set_volume();
}

int read_int() {
    int integer;
    int res = scanf("%d", &integer);
    if (res == EOF) return -1;
    getchar(); // EOL read here to eat line change
    return integer;
}

extern inline int select_file() {
    printf("%sSelect song%s (%s0 - %d%s) >", CLR_YELLOW, CLR_CLEAR, CLR_GREEN, file_count - 1, CLR_CLEAR);
    int song = read_int();
    if (song == NO_SONG) return NO_SONG;
    if (song >= 0 && song < file_count) {
        return song;
    } else {
        printf("No such song!\n");
    }
    return NO_SONG;
}

char *read_in() {
    char *in = NULL;
    char cmd[32] = {0};
    in = fgets(cmd, 32 - 1, stdin);
    if (in != NULL && cmd[0] != '\0') {
        char *str = malloc(strlen(cmd));
        strcpy(str, cmd);
        size_t len = strlen(str);
        str[len - 1] = '\0';
        return str;
    }
    return NULL;
}

/**
 * Checks if given input matches command
 * Will free input if matches
 * @param input input
 * @param cmd command to match
 * @param read if command was read and no further commands need to be processed
 * @return read == 0
 */
int check_command(char *input, char *cmd) {
    if (strcmp(input, cmd) == 0) {
        free(input);
        return TRUE;
    }
    return FALSE;
}

extern inline void scan_cmd() {
    printf("Directory to scan >");
    char *folder = read_in();
    if (folder != NULL) {
        printf("Scanning '%s'\n", folder);
        new_files = 0;
        if (walk_dir_tree(folder)) {
            printf("%s%s.%s\n", CLR_RED, strerror(errno), CLR_CLEAR);
        } else if (new_files > 0) {
            printf("%d new songs added!\n", new_files);
        }
        free(folder);
    }
}

extern inline void vol_cmd() {
    printf("Enter volume (0-100) >");
    int new_vol = read_int();
    if (new_vol != -1 && new_vol >= 0 && new_vol <= 100) {
        volume = new_vol;
        set_volume();
    }
}

extern inline void play_cmd() {
    if (file_count == 0) {
        printf("No songs to play!\n");
    } else {
        int song = select_file();
        if (song != NO_SONG) {
            last_sel = song;
            char *resolved = 0;
            char *play = realpath(song_library[song], resolved);
            play_file(play, TRUE);
        }
    }
}

extern inline void stop_cmd() {
    free_pipeline_if_needed();
}

extern inline void next_cmd() {
    if (file_count == 0) {
        printf("No songs to play!\n");
        return;
    }
    set_next_song();
    play_last_sel(TRUE);
}

extern inline void prev_cmd() {
    if (file_count == 0) {
        printf("No songs to play!\n");
        return;
    }
    set_prev_song();
    play_last_sel(TRUE);
}

extern inline void now_cmd() {
    if (file_count == 0 || now_playing == NULL) {
        printf("No song playing!\n");
    } else {
        printf("%sPlaying: %s%s\n", CLR_GREEN, now_playing, CLR_CLEAR);
    }
}

void print_mode_cmd() {
    char *mode;
    switch (play_mode) {
        case PLAY_MODE_RANDOM:
            mode = "Random";
            break;
        case PLAY_MODE_SINGLE:
            mode = "Single";
            break;
        case PLAY_MODE_NORMAL:
        default:
            mode = "Normal";
            break;
    }
    printf("Playback mode: %s\n", mode);
}

void chande_mode_cmd(int mode) {
    play_mode = mode;
    print_mode_cmd();
}

/**
 * Main Interface loop of Viola
 * Handles asking input and parsing it
 */
extern inline void player_interface() {
    while (1) {
        printf("%sViola%s >", CLR_BLUE, CLR_CLEAR);
        char *input = read_in();
        if (input != NULL) {
            if (check_command(input, "EXIT")) {
                printf("Bye bye!\n");
                return;
            }
            if (check_command(input, "scan")) {
                scan_cmd();
            } else if (check_command(input, "vol")) {
                vol_cmd();
            } else if (check_command(input, "play")) {
                play_cmd();
            } else if (check_command(input, "stop")) {
                stop_cmd();
            } else if (check_command(input, "next")) {
                next_cmd();
            } else if (check_command(input, "prev")) {
                prev_cmd();
            } else if (check_command(input, "pause")) {
                pause_playback();
            } else if (check_command(input, "resume")) {
                resume_playback();
            } else if (check_command(input, "now")) {
                now_cmd();
            } else if (check_command(input, "normal")) {
                chande_mode_cmd(PLAY_MODE_NORMAL);
            } else if (check_command(input, "random")) {
                chande_mode_cmd(PLAY_MODE_RANDOM);
            } else if (check_command(input, "single")) {
                chande_mode_cmd(PLAY_MODE_SINGLE);
            } else if (check_command(input, "mode")) {
                print_mode_cmd();
            } else if (check_command(input, "help")) {
                printf("%s\n", HELP);
            } else {
                printf("%sUnrecognized command %s'%s%s%s'\n", CLR_RED, CLR_CLEAR, CLR_MAGENTA, input, CLR_CLEAR);
                free(input);
            }
        }
    }
}