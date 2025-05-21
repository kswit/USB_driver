#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <string.h>
 
#define MAX_DATA_LEN 64

typedef struct {
    uint8_t  bmRequestType;
    uint8_t  bRequest;
    uint16_t wValue;
    uint16_t wIndex;
    uint16_t wLength;
    uint8_t  data[MAX_DATA_LEN];
} UsbRequest;

void parse_hex_string(const char* hex, uint8_t* buffer, size_t* length) {
    *length = 0;
    while (*hex) {
        if (*hex == ' ') {
            hex++;
            continue;
        }
        if (strlen(hex) < 2) break;
        sscanf(hex, "%2hhx", &buffer[*length]);
        (*length)++;
        hex += 2;
    }
}

int parse_request_file(const char* filename, UsbRequest* requests, int max_reqs) {
    FILE* f = fopen(filename, "r");
    if (!f) {
        perror("fopen");
        return -1;
    }

    char line[256];
    UsbRequest current = {0};
    int count = 0;

    while (fgets(line, sizeof(line), f)) {
        if (strncmp(line, "    bmRequestType:", 18) == 0) {
            sscanf(line, "    bmRequestType: 0x%hhx", &current.bmRequestType);
        } else if (strncmp(line, "    bRequest:", 13) == 0) {
            sscanf(line, "    bRequest: %hhu", &current.bRequest);
        } else if (strncmp(line, "    wValue:", 11) == 0) {
            sscanf(line, "    wValue: 0x%hx", &current.wValue);
        } else if (strncmp(line, "    wIndex:", 11) == 0) {
            sscanf(line, "    wIndex: %*u (0x%hx)", &current.wIndex);
        } else if (strncmp(line, "    wLength:", 12) == 0) {
            sscanf(line, "    wLength: %hu", &current.wLength);
        } else if (strncmp(line, "    Data Fragment:", 18) == 0) {
            char* hex_data = strchr(line, ':');
            if (hex_data && ++hex_data) {
                size_t data_len = 0;
                parse_hex_string(hex_data, current.data, &data_len);
                current.wLength = data_len;
                if (count < max_reqs) {
                    requests[count++] = current;
                }
                memset(&current, 0, sizeof(UsbRequest));
            }
        }
    }

    fclose(f);
    return count;
}
