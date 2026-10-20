/* SPDX-License-Identifier: GPL-3.0-or-later
 * Notification ABI follows ps5-payload-dev/sdk samples/hello_world (John Tornblom).
 */
#include <stdio.h>
#include <string.h>
#include <time.h>

struct notification { char reserved[45]; char message[3075]; };
int sceKernelSendNotificationRequest(int, struct notification *, size_t, int);

int main(void) {
    struct notification n = {0};
    strcpy(n.message, "Muse: hello from your PS5");
    printf("{\"ts\":%lld,\"level\":\"info\",\"event\":\"boot\","
           "\"detail\":\"Muse hello-world; hardware result unverified\"}\n",
           (long long)time(NULL));
    fflush(stdout);
    int rc = sceKernelSendNotificationRequest(0, &n, sizeof(n), 0);
    printf("{\"ts\":%lld,\"level\":\"%s\",\"event\":\"notification\","
           "\"detail\":\"return=%d\"}\n", (long long)time(NULL),
           rc < 0 ? "error" : "info", rc);
    fflush(stdout);
    return rc < 0 ? 1 : 0;
}
