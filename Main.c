//
// Created by Borreg0 on 2/10/25
//

#include "Main.h"
#include <stdio.h>
#include <microhttpd.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <unistd.h>
#include <netdb.h>


#define PORT 8888
#define UPLOAD_DIR "./LFT-Uploads"

//READ HTML FILE
char* readHTMLfile(const char* fileName) {
    FILE* file = fopen(fileName, "r");
    if (!file) {
        printf("Could not open file %s\n", fileName);
        return NULL;
    }
    fseek(file, 0, SEEK_END);
    long length = ftell(file);
    fseek(file, 0, SEEK_SET);

    char* buffer = (char*)malloc(length+1);
    if (!buffer) {
        fclose(file);
        return NULL;
    }
    fread(buffer, 1, length, file);
    buffer[length] = '\0';
    fclose(file);

    return buffer;
}

//CONTROL UPLOAD STATE
typedef struct {
    FILE *file;
    int in_file_data;
    char boundary[100];
    char filename[256];
} UploadState;

//EXTRACT BOUNDARY FROM CONTENT-TYPE HEADER
char* extract_boundary(const char* content_type) {
    static char boundary[100];
    const char* boundary_start = strstr(content_type, "boundary=");
    if (boundary_start) {
        boundary_start += 9;
        strncpy(boundary, boundary_start, sizeof(boundary)-1);
        boundary[sizeof(boundary)-1] = '\0';

        if (boundary[0] == '"') {
            memmove(boundary, boundary+1, strlen(boundary));
            char* end_quote = strchr(boundary, '"');
            if (end_quote) *end_quote = '\0';
        }
        return boundary;
    }
    return NULL;
}

//XTRACT FILENAME
char* extract_filename(const char* data, size_t data_len) {
    static char filename[256];
    const char* filename_start = strstr(data, "filename=");
    if (filename_start) {
        filename_start += 9;

        if (*filename_start == '"') filename_start++;

        const char* filename_end = strchr(filename_start, '"');
        if (!filename_end) {

            filename_end = strchr(filename_start, ';');
            if (!filename_end) {
                filename_end = strchr(filename_start, '\r');
            }
            if (!filename_end) {
                filename_end = strchr(filename_start, '\n');
            }
        }

        if (filename_end) {
            size_t len = filename_end - filename_start;
            if (len > sizeof(filename)-1) len = sizeof(filename)-1;
            strncpy(filename, filename_start, len);
            filename[len] = '\0';
            return filename;
        }
    }
    return NULL;
}

//PROCESS MULTIPART DATA
void process_multipart_data(UploadState *state, const char *data, size_t data_len) {
    if (!state->in_file_data) {
        //LOOK START OF FILE AFTER HEADERS
        const char *header_end = strstr(data, "\r\n\r\n");
        if (header_end) {
            size_t header_len = header_end - data + 4;
            const char *file_data_start = data + header_len;
            size_t file_data_len = data_len - header_len;

            //EXTRACT FILENAME HEADER
            if (state->filename[0] == '\0') {
                char *found_filename = extract_filename(data, header_len);
                if (found_filename) {
                    strncpy(state->filename, found_filename, sizeof(state->filename)-1);

                    //CREATING FILE
                    char filepath[512];
                    snprintf(filepath, sizeof(filepath), "%s/%s", UPLOAD_DIR, state->filename);
                    state->file = fopen(filepath, "wb");
                    if (state->file) {
                        printf("Starting upload: %s\n", state->filename);
                        state->in_file_data = 1;

                        //WRITE DATA
                        if (file_data_len > 0) {
                            size_t write_len = file_data_len;
                            if (file_data_len > 4) {
                                const char *end = file_data_start + file_data_len - 4;
                                if (memcmp(end, "\r\n--", 4) == 0) {
                                    write_len -= 4;
                                }
                            }
                            if (write_len > 0) {
                                fwrite(file_data_start, 1, write_len, state->file);
                                printf("Wrote %zu bytes\n", write_len);
                            }
                        }
                    }
                }
            }
        }
    } else {
        //WRITING FINAL DATA UNTIL BOUNDARIES
        const char *boundary_start = strstr(data, "\r\n--");
        if (boundary_start) {
            size_t file_data_len = boundary_start - data;
            if (file_data_len > 0) {
                fwrite(data, 1, file_data_len, state->file);
                printf("Wrote final %zu bytes\n", file_data_len);
            }

            //CLOSE FILE
            fclose(state->file);
            state->file = NULL;
            state->in_file_data = 0;
            printf("Finished upload: %s\n", state->filename);
            state->filename[0] = '\0';
        } else {
            size_t write_len = data_len;
            if (data_len >= 2) {
                if (memcmp(data + data_len - 2, "\r\n", 2) == 0) {
                    write_len -= 2;
                }
            }
            if (data_len >= 4) {
                if (memcmp(data + data_len - 4, "\r\n--", 4) == 0) {
                    write_len -= 4;
                }
            }

            if (write_len > 0) {
                fwrite(data, 1, write_len, state->file);
                printf("Wrote %zu bytes\n", write_len);
            }
        }
    }
}

