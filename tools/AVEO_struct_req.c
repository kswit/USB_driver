#include <libusb-1.0/libusb.h>
#include <stdio.h>
#include <stdint.h>
#include "usb_parser.h"
#define NUMREQ 156  // Zmienna wskazująca liczbę żądań w tablicy req




struct request {
    uint8_t bmRequestType;
    uint8_t bRequest;
    uint16_t wValue;
    uint16_t wIndex;
    uint16_t wLength; 
    uint8_t  unk2;
    int timeout;
};

struct request  req[1]={0x42, 4, 0x0000, 0x0001, 0, 32, 1000};
/*struct request req[NUMREQ] = {
    {0x42, 4, 0x0000, 0x0001, 32, 32, 1000},
   {0x42, 5, 0x0000, 0x0001, 0, 0, 10000},
   {0x42, 50, 0x0500, 0x1400,0, 0, 10000},
    {0x42, 113, 0x0005, 0x0001, 0, 0, 1000},
    {0x42, 112, 0x0004, 0x0001, 0, 0, 1000},
    {0x42, 110, 0x0003, 0x0000, 0, 0, 1000},
    {0x42, 95, 0x0000, 0x0000, 0, 0, 1000},
    {0x42, 108, 0x0032, 0x0002, 0, 0, 1000},
    {0x42, 94, 0x0000, 0x0000, 0, 0, 1000},
    {0x42, 143, 0x0000, 0x0000, 0, 0, 1000},
    {0x42, 144, 0x0000, 0x0000, 0, 0, 1000},
    {0x42, 114, 0x0000, 0x0001, 0, 0, 1000},
    {0x42, 94, 0x0000, 0x0000, 0, 0, 1000},
    {0x42, 93, 0x0000, 0x0000, 0, 0, 1000},
    {0x42, 92, 0x0000, 0x0000, 0, 0, 1000},
    {0x42, 50, 0x0500, 0x1400, 0, 0, 1000},
    {0x01, 0x0B, 5, 0, 0, 0, 1000},
    {0x42, 5, 0x0000, 0x0001, 0, 0, 1000},
    {0xc2, 1, 0x0000, 0x933f, 1, 64, 1000},
    {0xc2, 1, 0x0000, 0x933e, 1, 64, 1000}
};
*/
void end_req(libusb_device_handle *handle){
int res = libusb_control_transfer(handle,0x42,5,0x0000,0x0001,0,0,1000);
    libusb_reset_device(handle);
                                      
}


void usb_send_requests(libusb_device_handle *handle,UsbRequest *reqs) {
    unsigned char data[32];// Bufor do odbioru danych z urządzenia
                              
    int transferred;
    int res;
     memcpy(data, reqs->data, 32);
    // Przykład: Claim interfejs (w tym przypadku interfejs 0)
    int interface_number = 0;
    res = libusb_claim_interface(handle, interface_number);
    if (res < 0) {
        fprintf(stderr, "Nie udało się przydzielić interfejsu %d: %s\n", interface_number, libusb_error_name(res));
        return;
    }

    // Iterowanie przez wszystkie żądania
    //for (int i = 0; i < 1; i++) {
        struct request req_item;
        req_item.bmRequestType = req[0].bmRequestType;
        req_item.bRequest      = req[0].bRequest;
        req_item.wValue        = reqs->wValue;
        req_item.wIndex        = reqs->wIndex ;
        req_item.wLength       = reqs->wLength ;
        req_item.timeout       = 1000;  // lub z innego źródła
        //memcpy(data, reqs->data,sizeof(reqs->data) );
        
        //req_item->wValue=req->wValue;
        
        printf("Wysyłanie żądania %d (bmRequestType: 0x%02x, bRequest: %d,wIndex 0x%02x)...\ndata:%s\n", 
               1, req_item.bmRequestType, req_item.bRequest,req_item.wIndex,data);

        // Wysłanie żądania kontrolnego
        res = libusb_control_transfer(handle,
                                      req_item.bmRequestType,
                                      req_item.bRequest,
                                      req_item.wValue,
                                      req_item.wIndex,
                                      data,
                                      sizeof(data),
                                      req_item.timeout);
         if (res < 0) {
            // Obsługuje błędy LIBUSB_ERROR_XXX
            switch (res) {
                case LIBUSB_ERROR_PIPE:
                    printf("Błąd transferu: LIBUSB_ERROR_PIPE (Potencjalny problem z końcówką urządzenia)\n");
                    break;
                case LIBUSB_ERROR_TIMEOUT:
                    printf("Błąd transferu: LIBUSB_ERROR_TIMEOUT (Przekroczono limit czasu)\n");
                    break;
                case LIBUSB_ERROR_OVERFLOW:
                    printf("Błąd transferu: LIBUSB_ERROR_OVERFLOW (Bufor przepełniony)\n");
                    break;
                default:
                    printf("Błąd transferu: %d (Kod błędu: %d)\n", res, res);
                    break;
            }
        } else {
            // Transfer zakończony sukcesem
            printf("Transfer zakończony sukcesem. Wysłano %d bajtów.\n", res);
        }
 

// ✅ Logowanie danych, tylko dla IN (Device-to-Host)
        if ((req_item.bmRequestType & LIBUSB_ENDPOINT_IN) == LIBUSB_ENDPOINT_IN) {
            printf("Odebrano dane (%d bajtów): ", res);
            for (int j = 0; j < res; j++) {
                printf("%02x ", data[j]);
            }
            printf("\n");
        }
    




   

    // Zwolnienie interfejsu po zakończeniu
    res = libusb_release_interface(handle, interface_number);
    if (res < 0) {
        fprintf(stderr, "Nie udało się zwolnić interfejsu %d: %s\n", interface_number, libusb_error_name(res));
    }
}

int main() {
     UsbRequest reqs[NUMREQ];
    int n = parse_request_file("AVEO_PAKIET_DATA.txt", reqs, NUMREQ);

    libusb_context *ctx;
    libusb_device_handle *handle;

    // Inicjalizacja libusb
    if (libusb_init(&ctx) < 0) {
        fprintf(stderr, "Błąd inicjalizacji libusb\n");
        return 1;
    }

    // Otwieranie urządzenia (VID i PID należy ustawić w zależności od urządzenia)
    handle = libusb_open_device_with_vid_pid(ctx, 0x1871, 0x01b0);  // Przykładowe VID i PID

    if (handle == NULL) {
        fprintf(stderr, "Nie udało się otworzyć urządzenia USB\n");
        libusb_exit(ctx);
        return 1;
    }

    // Ustawienie konfiguracji urządzenia (przykład: 1)
    if (libusb_set_configuration(handle, 1) < 0) {
        fprintf(stderr, "Błąd ustawienia konfiguracji\n");
        libusb_close(handle);
        libusb_exit(ctx);
        return 1;
    }

    // Żądanie kontrolne
    for (int i = 0; i < n; ++i) {
    
    usb_send_requests(handle,&reqs[i]);
     }
     end_req(handle);
    // Zwolnienie zasobów
    libusb_close(handle);
    libusb_exit(ctx);

    return 0;
}
