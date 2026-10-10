/* heure : secondes depuis 1970 (horloge de la machine) puis duree depuis le demarrage. */
#include "phoenos.h"

int main(void)
{
    puts("time()   = "); putu(time()); puts(" s depuis 1970\n");
    puts("uptime() = "); putu(uptime_ms()); puts(" ms depuis le demarrage\n");
    puts("pause de 200 ms... ");
    sleep_ms(200);
    puts("fait.\n");
    return 0;
}