enum MHD_Result ConnectionAnswer(void *cls, struct MHD_Connection *connection, const char *url,
    const char *method, const char *version, const char *uploadData, size_t *uploadDataSize, void **req_cls) {

    const char *page = readHTMLfile("index.html");
    struct MHD_Response *response;
    enum MHD_Result ret;

    //HANDLE POST REQUEST FOR FILE UPLOAD
    if (strcmp(method, "POST") == 0) {
        if (*req_cls == NULL) {
            //FIRST POST CALL, INITIALIZE
            UploadState *state = malloc(sizeof(UploadState));
            if (!state) return MHD_NO;

            state->file = NULL;
            state->in_file_data = 0;
            state->filename[0] = '\0';
            state->boundary[0] = '\0';

            //GET BOUNDARY OF CONTENT-TYPE
            const char *content_type = MHD_lookup_connection_value(connection, MHD_HEADER_KIND, "Content-Type");
            if (content_type) {
                char *boundary = extract_boundary(content_type);
                if (boundary) {
                    strncpy(state->boundary, boundary, sizeof(state->boundary)-1);
                }
            }

            *req_cls = state;
            return MHD_YES;
        }

        UploadState *state = (UploadState*)*req_cls;

        if (*uploadDataSize != 0) {
            process_multipart_data(state, uploadData, *uploadDataSize);
            *uploadDataSize = 0;
            return MHD_YES;
        } else {
            //SUCCESS
            if (state->file != NULL || state->filename[0] != '\0') {
                // Clean up
                if (state->file) {
                    fclose(state->file);
                }
                free(state);
                *req_cls = NULL;

                //SEND THE SUCCESS RESPINSE
                const char *success_page = "<html><body><h2>Upload Successful!</h2><a href='/'>Go back</a></body></html>";
                response = MHD_create_response_from_buffer(strlen(success_page),
                            (void*)success_page, MHD_RESPMEM_PERSISTENT);
                MHD_add_response_header(response, "Content-Type", "text/html");
                ret = MHD_queue_response(connection, MHD_HTTP_OK, response);
                MHD_destroy_response(response);
                return ret;
            } else {
                //NO FILE PROCESSED
                free(state);
                *req_cls = NULL;
            }
        }
    }

    //HANDLE ERRORS (EMPTY UPLOAD OR GET ERROR)
    if (page == NULL) {
        const char *error_page = "<html><body>Error: Page not loaded</body></html>";
        response = MHD_create_response_from_buffer(strlen(error_page),
                    (void*)error_page, MHD_RESPMEM_PERSISTENT);
    } else {
        response = MHD_create_response_from_buffer(strlen(page),
                    (void*)page, MHD_RESPMEM_MUST_FREE);
    }

    MHD_add_response_header(response, "Content-Type", "text/html; charset=utf-8");
    ret = MHD_queue_response(connection, MHD_HTTP_OK, response);
    MHD_destroy_response(response);
    return ret;
}

void uploadDir() {
    struct stat st = {0};
    if (stat(UPLOAD_DIR, &st) == -1) {
        mkdir(UPLOAD_DIR,0700);
    }
}

int main() {
    struct MHD_Daemon *daemon;
    char hostbuffer[256];
    char *IPbuffer;
    struct hostent *hostentry;
    int hostname;

    hostname = gethostname(hostbuffer, sizeof(hostbuffer));
    if (hostname == -1) {
        perror("gethostname");
        exit(1);
    }

    hostentry = gethostbyname(hostbuffer);
    if (hostentry == NULL) {
        perror("gethostbyname");
        exit(1);
    }

    IPbuffer = inet_ntoa(*((struct in_addr*)hostentry->h_addr_list[0]));
    if (IPbuffer == NULL) {
        perror("inet_ntoa");
        exit(1);
    }

    uploadDir();

    daemon = MHD_start_daemon(MHD_USE_INTERNAL_POLLING_THREAD, PORT, NULL, NULL,
                              &ConnectionAnswer, NULL, MHD_OPTION_END);
    if (NULL == daemon)
        return 1;

    printf("Server running on %s:%d\n", IPbuffer, PORT);
    printf("Press Enter to disconnect\n");
    getchar();

    MHD_stop_daemon(daemon);
    printf("Disconnected!\n");
    return 0;
}
