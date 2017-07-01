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

#define CLR_RED     "\x1b[31m"
#define CLR_GREEN   "\x1b[32m"
#define CLR_YELLOW  "\x1b[33m"
#define CLR_BLUE    "\x1b[34m"
#define CLR_MAGENTA "\x1b[35m"
#define CLR_CYAN    "\x1b[36m"
#define CLR_CLEAR   "\x1b[0m"

const char *HELP =
        "Commands:\n"
                "help - Prinst this help\n"
                "play - Plays song\n"
                "stop - Stops playback\n"
                "next - Plays next song, loops around\n"
                "prev - Plays previous song, loops around\n"
                "pause - Pauses playback\n"
                "resume - Resumes playback";

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

int file_count = 0;
int new_files = 0;
int volume = 75;
int last_sel = 0;
GstElement *pipeline = NULL;


void parse_args(int argc, char **argv);

void free_pipeline_if_needed();

void play_file(char *play);

void set_volume();

void pause_playback();

void resume_playback();

void play_last_sel();

int add_entry(const char *filepath);

int get_music_files(const char *filepath, const struct stat *info, int typeflag, struct FTW *pathinfo);

int walk_dir_tree(const char *const dirpath);

gboolean bus_callback(GstBus *bus, GstMessage *message, gpointer data);

int read_int();

int select_file();

char *read_in();

void player_interface();

int only_version(int argc, char **argv) {
    return argc == 2 && (strcmp(argv[1], "-v") == 0 || strcmp(argv[1], "--version") == 0);
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
    printf("%s%s%s\nCopyright (c) Tuomo Heino\nVersion 1.0\n\n", CLR_CYAN, logo, CLR_CLEAR);
    song_library = malloc((file_count + 1) * sizeof(char *));
    if (song_library == NULL) {
        printf("Malloc failed exiting!");
        return EXIT_FAILURE;
    }

    parse_args(argc, argv);
    gst_init(&argc, &argv);

    if (argc == 1) {
        printf("Use scan command to add songs!\nType help for more commands\n");
    }
    player_interface();
    free_pipeline_if_needed();
    free(song_library);
    return EXIT_SUCCESS;
}

void parse_args(int argc, char **argv) {
    if (argc < 3) return;
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


void free_pipeline_if_needed() {
    if (pipeline != NULL) {
        gst_element_set_state(pipeline, GST_STATE_NULL);
        gst_object_unref(pipeline);
        pipeline = NULL;
    }
}

void set_volume() {
    if (pipeline != NULL) {
        double set_vol = volume / 100.0;
        g_object_set(pipeline, "volume", set_vol, NULL);
    }
}

void pause_playback() {
    if (pipeline != NULL) {
        gst_element_set_state(pipeline, GST_STATE_PAUSED);
    }
}

void resume_playback() {
    if (pipeline != NULL) {
        gst_element_set_state(pipeline, GST_STATE_PLAYING);
    }
}

void play_last_sel() {
    if (last_sel >= file_count) {
        last_sel = 0;
    }
    if (last_sel < 0) {
        last_sel = file_count - 1;
    }
    char *resolved = 0;
    char *play = realpath(song_library[last_sel], resolved);
    play_file(play);
}

int add_entry(const char *filepath) {
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
        last_sel++;
        play_last_sel();
    }
    return TRUE;
}

void play_file(char *play) {
    assert(play != NULL);
    char* base = basename(play);
    printf("%sPlaying: %s%s\n", CLR_GREEN, base, CLR_CLEAR);
    char *play_bin = "playbin uri=\"file://";
    char *esc = "\"";
    char *play_file = malloc(strlen(play_bin) + strlen(play) + strlen(esc) + 1);

    strcpy(play_file, play_bin);
    strcat(play_file, play);
    strcat(play_file, esc);

    free_pipeline_if_needed();
    pipeline = gst_parse_launch(play_file, NULL);
    free(play_file);

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

int select_file() {
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

void player_interface() {
    while (1) {
        printf("%sViola%s >", CLR_BLUE, CLR_CLEAR);
        char *input = read_in();
        if (input != NULL) {
            int cmd_read = 1;
            if (strcmp(input, "EXIT") == 0) {
                printf("Bye bye!\n");
                free(input);
                return;
            }

            if (strcmp(input, "scan") == 0) {
                cmd_read = 0;
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
            if (cmd_read && strcmp(input, "vol") == 0) {
                cmd_read = 0;
                printf("Enter volume (0-100) >");
                int new_vol = read_int();
                if (new_vol != -1 && new_vol >= 0 && new_vol <= 100) {
                    volume = new_vol;
                    set_volume();
                }
            }
            if (cmd_read && strcmp(input, "help") == 0) {
                cmd_read = 0;
                printf("%s\n", HELP);
            }
            if (cmd_read && strcmp(input, "play") == 0) {
                cmd_read = 0;
                if (file_count == 0) {
                    printf("No songs to play!\n");
                    continue;
                }
                int song = select_file();
                if (song != NO_SONG) {
                    last_sel = song;
                    char *resolved = 0;
                    char *play = realpath(song_library[song], resolved);
                    play_file(play);
                }
            }
            if (cmd_read && strcmp(input, "stop") == 0) {
                cmd_read = 0;
                free_pipeline_if_needed();
            }
            if (cmd_read && strcmp(input, "next") == 0) {
                cmd_read = 0;
                if (file_count == 0) {
                    printf("No songs to play!\n");
                    continue;
                }
                last_sel++;
                play_last_sel();
            }
            if (cmd_read && strcmp(input, "prev") == 0) {
                cmd_read = 0;
                if (file_count == 0) {
                    printf("No songs to play!\n");
                    continue;
                }
                last_sel--;
                play_last_sel();
            }
            if (cmd_read && strcmp(input, "pause") == 0) {
                cmd_read = 0;
                pause_playback();
            }
            if (cmd_read && strcmp(input, "resume") == 0) {
                cmd_read = 0;
                resume_playback();
            }
            if (cmd_read) {
                printf("%sUnrecognized command %s'%s%s%s'\n", CLR_RED, CLR_CLEAR, CLR_MAGENTA, input, CLR_CLEAR);
            }
            free(input);
        }
    }
}